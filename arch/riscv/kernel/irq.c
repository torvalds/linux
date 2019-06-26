// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 * Copyright (C) 2018 Christoph Hellwig
 */

#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/seq_file.h>
#include <asm/smp.h>

/*
 * Possible interrupt causes:
 */
#define INTERRUPT_CAUSE_SOFTWARE	IRQ_S_SOFT
#define INTERRUPT_CAUSE_TIMER		IRQ_S_TIMER
#define INTERRUPT_CAUSE_EXTERNAL	IRQ_S_EXT

int arch_show_interrupts(struct seq_file *p, int prec)
{
	show_ipi_stats(p, prec);
	return 0;
}

asmlinkage void __irq_entry do_IRQ(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();
	switch (regs->scause & ~SCAUSE_IRQ_FLAG) {
	case INTERRUPT_CAUSE_TIMER:
		riscv_timer_interrupt();
		break;
#ifdef CONFIG_SMP
	case INTERRUPT_CAUSE_SOFTWARE:
		/*
		 * We only use software interrupts to pass IPIs, so if a non-SMP
		 * system gets one, then we don't know what to do.
		 */
		riscv_software_interrupt();
		break;
#endif
	case INTERRUPT_CAUSE_EXTERNAL:
		handle_arch_irq(regs);
		break;
	default:
		pr_alert("unexpected interrupt cause 0x%lx", regs->scause);
		BUG();
	}
	irq_exit();

	set_irq_regs(old_regs);
}

void __init init_IRQ(void)
{
	irqchip_init();
}
