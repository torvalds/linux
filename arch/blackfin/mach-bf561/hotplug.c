/*
 * Copyright 2007-2009 Analog Devices Inc.
 *               Graff Yang <graf.yang@analog.com>
 *
 * Licensed under the GPL-2 or later.
 */

#include <asm/blackfin.h>
#include <asm/smp.h>
#define SIC_SYSIRQ(irq)	(irq - (IRQ_CORETMR + 1))

int hotplug_coreb;

void platform_cpu_die(void)
{
	unsigned long iwr[2] = {0, 0};
	unsigned long bank = SIC_SYSIRQ(IRQ_SUPPLE_0) / 32;
	unsigned long bit = 1 << (SIC_SYSIRQ(IRQ_SUPPLE_0) % 32);

	hotplug_coreb = 1;

	iwr[bank] = bit;

	/* disable core timer */
	bfin_write_TCNTL(0);

	/* clear ipi interrupt IRQ_SUPPLE_0 */
	bfin_write_SICB_SYSCR(bfin_read_SICB_SYSCR() | (1 << (10 + 1)));
	SSYNC();

	coreb_sleep(iwr[0], iwr[1], 0);
}
