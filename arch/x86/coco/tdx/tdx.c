// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021-2022 Intel Corporation */

#undef pr_fmt
#define pr_fmt(fmt)     "tdx: " fmt

#include <linux/cpufeature.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/kexec.h>
#include <asm/coco.h>
#include <asm/tdx.h>
#include <asm/vmx.h>
#include <asm/ia32.h>
#include <asm/insn.h>
#include <asm/insn-eval.h>
#include <asm/pgtable.h>
#include <asm/set_memory.h>
#include <asm/traps.h>

/* MMIO direction */
#define EPT_READ	0
#define EPT_WRITE	1

/* Port I/O direction */
#define PORT_READ	0
#define PORT_WRITE	1

/* See Exit Qualification for I/O Instructions in VMX documentation */
#define VE_IS_IO_IN(e)		((e) & BIT(3))
#define VE_GET_IO_SIZE(e)	(((e) & GENMASK(2, 0)) + 1)
#define VE_GET_PORT_NUM(e)	((e) >> 16)
#define VE_IS_IO_STRING(e)	((e) & BIT(4))

#define ATTR_DEBUG		BIT(0)
#define ATTR_SEPT_VE_DISABLE	BIT(28)

/* TDX Module call error codes */
#define TDCALL_RETURN_CODE(a)	((a) >> 32)
#define TDCALL_INVALID_OPERAND	0xc0000100

#define TDREPORT_SUBTYPE_0	0

static atomic_long_t nr_shared;

/* Called from __tdx_hypercall() for unrecoverable failure */
noinstr void __noreturn __tdx_hypercall_failed(void)
{
	instrumentation_begin();
	panic("TDVMCALL failed. TDX module bug?");
}

#ifdef CONFIG_KVM_GUEST
long tdx_kvm_hypercall(unsigned int nr, unsigned long p1, unsigned long p2,
		       unsigned long p3, unsigned long p4)
{
	struct tdx_module_args args = {
		.r10 = nr,
		.r11 = p1,
		.r12 = p2,
		.r13 = p3,
		.r14 = p4,
	};

	return __tdx_hypercall(&args);
}
EXPORT_SYMBOL_GPL(tdx_kvm_hypercall);
#endif

/*
 * Used for TDX guests to make calls directly to the TD module.  This
 * should only be used for calls that have no legitimate reason to fail
 * or where the kernel can not survive the call failing.
 */
static inline void tdcall(u64 fn, struct tdx_module_args *args)
{
	if (__tdcall_ret(fn, args))
		panic("TDCALL %lld failed (Buggy TDX module!)\n", fn);
}

/* Read TD-scoped metadata */
static inline u64 tdg_vm_rd(u64 field, u64 *value)
{
	struct tdx_module_args args = {
		.rdx = field,
	};
	u64 ret;

	ret = __tdcall_ret(TDG_VM_RD, &args);
	*value = args.r8;

	return ret;
}

/* Write TD-scoped metadata */
static inline u64 tdg_vm_wr(u64 field, u64 value, u64 mask)
{
	struct tdx_module_args args = {
		.rdx = field,
		.r8 = value,
		.r9 = mask,
	};

	return __tdcall(TDG_VM_WR, &args);
}

/**
 * tdx_mcall_get_report0() - Wrapper to get TDREPORT0 (a.k.a. TDREPORT
 *                           subtype 0) using TDG.MR.REPORT TDCALL.
 * @reportdata: Address of the input buffer which contains user-defined
 *              REPORTDATA to be included into TDREPORT.
 * @tdreport: Address of the output buffer to store TDREPORT.
 *
 * Refer to section titled "TDG.MR.REPORT leaf" in the TDX Module
 * v1.0 specification for more information on TDG.MR.REPORT TDCALL.
 * It is used in the TDX guest driver module to get the TDREPORT0.
 *
 * Return 0 on success, -EINVAL for invalid operands, or -EIO on
 * other TDCALL failures.
 */
int tdx_mcall_get_report0(u8 *reportdata, u8 *tdreport)
{
	struct tdx_module_args args = {
		.rcx = virt_to_phys(tdreport),
		.rdx = virt_to_phys(reportdata),
		.r8 = TDREPORT_SUBTYPE_0,
	};
	u64 ret;

	ret = __tdcall(TDG_MR_REPORT, &args);
	if (ret) {
		if (TDCALL_RETURN_CODE(ret) == TDCALL_INVALID_OPERAND)
			return -EINVAL;
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(tdx_mcall_get_report0);

/**
 * tdx_hcall_get_quote() - Wrapper to request TD Quote using GetQuote
 *                         hypercall.
 * @buf: Address of the directly mapped shared kernel buffer which
 *       contains TDREPORT. The same buffer will be used by VMM to
 *       store the generated TD Quote output.
 * @size: size of the tdquote buffer (4KB-aligned).
 *
 * Refer to section titled "TDG.VP.VMCALL<GetQuote>" in the TDX GHCI
 * v1.0 specification for more information on GetQuote hypercall.
 * It is used in the TDX guest driver module to get the TD Quote.
 *
 * Return 0 on success or error code on failure.
 */
u64 tdx_hcall_get_quote(u8 *buf, size_t size)
{
	/* Since buf is a shared memory, set the shared (decrypted) bits */
	return _tdx_hypercall(TDVMCALL_GET_QUOTE, cc_mkdec(virt_to_phys(buf)), size, 0, 0);
}
EXPORT_SYMBOL_GPL(tdx_hcall_get_quote);

static void __noreturn tdx_panic(const char *msg)
{
	struct tdx_module_args args = {
		.r10 = TDX_HYPERCALL_STANDARD,
		.r11 = TDVMCALL_REPORT_FATAL_ERROR,
		.r12 = 0, /* Error code: 0 is Panic */
	};
	union {
		/* Define register order according to the GHCI */
		struct { u64 r14, r15, rbx, rdi, rsi, r8, r9, rdx; };

		char str[64];
	} message;

	/* VMM assumes '\0' in byte 65, if the message took all 64 bytes */
	strtomem_pad(message.str, msg, '\0');

	args.r8  = message.r8;
	args.r9  = message.r9;
	args.r14 = message.r14;
	args.r15 = message.r15;
	args.rdi = message.rdi;
	args.rsi = message.rsi;
	args.rbx = message.rbx;
	args.rdx = message.rdx;

	/*
	 * This hypercall should never return and it is not safe
	 * to keep the guest running. Call it forever if it
	 * happens to return.
	 */
	while (1)
		__tdx_hypercall(&args);
}

/*
 * The kernel cannot handle #VEs when accessing normal kernel memory. Ensure
 * that no #VE will be delivered for accesses to TD-private memory.
 *
 * TDX 1.0 does not allow the guest to disable SEPT #VE on its own. The VMM
 * controls if the guest will receive such #VE with TD attribute
 * ATTR_SEPT_VE_DISABLE.
 *
 * Newer TDX modules allow the guest to control if it wants to receive SEPT
 * violation #VEs.
 *
 * Check if the feature is available and disable SEPT #VE if possible.
 *
 * If the TD is allowed to disable/enable SEPT #VEs, the ATTR_SEPT_VE_DISABLE
 * attribute is no longer reliable. It reflects the initial state of the
 * control for the TD, but it will not be updated if someone (e.g. bootloader)
 * changes it before the kernel starts. Kernel must check TDCS_TD_CTLS bit to
 * determine if SEPT #VEs are enabled or disabled.
 */
static void disable_sept_ve(u64 td_attr)
{
	const char *msg = "TD misconfiguration: SEPT #VE has to be disabled";
	bool debug = td_attr & ATTR_DEBUG;
	u64 config, controls;

	/* Is this TD allowed to disable SEPT #VE */
	tdg_vm_rd(TDCS_CONFIG_FLAGS, &config);
	if (!(config & TDCS_CONFIG_FLEXIBLE_PENDING_VE)) {
		/* No SEPT #VE controls for the guest: check the attribute */
		if (td_attr & ATTR_SEPT_VE_DISABLE)
			return;

		/* Relax SEPT_VE_DISABLE check for debug TD for backtraces */
		if (debug)
			pr_warn("%s\n", msg);
		else
			tdx_panic(msg);
		return;
	}

	/* Check if SEPT #VE has been disabled before us */
	tdg_vm_rd(TDCS_TD_CTLS, &controls);
	if (controls & TD_CTLS_PENDING_VE_DISABLE)
		return;

	/* Keep #VEs enabled for splats in debugging environments */
	if (debug)
		return;

	/* Disable SEPT #VEs */
	tdg_vm_wr(TDCS_TD_CTLS, TD_CTLS_PENDING_VE_DISABLE,
		  TD_CTLS_PENDING_VE_DISABLE);
}

static void tdx_setup(u64 *cc_mask)
{
	struct tdx_module_args args = {};
	unsigned int gpa_width;
	u64 td_attr;

	/*
	 * TDINFO TDX module call is used to get the TD execution environment
	 * information like GPA width, number of available vcpus, debug mode
	 * information, etc. More details about the ABI can be found in TDX
	 * Guest-Host-Communication Interface (GHCI), section 2.4.2 TDCALL
	 * [TDG.VP.INFO].
	 */
	tdcall(TDG_VP_INFO, &args);

	/*
	 * The highest bit of a guest physical address is the "sharing" bit.
	 * Set it for shared pages and clear it for private pages.
	 *
	 * The GPA width that comes out of this call is critical. TDX guests
	 * can not meaningfully run without it.
	 */
	gpa_width = args.rcx & GENMASK(5, 0);
	*cc_mask = BIT_ULL(gpa_width - 1);

	td_attr = args.rdx;

	/* Kernel does not use NOTIFY_ENABLES and does not need random #VEs */
	tdg_vm_wr(TDCS_NOTIFY_ENABLES, 0, -1ULL);

	disable_sept_ve(td_attr);
}

/*
 * The TDX module spec states that #VE may be injected for a limited set of
 * reasons:
 *
 *  - Emulation of the architectural #VE injection on EPT violation;
 *
 *  - As a result of guest TD execution of a disallowed instruction,
 *    a disallowed MSR access, or CPUID virtualization;
 *
 *  - A notification to the guest TD about anomalous behavior;
 *
 * The last one is opt-in and is not used by the kernel.
 *
 * The Intel Software Developer's Manual describes cases when instruction
 * length field can be used in section "Information for VM Exits Due to
 * Instruction Execution".
 *
 * For TDX, it ultimately means GET_VEINFO provides reliable instruction length
 * information if #VE occurred due to instruction execution, but not for EPT
 * violations.
 */
static int ve_instr_len(struct ve_info *ve)
{
	switch (ve->exit_reason) {
	case EXIT_REASON_HLT:
	case EXIT_REASON_MSR_READ:
	case EXIT_REASON_MSR_WRITE:
	case EXIT_REASON_CPUID:
	case EXIT_REASON_IO_INSTRUCTION:
		/* It is safe to use ve->instr_len for #VE due instructions */
		return ve->instr_len;
	case EXIT_REASON_EPT_VIOLATION:
		/*
		 * For EPT violations, ve->insn_len is not defined. For those,
		 * the kernel must decode instructions manually and should not
		 * be using this function.
		 */
		WARN_ONCE(1, "ve->instr_len is not defined for EPT violations");
		return 0;
	default:
		WARN_ONCE(1, "Unexpected #VE-type: %lld\n", ve->exit_reason);
		return ve->instr_len;
	}
}

static u64 __cpuidle __halt(const bool irq_disabled)
{
	struct tdx_module_args args = {
		.r10 = TDX_HYPERCALL_STANDARD,
		.r11 = hcall_func(EXIT_REASON_HLT),
		.r12 = irq_disabled,
	};

	/*
	 * Emulate HLT operation via hypercall. More info about ABI
	 * can be found in TDX Guest-Host-Communication Interface
	 * (GHCI), section 3.8 TDG.VP.VMCALL<Instruction.HLT>.
	 *
	 * The VMM uses the "IRQ disabled" param to understand IRQ
	 * enabled status (RFLAGS.IF) of the TD guest and to determine
	 * whether or not it should schedule the halted vCPU if an
	 * IRQ becomes pending. E.g. if IRQs are disabled, the VMM
	 * can keep the vCPU in virtual HLT, even if an IRQ is
	 * pending, without hanging/breaking the guest.
	 */
	return __tdx_hypercall(&args);
}

static int handle_halt(struct ve_info *ve)
{
	const bool irq_disabled = irqs_disabled();

	if (__halt(irq_disabled))
		return -EIO;

	return ve_instr_len(ve);
}

void __cpuidle tdx_safe_halt(void)
{
	const bool irq_disabled = false;

	/*
	 * Use WARN_ONCE() to report the failure.
	 */
	if (__halt(irq_disabled))
		WARN_ONCE(1, "HLT instruction emulation failed\n");
}

static int read_msr(struct pt_regs *regs, struct ve_info *ve)
{
	struct tdx_module_args args = {
		.r10 = TDX_HYPERCALL_STANDARD,
		.r11 = hcall_func(EXIT_REASON_MSR_READ),
		.r12 = regs->cx,
	};

	/*
	 * Emulate the MSR read via hypercall. More info about ABI
	 * can be found in TDX Guest-Host-Communication Interface
	 * (GHCI), section titled "TDG.VP.VMCALL<Instruction.RDMSR>".
	 */
	if (__tdx_hypercall(&args))
		return -EIO;

	regs->ax = lower_32_bits(args.r11);
	regs->dx = upper_32_bits(args.r11);
	return ve_instr_len(ve);
}

static int write_msr(struct pt_regs *regs, struct ve_info *ve)
{
	struct tdx_module_args args = {
		.r10 = TDX_HYPERCALL_STANDARD,
		.r11 = hcall_func(EXIT_REASON_MSR_WRITE),
		.r12 = regs->cx,
		.r13 = (u64)regs->dx << 32 | regs->ax,
	};

	/*
	 * Emulate the MSR write via hypercall. More info about ABI
	 * can be found in TDX Guest-Host-Communication Interface
	 * (GHCI) section titled "TDG.VP.VMCALL<Instruction.WRMSR>".
	 */
	if (__tdx_hypercall(&args))
		return -EIO;

	return ve_instr_len(ve);
}

static int handle_cpuid(struct pt_regs *regs, struct ve_info *ve)
{
	struct tdx_module_args args = {
		.r10 = TDX_HYPERCALL_STANDARD,
		.r11 = hcall_func(EXIT_REASON_CPUID),
		.r12 = regs->ax,
		.r13 = regs->cx,
	};

	/*
	 * Only allow VMM to control range reserved for hypervisor
	 * communication.
	 *
	 * Return all-zeros for any CPUID outside the range. It matches CPU
	 * behaviour for non-supported leaf.
	 */
	if (regs->ax < 0x40000000 || regs->ax > 0x4FFFFFFF) {
		regs->ax = regs->bx = regs->cx = regs->dx = 0;
		return ve_instr_len(ve);
	}

	/*
	 * Emulate the CPUID instruction via a hypercall. More info about
	 * ABI can be found in TDX Guest-Host-Communication Interface
	 * (GHCI), section titled "VP.VMCALL<Instruction.CPUID>".
	 */
	if (__tdx_hypercall(&args))
		return -EIO;

	/*
	 * As per TDX GHCI CPUID ABI, r12-r15 registers contain contents of
	 * EAX, EBX, ECX, EDX registers after the CPUID instruction execution.
	 * So copy the register contents back to pt_regs.
	 */
	regs->ax = args.r12;
	regs->bx = args.r13;
	regs->cx = args.r14;
	regs->dx = args.r15;

	return ve_instr_len(ve);
}

static bool mmio_read(int size, unsigned long addr, unsigned long *val)
{
	struct tdx_module_args args = {
		.r10 = TDX_HYPERCALL_STANDARD,
		.r11 = hcall_func(EXIT_REASON_EPT_VIOLATION),
		.r12 = size,
		.r13 = EPT_READ,
		.r14 = addr,
	};

	if (__tdx_hypercall(&args))
		return false;

	*val = args.r11;
	return true;
}

static bool mmio_write(int size, unsigned long addr, unsigned long val)
{
	return !_tdx_hypercall(hcall_func(EXIT_REASON_EPT_VIOLATION), size,
			       EPT_WRITE, addr, val);
}

static int handle_mmio(struct pt_regs *regs, struct ve_info *ve)
{
	unsigned long *reg, val, vaddr;
	char buffer[MAX_INSN_SIZE];
	enum insn_mmio_type mmio;
	struct insn insn = {};
	int size, extend_size;
	u8 extend_val = 0;

	/* Only in-kernel MMIO is supported */
	if (WARN_ON_ONCE(user_mode(regs)))
		return -EFAULT;

	if (copy_from_kernel_nofault(buffer, (void *)regs->ip, MAX_INSN_SIZE))
		return -EFAULT;

	if (insn_decode(&insn, buffer, MAX_INSN_SIZE, INSN_MODE_64))
		return -EINVAL;

	mmio = insn_decode_mmio(&insn, &size);
	if (WARN_ON_ONCE(mmio == INSN_MMIO_DECODE_FAILED))
		return -EINVAL;

	if (mmio != INSN_MMIO_WRITE_IMM && mmio != INSN_MMIO_MOVS) {
		reg = insn_get_modrm_reg_ptr(&insn, regs);
		if (!reg)
			return -EINVAL;
	}

	if (!fault_in_kernel_space(ve->gla)) {
		WARN_ONCE(1, "Access to userspace address is not supported");
		return -EINVAL;
	}

	/*
	 * Reject EPT violation #VEs that split pages.
	 *
	 * MMIO accesses are supposed to be naturally aligned and therefore
	 * never cross page boundaries. Seeing split page accesses indicates
	 * a bug or a load_unaligned_zeropad() that stepped into an MMIO page.
	 *
	 * load_unaligned_zeropad() will recover using exception fixups.
	 */
	vaddr = (unsigned long)insn_get_addr_ref(&insn, regs);
	if (vaddr / PAGE_SIZE != (vaddr + size - 1) / PAGE_SIZE)
		return -EFAULT;

	/* Handle writes first */
	switch (mmio) {
	case INSN_MMIO_WRITE:
		memcpy(&val, reg, size);
		if (!mmio_write(size, ve->gpa, val))
			return -EIO;
		return insn.length;
	case INSN_MMIO_WRITE_IMM:
		val = insn.immediate.value;
		if (!mmio_write(size, ve->gpa, val))
			return -EIO;
		return insn.length;
	case INSN_MMIO_READ:
	case INSN_MMIO_READ_ZERO_EXTEND:
	case INSN_MMIO_READ_SIGN_EXTEND:
		/* Reads are handled below */
		break;
	case INSN_MMIO_MOVS:
	case INSN_MMIO_DECODE_FAILED:
		/*
		 * MMIO was accessed with an instruction that could not be
		 * decoded or handled properly. It was likely not using io.h
		 * helpers or accessed MMIO accidentally.
		 */
		return -EINVAL;
	default:
		WARN_ONCE(1, "Unknown insn_decode_mmio() decode value?");
		return -EINVAL;
	}

	/* Handle reads */
	if (!mmio_read(size, ve->gpa, &val))
		return -EIO;

	switch (mmio) {
	case INSN_MMIO_READ:
		/* Zero-extend for 32-bit operation */
		extend_size = size == 4 ? sizeof(*reg) : 0;
		break;
	case INSN_MMIO_READ_ZERO_EXTEND:
		/* Zero extend based on operand size */
		extend_size = insn.opnd_bytes;
		break;
	case INSN_MMIO_READ_SIGN_EXTEND:
		/* Sign extend based on operand size */
		extend_size = insn.opnd_bytes;
		if (size == 1 && val & BIT(7))
			extend_val = 0xFF;
		else if (size > 1 && val & BIT(15))
			extend_val = 0xFF;
		break;
	default:
		/* All other cases has to be covered with the first switch() */
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	if (extend_size)
		memset(reg, extend_val, extend_size);
	memcpy(reg, &val, size);
	return insn.length;
}

static bool handle_in(struct pt_regs *regs, int size, int port)
{
	struct tdx_module_args args = {
		.r10 = TDX_HYPERCALL_STANDARD,
		.r11 = hcall_func(EXIT_REASON_IO_INSTRUCTION),
		.r12 = size,
		.r13 = PORT_READ,
		.r14 = port,
	};
	u64 mask = GENMASK(BITS_PER_BYTE * size, 0);
	bool success;

	/*
	 * Emulate the I/O read via hypercall. More info about ABI can be found
	 * in TDX Guest-Host-Communication Interface (GHCI) section titled
	 * "TDG.VP.VMCALL<Instruction.IO>".
	 */
	success = !__tdx_hypercall(&args);

	/* Update part of the register affected by the emulated instruction */
	regs->ax &= ~mask;
	if (success)
		regs->ax |= args.r11 & mask;

	return success;
}

static bool handle_out(struct pt_regs *regs, int size, int port)
{
	u64 mask = GENMASK(BITS_PER_BYTE * size, 0);

	/*
	 * Emulate the I/O write via hypercall. More info about ABI can be found
	 * in TDX Guest-Host-Communication Interface (GHCI) section titled
	 * "TDG.VP.VMCALL<Instruction.IO>".
	 */
	return !_tdx_hypercall(hcall_func(EXIT_REASON_IO_INSTRUCTION), size,
			       PORT_WRITE, port, regs->ax & mask);
}

/*
 * Emulate I/O using hypercall.
 *
 * Assumes the IO instruction was using ax, which is enforced
 * by the standard io.h macros.
 *
 * Return True on success or False on failure.
 */
static int handle_io(struct pt_regs *regs, struct ve_info *ve)
{
	u32 exit_qual = ve->exit_qual;
	int size, port;
	bool in, ret;

	if (VE_IS_IO_STRING(exit_qual))
		return -EIO;

	in   = VE_IS_IO_IN(exit_qual);
	size = VE_GET_IO_SIZE(exit_qual);
	port = VE_GET_PORT_NUM(exit_qual);


	if (in)
		ret = handle_in(regs, size, port);
	else
		ret = handle_out(regs, size, port);
	if (!ret)
		return -EIO;

	return ve_instr_len(ve);
}

/*
 * Early #VE exception handler. Only handles a subset of port I/O.
 * Intended only for earlyprintk. If failed, return false.
 */
__init bool tdx_early_handle_ve(struct pt_regs *regs)
{
	struct ve_info ve;
	int insn_len;

	tdx_get_ve_info(&ve);

	if (ve.exit_reason != EXIT_REASON_IO_INSTRUCTION)
		return false;

	insn_len = handle_io(regs, &ve);
	if (insn_len < 0)
		return false;

	regs->ip += insn_len;
	return true;
}

void tdx_get_ve_info(struct ve_info *ve)
{
	struct tdx_module_args args = {};

	/*
	 * Called during #VE handling to retrieve the #VE info from the
	 * TDX module.
	 *
	 * This has to be called early in #VE handling.  A "nested" #VE which
	 * occurs before this will raise a #DF and is not recoverable.
	 *
	 * The call retrieves the #VE info from the TDX module, which also
	 * clears the "#VE valid" flag. This must be done before anything else
	 * because any #VE that occurs while the valid flag is set will lead to
	 * #DF.
	 *
	 * Note, the TDX module treats virtual NMIs as inhibited if the #VE
	 * valid flag is set. It means that NMI=>#VE will not result in a #DF.
	 */
	tdcall(TDG_VP_VEINFO_GET, &args);

	/* Transfer the output parameters */
	ve->exit_reason = args.rcx;
	ve->exit_qual   = args.rdx;
	ve->gla         = args.r8;
	ve->gpa         = args.r9;
	ve->instr_len   = lower_32_bits(args.r10);
	ve->instr_info  = upper_32_bits(args.r10);
}

/*
 * Handle the user initiated #VE.
 *
 * On success, returns the number of bytes RIP should be incremented (>=0)
 * or -errno on error.
 */
static int virt_exception_user(struct pt_regs *regs, struct ve_info *ve)
{
	switch (ve->exit_reason) {
	case EXIT_REASON_CPUID:
		return handle_cpuid(regs, ve);
	default:
		pr_warn("Unexpected #VE: %lld\n", ve->exit_reason);
		return -EIO;
	}
}

static inline bool is_private_gpa(u64 gpa)
{
	return gpa == cc_mkenc(gpa);
}

/*
 * Handle the kernel #VE.
 *
 * On success, returns the number of bytes RIP should be incremented (>=0)
 * or -errno on error.
 */
static int virt_exception_kernel(struct pt_regs *regs, struct ve_info *ve)
{
	switch (ve->exit_reason) {
	case EXIT_REASON_HLT:
		return handle_halt(ve);
	case EXIT_REASON_MSR_READ:
		return read_msr(regs, ve);
	case EXIT_REASON_MSR_WRITE:
		return write_msr(regs, ve);
	case EXIT_REASON_CPUID:
		return handle_cpuid(regs, ve);
	case EXIT_REASON_EPT_VIOLATION:
		if (is_private_gpa(ve->gpa))
			panic("Unexpected EPT-violation on private memory.");
		return handle_mmio(regs, ve);
	case EXIT_REASON_IO_INSTRUCTION:
		return handle_io(regs, ve);
	default:
		pr_warn("Unexpected #VE: %lld\n", ve->exit_reason);
		return -EIO;
	}
}

bool tdx_handle_virt_exception(struct pt_regs *regs, struct ve_info *ve)
{
	int insn_len;

	if (user_mode(regs))
		insn_len = virt_exception_user(regs, ve);
	else
		insn_len = virt_exception_kernel(regs, ve);
	if (insn_len < 0)
		return false;

	/* After successful #VE handling, move the IP */
	regs->ip += insn_len;

	return true;
}

static bool tdx_tlb_flush_required(bool private)
{
	/*
	 * TDX guest is responsible for flushing TLB on private->shared
	 * transition. VMM is responsible for flushing on shared->private.
	 *
	 * The VMM _can't_ flush private addresses as it can't generate PAs
	 * with the guest's HKID.  Shared memory isn't subject to integrity
	 * checking, i.e. the VMM doesn't need to flush for its own protection.
	 *
	 * There's no need to flush when converting from shared to private,
	 * as flushing is the VMM's responsibility in this case, e.g. it must
	 * flush to avoid integrity failures in the face of a buggy or
	 * malicious guest.
	 */
	return !private;
}

static bool tdx_cache_flush_required(void)
{
	/*
	 * AMD SME/SEV can avoid cache flushing if HW enforces cache coherence.
	 * TDX doesn't have such capability.
	 *
	 * Flush cache unconditionally.
	 */
	return true;
}

/*
 * Notify the VMM about page mapping conversion. More info about ABI
 * can be found in TDX Guest-Host-Communication Interface (GHCI),
 * section "TDG.VP.VMCALL<MapGPA>".
 */
static bool tdx_map_gpa(phys_addr_t start, phys_addr_t end, bool enc)
{
	/* Retrying the hypercall a second time should succeed; use 3 just in case */
	const int max_retries_per_page = 3;
	int retry_count = 0;

	if (!enc) {
		/* Set the shared (decrypted) bits: */
		start |= cc_mkdec(0);
		end   |= cc_mkdec(0);
	}

	while (retry_count < max_retries_per_page) {
		struct tdx_module_args args = {
			.r10 = TDX_HYPERCALL_STANDARD,
			.r11 = TDVMCALL_MAP_GPA,
			.r12 = start,
			.r13 = end - start };

		u64 map_fail_paddr;
		u64 ret = __tdx_hypercall(&args);

		if (ret != TDVMCALL_STATUS_RETRY)
			return !ret;
		/*
		 * The guest must retry the operation for the pages in the
		 * region starting at the GPA specified in R11. R11 comes
		 * from the untrusted VMM. Sanity check it.
		 */
		map_fail_paddr = args.r11;
		if (map_fail_paddr < start || map_fail_paddr >= end)
			return false;

		/* "Consume" a retry without forward progress */
		if (map_fail_paddr == start) {
			retry_count++;
			continue;
		}

		start = map_fail_paddr;
		retry_count = 0;
	}

	return false;
}

/*
 * Inform the VMM of the guest's intent for this physical page: shared with
 * the VMM or private to the guest.  The VMM is expected to change its mapping
 * of the page in response.
 */
static bool tdx_enc_status_changed(unsigned long vaddr, int numpages, bool enc)
{
	phys_addr_t start = __pa(vaddr);
	phys_addr_t end   = __pa(vaddr + numpages * PAGE_SIZE);

	if (!tdx_map_gpa(start, end, enc))
		return false;

	/* shared->private conversion requires memory to be accepted before use */
	if (enc)
		return tdx_accept_memory(start, end);

	return true;
}

static int tdx_enc_status_change_prepare(unsigned long vaddr, int numpages,
					 bool enc)
{
	/*
	 * Only handle shared->private conversion here.
	 * See the comment in tdx_early_init().
	 */
	if (enc && !tdx_enc_status_changed(vaddr, numpages, enc))
		return -EIO;

	return 0;
}

static int tdx_enc_status_change_finish(unsigned long vaddr, int numpages,
					 bool enc)
{
	/*
	 * Only handle private->shared conversion here.
	 * See the comment in tdx_early_init().
	 */
	if (!enc && !tdx_enc_status_changed(vaddr, numpages, enc))
		return -EIO;

	if (enc)
		atomic_long_sub(numpages, &nr_shared);
	else
		atomic_long_add(numpages, &nr_shared);

	return 0;
}

/* Stop new private<->shared conversions */
static void tdx_kexec_begin(void)
{
	if (!IS_ENABLED(CONFIG_KEXEC_CORE))
		return;

	/*
	 * Crash kernel reaches here with interrupts disabled: can't wait for
	 * conversions to finish.
	 *
	 * If race happened, just report and proceed.
	 */
	if (!set_memory_enc_stop_conversion())
		pr_warn("Failed to stop shared<->private conversions\n");
}

/* Walk direct mapping and convert all shared memory back to private */
static void tdx_kexec_finish(void)
{
	unsigned long addr, end;
	long found = 0, shared;

	if (!IS_ENABLED(CONFIG_KEXEC_CORE))
		return;

	lockdep_assert_irqs_disabled();

	addr = PAGE_OFFSET;
	end  = PAGE_OFFSET + get_max_mapped();

	while (addr < end) {
		unsigned long size;
		unsigned int level;
		pte_t *pte;

		pte = lookup_address(addr, &level);
		size = page_level_size(level);

		if (pte && pte_decrypted(*pte)) {
			int pages = size / PAGE_SIZE;

			/*
			 * Touching memory with shared bit set triggers implicit
			 * conversion to shared.
			 *
			 * Make sure nobody touches the shared range from
			 * now on.
			 */
			set_pte(pte, __pte(0));

			/*
			 * Memory encryption state persists across kexec.
			 * If tdx_enc_status_changed() fails in the first
			 * kernel, it leaves memory in an unknown state.
			 *
			 * If that memory remains shared, accessing it in the
			 * *next* kernel through a private mapping will result
			 * in an unrecoverable guest shutdown.
			 *
			 * The kdump kernel boot is not impacted as it uses
			 * a pre-reserved memory range that is always private.
			 * However, gathering crash information could lead to
			 * a crash if it accesses unconverted memory through
			 * a private mapping which is possible when accessing
			 * that memory through /proc/vmcore, for example.
			 *
			 * In all cases, print error info in order to leave
			 * enough bread crumbs for debugging.
			 */
			if (!tdx_enc_status_changed(addr, pages, true)) {
				pr_err("Failed to unshare range %#lx-%#lx\n",
				       addr, addr + size);
			}

			found += pages;
		}

		addr += size;
	}

	__flush_tlb_all();

	shared = atomic_long_read(&nr_shared);
	if (shared != found) {
		pr_err("shared page accounting is off\n");
		pr_err("nr_shared = %ld, nr_found = %ld\n", shared, found);
	}
}

void __init tdx_early_init(void)
{
	u64 cc_mask;
	u32 eax, sig[3];

	cpuid_count(TDX_CPUID_LEAF_ID, 0, &eax, &sig[0], &sig[2],  &sig[1]);

	if (memcmp(TDX_IDENT, sig, sizeof(sig)))
		return;

	setup_force_cpu_cap(X86_FEATURE_TDX_GUEST);

	/* TSC is the only reliable clock in TDX guest */
	setup_force_cpu_cap(X86_FEATURE_TSC_RELIABLE);

	cc_vendor = CC_VENDOR_INTEL;

	/* Configure the TD */
	tdx_setup(&cc_mask);

	cc_set_mask(cc_mask);

	/*
	 * All bits above GPA width are reserved and kernel treats shared bit
	 * as flag, not as part of physical address.
	 *
	 * Adjust physical mask to only cover valid GPA bits.
	 */
	physical_mask &= cc_mask - 1;

	/*
	 * The kernel mapping should match the TDX metadata for the page.
	 * load_unaligned_zeropad() can touch memory *adjacent* to that which is
	 * owned by the caller and can catch even _momentary_ mismatches.  Bad
	 * things happen on mismatch:
	 *
	 *   - Private mapping => Shared Page  == Guest shutdown
         *   - Shared mapping  => Private Page == Recoverable #VE
	 *
	 * guest.enc_status_change_prepare() converts the page from
	 * shared=>private before the mapping becomes private.
	 *
	 * guest.enc_status_change_finish() converts the page from
	 * private=>shared after the mapping becomes private.
	 *
	 * In both cases there is a temporary shared mapping to a private page,
	 * which can result in a #VE.  But, there is never a private mapping to
	 * a shared page.
	 */
	x86_platform.guest.enc_status_change_prepare = tdx_enc_status_change_prepare;
	x86_platform.guest.enc_status_change_finish  = tdx_enc_status_change_finish;

	x86_platform.guest.enc_cache_flush_required  = tdx_cache_flush_required;
	x86_platform.guest.enc_tlb_flush_required    = tdx_tlb_flush_required;

	x86_platform.guest.enc_kexec_begin	     = tdx_kexec_begin;
	x86_platform.guest.enc_kexec_finish	     = tdx_kexec_finish;

	/*
	 * TDX intercepts the RDMSR to read the X2APIC ID in the parallel
	 * bringup low level code. That raises #VE which cannot be handled
	 * there.
	 *
	 * Intel-TDX has a secure RDMSR hypercall, but that needs to be
	 * implemented separately in the low level startup ASM code.
	 * Until that is in place, disable parallel bringup for TDX.
	 */
	x86_cpuinit.parallel_bringup = false;

	pr_info("Guest detected\n");
}
