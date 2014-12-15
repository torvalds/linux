/*
 * arch/arm/mach-meson/include/mach/uncompress.h
 *
 *  Copyright (C) 2010 AMLOGIC, INC.
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

#define UART0_RFIFO     (*(volatile unsigned int *)0xc11084c0)
#define UART0_WFIFO     (*(volatile unsigned int *)0xc11084c4)
#define UART0_CONTROL   (*(volatile unsigned int *)0xc11084c8)
#define UART0_STATUS    (*(volatile unsigned int *)0xc11084cc)

/*
 * This does not append a newline
 */
static inline void putc(int c)
{
    /* wait TX FIFO not full */
    while (UART0_STATUS & (1 << 21)) {
        barrier();
    }

    UART0_WFIFO = c;
}

static inline void flush(void)
{
    /* wait TX FIFO not empty */
    while ((UART0_STATUS & (1 << 22)) == 0) {
        barrier();
    }
}

static inline void arch_decomp_setup(void)
{
    /* 115200 bps at 180M MPEG clock */
    UART0_CONTROL = 0x185;
}

/*
 * nothing to do
 */
#define arch_decomp_wdog()
