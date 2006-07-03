/*
 *  linux/arch/arm/mach-pxa/pxa27x.c
 *
 *  Author:	Nicolas Pitre
 *  Created:	Nov 05, 2002
 *  Copyright:	MontaVista Software Inc.
 *
 * Code specific to PXA27x aka Bulverde.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/platform_device.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/ohci.h>

#include "generic.h"

/* Crystal clock: 13MHz */
#define BASE_CLK	13000000

/*
 * Get the clock frequency as reflected by CCSR and the turbo flag.
 * We assume these values have been applied via a fcs.
 * If info is not 0 we also display the current settings.
 */
unsigned int get_clk_frequency_khz( int info)
{
	unsigned long ccsr, clkcfg;
	unsigned int l, L, m, M, n2, N, S;
       	int cccr_a, t, ht, b;

	ccsr = CCSR;
	cccr_a = CCCR & (1 << 25);

	/* Read clkcfg register: it has turbo, b, half-turbo (and f) */
	asm( "mrc\tp14, 0, %0, c6, c0, 0" : "=r" (clkcfg) );
	t  = clkcfg & (1 << 0);
	ht = clkcfg & (1 << 2);
	b  = clkcfg & (1 << 3);

	l  = ccsr & 0x1f;
	n2 = (ccsr>>7) & 0xf;
	m  = (l <= 10) ? 1 : (l <= 20) ? 2 : 4;

	L  = l * BASE_CLK;
	N  = (L * n2) / 2;
	M  = (!cccr_a) ? (L/m) : ((b) ? L : (L/2));
	S  = (b) ? L : (L/2);

	if (info) {
		printk( KERN_INFO "Run Mode clock: %d.%02dMHz (*%d)\n",
			L / 1000000, (L % 1000000) / 10000, l );
		printk( KERN_INFO "Turbo Mode clock: %d.%02dMHz (*%d.%d, %sactive)\n",
			N / 1000000, (N % 1000000)/10000, n2 / 2, (n2 % 2)*5,
			(t) ? "" : "in" );
		printk( KERN_INFO "Memory clock: %d.%02dMHz (/%d)\n",
			M / 1000000, (M % 1000000) / 10000, m );
		printk( KERN_INFO "System bus clock: %d.%02dMHz \n",
			S / 1000000, (S % 1000000) / 10000 );
	}

	return (t) ? (N/1000) : (L/1000);
}

/*
 * Return the current mem clock frequency in units of 10kHz as
 * reflected by CCCR[A], B, and L
 */
unsigned int get_memclk_frequency_10khz(void)
{
	unsigned long ccsr, clkcfg;
	unsigned int l, L, m, M;
       	int cccr_a, b;

	ccsr = CCSR;
	cccr_a = CCCR & (1 << 25);

	/* Read clkcfg register: it has turbo, b, half-turbo (and f) */
	asm( "mrc\tp14, 0, %0, c6, c0, 0" : "=r" (clkcfg) );
	b = clkcfg & (1 << 3);

	l = ccsr & 0x1f;
	m = (l <= 10) ? 1 : (l <= 20) ? 2 : 4;

	L = l * BASE_CLK;
	M = (!cccr_a) ? (L/m) : ((b) ? L : (L/2));

	return (M / 10000);
}

/*
 * Return the current LCD clock frequency in units of 10kHz as
 */
unsigned int get_lcdclk_frequency_10khz(void)
{
	unsigned long ccsr;
	unsigned int l, L, k, K;

	ccsr = CCSR;

	l = ccsr & 0x1f;
	k = (l <= 7) ? 1 : (l <= 16) ? 2 : 4;

	L = l * BASE_CLK;
	K = L / k;

	return (K / 10000);
}

EXPORT_SYMBOL(get_clk_frequency_khz);
EXPORT_SYMBOL(get_memclk_frequency_10khz);
EXPORT_SYMBOL(get_lcdclk_frequency_10khz);

#ifdef CONFIG_PM

int pxa_cpu_pm_prepare(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_MEM:
	case PM_SUSPEND_STANDBY:
		return 0;
	default:
		return -EINVAL;
	}
}

void pxa_cpu_pm_enter(suspend_state_t state)
{
	extern void pxa_cpu_standby(void);
	extern void pxa_cpu_suspend(unsigned int);
	extern void pxa_cpu_resume(void);

	if (state == PM_SUSPEND_STANDBY)
		CKEN = CKEN22_MEMC | CKEN9_OSTIMER | CKEN16_LCD |CKEN0_PWM0;
	else
		CKEN = CKEN22_MEMC | CKEN9_OSTIMER;

	/* ensure voltage-change sequencer not initiated, which hangs */
	PCFR &= ~PCFR_FVC;

	/* Clear edge-detect status register. */
	PEDR = 0xDF12FE1B;

	switch (state) {
	case PM_SUSPEND_STANDBY:
		pxa_cpu_standby();
		break;
	case PM_SUSPEND_MEM:
		/* set resume return address */
		PSPR = virt_to_phys(pxa_cpu_resume);
		pxa_cpu_suspend(PWRMODE_SLEEP);
		break;
	}
}

#endif

/*
 * device registration specific to PXA27x.
 */

static u64 pxa27x_dmamask = 0xffffffffUL;

static struct resource pxa27x_ohci_resources[] = {
	[0] = {
		.start  = 0x4C000000,
		.end    = 0x4C00ff6f,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_USBH1,
		.end    = IRQ_USBH1,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device ohci_device = {
	.name		= "pxa27x-ohci",
	.id		= -1,
	.dev		= {
		.dma_mask = &pxa27x_dmamask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources  = ARRAY_SIZE(pxa27x_ohci_resources),
	.resource       = pxa27x_ohci_resources,
};

void __init pxa_set_ohci_info(struct pxaohci_platform_data *info)
{
	ohci_device.dev.platform_data = info;
}

static struct platform_device *devices[] __initdata = {
	&ohci_device,
};

static int __init pxa27x_init(void)
{
	return platform_add_devices(devices, ARRAY_SIZE(devices));
}

subsys_initcall(pxa27x_init);
