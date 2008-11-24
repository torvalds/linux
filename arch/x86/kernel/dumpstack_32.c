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
#include <linux/kexec.h>
#include <linux/bug.h>
#include <linux/nmi.h>
#include <linux/sysfs.h>

#include <asm/stacktrace.h>

#define STACKSLOTS_PER_LINE 8
#define get_bp(bp) asm("movl %%ebp, %0" : "=r" (bp) :)

int panic_on_unrecovered_nmi;
int kstack_depth_to_print = 3 * STACKSLOTS_PER_LINE;
static unsigned int code_bytes = 64;
static int die_counter;

void printk_address(unsigned long address, int reliable)
{
	printk(" [<%p>] %s%pS\n", (void *) address,
			reliable ? "" : "? ", (void *) address);
}

static inline int valid_stack_ptr(struct thread_info *tinfo,
			void *p, unsigned int size, void *end)
{
	void *t = tinfo;
	if (end) {
		if (p < end && p >= (end-THREAD_SIZE))
			return 1;
		else
			return 0;
	}
	return p > t && p < t + THREAD_SIZE - size;
}

/* The form of the top of the frame on the stack */
struct stack_frame {
	struct stack_frame *next_frame;
	unsigned long return_address;
};

static inline unsigned long
print_context_stack(struct thread_info *tinfo,
		unsigned long *stack, unsigned long bp,
		const struct stacktrace_ops *ops, void *data,
		unsigned long *end)
{
	struct stack_frame *frame = (struct stack_frame *)bp;

	while (valid_stack_ptr(tinfo, stack, sizeof(*stack), end)) {
		unsigned long addr;

		addr = *stack;
		if (__kernel_text_address(addr)) {
			if ((unsigned long) stack == bp + sizeof(long)) {
				ops->address(data, addr, 1);
				frame = frame->next_frame;
				bp = (unsigned long) frame;
			} else {
				ops->address(data, addr, bp == 0);
			}
		}
		stack++;
	}
	return bp;
}

void dump_trace(struct task_struct *task, struct pt_regs *regs,
		unsigned long *stack, unsigned long bp,
		const struct stacktrace_ops *ops, void *data)
{
	if (!task)
		task = current;

	if (!stack) {
		unsigned long dummy;
		stack = &dummy;
		if (task && task != current)
			stack = (unsigned long *)task->thread.sp;
	}

#ifdef CONFIG_FRAME_POINTER
	if (!bp) {
		if (task == current) {
			/* Grab bp right from our regs */
			get_bp(bp);
		} else {
			/* bp is the last reg pushed by switch_to */
			bp = *(unsigned long *) task->thread.sp;
		}
	}
#endif

	for (;;) {
		struct thread_info *context;

		context = (struct thread_info *)
			((unsigned long)stack & (~(THREAD_SIZE - 1)));
		bp = print_context_stack(context, stack, bp, ops, data, NULL);

		stack = (unsigned long *)context->previous_esp;
		if (!stack)
			break;
		if (ops->stack(data, "IRQ") < 0)
			break;
		touch_nmi_watchdog();
	}
}
EXPORT_SYMBOL(dump_trace);

static void
print_trace_warning_symbol(void *data, char *msg, unsigned long symbol)
{
	printk(data);
	print_symbol(msg, symbol);
	printk("\n");
}

static void print_trace_warning(void *data, char *msg)
{
	printk("%s%s\n", (char *)data, msg);
}

static int print_trace_stack(void *data, char *name)
{
	printk("%s <%s> ", (char *)data, name);
	return 0;
}

/*
 * Print one address/symbol entries per line.
 */
static void print_trace_address(void *data, unsigned long addr, int reliable)
{
	touch_nmi_watchdog();
	printk(data);
	printk_address(addr, reliable);
}

static const struct stacktrace_ops print_trace_ops = {
	.warning = print_trace_warning,
	.warning_symbol = print_trace_warning_symbol,
	.stack = print_trace_stack,
	.address = print_trace_address,
};

static void
show_trace_log_lvl(struct task_struct *task, struct pt_regs *regs,
		unsigned long *stack, unsigned long bp, char *log_lvl)
{
	printk("%sCall Trace:\n", log_lvl);
	dump_trace(task, regs, stack, bp, &print_trace_ops, log_lvl);
}

void show_trace(struct task_struct *task, struct pt_regs *regs,
		unsigned long *stack, unsigned long bp)
{
	show_trace_log_lvl(task, regs, stack, bp, "");
}

static void
show_stack_log_lvl(struct task_struct *task, struct pt_regs *regs,
		unsigned long *sp, unsigned long bp, char *log_lvl)
{
	unsigned long *stack;
	int i;

	if (sp == NULL) {
		if (task)
			sp = (unsigned long *)task->thread.sp;
		else
			sp = (unsigned long *)&sp;
	}

	stack = sp;
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (kstack_end(stack))
			break;
		if (i && ((i % STACKSLOTS_PER_LINE) == 0))
			printk("\n%s", log_lvl);
		printk(" %08lx", *stack++);
		touch_nmi_watchdog();
	}
	printk("\n");
	show_trace_log_lvl(task, regs, sp, bp, log_lvl);
}

void show_stack(struct task_struct *task, unsigned long *sp)
{
	show_stack_log_lvl(task, NULL, sp, 0, "");
}

/*
 * The architecture-independent dump_stack generator
 */
void dump_stack(void)
{
	unsigned long bp = 0;
	unsigned long stack;

#ifdef CONFIG_FRAME_POINTER
	if (!bp)
		get_bp(bp);
#endif

	printk("Pid: %d, comm: %.20s %s %s %.*s\n",
		current->pid, current->comm, print_tainted(),
		init_utsname()->release,
		(int)strcspn(init_utsname()->version, " "),
		init_utsname()->version);
	show_trace(NULL, NULL, &stack, bp);
}

EXPORT_SYMBOL(dump_stack);

void show_registers(struct pt_regs *regs)
{
	int i;

	print_modules();
	__show_regs(regs, 0);

	printk(KERN_EMERG "Process %.*s (pid: %d, ti=%p task=%p task.ti=%p)\n",
		TASK_COMM_LEN, current->comm, task_pid_nr(current),
		current_thread_info(), current, task_thread_info(current));
	/*
	 * When in-kernel, we also print out the stack and code at the
	 * time of the fault..
	 */
	if (!user_mode_vm(regs)) {
		unsigned int code_prologue = code_bytes * 43 / 64;
		unsigned int code_len = code_bytes;
		unsigned char c;
		u8 *ip;

		printk(KERN_EMERG "Stack:\n");
		show_stack_log_lvl(NULL, regs, &regs->sp,
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
				printk(" Bad EIP value.");
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

	if (ip < PAGE_OFFSET)
		return 0;
	if (probe_kernel_address((unsigned short *)ip, ud2))
		return 0;

	return ud2 == 0x0b0f;
}

static raw_spinlock_t die_lock = __RAW_SPIN_LOCK_UNLOCKED;
static int die_owner = -1;
static unsigned int die_nest_count;

unsigned __kprobes long oops_begin(void)
{
	unsigned long flags;

	oops_enter();

	if (die_owner != raw_smp_processor_id()) {
		console_verbose();
		raw_local_irq_save(flags);
		__raw_spin_lock(&die_lock);
		die_owner = smp_processor_id();
		die_nest_count = 0;
		bust_spinlocks(1);
	} else {
		raw_local_irq_save(flags);
	}
	die_nest_count++;
	return flags;
}

void __kprobes oops_end(unsigned long flags, struct pt_regs *regs, int signr)
{
	bust_spinlocks(0);
	die_owner = -1;
	add_taint(TAINT_DIE);
	__raw_spin_unlock(&die_lock);
	raw_local_irq_restore(flags);

	if (!regs)
		return;

	if (kexec_should_crash(current))
		crash_kexec(regs);
	if (in_interrupt())
		panic("Fatal exception in interrupt");
	if (panic_on_oops)
		panic("Fatal exception");
	oops_exit();
	do_exit(signr);
}

int __kprobes __die(const char *str, struct pt_regs *regs, long err)
{
	unsigned short ss;
	unsigned long sp;

	printk(KERN_EMERG "%s: %04lx [#%d] ", str, err & 0xffff, ++die_counter);
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
	sysfs_printk_last_file();
	if (notify_die(DIE_OOPS, str, regs, err,
			current->thread.trap_no, SIGSEGV) == NOTIFY_STOP)
		return 1;

	show_registers(regs);
	/* Executive summary in case the oops scrolled away */
	sp = (unsigned long) (&regs->sp);
	savesegment(ss, ss);
	if (user_mode(regs)) {
		sp = regs->sp;
		ss = regs->ss & 0xffff;
	}
	printk(KERN_EMERG "EIP: [<%08lx>] ", regs->ip);
	print_symbol("%s", regs->ip);
	printk(" SS:ESP %04x:%08lx\n", ss, sp);
	return 0;
}

/*
 * This is gone through when something in the kernel has done something bad
 * and is about to be terminated:
 */
void die(const char *str, struct pt_regs *regs, long err)
{
	unsigned long flags = oops_begin();

	if (die_nest_count < 3) {
		report_bug(regs->ip, regs);

		if (__die(str, regs, err))
			regs = NULL;
	} else {
		printk(KERN_EMERG "Recursive die() failure, output suppressed\n");
	}

	oops_end(flags, regs, SIGSEGV);
}

static DEFINE_SPINLOCK(nmi_print_lock);

void notrace __kprobes
die_nmi(char *str, struct pt_regs *regs, int do_panic)
{
	if (notify_die(DIE_NMIWATCHDOG, str, regs, 0, 2, SIGINT) == NOTIFY_STOP)
		return;

	spin_lock(&nmi_print_lock);
	/*
	* We are in trouble anyway, lets at least try
	* to get a message out:
	*/
	bust_spinlocks(1);
	printk(KERN_EMERG "%s", str);
	printk(" on CPU%d, ip %08lx, registers:\n",
		smp_processor_id(), regs->ip);
	show_registers(regs);
	if (do_panic)
		panic("Non maskable interrupt");
	console_silent();
	spin_unlock(&nmi_print_lock);

	/*
	 * If we are in kernel we are probably nested up pretty bad
	 * and might aswell get out now while we still can:
	 */
	if (!user_mode_vm(regs)) {
		current->thread.trap_no = 2;
		crash_kexec(regs);
	}

	bust_spinlocks(0);
	do_exit(SIGSEGV);
}

static int __init oops_setup(char *s)
{
	if (!s)
		return -EINVAL;
	if (!strcmp(s, "panic"))
		panic_on_oops = 1;
	return 0;
}
early_param("oops", oops_setup);

static int __init kstack_setup(char *s)
{
	if (!s)
		return -EINVAL;
	kstack_depth_to_print = simple_strtoul(s, NULL, 0);
	return 0;
}
early_param("kstack", kstack_setup);

static int __init code_bytes_setup(char *s)
{
	code_bytes = simple_strtoul(s, NULL, 0);
	if (code_bytes > 8192)
		code_bytes = 8192;

	return 1;
}
__setup("code_bytes=", code_bytes_setup);
