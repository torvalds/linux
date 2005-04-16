/*
 * arch/ppc/boot/simple/misc-cpci690.c
 *
 * Add birec data for Force CPCI690 board.
 *
 * Author: Mark A. Greer <source@mvista.com>
 *
 * 2003 (c) MontaVista Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/types.h>
#include <platforms/cpci690.h>

extern u32 mv64x60_console_baud;
extern u32 mv64x60_mpsc_clk_src;
extern u32 mv64x60_mpsc_clk_freq;

void
mv64x60_board_init(void __iomem *old_base, void __iomem *new_base)
{
	mv64x60_console_baud = CPCI690_MPSC_BAUD;
	mv64x60_mpsc_clk_src = CPCI690_MPSC_CLK_SRC;
	mv64x60_mpsc_clk_freq = CPCI690_BUS_FREQ;
}
