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
#include <asm/io.h>
#include <platforms/cpci690.h>

#define	KB	(1024UL)
#define	MB	(1024UL*KB)
#define	GB	(1024UL*MB)

extern u32 mv64x60_console_baud;
extern u32 mv64x60_mpsc_clk_src;
extern u32 mv64x60_mpsc_clk_freq;

u32 mag = 0xffff;

unsigned long
get_mem_size(void)
{
	u32	size;

	switch (in_8(((void __iomem *)CPCI690_BR_BASE + CPCI690_BR_MEM_CTLR))
			& 0x07) {
	case 0x01:
		size = 256*MB;
		break;
	case 0x02:
		size = 512*MB;
		break;
	case 0x03:
		size = 768*MB;
		break;
	case 0x04:
		size = 1*GB;
		break;
	case 0x05:
		size = 1*GB + 512*MB;
		break;
	case 0x06:
		size = 2*GB;
		break;
	default:
		size = 0;
	}

	return size;
}

void
mv64x60_board_init(void __iomem *old_base, void __iomem *new_base)
{
	mv64x60_console_baud = CPCI690_MPSC_BAUD;
	mv64x60_mpsc_clk_src = CPCI690_MPSC_CLK_SRC;
	mv64x60_mpsc_clk_freq =
		(get_mem_size() >= (1*GB)) ? 100000000 : 133333333;
}
