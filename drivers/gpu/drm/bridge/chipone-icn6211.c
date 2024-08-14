// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 Amarula Solutions(India)
 * Author: Jagan Teki <jagan@amarulasolutions.com>
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_mipi_dsi.h>

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#define VENDOR_ID		0x00
#define DEVICE_ID_H		0x01
#define DEVICE_ID_L		0x02
#define VERSION_ID		0x03
#define FIRMWARE_VERSION	0x08
#define CONFIG_FINISH		0x09
#define PD_CTRL(n)		(0x0a + ((n) & 0x3)) /* 0..3 */
#define RST_CTRL(n)		(0x0e + ((n) & 0x1)) /* 0..1 */
#define SYS_CTRL(n)		(0x10 + ((n) & 0x7)) /* 0..4 */
#define SYS_CTRL_1_CLK_PHASE_MSK	GENMASK(5, 4)
#define CLK_PHASE_0			0
#define CLK_PHASE_1_4			1
#define CLK_PHASE_1_2			2
#define CLK_PHASE_3_4			3
#define RGB_DRV(n)		(0x18 + ((n) & 0x3)) /* 0..3 */
#define RGB_DLY(n)		(0x1c + ((n) & 0x1)) /* 0..1 */
#define RGB_TEST_CTRL		0x1e
#define ATE_PLL_EN		0x1f
#define HACTIVE_LI		0x20
#define VACTIVE_LI		0x21
#define VACTIVE_HACTIVE_HI	0x22
#define HFP_LI			0x23
#define HSYNC_LI		0x24
#define HBP_LI			0x25
#define HFP_HSW_HBP_HI		0x26
#define HFP_HSW_HBP_HI_HFP(n)		(((n) & 0x300) >> 4)
#define HFP_HSW_HBP_HI_HS(n)		(((n) & 0x300) >> 6)
#define HFP_HSW_HBP_HI_HBP(n)		(((n) & 0x300) >> 8)
#define VFP			0x27
#define VSYNC			0x28
#define VBP			0x29
#define BIST_POL		0x2a
#define BIST_POL_BIST_MODE(n)		(((n) & 0xf) << 4)
#define BIST_POL_BIST_GEN		BIT(3)
#define BIST_POL_HSYNC_POL		BIT(2)
#define BIST_POL_VSYNC_POL		BIT(1)
#define BIST_POL_DE_POL			BIT(0)
#define BIST_RED		0x2b
#define BIST_GREEN		0x2c
#define BIST_BLUE		0x2d
#define BIST_CHESS_X		0x2e
#define BIST_CHESS_Y		0x2f
#define BIST_CHESS_XY_H		0x30
#define BIST_FRAME_TIME_L	0x31
#define BIST_FRAME_TIME_H	0x32
#define FIFO_MAX_ADDR_LOW	0x33
#define SYNC_EVENT_DLY		0x34
#define HSW_MIN			0x35
#define HFP_MIN			0x36
#define LOGIC_RST_NUM		0x37
#define OSC_CTRL(n)		(0x48 + ((n) & 0x7)) /* 0..5 */
#define BG_CTRL			0x4e
#define LDO_PLL			0x4f
#define PLL_CTRL(n)		(0x50 + ((n) & 0xf)) /* 0..15 */
#define PLL_CTRL_6_EXTERNAL		0x90
#define PLL_CTRL_6_MIPI_CLK		0x92
#define PLL_CTRL_6_INTERNAL		0x93
#define PLL_REM(n)		(0x60 + ((n) & 0x3)) /* 0..2 */
#define PLL_DIV(n)		(0x63 + ((n) & 0x3)) /* 0..2 */
#define PLL_FRAC(n)		(0x66 + ((n) & 0x3)) /* 0..2 */
#define PLL_INT(n)		(0x69 + ((n) & 0x1)) /* 0..1 */
#define PLL_REF_DIV		0x6b
#define PLL_REF_DIV_P(n)		((n) & 0xf)
#define PLL_REF_DIV_Pe			BIT(4)
#define PLL_REF_DIV_S(n)		(((n) & 0x7) << 5)
#define PLL_SSC_P(n)		(0x6c + ((n) & 0x3)) /* 0..2 */
#define PLL_SSC_STEP(n)		(0x6f + ((n) & 0x3)) /* 0..2 */
#define PLL_SSC_OFFSET(n)	(0x72 + ((n) & 0x3)) /* 0..3 */
#define GPIO_OEN		0x79
#define MIPI_CFG_PW		0x7a
#define MIPI_CFG_PW_CONFIG_DSI		0xc1
#define MIPI_CFG_PW_CONFIG_I2C		0x3e
#define GPIO_SEL(n)		(0x7b + ((n) & 0x1)) /* 0..1 */
#define IRQ_SEL			0x7d
#define DBG_SEL			0x7e
#define DBG_SIGNAL		0x7f
#define MIPI_ERR_VECTOR_L	0x80
#define MIPI_ERR_VECTOR_H	0x81
#define MIPI_ERR_VECTOR_EN_L	0x82
#define MIPI_ERR_VECTOR_EN_H	0x83
#define MIPI_MAX_SIZE_L		0x84
#define MIPI_MAX_SIZE_H		0x85
#define DSI_CTRL		0x86
#define DSI_CTRL_UNKNOWN		0x28
#define DSI_CTRL_DSI_LANES(n)		((n) & 0x3)
#define MIPI_PN_SWAP		0x87
#define MIPI_PN_SWAP_CLK		BIT(4)
#define MIPI_PN_SWAP_D(n)		BIT((n) & 0x3)
#define MIPI_SOT_SYNC_BIT(n)	(0x88 + ((n) & 0x1)) /* 0..1 */
#define MIPI_ULPS_CTRL		0x8a
#define MIPI_CLK_CHK_VAR	0x8e
#define MIPI_CLK_CHK_INI	0x8f
#define MIPI_T_TERM_EN		0x90
#define MIPI_T_HS_SETTLE	0x91
#define MIPI_T_TA_SURE_PRE	0x92
#define MIPI_T_LPX_SET		0x94
#define MIPI_T_CLK_MISS		0x95
#define MIPI_INIT_TIME_L	0x96
#define MIPI_INIT_TIME_H	0x97
#define MIPI_T_CLK_TERM_EN	0x99
#define MIPI_T_CLK_SETTLE	0x9a
#define MIPI_TO_HS_RX_L		0x9e
#define MIPI_TO_HS_RX_H		0x9f
#define MIPI_PHY(n)		(0xa0 + ((n) & 0x7)) /* 0..5 */
#define MIPI_PD_RX		0xb0
#define MIPI_PD_TERM		0xb1
#define MIPI_PD_HSRX		0xb2
#define MIPI_PD_LPTX		0xb3
#define MIPI_PD_LPRX		0xb4
#define MIPI_PD_CK_LANE		0xb5
#define MIPI_FORCE_0		0xb6
#define MIPI_RST_CTRL		0xb7
#define MIPI_RST_NUM		0xb8
#define MIPI_DBG_SET(n)		(0xc0 + ((n) & 0xf)) /* 0..9 */
#define MIPI_DBG_SEL		0xe0
#define MIPI_DBG_DATA		0xe1
#define MIPI_ATE_TEST_SEL	0xe2
#define MIPI_ATE_STATUS(n)	(0xe3 + ((n) & 0x1)) /* 0..1 */

struct chipone {
	struct device *dev;
	struct regmap *regmap;
	struct i2c_client *client;
	struct drm_bridge bridge;
	struct drm_display_mode mode;
	struct drm_bridge *panel_bridge;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *enable_gpio;
	struct regulator *vdd1;
	struct regulator *vdd2;
	struct regulator *vdd3;
	struct clk *refclk;
	unsigned long refclk_rate;
	bool interface_i2c;
};

static const struct regmap_range chipone_dsi_readable_ranges[] = {
	regmap_reg_range(VENDOR_ID, VERSION_ID),
	regmap_reg_range(FIRMWARE_VERSION, PLL_SSC_OFFSET(3)),
	regmap_reg_range(GPIO_OEN, MIPI_ULPS_CTRL),
	regmap_reg_range(MIPI_CLK_CHK_VAR, MIPI_T_TA_SURE_PRE),
	regmap_reg_range(MIPI_T_LPX_SET, MIPI_INIT_TIME_H),
	regmap_reg_range(MIPI_T_CLK_TERM_EN, MIPI_T_CLK_SETTLE),
	regmap_reg_range(MIPI_TO_HS_RX_L, MIPI_PHY(5)),
	regmap_reg_range(MIPI_PD_RX, MIPI_RST_NUM),
	regmap_reg_range(MIPI_DBG_SET(0), MIPI_DBG_SET(9)),
	regmap_reg_range(MIPI_DBG_SEL, MIPI_ATE_STATUS(1)),
};

static const struct regmap_access_table chipone_dsi_readable_table = {
	.yes_ranges = chipone_dsi_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(chipone_dsi_readable_ranges),
};

static const struct regmap_range chipone_dsi_writeable_ranges[] = {
	regmap_reg_range(CONFIG_FINISH, PLL_SSC_OFFSET(3)),
	regmap_reg_range(GPIO_OEN, MIPI_ULPS_CTRL),
	regmap_reg_range(MIPI_CLK_CHK_VAR, MIPI_T_TA_SURE_PRE),
	regmap_reg_range(MIPI_T_LPX_SET, MIPI_INIT_TIME_H),
	regmap_reg_range(MIPI_T_CLK_TERM_EN, MIPI_T_CLK_SETTLE),
	regmap_reg_range(MIPI_TO_HS_RX_L, MIPI_PHY(5)),
	regmap_reg_range(MIPI_PD_RX, MIPI_RST_NUM),
	regmap_reg_range(MIPI_DBG_SET(0), MIPI_DBG_SET(9)),
	regmap_reg_range(MIPI_DBG_SEL, MIPI_ATE_STATUS(1)),
};

static const struct regmap_access_table chipone_dsi_writeable_table = {
	.yes_ranges = chipone_dsi_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(chipone_dsi_writeable_ranges),
};

static const struct regmap_config chipone_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.rd_table = &chipone_dsi_readable_table,
	.wr_table = &chipone_dsi_writeable_table,
	.cache_type = REGCACHE_RBTREE,
	.max_register = MIPI_ATE_STATUS(1),
};

static int chipone_dsi_read(void *context,
			    const void *reg, size_t reg_size,
			    void *val, size_t val_size)
{
	struct mipi_dsi_device *dsi = context;
	const u16 reg16 = (val_size << 8) | *(u8 *)reg;
	int ret;

	ret = mipi_dsi_generic_read(dsi, &reg16, 2, val, val_size);

	return ret == val_size ? 0 : -EINVAL;
}

static int chipone_dsi_write(void *context, const void *data, size_t count)
{
	struct mipi_dsi_device *dsi = context;

	return mipi_dsi_generic_write(dsi, data, 2);
}

static const struct regmap_bus chipone_dsi_regmap_bus = {
	.read				= chipone_dsi_read,
	.write				= chipone_dsi_write,
	.reg_format_endian_default	= REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default	= REGMAP_ENDIAN_NATIVE,
};

static inline struct chipone *bridge_to_chipone(struct drm_bridge *bridge)
{
	return container_of(bridge, struct chipone, bridge);
}

static void chipone_readb(struct chipone *icn, u8 reg, u8 *val)
{
	int ret, pval;

	ret = regmap_read(icn->regmap, reg, &pval);

	*val = ret ? 0 : pval & 0xff;
}

static int chipone_writeb(struct chipone *icn, u8 reg, u8 val)
{
	return regmap_write(icn->regmap, reg, val);
}

static void chipone_configure_pll(struct chipone *icn,
				  const struct drm_display_mode *mode)
{
	unsigned int best_p = 0, best_m = 0, best_s = 0;
	unsigned int mode_clock = mode->clock * 1000;
	unsigned int delta, min_delta = 0xffffffff;
	unsigned int freq_p, freq_s, freq_out;
	unsigned int p_min, p_max;
	unsigned int p, m, s;
	unsigned int fin;
	bool best_p_pot;
	u8 ref_div;

	/*
	 * DSI byte clock frequency (input into PLL) is calculated as:
	 *  DSI_CLK = HS clock / 4
	 *
	 * DPI pixel clock frequency (output from PLL) is mode clock.
	 *
	 * The chip contains fractional PLL which works as follows:
	 *  DPI_CLK = ((DSI_CLK / P) * M) / S
	 * P is pre-divider, register PLL_REF_DIV[3:0] is 1:n divider
	 *                   register PLL_REF_DIV[4] is extra 1:2 divider
	 * M is integer multiplier, register PLL_INT(0) is multiplier
	 * S is post-divider, register PLL_REF_DIV[7:5] is 2^(n+1) divider
	 *
	 * It seems the PLL input clock after applying P pre-divider have
	 * to be lower than 20 MHz.
	 */
	if (icn->refclk)
		fin = icn->refclk_rate;
	else
		fin = icn->dsi->hs_rate / 4; /* in Hz */

	/* Minimum value of P predivider for PLL input in 5..20 MHz */
	p_min = clamp(DIV_ROUND_UP(fin, 20000000), 1U, 31U);
	p_max = clamp(fin / 5000000, 1U, 31U);

	for (p = p_min; p < p_max; p++) {	/* PLL_REF_DIV[4,3:0] */
		if (p > 16 && p & 1)		/* P > 16 uses extra /2 */
			continue;
		freq_p = fin / p;
		if (freq_p == 0)		/* Divider too high */
			break;

		for (s = 0; s < 0x7; s++) {	/* PLL_REF_DIV[7:5] */
			freq_s = freq_p / BIT(s + 1);
			if (freq_s == 0)	/* Divider too high */
				break;

			m = mode_clock / freq_s;

			/* Multiplier is 8 bit */
			if (m > 0xff)
				continue;

			/* Limit PLL VCO frequency to 1 GHz */
			freq_out = (fin * m) / p;
			if (freq_out > 1000000000)
				continue;

			/* Apply post-divider */
			freq_out /= BIT(s + 1);

			delta = abs(mode_clock - freq_out);
			if (delta < min_delta) {
				best_p = p;
				best_m = m;
				best_s = s;
				min_delta = delta;
			}
		}
	}

	best_p_pot = !(best_p & 1);

	dev_dbg(icn->dev,
		"PLL: P[3:0]=%d P[4]=2*%d M=%d S[7:5]=2^%d delta=%d => DSI f_in(%s)=%d Hz ; DPI f_out=%d Hz\n",
		best_p >> best_p_pot, best_p_pot, best_m, best_s + 1,
		min_delta, icn->refclk ? "EXT" : "DSI", fin,
		(fin * best_m) / (best_p << (best_s + 1)));

	ref_div = PLL_REF_DIV_P(best_p >> best_p_pot) | PLL_REF_DIV_S(best_s);
	if (best_p_pot)	/* Prefer /2 pre-divider */
		ref_div |= PLL_REF_DIV_Pe;

	/* Clock source selection either external clock or MIPI DSI clock lane */
	chipone_writeb(icn, PLL_CTRL(6),
		       icn->refclk ? PLL_CTRL_6_EXTERNAL : PLL_CTRL_6_MIPI_CLK);
	chipone_writeb(icn, PLL_REF_DIV, ref_div);
	chipone_writeb(icn, PLL_INT(0), best_m);
}

static void chipone_atomic_enable(struct drm_bridge *bridge,
				  struct drm_bridge_state *old_bridge_state)
{
	struct chipone *icn = bridge_to_chipone(bridge);
	struct drm_atomic_state *state = old_bridge_state->base.state;
	struct drm_display_mode *mode = &icn->mode;
	const struct drm_bridge_state *bridge_state;
	u16 hfp, hbp, hsync;
	u32 bus_flags;
	u8 pol, sys_ctrl_1, id[4];

	chipone_readb(icn, VENDOR_ID, id);
	chipone_readb(icn, DEVICE_ID_H, id + 1);
	chipone_readb(icn, DEVICE_ID_L, id + 2);
	chipone_readb(icn, VERSION_ID, id + 3);

	dev_dbg(icn->dev,
		"Chip IDs: Vendor=0x%02x Device=0x%02x:0x%02x Version=0x%02x\n",
		id[0], id[1], id[2], id[3]);

	if (id[0] != 0xc1 || id[1] != 0x62 || id[2] != 0x11) {
		dev_dbg(icn->dev, "Invalid Chip IDs, aborting configuration\n");
		return;
	}

	/* Get the DPI flags from the bridge state. */
	bridge_state = drm_atomic_get_new_bridge_state(state, bridge);
	bus_flags = bridge_state->output_bus_cfg.flags;

	if (icn->interface_i2c)
		chipone_writeb(icn, MIPI_CFG_PW, MIPI_CFG_PW_CONFIG_I2C);
	else
		chipone_writeb(icn, MIPI_CFG_PW, MIPI_CFG_PW_CONFIG_DSI);

	chipone_writeb(icn, HACTIVE_LI, mode->hdisplay & 0xff);

	chipone_writeb(icn, VACTIVE_LI, mode->vdisplay & 0xff);

	/*
	 * lsb nibble: 2nd nibble of hdisplay
	 * msb nibble: 2nd nibble of vdisplay
	 */
	chipone_writeb(icn, VACTIVE_HACTIVE_HI,
		       ((mode->hdisplay >> 8) & 0xf) |
		       (((mode->vdisplay >> 8) & 0xf) << 4));

	hfp = mode->hsync_start - mode->hdisplay;
	hsync = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;

	chipone_writeb(icn, HFP_LI, hfp & 0xff);
	chipone_writeb(icn, HSYNC_LI, hsync & 0xff);
	chipone_writeb(icn, HBP_LI, hbp & 0xff);
	/* Top two bits of Horizontal Front porch/Sync/Back porch */
	chipone_writeb(icn, HFP_HSW_HBP_HI,
		       HFP_HSW_HBP_HI_HFP(hfp) |
		       HFP_HSW_HBP_HI_HS(hsync) |
		       HFP_HSW_HBP_HI_HBP(hbp));

	chipone_writeb(icn, VFP, mode->vsync_start - mode->vdisplay);

	chipone_writeb(icn, VSYNC, mode->vsync_end - mode->vsync_start);

	chipone_writeb(icn, VBP, mode->vtotal - mode->vsync_end);

	/* dsi specific sequence */
	chipone_writeb(icn, SYNC_EVENT_DLY, 0x80);
	chipone_writeb(icn, HFP_MIN, hfp & 0xff);

	/* DSI data lane count */
	chipone_writeb(icn, DSI_CTRL,
		       DSI_CTRL_UNKNOWN | DSI_CTRL_DSI_LANES(icn->dsi->lanes - 1));

	chipone_writeb(icn, MIPI_PD_CK_LANE, 0xa0);
	chipone_writeb(icn, PLL_CTRL(12), 0xff);
	chipone_writeb(icn, MIPI_PN_SWAP, 0x00);

	/* DPI HS/VS/DE polarity */
	pol = ((mode->flags & DRM_MODE_FLAG_PHSYNC) ? BIST_POL_HSYNC_POL : 0) |
	      ((mode->flags & DRM_MODE_FLAG_PVSYNC) ? BIST_POL_VSYNC_POL : 0) |
	      ((bus_flags & DRM_BUS_FLAG_DE_HIGH) ? BIST_POL_DE_POL : 0);
	chipone_writeb(icn, BIST_POL, pol);

	/* Configure PLL settings */
	chipone_configure_pll(icn, mode);

	chipone_writeb(icn, SYS_CTRL(0), 0x40);
	sys_ctrl_1 = 0x88;

	if (bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE)
		sys_ctrl_1 |= FIELD_PREP(SYS_CTRL_1_CLK_PHASE_MSK, CLK_PHASE_0);
	else
		sys_ctrl_1 |= FIELD_PREP(SYS_CTRL_1_CLK_PHASE_MSK, CLK_PHASE_1_2);

	chipone_writeb(icn, SYS_CTRL(1), sys_ctrl_1);

	/* icn6211 specific sequence */
	chipone_writeb(icn, MIPI_FORCE_0, 0x20);
	chipone_writeb(icn, PLL_CTRL(1), 0x20);
	chipone_writeb(icn, CONFIG_FINISH, 0x10);

	usleep_range(10000, 11000);
}

static void chipone_atomic_pre_enable(struct drm_bridge *bridge,
				      struct drm_bridge_state *old_bridge_state)
{
	struct chipone *icn = bridge_to_chipone(bridge);
	int ret;

	if (icn->vdd1) {
		ret = regulator_enable(icn->vdd1);
		if (ret)
			DRM_DEV_ERROR(icn->dev,
				      "failed to enable VDD1 regulator: %d\n", ret);
	}

	if (icn->vdd2) {
		ret = regulator_enable(icn->vdd2);
		if (ret)
			DRM_DEV_ERROR(icn->dev,
				      "failed to enable VDD2 regulator: %d\n", ret);
	}

	if (icn->vdd3) {
		ret = regulator_enable(icn->vdd3);
		if (ret)
			DRM_DEV_ERROR(icn->dev,
				      "failed to enable VDD3 regulator: %d\n", ret);
	}

	ret = clk_prepare_enable(icn->refclk);
	if (ret)
		DRM_DEV_ERROR(icn->dev,
			      "failed to enable RECLK clock: %d\n", ret);

	gpiod_set_value(icn->enable_gpio, 1);

	usleep_range(10000, 11000);
}

static void chipone_atomic_post_disable(struct drm_bridge *bridge,
					struct drm_bridge_state *old_bridge_state)
{
	struct chipone *icn = bridge_to_chipone(bridge);

	clk_disable_unprepare(icn->refclk);

	if (icn->vdd1)
		regulator_disable(icn->vdd1);

	if (icn->vdd2)
		regulator_disable(icn->vdd2);

	if (icn->vdd3)
		regulator_disable(icn->vdd3);

	gpiod_set_value(icn->enable_gpio, 0);
}

static void chipone_mode_set(struct drm_bridge *bridge,
			     const struct drm_display_mode *mode,
			     const struct drm_display_mode *adjusted_mode)
{
	struct chipone *icn = bridge_to_chipone(bridge);

	drm_mode_copy(&icn->mode, adjusted_mode);
};

static int chipone_dsi_attach(struct chipone *icn)
{
	struct mipi_dsi_device *dsi = icn->dsi;
	struct device *dev = icn->dev;
	int dsi_lanes, ret;

	dsi_lanes = drm_of_get_data_lanes_count_ep(dev->of_node, 0, 0, 1, 4);

	/*
	 * If the 'data-lanes' property does not exist in DT or is invalid,
	 * default to previously hard-coded behavior, which was 4 data lanes.
	 */
	if (dsi_lanes < 0)
		icn->dsi->lanes = 4;
	else
		icn->dsi->lanes = dsi_lanes;

	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET;
	dsi->hs_rate = 500000000;
	dsi->lp_rate = 16000000;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		dev_err(icn->dev, "failed to attach dsi\n");

	return ret;
}

static int chipone_dsi_host_attach(struct chipone *icn)
{
	struct device *dev = icn->dev;
	struct device_node *host_node;
	struct device_node *endpoint;
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	int ret = 0;

	const struct mipi_dsi_device_info info = {
		.type = "chipone",
		.channel = 0,
		.node = NULL,
	};

	endpoint = of_graph_get_endpoint_by_regs(dev->of_node, 0, 0);
	host_node = of_graph_get_remote_port_parent(endpoint);
	of_node_put(endpoint);

	if (!host_node)
		return -EINVAL;

	host = of_find_mipi_dsi_host_by_node(host_node);
	of_node_put(host_node);
	if (!host)
		return dev_err_probe(dev, -EPROBE_DEFER, "failed to find dsi host\n");

	dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(dsi)) {
		return dev_err_probe(dev, PTR_ERR(dsi),
				     "failed to create dsi device\n");
	}

	icn->dsi = dsi;

	ret = chipone_dsi_attach(icn);
	if (ret < 0)
		mipi_dsi_device_unregister(dsi);

	return ret;
}

static int chipone_attach(struct drm_bridge *bridge, enum drm_bridge_attach_flags flags)
{
	struct chipone *icn = bridge_to_chipone(bridge);

	return drm_bridge_attach(bridge->encoder, icn->panel_bridge, bridge, flags);
}

#define MAX_INPUT_SEL_FORMATS	1

static u32 *
chipone_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
				  struct drm_bridge_state *bridge_state,
				  struct drm_crtc_state *crtc_state,
				  struct drm_connector_state *conn_state,
				  u32 output_fmt,
				  unsigned int *num_input_fmts)
{
	u32 *input_fmts;

	*num_input_fmts = 0;

	input_fmts = kcalloc(MAX_INPUT_SEL_FORMATS, sizeof(*input_fmts),
			     GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	/* This is the DSI-end bus format */
	input_fmts[0] = MEDIA_BUS_FMT_RGB888_1X24;
	*num_input_fmts = 1;

	return input_fmts;
}

static const struct drm_bridge_funcs chipone_bridge_funcs = {
	.atomic_duplicate_state	= drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_bridge_destroy_state,
	.atomic_reset		= drm_atomic_helper_bridge_reset,
	.atomic_pre_enable	= chipone_atomic_pre_enable,
	.atomic_enable		= chipone_atomic_enable,
	.atomic_post_disable	= chipone_atomic_post_disable,
	.mode_set		= chipone_mode_set,
	.attach			= chipone_attach,
	.atomic_get_input_bus_fmts = chipone_atomic_get_input_bus_fmts,
};

static int chipone_parse_dt(struct chipone *icn)
{
	struct device *dev = icn->dev;
	int ret;

	icn->refclk = devm_clk_get_optional(dev, "refclk");
	if (IS_ERR(icn->refclk)) {
		ret = PTR_ERR(icn->refclk);
		DRM_DEV_ERROR(dev, "failed to get REFCLK clock: %d\n", ret);
		return ret;
	} else if (icn->refclk) {
		icn->refclk_rate = clk_get_rate(icn->refclk);
		if (icn->refclk_rate < 10000000 || icn->refclk_rate > 154000000) {
			DRM_DEV_ERROR(dev, "REFCLK out of range: %ld Hz\n",
				      icn->refclk_rate);
			return -EINVAL;
		}
	}

	icn->vdd1 = devm_regulator_get_optional(dev, "vdd1");
	if (IS_ERR(icn->vdd1)) {
		ret = PTR_ERR(icn->vdd1);
		if (ret == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		icn->vdd1 = NULL;
		DRM_DEV_DEBUG(dev, "failed to get VDD1 regulator: %d\n", ret);
	}

	icn->vdd2 = devm_regulator_get_optional(dev, "vdd2");
	if (IS_ERR(icn->vdd2)) {
		ret = PTR_ERR(icn->vdd2);
		if (ret == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		icn->vdd2 = NULL;
		DRM_DEV_DEBUG(dev, "failed to get VDD2 regulator: %d\n", ret);
	}

	icn->vdd3 = devm_regulator_get_optional(dev, "vdd3");
	if (IS_ERR(icn->vdd3)) {
		ret = PTR_ERR(icn->vdd3);
		if (ret == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		icn->vdd3 = NULL;
		DRM_DEV_DEBUG(dev, "failed to get VDD3 regulator: %d\n", ret);
	}

	icn->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(icn->enable_gpio)) {
		DRM_DEV_ERROR(dev, "failed to get enable GPIO\n");
		return PTR_ERR(icn->enable_gpio);
	}

	icn->panel_bridge = devm_drm_of_get_bridge(dev, dev->of_node, 1, 0);
	if (IS_ERR(icn->panel_bridge))
		return PTR_ERR(icn->panel_bridge);

	return 0;
}

static int chipone_common_probe(struct device *dev, struct chipone **icnr)
{
	struct chipone *icn;
	int ret;

	icn = devm_kzalloc(dev, sizeof(struct chipone), GFP_KERNEL);
	if (!icn)
		return -ENOMEM;

	icn->dev = dev;

	ret = chipone_parse_dt(icn);
	if (ret)
		return ret;

	icn->bridge.funcs = &chipone_bridge_funcs;
	icn->bridge.type = DRM_MODE_CONNECTOR_DPI;
	icn->bridge.of_node = dev->of_node;

	*icnr = icn;

	return ret;
}

static int chipone_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct chipone *icn;
	int ret;

	ret = chipone_common_probe(dev, &icn);
	if (ret)
		return ret;

	icn->regmap = devm_regmap_init(dev, &chipone_dsi_regmap_bus,
				       dsi, &chipone_regmap_config);
	if (IS_ERR(icn->regmap))
		return PTR_ERR(icn->regmap);

	icn->interface_i2c = false;
	icn->dsi = dsi;

	mipi_dsi_set_drvdata(dsi, icn);

	drm_bridge_add(&icn->bridge);

	ret = chipone_dsi_attach(icn);
	if (ret)
		drm_bridge_remove(&icn->bridge);

	return ret;
}

static int chipone_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct chipone *icn;
	int ret;

	ret = chipone_common_probe(dev, &icn);
	if (ret)
		return ret;

	icn->regmap = devm_regmap_init_i2c(client, &chipone_regmap_config);
	if (IS_ERR(icn->regmap))
		return PTR_ERR(icn->regmap);

	icn->interface_i2c = true;
	icn->client = client;
	dev_set_drvdata(dev, icn);
	i2c_set_clientdata(client, icn);

	drm_bridge_add(&icn->bridge);

	return chipone_dsi_host_attach(icn);
}

static void chipone_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct chipone *icn = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_bridge_remove(&icn->bridge);
}

static const struct of_device_id chipone_of_match[] = {
	{ .compatible = "chipone,icn6211", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, chipone_of_match);

static struct mipi_dsi_driver chipone_dsi_driver = {
	.probe = chipone_dsi_probe,
	.remove = chipone_dsi_remove,
	.driver = {
		.name = "chipone-icn6211",
		.owner = THIS_MODULE,
		.of_match_table = chipone_of_match,
	},
};

static struct i2c_device_id chipone_i2c_id[] = {
	{ "chipone,icn6211" },
	{},
};
MODULE_DEVICE_TABLE(i2c, chipone_i2c_id);

static struct i2c_driver chipone_i2c_driver = {
	.probe = chipone_i2c_probe,
	.id_table = chipone_i2c_id,
	.driver = {
		.name = "chipone-icn6211-i2c",
		.of_match_table = chipone_of_match,
	},
};

static int __init chipone_init(void)
{
	if (IS_ENABLED(CONFIG_DRM_MIPI_DSI))
		mipi_dsi_driver_register(&chipone_dsi_driver);

	return i2c_add_driver(&chipone_i2c_driver);
}
module_init(chipone_init);

static void __exit chipone_exit(void)
{
	i2c_del_driver(&chipone_i2c_driver);

	if (IS_ENABLED(CONFIG_DRM_MIPI_DSI))
		mipi_dsi_driver_unregister(&chipone_dsi_driver);
}
module_exit(chipone_exit);

MODULE_AUTHOR("Jagan Teki <jagan@amarulasolutions.com>");
MODULE_DESCRIPTION("Chipone ICN6211 MIPI-DSI to RGB Converter Bridge");
MODULE_LICENSE("GPL");
