/*
 * Copyright (C) 1999, 2005 MIPS Technologies, Inc.  All rights reserved.
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
 */
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <asm/mips-boards/simint.h>


extern void mips_cpu_irq_init(int);

extern asmlinkage void simIRQ(void);

asmlinkage void sim_hw0_irqdispatch(struct pt_regs *regs)
{
	do_IRQ(2, regs);
}

void __init arch_init_irq(void)
{
	/* Now safe to set the exception vector. */
	set_except_vector(0, simIRQ);

	mips_cpu_irq_init(MIPSCPU_INT_BASE);
}
