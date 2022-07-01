/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#ifndef STF_DVP_H
#define STF_DVP_H

#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>
#include <media/media-entity.h>
#include <video/stf-vin.h>

#define STF_DVP_NAME "stf_dvp"

#define STF_DVP_PAD_SINK     0
#define STF_DVP_PAD_SRC      1
#define STF_DVP_PADS_NUM     2

struct dvp_format {
	u32 code;
	u8 bpp;
};

enum sensor_type;
enum subdev_type;

struct dvp_cfg {
	unsigned int flags;
	unsigned char bus_width;
	unsigned char data_shift;
};

struct stf_dvp_dev;

struct dvp_hw_ops {
	int (*dvp_clk_init)(struct stf_dvp_dev *dvp_dev);
	int (*dvp_config_set)(struct stf_dvp_dev *dvp_dev);
	int (*dvp_set_format)(struct stf_dvp_dev *dvp_dev,
			u32 pix_width, u8 bpp);
	int (*dvp_stream_set)(struct stf_dvp_dev *dvp_dev, int on);
};

struct stf_dvp_dev {
	struct stfcamss *stfcamss;
	struct dvp_cfg *dvp;
	u8 id;
	enum sensor_type s_type;
	struct v4l2_subdev subdev;
	struct media_pad pads[STF_DVP_PADS_NUM];
	struct v4l2_mbus_framefmt fmt[STF_DVP_PADS_NUM];
	const struct dvp_format *formats;
	unsigned int nformats;
	struct dvp_hw_ops *hw_ops;
	struct mutex stream_lock;
	int stream_count;
};

extern int stf_dvp_subdev_init(struct stfcamss *stfcamss);
extern int stf_dvp_register(struct stf_dvp_dev *dvp_dev,
			struct v4l2_device *v4l2_dev);
extern int stf_dvp_unregister(struct stf_dvp_dev *dvp_dev);

extern struct dvp_hw_ops dvp_ops;

#endif /* STF_DVP_H */
