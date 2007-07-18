/*
 *  linux/arch/arm/mach-pxa/pxa25x.c
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * Code specific to PXA21x/25x/26x variants.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Since this file should be linked before any other machine specific file,
 * the __initcall() here will be executed first.  This serves as default
 * initialization stuff for PXA machines which can be overridden later if
 * need be.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pm.h>

#include <asm/hardware.h>
#include <asm/arch/irqs.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/pm.h>
#include <asm/arch/dma.h>

#include "generic.h"
#include "devices.h"

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

/* Crystal clock */
#define BASE_CLK	3686400

/*
 * Get the clock frequency as reflected by CCCR and the turbo flag.
 * We assume these values have been applied via a fcs.
 * If info is not 0 we also display the current settings.
 */
unsigned int get_clk_frequency_khz(int info)
{
	unsigned long cccr, turbo;
	unsigned int l, L, m, M, n2, N;

	cccr = CCCR;
	asm( "mrc\tp14, 0, %0, c6, c0, 0" : "=r" (turbo) );

	l  =  L_clk_mult[(cccr >> 0) & 0x1f];
	m  =  M_clk_mult[(cccr >> 5) & 0x03];
	n2 = N2_clk_mult[(cccr >> 7) & 0x07];

	L = l * BASE_CLK;
	M = m * L;
	N = n2 * M / 2;

	if(info)
	{
		L += 5000;
		printk( KERN_INFO "Memory clock: %d.%02dMHz (*%d)\n",
			L / 1000000, (L % 1000000) / 10000, l );
		M += 5000;
		printk( KERN_INFO "Run Mode clock: %d.%02dMHz (*%d)\n",
			M / 1000000, (M % 1000000) / 10000, m );
		N += 5000;
		printk( KERN_INFO "Turbo Mode clock: %d.%02dMHz (*%d.%d, %sactive)\n",
			N / 1000000, (N % 1000000) / 10000, n2 / 2, (n2 % 2) * 5,
			(turbo & 1) ? "" : "in" );
	}

	return (turbo & 1) ? (N/1000) : (M/1000);
}

EXPORT_SYMBOL(get_clk_frequency_khz);

/*
 * Return the current memory clock frequency in units of 10kHz
 */
unsigned int get_memclk_frequency_10khz(void)
{
	return L_clk_mult[(CCCR >> 0) & 0x1f] * BASE_CLK / 10000;
}

EXPORT_SYMBOL(get_memclk_frequency_10khz);

/*
 * Return the current LCD clock frequency in units of 10kHz
 */
unsigned int get_lcdclk_frequency_10khz(void)
{
	return get_memclk_frequency_10khz();
}

EXPORT_SYMBOL(get_lcdclk_frequency_10khz);

#ifdef CONFIG_PM

#define SAVE(x)		sleep_save[SLEEP_SAVE_##x] = x
#define RESTORE(x)	x = sleep_save[SLEEP_SAVE_##x]

#define RESTORE_GPLEVEL(n) do { \
	GPSR##n = sleep_save[SLEEP_SAVE_GPLR##n]; \
	GPCR##n = ~sleep_save[SLEEP_SAVE_GPLR##n]; \
} while (0)

/*
 * List of global PXA peripheral registers to preserve.
 * More ones like CP and general purpose register values are preserved
 * with the stack pointer in sleep.S.
 */
enum {	SLEEP_SAVE_START = 0,

	SLEEP_SAVE_GPLR0, SLEEP_SAVE_GPLR1, SLEEP_SAVE_GPLR2,
	SLEEP_SAVE_GPDR0, SLEEP_SAVE_GPDR1, SLEEP_SAVE_GPDR2,
	SLEEP_SAVE_GRER0, SLEEP_SAVE_GRER1, SLEEP_SAVE_GRER2,
	SLEEP_SAVE_GFER0, SLEEP_SAVE_GFER1, SLEEP_SAVE_GFER2,
	SLEEP_SAVE_PGSR0, SLEEP_SAVE_PGSR1, SLEEP_SAVE_PGSR2,

	SLEEP_SAVE_GAFR0_L, SLEEP_SAVE_GAFR0_U,
	SLEEP_SAVE_GAFR1_L, SLEEP_SAVE_GAFR1_U,
	SLEEP_SAVE_GAFR2_L, SLEEP_SAVE_GAFR2_U,

	SLEEP_SAVE_PSTR,

	SLEEP_SAVE_ICMR,
	SLEEP_SAVE_CKEN,

	SLEEP_SAVE_SIZE
};


static void pxa25x_cpu_pm_save(unsigned long *sleep_save)
{
	SAVE(GPLR0); SAVE(GPLR1); SAVE(GPLR2);
	SAVE(GPDR0); SAVE(GPDR1); SAVE(GPDR2);
	SAVE(GRER0); SAVE(GRER1); SAVE(GRER2);
	SAVE(GFER0); SAVE(GFER1); SAVE(GFER2);
	SAVE(PGSR0); SAVE(PGSR1); SAVE(PGSR2);

	SAVE(GAFR0_L); SAVE(GAFR0_U);
	SAVE(GAFR1_L); SAVE(GAFR1_U);
	SAVE(GAFR2_L); SAVE(GAFR2_U);

	SAVE(ICMR);
	SAVE(CKEN);
	SAVE(PSTR);
}

static void pxa25x_cpu_pm_restore(unsigned long *sleep_save)
{
	/* restore registers */
	RESTORE_GPLEVEL(0); RESTORE_GPLEVEL(1); RESTORE_GPLEVEL(2);
	RESTORE(GPDR0); RESTORE(GPDR1); RESTORE(GPDR2);
	RESTORE(GAFR0_L); RESTORE(GAFR0_U);
	RESTORE(GAFR1_L); RESTORE(GAFR1_U);
	RESTORE(GAFR2_L); RESTORE(GAFR2_U);
	RESTORE(GRER0); RESTORE(GRER1); RESTORE(GRER2);
	RESTORE(GFER0); RESTORE(GFER1); RESTORE(GFER2);
	RESTORE(PGSR0); RESTORE(PGSR1); RESTORE(PGSR2);

	RESTORE(CKEN);
	RESTORE(ICMR);
	RESTORE(PSTR);
}

static void pxa25x_cpu_pm_enter(suspend_state_t state)
{
	CKEN = 0;

	switch (state) {
	case PM_SUSPEND_MEM:
		/* set resume return address */
		PSPR = virt_to_phys(pxa_cpu_resume);
		pxa25x_cpu_suspend(PWRMODE_SLEEP);
		break;
	}
}

static struct pxa_cpu_pm_fns pxa25x_cpu_pm_fns = {
	.save_size	= SLEEP_SAVE_SIZE,
	.valid		= pm_valid_only_mem,
	.save		= pxa25x_cpu_pm_save,
	.restore	= pxa25x_cpu_pm_restore,
	.enter		= pxa25x_cpu_pm_enter,
};

static void __init pxa25x_init_pm(void)
{
	pxa_cpu_pm_fns = &pxa25x_cpu_pm_fns;
}
#endif

void __init pxa25x_init_irq(void)
{
	pxa_init_irq_low();
	pxa_init_irq_gpio(85);
}

static struct platform_device *pxa25x_devices[] __initdata = {
	&pxa_device_mci,
	&pxa_device_udc,
	&pxa_device_fb,
	&pxa_device_ffuart,
	&pxa_device_btuart,
	&pxa_device_stuart,
	&pxa_device_i2c,
	&pxa_device_i2s,
	&pxa_device_ficp,
	&pxa_device_rtc,
};

static int __init pxa25x_init(void)
{
	int ret = 0;

	if (cpu_is_pxa21x() || cpu_is_pxa25x()) {
		if ((ret = pxa_init_dma(16)))
			return ret;
#ifdef CONFIG_PM
		pxa25x_init_pm();
#endif
		ret = platform_add_devices(pxa25x_devices,
					   ARRAY_SIZE(pxa25x_devices));
	}
	/* Only add HWUART for PXA255/26x; PXA210/250/27x do not have it. */
	if (cpu_is_pxa25x())
		ret = platform_device_register(&pxa_device_hwuart);

	return ret;
}

subsys_initcall(pxa25x_init);
