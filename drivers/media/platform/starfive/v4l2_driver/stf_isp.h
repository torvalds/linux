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

#define STF_ISP_PAD_SINK         0
#define STF_ISP_PAD_SRC          1
#define STF_ISP_PAD_SRC_SS0      2
#define STF_ISP_PAD_SRC_SS1      3
#define STF_ISP_PAD_SRC_ITIW     4
#define STF_ISP_PAD_SRC_ITIR     5
#define STF_ISP_PAD_SRC_RAW      6
#define STF_ISP_PAD_SRC_SCD_Y    7
#define STF_ISP_PADS_NUM         8

#define STF_ISP0_SETFILE     "stf_isp0_fw.bin"
#define STF_ISP1_SETFILE     "stf_isp1_fw.bin"

#define ISP_SCD_BUFFER_SIZE     (19 * 256 * 4)  // align 128
#define ISP_YHIST_BUFFER_SIZE   (64 * 4)
#define ISP_SCD_Y_BUFFER_SIZE   (ISP_SCD_BUFFER_SIZE + ISP_YHIST_BUFFER_SIZE)
#define ISP_RAW_DATA_BITS       12
#define SCALER_RATIO_MAX        1  // no compose function
#define STF_ISP_REG_OFFSET_MAX  0x0FFF
#define STF_ISP_REG_DELAY_MAX   100

#define ISP_REG_CSIINTS_ADDR    0x00000008
#define ISP_REG_SENSOR          0x00000014
#define ISP_REG_DUMP_CFG_0      0x00000024
#define ISP_REG_DUMP_CFG_1      0x00000028
#define ISP_REG_SCD_CFG_0       0x00000098
#define ISP_REG_SCD_CFG_1       0x0000009C
#define ISP_REG_SC_CFG_1        0x000000BC
#define ISP_REG_IESHD_ADDR      0x00000A50
#define ISP_REG_SS0AY           0x00000A94
#define ISP_REG_SS0AUV          0x00000A98
#define ISP_REG_SS0S            0x00000A9C
#define ISP_REG_SS0IW           0x00000AA8
#define ISP_REG_SS1AY           0x00000AAC
#define ISP_REG_SS1AUV          0x00000AB0
#define ISP_REG_SS1S            0x00000AB4
#define ISP_REG_SS1IW           0x00000AC0
#define ISP_REG_YHIST_CFG_4     0x00000CD8
#define ISP_REG_ITIIWSR         0x00000B20
#define ISP_REG_ITIDWLSR        0x00000B24
#define ISP_REG_ITIDWYSAR       0x00000B28
#define ISP_REG_ITIDWUSAR       0x00000B2C
#define ISP_REG_ITIDRYSAR       0x00000B30
#define ISP_REG_ITIDRUSAR       0x00000B34
#define ISP_REG_ITIPDFR         0x00000B38
#define ISP_REG_ITIDRLSR        0x00000B3C
#define ISP_REG_ITIBSR          0x00000B40
#define ISP_REG_ITIAIR          0x00000B44
#define ISP_REG_ITIDPSR         0x00000B48

enum {
	EN_INT_NONE                 = 0,
	EN_INT_ISP_DONE             = (0x1 << 24),
	EN_INT_CSI_DONE             = (0x1 << 25),
	EN_INT_SC_DONE              = (0x1 << 26),
	EN_INT_LINE_INT             = (0x1 << 27),
	EN_INT_ALL                  = (0xF << 24),
};

enum {
	DVP_SENSOR = 0,
	CSI_SENSOR,
};

#define ISP_AWB_OECF_SKIP_FRAME  0
// 0x0BC [31:30] SEL - sc0 input mux for sc awb
// 00 : after DEC, 01 : after OBC, 10 : after OECF, 11 : after AWB
enum scd_type {
	DEC_TYPE = 0,
	OBC_TYPE,
	OECF_TYPE,
	AWB_TYPE
};

struct isp_format {
	u32 code;
	u8 bpp;
};

struct isp_format_table {
	const struct isp_format *fmts;
	int nfmts;
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

struct isp_stream_format {
	struct v4l2_rect rect;
	u32 bpp;
};

struct stf_isp_dev;
enum subdev_type;

struct isp_hw_ops {
	int (*isp_clk_enable)(struct stf_isp_dev *isp_dev);
	int (*isp_clk_disable)(struct stf_isp_dev *isp_dev);
	int (*isp_reset)(struct stf_isp_dev *isp_dev);
	int (*isp_config_set)(struct stf_isp_dev *isp_dev);
	int (*isp_set_format)(struct stf_isp_dev *isp_dev,
			struct isp_stream_format *crop, u32 mcode,
			int type);
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

enum {
	ISP_CROP = 0,
	ISP_COMPOSE,
	ISP_SCALE_SS0,
	ISP_SCALE_SS1,
	ISP_ITIWS,
	ISP_RECT_MAX
};

struct stf_isp_dev {
	enum subdev_type sdev_type;  // must be frist
	struct stfcamss *stfcamss;
	u8 id;
	struct v4l2_subdev subdev;
	struct media_pad pads[STF_ISP_PADS_NUM];
	struct v4l2_mbus_framefmt fmt[STF_ISP_PADS_NUM];
	struct isp_stream_format rect[ISP_RECT_MAX];
	const struct isp_format_table *formats;
	unsigned int nformats;
	struct isp_hw_ops *hw_ops;
	struct mutex power_lock;
	int power_count;
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
