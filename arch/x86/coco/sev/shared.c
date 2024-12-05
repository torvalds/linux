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
#define sev_printk(fmt, ...)		printk(fmt, ##__VA_ARGS__)
#define sev_printk_rtl(fmt, ...)	printk_ratelimited(fmt, ##__VA_ARGS__)
#else
#undef WARN
#define WARN(condition, format...) (!!(condition))
#define sev_printk(fmt, ...)
#define sev_printk_rtl(fmt, ...)
#undef vc_forward_exception
#define vc_forward_exception(c)		panic("SNP: Hypervisor requested exception\n")
#endif

/*
 * SVSM related information:
 *   When running under an SVSM, the VMPL that Linux is executing at must be
 *   non-zero. The VMPL is therefore used to indicate the presence of an SVSM.
 *
 *   During boot, the page tables are set up as identity mapped and later
 *   changed to use kernel virtual addresses. Maintain separate virtual and
 *   physical addresses for the CAA to allow SVSM functions to be used during
 *   early boot, both with identity mapped virtual addresses and proper kernel
 *   virtual addresses.
 */
u8 snp_vmpl __ro_after_init;
EXPORT_SYMBOL_GPL(snp_vmpl);
static struct svsm_ca *boot_svsm_caa __ro_after_init;
static u64 boot_svsm_caa_pa __ro_after_init;

static struct svsm_ca *svsm_get_caa(void);
static u64 svsm_get_caa_pa(void);
static int svsm_perform_call_protocol(struct svsm_call *call);

/* I/O parameters for CPUID-related helpers */
struct cpuid_leaf {
	u32 fn;
	u32 subfn;
	u32 eax;
	u32 ebx;
	u32 ecx;
	u32 edx;
};

/*
 * Individual entries of the SNP CPUID table, as defined by the SNP
 * Firmware ABI, Revision 0.9, Section 7.1, Table 14.
 */
struct snp_cpuid_fn {
	u32 eax_in;
	u32 ecx_in;
	u64 xcr0_in;
	u64 xss_in;
	u32 eax;
	u32 ebx;
	u32 ecx;
	u32 edx;
	u64 __reserved;
} __packed;

/*
 * SNP CPUID table, as defined by the SNP Firmware ABI, Revision 0.9,
 * Section 8.14.2.6. Also noted there is the SNP firmware-enforced limit
 * of 64 entries per CPUID table.
 */
#define SNP_CPUID_COUNT_MAX 64

struct snp_cpuid_table {
	u32 count;
	u32 __reserved1;
	u64 __reserved2;
	struct snp_cpuid_fn fn[SNP_CPUID_COUNT_MAX];
} __packed;

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

static bool __init sev_es_check_cpu_features(void)
{
	if (!has_cpuflag(X86_FEATURE_RDRAND)) {
		error("RDRAND instruction not supported - no trusted source of randomness available\n");
		return false;
	}

	return true;
}

static void __head __noreturn
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
static u64 get_hv_features(void)
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

static void snp_register_ghcb_early(unsigned long paddr)
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

static bool sev_es_negotiate_protocol(void)
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

static __always_inline void vc_ghcb_invalidate(struct ghcb *ghcb)
{
	ghcb->save.sw_exit_code = 0;
	__builtin_memset(ghcb->save.valid_bitmap, 0, sizeof(ghcb->save.valid_bitmap));
}

static bool vc_decoding_needed(unsigned long exit_code)
{
	/* Exceptions don't require to decode the instruction */
	return !(exit_code >= SVM_EXIT_EXCP_BASE &&
		 exit_code <= SVM_EXIT_LAST_EXCP);
}

static enum es_result vc_init_em_ctxt(struct es_em_ctxt *ctxt,
				      struct pt_regs *regs,
				      unsigned long exit_code)
{
	enum es_result ret = ES_OK;

	memset(ctxt, 0, sizeof(*ctxt));
	ctxt->regs = regs;

	if (vc_decoding_needed(exit_code))
		ret = vc_decode_insn(ctxt);

	return ret;
}

static void vc_finish_insn(struct es_em_ctxt *ctxt)
{
	ctxt->regs->ip += ctxt->insn.length;
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
	ghcb->protocol_version = RIP_REL_REF(ghcb_version);
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

static enum es_result sev_es_ghcb_hv_call(struct ghcb *ghcb,
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
static const struct snp_cpuid_table *snp_cpuid_get_table(void)
{
	return &RIP_REL_REF(cpuid_table_copy);
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
static int __head
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
		if (!(leaf->fn <= RIP_REL_REF(cpuid_std_range_max) ||
		      (leaf->fn >= 0x40000000 && leaf->fn <= RIP_REL_REF(cpuid_hyp_range_max)) ||
		      (leaf->fn >= 0x80000000 && leaf->fn <= RIP_REL_REF(cpuid_ext_range_max))))
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

static enum es_result vc_insn_string_check(struct es_em_ctxt *ctxt,
					   unsigned long address,
					   bool write)
{
	if (user_mode(ctxt->regs) && fault_in_kernel_space(address)) {
		ctxt->fi.vector     = X86_TRAP_PF;
		ctxt->fi.error_code = X86_PF_USER;
		ctxt->fi.cr2        = address;
		if (write)
			ctxt->fi.error_code |= X86_PF_WRITE;

		return ES_EXCEPTION;
	}

	return ES_OK;
}

static enum es_result vc_insn_string_read(struct es_em_ctxt *ctxt,
					  void *src, char *buf,
					  unsigned int data_size,
					  unsigned int count,
					  bool backwards)
{
	int i, b = backwards ? -1 : 1;
	unsigned long address = (unsigned long)src;
	enum es_result ret;

	ret = vc_insn_string_check(ctxt, address, false);
	if (ret != ES_OK)
		return ret;

	for (i = 0; i < count; i++) {
		void *s = src + (i * data_size * b);
		char *d = buf + (i * data_size);

		ret = vc_read_mem(ctxt, s, d, data_size);
		if (ret != ES_OK)
			break;
	}

	return ret;
}

static enum es_result vc_insn_string_write(struct es_em_ctxt *ctxt,
					   void *dst, char *buf,
					   unsigned int data_size,
					   unsigned int count,
					   bool backwards)
{
	int i, s = backwards ? -1 : 1;
	unsigned long address = (unsigned long)dst;
	enum es_result ret;

	ret = vc_insn_string_check(ctxt, address, true);
	if (ret != ES_OK)
		return ret;

	for (i = 0; i < count; i++) {
		void *d = dst + (i * data_size * s);
		char *b = buf + (i * data_size);

		ret = vc_write_mem(ctxt, d, b, data_size);
		if (ret != ES_OK)
			break;
	}

	return ret;
}

#define IOIO_TYPE_STR  BIT(2)
#define IOIO_TYPE_IN   1
#define IOIO_TYPE_INS  (IOIO_TYPE_IN | IOIO_TYPE_STR)
#define IOIO_TYPE_OUT  0
#define IOIO_TYPE_OUTS (IOIO_TYPE_OUT | IOIO_TYPE_STR)

#define IOIO_REP       BIT(3)

#define IOIO_ADDR_64   BIT(9)
#define IOIO_ADDR_32   BIT(8)
#define IOIO_ADDR_16   BIT(7)

#define IOIO_DATA_32   BIT(6)
#define IOIO_DATA_16   BIT(5)
#define IOIO_DATA_8    BIT(4)

#define IOIO_SEG_ES    (0 << 10)
#define IOIO_SEG_DS    (3 << 10)

static enum es_result vc_ioio_exitinfo(struct es_em_ctxt *ctxt, u64 *exitinfo)
{
	struct insn *insn = &ctxt->insn;
	size_t size;
	u64 port;

	*exitinfo = 0;

	switch (insn->opcode.bytes[0]) {
	/* INS opcodes */
	case 0x6c:
	case 0x6d:
		*exitinfo |= IOIO_TYPE_INS;
		*exitinfo |= IOIO_SEG_ES;
		port	   = ctxt->regs->dx & 0xffff;
		break;

	/* OUTS opcodes */
	case 0x6e:
	case 0x6f:
		*exitinfo |= IOIO_TYPE_OUTS;
		*exitinfo |= IOIO_SEG_DS;
		port	   = ctxt->regs->dx & 0xffff;
		break;

	/* IN immediate opcodes */
	case 0xe4:
	case 0xe5:
		*exitinfo |= IOIO_TYPE_IN;
		port	   = (u8)insn->immediate.value & 0xffff;
		break;

	/* OUT immediate opcodes */
	case 0xe6:
	case 0xe7:
		*exitinfo |= IOIO_TYPE_OUT;
		port	   = (u8)insn->immediate.value & 0xffff;
		break;

	/* IN register opcodes */
	case 0xec:
	case 0xed:
		*exitinfo |= IOIO_TYPE_IN;
		port	   = ctxt->regs->dx & 0xffff;
		break;

	/* OUT register opcodes */
	case 0xee:
	case 0xef:
		*exitinfo |= IOIO_TYPE_OUT;
		port	   = ctxt->regs->dx & 0xffff;
		break;

	default:
		return ES_DECODE_FAILED;
	}

	*exitinfo |= port << 16;

	switch (insn->opcode.bytes[0]) {
	case 0x6c:
	case 0x6e:
	case 0xe4:
	case 0xe6:
	case 0xec:
	case 0xee:
		/* Single byte opcodes */
		*exitinfo |= IOIO_DATA_8;
		size       = 1;
		break;
	default:
		/* Length determined by instruction parsing */
		*exitinfo |= (insn->opnd_bytes == 2) ? IOIO_DATA_16
						     : IOIO_DATA_32;
		size       = (insn->opnd_bytes == 2) ? 2 : 4;
	}

	switch (insn->addr_bytes) {
	case 2:
		*exitinfo |= IOIO_ADDR_16;
		break;
	case 4:
		*exitinfo |= IOIO_ADDR_32;
		break;
	case 8:
		*exitinfo |= IOIO_ADDR_64;
		break;
	}

	if (insn_has_rep_prefix(insn))
		*exitinfo |= IOIO_REP;

	return vc_ioio_check(ctxt, (u16)port, size);
}

static enum es_result vc_handle_ioio(struct ghcb *ghcb, struct es_em_ctxt *ctxt)
{
	struct pt_regs *regs = ctxt->regs;
	u64 exit_info_1, exit_info_2;
	enum es_result ret;

	ret = vc_ioio_exitinfo(ctxt, &exit_info_1);
	if (ret != ES_OK)
		return ret;

	if (exit_info_1 & IOIO_TYPE_STR) {

		/* (REP) INS/OUTS */

		bool df = ((regs->flags & X86_EFLAGS_DF) == X86_EFLAGS_DF);
		unsigned int io_bytes, exit_bytes;
		unsigned int ghcb_count, op_count;
		unsigned long es_base;
		u64 sw_scratch;

		/*
		 * For the string variants with rep prefix the amount of in/out
		 * operations per #VC exception is limited so that the kernel
		 * has a chance to take interrupts and re-schedule while the
		 * instruction is emulated.
		 */
		io_bytes   = (exit_info_1 >> 4) & 0x7;
		ghcb_count = sizeof(ghcb->shared_buffer) / io_bytes;

		op_count    = (exit_info_1 & IOIO_REP) ? regs->cx : 1;
		exit_info_2 = min(op_count, ghcb_count);
		exit_bytes  = exit_info_2 * io_bytes;

		es_base = insn_get_seg_base(ctxt->regs, INAT_SEG_REG_ES);

		/* Read bytes of OUTS into the shared buffer */
		if (!(exit_info_1 & IOIO_TYPE_IN)) {
			ret = vc_insn_string_read(ctxt,
					       (void *)(es_base + regs->si),
					       ghcb->shared_buffer, io_bytes,
					       exit_info_2, df);
			if (ret)
				return ret;
		}

		/*
		 * Issue an VMGEXIT to the HV to consume the bytes from the
		 * shared buffer or to have it write them into the shared buffer
		 * depending on the instruction: OUTS or INS.
		 */
		sw_scratch = __pa(ghcb) + offsetof(struct ghcb, shared_buffer);
		ghcb_set_sw_scratch(ghcb, sw_scratch);
		ret = sev_es_ghcb_hv_call(ghcb, ctxt, SVM_EXIT_IOIO,
					  exit_info_1, exit_info_2);
		if (ret != ES_OK)
			return ret;

		/* Read bytes from shared buffer into the guest's destination. */
		if (exit_info_1 & IOIO_TYPE_IN) {
			ret = vc_insn_string_write(ctxt,
						   (void *)(es_base + regs->di),
						   ghcb->shared_buffer, io_bytes,
						   exit_info_2, df);
			if (ret)
				return ret;

			if (df)
				regs->di -= exit_bytes;
			else
				regs->di += exit_bytes;
		} else {
			if (df)
				regs->si -= exit_bytes;
			else
				regs->si += exit_bytes;
		}

		if (exit_info_1 & IOIO_REP)
			regs->cx -= exit_info_2;

		ret = regs->cx ? ES_RETRY : ES_OK;

	} else {

		/* IN/OUT into/from rAX */

		int bits = (exit_info_1 & 0x70) >> 1;
		u64 rax = 0;

		if (!(exit_info_1 & IOIO_TYPE_IN))
			rax = lower_bits(regs->ax, bits);

		ghcb_set_rax(ghcb, rax);

		ret = sev_es_ghcb_hv_call(ghcb, ctxt, SVM_EXIT_IOIO, exit_info_1, 0);
		if (ret != ES_OK)
			return ret;

		if (exit_info_1 & IOIO_TYPE_IN) {
			if (!ghcb_rax_is_valid(ghcb))
				return ES_VMM_ERROR;
			regs->ax = lower_bits(ghcb->save.rax, bits);
		}
	}

	return ret;
}

static int vc_handle_cpuid_snp(struct ghcb *ghcb, struct es_em_ctxt *ctxt)
{
	struct pt_regs *regs = ctxt->regs;
	struct cpuid_leaf leaf;
	int ret;

	leaf.fn = regs->ax;
	leaf.subfn = regs->cx;
	ret = snp_cpuid(ghcb, ctxt, &leaf);
	if (!ret) {
		regs->ax = leaf.eax;
		regs->bx = leaf.ebx;
		regs->cx = leaf.ecx;
		regs->dx = leaf.edx;
	}

	return ret;
}

static enum es_result vc_handle_cpuid(struct ghcb *ghcb,
				      struct es_em_ctxt *ctxt)
{
	struct pt_regs *regs = ctxt->regs;
	u32 cr4 = native_read_cr4();
	enum es_result ret;
	int snp_cpuid_ret;

	snp_cpuid_ret = vc_handle_cpuid_snp(ghcb, ctxt);
	if (!snp_cpuid_ret)
		return ES_OK;
	if (snp_cpuid_ret != -EOPNOTSUPP)
		return ES_VMM_ERROR;

	ghcb_set_rax(ghcb, regs->ax);
	ghcb_set_rcx(ghcb, regs->cx);

	if (cr4 & X86_CR4_OSXSAVE)
		/* Safe to read xcr0 */
		ghcb_set_xcr0(ghcb, xgetbv(XCR_XFEATURE_ENABLED_MASK));
	else
		/* xgetbv will cause #GP - use reset value for xcr0 */
		ghcb_set_xcr0(ghcb, 1);

	ret = sev_es_ghcb_hv_call(ghcb, ctxt, SVM_EXIT_CPUID, 0, 0);
	if (ret != ES_OK)
		return ret;

	if (!(ghcb_rax_is_valid(ghcb) &&
	      ghcb_rbx_is_valid(ghcb) &&
	      ghcb_rcx_is_valid(ghcb) &&
	      ghcb_rdx_is_valid(ghcb)))
		return ES_VMM_ERROR;

	regs->ax = ghcb->save.rax;
	regs->bx = ghcb->save.rbx;
	regs->cx = ghcb->save.rcx;
	regs->dx = ghcb->save.rdx;

	return ES_OK;
}

static enum es_result vc_handle_rdtsc(struct ghcb *ghcb,
				      struct es_em_ctxt *ctxt,
				      unsigned long exit_code)
{
	bool rdtscp = (exit_code == SVM_EXIT_RDTSCP);
	enum es_result ret;

	ret = sev_es_ghcb_hv_call(ghcb, ctxt, exit_code, 0, 0);
	if (ret != ES_OK)
		return ret;

	if (!(ghcb_rax_is_valid(ghcb) && ghcb_rdx_is_valid(ghcb) &&
	     (!rdtscp || ghcb_rcx_is_valid(ghcb))))
		return ES_VMM_ERROR;

	ctxt->regs->ax = ghcb->save.rax;
	ctxt->regs->dx = ghcb->save.rdx;
	if (rdtscp)
		ctxt->regs->cx = ghcb->save.rcx;

	return ES_OK;
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
			RIP_REL_REF(cpuid_std_range_max) = fn->eax;
		else if (fn->eax_in == 0x40000000)
			RIP_REL_REF(cpuid_hyp_range_max) = fn->eax;
		else if (fn->eax_in == 0x80000000)
			RIP_REL_REF(cpuid_ext_range_max) = fn->eax;
	}
}

static inline void __pval_terminate(u64 pfn, bool action, unsigned int page_size,
				    int ret, u64 svsm_ret)
{
	WARN(1, "PVALIDATE failure: pfn: 0x%llx, action: %u, size: %u, ret: %d, svsm_ret: 0x%llx\n",
	     pfn, action, page_size, ret, svsm_ret);

	sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_PVALIDATE);
}

static void svsm_pval_terminate(struct svsm_pvalidate_call *pc, int ret, u64 svsm_ret)
{
	unsigned int page_size;
	bool action;
	u64 pfn;

	pfn = pc->entry[pc->cur_index].pfn;
	action = pc->entry[pc->cur_index].action;
	page_size = pc->entry[pc->cur_index].page_size;

	__pval_terminate(pfn, action, page_size, ret, svsm_ret);
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

	/*
	 * This can be called very early during boot, so use rIP-relative
	 * references as needed.
	 */
	if (RIP_REL_REF(snp_vmpl)) {
		svsm_pval_4k_page(paddr, validate);
	} else {
		ret = pvalidate(vaddr, RMP_PG_SIZE_4K, validate);
		if (ret)
			sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_PVALIDATE);
	}
}

static void pval_pages(struct snp_psc_desc *desc)
{
	struct psc_entry *e;
	unsigned long vaddr;
	unsigned int size;
	unsigned int i;
	bool validate;
	u64 pfn;
	int rc;

	for (i = 0; i <= desc->hdr.end_entry; i++) {
		e = &desc->entries[i];

		pfn = e->gfn;
		vaddr = (unsigned long)pfn_to_kaddr(pfn);
		size = e->pagesize ? RMP_PG_SIZE_2M : RMP_PG_SIZE_4K;
		validate = e->operation == SNP_PAGE_STATE_PRIVATE;

		rc = pvalidate(vaddr, size, validate);
		if (!rc)
			continue;

		if (rc == PVALIDATE_FAIL_SIZEMISMATCH && size == RMP_PG_SIZE_2M) {
			unsigned long vaddr_end = vaddr + PMD_SIZE;

			for (; vaddr < vaddr_end; vaddr += PAGE_SIZE, pfn++) {
				rc = pvalidate(vaddr, RMP_PG_SIZE_4K, validate);
				if (rc)
					__pval_terminate(pfn, validate, RMP_PG_SIZE_4K, rc, 0);
			}
		} else {
			__pval_terminate(pfn, validate, size, rc, 0);
		}
	}
}

static u64 svsm_build_ca_from_pfn_range(u64 pfn, u64 pfn_end, bool action,
					struct svsm_pvalidate_call *pc)
{
	struct svsm_pvalidate_entry *pe;

	/* Nothing in the CA yet */
	pc->num_entries = 0;
	pc->cur_index   = 0;

	pe = &pc->entry[0];

	while (pfn < pfn_end) {
		pe->page_size = RMP_PG_SIZE_4K;
		pe->action    = action;
		pe->ignore_cf = 0;
		pe->pfn       = pfn;

		pe++;
		pfn++;

		pc->num_entries++;
		if (pc->num_entries == SVSM_PVALIDATE_MAX_COUNT)
			break;
	}

	return pfn;
}

static int svsm_build_ca_from_psc_desc(struct snp_psc_desc *desc, unsigned int desc_entry,
				       struct svsm_pvalidate_call *pc)
{
	struct svsm_pvalidate_entry *pe;
	struct psc_entry *e;

	/* Nothing in the CA yet */
	pc->num_entries = 0;
	pc->cur_index   = 0;

	pe = &pc->entry[0];
	e  = &desc->entries[desc_entry];

	while (desc_entry <= desc->hdr.end_entry) {
		pe->page_size = e->pagesize ? RMP_PG_SIZE_2M : RMP_PG_SIZE_4K;
		pe->action    = e->operation == SNP_PAGE_STATE_PRIVATE;
		pe->ignore_cf = 0;
		pe->pfn       = e->gfn;

		pe++;
		e++;

		desc_entry++;
		pc->num_entries++;
		if (pc->num_entries == SVSM_PVALIDATE_MAX_COUNT)
			break;
	}

	return desc_entry;
}

static void svsm_pval_pages(struct snp_psc_desc *desc)
{
	struct svsm_pvalidate_entry pv_4k[VMGEXIT_PSC_MAX_ENTRY];
	unsigned int i, pv_4k_count = 0;
	struct svsm_pvalidate_call *pc;
	struct svsm_call call = {};
	unsigned long flags;
	bool action;
	u64 pc_pa;
	int ret;

	/*
	 * This can be called very early in the boot, use native functions in
	 * order to avoid paravirt issues.
	 */
	flags = native_local_irq_save();

	/*
	 * The SVSM calling area (CA) can support processing 510 entries at a
	 * time. Loop through the Page State Change descriptor until the CA is
	 * full or the last entry in the descriptor is reached, at which time
	 * the SVSM is invoked. This repeats until all entries in the descriptor
	 * are processed.
	 */
	call.caa = svsm_get_caa();

	pc = (struct svsm_pvalidate_call *)call.caa->svsm_buffer;
	pc_pa = svsm_get_caa_pa() + offsetof(struct svsm_ca, svsm_buffer);

	/* Protocol 0, Call ID 1 */
	call.rax = SVSM_CORE_CALL(SVSM_CORE_PVALIDATE);
	call.rcx = pc_pa;

	for (i = 0; i <= desc->hdr.end_entry;) {
		i = svsm_build_ca_from_psc_desc(desc, i, pc);

		do {
			ret = svsm_perform_call_protocol(&call);
			if (!ret)
				continue;

			/*
			 * Check if the entry failed because of an RMP mismatch (a
			 * PVALIDATE at 2M was requested, but the page is mapped in
			 * the RMP as 4K).
			 */

			if (call.rax_out == SVSM_PVALIDATE_FAIL_SIZEMISMATCH &&
			    pc->entry[pc->cur_index].page_size == RMP_PG_SIZE_2M) {
				/* Save this entry for post-processing at 4K */
				pv_4k[pv_4k_count++] = pc->entry[pc->cur_index];

				/* Skip to the next one unless at the end of the list */
				pc->cur_index++;
				if (pc->cur_index < pc->num_entries)
					ret = -EAGAIN;
				else
					ret = 0;
			}
		} while (ret == -EAGAIN);

		if (ret)
			svsm_pval_terminate(pc, ret, call.rax_out);
	}

	/* Process any entries that failed to be validated at 2M and validate them at 4K */
	for (i = 0; i < pv_4k_count; i++) {
		u64 pfn, pfn_end;

		action  = pv_4k[i].action;
		pfn     = pv_4k[i].pfn;
		pfn_end = pfn + 512;

		while (pfn < pfn_end) {
			pfn = svsm_build_ca_from_pfn_range(pfn, pfn_end, action, pc);

			ret = svsm_perform_call_protocol(&call);
			if (ret)
				svsm_pval_terminate(pc, ret, call.rax_out);
		}
	}

	native_local_irq_restore(flags);
}

static void pvalidate_pages(struct snp_psc_desc *desc)
{
	if (snp_vmpl)
		svsm_pval_pages(desc);
	else
		pval_pages(desc);
}

static int vmgexit_psc(struct ghcb *ghcb, struct snp_psc_desc *desc)
{
	int cur_entry, end_entry, ret = 0;
	struct snp_psc_desc *data;
	struct es_em_ctxt ctxt;

	vc_ghcb_invalidate(ghcb);

	/* Copy the input desc into GHCB shared buffer */
	data = (struct snp_psc_desc *)ghcb->shared_buffer;
	memcpy(ghcb->shared_buffer, desc, min_t(int, GHCB_SHARED_BUF_SIZE, sizeof(*desc)));

	/*
	 * As per the GHCB specification, the hypervisor can resume the guest
	 * before processing all the entries. Check whether all the entries
	 * are processed. If not, then keep retrying. Note, the hypervisor
	 * will update the data memory directly to indicate the status, so
	 * reference the data->hdr everywhere.
	 *
	 * The strategy here is to wait for the hypervisor to change the page
	 * state in the RMP table before guest accesses the memory pages. If the
	 * page state change was not successful, then later memory access will
	 * result in a crash.
	 */
	cur_entry = data->hdr.cur_entry;
	end_entry = data->hdr.end_entry;

	while (data->hdr.cur_entry <= data->hdr.end_entry) {
		ghcb_set_sw_scratch(ghcb, (u64)__pa(data));

		/* This will advance the shared buffer data points to. */
		ret = sev_es_ghcb_hv_call(ghcb, &ctxt, SVM_VMGEXIT_PSC, 0, 0);

		/*
		 * Page State Change VMGEXIT can pass error code through
		 * exit_info_2.
		 */
		if (WARN(ret || ghcb->save.sw_exit_info_2,
			 "SNP: PSC failed ret=%d exit_info_2=%llx\n",
			 ret, ghcb->save.sw_exit_info_2)) {
			ret = 1;
			goto out;
		}

		/* Verify that reserved bit is not set */
		if (WARN(data->hdr.reserved, "Reserved bit is set in the PSC header\n")) {
			ret = 1;
			goto out;
		}

		/*
		 * Sanity check that entry processing is not going backwards.
		 * This will happen only if hypervisor is tricking us.
		 */
		if (WARN(data->hdr.end_entry > end_entry || cur_entry > data->hdr.cur_entry,
"SNP: PSC processing going backward, end_entry %d (got %d) cur_entry %d (got %d)\n",
			 end_entry, data->hdr.end_entry, cur_entry, data->hdr.cur_entry)) {
			ret = 1;
			goto out;
		}
	}

out:
	return ret;
}

static enum es_result vc_check_opcode_bytes(struct es_em_ctxt *ctxt,
					    unsigned long exit_code)
{
	unsigned int opcode = (unsigned int)ctxt->insn.opcode.value;
	u8 modrm = ctxt->insn.modrm.value;

	switch (exit_code) {

	case SVM_EXIT_IOIO:
	case SVM_EXIT_NPF:
		/* handled separately */
		return ES_OK;

	case SVM_EXIT_CPUID:
		if (opcode == 0xa20f)
			return ES_OK;
		break;

	case SVM_EXIT_INVD:
		if (opcode == 0x080f)
			return ES_OK;
		break;

	case SVM_EXIT_MONITOR:
		/* MONITOR and MONITORX instructions generate the same error code */
		if (opcode == 0x010f && (modrm == 0xc8 || modrm == 0xfa))
			return ES_OK;
		break;

	case SVM_EXIT_MWAIT:
		/* MWAIT and MWAITX instructions generate the same error code */
		if (opcode == 0x010f && (modrm == 0xc9 || modrm == 0xfb))
			return ES_OK;
		break;

	case SVM_EXIT_MSR:
		/* RDMSR */
		if (opcode == 0x320f ||
		/* WRMSR */
		    opcode == 0x300f)
			return ES_OK;
		break;

	case SVM_EXIT_RDPMC:
		if (opcode == 0x330f)
			return ES_OK;
		break;

	case SVM_EXIT_RDTSC:
		if (opcode == 0x310f)
			return ES_OK;
		break;

	case SVM_EXIT_RDTSCP:
		if (opcode == 0x010f && modrm == 0xf9)
			return ES_OK;
		break;

	case SVM_EXIT_READ_DR7:
		if (opcode == 0x210f &&
		    X86_MODRM_REG(ctxt->insn.modrm.value) == 7)
			return ES_OK;
		break;

	case SVM_EXIT_VMMCALL:
		if (opcode == 0x010f && modrm == 0xd9)
			return ES_OK;

		break;

	case SVM_EXIT_WRITE_DR7:
		if (opcode == 0x230f &&
		    X86_MODRM_REG(ctxt->insn.modrm.value) == 7)
			return ES_OK;
		break;

	case SVM_EXIT_WBINVD:
		if (opcode == 0x90f)
			return ES_OK;
		break;

	default:
		break;
	}

	sev_printk(KERN_ERR "Wrong/unhandled opcode bytes: 0x%x, exit_code: 0x%lx, rIP: 0x%lx\n",
		   opcode, exit_code, ctxt->regs->ip);

	return ES_UNSUPPORTED;
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
	if (!rmpadjust((unsigned long)&RIP_REL_REF(boot_ghcb_page), RMP_PG_SIZE_4K, 1))
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

	RIP_REL_REF(snp_vmpl) = secrets_page->svsm_guest_vmpl;

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
	RIP_REL_REF(boot_svsm_caa) = (struct svsm_ca *)caa;
	RIP_REL_REF(boot_svsm_caa_pa) = caa;

	/* Advertise the SVSM presence via CPUID. */
	cpuid_table = (struct snp_cpuid_table *)snp_cpuid_get_table();
	for (i = 0; i < cpuid_table->count; i++) {
		struct snp_cpuid_fn *fn = &cpuid_table->fn[i];

		if (fn->eax_in == 0x8000001f)
			fn->eax |= BIT(28);
	}

	return true;
}
