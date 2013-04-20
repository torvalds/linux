/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000, 2001, 2004 MIPS Technologies, Inc.
 * Copyright (C) 2001 Ralf Baechle
 * Portions copyright (C) 2009	Cisco Systems, Inc.
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
 * Routines for generic manipulation of the interrupts found on the PowerTV
 * platform.
 *
 * The interrupt controller is located in the South Bridge a PIIX4 device
 * with two internal 82C95 interrupt controllers.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/kernel.h>
#include <linux/random.h>

#include <asm/irq_cpu.h>
#include <linux/io.h>
#include <asm/irq_regs.h>
#include <asm/setup.h>
#include <asm/mips-boards/generic.h>

#include <asm/mach-powertv/asic_regs.h>

static DEFINE_RAW_SPINLOCK(asic_irq_lock);

static inline int get_int(void)
{
	unsigned long flags;
	int irq;

	raw_spin_lock_irqsave(&asic_irq_lock, flags);

	irq = (asic_read(int_int_scan) >> 4) - 1;

	if (irq == 0 || irq >= NR_IRQS)
		irq = -1;

	raw_spin_unlock_irqrestore(&asic_irq_lock, flags);

	return irq;
}

static void asic_irqdispatch(void)
{
	int irq;

	irq = get_int();
	if (irq < 0)
		return;	 /* interrupt has already been cleared */

	do_IRQ(irq);
}

static inline int clz(unsigned long x)
{
	__asm__(
	"	.set	push					\n"
	"	.set	mips32					\n"
	"	clz	%0, %1					\n"
	"	.set	pop					\n"
	: "=r" (x)
	: "r" (x));

	return x;
}

/*
 * Version of ffs that only looks at bits 12..15.
 */
static inline unsigned int irq_ffs(unsigned int pending)
{
	return fls(pending) - 1 + CAUSEB_IP;
}

/*
 * TODO: check how it works under EIC mode.
 */
asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_cause() & read_c0_status() & ST0_IM;
	int irq;

	irq = irq_ffs(pending);

	if (irq == CAUSEF_IP3)
		asic_irqdispatch();
	else if (irq >= 0)
		do_IRQ(irq);
	else
		spurious_interrupt();
}

void __init arch_init_irq(void)
{
	int i;

	asic_irq_init();

	/*
	 * Initialize interrupt exception vectors.
	 */
	if (cpu_has_veic || cpu_has_vint) {
		int nvec = cpu_has_veic ? 64 : 8;
		for (i = 0; i < nvec; i++)
			set_vi_handler(i, asic_irqdispatch);
	}
}
