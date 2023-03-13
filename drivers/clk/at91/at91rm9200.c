// SPDX-License-Identifier: GPL-2.0
#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/slab.h>

#include <dt-bindings/clock/at91.h>

#include "pmc.h"

struct sck {
	char *n;
	char *p;
	u8 id;
};

struct pck {
	char *n;
	u8 id;
};

static const struct clk_master_characteristics rm9200_mck_characteristics = {
	.output = { .min = 0, .max = 80000000 },
	.divisors = { 1, 2, 3, 4 },
};

static u8 rm9200_pll_out[] = { 0, 2 };

static const struct clk_range rm9200_pll_outputs[] = {
	{ .min = 80000000, .max = 160000000 },
	{ .min = 150000000, .max = 180000000 },
};

static const struct clk_pll_characteristics rm9200_pll_characteristics = {
	.input = { .min = 1000000, .max = 32000000 },
	.num_output = ARRAY_SIZE(rm9200_pll_outputs),
	.output = rm9200_pll_outputs,
	.out = rm9200_pll_out,
};

static const struct sck at91rm9200_systemck[] = {
	{ .n = "udpck", .p = "usbck",    .id = 1 },
	{ .n = "uhpck", .p = "usbck",    .id = 4 },
	{ .n = "pck0",  .p = "prog0",    .id = 8 },
	{ .n = "pck1",  .p = "prog1",    .id = 9 },
	{ .n = "pck2",  .p = "prog2",    .id = 10 },
	{ .n = "pck3",  .p = "prog3",    .id = 11 },
};

static const struct pck at91rm9200_periphck[] = {
	{ .n = "pioA_clk",   .id = 2 },
	{ .n = "pioB_clk",   .id = 3 },
	{ .n = "pioC_clk",   .id = 4 },
	{ .n = "pioD_clk",   .id = 5 },
	{ .n = "usart0_clk", .id = 6 },
	{ .n = "usart1_clk", .id = 7 },
	{ .n = "usart2_clk", .id = 8 },
	{ .n = "usart3_clk", .id = 9 },
	{ .n = "mci0_clk",   .id = 10 },
	{ .n = "udc_clk",    .id = 11 },
	{ .n = "twi0_clk",   .id = 12 },
	{ .n = "spi0_clk",   .id = 13 },
	{ .n = "ssc0_clk",   .id = 14 },
	{ .n = "ssc1_clk",   .id = 15 },
	{ .n = "ssc2_clk",   .id = 16 },
	{ .n = "tc0_clk",    .id = 17 },
	{ .n = "tc1_clk",    .id = 18 },
	{ .n = "tc2_clk",    .id = 19 },
	{ .n = "tc3_clk",    .id = 20 },
	{ .n = "tc4_clk",    .id = 21 },
	{ .n = "tc5_clk",    .id = 22 },
	{ .n = "ohci_clk",   .id = 23 },
	{ .n = "macb0_clk",  .id = 24 },
};

static void __init at91rm9200_pmc_setup(struct device_node *np)
{
	const char *slowxtal_name, *mainxtal_name;
	struct pmc_data *at91rm9200_pmc;
	u32 usb_div[] = { 1, 2, 0, 0 };
	const char *parent_names[6];
	struct regmap *regmap;
	struct clk_hw *hw;
	int i;
	bool bypass;

	i = of_property_match_string(np, "clock-names", "slow_xtal");
	if (i < 0)
		return;

	slowxtal_name = of_clk_get_parent_name(np, i);

	i = of_property_match_string(np, "clock-names", "main_xtal");
	if (i < 0)
		return;
	mainxtal_name = of_clk_get_parent_name(np, i);

	regmap = device_node_to_regmap(np);
	if (IS_ERR(regmap))
		return;

	at91rm9200_pmc = pmc_data_allocate(PMC_PLLBCK + 1,
					    nck(at91rm9200_systemck),
					    nck(at91rm9200_periphck), 0, 4);
	if (!at91rm9200_pmc)
		return;

	bypass = of_property_read_bool(np, "atmel,osc-bypass");

	hw = at91_clk_register_main_osc(regmap, "main_osc", mainxtal_name,
					bypass);
	if (IS_ERR(hw))
		goto err_free;

	hw = at91_clk_register_rm9200_main(regmap, "mainck", "main_osc");
	if (IS_ERR(hw))
		goto err_free;

	at91rm9200_pmc->chws[PMC_MAIN] = hw;

	hw = at91_clk_register_pll(regmap, "pllack", "mainck", 0,
				   &at91rm9200_pll_layout,
				   &rm9200_pll_characteristics);
	if (IS_ERR(hw))
		goto err_free;

	at91rm9200_pmc->chws[PMC_PLLACK] = hw;

	hw = at91_clk_register_pll(regmap, "pllbck", "mainck", 1,
				   &at91rm9200_pll_layout,
				   &rm9200_pll_characteristics);
	if (IS_ERR(hw))
		goto err_free;

	at91rm9200_pmc->chws[PMC_PLLBCK] = hw;

	parent_names[0] = slowxtal_name;
	parent_names[1] = "mainck";
	parent_names[2] = "pllack";
	parent_names[3] = "pllbck";
	hw = at91_clk_register_master(regmap, "masterck", 4, parent_names,
				      &at91rm9200_master_layout,
				      &rm9200_mck_characteristics);
	if (IS_ERR(hw))
		goto err_free;

	at91rm9200_pmc->chws[PMC_MCK] = hw;

	hw = at91rm9200_clk_register_usb(regmap, "usbck", "pllbck", usb_div);
	if (IS_ERR(hw))
		goto err_free;

	parent_names[0] = slowxtal_name;
	parent_names[1] = "mainck";
	parent_names[2] = "pllack";
	parent_names[3] = "pllbck";
	for (i = 0; i < 4; i++) {
		char name[6];

		snprintf(name, sizeof(name), "prog%d", i);

		hw = at91_clk_register_programmable(regmap, name,
						    parent_names, 4, i,
						    &at91rm9200_programmable_layout,
						    NULL);
		if (IS_ERR(hw))
			goto err_free;

		at91rm9200_pmc->pchws[i] = hw;
	}

	for (i = 0; i < ARRAY_SIZE(at91rm9200_systemck); i++) {
		hw = at91_clk_register_system(regmap, at91rm9200_systemck[i].n,
					      at91rm9200_systemck[i].p,
					      at91rm9200_systemck[i].id);
		if (IS_ERR(hw))
			goto err_free;

		at91rm9200_pmc->shws[at91rm9200_systemck[i].id] = hw;
	}

	for (i = 0; i < ARRAY_SIZE(at91rm9200_periphck); i++) {
		hw = at91_clk_register_peripheral(regmap,
						  at91rm9200_periphck[i].n,
						  "masterck",
						  at91rm9200_periphck[i].id);
		if (IS_ERR(hw))
			goto err_free;

		at91rm9200_pmc->phws[at91rm9200_periphck[i].id] = hw;
	}

	of_clk_add_hw_provider(np, of_clk_hw_pmc_get, at91rm9200_pmc);

	return;

err_free:
	kfree(at91rm9200_pmc);
}
/*
 * While the TCB can be used as the clocksource, the system timer is most likely
 * to be used instead. However, the pinctrl driver doesn't support probe
 * deferring properly. Once this is fixed, this can be switched to a platform
 * driver.
 */
CLK_OF_DECLARE_DRIVER(at91rm9200_pmc, "atmel,at91rm9200-pmc",
		      at91rm9200_pmc_setup);
