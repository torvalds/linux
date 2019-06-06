/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * arch/arm/mach-w90x900/include/mach/regs-serial.h
 *
 * Copyright (c) 2008 Nuvoton technology corporation
 * All rights reserved.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * Based on arch/arm/mach-s3c2410/include/mach/regs-serial.h
 */

#ifndef __ASM_ARM_REGS_SERIAL_H
#define __ASM_ARM_REGS_SERIAL_H

#define UART0_BA	W90X900_VA_UART
#define UART1_BA	(W90X900_VA_UART+0x100)
#define UART2_BA	(W90X900_VA_UART+0x200)
#define UART3_BA	(W90X900_VA_UART+0x300)
#define UART4_BA	(W90X900_VA_UART+0x400)

#define UART0_PA	W90X900_PA_UART
#define UART1_PA	(W90X900_PA_UART+0x100)
#define UART2_PA	(W90X900_PA_UART+0x200)
#define UART3_PA	(W90X900_PA_UART+0x300)
#define UART4_PA	(W90X900_PA_UART+0x400)

#ifndef __ASSEMBLY__

struct w90x900_uart_clksrc {
	const char	*name;
	unsigned int	divisor;
	unsigned int	min_baud;
	unsigned int	max_baud;
};

struct w90x900_uartcfg {
	unsigned char	hwport;
	unsigned char	unused;
	unsigned short	flags;
	unsigned long	uart_flags;

	unsigned long	ucon;
	unsigned long	ulcon;
	unsigned long	ufcon;

	struct w90x900_uart_clksrc *clocks;
	unsigned int	clocks_size;
};

#endif /* __ASSEMBLY__ */

#endif /* __ASM_ARM_REGS_SERIAL_H */

