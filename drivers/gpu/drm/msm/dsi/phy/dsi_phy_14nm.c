// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>

#include "dsi_phy.h"
#include "dsi.xml.h"
#include "dsi_phy_14nm.xml.h"

#define PHY_14NM_CKLN_IDX	4

/*
 * DSI PLL 14nm - clock diagram (eg: DSI0):
 *
 *         dsi0n1_postdiv_clk
 *                         |
 *                         |
 *                 +----+  |  +----+
 *  dsi0vco_clk ---| n1 |--o--| /8 |-- dsi0pllbyte
 *                 +----+  |  +----+
 *                         |           dsi0n1_postdivby2_clk
 *                         |   +----+  |
 *                         o---| /2 |--o--|\
 *                         |   +----+     | \   +----+
 *                         |              |  |--| n2 |-- dsi0pll
 *                         o--------------| /   +----+
 *                                        |/
 */

#define POLL_MAX_READS			15
#define POLL_TIMEOUT_US			1000

#define VCO_REF_CLK_RATE		19200000
#define VCO_MIN_RATE			1300000000UL
#define VCO_MAX_RATE			2600000000UL

struct dsi_pll_config {
	u64 vco_current_rate;

	u32 ssc_en;	/* SSC enable/disable */

	/* fixed params */
	u32 plllock_cnt;
	u32 ssc_center;
	u32 ssc_adj_period;
	u32 ssc_spread;
	u32 ssc_freq;

	/* calculated */
	u32 dec_start;
	u32 div_frac_start;
	u32 ssc_period;
	u32 ssc_step_size;
	u32 plllock_cmp;
	u32 pll_vco_div_ref;
	u32 pll_vco_count;
	u32 pll_kvco_div_ref;
	u32 pll_kvco_count;
};

struct pll_14nm_cached_state {
	unsigned long vco_rate;
	u8 n2postdiv;
	u8 n1postdiv;
};

struct dsi_pll_14nm {
	struct clk_hw clk_hw;

	struct msm_dsi_phy *phy;

	/* protects REG_DSI_14nm_PHY_CMN_CLK_CFG0 register */
	spinlock_t postdiv_lock;

	struct pll_14nm_cached_state cached_state;

	struct dsi_pll_14nm *slave;
};

#define to_pll_14nm(x)	container_of(x, struct dsi_pll_14nm, clk_hw)

/*
 * Private struct for N1/N2 post-divider clocks. These clocks are similar to
 * the generic clk_divider class of clocks. The only difference is that it
 * also sets the slave DSI PLL's post-dividers if in bonded DSI mode
 */
struct dsi_pll_14nm_postdiv {
	struct clk_hw hw;

	/* divider params */
	u8 shift;
	u8 width;
	u8 flags; /* same flags as used by clk_divider struct */

	struct dsi_pll_14nm *pll;
};

#define to_pll_14nm_postdiv(_hw) container_of(_hw, struct dsi_pll_14nm_postdiv, hw)

/*
 * Global list of private DSI PLL struct pointers. We need this for bonded DSI
 * mode, where the master PLL's clk_ops needs access the slave's private data
 */
static struct dsi_pll_14nm *pll_14nm_list[DSI_MAX];

static bool pll_14nm_poll_for_ready(struct dsi_pll_14nm *pll_14nm,
				    u32 nb_tries, u32 timeout_us)
{
	bool pll_locked = false, pll_ready = false;
	void __iomem *base = pll_14nm->phy->pll_base;
	u32 tries, val;

	tries = nb_tries;
	while (tries--) {
		val = dsi_phy_read(base + REG_DSI_14nm_PHY_PLL_RESET_SM_READY_STATUS);
		pll_locked = !!(val & BIT(5));

		if (pll_locked)
			break;

		udelay(timeout_us);
	}

	if (!pll_locked)
		goto out;

	tries = nb_tries;
	while (tries--) {
		val = dsi_phy_read(base + REG_DSI_14nm_PHY_PLL_RESET_SM_READY_STATUS);
		pll_ready = !!(val & BIT(0));

		if (pll_ready)
			break;

		udelay(timeout_us);
	}

out:
	DBG("DSI PLL is %slocked, %sready", pll_locked ? "" : "*not* ", pll_ready ? "" : "*not* ");

	return pll_locked && pll_ready;
}

static void dsi_pll_14nm_config_init(struct dsi_pll_config *pconf)
{
	/* fixed input */
	pconf->plllock_cnt = 1;

	/*
	 * SSC is enabled by default. We might need DT props for configuring
	 * some SSC params like PPM and center/down spread etc.
	 */
	pconf->ssc_en = 1;
	pconf->ssc_center = 0;		/* down spread by default */
	pconf->ssc_spread = 5;		/* PPM / 1000 */
	pconf->ssc_freq = 31500;	/* default recommended */
	pconf->ssc_adj_period = 37;
}

#define CEIL(x, y)		(((x) + ((y) - 1)) / (y))

static void pll_14nm_ssc_calc(struct dsi_pll_14nm *pll, struct dsi_pll_config *pconf)
{
	u32 period, ssc_period;
	u32 ref, rem;
	u64 step_size;

	DBG("vco=%lld ref=%d", pconf->vco_current_rate, VCO_REF_CLK_RATE);

	ssc_period = pconf->ssc_freq / 500;
	period = (u32)VCO_REF_CLK_RATE / 1000;
	ssc_period  = CEIL(period, ssc_period);
	ssc_period -= 1;
	pconf->ssc_period = ssc_period;

	DBG("ssc freq=%d spread=%d period=%d", pconf->ssc_freq,
	    pconf->ssc_spread, pconf->ssc_period);

	step_size = (u32)pconf->vco_current_rate;
	ref = VCO_REF_CLK_RATE;
	ref /= 1000;
	step_size = div_u64(step_size, ref);
	step_size <<= 20;
	step_size = div_u64(step_size, 1000);
	step_size *= pconf->ssc_spread;
	step_size = div_u64(step_size, 1000);
	step_size *= (pconf->ssc_adj_period + 1);

	rem = 0;
	step_size = div_u64_rem(step_size, ssc_period + 1, &rem);
	if (rem)
		step_size++;

	DBG("step_size=%lld", step_size);

	step_size &= 0x0ffff;	/* take lower 16 bits */

	pconf->ssc_step_size = step_size;
}

static void pll_14nm_dec_frac_calc(struct dsi_pll_14nm *pll, struct dsi_pll_config *pconf)
{
	u64 multiplier = BIT(20);
	u64 dec_start_multiple, dec_start, pll_comp_val;
	u32 duration, div_frac_start;
	u64 vco_clk_rate = pconf->vco_current_rate;
	u64 fref = VCO_REF_CLK_RATE;

	DBG("vco_clk_rate=%lld ref_clk_rate=%lld", vco_clk_rate, fref);

	dec_start_multiple = div_u64(vco_clk_rate * multiplier, fref);
	div_u64_rem(dec_start_multiple, multiplier, &div_frac_start);

	dec_start = div_u64(dec_start_multiple, multiplier);

	pconf->dec_start = (u32)dec_start;
	pconf->div_frac_start = div_frac_start;

	if (pconf->plllock_cnt == 0)
		duration = 1024;
	else if (pconf->plllock_cnt == 1)
		duration = 256;
	else if (pconf->plllock_cnt == 2)
		duration = 128;
	else
		duration = 32;

	pll_comp_val = duration * dec_start_multiple;
	pll_comp_val = div_u64(pll_comp_val, multiplier);
	do_div(pll_comp_val, 10);

	pconf->plllock_cmp = (u32)pll_comp_val;
}

static u32 pll_14nm_kvco_slop(u32 vrate)
{
	u32 slop = 0;

	if (vrate > VCO_MIN_RATE && vrate <= 1800000000UL)
		slop =  600;
	else if (vrate > 1800000000UL && vrate < 2300000000UL)
		slop = 400;
	else if (vrate > 2300000000UL && vrate < VCO_MAX_RATE)
		slop = 280;

	return slop;
}

static void pll_14nm_calc_vco_count(struct dsi_pll_14nm *pll, struct dsi_pll_config *pconf)
{
	u64 vco_clk_rate = pconf->vco_current_rate;
	u64 fref = VCO_REF_CLK_RATE;
	u32 vco_measure_time = 5;
	u32 kvco_measure_time = 5;
	u64 data;
	u32 cnt;

	data = fref * vco_measure_time;
	do_div(data, 1000000);
	data &= 0x03ff;	/* 10 bits */
	data -= 2;
	pconf->pll_vco_div_ref = data;

	data = div_u64(vco_clk_rate, 1000000);	/* unit is Mhz */
	data *= vco_measure_time;
	do_div(data, 10);
	pconf->pll_vco_count = data;

	data = fref * kvco_measure_time;
	do_div(data, 1000000);
	data &= 0x03ff;	/* 10 bits */
	data -= 1;
	pconf->pll_kvco_div_ref = data;

	cnt = pll_14nm_kvco_slop(vco_clk_rate);
	cnt *= 2;
	cnt /= 100;
	cnt *= kvco_measure_time;
	pconf->pll_kvco_count = cnt;
}

static void pll_db_commit_ssc(struct dsi_pll_14nm *pll, struct dsi_pll_config *pconf)
{
	void __iomem *base = pll->phy->pll_base;
	u8 data;

	data = pconf->ssc_adj_period;
	data &= 0x0ff;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_SSC_ADJ_PER1, data);
	data = (pconf->ssc_adj_period >> 8);
	data &= 0x03;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_SSC_ADJ_PER2, data);

	data = pconf->ssc_period;
	data &= 0x0ff;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_SSC_PER1, data);
	data = (pconf->ssc_period >> 8);
	data &= 0x0ff;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_SSC_PER2, data);

	data = pconf->ssc_step_size;
	data &= 0x0ff;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_SSC_STEP_SIZE1, data);
	data = (pconf->ssc_step_size >> 8);
	data &= 0x0ff;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_SSC_STEP_SIZE2, data);

	data = (pconf->ssc_center & 0x01);
	data <<= 1;
	data |= 0x01; /* enable */
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_SSC_EN_CENTER, data);

	wmb();	/* make sure register committed */
}

static void pll_db_commit_common(struct dsi_pll_14nm *pll,
				 struct dsi_pll_config *pconf)
{
	void __iomem *base = pll->phy->pll_base;
	u8 data;

	/* confgiure the non frequency dependent pll registers */
	data = 0;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_SYSCLK_EN_RESET, data);

	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_TXCLK_EN, 1);

	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_RESETSM_CNTRL, 48);
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_RESETSM_CNTRL2, 4 << 3); /* bandgap_timer */
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_RESETSM_CNTRL5, 5); /* pll_wakeup_timer */

	data = pconf->pll_vco_div_ref & 0xff;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_VCO_DIV_REF1, data);
	data = (pconf->pll_vco_div_ref >> 8) & 0x3;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_VCO_DIV_REF2, data);

	data = pconf->pll_kvco_div_ref & 0xff;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_KVCO_DIV_REF1, data);
	data = (pconf->pll_kvco_div_ref >> 8) & 0x3;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_KVCO_DIV_REF2, data);

	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_PLL_MISC1, 16);

	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_IE_TRIM, 4);

	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_IP_TRIM, 4);

	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_CP_SET_CUR, 1 << 3 | 1);

	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_PLL_ICPCSET, 0 << 3 | 0);

	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_PLL_ICPMSET, 0 << 3 | 0);

	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_PLL_ICP_SET, 4 << 3 | 4);

	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_PLL_LPF1, 1 << 4 | 11);

	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_IPTAT_TRIM, 7);

	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_PLL_CRCTRL, 1 << 4 | 2);
}

static void pll_14nm_software_reset(struct dsi_pll_14nm *pll_14nm)
{
	void __iomem *cmn_base = pll_14nm->phy->base;

	/* de assert pll start and apply pll sw reset */

	/* stop pll */
	dsi_phy_write(cmn_base + REG_DSI_14nm_PHY_CMN_PLL_CNTRL, 0);

	/* pll sw reset */
	dsi_phy_write_udelay(cmn_base + REG_DSI_14nm_PHY_CMN_CTRL_1, 0x20, 10);
	wmb();	/* make sure register committed */

	dsi_phy_write(cmn_base + REG_DSI_14nm_PHY_CMN_CTRL_1, 0);
	wmb();	/* make sure register committed */
}

static void pll_db_commit_14nm(struct dsi_pll_14nm *pll,
			       struct dsi_pll_config *pconf)
{
	void __iomem *base = pll->phy->pll_base;
	void __iomem *cmn_base = pll->phy->base;
	u8 data;

	DBG("DSI%d PLL", pll->phy->id);

	dsi_phy_write(cmn_base + REG_DSI_14nm_PHY_CMN_LDO_CNTRL, 0x3c);

	pll_db_commit_common(pll, pconf);

	pll_14nm_software_reset(pll);

	/* Use the /2 path in Mux */
	dsi_phy_write(cmn_base + REG_DSI_14nm_PHY_CMN_CLK_CFG1, 1);

	data = 0xff; /* data, clk, pll normal operation */
	dsi_phy_write(cmn_base + REG_DSI_14nm_PHY_CMN_CTRL_0, data);

	/* configure the frequency dependent pll registers */
	data = pconf->dec_start;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_DEC_START, data);

	data = pconf->div_frac_start & 0xff;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_DIV_FRAC_START1, data);
	data = (pconf->div_frac_start >> 8) & 0xff;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_DIV_FRAC_START2, data);
	data = (pconf->div_frac_start >> 16) & 0xf;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_DIV_FRAC_START3, data);

	data = pconf->plllock_cmp & 0xff;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_PLLLOCK_CMP1, data);

	data = (pconf->plllock_cmp >> 8) & 0xff;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_PLLLOCK_CMP2, data);

	data = (pconf->plllock_cmp >> 16) & 0x3;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_PLLLOCK_CMP3, data);

	data = pconf->plllock_cnt << 1 | 0 << 3; /* plllock_rng */
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_PLLLOCK_CMP_EN, data);

	data = pconf->pll_vco_count & 0xff;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_VCO_COUNT1, data);
	data = (pconf->pll_vco_count >> 8) & 0xff;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_VCO_COUNT2, data);

	data = pconf->pll_kvco_count & 0xff;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_KVCO_COUNT1, data);
	data = (pconf->pll_kvco_count >> 8) & 0x3;
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_KVCO_COUNT2, data);

	/*
	 * High nibble configures the post divider internal to the VCO. It's
	 * fixed to divide by 1 for now.
	 *
	 * 0: divided by 1
	 * 1: divided by 2
	 * 2: divided by 4
	 * 3: divided by 8
	 */
	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_PLL_LPF2_POSTDIV, 0 << 4 | 3);

	if (pconf->ssc_en)
		pll_db_commit_ssc(pll, pconf);

	wmb();	/* make sure register committed */
}

/*
 * VCO clock Callbacks
 */
static int dsi_pll_14nm_vco_set_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	struct dsi_pll_14nm *pll_14nm = to_pll_14nm(hw);
	struct dsi_pll_config conf;

	DBG("DSI PLL%d rate=%lu, parent's=%lu", pll_14nm->phy->id, rate,
	    parent_rate);

	dsi_pll_14nm_config_init(&conf);
	conf.vco_current_rate = rate;

	pll_14nm_dec_frac_calc(pll_14nm, &conf);

	if (conf.ssc_en)
		pll_14nm_ssc_calc(pll_14nm, &conf);

	pll_14nm_calc_vco_count(pll_14nm, &conf);

	/* commit the slave DSI PLL registers if we're master. Note that we
	 * don't lock the slave PLL. We just ensure that the PLL/PHY registers
	 * of the master and slave are identical
	 */
	if (pll_14nm->phy->usecase == MSM_DSI_PHY_MASTER) {
		struct dsi_pll_14nm *pll_14nm_slave = pll_14nm->slave;

		pll_db_commit_14nm(pll_14nm_slave, &conf);
	}

	pll_db_commit_14nm(pll_14nm, &conf);

	return 0;
}

static unsigned long dsi_pll_14nm_vco_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct dsi_pll_14nm *pll_14nm = to_pll_14nm(hw);
	void __iomem *base = pll_14nm->phy->pll_base;
	u64 vco_rate, multiplier = BIT(20);
	u32 div_frac_start;
	u32 dec_start;
	u64 ref_clk = parent_rate;

	dec_start = dsi_phy_read(base + REG_DSI_14nm_PHY_PLL_DEC_START);
	dec_start &= 0x0ff;

	DBG("dec_start = %x", dec_start);

	div_frac_start = (dsi_phy_read(base + REG_DSI_14nm_PHY_PLL_DIV_FRAC_START3)
				& 0xf) << 16;
	div_frac_start |= (dsi_phy_read(base + REG_DSI_14nm_PHY_PLL_DIV_FRAC_START2)
				& 0xff) << 8;
	div_frac_start |= dsi_phy_read(base + REG_DSI_14nm_PHY_PLL_DIV_FRAC_START1)
				& 0xff;

	DBG("div_frac_start = %x", div_frac_start);

	vco_rate = ref_clk * dec_start;

	vco_rate += ((ref_clk * div_frac_start) / multiplier);

	/*
	 * Recalculating the rate from dec_start and frac_start doesn't end up
	 * the rate we originally set. Convert the freq to KHz, round it up and
	 * convert it back to MHz.
	 */
	vco_rate = DIV_ROUND_UP_ULL(vco_rate, 1000) * 1000;

	DBG("returning vco rate = %lu", (unsigned long)vco_rate);

	return (unsigned long)vco_rate;
}

static int dsi_pll_14nm_vco_prepare(struct clk_hw *hw)
{
	struct dsi_pll_14nm *pll_14nm = to_pll_14nm(hw);
	void __iomem *base = pll_14nm->phy->pll_base;
	void __iomem *cmn_base = pll_14nm->phy->base;
	bool locked;

	DBG("");

	if (unlikely(pll_14nm->phy->pll_on))
		return 0;

	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_VREF_CFG1, 0x10);
	dsi_phy_write(cmn_base + REG_DSI_14nm_PHY_CMN_PLL_CNTRL, 1);

	locked = pll_14nm_poll_for_ready(pll_14nm, POLL_MAX_READS,
					 POLL_TIMEOUT_US);

	if (unlikely(!locked)) {
		DRM_DEV_ERROR(&pll_14nm->phy->pdev->dev, "DSI PLL lock failed\n");
		return -EINVAL;
	}

	DBG("DSI PLL lock success");
	pll_14nm->phy->pll_on = true;

	return 0;
}

static void dsi_pll_14nm_vco_unprepare(struct clk_hw *hw)
{
	struct dsi_pll_14nm *pll_14nm = to_pll_14nm(hw);
	void __iomem *cmn_base = pll_14nm->phy->base;

	DBG("");

	if (unlikely(!pll_14nm->phy->pll_on))
		return;

	dsi_phy_write(cmn_base + REG_DSI_14nm_PHY_CMN_PLL_CNTRL, 0);

	pll_14nm->phy->pll_on = false;
}

static long dsi_pll_14nm_clk_round_rate(struct clk_hw *hw,
		unsigned long rate, unsigned long *parent_rate)
{
	struct dsi_pll_14nm *pll_14nm = to_pll_14nm(hw);

	if      (rate < pll_14nm->phy->cfg->min_pll_rate)
		return  pll_14nm->phy->cfg->min_pll_rate;
	else if (rate > pll_14nm->phy->cfg->max_pll_rate)
		return  pll_14nm->phy->cfg->max_pll_rate;
	else
		return rate;
}

static const struct clk_ops clk_ops_dsi_pll_14nm_vco = {
	.round_rate = dsi_pll_14nm_clk_round_rate,
	.set_rate = dsi_pll_14nm_vco_set_rate,
	.recalc_rate = dsi_pll_14nm_vco_recalc_rate,
	.prepare = dsi_pll_14nm_vco_prepare,
	.unprepare = dsi_pll_14nm_vco_unprepare,
};

/*
 * N1 and N2 post-divider clock callbacks
 */
#define div_mask(width)	((1 << (width)) - 1)
static unsigned long dsi_pll_14nm_postdiv_recalc_rate(struct clk_hw *hw,
						      unsigned long parent_rate)
{
	struct dsi_pll_14nm_postdiv *postdiv = to_pll_14nm_postdiv(hw);
	struct dsi_pll_14nm *pll_14nm = postdiv->pll;
	void __iomem *base = pll_14nm->phy->base;
	u8 shift = postdiv->shift;
	u8 width = postdiv->width;
	u32 val;

	DBG("DSI%d PLL parent rate=%lu", pll_14nm->phy->id, parent_rate);

	val = dsi_phy_read(base + REG_DSI_14nm_PHY_CMN_CLK_CFG0) >> shift;
	val &= div_mask(width);

	return divider_recalc_rate(hw, parent_rate, val, NULL,
				   postdiv->flags, width);
}

static long dsi_pll_14nm_postdiv_round_rate(struct clk_hw *hw,
					    unsigned long rate,
					    unsigned long *prate)
{
	struct dsi_pll_14nm_postdiv *postdiv = to_pll_14nm_postdiv(hw);
	struct dsi_pll_14nm *pll_14nm = postdiv->pll;

	DBG("DSI%d PLL parent rate=%lu", pll_14nm->phy->id, rate);

	return divider_round_rate(hw, rate, prate, NULL,
				  postdiv->width,
				  postdiv->flags);
}

static int dsi_pll_14nm_postdiv_set_rate(struct clk_hw *hw, unsigned long rate,
					 unsigned long parent_rate)
{
	struct dsi_pll_14nm_postdiv *postdiv = to_pll_14nm_postdiv(hw);
	struct dsi_pll_14nm *pll_14nm = postdiv->pll;
	void __iomem *base = pll_14nm->phy->base;
	spinlock_t *lock = &pll_14nm->postdiv_lock;
	u8 shift = postdiv->shift;
	u8 width = postdiv->width;
	unsigned int value;
	unsigned long flags = 0;
	u32 val;

	DBG("DSI%d PLL parent rate=%lu parent rate %lu", pll_14nm->phy->id, rate,
	    parent_rate);

	value = divider_get_val(rate, parent_rate, NULL, postdiv->width,
				postdiv->flags);

	spin_lock_irqsave(lock, flags);

	val = dsi_phy_read(base + REG_DSI_14nm_PHY_CMN_CLK_CFG0);
	val &= ~(div_mask(width) << shift);

	val |= value << shift;
	dsi_phy_write(base + REG_DSI_14nm_PHY_CMN_CLK_CFG0, val);

	/* If we're master in bonded DSI mode, then the slave PLL's post-dividers
	 * follow the master's post dividers
	 */
	if (pll_14nm->phy->usecase == MSM_DSI_PHY_MASTER) {
		struct dsi_pll_14nm *pll_14nm_slave = pll_14nm->slave;
		void __iomem *slave_base = pll_14nm_slave->phy->base;

		dsi_phy_write(slave_base + REG_DSI_14nm_PHY_CMN_CLK_CFG0, val);
	}

	spin_unlock_irqrestore(lock, flags);

	return 0;
}

static const struct clk_ops clk_ops_dsi_pll_14nm_postdiv = {
	.recalc_rate = dsi_pll_14nm_postdiv_recalc_rate,
	.round_rate = dsi_pll_14nm_postdiv_round_rate,
	.set_rate = dsi_pll_14nm_postdiv_set_rate,
};

/*
 * PLL Callbacks
 */

static void dsi_14nm_pll_save_state(struct msm_dsi_phy *phy)
{
	struct dsi_pll_14nm *pll_14nm = to_pll_14nm(phy->vco_hw);
	struct pll_14nm_cached_state *cached_state = &pll_14nm->cached_state;
	void __iomem *cmn_base = pll_14nm->phy->base;
	u32 data;

	data = dsi_phy_read(cmn_base + REG_DSI_14nm_PHY_CMN_CLK_CFG0);

	cached_state->n1postdiv = data & 0xf;
	cached_state->n2postdiv = (data >> 4) & 0xf;

	DBG("DSI%d PLL save state %x %x", pll_14nm->phy->id,
	    cached_state->n1postdiv, cached_state->n2postdiv);

	cached_state->vco_rate = clk_hw_get_rate(phy->vco_hw);
}

static int dsi_14nm_pll_restore_state(struct msm_dsi_phy *phy)
{
	struct dsi_pll_14nm *pll_14nm = to_pll_14nm(phy->vco_hw);
	struct pll_14nm_cached_state *cached_state = &pll_14nm->cached_state;
	void __iomem *cmn_base = pll_14nm->phy->base;
	u32 data;
	int ret;

	ret = dsi_pll_14nm_vco_set_rate(phy->vco_hw,
					cached_state->vco_rate, 0);
	if (ret) {
		DRM_DEV_ERROR(&pll_14nm->phy->pdev->dev,
			"restore vco rate failed. ret=%d\n", ret);
		return ret;
	}

	data = cached_state->n1postdiv | (cached_state->n2postdiv << 4);

	DBG("DSI%d PLL restore state %x %x", pll_14nm->phy->id,
	    cached_state->n1postdiv, cached_state->n2postdiv);

	dsi_phy_write(cmn_base + REG_DSI_14nm_PHY_CMN_CLK_CFG0, data);

	/* also restore post-dividers for slave DSI PLL */
	if (phy->usecase == MSM_DSI_PHY_MASTER) {
		struct dsi_pll_14nm *pll_14nm_slave = pll_14nm->slave;
		void __iomem *slave_base = pll_14nm_slave->phy->base;

		dsi_phy_write(slave_base + REG_DSI_14nm_PHY_CMN_CLK_CFG0, data);
	}

	return 0;
}

static int dsi_14nm_set_usecase(struct msm_dsi_phy *phy)
{
	struct dsi_pll_14nm *pll_14nm = to_pll_14nm(phy->vco_hw);
	void __iomem *base = phy->pll_base;
	u32 clkbuflr_en, bandgap = 0;

	switch (phy->usecase) {
	case MSM_DSI_PHY_STANDALONE:
		clkbuflr_en = 0x1;
		break;
	case MSM_DSI_PHY_MASTER:
		clkbuflr_en = 0x3;
		pll_14nm->slave = pll_14nm_list[(pll_14nm->phy->id + 1) % DSI_MAX];
		break;
	case MSM_DSI_PHY_SLAVE:
		clkbuflr_en = 0x0;
		bandgap = 0x3;
		break;
	default:
		return -EINVAL;
	}

	dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_CLKBUFLR_EN, clkbuflr_en);
	if (bandgap)
		dsi_phy_write(base + REG_DSI_14nm_PHY_PLL_PLL_BANDGAP, bandgap);

	return 0;
}

static struct clk_hw *pll_14nm_postdiv_register(struct dsi_pll_14nm *pll_14nm,
						const char *name,
						const char *parent_name,
						unsigned long flags,
						u8 shift)
{
	struct dsi_pll_14nm_postdiv *pll_postdiv;
	struct device *dev = &pll_14nm->phy->pdev->dev;
	struct clk_init_data postdiv_init = {
		.parent_names = (const char *[]) { parent_name },
		.num_parents = 1,
		.name = name,
		.flags = flags,
		.ops = &clk_ops_dsi_pll_14nm_postdiv,
	};
	int ret;

	pll_postdiv = devm_kzalloc(dev, sizeof(*pll_postdiv), GFP_KERNEL);
	if (!pll_postdiv)
		return ERR_PTR(-ENOMEM);

	pll_postdiv->pll = pll_14nm;
	pll_postdiv->shift = shift;
	/* both N1 and N2 postdividers are 4 bits wide */
	pll_postdiv->width = 4;
	/* range of each divider is from 1 to 15 */
	pll_postdiv->flags = CLK_DIVIDER_ONE_BASED;
	pll_postdiv->hw.init = &postdiv_init;

	ret = devm_clk_hw_register(dev, &pll_postdiv->hw);
	if (ret)
		return ERR_PTR(ret);

	return &pll_postdiv->hw;
}

static int pll_14nm_register(struct dsi_pll_14nm *pll_14nm, struct clk_hw **provided_clocks)
{
	char clk_name[32], parent[32], vco_name[32];
	struct clk_init_data vco_init = {
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.name = vco_name,
		.flags = CLK_IGNORE_UNUSED,
		.ops = &clk_ops_dsi_pll_14nm_vco,
	};
	struct device *dev = &pll_14nm->phy->pdev->dev;
	struct clk_hw *hw;
	int ret;

	DBG("DSI%d", pll_14nm->phy->id);

	snprintf(vco_name, 32, "dsi%dvco_clk", pll_14nm->phy->id);
	pll_14nm->clk_hw.init = &vco_init;

	ret = devm_clk_hw_register(dev, &pll_14nm->clk_hw);
	if (ret)
		return ret;

	snprintf(clk_name, 32, "dsi%dn1_postdiv_clk", pll_14nm->phy->id);
	snprintf(parent, 32, "dsi%dvco_clk", pll_14nm->phy->id);

	/* N1 postdiv, bits 0-3 in REG_DSI_14nm_PHY_CMN_CLK_CFG0 */
	hw = pll_14nm_postdiv_register(pll_14nm, clk_name, parent,
				       CLK_SET_RATE_PARENT, 0);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	snprintf(clk_name, 32, "dsi%dpllbyte", pll_14nm->phy->id);
	snprintf(parent, 32, "dsi%dn1_postdiv_clk", pll_14nm->phy->id);

	/* DSI Byte clock = VCO_CLK / N1 / 8 */
	hw = devm_clk_hw_register_fixed_factor(dev, clk_name, parent,
					  CLK_SET_RATE_PARENT, 1, 8);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	provided_clocks[DSI_BYTE_PLL_CLK] = hw;

	snprintf(clk_name, 32, "dsi%dn1_postdivby2_clk", pll_14nm->phy->id);
	snprintf(parent, 32, "dsi%dn1_postdiv_clk", pll_14nm->phy->id);

	/*
	 * Skip the mux for now, force DSICLK_SEL to 1, Add a /2 divider
	 * on the way. Don't let it set parent.
	 */
	hw = devm_clk_hw_register_fixed_factor(dev, clk_name, parent, 0, 1, 2);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	snprintf(clk_name, 32, "dsi%dpll", pll_14nm->phy->id);
	snprintf(parent, 32, "dsi%dn1_postdivby2_clk", pll_14nm->phy->id);

	/* DSI pixel clock = VCO_CLK / N1 / 2 / N2
	 * This is the output of N2 post-divider, bits 4-7 in
	 * REG_DSI_14nm_PHY_CMN_CLK_CFG0. Don't let it set parent.
	 */
	hw = pll_14nm_postdiv_register(pll_14nm, clk_name, parent, 0, 4);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	provided_clocks[DSI_PIXEL_PLL_CLK]	= hw;

	return 0;
}

static int dsi_pll_14nm_init(struct msm_dsi_phy *phy)
{
	struct platform_device *pdev = phy->pdev;
	struct dsi_pll_14nm *pll_14nm;
	int ret;

	if (!pdev)
		return -ENODEV;

	pll_14nm = devm_kzalloc(&pdev->dev, sizeof(*pll_14nm), GFP_KERNEL);
	if (!pll_14nm)
		return -ENOMEM;

	DBG("PLL%d", phy->id);

	pll_14nm_list[phy->id] = pll_14nm;

	spin_lock_init(&pll_14nm->postdiv_lock);

	pll_14nm->phy = phy;

	ret = pll_14nm_register(pll_14nm, phy->provided_clocks->hws);
	if (ret) {
		DRM_DEV_ERROR(&pdev->dev, "failed to register PLL: %d\n", ret);
		return ret;
	}

	phy->vco_hw = &pll_14nm->clk_hw;

	return 0;
}

static void dsi_14nm_dphy_set_timing(struct msm_dsi_phy *phy,
				     struct msm_dsi_dphy_timing *timing,
				     int lane_idx)
{
	void __iomem *base = phy->lane_base;
	bool clk_ln = (lane_idx == PHY_14NM_CKLN_IDX);
	u32 zero = clk_ln ? timing->clk_zero : timing->hs_zero;
	u32 prepare = clk_ln ? timing->clk_prepare : timing->hs_prepare;
	u32 trail = clk_ln ? timing->clk_trail : timing->hs_trail;
	u32 rqst = clk_ln ? timing->hs_rqst_ckln : timing->hs_rqst;
	u32 prep_dly = clk_ln ? timing->hs_prep_dly_ckln : timing->hs_prep_dly;
	u32 halfbyte_en = clk_ln ? timing->hs_halfbyte_en_ckln :
				   timing->hs_halfbyte_en;

	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_TIMING_CTRL_4(lane_idx),
		      DSI_14nm_PHY_LN_TIMING_CTRL_4_HS_EXIT(timing->hs_exit));
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_TIMING_CTRL_5(lane_idx),
		      DSI_14nm_PHY_LN_TIMING_CTRL_5_HS_ZERO(zero));
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_TIMING_CTRL_6(lane_idx),
		      DSI_14nm_PHY_LN_TIMING_CTRL_6_HS_PREPARE(prepare));
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_TIMING_CTRL_7(lane_idx),
		      DSI_14nm_PHY_LN_TIMING_CTRL_7_HS_TRAIL(trail));
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_TIMING_CTRL_8(lane_idx),
		      DSI_14nm_PHY_LN_TIMING_CTRL_8_HS_RQST(rqst));
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_CFG0(lane_idx),
		      DSI_14nm_PHY_LN_CFG0_PREPARE_DLY(prep_dly));
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_CFG1(lane_idx),
		      halfbyte_en ? DSI_14nm_PHY_LN_CFG1_HALFBYTECLK_EN : 0);
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_TIMING_CTRL_9(lane_idx),
		      DSI_14nm_PHY_LN_TIMING_CTRL_9_TA_GO(timing->ta_go) |
		      DSI_14nm_PHY_LN_TIMING_CTRL_9_TA_SURE(timing->ta_sure));
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_TIMING_CTRL_10(lane_idx),
		      DSI_14nm_PHY_LN_TIMING_CTRL_10_TA_GET(timing->ta_get));
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_TIMING_CTRL_11(lane_idx),
		      DSI_14nm_PHY_LN_TIMING_CTRL_11_TRIG3_CMD(0xa0));
}

static int dsi_14nm_phy_enable(struct msm_dsi_phy *phy,
			       struct msm_dsi_phy_clk_request *clk_req)
{
	struct msm_dsi_dphy_timing *timing = &phy->timing;
	u32 data;
	int i;
	int ret;
	void __iomem *base = phy->base;
	void __iomem *lane_base = phy->lane_base;
	u32 glbl_test_ctrl;

	if (msm_dsi_dphy_timing_calc_v2(timing, clk_req)) {
		DRM_DEV_ERROR(&phy->pdev->dev,
			"%s: D-PHY timing calculation failed\n", __func__);
		return -EINVAL;
	}

	data = 0x1c;
	if (phy->usecase != MSM_DSI_PHY_STANDALONE)
		data |= DSI_14nm_PHY_CMN_LDO_CNTRL_VREG_CTRL(32);
	dsi_phy_write(base + REG_DSI_14nm_PHY_CMN_LDO_CNTRL, data);

	dsi_phy_write(base + REG_DSI_14nm_PHY_CMN_GLBL_TEST_CTRL, 0x1);

	/* 4 data lanes + 1 clk lane configuration */
	for (i = 0; i < 5; i++) {
		dsi_phy_write(lane_base + REG_DSI_14nm_PHY_LN_VREG_CNTRL(i),
			      0x1d);

		dsi_phy_write(lane_base +
			      REG_DSI_14nm_PHY_LN_STRENGTH_CTRL_0(i), 0xff);
		dsi_phy_write(lane_base +
			      REG_DSI_14nm_PHY_LN_STRENGTH_CTRL_1(i),
			      (i == PHY_14NM_CKLN_IDX) ? 0x00 : 0x06);

		dsi_phy_write(lane_base + REG_DSI_14nm_PHY_LN_CFG3(i),
			      (i == PHY_14NM_CKLN_IDX) ? 0x8f : 0x0f);
		dsi_phy_write(lane_base + REG_DSI_14nm_PHY_LN_CFG2(i), 0x10);
		dsi_phy_write(lane_base + REG_DSI_14nm_PHY_LN_TEST_DATAPATH(i),
			      0);
		dsi_phy_write(lane_base + REG_DSI_14nm_PHY_LN_TEST_STR(i),
			      0x88);

		dsi_14nm_dphy_set_timing(phy, timing, i);
	}

	/* Make sure PLL is not start */
	dsi_phy_write(base + REG_DSI_14nm_PHY_CMN_PLL_CNTRL, 0x00);

	wmb(); /* make sure everything is written before reset and enable */

	/* reset digital block */
	dsi_phy_write(base + REG_DSI_14nm_PHY_CMN_CTRL_1, 0x80);
	wmb(); /* ensure reset is asserted */
	udelay(100);
	dsi_phy_write(base + REG_DSI_14nm_PHY_CMN_CTRL_1, 0x00);

	glbl_test_ctrl = dsi_phy_read(base + REG_DSI_14nm_PHY_CMN_GLBL_TEST_CTRL);
	if (phy->id == DSI_1 && phy->usecase == MSM_DSI_PHY_SLAVE)
		glbl_test_ctrl |= DSI_14nm_PHY_CMN_GLBL_TEST_CTRL_BITCLK_HS_SEL;
	else
		glbl_test_ctrl &= ~DSI_14nm_PHY_CMN_GLBL_TEST_CTRL_BITCLK_HS_SEL;
	dsi_phy_write(base + REG_DSI_14nm_PHY_CMN_GLBL_TEST_CTRL, glbl_test_ctrl);
	ret = dsi_14nm_set_usecase(phy);
	if (ret) {
		DRM_DEV_ERROR(&phy->pdev->dev, "%s: set pll usecase failed, %d\n",
			__func__, ret);
		return ret;
	}

	/* Remove power down from PLL and all lanes */
	dsi_phy_write(base + REG_DSI_14nm_PHY_CMN_CTRL_0, 0xff);

	return 0;
}

static void dsi_14nm_phy_disable(struct msm_dsi_phy *phy)
{
	dsi_phy_write(phy->base + REG_DSI_14nm_PHY_CMN_GLBL_TEST_CTRL, 0);
	dsi_phy_write(phy->base + REG_DSI_14nm_PHY_CMN_CTRL_0, 0);

	/* ensure that the phy is completely disabled */
	wmb();
}

const struct msm_dsi_phy_cfg dsi_phy_14nm_cfgs = {
	.has_phy_lane = true,
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vcca", 17000, 32},
		},
	},
	.ops = {
		.enable = dsi_14nm_phy_enable,
		.disable = dsi_14nm_phy_disable,
		.pll_init = dsi_pll_14nm_init,
		.save_pll_state = dsi_14nm_pll_save_state,
		.restore_pll_state = dsi_14nm_pll_restore_state,
	},
	.min_pll_rate = VCO_MIN_RATE,
	.max_pll_rate = VCO_MAX_RATE,
	.io_start = { 0x994400, 0x996400 },
	.num_dsi_phy = 2,
};

const struct msm_dsi_phy_cfg dsi_phy_14nm_660_cfgs = {
	.has_phy_lane = true,
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vcca", 73400, 32},
		},
	},
	.ops = {
		.enable = dsi_14nm_phy_enable,
		.disable = dsi_14nm_phy_disable,
		.pll_init = dsi_pll_14nm_init,
		.save_pll_state = dsi_14nm_pll_save_state,
		.restore_pll_state = dsi_14nm_pll_restore_state,
	},
	.min_pll_rate = VCO_MIN_RATE,
	.max_pll_rate = VCO_MAX_RATE,
	.io_start = { 0xc994400, 0xc996000 },
	.num_dsi_phy = 2,
};
