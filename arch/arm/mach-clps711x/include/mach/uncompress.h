/*
 *  arch/arm/mach-clps711x/include/mach/uncompress.h
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <mach/io.h>
#include <mach/hardware.h>
#include <asm/hardware/clps7111.h>

#undef CLPS7111_BASE
#define CLPS7111_BASE CLPS7111_PHYS_BASE

#define __raw_readl(p)		(*(unsigned long *)(p))
#define __raw_writel(v,p)	(*(unsigned long *)(p) = (v))

#ifdef CONFIG_DEBUG_CLPS711X_UART2
#define SYSFLGx	SYSFLG2
#define UARTDRx	UARTDR2
#else
#define SYSFLGx	SYSFLG1
#define UARTDRx	UARTDR1
#endif

/*
 * This does not append a newline
 */
static inline void putc(int c)
{
	while (clps_readl(SYSFLGx) & SYSFLG_UTXFF)
		barrier();
	clps_writel(c, UARTDRx);
}

static inline void flush(void)
{
	while (clps_readl(SYSFLGx) & SYSFLG_UBUSY)
		barrier();
}

/*
 * nothing to do
 */
#define arch_decomp_setup()

#define arch_decomp_wdog()
