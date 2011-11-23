/*
 * Copyright 2011 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/io.h>
#include <asm/exception.h>
#include <asm/localtimer.h>
#include <asm/hardware/gic.h>
#ifdef CONFIG_SMP
#include <asm/smp.h>
#endif

asmlinkage void __exception_irq_entry gic_handle_irq(struct pt_regs *regs)
{
	u32 irqstat, irqnr;

	do {
		irqstat = readl_relaxed(gic_cpu_base_addr + GIC_CPU_INTACK);
		irqnr = irqstat & 0x3ff;
		if (irqnr == 1023)
			break;

		if (irqnr > 15 && irqnr < 1021)
			handle_IRQ(irqnr, regs);
#ifdef CONFIG_SMP
		else {
			writel_relaxed(irqstat, gic_cpu_base_addr +
						GIC_CPU_EOI);
			handle_IPI(irqnr, regs);
		}
#endif
	} while (1);
}
