/*
 * Misc data for Radstone PPC7D board.
 *
 * Author: James Chapman <jchapman@katalix.com>
 */

#include <linux/types.h>
#include <platforms/radstone_ppc7d.h>

#if defined(CONFIG_SERIAL_MPSC_CONSOLE)
extern u32 mv64x60_console_baud;
extern u32 mv64x60_mpsc_clk_src;
extern u32 mv64x60_mpsc_clk_freq;
#endif

void
mv64x60_board_init(void __iomem *old_base, void __iomem *new_base)
{
#if defined(CONFIG_SERIAL_MPSC_CONSOLE)
	mv64x60_console_baud = PPC7D_DEFAULT_BAUD;
	mv64x60_mpsc_clk_src = PPC7D_MPSC_CLK_SRC;
	mv64x60_mpsc_clk_freq = PPC7D_MPSC_CLK_FREQ;
#endif
}
