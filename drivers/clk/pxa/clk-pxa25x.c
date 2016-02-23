/*
 * Marvell PXA25x family clocks
 *
 * Copyright (C) 2014 Robert Jarzmik
 *
 * Heavily inspired from former arch/arm/mach-pxa/pxa25x.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * For non-devicetree platforms. Once pxa is fully converted to devicetree, this
 * should go away.
 */
#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/of.h>
#include <mach/pxa2xx-regs.h>

#include <dt-bindings/clock/pxa-clock.h>
#include "clk-pxa.h"

#define KHz 1000
#define MHz (1000 * 1000)

enum {
	PXA_CORE_RUN = 0,
	PXA_CORE_TURBO,
};

/*
 * Various clock factors driven by the CCCR register.
 */

/* Crystal Frequency to Memory Frequency Multiplier (L) */
static unsigned char L_clk_mult[32] = { 0, 27, 32, 36, 40, 45, 0, };

/* Memory Frequency to Run Mode Frequency Multiplier (M) */
static unsigned char M_clk_mult[4] = { 0, 1, 2, 4 };

/* Run Mode Frequency to Turbo Mode Frequency Multiplier (N) */
/* Note: we store the value N * 2 here. */
static unsigned char N2_clk_mult[8] = { 0, 0, 2, 3, 4, 0, 6, 0 };

static const char * const get_freq_khz[] = {
	"core", "run", "cpll", "memory"
};

/*
 * Get the clock frequency as reflected by CCCR and the turbo flag.
 * We assume these values have been applied via a fcs.
 * If info is not 0 we also display the current settings.
 */
unsigned int pxa25x_get_clk_frequency_khz(int info)
{
	struct clk *clk;
	unsigned long clks[5];
	int i;

	for (i = 0; i < ARRAY_SIZE(get_freq_khz); i++) {
		clk = clk_get(NULL, get_freq_khz[i]);
		if (IS_ERR(clk)) {
			clks[i] = 0;
		} else {
			clks[i] = clk_get_rate(clk);
			clk_put(clk);
		}
	}

	if (info) {
		pr_info("Run Mode clock: %ld.%02ldMHz\n",
			clks[1] / 1000000, (clks[1] % 1000000) / 10000);
		pr_info("Turbo Mode clock: %ld.%02ldMHz\n",
			clks[2] / 1000000, (clks[2] % 1000000) / 10000);
		pr_info("Memory clock: %ld.%02ldMHz\n",
			clks[3] / 1000000, (clks[3] % 1000000) / 10000);
	}

	return (unsigned int)clks[0] / KHz;
}

static unsigned long clk_pxa25x_memory_get_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	unsigned long cccr = CCCR;
	unsigned int m = M_clk_mult[(cccr >> 5) & 0x03];

	return parent_rate / m;
}
PARENTS(clk_pxa25x_memory) = { "run" };
RATE_RO_OPS(clk_pxa25x_memory, "memory");

PARENTS(pxa25x_pbus95) = { "ppll_95_85mhz", "ppll_95_85mhz" };
PARENTS(pxa25x_pbus147) = { "ppll_147_46mhz", "ppll_147_46mhz" };
PARENTS(pxa25x_osc3) = { "osc_3_6864mhz", "osc_3_6864mhz" };

#define PXA25X_CKEN(dev_id, con_id, parents, mult, div,			\
		    bit, is_lp, flags)					\
	PXA_CKEN(dev_id, con_id, bit, parents, mult, div, mult, div,	\
		 is_lp,  &CKEN, CKEN_ ## bit, flags)
#define PXA25X_PBUS95_CKEN(dev_id, con_id, bit, mult_hp, div_hp, delay)	\
	PXA25X_CKEN(dev_id, con_id, pxa25x_pbus95_parents, mult_hp,	\
		    div_hp, bit, NULL, 0)
#define PXA25X_PBUS147_CKEN(dev_id, con_id, bit, mult_hp, div_hp, delay)\
	PXA25X_CKEN(dev_id, con_id, pxa25x_pbus147_parents, mult_hp,	\
		    div_hp, bit, NULL, 0)
#define PXA25X_OSC3_CKEN(dev_id, con_id, bit, mult_hp, div_hp, delay)	\
	PXA25X_CKEN(dev_id, con_id, pxa25x_osc3_parents, mult_hp,	\
		    div_hp, bit, NULL, 0)

#define PXA25X_CKEN_1RATE(dev_id, con_id, bit, parents, delay)		\
	PXA_CKEN_1RATE(dev_id, con_id, bit, parents,			\
		       &CKEN, CKEN_ ## bit, 0)
#define PXA25X_CKEN_1RATE_AO(dev_id, con_id, bit, parents, delay)	\
	PXA_CKEN_1RATE(dev_id, con_id, bit, parents,			\
		       &CKEN, CKEN_ ## bit, CLK_IGNORE_UNUSED)

static struct desc_clk_cken pxa25x_clocks[] __initdata = {
	PXA25X_PBUS95_CKEN("pxa2xx-mci.0", NULL, MMC, 1, 5, 0),
	PXA25X_PBUS95_CKEN("pxa2xx-i2c.0", NULL, I2C, 1, 3, 0),
	PXA25X_PBUS95_CKEN("pxa2xx-ir", "FICPCLK", FICP, 1, 2, 0),
	PXA25X_PBUS95_CKEN("pxa25x-udc", NULL, USB, 1, 2, 5),
	PXA25X_PBUS147_CKEN("pxa2xx-uart.0", NULL, FFUART, 1, 10, 1),
	PXA25X_PBUS147_CKEN("pxa2xx-uart.1", NULL, BTUART, 1, 10, 1),
	PXA25X_PBUS147_CKEN("pxa2xx-uart.2", NULL, STUART, 1, 10, 1),
	PXA25X_PBUS147_CKEN("pxa2xx-uart.3", NULL, HWUART, 1, 10, 1),
	PXA25X_PBUS147_CKEN("pxa2xx-i2s", NULL, I2S, 1, 10, 0),
	PXA25X_PBUS147_CKEN(NULL, "AC97CLK", AC97, 1, 12, 0),
	PXA25X_OSC3_CKEN("pxa25x-ssp.0", NULL, SSP, 1, 1, 0),
	PXA25X_OSC3_CKEN("pxa25x-nssp.1", NULL, NSSP, 1, 1, 0),
	PXA25X_OSC3_CKEN("pxa25x-nssp.2", NULL, ASSP, 1, 1, 0),
	PXA25X_OSC3_CKEN("pxa25x-pwm.0", NULL, PWM0, 1, 1, 0),
	PXA25X_OSC3_CKEN("pxa25x-pwm.1", NULL, PWM1, 1, 1, 0),

	PXA25X_CKEN_1RATE("pxa2xx-fb", NULL, LCD, clk_pxa25x_memory_parents, 0),
	PXA25X_CKEN_1RATE_AO("pxa2xx-pcmcia", NULL, MEMC,
			     clk_pxa25x_memory_parents, 0),
};

static u8 clk_pxa25x_core_get_parent(struct clk_hw *hw)
{
	unsigned long clkcfg;
	unsigned int t;

	asm("mrc\tp14, 0, %0, c6, c0, 0" : "=r" (clkcfg));
	t  = clkcfg & (1 << 0);
	if (t)
		return PXA_CORE_TURBO;
	return PXA_CORE_RUN;
}

static unsigned long clk_pxa25x_core_get_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	return parent_rate;
}
PARENTS(clk_pxa25x_core) = { "run", "cpll" };
MUX_RO_RATE_RO_OPS(clk_pxa25x_core, "core");

static unsigned long clk_pxa25x_run_get_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	unsigned long cccr = CCCR;
	unsigned int n2 = N2_clk_mult[(cccr >> 7) & 0x07];

	return (parent_rate / n2) * 2;
}
PARENTS(clk_pxa25x_run) = { "cpll" };
RATE_RO_OPS(clk_pxa25x_run, "run");

static unsigned long clk_pxa25x_cpll_get_rate(struct clk_hw *hw,
	unsigned long parent_rate)
{
	unsigned long clkcfg, cccr = CCCR;
	unsigned int l, m, n2, t;

	asm("mrc\tp14, 0, %0, c6, c0, 0" : "=r" (clkcfg));
	t = clkcfg & (1 << 0);
	l  =  L_clk_mult[(cccr >> 0) & 0x1f];
	m = M_clk_mult[(cccr >> 5) & 0x03];
	n2 = N2_clk_mult[(cccr >> 7) & 0x07];

	if (t)
		return m * l * n2 * parent_rate / 2;
	return m * l * parent_rate;
}
PARENTS(clk_pxa25x_cpll) = { "osc_3_6864mhz" };
RATE_RO_OPS(clk_pxa25x_cpll, "cpll");

static void __init pxa25x_register_core(void)
{
	clk_register_clk_pxa25x_cpll();
	clk_register_clk_pxa25x_run();
	clkdev_pxa_register(CLK_CORE, "core", NULL,
			    clk_register_clk_pxa25x_core());
}

static void __init pxa25x_register_plls(void)
{
	clk_register_fixed_rate(NULL, "osc_3_6864mhz", NULL,
				CLK_GET_RATE_NOCACHE | CLK_IS_ROOT,
				3686400);
	clk_register_fixed_rate(NULL, "osc_32_768khz", NULL,
				CLK_GET_RATE_NOCACHE | CLK_IS_ROOT,
				32768);
	clk_register_fixed_rate(NULL, "clk_dummy", NULL, CLK_IS_ROOT, 0);
	clk_register_fixed_factor(NULL, "ppll_95_85mhz", "osc_3_6864mhz",
				  0, 26, 1);
	clk_register_fixed_factor(NULL, "ppll_147_46mhz", "osc_3_6864mhz",
				  0, 40, 1);
}

static void __init pxa25x_base_clocks_init(void)
{
	pxa25x_register_plls();
	pxa25x_register_core();
	clk_register_clk_pxa25x_memory();
}

#define DUMMY_CLK(_con_id, _dev_id, _parent) \
	{ .con_id = _con_id, .dev_id = _dev_id, .parent = _parent }
struct dummy_clk {
	const char *con_id;
	const char *dev_id;
	const char *parent;
};
static struct dummy_clk dummy_clks[] __initdata = {
	DUMMY_CLK(NULL, "pxa25x-gpio", "osc_32_768khz"),
	DUMMY_CLK(NULL, "pxa26x-gpio", "osc_32_768khz"),
	DUMMY_CLK("GPIO11_CLK", NULL, "osc_3_6864mhz"),
	DUMMY_CLK("GPIO12_CLK", NULL, "osc_32_768khz"),
	DUMMY_CLK(NULL, "sa1100-rtc", "osc_32_768khz"),
	DUMMY_CLK("OSTIMER0", NULL, "osc_32_768khz"),
	DUMMY_CLK("UARTCLK", "pxa2xx-ir", "STUART"),
};

static void __init pxa25x_dummy_clocks_init(void)
{
	struct clk *clk;
	struct dummy_clk *d;
	const char *name;
	int i;

	/*
	 * All pinctrl logic has been wiped out of the clock driver, especially
	 * for gpio11 and gpio12 outputs. Machine code should ensure proper pin
	 * control (ie. pxa2xx_mfp_config() invocation).
	 */
	for (i = 0; i < ARRAY_SIZE(dummy_clks); i++) {
		d = &dummy_clks[i];
		name = d->dev_id ? d->dev_id : d->con_id;
		clk = clk_register_fixed_factor(NULL, name, d->parent, 0, 1, 1);
		clk_register_clkdev(clk, d->con_id, d->dev_id);
	}
}

int __init pxa25x_clocks_init(void)
{
	pxa25x_base_clocks_init();
	pxa25x_dummy_clocks_init();
	return clk_pxa_cken_init(pxa25x_clocks, ARRAY_SIZE(pxa25x_clocks));
}

static void __init pxa25x_dt_clocks_init(struct device_node *np)
{
	pxa25x_clocks_init();
	clk_pxa_dt_common_init(np);
}
CLK_OF_DECLARE(pxa25x_clks, "marvell,pxa250-core-clocks",
	       pxa25x_dt_clocks_init);
