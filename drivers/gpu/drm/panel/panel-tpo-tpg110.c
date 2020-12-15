// SPDX-License-Identifier: GPL-2.0
/*
 * Panel driver for the TPO TPG110 400CH LTPS TFT LCD Single Chip
 * Digital Driver.
 *
 * This chip drives a TFT LCD, so it does not know what kind of
 * display is actually connected to it, so the width and height of that
 * display needs to be supplied from the machine configuration.
 *
 * Author:
 * Linus Walleij <linus.walleij@linaro.org>
 */
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

#define TPG110_TEST			0x00
#define TPG110_CHIPID			0x01
#define TPG110_CTRL1			0x02
#define TPG110_RES_MASK			GENMASK(2, 0)
#define TPG110_RES_800X480		0x07
#define TPG110_RES_640X480		0x06
#define TPG110_RES_480X272		0x05
#define TPG110_RES_480X640		0x04
#define TPG110_RES_480X272_D		0x01 /* Dual scan: outputs 800x480 */
#define TPG110_RES_400X240_D		0x00 /* Dual scan: outputs 800x480 */
#define TPG110_CTRL2			0x03
#define TPG110_CTRL2_PM			BIT(0)
#define TPG110_CTRL2_RES_PM_CTRL	BIT(7)

/**
 * struct tpg110_panel_mode - lookup struct for the supported modes
 */
struct tpg110_panel_mode {
	/**
	 * @name: the name of this panel
	 */
	const char *name;
	/**
	 * @magic: the magic value from the detection register
	 */
	u32 magic;
	/**
	 * @mode: the DRM display mode for this panel
	 */
	struct drm_display_mode mode;
	/**
	 * @bus_flags: the DRM bus flags for this panel e.g. inverted clock
	 */
	u32 bus_flags;
};

/**
 * struct tpg110 - state container for the TPG110 panel
 */
struct tpg110 {
	/**
	 * @dev: the container device
	 */
	struct device *dev;
	/**
	 * @spi: the corresponding SPI device
	 */
	struct spi_device *spi;
	/**
	 * @panel: the DRM panel instance for this device
	 */
	struct drm_panel panel;
	/**
	 * @panel_type: the panel mode as detected
	 */
	const struct tpg110_panel_mode *panel_mode;
	/**
	 * @width: the width of this panel in mm
	 */
	u32 width;
	/**
	 * @height: the height of this panel in mm
	 */
	u32 height;
	/**
	 * @grestb: reset GPIO line
	 */
	struct gpio_desc *grestb;
};

/*
 * TPG110 modes, these are the simple modes, the dualscan modes that
 * take 400x240 or 480x272 in and display as 800x480 are not listed.
 */
static const struct tpg110_panel_mode tpg110_modes[] = {
	{
		.name = "800x480 RGB",
		.magic = TPG110_RES_800X480,
		.mode = {
			.clock = 33200,
			.hdisplay = 800,
			.hsync_start = 800 + 40,
			.hsync_end = 800 + 40 + 1,
			.htotal = 800 + 40 + 1 + 216,
			.vdisplay = 480,
			.vsync_start = 480 + 10,
			.vsync_end = 480 + 10 + 1,
			.vtotal = 480 + 10 + 1 + 35,
		},
		.bus_flags = DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE,
	},
	{
		.name = "640x480 RGB",
		.magic = TPG110_RES_640X480,
		.mode = {
			.clock = 25200,
			.hdisplay = 640,
			.hsync_start = 640 + 24,
			.hsync_end = 640 + 24 + 1,
			.htotal = 640 + 24 + 1 + 136,
			.vdisplay = 480,
			.vsync_start = 480 + 18,
			.vsync_end = 480 + 18 + 1,
			.vtotal = 480 + 18 + 1 + 27,
		},
		.bus_flags = DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE,
	},
	{
		.name = "480x272 RGB",
		.magic = TPG110_RES_480X272,
		.mode = {
			.clock = 9000,
			.hdisplay = 480,
			.hsync_start = 480 + 2,
			.hsync_end = 480 + 2 + 1,
			.htotal = 480 + 2 + 1 + 43,
			.vdisplay = 272,
			.vsync_start = 272 + 2,
			.vsync_end = 272 + 2 + 1,
			.vtotal = 272 + 2 + 1 + 12,
		},
		.bus_flags = DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE,
	},
	{
		.name = "480x640 RGB",
		.magic = TPG110_RES_480X640,
		.mode = {
			.clock = 20500,
			.hdisplay = 480,
			.hsync_start = 480 + 2,
			.hsync_end = 480 + 2 + 1,
			.htotal = 480 + 2 + 1 + 43,
			.vdisplay = 640,
			.vsync_start = 640 + 4,
			.vsync_end = 640 + 4 + 1,
			.vtotal = 640 + 4 + 1 + 8,
		},
		.bus_flags = DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE,
	},
	{
		.name = "400x240 RGB",
		.magic = TPG110_RES_400X240_D,
		.mode = {
			.clock = 8300,
			.hdisplay = 400,
			.hsync_start = 400 + 20,
			.hsync_end = 400 + 20 + 1,
			.htotal = 400 + 20 + 1 + 108,
			.vdisplay = 240,
			.vsync_start = 240 + 2,
			.vsync_end = 240 + 2 + 1,
			.vtotal = 240 + 2 + 1 + 20,
		},
		.bus_flags = DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE,
	},
};

static inline struct tpg110 *
to_tpg110(struct drm_panel *panel)
{
	return container_of(panel, struct tpg110, panel);
}

static u8 tpg110_readwrite_reg(struct tpg110 *tpg, bool write,
			       u8 address, u8 outval)
{
	struct spi_message m;
	struct spi_transfer t[2];
	u8 buf[2];
	int ret;

	spi_message_init(&m);
	memset(t, 0, sizeof(t));

	if (write) {
		/*
		 * Clear address bit 0, 1 when writing, just to be sure
		 * The actual bit indicating a write here is bit 1, bit
		 * 0 is just surplus to pad it up to 8 bits.
		 */
		buf[0] = address << 2;
		buf[0] &= ~0x03;
		buf[1] = outval;

		t[0].bits_per_word = 8;
		t[0].tx_buf = &buf[0];
		t[0].len = 1;

		t[1].tx_buf = &buf[1];
		t[1].len = 1;
		t[1].bits_per_word = 8;
	} else {
		/* Set address bit 0 to 1 to read */
		buf[0] = address << 1;
		buf[0] |= 0x01;

		/*
		 * The last bit/clock is Hi-Z turnaround cycle, so we need
		 * to send only 7 bits here. The 8th bit is the high impedance
		 * turn-around cycle.
		 */
		t[0].bits_per_word = 7;
		t[0].tx_buf = &buf[0];
		t[0].len = 1;

		t[1].rx_buf = &buf[1];
		t[1].len = 1;
		t[1].bits_per_word = 8;
	}

	spi_message_add_tail(&t[0], &m);
	spi_message_add_tail(&t[1], &m);
	ret = spi_sync(tpg->spi, &m);
	if (ret) {
		DRM_DEV_ERROR(tpg->dev, "SPI message error %d\n", ret);
		return ret;
	}
	if (write)
		return 0;
	/* Read */
	return buf[1];
}

static u8 tpg110_read_reg(struct tpg110 *tpg, u8 address)
{
	return tpg110_readwrite_reg(tpg, false, address, 0);
}

static void tpg110_write_reg(struct tpg110 *tpg, u8 address, u8 outval)
{
	tpg110_readwrite_reg(tpg, true, address, outval);
}

static int tpg110_startup(struct tpg110 *tpg)
{
	u8 val;
	int i;

	/* De-assert the reset signal */
	gpiod_set_value_cansleep(tpg->grestb, 0);
	usleep_range(1000, 2000);
	DRM_DEV_DEBUG(tpg->dev, "de-asserted GRESTB\n");

	/* Test display communication */
	tpg110_write_reg(tpg, TPG110_TEST, 0x55);
	val = tpg110_read_reg(tpg, TPG110_TEST);
	if (val != 0x55) {
		DRM_DEV_ERROR(tpg->dev, "failed communication test\n");
		return -ENODEV;
	}

	val = tpg110_read_reg(tpg, TPG110_CHIPID);
	DRM_DEV_INFO(tpg->dev, "TPG110 chip ID: %d version: %d\n",
		 val >> 4, val & 0x0f);

	/* Show display resolution */
	val = tpg110_read_reg(tpg, TPG110_CTRL1);
	val &= TPG110_RES_MASK;
	switch (val) {
	case TPG110_RES_400X240_D:
		DRM_DEV_INFO(tpg->dev,
			 "IN 400x240 RGB -> OUT 800x480 RGB (dual scan)\n");
		break;
	case TPG110_RES_480X272_D:
		DRM_DEV_INFO(tpg->dev,
			 "IN 480x272 RGB -> OUT 800x480 RGB (dual scan)\n");
		break;
	case TPG110_RES_480X640:
		DRM_DEV_INFO(tpg->dev, "480x640 RGB\n");
		break;
	case TPG110_RES_480X272:
		DRM_DEV_INFO(tpg->dev, "480x272 RGB\n");
		break;
	case TPG110_RES_640X480:
		DRM_DEV_INFO(tpg->dev, "640x480 RGB\n");
		break;
	case TPG110_RES_800X480:
		DRM_DEV_INFO(tpg->dev, "800x480 RGB\n");
		break;
	default:
		DRM_DEV_ERROR(tpg->dev, "ILLEGAL RESOLUTION 0x%02x\n", val);
		break;
	}

	/* From the producer side, this is the same resolution */
	if (val == TPG110_RES_480X272_D)
		val = TPG110_RES_480X272;

	for (i = 0; i < ARRAY_SIZE(tpg110_modes); i++) {
		const struct tpg110_panel_mode *pm;

		pm = &tpg110_modes[i];
		if (pm->magic == val) {
			tpg->panel_mode = pm;
			break;
		}
	}
	if (i == ARRAY_SIZE(tpg110_modes)) {
		DRM_DEV_ERROR(tpg->dev, "unsupported mode (%02x) detected\n",
			val);
		return -ENODEV;
	}

	val = tpg110_read_reg(tpg, TPG110_CTRL2);
	DRM_DEV_INFO(tpg->dev, "resolution and standby is controlled by %s\n",
		 (val & TPG110_CTRL2_RES_PM_CTRL) ? "software" : "hardware");
	/* Take control over resolution and standby */
	val |= TPG110_CTRL2_RES_PM_CTRL;
	tpg110_write_reg(tpg, TPG110_CTRL2, val);

	return 0;
}

static int tpg110_disable(struct drm_panel *panel)
{
	struct tpg110 *tpg = to_tpg110(panel);
	u8 val;

	/* Put chip into standby */
	val = tpg110_read_reg(tpg, TPG110_CTRL2_PM);
	val &= ~TPG110_CTRL2_PM;
	tpg110_write_reg(tpg, TPG110_CTRL2_PM, val);

	return 0;
}

static int tpg110_enable(struct drm_panel *panel)
{
	struct tpg110 *tpg = to_tpg110(panel);
	u8 val;

	/* Take chip out of standby */
	val = tpg110_read_reg(tpg, TPG110_CTRL2_PM);
	val |= TPG110_CTRL2_PM;
	tpg110_write_reg(tpg, TPG110_CTRL2_PM, val);

	return 0;
}

/**
 * tpg110_get_modes() - return the appropriate mode
 * @panel: the panel to get the mode for
 *
 * This currently does not present a forest of modes, instead it
 * presents the mode that is configured for the system under use,
 * and which is detected by reading the registers of the display.
 */
static int tpg110_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct tpg110 *tpg = to_tpg110(panel);
	struct drm_display_mode *mode;

	connector->display_info.width_mm = tpg->width;
	connector->display_info.height_mm = tpg->height;
	connector->display_info.bus_flags = tpg->panel_mode->bus_flags;

	mode = drm_mode_duplicate(connector->dev, &tpg->panel_mode->mode);
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	mode->width_mm = tpg->width;
	mode->height_mm = tpg->height;

	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs tpg110_drm_funcs = {
	.disable = tpg110_disable,
	.enable = tpg110_enable,
	.get_modes = tpg110_get_modes,
};

static int tpg110_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct device_node *np = dev->of_node;
	struct tpg110 *tpg;
	int ret;

	tpg = devm_kzalloc(dev, sizeof(*tpg), GFP_KERNEL);
	if (!tpg)
		return -ENOMEM;
	tpg->dev = dev;

	/* We get the physical display dimensions from the DT */
	ret = of_property_read_u32(np, "width-mm", &tpg->width);
	if (ret)
		DRM_DEV_ERROR(dev, "no panel width specified\n");
	ret = of_property_read_u32(np, "height-mm", &tpg->height);
	if (ret)
		DRM_DEV_ERROR(dev, "no panel height specified\n");

	/* This asserts the GRESTB signal, putting the display into reset */
	tpg->grestb = devm_gpiod_get(dev, "grestb", GPIOD_OUT_HIGH);
	if (IS_ERR(tpg->grestb)) {
		DRM_DEV_ERROR(dev, "no GRESTB GPIO\n");
		return -ENODEV;
	}

	spi->bits_per_word = 8;
	spi->mode |= SPI_3WIRE_HIZ;
	ret = spi_setup(spi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "spi setup failed.\n");
		return ret;
	}
	tpg->spi = spi;

	ret = tpg110_startup(tpg);
	if (ret)
		return ret;

	drm_panel_init(&tpg->panel, dev, &tpg110_drm_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	ret = drm_panel_of_backlight(&tpg->panel);
	if (ret)
		return ret;

	spi_set_drvdata(spi, tpg);

	return drm_panel_add(&tpg->panel);
}

static int tpg110_remove(struct spi_device *spi)
{
	struct tpg110 *tpg = spi_get_drvdata(spi);

	drm_panel_remove(&tpg->panel);
	return 0;
}

static const struct of_device_id tpg110_match[] = {
	{ .compatible = "tpo,tpg110", },
	{},
};
MODULE_DEVICE_TABLE(of, tpg110_match);

static struct spi_driver tpg110_driver = {
	.probe		= tpg110_probe,
	.remove		= tpg110_remove,
	.driver		= {
		.name	= "tpo-tpg110-panel",
		.of_match_table = tpg110_match,
	},
};
module_spi_driver(tpg110_driver);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("TPO TPG110 panel driver");
MODULE_LICENSE("GPL v2");
