// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/kernel/traps.c
 *
 *  Copyright (C) 1995-2009 Russell King
 *  Fragments that appear the same as linux/arch/i386/kernel/traps.c (C) Linus Torvalds
 *
 *  'traps.c' handles hardware exceptions after we have saved some state in
 *  'linux/arch/arm/lib/traps.S'.  Mostly a debugging aid, but will probably
 *  kill the offending process.
 */
#include <linux/signal.h>
#include <linux/personality.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>
#include <linux/kdebug.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/kexec.h>
#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/irq.h>

#include <linux/atomic.h>
#include <asm/cacheflush.h>
#include <asm/exception.h>
#include <asm/unistd.h>
#include <asm/traps.h>
#include <asm/ptrace.h>
#include <asm/unwind.h>
#include <asm/tls.h>
#include <asm/system_misc.h>
#include <asm/opcodes.h>


static const char *handler[]= {
	"prefetch abort",
	"data abort",
	"address exception",
	"interrupt",
	"undefined instruction",
};

void *vectors_page;

#ifdef CONFIG_DEBUG_USER
unsigned int user_debug;

static int __init user_debug_setup(char *str)
{
	get_option(&str, &user_debug);
	return 1;
}
__setup("user_debug=", user_debug_setup);
#endif

static void dump_mem(const char *, const char *, unsigned long, unsigned long);

void dump_backtrace_entry(unsigned long where, unsigned long from, unsigned long frame)
{
#ifdef CONFIG_KALLSYMS
	printk("[<%08lx>] (%ps) from [<%08lx>] (%pS)\n", where, (void *)where, from, (void *)from);
#else
	printk("Function entered at [<%08lx>] from [<%08lx>]\n", where, from);
#endif

	if (in_entry_text(from))
		dump_mem("", "Exception stack", frame + 4, frame + 4 + sizeof(struct pt_regs));
}

void dump_backtrace_stm(u32 *stack, u32 instruction)
{
	char str[80], *p;
	unsigned int x;
	int reg;

	for (reg = 10, x = 0, p = str; reg >= 0; reg--) {
		if (instruction & BIT(reg)) {
			p += sprintf(p, " r%d:%08x", reg, *stack--);
			if (++x == 6) {
				x = 0;
				p = str;
				printk("%s\n", str);
			}
		}
	}
	if (p != str)
		printk("%s\n", str);
}

#ifndef CONFIG_ARM_UNWIND
/*
 * Stack pointers should always be within the kernels view of
 * physical memory.  If it is not there, then we can't dump
 * out any information relating to the stack.
 */
static int verify_stack(unsigned long sp)
{
	if (sp < PAGE_OFFSET ||
	    (sp > (unsigned long)high_memory && high_memory != NULL))
		return -EFAULT;

	return 0;
}
#endif

/*
 * Dump out the contents of some memory nicely...
 */
static void dump_mem(const char *lvl, const char *str, unsigned long bottom,
		     unsigned long top)
{
	unsigned long first;
	mm_segment_t fs;
	int i;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);

	printk("%s%s(0x%08lx to 0x%08lx)\n", lvl, str, bottom, top);

	for (first = bottom & ~31; first < top; first += 32) {
		unsigned long p;
		char str[sizeof(" 12345678") * 8 + 1];

		memset(str, ' ', sizeof(str));
		str[sizeof(str) - 1] = '\0';

		for (p = first, i = 0; i < 8 && p < top; i++, p += 4) {
			if (p >= bottom && p < top) {
				unsigned long val;
				if (__get_user(val, (unsigned long *)p) == 0)
					sprintf(str + i * 9, " %08lx", val);
				else
					sprintf(str + i * 9, " ????????");
			}
		}
		printk("%s%04lx:%s\n", lvl, first & 0xffff, str);
	}

	set_fs(fs);
}

static void __dump_instr(const char *lvl, struct pt_regs *regs)
{
	unsigned long addr = instruction_pointer(regs);
	const int thumb = thumb_mode(regs);
	const int width = thumb ? 4 : 8;
	char str[sizeof("00000000 ") * 5 + 2 + 1], *p = str;
	int i;

	/*
	 * Note that we now dump the code first, just in case the backtrace
	 * kills us.
	 */

	for (i = -4; i < 1 + !!thumb; i++) {
		unsigned int val, bad;

		if (thumb)
			bad = get_user(val, &((u16 *)addr)[i]);
		else
			bad = get_user(val, &((u32 *)addr)[i]);

		if (!bad)
			p += sprintf(p, i == 0 ? "(%0*x) " : "%0*x ",
					width, val);
		else {
			p += sprintf(p, "bad PC value");
			break;
		}
	}
	printk("%sCode: %s\n", lvl, str);
}

static void dump_instr(const char *lvl, struct pt_regs *regs)
{
	mm_segment_t fs;

	if (!user_mode(regs)) {
		fs = get_fs();
		set_fs(KERNEL_DS);
		__dump_instr(lvl, regs);
		set_fs(fs);
	} else {
		__dump_instr(lvl, regs);
	}
}

#ifdef CONFIG_ARM_UNWIND
static inline void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk)
{
	unwind_backtrace(regs, tsk);
}
#else
static void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk)
{
	unsigned int fp, mode;
	int ok = 1;

	printk("Backtrace: ");

	if (!tsk)
		tsk = current;

	if (regs) {
		fp = frame_pointer(regs);
		mode = processor_mode(regs);
	} else if (tsk != current) {
		fp = thread_saved_fp(tsk);
		mode = 0x10;
	} else {
		asm("mov %0, fp" : "=r" (fp) : : "cc");
		mode = 0x10;
	}

	if (!fp) {
		pr_cont("no frame pointer");
		ok = 0;
	} else if (verify_stack(fp)) {
		pr_cont("invalid frame pointer 0x%08x", fp);
		ok = 0;
	} else if (fp < (unsigned long)end_of_stack(tsk))
		pr_cont("frame pointer underflow");
	pr_cont("\n");

	if (ok)
		c_backtrace(fp, mode);
}
#endif

void show_stack(struct task_struct *tsk, unsigned long *sp)
{
	dump_backtrace(NULL, tsk);
	barrier();
}

#ifdef CONFIG_PREEMPT
#define S_PREEMPT " PREEMPT"
#else
#define S_PREEMPT ""
#endif
#ifdef CONFIG_SMP
#define S_SMP " SMP"
#else
#define S_SMP ""
#endif
#ifdef CONFIG_THUMB2_KERNEL
#define S_ISA " THUMB2"
#else
#define S_ISA " ARM"
#endif

static int __die(const char *str, int err, struct pt_regs *regs)
{
	struct task_struct *tsk = current;
	static int die_counter;
	int ret;

	pr_emerg("Internal error: %s: %x [#%d]" S_PREEMPT S_SMP S_ISA "\n",
	         str, err, ++die_counter);

	/* trap and error numbers are mostly meaningless on ARM */
	ret = notify_die(DIE_OOPS, str, regs, err, tsk->thread.trap_no, SIGSEGV);
	if (ret == NOTIFY_STOP)
		return 1;

	print_modules();
	__show_regs(regs);
	pr_emerg("Process %.*s (pid: %d, stack limit = 0x%p)\n",
		 TASK_COMM_LEN, tsk->comm, task_pid_nr(tsk), end_of_stack(tsk));

	if (!user_mode(regs) || in_interrupt()) {
		dump_mem(KERN_EMERG, "Stack: ", regs->ARM_sp,
			 THREAD_SIZE + (unsigned long)task_stack_page(tsk));
		dump_backtrace(regs, tsk);
		dump_instr(KERN_EMERG, regs);
	}

	return 0;
}

static arch_spinlock_t die_lock = __ARCH_SPIN_LOCK_UNLOCKED;
static int die_owner = -1;
static unsigned int die_nest_count;

static unsigned long oops_begin(void)
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

static void oops_end(unsigned long flags, struct pt_regs *regs, int signr)
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

	if (in_interrupt())
		panic("Fatal exception in interrupt");
	if (panic_on_oops)
		panic("Fatal exception");
	if (signr)
		do_exit(signr);
}

/*
 * This function is protected against re-entrancy.
 */
void die(const char *str, struct pt_regs *regs, int err)
{
	enum bug_trap_type bug_type = BUG_TRAP_TYPE_NONE;
	unsigned long flags = oops_begin();
	int sig = SIGSEGV;

	if (!user_mode(regs))
		bug_type = report_bug(regs->ARM_pc, regs);
	if (bug_type != BUG_TRAP_TYPE_NONE)
		str = "Oops - BUG";

	if (__die(str, err, regs))
		sig = 0;

	oops_end(flags, regs, sig);
}

void arm_notify_die(const char *str, struct pt_regs *regs,
		int signo, int si_code, void __user *addr,
		unsigned long err, unsigned long trap)
{
	if (user_mode(regs)) {
		current->thread.error_code = err;
		current->thread.trap_no = trap;

		force_sig_fault(signo, si_code, addr, current);
	} else {
		die(str, regs, err);
	}
}

#ifdef CONFIG_GENERIC_BUG

int is_valid_bugaddr(unsigned long pc)
{
#ifdef CONFIG_THUMB2_KERNEL
	u16 bkpt;
	u16 insn = __opcode_to_mem_thumb16(BUG_INSTR_VALUE);
#else
	u32 bkpt;
	u32 insn = __opcode_to_mem_arm(BUG_INSTR_VALUE);
#endif

	if (probe_kernel_address((unsigned *)pc, bkpt))
		return 0;

	return bkpt == insn;
}

#endif

static LIST_HEAD(undef_hook);
static DEFINE_RAW_SPINLOCK(undef_lock);

void register_undef_hook(struct undef_hook *hook)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&undef_lock, flags);
	list_add(&hook->node, &undef_hook);
	raw_spin_unlock_irqrestore(&undef_lock, flags);
}

void unregister_undef_hook(struct undef_hook *hook)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&undef_lock, flags);
	list_del(&hook->node);
	raw_spin_unlock_irqrestore(&undef_lock, flags);
}

static nokprobe_inline
int call_undef_hook(struct pt_regs *regs, unsigned int instr)
{
	struct undef_hook *hook;
	unsigned long flags;
	int (*fn)(struct pt_regs *regs, unsigned int instr) = NULL;

	raw_spin_lock_irqsave(&undef_lock, flags);
	list_for_each_entry(hook, &undef_hook, node)
		if ((instr & hook->instr_mask) == hook->instr_val &&
		    (regs->ARM_cpsr & hook->cpsr_mask) == hook->cpsr_val)
			fn = hook->fn;
	raw_spin_unlock_irqrestore(&undef_lock, flags);

	return fn ? fn(regs, instr) : 1;
}

asmlinkage void do_undefinstr(struct pt_regs *regs)
{
	unsigned int instr;
	void __user *pc;

	pc = (void __user *)instruction_pointer(regs);

	if (processor_mode(regs) == SVC_MODE) {
#ifdef CONFIG_THUMB2_KERNEL
		if (thumb_mode(regs)) {
			instr = __mem_to_opcode_thumb16(((u16 *)pc)[0]);
			if (is_wide_instruction(instr)) {
				u16 inst2;
				inst2 = __mem_to_opcode_thumb16(((u16 *)pc)[1]);
				instr = __opcode_thumb32_compose(instr, inst2);
			}
		} else
#endif
			instr = __mem_to_opcode_arm(*(u32 *) pc);
	} else if (thumb_mode(regs)) {
		if (get_user(instr, (u16 __user *)pc))
			goto die_sig;
		instr = __mem_to_opcode_thumb16(instr);
		if (is_wide_instruction(instr)) {
			unsigned int instr2;
			if (get_user(instr2, (u16 __user *)pc+1))
				goto die_sig;
			instr2 = __mem_to_opcode_thumb16(instr2);
			instr = __opcode_thumb32_compose(instr, instr2);
		}
	} else {
		if (get_user(instr, (u32 __user *)pc))
			goto die_sig;
		instr = __mem_to_opcode_arm(instr);
	}

	if (call_undef_hook(regs, instr) == 0)
		return;

die_sig:
#ifdef CONFIG_DEBUG_USER
	if (user_debug & UDBG_UNDEFINED) {
		pr_info("%s (%d): undefined instruction: pc=%p\n",
			current->comm, task_pid_nr(current), pc);
		__show_regs(regs);
		dump_instr(KERN_INFO, regs);
	}
#endif
	arm_notify_die("Oops - undefined instruction", regs,
		       SIGILL, ILL_ILLOPC, pc, 0, 6);
}
NOKPROBE_SYMBOL(do_undefinstr)

/*
 * Handle FIQ similarly to NMI on x86 systems.
 *
 * The runtime environment for NMIs is extremely restrictive
 * (NMIs can pre-empt critical sections meaning almost all locking is
 * forbidden) meaning this default FIQ handling must only be used in
 * circumstances where non-maskability improves robustness, such as
 * watchdog or debug logic.
 *
 * This handler is not appropriate for general purpose use in drivers
 * platform code and can be overrideen using set_fiq_handler.
 */
asmlinkage void __exception_irq_entry handle_fiq_as_nmi(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	nmi_enter();

	/* nop. FIQ handlers for special arch/arm features can be added here. */

	nmi_exit();

	set_irq_regs(old_regs);
}

/*
 * bad_mode handles the impossible case in the vectors.  If you see one of
 * these, then it's extremely serious, and could mean you have buggy hardware.
 * It never returns, and never tries to sync.  We hope that we can at least
 * dump out some state information...
 */
asmlinkage void bad_mode(struct pt_regs *regs, int reason)
{
	console_verbose();

	pr_crit("Bad mode in %s handler detected\n", handler[reason]);

	die("Oops - bad mode", regs, 0);
	local_irq_disable();
	panic("bad mode");
}

static int bad_syscall(int n, struct pt_regs *regs)
{
	if ((current->personality & PER_MASK) != PER_LINUX) {
		send_sig(SIGSEGV, current, 1);
		return regs->ARM_r0;
	}

#ifdef CONFIG_DEBUG_USER
	if (user_debug & UDBG_SYSCALL) {
		pr_err("[%d] %s: obsolete system call %08x.\n",
			task_pid_nr(current), current->comm, n);
		dump_instr(KERN_ERR, regs);
	}
#endif

	arm_notify_die("Oops - bad syscall", regs, SIGILL, ILL_ILLTRP,
		       (void __user *)instruction_pointer(regs) -
			 (thumb_mode(regs) ? 2 : 4),
		       n, 0);

	return regs->ARM_r0;
}

static inline int
__do_cache_op(unsigned long start, unsigned long end)
{
	int ret;

	do {
		unsigned long chunk = min(PAGE_SIZE, end - start);

		if (fatal_signal_pending(current))
			return 0;

		ret = flush_cache_user_range(start, start + chunk);
		if (ret)
			return ret;

		cond_resched();
		start += chunk;
	} while (start < end);

	return 0;
}

static inline int
do_cache_op(unsigned long start, unsigned long end, int flags)
{
	if (end < start || flags)
		return -EINVAL;

	if (!access_ok(start, end - start))
		return -EFAULT;

	return __do_cache_op(start, end);
}

/*
 * Handle all unrecognised system calls.
 *  0x9f0000 - 0x9fffff are some more esoteric system calls
 */
#define NR(x) ((__ARM_NR_##x) - __ARM_NR_BASE)
asmlinkage int arm_syscall(int no, struct pt_regs *regs)
{
	if ((no >> 16) != (__ARM_NR_BASE>> 16))
		return bad_syscall(no, regs);

	switch (no & 0xffff) {
	case 0: /* branch through 0 */
		arm_notify_die("branch through zero", regs,
			       SIGSEGV, SEGV_MAPERR, NULL, 0, 0);
		return 0;

	case NR(breakpoint): /* SWI BREAK_POINT */
		regs->ARM_pc -= thumb_mode(regs) ? 2 : 4;
		ptrace_break(current, regs);
		return regs->ARM_r0;

	/*
	 * Flush a region from virtual address 'r0' to virtual address 'r1'
	 * _exclusive_.  There is no alignment requirement on either address;
	 * user space does not need to know the hardware cache layout.
	 *
	 * r2 contains flags.  It should ALWAYS be passed as ZERO until it
	 * is defined to be something else.  For now we ignore it, but may
	 * the fires of hell burn in your belly if you break this rule. ;)
	 *
	 * (at a later date, we may want to allow this call to not flush
	 * various aspects of the cache.  Passing '0' will guarantee that
	 * everything necessary gets flushed to maintain consistency in
	 * the specified region).
	 */
	case NR(cacheflush):
		return do_cache_op(regs->ARM_r0, regs->ARM_r1, regs->ARM_r2);

	case NR(usr26):
		if (!(elf_hwcap & HWCAP_26BIT))
			break;
		regs->ARM_cpsr &= ~MODE32_BIT;
		return regs->ARM_r0;

	case NR(usr32):
		if (!(elf_hwcap & HWCAP_26BIT))
			break;
		regs->ARM_cpsr |= MODE32_BIT;
		return regs->ARM_r0;

	case NR(set_tls):
		set_tls(regs->ARM_r0);
		return 0;

	case NR(get_tls):
		return current_thread_info()->tp_value[0];

	default:
		/* Calls 9f00xx..9f07ff are defined to return -ENOSYS
		   if not implemented, rather than raising SIGILL.  This
		   way the calling program can gracefully determine whether
		   a feature is supported.  */
		if ((no & 0xffff) <= 0x7ff)
			return -ENOSYS;
		break;
	}
#ifdef CONFIG_DEBUG_USER
	/*
	 * experience shows that these seem to indicate that
	 * something catastrophic has happened
	 */
	if (user_debug & UDBG_SYSCALL) {
		pr_err("[%d] %s: arm syscall %d\n",
		       task_pid_nr(current), current->comm, no);
		dump_instr("", regs);
		if (user_mode(regs)) {
			__show_regs(regs);
			c_backtrace(frame_pointer(regs), processor_mode(regs));
		}
	}
#endif
	arm_notify_die("Oops - bad syscall(2)", regs, SIGILL, ILL_ILLTRP,
		       (void __user *)instruction_pointer(regs) -
			 (thumb_mode(regs) ? 2 : 4),
		       no, 0);
	return 0;
}

#ifdef CONFIG_TLS_REG_EMUL

/*
 * We might be running on an ARMv6+ processor which should have the TLS
 * register but for some reason we can't use it, or maybe an SMP system
 * using a pre-ARMv6 processor (there are apparently a few prototypes like
 * that in existence) and therefore access to that register must be
 * emulated.
 */

static int get_tp_trap(struct pt_regs *regs, unsigned int instr)
{
	int reg = (instr >> 12) & 15;
	if (reg == 15)
		return 1;
	regs->uregs[reg] = current_thread_info()->tp_value[0];
	regs->ARM_pc += 4;
	return 0;
}

static struct undef_hook arm_mrc_hook = {
	.instr_mask	= 0x0fff0fff,
	.instr_val	= 0x0e1d0f70,
	.cpsr_mask	= PSR_T_BIT,
	.cpsr_val	= 0,
	.fn		= get_tp_trap,
};

static int __init arm_mrc_hook_init(void)
{
	register_undef_hook(&arm_mrc_hook);
	return 0;
}

late_initcall(arm_mrc_hook_init);

#endif

/*
 * A data abort trap was taken, but we did not handle the instruction.
 * Try to abort the user program, or panic if it was the kernel.
 */
asmlinkage void
baddataabort(int code, unsigned long instr, struct pt_regs *regs)
{
	unsigned long addr = instruction_pointer(regs);

#ifdef CONFIG_DEBUG_USER
	if (user_debug & UDBG_BADABORT) {
		pr_err("[%d] %s: bad data abort: code %d instr 0x%08lx\n",
		       task_pid_nr(current), current->comm, code, instr);
		dump_instr(KERN_ERR, regs);
		show_pte(current->mm, addr);
	}
#endif

	arm_notify_die("unknown data abort code", regs,
		       SIGILL, ILL_ILLOPC, (void __user *)addr, instr, 0);
}

void __readwrite_bug(const char *fn)
{
	pr_err("%s called, but not implemented\n", fn);
	BUG();
}
EXPORT_SYMBOL(__readwrite_bug);

void __pte_error(const char *file, int line, pte_t pte)
{
	pr_err("%s:%d: bad pte %08llx.\n", file, line, (long long)pte_val(pte));
}

void __pmd_error(const char *file, int line, pmd_t pmd)
{
	pr_err("%s:%d: bad pmd %08llx.\n", file, line, (long long)pmd_val(pmd));
}

void __pgd_error(const char *file, int line, pgd_t pgd)
{
	pr_err("%s:%d: bad pgd %08llx.\n", file, line, (long long)pgd_val(pgd));
}

asmlinkage void __div0(void)
{
	pr_err("Division by zero in kernel.\n");
	dump_stack();
}
EXPORT_SYMBOL(__div0);

void abort(void)
{
	BUG();

	/* if that doesn't kill us, halt */
	panic("Oops failed to kill thread");
}

void __init trap_init(void)
{
	return;
}

#ifdef CONFIG_KUSER_HELPERS
static void __init kuser_init(void *vectors)
{
	extern char __kuser_helper_start[], __kuser_helper_end[];
	int kuser_sz = __kuser_helper_end - __kuser_helper_start;

	memcpy(vectors + 0x1000 - kuser_sz, __kuser_helper_start, kuser_sz);

	/*
	 * vectors + 0xfe0 = __kuser_get_tls
	 * vectors + 0xfe8 = hardware TLS instruction at 0xffff0fe8
	 */
	if (tls_emu || has_tls_reg)
		memcpy(vectors + 0xfe0, vectors + 0xfe8, 4);
}
#else
static inline void __init kuser_init(void *vectors)
{
}
#endif

void __init early_trap_init(void *vectors_base)
{
#ifndef CONFIG_CPU_V7M
	unsigned long vectors = (unsigned long)vectors_base;
	extern char __stubs_start[], __stubs_end[];
	extern char __vectors_start[], __vectors_end[];
	unsigned i;

	vectors_page = vectors_base;

	/*
	 * Poison the vectors page with an undefined instruction.  This
	 * instruction is chosen to be undefined for both ARM and Thumb
	 * ISAs.  The Thumb version is an undefined instruction with a
	 * branch back to the undefined instruction.
	 */
	for (i = 0; i < PAGE_SIZE / sizeof(u32); i++)
		((u32 *)vectors_base)[i] = 0xe7fddef1;

	/*
	 * Copy the vectors, stubs and kuser helpers (in entry-armv.S)
	 * into the vector page, mapped at 0xffff0000, and ensure these
	 * are visible to the instruction stream.
	 */
	memcpy((void *)vectors, __vectors_start, __vectors_end - __vectors_start);
	memcpy((void *)vectors + 0x1000, __stubs_start, __stubs_end - __stubs_start);

	kuser_init(vectors_base);

	flush_icache_range(vectors, vectors + PAGE_SIZE * 2);
#else /* ifndef CONFIG_CPU_V7M */
	/*
	 * on V7-M there is no need to copy the vector table to a dedicated
	 * memory area. The address is configurable and so a table in the kernel
	 * image can be used.
	 */
#endif
}
