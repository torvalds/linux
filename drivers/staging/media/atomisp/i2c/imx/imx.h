/*
 * Support for Sony IMX camera sensor.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef __IMX_H__
#define __IMX_H__
#include "../../include/linux/atomisp_platform.h"
#include "../../include/linux/atomisp.h"
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/v4l2-mediabus.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include "imx175.h"
#include "imx135.h"
#include "imx134.h"
#include "imx132.h"
#include "imx208.h"
#include "imx219.h"
#include "imx227.h"

#define IMX_MCLK		192

/* TODO - This should be added into include/linux/videodev2.h */
#ifndef V4L2_IDENT_IMX
#define V4L2_IDENT_IMX	8245
#endif

#define IMX_MAX_AE_LUT_LENGTH	5
/*
 * imx System control registers
 */
#define IMX_MASK_5BIT	0x1F
#define IMX_MASK_4BIT	0xF
#define IMX_MASK_3BIT	0x7
#define IMX_MASK_2BIT	0x3
#define IMX_MASK_8BIT	0xFF
#define IMX_MASK_11BIT	0x7FF
#define IMX_INTG_BUF_COUNT		2

#define IMX_FINE_INTG_TIME		0x1E8

#define IMX_VT_PIX_CLK_DIV		0x0301
#define IMX_VT_SYS_CLK_DIV		0x0303
#define IMX_PRE_PLL_CLK_DIV		0x0305
#define IMX227_IOP_PRE_PLL_CLK_DIV	0x030D
#define IMX227_PLL_MULTIPLIER		0x0306
#define IMX227_IOP_PLL_MULTIPLIER	0x030E
#define IMX227_PLL_MULTI_DRIVE		0x0310
#define IMX227_OP_PIX_CLK_DIV		0x0309
#define IMX227_OP_SYS_CLK_DIV		0x030B
#define IMX_PLL_MULTIPLIER		0x030C
#define IMX_OP_PIX_DIV			0x0309
#define IMX_OP_SYS_DIV			0x030B
#define IMX_FRAME_LENGTH_LINES		0x0340
#define IMX_LINE_LENGTH_PIXELS		0x0342
#define IMX_COARSE_INTG_TIME_MIN	0x1004
#define IMX_COARSE_INTG_TIME_MAX	0x1006
#define IMX_BINNING_ENABLE		0x0390
#define IMX227_BINNING_ENABLE		0x0900
#define IMX_BINNING_TYPE		0x0391
#define IMX227_BINNING_TYPE		0x0901
#define IMX_READ_MODE			0x0390
#define IMX227_READ_MODE		0x0900

#define IMX_HORIZONTAL_START_H 0x0344
#define IMX_VERTICAL_START_H 0x0346
#define IMX_HORIZONTAL_END_H 0x0348
#define IMX_VERTICAL_END_H 0x034a
#define IMX_HORIZONTAL_OUTPUT_SIZE_H 0x034c
#define IMX_VERTICAL_OUTPUT_SIZE_H 0x034e

/* Post Divider setting register for imx132 and imx208 */
#define IMX132_208_VT_RGPLTD		0x30A4

/* Multiplier setting register for imx132, imx208, and imx219 */
#define IMX132_208_219_PLL_MULTIPLIER		0x0306

#define IMX_COARSE_INTEGRATION_TIME		0x0202
#define IMX_TEST_PATTERN_MODE			0x0600
#define IMX_TEST_PATTERN_COLOR_R		0x0602
#define IMX_TEST_PATTERN_COLOR_GR		0x0604
#define IMX_TEST_PATTERN_COLOR_B		0x0606
#define IMX_TEST_PATTERN_COLOR_GB		0x0608
#define IMX_IMG_ORIENTATION			0x0101
#define IMX_VFLIP_BIT			2
#define IMX_HFLIP_BIT			1
#define IMX_GLOBAL_GAIN			0x0205
#define IMX_SHORT_AGC_GAIN		0x0233
#define IMX_DGC_ADJ		0x020E
#define IMX_DGC_LEN		10
#define IMX227_DGC_LEN		4
#define IMX_MAX_EXPOSURE_SUPPORTED 0xfffb
#define IMX_MAX_GLOBAL_GAIN_SUPPORTED 0x00ff
#define IMX_MAX_DIGITAL_GAIN_SUPPORTED 0x0fff

#define MAX_FMTS 1
#define IMX_OTP_DATA_SIZE		1280

#define IMX_SUBDEV_PREFIX "imx"
#define IMX_DRIVER	"imx1x5"

/* Sensor ids from identification register */
#define IMX_NAME_134	"imx134"
#define IMX_NAME_135	"imx135"
#define IMX_NAME_175	"imx175"
#define IMX_NAME_132	"imx132"
#define IMX_NAME_208	"imx208"
#define IMX_NAME_219	"imx219"
#define IMX_NAME_227	"imx227"
#define IMX175_ID	0x0175
#define IMX135_ID	0x0135
#define IMX134_ID	0x0134
#define IMX132_ID	0x0132
#define IMX208_ID	0x0208
#define IMX219_ID	0x0219
#define IMX227_ID	0x0227

/* Sensor id based on i2c_device_id table
 * (Fuji module can not be detected based on sensor registers) */
#define IMX135_FUJI_ID			0x0136
#define IMX_NAME_135_FUJI		"imx135fuji"

/* imx175 - use dw9714 vcm */
#define IMX175_MERRFLD 0x175
#define IMX175_VALLEYVIEW 0x176
#define IMX135_SALTBAY 0x135
#define IMX135_VICTORIABAY 0x136
#define IMX132_SALTBAY 0x132
#define IMX134_VALLEYVIEW 0x134
#define IMX208_MOFD_PD2 0x208
#define IMX219_MFV0_PRH 0x219
#define IMX227_SAND 0x227

/* otp - specific settings */
#define E2PROM_ADDR 0xa0
#define E2PROM_LITEON_12P1BA869D_ADDR 0xa0
#define E2PROM_ABICO_SS89A839_ADDR 0xa8
#define DEFAULT_OTP_SIZE 1280
#define IMX135_OTP_SIZE 1280
#define IMX219_OTP_SIZE 2048
#define IMX227_OTP_SIZE 2560
#define E2PROM_LITEON_12P1BA869D_SIZE 544

#define IMX_ID_DEFAULT	0x0000
#define IMX132_175_208_219_CHIP_ID	0x0000
#define IMX134_135_CHIP_ID	0x0016
#define IMX134_135_227_CHIP_ID	0x0016

#define IMX175_RES_WIDTH_MAX	3280
#define IMX175_RES_HEIGHT_MAX	2464
#define IMX135_RES_WIDTH_MAX	4208
#define IMX135_RES_HEIGHT_MAX	3120
#define IMX132_RES_WIDTH_MAX	1936
#define IMX132_RES_HEIGHT_MAX	1096
#define IMX134_RES_WIDTH_MAX	3280
#define IMX134_RES_HEIGHT_MAX	2464
#define IMX208_RES_WIDTH_MAX	1936
#define IMX208_RES_HEIGHT_MAX	1096
#define IMX219_RES_WIDTH_MAX	3280
#define IMX219_RES_HEIGHT_MAX	2464
#define IMX227_RES_WIDTH_MAX	2400
#define IMX227_RES_HEIGHT_MAX	2720

/* Defines for lens/VCM */
#define IMX_FOCAL_LENGTH_NUM	369	/*3.69mm*/
#define IMX_FOCAL_LENGTH_DEM	100
#define IMX_F_NUMBER_DEFAULT_NUM	22
#define IMX_F_NUMBER_DEM	10
#define IMX_INVALID_CONFIG	0xffffffff
#define IMX_MAX_FOCUS_POS	1023
#define IMX_MAX_FOCUS_NEG	(-1023)
#define IMX_VCM_SLEW_STEP_MAX	0x3f
#define IMX_VCM_SLEW_TIME_MAX	0x1f

#define IMX_BIN_FACTOR_MAX			4
#define IMX_INTEGRATION_TIME_MARGIN	4
/*
 * focal length bits definition:
 * bits 31-16: numerator, bits 15-0: denominator
 */
#define IMX_FOCAL_LENGTH_DEFAULT 0x1710064

/*
 * current f-number bits definition:
 * bits 31-16: numerator, bits 15-0: denominator
 */
#define IMX_F_NUMBER_DEFAULT 0x16000a

/*
 * f-number range bits definition:
 * bits 31-24: max f-number numerator
 * bits 23-16: max f-number denominator
 * bits 15-8: min f-number numerator
 * bits 7-0: min f-number denominator
 */
#define IMX_F_NUMBER_RANGE 0x160a160a

struct imx_vcm {
	int (*power_up)(struct v4l2_subdev *sd);
	int (*power_down)(struct v4l2_subdev *sd);
	int (*t_focus_abs)(struct v4l2_subdev *sd, s32 value);
	int (*t_focus_abs_init)(struct v4l2_subdev *sd);
	int (*t_focus_rel)(struct v4l2_subdev *sd, s32 value);
	int (*q_focus_status)(struct v4l2_subdev *sd, s32 *value);
	int (*q_focus_abs)(struct v4l2_subdev *sd, s32 *value);
	int (*t_vcm_slew)(struct v4l2_subdev *sd, s32 value);
	int (*t_vcm_timing)(struct v4l2_subdev *sd, s32 value);
};

struct imx_otp {
	void * (*otp_read)(struct v4l2_subdev *sd, u8 dev_addr,
		u32 start_addr, u32 size);
	u32 start_addr;
	u32 size;
	u8 dev_addr;
};

struct max_res {
	int res_max_width;
	int res_max_height;
};

struct max_res imx_max_res[] = {
	[IMX175_ID] = {
		.res_max_width = IMX175_RES_WIDTH_MAX,
		.res_max_height = IMX175_RES_HEIGHT_MAX,
	},
	[IMX135_ID] = {
		.res_max_width = IMX135_RES_WIDTH_MAX,
		.res_max_height = IMX135_RES_HEIGHT_MAX,
	},
	[IMX132_ID] = {
		.res_max_width = IMX132_RES_WIDTH_MAX,
		.res_max_height = IMX132_RES_HEIGHT_MAX,
	},
	[IMX134_ID] = {
		.res_max_width = IMX134_RES_WIDTH_MAX,
		.res_max_height = IMX134_RES_HEIGHT_MAX,
	},
	[IMX208_ID] = {
		.res_max_width = IMX208_RES_WIDTH_MAX,
		.res_max_height = IMX208_RES_HEIGHT_MAX,
	},
	[IMX219_ID] = {
		.res_max_width = IMX219_RES_WIDTH_MAX,
		.res_max_height = IMX219_RES_HEIGHT_MAX,
	},
	[IMX227_ID] = {
		.res_max_width = IMX227_RES_WIDTH_MAX,
		.res_max_height = IMX227_RES_HEIGHT_MAX,
	},
};

struct imx_settings {
	struct imx_reg const *init_settings;
	struct imx_resolution *res_preview;
	struct imx_resolution *res_still;
	struct imx_resolution *res_video;
	int n_res_preview;
	int n_res_still;
	int n_res_video;
};

struct imx_settings imx_sets[] = {
	[IMX175_MERRFLD] = {
		.init_settings = imx175_init_settings,
		.res_preview = imx175_res_preview,
		.res_still = imx175_res_still,
		.res_video = imx175_res_video,
		.n_res_preview = ARRAY_SIZE(imx175_res_preview),
		.n_res_still = ARRAY_SIZE(imx175_res_still),
		.n_res_video = ARRAY_SIZE(imx175_res_video),
	},
	[IMX175_VALLEYVIEW] = {
		.init_settings = imx175_init_settings,
		.res_preview = imx175_res_preview,
		.res_still = imx175_res_still,
		.res_video = imx175_res_video,
		.n_res_preview = ARRAY_SIZE(imx175_res_preview),
		.n_res_still = ARRAY_SIZE(imx175_res_still),
		.n_res_video = ARRAY_SIZE(imx175_res_video),
	},
	[IMX135_SALTBAY] = {
		.init_settings = imx135_init_settings,
		.res_preview = imx135_res_preview,
		.res_still = imx135_res_still,
		.res_video = imx135_res_video,
		.n_res_preview = ARRAY_SIZE(imx135_res_preview),
		.n_res_still = ARRAY_SIZE(imx135_res_still),
		.n_res_video = ARRAY_SIZE(imx135_res_video),
	},
	[IMX135_VICTORIABAY] = {
		.init_settings = imx135_init_settings,
		.res_preview = imx135_res_preview_mofd,
		.res_still = imx135_res_still_mofd,
		.res_video = imx135_res_video,
		.n_res_preview = ARRAY_SIZE(imx135_res_preview_mofd),
		.n_res_still = ARRAY_SIZE(imx135_res_still_mofd),
		.n_res_video = ARRAY_SIZE(imx135_res_video),
	},
	[IMX132_SALTBAY] = {
		.init_settings = imx132_init_settings,
		.res_preview = imx132_res_preview,
		.res_still = imx132_res_still,
		.res_video = imx132_res_video,
		.n_res_preview = ARRAY_SIZE(imx132_res_preview),
		.n_res_still = ARRAY_SIZE(imx132_res_still),
		.n_res_video = ARRAY_SIZE(imx132_res_video),
	},
	[IMX134_VALLEYVIEW] = {
		.init_settings = imx134_init_settings,
		.res_preview = imx134_res_preview,
		.res_still = imx134_res_still,
		.res_video = imx134_res_video,
		.n_res_preview = ARRAY_SIZE(imx134_res_preview),
		.n_res_still = ARRAY_SIZE(imx134_res_still),
		.n_res_video = ARRAY_SIZE(imx134_res_video),
	},
	[IMX208_MOFD_PD2] = {
		.init_settings = imx208_init_settings,
		.res_preview = imx208_res_preview,
		.res_still = imx208_res_still,
		.res_video = imx208_res_video,
		.n_res_preview = ARRAY_SIZE(imx208_res_preview),
		.n_res_still = ARRAY_SIZE(imx208_res_still),
		.n_res_video = ARRAY_SIZE(imx208_res_video),
	},
	[IMX219_MFV0_PRH] = {
		.init_settings = imx219_init_settings,
		.res_preview = imx219_res_preview,
		.res_still = imx219_res_still,
		.res_video = imx219_res_video,
		.n_res_preview = ARRAY_SIZE(imx219_res_preview),
		.n_res_still = ARRAY_SIZE(imx219_res_still),
		.n_res_video = ARRAY_SIZE(imx219_res_video),
	},
	[IMX227_SAND] = {
		.init_settings = imx227_init_settings,
		.res_preview = imx227_res_preview,
		.res_still = imx227_res_still,
		.res_video = imx227_res_video,
		.n_res_preview = ARRAY_SIZE(imx227_res_preview),
		.n_res_still = ARRAY_SIZE(imx227_res_still),
		.n_res_video = ARRAY_SIZE(imx227_res_video),
	},
};

struct imx_reg_addr {
	u16 frame_length_lines;
	u16 line_length_pixels;
	u16 horizontal_start_h;
	u16 vertical_start_h;
	u16 horizontal_end_h;
	u16 vertical_end_h;
	u16 horizontal_output_size_h;
	u16 vertical_output_size_h;
	u16 coarse_integration_time;
	u16 img_orientation;
	u16 global_gain;
	u16 dgc_adj;
};

struct imx_reg_addr imx_addr = {
	IMX_FRAME_LENGTH_LINES,
	IMX_LINE_LENGTH_PIXELS,
	IMX_HORIZONTAL_START_H,
	IMX_VERTICAL_START_H,
	IMX_HORIZONTAL_END_H,
	IMX_VERTICAL_END_H,
	IMX_HORIZONTAL_OUTPUT_SIZE_H,
	IMX_VERTICAL_OUTPUT_SIZE_H,
	IMX_COARSE_INTEGRATION_TIME,
	IMX_IMG_ORIENTATION,
	IMX_GLOBAL_GAIN,
	IMX_DGC_ADJ,
};

struct imx_reg_addr imx219_addr = {
	IMX219_FRAME_LENGTH_LINES,
	IMX219_LINE_LENGTH_PIXELS,
	IMX219_HORIZONTAL_START_H,
	IMX219_VERTICAL_START_H,
	IMX219_HORIZONTAL_END_H,
	IMX219_VERTICAL_END_H,
	IMX219_HORIZONTAL_OUTPUT_SIZE_H,
	IMX219_VERTICAL_OUTPUT_SIZE_H,
	IMX219_COARSE_INTEGRATION_TIME,
	IMX219_IMG_ORIENTATION,
	IMX219_GLOBAL_GAIN,
	IMX219_DGC_ADJ,
};

#define	v4l2_format_capture_type_entry(_width, _height, \
		_pixelformat, _bytesperline, _colorspace) \
	{\
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,\
		.fmt.pix.width = (_width),\
		.fmt.pix.height = (_height),\
		.fmt.pix.pixelformat = (_pixelformat),\
		.fmt.pix.bytesperline = (_bytesperline),\
		.fmt.pix.colorspace = (_colorspace),\
		.fmt.pix.sizeimage = (_height)*(_bytesperline),\
	}

#define	s_output_format_entry(_width, _height, _pixelformat, \
		_bytesperline, _colorspace, _fps) \
	{\
		.v4l2_fmt = v4l2_format_capture_type_entry(_width, \
			_height, _pixelformat, _bytesperline, \
				_colorspace),\
		.fps = (_fps),\
	}

#define	s_output_format_reg_entry(_width, _height, _pixelformat, \
		_bytesperline, _colorspace, _fps, _reg_setting) \
	{\
		.s_fmt = s_output_format_entry(_width, _height,\
				_pixelformat, _bytesperline, \
				_colorspace, _fps),\
		.reg_setting = (_reg_setting),\
	}

/* imx device structure */
struct imx_device {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_mbus_framefmt format;
	struct camera_sensor_platform_data *platform_data;
	struct mutex input_lock; /* serialize sensor's ioctl */
	int fmt_idx;
	int status;
	int streaming;
	int power;
	int run_mode;
	int vt_pix_clk_freq_mhz;
	int fps_index;
	u32 focus;
	u16 sensor_id;			/* Sensor id from registers */
	u16 i2c_id;			/* Sensor id from i2c_device_id */
	u16 coarse_itg;
	u16 fine_itg;
	u16 digital_gain;
	u16 gain;
	u16 pixels_per_line;
	u16 lines_per_frame;
	u8 targetfps;
	u8 fps;
	const struct imx_reg *regs;
	u8 res;
	u8 type;
	u8 sensor_revision;
	u8 *otp_data;
	struct imx_settings *mode_tables;
	struct imx_vcm *vcm_driver;
	struct imx_otp *otp_driver;
	const struct imx_resolution *curr_res_table;
	unsigned long entries_curr_table;
	const struct firmware *fw;
	struct imx_reg_addr *reg_addr;
	const struct imx_reg *param_hold;
	const struct imx_reg *param_update;

	/* used for h/b blank tuning */
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *h_blank;
	struct v4l2_ctrl *v_blank;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *h_flip;
	struct v4l2_ctrl *v_flip;

	/* Test pattern control */
	struct v4l2_ctrl *tp_mode;
	struct v4l2_ctrl *tp_r;
	struct v4l2_ctrl *tp_gr;
	struct v4l2_ctrl *tp_gb;
	struct v4l2_ctrl *tp_b;

	/* FIXME! */
	bool new_res_sel_method;
};

#define to_imx_sensor(x) container_of(x, struct imx_device, sd)

#define IMX_MAX_WRITE_BUF_SIZE	32
struct imx_write_buffer {
	u16 addr;
	u8 data[IMX_MAX_WRITE_BUF_SIZE];
};

struct imx_write_ctrl {
	int index;
	struct imx_write_buffer buffer;
};

static const struct imx_reg imx_soft_standby[] = {
	{IMX_8BIT, 0x0100, 0x00},
	{IMX_TOK_TERM, 0, 0}
};

static const struct imx_reg imx_streaming[] = {
	{IMX_8BIT, 0x0100, 0x01},
	{IMX_TOK_TERM, 0, 0}
};

static const struct imx_reg imx_param_hold[] = {
	{IMX_8BIT, 0x0104, 0x01},	/* GROUPED_PARAMETER_HOLD */
	{IMX_TOK_TERM, 0, 0}
};

static const struct imx_reg imx_param_update[] = {
	{IMX_8BIT, 0x0104, 0x00},	/* GROUPED_PARAMETER_HOLD */
	{IMX_TOK_TERM, 0, 0}
};

static const struct imx_reg imx219_param_hold[] = {
	{IMX_TOK_TERM, 0, 0}
};

static const struct imx_reg imx219_param_update[] = {
	{IMX_TOK_TERM, 0, 0}
};

extern int ad5816g_vcm_power_up(struct v4l2_subdev *sd);
extern int ad5816g_vcm_power_down(struct v4l2_subdev *sd);
extern int ad5816g_t_focus_abs(struct v4l2_subdev *sd, s32 value);
extern int ad5816g_t_focus_rel(struct v4l2_subdev *sd, s32 value);
extern int ad5816g_q_focus_status(struct v4l2_subdev *sd, s32 *value);
extern int ad5816g_q_focus_abs(struct v4l2_subdev *sd, s32 *value);
extern int ad5816g_t_vcm_slew(struct v4l2_subdev *sd, s32 value);
extern int ad5816g_t_vcm_timing(struct v4l2_subdev *sd, s32 value);

extern int drv201_vcm_power_up(struct v4l2_subdev *sd);
extern int drv201_vcm_power_down(struct v4l2_subdev *sd);
extern int drv201_t_focus_abs(struct v4l2_subdev *sd, s32 value);
extern int drv201_t_focus_rel(struct v4l2_subdev *sd, s32 value);
extern int drv201_q_focus_status(struct v4l2_subdev *sd, s32 *value);
extern int drv201_q_focus_abs(struct v4l2_subdev *sd, s32 *value);
extern int drv201_t_vcm_slew(struct v4l2_subdev *sd, s32 value);
extern int drv201_t_vcm_timing(struct v4l2_subdev *sd, s32 value);

extern int dw9714_vcm_power_up(struct v4l2_subdev *sd);
extern int dw9714_vcm_power_down(struct v4l2_subdev *sd);
extern int dw9714_t_focus_abs(struct v4l2_subdev *sd, s32 value);
extern int dw9714_t_focus_abs_init(struct v4l2_subdev *sd);
extern int dw9714_t_focus_rel(struct v4l2_subdev *sd, s32 value);
extern int dw9714_q_focus_status(struct v4l2_subdev *sd, s32 *value);
extern int dw9714_q_focus_abs(struct v4l2_subdev *sd, s32 *value);
extern int dw9714_t_vcm_slew(struct v4l2_subdev *sd, s32 value);
extern int dw9714_t_vcm_timing(struct v4l2_subdev *sd, s32 value);

extern int dw9719_vcm_power_up(struct v4l2_subdev *sd);
extern int dw9719_vcm_power_down(struct v4l2_subdev *sd);
extern int dw9719_t_focus_abs(struct v4l2_subdev *sd, s32 value);
extern int dw9719_t_focus_rel(struct v4l2_subdev *sd, s32 value);
extern int dw9719_q_focus_status(struct v4l2_subdev *sd, s32 *value);
extern int dw9719_q_focus_abs(struct v4l2_subdev *sd, s32 *value);
extern int dw9719_t_vcm_slew(struct v4l2_subdev *sd, s32 value);
extern int dw9719_t_vcm_timing(struct v4l2_subdev *sd, s32 value);

extern int dw9718_vcm_power_up(struct v4l2_subdev *sd);
extern int dw9718_vcm_power_down(struct v4l2_subdev *sd);
extern int dw9718_t_focus_abs(struct v4l2_subdev *sd, s32 value);
extern int dw9718_t_focus_rel(struct v4l2_subdev *sd, s32 value);
extern int dw9718_q_focus_status(struct v4l2_subdev *sd, s32 *value);
extern int dw9718_q_focus_abs(struct v4l2_subdev *sd, s32 *value);
extern int dw9718_t_vcm_slew(struct v4l2_subdev *sd, s32 value);
extern int dw9718_t_vcm_timing(struct v4l2_subdev *sd, s32 value);

extern int vcm_power_up(struct v4l2_subdev *sd);
extern int vcm_power_down(struct v4l2_subdev *sd);

struct imx_vcm imx_vcms[] = {
	[IMX175_MERRFLD] = {
		.power_up = drv201_vcm_power_up,
		.power_down = drv201_vcm_power_down,
		.t_focus_abs = drv201_t_focus_abs,
		.t_focus_abs_init = NULL,
		.t_focus_rel = drv201_t_focus_rel,
		.q_focus_status = drv201_q_focus_status,
		.q_focus_abs = drv201_q_focus_abs,
		.t_vcm_slew = drv201_t_vcm_slew,
		.t_vcm_timing = drv201_t_vcm_timing,
	},
	[IMX175_VALLEYVIEW] = {
		.power_up = dw9714_vcm_power_up,
		.power_down = dw9714_vcm_power_down,
		.t_focus_abs = dw9714_t_focus_abs,
		.t_focus_abs_init = NULL,
		.t_focus_rel = dw9714_t_focus_rel,
		.q_focus_status = dw9714_q_focus_status,
		.q_focus_abs = dw9714_q_focus_abs,
		.t_vcm_slew = dw9714_t_vcm_slew,
		.t_vcm_timing = dw9714_t_vcm_timing,
	},
	[IMX135_SALTBAY] = {
		.power_up = ad5816g_vcm_power_up,
		.power_down = ad5816g_vcm_power_down,
		.t_focus_abs = ad5816g_t_focus_abs,
		.t_focus_abs_init = NULL,
		.t_focus_rel = ad5816g_t_focus_rel,
		.q_focus_status = ad5816g_q_focus_status,
		.q_focus_abs = ad5816g_q_focus_abs,
		.t_vcm_slew = ad5816g_t_vcm_slew,
		.t_vcm_timing = ad5816g_t_vcm_timing,
	},
	[IMX135_VICTORIABAY] = {
		.power_up = dw9719_vcm_power_up,
		.power_down = dw9719_vcm_power_down,
		.t_focus_abs = dw9719_t_focus_abs,
		.t_focus_abs_init = NULL,
		.t_focus_rel = dw9719_t_focus_rel,
		.q_focus_status = dw9719_q_focus_status,
		.q_focus_abs = dw9719_q_focus_abs,
		.t_vcm_slew = dw9719_t_vcm_slew,
		.t_vcm_timing = dw9719_t_vcm_timing,
	},
	[IMX134_VALLEYVIEW] = {
		.power_up = dw9714_vcm_power_up,
		.power_down = dw9714_vcm_power_down,
		.t_focus_abs = dw9714_t_focus_abs,
		.t_focus_abs_init = dw9714_t_focus_abs_init,
		.t_focus_rel = dw9714_t_focus_rel,
		.q_focus_status = dw9714_q_focus_status,
		.q_focus_abs = dw9714_q_focus_abs,
		.t_vcm_slew = dw9714_t_vcm_slew,
		.t_vcm_timing = dw9714_t_vcm_timing,
	},
	[IMX219_MFV0_PRH] = {
		.power_up = dw9718_vcm_power_up,
		.power_down = dw9718_vcm_power_down,
		.t_focus_abs = dw9718_t_focus_abs,
		.t_focus_abs_init = NULL,
		.t_focus_rel = dw9718_t_focus_rel,
		.q_focus_status = dw9718_q_focus_status,
		.q_focus_abs = dw9718_q_focus_abs,
		.t_vcm_slew = dw9718_t_vcm_slew,
		.t_vcm_timing = dw9718_t_vcm_timing,
	},
	[IMX_ID_DEFAULT] = {
		.power_up = NULL,
		.power_down = NULL,
		.t_focus_abs_init = NULL,
	},
};

extern void *dummy_otp_read(struct v4l2_subdev *sd, u8 dev_addr,
	u32 start_addr, u32 size);
extern void *imx_otp_read(struct v4l2_subdev *sd, u8 dev_addr,
	u32 start_addr, u32 size);
extern void *e2prom_otp_read(struct v4l2_subdev *sd, u8 dev_addr,
	u32 start_addr, u32 size);
extern void *brcc064_otp_read(struct v4l2_subdev *sd, u8 dev_addr,
	u32 start_addr, u32 size);
extern void *imx227_otp_read(struct v4l2_subdev *sd, u8 dev_addr,
	u32 start_addr, u32 size);
extern void *e2prom_otp_read(struct v4l2_subdev *sd, u8 dev_addr,
	u32 start_addr, u32 size);
struct imx_otp imx_otps[] = {
	[IMX175_MERRFLD] = {
		.otp_read = imx_otp_read,
		.dev_addr = E2PROM_ADDR,
		.start_addr = 0,
		.size = DEFAULT_OTP_SIZE,
	},
	[IMX175_VALLEYVIEW] = {
		.otp_read = e2prom_otp_read,
		.dev_addr = E2PROM_ABICO_SS89A839_ADDR,
		.start_addr = E2PROM_2ADDR,
		.size = DEFAULT_OTP_SIZE,
	},
	[IMX135_SALTBAY] = {
		.otp_read = e2prom_otp_read,
		.dev_addr = E2PROM_ADDR,
		.start_addr = 0,
		.size = DEFAULT_OTP_SIZE,
	},
	[IMX135_VICTORIABAY] = {
		.otp_read = imx_otp_read,
		.size = DEFAULT_OTP_SIZE,
	},
	[IMX134_VALLEYVIEW] = {
		.otp_read = e2prom_otp_read,
		.dev_addr = E2PROM_LITEON_12P1BA869D_ADDR,
		.start_addr = 0,
		.size = E2PROM_LITEON_12P1BA869D_SIZE,
	},
	[IMX132_SALTBAY] = {
		.otp_read = dummy_otp_read,
		.size = DEFAULT_OTP_SIZE,
	},
	[IMX208_MOFD_PD2] = {
		.otp_read = dummy_otp_read,
		.size = DEFAULT_OTP_SIZE,
	},
	[IMX219_MFV0_PRH] = {
		.otp_read = brcc064_otp_read,
		.dev_addr = E2PROM_ADDR,
		.start_addr = 0,
		.size = IMX219_OTP_SIZE,
	},
	[IMX227_SAND] = {
		.otp_read = imx227_otp_read,
		.size = IMX227_OTP_SIZE,
	},
	[IMX_ID_DEFAULT] = {
		.otp_read = dummy_otp_read,
		.size = DEFAULT_OTP_SIZE,
	},
};

#endif

