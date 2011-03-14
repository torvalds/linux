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
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/clk.h>

#include <plat/display.h>
#include <plat/clock.h>
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
#define DSS_IRQSTATUS			DSS_REG(0x0018)
#define DSS_CONTROL			DSS_REG(0x0040)
#define DSS_SDI_CONTROL			DSS_REG(0x0044)
#define DSS_PLL_CONTROL			DSS_REG(0x0048)
#define DSS_SDI_STATUS			DSS_REG(0x005C)

#define REG_GET(idx, start, end) \
	FLD_GET(dss_read_reg(idx), start, end)

#define REG_FLD_MOD(idx, val, start, end) \
	dss_write_reg(idx, FLD_MOD(dss_read_reg(idx), val, start, end))

static struct {
	struct platform_device *pdev;
	void __iomem    *base;
	int             ctx_id;

	struct clk	*dpll4_m4_ck;
	struct clk	*dss_ick;
	struct clk	*dss_fck;
	struct clk	*dss_sys_clk;
	struct clk	*dss_tv_fck;
	struct clk	*dss_video_fck;
	unsigned	num_clks_enabled;

	unsigned long	cache_req_pck;
	unsigned long	cache_prate;
	struct dss_clock_info cache_dss_cinfo;
	struct dispc_clock_info cache_dispc_cinfo;

	enum dss_clk_source dsi_clk_source;
	enum dss_clk_source dispc_clk_source;
	enum dss_clk_source lcd_clk_source[MAX_DSS_LCD_MANAGERS];

	u32		ctx[DSS_SZ_REGS / sizeof(u32)];
} dss;

static const char * const dss_generic_clk_source_names[] = {
	[DSS_CLK_SRC_DSI_PLL_HSDIV_DISPC]	= "DSI_PLL_HSDIV_DISPC",
	[DSS_CLK_SRC_DSI_PLL_HSDIV_DSI]		= "DSI_PLL_HSDIV_DSI",
	[DSS_CLK_SRC_FCK]			= "DSS_FCK",
};

static void dss_clk_enable_all_no_ctx(void);
static void dss_clk_disable_all_no_ctx(void);
static void dss_clk_enable_no_ctx(enum dss_clock clks);
static void dss_clk_disable_no_ctx(enum dss_clock clks);

static int _omap_dss_wait_reset(void);

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

void dss_save_context(void)
{
	if (cpu_is_omap24xx())
		return;

	SR(SYSCONFIG);
	SR(CONTROL);

	if (dss_feat_get_supported_displays(OMAP_DSS_CHANNEL_LCD) &
			OMAP_DISPLAY_TYPE_SDI) {
		SR(SDI_CONTROL);
		SR(PLL_CONTROL);
	}
}

void dss_restore_context(void)
{
	if (_omap_dss_wait_reset())
		DSSERR("DSS not coming out of reset after sleep\n");

	RR(SYSCONFIG);
	RR(CONTROL);

	if (dss_feat_get_supported_displays(OMAP_DSS_CHANNEL_LCD) &
			OMAP_DISPLAY_TYPE_SDI) {
		RR(SDI_CONTROL);
		RR(PLL_CONTROL);
	}
}

#undef SR
#undef RR

void dss_sdi_init(u8 datapairs)
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

const char *dss_get_generic_clk_source_name(enum dss_clk_source clk_src)
{
	return dss_generic_clk_source_names[clk_src];
}

void dss_dump_clocks(struct seq_file *s)
{
	unsigned long dpll4_ck_rate;
	unsigned long dpll4_m4_ck_rate;
	const char *fclk_name, *fclk_real_name;
	unsigned long fclk_rate;

	dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK);

	seq_printf(s, "- DSS -\n");

	fclk_name = dss_get_generic_clk_source_name(DSS_CLK_SRC_FCK);
	fclk_real_name = dss_feat_get_clk_source_name(DSS_CLK_SRC_FCK);
	fclk_rate = dss_clk_get_rate(DSS_CLK_FCK);

	if (dss.dpll4_m4_ck) {
		dpll4_ck_rate = clk_get_rate(clk_get_parent(dss.dpll4_m4_ck));
		dpll4_m4_ck_rate = clk_get_rate(dss.dpll4_m4_ck);

		seq_printf(s, "dpll4_ck %lu\n", dpll4_ck_rate);

		if (cpu_is_omap3630() || cpu_is_omap44xx())
			seq_printf(s, "%s (%s) = %lu / %lu  = %lu\n",
					fclk_name, fclk_real_name,
					dpll4_ck_rate,
					dpll4_ck_rate / dpll4_m4_ck_rate,
					fclk_rate);
		else
			seq_printf(s, "%s (%s) = %lu / %lu * 2 = %lu\n",
					fclk_name, fclk_real_name,
					dpll4_ck_rate,
					dpll4_ck_rate / dpll4_m4_ck_rate,
					fclk_rate);
	} else {
		seq_printf(s, "%s (%s) = %lu\n",
				fclk_name, fclk_real_name,
				fclk_rate);
	}

	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK);
}

void dss_dump_regs(struct seq_file *s)
{
#define DUMPREG(r) seq_printf(s, "%-35s %08x\n", #r, dss_read_reg(r))

	dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK);

	DUMPREG(DSS_REVISION);
	DUMPREG(DSS_SYSCONFIG);
	DUMPREG(DSS_SYSSTATUS);
	DUMPREG(DSS_IRQSTATUS);
	DUMPREG(DSS_CONTROL);

	if (dss_feat_get_supported_displays(OMAP_DSS_CHANNEL_LCD) &
			OMAP_DISPLAY_TYPE_SDI) {
		DUMPREG(DSS_SDI_CONTROL);
		DUMPREG(DSS_PLL_CONTROL);
		DUMPREG(DSS_SDI_STATUS);
	}

	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK);
#undef DUMPREG
}

void dss_select_dispc_clk_source(enum dss_clk_source clk_src)
{
	int b;
	u8 start, end;

	switch (clk_src) {
	case DSS_CLK_SRC_FCK:
		b = 0;
		break;
	case DSS_CLK_SRC_DSI_PLL_HSDIV_DISPC:
		b = 1;
		dsi_wait_pll_hsdiv_dispc_active();
		break;
	default:
		BUG();
	}

	dss_feat_get_reg_field(FEAT_REG_DISPC_CLK_SWITCH, &start, &end);

	REG_FLD_MOD(DSS_CONTROL, b, start, end);	/* DISPC_CLK_SWITCH */

	dss.dispc_clk_source = clk_src;
}

void dss_select_dsi_clk_source(enum dss_clk_source clk_src)
{
	int b;

	switch (clk_src) {
	case DSS_CLK_SRC_FCK:
		b = 0;
		break;
	case DSS_CLK_SRC_DSI_PLL_HSDIV_DSI:
		b = 1;
		dsi_wait_pll_hsdiv_dsi_active();
		break;
	default:
		BUG();
	}

	REG_FLD_MOD(DSS_CONTROL, b, 1, 1);	/* DSI_CLK_SWITCH */

	dss.dsi_clk_source = clk_src;
}

void dss_select_lcd_clk_source(enum omap_channel channel,
		enum dss_clk_source clk_src)
{
	int b, ix, pos;

	if (!dss_has_feature(FEAT_LCD_CLK_SRC))
		return;

	switch (clk_src) {
	case DSS_CLK_SRC_FCK:
		b = 0;
		break;
	case DSS_CLK_SRC_DSI_PLL_HSDIV_DISPC:
		BUG_ON(channel != OMAP_DSS_CHANNEL_LCD);
		b = 1;
		dsi_wait_pll_hsdiv_dispc_active();
		break;
	default:
		BUG();
	}

	pos = channel == OMAP_DSS_CHANNEL_LCD ? 0 : 12;
	REG_FLD_MOD(DSS_CONTROL, b, pos, pos);	/* LCDx_CLK_SWITCH */

	ix = channel == OMAP_DSS_CHANNEL_LCD ? 0 : 1;
	dss.lcd_clk_source[ix] = clk_src;
}

enum dss_clk_source dss_get_dispc_clk_source(void)
{
	return dss.dispc_clk_source;
}

enum dss_clk_source dss_get_dsi_clk_source(void)
{
	return dss.dsi_clk_source;
}

enum dss_clk_source dss_get_lcd_clk_source(enum omap_channel channel)
{
	int ix = channel == OMAP_DSS_CHANNEL_LCD ? 0 : 1;
	return dss.lcd_clk_source[ix];
}

/* calculate clock rates using dividers in cinfo */
int dss_calc_clock_rates(struct dss_clock_info *cinfo)
{
	if (dss.dpll4_m4_ck) {
		unsigned long prate;
		u16 fck_div_max = 16;

		if (cpu_is_omap3630() || cpu_is_omap44xx())
			fck_div_max = 32;

		if (cinfo->fck_div > fck_div_max || cinfo->fck_div == 0)
			return -EINVAL;

		prate = clk_get_rate(clk_get_parent(dss.dpll4_m4_ck));

		cinfo->fck = prate / cinfo->fck_div;
	} else {
		if (cinfo->fck_div != 0)
			return -EINVAL;
		cinfo->fck = dss_clk_get_rate(DSS_CLK_FCK);
	}

	return 0;
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

int dss_get_clock_div(struct dss_clock_info *cinfo)
{
	cinfo->fck = dss_clk_get_rate(DSS_CLK_FCK);

	if (dss.dpll4_m4_ck) {
		unsigned long prate;

		prate = clk_get_rate(clk_get_parent(dss.dpll4_m4_ck));

		if (cpu_is_omap3630() || cpu_is_omap44xx())
			cinfo->fck_div = prate / (cinfo->fck);
		else
			cinfo->fck_div = prate / (cinfo->fck / 2);
	} else {
		cinfo->fck_div = 0;
	}

	return 0;
}

unsigned long dss_get_dpll4_rate(void)
{
	if (dss.dpll4_m4_ck)
		return clk_get_rate(clk_get_parent(dss.dpll4_m4_ck));
	else
		return 0;
}

int dss_calc_clock_div(bool is_tft, unsigned long req_pck,
		struct dss_clock_info *dss_cinfo,
		struct dispc_clock_info *dispc_cinfo)
{
	unsigned long prate;
	struct dss_clock_info best_dss;
	struct dispc_clock_info best_dispc;

	unsigned long fck, max_dss_fck;

	u16 fck_div, fck_div_max = 16;

	int match = 0;
	int min_fck_per_pck;

	prate = dss_get_dpll4_rate();

	max_dss_fck = dss_feat_get_param_max(FEAT_PARAM_DSS_FCK);

	fck = dss_clk_get_rate(DSS_CLK_FCK);
	if (req_pck == dss.cache_req_pck &&
			((cpu_is_omap34xx() && prate == dss.cache_prate) ||
			 dss.cache_dss_cinfo.fck == fck)) {
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
		fck = dss_clk_get_rate(DSS_CLK_FCK);
		fck_div = 1;

		dispc_find_clk_divs(is_tft, req_pck, fck, &cur_dispc);
		match = 1;

		best_dss.fck = fck;
		best_dss.fck_div = fck_div;

		best_dispc = cur_dispc;

		goto found;
	} else {
		if (cpu_is_omap3630() || cpu_is_omap44xx())
			fck_div_max = 32;

		for (fck_div = fck_div_max; fck_div > 0; --fck_div) {
			struct dispc_clock_info cur_dispc;

			if (fck_div_max == 32)
				fck = prate / fck_div;
			else
				fck = prate / fck_div * 2;

			if (fck > max_dss_fck)
				continue;

			if (min_fck_per_pck &&
					fck < req_pck * min_fck_per_pck)
				continue;

			match = 1;

			dispc_find_clk_divs(is_tft, req_pck, fck, &cur_dispc);

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

static int _omap_dss_wait_reset(void)
{
	int t = 0;

	while (REG_GET(DSS_SYSSTATUS, 0, 0) == 0) {
		if (++t > 1000) {
			DSSERR("soft reset failed\n");
			return -ENODEV;
		}
		udelay(1);
	}

	return 0;
}

static int _omap_dss_reset(void)
{
	/* Soft reset */
	REG_FLD_MOD(DSS_SYSCONFIG, 1, 1, 1);
	return _omap_dss_wait_reset();
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

void dss_select_hdmi_venc_clk_source(enum dss_hdmi_venc_clk_source_select hdmi)
{
	REG_FLD_MOD(DSS_CONTROL, hdmi, 15, 15);	/* VENC_HDMI_SWITCH */
}

static int dss_init(void)
{
	int r;
	u32 rev;
	struct resource *dss_mem;
	struct clk *dpll4_m4_ck;

	dss_mem = platform_get_resource(dss.pdev, IORESOURCE_MEM, 0);
	if (!dss_mem) {
		DSSERR("can't get IORESOURCE_MEM DSS\n");
		r = -EINVAL;
		goto fail0;
	}
	dss.base = ioremap(dss_mem->start, resource_size(dss_mem));
	if (!dss.base) {
		DSSERR("can't ioremap DSS\n");
		r = -ENOMEM;
		goto fail0;
	}

	/* disable LCD and DIGIT output. This seems to fix the synclost
	 * problem that we get, if the bootloader starts the DSS and
	 * the kernel resets it */
	omap_writel(omap_readl(0x48050440) & ~0x3, 0x48050440);

	/* We need to wait here a bit, otherwise we sometimes start to
	 * get synclost errors, and after that only power cycle will
	 * restore DSS functionality. I have no idea why this happens.
	 * And we have to wait _before_ resetting the DSS, but after
	 * enabling clocks.
	 */
	msleep(50);

	_omap_dss_reset();

	/* autoidle */
	REG_FLD_MOD(DSS_SYSCONFIG, 1, 0, 0);

	/* Select DPLL */
	REG_FLD_MOD(DSS_CONTROL, 0, 0, 0);

#ifdef CONFIG_OMAP2_DSS_VENC
	REG_FLD_MOD(DSS_CONTROL, 1, 4, 4);	/* venc dac demen */
	REG_FLD_MOD(DSS_CONTROL, 1, 3, 3);	/* venc clock 4x enable */
	REG_FLD_MOD(DSS_CONTROL, 0, 2, 2);	/* venc clock mode = normal */
#endif
	if (cpu_is_omap34xx()) {
		dpll4_m4_ck = clk_get(NULL, "dpll4_m4_ck");
		if (IS_ERR(dpll4_m4_ck)) {
			DSSERR("Failed to get dpll4_m4_ck\n");
			r = PTR_ERR(dpll4_m4_ck);
			goto fail1;
		}
	} else if (cpu_is_omap44xx()) {
		dpll4_m4_ck = clk_get(NULL, "dpll_per_m5x2_ck");
		if (IS_ERR(dpll4_m4_ck)) {
			DSSERR("Failed to get dpll4_m4_ck\n");
			r = PTR_ERR(dpll4_m4_ck);
			goto fail1;
		}
	} else { /* omap24xx */
		dpll4_m4_ck = NULL;
	}

	dss.dpll4_m4_ck = dpll4_m4_ck;

	dss.dsi_clk_source = DSS_CLK_SRC_FCK;
	dss.dispc_clk_source = DSS_CLK_SRC_FCK;
	dss.lcd_clk_source[0] = DSS_CLK_SRC_FCK;
	dss.lcd_clk_source[1] = DSS_CLK_SRC_FCK;

	dss_save_context();

	rev = dss_read_reg(DSS_REVISION);
	printk(KERN_INFO "OMAP DSS rev %d.%d\n",
			FLD_GET(rev, 7, 4), FLD_GET(rev, 3, 0));

	return 0;

fail1:
	iounmap(dss.base);
fail0:
	return r;
}

static void dss_exit(void)
{
	if (dss.dpll4_m4_ck)
		clk_put(dss.dpll4_m4_ck);

	iounmap(dss.base);
}

/* CONTEXT */
static int dss_get_ctx_id(void)
{
	struct omap_display_platform_data *pdata = dss.pdev->dev.platform_data;
	int r;

	if (!pdata->board_data->get_last_off_on_transaction_id)
		return 0;
	r = pdata->board_data->get_last_off_on_transaction_id(&dss.pdev->dev);
	if (r < 0) {
		dev_err(&dss.pdev->dev, "getting transaction ID failed, "
				"will force context restore\n");
		r = -1;
	}
	return r;
}

int dss_need_ctx_restore(void)
{
	int id = dss_get_ctx_id();

	if (id < 0 || id != dss.ctx_id) {
		DSSDBG("ctx id %d -> id %d\n",
				dss.ctx_id, id);
		dss.ctx_id = id;
		return 1;
	} else {
		return 0;
	}
}

static void save_all_ctx(void)
{
	DSSDBG("save context\n");

	dss_clk_enable_no_ctx(DSS_CLK_ICK | DSS_CLK_FCK);

	dss_save_context();
	dispc_save_context();
#ifdef CONFIG_OMAP2_DSS_DSI
	dsi_save_context();
#endif

	dss_clk_disable_no_ctx(DSS_CLK_ICK | DSS_CLK_FCK);
}

static void restore_all_ctx(void)
{
	DSSDBG("restore context\n");

	dss_clk_enable_all_no_ctx();

	dss_restore_context();
	dispc_restore_context();
#ifdef CONFIG_OMAP2_DSS_DSI
	dsi_restore_context();
#endif

	dss_clk_disable_all_no_ctx();
}

static int dss_get_clock(struct clk **clock, const char *clk_name)
{
	struct clk *clk;

	clk = clk_get(&dss.pdev->dev, clk_name);

	if (IS_ERR(clk)) {
		DSSERR("can't get clock %s", clk_name);
		return PTR_ERR(clk);
	}

	*clock = clk;

	DSSDBG("clk %s, rate %ld\n", clk_name, clk_get_rate(clk));

	return 0;
}

static int dss_get_clocks(void)
{
	int r;
	struct omap_display_platform_data *pdata = dss.pdev->dev.platform_data;

	dss.dss_ick = NULL;
	dss.dss_fck = NULL;
	dss.dss_sys_clk = NULL;
	dss.dss_tv_fck = NULL;
	dss.dss_video_fck = NULL;

	r = dss_get_clock(&dss.dss_ick, "ick");
	if (r)
		goto err;

	r = dss_get_clock(&dss.dss_fck, "fck");
	if (r)
		goto err;

	if (!pdata->opt_clock_available) {
		r = -ENODEV;
		goto err;
	}

	if (pdata->opt_clock_available("sys_clk")) {
		r = dss_get_clock(&dss.dss_sys_clk, "sys_clk");
		if (r)
			goto err;
	}

	if (pdata->opt_clock_available("tv_clk")) {
		r = dss_get_clock(&dss.dss_tv_fck, "tv_clk");
		if (r)
			goto err;
	}

	if (pdata->opt_clock_available("video_clk")) {
		r = dss_get_clock(&dss.dss_video_fck, "video_clk");
		if (r)
			goto err;
	}

	return 0;

err:
	if (dss.dss_ick)
		clk_put(dss.dss_ick);
	if (dss.dss_fck)
		clk_put(dss.dss_fck);
	if (dss.dss_sys_clk)
		clk_put(dss.dss_sys_clk);
	if (dss.dss_tv_fck)
		clk_put(dss.dss_tv_fck);
	if (dss.dss_video_fck)
		clk_put(dss.dss_video_fck);

	return r;
}

static void dss_put_clocks(void)
{
	if (dss.dss_video_fck)
		clk_put(dss.dss_video_fck);
	if (dss.dss_tv_fck)
		clk_put(dss.dss_tv_fck);
	if (dss.dss_sys_clk)
		clk_put(dss.dss_sys_clk);
	clk_put(dss.dss_fck);
	clk_put(dss.dss_ick);
}

unsigned long dss_clk_get_rate(enum dss_clock clk)
{
	switch (clk) {
	case DSS_CLK_ICK:
		return clk_get_rate(dss.dss_ick);
	case DSS_CLK_FCK:
		return clk_get_rate(dss.dss_fck);
	case DSS_CLK_SYSCK:
		return clk_get_rate(dss.dss_sys_clk);
	case DSS_CLK_TVFCK:
		return clk_get_rate(dss.dss_tv_fck);
	case DSS_CLK_VIDFCK:
		return clk_get_rate(dss.dss_video_fck);
	}

	BUG();
	return 0;
}

static unsigned count_clk_bits(enum dss_clock clks)
{
	unsigned num_clks = 0;

	if (clks & DSS_CLK_ICK)
		++num_clks;
	if (clks & DSS_CLK_FCK)
		++num_clks;
	if (clks & DSS_CLK_SYSCK)
		++num_clks;
	if (clks & DSS_CLK_TVFCK)
		++num_clks;
	if (clks & DSS_CLK_VIDFCK)
		++num_clks;

	return num_clks;
}

static void dss_clk_enable_no_ctx(enum dss_clock clks)
{
	unsigned num_clks = count_clk_bits(clks);

	if (clks & DSS_CLK_ICK)
		clk_enable(dss.dss_ick);
	if (clks & DSS_CLK_FCK)
		clk_enable(dss.dss_fck);
	if ((clks & DSS_CLK_SYSCK) && dss.dss_sys_clk)
		clk_enable(dss.dss_sys_clk);
	if ((clks & DSS_CLK_TVFCK) && dss.dss_tv_fck)
		clk_enable(dss.dss_tv_fck);
	if ((clks & DSS_CLK_VIDFCK) && dss.dss_video_fck)
		clk_enable(dss.dss_video_fck);

	dss.num_clks_enabled += num_clks;
}

void dss_clk_enable(enum dss_clock clks)
{
	bool check_ctx = dss.num_clks_enabled == 0;

	dss_clk_enable_no_ctx(clks);

	/*
	 * HACK: On omap4 the registers may not be accessible right after
	 * enabling the clocks. At some point this will be handled by
	 * pm_runtime, but for the time begin this should make things work.
	 */
	if (cpu_is_omap44xx() && check_ctx)
		udelay(10);

	if (check_ctx && cpu_is_omap34xx() && dss_need_ctx_restore())
		restore_all_ctx();
}

static void dss_clk_disable_no_ctx(enum dss_clock clks)
{
	unsigned num_clks = count_clk_bits(clks);

	if (clks & DSS_CLK_ICK)
		clk_disable(dss.dss_ick);
	if (clks & DSS_CLK_FCK)
		clk_disable(dss.dss_fck);
	if ((clks & DSS_CLK_SYSCK) && dss.dss_sys_clk)
		clk_disable(dss.dss_sys_clk);
	if ((clks & DSS_CLK_TVFCK) && dss.dss_tv_fck)
		clk_disable(dss.dss_tv_fck);
	if ((clks & DSS_CLK_VIDFCK) && dss.dss_video_fck)
		clk_disable(dss.dss_video_fck);

	dss.num_clks_enabled -= num_clks;
}

void dss_clk_disable(enum dss_clock clks)
{
	if (cpu_is_omap34xx()) {
		unsigned num_clks = count_clk_bits(clks);

		BUG_ON(dss.num_clks_enabled < num_clks);

		if (dss.num_clks_enabled == num_clks)
			save_all_ctx();
	}

	dss_clk_disable_no_ctx(clks);
}

static void dss_clk_enable_all_no_ctx(void)
{
	enum dss_clock clks;

	clks = DSS_CLK_ICK | DSS_CLK_FCK | DSS_CLK_SYSCK | DSS_CLK_TVFCK;
	if (cpu_is_omap34xx())
		clks |= DSS_CLK_VIDFCK;
	dss_clk_enable_no_ctx(clks);
}

static void dss_clk_disable_all_no_ctx(void)
{
	enum dss_clock clks;

	clks = DSS_CLK_ICK | DSS_CLK_FCK | DSS_CLK_SYSCK | DSS_CLK_TVFCK;
	if (cpu_is_omap34xx())
		clks |= DSS_CLK_VIDFCK;
	dss_clk_disable_no_ctx(clks);
}

#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_OMAP2_DSS_DEBUG_SUPPORT)
/* CLOCKS */
static void core_dump_clocks(struct seq_file *s)
{
	int i;
	struct clk *clocks[5] = {
		dss.dss_ick,
		dss.dss_fck,
		dss.dss_sys_clk,
		dss.dss_tv_fck,
		dss.dss_video_fck
	};

	seq_printf(s, "- CORE -\n");

	seq_printf(s, "internal clk count\t\t%u\n", dss.num_clks_enabled);

	for (i = 0; i < 5; i++) {
		if (!clocks[i])
			continue;
		seq_printf(s, "%-15s\t%lu\t%d\n",
				clocks[i]->name,
				clk_get_rate(clocks[i]),
				clocks[i]->usecount);
	}
}
#endif /* defined(CONFIG_DEBUG_FS) && defined(CONFIG_OMAP2_DSS_DEBUG_SUPPORT) */

/* DEBUGFS */
#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_OMAP2_DSS_DEBUG_SUPPORT)
void dss_debug_dump_clocks(struct seq_file *s)
{
	core_dump_clocks(s);
	dss_dump_clocks(s);
	dispc_dump_clocks(s);
#ifdef CONFIG_OMAP2_DSS_DSI
	dsi_dump_clocks(s);
#endif
}
#endif


/* DSS HW IP initialisation */
static int omap_dsshw_probe(struct platform_device *pdev)
{
	int r;

	dss.pdev = pdev;

	r = dss_get_clocks();
	if (r)
		goto err_clocks;

	dss_clk_enable_all_no_ctx();

	dss.ctx_id = dss_get_ctx_id();
	DSSDBG("initial ctx id %u\n", dss.ctx_id);

	r = dss_init();
	if (r) {
		DSSERR("Failed to initialize DSS\n");
		goto err_dss;
	}

	r = dpi_init();
	if (r) {
		DSSERR("Failed to initialize DPI\n");
		goto err_dpi;
	}

	r = sdi_init();
	if (r) {
		DSSERR("Failed to initialize SDI\n");
		goto err_sdi;
	}

	dss_clk_disable_all_no_ctx();
	return 0;
err_sdi:
	dpi_exit();
err_dpi:
	dss_exit();
err_dss:
	dss_clk_disable_all_no_ctx();
	dss_put_clocks();
err_clocks:
	return r;
}

static int omap_dsshw_remove(struct platform_device *pdev)
{

	dss_exit();

	/*
	 * As part of hwmod changes, DSS is not the only controller of dss
	 * clocks; hwmod framework itself will also enable clocks during hwmod
	 * init for dss, and autoidle is set in h/w for DSS. Hence, there's no
	 * need to disable clocks if their usecounts > 1.
	 */
	WARN_ON(dss.num_clks_enabled > 0);

	dss_put_clocks();
	return 0;
}

static struct platform_driver omap_dsshw_driver = {
	.probe          = omap_dsshw_probe,
	.remove         = omap_dsshw_remove,
	.driver         = {
		.name   = "omapdss_dss",
		.owner  = THIS_MODULE,
	},
};

int dss_init_platform_driver(void)
{
	return platform_driver_register(&omap_dsshw_driver);
}

void dss_uninit_platform_driver(void)
{
	return platform_driver_unregister(&omap_dsshw_driver);
}
