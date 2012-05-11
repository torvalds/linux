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
#include <asm/hardware/clps7111.h>

#ifdef CONFIG_DEBUG_CLPS711X_UART2
#define SYSFLGx	SYSFLG2
#define UARTDRx	UARTDR2
#else
#define SYSFLGx	SYSFLG1
#define UARTDRx	UARTDR1
#endif

#define phys_reg(x)	(*(volatile u32 *)(CLPS711X_PHYS_BASE + (x)))

/*
 * The following code assumes the serial port has already been
 * initialized by the bootloader.  If you didn't setup a port in
 * your bootloader then nothing will appear (which might be desired).
 *
 * This does not append a newline
 */
static inline void putc(int c)
{
	while (phys_reg(SYSFLGx) & SYSFLG_UTXFF)
		barrier();
	phys_reg(UARTDRx) = c;
}

static inline void flush(void)
{
	while (phys_reg(SYSFLGx) & SYSFLG_UBUSY)
		barrier();
}

/*
 * nothing to do
 */
#define arch_decomp_setup()

#define arch_decomp_wdog()
