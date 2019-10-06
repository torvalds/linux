/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  FM Driver for Connectivity chip of Texas Instruments.
 *
 *  FM V4L2 module header.
 *
 *  Copyright (C) 2011 Texas Instruments
 */

#ifndef _FMDRV_V4L2_H
#define _FMDRV_V4L2_H

#include <media/v4l2-ioctl.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>

int fm_v4l2_init_video_device(struct fmdev *, int);
void *fm_v4l2_deinit_video_device(void);

#endif
