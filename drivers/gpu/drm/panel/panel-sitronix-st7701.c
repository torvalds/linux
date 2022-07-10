// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019, Amarula Solutions.
 * Author: Jagan Teki <jagan@amarulasolutions.com>
 */

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <linux/bitfield.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

/* Command2 BKx selection command */
#define DSI_CMD2BKX_SEL			0xFF

/* Command2, BK0 commands */
#define DSI_CMD2_BK0_PVGAMCTRL		0xB0 /* Positive Voltage Gamma Control */
#define DSI_CMD2_BK0_NVGAMCTRL		0xB1 /* Negative Voltage Gamma Control */
#define DSI_CMD2_BK0_LNESET		0xC0 /* Display Line setting */
#define DSI_CMD2_BK0_PORCTRL		0xC1 /* Porch control */
#define DSI_CMD2_BK0_INVSEL		0xC2 /* Inversion selection, Frame Rate Control */

/* Command2, BK1 commands */
#define DSI_CMD2_BK1_VRHS		0xB0 /* Vop amplitude setting */
#define DSI_CMD2_BK1_VCOM		0xB1 /* VCOM amplitude setting */
#define DSI_CMD2_BK1_VGHSS		0xB2 /* VGH Voltage setting */
#define DSI_CMD2_BK1_TESTCMD		0xB3 /* TEST Command Setting */
#define DSI_CMD2_BK1_VGLS		0xB5 /* VGL Voltage setting */
#define DSI_CMD2_BK1_PWCTLR1		0xB7 /* Power Control 1 */
#define DSI_CMD2_BK1_PWCTLR2		0xB8 /* Power Control 2 */
#define DSI_CMD2_BK1_SPD1		0xC1 /* Source pre_drive timing set1 */
#define DSI_CMD2_BK1_SPD2		0xC2 /* Source EQ2 Setting */
#define DSI_CMD2_BK1_MIPISET1		0xD0 /* MIPI Setting 1 */

/*
 * Command2 with BK function selection.
 *
 * BIT[4, 0]: [CN2, BKXSEL]
 * 10 = CMD2BK0, Command2 BK0
 * 11 = CMD2BK1, Command2 BK1
 * 00 = Command2 disable
 */
#define DSI_CMD2BK1_SEL			0x11
#define DSI_CMD2BK0_SEL			0x10
#define DSI_CMD2BKX_SEL_NONE		0x00

/* Command2, BK0 bytes */
#define DSI_CMD2_BK0_GAMCTRL_AJ_MASK	GENMASK(7, 6)
#define DSI_CMD2_BK0_GAMCTRL_VC0_MASK	GENMASK(3, 0)
#define DSI_CMD2_BK0_GAMCTRL_VC4_MASK	GENMASK(5, 0)
#define DSI_CMD2_BK0_GAMCTRL_VC8_MASK	GENMASK(5, 0)
#define DSI_CMD2_BK0_GAMCTRL_VC16_MASK	GENMASK(4, 0)
#define DSI_CMD2_BK0_GAMCTRL_VC24_MASK	GENMASK(4, 0)
#define DSI_CMD2_BK0_GAMCTRL_VC52_MASK	GENMASK(3, 0)
#define DSI_CMD2_BK0_GAMCTRL_VC80_MASK	GENMASK(5, 0)
#define DSI_CMD2_BK0_GAMCTRL_VC108_MASK	GENMASK(3, 0)
#define DSI_CMD2_BK0_GAMCTRL_VC147_MASK	GENMASK(3, 0)
#define DSI_CMD2_BK0_GAMCTRL_VC175_MASK	GENMASK(5, 0)
#define DSI_CMD2_BK0_GAMCTRL_VC203_MASK	GENMASK(3, 0)
#define DSI_CMD2_BK0_GAMCTRL_VC231_MASK	GENMASK(4, 0)
#define DSI_CMD2_BK0_GAMCTRL_VC239_MASK	GENMASK(4, 0)
#define DSI_CMD2_BK0_GAMCTRL_VC247_MASK	GENMASK(5, 0)
#define DSI_CMD2_BK0_GAMCTRL_VC251_MASK	GENMASK(5, 0)
#define DSI_CMD2_BK0_GAMCTRL_VC255_MASK	GENMASK(4, 0)
#define DSI_CMD2_BK0_LNESET_LINE_MASK	GENMASK(6, 0)
#define DSI_CMD2_BK0_LNESET_LDE_EN	BIT(7)
#define DSI_CMD2_BK0_LNESET_LINEDELTA	GENMASK(1, 0)
#define DSI_CMD2_BK0_PORCTRL_VBP_MASK	GENMASK(7, 0)
#define DSI_CMD2_BK0_PORCTRL_VFP_MASK	GENMASK(7, 0)
#define DSI_CMD2_BK0_INVSEL_ONES_MASK	GENMASK(5, 4)
#define DSI_CMD2_BK0_INVSEL_NLINV_MASK	GENMASK(2, 0)
#define DSI_CMD2_BK0_INVSEL_RTNI_MASK	GENMASK(4, 0)

/* Command2, BK1 bytes */
#define DSI_CMD2_BK1_VRHA_SET		0x45
#define DSI_CMD2_BK1_VCOM_SET		0x13
#define DSI_CMD2_BK1_VGHSS_SET		GENMASK(2, 0)
#define DSI_CMD2_BK1_TESTCMD_VAL	BIT(7)
#define DSI_VGLS_DEFAULT		BIT(6)
#define DSI_VGLS_SEL			GENMASK(2, 0)
#define DSI_CMD2_BK1_VGLS_SET		(DSI_VGLS_DEFAULT | DSI_VGLS_SEL)
#define DSI_PWCTLR1_AP			BIT(7) /* Gamma OP bias, max */
#define DSI_PWCTLR1_APIS		BIT(2) /* Source OP input bias, min */
#define DSI_PWCTLR1_APOS		BIT(0) /* Source OP output bias, min */
#define DSI_CMD2_BK1_PWCTLR1_SET	(DSI_PWCTLR1_AP | DSI_PWCTLR1_APIS | \
					DSI_PWCTLR1_APOS)
#define DSI_PWCTLR2_AVDD		BIT(5) /* AVDD 6.6v */
#define DSI_PWCTLR2_AVCL		0x0    /* AVCL -4.4v */
#define DSI_CMD2_BK1_PWCTLR2_SET	(DSI_PWCTLR2_AVDD | DSI_PWCTLR2_AVCL)
#define DSI_SPD1_T2D			BIT(3)
#define DSI_CMD2_BK1_SPD1_SET		(GENMASK(6, 4) | DSI_SPD1_T2D)
#define DSI_CMD2_BK1_SPD2_SET		DSI_CMD2_BK1_SPD1_SET
#define DSI_MIPISET1_EOT_EN		BIT(3)
#define DSI_CMD2_BK1_MIPISET1_SET	(BIT(7) | DSI_MIPISET1_EOT_EN)

#define CFIELD_PREP(_mask, _val)					\
	(((typeof(_mask))(_val) << (__builtin_ffsll(_mask) - 1)) & (_mask))

struct st7701_panel_desc {
	const struct drm_display_mode *mode;
	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
	unsigned int panel_sleep_delay;

	/* TFT matrix driver configuration, panel specific. */
	const u8	pv_gamma[16];	/* Positive voltage gamma control */
	const u8	nv_gamma[16];	/* Negative voltage gamma control */
	const u8	nlinv;		/* Inversion selection */
};

struct st7701 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct st7701_panel_desc *desc;

	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset;
	unsigned int sleep_delay;
};

static inline struct st7701 *panel_to_st7701(struct drm_panel *panel)
{
	return container_of(panel, struct st7701, panel);
}

static inline int st7701_dsi_write(struct st7701 *st7701, const void *seq,
				   size_t len)
{
	return mipi_dsi_dcs_write_buffer(st7701->dsi, seq, len);
}

#define ST7701_DSI(st7701, seq...)				\
	{							\
		const u8 d[] = { seq };				\
		st7701_dsi_write(st7701, d, ARRAY_SIZE(d));	\
	}

static void st7701_init_sequence(struct st7701 *st7701)
{
	const struct st7701_panel_desc *desc = st7701->desc;
	const struct drm_display_mode *mode = desc->mode;
	const u8 linecount8 = mode->vdisplay / 8;
	const u8 linecountrem2 = (mode->vdisplay % 8) / 2;

	ST7701_DSI(st7701, MIPI_DCS_SOFT_RESET, 0x00);

	/* We need to wait 5ms before sending new commands */
	msleep(5);

	ST7701_DSI(st7701, MIPI_DCS_EXIT_SLEEP_MODE, 0x00);

	msleep(st7701->sleep_delay);

	/* Command2, BK0 */
	ST7701_DSI(st7701, DSI_CMD2BKX_SEL,
		   0x77, 0x01, 0x00, 0x00, DSI_CMD2BK0_SEL);
	mipi_dsi_dcs_write(st7701->dsi, DSI_CMD2_BK0_PVGAMCTRL,
			   desc->pv_gamma, ARRAY_SIZE(desc->pv_gamma));
	mipi_dsi_dcs_write(st7701->dsi, DSI_CMD2_BK0_NVGAMCTRL,
			   desc->nv_gamma, ARRAY_SIZE(desc->nv_gamma));
	/*
	 * Vertical line count configuration:
	 * Line[6:0]: select number of vertical lines of the TFT matrix in
	 *            multiples of 8 lines
	 * LDE_EN: enable sub-8-line granularity line count
	 * Line_delta[1:0]: add 0/2/4/6 extra lines to line count selected
	 *                  using Line[6:0]
	 *
	 * Total number of vertical lines:
	 * LN = ((Line[6:0] + 1) * 8) + (LDE_EN ? Line_delta[1:0] * 2 : 0)
	 */
	ST7701_DSI(st7701, DSI_CMD2_BK0_LNESET,
		   FIELD_PREP(DSI_CMD2_BK0_LNESET_LINE_MASK, linecount8 - 1) |
		   (linecountrem2 ? DSI_CMD2_BK0_LNESET_LDE_EN : 0),
		   FIELD_PREP(DSI_CMD2_BK0_LNESET_LINEDELTA, linecountrem2));
	ST7701_DSI(st7701, DSI_CMD2_BK0_PORCTRL,
		   FIELD_PREP(DSI_CMD2_BK0_PORCTRL_VBP_MASK,
			      mode->vtotal - mode->vsync_end),
		   FIELD_PREP(DSI_CMD2_BK0_PORCTRL_VFP_MASK,
			      mode->vsync_start - mode->vdisplay));
	/*
	 * Horizontal pixel count configuration:
	 * PCLK = 512 + (RTNI[4:0] * 16)
	 * The PCLK is number of pixel clock per line, which matches
	 * mode htotal. The minimum is 512 PCLK.
	 */
	ST7701_DSI(st7701, DSI_CMD2_BK0_INVSEL,
		   DSI_CMD2_BK0_INVSEL_ONES_MASK |
		   FIELD_PREP(DSI_CMD2_BK0_INVSEL_NLINV_MASK, desc->nlinv),
		   FIELD_PREP(DSI_CMD2_BK0_INVSEL_RTNI_MASK,
			      DIV_ROUND_UP(mode->htotal, 16)));

	/* Command2, BK1 */
	ST7701_DSI(st7701, DSI_CMD2BKX_SEL,
			0x77, 0x01, 0x00, 0x00, DSI_CMD2BK1_SEL);
	ST7701_DSI(st7701, DSI_CMD2_BK1_VRHS, DSI_CMD2_BK1_VRHA_SET);
	ST7701_DSI(st7701, DSI_CMD2_BK1_VCOM, DSI_CMD2_BK1_VCOM_SET);
	ST7701_DSI(st7701, DSI_CMD2_BK1_VGHSS, DSI_CMD2_BK1_VGHSS_SET);
	ST7701_DSI(st7701, DSI_CMD2_BK1_TESTCMD, DSI_CMD2_BK1_TESTCMD_VAL);
	ST7701_DSI(st7701, DSI_CMD2_BK1_VGLS, DSI_CMD2_BK1_VGLS_SET);
	ST7701_DSI(st7701, DSI_CMD2_BK1_PWCTLR1, DSI_CMD2_BK1_PWCTLR1_SET);
	ST7701_DSI(st7701, DSI_CMD2_BK1_PWCTLR2, DSI_CMD2_BK1_PWCTLR2_SET);
	ST7701_DSI(st7701, DSI_CMD2_BK1_SPD1, DSI_CMD2_BK1_SPD1_SET);
	ST7701_DSI(st7701, DSI_CMD2_BK1_SPD2, DSI_CMD2_BK1_SPD2_SET);
	ST7701_DSI(st7701, DSI_CMD2_BK1_MIPISET1, DSI_CMD2_BK1_MIPISET1_SET);

	/**
	 * ST7701_SPEC_V1.2 is unable to provide enough information above this
	 * specific command sequence, so grab the same from vendor BSP driver.
	 */
	ST7701_DSI(st7701, 0xE0, 0x00, 0x00, 0x02);
	ST7701_DSI(st7701, 0xE1, 0x0B, 0x00, 0x0D, 0x00, 0x0C, 0x00, 0x0E,
		   0x00, 0x00, 0x44, 0x44);
	ST7701_DSI(st7701, 0xE2, 0x33, 0x33, 0x44, 0x44, 0x64, 0x00, 0x66,
		   0x00, 0x65, 0x00, 0x67, 0x00, 0x00);
	ST7701_DSI(st7701, 0xE3, 0x00, 0x00, 0x33, 0x33);
	ST7701_DSI(st7701, 0xE4, 0x44, 0x44);
	ST7701_DSI(st7701, 0xE5, 0x0C, 0x78, 0x3C, 0xA0, 0x0E, 0x78, 0x3C,
		   0xA0, 0x10, 0x78, 0x3C, 0xA0, 0x12, 0x78, 0x3C, 0xA0);
	ST7701_DSI(st7701, 0xE6, 0x00, 0x00, 0x33, 0x33);
	ST7701_DSI(st7701, 0xE7, 0x44, 0x44);
	ST7701_DSI(st7701, 0xE8, 0x0D, 0x78, 0x3C, 0xA0, 0x0F, 0x78, 0x3C,
		   0xA0, 0x11, 0x78, 0x3C, 0xA0, 0x13, 0x78, 0x3C, 0xA0);
	ST7701_DSI(st7701, 0xEB, 0x02, 0x02, 0x39, 0x39, 0xEE, 0x44, 0x00);
	ST7701_DSI(st7701, 0xEC, 0x00, 0x00);
	ST7701_DSI(st7701, 0xED, 0xFF, 0xF1, 0x04, 0x56, 0x72, 0x3F, 0xFF,
		   0xFF, 0xFF, 0xFF, 0xF3, 0x27, 0x65, 0x40, 0x1F, 0xFF);

	/* disable Command2 */
	ST7701_DSI(st7701, DSI_CMD2BKX_SEL,
		   0x77, 0x01, 0x00, 0x00, DSI_CMD2BKX_SEL_NONE);
}

static int st7701_prepare(struct drm_panel *panel)
{
	struct st7701 *st7701 = panel_to_st7701(panel);
	int ret;

	gpiod_set_value(st7701->reset, 0);

	ret = regulator_bulk_enable(ARRAY_SIZE(st7701->supplies),
				    st7701->supplies);
	if (ret < 0)
		return ret;
	msleep(20);

	gpiod_set_value(st7701->reset, 1);
	msleep(150);

	st7701_init_sequence(st7701);

	return 0;
}

static int st7701_enable(struct drm_panel *panel)
{
	struct st7701 *st7701 = panel_to_st7701(panel);

	ST7701_DSI(st7701, MIPI_DCS_SET_DISPLAY_ON, 0x00);

	return 0;
}

static int st7701_disable(struct drm_panel *panel)
{
	struct st7701 *st7701 = panel_to_st7701(panel);

	ST7701_DSI(st7701, MIPI_DCS_SET_DISPLAY_OFF, 0x00);

	return 0;
}

static int st7701_unprepare(struct drm_panel *panel)
{
	struct st7701 *st7701 = panel_to_st7701(panel);

	ST7701_DSI(st7701, MIPI_DCS_ENTER_SLEEP_MODE, 0x00);

	msleep(st7701->sleep_delay);

	gpiod_set_value(st7701->reset, 0);

	/**
	 * During the Resetting period, the display will be blanked
	 * (The display is entering blanking sequence, which maximum
	 * time is 120 ms, when Reset Starts in Sleep Out –mode. The
	 * display remains the blank state in Sleep In –mode.) and
	 * then return to Default condition for Hardware Reset.
	 *
	 * So we need wait sleep_delay time to make sure reset completed.
	 */
	msleep(st7701->sleep_delay);

	regulator_bulk_disable(ARRAY_SIZE(st7701->supplies), st7701->supplies);

	return 0;
}

static int st7701_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct st7701 *st7701 = panel_to_st7701(panel);
	const struct drm_display_mode *desc_mode = st7701->desc->mode;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, desc_mode);
	if (!mode) {
		dev_err(&st7701->dsi->dev, "failed to add mode %ux%u@%u\n",
			desc_mode->hdisplay, desc_mode->vdisplay,
			drm_mode_vrefresh(desc_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = desc_mode->width_mm;
	connector->display_info.height_mm = desc_mode->height_mm;

	return 1;
}

static const struct drm_panel_funcs st7701_funcs = {
	.disable	= st7701_disable,
	.unprepare	= st7701_unprepare,
	.prepare	= st7701_prepare,
	.enable		= st7701_enable,
	.get_modes	= st7701_get_modes,
};

static const struct drm_display_mode ts8550b_mode = {
	.clock		= 27500,

	.hdisplay	= 480,
	.hsync_start	= 480 + 38,
	.hsync_end	= 480 + 38 + 12,
	.htotal		= 480 + 38 + 12 + 12,

	.vdisplay	= 854,
	.vsync_start	= 854 + 18,
	.vsync_end	= 854 + 18 + 8,
	.vtotal		= 854 + 18 + 8 + 4,

	.width_mm	= 69,
	.height_mm	= 139,

	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct st7701_panel_desc ts8550b_desc = {
	.mode = &ts8550b_mode,
	.lanes = 2,
	.format = MIPI_DSI_FMT_RGB888,
	.panel_sleep_delay = 80, /* panel need extra 80ms for sleep out cmd */

	.pv_gamma = {
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_AJ_MASK, 0) |
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC0_MASK, 0),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_AJ_MASK, 0) |
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC4_MASK, 0xe),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_AJ_MASK, 0) |
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC8_MASK, 0x15),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC16_MASK, 0xf),

		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_AJ_MASK, 0) |
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC24_MASK, 0x11),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC52_MASK, 0x8),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC80_MASK, 0x8),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC108_MASK, 0x8),

		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC147_MASK, 0x8),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC175_MASK, 0x23),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC203_MASK, 0x4),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_AJ_MASK, 0) |
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC231_MASK, 0x13),

		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC239_MASK, 0x12),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_AJ_MASK, 0) |
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC247_MASK, 0x2b),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_AJ_MASK, 0) |
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC251_MASK, 0x34),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_AJ_MASK, 0) |
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC255_MASK, 0x1f)
	},
	.nv_gamma = {
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_AJ_MASK, 0) |
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC0_MASK, 0),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_AJ_MASK, 0) |
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC4_MASK, 0xe),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_AJ_MASK, 0x2) |
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC8_MASK, 0x15),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC16_MASK, 0xf),

		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_AJ_MASK, 0) |
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC24_MASK, 0x13),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC52_MASK, 0x7),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC80_MASK, 0x9),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC108_MASK, 0x8),

		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC147_MASK, 0x8),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC175_MASK, 0x22),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC203_MASK, 0x4),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_AJ_MASK, 0) |
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC231_MASK, 0x10),

		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC239_MASK, 0xe),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_AJ_MASK, 0) |
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC247_MASK, 0x2c),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_AJ_MASK, 0) |
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC251_MASK, 0x34),
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_AJ_MASK, 0) |
		CFIELD_PREP(DSI_CMD2_BK0_GAMCTRL_VC255_MASK, 0x1f)
	},
	.nlinv = 7,
};

static int st7701_dsi_probe(struct mipi_dsi_device *dsi)
{
	const struct st7701_panel_desc *desc;
	struct st7701 *st7701;
	int ret;

	st7701 = devm_kzalloc(&dsi->dev, sizeof(*st7701), GFP_KERNEL);
	if (!st7701)
		return -ENOMEM;

	desc = of_device_get_match_data(&dsi->dev);
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS;
	dsi->format = desc->format;
	dsi->lanes = desc->lanes;

	st7701->supplies[0].supply = "VCC";
	st7701->supplies[1].supply = "IOVCC";

	ret = devm_regulator_bulk_get(&dsi->dev, ARRAY_SIZE(st7701->supplies),
				      st7701->supplies);
	if (ret < 0)
		return ret;

	st7701->reset = devm_gpiod_get(&dsi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(st7701->reset)) {
		dev_err(&dsi->dev, "Couldn't get our reset GPIO\n");
		return PTR_ERR(st7701->reset);
	}

	drm_panel_init(&st7701->panel, &dsi->dev, &st7701_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	/**
	 * Once sleep out has been issued, ST7701 IC required to wait 120ms
	 * before initiating new commands.
	 *
	 * On top of that some panels might need an extra delay to wait, so
	 * add panel specific delay for those cases. As now this panel specific
	 * delay information is referenced from those panel BSP driver, example
	 * ts8550b and there is no valid documentation for that.
	 */
	st7701->sleep_delay = 120 + desc->panel_sleep_delay;

	ret = drm_panel_of_backlight(&st7701->panel);
	if (ret)
		return ret;

	drm_panel_add(&st7701->panel);

	mipi_dsi_set_drvdata(dsi, st7701);
	st7701->dsi = dsi;
	st7701->desc = desc;

	return mipi_dsi_attach(dsi);
}

static void st7701_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct st7701 *st7701 = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&st7701->panel);
}

static const struct of_device_id st7701_of_match[] = {
	{ .compatible = "techstar,ts8550b", .data = &ts8550b_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, st7701_of_match);

static struct mipi_dsi_driver st7701_dsi_driver = {
	.probe		= st7701_dsi_probe,
	.remove		= st7701_dsi_remove,
	.driver = {
		.name		= "st7701",
		.of_match_table	= st7701_of_match,
	},
};
module_mipi_dsi_driver(st7701_dsi_driver);

MODULE_AUTHOR("Jagan Teki <jagan@amarulasolutions.com>");
MODULE_DESCRIPTION("Sitronix ST7701 LCD Panel Driver");
MODULE_LICENSE("GPL");
