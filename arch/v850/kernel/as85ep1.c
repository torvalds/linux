/*
 * arch/v850/kernel/as85ep1.c -- AS85EP1 V850E evaluation chip/board
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
#define LEDS_PORT	4


static void as85ep1_led_tick (void);

extern char _intv_copy_src_start, _intv_copy_src_end;
extern char _intv_copy_dst_start;


void __init mach_early_init (void)
{
#ifndef CONFIG_ROM_KERNEL
	const u32 *src;
	register u32 *dst asm ("ep");
#endif

	AS85EP1_CSC(0) = 0x0403;
	AS85EP1_BCT(0) = 0xB8B8;
	AS85EP1_DWC(0) = 0x0104;
	AS85EP1_BCC    = 0x0012;
	AS85EP1_ASC    = 0;
	AS85EP1_LBS    = 0x00A9;

	AS85EP1_PORT_PMC(6)  = 0xFF; /* valid A0,A1,A20-A25 */
	AS85EP1_PORT_PMC(7)  = 0x0E; /* valid CS1-CS3       */
	AS85EP1_PORT_PMC(9)  = 0xFF; /* valid D16-D23       */
	AS85EP1_PORT_PMC(10) = 0xFF; /* valid D24-D31       */

	AS85EP1_RFS(1) = 0x800c;
	AS85EP1_RFS(3) = 0x800c;
	AS85EP1_SCR(1) = 0x20A9;
	AS85EP1_SCR(3) = 0x20A9;

#ifndef CONFIG_ROM_KERNEL
	/* The early chip we have is buggy, and writing the interrupt
	   vectors into low RAM may screw up, so for non-ROM kernels, we
	   only rely on the reset vector being downloaded, and copy the
	   rest of the interrupt vectors into place here.  The specific bug
	   is that writing address N, where (N & 0x10) == 0x10, will _also_
	   write to address (N - 0x10).  We avoid this (effectively) by
	   writing in 16-byte chunks backwards from the end.  */

	AS85EP1_IRAMM = 0x3;	/* "write-mode" for the internal instruction memory */

	src = (u32 *)(((u32)&_intv_copy_src_end - 1) & ~0xF);
	dst = (u32 *)&_intv_copy_dst_start
		+ (src - (u32 *)&_intv_copy_src_start);
	do {
		u32 t0 = src[0], t1 = src[1], t2 = src[2], t3 = src[3];
		dst[0] = t0; dst[1] = t1; dst[2] = t2; dst[3] = t3;
		dst -= 4;
		src -= 4;
	} while (src > (u32 *)&_intv_copy_src_start);

	AS85EP1_IRAMM = 0x0;	/* "read-mode" for the internal instruction memory */
#endif /* !CONFIG_ROM_KERNEL */

	v850e_intc_disable_irqs ();
}

void __init mach_setup (char **cmdline)
{
	AS85EP1_PORT_PMC (LEDS_PORT) = 0; /* Make the LEDs port an I/O port. */
	AS85EP1_PORT_PM (LEDS_PORT) = 0; /* Make all the bits output pins.  */
	mach_tick = as85ep1_led_tick;
}

void __init mach_get_physical_ram (unsigned long *ram_start,
				   unsigned long *ram_len)
{
	*ram_start = RAM_START;
	*ram_len = RAM_END - RAM_START;
}

/* Convenience macros.  */
#define SRAM_END	(SRAM_ADDR + SRAM_SIZE)
#define SDRAM_END	(SDRAM_ADDR + SDRAM_SIZE)

void __init mach_reserve_bootmem ()
{
	if (SDRAM_ADDR < RAM_END && SDRAM_ADDR > RAM_START)
		/* We can't use the space between SRAM and SDRAM, so
		   prevent the kernel from trying.  */
		reserve_bootmem (SRAM_END, SDRAM_ADDR - SRAM_END);
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
	{ "CCC", IRQ_INTCCC(0),	IRQ_INTCCC_NUM, 1, 5 },
	{ "CMD", IRQ_INTCMD(0), IRQ_INTCMD_NUM,	1, 5 },
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
	AS85EP1_PORT_IO (LEDS_PORT) = 0xAA;	/* Note that we halted.  */
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
void as85ep1_uart_pre_configure (unsigned chan, unsigned cflags, unsigned baud)
{
	/* Make the shared uart/port pins be uart pins.  */
	AS85EP1_PORT_PMC(3) |= (0x5 << chan);

	/* The AS85EP1 connects some general-purpose I/O pins on the CPU to
	   the RTS/CTS lines of UART 1's serial connection.  I/O pins P53
	   and P54 are RTS and CTS respectively.  */
	if (chan == 1) {
		/* Put P53 & P54 in I/O port mode.  */
		AS85EP1_PORT_PMC(5) &= ~0x18;
		/* Make P53 an output, and P54 an input.  */
		AS85EP1_PORT_PM(5) |=  0x10;
	}
}

/* Minimum and maximum bounds for the moving upper LED boundary in the
   clock tick display.  */
#define MIN_MAX_POS 0
#define MAX_MAX_POS 7

/* There are MAX_MAX_POS^2 - MIN_MAX_POS^2 cycles in the animation, so if
   we pick 6 and 0 as above, we get 49 cycles, which is when divided into
   the standard 100 value for HZ, gives us an almost 1s total time.  */
#define TICKS_PER_FRAME \
	(HZ / (MAX_MAX_POS * MAX_MAX_POS - MIN_MAX_POS * MIN_MAX_POS))

static void as85ep1_led_tick ()
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
				set_bit (pos, &AS85EP1_PORT_IO(LEDS_PORT));
				pos += dir;
				clear_bit (pos, &AS85EP1_PORT_IO(LEDS_PORT));
			}
		}

		counter = 0;
	}
}
