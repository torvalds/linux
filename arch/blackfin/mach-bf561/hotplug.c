/*
 * Copyright 2007-2009 Analog Devices Inc.
 *               Graff Yang <graf.yang@analog.com>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/smp.h>
#include <asm/blackfin.h>
#include <asm/cacheflush.h>
#include <mach/pll.h>

int hotplug_coreb;

void platform_cpu_die(void)
{
	unsigned long iwr;

	hotplug_coreb = 1;

	/*
	 * When CoreB wakes up, the code in _coreb_trampoline_start cannot
	 * turn off the data cache. This causes the CoreB failed to boot.
	 * As a workaround, we invalidate all the data cache before sleep.
	 */
	blackfin_invalidate_entire_dcache();

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
