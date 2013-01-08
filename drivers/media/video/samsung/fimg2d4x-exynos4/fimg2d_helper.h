/* linux/drivers/media/video/samsung/fimg2d4x/fimg2d_helper.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *	http://www.samsung.com/
 *
 * Samsung Graphics 2D driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __FIMG2D_HELPER_H
#define __FIMG2D_HELPER_H

#include <linux/sched.h>
#include "fimg2d.h"

static inline char *perfname(enum perf_desc id)
{
	switch (id) {
	case PERF_INNERCACHE:
		return "INNER$";
	case PERF_OUTERCACHE:
		return "OUTER$";
	case PERF_BLIT:
		return "BITBLT";
	default:
		return "";
	}
}

static inline char *imagename(enum image_object image)
{
	switch (image) {
	case IDST:
		return "DST";
	case ISRC:
		return "SRC";
	case IMSK:
		return "MSK";
	default:
		return NULL;
	}
}

static inline int is_opaque(enum color_format fmt)
{
	switch (fmt) {
	case CF_ARGB_8888:
	case CF_ARGB_1555:
	case CF_ARGB_4444:
		return 0;

	case CF_XRGB_8888:
	case CF_XRGB_1555:
	case CF_XRGB_4444:
		return 1;

	case CF_RGB_565:
	case CF_RGB_888:
		return 1;

	default:
		break;
	}

	return 1;
}

static inline unsigned int rect_w(struct fimg2d_rect *r)
{
	return r->x2 - r->x1;
}

static inline unsigned int rect_h(struct fimg2d_rect *r)
{
	return r->y2 - r->y1;
}

static inline long elapsed_usec(struct fimg2d_context *ctx, enum perf_desc desc)
{
	struct fimg2d_perf *perf = &ctx->perf[desc];
#ifdef PERF_TIMEVAL
	struct timeval *start = &perf->start;
	struct timeval *end = &perf->end;
	long sec, usec;

	sec = end->tv_sec - start->tv_sec;
	if (end->tv_usec >= start->tv_usec) {
		usec = end->tv_usec - start->tv_usec;
	} else {
		usec = end->tv_usec + 1000000 - start->tv_usec;
		sec--;
	}
	return sec * 1000000 + usec;
#else
	return (long)(perf->end - perf->start)/1000;
#endif
}

static inline void perf_start(struct fimg2d_context *ctx, enum perf_desc desc)
{
	struct fimg2d_perf *perf = &ctx->perf[desc];

	if (!perf->valid) {
#ifdef PERF_TIMEVAL
		struct timeval time;
		do_gettimeofday(&time);
		perf->start = time;
#else
		long time;
		perf->start = sched_clock();
		time = perf->start / 1000;
#endif
		perf->valid = 0x01;
	}
}

static inline void perf_end(struct fimg2d_context *ctx, enum perf_desc desc)
{
	struct fimg2d_perf *perf = &ctx->perf[desc];

	if (perf->valid == 0x01) {
#ifdef PERF_TIMEVAL
		struct timeval time;
		do_gettimeofday(&time);
		perf->end = time;
#else
		long time;
		perf->end = sched_clock();
		time = perf->end / 1000;
#endif
		perf->valid |= 0x10;
	}
}

static inline void perf_clear(struct fimg2d_context *ctx)
{
	int i;
	for (i = 0; i < MAX_PERF_DESCS; i++)
		ctx->perf[i].valid = 0;
}

int pixel2offset(int pixel, enum color_format cf);
int width2bytes(int width, enum color_format cf);
void perf_print(struct fimg2d_context *ctx, int seq_no);
void fimg2d_print_params(struct fimg2d_blit __user *u);
void fimg2d_dump_command(struct fimg2d_bltcmd *cmd);

#endif /* __FIMG2D_HELPER_H */
