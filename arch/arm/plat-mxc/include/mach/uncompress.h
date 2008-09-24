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

#define UART(x) (*(volatile unsigned long *)(serial_port + (x)))

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
	static unsigned long serial_port = 0;

	if (unlikely(serial_port == 0)) {
		do {
			serial_port = UART1_BASE_ADDR;
			if (UART(UCR1) & UCR1_UARTEN)
				break;
			serial_port = UART2_BASE_ADDR;
			if (UART(UCR1) & UCR1_UARTEN)
				break;
			return;
		} while (0);
	}

	while (!(UART(USR2) & USR2_TXFE))
		barrier();

	UART(TXR) = ch;
}

#define flush() do { } while (0)

/*
 * nothing to do
 */
#define arch_decomp_setup()

#define arch_decomp_wdog()

#endif				/* __ASM_ARCH_MXC_UNCOMPRESS_H__ */
