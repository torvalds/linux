// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Common PowerQUICC II code.
 *
 * Author: Scott Wood <scottwood@freescale.com>
 * Copyright (c) 2007 Freescale Semiconductor
 *
 * Based on code by Vitaly Bordug <vbordug@ru.mvista.com>
 * pq2_restart fix by Wade Farnsworth <wfarnsworth@mvista.com>
 * Copyright (c) 2006 MontaVista Software, Inc.
 */

#include <linux/kprobes.h>

#include <asm/cpm2.h>
#include <asm/io.h>
#include <asm/pci-bridge.h>

#include <platforms/82xx/pq2.h>

#define RMR_CSRE 0x00000001

void __noreturn pq2_restart(char *cmd)
{
	local_irq_disable();
	setbits32(&cpm2_immr->im_clkrst.car_rmr, RMR_CSRE);

	/* Clear the ME,EE,IR & DR bits in MSR to cause checkstop */
	mtmsr(mfmsr() & ~(MSR_ME | MSR_EE | MSR_IR | MSR_DR));
	in_8(&cpm2_immr->im_clkrst.res[0]);

	panic("Restart failed\n");
}
NOKPROBE_SYMBOL(pq2_restart)
