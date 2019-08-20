/*
 * Copyright (c) 2001 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * video encoding with libavcodec API example
 *
 * @example encode_video.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libavdevice/avdevice.h>

#include <libavutil/opt.h>
#include <libavutil/imgutils.h>

static int alloc_avframe_buffer(AVFrame *frame, int align)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frame->format);
    int ret, i, padded_height;
    int plane_padding = FFMAX(16 + 16/*STRIDE_ALIGN*/, align);

    if (!desc)
        return AVERROR(EINVAL);

    if ((ret = av_image_check_size(frame->width, frame->height, 0, NULL)) < 0)
        return ret;

    if (!frame->linesize[0]) {
        if (align <= 0)
            align = 32; /* STRIDE_ALIGN. Should be av_cpu_max_align() */

        for(i=1; i<=align; i+=i) {
            ret = av_image_fill_linesizes(frame->linesize, frame->format,
                                          FFALIGN(frame->width, i));
            if (ret < 0)
                return ret;
            if (!(frame->linesize[0] & (align-1)))
                break;
        }

        for (i = 0; i < 4 && frame->linesize[i]; i++)
            frame->linesize[i] = FFALIGN(frame->linesize[i], align);
    }

    padded_height = FFALIGN(frame->height, 32);
    if ((ret = av_image_fill_pointers(frame->data, frame->format, padded_height,
                                      NULL, frame->linesize)) < 0)
        return ret;

    frame->buf[0] = av_buffer_alloc(ret + 4*plane_padding);
    if (!frame->buf[0]) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    frame->buf[1] = av_buffer_alloc(ret + 4*plane_padding);
    if (!frame->buf[1]) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    frame->data[0] = frame->buf[0]->data;
    frame->data[1] = frame->buf[1]->data;
    frame->data[2] = frame->data[1] + ((frame->width * padded_height) / 4);

    frame->extended_data = frame->data;

    return 0;
fail:
    av_frame_unref(frame);
    return ret;
}

static void flush_codec(AVCodecContext *enc_ctx, AVFormatContext *oc, AVPacket *pkt)
{
    int ret;
    ret = avcodec_send_frame(enc_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error flushing codec\n");
        exit(1);
    }

    while (ret != AVERROR_EOF) {
        av_init_packet(pkt);
        pkt->data = NULL;
        pkt->size = 0;
        ret = avcodec_receive_packet(enc_ctx, pkt);
        printf("received packet: code %d, pkt->flags: %d, pkt->pts: %lld, pkt->dts: %lld, pkt->size: %d\n", ret, pkt->flags, pkt->pts, pkt->dts, pkt->size);
        if (ret != AVERROR_EOF) {
            if (ret < 0) {
                fprintf(stderr, "Error draining codec\n");
                av_packet_unref(pkt);
                exit(1);
            }
            if ((pkt->pts > 0) && (pkt->size > 0)) {
                printf("Write packet %3"PRId64" (size=%5d)\n", pkt->pts, pkt->size);
                av_write_frame(oc, pkt);
            } else {
                ret = AVERROR_EOF;
            }
        }
        av_packet_unref(pkt);
    }
}

static void encode(AVCodecContext *enc_ctx, AVFormatContext *oc, AVFrame *frame, AVPacket *pkt, const char *filename)
{
    int ret;

    /* send the frame to the encoder */
    if (frame)
        printf("Send frame %3"PRId64"\n", frame->pts);

    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
        exit(1);
    }

    while (ret >= 0) {
        av_init_packet(pkt);
        pkt->data = NULL;
        pkt->size = 0;
        printf("Receiving packet...\n");
        ret = avcodec_receive_packet(enc_ctx, pkt);
        printf("received packet: code %d, pkt->flags: %d, pkt->pts: %lld, pkt->dts: %lld, pkt->size: %d\n", ret, pkt->flags, pkt->pts, pkt->dts, pkt->size);
        if (ret == AVERROR(EAGAIN)){
            //Buffered packet.
            av_packet_unref(pkt);
            return;
        }
        if (ret == AVERROR_EOF) {
            av_packet_unref(pkt);
            return;
        }
        if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
            av_packet_unref(pkt);
            exit(1);
        }

        if ((pkt->pts == 0) && (!(pkt->flags & AV_PKT_FLAG_KEY))) {
            printf("WARNING: First frame not keyframe ! : [ ");
            int i;
            for (i = 0; i < pkt->size; i++) {
                printf("%02x ", pkt->data[i]);
            }
            printf("]\n");
            //FILE *ptr = fopen(filename,"wb");
            //fwrite(pkt->data,pkt->size,1,ptr);
            //fclose(ptr);
        }
        //if ((pkt->pts > 0) || (pkt->flags & AV_PKT_FLAG_KEY)) {
            printf("Write packet %3"PRId64" (size=%5d)\n", pkt->pts, pkt->size);
            av_write_frame(oc, pkt);
        //}
        av_packet_unref(pkt);
    }
}


void log_ffmpeg(void *ignoreme, int errno_flag, const char *fmt, va_list vl) {

    char buf[1024];
    char *end;

    /* Flatten the message coming in from avcodec. */
    vsnprintf(buf, sizeof(buf), fmt, vl);
    end = buf + strlen(buf);
    if (end > buf && end[-1] == '\n')
    {
        *--end = 0;
    }

    printf("%s\n", buf);
}


int main(int argc, char **argv)
{
    const char *filename, *codec_name;
    const AVCodec *codec;
    AVFormatContext *oc=NULL;
    AVStream *video_st=NULL;
    AVCodecContext *c= NULL;
    int i, ret, x, y, gop_cnt;
    AVFrame *frame;
    AVPacket *pkt;

    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <output file> <codec name>\n", argv[0]);
        exit(0);
    }
    filename = argv[1];
    codec_name = argv[2];

    avformat_network_init();
    avdevice_register_all();
    av_log_set_callback((void *)log_ffmpeg);

    /* find the mpeg1video encoder */
    codec = avcodec_find_encoder_by_name(codec_name);
    if (!codec) {
        fprintf(stderr, "Codec '%s' not found\n", codec_name);
        exit(1);
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    pkt = av_packet_alloc();
    if (!pkt)
        exit(1);

    oc = avformat_alloc_context();
    oc->oformat = av_guess_format("mp4", NULL, NULL);
    if (oc->oformat)
        oc->oformat->video_codec = codec->id;
    else {
        fprintf(stderr, "Could not set output format: %s\n", "mp4");
        exit(1);
    }
    avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
    //f = fopen(filename, "wb");
    //if (!f) {
    //    fprintf(stderr, "Could not open %s\n", filename);
    //    exit(1);
    //}

    /* put sample parameters */
    c->bit_rate = 400000;
    /* resolution must be a multiple of two */
    c->width = 640; // multiple of 64
    c->height = 320;
    /* frames per second */
    c->time_base = (AVRational){1, 2};
    c->framerate = (AVRational){2, 1};

    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    c->gop_size = 3;
    gop_cnt = c->gop_size;
    //c->max_b_frames = 1;
    c->pix_fmt = AV_PIX_FMT_NV21;
    //c->pix_fmt = AV_PIX_FMT_YUV420P;
    c->codec_id = oc->oformat->video_codec;
    c->codec_type = AVMEDIA_TYPE_VIDEO;
    c->max_b_frames  = 0;
    c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (codec->id == AV_CODEC_ID_H264)
        av_opt_set(c->priv_data, "preset", "slow", 0);

    video_st = avformat_new_stream(oc, codec);
    avcodec_parameters_from_context(video_st->codecpar,c);
    video_st->time_base = (AVRational){1, 2};
    avformat_write_header(oc, NULL);

    int64_t one_frame_interval = av_rescale_q(1,(AVRational){1, 2},video_st->time_base);

    /* open it */
    ret = avcodec_open2(c, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open codec: %s\n", av_err2str(ret));
        exit(1);
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    frame->linesize[0] = c->width;
    frame->linesize[1] = c->width / 2;
    frame->linesize[2] = c->width / 2;
    frame->format = c->pix_fmt;
    frame->width  = c->width;
    frame->height = c->height;

    ret = alloc_avframe_buffer(frame, 32);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate frame buffers\n");
        exit(1);
    }

    /* encode 10 second of video */
    for (i = 0; i < 20; i++) {
        fflush(stdout);

        /* make sure the frame data is writable */
        ret = av_frame_make_writable(frame);
        if (ret < 0)
            exit(1);

        //gop_cnt++;
        //if (gop_cnt >= c->gop_size) {
        //    frame->pict_type = AV_PICTURE_TYPE_I;
        //    frame->key_frame = 1;
        //    gop_cnt = 0;
        //} else {
        //    frame->pict_type = AV_PICTURE_TYPE_P;
        //    frame->key_frame = 0;
        //}

        //https://www.infradead.org/~mchehab/kernel_docs/media/uapi/v4l/yuv-formats.html
        //https://www.infradead.org/~mchehab/kernel_docs/media/uapi/v4l/pixfmt-nv12.html

        //for (i = 0; i < avbuf->num_planes; i++)
        //    avbuf->plane_info[i].bytesperline


        /* prepare a dummy image */
        /* Y */
        // plane 1
        for (y = 0; y < c->height; y++) {
            for (x = 0; x < c->width; x++) {
                frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
            }
        }

        /* Cb and Cr */
        // plane 2
        for (y = 0; y < c->height; y++) {
            for (x = 0; x < c->width/4; x++) {
                frame->data[1][y * c->width/2 + x*2] = 128 + y + i * 2;
                frame->data[1][y * c->width/2 + x*2+1] = 64 + x + i * 5;
                //frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
                //frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
            }
        }

        frame->pts = (i+1)*one_frame_interval;

        /* encode the image */
        encode(c, oc, frame, pkt, filename);
    }

    /* flush the encoder */
    printf("Draining codec\n");
    flush_codec(c, oc, pkt);
    printf("Finished draining codec\n");

    /* add sequence end code to have a real MPEG file */
    av_write_trailer(oc);
    avio_close(oc->pb);
    avformat_free_context(oc);
    //if (codec->id == AV_CODEC_ID_MPEG1VIDEO || codec->id == AV_CODEC_ID_MPEG2VIDEO)
    //    fwrite(endcode, 1, sizeof(endcode), f);
    //fclose(f);

    avcodec_free_context(&c);
    av_frame_free(&frame);
    av_packet_free(&pkt);

    return 0;
}
