// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Free Electrons
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <video/mipi_display.h>

#include <drm/drm_device.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define ST7789V_COLMOD_RGB_FMT_18BITS		(6 << 4)
#define ST7789V_COLMOD_CTRL_FMT_18BITS		(6 << 0)

#define ST7789V_RAMCTRL_CMD		0xb0
#define ST7789V_RAMCTRL_RM_RGB			BIT(4)
#define ST7789V_RAMCTRL_DM_RGB			BIT(0)
#define ST7789V_RAMCTRL_MAGIC			(3 << 6)
#define ST7789V_RAMCTRL_EPF(n)			(((n) & 3) << 4)

#define ST7789V_RGBCTRL_CMD		0xb1
#define ST7789V_RGBCTRL_WO			BIT(7)
#define ST7789V_RGBCTRL_RCM(n)			(((n) & 3) << 5)
#define ST7789V_RGBCTRL_VSYNC_HIGH		BIT(3)
#define ST7789V_RGBCTRL_HSYNC_HIGH		BIT(2)
#define ST7789V_RGBCTRL_PCLK_HIGH		BIT(1)
#define ST7789V_RGBCTRL_VBP(n)			((n) & 0x7f)
#define ST7789V_RGBCTRL_HBP(n)			((n) & 0x1f)

#define ST7789V_PORCTRL_CMD		0xb2
#define ST7789V_PORCTRL_IDLE_BP(n)		(((n) & 0xf) << 4)
#define ST7789V_PORCTRL_IDLE_FP(n)		((n) & 0xf)
#define ST7789V_PORCTRL_PARTIAL_BP(n)		(((n) & 0xf) << 4)
#define ST7789V_PORCTRL_PARTIAL_FP(n)		((n) & 0xf)

#define ST7789V_GCTRL_CMD		0xb7
#define ST7789V_GCTRL_VGHS(n)			(((n) & 7) << 4)
#define ST7789V_GCTRL_VGLS(n)			((n) & 7)

#define ST7789V_VCOMS_CMD		0xbb

#define ST7789V_LCMCTRL_CMD		0xc0
#define ST7789V_LCMCTRL_XBGR			BIT(5)
#define ST7789V_LCMCTRL_XMX			BIT(3)
#define ST7789V_LCMCTRL_XMH			BIT(2)

#define ST7789V_VDVVRHEN_CMD		0xc2
#define ST7789V_VDVVRHEN_CMDEN			BIT(0)

#define ST7789V_VRHS_CMD		0xc3

#define ST7789V_VDVS_CMD		0xc4

#define ST7789V_FRCTRL2_CMD		0xc6

#define ST7789V_PWCTRL1_CMD		0xd0
#define ST7789V_PWCTRL1_MAGIC			0xa4
#define ST7789V_PWCTRL1_AVDD(n)			(((n) & 3) << 6)
#define ST7789V_PWCTRL1_AVCL(n)			(((n) & 3) << 4)
#define ST7789V_PWCTRL1_VDS(n)			((n) & 3)

#define ST7789V_PVGAMCTRL_CMD		0xe0
#define ST7789V_PVGAMCTRL_JP0(n)		(((n) & 3) << 4)
#define ST7789V_PVGAMCTRL_JP1(n)		(((n) & 3) << 4)
#define ST7789V_PVGAMCTRL_VP0(n)		((n) & 0xf)
#define ST7789V_PVGAMCTRL_VP1(n)		((n) & 0x3f)
#define ST7789V_PVGAMCTRL_VP2(n)		((n) & 0x3f)
#define ST7789V_PVGAMCTRL_VP4(n)		((n) & 0x1f)
#define ST7789V_PVGAMCTRL_VP6(n)		((n) & 0x1f)
#define ST7789V_PVGAMCTRL_VP13(n)		((n) & 0xf)
#define ST7789V_PVGAMCTRL_VP20(n)		((n) & 0x7f)
#define ST7789V_PVGAMCTRL_VP27(n)		((n) & 7)
#define ST7789V_PVGAMCTRL_VP36(n)		(((n) & 7) << 4)
#define ST7789V_PVGAMCTRL_VP43(n)		((n) & 0x7f)
#define ST7789V_PVGAMCTRL_VP50(n)		((n) & 0xf)
#define ST7789V_PVGAMCTRL_VP57(n)		((n) & 0x1f)
#define ST7789V_PVGAMCTRL_VP59(n)		((n) & 0x1f)
#define ST7789V_PVGAMCTRL_VP61(n)		((n) & 0x3f)
#define ST7789V_PVGAMCTRL_VP62(n)		((n) & 0x3f)
#define ST7789V_PVGAMCTRL_VP63(n)		(((n) & 0xf) << 4)

#define ST7789V_NVGAMCTRL_CMD		0xe1
#define ST7789V_NVGAMCTRL_JN0(n)		(((n) & 3) << 4)
#define ST7789V_NVGAMCTRL_JN1(n)		(((n) & 3) << 4)
#define ST7789V_NVGAMCTRL_VN0(n)		((n) & 0xf)
#define ST7789V_NVGAMCTRL_VN1(n)		((n) & 0x3f)
#define ST7789V_NVGAMCTRL_VN2(n)		((n) & 0x3f)
#define ST7789V_NVGAMCTRL_VN4(n)		((n) & 0x1f)
#define ST7789V_NVGAMCTRL_VN6(n)		((n) & 0x1f)
#define ST7789V_NVGAMCTRL_VN13(n)		((n) & 0xf)
#define ST7789V_NVGAMCTRL_VN20(n)		((n) & 0x7f)
#define ST7789V_NVGAMCTRL_VN27(n)		((n) & 7)
#define ST7789V_NVGAMCTRL_VN36(n)		(((n) & 7) << 4)
#define ST7789V_NVGAMCTRL_VN43(n)		((n) & 0x7f)
#define ST7789V_NVGAMCTRL_VN50(n)		((n) & 0xf)
#define ST7789V_NVGAMCTRL_VN57(n)		((n) & 0x1f)
#define ST7789V_NVGAMCTRL_VN59(n)		((n) & 0x1f)
#define ST7789V_NVGAMCTRL_VN61(n)		((n) & 0x3f)
#define ST7789V_NVGAMCTRL_VN62(n)		((n) & 0x3f)
#define ST7789V_NVGAMCTRL_VN63(n)		(((n) & 0xf) << 4)

#define ST7789V_TEST(val, func)			\
	do {					\
		if ((val = (func)))		\
			return val;		\
	} while (0)

struct st7789v {
	struct drm_panel panel;
	struct spi_device *spi;
	struct gpio_desc *reset;
	struct backlight_device *backlight;
	struct regulator *power;
};

enum st7789v_prefix {
	ST7789V_COMMAND = 0,
	ST7789V_DATA = 1,
};

static inline struct st7789v *panel_to_st7789v(struct drm_panel *panel)
{
	return container_of(panel, struct st7789v, panel);
}

static int st7789v_spi_write(struct st7789v *ctx, enum st7789v_prefix prefix,
			     u8 data)
{
	struct spi_transfer xfer = { };
	struct spi_message msg;
	u16 txbuf = ((prefix & 1) << 8) | data;

	spi_message_init(&msg);

	xfer.tx_buf = &txbuf;
	xfer.bits_per_word = 9;
	xfer.len = sizeof(txbuf);

	spi_message_add_tail(&xfer, &msg);
	return spi_sync(ctx->spi, &msg);
}

static int st7789v_write_command(struct st7789v *ctx, u8 cmd)
{
	return st7789v_spi_write(ctx, ST7789V_COMMAND, cmd);
}

static int st7789v_write_data(struct st7789v *ctx, u8 cmd)
{
	return st7789v_spi_write(ctx, ST7789V_DATA, cmd);
}

static const struct drm_display_mode default_mode = {
	.clock = 7000,
	.hdisplay = 240,
	.hsync_start = 240 + 38,
	.hsync_end = 240 + 38 + 10,
	.htotal = 240 + 38 + 10 + 10,
	.vdisplay = 320,
	.vsync_start = 320 + 8,
	.vsync_end = 320 + 8 + 4,
	.vtotal = 320 + 8 + 4 + 4,
	.vrefresh = 60,
};

static int st7789v_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	panel->connector->display_info.width_mm = 61;
	panel->connector->display_info.height_mm = 103;

	return 1;
}

static int st7789v_prepare(struct drm_panel *panel)
{
	struct st7789v *ctx = panel_to_st7789v(panel);
	int ret;

	ret = regulator_enable(ctx->power);
	if (ret)
		return ret;

	gpiod_set_value(ctx->reset, 1);
	msleep(30);
	gpiod_set_value(ctx->reset, 0);
	msleep(120);

	ST7789V_TEST(ret, st7789v_write_command(ctx, MIPI_DCS_EXIT_SLEEP_MODE));

	/* We need to wait 120ms after a sleep out command */
	msleep(120);

	ST7789V_TEST(ret, st7789v_write_command(ctx,
						MIPI_DCS_SET_ADDRESS_MODE));
	ST7789V_TEST(ret, st7789v_write_data(ctx, 0));

	ST7789V_TEST(ret, st7789v_write_command(ctx,
						MIPI_DCS_SET_PIXEL_FORMAT));
	ST7789V_TEST(ret, st7789v_write_data(ctx,
					     (MIPI_DCS_PIXEL_FMT_18BIT << 4) |
					     (MIPI_DCS_PIXEL_FMT_18BIT)));

	ST7789V_TEST(ret, st7789v_write_command(ctx, ST7789V_PORCTRL_CMD));
	ST7789V_TEST(ret, st7789v_write_data(ctx, 0xc));
	ST7789V_TEST(ret, st7789v_write_data(ctx, 0xc));
	ST7789V_TEST(ret, st7789v_write_data(ctx, 0));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_PORCTRL_IDLE_BP(3) |
					     ST7789V_PORCTRL_IDLE_FP(3)));
	ST7789V_TEST(ret, st7789v_write_data(ctx,
					     ST7789V_PORCTRL_PARTIAL_BP(3) |
					     ST7789V_PORCTRL_PARTIAL_FP(3)));

	ST7789V_TEST(ret, st7789v_write_command(ctx, ST7789V_GCTRL_CMD));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_GCTRL_VGLS(5) |
					     ST7789V_GCTRL_VGHS(3)));

	ST7789V_TEST(ret, st7789v_write_command(ctx, ST7789V_VCOMS_CMD));
	ST7789V_TEST(ret, st7789v_write_data(ctx, 0x2b));

	ST7789V_TEST(ret, st7789v_write_command(ctx, ST7789V_LCMCTRL_CMD));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_LCMCTRL_XMH |
					     ST7789V_LCMCTRL_XMX |
					     ST7789V_LCMCTRL_XBGR));

	ST7789V_TEST(ret, st7789v_write_command(ctx, ST7789V_VDVVRHEN_CMD));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_VDVVRHEN_CMDEN));

	ST7789V_TEST(ret, st7789v_write_command(ctx, ST7789V_VRHS_CMD));
	ST7789V_TEST(ret, st7789v_write_data(ctx, 0xf));

	ST7789V_TEST(ret, st7789v_write_command(ctx, ST7789V_VDVS_CMD));
	ST7789V_TEST(ret, st7789v_write_data(ctx, 0x20));

	ST7789V_TEST(ret, st7789v_write_command(ctx, ST7789V_FRCTRL2_CMD));
	ST7789V_TEST(ret, st7789v_write_data(ctx, 0xf));

	ST7789V_TEST(ret, st7789v_write_command(ctx, ST7789V_PWCTRL1_CMD));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_PWCTRL1_MAGIC));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_PWCTRL1_AVDD(2) |
					     ST7789V_PWCTRL1_AVCL(2) |
					     ST7789V_PWCTRL1_VDS(1)));

	ST7789V_TEST(ret, st7789v_write_command(ctx, ST7789V_PVGAMCTRL_CMD));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_PVGAMCTRL_VP63(0xd)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_PVGAMCTRL_VP1(0xca)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_PVGAMCTRL_VP2(0xe)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_PVGAMCTRL_VP4(8)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_PVGAMCTRL_VP6(9)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_PVGAMCTRL_VP13(7)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_PVGAMCTRL_VP20(0x2d)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_PVGAMCTRL_VP27(0xb) |
					     ST7789V_PVGAMCTRL_VP36(3)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_PVGAMCTRL_VP43(0x3d)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_PVGAMCTRL_JP1(3) |
					     ST7789V_PVGAMCTRL_VP50(4)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_PVGAMCTRL_VP57(0xa)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_PVGAMCTRL_VP59(0xa)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_PVGAMCTRL_VP61(0x1b)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_PVGAMCTRL_VP62(0x28)));

	ST7789V_TEST(ret, st7789v_write_command(ctx, ST7789V_NVGAMCTRL_CMD));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_NVGAMCTRL_VN63(0xd)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_NVGAMCTRL_VN1(0xca)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_NVGAMCTRL_VN2(0xf)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_NVGAMCTRL_VN4(8)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_NVGAMCTRL_VN6(8)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_NVGAMCTRL_VN13(7)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_NVGAMCTRL_VN20(0x2e)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_NVGAMCTRL_VN27(0xc) |
					     ST7789V_NVGAMCTRL_VN36(5)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_NVGAMCTRL_VN43(0x40)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_NVGAMCTRL_JN1(3) |
					     ST7789V_NVGAMCTRL_VN50(4)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_NVGAMCTRL_VN57(9)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_NVGAMCTRL_VN59(0xb)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_NVGAMCTRL_VN61(0x1b)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_NVGAMCTRL_VN62(0x28)));

	ST7789V_TEST(ret, st7789v_write_command(ctx, MIPI_DCS_ENTER_INVERT_MODE));

	ST7789V_TEST(ret, st7789v_write_command(ctx, ST7789V_RAMCTRL_CMD));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_RAMCTRL_DM_RGB |
					     ST7789V_RAMCTRL_RM_RGB));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_RAMCTRL_EPF(3) |
					     ST7789V_RAMCTRL_MAGIC));

	ST7789V_TEST(ret, st7789v_write_command(ctx, ST7789V_RGBCTRL_CMD));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_RGBCTRL_WO |
					     ST7789V_RGBCTRL_RCM(2) |
					     ST7789V_RGBCTRL_VSYNC_HIGH |
					     ST7789V_RGBCTRL_HSYNC_HIGH |
					     ST7789V_RGBCTRL_PCLK_HIGH));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_RGBCTRL_VBP(8)));
	ST7789V_TEST(ret, st7789v_write_data(ctx, ST7789V_RGBCTRL_HBP(20)));

	return 0;
}

static int st7789v_enable(struct drm_panel *panel)
{
	struct st7789v *ctx = panel_to_st7789v(panel);

	if (ctx->backlight) {
		ctx->backlight->props.state &= ~BL_CORE_FBBLANK;
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	return st7789v_write_command(ctx, MIPI_DCS_SET_DISPLAY_ON);
}

static int st7789v_disable(struct drm_panel *panel)
{
	struct st7789v *ctx = panel_to_st7789v(panel);
	int ret;

	ST7789V_TEST(ret, st7789v_write_command(ctx, MIPI_DCS_SET_DISPLAY_OFF));

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		ctx->backlight->props.state |= BL_CORE_FBBLANK;
		backlight_update_status(ctx->backlight);
	}

	return 0;
}

static int st7789v_unprepare(struct drm_panel *panel)
{
	struct st7789v *ctx = panel_to_st7789v(panel);
	int ret;

	ST7789V_TEST(ret, st7789v_write_command(ctx, MIPI_DCS_ENTER_SLEEP_MODE));

	regulator_disable(ctx->power);

	return 0;
}

static const struct drm_panel_funcs st7789v_drm_funcs = {
	.disable	= st7789v_disable,
	.enable		= st7789v_enable,
	.get_modes	= st7789v_get_modes,
	.prepare	= st7789v_prepare,
	.unprepare	= st7789v_unprepare,
};

static int st7789v_probe(struct spi_device *spi)
{
	struct device_node *backlight;
	struct st7789v *ctx;
	int ret;

	ctx = devm_kzalloc(&spi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	spi_set_drvdata(spi, ctx);
	ctx->spi = spi;

	ctx->panel.dev = &spi->dev;
	ctx->panel.funcs = &st7789v_drm_funcs;

	ctx->power = devm_regulator_get(&spi->dev, "power");
	if (IS_ERR(ctx->power))
		return PTR_ERR(ctx->power);

	ctx->reset = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset)) {
		dev_err(&spi->dev, "Couldn't get our reset line\n");
		return PTR_ERR(ctx->reset);
	}

	backlight = of_parse_phandle(spi->dev.of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0)
		goto err_free_backlight;

	return 0;

err_free_backlight:
	if (ctx->backlight)
		put_device(&ctx->backlight->dev);

	return ret;
}

static int st7789v_remove(struct spi_device *spi)
{
	struct st7789v *ctx = spi_get_drvdata(spi);

	drm_panel_remove(&ctx->panel);

	if (ctx->backlight)
		put_device(&ctx->backlight->dev);

	return 0;
}

static const struct of_device_id st7789v_of_match[] = {
	{ .compatible = "sitronix,st7789v" },
	{ }
};
MODULE_DEVICE_TABLE(of, st7789v_of_match);

static struct spi_driver st7789v_driver = {
	.probe = st7789v_probe,
	.remove = st7789v_remove,
	.driver = {
		.name = "st7789v",
		.of_match_table = st7789v_of_match,
	},
};
module_spi_driver(st7789v_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Sitronix st7789v LCD Driver");
MODULE_LICENSE("GPL v2");
