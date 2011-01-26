/* ASB2364 initialisation
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/setup.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/intctl-regs.h>
#include <unit/fpga-regs.h>

/*
 * initialise some of the unit hardware before gdbstub is set up
 */
asmlinkage void __init unit_init(void)
{
	/* set up the external interrupts */

	/* XIRQ[0]: NAND RXBY */
	/* SET_XIRQ_TRIGGER(0, XIRQ_TRIGGER_LOWLEVEL); */

	/* XIRQ[1]: LAN, UART, I2C, USB, PCI, FPGA */
	SET_XIRQ_TRIGGER(1, XIRQ_TRIGGER_LOWLEVEL);

	/* XIRQ[2]: Extend Slot 1-9 */
	/* SET_XIRQ_TRIGGER(2, XIRQ_TRIGGER_LOWLEVEL); */

#if defined(CONFIG_EXT_SERIAL_IRQ_LEVEL) &&	\
    defined(CONFIG_ETHERNET_IRQ_LEVEL) &&	\
    (CONFIG_EXT_SERIAL_IRQ_LEVEL != CONFIG_ETHERNET_IRQ_LEVEL)
# error CONFIG_EXT_SERIAL_IRQ_LEVEL != CONFIG_ETHERNET_IRQ_LEVEL
#endif

#if defined(CONFIG_EXT_SERIAL_IRQ_LEVEL)
	set_intr_level(XIRQ1, NUM2GxICR_LEVEL(CONFIG_EXT_SERIAL_IRQ_LEVEL));
#elif defined(CONFIG_ETHERNET_IRQ_LEVEL)
	set_intr_level(XIRQ1, NUM2GxICR_LEVEL(CONFIG_ETHERNET_IRQ_LEVEL));
#endif
}

/*
 * initialise the rest of the unit hardware after gdbstub is ready
 */
asmlinkage void __init unit_setup(void)
{

}

/*
 * initialise the external interrupts used by a unit of this type
 */
void __init unit_init_IRQ(void)
{
	unsigned int extnum;

	for (extnum = 0 ; extnum < NR_XIRQS ; extnum++) {
		switch (GET_XIRQ_TRIGGER(extnum)) {
			/* LEVEL triggered interrupts should be made
			 * post-ACK'able as they hold their lines until
			 * serviced
			 */
		case XIRQ_TRIGGER_HILEVEL:
		case XIRQ_TRIGGER_LOWLEVEL:
			mn10300_set_lateack_irq_type(XIRQ2IRQ(extnum));
			break;
		default:
			break;
		}
	}

#define IRQCTL	__SYSREG(0xd5000090, u32)
	IRQCTL |= 0x02;

	irq_fpga_init();
}
