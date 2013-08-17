/* linux/drivers/media/video/exynos/fimg2d/fimg2d_helper.h
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

#define rect_w(r)	((r)->x2 - (r)->x1)
#define rect_h(r)	((r)->y2 - (r)->y1)

#ifdef DEBUG
void fimg2d_perf_start(struct fimg2d_bltcmd *cmd, enum perf_desc desc);
void fimg2d_perf_end(struct fimg2d_bltcmd *cmd, enum perf_desc desc);
void fimg2d_perf_print(struct fimg2d_bltcmd *cmd);

static inline void perf_start(struct fimg2d_bltcmd *cmd, enum perf_desc desc)
{
	if (g2d_debug == DBG_PERF)
		fimg2d_perf_start(cmd, desc);
}

static inline void perf_end(struct fimg2d_bltcmd *cmd, enum perf_desc desc)
{
	if (g2d_debug == DBG_PERF)
		fimg2d_perf_end(cmd, desc);
}

static inline void perf_print(struct fimg2d_bltcmd *cmd)
{
	if (g2d_debug == DBG_PERF)
		fimg2d_perf_print(cmd);
}
#else
#define perf_start(cmd, desc)
#define perf_end(cmd, desc)
#define perf_print(cmd)
#endif

#ifdef DEBUG
void fimg2d_debug_command(struct fimg2d_bltcmd *cmd);
void fimg2d_debug_command_simple(struct fimg2d_bltcmd *cmd);

static inline void fimg2d_dump_command(struct fimg2d_bltcmd *cmd)
{
	if (g2d_debug == DBG_DEBUG)
		fimg2d_debug_command(cmd);
	else if (g2d_debug == DBG_ONELINE)
		fimg2d_debug_command_simple(cmd);
}
#else
#define fimg2d_dump_command(cmd)
#endif

#endif /* __FIMG2D_HELPER_H */
