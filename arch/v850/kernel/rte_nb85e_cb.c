/*
 * arch/v850/kernel/rte_nb85e_cb.c -- Midas labs RTE-V850E/NB85E-CB board
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/bootmem.h>
#include <linux/irq.h>

#include <asm/atomic.h>
#include <asm/page.h>
#include <asm/v850e.h>
#include <asm/rte_nb85e_cb.h>

#include "mach.h"

void __init mach_early_init (void)
{
	/* Configure caching; some possible settings:

	     BHC = 0x0000, DCC = 0x0000	 -- all caching disabled
	     BHC = 0x0040, DCC = 0x0000	 -- SDRAM: icache only
	     BHC = 0x0080, DCC = 0x0C00	 -- SDRAM: write-back dcache only
	     BHC = 0x00C0, DCC = 0x0C00	 -- SDRAM: icache + write-back dcache
	     BHC = 0x00C0, DCC = 0x0800	 -- SDRAM: icache + write-thru dcache

	   We can only cache SDRAM (we can't use cache SRAM because it's in
	   the same memory region as the on-chip RAM and I/O space).

	   Unfortunately, the dcache seems to be buggy, so we only use the
	   icache for now.  */
	v850e_cache_enable (0x0040 /*BHC*/, 0x0003 /*ICC*/, 0x0000 /*DCC*/);

	rte_cb_early_init ();
}

void __init mach_get_physical_ram (unsigned long *ram_start,
				   unsigned long *ram_len)
{
	/* We just use SDRAM here.  */
	*ram_start = SDRAM_ADDR;
	*ram_len = SDRAM_SIZE;
}

void mach_gettimeofday (struct timespec *tv)
{
	tv->tv_sec = 0;
	tv->tv_nsec = 0;
}

/* Called before configuring an on-chip UART.  */
void rte_nb85e_cb_uart_pre_configure (unsigned chan,
				    unsigned cflags, unsigned baud)
{
	/* The RTE-NB85E-CB connects some general-purpose I/O pins on the
	   CPU to the RTS/CTS lines the UART's serial connection, as follows:
	   P00 = CTS (in), P01 = DSR (in), P02 = RTS (out), P03 = DTR (out). */

	TEG_PORT0_PM = 0x03;	/* P00 and P01 inputs, P02 and P03 outputs */
	TEG_PORT0_IO = 0x03;	/* Accept input */

	/* Do pre-configuration for the actual UART.  */
	teg_uart_pre_configure (chan, cflags, baud);
}

void __init mach_init_irqs (void)
{
	teg_init_irqs ();
	rte_cb_init_irqs ();
}
