// SPDX-License-Identifier: GPL-2.0
/*
 * rcar_lvds.c  --  R-Car LVDS Encoder
 *
 * Copyright (C) 2013-2018 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include "rcar_lvds.h"
#include "rcar_lvds_regs.h"

struct rcar_lvds;

/* Keep in sync with the LVDCR0.LVMD hardware register values. */
enum rcar_lvds_mode {
	RCAR_LVDS_MODE_JEIDA = 0,
	RCAR_LVDS_MODE_MIRROR = 1,
	RCAR_LVDS_MODE_VESA = 4,
};

#define RCAR_LVDS_QUIRK_LANES		BIT(0)	/* LVDS lanes 1 and 3 inverted */
#define RCAR_LVDS_QUIRK_GEN3_LVEN	BIT(1)	/* LVEN bit needs to be set on R8A77970/R8A7799x */
#define RCAR_LVDS_QUIRK_PWD		BIT(2)	/* PWD bit available (all of Gen3 but E3) */
#define RCAR_LVDS_QUIRK_EXT_PLL		BIT(3)	/* Has extended PLL */
#define RCAR_LVDS_QUIRK_DUAL_LINK	BIT(4)	/* Supports dual-link operation */

struct rcar_lvds_device_info {
	unsigned int gen;
	unsigned int quirks;
	void (*pll_setup)(struct rcar_lvds *lvds, unsigned int freq);
};

struct rcar_lvds {
	struct device *dev;
	const struct rcar_lvds_device_info *info;

	struct drm_bridge bridge;

	struct drm_bridge *next_bridge;
	struct drm_connector connector;
	struct drm_panel *panel;

	void __iomem *mmio;
	struct {
		struct clk *mod;		/* CPG module clock */
		struct clk *extal;		/* External clock */
		struct clk *dotclkin[2];	/* External DU clocks */
	} clocks;

	struct drm_display_mode display_mode;
	enum rcar_lvds_mode mode;

	struct drm_bridge *companion;
	bool dual_link;
};

#define bridge_to_rcar_lvds(bridge) \
	container_of(bridge, struct rcar_lvds, bridge)

#define connector_to_rcar_lvds(connector) \
	container_of(connector, struct rcar_lvds, connector)

static void rcar_lvds_write(struct rcar_lvds *lvds, u32 reg, u32 data)
{
	iowrite32(data, lvds->mmio + reg);
}

/* -----------------------------------------------------------------------------
 * Connector & Panel
 */

static int rcar_lvds_connector_get_modes(struct drm_connector *connector)
{
	struct rcar_lvds *lvds = connector_to_rcar_lvds(connector);

	return drm_panel_get_modes(lvds->panel);
}

static int rcar_lvds_connector_atomic_check(struct drm_connector *connector,
					    struct drm_atomic_state *state)
{
	struct rcar_lvds *lvds = connector_to_rcar_lvds(connector);
	const struct drm_display_mode *panel_mode;
	struct drm_connector_state *conn_state;
	struct drm_crtc_state *crtc_state;

	conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (!conn_state->crtc)
		return 0;

	if (list_empty(&connector->modes)) {
		dev_dbg(lvds->dev, "connector: empty modes list\n");
		return -EINVAL;
	}

	panel_mode = list_first_entry(&connector->modes,
				      struct drm_display_mode, head);

	/* We're not allowed to modify the resolution. */
	crtc_state = drm_atomic_get_crtc_state(state, conn_state->crtc);
	if (!crtc_state)
		return -EINVAL;

	if (crtc_state->mode.hdisplay != panel_mode->hdisplay ||
	    crtc_state->mode.vdisplay != panel_mode->vdisplay)
		return -EINVAL;

	/* The flat panel mode is fixed, just copy it to the adjusted mode. */
	drm_mode_copy(&crtc_state->adjusted_mode, panel_mode);

	return 0;
}

static const struct drm_connector_helper_funcs rcar_lvds_conn_helper_funcs = {
	.get_modes = rcar_lvds_connector_get_modes,
	.atomic_check = rcar_lvds_connector_atomic_check,
};

static const struct drm_connector_funcs rcar_lvds_conn_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

/* -----------------------------------------------------------------------------
 * PLL Setup
 */

static void rcar_lvds_pll_setup_gen2(struct rcar_lvds *lvds, unsigned int freq)
{
	u32 val;

	if (freq < 39000000)
		val = LVDPLLCR_CEEN | LVDPLLCR_COSEL | LVDPLLCR_PLLDLYCNT_38M;
	else if (freq < 61000000)
		val = LVDPLLCR_CEEN | LVDPLLCR_COSEL | LVDPLLCR_PLLDLYCNT_60M;
	else if (freq < 121000000)
		val = LVDPLLCR_CEEN | LVDPLLCR_COSEL | LVDPLLCR_PLLDLYCNT_121M;
	else
		val = LVDPLLCR_PLLDLYCNT_150M;

	rcar_lvds_write(lvds, LVDPLLCR, val);
}

static void rcar_lvds_pll_setup_gen3(struct rcar_lvds *lvds, unsigned int freq)
{
	u32 val;

	if (freq < 42000000)
		val = LVDPLLCR_PLLDIVCNT_42M;
	else if (freq < 85000000)
		val = LVDPLLCR_PLLDIVCNT_85M;
	else if (freq < 128000000)
		val = LVDPLLCR_PLLDIVCNT_128M;
	else
		val = LVDPLLCR_PLLDIVCNT_148M;

	rcar_lvds_write(lvds, LVDPLLCR, val);
}

struct pll_info {
	unsigned long diff;
	unsigned int pll_m;
	unsigned int pll_n;
	unsigned int pll_e;
	unsigned int div;
	u32 clksel;
};

static void rcar_lvds_d3_e3_pll_calc(struct rcar_lvds *lvds, struct clk *clk,
				     unsigned long target, struct pll_info *pll,
				     u32 clksel, bool dot_clock_only)
{
	unsigned int div7 = dot_clock_only ? 1 : 7;
	unsigned long output;
	unsigned long fin;
	unsigned int m_min;
	unsigned int m_max;
	unsigned int m;
	int error;

	if (!clk)
		return;

	/*
	 * The LVDS PLL is made of a pre-divider and a multiplier (strangely
	 * enough called M and N respectively), followed by a post-divider E.
	 *
	 *         ,-----.         ,-----.     ,-----.         ,-----.
	 * Fin --> | 1/M | -Fpdf-> | PFD | --> | VCO | -Fvco-> | 1/E | --> Fout
	 *         `-----'     ,-> |     |     `-----'   |     `-----'
	 *                     |   `-----'               |
	 *                     |         ,-----.         |
	 *                     `-------- | 1/N | <-------'
	 *                               `-----'
	 *
	 * The clock output by the PLL is then further divided by a programmable
	 * divider DIV to achieve the desired target frequency. Finally, an
	 * optional fixed /7 divider is used to convert the bit clock to a pixel
	 * clock (as LVDS transmits 7 bits per lane per clock sample).
	 *
	 *          ,-------.     ,-----.     |\
	 * Fout --> | 1/DIV | --> | 1/7 | --> | |
	 *          `-------'  |  `-----'     | | --> dot clock
	 *                     `------------> | |
	 *                                    |/
	 *
	 * The /7 divider is optional, it is enabled when the LVDS PLL is used
	 * to drive the LVDS encoder, and disabled when  used to generate a dot
	 * clock for the DU RGB output, without using the LVDS encoder.
	 *
	 * The PLL allowed input frequency range is 12 MHz to 192 MHz.
	 */

	fin = clk_get_rate(clk);
	if (fin < 12000000 || fin > 192000000)
		return;

	/*
	 * The comparison frequency range is 12 MHz to 24 MHz, which limits the
	 * allowed values for the pre-divider M (normal range 1-8).
	 *
	 * Fpfd = Fin / M
	 */
	m_min = max_t(unsigned int, 1, DIV_ROUND_UP(fin, 24000000));
	m_max = min_t(unsigned int, 8, fin / 12000000);

	for (m = m_min; m <= m_max; ++m) {
		unsigned long fpfd;
		unsigned int n_min;
		unsigned int n_max;
		unsigned int n;

		/*
		 * The VCO operating range is 900 Mhz to 1800 MHz, which limits
		 * the allowed values for the multiplier N (normal range
		 * 60-120).
		 *
		 * Fvco = Fin * N / M
		 */
		fpfd = fin / m;
		n_min = max_t(unsigned int, 60, DIV_ROUND_UP(900000000, fpfd));
		n_max = min_t(unsigned int, 120, 1800000000 / fpfd);

		for (n = n_min; n < n_max; ++n) {
			unsigned long fvco;
			unsigned int e_min;
			unsigned int e;

			/*
			 * The output frequency is limited to 1039.5 MHz,
			 * limiting again the allowed values for the
			 * post-divider E (normal value 1, 2 or 4).
			 *
			 * Fout = Fvco / E
			 */
			fvco = fpfd * n;
			e_min = fvco > 1039500000 ? 1 : 0;

			for (e = e_min; e < 3; ++e) {
				unsigned long fout;
				unsigned long diff;
				unsigned int div;

				/*
				 * Finally we have a programable divider after
				 * the PLL, followed by a an optional fixed /7
				 * divider.
				 */
				fout = fvco / (1 << e) / div7;
				div = max(1UL, DIV_ROUND_CLOSEST(fout, target));
				diff = abs(fout / div - target);

				if (diff < pll->diff) {
					pll->diff = diff;
					pll->pll_m = m;
					pll->pll_n = n;
					pll->pll_e = e;
					pll->div = div;
					pll->clksel = clksel;

					if (diff == 0)
						goto done;
				}
			}
		}
	}

done:
	output = fin * pll->pll_n / pll->pll_m / (1 << pll->pll_e)
	       / div7 / pll->div;
	error = (long)(output - target) * 10000 / (long)target;

	dev_dbg(lvds->dev,
		"%pC %lu Hz -> Fout %lu Hz (target %lu Hz, error %d.%02u%%), PLL M/N/E/DIV %u/%u/%u/%u\n",
		clk, fin, output, target, error / 100,
		error < 0 ? -error % 100 : error % 100,
		pll->pll_m, pll->pll_n, pll->pll_e, pll->div);
}

static void __rcar_lvds_pll_setup_d3_e3(struct rcar_lvds *lvds,
					unsigned int freq, bool dot_clock_only)
{
	struct pll_info pll = { .diff = (unsigned long)-1 };
	u32 lvdpllcr;

	rcar_lvds_d3_e3_pll_calc(lvds, lvds->clocks.dotclkin[0], freq, &pll,
				 LVDPLLCR_CKSEL_DU_DOTCLKIN(0), dot_clock_only);
	rcar_lvds_d3_e3_pll_calc(lvds, lvds->clocks.dotclkin[1], freq, &pll,
				 LVDPLLCR_CKSEL_DU_DOTCLKIN(1), dot_clock_only);
	rcar_lvds_d3_e3_pll_calc(lvds, lvds->clocks.extal, freq, &pll,
				 LVDPLLCR_CKSEL_EXTAL, dot_clock_only);

	lvdpllcr = LVDPLLCR_PLLON | pll.clksel | LVDPLLCR_CLKOUT
		 | LVDPLLCR_PLLN(pll.pll_n - 1) | LVDPLLCR_PLLM(pll.pll_m - 1);

	if (pll.pll_e > 0)
		lvdpllcr |= LVDPLLCR_STP_CLKOUTE | LVDPLLCR_OUTCLKSEL
			 |  LVDPLLCR_PLLE(pll.pll_e - 1);

	if (dot_clock_only)
		lvdpllcr |= LVDPLLCR_OCKSEL;

	rcar_lvds_write(lvds, LVDPLLCR, lvdpllcr);

	if (pll.div > 1)
		/*
		 * The DIVRESET bit is a misnomer, setting it to 1 deasserts the
		 * divisor reset.
		 */
		rcar_lvds_write(lvds, LVDDIV, LVDDIV_DIVSEL |
				LVDDIV_DIVRESET | LVDDIV_DIV(pll.div - 1));
	else
		rcar_lvds_write(lvds, LVDDIV, 0);
}

static void rcar_lvds_pll_setup_d3_e3(struct rcar_lvds *lvds, unsigned int freq)
{
	__rcar_lvds_pll_setup_d3_e3(lvds, freq, false);
}

/* -----------------------------------------------------------------------------
 * Clock - D3/E3 only
 */

int rcar_lvds_clk_enable(struct drm_bridge *bridge, unsigned long freq)
{
	struct rcar_lvds *lvds = bridge_to_rcar_lvds(bridge);
	int ret;

	if (WARN_ON(!(lvds->info->quirks & RCAR_LVDS_QUIRK_EXT_PLL)))
		return -ENODEV;

	dev_dbg(lvds->dev, "enabling LVDS PLL, freq=%luHz\n", freq);

	ret = clk_prepare_enable(lvds->clocks.mod);
	if (ret < 0)
		return ret;

	__rcar_lvds_pll_setup_d3_e3(lvds, freq, true);

	return 0;
}
EXPORT_SYMBOL_GPL(rcar_lvds_clk_enable);

void rcar_lvds_clk_disable(struct drm_bridge *bridge)
{
	struct rcar_lvds *lvds = bridge_to_rcar_lvds(bridge);

	if (WARN_ON(!(lvds->info->quirks & RCAR_LVDS_QUIRK_EXT_PLL)))
		return;

	dev_dbg(lvds->dev, "disabling LVDS PLL\n");

	rcar_lvds_write(lvds, LVDPLLCR, 0);

	clk_disable_unprepare(lvds->clocks.mod);
}
EXPORT_SYMBOL_GPL(rcar_lvds_clk_disable);

/* -----------------------------------------------------------------------------
 * Bridge
 */

static void rcar_lvds_enable(struct drm_bridge *bridge)
{
	struct rcar_lvds *lvds = bridge_to_rcar_lvds(bridge);
	const struct drm_display_mode *mode = &lvds->display_mode;
	u32 lvdhcr;
	u32 lvdcr0;
	int ret;

	ret = clk_prepare_enable(lvds->clocks.mod);
	if (ret < 0)
		return;

	/* Enable the companion LVDS encoder in dual-link mode. */
	if (lvds->dual_link && lvds->companion)
		lvds->companion->funcs->enable(lvds->companion);

	/*
	 * Hardcode the channels and control signals routing for now.
	 *
	 * HSYNC -> CTRL0
	 * VSYNC -> CTRL1
	 * DISP  -> CTRL2
	 * 0     -> CTRL3
	 */
	rcar_lvds_write(lvds, LVDCTRCR, LVDCTRCR_CTR3SEL_ZERO |
			LVDCTRCR_CTR2SEL_DISP | LVDCTRCR_CTR1SEL_VSYNC |
			LVDCTRCR_CTR0SEL_HSYNC);

	if (lvds->info->quirks & RCAR_LVDS_QUIRK_LANES)
		lvdhcr = LVDCHCR_CHSEL_CH(0, 0) | LVDCHCR_CHSEL_CH(1, 3)
		       | LVDCHCR_CHSEL_CH(2, 2) | LVDCHCR_CHSEL_CH(3, 1);
	else
		lvdhcr = LVDCHCR_CHSEL_CH(0, 0) | LVDCHCR_CHSEL_CH(1, 1)
		       | LVDCHCR_CHSEL_CH(2, 2) | LVDCHCR_CHSEL_CH(3, 3);

	rcar_lvds_write(lvds, LVDCHCR, lvdhcr);

	if (lvds->info->quirks & RCAR_LVDS_QUIRK_DUAL_LINK) {
		/*
		 * Configure vertical stripe based on the mode of operation of
		 * the connected device.
		 */
		rcar_lvds_write(lvds, LVDSTRIPE,
				lvds->dual_link ? LVDSTRIPE_ST_ON : 0);
	}

	/*
	 * PLL clock configuration on all instances but the companion in
	 * dual-link mode.
	 */
	if (!lvds->dual_link || lvds->companion)
		lvds->info->pll_setup(lvds, mode->clock * 1000);

	/* Set the LVDS mode and select the input. */
	lvdcr0 = lvds->mode << LVDCR0_LVMD_SHIFT;

	if (lvds->bridge.encoder) {
		/*
		 * FIXME: We should really retrieve the CRTC through the state,
		 * but how do we get a state pointer?
		 */
		if (drm_crtc_index(lvds->bridge.encoder->crtc) == 2)
			lvdcr0 |= LVDCR0_DUSEL;
	}

	rcar_lvds_write(lvds, LVDCR0, lvdcr0);

	/* Turn all the channels on. */
	rcar_lvds_write(lvds, LVDCR1,
			LVDCR1_CHSTBY(3) | LVDCR1_CHSTBY(2) |
			LVDCR1_CHSTBY(1) | LVDCR1_CHSTBY(0) | LVDCR1_CLKSTBY);

	if (lvds->info->gen < 3) {
		/* Enable LVDS operation and turn the bias circuitry on. */
		lvdcr0 |= LVDCR0_BEN | LVDCR0_LVEN;
		rcar_lvds_write(lvds, LVDCR0, lvdcr0);
	}

	if (!(lvds->info->quirks & RCAR_LVDS_QUIRK_EXT_PLL)) {
		/*
		 * Turn the PLL on (simple PLL only, extended PLL is fully
		 * controlled through LVDPLLCR).
		 */
		lvdcr0 |= LVDCR0_PLLON;
		rcar_lvds_write(lvds, LVDCR0, lvdcr0);
	}

	if (lvds->info->quirks & RCAR_LVDS_QUIRK_PWD) {
		/* Set LVDS normal mode. */
		lvdcr0 |= LVDCR0_PWD;
		rcar_lvds_write(lvds, LVDCR0, lvdcr0);
	}

	if (lvds->info->quirks & RCAR_LVDS_QUIRK_GEN3_LVEN) {
		/*
		 * Turn on the LVDS PHY. On D3, the LVEN and LVRES bit must be
		 * set at the same time, so don't write the register yet.
		 */
		lvdcr0 |= LVDCR0_LVEN;
		if (!(lvds->info->quirks & RCAR_LVDS_QUIRK_PWD))
			rcar_lvds_write(lvds, LVDCR0, lvdcr0);
	}

	if (!(lvds->info->quirks & RCAR_LVDS_QUIRK_EXT_PLL)) {
		/* Wait for the PLL startup delay (simple PLL only). */
		usleep_range(100, 150);
	}

	/* Turn the output on. */
	lvdcr0 |= LVDCR0_LVRES;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);

	if (lvds->panel) {
		drm_panel_prepare(lvds->panel);
		drm_panel_enable(lvds->panel);
	}
}

static void rcar_lvds_disable(struct drm_bridge *bridge)
{
	struct rcar_lvds *lvds = bridge_to_rcar_lvds(bridge);

	if (lvds->panel) {
		drm_panel_disable(lvds->panel);
		drm_panel_unprepare(lvds->panel);
	}

	rcar_lvds_write(lvds, LVDCR0, 0);
	rcar_lvds_write(lvds, LVDCR1, 0);
	rcar_lvds_write(lvds, LVDPLLCR, 0);

	/* Disable the companion LVDS encoder in dual-link mode. */
	if (lvds->dual_link && lvds->companion)
		lvds->companion->funcs->disable(lvds->companion);

	clk_disable_unprepare(lvds->clocks.mod);
}

static bool rcar_lvds_mode_fixup(struct drm_bridge *bridge,
				 const struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode)
{
	struct rcar_lvds *lvds = bridge_to_rcar_lvds(bridge);
	int min_freq;

	/*
	 * The internal LVDS encoder has a restricted clock frequency operating
	 * range, from 5MHz to 148.5MHz on D3 and E3, and from 31MHz to
	 * 148.5MHz on all other platforms. Clamp the clock accordingly.
	 */
	min_freq = lvds->info->quirks & RCAR_LVDS_QUIRK_EXT_PLL ? 5000 : 31000;
	adjusted_mode->clock = clamp(adjusted_mode->clock, min_freq, 148500);

	return true;
}

static void rcar_lvds_get_lvds_mode(struct rcar_lvds *lvds)
{
	struct drm_display_info *info = &lvds->connector.display_info;
	enum rcar_lvds_mode mode;

	/*
	 * There is no API yet to retrieve LVDS mode from a bridge, only panels
	 * are supported.
	 */
	if (!lvds->panel)
		return;

	if (!info->num_bus_formats || !info->bus_formats) {
		dev_err(lvds->dev, "no LVDS bus format reported\n");
		return;
	}

	switch (info->bus_formats[0]) {
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
		mode = RCAR_LVDS_MODE_JEIDA;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
		mode = RCAR_LVDS_MODE_VESA;
		break;
	default:
		dev_err(lvds->dev, "unsupported LVDS bus format 0x%04x\n",
			info->bus_formats[0]);
		return;
	}

	if (info->bus_flags & DRM_BUS_FLAG_DATA_LSB_TO_MSB)
		mode |= RCAR_LVDS_MODE_MIRROR;

	lvds->mode = mode;
}

static void rcar_lvds_mode_set(struct drm_bridge *bridge,
			       const struct drm_display_mode *mode,
			       const struct drm_display_mode *adjusted_mode)
{
	struct rcar_lvds *lvds = bridge_to_rcar_lvds(bridge);

	lvds->display_mode = *adjusted_mode;

	rcar_lvds_get_lvds_mode(lvds);
}

static int rcar_lvds_attach(struct drm_bridge *bridge)
{
	struct rcar_lvds *lvds = bridge_to_rcar_lvds(bridge);
	struct drm_connector *connector = &lvds->connector;
	struct drm_encoder *encoder = bridge->encoder;
	int ret;

	/* If we have a next bridge just attach it. */
	if (lvds->next_bridge)
		return drm_bridge_attach(bridge->encoder, lvds->next_bridge,
					 bridge);

	/* Otherwise if we have a panel, create a connector. */
	if (!lvds->panel)
		return 0;

	ret = drm_connector_init(bridge->dev, connector, &rcar_lvds_conn_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret < 0)
		return ret;

	drm_connector_helper_add(connector, &rcar_lvds_conn_helper_funcs);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret < 0)
		return ret;

	return drm_panel_attach(lvds->panel, connector);
}

static void rcar_lvds_detach(struct drm_bridge *bridge)
{
	struct rcar_lvds *lvds = bridge_to_rcar_lvds(bridge);

	if (lvds->panel)
		drm_panel_detach(lvds->panel);
}

static const struct drm_bridge_funcs rcar_lvds_bridge_ops = {
	.attach = rcar_lvds_attach,
	.detach = rcar_lvds_detach,
	.enable = rcar_lvds_enable,
	.disable = rcar_lvds_disable,
	.mode_fixup = rcar_lvds_mode_fixup,
	.mode_set = rcar_lvds_mode_set,
};

bool rcar_lvds_dual_link(struct drm_bridge *bridge)
{
	struct rcar_lvds *lvds = bridge_to_rcar_lvds(bridge);

	return lvds->dual_link;
}
EXPORT_SYMBOL_GPL(rcar_lvds_dual_link);

/* -----------------------------------------------------------------------------
 * Probe & Remove
 */

static int rcar_lvds_parse_dt_companion(struct rcar_lvds *lvds)
{
	const struct of_device_id *match;
	struct device_node *companion;
	struct device *dev = lvds->dev;
	int ret = 0;

	/* Locate the companion LVDS encoder for dual-link operation, if any. */
	companion = of_parse_phandle(dev->of_node, "renesas,companion", 0);
	if (!companion) {
		dev_err(dev, "Companion LVDS encoder not found\n");
		return -ENXIO;
	}

	/*
	 * Sanity check: the companion encoder must have the same compatible
	 * string.
	 */
	match = of_match_device(dev->driver->of_match_table, dev);
	if (!of_device_is_compatible(companion, match->compatible)) {
		dev_err(dev, "Companion LVDS encoder is invalid\n");
		ret = -ENXIO;
		goto done;
	}

	lvds->companion = of_drm_find_bridge(companion);
	if (!lvds->companion) {
		ret = -EPROBE_DEFER;
		goto done;
	}

	dev_dbg(dev, "Found companion encoder %pOF\n", companion);

done:
	of_node_put(companion);

	return ret;
}

static int rcar_lvds_parse_dt(struct rcar_lvds *lvds)
{
	struct device_node *local_output = NULL;
	struct device_node *remote_input = NULL;
	struct device_node *remote = NULL;
	struct device_node *node;
	bool is_bridge = false;
	int ret = 0;

	local_output = of_graph_get_endpoint_by_regs(lvds->dev->of_node, 1, 0);
	if (!local_output) {
		dev_dbg(lvds->dev, "unconnected port@1\n");
		ret = -ENODEV;
		goto done;
	}

	/*
	 * Locate the connected entity and infer its type from the number of
	 * endpoints.
	 */
	remote = of_graph_get_remote_port_parent(local_output);
	if (!remote) {
		dev_dbg(lvds->dev, "unconnected endpoint %pOF\n", local_output);
		ret = -ENODEV;
		goto done;
	}

	if (!of_device_is_available(remote)) {
		dev_dbg(lvds->dev, "connected entity %pOF is disabled\n",
			remote);
		ret = -ENODEV;
		goto done;
	}

	remote_input = of_graph_get_remote_endpoint(local_output);

	for_each_endpoint_of_node(remote, node) {
		if (node != remote_input) {
			/*
			 * We've found one endpoint other than the input, this
			 * must be a bridge.
			 */
			is_bridge = true;
			of_node_put(node);
			break;
		}
	}

	if (is_bridge) {
		lvds->next_bridge = of_drm_find_bridge(remote);
		if (!lvds->next_bridge) {
			ret = -EPROBE_DEFER;
			goto done;
		}

		if (lvds->info->quirks & RCAR_LVDS_QUIRK_DUAL_LINK)
			lvds->dual_link = lvds->next_bridge->timings
					? lvds->next_bridge->timings->dual_link
					: false;
	} else {
		lvds->panel = of_drm_find_panel(remote);
		if (IS_ERR(lvds->panel)) {
			ret = PTR_ERR(lvds->panel);
			goto done;
		}
	}

	if (lvds->dual_link)
		ret = rcar_lvds_parse_dt_companion(lvds);

done:
	of_node_put(local_output);
	of_node_put(remote_input);
	of_node_put(remote);

	/*
	 * On D3/E3 the LVDS encoder provides a clock to the DU, which can be
	 * used for the DPAD output even when the LVDS output is not connected.
	 * Don't fail probe in that case as the DU will need the bridge to
	 * control the clock.
	 */
	if (lvds->info->quirks & RCAR_LVDS_QUIRK_EXT_PLL)
		return ret == -ENODEV ? 0 : ret;

	return ret;
}

static struct clk *rcar_lvds_get_clock(struct rcar_lvds *lvds, const char *name,
				       bool optional)
{
	struct clk *clk;

	clk = devm_clk_get(lvds->dev, name);
	if (!IS_ERR(clk))
		return clk;

	if (PTR_ERR(clk) == -ENOENT && optional)
		return NULL;

	if (PTR_ERR(clk) != -EPROBE_DEFER)
		dev_err(lvds->dev, "failed to get %s clock\n",
			name ? name : "module");

	return clk;
}

static int rcar_lvds_get_clocks(struct rcar_lvds *lvds)
{
	lvds->clocks.mod = rcar_lvds_get_clock(lvds, NULL, false);
	if (IS_ERR(lvds->clocks.mod))
		return PTR_ERR(lvds->clocks.mod);

	/*
	 * LVDS encoders without an extended PLL have no external clock inputs.
	 */
	if (!(lvds->info->quirks & RCAR_LVDS_QUIRK_EXT_PLL))
		return 0;

	lvds->clocks.extal = rcar_lvds_get_clock(lvds, "extal", true);
	if (IS_ERR(lvds->clocks.extal))
		return PTR_ERR(lvds->clocks.extal);

	lvds->clocks.dotclkin[0] = rcar_lvds_get_clock(lvds, "dclkin.0", true);
	if (IS_ERR(lvds->clocks.dotclkin[0]))
		return PTR_ERR(lvds->clocks.dotclkin[0]);

	lvds->clocks.dotclkin[1] = rcar_lvds_get_clock(lvds, "dclkin.1", true);
	if (IS_ERR(lvds->clocks.dotclkin[1]))
		return PTR_ERR(lvds->clocks.dotclkin[1]);

	/* At least one input to the PLL must be available. */
	if (!lvds->clocks.extal && !lvds->clocks.dotclkin[0] &&
	    !lvds->clocks.dotclkin[1]) {
		dev_err(lvds->dev,
			"no input clock (extal, dclkin.0 or dclkin.1)\n");
		return -EINVAL;
	}

	return 0;
}

static int rcar_lvds_probe(struct platform_device *pdev)
{
	struct rcar_lvds *lvds;
	struct resource *mem;
	int ret;

	lvds = devm_kzalloc(&pdev->dev, sizeof(*lvds), GFP_KERNEL);
	if (lvds == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, lvds);

	lvds->dev = &pdev->dev;
	lvds->info = of_device_get_match_data(&pdev->dev);

	ret = rcar_lvds_parse_dt(lvds);
	if (ret < 0)
		return ret;

	lvds->bridge.driver_private = lvds;
	lvds->bridge.funcs = &rcar_lvds_bridge_ops;
	lvds->bridge.of_node = pdev->dev.of_node;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lvds->mmio = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(lvds->mmio))
		return PTR_ERR(lvds->mmio);

	ret = rcar_lvds_get_clocks(lvds);
	if (ret < 0)
		return ret;

	drm_bridge_add(&lvds->bridge);

	return 0;
}

static int rcar_lvds_remove(struct platform_device *pdev)
{
	struct rcar_lvds *lvds = platform_get_drvdata(pdev);

	drm_bridge_remove(&lvds->bridge);

	return 0;
}

static const struct rcar_lvds_device_info rcar_lvds_gen2_info = {
	.gen = 2,
	.pll_setup = rcar_lvds_pll_setup_gen2,
};

static const struct rcar_lvds_device_info rcar_lvds_r8a7790_info = {
	.gen = 2,
	.quirks = RCAR_LVDS_QUIRK_LANES,
	.pll_setup = rcar_lvds_pll_setup_gen2,
};

static const struct rcar_lvds_device_info rcar_lvds_gen3_info = {
	.gen = 3,
	.quirks = RCAR_LVDS_QUIRK_PWD,
	.pll_setup = rcar_lvds_pll_setup_gen3,
};

static const struct rcar_lvds_device_info rcar_lvds_r8a77970_info = {
	.gen = 3,
	.quirks = RCAR_LVDS_QUIRK_PWD | RCAR_LVDS_QUIRK_GEN3_LVEN,
	.pll_setup = rcar_lvds_pll_setup_gen2,
};

static const struct rcar_lvds_device_info rcar_lvds_r8a77990_info = {
	.gen = 3,
	.quirks = RCAR_LVDS_QUIRK_GEN3_LVEN | RCAR_LVDS_QUIRK_EXT_PLL
		| RCAR_LVDS_QUIRK_DUAL_LINK,
	.pll_setup = rcar_lvds_pll_setup_d3_e3,
};

static const struct rcar_lvds_device_info rcar_lvds_r8a77995_info = {
	.gen = 3,
	.quirks = RCAR_LVDS_QUIRK_GEN3_LVEN | RCAR_LVDS_QUIRK_PWD
		| RCAR_LVDS_QUIRK_EXT_PLL | RCAR_LVDS_QUIRK_DUAL_LINK,
	.pll_setup = rcar_lvds_pll_setup_d3_e3,
};

static const struct of_device_id rcar_lvds_of_table[] = {
	{ .compatible = "renesas,r8a7743-lvds", .data = &rcar_lvds_gen2_info },
	{ .compatible = "renesas,r8a7744-lvds", .data = &rcar_lvds_gen2_info },
	{ .compatible = "renesas,r8a774a1-lvds", .data = &rcar_lvds_gen3_info },
	{ .compatible = "renesas,r8a774c0-lvds", .data = &rcar_lvds_r8a77990_info },
	{ .compatible = "renesas,r8a7790-lvds", .data = &rcar_lvds_r8a7790_info },
	{ .compatible = "renesas,r8a7791-lvds", .data = &rcar_lvds_gen2_info },
	{ .compatible = "renesas,r8a7793-lvds", .data = &rcar_lvds_gen2_info },
	{ .compatible = "renesas,r8a7795-lvds", .data = &rcar_lvds_gen3_info },
	{ .compatible = "renesas,r8a7796-lvds", .data = &rcar_lvds_gen3_info },
	{ .compatible = "renesas,r8a77965-lvds", .data = &rcar_lvds_gen3_info },
	{ .compatible = "renesas,r8a77970-lvds", .data = &rcar_lvds_r8a77970_info },
	{ .compatible = "renesas,r8a77980-lvds", .data = &rcar_lvds_gen3_info },
	{ .compatible = "renesas,r8a77990-lvds", .data = &rcar_lvds_r8a77990_info },
	{ .compatible = "renesas,r8a77995-lvds", .data = &rcar_lvds_r8a77995_info },
	{ }
};

MODULE_DEVICE_TABLE(of, rcar_lvds_of_table);

static struct platform_driver rcar_lvds_platform_driver = {
	.probe		= rcar_lvds_probe,
	.remove		= rcar_lvds_remove,
	.driver		= {
		.name	= "rcar-lvds",
		.of_match_table = rcar_lvds_of_table,
	},
};

module_platform_driver(rcar_lvds_platform_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Renesas R-Car LVDS Encoder Driver");
MODULE_LICENSE("GPL");
