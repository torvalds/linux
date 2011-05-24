/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 */
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/kexec.h>
#include <linux/sysfs.h>
#include <linux/bug.h>
#include <linux/nmi.h>

#include <asm/stacktrace.h>


#define N_EXCEPTION_STACKS_END \
		(N_EXCEPTION_STACKS + DEBUG_STKSZ/EXCEPTION_STKSZ - 2)

static char x86_stack_ids[][8] = {
		[ DEBUG_STACK-1			]	= "#DB",
		[ NMI_STACK-1			]	= "NMI",
		[ DOUBLEFAULT_STACK-1		]	= "#DF",
		[ STACKFAULT_STACK-1		]	= "#SS",
		[ MCE_STACK-1			]	= "#MC",
#if DEBUG_STKSZ > EXCEPTION_STKSZ
		[ N_EXCEPTION_STACKS ...
		  N_EXCEPTION_STACKS_END	]	= "#DB[?]"
#endif
};

static unsigned long *in_exception_stack(unsigned cpu, unsigned long stack,
					 unsigned *usedp, char **idp)
{
	unsigned k;

	/*
	 * Iterate over all exception stacks, and figure out whether
	 * 'stack' is in one of them:
	 */
	for (k = 0; k < N_EXCEPTION_STACKS; k++) {
		unsigned long end = per_cpu(orig_ist, cpu).ist[k];
		/*
		 * Is 'stack' above this exception frame's end?
		 * If yes then skip to the next frame.
		 */
		if (stack >= end)
			continue;
		/*
		 * Is 'stack' above this exception frame's start address?
		 * If yes then we found the right frame.
		 */
		if (stack >= end - EXCEPTION_STKSZ) {
			/*
			 * Make sure we only iterate through an exception
			 * stack once. If it comes up for the second time
			 * then there's something wrong going on - just
			 * break out and return NULL:
			 */
			if (*usedp & (1U << k))
				break;
			*usedp |= 1U << k;
			*idp = x86_stack_ids[k];
			return (unsigned long *)end;
		}
		/*
		 * If this is a debug stack, and if it has a larger size than
		 * the usual exception stacks, then 'stack' might still
		 * be within the lower portion of the debug stack:
		 */
#if DEBUG_STKSZ > EXCEPTION_STKSZ
		if (k == DEBUG_STACK - 1 && stack >= end - DEBUG_STKSZ) {
			unsigned j = N_EXCEPTION_STACKS - 1;

			/*
			 * Black magic. A large debug stack is composed of
			 * multiple exception stack entries, which we
			 * iterate through now. Dont look:
			 */
			do {
				++j;
				end -= EXCEPTION_STKSZ;
				x86_stack_ids[j][4] = '1' +
						(j - N_EXCEPTION_STACKS);
			} while (stack < end - EXCEPTION_STKSZ);
			if (*usedp & (1U << j))
				break;
			*usedp |= 1U << j;
			*idp = x86_stack_ids[j];
			return (unsigned long *)end;
		}
#endif
	}
	return NULL;
}

static inline int
in_irq_stack(unsigned long *stack, unsigned long *irq_stack,
	     unsigned long *irq_stack_end)
{
	return (stack >= irq_stack && stack < irq_stack_end);
}

/*
 * We are returning from the irq stack and go to the previous one.
 * If the previous stack is also in the irq stack, then bp in the first
 * frame of the irq stack points to the previous, interrupted one.
 * Otherwise we have another level of indirection: We first save
 * the bp of the previous stack, then we switch the stack to the irq one
 * and save a new bp that links to the previous one.
 * (See save_args())
 */
static inline unsigned long
fixup_bp_irq_link(unsigned long bp, unsigned long *stack,
		  unsigned long *irq_stack, unsigned long *irq_stack_end)
{
#ifdef CONFIG_FRAME_POINTER
	struct stack_frame *frame = (struct stack_frame *)bp;
	unsigned long next;

	if (!in_irq_stack(stack, irq_stack, irq_stack_end)) {
		if (!probe_kernel_address(&frame->next_frame, next))
			return next;
		else
			WARN_ONCE(1, "Perf: bad frame pointer = %p in "
				  "callchain\n", &frame->next_frame);
	}
#endif
	return bp;
}

/*
 * x86-64 can have up to three kernel stacks:
 * process stack
 * interrupt stack
 * severe exception (double fault, nmi, stack fault, debug, mce) hardware stack
 */

void dump_trace(struct task_struct *task, struct pt_regs *regs,
		unsigned long *stack, unsigned long bp,
		const struct stacktrace_ops *ops, void *data)
{
	const unsigned cpu = get_cpu();
	unsigned long *irq_stack_end =
		(unsigned long *)per_cpu(irq_stack_ptr, cpu);
	unsigned used = 0;
	struct thread_info *tinfo;
	int graph = 0;
	unsigned long dummy;

	if (!task)
		task = current;

	if (!stack) {
		stack = &dummy;
		if (task && task != current)
			stack = (unsigned long *)task->thread.sp;
	}

	if (!bp)
		bp = stack_frame(task, regs);
	/*
	 * Print function call entries in all stacks, starting at the
	 * current stack address. If the stacks consist of nested
	 * exceptions
	 */
	tinfo = task_thread_info(task);
	for (;;) {
		char *id;
		unsigned long *estack_end;
		estack_end = in_exception_stack(cpu, (unsigned long)stack,
						&used, &id);

		if (estack_end) {
			if (ops->stack(data, id) < 0)
				break;

			bp = ops->walk_stack(tinfo, stack, bp, ops,
					     data, estack_end, &graph);
			ops->stack(data, "<EOE>");
			/*
			 * We link to the next stack via the
			 * second-to-last pointer (index -2 to end) in the
			 * exception stack:
			 */
			stack = (unsigned long *) estack_end[-2];
			continue;
		}
		if (irq_stack_end) {
			unsigned long *irq_stack;
			irq_stack = irq_stack_end -
				(IRQ_STACK_SIZE - 64) / sizeof(*irq_stack);

			if (in_irq_stack(stack, irq_stack, irq_stack_end)) {
				if (ops->stack(data, "IRQ") < 0)
					break;
				bp = ops->walk_stack(tinfo, stack, bp,
					ops, data, irq_stack_end, &graph);
				/*
				 * We link to the next stack (which would be
				 * the process stack normally) the last
				 * pointer (index -1 to end) in the IRQ stack:
				 */
				stack = (unsigned long *) (irq_stack_end[-1]);
				bp = fixup_bp_irq_link(bp, stack, irq_stack,
						       irq_stack_end);
				irq_stack_end = NULL;
				ops->stack(data, "EOI");
				continue;
			}
		}
		break;
	}

	/*
	 * This handles the process stack:
	 */
	bp = ops->walk_stack(tinfo, stack, bp, ops, data, NULL, &graph);
	put_cpu();
}
EXPORT_SYMBOL(dump_trace);

void
show_stack_log_lvl(struct task_struct *task, struct pt_regs *regs,
		   unsigned long *sp, unsigned long bp, char *log_lvl)
{
	unsigned long *irq_stack_end;
	unsigned long *irq_stack;
	unsigned long *stack;
	int cpu;
	int i;

	preempt_disable();
	cpu = smp_processor_id();

	irq_stack_end	= (unsigned long *)(per_cpu(irq_stack_ptr, cpu));
	irq_stack	= (unsigned long *)(per_cpu(irq_stack_ptr, cpu) - IRQ_STACK_SIZE);

	/*
	 * Debugging aid: "show_stack(NULL, NULL);" prints the
	 * back trace for this cpu:
	 */
	if (sp == NULL) {
		if (task)
			sp = (unsigned long *)task->thread.sp;
		else
			sp = (unsigned long *)&sp;
	}

	stack = sp;
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (stack >= irq_stack && stack <= irq_stack_end) {
			if (stack == irq_stack_end) {
				stack = (unsigned long *) (irq_stack_end[-1]);
				printk(KERN_CONT " <EOI> ");
			}
		} else {
		if (((long) stack & (THREAD_SIZE-1)) == 0)
			break;
		}
		if (i && ((i % STACKSLOTS_PER_LINE) == 0))
			printk(KERN_CONT "\n");
		printk(KERN_CONT " %016lx", *stack++);
		touch_nmi_watchdog();
	}
	preempt_enable();

	printk(KERN_CONT "\n");
	show_trace_log_lvl(task, regs, sp, bp, log_lvl);
}

void show_registers(struct pt_regs *regs)
{
	int i;
	unsigned long sp;
	const int cpu = smp_processor_id();
	struct task_struct *cur = current;

	sp = regs->sp;
	printk("CPU %d ", cpu);
	print_modules();
	__show_regs(regs, 1);
	printk("Process %s (pid: %d, threadinfo %p, task %p)\n",
		cur->comm, cur->pid, task_thread_info(cur), cur);

	/*
	 * When in-kernel, we also print out the stack and code at the
	 * time of the fault..
	 */
	if (!user_mode(regs)) {
		unsigned int code_prologue = code_bytes * 43 / 64;
		unsigned int code_len = code_bytes;
		unsigned char c;
		u8 *ip;

		printk(KERN_EMERG "Stack:\n");
		show_stack_log_lvl(NULL, regs, (unsigned long *)sp,
				   0, KERN_EMERG);

		printk(KERN_EMERG "Code: ");

		ip = (u8 *)regs->ip - code_prologue;
		if (ip < (u8 *)PAGE_OFFSET || probe_kernel_address(ip, c)) {
			/* try starting at IP */
			ip = (u8 *)regs->ip;
			code_len = code_len - code_prologue + 1;
		}
		for (i = 0; i < code_len; i++, ip++) {
			if (ip < (u8 *)PAGE_OFFSET ||
					probe_kernel_address(ip, c)) {
				printk(" Bad RIP value.");
				break;
			}
			if (ip == (u8 *)regs->ip)
				printk("<%02x> ", c);
			else
				printk("%02x ", c);
		}
	}
	printk("\n");
}

int is_valid_bugaddr(unsigned long ip)
{
	unsigned short ud2;

	if (__copy_from_user(&ud2, (const void __user *) ip, sizeof(ud2)))
		return 0;

	return ud2 == 0x0b0f;
}
