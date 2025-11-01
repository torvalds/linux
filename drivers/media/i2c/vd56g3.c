// SPDX-License-Identifier: GPL-2.0
/*
 * A V4L2 driver for ST VD56G3 (Mono) and VD66GY (RGB) global shutter cameras.
 * Copyright (C) 2024, STMicroelectronics SA
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/unaligned.h>
#include <linux/units.h>

#include <media/mipi-csi2.h>
#include <media/v4l2-async.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

/* Register Map */
#define VD56G3_REG_MODEL_ID				CCI_REG16_LE(0x0000)
#define VD56G3_MODEL_ID					0x5603
#define VD56G3_REG_REVISION				CCI_REG16_LE(0x0002)
#define VD56G3_REVISION_CUT3				0x31
#define VD56G3_REG_OPTICAL_REVISION			CCI_REG8(0x001a)
#define VD56G3_OPTICAL_REVISION_MONO			0
#define VD56G3_OPTICAL_REVISION_BAYER			1
#define VD56G3_REG_SYSTEM_FSM				CCI_REG8(0x0028)
#define VD56G3_SYSTEM_FSM_READY_TO_BOOT			0x01
#define VD56G3_SYSTEM_FSM_SW_STBY			0x02
#define VD56G3_SYSTEM_FSM_STREAMING			0x03
#define VD56G3_REG_APPLIED_COARSE_EXPOSURE		CCI_REG16_LE(0x0064)
#define VD56G3_REG_APPLIED_ANALOG_GAIN			CCI_REG8(0x0068)
#define VD56G3_REG_APPLIED_DIGITAL_GAIN			CCI_REG16_LE(0x006a)
#define VD56G3_REG_BOOT					CCI_REG8(0x0200)
#define VD56G3_CMD_ACK					0
#define VD56G3_CMD_BOOT					1
#define VD56G3_REG_STBY					CCI_REG8(0x0201)
#define VD56G3_CMD_START_STREAM				1
#define VD56G3_REG_STREAMING				CCI_REG8(0x0202)
#define VD56G3_CMD_STOP_STREAM				1
#define VD56G3_REG_EXT_CLOCK				CCI_REG32_LE(0x0220)
#define VD56G3_REG_CLK_PLL_PREDIV			CCI_REG8(0x0224)
#define VD56G3_REG_CLK_SYS_PLL_MULT			CCI_REG8(0x0226)
#define VD56G3_REG_ORIENTATION				CCI_REG8(0x0302)
#define VD56G3_REG_FORMAT_CTRL				CCI_REG8(0x030a)
#define VD56G3_REG_OIF_CTRL				CCI_REG16_LE(0x030c)
#define VD56G3_REG_OIF_IMG_CTRL				CCI_REG8(0x030f)
#define VD56G3_REG_OIF_CSI_BITRATE			CCI_REG16_LE(0x0312)
#define VD56G3_REG_DUSTER_CTRL				CCI_REG8(0x0318)
#define VD56G3_DUSTER_DISABLE				0
#define VD56G3_DUSTER_ENABLE_DEF_MODULES		0x13
#define VD56G3_REG_ISL_ENABLE				CCI_REG8(0x0333)
#define VD56G3_REG_DARKCAL_CTRL				CCI_REG8(0x0340)
#define VD56G3_DARKCAL_ENABLE				1
#define VD56G3_DARKCAL_DISABLE_DARKAVG			2
#define VD56G3_REG_PATGEN_CTRL				CCI_REG16_LE(0x0400)
#define VD56G3_PATGEN_ENABLE				1
#define VD56G3_PATGEN_TYPE_SHIFT			4
#define VD56G3_REG_AE_COLDSTART_COARSE_EXPOSURE		CCI_REG16_LE(0x042a)
#define VD56G3_REG_AE_COLDSTART_ANALOG_GAIN		CCI_REG8(0x042c)
#define VD56G3_REG_AE_COLDSTART_DIGITAL_GAIN		CCI_REG16_LE(0x042e)
#define VD56G3_REG_AE_ROI_START_H			CCI_REG16_LE(0x0432)
#define VD56G3_REG_AE_ROI_START_V			CCI_REG16_LE(0x0434)
#define VD56G3_REG_AE_ROI_END_H				CCI_REG16_LE(0x0436)
#define VD56G3_REG_AE_ROI_END_V				CCI_REG16_LE(0x0438)
#define VD56G3_REG_AE_COMPENSATION			CCI_REG16_LE(0x043a)
#define VD56G3_REG_EXP_MODE				CCI_REG8(0x044c)
#define VD56G3_EXP_MODE_AUTO				0
#define VD56G3_EXP_MODE_FREEZE				1
#define VD56G3_EXP_MODE_MANUAL				2
#define VD56G3_REG_MANUAL_ANALOG_GAIN			CCI_REG8(0x044d)
#define VD56G3_REG_MANUAL_COARSE_EXPOSURE		CCI_REG16_LE(0x044e)
#define VD56G3_REG_MANUAL_DIGITAL_GAIN_CH0		CCI_REG16_LE(0x0450)
#define VD56G3_REG_MANUAL_DIGITAL_GAIN_CH1		CCI_REG16_LE(0x0452)
#define VD56G3_REG_MANUAL_DIGITAL_GAIN_CH2		CCI_REG16_LE(0x0454)
#define VD56G3_REG_MANUAL_DIGITAL_GAIN_CH3		CCI_REG16_LE(0x0456)
#define VD56G3_REG_FRAME_LENGTH				CCI_REG16_LE(0x0458)
#define VD56G3_REG_Y_START				CCI_REG16_LE(0x045a)
#define VD56G3_REG_Y_END				CCI_REG16_LE(0x045c)
#define VD56G3_REG_OUT_ROI_X_START			CCI_REG16_LE(0x045e)
#define VD56G3_REG_OUT_ROI_X_END			CCI_REG16_LE(0x0460)
#define VD56G3_REG_OUT_ROI_Y_START			CCI_REG16_LE(0x0462)
#define VD56G3_REG_OUT_ROI_Y_END			CCI_REG16_LE(0x0464)
#define VD56G3_REG_GPIO_0_CTRL				CCI_REG8(0x0467)
#define VD56G3_GPIOX_GPIO_IN				0x01
#define VD56G3_GPIOX_STROBE_MODE			0x02
#define VD56G3_REG_READOUT_CTRL				CCI_REG8(0x047e)
#define READOUT_NORMAL					0x00
#define READOUT_DIGITAL_BINNING_X2			0x01

/* The VD56G3 is a portrait image sensor with native resolution of 1124x1364. */
#define VD56G3_NATIVE_WIDTH				1124
#define VD56G3_NATIVE_HEIGHT				1364
#define VD56G3_DEFAULT_MODE				0

/* PLL settings */
#define VD56G3_TARGET_PLL				804000000UL
#define VD56G3_VT_CLOCK_DIV				5

/* External clock must be in [6Mhz-27Mhz] */
#define VD56G3_XCLK_FREQ_MIN				 (6 * HZ_PER_MHZ)
#define VD56G3_XCLK_FREQ_MAX				 (27 * HZ_PER_MHZ)

/* Line length and Frame length (settings are for standard 10bits ADC mode) */
#define VD56G3_LINE_LENGTH_MIN				1236
#define VD56G3_VBLANK_MIN				110
#define VD56G3_FRAME_LENGTH_DEF_60FPS			2168
#define VD56G3_FRAME_LENGTH_MAX				0xffff

/* Exposure settings */
#define VD56G3_EXPOSURE_MARGIN				75
#define VD56G3_EXPOSURE_MIN				5
#define VD56G3_EXPOSURE_DEFAULT				1420

/* Output Interface settings */
#define VD56G3_MAX_CSI_DATA_LANES			2
#define VD56G3_LINK_FREQ_DEF_1LANE			750000000UL
#define VD56G3_LINK_FREQ_DEF_2LANES			402000000UL

/* GPIOs */
#define VD56G3_NB_GPIOS					8

/* regulator supplies */
static const char *const vd56g3_supply_names[] = {
	"vcore",
	"vddio",
	"vana",
};

/* -----------------------------------------------------------------------------
 * Models (VD56G3: Mono, VD66GY: Bayer RGB), Modes and formats
 */

enum vd56g3_models {
	VD56G3_MODEL_VD56G3,
	VD56G3_MODEL_VD66GY,
};

struct vd56g3_mode {
	u32 width;
	u32 height;
};

static const struct vd56g3_mode vd56g3_supported_modes[] = {
	{
		.width = VD56G3_NATIVE_WIDTH,
		.height = VD56G3_NATIVE_HEIGHT,
	},
	{
		.width = 1120,
		.height = 1360,
	},
	{
		.width = 1024,
		.height = 1280,
	},
	{
		.width = 1024,
		.height = 768,
	},
	{
		.width = 768,
		.height = 1024,
	},
	{
		.width = 720,
		.height = 1280,
	},
	{
		.width = 640,
		.height = 480,
	},
	{
		.width = 480,
		.height = 640,
	},
	{
		.width = 320,
		.height = 240,
	},
};

/*
 * Sensor support 8bits and 10bits output in both variants
 *  - Monochrome
 *  - RGB (with all H/V flip variations)
 */
static const unsigned int vd56g3_mbus_codes[2][5] = {
	{
		MEDIA_BUS_FMT_Y8_1X8,
		MEDIA_BUS_FMT_SGRBG8_1X8,
		MEDIA_BUS_FMT_SRGGB8_1X8,
		MEDIA_BUS_FMT_SBGGR8_1X8,
		MEDIA_BUS_FMT_SGBRG8_1X8,
	},
	{
		MEDIA_BUS_FMT_Y10_1X10,
		MEDIA_BUS_FMT_SGRBG10_1X10,
		MEDIA_BUS_FMT_SRGGB10_1X10,
		MEDIA_BUS_FMT_SBGGR10_1X10,
		MEDIA_BUS_FMT_SGBRG10_1X10,
	},
};

struct vd56g3 {
	struct device *dev;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct regulator_bulk_data supplies[ARRAY_SIZE(vd56g3_supply_names)];
	struct gpio_desc *reset_gpio;
	struct clk *xclk;
	struct regmap *regmap;
	u32 xclk_freq;
	u32 pll_prediv;
	u32 pll_mult;
	u32 pixel_clock;
	u16 oif_ctrl;
	u8 nb_of_lane;
	u32 gpios[VD56G3_NB_GPIOS];
	unsigned long ext_leds_mask;
	bool is_mono;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *hblank_ctrl;
	struct v4l2_ctrl *vblank_ctrl;
	struct {
		struct v4l2_ctrl *hflip_ctrl;
		struct v4l2_ctrl *vflip_ctrl;
	};
	struct v4l2_ctrl *patgen_ctrl;
	struct {
		struct v4l2_ctrl *ae_ctrl;
		struct v4l2_ctrl *expo_ctrl;
		struct v4l2_ctrl *again_ctrl;
		struct v4l2_ctrl *dgain_ctrl;
	};
	struct v4l2_ctrl *ae_lock_ctrl;
	struct v4l2_ctrl *ae_bias_ctrl;
	struct v4l2_ctrl *led_ctrl;
};

static inline struct vd56g3 *to_vd56g3(struct v4l2_subdev *sd)
{
	return container_of_const(sd, struct vd56g3, sd);
}

static inline struct vd56g3 *ctrl_to_vd56g3(struct v4l2_ctrl *ctrl)
{
	return container_of_const(ctrl->handler, struct vd56g3, ctrl_handler);
}

/* -----------------------------------------------------------------------------
 * Additional i2c register helpers
 */

static int vd56g3_poll_reg(struct vd56g3 *sensor, u32 reg, u8 poll_val,
			   int *err)
{
	unsigned int val = 0;
	int ret;

	if (err && *err)
		return *err;

	/*
	 * Timeout must be higher than longuest frame duration. With current
	 * blanking constraints, frame duration can take up to 504ms.
	 */
	ret = regmap_read_poll_timeout(sensor->regmap, CCI_REG_ADDR(reg), val,
				       (val == poll_val), 2000,
				       600 * USEC_PER_MSEC);

	if (ret && err)
		*err = ret;

	return ret;
}

static int vd56g3_wait_state(struct vd56g3 *sensor, int state, int *err)
{
	return vd56g3_poll_reg(sensor, VD56G3_REG_SYSTEM_FSM, state, err);
}

/* -----------------------------------------------------------------------------
 * Controls: definitions, helpers and handlers
 */

static const char *const vd56g3_tp_menu[] = { "Disabled",
					      "Solid Color",
					      "Vertical Color Bars",
					      "Horizontal Gray Scale",
					      "Vertical Gray Scale",
					      "Diagonal Gray Scale",
					      "Pseudo Random" };

static const s64 vd56g3_ev_bias_qmenu[] = { -4000, -3500, -3000, -2500, -2000,
					    -1500, -1000, -500,	 0,	500,
					    1000,  1500,  2000,	 2500,	3000,
					    3500,  4000 };

static const s64 vd56g3_link_freq_1lane[] = { VD56G3_LINK_FREQ_DEF_1LANE };

static const s64 vd56g3_link_freq_2lanes[] = { VD56G3_LINK_FREQ_DEF_2LANES };

static u8 vd56g3_get_bpp(__u32 code)
{
	switch (code) {
	case MEDIA_BUS_FMT_Y8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	default:
		return 8;
	case MEDIA_BUS_FMT_Y10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
		return 10;
	}
}

static u8 vd56g3_get_datatype(__u32 code)
{
	switch (code) {
	case MEDIA_BUS_FMT_Y8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	default:
		return MIPI_CSI2_DT_RAW8;
	case MEDIA_BUS_FMT_Y10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
		return MIPI_CSI2_DT_RAW10;
	}
}

static int vd56g3_read_expo_cluster(struct vd56g3 *sensor, bool force_cur_val)
{
	u64 exposure;
	u64 again;
	u64 dgain;
	int ret = 0;

	/*
	 * When 'force_cur_val' is enabled, save the ctrl value in 'cur.val'
	 * instead of the normal 'val', this is used during poweroff to cache
	 * volatile ctrls and enable coldstart.
	 */
	cci_read(sensor->regmap, VD56G3_REG_APPLIED_COARSE_EXPOSURE, &exposure,
		 &ret);
	cci_read(sensor->regmap, VD56G3_REG_APPLIED_ANALOG_GAIN, &again, &ret);
	cci_read(sensor->regmap, VD56G3_REG_APPLIED_DIGITAL_GAIN, &dgain, &ret);
	if (ret)
		return ret;

	if (force_cur_val) {
		sensor->expo_ctrl->cur.val = exposure;
		sensor->again_ctrl->cur.val = again;
		sensor->dgain_ctrl->cur.val = dgain;
	} else {
		sensor->expo_ctrl->val = exposure;
		sensor->again_ctrl->val = again;
		sensor->dgain_ctrl->val = dgain;
	}

	return ret;
}

static int vd56g3_update_patgen(struct vd56g3 *sensor, u32 patgen_index)
{
	u32 pattern = patgen_index <= 2 ? patgen_index : patgen_index + 13;
	u16 patgen = pattern << VD56G3_PATGEN_TYPE_SHIFT;
	u8 duster = VD56G3_DUSTER_ENABLE_DEF_MODULES;
	u8 darkcal = VD56G3_DARKCAL_ENABLE;
	int ret = 0;

	if (patgen_index) {
		patgen |= VD56G3_PATGEN_ENABLE;
		duster = VD56G3_DUSTER_DISABLE;
		darkcal = VD56G3_DARKCAL_DISABLE_DARKAVG;
	}

	cci_write(sensor->regmap, VD56G3_REG_DUSTER_CTRL, duster, &ret);
	cci_write(sensor->regmap, VD56G3_REG_DARKCAL_CTRL, darkcal, &ret);
	cci_write(sensor->regmap, VD56G3_REG_PATGEN_CTRL, patgen, &ret);

	return ret;
}

static int vd56g3_update_expo_cluster(struct vd56g3 *sensor, bool is_auto)
{
	u8 expo_state = is_auto ? VD56G3_EXP_MODE_AUTO : VD56G3_EXP_MODE_MANUAL;
	int ret = 0;

	if (sensor->ae_ctrl->is_new)
		cci_write(sensor->regmap, VD56G3_REG_EXP_MODE, expo_state,
			  &ret);

	/* In Auto expo, set coldstart parameters */
	if (is_auto && sensor->ae_ctrl->is_new) {
		cci_write(sensor->regmap,
			  VD56G3_REG_AE_COLDSTART_COARSE_EXPOSURE,
			  sensor->expo_ctrl->val, &ret);
		cci_write(sensor->regmap, VD56G3_REG_AE_COLDSTART_ANALOG_GAIN,
			  sensor->again_ctrl->val, &ret);
		cci_write(sensor->regmap, VD56G3_REG_AE_COLDSTART_DIGITAL_GAIN,
			  sensor->dgain_ctrl->val, &ret);
	}

	/* In Manual expo, set exposure, analog and digital gains */
	if (!is_auto && sensor->expo_ctrl->is_new)
		cci_write(sensor->regmap, VD56G3_REG_MANUAL_COARSE_EXPOSURE,
			  sensor->expo_ctrl->val, &ret);

	if (!is_auto && sensor->again_ctrl->is_new)
		cci_write(sensor->regmap, VD56G3_REG_MANUAL_ANALOG_GAIN,
			  sensor->again_ctrl->val, &ret);

	if (!is_auto && sensor->dgain_ctrl->is_new) {
		cci_write(sensor->regmap, VD56G3_REG_MANUAL_DIGITAL_GAIN_CH0,
			  sensor->dgain_ctrl->val, &ret);
		cci_write(sensor->regmap, VD56G3_REG_MANUAL_DIGITAL_GAIN_CH1,
			  sensor->dgain_ctrl->val, &ret);
		cci_write(sensor->regmap, VD56G3_REG_MANUAL_DIGITAL_GAIN_CH2,
			  sensor->dgain_ctrl->val, &ret);
		cci_write(sensor->regmap, VD56G3_REG_MANUAL_DIGITAL_GAIN_CH3,
			  sensor->dgain_ctrl->val, &ret);
	}

	return ret;
}

static int vd56g3_lock_exposure(struct vd56g3 *sensor, u32 lock_val)
{
	bool ae_lock = lock_val & V4L2_LOCK_EXPOSURE;
	u8 expo_state = ae_lock ? VD56G3_EXP_MODE_FREEZE : VD56G3_EXP_MODE_AUTO;

	if (sensor->ae_ctrl->val == V4L2_EXPOSURE_AUTO)
		return cci_write(sensor->regmap, VD56G3_REG_EXP_MODE,
				 expo_state, NULL);

	return 0;
}

static int vd56g3_write_gpiox(struct vd56g3 *sensor, unsigned long gpio_mask)
{
	unsigned long io;
	u32 gpio_val;
	int ret = 0;

	for_each_set_bit(io, &gpio_mask, VD56G3_NB_GPIOS) {
		gpio_val = sensor->gpios[io];

		if (gpio_val == VD56G3_GPIOX_STROBE_MODE &&
		    sensor->led_ctrl->val == V4L2_FLASH_LED_MODE_NONE)
			gpio_val = VD56G3_GPIOX_GPIO_IN;

		cci_write(sensor->regmap, VD56G3_REG_GPIO_0_CTRL + io, gpio_val,
			  &ret);
	}

	return ret;
}

static int vd56g3_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vd56g3 *sensor = ctrl_to_vd56g3(ctrl);
	int ret = 0;

	/* Interact with HW only when it is powered ON */
	if (!pm_runtime_get_if_in_use(sensor->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE_AUTO:
		ret = vd56g3_read_expo_cluster(sensor, false);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put_autosuspend(sensor->dev);

	return ret;
}

static int vd56g3_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vd56g3 *sensor = ctrl_to_vd56g3(ctrl);
	struct v4l2_subdev_state *state;
	const struct v4l2_rect *crop;
	unsigned int frame_length = 0;
	unsigned int expo_max;
	unsigned int ae_compensation;
	bool is_auto = false;
	int ret = 0;

	state = v4l2_subdev_get_locked_active_state(&sensor->sd);
	crop = v4l2_subdev_state_get_crop(state, 0);

	if (ctrl->flags & V4L2_CTRL_FLAG_READ_ONLY)
		return 0;

	/* Update controls state, range, etc. whatever the state of the HW */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		frame_length = crop->height + ctrl->val;
		expo_max = frame_length - VD56G3_EXPOSURE_MARGIN;
		ret = __v4l2_ctrl_modify_range(sensor->expo_ctrl,
					       VD56G3_EXPOSURE_MIN, expo_max, 1,
					       min(VD56G3_EXPOSURE_DEFAULT,
						   expo_max));
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		is_auto = (ctrl->val == V4L2_EXPOSURE_AUTO);
		__v4l2_ctrl_grab(sensor->ae_lock_ctrl, !is_auto);
		__v4l2_ctrl_grab(sensor->ae_bias_ctrl, !is_auto);
		break;
	default:
		break;
	}

	if (ret)
		return ret;

	/* Interact with HW only when it is powered ON */
	if (!pm_runtime_get_if_in_use(sensor->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		ret = cci_write(sensor->regmap, VD56G3_REG_ORIENTATION,
				sensor->hflip_ctrl->val |
					(sensor->vflip_ctrl->val << 1),
				NULL);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = vd56g3_update_patgen(sensor, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		ret = vd56g3_update_expo_cluster(sensor, is_auto);
		break;
	case V4L2_CID_3A_LOCK:
		ret = vd56g3_lock_exposure(sensor, ctrl->val);
		break;
	case V4L2_CID_AUTO_EXPOSURE_BIAS:
		ae_compensation =
			DIV_ROUND_CLOSEST((int)vd56g3_ev_bias_qmenu[ctrl->val] *
					  256, 1000);
		ret = cci_write(sensor->regmap, VD56G3_REG_AE_COMPENSATION,
				ae_compensation, NULL);
		break;
	case V4L2_CID_VBLANK:
		ret = cci_write(sensor->regmap, VD56G3_REG_FRAME_LENGTH,
				frame_length, NULL);
		break;
	case V4L2_CID_FLASH_LED_MODE:
		ret = vd56g3_write_gpiox(sensor, sensor->ext_leds_mask);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put_autosuspend(sensor->dev);

	return ret;
}

static const struct v4l2_ctrl_ops vd56g3_ctrl_ops = {
	.g_volatile_ctrl = vd56g3_g_volatile_ctrl,
	.s_ctrl = vd56g3_s_ctrl,
};

static int vd56g3_update_controls(struct vd56g3 *sensor)
{
	struct v4l2_subdev_state *state;
	const struct v4l2_rect *crop;
	unsigned int hblank;
	unsigned int vblank_min, vblank, vblank_max;
	unsigned int frame_length;
	unsigned int expo_max;
	int ret;

	state = v4l2_subdev_get_locked_active_state(&sensor->sd);
	crop = v4l2_subdev_state_get_crop(state, 0);
	hblank = VD56G3_LINE_LENGTH_MIN - crop->width;
	vblank_min = VD56G3_VBLANK_MIN;
	vblank = VD56G3_FRAME_LENGTH_DEF_60FPS - crop->height;
	vblank_max = VD56G3_FRAME_LENGTH_MAX - crop->height;
	frame_length = crop->height + vblank;
	expo_max = frame_length - VD56G3_EXPOSURE_MARGIN;

	/* Update blanking and exposure (ranges + values) */
	ret = __v4l2_ctrl_modify_range(sensor->hblank_ctrl, hblank, hblank, 1,
				       hblank);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_modify_range(sensor->vblank_ctrl, vblank_min,
				       vblank_max, 1, vblank);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_s_ctrl(sensor->vblank_ctrl, vblank);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_modify_range(sensor->expo_ctrl, VD56G3_EXPOSURE_MIN,
				       expo_max, 1, VD56G3_EXPOSURE_DEFAULT);
	if (ret)
		return ret;

	return __v4l2_ctrl_s_ctrl(sensor->expo_ctrl, VD56G3_EXPOSURE_DEFAULT);
}

static int vd56g3_init_controls(struct vd56g3 *sensor)
{
	const struct v4l2_ctrl_ops *ops = &vd56g3_ctrl_ops;
	struct v4l2_ctrl_handler *hdl = &sensor->ctrl_handler;
	struct v4l2_fwnode_device_properties fwnode_props;
	struct v4l2_ctrl *ctrl;
	int ret;

	v4l2_ctrl_handler_init(hdl, 25);

	/* Horizontal & vertical flips modify bayer code on RGB variant */
	sensor->hflip_ctrl =
		v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	if (sensor->hflip_ctrl)
		sensor->hflip_ctrl->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	sensor->vflip_ctrl =
		v4l2_ctrl_new_std(hdl, ops, V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (sensor->vflip_ctrl)
		sensor->vflip_ctrl->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	sensor->patgen_ctrl =
		v4l2_ctrl_new_std_menu_items(hdl, ops, V4L2_CID_TEST_PATTERN,
					     ARRAY_SIZE(vd56g3_tp_menu) - 1, 0,
					     0, vd56g3_tp_menu);

	ctrl = v4l2_ctrl_new_int_menu(hdl, ops, V4L2_CID_LINK_FREQ,
				      ARRAY_SIZE(vd56g3_link_freq_1lane) - 1, 0,
				      (sensor->nb_of_lane == 2) ?
					      vd56g3_link_freq_2lanes :
					      vd56g3_link_freq_1lane);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ctrl = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_PIXEL_RATE,
				 sensor->pixel_clock, sensor->pixel_clock, 1,
				 sensor->pixel_clock);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	sensor->ae_ctrl = v4l2_ctrl_new_std_menu(hdl, ops,
						 V4L2_CID_EXPOSURE_AUTO,
						 V4L2_EXPOSURE_MANUAL, 0,
						 V4L2_EXPOSURE_AUTO);

	sensor->ae_lock_ctrl = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_3A_LOCK, 0,
						 GENMASK(2, 0), 0, 0);

	sensor->ae_bias_ctrl =
		v4l2_ctrl_new_int_menu(hdl, ops, V4L2_CID_AUTO_EXPOSURE_BIAS,
				       ARRAY_SIZE(vd56g3_ev_bias_qmenu) - 1,
				       ARRAY_SIZE(vd56g3_ev_bias_qmenu) / 2,
				       vd56g3_ev_bias_qmenu);

	/*
	 * Analog gain [1, 8] is computed with the following logic :
	 * 32/(32 - again_reg), with again_reg in the range [0:28]
	 * Digital gain [1.00, 8.00] is coded as a Fixed Point 5.8
	 */
	sensor->again_ctrl = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_ANALOGUE_GAIN,
					       0, 28, 1, 0);
	sensor->dgain_ctrl = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_DIGITAL_GAIN,
					       0x100, 0x800, 1, 0x100);

	/*
	 * Set the exposure, horizontal and vertical blanking ctrls
	 * to hardcoded values, they will be updated in vd56g3_update_controls.
	 * Exposure being in an auto-cluster, set a significant value here.
	 */
	sensor->expo_ctrl = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_EXPOSURE,
					      VD56G3_EXPOSURE_DEFAULT,
					      VD56G3_EXPOSURE_DEFAULT, 1,
					      VD56G3_EXPOSURE_DEFAULT);
	sensor->hblank_ctrl =
		v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HBLANK, 1, 1, 1, 1);
	if (sensor->hblank_ctrl)
		sensor->hblank_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	sensor->vblank_ctrl =
		v4l2_ctrl_new_std(hdl, ops, V4L2_CID_VBLANK, 1, 1, 1, 1);

	/* Additional control based on device tree properties */
	if (sensor->ext_leds_mask)
		sensor->led_ctrl =
			v4l2_ctrl_new_std_menu(hdl, ops,
					       V4L2_CID_FLASH_LED_MODE,
					       V4L2_FLASH_LED_MODE_FLASH, 0,
					       V4L2_FLASH_LED_MODE_NONE);

	if (hdl->error) {
		ret = hdl->error;
		goto free_ctrls;
	}

	v4l2_ctrl_cluster(2, &sensor->hflip_ctrl);
	v4l2_ctrl_auto_cluster(4, &sensor->ae_ctrl, V4L2_EXPOSURE_MANUAL, true);

	/* Optional controls coming from fwnode (e.g. rotation, orientation). */
	ret = v4l2_fwnode_device_parse(sensor->dev, &fwnode_props);
	if (ret)
		goto free_ctrls;

	ret = v4l2_ctrl_new_fwnode_properties(hdl, ops, &fwnode_props);
	if (ret)
		goto free_ctrls;

	sensor->sd.ctrl_handler = hdl;

	return 0;

free_ctrls:
	v4l2_ctrl_handler_free(hdl);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Pad ops
 */

/* Media bus code is dependent of :
 *      - 8bits or 10bits output
 *      - variant : Mono or RGB
 *      - H/V flips parameters in case of RGB
 */
static u32 vd56g3_get_mbus_code(struct vd56g3 *sensor, u32 code)
{
	unsigned int i_bpp;
	unsigned int j;

	for (i_bpp = 0; i_bpp < ARRAY_SIZE(vd56g3_mbus_codes); i_bpp++) {
		for (j = 0; j < ARRAY_SIZE(vd56g3_mbus_codes[i_bpp]); j++) {
			if (vd56g3_mbus_codes[i_bpp][j] == code)
				goto endloops;
		}
	}

endloops:
	if (i_bpp >= ARRAY_SIZE(vd56g3_mbus_codes))
		i_bpp = 0;

	if (sensor->is_mono)
		j = 0;
	else
		j = 1 + (sensor->hflip_ctrl->val ? 1 : 0) +
		    (sensor->vflip_ctrl->val ? 2 : 0);

	return vd56g3_mbus_codes[i_bpp][j];
}

static int vd56g3_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct vd56g3 *sensor = to_vd56g3(sd);

	if (code->index >= ARRAY_SIZE(vd56g3_mbus_codes))
		return -EINVAL;

	code->code =
		vd56g3_get_mbus_code(sensor, vd56g3_mbus_codes[code->index][0]);

	return 0;
}

static int vd56g3_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(vd56g3_supported_modes))
		return -EINVAL;

	fse->min_width = vd56g3_supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = vd56g3_supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static void vd56g3_update_img_pad_format(struct vd56g3 *sensor,
					 const struct vd56g3_mode *mode,
					 u32 mbus_code,
					 struct v4l2_mbus_framefmt *mbus_fmt)
{
	mbus_fmt->width = mode->width;
	mbus_fmt->height = mode->height;
	mbus_fmt->code = vd56g3_get_mbus_code(sensor, mbus_code);
	mbus_fmt->colorspace = V4L2_COLORSPACE_RAW;
	mbus_fmt->field = V4L2_FIELD_NONE;
	mbus_fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	mbus_fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	mbus_fmt->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int vd56g3_set_pad_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *sd_fmt)
{
	struct vd56g3 *sensor = to_vd56g3(sd);
	const struct vd56g3_mode *new_mode;
	struct v4l2_rect pad_crop;
	unsigned int binning;

	new_mode = v4l2_find_nearest_size(vd56g3_supported_modes,
					  ARRAY_SIZE(vd56g3_supported_modes),
					  width, height, sd_fmt->format.width,
					  sd_fmt->format.height);

	vd56g3_update_img_pad_format(sensor, new_mode, sd_fmt->format.code,
				     &sd_fmt->format);
	*v4l2_subdev_state_get_format(sd_state, sd_fmt->pad) = sd_fmt->format;

	/* Compute and update crop rectangle (maximized via binning) */
	binning = min(VD56G3_NATIVE_WIDTH / sd_fmt->format.width,
		      VD56G3_NATIVE_HEIGHT / sd_fmt->format.height);
	binning = min(binning, 2U);
	pad_crop.width = sd_fmt->format.width * binning;
	pad_crop.height = sd_fmt->format.height * binning;
	pad_crop.left = (VD56G3_NATIVE_WIDTH - pad_crop.width) / 2;
	pad_crop.top = (VD56G3_NATIVE_HEIGHT - pad_crop.height) / 2;
	*v4l2_subdev_state_get_crop(sd_state, sd_fmt->pad) = pad_crop;

	/* Update controls in case of active state */
	if (sd_fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		return vd56g3_update_controls(sensor);

	return 0;
}

static int vd56g3_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *v4l2_subdev_state_get_crop(sd_state, 0);
		break;
	case V4L2_SEL_TGT_NATIVE_SIZE:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = VD56G3_NATIVE_WIDTH;
		sel->r.height = VD56G3_NATIVE_HEIGHT;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int vd56g3_get_frame_desc(struct v4l2_subdev *sd, unsigned int pad,
				 struct v4l2_mbus_frame_desc *fd)
{
	struct v4l2_subdev_state *state;
	const struct v4l2_mbus_framefmt *format;

	state = v4l2_subdev_lock_and_get_active_state(sd);
	format = v4l2_subdev_state_get_format(state, pad);
	v4l2_subdev_unlock_state(state);

	fd->type = V4L2_MBUS_FRAME_DESC_TYPE_CSI2;
	fd->num_entries = 1;
	fd->entry[0].pixelcode = format->code;
	fd->entry[0].stream = 0;
	fd->entry[0].bus.csi2.vc = 0;
	fd->entry[0].bus.csi2.dt = vd56g3_get_datatype(format->code);

	return 0;
}

static int vd56g3_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct vd56g3 *sensor = to_vd56g3(sd);
	const struct v4l2_mbus_framefmt *format =
		v4l2_subdev_state_get_format(state, 0);
	const struct v4l2_rect *crop = v4l2_subdev_state_get_crop(state, 0);
	unsigned int csi_mbps = ((sensor->nb_of_lane == 2) ?
					 VD56G3_LINK_FREQ_DEF_2LANES :
					 VD56G3_LINK_FREQ_DEF_1LANE) *
				2 / MEGA;
	unsigned int binning;
	int ret;

	ret = pm_runtime_resume_and_get(sensor->dev);
	if (ret < 0)
		return ret;

	/* configure clocks */
	cci_write(sensor->regmap, VD56G3_REG_EXT_CLOCK, sensor->xclk_freq,
		  &ret);
	cci_write(sensor->regmap, VD56G3_REG_CLK_PLL_PREDIV, sensor->pll_prediv,
		  &ret);
	cci_write(sensor->regmap, VD56G3_REG_CLK_SYS_PLL_MULT, sensor->pll_mult,
		  &ret);

	/* configure output */
	cci_write(sensor->regmap, VD56G3_REG_FORMAT_CTRL,
		  vd56g3_get_bpp(format->code), &ret);
	cci_write(sensor->regmap, VD56G3_REG_OIF_CTRL, sensor->oif_ctrl, &ret);
	cci_write(sensor->regmap, VD56G3_REG_OIF_CSI_BITRATE, csi_mbps, &ret);
	cci_write(sensor->regmap, VD56G3_REG_OIF_IMG_CTRL,
		  vd56g3_get_datatype(format->code), &ret);
	cci_write(sensor->regmap, VD56G3_REG_ISL_ENABLE, 0, &ret);

	/* configure binning mode */
	switch (crop->width / format->width) {
	case 1:
	default:
		binning = READOUT_NORMAL;
		break;
	case 2:
		binning = READOUT_DIGITAL_BINNING_X2;
		break;
	}
	cci_write(sensor->regmap, VD56G3_REG_READOUT_CTRL, binning, &ret);

	/* configure ROIs */
	cci_write(sensor->regmap, VD56G3_REG_Y_START, crop->top, &ret);
	cci_write(sensor->regmap, VD56G3_REG_Y_END,
		  crop->top + crop->height - 1, &ret);
	cci_write(sensor->regmap, VD56G3_REG_OUT_ROI_X_START, crop->left, &ret);
	cci_write(sensor->regmap, VD56G3_REG_OUT_ROI_X_END,
		  crop->left + crop->width - 1, &ret);
	cci_write(sensor->regmap, VD56G3_REG_OUT_ROI_Y_START, 0, &ret);
	cci_write(sensor->regmap, VD56G3_REG_OUT_ROI_Y_END, crop->height - 1,
		  &ret);
	cci_write(sensor->regmap, VD56G3_REG_AE_ROI_START_H, crop->left, &ret);
	cci_write(sensor->regmap, VD56G3_REG_AE_ROI_END_H,
		  crop->left + crop->width - 1, &ret);
	cci_write(sensor->regmap, VD56G3_REG_AE_ROI_START_V, 0, &ret);
	cci_write(sensor->regmap, VD56G3_REG_AE_ROI_END_V, crop->height - 1,
		  &ret);
	if (ret)
		goto rpm_put;

	/* Setup default GPIO values; could be overridden by V4L2 ctrl setup */
	ret = vd56g3_write_gpiox(sensor, GENMASK(VD56G3_NB_GPIOS - 1, 0));
	if (ret)
		goto rpm_put;

	/* Apply settings from V4L2 ctrls */
	ret = __v4l2_ctrl_handler_setup(&sensor->ctrl_handler);
	if (ret)
		goto rpm_put;

	/* start streaming */
	cci_write(sensor->regmap, VD56G3_REG_STBY, VD56G3_CMD_START_STREAM,
		  &ret);
	vd56g3_poll_reg(sensor, VD56G3_REG_STBY, VD56G3_CMD_ACK, &ret);
	vd56g3_wait_state(sensor, VD56G3_SYSTEM_FSM_STREAMING, &ret);
	if (ret)
		goto rpm_put;

	/* some controls are locked during streaming */
	__v4l2_ctrl_grab(sensor->hflip_ctrl, true);
	__v4l2_ctrl_grab(sensor->vflip_ctrl, true);
	__v4l2_ctrl_grab(sensor->patgen_ctrl, true);

	return ret;

rpm_put:
	dev_err(sensor->dev, "Failed to start streaming\n");
	pm_runtime_put_sync(sensor->dev);

	return ret;
}

static int vd56g3_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct vd56g3 *sensor = to_vd56g3(sd);
	int ret;

	/* Retrieve Expo cluster to enable coldstart of AE */
	ret = vd56g3_read_expo_cluster(sensor, true);

	cci_write(sensor->regmap, VD56G3_REG_STREAMING, VD56G3_CMD_STOP_STREAM,
		  &ret);
	vd56g3_poll_reg(sensor, VD56G3_REG_STREAMING, VD56G3_CMD_ACK, &ret);
	vd56g3_wait_state(sensor, VD56G3_SYSTEM_FSM_SW_STBY, &ret);

	/* locked controls must be unlocked */
	__v4l2_ctrl_grab(sensor->hflip_ctrl, false);
	__v4l2_ctrl_grab(sensor->vflip_ctrl, false);
	__v4l2_ctrl_grab(sensor->patgen_ctrl, false);

	pm_runtime_put_autosuspend(sensor->dev);

	return ret;
}

static int vd56g3_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	unsigned int def_mode = VD56G3_DEFAULT_MODE;
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.pad = 0,
		.format = {
			.code = vd56g3_mbus_codes[0][0],
			.width = vd56g3_supported_modes[def_mode].width,
			.height = vd56g3_supported_modes[def_mode].height,
		},
	};

	return vd56g3_set_pad_fmt(sd, sd_state, &fmt);
}

static const struct v4l2_subdev_video_ops vd56g3_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops vd56g3_pad_ops = {
	.enum_mbus_code = vd56g3_enum_mbus_code,
	.enum_frame_size = vd56g3_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = vd56g3_set_pad_fmt,
	.get_selection = vd56g3_get_selection,
	.get_frame_desc = vd56g3_get_frame_desc,
	.enable_streams = vd56g3_enable_streams,
	.disable_streams = vd56g3_disable_streams,
};

static const struct v4l2_subdev_ops vd56g3_subdev_ops = {
	.video = &vd56g3_video_ops,
	.pad = &vd56g3_pad_ops,
};

static const struct media_entity_operations vd56g3_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops vd56g3_internal_ops = {
	.init_state = vd56g3_init_state,
};

/* -----------------------------------------------------------------------------
 * Power management
 */

static int vd56g3_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct vd56g3 *sensor = to_vd56g3(sd);
	int ret;

	/* power on */
	ret = regulator_bulk_enable(ARRAY_SIZE(sensor->supplies),
				    sensor->supplies);
	if (ret) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(sensor->xclk);
	if (ret) {
		dev_err(dev, "Failed to enable clock: %d\n", ret);
		goto disable_reg;
	}

	gpiod_set_value_cansleep(sensor->reset_gpio, 0);
	usleep_range(3500, 4000);
	ret = vd56g3_wait_state(sensor, VD56G3_SYSTEM_FSM_READY_TO_BOOT, NULL);
	if (ret) {
		dev_err(dev, "Sensor reset failed: %d\n", ret);
		goto disable_clock;
	}

	/* boot sensor */
	cci_write(sensor->regmap, VD56G3_REG_BOOT, VD56G3_CMD_BOOT, &ret);
	vd56g3_poll_reg(sensor, VD56G3_REG_BOOT, VD56G3_CMD_ACK, &ret);
	vd56g3_wait_state(sensor, VD56G3_SYSTEM_FSM_SW_STBY, &ret);
	if (ret) {
		dev_err(dev, "Sensor boot failed: %d\n", ret);
		goto disable_clock;
	}

	return 0;

disable_clock:
	gpiod_set_value_cansleep(sensor->reset_gpio, 1);
	clk_disable_unprepare(sensor->xclk);
disable_reg:
	regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);

	return ret;
}

static int vd56g3_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct vd56g3 *sensor = to_vd56g3(sd);

	gpiod_set_value_cansleep(sensor->reset_gpio, 1);
	clk_disable_unprepare(sensor->xclk);
	regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);

	return 0;
}

static const struct dev_pm_ops vd56g3_pm_ops = {
	SET_RUNTIME_PM_OPS(vd56g3_power_off, vd56g3_power_on, NULL)
};

/* -----------------------------------------------------------------------------
 * Probe and initialization
 */

static int vd56g3_check_csi_conf(struct vd56g3 *sensor,
				 struct fwnode_handle *endpoint)
{
	struct v4l2_fwnode_endpoint ep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	u32 phy_data_lanes[VD56G3_MAX_CSI_DATA_LANES] = { ~0, ~0 };
	u8 n_lanes;
	u64 frequency;
	int p, l;
	int ret = 0;

	ret = v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep);
	if (ret)
		return -EINVAL;

	/* Check lanes number */
	n_lanes = ep.bus.mipi_csi2.num_data_lanes;
	if (n_lanes != 1 && n_lanes != 2) {
		dev_err(sensor->dev, "Invalid data lane number: %d\n", n_lanes);
		ret = -EINVAL;
		goto done;
	}
	sensor->nb_of_lane = n_lanes;

	/* Clock lane must be first */
	if (ep.bus.mipi_csi2.clock_lane != 0) {
		dev_err(sensor->dev, "Clock lane must be mapped to lane 0\n");
		ret = -EINVAL;
		goto done;
	}

	/*
	 * Prepare Output Interface conf based on lane settings
	 * logical to physical lane conversion (+ pad remaining slots)
	 */
	for (l = 0; l < n_lanes; l++)
		phy_data_lanes[ep.bus.mipi_csi2.data_lanes[l] - 1] = l;
	for (p = 0; p < VD56G3_MAX_CSI_DATA_LANES; p++) {
		if (phy_data_lanes[p] != ~0)
			continue;
		phy_data_lanes[p] = l;
		l++;
	}
	sensor->oif_ctrl = n_lanes |
			   (ep.bus.mipi_csi2.lane_polarities[0] << 3) |
			   ((phy_data_lanes[0]) << 4) |
			   (ep.bus.mipi_csi2.lane_polarities[1] << 6) |
			   ((phy_data_lanes[1]) << 7) |
			   (ep.bus.mipi_csi2.lane_polarities[2] << 9);

	/* Check link frequency */
	if (!ep.nr_of_link_frequencies) {
		dev_err(sensor->dev, "link-frequency not found in DT\n");
		ret = -EINVAL;
		goto done;
	}
	frequency = (n_lanes == 2) ? VD56G3_LINK_FREQ_DEF_2LANES :
				     VD56G3_LINK_FREQ_DEF_1LANE;
	if (ep.nr_of_link_frequencies != 1 ||
	    ep.link_frequencies[0] != frequency) {
		dev_err(sensor->dev, "Link frequency not supported: %lld\n",
			ep.link_frequencies[0]);
		ret = -EINVAL;
		goto done;
	}

done:
	v4l2_fwnode_endpoint_free(&ep);

	return ret;
}

static int vd56g3_parse_dt_gpios_array(struct vd56g3 *sensor, char *prop_name,
				       u32 *array, unsigned int *nb)
{
	struct device *dev = sensor->dev;
	unsigned int i;
	int ret;

	if (!device_property_present(dev, prop_name)) {
		*nb = 0;
		return 0;
	}

	ret = device_property_count_u32(dev, prop_name);
	if (ret < 0) {
		dev_err(dev, "Failed to read %s count\n", prop_name);
		return ret;
	}

	*nb = ret;
	ret = device_property_read_u32_array(dev, prop_name, array, *nb);
	if (ret) {
		dev_err(dev, "Failed to read %s prop\n", prop_name);
		return ret;
	}

	for (i = 0; i < *nb; i++) {
		if (array[i] >= VD56G3_NB_GPIOS) {
			dev_err(dev, "Invalid GPIO: %d\n", array[i]);
			return -EINVAL;
		}
	}

	return 0;
}

static int vd56g3_parse_dt_gpios(struct vd56g3 *sensor)
{
	u32 led_gpios[VD56G3_NB_GPIOS];
	unsigned int nb_gpios_leds;
	unsigned int i;
	int ret;

	/* Initialize GPIOs to default */
	for (i = 0; i < VD56G3_NB_GPIOS; i++)
		sensor->gpios[i] = VD56G3_GPIOX_GPIO_IN;
	sensor->ext_leds_mask = 0;

	/* Take into account optional 'st,leds' output for GPIOs */
	ret = vd56g3_parse_dt_gpios_array(sensor, "st,leds", led_gpios,
					  &nb_gpios_leds);
	if (ret)
		return ret;
	for (i = 0; i < nb_gpios_leds; i++) {
		sensor->gpios[led_gpios[i]] = VD56G3_GPIOX_STROBE_MODE;
		set_bit(led_gpios[i], &sensor->ext_leds_mask);
	}

	return 0;
}

static int vd56g3_parse_dt(struct vd56g3 *sensor)
{
	struct fwnode_handle *endpoint;
	int ret;

	endpoint = fwnode_graph_get_endpoint_by_id(dev_fwnode(sensor->dev), 0,
						   0, 0);
	if (!endpoint) {
		dev_err(sensor->dev, "Endpoint node not found\n");
		return -EINVAL;
	}

	ret = vd56g3_check_csi_conf(sensor, endpoint);
	fwnode_handle_put(endpoint);
	if (ret)
		return ret;

	return vd56g3_parse_dt_gpios(sensor);
}

static int vd56g3_get_regulators(struct vd56g3 *sensor)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sensor->supplies); i++)
		sensor->supplies[i].supply = vd56g3_supply_names[i];

	return devm_regulator_bulk_get(sensor->dev,
				       ARRAY_SIZE(sensor->supplies),
				       sensor->supplies);
}

static int vd56g3_prepare_clock_tree(struct vd56g3 *sensor)
{
	const unsigned int predivs[] = { 1, 2, 4 };
	u32 pll_out;
	int i;

	/* External clock must be in [6Mhz-27Mhz] */
	if (sensor->xclk_freq < VD56G3_XCLK_FREQ_MIN ||
	    sensor->xclk_freq > VD56G3_XCLK_FREQ_MAX) {
		dev_err(sensor->dev,
			"Only 6Mhz-27Mhz clock range supported. Provided %lu MHz\n",
			sensor->xclk_freq / HZ_PER_MHZ);
		return -EINVAL;
	}

	/* PLL input should be in [6Mhz-12Mhz[ */
	for (i = 0; i < ARRAY_SIZE(predivs); i++) {
		sensor->pll_prediv = predivs[i];
		if (sensor->xclk_freq / sensor->pll_prediv < 12 * HZ_PER_MHZ)
			break;
	}

	/* PLL output clock must be as close as possible to 804Mhz */
	sensor->pll_mult = (VD56G3_TARGET_PLL * sensor->pll_prediv +
			    sensor->xclk_freq / 2) /
			   sensor->xclk_freq;
	pll_out = sensor->xclk_freq * sensor->pll_mult / sensor->pll_prediv;

	/* Target Pixel Clock for standard 10bit ADC mode : 160.8Mhz */
	sensor->pixel_clock = pll_out / VD56G3_VT_CLOCK_DIV;

	return 0;
}

static int vd56g3_detect(struct vd56g3 *sensor)
{
	struct device *dev = sensor->dev;
	unsigned int model;
	u64 model_id;
	u64 device_revision;
	u64 optical_revision;
	int ret = 0;

	model = (uintptr_t)device_get_match_data(dev);

	ret = cci_read(sensor->regmap, VD56G3_REG_MODEL_ID, &model_id, NULL);
	if (ret)
		return ret;

	if (model_id != VD56G3_MODEL_ID) {
		dev_err(dev, "Unsupported sensor id: %x\n", (u16)model_id);
		return -ENODEV;
	}

	ret = cci_read(sensor->regmap, VD56G3_REG_REVISION, &device_revision,
		       NULL);
	if (ret)
		return ret;

	if ((device_revision >> 8) != VD56G3_REVISION_CUT3) {
		dev_err(dev, "Unsupported version: %x\n", (u16)device_revision);
		return -ENODEV;
	}

	ret = cci_read(sensor->regmap, VD56G3_REG_OPTICAL_REVISION,
		       &optical_revision, NULL);
	if (ret)
		return ret;

	sensor->is_mono =
		((optical_revision & 1) == VD56G3_OPTICAL_REVISION_MONO);
	if ((sensor->is_mono && model == VD56G3_MODEL_VD66GY) ||
	    (!sensor->is_mono && model == VD56G3_MODEL_VD56G3)) {
		dev_err(dev, "Found %s sensor, while %s model is defined in DT\n",
			(sensor->is_mono) ? "Mono" : "Bayer",
			(model == VD56G3_MODEL_VD56G3) ? "vd56g3" : "vd66gy");
		return -ENODEV;
	}

	return 0;
}

static int vd56g3_subdev_init(struct vd56g3 *sensor)
{
	struct v4l2_subdev_state *state;
	int ret;

	/* Init remaining sub device ops */
	sensor->sd.internal_ops = &vd56g3_internal_ops;
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sensor->sd.entity.ops = &vd56g3_subdev_entity_ops;

	/* Init source pad */
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret) {
		dev_err(sensor->dev, "Failed to init media entity: %d\n", ret);
		return ret;
	}

	/* Init controls */
	ret = vd56g3_init_controls(sensor);
	if (ret) {
		dev_err(sensor->dev, "Controls initialization failed: %d\n",
			ret);
		goto err_media;
	}

	/* Init vd56g3 struct : default resolution + raw8 */
	sensor->sd.state_lock = sensor->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret) {
		dev_err(sensor->dev, "Subdev init failed: %d\n", ret);
		goto err_ctrls;
	}

	/* Update controls according to the resolution set */
	state = v4l2_subdev_lock_and_get_active_state(&sensor->sd);
	ret = vd56g3_update_controls(sensor);
	v4l2_subdev_unlock_state(state);
	if (ret) {
		dev_err(sensor->dev, "Controls update failed: %d\n", ret);
		goto err_ctrls;
	}

	return 0;

err_ctrls:
	v4l2_ctrl_handler_free(sensor->sd.ctrl_handler);

err_media:
	media_entity_cleanup(&sensor->sd.entity);

	return ret;
}

static void vd56g3_subdev_cleanup(struct vd56g3 *sensor)
{
	v4l2_async_unregister_subdev(&sensor->sd);
	v4l2_subdev_cleanup(&sensor->sd);
	media_entity_cleanup(&sensor->sd.entity);
	v4l2_ctrl_handler_free(sensor->sd.ctrl_handler);
}

static int vd56g3_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct vd56g3 *sensor;
	int ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&sensor->sd, client, &vd56g3_subdev_ops);
	sensor->dev = dev;

	ret = vd56g3_parse_dt(sensor);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to parse Device Tree\n");

	/* Get (and check) resources : power regs, ext clock, reset gpio */
	ret = vd56g3_get_regulators(sensor);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	sensor->xclk = devm_v4l2_sensor_clk_get(dev, NULL);
	if (IS_ERR(sensor->xclk))
		return dev_err_probe(dev, PTR_ERR(sensor->xclk),
				     "Failed to get xclk\n");
	sensor->xclk_freq = clk_get_rate(sensor->xclk);
	ret = vd56g3_prepare_clock_tree(sensor);
	if (ret)
		return ret;

	sensor->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(sensor->reset_gpio),
				     "Failed to get reset gpio\n");

	sensor->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(sensor->regmap))
		return dev_err_probe(dev, PTR_ERR(sensor->regmap),
				     "Failed to init regmap\n");

	/* Power ON */
	ret = vd56g3_power_on(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Sensor power on failed\n");

	/* Enable PM runtime with autosuspend (sensor being ON, set active) */
	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);

	/* Check HW model/version */
	ret = vd56g3_detect(sensor);
	if (ret) {
		dev_err(dev, "Sensor detect failed: %d\n", ret);
		goto err_power_off;
	}

	/* Initialize & register subdev (v4l2_i2c subdev already initialized) */
	ret = vd56g3_subdev_init(sensor);
	if (ret) {
		dev_err(dev, "V4l2 init failed: %d\n", ret);
		goto err_power_off;
	}

	ret = v4l2_async_register_subdev(&sensor->sd);
	if (ret) {
		dev_err(dev, "Async subdev register failed: %d\n", ret);
		goto err_subdev;
	}

	/* Sensor could now be powered off (after the autosuspend delay) */
	pm_runtime_put_autosuspend(dev);

	dev_dbg(dev, "Successfully probe %s sensor\n",
		(sensor->is_mono) ? "vd56g3" : "vd66gy");

	return 0;

err_subdev:
	vd56g3_subdev_cleanup(sensor);
err_power_off:
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_dont_use_autosuspend(dev);
	vd56g3_power_off(dev);

	return ret;
}

static void vd56g3_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct vd56g3 *sensor = to_vd56g3(sd);

	vd56g3_subdev_cleanup(sensor);

	pm_runtime_disable(sensor->dev);
	if (!pm_runtime_status_suspended(sensor->dev))
		vd56g3_power_off(sensor->dev);
	pm_runtime_set_suspended(sensor->dev);
	pm_runtime_dont_use_autosuspend(sensor->dev);
}

static const struct of_device_id vd56g3_dt_ids[] = {
	{ .compatible = "st,vd56g3", .data = (void *)VD56G3_MODEL_VD56G3 },
	{ .compatible = "st,vd66gy", .data = (void *)VD56G3_MODEL_VD66GY },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, vd56g3_dt_ids);

static struct i2c_driver vd56g3_i2c_driver = {
	.driver = {
		.name  = "vd56g3",
		.of_match_table = vd56g3_dt_ids,
		.pm = &vd56g3_pm_ops,
	},
	.probe = vd56g3_probe,
	.remove = vd56g3_remove,
};

module_i2c_driver(vd56g3_i2c_driver);

MODULE_AUTHOR("Benjamin Mugnier <benjamin.mugnier@foss.st.com>");
MODULE_AUTHOR("Mickael Guene <mickael.guene@st.com>");
MODULE_AUTHOR("Sylvain Petinot <sylvain.petinot@foss.st.com>");
MODULE_DESCRIPTION("ST VD56G3 sensor driver");
MODULE_LICENSE("GPL");
