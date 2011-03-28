/* Unit-specific 8250 serial ports
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_UNIT_SERIAL_H
#define _ASM_UNIT_SERIAL_H

#include <asm/cpu-regs.h>
#include <proc/irq.h>
#include <unit/fpga-regs.h>
#include <linux/serial_reg.h>

#define SERIAL_PORT0_BASE_ADDRESS	0xA8200000

#define SERIAL_IRQ	XIRQ1	/* single serial (TL16C550C)	(Lo) */

/*
 * The ASB2364 has an 12.288 MHz clock
 * for your UART.
 *
 * It'd be nice if someone built a serial card with a 24.576 MHz
 * clock, since the 16550A is capable of handling a top speed of 1.5
 * megabits/second; but this requires the faster clock.
 */
#define BASE_BAUD (12288000 / 16)

/*
 * dispose of the /dev/ttyS0 and /dev/ttyS1 serial ports
 */
#ifndef CONFIG_GDBSTUB_ON_TTYSx

#define SERIAL_PORT_DFNS						\
	{								\
		.baud_base	= BASE_BAUD,				\
		.irq		= SERIAL_IRQ,				\
		.flags		= STD_COM_FLAGS,			\
		.iomem_base	= (u8 *) SERIAL_PORT0_BASE_ADDRESS,	\
		.iomem_reg_shift = 1,					\
		.io_type	= SERIAL_IO_MEM,			\
	},

#ifndef __ASSEMBLY__

static inline void __debug_to_serial(const char *p, int n)
{
}

#endif /* !__ASSEMBLY__ */

#else /* CONFIG_GDBSTUB_ON_TTYSx */

#define SERIAL_PORT_DFNS /* stolen by gdb-stub */

#if defined(CONFIG_GDBSTUB_ON_TTYS0)
#define GDBPORT_SERIAL_RX	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_RX  * 2, u8)
#define GDBPORT_SERIAL_TX	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_TX  * 2, u8)
#define GDBPORT_SERIAL_DLL	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_DLL * 2, u8)
#define GDBPORT_SERIAL_DLM	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_DLM * 2, u8)
#define GDBPORT_SERIAL_IER	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_IER * 2, u8)
#define GDBPORT_SERIAL_IIR	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_IIR * 2, u8)
#define GDBPORT_SERIAL_FCR	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_FCR * 2, u8)
#define GDBPORT_SERIAL_LCR	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_LCR * 2, u8)
#define GDBPORT_SERIAL_MCR	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_MCR * 2, u8)
#define GDBPORT_SERIAL_LSR	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_LSR * 2, u8)
#define GDBPORT_SERIAL_MSR	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_MSR * 2, u8)
#define GDBPORT_SERIAL_SCR	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_SCR * 2, u8)
#define GDBPORT_SERIAL_IRQ	SERIAL_IRQ

#elif defined(CONFIG_GDBSTUB_ON_TTYS1)
#error The ASB2364 does not have a /dev/ttyS1
#endif

#ifndef __ASSEMBLY__

static inline void __debug_to_serial(const char *p, int n)
{
	char ch;

#define LSR_WAIT_FOR(STATE)	\
	do {} while (!(GDBPORT_SERIAL_LSR & UART_LSR_##STATE))
#define FLOWCTL_QUERY(LINE)	\
	({ GDBPORT_SERIAL_MSR & UART_MSR_##LINE; })
#define FLOWCTL_WAIT_FOR(LINE)	\
	do {} while (!(GDBPORT_SERIAL_MSR & UART_MSR_##LINE))
#define FLOWCTL_CLEAR(LINE)	\
	do { GDBPORT_SERIAL_MCR &= ~UART_MCR_##LINE; } while (0)
#define FLOWCTL_SET(LINE)	\
	do { GDBPORT_SERIAL_MCR |= UART_MCR_##LINE; } while (0)

	FLOWCTL_SET(DTR);

	for (; n > 0; n--) {
		LSR_WAIT_FOR(THRE);
		FLOWCTL_WAIT_FOR(CTS);

		ch = *p++;
		if (ch == 0x0a) {
			GDBPORT_SERIAL_TX = 0x0d;
			LSR_WAIT_FOR(THRE);
			FLOWCTL_WAIT_FOR(CTS);
		}
		GDBPORT_SERIAL_TX = ch;
	}

	FLOWCTL_CLEAR(DTR);
}

#endif /* !__ASSEMBLY__ */

#endif /* CONFIG_GDBSTUB_ON_TTYSx */

#define SERIAL_INITIALIZE					\
do {								\
	/* release reset */					\
	ASB2364_FPGA_REG_RESET_UART = 0x0001;			\
	SyncExBus();						\
} while (0)

#define SERIAL_CHECK_INTERRUPT					\
do {								\
	if ((ASB2364_FPGA_REG_IRQ_UART & 0x0001) == 0x0001) {	\
		return IRQ_NONE;				\
	}							\
} while (0)

#define SERIAL_CLEAR_INTERRUPT					\
do {								\
	ASB2364_FPGA_REG_IRQ_UART = 0x0001;			\
	SyncExBus();						\
} while (0)

#define SERIAL_SET_INT_MASK					\
do {								\
	ASB2364_FPGA_REG_MASK_UART = 0x0001;			\
	SyncExBus();						\
} while (0)

#define SERIAL_CLEAR_INT_MASK					\
do {								\
	ASB2364_FPGA_REG_MASK_UART = 0x0000;			\
	SyncExBus();						\
} while (0)

#endif /* _ASM_UNIT_SERIAL_H */
