/*
 * Stack dumping functions
 *
 *  Copyright IBM Corp. 1999, 2013
 */

#include <linux/kallsyms.h>
#include <linux/hardirq.h>
#include <linux/kprobes.h>
#include <linux/utsname.h>
#include <linux/export.h>
#include <linux/kdebug.h>
#include <linux/ptrace.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <asm/processor.h>
#include <asm/debug.h>
#include <asm/dis.h>
#include <asm/ipl.h>

/*
 * For show_trace we have tree different stack to consider:
 *   - the panic stack which is used if the kernel stack has overflown
 *   - the asynchronous interrupt stack (cpu related)
 *   - the synchronous kernel stack (process related)
 * The stack trace can start at any of the three stack and can potentially
 * touch all of them. The order is: panic stack, async stack, sync stack.
 */
static unsigned long
__show_trace(unsigned long sp, unsigned long low, unsigned long high)
{
	struct stack_frame *sf;
	struct pt_regs *regs;
	unsigned long addr;

	while (1) {
		sp = sp & PSW_ADDR_INSN;
		if (sp < low || sp > high - sizeof(*sf))
			return sp;
		sf = (struct stack_frame *) sp;
		addr = sf->gprs[8] & PSW_ADDR_INSN;
		printk("([<%016lx>] %pSR)\n", addr, (void *)addr);
		/* Follow the backchain. */
		while (1) {
			low = sp;
			sp = sf->back_chain & PSW_ADDR_INSN;
			if (!sp)
				break;
			if (sp <= low || sp > high - sizeof(*sf))
				return sp;
			sf = (struct stack_frame *) sp;
			addr = sf->gprs[8] & PSW_ADDR_INSN;
			printk(" [<%016lx>] %pSR\n", addr, (void *)addr);
		}
		/* Zero backchain detected, check for interrupt frame. */
		sp = (unsigned long) (sf + 1);
		if (sp <= low || sp > high - sizeof(*regs))
			return sp;
		regs = (struct pt_regs *) sp;
		addr = regs->psw.addr & PSW_ADDR_INSN;
		printk(" [<%016lx>] %pSR\n", addr, (void *)addr);
		low = sp;
		sp = regs->gprs[15];
	}
}

static void show_trace(struct task_struct *task, unsigned long *stack)
{
	const unsigned long frame_size =
		STACK_FRAME_OVERHEAD + sizeof(struct pt_regs);
	register unsigned long __r15 asm ("15");
	unsigned long sp;

	sp = (unsigned long) stack;
	if (!sp)
		sp = task ? task->thread.ksp : __r15;
	printk("Call Trace:\n");
#ifdef CONFIG_CHECK_STACK
	sp = __show_trace(sp,
			  S390_lowcore.panic_stack + frame_size - 4096,
			  S390_lowcore.panic_stack + frame_size);
#endif
	sp = __show_trace(sp,
			  S390_lowcore.async_stack + frame_size - ASYNC_SIZE,
			  S390_lowcore.async_stack + frame_size);
	if (task)
		__show_trace(sp, (unsigned long) task_stack_page(task),
			     (unsigned long) task_stack_page(task) + THREAD_SIZE);
	else
		__show_trace(sp, S390_lowcore.thread_info,
			     S390_lowcore.thread_info + THREAD_SIZE);
	if (!task)
		task = current;
	debug_show_held_locks(task);
}

void show_stack(struct task_struct *task, unsigned long *sp)
{
	register unsigned long *__r15 asm ("15");
	unsigned long *stack;
	int i;

	if (!sp)
		stack = task ? (unsigned long *) task->thread.ksp : __r15;
	else
		stack = sp;

	for (i = 0; i < 20; i++) {
		if (((addr_t) stack & (THREAD_SIZE-1)) == 0)
			break;
		if ((i * sizeof(long) % 32) == 0)
			printk("%s       ", i == 0 ? "" : "\n");
		printk("%016lx ", *stack++);
	}
	printk("\n");
	show_trace(task, sp);
}

static void show_last_breaking_event(struct pt_regs *regs)
{
	printk("Last Breaking-Event-Address:\n");
	printk(" [<%016lx>] %pSR\n", regs->args[0], (void *)regs->args[0]);
}

static inline int mask_bits(struct pt_regs *regs, unsigned long bits)
{
	return (regs->psw.mask & bits) / ((~bits + 1) & bits);
}

void show_registers(struct pt_regs *regs)
{
	char *mode;

	mode = user_mode(regs) ? "User" : "Krnl";
	printk("%s PSW : %p %p", mode, (void *)regs->psw.mask, (void *)regs->psw.addr);
	if (!user_mode(regs))
		printk(" (%pSR)", (void *)regs->psw.addr);
	printk("\n");
	printk("           R:%x T:%x IO:%x EX:%x Key:%x M:%x W:%x "
	       "P:%x AS:%x CC:%x PM:%x", mask_bits(regs, PSW_MASK_PER),
	       mask_bits(regs, PSW_MASK_DAT), mask_bits(regs, PSW_MASK_IO),
	       mask_bits(regs, PSW_MASK_EXT), mask_bits(regs, PSW_MASK_KEY),
	       mask_bits(regs, PSW_MASK_MCHECK), mask_bits(regs, PSW_MASK_WAIT),
	       mask_bits(regs, PSW_MASK_PSTATE), mask_bits(regs, PSW_MASK_ASC),
	       mask_bits(regs, PSW_MASK_CC), mask_bits(regs, PSW_MASK_PM));
	printk(" EA:%x", mask_bits(regs, PSW_MASK_EA | PSW_MASK_BA));
	printk("\n%s GPRS: %016lx %016lx %016lx %016lx\n", mode,
	       regs->gprs[0], regs->gprs[1], regs->gprs[2], regs->gprs[3]);
	printk("           %016lx %016lx %016lx %016lx\n",
	       regs->gprs[4], regs->gprs[5], regs->gprs[6], regs->gprs[7]);
	printk("           %016lx %016lx %016lx %016lx\n",
	       regs->gprs[8], regs->gprs[9], regs->gprs[10], regs->gprs[11]);
	printk("           %016lx %016lx %016lx %016lx\n",
	       regs->gprs[12], regs->gprs[13], regs->gprs[14], regs->gprs[15]);
	show_code(regs);
}

void show_regs(struct pt_regs *regs)
{
	show_regs_print_info(KERN_DEFAULT);
	show_registers(regs);
	/* Show stack backtrace if pt_regs is from kernel mode */
	if (!user_mode(regs))
		show_trace(NULL, (unsigned long *) regs->gprs[15]);
	show_last_breaking_event(regs);
}

static DEFINE_SPINLOCK(die_lock);

void die(struct pt_regs *regs, const char *str)
{
	static int die_counter;

	oops_enter();
	lgr_info_log();
	debug_stop_all();
	console_verbose();
	spin_lock_irq(&die_lock);
	bust_spinlocks(1);
	printk("%s: %04x ilc:%d [#%d] ", str, regs->int_code & 0xffff,
	       regs->int_code >> 17, ++die_counter);
#ifdef CONFIG_PREEMPT
	printk("PREEMPT ");
#endif
#ifdef CONFIG_SMP
	printk("SMP ");
#endif
#ifdef CONFIG_DEBUG_PAGEALLOC
	printk("DEBUG_PAGEALLOC");
#endif
	printk("\n");
	notify_die(DIE_OOPS, str, regs, 0, regs->int_code & 0xffff, SIGSEGV);
	print_modules();
	show_regs(regs);
	bust_spinlocks(0);
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	spin_unlock_irq(&die_lock);
	if (in_interrupt())
		panic("Fatal exception in interrupt");
	if (panic_on_oops)
		panic("Fatal exception: panic_on_oops");
	oops_exit();
	do_exit(SIGSEGV);
}
