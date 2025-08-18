// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sony imx334 sensor driver
 *
 * Copyright (C) 2021 Intel Corporation
 */
#include <linux/unaligned.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

/* Streaming Mode */
#define IMX334_REG_MODE_SELECT		CCI_REG8(0x3000)
#define IMX334_MODE_STANDBY		0x01
#define IMX334_MODE_STREAMING		0x00

/* Lines per frame */
#define IMX334_REG_VMAX			CCI_REG24_LE(0x3030)

#define IMX334_REG_HMAX			CCI_REG16_LE(0x3034)

#define IMX334_REG_OPB_SIZE_V		CCI_REG8(0x304c)
#define IMX334_REG_ADBIT		CCI_REG8(0x3050)
#define IMX334_REG_MDBIT		CCI_REG8(0x319d)
#define IMX334_REG_ADBIT1		CCI_REG16_LE(0x341c)
#define IMX334_REG_Y_OUT_SIZE		CCI_REG16_LE(0x3308)
#define IMX334_REG_XVS_XHS_OUTSEL	CCI_REG8(0x31a0)
#define IMX334_REG_XVS_XHS_DRV		CCI_REG8(0x31a1)

/* Chip ID */
#define IMX334_REG_ID			CCI_REG8(0x3044)
#define IMX334_ID			0x1e

/* Exposure control */
#define IMX334_REG_SHUTTER		CCI_REG24_LE(0x3058)
#define IMX334_EXPOSURE_MIN		1
#define IMX334_EXPOSURE_OFFSET		5
#define IMX334_EXPOSURE_STEP		1
#define IMX334_EXPOSURE_DEFAULT		0x0648

#define IMX334_REG_LANEMODE		CCI_REG8(0x3a01)
#define IMX334_CSI_4_LANE_MODE		3
#define IMX334_CSI_8_LANE_MODE		7

/* Window cropping Settings */
#define IMX334_REG_AREA3_ST_ADR_1	CCI_REG16_LE(0x3074)
#define IMX334_REG_AREA3_ST_ADR_2	CCI_REG16_LE(0x308e)
#define IMX334_REG_UNREAD_PARAM5	CCI_REG16_LE(0x30b6)
#define IMX334_REG_AREA3_WIDTH_1	CCI_REG16_LE(0x3076)
#define IMX334_REG_AREA3_WIDTH_2	CCI_REG16_LE(0x3090)
#define IMX334_REG_BLACK_OFSET_ADR	CCI_REG16_LE(0x30c6)
#define IMX334_REG_UNRD_LINE_MAX	CCI_REG16_LE(0x30ce)
#define IMX334_REG_UNREAD_ED_ADR	CCI_REG16_LE(0x30d8)
#define IMX334_REG_UNREAD_PARAM6	CCI_REG16_LE(0x3116)

#define IMX334_REG_VREVERSE		CCI_REG8(0x304f)
#define IMX334_REG_HREVERSE		CCI_REG8(0x304e)

/* Binning Settings */
#define IMX334_REG_HADD_VADD		CCI_REG8(0x3199)
#define IMX334_REG_VALID_EXPAND		CCI_REG8(0x31dd)
#define IMX334_REG_TCYCLE		CCI_REG8(0x3300)

/* Analog gain control */
#define IMX334_REG_AGAIN		CCI_REG16_LE(0x30e8)
#define IMX334_AGAIN_MIN		0
#define IMX334_AGAIN_MAX		240
#define IMX334_AGAIN_STEP		1
#define IMX334_AGAIN_DEFAULT		0

/* Group hold register */
#define IMX334_REG_HOLD			CCI_REG8(0x3001)

#define IMX334_REG_MASTER_MODE		CCI_REG8(0x3002)
#define IMX334_REG_WINMODE		CCI_REG8(0x3018)
#define IMX334_REG_HTRIMMING_START	CCI_REG16_LE(0x302c)
#define IMX334_REG_HNUM			CCI_REG16_LE(0x302e)

/* Input clock rate */
#define IMX334_INCLK_RATE		24000000

/* INCK Setting Register */
#define IMX334_REG_BCWAIT_TIME		CCI_REG8(0x300c)
#define IMX334_REG_CPWAIT_TIME		CCI_REG8(0x300d)
#define IMX334_REG_INCKSEL1		CCI_REG16_LE(0x314c)
#define IMX334_REG_INCKSEL2		CCI_REG8(0x315a)
#define IMX334_REG_INCKSEL3		CCI_REG8(0x3168)
#define IMX334_REG_INCKSEL4		CCI_REG8(0x316a)
#define IMX334_REG_SYS_MODE		CCI_REG8(0x319e)

#define IMX334_REG_TCLKPOST		CCI_REG16_LE(0x3a18)
#define IMX334_REG_TCLKPREPARE		CCI_REG16_LE(0x3a1a)
#define IMX334_REG_TCLKTRAIL		CCI_REG16_LE(0x3a1c)
#define IMX334_REG_TCLKZERO		CCI_REG16_LE(0x3a1e)
#define IMX334_REG_THSPREPARE		CCI_REG16_LE(0x3a20)
#define IMX334_REG_THSZERO		CCI_REG16_LE(0x3a22)
#define IMX334_REG_THSTRAIL		CCI_REG16_LE(0x3a24)
#define IMX334_REG_THSEXIT		CCI_REG16_LE(0x3a26)
#define IMX334_REG_TPLX			CCI_REG16_LE(0x3a28)

/* CSI2 HW configuration */
#define IMX334_LINK_FREQ_891M		891000000
#define IMX334_LINK_FREQ_445M		445500000
#define IMX334_NUM_DATA_LANES		4

#define IMX334_REG_MIN			0x00
#define IMX334_REG_MAX			0xfffff

/* Test Pattern Control */
#define IMX334_REG_TP			CCI_REG8(0x329e)
#define IMX334_TP_COLOR_HBARS		0xa
#define IMX334_TP_COLOR_VBARS		0xb
#define IMX334_TP_BLACK			0x0
#define IMX334_TP_WHITE			0x1
#define IMX334_TP_BLACK_GREY		0xc

#define IMX334_TPG_EN_DOUT		CCI_REG8(0x329c)
#define IMX334_TP_ENABLE		0x1
#define IMX334_TP_DISABLE		0x0

#define IMX334_TPG_COLORW		CCI_REG8(0x32a0)
#define IMX334_TPG_COLORW_120P		0x13

#define IMX334_TP_CLK_EN		CCI_REG8(0x3148)
#define IMX334_TP_CLK_EN_VAL		0x10
#define IMX334_TP_CLK_DIS_VAL		0x0

#define IMX334_DIG_CLP_MODE		CCI_REG8(0x3280)

/**
 * struct imx334_reg_list - imx334 sensor register list
 * @num_of_regs: Number of registers in the list
 * @regs: Pointer to register list
 */
struct imx334_reg_list {
	u32 num_of_regs;
	const struct cci_reg_sequence *regs;
};

/**
 * struct imx334_mode - imx334 sensor mode structure
 * @width: Frame width
 * @height: Frame height
 * @hblank: Horizontal blanking in lines
 * @vblank: Vertical blanking in lines
 * @vblank_min: Minimal vertical blanking in lines
 * @vblank_max: Maximum vertical blanking in lines
 * @pclk: Sensor pixel clock
 * @link_freq_idx: Link frequency index
 * @reg_list: Register list for sensor mode
 */
struct imx334_mode {
	u32 width;
	u32 height;
	u32 hblank;
	u32 vblank;
	u32 vblank_min;
	u32 vblank_max;
	u64 pclk;
	u32 link_freq_idx;
	struct imx334_reg_list reg_list;
};

/**
 * struct imx334 - imx334 sensor device structure
 * @dev: Pointer to generic device
 * @cci: CCI register map
 * @client: Pointer to i2c client
 * @sd: V4L2 sub-device
 * @pad: Media pad. Only one pad supported
 * @reset_gpio: Sensor reset gpio
 * @inclk: Sensor input clock
 * @ctrl_handler: V4L2 control handler
 * @link_freq_ctrl: Pointer to link frequency control
 * @pclk_ctrl: Pointer to pixel clock control
 * @hblank_ctrl: Pointer to horizontal blanking control
 * @vblank_ctrl: Pointer to vertical blanking control
 * @exp_ctrl: Pointer to exposure control
 * @again_ctrl: Pointer to analog gain control
 * @vblank: Vertical blanking in lines
 * @cur_mode: Pointer to current selected sensor mode
 * @link_freq_bitmap: Menu bitmap for link_freq_ctrl
 * @cur_code: current selected format code
 */
struct imx334 {
	struct device *dev;
	struct regmap *cci;
	struct i2c_client *client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct gpio_desc *reset_gpio;
	struct clk *inclk;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *link_freq_ctrl;
	struct v4l2_ctrl *pclk_ctrl;
	struct v4l2_ctrl *hblank_ctrl;
	struct v4l2_ctrl *vblank_ctrl;
	struct {
		struct v4l2_ctrl *exp_ctrl;
		struct v4l2_ctrl *again_ctrl;
	};
	u32 vblank;
	const struct imx334_mode *cur_mode;
	unsigned long link_freq_bitmap;
	u32 cur_code;
};

static const s64 link_freq[] = {
	IMX334_LINK_FREQ_891M,
	IMX334_LINK_FREQ_445M,
};

/* Sensor common mode registers values */
static const struct cci_reg_sequence common_mode_regs[] = {
	{ IMX334_REG_MODE_SELECT, IMX334_MODE_STANDBY },
	{ IMX334_REG_WINMODE,		0x04 },
	{ IMX334_REG_VMAX,		0x0008ca },
	{ IMX334_REG_HMAX,		0x044c },
	{ IMX334_REG_BLACK_OFSET_ADR,	0x0000 },
	{ IMX334_REG_UNRD_LINE_MAX,	0x0000 },
	{ IMX334_REG_OPB_SIZE_V,	0x00 },
	{ IMX334_REG_HREVERSE,		0x00 },
	{ IMX334_REG_VREVERSE,		0x00 },
	{ IMX334_REG_UNREAD_PARAM5,	0x0000 },
	{ IMX334_REG_UNREAD_PARAM6,	0x0008 },
	{ IMX334_REG_XVS_XHS_OUTSEL,	0x20 },
	{ IMX334_REG_XVS_XHS_DRV,	0x0f },
	{ IMX334_REG_BCWAIT_TIME,	0x3b },
	{ IMX334_REG_CPWAIT_TIME,	0x2a },
	{ IMX334_REG_INCKSEL1,		0x0129 },
	{ IMX334_REG_INCKSEL2,		0x06 },
	{ IMX334_REG_INCKSEL3,		0xa0 },
	{ IMX334_REG_INCKSEL4,		0x7e },
	{ IMX334_REG_SYS_MODE,		0x02 },
	{ IMX334_REG_HADD_VADD,		0x00 },
	{ IMX334_REG_VALID_EXPAND,	0x03 },
	{ IMX334_REG_TCYCLE,		0x00 },
	{ IMX334_REG_TCLKPOST,		0x007f },
	{ IMX334_REG_TCLKPREPARE,	0x0037 },
	{ IMX334_REG_TCLKTRAIL,		0x0037 },
	{ IMX334_REG_TCLKZERO,		0xf7 },
	{ IMX334_REG_THSPREPARE,	0x002f },
	{ CCI_REG8(0x3078), 0x02 },
	{ CCI_REG8(0x3079), 0x00 },
	{ CCI_REG8(0x307a), 0x00 },
	{ CCI_REG8(0x307b), 0x00 },
	{ CCI_REG8(0x3080), 0x02 },
	{ CCI_REG8(0x3081), 0x00 },
	{ CCI_REG8(0x3082), 0x00 },
	{ CCI_REG8(0x3083), 0x00 },
	{ CCI_REG8(0x3088), 0x02 },
	{ CCI_REG8(0x3094), 0x00 },
	{ CCI_REG8(0x3095), 0x00 },
	{ CCI_REG8(0x3096), 0x00 },
	{ CCI_REG8(0x309b), 0x02 },
	{ CCI_REG8(0x309c), 0x00 },
	{ CCI_REG8(0x309d), 0x00 },
	{ CCI_REG8(0x309e), 0x00 },
	{ CCI_REG8(0x30a4), 0x00 },
	{ CCI_REG8(0x30a5), 0x00 },
	{ CCI_REG8(0x3288), 0x21 },
	{ CCI_REG8(0x328a), 0x02 },
	{ CCI_REG8(0x3414), 0x05 },
	{ CCI_REG8(0x3416), 0x18 },
	{ CCI_REG8(0x35Ac), 0x0e },
	{ CCI_REG8(0x3648), 0x01 },
	{ CCI_REG8(0x364a), 0x04 },
	{ CCI_REG8(0x364c), 0x04 },
	{ CCI_REG8(0x3678), 0x01 },
	{ CCI_REG8(0x367c), 0x31 },
	{ CCI_REG8(0x367e), 0x31 },
	{ CCI_REG8(0x3708), 0x02 },
	{ CCI_REG8(0x3714), 0x01 },
	{ CCI_REG8(0x3715), 0x02 },
	{ CCI_REG8(0x3716), 0x02 },
	{ CCI_REG8(0x3717), 0x02 },
	{ CCI_REG8(0x371c), 0x3d },
	{ CCI_REG8(0x371d), 0x3f },
	{ CCI_REG8(0x372c), 0x00 },
	{ CCI_REG8(0x372d), 0x00 },
	{ CCI_REG8(0x372e), 0x46 },
	{ CCI_REG8(0x372f), 0x00 },
	{ CCI_REG8(0x3730), 0x89 },
	{ CCI_REG8(0x3731), 0x00 },
	{ CCI_REG8(0x3732), 0x08 },
	{ CCI_REG8(0x3733), 0x01 },
	{ CCI_REG8(0x3734), 0xfe },
	{ CCI_REG8(0x3735), 0x05 },
	{ CCI_REG8(0x375d), 0x00 },
	{ CCI_REG8(0x375e), 0x00 },
	{ CCI_REG8(0x375f), 0x61 },
	{ CCI_REG8(0x3760), 0x06 },
	{ CCI_REG8(0x3768), 0x1b },
	{ CCI_REG8(0x3769), 0x1b },
	{ CCI_REG8(0x376a), 0x1a },
	{ CCI_REG8(0x376b), 0x19 },
	{ CCI_REG8(0x376c), 0x18 },
	{ CCI_REG8(0x376d), 0x14 },
	{ CCI_REG8(0x376e), 0x0f },
	{ CCI_REG8(0x3776), 0x00 },
	{ CCI_REG8(0x3777), 0x00 },
	{ CCI_REG8(0x3778), 0x46 },
	{ CCI_REG8(0x3779), 0x00 },
	{ CCI_REG8(0x377a), 0x08 },
	{ CCI_REG8(0x377b), 0x01 },
	{ CCI_REG8(0x377c), 0x45 },
	{ CCI_REG8(0x377d), 0x01 },
	{ CCI_REG8(0x377e), 0x23 },
	{ CCI_REG8(0x377f), 0x02 },
	{ CCI_REG8(0x3780), 0xd9 },
	{ CCI_REG8(0x3781), 0x03 },
	{ CCI_REG8(0x3782), 0xf5 },
	{ CCI_REG8(0x3783), 0x06 },
	{ CCI_REG8(0x3784), 0xa5 },
	{ CCI_REG8(0x3788), 0x0f },
	{ CCI_REG8(0x378a), 0xd9 },
	{ CCI_REG8(0x378b), 0x03 },
	{ CCI_REG8(0x378c), 0xeb },
	{ CCI_REG8(0x378d), 0x05 },
	{ CCI_REG8(0x378e), 0x87 },
	{ CCI_REG8(0x378f), 0x06 },
	{ CCI_REG8(0x3790), 0xf5 },
	{ CCI_REG8(0x3792), 0x43 },
	{ CCI_REG8(0x3794), 0x7a },
	{ CCI_REG8(0x3796), 0xa1 },
	{ CCI_REG8(0x37b0), 0x37 },
	{ CCI_REG8(0x3e04), 0x0e },
	{ IMX334_REG_AGAIN, 0x0050 },
	{ IMX334_REG_MASTER_MODE, 0x00 },
};

/* Sensor mode registers for 640x480@30fps */
static const struct cci_reg_sequence mode_640x480_regs[] = {
	{ IMX334_REG_HTRIMMING_START,	0x0670 },
	{ IMX334_REG_HNUM,		0x0280 },
	{ IMX334_REG_AREA3_ST_ADR_1,	0x0748 },
	{ IMX334_REG_AREA3_ST_ADR_2,	0x0749 },
	{ IMX334_REG_AREA3_WIDTH_1,	0x01e0 },
	{ IMX334_REG_AREA3_WIDTH_2,	0x01e0 },
	{ IMX334_REG_Y_OUT_SIZE,	0x01e0 },
	{ IMX334_REG_UNREAD_ED_ADR,	0x0b30 },
};

/* Sensor mode registers for 1280x720@30fps */
static const struct cci_reg_sequence mode_1280x720_regs[] = {
	{ IMX334_REG_HTRIMMING_START,	0x0530 },
	{ IMX334_REG_HNUM,		0x0500 },
	{ IMX334_REG_AREA3_ST_ADR_1,	0x0384 },
	{ IMX334_REG_AREA3_ST_ADR_2,	0x0385 },
	{ IMX334_REG_AREA3_WIDTH_1,	0x02d0 },
	{ IMX334_REG_AREA3_WIDTH_2,	0x02d0 },
	{ IMX334_REG_Y_OUT_SIZE,	0x02d0 },
	{ IMX334_REG_UNREAD_ED_ADR,	0x0b30 },
};

/* Sensor mode registers for 1920x1080@30fps */
static const struct cci_reg_sequence mode_1920x1080_regs[] = {
	{ IMX334_REG_HTRIMMING_START,	0x03f0 },
	{ IMX334_REG_HNUM,		0x0780 },
	{ IMX334_REG_AREA3_ST_ADR_1,	0x02cc },
	{ IMX334_REG_AREA3_ST_ADR_2,	0x02cd },
	{ IMX334_REG_AREA3_WIDTH_1,	0x0438 },
	{ IMX334_REG_AREA3_WIDTH_2,	0x0438 },
	{ IMX334_REG_Y_OUT_SIZE,	0x0438 },
	{ IMX334_REG_UNREAD_ED_ADR,	0x0a18 },
};

/* Sensor mode registers for 3840x2160@30fps */
static const struct cci_reg_sequence mode_3840x2160_regs[] = {
	{ IMX334_REG_HMAX,		0x0226 },
	{ IMX334_REG_INCKSEL2,		0x02 },
	{ IMX334_REG_HTRIMMING_START,	0x003c },
	{ IMX334_REG_HNUM,		0x0f00 },
	{ IMX334_REG_AREA3_ST_ADR_1,	0x00b0 },
	{ IMX334_REG_AREA3_ST_ADR_2,	0x00b1 },
	{ IMX334_REG_UNREAD_ED_ADR,	0x1220 },
	{ IMX334_REG_AREA3_WIDTH_1,	0x0870 },
	{ IMX334_REG_AREA3_WIDTH_2,	0x0870 },
	{ IMX334_REG_Y_OUT_SIZE,	0x0870 },
	{ IMX334_REG_SYS_MODE,		0x0100 },
	{ IMX334_REG_TCLKPOST,		0x00bf },
	{ IMX334_REG_TCLKPREPARE,	0x0067 },
	{ IMX334_REG_TCLKTRAIL,		0x006f },
	{ IMX334_REG_TCLKZERO,		0x1d7 },
	{ IMX334_REG_THSPREPARE,	0x006f },
	{ IMX334_REG_THSZERO,		0x00cf },
	{ IMX334_REG_THSTRAIL,		0x006f },
	{ IMX334_REG_THSEXIT,		0x00b7 },
	{ IMX334_REG_TPLX,		0x005f },
};

static const char * const imx334_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bars",
	"Horizontal Color Bars",
	"Black and Grey Bars",
	"Black Color",
	"White Color",
};

static const int imx334_test_pattern_val[] = {
	IMX334_TP_DISABLE,
	IMX334_TP_COLOR_HBARS,
	IMX334_TP_COLOR_VBARS,
	IMX334_TP_BLACK_GREY,
	IMX334_TP_BLACK,
	IMX334_TP_WHITE,
};

static const struct cci_reg_sequence raw10_framefmt_regs[] = {
	{ IMX334_REG_ADBIT,  0x00 },
	{ IMX334_REG_MDBIT,  0x00 },
	{ IMX334_REG_ADBIT1, 0x01ff },
};

static const struct cci_reg_sequence raw12_framefmt_regs[] = {
	{ IMX334_REG_ADBIT,  0x01 },
	{ IMX334_REG_MDBIT,  0x01 },
	{ IMX334_REG_ADBIT1, 0x0047 },
};

static const u32 imx334_mbus_codes[] = {
	MEDIA_BUS_FMT_SRGGB12_1X12,
	MEDIA_BUS_FMT_SRGGB10_1X10,
};

/* Supported sensor mode configurations */
static const struct imx334_mode supported_modes[] = {
	{
		.width = 3840,
		.height = 2160,
		.hblank = 560,
		.vblank = 2340,
		.vblank_min = 90,
		.vblank_max = 132840,
		.pclk = 594000000,
		.link_freq_idx = 0,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_3840x2160_regs),
			.regs = mode_3840x2160_regs,
		},
	}, {
		.width = 1920,
		.height = 1080,
		.hblank = 2480,
		.vblank = 1170,
		.vblank_min = 45,
		.vblank_max = 132840,
		.pclk = 297000000,
		.link_freq_idx = 1,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1920x1080_regs),
			.regs = mode_1920x1080_regs,
		},
	}, {
		.width = 1280,
		.height = 720,
		.hblank = 2480,
		.vblank = 1170,
		.vblank_min = 45,
		.vblank_max = 132840,
		.pclk = 297000000,
		.link_freq_idx = 1,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1280x720_regs),
			.regs = mode_1280x720_regs,
		},
	}, {
		.width = 640,
		.height = 480,
		.hblank = 2480,
		.vblank = 1170,
		.vblank_min = 45,
		.vblank_max = 132840,
		.pclk = 297000000,
		.link_freq_idx = 1,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_640x480_regs),
			.regs = mode_640x480_regs,
		},
	},
};

/**
 * to_imx334() - imv334 V4L2 sub-device to imx334 device.
 * @subdev: pointer to imx334 V4L2 sub-device
 *
 * Return: pointer to imx334 device
 */
static inline struct imx334 *to_imx334(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct imx334, sd);
}

/**
 * imx334_update_controls() - Update control ranges based on streaming mode
 * @imx334: pointer to imx334 device
 * @mode: pointer to imx334_mode sensor mode
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_update_controls(struct imx334 *imx334,
				  const struct imx334_mode *mode)
{
	int ret;

	ret = __v4l2_ctrl_s_ctrl(imx334->link_freq_ctrl, mode->link_freq_idx);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_modify_range(imx334->pclk_ctrl, mode->pclk,
				       mode->pclk, 1, mode->pclk);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_modify_range(imx334->hblank_ctrl, mode->hblank,
				       mode->hblank, 1, mode->hblank);
	if (ret)
		return ret;

	ret =  __v4l2_ctrl_modify_range(imx334->vblank_ctrl, mode->vblank_min,
					mode->vblank_max, 1, mode->vblank);
	if (ret)
		return ret;

	return __v4l2_ctrl_s_ctrl(imx334->vblank_ctrl, mode->vblank);
}

/**
 * imx334_update_exp_gain() - Set updated exposure and gain
 * @imx334: pointer to imx334 device
 * @exposure: updated exposure value
 * @gain: updated analog gain value
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_update_exp_gain(struct imx334 *imx334, u32 exposure, u32 gain)
{
	u32 lpfr, shutter;
	int ret_hold;
	int ret = 0;

	lpfr = imx334->vblank + imx334->cur_mode->height;
	shutter = lpfr - exposure;

	dev_dbg(imx334->dev, "Set long exp %u analog gain %u sh0 %u lpfr %u\n",
		exposure, gain, shutter, lpfr);

	cci_write(imx334->cci, IMX334_REG_HOLD, 1, &ret);
	cci_write(imx334->cci, IMX334_REG_VMAX, lpfr, &ret);
	cci_write(imx334->cci, IMX334_REG_SHUTTER, shutter, &ret);
	cci_write(imx334->cci, IMX334_REG_AGAIN, gain, &ret);

	ret_hold = cci_write(imx334->cci, IMX334_REG_HOLD, 0, NULL);
	if (ret_hold)
		return ret_hold;

	return ret;
}

/**
 * imx334_set_ctrl() - Set subdevice control
 * @ctrl: pointer to v4l2_ctrl structure
 *
 * Supported controls:
 * - V4L2_CID_VBLANK
 * - cluster controls:
 *   - V4L2_CID_ANALOGUE_GAIN
 *   - V4L2_CID_EXPOSURE
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx334 *imx334 =
		container_of(ctrl->handler, struct imx334, ctrl_handler);
	u32 analog_gain;
	u32 exposure;
	int ret;

	if (ctrl->id == V4L2_CID_VBLANK) {
		imx334->vblank = imx334->vblank_ctrl->val;

		dev_dbg(imx334->dev, "Received vblank %u, new lpfr %u\n",
			imx334->vblank,
			imx334->vblank + imx334->cur_mode->height);

		ret = __v4l2_ctrl_modify_range(imx334->exp_ctrl,
					       IMX334_EXPOSURE_MIN,
					       imx334->vblank +
					       imx334->cur_mode->height -
					       IMX334_EXPOSURE_OFFSET,
					       1, IMX334_EXPOSURE_DEFAULT);
		if (ret)
			return ret;
	}

	/* Set controls only if sensor is in power on state */
	if (!pm_runtime_get_if_in_use(imx334->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		exposure = imx334->exp_ctrl->val;
		analog_gain = imx334->again_ctrl->val;

		ret = imx334_update_exp_gain(imx334, exposure, analog_gain);

		break;
	case V4L2_CID_EXPOSURE:

		exposure = ctrl->val;
		analog_gain = imx334->again_ctrl->val;

		dev_dbg(imx334->dev, "Received exp %u analog gain %u\n",
			exposure, analog_gain);

		ret = imx334_update_exp_gain(imx334, exposure, analog_gain);

		break;
	case V4L2_CID_PIXEL_RATE:
	case V4L2_CID_LINK_FREQ:
	case V4L2_CID_HBLANK:
		ret = 0;
		break;
	case V4L2_CID_TEST_PATTERN:
		if (ctrl->val) {
			cci_write(imx334->cci, IMX334_TP_CLK_EN,
				  IMX334_TP_CLK_EN_VAL, NULL);
			cci_write(imx334->cci, IMX334_DIG_CLP_MODE, 0x0, NULL);
			cci_write(imx334->cci, IMX334_TPG_COLORW,
				  IMX334_TPG_COLORW_120P, NULL);
			cci_write(imx334->cci, IMX334_REG_TP,
				  imx334_test_pattern_val[ctrl->val], NULL);
			cci_write(imx334->cci, IMX334_TPG_EN_DOUT,
				  IMX334_TP_ENABLE, NULL);
		} else {
			cci_write(imx334->cci, IMX334_DIG_CLP_MODE, 0x1, NULL);
			cci_write(imx334->cci, IMX334_TP_CLK_EN,
				  IMX334_TP_CLK_DIS_VAL, NULL);
			cci_write(imx334->cci, IMX334_TPG_EN_DOUT,
				  IMX334_TP_DISABLE, NULL);
		}
		ret = 0;
		break;
	default:
		dev_err(imx334->dev, "Invalid control %d\n", ctrl->id);
		ret = -EINVAL;
	}

	pm_runtime_put(imx334->dev);

	return ret;
}

/* V4l2 subdevice control ops*/
static const struct v4l2_ctrl_ops imx334_ctrl_ops = {
	.s_ctrl = imx334_set_ctrl,
};

static int imx334_get_format_code(struct imx334 *imx334, u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(imx334_mbus_codes); i++) {
		if (imx334_mbus_codes[i] == code)
			return imx334_mbus_codes[i];
	}

	return imx334_mbus_codes[0];
}

/**
 * imx334_enum_mbus_code() - Enumerate V4L2 sub-device mbus codes
 * @sd: pointer to imx334 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device state
 * @code: V4L2 sub-device code enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(imx334_mbus_codes))
		return -EINVAL;

	code->code = imx334_mbus_codes[code->index];

	return 0;
}

/**
 * imx334_enum_frame_size() - Enumerate V4L2 sub-device frame sizes
 * @sd: pointer to imx334 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device state
 * @fsize: V4L2 sub-device size enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fsize)
{
	struct imx334 *imx334 = to_imx334(sd);
	u32 code;

	if (fsize->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	code = imx334_get_format_code(imx334, fsize->code);

	if (fsize->code != code)
		return -EINVAL;

	fsize->min_width = supported_modes[fsize->index].width;
	fsize->max_width = fsize->min_width;
	fsize->min_height = supported_modes[fsize->index].height;
	fsize->max_height = fsize->min_height;

	return 0;
}

/**
 * imx334_fill_pad_format() - Fill subdevice pad format
 *                            from selected sensor mode
 * @imx334: pointer to imx334 device
 * @mode: pointer to imx334_mode sensor mode
 * @fmt: V4L2 sub-device format need to be filled
 */
static void imx334_fill_pad_format(struct imx334 *imx334,
				   const struct imx334_mode *mode,
				   struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_RAW;
	fmt->format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->format.quantization = V4L2_QUANTIZATION_DEFAULT;
	fmt->format.xfer_func = V4L2_XFER_FUNC_NONE;
}

/**
 * imx334_get_pad_format() - Get subdevice pad format
 * @sd: pointer to imx334 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device state
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx334 *imx334 = to_imx334(sd);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		fmt->format = *framefmt;
	} else {
		fmt->format.code = imx334->cur_code;
		imx334_fill_pad_format(imx334, imx334->cur_mode, fmt);
	}

	return 0;
}

/**
 * imx334_set_pad_format() - Set subdevice pad format
 * @sd: pointer to imx334 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device state
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx334 *imx334 = to_imx334(sd);
	const struct imx334_mode *mode;
	int ret = 0;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	imx334_fill_pad_format(imx334, mode, fmt);
	fmt->format.code = imx334_get_format_code(imx334, fmt->format.code);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		*framefmt = fmt->format;
	} else if (imx334->cur_mode != mode || imx334->cur_code != fmt->format.code) {
		imx334->cur_code = fmt->format.code;
		ret = imx334_update_controls(imx334, mode);
		if (!ret)
			imx334->cur_mode = mode;
	}

	return ret;
}

/**
 * imx334_init_state() - Initialize sub-device state
 * @sd: pointer to imx334 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device state
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	struct imx334 *imx334 = to_imx334(sd);
	struct v4l2_subdev_format fmt = { 0 };

	fmt.which = sd_state ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;

	imx334_fill_pad_format(imx334, imx334->cur_mode, &fmt);

	__v4l2_ctrl_modify_range(imx334->link_freq_ctrl, 0,
				 __fls(imx334->link_freq_bitmap),
				 ~(imx334->link_freq_bitmap),
				 __ffs(imx334->link_freq_bitmap));

	return imx334_set_pad_format(sd, sd_state, &fmt);
}

static int imx334_set_framefmt(struct imx334 *imx334)
{
	switch (imx334->cur_code) {
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		return cci_multi_reg_write(imx334->cci, raw10_framefmt_regs,
					ARRAY_SIZE(raw10_framefmt_regs), NULL);


	case MEDIA_BUS_FMT_SRGGB12_1X12:
		return cci_multi_reg_write(imx334->cci, raw12_framefmt_regs,
					ARRAY_SIZE(raw12_framefmt_regs), NULL);
	}

	return -EINVAL;
}

/**
 * imx334_enable_streams() - Enable specified streams for the sensor
 * @sd: pointer to the V4L2 subdevice
 * @state: pointer to the subdevice state
 * @pad: pad number for which streams are enabled
 * @streams_mask: bitmask specifying the streams to enable
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct imx334 *imx334 = to_imx334(sd);
	const struct imx334_reg_list *reg_list;
	int ret;

	ret = pm_runtime_resume_and_get(imx334->dev);
	if (ret < 0)
		return ret;

	ret = cci_multi_reg_write(imx334->cci, common_mode_regs,
				  ARRAY_SIZE(common_mode_regs), NULL);
	if (ret) {
		dev_err(imx334->dev, "fail to write common registers\n");
		goto err_rpm_put;
	}

	/* Write sensor mode registers */
	reg_list = &imx334->cur_mode->reg_list;
	ret = cci_multi_reg_write(imx334->cci, reg_list->regs,
				  reg_list->num_of_regs, NULL);
	if (ret) {
		dev_err(imx334->dev, "fail to write initial registers\n");
		goto err_rpm_put;
	}

	ret = cci_write(imx334->cci, IMX334_REG_LANEMODE,
			IMX334_CSI_4_LANE_MODE, NULL);
	if (ret) {
		dev_err(imx334->dev, "failed to configure lanes\n");
		goto err_rpm_put;
	}

	ret = imx334_set_framefmt(imx334);
	if (ret) {
		dev_err(imx334->dev, "%s failed to set frame format: %d\n",
			__func__, ret);
		goto err_rpm_put;
	}

	/* Setup handler will write actual exposure and gain */
	ret =  __v4l2_ctrl_handler_setup(imx334->sd.ctrl_handler);
	if (ret) {
		dev_err(imx334->dev, "fail to setup handler\n");
		goto err_rpm_put;
	}

	/* Start streaming */
	ret = cci_write(imx334->cci, IMX334_REG_MODE_SELECT,
			IMX334_MODE_STREAMING, NULL);
	if (ret) {
		dev_err(imx334->dev, "fail to start streaming\n");
		goto err_rpm_put;
	}

	return 0;

err_rpm_put:
	pm_runtime_put(imx334->dev);
	return ret;
}

/**
 * imx334_disable_streams() - Enable specified streams for the sensor
 * @sd: pointer to the V4L2 subdevice
 * @state: pointer to the subdevice state
 * @pad: pad number for which streams are disabled
 * @streams_mask: bitmask specifying the streams to disable
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct imx334 *imx334 = to_imx334(sd);
	int ret;

	ret = cci_write(imx334->cci, IMX334_REG_MODE_SELECT,
			IMX334_MODE_STANDBY, NULL);
	if (ret)
		dev_err(imx334->dev, "%s failed to stop stream\n", __func__);

	pm_runtime_put(imx334->dev);

	return ret;
}

/**
 * imx334_detect() - Detect imx334 sensor
 * @imx334: pointer to imx334 device
 *
 * Return: 0 if successful, -EIO if sensor id does not match
 */
static int imx334_detect(struct imx334 *imx334)
{
	int ret;
	u64 val;

	ret = cci_read(imx334->cci, IMX334_REG_ID, &val, NULL);
	if (ret)
		return ret;

	if (val != IMX334_ID) {
		dev_err(imx334->dev, "chip id mismatch: %x!=%llx\n",
			IMX334_ID, val);
		return -ENXIO;
	}

	return 0;
}

/**
 * imx334_parse_hw_config() - Parse HW configuration and check if supported
 * @imx334: pointer to imx334 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_parse_hw_config(struct imx334 *imx334)
{
	struct fwnode_handle *fwnode = dev_fwnode(imx334->dev);
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct fwnode_handle *ep;
	unsigned long rate;
	int ret;

	if (!fwnode)
		return -ENXIO;

	/* Request optional reset pin */
	imx334->reset_gpio = devm_gpiod_get_optional(imx334->dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(imx334->reset_gpio))
		return dev_err_probe(imx334->dev, PTR_ERR(imx334->reset_gpio),
				     "failed to get reset gpio\n");

	/* Get sensor input clock */
	imx334->inclk = devm_v4l2_sensor_clk_get(imx334->dev, NULL);
	if (IS_ERR(imx334->inclk))
		return dev_err_probe(imx334->dev, PTR_ERR(imx334->inclk),
					 "could not get inclk\n");

	rate = clk_get_rate(imx334->inclk);
	if (rate != IMX334_INCLK_RATE)
		return dev_err_probe(imx334->dev, -EINVAL,
					 "inclk frequency mismatch\n");

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != IMX334_NUM_DATA_LANES) {
		dev_err(imx334->dev,
			"number of CSI2 data lanes %d is not supported\n",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	ret = v4l2_link_freq_to_bitmap(imx334->dev, bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       link_freq, ARRAY_SIZE(link_freq),
				       &imx334->link_freq_bitmap);

done_endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

/* V4l2 subdevice ops */
static const struct v4l2_subdev_video_ops imx334_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops imx334_pad_ops = {
	.enum_mbus_code = imx334_enum_mbus_code,
	.enum_frame_size = imx334_enum_frame_size,
	.get_fmt = imx334_get_pad_format,
	.set_fmt = imx334_set_pad_format,
	.enable_streams = imx334_enable_streams,
	.disable_streams = imx334_disable_streams,
};

static const struct v4l2_subdev_ops imx334_subdev_ops = {
	.video = &imx334_video_ops,
	.pad = &imx334_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx334_internal_ops = {
	.init_state = imx334_init_state,
};

/**
 * imx334_power_on() - Sensor power on sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx334 *imx334 = to_imx334(sd);
	int ret;

	/*
	 * Note: Misinterpretation of reset assertion - do not re-use this code.
	 * XCLR pin is using incorrect (for reset signal) logical level.
	 */
	gpiod_set_value_cansleep(imx334->reset_gpio, 1);

	ret = clk_prepare_enable(imx334->inclk);
	if (ret) {
		dev_err(imx334->dev, "fail to enable inclk\n");
		goto error_reset;
	}

	usleep_range(18000, 20000);

	return 0;

error_reset:
	gpiod_set_value_cansleep(imx334->reset_gpio, 0);

	return ret;
}

/**
 * imx334_power_off() - Sensor power off sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx334 *imx334 = to_imx334(sd);

	gpiod_set_value_cansleep(imx334->reset_gpio, 0);

	clk_disable_unprepare(imx334->inclk);

	return 0;
}

/**
 * imx334_init_controls() - Initialize sensor subdevice controls
 * @imx334: pointer to imx334 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_init_controls(struct imx334 *imx334)
{
	struct v4l2_ctrl_handler *ctrl_hdlr = &imx334->ctrl_handler;
	const struct imx334_mode *mode = imx334->cur_mode;
	u32 lpfr;
	int ret;

	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 7);
	if (ret)
		return ret;

	/* Initialize exposure and gain */
	lpfr = mode->vblank + mode->height;
	imx334->exp_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					     &imx334_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX334_EXPOSURE_MIN,
					     lpfr - IMX334_EXPOSURE_OFFSET,
					     IMX334_EXPOSURE_STEP,
					     IMX334_EXPOSURE_DEFAULT);

	imx334->again_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					       &imx334_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN,
					       IMX334_AGAIN_MIN,
					       IMX334_AGAIN_MAX,
					       IMX334_AGAIN_STEP,
					       IMX334_AGAIN_DEFAULT);

	v4l2_ctrl_cluster(2, &imx334->exp_ctrl);

	imx334->vblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&imx334_ctrl_ops,
						V4L2_CID_VBLANK,
						mode->vblank_min,
						mode->vblank_max,
						1, mode->vblank);

	/* Read only controls */
	imx334->pclk_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					      &imx334_ctrl_ops,
					      V4L2_CID_PIXEL_RATE,
					      mode->pclk, mode->pclk,
					      1, mode->pclk);

	imx334->link_freq_ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr,
							&imx334_ctrl_ops,
							V4L2_CID_LINK_FREQ,
							__fls(imx334->link_freq_bitmap),
							__ffs(imx334->link_freq_bitmap),
							link_freq);

	if (imx334->link_freq_ctrl)
		imx334->link_freq_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx334->hblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&imx334_ctrl_ops,
						V4L2_CID_HBLANK,
						IMX334_REG_MIN,
						IMX334_REG_MAX,
						1, mode->hblank);
	if (imx334->hblank_ctrl)
		imx334->hblank_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &imx334_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx334_test_pattern_menu) - 1,
				     0, 0, imx334_test_pattern_menu);

	if (ctrl_hdlr->error) {
		dev_err(imx334->dev, "control init failed: %d",
			ctrl_hdlr->error);
		v4l2_ctrl_handler_free(ctrl_hdlr);
		return ctrl_hdlr->error;
	}

	imx334->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

/**
 * imx334_probe() - I2C client device binding
 * @client: pointer to i2c client device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx334_probe(struct i2c_client *client)
{
	struct imx334 *imx334;
	int ret;

	imx334 = devm_kzalloc(&client->dev, sizeof(*imx334), GFP_KERNEL);
	if (!imx334)
		return -ENOMEM;

	imx334->dev = &client->dev;
	imx334->cci = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(imx334->cci)) {
		dev_err(imx334->dev, "Unable to initialize I2C\n");
		return -ENODEV;
	}

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&imx334->sd, client, &imx334_subdev_ops);
	imx334->sd.internal_ops = &imx334_internal_ops;

	ret = imx334_parse_hw_config(imx334);
	if (ret)
		return dev_err_probe(imx334->dev, ret,
					"HW configuration is not supported\n");

	ret = imx334_power_on(imx334->dev);
	if (ret) {
		dev_err_probe(imx334->dev, ret, "failed to power-on the sensor\n");
		return ret;
	}

	/* Check module identity */
	ret = imx334_detect(imx334);
	if (ret) {
		dev_err(imx334->dev, "failed to find sensor: %d\n", ret);
		goto error_power_off;
	}

	/* Set default mode to max resolution */
	imx334->cur_mode = &supported_modes[__ffs(imx334->link_freq_bitmap)];
	imx334->cur_code = imx334_mbus_codes[0];
	imx334->vblank = imx334->cur_mode->vblank;

	ret = imx334_init_controls(imx334);
	if (ret) {
		dev_err(imx334->dev, "failed to init controls: %d\n", ret);
		goto error_power_off;
	}

	/* Initialize subdev */
	imx334->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx334->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	imx334->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&imx334->sd.entity, 1, &imx334->pad);
	if (ret) {
		dev_err(imx334->dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	imx334->sd.state_lock = imx334->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&imx334->sd);
	if (ret < 0) {
		dev_err(imx334->dev, "subdev init error: %d\n", ret);
		goto error_media_entity;
	}

	pm_runtime_set_active(imx334->dev);
	pm_runtime_enable(imx334->dev);

	ret = v4l2_async_register_subdev_sensor(&imx334->sd);
	if (ret < 0) {
		dev_err(imx334->dev,
			"failed to register async subdev: %d\n", ret);
		goto error_subdev_cleanup;
	}

	pm_runtime_idle(imx334->dev);

	return 0;

error_subdev_cleanup:
	v4l2_subdev_cleanup(&imx334->sd);
	pm_runtime_disable(imx334->dev);
	pm_runtime_set_suspended(imx334->dev);

error_media_entity:
	media_entity_cleanup(&imx334->sd.entity);

error_handler_free:
	v4l2_ctrl_handler_free(imx334->sd.ctrl_handler);

error_power_off:
	imx334_power_off(imx334->dev);

	return ret;
}

/**
 * imx334_remove() - I2C client device unbinding
 * @client: pointer to I2C client device
 *
 * Return: 0 if successful, error code otherwise.
 */
static void imx334_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev)) {
		imx334_power_off(&client->dev);
		pm_runtime_set_suspended(&client->dev);
	}
}

static const struct dev_pm_ops imx334_pm_ops = {
	SET_RUNTIME_PM_OPS(imx334_power_off, imx334_power_on, NULL)
};

static const struct of_device_id imx334_of_match[] = {
	{ .compatible = "sony,imx334" },
	{ }
};

MODULE_DEVICE_TABLE(of, imx334_of_match);

static struct i2c_driver imx334_driver = {
	.probe = imx334_probe,
	.remove = imx334_remove,
	.driver = {
		.name = "imx334",
		.pm = &imx334_pm_ops,
		.of_match_table = imx334_of_match,
	},
};

module_i2c_driver(imx334_driver);

MODULE_DESCRIPTION("Sony imx334 sensor driver");
MODULE_LICENSE("GPL");
