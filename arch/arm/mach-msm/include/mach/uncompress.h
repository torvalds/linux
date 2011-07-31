/* arch/arm/mach-msm/include/mach/uncompress.h
 *
 * Copyright (C) 2007 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ASM_ARCH_MSM_UNCOMPRESS_H

#include "hardware.h"
#include "linux/io.h"
#include "mach/msm_iomap.h"

static void putc(int c)
{
#if defined(MSM_DEBUG_UART_PHYS)
	unsigned base = MSM_DEBUG_UART_PHYS;
	while (!(readl(base + 0x08) & 0x04)) ;
	writel(c, base + 0x0c);
#endif
}

static inline void flush(void)
{
}

static inline void arch_decomp_setup(void)
{
}

static inline void arch_decomp_wdog(void)
{
}

#endif
