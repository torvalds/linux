/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Putting things on the screen/serial line using YAMONs facilities.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/serial_reg.h>
#include <linux/spinlock.h>
#include <asm/io.h>

#ifdef CONFIG_MIPS_ATLAS
#include <asm/mips-boards/atlas.h>

#ifdef CONFIG_CPU_LITTLE_ENDIAN
#define PORT(offset) (ATLAS_UART_REGS_BASE     + ((offset)<<3))
#else
#define PORT(offset) (ATLAS_UART_REGS_BASE + 3 + ((offset)<<3))
#endif

#elif defined(CONFIG_MIPS_SEAD)

#include <asm/mips-boards/sead.h>

#ifdef CONFIG_CPU_LITTLE_ENDIAN
#define PORT(offset) (SEAD_UART0_REGS_BASE     + ((offset)<<3))
#else
#define PORT(offset) (SEAD_UART0_REGS_BASE + 3 + ((offset)<<3))
#endif

#else

#define PORT(offset) (0x3f8 + (offset))

#endif

static inline unsigned int serial_in(int offset)
{
	return inb(PORT(offset));
}

static inline void serial_out(int offset, int value)
{
	outb(value, PORT(offset));
}

int prom_putchar(char c)
{
	while ((serial_in(UART_LSR) & UART_LSR_THRE) == 0)
		;

	serial_out(UART_TX, c);

	return 1;
}

char prom_getchar(void)
{
	while (!(serial_in(UART_LSR) & UART_LSR_DR))
		;

	return serial_in(UART_RX);
}

