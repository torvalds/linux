// SPDX-License-Identifier: GPL-2.0
/*
 * Hyper-V Isolation VM interface with paravisor and hypervisor
 *
 * Author:
 *  Tianyu Lan <Tianyu.Lan@microsoft.com>
 */

#include <linux/bitfield.h>
#include <linux/hyperv.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <asm/svm.h>
#include <asm/sev.h>
#include <asm/io.h>
#include <asm/coco.h>
#include <asm/mem_encrypt.h>
#include <asm/set_memory.h>
#include <asm/mshyperv.h>
#include <asm/hypervisor.h>
#include <asm/mtrr.h>
#include <asm/io_apic.h>
#include <asm/realmode.h>
#include <asm/e820/api.h>
#include <asm/desc.h>
#include <uapi/asm/vmx.h>

#ifdef CONFIG_AMD_MEM_ENCRYPT

#define GHCB_USAGE_HYPERV_CALL	1

union hv_ghcb {
	struct ghcb ghcb;
	struct {
		u64 hypercalldata[509];
		u64 outputgpa;
		union {
			union {
				struct {
					u32 callcode        : 16;
					u32 isfast          : 1;
					u32 reserved1       : 14;
					u32 isnested        : 1;
					u32 countofelements : 12;
					u32 reserved2       : 4;
					u32 repstartindex   : 12;
					u32 reserved3       : 4;
				};
				u64 asuint64;
			} hypercallinput;
			union {
				struct {
					u16 callstatus;
					u16 reserved1;
					u32 elementsprocessed : 12;
					u32 reserved2         : 20;
				};
				u64 asunit64;
			} hypercalloutput;
		};
		u64 reserved2;
	} hypercall;
} __packed __aligned(HV_HYP_PAGE_SIZE);

/* Only used in an SNP VM with the paravisor */
static u16 hv_ghcb_version __ro_after_init;

/* Functions only used in an SNP VM with the paravisor go here. */
u64 hv_ghcb_hypercall(u64 control, void *input, void *output, u32 input_size)
{
	union hv_ghcb *hv_ghcb;
	void **ghcb_base;
	unsigned long flags;
	u64 status;

	if (!hv_ghcb_pg)
		return -EFAULT;

	WARN_ON(in_nmi());

	local_irq_save(flags);
	ghcb_base = (void **)this_cpu_ptr(hv_ghcb_pg);
	hv_ghcb = (union hv_ghcb *)*ghcb_base;
	if (!hv_ghcb) {
		local_irq_restore(flags);
		return -EFAULT;
	}

	hv_ghcb->ghcb.protocol_version = GHCB_PROTOCOL_MAX;
	hv_ghcb->ghcb.ghcb_usage = GHCB_USAGE_HYPERV_CALL;

	hv_ghcb->hypercall.outputgpa = (u64)output;
	hv_ghcb->hypercall.hypercallinput.asuint64 = 0;
	hv_ghcb->hypercall.hypercallinput.callcode = control;

	if (input_size)
		memcpy(hv_ghcb->hypercall.hypercalldata, input, input_size);

	VMGEXIT();

	hv_ghcb->ghcb.ghcb_usage = 0xffffffff;
	memset(hv_ghcb->ghcb.save.valid_bitmap, 0,
	       sizeof(hv_ghcb->ghcb.save.valid_bitmap));

	status = hv_ghcb->hypercall.hypercalloutput.callstatus;

	local_irq_restore(flags);

	return status;
}

static inline u64 rd_ghcb_msr(void)
{
	return __rdmsr(MSR_AMD64_SEV_ES_GHCB);
}

static inline void wr_ghcb_msr(u64 val)
{
	native_wrmsrl(MSR_AMD64_SEV_ES_GHCB, val);
}

static enum es_result hv_ghcb_hv_call(struct ghcb *ghcb, u64 exit_code,
				   u64 exit_info_1, u64 exit_info_2)
{
	/* Fill in protocol and format specifiers */
	ghcb->protocol_version = hv_ghcb_version;
	ghcb->ghcb_usage       = GHCB_DEFAULT_USAGE;

	ghcb_set_sw_exit_code(ghcb, exit_code);
	ghcb_set_sw_exit_info_1(ghcb, exit_info_1);
	ghcb_set_sw_exit_info_2(ghcb, exit_info_2);

	VMGEXIT();

	if (ghcb->save.sw_exit_info_1 & GENMASK_ULL(31, 0))
		return ES_VMM_ERROR;
	else
		return ES_OK;
}

void __noreturn hv_ghcb_terminate(unsigned int set, unsigned int reason)
{
	u64 val = GHCB_MSR_TERM_REQ;

	/* Tell the hypervisor what went wrong. */
	val |= GHCB_SEV_TERM_REASON(set, reason);

	/* Request Guest Termination from Hypervisor */
	wr_ghcb_msr(val);
	VMGEXIT();

	while (true)
		asm volatile("hlt\n" : : : "memory");
}

bool hv_ghcb_negotiate_protocol(void)
{
	u64 ghcb_gpa;
	u64 val;

	/* Save ghcb page gpa. */
	ghcb_gpa = rd_ghcb_msr();

	/* Do the GHCB protocol version negotiation */
	wr_ghcb_msr(GHCB_MSR_SEV_INFO_REQ);
	VMGEXIT();
	val = rd_ghcb_msr();

	if (GHCB_MSR_INFO(val) != GHCB_MSR_SEV_INFO_RESP)
		return false;

	if (GHCB_MSR_PROTO_MAX(val) < GHCB_PROTOCOL_MIN ||
	    GHCB_MSR_PROTO_MIN(val) > GHCB_PROTOCOL_MAX)
		return false;

	hv_ghcb_version = min_t(size_t, GHCB_MSR_PROTO_MAX(val),
			     GHCB_PROTOCOL_MAX);

	/* Write ghcb page back after negotiating protocol. */
	wr_ghcb_msr(ghcb_gpa);
	VMGEXIT();

	return true;
}

static void hv_ghcb_msr_write(u64 msr, u64 value)
{
	union hv_ghcb *hv_ghcb;
	void **ghcb_base;
	unsigned long flags;

	if (!hv_ghcb_pg)
		return;

	WARN_ON(in_nmi());

	local_irq_save(flags);
	ghcb_base = (void **)this_cpu_ptr(hv_ghcb_pg);
	hv_ghcb = (union hv_ghcb *)*ghcb_base;
	if (!hv_ghcb) {
		local_irq_restore(flags);
		return;
	}

	ghcb_set_rcx(&hv_ghcb->ghcb, msr);
	ghcb_set_rax(&hv_ghcb->ghcb, lower_32_bits(value));
	ghcb_set_rdx(&hv_ghcb->ghcb, upper_32_bits(value));

	if (hv_ghcb_hv_call(&hv_ghcb->ghcb, SVM_EXIT_MSR, 1, 0))
		pr_warn("Fail to write msr via ghcb %llx.\n", msr);

	local_irq_restore(flags);
}

static void hv_ghcb_msr_read(u64 msr, u64 *value)
{
	union hv_ghcb *hv_ghcb;
	void **ghcb_base;
	unsigned long flags;

	/* Check size of union hv_ghcb here. */
	BUILD_BUG_ON(sizeof(union hv_ghcb) != HV_HYP_PAGE_SIZE);

	if (!hv_ghcb_pg)
		return;

	WARN_ON(in_nmi());

	local_irq_save(flags);
	ghcb_base = (void **)this_cpu_ptr(hv_ghcb_pg);
	hv_ghcb = (union hv_ghcb *)*ghcb_base;
	if (!hv_ghcb) {
		local_irq_restore(flags);
		return;
	}

	ghcb_set_rcx(&hv_ghcb->ghcb, msr);
	if (hv_ghcb_hv_call(&hv_ghcb->ghcb, SVM_EXIT_MSR, 0, 0))
		pr_warn("Fail to read msr via ghcb %llx.\n", msr);
	else
		*value = (u64)lower_32_bits(hv_ghcb->ghcb.save.rax)
			| ((u64)lower_32_bits(hv_ghcb->ghcb.save.rdx) << 32);
	local_irq_restore(flags);
}

/* Only used in a fully enlightened SNP VM, i.e. without the paravisor */
static u8 ap_start_input_arg[PAGE_SIZE] __bss_decrypted __aligned(PAGE_SIZE);
static u8 ap_start_stack[PAGE_SIZE] __aligned(PAGE_SIZE);
static DEFINE_PER_CPU(struct sev_es_save_area *, hv_sev_vmsa);

/* Functions only used in an SNP VM without the paravisor go here. */

#define hv_populate_vmcb_seg(seg, gdtr_base)			\
do {								\
	if (seg.selector) {					\
		seg.base = 0;					\
		seg.limit = HV_AP_SEGMENT_LIMIT;		\
		seg.attrib = *(u16 *)(gdtr_base + seg.selector + 5);	\
		seg.attrib = (seg.attrib & 0xFF) | ((seg.attrib >> 4) & 0xF00); \
	}							\
} while (0)							\

static int snp_set_vmsa(void *va, bool vmsa)
{
	u64 attrs;

	/*
	 * Running at VMPL0 allows the kernel to change the VMSA bit for a page
	 * using the RMPADJUST instruction. However, for the instruction to
	 * succeed it must target the permissions of a lesser privileged
	 * (higher numbered) VMPL level, so use VMPL1 (refer to the RMPADJUST
	 * instruction in the AMD64 APM Volume 3).
	 */
	attrs = 1;
	if (vmsa)
		attrs |= RMPADJUST_VMSA_PAGE_BIT;

	return rmpadjust((unsigned long)va, RMP_PG_SIZE_4K, attrs);
}

static void snp_cleanup_vmsa(struct sev_es_save_area *vmsa)
{
	int err;

	err = snp_set_vmsa(vmsa, false);
	if (err)
		pr_err("clear VMSA page failed (%u), leaking page\n", err);
	else
		free_page((unsigned long)vmsa);
}

int hv_snp_boot_ap(u32 cpu, unsigned long start_ip)
{
	struct sev_es_save_area *vmsa = (struct sev_es_save_area *)
		__get_free_page(GFP_KERNEL | __GFP_ZERO);
	struct sev_es_save_area *cur_vmsa;
	struct desc_ptr gdtr;
	u64 ret, retry = 5;
	struct hv_enable_vp_vtl *start_vp_input;
	unsigned long flags;

	if (!vmsa)
		return -ENOMEM;

	native_store_gdt(&gdtr);

	vmsa->gdtr.base = gdtr.address;
	vmsa->gdtr.limit = gdtr.size;

	asm volatile("movl %%es, %%eax;" : "=a" (vmsa->es.selector));
	hv_populate_vmcb_seg(vmsa->es, vmsa->gdtr.base);

	asm volatile("movl %%cs, %%eax;" : "=a" (vmsa->cs.selector));
	hv_populate_vmcb_seg(vmsa->cs, vmsa->gdtr.base);

	asm volatile("movl %%ss, %%eax;" : "=a" (vmsa->ss.selector));
	hv_populate_vmcb_seg(vmsa->ss, vmsa->gdtr.base);

	asm volatile("movl %%ds, %%eax;" : "=a" (vmsa->ds.selector));
	hv_populate_vmcb_seg(vmsa->ds, vmsa->gdtr.base);

	vmsa->efer = native_read_msr(MSR_EFER);

	asm volatile("movq %%cr4, %%rax;" : "=a" (vmsa->cr4));
	asm volatile("movq %%cr3, %%rax;" : "=a" (vmsa->cr3));
	asm volatile("movq %%cr0, %%rax;" : "=a" (vmsa->cr0));

	vmsa->xcr0 = 1;
	vmsa->g_pat = HV_AP_INIT_GPAT_DEFAULT;
	vmsa->rip = (u64)secondary_startup_64_no_verify;
	vmsa->rsp = (u64)&ap_start_stack[PAGE_SIZE];

	/*
	 * Set the SNP-specific fields for this VMSA:
	 *   VMPL level
	 *   SEV_FEATURES (matches the SEV STATUS MSR right shifted 2 bits)
	 */
	vmsa->vmpl = 0;
	vmsa->sev_features = sev_status >> 2;

	ret = snp_set_vmsa(vmsa, true);
	if (!ret) {
		pr_err("RMPADJUST(%llx) failed: %llx\n", (u64)vmsa, ret);
		free_page((u64)vmsa);
		return ret;
	}

	local_irq_save(flags);
	start_vp_input = (struct hv_enable_vp_vtl *)ap_start_input_arg;
	memset(start_vp_input, 0, sizeof(*start_vp_input));
	start_vp_input->partition_id = -1;
	start_vp_input->vp_index = cpu;
	start_vp_input->target_vtl.target_vtl = ms_hyperv.vtl;
	*(u64 *)&start_vp_input->vp_context = __pa(vmsa) | 1;

	do {
		ret = hv_do_hypercall(HVCALL_START_VP,
				      start_vp_input, NULL);
	} while (hv_result(ret) == HV_STATUS_TIME_OUT && retry--);

	local_irq_restore(flags);

	if (!hv_result_success(ret)) {
		pr_err("HvCallStartVirtualProcessor failed: %llx\n", ret);
		snp_cleanup_vmsa(vmsa);
		vmsa = NULL;
	}

	cur_vmsa = per_cpu(hv_sev_vmsa, cpu);
	/* Free up any previous VMSA page */
	if (cur_vmsa)
		snp_cleanup_vmsa(cur_vmsa);

	/* Record the current VMSA page */
	per_cpu(hv_sev_vmsa, cpu) = vmsa;

	return ret;
}

#else
static inline void hv_ghcb_msr_write(u64 msr, u64 value) {}
static inline void hv_ghcb_msr_read(u64 msr, u64 *value) {}
#endif /* CONFIG_AMD_MEM_ENCRYPT */

#ifdef CONFIG_INTEL_TDX_GUEST
static void hv_tdx_msr_write(u64 msr, u64 val)
{
	struct tdx_module_args args = {
		.r10 = TDX_HYPERCALL_STANDARD,
		.r11 = EXIT_REASON_MSR_WRITE,
		.r12 = msr,
		.r13 = val,
	};

	u64 ret = __tdx_hypercall(&args);

	WARN_ONCE(ret, "Failed to emulate MSR write: %lld\n", ret);
}

static void hv_tdx_msr_read(u64 msr, u64 *val)
{
	struct tdx_module_args args = {
		.r10 = TDX_HYPERCALL_STANDARD,
		.r11 = EXIT_REASON_MSR_READ,
		.r12 = msr,
	};

	u64 ret = __tdx_hypercall(&args);

	if (WARN_ONCE(ret, "Failed to emulate MSR read: %lld\n", ret))
		*val = 0;
	else
		*val = args.r11;
}

u64 hv_tdx_hypercall(u64 control, u64 param1, u64 param2)
{
	struct tdx_module_args args = { };

	args.r10 = control;
	args.rdx = param1;
	args.r8  = param2;

	(void)__tdx_hypercall(&args);

	return args.r11;
}

#else
static inline void hv_tdx_msr_write(u64 msr, u64 value) {}
static inline void hv_tdx_msr_read(u64 msr, u64 *value) {}
#endif /* CONFIG_INTEL_TDX_GUEST */

#if defined(CONFIG_AMD_MEM_ENCRYPT) || defined(CONFIG_INTEL_TDX_GUEST)
void hv_ivm_msr_write(u64 msr, u64 value)
{
	if (!ms_hyperv.paravisor_present)
		return;

	if (hv_isolation_type_tdx())
		hv_tdx_msr_write(msr, value);
	else if (hv_isolation_type_snp())
		hv_ghcb_msr_write(msr, value);
}

void hv_ivm_msr_read(u64 msr, u64 *value)
{
	if (!ms_hyperv.paravisor_present)
		return;

	if (hv_isolation_type_tdx())
		hv_tdx_msr_read(msr, value);
	else if (hv_isolation_type_snp())
		hv_ghcb_msr_read(msr, value);
}

/*
 * hv_mark_gpa_visibility - Set pages visible to host via hvcall.
 *
 * In Isolation VM, all guest memory is encrypted from host and guest
 * needs to set memory visible to host via hvcall before sharing memory
 * with host.
 */
static int hv_mark_gpa_visibility(u16 count, const u64 pfn[],
			   enum hv_mem_host_visibility visibility)
{
	struct hv_gpa_range_for_visibility *input;
	u16 pages_processed;
	u64 hv_status;
	unsigned long flags;

	/* no-op if partition isolation is not enabled */
	if (!hv_is_isolation_supported())
		return 0;

	if (count > HV_MAX_MODIFY_GPA_REP_COUNT) {
		pr_err("Hyper-V: GPA count:%d exceeds supported:%lu\n", count,
			HV_MAX_MODIFY_GPA_REP_COUNT);
		return -EINVAL;
	}

	local_irq_save(flags);
	input = *this_cpu_ptr(hyperv_pcpu_input_arg);

	if (unlikely(!input)) {
		local_irq_restore(flags);
		return -EINVAL;
	}

	input->partition_id = HV_PARTITION_ID_SELF;
	input->host_visibility = visibility;
	input->reserved0 = 0;
	input->reserved1 = 0;
	memcpy((void *)input->gpa_page_list, pfn, count * sizeof(*pfn));
	hv_status = hv_do_rep_hypercall(
			HVCALL_MODIFY_SPARSE_GPA_PAGE_HOST_VISIBILITY, count,
			0, input, &pages_processed);
	local_irq_restore(flags);

	if (hv_result_success(hv_status))
		return 0;
	else
		return -EFAULT;
}

/*
 * When transitioning memory between encrypted and decrypted, the caller
 * of set_memory_encrypted() or set_memory_decrypted() is responsible for
 * ensuring that the memory isn't in use and isn't referenced while the
 * transition is in progress.  The transition has multiple steps, and the
 * memory is in an inconsistent state until all steps are complete. A
 * reference while the state is inconsistent could result in an exception
 * that can't be cleanly fixed up.
 *
 * But the Linux kernel load_unaligned_zeropad() mechanism could cause a
 * stray reference that can't be prevented by the caller, so Linux has
 * specific code to handle this case. But when the #VC and #VE exceptions
 * routed to a paravisor, the specific code doesn't work. To avoid this
 * problem, mark the pages as "not present" while the transition is in
 * progress. If load_unaligned_zeropad() causes a stray reference, a normal
 * page fault is generated instead of #VC or #VE, and the page-fault-based
 * handlers for load_unaligned_zeropad() resolve the reference.  When the
 * transition is complete, hv_vtom_set_host_visibility() marks the pages
 * as "present" again.
 */
static int hv_vtom_clear_present(unsigned long kbuffer, int pagecount, bool enc)
{
	return set_memory_np(kbuffer, pagecount);
}

/*
 * hv_vtom_set_host_visibility - Set specified memory visible to host.
 *
 * In Isolation VM, all guest memory is encrypted from host and guest
 * needs to set memory visible to host via hvcall before sharing memory
 * with host. This function works as wrap of hv_mark_gpa_visibility()
 * with memory base and size.
 */
static int hv_vtom_set_host_visibility(unsigned long kbuffer, int pagecount, bool enc)
{
	enum hv_mem_host_visibility visibility = enc ?
			VMBUS_PAGE_NOT_VISIBLE : VMBUS_PAGE_VISIBLE_READ_WRITE;
	u64 *pfn_array;
	phys_addr_t paddr;
	int i, pfn, err;
	void *vaddr;
	int ret = 0;

	pfn_array = kmalloc(HV_HYP_PAGE_SIZE, GFP_KERNEL);
	if (!pfn_array) {
		ret = -ENOMEM;
		goto err_set_memory_p;
	}

	for (i = 0, pfn = 0; i < pagecount; i++) {
		/*
		 * Use slow_virt_to_phys() because the PRESENT bit has been
		 * temporarily cleared in the PTEs.  slow_virt_to_phys() works
		 * without the PRESENT bit while virt_to_hvpfn() or similar
		 * does not.
		 */
		vaddr = (void *)kbuffer + (i * HV_HYP_PAGE_SIZE);
		paddr = slow_virt_to_phys(vaddr);
		pfn_array[pfn] = paddr >> HV_HYP_PAGE_SHIFT;
		pfn++;

		if (pfn == HV_MAX_MODIFY_GPA_REP_COUNT || i == pagecount - 1) {
			ret = hv_mark_gpa_visibility(pfn, pfn_array,
						     visibility);
			if (ret)
				goto err_free_pfn_array;
			pfn = 0;
		}
	}

err_free_pfn_array:
	kfree(pfn_array);

err_set_memory_p:
	/*
	 * Set the PTE PRESENT bits again to revert what hv_vtom_clear_present()
	 * did. Do this even if there is an error earlier in this function in
	 * order to avoid leaving the memory range in a "broken" state. Setting
	 * the PRESENT bits shouldn't fail, but return an error if it does.
	 */
	err = set_memory_p(kbuffer, pagecount);
	if (err && !ret)
		ret = err;

	return ret;
}

static bool hv_vtom_tlb_flush_required(bool private)
{
	/*
	 * Since hv_vtom_clear_present() marks the PTEs as "not present"
	 * and flushes the TLB, they can't be in the TLB. That makes the
	 * flush controlled by this function redundant, so return "false".
	 */
	return false;
}

static bool hv_vtom_cache_flush_required(void)
{
	return false;
}

static bool hv_is_private_mmio(u64 addr)
{
	/*
	 * Hyper-V always provides a single IO-APIC in a guest VM.
	 * When a paravisor is used, it is emulated by the paravisor
	 * in the guest context and must be mapped private.
	 */
	if (addr >= HV_IOAPIC_BASE_ADDRESS &&
	    addr < (HV_IOAPIC_BASE_ADDRESS + PAGE_SIZE))
		return true;

	/* Same with a vTPM */
	if (addr >= VTPM_BASE_ADDRESS &&
	    addr < (VTPM_BASE_ADDRESS + PAGE_SIZE))
		return true;

	return false;
}

void __init hv_vtom_init(void)
{
	enum hv_isolation_type type = hv_get_isolation_type();

	switch (type) {
	case HV_ISOLATION_TYPE_VBS:
		fallthrough;
	/*
	 * By design, a VM using vTOM doesn't see the SEV setting,
	 * so SEV initialization is bypassed and sev_status isn't set.
	 * Set it here to indicate a vTOM VM.
	 *
	 * Note: if CONFIG_AMD_MEM_ENCRYPT is not set, sev_status is
	 * defined as 0ULL, to which we can't assigned a value.
	 */
#ifdef CONFIG_AMD_MEM_ENCRYPT
	case HV_ISOLATION_TYPE_SNP:
		sev_status = MSR_AMD64_SNP_VTOM;
		cc_vendor = CC_VENDOR_AMD;
		break;
#endif

	case HV_ISOLATION_TYPE_TDX:
		cc_vendor = CC_VENDOR_INTEL;
		break;

	default:
		panic("hv_vtom_init: unsupported isolation type %d\n", type);
	}

	cc_set_mask(ms_hyperv.shared_gpa_boundary);
	physical_mask &= ms_hyperv.shared_gpa_boundary - 1;

	x86_platform.hyper.is_private_mmio = hv_is_private_mmio;
	x86_platform.guest.enc_cache_flush_required = hv_vtom_cache_flush_required;
	x86_platform.guest.enc_tlb_flush_required = hv_vtom_tlb_flush_required;
	x86_platform.guest.enc_status_change_prepare = hv_vtom_clear_present;
	x86_platform.guest.enc_status_change_finish = hv_vtom_set_host_visibility;

	/* Set WB as the default cache mode. */
	mtrr_overwrite_state(NULL, 0, MTRR_TYPE_WRBACK);
}

#endif /* defined(CONFIG_AMD_MEM_ENCRYPT) || defined(CONFIG_INTEL_TDX_GUEST) */

enum hv_isolation_type hv_get_isolation_type(void)
{
	if (!(ms_hyperv.priv_high & HV_ISOLATION))
		return HV_ISOLATION_TYPE_NONE;
	return FIELD_GET(HV_ISOLATION_TYPE, ms_hyperv.isolation_config_b);
}
EXPORT_SYMBOL_GPL(hv_get_isolation_type);

/*
 * hv_is_isolation_supported - Check system runs in the Hyper-V
 * isolation VM.
 */
bool hv_is_isolation_supported(void)
{
	if (!cpu_feature_enabled(X86_FEATURE_HYPERVISOR))
		return false;

	if (!hypervisor_is_type(X86_HYPER_MS_HYPERV))
		return false;

	return hv_get_isolation_type() != HV_ISOLATION_TYPE_NONE;
}

DEFINE_STATIC_KEY_FALSE(isolation_type_snp);

/*
 * hv_isolation_type_snp - Check if the system runs in an AMD SEV-SNP based
 * isolation VM.
 */
bool hv_isolation_type_snp(void)
{
	return static_branch_unlikely(&isolation_type_snp);
}

DEFINE_STATIC_KEY_FALSE(isolation_type_tdx);
/*
 * hv_isolation_type_tdx - Check if the system runs in an Intel TDX based
 * isolated VM.
 */
bool hv_isolation_type_tdx(void)
{
	return static_branch_unlikely(&isolation_type_tdx);
}
