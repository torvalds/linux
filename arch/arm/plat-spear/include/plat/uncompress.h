/*
 * arch/arm/plat-spear/include/plat/uncompress.h
 *
 * Serial port stubs for kernel decompress status messages
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/io.h>
#include <linux/amba/serial.h>
#include <mach/spear.h>

#ifndef __PLAT_UNCOMPRESS_H
#define __PLAT_UNCOMPRESS_H
/*
 * This does not append a newline
 */
static inline void putc(int c)
{
	void __iomem *base = (void __iomem *)SPEAR_DBG_UART_BASE;

	while (readl(base + UART01x_FR) & UART01x_FR_TXFF)
		barrier();

	writel(c, base + UART01x_DR);
}

static inline void flush(void)
{
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()

#endif /* __PLAT_UNCOMPRESS_H */
