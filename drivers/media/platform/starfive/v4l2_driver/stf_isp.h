/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#ifndef STF_ISP_H
#define STF_ISP_H

#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/media-entity.h>
#include <video/stf-vin.h>

#define STF_ISP_NAME "stf_isp"

//#define ISP_USE_CSI_AND_SC_DONE_INTERRUPT  1
#define STF_ISP_PAD_SINK     0
#define STF_ISP_PAD_SRC      1
#define STF_ISP_PADS_NUM     2

#define STF_ISP0_SETFILE     "stf_isp0_fw.bin"
#define STF_ISP1_SETFILE     "stf_isp1_fw.bin"

#define SCALER_RATIO_MAX     1  // no compose function
#define STF_ISP_REG_OFFSET_MAX  0x0FFF
#define STF_ISP_REG_DELAY_MAX   100

#define ISP_REG_CSIINTS_ADDR    0x00000008
#define ISP_REG_DUMP_CFG_0      0x00000024
#define ISP_REG_DUMP_CFG_1      0x00000028
#define ISP_REG_IESHD_ADDR      0x00000A50

enum {
	EN_INT_NONE                 = 0,
	EN_INT_ISP_DONE             = (0x1 << 24),
	EN_INT_CSI_DONE             = (0x1 << 25),
	EN_INT_SC_DONE              = (0x1 << 26),
	EN_INT_LINE_INT             = (0x1 << 27),
	EN_INT_ALL                  = (0xF << 24),
};

struct isp_format {
	u32 code;
	u8 bpp;
};

struct regval_t {
	u32 addr;
	u32 val;
	u32 mask;
	u32 delay_ms;
};

struct reg_table {
	const struct regval_t *regval;
	int regval_num;
};

struct stf_isp_dev;
enum subdev_type;

struct isp_hw_ops {
	int (*isp_clk_enable)(struct stf_isp_dev *isp_dev);
	int (*isp_clk_disable)(struct stf_isp_dev *isp_dev);
	int (*isp_reset)(struct stf_isp_dev *isp_dev);
	int (*isp_config_set)(struct stf_isp_dev *isp_dev);
	int (*isp_set_format)(struct stf_isp_dev *isp_dev,
			struct v4l2_rect *crop, u32 mcode);
			// u32 width, u32 height);
	int (*isp_stream_set)(struct stf_isp_dev *isp_dev, int on);
	int (*isp_reg_read)(struct stf_isp_dev *isp_dev, void *arg);
	int (*isp_reg_write)(struct stf_isp_dev *isp_dev, void *arg);
	int (*isp_shadow_trigger)(struct stf_isp_dev *isp_dev);
};

struct isp_ctrls {
	struct v4l2_ctrl_handler handler;
	struct v4l2_ctrl *pixel_rate;
	struct {
		struct v4l2_ctrl *auto_exp;
		struct v4l2_ctrl *exposure;
	};
	struct {
		struct v4l2_ctrl *auto_wb;
		struct v4l2_ctrl *blue_balance;
		struct v4l2_ctrl *red_balance;
	};
	struct {
		struct v4l2_ctrl *auto_gain;
		struct v4l2_ctrl *gain;
	};
	struct v4l2_ctrl *brightness;
	struct v4l2_ctrl *light_freq;
	struct v4l2_ctrl *saturation;
	struct v4l2_ctrl *contrast;
	struct v4l2_ctrl *hue;
	struct v4l2_ctrl *test_pattern;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
};

struct isp_setfile {
	struct reg_table settings;
	const u8 *data;
	unsigned int size;
	unsigned int state;
};

struct stf_isp_dev {
	enum subdev_type sdev_type;  // must be frist
	struct stfcamss *stfcamss;
	atomic_t ref_count;
	u8 id;
	struct v4l2_subdev subdev;
	struct media_pad pads[STF_ISP_PADS_NUM];
	struct v4l2_mbus_framefmt fmt[STF_ISP_PADS_NUM];
	struct v4l2_rect compose;
	struct v4l2_rect crop;
	const struct isp_format *formats;
	unsigned int nformats;
	struct isp_hw_ops *hw_ops;
	struct mutex stream_lock;
	int stream_count;
	atomic_t shadow_count;

	struct isp_ctrls ctrls;
	struct mutex setfile_lock;
	struct isp_setfile setfile;
};

extern int stf_isp_subdev_init(struct stfcamss *stfcamss, int id);
extern int stf_isp_register(struct stf_isp_dev *isp_dev,
		struct v4l2_device *v4l2_dev);
extern int stf_isp_unregister(struct stf_isp_dev *isp_dev);
extern struct isp_hw_ops isp_ops;
extern void dump_isp_reg(void *__iomem ispbase, int id);

#endif /* STF_ISP_H */
