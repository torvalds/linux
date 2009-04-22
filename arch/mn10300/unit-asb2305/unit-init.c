/* ASB2305 Initialisation
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <asm/processor.h>
#include <asm/cpu/intctl-regs.h>
#include <asm/cpu/rtc-regs.h>
#include <asm/cpu/serial-regs.h>
#include <unit/serial.h>

/*
 * initialise some of the unit hardware before gdbstub is set up
 */
asmlinkage void __init unit_init(void)
{
#ifndef CONFIG_GDBSTUB_ON_TTYSx
	/* set the 16550 interrupt line to level 3 if not being used for GDB */
	set_intr_level(XIRQ0, GxICR_LEVEL_3);
#endif
}

/*
 * initialise the rest of the unit hardware after gdbstub is ready
 */
void __init unit_setup(void)
{
#ifdef CONFIG_PCI
	unit_pci_init();
#endif
}

/*
 * initialise the external interrupts used by a unit of this type
 */
void __init unit_init_IRQ(void)
{
	unsigned int extnum;

	for (extnum = 0; extnum < NR_XIRQS; extnum++) {
		switch (GET_XIRQ_TRIGGER(extnum)) {
		case XIRQ_TRIGGER_HILEVEL:
		case XIRQ_TRIGGER_LOWLEVEL:
			set_intr_postackable(XIRQ2IRQ(extnum));
			break;
		default:
			break;
		}
	}
}
