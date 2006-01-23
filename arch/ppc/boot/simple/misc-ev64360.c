/*
 * Copyright (C) 2005 Lee Nicks <allinux@gmail.com>
 *
 * Based on arch/ppc/boot/simple/misc-katana.c from:
 * Mark A. Greer <source@mvista.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <asm/io.h>
#include <asm/mv64x60_defs.h>
#include <platforms/ev64360.h>

extern u32 mv64x60_console_baud;
extern u32 mv64x60_mpsc_clk_src;
extern u32 mv64x60_mpsc_clk_freq;

/* Not in the kernel so won't include kernel.h to get its 'min' definition */
#ifndef min
#define	min(a,b)	(((a) < (b)) ? (a) : (b))
#endif

void
mv64x60_board_init(void __iomem *old_base, void __iomem *new_base)
{
	mv64x60_console_baud  = EV64360_DEFAULT_BAUD;
	mv64x60_mpsc_clk_src  = EV64360_MPSC_CLK_SRC;
	mv64x60_mpsc_clk_freq = EV64360_MPSC_CLK_FREQ;
}
