/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 - 2025 Intel Corporation
 */

#ifndef IPU7_ISYS_SUBDEV_H
#define IPU7_ISYS_SUBDEV_H

#include <linux/container_of.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

struct ipu7_isys;

struct ipu7_isys_subdev {
	struct v4l2_subdev sd;
	struct ipu7_isys *isys;
	u32 const *supported_codes;
	struct media_pad *pad;
	struct v4l2_ctrl_handler ctrl_handler;
	void (*ctrl_init)(struct v4l2_subdev *sd);
	int source;	/* SSI stream source; -1 if unset */
};

#define to_ipu7_isys_subdev(__sd)			\
	container_of(__sd, struct ipu7_isys_subdev, sd)
unsigned int ipu7_isys_mbus_code_to_mipi(u32 code);
bool ipu7_isys_is_bayer_format(u32 code);
u32 ipu7_isys_convert_bayer_order(u32 code, int x, int y);

int ipu7_isys_subdev_set_fmt(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_format *format);
int ipu7_isys_subdev_enum_mbus_code(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_mbus_code_enum
				    *code);
int ipu7_isys_get_stream_pad_fmt(struct v4l2_subdev *sd, u32 pad, u32 stream,
				 struct v4l2_mbus_framefmt *format);
int ipu7_isys_subdev_set_routing(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 enum v4l2_subdev_format_whence which,
				 struct v4l2_subdev_krouting *routing);
int ipu7_isys_subdev_init(struct ipu7_isys_subdev *asd,
			  const struct v4l2_subdev_ops *ops,
			  unsigned int nr_ctrls,
			  unsigned int num_sink_pads,
			  unsigned int num_source_pads);
void ipu7_isys_subdev_cleanup(struct ipu7_isys_subdev *asd);
#endif /* IPU7_ISYS_SUBDEV_H */
