// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
 */

#define DSS_SUBSYS_NAME "DSS"

#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/clk.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/gfp.h>
#include <linux/sizes.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/suspend.h>
#include <linux/component.h>
#include <linux/sys_soc.h>

#include "omapdss.h"
#include "dss.h"

struct dss_reg {
	u16 idx;
};

#define DSS_REG(idx)			((const struct dss_reg) { idx })

#define DSS_REVISION			DSS_REG(0x0000)
#define DSS_SYSCONFIG			DSS_REG(0x0010)
#define DSS_SYSSTATUS			DSS_REG(0x0014)
#define DSS_CONTROL			DSS_REG(0x0040)
#define DSS_SDI_CONTROL			DSS_REG(0x0044)
#define DSS_PLL_CONTROL			DSS_REG(0x0048)
#define DSS_SDI_STATUS			DSS_REG(0x005C)

#define REG_GET(dss, idx, start, end) \
	FLD_GET(dss_read_reg(dss, idx), start, end)

#define REG_FLD_MOD(dss, idx, val, start, end) \
	dss_write_reg(dss, idx, \
		      FLD_MOD(dss_read_reg(dss, idx), val, start, end))

struct dss_ops {
	int (*dpi_select_source)(struct dss_device *dss, int port,
				 enum omap_channel channel);
	int (*select_lcd_source)(struct dss_device *dss,
				 enum omap_channel channel,
				 enum dss_clk_source clk_src);
};

struct dss_features {
	enum dss_model model;
	u8 fck_div_max;
	unsigned int fck_freq_max;
	u8 dss_fck_multiplier;
	const char *parent_clk_name;
	const enum omap_display_type *ports;
	int num_ports;
	const enum omap_dss_output_id *outputs;
	const struct dss_ops *ops;
	struct dss_reg_field dispc_clk_switch;
	bool has_lcd_clk_src;
};

static const char * const dss_generic_clk_source_names[] = {
	[DSS_CLK_SRC_FCK]	= "FCK",
	[DSS_CLK_SRC_PLL1_1]	= "PLL1:1",
	[DSS_CLK_SRC_PLL1_2]	= "PLL1:2",
	[DSS_CLK_SRC_PLL1_3]	= "PLL1:3",
	[DSS_CLK_SRC_PLL2_1]	= "PLL2:1",
	[DSS_CLK_SRC_PLL2_2]	= "PLL2:2",
	[DSS_CLK_SRC_PLL2_3]	= "PLL2:3",
	[DSS_CLK_SRC_HDMI_PLL]	= "HDMI PLL",
};

static inline void dss_write_reg(struct dss_device *dss,
				 const struct dss_reg idx, u32 val)
{
	__raw_writel(val, dss->base + idx.idx);
}

static inline u32 dss_read_reg(struct dss_device *dss, const struct dss_reg idx)
{
	return __raw_readl(dss->base + idx.idx);
}

#define SR(dss, reg) \
	dss->ctx[(DSS_##reg).idx / sizeof(u32)] = dss_read_reg(dss, DSS_##reg)
#define RR(dss, reg) \
	dss_write_reg(dss, DSS_##reg, dss->ctx[(DSS_##reg).idx / sizeof(u32)])

static void dss_save_context(struct dss_device *dss)
{
	DSSDBG("dss_save_context\n");

	SR(dss, CONTROL);

	if (dss->feat->outputs[OMAP_DSS_CHANNEL_LCD] & OMAP_DSS_OUTPUT_SDI) {
		SR(dss, SDI_CONTROL);
		SR(dss, PLL_CONTROL);
	}

	dss->ctx_valid = true;

	DSSDBG("context saved\n");
}

static void dss_restore_context(struct dss_device *dss)
{
	DSSDBG("dss_restore_context\n");

	if (!dss->ctx_valid)
		return;

	RR(dss, CONTROL);

	if (dss->feat->outputs[OMAP_DSS_CHANNEL_LCD] & OMAP_DSS_OUTPUT_SDI) {
		RR(dss, SDI_CONTROL);
		RR(dss, PLL_CONTROL);
	}

	DSSDBG("context restored\n");
}

#undef SR
#undef RR

void dss_ctrl_pll_enable(struct dss_pll *pll, bool enable)
{
	unsigned int shift;
	unsigned int val;

	if (!pll->dss->syscon_pll_ctrl)
		return;

	val = !enable;

	switch (pll->id) {
	case DSS_PLL_VIDEO1:
		shift = 0;
		break;
	case DSS_PLL_VIDEO2:
		shift = 1;
		break;
	case DSS_PLL_HDMI:
		shift = 2;
		break;
	default:
		DSSERR("illegal DSS PLL ID %d\n", pll->id);
		return;
	}

	regmap_update_bits(pll->dss->syscon_pll_ctrl,
			   pll->dss->syscon_pll_ctrl_offset,
			   1 << shift, val << shift);
}

static int dss_ctrl_pll_set_control_mux(struct dss_device *dss,
					enum dss_clk_source clk_src,
					enum omap_channel channel)
{
	unsigned int shift, val;

	if (!dss->syscon_pll_ctrl)
		return -EINVAL;

	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		shift = 3;

		switch (clk_src) {
		case DSS_CLK_SRC_PLL1_1:
			val = 0; break;
		case DSS_CLK_SRC_HDMI_PLL:
			val = 1; break;
		default:
			DSSERR("error in PLL mux config for LCD\n");
			return -EINVAL;
		}

		break;
	case OMAP_DSS_CHANNEL_LCD2:
		shift = 5;

		switch (clk_src) {
		case DSS_CLK_SRC_PLL1_3:
			val = 0; break;
		case DSS_CLK_SRC_PLL2_3:
			val = 1; break;
		case DSS_CLK_SRC_HDMI_PLL:
			val = 2; break;
		default:
			DSSERR("error in PLL mux config for LCD2\n");
			return -EINVAL;
		}

		break;
	case OMAP_DSS_CHANNEL_LCD3:
		shift = 7;

		switch (clk_src) {
		case DSS_CLK_SRC_PLL2_1:
			val = 0; break;
		case DSS_CLK_SRC_PLL1_3:
			val = 1; break;
		case DSS_CLK_SRC_HDMI_PLL:
			val = 2; break;
		default:
			DSSERR("error in PLL mux config for LCD3\n");
			return -EINVAL;
		}

		break;
	default:
		DSSERR("error in PLL mux config\n");
		return -EINVAL;
	}

	regmap_update_bits(dss->syscon_pll_ctrl, dss->syscon_pll_ctrl_offset,
		0x3 << shift, val << shift);

	return 0;
}

void dss_sdi_init(struct dss_device *dss, int datapairs)
{
	u32 l;

	BUG_ON(datapairs > 3 || datapairs < 1);

	l = dss_read_reg(dss, DSS_SDI_CONTROL);
	l = FLD_MOD(l, 0xf, 19, 15);		/* SDI_PDIV */
	l = FLD_MOD(l, datapairs-1, 3, 2);	/* SDI_PRSEL */
	l = FLD_MOD(l, 2, 1, 0);		/* SDI_BWSEL */
	dss_write_reg(dss, DSS_SDI_CONTROL, l);

	l = dss_read_reg(dss, DSS_PLL_CONTROL);
	l = FLD_MOD(l, 0x7, 25, 22);	/* SDI_PLL_FREQSEL */
	l = FLD_MOD(l, 0xb, 16, 11);	/* SDI_PLL_REGN */
	l = FLD_MOD(l, 0xb4, 10, 1);	/* SDI_PLL_REGM */
	dss_write_reg(dss, DSS_PLL_CONTROL, l);
}

int dss_sdi_enable(struct dss_device *dss)
{
	unsigned long timeout;

	dispc_pck_free_enable(dss->dispc, 1);

	/* Reset SDI PLL */
	REG_FLD_MOD(dss, DSS_PLL_CONTROL, 1, 18, 18); /* SDI_PLL_SYSRESET */
	udelay(1);	/* wait 2x PCLK */

	/* Lock SDI PLL */
	REG_FLD_MOD(dss, DSS_PLL_CONTROL, 1, 28, 28); /* SDI_PLL_GOBIT */

	/* Waiting for PLL lock request to complete */
	timeout = jiffies + msecs_to_jiffies(500);
	while (dss_read_reg(dss, DSS_SDI_STATUS) & (1 << 6)) {
		if (time_after_eq(jiffies, timeout)) {
			DSSERR("PLL lock request timed out\n");
			goto err1;
		}
	}

	/* Clearing PLL_GO bit */
	REG_FLD_MOD(dss, DSS_PLL_CONTROL, 0, 28, 28);

	/* Waiting for PLL to lock */
	timeout = jiffies + msecs_to_jiffies(500);
	while (!(dss_read_reg(dss, DSS_SDI_STATUS) & (1 << 5))) {
		if (time_after_eq(jiffies, timeout)) {
			DSSERR("PLL lock timed out\n");
			goto err1;
		}
	}

	dispc_lcd_enable_signal(dss->dispc, 1);

	/* Waiting for SDI reset to complete */
	timeout = jiffies + msecs_to_jiffies(500);
	while (!(dss_read_reg(dss, DSS_SDI_STATUS) & (1 << 2))) {
		if (time_after_eq(jiffies, timeout)) {
			DSSERR("SDI reset timed out\n");
			goto err2;
		}
	}

	return 0;

 err2:
	dispc_lcd_enable_signal(dss->dispc, 0);
 err1:
	/* Reset SDI PLL */
	REG_FLD_MOD(dss, DSS_PLL_CONTROL, 0, 18, 18); /* SDI_PLL_SYSRESET */

	dispc_pck_free_enable(dss->dispc, 0);

	return -ETIMEDOUT;
}

void dss_sdi_disable(struct dss_device *dss)
{
	dispc_lcd_enable_signal(dss->dispc, 0);

	dispc_pck_free_enable(dss->dispc, 0);

	/* Reset SDI PLL */
	REG_FLD_MOD(dss, DSS_PLL_CONTROL, 0, 18, 18); /* SDI_PLL_SYSRESET */
}

const char *dss_get_clk_source_name(enum dss_clk_source clk_src)
{
	return dss_generic_clk_source_names[clk_src];
}

static void dss_dump_clocks(struct dss_device *dss, struct seq_file *s)
{
	const char *fclk_name;
	unsigned long fclk_rate;

	if (dss_runtime_get(dss))
		return;

	seq_printf(s, "- DSS -\n");

	fclk_name = dss_get_clk_source_name(DSS_CLK_SRC_FCK);
	fclk_rate = clk_get_rate(dss->dss_clk);

	seq_printf(s, "%s = %lu\n",
			fclk_name,
			fclk_rate);

	dss_runtime_put(dss);
}

static int dss_dump_regs(struct seq_file *s, void *p)
{
	struct dss_device *dss = s->private;

#define DUMPREG(dss, r) seq_printf(s, "%-35s %08x\n", #r, dss_read_reg(dss, r))

	if (dss_runtime_get(dss))
		return 0;

	DUMPREG(dss, DSS_REVISION);
	DUMPREG(dss, DSS_SYSCONFIG);
	DUMPREG(dss, DSS_SYSSTATUS);
	DUMPREG(dss, DSS_CONTROL);

	if (dss->feat->outputs[OMAP_DSS_CHANNEL_LCD] & OMAP_DSS_OUTPUT_SDI) {
		DUMPREG(dss, DSS_SDI_CONTROL);
		DUMPREG(dss, DSS_PLL_CONTROL);
		DUMPREG(dss, DSS_SDI_STATUS);
	}

	dss_runtime_put(dss);
#undef DUMPREG
	return 0;
}

static int dss_debug_dump_clocks(struct seq_file *s, void *p)
{
	struct dss_device *dss = s->private;

	dss_dump_clocks(dss, s);
	dispc_dump_clocks(dss->dispc, s);
	return 0;
}

static int dss_get_channel_index(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return 0;
	case OMAP_DSS_CHANNEL_LCD2:
		return 1;
	case OMAP_DSS_CHANNEL_LCD3:
		return 2;
	default:
		WARN_ON(1);
		return 0;
	}
}

static void dss_select_dispc_clk_source(struct dss_device *dss,
					enum dss_clk_source clk_src)
{
	int b;

	/*
	 * We always use PRCM clock as the DISPC func clock, except on DSS3,
	 * where we don't have separate DISPC and LCD clock sources.
	 */
	if (WARN_ON(dss->feat->has_lcd_clk_src && clk_src != DSS_CLK_SRC_FCK))
		return;

	switch (clk_src) {
	case DSS_CLK_SRC_FCK:
		b = 0;
		break;
	case DSS_CLK_SRC_PLL1_1:
		b = 1;
		break;
	case DSS_CLK_SRC_PLL2_1:
		b = 2;
		break;
	default:
		BUG();
		return;
	}

	REG_FLD_MOD(dss, DSS_CONTROL, b,		/* DISPC_CLK_SWITCH */
		    dss->feat->dispc_clk_switch.start,
		    dss->feat->dispc_clk_switch.end);

	dss->dispc_clk_source = clk_src;
}

void dss_select_dsi_clk_source(struct dss_device *dss, int dsi_module,
			       enum dss_clk_source clk_src)
{
	int b, pos;

	switch (clk_src) {
	case DSS_CLK_SRC_FCK:
		b = 0;
		break;
	case DSS_CLK_SRC_PLL1_2:
		BUG_ON(dsi_module != 0);
		b = 1;
		break;
	case DSS_CLK_SRC_PLL2_2:
		BUG_ON(dsi_module != 1);
		b = 1;
		break;
	default:
		BUG();
		return;
	}

	pos = dsi_module == 0 ? 1 : 10;
	REG_FLD_MOD(dss, DSS_CONTROL, b, pos, pos);	/* DSIx_CLK_SWITCH */

	dss->dsi_clk_source[dsi_module] = clk_src;
}

static int dss_lcd_clk_mux_dra7(struct dss_device *dss,
				enum omap_channel channel,
				enum dss_clk_source clk_src)
{
	const u8 ctrl_bits[] = {
		[OMAP_DSS_CHANNEL_LCD] = 0,
		[OMAP_DSS_CHANNEL_LCD2] = 12,
		[OMAP_DSS_CHANNEL_LCD3] = 19,
	};

	u8 ctrl_bit = ctrl_bits[channel];
	int r;

	if (clk_src == DSS_CLK_SRC_FCK) {
		/* LCDx_CLK_SWITCH */
		REG_FLD_MOD(dss, DSS_CONTROL, 0, ctrl_bit, ctrl_bit);
		return -EINVAL;
	}

	r = dss_ctrl_pll_set_control_mux(dss, clk_src, channel);
	if (r)
		return r;

	REG_FLD_MOD(dss, DSS_CONTROL, 1, ctrl_bit, ctrl_bit);

	return 0;
}

static int dss_lcd_clk_mux_omap5(struct dss_device *dss,
				 enum omap_channel channel,
				 enum dss_clk_source clk_src)
{
	const u8 ctrl_bits[] = {
		[OMAP_DSS_CHANNEL_LCD] = 0,
		[OMAP_DSS_CHANNEL_LCD2] = 12,
		[OMAP_DSS_CHANNEL_LCD3] = 19,
	};
	const enum dss_clk_source allowed_plls[] = {
		[OMAP_DSS_CHANNEL_LCD] = DSS_CLK_SRC_PLL1_1,
		[OMAP_DSS_CHANNEL_LCD2] = DSS_CLK_SRC_FCK,
		[OMAP_DSS_CHANNEL_LCD3] = DSS_CLK_SRC_PLL2_1,
	};

	u8 ctrl_bit = ctrl_bits[channel];

	if (clk_src == DSS_CLK_SRC_FCK) {
		/* LCDx_CLK_SWITCH */
		REG_FLD_MOD(dss, DSS_CONTROL, 0, ctrl_bit, ctrl_bit);
		return -EINVAL;
	}

	if (WARN_ON(allowed_plls[channel] != clk_src))
		return -EINVAL;

	REG_FLD_MOD(dss, DSS_CONTROL, 1, ctrl_bit, ctrl_bit);

	return 0;
}

static int dss_lcd_clk_mux_omap4(struct dss_device *dss,
				 enum omap_channel channel,
				 enum dss_clk_source clk_src)
{
	const u8 ctrl_bits[] = {
		[OMAP_DSS_CHANNEL_LCD] = 0,
		[OMAP_DSS_CHANNEL_LCD2] = 12,
	};
	const enum dss_clk_source allowed_plls[] = {
		[OMAP_DSS_CHANNEL_LCD] = DSS_CLK_SRC_PLL1_1,
		[OMAP_DSS_CHANNEL_LCD2] = DSS_CLK_SRC_PLL2_1,
	};

	u8 ctrl_bit = ctrl_bits[channel];

	if (clk_src == DSS_CLK_SRC_FCK) {
		/* LCDx_CLK_SWITCH */
		REG_FLD_MOD(dss, DSS_CONTROL, 0, ctrl_bit, ctrl_bit);
		return 0;
	}

	if (WARN_ON(allowed_plls[channel] != clk_src))
		return -EINVAL;

	REG_FLD_MOD(dss, DSS_CONTROL, 1, ctrl_bit, ctrl_bit);

	return 0;
}

void dss_select_lcd_clk_source(struct dss_device *dss,
			       enum omap_channel channel,
			       enum dss_clk_source clk_src)
{
	int idx = dss_get_channel_index(channel);
	int r;

	if (!dss->feat->has_lcd_clk_src) {
		dss_select_dispc_clk_source(dss, clk_src);
		dss->lcd_clk_source[idx] = clk_src;
		return;
	}

	r = dss->feat->ops->select_lcd_source(dss, channel, clk_src);
	if (r)
		return;

	dss->lcd_clk_source[idx] = clk_src;
}

enum dss_clk_source dss_get_dispc_clk_source(struct dss_device *dss)
{
	return dss->dispc_clk_source;
}

enum dss_clk_source dss_get_dsi_clk_source(struct dss_device *dss,
					   int dsi_module)
{
	return dss->dsi_clk_source[dsi_module];
}

enum dss_clk_source dss_get_lcd_clk_source(struct dss_device *dss,
					   enum omap_channel channel)
{
	if (dss->feat->has_lcd_clk_src) {
		int idx = dss_get_channel_index(channel);
		return dss->lcd_clk_source[idx];
	} else {
		/* LCD_CLK source is the same as DISPC_FCLK source for
		 * OMAP2 and OMAP3 */
		return dss->dispc_clk_source;
	}
}

bool dss_div_calc(struct dss_device *dss, unsigned long pck,
		  unsigned long fck_min, dss_div_calc_func func, void *data)
{
	int fckd, fckd_start, fckd_stop;
	unsigned long fck;
	unsigned long fck_hw_max;
	unsigned long fckd_hw_max;
	unsigned long prate;
	unsigned int m;

	fck_hw_max = dss->feat->fck_freq_max;

	if (dss->parent_clk == NULL) {
		unsigned int pckd;

		pckd = fck_hw_max / pck;

		fck = pck * pckd;

		fck = clk_round_rate(dss->dss_clk, fck);

		return func(fck, data);
	}

	fckd_hw_max = dss->feat->fck_div_max;

	m = dss->feat->dss_fck_multiplier;
	prate = clk_get_rate(dss->parent_clk);

	fck_min = fck_min ? fck_min : 1;

	fckd_start = min(prate * m / fck_min, fckd_hw_max);
	fckd_stop = max(DIV_ROUND_UP(prate * m, fck_hw_max), 1ul);

	for (fckd = fckd_start; fckd >= fckd_stop; --fckd) {
		fck = DIV_ROUND_UP(prate, fckd) * m;

		if (func(fck, data))
			return true;
	}

	return false;
}

int dss_set_fck_rate(struct dss_device *dss, unsigned long rate)
{
	int r;

	DSSDBG("set fck to %lu\n", rate);

	r = clk_set_rate(dss->dss_clk, rate);
	if (r)
		return r;

	dss->dss_clk_rate = clk_get_rate(dss->dss_clk);

	WARN_ONCE(dss->dss_clk_rate != rate, "clk rate mismatch: %lu != %lu",
		  dss->dss_clk_rate, rate);

	return 0;
}

unsigned long dss_get_dispc_clk_rate(struct dss_device *dss)
{
	return dss->dss_clk_rate;
}

unsigned long dss_get_max_fck_rate(struct dss_device *dss)
{
	return dss->feat->fck_freq_max;
}

static int dss_setup_default_clock(struct dss_device *dss)
{
	unsigned long max_dss_fck, prate;
	unsigned long fck;
	unsigned int fck_div;
	int r;

	max_dss_fck = dss->feat->fck_freq_max;

	if (dss->parent_clk == NULL) {
		fck = clk_round_rate(dss->dss_clk, max_dss_fck);
	} else {
		prate = clk_get_rate(dss->parent_clk);

		fck_div = DIV_ROUND_UP(prate * dss->feat->dss_fck_multiplier,
				max_dss_fck);
		fck = DIV_ROUND_UP(prate, fck_div)
		    * dss->feat->dss_fck_multiplier;
	}

	r = dss_set_fck_rate(dss, fck);
	if (r)
		return r;

	return 0;
}

void dss_set_venc_output(struct dss_device *dss, enum omap_dss_venc_type type)
{
	int l = 0;

	if (type == OMAP_DSS_VENC_TYPE_COMPOSITE)
		l = 0;
	else if (type == OMAP_DSS_VENC_TYPE_SVIDEO)
		l = 1;
	else
		BUG();

	/* venc out selection. 0 = comp, 1 = svideo */
	REG_FLD_MOD(dss, DSS_CONTROL, l, 6, 6);
}

void dss_set_dac_pwrdn_bgz(struct dss_device *dss, bool enable)
{
	/* DAC Power-Down Control */
	REG_FLD_MOD(dss, DSS_CONTROL, enable, 5, 5);
}

void dss_select_hdmi_venc_clk_source(struct dss_device *dss,
				     enum dss_hdmi_venc_clk_source_select src)
{
	enum omap_dss_output_id outputs;

	outputs = dss->feat->outputs[OMAP_DSS_CHANNEL_DIGIT];

	/* Complain about invalid selections */
	WARN_ON((src == DSS_VENC_TV_CLK) && !(outputs & OMAP_DSS_OUTPUT_VENC));
	WARN_ON((src == DSS_HDMI_M_PCLK) && !(outputs & OMAP_DSS_OUTPUT_HDMI));

	/* Select only if we have options */
	if ((outputs & OMAP_DSS_OUTPUT_VENC) &&
	    (outputs & OMAP_DSS_OUTPUT_HDMI))
		/* VENC_HDMI_SWITCH */
		REG_FLD_MOD(dss, DSS_CONTROL, src, 15, 15);
}

static int dss_dpi_select_source_omap2_omap3(struct dss_device *dss, int port,
					     enum omap_channel channel)
{
	if (channel != OMAP_DSS_CHANNEL_LCD)
		return -EINVAL;

	return 0;
}

static int dss_dpi_select_source_omap4(struct dss_device *dss, int port,
				       enum omap_channel channel)
{
	int val;

	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD2:
		val = 0;
		break;
	case OMAP_DSS_CHANNEL_DIGIT:
		val = 1;
		break;
	default:
		return -EINVAL;
	}

	REG_FLD_MOD(dss, DSS_CONTROL, val, 17, 17);

	return 0;
}

static int dss_dpi_select_source_omap5(struct dss_device *dss, int port,
				       enum omap_channel channel)
{
	int val;

	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		val = 1;
		break;
	case OMAP_DSS_CHANNEL_LCD2:
		val = 2;
		break;
	case OMAP_DSS_CHANNEL_LCD3:
		val = 3;
		break;
	case OMAP_DSS_CHANNEL_DIGIT:
		val = 0;
		break;
	default:
		return -EINVAL;
	}

	REG_FLD_MOD(dss, DSS_CONTROL, val, 17, 16);

	return 0;
}

static int dss_dpi_select_source_dra7xx(struct dss_device *dss, int port,
					enum omap_channel channel)
{
	switch (port) {
	case 0:
		return dss_dpi_select_source_omap5(dss, port, channel);
	case 1:
		if (channel != OMAP_DSS_CHANNEL_LCD2)
			return -EINVAL;
		break;
	case 2:
		if (channel != OMAP_DSS_CHANNEL_LCD3)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int dss_dpi_select_source(struct dss_device *dss, int port,
			  enum omap_channel channel)
{
	return dss->feat->ops->dpi_select_source(dss, port, channel);
}

static int dss_get_clocks(struct dss_device *dss)
{
	struct clk *clk;

	clk = devm_clk_get(&dss->pdev->dev, "fck");
	if (IS_ERR(clk)) {
		DSSERR("can't get clock fck\n");
		return PTR_ERR(clk);
	}

	dss->dss_clk = clk;

	if (dss->feat->parent_clk_name) {
		clk = clk_get(NULL, dss->feat->parent_clk_name);
		if (IS_ERR(clk)) {
			DSSERR("Failed to get %s\n",
			       dss->feat->parent_clk_name);
			return PTR_ERR(clk);
		}
	} else {
		clk = NULL;
	}

	dss->parent_clk = clk;

	return 0;
}

static void dss_put_clocks(struct dss_device *dss)
{
	if (dss->parent_clk)
		clk_put(dss->parent_clk);
}

int dss_runtime_get(struct dss_device *dss)
{
	int r;

	DSSDBG("dss_runtime_get\n");

	r = pm_runtime_get_sync(&dss->pdev->dev);
	WARN_ON(r < 0);
	return r < 0 ? r : 0;
}

void dss_runtime_put(struct dss_device *dss)
{
	int r;

	DSSDBG("dss_runtime_put\n");

	r = pm_runtime_put_sync(&dss->pdev->dev);
	WARN_ON(r < 0 && r != -ENOSYS && r != -EBUSY);
}

struct dss_device *dss_get_device(struct device *dev)
{
	return dev_get_drvdata(dev);
}

/* DEBUGFS */
#if defined(CONFIG_OMAP2_DSS_DEBUGFS)
static int dss_initialize_debugfs(struct dss_device *dss)
{
	struct dentry *dir;

	dir = debugfs_create_dir("omapdss", NULL);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	dss->debugfs.root = dir;

	return 0;
}

static void dss_uninitialize_debugfs(struct dss_device *dss)
{
	debugfs_remove_recursive(dss->debugfs.root);
}

struct dss_debugfs_entry {
	struct dentry *dentry;
	int (*show_fn)(struct seq_file *s, void *data);
	void *data;
};

static int dss_debug_open(struct inode *inode, struct file *file)
{
	struct dss_debugfs_entry *entry = inode->i_private;

	return single_open(file, entry->show_fn, entry->data);
}

static const struct file_operations dss_debug_fops = {
	.open		= dss_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

struct dss_debugfs_entry *
dss_debugfs_create_file(struct dss_device *dss, const char *name,
			int (*show_fn)(struct seq_file *s, void *data),
			void *data)
{
	struct dss_debugfs_entry *entry;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return ERR_PTR(-ENOMEM);

	entry->show_fn = show_fn;
	entry->data = data;
	entry->dentry = debugfs_create_file(name, 0444, dss->debugfs.root,
					    entry, &dss_debug_fops);

	return entry;
}

void dss_debugfs_remove_file(struct dss_debugfs_entry *entry)
{
	if (IS_ERR_OR_NULL(entry))
		return;

	debugfs_remove(entry->dentry);
	kfree(entry);
}

#else /* CONFIG_OMAP2_DSS_DEBUGFS */
static inline int dss_initialize_debugfs(struct dss_device *dss)
{
	return 0;
}
static inline void dss_uninitialize_debugfs(struct dss_device *dss)
{
}
#endif /* CONFIG_OMAP2_DSS_DEBUGFS */

static const struct dss_ops dss_ops_omap2_omap3 = {
	.dpi_select_source = &dss_dpi_select_source_omap2_omap3,
};

static const struct dss_ops dss_ops_omap4 = {
	.dpi_select_source = &dss_dpi_select_source_omap4,
	.select_lcd_source = &dss_lcd_clk_mux_omap4,
};

static const struct dss_ops dss_ops_omap5 = {
	.dpi_select_source = &dss_dpi_select_source_omap5,
	.select_lcd_source = &dss_lcd_clk_mux_omap5,
};

static const struct dss_ops dss_ops_dra7 = {
	.dpi_select_source = &dss_dpi_select_source_dra7xx,
	.select_lcd_source = &dss_lcd_clk_mux_dra7,
};

static const enum omap_display_type omap2plus_ports[] = {
	OMAP_DISPLAY_TYPE_DPI,
};

static const enum omap_display_type omap34xx_ports[] = {
	OMAP_DISPLAY_TYPE_DPI,
	OMAP_DISPLAY_TYPE_SDI,
};

static const enum omap_display_type dra7xx_ports[] = {
	OMAP_DISPLAY_TYPE_DPI,
	OMAP_DISPLAY_TYPE_DPI,
	OMAP_DISPLAY_TYPE_DPI,
};

static const enum omap_dss_output_id omap2_dss_supported_outputs[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DSS_OUTPUT_DPI | OMAP_DSS_OUTPUT_DBI,

	/* OMAP_DSS_CHANNEL_DIGIT */
	OMAP_DSS_OUTPUT_VENC,
};

static const enum omap_dss_output_id omap3430_dss_supported_outputs[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DSS_OUTPUT_DPI | OMAP_DSS_OUTPUT_DBI |
	OMAP_DSS_OUTPUT_SDI | OMAP_DSS_OUTPUT_DSI1,

	/* OMAP_DSS_CHANNEL_DIGIT */
	OMAP_DSS_OUTPUT_VENC,
};

static const enum omap_dss_output_id omap3630_dss_supported_outputs[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DSS_OUTPUT_DPI | OMAP_DSS_OUTPUT_DBI |
	OMAP_DSS_OUTPUT_DSI1,

	/* OMAP_DSS_CHANNEL_DIGIT */
	OMAP_DSS_OUTPUT_VENC,
};

static const enum omap_dss_output_id am43xx_dss_supported_outputs[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DSS_OUTPUT_DPI | OMAP_DSS_OUTPUT_DBI,
};

static const enum omap_dss_output_id omap4_dss_supported_outputs[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DSS_OUTPUT_DBI | OMAP_DSS_OUTPUT_DSI1,

	/* OMAP_DSS_CHANNEL_DIGIT */
	OMAP_DSS_OUTPUT_VENC | OMAP_DSS_OUTPUT_HDMI,

	/* OMAP_DSS_CHANNEL_LCD2 */
	OMAP_DSS_OUTPUT_DPI | OMAP_DSS_OUTPUT_DBI |
	OMAP_DSS_OUTPUT_DSI2,
};

static const enum omap_dss_output_id omap5_dss_supported_outputs[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DSS_OUTPUT_DPI | OMAP_DSS_OUTPUT_DBI |
	OMAP_DSS_OUTPUT_DSI1 | OMAP_DSS_OUTPUT_DSI2,

	/* OMAP_DSS_CHANNEL_DIGIT */
	OMAP_DSS_OUTPUT_HDMI,

	/* OMAP_DSS_CHANNEL_LCD2 */
	OMAP_DSS_OUTPUT_DPI | OMAP_DSS_OUTPUT_DBI |
	OMAP_DSS_OUTPUT_DSI1,

	/* OMAP_DSS_CHANNEL_LCD3 */
	OMAP_DSS_OUTPUT_DPI | OMAP_DSS_OUTPUT_DBI |
	OMAP_DSS_OUTPUT_DSI2,
};

static const struct dss_features omap24xx_dss_feats = {
	.model			=	DSS_MODEL_OMAP2,
	/*
	 * fck div max is really 16, but the divider range has gaps. The range
	 * from 1 to 6 has no gaps, so let's use that as a max.
	 */
	.fck_div_max		=	6,
	.fck_freq_max		=	133000000,
	.dss_fck_multiplier	=	2,
	.parent_clk_name	=	"core_ck",
	.ports			=	omap2plus_ports,
	.num_ports		=	ARRAY_SIZE(omap2plus_ports),
	.outputs		=	omap2_dss_supported_outputs,
	.ops			=	&dss_ops_omap2_omap3,
	.dispc_clk_switch	=	{ 0, 0 },
	.has_lcd_clk_src	=	false,
};

static const struct dss_features omap34xx_dss_feats = {
	.model			=	DSS_MODEL_OMAP3,
	.fck_div_max		=	16,
	.fck_freq_max		=	173000000,
	.dss_fck_multiplier	=	2,
	.parent_clk_name	=	"dpll4_ck",
	.ports			=	omap34xx_ports,
	.outputs		=	omap3430_dss_supported_outputs,
	.num_ports		=	ARRAY_SIZE(omap34xx_ports),
	.ops			=	&dss_ops_omap2_omap3,
	.dispc_clk_switch	=	{ 0, 0 },
	.has_lcd_clk_src	=	false,
};

static const struct dss_features omap3630_dss_feats = {
	.model			=	DSS_MODEL_OMAP3,
	.fck_div_max		=	31,
	.fck_freq_max		=	173000000,
	.dss_fck_multiplier	=	1,
	.parent_clk_name	=	"dpll4_ck",
	.ports			=	omap2plus_ports,
	.num_ports		=	ARRAY_SIZE(omap2plus_ports),
	.outputs		=	omap3630_dss_supported_outputs,
	.ops			=	&dss_ops_omap2_omap3,
	.dispc_clk_switch	=	{ 0, 0 },
	.has_lcd_clk_src	=	false,
};

static const struct dss_features omap44xx_dss_feats = {
	.model			=	DSS_MODEL_OMAP4,
	.fck_div_max		=	32,
	.fck_freq_max		=	186000000,
	.dss_fck_multiplier	=	1,
	.parent_clk_name	=	"dpll_per_x2_ck",
	.ports			=	omap2plus_ports,
	.num_ports		=	ARRAY_SIZE(omap2plus_ports),
	.outputs		=	omap4_dss_supported_outputs,
	.ops			=	&dss_ops_omap4,
	.dispc_clk_switch	=	{ 9, 8 },
	.has_lcd_clk_src	=	true,
};

static const struct dss_features omap54xx_dss_feats = {
	.model			=	DSS_MODEL_OMAP5,
	.fck_div_max		=	64,
	.fck_freq_max		=	209250000,
	.dss_fck_multiplier	=	1,
	.parent_clk_name	=	"dpll_per_x2_ck",
	.ports			=	omap2plus_ports,
	.num_ports		=	ARRAY_SIZE(omap2plus_ports),
	.outputs		=	omap5_dss_supported_outputs,
	.ops			=	&dss_ops_omap5,
	.dispc_clk_switch	=	{ 9, 7 },
	.has_lcd_clk_src	=	true,
};

static const struct dss_features am43xx_dss_feats = {
	.model			=	DSS_MODEL_OMAP3,
	.fck_div_max		=	0,
	.fck_freq_max		=	200000000,
	.dss_fck_multiplier	=	0,
	.parent_clk_name	=	NULL,
	.ports			=	omap2plus_ports,
	.num_ports		=	ARRAY_SIZE(omap2plus_ports),
	.outputs		=	am43xx_dss_supported_outputs,
	.ops			=	&dss_ops_omap2_omap3,
	.dispc_clk_switch	=	{ 0, 0 },
	.has_lcd_clk_src	=	true,
};

static const struct dss_features dra7xx_dss_feats = {
	.model			=	DSS_MODEL_DRA7,
	.fck_div_max		=	64,
	.fck_freq_max		=	209250000,
	.dss_fck_multiplier	=	1,
	.parent_clk_name	=	"dpll_per_x2_ck",
	.ports			=	dra7xx_ports,
	.num_ports		=	ARRAY_SIZE(dra7xx_ports),
	.outputs		=	omap5_dss_supported_outputs,
	.ops			=	&dss_ops_dra7,
	.dispc_clk_switch	=	{ 9, 7 },
	.has_lcd_clk_src	=	true,
};

static void __dss_uninit_ports(struct dss_device *dss, unsigned int num_ports)
{
	struct platform_device *pdev = dss->pdev;
	struct device_node *parent = pdev->dev.of_node;
	struct device_node *port;
	unsigned int i;

	for (i = 0; i < num_ports; i++) {
		port = of_graph_get_port_by_id(parent, i);
		if (!port)
			continue;

		switch (dss->feat->ports[i]) {
		case OMAP_DISPLAY_TYPE_DPI:
			dpi_uninit_port(port);
			break;
		case OMAP_DISPLAY_TYPE_SDI:
			sdi_uninit_port(port);
			break;
		default:
			break;
		}
	}
}

static int dss_init_ports(struct dss_device *dss)
{
	struct platform_device *pdev = dss->pdev;
	struct device_node *parent = pdev->dev.of_node;
	struct device_node *port;
	unsigned int i;
	int r;

	for (i = 0; i < dss->feat->num_ports; i++) {
		port = of_graph_get_port_by_id(parent, i);
		if (!port)
			continue;

		switch (dss->feat->ports[i]) {
		case OMAP_DISPLAY_TYPE_DPI:
			r = dpi_init_port(dss, pdev, port, dss->feat->model);
			if (r)
				goto error;
			break;

		case OMAP_DISPLAY_TYPE_SDI:
			r = sdi_init_port(dss, pdev, port);
			if (r)
				goto error;
			break;

		default:
			break;
		}
	}

	return 0;

error:
	__dss_uninit_ports(dss, i);
	return r;
}

static void dss_uninit_ports(struct dss_device *dss)
{
	__dss_uninit_ports(dss, dss->feat->num_ports);
}

static int dss_video_pll_probe(struct dss_device *dss)
{
	struct platform_device *pdev = dss->pdev;
	struct device_node *np = pdev->dev.of_node;
	struct regulator *pll_regulator;
	int r;

	if (!np)
		return 0;

	if (of_property_read_bool(np, "syscon-pll-ctrl")) {
		dss->syscon_pll_ctrl = syscon_regmap_lookup_by_phandle(np,
			"syscon-pll-ctrl");
		if (IS_ERR(dss->syscon_pll_ctrl)) {
			dev_err(&pdev->dev,
				"failed to get syscon-pll-ctrl regmap\n");
			return PTR_ERR(dss->syscon_pll_ctrl);
		}

		if (of_property_read_u32_index(np, "syscon-pll-ctrl", 1,
				&dss->syscon_pll_ctrl_offset)) {
			dev_err(&pdev->dev,
				"failed to get syscon-pll-ctrl offset\n");
			return -EINVAL;
		}
	}

	pll_regulator = devm_regulator_get(&pdev->dev, "vdda_video");
	if (IS_ERR(pll_regulator)) {
		r = PTR_ERR(pll_regulator);

		switch (r) {
		case -ENOENT:
			pll_regulator = NULL;
			break;

		case -EPROBE_DEFER:
			return -EPROBE_DEFER;

		default:
			DSSERR("can't get DPLL VDDA regulator\n");
			return r;
		}
	}

	if (of_property_match_string(np, "reg-names", "pll1") >= 0) {
		dss->video1_pll = dss_video_pll_init(dss, pdev, 0,
						     pll_regulator);
		if (IS_ERR(dss->video1_pll))
			return PTR_ERR(dss->video1_pll);
	}

	if (of_property_match_string(np, "reg-names", "pll2") >= 0) {
		dss->video2_pll = dss_video_pll_init(dss, pdev, 1,
						     pll_regulator);
		if (IS_ERR(dss->video2_pll)) {
			dss_video_pll_uninit(dss->video1_pll);
			return PTR_ERR(dss->video2_pll);
		}
	}

	return 0;
}

/* DSS HW IP initialisation */
static const struct of_device_id dss_of_match[] = {
	{ .compatible = "ti,omap2-dss", .data = &omap24xx_dss_feats },
	{ .compatible = "ti,omap3-dss", .data = &omap3630_dss_feats },
	{ .compatible = "ti,omap4-dss", .data = &omap44xx_dss_feats },
	{ .compatible = "ti,omap5-dss", .data = &omap54xx_dss_feats },
	{ .compatible = "ti,dra7-dss",  .data = &dra7xx_dss_feats },
	{},
};
MODULE_DEVICE_TABLE(of, dss_of_match);

static const struct soc_device_attribute dss_soc_devices[] = {
	{ .machine = "OMAP3430/3530", .data = &omap34xx_dss_feats },
	{ .machine = "AM35??",        .data = &omap34xx_dss_feats },
	{ .family  = "AM43xx",        .data = &am43xx_dss_feats },
	{ /* sentinel */ }
};

static int dss_bind(struct device *dev)
{
	struct dss_device *dss = dev_get_drvdata(dev);
	struct platform_device *drm_pdev;
	int r;

	r = component_bind_all(dev, NULL);
	if (r)
		return r;

	pm_set_vt_switch(0);

	omapdss_set_dss(dss);

	drm_pdev = platform_device_register_simple("omapdrm", 0, NULL, 0);
	if (IS_ERR(drm_pdev)) {
		component_unbind_all(dev, NULL);
		return PTR_ERR(drm_pdev);
	}

	dss->drm_pdev = drm_pdev;

	return 0;
}

static void dss_unbind(struct device *dev)
{
	struct dss_device *dss = dev_get_drvdata(dev);

	platform_device_unregister(dss->drm_pdev);

	omapdss_set_dss(NULL);

	component_unbind_all(dev, NULL);
}

static const struct component_master_ops dss_component_ops = {
	.bind = dss_bind,
	.unbind = dss_unbind,
};

static int dss_component_compare(struct device *dev, void *data)
{
	struct device *child = data;
	return dev == child;
}

struct dss_component_match_data {
	struct device *dev;
	struct component_match **match;
};

static int dss_add_child_component(struct device *dev, void *data)
{
	struct dss_component_match_data *cmatch = data;
	struct component_match **match = cmatch->match;

	/*
	 * HACK
	 * We don't have a working driver for rfbi, so skip it here always.
	 * Otherwise dss will never get probed successfully, as it will wait
	 * for rfbi to get probed.
	 */
	if (strstr(dev_name(dev), "rfbi"))
		return 0;

	/*
	 * Handle possible interconnect target modules defined within the DSS.
	 * The DSS components can be children of an interconnect target module
	 * after the device tree has been updated for the module data.
	 * See also omapdss_boot_init() for compatible fixup.
	 */
	if (strstr(dev_name(dev), "target-module"))
		return device_for_each_child(dev, cmatch,
					     dss_add_child_component);

	component_match_add(cmatch->dev, match, dss_component_compare, dev);

	return 0;
}

static int dss_probe_hardware(struct dss_device *dss)
{
	u32 rev;
	int r;

	r = dss_runtime_get(dss);
	if (r)
		return r;

	dss->dss_clk_rate = clk_get_rate(dss->dss_clk);

	/* Select DPLL */
	REG_FLD_MOD(dss, DSS_CONTROL, 0, 0, 0);

	dss_select_dispc_clk_source(dss, DSS_CLK_SRC_FCK);

#ifdef CONFIG_OMAP2_DSS_VENC
	REG_FLD_MOD(dss, DSS_CONTROL, 1, 4, 4);	/* venc dac demen */
	REG_FLD_MOD(dss, DSS_CONTROL, 1, 3, 3);	/* venc clock 4x enable */
	REG_FLD_MOD(dss, DSS_CONTROL, 0, 2, 2);	/* venc clock mode = normal */
#endif
	dss->dsi_clk_source[0] = DSS_CLK_SRC_FCK;
	dss->dsi_clk_source[1] = DSS_CLK_SRC_FCK;
	dss->dispc_clk_source = DSS_CLK_SRC_FCK;
	dss->lcd_clk_source[0] = DSS_CLK_SRC_FCK;
	dss->lcd_clk_source[1] = DSS_CLK_SRC_FCK;

	rev = dss_read_reg(dss, DSS_REVISION);
	pr_info("OMAP DSS rev %d.%d\n", FLD_GET(rev, 7, 4), FLD_GET(rev, 3, 0));

	dss_runtime_put(dss);

	return 0;
}

static int dss_probe(struct platform_device *pdev)
{
	const struct soc_device_attribute *soc;
	struct dss_component_match_data cmatch;
	struct component_match *match = NULL;
	struct resource *dss_mem;
	struct dss_device *dss;
	int r;

	dss = kzalloc(sizeof(*dss), GFP_KERNEL);
	if (!dss)
		return -ENOMEM;

	dss->pdev = pdev;
	platform_set_drvdata(pdev, dss);

	r = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (r) {
		dev_err(&pdev->dev, "Failed to set the DMA mask\n");
		goto err_free_dss;
	}

	/*
	 * The various OMAP3-based SoCs can't be told apart using the compatible
	 * string, use SoC device matching.
	 */
	soc = soc_device_match(dss_soc_devices);
	if (soc)
		dss->feat = soc->data;
	else
		dss->feat = of_match_device(dss_of_match, &pdev->dev)->data;

	/* Map I/O registers, get and setup clocks. */
	dss_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dss->base = devm_ioremap_resource(&pdev->dev, dss_mem);
	if (IS_ERR(dss->base)) {
		r = PTR_ERR(dss->base);
		goto err_free_dss;
	}

	r = dss_get_clocks(dss);
	if (r)
		goto err_free_dss;

	r = dss_setup_default_clock(dss);
	if (r)
		goto err_put_clocks;

	/* Setup the video PLLs and the DPI and SDI ports. */
	r = dss_video_pll_probe(dss);
	if (r)
		goto err_put_clocks;

	r = dss_init_ports(dss);
	if (r)
		goto err_uninit_plls;

	/* Enable runtime PM and probe the hardware. */
	pm_runtime_enable(&pdev->dev);

	r = dss_probe_hardware(dss);
	if (r)
		goto err_pm_runtime_disable;

	/* Initialize debugfs. */
	r = dss_initialize_debugfs(dss);
	if (r)
		goto err_pm_runtime_disable;

	dss->debugfs.clk = dss_debugfs_create_file(dss, "clk",
						   dss_debug_dump_clocks, dss);
	dss->debugfs.dss = dss_debugfs_create_file(dss, "dss", dss_dump_regs,
						   dss);

	/* Add all the child devices as components. */
	r = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (r)
		goto err_uninit_debugfs;

	omapdss_gather_components(&pdev->dev);

	cmatch.dev = &pdev->dev;
	cmatch.match = &match;
	device_for_each_child(&pdev->dev, &cmatch, dss_add_child_component);

	r = component_master_add_with_match(&pdev->dev, &dss_component_ops, match);
	if (r)
		goto err_of_depopulate;

	return 0;

err_of_depopulate:
	of_platform_depopulate(&pdev->dev);

err_uninit_debugfs:
	dss_debugfs_remove_file(dss->debugfs.clk);
	dss_debugfs_remove_file(dss->debugfs.dss);
	dss_uninitialize_debugfs(dss);

err_pm_runtime_disable:
	pm_runtime_disable(&pdev->dev);
	dss_uninit_ports(dss);

err_uninit_plls:
	if (dss->video1_pll)
		dss_video_pll_uninit(dss->video1_pll);
	if (dss->video2_pll)
		dss_video_pll_uninit(dss->video2_pll);

err_put_clocks:
	dss_put_clocks(dss);

err_free_dss:
	kfree(dss);

	return r;
}

static int dss_remove(struct platform_device *pdev)
{
	struct dss_device *dss = platform_get_drvdata(pdev);

	of_platform_depopulate(&pdev->dev);

	component_master_del(&pdev->dev, &dss_component_ops);

	dss_debugfs_remove_file(dss->debugfs.clk);
	dss_debugfs_remove_file(dss->debugfs.dss);
	dss_uninitialize_debugfs(dss);

	pm_runtime_disable(&pdev->dev);

	dss_uninit_ports(dss);

	if (dss->video1_pll)
		dss_video_pll_uninit(dss->video1_pll);

	if (dss->video2_pll)
		dss_video_pll_uninit(dss->video2_pll);

	dss_put_clocks(dss);

	kfree(dss);

	return 0;
}

static void dss_shutdown(struct platform_device *pdev)
{
	struct omap_dss_device *dssdev = NULL;

	DSSDBG("shutdown\n");

	for_each_dss_output(dssdev) {
		if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE &&
		    dssdev->ops && dssdev->ops->disable)
			dssdev->ops->disable(dssdev);
	}
}

static int dss_runtime_suspend(struct device *dev)
{
	struct dss_device *dss = dev_get_drvdata(dev);

	dss_save_context(dss);
	dss_set_min_bus_tput(dev, 0);

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int dss_runtime_resume(struct device *dev)
{
	struct dss_device *dss = dev_get_drvdata(dev);
	int r;

	pinctrl_pm_select_default_state(dev);

	/*
	 * Set an arbitrarily high tput request to ensure OPP100.
	 * What we should really do is to make a request to stay in OPP100,
	 * without any tput requirements, but that is not currently possible
	 * via the PM layer.
	 */

	r = dss_set_min_bus_tput(dev, 1000000000);
	if (r)
		return r;

	dss_restore_context(dss);
	return 0;
}

static const struct dev_pm_ops dss_pm_ops = {
	.runtime_suspend = dss_runtime_suspend,
	.runtime_resume = dss_runtime_resume,
};

struct platform_driver omap_dsshw_driver = {
	.probe		= dss_probe,
	.remove		= dss_remove,
	.shutdown	= dss_shutdown,
	.driver         = {
		.name   = "omapdss_dss",
		.pm	= &dss_pm_ops,
		.of_match_table = dss_of_match,
		.suppress_bind_attrs = true,
	},
};

/* INIT */
static struct platform_driver * const omap_dss_drivers[] = {
	&omap_dsshw_driver,
	&omap_dispchw_driver,
#ifdef CONFIG_OMAP2_DSS_DSI
	&omap_dsihw_driver,
#endif
#ifdef CONFIG_OMAP2_DSS_VENC
	&omap_venchw_driver,
#endif
#ifdef CONFIG_OMAP4_DSS_HDMI
	&omapdss_hdmi4hw_driver,
#endif
#ifdef CONFIG_OMAP5_DSS_HDMI
	&omapdss_hdmi5hw_driver,
#endif
};

static int __init omap_dss_init(void)
{
	return platform_register_drivers(omap_dss_drivers,
					 ARRAY_SIZE(omap_dss_drivers));
}

static void __exit omap_dss_exit(void)
{
	platform_unregister_drivers(omap_dss_drivers,
				    ARRAY_SIZE(omap_dss_drivers));
}

module_init(omap_dss_init);
module_exit(omap_dss_exit);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("OMAP2/3/4/5 Display Subsystem");
MODULE_LICENSE("GPL v2");
