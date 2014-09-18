/*
 * Copyright 2005-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/seq_file.h>
#include <linux/kallsyms.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <asm/irq_handler.h>
#include <asm/trace.h>
#include <asm/pda.h>

static atomic_t irq_err_count;
void ack_bad_irq(unsigned int irq)
{
	atomic_inc(&irq_err_count);
	printk(KERN_ERR "IRQ: spurious interrupt %d\n", irq);
}

static struct irq_desc bad_irq_desc = {
	.handle_irq = handle_bad_irq,
	.lock = __RAW_SPIN_LOCK_UNLOCKED(bad_irq_desc.lock),
};

#ifdef CONFIG_CPUMASK_OFFSTACK
/* We are not allocating a variable-sized bad_irq_desc.affinity */
#error "Blackfin architecture does not support CONFIG_CPUMASK_OFFSTACK."
#endif

#ifdef CONFIG_PROC_FS
int arch_show_interrupts(struct seq_file *p, int prec)
{
	int j;

	seq_printf(p, "%*s: ", prec, "NMI");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", cpu_pda[j].__nmi_count);
	seq_printf(p, "  CORE  Non Maskable Interrupt\n");
	seq_printf(p, "%*s: %10u\n", prec, "ERR", atomic_read(&irq_err_count));
	return 0;
}
#endif

#ifdef CONFIG_DEBUG_STACKOVERFLOW
static void check_stack_overflow(int irq)
{
	/* Debugging check for stack overflow: is there less than STACK_WARN free? */
	long sp = __get_SP() & (THREAD_SIZE - 1);

	if (unlikely(sp < (sizeof(struct thread_info) + STACK_WARN))) {
		dump_stack();
		pr_emerg("irq%i: possible stack overflow only %ld bytes free\n",
			irq, sp - sizeof(struct thread_info));
	}
}
#else
static inline void check_stack_overflow(int irq) { }
#endif

#ifndef CONFIG_IPIPE
static void maybe_lower_to_irq14(void)
{
	unsigned short pending, other_ints;

	/*
	 * If we're the only interrupt running (ignoring IRQ15 which
	 * is for syscalls), lower our priority to IRQ14 so that
	 * softirqs run at that level.  If there's another,
	 * lower-level interrupt, irq_exit will defer softirqs to
	 * that. If the interrupt pipeline is enabled, we are already
	 * running at IRQ14 priority, so we don't need this code.
	 */
	CSYNC();
	pending = bfin_read_IPEND() & ~0x8000;
	other_ints = pending & (pending - 1);
	if (other_ints == 0)
		lower_to_irq14();
}
#else
static inline void maybe_lower_to_irq14(void) { }
#endif

/*
 * do_IRQ handles all hardware IRQs.  Decoded IRQs should not
 * come via this function.  Instead, they should provide their
 * own 'handler'
 */
#ifdef CONFIG_DO_IRQ_L1
__attribute__((l1_text))
#endif
asmlinkage void asm_do_IRQ(unsigned int irq, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();

	check_stack_overflow(irq);

	/*
	 * Some hardware gives randomly wrong interrupts.  Rather
	 * than crashing, do something sensible.
	 */
	if (irq >= NR_IRQS)
		handle_bad_irq(irq, &bad_irq_desc);
	else
		generic_handle_irq(irq);

	maybe_lower_to_irq14();

	irq_exit();

	set_irq_regs(old_regs);
}

void __init init_IRQ(void)
{
	init_arch_irq();

#ifdef CONFIG_DEBUG_BFIN_HWTRACE_EXPAND
	/* Now that evt_ivhw is set up, turn this on */
	trace_buff_offset = 0;
	bfin_write_TBUFCTL(BFIN_TRACE_ON);
	printk(KERN_INFO "Hardware Trace expanded to %ik\n",
	  1 << CONFIG_DEBUG_BFIN_HWTRACE_EXPAND_LEN);
#endif
}
