// SPDX-License-Identifier: GPL-2.0-only
#include <linux/extable.h>
#include <linux/uaccess.h>
#include <linux/sched/debug.h>
#include <xen/xen.h>

#include <asm/fpu/internal.h>
#include <asm/sev-es.h>
#include <asm/traps.h>
#include <asm/kdebug.h>

typedef bool (*ex_handler_t)(const struct exception_table_entry *,
			    struct pt_regs *, int, unsigned long,
			    unsigned long);

static inline unsigned long
ex_fixup_addr(const struct exception_table_entry *x)
{
	return (unsigned long)&x->fixup + x->fixup;
}
static inline ex_handler_t
ex_fixup_handler(const struct exception_table_entry *x)
{
	return (ex_handler_t)((unsigned long)&x->handler + x->handler);
}

__visible bool ex_handler_default(const struct exception_table_entry *fixup,
				  struct pt_regs *regs, int trapnr,
				  unsigned long error_code,
				  unsigned long fault_addr)
{
	regs->ip = ex_fixup_addr(fixup);
	return true;
}
EXPORT_SYMBOL(ex_handler_default);

__visible bool ex_handler_fault(const struct exception_table_entry *fixup,
				struct pt_regs *regs, int trapnr,
				unsigned long error_code,
				unsigned long fault_addr)
{
	regs->ip = ex_fixup_addr(fixup);
	regs->ax = trapnr;
	return true;
}
EXPORT_SYMBOL_GPL(ex_handler_fault);

/*
 * Handler for when we fail to restore a task's FPU state.  We should never get
 * here because the FPU state of a task using the FPU (task->thread.fpu.state)
 * should always be valid.  However, past bugs have allowed userspace to set
 * reserved bits in the XSAVE area using PTRACE_SETREGSET or sys_rt_sigreturn().
 * These caused XRSTOR to fail when switching to the task, leaking the FPU
 * registers of the task previously executing on the CPU.  Mitigate this class
 * of vulnerability by restoring from the initial state (essentially, zeroing
 * out all the FPU registers) if we can't restore from the task's FPU state.
 */
__visible bool ex_handler_fprestore(const struct exception_table_entry *fixup,
				    struct pt_regs *regs, int trapnr,
				    unsigned long error_code,
				    unsigned long fault_addr)
{
	regs->ip = ex_fixup_addr(fixup);

	WARN_ONCE(1, "Bad FPU state detected at %pB, reinitializing FPU registers.",
		  (void *)instruction_pointer(regs));

	__copy_kernel_to_fpregs(&init_fpstate, -1);
	return true;
}
EXPORT_SYMBOL_GPL(ex_handler_fprestore);

__visible bool ex_handler_uaccess(const struct exception_table_entry *fixup,
				  struct pt_regs *regs, int trapnr,
				  unsigned long error_code,
				  unsigned long fault_addr)
{
	WARN_ONCE(trapnr == X86_TRAP_GP, "General protection fault in user access. Non-canonical address?");
	regs->ip = ex_fixup_addr(fixup);
	return true;
}
EXPORT_SYMBOL(ex_handler_uaccess);

__visible bool ex_handler_copy(const struct exception_table_entry *fixup,
			       struct pt_regs *regs, int trapnr,
			       unsigned long error_code,
			       unsigned long fault_addr)
{
	WARN_ONCE(trapnr == X86_TRAP_GP, "General protection fault in user access. Non-canonical address?");
	regs->ip = ex_fixup_addr(fixup);
	regs->ax = trapnr;
	return true;
}
EXPORT_SYMBOL(ex_handler_copy);

__visible bool ex_handler_rdmsr_unsafe(const struct exception_table_entry *fixup,
				       struct pt_regs *regs, int trapnr,
				       unsigned long error_code,
				       unsigned long fault_addr)
{
	if (pr_warn_once("unchecked MSR access error: RDMSR from 0x%x at rIP: 0x%lx (%pS)\n",
			 (unsigned int)regs->cx, regs->ip, (void *)regs->ip))
		show_stack_regs(regs);

	/* Pretend that the read succeeded and returned 0. */
	regs->ip = ex_fixup_addr(fixup);
	regs->ax = 0;
	regs->dx = 0;
	return true;
}
EXPORT_SYMBOL(ex_handler_rdmsr_unsafe);

__visible bool ex_handler_wrmsr_unsafe(const struct exception_table_entry *fixup,
				       struct pt_regs *regs, int trapnr,
				       unsigned long error_code,
				       unsigned long fault_addr)
{
	if (pr_warn_once("unchecked MSR access error: WRMSR to 0x%x (tried to write 0x%08x%08x) at rIP: 0x%lx (%pS)\n",
			 (unsigned int)regs->cx, (unsigned int)regs->dx,
			 (unsigned int)regs->ax,  regs->ip, (void *)regs->ip))
		show_stack_regs(regs);

	/* Pretend that the write succeeded. */
	regs->ip = ex_fixup_addr(fixup);
	return true;
}
EXPORT_SYMBOL(ex_handler_wrmsr_unsafe);

__visible bool ex_handler_clear_fs(const struct exception_table_entry *fixup,
				   struct pt_regs *regs, int trapnr,
				   unsigned long error_code,
				   unsigned long fault_addr)
{
	if (static_cpu_has(X86_BUG_NULL_SEG))
		asm volatile ("mov %0, %%fs" : : "rm" (__USER_DS));
	asm volatile ("mov %0, %%fs" : : "rm" (0));
	return ex_handler_default(fixup, regs, trapnr, error_code, fault_addr);
}
EXPORT_SYMBOL(ex_handler_clear_fs);

enum handler_type ex_get_fault_handler_type(unsigned long ip)
{
	const struct exception_table_entry *e;
	ex_handler_t handler;

	e = search_exception_tables(ip);
	if (!e)
		return EX_HANDLER_NONE;
	handler = ex_fixup_handler(e);
	if (handler == ex_handler_fault)
		return EX_HANDLER_FAULT;
	else if (handler == ex_handler_uaccess || handler == ex_handler_copy)
		return EX_HANDLER_UACCESS;
	else
		return EX_HANDLER_OTHER;
}

__nocfi
int fixup_exception(struct pt_regs *regs, int trapnr, unsigned long error_code,
		    unsigned long fault_addr)
{
	const struct exception_table_entry *e;
	ex_handler_t handler;

#ifdef CONFIG_PNPBIOS
	if (unlikely(SEGMENT_IS_PNP_CODE(regs->cs))) {
		extern u32 pnp_bios_fault_eip, pnp_bios_fault_esp;
		extern u32 pnp_bios_is_utter_crap;
		pnp_bios_is_utter_crap = 1;
		printk(KERN_CRIT "PNPBIOS fault.. attempting recovery.\n");
		__asm__ volatile(
			"movl %0, %%esp\n\t"
			"jmp *%1\n\t"
			: : "g" (pnp_bios_fault_esp), "g" (pnp_bios_fault_eip));
		panic("do_trap: can't hit this");
	}
#endif

	e = search_exception_tables(regs->ip);
	if (!e)
		return 0;

	handler = ex_fixup_handler(e);
	return handler(e, regs, trapnr, error_code, fault_addr);
}

extern unsigned int early_recursion_flag;

/* Restricted version used during very early boot */
void __init early_fixup_exception(struct pt_regs *regs, int trapnr)
{
	/* Ignore early NMIs. */
	if (trapnr == X86_TRAP_NMI)
		return;

	if (early_recursion_flag > 2)
		goto halt_loop;

	/*
	 * Old CPUs leave the high bits of CS on the stack
	 * undefined.  I'm not sure which CPUs do this, but at least
	 * the 486 DX works this way.
	 * Xen pv domains are not using the default __KERNEL_CS.
	 */
	if (!xen_pv_domain() && regs->cs != __KERNEL_CS)
		goto fail;

	/*
	 * The full exception fixup machinery is available as soon as
	 * the early IDT is loaded.  This means that it is the
	 * responsibility of extable users to either function correctly
	 * when handlers are invoked early or to simply avoid causing
	 * exceptions before they're ready to handle them.
	 *
	 * This is better than filtering which handlers can be used,
	 * because refusing to call a handler here is guaranteed to
	 * result in a hard-to-debug panic.
	 *
	 * Keep in mind that not all vectors actually get here.  Early
	 * page faults, for example, are special.
	 */
	if (fixup_exception(regs, trapnr, regs->orig_ax, 0))
		return;

	if (trapnr == X86_TRAP_UD) {
		if (report_bug(regs->ip, regs) == BUG_TRAP_TYPE_WARN) {
			/* Skip the ud2. */
			regs->ip += LEN_UD2;
			return;
		}

		/*
		 * If this was a BUG and report_bug returns or if this
		 * was just a normal #UD, we want to continue onward and
		 * crash.
		 */
	}

fail:
	early_printk("PANIC: early exception 0x%02x IP %lx:%lx error %lx cr2 0x%lx\n",
		     (unsigned)trapnr, (unsigned long)regs->cs, regs->ip,
		     regs->orig_ax, read_cr2());

	show_regs(regs);

halt_loop:
	while (true)
		halt();
}
