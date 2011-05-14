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
#include <asm/serial-regs.h>
#include <unit/fpga-regs.h>
#include <unit/serial.h>
#include <unit/smsc911x.h>

#define TTYS0_SERIAL_IER	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_IER * 2, u8)
#define LAN_IRQ_CFG		__SYSREG(SMSC911X_BASE + 0x54, u32)
#define LAN_INT_EN		__SYSREG(SMSC911X_BASE + 0x5c, u32)

/*
 * initialise some of the unit hardware before gdbstub is set up
 */
asmlinkage void __init unit_init(void)
{
	/* Make sure we aren't going to get unexpected interrupts */
	TTYS0_SERIAL_IER = 0;
	SC0RXICR = 0;
	SC0TXICR = 0;
	SC1RXICR = 0;
	SC1TXICR = 0;
	SC2RXICR = 0;
	SC2TXICR = 0;

	/* Attempt to reset the FPGA attached peripherals */
	ASB2364_FPGA_REG_RESET_LAN = 0x0000;
	SyncExBus();
	ASB2364_FPGA_REG_RESET_UART = 0x0000;
	SyncExBus();
	ASB2364_FPGA_REG_RESET_I2C = 0x0000;
	SyncExBus();
	ASB2364_FPGA_REG_RESET_USB = 0x0000;
	SyncExBus();
	ASB2364_FPGA_REG_RESET_AV = 0x0000;
	SyncExBus();

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
	/* Release the reset on the SMSC911X so that it is ready by the time we
	 * need it */
	ASB2364_FPGA_REG_RESET_LAN = 0x0001;
	SyncExBus();
	ASB2364_FPGA_REG_RESET_UART = 0x0001;
	SyncExBus();
	ASB2364_FPGA_REG_RESET_I2C = 0x0001;
	SyncExBus();
	ASB2364_FPGA_REG_RESET_USB = 0x0001;
	SyncExBus();
	ASB2364_FPGA_REG_RESET_AV = 0x0001;
	SyncExBus();

	/* Make sure the ethernet chipset isn't going to give us an interrupt
	 * storm from stuff it was doing pre-reset */
	LAN_IRQ_CFG = 0;
	LAN_INT_EN = 0;
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
