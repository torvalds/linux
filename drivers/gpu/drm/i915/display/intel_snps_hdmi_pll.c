// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Synopsys, Inc., Intel Corporation
 */

#include <linux/math.h>

#include "intel_cx0_phy_regs.h"
#include "intel_display_types.h"
#include "intel_snps_phy.h"
#include "intel_snps_phy_regs.h"
#include "intel_snps_hdmi_pll.h"

#define INTEL_SNPS_PHY_HDMI_4999MHZ 4999999900ULL
#define INTEL_SNPS_PHY_HDMI_16GHZ 16000000000ULL
#define INTEL_SNPS_PHY_HDMI_9999MHZ (2 * INTEL_SNPS_PHY_HDMI_4999MHZ)

#define CURVE0_MULTIPLIER 1000000000
#define CURVE1_MULTIPLIER 100
#define CURVE2_MULTIPLIER 1000000000000ULL

struct pll_output_params {
	u32 ssc_up_spread;
	u32 mpll_div5_en;
	u32 hdmi_div;
	u32 ana_cp_int;
	u32 ana_cp_prop;
	u32 refclk_postscalar;
	u32 tx_clk_div;
	u32 fracn_quot;
	u32 fracn_rem;
	u32 fracn_den;
	u32 fracn_en;
	u32 pmix_en;
	u32 multiplier;
	int mpll_ana_v2i;
	int ana_freq_vco;
};

static s64 interp(s64 x, s64 x1, s64 x2, s64 y1, s64 y2)
{
	s64 dydx;

	dydx = DIV_ROUND_UP_ULL((y2 - y1) * 100000, (x2 - x1));

	return (y1 + DIV_ROUND_UP_ULL(dydx * (x - x1), 100000));
}

static void get_ana_cp_int_prop(u32 vco_clk,
				u32 refclk_postscalar,
				int mpll_ana_v2i,
				int c, int a,
				const u64 curve_freq_hz[2][8],
				const u64 curve_0[2][8],
				const u64 curve_1[2][8],
				const u64 curve_2[2][8],
				u32 *ana_cp_int,
				u32 *ana_cp_prop)
{
	u64 vco_div_refclk_float;
	u64 curve_0_interpolated;
	u64 curve_2_interpolated;
	u64 curve_1_interpolated;
	u64 curve_2_scaled1;
	u64 curve_2_scaled2;
	u64 adjusted_vco_clk1;
	u64 adjusted_vco_clk2;
	u64 curve_2_scaled_int;
	u64 interpolated_product;
	u64 scaled_interpolated_sqrt;
	u64 scaled_vco_div_refclk1;
	u64 scaled_vco_div_refclk2;
	u64 ana_cp_int_temp;
	u64 temp;

	vco_div_refclk_float = vco_clk * DIV_ROUND_DOWN_ULL(1000000000000ULL, refclk_postscalar);

	/* Interpolate curve values at the target vco_clk frequency */
	curve_0_interpolated = interp(vco_clk, curve_freq_hz[c][a], curve_freq_hz[c][a + 1],
				      curve_0[c][a], curve_0[c][a + 1]);

	curve_2_interpolated = interp(vco_clk, curve_freq_hz[c][a], curve_freq_hz[c][a + 1],
				      curve_2[c][a], curve_2[c][a + 1]);

	curve_1_interpolated = interp(vco_clk, curve_freq_hz[c][a], curve_freq_hz[c][a + 1],
				      curve_1[c][a], curve_1[c][a + 1]);

	curve_1_interpolated = DIV_ROUND_DOWN_ULL(curve_1_interpolated, CURVE1_MULTIPLIER);

	/*
	 * Scale curve_2_interpolated based on mpll_ana_v2i, for integer part
	 * ana_cp_int and for the proportional part ana_cp_prop
	 */
	temp = curve_2_interpolated * (4 - mpll_ana_v2i);
	curve_2_scaled1 = DIV_ROUND_DOWN_ULL(temp, 16000);
	curve_2_scaled2 = DIV_ROUND_DOWN_ULL(temp, 160);

	/* Scale vco_div_refclk for ana_cp_int */
	scaled_vco_div_refclk1 = 112008301 * DIV_ROUND_DOWN_ULL(vco_div_refclk_float, 100000);

	adjusted_vco_clk1 = CURVE2_MULTIPLIER *
			    DIV_ROUND_DOWN_ULL(scaled_vco_div_refclk1, (curve_0_interpolated *
			    DIV_ROUND_DOWN_ULL(curve_1_interpolated, CURVE0_MULTIPLIER)));

	ana_cp_int_temp =
		DIV_ROUND_CLOSEST_ULL(DIV_ROUND_DOWN_ULL(adjusted_vco_clk1, curve_2_scaled1),
				      CURVE2_MULTIPLIER);

	*ana_cp_int = max(1, min(ana_cp_int_temp, 127));

	curve_2_scaled_int = curve_2_scaled1 * (*ana_cp_int);

	interpolated_product = curve_1_interpolated *
			       (curve_2_scaled_int * DIV_ROUND_DOWN_ULL(curve_0_interpolated,
								      CURVE0_MULTIPLIER));

	scaled_interpolated_sqrt =
			int_sqrt(DIV_ROUND_UP_ULL(interpolated_product, vco_div_refclk_float) *
			DIV_ROUND_DOWN_ULL(1000000000000ULL, 55));

	/* Scale vco_div_refclk for ana_cp_int */
	scaled_vco_div_refclk2 = DIV_ROUND_UP_ULL(vco_div_refclk_float, 1000000);
	adjusted_vco_clk2 = 1460281 * DIV_ROUND_UP_ULL(scaled_interpolated_sqrt *
						       scaled_vco_div_refclk2,
						       curve_1_interpolated);

	*ana_cp_prop = DIV_ROUND_UP_ULL(adjusted_vco_clk2, curve_2_scaled2);
	*ana_cp_prop = max(1, min(*ana_cp_prop, 127));
}

static void compute_hdmi_tmds_pll(u64 pixel_clock, u32 refclk,
				  u32 ref_range,
				  u32 ana_cp_int_gs,
				  u32 ana_cp_prop_gs,
				  const u64 curve_freq_hz[2][8],
				  const u64 curve_0[2][8],
				  const u64 curve_1[2][8],
				  const u64 curve_2[2][8],
				  u32 prescaler_divider,
				  struct pll_output_params *pll_params)
{
	u64 datarate = pixel_clock * 10000;
	u32 ssc_up_spread = 1;
	u32 mpll_div5_en = 1;
	u32 hdmi_div = 1;
	u32 ana_cp_int;
	u32 ana_cp_prop;
	u32 refclk_postscalar = refclk >> prescaler_divider;
	u32 tx_clk_div;
	u64 vco_clk;
	u64 vco_clk_do_div;
	u32 vco_div_refclk_integer;
	u32 vco_div_refclk_fracn;
	u32 fracn_quot;
	u32 fracn_rem;
	u32 fracn_den;
	u32 fracn_en;
	u32 pmix_en;
	u32 multiplier;
	int mpll_ana_v2i;
	int ana_freq_vco = 0;
	int c, a = 0;
	int i;

	/* Select appropriate v2i point */
	if (datarate <= INTEL_SNPS_PHY_HDMI_9999MHZ) {
		mpll_ana_v2i = 2;
		tx_clk_div = ilog2(DIV_ROUND_DOWN_ULL(INTEL_SNPS_PHY_HDMI_9999MHZ, datarate));
	} else {
		mpll_ana_v2i = 3;
		tx_clk_div = ilog2(DIV_ROUND_DOWN_ULL(INTEL_SNPS_PHY_HDMI_16GHZ, datarate));
	}
	vco_clk = (datarate << tx_clk_div) >> 1;

	vco_div_refclk_integer = DIV_ROUND_DOWN_ULL(vco_clk, refclk_postscalar);
	vco_clk_do_div = do_div(vco_clk, refclk_postscalar);
	vco_div_refclk_fracn = DIV_ROUND_DOWN_ULL(vco_clk_do_div << 32, refclk_postscalar);

	fracn_quot = vco_div_refclk_fracn >> 16;
	fracn_rem = vco_div_refclk_fracn & 0xffff;
	fracn_rem = fracn_rem - (fracn_rem >> 15);
	fracn_den = 0xffff;
	fracn_en = (fracn_quot != 0 || fracn_rem != 0) ? 1 : 0;
	pmix_en = fracn_en;
	multiplier = (vco_div_refclk_integer - 16) * 2;
	/* Curve selection for ana_cp_* calculations. One curve hardcoded per v2i range */
	c = mpll_ana_v2i - 2;

	/* Find the right segment of the table */
	for (i = 0; i < 8; i += 2) {
		if (vco_clk <= curve_freq_hz[c][i + 1]) {
			a = i;
			ana_freq_vco = 3 - (a >> 1);
			break;
		}
	}

	get_ana_cp_int_prop(vco_clk, refclk_postscalar, mpll_ana_v2i, c, a,
			    curve_freq_hz, curve_0, curve_1, curve_2,
			    &ana_cp_int, &ana_cp_prop);

	pll_params->ssc_up_spread = ssc_up_spread;
	pll_params->mpll_div5_en = mpll_div5_en;
	pll_params->hdmi_div = hdmi_div;
	pll_params->ana_cp_int = ana_cp_int;
	pll_params->refclk_postscalar = refclk_postscalar;
	pll_params->tx_clk_div = tx_clk_div;
	pll_params->fracn_quot = fracn_quot;
	pll_params->fracn_rem = fracn_rem;
	pll_params->fracn_den = fracn_den;
	pll_params->fracn_en = fracn_en;
	pll_params->pmix_en = pmix_en;
	pll_params->multiplier = multiplier;
	pll_params->ana_cp_prop = ana_cp_prop;
	pll_params->mpll_ana_v2i = mpll_ana_v2i;
	pll_params->ana_freq_vco = ana_freq_vco;
}

void intel_snps_hdmi_pll_compute_mpllb(struct intel_mpllb_state *pll_state, u64 pixel_clock)
{
	/* x axis frequencies. One curve in each array per v2i point */
	static const u64 dg2_curve_freq_hz[2][8] = {
		{ 2500000000ULL, 3000000000ULL, 3000000000ULL, 3500000000ULL, 3500000000ULL,
		  4000000000ULL, 4000000000ULL, 5000000000ULL },
		{ 4000000000ULL, 4600000000ULL, 4601000000ULL, 5400000000ULL, 5401000000ULL,
		  6600000000ULL, 6601000000ULL, 8001000000ULL }
	};

	/* y axis heights multiplied with 1000000000 */
	static const u64 dg2_curve_0[2][8] = {
		{ 34149871, 39803269, 36034544, 40601014, 35646940, 40016109, 35127987, 41889522 },
		{ 70000000, 78770454, 70451838, 80427119, 70991400, 84230173, 72945921, 87064218 }
	};

	/* Multiplied with 100 */
	static const u64 dg2_curve_1[2][8] = {
		{ 85177000000000ULL, 79385227160000ULL, 95672603580000ULL, 88857207160000ULL,
		  109379790900000ULL, 103528193900000ULL, 131941242400000ULL, 117279000000000ULL },
		{ 60255000000000ULL, 55569000000000ULL, 72036000000000ULL, 69509000000000ULL,
		  81785000000000ULL, 731030000000000ULL, 96591000000000ULL, 69077000000000ULL }
	};

	/* Multiplied with 1000000000000 */
	static const u64 dg2_curve_2[2][8] = {
		{ 2186930000ULL, 2835287134ULL, 2395395343ULL, 2932270687ULL, 2351887545ULL,
		  2861031697ULL, 2294149152ULL, 3091730000ULL },
		{ 4560000000ULL, 5570000000ULL, 4610000000ULL, 5770000000ULL, 4670000000ULL,
		  6240000000ULL, 4890000000ULL, 6600000000ULL }
	};

	struct pll_output_params pll_params;
	u32 refclk = 100000000;
	u32 prescaler_divider = 1;
	u32 ref_range = 3;
	u32 ana_cp_int_gs = 64;
	u32 ana_cp_prop_gs = 124;

	compute_hdmi_tmds_pll(pixel_clock, refclk, ref_range, ana_cp_int_gs, ana_cp_prop_gs,
			      dg2_curve_freq_hz, dg2_curve_0, dg2_curve_1, dg2_curve_2,
			      prescaler_divider, &pll_params);

	pll_state->clock = pixel_clock;
	pll_state->ref_control =
		REG_FIELD_PREP(SNPS_PHY_REF_CONTROL_REF_RANGE, ref_range);
	pll_state->mpllb_cp =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT, pll_params.ana_cp_int) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP, pll_params.ana_cp_prop) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_INT_GS, ana_cp_int_gs) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_CP_PROP_GS, ana_cp_prop_gs);
	pll_state->mpllb_div =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_DIV5_CLK_EN, pll_params.mpll_div5_en) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_TX_CLK_DIV, pll_params.tx_clk_div) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_PMIX_EN, pll_params.pmix_en) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_V2I, pll_params.mpll_ana_v2i) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FREQ_VCO, pll_params.ana_freq_vco);
	pll_state->mpllb_div2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_REF_CLK_DIV, prescaler_divider) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_MULTIPLIER, pll_params.multiplier) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_HDMI_DIV, pll_params.hdmi_div);
	pll_state->mpllb_fracn1 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_CGG_UPDATE_EN, 1) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_EN, pll_params.fracn_en) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_DEN, pll_params.fracn_den);
	pll_state->mpllb_fracn2 =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_QUOT, pll_params.fracn_quot) |
		REG_FIELD_PREP(SNPS_PHY_MPLLB_FRACN_REM, pll_params.fracn_rem);
	pll_state->mpllb_sscen =
		REG_FIELD_PREP(SNPS_PHY_MPLLB_SSC_UP_SPREAD, pll_params.ssc_up_spread);
}

void intel_snps_hdmi_pll_compute_c10pll(struct intel_c10pll_state *pll_state, u64 pixel_clock)
{
	/* x axis frequencies. One curve in each array per v2i point */
	static const u64 c10_curve_freq_hz[2][8] = {
		{ 2500000000ULL, 3000000000ULL, 3000000000ULL, 3500000000ULL, 3500000000ULL,
		  4000000000ULL, 4000000000ULL, 5000000000ULL },
		{ 4000000000ULL, 4600000000ULL, 4601000000ULL, 5400000000ULL, 5401000000ULL,
		  6600000000ULL, 6601000000ULL, 8001000000ULL }
	};

	/* y axis heights multiplied with 1000000000 */
	static const u64 c10_curve_0[2][8] = {
		{ 41174500, 48605500, 42973700, 49433100, 42408600, 47681900, 40297400, 49131400 },
		{ 82056800, 94420700, 82323400, 96370600, 81273300, 98630100, 81728700, 99105700}
	};

	static const u64 c10_curve_1[2][8] = {
		{ 73300000000000ULL, 66000000000000ULL, 83100000000000ULL, 75300000000000ULL,
		  99700000000000ULL, 92300000000000ULL, 125000000000000ULL, 110000000000000ULL },
		{ 53700000000000ULL, 47700000000000ULL, 62200000000000ULL, 54400000000000ULL,
		  75100000000000ULL, 63400000000000ULL, 90600000000000ULL, 76300000000000ULL }
	};

	/* Multiplied with 1000000000000 */
	static const u64 c10_curve_2[2][8] = {
		{ 2415790000ULL, 3136460000ULL, 2581990000ULL, 3222670000ULL, 2529330000ULL,
		  3042020000ULL, 2336970000ULL, 3191460000ULL},
		{ 4808390000ULL, 5994250000ULL, 4832730000ULL, 6193730000ULL, 4737700000ULL,
		  6428750000ULL, 4779200000ULL, 6479340000ULL }
	};

	struct pll_output_params pll_params;
	u32 refclk = 38400000;
	u32 prescaler_divider = 0;
	u32 ref_range = 1;
	u32 ana_cp_int_gs = 30;
	u32 ana_cp_prop_gs = 28;

	compute_hdmi_tmds_pll(pixel_clock, refclk, ref_range,
			      ana_cp_int_gs, ana_cp_prop_gs,
			      c10_curve_freq_hz, c10_curve_0,
			      c10_curve_1, c10_curve_2, prescaler_divider,
			      &pll_params);

	pll_state->tx = 0x10;
	pll_state->cmn = 0x1;
	pll_state->pll[0] = REG_FIELD_PREP(C10_PLL0_DIV5CLK_EN, pll_params.mpll_div5_en) |
			    REG_FIELD_PREP(C10_PLL0_FRACEN, pll_params.fracn_en) |
			    REG_FIELD_PREP(C10_PLL0_PMIX_EN, pll_params.pmix_en) |
			    REG_FIELD_PREP(C10_PLL0_ANA_FREQ_VCO_MASK, pll_params.ana_freq_vco);
	pll_state->pll[2] = REG_FIELD_PREP(C10_PLL2_MULTIPLIERL_MASK, pll_params.multiplier);
	pll_state->pll[3] = REG_FIELD_PREP(C10_PLL3_MULTIPLIERH_MASK, pll_params.multiplier >> 8);
	pll_state->pll[8] = REG_FIELD_PREP(C10_PLL8_SSC_UP_SPREAD, pll_params.ssc_up_spread);
	pll_state->pll[9] = REG_FIELD_PREP(C10_PLL9_FRACN_DENL_MASK, pll_params.fracn_den);
	pll_state->pll[10] = REG_FIELD_PREP(C10_PLL10_FRACN_DENH_MASK, pll_params.fracn_den >> 8);
	pll_state->pll[11] = REG_FIELD_PREP(C10_PLL11_FRACN_QUOT_L_MASK, pll_params.fracn_quot);
	pll_state->pll[12] = REG_FIELD_PREP(C10_PLL12_FRACN_QUOT_H_MASK,
					    pll_params.fracn_quot >> 8);

	pll_state->pll[13] = REG_FIELD_PREP(C10_PLL13_FRACN_REM_L_MASK, pll_params.fracn_rem);
	pll_state->pll[14] = REG_FIELD_PREP(C10_PLL14_FRACN_REM_H_MASK, pll_params.fracn_rem >> 8);
	pll_state->pll[15] = REG_FIELD_PREP(C10_PLL15_TXCLKDIV_MASK, pll_params.tx_clk_div) |
			     REG_FIELD_PREP(C10_PLL15_HDMIDIV_MASK, pll_params.hdmi_div);
	pll_state->pll[16] = REG_FIELD_PREP(C10_PLL16_ANA_CPINT, pll_params.ana_cp_int) |
			     REG_FIELD_PREP(C10_PLL16_ANA_CPINTGS_L, ana_cp_int_gs);
	pll_state->pll[17] = REG_FIELD_PREP(C10_PLL17_ANA_CPINTGS_H_MASK, ana_cp_int_gs >> 1) |
			     REG_FIELD_PREP(C10_PLL17_ANA_CPPROP_L_MASK, pll_params.ana_cp_prop);
	pll_state->pll[18] =
			REG_FIELD_PREP(C10_PLL18_ANA_CPPROP_H_MASK, pll_params.ana_cp_prop >> 2) |
			REG_FIELD_PREP(C10_PLL18_ANA_CPPROPGS_L_MASK, ana_cp_prop_gs);

	pll_state->pll[19] = REG_FIELD_PREP(C10_PLL19_ANA_CPPROPGS_H_MASK, ana_cp_prop_gs >> 3) |
			     REG_FIELD_PREP(C10_PLL19_ANA_V2I_MASK, pll_params.mpll_ana_v2i);
}
