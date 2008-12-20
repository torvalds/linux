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

#define STACKSLOTS_PER_LINE 4
#define get_bp(bp) asm("movq %%rbp, %0" : "=r" (bp) :)

int panic_on_unrecovered_nmi;
int kstack_depth_to_print = 3 * STACKSLOTS_PER_LINE;
static unsigned int code_bytes = 64;
static int die_counter;

void printk_address(unsigned long address, int reliable)
{
	printk(" [<%p>] %s%pS\n", (void *) address,
			reliable ? "" : "? ", (void *) address);
}

static unsigned long *in_exception_stack(unsigned cpu, unsigned long stack,
					unsigned *usedp, char **idp)
{
	static char ids[][8] = {
		[DEBUG_STACK - 1] = "#DB",
		[NMI_STACK - 1] = "NMI",
		[DOUBLEFAULT_STACK - 1] = "#DF",
		[STACKFAULT_STACK - 1] = "#SS",
		[MCE_STACK - 1] = "#MC",
#if DEBUG_STKSZ > EXCEPTION_STKSZ
		[N_EXCEPTION_STACKS ...
			N_EXCEPTION_STACKS + DEBUG_STKSZ / EXCEPTION_STKSZ - 2] = "#DB[?]"
#endif
	};
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
			*idp = ids[k];
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
				ids[j][4] = '1' + (j - N_EXCEPTION_STACKS);
			} while (stack < end - EXCEPTION_STKSZ);
			if (*usedp & (1U << j))
				break;
			*usedp |= 1U << j;
			*idp = ids[j];
			return (unsigned long *)end;
		}
#endif
	}
	return NULL;
}

/*
 * x86-64 can have up to three kernel stacks:
 * process stack
 * interrupt stack
 * severe exception (double fault, nmi, stack fault, debug, mce) hardware stack
 */

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
	const unsigned cpu = get_cpu();
	unsigned long *irqstack_end = (unsigned long *)cpu_pda(cpu)->irqstackptr;
	unsigned used = 0;
	struct thread_info *tinfo;

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

			bp = print_context_stack(tinfo, stack, bp, ops,
							data, estack_end);
			ops->stack(data, "<EOE>");
			/*
			 * We link to the next stack via the
			 * second-to-last pointer (index -2 to end) in the
			 * exception stack:
			 */
			stack = (unsigned long *) estack_end[-2];
			continue;
		}
		if (irqstack_end) {
			unsigned long *irqstack;
			irqstack = irqstack_end -
				(IRQSTACKSIZE - 64) / sizeof(*irqstack);

			if (stack >= irqstack && stack < irqstack_end) {
				if (ops->stack(data, "IRQ") < 0)
					break;
				bp = print_context_stack(tinfo, stack, bp,
						ops, data, irqstack_end);
				/*
				 * We link to the next stack (which would be
				 * the process stack normally) the last
				 * pointer (index -1 to end) in the IRQ stack:
				 */
				stack = (unsigned long *) (irqstack_end[-1]);
				irqstack_end = NULL;
				ops->stack(data, "EOI");
				continue;
			}
		}
		break;
	}

	/*
	 * This handles the process stack:
	 */
	bp = print_context_stack(tinfo, stack, bp, ops, data, NULL);
	put_cpu();
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
	const int cpu = smp_processor_id();
	unsigned long *irqstack_end =
		(unsigned long *) (cpu_pda(cpu)->irqstackptr);
	unsigned long *irqstack =
		(unsigned long *) (cpu_pda(cpu)->irqstackptr - IRQSTACKSIZE);

	/*
	 * debugging aid: "show_stack(NULL, NULL);" prints the
	 * back trace for this cpu.
	 */

	if (sp == NULL) {
		if (task)
			sp = (unsigned long *)task->thread.sp;
		else
			sp = (unsigned long *)&sp;
	}

	stack = sp;
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (stack >= irqstack && stack <= irqstack_end) {
			if (stack == irqstack_end) {
				stack = (unsigned long *) (irqstack_end[-1]);
				printk(" <EOI> ");
			}
		} else {
		if (((long) stack & (THREAD_SIZE-1)) == 0)
			break;
		}
		if (i && ((i % STACKSLOTS_PER_LINE) == 0))
			printk("\n%s", log_lvl);
		printk(" %016lx", *stack++);
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
	unsigned long sp;
	const int cpu = smp_processor_id();
	struct task_struct *cur = cpu_pda(cpu)->pcurrent;

	sp = regs->sp;
	printk("CPU %d ", cpu);
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
				regs->bp, KERN_EMERG);

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

static raw_spinlock_t die_lock = __RAW_SPIN_LOCK_UNLOCKED;
static int die_owner = -1;
static unsigned int die_nest_count;

unsigned __kprobes long oops_begin(void)
{
	int cpu;
	unsigned long flags;

	oops_enter();

	/* racy, but better than risking deadlock. */
	raw_local_irq_save(flags);
	cpu = smp_processor_id();
	if (!__raw_spin_trylock(&die_lock)) {
		if (cpu == die_owner)
			/* nested oops. should stop eventually */;
		else
			__raw_spin_lock(&die_lock);
	}
	die_nest_count++;
	die_owner = cpu;
	console_verbose();
	bust_spinlocks(1);
	return flags;
}

void __kprobes oops_end(unsigned long flags, struct pt_regs *regs, int signr)
{
	die_owner = -1;
	bust_spinlocks(0);
	die_nest_count--;
	if (!die_nest_count)
		/* Nest count reaches zero, release the lock. */
		__raw_spin_unlock(&die_lock);
	raw_local_irq_restore(flags);
	if (!regs) {
		oops_exit();
		return;
	}
	if (in_interrupt())
		panic("Fatal exception in interrupt");
	if (panic_on_oops)
		panic("Fatal exception");
	oops_exit();
	do_exit(signr);
}

int __kprobes __die(const char *str, struct pt_regs *regs, long err)
{
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
	add_taint(TAINT_DIE);
	/* Executive summary in case the oops scrolled away */
	printk(KERN_ALERT "RIP ");
	printk_address(regs->ip, 1);
	printk(" RSP <%016lx>\n", regs->sp);
	if (kexec_should_crash(current))
		crash_kexec(regs);
	return 0;
}

void die(const char *str, struct pt_regs *regs, long err)
{
	unsigned long flags = oops_begin();

	if (!user_mode(regs))
		report_bug(regs->ip, regs);

	if (__die(str, regs, err))
		regs = NULL;
	oops_end(flags, regs, SIGSEGV);
}

notrace __kprobes void
die_nmi(char *str, struct pt_regs *regs, int do_panic)
{
	unsigned long flags;

	if (notify_die(DIE_NMIWATCHDOG, str, regs, 0, 2, SIGINT) == NOTIFY_STOP)
		return;

	flags = oops_begin();
	/*
	 * We are in trouble anyway, lets at least try
	 * to get a message out.
	 */
	printk(KERN_EMERG "%s", str);
	printk(" on CPU%d, ip %08lx, registers:\n",
		smp_processor_id(), regs->ip);
	show_registers(regs);
	if (kexec_should_crash(current))
		crash_kexec(regs);
	if (do_panic || panic_on_oops)
		panic("Non maskable interrupt");
	oops_end(flags, NULL, SIGBUS);
	nmi_exit();
	local_irq_enable();
	do_exit(SIGBUS);
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
