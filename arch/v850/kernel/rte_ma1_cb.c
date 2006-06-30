/*
 * arch/v850/kernel/rte_ma1_cb.c -- Midas labs RTE-V850E/MA1-CB board
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/atomic.h>
#include <asm/page.h>
#include <asm/ma1.h>
#include <asm/rte_ma1_cb.h>
#include <asm/v850e_timer_c.h>

#include "mach.h"


/* SRAM and SDRAM are almost contiguous (with a small hole in between;
   see mach_reserve_bootmem for details), so just use both as one big area.  */
#define RAM_START 	SRAM_ADDR
#define RAM_END		(SDRAM_ADDR + SDRAM_SIZE)


void __init mach_early_init (void)
{
	rte_cb_early_init ();
}

void __init mach_get_physical_ram (unsigned long *ram_start,
				   unsigned long *ram_len)
{
	*ram_start = RAM_START;
	*ram_len = RAM_END - RAM_START;
}

void __init mach_reserve_bootmem ()
{
#ifdef CONFIG_RTE_CB_MULTI
	/* Prevent the kernel from touching the monitor's scratch RAM.  */
	reserve_bootmem (MON_SCRATCH_ADDR, MON_SCRATCH_SIZE);
#endif

	/* The space between SRAM and SDRAM is filled with duplicate
	   images of SRAM.  Prevent the kernel from using them.  */
	reserve_bootmem (SRAM_ADDR + SRAM_SIZE,
			 SDRAM_ADDR - (SRAM_ADDR + SRAM_SIZE));
}

void mach_gettimeofday (struct timespec *tv)
{
	tv->tv_sec = 0;
	tv->tv_nsec = 0;
}

/* Called before configuring an on-chip UART.  */
void rte_ma1_cb_uart_pre_configure (unsigned chan,
				    unsigned cflags, unsigned baud)
{
	/* The RTE-MA1-CB connects some general-purpose I/O pins on the
	   CPU to the RTS/CTS lines of UART 0's serial connection.
	   I/O pins P42 and P43 are RTS and CTS respectively.  */
	if (chan == 0) {
		/* Put P42 & P43 in I/O port mode.  */
		MA_PORT4_PMC &= ~0xC;
		/* Make P42 an output, and P43 an input.  */
		MA_PORT4_PM = (MA_PORT4_PM & ~0xC) | 0x8;
	}

	/* Do pre-configuration for the actual UART.  */
	ma_uart_pre_configure (chan, cflags, baud);
}

void __init mach_init_irqs (void)
{
	unsigned tc;

	/* Initialize interrupts.  */
	ma_init_irqs ();
	rte_cb_init_irqs ();

	/* Use falling-edge-sensitivity for interrupts .  */
	V850E_TIMER_C_SESC (0) &= ~0xC;
	V850E_TIMER_C_SESC (1) &= ~0xF;

	/* INTP000-INTP011 are shared with `Timer C', so we have to set
	   up Timer C to pass them through as raw interrupts.  */
	for (tc = 0; tc < 2; tc++)
		/* Turn on the timer.  */
		V850E_TIMER_C_TMCC0 (tc) |= V850E_TIMER_C_TMCC0_CAE;

	/* Make sure the relevant port0/port1 pins are assigned
	   interrupt duty.  We used INTP001-INTP011 (don't screw with
	   INTP000 because the monitor uses it).  */
	MA_PORT0_PMC |= 0x4;	/* P02 (INTP001) in IRQ mode.  */
	MA_PORT1_PMC |= 0x6;	/* P11 (INTP010) & P12 (INTP011) in IRQ mode.*/
}
