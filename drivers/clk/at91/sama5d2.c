// SPDX-License-Identifier: GPL-2.0
#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/slab.h>

#include <dt-bindings/clock/at91.h>

#include "pmc.h"

static const struct clk_master_characteristics mck_characteristics = {
	.output = { .min = 124000000, .max = 166000000 },
	.divisors = { 1, 2, 4, 3 },
};

static u8 plla_out[] = { 0 };

static u16 plla_icpll[] = { 0 };

static struct clk_range plla_outputs[] = {
	{ .min = 600000000, .max = 1200000000 },
};

static const struct clk_pll_characteristics plla_characteristics = {
	.input = { .min = 12000000, .max = 12000000 },
	.num_output = ARRAY_SIZE(plla_outputs),
	.output = plla_outputs,
	.icpll = plla_icpll,
	.out = plla_out,
};

static const struct {
	char *n;
	char *p;
	u8 id;
} sama5d2_systemck[] = {
	{ .n = "ddrck", .p = "masterck", .id = 2 },
	{ .n = "lcdck", .p = "masterck", .id = 3 },
	{ .n = "uhpck", .p = "usbck",    .id = 6 },
	{ .n = "udpck", .p = "usbck",    .id = 7 },
	{ .n = "pck0",  .p = "prog0",    .id = 8 },
	{ .n = "pck1",  .p = "prog1",    .id = 9 },
	{ .n = "pck2",  .p = "prog2",    .id = 10 },
	{ .n = "iscck", .p = "masterck", .id = 18 },
};

static const struct {
	char *n;
	u8 id;
	struct clk_range r;
} sama5d2_periph32ck[] = {
	{ .n = "macb0_clk",   .id = 5,  .r = { .min = 0, .max = 83000000 }, },
	{ .n = "tdes_clk",    .id = 11, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "matrix1_clk", .id = 14, },
	{ .n = "hsmc_clk",    .id = 17, },
	{ .n = "pioA_clk",    .id = 18, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "flx0_clk",    .id = 19, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "flx1_clk",    .id = 20, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "flx2_clk",    .id = 21, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "flx3_clk",    .id = 22, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "flx4_clk",    .id = 23, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "uart0_clk",   .id = 24, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "uart1_clk",   .id = 25, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "uart2_clk",   .id = 26, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "uart3_clk",   .id = 27, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "uart4_clk",   .id = 28, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "twi0_clk",    .id = 29, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "twi1_clk",    .id = 30, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "spi0_clk",    .id = 33, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "spi1_clk",    .id = 34, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "tcb0_clk",    .id = 35, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "tcb1_clk",    .id = 36, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "pwm_clk",     .id = 38, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "adc_clk",     .id = 40, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "uhphs_clk",   .id = 41, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "udphs_clk",   .id = 42, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "ssc0_clk",    .id = 43, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "ssc1_clk",    .id = 44, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "trng_clk",    .id = 47, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "pdmic_clk",   .id = 48, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "securam_clk", .id = 51, },
	{ .n = "i2s0_clk",    .id = 54, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "i2s1_clk",    .id = 55, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "can0_clk",    .id = 56, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "can1_clk",    .id = 57, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "classd_clk",  .id = 59, .r = { .min = 0, .max = 83000000 }, },
};

static const struct {
	char *n;
	u8 id;
} sama5d2_periphck[] = {
	{ .n = "dma0_clk",    .id = 6, },
	{ .n = "dma1_clk",    .id = 7, },
	{ .n = "aes_clk",     .id = 9, },
	{ .n = "aesb_clk",    .id = 10, },
	{ .n = "sha_clk",     .id = 12, },
	{ .n = "mpddr_clk",   .id = 13, },
	{ .n = "matrix0_clk", .id = 15, },
	{ .n = "sdmmc0_hclk", .id = 31, },
	{ .n = "sdmmc1_hclk", .id = 32, },
	{ .n = "lcdc_clk",    .id = 45, },
	{ .n = "isc_clk",     .id = 46, },
	{ .n = "qspi0_clk",   .id = 52, },
	{ .n = "qspi1_clk",   .id = 53, },
};

static const struct {
	char *n;
	u8 id;
	struct clk_range r;
	bool pll;
} sama5d2_gck[] = {
	{ .n = "sdmmc0_gclk", .id = 31, },
	{ .n = "sdmmc1_gclk", .id = 32, },
	{ .n = "tcb0_gclk",   .id = 35, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "tcb1_gclk",   .id = 36, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "pwm_gclk",    .id = 38, .r = { .min = 0, .max = 83000000 }, },
	{ .n = "isc_gclk",    .id = 46, },
	{ .n = "pdmic_gclk",  .id = 48, },
	{ .n = "i2s0_gclk",   .id = 54, .pll = true },
	{ .n = "i2s1_gclk",   .id = 55, .pll = true },
	{ .n = "can0_gclk",   .id = 56, .r = { .min = 0, .max = 80000000 }, },
	{ .n = "can1_gclk",   .id = 57, .r = { .min = 0, .max = 80000000 }, },
	{ .n = "classd_gclk", .id = 59, .r = { .min = 0, .max = 100000000 },
	  .pll = true },
};

static void __init sama5d2_pmc_setup(struct device_node *np)
{
	struct clk_range range = CLK_RANGE(0, 0);
	const char *slck_name, *mainxtal_name;
	struct pmc_data *sama5d2_pmc;
	const char *parent_names[6];
	struct regmap *regmap, *regmap_sfr;
	struct clk_hw *hw;
	int i;
	bool bypass;

	i = of_property_match_string(np, "clock-names", "slow_clk");
	if (i < 0)
		return;

	slck_name = of_clk_get_parent_name(np, i);

	i = of_property_match_string(np, "clock-names", "main_xtal");
	if (i < 0)
		return;
	mainxtal_name = of_clk_get_parent_name(np, i);

	regmap = syscon_node_to_regmap(np);
	if (IS_ERR(regmap))
		return;

	sama5d2_pmc = pmc_data_allocate(PMC_I2S1_MUX + 1,
					nck(sama5d2_systemck),
					nck(sama5d2_periph32ck),
					nck(sama5d2_gck));
	if (!sama5d2_pmc)
		return;

	hw = at91_clk_register_main_rc_osc(regmap, "main_rc_osc", 12000000,
					   100000000);
	if (IS_ERR(hw))
		goto err_free;

	bypass = of_property_read_bool(np, "atmel,osc-bypass");

	hw = at91_clk_register_main_osc(regmap, "main_osc", mainxtal_name,
					bypass);
	if (IS_ERR(hw))
		goto err_free;

	parent_names[0] = "main_rc_osc";
	parent_names[1] = "main_osc";
	hw = at91_clk_register_sam9x5_main(regmap, "mainck", parent_names, 2);
	if (IS_ERR(hw))
		goto err_free;

	sama5d2_pmc->chws[PMC_MAIN] = hw;

	hw = at91_clk_register_pll(regmap, "pllack", "mainck", 0,
				   &sama5d3_pll_layout, &plla_characteristics);
	if (IS_ERR(hw))
		goto err_free;

	hw = at91_clk_register_plldiv(regmap, "plladivck", "pllack");
	if (IS_ERR(hw))
		goto err_free;

	hw = at91_clk_register_audio_pll_frac(regmap, "audiopll_fracck",
					      "mainck");
	if (IS_ERR(hw))
		goto err_free;

	hw = at91_clk_register_audio_pll_pad(regmap, "audiopll_padck",
					     "audiopll_fracck");
	if (IS_ERR(hw))
		goto err_free;

	hw = at91_clk_register_audio_pll_pmc(regmap, "audiopll_pmcck",
					     "audiopll_fracck");
	if (IS_ERR(hw))
		goto err_free;

	regmap_sfr = syscon_regmap_lookup_by_compatible("atmel,sama5d2-sfr");
	if (IS_ERR(regmap_sfr))
		regmap_sfr = NULL;

	hw = at91_clk_register_utmi(regmap, regmap_sfr, "utmick", "mainck");
	if (IS_ERR(hw))
		goto err_free;

	sama5d2_pmc->chws[PMC_UTMI] = hw;

	parent_names[0] = slck_name;
	parent_names[1] = "mainck";
	parent_names[2] = "plladivck";
	parent_names[3] = "utmick";
	hw = at91_clk_register_master(regmap, "masterck", 4, parent_names,
				      &at91sam9x5_master_layout,
				      &mck_characteristics);
	if (IS_ERR(hw))
		goto err_free;

	sama5d2_pmc->chws[PMC_MCK] = hw;

	hw = at91_clk_register_h32mx(regmap, "h32mxck", "masterck");
	if (IS_ERR(hw))
		goto err_free;

	sama5d2_pmc->chws[PMC_MCK2] = hw;

	parent_names[0] = "plladivck";
	parent_names[1] = "utmick";
	hw = at91sam9x5_clk_register_usb(regmap, "usbck", parent_names, 2);
	if (IS_ERR(hw))
		goto err_free;

	parent_names[0] = slck_name;
	parent_names[1] = "mainck";
	parent_names[2] = "plladivck";
	parent_names[3] = "utmick";
	parent_names[4] = "masterck";
	parent_names[5] = "audiopll_pmcck";
	for (i = 0; i < 3; i++) {
		char name[6];

		snprintf(name, sizeof(name), "prog%d", i);

		hw = at91_clk_register_programmable(regmap, name,
						    parent_names, 6, i,
						    &at91sam9x5_programmable_layout);
		if (IS_ERR(hw))
			goto err_free;
	}

	for (i = 0; i < ARRAY_SIZE(sama5d2_systemck); i++) {
		hw = at91_clk_register_system(regmap, sama5d2_systemck[i].n,
					      sama5d2_systemck[i].p,
					      sama5d2_systemck[i].id);
		if (IS_ERR(hw))
			goto err_free;

		sama5d2_pmc->shws[sama5d2_systemck[i].id] = hw;
	}

	for (i = 0; i < ARRAY_SIZE(sama5d2_periphck); i++) {
		hw = at91_clk_register_sam9x5_peripheral(regmap, &pmc_pcr_lock,
							 sama5d2_periphck[i].n,
							 "masterck",
							 sama5d2_periphck[i].id,
							 &range);
		if (IS_ERR(hw))
			goto err_free;

		sama5d2_pmc->phws[sama5d2_periphck[i].id] = hw;
	}

	for (i = 0; i < ARRAY_SIZE(sama5d2_periph32ck); i++) {
		hw = at91_clk_register_sam9x5_peripheral(regmap, &pmc_pcr_lock,
							 sama5d2_periph32ck[i].n,
							 "h32mxck",
							 sama5d2_periph32ck[i].id,
							 &sama5d2_periph32ck[i].r);
		if (IS_ERR(hw))
			goto err_free;

		sama5d2_pmc->phws[sama5d2_periph32ck[i].id] = hw;
	}

	parent_names[0] = slck_name;
	parent_names[1] = "mainck";
	parent_names[2] = "plladivck";
	parent_names[3] = "utmick";
	parent_names[4] = "masterck";
	parent_names[5] = "audiopll_pmcck";
	for (i = 0; i < ARRAY_SIZE(sama5d2_gck); i++) {
		hw = at91_clk_register_generated(regmap, &pmc_pcr_lock,
						 sama5d2_gck[i].n,
						 parent_names, 6,
						 sama5d2_gck[i].id,
						 sama5d2_gck[i].pll,
						 &sama5d2_gck[i].r);
		if (IS_ERR(hw))
			goto err_free;

		sama5d2_pmc->ghws[sama5d2_gck[i].id] = hw;
	}

	if (regmap_sfr) {
		parent_names[0] = "i2s0_clk";
		parent_names[1] = "i2s0_gclk";
		hw = at91_clk_i2s_mux_register(regmap_sfr, "i2s0_muxclk",
					       parent_names, 2, 0);
		if (IS_ERR(hw))
			goto err_free;

		sama5d2_pmc->chws[PMC_I2S0_MUX] = hw;

		parent_names[0] = "i2s1_clk";
		parent_names[1] = "i2s1_gclk";
		hw = at91_clk_i2s_mux_register(regmap_sfr, "i2s1_muxclk",
					       parent_names, 2, 1);
		if (IS_ERR(hw))
			goto err_free;

		sama5d2_pmc->chws[PMC_I2S1_MUX] = hw;
	}

	of_clk_add_hw_provider(np, of_clk_hw_pmc_get, sama5d2_pmc);

	return;

err_free:
	pmc_data_free(sama5d2_pmc);
}
CLK_OF_DECLARE_DRIVER(sama5d2_pmc, "atmel,sama5d2-pmc", sama5d2_pmc_setup);
