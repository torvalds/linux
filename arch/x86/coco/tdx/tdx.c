// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021-2022 Intel Corporation */

#undef pr_fmt
#define pr_fmt(fmt)     "tdx: " fmt

#include <linux/cpufeature.h>
#include <asm/coco.h>
#include <asm/tdx.h>
#include <asm/vmx.h>
#include <asm/insn.h>
#include <asm/insn-eval.h>
#include <asm/pgtable.h>

/* TDX module Call Leaf IDs */
#define TDX_GET_INFO			1
#define TDX_GET_VEINFO			3
#define TDX_ACCEPT_PAGE			6

/* TDX hypercall Leaf IDs */
#define TDVMCALL_MAP_GPA		0x10001

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

/*
 * Wrapper for standard use of __tdx_hypercall with no output aside from
 * return code.
 */
static inline u64 _tdx_hypercall(u64 fn, u64 r12, u64 r13, u64 r14, u64 r15)
{
	struct tdx_hypercall_args args = {
		.r10 = TDX_HYPERCALL_STANDARD,
		.r11 = fn,
		.r12 = r12,
		.r13 = r13,
		.r14 = r14,
		.r15 = r15,
	};

	return __tdx_hypercall(&args, 0);
}

/* Called from __tdx_hypercall() for unrecoverable failure */
void __tdx_hypercall_failed(void)
{
	panic("TDVMCALL failed. TDX module bug?");
}

/*
 * The TDG.VP.VMCALL-Instruction-execution sub-functions are defined
 * independently from but are currently matched 1:1 with VMX EXIT_REASONs.
 * Reusing the KVM EXIT_REASON macros makes it easier to connect the host and
 * guest sides of these calls.
 */
static u64 hcall_func(u64 exit_reason)
{
	return exit_reason;
}

#ifdef CONFIG_KVM_GUEST
long tdx_kvm_hypercall(unsigned int nr, unsigned long p1, unsigned long p2,
		       unsigned long p3, unsigned long p4)
{
	struct tdx_hypercall_args args = {
		.r10 = nr,
		.r11 = p1,
		.r12 = p2,
		.r13 = p3,
		.r14 = p4,
	};

	return __tdx_hypercall(&args, 0);
}
EXPORT_SYMBOL_GPL(tdx_kvm_hypercall);
#endif

/*
 * Used for TDX guests to make calls directly to the TD module.  This
 * should only be used for calls that have no legitimate reason to fail
 * or where the kernel can not survive the call failing.
 */
static inline void tdx_module_call(u64 fn, u64 rcx, u64 rdx, u64 r8, u64 r9,
				   struct tdx_module_output *out)
{
	if (__tdx_module_call(fn, rcx, rdx, r8, r9, out))
		panic("TDCALL %lld failed (Buggy TDX module!)\n", fn);
}

static u64 get_cc_mask(void)
{
	struct tdx_module_output out;
	unsigned int gpa_width;

	/*
	 * TDINFO TDX module call is used to get the TD execution environment
	 * information like GPA width, number of available vcpus, debug mode
	 * information, etc. More details about the ABI can be found in TDX
	 * Guest-Host-Communication Interface (GHCI), section 2.4.2 TDCALL
	 * [TDG.VP.INFO].
	 *
	 * The GPA width that comes out of this call is critical. TDX guests
	 * can not meaningfully run without it.
	 */
	tdx_module_call(TDX_GET_INFO, 0, 0, 0, 0, &out);

	gpa_width = out.rcx & GENMASK(5, 0);

	/*
	 * The highest bit of a guest physical address is the "sharing" bit.
	 * Set it for shared pages and clear it for private pages.
	 */
	return BIT_ULL(gpa_width - 1);
}

static u64 __cpuidle __halt(const bool irq_disabled, const bool do_sti)
{
	struct tdx_hypercall_args args = {
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
	return __tdx_hypercall(&args, do_sti ? TDX_HCALL_ISSUE_STI : 0);
}

static bool handle_halt(void)
{
	/*
	 * Since non safe halt is mainly used in CPU offlining
	 * and the guest will always stay in the halt state, don't
	 * call the STI instruction (set do_sti as false).
	 */
	const bool irq_disabled = irqs_disabled();
	const bool do_sti = false;

	if (__halt(irq_disabled, do_sti))
		return false;

	return true;
}

void __cpuidle tdx_safe_halt(void)
{
	 /*
	  * For do_sti=true case, __tdx_hypercall() function enables
	  * interrupts using the STI instruction before the TDCALL. So
	  * set irq_disabled as false.
	  */
	const bool irq_disabled = false;
	const bool do_sti = true;

	/*
	 * Use WARN_ONCE() to report the failure.
	 */
	if (__halt(irq_disabled, do_sti))
		WARN_ONCE(1, "HLT instruction emulation failed\n");
}

static bool read_msr(struct pt_regs *regs)
{
	struct tdx_hypercall_args args = {
		.r10 = TDX_HYPERCALL_STANDARD,
		.r11 = hcall_func(EXIT_REASON_MSR_READ),
		.r12 = regs->cx,
	};

	/*
	 * Emulate the MSR read via hypercall. More info about ABI
	 * can be found in TDX Guest-Host-Communication Interface
	 * (GHCI), section titled "TDG.VP.VMCALL<Instruction.RDMSR>".
	 */
	if (__tdx_hypercall(&args, TDX_HCALL_HAS_OUTPUT))
		return false;

	regs->ax = lower_32_bits(args.r11);
	regs->dx = upper_32_bits(args.r11);
	return true;
}

static bool write_msr(struct pt_regs *regs)
{
	struct tdx_hypercall_args args = {
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
	return !__tdx_hypercall(&args, 0);
}

static bool handle_cpuid(struct pt_regs *regs)
{
	struct tdx_hypercall_args args = {
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
		return true;
	}

	/*
	 * Emulate the CPUID instruction via a hypercall. More info about
	 * ABI can be found in TDX Guest-Host-Communication Interface
	 * (GHCI), section titled "VP.VMCALL<Instruction.CPUID>".
	 */
	if (__tdx_hypercall(&args, TDX_HCALL_HAS_OUTPUT))
		return false;

	/*
	 * As per TDX GHCI CPUID ABI, r12-r15 registers contain contents of
	 * EAX, EBX, ECX, EDX registers after the CPUID instruction execution.
	 * So copy the register contents back to pt_regs.
	 */
	regs->ax = args.r12;
	regs->bx = args.r13;
	regs->cx = args.r14;
	regs->dx = args.r15;

	return true;
}

static bool mmio_read(int size, unsigned long addr, unsigned long *val)
{
	struct tdx_hypercall_args args = {
		.r10 = TDX_HYPERCALL_STANDARD,
		.r11 = hcall_func(EXIT_REASON_EPT_VIOLATION),
		.r12 = size,
		.r13 = EPT_READ,
		.r14 = addr,
		.r15 = *val,
	};

	if (__tdx_hypercall(&args, TDX_HCALL_HAS_OUTPUT))
		return false;
	*val = args.r11;
	return true;
}

static bool mmio_write(int size, unsigned long addr, unsigned long val)
{
	return !_tdx_hypercall(hcall_func(EXIT_REASON_EPT_VIOLATION), size,
			       EPT_WRITE, addr, val);
}

static bool handle_mmio(struct pt_regs *regs, struct ve_info *ve)
{
	char buffer[MAX_INSN_SIZE];
	unsigned long *reg, val;
	struct insn insn = {};
	enum mmio_type mmio;
	int size, extend_size;
	u8 extend_val = 0;

	/* Only in-kernel MMIO is supported */
	if (WARN_ON_ONCE(user_mode(regs)))
		return false;

	if (copy_from_kernel_nofault(buffer, (void *)regs->ip, MAX_INSN_SIZE))
		return false;

	if (insn_decode(&insn, buffer, MAX_INSN_SIZE, INSN_MODE_64))
		return false;

	mmio = insn_decode_mmio(&insn, &size);
	if (WARN_ON_ONCE(mmio == MMIO_DECODE_FAILED))
		return false;

	if (mmio != MMIO_WRITE_IMM && mmio != MMIO_MOVS) {
		reg = insn_get_modrm_reg_ptr(&insn, regs);
		if (!reg)
			return false;
	}

	ve->instr_len = insn.length;

	/* Handle writes first */
	switch (mmio) {
	case MMIO_WRITE:
		memcpy(&val, reg, size);
		return mmio_write(size, ve->gpa, val);
	case MMIO_WRITE_IMM:
		val = insn.immediate.value;
		return mmio_write(size, ve->gpa, val);
	case MMIO_READ:
	case MMIO_READ_ZERO_EXTEND:
	case MMIO_READ_SIGN_EXTEND:
		/* Reads are handled below */
		break;
	case MMIO_MOVS:
	case MMIO_DECODE_FAILED:
		/*
		 * MMIO was accessed with an instruction that could not be
		 * decoded or handled properly. It was likely not using io.h
		 * helpers or accessed MMIO accidentally.
		 */
		return false;
	default:
		WARN_ONCE(1, "Unknown insn_decode_mmio() decode value?");
		return false;
	}

	/* Handle reads */
	if (!mmio_read(size, ve->gpa, &val))
		return false;

	switch (mmio) {
	case MMIO_READ:
		/* Zero-extend for 32-bit operation */
		extend_size = size == 4 ? sizeof(*reg) : 0;
		break;
	case MMIO_READ_ZERO_EXTEND:
		/* Zero extend based on operand size */
		extend_size = insn.opnd_bytes;
		break;
	case MMIO_READ_SIGN_EXTEND:
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
		return false;
	}

	if (extend_size)
		memset(reg, extend_val, extend_size);
	memcpy(reg, &val, size);
	return true;
}

static bool handle_in(struct pt_regs *regs, int size, int port)
{
	struct tdx_hypercall_args args = {
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
	success = !__tdx_hypercall(&args, TDX_HCALL_HAS_OUTPUT);

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
static bool handle_io(struct pt_regs *regs, u32 exit_qual)
{
	int size, port;
	bool in;

	if (VE_IS_IO_STRING(exit_qual))
		return false;

	in   = VE_IS_IO_IN(exit_qual);
	size = VE_GET_IO_SIZE(exit_qual);
	port = VE_GET_PORT_NUM(exit_qual);


	if (in)
		return handle_in(regs, size, port);
	else
		return handle_out(regs, size, port);
}

/*
 * Early #VE exception handler. Only handles a subset of port I/O.
 * Intended only for earlyprintk. If failed, return false.
 */
__init bool tdx_early_handle_ve(struct pt_regs *regs)
{
	struct ve_info ve;

	tdx_get_ve_info(&ve);

	if (ve.exit_reason != EXIT_REASON_IO_INSTRUCTION)
		return false;

	return handle_io(regs, ve.exit_qual);
}

void tdx_get_ve_info(struct ve_info *ve)
{
	struct tdx_module_output out;

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
	tdx_module_call(TDX_GET_VEINFO, 0, 0, 0, 0, &out);

	/* Transfer the output parameters */
	ve->exit_reason = out.rcx;
	ve->exit_qual   = out.rdx;
	ve->gla         = out.r8;
	ve->gpa         = out.r9;
	ve->instr_len   = lower_32_bits(out.r10);
	ve->instr_info  = upper_32_bits(out.r10);
}

/* Handle the user initiated #VE */
static bool virt_exception_user(struct pt_regs *regs, struct ve_info *ve)
{
	switch (ve->exit_reason) {
	case EXIT_REASON_CPUID:
		return handle_cpuid(regs);
	default:
		pr_warn("Unexpected #VE: %lld\n", ve->exit_reason);
		return false;
	}
}

/* Handle the kernel #VE */
static bool virt_exception_kernel(struct pt_regs *regs, struct ve_info *ve)
{
	switch (ve->exit_reason) {
	case EXIT_REASON_HLT:
		return handle_halt();
	case EXIT_REASON_MSR_READ:
		return read_msr(regs);
	case EXIT_REASON_MSR_WRITE:
		return write_msr(regs);
	case EXIT_REASON_CPUID:
		return handle_cpuid(regs);
	case EXIT_REASON_EPT_VIOLATION:
		return handle_mmio(regs, ve);
	case EXIT_REASON_IO_INSTRUCTION:
		return handle_io(regs, ve->exit_qual);
	default:
		pr_warn("Unexpected #VE: %lld\n", ve->exit_reason);
		return false;
	}
}

bool tdx_handle_virt_exception(struct pt_regs *regs, struct ve_info *ve)
{
	bool ret;

	if (user_mode(regs))
		ret = virt_exception_user(regs, ve);
	else
		ret = virt_exception_kernel(regs, ve);

	/* After successful #VE handling, move the IP */
	if (ret)
		regs->ip += ve->instr_len;

	return ret;
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

static bool try_accept_one(phys_addr_t *start, unsigned long len,
			  enum pg_level pg_level)
{
	unsigned long accept_size = page_level_size(pg_level);
	u64 tdcall_rcx;
	u8 page_size;

	if (!IS_ALIGNED(*start, accept_size))
		return false;

	if (len < accept_size)
		return false;

	/*
	 * Pass the page physical address to the TDX module to accept the
	 * pending, private page.
	 *
	 * Bits 2:0 of RCX encode page size: 0 - 4K, 1 - 2M, 2 - 1G.
	 */
	switch (pg_level) {
	case PG_LEVEL_4K:
		page_size = 0;
		break;
	case PG_LEVEL_2M:
		page_size = 1;
		break;
	case PG_LEVEL_1G:
		page_size = 2;
		break;
	default:
		return false;
	}

	tdcall_rcx = *start | page_size;
	if (__tdx_module_call(TDX_ACCEPT_PAGE, tdcall_rcx, 0, 0, 0, NULL))
		return false;

	*start += accept_size;
	return true;
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

	if (!enc) {
		/* Set the shared (decrypted) bits: */
		start |= cc_mkdec(0);
		end   |= cc_mkdec(0);
	}

	/*
	 * Notify the VMM about page mapping conversion. More info about ABI
	 * can be found in TDX Guest-Host-Communication Interface (GHCI),
	 * section "TDG.VP.VMCALL<MapGPA>"
	 */
	if (_tdx_hypercall(TDVMCALL_MAP_GPA, start, end - start, 0, 0))
		return false;

	/* private->shared conversion  requires only MapGPA call */
	if (!enc)
		return true;

	/*
	 * For shared->private conversion, accept the page using
	 * TDX_ACCEPT_PAGE TDX module call.
	 */
	while (start < end) {
		unsigned long len = end - start;

		/*
		 * Try larger accepts first. It gives chance to VMM to keep
		 * 1G/2M SEPT entries where possible and speeds up process by
		 * cutting number of hypercalls (if successful).
		 */

		if (try_accept_one(&start, len, PG_LEVEL_1G))
			continue;

		if (try_accept_one(&start, len, PG_LEVEL_2M))
			continue;

		if (!try_accept_one(&start, len, PG_LEVEL_4K))
			return false;
	}

	return true;
}

void __init tdx_early_init(void)
{
	u64 cc_mask;
	u32 eax, sig[3];

	cpuid_count(TDX_CPUID_LEAF_ID, 0, &eax, &sig[0], &sig[2],  &sig[1]);

	if (memcmp(TDX_IDENT, sig, sizeof(sig)))
		return;

	setup_force_cpu_cap(X86_FEATURE_TDX_GUEST);

	cc_set_vendor(CC_VENDOR_INTEL);
	cc_mask = get_cc_mask();
	cc_set_mask(cc_mask);

	/*
	 * All bits above GPA width are reserved and kernel treats shared bit
	 * as flag, not as part of physical address.
	 *
	 * Adjust physical mask to only cover valid GPA bits.
	 */
	physical_mask &= cc_mask - 1;

	x86_platform.guest.enc_cache_flush_required = tdx_cache_flush_required;
	x86_platform.guest.enc_tlb_flush_required   = tdx_tlb_flush_required;
	x86_platform.guest.enc_status_change_finish = tdx_enc_status_changed;

	pr_info("Guest detected\n");
}
