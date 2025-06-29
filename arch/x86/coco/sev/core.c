// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Memory Encryption Support
 *
 * Copyright (C) 2019 SUSE
 *
 * Author: Joerg Roedel <jroedel@suse.de>
 */

#define pr_fmt(fmt)	"SEV: " fmt

#include <linux/sched/debug.h>	/* For show_regs() */
#include <linux/percpu-defs.h>
#include <linux/cc_platform.h>
#include <linux/printk.h>
#include <linux/mm_types.h>
#include <linux/set_memory.h>
#include <linux/memblock.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/cpumask.h>
#include <linux/efi.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/psp-sev.h>
#include <linux/dmi.h>
#include <uapi/linux/sev-guest.h>
#include <crypto/gcm.h>

#include <asm/init.h>
#include <asm/cpu_entry_area.h>
#include <asm/stacktrace.h>
#include <asm/sev.h>
#include <asm/sev-internal.h>
#include <asm/insn-eval.h>
#include <asm/fpu/xcr.h>
#include <asm/processor.h>
#include <asm/realmode.h>
#include <asm/setup.h>
#include <asm/traps.h>
#include <asm/svm.h>
#include <asm/smp.h>
#include <asm/cpu.h>
#include <asm/apic.h>
#include <asm/cpuid/api.h>
#include <asm/cmdline.h>
#include <asm/msr.h>

/* AP INIT values as documented in the APM2  section "Processor Initialization State" */
#define AP_INIT_CS_LIMIT		0xffff
#define AP_INIT_DS_LIMIT		0xffff
#define AP_INIT_LDTR_LIMIT		0xffff
#define AP_INIT_GDTR_LIMIT		0xffff
#define AP_INIT_IDTR_LIMIT		0xffff
#define AP_INIT_TR_LIMIT		0xffff
#define AP_INIT_RFLAGS_DEFAULT		0x2
#define AP_INIT_DR6_DEFAULT		0xffff0ff0
#define AP_INIT_GPAT_DEFAULT		0x0007040600070406ULL
#define AP_INIT_XCR0_DEFAULT		0x1
#define AP_INIT_X87_FTW_DEFAULT		0x5555
#define AP_INIT_X87_FCW_DEFAULT		0x0040
#define AP_INIT_CR0_DEFAULT		0x60000010
#define AP_INIT_MXCSR_DEFAULT		0x1f80

static const char * const sev_status_feat_names[] = {
	[MSR_AMD64_SEV_ENABLED_BIT]		= "SEV",
	[MSR_AMD64_SEV_ES_ENABLED_BIT]		= "SEV-ES",
	[MSR_AMD64_SEV_SNP_ENABLED_BIT]		= "SEV-SNP",
	[MSR_AMD64_SNP_VTOM_BIT]		= "vTom",
	[MSR_AMD64_SNP_REFLECT_VC_BIT]		= "ReflectVC",
	[MSR_AMD64_SNP_RESTRICTED_INJ_BIT]	= "RI",
	[MSR_AMD64_SNP_ALT_INJ_BIT]		= "AI",
	[MSR_AMD64_SNP_DEBUG_SWAP_BIT]		= "DebugSwap",
	[MSR_AMD64_SNP_PREVENT_HOST_IBS_BIT]	= "NoHostIBS",
	[MSR_AMD64_SNP_BTB_ISOLATION_BIT]	= "BTBIsol",
	[MSR_AMD64_SNP_VMPL_SSS_BIT]		= "VmplSSS",
	[MSR_AMD64_SNP_SECURE_TSC_BIT]		= "SecureTSC",
	[MSR_AMD64_SNP_VMGEXIT_PARAM_BIT]	= "VMGExitParam",
	[MSR_AMD64_SNP_IBS_VIRT_BIT]		= "IBSVirt",
	[MSR_AMD64_SNP_VMSA_REG_PROT_BIT]	= "VMSARegProt",
	[MSR_AMD64_SNP_SMT_PROT_BIT]		= "SMTProt",
};

/*
 * For Secure TSC guests, the BSP fetches TSC_INFO using SNP guest messaging and
 * initializes snp_tsc_scale and snp_tsc_offset. These values are replicated
 * across the APs VMSA fields (TSC_SCALE and TSC_OFFSET).
 */
static u64 snp_tsc_scale __ro_after_init;
static u64 snp_tsc_offset __ro_after_init;
static u64 snp_tsc_freq_khz __ro_after_init;

DEFINE_PER_CPU(struct sev_es_runtime_data*, runtime_data);
DEFINE_PER_CPU(struct sev_es_save_area *, sev_vmsa);

/*
 * SVSM related information:
 *   When running under an SVSM, the VMPL that Linux is executing at must be
 *   non-zero. The VMPL is therefore used to indicate the presence of an SVSM.
 */
u8 snp_vmpl __ro_after_init;
EXPORT_SYMBOL_GPL(snp_vmpl);

static u64 __init get_snp_jump_table_addr(void)
{
	struct snp_secrets_page *secrets;
	void __iomem *mem;
	u64 addr;

	mem = ioremap_encrypted(sev_secrets_pa, PAGE_SIZE);
	if (!mem) {
		pr_err("Unable to locate AP jump table address: failed to map the SNP secrets page.\n");
		return 0;
	}

	secrets = (__force struct snp_secrets_page *)mem;

	addr = secrets->os_area.ap_jump_table_pa;
	iounmap(mem);

	return addr;
}

static u64 __init get_jump_table_addr(void)
{
	struct ghcb_state state;
	unsigned long flags;
	struct ghcb *ghcb;
	u64 ret = 0;

	if (cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		return get_snp_jump_table_addr();

	local_irq_save(flags);

	ghcb = __sev_get_ghcb(&state);

	vc_ghcb_invalidate(ghcb);
	ghcb_set_sw_exit_code(ghcb, SVM_VMGEXIT_AP_JUMP_TABLE);
	ghcb_set_sw_exit_info_1(ghcb, SVM_VMGEXIT_GET_AP_JUMP_TABLE);
	ghcb_set_sw_exit_info_2(ghcb, 0);

	sev_es_wr_ghcb_msr(__pa(ghcb));
	VMGEXIT();

	if (ghcb_sw_exit_info_1_is_valid(ghcb) &&
	    ghcb_sw_exit_info_2_is_valid(ghcb))
		ret = ghcb->save.sw_exit_info_2;

	__sev_put_ghcb(&state);

	local_irq_restore(flags);

	return ret;
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

static unsigned long __set_pages_state(struct snp_psc_desc *data, unsigned long vaddr,
				       unsigned long vaddr_end, int op)
{
	struct ghcb_state state;
	bool use_large_entry;
	struct psc_hdr *hdr;
	struct psc_entry *e;
	unsigned long flags;
	unsigned long pfn;
	struct ghcb *ghcb;
	int i;

	hdr = &data->hdr;
	e = data->entries;

	memset(data, 0, sizeof(*data));
	i = 0;

	while (vaddr < vaddr_end && i < ARRAY_SIZE(data->entries)) {
		hdr->end_entry = i;

		if (is_vmalloc_addr((void *)vaddr)) {
			pfn = vmalloc_to_pfn((void *)vaddr);
			use_large_entry = false;
		} else {
			pfn = __pa(vaddr) >> PAGE_SHIFT;
			use_large_entry = true;
		}

		e->gfn = pfn;
		e->operation = op;

		if (use_large_entry && IS_ALIGNED(vaddr, PMD_SIZE) &&
		    (vaddr_end - vaddr) >= PMD_SIZE) {
			e->pagesize = RMP_PG_SIZE_2M;
			vaddr += PMD_SIZE;
		} else {
			e->pagesize = RMP_PG_SIZE_4K;
			vaddr += PAGE_SIZE;
		}

		e++;
		i++;
	}

	/* Page validation must be rescinded before changing to shared */
	if (op == SNP_PAGE_STATE_SHARED)
		pvalidate_pages(data);

	local_irq_save(flags);

	if (sev_cfg.ghcbs_initialized)
		ghcb = __sev_get_ghcb(&state);
	else
		ghcb = boot_ghcb;

	/* Invoke the hypervisor to perform the page state changes */
	if (!ghcb || vmgexit_psc(ghcb, data))
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_PSC);

	if (sev_cfg.ghcbs_initialized)
		__sev_put_ghcb(&state);

	local_irq_restore(flags);

	/* Page validation must be performed after changing to private */
	if (op == SNP_PAGE_STATE_PRIVATE)
		pvalidate_pages(data);

	return vaddr;
}

static void set_pages_state(unsigned long vaddr, unsigned long npages, int op)
{
	struct snp_psc_desc desc;
	unsigned long vaddr_end;

	/* Use the MSR protocol when a GHCB is not available. */
	if (!boot_ghcb)
		return early_set_pages_state(vaddr, __pa(vaddr), npages, op);

	vaddr = vaddr & PAGE_MASK;
	vaddr_end = vaddr + (npages << PAGE_SHIFT);

	while (vaddr < vaddr_end)
		vaddr = __set_pages_state(&desc, vaddr, vaddr_end, op);
}

void snp_set_memory_shared(unsigned long vaddr, unsigned long npages)
{
	if (!cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		return;

	set_pages_state(vaddr, npages, SNP_PAGE_STATE_SHARED);
}

void snp_set_memory_private(unsigned long vaddr, unsigned long npages)
{
	if (!cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		return;

	set_pages_state(vaddr, npages, SNP_PAGE_STATE_PRIVATE);
}

void snp_accept_memory(phys_addr_t start, phys_addr_t end)
{
	unsigned long vaddr, npages;

	if (!cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		return;

	vaddr = (unsigned long)__va(start);
	npages = (end - start) >> PAGE_SHIFT;

	set_pages_state(vaddr, npages, SNP_PAGE_STATE_PRIVATE);
}

static int vmgexit_ap_control(u64 event, struct sev_es_save_area *vmsa, u32 apic_id)
{
	bool create = event != SVM_VMGEXIT_AP_DESTROY;
	struct ghcb_state state;
	unsigned long flags;
	struct ghcb *ghcb;
	int ret = 0;

	local_irq_save(flags);

	ghcb = __sev_get_ghcb(&state);

	vc_ghcb_invalidate(ghcb);

	if (create)
		ghcb_set_rax(ghcb, vmsa->sev_features);

	ghcb_set_sw_exit_code(ghcb, SVM_VMGEXIT_AP_CREATION);
	ghcb_set_sw_exit_info_1(ghcb,
				((u64)apic_id << 32)	|
				((u64)snp_vmpl << 16)	|
				event);
	ghcb_set_sw_exit_info_2(ghcb, __pa(vmsa));

	sev_es_wr_ghcb_msr(__pa(ghcb));
	VMGEXIT();

	if (!ghcb_sw_exit_info_1_is_valid(ghcb) ||
	    lower_32_bits(ghcb->save.sw_exit_info_1)) {
		pr_err("SNP AP %s error\n", (create ? "CREATE" : "DESTROY"));
		ret = -EINVAL;
	}

	__sev_put_ghcb(&state);

	local_irq_restore(flags);

	return ret;
}

static int snp_set_vmsa(void *va, void *caa, int apic_id, bool make_vmsa)
{
	int ret;

	if (snp_vmpl) {
		struct svsm_call call = {};
		unsigned long flags;

		local_irq_save(flags);

		call.caa = this_cpu_read(svsm_caa);
		call.rcx = __pa(va);

		if (make_vmsa) {
			/* Protocol 0, Call ID 2 */
			call.rax = SVSM_CORE_CALL(SVSM_CORE_CREATE_VCPU);
			call.rdx = __pa(caa);
			call.r8  = apic_id;
		} else {
			/* Protocol 0, Call ID 3 */
			call.rax = SVSM_CORE_CALL(SVSM_CORE_DELETE_VCPU);
		}

		ret = svsm_perform_call_protocol(&call);

		local_irq_restore(flags);
	} else {
		/*
		 * If the kernel runs at VMPL0, it can change the VMSA
		 * bit for a page using the RMPADJUST instruction.
		 * However, for the instruction to succeed it must
		 * target the permissions of a lesser privileged (higher
		 * numbered) VMPL level, so use VMPL1.
		 */
		u64 attrs = 1;

		if (make_vmsa)
			attrs |= RMPADJUST_VMSA_PAGE_BIT;

		ret = rmpadjust((unsigned long)va, RMP_PG_SIZE_4K, attrs);
	}

	return ret;
}

static void snp_cleanup_vmsa(struct sev_es_save_area *vmsa, int apic_id)
{
	int err;

	err = snp_set_vmsa(vmsa, NULL, apic_id, false);
	if (err)
		pr_err("clear VMSA page failed (%u), leaking page\n", err);
	else
		free_page((unsigned long)vmsa);
}

static void set_pte_enc(pte_t *kpte, int level, void *va)
{
	struct pte_enc_desc d = {
		.kpte	   = kpte,
		.pte_level = level,
		.va	   = va,
		.encrypt   = true
	};

	prepare_pte_enc(&d);
	set_pte_enc_mask(kpte, d.pfn, d.new_pgprot);
}

static void unshare_all_memory(void)
{
	unsigned long addr, end, size, ghcb;
	struct sev_es_runtime_data *data;
	unsigned int npages, level;
	bool skipped_addr;
	pte_t *pte;
	int cpu;

	/* Unshare the direct mapping. */
	addr = PAGE_OFFSET;
	end  = PAGE_OFFSET + get_max_mapped();

	while (addr < end) {
		pte = lookup_address(addr, &level);
		size = page_level_size(level);
		npages = size / PAGE_SIZE;
		skipped_addr = false;

		if (!pte || !pte_decrypted(*pte) || pte_none(*pte)) {
			addr += size;
			continue;
		}

		/*
		 * Ensure that all the per-CPU GHCBs are made private at the
		 * end of the unsharing loop so that the switch to the slower
		 * MSR protocol happens last.
		 */
		for_each_possible_cpu(cpu) {
			data = per_cpu(runtime_data, cpu);
			ghcb = (unsigned long)&data->ghcb_page;

			/* Handle the case of a huge page containing the GHCB page */
			if (addr <= ghcb && ghcb < addr + size) {
				skipped_addr = true;
				break;
			}
		}

		if (!skipped_addr) {
			set_pte_enc(pte, level, (void *)addr);
			snp_set_memory_private(addr, npages);
		}
		addr += size;
	}

	/* Unshare all bss decrypted memory. */
	addr = (unsigned long)__start_bss_decrypted;
	end  = (unsigned long)__start_bss_decrypted_unused;
	npages = (end - addr) >> PAGE_SHIFT;

	for (; addr < end; addr += PAGE_SIZE) {
		pte = lookup_address(addr, &level);
		if (!pte || !pte_decrypted(*pte) || pte_none(*pte))
			continue;

		set_pte_enc(pte, level, (void *)addr);
	}
	addr = (unsigned long)__start_bss_decrypted;
	snp_set_memory_private(addr, npages);

	__flush_tlb_all();
}

/* Stop new private<->shared conversions */
void snp_kexec_begin(void)
{
	if (!cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		return;

	if (!IS_ENABLED(CONFIG_KEXEC_CORE))
		return;

	/*
	 * Crash kernel ends up here with interrupts disabled: can't wait for
	 * conversions to finish.
	 *
	 * If race happened, just report and proceed.
	 */
	if (!set_memory_enc_stop_conversion())
		pr_warn("Failed to stop shared<->private conversions\n");
}

/*
 * Shutdown all APs except the one handling kexec/kdump and clearing
 * the VMSA tag on AP's VMSA pages as they are not being used as
 * VMSA page anymore.
 */
static void shutdown_all_aps(void)
{
	struct sev_es_save_area *vmsa;
	int apic_id, this_cpu, cpu;

	this_cpu = get_cpu();

	/*
	 * APs are already in HLT loop when enc_kexec_finish() callback
	 * is invoked.
	 */
	for_each_present_cpu(cpu) {
		vmsa = per_cpu(sev_vmsa, cpu);

		/*
		 * The BSP or offlined APs do not have guest allocated VMSA
		 * and there is no need  to clear the VMSA tag for this page.
		 */
		if (!vmsa)
			continue;

		/*
		 * Cannot clear the VMSA tag for the currently running vCPU.
		 */
		if (this_cpu == cpu) {
			unsigned long pa;
			struct page *p;

			pa = __pa(vmsa);
			/*
			 * Mark the VMSA page of the running vCPU as offline
			 * so that is excluded and not touched by makedumpfile
			 * while generating vmcore during kdump.
			 */
			p = pfn_to_online_page(pa >> PAGE_SHIFT);
			if (p)
				__SetPageOffline(p);
			continue;
		}

		apic_id = cpuid_to_apicid[cpu];

		/*
		 * Issue AP destroy to ensure AP gets kicked out of guest mode
		 * to allow using RMPADJUST to remove the VMSA tag on it's
		 * VMSA page.
		 */
		vmgexit_ap_control(SVM_VMGEXIT_AP_DESTROY, vmsa, apic_id);
		snp_cleanup_vmsa(vmsa, apic_id);
	}

	put_cpu();
}

void snp_kexec_finish(void)
{
	struct sev_es_runtime_data *data;
	unsigned long size, addr;
	unsigned int level, cpu;
	struct ghcb *ghcb;
	pte_t *pte;

	if (!cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		return;

	if (!IS_ENABLED(CONFIG_KEXEC_CORE))
		return;

	shutdown_all_aps();

	unshare_all_memory();

	/*
	 * Switch to using the MSR protocol to change per-CPU GHCBs to
	 * private. All the per-CPU GHCBs have been switched back to private,
	 * so can't do any more GHCB calls to the hypervisor beyond this point
	 * until the kexec'ed kernel starts running.
	 */
	boot_ghcb = NULL;
	sev_cfg.ghcbs_initialized = false;

	for_each_possible_cpu(cpu) {
		data = per_cpu(runtime_data, cpu);
		ghcb = &data->ghcb_page;
		pte = lookup_address((unsigned long)ghcb, &level);
		size = page_level_size(level);
		/* Handle the case of a huge page containing the GHCB page */
		addr = (unsigned long)ghcb & page_level_mask(level);
		set_pte_enc(pte, level, (void *)addr);
		snp_set_memory_private(addr, (size / PAGE_SIZE));
	}
}

#define __ATTR_BASE		(SVM_SELECTOR_P_MASK | SVM_SELECTOR_S_MASK)
#define INIT_CS_ATTRIBS		(__ATTR_BASE | SVM_SELECTOR_READ_MASK | SVM_SELECTOR_CODE_MASK)
#define INIT_DS_ATTRIBS		(__ATTR_BASE | SVM_SELECTOR_WRITE_MASK)

#define INIT_LDTR_ATTRIBS	(SVM_SELECTOR_P_MASK | 2)
#define INIT_TR_ATTRIBS		(SVM_SELECTOR_P_MASK | 3)

static void *snp_alloc_vmsa_page(int cpu)
{
	struct page *p;

	/*
	 * Allocate VMSA page to work around the SNP erratum where the CPU will
	 * incorrectly signal an RMP violation #PF if a large page (2MB or 1GB)
	 * collides with the RMP entry of VMSA page. The recommended workaround
	 * is to not use a large page.
	 *
	 * Allocate an 8k page which is also 8k-aligned.
	 */
	p = alloc_pages_node(cpu_to_node(cpu), GFP_KERNEL_ACCOUNT | __GFP_ZERO, 1);
	if (!p)
		return NULL;

	split_page(p, 1);

	/* Free the first 4k. This page may be 2M/1G aligned and cannot be used. */
	__free_page(p);

	return page_address(p + 1);
}

static int wakeup_cpu_via_vmgexit(u32 apic_id, unsigned long start_ip, unsigned int cpu)
{
	struct sev_es_save_area *cur_vmsa, *vmsa;
	struct svsm_ca *caa;
	u8 sipi_vector;
	int ret;
	u64 cr4;

	/*
	 * The hypervisor SNP feature support check has happened earlier, just check
	 * the AP_CREATION one here.
	 */
	if (!(sev_hv_features & GHCB_HV_FT_SNP_AP_CREATION))
		return -EOPNOTSUPP;

	/*
	 * Verify the desired start IP against the known trampoline start IP
	 * to catch any future new trampolines that may be introduced that
	 * would require a new protected guest entry point.
	 */
	if (WARN_ONCE(start_ip != real_mode_header->trampoline_start,
		      "Unsupported SNP start_ip: %lx\n", start_ip))
		return -EINVAL;

	/* Override start_ip with known protected guest start IP */
	start_ip = real_mode_header->sev_es_trampoline_start;
	cur_vmsa = per_cpu(sev_vmsa, cpu);

	/*
	 * A new VMSA is created each time because there is no guarantee that
	 * the current VMSA is the kernels or that the vCPU is not running. If
	 * an attempt was done to use the current VMSA with a running vCPU, a
	 * #VMEXIT of that vCPU would wipe out all of the settings being done
	 * here.
	 */
	vmsa = (struct sev_es_save_area *)snp_alloc_vmsa_page(cpu);
	if (!vmsa)
		return -ENOMEM;

	/* If an SVSM is present, the SVSM per-CPU CAA will be !NULL */
	caa = per_cpu(svsm_caa, cpu);

	/* CR4 should maintain the MCE value */
	cr4 = native_read_cr4() & X86_CR4_MCE;

	/* Set the CS value based on the start_ip converted to a SIPI vector */
	sipi_vector		= (start_ip >> 12);
	vmsa->cs.base		= sipi_vector << 12;
	vmsa->cs.limit		= AP_INIT_CS_LIMIT;
	vmsa->cs.attrib		= INIT_CS_ATTRIBS;
	vmsa->cs.selector	= sipi_vector << 8;

	/* Set the RIP value based on start_ip */
	vmsa->rip		= start_ip & 0xfff;

	/* Set AP INIT defaults as documented in the APM */
	vmsa->ds.limit		= AP_INIT_DS_LIMIT;
	vmsa->ds.attrib		= INIT_DS_ATTRIBS;
	vmsa->es		= vmsa->ds;
	vmsa->fs		= vmsa->ds;
	vmsa->gs		= vmsa->ds;
	vmsa->ss		= vmsa->ds;

	vmsa->gdtr.limit	= AP_INIT_GDTR_LIMIT;
	vmsa->ldtr.limit	= AP_INIT_LDTR_LIMIT;
	vmsa->ldtr.attrib	= INIT_LDTR_ATTRIBS;
	vmsa->idtr.limit	= AP_INIT_IDTR_LIMIT;
	vmsa->tr.limit		= AP_INIT_TR_LIMIT;
	vmsa->tr.attrib		= INIT_TR_ATTRIBS;

	vmsa->cr4		= cr4;
	vmsa->cr0		= AP_INIT_CR0_DEFAULT;
	vmsa->dr7		= DR7_RESET_VALUE;
	vmsa->dr6		= AP_INIT_DR6_DEFAULT;
	vmsa->rflags		= AP_INIT_RFLAGS_DEFAULT;
	vmsa->g_pat		= AP_INIT_GPAT_DEFAULT;
	vmsa->xcr0		= AP_INIT_XCR0_DEFAULT;
	vmsa->mxcsr		= AP_INIT_MXCSR_DEFAULT;
	vmsa->x87_ftw		= AP_INIT_X87_FTW_DEFAULT;
	vmsa->x87_fcw		= AP_INIT_X87_FCW_DEFAULT;

	/* SVME must be set. */
	vmsa->efer		= EFER_SVME;

	/*
	 * Set the SNP-specific fields for this VMSA:
	 *   VMPL level
	 *   SEV_FEATURES (matches the SEV STATUS MSR right shifted 2 bits)
	 */
	vmsa->vmpl		= snp_vmpl;
	vmsa->sev_features	= sev_status >> 2;

	/* Populate AP's TSC scale/offset to get accurate TSC values. */
	if (cc_platform_has(CC_ATTR_GUEST_SNP_SECURE_TSC)) {
		vmsa->tsc_scale = snp_tsc_scale;
		vmsa->tsc_offset = snp_tsc_offset;
	}

	/* Switch the page over to a VMSA page now that it is initialized */
	ret = snp_set_vmsa(vmsa, caa, apic_id, true);
	if (ret) {
		pr_err("set VMSA page failed (%u)\n", ret);
		free_page((unsigned long)vmsa);

		return -EINVAL;
	}

	/* Issue VMGEXIT AP Creation NAE event */
	ret = vmgexit_ap_control(SVM_VMGEXIT_AP_CREATE, vmsa, apic_id);
	if (ret) {
		snp_cleanup_vmsa(vmsa, apic_id);
		vmsa = NULL;
	}

	/* Free up any previous VMSA page */
	if (cur_vmsa)
		snp_cleanup_vmsa(cur_vmsa, apic_id);

	/* Record the current VMSA page */
	per_cpu(sev_vmsa, cpu) = vmsa;

	return ret;
}

void __init snp_set_wakeup_secondary_cpu(void)
{
	if (!cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		return;

	/*
	 * Always set this override if SNP is enabled. This makes it the
	 * required method to start APs under SNP. If the hypervisor does
	 * not support AP creation, then no APs will be started.
	 */
	apic_update_callback(wakeup_secondary_cpu, wakeup_cpu_via_vmgexit);
}

int __init sev_es_setup_ap_jump_table(struct real_mode_header *rmh)
{
	u16 startup_cs, startup_ip;
	phys_addr_t jump_table_pa;
	u64 jump_table_addr;
	u16 __iomem *jump_table;

	jump_table_addr = get_jump_table_addr();

	/* On UP guests there is no jump table so this is not a failure */
	if (!jump_table_addr)
		return 0;

	/* Check if AP Jump Table is page-aligned */
	if (jump_table_addr & ~PAGE_MASK)
		return -EINVAL;

	jump_table_pa = jump_table_addr & PAGE_MASK;

	startup_cs = (u16)(rmh->trampoline_start >> 4);
	startup_ip = (u16)(rmh->sev_es_trampoline_start -
			   rmh->trampoline_start);

	jump_table = ioremap_encrypted(jump_table_pa, PAGE_SIZE);
	if (!jump_table)
		return -EIO;

	writew(startup_ip, &jump_table[0]);
	writew(startup_cs, &jump_table[1]);

	iounmap(jump_table);

	return 0;
}

/*
 * This is needed by the OVMF UEFI firmware which will use whatever it finds in
 * the GHCB MSR as its GHCB to talk to the hypervisor. So make sure the per-cpu
 * runtime GHCBs used by the kernel are also mapped in the EFI page-table.
 */
int __init sev_es_efi_map_ghcbs(pgd_t *pgd)
{
	struct sev_es_runtime_data *data;
	unsigned long address, pflags;
	int cpu;
	u64 pfn;

	if (!cc_platform_has(CC_ATTR_GUEST_STATE_ENCRYPT))
		return 0;

	pflags = _PAGE_NX | _PAGE_RW;

	for_each_possible_cpu(cpu) {
		data = per_cpu(runtime_data, cpu);

		address = __pa(&data->ghcb_page);
		pfn = address >> PAGE_SHIFT;

		if (kernel_map_pages_in_pgd(pgd, pfn, address, 1, pflags))
			return 1;
	}

	return 0;
}

static void snp_register_per_cpu_ghcb(void)
{
	struct sev_es_runtime_data *data;
	struct ghcb *ghcb;

	data = this_cpu_read(runtime_data);
	ghcb = &data->ghcb_page;

	snp_register_ghcb_early(__pa(ghcb));
}

void setup_ghcb(void)
{
	if (!cc_platform_has(CC_ATTR_GUEST_STATE_ENCRYPT))
		return;

	/*
	 * Check whether the runtime #VC exception handler is active. It uses
	 * the per-CPU GHCB page which is set up by sev_es_init_vc_handling().
	 *
	 * If SNP is active, register the per-CPU GHCB page so that the runtime
	 * exception handler can use it.
	 */
	if (initial_vc_handler == (unsigned long)kernel_exc_vmm_communication) {
		if (cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
			snp_register_per_cpu_ghcb();

		sev_cfg.ghcbs_initialized = true;

		return;
	}

	/*
	 * Make sure the hypervisor talks a supported protocol.
	 * This gets called only in the BSP boot phase.
	 */
	if (!sev_es_negotiate_protocol())
		sev_es_terminate(SEV_TERM_SET_GEN, GHCB_SEV_ES_GEN_REQ);

	/*
	 * Clear the boot_ghcb. The first exception comes in before the bss
	 * section is cleared.
	 */
	memset(&boot_ghcb_page, 0, PAGE_SIZE);

	/* Alright - Make the boot-ghcb public */
	boot_ghcb = &boot_ghcb_page;

	/* SNP guest requires that GHCB GPA must be registered. */
	if (cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		snp_register_ghcb_early(__pa(&boot_ghcb_page));
}

#ifdef CONFIG_HOTPLUG_CPU
static void sev_es_ap_hlt_loop(void)
{
	struct ghcb_state state;
	struct ghcb *ghcb;

	ghcb = __sev_get_ghcb(&state);

	while (true) {
		vc_ghcb_invalidate(ghcb);
		ghcb_set_sw_exit_code(ghcb, SVM_VMGEXIT_AP_HLT_LOOP);
		ghcb_set_sw_exit_info_1(ghcb, 0);
		ghcb_set_sw_exit_info_2(ghcb, 0);

		sev_es_wr_ghcb_msr(__pa(ghcb));
		VMGEXIT();

		/* Wakeup signal? */
		if (ghcb_sw_exit_info_2_is_valid(ghcb) &&
		    ghcb->save.sw_exit_info_2)
			break;
	}

	__sev_put_ghcb(&state);
}

/*
 * Play_dead handler when running under SEV-ES. This is needed because
 * the hypervisor can't deliver an SIPI request to restart the AP.
 * Instead the kernel has to issue a VMGEXIT to halt the VCPU until the
 * hypervisor wakes it up again.
 */
static void sev_es_play_dead(void)
{
	play_dead_common();

	/* IRQs now disabled */

	sev_es_ap_hlt_loop();

	/*
	 * If we get here, the VCPU was woken up again. Jump to CPU
	 * startup code to get it back online.
	 */
	soft_restart_cpu();
}
#else  /* CONFIG_HOTPLUG_CPU */
#define sev_es_play_dead	native_play_dead
#endif /* CONFIG_HOTPLUG_CPU */

#ifdef CONFIG_SMP
static void __init sev_es_setup_play_dead(void)
{
	smp_ops.play_dead = sev_es_play_dead;
}
#else
static inline void sev_es_setup_play_dead(void) { }
#endif

static void __init alloc_runtime_data(int cpu)
{
	struct sev_es_runtime_data *data;

	data = memblock_alloc_node(sizeof(*data), PAGE_SIZE, cpu_to_node(cpu));
	if (!data)
		panic("Can't allocate SEV-ES runtime data");

	per_cpu(runtime_data, cpu) = data;

	if (snp_vmpl) {
		struct svsm_ca *caa;

		/* Allocate the SVSM CA page if an SVSM is present */
		caa = memblock_alloc_or_panic(sizeof(*caa), PAGE_SIZE);

		per_cpu(svsm_caa, cpu) = caa;
		per_cpu(svsm_caa_pa, cpu) = __pa(caa);
	}
}

static void __init init_ghcb(int cpu)
{
	struct sev_es_runtime_data *data;
	int err;

	data = per_cpu(runtime_data, cpu);

	err = early_set_memory_decrypted((unsigned long)&data->ghcb_page,
					 sizeof(data->ghcb_page));
	if (err)
		panic("Can't map GHCBs unencrypted");

	memset(&data->ghcb_page, 0, sizeof(data->ghcb_page));

	data->ghcb_active = false;
	data->backup_ghcb_active = false;
}

void __init sev_es_init_vc_handling(void)
{
	int cpu;

	BUILD_BUG_ON(offsetof(struct sev_es_runtime_data, ghcb_page) % PAGE_SIZE);

	if (!cc_platform_has(CC_ATTR_GUEST_STATE_ENCRYPT))
		return;

	if (!sev_es_check_cpu_features())
		panic("SEV-ES CPU Features missing");

	/*
	 * SNP is supported in v2 of the GHCB spec which mandates support for HV
	 * features.
	 */
	if (cc_platform_has(CC_ATTR_GUEST_SEV_SNP)) {
		sev_hv_features = get_hv_features();

		if (!(sev_hv_features & GHCB_HV_FT_SNP))
			sev_es_terminate(SEV_TERM_SET_GEN, GHCB_SNP_UNSUPPORTED);
	}

	/* Initialize per-cpu GHCB pages */
	for_each_possible_cpu(cpu) {
		alloc_runtime_data(cpu);
		init_ghcb(cpu);
	}

	/* If running under an SVSM, switch to the per-cpu CA */
	if (snp_vmpl) {
		struct svsm_call call = {};
		unsigned long flags;
		int ret;

		local_irq_save(flags);

		/*
		 * SVSM_CORE_REMAP_CA call:
		 *   RAX = 0 (Protocol=0, CallID=0)
		 *   RCX = New CA GPA
		 */
		call.caa = svsm_get_caa();
		call.rax = SVSM_CORE_CALL(SVSM_CORE_REMAP_CA);
		call.rcx = this_cpu_read(svsm_caa_pa);
		ret = svsm_perform_call_protocol(&call);
		if (ret)
			panic("Can't remap the SVSM CA, ret=%d, rax_out=0x%llx\n",
			      ret, call.rax_out);

		sev_cfg.use_cas = true;

		local_irq_restore(flags);
	}

	sev_es_setup_play_dead();

	/* Secondary CPUs use the runtime #VC handler */
	initial_vc_handler = (unsigned long)kernel_exc_vmm_communication;
}

/*
 * SEV-SNP guests should only execute dmi_setup() if EFI_CONFIG_TABLES are
 * enabled, as the alternative (fallback) logic for DMI probing in the legacy
 * ROM region can cause a crash since this region is not pre-validated.
 */
void __init snp_dmi_setup(void)
{
	if (efi_enabled(EFI_CONFIG_TABLES))
		dmi_setup();
}

static void dump_cpuid_table(void)
{
	const struct snp_cpuid_table *cpuid_table = snp_cpuid_get_table();
	int i = 0;

	pr_info("count=%d reserved=0x%x reserved2=0x%llx\n",
		cpuid_table->count, cpuid_table->__reserved1, cpuid_table->__reserved2);

	for (i = 0; i < SNP_CPUID_COUNT_MAX; i++) {
		const struct snp_cpuid_fn *fn = &cpuid_table->fn[i];

		pr_info("index=%3d fn=0x%08x subfn=0x%08x: eax=0x%08x ebx=0x%08x ecx=0x%08x edx=0x%08x xcr0_in=0x%016llx xss_in=0x%016llx reserved=0x%016llx\n",
			i, fn->eax_in, fn->ecx_in, fn->eax, fn->ebx, fn->ecx,
			fn->edx, fn->xcr0_in, fn->xss_in, fn->__reserved);
	}
}

/*
 * It is useful from an auditing/testing perspective to provide an easy way
 * for the guest owner to know that the CPUID table has been initialized as
 * expected, but that initialization happens too early in boot to print any
 * sort of indicator, and there's not really any other good place to do it,
 * so do it here.
 *
 * If running as an SNP guest, report the current VM privilege level (VMPL).
 */
static int __init report_snp_info(void)
{
	const struct snp_cpuid_table *cpuid_table = snp_cpuid_get_table();

	if (cpuid_table->count) {
		pr_info("Using SNP CPUID table, %d entries present.\n",
			cpuid_table->count);

		if (sev_cfg.debug)
			dump_cpuid_table();
	}

	if (cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		pr_info("SNP running at VMPL%u.\n", snp_vmpl);

	return 0;
}
arch_initcall(report_snp_info);

static void update_attest_input(struct svsm_call *call, struct svsm_attest_call *input)
{
	/* If (new) lengths have been returned, propagate them up */
	if (call->rcx_out != call->rcx)
		input->manifest_buf.len = call->rcx_out;

	if (call->rdx_out != call->rdx)
		input->certificates_buf.len = call->rdx_out;

	if (call->r8_out != call->r8)
		input->report_buf.len = call->r8_out;
}

int snp_issue_svsm_attest_req(u64 call_id, struct svsm_call *call,
			      struct svsm_attest_call *input)
{
	struct svsm_attest_call *ac;
	unsigned long flags;
	u64 attest_call_pa;
	int ret;

	if (!snp_vmpl)
		return -EINVAL;

	local_irq_save(flags);

	call->caa = svsm_get_caa();

	ac = (struct svsm_attest_call *)call->caa->svsm_buffer;
	attest_call_pa = svsm_get_caa_pa() + offsetof(struct svsm_ca, svsm_buffer);

	*ac = *input;

	/*
	 * Set input registers for the request and set RDX and R8 to known
	 * values in order to detect length values being returned in them.
	 */
	call->rax = call_id;
	call->rcx = attest_call_pa;
	call->rdx = -1;
	call->r8 = -1;
	ret = svsm_perform_call_protocol(call);
	update_attest_input(call, input);

	local_irq_restore(flags);

	return ret;
}
EXPORT_SYMBOL_GPL(snp_issue_svsm_attest_req);

static int snp_issue_guest_request(struct snp_guest_req *req, struct snp_req_data *input,
				   struct snp_guest_request_ioctl *rio)
{
	struct ghcb_state state;
	struct es_em_ctxt ctxt;
	unsigned long flags;
	struct ghcb *ghcb;
	int ret;

	rio->exitinfo2 = SEV_RET_NO_FW_CALL;

	/*
	 * __sev_get_ghcb() needs to run with IRQs disabled because it is using
	 * a per-CPU GHCB.
	 */
	local_irq_save(flags);

	ghcb = __sev_get_ghcb(&state);
	if (!ghcb) {
		ret = -EIO;
		goto e_restore_irq;
	}

	vc_ghcb_invalidate(ghcb);

	if (req->exit_code == SVM_VMGEXIT_EXT_GUEST_REQUEST) {
		ghcb_set_rax(ghcb, input->data_gpa);
		ghcb_set_rbx(ghcb, input->data_npages);
	}

	ret = sev_es_ghcb_hv_call(ghcb, &ctxt, req->exit_code, input->req_gpa, input->resp_gpa);
	if (ret)
		goto e_put;

	rio->exitinfo2 = ghcb->save.sw_exit_info_2;
	switch (rio->exitinfo2) {
	case 0:
		break;

	case SNP_GUEST_VMM_ERR(SNP_GUEST_VMM_ERR_BUSY):
		ret = -EAGAIN;
		break;

	case SNP_GUEST_VMM_ERR(SNP_GUEST_VMM_ERR_INVALID_LEN):
		/* Number of expected pages are returned in RBX */
		if (req->exit_code == SVM_VMGEXIT_EXT_GUEST_REQUEST) {
			input->data_npages = ghcb_get_rbx(ghcb);
			ret = -ENOSPC;
			break;
		}
		fallthrough;
	default:
		ret = -EIO;
		break;
	}

e_put:
	__sev_put_ghcb(&state);
e_restore_irq:
	local_irq_restore(flags);

	return ret;
}

/**
 * snp_svsm_vtpm_probe() - Probe if SVSM provides a vTPM device
 *
 * Check that there is SVSM and that it supports at least TPM_SEND_COMMAND
 * which is the only request used so far.
 *
 * Return: true if the platform provides a vTPM SVSM device, false otherwise.
 */
static bool snp_svsm_vtpm_probe(void)
{
	struct svsm_call call = {};

	/* The vTPM device is available only if a SVSM is present */
	if (!snp_vmpl)
		return false;

	call.caa = svsm_get_caa();
	call.rax = SVSM_VTPM_CALL(SVSM_VTPM_QUERY);

	if (svsm_perform_call_protocol(&call))
		return false;

	/* Check platform commands contains TPM_SEND_COMMAND - platform command 8 */
	return call.rcx_out & BIT_ULL(8);
}

/**
 * snp_svsm_vtpm_send_command() - Execute a vTPM operation on SVSM
 * @buffer: A buffer used to both send the command and receive the response.
 *
 * Execute a SVSM_VTPM_CMD call as defined by
 * "Secure VM Service Module for SEV-SNP Guests" Publication # 58019 Revision: 1.00
 *
 * All command request/response buffers have a common structure as specified by
 * the following table:
 *     Byte      Size       In/Out    Description
 *     Offset    (Bytes)
 *     0x000     4          In        Platform command
 *                          Out       Platform command response size
 *
 * Each command can build upon this common request/response structure to create
 * a structure specific to the command. See include/linux/tpm_svsm.h for more
 * details.
 *
 * Return: 0 on success, -errno on failure
 */
int snp_svsm_vtpm_send_command(u8 *buffer)
{
	struct svsm_call call = {};

	call.caa = svsm_get_caa();
	call.rax = SVSM_VTPM_CALL(SVSM_VTPM_CMD);
	call.rcx = __pa(buffer);

	return svsm_perform_call_protocol(&call);
}
EXPORT_SYMBOL_GPL(snp_svsm_vtpm_send_command);

static struct platform_device sev_guest_device = {
	.name		= "sev-guest",
	.id		= -1,
};

static struct platform_device tpm_svsm_device = {
	.name		= "tpm-svsm",
	.id		= -1,
};

static int __init snp_init_platform_device(void)
{
	if (!cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		return -ENODEV;

	if (platform_device_register(&sev_guest_device))
		return -ENODEV;

	if (snp_svsm_vtpm_probe() &&
	    platform_device_register(&tpm_svsm_device))
		return -ENODEV;

	pr_info("SNP guest platform devices initialized.\n");
	return 0;
}
device_initcall(snp_init_platform_device);

void sev_show_status(void)
{
	int i;

	pr_info("Status: ");
	for (i = 0; i < MSR_AMD64_SNP_RESV_BIT; i++) {
		if (sev_status & BIT_ULL(i)) {
			if (!sev_status_feat_names[i])
				continue;

			pr_cont("%s ", sev_status_feat_names[i]);
		}
	}
	pr_cont("\n");
}

void __init snp_update_svsm_ca(void)
{
	if (!snp_vmpl)
		return;

	/* Update the CAA to a proper kernel address */
	boot_svsm_caa = &boot_svsm_ca_page;
}

#ifdef CONFIG_SYSFS
static ssize_t vmpl_show(struct kobject *kobj,
			 struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", snp_vmpl);
}

static struct kobj_attribute vmpl_attr = __ATTR_RO(vmpl);

static struct attribute *vmpl_attrs[] = {
	&vmpl_attr.attr,
	NULL
};

static struct attribute_group sev_attr_group = {
	.attrs = vmpl_attrs,
};

static int __init sev_sysfs_init(void)
{
	struct kobject *sev_kobj;
	struct device *dev_root;
	int ret;

	if (!cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		return -ENODEV;

	dev_root = bus_get_dev_root(&cpu_subsys);
	if (!dev_root)
		return -ENODEV;

	sev_kobj = kobject_create_and_add("sev", &dev_root->kobj);
	put_device(dev_root);

	if (!sev_kobj)
		return -ENOMEM;

	ret = sysfs_create_group(sev_kobj, &sev_attr_group);
	if (ret)
		kobject_put(sev_kobj);

	return ret;
}
arch_initcall(sev_sysfs_init);
#endif // CONFIG_SYSFS

static void free_shared_pages(void *buf, size_t sz)
{
	unsigned int npages = PAGE_ALIGN(sz) >> PAGE_SHIFT;
	int ret;

	if (!buf)
		return;

	ret = set_memory_encrypted((unsigned long)buf, npages);
	if (ret) {
		WARN_ONCE(ret, "failed to restore encryption mask (leak it)\n");
		return;
	}

	__free_pages(virt_to_page(buf), get_order(sz));
}

static void *alloc_shared_pages(size_t sz)
{
	unsigned int npages = PAGE_ALIGN(sz) >> PAGE_SHIFT;
	struct page *page;
	int ret;

	page = alloc_pages(GFP_KERNEL_ACCOUNT, get_order(sz));
	if (!page)
		return NULL;

	ret = set_memory_decrypted((unsigned long)page_address(page), npages);
	if (ret) {
		pr_err("failed to mark page shared, ret=%d\n", ret);
		__free_pages(page, get_order(sz));
		return NULL;
	}

	return page_address(page);
}

static u8 *get_vmpck(int id, struct snp_secrets_page *secrets, u32 **seqno)
{
	u8 *key = NULL;

	switch (id) {
	case 0:
		*seqno = &secrets->os_area.msg_seqno_0;
		key = secrets->vmpck0;
		break;
	case 1:
		*seqno = &secrets->os_area.msg_seqno_1;
		key = secrets->vmpck1;
		break;
	case 2:
		*seqno = &secrets->os_area.msg_seqno_2;
		key = secrets->vmpck2;
		break;
	case 3:
		*seqno = &secrets->os_area.msg_seqno_3;
		key = secrets->vmpck3;
		break;
	default:
		break;
	}

	return key;
}

static struct aesgcm_ctx *snp_init_crypto(u8 *key, size_t keylen)
{
	struct aesgcm_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	if (aesgcm_expandkey(ctx, key, keylen, AUTHTAG_LEN)) {
		pr_err("Crypto context initialization failed\n");
		kfree(ctx);
		return NULL;
	}

	return ctx;
}

int snp_msg_init(struct snp_msg_desc *mdesc, int vmpck_id)
{
	/* Adjust the default VMPCK key based on the executing VMPL level */
	if (vmpck_id == -1)
		vmpck_id = snp_vmpl;

	mdesc->vmpck = get_vmpck(vmpck_id, mdesc->secrets, &mdesc->os_area_msg_seqno);
	if (!mdesc->vmpck) {
		pr_err("Invalid VMPCK%d communication key\n", vmpck_id);
		return -EINVAL;
	}

	/* Verify that VMPCK is not zero. */
	if (!memchr_inv(mdesc->vmpck, 0, VMPCK_KEY_LEN)) {
		pr_err("Empty VMPCK%d communication key\n", vmpck_id);
		return -EINVAL;
	}

	mdesc->vmpck_id = vmpck_id;

	mdesc->ctx = snp_init_crypto(mdesc->vmpck, VMPCK_KEY_LEN);
	if (!mdesc->ctx)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL_GPL(snp_msg_init);

struct snp_msg_desc *snp_msg_alloc(void)
{
	struct snp_msg_desc *mdesc;
	void __iomem *mem;

	BUILD_BUG_ON(sizeof(struct snp_guest_msg) > PAGE_SIZE);

	mdesc = kzalloc(sizeof(struct snp_msg_desc), GFP_KERNEL);
	if (!mdesc)
		return ERR_PTR(-ENOMEM);

	mem = ioremap_encrypted(sev_secrets_pa, PAGE_SIZE);
	if (!mem)
		goto e_free_mdesc;

	mdesc->secrets = (__force struct snp_secrets_page *)mem;

	/* Allocate the shared page used for the request and response message. */
	mdesc->request = alloc_shared_pages(sizeof(struct snp_guest_msg));
	if (!mdesc->request)
		goto e_unmap;

	mdesc->response = alloc_shared_pages(sizeof(struct snp_guest_msg));
	if (!mdesc->response)
		goto e_free_request;

	return mdesc;

e_free_request:
	free_shared_pages(mdesc->request, sizeof(struct snp_guest_msg));
e_unmap:
	iounmap(mem);
e_free_mdesc:
	kfree(mdesc);

	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL_GPL(snp_msg_alloc);

void snp_msg_free(struct snp_msg_desc *mdesc)
{
	if (!mdesc)
		return;

	kfree(mdesc->ctx);
	free_shared_pages(mdesc->response, sizeof(struct snp_guest_msg));
	free_shared_pages(mdesc->request, sizeof(struct snp_guest_msg));
	iounmap((__force void __iomem *)mdesc->secrets);

	memset(mdesc, 0, sizeof(*mdesc));
	kfree(mdesc);
}
EXPORT_SYMBOL_GPL(snp_msg_free);

/* Mutex to serialize the shared buffer access and command handling. */
static DEFINE_MUTEX(snp_cmd_mutex);

/*
 * If an error is received from the host or AMD Secure Processor (ASP) there
 * are two options. Either retry the exact same encrypted request or discontinue
 * using the VMPCK.
 *
 * This is because in the current encryption scheme GHCB v2 uses AES-GCM to
 * encrypt the requests. The IV for this scheme is the sequence number. GCM
 * cannot tolerate IV reuse.
 *
 * The ASP FW v1.51 only increments the sequence numbers on a successful
 * guest<->ASP back and forth and only accepts messages at its exact sequence
 * number.
 *
 * So if the sequence number were to be reused the encryption scheme is
 * vulnerable. If the sequence number were incremented for a fresh IV the ASP
 * will reject the request.
 */
static void snp_disable_vmpck(struct snp_msg_desc *mdesc)
{
	pr_alert("Disabling VMPCK%d communication key to prevent IV reuse.\n",
		  mdesc->vmpck_id);
	memzero_explicit(mdesc->vmpck, VMPCK_KEY_LEN);
	mdesc->vmpck = NULL;
}

static inline u64 __snp_get_msg_seqno(struct snp_msg_desc *mdesc)
{
	u64 count;

	lockdep_assert_held(&snp_cmd_mutex);

	/* Read the current message sequence counter from secrets pages */
	count = *mdesc->os_area_msg_seqno;

	return count + 1;
}

/* Return a non-zero on success */
static u64 snp_get_msg_seqno(struct snp_msg_desc *mdesc)
{
	u64 count = __snp_get_msg_seqno(mdesc);

	/*
	 * The message sequence counter for the SNP guest request is a  64-bit
	 * value but the version 2 of GHCB specification defines a 32-bit storage
	 * for it. If the counter exceeds the 32-bit value then return zero.
	 * The caller should check the return value, but if the caller happens to
	 * not check the value and use it, then the firmware treats zero as an
	 * invalid number and will fail the  message request.
	 */
	if (count >= UINT_MAX) {
		pr_err("request message sequence counter overflow\n");
		return 0;
	}

	return count;
}

static void snp_inc_msg_seqno(struct snp_msg_desc *mdesc)
{
	/*
	 * The counter is also incremented by the PSP, so increment it by 2
	 * and save in secrets page.
	 */
	*mdesc->os_area_msg_seqno += 2;
}

static int verify_and_dec_payload(struct snp_msg_desc *mdesc, struct snp_guest_req *req)
{
	struct snp_guest_msg *resp_msg = &mdesc->secret_response;
	struct snp_guest_msg *req_msg = &mdesc->secret_request;
	struct snp_guest_msg_hdr *req_msg_hdr = &req_msg->hdr;
	struct snp_guest_msg_hdr *resp_msg_hdr = &resp_msg->hdr;
	struct aesgcm_ctx *ctx = mdesc->ctx;
	u8 iv[GCM_AES_IV_SIZE] = {};

	pr_debug("response [seqno %lld type %d version %d sz %d]\n",
		 resp_msg_hdr->msg_seqno, resp_msg_hdr->msg_type, resp_msg_hdr->msg_version,
		 resp_msg_hdr->msg_sz);

	/* Copy response from shared memory to encrypted memory. */
	memcpy(resp_msg, mdesc->response, sizeof(*resp_msg));

	/* Verify that the sequence counter is incremented by 1 */
	if (unlikely(resp_msg_hdr->msg_seqno != (req_msg_hdr->msg_seqno + 1)))
		return -EBADMSG;

	/* Verify response message type and version number. */
	if (resp_msg_hdr->msg_type != (req_msg_hdr->msg_type + 1) ||
	    resp_msg_hdr->msg_version != req_msg_hdr->msg_version)
		return -EBADMSG;

	/*
	 * If the message size is greater than our buffer length then return
	 * an error.
	 */
	if (unlikely((resp_msg_hdr->msg_sz + ctx->authsize) > req->resp_sz))
		return -EBADMSG;

	/* Decrypt the payload */
	memcpy(iv, &resp_msg_hdr->msg_seqno, min(sizeof(iv), sizeof(resp_msg_hdr->msg_seqno)));
	if (!aesgcm_decrypt(ctx, req->resp_buf, resp_msg->payload, resp_msg_hdr->msg_sz,
			    &resp_msg_hdr->algo, AAD_LEN, iv, resp_msg_hdr->authtag))
		return -EBADMSG;

	return 0;
}

static int enc_payload(struct snp_msg_desc *mdesc, u64 seqno, struct snp_guest_req *req)
{
	struct snp_guest_msg *msg = &mdesc->secret_request;
	struct snp_guest_msg_hdr *hdr = &msg->hdr;
	struct aesgcm_ctx *ctx = mdesc->ctx;
	u8 iv[GCM_AES_IV_SIZE] = {};

	memset(msg, 0, sizeof(*msg));

	hdr->algo = SNP_AEAD_AES_256_GCM;
	hdr->hdr_version = MSG_HDR_VER;
	hdr->hdr_sz = sizeof(*hdr);
	hdr->msg_type = req->msg_type;
	hdr->msg_version = req->msg_version;
	hdr->msg_seqno = seqno;
	hdr->msg_vmpck = req->vmpck_id;
	hdr->msg_sz = req->req_sz;

	/* Verify the sequence number is non-zero */
	if (!hdr->msg_seqno)
		return -ENOSR;

	pr_debug("request [seqno %lld type %d version %d sz %d]\n",
		 hdr->msg_seqno, hdr->msg_type, hdr->msg_version, hdr->msg_sz);

	if (WARN_ON((req->req_sz + ctx->authsize) > sizeof(msg->payload)))
		return -EBADMSG;

	memcpy(iv, &hdr->msg_seqno, min(sizeof(iv), sizeof(hdr->msg_seqno)));
	aesgcm_encrypt(ctx, msg->payload, req->req_buf, req->req_sz, &hdr->algo,
		       AAD_LEN, iv, hdr->authtag);

	return 0;
}

static int __handle_guest_request(struct snp_msg_desc *mdesc, struct snp_guest_req *req,
				  struct snp_guest_request_ioctl *rio)
{
	unsigned long req_start = jiffies;
	unsigned int override_npages = 0;
	u64 override_err = 0;
	int rc;

retry_request:
	/*
	 * Call firmware to process the request. In this function the encrypted
	 * message enters shared memory with the host. So after this call the
	 * sequence number must be incremented or the VMPCK must be deleted to
	 * prevent reuse of the IV.
	 */
	rc = snp_issue_guest_request(req, &req->input, rio);
	switch (rc) {
	case -ENOSPC:
		/*
		 * If the extended guest request fails due to having too
		 * small of a certificate data buffer, retry the same
		 * guest request without the extended data request in
		 * order to increment the sequence number and thus avoid
		 * IV reuse.
		 */
		override_npages = req->input.data_npages;
		req->exit_code	= SVM_VMGEXIT_GUEST_REQUEST;

		/*
		 * Override the error to inform callers the given extended
		 * request buffer size was too small and give the caller the
		 * required buffer size.
		 */
		override_err = SNP_GUEST_VMM_ERR(SNP_GUEST_VMM_ERR_INVALID_LEN);

		/*
		 * If this call to the firmware succeeds, the sequence number can
		 * be incremented allowing for continued use of the VMPCK. If
		 * there is an error reflected in the return value, this value
		 * is checked further down and the result will be the deletion
		 * of the VMPCK and the error code being propagated back to the
		 * user as an ioctl() return code.
		 */
		goto retry_request;

	/*
	 * The host may return SNP_GUEST_VMM_ERR_BUSY if the request has been
	 * throttled. Retry in the driver to avoid returning and reusing the
	 * message sequence number on a different message.
	 */
	case -EAGAIN:
		if (jiffies - req_start > SNP_REQ_MAX_RETRY_DURATION) {
			rc = -ETIMEDOUT;
			break;
		}
		schedule_timeout_killable(SNP_REQ_RETRY_DELAY);
		goto retry_request;
	}

	/*
	 * Increment the message sequence number. There is no harm in doing
	 * this now because decryption uses the value stored in the response
	 * structure and any failure will wipe the VMPCK, preventing further
	 * use anyway.
	 */
	snp_inc_msg_seqno(mdesc);

	if (override_err) {
		rio->exitinfo2 = override_err;

		/*
		 * If an extended guest request was issued and the supplied certificate
		 * buffer was not large enough, a standard guest request was issued to
		 * prevent IV reuse. If the standard request was successful, return -EIO
		 * back to the caller as would have originally been returned.
		 */
		if (!rc && override_err == SNP_GUEST_VMM_ERR(SNP_GUEST_VMM_ERR_INVALID_LEN))
			rc = -EIO;
	}

	if (override_npages)
		req->input.data_npages = override_npages;

	return rc;
}

int snp_send_guest_request(struct snp_msg_desc *mdesc, struct snp_guest_req *req,
			   struct snp_guest_request_ioctl *rio)
{
	u64 seqno;
	int rc;

	guard(mutex)(&snp_cmd_mutex);

	/* Check if the VMPCK is not empty */
	if (!mdesc->vmpck || !memchr_inv(mdesc->vmpck, 0, VMPCK_KEY_LEN)) {
		pr_err_ratelimited("VMPCK is disabled\n");
		return -ENOTTY;
	}

	/* Get message sequence and verify that its a non-zero */
	seqno = snp_get_msg_seqno(mdesc);
	if (!seqno)
		return -EIO;

	/* Clear shared memory's response for the host to populate. */
	memset(mdesc->response, 0, sizeof(struct snp_guest_msg));

	/* Encrypt the userspace provided payload in mdesc->secret_request. */
	rc = enc_payload(mdesc, seqno, req);
	if (rc)
		return rc;

	/*
	 * Write the fully encrypted request to the shared unencrypted
	 * request page.
	 */
	memcpy(mdesc->request, &mdesc->secret_request, sizeof(mdesc->secret_request));

	/* Initialize the input address for guest request */
	req->input.req_gpa = __pa(mdesc->request);
	req->input.resp_gpa = __pa(mdesc->response);
	req->input.data_gpa = req->certs_data ? __pa(req->certs_data) : 0;

	rc = __handle_guest_request(mdesc, req, rio);
	if (rc) {
		if (rc == -EIO &&
		    rio->exitinfo2 == SNP_GUEST_VMM_ERR(SNP_GUEST_VMM_ERR_INVALID_LEN))
			return rc;

		pr_alert("Detected error from ASP request. rc: %d, exitinfo2: 0x%llx\n",
			 rc, rio->exitinfo2);

		snp_disable_vmpck(mdesc);
		return rc;
	}

	rc = verify_and_dec_payload(mdesc, req);
	if (rc) {
		pr_alert("Detected unexpected decode failure from ASP. rc: %d\n", rc);
		snp_disable_vmpck(mdesc);
		return rc;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snp_send_guest_request);

static int __init snp_get_tsc_info(void)
{
	struct snp_guest_request_ioctl *rio;
	struct snp_tsc_info_resp *tsc_resp;
	struct snp_tsc_info_req *tsc_req;
	struct snp_msg_desc *mdesc;
	struct snp_guest_req *req;
	int rc = -ENOMEM;

	tsc_req = kzalloc(sizeof(*tsc_req), GFP_KERNEL);
	if (!tsc_req)
		return rc;

	/*
	 * The intermediate response buffer is used while decrypting the
	 * response payload. Make sure that it has enough space to cover
	 * the authtag.
	 */
	tsc_resp = kzalloc(sizeof(*tsc_resp) + AUTHTAG_LEN, GFP_KERNEL);
	if (!tsc_resp)
		goto e_free_tsc_req;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		goto e_free_tsc_resp;

	rio = kzalloc(sizeof(*rio), GFP_KERNEL);
	if (!rio)
		goto e_free_req;

	mdesc = snp_msg_alloc();
	if (IS_ERR_OR_NULL(mdesc))
		goto e_free_rio;

	rc = snp_msg_init(mdesc, snp_vmpl);
	if (rc)
		goto e_free_mdesc;

	req->msg_version = MSG_HDR_VER;
	req->msg_type = SNP_MSG_TSC_INFO_REQ;
	req->vmpck_id = snp_vmpl;
	req->req_buf = tsc_req;
	req->req_sz = sizeof(*tsc_req);
	req->resp_buf = (void *)tsc_resp;
	req->resp_sz = sizeof(*tsc_resp) + AUTHTAG_LEN;
	req->exit_code = SVM_VMGEXIT_GUEST_REQUEST;

	rc = snp_send_guest_request(mdesc, req, rio);
	if (rc)
		goto e_request;

	pr_debug("%s: response status 0x%x scale 0x%llx offset 0x%llx factor 0x%x\n",
		 __func__, tsc_resp->status, tsc_resp->tsc_scale, tsc_resp->tsc_offset,
		 tsc_resp->tsc_factor);

	if (!tsc_resp->status) {
		snp_tsc_scale = tsc_resp->tsc_scale;
		snp_tsc_offset = tsc_resp->tsc_offset;
	} else {
		pr_err("Failed to get TSC info, response status 0x%x\n", tsc_resp->status);
		rc = -EIO;
	}

e_request:
	/* The response buffer contains sensitive data, explicitly clear it. */
	memzero_explicit(tsc_resp, sizeof(*tsc_resp) + AUTHTAG_LEN);
e_free_mdesc:
	snp_msg_free(mdesc);
e_free_rio:
	kfree(rio);
e_free_req:
	kfree(req);
 e_free_tsc_resp:
	kfree(tsc_resp);
e_free_tsc_req:
	kfree(tsc_req);

	return rc;
}

void __init snp_secure_tsc_prepare(void)
{
	if (!cc_platform_has(CC_ATTR_GUEST_SNP_SECURE_TSC))
		return;

	if (snp_get_tsc_info()) {
		pr_alert("Unable to retrieve Secure TSC info from ASP\n");
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_SECURE_TSC);
	}

	pr_debug("SecureTSC enabled");
}

static unsigned long securetsc_get_tsc_khz(void)
{
	return snp_tsc_freq_khz;
}

void __init snp_secure_tsc_init(void)
{
	unsigned long long tsc_freq_mhz;

	if (!cc_platform_has(CC_ATTR_GUEST_SNP_SECURE_TSC))
		return;

	setup_force_cpu_cap(X86_FEATURE_TSC_KNOWN_FREQ);
	rdmsrq(MSR_AMD64_GUEST_TSC_FREQ, tsc_freq_mhz);
	snp_tsc_freq_khz = (unsigned long)(tsc_freq_mhz * 1000);

	x86_platform.calibrate_cpu = securetsc_get_tsc_khz;
	x86_platform.calibrate_tsc = securetsc_get_tsc_khz;
}
