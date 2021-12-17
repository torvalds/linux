// SPDX-License-Identifier: GPL-2.0
#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/slab.h>

#include <dt-bindings/clock/at91.h>

#include "pmc.h"

static DEFINE_SPINLOCK(pmc_pll_lock);
static DEFINE_SPINLOCK(mck_lock);

static const struct clk_master_characteristics mck_characteristics = {
	.output = { .min = 140000000, .max = 200000000 },
	.divisors = { 1, 2, 4, 3 },
	.have_div3_pres = 1,
};

static const struct clk_master_layout sam9x60_master_layout = {
	.mask = 0x373,
	.pres_shift = 4,
	.offset = 0x28,
};

static const struct clk_range plla_outputs[] = {
	{ .min = 2343750, .max = 1200000000 },
};

static const struct clk_pll_characteristics plla_characteristics = {
	.input = { .min = 12000000, .max = 48000000 },
	.num_output = ARRAY_SIZE(plla_outputs),
	.output = plla_outputs,
};

static const struct clk_range upll_outputs[] = {
	{ .min = 300000000, .max = 500000000 },
};

static const struct clk_pll_characteristics upll_characteristics = {
	.input = { .min = 12000000, .max = 48000000 },
	.num_output = ARRAY_SIZE(upll_outputs),
	.output = upll_outputs,
	.upll = true,
};

static const struct clk_pll_layout pll_frac_layout = {
	.mul_mask = GENMASK(31, 24),
	.frac_mask = GENMASK(21, 0),
	.mul_shift = 24,
	.frac_shift = 0,
};

static const struct clk_pll_layout pll_div_layout = {
	.div_mask = GENMASK(7, 0),
	.endiv_mask = BIT(29),
	.div_shift = 0,
	.endiv_shift = 29,
};

static const struct clk_programmable_layout sam9x60_programmable_layout = {
	.pres_mask = 0xff,
	.pres_shift = 8,
	.css_mask = 0x1f,
	.have_slck_mck = 0,
	.is_pres_direct = 1,
};

static const struct clk_pcr_layout sam9x60_pcr_layout = {
	.offset = 0x88,
	.cmd = BIT(31),
	.gckcss_mask = GENMASK(12, 8),
	.pid_mask = GENMASK(6, 0),
};

static const struct {
	char *n;
	char *p;
	u8 id;
} sam9x60_systemck[] = {
	{ .n = "ddrck",  .p = "masterck_div", .id = 2 },
	{ .n = "uhpck",  .p = "usbck",    .id = 6 },
	{ .n = "pck0",   .p = "prog0",    .id = 8 },
	{ .n = "pck1",   .p = "prog1",    .id = 9 },
	{ .n = "qspick", .p = "masterck_div", .id = 19 },
};

static const struct {
	char *n;
	u8 id;
} sam9x60_periphck[] = {
	{ .n = "pioA_clk",   .id = 2, },
	{ .n = "pioB_clk",   .id = 3, },
	{ .n = "pioC_clk",   .id = 4, },
	{ .n = "flex0_clk",  .id = 5, },
	{ .n = "flex1_clk",  .id = 6, },
	{ .n = "flex2_clk",  .id = 7, },
	{ .n = "flex3_clk",  .id = 8, },
	{ .n = "flex6_clk",  .id = 9, },
	{ .n = "flex7_clk",  .id = 10, },
	{ .n = "flex8_clk",  .id = 11, },
	{ .n = "sdmmc0_clk", .id = 12, },
	{ .n = "flex4_clk",  .id = 13, },
	{ .n = "flex5_clk",  .id = 14, },
	{ .n = "flex9_clk",  .id = 15, },
	{ .n = "flex10_clk", .id = 16, },
	{ .n = "tcb0_clk",   .id = 17, },
	{ .n = "pwm_clk",    .id = 18, },
	{ .n = "adc_clk",    .id = 19, },
	{ .n = "dma0_clk",   .id = 20, },
	{ .n = "matrix_clk", .id = 21, },
	{ .n = "uhphs_clk",  .id = 22, },
	{ .n = "udphs_clk",  .id = 23, },
	{ .n = "macb0_clk",  .id = 24, },
	{ .n = "lcd_clk",    .id = 25, },
	{ .n = "sdmmc1_clk", .id = 26, },
	{ .n = "macb1_clk",  .id = 27, },
	{ .n = "ssc_clk",    .id = 28, },
	{ .n = "can0_clk",   .id = 29, },
	{ .n = "can1_clk",   .id = 30, },
	{ .n = "flex11_clk", .id = 32, },
	{ .n = "flex12_clk", .id = 33, },
	{ .n = "i2s_clk",    .id = 34, },
	{ .n = "qspi_clk",   .id = 35, },
	{ .n = "gfx2d_clk",  .id = 36, },
	{ .n = "pit64b_clk", .id = 37, },
	{ .n = "trng_clk",   .id = 38, },
	{ .n = "aes_clk",    .id = 39, },
	{ .n = "tdes_clk",   .id = 40, },
	{ .n = "sha_clk",    .id = 41, },
	{ .n = "classd_clk", .id = 42, },
	{ .n = "isi_clk",    .id = 43, },
	{ .n = "pioD_clk",   .id = 44, },
	{ .n = "tcb1_clk",   .id = 45, },
	{ .n = "dbgu_clk",   .id = 47, },
	{ .n = "mpddr_clk",  .id = 49, },
};

static const struct {
	char *n;
	u8 id;
	struct clk_range r;
} sam9x60_gck[] = {
	{ .n = "flex0_gclk",  .id = 5, },
	{ .n = "flex1_gclk",  .id = 6, },
	{ .n = "flex2_gclk",  .id = 7, },
	{ .n = "flex3_gclk",  .id = 8, },
	{ .n = "flex6_gclk",  .id = 9, },
	{ .n = "flex7_gclk",  .id = 10, },
	{ .n = "flex8_gclk",  .id = 11, },
	{ .n = "sdmmc0_gclk", .id = 12, .r = { .min = 0, .max = 105000000 }, },
	{ .n = "flex4_gclk",  .id = 13, },
	{ .n = "flex5_gclk",  .id = 14, },
	{ .n = "flex9_gclk",  .id = 15, },
	{ .n = "flex10_gclk", .id = 16, },
	{ .n = "tcb0_gclk",   .id = 17, },
	{ .n = "adc_gclk",    .id = 19, },
	{ .n = "lcd_gclk",    .id = 25, .r = { .min = 0, .max = 140000000 }, },
	{ .n = "sdmmc1_gclk", .id = 26, .r = { .min = 0, .max = 105000000 }, },
	{ .n = "flex11_gclk", .id = 32, },
	{ .n = "flex12_gclk", .id = 33, },
	{ .n = "i2s_gclk",    .id = 34, .r = { .min = 0, .max = 105000000 }, },
	{ .n = "pit64b_gclk", .id = 37, },
	{ .n = "classd_gclk", .id = 42, .r = { .min = 0, .max = 100000000 }, },
	{ .n = "tcb1_gclk",   .id = 45, },
	{ .n = "dbgu_gclk",   .id = 47, },
};

static void __init sam9x60_pmc_setup(struct device_node *np)
{
	struct clk_range range = CLK_RANGE(0, 0);
	const char *td_slck_name, *md_slck_name, *mainxtal_name;
	struct pmc_data *sam9x60_pmc;
	const char *parent_names[6];
	struct clk_hw *main_osc_hw;
	struct regmap *regmap;
	struct clk_hw *hw;
	int i;

	i = of_property_match_string(np, "clock-names", "td_slck");
	if (i < 0)
		return;

	td_slck_name = of_clk_get_parent_name(np, i);

	i = of_property_match_string(np, "clock-names", "md_slck");
	if (i < 0)
		return;

	md_slck_name = of_clk_get_parent_name(np, i);

	i = of_property_match_string(np, "clock-names", "main_xtal");
	if (i < 0)
		return;
	mainxtal_name = of_clk_get_parent_name(np, i);

	regmap = device_node_to_regmap(np);
	if (IS_ERR(regmap))
		return;

	sam9x60_pmc = pmc_data_allocate(PMC_PLLACK + 1,
					nck(sam9x60_systemck),
					nck(sam9x60_periphck),
					nck(sam9x60_gck), 8);
	if (!sam9x60_pmc)
		return;

	hw = at91_clk_register_main_rc_osc(regmap, "main_rc_osc", 12000000,
					   50000000);
	if (IS_ERR(hw))
		goto err_free;

	hw = at91_clk_register_main_osc(regmap, "main_osc", mainxtal_name, 0);
	if (IS_ERR(hw))
		goto err_free;
	main_osc_hw = hw;

	parent_names[0] = "main_rc_osc";
	parent_names[1] = "main_osc";
	hw = at91_clk_register_sam9x5_main(regmap, "mainck", parent_names, 2);
	if (IS_ERR(hw))
		goto err_free;

	sam9x60_pmc->chws[PMC_MAIN] = hw;

	hw = sam9x60_clk_register_frac_pll(regmap, &pmc_pll_lock, "pllack_fracck",
					   "mainck", sam9x60_pmc->chws[PMC_MAIN],
					   0, &plla_characteristics,
					   &pll_frac_layout,
					   /*
					    * This feeds pllack_divck which
					    * feeds CPU. It should not be
					    * disabled.
					    */
					   CLK_IS_CRITICAL | CLK_SET_RATE_GATE);
	if (IS_ERR(hw))
		goto err_free;

	hw = sam9x60_clk_register_div_pll(regmap, &pmc_pll_lock, "pllack_divck",
					  "pllack_fracck", 0, &plla_characteristics,
					  &pll_div_layout,
					   /*
					    * This feeds CPU. It should not
					    * be disabled.
					    */
					  CLK_IS_CRITICAL | CLK_SET_RATE_GATE, 0);
	if (IS_ERR(hw))
		goto err_free;

	sam9x60_pmc->chws[PMC_PLLACK] = hw;

	hw = sam9x60_clk_register_frac_pll(regmap, &pmc_pll_lock, "upllck_fracck",
					   "main_osc", main_osc_hw, 1,
					   &upll_characteristics,
					   &pll_frac_layout, CLK_SET_RATE_GATE);
	if (IS_ERR(hw))
		goto err_free;

	hw = sam9x60_clk_register_div_pll(regmap, &pmc_pll_lock, "upllck_divck",
					  "upllck_fracck", 1, &upll_characteristics,
					  &pll_div_layout,
					  CLK_SET_RATE_GATE |
					  CLK_SET_PARENT_GATE |
					  CLK_SET_RATE_PARENT, 0);
	if (IS_ERR(hw))
		goto err_free;

	sam9x60_pmc->chws[PMC_UTMI] = hw;

	parent_names[0] = md_slck_name;
	parent_names[1] = "mainck";
	parent_names[2] = "pllack_divck";
	hw = at91_clk_register_master_pres(regmap, "masterck_pres", 3,
					   parent_names, &sam9x60_master_layout,
					   &mck_characteristics, &mck_lock,
					   CLK_SET_RATE_GATE, INT_MIN);
	if (IS_ERR(hw))
		goto err_free;

	hw = at91_clk_register_master_div(regmap, "masterck_div",
					  "masterck_pres", &sam9x60_master_layout,
					  &mck_characteristics, &mck_lock,
					  CLK_SET_RATE_GATE, 0);
	if (IS_ERR(hw))
		goto err_free;

	sam9x60_pmc->chws[PMC_MCK] = hw;

	parent_names[0] = "pllack_divck";
	parent_names[1] = "upllck_divck";
	parent_names[2] = "main_osc";
	hw = sam9x60_clk_register_usb(regmap, "usbck", parent_names, 3);
	if (IS_ERR(hw))
		goto err_free;

	parent_names[0] = md_slck_name;
	parent_names[1] = td_slck_name;
	parent_names[2] = "mainck";
	parent_names[3] = "masterck_div";
	parent_names[4] = "pllack_divck";
	parent_names[5] = "upllck_divck";
	for (i = 0; i < 2; i++) {
		char name[6];

		snprintf(name, sizeof(name), "prog%d", i);

		hw = at91_clk_register_programmable(regmap, name,
						    parent_names, 6, i,
						    &sam9x60_programmable_layout,
						    NULL);
		if (IS_ERR(hw))
			goto err_free;

		sam9x60_pmc->pchws[i] = hw;
	}

	for (i = 0; i < ARRAY_SIZE(sam9x60_systemck); i++) {
		hw = at91_clk_register_system(regmap, sam9x60_systemck[i].n,
					      sam9x60_systemck[i].p,
					      sam9x60_systemck[i].id);
		if (IS_ERR(hw))
			goto err_free;

		sam9x60_pmc->shws[sam9x60_systemck[i].id] = hw;
	}

	for (i = 0; i < ARRAY_SIZE(sam9x60_periphck); i++) {
		hw = at91_clk_register_sam9x5_peripheral(regmap, &pmc_pcr_lock,
							 &sam9x60_pcr_layout,
							 sam9x60_periphck[i].n,
							 "masterck_div",
							 sam9x60_periphck[i].id,
							 &range, INT_MIN);
		if (IS_ERR(hw))
			goto err_free;

		sam9x60_pmc->phws[sam9x60_periphck[i].id] = hw;
	}

	for (i = 0; i < ARRAY_SIZE(sam9x60_gck); i++) {
		hw = at91_clk_register_generated(regmap, &pmc_pcr_lock,
						 &sam9x60_pcr_layout,
						 sam9x60_gck[i].n,
						 parent_names, NULL, 6,
						 sam9x60_gck[i].id,
						 &sam9x60_gck[i].r, INT_MIN);
		if (IS_ERR(hw))
			goto err_free;

		sam9x60_pmc->ghws[sam9x60_gck[i].id] = hw;
	}

	of_clk_add_hw_provider(np, of_clk_hw_pmc_get, sam9x60_pmc);

	return;

err_free:
	kfree(sam9x60_pmc);
}
/* Some clks are used for a clocksource */
CLK_OF_DECLARE(sam9x60_pmc, "microchip,sam9x60-pmc", sam9x60_pmc_setup);
