/*
 * Copyright (C) 2011-12 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
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
void arch_do_IRQ(unsigned int irq, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();
	generic_handle_irq(irq);
	irq_exit();
	set_irq_regs(old_regs);
}

/*
 * API called for requesting percpu interrupts - called by each CPU
 *  - For boot CPU, actually request the IRQ with genirq core + enables
 *  - For subsequent callers only enable called locally
 *
 * Relies on being called by boot cpu first (i.e. request called ahead) of
 * any enable as expected by genirq. Hence Suitable only for TIMER, IPI
 * which are guaranteed to be setup on boot core first.
 * Late probed peripherals such as perf can't use this as there no guarantee
 * of being called on boot CPU first.
 */

void arc_request_percpu_irq(int irq, int cpu,
                            irqreturn_t (*isr)(int irq, void *dev),
                            const char *irq_nm,
                            void *percpu_dev)
{
	/* Boot cpu calls request, all call enable */
	if (!cpu) {
		int rc;

#ifdef CONFIG_ISA_ARCOMPACT
		/*
		 * A subsequent request_percpu_irq() fails if percpu_devid is
		 * not set. That in turns sets NOAUTOEN, meaning each core needs
		 * to call enable_percpu_irq()
		 *
		 * For ARCv2, this is done in irq map function since we know
		 * which irqs are strictly per cpu
		 */
		irq_set_percpu_devid(irq);
#endif

		rc = request_percpu_irq(irq, isr, irq_nm, percpu_dev);
		if (rc)
			panic("Percpu IRQ request failed for %d\n", irq);
	}

	enable_percpu_irq(irq, 0);
}
