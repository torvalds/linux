/*
 *  arch/arm/plat-mxc/include/mach/uncompress.h
 *
 *
 *
 *  Copyright (C) 1999 ARM Limited
 *  Copyright (C) Shane Nay (shane@minirl.com)
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
#ifndef __ASM_ARCH_MXC_UNCOMPRESS_H__
#define __ASM_ARCH_MXC_UNCOMPRESS_H__

#define __MXC_BOOT_UNCOMPRESS

#include <mach/hardware.h>
#include <asm/mach-types.h>

static unsigned long uart_base;

#define UART(x) (*(volatile unsigned long *)(uart_base + (x)))

#define USR2 0x98
#define USR2_TXFE (1<<14)
#define TXR  0x40
#define UCR1 0x80
#define UCR1_UARTEN 1

/*
 * The following code assumes the serial port has already been
 * initialized by the bootloader.  We search for the first enabled
 * port in the most probable order.  If you didn't setup a port in
 * your bootloader then nothing will appear (which might be desired).
 *
 * This does not append a newline
 */

static void putc(int ch)
{
	if (!uart_base)
		return;
	if (!(UART(UCR1) & UCR1_UARTEN))
		return;

	while (!(UART(USR2) & USR2_TXFE))
		barrier();

	UART(TXR) = ch;
}

#define flush() do { } while (0)

#define MX1_UART1_BASE_ADDR	0x00206000
#define MX25_UART1_BASE_ADDR	0x43f90000
#define MX2X_UART1_BASE_ADDR	0x1000a000
#define MX3X_UART1_BASE_ADDR	0x43F90000
#define MX3X_UART2_BASE_ADDR	0x43F94000

static __inline__ void __arch_decomp_setup(unsigned long arch_id)
{
	switch (arch_id) {
	case MACH_TYPE_MX1ADS:
	case MACH_TYPE_SCB9328:
		uart_base = MX1_UART1_BASE_ADDR;
		break;
	case MACH_TYPE_MX25_3DS:
		uart_base = MX25_UART1_BASE_ADDR;
		break;
	case MACH_TYPE_IMX27LITE:
	case MACH_TYPE_MX27_3DS:
	case MACH_TYPE_MX27ADS:
	case MACH_TYPE_PCM038:
	case MACH_TYPE_MX21ADS:
		uart_base = MX2X_UART1_BASE_ADDR;
		break;
	case MACH_TYPE_MX31LITE:
	case MACH_TYPE_ARMADILLO5X0:
	case MACH_TYPE_MX31MOBOARD:
	case MACH_TYPE_QONG:
	case MACH_TYPE_MX31_3DS:
	case MACH_TYPE_PCM037:
	case MACH_TYPE_MX31ADS:
	case MACH_TYPE_MX35_3DS:
	case MACH_TYPE_PCM043:
		uart_base = MX3X_UART1_BASE_ADDR;
		break;
	case MACH_TYPE_MAGX_ZN5:
		uart_base = MX3X_UART2_BASE_ADDR;
		break;
	default:
		break;
	}
}

#define arch_decomp_setup()	__arch_decomp_setup(arch_id)
#define arch_decomp_wdog()

#endif				/* __ASM_ARCH_MXC_UNCOMPRESS_H__ */
