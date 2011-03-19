/*
 * Copyright 2007-2009 Analog Devices Inc.
 *               Graff Yang <graf.yang@analog.com>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/smp.h>
#include <asm/blackfin.h>
#include <mach/pll.h>

int hotplug_coreb;

void platform_cpu_die(void)
{
	unsigned long iwr;
	hotplug_coreb = 1;

	/* disable core timer */
	bfin_write_TCNTL(0);

	/* clear ipi interrupt IRQ_SUPPLE_0 of CoreB */
	bfin_write_SICB_SYSCR(bfin_read_SICB_SYSCR() | (1 << (10 + 1)));
	SSYNC();

	/* set CoreB wakeup by ipi0, iwr will be discarded */
	bfin_iwr_set_sup0(&iwr, &iwr, &iwr);
	SSYNC();

	coreb_die();
}
