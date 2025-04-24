// SPDX-License-Identifier: GPL-2.0
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
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <asm/processor.h>
#include <asm/debug.h>
#include <asm/dis.h>
#include <asm/ipl.h>
#include <asm/unwind.h>

const char *stack_type_name(enum stack_type type)
{
	switch (type) {
	case STACK_TYPE_TASK:
		return "task";
	case STACK_TYPE_IRQ:
		return "irq";
	case STACK_TYPE_NODAT:
		return "nodat";
	case STACK_TYPE_RESTART:
		return "restart";
	default:
		return "unknown";
	}
}
EXPORT_SYMBOL_GPL(stack_type_name);

static inline bool in_stack(unsigned long sp, struct stack_info *info,
			    enum stack_type type, unsigned long stack)
{
	if (sp < stack || sp >= stack + THREAD_SIZE)
		return false;
	info->type = type;
	info->begin = stack;
	info->end = stack + THREAD_SIZE;
	return true;
}

static bool in_task_stack(unsigned long sp, struct task_struct *task,
			  struct stack_info *info)
{
	unsigned long stack = (unsigned long)task_stack_page(task);

	return in_stack(sp, info, STACK_TYPE_TASK, stack);
}

static bool in_irq_stack(unsigned long sp, struct stack_info *info)
{
	unsigned long stack = get_lowcore()->async_stack - STACK_INIT_OFFSET;

	return in_stack(sp, info, STACK_TYPE_IRQ, stack);
}

static bool in_nodat_stack(unsigned long sp, struct stack_info *info)
{
	unsigned long stack = get_lowcore()->nodat_stack - STACK_INIT_OFFSET;

	return in_stack(sp, info, STACK_TYPE_NODAT, stack);
}

static bool in_mcck_stack(unsigned long sp, struct stack_info *info)
{
	unsigned long stack = get_lowcore()->mcck_stack - STACK_INIT_OFFSET;

	return in_stack(sp, info, STACK_TYPE_MCCK, stack);
}

static bool in_restart_stack(unsigned long sp, struct stack_info *info)
{
	unsigned long stack = get_lowcore()->restart_stack - STACK_INIT_OFFSET;

	return in_stack(sp, info, STACK_TYPE_RESTART, stack);
}

int get_stack_info(unsigned long sp, struct task_struct *task,
		   struct stack_info *info, unsigned long *visit_mask)
{
	if (!sp)
		goto unknown;

	/* Sanity check: ABI requires SP to be aligned 8 bytes. */
	if (sp & 0x7)
		goto unknown;

	/* Check per-task stack */
	if (in_task_stack(sp, task, info))
		goto recursion_check;

	if (task != current)
		goto unknown;

	/* Check per-cpu stacks */
	if (!in_irq_stack(sp, info) &&
	    !in_nodat_stack(sp, info) &&
	    !in_restart_stack(sp, info) &&
	    !in_mcck_stack(sp, info))
		goto unknown;

recursion_check:
	/*
	 * Make sure we don't iterate through any given stack more than once.
	 * If it comes up a second time then there's something wrong going on:
	 * just break out and report an unknown stack type.
	 */
	if (*visit_mask & (1UL << info->type))
		goto unknown;
	*visit_mask |= 1UL << info->type;
	return 0;
unknown:
	info->type = STACK_TYPE_UNKNOWN;
	return -EINVAL;
}

void show_stack(struct task_struct *task, unsigned long *stack,
		       const char *loglvl)
{
	struct unwind_state state;

	printk("%sCall Trace:\n", loglvl);
	unwind_for_each_frame(&state, task, NULL, (unsigned long) stack)
		printk(state.reliable ? "%s [<%016lx>] %pSR \n" :
					"%s([<%016lx>] %pSR)\n",
		       loglvl, state.ip, (void *) state.ip);
	debug_show_held_locks(task ? : current);
}

static void show_last_breaking_event(struct pt_regs *regs)
{
	printk("Last Breaking-Event-Address:\n");
	printk(" [<%016lx>] ", regs->last_break);
	if (user_mode(regs)) {
		print_vma_addr(KERN_CONT, regs->last_break);
		pr_cont("\n");
	} else {
		pr_cont("%pSR\n", (void *)regs->last_break);
	}
}

void show_registers(struct pt_regs *regs)
{
	struct psw_bits *psw = &psw_bits(regs->psw);
	char *mode;

	mode = user_mode(regs) ? "User" : "Krnl";
	printk("%s PSW : %px %px", mode, (void *)regs->psw.mask, (void *)regs->psw.addr);
	if (!user_mode(regs))
		pr_cont(" (%pSR)", (void *)regs->psw.addr);
	pr_cont("\n");
	printk("           R:%x T:%x IO:%x EX:%x Key:%x M:%x W:%x "
	       "P:%x AS:%x CC:%x PM:%x", psw->per, psw->dat, psw->io, psw->ext,
	       psw->key, psw->mcheck, psw->wait, psw->pstate, psw->as, psw->cc, psw->pm);
	pr_cont(" RI:%x EA:%x\n", psw->ri, psw->eaba);
	printk("%s GPRS: %016lx %016lx %016lx %016lx\n", mode,
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
		show_stack(NULL, (unsigned long *) regs->gprs[15], KERN_DEFAULT);
	show_last_breaking_event(regs);
}

static DEFINE_SPINLOCK(die_lock);

void __noreturn die(struct pt_regs *regs, const char *str)
{
	static int die_counter;

	oops_enter();
	lgr_info_log();
	debug_stop_all();
	console_verbose();
	spin_lock_irq(&die_lock);
	bust_spinlocks(1);
	printk("%s: %04x ilc:%d [#%d]", str, regs->int_code & 0xffff,
	       regs->int_code >> 17, ++die_counter);
	pr_cont("SMP ");
	if (debug_pagealloc_enabled())
		pr_cont("DEBUG_PAGEALLOC");
	pr_cont("\n");
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
	make_task_dead(SIGSEGV);
}
