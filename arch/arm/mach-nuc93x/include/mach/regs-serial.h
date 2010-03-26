/*
 * arch/arm/mach-nuc93x/include/mach/regs-serial.h
 *
 * Copyright (c) 2008 Nuvoton technology corporation
 * All rights reserved.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __ASM_ARM_REGS_SERIAL_H
#define __ASM_ARM_REGS_SERIAL_H

#define UART0_BA	NUC93X_VA_UART
#define UART1_BA	(NUC93X_VA_UART+0x100)

#define UART0_PA	NUC93X_PA_UART
#define UART1_PA	(NUC93X_PA_UART+0x100)


#ifndef __ASSEMBLY__

struct nuc93x_uart_clksrc {
	const char	*name;
	unsigned int	divisor;
	unsigned int	min_baud;
	unsigned int	max_baud;
};

struct nuc93x_uartcfg {
	unsigned char	hwport;
	unsigned char	unused;
	unsigned short	flags;
	unsigned long	uart_flags;

	unsigned long	ucon;
	unsigned long	ulcon;
	unsigned long	ufcon;

	struct nuc93x_uart_clksrc *clocks;
	unsigned int	clocks_size;
};

#endif /* __ASSEMBLY__ */

#endif /* __ASM_ARM_REGS_SERIAL_H */

