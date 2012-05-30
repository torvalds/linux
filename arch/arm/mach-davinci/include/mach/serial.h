/*
 * DaVinci serial device definitions
 *
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __ASM_ARCH_SERIAL_H
#define __ASM_ARCH_SERIAL_H

#include <asm/memory.h>

#include <mach/hardware.h>

#define DAVINCI_UART0_BASE	(IO_PHYS + 0x20000)
#define DAVINCI_UART1_BASE	(IO_PHYS + 0x20400)
#define DAVINCI_UART2_BASE	(IO_PHYS + 0x20800)

#define DA8XX_UART0_BASE	(IO_PHYS + 0x042000)
#define DA8XX_UART1_BASE	(IO_PHYS + 0x10c000)
#define DA8XX_UART2_BASE	(IO_PHYS + 0x10d000)

#define TNETV107X_UART0_BASE	0x08108100
#define TNETV107X_UART1_BASE	0x08088400
#define TNETV107X_UART2_BASE	0x08108300

#define TNETV107X_UART0_VIRT	IOMEM(0xfee08100)
#define TNETV107X_UART1_VIRT	IOMEM(0xfed88400)
#define TNETV107X_UART2_VIRT	IOMEM(0xfee08300)

/* DaVinci UART register offsets */
#define UART_DAVINCI_PWREMU		0x0c
#define UART_DM646X_SCR			0x10
#define UART_DM646X_SCR_TX_WATERMARK	0x08

#ifndef __ASSEMBLY__
struct davinci_uart_config {
	/* Bit field of UARTs present; bit 0 --> UART1 */
	unsigned int enabled_uarts;
};

extern int davinci_serial_init(struct davinci_uart_config *);
#endif

#endif /* __ASM_ARCH_SERIAL_H */
