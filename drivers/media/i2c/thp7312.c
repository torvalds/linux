// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 THine Electronics, Inc.
 * Copyright (C) 2023 Ideas on Board Oy
 */

#include <asm/unaligned.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/spi-nor.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <media/v4l2-async.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include <uapi/linux/thp7312.h>

/* ISP registers */

#define THP7312_REG_FIRMWARE_VERSION_1			CCI_REG8(0xf000)
#define THP7312_REG_CAMERA_STATUS			CCI_REG8(0xf001)
#define THP7312_REG_FIRMWARE_VERSION_2			CCI_REG8(0xf005)
#define THP7312_REG_SET_OUTPUT_ENABLE			CCI_REG8(0xf008)
#define THP7312_OUTPUT_ENABLE				0x01
#define THP7312_OUTPUT_DISABLE				0x00
#define THP7312_REG_SET_OUTPUT_COLOR_COMPRESSION	CCI_REG8(0xf009)
#define THP7312_REG_SET_OUTPUT_COLOR_UYVY		0x00
#define THP7312_REG_SET_OUTPUT_COLOR_YUY2		0x04
#define THP7312_REG_FLIP_MIRROR				CCI_REG8(0xf00c)
#define THP7312_REG_FLIP_MIRROR_FLIP			BIT(0)
#define THP7312_REG_FLIP_MIRROR_MIRROR			BIT(1)
#define THP7312_REG_VIDEO_IMAGE_SIZE			CCI_REG8(0xf00d)
#define THP7312_VIDEO_IMAGE_SIZE_640x360		0x52
#define THP7312_VIDEO_IMAGE_SIZE_640x460		0x03
#define THP7312_VIDEO_IMAGE_SIZE_1280x720		0x0a
#define THP7312_VIDEO_IMAGE_SIZE_1920x1080		0x0b
#define THP7312_VIDEO_IMAGE_SIZE_3840x2160		0x0d
#define THP7312_VIDEO_IMAGE_SIZE_4160x3120		0x14
#define THP7312_VIDEO_IMAGE_SIZE_2016x1512		0x20
#define THP7312_VIDEO_IMAGE_SIZE_2048x1536		0x21
#define THP7312_REG_VIDEO_FRAME_RATE_MODE		CCI_REG8(0xf00f)
#define THP7312_VIDEO_FRAME_RATE_MODE1			0x80
#define THP7312_VIDEO_FRAME_RATE_MODE2			0x81
#define THP7312_VIDEO_FRAME_RATE_MODE3			0x82
#define THP7312_REG_SET_DRIVING_MODE			CCI_REG8(0xf010)
#define THP7312_REG_DRIVING_MODE_STATUS			CCI_REG8(0xf011)
#define THP7312_REG_JPEG_COMPRESSION_FACTOR		CCI_REG8(0xf01b)
#define THP7312_REG_AE_EXPOSURE_COMPENSATION		CCI_REG8(0xf022)
#define THP7312_REG_AE_FLICKER_MODE			CCI_REG8(0xf023)
#define THP7312_AE_FLICKER_MODE_50			0x00
#define THP7312_AE_FLICKER_MODE_60			0x01
#define THP7312_AE_FLICKER_MODE_DISABLE			0x80
#define THP7312_REG_AE_FIX_FRAME_RATE			CCI_REG8(0xf02e)
#define THP7312_REG_MANUAL_WB_RED_GAIN			CCI_REG8(0xf036)
#define THP7312_REG_MANUAL_WB_BLUE_GAIN			CCI_REG8(0xf037)
#define THP7312_REG_WB_MODE				CCI_REG8(0xf039)
#define THP7312_WB_MODE_AUTO				0x00
#define THP7312_WB_MODE_MANUAL				0x11
#define THP7312_REG_MANUAL_FOCUS_POSITION		CCI_REG16(0xf03c)
#define THP7312_REG_AF_CONTROL				CCI_REG8(0xf040)
#define THP7312_REG_AF_CONTROL_AF			0x01
#define THP7312_REG_AF_CONTROL_MANUAL			0x10
#define THP7312_REG_AF_CONTROL_LOCK			0x80
#define THP7312_REG_AF_SETTING				CCI_REG8(0xf041)
#define THP7312_REG_AF_SETTING_ONESHOT_CONTRAST		0x00
#define THP7312_REG_AF_SETTING_ONESHOT_PDAF		0x40
#define THP7312_REG_AF_SETTING_ONESHOT_HYBRID		0x80
#define THP7312_REG_AF_SETTING_CONTINUOUS_CONTRAST	0x30
#define THP7312_REG_AF_SETTING_CONTINUOUS_PDAF		0x70
#define THP7312_REG_AF_SETTING_CONTINUOUS_HYBRID	0xf0
#define THP7312_REG_AF_SUPPORT				CCI_REG8(0xf043)
#define THP7312_AF_SUPPORT_PDAF				BIT(1)
#define THP7312_AF_SUPPORT_CONTRAST			BIT(0)
#define THP7312_REG_SATURATION				CCI_REG8(0xf052)
#define THP7312_REG_SHARPNESS				CCI_REG8(0xf053)
#define THP7312_REG_BRIGHTNESS				CCI_REG8(0xf056)
#define THP7312_REG_CONTRAST				CCI_REG8(0xf057)
#define THP7312_REG_NOISE_REDUCTION			CCI_REG8(0xf059)
#define THP7312_REG_NOISE_REDUCTION_FIXED		BIT(7)

#define TH7312_REG_CUSTOM_MIPI_SET			CCI_REG8(0xf0f6)
#define TH7312_REG_CUSTOM_MIPI_STATUS			CCI_REG8(0xf0f7)
#define TH7312_REG_CUSTOM_MIPI_RD			CCI_REG8(0xf0f8)
#define TH7312_REG_CUSTOM_MIPI_TD			CCI_REG8(0xf0f9)

/*
 * Firmware update registers. Those use a different address space than the
 * normal operation ISP registers.
 */

#define THP7312_REG_FW_DRIVABILITY			CCI_REG32(0xd65c)
#define THP7312_REG_FW_DEST_BANK_ADDR			CCI_REG32(0xff08)
#define THP7312_REG_FW_VERIFY_RESULT			CCI_REG8(0xff60)
#define THP7312_REG_FW_RESET_FLASH			CCI_REG8(0xff61)
#define THP7312_REG_FW_MEMORY_IO_SETTING		CCI_REG8(0xff62)
#define THP7312_FW_MEMORY_IO_GPIO0			1
#define THP7312_FW_MEMORY_IO_GPIO1			0
#define THP7312_REG_FW_CRC_RESULT			CCI_REG32(0xff64)
#define THP7312_REG_FW_STATUS				CCI_REG8(0xfffc)

#define THP7312_FW_VERSION(major, minor)		(((major) << 8) | (minor))
#define THP7312_FW_VERSION_MAJOR(v)			((v) >> 8)
#define THP7312_FW_VERSION_MINOR(v)			((v) & 0xff)

enum thp7312_focus_method {
	THP7312_FOCUS_METHOD_CONTRAST,
	THP7312_FOCUS_METHOD_PDAF,
	THP7312_FOCUS_METHOD_HYBRID,
};

/*
 * enum thp7312_focus_state - State of the focus handler
 *
 * @THP7312_FOCUS_STATE_MANUAL: Manual focus, controlled through the
 *	V4L2_CID_FOCUS_ABSOLUTE control
 * @THP7312_FOCUS_STATE_AUTO: Continuous auto-focus
 * @THP7312_FOCUS_STATE_LOCKED: Lock the focus to a fixed position. This state
 *	is entered when switching from auto to manual mode.
 * @THP7312_FOCUS_STATE_ONESHOT: One-shot auto-focus
 *
 * Valid transitions are as follow:
 *
 * digraph fsm {
 *         node [shape=circle];
 *
 *         manual [label="MANUAL"];
 *         auto [label="AUTO"];
 *         locked [label="LOCKED"];
 *         oneshot [label="ONESHOT"];
 *
 *         manual -> auto [label="FOCUS_AUTO <- true"]
 *         locked -> auto [label="FOCUS_AUTO <- true"]
 *         oneshot -> auto [label="FOCUS_AUTO <- true"]
 *         auto -> locked [label="FOCUS_AUTO <- false"]
 *
 *         locked -> manual [label="FOCUS_ABSOLUTE <- *"]
 *         oneshot -> manual [label="FOCUS_ABSOLUTE <- *"]
 *
 *         manual -> oneshot [label="FOCUS_START <- *"]
 *         locked -> oneshot [label="FOCUS_START <- *"]
 * }
 */
enum thp7312_focus_state {
	THP7312_FOCUS_STATE_MANUAL,
	THP7312_FOCUS_STATE_AUTO,
	THP7312_FOCUS_STATE_LOCKED,
	THP7312_FOCUS_STATE_ONESHOT,
};

enum thp7312_boot_mode {
	THP7312_BOOT_MODE_2WIRE_SLAVE = 0,
	THP7312_BOOT_MODE_SPI_MASTER = 1,
};

struct thp7312_frame_rate {
	u32 fps;
	u32 link_freq;
	u8 reg_frame_rate_mode;
};

struct thp7312_mode_info {
	u32 width;
	u32 height;
	u8 reg_image_size;
	const struct thp7312_frame_rate *rates;
};

static const u32 thp7312_colour_fmts[] = {
	MEDIA_BUS_FMT_YUYV8_1X16,
};

/* regulator supplies */
static const char * const thp7312_supply_name[] = {
	"vddcore",
	"vhtermrx",
	"vddtx",
	"vddhost",
	"vddcmos",
	"vddgpio-0",
	"vddgpio-1",
};

static const struct thp7312_mode_info thp7312_mode_info_data[] = {
	{
		.width = 1920,
		.height = 1080,
		.reg_image_size = THP7312_VIDEO_IMAGE_SIZE_1920x1080,
		.rates = (const struct thp7312_frame_rate[]) {
			{ 30, 300000000, 0x81 },
			{ 60, 387500000, 0x82 },
			{ 0 }
		},
	}, {
		.width = 2048,
		.height = 1536,
		.reg_image_size = THP7312_VIDEO_IMAGE_SIZE_2048x1536,
		.rates = (const struct thp7312_frame_rate[]) {
			{ 30, 300000000, 0x81 },
			{ 0 }
		}
	}, {
		.width = 3840,
		.height = 2160,
		.reg_image_size = THP7312_VIDEO_IMAGE_SIZE_3840x2160,
		.rates = (const struct thp7312_frame_rate[]) {
			{ 30, 600000000, 0x81 },
			{ 0 }
		},
	}, {
		.width = 4160,
		.height = 3120,
		.reg_image_size = THP7312_VIDEO_IMAGE_SIZE_4160x3120,
		.rates = (const struct thp7312_frame_rate[]) {
			{ 20, 600000000, 0x81 },
			{ 0 }
		},
	},
};

struct thp7312_device;

struct thp7312_sensor_info {
	const char *model;
};

struct thp7312_sensor {
	const struct thp7312_sensor_info *info;
	u8 lane_remap;
};

struct thp7312_device {
	struct device *dev;
	struct regmap *regmap;

	struct v4l2_subdev sd;
	struct media_pad pad;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[ARRAY_SIZE(thp7312_supply_name)];
	struct clk *iclk;

	u8 lane_remap;

	struct thp7312_sensor sensors[1];

	enum thp7312_boot_mode boot_mode;

	struct v4l2_ctrl_handler ctrl_handler;
	bool ctrls_applied;

	s64 link_freq;

	struct {
		struct v4l2_ctrl *hflip;
		struct v4l2_ctrl *vflip;
	};

	struct {
		struct v4l2_ctrl *focus_auto;
		struct v4l2_ctrl *focus_absolute;
		struct v4l2_ctrl *focus_start;
		struct v4l2_ctrl *focus_method;
	};

	enum thp7312_focus_state focus_state;

	struct {
		struct v4l2_ctrl *noise_reduction_auto;
		struct v4l2_ctrl *noise_reduction_absolute;
	};

	/* Lock to protect fw_cancel */
	struct mutex fw_lock;
	struct fw_upload *fwl;
	u8 *fw_write_buf;
	bool fw_cancel;

	u16 fw_version;
};

static const struct thp7312_sensor_info thp7312_sensor_info[] = {
	{
		.model = "sony,imx258",
	},
};

static inline struct thp7312_device *to_thp7312_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct thp7312_device, sd);
}

static const struct thp7312_mode_info *
thp7312_find_mode(unsigned int width, unsigned int height, bool nearest)
{
	const struct thp7312_mode_info *mode;

	mode = v4l2_find_nearest_size(thp7312_mode_info_data,
				      ARRAY_SIZE(thp7312_mode_info_data),
				      width, height, width, height);

	if (!nearest && (mode->width != width || mode->height != height))
		return NULL;

	return mode;
}

static const struct thp7312_frame_rate *
thp7312_find_rate(const struct thp7312_mode_info *mode, unsigned int fps,
		  bool nearest)
{
	const struct thp7312_frame_rate *best_rate = NULL;
	const struct thp7312_frame_rate *rate;
	unsigned int best_delta = UINT_MAX;

	if (!mode)
		return NULL;

	for (rate = mode->rates; rate->fps && best_delta; ++rate) {
		unsigned int delta = abs(rate->fps - fps);

		if (delta <= best_delta) {
			best_delta = delta;
			best_rate = rate;
		}
	}

	if (!nearest && best_delta)
		return NULL;

	return best_rate;
}

/* -----------------------------------------------------------------------------
 * Device Access & Configuration
 */

#define thp7312_read_poll_timeout(dev, addr, val, cond, sleep_us, timeout_us) \
({ \
	int __ret, __err; \
	__ret = read_poll_timeout(cci_read, __err, __err || (cond), sleep_us, \
				  timeout_us, false, (dev)->regmap, addr, \
				  &(val), NULL); \
	__ret ? : __err; \
})

static int thp7312_map_data_lanes(u8 *lane_remap, const u8 *lanes, u8 num_lanes)
{
	u8 used_lanes = 0;
	u8 val = 0;
	unsigned int i;

	/*
	 * The value that we write to the register is the index in the
	 * data-lanes array, so we need to do a conversion. Do this in the same
	 * pass as validating data-lanes.
	 */
	for (i = 0; i < num_lanes; i++) {
		if (lanes[i] < 1 || lanes[i] > 4)
			return -EINVAL;

		if (used_lanes & (BIT(lanes[i])))
			return -EINVAL;

		used_lanes |= BIT(lanes[i]);

		/*
		 * data-lanes is 1-indexed while the field position in the
		 * register is 0-indexed.
		 */
		val |= i << ((lanes[i] - 1) * 2);
	}

	*lane_remap = val;

	return 0;
}

static int thp7312_set_mipi_lanes(struct thp7312_device *thp7312)
{
	struct device *dev = thp7312->dev;
	int ret = 0;
	u64 val;

	cci_write(thp7312->regmap, TH7312_REG_CUSTOM_MIPI_RD,
		  thp7312->sensors[0].lane_remap, &ret);
	cci_write(thp7312->regmap, TH7312_REG_CUSTOM_MIPI_TD,
		  thp7312->lane_remap, &ret);
	cci_write(thp7312->regmap, TH7312_REG_CUSTOM_MIPI_SET, 1, &ret);

	if (ret)
		return ret;

	ret = thp7312_read_poll_timeout(thp7312, TH7312_REG_CUSTOM_MIPI_STATUS,
					val, val == 0x00, 100000, 2000000);
	if (ret) {
		dev_err(dev, "Failed to poll MIPI lane status: %d\n", ret);
		return ret;
	}

	return 0;
}

static int thp7312_change_mode(struct thp7312_device *thp7312,
			       const struct thp7312_mode_info *mode,
			       const struct thp7312_frame_rate *rate)
{
	struct device *dev = thp7312->dev;
	u64 val = 0;
	int ret;

	ret = thp7312_read_poll_timeout(thp7312, THP7312_REG_CAMERA_STATUS, val,
					val == 0x80, 20000, 200000);
	if (ret < 0) {
		dev_err(dev, "%s(): failed to poll ISP: %d\n", __func__, ret);
		return ret;
	}

	cci_write(thp7312->regmap, THP7312_REG_VIDEO_IMAGE_SIZE,
		  mode->reg_image_size, &ret);
	cci_write(thp7312->regmap, THP7312_REG_VIDEO_FRAME_RATE_MODE,
		  rate->reg_frame_rate_mode, &ret);
	cci_write(thp7312->regmap, THP7312_REG_JPEG_COMPRESSION_FACTOR, 0x5e,
		  &ret);
	cci_write(thp7312->regmap, THP7312_REG_SET_DRIVING_MODE, 0x01, &ret);

	if (ret)
		return ret;

	ret = thp7312_read_poll_timeout(thp7312, THP7312_REG_DRIVING_MODE_STATUS,
					val, val == 0x01, 20000, 100000);
	if (ret < 0) {
		dev_err(dev, "%s(): failed\n", __func__);
		return ret;
	}

	return 0;
}

static int thp7312_set_framefmt(struct thp7312_device *thp7312,
				struct v4l2_mbus_framefmt *format)
{
	u8 val;

	switch (format->code) {
	case MEDIA_BUS_FMT_UYVY8_1X16:
		/* YUV422, UYVY */
		val = THP7312_REG_SET_OUTPUT_COLOR_UYVY;
		break;
	case MEDIA_BUS_FMT_YUYV8_1X16:
		/* YUV422, YUYV */
		val = THP7312_REG_SET_OUTPUT_COLOR_YUY2;
		break;
	default:
		/* Should never happen */
		return -EINVAL;
	}

	return cci_write(thp7312->regmap,
			 THP7312_REG_SET_OUTPUT_COLOR_COMPRESSION, val, NULL);
}

static int thp7312_init_mode(struct thp7312_device *thp7312,
			     struct v4l2_subdev_state *sd_state)
{
	const struct thp7312_mode_info *mode;
	const struct thp7312_frame_rate *rate;
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_fract *interval;
	int ret;

	/*
	 * TODO: The mode and rate should be cached in the subdev state, once
	 * support for extending states will be available.
	 */
	fmt = v4l2_subdev_state_get_format(sd_state, 0);
	interval = v4l2_subdev_state_get_interval(sd_state, 0);

	mode = thp7312_find_mode(fmt->width, fmt->height, false);
	rate = thp7312_find_rate(mode, interval->denominator, false);

	if (WARN_ON(!mode || !rate))
		return -EINVAL;

	ret = thp7312_set_framefmt(thp7312, fmt);
	if (ret)
		return ret;

	return thp7312_change_mode(thp7312, mode, rate);
}

static int thp7312_stream_enable(struct thp7312_device *thp7312, bool enable)
{
	return cci_write(thp7312->regmap, THP7312_REG_SET_OUTPUT_ENABLE,
			 enable ? THP7312_OUTPUT_ENABLE : THP7312_OUTPUT_DISABLE,
			 NULL);
}

static int thp7312_check_status_stream_mode(struct thp7312_device *thp7312)
{
	struct device *dev = thp7312->dev;
	u64 status = 0;
	int ret;

	while (status != 0x80) {
		ret = cci_read(thp7312->regmap, THP7312_REG_CAMERA_STATUS,
			       &status, NULL);
		if (ret)
			return ret;

		if (status == 0x80) {
			dev_dbg(dev, "Camera initialization done\n");
			return 0;
		}

		if (status != 0x00) {
			dev_err(dev, "Invalid camera status %llx\n", status);
			return -EINVAL;
		}

		dev_dbg(dev, "Camera initializing...\n");
		usleep_range(70000, 80000);
	}

	return 0;
}

static void thp7312_reset(struct thp7312_device *thp7312)
{
	unsigned long rate;

	gpiod_set_value_cansleep(thp7312->reset_gpio, 1);

	/*
	 * The minimum reset duration is 8 clock cycles, make it 10 to provide
	 * a safety margin.
	 */
	rate = clk_get_rate(thp7312->iclk);
	fsleep(DIV_ROUND_UP(10 * USEC_PER_SEC, rate));

	gpiod_set_value_cansleep(thp7312->reset_gpio, 0);

	/*
	 * TODO: The documentation states that the device needs 2ms to
	 * initialize after reset is deasserted. It then proceeds to load the
	 * firmware from the flash memory, which takes an unspecified amount of
	 * time. Check if this delay could be reduced.
	 */
	fsleep(300000);
}

/* -----------------------------------------------------------------------------
 * Power Management
 */

static void __thp7312_power_off(struct thp7312_device *thp7312)
{
	regulator_bulk_disable(ARRAY_SIZE(thp7312->supplies), thp7312->supplies);
	clk_disable_unprepare(thp7312->iclk);
}

static void thp7312_power_off(struct thp7312_device *thp7312)
{
	__thp7312_power_off(thp7312);
}

static int __thp7312_power_on(struct thp7312_device *thp7312)
{
	struct device *dev = thp7312->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(thp7312->supplies),
				    thp7312->supplies);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(thp7312->iclk);
	if (ret < 0) {
		dev_err(dev, "clk prepare enable failed\n");
		regulator_bulk_disable(ARRAY_SIZE(thp7312->supplies),
				       thp7312->supplies);
		return ret;
	}

	/*
	 * We cannot assume that turning off and on again will reset, so do a
	 * software reset on power up.
	 */
	thp7312_reset(thp7312);

	return 0;
}

static int thp7312_power_on(struct thp7312_device *thp7312)
{
	int ret;

	ret = __thp7312_power_on(thp7312);
	if (ret < 0)
		return ret;

	ret = thp7312_check_status_stream_mode(thp7312);
	if (ret < 0)
		goto error;

	ret = thp7312_set_mipi_lanes(thp7312);
	if (ret)
		goto error;

	return 0;

error:
	thp7312_power_off(thp7312);
	return ret;
}

static int __maybe_unused thp7312_pm_runtime_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct thp7312_device *thp7312 = to_thp7312_dev(sd);

	thp7312_power_off(thp7312);

	thp7312->ctrls_applied = false;

	return 0;
}

static int __maybe_unused thp7312_pm_runtime_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct thp7312_device *thp7312 = to_thp7312_dev(sd);

	return thp7312_power_on(thp7312);
}

static const struct dev_pm_ops thp7312_pm_ops = {
	SET_RUNTIME_PM_OPS(thp7312_pm_runtime_suspend,
			   thp7312_pm_runtime_resume, NULL)
};

/* -----------------------------------------------------------------------------
 * V4L2 Subdev Operations
 */

static bool thp7312_find_bus_code(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(thp7312_colour_fmts); ++i) {
		if (thp7312_colour_fmts[i] == code)
			return true;
	}

	return false;
}

static int thp7312_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(thp7312_colour_fmts))
		return -EINVAL;

	code->code = thp7312_colour_fmts[code->index];

	return 0;
}

static int thp7312_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (!thp7312_find_bus_code(fse->code))
		return -EINVAL;

	if (fse->index >= ARRAY_SIZE(thp7312_mode_info_data))
		return -EINVAL;

	fse->min_width = thp7312_mode_info_data[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = thp7312_mode_info_data[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int thp7312_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *sd_state,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	const struct thp7312_frame_rate *rate;
	const struct thp7312_mode_info *mode;
	unsigned int index = fie->index;

	if (!thp7312_find_bus_code(fie->code))
		return -EINVAL;

	mode = thp7312_find_mode(fie->width, fie->height, false);
	if (!mode)
		return -EINVAL;

	for (rate = mode->rates; rate->fps; ++rate, --index) {
		if (!index) {
			fie->interval.numerator = 1;
			fie->interval.denominator = rate->fps;

			return 0;
		}
	}

	return -EINVAL;
}

static int thp7312_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *sd_state,
			   struct v4l2_subdev_format *format)
{
	struct thp7312_device *thp7312 = to_thp7312_dev(sd);
	struct v4l2_mbus_framefmt *mbus_fmt = &format->format;
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_fract *interval;
	const struct thp7312_mode_info *mode;

	if (!thp7312_find_bus_code(mbus_fmt->code))
		mbus_fmt->code = thp7312_colour_fmts[0];

	mode = thp7312_find_mode(mbus_fmt->width, mbus_fmt->height, true);

	fmt = v4l2_subdev_state_get_format(sd_state, 0);

	fmt->code = mbus_fmt->code;
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);

	*mbus_fmt = *fmt;

	interval = v4l2_subdev_state_get_interval(sd_state, 0);
	interval->numerator = 1;
	interval->denominator = mode->rates[0].fps;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		thp7312->link_freq = mode->rates[0].link_freq;

	return 0;
}

static int thp7312_set_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *sd_state,
				      struct v4l2_subdev_frame_interval *fi)
{
	struct thp7312_device *thp7312 = to_thp7312_dev(sd);
	const struct thp7312_mode_info *mode;
	const struct thp7312_frame_rate *rate;
	const struct v4l2_mbus_framefmt *fmt;
	struct v4l2_fract *interval;
	unsigned int fps;

	/* Avoid divisions by 0, pick the highest frame if the interval is 0. */
	fps = fi->interval.numerator
	    ? DIV_ROUND_CLOSEST(fi->interval.denominator, fi->interval.numerator)
	    : UINT_MAX;

	fmt = v4l2_subdev_state_get_format(sd_state, 0);
	mode = thp7312_find_mode(fmt->width, fmt->height, false);
	rate = thp7312_find_rate(mode, fps, true);

	interval = v4l2_subdev_state_get_interval(sd_state, 0);
	interval->numerator = 1;
	interval->denominator = rate->fps;

	if (fi->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		thp7312->link_freq = rate->link_freq;

	fi->interval = *interval;

	return 0;
}

static int thp7312_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct thp7312_device *thp7312 = to_thp7312_dev(sd);
	struct v4l2_subdev_state *sd_state;
	int ret;

	sd_state = v4l2_subdev_lock_and_get_active_state(sd);

	if (!enable) {
		thp7312_stream_enable(thp7312, false);

		pm_runtime_mark_last_busy(thp7312->dev);
		pm_runtime_put_autosuspend(thp7312->dev);

		v4l2_subdev_unlock_state(sd_state);

		return 0;
	}

	ret = pm_runtime_resume_and_get(thp7312->dev);
	if (ret)
		goto finish_unlock;

	ret = thp7312_init_mode(thp7312, sd_state);
	if (ret)
		goto finish_pm;

	if (!thp7312->ctrls_applied) {
		ret = __v4l2_ctrl_handler_setup(&thp7312->ctrl_handler);
		if (ret)
			goto finish_pm;

		thp7312->ctrls_applied = true;
	}

	ret = thp7312_stream_enable(thp7312, true);
	if (ret)
		goto finish_pm;

	goto finish_unlock;

finish_pm:
	pm_runtime_mark_last_busy(thp7312->dev);
	pm_runtime_put_autosuspend(thp7312->dev);
finish_unlock:
	v4l2_subdev_unlock_state(sd_state);

	return ret;
}

static int thp7312_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state)
{
	const struct thp7312_mode_info *default_mode = &thp7312_mode_info_data[0];
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_fract *interval;

	fmt = v4l2_subdev_state_get_format(sd_state, 0);
	interval = v4l2_subdev_state_get_interval(sd_state, 0);

	/*
	 * default init sequence initialize thp7312 to
	 * YUV422 YUYV VGA@30fps
	 */
	fmt->code = MEDIA_BUS_FMT_YUYV8_1X16;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
	fmt->width = default_mode->width;
	fmt->height = default_mode->height;
	fmt->field = V4L2_FIELD_NONE;

	interval->numerator = 1;
	interval->denominator = default_mode->rates[0].fps;

	return 0;
}

static const struct v4l2_subdev_core_ops thp7312_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops thp7312_video_ops = {
	.s_stream = thp7312_s_stream,
};

static const struct v4l2_subdev_pad_ops thp7312_pad_ops = {
	.enum_mbus_code = thp7312_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = thp7312_set_fmt,
	.get_frame_interval = v4l2_subdev_get_frame_interval,
	.set_frame_interval = thp7312_set_frame_interval,
	.enum_frame_size = thp7312_enum_frame_size,
	.enum_frame_interval = thp7312_enum_frame_interval,
};

static const struct v4l2_subdev_ops thp7312_subdev_ops = {
	.core = &thp7312_core_ops,
	.video = &thp7312_video_ops,
	.pad = &thp7312_pad_ops,
};

static const struct v4l2_subdev_internal_ops thp7312_internal_ops = {
	.init_state = thp7312_init_state,
};

/* -----------------------------------------------------------------------------
 * V4L2 Control Operations
 */

static inline struct thp7312_device *to_thp7312_from_ctrl(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct thp7312_device, ctrl_handler);
}

/* 0: 3000cm, 18: 8cm */
static const u16 thp7312_focus_values[] = {
	3000, 1000, 600, 450, 350,
	290,  240,  200, 170, 150,
	140,  130,  120, 110, 100,
	93,   87,   83,  80,
};

static int thp7312_set_focus(struct thp7312_device *thp7312)
{
	enum thp7312_focus_state new_state = thp7312->focus_state;
	bool continuous;
	u8 af_control;
	u8 af_setting;
	int ret = 0;

	/* Start by programming the manual focus position if it has changed. */
	if (thp7312->focus_absolute->is_new) {
		unsigned int value;

		value = thp7312_focus_values[thp7312->focus_absolute->val];

		ret = cci_write(thp7312->regmap,
				THP7312_REG_MANUAL_FOCUS_POSITION, value, NULL);
		if (ret)
			return ret;
	}

	/* Calculate the new focus state. */
	switch (thp7312->focus_state) {
	case THP7312_FOCUS_STATE_MANUAL:
	default:
		if (thp7312->focus_auto->val)
			new_state = THP7312_FOCUS_STATE_AUTO;
		else if (thp7312->focus_start->is_new)
			new_state = THP7312_FOCUS_STATE_ONESHOT;
		break;

	case THP7312_FOCUS_STATE_AUTO:
		if (!thp7312->focus_auto->val)
			new_state = THP7312_FOCUS_STATE_LOCKED;
		break;

	case THP7312_FOCUS_STATE_LOCKED:
		if (thp7312->focus_auto->val)
			new_state = THP7312_FOCUS_STATE_AUTO;
		else if (thp7312->focus_start->is_new)
			new_state = THP7312_FOCUS_STATE_ONESHOT;
		else if (thp7312->focus_absolute->is_new)
			new_state = THP7312_FOCUS_STATE_MANUAL;
		break;

	case THP7312_FOCUS_STATE_ONESHOT:
		if (thp7312->focus_auto->val)
			new_state = THP7312_FOCUS_STATE_AUTO;
		else if (thp7312->focus_start->is_new)
			new_state = THP7312_FOCUS_STATE_ONESHOT;
		else if (thp7312->focus_absolute->is_new)
			new_state = THP7312_FOCUS_STATE_MANUAL;
		break;
	}

	/*
	 * If neither the state nor the focus method has changed, and no new
	 * one-shot focus is requested, there's nothing new to program to the
	 * hardware.
	 */
	if (thp7312->focus_state == new_state &&
	    !thp7312->focus_method->is_new && !thp7312->focus_start->is_new)
		return 0;

	continuous = new_state == THP7312_FOCUS_STATE_MANUAL ||
		     new_state == THP7312_FOCUS_STATE_ONESHOT;

	switch (thp7312->focus_method->val) {
	case THP7312_FOCUS_METHOD_CONTRAST:
	default:
		af_setting = continuous
			   ? THP7312_REG_AF_SETTING_CONTINUOUS_CONTRAST
			   : THP7312_REG_AF_SETTING_ONESHOT_CONTRAST;
		break;
	case THP7312_FOCUS_METHOD_PDAF:
		af_setting = continuous
			   ? THP7312_REG_AF_SETTING_CONTINUOUS_PDAF
			   : THP7312_REG_AF_SETTING_ONESHOT_PDAF;
		break;
	case THP7312_FOCUS_METHOD_HYBRID:
		af_setting = continuous
			   ? THP7312_REG_AF_SETTING_CONTINUOUS_HYBRID
			   : THP7312_REG_AF_SETTING_ONESHOT_HYBRID;
		break;
	}

	switch (new_state) {
	case THP7312_FOCUS_STATE_MANUAL:
	default:
		af_control = THP7312_REG_AF_CONTROL_MANUAL;
		break;
	case THP7312_FOCUS_STATE_AUTO:
	case THP7312_FOCUS_STATE_ONESHOT:
		af_control = THP7312_REG_AF_CONTROL_AF;
		break;
	case THP7312_FOCUS_STATE_LOCKED:
		af_control = THP7312_REG_AF_CONTROL_LOCK;
		break;
	}

	cci_write(thp7312->regmap, THP7312_REG_AF_SETTING, af_setting, &ret);

	if (new_state == THP7312_FOCUS_STATE_MANUAL &&
	    (thp7312->focus_state == THP7312_FOCUS_STATE_AUTO ||
	     thp7312->focus_state == THP7312_FOCUS_STATE_ONESHOT)) {
		/* When switching to manual state, lock AF first. */
		cci_write(thp7312->regmap, THP7312_REG_AF_CONTROL,
			  THP7312_REG_AF_CONTROL_LOCK, &ret);
	}

	cci_write(thp7312->regmap, THP7312_REG_AF_CONTROL, af_control, &ret);

	if (ret)
		return ret;

	thp7312->focus_state = new_state;

	return 0;
}

static int thp7312_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct thp7312_device *thp7312 = to_thp7312_from_ctrl(ctrl);
	int ret = 0;
	u8 value;

	if (ctrl->flags & V4L2_CTRL_FLAG_INACTIVE)
		return -EINVAL;

	if (!pm_runtime_get_if_active(thp7312->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		cci_write(thp7312->regmap, THP7312_REG_BRIGHTNESS,
			  ctrl->val + 10, &ret);
		break;

	case V4L2_CID_THP7312_LOW_LIGHT_COMPENSATION:
		/* 0 = Auto adjust frame rate, 1 = Fix frame rate */
		cci_write(thp7312->regmap, THP7312_REG_AE_FIX_FRAME_RATE,
			  ctrl->val ? 0 : 1, &ret);
		break;

	case V4L2_CID_FOCUS_AUTO:
	case V4L2_CID_FOCUS_ABSOLUTE:
	case V4L2_CID_AUTO_FOCUS_START:
	case V4L2_CID_THP7312_AUTO_FOCUS_METHOD:
		ret = thp7312_set_focus(thp7312);
		break;

	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		value = (thp7312->hflip->val ? THP7312_REG_FLIP_MIRROR_MIRROR : 0)
		      | (thp7312->vflip->val ? THP7312_REG_FLIP_MIRROR_FLIP : 0);

		cci_write(thp7312->regmap, THP7312_REG_FLIP_MIRROR, value, &ret);
		break;

	case V4L2_CID_THP7312_NOISE_REDUCTION_AUTO:
	case V4L2_CID_THP7312_NOISE_REDUCTION_ABSOLUTE:
		value = thp7312->noise_reduction_auto->val ? 0
		      : THP7312_REG_NOISE_REDUCTION_FIXED |
			thp7312->noise_reduction_absolute->val;

		cci_write(thp7312->regmap, THP7312_REG_NOISE_REDUCTION, value,
			  &ret);
		break;

	case V4L2_CID_AUTO_WHITE_BALANCE:
		value = ctrl->val ? THP7312_WB_MODE_AUTO : THP7312_WB_MODE_MANUAL;

		cci_write(thp7312->regmap, THP7312_REG_WB_MODE, value, &ret);
		break;

	case V4L2_CID_RED_BALANCE:
		cci_write(thp7312->regmap, THP7312_REG_MANUAL_WB_RED_GAIN,
			  ctrl->val, &ret);
		break;

	case V4L2_CID_BLUE_BALANCE:
		cci_write(thp7312->regmap, THP7312_REG_MANUAL_WB_BLUE_GAIN,
			  ctrl->val, &ret);
		break;

	case V4L2_CID_AUTO_EXPOSURE_BIAS:
		cci_write(thp7312->regmap, THP7312_REG_AE_EXPOSURE_COMPENSATION,
			  ctrl->val, &ret);
		break;

	case V4L2_CID_POWER_LINE_FREQUENCY:
		if (ctrl->val == V4L2_CID_POWER_LINE_FREQUENCY_60HZ) {
			value = THP7312_AE_FLICKER_MODE_60;
		} else if (ctrl->val == V4L2_CID_POWER_LINE_FREQUENCY_50HZ) {
			value = THP7312_AE_FLICKER_MODE_50;
		} else {
			if (thp7312->fw_version == THP7312_FW_VERSION(40, 3)) {
				/* THP7312_AE_FLICKER_MODE_DISABLE is not supported */
				value = THP7312_AE_FLICKER_MODE_50;
			} else {
				value = THP7312_AE_FLICKER_MODE_DISABLE;
			}
		}

		cci_write(thp7312->regmap, THP7312_REG_AE_FLICKER_MODE,
			  value, &ret);
		break;

	case V4L2_CID_SATURATION:
		cci_write(thp7312->regmap, THP7312_REG_SATURATION,
			  ctrl->val, &ret);
		break;

	case V4L2_CID_CONTRAST:
		cci_write(thp7312->regmap, THP7312_REG_CONTRAST,
			  ctrl->val, &ret);
		break;

	case V4L2_CID_SHARPNESS:
		cci_write(thp7312->regmap, THP7312_REG_SHARPNESS,
			  ctrl->val, &ret);
		break;

	default:
		break;
	}

	pm_runtime_mark_last_busy(thp7312->dev);
	pm_runtime_put_autosuspend(thp7312->dev);

	return ret;
}

static const struct v4l2_ctrl_ops thp7312_ctrl_ops = {
	.s_ctrl = thp7312_s_ctrl,
};

/*
 * Refer to Documentation/userspace-api/media/drivers/thp7312.rst for details.
 */
static const struct v4l2_ctrl_config thp7312_ctrl_focus_method_cdaf = {
	.ops = &thp7312_ctrl_ops,
	.id = V4L2_CID_THP7312_AUTO_FOCUS_METHOD,
	.name = "Auto-Focus Method",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = THP7312_FOCUS_METHOD_CONTRAST,
	.def = THP7312_FOCUS_METHOD_CONTRAST,
	.max = THP7312_FOCUS_METHOD_CONTRAST,
	.step = 1,
};

static const struct v4l2_ctrl_config thp7312_ctrl_focus_method_pdaf = {
	.ops = &thp7312_ctrl_ops,
	.id = V4L2_CID_THP7312_AUTO_FOCUS_METHOD,
	.name = "Auto-Focus Method",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = THP7312_FOCUS_METHOD_CONTRAST,
	.def = THP7312_FOCUS_METHOD_HYBRID,
	.max = THP7312_FOCUS_METHOD_HYBRID,
	.step = 1,
};

static const struct v4l2_ctrl_config thp7312_v4l2_ctrls_custom[] = {
	{
		.ops = &thp7312_ctrl_ops,
		.id = V4L2_CID_THP7312_LOW_LIGHT_COMPENSATION,
		.name = "Low Light Compensation",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = 0,
		.def = 1,
		.max = 1,
		.step = 1,
	}, {
		.ops = &thp7312_ctrl_ops,
		.id = V4L2_CID_THP7312_NOISE_REDUCTION_AUTO,
		.name = "Noise Reduction Auto",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = 0,
		.def = 1,
		.max = 1,
		.step = 1,
	}, {
		.ops = &thp7312_ctrl_ops,
		.id = V4L2_CID_THP7312_NOISE_REDUCTION_ABSOLUTE,
		.name = "Noise Reduction Level",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.def = 0,
		.max = 10,
		.step = 1,
	},
};

static const s64 exp_bias_qmenu[] = {
	-2000, -1667, -1333, -1000, -667, -333, 0, 333, 667, 1000, 1333, 1667, 2000
};

static int thp7312_init_controls(struct thp7312_device *thp7312)
{
	struct v4l2_ctrl_handler *hdl = &thp7312->ctrl_handler;
	struct device *dev = thp7312->dev;
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl *link_freq;
	unsigned int num_controls;
	unsigned int i;
	u8 af_support;
	int ret;

	/*
	 * Check what auto-focus methods the connected sensor supports, if any.
	 * Firmwares before v90.03 didn't expose the AF_SUPPORT register,
	 * consider both CDAF and PDAF as supported in that case.
	 */
	if (thp7312->fw_version >= THP7312_FW_VERSION(90, 3)) {
		u64 val;

		ret = cci_read(thp7312->regmap, THP7312_REG_AF_SUPPORT, &val,
			       NULL);
		if (ret)
			return ret;

		af_support = val & (THP7312_AF_SUPPORT_PDAF |
				    THP7312_AF_SUPPORT_CONTRAST);
	} else {
		af_support = THP7312_AF_SUPPORT_PDAF
			   | THP7312_AF_SUPPORT_CONTRAST;
	}

	num_controls = 14 + ARRAY_SIZE(thp7312_v4l2_ctrls_custom)
		     + (af_support ? 4 : 0);

	v4l2_ctrl_handler_init(hdl, num_controls);

	if (af_support) {
		const struct v4l2_ctrl_config *af_method;

		af_method = af_support & THP7312_AF_SUPPORT_PDAF
			  ? &thp7312_ctrl_focus_method_pdaf
			  : &thp7312_ctrl_focus_method_cdaf;

		thp7312->focus_state = THP7312_FOCUS_STATE_MANUAL;

		thp7312->focus_auto =
			v4l2_ctrl_new_std(hdl, &thp7312_ctrl_ops,
					  V4L2_CID_FOCUS_AUTO,
					  0, 1, 1, 1);
		thp7312->focus_absolute =
			v4l2_ctrl_new_std(hdl, &thp7312_ctrl_ops,
					  V4L2_CID_FOCUS_ABSOLUTE,
					  0, ARRAY_SIZE(thp7312_focus_values),
					  1, 0);
		thp7312->focus_method =
			v4l2_ctrl_new_custom(hdl, af_method, NULL);
		thp7312->focus_start =
			v4l2_ctrl_new_std(hdl, &thp7312_ctrl_ops,
					  V4L2_CID_AUTO_FOCUS_START,
					  1, 1, 1, 1);

		v4l2_ctrl_cluster(4, &thp7312->focus_auto);
	}

	v4l2_ctrl_new_std(hdl, &thp7312_ctrl_ops, V4L2_CID_AUTO_WHITE_BALANCE,
			  0, 1, 1, 1);
	/* 32: 1x, 255: 7.95x */
	v4l2_ctrl_new_std(hdl, &thp7312_ctrl_ops, V4L2_CID_RED_BALANCE,
			  32, 255, 1, 64);
	/* 32: 1x, 255: 7.95x */
	v4l2_ctrl_new_std(hdl, &thp7312_ctrl_ops, V4L2_CID_BLUE_BALANCE,
			  32, 255, 1, 50);

	v4l2_ctrl_new_std(hdl, &thp7312_ctrl_ops, V4L2_CID_BRIGHTNESS,
			  -10, 10, 1, 0);
	v4l2_ctrl_new_std(hdl, &thp7312_ctrl_ops, V4L2_CID_SATURATION,
			  0, 31, 1, 10);
	v4l2_ctrl_new_std(hdl, &thp7312_ctrl_ops, V4L2_CID_CONTRAST,
			  0, 20, 1, 10);
	v4l2_ctrl_new_std(hdl, &thp7312_ctrl_ops, V4L2_CID_SHARPNESS,
			  0, 31, 1, 8);

	thp7312->hflip = v4l2_ctrl_new_std(hdl, &thp7312_ctrl_ops,
					   V4L2_CID_HFLIP, 0, 1, 1, 0);
	thp7312->vflip = v4l2_ctrl_new_std(hdl, &thp7312_ctrl_ops,
					   V4L2_CID_VFLIP, 0, 1, 1, 0);

	v4l2_ctrl_cluster(2, &thp7312->hflip);

	v4l2_ctrl_new_int_menu(hdl, &thp7312_ctrl_ops,
			       V4L2_CID_AUTO_EXPOSURE_BIAS,
			       ARRAY_SIZE(exp_bias_qmenu) - 1,
			       ARRAY_SIZE(exp_bias_qmenu) / 2, exp_bias_qmenu);

	v4l2_ctrl_new_std_menu(hdl, &thp7312_ctrl_ops,
			       V4L2_CID_POWER_LINE_FREQUENCY,
			       V4L2_CID_POWER_LINE_FREQUENCY_60HZ, 0,
			       V4L2_CID_POWER_LINE_FREQUENCY_50HZ);

	thp7312->link_freq = thp7312_mode_info_data[0].rates[0].link_freq;

	link_freq = v4l2_ctrl_new_int_menu(hdl, &thp7312_ctrl_ops,
					   V4L2_CID_LINK_FREQ, 0, 0,
					   &thp7312->link_freq);

	/* Set properties from fwnode (e.g. rotation, orientation). */
	ret = v4l2_fwnode_device_parse(dev, &props);
	if (ret) {
		dev_err(dev, "Failed to parse fwnode: %d\n", ret);
		goto error;
	}

	ret = v4l2_ctrl_new_fwnode_properties(hdl, &thp7312_ctrl_ops, &props);
	if (ret) {
		dev_err(dev, "Failed to create new v4l2 ctrl for fwnode properties: %d\n", ret);
		goto error;
	}

	for (i = 0; i < ARRAY_SIZE(thp7312_v4l2_ctrls_custom); i++) {
		const struct v4l2_ctrl_config *ctrl_cfg =
			&thp7312_v4l2_ctrls_custom[i];
		struct v4l2_ctrl *ctrl;

		ctrl = v4l2_ctrl_new_custom(hdl, ctrl_cfg, NULL);

		if (ctrl_cfg->id == V4L2_CID_THP7312_NOISE_REDUCTION_AUTO)
			thp7312->noise_reduction_auto = ctrl;
		else if (ctrl_cfg->id == V4L2_CID_THP7312_NOISE_REDUCTION_ABSOLUTE)
			thp7312->noise_reduction_absolute = ctrl;
	}

	v4l2_ctrl_cluster(2, &thp7312->noise_reduction_auto);

	if (hdl->error) {
		dev_err(dev, "v4l2_ctrl_handler error\n");
		ret = hdl->error;
		goto error;
	}

	link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	return ret;

error:
	v4l2_ctrl_handler_free(hdl);
	return ret;
}

/* -----------------------------------------------------------------------------
 * Firmware Update
 */

/*
 * The firmware data is made of 128kB of RAM firmware, followed by a
 * variable-size "header". Both are stored in flash memory.
 */
#define THP7312_FW_RAM_SIZE			(128 * 1024)
#define THP7312_FW_MIN_SIZE			(THP7312_FW_RAM_SIZE + 4)
#define THP7312_FW_MAX_SIZE			(THP7312_FW_RAM_SIZE + 64 * 1024)

/*
 * Data is first uploaded to the THP7312 128kB SRAM, and then written to flash.
 * The SRAM is exposed over I2C as 32kB banks, and up to 4kB of data can be
 * transferred in a single I2C write.
 */
#define THP7312_RAM_BANK_SIZE			(32 * 1024)
#define THP7312_FW_DOWNLOAD_UNIT		(4 * 1024)

#define THP7312_FLASH_MEMORY_ERASE_TIMEOUT	40

#define THP7312_FLASH_MAX_REG_READ_SIZE		10
#define THP7312_FLASH_MAX_REG_DATA_SIZE		10

static const u8 thp7312_cmd_config_flash_mem_if[] = {
	0xd5, 0x18, 0x00, 0x00, 0x00, 0x80
};

static const u8 thp7312_cmd_write_to_reg[] = {
	0xd5, 0x0c, 0x80, 0x00, 0x00, 0x00
};

static const u8 thp7312_cmd_read_reg[] = {
	0xd5, 0x04
};

/*
 * THP7312 Write data from RAM to Flash Memory
 * Command ID FF700F
 * Format: FF700F AA AA AA BB BB BB
 * AA AA AA: destination start address
 * BB BB BB: (write size - 1)
 * Source address always starts from 0
 */
static const u8 thp7312_cmd_write_ram_to_flash[] = { 0xff, 0x70, 0x0f };

/*
 * THP7312 Calculate CRC command
 * Command ID: FF70 09
 * Format: FF70 09 AA AA AA BB BB BB
 * AA AA AA: Start address of calculation
 * BB BB BB: (calculate size - 1)
 */
static const u8 thp7312_cmd_calc_crc[] = { 0xff, 0x70, 0x09 };

static const u8 thp7312_jedec_rdid[] = { SPINOR_OP_RDID, 0x00, 0x00, 0x00 };
static const u8 thp7312_jedec_rdsr[] = { SPINOR_OP_RDSR, 0x00, 0x00, 0x00 };
static const u8 thp7312_jedec_wen[] = { SPINOR_OP_WREN };

static int thp7312_read_firmware_version(struct thp7312_device *thp7312)
{
	u64 val = 0;
	int ret = 0;
	u8 major;
	u8 minor;

	cci_read(thp7312->regmap, THP7312_REG_FIRMWARE_VERSION_1, &val, &ret);
	major = val;

	cci_read(thp7312->regmap, THP7312_REG_FIRMWARE_VERSION_2, &val, &ret);
	minor = val;

	thp7312->fw_version = THP7312_FW_VERSION(major, minor);
	return ret;
}

static int thp7312_write_buf(struct thp7312_device *thp7312,
			     const u8 *write_buf, u16 write_size)
{
	struct i2c_client *client = to_i2c_client(thp7312->dev);
	int ret;

	ret = i2c_master_send(client, write_buf, write_size);
	return ret >= 0 ? 0 : ret;
}

static int __thp7312_flash_reg_write(struct thp7312_device *thp7312,
				     const u8 *write_buf, u16 write_size)
{
	struct device *dev = thp7312->dev;
	u8 temp_write_buf[THP7312_FLASH_MAX_REG_DATA_SIZE + 2];
	int ret;

	if (write_size > THP7312_FLASH_MAX_REG_DATA_SIZE) {
		dev_err(dev, "%s: Write size error size = %d\n",
			__func__, write_size);
		return -EINVAL;
	}

	ret = thp7312_write_buf(thp7312, thp7312_cmd_config_flash_mem_if,
				sizeof(thp7312_cmd_config_flash_mem_if));
	if (ret < 0) {
		dev_err(dev, "%s: Failed to config flash memory IF: %d\n",
			__func__, ret);
		return ret;
	}

	temp_write_buf[0] = 0xd5;
	temp_write_buf[1] = 0x00;
	memcpy((temp_write_buf + 2), write_buf, write_size);
	ret = thp7312_write_buf(thp7312, temp_write_buf, write_size + 2);
	if (ret < 0)
		return ret;

	thp7312_write_buf(thp7312, thp7312_cmd_write_to_reg,
			  sizeof(thp7312_cmd_write_to_reg));

	return 0;
}

static int __thp7312_flash_reg_read(struct thp7312_device *thp7312,
				    const u8 *write_buf, u16 write_size,
				    u8 *read_buf, u16 read_size)
{
	struct i2c_client *client = to_i2c_client(thp7312->dev);
	struct i2c_msg msgs[2];
	int ret;

	ret = __thp7312_flash_reg_write(thp7312, write_buf, write_size);
	if (ret)
		return ret;

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = sizeof(thp7312_cmd_read_reg);
	msgs[0].buf = (u8 *)thp7312_cmd_read_reg;

	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = read_size;
	msgs[1].buf = read_buf;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	return ret >= 0 ? 0 : ret;
}

#define thp7312_flash_reg_write(thp7312, wrbuf) \
	__thp7312_flash_reg_write(thp7312, wrbuf, sizeof(wrbuf))

#define thp7312_flash_reg_read(thp7312, wrbuf, rdbuf) \
	__thp7312_flash_reg_read(thp7312, wrbuf, sizeof(wrbuf), \
				 rdbuf, sizeof(rdbuf))

static enum fw_upload_err thp7312_fw_prepare_config(struct thp7312_device *thp7312)
{
	struct device *dev = thp7312->dev;
	int ret;

	ret = cci_write(thp7312->regmap, THP7312_REG_FW_MEMORY_IO_SETTING,
			THP7312_FW_MEMORY_IO_GPIO0, NULL);
	if (ret) {
		dev_err(dev, "Failed to set flash memory I/O\n");
		return FW_UPLOAD_ERR_HW_ERROR;
	}

	/* Set max drivability. */
	ret = cci_write(thp7312->regmap, THP7312_REG_FW_DRIVABILITY, 0x00777777,
			NULL);
	if (ret) {
		dev_err(dev, "Failed to set drivability: %d\n", ret);
		return FW_UPLOAD_ERR_HW_ERROR;
	}

	return FW_UPLOAD_ERR_NONE;
}

static enum fw_upload_err thp7312_fw_prepare_check(struct thp7312_device *thp7312)
{
	struct device *dev = thp7312->dev;
	u8 read_buf[3] = { 0 };
	int ret;

	/* Get JEDEC ID */
	ret = thp7312_flash_reg_read(thp7312, thp7312_jedec_rdid, read_buf);
	if (ret) {
		dev_err(dev, "Failed to get JEDEC ID: %d\n", ret);
		return FW_UPLOAD_ERR_HW_ERROR;
	}

	dev_dbg(dev, "Flash Memory: JEDEC ID = 0x%x 0x%x 0x%x\n",
		read_buf[0], read_buf[1], read_buf[2]);

	return FW_UPLOAD_ERR_NONE;
}

static enum fw_upload_err thp7312_fw_prepare_reset(struct thp7312_device *thp7312)
{
	struct device *dev = thp7312->dev;
	int ret;

	ret = cci_write(thp7312->regmap, THP7312_REG_FW_RESET_FLASH, 0x81, NULL);
	if (ret) {
		dev_err(dev, "Failed to reset flash memory: %d\n", ret);
		return FW_UPLOAD_ERR_HW_ERROR;
	}

	return FW_UPLOAD_ERR_NONE;
}

/* TODO: Erase only the amount of blocks necessary */
static enum fw_upload_err thp7312_flash_erase(struct thp7312_device *thp7312)
{
	struct device *dev = thp7312->dev;
	u8 read_buf[1] = { 0 };
	unsigned int i;
	u8 block;
	int ret;

	for (block = 0; block < 3; block++) {
		const u8 jedec_se[] = { SPINOR_OP_SE, block, 0x00, 0x00 };

		ret = thp7312_flash_reg_write(thp7312, thp7312_jedec_wen);
		if (ret < 0) {
			dev_err(dev, "Failed to enable flash for writing\n");
			return FW_UPLOAD_ERR_RW_ERROR;
		}

		ret = thp7312_flash_reg_write(thp7312, jedec_se);
		if (ret < 0) {
			dev_err(dev, "Failed to erase flash sector\n");
			return FW_UPLOAD_ERR_RW_ERROR;
		}

		for (i = 0; i < THP7312_FLASH_MEMORY_ERASE_TIMEOUT; i++) {
			usleep_range(100000, 101000);
			thp7312_flash_reg_read(thp7312, thp7312_jedec_rdsr,
					       read_buf);

			/* Check Busy bit. Busy == 0x0 means erase complete. */
			if (!(read_buf[0] & SR_WIP))
				break;
		}

		if (i == THP7312_FLASH_MEMORY_ERASE_TIMEOUT)
			return FW_UPLOAD_ERR_TIMEOUT;
	}

	thp7312_flash_reg_read(thp7312, thp7312_jedec_rdsr, read_buf);

	/* Check WEL bit. */
	if (read_buf[0] & SR_WEL)
		return FW_UPLOAD_ERR_HW_ERROR;

	return FW_UPLOAD_ERR_NONE;
}

static enum fw_upload_err
thp7312_write_download_data_by_unit(struct thp7312_device *thp7312,
				    unsigned int addr, const u8 *data,
				    unsigned int size)
{
	struct device *dev = thp7312->dev;
	u8 *write_buf = thp7312->fw_write_buf;
	int ret;

	dev_dbg(dev, "%s: addr = 0x%04x, data = 0x%p, size = %u\n",
		__func__, addr, data, size);

	write_buf[0] = (addr >> 8) & 0xff;
	write_buf[1] = (addr >> 0) & 0xff;
	memcpy(&write_buf[2], data, size);

	/*
	 * THP7312 Firmware download to RAM
	 * Command ID (address to download): 0x0000 - 0x7fff
	 * Format:: 0000 XX XX XX ........ XX
	 */
	ret = thp7312_write_buf(thp7312, write_buf, size + 2);
	if (ret < 0)
		dev_err(dev, "Unit transfer ERROR %s(): ret = %d\n", __func__, ret);

	return ret >= 0 ? FW_UPLOAD_ERR_NONE : FW_UPLOAD_ERR_RW_ERROR;
}

static enum fw_upload_err thp7312_fw_load_to_ram(struct thp7312_device *thp7312,
						 const u8 *data, u32 size)
{
	struct device *dev = thp7312->dev;
	enum fw_upload_err ret;
	unsigned int num_banks;
	unsigned int i, j;

	num_banks = DIV_ROUND_UP(size, THP7312_RAM_BANK_SIZE);

	dev_dbg(dev, "%s: loading %u bytes in SRAM (%u banks)\n", __func__,
		size, num_banks);

	for (i = 0; i < num_banks; i++) {
		const u32 bank_addr = 0x10000000 | (i * THP7312_RAM_BANK_SIZE);
		unsigned int bank_size;
		unsigned int num_chunks;

		ret = cci_write(thp7312->regmap, THP7312_REG_FW_DEST_BANK_ADDR,
				bank_addr, NULL);
		if (ret)
			return FW_UPLOAD_ERR_HW_ERROR;

		bank_size = min_t(u32, size, THP7312_RAM_BANK_SIZE);
		num_chunks = DIV_ROUND_UP(bank_size, THP7312_FW_DOWNLOAD_UNIT);

		dev_dbg(dev, "%s: loading %u bytes in SRAM bank %u (%u chunks)\n",
			__func__, bank_size, i, num_chunks);

		for (j = 0 ; j < num_chunks; j++) {
			unsigned int chunk_addr;
			unsigned int chunk_size;

			chunk_addr = j * THP7312_FW_DOWNLOAD_UNIT;
			chunk_size = min_t(u32, size, THP7312_FW_DOWNLOAD_UNIT);

			ret = thp7312_write_download_data_by_unit(thp7312, chunk_addr,
								  data, chunk_size);
			if (ret != FW_UPLOAD_ERR_NONE) {
				dev_err(dev, "Unit transfer ERROR at bank transfer %s(): %d\n",
					__func__, j);
				return ret;
			}

			data += chunk_size;
			size -= chunk_size;
		}
	}

	return FW_UPLOAD_ERR_NONE;
}

static enum fw_upload_err thp7312_fw_write_to_flash(struct thp7312_device *thp7312,
						    u32 dest, u32 write_size)
{
	u8 command[sizeof(thp7312_cmd_write_ram_to_flash) + 6];
	static const u32 cmd_size = sizeof(thp7312_cmd_write_ram_to_flash);
	u64 val;
	int ret;

	memcpy(command, thp7312_cmd_write_ram_to_flash, cmd_size);

	command[cmd_size] = (dest & 0xff0000) >> 16;
	command[cmd_size + 1] = (dest & 0x00ff00) >> 8;
	command[cmd_size + 2] = (dest & 0x0000ff);
	command[cmd_size + 3] = ((write_size - 1) & 0xff0000) >> 16;
	command[cmd_size + 4] = ((write_size - 1) & 0x00ff00) >> 8;
	command[cmd_size + 5] = ((write_size - 1) & 0x0000ff);

	ret = thp7312_write_buf(thp7312, command, sizeof(command));
	if (ret < 0)
		return FW_UPLOAD_ERR_RW_ERROR;

	usleep_range(8000000, 8100000);

	ret = cci_read(thp7312->regmap, THP7312_REG_FW_VERIFY_RESULT, &val,
		       NULL);
	if (ret < 0)
		return FW_UPLOAD_ERR_RW_ERROR;

	return val ?  FW_UPLOAD_ERR_HW_ERROR : FW_UPLOAD_ERR_NONE;
}

static enum fw_upload_err thp7312_fw_check_crc(struct thp7312_device *thp7312,
					       const u8 *fw_data, u32 fw_size)
{
	struct device *dev = thp7312->dev;
	u16 header_size = fw_size - THP7312_FW_RAM_SIZE;
	u8 command[sizeof(thp7312_cmd_calc_crc) + 6];
	static const u32 cmd_size = sizeof(thp7312_cmd_calc_crc);
	u32 size = THP7312_FW_RAM_SIZE - 4;
	u32 fw_crc;
	u64 crc;
	int ret;

	memcpy(command, thp7312_cmd_calc_crc, cmd_size);

	command[cmd_size] = 0;
	command[cmd_size + 1] = (header_size >> 8) & 0xff;
	command[cmd_size + 2] = header_size & 0xff;

	command[cmd_size + 3] = (size >> 16) & 0xff;
	command[cmd_size + 4] = (size >> 8) & 0xff;
	command[cmd_size + 5] = size & 0xff;

	ret = thp7312_write_buf(thp7312, command, sizeof(command));
	if (ret < 0)
		return FW_UPLOAD_ERR_RW_ERROR;

	usleep_range(2000000, 2100000);

	fw_crc = get_unaligned_be32(&fw_data[fw_size - 4]);

	ret = cci_read(thp7312->regmap, THP7312_REG_FW_CRC_RESULT, &crc, NULL);
	if (ret < 0)
		return FW_UPLOAD_ERR_RW_ERROR;

	if (fw_crc != crc) {
		dev_err(dev, "CRC mismatch: firmware 0x%08x, flash 0x%08llx\n",
			fw_crc, crc);
		return FW_UPLOAD_ERR_HW_ERROR;
	}

	return FW_UPLOAD_ERR_NONE;
}

static enum fw_upload_err thp7312_fw_prepare(struct fw_upload *fw_upload,
					     const u8 *data, u32 size)
{
	struct thp7312_device *thp7312 = fw_upload->dd_handle;
	struct device *dev = thp7312->dev;
	enum fw_upload_err ret;

	mutex_lock(&thp7312->fw_lock);
	thp7312->fw_cancel = false;
	mutex_unlock(&thp7312->fw_lock);

	if (size < THP7312_FW_MIN_SIZE || size > THP7312_FW_MAX_SIZE) {
		dev_err(dev, "%s: Invalid firmware size %d; must be between %d and %d\n",
			__func__, size, THP7312_FW_MIN_SIZE, THP7312_FW_MAX_SIZE);
		return FW_UPLOAD_ERR_INVALID_SIZE;
	}

	ret = thp7312_fw_prepare_config(thp7312);
	if (ret != FW_UPLOAD_ERR_NONE)
		return ret;

	ret = thp7312_fw_prepare_check(thp7312);
	if (ret != FW_UPLOAD_ERR_NONE)
		return ret;

	ret = thp7312_fw_prepare_reset(thp7312);
	if (ret != FW_UPLOAD_ERR_NONE)
		return ret;

	mutex_lock(&thp7312->fw_lock);
	ret = thp7312->fw_cancel ? FW_UPLOAD_ERR_CANCELED : FW_UPLOAD_ERR_NONE;
	mutex_unlock(&thp7312->fw_lock);

	return ret;
}

static enum fw_upload_err thp7312_fw_write(struct fw_upload *fw_upload,
					   const u8 *data, u32 offset,
					   u32 size, u32 *written)
{
	struct thp7312_device *thp7312 = fw_upload->dd_handle;
	struct device *dev = thp7312->dev;
	u16 header_size = size - THP7312_FW_RAM_SIZE;
	enum fw_upload_err ret;
	bool cancel;

	mutex_lock(&thp7312->fw_lock);
	cancel = thp7312->fw_cancel;
	mutex_unlock(&thp7312->fw_lock);

	if (cancel)
		return FW_UPLOAD_ERR_CANCELED;

	ret = thp7312_flash_erase(thp7312);
	if (ret != FW_UPLOAD_ERR_NONE)
		return ret;

	ret = thp7312_fw_load_to_ram(thp7312, data, THP7312_FW_RAM_SIZE);
	if (ret != FW_UPLOAD_ERR_NONE)
		return ret;

	ret = thp7312_fw_write_to_flash(thp7312, 0, 0x1ffff);
	if (ret != FW_UPLOAD_ERR_NONE)
		return ret;

	ret = thp7312_fw_load_to_ram(thp7312, data + THP7312_FW_RAM_SIZE, header_size);
	if (ret != FW_UPLOAD_ERR_NONE)
		return ret;

	ret = thp7312_fw_write_to_flash(thp7312, 0x20000, header_size - 1);
	if (ret != FW_UPLOAD_ERR_NONE)
		return ret;

	ret = thp7312_fw_check_crc(thp7312, data, size);
	if (ret != FW_UPLOAD_ERR_NONE)
		return ret;

	dev_info(dev, "Successfully wrote firmware\n");

	*written = size;
	return FW_UPLOAD_ERR_NONE;
}

static enum fw_upload_err thp7312_fw_poll_complete(struct fw_upload *fw_upload)
{
	return FW_UPLOAD_ERR_NONE;
}

/*
 * This may be called asynchronously with an on-going update.  All other
 * functions are called sequentially in a single thread. To avoid contention on
 * register accesses, only update the cancel_request flag. Other functions will
 * check this flag and handle the cancel request synchronously.
 */
static void thp7312_fw_cancel(struct fw_upload *fw_upload)
{
	struct thp7312_device *thp7312 = fw_upload->dd_handle;

	mutex_lock(&thp7312->fw_lock);
	thp7312->fw_cancel = true;
	mutex_unlock(&thp7312->fw_lock);
}

static const struct fw_upload_ops thp7312_fw_upload_ops = {
	.prepare = thp7312_fw_prepare,
	.write = thp7312_fw_write,
	.poll_complete = thp7312_fw_poll_complete,
	.cancel = thp7312_fw_cancel,
};

static int thp7312_register_flash_mode(struct thp7312_device *thp7312)
{
	struct device *dev = thp7312->dev;
	struct fw_upload *fwl;
	u64 val;
	int ret;

	dev_info(dev, "booted in flash mode\n");

	mutex_init(&thp7312->fw_lock);

	thp7312->fw_write_buf = devm_kzalloc(dev, THP7312_FW_DOWNLOAD_UNIT + 2,
					     GFP_KERNEL);
	if (!thp7312->fw_write_buf)
		return -ENOMEM;

	ret = __thp7312_power_on(thp7312);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to power on\n");

	ret = cci_read(thp7312->regmap, THP7312_REG_FW_STATUS, &val, NULL);
	if (ret) {
		dev_err_probe(dev, ret, "Camera status read failed\n");
		goto error;
	}

	fwl = firmware_upload_register(THIS_MODULE, dev, "thp7312-firmware",
				       &thp7312_fw_upload_ops, thp7312);
	if (IS_ERR(fwl)) {
		ret = PTR_ERR(fwl);
		dev_err_probe(dev, ret, "Failed to register firmware upload\n");
		goto error;
	}

	thp7312->fwl = fwl;
	return 0;

error:
	__thp7312_power_off(thp7312);
	return ret;
}

/* -----------------------------------------------------------------------------
 * Probe & Remove
 */

static int thp7312_get_regulators(struct thp7312_device *thp7312)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(thp7312->supplies); i++)
		thp7312->supplies[i].supply = thp7312_supply_name[i];

	return devm_regulator_bulk_get(thp7312->dev,
				       ARRAY_SIZE(thp7312->supplies),
				       thp7312->supplies);
}

static int thp7312_sensor_parse_dt(struct thp7312_device *thp7312,
				   struct fwnode_handle *node)
{
	struct device *dev = thp7312->dev;
	struct thp7312_sensor *sensor;
	const char *model;
	u8 data_lanes[4];
	u32 values[4];
	unsigned int i;
	u32 reg;
	int ret;

	/* Retrieve the sensor index from the reg property. */
	ret = fwnode_property_read_u32(node, "reg", &reg);
	if (ret < 0) {
		dev_err(dev, "'reg' property missing in sensor node\n");
		return -EINVAL;
	}

	if (reg >= ARRAY_SIZE(thp7312->sensors)) {
		dev_err(dev, "Out-of-bounds 'reg' value %u\n", reg);
		return -EINVAL;
	}

	sensor = &thp7312->sensors[reg];
	if (sensor->info) {
		dev_err(dev, "Duplicate entry for sensor %u\n", reg);
		return -EINVAL;
	}

	ret = fwnode_property_read_string(node, "thine,model", &model);
	if (ret < 0) {
		dev_err(dev, "'thine,model' property missing in sensor node\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(thp7312_sensor_info); i++) {
		const struct thp7312_sensor_info *info =
			&thp7312_sensor_info[i];

		if (!strcmp(info->model, model)) {
			sensor->info = info;
			break;
		}
	}

	if (!sensor->info) {
		dev_err(dev, "Unsupported sensor model %s\n", model);
		return -EINVAL;
	}

	ret = fwnode_property_read_u32_array(node, "data-lanes", values,
					     ARRAY_SIZE(values));
	if (ret < 0) {
		dev_err(dev, "Failed to read property data-lanes: %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(data_lanes); ++i)
		data_lanes[i] = values[i];

	ret = thp7312_map_data_lanes(&sensor->lane_remap, data_lanes,
				     ARRAY_SIZE(data_lanes));
	if (ret) {
		dev_err(dev, "Invalid sensor@%u data-lanes value\n", reg);
		return ret;
	}

	return 0;
}

static int thp7312_parse_dt(struct thp7312_device *thp7312)
{
	struct v4l2_fwnode_endpoint ep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct device *dev = thp7312->dev;
	struct fwnode_handle *endpoint;
	struct fwnode_handle *sensors;
	unsigned int num_sensors = 0;
	struct fwnode_handle *node;
	int ret;

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint)
		return dev_err_probe(dev, -EINVAL, "Endpoint node not found\n");

	ret = v4l2_fwnode_endpoint_parse(endpoint, &ep);
	fwnode_handle_put(endpoint);
	if (ret)
		return dev_err_probe(dev, ret, "Could not parse endpoint\n");

	ret = thp7312_map_data_lanes(&thp7312->lane_remap,
				     ep.bus.mipi_csi2.data_lanes,
				     ep.bus.mipi_csi2.num_data_lanes);
	if (ret) {
		dev_err(dev, "Invalid data-lanes value\n");
		return ret;
	}

	/*
	 * The thine,boot-mode property is optional and default to
	 * THP7312_BOOT_MODE_SPI_MASTER (1).
	 */
	thp7312->boot_mode = THP7312_BOOT_MODE_SPI_MASTER;
	ret = device_property_read_u32(dev, "thine,boot-mode",
				       &thp7312->boot_mode);
	if (ret && ret != -EINVAL)
		return dev_err_probe(dev, ret, "Property '%s' is invalid\n",
				     "thine,boot-mode");

	if (thp7312->boot_mode != THP7312_BOOT_MODE_2WIRE_SLAVE &&
	    thp7312->boot_mode != THP7312_BOOT_MODE_SPI_MASTER)
		return dev_err_probe(dev, -EINVAL, "Invalid '%s' value %u\n",
				     "thine,boot-mode", thp7312->boot_mode);

	/* Sensors */
	sensors = device_get_named_child_node(dev, "sensors");
	if (!sensors) {
		dev_err(dev, "'sensors' child node not found\n");
		return -EINVAL;
	}

	fwnode_for_each_available_child_node(sensors, node) {
		if (fwnode_name_eq(node, "sensor")) {
			if (!thp7312_sensor_parse_dt(thp7312, node))
				num_sensors++;
		}
	}

	fwnode_handle_put(sensors);

	if (!num_sensors) {
		dev_err(dev, "No sensor found\n");
		return -EINVAL;
	}

	return 0;
}

static int thp7312_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct thp7312_device *thp7312;
	int ret;

	thp7312 = devm_kzalloc(dev, sizeof(*thp7312), GFP_KERNEL);
	if (!thp7312)
		return -ENOMEM;

	thp7312->dev = dev;

	thp7312->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(thp7312->regmap))
		return dev_err_probe(dev, PTR_ERR(thp7312->regmap),
				     "Unable to initialize I2C\n");

	ret = thp7312_parse_dt(thp7312);
	if (ret < 0)
		return ret;

	ret = thp7312_get_regulators(thp7312);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	thp7312->iclk = devm_clk_get(dev, NULL);
	if (IS_ERR(thp7312->iclk))
		return dev_err_probe(dev, PTR_ERR(thp7312->iclk),
				     "Failed to get iclk\n");

	thp7312->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(thp7312->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(thp7312->reset_gpio),
				     "Failed to get reset gpio\n");

	if (thp7312->boot_mode == THP7312_BOOT_MODE_2WIRE_SLAVE)
		return thp7312_register_flash_mode(thp7312);

	v4l2_i2c_subdev_init(&thp7312->sd, client, &thp7312_subdev_ops);
	thp7312->sd.internal_ops = &thp7312_internal_ops;
	thp7312->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	thp7312->pad.flags = MEDIA_PAD_FL_SOURCE;
	thp7312->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ret = media_entity_pads_init(&thp7312->sd.entity, 1, &thp7312->pad);
	if (ret)
		return ret;

	/*
	 * Enable power management. The driver supports runtime PM, but needs to
	 * work when runtime PM is disabled in the kernel. To that end, power
	 * the device manually here.
	 */
	ret = thp7312_power_on(thp7312);
	if (ret)
		goto err_entity_cleanup;

	ret = thp7312_read_firmware_version(thp7312);
	if (ret < 0) {
		dev_err(dev, "Camera is not found\n");
		goto err_power_off;
	}

	ret = thp7312_init_controls(thp7312);
	if (ret) {
		dev_err(dev, "Failed to initialize controls\n");
		goto err_power_off;
	}

	thp7312->sd.ctrl_handler = &thp7312->ctrl_handler;
	thp7312->sd.state_lock = thp7312->ctrl_handler.lock;

	ret = v4l2_subdev_init_finalize(&thp7312->sd);
	if (ret < 0) {
		dev_err(dev, "Subdev active state initialization failed\n");
		goto err_free_ctrls;
	}

	/*
	 * Enable runtime PM with autosuspend. As the device has been powered
	 * manually, mark it as active, and increase the usage count without
	 * resuming the device.
	 */
	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);

	ret = v4l2_async_register_subdev(&thp7312->sd);
	if (ret < 0) {
		dev_err(dev, "Subdev registration failed\n");
		goto err_pm;
	}

	/*
	 * Decrease the PM usage count. The device will get suspended after the
	 * autosuspend delay, turning the power off.
	 */
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	dev_info(dev, "THP7312 firmware version %02u.%02u\n",
		 THP7312_FW_VERSION_MAJOR(thp7312->fw_version),
		 THP7312_FW_VERSION_MINOR(thp7312->fw_version));

	return 0;

err_pm:
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	v4l2_subdev_cleanup(&thp7312->sd);
err_free_ctrls:
	v4l2_ctrl_handler_free(&thp7312->ctrl_handler);
err_power_off:
	thp7312_power_off(thp7312);
err_entity_cleanup:
	media_entity_cleanup(&thp7312->sd.entity);
	return ret;
}

static void thp7312_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct thp7312_device *thp7312 = to_thp7312_dev(sd);

	if (thp7312->boot_mode == THP7312_BOOT_MODE_2WIRE_SLAVE) {
		firmware_upload_unregister(thp7312->fwl);
		__thp7312_power_off(thp7312);
		return;
	}

	v4l2_async_unregister_subdev(&thp7312->sd);
	v4l2_subdev_cleanup(&thp7312->sd);
	media_entity_cleanup(&thp7312->sd.entity);
	v4l2_ctrl_handler_free(&thp7312->ctrl_handler);

	/*
	 * Disable runtime PM. In case runtime PM is disabled in the kernel,
	 * make sure to turn power off manually.
	 */
	pm_runtime_disable(thp7312->dev);
	if (!pm_runtime_status_suspended(thp7312->dev))
		thp7312_power_off(thp7312);
	pm_runtime_set_suspended(thp7312->dev);
}

static const struct of_device_id thp7312_dt_ids[] = {
	{ .compatible = "thine,thp7312" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, thp7312_dt_ids);

static struct i2c_driver thp7312_i2c_driver = {
	.driver = {
		.name  = "thp7312",
		.pm = &thp7312_pm_ops,
		.of_match_table	= thp7312_dt_ids,
	},
	.probe = thp7312_probe,
	.remove = thp7312_remove,
};

module_i2c_driver(thp7312_i2c_driver);

MODULE_DESCRIPTION("THP7312 MIPI Camera Subdev Driver");
MODULE_LICENSE("GPL");
