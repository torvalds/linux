// SPDX-License-Identifier: GPL-2.0
/*
 * S6E63M0 AMOLED LCD drm_panel driver.
 *
 * Copyright (C) 2019 Paweł Chmiel <pawel.mikolaj.chmiel@gmail.com>
 * Derived from drivers/gpu/drm/panel-samsung-ld9040.c
 *
 * Andrzej Hajda <a.hajda@samsung.com>
 */

#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <video/mipi_display.h>

/* Manufacturer Command Set */
#define MCS_ELVSS_ON                0xb1
#define MCS_MIECTL1                0xc0
#define MCS_BCMODE                              0xc1
#define MCS_DISCTL   0xf2
#define MCS_SRCCTL           0xf6
#define MCS_IFCTL                       0xf7
#define MCS_PANELCTL         0xF8
#define MCS_PGAMMACTL                   0xfa

#define NUM_GAMMA_LEVELS             11
#define GAMMA_TABLE_COUNT           23

#define DATA_MASK                                       0x100

#define MAX_BRIGHTNESS              (NUM_GAMMA_LEVELS - 1)

/* array of gamma tables for gamma value 2.2 */
static u8 const s6e63m0_gamma_22[NUM_GAMMA_LEVELS][GAMMA_TABLE_COUNT] = {
	{ MCS_PGAMMACTL, 0x00,
	  0x18, 0x08, 0x24, 0x78, 0xEC, 0x3D, 0xC8,
	  0xC2, 0xB6, 0xC4, 0xC7, 0xB6, 0xD5, 0xD7,
	  0xCC, 0x00, 0x39, 0x00, 0x36, 0x00, 0x51 },
	{ MCS_PGAMMACTL, 0x00,
	  0x18, 0x08, 0x24, 0x73, 0x4A, 0x3D, 0xC0,
	  0xC2, 0xB1, 0xBB, 0xBE, 0xAC, 0xCE, 0xCF,
	  0xC5, 0x00, 0x5D, 0x00, 0x5E, 0x00, 0x82 },
	{ MCS_PGAMMACTL, 0x00,
	  0x18, 0x08, 0x24, 0x70, 0x51, 0x3E, 0xBF,
	  0xC1, 0xAF, 0xB9, 0xBC, 0xAB, 0xCC, 0xCC,
	  0xC2, 0x00, 0x65, 0x00, 0x67, 0x00, 0x8D },
	{ MCS_PGAMMACTL, 0x00,
	  0x18, 0x08, 0x24, 0x6C, 0x54, 0x3A, 0xBC,
	  0xBF, 0xAC, 0xB7, 0xBB, 0xA9, 0xC9, 0xC9,
	  0xBE, 0x00, 0x71, 0x00, 0x73, 0x00, 0x9E },
	{ MCS_PGAMMACTL, 0x00,
	  0x18, 0x08, 0x24, 0x69, 0x54, 0x37, 0xBB,
	  0xBE, 0xAC, 0xB4, 0xB7, 0xA6, 0xC7, 0xC8,
	  0xBC, 0x00, 0x7B, 0x00, 0x7E, 0x00, 0xAB },
	{ MCS_PGAMMACTL, 0x00,
	  0x18, 0x08, 0x24, 0x66, 0x55, 0x34, 0xBA,
	  0xBD, 0xAB, 0xB1, 0xB5, 0xA3, 0xC5, 0xC6,
	  0xB9, 0x00, 0x85, 0x00, 0x88, 0x00, 0xBA },
	{ MCS_PGAMMACTL, 0x00,
	  0x18, 0x08, 0x24, 0x63, 0x53, 0x31, 0xB8,
	  0xBC, 0xA9, 0xB0, 0xB5, 0xA2, 0xC4, 0xC4,
	  0xB8, 0x00, 0x8B, 0x00, 0x8E, 0x00, 0xC2 },
	{ MCS_PGAMMACTL, 0x00,
	  0x18, 0x08, 0x24, 0x62, 0x54, 0x30, 0xB9,
	  0xBB, 0xA9, 0xB0, 0xB3, 0xA1, 0xC1, 0xC3,
	  0xB7, 0x00, 0x91, 0x00, 0x95, 0x00, 0xDA },
	{ MCS_PGAMMACTL, 0x00,
	  0x18, 0x08, 0x24, 0x66, 0x58, 0x34, 0xB6,
	  0xBA, 0xA7, 0xAF, 0xB3, 0xA0, 0xC1, 0xC2,
	  0xB7, 0x00, 0x97, 0x00, 0x9A, 0x00, 0xD1 },
	{ MCS_PGAMMACTL, 0x00,
	  0x18, 0x08, 0x24, 0x64, 0x56, 0x33, 0xB6,
	  0xBA, 0xA8, 0xAC, 0xB1, 0x9D, 0xC1, 0xC1,
	  0xB7, 0x00, 0x9C, 0x00, 0x9F, 0x00, 0xD6 },
	{ MCS_PGAMMACTL, 0x00,
	  0x18, 0x08, 0x24, 0x5f, 0x50, 0x2d, 0xB6,
	  0xB9, 0xA7, 0xAd, 0xB1, 0x9f, 0xbe, 0xC0,
	  0xB5, 0x00, 0xa0, 0x00, 0xa4, 0x00, 0xdb },
};

struct s6e63m0 {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *bl_dev;

	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;

	bool prepared;
	bool enabled;

	/*
	 * This field is tested by functions directly accessing bus before
	 * transfer, transfer is skipped if it is set. In case of transfer
	 * failure or unexpected response the field is set to error value.
	 * Such construct allows to eliminate many checks in higher level
	 * functions.
	 */
	int error;
};

static const struct drm_display_mode default_mode = {
	.clock		= 25628,
	.hdisplay	= 480,
	.hsync_start	= 480 + 16,
	.hsync_end	= 480 + 16 + 2,
	.htotal		= 480 + 16 + 2 + 16,
	.vdisplay	= 800,
	.vsync_start	= 800 + 28,
	.vsync_end	= 800 + 28 + 2,
	.vtotal		= 800 + 28 + 2 + 1,
	.vrefresh	= 60,
	.width_mm	= 53,
	.height_mm	= 89,
	.flags		= DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static inline struct s6e63m0 *panel_to_s6e63m0(struct drm_panel *panel)
{
	return container_of(panel, struct s6e63m0, panel);
}

static int s6e63m0_clear_error(struct s6e63m0 *ctx)
{
	int ret = ctx->error;

	ctx->error = 0;
	return ret;
}

static int s6e63m0_spi_write_word(struct s6e63m0 *ctx, u16 data)
{
	struct spi_device *spi = to_spi_device(ctx->dev);
	struct spi_transfer xfer = {
		.len	= 2,
		.tx_buf = &data,
	};
	struct spi_message msg;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	return spi_sync(spi, &msg);
}

static void s6e63m0_dcs_write(struct s6e63m0 *ctx, const u8 *data, size_t len)
{
	int ret = 0;

	if (ctx->error < 0 || len == 0)
		return;

	DRM_DEV_DEBUG(ctx->dev, "writing dcs seq: %*ph\n", (int)len, data);
	ret = s6e63m0_spi_write_word(ctx, *data);

	while (!ret && --len) {
		++data;
		ret = s6e63m0_spi_write_word(ctx, *data | DATA_MASK);
	}

	if (ret) {
		DRM_DEV_ERROR(ctx->dev, "error %d writing dcs seq: %*ph\n", ret,
			      (int)len, data);
		ctx->error = ret;
	}

	usleep_range(300, 310);
}

#define s6e63m0_dcs_write_seq_static(ctx, seq ...) \
	({ \
		static const u8 d[] = { seq }; \
		s6e63m0_dcs_write(ctx, d, ARRAY_SIZE(d)); \
	})

static void s6e63m0_init(struct s6e63m0 *ctx)
{
	s6e63m0_dcs_write_seq_static(ctx, MCS_PANELCTL,
				     0x01, 0x27, 0x27, 0x07, 0x07, 0x54, 0x9f,
				     0x63, 0x86, 0x1a, 0x33, 0x0d, 0x00, 0x00);

	s6e63m0_dcs_write_seq_static(ctx, MCS_DISCTL,
				     0x02, 0x03, 0x1c, 0x10, 0x10);
	s6e63m0_dcs_write_seq_static(ctx, MCS_IFCTL,
				     0x03, 0x00, 0x00);

	s6e63m0_dcs_write_seq_static(ctx, MCS_PGAMMACTL,
				     0x00, 0x18, 0x08, 0x24, 0x64, 0x56, 0x33,
				     0xb6, 0xba, 0xa8, 0xac, 0xb1, 0x9d, 0xc1,
				     0xc1, 0xb7, 0x00, 0x9c, 0x00, 0x9f, 0x00,
				     0xd6);
	s6e63m0_dcs_write_seq_static(ctx, MCS_PGAMMACTL,
				     0x01);

	s6e63m0_dcs_write_seq_static(ctx, MCS_SRCCTL,
				     0x00, 0x8c, 0x07);
	s6e63m0_dcs_write_seq_static(ctx, 0xb3,
				     0xc);

	s6e63m0_dcs_write_seq_static(ctx, 0xb5,
				     0x2c, 0x12, 0x0c, 0x0a, 0x10, 0x0e, 0x17,
				     0x13, 0x1f, 0x1a, 0x2a, 0x24, 0x1f, 0x1b,
				     0x1a, 0x17, 0x2b, 0x26, 0x22, 0x20, 0x3a,
				     0x34, 0x30, 0x2c, 0x29, 0x26, 0x25, 0x23,
				     0x21, 0x20, 0x1e, 0x1e);

	s6e63m0_dcs_write_seq_static(ctx, 0xb6,
				     0x00, 0x00, 0x11, 0x22, 0x33, 0x44, 0x44,
				     0x44, 0x55, 0x55, 0x66, 0x66, 0x66, 0x66,
				     0x66, 0x66);

	s6e63m0_dcs_write_seq_static(ctx, 0xb7,
				     0x2c, 0x12, 0x0c, 0x0a, 0x10, 0x0e, 0x17,
				     0x13, 0x1f, 0x1a, 0x2a, 0x24, 0x1f, 0x1b,
				     0x1a, 0x17, 0x2b, 0x26, 0x22, 0x20, 0x3a,
				     0x34, 0x30, 0x2c, 0x29, 0x26, 0x25, 0x23,
				     0x21, 0x20, 0x1e, 0x1e, 0x00, 0x00, 0x11,
				     0x22, 0x33, 0x44, 0x44, 0x44, 0x55, 0x55,
				     0x66, 0x66, 0x66, 0x66, 0x66, 0x66);

	s6e63m0_dcs_write_seq_static(ctx, 0xb9,
				     0x2c, 0x12, 0x0c, 0x0a, 0x10, 0x0e, 0x17,
				     0x13, 0x1f, 0x1a, 0x2a, 0x24, 0x1f, 0x1b,
				     0x1a, 0x17, 0x2b, 0x26, 0x22, 0x20, 0x3a,
				     0x34, 0x30, 0x2c, 0x29, 0x26, 0x25, 0x23,
				     0x21, 0x20, 0x1e, 0x1e);

	s6e63m0_dcs_write_seq_static(ctx, 0xba,
				     0x00, 0x00, 0x11, 0x22, 0x33, 0x44, 0x44,
				     0x44, 0x55, 0x55, 0x66, 0x66, 0x66, 0x66,
				     0x66, 0x66);

	s6e63m0_dcs_write_seq_static(ctx, MCS_BCMODE,
				     0x4d, 0x96, 0x1d, 0x00, 0x00, 0x01, 0xdf,
				     0x00, 0x00, 0x03, 0x1f, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06,
				     0x09, 0x0d, 0x0f, 0x12, 0x15, 0x18);

	s6e63m0_dcs_write_seq_static(ctx, 0xb2,
				     0x10, 0x10, 0x0b, 0x05);

	s6e63m0_dcs_write_seq_static(ctx, MCS_MIECTL1,
				     0x01);

	s6e63m0_dcs_write_seq_static(ctx, MCS_ELVSS_ON,
				     0x0b);

	s6e63m0_dcs_write_seq_static(ctx, MIPI_DCS_EXIT_SLEEP_MODE);
}

static int s6e63m0_power_on(struct s6e63m0 *ctx)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	msleep(25);

	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(120);

	return 0;
}

static int s6e63m0_power_off(struct s6e63m0 *ctx)
{
	int ret;

	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(120);

	ret = regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	return 0;
}

static int s6e63m0_disable(struct drm_panel *panel)
{
	struct s6e63m0 *ctx = panel_to_s6e63m0(panel);

	if (!ctx->enabled)
		return 0;

	backlight_disable(ctx->bl_dev);

	s6e63m0_dcs_write_seq_static(ctx, MIPI_DCS_ENTER_SLEEP_MODE);
	msleep(200);

	ctx->enabled = false;

	return 0;
}

static int s6e63m0_unprepare(struct drm_panel *panel)
{
	struct s6e63m0 *ctx = panel_to_s6e63m0(panel);
	int ret;

	if (!ctx->prepared)
		return 0;

	s6e63m0_clear_error(ctx);

	ret = s6e63m0_power_off(ctx);
	if (ret < 0)
		return ret;

	ctx->prepared = false;

	return 0;
}

static int s6e63m0_prepare(struct drm_panel *panel)
{
	struct s6e63m0 *ctx = panel_to_s6e63m0(panel);
	int ret;

	if (ctx->prepared)
		return 0;

	ret = s6e63m0_power_on(ctx);
	if (ret < 0)
		return ret;

	s6e63m0_init(ctx);

	ret = s6e63m0_clear_error(ctx);

	if (ret < 0)
		s6e63m0_unprepare(panel);

	ctx->prepared = true;

	return ret;
}

static int s6e63m0_enable(struct drm_panel *panel)
{
	struct s6e63m0 *ctx = panel_to_s6e63m0(panel);

	if (ctx->enabled)
		return 0;

	s6e63m0_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_ON);

	backlight_enable(ctx->bl_dev);

	ctx->enabled = true;

	return 0;
}

static int s6e63m0_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		DRM_ERROR("failed to add mode %ux%ux@%u\n",
			  default_mode.hdisplay, default_mode.vdisplay,
			  default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs s6e63m0_drm_funcs = {
	.disable	= s6e63m0_disable,
	.unprepare	= s6e63m0_unprepare,
	.prepare	= s6e63m0_prepare,
	.enable		= s6e63m0_enable,
	.get_modes	= s6e63m0_get_modes,
};

static int s6e63m0_set_brightness(struct backlight_device *bd)
{
	struct s6e63m0 *ctx = bl_get_data(bd);

	int brightness = bd->props.brightness;

	/* disable and set new gamma */
	s6e63m0_dcs_write(ctx, s6e63m0_gamma_22[brightness],
			  ARRAY_SIZE(s6e63m0_gamma_22[brightness]));

	/* update gamma table. */
	s6e63m0_dcs_write_seq_static(ctx, MCS_PGAMMACTL, 0x01);

	return s6e63m0_clear_error(ctx);
}

static const struct backlight_ops s6e63m0_backlight_ops = {
	.update_status	= s6e63m0_set_brightness,
};

static int s6e63m0_backlight_register(struct s6e63m0 *ctx)
{
	struct backlight_properties props = {
		.type		= BACKLIGHT_RAW,
		.brightness	= MAX_BRIGHTNESS,
		.max_brightness = MAX_BRIGHTNESS
	};
	struct device *dev = ctx->dev;
	int ret = 0;

	ctx->bl_dev = devm_backlight_device_register(dev, "panel", dev, ctx,
						     &s6e63m0_backlight_ops,
						     &props);
	if (IS_ERR(ctx->bl_dev)) {
		ret = PTR_ERR(ctx->bl_dev);
		DRM_DEV_ERROR(dev, "error registering backlight device (%d)\n",
			      ret);
	}

	return ret;
}

static int s6e63m0_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct s6e63m0 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(struct s6e63m0), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	spi_set_drvdata(spi, ctx);

	ctx->dev = dev;
	ctx->enabled = false;
	ctx->prepared = false;

	ctx->supplies[0].supply = "vdd3";
	ctx->supplies[1].supply = "vci";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "failed to get regulators: %d\n", ret);
		return ret;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		DRM_DEV_ERROR(dev, "cannot get reset-gpios %ld\n",
			      PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}

	spi->bits_per_word = 9;
	spi->mode = SPI_MODE_3;
	ret = spi_setup(spi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "spi setup failed.\n");
		return ret;
	}

	drm_panel_init(&ctx->panel, dev, &s6e63m0_drm_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	ret = s6e63m0_backlight_register(ctx);
	if (ret < 0)
		return ret;

	return drm_panel_add(&ctx->panel);
}

static int s6e63m0_remove(struct spi_device *spi)
{
	struct s6e63m0 *ctx = spi_get_drvdata(spi);

	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id s6e63m0_of_match[] = {
	{ .compatible = "samsung,s6e63m0" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s6e63m0_of_match);

static struct spi_driver s6e63m0_driver = {
	.probe			= s6e63m0_probe,
	.remove			= s6e63m0_remove,
	.driver			= {
		.name		= "panel-samsung-s6e63m0",
		.of_match_table = s6e63m0_of_match,
	},
};
module_spi_driver(s6e63m0_driver);

MODULE_AUTHOR("Paweł Chmiel <pawel.mikolaj.chmiel@gmail.com>");
MODULE_DESCRIPTION("s6e63m0 LCD Driver");
MODULE_LICENSE("GPL v2");
