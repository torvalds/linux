/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'entry.S'.
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/nmi.h>
#include <linux/kprobes.h>
#include <linux/kexec.h>
#include <linux/unwind.h>
#include <linux/uaccess.h>
#include <linux/bug.h>
#include <linux/kdebug.h>
#include <linux/utsname.h>

#if defined(CONFIG_EDAC)
#include <linux/edac.h>
#endif

#include <asm/system.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/debugreg.h>
#include <asm/desc.h>
#include <asm/i387.h>
#include <asm/processor.h>
#include <asm/unwind.h>
#include <asm/smp.h>
#include <asm/pgalloc.h>
#include <asm/pda.h>
#include <asm/proto.h>
#include <asm/nmi.h>
#include <asm/stacktrace.h>

asmlinkage void divide_error(void);
asmlinkage void debug(void);
asmlinkage void nmi(void);
asmlinkage void int3(void);
asmlinkage void overflow(void);
asmlinkage void bounds(void);
asmlinkage void invalid_op(void);
asmlinkage void device_not_available(void);
asmlinkage void double_fault(void);
asmlinkage void coprocessor_segment_overrun(void);
asmlinkage void invalid_TSS(void);
asmlinkage void segment_not_present(void);
asmlinkage void stack_segment(void);
asmlinkage void general_protection(void);
asmlinkage void page_fault(void);
asmlinkage void coprocessor_error(void);
asmlinkage void simd_coprocessor_error(void);
asmlinkage void reserved(void);
asmlinkage void alignment_check(void);
asmlinkage void machine_check(void);
asmlinkage void spurious_interrupt_bug(void);

static inline void conditional_sti(struct pt_regs *regs)
{
	if (regs->eflags & X86_EFLAGS_IF)
		local_irq_enable();
}

static inline void preempt_conditional_sti(struct pt_regs *regs)
{
	preempt_disable();
	if (regs->eflags & X86_EFLAGS_IF)
		local_irq_enable();
}

static inline void preempt_conditional_cli(struct pt_regs *regs)
{
	if (regs->eflags & X86_EFLAGS_IF)
		local_irq_disable();
	/* Make sure to not schedule here because we could be running
	   on an exception stack. */
	preempt_enable_no_resched();
}

int kstack_depth_to_print = 12;

#ifdef CONFIG_KALLSYMS
void printk_address(unsigned long address)
{
	unsigned long offset = 0, symsize;
	const char *symname;
	char *modname;
	char *delim = ":";
	char namebuf[128];

	symname = kallsyms_lookup(address, &symsize, &offset,
					&modname, namebuf);
	if (!symname) {
		printk(" [<%016lx>]\n", address);
		return;
	}
	if (!modname)
		modname = delim = ""; 		
	printk(" [<%016lx>] %s%s%s%s+0x%lx/0x%lx\n",
		address, delim, modname, delim, symname, offset, symsize);
}
#else
void printk_address(unsigned long address)
{
	printk(" [<%016lx>]\n", address);
}
#endif

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
		[N_EXCEPTION_STACKS ... N_EXCEPTION_STACKS + DEBUG_STKSZ / EXCEPTION_STKSZ - 2] = "#DB[?]"
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

#define MSG(txt) ops->warning(data, txt)

/*
 * x86-64 can have up to three kernel stacks: 
 * process stack
 * interrupt stack
 * severe exception (double fault, nmi, stack fault, debug, mce) hardware stack
 */

static inline int valid_stack_ptr(struct thread_info *tinfo, void *p)
{
	void *t = (void *)tinfo;
        return p > t && p < t + THREAD_SIZE - 3;
}

void dump_trace(struct task_struct *tsk, struct pt_regs *regs,
		unsigned long *stack,
		const struct stacktrace_ops *ops, void *data)
{
	const unsigned cpu = get_cpu();
	unsigned long *irqstack_end = (unsigned long*)cpu_pda(cpu)->irqstackptr;
	unsigned used = 0;
	struct thread_info *tinfo;

	if (!tsk)
		tsk = current;

	if (!stack) {
		unsigned long dummy;
		stack = &dummy;
		if (tsk && tsk != current)
			stack = (unsigned long *)tsk->thread.rsp;
	}

	/*
	 * Print function call entries within a stack. 'cond' is the
	 * "end of stackframe" condition, that the 'stack++'
	 * iteration will eventually trigger.
	 */
#define HANDLE_STACK(cond) \
	do while (cond) { \
		unsigned long addr = *stack++; \
		/* Use unlocked access here because except for NMIs	\
		   we should be already protected against module unloads */ \
		if (__kernel_text_address(addr)) { \
			/* \
			 * If the address is either in the text segment of the \
			 * kernel, or in the region which contains vmalloc'ed \
			 * memory, it *may* be the address of a calling \
			 * routine; if so, print it so that someone tracing \
			 * down the cause of the crash will be able to figure \
			 * out the call path that was taken. \
			 */ \
			ops->address(data, addr);   \
		} \
	} while (0)

	/*
	 * Print function call entries in all stacks, starting at the
	 * current stack address. If the stacks consist of nested
	 * exceptions
	 */
	for (;;) {
		char *id;
		unsigned long *estack_end;
		estack_end = in_exception_stack(cpu, (unsigned long)stack,
						&used, &id);

		if (estack_end) {
			if (ops->stack(data, id) < 0)
				break;
			HANDLE_STACK (stack < estack_end);
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
				HANDLE_STACK (stack < irqstack_end);
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
	tinfo = task_thread_info(tsk);
	HANDLE_STACK (valid_stack_ptr(tinfo, stack));
#undef HANDLE_STACK
	put_cpu();
}
EXPORT_SYMBOL(dump_trace);

static void
print_trace_warning_symbol(void *data, char *msg, unsigned long symbol)
{
	print_symbol(msg, symbol);
	printk("\n");
}

static void print_trace_warning(void *data, char *msg)
{
	printk("%s\n", msg);
}

static int print_trace_stack(void *data, char *name)
{
	printk(" <%s> ", name);
	return 0;
}

static void print_trace_address(void *data, unsigned long addr)
{
	touch_nmi_watchdog();
	printk_address(addr);
}

static const struct stacktrace_ops print_trace_ops = {
	.warning = print_trace_warning,
	.warning_symbol = print_trace_warning_symbol,
	.stack = print_trace_stack,
	.address = print_trace_address,
};

void
show_trace(struct task_struct *tsk, struct pt_regs *regs, unsigned long *stack)
{
	printk("\nCall Trace:\n");
	dump_trace(tsk, regs, stack, &print_trace_ops, NULL);
	printk("\n");
}

static void
_show_stack(struct task_struct *tsk, struct pt_regs *regs, unsigned long *rsp)
{
	unsigned long *stack;
	int i;
	const int cpu = smp_processor_id();
	unsigned long *irqstack_end = (unsigned long *) (cpu_pda(cpu)->irqstackptr);
	unsigned long *irqstack = (unsigned long *) (cpu_pda(cpu)->irqstackptr - IRQSTACKSIZE);

	// debugging aid: "show_stack(NULL, NULL);" prints the
	// back trace for this cpu.

	if (rsp == NULL) {
		if (tsk)
			rsp = (unsigned long *)tsk->thread.rsp;
		else
			rsp = (unsigned long *)&rsp;
	}

	stack = rsp;
	for(i=0; i < kstack_depth_to_print; i++) {
		if (stack >= irqstack && stack <= irqstack_end) {
			if (stack == irqstack_end) {
				stack = (unsigned long *) (irqstack_end[-1]);
				printk(" <EOI> ");
			}
		} else {
		if (((long) stack & (THREAD_SIZE-1)) == 0)
			break;
		}
		if (i && ((i % 4) == 0))
			printk("\n");
		printk(" %016lx", *stack++);
		touch_nmi_watchdog();
	}
	show_trace(tsk, regs, rsp);
}

void show_stack(struct task_struct *tsk, unsigned long * rsp)
{
	_show_stack(tsk, NULL, rsp);
}

/*
 * The architecture-independent dump_stack generator
 */
void dump_stack(void)
{
	unsigned long dummy;

	printk("Pid: %d, comm: %.20s %s %s %.*s\n",
		current->pid, current->comm, print_tainted(),
		init_utsname()->release,
		(int)strcspn(init_utsname()->version, " "),
		init_utsname()->version);
	show_trace(NULL, NULL, &dummy);
}

EXPORT_SYMBOL(dump_stack);

void show_registers(struct pt_regs *regs)
{
	int i;
	int in_kernel = !user_mode(regs);
	unsigned long rsp;
	const int cpu = smp_processor_id();
	struct task_struct *cur = cpu_pda(cpu)->pcurrent;

	rsp = regs->rsp;
	printk("CPU %d ", cpu);
	__show_regs(regs);
	printk("Process %s (pid: %d, threadinfo %p, task %p)\n",
		cur->comm, cur->pid, task_thread_info(cur), cur);

	/*
	 * When in-kernel, we also print out the stack and code at the
	 * time of the fault..
	 */
	if (in_kernel) {
		printk("Stack: ");
		_show_stack(NULL, regs, (unsigned long*)rsp);

		printk("\nCode: ");
		if (regs->rip < PAGE_OFFSET)
			goto bad;

		for (i=0; i<20; i++) {
			unsigned char c;
			if (__get_user(c, &((unsigned char*)regs->rip)[i])) {
bad:
				printk(" Bad RIP value.");
				break;
			}
			printk("%02x ", c);
		}
	}
	printk("\n");
}	

int is_valid_bugaddr(unsigned long rip)
{
	unsigned short ud2;

	if (__copy_from_user(&ud2, (const void __user *) rip, sizeof(ud2)))
		return 0;

	return ud2 == 0x0b0f;
}

#ifdef CONFIG_BUG
void out_of_line_bug(void)
{ 
	BUG(); 
} 
EXPORT_SYMBOL(out_of_line_bug);
#endif

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

void __kprobes oops_end(unsigned long flags)
{ 
	die_owner = -1;
	bust_spinlocks(0);
	die_nest_count--;
	if (!die_nest_count)
		/* Nest count reaches zero, release the lock. */
		__raw_spin_unlock(&die_lock);
	raw_local_irq_restore(flags);
	if (panic_on_oops)
		panic("Fatal exception");
	oops_exit();
}

void __kprobes __die(const char * str, struct pt_regs * regs, long err)
{
	static int die_counter;
	printk(KERN_EMERG "%s: %04lx [%u] ", str, err & 0xffff,++die_counter);
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
	notify_die(DIE_OOPS, str, regs, err, current->thread.trap_no, SIGSEGV);
	show_registers(regs);
	add_taint(TAINT_DIE);
	/* Executive summary in case the oops scrolled away */
	printk(KERN_ALERT "RIP ");
	printk_address(regs->rip); 
	printk(" RSP <%016lx>\n", regs->rsp); 
	if (kexec_should_crash(current))
		crash_kexec(regs);
}

void die(const char * str, struct pt_regs * regs, long err)
{
	unsigned long flags = oops_begin();

	if (!user_mode(regs))
		report_bug(regs->rip, regs);

	__die(str, regs, err);
	oops_end(flags);
	do_exit(SIGSEGV); 
}

void __kprobes die_nmi(char *str, struct pt_regs *regs, int do_panic)
{
	unsigned long flags = oops_begin();

	/*
	 * We are in trouble anyway, lets at least try
	 * to get a message out.
	 */
	printk(str, smp_processor_id());
	show_registers(regs);
	if (kexec_should_crash(current))
		crash_kexec(regs);
	if (do_panic || panic_on_oops)
		panic("Non maskable interrupt");
	oops_end(flags);
	nmi_exit();
	local_irq_enable();
	do_exit(SIGSEGV);
}

static void __kprobes do_trap(int trapnr, int signr, char *str,
			      struct pt_regs * regs, long error_code,
			      siginfo_t *info)
{
	struct task_struct *tsk = current;

	if (user_mode(regs)) {
		/*
		 * We want error_code and trap_no set for userspace
		 * faults and kernelspace faults which result in
		 * die(), but not kernelspace faults which are fixed
		 * up.  die() gives the process no chance to handle
		 * the signal and notice the kernel fault information,
		 * so that won't result in polluting the information
		 * about previously queued, but not yet delivered,
		 * faults.  See also do_general_protection below.
		 */
		tsk->thread.error_code = error_code;
		tsk->thread.trap_no = trapnr;

		if (show_unhandled_signals && unhandled_signal(tsk, signr) &&
		    printk_ratelimit())
			printk(KERN_INFO
			       "%s[%d] trap %s rip:%lx rsp:%lx error:%lx\n",
			       tsk->comm, tsk->pid, str,
			       regs->rip, regs->rsp, error_code); 

		if (info)
			force_sig_info(signr, info, tsk);
		else
			force_sig(signr, tsk);
		return;
	}


	/* kernel trap */ 
	{	     
		const struct exception_table_entry *fixup;
		fixup = search_exception_tables(regs->rip);
		if (fixup)
			regs->rip = fixup->fixup;
		else {
			tsk->thread.error_code = error_code;
			tsk->thread.trap_no = trapnr;
			die(str, regs, error_code);
		}
		return;
	}
}

#define DO_ERROR(trapnr, signr, str, name) \
asmlinkage void do_##name(struct pt_regs * regs, long error_code) \
{ \
	if (notify_die(DIE_TRAP, str, regs, error_code, trapnr, signr) \
							== NOTIFY_STOP) \
		return; \
	conditional_sti(regs);						\
	do_trap(trapnr, signr, str, regs, error_code, NULL); \
}

#define DO_ERROR_INFO(trapnr, signr, str, name, sicode, siaddr) \
asmlinkage void do_##name(struct pt_regs * regs, long error_code) \
{ \
	siginfo_t info; \
	info.si_signo = signr; \
	info.si_errno = 0; \
	info.si_code = sicode; \
	info.si_addr = (void __user *)siaddr; \
	if (notify_die(DIE_TRAP, str, regs, error_code, trapnr, signr) \
							== NOTIFY_STOP) \
		return; \
	conditional_sti(regs);						\
	do_trap(trapnr, signr, str, regs, error_code, &info); \
}

DO_ERROR_INFO( 0, SIGFPE,  "divide error", divide_error, FPE_INTDIV, regs->rip)
DO_ERROR( 4, SIGSEGV, "overflow", overflow)
DO_ERROR( 5, SIGSEGV, "bounds", bounds)
DO_ERROR_INFO( 6, SIGILL,  "invalid opcode", invalid_op, ILL_ILLOPN, regs->rip)
DO_ERROR( 7, SIGSEGV, "device not available", device_not_available)
DO_ERROR( 9, SIGFPE,  "coprocessor segment overrun", coprocessor_segment_overrun)
DO_ERROR(10, SIGSEGV, "invalid TSS", invalid_TSS)
DO_ERROR(11, SIGBUS,  "segment not present", segment_not_present)
DO_ERROR_INFO(17, SIGBUS, "alignment check", alignment_check, BUS_ADRALN, 0)
DO_ERROR(18, SIGSEGV, "reserved", reserved)

/* Runs on IST stack */
asmlinkage void do_stack_segment(struct pt_regs *regs, long error_code)
{
	if (notify_die(DIE_TRAP, "stack segment", regs, error_code,
			12, SIGBUS) == NOTIFY_STOP)
		return;
	preempt_conditional_sti(regs);
	do_trap(12, SIGBUS, "stack segment", regs, error_code, NULL);
	preempt_conditional_cli(regs);
}

asmlinkage void do_double_fault(struct pt_regs * regs, long error_code)
{
	static const char str[] = "double fault";
	struct task_struct *tsk = current;

	/* Return not checked because double check cannot be ignored */
	notify_die(DIE_TRAP, str, regs, error_code, 8, SIGSEGV);

	tsk->thread.error_code = error_code;
	tsk->thread.trap_no = 8;

	/* This is always a kernel trap and never fixable (and thus must
	   never return). */
	for (;;)
		die(str, regs, error_code);
}

asmlinkage void __kprobes do_general_protection(struct pt_regs * regs,
						long error_code)
{
	struct task_struct *tsk = current;

	conditional_sti(regs);

	if (user_mode(regs)) {
		tsk->thread.error_code = error_code;
		tsk->thread.trap_no = 13;

		if (show_unhandled_signals && unhandled_signal(tsk, SIGSEGV) &&
		    printk_ratelimit())
			printk(KERN_INFO
		       "%s[%d] general protection rip:%lx rsp:%lx error:%lx\n",
			       tsk->comm, tsk->pid,
			       regs->rip, regs->rsp, error_code); 

		force_sig(SIGSEGV, tsk);
		return;
	} 

	/* kernel gp */
	{
		const struct exception_table_entry *fixup;
		fixup = search_exception_tables(regs->rip);
		if (fixup) {
			regs->rip = fixup->fixup;
			return;
		}

		tsk->thread.error_code = error_code;
		tsk->thread.trap_no = 13;
		if (notify_die(DIE_GPF, "general protection fault", regs,
					error_code, 13, SIGSEGV) == NOTIFY_STOP)
			return;
		die("general protection fault", regs, error_code);
	}
}

static __kprobes void
mem_parity_error(unsigned char reason, struct pt_regs * regs)
{
	printk(KERN_EMERG "Uhhuh. NMI received for unknown reason %02x.\n",
		reason);
	printk(KERN_EMERG "You have some hardware problem, likely on the PCI bus.\n");

#if defined(CONFIG_EDAC)
	if(edac_handler_set()) {
		edac_atomic_assert_error();
		return;
	}
#endif

	if (panic_on_unrecovered_nmi)
		panic("NMI: Not continuing");

	printk(KERN_EMERG "Dazed and confused, but trying to continue\n");

	/* Clear and disable the memory parity error line. */
	reason = (reason & 0xf) | 4;
	outb(reason, 0x61);
}

static __kprobes void
io_check_error(unsigned char reason, struct pt_regs * regs)
{
	printk("NMI: IOCK error (debug interrupt?)\n");
	show_registers(regs);

	/* Re-enable the IOCK line, wait for a few seconds */
	reason = (reason & 0xf) | 8;
	outb(reason, 0x61);
	mdelay(2000);
	reason &= ~8;
	outb(reason, 0x61);
}

static __kprobes void
unknown_nmi_error(unsigned char reason, struct pt_regs * regs)
{
	printk(KERN_EMERG "Uhhuh. NMI received for unknown reason %02x.\n",
		reason);
	printk(KERN_EMERG "Do you have a strange power saving mode enabled?\n");

	if (panic_on_unrecovered_nmi)
		panic("NMI: Not continuing");

	printk(KERN_EMERG "Dazed and confused, but trying to continue\n");
}

/* Runs on IST stack. This code must keep interrupts off all the time.
   Nested NMIs are prevented by the CPU. */
asmlinkage __kprobes void default_do_nmi(struct pt_regs *regs)
{
	unsigned char reason = 0;
	int cpu;

	cpu = smp_processor_id();

	/* Only the BSP gets external NMIs from the system.  */
	if (!cpu)
		reason = get_nmi_reason();

	if (!(reason & 0xc0)) {
		if (notify_die(DIE_NMI_IPI, "nmi_ipi", regs, reason, 2, SIGINT)
								== NOTIFY_STOP)
			return;
		/*
		 * Ok, so this is none of the documented NMI sources,
		 * so it must be the NMI watchdog.
		 */
		if (nmi_watchdog_tick(regs,reason))
			return;
		if (!do_nmi_callback(regs,cpu))
			unknown_nmi_error(reason, regs);

		return;
	}
	if (notify_die(DIE_NMI, "nmi", regs, reason, 2, SIGINT) == NOTIFY_STOP)
		return; 

	/* AK: following checks seem to be broken on modern chipsets. FIXME */

	if (reason & 0x80)
		mem_parity_error(reason, regs);
	if (reason & 0x40)
		io_check_error(reason, regs);
}

/* runs on IST stack. */
asmlinkage void __kprobes do_int3(struct pt_regs * regs, long error_code)
{
	trace_hardirqs_fixup();

	if (notify_die(DIE_INT3, "int3", regs, error_code, 3, SIGTRAP) == NOTIFY_STOP) {
		return;
	}
	preempt_conditional_sti(regs);
	do_trap(3, SIGTRAP, "int3", regs, error_code, NULL);
	preempt_conditional_cli(regs);
}

/* Help handler running on IST stack to switch back to user stack
   for scheduling or signal handling. The actual stack switch is done in
   entry.S */
asmlinkage __kprobes struct pt_regs *sync_regs(struct pt_regs *eregs)
{
	struct pt_regs *regs = eregs;
	/* Did already sync */
	if (eregs == (struct pt_regs *)eregs->rsp)
		;
	/* Exception from user space */
	else if (user_mode(eregs))
		regs = task_pt_regs(current);
	/* Exception from kernel and interrupts are enabled. Move to
 	   kernel process stack. */
	else if (eregs->eflags & X86_EFLAGS_IF)
		regs = (struct pt_regs *)(eregs->rsp -= sizeof(struct pt_regs));
	if (eregs != regs)
		*regs = *eregs;
	return regs;
}

/* runs on IST stack. */
asmlinkage void __kprobes do_debug(struct pt_regs * regs,
				   unsigned long error_code)
{
	unsigned long condition;
	struct task_struct *tsk = current;
	siginfo_t info;

	trace_hardirqs_fixup();

	get_debugreg(condition, 6);

	if (notify_die(DIE_DEBUG, "debug", regs, condition, error_code,
						SIGTRAP) == NOTIFY_STOP)
		return;

	preempt_conditional_sti(regs);

	/* Mask out spurious debug traps due to lazy DR7 setting */
	if (condition & (DR_TRAP0|DR_TRAP1|DR_TRAP2|DR_TRAP3)) {
		if (!tsk->thread.debugreg7) { 
			goto clear_dr7;
		}
	}

	tsk->thread.debugreg6 = condition;

	/* Mask out spurious TF errors due to lazy TF clearing */
	if (condition & DR_STEP) {
		/*
		 * The TF error should be masked out only if the current
		 * process is not traced and if the TRAP flag has been set
		 * previously by a tracing process (condition detected by
		 * the PT_DTRACE flag); remember that the i386 TRAP flag
		 * can be modified by the process itself in user mode,
		 * allowing programs to debug themselves without the ptrace()
		 * interface.
		 */
                if (!user_mode(regs))
                       goto clear_TF_reenable;
		/*
		 * Was the TF flag set by a debugger? If so, clear it now,
		 * so that register information is correct.
		 */
		if (tsk->ptrace & PT_DTRACE) {
			regs->eflags &= ~TF_MASK;
			tsk->ptrace &= ~PT_DTRACE;
		}
	}

	/* Ok, finally something we can handle */
	tsk->thread.trap_no = 1;
	tsk->thread.error_code = error_code;
	info.si_signo = SIGTRAP;
	info.si_errno = 0;
	info.si_code = TRAP_BRKPT;
	info.si_addr = user_mode(regs) ? (void __user *)regs->rip : NULL;
	force_sig_info(SIGTRAP, &info, tsk);

clear_dr7:
	set_debugreg(0UL, 7);
	preempt_conditional_cli(regs);
	return;

clear_TF_reenable:
	set_tsk_thread_flag(tsk, TIF_SINGLESTEP);
	regs->eflags &= ~TF_MASK;
	preempt_conditional_cli(regs);
}

static int kernel_math_error(struct pt_regs *regs, const char *str, int trapnr)
{
	const struct exception_table_entry *fixup;
	fixup = search_exception_tables(regs->rip);
	if (fixup) {
		regs->rip = fixup->fixup;
		return 1;
	}
	notify_die(DIE_GPF, str, regs, 0, trapnr, SIGFPE);
	/* Illegal floating point operation in the kernel */
	current->thread.trap_no = trapnr;
	die(str, regs, 0);
	return 0;
}

/*
 * Note that we play around with the 'TS' bit in an attempt to get
 * the correct behaviour even in the presence of the asynchronous
 * IRQ13 behaviour
 */
asmlinkage void do_coprocessor_error(struct pt_regs *regs)
{
	void __user *rip = (void __user *)(regs->rip);
	struct task_struct * task;
	siginfo_t info;
	unsigned short cwd, swd;

	conditional_sti(regs);
	if (!user_mode(regs) &&
	    kernel_math_error(regs, "kernel x87 math error", 16))
		return;

	/*
	 * Save the info for the exception handler and clear the error.
	 */
	task = current;
	save_init_fpu(task);
	task->thread.trap_no = 16;
	task->thread.error_code = 0;
	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_code = __SI_FAULT;
	info.si_addr = rip;
	/*
	 * (~cwd & swd) will mask out exceptions that are not set to unmasked
	 * status.  0x3f is the exception bits in these regs, 0x200 is the
	 * C1 reg you need in case of a stack fault, 0x040 is the stack
	 * fault bit.  We should only be taking one exception at a time,
	 * so if this combination doesn't produce any single exception,
	 * then we have a bad program that isn't synchronizing its FPU usage
	 * and it will suffer the consequences since we won't be able to
	 * fully reproduce the context of the exception
	 */
	cwd = get_fpu_cwd(task);
	swd = get_fpu_swd(task);
	switch (swd & ~cwd & 0x3f) {
		case 0x000:
		default:
			break;
		case 0x001: /* Invalid Op */
			/*
			 * swd & 0x240 == 0x040: Stack Underflow
			 * swd & 0x240 == 0x240: Stack Overflow
			 * User must clear the SF bit (0x40) if set
			 */
			info.si_code = FPE_FLTINV;
			break;
		case 0x002: /* Denormalize */
		case 0x010: /* Underflow */
			info.si_code = FPE_FLTUND;
			break;
		case 0x004: /* Zero Divide */
			info.si_code = FPE_FLTDIV;
			break;
		case 0x008: /* Overflow */
			info.si_code = FPE_FLTOVF;
			break;
		case 0x020: /* Precision */
			info.si_code = FPE_FLTRES;
			break;
	}
	force_sig_info(SIGFPE, &info, task);
}

asmlinkage void bad_intr(void)
{
	printk("bad interrupt"); 
}

asmlinkage void do_simd_coprocessor_error(struct pt_regs *regs)
{
	void __user *rip = (void __user *)(regs->rip);
	struct task_struct * task;
	siginfo_t info;
	unsigned short mxcsr;

	conditional_sti(regs);
	if (!user_mode(regs) &&
        	kernel_math_error(regs, "kernel simd math error", 19))
		return;

	/*
	 * Save the info for the exception handler and clear the error.
	 */
	task = current;
	save_init_fpu(task);
	task->thread.trap_no = 19;
	task->thread.error_code = 0;
	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_code = __SI_FAULT;
	info.si_addr = rip;
	/*
	 * The SIMD FPU exceptions are handled a little differently, as there
	 * is only a single status/control register.  Thus, to determine which
	 * unmasked exception was caught we must mask the exception mask bits
	 * at 0x1f80, and then use these to mask the exception bits at 0x3f.
	 */
	mxcsr = get_fpu_mxcsr(task);
	switch (~((mxcsr & 0x1f80) >> 7) & (mxcsr & 0x3f)) {
		case 0x000:
		default:
			break;
		case 0x001: /* Invalid Op */
			info.si_code = FPE_FLTINV;
			break;
		case 0x002: /* Denormalize */
		case 0x010: /* Underflow */
			info.si_code = FPE_FLTUND;
			break;
		case 0x004: /* Zero Divide */
			info.si_code = FPE_FLTDIV;
			break;
		case 0x008: /* Overflow */
			info.si_code = FPE_FLTOVF;
			break;
		case 0x020: /* Precision */
			info.si_code = FPE_FLTRES;
			break;
	}
	force_sig_info(SIGFPE, &info, task);
}

asmlinkage void do_spurious_interrupt_bug(struct pt_regs * regs)
{
}

asmlinkage void __attribute__((weak)) smp_thermal_interrupt(void)
{
}

asmlinkage void __attribute__((weak)) mce_threshold_interrupt(void)
{
}

/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 *
 * Careful.. There are problems with IBM-designed IRQ13 behaviour.
 * Don't touch unless you *really* know how it works.
 */
asmlinkage void math_state_restore(void)
{
	struct task_struct *me = current;
	clts();			/* Allow maths ops (or we recurse) */

	if (!used_math())
		init_fpu(me);
	restore_fpu_checking(&me->thread.i387.fxsave);
	task_thread_info(me)->status |= TS_USEDFPU;
	me->fpu_counter++;
}

void __init trap_init(void)
{
	set_intr_gate(0,&divide_error);
	set_intr_gate_ist(1,&debug,DEBUG_STACK);
	set_intr_gate_ist(2,&nmi,NMI_STACK);
 	set_system_gate_ist(3,&int3,DEBUG_STACK); /* int3 can be called from all */
	set_system_gate(4,&overflow);	/* int4 can be called from all */
	set_intr_gate(5,&bounds);
	set_intr_gate(6,&invalid_op);
	set_intr_gate(7,&device_not_available);
	set_intr_gate_ist(8,&double_fault, DOUBLEFAULT_STACK);
	set_intr_gate(9,&coprocessor_segment_overrun);
	set_intr_gate(10,&invalid_TSS);
	set_intr_gate(11,&segment_not_present);
	set_intr_gate_ist(12,&stack_segment,STACKFAULT_STACK);
	set_intr_gate(13,&general_protection);
	set_intr_gate(14,&page_fault);
	set_intr_gate(15,&spurious_interrupt_bug);
	set_intr_gate(16,&coprocessor_error);
	set_intr_gate(17,&alignment_check);
#ifdef CONFIG_X86_MCE
	set_intr_gate_ist(18,&machine_check, MCE_STACK); 
#endif
	set_intr_gate(19,&simd_coprocessor_error);

#ifdef CONFIG_IA32_EMULATION
	set_system_gate(IA32_SYSCALL_VECTOR, ia32_syscall);
#endif
       
	/*
	 * Should be a barrier for any external CPU state.
	 */
	cpu_init();
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
	kstack_depth_to_print = simple_strtoul(s,NULL,0);
	return 0;
}
early_param("kstack", kstack_setup);
