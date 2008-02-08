/* ASB2303 initialisation
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
#include <linux/device.h>

#include <asm/io.h>
#include <asm/setup.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/intctl-regs.h>

/*
 * initialise some of the unit hardware before gdbstub is set up
 */
asmlinkage void __init unit_init(void)
{
	/* set up the external interrupts */
	SET_XIRQ_TRIGGER(0, XIRQ_TRIGGER_HILEVEL);
	SET_XIRQ_TRIGGER(2, XIRQ_TRIGGER_LOWLEVEL);
	SET_XIRQ_TRIGGER(3, XIRQ_TRIGGER_HILEVEL);
	SET_XIRQ_TRIGGER(4, XIRQ_TRIGGER_LOWLEVEL);
	SET_XIRQ_TRIGGER(5, XIRQ_TRIGGER_LOWLEVEL);
}

/*
 * initialise the rest of the unit hardware after gdbstub is ready
 */
void __init unit_setup(void)
{
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
			set_irq_handler(XIRQ2IRQ(extnum), handle_level_irq);
			break;
		default:
			break;
		}
	}
}
