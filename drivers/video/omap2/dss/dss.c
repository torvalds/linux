/*
 * linux/drivers/video/omap2/dss/dss.c
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define DSS_SUBSYS_NAME "DSS"

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/gfp.h>

#include <video/omapdss.h>

#include "dss.h"
#include "dss_features.h"

#define DSS_SZ_REGS			SZ_512

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

#define REG_GET(idx, start, end) \
	FLD_GET(dss_read_reg(idx), start, end)

#define REG_FLD_MOD(idx, val, start, end) \
	dss_write_reg(idx, FLD_MOD(dss_read_reg(idx), val, start, end))

static int dss_runtime_get(void);
static void dss_runtime_put(void);

struct dss_features {
	u8 fck_div_max;
	u8 dss_fck_multiplier;
	const char *clk_name;
	int (*dpi_select_source)(enum omap_channel channel);
};

static struct {
	struct platform_device *pdev;
	void __iomem    *base;

	struct clk	*dpll4_m4_ck;
	struct clk	*dss_clk;

	unsigned long	cache_req_pck;
	unsigned long	cache_prate;
	struct dss_clock_info cache_dss_cinfo;
	struct dispc_clock_info cache_dispc_cinfo;

	enum omap_dss_clk_source dsi_clk_source[MAX_NUM_DSI];
	enum omap_dss_clk_source dispc_clk_source;
	enum omap_dss_clk_source lcd_clk_source[MAX_DSS_LCD_MANAGERS];

	bool		ctx_valid;
	u32		ctx[DSS_SZ_REGS / sizeof(u32)];

	const struct dss_features *feat;
} dss;

static const char * const dss_generic_clk_source_names[] = {
	[OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DISPC]	= "DSI_PLL_HSDIV_DISPC",
	[OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DSI]	= "DSI_PLL_HSDIV_DSI",
	[OMAP_DSS_CLK_SRC_FCK]			= "DSS_FCK",
};

static inline void dss_write_reg(const struct dss_reg idx, u32 val)
{
	__raw_writel(val, dss.base + idx.idx);
}

static inline u32 dss_read_reg(const struct dss_reg idx)
{
	return __raw_readl(dss.base + idx.idx);
}

#define SR(reg) \
	dss.ctx[(DSS_##reg).idx / sizeof(u32)] = dss_read_reg(DSS_##reg)
#define RR(reg) \
	dss_write_reg(DSS_##reg, dss.ctx[(DSS_##reg).idx / sizeof(u32)])

static void dss_save_context(void)
{
	DSSDBG("dss_save_context\n");

	SR(CONTROL);

	if (dss_feat_get_supported_displays(OMAP_DSS_CHANNEL_LCD) &
			OMAP_DISPLAY_TYPE_SDI) {
		SR(SDI_CONTROL);
		SR(PLL_CONTROL);
	}

	dss.ctx_valid = true;

	DSSDBG("context saved\n");
}

static void dss_restore_context(void)
{
	DSSDBG("dss_restore_context\n");

	if (!dss.ctx_valid)
		return;

	RR(CONTROL);

	if (dss_feat_get_supported_displays(OMAP_DSS_CHANNEL_LCD) &
			OMAP_DISPLAY_TYPE_SDI) {
		RR(SDI_CONTROL);
		RR(PLL_CONTROL);
	}

	DSSDBG("context restored\n");
}

#undef SR
#undef RR

void dss_sdi_init(int datapairs)
{
	u32 l;

	BUG_ON(datapairs > 3 || datapairs < 1);

	l = dss_read_reg(DSS_SDI_CONTROL);
	l = FLD_MOD(l, 0xf, 19, 15);		/* SDI_PDIV */
	l = FLD_MOD(l, datapairs-1, 3, 2);	/* SDI_PRSEL */
	l = FLD_MOD(l, 2, 1, 0);		/* SDI_BWSEL */
	dss_write_reg(DSS_SDI_CONTROL, l);

	l = dss_read_reg(DSS_PLL_CONTROL);
	l = FLD_MOD(l, 0x7, 25, 22);	/* SDI_PLL_FREQSEL */
	l = FLD_MOD(l, 0xb, 16, 11);	/* SDI_PLL_REGN */
	l = FLD_MOD(l, 0xb4, 10, 1);	/* SDI_PLL_REGM */
	dss_write_reg(DSS_PLL_CONTROL, l);
}

int dss_sdi_enable(void)
{
	unsigned long timeout;

	dispc_pck_free_enable(1);

	/* Reset SDI PLL */
	REG_FLD_MOD(DSS_PLL_CONTROL, 1, 18, 18); /* SDI_PLL_SYSRESET */
	udelay(1);	/* wait 2x PCLK */

	/* Lock SDI PLL */
	REG_FLD_MOD(DSS_PLL_CONTROL, 1, 28, 28); /* SDI_PLL_GOBIT */

	/* Waiting for PLL lock request to complete */
	timeout = jiffies + msecs_to_jiffies(500);
	while (dss_read_reg(DSS_SDI_STATUS) & (1 << 6)) {
		if (time_after_eq(jiffies, timeout)) {
			DSSERR("PLL lock request timed out\n");
			goto err1;
		}
	}

	/* Clearing PLL_GO bit */
	REG_FLD_MOD(DSS_PLL_CONTROL, 0, 28, 28);

	/* Waiting for PLL to lock */
	timeout = jiffies + msecs_to_jiffies(500);
	while (!(dss_read_reg(DSS_SDI_STATUS) & (1 << 5))) {
		if (time_after_eq(jiffies, timeout)) {
			DSSERR("PLL lock timed out\n");
			goto err1;
		}
	}

	dispc_lcd_enable_signal(1);

	/* Waiting for SDI reset to complete */
	timeout = jiffies + msecs_to_jiffies(500);
	while (!(dss_read_reg(DSS_SDI_STATUS) & (1 << 2))) {
		if (time_after_eq(jiffies, timeout)) {
			DSSERR("SDI reset timed out\n");
			goto err2;
		}
	}

	return 0;

 err2:
	dispc_lcd_enable_signal(0);
 err1:
	/* Reset SDI PLL */
	REG_FLD_MOD(DSS_PLL_CONTROL, 0, 18, 18); /* SDI_PLL_SYSRESET */

	dispc_pck_free_enable(0);

	return -ETIMEDOUT;
}

void dss_sdi_disable(void)
{
	dispc_lcd_enable_signal(0);

	dispc_pck_free_enable(0);

	/* Reset SDI PLL */
	REG_FLD_MOD(DSS_PLL_CONTROL, 0, 18, 18); /* SDI_PLL_SYSRESET */
}

const char *dss_get_generic_clk_source_name(enum omap_dss_clk_source clk_src)
{
	return dss_generic_clk_source_names[clk_src];
}

void dss_dump_clocks(struct seq_file *s)
{
	unsigned long dpll4_ck_rate;
	unsigned long dpll4_m4_ck_rate;
	const char *fclk_name, *fclk_real_name;
	unsigned long fclk_rate;

	if (dss_runtime_get())
		return;

	seq_printf(s, "- DSS -\n");

	fclk_name = dss_get_generic_clk_source_name(OMAP_DSS_CLK_SRC_FCK);
	fclk_real_name = dss_feat_get_clk_source_name(OMAP_DSS_CLK_SRC_FCK);
	fclk_rate = clk_get_rate(dss.dss_clk);

	if (dss.dpll4_m4_ck) {
		dpll4_ck_rate = clk_get_rate(clk_get_parent(dss.dpll4_m4_ck));
		dpll4_m4_ck_rate = clk_get_rate(dss.dpll4_m4_ck);

		seq_printf(s, "dpll4_ck %lu\n", dpll4_ck_rate);

		seq_printf(s, "%s (%s) = %lu / %lu * %d  = %lu\n",
				fclk_name, fclk_real_name, dpll4_ck_rate,
				dpll4_ck_rate / dpll4_m4_ck_rate,
				dss.feat->dss_fck_multiplier, fclk_rate);
	} else {
		seq_printf(s, "%s (%s) = %lu\n",
				fclk_name, fclk_real_name,
				fclk_rate);
	}

	dss_runtime_put();
}

static void dss_dump_regs(struct seq_file *s)
{
#define DUMPREG(r) seq_printf(s, "%-35s %08x\n", #r, dss_read_reg(r))

	if (dss_runtime_get())
		return;

	DUMPREG(DSS_REVISION);
	DUMPREG(DSS_SYSCONFIG);
	DUMPREG(DSS_SYSSTATUS);
	DUMPREG(DSS_CONTROL);

	if (dss_feat_get_supported_displays(OMAP_DSS_CHANNEL_LCD) &
			OMAP_DISPLAY_TYPE_SDI) {
		DUMPREG(DSS_SDI_CONTROL);
		DUMPREG(DSS_PLL_CONTROL);
		DUMPREG(DSS_SDI_STATUS);
	}

	dss_runtime_put();
#undef DUMPREG
}

void dss_select_dispc_clk_source(enum omap_dss_clk_source clk_src)
{
	struct platform_device *dsidev;
	int b;
	u8 start, end;

	switch (clk_src) {
	case OMAP_DSS_CLK_SRC_FCK:
		b = 0;
		break;
	case OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DISPC:
		b = 1;
		dsidev = dsi_get_dsidev_from_id(0);
		dsi_wait_pll_hsdiv_dispc_active(dsidev);
		break;
	case OMAP_DSS_CLK_SRC_DSI2_PLL_HSDIV_DISPC:
		b = 2;
		dsidev = dsi_get_dsidev_from_id(1);
		dsi_wait_pll_hsdiv_dispc_active(dsidev);
		break;
	default:
		BUG();
		return;
	}

	dss_feat_get_reg_field(FEAT_REG_DISPC_CLK_SWITCH, &start, &end);

	REG_FLD_MOD(DSS_CONTROL, b, start, end);	/* DISPC_CLK_SWITCH */

	dss.dispc_clk_source = clk_src;
}

void dss_select_dsi_clk_source(int dsi_module,
		enum omap_dss_clk_source clk_src)
{
	struct platform_device *dsidev;
	int b, pos;

	switch (clk_src) {
	case OMAP_DSS_CLK_SRC_FCK:
		b = 0;
		break;
	case OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DSI:
		BUG_ON(dsi_module != 0);
		b = 1;
		dsidev = dsi_get_dsidev_from_id(0);
		dsi_wait_pll_hsdiv_dsi_active(dsidev);
		break;
	case OMAP_DSS_CLK_SRC_DSI2_PLL_HSDIV_DSI:
		BUG_ON(dsi_module != 1);
		b = 1;
		dsidev = dsi_get_dsidev_from_id(1);
		dsi_wait_pll_hsdiv_dsi_active(dsidev);
		break;
	default:
		BUG();
		return;
	}

	pos = dsi_module == 0 ? 1 : 10;
	REG_FLD_MOD(DSS_CONTROL, b, pos, pos);	/* DSIx_CLK_SWITCH */

	dss.dsi_clk_source[dsi_module] = clk_src;
}

void dss_select_lcd_clk_source(enum omap_channel channel,
		enum omap_dss_clk_source clk_src)
{
	struct platform_device *dsidev;
	int b, ix, pos;

	if (!dss_has_feature(FEAT_LCD_CLK_SRC))
		return;

	switch (clk_src) {
	case OMAP_DSS_CLK_SRC_FCK:
		b = 0;
		break;
	case OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DISPC:
		BUG_ON(channel != OMAP_DSS_CHANNEL_LCD);
		b = 1;
		dsidev = dsi_get_dsidev_from_id(0);
		dsi_wait_pll_hsdiv_dispc_active(dsidev);
		break;
	case OMAP_DSS_CLK_SRC_DSI2_PLL_HSDIV_DISPC:
		BUG_ON(channel != OMAP_DSS_CHANNEL_LCD2 &&
		       channel != OMAP_DSS_CHANNEL_LCD3);
		b = 1;
		dsidev = dsi_get_dsidev_from_id(1);
		dsi_wait_pll_hsdiv_dispc_active(dsidev);
		break;
	default:
		BUG();
		return;
	}

	pos = channel == OMAP_DSS_CHANNEL_LCD ? 0 :
	     (channel == OMAP_DSS_CHANNEL_LCD2 ? 12 : 19);
	REG_FLD_MOD(DSS_CONTROL, b, pos, pos);	/* LCDx_CLK_SWITCH */

	ix = channel == OMAP_DSS_CHANNEL_LCD ? 0 :
	    (channel == OMAP_DSS_CHANNEL_LCD2 ? 1 : 2);
	dss.lcd_clk_source[ix] = clk_src;
}

enum omap_dss_clk_source dss_get_dispc_clk_source(void)
{
	return dss.dispc_clk_source;
}

enum omap_dss_clk_source dss_get_dsi_clk_source(int dsi_module)
{
	return dss.dsi_clk_source[dsi_module];
}

enum omap_dss_clk_source dss_get_lcd_clk_source(enum omap_channel channel)
{
	if (dss_has_feature(FEAT_LCD_CLK_SRC)) {
		int ix = channel == OMAP_DSS_CHANNEL_LCD ? 0 :
			(channel == OMAP_DSS_CHANNEL_LCD2 ? 1 : 2);
		return dss.lcd_clk_source[ix];
	} else {
		/* LCD_CLK source is the same as DISPC_FCLK source for
		 * OMAP2 and OMAP3 */
		return dss.dispc_clk_source;
	}
}

int dss_set_clock_div(struct dss_clock_info *cinfo)
{
	if (dss.dpll4_m4_ck) {
		unsigned long prate;
		int r;

		prate = clk_get_rate(clk_get_parent(dss.dpll4_m4_ck));
		DSSDBG("dpll4_m4 = %ld\n", prate);

		r = clk_set_rate(dss.dpll4_m4_ck, prate / cinfo->fck_div);
		if (r)
			return r;
	} else {
		if (cinfo->fck_div != 0)
			return -EINVAL;
	}

	DSSDBG("fck = %ld (%d)\n", cinfo->fck, cinfo->fck_div);

	return 0;
}

unsigned long dss_get_dpll4_rate(void)
{
	if (dss.dpll4_m4_ck)
		return clk_get_rate(clk_get_parent(dss.dpll4_m4_ck));
	else
		return 0;
}

int dss_calc_clock_div(unsigned long req_pck, struct dss_clock_info *dss_cinfo,
		struct dispc_clock_info *dispc_cinfo)
{
	unsigned long prate;
	struct dss_clock_info best_dss;
	struct dispc_clock_info best_dispc;

	unsigned long fck, max_dss_fck;

	u16 fck_div;

	int match = 0;
	int min_fck_per_pck;

	prate = dss_get_dpll4_rate();

	max_dss_fck = dss_feat_get_param_max(FEAT_PARAM_DSS_FCK);

	fck = clk_get_rate(dss.dss_clk);
	if (req_pck == dss.cache_req_pck && prate == dss.cache_prate &&
		dss.cache_dss_cinfo.fck == fck) {
		DSSDBG("dispc clock info found from cache.\n");
		*dss_cinfo = dss.cache_dss_cinfo;
		*dispc_cinfo = dss.cache_dispc_cinfo;
		return 0;
	}

	min_fck_per_pck = CONFIG_OMAP2_DSS_MIN_FCK_PER_PCK;

	if (min_fck_per_pck &&
		req_pck * min_fck_per_pck > max_dss_fck) {
		DSSERR("Requested pixel clock not possible with the current "
				"OMAP2_DSS_MIN_FCK_PER_PCK setting. Turning "
				"the constraint off.\n");
		min_fck_per_pck = 0;
	}

retry:
	memset(&best_dss, 0, sizeof(best_dss));
	memset(&best_dispc, 0, sizeof(best_dispc));

	if (dss.dpll4_m4_ck == NULL) {
		struct dispc_clock_info cur_dispc;
		/* XXX can we change the clock on omap2? */
		fck = clk_get_rate(dss.dss_clk);
		fck_div = 1;

		dispc_find_clk_divs(req_pck, fck, &cur_dispc);
		match = 1;

		best_dss.fck = fck;
		best_dss.fck_div = fck_div;

		best_dispc = cur_dispc;

		goto found;
	} else {
		for (fck_div = dss.feat->fck_div_max; fck_div > 0; --fck_div) {
			struct dispc_clock_info cur_dispc;

			fck = prate / fck_div * dss.feat->dss_fck_multiplier;

			if (fck > max_dss_fck)
				continue;

			if (min_fck_per_pck &&
					fck < req_pck * min_fck_per_pck)
				continue;

			match = 1;

			dispc_find_clk_divs(req_pck, fck, &cur_dispc);

			if (abs(cur_dispc.pck - req_pck) <
					abs(best_dispc.pck - req_pck)) {

				best_dss.fck = fck;
				best_dss.fck_div = fck_div;

				best_dispc = cur_dispc;

				if (cur_dispc.pck == req_pck)
					goto found;
			}
		}
	}

found:
	if (!match) {
		if (min_fck_per_pck) {
			DSSERR("Could not find suitable clock settings.\n"
					"Turning FCK/PCK constraint off and"
					"trying again.\n");
			min_fck_per_pck = 0;
			goto retry;
		}

		DSSERR("Could not find suitable clock settings.\n");

		return -EINVAL;
	}

	if (dss_cinfo)
		*dss_cinfo = best_dss;
	if (dispc_cinfo)
		*dispc_cinfo = best_dispc;

	dss.cache_req_pck = req_pck;
	dss.cache_prate = prate;
	dss.cache_dss_cinfo = best_dss;
	dss.cache_dispc_cinfo = best_dispc;

	return 0;
}

void dss_set_venc_output(enum omap_dss_venc_type type)
{
	int l = 0;

	if (type == OMAP_DSS_VENC_TYPE_COMPOSITE)
		l = 0;
	else if (type == OMAP_DSS_VENC_TYPE_SVIDEO)
		l = 1;
	else
		BUG();

	/* venc out selection. 0 = comp, 1 = svideo */
	REG_FLD_MOD(DSS_CONTROL, l, 6, 6);
}

void dss_set_dac_pwrdn_bgz(bool enable)
{
	REG_FLD_MOD(DSS_CONTROL, enable, 5, 5);	/* DAC Power-Down Control */
}

void dss_select_hdmi_venc_clk_source(enum dss_hdmi_venc_clk_source_select src)
{
	enum omap_display_type dp;
	dp = dss_feat_get_supported_displays(OMAP_DSS_CHANNEL_DIGIT);

	/* Complain about invalid selections */
	WARN_ON((src == DSS_VENC_TV_CLK) && !(dp & OMAP_DISPLAY_TYPE_VENC));
	WARN_ON((src == DSS_HDMI_M_PCLK) && !(dp & OMAP_DISPLAY_TYPE_HDMI));

	/* Select only if we have options */
	if ((dp & OMAP_DISPLAY_TYPE_VENC) && (dp & OMAP_DISPLAY_TYPE_HDMI))
		REG_FLD_MOD(DSS_CONTROL, src, 15, 15);	/* VENC_HDMI_SWITCH */
}

enum dss_hdmi_venc_clk_source_select dss_get_hdmi_venc_clk_source(void)
{
	enum omap_display_type displays;

	displays = dss_feat_get_supported_displays(OMAP_DSS_CHANNEL_DIGIT);
	if ((displays & OMAP_DISPLAY_TYPE_HDMI) == 0)
		return DSS_VENC_TV_CLK;

	if ((displays & OMAP_DISPLAY_TYPE_VENC) == 0)
		return DSS_HDMI_M_PCLK;

	return REG_GET(DSS_CONTROL, 15, 15);
}

static int dss_dpi_select_source_omap2_omap3(enum omap_channel channel)
{
	if (channel != OMAP_DSS_CHANNEL_LCD)
		return -EINVAL;

	return 0;
}

static int dss_dpi_select_source_omap4(enum omap_channel channel)
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

	REG_FLD_MOD(DSS_CONTROL, val, 17, 17);

	return 0;
}

static int dss_dpi_select_source_omap5(enum omap_channel channel)
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

	REG_FLD_MOD(DSS_CONTROL, val, 17, 16);

	return 0;
}

int dss_dpi_select_source(enum omap_channel channel)
{
	return dss.feat->dpi_select_source(channel);
}

static int dss_get_clocks(void)
{
	struct clk *clk;
	int r;

	clk = clk_get(&dss.pdev->dev, "fck");
	if (IS_ERR(clk)) {
		DSSERR("can't get clock fck\n");
		r = PTR_ERR(clk);
		goto err;
	}

	dss.dss_clk = clk;

	if (dss.feat->clk_name) {
		clk = clk_get(NULL, dss.feat->clk_name);
		if (IS_ERR(clk)) {
			DSSERR("Failed to get %s\n", dss.feat->clk_name);
			r = PTR_ERR(clk);
			goto err;
		}
	} else {
		clk = NULL;
	}

	dss.dpll4_m4_ck = clk;

	return 0;

err:
	if (dss.dss_clk)
		clk_put(dss.dss_clk);
	if (dss.dpll4_m4_ck)
		clk_put(dss.dpll4_m4_ck);

	return r;
}

static void dss_put_clocks(void)
{
	if (dss.dpll4_m4_ck)
		clk_put(dss.dpll4_m4_ck);
	clk_put(dss.dss_clk);
}

static int dss_runtime_get(void)
{
	int r;

	DSSDBG("dss_runtime_get\n");

	r = pm_runtime_get_sync(&dss.pdev->dev);
	WARN_ON(r < 0);
	return r < 0 ? r : 0;
}

static void dss_runtime_put(void)
{
	int r;

	DSSDBG("dss_runtime_put\n");

	r = pm_runtime_put_sync(&dss.pdev->dev);
	WARN_ON(r < 0 && r != -ENOSYS && r != -EBUSY);
}

/* DEBUGFS */
#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_OMAP2_DSS_DEBUG_SUPPORT)
void dss_debug_dump_clocks(struct seq_file *s)
{
	dss_dump_clocks(s);
	dispc_dump_clocks(s);
#ifdef CONFIG_OMAP2_DSS_DSI
	dsi_dump_clocks(s);
#endif
}
#endif

static const struct dss_features omap24xx_dss_feats __initconst = {
	.fck_div_max		=	16,
	.dss_fck_multiplier	=	2,
	.clk_name		=	NULL,
	.dpi_select_source	=	&dss_dpi_select_source_omap2_omap3,
};

static const struct dss_features omap34xx_dss_feats __initconst = {
	.fck_div_max		=	16,
	.dss_fck_multiplier	=	2,
	.clk_name		=	"dpll4_m4_ck",
	.dpi_select_source	=	&dss_dpi_select_source_omap2_omap3,
};

static const struct dss_features omap3630_dss_feats __initconst = {
	.fck_div_max		=	32,
	.dss_fck_multiplier	=	1,
	.clk_name		=	"dpll4_m4_ck",
	.dpi_select_source	=	&dss_dpi_select_source_omap2_omap3,
};

static const struct dss_features omap44xx_dss_feats __initconst = {
	.fck_div_max		=	32,
	.dss_fck_multiplier	=	1,
	.clk_name		=	"dpll_per_m5x2_ck",
	.dpi_select_source	=	&dss_dpi_select_source_omap4,
};

static const struct dss_features omap54xx_dss_feats __initconst = {
	.fck_div_max		=	64,
	.dss_fck_multiplier	=	1,
	.clk_name		=	"dpll_per_h12x2_ck",
	.dpi_select_source	=	&dss_dpi_select_source_omap5,
};

static int __init dss_init_features(struct platform_device *pdev)
{
	struct omap_dss_board_info *pdata = pdev->dev.platform_data;
	const struct dss_features *src;
	struct dss_features *dst;

	dst = devm_kzalloc(&pdev->dev, sizeof(*dst), GFP_KERNEL);
	if (!dst) {
		dev_err(&pdev->dev, "Failed to allocate local DSS Features\n");
		return -ENOMEM;
	}

	switch (pdata->version) {
	case OMAPDSS_VER_OMAP24xx:
		src = &omap24xx_dss_feats;
		break;

	case OMAPDSS_VER_OMAP34xx_ES1:
	case OMAPDSS_VER_OMAP34xx_ES3:
	case OMAPDSS_VER_AM35xx:
		src = &omap34xx_dss_feats;
		break;

	case OMAPDSS_VER_OMAP3630:
		src = &omap3630_dss_feats;
		break;

	case OMAPDSS_VER_OMAP4430_ES1:
	case OMAPDSS_VER_OMAP4430_ES2:
	case OMAPDSS_VER_OMAP4:
		src = &omap44xx_dss_feats;
		break;

	case OMAPDSS_VER_OMAP5:
		src = &omap54xx_dss_feats;
		break;

	default:
		return -ENODEV;
	}

	memcpy(dst, src, sizeof(*dst));
	dss.feat = dst;

	return 0;
}

/* DSS HW IP initialisation */
static int __init omap_dsshw_probe(struct platform_device *pdev)
{
	struct resource *dss_mem;
	u32 rev;
	int r;

	dss.pdev = pdev;

	r = dss_init_features(dss.pdev);
	if (r)
		return r;

	dss_mem = platform_get_resource(dss.pdev, IORESOURCE_MEM, 0);
	if (!dss_mem) {
		DSSERR("can't get IORESOURCE_MEM DSS\n");
		return -EINVAL;
	}

	dss.base = devm_ioremap(&pdev->dev, dss_mem->start,
				resource_size(dss_mem));
	if (!dss.base) {
		DSSERR("can't ioremap DSS\n");
		return -ENOMEM;
	}

	r = dss_get_clocks();
	if (r)
		return r;

	pm_runtime_enable(&pdev->dev);

	r = dss_runtime_get();
	if (r)
		goto err_runtime_get;

	/* Select DPLL */
	REG_FLD_MOD(DSS_CONTROL, 0, 0, 0);

#ifdef CONFIG_OMAP2_DSS_VENC
	REG_FLD_MOD(DSS_CONTROL, 1, 4, 4);	/* venc dac demen */
	REG_FLD_MOD(DSS_CONTROL, 1, 3, 3);	/* venc clock 4x enable */
	REG_FLD_MOD(DSS_CONTROL, 0, 2, 2);	/* venc clock mode = normal */
#endif
	dss.dsi_clk_source[0] = OMAP_DSS_CLK_SRC_FCK;
	dss.dsi_clk_source[1] = OMAP_DSS_CLK_SRC_FCK;
	dss.dispc_clk_source = OMAP_DSS_CLK_SRC_FCK;
	dss.lcd_clk_source[0] = OMAP_DSS_CLK_SRC_FCK;
	dss.lcd_clk_source[1] = OMAP_DSS_CLK_SRC_FCK;

	rev = dss_read_reg(DSS_REVISION);
	printk(KERN_INFO "OMAP DSS rev %d.%d\n",
			FLD_GET(rev, 7, 4), FLD_GET(rev, 3, 0));

	dss_runtime_put();

	dss_debugfs_create_file("dss", dss_dump_regs);

	return 0;

err_runtime_get:
	pm_runtime_disable(&pdev->dev);
	dss_put_clocks();
	return r;
}

static int __exit omap_dsshw_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	dss_put_clocks();

	return 0;
}

static int dss_runtime_suspend(struct device *dev)
{
	dss_save_context();
	dss_set_min_bus_tput(dev, 0);
	return 0;
}

static int dss_runtime_resume(struct device *dev)
{
	int r;
	/*
	 * Set an arbitrarily high tput request to ensure OPP100.
	 * What we should really do is to make a request to stay in OPP100,
	 * without any tput requirements, but that is not currently possible
	 * via the PM layer.
	 */

	r = dss_set_min_bus_tput(dev, 1000000000);
	if (r)
		return r;

	dss_restore_context();
	return 0;
}

static const struct dev_pm_ops dss_pm_ops = {
	.runtime_suspend = dss_runtime_suspend,
	.runtime_resume = dss_runtime_resume,
};

static struct platform_driver omap_dsshw_driver = {
	.remove         = __exit_p(omap_dsshw_remove),
	.driver         = {
		.name   = "omapdss_dss",
		.owner  = THIS_MODULE,
		.pm	= &dss_pm_ops,
	},
};

int __init dss_init_platform_driver(void)
{
	return platform_driver_probe(&omap_dsshw_driver, omap_dsshw_probe);
}

void dss_uninit_platform_driver(void)
{
	platform_driver_unregister(&omap_dsshw_driver);
}
