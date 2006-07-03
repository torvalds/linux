/*
 * Host bridge init code for the Marvell/Galileo EV-64260-BP evaluation board
 * with a GT64260 onboard.
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2001 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/types.h>
#include <asm/reg.h>
#include <asm/io.h>
#include <asm/mv64x60_defs.h>
#include <platforms/ev64260.h>

#ifdef CONFIG_SERIAL_MPSC_CONSOLE
extern u32 mv64x60_console_baud;
extern u32 mv64x60_mpsc_clk_src;
extern u32 mv64x60_mpsc_clk_freq;
#endif

void
mv64x60_board_init(void __iomem *old_base, void __iomem *new_base)
{
	u32	p, v;

	/* DINK doesn't enable 745x timebase, so enable here (Adrian Cox) */
	p = mfspr(SPRN_PVR);
	p >>= 16;

	/* Reasonable SWAG at a 745x PVR value */
	if (((p & 0xfff0) == 0x8000) && (p != 0x800c)) {
		v = mfspr(SPRN_HID0);
		v |= HID0_TBEN;
		mtspr(SPRN_HID0, v);
	}

#ifdef CONFIG_SERIAL_8250_CONSOLE
	/*
	 * Change device bus 2 window so that bootoader can do I/O thru
	 * 8250/16550 UART that's mapped in that window.
	 */
	out_le32(new_base + MV64x60_CPU2DEV_2_BASE, EV64260_UART_BASE >> 20);
	out_le32(new_base + MV64x60_CPU2DEV_2_SIZE, EV64260_UART_END >> 20);
	__asm__ __volatile__("sync");
#elif defined(CONFIG_SERIAL_MPSC_CONSOLE)
	mv64x60_console_baud = EV64260_DEFAULT_BAUD;
	mv64x60_mpsc_clk_src = EV64260_MPSC_CLK_SRC;
	mv64x60_mpsc_clk_freq = EV64260_MPSC_CLK_FREQ;
#endif
}
