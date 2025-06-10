// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Encrypted Register State Support
 *
 * Author: Joerg Roedel <jroedel@suse.de>
 *
 * This file is not compiled stand-alone. It contains code shared
 * between the pre-decompression boot code and the running Linux kernel
 * and is included directly into both code-bases.
 */

#include <asm/setup_data.h>

#ifndef __BOOT_COMPRESSED
#define error(v)			pr_err(v)
#define has_cpuflag(f)			boot_cpu_has(f)
#else
#undef WARN
#define WARN(condition, format...) (!!(condition))
#undef vc_forward_exception
#define vc_forward_exception(c)		panic("SNP: Hypervisor requested exception\n")
#endif

/*
 * SVSM related information:
 *   During boot, the page tables are set up as identity mapped and later
 *   changed to use kernel virtual addresses. Maintain separate virtual and
 *   physical addresses for the CAA to allow SVSM functions to be used during
 *   early boot, both with identity mapped virtual addresses and proper kernel
 *   virtual addresses.
 */
struct svsm_ca *boot_svsm_caa __ro_after_init;
u64 boot_svsm_caa_pa __ro_after_init;

/*
 * Since feature negotiation related variables are set early in the boot
 * process they must reside in the .data section so as not to be zeroed
 * out when the .bss section is later cleared.
 *
 * GHCB protocol version negotiated with the hypervisor.
 */
static u16 ghcb_version __ro_after_init;

/* Copy of the SNP firmware's CPUID page. */
static struct snp_cpuid_table cpuid_table_copy __ro_after_init;

/*
 * These will be initialized based on CPUID table so that non-present
 * all-zero leaves (for sparse tables) can be differentiated from
 * invalid/out-of-range leaves. This is needed since all-zero leaves
 * still need to be post-processed.
 */
static u32 cpuid_std_range_max __ro_after_init;
static u32 cpuid_hyp_range_max __ro_after_init;
static u32 cpuid_ext_range_max __ro_after_init;

bool __init sev_es_check_cpu_features(void)
{
	if (!has_cpuflag(X86_FEATURE_RDRAND)) {
		error("RDRAND instruction not supported - no trusted source of randomness available\n");
		return false;
	}

	return true;
}

void __head __noreturn
sev_es_terminate(unsigned int set, unsigned int reason)
{
	u64 val = GHCB_MSR_TERM_REQ;

	/* Tell the hypervisor what went wrong. */
	val |= GHCB_SEV_TERM_REASON(set, reason);

	/* Request Guest Termination from Hypervisor */
	sev_es_wr_ghcb_msr(val);
	VMGEXIT();

	while (true)
		asm volatile("hlt\n" : : : "memory");
}

/*
 * The hypervisor features are available from GHCB version 2 onward.
 */
u64 get_hv_features(void)
{
	u64 val;

	if (ghcb_version < 2)
		return 0;

	sev_es_wr_ghcb_msr(GHCB_MSR_HV_FT_REQ);
	VMGEXIT();

	val = sev_es_rd_ghcb_msr();
	if (GHCB_RESP_CODE(val) != GHCB_MSR_HV_FT_RESP)
		return 0;

	return GHCB_MSR_HV_FT_RESP_VAL(val);
}

void snp_register_ghcb_early(unsigned long paddr)
{
	unsigned long pfn = paddr >> PAGE_SHIFT;
	u64 val;

	sev_es_wr_ghcb_msr(GHCB_MSR_REG_GPA_REQ_VAL(pfn));
	VMGEXIT();

	val = sev_es_rd_ghcb_msr();

	/* If the response GPA is not ours then abort the guest */
	if ((GHCB_RESP_CODE(val) != GHCB_MSR_REG_GPA_RESP) ||
	    (GHCB_MSR_REG_GPA_RESP_VAL(val) != pfn))
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_REGISTER);
}

bool sev_es_negotiate_protocol(void)
{
	u64 val;

	/* Do the GHCB protocol version negotiation */
	sev_es_wr_ghcb_msr(GHCB_MSR_SEV_INFO_REQ);
	VMGEXIT();
	val = sev_es_rd_ghcb_msr();

	if (GHCB_MSR_INFO(val) != GHCB_MSR_SEV_INFO_RESP)
		return false;

	if (GHCB_MSR_PROTO_MAX(val) < GHCB_PROTOCOL_MIN ||
	    GHCB_MSR_PROTO_MIN(val) > GHCB_PROTOCOL_MAX)
		return false;

	ghcb_version = min_t(size_t, GHCB_MSR_PROTO_MAX(val), GHCB_PROTOCOL_MAX);

	return true;
}

static enum es_result verify_exception_info(struct ghcb *ghcb, struct es_em_ctxt *ctxt)
{
	u32 ret;

	ret = ghcb->save.sw_exit_info_1 & GENMASK_ULL(31, 0);
	if (!ret)
		return ES_OK;

	if (ret == 1) {
		u64 info = ghcb->save.sw_exit_info_2;
		unsigned long v = info & SVM_EVTINJ_VEC_MASK;

		/* Check if exception information from hypervisor is sane. */
		if ((info & SVM_EVTINJ_VALID) &&
		    ((v == X86_TRAP_GP) || (v == X86_TRAP_UD)) &&
		    ((info & SVM_EVTINJ_TYPE_MASK) == SVM_EVTINJ_TYPE_EXEPT)) {
			ctxt->fi.vector = v;

			if (info & SVM_EVTINJ_VALID_ERR)
				ctxt->fi.error_code = info >> 32;

			return ES_EXCEPTION;
		}
	}

	return ES_VMM_ERROR;
}

static inline int svsm_process_result_codes(struct svsm_call *call)
{
	switch (call->rax_out) {
	case SVSM_SUCCESS:
		return 0;
	case SVSM_ERR_INCOMPLETE:
	case SVSM_ERR_BUSY:
		return -EAGAIN;
	default:
		return -EINVAL;
	}
}

/*
 * Issue a VMGEXIT to call the SVSM:
 *   - Load the SVSM register state (RAX, RCX, RDX, R8 and R9)
 *   - Set the CA call pending field to 1
 *   - Issue VMGEXIT
 *   - Save the SVSM return register state (RAX, RCX, RDX, R8 and R9)
 *   - Perform atomic exchange of the CA call pending field
 *
 *   - See the "Secure VM Service Module for SEV-SNP Guests" specification for
 *     details on the calling convention.
 *     - The calling convention loosely follows the Microsoft X64 calling
 *       convention by putting arguments in RCX, RDX, R8 and R9.
 *     - RAX specifies the SVSM protocol/callid as input and the return code
 *       as output.
 */
static __always_inline void svsm_issue_call(struct svsm_call *call, u8 *pending)
{
	register unsigned long rax asm("rax") = call->rax;
	register unsigned long rcx asm("rcx") = call->rcx;
	register unsigned long rdx asm("rdx") = call->rdx;
	register unsigned long r8  asm("r8")  = call->r8;
	register unsigned long r9  asm("r9")  = call->r9;

	call->caa->call_pending = 1;

	asm volatile("rep; vmmcall\n\t"
		     : "+r" (rax), "+r" (rcx), "+r" (rdx), "+r" (r8), "+r" (r9)
		     : : "memory");

	*pending = xchg(&call->caa->call_pending, *pending);

	call->rax_out = rax;
	call->rcx_out = rcx;
	call->rdx_out = rdx;
	call->r8_out  = r8;
	call->r9_out  = r9;
}

static int svsm_perform_msr_protocol(struct svsm_call *call)
{
	u8 pending = 0;
	u64 val, resp;

	/*
	 * When using the MSR protocol, be sure to save and restore
	 * the current MSR value.
	 */
	val = sev_es_rd_ghcb_msr();

	sev_es_wr_ghcb_msr(GHCB_MSR_VMPL_REQ_LEVEL(0));

	svsm_issue_call(call, &pending);

	resp = sev_es_rd_ghcb_msr();

	sev_es_wr_ghcb_msr(val);

	if (pending)
		return -EINVAL;

	if (GHCB_RESP_CODE(resp) != GHCB_MSR_VMPL_RESP)
		return -EINVAL;

	if (GHCB_MSR_VMPL_RESP_VAL(resp))
		return -EINVAL;

	return svsm_process_result_codes(call);
}

static int svsm_perform_ghcb_protocol(struct ghcb *ghcb, struct svsm_call *call)
{
	struct es_em_ctxt ctxt;
	u8 pending = 0;

	vc_ghcb_invalidate(ghcb);

	/*
	 * Fill in protocol and format specifiers. This can be called very early
	 * in the boot, so use rip-relative references as needed.
	 */
	ghcb->protocol_version = ghcb_version;
	ghcb->ghcb_usage       = GHCB_DEFAULT_USAGE;

	ghcb_set_sw_exit_code(ghcb, SVM_VMGEXIT_SNP_RUN_VMPL);
	ghcb_set_sw_exit_info_1(ghcb, 0);
	ghcb_set_sw_exit_info_2(ghcb, 0);

	sev_es_wr_ghcb_msr(__pa(ghcb));

	svsm_issue_call(call, &pending);

	if (pending)
		return -EINVAL;

	switch (verify_exception_info(ghcb, &ctxt)) {
	case ES_OK:
		break;
	case ES_EXCEPTION:
		vc_forward_exception(&ctxt);
		fallthrough;
	default:
		return -EINVAL;
	}

	return svsm_process_result_codes(call);
}

enum es_result sev_es_ghcb_hv_call(struct ghcb *ghcb,
				   struct es_em_ctxt *ctxt,
				   u64 exit_code, u64 exit_info_1,
				   u64 exit_info_2)
{
	/* Fill in protocol and format specifiers */
	ghcb->protocol_version = ghcb_version;
	ghcb->ghcb_usage       = GHCB_DEFAULT_USAGE;

	ghcb_set_sw_exit_code(ghcb, exit_code);
	ghcb_set_sw_exit_info_1(ghcb, exit_info_1);
	ghcb_set_sw_exit_info_2(ghcb, exit_info_2);

	sev_es_wr_ghcb_msr(__pa(ghcb));
	VMGEXIT();

	return verify_exception_info(ghcb, ctxt);
}

static int __sev_cpuid_hv(u32 fn, int reg_idx, u32 *reg)
{
	u64 val;

	sev_es_wr_ghcb_msr(GHCB_CPUID_REQ(fn, reg_idx));
	VMGEXIT();
	val = sev_es_rd_ghcb_msr();
	if (GHCB_RESP_CODE(val) != GHCB_MSR_CPUID_RESP)
		return -EIO;

	*reg = (val >> 32);

	return 0;
}

static int __sev_cpuid_hv_msr(struct cpuid_leaf *leaf)
{
	int ret;

	/*
	 * MSR protocol does not support fetching non-zero subfunctions, but is
	 * sufficient to handle current early-boot cases. Should that change,
	 * make sure to report an error rather than ignoring the index and
	 * grabbing random values. If this issue arises in the future, handling
	 * can be added here to use GHCB-page protocol for cases that occur late
	 * enough in boot that GHCB page is available.
	 */
	if (cpuid_function_is_indexed(leaf->fn) && leaf->subfn)
		return -EINVAL;

	ret =         __sev_cpuid_hv(leaf->fn, GHCB_CPUID_REQ_EAX, &leaf->eax);
	ret = ret ? : __sev_cpuid_hv(leaf->fn, GHCB_CPUID_REQ_EBX, &leaf->ebx);
	ret = ret ? : __sev_cpuid_hv(leaf->fn, GHCB_CPUID_REQ_ECX, &leaf->ecx);
	ret = ret ? : __sev_cpuid_hv(leaf->fn, GHCB_CPUID_REQ_EDX, &leaf->edx);

	return ret;
}

static int __sev_cpuid_hv_ghcb(struct ghcb *ghcb, struct es_em_ctxt *ctxt, struct cpuid_leaf *leaf)
{
	u32 cr4 = native_read_cr4();
	int ret;

	ghcb_set_rax(ghcb, leaf->fn);
	ghcb_set_rcx(ghcb, leaf->subfn);

	if (cr4 & X86_CR4_OSXSAVE)
		/* Safe to read xcr0 */
		ghcb_set_xcr0(ghcb, xgetbv(XCR_XFEATURE_ENABLED_MASK));
	else
		/* xgetbv will cause #UD - use reset value for xcr0 */
		ghcb_set_xcr0(ghcb, 1);

	ret = sev_es_ghcb_hv_call(ghcb, ctxt, SVM_EXIT_CPUID, 0, 0);
	if (ret != ES_OK)
		return ret;

	if (!(ghcb_rax_is_valid(ghcb) &&
	      ghcb_rbx_is_valid(ghcb) &&
	      ghcb_rcx_is_valid(ghcb) &&
	      ghcb_rdx_is_valid(ghcb)))
		return ES_VMM_ERROR;

	leaf->eax = ghcb->save.rax;
	leaf->ebx = ghcb->save.rbx;
	leaf->ecx = ghcb->save.rcx;
	leaf->edx = ghcb->save.rdx;

	return ES_OK;
}

static int sev_cpuid_hv(struct ghcb *ghcb, struct es_em_ctxt *ctxt, struct cpuid_leaf *leaf)
{
	return ghcb ? __sev_cpuid_hv_ghcb(ghcb, ctxt, leaf)
		    : __sev_cpuid_hv_msr(leaf);
}

/*
 * This may be called early while still running on the initial identity
 * mapping. Use RIP-relative addressing to obtain the correct address
 * while running with the initial identity mapping as well as the
 * switch-over to kernel virtual addresses later.
 */
const struct snp_cpuid_table *snp_cpuid_get_table(void)
{
	return rip_rel_ptr(&cpuid_table_copy);
}

/*
 * The SNP Firmware ABI, Revision 0.9, Section 7.1, details the use of
 * XCR0_IN and XSS_IN to encode multiple versions of 0xD subfunctions 0
 * and 1 based on the corresponding features enabled by a particular
 * combination of XCR0 and XSS registers so that a guest can look up the
 * version corresponding to the features currently enabled in its XCR0/XSS
 * registers. The only values that differ between these versions/table
 * entries is the enabled XSAVE area size advertised via EBX.
 *
 * While hypervisors may choose to make use of this support, it is more
 * robust/secure for a guest to simply find the entry corresponding to the
 * base/legacy XSAVE area size (XCR0=1 or XCR0=3), and then calculate the
 * XSAVE area size using subfunctions 2 through 64, as documented in APM
 * Volume 3, Rev 3.31, Appendix E.3.8, which is what is done here.
 *
 * Since base/legacy XSAVE area size is documented as 0x240, use that value
 * directly rather than relying on the base size in the CPUID table.
 *
 * Return: XSAVE area size on success, 0 otherwise.
 */
static u32 __head snp_cpuid_calc_xsave_size(u64 xfeatures_en, bool compacted)
{
	const struct snp_cpuid_table *cpuid_table = snp_cpuid_get_table();
	u64 xfeatures_found = 0;
	u32 xsave_size = 0x240;
	int i;

	for (i = 0; i < cpuid_table->count; i++) {
		const struct snp_cpuid_fn *e = &cpuid_table->fn[i];

		if (!(e->eax_in == 0xD && e->ecx_in > 1 && e->ecx_in < 64))
			continue;
		if (!(xfeatures_en & (BIT_ULL(e->ecx_in))))
			continue;
		if (xfeatures_found & (BIT_ULL(e->ecx_in)))
			continue;

		xfeatures_found |= (BIT_ULL(e->ecx_in));

		if (compacted)
			xsave_size += e->eax;
		else
			xsave_size = max(xsave_size, e->eax + e->ebx);
	}

	/*
	 * Either the guest set unsupported XCR0/XSS bits, or the corresponding
	 * entries in the CPUID table were not present. This is not a valid
	 * state to be in.
	 */
	if (xfeatures_found != (xfeatures_en & GENMASK_ULL(63, 2)))
		return 0;

	return xsave_size;
}

static bool __head
snp_cpuid_get_validated_func(struct cpuid_leaf *leaf)
{
	const struct snp_cpuid_table *cpuid_table = snp_cpuid_get_table();
	int i;

	for (i = 0; i < cpuid_table->count; i++) {
		const struct snp_cpuid_fn *e = &cpuid_table->fn[i];

		if (e->eax_in != leaf->fn)
			continue;

		if (cpuid_function_is_indexed(leaf->fn) && e->ecx_in != leaf->subfn)
			continue;

		/*
		 * For 0xD subfunctions 0 and 1, only use the entry corresponding
		 * to the base/legacy XSAVE area size (XCR0=1 or XCR0=3, XSS=0).
		 * See the comments above snp_cpuid_calc_xsave_size() for more
		 * details.
		 */
		if (e->eax_in == 0xD && (e->ecx_in == 0 || e->ecx_in == 1))
			if (!(e->xcr0_in == 1 || e->xcr0_in == 3) || e->xss_in)
				continue;

		leaf->eax = e->eax;
		leaf->ebx = e->ebx;
		leaf->ecx = e->ecx;
		leaf->edx = e->edx;

		return true;
	}

	return false;
}

static void snp_cpuid_hv(struct ghcb *ghcb, struct es_em_ctxt *ctxt, struct cpuid_leaf *leaf)
{
	if (sev_cpuid_hv(ghcb, ctxt, leaf))
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_CPUID_HV);
}

static int __head
snp_cpuid_postprocess(struct ghcb *ghcb, struct es_em_ctxt *ctxt,
		      struct cpuid_leaf *leaf)
{
	struct cpuid_leaf leaf_hv = *leaf;

	switch (leaf->fn) {
	case 0x1:
		snp_cpuid_hv(ghcb, ctxt, &leaf_hv);

		/* initial APIC ID */
		leaf->ebx = (leaf_hv.ebx & GENMASK(31, 24)) | (leaf->ebx & GENMASK(23, 0));
		/* APIC enabled bit */
		leaf->edx = (leaf_hv.edx & BIT(9)) | (leaf->edx & ~BIT(9));

		/* OSXSAVE enabled bit */
		if (native_read_cr4() & X86_CR4_OSXSAVE)
			leaf->ecx |= BIT(27);
		break;
	case 0x7:
		/* OSPKE enabled bit */
		leaf->ecx &= ~BIT(4);
		if (native_read_cr4() & X86_CR4_PKE)
			leaf->ecx |= BIT(4);
		break;
	case 0xB:
		leaf_hv.subfn = 0;
		snp_cpuid_hv(ghcb, ctxt, &leaf_hv);

		/* extended APIC ID */
		leaf->edx = leaf_hv.edx;
		break;
	case 0xD: {
		bool compacted = false;
		u64 xcr0 = 1, xss = 0;
		u32 xsave_size;

		if (leaf->subfn != 0 && leaf->subfn != 1)
			return 0;

		if (native_read_cr4() & X86_CR4_OSXSAVE)
			xcr0 = xgetbv(XCR_XFEATURE_ENABLED_MASK);
		if (leaf->subfn == 1) {
			/* Get XSS value if XSAVES is enabled. */
			if (leaf->eax & BIT(3)) {
				unsigned long lo, hi;

				asm volatile("rdmsr" : "=a" (lo), "=d" (hi)
						     : "c" (MSR_IA32_XSS));
				xss = (hi << 32) | lo;
			}

			/*
			 * The PPR and APM aren't clear on what size should be
			 * encoded in 0xD:0x1:EBX when compaction is not enabled
			 * by either XSAVEC (feature bit 1) or XSAVES (feature
			 * bit 3) since SNP-capable hardware has these feature
			 * bits fixed as 1. KVM sets it to 0 in this case, but
			 * to avoid this becoming an issue it's safer to simply
			 * treat this as unsupported for SNP guests.
			 */
			if (!(leaf->eax & (BIT(1) | BIT(3))))
				return -EINVAL;

			compacted = true;
		}

		xsave_size = snp_cpuid_calc_xsave_size(xcr0 | xss, compacted);
		if (!xsave_size)
			return -EINVAL;

		leaf->ebx = xsave_size;
		}
		break;
	case 0x8000001E:
		snp_cpuid_hv(ghcb, ctxt, &leaf_hv);

		/* extended APIC ID */
		leaf->eax = leaf_hv.eax;
		/* compute ID */
		leaf->ebx = (leaf->ebx & GENMASK(31, 8)) | (leaf_hv.ebx & GENMASK(7, 0));
		/* node ID */
		leaf->ecx = (leaf->ecx & GENMASK(31, 8)) | (leaf_hv.ecx & GENMASK(7, 0));
		break;
	default:
		/* No fix-ups needed, use values as-is. */
		break;
	}

	return 0;
}

/*
 * Returns -EOPNOTSUPP if feature not enabled. Any other non-zero return value
 * should be treated as fatal by caller.
 */
int __head
snp_cpuid(struct ghcb *ghcb, struct es_em_ctxt *ctxt, struct cpuid_leaf *leaf)
{
	const struct snp_cpuid_table *cpuid_table = snp_cpuid_get_table();

	if (!cpuid_table->count)
		return -EOPNOTSUPP;

	if (!snp_cpuid_get_validated_func(leaf)) {
		/*
		 * Some hypervisors will avoid keeping track of CPUID entries
		 * where all values are zero, since they can be handled the
		 * same as out-of-range values (all-zero). This is useful here
		 * as well as it allows virtually all guest configurations to
		 * work using a single SNP CPUID table.
		 *
		 * To allow for this, there is a need to distinguish between
		 * out-of-range entries and in-range zero entries, since the
		 * CPUID table entries are only a template that may need to be
		 * augmented with additional values for things like
		 * CPU-specific information during post-processing. So if it's
		 * not in the table, set the values to zero. Then, if they are
		 * within a valid CPUID range, proceed with post-processing
		 * using zeros as the initial values. Otherwise, skip
		 * post-processing and just return zeros immediately.
		 */
		leaf->eax = leaf->ebx = leaf->ecx = leaf->edx = 0;

		/* Skip post-processing for out-of-range zero leafs. */
		if (!(leaf->fn <= cpuid_std_range_max ||
		      (leaf->fn >= 0x40000000 && leaf->fn <= cpuid_hyp_range_max) ||
		      (leaf->fn >= 0x80000000 && leaf->fn <= cpuid_ext_range_max)))
			return 0;
	}

	return snp_cpuid_postprocess(ghcb, ctxt, leaf);
}

/*
 * Boot VC Handler - This is the first VC handler during boot, there is no GHCB
 * page yet, so it only supports the MSR based communication with the
 * hypervisor and only the CPUID exit-code.
 */
void __head do_vc_no_ghcb(struct pt_regs *regs, unsigned long exit_code)
{
	unsigned int subfn = lower_bits(regs->cx, 32);
	unsigned int fn = lower_bits(regs->ax, 32);
	u16 opcode = *(unsigned short *)regs->ip;
	struct cpuid_leaf leaf;
	int ret;

	/* Only CPUID is supported via MSR protocol */
	if (exit_code != SVM_EXIT_CPUID)
		goto fail;

	/* Is it really a CPUID insn? */
	if (opcode != 0xa20f)
		goto fail;

	leaf.fn = fn;
	leaf.subfn = subfn;

	ret = snp_cpuid(NULL, NULL, &leaf);
	if (!ret)
		goto cpuid_done;

	if (ret != -EOPNOTSUPP)
		goto fail;

	if (__sev_cpuid_hv_msr(&leaf))
		goto fail;

cpuid_done:
	regs->ax = leaf.eax;
	regs->bx = leaf.ebx;
	regs->cx = leaf.ecx;
	regs->dx = leaf.edx;

	/*
	 * This is a VC handler and the #VC is only raised when SEV-ES is
	 * active, which means SEV must be active too. Do sanity checks on the
	 * CPUID results to make sure the hypervisor does not trick the kernel
	 * into the no-sev path. This could map sensitive data unencrypted and
	 * make it accessible to the hypervisor.
	 *
	 * In particular, check for:
	 *	- Availability of CPUID leaf 0x8000001f
	 *	- SEV CPUID bit.
	 *
	 * The hypervisor might still report the wrong C-bit position, but this
	 * can't be checked here.
	 */

	if (fn == 0x80000000 && (regs->ax < 0x8000001f))
		/* SEV leaf check */
		goto fail;
	else if ((fn == 0x8000001f && !(regs->ax & BIT(1))))
		/* SEV bit */
		goto fail;

	/* Skip over the CPUID two-byte opcode */
	regs->ip += 2;

	return;

fail:
	/* Terminate the guest */
	sev_es_terminate(SEV_TERM_SET_GEN, GHCB_SEV_ES_GEN_REQ);
}

struct cc_setup_data {
	struct setup_data header;
	u32 cc_blob_address;
};

/*
 * Search for a Confidential Computing blob passed in as a setup_data entry
 * via the Linux Boot Protocol.
 */
static __head
struct cc_blob_sev_info *find_cc_blob_setup_data(struct boot_params *bp)
{
	struct cc_setup_data *sd = NULL;
	struct setup_data *hdr;

	hdr = (struct setup_data *)bp->hdr.setup_data;

	while (hdr) {
		if (hdr->type == SETUP_CC_BLOB) {
			sd = (struct cc_setup_data *)hdr;
			return (struct cc_blob_sev_info *)(unsigned long)sd->cc_blob_address;
		}
		hdr = (struct setup_data *)hdr->next;
	}

	return NULL;
}

/*
 * Initialize the kernel's copy of the SNP CPUID table, and set up the
 * pointer that will be used to access it.
 *
 * Maintaining a direct mapping of the SNP CPUID table used by firmware would
 * be possible as an alternative, but the approach is brittle since the
 * mapping needs to be updated in sync with all the changes to virtual memory
 * layout and related mapping facilities throughout the boot process.
 */
static void __head setup_cpuid_table(const struct cc_blob_sev_info *cc_info)
{
	const struct snp_cpuid_table *cpuid_table_fw, *cpuid_table;
	int i;

	if (!cc_info || !cc_info->cpuid_phys || cc_info->cpuid_len < PAGE_SIZE)
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_CPUID);

	cpuid_table_fw = (const struct snp_cpuid_table *)cc_info->cpuid_phys;
	if (!cpuid_table_fw->count || cpuid_table_fw->count > SNP_CPUID_COUNT_MAX)
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_CPUID);

	cpuid_table = snp_cpuid_get_table();
	memcpy((void *)cpuid_table, cpuid_table_fw, sizeof(*cpuid_table));

	/* Initialize CPUID ranges for range-checking. */
	for (i = 0; i < cpuid_table->count; i++) {
		const struct snp_cpuid_fn *fn = &cpuid_table->fn[i];

		if (fn->eax_in == 0x0)
			cpuid_std_range_max = fn->eax;
		else if (fn->eax_in == 0x40000000)
			cpuid_hyp_range_max = fn->eax;
		else if (fn->eax_in == 0x80000000)
			cpuid_ext_range_max = fn->eax;
	}
}

static void __head svsm_pval_4k_page(unsigned long paddr, bool validate)
{
	struct svsm_pvalidate_call *pc;
	struct svsm_call call = {};
	unsigned long flags;
	u64 pc_pa;
	int ret;

	/*
	 * This can be called very early in the boot, use native functions in
	 * order to avoid paravirt issues.
	 */
	flags = native_local_irq_save();

	call.caa = svsm_get_caa();

	pc = (struct svsm_pvalidate_call *)call.caa->svsm_buffer;
	pc_pa = svsm_get_caa_pa() + offsetof(struct svsm_ca, svsm_buffer);

	pc->num_entries = 1;
	pc->cur_index   = 0;
	pc->entry[0].page_size = RMP_PG_SIZE_4K;
	pc->entry[0].action    = validate;
	pc->entry[0].ignore_cf = 0;
	pc->entry[0].pfn       = paddr >> PAGE_SHIFT;

	/* Protocol 0, Call ID 1 */
	call.rax = SVSM_CORE_CALL(SVSM_CORE_PVALIDATE);
	call.rcx = pc_pa;

	ret = svsm_perform_call_protocol(&call);
	if (ret)
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_PVALIDATE);

	native_local_irq_restore(flags);
}

static void __head pvalidate_4k_page(unsigned long vaddr, unsigned long paddr,
				     bool validate)
{
	int ret;

	if (snp_vmpl) {
		svsm_pval_4k_page(paddr, validate);
	} else {
		ret = pvalidate(vaddr, RMP_PG_SIZE_4K, validate);
		if (ret)
			sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_PVALIDATE);
	}
}

/*
 * Maintain the GPA of the SVSM Calling Area (CA) in order to utilize the SVSM
 * services needed when not running in VMPL0.
 */
static bool __head svsm_setup_ca(const struct cc_blob_sev_info *cc_info)
{
	struct snp_secrets_page *secrets_page;
	struct snp_cpuid_table *cpuid_table;
	unsigned int i;
	u64 caa;

	BUILD_BUG_ON(sizeof(*secrets_page) != PAGE_SIZE);

	/*
	 * Check if running at VMPL0.
	 *
	 * Use RMPADJUST (see the rmpadjust() function for a description of what
	 * the instruction does) to update the VMPL1 permissions of a page. If
	 * the guest is running at VMPL0, this will succeed and implies there is
	 * no SVSM. If the guest is running at any other VMPL, this will fail.
	 * Linux SNP guests only ever run at a single VMPL level so permission mask
	 * changes of a lesser-privileged VMPL are a don't-care.
	 *
	 * Use a rip-relative reference to obtain the proper address, since this
	 * routine is running identity mapped when called, both by the decompressor
	 * code and the early kernel code.
	 */
	if (!rmpadjust((unsigned long)rip_rel_ptr(&boot_ghcb_page), RMP_PG_SIZE_4K, 1))
		return false;

	/*
	 * Not running at VMPL0, ensure everything has been properly supplied
	 * for running under an SVSM.
	 */
	if (!cc_info || !cc_info->secrets_phys || cc_info->secrets_len != PAGE_SIZE)
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_SECRETS_PAGE);

	secrets_page = (struct snp_secrets_page *)cc_info->secrets_phys;
	if (!secrets_page->svsm_size)
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_NO_SVSM);

	if (!secrets_page->svsm_guest_vmpl)
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_SVSM_VMPL0);

	snp_vmpl = secrets_page->svsm_guest_vmpl;

	caa = secrets_page->svsm_caa;

	/*
	 * An open-coded PAGE_ALIGNED() in order to avoid including
	 * kernel-proper headers into the decompressor.
	 */
	if (caa & (PAGE_SIZE - 1))
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_SVSM_CAA);

	/*
	 * The CA is identity mapped when this routine is called, both by the
	 * decompressor code and the early kernel code.
	 */
	boot_svsm_caa = (struct svsm_ca *)caa;
	boot_svsm_caa_pa = caa;

	/* Advertise the SVSM presence via CPUID. */
	cpuid_table = (struct snp_cpuid_table *)snp_cpuid_get_table();
	for (i = 0; i < cpuid_table->count; i++) {
		struct snp_cpuid_fn *fn = &cpuid_table->fn[i];

		if (fn->eax_in == 0x8000001f)
			fn->eax |= BIT(28);
	}

	return true;
}
