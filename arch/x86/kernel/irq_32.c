/*
 *	Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 * This file contains the lowest level x86-specific interrupt
 * entry, irq-stacks and irq statistics code. All the remaining
 * irq logic is done by the generic kernel/irq/ code and
 * by the x86-specific irq controller code. (e.g. i8259.c and
 * io_apic.c.)
 */

#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/percpu.h>
#include <linux/mm.h>

#include <asm/apic.h>
#include <asm/nospec-branch.h>

#ifdef CONFIG_DEBUG_STACKOVERFLOW

int sysctl_panic_on_stackoverflow __read_mostly;

/* Debugging check for stack overflow: is there less than 1KB free? */
static int check_stack_overflow(void)
{
	long sp;

	__asm__ __volatile__("andl %%esp,%0" :
			     "=r" (sp) : "0" (THREAD_SIZE - 1));

	return sp < (sizeof(struct thread_info) + STACK_WARN);
}

static void print_stack_overflow(void)
{
	printk(KERN_WARNING "low stack detected by irq handler\n");
	dump_stack();
	if (sysctl_panic_on_stackoverflow)
		panic("low stack detected by irq handler - check messages\n");
}

#else
static inline int check_stack_overflow(void) { return 0; }
static inline void print_stack_overflow(void) { }
#endif

DEFINE_PER_CPU(struct irq_stack *, hardirq_stack);
DEFINE_PER_CPU(struct irq_stack *, softirq_stack);

static void call_on_stack(void *func, void *stack)
{
	asm volatile("xchgl	%%ebx,%%esp	\n"
		     CALL_NOSPEC
		     "movl	%%ebx,%%esp	\n"
		     : "=b" (stack)
		     : "0" (stack),
		       [thunk_target] "D"(func)
		     : "memory", "cc", "edx", "ecx", "eax");
}

static inline void *current_stack(void)
{
	return (void *)(current_stack_pointer & ~(THREAD_SIZE - 1));
}

static inline int execute_on_irq_stack(int overflow, struct irq_desc *desc)
{
	struct irq_stack *curstk, *irqstk;
	u32 *isp, *prev_esp, arg1;

	curstk = (struct irq_stack *) current_stack();
	irqstk = __this_cpu_read(hardirq_stack);

	/*
	 * this is where we switch to the IRQ stack. However, if we are
	 * already using the IRQ stack (because we interrupted a hardirq
	 * handler) we can't do that and just have to keep using the
	 * current stack (which is the irq stack already after all)
	 */
	if (unlikely(curstk == irqstk))
		return 0;

	isp = (u32 *) ((char *)irqstk + sizeof(*irqstk));

	/* Save the next esp at the bottom of the stack */
	prev_esp = (u32 *)irqstk;
	*prev_esp = current_stack_pointer;

	if (unlikely(overflow))
		call_on_stack(print_stack_overflow, isp);

	asm volatile("xchgl	%%ebx,%%esp	\n"
		     CALL_NOSPEC
		     "movl	%%ebx,%%esp	\n"
		     : "=a" (arg1), "=b" (isp)
		     :  "0" (desc),   "1" (isp),
			[thunk_target] "D" (desc->handle_irq)
		     : "memory", "cc", "ecx");
	return 1;
}

/*
 * allocate per-cpu stacks for hardirq and for softirq processing
 */
void irq_ctx_init(int cpu)
{
	struct irq_stack *irqstk;

	if (per_cpu(hardirq_stack, cpu))
		return;

	irqstk = page_address(alloc_pages_node(cpu_to_node(cpu),
					       THREADINFO_GFP,
					       THREAD_SIZE_ORDER));
	per_cpu(hardirq_stack, cpu) = irqstk;

	irqstk = page_address(alloc_pages_node(cpu_to_node(cpu),
					       THREADINFO_GFP,
					       THREAD_SIZE_ORDER));
	per_cpu(softirq_stack, cpu) = irqstk;

	printk(KERN_DEBUG "CPU %u irqstacks, hard=%p soft=%p\n",
	       cpu, per_cpu(hardirq_stack, cpu),  per_cpu(softirq_stack, cpu));
}

void do_softirq_own_stack(void)
{
	struct thread_info *curstk;
	struct irq_stack *irqstk;
	u32 *isp, *prev_esp;

	curstk = current_stack();
	irqstk = __this_cpu_read(softirq_stack);

	/* build the stack frame on the softirq stack */
	isp = (u32 *) ((char *)irqstk + sizeof(*irqstk));

	/* Push the previous esp onto the stack */
	prev_esp = (u32 *)irqstk;
	*prev_esp = current_stack_pointer;

	call_on_stack(__do_softirq, isp);
}

bool handle_irq(struct irq_desc *desc, struct pt_regs *regs)
{
	int overflow = check_stack_overflow();

	if (IS_ERR_OR_NULL(desc))
		return false;

	if (user_mode(regs) || !execute_on_irq_stack(overflow, desc)) {
		if (unlikely(overflow))
			print_stack_overflow();
		generic_handle_irq_desc(desc);
	}

	return true;
}
