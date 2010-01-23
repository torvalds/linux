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
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/clk.h>

#include <plat/display.h>
#include "dss.h"

#define DSS_BASE			0x48050000

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
	void __iomem    *base;

	struct clk	*dpll4_m4_ck;

	unsigned long	cache_req_pck;
	unsigned long	cache_prate;
	struct dss_clock_info cache_dss_cinfo;
	struct dispc_clock_info cache_dispc_cinfo;

	u32		ctx[DSS_SZ_REGS / sizeof(u32)];
} dss;

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

#ifdef CONFIG_OMAP2_DSS_SDI
	SR(SDI_CONTROL);
	SR(PLL_CONTROL);
#endif
}

void dss_restore_context(void)
{
	if (_omap_dss_wait_reset())
		DSSERR("DSS not coming out of reset after sleep\n");

	RR(SYSCONFIG);
	RR(CONTROL);

#ifdef CONFIG_OMAP2_DSS_SDI
	RR(SDI_CONTROL);
	RR(PLL_CONTROL);
#endif
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

void dss_dump_clocks(struct seq_file *s)
{
	unsigned long dpll4_ck_rate;
	unsigned long dpll4_m4_ck_rate;

	dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK1);

	dpll4_ck_rate = clk_get_rate(clk_get_parent(dss.dpll4_m4_ck));
	dpll4_m4_ck_rate = clk_get_rate(dss.dpll4_m4_ck);

	seq_printf(s, "- DSS -\n");

	seq_printf(s, "dpll4_ck %lu\n", dpll4_ck_rate);

	seq_printf(s, "dss1_alwon_fclk = %lu / %lu * 2 = %lu\n",
			dpll4_ck_rate,
			dpll4_ck_rate / dpll4_m4_ck_rate,
			dss_clk_get_rate(DSS_CLK_FCK1));

	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);
}

void dss_dump_regs(struct seq_file *s)
{
#define DUMPREG(r) seq_printf(s, "%-35s %08x\n", #r, dss_read_reg(r))

	dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK1);

	DUMPREG(DSS_REVISION);
	DUMPREG(DSS_SYSCONFIG);
	DUMPREG(DSS_SYSSTATUS);
	DUMPREG(DSS_IRQSTATUS);
	DUMPREG(DSS_CONTROL);
	DUMPREG(DSS_SDI_CONTROL);
	DUMPREG(DSS_PLL_CONTROL);
	DUMPREG(DSS_SDI_STATUS);

	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);
#undef DUMPREG
}

void dss_select_clk_source(bool dsi, bool dispc)
{
	u32 r;
	r = dss_read_reg(DSS_CONTROL);
	r = FLD_MOD(r, dsi, 1, 1);	/* DSI_CLK_SWITCH */
	r = FLD_MOD(r, dispc, 0, 0);	/* DISPC_CLK_SWITCH */
	dss_write_reg(DSS_CONTROL, r);
}

int dss_get_dsi_clk_source(void)
{
	return FLD_GET(dss_read_reg(DSS_CONTROL), 1, 1);
}

int dss_get_dispc_clk_source(void)
{
	return FLD_GET(dss_read_reg(DSS_CONTROL), 0, 0);
}

/* calculate clock rates using dividers in cinfo */
int dss_calc_clock_rates(struct dss_clock_info *cinfo)
{
	unsigned long prate;

	if (cinfo->fck_div > 16 || cinfo->fck_div == 0)
		return -EINVAL;

	prate = clk_get_rate(clk_get_parent(dss.dpll4_m4_ck));

	cinfo->fck = prate / cinfo->fck_div;

	return 0;
}

int dss_set_clock_div(struct dss_clock_info *cinfo)
{
	unsigned long prate;
	int r;

	if (cpu_is_omap34xx()) {
		prate = clk_get_rate(clk_get_parent(dss.dpll4_m4_ck));
		DSSDBG("dpll4_m4 = %ld\n", prate);

		r = clk_set_rate(dss.dpll4_m4_ck, prate / cinfo->fck_div);
		if (r)
			return r;
	}

	DSSDBG("fck = %ld (%d)\n", cinfo->fck, cinfo->fck_div);

	return 0;
}

int dss_get_clock_div(struct dss_clock_info *cinfo)
{
	cinfo->fck = dss_clk_get_rate(DSS_CLK_FCK1);

	if (cpu_is_omap34xx()) {
		unsigned long prate;
		prate = clk_get_rate(clk_get_parent(dss.dpll4_m4_ck));
		cinfo->fck_div = prate / (cinfo->fck / 2);
	} else {
		cinfo->fck_div = 0;
	}

	return 0;
}

unsigned long dss_get_dpll4_rate(void)
{
	if (cpu_is_omap34xx())
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

	unsigned long fck;

	u16 fck_div;

	int match = 0;
	int min_fck_per_pck;

	prate = dss_get_dpll4_rate();

	fck = dss_clk_get_rate(DSS_CLK_FCK1);
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
		req_pck * min_fck_per_pck > DISPC_MAX_FCK) {
		DSSERR("Requested pixel clock not possible with the current "
				"OMAP2_DSS_MIN_FCK_PER_PCK setting. Turning "
				"the constraint off.\n");
		min_fck_per_pck = 0;
	}

retry:
	memset(&best_dss, 0, sizeof(best_dss));
	memset(&best_dispc, 0, sizeof(best_dispc));

	if (cpu_is_omap24xx()) {
		struct dispc_clock_info cur_dispc;
		/* XXX can we change the clock on omap2? */
		fck = dss_clk_get_rate(DSS_CLK_FCK1);
		fck_div = 1;

		dispc_find_clk_divs(is_tft, req_pck, fck, &cur_dispc);
		match = 1;

		best_dss.fck = fck;
		best_dss.fck_div = fck_div;

		best_dispc = cur_dispc;

		goto found;
	} else if (cpu_is_omap34xx()) {
		for (fck_div = 16; fck_div > 0; --fck_div) {
			struct dispc_clock_info cur_dispc;

			fck = prate / fck_div * 2;

			if (fck > DISPC_MAX_FCK)
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
	} else {
		BUG();
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



static irqreturn_t dss_irq_handler_omap2(int irq, void *arg)
{
	dispc_irq_handler();

	return IRQ_HANDLED;
}

static irqreturn_t dss_irq_handler_omap3(int irq, void *arg)
{
	u32 irqstatus;

	irqstatus = dss_read_reg(DSS_IRQSTATUS);

	if (irqstatus & (1<<0))	/* DISPC_IRQ */
		dispc_irq_handler();
#ifdef CONFIG_OMAP2_DSS_DSI
	if (irqstatus & (1<<1))	/* DSI_IRQ */
		dsi_irq_handler();
#endif

	return IRQ_HANDLED;
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

int dss_init(bool skip_init)
{
	int r;
	u32 rev;

	dss.base = ioremap(DSS_BASE, DSS_SZ_REGS);
	if (!dss.base) {
		DSSERR("can't ioremap DSS\n");
		r = -ENOMEM;
		goto fail0;
	}

	if (!skip_init) {
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
	}

	/* autoidle */
	REG_FLD_MOD(DSS_SYSCONFIG, 1, 0, 0);

	/* Select DPLL */
	REG_FLD_MOD(DSS_CONTROL, 0, 0, 0);

#ifdef CONFIG_OMAP2_DSS_VENC
	REG_FLD_MOD(DSS_CONTROL, 1, 4, 4);	/* venc dac demen */
	REG_FLD_MOD(DSS_CONTROL, 1, 3, 3);	/* venc clock 4x enable */
	REG_FLD_MOD(DSS_CONTROL, 0, 2, 2);	/* venc clock mode = normal */
#endif

	r = request_irq(INT_24XX_DSS_IRQ,
			cpu_is_omap24xx()
			? dss_irq_handler_omap2
			: dss_irq_handler_omap3,
			0, "OMAP DSS", NULL);

	if (r < 0) {
		DSSERR("omap2 dss: request_irq failed\n");
		goto fail1;
	}

	if (cpu_is_omap34xx()) {
		dss.dpll4_m4_ck = clk_get(NULL, "dpll4_m4_ck");
		if (IS_ERR(dss.dpll4_m4_ck)) {
			DSSERR("Failed to get dpll4_m4_ck\n");
			r = PTR_ERR(dss.dpll4_m4_ck);
			goto fail2;
		}
	}

	dss_save_context();

	rev = dss_read_reg(DSS_REVISION);
	printk(KERN_INFO "OMAP DSS rev %d.%d\n",
			FLD_GET(rev, 7, 4), FLD_GET(rev, 3, 0));

	return 0;

fail2:
	free_irq(INT_24XX_DSS_IRQ, NULL);
fail1:
	iounmap(dss.base);
fail0:
	return r;
}

void dss_exit(void)
{
	if (cpu_is_omap34xx())
		clk_put(dss.dpll4_m4_ck);

	free_irq(INT_24XX_DSS_IRQ, NULL);

	iounmap(dss.base);
}

