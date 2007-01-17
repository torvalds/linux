/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Routines for generic manipulation of the interrupts found on the
 * Lasat boards.
 */
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>

#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/lasat/lasatint.h>
#include <asm/time.h>
#include <asm/gdb-stub.h>

static volatile int *lasat_int_status = NULL;
static volatile int *lasat_int_mask = NULL;
static volatile int lasat_int_mask_shift;

void disable_lasat_irq(unsigned int irq_nr)
{
	*lasat_int_mask &= ~(1 << irq_nr) << lasat_int_mask_shift;
}

void enable_lasat_irq(unsigned int irq_nr)
{
	*lasat_int_mask |= (1 << irq_nr) << lasat_int_mask_shift;
}

static struct irq_chip lasat_irq_type = {
	.typename = "Lasat",
	.ack = disable_lasat_irq,
	.mask = disable_lasat_irq,
	.mask_ack = disable_lasat_irq,
	.unmask = enable_lasat_irq,
};

static inline int ls1bit32(unsigned int x)
{
	int b = 31, s;

	s = 16; if (x << 16 == 0) s = 0; b -= s; x <<= s;
	s =  8; if (x <<  8 == 0) s = 0; b -= s; x <<= s;
	s =  4; if (x <<  4 == 0) s = 0; b -= s; x <<= s;
	s =  2; if (x <<  2 == 0) s = 0; b -= s; x <<= s;
	s =  1; if (x <<  1 == 0) s = 0; b -= s;

	return b;
}

static unsigned long (* get_int_status)(void);

static unsigned long get_int_status_100(void)
{
	return *lasat_int_status & *lasat_int_mask;
}

static unsigned long get_int_status_200(void)
{
	unsigned long int_status;

	int_status = *lasat_int_status;
	int_status &= (int_status >> LASATINT_MASK_SHIFT_200) & 0xffff;
	return int_status;
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned long int_status;
	unsigned int cause = read_c0_cause();
	int irq;

	if (cause & CAUSEF_IP7) {	/* R4000 count / compare IRQ */
		ll_timer_interrupt(7);
		return;
	}

	int_status = get_int_status();

	/* if int_status == 0, then the interrupt has already been cleared */
	if (int_status) {
		irq = ls1bit32(int_status);

		do_IRQ(irq);
	}
}

void __init arch_init_irq(void)
{
	int i;

	switch (mips_machtype) {
	case MACH_LASAT_100:
		lasat_int_status = (void *)LASAT_INT_STATUS_REG_100;
		lasat_int_mask = (void *)LASAT_INT_MASK_REG_100;
		lasat_int_mask_shift = LASATINT_MASK_SHIFT_100;
		get_int_status = get_int_status_100;
		*lasat_int_mask = 0;
		break;
	case MACH_LASAT_200:
		lasat_int_status = (void *)LASAT_INT_STATUS_REG_200;
		lasat_int_mask = (void *)LASAT_INT_MASK_REG_200;
		lasat_int_mask_shift = LASATINT_MASK_SHIFT_200;
		get_int_status = get_int_status_200;
		*lasat_int_mask &= 0xffff;
		break;
	default:
		panic("arch_init_irq: mips_machtype incorrect");
	}

	for (i = 0; i <= LASATINT_END; i++)
		set_irq_chip_and_handler(i, &lasat_irq_type, handle_level_irq);
}
