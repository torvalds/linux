/*
 * Serial port stubs for kernel decompress status messages
 *
 * Initially based on:
 * arch/arm/plat-omap/include/mach/uncompress.h
 *
 * Original copyrights follow.
 *
 * Copyright (C) 2000 RidgeRun, Inc.
 * Author: Greg Lonnon <glonnon@ridgerun.com>
 *
 * Rewritten by:
 * Author: <source@mvista.com>
 * 2004 (c) MontaVista Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/types.h>
#include <linux/serial_reg.h>

#include <asm/mach-types.h>

#include <mach/serial.h>

static u32 *uart;
static u32 *uart_info = (u32 *)(DAVINCI_UART_INFO);

/* PORT_16C550A, in polled non-fifo mode */
static void putc(char c)
{
	while (!(uart[UART_LSR] & UART_LSR_THRE))
		barrier();
	uart[UART_TX] = c;
}

static inline void flush(void)
{
	while (!(uart[UART_LSR] & UART_LSR_THRE))
		barrier();
}

static inline void set_uart_info(u32 phys, void * __iomem virt)
{
	uart = (u32 *)phys;
	uart_info[0] = phys;
	uart_info[1] = (u32)virt;
}

#define _DEBUG_LL_ENTRY(machine, phys, virt)			\
	if (machine_is_##machine()) {				\
		set_uart_info(phys, virt);			\
		break;						\
	}

#define DEBUG_LL_DAVINCI(machine, port)				\
	_DEBUG_LL_ENTRY(machine, DAVINCI_UART##port##_BASE,	\
			IO_ADDRESS(DAVINCI_UART##port##_BASE))

#define DEBUG_LL_DA8XX(machine, port)				\
	_DEBUG_LL_ENTRY(machine, DA8XX_UART##port##_BASE,	\
			IO_ADDRESS(DA8XX_UART##port##_BASE))

#define DEBUG_LL_TNETV107X(machine, port)			\
	_DEBUG_LL_ENTRY(machine, TNETV107X_UART##port##_BASE,	\
			TNETV107X_UART##port##_VIRT)

static inline void __arch_decomp_setup(unsigned long arch_id)
{
	/*
	 * Initialize the port based on the machine ID from the bootloader.
	 * Note that we're using macros here instead of switch statement
	 * as machine_is functions are optimized out for the boards that
	 * are not selected.
	 */
	do {
		/* Davinci boards */
		DEBUG_LL_DAVINCI(davinci_evm,		0);
		DEBUG_LL_DAVINCI(sffsdr,		0);
		DEBUG_LL_DAVINCI(neuros_osd2,		0);
		DEBUG_LL_DAVINCI(davinci_dm355_evm,	0);
		DEBUG_LL_DAVINCI(dm355_leopard,		0);
		DEBUG_LL_DAVINCI(davinci_dm6467_evm,	0);
		DEBUG_LL_DAVINCI(davinci_dm365_evm,	0);

		/* DA8xx boards */
		DEBUG_LL_DA8XX(davinci_da830_evm,	2);
		DEBUG_LL_DA8XX(davinci_da850_evm,	2);

		/* TNETV107x boards */
		DEBUG_LL_TNETV107X(tnetv107x,		1);
	} while (0);
}

#define arch_decomp_setup()	__arch_decomp_setup(arch_id)
#define arch_decomp_wdog()
