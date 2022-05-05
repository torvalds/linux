// SPDX-License-Identifier: GPL-2.0
/*
 * Asia Better Technology Ltd. Y030XX067A IPS LCD panel driver
 *
 * Copyright (C) 2020, Paul Cercueil <paul@crapouillou.net>
 * Copyright (C) 2020, Christophe Branchereau <cbranchereau@gmail.com>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define REG00_VBRT_CTRL(val)		(val)

#define REG01_COM_DC(val)		(val)

#define REG02_DA_CONTRAST(val)		(val)
#define REG02_VESA_SEL(val)		((val) << 5)
#define REG02_COMDC_SW			BIT(7)

#define REG03_VPOSITION(val)		(val)
#define REG03_BSMOUNT			BIT(5)
#define REG03_COMTST			BIT(6)
#define REG03_HPOSITION1		BIT(7)

#define REG04_HPOSITION1(val)		(val)

#define REG05_CLIP			BIT(0)
#define REG05_NVM_VREFRESH		BIT(1)
#define REG05_SLFR			BIT(2)
#define REG05_SLBRCHARGE(val)		((val) << 3)
#define REG05_PRECHARGE_LEVEL(val)	((val) << 6)

#define REG06_TEST5			BIT(0)
#define REG06_SLDWN			BIT(1)
#define REG06_SLRGT			BIT(2)
#define REG06_TEST2			BIT(3)
#define REG06_XPSAVE			BIT(4)
#define REG06_GAMMA_SEL(val)		((val) << 5)
#define REG06_NT			BIT(7)

#define REG07_TEST1			BIT(0)
#define REG07_HDVD_POL			BIT(1)
#define REG07_CK_POL			BIT(2)
#define REG07_TEST3			BIT(3)
#define REG07_TEST4			BIT(4)
#define REG07_480_LINEMASK		BIT(5)
#define REG07_AMPTST(val)		((val) << 6)

#define REG08_SLHRC(val)		(val)
#define REG08_CLOCK_DIV(val)		((val) << 2)
#define REG08_PANEL(val)		((val) << 5)

#define REG09_SUB_BRIGHT_R(val)		(val)
#define REG09_NW_NB			BIT(6)
#define REG09_IPCON			BIT(7)

#define REG0A_SUB_BRIGHT_B(val)		(val)
#define REG0A_PAIR			BIT(6)
#define REG0A_DE_SEL			BIT(7)

#define REG0B_MBK_POSITION(val)		(val)
#define REG0B_HD_FREERUN		BIT(4)
#define REG0B_VD_FREERUN		BIT(5)
#define REG0B_YUV2BIN(val)		((val) << 6)

#define REG0C_CONTRAST_R(val)		(val)
#define REG0C_DOUBLEREAD		BIT(7)

#define REG0D_CONTRAST_G(val)		(val)
#define REG0D_RGB_YUV			BIT(7)

#define REG0E_CONTRAST_B(val)		(val)
#define REG0E_PIXELCOLORDRIVE		BIT(7)

#define REG0F_ASPECT			BIT(0)
#define REG0F_OVERSCAN(val)		((val) << 1)
#define REG0F_FRAMEWIDTH(val)		((val) << 3)

#define REG10_BRIGHT(val)		(val)

#define REG11_SIG_GAIN(val)		(val)
#define REG11_SIGC_CNTL			BIT(6)
#define REG11_SIGC_POL			BIT(7)

#define REG12_COLOR(val)		(val)
#define REG12_PWCKSEL(val)		((val) << 6)

#define REG13_4096LEVEL_CNTL(val)	(val)
#define REG13_SL4096(val)		((val) << 4)
#define REG13_LIMITER_CONTROL		BIT(7)

#define REG14_PANEL_TEST(val)		(val)

#define REG15_NVM_LINK0			BIT(0)
#define REG15_NVM_LINK1			BIT(1)
#define REG15_NVM_LINK2			BIT(2)
#define REG15_NVM_LINK3			BIT(3)
#define REG15_NVM_LINK4			BIT(4)
#define REG15_NVM_LINK5			BIT(5)
#define REG15_NVM_LINK6			BIT(6)
#define REG15_NVM_LINK7			BIT(7)

struct y030xx067a_info {
	const struct drm_display_mode *display_modes;
	unsigned int num_modes;
	u16 width_mm, height_mm;
	u32 bus_format, bus_flags;
};

struct y030xx067a {
	struct drm_panel panel;
	struct spi_device *spi;
	struct regmap *map;

	const struct y030xx067a_info *panel_info;

	struct regulator *supply;
	struct gpio_desc *reset_gpio;
};

static inline struct y030xx067a *to_y030xx067a(struct drm_panel *panel)
{
	return container_of(panel, struct y030xx067a, panel);
}

static const struct reg_sequence y030xx067a_init_sequence[] = {
	{ 0x00, REG00_VBRT_CTRL(0x7f) },
	{ 0x01, REG01_COM_DC(0x3c) },
	{ 0x02, REG02_VESA_SEL(0x3) | REG02_DA_CONTRAST(0x1f) },
	{ 0x03, REG03_VPOSITION(0x0a) },
	{ 0x04, REG04_HPOSITION1(0xd2) },
	{ 0x05, REG05_CLIP | REG05_NVM_VREFRESH | REG05_SLBRCHARGE(0x2) },
	{ 0x06, REG06_XPSAVE | REG06_NT },
	{ 0x07, 0 },
	{ 0x08, REG08_PANEL(0x1) | REG08_CLOCK_DIV(0x2) },
	{ 0x09, REG09_SUB_BRIGHT_R(0x20) },
	{ 0x0a, REG0A_SUB_BRIGHT_B(0x20) },
	{ 0x0b, REG0B_HD_FREERUN | REG0B_VD_FREERUN },
	{ 0x0c, REG0C_CONTRAST_R(0x00) },
	{ 0x0d, REG0D_CONTRAST_G(0x00) },
	{ 0x0e, REG0E_CONTRAST_B(0x10) },
	{ 0x0f, 0 },
	{ 0x10, REG10_BRIGHT(0x7f) },
	{ 0x11, REG11_SIGC_CNTL | REG11_SIG_GAIN(0x3f) },
	{ 0x12, REG12_COLOR(0x20) | REG12_PWCKSEL(0x1) },
	{ 0x13, REG13_4096LEVEL_CNTL(0x8) },
	{ 0x14, 0 },
	{ 0x15, 0 },
};

static int y030xx067a_prepare(struct drm_panel *panel)
{
	struct y030xx067a *priv = to_y030xx067a(panel);
	struct device *dev = &priv->spi->dev;
	int err;

	err = regulator_enable(priv->supply);
	if (err) {
		dev_err(dev, "Failed to enable power supply: %d\n", err);
		return err;
	}

	/* Reset the chip */
	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	usleep_range(1000, 20000);
	gpiod_set_value_cansleep(priv->reset_gpio, 0);
	usleep_range(1000, 20000);

	err = regmap_multi_reg_write(priv->map, y030xx067a_init_sequence,
				     ARRAY_SIZE(y030xx067a_init_sequence));
	if (err) {
		dev_err(dev, "Failed to init registers: %d\n", err);
		goto err_disable_regulator;
	}

	msleep(120);

	return 0;

err_disable_regulator:
	regulator_disable(priv->supply);
	return err;
}

static int y030xx067a_unprepare(struct drm_panel *panel)
{
	struct y030xx067a *priv = to_y030xx067a(panel);

	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	regulator_disable(priv->supply);

	return 0;
}

static int y030xx067a_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	struct y030xx067a *priv = to_y030xx067a(panel);
	const struct y030xx067a_info *panel_info = priv->panel_info;
	struct drm_display_mode *mode;
	unsigned int i;

	for (i = 0; i < panel_info->num_modes; i++) {
		mode = drm_mode_duplicate(connector->dev,
					  &panel_info->display_modes[i]);
		if (!mode)
			return -ENOMEM;

		drm_mode_set_name(mode);

		mode->type = DRM_MODE_TYPE_DRIVER;
		if (panel_info->num_modes == 1)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_probed_add(connector, mode);
	}

	connector->display_info.bpc = 8;
	connector->display_info.width_mm = panel_info->width_mm;
	connector->display_info.height_mm = panel_info->height_mm;

	drm_display_info_set_bus_formats(&connector->display_info,
					 &panel_info->bus_format, 1);
	connector->display_info.bus_flags = panel_info->bus_flags;

	return panel_info->num_modes;
}

static const struct drm_panel_funcs y030xx067a_funcs = {
	.prepare	= y030xx067a_prepare,
	.unprepare	= y030xx067a_unprepare,
	.get_modes	= y030xx067a_get_modes,
};

static const struct regmap_config y030xx067a_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x15,
};

static int y030xx067a_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct y030xx067a *priv;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->spi = spi;
	spi_set_drvdata(spi, priv);

	priv->map = devm_regmap_init_spi(spi, &y030xx067a_regmap_config);
	if (IS_ERR(priv->map)) {
		dev_err(dev, "Unable to init regmap\n");
		return PTR_ERR(priv->map);
	}

	priv->panel_info = of_device_get_match_data(dev);
	if (!priv->panel_info)
		return -EINVAL;

	priv->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(priv->supply))
		return dev_err_probe(dev, PTR_ERR(priv->supply),
				     "Failed to get power supply\n");

	priv->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(priv->reset_gpio),
				     "Failed to get reset GPIO\n");

	drm_panel_init(&priv->panel, dev, &y030xx067a_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	err = drm_panel_of_backlight(&priv->panel);
	if (err)
		return err;

	drm_panel_add(&priv->panel);

	return 0;
}

static void y030xx067a_remove(struct spi_device *spi)
{
	struct y030xx067a *priv = spi_get_drvdata(spi);

	drm_panel_remove(&priv->panel);
	drm_panel_disable(&priv->panel);
	drm_panel_unprepare(&priv->panel);
}

static const struct drm_display_mode y030xx067a_modes[] = {
	{ /* 60 Hz */
		.clock = 14400,
		.hdisplay = 320,
		.hsync_start = 320 + 10,
		.hsync_end = 320 + 10 + 37,
		.htotal = 320 + 10 + 37 + 33,
		.vdisplay = 480,
		.vsync_start = 480 + 84,
		.vsync_end = 480 + 84 + 20,
		.vtotal = 480 + 84 + 20 + 16,
		.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
	{ /* 50 Hz */
		.clock = 12000,
		.hdisplay = 320,
		.hsync_start = 320 + 10,
		.hsync_end = 320 + 10 + 37,
		.htotal = 320 + 10 + 37 + 33,
		.vdisplay = 480,
		.vsync_start = 480 + 84,
		.vsync_end = 480 + 84 + 20,
		.vtotal = 480 + 84 + 20 + 16,
		.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
};

static const struct y030xx067a_info y030xx067a_info = {
	.display_modes = y030xx067a_modes,
	.num_modes = ARRAY_SIZE(y030xx067a_modes),
	.width_mm = 69,
	.height_mm = 51,
	.bus_format = MEDIA_BUS_FMT_RGB888_3X8_DELTA,
	.bus_flags = DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE | DRM_BUS_FLAG_DE_LOW,
};

static const struct of_device_id y030xx067a_of_match[] = {
	{ .compatible = "abt,y030xx067a", .data = &y030xx067a_info },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, y030xx067a_of_match);

static struct spi_driver y030xx067a_driver = {
	.driver = {
		.name = "abt-y030xx067a",
		.of_match_table = y030xx067a_of_match,
	},
	.probe = y030xx067a_probe,
	.remove = y030xx067a_remove,
};
module_spi_driver(y030xx067a_driver);

MODULE_AUTHOR("Paul Cercueil <paul@crapouillou.net>");
MODULE_AUTHOR("Christophe Branchereau <cbranchereau@gmail.com>");
MODULE_LICENSE("GPL v2");
