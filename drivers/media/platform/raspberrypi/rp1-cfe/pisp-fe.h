/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PiSP Front End Driver
 *
 * Copyright (c) 2021-2024 Raspberry Pi Ltd.
 */
#ifndef _PISP_FE_H_
#define _PISP_FE_H_

#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/media-device.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include <linux/media/raspberrypi/pisp_fe_config.h>

enum pisp_fe_pads {
	FE_STREAM_PAD,
	FE_CONFIG_PAD,
	FE_OUTPUT0_PAD,
	FE_OUTPUT1_PAD,
	FE_STATS_PAD,
	FE_NUM_PADS
};

struct pisp_fe_device {
	/* Parent V4l2 device */
	struct v4l2_device *v4l2_dev;
	void __iomem *base;
	u32 hw_revision;

	u16 inframe_count;
	struct media_pad pad[FE_NUM_PADS];
	struct v4l2_subdev sd;
};

void pisp_fe_isr(struct pisp_fe_device *fe, bool *sof, bool *eof);
int pisp_fe_validate_config(struct pisp_fe_device *fe,
			    struct pisp_fe_config *cfg,
			    struct v4l2_format const *f0,
			    struct v4l2_format const *f1);
void pisp_fe_submit_job(struct pisp_fe_device *fe, struct vb2_buffer **vb2_bufs,
			struct pisp_fe_config *cfg);
void pisp_fe_start(struct pisp_fe_device *fe);
void pisp_fe_stop(struct pisp_fe_device *fe);
int pisp_fe_init(struct pisp_fe_device *fe, struct dentry *debugfs);
void pisp_fe_uninit(struct pisp_fe_device *fe);

#endif
