/*
 * arch/v850/kernel/anna.c -- Anna V850E2 evaluation chip/board
 *
 *  Copyright (C) 2002,03  NEC Electronics Corporation
 *  Copyright (C) 2002,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/major.h>
#include <linux/irq.h>

#include <asm/machdep.h>
#include <asm/atomic.h>
#include <asm/page.h>
#include <asm/v850e_timer_d.h>
#include <asm/v850e_uart.h>

#include "mach.h"


/* SRAM and SDRAM are vaguely contiguous (with a big hole in between; see
   mach_reserve_bootmem for details); use both as one big area.  */
#define RAM_START 	SRAM_ADDR
#define RAM_END		(SDRAM_ADDR + SDRAM_SIZE)

/* The bits of this port are connected to an 8-LED bar-graph.  */
#define LEDS_PORT	0


static void anna_led_tick (void);


void __init mach_early_init (void)
{
	ANNA_ILBEN    = 0;

	V850E2_CSC(0) = 0x402F;
	V850E2_CSC(1) = 0x4000;
	V850E2_BPC    = 0;
	V850E2_BSC    = 0xAAAA;
	V850E2_BEC    = 0;

#if 0
	V850E2_BHC    = 0xFFFF;	/* icache all memory, dcache all */
#else
	V850E2_BHC    = 0;	/* cache no memory */
#endif
	V850E2_BCT(0) = 0xB088;
	V850E2_BCT(1) = 0x0008;
	V850E2_DWC(0) = 0x0027;
	V850E2_DWC(1) = 0;
	V850E2_BCC    = 0x0006;
	V850E2_ASC    = 0;
	V850E2_LBS    = 0x0089;
	V850E2_SCR(3) = 0x21A9;
	V850E2_RFS(3) = 0x8121;

	v850e_intc_disable_irqs ();
}

void __init mach_setup (char **cmdline)
{
	ANNA_PORT_PM (LEDS_PORT) = 0;	/* Make all LED pins output pins.  */
	mach_tick = anna_led_tick;
}

void __init mach_get_physical_ram (unsigned long *ram_start,
				   unsigned long *ram_len)
{
	*ram_start = RAM_START;
	*ram_len = RAM_END - RAM_START;
}

void __init mach_reserve_bootmem ()
{
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

void __init mach_sched_init (struct irqaction *timer_action)
{
	/* Start hardware timer.  */
	v850e_timer_d_configure (0, HZ);
	/* Install timer interrupt handler.  */
	setup_irq (IRQ_INTCMD(0), timer_action);
}

static struct v850e_intc_irq_init irq_inits[] = {
	{ "IRQ", 0, 		NUM_MACH_IRQS,	1, 7 },
	{ "PIN", IRQ_INTP(0),   IRQ_INTP_NUM,   1, 4 },
	{ "CCC", IRQ_INTCCC(0),	IRQ_INTCCC_NUM, 1, 5 },
	{ "CMD", IRQ_INTCMD(0), IRQ_INTCMD_NUM,	1, 5 },
	{ "DMA", IRQ_INTDMA(0), IRQ_INTDMA_NUM,	1, 2 },
	{ "DMXER", IRQ_INTDMXER,1,		1, 2 },
	{ "SRE", IRQ_INTSRE(0), IRQ_INTSRE_NUM,	3, 3 },
	{ "SR",	 IRQ_INTSR(0),	IRQ_INTSR_NUM, 	3, 4 },
	{ "ST",  IRQ_INTST(0), 	IRQ_INTST_NUM, 	3, 5 },
	{ 0 }
};
#define NUM_IRQ_INITS ((sizeof irq_inits / sizeof irq_inits[0]) - 1)

static struct hw_interrupt_type hw_itypes[NUM_IRQ_INITS];

void __init mach_init_irqs (void)
{
	v850e_intc_init_irq_types (irq_inits, hw_itypes);
}

void machine_restart (char *__unused)
{
#ifdef CONFIG_RESET_GUARD
	disable_reset_guard ();
#endif
	asm ("jmp r0"); /* Jump to the reset vector.  */
}

EXPORT_SYMBOL(machine_restart);

void machine_halt (void)
{
#ifdef CONFIG_RESET_GUARD
	disable_reset_guard ();
#endif
	local_irq_disable ();	/* Ignore all interrupts.  */
	ANNA_PORT_IO(LEDS_PORT) = 0xAA;	/* Note that we halted.  */
	for (;;)
		asm ("halt; nop; nop; nop; nop; nop");
}

EXPORT_SYMBOL(machine_halt);

void machine_power_off (void)
{
	machine_halt ();
}

EXPORT_SYMBOL(machine_power_off);

/* Called before configuring an on-chip UART.  */
void anna_uart_pre_configure (unsigned chan, unsigned cflags, unsigned baud)
{
	/* The Anna connects some general-purpose I/O pins on the CPU to
	   the RTS/CTS lines of UART 1's serial connection.  I/O pins P07
	   and P37 are RTS and CTS respectively.  */
	if (chan == 1) {
		ANNA_PORT_PM(0) &= ~0x80; /* P07 in output mode */
		ANNA_PORT_PM(3) |=  0x80; /* P37 in input mode */
	}
}

/* Minimum and maximum bounds for the moving upper LED boundary in the
   clock tick display.  We can't use the last bit because it's used for
   UART0's CTS output.  */
#define MIN_MAX_POS 0
#define MAX_MAX_POS 6

/* There are MAX_MAX_POS^2 - MIN_MAX_POS^2 cycles in the animation, so if
   we pick 6 and 0 as above, we get 49 cycles, which is when divided into
   the standard 100 value for HZ, gives us an almost 1s total time.  */
#define TICKS_PER_FRAME \
	(HZ / (MAX_MAX_POS * MAX_MAX_POS - MIN_MAX_POS * MIN_MAX_POS))

static void anna_led_tick ()
{
	static unsigned counter = 0;
	
	if (++counter == TICKS_PER_FRAME) {
		static int pos = 0, max_pos = MAX_MAX_POS, dir = 1;

		if (dir > 0 && pos == max_pos) {
			dir = -1;
			if (max_pos == MIN_MAX_POS)
				max_pos = MAX_MAX_POS;
			else
				max_pos--;
		} else {
			if (dir < 0 && pos == 0)
				dir = 1;

			if (pos + dir <= max_pos) {
				/* Each bit of port 0 has a LED. */
				clear_bit (pos, &ANNA_PORT_IO(LEDS_PORT));
				pos += dir;
				set_bit (pos, &ANNA_PORT_IO(LEDS_PORT));
			}
		}

		counter = 0;
	}
}
