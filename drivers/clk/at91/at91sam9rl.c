// SPDX-License-Identifier: GPL-2.0
#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/slab.h>

#include <dt-bindings/clock/at91.h>

#include "pmc.h"

static DEFINE_SPINLOCK(sam9rl_mck_lock);

static const struct clk_master_characteristics sam9rl_mck_characteristics = {
	.output = { .min = 0, .max = 94000000 },
	.divisors = { 1, 2, 4, 0 },
};

static u8 sam9rl_plla_out[] = { 0, 2 };

static const struct clk_range sam9rl_plla_outputs[] = {
	{ .min = 80000000, .max = 200000000 },
	{ .min = 190000000, .max = 240000000 },
};

static const struct clk_pll_characteristics sam9rl_plla_characteristics = {
	.input = { .min = 1000000, .max = 32000000 },
	.num_output = ARRAY_SIZE(sam9rl_plla_outputs),
	.output = sam9rl_plla_outputs,
	.out = sam9rl_plla_out,
};

static const struct {
	char *n;
	char *p;
	u8 id;
} at91sam9rl_systemck[] = {
	{ .n = "pck0",  .p = "prog0",    .id = 8 },
	{ .n = "pck1",  .p = "prog1",    .id = 9 },
};

static const struct {
	char *n;
	u8 id;
} at91sam9rl_periphck[] = {
	{ .n = "pioA_clk",   .id = 2, },
	{ .n = "pioB_clk",   .id = 3, },
	{ .n = "pioC_clk",   .id = 4, },
	{ .n = "pioD_clk",   .id = 5, },
	{ .n = "usart0_clk", .id = 6, },
	{ .n = "usart1_clk", .id = 7, },
	{ .n = "usart2_clk", .id = 8, },
	{ .n = "usart3_clk", .id = 9, },
	{ .n = "mci0_clk",   .id = 10, },
	{ .n = "twi0_clk",   .id = 11, },
	{ .n = "twi1_clk",   .id = 12, },
	{ .n = "spi0_clk",   .id = 13, },
	{ .n = "ssc0_clk",   .id = 14, },
	{ .n = "ssc1_clk",   .id = 15, },
	{ .n = "tc0_clk",    .id = 16, },
	{ .n = "tc1_clk",    .id = 17, },
	{ .n = "tc2_clk",    .id = 18, },
	{ .n = "pwm_clk",    .id = 19, },
	{ .n = "adc_clk",    .id = 20, },
	{ .n = "dma0_clk",   .id = 21, },
	{ .n = "udphs_clk",  .id = 22, },
	{ .n = "lcd_clk",    .id = 23, },
};

static void __init at91sam9rl_pmc_setup(struct device_node *np)
{
	const char *slck_name, *mainxtal_name;
	struct pmc_data *at91sam9rl_pmc;
	const char *parent_names[6];
	struct regmap *regmap;
	struct clk_hw *hw;
	int i;

	i = of_property_match_string(np, "clock-names", "slow_clk");
	if (i < 0)
		return;

	slck_name = of_clk_get_parent_name(np, i);

	i = of_property_match_string(np, "clock-names", "main_xtal");
	if (i < 0)
		return;
	mainxtal_name = of_clk_get_parent_name(np, i);

	regmap = device_node_to_regmap(np);
	if (IS_ERR(regmap))
		return;

	at91sam9rl_pmc = pmc_data_allocate(PMC_PLLACK + 1,
					   nck(at91sam9rl_systemck),
					   nck(at91sam9rl_periphck), 0, 2);
	if (!at91sam9rl_pmc)
		return;

	hw = at91_clk_register_rm9200_main(regmap, "mainck", mainxtal_name);
	if (IS_ERR(hw))
		goto err_free;

	at91sam9rl_pmc->chws[PMC_MAIN] = hw;

	hw = at91_clk_register_pll(regmap, "pllack", "mainck", 0,
				   &at91rm9200_pll_layout,
				   &sam9rl_plla_characteristics);
	if (IS_ERR(hw))
		goto err_free;

	at91sam9rl_pmc->chws[PMC_PLLACK] = hw;

	hw = at91_clk_register_utmi(regmap, NULL, "utmick", "mainck");
	if (IS_ERR(hw))
		goto err_free;

	at91sam9rl_pmc->chws[PMC_UTMI] = hw;

	parent_names[0] = slck_name;
	parent_names[1] = "mainck";
	parent_names[2] = "pllack";
	parent_names[3] = "utmick";
	hw = at91_clk_register_master_pres(regmap, "masterck_pres", 4,
					   parent_names,
					   &at91rm9200_master_layout,
					   &sam9rl_mck_characteristics,
					   &sam9rl_mck_lock, CLK_SET_RATE_GATE,
					   INT_MIN);
	if (IS_ERR(hw))
		goto err_free;

	hw = at91_clk_register_master_div(regmap, "masterck_div",
					  "masterck_pres",
					  &at91rm9200_master_layout,
					  &sam9rl_mck_characteristics,
					  &sam9rl_mck_lock, CLK_SET_RATE_GATE);
	if (IS_ERR(hw))
		goto err_free;

	at91sam9rl_pmc->chws[PMC_MCK] = hw;

	parent_names[0] = slck_name;
	parent_names[1] = "mainck";
	parent_names[2] = "pllack";
	parent_names[3] = "utmick";
	parent_names[4] = "masterck_div";
	for (i = 0; i < 2; i++) {
		char name[6];

		snprintf(name, sizeof(name), "prog%d", i);

		hw = at91_clk_register_programmable(regmap, name,
						    parent_names, 5, i,
						    &at91rm9200_programmable_layout,
						    NULL);
		if (IS_ERR(hw))
			goto err_free;

		at91sam9rl_pmc->pchws[i] = hw;
	}

	for (i = 0; i < ARRAY_SIZE(at91sam9rl_systemck); i++) {
		hw = at91_clk_register_system(regmap, at91sam9rl_systemck[i].n,
					      at91sam9rl_systemck[i].p,
					      at91sam9rl_systemck[i].id);
		if (IS_ERR(hw))
			goto err_free;

		at91sam9rl_pmc->shws[at91sam9rl_systemck[i].id] = hw;
	}

	for (i = 0; i < ARRAY_SIZE(at91sam9rl_periphck); i++) {
		hw = at91_clk_register_peripheral(regmap,
						  at91sam9rl_periphck[i].n,
						  "masterck_div",
						  at91sam9rl_periphck[i].id);
		if (IS_ERR(hw))
			goto err_free;

		at91sam9rl_pmc->phws[at91sam9rl_periphck[i].id] = hw;
	}

	of_clk_add_hw_provider(np, of_clk_hw_pmc_get, at91sam9rl_pmc);

	return;

err_free:
	kfree(at91sam9rl_pmc);
}
CLK_OF_DECLARE_DRIVER(at91sam9rl_pmc, "atmel,at91sam9rl-pmc", at91sam9rl_pmc_setup);
