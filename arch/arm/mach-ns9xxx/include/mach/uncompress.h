/*
 * arch/arm/mach-ns9xxx/include/mach/uncompress.h
 *
 * Copyright (C) 2006 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef __ASM_ARCH_UNCOMPRESS_H
#define __ASM_ARCH_UNCOMPRESS_H

#include <linux/io.h>

#define __REG(x)	((void __iomem __force *)(x))

static void putc_dummy(char c, void __iomem *base)
{
	/* nothing */
}

static int timeout;

static void putc_ns9360(char c, void __iomem *base)
{
	do {
		if (timeout)
			--timeout;

		if (__raw_readl(base + 8) & (1 << 3)) {
			__raw_writeb(c, base + 16);
			timeout = 0x10000;
			break;
		}
	} while (timeout);
}

static void putc_a9m9750dev(char c, void __iomem *base)
{
	do {
		if (timeout)
			--timeout;

		if (__raw_readb(base + 5) & (1 << 5)) {
			__raw_writeb(c, base);
			timeout = 0x10000;
			break;
		}
	} while (timeout);

}

static void putc_ns921x(char c, void __iomem *base)
{
	do {
		if (timeout)
			--timeout;

		if (!(__raw_readl(base) & (1 << 11))) {
			__raw_writeb(c, base + 0x0028);
			timeout = 0x10000;
			break;
		}
	} while (timeout);
}

#define MSCS __REG(0xA0900184)

#define NS9360_UARTA	__REG(0x90200040)
#define NS9360_UARTB	__REG(0x90200000)
#define NS9360_UARTC	__REG(0x90300000)
#define NS9360_UARTD	__REG(0x90300040)

#define NS9360_UART_ENABLED(base)					\
		(__raw_readl(NS9360_UARTA) & (1 << 31))

#define A9M9750DEV_UARTA	__REG(0x40000000)

#define NS921XSYS_CLOCK	__REG(0xa090017c)
#define NS921X_UARTA	__REG(0x90010000)
#define NS921X_UARTB	__REG(0x90018000)
#define NS921X_UARTC	__REG(0x90020000)
#define NS921X_UARTD	__REG(0x90028000)

#define NS921X_UART_ENABLED(base)					\
		(__raw_readl((base) + 0x1000) & (1 << 29))

static void autodetect(void (**putc)(char, void __iomem *), void __iomem **base)
{
	timeout = 0x10000;
	if (((__raw_readl(MSCS) >> 16) & 0xfe) == 0x00) {
		/* ns9360 or ns9750 */
		if (NS9360_UART_ENABLED(NS9360_UARTA)) {
			*putc = putc_ns9360;
			*base = NS9360_UARTA;
			return;
		} else if (NS9360_UART_ENABLED(NS9360_UARTB)) {
			*putc = putc_ns9360;
			*base = NS9360_UARTB;
			return;
		} else if (NS9360_UART_ENABLED(NS9360_UARTC)) {
			*putc = putc_ns9360;
			*base = NS9360_UARTC;
			return;
		} else if (NS9360_UART_ENABLED(NS9360_UARTD)) {
			*putc = putc_ns9360;
			*base = NS9360_UARTD;
			return;
		} else if (__raw_readl(__REG(0xa09001f4)) == 0xfffff001) {
			*putc = putc_a9m9750dev;
			*base = A9M9750DEV_UARTA;
			return;
		}
	} else if (((__raw_readl(MSCS) >> 16) & 0xfe) == 0x02) {
		/* ns921x */
		u32 clock = __raw_readl(NS921XSYS_CLOCK);

		if ((clock & (1 << 1)) &&
				NS921X_UART_ENABLED(NS921X_UARTA)) {
			*putc = putc_ns921x;
			*base = NS921X_UARTA;
			return;
		} else if ((clock & (1 << 2)) &&
				NS921X_UART_ENABLED(NS921X_UARTB)) {
			*putc = putc_ns921x;
			*base = NS921X_UARTB;
			return;
		} else if ((clock & (1 << 3)) &&
				NS921X_UART_ENABLED(NS921X_UARTC)) {
			*putc = putc_ns921x;
			*base = NS921X_UARTC;
			return;
		} else if ((clock & (1 << 4)) &&
				NS921X_UART_ENABLED(NS921X_UARTD)) {
			*putc = putc_ns921x;
			*base = NS921X_UARTD;
			return;
		}
	}

	*putc = putc_dummy;
}

void (*myputc)(char, void __iomem *);
void __iomem *base;

static void putc(char c)
{
	myputc(c, base);
}

static void arch_decomp_setup(void)
{
	autodetect(&myputc, &base);
}
#define arch_decomp_wdog()

static void flush(void)
{
	/* nothing */
}

#endif /* ifndef __ASM_ARCH_UNCOMPRESS_H */
