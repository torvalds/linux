// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ilitek ILI9322 TFT LCD drm_panel driver.
 *
 * This panel can be configured to support:
 * - 8-bit serial RGB interface
 * - 24-bit parallel RGB interface
 * - 8-bit ITU-R BT.601 interface
 * - 8-bit ITU-R BT.656 interface
 * - Up to 320RGBx240 dots resolution TFT LCD displays
 * - Scaling, brightness and contrast
 *
 * The scaling means that the display accepts a 640x480 or 720x480
 * input and rescales it to fit to the 320x240 display. So what we
 * present to the system is something else than what comes out on the
 * actual display.
 *
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 * Derived from drivers/drm/gpu/panel/panel-samsung-ld9040.c
 */

#include <linux/bitops.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define ILI9322_CHIP_ID			0x00
#define ILI9322_CHIP_ID_MAGIC		0x96

/*
 * Voltage on the communication interface, from 0.7 (0x00)
 * to 1.32 (0x1f) times the VREG1OUT voltage in 2% increments.
 * 1.00 (0x0f) is the default.
 */
#define ILI9322_VCOM_AMP		0x01

/*
 * High voltage on the communication signals, from 0.37 (0x00) to
 * 1.0 (0x3f) times the VREGOUT1 voltage in 1% increments.
 * 0.83 (0x2e) is the default.
 */
#define ILI9322_VCOM_HIGH		0x02

/*
 * VREG1 voltage regulator from 3.6V (0x00) to 6.0V (0x18) in 0.1V
 * increments. 5.4V (0x12) is the default. This is the reference
 * voltage for the VCOM levels and the greyscale level.
 */
#define ILI9322_VREG1_VOLTAGE		0x03

/* Describes the incoming signal */
#define ILI9322_ENTRY			0x06
/* 0 = right-to-left, 1 = left-to-right (default), horizontal flip */
#define ILI9322_ENTRY_HDIR		BIT(0)
/* 0 = down-to-up, 1 = up-to-down (default), vertical flip  */
#define ILI9322_ENTRY_VDIR		BIT(1)
/* NTSC, PAL or autodetect */
#define ILI9322_ENTRY_NTSC		(0 << 2)
#define ILI9322_ENTRY_PAL		(1 << 2)
#define ILI9322_ENTRY_AUTODETECT	(3 << 2)
/* Input format */
#define ILI9322_ENTRY_SERIAL_RGB_THROUGH (0 << 4)
#define ILI9322_ENTRY_SERIAL_RGB_ALIGNED (1 << 4)
#define ILI9322_ENTRY_SERIAL_RGB_DUMMY_320X240 (2 << 4)
#define ILI9322_ENTRY_SERIAL_RGB_DUMMY_360X240 (3 << 4)
#define ILI9322_ENTRY_DISABLE_1		(4 << 4)
#define ILI9322_ENTRY_PARALLEL_RGB_THROUGH (5 << 4)
#define ILI9322_ENTRY_PARALLEL_RGB_ALIGNED (6 << 4)
#define ILI9322_ENTRY_YUV_640Y_320CBCR_25_54_MHZ (7 << 4)
#define ILI9322_ENTRY_YUV_720Y_360CBCR_27_MHZ (8 << 4)
#define ILI9322_ENTRY_DISABLE_2		(9 << 4)
#define ILI9322_ENTRY_ITU_R_BT_656_720X360 (10 << 4)
#define ILI9322_ENTRY_ITU_R_BT_656_640X320 (11 << 4)

/* Power control */
#define ILI9322_POW_CTRL		0x07
#define ILI9322_POW_CTRL_STB		BIT(0) /* 0 = standby, 1 = normal */
#define ILI9322_POW_CTRL_VGL		BIT(1) /* 0 = off, 1 = on  */
#define ILI9322_POW_CTRL_VGH		BIT(2) /* 0 = off, 1 = on  */
#define ILI9322_POW_CTRL_DDVDH		BIT(3) /* 0 = off, 1 = on  */
#define ILI9322_POW_CTRL_VCOM		BIT(4) /* 0 = off, 1 = on  */
#define ILI9322_POW_CTRL_VCL		BIT(5) /* 0 = off, 1 = on  */
#define ILI9322_POW_CTRL_AUTO		BIT(6) /* 0 = interactive, 1 = auto */
#define ILI9322_POW_CTRL_STANDBY	(ILI9322_POW_CTRL_VGL | \
					 ILI9322_POW_CTRL_VGH | \
					 ILI9322_POW_CTRL_DDVDH | \
					 ILI9322_POW_CTRL_VCL | \
					 ILI9322_POW_CTRL_AUTO | \
					 BIT(7))
#define ILI9322_POW_CTRL_DEFAULT	(ILI9322_POW_CTRL_STANDBY | \
					 ILI9322_POW_CTRL_STB)

/* Vertical back porch bits 0..5 */
#define ILI9322_VBP			0x08

/* Horizontal back porch, 8 bits */
#define ILI9322_HBP			0x09

/*
 * Polarity settings:
 * 1 = positive polarity
 * 0 = negative polarity
 */
#define ILI9322_POL			0x0a
#define ILI9322_POL_DCLK		BIT(0) /* 1 default */
#define ILI9322_POL_HSYNC		BIT(1) /* 0 default */
#define ILI9322_POL_VSYNC		BIT(2) /* 0 default */
#define ILI9322_POL_DE			BIT(3) /* 1 default */
/*
 * 0 means YCBCR are ordered Cb0,Y0,Cr0,Y1,Cb2,Y2,Cr2,Y3 (default)
 *   in RGB mode this means RGB comes in RGBRGB
 * 1 means YCBCR are ordered Cr0,Y0,Cb0,Y1,Cr2,Y2,Cb2,Y3
 *   in RGB mode this means RGB comes in BGRBGR
 */
#define ILI9322_POL_YCBCR_MODE		BIT(4)
/* Formula A for YCbCR->RGB = 0, Formula B = 1 */
#define ILI9322_POL_FORMULA		BIT(5)
/* Reverse polarity: 0 = 0..255, 1 = 255..0 */
#define ILI9322_POL_REV			BIT(6)

#define ILI9322_IF_CTRL			0x0b
#define ILI9322_IF_CTRL_HSYNC_VSYNC	0x00
#define ILI9322_IF_CTRL_HSYNC_VSYNC_DE	BIT(2)
#define ILI9322_IF_CTRL_DE_ONLY		BIT(3)
#define ILI9322_IF_CTRL_SYNC_DISABLED	(BIT(2) | BIT(3))
#define ILI9322_IF_CTRL_LINE_INVERSION	BIT(0) /* Not set means frame inv */

#define ILI9322_GLOBAL_RESET		0x04
#define ILI9322_GLOBAL_RESET_ASSERT	0x00 /* bit 0 = 0 -> reset */

/*
 * 4+4 bits of negative and positive gamma correction
 * Upper nybble, bits 4-7 are negative gamma
 * Lower nybble, bits 0-3 are positive gamma
 */
#define ILI9322_GAMMA_1			0x10
#define ILI9322_GAMMA_2			0x11
#define ILI9322_GAMMA_3			0x12
#define ILI9322_GAMMA_4			0x13
#define ILI9322_GAMMA_5			0x14
#define ILI9322_GAMMA_6			0x15
#define ILI9322_GAMMA_7			0x16
#define ILI9322_GAMMA_8			0x17

/*
 * enum ili9322_input - the format of the incoming signal to the panel
 *
 * The panel can be connected to various input streams and four of them can
 * be selected by electronic straps on the display. However it is possible
 * to select another mode or override the electronic default with this
 * setting.
 */
enum ili9322_input {
	ILI9322_INPUT_SRGB_THROUGH = 0x0,
	ILI9322_INPUT_SRGB_ALIGNED = 0x1,
	ILI9322_INPUT_SRGB_DUMMY_320X240 = 0x2,
	ILI9322_INPUT_SRGB_DUMMY_360X240 = 0x3,
	ILI9322_INPUT_DISABLED_1 = 0x4,
	ILI9322_INPUT_PRGB_THROUGH = 0x5,
	ILI9322_INPUT_PRGB_ALIGNED = 0x6,
	ILI9322_INPUT_YUV_640X320_YCBCR = 0x7,
	ILI9322_INPUT_YUV_720X360_YCBCR = 0x8,
	ILI9322_INPUT_DISABLED_2 = 0x9,
	ILI9322_INPUT_ITU_R_BT656_720X360_YCBCR = 0xa,
	ILI9322_INPUT_ITU_R_BT656_640X320_YCBCR = 0xb,
	ILI9322_INPUT_UNKNOWN = 0xc,
};

static const char * const ili9322_inputs[] = {
	"8 bit serial RGB through",
	"8 bit serial RGB aligned",
	"8 bit serial RGB dummy 320x240",
	"8 bit serial RGB dummy 360x240",
	"disabled 1",
	"24 bit parallel RGB through",
	"24 bit parallel RGB aligned",
	"24 bit YUV 640Y 320CbCr",
	"24 bit YUV 720Y 360CbCr",
	"disabled 2",
	"8 bit ITU-R BT.656 720Y 360CbCr",
	"8 bit ITU-R BT.656 640Y 320CbCr",
};

/**
 * struct ili9322_config - the system specific ILI9322 configuration
 * @width_mm: physical panel width [mm]
 * @height_mm: physical panel height [mm]
 * @flip_horizontal: flip the image horizontally (right-to-left scan)
 * (only in RGB and YUV modes)
 * @flip_vertical: flip the image vertically (down-to-up scan)
 * (only in RGB and YUV modes)
 * @input: the input/entry type used in this system, if this is set to
 * ILI9322_INPUT_UNKNOWN the driver will try to figure it out by probing
 * the hardware
 * @vreg1out_mv: the output in microvolts for the VREGOUT1 regulator used
 * to drive the physical display. Valid ranges are 3600 thru 6000 in 100
 * microvolt increments. If not specified, hardware defaults will be
 * used (4.5V).
 * @vcom_high_percent: the percentage of VREGOUT1 used for the peak
 * voltage on the communications link. Valid ranges are 37 thru 100
 * percent. If not specified, hardware defaults will be used (91%).
 * @vcom_amplitude_percent: the percentage of VREGOUT1 used for the
 * peak-to-peak amplitude of the communcation signals to the physical
 * display. Valid ranges are 70 thru 132 percent in increments if two
 * percent. Odd percentages will be truncated. If not specified, hardware
 * defaults will be used (114%).
 * @dclk_active_high: data/pixel clock active high, data will be clocked
 * in on the rising edge of the DCLK (this is usually the case).
 * @syncmode: The synchronization mode, what sync signals are emitted.
 * See the enum for details.
 * @de_active_high: DE (data entry) is active high
 * @hsync_active_high: HSYNC is active high
 * @vsync_active_high: VSYNC is active high
 * @gamma_corr_pos: a set of 8 nybbles describing positive
 * gamma correction for voltages V1 thru V8. Valid range 0..15
 * @gamma_corr_neg: a set of 8 nybbles describing negative
 * gamma correction for voltages V1 thru V8. Valid range 0..15
 *
 * These adjust what grayscale voltage will be output for input data V1 = 0,
 * V2 = 16, V3 = 48, V4 = 96, V5 = 160, V6 = 208, V7 = 240 and V8 = 255.
 * The curve is shaped like this:
 *
 *  ^
 *  |                                                        V8
 *  |                                                   V7
 *  |                                          V6
 *  |                               V5
 *  |                    V4
 *  |            V3
 *  |     V2
 *  | V1
 *  +----------------------------------------------------------->
 *    0   16     48      96         160        208      240  255
 *
 * The negative and postive gamma values adjust the V1 thru V8 up/down
 * according to the datasheet specifications. This is a property of the
 * physical display connected to the display controller and may vary.
 * If defined, both arrays must be supplied in full. If the properties
 * are not supplied, hardware defaults will be used.
 */
struct ili9322_config {
	u32 width_mm;
	u32 height_mm;
	bool flip_horizontal;
	bool flip_vertical;
	enum ili9322_input input;
	u32 vreg1out_mv;
	u32 vcom_high_percent;
	u32 vcom_amplitude_percent;
	bool dclk_active_high;
	bool de_active_high;
	bool hsync_active_high;
	bool vsync_active_high;
	u8 syncmode;
	u8 gamma_corr_pos[8];
	u8 gamma_corr_neg[8];
};

struct ili9322 {
	struct device *dev;
	const struct ili9322_config *conf;
	struct drm_panel panel;
	struct regmap *regmap;
	struct regulator_bulk_data supplies[3];
	struct gpio_desc *reset_gpio;
	enum ili9322_input input;
	struct videomode vm;
	u8 gamma[8];
	u8 vreg1out;
	u8 vcom_high;
	u8 vcom_amplitude;
};

static inline struct ili9322 *panel_to_ili9322(struct drm_panel *panel)
{
	return container_of(panel, struct ili9322, panel);
}

static int ili9322_regmap_spi_write(void *context, const void *data,
				    size_t count)
{
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);
	u8 buf[2];

	/* Clear bit 7 to write */
	memcpy(buf, data, 2);
	buf[0] &= ~0x80;

	dev_dbg(dev, "WRITE: %02x %02x\n", buf[0], buf[1]);
	return spi_write_then_read(spi, buf, 2, NULL, 0);
}

static int ili9322_regmap_spi_read(void *context, const void *reg,
				   size_t reg_size, void *val, size_t val_size)
{
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);
	u8 buf[1];

	/* Set bit 7 to 1 to read */
	memcpy(buf, reg, 1);
	dev_dbg(dev, "READ: %02x reg size = %zu, val size = %zu\n",
		buf[0], reg_size, val_size);
	buf[0] |= 0x80;

	return spi_write_then_read(spi, buf, 1, val, 1);
}

static struct regmap_bus ili9322_regmap_bus = {
	.write = ili9322_regmap_spi_write,
	.read = ili9322_regmap_spi_read,
	.reg_format_endian_default = REGMAP_ENDIAN_BIG,
	.val_format_endian_default = REGMAP_ENDIAN_BIG,
};

static bool ili9322_writeable_reg(struct device *dev, unsigned int reg)
{
	/* Just register 0 is read-only */
	if (reg == 0x00)
		return false;
	return true;
}

static const struct regmap_config ili9322_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x44,
	.cache_type = REGCACHE_MAPLE,
	.writeable_reg = ili9322_writeable_reg,
};

static int ili9322_init(struct drm_panel *panel, struct ili9322 *ili)
{
	u8 reg;
	int ret;
	int i;

	/* Reset display */
	ret = regmap_write(ili->regmap, ILI9322_GLOBAL_RESET,
			   ILI9322_GLOBAL_RESET_ASSERT);
	if (ret) {
		dev_err(ili->dev, "can't issue GRESET (%d)\n", ret);
		return ret;
	}

	/* Set up the main voltage regulator */
	if (ili->vreg1out != U8_MAX) {
		ret = regmap_write(ili->regmap, ILI9322_VREG1_VOLTAGE,
				   ili->vreg1out);
		if (ret) {
			dev_err(ili->dev, "can't set up VREG1OUT (%d)\n", ret);
			return ret;
		}
	}

	if (ili->vcom_amplitude != U8_MAX) {
		ret = regmap_write(ili->regmap, ILI9322_VCOM_AMP,
				   ili->vcom_amplitude);
		if (ret) {
			dev_err(ili->dev,
				"can't set up VCOM amplitude (%d)\n", ret);
			return ret;
		}
	}

	if (ili->vcom_high != U8_MAX) {
		ret = regmap_write(ili->regmap, ILI9322_VCOM_HIGH,
				   ili->vcom_high);
		if (ret) {
			dev_err(ili->dev, "can't set up VCOM high (%d)\n", ret);
			return ret;
		}
	}

	/* Set up gamma correction */
	for (i = 0; i < ARRAY_SIZE(ili->gamma); i++) {
		ret = regmap_write(ili->regmap, ILI9322_GAMMA_1 + i,
				   ili->gamma[i]);
		if (ret) {
			dev_err(ili->dev,
				"can't write gamma V%d to 0x%02x (%d)\n",
				i + 1, ILI9322_GAMMA_1 + i, ret);
			return ret;
		}
	}

	/*
	 * Polarity and inverted color order for RGB input.
	 * None of this applies in the BT.656 mode.
	 */
	reg = 0;
	if (ili->conf->dclk_active_high)
		reg = ILI9322_POL_DCLK;
	if (ili->conf->de_active_high)
		reg |= ILI9322_POL_DE;
	if (ili->conf->hsync_active_high)
		reg |= ILI9322_POL_HSYNC;
	if (ili->conf->vsync_active_high)
		reg |= ILI9322_POL_VSYNC;
	ret = regmap_write(ili->regmap, ILI9322_POL, reg);
	if (ret) {
		dev_err(ili->dev, "can't write POL register (%d)\n", ret);
		return ret;
	}

	/*
	 * Set up interface control.
	 * This is not used in the BT.656 mode (no H/Vsync or DE signals).
	 */
	reg = ili->conf->syncmode;
	reg |= ILI9322_IF_CTRL_LINE_INVERSION;
	ret = regmap_write(ili->regmap, ILI9322_IF_CTRL, reg);
	if (ret) {
		dev_err(ili->dev, "can't write IF CTRL register (%d)\n", ret);
		return ret;
	}

	/* Set up the input mode */
	reg = (ili->input << 4);
	/* These are inverted, setting to 1 is the default, clearing flips */
	if (!ili->conf->flip_horizontal)
		reg |= ILI9322_ENTRY_HDIR;
	if (!ili->conf->flip_vertical)
		reg |= ILI9322_ENTRY_VDIR;
	reg |= ILI9322_ENTRY_AUTODETECT;
	ret = regmap_write(ili->regmap, ILI9322_ENTRY, reg);
	if (ret) {
		dev_err(ili->dev, "can't write ENTRY reg (%d)\n", ret);
		return ret;
	}
	dev_info(ili->dev, "display is in %s mode, syncmode %02x\n",
		 ili9322_inputs[ili->input],
		 ili->conf->syncmode);

	dev_info(ili->dev, "initialized display\n");

	return 0;
}

/*
 * This power-on sequence if from the datasheet, page 57.
 */
static int ili9322_power_on(struct ili9322 *ili)
{
	int ret;

	/* Assert RESET */
	gpiod_set_value(ili->reset_gpio, 1);

	ret = regulator_bulk_enable(ARRAY_SIZE(ili->supplies), ili->supplies);
	if (ret < 0) {
		dev_err(ili->dev, "unable to enable regulators\n");
		return ret;
	}
	msleep(20);

	/* De-assert RESET */
	gpiod_set_value(ili->reset_gpio, 0);

	msleep(10);

	return 0;
}

static int ili9322_power_off(struct ili9322 *ili)
{
	return regulator_bulk_disable(ARRAY_SIZE(ili->supplies), ili->supplies);
}

static int ili9322_disable(struct drm_panel *panel)
{
	struct ili9322 *ili = panel_to_ili9322(panel);
	int ret;

	ret = regmap_write(ili->regmap, ILI9322_POW_CTRL,
			   ILI9322_POW_CTRL_STANDBY);
	if (ret) {
		dev_err(ili->dev, "unable to go to standby mode\n");
		return ret;
	}

	return 0;
}

static int ili9322_unprepare(struct drm_panel *panel)
{
	struct ili9322 *ili = panel_to_ili9322(panel);

	return ili9322_power_off(ili);
}

static int ili9322_prepare(struct drm_panel *panel)
{
	struct ili9322 *ili = panel_to_ili9322(panel);
	int ret;

	ret = ili9322_power_on(ili);
	if (ret < 0)
		return ret;

	ret = ili9322_init(panel, ili);
	if (ret < 0)
		ili9322_unprepare(panel);

	return ret;
}

static int ili9322_enable(struct drm_panel *panel)
{
	struct ili9322 *ili = panel_to_ili9322(panel);
	int ret;

	ret = regmap_write(ili->regmap, ILI9322_POW_CTRL,
			   ILI9322_POW_CTRL_DEFAULT);
	if (ret) {
		dev_err(ili->dev, "unable to enable panel\n");
		return ret;
	}

	return 0;
}

/* Serial RGB modes */
static const struct drm_display_mode srgb_320x240_mode = {
	.clock = 24535,
	.hdisplay = 320,
	.hsync_start = 320 + 359,
	.hsync_end = 320 + 359 + 1,
	.htotal = 320 + 359 + 1 + 241,
	.vdisplay = 240,
	.vsync_start = 240 + 4,
	.vsync_end = 240 + 4 + 1,
	.vtotal = 262,
	.flags = 0,
};

static const struct drm_display_mode srgb_360x240_mode = {
	.clock = 27000,
	.hdisplay = 360,
	.hsync_start = 360 + 35,
	.hsync_end = 360 + 35 + 1,
	.htotal = 360 + 35 + 1 + 241,
	.vdisplay = 240,
	.vsync_start = 240 + 21,
	.vsync_end = 240 + 21 + 1,
	.vtotal = 262,
	.flags = 0,
};

/* This is the only mode listed for parallel RGB in the datasheet */
static const struct drm_display_mode prgb_320x240_mode = {
	.clock = 64000,
	.hdisplay = 320,
	.hsync_start = 320 + 38,
	.hsync_end = 320 + 38 + 1,
	.htotal = 320 + 38 + 1 + 50,
	.vdisplay = 240,
	.vsync_start = 240 + 4,
	.vsync_end = 240 + 4 + 1,
	.vtotal = 262,
	.flags = 0,
};

/* YUV modes */
static const struct drm_display_mode yuv_640x320_mode = {
	.clock = 24540,
	.hdisplay = 640,
	.hsync_start = 640 + 252,
	.hsync_end = 640 + 252 + 1,
	.htotal = 640 + 252 + 1 + 28,
	.vdisplay = 320,
	.vsync_start = 320 + 4,
	.vsync_end = 320 + 4 + 1,
	.vtotal = 320 + 4 + 1 + 18,
	.flags = 0,
};

static const struct drm_display_mode yuv_720x360_mode = {
	.clock = 27000,
	.hdisplay = 720,
	.hsync_start = 720 + 252,
	.hsync_end = 720 + 252 + 1,
	.htotal = 720 + 252 + 1 + 24,
	.vdisplay = 360,
	.vsync_start = 360 + 4,
	.vsync_end = 360 + 4 + 1,
	.vtotal = 360 + 4 + 1 + 18,
	.flags = 0,
};

/* BT.656 VGA mode, 640x480 */
static const struct drm_display_mode itu_r_bt_656_640_mode = {
	.clock = 24540,
	.hdisplay = 640,
	.hsync_start = 640 + 3,
	.hsync_end = 640 + 3 + 1,
	.htotal = 640 + 3 + 1 + 272,
	.vdisplay = 480,
	.vsync_start = 480 + 4,
	.vsync_end = 480 + 4 + 1,
	.vtotal = 500,
	.flags = 0,
};

/* BT.656 D1 mode 720x480 */
static const struct drm_display_mode itu_r_bt_656_720_mode = {
	.clock = 27000,
	.hdisplay = 720,
	.hsync_start = 720 + 3,
	.hsync_end = 720 + 3 + 1,
	.htotal = 720 + 3 + 1 + 272,
	.vdisplay = 480,
	.vsync_start = 480 + 4,
	.vsync_end = 480 + 4 + 1,
	.vtotal = 500,
	.flags = 0,
};

static int ili9322_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct ili9322 *ili = panel_to_ili9322(panel);
	struct drm_device *drm = connector->dev;
	struct drm_display_mode *mode;
	struct drm_display_info *info;

	info = &connector->display_info;
	info->width_mm = ili->conf->width_mm;
	info->height_mm = ili->conf->height_mm;
	if (ili->conf->dclk_active_high)
		info->bus_flags |= DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE;
	else
		info->bus_flags |= DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE;

	if (ili->conf->de_active_high)
		info->bus_flags |= DRM_BUS_FLAG_DE_HIGH;
	else
		info->bus_flags |= DRM_BUS_FLAG_DE_LOW;

	switch (ili->input) {
	case ILI9322_INPUT_SRGB_DUMMY_320X240:
		mode = drm_mode_duplicate(drm, &srgb_320x240_mode);
		break;
	case ILI9322_INPUT_SRGB_DUMMY_360X240:
		mode = drm_mode_duplicate(drm, &srgb_360x240_mode);
		break;
	case ILI9322_INPUT_PRGB_THROUGH:
	case ILI9322_INPUT_PRGB_ALIGNED:
		mode = drm_mode_duplicate(drm, &prgb_320x240_mode);
		break;
	case ILI9322_INPUT_YUV_640X320_YCBCR:
		mode = drm_mode_duplicate(drm, &yuv_640x320_mode);
		break;
	case ILI9322_INPUT_YUV_720X360_YCBCR:
		mode = drm_mode_duplicate(drm, &yuv_720x360_mode);
		break;
	case ILI9322_INPUT_ITU_R_BT656_720X360_YCBCR:
		mode = drm_mode_duplicate(drm, &itu_r_bt_656_720_mode);
		break;
	case ILI9322_INPUT_ITU_R_BT656_640X320_YCBCR:
		mode = drm_mode_duplicate(drm, &itu_r_bt_656_640_mode);
		break;
	default:
		mode = NULL;
		break;
	}
	if (!mode) {
		dev_err(panel->dev, "bad mode or failed to add mode\n");
		return -EINVAL;
	}
	drm_mode_set_name(mode);
	/*
	 * This is the preferred mode because most people are going
	 * to want to use the display with VGA type graphics.
	 */
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	/* Set up the polarity */
	if (ili->conf->hsync_active_high)
		mode->flags |= DRM_MODE_FLAG_PHSYNC;
	else
		mode->flags |= DRM_MODE_FLAG_NHSYNC;
	if (ili->conf->vsync_active_high)
		mode->flags |= DRM_MODE_FLAG_PVSYNC;
	else
		mode->flags |= DRM_MODE_FLAG_NVSYNC;

	mode->width_mm = ili->conf->width_mm;
	mode->height_mm = ili->conf->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1; /* Number of modes */
}

static const struct drm_panel_funcs ili9322_drm_funcs = {
	.disable = ili9322_disable,
	.unprepare = ili9322_unprepare,
	.prepare = ili9322_prepare,
	.enable = ili9322_enable,
	.get_modes = ili9322_get_modes,
};

static int ili9322_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct ili9322 *ili;
	const struct regmap_config *regmap_config;
	u8 gamma;
	u32 val;
	int ret;
	int i;

	ili = devm_kzalloc(dev, sizeof(struct ili9322), GFP_KERNEL);
	if (!ili)
		return -ENOMEM;

	spi_set_drvdata(spi, ili);

	ili->dev = dev;

	/*
	 * Every new incarnation of this display must have a unique
	 * data entry for the system in this driver.
	 */
	ili->conf = of_device_get_match_data(dev);
	if (!ili->conf) {
		dev_err(dev, "missing device configuration\n");
		return -ENODEV;
	}

	val = ili->conf->vreg1out_mv;
	if (!val) {
		/* Default HW value, do not touch (should be 4.5V) */
		ili->vreg1out = U8_MAX;
	} else {
		if (val < 3600) {
			dev_err(dev, "too low VREG1OUT\n");
			return -EINVAL;
		}
		if (val > 6000) {
			dev_err(dev, "too high VREG1OUT\n");
			return -EINVAL;
		}
		if ((val % 100) != 0) {
			dev_err(dev, "VREG1OUT is no even 100 microvolt\n");
			return -EINVAL;
		}
		val -= 3600;
		val /= 100;
		dev_dbg(dev, "VREG1OUT = 0x%02x\n", val);
		ili->vreg1out = val;
	}

	val = ili->conf->vcom_high_percent;
	if (!val) {
		/* Default HW value, do not touch (should be 91%) */
		ili->vcom_high = U8_MAX;
	} else {
		if (val < 37) {
			dev_err(dev, "too low VCOM high\n");
			return -EINVAL;
		}
		if (val > 100) {
			dev_err(dev, "too high VCOM high\n");
			return -EINVAL;
		}
		val -= 37;
		dev_dbg(dev, "VCOM high = 0x%02x\n", val);
		ili->vcom_high = val;
	}

	val = ili->conf->vcom_amplitude_percent;
	if (!val) {
		/* Default HW value, do not touch (should be 114%) */
		ili->vcom_high = U8_MAX;
	} else {
		if (val < 70) {
			dev_err(dev, "too low VCOM amplitude\n");
			return -EINVAL;
		}
		if (val > 132) {
			dev_err(dev, "too high VCOM amplitude\n");
			return -EINVAL;
		}
		val -= 70;
		val >>= 1; /* Increments of 2% */
		dev_dbg(dev, "VCOM amplitude = 0x%02x\n", val);
		ili->vcom_amplitude = val;
	}

	for (i = 0; i < ARRAY_SIZE(ili->gamma); i++) {
		val = ili->conf->gamma_corr_neg[i];
		if (val > 15) {
			dev_err(dev, "negative gamma %u > 15, capping\n", val);
			val = 15;
		}
		gamma = val << 4;
		val = ili->conf->gamma_corr_pos[i];
		if (val > 15) {
			dev_err(dev, "positive gamma %u > 15, capping\n", val);
			val = 15;
		}
		gamma |= val;
		ili->gamma[i] = gamma;
		dev_dbg(dev, "gamma V%d: 0x%02x\n", i + 1, gamma);
	}

	ili->supplies[0].supply = "vcc"; /* 2.7-3.6 V */
	ili->supplies[1].supply = "iovcc"; /* 1.65-3.6V */
	ili->supplies[2].supply = "vci"; /* 2.7-3.6V */
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ili->supplies),
				      ili->supplies);
	if (ret < 0)
		return ret;
	ret = regulator_set_voltage(ili->supplies[0].consumer,
				    2700000, 3600000);
	if (ret)
		return ret;
	ret = regulator_set_voltage(ili->supplies[1].consumer,
				    1650000, 3600000);
	if (ret)
		return ret;
	ret = regulator_set_voltage(ili->supplies[2].consumer,
				    2700000, 3600000);
	if (ret)
		return ret;

	ili->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ili->reset_gpio)) {
		dev_err(dev, "failed to get RESET GPIO\n");
		return PTR_ERR(ili->reset_gpio);
	}

	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(dev, "spi setup failed.\n");
		return ret;
	}
	regmap_config = &ili9322_regmap_config;
	ili->regmap = devm_regmap_init(dev, &ili9322_regmap_bus, dev,
				       regmap_config);
	if (IS_ERR(ili->regmap)) {
		dev_err(dev, "failed to allocate register map\n");
		return PTR_ERR(ili->regmap);
	}

	ret = regmap_read(ili->regmap, ILI9322_CHIP_ID, &val);
	if (ret) {
		dev_err(dev, "can't get chip ID (%d)\n", ret);
		return ret;
	}
	if (val != ILI9322_CHIP_ID_MAGIC) {
		dev_err(dev, "chip ID 0x%0x2, expected 0x%02x\n", val,
			ILI9322_CHIP_ID_MAGIC);
		return -ENODEV;
	}

	/* Probe the system to find the display setting */
	if (ili->conf->input == ILI9322_INPUT_UNKNOWN) {
		ret = regmap_read(ili->regmap, ILI9322_ENTRY, &val);
		if (ret) {
			dev_err(dev, "can't get entry setting (%d)\n", ret);
			return ret;
		}
		/* Input enum corresponds to HW setting */
		ili->input = (val >> 4) & 0x0f;
		if (ili->input >= ILI9322_INPUT_UNKNOWN)
			ili->input = ILI9322_INPUT_UNKNOWN;
	} else {
		ili->input = ili->conf->input;
	}

	drm_panel_init(&ili->panel, dev, &ili9322_drm_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	drm_panel_add(&ili->panel);

	return 0;
}

static void ili9322_remove(struct spi_device *spi)
{
	struct ili9322 *ili = spi_get_drvdata(spi);

	ili9322_power_off(ili);
	drm_panel_remove(&ili->panel);
}

/*
 * The D-Link DIR-685 panel is marked LM918A01-1A SY-B4-091116-E0199
 */
static const struct ili9322_config ili9322_dir_685 = {
	.width_mm = 65,
	.height_mm = 50,
	.input = ILI9322_INPUT_ITU_R_BT656_640X320_YCBCR,
	.vreg1out_mv = 4600,
	.vcom_high_percent = 91,
	.vcom_amplitude_percent = 114,
	.syncmode = ILI9322_IF_CTRL_SYNC_DISABLED,
	.dclk_active_high = true,
	.gamma_corr_neg = { 0xa, 0x5, 0x7, 0x7, 0x7, 0x5, 0x1, 0x6 },
	.gamma_corr_pos = { 0x7, 0x7, 0x3, 0x2, 0x3, 0x5, 0x7, 0x2 },
};

static const struct of_device_id ili9322_of_match[] = {
	{
		.compatible = "dlink,dir-685-panel",
		.data = &ili9322_dir_685,
	},
	{
		.compatible = "ilitek,ili9322",
		.data = NULL,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, ili9322_of_match);

static struct spi_driver ili9322_driver = {
	.probe = ili9322_probe,
	.remove = ili9322_remove,
	.driver = {
		.name = "panel-ilitek-ili9322",
		.of_match_table = ili9322_of_match,
	},
};
module_spi_driver(ili9322_driver);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("ILI9322 LCD panel driver");
MODULE_LICENSE("GPL v2");
