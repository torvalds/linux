// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011-12 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <asm/mach_desc.h>
#include <asm/smp.h>

/*
 * Late Interrupt system init called from start_kernel for Boot CPU only
 *
 * Since slab must already be initialized, platforms can start doing any
 * needed request_irq( )s
 */
void __init init_IRQ(void)
{
	/*
	 * process the entire interrupt tree in one go
	 * Any external intc will be setup provided DT chains them
	 * properly
	 */
	irqchip_init();

#ifdef CONFIG_SMP
	/* a SMP H/w block could do IPI IRQ request here */
	if (plat_smp_ops.init_per_cpu)
		plat_smp_ops.init_per_cpu(smp_processor_id());
#endif

	if (machine_desc->init_per_cpu)
		machine_desc->init_per_cpu(smp_processor_id());
}

/*
 * "C" Entry point for any ARC ISR, called from low level vector handler
 * @irq is the vector number read from ICAUSE reg of on-chip intc
 */
void arch_do_IRQ(unsigned int hwirq, struct pt_regs *regs)
{
	handle_domain_irq(NULL, hwirq, regs);
}
