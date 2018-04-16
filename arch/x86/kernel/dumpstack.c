/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 */
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/utsname.h>
#include <linux/hardirq.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/ftrace.h>
#include <linux/kexec.h>
#include <linux/bug.h>
#include <linux/nmi.h>
#include <linux/sysfs.h>

#include <asm/cpu_entry_area.h>
#include <asm/stacktrace.h>
#include <asm/unwind.h>

int panic_on_unrecovered_nmi;
int panic_on_io_nmi;
static unsigned int code_bytes = 64;
static int die_counter;

bool in_task_stack(unsigned long *stack, struct task_struct *task,
		   struct stack_info *info)
{
	unsigned long *begin = task_stack_page(task);
	unsigned long *end   = task_stack_page(task) + THREAD_SIZE;

	if (stack < begin || stack >= end)
		return false;

	info->type	= STACK_TYPE_TASK;
	info->begin	= begin;
	info->end	= end;
	info->next_sp	= NULL;

	return true;
}

bool in_entry_stack(unsigned long *stack, struct stack_info *info)
{
	struct entry_stack *ss = cpu_entry_stack(smp_processor_id());

	void *begin = ss;
	void *end = ss + 1;

	if ((void *)stack < begin || (void *)stack >= end)
		return false;

	info->type	= STACK_TYPE_ENTRY;
	info->begin	= begin;
	info->end	= end;
	info->next_sp	= NULL;

	return true;
}

static void printk_stack_address(unsigned long address, int reliable,
				 char *log_lvl)
{
	touch_nmi_watchdog();
	printk("%s %s%pB\n", log_lvl, reliable ? "" : "? ", (void *)address);
}

void show_iret_regs(struct pt_regs *regs)
{
	printk(KERN_DEFAULT "RIP: %04x:%pS\n", (int)regs->cs, (void *)regs->ip);
	printk(KERN_DEFAULT "RSP: %04x:%016lx EFLAGS: %08lx", (int)regs->ss,
		regs->sp, regs->flags);
}

static void show_regs_if_on_stack(struct stack_info *info, struct pt_regs *regs,
				  bool partial)
{
	/*
	 * These on_stack() checks aren't strictly necessary: the unwind code
	 * has already validated the 'regs' pointer.  The checks are done for
	 * ordering reasons: if the registers are on the next stack, we don't
	 * want to print them out yet.  Otherwise they'll be shown as part of
	 * the wrong stack.  Later, when show_trace_log_lvl() switches to the
	 * next stack, this function will be called again with the same regs so
	 * they can be printed in the right context.
	 */
	if (!partial && on_stack(info, regs, sizeof(*regs))) {
		__show_regs(regs, 0);

	} else if (partial && on_stack(info, (void *)regs + IRET_FRAME_OFFSET,
				       IRET_FRAME_SIZE)) {
		/*
		 * When an interrupt or exception occurs in entry code, the
		 * full pt_regs might not have been saved yet.  In that case
		 * just print the iret frame.
		 */
		show_iret_regs(regs);
	}
}

void show_trace_log_lvl(struct task_struct *task, struct pt_regs *regs,
			unsigned long *stack, char *log_lvl)
{
	struct unwind_state state;
	struct stack_info stack_info = {0};
	unsigned long visit_mask = 0;
	int graph_idx = 0;
	bool partial = false;

	printk("%sCall Trace:\n", log_lvl);

	unwind_start(&state, task, regs, stack);
	stack = stack ? : get_stack_pointer(task, regs);
	regs = unwind_get_entry_regs(&state, &partial);

	/*
	 * Iterate through the stacks, starting with the current stack pointer.
	 * Each stack has a pointer to the next one.
	 *
	 * x86-64 can have several stacks:
	 * - task stack
	 * - interrupt stack
	 * - HW exception stacks (double fault, nmi, debug, mce)
	 * - entry stack
	 *
	 * x86-32 can have up to four stacks:
	 * - task stack
	 * - softirq stack
	 * - hardirq stack
	 * - entry stack
	 */
	for ( ; stack; stack = PTR_ALIGN(stack_info.next_sp, sizeof(long))) {
		const char *stack_name;

		if (get_stack_info(stack, task, &stack_info, &visit_mask)) {
			/*
			 * We weren't on a valid stack.  It's possible that
			 * we overflowed a valid stack into a guard page.
			 * See if the next page up is valid so that we can
			 * generate some kind of backtrace if this happens.
			 */
			stack = (unsigned long *)PAGE_ALIGN((unsigned long)stack);
			if (get_stack_info(stack, task, &stack_info, &visit_mask))
				break;
		}

		stack_name = stack_type_name(stack_info.type);
		if (stack_name)
			printk("%s <%s>\n", log_lvl, stack_name);

		if (regs)
			show_regs_if_on_stack(&stack_info, regs, partial);

		/*
		 * Scan the stack, printing any text addresses we find.  At the
		 * same time, follow proper stack frames with the unwinder.
		 *
		 * Addresses found during the scan which are not reported by
		 * the unwinder are considered to be additional clues which are
		 * sometimes useful for debugging and are prefixed with '?'.
		 * This also serves as a failsafe option in case the unwinder
		 * goes off in the weeds.
		 */
		for (; stack < stack_info.end; stack++) {
			unsigned long real_addr;
			int reliable = 0;
			unsigned long addr = READ_ONCE_NOCHECK(*stack);
			unsigned long *ret_addr_p =
				unwind_get_return_address_ptr(&state);

			if (!__kernel_text_address(addr))
				continue;

			/*
			 * Don't print regs->ip again if it was already printed
			 * by show_regs_if_on_stack().
			 */
			if (regs && stack == &regs->ip)
				goto next;

			if (stack == ret_addr_p)
				reliable = 1;

			/*
			 * When function graph tracing is enabled for a
			 * function, its return address on the stack is
			 * replaced with the address of an ftrace handler
			 * (return_to_handler).  In that case, before printing
			 * the "real" address, we want to print the handler
			 * address as an "unreliable" hint that function graph
			 * tracing was involved.
			 */
			real_addr = ftrace_graph_ret_addr(task, &graph_idx,
							  addr, stack);
			if (real_addr != addr)
				printk_stack_address(addr, 0, log_lvl);
			printk_stack_address(real_addr, reliable, log_lvl);

			if (!reliable)
				continue;

next:
			/*
			 * Get the next frame from the unwinder.  No need to
			 * check for an error: if anything goes wrong, the rest
			 * of the addresses will just be printed as unreliable.
			 */
			unwind_next_frame(&state);

			/* if the frame has entry regs, print them */
			regs = unwind_get_entry_regs(&state, &partial);
			if (regs)
				show_regs_if_on_stack(&stack_info, regs, partial);
		}

		if (stack_name)
			printk("%s </%s>\n", log_lvl, stack_name);
	}
}

void show_stack(struct task_struct *task, unsigned long *sp)
{
	task = task ? : current;

	/*
	 * Stack frames below this one aren't interesting.  Don't show them
	 * if we're printing for %current.
	 */
	if (!sp && task == current)
		sp = get_stack_pointer(current, NULL);

	show_trace_log_lvl(task, NULL, sp, KERN_DEFAULT);
}

void show_stack_regs(struct pt_regs *regs)
{
	show_trace_log_lvl(current, regs, NULL, KERN_DEFAULT);
}

static arch_spinlock_t die_lock = __ARCH_SPIN_LOCK_UNLOCKED;
static int die_owner = -1;
static unsigned int die_nest_count;

unsigned long oops_begin(void)
{
	int cpu;
	unsigned long flags;

	oops_enter();

	/* racy, but better than risking deadlock. */
	raw_local_irq_save(flags);
	cpu = smp_processor_id();
	if (!arch_spin_trylock(&die_lock)) {
		if (cpu == die_owner)
			/* nested oops. should stop eventually */;
		else
			arch_spin_lock(&die_lock);
	}
	die_nest_count++;
	die_owner = cpu;
	console_verbose();
	bust_spinlocks(1);
	return flags;
}
EXPORT_SYMBOL_GPL(oops_begin);
NOKPROBE_SYMBOL(oops_begin);

void __noreturn rewind_stack_do_exit(int signr);

void oops_end(unsigned long flags, struct pt_regs *regs, int signr)
{
	if (regs && kexec_should_crash(current))
		crash_kexec(regs);

	bust_spinlocks(0);
	die_owner = -1;
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	die_nest_count--;
	if (!die_nest_count)
		/* Nest count reaches zero, release the lock. */
		arch_spin_unlock(&die_lock);
	raw_local_irq_restore(flags);
	oops_exit();

	if (!signr)
		return;
	if (in_interrupt())
		panic("Fatal exception in interrupt");
	if (panic_on_oops)
		panic("Fatal exception");

	/*
	 * We're not going to return, but we might be on an IST stack or
	 * have very little stack space left.  Rewind the stack and kill
	 * the task.
	 */
	rewind_stack_do_exit(signr);
}
NOKPROBE_SYMBOL(oops_end);

int __die(const char *str, struct pt_regs *regs, long err)
{
#ifdef CONFIG_X86_32
	unsigned short ss;
	unsigned long sp;
#endif
	printk(KERN_DEFAULT
	       "%s: %04lx [#%d]%s%s%s%s%s\n", str, err & 0xffff, ++die_counter,
	       IS_ENABLED(CONFIG_PREEMPT) ? " PREEMPT"         : "",
	       IS_ENABLED(CONFIG_SMP)     ? " SMP"             : "",
	       debug_pagealloc_enabled()  ? " DEBUG_PAGEALLOC" : "",
	       IS_ENABLED(CONFIG_KASAN)   ? " KASAN"           : "",
	       IS_ENABLED(CONFIG_PAGE_TABLE_ISOLATION) ?
	       (boot_cpu_has(X86_FEATURE_PTI) ? " PTI" : " NOPTI") : "");

	if (notify_die(DIE_OOPS, str, regs, err,
			current->thread.trap_nr, SIGSEGV) == NOTIFY_STOP)
		return 1;

	print_modules();
	show_regs(regs);
#ifdef CONFIG_X86_32
	if (user_mode(regs)) {
		sp = regs->sp;
		ss = regs->ss;
	} else {
		sp = kernel_stack_pointer(regs);
		savesegment(ss, ss);
	}
	printk(KERN_EMERG "EIP: %pS SS:ESP: %04x:%08lx\n",
	       (void *)regs->ip, ss, sp);
#else
	/* Executive summary in case the oops scrolled away */
	printk(KERN_ALERT "RIP: %pS RSP: %016lx\n", (void *)regs->ip, regs->sp);
#endif
	return 0;
}
NOKPROBE_SYMBOL(__die);

/*
 * This is gone through when something in the kernel has done something bad
 * and is about to be terminated:
 */
void die(const char *str, struct pt_regs *regs, long err)
{
	unsigned long flags = oops_begin();
	int sig = SIGSEGV;

	if (__die(str, regs, err))
		sig = 0;
	oops_end(flags, regs, sig);
}

static int __init code_bytes_setup(char *s)
{
	ssize_t ret;
	unsigned long val;

	if (!s)
		return -EINVAL;

	ret = kstrtoul(s, 0, &val);
	if (ret)
		return ret;

	code_bytes = val;
	if (code_bytes > 8192)
		code_bytes = 8192;

	return 1;
}
__setup("code_bytes=", code_bytes_setup);

void show_regs(struct pt_regs *regs)
{
	bool all = true;
	int i;

	show_regs_print_info(KERN_DEFAULT);

	if (IS_ENABLED(CONFIG_X86_32))
		all = !user_mode(regs);

	__show_regs(regs, all);

	/*
	 * When in-kernel, we also print out the stack and code at the
	 * time of the fault..
	 */
	if (!user_mode(regs)) {
		unsigned int code_prologue = code_bytes * 43 / 64;
		unsigned int code_len = code_bytes;
		unsigned char c;
		u8 *ip;

		show_trace_log_lvl(current, regs, NULL, KERN_DEFAULT);

		printk(KERN_DEFAULT "Code: ");

		ip = (u8 *)regs->ip - code_prologue;
		if (ip < (u8 *)PAGE_OFFSET || probe_kernel_address(ip, c)) {
			/* try starting at IP */
			ip = (u8 *)regs->ip;
			code_len = code_len - code_prologue + 1;
		}
		for (i = 0; i < code_len; i++, ip++) {
			if (ip < (u8 *)PAGE_OFFSET ||
					probe_kernel_address(ip, c)) {
				pr_cont(" Bad RIP value.");
				break;
			}
			if (ip == (u8 *)regs->ip)
				pr_cont("<%02x> ", c);
			else
				pr_cont("%02x ", c);
		}
	}
	pr_cont("\n");
}
