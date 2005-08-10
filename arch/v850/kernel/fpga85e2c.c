/*
 * arch/v850/kernel/fpga85e2c.h -- Machine-dependent defs for
 *	FPGA implementation of V850E2/NA85E2C
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
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/bootmem.h>
#include <linux/irq.h>
#include <linux/bitops.h>

#include <asm/atomic.h>
#include <asm/page.h>
#include <asm/machdep.h>

#include "mach.h"

extern void memcons_setup (void);


#define REG_DUMP_ADDR		0x220000


extern struct irqaction reg_snap_action; /* fwd decl */


void __init mach_early_init (void)
{
	int i;
	const u32 *src;
	register u32 *dst asm ("ep");
	extern u32 _intv_end, _intv_load_start;

	/* Set bus sizes: CS0 32-bit, CS1 16-bit, CS7 8-bit,
	   everything else 32-bit.  */
	V850E2_BSC = 0x2AA6;
	for (i = 2; i <= 6; i++)
		CSDEV(i) = 0;	/* 32 bit */

	/* Ensure that the simulator halts on a panic, instead of going
	   into an infinite loop inside the panic function.  */
	panic_timeout = -1;

	/* Move the interrupt vectors into their real location.  Note that
	   any relocations there are relative to the real location, so we
	   don't have to fix anything up.  We use a loop instead of calling
	   memcpy to keep this a leaf function (to avoid a function
	   prologue being generated).  */
	dst = 0x10;		/* &_intv_start + 0x10.  */
	src = &_intv_load_start;
	do {
		u32 t0 = src[0], t1 = src[1], t2 = src[2], t3 = src[3];
		u32 t4 = src[4], t5 = src[5], t6 = src[6], t7 = src[7];
		dst[0] = t0; dst[1] = t1; dst[2] = t2; dst[3] = t3;
		dst[4] = t4; dst[5] = t5; dst[6] = t6; dst[7] = t7;
		dst += 8;
		src += 8;
	} while (dst < &_intv_end);
}

void __init mach_setup (char **cmdline)
{
	memcons_setup ();

	/* Setup up NMI0 to copy the registers to a known memory location.
	   The FGPA board has a button that produces NMI0 when pressed, so
	   this allows us to push the button, and then look at memory to see
	   what's in the registers (there's no other way to easily do so).
	   We have to use `setup_irq' instead of `request_irq' because it's
	   still too early to do memory allocation.  */
	setup_irq (IRQ_NMI (0), &reg_snap_action);
}

void mach_get_physical_ram (unsigned long *ram_start, unsigned long *ram_len)
{
	*ram_start = ERAM_ADDR;
	*ram_len = ERAM_SIZE;
}

void __init mach_sched_init (struct irqaction *timer_action)
{
	/* Setup up the timer interrupt.  The FPGA peripheral control
	   registers _only_ work with single-bit writes (set1/clr1)!  */
	__clear_bit (RPU_GTMC_CE_BIT, &RPU_GTMC);
	__clear_bit (RPU_GTMC_CLK_BIT, &RPU_GTMC);
	__set_bit (RPU_GTMC_CE_BIT, &RPU_GTMC);

	/* We use the first RPU interrupt, which occurs every 8.192ms.  */
	setup_irq (IRQ_RPU (0), timer_action);
}


void mach_gettimeofday (struct timespec *tv)
{
	tv->tv_sec = 0;
	tv->tv_nsec = 0;
}

void machine_halt (void) __attribute__ ((noreturn));
void machine_halt (void)
{
	for (;;) {
		DWC(0) = 0x7777;
		DWC(1) = 0x7777;
		ASC = 0xffff;
		FLGREG(0) = 1;	/* Halt immediately.  */
		asm ("di; halt; nop; nop; nop; nop; nop");
	}
}

void machine_restart (char *__unused)
{
	machine_halt ();
}

void machine_power_off (void)
{
	machine_halt ();
}


/* Interrupts */

struct v850e_intc_irq_init irq_inits[] = {
	{ "IRQ", 0, 		NUM_MACH_IRQS,	1, 7 },
	{ "RPU", IRQ_RPU(0),	IRQ_RPU_NUM,	1, 6 },
	{ 0 }
};
#define NUM_IRQ_INITS ((sizeof irq_inits / sizeof irq_inits[0]) - 1)

struct hw_interrupt_type hw_itypes[NUM_IRQ_INITS];

/* Initialize interrupts.  */
void __init mach_init_irqs (void)
{
	v850e_intc_init_irq_types (irq_inits, hw_itypes);
}


/* An interrupt handler that copies the registers to a known memory location,
   for debugging purposes.  */

static void make_reg_snap (int irq, void *dummy, struct pt_regs *regs)
{
	(*(unsigned *)REG_DUMP_ADDR)++;
	(*(struct pt_regs *)(REG_DUMP_ADDR + sizeof (unsigned))) = *regs;
}

static int reg_snap_dev_id;
static struct irqaction reg_snap_action = {
	make_reg_snap, 0, CPU_MASK_NONE, "reg_snap", &reg_snap_dev_id, 0
};
