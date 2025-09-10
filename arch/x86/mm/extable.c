// SPDX-License-Identifier: GPL-2.0-only
#include <linux/extable.h>
#include <linux/uaccess.h>
#include <linux/sched/debug.h>
#include <linux/bitfield.h>
#include <xen/xen.h>

#include <asm/fpu/api.h>
#include <asm/fred.h>
#include <asm/sev.h>
#include <asm/traps.h>
#include <asm/kdebug.h>
#include <asm/insn-eval.h>
#include <asm/sgx.h>

static inline unsigned long *pt_regs_nr(struct pt_regs *regs, int nr)
{
	int reg_offset = pt_regs_offset(regs, nr);
	static unsigned long __dummy;

	if (WARN_ON_ONCE(reg_offset < 0))
		return &__dummy;

	return (unsigned long *)((unsigned long)regs + reg_offset);
}

static inline unsigned long
ex_fixup_addr(const struct exception_table_entry *x)
{
	return (unsigned long)&x->fixup + x->fixup;
}

static bool ex_handler_default(const struct exception_table_entry *e,
			       struct pt_regs *regs)
{
	if (e->data & EX_FLAG_CLEAR_AX)
		regs->ax = 0;
	if (e->data & EX_FLAG_CLEAR_DX)
		regs->dx = 0;

	regs->ip = ex_fixup_addr(e);
	return true;
}

/*
 * This is the *very* rare case where we do a "load_unaligned_zeropad()"
 * and it's a page crosser into a non-existent page.
 *
 * This happens when we optimistically load a pathname a word-at-a-time
 * and the name is less than the full word and the  next page is not
 * mapped. Typically that only happens for CONFIG_DEBUG_PAGEALLOC.
 *
 * NOTE! The faulting address is always a 'mov mem,reg' type instruction
 * of size 'long', and the exception fixup must always point to right
 * after the instruction.
 */
static bool ex_handler_zeropad(const struct exception_table_entry *e,
			       struct pt_regs *regs,
			       unsigned long fault_addr)
{
	struct insn insn;
	const unsigned long mask = sizeof(long) - 1;
	unsigned long offset, addr, next_ip, len;
	unsigned long *reg;

	next_ip = ex_fixup_addr(e);
	len = next_ip - regs->ip;
	if (len > MAX_INSN_SIZE)
		return false;

	if (insn_decode(&insn, (void *) regs->ip, len, INSN_MODE_KERN))
		return false;
	if (insn.length != len)
		return false;

	if (insn.opcode.bytes[0] != 0x8b)
		return false;
	if (insn.opnd_bytes != sizeof(long))
		return false;

	addr = (unsigned long) insn_get_addr_ref(&insn, regs);
	if (addr == ~0ul)
		return false;

	offset = addr & mask;
	addr = addr & ~mask;
	if (fault_addr != addr + sizeof(long))
		return false;

	reg = insn_get_modrm_reg_ptr(&insn, regs);
	if (!reg)
		return false;

	*reg = *(unsigned long *)addr >> (offset * 8);
	return ex_handler_default(e, regs);
}

static bool ex_handler_fault(const struct exception_table_entry *fixup,
			     struct pt_regs *regs, int trapnr)
{
	regs->ax = trapnr;
	return ex_handler_default(fixup, regs);
}

static bool ex_handler_sgx(const struct exception_table_entry *fixup,
			   struct pt_regs *regs, int trapnr)
{
	regs->ax = trapnr | SGX_ENCLS_FAULT_FLAG;
	return ex_handler_default(fixup, regs);
}

/*
 * Handler for when we fail to restore a task's FPU state.  We should never get
 * here because the FPU state of a task using the FPU (struct fpu::fpstate)
 * should always be valid.  However, past bugs have allowed userspace to set
 * reserved bits in the XSAVE area using PTRACE_SETREGSET or sys_rt_sigreturn().
 * These caused XRSTOR to fail when switching to the task, leaking the FPU
 * registers of the task previously executing on the CPU.  Mitigate this class
 * of vulnerability by restoring from the initial state (essentially, zeroing
 * out all the FPU registers) if we can't restore from the task's FPU state.
 */
static bool ex_handler_fprestore(const struct exception_table_entry *fixup,
				 struct pt_regs *regs)
{
	WARN_ONCE(1, "Bad FPU state detected at %pB, reinitializing FPU registers.",
		  (void *)instruction_pointer(regs));

	fpu_reset_from_exception_fixup();

	return ex_handler_default(fixup, regs);
}

/*
 * On x86-64, we end up being imprecise with 'access_ok()', and allow
 * non-canonical user addresses to make the range comparisons simpler,
 * and to not have to worry about LAM being enabled.
 *
 * In fact, we allow up to one page of "slop" at the sign boundary,
 * which means that we can do access_ok() by just checking the sign
 * of the pointer for the common case of having a small access size.
 */
static bool gp_fault_address_ok(unsigned long fault_address)
{
#ifdef CONFIG_X86_64
	/* Is it in the "user space" part of the non-canonical space? */
	if (valid_user_address(fault_address))
		return true;

	/* .. or just above it? */
	fault_address -= PAGE_SIZE;
	if (valid_user_address(fault_address))
		return true;
#endif
	return false;
}

static bool ex_handler_uaccess(const struct exception_table_entry *fixup,
			       struct pt_regs *regs, int trapnr,
			       unsigned long fault_address)
{
	WARN_ONCE(trapnr == X86_TRAP_GP && !gp_fault_address_ok(fault_address),
		"General protection fault in user access. Non-canonical address?");
	return ex_handler_default(fixup, regs);
}

static bool ex_handler_msr(const struct exception_table_entry *fixup,
			   struct pt_regs *regs, bool wrmsr, bool safe, int reg)
{
	if (__ONCE_LITE_IF(!safe && wrmsr)) {
		pr_warn("unchecked MSR access error: WRMSR to 0x%x (tried to write 0x%08x%08x) at rIP: 0x%lx (%pS)\n",
			(unsigned int)regs->cx, (unsigned int)regs->dx,
			(unsigned int)regs->ax,  regs->ip, (void *)regs->ip);
		show_stack_regs(regs);
	}

	if (__ONCE_LITE_IF(!safe && !wrmsr)) {
		pr_warn("unchecked MSR access error: RDMSR from 0x%x at rIP: 0x%lx (%pS)\n",
			(unsigned int)regs->cx, regs->ip, (void *)regs->ip);
		show_stack_regs(regs);
	}

	if (!wrmsr) {
		/* Pretend that the read succeeded and returned 0. */
		regs->ax = 0;
		regs->dx = 0;
	}

	if (safe)
		*pt_regs_nr(regs, reg) = -EIO;

	return ex_handler_default(fixup, regs);
}

static bool ex_handler_clear_fs(const struct exception_table_entry *fixup,
				struct pt_regs *regs)
{
	if (static_cpu_has(X86_BUG_NULL_SEG))
		asm volatile ("mov %0, %%fs" : : "rm" (__USER_DS));
	asm volatile ("mov %0, %%fs" : : "rm" (0));
	return ex_handler_default(fixup, regs);
}

static bool ex_handler_imm_reg(const struct exception_table_entry *fixup,
			       struct pt_regs *regs, int reg, int imm)
{
	*pt_regs_nr(regs, reg) = (long)imm;
	return ex_handler_default(fixup, regs);
}

static bool ex_handler_ucopy_len(const struct exception_table_entry *fixup,
				  struct pt_regs *regs, int trapnr,
				  unsigned long fault_address,
				  int reg, int imm)
{
	regs->cx = imm * regs->cx + *pt_regs_nr(regs, reg);
	return ex_handler_uaccess(fixup, regs, trapnr, fault_address);
}

#ifdef CONFIG_X86_FRED
static bool ex_handler_eretu(const struct exception_table_entry *fixup,
			     struct pt_regs *regs, unsigned long error_code)
{
	struct pt_regs *uregs = (struct pt_regs *)(regs->sp - offsetof(struct pt_regs, orig_ax));
	unsigned short ss = uregs->ss;
	unsigned short cs = uregs->cs;

	/*
	 * Move the NMI bit from the invalid stack frame, which caused ERETU
	 * to fault, to the fault handler's stack frame, thus to unblock NMI
	 * with the fault handler's ERETS instruction ASAP if NMI is blocked.
	 */
	regs->fred_ss.nmi = uregs->fred_ss.nmi;

	/*
	 * Sync event information to uregs, i.e., the ERETU return frame, but
	 * is it safe to write to the ERETU return frame which is just above
	 * current event stack frame?
	 *
	 * The RSP used by FRED to push a stack frame is not the value in %rsp,
	 * it is calculated from %rsp with the following 2 steps:
	 * 1) RSP = %rsp - (IA32_FRED_CONFIG & 0x1c0)	// Reserve N*64 bytes
	 * 2) RSP = RSP & ~0x3f		// Align to a 64-byte cache line
	 * when an event delivery doesn't trigger a stack level change.
	 *
	 * Here is an example with N*64 (N=1) bytes reserved:
	 *
	 *  64-byte cache line ==>  ______________
	 *                         |___Reserved___|
	 *                         |__Event_data__|
	 *                         |_____SS_______|
	 *                         |_____RSP______|
	 *                         |_____FLAGS____|
	 *                         |_____CS_______|
	 *                         |_____IP_______|
	 *  64-byte cache line ==> |__Error_code__| <== ERETU return frame
	 *                         |______________|
	 *                         |______________|
	 *                         |______________|
	 *                         |______________|
	 *                         |______________|
	 *                         |______________|
	 *                         |______________|
	 *  64-byte cache line ==> |______________| <== RSP after step 1) and 2)
	 *                         |___Reserved___|
	 *                         |__Event_data__|
	 *                         |_____SS_______|
	 *                         |_____RSP______|
	 *                         |_____FLAGS____|
	 *                         |_____CS_______|
	 *                         |_____IP_______|
	 *  64-byte cache line ==> |__Error_code__| <== ERETS return frame
	 *
	 * Thus a new FRED stack frame will always be pushed below a previous
	 * FRED stack frame ((N*64) bytes may be reserved between), and it is
	 * safe to write to a previous FRED stack frame as they never overlap.
	 */
	fred_info(uregs)->edata = fred_event_data(regs);
	uregs->ssx = regs->ssx;
	uregs->fred_ss.ss = ss;
	/* The NMI bit was moved away above */
	uregs->fred_ss.nmi = 0;
	uregs->csx = regs->csx;
	uregs->fred_cs.sl = 0;
	uregs->fred_cs.wfe = 0;
	uregs->cs = cs;
	uregs->orig_ax = error_code;

	return ex_handler_default(fixup, regs);
}
#endif

int ex_get_fixup_type(unsigned long ip)
{
	const struct exception_table_entry *e = search_exception_tables(ip);

	return e ? FIELD_GET(EX_DATA_TYPE_MASK, e->data) : EX_TYPE_NONE;
}

int fixup_exception(struct pt_regs *regs, int trapnr, unsigned long error_code,
		    unsigned long fault_addr)
{
	const struct exception_table_entry *e;
	int type, reg, imm;

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

	type = FIELD_GET(EX_DATA_TYPE_MASK, e->data);
	reg  = FIELD_GET(EX_DATA_REG_MASK,  e->data);
	imm  = FIELD_GET(EX_DATA_IMM_MASK,  e->data);

	switch (type) {
	case EX_TYPE_DEFAULT:
	case EX_TYPE_DEFAULT_MCE_SAFE:
		return ex_handler_default(e, regs);
	case EX_TYPE_FAULT:
	case EX_TYPE_FAULT_MCE_SAFE:
		return ex_handler_fault(e, regs, trapnr);
	case EX_TYPE_UACCESS:
		return ex_handler_uaccess(e, regs, trapnr, fault_addr);
	case EX_TYPE_CLEAR_FS:
		return ex_handler_clear_fs(e, regs);
	case EX_TYPE_FPU_RESTORE:
		return ex_handler_fprestore(e, regs);
	case EX_TYPE_BPF:
		return ex_handler_bpf(e, regs);
	case EX_TYPE_WRMSR:
		return ex_handler_msr(e, regs, true, false, reg);
	case EX_TYPE_RDMSR:
		return ex_handler_msr(e, regs, false, false, reg);
	case EX_TYPE_WRMSR_SAFE:
		return ex_handler_msr(e, regs, true, true, reg);
	case EX_TYPE_RDMSR_SAFE:
		return ex_handler_msr(e, regs, false, true, reg);
	case EX_TYPE_WRMSR_IN_MCE:
		ex_handler_msr_mce(regs, true);
		break;
	case EX_TYPE_RDMSR_IN_MCE:
		ex_handler_msr_mce(regs, false);
		break;
	case EX_TYPE_POP_REG:
		regs->sp += sizeof(long);
		fallthrough;
	case EX_TYPE_IMM_REG:
		return ex_handler_imm_reg(e, regs, reg, imm);
	case EX_TYPE_FAULT_SGX:
		return ex_handler_sgx(e, regs, trapnr);
	case EX_TYPE_UCOPY_LEN:
		return ex_handler_ucopy_len(e, regs, trapnr, fault_addr, reg, imm);
	case EX_TYPE_ZEROPAD:
		return ex_handler_zeropad(e, regs, fault_addr);
#ifdef CONFIG_X86_FRED
	case EX_TYPE_ERETU:
		return ex_handler_eretu(e, regs, error_code);
#endif
	}
	BUG();
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
