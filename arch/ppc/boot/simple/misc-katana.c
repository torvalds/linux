/*
 * Set up MPSC values to bootwrapper can prompt user.
 *
 * Author: Mark A. Greer <source@mvista.com>
 *
 * 2004 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <asm/io.h>
#include <asm/mv64x60_defs.h>
#include <platforms/katana.h>

extern u32 mv64x60_console_baud;
extern u32 mv64x60_mpsc_clk_src;
extern u32 mv64x60_mpsc_clk_freq;

/* Not in the kernel so won't include kernel.h to get its 'min' definition */
#ifndef min
#define	min(a,b)	(((a) < (b)) ? (a) : (b))
#endif

unsigned long mv64360_get_mem_size(void);

void
mv64x60_board_init(void __iomem *old_base, void __iomem *new_base)
{
	mv64x60_console_baud = KATANA_DEFAULT_BAUD;
	mv64x60_mpsc_clk_src = KATANA_MPSC_CLK_SRC;
	mv64x60_mpsc_clk_freq =
		min(katana_bus_freq((void __iomem *)KATANA_CPLD_BASE),
			MV64x60_TCLK_FREQ_MAX);
}

unsigned long
get_mem_size(void)
{
	return mv64360_get_mem_size();
}
