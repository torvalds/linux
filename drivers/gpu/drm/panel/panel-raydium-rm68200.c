// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2017
 *
 * Authors: Philippe Cornu <philippe.cornu@st.com>
 *          Yannick Fertre <yannick.fertre@st.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

/*** Manufacturer Command Set ***/
#define MCS_CMD_MODE_SW		0xFE /* CMD Mode Switch */
#define MCS_CMD1_UCS		0x00 /* User Command Set (UCS = CMD1) */
#define MCS_CMD2_P0		0x01 /* Manufacture Command Set Page0 (CMD2 P0) */
#define MCS_CMD2_P1		0x02 /* Manufacture Command Set Page1 (CMD2 P1) */
#define MCS_CMD2_P2		0x03 /* Manufacture Command Set Page2 (CMD2 P2) */
#define MCS_CMD2_P3		0x04 /* Manufacture Command Set Page3 (CMD2 P3) */

/* CMD2 P0 commands (Display Options and Power) */
#define MCS_STBCTR		0x12 /* TE1 Output Setting Zig-Zag Connection */
#define MCS_SGOPCTR		0x16 /* Source Bias Current */
#define MCS_SDCTR		0x1A /* Source Output Delay Time */
#define MCS_INVCTR		0x1B /* Inversion Type */
#define MCS_EXT_PWR_IC		0x24 /* External PWR IC Control */
#define MCS_SETAVDD		0x27 /* PFM Control for AVDD Output */
#define MCS_SETAVEE		0x29 /* PFM Control for AVEE Output */
#define MCS_BT2CTR		0x2B /* DDVDL Charge Pump Control */
#define MCS_BT3CTR		0x2F /* VGH Charge Pump Control */
#define MCS_BT4CTR		0x34 /* VGL Charge Pump Control */
#define MCS_VCMCTR		0x46 /* VCOM Output Level Control */
#define MCS_SETVGN		0x52 /* VG M/S N Control */
#define MCS_SETVGP		0x54 /* VG M/S P Control */
#define MCS_SW_CTRL		0x5F /* Interface Control for PFM and MIPI */

/* CMD2 P2 commands (GOA Timing Control) - no description in datasheet */
#define GOA_VSTV1		0x00
#define GOA_VSTV2		0x07
#define GOA_VCLK1		0x0E
#define GOA_VCLK2		0x17
#define GOA_VCLK_OPT1		0x20
#define GOA_BICLK1		0x2A
#define GOA_BICLK2		0x37
#define GOA_BICLK3		0x44
#define GOA_BICLK4		0x4F
#define GOA_BICLK_OPT1		0x5B
#define GOA_BICLK_OPT2		0x60
#define MCS_GOA_GPO1		0x6D
#define MCS_GOA_GPO2		0x71
#define MCS_GOA_EQ		0x74
#define MCS_GOA_CLK_GALLON	0x7C
#define MCS_GOA_FS_SEL0		0x7E
#define MCS_GOA_FS_SEL1		0x87
#define MCS_GOA_FS_SEL2		0x91
#define MCS_GOA_FS_SEL3		0x9B
#define MCS_GOA_BS_SEL0		0xAC
#define MCS_GOA_BS_SEL1		0xB5
#define MCS_GOA_BS_SEL2		0xBF
#define MCS_GOA_BS_SEL3		0xC9
#define MCS_GOA_BS_SEL4		0xD3

/* CMD2 P3 commands (Gamma) */
#define MCS_GAMMA_VP		0x60 /* Gamma VP1~VP16 */
#define MCS_GAMMA_VN		0x70 /* Gamma VN1~VN16 */

struct rm68200 {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	struct regulator *supply;
	bool prepared;
	bool enabled;
};

static const struct drm_display_mode default_mode = {
	.clock = 54000,
	.hdisplay = 720,
	.hsync_start = 720 + 48,
	.hsync_end = 720 + 48 + 9,
	.htotal = 720 + 48 + 9 + 48,
	.vdisplay = 1280,
	.vsync_start = 1280 + 12,
	.vsync_end = 1280 + 12 + 5,
	.vtotal = 1280 + 12 + 5 + 12,
	.flags = 0,
	.width_mm = 68,
	.height_mm = 122,
};

static inline struct rm68200 *panel_to_rm68200(struct drm_panel *panel)
{
	return container_of(panel, struct rm68200, panel);
}

static void rm68200_dcs_write_buf(struct rm68200 *ctx, const void *data,
				  size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int err;

	err = mipi_dsi_dcs_write_buffer(dsi, data, len);
	if (err < 0)
		dev_err_ratelimited(ctx->dev, "MIPI DSI DCS write buffer failed: %d\n", err);
}

static void rm68200_dcs_write_cmd(struct rm68200 *ctx, u8 cmd, u8 value)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int err;

	err = mipi_dsi_dcs_write(dsi, cmd, &value, 1);
	if (err < 0)
		dev_err_ratelimited(ctx->dev, "MIPI DSI DCS write failed: %d\n", err);
}

#define dcs_write_seq(ctx, seq...)				\
({								\
	static const u8 d[] = { seq };				\
								\
	rm68200_dcs_write_buf(ctx, d, ARRAY_SIZE(d));		\
})

/*
 * This panel is not able to auto-increment all cmd addresses so for some of
 * them, we need to send them one by one...
 */
#define dcs_write_cmd_seq(ctx, cmd, seq...)			\
({								\
	static const u8 d[] = { seq };				\
	unsigned int i;						\
								\
	for (i = 0; i < ARRAY_SIZE(d) ; i++)			\
		rm68200_dcs_write_cmd(ctx, cmd + i, d[i]);	\
})

static void rm68200_init_sequence(struct rm68200 *ctx)
{
	/* Enter CMD2 with page 0 */
	dcs_write_seq(ctx, MCS_CMD_MODE_SW, MCS_CMD2_P0);
	dcs_write_cmd_seq(ctx, MCS_EXT_PWR_IC, 0xC0, 0x53, 0x00);
	dcs_write_seq(ctx, MCS_BT2CTR, 0xE5);
	dcs_write_seq(ctx, MCS_SETAVDD, 0x0A);
	dcs_write_seq(ctx, MCS_SETAVEE, 0x0A);
	dcs_write_seq(ctx, MCS_SGOPCTR, 0x52);
	dcs_write_seq(ctx, MCS_BT3CTR, 0x53);
	dcs_write_seq(ctx, MCS_BT4CTR, 0x5A);
	dcs_write_seq(ctx, MCS_INVCTR, 0x00);
	dcs_write_seq(ctx, MCS_STBCTR, 0x0A);
	dcs_write_seq(ctx, MCS_SDCTR, 0x06);
	dcs_write_seq(ctx, MCS_VCMCTR, 0x56);
	dcs_write_seq(ctx, MCS_SETVGN, 0xA0, 0x00);
	dcs_write_seq(ctx, MCS_SETVGP, 0xA0, 0x00);
	dcs_write_seq(ctx, MCS_SW_CTRL, 0x11); /* 2 data lanes, see doc */

	dcs_write_seq(ctx, MCS_CMD_MODE_SW, MCS_CMD2_P2);
	dcs_write_seq(ctx, GOA_VSTV1, 0x05);
	dcs_write_seq(ctx, 0x02, 0x0B);
	dcs_write_seq(ctx, 0x03, 0x0F);
	dcs_write_seq(ctx, 0x04, 0x7D, 0x00, 0x50);
	dcs_write_cmd_seq(ctx, GOA_VSTV2, 0x05, 0x16, 0x0D, 0x11, 0x7D, 0x00,
			  0x50);
	dcs_write_cmd_seq(ctx, GOA_VCLK1, 0x07, 0x08, 0x01, 0x02, 0x00, 0x7D,
			  0x00, 0x85, 0x08);
	dcs_write_cmd_seq(ctx, GOA_VCLK2, 0x03, 0x04, 0x05, 0x06, 0x00, 0x7D,
			  0x00, 0x85, 0x08);
	dcs_write_seq(ctx, GOA_VCLK_OPT1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		      0x00, 0x00, 0x00, 0x00);
	dcs_write_cmd_seq(ctx, GOA_BICLK1, 0x07, 0x08);
	dcs_write_seq(ctx, 0x2D, 0x01);
	dcs_write_seq(ctx, 0x2F, 0x02, 0x00, 0x40, 0x05, 0x08, 0x54, 0x7D,
		      0x00);
	dcs_write_cmd_seq(ctx, GOA_BICLK2, 0x03, 0x04, 0x05, 0x06, 0x00);
	dcs_write_seq(ctx, 0x3D, 0x40);
	dcs_write_seq(ctx, 0x3F, 0x05, 0x08, 0x54, 0x7D, 0x00);
	dcs_write_seq(ctx, GOA_BICLK3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		      0x00, 0x00, 0x00, 0x00, 0x00);
	dcs_write_seq(ctx, GOA_BICLK4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		      0x00, 0x00);
	dcs_write_seq(ctx, 0x58, 0x00, 0x00, 0x00);
	dcs_write_seq(ctx, GOA_BICLK_OPT1, 0x00, 0x00, 0x00, 0x00, 0x00);
	dcs_write_seq(ctx, GOA_BICLK_OPT2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	dcs_write_seq(ctx, MCS_GOA_GPO1, 0x00, 0x00, 0x00, 0x00);
	dcs_write_seq(ctx, MCS_GOA_GPO2, 0x00, 0x20, 0x00);
	dcs_write_seq(ctx, MCS_GOA_EQ, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
		      0x00, 0x00);
	dcs_write_seq(ctx, MCS_GOA_CLK_GALLON, 0x00, 0x00);
	dcs_write_cmd_seq(ctx, MCS_GOA_FS_SEL0, 0xBF, 0x02, 0x06, 0x14, 0x10,
			  0x16, 0x12, 0x08, 0x3F);
	dcs_write_cmd_seq(ctx, MCS_GOA_FS_SEL1, 0x3F, 0x3F, 0x3F, 0x3F, 0x0C,
			  0x0A, 0x0E, 0x3F, 0x3F, 0x00);
	dcs_write_cmd_seq(ctx, MCS_GOA_FS_SEL2, 0x04, 0x3F, 0x3F, 0x3F, 0x3F,
			  0x05, 0x01, 0x3F, 0x3F, 0x0F);
	dcs_write_cmd_seq(ctx, MCS_GOA_FS_SEL3, 0x0B, 0x0D, 0x3F, 0x3F, 0x3F,
			  0x3F);
	dcs_write_cmd_seq(ctx, 0xA2, 0x3F, 0x09, 0x13, 0x17, 0x11, 0x15);
	dcs_write_cmd_seq(ctx, 0xA9, 0x07, 0x03, 0x3F);
	dcs_write_cmd_seq(ctx, MCS_GOA_BS_SEL0, 0x3F, 0x05, 0x01, 0x17, 0x13,
			  0x15, 0x11, 0x0F, 0x3F);
	dcs_write_cmd_seq(ctx, MCS_GOA_BS_SEL1, 0x3F, 0x3F, 0x3F, 0x3F, 0x0B,
			  0x0D, 0x09, 0x3F, 0x3F, 0x07);
	dcs_write_cmd_seq(ctx, MCS_GOA_BS_SEL2, 0x03, 0x3F, 0x3F, 0x3F, 0x3F,
			  0x02, 0x06, 0x3F, 0x3F, 0x08);
	dcs_write_cmd_seq(ctx, MCS_GOA_BS_SEL3, 0x0C, 0x0A, 0x3F, 0x3F, 0x3F,
			  0x3F, 0x3F, 0x0E, 0x10, 0x14);
	dcs_write_cmd_seq(ctx, MCS_GOA_BS_SEL4, 0x12, 0x16, 0x00, 0x04, 0x3F);
	dcs_write_seq(ctx, 0xDC, 0x02);
	dcs_write_seq(ctx, 0xDE, 0x12);

	dcs_write_seq(ctx, MCS_CMD_MODE_SW, 0x0E); /* No documentation */
	dcs_write_seq(ctx, 0x01, 0x75);

	dcs_write_seq(ctx, MCS_CMD_MODE_SW, MCS_CMD2_P3);
	dcs_write_cmd_seq(ctx, MCS_GAMMA_VP, 0x00, 0x0C, 0x12, 0x0E, 0x06,
			  0x12, 0x0E, 0x0B, 0x15, 0x0B, 0x10, 0x07, 0x0F,
			  0x12, 0x0C, 0x00);
	dcs_write_cmd_seq(ctx, MCS_GAMMA_VN, 0x00, 0x0C, 0x12, 0x0E, 0x06,
			  0x12, 0x0E, 0x0B, 0x15, 0x0B, 0x10, 0x07, 0x0F,
			  0x12, 0x0C, 0x00);

	/* Exit CMD2 */
	dcs_write_seq(ctx, MCS_CMD_MODE_SW, MCS_CMD1_UCS);
}

static int rm68200_disable(struct drm_panel *panel)
{
	struct rm68200 *ctx = panel_to_rm68200(panel);

	if (!ctx->enabled)
		return 0;

	ctx->enabled = false;

	return 0;
}

static int rm68200_unprepare(struct drm_panel *panel)
{
	struct rm68200 *ctx = panel_to_rm68200(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret)
		dev_warn(panel->dev, "failed to set display off: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret)
		dev_warn(panel->dev, "failed to enter sleep mode: %d\n", ret);

	msleep(120);

	if (ctx->reset_gpio) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		msleep(20);
	}

	regulator_disable(ctx->supply);

	ctx->prepared = false;

	return 0;
}

static int rm68200_prepare(struct drm_panel *panel)
{
	struct rm68200 *ctx = panel_to_rm68200(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_enable(ctx->supply);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to enable supply: %d\n", ret);
		return ret;
	}

	if (ctx->reset_gpio) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		msleep(20);
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		msleep(100);
	}

	rm68200_init_sequence(ctx);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret)
		return ret;

	msleep(125);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret)
		return ret;

	msleep(20);

	ctx->prepared = true;

	return 0;
}

static int rm68200_enable(struct drm_panel *panel)
{
	struct rm68200 *ctx = panel_to_rm68200(panel);

	if (ctx->enabled)
		return 0;

	ctx->enabled = true;

	return 0;
}

static int rm68200_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static const struct drm_panel_funcs rm68200_drm_funcs = {
	.disable = rm68200_disable,
	.unprepare = rm68200_unprepare,
	.prepare = rm68200_prepare,
	.enable = rm68200_enable,
	.get_modes = rm68200_get_modes,
};

static int rm68200_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct rm68200 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		ret = PTR_ERR(ctx->reset_gpio);
		dev_err(dev, "cannot get reset GPIO: %d\n", ret);
		return ret;
	}

	ctx->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(ctx->supply)) {
		ret = PTR_ERR(ctx->supply);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "cannot get regulator: %d\n", ret);
		return ret;
	}

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &rm68200_drm_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "mipi_dsi_attach() failed: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static int rm68200_remove(struct mipi_dsi_device *dsi)
{
	struct rm68200 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id raydium_rm68200_of_match[] = {
	{ .compatible = "raydium,rm68200" },
	{ }
};
MODULE_DEVICE_TABLE(of, raydium_rm68200_of_match);

static struct mipi_dsi_driver raydium_rm68200_driver = {
	.probe = rm68200_probe,
	.remove = rm68200_remove,
	.driver = {
		.name = "panel-raydium-rm68200",
		.of_match_table = raydium_rm68200_of_match,
	},
};
module_mipi_dsi_driver(raydium_rm68200_driver);

MODULE_AUTHOR("Philippe Cornu <philippe.cornu@st.com>");
MODULE_AUTHOR("Yannick Fertre <yannick.fertre@st.com>");
MODULE_DESCRIPTION("DRM Driver for Raydium RM68200 MIPI DSI panel");
MODULE_LICENSE("GPL v2");
