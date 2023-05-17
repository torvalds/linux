// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Edward-JW Yang <edward-jw.yang@mediatek.com>
 */

#include <linux/io.h>
#include <linux/iopoll.h>

#include "clk-mtk.h"
#include "clk-pllfh.h"
#include "clk-fhctl.h"

#define PERCENT_TO_DDSLMT(dds, percent_m10) \
	((((dds) * (percent_m10)) >> 5) / 100)

static const struct fhctl_offset fhctl_offset_v1 = {
	.offset_hp_en = 0x0,
	.offset_clk_con = 0x4,
	.offset_rst_con = 0x8,
	.offset_slope0 = 0xc,
	.offset_slope1 = 0x10,
	.offset_cfg = 0x0,
	.offset_updnlmt = 0x4,
	.offset_dds = 0x8,
	.offset_dvfs = 0xc,
	.offset_mon = 0x10,
};

static const struct fhctl_offset fhctl_offset_v2 = {
	.offset_hp_en = 0x0,
	.offset_clk_con = 0x8,
	.offset_rst_con = 0xc,
	.offset_slope0 = 0x10,
	.offset_slope1 = 0x14,
	.offset_cfg = 0x0,
	.offset_updnlmt = 0x4,
	.offset_dds = 0x8,
	.offset_dvfs = 0xc,
	.offset_mon = 0x10,
};

const struct fhctl_offset *fhctl_get_offset_table(enum fhctl_variant v)
{
	switch (v) {
	case FHCTL_PLLFH_V1:
		return &fhctl_offset_v1;
	case FHCTL_PLLFH_V2:
		return &fhctl_offset_v2;
	default:
		return ERR_PTR(-EINVAL);
	};
}

static void dump_hw(struct mtk_clk_pll *pll, struct fh_pll_regs *regs,
		    const struct fh_pll_data *data)
{
	pr_info("hp_en<%x>,clk_con<%x>,slope0<%x>,slope1<%x>\n",
		readl(regs->reg_hp_en), readl(regs->reg_clk_con),
		readl(regs->reg_slope0), readl(regs->reg_slope1));
	pr_info("cfg<%x>,lmt<%x>,dds<%x>,dvfs<%x>,mon<%x>\n",
		readl(regs->reg_cfg), readl(regs->reg_updnlmt),
		readl(regs->reg_dds), readl(regs->reg_dvfs),
		readl(regs->reg_mon));
	pr_info("pcw<%x>\n", readl(pll->pcw_addr));
}

static int fhctl_set_ssc_regs(struct mtk_clk_pll *pll, struct fh_pll_regs *regs,
			      const struct fh_pll_data *data, u32 rate)
{
	u32 updnlmt_val, r;

	writel((readl(regs->reg_cfg) & ~(data->frddsx_en)), regs->reg_cfg);
	writel((readl(regs->reg_cfg) & ~(data->sfstrx_en)), regs->reg_cfg);
	writel((readl(regs->reg_cfg) & ~(data->fhctlx_en)), regs->reg_cfg);

	if (rate > 0) {
		/* Set the relative parameter registers (dt/df/upbnd/downbnd) */
		r = readl(regs->reg_cfg);
		r &= ~(data->msk_frddsx_dys);
		r |= (data->df_val << (ffs(data->msk_frddsx_dys) - 1));
		writel(r, regs->reg_cfg);

		r = readl(regs->reg_cfg);
		r &= ~(data->msk_frddsx_dts);
		r |= (data->dt_val << (ffs(data->msk_frddsx_dts) - 1));
		writel(r, regs->reg_cfg);

		writel((readl(pll->pcw_addr) & data->dds_mask) | data->tgl_org,
			regs->reg_dds);

		/* Calculate UPDNLMT */
		updnlmt_val = PERCENT_TO_DDSLMT((readl(regs->reg_dds) &
						 data->dds_mask), rate) <<
						 data->updnlmt_shft;

		writel(updnlmt_val, regs->reg_updnlmt);
		writel(readl(regs->reg_hp_en) | BIT(data->fh_id),
		       regs->reg_hp_en);
		/* Enable SSC */
		writel(readl(regs->reg_cfg) | data->frddsx_en, regs->reg_cfg);
		/* Enable Hopping control */
		writel(readl(regs->reg_cfg) | data->fhctlx_en, regs->reg_cfg);

	} else {
		/* Switch to APMIXEDSYS control */
		writel(readl(regs->reg_hp_en) & ~BIT(data->fh_id),
		       regs->reg_hp_en);
		/* Wait for DDS to be stable */
		udelay(30);
	}

	return 0;
}

static int hopping_hw_flow(struct mtk_clk_pll *pll, struct fh_pll_regs *regs,
			   const struct fh_pll_data *data,
			   struct fh_pll_state *state, unsigned int new_dds)
{
	u32 dds_mask = data->dds_mask;
	u32 mon_dds = 0;
	u32 con_pcw_tmp;
	int ret;

	if (state->ssc_rate)
		fhctl_set_ssc_regs(pll, regs, data, 0);

	writel((readl(pll->pcw_addr) & dds_mask) | data->tgl_org,
		regs->reg_dds);

	writel(readl(regs->reg_cfg) | data->sfstrx_en, regs->reg_cfg);
	writel(readl(regs->reg_cfg) | data->fhctlx_en, regs->reg_cfg);
	writel(data->slope0_value, regs->reg_slope0);
	writel(data->slope1_value, regs->reg_slope1);

	writel(readl(regs->reg_hp_en) | BIT(data->fh_id), regs->reg_hp_en);
	writel((new_dds) | (data->dvfs_tri), regs->reg_dvfs);

	/* Wait 1000 us until DDS stable */
	ret = readl_poll_timeout_atomic(regs->reg_mon, mon_dds,
				       (mon_dds & dds_mask) == new_dds,
					10, 1000);
	if (ret) {
		pr_warn("%s: FHCTL hopping timeout\n", pll->data->name);
		dump_hw(pll, regs, data);
	}

	con_pcw_tmp = readl(pll->pcw_addr) & (~dds_mask);
	con_pcw_tmp = (con_pcw_tmp | (readl(regs->reg_mon) & dds_mask) |
		       data->pcwchg);

	writel(con_pcw_tmp, pll->pcw_addr);
	writel(readl(regs->reg_hp_en) & ~BIT(data->fh_id), regs->reg_hp_en);

	if (state->ssc_rate)
		fhctl_set_ssc_regs(pll, regs, data, state->ssc_rate);

	return ret;
}

static unsigned int __get_postdiv(struct mtk_clk_pll *pll)
{
	unsigned int regval;

	regval = readl(pll->pd_addr) >> pll->data->pd_shift;
	regval &= POSTDIV_MASK;

	return BIT(regval);
}

static void __set_postdiv(struct mtk_clk_pll *pll, unsigned int postdiv)
{
	unsigned int regval;

	regval = readl(pll->pd_addr);
	regval &= ~(POSTDIV_MASK << pll->data->pd_shift);
	regval |= (ffs(postdiv) - 1) << pll->data->pd_shift;
	writel(regval, pll->pd_addr);
}

static int fhctl_hopping(struct mtk_fh *fh, unsigned int new_dds,
			 unsigned int postdiv)
{
	const struct fh_pll_data *data = &fh->pllfh_data->data;
	struct fh_pll_state *state = &fh->pllfh_data->state;
	struct fh_pll_regs *regs = &fh->regs;
	struct mtk_clk_pll *pll = &fh->clk_pll;
	spinlock_t *lock = fh->lock;
	unsigned int pll_postdiv;
	unsigned long flags = 0;
	int ret;

	if (postdiv) {
		pll_postdiv = __get_postdiv(pll);

		if (postdiv > pll_postdiv)
			__set_postdiv(pll, postdiv);
	}

	spin_lock_irqsave(lock, flags);

	ret = hopping_hw_flow(pll, regs, data, state, new_dds);

	spin_unlock_irqrestore(lock, flags);

	if (postdiv && postdiv < pll_postdiv)
		__set_postdiv(pll, postdiv);

	return ret;
}

static int fhctl_ssc_enable(struct mtk_fh *fh, u32 rate)
{
	const struct fh_pll_data *data = &fh->pllfh_data->data;
	struct fh_pll_state *state = &fh->pllfh_data->state;
	struct fh_pll_regs *regs = &fh->regs;
	struct mtk_clk_pll *pll = &fh->clk_pll;
	spinlock_t *lock = fh->lock;
	unsigned long flags = 0;

	spin_lock_irqsave(lock, flags);

	fhctl_set_ssc_regs(pll, regs, data, rate);
	state->ssc_rate = rate;

	spin_unlock_irqrestore(lock, flags);

	return 0;
}

static const struct fh_operation fhctl_ops = {
	.hopping = fhctl_hopping,
	.ssc_enable = fhctl_ssc_enable,
};

const struct fh_operation *fhctl_get_ops(void)
{
	return &fhctl_ops;
}

void fhctl_hw_init(struct mtk_fh *fh)
{
	const struct fh_pll_data data = fh->pllfh_data->data;
	struct fh_pll_state state = fh->pllfh_data->state;
	struct fh_pll_regs regs = fh->regs;
	u32 val;

	/* initial hw register */
	val = readl(regs.reg_clk_con) | BIT(data.fh_id);
	writel(val, regs.reg_clk_con);

	val = readl(regs.reg_rst_con) & ~BIT(data.fh_id);
	writel(val, regs.reg_rst_con);
	val = readl(regs.reg_rst_con) | BIT(data.fh_id);
	writel(val, regs.reg_rst_con);

	writel(0x0, regs.reg_cfg);
	writel(0x0, regs.reg_updnlmt);
	writel(0x0, regs.reg_dds);

	/* enable ssc if needed */
	if (state.ssc_rate)
		fh->ops->ssc_enable(fh, state.ssc_rate);
}
