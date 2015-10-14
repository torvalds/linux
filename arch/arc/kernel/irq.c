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
	if (plat_smp_ops.init_irq_cpu)
		plat_smp_ops.init_irq_cpu(smp_processor_id());

	if (machine_desc->init_cpu_smp)
		machine_desc->init_cpu_smp(smp_processor_id());
#endif
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

void arc_request_percpu_irq(int irq, int cpu,
                            irqreturn_t (*isr)(int irq, void *dev),
                            const char *irq_nm,
                            void *percpu_dev)
{
	/* Boot cpu calls request, all call enable */
	if (!cpu) {
		int rc;

		/*
		 * These 2 calls are essential to making percpu IRQ APIs work
		 * Ideally these details could be hidden in irq chip map function
		 * but the issue is IPIs IRQs being static (non-DT) and platform
		 * specific, so we can't identify them there.
		 */
		irq_set_percpu_devid(irq);
		irq_modify_status(irq, IRQ_NOAUTOEN, 0);  /* @irq, @clr, @set */

		rc = request_percpu_irq(irq, isr, irq_nm, percpu_dev);
		if (rc)
			panic("Percpu IRQ request failed for %d\n", irq);
	}

	enable_percpu_irq(irq, 0);
}
