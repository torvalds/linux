// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>

#include "dsi_pll.h"
#include "dsi.xml.h"

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

#define NUM_PROVIDED_CLKS		2

#define VCO_REF_CLK_RATE		19200000
#define VCO_MIN_RATE			1300000000UL
#define VCO_MAX_RATE			2600000000UL

#define DSI_BYTE_PLL_CLK		0
#define DSI_PIXEL_PLL_CLK		1

#define DSI_PLL_DEFAULT_VCO_POSTDIV	1

struct dsi_pll_input {
	u32 fref;	/* reference clk */
	u32 fdata;	/* bit clock rate */
	u32 dsiclk_sel; /* Mux configuration (see diagram) */
	u32 ssc_en;	/* SSC enable/disable */
	u32 ldo_en;

	/* fixed params */
	u32 refclk_dbler_en;
	u32 vco_measure_time;
	u32 kvco_measure_time;
	u32 bandgap_timer;
	u32 pll_wakeup_timer;
	u32 plllock_cnt;
	u32 plllock_rng;
	u32 ssc_center;
	u32 ssc_adj_period;
	u32 ssc_spread;
	u32 ssc_freq;
	u32 pll_ie_trim;
	u32 pll_ip_trim;
	u32 pll_iptat_trim;
	u32 pll_cpcset_cur;
	u32 pll_cpmset_cur;

	u32 pll_icpmset;
	u32 pll_icpcset;

	u32 pll_icpmset_p;
	u32 pll_icpmset_m;

	u32 pll_icpcset_p;
	u32 pll_icpcset_m;

	u32 pll_lpf_res1;
	u32 pll_lpf_cap1;
	u32 pll_lpf_cap2;
	u32 pll_c3ctrl;
	u32 pll_r3ctrl;
};

struct dsi_pll_output {
	u32 pll_txclk_en;
	u32 dec_start;
	u32 div_frac_start;
	u32 ssc_period;
	u32 ssc_step_size;
	u32 plllock_cmp;
	u32 pll_vco_div_ref;
	u32 pll_vco_count;
	u32 pll_kvco_div_ref;
	u32 pll_kvco_count;
	u32 pll_misc1;
	u32 pll_lpf2_postdiv;
	u32 pll_resetsm_cntrl;
	u32 pll_resetsm_cntrl2;
	u32 pll_resetsm_cntrl5;
	u32 pll_kvco_code;

	u32 cmn_clk_cfg0;
	u32 cmn_clk_cfg1;
	u32 cmn_ldo_cntrl;

	u32 pll_postdiv;
	u32 fcvo;
};

struct pll_14nm_cached_state {
	unsigned long vco_rate;
	u8 n2postdiv;
	u8 n1postdiv;
};

struct dsi_pll_14nm {
	struct msm_dsi_pll base;

	int id;
	struct platform_device *pdev;

	void __iomem *phy_cmn_mmio;
	void __iomem *mmio;

	int vco_delay;

	struct dsi_pll_input in;
	struct dsi_pll_output out;

	/* protects REG_DSI_14nm_PHY_CMN_CLK_CFG0 register */
	spinlock_t postdiv_lock;

	u64 vco_current_rate;
	u64 vco_ref_clk_rate;

	/* private clocks: */
	struct clk_hw *hws[NUM_DSI_CLOCKS_MAX];
	u32 num_hws;

	/* clock-provider: */
	struct clk_hw_onecell_data *hw_data;

	struct pll_14nm_cached_state cached_state;

	enum msm_dsi_phy_usecase uc;
	struct dsi_pll_14nm *slave;
};

#define to_pll_14nm(x)	container_of(x, struct dsi_pll_14nm, base)

/*
 * Private struct for N1/N2 post-divider clocks. These clocks are similar to
 * the generic clk_divider class of clocks. The only difference is that it
 * also sets the slave DSI PLL's post-dividers if in Dual DSI mode
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
 * Global list of private DSI PLL struct pointers. We need this for Dual DSI
 * mode, where the master PLL's clk_ops needs access the slave's private data
 */
static struct dsi_pll_14nm *pll_14nm_list[DSI_MAX];

static bool pll_14nm_poll_for_ready(struct dsi_pll_14nm *pll_14nm,
				    u32 nb_tries, u32 timeout_us)
{
	bool pll_locked = false;
	void __iomem *base = pll_14nm->mmio;
	u32 tries, val;

	tries = nb_tries;
	while (tries--) {
		val = pll_read(base +
			       REG_DSI_14nm_PHY_PLL_RESET_SM_READY_STATUS);
		pll_locked = !!(val & BIT(5));

		if (pll_locked)
			break;

		udelay(timeout_us);
	}

	if (!pll_locked) {
		tries = nb_tries;
		while (tries--) {
			val = pll_read(base +
				REG_DSI_14nm_PHY_PLL_RESET_SM_READY_STATUS);
			pll_locked = !!(val & BIT(0));

			if (pll_locked)
				break;

			udelay(timeout_us);
		}
	}

	DBG("DSI PLL is %slocked", pll_locked ? "" : "*not* ");

	return pll_locked;
}

static void dsi_pll_14nm_input_init(struct dsi_pll_14nm *pll)
{
	pll->in.fref = pll->vco_ref_clk_rate;
	pll->in.fdata = 0;
	pll->in.dsiclk_sel = 1;	/* Use the /2 path in Mux */
	pll->in.ldo_en = 0;	/* disabled for now */

	/* fixed input */
	pll->in.refclk_dbler_en = 0;
	pll->in.vco_measure_time = 5;
	pll->in.kvco_measure_time = 5;
	pll->in.bandgap_timer = 4;
	pll->in.pll_wakeup_timer = 5;
	pll->in.plllock_cnt = 1;
	pll->in.plllock_rng = 0;

	/*
	 * SSC is enabled by default. We might need DT props for configuring
	 * some SSC params like PPM and center/down spread etc.
	 */
	pll->in.ssc_en = 1;
	pll->in.ssc_center = 0;		/* down spread by default */
	pll->in.ssc_spread = 5;		/* PPM / 1000 */
	pll->in.ssc_freq = 31500;	/* default recommended */
	pll->in.ssc_adj_period = 37;

	pll->in.pll_ie_trim = 4;
	pll->in.pll_ip_trim = 4;
	pll->in.pll_cpcset_cur = 1;
	pll->in.pll_cpmset_cur = 1;
	pll->in.pll_icpmset = 4;
	pll->in.pll_icpcset = 4;
	pll->in.pll_icpmset_p = 0;
	pll->in.pll_icpmset_m = 0;
	pll->in.pll_icpcset_p = 0;
	pll->in.pll_icpcset_m = 0;
	pll->in.pll_lpf_res1 = 3;
	pll->in.pll_lpf_cap1 = 11;
	pll->in.pll_lpf_cap2 = 1;
	pll->in.pll_iptat_trim = 7;
	pll->in.pll_c3ctrl = 2;
	pll->in.pll_r3ctrl = 1;
}

#define CEIL(x, y)		(((x) + ((y) - 1)) / (y))

static void pll_14nm_ssc_calc(struct dsi_pll_14nm *pll)
{
	u32 period, ssc_period;
	u32 ref, rem;
	u64 step_size;

	DBG("vco=%lld ref=%lld", pll->vco_current_rate, pll->vco_ref_clk_rate);

	ssc_period = pll->in.ssc_freq / 500;
	period = (u32)pll->vco_ref_clk_rate / 1000;
	ssc_period  = CEIL(period, ssc_period);
	ssc_period -= 1;
	pll->out.ssc_period = ssc_period;

	DBG("ssc freq=%d spread=%d period=%d", pll->in.ssc_freq,
	    pll->in.ssc_spread, pll->out.ssc_period);

	step_size = (u32)pll->vco_current_rate;
	ref = pll->vco_ref_clk_rate;
	ref /= 1000;
	step_size = div_u64(step_size, ref);
	step_size <<= 20;
	step_size = div_u64(step_size, 1000);
	step_size *= pll->in.ssc_spread;
	step_size = div_u64(step_size, 1000);
	step_size *= (pll->in.ssc_adj_period + 1);

	rem = 0;
	step_size = div_u64_rem(step_size, ssc_period + 1, &rem);
	if (rem)
		step_size++;

	DBG("step_size=%lld", step_size);

	step_size &= 0x0ffff;	/* take lower 16 bits */

	pll->out.ssc_step_size = step_size;
}

static void pll_14nm_dec_frac_calc(struct dsi_pll_14nm *pll)
{
	struct dsi_pll_input *pin = &pll->in;
	struct dsi_pll_output *pout = &pll->out;
	u64 multiplier = BIT(20);
	u64 dec_start_multiple, dec_start, pll_comp_val;
	u32 duration, div_frac_start;
	u64 vco_clk_rate = pll->vco_current_rate;
	u64 fref = pll->vco_ref_clk_rate;

	DBG("vco_clk_rate=%lld ref_clk_rate=%lld", vco_clk_rate, fref);

	dec_start_multiple = div_u64(vco_clk_rate * multiplier, fref);
	div_u64_rem(dec_start_multiple, multiplier, &div_frac_start);

	dec_start = div_u64(dec_start_multiple, multiplier);

	pout->dec_start = (u32)dec_start;
	pout->div_frac_start = div_frac_start;

	if (pin->plllock_cnt == 0)
		duration = 1024;
	else if (pin->plllock_cnt == 1)
		duration = 256;
	else if (pin->plllock_cnt == 2)
		duration = 128;
	else
		duration = 32;

	pll_comp_val = duration * dec_start_multiple;
	pll_comp_val = div_u64(pll_comp_val, multiplier);
	do_div(pll_comp_val, 10);

	pout->plllock_cmp = (u32)pll_comp_val;

	pout->pll_txclk_en = 1;
	pout->cmn_ldo_cntrl = 0x3c;
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

static void pll_14nm_calc_vco_count(struct dsi_pll_14nm *pll)
{
	struct dsi_pll_input *pin = &pll->in;
	struct dsi_pll_output *pout = &pll->out;
	u64 vco_clk_rate = pll->vco_current_rate;
	u64 fref = pll->vco_ref_clk_rate;
	u64 data;
	u32 cnt;

	data = fref * pin->vco_measure_time;
	do_div(data, 1000000);
	data &= 0x03ff;	/* 10 bits */
	data -= 2;
	pout->pll_vco_div_ref = data;

	data = div_u64(vco_clk_rate, 1000000);	/* unit is Mhz */
	data *= pin->vco_measure_time;
	do_div(data, 10);
	pout->pll_vco_count = data;

	data = fref * pin->kvco_measure_time;
	do_div(data, 1000000);
	data &= 0x03ff;	/* 10 bits */
	data -= 1;
	pout->pll_kvco_div_ref = data;

	cnt = pll_14nm_kvco_slop(vco_clk_rate);
	cnt *= 2;
	cnt /= 100;
	cnt *= pin->kvco_measure_time;
	pout->pll_kvco_count = cnt;

	pout->pll_misc1 = 16;
	pout->pll_resetsm_cntrl = 48;
	pout->pll_resetsm_cntrl2 = pin->bandgap_timer << 3;
	pout->pll_resetsm_cntrl5 = pin->pll_wakeup_timer;
	pout->pll_kvco_code = 0;
}

static void pll_db_commit_ssc(struct dsi_pll_14nm *pll)
{
	void __iomem *base = pll->mmio;
	struct dsi_pll_input *pin = &pll->in;
	struct dsi_pll_output *pout = &pll->out;
	u8 data;

	data = pin->ssc_adj_period;
	data &= 0x0ff;
	pll_write(base + REG_DSI_14nm_PHY_PLL_SSC_ADJ_PER1, data);
	data = (pin->ssc_adj_period >> 8);
	data &= 0x03;
	pll_write(base + REG_DSI_14nm_PHY_PLL_SSC_ADJ_PER2, data);

	data = pout->ssc_period;
	data &= 0x0ff;
	pll_write(base + REG_DSI_14nm_PHY_PLL_SSC_PER1, data);
	data = (pout->ssc_period >> 8);
	data &= 0x0ff;
	pll_write(base + REG_DSI_14nm_PHY_PLL_SSC_PER2, data);

	data = pout->ssc_step_size;
	data &= 0x0ff;
	pll_write(base + REG_DSI_14nm_PHY_PLL_SSC_STEP_SIZE1, data);
	data = (pout->ssc_step_size >> 8);
	data &= 0x0ff;
	pll_write(base + REG_DSI_14nm_PHY_PLL_SSC_STEP_SIZE2, data);

	data = (pin->ssc_center & 0x01);
	data <<= 1;
	data |= 0x01; /* enable */
	pll_write(base + REG_DSI_14nm_PHY_PLL_SSC_EN_CENTER, data);

	wmb();	/* make sure register committed */
}

static void pll_db_commit_common(struct dsi_pll_14nm *pll,
				 struct dsi_pll_input *pin,
				 struct dsi_pll_output *pout)
{
	void __iomem *base = pll->mmio;
	u8 data;

	/* confgiure the non frequency dependent pll registers */
	data = 0;
	pll_write(base + REG_DSI_14nm_PHY_PLL_SYSCLK_EN_RESET, data);

	data = pout->pll_txclk_en;
	pll_write(base + REG_DSI_14nm_PHY_PLL_TXCLK_EN, data);

	data = pout->pll_resetsm_cntrl;
	pll_write(base + REG_DSI_14nm_PHY_PLL_RESETSM_CNTRL, data);
	data = pout->pll_resetsm_cntrl2;
	pll_write(base + REG_DSI_14nm_PHY_PLL_RESETSM_CNTRL2, data);
	data = pout->pll_resetsm_cntrl5;
	pll_write(base + REG_DSI_14nm_PHY_PLL_RESETSM_CNTRL5, data);

	data = pout->pll_vco_div_ref & 0xff;
	pll_write(base + REG_DSI_14nm_PHY_PLL_VCO_DIV_REF1, data);
	data = (pout->pll_vco_div_ref >> 8) & 0x3;
	pll_write(base + REG_DSI_14nm_PHY_PLL_VCO_DIV_REF2, data);

	data = pout->pll_kvco_div_ref & 0xff;
	pll_write(base + REG_DSI_14nm_PHY_PLL_KVCO_DIV_REF1, data);
	data = (pout->pll_kvco_div_ref >> 8) & 0x3;
	pll_write(base + REG_DSI_14nm_PHY_PLL_KVCO_DIV_REF2, data);

	data = pout->pll_misc1;
	pll_write(base + REG_DSI_14nm_PHY_PLL_PLL_MISC1, data);

	data = pin->pll_ie_trim;
	pll_write(base + REG_DSI_14nm_PHY_PLL_IE_TRIM, data);

	data = pin->pll_ip_trim;
	pll_write(base + REG_DSI_14nm_PHY_PLL_IP_TRIM, data);

	data = pin->pll_cpmset_cur << 3 | pin->pll_cpcset_cur;
	pll_write(base + REG_DSI_14nm_PHY_PLL_CP_SET_CUR, data);

	data = pin->pll_icpcset_p << 3 | pin->pll_icpcset_m;
	pll_write(base + REG_DSI_14nm_PHY_PLL_PLL_ICPCSET, data);

	data = pin->pll_icpmset_p << 3 | pin->pll_icpcset_m;
	pll_write(base + REG_DSI_14nm_PHY_PLL_PLL_ICPMSET, data);

	data = pin->pll_icpmset << 3 | pin->pll_icpcset;
	pll_write(base + REG_DSI_14nm_PHY_PLL_PLL_ICP_SET, data);

	data = pin->pll_lpf_cap2 << 4 | pin->pll_lpf_cap1;
	pll_write(base + REG_DSI_14nm_PHY_PLL_PLL_LPF1, data);

	data = pin->pll_iptat_trim;
	pll_write(base + REG_DSI_14nm_PHY_PLL_IPTAT_TRIM, data);

	data = pin->pll_c3ctrl | pin->pll_r3ctrl << 4;
	pll_write(base + REG_DSI_14nm_PHY_PLL_PLL_CRCTRL, data);
}

static void pll_14nm_software_reset(struct dsi_pll_14nm *pll_14nm)
{
	void __iomem *cmn_base = pll_14nm->phy_cmn_mmio;

	/* de assert pll start and apply pll sw reset */

	/* stop pll */
	pll_write(cmn_base + REG_DSI_14nm_PHY_CMN_PLL_CNTRL, 0);

	/* pll sw reset */
	pll_write_udelay(cmn_base + REG_DSI_14nm_PHY_CMN_CTRL_1, 0x20, 10);
	wmb();	/* make sure register committed */

	pll_write(cmn_base + REG_DSI_14nm_PHY_CMN_CTRL_1, 0);
	wmb();	/* make sure register committed */
}

static void pll_db_commit_14nm(struct dsi_pll_14nm *pll,
			       struct dsi_pll_input *pin,
			       struct dsi_pll_output *pout)
{
	void __iomem *base = pll->mmio;
	void __iomem *cmn_base = pll->phy_cmn_mmio;
	u8 data;

	DBG("DSI%d PLL", pll->id);

	data = pout->cmn_ldo_cntrl;
	pll_write(cmn_base + REG_DSI_14nm_PHY_CMN_LDO_CNTRL, data);

	pll_db_commit_common(pll, pin, pout);

	pll_14nm_software_reset(pll);

	data = pin->dsiclk_sel; /* set dsiclk_sel = 1  */
	pll_write(cmn_base + REG_DSI_14nm_PHY_CMN_CLK_CFG1, data);

	data = 0xff; /* data, clk, pll normal operation */
	pll_write(cmn_base + REG_DSI_14nm_PHY_CMN_CTRL_0, data);

	/* configure the frequency dependent pll registers */
	data = pout->dec_start;
	pll_write(base + REG_DSI_14nm_PHY_PLL_DEC_START, data);

	data = pout->div_frac_start & 0xff;
	pll_write(base + REG_DSI_14nm_PHY_PLL_DIV_FRAC_START1, data);
	data = (pout->div_frac_start >> 8) & 0xff;
	pll_write(base + REG_DSI_14nm_PHY_PLL_DIV_FRAC_START2, data);
	data = (pout->div_frac_start >> 16) & 0xf;
	pll_write(base + REG_DSI_14nm_PHY_PLL_DIV_FRAC_START3, data);

	data = pout->plllock_cmp & 0xff;
	pll_write(base + REG_DSI_14nm_PHY_PLL_PLLLOCK_CMP1, data);

	data = (pout->plllock_cmp >> 8) & 0xff;
	pll_write(base + REG_DSI_14nm_PHY_PLL_PLLLOCK_CMP2, data);

	data = (pout->plllock_cmp >> 16) & 0x3;
	pll_write(base + REG_DSI_14nm_PHY_PLL_PLLLOCK_CMP3, data);

	data = pin->plllock_cnt << 1 | pin->plllock_rng << 3;
	pll_write(base + REG_DSI_14nm_PHY_PLL_PLLLOCK_CMP_EN, data);

	data = pout->pll_vco_count & 0xff;
	pll_write(base + REG_DSI_14nm_PHY_PLL_VCO_COUNT1, data);
	data = (pout->pll_vco_count >> 8) & 0xff;
	pll_write(base + REG_DSI_14nm_PHY_PLL_VCO_COUNT2, data);

	data = pout->pll_kvco_count & 0xff;
	pll_write(base + REG_DSI_14nm_PHY_PLL_KVCO_COUNT1, data);
	data = (pout->pll_kvco_count >> 8) & 0x3;
	pll_write(base + REG_DSI_14nm_PHY_PLL_KVCO_COUNT2, data);

	data = (pout->pll_postdiv - 1) << 4 | pin->pll_lpf_res1;
	pll_write(base + REG_DSI_14nm_PHY_PLL_PLL_LPF2_POSTDIV, data);

	if (pin->ssc_en)
		pll_db_commit_ssc(pll);

	wmb();	/* make sure register committed */
}

/*
 * VCO clock Callbacks
 */
static int dsi_pll_14nm_vco_set_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	struct msm_dsi_pll *pll = hw_clk_to_pll(hw);
	struct dsi_pll_14nm *pll_14nm = to_pll_14nm(pll);
	struct dsi_pll_input *pin = &pll_14nm->in;
	struct dsi_pll_output *pout = &pll_14nm->out;

	DBG("DSI PLL%d rate=%lu, parent's=%lu", pll_14nm->id, rate,
	    parent_rate);

	pll_14nm->vco_current_rate = rate;
	pll_14nm->vco_ref_clk_rate = VCO_REF_CLK_RATE;

	dsi_pll_14nm_input_init(pll_14nm);

	/*
	 * This configures the post divider internal to the VCO. It's
	 * fixed to divide by 1 for now.
	 *
	 * tx_band = pll_postdiv.
	 * 0: divided by 1
	 * 1: divided by 2
	 * 2: divided by 4
	 * 3: divided by 8
	 */
	pout->pll_postdiv = DSI_PLL_DEFAULT_VCO_POSTDIV;

	pll_14nm_dec_frac_calc(pll_14nm);

	if (pin->ssc_en)
		pll_14nm_ssc_calc(pll_14nm);

	pll_14nm_calc_vco_count(pll_14nm);

	/* commit the slave DSI PLL registers if we're master. Note that we
	 * don't lock the slave PLL. We just ensure that the PLL/PHY registers
	 * of the master and slave are identical
	 */
	if (pll_14nm->uc == MSM_DSI_PHY_MASTER) {
		struct dsi_pll_14nm *pll_14nm_slave = pll_14nm->slave;

		pll_db_commit_14nm(pll_14nm_slave, pin, pout);
	}

	pll_db_commit_14nm(pll_14nm, pin, pout);

	return 0;
}

static unsigned long dsi_pll_14nm_vco_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct msm_dsi_pll *pll = hw_clk_to_pll(hw);
	struct dsi_pll_14nm *pll_14nm = to_pll_14nm(pll);
	void __iomem *base = pll_14nm->mmio;
	u64 vco_rate, multiplier = BIT(20);
	u32 div_frac_start;
	u32 dec_start;
	u64 ref_clk = parent_rate;

	dec_start = pll_read(base + REG_DSI_14nm_PHY_PLL_DEC_START);
	dec_start &= 0x0ff;

	DBG("dec_start = %x", dec_start);

	div_frac_start = (pll_read(base + REG_DSI_14nm_PHY_PLL_DIV_FRAC_START3)
				& 0xf) << 16;
	div_frac_start |= (pll_read(base + REG_DSI_14nm_PHY_PLL_DIV_FRAC_START2)
				& 0xff) << 8;
	div_frac_start |= pll_read(base + REG_DSI_14nm_PHY_PLL_DIV_FRAC_START1)
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

static const struct clk_ops clk_ops_dsi_pll_14nm_vco = {
	.round_rate = msm_dsi_pll_helper_clk_round_rate,
	.set_rate = dsi_pll_14nm_vco_set_rate,
	.recalc_rate = dsi_pll_14nm_vco_recalc_rate,
	.prepare = msm_dsi_pll_helper_clk_prepare,
	.unprepare = msm_dsi_pll_helper_clk_unprepare,
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
	void __iomem *base = pll_14nm->phy_cmn_mmio;
	u8 shift = postdiv->shift;
	u8 width = postdiv->width;
	u32 val;

	DBG("DSI%d PLL parent rate=%lu", pll_14nm->id, parent_rate);

	val = pll_read(base + REG_DSI_14nm_PHY_CMN_CLK_CFG0) >> shift;
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

	DBG("DSI%d PLL parent rate=%lu", pll_14nm->id, rate);

	return divider_round_rate(hw, rate, prate, NULL,
				  postdiv->width,
				  postdiv->flags);
}

static int dsi_pll_14nm_postdiv_set_rate(struct clk_hw *hw, unsigned long rate,
					 unsigned long parent_rate)
{
	struct dsi_pll_14nm_postdiv *postdiv = to_pll_14nm_postdiv(hw);
	struct dsi_pll_14nm *pll_14nm = postdiv->pll;
	void __iomem *base = pll_14nm->phy_cmn_mmio;
	spinlock_t *lock = &pll_14nm->postdiv_lock;
	u8 shift = postdiv->shift;
	u8 width = postdiv->width;
	unsigned int value;
	unsigned long flags = 0;
	u32 val;

	DBG("DSI%d PLL parent rate=%lu parent rate %lu", pll_14nm->id, rate,
	    parent_rate);

	value = divider_get_val(rate, parent_rate, NULL, postdiv->width,
				postdiv->flags);

	spin_lock_irqsave(lock, flags);

	val = pll_read(base + REG_DSI_14nm_PHY_CMN_CLK_CFG0);
	val &= ~(div_mask(width) << shift);

	val |= value << shift;
	pll_write(base + REG_DSI_14nm_PHY_CMN_CLK_CFG0, val);

	/* If we're master in dual DSI mode, then the slave PLL's post-dividers
	 * follow the master's post dividers
	 */
	if (pll_14nm->uc == MSM_DSI_PHY_MASTER) {
		struct dsi_pll_14nm *pll_14nm_slave = pll_14nm->slave;
		void __iomem *slave_base = pll_14nm_slave->phy_cmn_mmio;

		pll_write(slave_base + REG_DSI_14nm_PHY_CMN_CLK_CFG0, val);
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

static int dsi_pll_14nm_enable_seq(struct msm_dsi_pll *pll)
{
	struct dsi_pll_14nm *pll_14nm = to_pll_14nm(pll);
	void __iomem *base = pll_14nm->mmio;
	void __iomem *cmn_base = pll_14nm->phy_cmn_mmio;
	bool locked;

	DBG("");

	pll_write(base + REG_DSI_14nm_PHY_PLL_VREF_CFG1, 0x10);
	pll_write(cmn_base + REG_DSI_14nm_PHY_CMN_PLL_CNTRL, 1);

	locked = pll_14nm_poll_for_ready(pll_14nm, POLL_MAX_READS,
					 POLL_TIMEOUT_US);

	if (unlikely(!locked))
		DRM_DEV_ERROR(&pll_14nm->pdev->dev, "DSI PLL lock failed\n");
	else
		DBG("DSI PLL lock success");

	return locked ? 0 : -EINVAL;
}

static void dsi_pll_14nm_disable_seq(struct msm_dsi_pll *pll)
{
	struct dsi_pll_14nm *pll_14nm = to_pll_14nm(pll);
	void __iomem *cmn_base = pll_14nm->phy_cmn_mmio;

	DBG("");

	pll_write(cmn_base + REG_DSI_14nm_PHY_CMN_PLL_CNTRL, 0);
}

static void dsi_pll_14nm_save_state(struct msm_dsi_pll *pll)
{
	struct dsi_pll_14nm *pll_14nm = to_pll_14nm(pll);
	struct pll_14nm_cached_state *cached_state = &pll_14nm->cached_state;
	void __iomem *cmn_base = pll_14nm->phy_cmn_mmio;
	u32 data;

	data = pll_read(cmn_base + REG_DSI_14nm_PHY_CMN_CLK_CFG0);

	cached_state->n1postdiv = data & 0xf;
	cached_state->n2postdiv = (data >> 4) & 0xf;

	DBG("DSI%d PLL save state %x %x", pll_14nm->id,
	    cached_state->n1postdiv, cached_state->n2postdiv);

	cached_state->vco_rate = clk_hw_get_rate(&pll->clk_hw);
}

static int dsi_pll_14nm_restore_state(struct msm_dsi_pll *pll)
{
	struct dsi_pll_14nm *pll_14nm = to_pll_14nm(pll);
	struct pll_14nm_cached_state *cached_state = &pll_14nm->cached_state;
	void __iomem *cmn_base = pll_14nm->phy_cmn_mmio;
	u32 data;
	int ret;

	ret = dsi_pll_14nm_vco_set_rate(&pll->clk_hw,
					cached_state->vco_rate, 0);
	if (ret) {
		DRM_DEV_ERROR(&pll_14nm->pdev->dev,
			"restore vco rate failed. ret=%d\n", ret);
		return ret;
	}

	data = cached_state->n1postdiv | (cached_state->n2postdiv << 4);

	DBG("DSI%d PLL restore state %x %x", pll_14nm->id,
	    cached_state->n1postdiv, cached_state->n2postdiv);

	pll_write(cmn_base + REG_DSI_14nm_PHY_CMN_CLK_CFG0, data);

	/* also restore post-dividers for slave DSI PLL */
	if (pll_14nm->uc == MSM_DSI_PHY_MASTER) {
		struct dsi_pll_14nm *pll_14nm_slave = pll_14nm->slave;
		void __iomem *slave_base = pll_14nm_slave->phy_cmn_mmio;

		pll_write(slave_base + REG_DSI_14nm_PHY_CMN_CLK_CFG0, data);
	}

	return 0;
}

static int dsi_pll_14nm_set_usecase(struct msm_dsi_pll *pll,
				    enum msm_dsi_phy_usecase uc)
{
	struct dsi_pll_14nm *pll_14nm = to_pll_14nm(pll);
	void __iomem *base = pll_14nm->mmio;
	u32 clkbuflr_en, bandgap = 0;

	switch (uc) {
	case MSM_DSI_PHY_STANDALONE:
		clkbuflr_en = 0x1;
		break;
	case MSM_DSI_PHY_MASTER:
		clkbuflr_en = 0x3;
		pll_14nm->slave = pll_14nm_list[(pll_14nm->id + 1) % DSI_MAX];
		break;
	case MSM_DSI_PHY_SLAVE:
		clkbuflr_en = 0x0;
		bandgap = 0x3;
		break;
	default:
		return -EINVAL;
	}

	pll_write(base + REG_DSI_14nm_PHY_PLL_CLKBUFLR_EN, clkbuflr_en);
	if (bandgap)
		pll_write(base + REG_DSI_14nm_PHY_PLL_PLL_BANDGAP, bandgap);

	pll_14nm->uc = uc;

	return 0;
}

static int dsi_pll_14nm_get_provider(struct msm_dsi_pll *pll,
				     struct clk **byte_clk_provider,
				     struct clk **pixel_clk_provider)
{
	struct dsi_pll_14nm *pll_14nm = to_pll_14nm(pll);
	struct clk_hw_onecell_data *hw_data = pll_14nm->hw_data;

	if (byte_clk_provider)
		*byte_clk_provider = hw_data->hws[DSI_BYTE_PLL_CLK]->clk;
	if (pixel_clk_provider)
		*pixel_clk_provider = hw_data->hws[DSI_PIXEL_PLL_CLK]->clk;

	return 0;
}

static void dsi_pll_14nm_destroy(struct msm_dsi_pll *pll)
{
	struct dsi_pll_14nm *pll_14nm = to_pll_14nm(pll);
	struct platform_device *pdev = pll_14nm->pdev;
	int num_hws = pll_14nm->num_hws;

	of_clk_del_provider(pdev->dev.of_node);

	while (num_hws--)
		clk_hw_unregister(pll_14nm->hws[num_hws]);
}

static struct clk_hw *pll_14nm_postdiv_register(struct dsi_pll_14nm *pll_14nm,
						const char *name,
						const char *parent_name,
						unsigned long flags,
						u8 shift)
{
	struct dsi_pll_14nm_postdiv *pll_postdiv;
	struct device *dev = &pll_14nm->pdev->dev;
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

	ret = clk_hw_register(dev, &pll_postdiv->hw);
	if (ret)
		return ERR_PTR(ret);

	return &pll_postdiv->hw;
}

static int pll_14nm_register(struct dsi_pll_14nm *pll_14nm)
{
	char clk_name[32], parent[32], vco_name[32];
	struct clk_init_data vco_init = {
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.name = vco_name,
		.flags = CLK_IGNORE_UNUSED,
		.ops = &clk_ops_dsi_pll_14nm_vco,
	};
	struct device *dev = &pll_14nm->pdev->dev;
	struct clk_hw **hws = pll_14nm->hws;
	struct clk_hw_onecell_data *hw_data;
	struct clk_hw *hw;
	int num = 0;
	int ret;

	DBG("DSI%d", pll_14nm->id);

	hw_data = devm_kzalloc(dev, sizeof(*hw_data) +
			       NUM_PROVIDED_CLKS * sizeof(struct clk_hw *),
			       GFP_KERNEL);
	if (!hw_data)
		return -ENOMEM;

	snprintf(vco_name, 32, "dsi%dvco_clk", pll_14nm->id);
	pll_14nm->base.clk_hw.init = &vco_init;

	ret = clk_hw_register(dev, &pll_14nm->base.clk_hw);
	if (ret)
		return ret;

	hws[num++] = &pll_14nm->base.clk_hw;

	snprintf(clk_name, 32, "dsi%dn1_postdiv_clk", pll_14nm->id);
	snprintf(parent, 32, "dsi%dvco_clk", pll_14nm->id);

	/* N1 postdiv, bits 0-3 in REG_DSI_14nm_PHY_CMN_CLK_CFG0 */
	hw = pll_14nm_postdiv_register(pll_14nm, clk_name, parent,
				       CLK_SET_RATE_PARENT, 0);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	hws[num++] = hw;

	snprintf(clk_name, 32, "dsi%dpllbyte", pll_14nm->id);
	snprintf(parent, 32, "dsi%dn1_postdiv_clk", pll_14nm->id);

	/* DSI Byte clock = VCO_CLK / N1 / 8 */
	hw = clk_hw_register_fixed_factor(dev, clk_name, parent,
					  CLK_SET_RATE_PARENT, 1, 8);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	hws[num++] = hw;
	hw_data->hws[DSI_BYTE_PLL_CLK] = hw;

	snprintf(clk_name, 32, "dsi%dn1_postdivby2_clk", pll_14nm->id);
	snprintf(parent, 32, "dsi%dn1_postdiv_clk", pll_14nm->id);

	/*
	 * Skip the mux for now, force DSICLK_SEL to 1, Add a /2 divider
	 * on the way. Don't let it set parent.
	 */
	hw = clk_hw_register_fixed_factor(dev, clk_name, parent, 0, 1, 2);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	hws[num++] = hw;

	snprintf(clk_name, 32, "dsi%dpll", pll_14nm->id);
	snprintf(parent, 32, "dsi%dn1_postdivby2_clk", pll_14nm->id);

	/* DSI pixel clock = VCO_CLK / N1 / 2 / N2
	 * This is the output of N2 post-divider, bits 4-7 in
	 * REG_DSI_14nm_PHY_CMN_CLK_CFG0. Don't let it set parent.
	 */
	hw = pll_14nm_postdiv_register(pll_14nm, clk_name, parent, 0, 4);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	hws[num++] = hw;
	hw_data->hws[DSI_PIXEL_PLL_CLK]	= hw;

	pll_14nm->num_hws = num;

	hw_data->num = NUM_PROVIDED_CLKS;
	pll_14nm->hw_data = hw_data;

	ret = of_clk_add_hw_provider(dev->of_node, of_clk_hw_onecell_get,
				     pll_14nm->hw_data);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to register clk provider: %d\n", ret);
		return ret;
	}

	return 0;
}

struct msm_dsi_pll *msm_dsi_pll_14nm_init(struct platform_device *pdev, int id)
{
	struct dsi_pll_14nm *pll_14nm;
	struct msm_dsi_pll *pll;
	int ret;

	if (!pdev)
		return ERR_PTR(-ENODEV);

	pll_14nm = devm_kzalloc(&pdev->dev, sizeof(*pll_14nm), GFP_KERNEL);
	if (!pll_14nm)
		return ERR_PTR(-ENOMEM);

	DBG("PLL%d", id);

	pll_14nm->pdev = pdev;
	pll_14nm->id = id;
	pll_14nm_list[id] = pll_14nm;

	pll_14nm->phy_cmn_mmio = msm_ioremap(pdev, "dsi_phy", "DSI_PHY");
	if (IS_ERR_OR_NULL(pll_14nm->phy_cmn_mmio)) {
		DRM_DEV_ERROR(&pdev->dev, "failed to map CMN PHY base\n");
		return ERR_PTR(-ENOMEM);
	}

	pll_14nm->mmio = msm_ioremap(pdev, "dsi_pll", "DSI_PLL");
	if (IS_ERR_OR_NULL(pll_14nm->mmio)) {
		DRM_DEV_ERROR(&pdev->dev, "failed to map PLL base\n");
		return ERR_PTR(-ENOMEM);
	}

	spin_lock_init(&pll_14nm->postdiv_lock);

	pll = &pll_14nm->base;
	pll->min_rate = VCO_MIN_RATE;
	pll->max_rate = VCO_MAX_RATE;
	pll->get_provider = dsi_pll_14nm_get_provider;
	pll->destroy = dsi_pll_14nm_destroy;
	pll->disable_seq = dsi_pll_14nm_disable_seq;
	pll->save_state = dsi_pll_14nm_save_state;
	pll->restore_state = dsi_pll_14nm_restore_state;
	pll->set_usecase = dsi_pll_14nm_set_usecase;

	pll_14nm->vco_delay = 1;

	pll->en_seq_cnt = 1;
	pll->enable_seqs[0] = dsi_pll_14nm_enable_seq;

	ret = pll_14nm_register(pll_14nm);
	if (ret) {
		DRM_DEV_ERROR(&pdev->dev, "failed to register PLL: %d\n", ret);
		return ERR_PTR(ret);
	}

	return pll;
}
