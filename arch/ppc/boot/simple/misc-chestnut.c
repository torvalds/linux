/*
 * arch/ppc/boot/simple/misc-chestnut.c
 *
 * Setup for the IBM Chestnut (ibm-750fxgx_eval)
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <asm/io.h>
#include <asm/mv64x60_defs.h>
#include <platforms/chestnut.h>

/* Not in the kernel so won't include kernel.h to get its 'max' definition */
#define max(a,b)	(((a) > (b)) ? (a) : (b))

void
mv64x60_board_init(void __iomem *old_base, void __iomem *new_base)
{
#ifdef CONFIG_SERIAL_8250_CONSOLE
	/*
	 * Change device bus 2 window so that bootoader can do I/O thru
	 * 8250/16550 UART that's mapped in that window.
	 */
	out_le32(new_base + MV64x60_CPU2DEV_2_BASE, CHESTNUT_UART_BASE >> 16);
	out_le32(new_base + MV64x60_CPU2DEV_2_SIZE, CHESTNUT_UART_SIZE >> 16);
	__asm__ __volatile__("sync");
#endif
}
