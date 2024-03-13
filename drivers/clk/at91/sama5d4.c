// SPDX-License-Identifier: GPL-2.0
#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/slab.h>

#include <dt-bindings/clock/at91.h>

#include "pmc.h"

static DEFINE_SPINLOCK(mck_lock);

static const struct clk_master_characteristics mck_characteristics = {
	.output = { .min = 125000000, .max = 200000000 },
	.divisors = { 1, 2, 4, 3 },
};

static u8 plla_out[] = { 0 };

static u16 plla_icpll[] = { 0 };

static const struct clk_range plla_outputs[] = {
	{ .min = 600000000, .max = 1200000000 },
};

static const struct clk_pll_characteristics plla_characteristics = {
	.input = { .min = 12000000, .max = 12000000 },
	.num_output = ARRAY_SIZE(plla_outputs),
	.output = plla_outputs,
	.icpll = plla_icpll,
	.out = plla_out,
};

static const struct clk_pcr_layout sama5d4_pcr_layout = {
	.offset = 0x10c,
	.cmd = BIT(12),
	.pid_mask = GENMASK(6, 0),
};

static const struct {
	char *n;
	char *p;
	u8 id;
} sama5d4_systemck[] = {
	{ .n = "ddrck", .p = "masterck_div", .id = 2 },
	{ .n = "lcdck", .p = "masterck_div", .id = 3 },
	{ .n = "smdck", .p = "smdclk",       .id = 4 },
	{ .n = "uhpck", .p = "usbck",        .id = 6 },
	{ .n = "udpck", .p = "usbck",        .id = 7 },
	{ .n = "pck0",  .p = "prog0",        .id = 8 },
	{ .n = "pck1",  .p = "prog1",        .id = 9 },
	{ .n = "pck2",  .p = "prog2",        .id = 10 },
};

static const struct {
	char *n;
	u8 id;
} sama5d4_periph32ck[] = {
	{ .n = "pioD_clk", .id = 5 },
	{ .n = "usart0_clk", .id = 6 },
	{ .n = "usart1_clk", .id = 7 },
	{ .n = "icm_clk", .id = 9 },
	{ .n = "aes_clk", .id = 12 },
	{ .n = "tdes_clk", .id = 14 },
	{ .n = "sha_clk", .id = 15 },
	{ .n = "matrix1_clk", .id = 17 },
	{ .n = "hsmc_clk", .id = 22 },
	{ .n = "pioA_clk", .id = 23 },
	{ .n = "pioB_clk", .id = 24 },
	{ .n = "pioC_clk", .id = 25 },
	{ .n = "pioE_clk", .id = 26 },
	{ .n = "uart0_clk", .id = 27 },
	{ .n = "uart1_clk", .id = 28 },
	{ .n = "usart2_clk", .id = 29 },
	{ .n = "usart3_clk", .id = 30 },
	{ .n = "usart4_clk", .id = 31 },
	{ .n = "twi0_clk", .id = 32 },
	{ .n = "twi1_clk", .id = 33 },
	{ .n = "twi2_clk", .id = 34 },
	{ .n = "mci0_clk", .id = 35 },
	{ .n = "mci1_clk", .id = 36 },
	{ .n = "spi0_clk", .id = 37 },
	{ .n = "spi1_clk", .id = 38 },
	{ .n = "spi2_clk", .id = 39 },
	{ .n = "tcb0_clk", .id = 40 },
	{ .n = "tcb1_clk", .id = 41 },
	{ .n = "tcb2_clk", .id = 42 },
	{ .n = "pwm_clk", .id = 43 },
	{ .n = "adc_clk", .id = 44 },
	{ .n = "dbgu_clk", .id = 45 },
	{ .n = "uhphs_clk", .id = 46 },
	{ .n = "udphs_clk", .id = 47 },
	{ .n = "ssc0_clk", .id = 48 },
	{ .n = "ssc1_clk", .id = 49 },
	{ .n = "trng_clk", .id = 53 },
	{ .n = "macb0_clk", .id = 54 },
	{ .n = "macb1_clk", .id = 55 },
	{ .n = "fuse_clk", .id = 57 },
	{ .n = "securam_clk", .id = 59 },
	{ .n = "smd_clk", .id = 61 },
	{ .n = "twi3_clk", .id = 62 },
	{ .n = "catb_clk", .id = 63 },
};

static const struct {
	char *n;
	u8 id;
} sama5d4_periphck[] = {
	{ .n = "dma0_clk", .id = 8 },
	{ .n = "cpkcc_clk", .id = 10 },
	{ .n = "aesb_clk", .id = 13 },
	{ .n = "mpddr_clk", .id = 16 },
	{ .n = "matrix0_clk", .id = 18 },
	{ .n = "vdec_clk", .id = 19 },
	{ .n = "dma1_clk", .id = 50 },
	{ .n = "lcdc_clk", .id = 51 },
	{ .n = "isi_clk", .id = 52 },
};

static void __init sama5d4_pmc_setup(struct device_node *np)
{
	struct clk_range range = CLK_RANGE(0, 0);
	const char *slck_name, *mainxtal_name;
	struct pmc_data *sama5d4_pmc;
	const char *parent_names[5];
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

	sama5d4_pmc = pmc_data_allocate(PMC_PLLACK + 1,
					nck(sama5d4_systemck),
					nck(sama5d4_periph32ck), 0, 3);
	if (!sama5d4_pmc)
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

	hw = at91_clk_register_pll(regmap, "pllack", "mainck", 0,
				   &sama5d3_pll_layout, &plla_characteristics);
	if (IS_ERR(hw))
		goto err_free;

	hw = at91_clk_register_plldiv(regmap, "plladivck", "pllack");
	if (IS_ERR(hw))
		goto err_free;

	sama5d4_pmc->chws[PMC_PLLACK] = hw;

	hw = at91_clk_register_utmi(regmap, NULL, "utmick", "mainck");
	if (IS_ERR(hw))
		goto err_free;

	sama5d4_pmc->chws[PMC_UTMI] = hw;

	parent_names[0] = slck_name;
	parent_names[1] = "mainck";
	parent_names[2] = "plladivck";
	parent_names[3] = "utmick";
	hw = at91_clk_register_master_pres(regmap, "masterck_pres", 4,
					   parent_names,
					   &at91sam9x5_master_layout,
					   &mck_characteristics, &mck_lock);
	if (IS_ERR(hw))
		goto err_free;

	hw = at91_clk_register_master_div(regmap, "masterck_div",
					  "masterck_pres",
					  &at91sam9x5_master_layout,
					  &mck_characteristics, &mck_lock,
					  CLK_SET_RATE_GATE, 0);
	if (IS_ERR(hw))
		goto err_free;

	sama5d4_pmc->chws[PMC_MCK] = hw;

	hw = at91_clk_register_h32mx(regmap, "h32mxck", "masterck_div");
	if (IS_ERR(hw))
		goto err_free;

	sama5d4_pmc->chws[PMC_MCK2] = hw;

	parent_names[0] = "plladivck";
	parent_names[1] = "utmick";
	hw = at91sam9x5_clk_register_usb(regmap, "usbck", parent_names, 2);
	if (IS_ERR(hw))
		goto err_free;

	parent_names[0] = "plladivck";
	parent_names[1] = "utmick";
	hw = at91sam9x5_clk_register_smd(regmap, "smdclk", parent_names, 2);
	if (IS_ERR(hw))
		goto err_free;

	parent_names[0] = slck_name;
	parent_names[1] = "mainck";
	parent_names[2] = "plladivck";
	parent_names[3] = "utmick";
	parent_names[4] = "masterck_div";
	for (i = 0; i < 3; i++) {
		char name[6];

		snprintf(name, sizeof(name), "prog%d", i);

		hw = at91_clk_register_programmable(regmap, name,
						    parent_names, 5, i,
						    &at91sam9x5_programmable_layout,
						    NULL);
		if (IS_ERR(hw))
			goto err_free;

		sama5d4_pmc->pchws[i] = hw;
	}

	for (i = 0; i < ARRAY_SIZE(sama5d4_systemck); i++) {
		hw = at91_clk_register_system(regmap, sama5d4_systemck[i].n,
					      sama5d4_systemck[i].p,
					      sama5d4_systemck[i].id);
		if (IS_ERR(hw))
			goto err_free;

		sama5d4_pmc->shws[sama5d4_systemck[i].id] = hw;
	}

	for (i = 0; i < ARRAY_SIZE(sama5d4_periphck); i++) {
		hw = at91_clk_register_sam9x5_peripheral(regmap, &pmc_pcr_lock,
							 &sama5d4_pcr_layout,
							 sama5d4_periphck[i].n,
							 "masterck_div",
							 sama5d4_periphck[i].id,
							 &range, INT_MIN);
		if (IS_ERR(hw))
			goto err_free;

		sama5d4_pmc->phws[sama5d4_periphck[i].id] = hw;
	}

	for (i = 0; i < ARRAY_SIZE(sama5d4_periph32ck); i++) {
		hw = at91_clk_register_sam9x5_peripheral(regmap, &pmc_pcr_lock,
							 &sama5d4_pcr_layout,
							 sama5d4_periph32ck[i].n,
							 "h32mxck",
							 sama5d4_periph32ck[i].id,
							 &range, INT_MIN);
		if (IS_ERR(hw))
			goto err_free;

		sama5d4_pmc->phws[sama5d4_periph32ck[i].id] = hw;
	}

	of_clk_add_hw_provider(np, of_clk_hw_pmc_get, sama5d4_pmc);

	return;

err_free:
	kfree(sama5d4_pmc);
}

CLK_OF_DECLARE(sama5d4_pmc, "atmel,sama5d4-pmc", sama5d4_pmc_setup);
