/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#ifndef STF_CSI_H
#define STF_CSI_H

#include <linux/regulator/consumer.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>
#include <media/media-entity.h>
#include <video/stf-vin.h>

#define STF_CSI_NAME "stf_csi"

#define STF_CSI_PAD_SINK     0
#define STF_CSI_PAD_SRC      1
#define STF_CSI_PADS_NUM     2

struct csi_format {
	u32 code;
	u8 bpp;
};

struct stf_csi_dev;

struct csi_hw_ops {
	int (*csi_power_on)(struct stf_csi_dev *csi_dev, u8 on);
	int (*csi_clk_enable)(struct stf_csi_dev *csi_dev);
	int (*csi_clk_disable)(struct stf_csi_dev *csi_dev);
	int (*csi_set_format)(struct stf_csi_dev *csi_dev,
			u32 vsize, u8 bpp, int is_raw10);
	int (*csi_stream_set)(struct stf_csi_dev *csi_dev, int on);
};

struct stf_csi_dev {
	struct stfcamss *stfcamss;
	enum sensor_type s_type;
	struct v4l2_subdev subdev;
	struct media_pad pads[STF_CSI_PADS_NUM];
	struct v4l2_mbus_framefmt fmt[STF_CSI_PADS_NUM];
	const struct csi_format *formats;
	unsigned int nformats;
	struct csi_hw_ops *hw_ops;
	struct mutex stream_lock;
	int stream_count;
	struct regulator *mipirx_1p8;
	struct regulator *mipirx_0p9;
};

extern int stf_csi_subdev_init(struct stfcamss *stfcamss);
extern int stf_csi_register(struct stf_csi_dev *csi_dev,
			struct v4l2_device *v4l2_dev);
extern int stf_csi_unregister(struct stf_csi_dev *csi_dev);
extern struct csi_hw_ops csi_ops;
extern void dump_csi_reg(void *__iomem csibase);

#endif /* STF_CSI_H */
