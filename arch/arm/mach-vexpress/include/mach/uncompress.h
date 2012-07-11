/*
 *  arch/arm/mach-vexpress/include/mach/uncompress.h
 *
 *  Copyright (C) 2003 ARM Limited
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
#define AMBA_UART_DR(base)	(*(volatile unsigned char *)((base) + 0x00))
#define AMBA_UART_LCRH(base)	(*(volatile unsigned char *)((base) + 0x2c))
#define AMBA_UART_CR(base)	(*(volatile unsigned char *)((base) + 0x30))
#define AMBA_UART_FR(base)	(*(volatile unsigned char *)((base) + 0x18))

#define UART_BASE	0x10009000
#define UART_BASE_RS1	0x1c090000

static unsigned long get_uart_base(void)
{
#if defined(CONFIG_DEBUG_VEXPRESS_UART0_DETECT)
	unsigned long mpcore_periph;

	/*
	 * Make an educated guess regarding the memory map:
	 * - the original A9 core tile, which has MPCore peripherals
	 *   located at 0x1e000000, should use UART at 0x10009000
	 * - all other (RS1 complaint) tiles use UART mapped
	 *   at 0x1c090000
	 */
	asm("mrc p15, 4, %0, c15, c0, 0" : "=r" (mpcore_periph));

	if (mpcore_periph == 0x1e000000)
		return UART_BASE;
	else
		return UART_BASE_RS1;
#elif defined(CONFIG_DEBUG_VEXPRESS_UART0_CA9)
	return UART_BASE;
#elif defined(CONFIG_DEBUG_VEXPRESS_UART0_RS1)
	return UART_BASE_RS1;
#else
	return 0;
#endif
}

/*
 * This does not append a newline
 */
static inline void putc(int c)
{
	unsigned long base = get_uart_base();

	if (!base)
		return;

	while (AMBA_UART_FR(base) & (1 << 5))
		barrier();

	AMBA_UART_DR(base) = c;
}

static inline void flush(void)
{
	unsigned long base = get_uart_base();

	if (!base)
		return;

	while (AMBA_UART_FR(base) & (1 << 3))
		barrier();
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()
