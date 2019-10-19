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

struct at91sam926x_data {
	const struct clk_pll_layout *plla_layout;
	const struct clk_pll_characteristics *plla_characteristics;
	const struct clk_pll_layout *pllb_layout;
	const struct clk_pll_characteristics *pllb_characteristics;
	const struct clk_master_characteristics *mck_characteristics;
	const struct sck *sck;
	const struct pck *pck;
	u8 num_sck;
	u8 num_pck;
	u8 num_progck;
	bool has_slck;
};

static const struct clk_master_characteristics sam9260_mck_characteristics = {
	.output = { .min = 0, .max = 105000000 },
	.divisors = { 1, 2, 4, 0 },
};

static u8 sam9260_plla_out[] = { 0, 2 };

static u16 sam9260_plla_icpll[] = { 1, 1 };

static const struct clk_range sam9260_plla_outputs[] = {
	{ .min = 80000000, .max = 160000000 },
	{ .min = 150000000, .max = 240000000 },
};

static const struct clk_pll_characteristics sam9260_plla_characteristics = {
	.input = { .min = 1000000, .max = 32000000 },
	.num_output = ARRAY_SIZE(sam9260_plla_outputs),
	.output = sam9260_plla_outputs,
	.icpll = sam9260_plla_icpll,
	.out = sam9260_plla_out,
};

static u8 sam9260_pllb_out[] = { 1 };

static u16 sam9260_pllb_icpll[] = { 1 };

static const struct clk_range sam9260_pllb_outputs[] = {
	{ .min = 70000000, .max = 130000000 },
};

static const struct clk_pll_characteristics sam9260_pllb_characteristics = {
	.input = { .min = 1000000, .max = 5000000 },
	.num_output = ARRAY_SIZE(sam9260_pllb_outputs),
	.output = sam9260_pllb_outputs,
	.icpll = sam9260_pllb_icpll,
	.out = sam9260_pllb_out,
};

static const struct sck at91sam9260_systemck[] = {
	{ .n = "uhpck", .p = "usbck",    .id = 6 },
	{ .n = "udpck", .p = "usbck",    .id = 7 },
	{ .n = "pck0",  .p = "prog0",    .id = 8 },
	{ .n = "pck1",  .p = "prog1",    .id = 9 },
};

static const struct pck at91sam9260_periphck[] = {
	{ .n = "pioA_clk",   .id = 2 },
	{ .n = "pioB_clk",   .id = 3 },
	{ .n = "pioC_clk",   .id = 4 },
	{ .n = "adc_clk",    .id = 5 },
	{ .n = "usart0_clk", .id = 6 },
	{ .n = "usart1_clk", .id = 7 },
	{ .n = "usart2_clk", .id = 8 },
	{ .n = "mci0_clk",   .id = 9 },
	{ .n = "udc_clk",    .id = 10 },
	{ .n = "twi0_clk",   .id = 11 },
	{ .n = "spi0_clk",   .id = 12 },
	{ .n = "spi1_clk",   .id = 13 },
	{ .n = "ssc0_clk",   .id = 14 },
	{ .n = "tc0_clk",    .id = 17 },
	{ .n = "tc1_clk",    .id = 18 },
	{ .n = "tc2_clk",    .id = 19 },
	{ .n = "ohci_clk",   .id = 20 },
	{ .n = "macb0_clk",  .id = 21 },
	{ .n = "isi_clk",    .id = 22 },
	{ .n = "usart3_clk", .id = 23 },
	{ .n = "uart0_clk",  .id = 24 },
	{ .n = "uart1_clk",  .id = 25 },
	{ .n = "tc3_clk",    .id = 26 },
	{ .n = "tc4_clk",    .id = 27 },
	{ .n = "tc5_clk",    .id = 28 },
};

static struct at91sam926x_data at91sam9260_data = {
	.plla_layout = &at91rm9200_pll_layout,
	.plla_characteristics = &sam9260_plla_characteristics,
	.pllb_layout = &at91rm9200_pll_layout,
	.pllb_characteristics = &sam9260_pllb_characteristics,
	.mck_characteristics = &sam9260_mck_characteristics,
	.sck = at91sam9260_systemck,
	.num_sck = ARRAY_SIZE(at91sam9260_systemck),
	.pck = at91sam9260_periphck,
	.num_pck = ARRAY_SIZE(at91sam9260_periphck),
	.num_progck = 2,
	.has_slck = true,
};

static const struct clk_master_characteristics sam9g20_mck_characteristics = {
	.output = { .min = 0, .max = 133000000 },
	.divisors = { 1, 2, 4, 6 },
};

static u8 sam9g20_plla_out[] = { 0, 1, 2, 3, 0, 1, 2, 3 };

static u16 sam9g20_plla_icpll[] = { 0, 0, 0, 0, 1, 1, 1, 1 };

static const struct clk_range sam9g20_plla_outputs[] = {
	{ .min = 745000000, .max = 800000000 },
	{ .min = 695000000, .max = 750000000 },
	{ .min = 645000000, .max = 700000000 },
	{ .min = 595000000, .max = 650000000 },
	{ .min = 545000000, .max = 600000000 },
	{ .min = 495000000, .max = 550000000 },
	{ .min = 445000000, .max = 500000000 },
	{ .min = 400000000, .max = 450000000 },
};

static const struct clk_pll_characteristics sam9g20_plla_characteristics = {
	.input = { .min = 2000000, .max = 32000000 },
	.num_output = ARRAY_SIZE(sam9g20_plla_outputs),
	.output = sam9g20_plla_outputs,
	.icpll = sam9g20_plla_icpll,
	.out = sam9g20_plla_out,
};

static u8 sam9g20_pllb_out[] = { 0 };

static u16 sam9g20_pllb_icpll[] = { 0 };

static const struct clk_range sam9g20_pllb_outputs[] = {
	{ .min = 30000000, .max = 100000000 },
};

static const struct clk_pll_characteristics sam9g20_pllb_characteristics = {
	.input = { .min = 2000000, .max = 32000000 },
	.num_output = ARRAY_SIZE(sam9g20_pllb_outputs),
	.output = sam9g20_pllb_outputs,
	.icpll = sam9g20_pllb_icpll,
	.out = sam9g20_pllb_out,
};

static struct at91sam926x_data at91sam9g20_data = {
	.plla_layout = &at91sam9g45_pll_layout,
	.plla_characteristics = &sam9g20_plla_characteristics,
	.pllb_layout = &at91sam9g20_pllb_layout,
	.pllb_characteristics = &sam9g20_pllb_characteristics,
	.mck_characteristics = &sam9g20_mck_characteristics,
	.sck = at91sam9260_systemck,
	.num_sck = ARRAY_SIZE(at91sam9260_systemck),
	.pck = at91sam9260_periphck,
	.num_pck = ARRAY_SIZE(at91sam9260_periphck),
	.num_progck = 2,
	.has_slck = true,
};

static const struct clk_master_characteristics sam9261_mck_characteristics = {
	.output = { .min = 0, .max = 94000000 },
	.divisors = { 1, 2, 4, 0 },
};

static const struct clk_range sam9261_plla_outputs[] = {
	{ .min = 80000000, .max = 200000000 },
	{ .min = 190000000, .max = 240000000 },
};

static const struct clk_pll_characteristics sam9261_plla_characteristics = {
	.input = { .min = 1000000, .max = 32000000 },
	.num_output = ARRAY_SIZE(sam9261_plla_outputs),
	.output = sam9261_plla_outputs,
	.icpll = sam9260_plla_icpll,
	.out = sam9260_plla_out,
};

static u8 sam9261_pllb_out[] = { 1 };

static u16 sam9261_pllb_icpll[] = { 1 };

static const struct clk_range sam9261_pllb_outputs[] = {
	{ .min = 70000000, .max = 130000000 },
};

static const struct clk_pll_characteristics sam9261_pllb_characteristics = {
	.input = { .min = 1000000, .max = 5000000 },
	.num_output = ARRAY_SIZE(sam9261_pllb_outputs),
	.output = sam9261_pllb_outputs,
	.icpll = sam9261_pllb_icpll,
	.out = sam9261_pllb_out,
};

static const struct sck at91sam9261_systemck[] = {
	{ .n = "uhpck", .p = "usbck",    .id = 6 },
	{ .n = "udpck", .p = "usbck",    .id = 7 },
	{ .n = "pck0",  .p = "prog0",    .id = 8 },
	{ .n = "pck1",  .p = "prog1",    .id = 9 },
	{ .n = "pck2",  .p = "prog2",    .id = 10 },
	{ .n = "pck3",  .p = "prog3",    .id = 11 },
	{ .n = "hclk0", .p = "masterck", .id = 16 },
	{ .n = "hclk1", .p = "masterck", .id = 17 },
};

static const struct pck at91sam9261_periphck[] = {
	{ .n = "pioA_clk",   .id = 2, },
	{ .n = "pioB_clk",   .id = 3, },
	{ .n = "pioC_clk",   .id = 4, },
	{ .n = "usart0_clk", .id = 6, },
	{ .n = "usart1_clk", .id = 7, },
	{ .n = "usart2_clk", .id = 8, },
	{ .n = "mci0_clk",   .id = 9, },
	{ .n = "udc_clk",    .id = 10, },
	{ .n = "twi0_clk",   .id = 11, },
	{ .n = "spi0_clk",   .id = 12, },
	{ .n = "spi1_clk",   .id = 13, },
	{ .n = "ssc0_clk",   .id = 14, },
	{ .n = "ssc1_clk",   .id = 15, },
	{ .n = "ssc2_clk",   .id = 16, },
	{ .n = "tc0_clk",    .id = 17, },
	{ .n = "tc1_clk",    .id = 18, },
	{ .n = "tc2_clk",    .id = 19, },
	{ .n = "ohci_clk",   .id = 20, },
	{ .n = "lcd_clk",    .id = 21, },
};

static struct at91sam926x_data at91sam9261_data = {
	.plla_layout = &at91rm9200_pll_layout,
	.plla_characteristics = &sam9261_plla_characteristics,
	.pllb_layout = &at91rm9200_pll_layout,
	.pllb_characteristics = &sam9261_pllb_characteristics,
	.mck_characteristics = &sam9261_mck_characteristics,
	.sck = at91sam9261_systemck,
	.num_sck = ARRAY_SIZE(at91sam9261_systemck),
	.pck = at91sam9261_periphck,
	.num_pck = ARRAY_SIZE(at91sam9261_periphck),
	.num_progck = 4,
};

static const struct clk_master_characteristics sam9263_mck_characteristics = {
	.output = { .min = 0, .max = 120000000 },
	.divisors = { 1, 2, 4, 0 },
};

static const struct clk_range sam9263_pll_outputs[] = {
	{ .min = 80000000, .max = 200000000 },
	{ .min = 190000000, .max = 240000000 },
};

static const struct clk_pll_characteristics sam9263_pll_characteristics = {
	.input = { .min = 1000000, .max = 32000000 },
	.num_output = ARRAY_SIZE(sam9263_pll_outputs),
	.output = sam9263_pll_outputs,
	.icpll = sam9260_plla_icpll,
	.out = sam9260_plla_out,
};

static const struct sck at91sam9263_systemck[] = {
	{ .n = "uhpck", .p = "usbck",    .id = 6 },
	{ .n = "udpck", .p = "usbck",    .id = 7 },
	{ .n = "pck0",  .p = "prog0",    .id = 8 },
	{ .n = "pck1",  .p = "prog1",    .id = 9 },
	{ .n = "pck2",  .p = "prog2",    .id = 10 },
	{ .n = "pck3",  .p = "prog3",    .id = 11 },
};

static const struct pck at91sam9263_periphck[] = {
	{ .n = "pioA_clk",   .id = 2, },
	{ .n = "pioB_clk",   .id = 3, },
	{ .n = "pioCDE_clk", .id = 4, },
	{ .n = "usart0_clk", .id = 7, },
	{ .n = "usart1_clk", .id = 8, },
	{ .n = "usart2_clk", .id = 9, },
	{ .n = "mci0_clk",   .id = 10, },
	{ .n = "mci1_clk",   .id = 11, },
	{ .n = "can_clk",    .id = 12, },
	{ .n = "twi0_clk",   .id = 13, },
	{ .n = "spi0_clk",   .id = 14, },
	{ .n = "spi1_clk",   .id = 15, },
	{ .n = "ssc0_clk",   .id = 16, },
	{ .n = "ssc1_clk",   .id = 17, },
	{ .n = "ac97_clk",   .id = 18, },
	{ .n = "tcb_clk",    .id = 19, },
	{ .n = "pwm_clk",    .id = 20, },
	{ .n = "macb0_clk",  .id = 21, },
	{ .n = "g2de_clk",   .id = 23, },
	{ .n = "udc_clk",    .id = 24, },
	{ .n = "isi_clk",    .id = 25, },
	{ .n = "lcd_clk",    .id = 26, },
	{ .n = "dma_clk",    .id = 27, },
	{ .n = "ohci_clk",   .id = 29, },
};

static struct at91sam926x_data at91sam9263_data = {
	.plla_layout = &at91rm9200_pll_layout,
	.plla_characteristics = &sam9263_pll_characteristics,
	.pllb_layout = &at91rm9200_pll_layout,
	.pllb_characteristics = &sam9263_pll_characteristics,
	.mck_characteristics = &sam9263_mck_characteristics,
	.sck = at91sam9263_systemck,
	.num_sck = ARRAY_SIZE(at91sam9263_systemck),
	.pck = at91sam9263_periphck,
	.num_pck = ARRAY_SIZE(at91sam9263_periphck),
	.num_progck = 4,
};

static void __init at91sam926x_pmc_setup(struct device_node *np,
					 struct at91sam926x_data *data)
{
	const char *slowxtal_name, *mainxtal_name;
	struct pmc_data *at91sam9260_pmc;
	u32 usb_div[] = { 1, 2, 4, 0 };
	const char *parent_names[6];
	const char *slck_name;
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

	regmap = syscon_node_to_regmap(np);
	if (IS_ERR(regmap))
		return;

	at91sam9260_pmc = pmc_data_allocate(PMC_MAIN + 1,
					    ndck(data->sck, data->num_sck),
					    ndck(data->pck, data->num_pck), 0);
	if (!at91sam9260_pmc)
		return;

	bypass = of_property_read_bool(np, "atmel,osc-bypass");

	hw = at91_clk_register_main_osc(regmap, "main_osc", mainxtal_name,
					bypass);
	if (IS_ERR(hw))
		goto err_free;

	hw = at91_clk_register_rm9200_main(regmap, "mainck", "main_osc");
	if (IS_ERR(hw))
		goto err_free;

	at91sam9260_pmc->chws[PMC_MAIN] = hw;

	if (data->has_slck) {
		hw = clk_hw_register_fixed_rate_with_accuracy(NULL,
							      "slow_rc_osc",
							      NULL, 0, 32768,
							      50000000);
		if (IS_ERR(hw))
			goto err_free;

		parent_names[0] = "slow_rc_osc";
		parent_names[1] = "slow_xtal";
		hw = at91_clk_register_sam9260_slow(regmap, "slck",
						    parent_names, 2);
		if (IS_ERR(hw))
			goto err_free;

		at91sam9260_pmc->chws[PMC_SLOW] = hw;
		slck_name = "slck";
	} else {
		slck_name = slowxtal_name;
	}

	hw = at91_clk_register_pll(regmap, "pllack", "mainck", 0,
				   data->plla_layout,
				   data->plla_characteristics);
	if (IS_ERR(hw))
		goto err_free;

	hw = at91_clk_register_pll(regmap, "pllbck", "mainck", 1,
				   data->pllb_layout,
				   data->pllb_characteristics);
	if (IS_ERR(hw))
		goto err_free;

	parent_names[0] = slck_name;
	parent_names[1] = "mainck";
	parent_names[2] = "pllack";
	parent_names[3] = "pllbck";
	hw = at91_clk_register_master(regmap, "masterck", 4, parent_names,
				      &at91rm9200_master_layout,
				      data->mck_characteristics);
	if (IS_ERR(hw))
		goto err_free;

	at91sam9260_pmc->chws[PMC_MCK] = hw;

	hw = at91rm9200_clk_register_usb(regmap, "usbck", "pllbck", usb_div);
	if (IS_ERR(hw))
		goto err_free;

	parent_names[0] = slck_name;
	parent_names[1] = "mainck";
	parent_names[2] = "pllack";
	parent_names[3] = "pllbck";
	for (i = 0; i < data->num_progck; i++) {
		char name[6];

		snprintf(name, sizeof(name), "prog%d", i);

		hw = at91_clk_register_programmable(regmap, name,
						    parent_names, 4, i,
						    &at91rm9200_programmable_layout);
		if (IS_ERR(hw))
			goto err_free;
	}

	for (i = 0; i < data->num_sck; i++) {
		hw = at91_clk_register_system(regmap, data->sck[i].n,
					      data->sck[i].p,
					      data->sck[i].id);
		if (IS_ERR(hw))
			goto err_free;

		at91sam9260_pmc->shws[data->sck[i].id] = hw;
	}

	for (i = 0; i < data->num_pck; i++) {
		hw = at91_clk_register_peripheral(regmap,
						  data->pck[i].n,
						  "masterck",
						  data->pck[i].id);
		if (IS_ERR(hw))
			goto err_free;

		at91sam9260_pmc->phws[data->pck[i].id] = hw;
	}

	of_clk_add_hw_provider(np, of_clk_hw_pmc_get, at91sam9260_pmc);

	return;

err_free:
	pmc_data_free(at91sam9260_pmc);
}

static void __init at91sam9260_pmc_setup(struct device_node *np)
{
	at91sam926x_pmc_setup(np, &at91sam9260_data);
}
CLK_OF_DECLARE_DRIVER(at91sam9260_pmc, "atmel,at91sam9260-pmc",
		      at91sam9260_pmc_setup);

static void __init at91sam9261_pmc_setup(struct device_node *np)
{
	at91sam926x_pmc_setup(np, &at91sam9261_data);
}
CLK_OF_DECLARE_DRIVER(at91sam9261_pmc, "atmel,at91sam9261-pmc",
		      at91sam9261_pmc_setup);

static void __init at91sam9263_pmc_setup(struct device_node *np)
{
	at91sam926x_pmc_setup(np, &at91sam9263_data);
}
CLK_OF_DECLARE_DRIVER(at91sam9263_pmc, "atmel,at91sam9263-pmc",
		      at91sam9263_pmc_setup);

static void __init at91sam9g20_pmc_setup(struct device_node *np)
{
	at91sam926x_pmc_setup(np, &at91sam9g20_data);
}
CLK_OF_DECLARE_DRIVER(at91sam9g20_pmc, "atmel,at91sam9g20-pmc",
		      at91sam9g20_pmc_setup);
