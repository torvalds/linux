// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Encrypted Register State Support
 *
 * Author: Joerg Roedel <jroedel@suse.de>
 */

/*
 * misc.h needs to be first because it knows how to include the other kernel
 * headers in the pre-decompression code in a way that does not break
 * compilation.
 */
#include "misc.h"

#include <asm/pgtable_types.h>
#include <asm/sev.h>
#include <asm/trapnr.h>
#include <asm/trap_pf.h>
#include <asm/msr-index.h>
#include <asm/fpu/xcr.h>
#include <asm/ptrace.h>
#include <asm/svm.h>
#include <asm/cpuid.h>

#include "error.h"
#include "../msr.h"

struct ghcb boot_ghcb_page __aligned(PAGE_SIZE);
struct ghcb *boot_ghcb;

/*
 * Copy a version of this function here - insn-eval.c can't be used in
 * pre-decompression code.
 */
static bool insn_has_rep_prefix(struct insn *insn)
{
	insn_byte_t p;
	int i;

	insn_get_prefixes(insn);

	for_each_insn_prefix(insn, i, p) {
		if (p == 0xf2 || p == 0xf3)
			return true;
	}

	return false;
}

/*
 * Only a dummy for insn_get_seg_base() - Early boot-code is 64bit only and
 * doesn't use segments.
 */
static unsigned long insn_get_seg_base(struct pt_regs *regs, int seg_reg_idx)
{
	return 0UL;
}

static inline u64 sev_es_rd_ghcb_msr(void)
{
	struct msr m;

	boot_rdmsr(MSR_AMD64_SEV_ES_GHCB, &m);

	return m.q;
}

static inline void sev_es_wr_ghcb_msr(u64 val)
{
	struct msr m;

	m.q = val;
	boot_wrmsr(MSR_AMD64_SEV_ES_GHCB, &m);
}

static enum es_result vc_decode_insn(struct es_em_ctxt *ctxt)
{
	char buffer[MAX_INSN_SIZE];
	int ret;

	memcpy(buffer, (unsigned char *)ctxt->regs->ip, MAX_INSN_SIZE);

	ret = insn_decode(&ctxt->insn, buffer, MAX_INSN_SIZE, INSN_MODE_64);
	if (ret < 0)
		return ES_DECODE_FAILED;

	return ES_OK;
}

static enum es_result vc_write_mem(struct es_em_ctxt *ctxt,
				   void *dst, char *buf, size_t size)
{
	memcpy(dst, buf, size);

	return ES_OK;
}

static enum es_result vc_read_mem(struct es_em_ctxt *ctxt,
				  void *src, char *buf, size_t size)
{
	memcpy(buf, src, size);

	return ES_OK;
}

#undef __init
#undef __pa
#define __init
#define __pa(x)	((unsigned long)(x))

#define __BOOT_COMPRESSED

/* Basic instruction decoding support needed */
#include "../../lib/inat.c"
#include "../../lib/insn.c"

/* Include code for early handlers */
#include "../../kernel/sev-shared.c"

static inline bool sev_snp_enabled(void)
{
	return sev_status & MSR_AMD64_SEV_SNP_ENABLED;
}

static void __page_state_change(unsigned long paddr, enum psc_op op)
{
	u64 val;

	if (!sev_snp_enabled())
		return;

	/*
	 * If private -> shared then invalidate the page before requesting the
	 * state change in the RMP table.
	 */
	if (op == SNP_PAGE_STATE_SHARED && pvalidate(paddr, RMP_PG_SIZE_4K, 0))
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_PVALIDATE);

	/* Issue VMGEXIT to change the page state in RMP table. */
	sev_es_wr_ghcb_msr(GHCB_MSR_PSC_REQ_GFN(paddr >> PAGE_SHIFT, op));
	VMGEXIT();

	/* Read the response of the VMGEXIT. */
	val = sev_es_rd_ghcb_msr();
	if ((GHCB_RESP_CODE(val) != GHCB_MSR_PSC_RESP) || GHCB_MSR_PSC_RESP_VAL(val))
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_PSC);

	/*
	 * Now that page state is changed in the RMP table, validate it so that it is
	 * consistent with the RMP entry.
	 */
	if (op == SNP_PAGE_STATE_PRIVATE && pvalidate(paddr, RMP_PG_SIZE_4K, 1))
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_PVALIDATE);
}

void snp_set_page_private(unsigned long paddr)
{
	__page_state_change(paddr, SNP_PAGE_STATE_PRIVATE);
}

void snp_set_page_shared(unsigned long paddr)
{
	__page_state_change(paddr, SNP_PAGE_STATE_SHARED);
}

static bool early_setup_ghcb(void)
{
	if (set_page_decrypted((unsigned long)&boot_ghcb_page))
		return false;

	/* Page is now mapped decrypted, clear it */
	memset(&boot_ghcb_page, 0, sizeof(boot_ghcb_page));

	boot_ghcb = &boot_ghcb_page;

	/* Initialize lookup tables for the instruction decoder */
	inat_init_tables();

	/* SNP guest requires the GHCB GPA must be registered */
	if (sev_snp_enabled())
		snp_register_ghcb_early(__pa(&boot_ghcb_page));

	return true;
}

void sev_es_shutdown_ghcb(void)
{
	if (!boot_ghcb)
		return;

	if (!sev_es_check_cpu_features())
		error("SEV-ES CPU Features missing.");

	/*
	 * GHCB Page must be flushed from the cache and mapped encrypted again.
	 * Otherwise the running kernel will see strange cache effects when
	 * trying to use that page.
	 */
	if (set_page_encrypted((unsigned long)&boot_ghcb_page))
		error("Can't map GHCB page encrypted");

	/*
	 * GHCB page is mapped encrypted again and flushed from the cache.
	 * Mark it non-present now to catch bugs when #VC exceptions trigger
	 * after this point.
	 */
	if (set_page_non_present((unsigned long)&boot_ghcb_page))
		error("Can't unmap GHCB page");
}

bool sev_es_check_ghcb_fault(unsigned long address)
{
	/* Check whether the fault was on the GHCB page */
	return ((address & PAGE_MASK) == (unsigned long)&boot_ghcb_page);
}

void do_boot_stage2_vc(struct pt_regs *regs, unsigned long exit_code)
{
	struct es_em_ctxt ctxt;
	enum es_result result;

	if (!boot_ghcb && !early_setup_ghcb())
		sev_es_terminate(SEV_TERM_SET_GEN, GHCB_SEV_ES_GEN_REQ);

	vc_ghcb_invalidate(boot_ghcb);
	result = vc_init_em_ctxt(&ctxt, regs, exit_code);
	if (result != ES_OK)
		goto finish;

	switch (exit_code) {
	case SVM_EXIT_RDTSC:
	case SVM_EXIT_RDTSCP:
		result = vc_handle_rdtsc(boot_ghcb, &ctxt, exit_code);
		break;
	case SVM_EXIT_IOIO:
		result = vc_handle_ioio(boot_ghcb, &ctxt);
		break;
	case SVM_EXIT_CPUID:
		result = vc_handle_cpuid(boot_ghcb, &ctxt);
		break;
	default:
		result = ES_UNSUPPORTED;
		break;
	}

finish:
	if (result == ES_OK)
		vc_finish_insn(&ctxt);
	else if (result != ES_RETRY)
		sev_es_terminate(SEV_TERM_SET_GEN, GHCB_SEV_ES_GEN_REQ);
}

static void enforce_vmpl0(void)
{
	u64 attrs;
	int err;

	/*
	 * RMPADJUST modifies RMP permissions of a lesser-privileged (numerically
	 * higher) privilege level. Here, clear the VMPL1 permission mask of the
	 * GHCB page. If the guest is not running at VMPL0, this will fail.
	 *
	 * If the guest is running at VMPL0, it will succeed. Even if that operation
	 * modifies permission bits, it is still ok to do so currently because Linux
	 * SNP guests are supported only on VMPL0 so VMPL1 or higher permission masks
	 * changing is a don't-care.
	 */
	attrs = 1;
	if (rmpadjust((unsigned long)&boot_ghcb_page, RMP_PG_SIZE_4K, attrs))
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_NOT_VMPL0);
}

void sev_enable(struct boot_params *bp)
{
	unsigned int eax, ebx, ecx, edx;
	struct msr m;
	bool snp;

	/*
	 * Setup/preliminary detection of SNP. This will be sanity-checked
	 * against CPUID/MSR values later.
	 */
	snp = snp_init(bp);

	/* Check for the SME/SEV support leaf */
	eax = 0x80000000;
	ecx = 0;
	native_cpuid(&eax, &ebx, &ecx, &edx);
	if (eax < 0x8000001f)
		return;

	/*
	 * Check for the SME/SEV feature:
	 *   CPUID Fn8000_001F[EAX]
	 *   - Bit 0 - Secure Memory Encryption support
	 *   - Bit 1 - Secure Encrypted Virtualization support
	 *   CPUID Fn8000_001F[EBX]
	 *   - Bits 5:0 - Pagetable bit position used to indicate encryption
	 */
	eax = 0x8000001f;
	ecx = 0;
	native_cpuid(&eax, &ebx, &ecx, &edx);
	/* Check whether SEV is supported */
	if (!(eax & BIT(1))) {
		if (snp)
			error("SEV-SNP support indicated by CC blob, but not CPUID.");
		return;
	}

	/* Set the SME mask if this is an SEV guest. */
	boot_rdmsr(MSR_AMD64_SEV, &m);
	sev_status = m.q;
	if (!(sev_status & MSR_AMD64_SEV_ENABLED))
		return;

	/* Negotiate the GHCB protocol version. */
	if (sev_status & MSR_AMD64_SEV_ES_ENABLED) {
		if (!sev_es_negotiate_protocol())
			sev_es_terminate(SEV_TERM_SET_GEN, GHCB_SEV_ES_PROT_UNSUPPORTED);
	}

	/*
	 * SNP is supported in v2 of the GHCB spec which mandates support for HV
	 * features.
	 */
	if (sev_status & MSR_AMD64_SEV_SNP_ENABLED) {
		if (!(get_hv_features() & GHCB_HV_FT_SNP))
			sev_es_terminate(SEV_TERM_SET_GEN, GHCB_SNP_UNSUPPORTED);

		enforce_vmpl0();
	}

	if (snp && !(sev_status & MSR_AMD64_SEV_SNP_ENABLED))
		error("SEV-SNP supported indicated by CC blob, but not SEV status MSR.");

	sme_me_mask = BIT_ULL(ebx & 0x3f);
}

/* Search for Confidential Computing blob in the EFI config table. */
static struct cc_blob_sev_info *find_cc_blob_efi(struct boot_params *bp)
{
	unsigned long cfg_table_pa;
	unsigned int cfg_table_len;
	int ret;

	ret = efi_get_conf_table(bp, &cfg_table_pa, &cfg_table_len);
	if (ret)
		return NULL;

	return (struct cc_blob_sev_info *)efi_find_vendor_table(bp, cfg_table_pa,
								cfg_table_len,
								EFI_CC_BLOB_GUID);
}

/*
 * Initial set up of SNP relies on information provided by the
 * Confidential Computing blob, which can be passed to the boot kernel
 * by firmware/bootloader in the following ways:
 *
 * - via an entry in the EFI config table
 * - via a setup_data structure, as defined by the Linux Boot Protocol
 *
 * Scan for the blob in that order.
 */
static struct cc_blob_sev_info *find_cc_blob(struct boot_params *bp)
{
	struct cc_blob_sev_info *cc_info;

	cc_info = find_cc_blob_efi(bp);
	if (cc_info)
		goto found_cc_info;

	cc_info = find_cc_blob_setup_data(bp);
	if (!cc_info)
		return NULL;

found_cc_info:
	if (cc_info->magic != CC_BLOB_SEV_HDR_MAGIC)
		sev_es_terminate(SEV_TERM_SET_GEN, GHCB_SNP_UNSUPPORTED);

	return cc_info;
}

/*
 * Indicate SNP based on presence of SNP-specific CC blob. Subsequent checks
 * will verify the SNP CPUID/MSR bits.
 */
bool snp_init(struct boot_params *bp)
{
	struct cc_blob_sev_info *cc_info;

	if (!bp)
		return false;

	cc_info = find_cc_blob(bp);
	if (!cc_info)
		return false;

	/*
	 * If a SNP-specific Confidential Computing blob is present, then
	 * firmware/bootloader have indicated SNP support. Verifying this
	 * involves CPUID checks which will be more reliable if the SNP
	 * CPUID table is used. See comments over snp_setup_cpuid_table() for
	 * more details.
	 */
	setup_cpuid_table(cc_info);

	/*
	 * Pass run-time kernel a pointer to CC info via boot_params so EFI
	 * config table doesn't need to be searched again during early startup
	 * phase.
	 */
	bp->cc_blob_address = (u32)(unsigned long)cc_info;

	return true;
}

void sev_prep_identity_maps(unsigned long top_level_pgt)
{
	/*
	 * The Confidential Computing blob is used very early in uncompressed
	 * kernel to find the in-memory CPUID table to handle CPUID
	 * instructions. Make sure an identity-mapping exists so it can be
	 * accessed after switchover.
	 */
	if (sev_snp_enabled()) {
		unsigned long cc_info_pa = boot_params->cc_blob_address;
		struct cc_blob_sev_info *cc_info;

		kernel_add_identity_map(cc_info_pa, cc_info_pa + sizeof(*cc_info));

		cc_info = (struct cc_blob_sev_info *)cc_info_pa;
		kernel_add_identity_map(cc_info->cpuid_phys, cc_info->cpuid_phys + cc_info->cpuid_len);
	}

	sev_verify_cbit(top_level_pgt);
}
