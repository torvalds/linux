/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * Handle hardware traps and faults.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/context_tracking.h>
#include <linux/interrupt.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/kdebug.h>
#include <linux/kgdb.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/ptrace.h>
#include <linux/uprobes.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kexec.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/bug.h>
#include <linux/nmi.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/hardirq.h>
#include <linux/atomic.h>

#include <asm/stacktrace.h>
#include <asm/processor.h>
#include <asm/debugreg.h>
#include <asm/realmode.h>
#include <asm/text-patching.h>
#include <asm/ftrace.h>
#include <asm/traps.h>
#include <asm/desc.h>
#include <asm/fpu/internal.h>
#include <asm/cpu.h>
#include <asm/cpu_entry_area.h>
#include <asm/mce.h>
#include <asm/fixmap.h>
#include <asm/mach_traps.h>
#include <asm/alternative.h>
#include <asm/fpu/xstate.h>
#include <asm/vm86.h>
#include <asm/umip.h>
#include <asm/insn.h>
#include <asm/insn-eval.h>

#ifdef CONFIG_X86_64
#include <asm/x86_init.h>
#include <asm/proto.h>
#else
#include <asm/processor-flags.h>
#include <asm/setup.h>
#include <asm/proto.h>
#endif

DECLARE_BITMAP(system_vectors, NR_VECTORS);

static inline void cond_local_irq_enable(struct pt_regs *regs)
{
	if (regs->flags & X86_EFLAGS_IF)
		local_irq_enable();
}

static inline void cond_local_irq_disable(struct pt_regs *regs)
{
	if (regs->flags & X86_EFLAGS_IF)
		local_irq_disable();
}

__always_inline int is_valid_bugaddr(unsigned long addr)
{
	if (addr < TASK_SIZE_MAX)
		return 0;

	/*
	 * We got #UD, if the text isn't readable we'd have gotten
	 * a different exception.
	 */
	return *(unsigned short *)addr == INSN_UD2;
}

static nokprobe_inline int
do_trap_no_signal(struct task_struct *tsk, int trapnr, const char *str,
		  struct pt_regs *regs,	long error_code)
{
	if (v8086_mode(regs)) {
		/*
		 * Traps 0, 1, 3, 4, and 5 should be forwarded to vm86.
		 * On nmi (interrupt 2), do_trap should not be called.
		 */
		if (trapnr < X86_TRAP_UD) {
			if (!handle_vm86_trap((struct kernel_vm86_regs *) regs,
						error_code, trapnr))
				return 0;
		}
	} else if (!user_mode(regs)) {
		if (fixup_exception(regs, trapnr, error_code, 0))
			return 0;

		tsk->thread.error_code = error_code;
		tsk->thread.trap_nr = trapnr;
		die(str, regs, error_code);
	}

	/*
	 * We want error_code and trap_nr set for userspace faults and
	 * kernelspace faults which result in die(), but not
	 * kernelspace faults which are fixed up.  die() gives the
	 * process no chance to handle the signal and notice the
	 * kernel fault information, so that won't result in polluting
	 * the information about previously queued, but not yet
	 * delivered, faults.  See also exc_general_protection below.
	 */
	tsk->thread.error_code = error_code;
	tsk->thread.trap_nr = trapnr;

	return -1;
}

static void show_signal(struct task_struct *tsk, int signr,
			const char *type, const char *desc,
			struct pt_regs *regs, long error_code)
{
	if (show_unhandled_signals && unhandled_signal(tsk, signr) &&
	    printk_ratelimit()) {
		pr_info("%s[%d] %s%s ip:%lx sp:%lx error:%lx",
			tsk->comm, task_pid_nr(tsk), type, desc,
			regs->ip, regs->sp, error_code);
		print_vma_addr(KERN_CONT " in ", regs->ip);
		pr_cont("\n");
	}
}

static void
do_trap(int trapnr, int signr, char *str, struct pt_regs *regs,
	long error_code, int sicode, void __user *addr)
{
	struct task_struct *tsk = current;

	if (!do_trap_no_signal(tsk, trapnr, str, regs, error_code))
		return;

	show_signal(tsk, signr, "trap ", str, regs, error_code);

	if (!sicode)
		force_sig(signr);
	else
		force_sig_fault(signr, sicode, addr);
}
NOKPROBE_SYMBOL(do_trap);

static void do_error_trap(struct pt_regs *regs, long error_code, char *str,
	unsigned long trapnr, int signr, int sicode, void __user *addr)
{
	RCU_LOCKDEP_WARN(!rcu_is_watching(), "entry code didn't wake RCU");

	if (notify_die(DIE_TRAP, str, regs, error_code, trapnr, signr) !=
			NOTIFY_STOP) {
		cond_local_irq_enable(regs);
		do_trap(trapnr, signr, str, regs, error_code, sicode, addr);
		cond_local_irq_disable(regs);
	}
}

/*
 * Posix requires to provide the address of the faulting instruction for
 * SIGILL (#UD) and SIGFPE (#DE) in the si_addr member of siginfo_t.
 *
 * This address is usually regs->ip, but when an uprobe moved the code out
 * of line then regs->ip points to the XOL code which would confuse
 * anything which analyzes the fault address vs. the unmodified binary. If
 * a trap happened in XOL code then uprobe maps regs->ip back to the
 * original instruction address.
 */
static __always_inline void __user *error_get_trap_addr(struct pt_regs *regs)
{
	return (void __user *)uprobe_get_trap_addr(regs);
}

DEFINE_IDTENTRY(exc_divide_error)
{
	do_error_trap(regs, 0, "divide error", X86_TRAP_DE, SIGFPE,
		      FPE_INTDIV, error_get_trap_addr(regs));
}

DEFINE_IDTENTRY(exc_overflow)
{
	do_error_trap(regs, 0, "overflow", X86_TRAP_OF, SIGSEGV, 0, NULL);
}

#ifdef CONFIG_X86_F00F_BUG
void handle_invalid_op(struct pt_regs *regs)
#else
static inline void handle_invalid_op(struct pt_regs *regs)
#endif
{
	do_error_trap(regs, 0, "invalid opcode", X86_TRAP_UD, SIGILL,
		      ILL_ILLOPN, error_get_trap_addr(regs));
}

static noinstr bool handle_bug(struct pt_regs *regs)
{
	bool handled = false;

	if (!is_valid_bugaddr(regs->ip))
		return handled;

	/*
	 * All lies, just get the WARN/BUG out.
	 */
	instrumentation_begin();
	/*
	 * Since we're emulating a CALL with exceptions, restore the interrupt
	 * state to what it was at the exception site.
	 */
	if (regs->flags & X86_EFLAGS_IF)
		raw_local_irq_enable();
	if (report_bug(regs->ip, regs) == BUG_TRAP_TYPE_WARN) {
		regs->ip += LEN_UD2;
		handled = true;
	}
	if (regs->flags & X86_EFLAGS_IF)
		raw_local_irq_disable();
	instrumentation_end();

	return handled;
}

DEFINE_IDTENTRY_RAW(exc_invalid_op)
{
	irqentry_state_t state;

	/*
	 * We use UD2 as a short encoding for 'CALL __WARN', as such
	 * handle it before exception entry to avoid recursive WARN
	 * in case exception entry is the one triggering WARNs.
	 */
	if (!user_mode(regs) && handle_bug(regs))
		return;

	state = irqentry_enter(regs);
	instrumentation_begin();
	handle_invalid_op(regs);
	instrumentation_end();
	irqentry_exit(regs, state);
}

DEFINE_IDTENTRY(exc_coproc_segment_overrun)
{
	do_error_trap(regs, 0, "coprocessor segment overrun",
		      X86_TRAP_OLD_MF, SIGFPE, 0, NULL);
}

DEFINE_IDTENTRY_ERRORCODE(exc_invalid_tss)
{
	do_error_trap(regs, error_code, "invalid TSS", X86_TRAP_TS, SIGSEGV,
		      0, NULL);
}

DEFINE_IDTENTRY_ERRORCODE(exc_segment_not_present)
{
	do_error_trap(regs, error_code, "segment not present", X86_TRAP_NP,
		      SIGBUS, 0, NULL);
}

DEFINE_IDTENTRY_ERRORCODE(exc_stack_segment)
{
	do_error_trap(regs, error_code, "stack segment", X86_TRAP_SS, SIGBUS,
		      0, NULL);
}

DEFINE_IDTENTRY_ERRORCODE(exc_alignment_check)
{
	char *str = "alignment check";

	if (notify_die(DIE_TRAP, str, regs, error_code, X86_TRAP_AC, SIGBUS) == NOTIFY_STOP)
		return;

	if (!user_mode(regs))
		die("Split lock detected\n", regs, error_code);

	local_irq_enable();

	if (handle_user_split_lock(regs, error_code))
		goto out;

	do_trap(X86_TRAP_AC, SIGBUS, "alignment check", regs,
		error_code, BUS_ADRALN, NULL);

out:
	local_irq_disable();
}

#ifdef CONFIG_VMAP_STACK
__visible void __noreturn handle_stack_overflow(const char *message,
						struct pt_regs *regs,
						unsigned long fault_address)
{
	printk(KERN_EMERG "BUG: stack guard page was hit at %p (stack is %p..%p)\n",
		 (void *)fault_address, current->stack,
		 (char *)current->stack + THREAD_SIZE - 1);
	die(message, regs, 0);

	/* Be absolutely certain we don't return. */
	panic("%s", message);
}
#endif

/*
 * Runs on an IST stack for x86_64 and on a special task stack for x86_32.
 *
 * On x86_64, this is more or less a normal kernel entry.  Notwithstanding the
 * SDM's warnings about double faults being unrecoverable, returning works as
 * expected.  Presumably what the SDM actually means is that the CPU may get
 * the register state wrong on entry, so returning could be a bad idea.
 *
 * Various CPU engineers have promised that double faults due to an IRET fault
 * while the stack is read-only are, in fact, recoverable.
 *
 * On x86_32, this is entered through a task gate, and regs are synthesized
 * from the TSS.  Returning is, in principle, okay, but changes to regs will
 * be lost.  If, for some reason, we need to return to a context with modified
 * regs, the shim code could be adjusted to synchronize the registers.
 *
 * The 32bit #DF shim provides CR2 already as an argument. On 64bit it needs
 * to be read before doing anything else.
 */
DEFINE_IDTENTRY_DF(exc_double_fault)
{
	static const char str[] = "double fault";
	struct task_struct *tsk = current;

#ifdef CONFIG_VMAP_STACK
	unsigned long address = read_cr2();
#endif

#ifdef CONFIG_X86_ESPFIX64
	extern unsigned char native_irq_return_iret[];

	/*
	 * If IRET takes a non-IST fault on the espfix64 stack, then we
	 * end up promoting it to a doublefault.  In that case, take
	 * advantage of the fact that we're not using the normal (TSS.sp0)
	 * stack right now.  We can write a fake #GP(0) frame at TSS.sp0
	 * and then modify our own IRET frame so that, when we return,
	 * we land directly at the #GP(0) vector with the stack already
	 * set up according to its expectations.
	 *
	 * The net result is that our #GP handler will think that we
	 * entered from usermode with the bad user context.
	 *
	 * No need for nmi_enter() here because we don't use RCU.
	 */
	if (((long)regs->sp >> P4D_SHIFT) == ESPFIX_PGD_ENTRY &&
		regs->cs == __KERNEL_CS &&
		regs->ip == (unsigned long)native_irq_return_iret)
	{
		struct pt_regs *gpregs = (struct pt_regs *)this_cpu_read(cpu_tss_rw.x86_tss.sp0) - 1;
		unsigned long *p = (unsigned long *)regs->sp;

		/*
		 * regs->sp points to the failing IRET frame on the
		 * ESPFIX64 stack.  Copy it to the entry stack.  This fills
		 * in gpregs->ss through gpregs->ip.
		 *
		 */
		gpregs->ip	= p[0];
		gpregs->cs	= p[1];
		gpregs->flags	= p[2];
		gpregs->sp	= p[3];
		gpregs->ss	= p[4];
		gpregs->orig_ax = 0;  /* Missing (lost) #GP error code */

		/*
		 * Adjust our frame so that we return straight to the #GP
		 * vector with the expected RSP value.  This is safe because
		 * we won't enable interupts or schedule before we invoke
		 * general_protection, so nothing will clobber the stack
		 * frame we just set up.
		 *
		 * We will enter general_protection with kernel GSBASE,
		 * which is what the stub expects, given that the faulting
		 * RIP will be the IRET instruction.
		 */
		regs->ip = (unsigned long)asm_exc_general_protection;
		regs->sp = (unsigned long)&gpregs->orig_ax;

		return;
	}
#endif

	irqentry_nmi_enter(regs);
	instrumentation_begin();
	notify_die(DIE_TRAP, str, regs, error_code, X86_TRAP_DF, SIGSEGV);

	tsk->thread.error_code = error_code;
	tsk->thread.trap_nr = X86_TRAP_DF;

#ifdef CONFIG_VMAP_STACK
	/*
	 * If we overflow the stack into a guard page, the CPU will fail
	 * to deliver #PF and will send #DF instead.  Similarly, if we
	 * take any non-IST exception while too close to the bottom of
	 * the stack, the processor will get a page fault while
	 * delivering the exception and will generate a double fault.
	 *
	 * According to the SDM (footnote in 6.15 under "Interrupt 14 -
	 * Page-Fault Exception (#PF):
	 *
	 *   Processors update CR2 whenever a page fault is detected. If a
	 *   second page fault occurs while an earlier page fault is being
	 *   delivered, the faulting linear address of the second fault will
	 *   overwrite the contents of CR2 (replacing the previous
	 *   address). These updates to CR2 occur even if the page fault
	 *   results in a double fault or occurs during the delivery of a
	 *   double fault.
	 *
	 * The logic below has a small possibility of incorrectly diagnosing
	 * some errors as stack overflows.  For example, if the IDT or GDT
	 * gets corrupted such that #GP delivery fails due to a bad descriptor
	 * causing #GP and we hit this condition while CR2 coincidentally
	 * points to the stack guard page, we'll think we overflowed the
	 * stack.  Given that we're going to panic one way or another
	 * if this happens, this isn't necessarily worth fixing.
	 *
	 * If necessary, we could improve the test by only diagnosing
	 * a stack overflow if the saved RSP points within 47 bytes of
	 * the bottom of the stack: if RSP == tsk_stack + 48 and we
	 * take an exception, the stack is already aligned and there
	 * will be enough room SS, RSP, RFLAGS, CS, RIP, and a
	 * possible error code, so a stack overflow would *not* double
	 * fault.  With any less space left, exception delivery could
	 * fail, and, as a practical matter, we've overflowed the
	 * stack even if the actual trigger for the double fault was
	 * something else.
	 */
	if ((unsigned long)task_stack_page(tsk) - 1 - address < PAGE_SIZE) {
		handle_stack_overflow("kernel stack overflow (double-fault)",
				      regs, address);
	}
#endif

	pr_emerg("PANIC: double fault, error_code: 0x%lx\n", error_code);
	die("double fault", regs, error_code);
	panic("Machine halted.");
	instrumentation_end();
}

DEFINE_IDTENTRY(exc_bounds)
{
	if (notify_die(DIE_TRAP, "bounds", regs, 0,
			X86_TRAP_BR, SIGSEGV) == NOTIFY_STOP)
		return;
	cond_local_irq_enable(regs);

	if (!user_mode(regs))
		die("bounds", regs, 0);

	do_trap(X86_TRAP_BR, SIGSEGV, "bounds", regs, 0, 0, NULL);

	cond_local_irq_disable(regs);
}

enum kernel_gp_hint {
	GP_NO_HINT,
	GP_NON_CANONICAL,
	GP_CANONICAL
};

/*
 * When an uncaught #GP occurs, try to determine the memory address accessed by
 * the instruction and return that address to the caller. Also, try to figure
 * out whether any part of the access to that address was non-canonical.
 */
static enum kernel_gp_hint get_kernel_gp_address(struct pt_regs *regs,
						 unsigned long *addr)
{
	u8 insn_buf[MAX_INSN_SIZE];
	struct insn insn;

	if (copy_from_kernel_nofault(insn_buf, (void *)regs->ip,
			MAX_INSN_SIZE))
		return GP_NO_HINT;

	kernel_insn_init(&insn, insn_buf, MAX_INSN_SIZE);
	insn_get_modrm(&insn);
	insn_get_sib(&insn);

	*addr = (unsigned long)insn_get_addr_ref(&insn, regs);
	if (*addr == -1UL)
		return GP_NO_HINT;

#ifdef CONFIG_X86_64
	/*
	 * Check that:
	 *  - the operand is not in the kernel half
	 *  - the last byte of the operand is not in the user canonical half
	 */
	if (*addr < ~__VIRTUAL_MASK &&
	    *addr + insn.opnd_bytes - 1 > __VIRTUAL_MASK)
		return GP_NON_CANONICAL;
#endif

	return GP_CANONICAL;
}

#define GPFSTR "general protection fault"

static bool fixup_iopl_exception(struct pt_regs *regs)
{
	struct thread_struct *t = &current->thread;
	unsigned char byte;
	unsigned long ip;

	if (!IS_ENABLED(CONFIG_X86_IOPL_IOPERM) || t->iopl_emul != 3)
		return false;

	ip = insn_get_effective_ip(regs);
	if (!ip)
		return false;

	if (get_user(byte, (const char __user *)ip))
		return false;

	if (byte != 0xfa && byte != 0xfb)
		return false;

	if (!t->iopl_warn && printk_ratelimit()) {
		pr_err("%s[%d] attempts to use CLI/STI, pretending it's a NOP, ip:%lx",
		       current->comm, task_pid_nr(current), ip);
		print_vma_addr(KERN_CONT " in ", ip);
		pr_cont("\n");
		t->iopl_warn = 1;
	}

	regs->ip += 1;
	return true;
}

DEFINE_IDTENTRY_ERRORCODE(exc_general_protection)
{
	char desc[sizeof(GPFSTR) + 50 + 2*sizeof(unsigned long) + 1] = GPFSTR;
	enum kernel_gp_hint hint = GP_NO_HINT;
	struct task_struct *tsk;
	unsigned long gp_addr;
	int ret;

	cond_local_irq_enable(regs);

	if (static_cpu_has(X86_FEATURE_UMIP)) {
		if (user_mode(regs) && fixup_umip_exception(regs))
			goto exit;
	}

	if (v8086_mode(regs)) {
		local_irq_enable();
		handle_vm86_fault((struct kernel_vm86_regs *) regs, error_code);
		local_irq_disable();
		return;
	}

	tsk = current;

	if (user_mode(regs)) {
		if (fixup_iopl_exception(regs))
			goto exit;

		tsk->thread.error_code = error_code;
		tsk->thread.trap_nr = X86_TRAP_GP;

		show_signal(tsk, SIGSEGV, "", desc, regs, error_code);
		force_sig(SIGSEGV);
		goto exit;
	}

	if (fixup_exception(regs, X86_TRAP_GP, error_code, 0))
		goto exit;

	tsk->thread.error_code = error_code;
	tsk->thread.trap_nr = X86_TRAP_GP;

	/*
	 * To be potentially processing a kprobe fault and to trust the result
	 * from kprobe_running(), we have to be non-preemptible.
	 */
	if (!preemptible() &&
	    kprobe_running() &&
	    kprobe_fault_handler(regs, X86_TRAP_GP))
		goto exit;

	ret = notify_die(DIE_GPF, desc, regs, error_code, X86_TRAP_GP, SIGSEGV);
	if (ret == NOTIFY_STOP)
		goto exit;

	if (error_code)
		snprintf(desc, sizeof(desc), "segment-related " GPFSTR);
	else
		hint = get_kernel_gp_address(regs, &gp_addr);

	if (hint != GP_NO_HINT)
		snprintf(desc, sizeof(desc), GPFSTR ", %s 0x%lx",
			 (hint == GP_NON_CANONICAL) ? "probably for non-canonical address"
						    : "maybe for address",
			 gp_addr);

	/*
	 * KASAN is interested only in the non-canonical case, clear it
	 * otherwise.
	 */
	if (hint != GP_NON_CANONICAL)
		gp_addr = 0;

	die_addr(desc, regs, error_code, gp_addr);

exit:
	cond_local_irq_disable(regs);
}

static bool do_int3(struct pt_regs *regs)
{
	int res;

#ifdef CONFIG_KGDB_LOW_LEVEL_TRAP
	if (kgdb_ll_trap(DIE_INT3, "int3", regs, 0, X86_TRAP_BP,
			 SIGTRAP) == NOTIFY_STOP)
		return true;
#endif /* CONFIG_KGDB_LOW_LEVEL_TRAP */

#ifdef CONFIG_KPROBES
	if (kprobe_int3_handler(regs))
		return true;
#endif
	res = notify_die(DIE_INT3, "int3", regs, 0, X86_TRAP_BP, SIGTRAP);

	return res == NOTIFY_STOP;
}
NOKPROBE_SYMBOL(do_int3);

static void do_int3_user(struct pt_regs *regs)
{
	if (do_int3(regs))
		return;

	cond_local_irq_enable(regs);
	do_trap(X86_TRAP_BP, SIGTRAP, "int3", regs, 0, 0, NULL);
	cond_local_irq_disable(regs);
}

DEFINE_IDTENTRY_RAW(exc_int3)
{
	/*
	 * poke_int3_handler() is completely self contained code; it does (and
	 * must) *NOT* call out to anything, lest it hits upon yet another
	 * INT3.
	 */
	if (poke_int3_handler(regs))
		return;

	/*
	 * irqentry_enter_from_user_mode() uses static_branch_{,un}likely()
	 * and therefore can trigger INT3, hence poke_int3_handler() must
	 * be done before. If the entry came from kernel mode, then use
	 * nmi_enter() because the INT3 could have been hit in any context
	 * including NMI.
	 */
	if (user_mode(regs)) {
		irqentry_enter_from_user_mode(regs);
		instrumentation_begin();
		do_int3_user(regs);
		instrumentation_end();
		irqentry_exit_to_user_mode(regs);
	} else {
		irqentry_state_t irq_state = irqentry_nmi_enter(regs);

		instrumentation_begin();
		if (!do_int3(regs))
			die("int3", regs, 0);
		instrumentation_end();
		irqentry_nmi_exit(regs, irq_state);
	}
}

#ifdef CONFIG_X86_64
/*
 * Help handler running on a per-cpu (IST or entry trampoline) stack
 * to switch to the normal thread stack if the interrupted code was in
 * user mode. The actual stack switch is done in entry_64.S
 */
asmlinkage __visible noinstr struct pt_regs *sync_regs(struct pt_regs *eregs)
{
	struct pt_regs *regs = (struct pt_regs *)this_cpu_read(cpu_current_top_of_stack) - 1;
	if (regs != eregs)
		*regs = *eregs;
	return regs;
}

#ifdef CONFIG_AMD_MEM_ENCRYPT
asmlinkage __visible noinstr struct pt_regs *vc_switch_off_ist(struct pt_regs *regs)
{
	unsigned long sp, *stack;
	struct stack_info info;
	struct pt_regs *regs_ret;

	/*
	 * In the SYSCALL entry path the RSP value comes from user-space - don't
	 * trust it and switch to the current kernel stack
	 */
	if (ip_within_syscall_gap(regs)) {
		sp = this_cpu_read(cpu_current_top_of_stack);
		goto sync;
	}

	/*
	 * From here on the RSP value is trusted. Now check whether entry
	 * happened from a safe stack. Not safe are the entry or unknown stacks,
	 * use the fall-back stack instead in this case.
	 */
	sp    = regs->sp;
	stack = (unsigned long *)sp;

	if (!get_stack_info_noinstr(stack, current, &info) || info.type == STACK_TYPE_ENTRY ||
	    info.type > STACK_TYPE_EXCEPTION_LAST)
		sp = __this_cpu_ist_top_va(VC2);

sync:
	/*
	 * Found a safe stack - switch to it as if the entry didn't happen via
	 * IST stack. The code below only copies pt_regs, the real switch happens
	 * in assembly code.
	 */
	sp = ALIGN_DOWN(sp, 8) - sizeof(*regs_ret);

	regs_ret = (struct pt_regs *)sp;
	*regs_ret = *regs;

	return regs_ret;
}
#endif

struct bad_iret_stack {
	void *error_entry_ret;
	struct pt_regs regs;
};

asmlinkage __visible noinstr
struct bad_iret_stack *fixup_bad_iret(struct bad_iret_stack *s)
{
	/*
	 * This is called from entry_64.S early in handling a fault
	 * caused by a bad iret to user mode.  To handle the fault
	 * correctly, we want to move our stack frame to where it would
	 * be had we entered directly on the entry stack (rather than
	 * just below the IRET frame) and we want to pretend that the
	 * exception came from the IRET target.
	 */
	struct bad_iret_stack tmp, *new_stack =
		(struct bad_iret_stack *)__this_cpu_read(cpu_tss_rw.x86_tss.sp0) - 1;

	/* Copy the IRET target to the temporary storage. */
	__memcpy(&tmp.regs.ip, (void *)s->regs.sp, 5*8);

	/* Copy the remainder of the stack from the current stack. */
	__memcpy(&tmp, s, offsetof(struct bad_iret_stack, regs.ip));

	/* Update the entry stack */
	__memcpy(new_stack, &tmp, sizeof(tmp));

	BUG_ON(!user_mode(&new_stack->regs));
	return new_stack;
}
#endif

static bool is_sysenter_singlestep(struct pt_regs *regs)
{
	/*
	 * We don't try for precision here.  If we're anywhere in the region of
	 * code that can be single-stepped in the SYSENTER entry path, then
	 * assume that this is a useless single-step trap due to SYSENTER
	 * being invoked with TF set.  (We don't know in advance exactly
	 * which instructions will be hit because BTF could plausibly
	 * be set.)
	 */
#ifdef CONFIG_X86_32
	return (regs->ip - (unsigned long)__begin_SYSENTER_singlestep_region) <
		(unsigned long)__end_SYSENTER_singlestep_region -
		(unsigned long)__begin_SYSENTER_singlestep_region;
#elif defined(CONFIG_IA32_EMULATION)
	return (regs->ip - (unsigned long)entry_SYSENTER_compat) <
		(unsigned long)__end_entry_SYSENTER_compat -
		(unsigned long)entry_SYSENTER_compat;
#else
	return false;
#endif
}

static __always_inline unsigned long debug_read_clear_dr6(void)
{
	unsigned long dr6;

	/*
	 * The Intel SDM says:
	 *
	 *   Certain debug exceptions may clear bits 0-3. The remaining
	 *   contents of the DR6 register are never cleared by the
	 *   processor. To avoid confusion in identifying debug
	 *   exceptions, debug handlers should clear the register before
	 *   returning to the interrupted task.
	 *
	 * Keep it simple: clear DR6 immediately.
	 */
	get_debugreg(dr6, 6);
	set_debugreg(DR6_RESERVED, 6);
	dr6 ^= DR6_RESERVED; /* Flip to positive polarity */

	return dr6;
}

/*
 * Our handling of the processor debug registers is non-trivial.
 * We do not clear them on entry and exit from the kernel. Therefore
 * it is possible to get a watchpoint trap here from inside the kernel.
 * However, the code in ./ptrace.c has ensured that the user can
 * only set watchpoints on userspace addresses. Therefore the in-kernel
 * watchpoint trap can only occur in code which is reading/writing
 * from user space. Such code must not hold kernel locks (since it
 * can equally take a page fault), therefore it is safe to call
 * force_sig_info even though that claims and releases locks.
 *
 * Code in ./signal.c ensures that the debug control register
 * is restored before we deliver any signal, and therefore that
 * user code runs with the correct debug control register even though
 * we clear it here.
 *
 * Being careful here means that we don't have to be as careful in a
 * lot of more complicated places (task switching can be a bit lazy
 * about restoring all the debug state, and ptrace doesn't have to
 * find every occurrence of the TF bit that could be saved away even
 * by user code)
 *
 * May run on IST stack.
 */

static bool notify_debug(struct pt_regs *regs, unsigned long *dr6)
{
	/*
	 * Notifiers will clear bits in @dr6 to indicate the event has been
	 * consumed - hw_breakpoint_handler(), single_stop_cont().
	 *
	 * Notifiers will set bits in @virtual_dr6 to indicate the desire
	 * for signals - ptrace_triggered(), kgdb_hw_overflow_handler().
	 */
	if (notify_die(DIE_DEBUG, "debug", regs, (long)dr6, 0, SIGTRAP) == NOTIFY_STOP)
		return true;

	return false;
}

static __always_inline void exc_debug_kernel(struct pt_regs *regs,
					     unsigned long dr6)
{
	/*
	 * Disable breakpoints during exception handling; recursive exceptions
	 * are exceedingly 'fun'.
	 *
	 * Since this function is NOKPROBE, and that also applies to
	 * HW_BREAKPOINT_X, we can't hit a breakpoint before this (XXX except a
	 * HW_BREAKPOINT_W on our stack)
	 *
	 * Entry text is excluded for HW_BP_X and cpu_entry_area, which
	 * includes the entry stack is excluded for everything.
	 */
	unsigned long dr7 = local_db_save();
	irqentry_state_t irq_state = irqentry_nmi_enter(regs);
	instrumentation_begin();

	/*
	 * If something gets miswired and we end up here for a user mode
	 * #DB, we will malfunction.
	 */
	WARN_ON_ONCE(user_mode(regs));

	if (test_thread_flag(TIF_BLOCKSTEP)) {
		/*
		 * The SDM says "The processor clears the BTF flag when it
		 * generates a debug exception." but PTRACE_BLOCKSTEP requested
		 * it for userspace, but we just took a kernel #DB, so re-set
		 * BTF.
		 */
		unsigned long debugctl;

		rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
		debugctl |= DEBUGCTLMSR_BTF;
		wrmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
	}

	/*
	 * Catch SYSENTER with TF set and clear DR_STEP. If this hit a
	 * watchpoint at the same time then that will still be handled.
	 */
	if ((dr6 & DR_STEP) && is_sysenter_singlestep(regs))
		dr6 &= ~DR_STEP;

	/*
	 * The kernel doesn't use INT1
	 */
	if (!dr6)
		goto out;

	if (notify_debug(regs, &dr6))
		goto out;

	/*
	 * The kernel doesn't use TF single-step outside of:
	 *
	 *  - Kprobes, consumed through kprobe_debug_handler()
	 *  - KGDB, consumed through notify_debug()
	 *
	 * So if we get here with DR_STEP set, something is wonky.
	 *
	 * A known way to trigger this is through QEMU's GDB stub,
	 * which leaks #DB into the guest and causes IST recursion.
	 */
	if (WARN_ON_ONCE(dr6 & DR_STEP))
		regs->flags &= ~X86_EFLAGS_TF;
out:
	instrumentation_end();
	irqentry_nmi_exit(regs, irq_state);

	local_db_restore(dr7);
}

static __always_inline void exc_debug_user(struct pt_regs *regs,
					   unsigned long dr6)
{
	bool icebp;

	/*
	 * If something gets miswired and we end up here for a kernel mode
	 * #DB, we will malfunction.
	 */
	WARN_ON_ONCE(!user_mode(regs));

	/*
	 * NB: We can't easily clear DR7 here because
	 * irqentry_exit_to_usermode() can invoke ptrace, schedule, access
	 * user memory, etc.  This means that a recursive #DB is possible.  If
	 * this happens, that #DB will hit exc_debug_kernel() and clear DR7.
	 * Since we're not on the IST stack right now, everything will be
	 * fine.
	 */

	irqentry_enter_from_user_mode(regs);
	instrumentation_begin();

	/*
	 * Start the virtual/ptrace DR6 value with just the DR_STEP mask
	 * of the real DR6. ptrace_triggered() will set the DR_TRAPn bits.
	 *
	 * Userspace expects DR_STEP to be visible in ptrace_get_debugreg(6)
	 * even if it is not the result of PTRACE_SINGLESTEP.
	 */
	current->thread.virtual_dr6 = (dr6 & DR_STEP);

	/*
	 * The SDM says "The processor clears the BTF flag when it
	 * generates a debug exception."  Clear TIF_BLOCKSTEP to keep
	 * TIF_BLOCKSTEP in sync with the hardware BTF flag.
	 */
	clear_thread_flag(TIF_BLOCKSTEP);

	/*
	 * If dr6 has no reason to give us about the origin of this trap,
	 * then it's very likely the result of an icebp/int01 trap.
	 * User wants a sigtrap for that.
	 */
	icebp = !dr6;

	if (notify_debug(regs, &dr6))
		goto out;

	/* It's safe to allow irq's after DR6 has been saved */
	local_irq_enable();

	if (v8086_mode(regs)) {
		handle_vm86_trap((struct kernel_vm86_regs *)regs, 0, X86_TRAP_DB);
		goto out_irq;
	}

	/* Add the virtual_dr6 bits for signals. */
	dr6 |= current->thread.virtual_dr6;
	if (dr6 & (DR_STEP | DR_TRAP_BITS) || icebp)
		send_sigtrap(regs, 0, get_si_code(dr6));

out_irq:
	local_irq_disable();
out:
	instrumentation_end();
	irqentry_exit_to_user_mode(regs);
}

#ifdef CONFIG_X86_64
/* IST stack entry */
DEFINE_IDTENTRY_DEBUG(exc_debug)
{
	exc_debug_kernel(regs, debug_read_clear_dr6());
}

/* User entry, runs on regular task stack */
DEFINE_IDTENTRY_DEBUG_USER(exc_debug)
{
	exc_debug_user(regs, debug_read_clear_dr6());
}
#else
/* 32 bit does not have separate entry points. */
DEFINE_IDTENTRY_RAW(exc_debug)
{
	unsigned long dr6 = debug_read_clear_dr6();

	if (user_mode(regs))
		exc_debug_user(regs, dr6);
	else
		exc_debug_kernel(regs, dr6);
}
#endif

/*
 * Note that we play around with the 'TS' bit in an attempt to get
 * the correct behaviour even in the presence of the asynchronous
 * IRQ13 behaviour
 */
static void math_error(struct pt_regs *regs, int trapnr)
{
	struct task_struct *task = current;
	struct fpu *fpu = &task->thread.fpu;
	int si_code;
	char *str = (trapnr == X86_TRAP_MF) ? "fpu exception" :
						"simd exception";

	cond_local_irq_enable(regs);

	if (!user_mode(regs)) {
		if (fixup_exception(regs, trapnr, 0, 0))
			goto exit;

		task->thread.error_code = 0;
		task->thread.trap_nr = trapnr;

		if (notify_die(DIE_TRAP, str, regs, 0, trapnr,
			       SIGFPE) != NOTIFY_STOP)
			die(str, regs, 0);
		goto exit;
	}

	/*
	 * Save the info for the exception handler and clear the error.
	 */
	fpu__save(fpu);

	task->thread.trap_nr	= trapnr;
	task->thread.error_code = 0;

	si_code = fpu__exception_code(fpu, trapnr);
	/* Retry when we get spurious exceptions: */
	if (!si_code)
		goto exit;

	force_sig_fault(SIGFPE, si_code,
			(void __user *)uprobe_get_trap_addr(regs));
exit:
	cond_local_irq_disable(regs);
}

DEFINE_IDTENTRY(exc_coprocessor_error)
{
	math_error(regs, X86_TRAP_MF);
}

DEFINE_IDTENTRY(exc_simd_coprocessor_error)
{
	if (IS_ENABLED(CONFIG_X86_INVD_BUG)) {
		/* AMD 486 bug: INVD in CPL 0 raises #XF instead of #GP */
		if (!static_cpu_has(X86_FEATURE_XMM)) {
			__exc_general_protection(regs, 0);
			return;
		}
	}
	math_error(regs, X86_TRAP_XF);
}

DEFINE_IDTENTRY(exc_spurious_interrupt_bug)
{
	/*
	 * This addresses a Pentium Pro Erratum:
	 *
	 * PROBLEM: If the APIC subsystem is configured in mixed mode with
	 * Virtual Wire mode implemented through the local APIC, an
	 * interrupt vector of 0Fh (Intel reserved encoding) may be
	 * generated by the local APIC (Int 15).  This vector may be
	 * generated upon receipt of a spurious interrupt (an interrupt
	 * which is removed before the system receives the INTA sequence)
	 * instead of the programmed 8259 spurious interrupt vector.
	 *
	 * IMPLICATION: The spurious interrupt vector programmed in the
	 * 8259 is normally handled by an operating system's spurious
	 * interrupt handler. However, a vector of 0Fh is unknown to some
	 * operating systems, which would crash if this erratum occurred.
	 *
	 * In theory this could be limited to 32bit, but the handler is not
	 * hurting and who knows which other CPUs suffer from this.
	 */
}

DEFINE_IDTENTRY(exc_device_not_available)
{
	unsigned long cr0 = read_cr0();

#ifdef CONFIG_MATH_EMULATION
	if (!boot_cpu_has(X86_FEATURE_FPU) && (cr0 & X86_CR0_EM)) {
		struct math_emu_info info = { };

		cond_local_irq_enable(regs);

		info.regs = regs;
		math_emulate(&info);

		cond_local_irq_disable(regs);
		return;
	}
#endif

	/* This should not happen. */
	if (WARN(cr0 & X86_CR0_TS, "CR0.TS was set")) {
		/* Try to fix it up and carry on. */
		write_cr0(cr0 & ~X86_CR0_TS);
	} else {
		/*
		 * Something terrible happened, and we're better off trying
		 * to kill the task than getting stuck in a never-ending
		 * loop of #NM faults.
		 */
		die("unexpected #NM exception", regs, 0);
	}
}

#ifdef CONFIG_X86_32
DEFINE_IDTENTRY_SW(iret_error)
{
	local_irq_enable();
	if (notify_die(DIE_TRAP, "iret exception", regs, 0,
			X86_TRAP_IRET, SIGILL) != NOTIFY_STOP) {
		do_trap(X86_TRAP_IRET, SIGILL, "iret exception", regs, 0,
			ILL_BADSTK, (void __user *)NULL);
	}
	local_irq_disable();
}
#endif

void __init trap_init(void)
{
	/* Init cpu_entry_area before IST entries are set up */
	setup_cpu_entry_areas();

	/* Init GHCB memory pages when running as an SEV-ES guest */
	sev_es_init_vc_handling();

	idt_setup_traps();

	cpu_init_exception_handling();
	cpu_init();

	idt_setup_ist_traps();
}
