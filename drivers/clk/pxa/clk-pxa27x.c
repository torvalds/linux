/*
 * Marvell PXA27x family clocks
 *
 * Copyright (C) 2014 Robert Jarzmik
 *
 * Heavily inspired from former arch/arm/mach-pxa/clock.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 */
#include <linux/clk-provider.h>
#include <mach/pxa2xx-regs.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/of.h>

#include <dt-bindings/clock/pxa-clock.h>
#include "clk-pxa.h"

#define KHz 1000
#define MHz (1000 * 1000)

enum {
	PXA_CORE_13Mhz = 0,
	PXA_CORE_RUN,
	PXA_CORE_TURBO,
};

enum {
	PXA_BUS_13Mhz = 0,
	PXA_BUS_RUN,
};

enum {
	PXA_LCD_13Mhz = 0,
	PXA_LCD_RUN,
};

enum {
	PXA_MEM_13Mhz = 0,
	PXA_MEM_SYSTEM_BUS,
	PXA_MEM_RUN,
};

static const char * const get_freq_khz[] = {
	"core", "run", "cpll", "memory",
	"system_bus"
};

/*
 * Get the clock frequency as reflected by CCSR and the turbo flag.
 * We assume these values have been applied via a fcs.
 * If info is not 0 we also display the current settings.
 */
unsigned int pxa27x_get_clk_frequency_khz(int info)
{
	struct clk *clk;
	unsigned long clks[5];
	int i;

	for (i = 0; i < 5; i++) {
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
		pr_info("System bus clock: %ld.%02ldMHz\n",
			clks[4] / 1000000, (clks[4] % 1000000) / 10000);
	}
	return (unsigned int)clks[0] / KHz;
}

bool pxa27x_is_ppll_disabled(void)
{
	unsigned long ccsr = readl(CCSR);

	return ccsr & (1 << CCCR_PPDIS_BIT);
}

#define PXA27X_CKEN(dev_id, con_id, parents, mult_hp, div_hp,		\
		    bit, is_lp, flags)					\
	PXA_CKEN(dev_id, con_id, bit, parents, 1, 1, mult_hp, div_hp,	\
		 is_lp,  CKEN, CKEN_ ## bit, flags)
#define PXA27X_PBUS_CKEN(dev_id, con_id, bit, mult_hp, div_hp, delay)	\
	PXA27X_CKEN(dev_id, con_id, pxa27x_pbus_parents, mult_hp,	\
		    div_hp, bit, pxa27x_is_ppll_disabled, 0)

PARENTS(pxa27x_pbus) = { "osc_13mhz", "ppll_312mhz" };
PARENTS(pxa27x_sbus) = { "system_bus", "system_bus" };
PARENTS(pxa27x_32Mhz_bus) = { "osc_32_768khz", "osc_32_768khz" };
PARENTS(pxa27x_lcd_bus) = { "lcd_base", "lcd_base" };
PARENTS(pxa27x_membus) = { "lcd_base", "lcd_base" };

#define PXA27X_CKEN_1RATE(dev_id, con_id, bit, parents, delay)		\
	PXA_CKEN_1RATE(dev_id, con_id, bit, parents,			\
		       CKEN, CKEN_ ## bit, 0)
#define PXA27X_CKEN_1RATE_AO(dev_id, con_id, bit, parents, delay)	\
	PXA_CKEN_1RATE(dev_id, con_id, bit, parents,			\
		       CKEN, CKEN_ ## bit, CLK_IGNORE_UNUSED)

static struct desc_clk_cken pxa27x_clocks[] __initdata = {
	PXA27X_PBUS_CKEN("pxa2xx-uart.0", NULL, FFUART, 2, 42, 1),
	PXA27X_PBUS_CKEN("pxa2xx-uart.1", NULL, BTUART, 2, 42, 1),
	PXA27X_PBUS_CKEN("pxa2xx-uart.2", NULL, STUART, 2, 42, 1),
	PXA27X_PBUS_CKEN("pxa2xx-i2s", NULL, I2S, 2, 51, 0),
	PXA27X_PBUS_CKEN("pxa2xx-i2c.0", NULL, I2C, 2, 19, 0),
	PXA27X_PBUS_CKEN("pxa27x-udc", NULL, USB, 2, 13, 5),
	PXA27X_PBUS_CKEN("pxa2xx-mci.0", NULL, MMC, 2, 32, 0),
	PXA27X_PBUS_CKEN("pxa2xx-ir", "FICPCLK", FICP, 2, 13, 0),
	PXA27X_PBUS_CKEN("pxa27x-ohci", NULL, USBHOST, 2, 13, 0),
	PXA27X_PBUS_CKEN("pxa2xx-i2c.1", NULL, PWRI2C, 1, 24, 0),
	PXA27X_PBUS_CKEN("pxa27x-ssp.0", NULL, SSP1, 1, 24, 0),
	PXA27X_PBUS_CKEN("pxa27x-ssp.1", NULL, SSP2, 1, 24, 0),
	PXA27X_PBUS_CKEN("pxa27x-ssp.2", NULL, SSP3, 1, 24, 0),
	PXA27X_PBUS_CKEN("pxa27x-pwm.0", NULL, PWM0, 1, 24, 0),
	PXA27X_PBUS_CKEN("pxa27x-pwm.1", NULL, PWM1, 1, 24, 0),
	PXA27X_PBUS_CKEN(NULL, "MSLCLK", MSL, 2, 13, 0),
	PXA27X_PBUS_CKEN(NULL, "USIMCLK", USIM, 2, 13, 0),
	PXA27X_PBUS_CKEN(NULL, "MSTKCLK", MEMSTK, 2, 32, 0),
	PXA27X_PBUS_CKEN(NULL, "AC97CLK", AC97, 1, 1, 0),
	PXA27X_PBUS_CKEN(NULL, "AC97CONFCLK", AC97CONF, 1, 1, 0),
	PXA27X_PBUS_CKEN(NULL, "OSTIMER0", OSTIMER, 1, 96, 0),

	PXA27X_CKEN_1RATE("pxa27x-keypad", NULL, KEYPAD,
			  pxa27x_32Mhz_bus_parents, 0),
	PXA27X_CKEN_1RATE(NULL, "IMCLK", IM, pxa27x_sbus_parents, 0),
	PXA27X_CKEN_1RATE("pxa2xx-fb", NULL, LCD, pxa27x_lcd_bus_parents, 0),
	PXA27X_CKEN_1RATE("pxa27x-camera.0", NULL, CAMERA,
			  pxa27x_lcd_bus_parents, 0),
	PXA27X_CKEN_1RATE_AO("pxa2xx-pcmcia", NULL, MEMC,
			     pxa27x_membus_parents, 0),

};

static unsigned long clk_pxa27x_cpll_get_rate(struct clk_hw *hw,
	unsigned long parent_rate)
{
	unsigned long clkcfg;
	unsigned int t, ht;
	unsigned int l, L, n2, N;
	unsigned long ccsr = readl(CCSR);

	asm("mrc\tp14, 0, %0, c6, c0, 0" : "=r" (clkcfg));
	t  = clkcfg & (1 << 0);
	ht = clkcfg & (1 << 2);

	l  = ccsr & CCSR_L_MASK;
	n2 = (ccsr & CCSR_N2_MASK) >> CCSR_N2_SHIFT;
	L  = l * parent_rate;
	N  = (L * n2) / 2;

	return t ? N : L;
}
PARENTS(clk_pxa27x_cpll) = { "osc_13mhz" };
RATE_RO_OPS(clk_pxa27x_cpll, "cpll");

static unsigned long clk_pxa27x_lcd_base_get_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	unsigned int l, osc_forced;
	unsigned long ccsr = readl(CCSR);
	unsigned long cccr = readl(CCCR);

	l  = ccsr & CCSR_L_MASK;
	osc_forced = ccsr & (1 << CCCR_CPDIS_BIT);
	if (osc_forced) {
		if (cccr & (1 << CCCR_LCD_26_BIT))
			return parent_rate * 2;
		else
			return parent_rate;
	}

	if (l <= 7)
		return parent_rate;
	if (l <= 16)
		return parent_rate / 2;
	return parent_rate / 4;
}

static u8 clk_pxa27x_lcd_base_get_parent(struct clk_hw *hw)
{
	unsigned int osc_forced;
	unsigned long ccsr = readl(CCSR);

	osc_forced = ccsr & (1 << CCCR_CPDIS_BIT);
	if (osc_forced)
		return PXA_LCD_13Mhz;
	else
		return PXA_LCD_RUN;
}

PARENTS(clk_pxa27x_lcd_base) = { "osc_13mhz", "run" };
MUX_RO_RATE_RO_OPS(clk_pxa27x_lcd_base, "lcd_base");

static void __init pxa27x_register_plls(void)
{
	clk_register_fixed_rate(NULL, "osc_13mhz", NULL,
				CLK_GET_RATE_NOCACHE,
				13 * MHz);
	clk_register_fixed_rate(NULL, "osc_32_768khz", NULL,
				CLK_GET_RATE_NOCACHE,
				32768 * KHz);
	clk_register_fixed_rate(NULL, "clk_dummy", NULL, 0, 0);
	clk_register_fixed_factor(NULL, "ppll_312mhz", "osc_13mhz", 0, 24, 1);
}

static unsigned long clk_pxa27x_core_get_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	unsigned long clkcfg;
	unsigned int t, ht, b, osc_forced;
	unsigned long ccsr = readl(CCSR);

	osc_forced = ccsr & (1 << CCCR_CPDIS_BIT);
	asm("mrc\tp14, 0, %0, c6, c0, 0" : "=r" (clkcfg));
	t  = clkcfg & (1 << 0);
	ht = clkcfg & (1 << 2);
	b  = clkcfg & (1 << 3);

	if (osc_forced)
		return parent_rate;
	if (ht)
		return parent_rate / 2;
	else
		return parent_rate;
}

static u8 clk_pxa27x_core_get_parent(struct clk_hw *hw)
{
	unsigned long clkcfg;
	unsigned int t, ht, b, osc_forced;
	unsigned long ccsr = readl(CCSR);

	osc_forced = ccsr & (1 << CCCR_CPDIS_BIT);
	if (osc_forced)
		return PXA_CORE_13Mhz;

	asm("mrc\tp14, 0, %0, c6, c0, 0" : "=r" (clkcfg));
	t  = clkcfg & (1 << 0);
	ht = clkcfg & (1 << 2);
	b  = clkcfg & (1 << 3);

	if (ht || t)
		return PXA_CORE_TURBO;
	return PXA_CORE_RUN;
}
PARENTS(clk_pxa27x_core) = { "osc_13mhz", "run", "cpll" };
MUX_RO_RATE_RO_OPS(clk_pxa27x_core, "core");

static unsigned long clk_pxa27x_run_get_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	unsigned long ccsr = readl(CCSR);
	unsigned int n2 = (ccsr & CCSR_N2_MASK) >> CCSR_N2_SHIFT;

	return (parent_rate / n2) * 2;
}
PARENTS(clk_pxa27x_run) = { "cpll" };
RATE_RO_OPS(clk_pxa27x_run, "run");

static void __init pxa27x_register_core(void)
{
	clk_register_clk_pxa27x_cpll();
	clk_register_clk_pxa27x_run();

	clkdev_pxa_register(CLK_CORE, "core", NULL,
			    clk_register_clk_pxa27x_core());
}

static unsigned long clk_pxa27x_system_bus_get_rate(struct clk_hw *hw,
						    unsigned long parent_rate)
{
	unsigned long clkcfg;
	unsigned int b, osc_forced;
	unsigned long ccsr = readl(CCSR);

	osc_forced = ccsr & (1 << CCCR_CPDIS_BIT);
	asm("mrc\tp14, 0, %0, c6, c0, 0" : "=r" (clkcfg));
	b  = clkcfg & (1 << 3);

	if (osc_forced)
		return parent_rate;
	if (b)
		return parent_rate / 2;
	else
		return parent_rate;
}

static u8 clk_pxa27x_system_bus_get_parent(struct clk_hw *hw)
{
	unsigned int osc_forced;
	unsigned long ccsr = readl(CCSR);

	osc_forced = ccsr & (1 << CCCR_CPDIS_BIT);
	if (osc_forced)
		return PXA_BUS_13Mhz;
	else
		return PXA_BUS_RUN;
}

PARENTS(clk_pxa27x_system_bus) = { "osc_13mhz", "run" };
MUX_RO_RATE_RO_OPS(clk_pxa27x_system_bus, "system_bus");

static unsigned long clk_pxa27x_memory_get_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	unsigned int a, l, osc_forced;
	unsigned long cccr = readl(CCCR);
	unsigned long ccsr = readl(CCSR);

	osc_forced = ccsr & (1 << CCCR_CPDIS_BIT);
	a = cccr & (1 << CCCR_A_BIT);
	l  = ccsr & CCSR_L_MASK;

	if (osc_forced || a)
		return parent_rate;
	if (l <= 10)
		return parent_rate;
	if (l <= 20)
		return parent_rate / 2;
	return parent_rate / 4;
}

static u8 clk_pxa27x_memory_get_parent(struct clk_hw *hw)
{
	unsigned int osc_forced, a;
	unsigned long cccr = readl(CCCR);
	unsigned long ccsr = readl(CCSR);

	osc_forced = ccsr & (1 << CCCR_CPDIS_BIT);
	a = cccr & (1 << CCCR_A_BIT);
	if (osc_forced)
		return PXA_MEM_13Mhz;
	if (a)
		return PXA_MEM_SYSTEM_BUS;
	else
		return PXA_MEM_RUN;
}

PARENTS(clk_pxa27x_memory) = { "osc_13mhz", "system_bus", "run" };
MUX_RO_RATE_RO_OPS(clk_pxa27x_memory, "memory");

#define DUMMY_CLK(_con_id, _dev_id, _parent) \
	{ .con_id = _con_id, .dev_id = _dev_id, .parent = _parent }
struct dummy_clk {
	const char *con_id;
	const char *dev_id;
	const char *parent;
};
static struct dummy_clk dummy_clks[] __initdata = {
	DUMMY_CLK(NULL, "pxa27x-gpio", "osc_32_768khz"),
	DUMMY_CLK(NULL, "sa1100-rtc", "osc_32_768khz"),
	DUMMY_CLK("UARTCLK", "pxa2xx-ir", "STUART"),
};

static void __init pxa27x_dummy_clocks_init(void)
{
	struct clk *clk;
	struct dummy_clk *d;
	const char *name;
	int i;

	for (i = 0; i < ARRAY_SIZE(dummy_clks); i++) {
		d = &dummy_clks[i];
		name = d->dev_id ? d->dev_id : d->con_id;
		clk = clk_register_fixed_factor(NULL, name, d->parent, 0, 1, 1);
		clk_register_clkdev(clk, d->con_id, d->dev_id);
	}
}

static void __init pxa27x_base_clocks_init(void)
{
	pxa27x_register_plls();
	pxa27x_register_core();
	clk_register_clk_pxa27x_system_bus();
	clk_register_clk_pxa27x_memory();
	clk_register_clk_pxa27x_lcd_base();
}

int __init pxa27x_clocks_init(void)
{
	pxa27x_base_clocks_init();
	pxa27x_dummy_clocks_init();
	return clk_pxa_cken_init(pxa27x_clocks, ARRAY_SIZE(pxa27x_clocks));
}

static void __init pxa27x_dt_clocks_init(struct device_node *np)
{
	pxa27x_clocks_init();
	clk_pxa_dt_common_init(np);
}
CLK_OF_DECLARE(pxa_clks, "marvell,pxa270-clocks", pxa27x_dt_clocks_init);
