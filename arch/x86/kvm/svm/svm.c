#define pr_fmt(fmt) "SVM: " fmt

#include <linux/kvm_host.h>

#include "irq.h"
#include "mmu.h"
#include "kvm_cache_regs.h"
#include "x86.h"
#include "cpuid.h"
#include "pmu.h"

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/amd-iommu.h>
#include <linux/sched.h>
#include <linux/trace_events.h>
#include <linux/slab.h>
#include <linux/hashtable.h>
#include <linux/frame.h>
#include <linux/psp-sev.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/rwsem.h>

#include <asm/apic.h>
#include <asm/perf_event.h>
#include <asm/tlbflush.h>
#include <asm/desc.h>
#include <asm/debugreg.h>
#include <asm/kvm_para.h>
#include <asm/irq_remapping.h>
#include <asm/spec-ctrl.h>
#include <asm/cpu_device_id.h>

#include <asm/virtext.h>
#include "trace.h"

#include "svm.h"

#define __ex(x) __kvm_handle_fault_on_reboot(x)

MODULE_AUTHOR("Qumranet");
MODULE_LICENSE("GPL");

#ifdef MODULE
static const struct x86_cpu_id svm_cpu_id[] = {
	X86_MATCH_FEATURE(X86_FEATURE_SVM, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, svm_cpu_id);
#endif

#define IOPM_ALLOC_ORDER 2
#define MSRPM_ALLOC_ORDER 1

#define SEG_TYPE_LDT 2
#define SEG_TYPE_BUSY_TSS16 3

#define SVM_FEATURE_LBRV           (1 <<  1)
#define SVM_FEATURE_SVML           (1 <<  2)
#define SVM_FEATURE_TSC_RATE       (1 <<  4)
#define SVM_FEATURE_VMCB_CLEAN     (1 <<  5)
#define SVM_FEATURE_FLUSH_ASID     (1 <<  6)
#define SVM_FEATURE_DECODE_ASSIST  (1 <<  7)
#define SVM_FEATURE_PAUSE_FILTER   (1 << 10)

#define DEBUGCTL_RESERVED_BITS (~(0x3fULL))

#define TSC_RATIO_RSVD          0xffffff0000000000ULL
#define TSC_RATIO_MIN		0x0000000000000001ULL
#define TSC_RATIO_MAX		0x000000ffffffffffULL

static bool erratum_383_found __read_mostly;

u32 msrpm_offsets[MSRPM_OFFSETS] __read_mostly;

/*
 * Set osvw_len to higher value when updated Revision Guides
 * are published and we know what the new status bits are
 */
static uint64_t osvw_len = 4, osvw_status;

static DEFINE_PER_CPU(u64, current_tsc_ratio);
#define TSC_RATIO_DEFAULT	0x0100000000ULL

static const struct svm_direct_access_msrs {
	u32 index;   /* Index of the MSR */
	bool always; /* True if intercept is always on */
} direct_access_msrs[] = {
	{ .index = MSR_STAR,				.always = true  },
	{ .index = MSR_IA32_SYSENTER_CS,		.always = true  },
#ifdef CONFIG_X86_64
	{ .index = MSR_GS_BASE,				.always = true  },
	{ .index = MSR_FS_BASE,				.always = true  },
	{ .index = MSR_KERNEL_GS_BASE,			.always = true  },
	{ .index = MSR_LSTAR,				.always = true  },
	{ .index = MSR_CSTAR,				.always = true  },
	{ .index = MSR_SYSCALL_MASK,			.always = true  },
#endif
	{ .index = MSR_IA32_SPEC_CTRL,			.always = false },
	{ .index = MSR_IA32_PRED_CMD,			.always = false },
	{ .index = MSR_IA32_LASTBRANCHFROMIP,		.always = false },
	{ .index = MSR_IA32_LASTBRANCHTOIP,		.always = false },
	{ .index = MSR_IA32_LASTINTFROMIP,		.always = false },
	{ .index = MSR_IA32_LASTINTTOIP,		.always = false },
	{ .index = MSR_INVALID,				.always = false },
};

/* enable NPT for AMD64 and X86 with PAE */
#if defined(CONFIG_X86_64) || defined(CONFIG_X86_PAE)
bool npt_enabled = true;
#else
bool npt_enabled;
#endif

/*
 * These 2 parameters are used to config the controls for Pause-Loop Exiting:
 * pause_filter_count: On processors that support Pause filtering(indicated
 *	by CPUID Fn8000_000A_EDX), the VMCB provides a 16 bit pause filter
 *	count value. On VMRUN this value is loaded into an internal counter.
 *	Each time a pause instruction is executed, this counter is decremented
 *	until it reaches zero at which time a #VMEXIT is generated if pause
 *	intercept is enabled. Refer to  AMD APM Vol 2 Section 15.14.4 Pause
 *	Intercept Filtering for more details.
 *	This also indicate if ple logic enabled.
 *
 * pause_filter_thresh: In addition, some processor families support advanced
 *	pause filtering (indicated by CPUID Fn8000_000A_EDX) upper bound on
 *	the amount of time a guest is allowed to execute in a pause loop.
 *	In this mode, a 16-bit pause filter threshold field is added in the
 *	VMCB. The threshold value is a cycle count that is used to reset the
 *	pause counter. As with simple pause filtering, VMRUN loads the pause
 *	count value from VMCB into an internal counter. Then, on each pause
 *	instruction the hardware checks the elapsed number of cycles since
 *	the most recent pause instruction against the pause filter threshold.
 *	If the elapsed cycle count is greater than the pause filter threshold,
 *	then the internal pause count is reloaded from the VMCB and execution
 *	continues. If the elapsed cycle count is less than the pause filter
 *	threshold, then the internal pause count is decremented. If the count
 *	value is less than zero and PAUSE intercept is enabled, a #VMEXIT is
 *	triggered. If advanced pause filtering is supported and pause filter
 *	threshold field is set to zero, the filter will operate in the simpler,
 *	count only mode.
 */

static unsigned short pause_filter_thresh = KVM_DEFAULT_PLE_GAP;
module_param(pause_filter_thresh, ushort, 0444);

static unsigned short pause_filter_count = KVM_SVM_DEFAULT_PLE_WINDOW;
module_param(pause_filter_count, ushort, 0444);

/* Default doubles per-vcpu window every exit. */
static unsigned short pause_filter_count_grow = KVM_DEFAULT_PLE_WINDOW_GROW;
module_param(pause_filter_count_grow, ushort, 0444);

/* Default resets per-vcpu window every exit to pause_filter_count. */
static unsigned short pause_filter_count_shrink = KVM_DEFAULT_PLE_WINDOW_SHRINK;
module_param(pause_filter_count_shrink, ushort, 0444);

/* Default is to compute the maximum so we can never overflow. */
static unsigned short pause_filter_count_max = KVM_SVM_DEFAULT_PLE_WINDOW_MAX;
module_param(pause_filter_count_max, ushort, 0444);

/* allow nested paging (virtualized MMU) for all guests */
static int npt = true;
module_param(npt, int, S_IRUGO);

/* allow nested virtualization in KVM/SVM */
static int nested = true;
module_param(nested, int, S_IRUGO);

/* enable/disable Next RIP Save */
static int nrips = true;
module_param(nrips, int, 0444);

/* enable/disable Virtual VMLOAD VMSAVE */
static int vls = true;
module_param(vls, int, 0444);

/* enable/disable Virtual GIF */
static int vgif = true;
module_param(vgif, int, 0444);

/* enable/disable SEV support */
static int sev = IS_ENABLED(CONFIG_AMD_MEM_ENCRYPT_ACTIVE_BY_DEFAULT);
module_param(sev, int, 0444);

static bool __read_mostly dump_invalid_vmcb = 0;
module_param(dump_invalid_vmcb, bool, 0644);

static u8 rsm_ins_bytes[] = "\x0f\xaa";

static void svm_complete_interrupts(struct vcpu_svm *svm);

static unsigned long iopm_base;

struct kvm_ldttss_desc {
	u16 limit0;
	u16 base0;
	unsigned base1:8, type:5, dpl:2, p:1;
	unsigned limit1:4, zero0:3, g:1, base2:8;
	u32 base3;
	u32 zero1;
} __attribute__((packed));

DEFINE_PER_CPU(struct svm_cpu_data *, svm_data);

static const u32 msrpm_ranges[] = {0, 0xc0000000, 0xc0010000};

#define NUM_MSR_MAPS ARRAY_SIZE(msrpm_ranges)
#define MSRS_RANGE_SIZE 2048
#define MSRS_IN_RANGE (MSRS_RANGE_SIZE * 8 / 2)

u32 svm_msrpm_offset(u32 msr)
{
	u32 offset;
	int i;

	for (i = 0; i < NUM_MSR_MAPS; i++) {
		if (msr < msrpm_ranges[i] ||
		    msr >= msrpm_ranges[i] + MSRS_IN_RANGE)
			continue;

		offset  = (msr - msrpm_ranges[i]) / 4; /* 4 msrs per u8 */
		offset += (i * MSRS_RANGE_SIZE);       /* add range offset */

		/* Now we have the u8 offset - but need the u32 offset */
		return offset / 4;
	}

	/* MSR not in any range */
	return MSR_INVALID;
}

#define MAX_INST_SIZE 15

static inline void clgi(void)
{
	asm volatile (__ex("clgi"));
}

static inline void stgi(void)
{
	asm volatile (__ex("stgi"));
}

static inline void invlpga(unsigned long addr, u32 asid)
{
	asm volatile (__ex("invlpga %1, %0") : : "c"(asid), "a"(addr));
}

static int get_npt_level(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_X86_64
	return PT64_ROOT_4LEVEL;
#else
	return PT32E_ROOT_LEVEL;
#endif
}

void svm_set_efer(struct kvm_vcpu *vcpu, u64 efer)
{
	vcpu->arch.efer = efer;

	if (!npt_enabled) {
		/* Shadow paging assumes NX to be available.  */
		efer |= EFER_NX;

		if (!(efer & EFER_LMA))
			efer &= ~EFER_LME;
	}

	to_svm(vcpu)->vmcb->save.efer = efer | EFER_SVME;
	mark_dirty(to_svm(vcpu)->vmcb, VMCB_CR);
}

static int is_external_interrupt(u32 info)
{
	info &= SVM_EVTINJ_TYPE_MASK | SVM_EVTINJ_VALID;
	return info == (SVM_EVTINJ_VALID | SVM_EVTINJ_TYPE_INTR);
}

static u32 svm_get_interrupt_shadow(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u32 ret = 0;

	if (svm->vmcb->control.int_state & SVM_INTERRUPT_SHADOW_MASK)
		ret = KVM_X86_SHADOW_INT_STI | KVM_X86_SHADOW_INT_MOV_SS;
	return ret;
}

static void svm_set_interrupt_shadow(struct kvm_vcpu *vcpu, int mask)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (mask == 0)
		svm->vmcb->control.int_state &= ~SVM_INTERRUPT_SHADOW_MASK;
	else
		svm->vmcb->control.int_state |= SVM_INTERRUPT_SHADOW_MASK;

}

static int skip_emulated_instruction(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (nrips && svm->vmcb->control.next_rip != 0) {
		WARN_ON_ONCE(!static_cpu_has(X86_FEATURE_NRIPS));
		svm->next_rip = svm->vmcb->control.next_rip;
	}

	if (!svm->next_rip) {
		if (!kvm_emulate_instruction(vcpu, EMULTYPE_SKIP))
			return 0;
	} else {
		if (svm->next_rip - kvm_rip_read(vcpu) > MAX_INST_SIZE)
			pr_err("%s: ip 0x%lx next 0x%llx\n",
			       __func__, kvm_rip_read(vcpu), svm->next_rip);
		kvm_rip_write(vcpu, svm->next_rip);
	}
	svm_set_interrupt_shadow(vcpu, 0);

	return 1;
}

static void svm_queue_exception(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	unsigned nr = vcpu->arch.exception.nr;
	bool has_error_code = vcpu->arch.exception.has_error_code;
	bool reinject = vcpu->arch.exception.injected;
	u32 error_code = vcpu->arch.exception.error_code;

	/*
	 * If we are within a nested VM we'd better #VMEXIT and let the guest
	 * handle the exception
	 */
	if (!reinject &&
	    nested_svm_check_exception(svm, nr, has_error_code, error_code))
		return;

	kvm_deliver_exception_payload(&svm->vcpu);

	if (nr == BP_VECTOR && !nrips) {
		unsigned long rip, old_rip = kvm_rip_read(&svm->vcpu);

		/*
		 * For guest debugging where we have to reinject #BP if some
		 * INT3 is guest-owned:
		 * Emulate nRIP by moving RIP forward. Will fail if injection
		 * raises a fault that is not intercepted. Still better than
		 * failing in all cases.
		 */
		(void)skip_emulated_instruction(&svm->vcpu);
		rip = kvm_rip_read(&svm->vcpu);
		svm->int3_rip = rip + svm->vmcb->save.cs.base;
		svm->int3_injected = rip - old_rip;
	}

	svm->vmcb->control.event_inj = nr
		| SVM_EVTINJ_VALID
		| (has_error_code ? SVM_EVTINJ_VALID_ERR : 0)
		| SVM_EVTINJ_TYPE_EXEPT;
	svm->vmcb->control.event_inj_err = error_code;
}

static void svm_init_erratum_383(void)
{
	u32 low, high;
	int err;
	u64 val;

	if (!static_cpu_has_bug(X86_BUG_AMD_TLB_MMATCH))
		return;

	/* Use _safe variants to not break nested virtualization */
	val = native_read_msr_safe(MSR_AMD64_DC_CFG, &err);
	if (err)
		return;

	val |= (1ULL << 47);

	low  = lower_32_bits(val);
	high = upper_32_bits(val);

	native_write_msr_safe(MSR_AMD64_DC_CFG, low, high);

	erratum_383_found = true;
}

static void svm_init_osvw(struct kvm_vcpu *vcpu)
{
	/*
	 * Guests should see errata 400 and 415 as fixed (assuming that
	 * HLT and IO instructions are intercepted).
	 */
	vcpu->arch.osvw.length = (osvw_len >= 3) ? (osvw_len) : 3;
	vcpu->arch.osvw.status = osvw_status & ~(6ULL);

	/*
	 * By increasing VCPU's osvw.length to 3 we are telling the guest that
	 * all osvw.status bits inside that length, including bit 0 (which is
	 * reserved for erratum 298), are valid. However, if host processor's
	 * osvw_len is 0 then osvw_status[0] carries no information. We need to
	 * be conservative here and therefore we tell the guest that erratum 298
	 * is present (because we really don't know).
	 */
	if (osvw_len == 0 && boot_cpu_data.x86 == 0x10)
		vcpu->arch.osvw.status |= 1;
}

static int has_svm(void)
{
	const char *msg;

	if (!cpu_has_svm(&msg)) {
		printk(KERN_INFO "has_svm: %s\n", msg);
		return 0;
	}

	return 1;
}

static void svm_hardware_disable(void)
{
	/* Make sure we clean up behind us */
	if (static_cpu_has(X86_FEATURE_TSCRATEMSR))
		wrmsrl(MSR_AMD64_TSC_RATIO, TSC_RATIO_DEFAULT);

	cpu_svm_disable();

	amd_pmu_disable_virt();
}

static int svm_hardware_enable(void)
{

	struct svm_cpu_data *sd;
	uint64_t efer;
	struct desc_struct *gdt;
	int me = raw_smp_processor_id();

	rdmsrl(MSR_EFER, efer);
	if (efer & EFER_SVME)
		return -EBUSY;

	if (!has_svm()) {
		pr_err("%s: err EOPNOTSUPP on %d\n", __func__, me);
		return -EINVAL;
	}
	sd = per_cpu(svm_data, me);
	if (!sd) {
		pr_err("%s: svm_data is NULL on %d\n", __func__, me);
		return -EINVAL;
	}

	sd->asid_generation = 1;
	sd->max_asid = cpuid_ebx(SVM_CPUID_FUNC) - 1;
	sd->next_asid = sd->max_asid + 1;
	sd->min_asid = max_sev_asid + 1;

	gdt = get_current_gdt_rw();
	sd->tss_desc = (struct kvm_ldttss_desc *)(gdt + GDT_ENTRY_TSS);

	wrmsrl(MSR_EFER, efer | EFER_SVME);

	wrmsrl(MSR_VM_HSAVE_PA, page_to_pfn(sd->save_area) << PAGE_SHIFT);

	if (static_cpu_has(X86_FEATURE_TSCRATEMSR)) {
		wrmsrl(MSR_AMD64_TSC_RATIO, TSC_RATIO_DEFAULT);
		__this_cpu_write(current_tsc_ratio, TSC_RATIO_DEFAULT);
	}


	/*
	 * Get OSVW bits.
	 *
	 * Note that it is possible to have a system with mixed processor
	 * revisions and therefore different OSVW bits. If bits are not the same
	 * on different processors then choose the worst case (i.e. if erratum
	 * is present on one processor and not on another then assume that the
	 * erratum is present everywhere).
	 */
	if (cpu_has(&boot_cpu_data, X86_FEATURE_OSVW)) {
		uint64_t len, status = 0;
		int err;

		len = native_read_msr_safe(MSR_AMD64_OSVW_ID_LENGTH, &err);
		if (!err)
			status = native_read_msr_safe(MSR_AMD64_OSVW_STATUS,
						      &err);

		if (err)
			osvw_status = osvw_len = 0;
		else {
			if (len < osvw_len)
				osvw_len = len;
			osvw_status |= status;
			osvw_status &= (1ULL << osvw_len) - 1;
		}
	} else
		osvw_status = osvw_len = 0;

	svm_init_erratum_383();

	amd_pmu_enable_virt();

	return 0;
}

static void svm_cpu_uninit(int cpu)
{
	struct svm_cpu_data *sd = per_cpu(svm_data, raw_smp_processor_id());

	if (!sd)
		return;

	per_cpu(svm_data, raw_smp_processor_id()) = NULL;
	kfree(sd->sev_vmcbs);
	__free_page(sd->save_area);
	kfree(sd);
}

static int svm_cpu_init(int cpu)
{
	struct svm_cpu_data *sd;

	sd = kzalloc(sizeof(struct svm_cpu_data), GFP_KERNEL);
	if (!sd)
		return -ENOMEM;
	sd->cpu = cpu;
	sd->save_area = alloc_page(GFP_KERNEL);
	if (!sd->save_area)
		goto free_cpu_data;

	if (svm_sev_enabled()) {
		sd->sev_vmcbs = kmalloc_array(max_sev_asid + 1,
					      sizeof(void *),
					      GFP_KERNEL);
		if (!sd->sev_vmcbs)
			goto free_save_area;
	}

	per_cpu(svm_data, cpu) = sd;

	return 0;

free_save_area:
	__free_page(sd->save_area);
free_cpu_data:
	kfree(sd);
	return -ENOMEM;

}

static bool valid_msr_intercept(u32 index)
{
	int i;

	for (i = 0; direct_access_msrs[i].index != MSR_INVALID; i++)
		if (direct_access_msrs[i].index == index)
			return true;

	return false;
}

static bool msr_write_intercepted(struct kvm_vcpu *vcpu, unsigned msr)
{
	u8 bit_write;
	unsigned long tmp;
	u32 offset;
	u32 *msrpm;

	msrpm = is_guest_mode(vcpu) ? to_svm(vcpu)->nested.msrpm:
				      to_svm(vcpu)->msrpm;

	offset    = svm_msrpm_offset(msr);
	bit_write = 2 * (msr & 0x0f) + 1;
	tmp       = msrpm[offset];

	BUG_ON(offset == MSR_INVALID);

	return !!test_bit(bit_write,  &tmp);
}

static void set_msr_interception(u32 *msrpm, unsigned msr,
				 int read, int write)
{
	u8 bit_read, bit_write;
	unsigned long tmp;
	u32 offset;

	/*
	 * If this warning triggers extend the direct_access_msrs list at the
	 * beginning of the file
	 */
	WARN_ON(!valid_msr_intercept(msr));

	offset    = svm_msrpm_offset(msr);
	bit_read  = 2 * (msr & 0x0f);
	bit_write = 2 * (msr & 0x0f) + 1;
	tmp       = msrpm[offset];

	BUG_ON(offset == MSR_INVALID);

	read  ? clear_bit(bit_read,  &tmp) : set_bit(bit_read,  &tmp);
	write ? clear_bit(bit_write, &tmp) : set_bit(bit_write, &tmp);

	msrpm[offset] = tmp;
}

static void svm_vcpu_init_msrpm(u32 *msrpm)
{
	int i;

	memset(msrpm, 0xff, PAGE_SIZE * (1 << MSRPM_ALLOC_ORDER));

	for (i = 0; direct_access_msrs[i].index != MSR_INVALID; i++) {
		if (!direct_access_msrs[i].always)
			continue;

		set_msr_interception(msrpm, direct_access_msrs[i].index, 1, 1);
	}
}

static void add_msr_offset(u32 offset)
{
	int i;

	for (i = 0; i < MSRPM_OFFSETS; ++i) {

		/* Offset already in list? */
		if (msrpm_offsets[i] == offset)
			return;

		/* Slot used by another offset? */
		if (msrpm_offsets[i] != MSR_INVALID)
			continue;

		/* Add offset to list */
		msrpm_offsets[i] = offset;

		return;
	}

	/*
	 * If this BUG triggers the msrpm_offsets table has an overflow. Just
	 * increase MSRPM_OFFSETS in this case.
	 */
	BUG();
}

static void init_msrpm_offsets(void)
{
	int i;

	memset(msrpm_offsets, 0xff, sizeof(msrpm_offsets));

	for (i = 0; direct_access_msrs[i].index != MSR_INVALID; i++) {
		u32 offset;

		offset = svm_msrpm_offset(direct_access_msrs[i].index);
		BUG_ON(offset == MSR_INVALID);

		add_msr_offset(offset);
	}
}

static void svm_enable_lbrv(struct vcpu_svm *svm)
{
	u32 *msrpm = svm->msrpm;

	svm->vmcb->control.virt_ext |= LBR_CTL_ENABLE_MASK;
	set_msr_interception(msrpm, MSR_IA32_LASTBRANCHFROMIP, 1, 1);
	set_msr_interception(msrpm, MSR_IA32_LASTBRANCHTOIP, 1, 1);
	set_msr_interception(msrpm, MSR_IA32_LASTINTFROMIP, 1, 1);
	set_msr_interception(msrpm, MSR_IA32_LASTINTTOIP, 1, 1);
}

static void svm_disable_lbrv(struct vcpu_svm *svm)
{
	u32 *msrpm = svm->msrpm;

	svm->vmcb->control.virt_ext &= ~LBR_CTL_ENABLE_MASK;
	set_msr_interception(msrpm, MSR_IA32_LASTBRANCHFROMIP, 0, 0);
	set_msr_interception(msrpm, MSR_IA32_LASTBRANCHTOIP, 0, 0);
	set_msr_interception(msrpm, MSR_IA32_LASTINTFROMIP, 0, 0);
	set_msr_interception(msrpm, MSR_IA32_LASTINTTOIP, 0, 0);
}

void disable_nmi_singlestep(struct vcpu_svm *svm)
{
	svm->nmi_singlestep = false;

	if (!(svm->vcpu.guest_debug & KVM_GUESTDBG_SINGLESTEP)) {
		/* Clear our flags if they were not set by the guest */
		if (!(svm->nmi_singlestep_guest_rflags & X86_EFLAGS_TF))
			svm->vmcb->save.rflags &= ~X86_EFLAGS_TF;
		if (!(svm->nmi_singlestep_guest_rflags & X86_EFLAGS_RF))
			svm->vmcb->save.rflags &= ~X86_EFLAGS_RF;
	}
}

static void grow_ple_window(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb_control_area *control = &svm->vmcb->control;
	int old = control->pause_filter_count;

	control->pause_filter_count = __grow_ple_window(old,
							pause_filter_count,
							pause_filter_count_grow,
							pause_filter_count_max);

	if (control->pause_filter_count != old) {
		mark_dirty(svm->vmcb, VMCB_INTERCEPTS);
		trace_kvm_ple_window_update(vcpu->vcpu_id,
					    control->pause_filter_count, old);
	}
}

static void shrink_ple_window(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb_control_area *control = &svm->vmcb->control;
	int old = control->pause_filter_count;

	control->pause_filter_count =
				__shrink_ple_window(old,
						    pause_filter_count,
						    pause_filter_count_shrink,
						    pause_filter_count);
	if (control->pause_filter_count != old) {
		mark_dirty(svm->vmcb, VMCB_INTERCEPTS);
		trace_kvm_ple_window_update(vcpu->vcpu_id,
					    control->pause_filter_count, old);
	}
}

/*
 * The default MMIO mask is a single bit (excluding the present bit),
 * which could conflict with the memory encryption bit. Check for
 * memory encryption support and override the default MMIO mask if
 * memory encryption is enabled.
 */
static __init void svm_adjust_mmio_mask(void)
{
	unsigned int enc_bit, mask_bit;
	u64 msr, mask;

	/* If there is no memory encryption support, use existing mask */
	if (cpuid_eax(0x80000000) < 0x8000001f)
		return;

	/* If memory encryption is not enabled, use existing mask */
	rdmsrl(MSR_K8_SYSCFG, msr);
	if (!(msr & MSR_K8_SYSCFG_MEM_ENCRYPT))
		return;

	enc_bit = cpuid_ebx(0x8000001f) & 0x3f;
	mask_bit = boot_cpu_data.x86_phys_bits;

	/* Increment the mask bit if it is the same as the encryption bit */
	if (enc_bit == mask_bit)
		mask_bit++;

	/*
	 * If the mask bit location is below 52, then some bits above the
	 * physical addressing limit will always be reserved, so use the
	 * rsvd_bits() function to generate the mask. This mask, along with
	 * the present bit, will be used to generate a page fault with
	 * PFER.RSV = 1.
	 *
	 * If the mask bit location is 52 (or above), then clear the mask.
	 */
	mask = (mask_bit < 52) ? rsvd_bits(mask_bit, 51) | PT_PRESENT_MASK : 0;

	kvm_mmu_set_mmio_spte_mask(mask, mask, PT_WRITABLE_MASK | PT_USER_MASK);
}

static void svm_hardware_teardown(void)
{
	int cpu;

	if (svm_sev_enabled())
		sev_hardware_teardown();

	for_each_possible_cpu(cpu)
		svm_cpu_uninit(cpu);

	__free_pages(pfn_to_page(iopm_base >> PAGE_SHIFT), IOPM_ALLOC_ORDER);
	iopm_base = 0;
}

static __init void svm_set_cpu_caps(void)
{
	kvm_set_cpu_caps();

	supported_xss = 0;

	/* CPUID 0x80000001 and 0x8000000A (SVM features) */
	if (nested) {
		kvm_cpu_cap_set(X86_FEATURE_SVM);

		if (nrips)
			kvm_cpu_cap_set(X86_FEATURE_NRIPS);

		if (npt_enabled)
			kvm_cpu_cap_set(X86_FEATURE_NPT);
	}

	/* CPUID 0x80000008 */
	if (boot_cpu_has(X86_FEATURE_LS_CFG_SSBD) ||
	    boot_cpu_has(X86_FEATURE_AMD_SSBD))
		kvm_cpu_cap_set(X86_FEATURE_VIRT_SSBD);
}

static __init int svm_hardware_setup(void)
{
	int cpu;
	struct page *iopm_pages;
	void *iopm_va;
	int r;

	iopm_pages = alloc_pages(GFP_KERNEL, IOPM_ALLOC_ORDER);

	if (!iopm_pages)
		return -ENOMEM;

	iopm_va = page_address(iopm_pages);
	memset(iopm_va, 0xff, PAGE_SIZE * (1 << IOPM_ALLOC_ORDER));
	iopm_base = page_to_pfn(iopm_pages) << PAGE_SHIFT;

	init_msrpm_offsets();

	supported_xcr0 &= ~(XFEATURE_MASK_BNDREGS | XFEATURE_MASK_BNDCSR);

	if (boot_cpu_has(X86_FEATURE_NX))
		kvm_enable_efer_bits(EFER_NX);

	if (boot_cpu_has(X86_FEATURE_FXSR_OPT))
		kvm_enable_efer_bits(EFER_FFXSR);

	if (boot_cpu_has(X86_FEATURE_TSCRATEMSR)) {
		kvm_has_tsc_control = true;
		kvm_max_tsc_scaling_ratio = TSC_RATIO_MAX;
		kvm_tsc_scaling_ratio_frac_bits = 32;
	}

	/* Check for pause filtering support */
	if (!boot_cpu_has(X86_FEATURE_PAUSEFILTER)) {
		pause_filter_count = 0;
		pause_filter_thresh = 0;
	} else if (!boot_cpu_has(X86_FEATURE_PFTHRESHOLD)) {
		pause_filter_thresh = 0;
	}

	if (nested) {
		printk(KERN_INFO "kvm: Nested Virtualization enabled\n");
		kvm_enable_efer_bits(EFER_SVME | EFER_LMSLE);
	}

	if (sev) {
		if (boot_cpu_has(X86_FEATURE_SEV) &&
		    IS_ENABLED(CONFIG_KVM_AMD_SEV)) {
			r = sev_hardware_setup();
			if (r)
				sev = false;
		} else {
			sev = false;
		}
	}

	svm_adjust_mmio_mask();

	for_each_possible_cpu(cpu) {
		r = svm_cpu_init(cpu);
		if (r)
			goto err;
	}

	if (!boot_cpu_has(X86_FEATURE_NPT))
		npt_enabled = false;

	if (npt_enabled && !npt)
		npt_enabled = false;

	kvm_configure_mmu(npt_enabled, PT_PDPE_LEVEL);
	pr_info("kvm: Nested Paging %sabled\n", npt_enabled ? "en" : "dis");

	if (nrips) {
		if (!boot_cpu_has(X86_FEATURE_NRIPS))
			nrips = false;
	}

	if (avic) {
		if (!npt_enabled ||
		    !boot_cpu_has(X86_FEATURE_AVIC) ||
		    !IS_ENABLED(CONFIG_X86_LOCAL_APIC)) {
			avic = false;
		} else {
			pr_info("AVIC enabled\n");

			amd_iommu_register_ga_log_notifier(&avic_ga_log_notifier);
		}
	}

	if (vls) {
		if (!npt_enabled ||
		    !boot_cpu_has(X86_FEATURE_V_VMSAVE_VMLOAD) ||
		    !IS_ENABLED(CONFIG_X86_64)) {
			vls = false;
		} else {
			pr_info("Virtual VMLOAD VMSAVE supported\n");
		}
	}

	if (vgif) {
		if (!boot_cpu_has(X86_FEATURE_VGIF))
			vgif = false;
		else
			pr_info("Virtual GIF supported\n");
	}

	svm_set_cpu_caps();

	return 0;

err:
	svm_hardware_teardown();
	return r;
}

static void init_seg(struct vmcb_seg *seg)
{
	seg->selector = 0;
	seg->attrib = SVM_SELECTOR_P_MASK | SVM_SELECTOR_S_MASK |
		      SVM_SELECTOR_WRITE_MASK; /* Read/Write Data Segment */
	seg->limit = 0xffff;
	seg->base = 0;
}

static void init_sys_seg(struct vmcb_seg *seg, uint32_t type)
{
	seg->selector = 0;
	seg->attrib = SVM_SELECTOR_P_MASK | type;
	seg->limit = 0xffff;
	seg->base = 0;
}

static u64 svm_read_l1_tsc_offset(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (is_guest_mode(vcpu))
		return svm->nested.hsave->control.tsc_offset;

	return vcpu->arch.tsc_offset;
}

static u64 svm_write_l1_tsc_offset(struct kvm_vcpu *vcpu, u64 offset)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u64 g_tsc_offset = 0;

	if (is_guest_mode(vcpu)) {
		/* Write L1's TSC offset.  */
		g_tsc_offset = svm->vmcb->control.tsc_offset -
			       svm->nested.hsave->control.tsc_offset;
		svm->nested.hsave->control.tsc_offset = offset;
	}

	trace_kvm_write_tsc_offset(vcpu->vcpu_id,
				   svm->vmcb->control.tsc_offset - g_tsc_offset,
				   offset);

	svm->vmcb->control.tsc_offset = offset + g_tsc_offset;

	mark_dirty(svm->vmcb, VMCB_INTERCEPTS);
	return svm->vmcb->control.tsc_offset;
}

static void init_vmcb(struct vcpu_svm *svm)
{
	struct vmcb_control_area *control = &svm->vmcb->control;
	struct vmcb_save_area *save = &svm->vmcb->save;

	svm->vcpu.arch.hflags = 0;

	set_cr_intercept(svm, INTERCEPT_CR0_READ);
	set_cr_intercept(svm, INTERCEPT_CR3_READ);
	set_cr_intercept(svm, INTERCEPT_CR4_READ);
	set_cr_intercept(svm, INTERCEPT_CR0_WRITE);
	set_cr_intercept(svm, INTERCEPT_CR3_WRITE);
	set_cr_intercept(svm, INTERCEPT_CR4_WRITE);
	if (!kvm_vcpu_apicv_active(&svm->vcpu))
		set_cr_intercept(svm, INTERCEPT_CR8_WRITE);

	set_dr_intercepts(svm);

	set_exception_intercept(svm, PF_VECTOR);
	set_exception_intercept(svm, UD_VECTOR);
	set_exception_intercept(svm, MC_VECTOR);
	set_exception_intercept(svm, AC_VECTOR);
	set_exception_intercept(svm, DB_VECTOR);
	/*
	 * Guest access to VMware backdoor ports could legitimately
	 * trigger #GP because of TSS I/O permission bitmap.
	 * We intercept those #GP and allow access to them anyway
	 * as VMware does.
	 */
	if (enable_vmware_backdoor)
		set_exception_intercept(svm, GP_VECTOR);

	set_intercept(svm, INTERCEPT_INTR);
	set_intercept(svm, INTERCEPT_NMI);
	set_intercept(svm, INTERCEPT_SMI);
	set_intercept(svm, INTERCEPT_SELECTIVE_CR0);
	set_intercept(svm, INTERCEPT_RDPMC);
	set_intercept(svm, INTERCEPT_CPUID);
	set_intercept(svm, INTERCEPT_INVD);
	set_intercept(svm, INTERCEPT_INVLPG);
	set_intercept(svm, INTERCEPT_INVLPGA);
	set_intercept(svm, INTERCEPT_IOIO_PROT);
	set_intercept(svm, INTERCEPT_MSR_PROT);
	set_intercept(svm, INTERCEPT_TASK_SWITCH);
	set_intercept(svm, INTERCEPT_SHUTDOWN);
	set_intercept(svm, INTERCEPT_VMRUN);
	set_intercept(svm, INTERCEPT_VMMCALL);
	set_intercept(svm, INTERCEPT_VMLOAD);
	set_intercept(svm, INTERCEPT_VMSAVE);
	set_intercept(svm, INTERCEPT_STGI);
	set_intercept(svm, INTERCEPT_CLGI);
	set_intercept(svm, INTERCEPT_SKINIT);
	set_intercept(svm, INTERCEPT_WBINVD);
	set_intercept(svm, INTERCEPT_XSETBV);
	set_intercept(svm, INTERCEPT_RDPRU);
	set_intercept(svm, INTERCEPT_RSM);

	if (!kvm_mwait_in_guest(svm->vcpu.kvm)) {
		set_intercept(svm, INTERCEPT_MONITOR);
		set_intercept(svm, INTERCEPT_MWAIT);
	}

	if (!kvm_hlt_in_guest(svm->vcpu.kvm))
		set_intercept(svm, INTERCEPT_HLT);

	control->iopm_base_pa = __sme_set(iopm_base);
	control->msrpm_base_pa = __sme_set(__pa(svm->msrpm));
	control->int_ctl = V_INTR_MASKING_MASK;

	init_seg(&save->es);
	init_seg(&save->ss);
	init_seg(&save->ds);
	init_seg(&save->fs);
	init_seg(&save->gs);

	save->cs.selector = 0xf000;
	save->cs.base = 0xffff0000;
	/* Executable/Readable Code Segment */
	save->cs.attrib = SVM_SELECTOR_READ_MASK | SVM_SELECTOR_P_MASK |
		SVM_SELECTOR_S_MASK | SVM_SELECTOR_CODE_MASK;
	save->cs.limit = 0xffff;

	save->gdtr.limit = 0xffff;
	save->idtr.limit = 0xffff;

	init_sys_seg(&save->ldtr, SEG_TYPE_LDT);
	init_sys_seg(&save->tr, SEG_TYPE_BUSY_TSS16);

	svm_set_efer(&svm->vcpu, 0);
	save->dr6 = 0xffff0ff0;
	kvm_set_rflags(&svm->vcpu, 2);
	save->rip = 0x0000fff0;
	svm->vcpu.arch.regs[VCPU_REGS_RIP] = save->rip;

	/*
	 * svm_set_cr0() sets PG and WP and clears NW and CD on save->cr0.
	 * It also updates the guest-visible cr0 value.
	 */
	svm_set_cr0(&svm->vcpu, X86_CR0_NW | X86_CR0_CD | X86_CR0_ET);
	kvm_mmu_reset_context(&svm->vcpu);

	save->cr4 = X86_CR4_PAE;
	/* rdx = ?? */

	if (npt_enabled) {
		/* Setup VMCB for Nested Paging */
		control->nested_ctl |= SVM_NESTED_CTL_NP_ENABLE;
		clr_intercept(svm, INTERCEPT_INVLPG);
		clr_exception_intercept(svm, PF_VECTOR);
		clr_cr_intercept(svm, INTERCEPT_CR3_READ);
		clr_cr_intercept(svm, INTERCEPT_CR3_WRITE);
		save->g_pat = svm->vcpu.arch.pat;
		save->cr3 = 0;
		save->cr4 = 0;
	}
	svm->asid_generation = 0;

	svm->nested.vmcb = 0;
	svm->vcpu.arch.hflags = 0;

	if (pause_filter_count) {
		control->pause_filter_count = pause_filter_count;
		if (pause_filter_thresh)
			control->pause_filter_thresh = pause_filter_thresh;
		set_intercept(svm, INTERCEPT_PAUSE);
	} else {
		clr_intercept(svm, INTERCEPT_PAUSE);
	}

	if (kvm_vcpu_apicv_active(&svm->vcpu))
		avic_init_vmcb(svm);

	/*
	 * If hardware supports Virtual VMLOAD VMSAVE then enable it
	 * in VMCB and clear intercepts to avoid #VMEXIT.
	 */
	if (vls) {
		clr_intercept(svm, INTERCEPT_VMLOAD);
		clr_intercept(svm, INTERCEPT_VMSAVE);
		svm->vmcb->control.virt_ext |= VIRTUAL_VMLOAD_VMSAVE_ENABLE_MASK;
	}

	if (vgif) {
		clr_intercept(svm, INTERCEPT_STGI);
		clr_intercept(svm, INTERCEPT_CLGI);
		svm->vmcb->control.int_ctl |= V_GIF_ENABLE_MASK;
	}

	if (sev_guest(svm->vcpu.kvm)) {
		svm->vmcb->control.nested_ctl |= SVM_NESTED_CTL_SEV_ENABLE;
		clr_exception_intercept(svm, UD_VECTOR);
	}

	mark_all_dirty(svm->vmcb);

	enable_gif(svm);

}

static void svm_vcpu_reset(struct kvm_vcpu *vcpu, bool init_event)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u32 dummy;
	u32 eax = 1;

	svm->spec_ctrl = 0;
	svm->virt_spec_ctrl = 0;

	if (!init_event) {
		svm->vcpu.arch.apic_base = APIC_DEFAULT_PHYS_BASE |
					   MSR_IA32_APICBASE_ENABLE;
		if (kvm_vcpu_is_reset_bsp(&svm->vcpu))
			svm->vcpu.arch.apic_base |= MSR_IA32_APICBASE_BSP;
	}
	init_vmcb(svm);

	kvm_cpuid(vcpu, &eax, &dummy, &dummy, &dummy, false);
	kvm_rdx_write(vcpu, eax);

	if (kvm_vcpu_apicv_active(vcpu) && !init_event)
		avic_update_vapic_bar(svm, APIC_DEFAULT_PHYS_BASE);
}

static int svm_create_vcpu(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm;
	struct page *page;
	struct page *msrpm_pages;
	struct page *hsave_page;
	struct page *nested_msrpm_pages;
	int err;

	BUILD_BUG_ON(offsetof(struct vcpu_svm, vcpu) != 0);
	svm = to_svm(vcpu);

	err = -ENOMEM;
	page = alloc_page(GFP_KERNEL_ACCOUNT);
	if (!page)
		goto out;

	msrpm_pages = alloc_pages(GFP_KERNEL_ACCOUNT, MSRPM_ALLOC_ORDER);
	if (!msrpm_pages)
		goto free_page1;

	nested_msrpm_pages = alloc_pages(GFP_KERNEL_ACCOUNT, MSRPM_ALLOC_ORDER);
	if (!nested_msrpm_pages)
		goto free_page2;

	hsave_page = alloc_page(GFP_KERNEL_ACCOUNT);
	if (!hsave_page)
		goto free_page3;

	err = avic_init_vcpu(svm);
	if (err)
		goto free_page4;

	/* We initialize this flag to true to make sure that the is_running
	 * bit would be set the first time the vcpu is loaded.
	 */
	if (irqchip_in_kernel(vcpu->kvm) && kvm_apicv_activated(vcpu->kvm))
		svm->avic_is_running = true;

	svm->nested.hsave = page_address(hsave_page);

	svm->msrpm = page_address(msrpm_pages);
	svm_vcpu_init_msrpm(svm->msrpm);

	svm->nested.msrpm = page_address(nested_msrpm_pages);
	svm_vcpu_init_msrpm(svm->nested.msrpm);

	svm->vmcb = page_address(page);
	clear_page(svm->vmcb);
	svm->vmcb_pa = __sme_set(page_to_pfn(page) << PAGE_SHIFT);
	svm->asid_generation = 0;
	init_vmcb(svm);

	svm_init_osvw(vcpu);
	vcpu->arch.microcode_version = 0x01000065;

	return 0;

free_page4:
	__free_page(hsave_page);
free_page3:
	__free_pages(nested_msrpm_pages, MSRPM_ALLOC_ORDER);
free_page2:
	__free_pages(msrpm_pages, MSRPM_ALLOC_ORDER);
free_page1:
	__free_page(page);
out:
	return err;
}

static void svm_clear_current_vmcb(struct vmcb *vmcb)
{
	int i;

	for_each_online_cpu(i)
		cmpxchg(&per_cpu(svm_data, i)->current_vmcb, vmcb, NULL);
}

static void svm_free_vcpu(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	/*
	 * The vmcb page can be recycled, causing a false negative in
	 * svm_vcpu_load(). So, ensure that no logical CPU has this
	 * vmcb page recorded as its current vmcb.
	 */
	svm_clear_current_vmcb(svm->vmcb);

	__free_page(pfn_to_page(__sme_clr(svm->vmcb_pa) >> PAGE_SHIFT));
	__free_pages(virt_to_page(svm->msrpm), MSRPM_ALLOC_ORDER);
	__free_page(virt_to_page(svm->nested.hsave));
	__free_pages(virt_to_page(svm->nested.msrpm), MSRPM_ALLOC_ORDER);
}

static void svm_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct svm_cpu_data *sd = per_cpu(svm_data, cpu);
	int i;

	if (unlikely(cpu != vcpu->cpu)) {
		svm->asid_generation = 0;
		mark_all_dirty(svm->vmcb);
	}

#ifdef CONFIG_X86_64
	rdmsrl(MSR_GS_BASE, to_svm(vcpu)->host.gs_base);
#endif
	savesegment(fs, svm->host.fs);
	savesegment(gs, svm->host.gs);
	svm->host.ldt = kvm_read_ldt();

	for (i = 0; i < NR_HOST_SAVE_USER_MSRS; i++)
		rdmsrl(host_save_user_msrs[i], svm->host_user_msrs[i]);

	if (static_cpu_has(X86_FEATURE_TSCRATEMSR)) {
		u64 tsc_ratio = vcpu->arch.tsc_scaling_ratio;
		if (tsc_ratio != __this_cpu_read(current_tsc_ratio)) {
			__this_cpu_write(current_tsc_ratio, tsc_ratio);
			wrmsrl(MSR_AMD64_TSC_RATIO, tsc_ratio);
		}
	}
	/* This assumes that the kernel never uses MSR_TSC_AUX */
	if (static_cpu_has(X86_FEATURE_RDTSCP))
		wrmsrl(MSR_TSC_AUX, svm->tsc_aux);

	if (sd->current_vmcb != svm->vmcb) {
		sd->current_vmcb = svm->vmcb;
		indirect_branch_prediction_barrier();
	}
	avic_vcpu_load(vcpu, cpu);
}

static void svm_vcpu_put(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	int i;

	avic_vcpu_put(vcpu);

	++vcpu->stat.host_state_reload;
	kvm_load_ldt(svm->host.ldt);
#ifdef CONFIG_X86_64
	loadsegment(fs, svm->host.fs);
	wrmsrl(MSR_KERNEL_GS_BASE, current->thread.gsbase);
	load_gs_index(svm->host.gs);
#else
#ifdef CONFIG_X86_32_LAZY_GS
	loadsegment(gs, svm->host.gs);
#endif
#endif
	for (i = 0; i < NR_HOST_SAVE_USER_MSRS; i++)
		wrmsrl(host_save_user_msrs[i], svm->host_user_msrs[i]);
}

static unsigned long svm_get_rflags(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	unsigned long rflags = svm->vmcb->save.rflags;

	if (svm->nmi_singlestep) {
		/* Hide our flags if they were not set by the guest */
		if (!(svm->nmi_singlestep_guest_rflags & X86_EFLAGS_TF))
			rflags &= ~X86_EFLAGS_TF;
		if (!(svm->nmi_singlestep_guest_rflags & X86_EFLAGS_RF))
			rflags &= ~X86_EFLAGS_RF;
	}
	return rflags;
}

static void svm_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags)
{
	if (to_svm(vcpu)->nmi_singlestep)
		rflags |= (X86_EFLAGS_TF | X86_EFLAGS_RF);

       /*
        * Any change of EFLAGS.VM is accompanied by a reload of SS
        * (caused by either a task switch or an inter-privilege IRET),
        * so we do not need to update the CPL here.
        */
	to_svm(vcpu)->vmcb->save.rflags = rflags;
}

static void svm_cache_reg(struct kvm_vcpu *vcpu, enum kvm_reg reg)
{
	switch (reg) {
	case VCPU_EXREG_PDPTR:
		BUG_ON(!npt_enabled);
		load_pdptrs(vcpu, vcpu->arch.walk_mmu, kvm_read_cr3(vcpu));
		break;
	default:
		WARN_ON_ONCE(1);
	}
}

static inline void svm_enable_vintr(struct vcpu_svm *svm)
{
	struct vmcb_control_area *control;

	/* The following fields are ignored when AVIC is enabled */
	WARN_ON(kvm_vcpu_apicv_active(&svm->vcpu));

	/*
	 * This is just a dummy VINTR to actually cause a vmexit to happen.
	 * Actual injection of virtual interrupts happens through EVENTINJ.
	 */
	control = &svm->vmcb->control;
	control->int_vector = 0x0;
	control->int_ctl &= ~V_INTR_PRIO_MASK;
	control->int_ctl |= V_IRQ_MASK |
		((/*control->int_vector >> 4*/ 0xf) << V_INTR_PRIO_SHIFT);
	mark_dirty(svm->vmcb, VMCB_INTR);
}

static void svm_set_vintr(struct vcpu_svm *svm)
{
	set_intercept(svm, INTERCEPT_VINTR);
	if (is_intercept(svm, INTERCEPT_VINTR))
		svm_enable_vintr(svm);
}

static void svm_clear_vintr(struct vcpu_svm *svm)
{
	clr_intercept(svm, INTERCEPT_VINTR);

	svm->vmcb->control.int_ctl &= ~V_IRQ_MASK;
	mark_dirty(svm->vmcb, VMCB_INTR);
}

static struct vmcb_seg *svm_seg(struct kvm_vcpu *vcpu, int seg)
{
	struct vmcb_save_area *save = &to_svm(vcpu)->vmcb->save;

	switch (seg) {
	case VCPU_SREG_CS: return &save->cs;
	case VCPU_SREG_DS: return &save->ds;
	case VCPU_SREG_ES: return &save->es;
	case VCPU_SREG_FS: return &save->fs;
	case VCPU_SREG_GS: return &save->gs;
	case VCPU_SREG_SS: return &save->ss;
	case VCPU_SREG_TR: return &save->tr;
	case VCPU_SREG_LDTR: return &save->ldtr;
	}
	BUG();
	return NULL;
}

static u64 svm_get_segment_base(struct kvm_vcpu *vcpu, int seg)
{
	struct vmcb_seg *s = svm_seg(vcpu, seg);

	return s->base;
}

static void svm_get_segment(struct kvm_vcpu *vcpu,
			    struct kvm_segment *var, int seg)
{
	struct vmcb_seg *s = svm_seg(vcpu, seg);

	var->base = s->base;
	var->limit = s->limit;
	var->selector = s->selector;
	var->type = s->attrib & SVM_SELECTOR_TYPE_MASK;
	var->s = (s->attrib >> SVM_SELECTOR_S_SHIFT) & 1;
	var->dpl = (s->attrib >> SVM_SELECTOR_DPL_SHIFT) & 3;
	var->present = (s->attrib >> SVM_SELECTOR_P_SHIFT) & 1;
	var->avl = (s->attrib >> SVM_SELECTOR_AVL_SHIFT) & 1;
	var->l = (s->attrib >> SVM_SELECTOR_L_SHIFT) & 1;
	var->db = (s->attrib >> SVM_SELECTOR_DB_SHIFT) & 1;

	/*
	 * AMD CPUs circa 2014 track the G bit for all segments except CS.
	 * However, the SVM spec states that the G bit is not observed by the
	 * CPU, and some VMware virtual CPUs drop the G bit for all segments.
	 * So let's synthesize a legal G bit for all segments, this helps
	 * running KVM nested. It also helps cross-vendor migration, because
	 * Intel's vmentry has a check on the 'G' bit.
	 */
	var->g = s->limit > 0xfffff;

	/*
	 * AMD's VMCB does not have an explicit unusable field, so emulate it
	 * for cross vendor migration purposes by "not present"
	 */
	var->unusable = !var->present;

	switch (seg) {
	case VCPU_SREG_TR:
		/*
		 * Work around a bug where the busy flag in the tr selector
		 * isn't exposed
		 */
		var->type |= 0x2;
		break;
	case VCPU_SREG_DS:
	case VCPU_SREG_ES:
	case VCPU_SREG_FS:
	case VCPU_SREG_GS:
		/*
		 * The accessed bit must always be set in the segment
		 * descriptor cache, although it can be cleared in the
		 * descriptor, the cached bit always remains at 1. Since
		 * Intel has a check on this, set it here to support
		 * cross-vendor migration.
		 */
		if (!var->unusable)
			var->type |= 0x1;
		break;
	case VCPU_SREG_SS:
		/*
		 * On AMD CPUs sometimes the DB bit in the segment
		 * descriptor is left as 1, although the whole segment has
		 * been made unusable. Clear it here to pass an Intel VMX
		 * entry check when cross vendor migrating.
		 */
		if (var->unusable)
			var->db = 0;
		/* This is symmetric with svm_set_segment() */
		var->dpl = to_svm(vcpu)->vmcb->save.cpl;
		break;
	}
}

static int svm_get_cpl(struct kvm_vcpu *vcpu)
{
	struct vmcb_save_area *save = &to_svm(vcpu)->vmcb->save;

	return save->cpl;
}

static void svm_get_idt(struct kvm_vcpu *vcpu, struct desc_ptr *dt)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	dt->size = svm->vmcb->save.idtr.limit;
	dt->address = svm->vmcb->save.idtr.base;
}

static void svm_set_idt(struct kvm_vcpu *vcpu, struct desc_ptr *dt)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->vmcb->save.idtr.limit = dt->size;
	svm->vmcb->save.idtr.base = dt->address ;
	mark_dirty(svm->vmcb, VMCB_DT);
}

static void svm_get_gdt(struct kvm_vcpu *vcpu, struct desc_ptr *dt)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	dt->size = svm->vmcb->save.gdtr.limit;
	dt->address = svm->vmcb->save.gdtr.base;
}

static void svm_set_gdt(struct kvm_vcpu *vcpu, struct desc_ptr *dt)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->vmcb->save.gdtr.limit = dt->size;
	svm->vmcb->save.gdtr.base = dt->address ;
	mark_dirty(svm->vmcb, VMCB_DT);
}

static void svm_decache_cr0_guest_bits(struct kvm_vcpu *vcpu)
{
}

static void svm_decache_cr4_guest_bits(struct kvm_vcpu *vcpu)
{
}

static void update_cr0_intercept(struct vcpu_svm *svm)
{
	ulong gcr0 = svm->vcpu.arch.cr0;
	u64 *hcr0 = &svm->vmcb->save.cr0;

	*hcr0 = (*hcr0 & ~SVM_CR0_SELECTIVE_MASK)
		| (gcr0 & SVM_CR0_SELECTIVE_MASK);

	mark_dirty(svm->vmcb, VMCB_CR);

	if (gcr0 == *hcr0) {
		clr_cr_intercept(svm, INTERCEPT_CR0_READ);
		clr_cr_intercept(svm, INTERCEPT_CR0_WRITE);
	} else {
		set_cr_intercept(svm, INTERCEPT_CR0_READ);
		set_cr_intercept(svm, INTERCEPT_CR0_WRITE);
	}
}

void svm_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0)
{
	struct vcpu_svm *svm = to_svm(vcpu);

#ifdef CONFIG_X86_64
	if (vcpu->arch.efer & EFER_LME) {
		if (!is_paging(vcpu) && (cr0 & X86_CR0_PG)) {
			vcpu->arch.efer |= EFER_LMA;
			svm->vmcb->save.efer |= EFER_LMA | EFER_LME;
		}

		if (is_paging(vcpu) && !(cr0 & X86_CR0_PG)) {
			vcpu->arch.efer &= ~EFER_LMA;
			svm->vmcb->save.efer &= ~(EFER_LMA | EFER_LME);
		}
	}
#endif
	vcpu->arch.cr0 = cr0;

	if (!npt_enabled)
		cr0 |= X86_CR0_PG | X86_CR0_WP;

	/*
	 * re-enable caching here because the QEMU bios
	 * does not do it - this results in some delay at
	 * reboot
	 */
	if (kvm_check_has_quirk(vcpu->kvm, KVM_X86_QUIRK_CD_NW_CLEARED))
		cr0 &= ~(X86_CR0_CD | X86_CR0_NW);
	svm->vmcb->save.cr0 = cr0;
	mark_dirty(svm->vmcb, VMCB_CR);
	update_cr0_intercept(svm);
}

int svm_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	unsigned long host_cr4_mce = cr4_read_shadow() & X86_CR4_MCE;
	unsigned long old_cr4 = to_svm(vcpu)->vmcb->save.cr4;

	if (cr4 & X86_CR4_VMXE)
		return 1;

	if (npt_enabled && ((old_cr4 ^ cr4) & X86_CR4_PGE))
		svm_flush_tlb(vcpu, true);

	vcpu->arch.cr4 = cr4;
	if (!npt_enabled)
		cr4 |= X86_CR4_PAE;
	cr4 |= host_cr4_mce;
	to_svm(vcpu)->vmcb->save.cr4 = cr4;
	mark_dirty(to_svm(vcpu)->vmcb, VMCB_CR);
	return 0;
}

static void svm_set_segment(struct kvm_vcpu *vcpu,
			    struct kvm_segment *var, int seg)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb_seg *s = svm_seg(vcpu, seg);

	s->base = var->base;
	s->limit = var->limit;
	s->selector = var->selector;
	s->attrib = (var->type & SVM_SELECTOR_TYPE_MASK);
	s->attrib |= (var->s & 1) << SVM_SELECTOR_S_SHIFT;
	s->attrib |= (var->dpl & 3) << SVM_SELECTOR_DPL_SHIFT;
	s->attrib |= ((var->present & 1) && !var->unusable) << SVM_SELECTOR_P_SHIFT;
	s->attrib |= (var->avl & 1) << SVM_SELECTOR_AVL_SHIFT;
	s->attrib |= (var->l & 1) << SVM_SELECTOR_L_SHIFT;
	s->attrib |= (var->db & 1) << SVM_SELECTOR_DB_SHIFT;
	s->attrib |= (var->g & 1) << SVM_SELECTOR_G_SHIFT;

	/*
	 * This is always accurate, except if SYSRET returned to a segment
	 * with SS.DPL != 3.  Intel does not have this quirk, and always
	 * forces SS.DPL to 3 on sysret, so we ignore that case; fixing it
	 * would entail passing the CPL to userspace and back.
	 */
	if (seg == VCPU_SREG_SS)
		/* This is symmetric with svm_get_segment() */
		svm->vmcb->save.cpl = (var->dpl & 3);

	mark_dirty(svm->vmcb, VMCB_SEG);
}

static void update_bp_intercept(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	clr_exception_intercept(svm, BP_VECTOR);

	if (vcpu->guest_debug & KVM_GUESTDBG_ENABLE) {
		if (vcpu->guest_debug & KVM_GUESTDBG_USE_SW_BP)
			set_exception_intercept(svm, BP_VECTOR);
	} else
		vcpu->guest_debug = 0;
}

static void new_asid(struct vcpu_svm *svm, struct svm_cpu_data *sd)
{
	if (sd->next_asid > sd->max_asid) {
		++sd->asid_generation;
		sd->next_asid = sd->min_asid;
		svm->vmcb->control.tlb_ctl = TLB_CONTROL_FLUSH_ALL_ASID;
	}

	svm->asid_generation = sd->asid_generation;
	svm->vmcb->control.asid = sd->next_asid++;

	mark_dirty(svm->vmcb, VMCB_ASID);
}

static void svm_set_dr6(struct vcpu_svm *svm, unsigned long value)
{
	struct vmcb *vmcb = svm->vmcb;

	if (unlikely(value != vmcb->save.dr6)) {
		vmcb->save.dr6 = value;
		mark_dirty(vmcb, VMCB_DR);
	}
}

static void svm_sync_dirty_debug_regs(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	get_debugreg(vcpu->arch.db[0], 0);
	get_debugreg(vcpu->arch.db[1], 1);
	get_debugreg(vcpu->arch.db[2], 2);
	get_debugreg(vcpu->arch.db[3], 3);
	/*
	 * We cannot reset svm->vmcb->save.dr6 to DR6_FIXED_1|DR6_RTM here,
	 * because db_interception might need it.  We can do it before vmentry.
	 */
	vcpu->arch.dr6 = svm->vmcb->save.dr6;
	vcpu->arch.dr7 = svm->vmcb->save.dr7;
	vcpu->arch.switch_db_regs &= ~KVM_DEBUGREG_WONT_EXIT;
	set_dr_intercepts(svm);
}

static void svm_set_dr7(struct kvm_vcpu *vcpu, unsigned long value)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->vmcb->save.dr7 = value;
	mark_dirty(svm->vmcb, VMCB_DR);
}

static int pf_interception(struct vcpu_svm *svm)
{
	u64 fault_address = __sme_clr(svm->vmcb->control.exit_info_2);
	u64 error_code = svm->vmcb->control.exit_info_1;

	return kvm_handle_page_fault(&svm->vcpu, error_code, fault_address,
			static_cpu_has(X86_FEATURE_DECODEASSISTS) ?
			svm->vmcb->control.insn_bytes : NULL,
			svm->vmcb->control.insn_len);
}

static int npf_interception(struct vcpu_svm *svm)
{
	u64 fault_address = __sme_clr(svm->vmcb->control.exit_info_2);
	u64 error_code = svm->vmcb->control.exit_info_1;

	trace_kvm_page_fault(fault_address, error_code);
	return kvm_mmu_page_fault(&svm->vcpu, fault_address, error_code,
			static_cpu_has(X86_FEATURE_DECODEASSISTS) ?
			svm->vmcb->control.insn_bytes : NULL,
			svm->vmcb->control.insn_len);
}

static int db_interception(struct vcpu_svm *svm)
{
	struct kvm_run *kvm_run = svm->vcpu.run;
	struct kvm_vcpu *vcpu = &svm->vcpu;

	if (!(svm->vcpu.guest_debug &
	      (KVM_GUESTDBG_SINGLESTEP | KVM_GUESTDBG_USE_HW_BP)) &&
		!svm->nmi_singlestep) {
		u32 payload = (svm->vmcb->save.dr6 ^ DR6_RTM) & ~DR6_FIXED_1;
		kvm_queue_exception_p(&svm->vcpu, DB_VECTOR, payload);
		return 1;
	}

	if (svm->nmi_singlestep) {
		disable_nmi_singlestep(svm);
		/* Make sure we check for pending NMIs upon entry */
		kvm_make_request(KVM_REQ_EVENT, vcpu);
	}

	if (svm->vcpu.guest_debug &
	    (KVM_GUESTDBG_SINGLESTEP | KVM_GUESTDBG_USE_HW_BP)) {
		kvm_run->exit_reason = KVM_EXIT_DEBUG;
		kvm_run->debug.arch.dr6 = svm->vmcb->save.dr6;
		kvm_run->debug.arch.dr7 = svm->vmcb->save.dr7;
		kvm_run->debug.arch.pc =
			svm->vmcb->save.cs.base + svm->vmcb->save.rip;
		kvm_run->debug.arch.exception = DB_VECTOR;
		return 0;
	}

	return 1;
}

static int bp_interception(struct vcpu_svm *svm)
{
	struct kvm_run *kvm_run = svm->vcpu.run;

	kvm_run->exit_reason = KVM_EXIT_DEBUG;
	kvm_run->debug.arch.pc = svm->vmcb->save.cs.base + svm->vmcb->save.rip;
	kvm_run->debug.arch.exception = BP_VECTOR;
	return 0;
}

static int ud_interception(struct vcpu_svm *svm)
{
	return handle_ud(&svm->vcpu);
}

static int ac_interception(struct vcpu_svm *svm)
{
	kvm_queue_exception_e(&svm->vcpu, AC_VECTOR, 0);
	return 1;
}

static int gp_interception(struct vcpu_svm *svm)
{
	struct kvm_vcpu *vcpu = &svm->vcpu;
	u32 error_code = svm->vmcb->control.exit_info_1;

	WARN_ON_ONCE(!enable_vmware_backdoor);

	/*
	 * VMware backdoor emulation on #GP interception only handles IN{S},
	 * OUT{S}, and RDPMC, none of which generate a non-zero error code.
	 */
	if (error_code) {
		kvm_queue_exception_e(vcpu, GP_VECTOR, error_code);
		return 1;
	}
	return kvm_emulate_instruction(vcpu, EMULTYPE_VMWARE_GP);
}

static bool is_erratum_383(void)
{
	int err, i;
	u64 value;

	if (!erratum_383_found)
		return false;

	value = native_read_msr_safe(MSR_IA32_MC0_STATUS, &err);
	if (err)
		return false;

	/* Bit 62 may or may not be set for this mce */
	value &= ~(1ULL << 62);

	if (value != 0xb600000000010015ULL)
		return false;

	/* Clear MCi_STATUS registers */
	for (i = 0; i < 6; ++i)
		native_write_msr_safe(MSR_IA32_MCx_STATUS(i), 0, 0);

	value = native_read_msr_safe(MSR_IA32_MCG_STATUS, &err);
	if (!err) {
		u32 low, high;

		value &= ~(1ULL << 2);
		low    = lower_32_bits(value);
		high   = upper_32_bits(value);

		native_write_msr_safe(MSR_IA32_MCG_STATUS, low, high);
	}

	/* Flush tlb to evict multi-match entries */
	__flush_tlb_all();

	return true;
}

static void svm_handle_mce(struct vcpu_svm *svm)
{
	if (is_erratum_383()) {
		/*
		 * Erratum 383 triggered. Guest state is corrupt so kill the
		 * guest.
		 */
		pr_err("KVM: Guest triggered AMD Erratum 383\n");

		kvm_make_request(KVM_REQ_TRIPLE_FAULT, &svm->vcpu);

		return;
	}

	/*
	 * On an #MC intercept the MCE handler is not called automatically in
	 * the host. So do it by hand here.
	 */
	asm volatile (
		"int $0x12\n");
	/* not sure if we ever come back to this point */

	return;
}

static int mc_interception(struct vcpu_svm *svm)
{
	return 1;
}

static int shutdown_interception(struct vcpu_svm *svm)
{
	struct kvm_run *kvm_run = svm->vcpu.run;

	/*
	 * VMCB is undefined after a SHUTDOWN intercept
	 * so reinitialize it.
	 */
	clear_page(svm->vmcb);
	init_vmcb(svm);

	kvm_run->exit_reason = KVM_EXIT_SHUTDOWN;
	return 0;
}

static int io_interception(struct vcpu_svm *svm)
{
	struct kvm_vcpu *vcpu = &svm->vcpu;
	u32 io_info = svm->vmcb->control.exit_info_1; /* address size bug? */
	int size, in, string;
	unsigned port;

	++svm->vcpu.stat.io_exits;
	string = (io_info & SVM_IOIO_STR_MASK) != 0;
	in = (io_info & SVM_IOIO_TYPE_MASK) != 0;
	if (string)
		return kvm_emulate_instruction(vcpu, 0);

	port = io_info >> 16;
	size = (io_info & SVM_IOIO_SIZE_MASK) >> SVM_IOIO_SIZE_SHIFT;
	svm->next_rip = svm->vmcb->control.exit_info_2;

	return kvm_fast_pio(&svm->vcpu, size, port, in);
}

static int nmi_interception(struct vcpu_svm *svm)
{
	return 1;
}

static int intr_interception(struct vcpu_svm *svm)
{
	++svm->vcpu.stat.irq_exits;
	return 1;
}

static int nop_on_interception(struct vcpu_svm *svm)
{
	return 1;
}

static int halt_interception(struct vcpu_svm *svm)
{
	return kvm_emulate_halt(&svm->vcpu);
}

static int vmmcall_interception(struct vcpu_svm *svm)
{
	return kvm_emulate_hypercall(&svm->vcpu);
}

static int vmload_interception(struct vcpu_svm *svm)
{
	struct vmcb *nested_vmcb;
	struct kvm_host_map map;
	int ret;

	if (nested_svm_check_permissions(svm))
		return 1;

	ret = kvm_vcpu_map(&svm->vcpu, gpa_to_gfn(svm->vmcb->save.rax), &map);
	if (ret) {
		if (ret == -EINVAL)
			kvm_inject_gp(&svm->vcpu, 0);
		return 1;
	}

	nested_vmcb = map.hva;

	ret = kvm_skip_emulated_instruction(&svm->vcpu);

	nested_svm_vmloadsave(nested_vmcb, svm->vmcb);
	kvm_vcpu_unmap(&svm->vcpu, &map, true);

	return ret;
}

static int vmsave_interception(struct vcpu_svm *svm)
{
	struct vmcb *nested_vmcb;
	struct kvm_host_map map;
	int ret;

	if (nested_svm_check_permissions(svm))
		return 1;

	ret = kvm_vcpu_map(&svm->vcpu, gpa_to_gfn(svm->vmcb->save.rax), &map);
	if (ret) {
		if (ret == -EINVAL)
			kvm_inject_gp(&svm->vcpu, 0);
		return 1;
	}

	nested_vmcb = map.hva;

	ret = kvm_skip_emulated_instruction(&svm->vcpu);

	nested_svm_vmloadsave(svm->vmcb, nested_vmcb);
	kvm_vcpu_unmap(&svm->vcpu, &map, true);

	return ret;
}

static int vmrun_interception(struct vcpu_svm *svm)
{
	if (nested_svm_check_permissions(svm))
		return 1;

	return nested_svm_vmrun(svm);
}

static int stgi_interception(struct vcpu_svm *svm)
{
	int ret;

	if (nested_svm_check_permissions(svm))
		return 1;

	/*
	 * If VGIF is enabled, the STGI intercept is only added to
	 * detect the opening of the SMI/NMI window; remove it now.
	 */
	if (vgif_enabled(svm))
		clr_intercept(svm, INTERCEPT_STGI);

	ret = kvm_skip_emulated_instruction(&svm->vcpu);
	kvm_make_request(KVM_REQ_EVENT, &svm->vcpu);

	enable_gif(svm);

	return ret;
}

static int clgi_interception(struct vcpu_svm *svm)
{
	int ret;

	if (nested_svm_check_permissions(svm))
		return 1;

	ret = kvm_skip_emulated_instruction(&svm->vcpu);

	disable_gif(svm);

	/* After a CLGI no interrupts should come */
	if (!kvm_vcpu_apicv_active(&svm->vcpu))
		svm_clear_vintr(svm);

	return ret;
}

static int invlpga_interception(struct vcpu_svm *svm)
{
	struct kvm_vcpu *vcpu = &svm->vcpu;

	trace_kvm_invlpga(svm->vmcb->save.rip, kvm_rcx_read(&svm->vcpu),
			  kvm_rax_read(&svm->vcpu));

	/* Let's treat INVLPGA the same as INVLPG (can be optimized!) */
	kvm_mmu_invlpg(vcpu, kvm_rax_read(&svm->vcpu));

	return kvm_skip_emulated_instruction(&svm->vcpu);
}

static int skinit_interception(struct vcpu_svm *svm)
{
	trace_kvm_skinit(svm->vmcb->save.rip, kvm_rax_read(&svm->vcpu));

	kvm_queue_exception(&svm->vcpu, UD_VECTOR);
	return 1;
}

static int wbinvd_interception(struct vcpu_svm *svm)
{
	return kvm_emulate_wbinvd(&svm->vcpu);
}

static int xsetbv_interception(struct vcpu_svm *svm)
{
	u64 new_bv = kvm_read_edx_eax(&svm->vcpu);
	u32 index = kvm_rcx_read(&svm->vcpu);

	if (kvm_set_xcr(&svm->vcpu, index, new_bv) == 0) {
		return kvm_skip_emulated_instruction(&svm->vcpu);
	}

	return 1;
}

static int rdpru_interception(struct vcpu_svm *svm)
{
	kvm_queue_exception(&svm->vcpu, UD_VECTOR);
	return 1;
}

static int task_switch_interception(struct vcpu_svm *svm)
{
	u16 tss_selector;
	int reason;
	int int_type = svm->vmcb->control.exit_int_info &
		SVM_EXITINTINFO_TYPE_MASK;
	int int_vec = svm->vmcb->control.exit_int_info & SVM_EVTINJ_VEC_MASK;
	uint32_t type =
		svm->vmcb->control.exit_int_info & SVM_EXITINTINFO_TYPE_MASK;
	uint32_t idt_v =
		svm->vmcb->control.exit_int_info & SVM_EXITINTINFO_VALID;
	bool has_error_code = false;
	u32 error_code = 0;

	tss_selector = (u16)svm->vmcb->control.exit_info_1;

	if (svm->vmcb->control.exit_info_2 &
	    (1ULL << SVM_EXITINFOSHIFT_TS_REASON_IRET))
		reason = TASK_SWITCH_IRET;
	else if (svm->vmcb->control.exit_info_2 &
		 (1ULL << SVM_EXITINFOSHIFT_TS_REASON_JMP))
		reason = TASK_SWITCH_JMP;
	else if (idt_v)
		reason = TASK_SWITCH_GATE;
	else
		reason = TASK_SWITCH_CALL;

	if (reason == TASK_SWITCH_GATE) {
		switch (type) {
		case SVM_EXITINTINFO_TYPE_NMI:
			svm->vcpu.arch.nmi_injected = false;
			break;
		case SVM_EXITINTINFO_TYPE_EXEPT:
			if (svm->vmcb->control.exit_info_2 &
			    (1ULL << SVM_EXITINFOSHIFT_TS_HAS_ERROR_CODE)) {
				has_error_code = true;
				error_code =
					(u32)svm->vmcb->control.exit_info_2;
			}
			kvm_clear_exception_queue(&svm->vcpu);
			break;
		case SVM_EXITINTINFO_TYPE_INTR:
			kvm_clear_interrupt_queue(&svm->vcpu);
			break;
		default:
			break;
		}
	}

	if (reason != TASK_SWITCH_GATE ||
	    int_type == SVM_EXITINTINFO_TYPE_SOFT ||
	    (int_type == SVM_EXITINTINFO_TYPE_EXEPT &&
	     (int_vec == OF_VECTOR || int_vec == BP_VECTOR))) {
		if (!skip_emulated_instruction(&svm->vcpu))
			return 0;
	}

	if (int_type != SVM_EXITINTINFO_TYPE_SOFT)
		int_vec = -1;

	return kvm_task_switch(&svm->vcpu, tss_selector, int_vec, reason,
			       has_error_code, error_code);
}

static int cpuid_interception(struct vcpu_svm *svm)
{
	return kvm_emulate_cpuid(&svm->vcpu);
}

static int iret_interception(struct vcpu_svm *svm)
{
	++svm->vcpu.stat.nmi_window_exits;
	clr_intercept(svm, INTERCEPT_IRET);
	svm->vcpu.arch.hflags |= HF_IRET_MASK;
	svm->nmi_iret_rip = kvm_rip_read(&svm->vcpu);
	kvm_make_request(KVM_REQ_EVENT, &svm->vcpu);
	return 1;
}

static int invlpg_interception(struct vcpu_svm *svm)
{
	if (!static_cpu_has(X86_FEATURE_DECODEASSISTS))
		return kvm_emulate_instruction(&svm->vcpu, 0);

	kvm_mmu_invlpg(&svm->vcpu, svm->vmcb->control.exit_info_1);
	return kvm_skip_emulated_instruction(&svm->vcpu);
}

static int emulate_on_interception(struct vcpu_svm *svm)
{
	return kvm_emulate_instruction(&svm->vcpu, 0);
}

static int rsm_interception(struct vcpu_svm *svm)
{
	return kvm_emulate_instruction_from_buffer(&svm->vcpu, rsm_ins_bytes, 2);
}

static int rdpmc_interception(struct vcpu_svm *svm)
{
	int err;

	if (!nrips)
		return emulate_on_interception(svm);

	err = kvm_rdpmc(&svm->vcpu);
	return kvm_complete_insn_gp(&svm->vcpu, err);
}

static bool check_selective_cr0_intercepted(struct vcpu_svm *svm,
					    unsigned long val)
{
	unsigned long cr0 = svm->vcpu.arch.cr0;
	bool ret = false;
	u64 intercept;

	intercept = svm->nested.intercept;

	if (!is_guest_mode(&svm->vcpu) ||
	    (!(intercept & (1ULL << INTERCEPT_SELECTIVE_CR0))))
		return false;

	cr0 &= ~SVM_CR0_SELECTIVE_MASK;
	val &= ~SVM_CR0_SELECTIVE_MASK;

	if (cr0 ^ val) {
		svm->vmcb->control.exit_code = SVM_EXIT_CR0_SEL_WRITE;
		ret = (nested_svm_exit_handled(svm) == NESTED_EXIT_DONE);
	}

	return ret;
}

#define CR_VALID (1ULL << 63)

static int cr_interception(struct vcpu_svm *svm)
{
	int reg, cr;
	unsigned long val;
	int err;

	if (!static_cpu_has(X86_FEATURE_DECODEASSISTS))
		return emulate_on_interception(svm);

	if (unlikely((svm->vmcb->control.exit_info_1 & CR_VALID) == 0))
		return emulate_on_interception(svm);

	reg = svm->vmcb->control.exit_info_1 & SVM_EXITINFO_REG_MASK;
	if (svm->vmcb->control.exit_code == SVM_EXIT_CR0_SEL_WRITE)
		cr = SVM_EXIT_WRITE_CR0 - SVM_EXIT_READ_CR0;
	else
		cr = svm->vmcb->control.exit_code - SVM_EXIT_READ_CR0;

	err = 0;
	if (cr >= 16) { /* mov to cr */
		cr -= 16;
		val = kvm_register_read(&svm->vcpu, reg);
		switch (cr) {
		case 0:
			if (!check_selective_cr0_intercepted(svm, val))
				err = kvm_set_cr0(&svm->vcpu, val);
			else
				return 1;

			break;
		case 3:
			err = kvm_set_cr3(&svm->vcpu, val);
			break;
		case 4:
			err = kvm_set_cr4(&svm->vcpu, val);
			break;
		case 8:
			err = kvm_set_cr8(&svm->vcpu, val);
			break;
		default:
			WARN(1, "unhandled write to CR%d", cr);
			kvm_queue_exception(&svm->vcpu, UD_VECTOR);
			return 1;
		}
	} else { /* mov from cr */
		switch (cr) {
		case 0:
			val = kvm_read_cr0(&svm->vcpu);
			break;
		case 2:
			val = svm->vcpu.arch.cr2;
			break;
		case 3:
			val = kvm_read_cr3(&svm->vcpu);
			break;
		case 4:
			val = kvm_read_cr4(&svm->vcpu);
			break;
		case 8:
			val = kvm_get_cr8(&svm->vcpu);
			break;
		default:
			WARN(1, "unhandled read from CR%d", cr);
			kvm_queue_exception(&svm->vcpu, UD_VECTOR);
			return 1;
		}
		kvm_register_write(&svm->vcpu, reg, val);
	}
	return kvm_complete_insn_gp(&svm->vcpu, err);
}

static int dr_interception(struct vcpu_svm *svm)
{
	int reg, dr;
	unsigned long val;

	if (svm->vcpu.guest_debug == 0) {
		/*
		 * No more DR vmexits; force a reload of the debug registers
		 * and reenter on this instruction.  The next vmexit will
		 * retrieve the full state of the debug registers.
		 */
		clr_dr_intercepts(svm);
		svm->vcpu.arch.switch_db_regs |= KVM_DEBUGREG_WONT_EXIT;
		return 1;
	}

	if (!boot_cpu_has(X86_FEATURE_DECODEASSISTS))
		return emulate_on_interception(svm);

	reg = svm->vmcb->control.exit_info_1 & SVM_EXITINFO_REG_MASK;
	dr = svm->vmcb->control.exit_code - SVM_EXIT_READ_DR0;

	if (dr >= 16) { /* mov to DRn */
		if (!kvm_require_dr(&svm->vcpu, dr - 16))
			return 1;
		val = kvm_register_read(&svm->vcpu, reg);
		kvm_set_dr(&svm->vcpu, dr - 16, val);
	} else {
		if (!kvm_require_dr(&svm->vcpu, dr))
			return 1;
		kvm_get_dr(&svm->vcpu, dr, &val);
		kvm_register_write(&svm->vcpu, reg, val);
	}

	return kvm_skip_emulated_instruction(&svm->vcpu);
}

static int cr8_write_interception(struct vcpu_svm *svm)
{
	struct kvm_run *kvm_run = svm->vcpu.run;
	int r;

	u8 cr8_prev = kvm_get_cr8(&svm->vcpu);
	/* instruction emulation calls kvm_set_cr8() */
	r = cr_interception(svm);
	if (lapic_in_kernel(&svm->vcpu))
		return r;
	if (cr8_prev <= kvm_get_cr8(&svm->vcpu))
		return r;
	kvm_run->exit_reason = KVM_EXIT_SET_TPR;
	return 0;
}

static int svm_get_msr_feature(struct kvm_msr_entry *msr)
{
	msr->data = 0;

	switch (msr->index) {
	case MSR_F10H_DECFG:
		if (boot_cpu_has(X86_FEATURE_LFENCE_RDTSC))
			msr->data |= MSR_F10H_DECFG_LFENCE_SERIALIZE;
		break;
	default:
		return 1;
	}

	return 0;
}

static int svm_get_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	switch (msr_info->index) {
	case MSR_STAR:
		msr_info->data = svm->vmcb->save.star;
		break;
#ifdef CONFIG_X86_64
	case MSR_LSTAR:
		msr_info->data = svm->vmcb->save.lstar;
		break;
	case MSR_CSTAR:
		msr_info->data = svm->vmcb->save.cstar;
		break;
	case MSR_KERNEL_GS_BASE:
		msr_info->data = svm->vmcb->save.kernel_gs_base;
		break;
	case MSR_SYSCALL_MASK:
		msr_info->data = svm->vmcb->save.sfmask;
		break;
#endif
	case MSR_IA32_SYSENTER_CS:
		msr_info->data = svm->vmcb->save.sysenter_cs;
		break;
	case MSR_IA32_SYSENTER_EIP:
		msr_info->data = svm->sysenter_eip;
		break;
	case MSR_IA32_SYSENTER_ESP:
		msr_info->data = svm->sysenter_esp;
		break;
	case MSR_TSC_AUX:
		if (!boot_cpu_has(X86_FEATURE_RDTSCP))
			return 1;
		msr_info->data = svm->tsc_aux;
		break;
	/*
	 * Nobody will change the following 5 values in the VMCB so we can
	 * safely return them on rdmsr. They will always be 0 until LBRV is
	 * implemented.
	 */
	case MSR_IA32_DEBUGCTLMSR:
		msr_info->data = svm->vmcb->save.dbgctl;
		break;
	case MSR_IA32_LASTBRANCHFROMIP:
		msr_info->data = svm->vmcb->save.br_from;
		break;
	case MSR_IA32_LASTBRANCHTOIP:
		msr_info->data = svm->vmcb->save.br_to;
		break;
	case MSR_IA32_LASTINTFROMIP:
		msr_info->data = svm->vmcb->save.last_excp_from;
		break;
	case MSR_IA32_LASTINTTOIP:
		msr_info->data = svm->vmcb->save.last_excp_to;
		break;
	case MSR_VM_HSAVE_PA:
		msr_info->data = svm->nested.hsave_msr;
		break;
	case MSR_VM_CR:
		msr_info->data = svm->nested.vm_cr_msr;
		break;
	case MSR_IA32_SPEC_CTRL:
		if (!msr_info->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_SPEC_CTRL) &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_AMD_STIBP) &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_AMD_IBRS) &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_AMD_SSBD))
			return 1;

		msr_info->data = svm->spec_ctrl;
		break;
	case MSR_AMD64_VIRT_SPEC_CTRL:
		if (!msr_info->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_VIRT_SSBD))
			return 1;

		msr_info->data = svm->virt_spec_ctrl;
		break;
	case MSR_F15H_IC_CFG: {

		int family, model;

		family = guest_cpuid_family(vcpu);
		model  = guest_cpuid_model(vcpu);

		if (family < 0 || model < 0)
			return kvm_get_msr_common(vcpu, msr_info);

		msr_info->data = 0;

		if (family == 0x15 &&
		    (model >= 0x2 && model < 0x20))
			msr_info->data = 0x1E;
		}
		break;
	case MSR_F10H_DECFG:
		msr_info->data = svm->msr_decfg;
		break;
	default:
		return kvm_get_msr_common(vcpu, msr_info);
	}
	return 0;
}

static int rdmsr_interception(struct vcpu_svm *svm)
{
	return kvm_emulate_rdmsr(&svm->vcpu);
}

static int svm_set_vm_cr(struct kvm_vcpu *vcpu, u64 data)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	int svm_dis, chg_mask;

	if (data & ~SVM_VM_CR_VALID_MASK)
		return 1;

	chg_mask = SVM_VM_CR_VALID_MASK;

	if (svm->nested.vm_cr_msr & SVM_VM_CR_SVM_DIS_MASK)
		chg_mask &= ~(SVM_VM_CR_SVM_LOCK_MASK | SVM_VM_CR_SVM_DIS_MASK);

	svm->nested.vm_cr_msr &= ~chg_mask;
	svm->nested.vm_cr_msr |= (data & chg_mask);

	svm_dis = svm->nested.vm_cr_msr & SVM_VM_CR_SVM_DIS_MASK;

	/* check for svm_disable while efer.svme is set */
	if (svm_dis && (vcpu->arch.efer & EFER_SVME))
		return 1;

	return 0;
}

static int svm_set_msr(struct kvm_vcpu *vcpu, struct msr_data *msr)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	u32 ecx = msr->index;
	u64 data = msr->data;
	switch (ecx) {
	case MSR_IA32_CR_PAT:
		if (!kvm_mtrr_valid(vcpu, MSR_IA32_CR_PAT, data))
			return 1;
		vcpu->arch.pat = data;
		svm->vmcb->save.g_pat = data;
		mark_dirty(svm->vmcb, VMCB_NPT);
		break;
	case MSR_IA32_SPEC_CTRL:
		if (!msr->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_SPEC_CTRL) &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_AMD_STIBP) &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_AMD_IBRS) &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_AMD_SSBD))
			return 1;

		if (data & ~kvm_spec_ctrl_valid_bits(vcpu))
			return 1;

		svm->spec_ctrl = data;
		if (!data)
			break;

		/*
		 * For non-nested:
		 * When it's written (to non-zero) for the first time, pass
		 * it through.
		 *
		 * For nested:
		 * The handling of the MSR bitmap for L2 guests is done in
		 * nested_svm_vmrun_msrpm.
		 * We update the L1 MSR bit as well since it will end up
		 * touching the MSR anyway now.
		 */
		set_msr_interception(svm->msrpm, MSR_IA32_SPEC_CTRL, 1, 1);
		break;
	case MSR_IA32_PRED_CMD:
		if (!msr->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_AMD_IBPB))
			return 1;

		if (data & ~PRED_CMD_IBPB)
			return 1;
		if (!boot_cpu_has(X86_FEATURE_AMD_IBPB))
			return 1;
		if (!data)
			break;

		wrmsrl(MSR_IA32_PRED_CMD, PRED_CMD_IBPB);
		set_msr_interception(svm->msrpm, MSR_IA32_PRED_CMD, 0, 1);
		break;
	case MSR_AMD64_VIRT_SPEC_CTRL:
		if (!msr->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_VIRT_SSBD))
			return 1;

		if (data & ~SPEC_CTRL_SSBD)
			return 1;

		svm->virt_spec_ctrl = data;
		break;
	case MSR_STAR:
		svm->vmcb->save.star = data;
		break;
#ifdef CONFIG_X86_64
	case MSR_LSTAR:
		svm->vmcb->save.lstar = data;
		break;
	case MSR_CSTAR:
		svm->vmcb->save.cstar = data;
		break;
	case MSR_KERNEL_GS_BASE:
		svm->vmcb->save.kernel_gs_base = data;
		break;
	case MSR_SYSCALL_MASK:
		svm->vmcb->save.sfmask = data;
		break;
#endif
	case MSR_IA32_SYSENTER_CS:
		svm->vmcb->save.sysenter_cs = data;
		break;
	case MSR_IA32_SYSENTER_EIP:
		svm->sysenter_eip = data;
		svm->vmcb->save.sysenter_eip = data;
		break;
	case MSR_IA32_SYSENTER_ESP:
		svm->sysenter_esp = data;
		svm->vmcb->save.sysenter_esp = data;
		break;
	case MSR_TSC_AUX:
		if (!boot_cpu_has(X86_FEATURE_RDTSCP))
			return 1;

		/*
		 * This is rare, so we update the MSR here instead of using
		 * direct_access_msrs.  Doing that would require a rdmsr in
		 * svm_vcpu_put.
		 */
		svm->tsc_aux = data;
		wrmsrl(MSR_TSC_AUX, svm->tsc_aux);
		break;
	case MSR_IA32_DEBUGCTLMSR:
		if (!boot_cpu_has(X86_FEATURE_LBRV)) {
			vcpu_unimpl(vcpu, "%s: MSR_IA32_DEBUGCTL 0x%llx, nop\n",
				    __func__, data);
			break;
		}
		if (data & DEBUGCTL_RESERVED_BITS)
			return 1;

		svm->vmcb->save.dbgctl = data;
		mark_dirty(svm->vmcb, VMCB_LBR);
		if (data & (1ULL<<0))
			svm_enable_lbrv(svm);
		else
			svm_disable_lbrv(svm);
		break;
	case MSR_VM_HSAVE_PA:
		svm->nested.hsave_msr = data;
		break;
	case MSR_VM_CR:
		return svm_set_vm_cr(vcpu, data);
	case MSR_VM_IGNNE:
		vcpu_unimpl(vcpu, "unimplemented wrmsr: 0x%x data 0x%llx\n", ecx, data);
		break;
	case MSR_F10H_DECFG: {
		struct kvm_msr_entry msr_entry;

		msr_entry.index = msr->index;
		if (svm_get_msr_feature(&msr_entry))
			return 1;

		/* Check the supported bits */
		if (data & ~msr_entry.data)
			return 1;

		/* Don't allow the guest to change a bit, #GP */
		if (!msr->host_initiated && (data ^ msr_entry.data))
			return 1;

		svm->msr_decfg = data;
		break;
	}
	case MSR_IA32_APICBASE:
		if (kvm_vcpu_apicv_active(vcpu))
			avic_update_vapic_bar(to_svm(vcpu), data);
		/* Fall through */
	default:
		return kvm_set_msr_common(vcpu, msr);
	}
	return 0;
}

static int wrmsr_interception(struct vcpu_svm *svm)
{
	return kvm_emulate_wrmsr(&svm->vcpu);
}

static int msr_interception(struct vcpu_svm *svm)
{
	if (svm->vmcb->control.exit_info_1)
		return wrmsr_interception(svm);
	else
		return rdmsr_interception(svm);
}

static int interrupt_window_interception(struct vcpu_svm *svm)
{
	kvm_make_request(KVM_REQ_EVENT, &svm->vcpu);
	svm_clear_vintr(svm);

	/*
	 * For AVIC, the only reason to end up here is ExtINTs.
	 * In this case AVIC was temporarily disabled for
	 * requesting the IRQ window and we have to re-enable it.
	 */
	svm_toggle_avic_for_irq_window(&svm->vcpu, true);

	svm->vmcb->control.int_ctl &= ~V_IRQ_MASK;
	mark_dirty(svm->vmcb, VMCB_INTR);
	++svm->vcpu.stat.irq_window_exits;
	return 1;
}

static int pause_interception(struct vcpu_svm *svm)
{
	struct kvm_vcpu *vcpu = &svm->vcpu;
	bool in_kernel = (svm_get_cpl(vcpu) == 0);

	if (pause_filter_thresh)
		grow_ple_window(vcpu);

	kvm_vcpu_on_spin(vcpu, in_kernel);
	return 1;
}

static int nop_interception(struct vcpu_svm *svm)
{
	return kvm_skip_emulated_instruction(&(svm->vcpu));
}

static int monitor_interception(struct vcpu_svm *svm)
{
	printk_once(KERN_WARNING "kvm: MONITOR instruction emulated as NOP!\n");
	return nop_interception(svm);
}

static int mwait_interception(struct vcpu_svm *svm)
{
	printk_once(KERN_WARNING "kvm: MWAIT instruction emulated as NOP!\n");
	return nop_interception(svm);
}

static int (*const svm_exit_handlers[])(struct vcpu_svm *svm) = {
	[SVM_EXIT_READ_CR0]			= cr_interception,
	[SVM_EXIT_READ_CR3]			= cr_interception,
	[SVM_EXIT_READ_CR4]			= cr_interception,
	[SVM_EXIT_READ_CR8]			= cr_interception,
	[SVM_EXIT_CR0_SEL_WRITE]		= cr_interception,
	[SVM_EXIT_WRITE_CR0]			= cr_interception,
	[SVM_EXIT_WRITE_CR3]			= cr_interception,
	[SVM_EXIT_WRITE_CR4]			= cr_interception,
	[SVM_EXIT_WRITE_CR8]			= cr8_write_interception,
	[SVM_EXIT_READ_DR0]			= dr_interception,
	[SVM_EXIT_READ_DR1]			= dr_interception,
	[SVM_EXIT_READ_DR2]			= dr_interception,
	[SVM_EXIT_READ_DR3]			= dr_interception,
	[SVM_EXIT_READ_DR4]			= dr_interception,
	[SVM_EXIT_READ_DR5]			= dr_interception,
	[SVM_EXIT_READ_DR6]			= dr_interception,
	[SVM_EXIT_READ_DR7]			= dr_interception,
	[SVM_EXIT_WRITE_DR0]			= dr_interception,
	[SVM_EXIT_WRITE_DR1]			= dr_interception,
	[SVM_EXIT_WRITE_DR2]			= dr_interception,
	[SVM_EXIT_WRITE_DR3]			= dr_interception,
	[SVM_EXIT_WRITE_DR4]			= dr_interception,
	[SVM_EXIT_WRITE_DR5]			= dr_interception,
	[SVM_EXIT_WRITE_DR6]			= dr_interception,
	[SVM_EXIT_WRITE_DR7]			= dr_interception,
	[SVM_EXIT_EXCP_BASE + DB_VECTOR]	= db_interception,
	[SVM_EXIT_EXCP_BASE + BP_VECTOR]	= bp_interception,
	[SVM_EXIT_EXCP_BASE + UD_VECTOR]	= ud_interception,
	[SVM_EXIT_EXCP_BASE + PF_VECTOR]	= pf_interception,
	[SVM_EXIT_EXCP_BASE + MC_VECTOR]	= mc_interception,
	[SVM_EXIT_EXCP_BASE + AC_VECTOR]	= ac_interception,
	[SVM_EXIT_EXCP_BASE + GP_VECTOR]	= gp_interception,
	[SVM_EXIT_INTR]				= intr_interception,
	[SVM_EXIT_NMI]				= nmi_interception,
	[SVM_EXIT_SMI]				= nop_on_interception,
	[SVM_EXIT_INIT]				= nop_on_interception,
	[SVM_EXIT_VINTR]			= interrupt_window_interception,
	[SVM_EXIT_RDPMC]			= rdpmc_interception,
	[SVM_EXIT_CPUID]			= cpuid_interception,
	[SVM_EXIT_IRET]                         = iret_interception,
	[SVM_EXIT_INVD]                         = emulate_on_interception,
	[SVM_EXIT_PAUSE]			= pause_interception,
	[SVM_EXIT_HLT]				= halt_interception,
	[SVM_EXIT_INVLPG]			= invlpg_interception,
	[SVM_EXIT_INVLPGA]			= invlpga_interception,
	[SVM_EXIT_IOIO]				= io_interception,
	[SVM_EXIT_MSR]				= msr_interception,
	[SVM_EXIT_TASK_SWITCH]			= task_switch_interception,
	[SVM_EXIT_SHUTDOWN]			= shutdown_interception,
	[SVM_EXIT_VMRUN]			= vmrun_interception,
	[SVM_EXIT_VMMCALL]			= vmmcall_interception,
	[SVM_EXIT_VMLOAD]			= vmload_interception,
	[SVM_EXIT_VMSAVE]			= vmsave_interception,
	[SVM_EXIT_STGI]				= stgi_interception,
	[SVM_EXIT_CLGI]				= clgi_interception,
	[SVM_EXIT_SKINIT]			= skinit_interception,
	[SVM_EXIT_WBINVD]                       = wbinvd_interception,
	[SVM_EXIT_MONITOR]			= monitor_interception,
	[SVM_EXIT_MWAIT]			= mwait_interception,
	[SVM_EXIT_XSETBV]			= xsetbv_interception,
	[SVM_EXIT_RDPRU]			= rdpru_interception,
	[SVM_EXIT_NPF]				= npf_interception,
	[SVM_EXIT_RSM]                          = rsm_interception,
	[SVM_EXIT_AVIC_INCOMPLETE_IPI]		= avic_incomplete_ipi_interception,
	[SVM_EXIT_AVIC_UNACCELERATED_ACCESS]	= avic_unaccelerated_access_interception,
};

static void dump_vmcb(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb_control_area *control = &svm->vmcb->control;
	struct vmcb_save_area *save = &svm->vmcb->save;

	if (!dump_invalid_vmcb) {
		pr_warn_ratelimited("set kvm_amd.dump_invalid_vmcb=1 to dump internal KVM state.\n");
		return;
	}

	pr_err("VMCB Control Area:\n");
	pr_err("%-20s%04x\n", "cr_read:", control->intercept_cr & 0xffff);
	pr_err("%-20s%04x\n", "cr_write:", control->intercept_cr >> 16);
	pr_err("%-20s%04x\n", "dr_read:", control->intercept_dr & 0xffff);
	pr_err("%-20s%04x\n", "dr_write:", control->intercept_dr >> 16);
	pr_err("%-20s%08x\n", "exceptions:", control->intercept_exceptions);
	pr_err("%-20s%016llx\n", "intercepts:", control->intercept);
	pr_err("%-20s%d\n", "pause filter count:", control->pause_filter_count);
	pr_err("%-20s%d\n", "pause filter threshold:",
	       control->pause_filter_thresh);
	pr_err("%-20s%016llx\n", "iopm_base_pa:", control->iopm_base_pa);
	pr_err("%-20s%016llx\n", "msrpm_base_pa:", control->msrpm_base_pa);
	pr_err("%-20s%016llx\n", "tsc_offset:", control->tsc_offset);
	pr_err("%-20s%d\n", "asid:", control->asid);
	pr_err("%-20s%d\n", "tlb_ctl:", control->tlb_ctl);
	pr_err("%-20s%08x\n", "int_ctl:", control->int_ctl);
	pr_err("%-20s%08x\n", "int_vector:", control->int_vector);
	pr_err("%-20s%08x\n", "int_state:", control->int_state);
	pr_err("%-20s%08x\n", "exit_code:", control->exit_code);
	pr_err("%-20s%016llx\n", "exit_info1:", control->exit_info_1);
	pr_err("%-20s%016llx\n", "exit_info2:", control->exit_info_2);
	pr_err("%-20s%08x\n", "exit_int_info:", control->exit_int_info);
	pr_err("%-20s%08x\n", "exit_int_info_err:", control->exit_int_info_err);
	pr_err("%-20s%lld\n", "nested_ctl:", control->nested_ctl);
	pr_err("%-20s%016llx\n", "nested_cr3:", control->nested_cr3);
	pr_err("%-20s%016llx\n", "avic_vapic_bar:", control->avic_vapic_bar);
	pr_err("%-20s%08x\n", "event_inj:", control->event_inj);
	pr_err("%-20s%08x\n", "event_inj_err:", control->event_inj_err);
	pr_err("%-20s%lld\n", "virt_ext:", control->virt_ext);
	pr_err("%-20s%016llx\n", "next_rip:", control->next_rip);
	pr_err("%-20s%016llx\n", "avic_backing_page:", control->avic_backing_page);
	pr_err("%-20s%016llx\n", "avic_logical_id:", control->avic_logical_id);
	pr_err("%-20s%016llx\n", "avic_physical_id:", control->avic_physical_id);
	pr_err("VMCB State Save Area:\n");
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "es:",
	       save->es.selector, save->es.attrib,
	       save->es.limit, save->es.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "cs:",
	       save->cs.selector, save->cs.attrib,
	       save->cs.limit, save->cs.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "ss:",
	       save->ss.selector, save->ss.attrib,
	       save->ss.limit, save->ss.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "ds:",
	       save->ds.selector, save->ds.attrib,
	       save->ds.limit, save->ds.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "fs:",
	       save->fs.selector, save->fs.attrib,
	       save->fs.limit, save->fs.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "gs:",
	       save->gs.selector, save->gs.attrib,
	       save->gs.limit, save->gs.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "gdtr:",
	       save->gdtr.selector, save->gdtr.attrib,
	       save->gdtr.limit, save->gdtr.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "ldtr:",
	       save->ldtr.selector, save->ldtr.attrib,
	       save->ldtr.limit, save->ldtr.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "idtr:",
	       save->idtr.selector, save->idtr.attrib,
	       save->idtr.limit, save->idtr.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "tr:",
	       save->tr.selector, save->tr.attrib,
	       save->tr.limit, save->tr.base);
	pr_err("cpl:            %d                efer:         %016llx\n",
		save->cpl, save->efer);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "cr0:", save->cr0, "cr2:", save->cr2);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "cr3:", save->cr3, "cr4:", save->cr4);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "dr6:", save->dr6, "dr7:", save->dr7);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "rip:", save->rip, "rflags:", save->rflags);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "rsp:", save->rsp, "rax:", save->rax);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "star:", save->star, "lstar:", save->lstar);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "cstar:", save->cstar, "sfmask:", save->sfmask);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "kernel_gs_base:", save->kernel_gs_base,
	       "sysenter_cs:", save->sysenter_cs);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "sysenter_esp:", save->sysenter_esp,
	       "sysenter_eip:", save->sysenter_eip);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "gpat:", save->g_pat, "dbgctl:", save->dbgctl);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "br_from:", save->br_from, "br_to:", save->br_to);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "excp_from:", save->last_excp_from,
	       "excp_to:", save->last_excp_to);
}

static void svm_get_exit_info(struct kvm_vcpu *vcpu, u64 *info1, u64 *info2)
{
	struct vmcb_control_area *control = &to_svm(vcpu)->vmcb->control;

	*info1 = control->exit_info_1;
	*info2 = control->exit_info_2;
}

static int handle_exit(struct kvm_vcpu *vcpu,
	enum exit_fastpath_completion exit_fastpath)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct kvm_run *kvm_run = vcpu->run;
	u32 exit_code = svm->vmcb->control.exit_code;

	trace_kvm_exit(exit_code, vcpu, KVM_ISA_SVM);

	if (!is_cr_intercept(svm, INTERCEPT_CR0_WRITE))
		vcpu->arch.cr0 = svm->vmcb->save.cr0;
	if (npt_enabled)
		vcpu->arch.cr3 = svm->vmcb->save.cr3;

	if (unlikely(svm->nested.exit_required)) {
		nested_svm_vmexit(svm);
		svm->nested.exit_required = false;

		return 1;
	}

	if (is_guest_mode(vcpu)) {
		int vmexit;

		trace_kvm_nested_vmexit(svm->vmcb->save.rip, exit_code,
					svm->vmcb->control.exit_info_1,
					svm->vmcb->control.exit_info_2,
					svm->vmcb->control.exit_int_info,
					svm->vmcb->control.exit_int_info_err,
					KVM_ISA_SVM);

		vmexit = nested_svm_exit_special(svm);

		if (vmexit == NESTED_EXIT_CONTINUE)
			vmexit = nested_svm_exit_handled(svm);

		if (vmexit == NESTED_EXIT_DONE)
			return 1;
	}

	svm_complete_interrupts(svm);

	if (svm->vmcb->control.exit_code == SVM_EXIT_ERR) {
		kvm_run->exit_reason = KVM_EXIT_FAIL_ENTRY;
		kvm_run->fail_entry.hardware_entry_failure_reason
			= svm->vmcb->control.exit_code;
		dump_vmcb(vcpu);
		return 0;
	}

	if (is_external_interrupt(svm->vmcb->control.exit_int_info) &&
	    exit_code != SVM_EXIT_EXCP_BASE + PF_VECTOR &&
	    exit_code != SVM_EXIT_NPF && exit_code != SVM_EXIT_TASK_SWITCH &&
	    exit_code != SVM_EXIT_INTR && exit_code != SVM_EXIT_NMI)
		printk(KERN_ERR "%s: unexpected exit_int_info 0x%x "
		       "exit_code 0x%x\n",
		       __func__, svm->vmcb->control.exit_int_info,
		       exit_code);

	if (exit_fastpath == EXIT_FASTPATH_SKIP_EMUL_INS) {
		kvm_skip_emulated_instruction(vcpu);
		return 1;
	} else if (exit_code >= ARRAY_SIZE(svm_exit_handlers)
	    || !svm_exit_handlers[exit_code]) {
		vcpu_unimpl(vcpu, "svm: unexpected exit reason 0x%x\n", exit_code);
		dump_vmcb(vcpu);
		vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		vcpu->run->internal.suberror =
			KVM_INTERNAL_ERROR_UNEXPECTED_EXIT_REASON;
		vcpu->run->internal.ndata = 1;
		vcpu->run->internal.data[0] = exit_code;
		return 0;
	}

#ifdef CONFIG_RETPOLINE
	if (exit_code == SVM_EXIT_MSR)
		return msr_interception(svm);
	else if (exit_code == SVM_EXIT_VINTR)
		return interrupt_window_interception(svm);
	else if (exit_code == SVM_EXIT_INTR)
		return intr_interception(svm);
	else if (exit_code == SVM_EXIT_HLT)
		return halt_interception(svm);
	else if (exit_code == SVM_EXIT_NPF)
		return npf_interception(svm);
#endif
	return svm_exit_handlers[exit_code](svm);
}

static void reload_tss(struct kvm_vcpu *vcpu)
{
	int cpu = raw_smp_processor_id();

	struct svm_cpu_data *sd = per_cpu(svm_data, cpu);
	sd->tss_desc->type = 9; /* available 32/64-bit TSS */
	load_TR_desc();
}

static void pre_svm_run(struct vcpu_svm *svm)
{
	int cpu = raw_smp_processor_id();

	struct svm_cpu_data *sd = per_cpu(svm_data, cpu);

	if (sev_guest(svm->vcpu.kvm))
		return pre_sev_run(svm, cpu);

	/* FIXME: handle wraparound of asid_generation */
	if (svm->asid_generation != sd->asid_generation)
		new_asid(svm, sd);
}

static void svm_inject_nmi(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->vmcb->control.event_inj = SVM_EVTINJ_VALID | SVM_EVTINJ_TYPE_NMI;
	vcpu->arch.hflags |= HF_NMI_MASK;
	set_intercept(svm, INTERCEPT_IRET);
	++vcpu->stat.nmi_injections;
}

static void svm_set_irq(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	BUG_ON(!(gif_set(svm)));

	trace_kvm_inj_virq(vcpu->arch.interrupt.nr);
	++vcpu->stat.irq_injections;

	svm->vmcb->control.event_inj = vcpu->arch.interrupt.nr |
		SVM_EVTINJ_VALID | SVM_EVTINJ_TYPE_INTR;
}

static void update_cr8_intercept(struct kvm_vcpu *vcpu, int tpr, int irr)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (svm_nested_virtualize_tpr(vcpu))
		return;

	clr_cr_intercept(svm, INTERCEPT_CR8_WRITE);

	if (irr == -1)
		return;

	if (tpr >= irr)
		set_cr_intercept(svm, INTERCEPT_CR8_WRITE);
}

static int svm_nmi_allowed(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb *vmcb = svm->vmcb;
	int ret;
	ret = !(vmcb->control.int_state & SVM_INTERRUPT_SHADOW_MASK) &&
	      !(svm->vcpu.arch.hflags & HF_NMI_MASK);
	ret = ret && gif_set(svm) && nested_svm_nmi(svm);

	return ret;
}

static bool svm_get_nmi_mask(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	return !!(svm->vcpu.arch.hflags & HF_NMI_MASK);
}

static void svm_set_nmi_mask(struct kvm_vcpu *vcpu, bool masked)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (masked) {
		svm->vcpu.arch.hflags |= HF_NMI_MASK;
		set_intercept(svm, INTERCEPT_IRET);
	} else {
		svm->vcpu.arch.hflags &= ~HF_NMI_MASK;
		clr_intercept(svm, INTERCEPT_IRET);
	}
}

static int svm_interrupt_allowed(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb *vmcb = svm->vmcb;

	if (!gif_set(svm) ||
	     (vmcb->control.int_state & SVM_INTERRUPT_SHADOW_MASK))
		return 0;

	if (is_guest_mode(vcpu) && (svm->vcpu.arch.hflags & HF_VINTR_MASK))
		return !!(svm->vcpu.arch.hflags & HF_HIF_MASK);
	else
		return !!(kvm_get_rflags(vcpu) & X86_EFLAGS_IF);
}

static void enable_irq_window(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	/*
	 * In case GIF=0 we can't rely on the CPU to tell us when GIF becomes
	 * 1, because that's a separate STGI/VMRUN intercept.  The next time we
	 * get that intercept, this function will be called again though and
	 * we'll get the vintr intercept. However, if the vGIF feature is
	 * enabled, the STGI interception will not occur. Enable the irq
	 * window under the assumption that the hardware will set the GIF.
	 */
	if (vgif_enabled(svm) || gif_set(svm)) {
		/*
		 * IRQ window is not needed when AVIC is enabled,
		 * unless we have pending ExtINT since it cannot be injected
		 * via AVIC. In such case, we need to temporarily disable AVIC,
		 * and fallback to injecting IRQ via V_IRQ.
		 */
		svm_toggle_avic_for_irq_window(vcpu, false);
		svm_set_vintr(svm);
	}
}

static void enable_nmi_window(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if ((svm->vcpu.arch.hflags & (HF_NMI_MASK | HF_IRET_MASK))
	    == HF_NMI_MASK)
		return; /* IRET will cause a vm exit */

	if (!gif_set(svm)) {
		if (vgif_enabled(svm))
			set_intercept(svm, INTERCEPT_STGI);
		return; /* STGI will cause a vm exit */
	}

	if (svm->nested.exit_required)
		return; /* we're not going to run the guest yet */

	/*
	 * Something prevents NMI from been injected. Single step over possible
	 * problem (IRET or exception injection or interrupt shadow)
	 */
	svm->nmi_singlestep_guest_rflags = svm_get_rflags(vcpu);
	svm->nmi_singlestep = true;
	svm->vmcb->save.rflags |= (X86_EFLAGS_TF | X86_EFLAGS_RF);
}

static int svm_set_tss_addr(struct kvm *kvm, unsigned int addr)
{
	return 0;
}

static int svm_set_identity_map_addr(struct kvm *kvm, u64 ident_addr)
{
	return 0;
}

void svm_flush_tlb(struct kvm_vcpu *vcpu, bool invalidate_gpa)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (static_cpu_has(X86_FEATURE_FLUSHBYASID))
		svm->vmcb->control.tlb_ctl = TLB_CONTROL_FLUSH_ASID;
	else
		svm->asid_generation--;
}

static void svm_flush_tlb_gva(struct kvm_vcpu *vcpu, gva_t gva)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	invlpga(gva, svm->vmcb->control.asid);
}

static void svm_prepare_guest_switch(struct kvm_vcpu *vcpu)
{
}

static inline void sync_cr8_to_lapic(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (svm_nested_virtualize_tpr(vcpu))
		return;

	if (!is_cr_intercept(svm, INTERCEPT_CR8_WRITE)) {
		int cr8 = svm->vmcb->control.int_ctl & V_TPR_MASK;
		kvm_set_cr8(vcpu, cr8);
	}
}

static inline void sync_lapic_to_cr8(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u64 cr8;

	if (svm_nested_virtualize_tpr(vcpu) ||
	    kvm_vcpu_apicv_active(vcpu))
		return;

	cr8 = kvm_get_cr8(vcpu);
	svm->vmcb->control.int_ctl &= ~V_TPR_MASK;
	svm->vmcb->control.int_ctl |= cr8 & V_TPR_MASK;
}

static void svm_complete_interrupts(struct vcpu_svm *svm)
{
	u8 vector;
	int type;
	u32 exitintinfo = svm->vmcb->control.exit_int_info;
	unsigned int3_injected = svm->int3_injected;

	svm->int3_injected = 0;

	/*
	 * If we've made progress since setting HF_IRET_MASK, we've
	 * executed an IRET and can allow NMI injection.
	 */
	if ((svm->vcpu.arch.hflags & HF_IRET_MASK)
	    && kvm_rip_read(&svm->vcpu) != svm->nmi_iret_rip) {
		svm->vcpu.arch.hflags &= ~(HF_NMI_MASK | HF_IRET_MASK);
		kvm_make_request(KVM_REQ_EVENT, &svm->vcpu);
	}

	svm->vcpu.arch.nmi_injected = false;
	kvm_clear_exception_queue(&svm->vcpu);
	kvm_clear_interrupt_queue(&svm->vcpu);

	if (!(exitintinfo & SVM_EXITINTINFO_VALID))
		return;

	kvm_make_request(KVM_REQ_EVENT, &svm->vcpu);

	vector = exitintinfo & SVM_EXITINTINFO_VEC_MASK;
	type = exitintinfo & SVM_EXITINTINFO_TYPE_MASK;

	switch (type) {
	case SVM_EXITINTINFO_TYPE_NMI:
		svm->vcpu.arch.nmi_injected = true;
		break;
	case SVM_EXITINTINFO_TYPE_EXEPT:
		/*
		 * In case of software exceptions, do not reinject the vector,
		 * but re-execute the instruction instead. Rewind RIP first
		 * if we emulated INT3 before.
		 */
		if (kvm_exception_is_soft(vector)) {
			if (vector == BP_VECTOR && int3_injected &&
			    kvm_is_linear_rip(&svm->vcpu, svm->int3_rip))
				kvm_rip_write(&svm->vcpu,
					      kvm_rip_read(&svm->vcpu) -
					      int3_injected);
			break;
		}
		if (exitintinfo & SVM_EXITINTINFO_VALID_ERR) {
			u32 err = svm->vmcb->control.exit_int_info_err;
			kvm_requeue_exception_e(&svm->vcpu, vector, err);

		} else
			kvm_requeue_exception(&svm->vcpu, vector);
		break;
	case SVM_EXITINTINFO_TYPE_INTR:
		kvm_queue_interrupt(&svm->vcpu, vector, false);
		break;
	default:
		break;
	}
}

static void svm_cancel_injection(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb_control_area *control = &svm->vmcb->control;

	control->exit_int_info = control->event_inj;
	control->exit_int_info_err = control->event_inj_err;
	control->event_inj = 0;
	svm_complete_interrupts(svm);
}

void __svm_vcpu_run(unsigned long vmcb_pa, unsigned long *regs);

static void svm_vcpu_run(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->vmcb->save.rax = vcpu->arch.regs[VCPU_REGS_RAX];
	svm->vmcb->save.rsp = vcpu->arch.regs[VCPU_REGS_RSP];
	svm->vmcb->save.rip = vcpu->arch.regs[VCPU_REGS_RIP];

	/*
	 * A vmexit emulation is required before the vcpu can be executed
	 * again.
	 */
	if (unlikely(svm->nested.exit_required))
		return;

	/*
	 * Disable singlestep if we're injecting an interrupt/exception.
	 * We don't want our modified rflags to be pushed on the stack where
	 * we might not be able to easily reset them if we disabled NMI
	 * singlestep later.
	 */
	if (svm->nmi_singlestep && svm->vmcb->control.event_inj) {
		/*
		 * Event injection happens before external interrupts cause a
		 * vmexit and interrupts are disabled here, so smp_send_reschedule
		 * is enough to force an immediate vmexit.
		 */
		disable_nmi_singlestep(svm);
		smp_send_reschedule(vcpu->cpu);
	}

	pre_svm_run(svm);

	sync_lapic_to_cr8(vcpu);

	svm->vmcb->save.cr2 = vcpu->arch.cr2;

	/*
	 * Run with all-zero DR6 unless needed, so that we can get the exact cause
	 * of a #DB.
	 */
	if (unlikely(svm->vcpu.arch.switch_db_regs & KVM_DEBUGREG_WONT_EXIT))
		svm_set_dr6(svm, vcpu->arch.dr6);
	else
		svm_set_dr6(svm, DR6_FIXED_1 | DR6_RTM);

	clgi();
	kvm_load_guest_xsave_state(vcpu);

	if (lapic_in_kernel(vcpu) &&
		vcpu->arch.apic->lapic_timer.timer_advance_ns)
		kvm_wait_lapic_expire(vcpu);

	/*
	 * If this vCPU has touched SPEC_CTRL, restore the guest's value if
	 * it's non-zero. Since vmentry is serialising on affected CPUs, there
	 * is no need to worry about the conditional branch over the wrmsr
	 * being speculatively taken.
	 */
	x86_spec_ctrl_set_guest(svm->spec_ctrl, svm->virt_spec_ctrl);

	__svm_vcpu_run(svm->vmcb_pa, (unsigned long *)&svm->vcpu.arch.regs);

#ifdef CONFIG_X86_64
	wrmsrl(MSR_GS_BASE, svm->host.gs_base);
#else
	loadsegment(fs, svm->host.fs);
#ifndef CONFIG_X86_32_LAZY_GS
	loadsegment(gs, svm->host.gs);
#endif
#endif

	/*
	 * We do not use IBRS in the kernel. If this vCPU has used the
	 * SPEC_CTRL MSR it may have left it on; save the value and
	 * turn it off. This is much more efficient than blindly adding
	 * it to the atomic save/restore list. Especially as the former
	 * (Saving guest MSRs on vmexit) doesn't even exist in KVM.
	 *
	 * For non-nested case:
	 * If the L01 MSR bitmap does not intercept the MSR, then we need to
	 * save it.
	 *
	 * For nested case:
	 * If the L02 MSR bitmap does not intercept the MSR, then we need to
	 * save it.
	 */
	if (unlikely(!msr_write_intercepted(vcpu, MSR_IA32_SPEC_CTRL)))
		svm->spec_ctrl = native_read_msr(MSR_IA32_SPEC_CTRL);

	reload_tss(vcpu);

	x86_spec_ctrl_restore_host(svm->spec_ctrl, svm->virt_spec_ctrl);

	vcpu->arch.cr2 = svm->vmcb->save.cr2;
	vcpu->arch.regs[VCPU_REGS_RAX] = svm->vmcb->save.rax;
	vcpu->arch.regs[VCPU_REGS_RSP] = svm->vmcb->save.rsp;
	vcpu->arch.regs[VCPU_REGS_RIP] = svm->vmcb->save.rip;

	if (unlikely(svm->vmcb->control.exit_code == SVM_EXIT_NMI))
		kvm_before_interrupt(&svm->vcpu);

	kvm_load_host_xsave_state(vcpu);
	stgi();

	/* Any pending NMI will happen here */

	if (unlikely(svm->vmcb->control.exit_code == SVM_EXIT_NMI))
		kvm_after_interrupt(&svm->vcpu);

	sync_cr8_to_lapic(vcpu);

	svm->next_rip = 0;

	svm->vmcb->control.tlb_ctl = TLB_CONTROL_DO_NOTHING;

	/* if exit due to PF check for async PF */
	if (svm->vmcb->control.exit_code == SVM_EXIT_EXCP_BASE + PF_VECTOR)
		svm->vcpu.arch.apf.host_apf_reason = kvm_read_and_reset_pf_reason();

	if (npt_enabled) {
		vcpu->arch.regs_avail &= ~(1 << VCPU_EXREG_PDPTR);
		vcpu->arch.regs_dirty &= ~(1 << VCPU_EXREG_PDPTR);
	}

	/*
	 * We need to handle MC intercepts here before the vcpu has a chance to
	 * change the physical cpu
	 */
	if (unlikely(svm->vmcb->control.exit_code ==
		     SVM_EXIT_EXCP_BASE + MC_VECTOR))
		svm_handle_mce(svm);

	mark_all_clean(svm->vmcb);
}

static void svm_load_mmu_pgd(struct kvm_vcpu *vcpu, unsigned long root)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	bool update_guest_cr3 = true;
	unsigned long cr3;

	cr3 = __sme_set(root);
	if (npt_enabled) {
		svm->vmcb->control.nested_cr3 = cr3;
		mark_dirty(svm->vmcb, VMCB_NPT);

		/* Loading L2's CR3 is handled by enter_svm_guest_mode.  */
		if (is_guest_mode(vcpu))
			update_guest_cr3 = false;
		else if (test_bit(VCPU_EXREG_CR3, (ulong *)&vcpu->arch.regs_avail))
			cr3 = vcpu->arch.cr3;
		else /* CR3 is already up-to-date.  */
			update_guest_cr3 = false;
	}

	if (update_guest_cr3) {
		svm->vmcb->save.cr3 = cr3;
		mark_dirty(svm->vmcb, VMCB_CR);
	}
}

static int is_disabled(void)
{
	u64 vm_cr;

	rdmsrl(MSR_VM_CR, vm_cr);
	if (vm_cr & (1 << SVM_VM_CR_SVM_DISABLE))
		return 1;

	return 0;
}

static void
svm_patch_hypercall(struct kvm_vcpu *vcpu, unsigned char *hypercall)
{
	/*
	 * Patch in the VMMCALL instruction:
	 */
	hypercall[0] = 0x0f;
	hypercall[1] = 0x01;
	hypercall[2] = 0xd9;
}

static int __init svm_check_processor_compat(void)
{
	return 0;
}

static bool svm_cpu_has_accelerated_tpr(void)
{
	return false;
}

static bool svm_has_emulated_msr(int index)
{
	switch (index) {
	case MSR_IA32_MCG_EXT_CTL:
	case MSR_IA32_VMX_BASIC ... MSR_IA32_VMX_VMFUNC:
		return false;
	default:
		break;
	}

	return true;
}

static u64 svm_get_mt_mask(struct kvm_vcpu *vcpu, gfn_t gfn, bool is_mmio)
{
	return 0;
}

static void svm_cpuid_update(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	vcpu->arch.xsaves_enabled = guest_cpuid_has(vcpu, X86_FEATURE_XSAVE) &&
				    boot_cpu_has(X86_FEATURE_XSAVE) &&
				    boot_cpu_has(X86_FEATURE_XSAVES);

	/* Update nrips enabled cache */
	svm->nrips_enabled = kvm_cpu_cap_has(X86_FEATURE_NRIPS) &&
			     guest_cpuid_has(&svm->vcpu, X86_FEATURE_NRIPS);

	if (!kvm_vcpu_apicv_active(vcpu))
		return;

	/*
	 * AVIC does not work with an x2APIC mode guest. If the X2APIC feature
	 * is exposed to the guest, disable AVIC.
	 */
	if (guest_cpuid_has(vcpu, X86_FEATURE_X2APIC))
		kvm_request_apicv_update(vcpu->kvm, false,
					 APICV_INHIBIT_REASON_X2APIC);

	/*
	 * Currently, AVIC does not work with nested virtualization.
	 * So, we disable AVIC when cpuid for SVM is set in the L1 guest.
	 */
	if (nested && guest_cpuid_has(vcpu, X86_FEATURE_SVM))
		kvm_request_apicv_update(vcpu->kvm, false,
					 APICV_INHIBIT_REASON_NESTED);
}

static bool svm_has_wbinvd_exit(void)
{
	return true;
}

#define PRE_EX(exit)  { .exit_code = (exit), \
			.stage = X86_ICPT_PRE_EXCEPT, }
#define POST_EX(exit) { .exit_code = (exit), \
			.stage = X86_ICPT_POST_EXCEPT, }
#define POST_MEM(exit) { .exit_code = (exit), \
			.stage = X86_ICPT_POST_MEMACCESS, }

static const struct __x86_intercept {
	u32 exit_code;
	enum x86_intercept_stage stage;
} x86_intercept_map[] = {
	[x86_intercept_cr_read]		= POST_EX(SVM_EXIT_READ_CR0),
	[x86_intercept_cr_write]	= POST_EX(SVM_EXIT_WRITE_CR0),
	[x86_intercept_clts]		= POST_EX(SVM_EXIT_WRITE_CR0),
	[x86_intercept_lmsw]		= POST_EX(SVM_EXIT_WRITE_CR0),
	[x86_intercept_smsw]		= POST_EX(SVM_EXIT_READ_CR0),
	[x86_intercept_dr_read]		= POST_EX(SVM_EXIT_READ_DR0),
	[x86_intercept_dr_write]	= POST_EX(SVM_EXIT_WRITE_DR0),
	[x86_intercept_sldt]		= POST_EX(SVM_EXIT_LDTR_READ),
	[x86_intercept_str]		= POST_EX(SVM_EXIT_TR_READ),
	[x86_intercept_lldt]		= POST_EX(SVM_EXIT_LDTR_WRITE),
	[x86_intercept_ltr]		= POST_EX(SVM_EXIT_TR_WRITE),
	[x86_intercept_sgdt]		= POST_EX(SVM_EXIT_GDTR_READ),
	[x86_intercept_sidt]		= POST_EX(SVM_EXIT_IDTR_READ),
	[x86_intercept_lgdt]		= POST_EX(SVM_EXIT_GDTR_WRITE),
	[x86_intercept_lidt]		= POST_EX(SVM_EXIT_IDTR_WRITE),
	[x86_intercept_vmrun]		= POST_EX(SVM_EXIT_VMRUN),
	[x86_intercept_vmmcall]		= POST_EX(SVM_EXIT_VMMCALL),
	[x86_intercept_vmload]		= POST_EX(SVM_EXIT_VMLOAD),
	[x86_intercept_vmsave]		= POST_EX(SVM_EXIT_VMSAVE),
	[x86_intercept_stgi]		= POST_EX(SVM_EXIT_STGI),
	[x86_intercept_clgi]		= POST_EX(SVM_EXIT_CLGI),
	[x86_intercept_skinit]		= POST_EX(SVM_EXIT_SKINIT),
	[x86_intercept_invlpga]		= POST_EX(SVM_EXIT_INVLPGA),
	[x86_intercept_rdtscp]		= POST_EX(SVM_EXIT_RDTSCP),
	[x86_intercept_monitor]		= POST_MEM(SVM_EXIT_MONITOR),
	[x86_intercept_mwait]		= POST_EX(SVM_EXIT_MWAIT),
	[x86_intercept_invlpg]		= POST_EX(SVM_EXIT_INVLPG),
	[x86_intercept_invd]		= POST_EX(SVM_EXIT_INVD),
	[x86_intercept_wbinvd]		= POST_EX(SVM_EXIT_WBINVD),
	[x86_intercept_wrmsr]		= POST_EX(SVM_EXIT_MSR),
	[x86_intercept_rdtsc]		= POST_EX(SVM_EXIT_RDTSC),
	[x86_intercept_rdmsr]		= POST_EX(SVM_EXIT_MSR),
	[x86_intercept_rdpmc]		= POST_EX(SVM_EXIT_RDPMC),
	[x86_intercept_cpuid]		= PRE_EX(SVM_EXIT_CPUID),
	[x86_intercept_rsm]		= PRE_EX(SVM_EXIT_RSM),
	[x86_intercept_pause]		= PRE_EX(SVM_EXIT_PAUSE),
	[x86_intercept_pushf]		= PRE_EX(SVM_EXIT_PUSHF),
	[x86_intercept_popf]		= PRE_EX(SVM_EXIT_POPF),
	[x86_intercept_intn]		= PRE_EX(SVM_EXIT_SWINT),
	[x86_intercept_iret]		= PRE_EX(SVM_EXIT_IRET),
	[x86_intercept_icebp]		= PRE_EX(SVM_EXIT_ICEBP),
	[x86_intercept_hlt]		= POST_EX(SVM_EXIT_HLT),
	[x86_intercept_in]		= POST_EX(SVM_EXIT_IOIO),
	[x86_intercept_ins]		= POST_EX(SVM_EXIT_IOIO),
	[x86_intercept_out]		= POST_EX(SVM_EXIT_IOIO),
	[x86_intercept_outs]		= POST_EX(SVM_EXIT_IOIO),
	[x86_intercept_xsetbv]		= PRE_EX(SVM_EXIT_XSETBV),
};

#undef PRE_EX
#undef POST_EX
#undef POST_MEM

static int svm_check_intercept(struct kvm_vcpu *vcpu,
			       struct x86_instruction_info *info,
			       enum x86_intercept_stage stage,
			       struct x86_exception *exception)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	int vmexit, ret = X86EMUL_CONTINUE;
	struct __x86_intercept icpt_info;
	struct vmcb *vmcb = svm->vmcb;

	if (info->intercept >= ARRAY_SIZE(x86_intercept_map))
		goto out;

	icpt_info = x86_intercept_map[info->intercept];

	if (stage != icpt_info.stage)
		goto out;

	switch (icpt_info.exit_code) {
	case SVM_EXIT_READ_CR0:
		if (info->intercept == x86_intercept_cr_read)
			icpt_info.exit_code += info->modrm_reg;
		break;
	case SVM_EXIT_WRITE_CR0: {
		unsigned long cr0, val;
		u64 intercept;

		if (info->intercept == x86_intercept_cr_write)
			icpt_info.exit_code += info->modrm_reg;

		if (icpt_info.exit_code != SVM_EXIT_WRITE_CR0 ||
		    info->intercept == x86_intercept_clts)
			break;

		intercept = svm->nested.intercept;

		if (!(intercept & (1ULL << INTERCEPT_SELECTIVE_CR0)))
			break;

		cr0 = vcpu->arch.cr0 & ~SVM_CR0_SELECTIVE_MASK;
		val = info->src_val  & ~SVM_CR0_SELECTIVE_MASK;

		if (info->intercept == x86_intercept_lmsw) {
			cr0 &= 0xfUL;
			val &= 0xfUL;
			/* lmsw can't clear PE - catch this here */
			if (cr0 & X86_CR0_PE)
				val |= X86_CR0_PE;
		}

		if (cr0 ^ val)
			icpt_info.exit_code = SVM_EXIT_CR0_SEL_WRITE;

		break;
	}
	case SVM_EXIT_READ_DR0:
	case SVM_EXIT_WRITE_DR0:
		icpt_info.exit_code += info->modrm_reg;
		break;
	case SVM_EXIT_MSR:
		if (info->intercept == x86_intercept_wrmsr)
			vmcb->control.exit_info_1 = 1;
		else
			vmcb->control.exit_info_1 = 0;
		break;
	case SVM_EXIT_PAUSE:
		/*
		 * We get this for NOP only, but pause
		 * is rep not, check this here
		 */
		if (info->rep_prefix != REPE_PREFIX)
			goto out;
		break;
	case SVM_EXIT_IOIO: {
		u64 exit_info;
		u32 bytes;

		if (info->intercept == x86_intercept_in ||
		    info->intercept == x86_intercept_ins) {
			exit_info = ((info->src_val & 0xffff) << 16) |
				SVM_IOIO_TYPE_MASK;
			bytes = info->dst_bytes;
		} else {
			exit_info = (info->dst_val & 0xffff) << 16;
			bytes = info->src_bytes;
		}

		if (info->intercept == x86_intercept_outs ||
		    info->intercept == x86_intercept_ins)
			exit_info |= SVM_IOIO_STR_MASK;

		if (info->rep_prefix)
			exit_info |= SVM_IOIO_REP_MASK;

		bytes = min(bytes, 4u);

		exit_info |= bytes << SVM_IOIO_SIZE_SHIFT;

		exit_info |= (u32)info->ad_bytes << (SVM_IOIO_ASIZE_SHIFT - 1);

		vmcb->control.exit_info_1 = exit_info;
		vmcb->control.exit_info_2 = info->next_rip;

		break;
	}
	default:
		break;
	}

	/* TODO: Advertise NRIPS to guest hypervisor unconditionally */
	if (static_cpu_has(X86_FEATURE_NRIPS))
		vmcb->control.next_rip  = info->next_rip;
	vmcb->control.exit_code = icpt_info.exit_code;
	vmexit = nested_svm_exit_handled(svm);

	ret = (vmexit == NESTED_EXIT_DONE) ? X86EMUL_INTERCEPTED
					   : X86EMUL_CONTINUE;

out:
	return ret;
}

static void svm_handle_exit_irqoff(struct kvm_vcpu *vcpu,
	enum exit_fastpath_completion *exit_fastpath)
{
	if (!is_guest_mode(vcpu) &&
	    to_svm(vcpu)->vmcb->control.exit_code == SVM_EXIT_MSR &&
	    to_svm(vcpu)->vmcb->control.exit_info_1)
		*exit_fastpath = handle_fastpath_set_msr_irqoff(vcpu);
}

static void svm_sched_in(struct kvm_vcpu *vcpu, int cpu)
{
	if (pause_filter_thresh)
		shrink_ple_window(vcpu);
}

static void svm_setup_mce(struct kvm_vcpu *vcpu)
{
	/* [63:9] are reserved. */
	vcpu->arch.mcg_cap &= 0x1ff;
}

static int svm_smi_allowed(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	/* Per APM Vol.2 15.22.2 "Response to SMI" */
	if (!gif_set(svm))
		return 0;

	if (is_guest_mode(&svm->vcpu) &&
	    svm->nested.intercept & (1ULL << INTERCEPT_SMI)) {
		/* TODO: Might need to set exit_info_1 and exit_info_2 here */
		svm->vmcb->control.exit_code = SVM_EXIT_SMI;
		svm->nested.exit_required = true;
		return 0;
	}

	return 1;
}

static int svm_pre_enter_smm(struct kvm_vcpu *vcpu, char *smstate)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	int ret;

	if (is_guest_mode(vcpu)) {
		/* FED8h - SVM Guest */
		put_smstate(u64, smstate, 0x7ed8, 1);
		/* FEE0h - SVM Guest VMCB Physical Address */
		put_smstate(u64, smstate, 0x7ee0, svm->nested.vmcb);

		svm->vmcb->save.rax = vcpu->arch.regs[VCPU_REGS_RAX];
		svm->vmcb->save.rsp = vcpu->arch.regs[VCPU_REGS_RSP];
		svm->vmcb->save.rip = vcpu->arch.regs[VCPU_REGS_RIP];

		ret = nested_svm_vmexit(svm);
		if (ret)
			return ret;
	}
	return 0;
}

static int svm_pre_leave_smm(struct kvm_vcpu *vcpu, const char *smstate)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb *nested_vmcb;
	struct kvm_host_map map;
	u64 guest;
	u64 vmcb;

	guest = GET_SMSTATE(u64, smstate, 0x7ed8);
	vmcb = GET_SMSTATE(u64, smstate, 0x7ee0);

	if (guest) {
		if (kvm_vcpu_map(&svm->vcpu, gpa_to_gfn(vmcb), &map) == -EINVAL)
			return 1;
		nested_vmcb = map.hva;
		enter_svm_guest_mode(svm, vmcb, nested_vmcb, &map);
	}
	return 0;
}

static int enable_smi_window(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (!gif_set(svm)) {
		if (vgif_enabled(svm))
			set_intercept(svm, INTERCEPT_STGI);
		/* STGI will cause a vm exit */
		return 1;
	}
	return 0;
}

static bool svm_need_emulation_on_page_fault(struct kvm_vcpu *vcpu)
{
	unsigned long cr4 = kvm_read_cr4(vcpu);
	bool smep = cr4 & X86_CR4_SMEP;
	bool smap = cr4 & X86_CR4_SMAP;
	bool is_user = svm_get_cpl(vcpu) == 3;

	/*
	 * Detect and workaround Errata 1096 Fam_17h_00_0Fh.
	 *
	 * Errata:
	 * When CPU raise #NPF on guest data access and vCPU CR4.SMAP=1, it is
	 * possible that CPU microcode implementing DecodeAssist will fail
	 * to read bytes of instruction which caused #NPF. In this case,
	 * GuestIntrBytes field of the VMCB on a VMEXIT will incorrectly
	 * return 0 instead of the correct guest instruction bytes.
	 *
	 * This happens because CPU microcode reading instruction bytes
	 * uses a special opcode which attempts to read data using CPL=0
	 * priviledges. The microcode reads CS:RIP and if it hits a SMAP
	 * fault, it gives up and returns no instruction bytes.
	 *
	 * Detection:
	 * We reach here in case CPU supports DecodeAssist, raised #NPF and
	 * returned 0 in GuestIntrBytes field of the VMCB.
	 * First, errata can only be triggered in case vCPU CR4.SMAP=1.
	 * Second, if vCPU CR4.SMEP=1, errata could only be triggered
	 * in case vCPU CPL==3 (Because otherwise guest would have triggered
	 * a SMEP fault instead of #NPF).
	 * Otherwise, vCPU CR4.SMEP=0, errata could be triggered by any vCPU CPL.
	 * As most guests enable SMAP if they have also enabled SMEP, use above
	 * logic in order to attempt minimize false-positive of detecting errata
	 * while still preserving all cases semantic correctness.
	 *
	 * Workaround:
	 * To determine what instruction the guest was executing, the hypervisor
	 * will have to decode the instruction at the instruction pointer.
	 *
	 * In non SEV guest, hypervisor will be able to read the guest
	 * memory to decode the instruction pointer when insn_len is zero
	 * so we return true to indicate that decoding is possible.
	 *
	 * But in the SEV guest, the guest memory is encrypted with the
	 * guest specific key and hypervisor will not be able to decode the
	 * instruction pointer so we will not able to workaround it. Lets
	 * print the error and request to kill the guest.
	 */
	if (smap && (!smep || is_user)) {
		if (!sev_guest(vcpu->kvm))
			return true;

		pr_err_ratelimited("KVM: SEV Guest triggered AMD Erratum 1096\n");
		kvm_make_request(KVM_REQ_TRIPLE_FAULT, vcpu);
	}

	return false;
}

static bool svm_apic_init_signal_blocked(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	/*
	 * TODO: Last condition latch INIT signals on vCPU when
	 * vCPU is in guest-mode and vmcb12 defines intercept on INIT.
	 * To properly emulate the INIT intercept, SVM should implement
	 * kvm_x86_ops.check_nested_events() and call nested_svm_vmexit()
	 * there if an INIT signal is pending.
	 */
	return !gif_set(svm) ||
		   (svm->vmcb->control.intercept & (1ULL << INTERCEPT_INIT));
}

static void svm_vm_destroy(struct kvm *kvm)
{
	avic_vm_destroy(kvm);
	sev_vm_destroy(kvm);
}

static int svm_vm_init(struct kvm *kvm)
{
	if (avic) {
		int ret = avic_vm_init(kvm);
		if (ret)
			return ret;
	}

	kvm_apicv_init(kvm, avic);
	return 0;
}

static struct kvm_x86_ops svm_x86_ops __initdata = {
	.hardware_unsetup = svm_hardware_teardown,
	.hardware_enable = svm_hardware_enable,
	.hardware_disable = svm_hardware_disable,
	.cpu_has_accelerated_tpr = svm_cpu_has_accelerated_tpr,
	.has_emulated_msr = svm_has_emulated_msr,

	.vcpu_create = svm_create_vcpu,
	.vcpu_free = svm_free_vcpu,
	.vcpu_reset = svm_vcpu_reset,

	.vm_size = sizeof(struct kvm_svm),
	.vm_init = svm_vm_init,
	.vm_destroy = svm_vm_destroy,

	.prepare_guest_switch = svm_prepare_guest_switch,
	.vcpu_load = svm_vcpu_load,
	.vcpu_put = svm_vcpu_put,
	.vcpu_blocking = svm_vcpu_blocking,
	.vcpu_unblocking = svm_vcpu_unblocking,

	.update_bp_intercept = update_bp_intercept,
	.get_msr_feature = svm_get_msr_feature,
	.get_msr = svm_get_msr,
	.set_msr = svm_set_msr,
	.get_segment_base = svm_get_segment_base,
	.get_segment = svm_get_segment,
	.set_segment = svm_set_segment,
	.get_cpl = svm_get_cpl,
	.get_cs_db_l_bits = kvm_get_cs_db_l_bits,
	.decache_cr0_guest_bits = svm_decache_cr0_guest_bits,
	.decache_cr4_guest_bits = svm_decache_cr4_guest_bits,
	.set_cr0 = svm_set_cr0,
	.set_cr4 = svm_set_cr4,
	.set_efer = svm_set_efer,
	.get_idt = svm_get_idt,
	.set_idt = svm_set_idt,
	.get_gdt = svm_get_gdt,
	.set_gdt = svm_set_gdt,
	.set_dr7 = svm_set_dr7,
	.sync_dirty_debug_regs = svm_sync_dirty_debug_regs,
	.cache_reg = svm_cache_reg,
	.get_rflags = svm_get_rflags,
	.set_rflags = svm_set_rflags,

	.tlb_flush = svm_flush_tlb,
	.tlb_flush_gva = svm_flush_tlb_gva,

	.run = svm_vcpu_run,
	.handle_exit = handle_exit,
	.skip_emulated_instruction = skip_emulated_instruction,
	.update_emulated_instruction = NULL,
	.set_interrupt_shadow = svm_set_interrupt_shadow,
	.get_interrupt_shadow = svm_get_interrupt_shadow,
	.patch_hypercall = svm_patch_hypercall,
	.set_irq = svm_set_irq,
	.set_nmi = svm_inject_nmi,
	.queue_exception = svm_queue_exception,
	.cancel_injection = svm_cancel_injection,
	.interrupt_allowed = svm_interrupt_allowed,
	.nmi_allowed = svm_nmi_allowed,
	.get_nmi_mask = svm_get_nmi_mask,
	.set_nmi_mask = svm_set_nmi_mask,
	.enable_nmi_window = enable_nmi_window,
	.enable_irq_window = enable_irq_window,
	.update_cr8_intercept = update_cr8_intercept,
	.set_virtual_apic_mode = svm_set_virtual_apic_mode,
	.refresh_apicv_exec_ctrl = svm_refresh_apicv_exec_ctrl,
	.check_apicv_inhibit_reasons = svm_check_apicv_inhibit_reasons,
	.pre_update_apicv_exec_ctrl = svm_pre_update_apicv_exec_ctrl,
	.load_eoi_exitmap = svm_load_eoi_exitmap,
	.hwapic_irr_update = svm_hwapic_irr_update,
	.hwapic_isr_update = svm_hwapic_isr_update,
	.sync_pir_to_irr = kvm_lapic_find_highest_irr,
	.apicv_post_state_restore = avic_post_state_restore,

	.set_tss_addr = svm_set_tss_addr,
	.set_identity_map_addr = svm_set_identity_map_addr,
	.get_tdp_level = get_npt_level,
	.get_mt_mask = svm_get_mt_mask,

	.get_exit_info = svm_get_exit_info,

	.cpuid_update = svm_cpuid_update,

	.has_wbinvd_exit = svm_has_wbinvd_exit,

	.read_l1_tsc_offset = svm_read_l1_tsc_offset,
	.write_l1_tsc_offset = svm_write_l1_tsc_offset,

	.load_mmu_pgd = svm_load_mmu_pgd,

	.check_intercept = svm_check_intercept,
	.handle_exit_irqoff = svm_handle_exit_irqoff,

	.request_immediate_exit = __kvm_request_immediate_exit,

	.sched_in = svm_sched_in,

	.pmu_ops = &amd_pmu_ops,
	.deliver_posted_interrupt = svm_deliver_avic_intr,
	.dy_apicv_has_pending_interrupt = svm_dy_apicv_has_pending_interrupt,
	.update_pi_irte = svm_update_pi_irte,
	.setup_mce = svm_setup_mce,

	.smi_allowed = svm_smi_allowed,
	.pre_enter_smm = svm_pre_enter_smm,
	.pre_leave_smm = svm_pre_leave_smm,
	.enable_smi_window = enable_smi_window,

	.mem_enc_op = svm_mem_enc_op,
	.mem_enc_reg_region = svm_register_enc_region,
	.mem_enc_unreg_region = svm_unregister_enc_region,

	.nested_enable_evmcs = NULL,
	.nested_get_evmcs_version = NULL,

	.need_emulation_on_page_fault = svm_need_emulation_on_page_fault,

	.apic_init_signal_blocked = svm_apic_init_signal_blocked,

	.check_nested_events = svm_check_nested_events,
};

static struct kvm_x86_init_ops svm_init_ops __initdata = {
	.cpu_has_kvm_support = has_svm,
	.disabled_by_bios = is_disabled,
	.hardware_setup = svm_hardware_setup,
	.check_processor_compatibility = svm_check_processor_compat,

	.runtime_ops = &svm_x86_ops,
};

static int __init svm_init(void)
{
	return kvm_init(&svm_init_ops, sizeof(struct vcpu_svm),
			__alignof__(struct vcpu_svm), THIS_MODULE);
}

static void __exit svm_exit(void)
{
	kvm_exit();
}

module_init(svm_init)
module_exit(svm_exit)
