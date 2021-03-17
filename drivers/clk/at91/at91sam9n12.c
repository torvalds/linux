// SPDX-License-Identifier: GPL-2.0
#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/slab.h>

#include <dt-bindings/clock/at91.h>

#include "pmc.h"

static const struct clk_master_characteristics mck_characteristics = {
	.output = { .min = 0, .max = 133333333 },
	.divisors = { 1, 2, 4, 3 },
	.have_div3_pres = 1,
};

static u8 plla_out[] = { 0, 1, 2, 3, 0, 1, 2, 3 };

static u16 plla_icpll[] = { 0, 0, 0, 0, 1, 1, 1, 1 };

static const struct clk_range plla_outputs[] = {
	{ .min = 745000000, .max = 800000000 },
	{ .min = 695000000, .max = 750000000 },
	{ .min = 645000000, .max = 700000000 },
	{ .min = 595000000, .max = 650000000 },
	{ .min = 545000000, .max = 600000000 },
	{ .min = 495000000, .max = 555000000 },
	{ .min = 445000000, .max = 500000000 },
	{ .min = 400000000, .max = 450000000 },
};

static const struct clk_pll_characteristics plla_characteristics = {
	.input = { .min = 2000000, .max = 32000000 },
	.num_output = ARRAY_SIZE(plla_outputs),
	.output = plla_outputs,
	.icpll = plla_icpll,
	.out = plla_out,
};

static u8 pllb_out[] = { 0 };

static const struct clk_range pllb_outputs[] = {
	{ .min = 30000000, .max = 100000000 },
};

static const struct clk_pll_characteristics pllb_characteristics = {
	.input = { .min = 2000000, .max = 32000000 },
	.num_output = ARRAY_SIZE(pllb_outputs),
	.output = pllb_outputs,
	.out = pllb_out,
};

static const struct {
	char *n;
	char *p;
	u8 id;
} at91sam9n12_systemck[] = {
	{ .n = "ddrck", .p = "masterck", .id = 2 },
	{ .n = "lcdck", .p = "masterck", .id = 3 },
	{ .n = "uhpck", .p = "usbck",    .id = 6 },
	{ .n = "udpck", .p = "usbck",    .id = 7 },
	{ .n = "pck0",  .p = "prog0",    .id = 8 },
	{ .n = "pck1",  .p = "prog1",    .id = 9 },
};

static const struct clk_pcr_layout at91sam9n12_pcr_layout = {
	.offset = 0x10c,
	.cmd = BIT(12),
	.pid_mask = GENMASK(5, 0),
	.div_mask = GENMASK(17, 16),
};

struct pck {
	char *n;
	u8 id;
};

static const struct pck at91sam9n12_periphck[] = {
	{ .n = "pioAB_clk",  .id = 2, },
	{ .n = "pioCD_clk",  .id = 3, },
	{ .n = "fuse_clk",   .id = 4, },
	{ .n = "usart0_clk", .id = 5, },
	{ .n = "usart1_clk", .id = 6, },
	{ .n = "usart2_clk", .id = 7, },
	{ .n = "usart3_clk", .id = 8, },
	{ .n = "twi0_clk",   .id = 9, },
	{ .n = "twi1_clk",   .id = 10, },
	{ .n = "mci0_clk",   .id = 12, },
	{ .n = "spi0_clk",   .id = 13, },
	{ .n = "spi1_clk",   .id = 14, },
	{ .n = "uart0_clk",  .id = 15, },
	{ .n = "uart1_clk",  .id = 16, },
	{ .n = "tcb_clk",    .id = 17, },
	{ .n = "pwm_clk",    .id = 18, },
	{ .n = "adc_clk",    .id = 19, },
	{ .n = "dma0_clk",   .id = 20, },
	{ .n = "uhphs_clk",  .id = 22, },
	{ .n = "udphs_clk",  .id = 23, },
	{ .n = "lcdc_clk",   .id = 25, },
	{ .n = "sha_clk",    .id = 27, },
	{ .n = "ssc0_clk",   .id = 28, },
	{ .n = "aes_clk",    .id = 29, },
	{ .n = "trng_clk",   .id = 30, },
};

static void __init at91sam9n12_pmc_setup(struct device_node *np)
{
	struct clk_range range = CLK_RANGE(0, 0);
	const char *slck_name, *mainxtal_name;
	struct pmc_data *at91sam9n12_pmc;
	const char *parent_names[6];
	struct regmap *regmap;
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

	regmap = device_node_to_regmap(np);
	if (IS_ERR(regmap))
		return;

	at91sam9n12_pmc = pmc_data_allocate(PMC_PLLBCK + 1,
					   nck(at91sam9n12_systemck), 31, 0, 2);
	if (!at91sam9n12_pmc)
		return;

	hw = at91_clk_register_main_rc_osc(regmap, "main_rc_osc", 12000000,
					   50000000);
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

	at91sam9n12_pmc->chws[PMC_MAIN] = hw;

	hw = at91_clk_register_pll(regmap, "pllack", "mainck", 0,
				   &at91rm9200_pll_layout, &plla_characteristics);
	if (IS_ERR(hw))
		goto err_free;

	hw = at91_clk_register_plldiv(regmap, "plladivck", "pllack");
	if (IS_ERR(hw))
		goto err_free;

	at91sam9n12_pmc->chws[PMC_PLLACK] = hw;

	hw = at91_clk_register_pll(regmap, "pllbck", "mainck", 1,
				   &at91rm9200_pll_layout, &pllb_characteristics);
	if (IS_ERR(hw))
		goto err_free;

	at91sam9n12_pmc->chws[PMC_PLLBCK] = hw;

	parent_names[0] = slck_name;
	parent_names[1] = "mainck";
	parent_names[2] = "plladivck";
	parent_names[3] = "pllbck";
	hw = at91_clk_register_master(regmap, "masterck", 4, parent_names,
				      &at91sam9x5_master_layout,
				      &mck_characteristics);
	if (IS_ERR(hw))
		goto err_free;

	at91sam9n12_pmc->chws[PMC_MCK] = hw;

	hw = at91sam9n12_clk_register_usb(regmap, "usbck", "pllbck");
	if (IS_ERR(hw))
		goto err_free;

	parent_names[0] = slck_name;
	parent_names[1] = "mainck";
	parent_names[2] = "plladivck";
	parent_names[3] = "pllbck";
	parent_names[4] = "masterck";
	for (i = 0; i < 2; i++) {
		char name[6];

		snprintf(name, sizeof(name), "prog%d", i);

		hw = at91_clk_register_programmable(regmap, name,
						    parent_names, 5, i,
						    &at91sam9x5_programmable_layout,
						    NULL);
		if (IS_ERR(hw))
			goto err_free;

		at91sam9n12_pmc->pchws[i] = hw;
	}

	for (i = 0; i < ARRAY_SIZE(at91sam9n12_systemck); i++) {
		hw = at91_clk_register_system(regmap, at91sam9n12_systemck[i].n,
					      at91sam9n12_systemck[i].p,
					      at91sam9n12_systemck[i].id);
		if (IS_ERR(hw))
			goto err_free;

		at91sam9n12_pmc->shws[at91sam9n12_systemck[i].id] = hw;
	}

	for (i = 0; i < ARRAY_SIZE(at91sam9n12_periphck); i++) {
		hw = at91_clk_register_sam9x5_peripheral(regmap, &pmc_pcr_lock,
							 &at91sam9n12_pcr_layout,
							 at91sam9n12_periphck[i].n,
							 "masterck",
							 at91sam9n12_periphck[i].id,
							 &range, INT_MIN);
		if (IS_ERR(hw))
			goto err_free;

		at91sam9n12_pmc->phws[at91sam9n12_periphck[i].id] = hw;
	}

	of_clk_add_hw_provider(np, of_clk_hw_pmc_get, at91sam9n12_pmc);

	return;

err_free:
	kfree(at91sam9n12_pmc);
}
/*
 * The TCB is used as the clocksource so its clock is needed early. This means
 * this can't be a platform driver.
 */
CLK_OF_DECLARE_DRIVER(at91sam9n12_pmc, "atmel,at91sam9n12-pmc",
		      at91sam9n12_pmc_setup);
