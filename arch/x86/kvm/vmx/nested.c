// SPDX-License-Identifier: GPL-2.0

#include <linux/frame.h>
#include <linux/percpu.h>

#include <asm/debugreg.h>
#include <asm/mmu_context.h>

#include "cpuid.h"
#include "hyperv.h"
#include "mmu.h"
#include "nested.h"
#include "pmu.h"
#include "trace.h"
#include "x86.h"

static bool __read_mostly enable_shadow_vmcs = 1;
module_param_named(enable_shadow_vmcs, enable_shadow_vmcs, bool, S_IRUGO);

static bool __read_mostly nested_early_check = 0;
module_param(nested_early_check, bool, S_IRUGO);

#define CC(consistency_check)						\
({									\
	bool failed = (consistency_check);				\
	if (failed)							\
		trace_kvm_nested_vmenter_failed(#consistency_check, 0);	\
	failed;								\
})

/*
 * Hyper-V requires all of these, so mark them as supported even though
 * they are just treated the same as all-context.
 */
#define VMX_VPID_EXTENT_SUPPORTED_MASK		\
	(VMX_VPID_EXTENT_INDIVIDUAL_ADDR_BIT |	\
	VMX_VPID_EXTENT_SINGLE_CONTEXT_BIT |	\
	VMX_VPID_EXTENT_GLOBAL_CONTEXT_BIT |	\
	VMX_VPID_EXTENT_SINGLE_NON_GLOBAL_BIT)

#define VMX_MISC_EMULATED_PREEMPTION_TIMER_RATE 5

enum {
	VMX_VMREAD_BITMAP,
	VMX_VMWRITE_BITMAP,
	VMX_BITMAP_NR
};
static unsigned long *vmx_bitmap[VMX_BITMAP_NR];

#define vmx_vmread_bitmap                    (vmx_bitmap[VMX_VMREAD_BITMAP])
#define vmx_vmwrite_bitmap                   (vmx_bitmap[VMX_VMWRITE_BITMAP])

struct shadow_vmcs_field {
	u16	encoding;
	u16	offset;
};
static struct shadow_vmcs_field shadow_read_only_fields[] = {
#define SHADOW_FIELD_RO(x, y) { x, offsetof(struct vmcs12, y) },
#include "vmcs_shadow_fields.h"
};
static int max_shadow_read_only_fields =
	ARRAY_SIZE(shadow_read_only_fields);

static struct shadow_vmcs_field shadow_read_write_fields[] = {
#define SHADOW_FIELD_RW(x, y) { x, offsetof(struct vmcs12, y) },
#include "vmcs_shadow_fields.h"
};
static int max_shadow_read_write_fields =
	ARRAY_SIZE(shadow_read_write_fields);

static void init_vmcs_shadow_fields(void)
{
	int i, j;

	memset(vmx_vmread_bitmap, 0xff, PAGE_SIZE);
	memset(vmx_vmwrite_bitmap, 0xff, PAGE_SIZE);

	for (i = j = 0; i < max_shadow_read_only_fields; i++) {
		struct shadow_vmcs_field entry = shadow_read_only_fields[i];
		u16 field = entry.encoding;

		if (vmcs_field_width(field) == VMCS_FIELD_WIDTH_U64 &&
		    (i + 1 == max_shadow_read_only_fields ||
		     shadow_read_only_fields[i + 1].encoding != field + 1))
			pr_err("Missing field from shadow_read_only_field %x\n",
			       field + 1);

		clear_bit(field, vmx_vmread_bitmap);
		if (field & 1)
#ifdef CONFIG_X86_64
			continue;
#else
			entry.offset += sizeof(u32);
#endif
		shadow_read_only_fields[j++] = entry;
	}
	max_shadow_read_only_fields = j;

	for (i = j = 0; i < max_shadow_read_write_fields; i++) {
		struct shadow_vmcs_field entry = shadow_read_write_fields[i];
		u16 field = entry.encoding;

		if (vmcs_field_width(field) == VMCS_FIELD_WIDTH_U64 &&
		    (i + 1 == max_shadow_read_write_fields ||
		     shadow_read_write_fields[i + 1].encoding != field + 1))
			pr_err("Missing field from shadow_read_write_field %x\n",
			       field + 1);

		WARN_ONCE(field >= GUEST_ES_AR_BYTES &&
			  field <= GUEST_TR_AR_BYTES,
			  "Update vmcs12_write_any() to drop reserved bits from AR_BYTES");

		/*
		 * PML and the preemption timer can be emulated, but the
		 * processor cannot vmwrite to fields that don't exist
		 * on bare metal.
		 */
		switch (field) {
		case GUEST_PML_INDEX:
			if (!cpu_has_vmx_pml())
				continue;
			break;
		case VMX_PREEMPTION_TIMER_VALUE:
			if (!cpu_has_vmx_preemption_timer())
				continue;
			break;
		case GUEST_INTR_STATUS:
			if (!cpu_has_vmx_apicv())
				continue;
			break;
		default:
			break;
		}

		clear_bit(field, vmx_vmwrite_bitmap);
		clear_bit(field, vmx_vmread_bitmap);
		if (field & 1)
#ifdef CONFIG_X86_64
			continue;
#else
			entry.offset += sizeof(u32);
#endif
		shadow_read_write_fields[j++] = entry;
	}
	max_shadow_read_write_fields = j;
}

/*
 * The following 3 functions, nested_vmx_succeed()/failValid()/failInvalid(),
 * set the success or error code of an emulated VMX instruction (as specified
 * by Vol 2B, VMX Instruction Reference, "Conventions"), and skip the emulated
 * instruction.
 */
static int nested_vmx_succeed(struct kvm_vcpu *vcpu)
{
	vmx_set_rflags(vcpu, vmx_get_rflags(vcpu)
			& ~(X86_EFLAGS_CF | X86_EFLAGS_PF | X86_EFLAGS_AF |
			    X86_EFLAGS_ZF | X86_EFLAGS_SF | X86_EFLAGS_OF));
	return kvm_skip_emulated_instruction(vcpu);
}

static int nested_vmx_failInvalid(struct kvm_vcpu *vcpu)
{
	vmx_set_rflags(vcpu, (vmx_get_rflags(vcpu)
			& ~(X86_EFLAGS_PF | X86_EFLAGS_AF | X86_EFLAGS_ZF |
			    X86_EFLAGS_SF | X86_EFLAGS_OF))
			| X86_EFLAGS_CF);
	return kvm_skip_emulated_instruction(vcpu);
}

static int nested_vmx_failValid(struct kvm_vcpu *vcpu,
				u32 vm_instruction_error)
{
	vmx_set_rflags(vcpu, (vmx_get_rflags(vcpu)
			& ~(X86_EFLAGS_CF | X86_EFLAGS_PF | X86_EFLAGS_AF |
			    X86_EFLAGS_SF | X86_EFLAGS_OF))
			| X86_EFLAGS_ZF);
	get_vmcs12(vcpu)->vm_instruction_error = vm_instruction_error;
	/*
	 * We don't need to force a shadow sync because
	 * VM_INSTRUCTION_ERROR is not shadowed
	 */
	return kvm_skip_emulated_instruction(vcpu);
}

static int nested_vmx_fail(struct kvm_vcpu *vcpu, u32 vm_instruction_error)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	/*
	 * failValid writes the error number to the current VMCS, which
	 * can't be done if there isn't a current VMCS.
	 */
	if (vmx->nested.current_vmptr == -1ull && !vmx->nested.hv_evmcs)
		return nested_vmx_failInvalid(vcpu);

	return nested_vmx_failValid(vcpu, vm_instruction_error);
}

static void nested_vmx_abort(struct kvm_vcpu *vcpu, u32 indicator)
{
	/* TODO: not to reset guest simply here. */
	kvm_make_request(KVM_REQ_TRIPLE_FAULT, vcpu);
	pr_debug_ratelimited("kvm: nested vmx abort, indicator %d\n", indicator);
}

static inline bool vmx_control_verify(u32 control, u32 low, u32 high)
{
	return fixed_bits_valid(control, low, high);
}

static inline u64 vmx_control_msr(u32 low, u32 high)
{
	return low | ((u64)high << 32);
}

static void vmx_disable_shadow_vmcs(struct vcpu_vmx *vmx)
{
	secondary_exec_controls_clearbit(vmx, SECONDARY_EXEC_SHADOW_VMCS);
	vmcs_write64(VMCS_LINK_POINTER, -1ull);
	vmx->nested.need_vmcs12_to_shadow_sync = false;
}

static inline void nested_release_evmcs(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (!vmx->nested.hv_evmcs)
		return;

	kvm_vcpu_unmap(vcpu, &vmx->nested.hv_evmcs_map, true);
	vmx->nested.hv_evmcs_vmptr = 0;
	vmx->nested.hv_evmcs = NULL;
}

/*
 * Free whatever needs to be freed from vmx->nested when L1 goes down, or
 * just stops using VMX.
 */
static void free_nested(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (!vmx->nested.vmxon && !vmx->nested.smm.vmxon)
		return;

	kvm_clear_request(KVM_REQ_GET_VMCS12_PAGES, vcpu);

	vmx->nested.vmxon = false;
	vmx->nested.smm.vmxon = false;
	free_vpid(vmx->nested.vpid02);
	vmx->nested.posted_intr_nv = -1;
	vmx->nested.current_vmptr = -1ull;
	if (enable_shadow_vmcs) {
		vmx_disable_shadow_vmcs(vmx);
		vmcs_clear(vmx->vmcs01.shadow_vmcs);
		free_vmcs(vmx->vmcs01.shadow_vmcs);
		vmx->vmcs01.shadow_vmcs = NULL;
	}
	kfree(vmx->nested.cached_vmcs12);
	vmx->nested.cached_vmcs12 = NULL;
	kfree(vmx->nested.cached_shadow_vmcs12);
	vmx->nested.cached_shadow_vmcs12 = NULL;
	/* Unpin physical memory we referred to in the vmcs02 */
	if (vmx->nested.apic_access_page) {
		kvm_release_page_clean(vmx->nested.apic_access_page);
		vmx->nested.apic_access_page = NULL;
	}
	kvm_vcpu_unmap(vcpu, &vmx->nested.virtual_apic_map, true);
	kvm_vcpu_unmap(vcpu, &vmx->nested.pi_desc_map, true);
	vmx->nested.pi_desc = NULL;

	kvm_mmu_free_roots(vcpu, &vcpu->arch.guest_mmu, KVM_MMU_ROOTS_ALL);

	nested_release_evmcs(vcpu);

	free_loaded_vmcs(&vmx->nested.vmcs02);
}

static void vmx_sync_vmcs_host_state(struct vcpu_vmx *vmx,
				     struct loaded_vmcs *prev)
{
	struct vmcs_host_state *dest, *src;

	if (unlikely(!vmx->guest_state_loaded))
		return;

	src = &prev->host_state;
	dest = &vmx->loaded_vmcs->host_state;

	vmx_set_host_fs_gs(dest, src->fs_sel, src->gs_sel, src->fs_base, src->gs_base);
	dest->ldt_sel = src->ldt_sel;
#ifdef CONFIG_X86_64
	dest->ds_sel = src->ds_sel;
	dest->es_sel = src->es_sel;
#endif
}

static void vmx_switch_vmcs(struct kvm_vcpu *vcpu, struct loaded_vmcs *vmcs)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct loaded_vmcs *prev;
	int cpu;

	if (vmx->loaded_vmcs == vmcs)
		return;

	cpu = get_cpu();
	prev = vmx->loaded_vmcs;
	vmx->loaded_vmcs = vmcs;
	vmx_vcpu_load_vmcs(vcpu, cpu, prev);
	vmx_sync_vmcs_host_state(vmx, prev);
	put_cpu();

	vmx_register_cache_reset(vcpu);
}

/*
 * Ensure that the current vmcs of the logical processor is the
 * vmcs01 of the vcpu before calling free_nested().
 */
void nested_vmx_free_vcpu(struct kvm_vcpu *vcpu)
{
	vcpu_load(vcpu);
	vmx_leave_nested(vcpu);
	vmx_switch_vmcs(vcpu, &to_vmx(vcpu)->vmcs01);
	free_nested(vcpu);
	vcpu_put(vcpu);
}

static void nested_ept_inject_page_fault(struct kvm_vcpu *vcpu,
		struct x86_exception *fault)
{
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	u32 vm_exit_reason;
	unsigned long exit_qualification = vcpu->arch.exit_qualification;

	if (vmx->nested.pml_full) {
		vm_exit_reason = EXIT_REASON_PML_FULL;
		vmx->nested.pml_full = false;
		exit_qualification &= INTR_INFO_UNBLOCK_NMI;
	} else if (fault->error_code & PFERR_RSVD_MASK)
		vm_exit_reason = EXIT_REASON_EPT_MISCONFIG;
	else
		vm_exit_reason = EXIT_REASON_EPT_VIOLATION;

	nested_vmx_vmexit(vcpu, vm_exit_reason, 0, exit_qualification);
	vmcs12->guest_physical_address = fault->address;
}

static void nested_ept_init_mmu_context(struct kvm_vcpu *vcpu)
{
	WARN_ON(mmu_is_nested(vcpu));

	vcpu->arch.mmu = &vcpu->arch.guest_mmu;
	kvm_init_shadow_ept_mmu(vcpu,
			to_vmx(vcpu)->nested.msrs.ept_caps &
			VMX_EPT_EXECUTE_ONLY_BIT,
			nested_ept_ad_enabled(vcpu),
			nested_ept_get_eptp(vcpu));
	vcpu->arch.mmu->get_guest_pgd     = nested_ept_get_eptp;
	vcpu->arch.mmu->inject_page_fault = nested_ept_inject_page_fault;
	vcpu->arch.mmu->get_pdptr         = kvm_pdptr_read;

	vcpu->arch.walk_mmu              = &vcpu->arch.nested_mmu;
}

static void nested_ept_uninit_mmu_context(struct kvm_vcpu *vcpu)
{
	vcpu->arch.mmu = &vcpu->arch.root_mmu;
	vcpu->arch.walk_mmu = &vcpu->arch.root_mmu;
}

static bool nested_vmx_is_page_fault_vmexit(struct vmcs12 *vmcs12,
					    u16 error_code)
{
	bool inequality, bit;

	bit = (vmcs12->exception_bitmap & (1u << PF_VECTOR)) != 0;
	inequality =
		(error_code & vmcs12->page_fault_error_code_mask) !=
		 vmcs12->page_fault_error_code_match;
	return inequality ^ bit;
}


/*
 * KVM wants to inject page-faults which it got to the guest. This function
 * checks whether in a nested guest, we need to inject them to L1 or L2.
 */
static int nested_vmx_check_exception(struct kvm_vcpu *vcpu, unsigned long *exit_qual)
{
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
	unsigned int nr = vcpu->arch.exception.nr;
	bool has_payload = vcpu->arch.exception.has_payload;
	unsigned long payload = vcpu->arch.exception.payload;

	if (nr == PF_VECTOR) {
		if (vcpu->arch.exception.nested_apf) {
			*exit_qual = vcpu->arch.apf.nested_apf_token;
			return 1;
		}
		if (nested_vmx_is_page_fault_vmexit(vmcs12,
						    vcpu->arch.exception.error_code)) {
			*exit_qual = has_payload ? payload : vcpu->arch.cr2;
			return 1;
		}
	} else if (vmcs12->exception_bitmap & (1u << nr)) {
		if (nr == DB_VECTOR) {
			if (!has_payload) {
				payload = vcpu->arch.dr6;
				payload &= ~(DR6_FIXED_1 | DR6_BT);
				payload ^= DR6_RTM;
			}
			*exit_qual = payload;
		} else
			*exit_qual = 0;
		return 1;
	}

	return 0;
}


static void vmx_inject_page_fault_nested(struct kvm_vcpu *vcpu,
		struct x86_exception *fault)
{
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);

	WARN_ON(!is_guest_mode(vcpu));

	if (nested_vmx_is_page_fault_vmexit(vmcs12, fault->error_code) &&
		!to_vmx(vcpu)->nested.nested_run_pending) {
		vmcs12->vm_exit_intr_error_code = fault->error_code;
		nested_vmx_vmexit(vcpu, EXIT_REASON_EXCEPTION_NMI,
				  PF_VECTOR | INTR_TYPE_HARD_EXCEPTION |
				  INTR_INFO_DELIVER_CODE_MASK | INTR_INFO_VALID_MASK,
				  fault->address);
	} else {
		kvm_inject_page_fault(vcpu, fault);
	}
}

static int nested_vmx_check_io_bitmap_controls(struct kvm_vcpu *vcpu,
					       struct vmcs12 *vmcs12)
{
	if (!nested_cpu_has(vmcs12, CPU_BASED_USE_IO_BITMAPS))
		return 0;

	if (CC(!page_address_valid(vcpu, vmcs12->io_bitmap_a)) ||
	    CC(!page_address_valid(vcpu, vmcs12->io_bitmap_b)))
		return -EINVAL;

	return 0;
}

static int nested_vmx_check_msr_bitmap_controls(struct kvm_vcpu *vcpu,
						struct vmcs12 *vmcs12)
{
	if (!nested_cpu_has(vmcs12, CPU_BASED_USE_MSR_BITMAPS))
		return 0;

	if (CC(!page_address_valid(vcpu, vmcs12->msr_bitmap)))
		return -EINVAL;

	return 0;
}

static int nested_vmx_check_tpr_shadow_controls(struct kvm_vcpu *vcpu,
						struct vmcs12 *vmcs12)
{
	if (!nested_cpu_has(vmcs12, CPU_BASED_TPR_SHADOW))
		return 0;

	if (CC(!page_address_valid(vcpu, vmcs12->virtual_apic_page_addr)))
		return -EINVAL;

	return 0;
}

/*
 * Check if MSR is intercepted for L01 MSR bitmap.
 */
static bool msr_write_intercepted_l01(struct kvm_vcpu *vcpu, u32 msr)
{
	unsigned long *msr_bitmap;
	int f = sizeof(unsigned long);

	if (!cpu_has_vmx_msr_bitmap())
		return true;

	msr_bitmap = to_vmx(vcpu)->vmcs01.msr_bitmap;

	if (msr <= 0x1fff) {
		return !!test_bit(msr, msr_bitmap + 0x800 / f);
	} else if ((msr >= 0xc0000000) && (msr <= 0xc0001fff)) {
		msr &= 0x1fff;
		return !!test_bit(msr, msr_bitmap + 0xc00 / f);
	}

	return true;
}

/*
 * If a msr is allowed by L0, we should check whether it is allowed by L1.
 * The corresponding bit will be cleared unless both of L0 and L1 allow it.
 */
static void nested_vmx_disable_intercept_for_msr(unsigned long *msr_bitmap_l1,
					       unsigned long *msr_bitmap_nested,
					       u32 msr, int type)
{
	int f = sizeof(unsigned long);

	/*
	 * See Intel PRM Vol. 3, 20.6.9 (MSR-Bitmap Address). Early manuals
	 * have the write-low and read-high bitmap offsets the wrong way round.
	 * We can control MSRs 0x00000000-0x00001fff and 0xc0000000-0xc0001fff.
	 */
	if (msr <= 0x1fff) {
		if (type & MSR_TYPE_R &&
		   !test_bit(msr, msr_bitmap_l1 + 0x000 / f))
			/* read-low */
			__clear_bit(msr, msr_bitmap_nested + 0x000 / f);

		if (type & MSR_TYPE_W &&
		   !test_bit(msr, msr_bitmap_l1 + 0x800 / f))
			/* write-low */
			__clear_bit(msr, msr_bitmap_nested + 0x800 / f);

	} else if ((msr >= 0xc0000000) && (msr <= 0xc0001fff)) {
		msr &= 0x1fff;
		if (type & MSR_TYPE_R &&
		   !test_bit(msr, msr_bitmap_l1 + 0x400 / f))
			/* read-high */
			__clear_bit(msr, msr_bitmap_nested + 0x400 / f);

		if (type & MSR_TYPE_W &&
		   !test_bit(msr, msr_bitmap_l1 + 0xc00 / f))
			/* write-high */
			__clear_bit(msr, msr_bitmap_nested + 0xc00 / f);

	}
}

static inline void enable_x2apic_msr_intercepts(unsigned long *msr_bitmap)
{
	int msr;

	for (msr = 0x800; msr <= 0x8ff; msr += BITS_PER_LONG) {
		unsigned word = msr / BITS_PER_LONG;

		msr_bitmap[word] = ~0;
		msr_bitmap[word + (0x800 / sizeof(long))] = ~0;
	}
}

/*
 * Merge L0's and L1's MSR bitmap, return false to indicate that
 * we do not use the hardware.
 */
static inline bool nested_vmx_prepare_msr_bitmap(struct kvm_vcpu *vcpu,
						 struct vmcs12 *vmcs12)
{
	int msr;
	unsigned long *msr_bitmap_l1;
	unsigned long *msr_bitmap_l0 = to_vmx(vcpu)->nested.vmcs02.msr_bitmap;
	struct kvm_host_map *map = &to_vmx(vcpu)->nested.msr_bitmap_map;

	/* Nothing to do if the MSR bitmap is not in use.  */
	if (!cpu_has_vmx_msr_bitmap() ||
	    !nested_cpu_has(vmcs12, CPU_BASED_USE_MSR_BITMAPS))
		return false;

	if (kvm_vcpu_map(vcpu, gpa_to_gfn(vmcs12->msr_bitmap), map))
		return false;

	msr_bitmap_l1 = (unsigned long *)map->hva;

	/*
	 * To keep the control flow simple, pay eight 8-byte writes (sixteen
	 * 4-byte writes on 32-bit systems) up front to enable intercepts for
	 * the x2APIC MSR range and selectively disable them below.
	 */
	enable_x2apic_msr_intercepts(msr_bitmap_l0);

	if (nested_cpu_has_virt_x2apic_mode(vmcs12)) {
		if (nested_cpu_has_apic_reg_virt(vmcs12)) {
			/*
			 * L0 need not intercept reads for MSRs between 0x800
			 * and 0x8ff, it just lets the processor take the value
			 * from the virtual-APIC page; take those 256 bits
			 * directly from the L1 bitmap.
			 */
			for (msr = 0x800; msr <= 0x8ff; msr += BITS_PER_LONG) {
				unsigned word = msr / BITS_PER_LONG;

				msr_bitmap_l0[word] = msr_bitmap_l1[word];
			}
		}

		nested_vmx_disable_intercept_for_msr(
			msr_bitmap_l1, msr_bitmap_l0,
			X2APIC_MSR(APIC_TASKPRI),
			MSR_TYPE_R | MSR_TYPE_W);

		if (nested_cpu_has_vid(vmcs12)) {
			nested_vmx_disable_intercept_for_msr(
				msr_bitmap_l1, msr_bitmap_l0,
				X2APIC_MSR(APIC_EOI),
				MSR_TYPE_W);
			nested_vmx_disable_intercept_for_msr(
				msr_bitmap_l1, msr_bitmap_l0,
				X2APIC_MSR(APIC_SELF_IPI),
				MSR_TYPE_W);
		}
	}

	/* KVM unconditionally exposes the FS/GS base MSRs to L1. */
	nested_vmx_disable_intercept_for_msr(msr_bitmap_l1, msr_bitmap_l0,
					     MSR_FS_BASE, MSR_TYPE_RW);

	nested_vmx_disable_intercept_for_msr(msr_bitmap_l1, msr_bitmap_l0,
					     MSR_GS_BASE, MSR_TYPE_RW);

	nested_vmx_disable_intercept_for_msr(msr_bitmap_l1, msr_bitmap_l0,
					     MSR_KERNEL_GS_BASE, MSR_TYPE_RW);

	/*
	 * Checking the L0->L1 bitmap is trying to verify two things:
	 *
	 * 1. L0 gave a permission to L1 to actually passthrough the MSR. This
	 *    ensures that we do not accidentally generate an L02 MSR bitmap
	 *    from the L12 MSR bitmap that is too permissive.
	 * 2. That L1 or L2s have actually used the MSR. This avoids
	 *    unnecessarily merging of the bitmap if the MSR is unused. This
	 *    works properly because we only update the L01 MSR bitmap lazily.
	 *    So even if L0 should pass L1 these MSRs, the L01 bitmap is only
	 *    updated to reflect this when L1 (or its L2s) actually write to
	 *    the MSR.
	 */
	if (!msr_write_intercepted_l01(vcpu, MSR_IA32_SPEC_CTRL))
		nested_vmx_disable_intercept_for_msr(
					msr_bitmap_l1, msr_bitmap_l0,
					MSR_IA32_SPEC_CTRL,
					MSR_TYPE_R | MSR_TYPE_W);

	if (!msr_write_intercepted_l01(vcpu, MSR_IA32_PRED_CMD))
		nested_vmx_disable_intercept_for_msr(
					msr_bitmap_l1, msr_bitmap_l0,
					MSR_IA32_PRED_CMD,
					MSR_TYPE_W);

	kvm_vcpu_unmap(vcpu, &to_vmx(vcpu)->nested.msr_bitmap_map, false);

	return true;
}

static void nested_cache_shadow_vmcs12(struct kvm_vcpu *vcpu,
				       struct vmcs12 *vmcs12)
{
	struct kvm_host_map map;
	struct vmcs12 *shadow;

	if (!nested_cpu_has_shadow_vmcs(vmcs12) ||
	    vmcs12->vmcs_link_pointer == -1ull)
		return;

	shadow = get_shadow_vmcs12(vcpu);

	if (kvm_vcpu_map(vcpu, gpa_to_gfn(vmcs12->vmcs_link_pointer), &map))
		return;

	memcpy(shadow, map.hva, VMCS12_SIZE);
	kvm_vcpu_unmap(vcpu, &map, false);
}

static void nested_flush_cached_shadow_vmcs12(struct kvm_vcpu *vcpu,
					      struct vmcs12 *vmcs12)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (!nested_cpu_has_shadow_vmcs(vmcs12) ||
	    vmcs12->vmcs_link_pointer == -1ull)
		return;

	kvm_write_guest(vmx->vcpu.kvm, vmcs12->vmcs_link_pointer,
			get_shadow_vmcs12(vcpu), VMCS12_SIZE);
}

/*
 * In nested virtualization, check if L1 has set
 * VM_EXIT_ACK_INTR_ON_EXIT
 */
static bool nested_exit_intr_ack_set(struct kvm_vcpu *vcpu)
{
	return get_vmcs12(vcpu)->vm_exit_controls &
		VM_EXIT_ACK_INTR_ON_EXIT;
}

static int nested_vmx_check_apic_access_controls(struct kvm_vcpu *vcpu,
					  struct vmcs12 *vmcs12)
{
	if (nested_cpu_has2(vmcs12, SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES) &&
	    CC(!page_address_valid(vcpu, vmcs12->apic_access_addr)))
		return -EINVAL;
	else
		return 0;
}

static int nested_vmx_check_apicv_controls(struct kvm_vcpu *vcpu,
					   struct vmcs12 *vmcs12)
{
	if (!nested_cpu_has_virt_x2apic_mode(vmcs12) &&
	    !nested_cpu_has_apic_reg_virt(vmcs12) &&
	    !nested_cpu_has_vid(vmcs12) &&
	    !nested_cpu_has_posted_intr(vmcs12))
		return 0;

	/*
	 * If virtualize x2apic mode is enabled,
	 * virtualize apic access must be disabled.
	 */
	if (CC(nested_cpu_has_virt_x2apic_mode(vmcs12) &&
	       nested_cpu_has2(vmcs12, SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES)))
		return -EINVAL;

	/*
	 * If virtual interrupt delivery is enabled,
	 * we must exit on external interrupts.
	 */
	if (CC(nested_cpu_has_vid(vmcs12) && !nested_exit_on_intr(vcpu)))
		return -EINVAL;

	/*
	 * bits 15:8 should be zero in posted_intr_nv,
	 * the descriptor address has been already checked
	 * in nested_get_vmcs12_pages.
	 *
	 * bits 5:0 of posted_intr_desc_addr should be zero.
	 */
	if (nested_cpu_has_posted_intr(vmcs12) &&
	   (CC(!nested_cpu_has_vid(vmcs12)) ||
	    CC(!nested_exit_intr_ack_set(vcpu)) ||
	    CC((vmcs12->posted_intr_nv & 0xff00)) ||
	    CC((vmcs12->posted_intr_desc_addr & 0x3f)) ||
	    CC((vmcs12->posted_intr_desc_addr >> cpuid_maxphyaddr(vcpu)))))
		return -EINVAL;

	/* tpr shadow is needed by all apicv features. */
	if (CC(!nested_cpu_has(vmcs12, CPU_BASED_TPR_SHADOW)))
		return -EINVAL;

	return 0;
}

static int nested_vmx_check_msr_switch(struct kvm_vcpu *vcpu,
				       u32 count, u64 addr)
{
	int maxphyaddr;

	if (count == 0)
		return 0;
	maxphyaddr = cpuid_maxphyaddr(vcpu);
	if (!IS_ALIGNED(addr, 16) || addr >> maxphyaddr ||
	    (addr + count * sizeof(struct vmx_msr_entry) - 1) >> maxphyaddr)
		return -EINVAL;

	return 0;
}

static int nested_vmx_check_exit_msr_switch_controls(struct kvm_vcpu *vcpu,
						     struct vmcs12 *vmcs12)
{
	if (CC(nested_vmx_check_msr_switch(vcpu,
					   vmcs12->vm_exit_msr_load_count,
					   vmcs12->vm_exit_msr_load_addr)) ||
	    CC(nested_vmx_check_msr_switch(vcpu,
					   vmcs12->vm_exit_msr_store_count,
					   vmcs12->vm_exit_msr_store_addr)))
		return -EINVAL;

	return 0;
}

static int nested_vmx_check_entry_msr_switch_controls(struct kvm_vcpu *vcpu,
                                                      struct vmcs12 *vmcs12)
{
	if (CC(nested_vmx_check_msr_switch(vcpu,
					   vmcs12->vm_entry_msr_load_count,
					   vmcs12->vm_entry_msr_load_addr)))
                return -EINVAL;

	return 0;
}

static int nested_vmx_check_pml_controls(struct kvm_vcpu *vcpu,
					 struct vmcs12 *vmcs12)
{
	if (!nested_cpu_has_pml(vmcs12))
		return 0;

	if (CC(!nested_cpu_has_ept(vmcs12)) ||
	    CC(!page_address_valid(vcpu, vmcs12->pml_address)))
		return -EINVAL;

	return 0;
}

static int nested_vmx_check_unrestricted_guest_controls(struct kvm_vcpu *vcpu,
							struct vmcs12 *vmcs12)
{
	if (CC(nested_cpu_has2(vmcs12, SECONDARY_EXEC_UNRESTRICTED_GUEST) &&
	       !nested_cpu_has_ept(vmcs12)))
		return -EINVAL;
	return 0;
}

static int nested_vmx_check_mode_based_ept_exec_controls(struct kvm_vcpu *vcpu,
							 struct vmcs12 *vmcs12)
{
	if (CC(nested_cpu_has2(vmcs12, SECONDARY_EXEC_MODE_BASED_EPT_EXEC) &&
	       !nested_cpu_has_ept(vmcs12)))
		return -EINVAL;
	return 0;
}

static int nested_vmx_check_shadow_vmcs_controls(struct kvm_vcpu *vcpu,
						 struct vmcs12 *vmcs12)
{
	if (!nested_cpu_has_shadow_vmcs(vmcs12))
		return 0;

	if (CC(!page_address_valid(vcpu, vmcs12->vmread_bitmap)) ||
	    CC(!page_address_valid(vcpu, vmcs12->vmwrite_bitmap)))
		return -EINVAL;

	return 0;
}

static int nested_vmx_msr_check_common(struct kvm_vcpu *vcpu,
				       struct vmx_msr_entry *e)
{
	/* x2APIC MSR accesses are not allowed */
	if (CC(vcpu->arch.apic_base & X2APIC_ENABLE && e->index >> 8 == 0x8))
		return -EINVAL;
	if (CC(e->index == MSR_IA32_UCODE_WRITE) || /* SDM Table 35-2 */
	    CC(e->index == MSR_IA32_UCODE_REV))
		return -EINVAL;
	if (CC(e->reserved != 0))
		return -EINVAL;
	return 0;
}

static int nested_vmx_load_msr_check(struct kvm_vcpu *vcpu,
				     struct vmx_msr_entry *e)
{
	if (CC(e->index == MSR_FS_BASE) ||
	    CC(e->index == MSR_GS_BASE) ||
	    CC(e->index == MSR_IA32_SMM_MONITOR_CTL) || /* SMM is not supported */
	    nested_vmx_msr_check_common(vcpu, e))
		return -EINVAL;
	return 0;
}

static int nested_vmx_store_msr_check(struct kvm_vcpu *vcpu,
				      struct vmx_msr_entry *e)
{
	if (CC(e->index == MSR_IA32_SMBASE) || /* SMM is not supported */
	    nested_vmx_msr_check_common(vcpu, e))
		return -EINVAL;
	return 0;
}

static u32 nested_vmx_max_atomic_switch_msrs(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	u64 vmx_misc = vmx_control_msr(vmx->nested.msrs.misc_low,
				       vmx->nested.msrs.misc_high);

	return (vmx_misc_max_msr(vmx_misc) + 1) * VMX_MISC_MSR_LIST_MULTIPLIER;
}

/*
 * Load guest's/host's msr at nested entry/exit.
 * return 0 for success, entry index for failure.
 *
 * One of the failure modes for MSR load/store is when a list exceeds the
 * virtual hardware's capacity. To maintain compatibility with hardware inasmuch
 * as possible, process all valid entries before failing rather than precheck
 * for a capacity violation.
 */
static u32 nested_vmx_load_msr(struct kvm_vcpu *vcpu, u64 gpa, u32 count)
{
	u32 i;
	struct vmx_msr_entry e;
	u32 max_msr_list_size = nested_vmx_max_atomic_switch_msrs(vcpu);

	for (i = 0; i < count; i++) {
		if (unlikely(i >= max_msr_list_size))
			goto fail;

		if (kvm_vcpu_read_guest(vcpu, gpa + i * sizeof(e),
					&e, sizeof(e))) {
			pr_debug_ratelimited(
				"%s cannot read MSR entry (%u, 0x%08llx)\n",
				__func__, i, gpa + i * sizeof(e));
			goto fail;
		}
		if (nested_vmx_load_msr_check(vcpu, &e)) {
			pr_debug_ratelimited(
				"%s check failed (%u, 0x%x, 0x%x)\n",
				__func__, i, e.index, e.reserved);
			goto fail;
		}
		if (kvm_set_msr(vcpu, e.index, e.value)) {
			pr_debug_ratelimited(
				"%s cannot write MSR (%u, 0x%x, 0x%llx)\n",
				__func__, i, e.index, e.value);
			goto fail;
		}
	}
	return 0;
fail:
	/* Note, max_msr_list_size is at most 4096, i.e. this can't wrap. */
	return i + 1;
}

static bool nested_vmx_get_vmexit_msr_value(struct kvm_vcpu *vcpu,
					    u32 msr_index,
					    u64 *data)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	/*
	 * If the L0 hypervisor stored a more accurate value for the TSC that
	 * does not include the time taken for emulation of the L2->L1
	 * VM-exit in L0, use the more accurate value.
	 */
	if (msr_index == MSR_IA32_TSC) {
		int index = vmx_find_msr_index(&vmx->msr_autostore.guest,
					       MSR_IA32_TSC);

		if (index >= 0) {
			u64 val = vmx->msr_autostore.guest.val[index].value;

			*data = kvm_read_l1_tsc(vcpu, val);
			return true;
		}
	}

	if (kvm_get_msr(vcpu, msr_index, data)) {
		pr_debug_ratelimited("%s cannot read MSR (0x%x)\n", __func__,
			msr_index);
		return false;
	}
	return true;
}

static bool read_and_check_msr_entry(struct kvm_vcpu *vcpu, u64 gpa, int i,
				     struct vmx_msr_entry *e)
{
	if (kvm_vcpu_read_guest(vcpu,
				gpa + i * sizeof(*e),
				e, 2 * sizeof(u32))) {
		pr_debug_ratelimited(
			"%s cannot read MSR entry (%u, 0x%08llx)\n",
			__func__, i, gpa + i * sizeof(*e));
		return false;
	}
	if (nested_vmx_store_msr_check(vcpu, e)) {
		pr_debug_ratelimited(
			"%s check failed (%u, 0x%x, 0x%x)\n",
			__func__, i, e->index, e->reserved);
		return false;
	}
	return true;
}

static int nested_vmx_store_msr(struct kvm_vcpu *vcpu, u64 gpa, u32 count)
{
	u64 data;
	u32 i;
	struct vmx_msr_entry e;
	u32 max_msr_list_size = nested_vmx_max_atomic_switch_msrs(vcpu);

	for (i = 0; i < count; i++) {
		if (unlikely(i >= max_msr_list_size))
			return -EINVAL;

		if (!read_and_check_msr_entry(vcpu, gpa, i, &e))
			return -EINVAL;

		if (!nested_vmx_get_vmexit_msr_value(vcpu, e.index, &data))
			return -EINVAL;

		if (kvm_vcpu_write_guest(vcpu,
					 gpa + i * sizeof(e) +
					     offsetof(struct vmx_msr_entry, value),
					 &data, sizeof(data))) {
			pr_debug_ratelimited(
				"%s cannot write MSR (%u, 0x%x, 0x%llx)\n",
				__func__, i, e.index, data);
			return -EINVAL;
		}
	}
	return 0;
}

static bool nested_msr_store_list_has_msr(struct kvm_vcpu *vcpu, u32 msr_index)
{
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
	u32 count = vmcs12->vm_exit_msr_store_count;
	u64 gpa = vmcs12->vm_exit_msr_store_addr;
	struct vmx_msr_entry e;
	u32 i;

	for (i = 0; i < count; i++) {
		if (!read_and_check_msr_entry(vcpu, gpa, i, &e))
			return false;

		if (e.index == msr_index)
			return true;
	}
	return false;
}

static void prepare_vmx_msr_autostore_list(struct kvm_vcpu *vcpu,
					   u32 msr_index)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct vmx_msrs *autostore = &vmx->msr_autostore.guest;
	bool in_vmcs12_store_list;
	int msr_autostore_index;
	bool in_autostore_list;
	int last;

	msr_autostore_index = vmx_find_msr_index(autostore, msr_index);
	in_autostore_list = msr_autostore_index >= 0;
	in_vmcs12_store_list = nested_msr_store_list_has_msr(vcpu, msr_index);

	if (in_vmcs12_store_list && !in_autostore_list) {
		if (autostore->nr == NR_LOADSTORE_MSRS) {
			/*
			 * Emulated VMEntry does not fail here.  Instead a less
			 * accurate value will be returned by
			 * nested_vmx_get_vmexit_msr_value() using kvm_get_msr()
			 * instead of reading the value from the vmcs02 VMExit
			 * MSR-store area.
			 */
			pr_warn_ratelimited(
				"Not enough msr entries in msr_autostore.  Can't add msr %x\n",
				msr_index);
			return;
		}
		last = autostore->nr++;
		autostore->val[last].index = msr_index;
	} else if (!in_vmcs12_store_list && in_autostore_list) {
		last = --autostore->nr;
		autostore->val[msr_autostore_index] = autostore->val[last];
	}
}

static bool nested_cr3_valid(struct kvm_vcpu *vcpu, unsigned long val)
{
	unsigned long invalid_mask;

	invalid_mask = (~0ULL) << cpuid_maxphyaddr(vcpu);
	return (val & invalid_mask) == 0;
}

/*
 * Returns true if the MMU needs to be sync'd on nested VM-Enter/VM-Exit.
 * tl;dr: the MMU needs a sync if L0 is using shadow paging and L1 didn't
 * enable VPID for L2 (implying it expects a TLB flush on VMX transitions).
 * Here's why.
 *
 * If EPT is enabled by L0 a sync is never needed:
 * - if it is disabled by L1, then L0 is not shadowing L1 or L2 PTEs, there
 *   cannot be unsync'd SPTEs for either L1 or L2.
 *
 * - if it is also enabled by L1, then L0 doesn't need to sync on VM-Enter
 *   VM-Enter as VM-Enter isn't required to invalidate guest-physical mappings
 *   (irrespective of VPID), i.e. L1 can't rely on the (virtual) CPU to flush
 *   stale guest-physical mappings for L2 from the TLB.  And as above, L0 isn't
 *   shadowing L1 PTEs so there are no unsync'd SPTEs to sync on VM-Exit.
 *
 * If EPT is disabled by L0:
 * - if VPID is enabled by L1 (for L2), the situation is similar to when L1
 *   enables EPT: L0 doesn't need to sync as VM-Enter and VM-Exit aren't
 *   required to invalidate linear mappings (EPT is disabled so there are
 *   no combined or guest-physical mappings), i.e. L1 can't rely on the
 *   (virtual) CPU to flush stale linear mappings for either L2 or itself (L1).
 *
 * - however if VPID is disabled by L1, then a sync is needed as L1 expects all
 *   linear mappings (EPT is disabled so there are no combined or guest-physical
 *   mappings) to be invalidated on both VM-Enter and VM-Exit.
 *
 * Note, this logic is subtly different than nested_has_guest_tlb_tag(), which
 * additionally checks that L2 has been assigned a VPID (when EPT is disabled).
 * Whether or not L2 has been assigned a VPID by L0 is irrelevant with respect
 * to L1's expectations, e.g. L0 needs to invalidate hardware TLB entries if L2
 * doesn't have a unique VPID to prevent reusing L1's entries (assuming L1 has
 * been assigned a VPID), but L0 doesn't need to do a MMU sync because L1
 * doesn't expect stale (virtual) TLB entries to be flushed, i.e. L1 doesn't
 * know that L0 will flush the TLB and so L1 will do INVVPID as needed to flush
 * stale TLB entries, at which point L0 will sync L2's MMU.
 */
static bool nested_vmx_transition_mmu_sync(struct kvm_vcpu *vcpu)
{
	return !enable_ept && !nested_cpu_has_vpid(get_vmcs12(vcpu));
}

/*
 * Load guest's/host's cr3 at nested entry/exit.  @nested_ept is true if we are
 * emulating VM-Entry into a guest with EPT enabled.  On failure, the expected
 * Exit Qualification (for a VM-Entry consistency check VM-Exit) is assigned to
 * @entry_failure_code.
 */
static int nested_vmx_load_cr3(struct kvm_vcpu *vcpu, unsigned long cr3, bool nested_ept,
			       enum vm_entry_failure_code *entry_failure_code)
{
	if (CC(!nested_cr3_valid(vcpu, cr3))) {
		*entry_failure_code = ENTRY_FAIL_DEFAULT;
		return -EINVAL;
	}

	/*
	 * If PAE paging and EPT are both on, CR3 is not used by the CPU and
	 * must not be dereferenced.
	 */
	if (!nested_ept && is_pae_paging(vcpu) &&
	    (cr3 != kvm_read_cr3(vcpu) || pdptrs_changed(vcpu))) {
		if (CC(!load_pdptrs(vcpu, vcpu->arch.walk_mmu, cr3))) {
			*entry_failure_code = ENTRY_FAIL_PDPTE;
			return -EINVAL;
		}
	}

	/*
	 * Unconditionally skip the TLB flush on fast CR3 switch, all TLB
	 * flushes are handled by nested_vmx_transition_tlb_flush().  See
	 * nested_vmx_transition_mmu_sync for details on skipping the MMU sync.
	 */
	if (!nested_ept)
		kvm_mmu_new_pgd(vcpu, cr3, true,
				!nested_vmx_transition_mmu_sync(vcpu));

	vcpu->arch.cr3 = cr3;
	kvm_register_mark_available(vcpu, VCPU_EXREG_CR3);

	kvm_init_mmu(vcpu, false);

	return 0;
}

/*
 * Returns if KVM is able to config CPU to tag TLB entries
 * populated by L2 differently than TLB entries populated
 * by L1.
 *
 * If L0 uses EPT, L1 and L2 run with different EPTP because
 * guest_mode is part of kvm_mmu_page_role. Thus, TLB entries
 * are tagged with different EPTP.
 *
 * If L1 uses VPID and we allocated a vpid02, TLB entries are tagged
 * with different VPID (L1 entries are tagged with vmx->vpid
 * while L2 entries are tagged with vmx->nested.vpid02).
 */
static bool nested_has_guest_tlb_tag(struct kvm_vcpu *vcpu)
{
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);

	return enable_ept ||
	       (nested_cpu_has_vpid(vmcs12) && to_vmx(vcpu)->nested.vpid02);
}

static void nested_vmx_transition_tlb_flush(struct kvm_vcpu *vcpu,
					    struct vmcs12 *vmcs12,
					    bool is_vmenter)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	/*
	 * If VPID is disabled, linear and combined mappings are flushed on
	 * VM-Enter/VM-Exit, and guest-physical mappings are valid only for
	 * their associated EPTP.
	 */
	if (!enable_vpid)
		return;

	/*
	 * If vmcs12 doesn't use VPID, L1 expects linear and combined mappings
	 * for *all* contexts to be flushed on VM-Enter/VM-Exit.
	 *
	 * If VPID is enabled and used by vmc12, but L2 does not have a unique
	 * TLB tag (ASID), i.e. EPT is disabled and KVM was unable to allocate
	 * a VPID for L2, flush the current context as the effective ASID is
	 * common to both L1 and L2.
	 *
	 * Defer the flush so that it runs after vmcs02.EPTP has been set by
	 * KVM_REQ_LOAD_MMU_PGD (if nested EPT is enabled) and to avoid
	 * redundant flushes further down the nested pipeline.
	 *
	 * If a TLB flush isn't required due to any of the above, and vpid12 is
	 * changing then the new "virtual" VPID (vpid12) will reuse the same
	 * "real" VPID (vpid02), and so needs to be sync'd.  There is no direct
	 * mapping between vpid02 and vpid12, vpid02 is per-vCPU and reused for
	 * all nested vCPUs.
	 */
	if (!nested_cpu_has_vpid(vmcs12)) {
		kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
	} else if (!nested_has_guest_tlb_tag(vcpu)) {
		kvm_make_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu);
	} else if (is_vmenter &&
		   vmcs12->virtual_processor_id != vmx->nested.last_vpid) {
		vmx->nested.last_vpid = vmcs12->virtual_processor_id;
		vpid_sync_context(nested_get_vpid02(vcpu));
	}
}

static bool is_bitwise_subset(u64 superset, u64 subset, u64 mask)
{
	superset &= mask;
	subset &= mask;

	return (superset | subset) == superset;
}

static int vmx_restore_vmx_basic(struct vcpu_vmx *vmx, u64 data)
{
	const u64 feature_and_reserved =
		/* feature (except bit 48; see below) */
		BIT_ULL(49) | BIT_ULL(54) | BIT_ULL(55) |
		/* reserved */
		BIT_ULL(31) | GENMASK_ULL(47, 45) | GENMASK_ULL(63, 56);
	u64 vmx_basic = vmx->nested.msrs.basic;

	if (!is_bitwise_subset(vmx_basic, data, feature_and_reserved))
		return -EINVAL;

	/*
	 * KVM does not emulate a version of VMX that constrains physical
	 * addresses of VMX structures (e.g. VMCS) to 32-bits.
	 */
	if (data & BIT_ULL(48))
		return -EINVAL;

	if (vmx_basic_vmcs_revision_id(vmx_basic) !=
	    vmx_basic_vmcs_revision_id(data))
		return -EINVAL;

	if (vmx_basic_vmcs_size(vmx_basic) > vmx_basic_vmcs_size(data))
		return -EINVAL;

	vmx->nested.msrs.basic = data;
	return 0;
}

static int
vmx_restore_control_msr(struct vcpu_vmx *vmx, u32 msr_index, u64 data)
{
	u64 supported;
	u32 *lowp, *highp;

	switch (msr_index) {
	case MSR_IA32_VMX_TRUE_PINBASED_CTLS:
		lowp = &vmx->nested.msrs.pinbased_ctls_low;
		highp = &vmx->nested.msrs.pinbased_ctls_high;
		break;
	case MSR_IA32_VMX_TRUE_PROCBASED_CTLS:
		lowp = &vmx->nested.msrs.procbased_ctls_low;
		highp = &vmx->nested.msrs.procbased_ctls_high;
		break;
	case MSR_IA32_VMX_TRUE_EXIT_CTLS:
		lowp = &vmx->nested.msrs.exit_ctls_low;
		highp = &vmx->nested.msrs.exit_ctls_high;
		break;
	case MSR_IA32_VMX_TRUE_ENTRY_CTLS:
		lowp = &vmx->nested.msrs.entry_ctls_low;
		highp = &vmx->nested.msrs.entry_ctls_high;
		break;
	case MSR_IA32_VMX_PROCBASED_CTLS2:
		lowp = &vmx->nested.msrs.secondary_ctls_low;
		highp = &vmx->nested.msrs.secondary_ctls_high;
		break;
	default:
		BUG();
	}

	supported = vmx_control_msr(*lowp, *highp);

	/* Check must-be-1 bits are still 1. */
	if (!is_bitwise_subset(data, supported, GENMASK_ULL(31, 0)))
		return -EINVAL;

	/* Check must-be-0 bits are still 0. */
	if (!is_bitwise_subset(supported, data, GENMASK_ULL(63, 32)))
		return -EINVAL;

	*lowp = data;
	*highp = data >> 32;
	return 0;
}

static int vmx_restore_vmx_misc(struct vcpu_vmx *vmx, u64 data)
{
	const u64 feature_and_reserved_bits =
		/* feature */
		BIT_ULL(5) | GENMASK_ULL(8, 6) | BIT_ULL(14) | BIT_ULL(15) |
		BIT_ULL(28) | BIT_ULL(29) | BIT_ULL(30) |
		/* reserved */
		GENMASK_ULL(13, 9) | BIT_ULL(31);
	u64 vmx_misc;

	vmx_misc = vmx_control_msr(vmx->nested.msrs.misc_low,
				   vmx->nested.msrs.misc_high);

	if (!is_bitwise_subset(vmx_misc, data, feature_and_reserved_bits))
		return -EINVAL;

	if ((vmx->nested.msrs.pinbased_ctls_high &
	     PIN_BASED_VMX_PREEMPTION_TIMER) &&
	    vmx_misc_preemption_timer_rate(data) !=
	    vmx_misc_preemption_timer_rate(vmx_misc))
		return -EINVAL;

	if (vmx_misc_cr3_count(data) > vmx_misc_cr3_count(vmx_misc))
		return -EINVAL;

	if (vmx_misc_max_msr(data) > vmx_misc_max_msr(vmx_misc))
		return -EINVAL;

	if (vmx_misc_mseg_revid(data) != vmx_misc_mseg_revid(vmx_misc))
		return -EINVAL;

	vmx->nested.msrs.misc_low = data;
	vmx->nested.msrs.misc_high = data >> 32;

	return 0;
}

static int vmx_restore_vmx_ept_vpid_cap(struct vcpu_vmx *vmx, u64 data)
{
	u64 vmx_ept_vpid_cap;

	vmx_ept_vpid_cap = vmx_control_msr(vmx->nested.msrs.ept_caps,
					   vmx->nested.msrs.vpid_caps);

	/* Every bit is either reserved or a feature bit. */
	if (!is_bitwise_subset(vmx_ept_vpid_cap, data, -1ULL))
		return -EINVAL;

	vmx->nested.msrs.ept_caps = data;
	vmx->nested.msrs.vpid_caps = data >> 32;
	return 0;
}

static int vmx_restore_fixed0_msr(struct vcpu_vmx *vmx, u32 msr_index, u64 data)
{
	u64 *msr;

	switch (msr_index) {
	case MSR_IA32_VMX_CR0_FIXED0:
		msr = &vmx->nested.msrs.cr0_fixed0;
		break;
	case MSR_IA32_VMX_CR4_FIXED0:
		msr = &vmx->nested.msrs.cr4_fixed0;
		break;
	default:
		BUG();
	}

	/*
	 * 1 bits (which indicates bits which "must-be-1" during VMX operation)
	 * must be 1 in the restored value.
	 */
	if (!is_bitwise_subset(data, *msr, -1ULL))
		return -EINVAL;

	*msr = data;
	return 0;
}

/*
 * Called when userspace is restoring VMX MSRs.
 *
 * Returns 0 on success, non-0 otherwise.
 */
int vmx_set_vmx_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 data)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	/*
	 * Don't allow changes to the VMX capability MSRs while the vCPU
	 * is in VMX operation.
	 */
	if (vmx->nested.vmxon)
		return -EBUSY;

	switch (msr_index) {
	case MSR_IA32_VMX_BASIC:
		return vmx_restore_vmx_basic(vmx, data);
	case MSR_IA32_VMX_PINBASED_CTLS:
	case MSR_IA32_VMX_PROCBASED_CTLS:
	case MSR_IA32_VMX_EXIT_CTLS:
	case MSR_IA32_VMX_ENTRY_CTLS:
		/*
		 * The "non-true" VMX capability MSRs are generated from the
		 * "true" MSRs, so we do not support restoring them directly.
		 *
		 * If userspace wants to emulate VMX_BASIC[55]=0, userspace
		 * should restore the "true" MSRs with the must-be-1 bits
		 * set according to the SDM Vol 3. A.2 "RESERVED CONTROLS AND
		 * DEFAULT SETTINGS".
		 */
		return -EINVAL;
	case MSR_IA32_VMX_TRUE_PINBASED_CTLS:
	case MSR_IA32_VMX_TRUE_PROCBASED_CTLS:
	case MSR_IA32_VMX_TRUE_EXIT_CTLS:
	case MSR_IA32_VMX_TRUE_ENTRY_CTLS:
	case MSR_IA32_VMX_PROCBASED_CTLS2:
		return vmx_restore_control_msr(vmx, msr_index, data);
	case MSR_IA32_VMX_MISC:
		return vmx_restore_vmx_misc(vmx, data);
	case MSR_IA32_VMX_CR0_FIXED0:
	case MSR_IA32_VMX_CR4_FIXED0:
		return vmx_restore_fixed0_msr(vmx, msr_index, data);
	case MSR_IA32_VMX_CR0_FIXED1:
	case MSR_IA32_VMX_CR4_FIXED1:
		/*
		 * These MSRs are generated based on the vCPU's CPUID, so we
		 * do not support restoring them directly.
		 */
		return -EINVAL;
	case MSR_IA32_VMX_EPT_VPID_CAP:
		return vmx_restore_vmx_ept_vpid_cap(vmx, data);
	case MSR_IA32_VMX_VMCS_ENUM:
		vmx->nested.msrs.vmcs_enum = data;
		return 0;
	case MSR_IA32_VMX_VMFUNC:
		if (data & ~vmx->nested.msrs.vmfunc_controls)
			return -EINVAL;
		vmx->nested.msrs.vmfunc_controls = data;
		return 0;
	default:
		/*
		 * The rest of the VMX capability MSRs do not support restore.
		 */
		return -EINVAL;
	}
}

/* Returns 0 on success, non-0 otherwise. */
int vmx_get_vmx_msr(struct nested_vmx_msrs *msrs, u32 msr_index, u64 *pdata)
{
	switch (msr_index) {
	case MSR_IA32_VMX_BASIC:
		*pdata = msrs->basic;
		break;
	case MSR_IA32_VMX_TRUE_PINBASED_CTLS:
	case MSR_IA32_VMX_PINBASED_CTLS:
		*pdata = vmx_control_msr(
			msrs->pinbased_ctls_low,
			msrs->pinbased_ctls_high);
		if (msr_index == MSR_IA32_VMX_PINBASED_CTLS)
			*pdata |= PIN_BASED_ALWAYSON_WITHOUT_TRUE_MSR;
		break;
	case MSR_IA32_VMX_TRUE_PROCBASED_CTLS:
	case MSR_IA32_VMX_PROCBASED_CTLS:
		*pdata = vmx_control_msr(
			msrs->procbased_ctls_low,
			msrs->procbased_ctls_high);
		if (msr_index == MSR_IA32_VMX_PROCBASED_CTLS)
			*pdata |= CPU_BASED_ALWAYSON_WITHOUT_TRUE_MSR;
		break;
	case MSR_IA32_VMX_TRUE_EXIT_CTLS:
	case MSR_IA32_VMX_EXIT_CTLS:
		*pdata = vmx_control_msr(
			msrs->exit_ctls_low,
			msrs->exit_ctls_high);
		if (msr_index == MSR_IA32_VMX_EXIT_CTLS)
			*pdata |= VM_EXIT_ALWAYSON_WITHOUT_TRUE_MSR;
		break;
	case MSR_IA32_VMX_TRUE_ENTRY_CTLS:
	case MSR_IA32_VMX_ENTRY_CTLS:
		*pdata = vmx_control_msr(
			msrs->entry_ctls_low,
			msrs->entry_ctls_high);
		if (msr_index == MSR_IA32_VMX_ENTRY_CTLS)
			*pdata |= VM_ENTRY_ALWAYSON_WITHOUT_TRUE_MSR;
		break;
	case MSR_IA32_VMX_MISC:
		*pdata = vmx_control_msr(
			msrs->misc_low,
			msrs->misc_high);
		break;
	case MSR_IA32_VMX_CR0_FIXED0:
		*pdata = msrs->cr0_fixed0;
		break;
	case MSR_IA32_VMX_CR0_FIXED1:
		*pdata = msrs->cr0_fixed1;
		break;
	case MSR_IA32_VMX_CR4_FIXED0:
		*pdata = msrs->cr4_fixed0;
		break;
	case MSR_IA32_VMX_CR4_FIXED1:
		*pdata = msrs->cr4_fixed1;
		break;
	case MSR_IA32_VMX_VMCS_ENUM:
		*pdata = msrs->vmcs_enum;
		break;
	case MSR_IA32_VMX_PROCBASED_CTLS2:
		*pdata = vmx_control_msr(
			msrs->secondary_ctls_low,
			msrs->secondary_ctls_high);
		break;
	case MSR_IA32_VMX_EPT_VPID_CAP:
		*pdata = msrs->ept_caps |
			((u64)msrs->vpid_caps << 32);
		break;
	case MSR_IA32_VMX_VMFUNC:
		*pdata = msrs->vmfunc_controls;
		break;
	default:
		return 1;
	}

	return 0;
}

/*
 * Copy the writable VMCS shadow fields back to the VMCS12, in case they have
 * been modified by the L1 guest.  Note, "writable" in this context means
 * "writable by the guest", i.e. tagged SHADOW_FIELD_RW; the set of
 * fields tagged SHADOW_FIELD_RO may or may not align with the "read-only"
 * VM-exit information fields (which are actually writable if the vCPU is
 * configured to support "VMWRITE to any supported field in the VMCS").
 */
static void copy_shadow_to_vmcs12(struct vcpu_vmx *vmx)
{
	struct vmcs *shadow_vmcs = vmx->vmcs01.shadow_vmcs;
	struct vmcs12 *vmcs12 = get_vmcs12(&vmx->vcpu);
	struct shadow_vmcs_field field;
	unsigned long val;
	int i;

	if (WARN_ON(!shadow_vmcs))
		return;

	preempt_disable();

	vmcs_load(shadow_vmcs);

	for (i = 0; i < max_shadow_read_write_fields; i++) {
		field = shadow_read_write_fields[i];
		val = __vmcs_readl(field.encoding);
		vmcs12_write_any(vmcs12, field.encoding, field.offset, val);
	}

	vmcs_clear(shadow_vmcs);
	vmcs_load(vmx->loaded_vmcs->vmcs);

	preempt_enable();
}

static void copy_vmcs12_to_shadow(struct vcpu_vmx *vmx)
{
	const struct shadow_vmcs_field *fields[] = {
		shadow_read_write_fields,
		shadow_read_only_fields
	};
	const int max_fields[] = {
		max_shadow_read_write_fields,
		max_shadow_read_only_fields
	};
	struct vmcs *shadow_vmcs = vmx->vmcs01.shadow_vmcs;
	struct vmcs12 *vmcs12 = get_vmcs12(&vmx->vcpu);
	struct shadow_vmcs_field field;
	unsigned long val;
	int i, q;

	if (WARN_ON(!shadow_vmcs))
		return;

	vmcs_load(shadow_vmcs);

	for (q = 0; q < ARRAY_SIZE(fields); q++) {
		for (i = 0; i < max_fields[q]; i++) {
			field = fields[q][i];
			val = vmcs12_read_any(vmcs12, field.encoding,
					      field.offset);
			__vmcs_writel(field.encoding, val);
		}
	}

	vmcs_clear(shadow_vmcs);
	vmcs_load(vmx->loaded_vmcs->vmcs);
}

static int copy_enlightened_to_vmcs12(struct vcpu_vmx *vmx)
{
	struct vmcs12 *vmcs12 = vmx->nested.cached_vmcs12;
	struct hv_enlightened_vmcs *evmcs = vmx->nested.hv_evmcs;

	/* HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE */
	vmcs12->tpr_threshold = evmcs->tpr_threshold;
	vmcs12->guest_rip = evmcs->guest_rip;

	if (unlikely(!(evmcs->hv_clean_fields &
		       HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_BASIC))) {
		vmcs12->guest_rsp = evmcs->guest_rsp;
		vmcs12->guest_rflags = evmcs->guest_rflags;
		vmcs12->guest_interruptibility_info =
			evmcs->guest_interruptibility_info;
	}

	if (unlikely(!(evmcs->hv_clean_fields &
		       HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_PROC))) {
		vmcs12->cpu_based_vm_exec_control =
			evmcs->cpu_based_vm_exec_control;
	}

	if (unlikely(!(evmcs->hv_clean_fields &
		       HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_EXCPN))) {
		vmcs12->exception_bitmap = evmcs->exception_bitmap;
	}

	if (unlikely(!(evmcs->hv_clean_fields &
		       HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_ENTRY))) {
		vmcs12->vm_entry_controls = evmcs->vm_entry_controls;
	}

	if (unlikely(!(evmcs->hv_clean_fields &
		       HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_EVENT))) {
		vmcs12->vm_entry_intr_info_field =
			evmcs->vm_entry_intr_info_field;
		vmcs12->vm_entry_exception_error_code =
			evmcs->vm_entry_exception_error_code;
		vmcs12->vm_entry_instruction_len =
			evmcs->vm_entry_instruction_len;
	}

	if (unlikely(!(evmcs->hv_clean_fields &
		       HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1))) {
		vmcs12->host_ia32_pat = evmcs->host_ia32_pat;
		vmcs12->host_ia32_efer = evmcs->host_ia32_efer;
		vmcs12->host_cr0 = evmcs->host_cr0;
		vmcs12->host_cr3 = evmcs->host_cr3;
		vmcs12->host_cr4 = evmcs->host_cr4;
		vmcs12->host_ia32_sysenter_esp = evmcs->host_ia32_sysenter_esp;
		vmcs12->host_ia32_sysenter_eip = evmcs->host_ia32_sysenter_eip;
		vmcs12->host_rip = evmcs->host_rip;
		vmcs12->host_ia32_sysenter_cs = evmcs->host_ia32_sysenter_cs;
		vmcs12->host_es_selector = evmcs->host_es_selector;
		vmcs12->host_cs_selector = evmcs->host_cs_selector;
		vmcs12->host_ss_selector = evmcs->host_ss_selector;
		vmcs12->host_ds_selector = evmcs->host_ds_selector;
		vmcs12->host_fs_selector = evmcs->host_fs_selector;
		vmcs12->host_gs_selector = evmcs->host_gs_selector;
		vmcs12->host_tr_selector = evmcs->host_tr_selector;
	}

	if (unlikely(!(evmcs->hv_clean_fields &
		       HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_GRP1))) {
		vmcs12->pin_based_vm_exec_control =
			evmcs->pin_based_vm_exec_control;
		vmcs12->vm_exit_controls = evmcs->vm_exit_controls;
		vmcs12->secondary_vm_exec_control =
			evmcs->secondary_vm_exec_control;
	}

	if (unlikely(!(evmcs->hv_clean_fields &
		       HV_VMX_ENLIGHTENED_CLEAN_FIELD_IO_BITMAP))) {
		vmcs12->io_bitmap_a = evmcs->io_bitmap_a;
		vmcs12->io_bitmap_b = evmcs->io_bitmap_b;
	}

	if (unlikely(!(evmcs->hv_clean_fields &
		       HV_VMX_ENLIGHTENED_CLEAN_FIELD_MSR_BITMAP))) {
		vmcs12->msr_bitmap = evmcs->msr_bitmap;
	}

	if (unlikely(!(evmcs->hv_clean_fields &
		       HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2))) {
		vmcs12->guest_es_base = evmcs->guest_es_base;
		vmcs12->guest_cs_base = evmcs->guest_cs_base;
		vmcs12->guest_ss_base = evmcs->guest_ss_base;
		vmcs12->guest_ds_base = evmcs->guest_ds_base;
		vmcs12->guest_fs_base = evmcs->guest_fs_base;
		vmcs12->guest_gs_base = evmcs->guest_gs_base;
		vmcs12->guest_ldtr_base = evmcs->guest_ldtr_base;
		vmcs12->guest_tr_base = evmcs->guest_tr_base;
		vmcs12->guest_gdtr_base = evmcs->guest_gdtr_base;
		vmcs12->guest_idtr_base = evmcs->guest_idtr_base;
		vmcs12->guest_es_limit = evmcs->guest_es_limit;
		vmcs12->guest_cs_limit = evmcs->guest_cs_limit;
		vmcs12->guest_ss_limit = evmcs->guest_ss_limit;
		vmcs12->guest_ds_limit = evmcs->guest_ds_limit;
		vmcs12->guest_fs_limit = evmcs->guest_fs_limit;
		vmcs12->guest_gs_limit = evmcs->guest_gs_limit;
		vmcs12->guest_ldtr_limit = evmcs->guest_ldtr_limit;
		vmcs12->guest_tr_limit = evmcs->guest_tr_limit;
		vmcs12->guest_gdtr_limit = evmcs->guest_gdtr_limit;
		vmcs12->guest_idtr_limit = evmcs->guest_idtr_limit;
		vmcs12->guest_es_ar_bytes = evmcs->guest_es_ar_bytes;
		vmcs12->guest_cs_ar_bytes = evmcs->guest_cs_ar_bytes;
		vmcs12->guest_ss_ar_bytes = evmcs->guest_ss_ar_bytes;
		vmcs12->guest_ds_ar_bytes = evmcs->guest_ds_ar_bytes;
		vmcs12->guest_fs_ar_bytes = evmcs->guest_fs_ar_bytes;
		vmcs12->guest_gs_ar_bytes = evmcs->guest_gs_ar_bytes;
		vmcs12->guest_ldtr_ar_bytes = evmcs->guest_ldtr_ar_bytes;
		vmcs12->guest_tr_ar_bytes = evmcs->guest_tr_ar_bytes;
		vmcs12->guest_es_selector = evmcs->guest_es_selector;
		vmcs12->guest_cs_selector = evmcs->guest_cs_selector;
		vmcs12->guest_ss_selector = evmcs->guest_ss_selector;
		vmcs12->guest_ds_selector = evmcs->guest_ds_selector;
		vmcs12->guest_fs_selector = evmcs->guest_fs_selector;
		vmcs12->guest_gs_selector = evmcs->guest_gs_selector;
		vmcs12->guest_ldtr_selector = evmcs->guest_ldtr_selector;
		vmcs12->guest_tr_selector = evmcs->guest_tr_selector;
	}

	if (unlikely(!(evmcs->hv_clean_fields &
		       HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_GRP2))) {
		vmcs12->tsc_offset = evmcs->tsc_offset;
		vmcs12->virtual_apic_page_addr = evmcs->virtual_apic_page_addr;
		vmcs12->xss_exit_bitmap = evmcs->xss_exit_bitmap;
	}

	if (unlikely(!(evmcs->hv_clean_fields &
		       HV_VMX_ENLIGHTENED_CLEAN_FIELD_CRDR))) {
		vmcs12->cr0_guest_host_mask = evmcs->cr0_guest_host_mask;
		vmcs12->cr4_guest_host_mask = evmcs->cr4_guest_host_mask;
		vmcs12->cr0_read_shadow = evmcs->cr0_read_shadow;
		vmcs12->cr4_read_shadow = evmcs->cr4_read_shadow;
		vmcs12->guest_cr0 = evmcs->guest_cr0;
		vmcs12->guest_cr3 = evmcs->guest_cr3;
		vmcs12->guest_cr4 = evmcs->guest_cr4;
		vmcs12->guest_dr7 = evmcs->guest_dr7;
	}

	if (unlikely(!(evmcs->hv_clean_fields &
		       HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_POINTER))) {
		vmcs12->host_fs_base = evmcs->host_fs_base;
		vmcs12->host_gs_base = evmcs->host_gs_base;
		vmcs12->host_tr_base = evmcs->host_tr_base;
		vmcs12->host_gdtr_base = evmcs->host_gdtr_base;
		vmcs12->host_idtr_base = evmcs->host_idtr_base;
		vmcs12->host_rsp = evmcs->host_rsp;
	}

	if (unlikely(!(evmcs->hv_clean_fields &
		       HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_XLAT))) {
		vmcs12->ept_pointer = evmcs->ept_pointer;
		vmcs12->virtual_processor_id = evmcs->virtual_processor_id;
	}

	if (unlikely(!(evmcs->hv_clean_fields &
		       HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1))) {
		vmcs12->vmcs_link_pointer = evmcs->vmcs_link_pointer;
		vmcs12->guest_ia32_debugctl = evmcs->guest_ia32_debugctl;
		vmcs12->guest_ia32_pat = evmcs->guest_ia32_pat;
		vmcs12->guest_ia32_efer = evmcs->guest_ia32_efer;
		vmcs12->guest_pdptr0 = evmcs->guest_pdptr0;
		vmcs12->guest_pdptr1 = evmcs->guest_pdptr1;
		vmcs12->guest_pdptr2 = evmcs->guest_pdptr2;
		vmcs12->guest_pdptr3 = evmcs->guest_pdptr3;
		vmcs12->guest_pending_dbg_exceptions =
			evmcs->guest_pending_dbg_exceptions;
		vmcs12->guest_sysenter_esp = evmcs->guest_sysenter_esp;
		vmcs12->guest_sysenter_eip = evmcs->guest_sysenter_eip;
		vmcs12->guest_bndcfgs = evmcs->guest_bndcfgs;
		vmcs12->guest_activity_state = evmcs->guest_activity_state;
		vmcs12->guest_sysenter_cs = evmcs->guest_sysenter_cs;
	}

	/*
	 * Not used?
	 * vmcs12->vm_exit_msr_store_addr = evmcs->vm_exit_msr_store_addr;
	 * vmcs12->vm_exit_msr_load_addr = evmcs->vm_exit_msr_load_addr;
	 * vmcs12->vm_entry_msr_load_addr = evmcs->vm_entry_msr_load_addr;
	 * vmcs12->page_fault_error_code_mask =
	 *		evmcs->page_fault_error_code_mask;
	 * vmcs12->page_fault_error_code_match =
	 *		evmcs->page_fault_error_code_match;
	 * vmcs12->cr3_target_count = evmcs->cr3_target_count;
	 * vmcs12->vm_exit_msr_store_count = evmcs->vm_exit_msr_store_count;
	 * vmcs12->vm_exit_msr_load_count = evmcs->vm_exit_msr_load_count;
	 * vmcs12->vm_entry_msr_load_count = evmcs->vm_entry_msr_load_count;
	 */

	/*
	 * Read only fields:
	 * vmcs12->guest_physical_address = evmcs->guest_physical_address;
	 * vmcs12->vm_instruction_error = evmcs->vm_instruction_error;
	 * vmcs12->vm_exit_reason = evmcs->vm_exit_reason;
	 * vmcs12->vm_exit_intr_info = evmcs->vm_exit_intr_info;
	 * vmcs12->vm_exit_intr_error_code = evmcs->vm_exit_intr_error_code;
	 * vmcs12->idt_vectoring_info_field = evmcs->idt_vectoring_info_field;
	 * vmcs12->idt_vectoring_error_code = evmcs->idt_vectoring_error_code;
	 * vmcs12->vm_exit_instruction_len = evmcs->vm_exit_instruction_len;
	 * vmcs12->vmx_instruction_info = evmcs->vmx_instruction_info;
	 * vmcs12->exit_qualification = evmcs->exit_qualification;
	 * vmcs12->guest_linear_address = evmcs->guest_linear_address;
	 *
	 * Not present in struct vmcs12:
	 * vmcs12->exit_io_instruction_ecx = evmcs->exit_io_instruction_ecx;
	 * vmcs12->exit_io_instruction_esi = evmcs->exit_io_instruction_esi;
	 * vmcs12->exit_io_instruction_edi = evmcs->exit_io_instruction_edi;
	 * vmcs12->exit_io_instruction_eip = evmcs->exit_io_instruction_eip;
	 */

	return 0;
}

static int copy_vmcs12_to_enlightened(struct vcpu_vmx *vmx)
{
	struct vmcs12 *vmcs12 = vmx->nested.cached_vmcs12;
	struct hv_enlightened_vmcs *evmcs = vmx->nested.hv_evmcs;

	/*
	 * Should not be changed by KVM:
	 *
	 * evmcs->host_es_selector = vmcs12->host_es_selector;
	 * evmcs->host_cs_selector = vmcs12->host_cs_selector;
	 * evmcs->host_ss_selector = vmcs12->host_ss_selector;
	 * evmcs->host_ds_selector = vmcs12->host_ds_selector;
	 * evmcs->host_fs_selector = vmcs12->host_fs_selector;
	 * evmcs->host_gs_selector = vmcs12->host_gs_selector;
	 * evmcs->host_tr_selector = vmcs12->host_tr_selector;
	 * evmcs->host_ia32_pat = vmcs12->host_ia32_pat;
	 * evmcs->host_ia32_efer = vmcs12->host_ia32_efer;
	 * evmcs->host_cr0 = vmcs12->host_cr0;
	 * evmcs->host_cr3 = vmcs12->host_cr3;
	 * evmcs->host_cr4 = vmcs12->host_cr4;
	 * evmcs->host_ia32_sysenter_esp = vmcs12->host_ia32_sysenter_esp;
	 * evmcs->host_ia32_sysenter_eip = vmcs12->host_ia32_sysenter_eip;
	 * evmcs->host_rip = vmcs12->host_rip;
	 * evmcs->host_ia32_sysenter_cs = vmcs12->host_ia32_sysenter_cs;
	 * evmcs->host_fs_base = vmcs12->host_fs_base;
	 * evmcs->host_gs_base = vmcs12->host_gs_base;
	 * evmcs->host_tr_base = vmcs12->host_tr_base;
	 * evmcs->host_gdtr_base = vmcs12->host_gdtr_base;
	 * evmcs->host_idtr_base = vmcs12->host_idtr_base;
	 * evmcs->host_rsp = vmcs12->host_rsp;
	 * sync_vmcs02_to_vmcs12() doesn't read these:
	 * evmcs->io_bitmap_a = vmcs12->io_bitmap_a;
	 * evmcs->io_bitmap_b = vmcs12->io_bitmap_b;
	 * evmcs->msr_bitmap = vmcs12->msr_bitmap;
	 * evmcs->ept_pointer = vmcs12->ept_pointer;
	 * evmcs->xss_exit_bitmap = vmcs12->xss_exit_bitmap;
	 * evmcs->vm_exit_msr_store_addr = vmcs12->vm_exit_msr_store_addr;
	 * evmcs->vm_exit_msr_load_addr = vmcs12->vm_exit_msr_load_addr;
	 * evmcs->vm_entry_msr_load_addr = vmcs12->vm_entry_msr_load_addr;
	 * evmcs->tpr_threshold = vmcs12->tpr_threshold;
	 * evmcs->virtual_processor_id = vmcs12->virtual_processor_id;
	 * evmcs->exception_bitmap = vmcs12->exception_bitmap;
	 * evmcs->vmcs_link_pointer = vmcs12->vmcs_link_pointer;
	 * evmcs->pin_based_vm_exec_control = vmcs12->pin_based_vm_exec_control;
	 * evmcs->vm_exit_controls = vmcs12->vm_exit_controls;
	 * evmcs->secondary_vm_exec_control = vmcs12->secondary_vm_exec_control;
	 * evmcs->page_fault_error_code_mask =
	 *		vmcs12->page_fault_error_code_mask;
	 * evmcs->page_fault_error_code_match =
	 *		vmcs12->page_fault_error_code_match;
	 * evmcs->cr3_target_count = vmcs12->cr3_target_count;
	 * evmcs->virtual_apic_page_addr = vmcs12->virtual_apic_page_addr;
	 * evmcs->tsc_offset = vmcs12->tsc_offset;
	 * evmcs->guest_ia32_debugctl = vmcs12->guest_ia32_debugctl;
	 * evmcs->cr0_guest_host_mask = vmcs12->cr0_guest_host_mask;
	 * evmcs->cr4_guest_host_mask = vmcs12->cr4_guest_host_mask;
	 * evmcs->cr0_read_shadow = vmcs12->cr0_read_shadow;
	 * evmcs->cr4_read_shadow = vmcs12->cr4_read_shadow;
	 * evmcs->vm_exit_msr_store_count = vmcs12->vm_exit_msr_store_count;
	 * evmcs->vm_exit_msr_load_count = vmcs12->vm_exit_msr_load_count;
	 * evmcs->vm_entry_msr_load_count = vmcs12->vm_entry_msr_load_count;
	 *
	 * Not present in struct vmcs12:
	 * evmcs->exit_io_instruction_ecx = vmcs12->exit_io_instruction_ecx;
	 * evmcs->exit_io_instruction_esi = vmcs12->exit_io_instruction_esi;
	 * evmcs->exit_io_instruction_edi = vmcs12->exit_io_instruction_edi;
	 * evmcs->exit_io_instruction_eip = vmcs12->exit_io_instruction_eip;
	 */

	evmcs->guest_es_selector = vmcs12->guest_es_selector;
	evmcs->guest_cs_selector = vmcs12->guest_cs_selector;
	evmcs->guest_ss_selector = vmcs12->guest_ss_selector;
	evmcs->guest_ds_selector = vmcs12->guest_ds_selector;
	evmcs->guest_fs_selector = vmcs12->guest_fs_selector;
	evmcs->guest_gs_selector = vmcs12->guest_gs_selector;
	evmcs->guest_ldtr_selector = vmcs12->guest_ldtr_selector;
	evmcs->guest_tr_selector = vmcs12->guest_tr_selector;

	evmcs->guest_es_limit = vmcs12->guest_es_limit;
	evmcs->guest_cs_limit = vmcs12->guest_cs_limit;
	evmcs->guest_ss_limit = vmcs12->guest_ss_limit;
	evmcs->guest_ds_limit = vmcs12->guest_ds_limit;
	evmcs->guest_fs_limit = vmcs12->guest_fs_limit;
	evmcs->guest_gs_limit = vmcs12->guest_gs_limit;
	evmcs->guest_ldtr_limit = vmcs12->guest_ldtr_limit;
	evmcs->guest_tr_limit = vmcs12->guest_tr_limit;
	evmcs->guest_gdtr_limit = vmcs12->guest_gdtr_limit;
	evmcs->guest_idtr_limit = vmcs12->guest_idtr_limit;

	evmcs->guest_es_ar_bytes = vmcs12->guest_es_ar_bytes;
	evmcs->guest_cs_ar_bytes = vmcs12->guest_cs_ar_bytes;
	evmcs->guest_ss_ar_bytes = vmcs12->guest_ss_ar_bytes;
	evmcs->guest_ds_ar_bytes = vmcs12->guest_ds_ar_bytes;
	evmcs->guest_fs_ar_bytes = vmcs12->guest_fs_ar_bytes;
	evmcs->guest_gs_ar_bytes = vmcs12->guest_gs_ar_bytes;
	evmcs->guest_ldtr_ar_bytes = vmcs12->guest_ldtr_ar_bytes;
	evmcs->guest_tr_ar_bytes = vmcs12->guest_tr_ar_bytes;

	evmcs->guest_es_base = vmcs12->guest_es_base;
	evmcs->guest_cs_base = vmcs12->guest_cs_base;
	evmcs->guest_ss_base = vmcs12->guest_ss_base;
	evmcs->guest_ds_base = vmcs12->guest_ds_base;
	evmcs->guest_fs_base = vmcs12->guest_fs_base;
	evmcs->guest_gs_base = vmcs12->guest_gs_base;
	evmcs->guest_ldtr_base = vmcs12->guest_ldtr_base;
	evmcs->guest_tr_base = vmcs12->guest_tr_base;
	evmcs->guest_gdtr_base = vmcs12->guest_gdtr_base;
	evmcs->guest_idtr_base = vmcs12->guest_idtr_base;

	evmcs->guest_ia32_pat = vmcs12->guest_ia32_pat;
	evmcs->guest_ia32_efer = vmcs12->guest_ia32_efer;

	evmcs->guest_pdptr0 = vmcs12->guest_pdptr0;
	evmcs->guest_pdptr1 = vmcs12->guest_pdptr1;
	evmcs->guest_pdptr2 = vmcs12->guest_pdptr2;
	evmcs->guest_pdptr3 = vmcs12->guest_pdptr3;

	evmcs->guest_pending_dbg_exceptions =
		vmcs12->guest_pending_dbg_exceptions;
	evmcs->guest_sysenter_esp = vmcs12->guest_sysenter_esp;
	evmcs->guest_sysenter_eip = vmcs12->guest_sysenter_eip;

	evmcs->guest_activity_state = vmcs12->guest_activity_state;
	evmcs->guest_sysenter_cs = vmcs12->guest_sysenter_cs;

	evmcs->guest_cr0 = vmcs12->guest_cr0;
	evmcs->guest_cr3 = vmcs12->guest_cr3;
	evmcs->guest_cr4 = vmcs12->guest_cr4;
	evmcs->guest_dr7 = vmcs12->guest_dr7;

	evmcs->guest_physical_address = vmcs12->guest_physical_address;

	evmcs->vm_instruction_error = vmcs12->vm_instruction_error;
	evmcs->vm_exit_reason = vmcs12->vm_exit_reason;
	evmcs->vm_exit_intr_info = vmcs12->vm_exit_intr_info;
	evmcs->vm_exit_intr_error_code = vmcs12->vm_exit_intr_error_code;
	evmcs->idt_vectoring_info_field = vmcs12->idt_vectoring_info_field;
	evmcs->idt_vectoring_error_code = vmcs12->idt_vectoring_error_code;
	evmcs->vm_exit_instruction_len = vmcs12->vm_exit_instruction_len;
	evmcs->vmx_instruction_info = vmcs12->vmx_instruction_info;

	evmcs->exit_qualification = vmcs12->exit_qualification;

	evmcs->guest_linear_address = vmcs12->guest_linear_address;
	evmcs->guest_rsp = vmcs12->guest_rsp;
	evmcs->guest_rflags = vmcs12->guest_rflags;

	evmcs->guest_interruptibility_info =
		vmcs12->guest_interruptibility_info;
	evmcs->cpu_based_vm_exec_control = vmcs12->cpu_based_vm_exec_control;
	evmcs->vm_entry_controls = vmcs12->vm_entry_controls;
	evmcs->vm_entry_intr_info_field = vmcs12->vm_entry_intr_info_field;
	evmcs->vm_entry_exception_error_code =
		vmcs12->vm_entry_exception_error_code;
	evmcs->vm_entry_instruction_len = vmcs12->vm_entry_instruction_len;

	evmcs->guest_rip = vmcs12->guest_rip;

	evmcs->guest_bndcfgs = vmcs12->guest_bndcfgs;

	return 0;
}

/*
 * This is an equivalent of the nested hypervisor executing the vmptrld
 * instruction.
 */
static enum nested_evmptrld_status nested_vmx_handle_enlightened_vmptrld(
	struct kvm_vcpu *vcpu, bool from_launch)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	bool evmcs_gpa_changed = false;
	u64 evmcs_gpa;

	if (likely(!vmx->nested.enlightened_vmcs_enabled))
		return EVMPTRLD_DISABLED;

	if (!nested_enlightened_vmentry(vcpu, &evmcs_gpa))
		return EVMPTRLD_DISABLED;

	if (unlikely(!vmx->nested.hv_evmcs ||
		     evmcs_gpa != vmx->nested.hv_evmcs_vmptr)) {
		if (!vmx->nested.hv_evmcs)
			vmx->nested.current_vmptr = -1ull;

		nested_release_evmcs(vcpu);

		if (kvm_vcpu_map(vcpu, gpa_to_gfn(evmcs_gpa),
				 &vmx->nested.hv_evmcs_map))
			return EVMPTRLD_ERROR;

		vmx->nested.hv_evmcs = vmx->nested.hv_evmcs_map.hva;

		/*
		 * Currently, KVM only supports eVMCS version 1
		 * (== KVM_EVMCS_VERSION) and thus we expect guest to set this
		 * value to first u32 field of eVMCS which should specify eVMCS
		 * VersionNumber.
		 *
		 * Guest should be aware of supported eVMCS versions by host by
		 * examining CPUID.0x4000000A.EAX[0:15]. Host userspace VMM is
		 * expected to set this CPUID leaf according to the value
		 * returned in vmcs_version from nested_enable_evmcs().
		 *
		 * However, it turns out that Microsoft Hyper-V fails to comply
		 * to their own invented interface: When Hyper-V use eVMCS, it
		 * just sets first u32 field of eVMCS to revision_id specified
		 * in MSR_IA32_VMX_BASIC. Instead of used eVMCS version number
		 * which is one of the supported versions specified in
		 * CPUID.0x4000000A.EAX[0:15].
		 *
		 * To overcome Hyper-V bug, we accept here either a supported
		 * eVMCS version or VMCS12 revision_id as valid values for first
		 * u32 field of eVMCS.
		 */
		if ((vmx->nested.hv_evmcs->revision_id != KVM_EVMCS_VERSION) &&
		    (vmx->nested.hv_evmcs->revision_id != VMCS12_REVISION)) {
			nested_release_evmcs(vcpu);
			return EVMPTRLD_VMFAIL;
		}

		vmx->nested.dirty_vmcs12 = true;
		vmx->nested.hv_evmcs_vmptr = evmcs_gpa;

		evmcs_gpa_changed = true;
		/*
		 * Unlike normal vmcs12, enlightened vmcs12 is not fully
		 * reloaded from guest's memory (read only fields, fields not
		 * present in struct hv_enlightened_vmcs, ...). Make sure there
		 * are no leftovers.
		 */
		if (from_launch) {
			struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
			memset(vmcs12, 0, sizeof(*vmcs12));
			vmcs12->hdr.revision_id = VMCS12_REVISION;
		}

	}

	/*
	 * Clean fields data can't be used on VMLAUNCH and when we switch
	 * between different L2 guests as KVM keeps a single VMCS12 per L1.
	 */
	if (from_launch || evmcs_gpa_changed)
		vmx->nested.hv_evmcs->hv_clean_fields &=
			~HV_VMX_ENLIGHTENED_CLEAN_FIELD_ALL;

	return EVMPTRLD_SUCCEEDED;
}

void nested_sync_vmcs12_to_shadow(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (vmx->nested.hv_evmcs) {
		copy_vmcs12_to_enlightened(vmx);
		/* All fields are clean */
		vmx->nested.hv_evmcs->hv_clean_fields |=
			HV_VMX_ENLIGHTENED_CLEAN_FIELD_ALL;
	} else {
		copy_vmcs12_to_shadow(vmx);
	}

	vmx->nested.need_vmcs12_to_shadow_sync = false;
}

static enum hrtimer_restart vmx_preemption_timer_fn(struct hrtimer *timer)
{
	struct vcpu_vmx *vmx =
		container_of(timer, struct vcpu_vmx, nested.preemption_timer);

	vmx->nested.preemption_timer_expired = true;
	kvm_make_request(KVM_REQ_EVENT, &vmx->vcpu);
	kvm_vcpu_kick(&vmx->vcpu);

	return HRTIMER_NORESTART;
}

static u64 vmx_calc_preemption_timer_value(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);

	u64 l1_scaled_tsc = kvm_read_l1_tsc(vcpu, rdtsc()) >>
			    VMX_MISC_EMULATED_PREEMPTION_TIMER_RATE;

	if (!vmx->nested.has_preemption_timer_deadline) {
		vmx->nested.preemption_timer_deadline =
			vmcs12->vmx_preemption_timer_value + l1_scaled_tsc;
		vmx->nested.has_preemption_timer_deadline = true;
	}
	return vmx->nested.preemption_timer_deadline - l1_scaled_tsc;
}

static void vmx_start_preemption_timer(struct kvm_vcpu *vcpu,
					u64 preemption_timeout)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	/*
	 * A timer value of zero is architecturally guaranteed to cause
	 * a VMExit prior to executing any instructions in the guest.
	 */
	if (preemption_timeout == 0) {
		vmx_preemption_timer_fn(&vmx->nested.preemption_timer);
		return;
	}

	if (vcpu->arch.virtual_tsc_khz == 0)
		return;

	preemption_timeout <<= VMX_MISC_EMULATED_PREEMPTION_TIMER_RATE;
	preemption_timeout *= 1000000;
	do_div(preemption_timeout, vcpu->arch.virtual_tsc_khz);
	hrtimer_start(&vmx->nested.preemption_timer,
		      ktime_add_ns(ktime_get(), preemption_timeout),
		      HRTIMER_MODE_ABS_PINNED);
}

static u64 nested_vmx_calc_efer(struct vcpu_vmx *vmx, struct vmcs12 *vmcs12)
{
	if (vmx->nested.nested_run_pending &&
	    (vmcs12->vm_entry_controls & VM_ENTRY_LOAD_IA32_EFER))
		return vmcs12->guest_ia32_efer;
	else if (vmcs12->vm_entry_controls & VM_ENTRY_IA32E_MODE)
		return vmx->vcpu.arch.efer | (EFER_LMA | EFER_LME);
	else
		return vmx->vcpu.arch.efer & ~(EFER_LMA | EFER_LME);
}

static void prepare_vmcs02_constant_state(struct vcpu_vmx *vmx)
{
	/*
	 * If vmcs02 hasn't been initialized, set the constant vmcs02 state
	 * according to L0's settings (vmcs12 is irrelevant here).  Host
	 * fields that come from L0 and are not constant, e.g. HOST_CR3,
	 * will be set as needed prior to VMLAUNCH/VMRESUME.
	 */
	if (vmx->nested.vmcs02_initialized)
		return;
	vmx->nested.vmcs02_initialized = true;

	/*
	 * We don't care what the EPTP value is we just need to guarantee
	 * it's valid so we don't get a false positive when doing early
	 * consistency checks.
	 */
	if (enable_ept && nested_early_check)
		vmcs_write64(EPT_POINTER,
			     construct_eptp(&vmx->vcpu, 0, PT64_ROOT_4LEVEL));

	/* All VMFUNCs are currently emulated through L0 vmexits.  */
	if (cpu_has_vmx_vmfunc())
		vmcs_write64(VM_FUNCTION_CONTROL, 0);

	if (cpu_has_vmx_posted_intr())
		vmcs_write16(POSTED_INTR_NV, POSTED_INTR_NESTED_VECTOR);

	if (cpu_has_vmx_msr_bitmap())
		vmcs_write64(MSR_BITMAP, __pa(vmx->nested.vmcs02.msr_bitmap));

	/*
	 * The PML address never changes, so it is constant in vmcs02.
	 * Conceptually we want to copy the PML index from vmcs01 here,
	 * and then back to vmcs01 on nested vmexit.  But since we flush
	 * the log and reset GUEST_PML_INDEX on each vmexit, the PML
	 * index is also effectively constant in vmcs02.
	 */
	if (enable_pml) {
		vmcs_write64(PML_ADDRESS, page_to_phys(vmx->pml_pg));
		vmcs_write16(GUEST_PML_INDEX, PML_ENTITY_NUM - 1);
	}

	if (cpu_has_vmx_encls_vmexit())
		vmcs_write64(ENCLS_EXITING_BITMAP, -1ull);

	/*
	 * Set the MSR load/store lists to match L0's settings.  Only the
	 * addresses are constant (for vmcs02), the counts can change based
	 * on L2's behavior, e.g. switching to/from long mode.
	 */
	vmcs_write64(VM_EXIT_MSR_STORE_ADDR, __pa(vmx->msr_autostore.guest.val));
	vmcs_write64(VM_EXIT_MSR_LOAD_ADDR, __pa(vmx->msr_autoload.host.val));
	vmcs_write64(VM_ENTRY_MSR_LOAD_ADDR, __pa(vmx->msr_autoload.guest.val));

	vmx_set_constant_host_state(vmx);
}

static void prepare_vmcs02_early_rare(struct vcpu_vmx *vmx,
				      struct vmcs12 *vmcs12)
{
	prepare_vmcs02_constant_state(vmx);

	vmcs_write64(VMCS_LINK_POINTER, -1ull);

	if (enable_vpid) {
		if (nested_cpu_has_vpid(vmcs12) && vmx->nested.vpid02)
			vmcs_write16(VIRTUAL_PROCESSOR_ID, vmx->nested.vpid02);
		else
			vmcs_write16(VIRTUAL_PROCESSOR_ID, vmx->vpid);
	}
}

static void prepare_vmcs02_early(struct vcpu_vmx *vmx, struct vmcs12 *vmcs12)
{
	u32 exec_control, vmcs12_exec_ctrl;
	u64 guest_efer = nested_vmx_calc_efer(vmx, vmcs12);

	if (vmx->nested.dirty_vmcs12 || vmx->nested.hv_evmcs)
		prepare_vmcs02_early_rare(vmx, vmcs12);

	/*
	 * PIN CONTROLS
	 */
	exec_control = vmx_pin_based_exec_ctrl(vmx);
	exec_control |= (vmcs12->pin_based_vm_exec_control &
			 ~PIN_BASED_VMX_PREEMPTION_TIMER);

	/* Posted interrupts setting is only taken from vmcs12.  */
	if (nested_cpu_has_posted_intr(vmcs12)) {
		vmx->nested.posted_intr_nv = vmcs12->posted_intr_nv;
		vmx->nested.pi_pending = false;
	} else {
		exec_control &= ~PIN_BASED_POSTED_INTR;
	}
	pin_controls_set(vmx, exec_control);

	/*
	 * EXEC CONTROLS
	 */
	exec_control = vmx_exec_control(vmx); /* L0's desires */
	exec_control &= ~CPU_BASED_INTR_WINDOW_EXITING;
	exec_control &= ~CPU_BASED_NMI_WINDOW_EXITING;
	exec_control &= ~CPU_BASED_TPR_SHADOW;
	exec_control |= vmcs12->cpu_based_vm_exec_control;

	vmx->nested.l1_tpr_threshold = -1;
	if (exec_control & CPU_BASED_TPR_SHADOW)
		vmcs_write32(TPR_THRESHOLD, vmcs12->tpr_threshold);
#ifdef CONFIG_X86_64
	else
		exec_control |= CPU_BASED_CR8_LOAD_EXITING |
				CPU_BASED_CR8_STORE_EXITING;
#endif

	/*
	 * A vmexit (to either L1 hypervisor or L0 userspace) is always needed
	 * for I/O port accesses.
	 */
	exec_control |= CPU_BASED_UNCOND_IO_EXITING;
	exec_control &= ~CPU_BASED_USE_IO_BITMAPS;

	/*
	 * This bit will be computed in nested_get_vmcs12_pages, because
	 * we do not have access to L1's MSR bitmap yet.  For now, keep
	 * the same bit as before, hoping to avoid multiple VMWRITEs that
	 * only set/clear this bit.
	 */
	exec_control &= ~CPU_BASED_USE_MSR_BITMAPS;
	exec_control |= exec_controls_get(vmx) & CPU_BASED_USE_MSR_BITMAPS;

	exec_controls_set(vmx, exec_control);

	/*
	 * SECONDARY EXEC CONTROLS
	 */
	if (cpu_has_secondary_exec_ctrls()) {
		exec_control = vmx->secondary_exec_control;

		/* Take the following fields only from vmcs12 */
		exec_control &= ~(SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
				  SECONDARY_EXEC_ENABLE_INVPCID |
				  SECONDARY_EXEC_RDTSCP |
				  SECONDARY_EXEC_XSAVES |
				  SECONDARY_EXEC_ENABLE_USR_WAIT_PAUSE |
				  SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
				  SECONDARY_EXEC_APIC_REGISTER_VIRT |
				  SECONDARY_EXEC_ENABLE_VMFUNC);
		if (nested_cpu_has(vmcs12,
				   CPU_BASED_ACTIVATE_SECONDARY_CONTROLS)) {
			vmcs12_exec_ctrl = vmcs12->secondary_vm_exec_control &
				~SECONDARY_EXEC_ENABLE_PML;
			exec_control |= vmcs12_exec_ctrl;
		}

		/* VMCS shadowing for L2 is emulated for now */
		exec_control &= ~SECONDARY_EXEC_SHADOW_VMCS;

		/*
		 * Preset *DT exiting when emulating UMIP, so that vmx_set_cr4()
		 * will not have to rewrite the controls just for this bit.
		 */
		if (!boot_cpu_has(X86_FEATURE_UMIP) && vmx_umip_emulated() &&
		    (vmcs12->guest_cr4 & X86_CR4_UMIP))
			exec_control |= SECONDARY_EXEC_DESC;

		if (exec_control & SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY)
			vmcs_write16(GUEST_INTR_STATUS,
				vmcs12->guest_intr_status);

		if (!nested_cpu_has2(vmcs12, SECONDARY_EXEC_UNRESTRICTED_GUEST))
		    exec_control &= ~SECONDARY_EXEC_UNRESTRICTED_GUEST;

		secondary_exec_controls_set(vmx, exec_control);
	}

	/*
	 * ENTRY CONTROLS
	 *
	 * vmcs12's VM_{ENTRY,EXIT}_LOAD_IA32_EFER and VM_ENTRY_IA32E_MODE
	 * are emulated by vmx_set_efer() in prepare_vmcs02(), but speculate
	 * on the related bits (if supported by the CPU) in the hope that
	 * we can avoid VMWrites during vmx_set_efer().
	 */
	exec_control = (vmcs12->vm_entry_controls | vmx_vmentry_ctrl()) &
			~VM_ENTRY_IA32E_MODE & ~VM_ENTRY_LOAD_IA32_EFER;
	if (cpu_has_load_ia32_efer()) {
		if (guest_efer & EFER_LMA)
			exec_control |= VM_ENTRY_IA32E_MODE;
		if (guest_efer != host_efer)
			exec_control |= VM_ENTRY_LOAD_IA32_EFER;
	}
	vm_entry_controls_set(vmx, exec_control);

	/*
	 * EXIT CONTROLS
	 *
	 * L2->L1 exit controls are emulated - the hardware exit is to L0 so
	 * we should use its exit controls. Note that VM_EXIT_LOAD_IA32_EFER
	 * bits may be modified by vmx_set_efer() in prepare_vmcs02().
	 */
	exec_control = vmx_vmexit_ctrl();
	if (cpu_has_load_ia32_efer() && guest_efer != host_efer)
		exec_control |= VM_EXIT_LOAD_IA32_EFER;
	vm_exit_controls_set(vmx, exec_control);

	/*
	 * Interrupt/Exception Fields
	 */
	if (vmx->nested.nested_run_pending) {
		vmcs_write32(VM_ENTRY_INTR_INFO_FIELD,
			     vmcs12->vm_entry_intr_info_field);
		vmcs_write32(VM_ENTRY_EXCEPTION_ERROR_CODE,
			     vmcs12->vm_entry_exception_error_code);
		vmcs_write32(VM_ENTRY_INSTRUCTION_LEN,
			     vmcs12->vm_entry_instruction_len);
		vmcs_write32(GUEST_INTERRUPTIBILITY_INFO,
			     vmcs12->guest_interruptibility_info);
		vmx->loaded_vmcs->nmi_known_unmasked =
			!(vmcs12->guest_interruptibility_info & GUEST_INTR_STATE_NMI);
	} else {
		vmcs_write32(VM_ENTRY_INTR_INFO_FIELD, 0);
	}
}

static void prepare_vmcs02_rare(struct vcpu_vmx *vmx, struct vmcs12 *vmcs12)
{
	struct hv_enlightened_vmcs *hv_evmcs = vmx->nested.hv_evmcs;

	if (!hv_evmcs || !(hv_evmcs->hv_clean_fields &
			   HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2)) {
		vmcs_write16(GUEST_ES_SELECTOR, vmcs12->guest_es_selector);
		vmcs_write16(GUEST_CS_SELECTOR, vmcs12->guest_cs_selector);
		vmcs_write16(GUEST_SS_SELECTOR, vmcs12->guest_ss_selector);
		vmcs_write16(GUEST_DS_SELECTOR, vmcs12->guest_ds_selector);
		vmcs_write16(GUEST_FS_SELECTOR, vmcs12->guest_fs_selector);
		vmcs_write16(GUEST_GS_SELECTOR, vmcs12->guest_gs_selector);
		vmcs_write16(GUEST_LDTR_SELECTOR, vmcs12->guest_ldtr_selector);
		vmcs_write16(GUEST_TR_SELECTOR, vmcs12->guest_tr_selector);
		vmcs_write32(GUEST_ES_LIMIT, vmcs12->guest_es_limit);
		vmcs_write32(GUEST_CS_LIMIT, vmcs12->guest_cs_limit);
		vmcs_write32(GUEST_SS_LIMIT, vmcs12->guest_ss_limit);
		vmcs_write32(GUEST_DS_LIMIT, vmcs12->guest_ds_limit);
		vmcs_write32(GUEST_FS_LIMIT, vmcs12->guest_fs_limit);
		vmcs_write32(GUEST_GS_LIMIT, vmcs12->guest_gs_limit);
		vmcs_write32(GUEST_LDTR_LIMIT, vmcs12->guest_ldtr_limit);
		vmcs_write32(GUEST_TR_LIMIT, vmcs12->guest_tr_limit);
		vmcs_write32(GUEST_GDTR_LIMIT, vmcs12->guest_gdtr_limit);
		vmcs_write32(GUEST_IDTR_LIMIT, vmcs12->guest_idtr_limit);
		vmcs_write32(GUEST_CS_AR_BYTES, vmcs12->guest_cs_ar_bytes);
		vmcs_write32(GUEST_SS_AR_BYTES, vmcs12->guest_ss_ar_bytes);
		vmcs_write32(GUEST_ES_AR_BYTES, vmcs12->guest_es_ar_bytes);
		vmcs_write32(GUEST_DS_AR_BYTES, vmcs12->guest_ds_ar_bytes);
		vmcs_write32(GUEST_FS_AR_BYTES, vmcs12->guest_fs_ar_bytes);
		vmcs_write32(GUEST_GS_AR_BYTES, vmcs12->guest_gs_ar_bytes);
		vmcs_write32(GUEST_LDTR_AR_BYTES, vmcs12->guest_ldtr_ar_bytes);
		vmcs_write32(GUEST_TR_AR_BYTES, vmcs12->guest_tr_ar_bytes);
		vmcs_writel(GUEST_ES_BASE, vmcs12->guest_es_base);
		vmcs_writel(GUEST_CS_BASE, vmcs12->guest_cs_base);
		vmcs_writel(GUEST_SS_BASE, vmcs12->guest_ss_base);
		vmcs_writel(GUEST_DS_BASE, vmcs12->guest_ds_base);
		vmcs_writel(GUEST_FS_BASE, vmcs12->guest_fs_base);
		vmcs_writel(GUEST_GS_BASE, vmcs12->guest_gs_base);
		vmcs_writel(GUEST_LDTR_BASE, vmcs12->guest_ldtr_base);
		vmcs_writel(GUEST_TR_BASE, vmcs12->guest_tr_base);
		vmcs_writel(GUEST_GDTR_BASE, vmcs12->guest_gdtr_base);
		vmcs_writel(GUEST_IDTR_BASE, vmcs12->guest_idtr_base);
	}

	if (!hv_evmcs || !(hv_evmcs->hv_clean_fields &
			   HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1)) {
		vmcs_write32(GUEST_SYSENTER_CS, vmcs12->guest_sysenter_cs);
		vmcs_writel(GUEST_PENDING_DBG_EXCEPTIONS,
			    vmcs12->guest_pending_dbg_exceptions);
		vmcs_writel(GUEST_SYSENTER_ESP, vmcs12->guest_sysenter_esp);
		vmcs_writel(GUEST_SYSENTER_EIP, vmcs12->guest_sysenter_eip);

		/*
		 * L1 may access the L2's PDPTR, so save them to construct
		 * vmcs12
		 */
		if (enable_ept) {
			vmcs_write64(GUEST_PDPTR0, vmcs12->guest_pdptr0);
			vmcs_write64(GUEST_PDPTR1, vmcs12->guest_pdptr1);
			vmcs_write64(GUEST_PDPTR2, vmcs12->guest_pdptr2);
			vmcs_write64(GUEST_PDPTR3, vmcs12->guest_pdptr3);
		}

		if (kvm_mpx_supported() && vmx->nested.nested_run_pending &&
		    (vmcs12->vm_entry_controls & VM_ENTRY_LOAD_BNDCFGS))
			vmcs_write64(GUEST_BNDCFGS, vmcs12->guest_bndcfgs);
	}

	if (nested_cpu_has_xsaves(vmcs12))
		vmcs_write64(XSS_EXIT_BITMAP, vmcs12->xss_exit_bitmap);

	/*
	 * Whether page-faults are trapped is determined by a combination of
	 * 3 settings: PFEC_MASK, PFEC_MATCH and EXCEPTION_BITMAP.PF.  If L0
	 * doesn't care about page faults then we should set all of these to
	 * L1's desires. However, if L0 does care about (some) page faults, it
	 * is not easy (if at all possible?) to merge L0 and L1's desires, we
	 * simply ask to exit on each and every L2 page fault. This is done by
	 * setting MASK=MATCH=0 and (see below) EB.PF=1.
	 * Note that below we don't need special code to set EB.PF beyond the
	 * "or"ing of the EB of vmcs01 and vmcs12, because when enable_ept,
	 * vmcs01's EB.PF is 0 so the "or" will take vmcs12's value, and when
	 * !enable_ept, EB.PF is 1, so the "or" will always be 1.
	 */
	if (vmx_need_pf_intercept(&vmx->vcpu)) {
		/*
		 * TODO: if both L0 and L1 need the same MASK and MATCH,
		 * go ahead and use it?
		 */
		vmcs_write32(PAGE_FAULT_ERROR_CODE_MASK, 0);
		vmcs_write32(PAGE_FAULT_ERROR_CODE_MATCH, 0);
	} else {
		vmcs_write32(PAGE_FAULT_ERROR_CODE_MASK, vmcs12->page_fault_error_code_mask);
		vmcs_write32(PAGE_FAULT_ERROR_CODE_MATCH, vmcs12->page_fault_error_code_match);
	}

	if (cpu_has_vmx_apicv()) {
		vmcs_write64(EOI_EXIT_BITMAP0, vmcs12->eoi_exit_bitmap0);
		vmcs_write64(EOI_EXIT_BITMAP1, vmcs12->eoi_exit_bitmap1);
		vmcs_write64(EOI_EXIT_BITMAP2, vmcs12->eoi_exit_bitmap2);
		vmcs_write64(EOI_EXIT_BITMAP3, vmcs12->eoi_exit_bitmap3);
	}

	/*
	 * Make sure the msr_autostore list is up to date before we set the
	 * count in the vmcs02.
	 */
	prepare_vmx_msr_autostore_list(&vmx->vcpu, MSR_IA32_TSC);

	vmcs_write32(VM_EXIT_MSR_STORE_COUNT, vmx->msr_autostore.guest.nr);
	vmcs_write32(VM_EXIT_MSR_LOAD_COUNT, vmx->msr_autoload.host.nr);
	vmcs_write32(VM_ENTRY_MSR_LOAD_COUNT, vmx->msr_autoload.guest.nr);

	set_cr4_guest_host_mask(vmx);
}

/*
 * prepare_vmcs02 is called when the L1 guest hypervisor runs its nested
 * L2 guest. L1 has a vmcs for L2 (vmcs12), and this function "merges" it
 * with L0's requirements for its guest (a.k.a. vmcs01), so we can run the L2
 * guest in a way that will both be appropriate to L1's requests, and our
 * needs. In addition to modifying the active vmcs (which is vmcs02), this
 * function also has additional necessary side-effects, like setting various
 * vcpu->arch fields.
 * Returns 0 on success, 1 on failure. Invalid state exit qualification code
 * is assigned to entry_failure_code on failure.
 */
static int prepare_vmcs02(struct kvm_vcpu *vcpu, struct vmcs12 *vmcs12,
			  enum vm_entry_failure_code *entry_failure_code)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct hv_enlightened_vmcs *hv_evmcs = vmx->nested.hv_evmcs;
	bool load_guest_pdptrs_vmcs12 = false;

	if (vmx->nested.dirty_vmcs12 || hv_evmcs) {
		prepare_vmcs02_rare(vmx, vmcs12);
		vmx->nested.dirty_vmcs12 = false;

		load_guest_pdptrs_vmcs12 = !hv_evmcs ||
			!(hv_evmcs->hv_clean_fields &
			  HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1);
	}

	if (vmx->nested.nested_run_pending &&
	    (vmcs12->vm_entry_controls & VM_ENTRY_LOAD_DEBUG_CONTROLS)) {
		kvm_set_dr(vcpu, 7, vmcs12->guest_dr7);
		vmcs_write64(GUEST_IA32_DEBUGCTL, vmcs12->guest_ia32_debugctl);
	} else {
		kvm_set_dr(vcpu, 7, vcpu->arch.dr7);
		vmcs_write64(GUEST_IA32_DEBUGCTL, vmx->nested.vmcs01_debugctl);
	}
	if (kvm_mpx_supported() && (!vmx->nested.nested_run_pending ||
	    !(vmcs12->vm_entry_controls & VM_ENTRY_LOAD_BNDCFGS)))
		vmcs_write64(GUEST_BNDCFGS, vmx->nested.vmcs01_guest_bndcfgs);
	vmx_set_rflags(vcpu, vmcs12->guest_rflags);

	/* EXCEPTION_BITMAP and CR0_GUEST_HOST_MASK should basically be the
	 * bitwise-or of what L1 wants to trap for L2, and what we want to
	 * trap. Note that CR0.TS also needs updating - we do this later.
	 */
	update_exception_bitmap(vcpu);
	vcpu->arch.cr0_guest_owned_bits &= ~vmcs12->cr0_guest_host_mask;
	vmcs_writel(CR0_GUEST_HOST_MASK, ~vcpu->arch.cr0_guest_owned_bits);

	if (vmx->nested.nested_run_pending &&
	    (vmcs12->vm_entry_controls & VM_ENTRY_LOAD_IA32_PAT)) {
		vmcs_write64(GUEST_IA32_PAT, vmcs12->guest_ia32_pat);
		vcpu->arch.pat = vmcs12->guest_ia32_pat;
	} else if (vmcs_config.vmentry_ctrl & VM_ENTRY_LOAD_IA32_PAT) {
		vmcs_write64(GUEST_IA32_PAT, vmx->vcpu.arch.pat);
	}

	vmcs_write64(TSC_OFFSET, vcpu->arch.tsc_offset);

	if (kvm_has_tsc_control)
		decache_tsc_multiplier(vmx);

	nested_vmx_transition_tlb_flush(vcpu, vmcs12, true);

	if (nested_cpu_has_ept(vmcs12))
		nested_ept_init_mmu_context(vcpu);

	/*
	 * This sets GUEST_CR0 to vmcs12->guest_cr0, possibly modifying those
	 * bits which we consider mandatory enabled.
	 * The CR0_READ_SHADOW is what L2 should have expected to read given
	 * the specifications by L1; It's not enough to take
	 * vmcs12->cr0_read_shadow because on our cr0_guest_host_mask we we
	 * have more bits than L1 expected.
	 */
	vmx_set_cr0(vcpu, vmcs12->guest_cr0);
	vmcs_writel(CR0_READ_SHADOW, nested_read_cr0(vmcs12));

	vmx_set_cr4(vcpu, vmcs12->guest_cr4);
	vmcs_writel(CR4_READ_SHADOW, nested_read_cr4(vmcs12));

	vcpu->arch.efer = nested_vmx_calc_efer(vmx, vmcs12);
	/* Note: may modify VM_ENTRY/EXIT_CONTROLS and GUEST/HOST_IA32_EFER */
	vmx_set_efer(vcpu, vcpu->arch.efer);

	/*
	 * Guest state is invalid and unrestricted guest is disabled,
	 * which means L1 attempted VMEntry to L2 with invalid state.
	 * Fail the VMEntry.
	 */
	if (vmx->emulation_required) {
		*entry_failure_code = ENTRY_FAIL_DEFAULT;
		return -EINVAL;
	}

	/* Shadow page tables on either EPT or shadow page tables. */
	if (nested_vmx_load_cr3(vcpu, vmcs12->guest_cr3, nested_cpu_has_ept(vmcs12),
				entry_failure_code))
		return -EINVAL;

	/*
	 * Immediately write vmcs02.GUEST_CR3.  It will be propagated to vmcs12
	 * on nested VM-Exit, which can occur without actually running L2 and
	 * thus without hitting vmx_load_mmu_pgd(), e.g. if L1 is entering L2 with
	 * vmcs12.GUEST_ACTIVITYSTATE=HLT, in which case KVM will intercept the
	 * transition to HLT instead of running L2.
	 */
	if (enable_ept)
		vmcs_writel(GUEST_CR3, vmcs12->guest_cr3);

	/* Late preparation of GUEST_PDPTRs now that EFER and CRs are set. */
	if (load_guest_pdptrs_vmcs12 && nested_cpu_has_ept(vmcs12) &&
	    is_pae_paging(vcpu)) {
		vmcs_write64(GUEST_PDPTR0, vmcs12->guest_pdptr0);
		vmcs_write64(GUEST_PDPTR1, vmcs12->guest_pdptr1);
		vmcs_write64(GUEST_PDPTR2, vmcs12->guest_pdptr2);
		vmcs_write64(GUEST_PDPTR3, vmcs12->guest_pdptr3);
	}

	if (!enable_ept)
		vcpu->arch.walk_mmu->inject_page_fault = vmx_inject_page_fault_nested;

	if ((vmcs12->vm_entry_controls & VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL) &&
	    WARN_ON_ONCE(kvm_set_msr(vcpu, MSR_CORE_PERF_GLOBAL_CTRL,
				     vmcs12->guest_ia32_perf_global_ctrl)))
		return -EINVAL;

	kvm_rsp_write(vcpu, vmcs12->guest_rsp);
	kvm_rip_write(vcpu, vmcs12->guest_rip);
	return 0;
}

static int nested_vmx_check_nmi_controls(struct vmcs12 *vmcs12)
{
	if (CC(!nested_cpu_has_nmi_exiting(vmcs12) &&
	       nested_cpu_has_virtual_nmis(vmcs12)))
		return -EINVAL;

	if (CC(!nested_cpu_has_virtual_nmis(vmcs12) &&
	       nested_cpu_has(vmcs12, CPU_BASED_NMI_WINDOW_EXITING)))
		return -EINVAL;

	return 0;
}

static bool nested_vmx_check_eptp(struct kvm_vcpu *vcpu, u64 new_eptp)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	int maxphyaddr = cpuid_maxphyaddr(vcpu);

	/* Check for memory type validity */
	switch (new_eptp & VMX_EPTP_MT_MASK) {
	case VMX_EPTP_MT_UC:
		if (CC(!(vmx->nested.msrs.ept_caps & VMX_EPTP_UC_BIT)))
			return false;
		break;
	case VMX_EPTP_MT_WB:
		if (CC(!(vmx->nested.msrs.ept_caps & VMX_EPTP_WB_BIT)))
			return false;
		break;
	default:
		return false;
	}

	/* Page-walk levels validity. */
	switch (new_eptp & VMX_EPTP_PWL_MASK) {
	case VMX_EPTP_PWL_5:
		if (CC(!(vmx->nested.msrs.ept_caps & VMX_EPT_PAGE_WALK_5_BIT)))
			return false;
		break;
	case VMX_EPTP_PWL_4:
		if (CC(!(vmx->nested.msrs.ept_caps & VMX_EPT_PAGE_WALK_4_BIT)))
			return false;
		break;
	default:
		return false;
	}

	/* Reserved bits should not be set */
	if (CC(new_eptp >> maxphyaddr || ((new_eptp >> 7) & 0x1f)))
		return false;

	/* AD, if set, should be supported */
	if (new_eptp & VMX_EPTP_AD_ENABLE_BIT) {
		if (CC(!(vmx->nested.msrs.ept_caps & VMX_EPT_AD_BIT)))
			return false;
	}

	return true;
}

/*
 * Checks related to VM-Execution Control Fields
 */
static int nested_check_vm_execution_controls(struct kvm_vcpu *vcpu,
                                              struct vmcs12 *vmcs12)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (CC(!vmx_control_verify(vmcs12->pin_based_vm_exec_control,
				   vmx->nested.msrs.pinbased_ctls_low,
				   vmx->nested.msrs.pinbased_ctls_high)) ||
	    CC(!vmx_control_verify(vmcs12->cpu_based_vm_exec_control,
				   vmx->nested.msrs.procbased_ctls_low,
				   vmx->nested.msrs.procbased_ctls_high)))
		return -EINVAL;

	if (nested_cpu_has(vmcs12, CPU_BASED_ACTIVATE_SECONDARY_CONTROLS) &&
	    CC(!vmx_control_verify(vmcs12->secondary_vm_exec_control,
				   vmx->nested.msrs.secondary_ctls_low,
				   vmx->nested.msrs.secondary_ctls_high)))
		return -EINVAL;

	if (CC(vmcs12->cr3_target_count > nested_cpu_vmx_misc_cr3_count(vcpu)) ||
	    nested_vmx_check_io_bitmap_controls(vcpu, vmcs12) ||
	    nested_vmx_check_msr_bitmap_controls(vcpu, vmcs12) ||
	    nested_vmx_check_tpr_shadow_controls(vcpu, vmcs12) ||
	    nested_vmx_check_apic_access_controls(vcpu, vmcs12) ||
	    nested_vmx_check_apicv_controls(vcpu, vmcs12) ||
	    nested_vmx_check_nmi_controls(vmcs12) ||
	    nested_vmx_check_pml_controls(vcpu, vmcs12) ||
	    nested_vmx_check_unrestricted_guest_controls(vcpu, vmcs12) ||
	    nested_vmx_check_mode_based_ept_exec_controls(vcpu, vmcs12) ||
	    nested_vmx_check_shadow_vmcs_controls(vcpu, vmcs12) ||
	    CC(nested_cpu_has_vpid(vmcs12) && !vmcs12->virtual_processor_id))
		return -EINVAL;

	if (!nested_cpu_has_preemption_timer(vmcs12) &&
	    nested_cpu_has_save_preemption_timer(vmcs12))
		return -EINVAL;

	if (nested_cpu_has_ept(vmcs12) &&
	    CC(!nested_vmx_check_eptp(vcpu, vmcs12->ept_pointer)))
		return -EINVAL;

	if (nested_cpu_has_vmfunc(vmcs12)) {
		if (CC(vmcs12->vm_function_control &
		       ~vmx->nested.msrs.vmfunc_controls))
			return -EINVAL;

		if (nested_cpu_has_eptp_switching(vmcs12)) {
			if (CC(!nested_cpu_has_ept(vmcs12)) ||
			    CC(!page_address_valid(vcpu, vmcs12->eptp_list_address)))
				return -EINVAL;
		}
	}

	return 0;
}

/*
 * Checks related to VM-Exit Control Fields
 */
static int nested_check_vm_exit_controls(struct kvm_vcpu *vcpu,
                                         struct vmcs12 *vmcs12)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (CC(!vmx_control_verify(vmcs12->vm_exit_controls,
				    vmx->nested.msrs.exit_ctls_low,
				    vmx->nested.msrs.exit_ctls_high)) ||
	    CC(nested_vmx_check_exit_msr_switch_controls(vcpu, vmcs12)))
		return -EINVAL;

	return 0;
}

/*
 * Checks related to VM-Entry Control Fields
 */
static int nested_check_vm_entry_controls(struct kvm_vcpu *vcpu,
					  struct vmcs12 *vmcs12)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (CC(!vmx_control_verify(vmcs12->vm_entry_controls,
				    vmx->nested.msrs.entry_ctls_low,
				    vmx->nested.msrs.entry_ctls_high)))
		return -EINVAL;

	/*
	 * From the Intel SDM, volume 3:
	 * Fields relevant to VM-entry event injection must be set properly.
	 * These fields are the VM-entry interruption-information field, the
	 * VM-entry exception error code, and the VM-entry instruction length.
	 */
	if (vmcs12->vm_entry_intr_info_field & INTR_INFO_VALID_MASK) {
		u32 intr_info = vmcs12->vm_entry_intr_info_field;
		u8 vector = intr_info & INTR_INFO_VECTOR_MASK;
		u32 intr_type = intr_info & INTR_INFO_INTR_TYPE_MASK;
		bool has_error_code = intr_info & INTR_INFO_DELIVER_CODE_MASK;
		bool should_have_error_code;
		bool urg = nested_cpu_has2(vmcs12,
					   SECONDARY_EXEC_UNRESTRICTED_GUEST);
		bool prot_mode = !urg || vmcs12->guest_cr0 & X86_CR0_PE;

		/* VM-entry interruption-info field: interruption type */
		if (CC(intr_type == INTR_TYPE_RESERVED) ||
		    CC(intr_type == INTR_TYPE_OTHER_EVENT &&
		       !nested_cpu_supports_monitor_trap_flag(vcpu)))
			return -EINVAL;

		/* VM-entry interruption-info field: vector */
		if (CC(intr_type == INTR_TYPE_NMI_INTR && vector != NMI_VECTOR) ||
		    CC(intr_type == INTR_TYPE_HARD_EXCEPTION && vector > 31) ||
		    CC(intr_type == INTR_TYPE_OTHER_EVENT && vector != 0))
			return -EINVAL;

		/* VM-entry interruption-info field: deliver error code */
		should_have_error_code =
			intr_type == INTR_TYPE_HARD_EXCEPTION && prot_mode &&
			x86_exception_has_error_code(vector);
		if (CC(has_error_code != should_have_error_code))
			return -EINVAL;

		/* VM-entry exception error code */
		if (CC(has_error_code &&
		       vmcs12->vm_entry_exception_error_code & GENMASK(31, 16)))
			return -EINVAL;

		/* VM-entry interruption-info field: reserved bits */
		if (CC(intr_info & INTR_INFO_RESVD_BITS_MASK))
			return -EINVAL;

		/* VM-entry instruction length */
		switch (intr_type) {
		case INTR_TYPE_SOFT_EXCEPTION:
		case INTR_TYPE_SOFT_INTR:
		case INTR_TYPE_PRIV_SW_EXCEPTION:
			if (CC(vmcs12->vm_entry_instruction_len > 15) ||
			    CC(vmcs12->vm_entry_instruction_len == 0 &&
			    CC(!nested_cpu_has_zero_length_injection(vcpu))))
				return -EINVAL;
		}
	}

	if (nested_vmx_check_entry_msr_switch_controls(vcpu, vmcs12))
		return -EINVAL;

	return 0;
}

static int nested_vmx_check_controls(struct kvm_vcpu *vcpu,
				     struct vmcs12 *vmcs12)
{
	if (nested_check_vm_execution_controls(vcpu, vmcs12) ||
	    nested_check_vm_exit_controls(vcpu, vmcs12) ||
	    nested_check_vm_entry_controls(vcpu, vmcs12))
		return -EINVAL;

	if (to_vmx(vcpu)->nested.enlightened_vmcs_enabled)
		return nested_evmcs_check_controls(vmcs12);

	return 0;
}

static int nested_vmx_check_host_state(struct kvm_vcpu *vcpu,
				       struct vmcs12 *vmcs12)
{
	bool ia32e;

	if (CC(!nested_host_cr0_valid(vcpu, vmcs12->host_cr0)) ||
	    CC(!nested_host_cr4_valid(vcpu, vmcs12->host_cr4)) ||
	    CC(!nested_cr3_valid(vcpu, vmcs12->host_cr3)))
		return -EINVAL;

	if (CC(is_noncanonical_address(vmcs12->host_ia32_sysenter_esp, vcpu)) ||
	    CC(is_noncanonical_address(vmcs12->host_ia32_sysenter_eip, vcpu)))
		return -EINVAL;

	if ((vmcs12->vm_exit_controls & VM_EXIT_LOAD_IA32_PAT) &&
	    CC(!kvm_pat_valid(vmcs12->host_ia32_pat)))
		return -EINVAL;

	if ((vmcs12->vm_exit_controls & VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL) &&
	    CC(!kvm_valid_perf_global_ctrl(vcpu_to_pmu(vcpu),
					   vmcs12->host_ia32_perf_global_ctrl)))
		return -EINVAL;

#ifdef CONFIG_X86_64
	ia32e = !!(vcpu->arch.efer & EFER_LMA);
#else
	ia32e = false;
#endif

	if (ia32e) {
		if (CC(!(vmcs12->vm_exit_controls & VM_EXIT_HOST_ADDR_SPACE_SIZE)) ||
		    CC(!(vmcs12->host_cr4 & X86_CR4_PAE)))
			return -EINVAL;
	} else {
		if (CC(vmcs12->vm_exit_controls & VM_EXIT_HOST_ADDR_SPACE_SIZE) ||
		    CC(vmcs12->vm_entry_controls & VM_ENTRY_IA32E_MODE) ||
		    CC(vmcs12->host_cr4 & X86_CR4_PCIDE) ||
		    CC((vmcs12->host_rip) >> 32))
			return -EINVAL;
	}

	if (CC(vmcs12->host_cs_selector & (SEGMENT_RPL_MASK | SEGMENT_TI_MASK)) ||
	    CC(vmcs12->host_ss_selector & (SEGMENT_RPL_MASK | SEGMENT_TI_MASK)) ||
	    CC(vmcs12->host_ds_selector & (SEGMENT_RPL_MASK | SEGMENT_TI_MASK)) ||
	    CC(vmcs12->host_es_selector & (SEGMENT_RPL_MASK | SEGMENT_TI_MASK)) ||
	    CC(vmcs12->host_fs_selector & (SEGMENT_RPL_MASK | SEGMENT_TI_MASK)) ||
	    CC(vmcs12->host_gs_selector & (SEGMENT_RPL_MASK | SEGMENT_TI_MASK)) ||
	    CC(vmcs12->host_tr_selector & (SEGMENT_RPL_MASK | SEGMENT_TI_MASK)) ||
	    CC(vmcs12->host_cs_selector == 0) ||
	    CC(vmcs12->host_tr_selector == 0) ||
	    CC(vmcs12->host_ss_selector == 0 && !ia32e))
		return -EINVAL;

	if (CC(is_noncanonical_address(vmcs12->host_fs_base, vcpu)) ||
	    CC(is_noncanonical_address(vmcs12->host_gs_base, vcpu)) ||
	    CC(is_noncanonical_address(vmcs12->host_gdtr_base, vcpu)) ||
	    CC(is_noncanonical_address(vmcs12->host_idtr_base, vcpu)) ||
	    CC(is_noncanonical_address(vmcs12->host_tr_base, vcpu)) ||
	    CC(is_noncanonical_address(vmcs12->host_rip, vcpu)))
		return -EINVAL;

	/*
	 * If the load IA32_EFER VM-exit control is 1, bits reserved in the
	 * IA32_EFER MSR must be 0 in the field for that register. In addition,
	 * the values of the LMA and LME bits in the field must each be that of
	 * the host address-space size VM-exit control.
	 */
	if (vmcs12->vm_exit_controls & VM_EXIT_LOAD_IA32_EFER) {
		if (CC(!kvm_valid_efer(vcpu, vmcs12->host_ia32_efer)) ||
		    CC(ia32e != !!(vmcs12->host_ia32_efer & EFER_LMA)) ||
		    CC(ia32e != !!(vmcs12->host_ia32_efer & EFER_LME)))
			return -EINVAL;
	}

	return 0;
}

static int nested_vmx_check_vmcs_link_ptr(struct kvm_vcpu *vcpu,
					  struct vmcs12 *vmcs12)
{
	int r = 0;
	struct vmcs12 *shadow;
	struct kvm_host_map map;

	if (vmcs12->vmcs_link_pointer == -1ull)
		return 0;

	if (CC(!page_address_valid(vcpu, vmcs12->vmcs_link_pointer)))
		return -EINVAL;

	if (CC(kvm_vcpu_map(vcpu, gpa_to_gfn(vmcs12->vmcs_link_pointer), &map)))
		return -EINVAL;

	shadow = map.hva;

	if (CC(shadow->hdr.revision_id != VMCS12_REVISION) ||
	    CC(shadow->hdr.shadow_vmcs != nested_cpu_has_shadow_vmcs(vmcs12)))
		r = -EINVAL;

	kvm_vcpu_unmap(vcpu, &map, false);
	return r;
}

/*
 * Checks related to Guest Non-register State
 */
static int nested_check_guest_non_reg_state(struct vmcs12 *vmcs12)
{
	if (CC(vmcs12->guest_activity_state != GUEST_ACTIVITY_ACTIVE &&
	       vmcs12->guest_activity_state != GUEST_ACTIVITY_HLT))
		return -EINVAL;

	return 0;
}

static int nested_vmx_check_guest_state(struct kvm_vcpu *vcpu,
					struct vmcs12 *vmcs12,
					enum vm_entry_failure_code *entry_failure_code)
{
	bool ia32e;

	*entry_failure_code = ENTRY_FAIL_DEFAULT;

	if (CC(!nested_guest_cr0_valid(vcpu, vmcs12->guest_cr0)) ||
	    CC(!nested_guest_cr4_valid(vcpu, vmcs12->guest_cr4)))
		return -EINVAL;

	if ((vmcs12->vm_entry_controls & VM_ENTRY_LOAD_DEBUG_CONTROLS) &&
	    CC(!kvm_dr7_valid(vmcs12->guest_dr7)))
		return -EINVAL;

	if ((vmcs12->vm_entry_controls & VM_ENTRY_LOAD_IA32_PAT) &&
	    CC(!kvm_pat_valid(vmcs12->guest_ia32_pat)))
		return -EINVAL;

	if (nested_vmx_check_vmcs_link_ptr(vcpu, vmcs12)) {
		*entry_failure_code = ENTRY_FAIL_VMCS_LINK_PTR;
		return -EINVAL;
	}

	if ((vmcs12->vm_entry_controls & VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL) &&
	    CC(!kvm_valid_perf_global_ctrl(vcpu_to_pmu(vcpu),
					   vmcs12->guest_ia32_perf_global_ctrl)))
		return -EINVAL;

	/*
	 * If the load IA32_EFER VM-entry control is 1, the following checks
	 * are performed on the field for the IA32_EFER MSR:
	 * - Bits reserved in the IA32_EFER MSR must be 0.
	 * - Bit 10 (corresponding to IA32_EFER.LMA) must equal the value of
	 *   the IA-32e mode guest VM-exit control. It must also be identical
	 *   to bit 8 (LME) if bit 31 in the CR0 field (corresponding to
	 *   CR0.PG) is 1.
	 */
	if (to_vmx(vcpu)->nested.nested_run_pending &&
	    (vmcs12->vm_entry_controls & VM_ENTRY_LOAD_IA32_EFER)) {
		ia32e = (vmcs12->vm_entry_controls & VM_ENTRY_IA32E_MODE) != 0;
		if (CC(!kvm_valid_efer(vcpu, vmcs12->guest_ia32_efer)) ||
		    CC(ia32e != !!(vmcs12->guest_ia32_efer & EFER_LMA)) ||
		    CC(((vmcs12->guest_cr0 & X86_CR0_PG) &&
		     ia32e != !!(vmcs12->guest_ia32_efer & EFER_LME))))
			return -EINVAL;
	}

	if ((vmcs12->vm_entry_controls & VM_ENTRY_LOAD_BNDCFGS) &&
	    (CC(is_noncanonical_address(vmcs12->guest_bndcfgs & PAGE_MASK, vcpu)) ||
	     CC((vmcs12->guest_bndcfgs & MSR_IA32_BNDCFGS_RSVD))))
		return -EINVAL;

	if (nested_check_guest_non_reg_state(vmcs12))
		return -EINVAL;

	return 0;
}

static int nested_vmx_check_vmentry_hw(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	unsigned long cr3, cr4;
	bool vm_fail;

	if (!nested_early_check)
		return 0;

	if (vmx->msr_autoload.host.nr)
		vmcs_write32(VM_EXIT_MSR_LOAD_COUNT, 0);
	if (vmx->msr_autoload.guest.nr)
		vmcs_write32(VM_ENTRY_MSR_LOAD_COUNT, 0);

	preempt_disable();

	vmx_prepare_switch_to_guest(vcpu);

	/*
	 * Induce a consistency check VMExit by clearing bit 1 in GUEST_RFLAGS,
	 * which is reserved to '1' by hardware.  GUEST_RFLAGS is guaranteed to
	 * be written (by prepare_vmcs02()) before the "real" VMEnter, i.e.
	 * there is no need to preserve other bits or save/restore the field.
	 */
	vmcs_writel(GUEST_RFLAGS, 0);

	cr3 = __get_current_cr3_fast();
	if (unlikely(cr3 != vmx->loaded_vmcs->host_state.cr3)) {
		vmcs_writel(HOST_CR3, cr3);
		vmx->loaded_vmcs->host_state.cr3 = cr3;
	}

	cr4 = cr4_read_shadow();
	if (unlikely(cr4 != vmx->loaded_vmcs->host_state.cr4)) {
		vmcs_writel(HOST_CR4, cr4);
		vmx->loaded_vmcs->host_state.cr4 = cr4;
	}

	asm(
		"sub $%c[wordsize], %%" _ASM_SP "\n\t" /* temporarily adjust RSP for CALL */
		"cmp %%" _ASM_SP ", %c[host_state_rsp](%[loaded_vmcs]) \n\t"
		"je 1f \n\t"
		__ex("vmwrite %%" _ASM_SP ", %[HOST_RSP]") "\n\t"
		"mov %%" _ASM_SP ", %c[host_state_rsp](%[loaded_vmcs]) \n\t"
		"1: \n\t"
		"add $%c[wordsize], %%" _ASM_SP "\n\t" /* un-adjust RSP */

		/* Check if vmlaunch or vmresume is needed */
		"cmpb $0, %c[launched](%[loaded_vmcs])\n\t"

		/*
		 * VMLAUNCH and VMRESUME clear RFLAGS.{CF,ZF} on VM-Exit, set
		 * RFLAGS.CF on VM-Fail Invalid and set RFLAGS.ZF on VM-Fail
		 * Valid.  vmx_vmenter() directly "returns" RFLAGS, and so the
		 * results of VM-Enter is captured via CC_{SET,OUT} to vm_fail.
		 */
		"call vmx_vmenter\n\t"

		CC_SET(be)
	      : ASM_CALL_CONSTRAINT, CC_OUT(be) (vm_fail)
	      :	[HOST_RSP]"r"((unsigned long)HOST_RSP),
		[loaded_vmcs]"r"(vmx->loaded_vmcs),
		[launched]"i"(offsetof(struct loaded_vmcs, launched)),
		[host_state_rsp]"i"(offsetof(struct loaded_vmcs, host_state.rsp)),
		[wordsize]"i"(sizeof(ulong))
	      : "memory"
	);

	if (vmx->msr_autoload.host.nr)
		vmcs_write32(VM_EXIT_MSR_LOAD_COUNT, vmx->msr_autoload.host.nr);
	if (vmx->msr_autoload.guest.nr)
		vmcs_write32(VM_ENTRY_MSR_LOAD_COUNT, vmx->msr_autoload.guest.nr);

	if (vm_fail) {
		u32 error = vmcs_read32(VM_INSTRUCTION_ERROR);

		preempt_enable();

		trace_kvm_nested_vmenter_failed(
			"early hardware check VM-instruction error: ", error);
		WARN_ON_ONCE(error != VMXERR_ENTRY_INVALID_CONTROL_FIELD);
		return 1;
	}

	/*
	 * VMExit clears RFLAGS.IF and DR7, even on a consistency check.
	 */
	if (hw_breakpoint_active())
		set_debugreg(__this_cpu_read(cpu_dr7), 7);
	local_irq_enable();
	preempt_enable();

	/*
	 * A non-failing VMEntry means we somehow entered guest mode with
	 * an illegal RIP, and that's just the tip of the iceberg.  There
	 * is no telling what memory has been modified or what state has
	 * been exposed to unknown code.  Hitting this all but guarantees
	 * a (very critical) hardware issue.
	 */
	WARN_ON(!(vmcs_read32(VM_EXIT_REASON) &
		VMX_EXIT_REASONS_FAILED_VMENTRY));

	return 0;
}

static bool nested_get_vmcs12_pages(struct kvm_vcpu *vcpu)
{
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct kvm_host_map *map;
	struct page *page;
	u64 hpa;

	/*
	 * hv_evmcs may end up being not mapped after migration (when
	 * L2 was running), map it here to make sure vmcs12 changes are
	 * properly reflected.
	 */
	if (vmx->nested.enlightened_vmcs_enabled && !vmx->nested.hv_evmcs) {
		enum nested_evmptrld_status evmptrld_status =
			nested_vmx_handle_enlightened_vmptrld(vcpu, false);

		if (evmptrld_status == EVMPTRLD_VMFAIL ||
		    evmptrld_status == EVMPTRLD_ERROR) {
			pr_debug_ratelimited("%s: enlightened vmptrld failed\n",
					     __func__);
			vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			vcpu->run->internal.suberror =
				KVM_INTERNAL_ERROR_EMULATION;
			vcpu->run->internal.ndata = 0;
			return false;
		}
	}

	if (nested_cpu_has2(vmcs12, SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES)) {
		/*
		 * Translate L1 physical address to host physical
		 * address for vmcs02. Keep the page pinned, so this
		 * physical address remains valid. We keep a reference
		 * to it so we can release it later.
		 */
		if (vmx->nested.apic_access_page) { /* shouldn't happen */
			kvm_release_page_clean(vmx->nested.apic_access_page);
			vmx->nested.apic_access_page = NULL;
		}
		page = kvm_vcpu_gpa_to_page(vcpu, vmcs12->apic_access_addr);
		if (!is_error_page(page)) {
			vmx->nested.apic_access_page = page;
			hpa = page_to_phys(vmx->nested.apic_access_page);
			vmcs_write64(APIC_ACCESS_ADDR, hpa);
		} else {
			pr_debug_ratelimited("%s: no backing 'struct page' for APIC-access address in vmcs12\n",
					     __func__);
			vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			vcpu->run->internal.suberror =
				KVM_INTERNAL_ERROR_EMULATION;
			vcpu->run->internal.ndata = 0;
			return false;
		}
	}

	if (nested_cpu_has(vmcs12, CPU_BASED_TPR_SHADOW)) {
		map = &vmx->nested.virtual_apic_map;

		if (!kvm_vcpu_map(vcpu, gpa_to_gfn(vmcs12->virtual_apic_page_addr), map)) {
			vmcs_write64(VIRTUAL_APIC_PAGE_ADDR, pfn_to_hpa(map->pfn));
		} else if (nested_cpu_has(vmcs12, CPU_BASED_CR8_LOAD_EXITING) &&
		           nested_cpu_has(vmcs12, CPU_BASED_CR8_STORE_EXITING) &&
			   !nested_cpu_has2(vmcs12, SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES)) {
			/*
			 * The processor will never use the TPR shadow, simply
			 * clear the bit from the execution control.  Such a
			 * configuration is useless, but it happens in tests.
			 * For any other configuration, failing the vm entry is
			 * _not_ what the processor does but it's basically the
			 * only possibility we have.
			 */
			exec_controls_clearbit(vmx, CPU_BASED_TPR_SHADOW);
		} else {
			/*
			 * Write an illegal value to VIRTUAL_APIC_PAGE_ADDR to
			 * force VM-Entry to fail.
			 */
			vmcs_write64(VIRTUAL_APIC_PAGE_ADDR, -1ull);
		}
	}

	if (nested_cpu_has_posted_intr(vmcs12)) {
		map = &vmx->nested.pi_desc_map;

		if (!kvm_vcpu_map(vcpu, gpa_to_gfn(vmcs12->posted_intr_desc_addr), map)) {
			vmx->nested.pi_desc =
				(struct pi_desc *)(((void *)map->hva) +
				offset_in_page(vmcs12->posted_intr_desc_addr));
			vmcs_write64(POSTED_INTR_DESC_ADDR,
				     pfn_to_hpa(map->pfn) + offset_in_page(vmcs12->posted_intr_desc_addr));
		}
	}
	if (nested_vmx_prepare_msr_bitmap(vcpu, vmcs12))
		exec_controls_setbit(vmx, CPU_BASED_USE_MSR_BITMAPS);
	else
		exec_controls_clearbit(vmx, CPU_BASED_USE_MSR_BITMAPS);
	return true;
}

static int nested_vmx_write_pml_buffer(struct kvm_vcpu *vcpu, gpa_t gpa)
{
	struct vmcs12 *vmcs12;
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	gpa_t dst;

	if (WARN_ON_ONCE(!is_guest_mode(vcpu)))
		return 0;

	if (WARN_ON_ONCE(vmx->nested.pml_full))
		return 1;

	/*
	 * Check if PML is enabled for the nested guest. Whether eptp bit 6 is
	 * set is already checked as part of A/D emulation.
	 */
	vmcs12 = get_vmcs12(vcpu);
	if (!nested_cpu_has_pml(vmcs12))
		return 0;

	if (vmcs12->guest_pml_index >= PML_ENTITY_NUM) {
		vmx->nested.pml_full = true;
		return 1;
	}

	gpa &= ~0xFFFull;
	dst = vmcs12->pml_address + sizeof(u64) * vmcs12->guest_pml_index;

	if (kvm_write_guest_page(vcpu->kvm, gpa_to_gfn(dst), &gpa,
				 offset_in_page(dst), sizeof(gpa)))
		return 0;

	vmcs12->guest_pml_index--;

	return 0;
}

/*
 * Intel's VMX Instruction Reference specifies a common set of prerequisites
 * for running VMX instructions (except VMXON, whose prerequisites are
 * slightly different). It also specifies what exception to inject otherwise.
 * Note that many of these exceptions have priority over VM exits, so they
 * don't have to be checked again here.
 */
static int nested_vmx_check_permission(struct kvm_vcpu *vcpu)
{
	if (!to_vmx(vcpu)->nested.vmxon) {
		kvm_queue_exception(vcpu, UD_VECTOR);
		return 0;
	}

	if (vmx_get_cpl(vcpu)) {
		kvm_inject_gp(vcpu, 0);
		return 0;
	}

	return 1;
}

static u8 vmx_has_apicv_interrupt(struct kvm_vcpu *vcpu)
{
	u8 rvi = vmx_get_rvi();
	u8 vppr = kvm_lapic_get_reg(vcpu->arch.apic, APIC_PROCPRI);

	return ((rvi & 0xf0) > (vppr & 0xf0));
}

static void load_vmcs12_host_state(struct kvm_vcpu *vcpu,
				   struct vmcs12 *vmcs12);

/*
 * If from_vmentry is false, this is being called from state restore (either RSM
 * or KVM_SET_NESTED_STATE).  Otherwise it's called from vmlaunch/vmresume.
 *
 * Returns:
 *	NVMX_VMENTRY_SUCCESS: Entered VMX non-root mode
 *	NVMX_VMENTRY_VMFAIL:  Consistency check VMFail
 *	NVMX_VMENTRY_VMEXIT:  Consistency check VMExit
 *	NVMX_VMENTRY_KVM_INTERNAL_ERROR: KVM internal error
 */
enum nvmx_vmentry_status nested_vmx_enter_non_root_mode(struct kvm_vcpu *vcpu,
							bool from_vmentry)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
	enum vm_entry_failure_code entry_failure_code;
	bool evaluate_pending_interrupts;
	u32 exit_reason, failed_index;

	if (kvm_check_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu))
		kvm_vcpu_flush_tlb_current(vcpu);

	evaluate_pending_interrupts = exec_controls_get(vmx) &
		(CPU_BASED_INTR_WINDOW_EXITING | CPU_BASED_NMI_WINDOW_EXITING);
	if (likely(!evaluate_pending_interrupts) && kvm_vcpu_apicv_active(vcpu))
		evaluate_pending_interrupts |= vmx_has_apicv_interrupt(vcpu);

	if (!(vmcs12->vm_entry_controls & VM_ENTRY_LOAD_DEBUG_CONTROLS))
		vmx->nested.vmcs01_debugctl = vmcs_read64(GUEST_IA32_DEBUGCTL);
	if (kvm_mpx_supported() &&
		!(vmcs12->vm_entry_controls & VM_ENTRY_LOAD_BNDCFGS))
		vmx->nested.vmcs01_guest_bndcfgs = vmcs_read64(GUEST_BNDCFGS);

	/*
	 * Overwrite vmcs01.GUEST_CR3 with L1's CR3 if EPT is disabled *and*
	 * nested early checks are disabled.  In the event of a "late" VM-Fail,
	 * i.e. a VM-Fail detected by hardware but not KVM, KVM must unwind its
	 * software model to the pre-VMEntry host state.  When EPT is disabled,
	 * GUEST_CR3 holds KVM's shadow CR3, not L1's "real" CR3, which causes
	 * nested_vmx_restore_host_state() to corrupt vcpu->arch.cr3.  Stuffing
	 * vmcs01.GUEST_CR3 results in the unwind naturally setting arch.cr3 to
	 * the correct value.  Smashing vmcs01.GUEST_CR3 is safe because nested
	 * VM-Exits, and the unwind, reset KVM's MMU, i.e. vmcs01.GUEST_CR3 is
	 * guaranteed to be overwritten with a shadow CR3 prior to re-entering
	 * L1.  Don't stuff vmcs01.GUEST_CR3 when using nested early checks as
	 * KVM modifies vcpu->arch.cr3 if and only if the early hardware checks
	 * pass, and early VM-Fails do not reset KVM's MMU, i.e. the VM-Fail
	 * path would need to manually save/restore vmcs01.GUEST_CR3.
	 */
	if (!enable_ept && !nested_early_check)
		vmcs_writel(GUEST_CR3, vcpu->arch.cr3);

	vmx_switch_vmcs(vcpu, &vmx->nested.vmcs02);

	prepare_vmcs02_early(vmx, vmcs12);

	if (from_vmentry) {
		if (unlikely(!nested_get_vmcs12_pages(vcpu)))
			return NVMX_VMENTRY_KVM_INTERNAL_ERROR;

		if (nested_vmx_check_vmentry_hw(vcpu)) {
			vmx_switch_vmcs(vcpu, &vmx->vmcs01);
			return NVMX_VMENTRY_VMFAIL;
		}

		if (nested_vmx_check_guest_state(vcpu, vmcs12,
						 &entry_failure_code)) {
			exit_reason = EXIT_REASON_INVALID_STATE;
			vmcs12->exit_qualification = entry_failure_code;
			goto vmentry_fail_vmexit;
		}
	}

	enter_guest_mode(vcpu);
	if (vmcs12->cpu_based_vm_exec_control & CPU_BASED_USE_TSC_OFFSETTING)
		vcpu->arch.tsc_offset += vmcs12->tsc_offset;

	if (prepare_vmcs02(vcpu, vmcs12, &entry_failure_code)) {
		exit_reason = EXIT_REASON_INVALID_STATE;
		vmcs12->exit_qualification = entry_failure_code;
		goto vmentry_fail_vmexit_guest_mode;
	}

	if (from_vmentry) {
		failed_index = nested_vmx_load_msr(vcpu,
						   vmcs12->vm_entry_msr_load_addr,
						   vmcs12->vm_entry_msr_load_count);
		if (failed_index) {
			exit_reason = EXIT_REASON_MSR_LOAD_FAIL;
			vmcs12->exit_qualification = failed_index;
			goto vmentry_fail_vmexit_guest_mode;
		}
	} else {
		/*
		 * The MMU is not initialized to point at the right entities yet and
		 * "get pages" would need to read data from the guest (i.e. we will
		 * need to perform gpa to hpa translation). Request a call
		 * to nested_get_vmcs12_pages before the next VM-entry.  The MSRs
		 * have already been set at vmentry time and should not be reset.
		 */
		kvm_make_request(KVM_REQ_GET_VMCS12_PAGES, vcpu);
	}

	/*
	 * If L1 had a pending IRQ/NMI until it executed
	 * VMLAUNCH/VMRESUME which wasn't delivered because it was
	 * disallowed (e.g. interrupts disabled), L0 needs to
	 * evaluate if this pending event should cause an exit from L2
	 * to L1 or delivered directly to L2 (e.g. In case L1 don't
	 * intercept EXTERNAL_INTERRUPT).
	 *
	 * Usually this would be handled by the processor noticing an
	 * IRQ/NMI window request, or checking RVI during evaluation of
	 * pending virtual interrupts.  However, this setting was done
	 * on VMCS01 and now VMCS02 is active instead. Thus, we force L0
	 * to perform pending event evaluation by requesting a KVM_REQ_EVENT.
	 */
	if (unlikely(evaluate_pending_interrupts))
		kvm_make_request(KVM_REQ_EVENT, vcpu);

	/*
	 * Do not start the preemption timer hrtimer until after we know
	 * we are successful, so that only nested_vmx_vmexit needs to cancel
	 * the timer.
	 */
	vmx->nested.preemption_timer_expired = false;
	if (nested_cpu_has_preemption_timer(vmcs12)) {
		u64 timer_value = vmx_calc_preemption_timer_value(vcpu);
		vmx_start_preemption_timer(vcpu, timer_value);
	}

	/*
	 * Note no nested_vmx_succeed or nested_vmx_fail here. At this point
	 * we are no longer running L1, and VMLAUNCH/VMRESUME has not yet
	 * returned as far as L1 is concerned. It will only return (and set
	 * the success flag) when L2 exits (see nested_vmx_vmexit()).
	 */
	return NVMX_VMENTRY_SUCCESS;

	/*
	 * A failed consistency check that leads to a VMExit during L1's
	 * VMEnter to L2 is a variation of a normal VMexit, as explained in
	 * 26.7 "VM-entry failures during or after loading guest state".
	 */
vmentry_fail_vmexit_guest_mode:
	if (vmcs12->cpu_based_vm_exec_control & CPU_BASED_USE_TSC_OFFSETTING)
		vcpu->arch.tsc_offset -= vmcs12->tsc_offset;
	leave_guest_mode(vcpu);

vmentry_fail_vmexit:
	vmx_switch_vmcs(vcpu, &vmx->vmcs01);

	if (!from_vmentry)
		return NVMX_VMENTRY_VMEXIT;

	load_vmcs12_host_state(vcpu, vmcs12);
	vmcs12->vm_exit_reason = exit_reason | VMX_EXIT_REASONS_FAILED_VMENTRY;
	if (enable_shadow_vmcs || vmx->nested.hv_evmcs)
		vmx->nested.need_vmcs12_to_shadow_sync = true;
	return NVMX_VMENTRY_VMEXIT;
}

/*
 * nested_vmx_run() handles a nested entry, i.e., a VMLAUNCH or VMRESUME on L1
 * for running an L2 nested guest.
 */
static int nested_vmx_run(struct kvm_vcpu *vcpu, bool launch)
{
	struct vmcs12 *vmcs12;
	enum nvmx_vmentry_status status;
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	u32 interrupt_shadow = vmx_get_interrupt_shadow(vcpu);
	enum nested_evmptrld_status evmptrld_status;

	if (!nested_vmx_check_permission(vcpu))
		return 1;

	evmptrld_status = nested_vmx_handle_enlightened_vmptrld(vcpu, launch);
	if (evmptrld_status == EVMPTRLD_ERROR) {
		kvm_queue_exception(vcpu, UD_VECTOR);
		return 1;
	} else if (evmptrld_status == EVMPTRLD_VMFAIL) {
		return nested_vmx_failInvalid(vcpu);
	}

	if (!vmx->nested.hv_evmcs && vmx->nested.current_vmptr == -1ull)
		return nested_vmx_failInvalid(vcpu);

	vmcs12 = get_vmcs12(vcpu);

	/*
	 * Can't VMLAUNCH or VMRESUME a shadow VMCS. Despite the fact
	 * that there *is* a valid VMCS pointer, RFLAGS.CF is set
	 * rather than RFLAGS.ZF, and no error number is stored to the
	 * VM-instruction error field.
	 */
	if (vmcs12->hdr.shadow_vmcs)
		return nested_vmx_failInvalid(vcpu);

	if (vmx->nested.hv_evmcs) {
		copy_enlightened_to_vmcs12(vmx);
		/* Enlightened VMCS doesn't have launch state */
		vmcs12->launch_state = !launch;
	} else if (enable_shadow_vmcs) {
		copy_shadow_to_vmcs12(vmx);
	}

	/*
	 * The nested entry process starts with enforcing various prerequisites
	 * on vmcs12 as required by the Intel SDM, and act appropriately when
	 * they fail: As the SDM explains, some conditions should cause the
	 * instruction to fail, while others will cause the instruction to seem
	 * to succeed, but return an EXIT_REASON_INVALID_STATE.
	 * To speed up the normal (success) code path, we should avoid checking
	 * for misconfigurations which will anyway be caught by the processor
	 * when using the merged vmcs02.
	 */
	if (interrupt_shadow & KVM_X86_SHADOW_INT_MOV_SS)
		return nested_vmx_fail(vcpu, VMXERR_ENTRY_EVENTS_BLOCKED_BY_MOV_SS);

	if (vmcs12->launch_state == launch)
		return nested_vmx_fail(vcpu,
			launch ? VMXERR_VMLAUNCH_NONCLEAR_VMCS
			       : VMXERR_VMRESUME_NONLAUNCHED_VMCS);

	if (nested_vmx_check_controls(vcpu, vmcs12))
		return nested_vmx_fail(vcpu, VMXERR_ENTRY_INVALID_CONTROL_FIELD);

	if (nested_vmx_check_host_state(vcpu, vmcs12))
		return nested_vmx_fail(vcpu, VMXERR_ENTRY_INVALID_HOST_STATE_FIELD);

	/*
	 * We're finally done with prerequisite checking, and can start with
	 * the nested entry.
	 */
	vmx->nested.nested_run_pending = 1;
	vmx->nested.has_preemption_timer_deadline = false;
	status = nested_vmx_enter_non_root_mode(vcpu, true);
	if (unlikely(status != NVMX_VMENTRY_SUCCESS))
		goto vmentry_failed;

	/* Emulate processing of posted interrupts on VM-Enter. */
	if (nested_cpu_has_posted_intr(vmcs12) &&
	    kvm_apic_has_interrupt(vcpu) == vmx->nested.posted_intr_nv) {
		vmx->nested.pi_pending = true;
		kvm_make_request(KVM_REQ_EVENT, vcpu);
		kvm_apic_clear_irr(vcpu, vmx->nested.posted_intr_nv);
	}

	/* Hide L1D cache contents from the nested guest.  */
	vmx->vcpu.arch.l1tf_flush_l1d = true;

	/*
	 * Must happen outside of nested_vmx_enter_non_root_mode() as it will
	 * also be used as part of restoring nVMX state for
	 * snapshot restore (migration).
	 *
	 * In this flow, it is assumed that vmcs12 cache was
	 * trasferred as part of captured nVMX state and should
	 * therefore not be read from guest memory (which may not
	 * exist on destination host yet).
	 */
	nested_cache_shadow_vmcs12(vcpu, vmcs12);

	/*
	 * If we're entering a halted L2 vcpu and the L2 vcpu won't be
	 * awakened by event injection or by an NMI-window VM-exit or
	 * by an interrupt-window VM-exit, halt the vcpu.
	 */
	if ((vmcs12->guest_activity_state == GUEST_ACTIVITY_HLT) &&
	    !(vmcs12->vm_entry_intr_info_field & INTR_INFO_VALID_MASK) &&
	    !(vmcs12->cpu_based_vm_exec_control & CPU_BASED_NMI_WINDOW_EXITING) &&
	    !((vmcs12->cpu_based_vm_exec_control & CPU_BASED_INTR_WINDOW_EXITING) &&
	      (vmcs12->guest_rflags & X86_EFLAGS_IF))) {
		vmx->nested.nested_run_pending = 0;
		return kvm_vcpu_halt(vcpu);
	}
	return 1;

vmentry_failed:
	vmx->nested.nested_run_pending = 0;
	if (status == NVMX_VMENTRY_KVM_INTERNAL_ERROR)
		return 0;
	if (status == NVMX_VMENTRY_VMEXIT)
		return 1;
	WARN_ON_ONCE(status != NVMX_VMENTRY_VMFAIL);
	return nested_vmx_fail(vcpu, VMXERR_ENTRY_INVALID_CONTROL_FIELD);
}

/*
 * On a nested exit from L2 to L1, vmcs12.guest_cr0 might not be up-to-date
 * because L2 may have changed some cr0 bits directly (CR0_GUEST_HOST_MASK).
 * This function returns the new value we should put in vmcs12.guest_cr0.
 * It's not enough to just return the vmcs02 GUEST_CR0. Rather,
 *  1. Bits that neither L0 nor L1 trapped, were set directly by L2 and are now
 *     available in vmcs02 GUEST_CR0. (Note: It's enough to check that L0
 *     didn't trap the bit, because if L1 did, so would L0).
 *  2. Bits that L1 asked to trap (and therefore L0 also did) could not have
 *     been modified by L2, and L1 knows it. So just leave the old value of
 *     the bit from vmcs12.guest_cr0. Note that the bit from vmcs02 GUEST_CR0
 *     isn't relevant, because if L0 traps this bit it can set it to anything.
 *  3. Bits that L1 didn't trap, but L0 did. L1 believes the guest could have
 *     changed these bits, and therefore they need to be updated, but L0
 *     didn't necessarily allow them to be changed in GUEST_CR0 - and rather
 *     put them in vmcs02 CR0_READ_SHADOW. So take these bits from there.
 */
static inline unsigned long
vmcs12_guest_cr0(struct kvm_vcpu *vcpu, struct vmcs12 *vmcs12)
{
	return
	/*1*/	(vmcs_readl(GUEST_CR0) & vcpu->arch.cr0_guest_owned_bits) |
	/*2*/	(vmcs12->guest_cr0 & vmcs12->cr0_guest_host_mask) |
	/*3*/	(vmcs_readl(CR0_READ_SHADOW) & ~(vmcs12->cr0_guest_host_mask |
			vcpu->arch.cr0_guest_owned_bits));
}

static inline unsigned long
vmcs12_guest_cr4(struct kvm_vcpu *vcpu, struct vmcs12 *vmcs12)
{
	return
	/*1*/	(vmcs_readl(GUEST_CR4) & vcpu->arch.cr4_guest_owned_bits) |
	/*2*/	(vmcs12->guest_cr4 & vmcs12->cr4_guest_host_mask) |
	/*3*/	(vmcs_readl(CR4_READ_SHADOW) & ~(vmcs12->cr4_guest_host_mask |
			vcpu->arch.cr4_guest_owned_bits));
}

static void vmcs12_save_pending_event(struct kvm_vcpu *vcpu,
				      struct vmcs12 *vmcs12)
{
	u32 idt_vectoring;
	unsigned int nr;

	if (vcpu->arch.exception.injected) {
		nr = vcpu->arch.exception.nr;
		idt_vectoring = nr | VECTORING_INFO_VALID_MASK;

		if (kvm_exception_is_soft(nr)) {
			vmcs12->vm_exit_instruction_len =
				vcpu->arch.event_exit_inst_len;
			idt_vectoring |= INTR_TYPE_SOFT_EXCEPTION;
		} else
			idt_vectoring |= INTR_TYPE_HARD_EXCEPTION;

		if (vcpu->arch.exception.has_error_code) {
			idt_vectoring |= VECTORING_INFO_DELIVER_CODE_MASK;
			vmcs12->idt_vectoring_error_code =
				vcpu->arch.exception.error_code;
		}

		vmcs12->idt_vectoring_info_field = idt_vectoring;
	} else if (vcpu->arch.nmi_injected) {
		vmcs12->idt_vectoring_info_field =
			INTR_TYPE_NMI_INTR | INTR_INFO_VALID_MASK | NMI_VECTOR;
	} else if (vcpu->arch.interrupt.injected) {
		nr = vcpu->arch.interrupt.nr;
		idt_vectoring = nr | VECTORING_INFO_VALID_MASK;

		if (vcpu->arch.interrupt.soft) {
			idt_vectoring |= INTR_TYPE_SOFT_INTR;
			vmcs12->vm_entry_instruction_len =
				vcpu->arch.event_exit_inst_len;
		} else
			idt_vectoring |= INTR_TYPE_EXT_INTR;

		vmcs12->idt_vectoring_info_field = idt_vectoring;
	}
}


void nested_mark_vmcs12_pages_dirty(struct kvm_vcpu *vcpu)
{
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
	gfn_t gfn;

	/*
	 * Don't need to mark the APIC access page dirty; it is never
	 * written to by the CPU during APIC virtualization.
	 */

	if (nested_cpu_has(vmcs12, CPU_BASED_TPR_SHADOW)) {
		gfn = vmcs12->virtual_apic_page_addr >> PAGE_SHIFT;
		kvm_vcpu_mark_page_dirty(vcpu, gfn);
	}

	if (nested_cpu_has_posted_intr(vmcs12)) {
		gfn = vmcs12->posted_intr_desc_addr >> PAGE_SHIFT;
		kvm_vcpu_mark_page_dirty(vcpu, gfn);
	}
}

static void vmx_complete_nested_posted_interrupt(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	int max_irr;
	void *vapic_page;
	u16 status;

	if (!vmx->nested.pi_desc || !vmx->nested.pi_pending)
		return;

	vmx->nested.pi_pending = false;
	if (!pi_test_and_clear_on(vmx->nested.pi_desc))
		return;

	max_irr = find_last_bit((unsigned long *)vmx->nested.pi_desc->pir, 256);
	if (max_irr != 256) {
		vapic_page = vmx->nested.virtual_apic_map.hva;
		if (!vapic_page)
			return;

		__kvm_apic_update_irr(vmx->nested.pi_desc->pir,
			vapic_page, &max_irr);
		status = vmcs_read16(GUEST_INTR_STATUS);
		if ((u8)max_irr > ((u8)status & 0xff)) {
			status &= ~0xff;
			status |= (u8)max_irr;
			vmcs_write16(GUEST_INTR_STATUS, status);
		}
	}

	nested_mark_vmcs12_pages_dirty(vcpu);
}

static void nested_vmx_inject_exception_vmexit(struct kvm_vcpu *vcpu,
					       unsigned long exit_qual)
{
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
	unsigned int nr = vcpu->arch.exception.nr;
	u32 intr_info = nr | INTR_INFO_VALID_MASK;

	if (vcpu->arch.exception.has_error_code) {
		vmcs12->vm_exit_intr_error_code = vcpu->arch.exception.error_code;
		intr_info |= INTR_INFO_DELIVER_CODE_MASK;
	}

	if (kvm_exception_is_soft(nr))
		intr_info |= INTR_TYPE_SOFT_EXCEPTION;
	else
		intr_info |= INTR_TYPE_HARD_EXCEPTION;

	if (!(vmcs12->idt_vectoring_info_field & VECTORING_INFO_VALID_MASK) &&
	    vmx_get_nmi_mask(vcpu))
		intr_info |= INTR_INFO_UNBLOCK_NMI;

	nested_vmx_vmexit(vcpu, EXIT_REASON_EXCEPTION_NMI, intr_info, exit_qual);
}

/*
 * Returns true if a debug trap is pending delivery.
 *
 * In KVM, debug traps bear an exception payload. As such, the class of a #DB
 * exception may be inferred from the presence of an exception payload.
 */
static inline bool vmx_pending_dbg_trap(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.exception.pending &&
			vcpu->arch.exception.nr == DB_VECTOR &&
			vcpu->arch.exception.payload;
}

/*
 * Certain VM-exits set the 'pending debug exceptions' field to indicate a
 * recognized #DB (data or single-step) that has yet to be delivered. Since KVM
 * represents these debug traps with a payload that is said to be compatible
 * with the 'pending debug exceptions' field, write the payload to the VMCS
 * field if a VM-exit is delivered before the debug trap.
 */
static void nested_vmx_update_pending_dbg(struct kvm_vcpu *vcpu)
{
	if (vmx_pending_dbg_trap(vcpu))
		vmcs_writel(GUEST_PENDING_DBG_EXCEPTIONS,
			    vcpu->arch.exception.payload);
}

static bool nested_vmx_preemption_timer_pending(struct kvm_vcpu *vcpu)
{
	return nested_cpu_has_preemption_timer(get_vmcs12(vcpu)) &&
	       to_vmx(vcpu)->nested.preemption_timer_expired;
}

static int vmx_check_nested_events(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	unsigned long exit_qual;
	bool block_nested_events =
	    vmx->nested.nested_run_pending || kvm_event_needs_reinjection(vcpu);
	bool mtf_pending = vmx->nested.mtf_pending;
	struct kvm_lapic *apic = vcpu->arch.apic;

	/*
	 * Clear the MTF state. If a higher priority VM-exit is delivered first,
	 * this state is discarded.
	 */
	if (!block_nested_events)
		vmx->nested.mtf_pending = false;

	if (lapic_in_kernel(vcpu) &&
		test_bit(KVM_APIC_INIT, &apic->pending_events)) {
		if (block_nested_events)
			return -EBUSY;
		nested_vmx_update_pending_dbg(vcpu);
		clear_bit(KVM_APIC_INIT, &apic->pending_events);
		nested_vmx_vmexit(vcpu, EXIT_REASON_INIT_SIGNAL, 0, 0);
		return 0;
	}

	/*
	 * Process any exceptions that are not debug traps before MTF.
	 */
	if (vcpu->arch.exception.pending && !vmx_pending_dbg_trap(vcpu)) {
		if (block_nested_events)
			return -EBUSY;
		if (!nested_vmx_check_exception(vcpu, &exit_qual))
			goto no_vmexit;
		nested_vmx_inject_exception_vmexit(vcpu, exit_qual);
		return 0;
	}

	if (mtf_pending) {
		if (block_nested_events)
			return -EBUSY;
		nested_vmx_update_pending_dbg(vcpu);
		nested_vmx_vmexit(vcpu, EXIT_REASON_MONITOR_TRAP_FLAG, 0, 0);
		return 0;
	}

	if (vcpu->arch.exception.pending) {
		if (block_nested_events)
			return -EBUSY;
		if (!nested_vmx_check_exception(vcpu, &exit_qual))
			goto no_vmexit;
		nested_vmx_inject_exception_vmexit(vcpu, exit_qual);
		return 0;
	}

	if (nested_vmx_preemption_timer_pending(vcpu)) {
		if (block_nested_events)
			return -EBUSY;
		nested_vmx_vmexit(vcpu, EXIT_REASON_PREEMPTION_TIMER, 0, 0);
		return 0;
	}

	if (vcpu->arch.smi_pending && !is_smm(vcpu)) {
		if (block_nested_events)
			return -EBUSY;
		goto no_vmexit;
	}

	if (vcpu->arch.nmi_pending && !vmx_nmi_blocked(vcpu)) {
		if (block_nested_events)
			return -EBUSY;
		if (!nested_exit_on_nmi(vcpu))
			goto no_vmexit;

		nested_vmx_vmexit(vcpu, EXIT_REASON_EXCEPTION_NMI,
				  NMI_VECTOR | INTR_TYPE_NMI_INTR |
				  INTR_INFO_VALID_MASK, 0);
		/*
		 * The NMI-triggered VM exit counts as injection:
		 * clear this one and block further NMIs.
		 */
		vcpu->arch.nmi_pending = 0;
		vmx_set_nmi_mask(vcpu, true);
		return 0;
	}

	if (kvm_cpu_has_interrupt(vcpu) && !vmx_interrupt_blocked(vcpu)) {
		if (block_nested_events)
			return -EBUSY;
		if (!nested_exit_on_intr(vcpu))
			goto no_vmexit;
		nested_vmx_vmexit(vcpu, EXIT_REASON_EXTERNAL_INTERRUPT, 0, 0);
		return 0;
	}

no_vmexit:
	vmx_complete_nested_posted_interrupt(vcpu);
	return 0;
}

static u32 vmx_get_preemption_timer_value(struct kvm_vcpu *vcpu)
{
	ktime_t remaining =
		hrtimer_get_remaining(&to_vmx(vcpu)->nested.preemption_timer);
	u64 value;

	if (ktime_to_ns(remaining) <= 0)
		return 0;

	value = ktime_to_ns(remaining) * vcpu->arch.virtual_tsc_khz;
	do_div(value, 1000000);
	return value >> VMX_MISC_EMULATED_PREEMPTION_TIMER_RATE;
}

static bool is_vmcs12_ext_field(unsigned long field)
{
	switch (field) {
	case GUEST_ES_SELECTOR:
	case GUEST_CS_SELECTOR:
	case GUEST_SS_SELECTOR:
	case GUEST_DS_SELECTOR:
	case GUEST_FS_SELECTOR:
	case GUEST_GS_SELECTOR:
	case GUEST_LDTR_SELECTOR:
	case GUEST_TR_SELECTOR:
	case GUEST_ES_LIMIT:
	case GUEST_CS_LIMIT:
	case GUEST_SS_LIMIT:
	case GUEST_DS_LIMIT:
	case GUEST_FS_LIMIT:
	case GUEST_GS_LIMIT:
	case GUEST_LDTR_LIMIT:
	case GUEST_TR_LIMIT:
	case GUEST_GDTR_LIMIT:
	case GUEST_IDTR_LIMIT:
	case GUEST_ES_AR_BYTES:
	case GUEST_DS_AR_BYTES:
	case GUEST_FS_AR_BYTES:
	case GUEST_GS_AR_BYTES:
	case GUEST_LDTR_AR_BYTES:
	case GUEST_TR_AR_BYTES:
	case GUEST_ES_BASE:
	case GUEST_CS_BASE:
	case GUEST_SS_BASE:
	case GUEST_DS_BASE:
	case GUEST_FS_BASE:
	case GUEST_GS_BASE:
	case GUEST_LDTR_BASE:
	case GUEST_TR_BASE:
	case GUEST_GDTR_BASE:
	case GUEST_IDTR_BASE:
	case GUEST_PENDING_DBG_EXCEPTIONS:
	case GUEST_BNDCFGS:
		return true;
	default:
		break;
	}

	return false;
}

static void sync_vmcs02_to_vmcs12_rare(struct kvm_vcpu *vcpu,
				       struct vmcs12 *vmcs12)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	vmcs12->guest_es_selector = vmcs_read16(GUEST_ES_SELECTOR);
	vmcs12->guest_cs_selector = vmcs_read16(GUEST_CS_SELECTOR);
	vmcs12->guest_ss_selector = vmcs_read16(GUEST_SS_SELECTOR);
	vmcs12->guest_ds_selector = vmcs_read16(GUEST_DS_SELECTOR);
	vmcs12->guest_fs_selector = vmcs_read16(GUEST_FS_SELECTOR);
	vmcs12->guest_gs_selector = vmcs_read16(GUEST_GS_SELECTOR);
	vmcs12->guest_ldtr_selector = vmcs_read16(GUEST_LDTR_SELECTOR);
	vmcs12->guest_tr_selector = vmcs_read16(GUEST_TR_SELECTOR);
	vmcs12->guest_es_limit = vmcs_read32(GUEST_ES_LIMIT);
	vmcs12->guest_cs_limit = vmcs_read32(GUEST_CS_LIMIT);
	vmcs12->guest_ss_limit = vmcs_read32(GUEST_SS_LIMIT);
	vmcs12->guest_ds_limit = vmcs_read32(GUEST_DS_LIMIT);
	vmcs12->guest_fs_limit = vmcs_read32(GUEST_FS_LIMIT);
	vmcs12->guest_gs_limit = vmcs_read32(GUEST_GS_LIMIT);
	vmcs12->guest_ldtr_limit = vmcs_read32(GUEST_LDTR_LIMIT);
	vmcs12->guest_tr_limit = vmcs_read32(GUEST_TR_LIMIT);
	vmcs12->guest_gdtr_limit = vmcs_read32(GUEST_GDTR_LIMIT);
	vmcs12->guest_idtr_limit = vmcs_read32(GUEST_IDTR_LIMIT);
	vmcs12->guest_es_ar_bytes = vmcs_read32(GUEST_ES_AR_BYTES);
	vmcs12->guest_ds_ar_bytes = vmcs_read32(GUEST_DS_AR_BYTES);
	vmcs12->guest_fs_ar_bytes = vmcs_read32(GUEST_FS_AR_BYTES);
	vmcs12->guest_gs_ar_bytes = vmcs_read32(GUEST_GS_AR_BYTES);
	vmcs12->guest_ldtr_ar_bytes = vmcs_read32(GUEST_LDTR_AR_BYTES);
	vmcs12->guest_tr_ar_bytes = vmcs_read32(GUEST_TR_AR_BYTES);
	vmcs12->guest_es_base = vmcs_readl(GUEST_ES_BASE);
	vmcs12->guest_cs_base = vmcs_readl(GUEST_CS_BASE);
	vmcs12->guest_ss_base = vmcs_readl(GUEST_SS_BASE);
	vmcs12->guest_ds_base = vmcs_readl(GUEST_DS_BASE);
	vmcs12->guest_fs_base = vmcs_readl(GUEST_FS_BASE);
	vmcs12->guest_gs_base = vmcs_readl(GUEST_GS_BASE);
	vmcs12->guest_ldtr_base = vmcs_readl(GUEST_LDTR_BASE);
	vmcs12->guest_tr_base = vmcs_readl(GUEST_TR_BASE);
	vmcs12->guest_gdtr_base = vmcs_readl(GUEST_GDTR_BASE);
	vmcs12->guest_idtr_base = vmcs_readl(GUEST_IDTR_BASE);
	vmcs12->guest_pending_dbg_exceptions =
		vmcs_readl(GUEST_PENDING_DBG_EXCEPTIONS);
	if (kvm_mpx_supported())
		vmcs12->guest_bndcfgs = vmcs_read64(GUEST_BNDCFGS);

	vmx->nested.need_sync_vmcs02_to_vmcs12_rare = false;
}

static void copy_vmcs02_to_vmcs12_rare(struct kvm_vcpu *vcpu,
				       struct vmcs12 *vmcs12)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	int cpu;

	if (!vmx->nested.need_sync_vmcs02_to_vmcs12_rare)
		return;


	WARN_ON_ONCE(vmx->loaded_vmcs != &vmx->vmcs01);

	cpu = get_cpu();
	vmx->loaded_vmcs = &vmx->nested.vmcs02;
	vmx_vcpu_load_vmcs(vcpu, cpu, &vmx->vmcs01);

	sync_vmcs02_to_vmcs12_rare(vcpu, vmcs12);

	vmx->loaded_vmcs = &vmx->vmcs01;
	vmx_vcpu_load_vmcs(vcpu, cpu, &vmx->nested.vmcs02);
	put_cpu();
}

/*
 * Update the guest state fields of vmcs12 to reflect changes that
 * occurred while L2 was running. (The "IA-32e mode guest" bit of the
 * VM-entry controls is also updated, since this is really a guest
 * state bit.)
 */
static void sync_vmcs02_to_vmcs12(struct kvm_vcpu *vcpu, struct vmcs12 *vmcs12)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (vmx->nested.hv_evmcs)
		sync_vmcs02_to_vmcs12_rare(vcpu, vmcs12);

	vmx->nested.need_sync_vmcs02_to_vmcs12_rare = !vmx->nested.hv_evmcs;

	vmcs12->guest_cr0 = vmcs12_guest_cr0(vcpu, vmcs12);
	vmcs12->guest_cr4 = vmcs12_guest_cr4(vcpu, vmcs12);

	vmcs12->guest_rsp = kvm_rsp_read(vcpu);
	vmcs12->guest_rip = kvm_rip_read(vcpu);
	vmcs12->guest_rflags = vmcs_readl(GUEST_RFLAGS);

	vmcs12->guest_cs_ar_bytes = vmcs_read32(GUEST_CS_AR_BYTES);
	vmcs12->guest_ss_ar_bytes = vmcs_read32(GUEST_SS_AR_BYTES);

	vmcs12->guest_interruptibility_info =
		vmcs_read32(GUEST_INTERRUPTIBILITY_INFO);

	if (vcpu->arch.mp_state == KVM_MP_STATE_HALTED)
		vmcs12->guest_activity_state = GUEST_ACTIVITY_HLT;
	else
		vmcs12->guest_activity_state = GUEST_ACTIVITY_ACTIVE;

	if (nested_cpu_has_preemption_timer(vmcs12) &&
	    vmcs12->vm_exit_controls & VM_EXIT_SAVE_VMX_PREEMPTION_TIMER &&
	    !vmx->nested.nested_run_pending)
		vmcs12->vmx_preemption_timer_value =
			vmx_get_preemption_timer_value(vcpu);

	/*
	 * In some cases (usually, nested EPT), L2 is allowed to change its
	 * own CR3 without exiting. If it has changed it, we must keep it.
	 * Of course, if L0 is using shadow page tables, GUEST_CR3 was defined
	 * by L0, not L1 or L2, so we mustn't unconditionally copy it to vmcs12.
	 *
	 * Additionally, restore L2's PDPTR to vmcs12.
	 */
	if (enable_ept) {
		vmcs12->guest_cr3 = vmcs_readl(GUEST_CR3);
		if (nested_cpu_has_ept(vmcs12) && is_pae_paging(vcpu)) {
			vmcs12->guest_pdptr0 = vmcs_read64(GUEST_PDPTR0);
			vmcs12->guest_pdptr1 = vmcs_read64(GUEST_PDPTR1);
			vmcs12->guest_pdptr2 = vmcs_read64(GUEST_PDPTR2);
			vmcs12->guest_pdptr3 = vmcs_read64(GUEST_PDPTR3);
		}
	}

	vmcs12->guest_linear_address = vmcs_readl(GUEST_LINEAR_ADDRESS);

	if (nested_cpu_has_vid(vmcs12))
		vmcs12->guest_intr_status = vmcs_read16(GUEST_INTR_STATUS);

	vmcs12->vm_entry_controls =
		(vmcs12->vm_entry_controls & ~VM_ENTRY_IA32E_MODE) |
		(vm_entry_controls_get(to_vmx(vcpu)) & VM_ENTRY_IA32E_MODE);

	if (vmcs12->vm_exit_controls & VM_EXIT_SAVE_DEBUG_CONTROLS)
		kvm_get_dr(vcpu, 7, (unsigned long *)&vmcs12->guest_dr7);

	if (vmcs12->vm_exit_controls & VM_EXIT_SAVE_IA32_EFER)
		vmcs12->guest_ia32_efer = vcpu->arch.efer;
}

/*
 * prepare_vmcs12 is part of what we need to do when the nested L2 guest exits
 * and we want to prepare to run its L1 parent. L1 keeps a vmcs for L2 (vmcs12),
 * and this function updates it to reflect the changes to the guest state while
 * L2 was running (and perhaps made some exits which were handled directly by L0
 * without going back to L1), and to reflect the exit reason.
 * Note that we do not have to copy here all VMCS fields, just those that
 * could have changed by the L2 guest or the exit - i.e., the guest-state and
 * exit-information fields only. Other fields are modified by L1 with VMWRITE,
 * which already writes to vmcs12 directly.
 */
static void prepare_vmcs12(struct kvm_vcpu *vcpu, struct vmcs12 *vmcs12,
			   u32 vm_exit_reason, u32 exit_intr_info,
			   unsigned long exit_qualification)
{
	/* update exit information fields: */
	vmcs12->vm_exit_reason = vm_exit_reason;
	vmcs12->exit_qualification = exit_qualification;
	vmcs12->vm_exit_intr_info = exit_intr_info;

	vmcs12->idt_vectoring_info_field = 0;
	vmcs12->vm_exit_instruction_len = vmcs_read32(VM_EXIT_INSTRUCTION_LEN);
	vmcs12->vmx_instruction_info = vmcs_read32(VMX_INSTRUCTION_INFO);

	if (!(vmcs12->vm_exit_reason & VMX_EXIT_REASONS_FAILED_VMENTRY)) {
		vmcs12->launch_state = 1;

		/* vm_entry_intr_info_field is cleared on exit. Emulate this
		 * instead of reading the real value. */
		vmcs12->vm_entry_intr_info_field &= ~INTR_INFO_VALID_MASK;

		/*
		 * Transfer the event that L0 or L1 may wanted to inject into
		 * L2 to IDT_VECTORING_INFO_FIELD.
		 */
		vmcs12_save_pending_event(vcpu, vmcs12);

		/*
		 * According to spec, there's no need to store the guest's
		 * MSRs if the exit is due to a VM-entry failure that occurs
		 * during or after loading the guest state. Since this exit
		 * does not fall in that category, we need to save the MSRs.
		 */
		if (nested_vmx_store_msr(vcpu,
					 vmcs12->vm_exit_msr_store_addr,
					 vmcs12->vm_exit_msr_store_count))
			nested_vmx_abort(vcpu,
					 VMX_ABORT_SAVE_GUEST_MSR_FAIL);
	}

	/*
	 * Drop what we picked up for L2 via vmx_complete_interrupts. It is
	 * preserved above and would only end up incorrectly in L1.
	 */
	vcpu->arch.nmi_injected = false;
	kvm_clear_exception_queue(vcpu);
	kvm_clear_interrupt_queue(vcpu);
}

/*
 * A part of what we need to when the nested L2 guest exits and we want to
 * run its L1 parent, is to reset L1's guest state to the host state specified
 * in vmcs12.
 * This function is to be called not only on normal nested exit, but also on
 * a nested entry failure, as explained in Intel's spec, 3B.23.7 ("VM-Entry
 * Failures During or After Loading Guest State").
 * This function should be called when the active VMCS is L1's (vmcs01).
 */
static void load_vmcs12_host_state(struct kvm_vcpu *vcpu,
				   struct vmcs12 *vmcs12)
{
	enum vm_entry_failure_code ignored;
	struct kvm_segment seg;

	if (vmcs12->vm_exit_controls & VM_EXIT_LOAD_IA32_EFER)
		vcpu->arch.efer = vmcs12->host_ia32_efer;
	else if (vmcs12->vm_exit_controls & VM_EXIT_HOST_ADDR_SPACE_SIZE)
		vcpu->arch.efer |= (EFER_LMA | EFER_LME);
	else
		vcpu->arch.efer &= ~(EFER_LMA | EFER_LME);
	vmx_set_efer(vcpu, vcpu->arch.efer);

	kvm_rsp_write(vcpu, vmcs12->host_rsp);
	kvm_rip_write(vcpu, vmcs12->host_rip);
	vmx_set_rflags(vcpu, X86_EFLAGS_FIXED);
	vmx_set_interrupt_shadow(vcpu, 0);

	/*
	 * Note that calling vmx_set_cr0 is important, even if cr0 hasn't
	 * actually changed, because vmx_set_cr0 refers to efer set above.
	 *
	 * CR0_GUEST_HOST_MASK is already set in the original vmcs01
	 * (KVM doesn't change it);
	 */
	vcpu->arch.cr0_guest_owned_bits = KVM_POSSIBLE_CR0_GUEST_BITS;
	vmx_set_cr0(vcpu, vmcs12->host_cr0);

	/* Same as above - no reason to call set_cr4_guest_host_mask().  */
	vcpu->arch.cr4_guest_owned_bits = ~vmcs_readl(CR4_GUEST_HOST_MASK);
	vmx_set_cr4(vcpu, vmcs12->host_cr4);

	nested_ept_uninit_mmu_context(vcpu);

	/*
	 * Only PDPTE load can fail as the value of cr3 was checked on entry and
	 * couldn't have changed.
	 */
	if (nested_vmx_load_cr3(vcpu, vmcs12->host_cr3, false, &ignored))
		nested_vmx_abort(vcpu, VMX_ABORT_LOAD_HOST_PDPTE_FAIL);

	if (!enable_ept)
		vcpu->arch.walk_mmu->inject_page_fault = kvm_inject_page_fault;

	nested_vmx_transition_tlb_flush(vcpu, vmcs12, false);

	vmcs_write32(GUEST_SYSENTER_CS, vmcs12->host_ia32_sysenter_cs);
	vmcs_writel(GUEST_SYSENTER_ESP, vmcs12->host_ia32_sysenter_esp);
	vmcs_writel(GUEST_SYSENTER_EIP, vmcs12->host_ia32_sysenter_eip);
	vmcs_writel(GUEST_IDTR_BASE, vmcs12->host_idtr_base);
	vmcs_writel(GUEST_GDTR_BASE, vmcs12->host_gdtr_base);
	vmcs_write32(GUEST_IDTR_LIMIT, 0xFFFF);
	vmcs_write32(GUEST_GDTR_LIMIT, 0xFFFF);

	/* If not VM_EXIT_CLEAR_BNDCFGS, the L2 value propagates to L1.  */
	if (vmcs12->vm_exit_controls & VM_EXIT_CLEAR_BNDCFGS)
		vmcs_write64(GUEST_BNDCFGS, 0);

	if (vmcs12->vm_exit_controls & VM_EXIT_LOAD_IA32_PAT) {
		vmcs_write64(GUEST_IA32_PAT, vmcs12->host_ia32_pat);
		vcpu->arch.pat = vmcs12->host_ia32_pat;
	}
	if (vmcs12->vm_exit_controls & VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL)
		WARN_ON_ONCE(kvm_set_msr(vcpu, MSR_CORE_PERF_GLOBAL_CTRL,
					 vmcs12->host_ia32_perf_global_ctrl));

	/* Set L1 segment info according to Intel SDM
	    27.5.2 Loading Host Segment and Descriptor-Table Registers */
	seg = (struct kvm_segment) {
		.base = 0,
		.limit = 0xFFFFFFFF,
		.selector = vmcs12->host_cs_selector,
		.type = 11,
		.present = 1,
		.s = 1,
		.g = 1
	};
	if (vmcs12->vm_exit_controls & VM_EXIT_HOST_ADDR_SPACE_SIZE)
		seg.l = 1;
	else
		seg.db = 1;
	vmx_set_segment(vcpu, &seg, VCPU_SREG_CS);
	seg = (struct kvm_segment) {
		.base = 0,
		.limit = 0xFFFFFFFF,
		.type = 3,
		.present = 1,
		.s = 1,
		.db = 1,
		.g = 1
	};
	seg.selector = vmcs12->host_ds_selector;
	vmx_set_segment(vcpu, &seg, VCPU_SREG_DS);
	seg.selector = vmcs12->host_es_selector;
	vmx_set_segment(vcpu, &seg, VCPU_SREG_ES);
	seg.selector = vmcs12->host_ss_selector;
	vmx_set_segment(vcpu, &seg, VCPU_SREG_SS);
	seg.selector = vmcs12->host_fs_selector;
	seg.base = vmcs12->host_fs_base;
	vmx_set_segment(vcpu, &seg, VCPU_SREG_FS);
	seg.selector = vmcs12->host_gs_selector;
	seg.base = vmcs12->host_gs_base;
	vmx_set_segment(vcpu, &seg, VCPU_SREG_GS);
	seg = (struct kvm_segment) {
		.base = vmcs12->host_tr_base,
		.limit = 0x67,
		.selector = vmcs12->host_tr_selector,
		.type = 11,
		.present = 1
	};
	vmx_set_segment(vcpu, &seg, VCPU_SREG_TR);

	kvm_set_dr(vcpu, 7, 0x400);
	vmcs_write64(GUEST_IA32_DEBUGCTL, 0);

	if (cpu_has_vmx_msr_bitmap())
		vmx_update_msr_bitmap(vcpu);

	if (nested_vmx_load_msr(vcpu, vmcs12->vm_exit_msr_load_addr,
				vmcs12->vm_exit_msr_load_count))
		nested_vmx_abort(vcpu, VMX_ABORT_LOAD_HOST_MSR_FAIL);
}

static inline u64 nested_vmx_get_vmcs01_guest_efer(struct vcpu_vmx *vmx)
{
	struct shared_msr_entry *efer_msr;
	unsigned int i;

	if (vm_entry_controls_get(vmx) & VM_ENTRY_LOAD_IA32_EFER)
		return vmcs_read64(GUEST_IA32_EFER);

	if (cpu_has_load_ia32_efer())
		return host_efer;

	for (i = 0; i < vmx->msr_autoload.guest.nr; ++i) {
		if (vmx->msr_autoload.guest.val[i].index == MSR_EFER)
			return vmx->msr_autoload.guest.val[i].value;
	}

	efer_msr = find_msr_entry(vmx, MSR_EFER);
	if (efer_msr)
		return efer_msr->data;

	return host_efer;
}

static void nested_vmx_restore_host_state(struct kvm_vcpu *vcpu)
{
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct vmx_msr_entry g, h;
	gpa_t gpa;
	u32 i, j;

	vcpu->arch.pat = vmcs_read64(GUEST_IA32_PAT);

	if (vmcs12->vm_entry_controls & VM_ENTRY_LOAD_DEBUG_CONTROLS) {
		/*
		 * L1's host DR7 is lost if KVM_GUESTDBG_USE_HW_BP is set
		 * as vmcs01.GUEST_DR7 contains a userspace defined value
		 * and vcpu->arch.dr7 is not squirreled away before the
		 * nested VMENTER (not worth adding a variable in nested_vmx).
		 */
		if (vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP)
			kvm_set_dr(vcpu, 7, DR7_FIXED_1);
		else
			WARN_ON(kvm_set_dr(vcpu, 7, vmcs_readl(GUEST_DR7)));
	}

	/*
	 * Note that calling vmx_set_{efer,cr0,cr4} is important as they
	 * handle a variety of side effects to KVM's software model.
	 */
	vmx_set_efer(vcpu, nested_vmx_get_vmcs01_guest_efer(vmx));

	vcpu->arch.cr0_guest_owned_bits = KVM_POSSIBLE_CR0_GUEST_BITS;
	vmx_set_cr0(vcpu, vmcs_readl(CR0_READ_SHADOW));

	vcpu->arch.cr4_guest_owned_bits = ~vmcs_readl(CR4_GUEST_HOST_MASK);
	vmx_set_cr4(vcpu, vmcs_readl(CR4_READ_SHADOW));

	nested_ept_uninit_mmu_context(vcpu);
	vcpu->arch.cr3 = vmcs_readl(GUEST_CR3);
	kvm_register_mark_available(vcpu, VCPU_EXREG_CR3);

	/*
	 * Use ept_save_pdptrs(vcpu) to load the MMU's cached PDPTRs
	 * from vmcs01 (if necessary).  The PDPTRs are not loaded on
	 * VMFail, like everything else we just need to ensure our
	 * software model is up-to-date.
	 */
	if (enable_ept && is_pae_paging(vcpu))
		ept_save_pdptrs(vcpu);

	kvm_mmu_reset_context(vcpu);

	if (cpu_has_vmx_msr_bitmap())
		vmx_update_msr_bitmap(vcpu);

	/*
	 * This nasty bit of open coding is a compromise between blindly
	 * loading L1's MSRs using the exit load lists (incorrect emulation
	 * of VMFail), leaving the nested VM's MSRs in the software model
	 * (incorrect behavior) and snapshotting the modified MSRs (too
	 * expensive since the lists are unbound by hardware).  For each
	 * MSR that was (prematurely) loaded from the nested VMEntry load
	 * list, reload it from the exit load list if it exists and differs
	 * from the guest value.  The intent is to stuff host state as
	 * silently as possible, not to fully process the exit load list.
	 */
	for (i = 0; i < vmcs12->vm_entry_msr_load_count; i++) {
		gpa = vmcs12->vm_entry_msr_load_addr + (i * sizeof(g));
		if (kvm_vcpu_read_guest(vcpu, gpa, &g, sizeof(g))) {
			pr_debug_ratelimited(
				"%s read MSR index failed (%u, 0x%08llx)\n",
				__func__, i, gpa);
			goto vmabort;
		}

		for (j = 0; j < vmcs12->vm_exit_msr_load_count; j++) {
			gpa = vmcs12->vm_exit_msr_load_addr + (j * sizeof(h));
			if (kvm_vcpu_read_guest(vcpu, gpa, &h, sizeof(h))) {
				pr_debug_ratelimited(
					"%s read MSR failed (%u, 0x%08llx)\n",
					__func__, j, gpa);
				goto vmabort;
			}
			if (h.index != g.index)
				continue;
			if (h.value == g.value)
				break;

			if (nested_vmx_load_msr_check(vcpu, &h)) {
				pr_debug_ratelimited(
					"%s check failed (%u, 0x%x, 0x%x)\n",
					__func__, j, h.index, h.reserved);
				goto vmabort;
			}

			if (kvm_set_msr(vcpu, h.index, h.value)) {
				pr_debug_ratelimited(
					"%s WRMSR failed (%u, 0x%x, 0x%llx)\n",
					__func__, j, h.index, h.value);
				goto vmabort;
			}
		}
	}

	return;

vmabort:
	nested_vmx_abort(vcpu, VMX_ABORT_LOAD_HOST_MSR_FAIL);
}

/*
 * Emulate an exit from nested guest (L2) to L1, i.e., prepare to run L1
 * and modify vmcs12 to make it see what it would expect to see there if
 * L2 was its real guest. Must only be called when in L2 (is_guest_mode())
 */
void nested_vmx_vmexit(struct kvm_vcpu *vcpu, u32 vm_exit_reason,
		       u32 exit_intr_info, unsigned long exit_qualification)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);

	/* trying to cancel vmlaunch/vmresume is a bug */
	WARN_ON_ONCE(vmx->nested.nested_run_pending);

	/* Service the TLB flush request for L2 before switching to L1. */
	if (kvm_check_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu))
		kvm_vcpu_flush_tlb_current(vcpu);

	/*
	 * VCPU_EXREG_PDPTR will be clobbered in arch/x86/kvm/vmx/vmx.h between
	 * now and the new vmentry.  Ensure that the VMCS02 PDPTR fields are
	 * up-to-date before switching to L1.
	 */
	if (enable_ept && is_pae_paging(vcpu))
		vmx_ept_load_pdptrs(vcpu);

	leave_guest_mode(vcpu);

	if (nested_cpu_has_preemption_timer(vmcs12))
		hrtimer_cancel(&to_vmx(vcpu)->nested.preemption_timer);

	if (vmcs12->cpu_based_vm_exec_control & CPU_BASED_USE_TSC_OFFSETTING)
		vcpu->arch.tsc_offset -= vmcs12->tsc_offset;

	if (likely(!vmx->fail)) {
		sync_vmcs02_to_vmcs12(vcpu, vmcs12);

		if (vm_exit_reason != -1)
			prepare_vmcs12(vcpu, vmcs12, vm_exit_reason,
				       exit_intr_info, exit_qualification);

		/*
		 * Must happen outside of sync_vmcs02_to_vmcs12() as it will
		 * also be used to capture vmcs12 cache as part of
		 * capturing nVMX state for snapshot (migration).
		 *
		 * Otherwise, this flush will dirty guest memory at a
		 * point it is already assumed by user-space to be
		 * immutable.
		 */
		nested_flush_cached_shadow_vmcs12(vcpu, vmcs12);
	} else {
		/*
		 * The only expected VM-instruction error is "VM entry with
		 * invalid control field(s)." Anything else indicates a
		 * problem with L0.  And we should never get here with a
		 * VMFail of any type if early consistency checks are enabled.
		 */
		WARN_ON_ONCE(vmcs_read32(VM_INSTRUCTION_ERROR) !=
			     VMXERR_ENTRY_INVALID_CONTROL_FIELD);
		WARN_ON_ONCE(nested_early_check);
	}

	vmx_switch_vmcs(vcpu, &vmx->vmcs01);

	/* Update any VMCS fields that might have changed while L2 ran */
	vmcs_write32(VM_EXIT_MSR_LOAD_COUNT, vmx->msr_autoload.host.nr);
	vmcs_write32(VM_ENTRY_MSR_LOAD_COUNT, vmx->msr_autoload.guest.nr);
	vmcs_write64(TSC_OFFSET, vcpu->arch.tsc_offset);
	if (vmx->nested.l1_tpr_threshold != -1)
		vmcs_write32(TPR_THRESHOLD, vmx->nested.l1_tpr_threshold);

	if (kvm_has_tsc_control)
		decache_tsc_multiplier(vmx);

	if (vmx->nested.change_vmcs01_virtual_apic_mode) {
		vmx->nested.change_vmcs01_virtual_apic_mode = false;
		vmx_set_virtual_apic_mode(vcpu);
	}

	/* Unpin physical memory we referred to in vmcs02 */
	if (vmx->nested.apic_access_page) {
		kvm_release_page_clean(vmx->nested.apic_access_page);
		vmx->nested.apic_access_page = NULL;
	}
	kvm_vcpu_unmap(vcpu, &vmx->nested.virtual_apic_map, true);
	kvm_vcpu_unmap(vcpu, &vmx->nested.pi_desc_map, true);
	vmx->nested.pi_desc = NULL;

	if (vmx->nested.reload_vmcs01_apic_access_page) {
		vmx->nested.reload_vmcs01_apic_access_page = false;
		kvm_make_request(KVM_REQ_APIC_PAGE_RELOAD, vcpu);
	}

	if ((vm_exit_reason != -1) &&
	    (enable_shadow_vmcs || vmx->nested.hv_evmcs))
		vmx->nested.need_vmcs12_to_shadow_sync = true;

	/* in case we halted in L2 */
	vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;

	if (likely(!vmx->fail)) {
		if ((u16)vm_exit_reason == EXIT_REASON_EXTERNAL_INTERRUPT &&
		    nested_exit_intr_ack_set(vcpu)) {
			int irq = kvm_cpu_get_interrupt(vcpu);
			WARN_ON(irq < 0);
			vmcs12->vm_exit_intr_info = irq |
				INTR_INFO_VALID_MASK | INTR_TYPE_EXT_INTR;
		}

		if (vm_exit_reason != -1)
			trace_kvm_nested_vmexit_inject(vmcs12->vm_exit_reason,
						       vmcs12->exit_qualification,
						       vmcs12->idt_vectoring_info_field,
						       vmcs12->vm_exit_intr_info,
						       vmcs12->vm_exit_intr_error_code,
						       KVM_ISA_VMX);

		load_vmcs12_host_state(vcpu, vmcs12);

		return;
	}

	/*
	 * After an early L2 VM-entry failure, we're now back
	 * in L1 which thinks it just finished a VMLAUNCH or
	 * VMRESUME instruction, so we need to set the failure
	 * flag and the VM-instruction error field of the VMCS
	 * accordingly, and skip the emulated instruction.
	 */
	(void)nested_vmx_fail(vcpu, VMXERR_ENTRY_INVALID_CONTROL_FIELD);

	/*
	 * Restore L1's host state to KVM's software model.  We're here
	 * because a consistency check was caught by hardware, which
	 * means some amount of guest state has been propagated to KVM's
	 * model and needs to be unwound to the host's state.
	 */
	nested_vmx_restore_host_state(vcpu);

	vmx->fail = 0;
}

/*
 * Decode the memory-address operand of a vmx instruction, as recorded on an
 * exit caused by such an instruction (run by a guest hypervisor).
 * On success, returns 0. When the operand is invalid, returns 1 and throws
 * #UD, #GP, or #SS.
 */
int get_vmx_mem_address(struct kvm_vcpu *vcpu, unsigned long exit_qualification,
			u32 vmx_instruction_info, bool wr, int len, gva_t *ret)
{
	gva_t off;
	bool exn;
	struct kvm_segment s;

	/*
	 * According to Vol. 3B, "Information for VM Exits Due to Instruction
	 * Execution", on an exit, vmx_instruction_info holds most of the
	 * addressing components of the operand. Only the displacement part
	 * is put in exit_qualification (see 3B, "Basic VM-Exit Information").
	 * For how an actual address is calculated from all these components,
	 * refer to Vol. 1, "Operand Addressing".
	 */
	int  scaling = vmx_instruction_info & 3;
	int  addr_size = (vmx_instruction_info >> 7) & 7;
	bool is_reg = vmx_instruction_info & (1u << 10);
	int  seg_reg = (vmx_instruction_info >> 15) & 7;
	int  index_reg = (vmx_instruction_info >> 18) & 0xf;
	bool index_is_valid = !(vmx_instruction_info & (1u << 22));
	int  base_reg       = (vmx_instruction_info >> 23) & 0xf;
	bool base_is_valid  = !(vmx_instruction_info & (1u << 27));

	if (is_reg) {
		kvm_queue_exception(vcpu, UD_VECTOR);
		return 1;
	}

	/* Addr = segment_base + offset */
	/* offset = base + [index * scale] + displacement */
	off = exit_qualification; /* holds the displacement */
	if (addr_size == 1)
		off = (gva_t)sign_extend64(off, 31);
	else if (addr_size == 0)
		off = (gva_t)sign_extend64(off, 15);
	if (base_is_valid)
		off += kvm_register_read(vcpu, base_reg);
	if (index_is_valid)
		off += kvm_register_read(vcpu, index_reg) << scaling;
	vmx_get_segment(vcpu, &s, seg_reg);

	/*
	 * The effective address, i.e. @off, of a memory operand is truncated
	 * based on the address size of the instruction.  Note that this is
	 * the *effective address*, i.e. the address prior to accounting for
	 * the segment's base.
	 */
	if (addr_size == 1) /* 32 bit */
		off &= 0xffffffff;
	else if (addr_size == 0) /* 16 bit */
		off &= 0xffff;

	/* Checks for #GP/#SS exceptions. */
	exn = false;
	if (is_long_mode(vcpu)) {
		/*
		 * The virtual/linear address is never truncated in 64-bit
		 * mode, e.g. a 32-bit address size can yield a 64-bit virtual
		 * address when using FS/GS with a non-zero base.
		 */
		if (seg_reg == VCPU_SREG_FS || seg_reg == VCPU_SREG_GS)
			*ret = s.base + off;
		else
			*ret = off;

		/* Long mode: #GP(0)/#SS(0) if the memory address is in a
		 * non-canonical form. This is the only check on the memory
		 * destination for long mode!
		 */
		exn = is_noncanonical_address(*ret, vcpu);
	} else {
		/*
		 * When not in long mode, the virtual/linear address is
		 * unconditionally truncated to 32 bits regardless of the
		 * address size.
		 */
		*ret = (s.base + off) & 0xffffffff;

		/* Protected mode: apply checks for segment validity in the
		 * following order:
		 * - segment type check (#GP(0) may be thrown)
		 * - usability check (#GP(0)/#SS(0))
		 * - limit check (#GP(0)/#SS(0))
		 */
		if (wr)
			/* #GP(0) if the destination operand is located in a
			 * read-only data segment or any code segment.
			 */
			exn = ((s.type & 0xa) == 0 || (s.type & 8));
		else
			/* #GP(0) if the source operand is located in an
			 * execute-only code segment
			 */
			exn = ((s.type & 0xa) == 8);
		if (exn) {
			kvm_queue_exception_e(vcpu, GP_VECTOR, 0);
			return 1;
		}
		/* Protected mode: #GP(0)/#SS(0) if the segment is unusable.
		 */
		exn = (s.unusable != 0);

		/*
		 * Protected mode: #GP(0)/#SS(0) if the memory operand is
		 * outside the segment limit.  All CPUs that support VMX ignore
		 * limit checks for flat segments, i.e. segments with base==0,
		 * limit==0xffffffff and of type expand-up data or code.
		 */
		if (!(s.base == 0 && s.limit == 0xffffffff &&
		     ((s.type & 8) || !(s.type & 4))))
			exn = exn || ((u64)off + len - 1 > s.limit);
	}
	if (exn) {
		kvm_queue_exception_e(vcpu,
				      seg_reg == VCPU_SREG_SS ?
						SS_VECTOR : GP_VECTOR,
				      0);
		return 1;
	}

	return 0;
}

void nested_vmx_pmu_entry_exit_ctls_update(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx;

	if (!nested_vmx_allowed(vcpu))
		return;

	vmx = to_vmx(vcpu);
	if (kvm_x86_ops.pmu_ops->is_valid_msr(vcpu, MSR_CORE_PERF_GLOBAL_CTRL)) {
		vmx->nested.msrs.entry_ctls_high |=
				VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL;
		vmx->nested.msrs.exit_ctls_high |=
				VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL;
	} else {
		vmx->nested.msrs.entry_ctls_high &=
				~VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL;
		vmx->nested.msrs.exit_ctls_high &=
				~VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL;
	}
}

static int nested_vmx_get_vmptr(struct kvm_vcpu *vcpu, gpa_t *vmpointer,
				int *ret)
{
	gva_t gva;
	struct x86_exception e;
	int r;

	if (get_vmx_mem_address(vcpu, vmx_get_exit_qual(vcpu),
				vmcs_read32(VMX_INSTRUCTION_INFO), false,
				sizeof(*vmpointer), &gva)) {
		*ret = 1;
		return -EINVAL;
	}

	r = kvm_read_guest_virt(vcpu, gva, vmpointer, sizeof(*vmpointer), &e);
	if (r != X86EMUL_CONTINUE) {
		*ret = kvm_handle_memory_failure(vcpu, r, &e);
		return -EINVAL;
	}

	return 0;
}

/*
 * Allocate a shadow VMCS and associate it with the currently loaded
 * VMCS, unless such a shadow VMCS already exists. The newly allocated
 * VMCS is also VMCLEARed, so that it is ready for use.
 */
static struct vmcs *alloc_shadow_vmcs(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct loaded_vmcs *loaded_vmcs = vmx->loaded_vmcs;

	/*
	 * We should allocate a shadow vmcs for vmcs01 only when L1
	 * executes VMXON and free it when L1 executes VMXOFF.
	 * As it is invalid to execute VMXON twice, we shouldn't reach
	 * here when vmcs01 already have an allocated shadow vmcs.
	 */
	WARN_ON(loaded_vmcs == &vmx->vmcs01 && loaded_vmcs->shadow_vmcs);

	if (!loaded_vmcs->shadow_vmcs) {
		loaded_vmcs->shadow_vmcs = alloc_vmcs(true);
		if (loaded_vmcs->shadow_vmcs)
			vmcs_clear(loaded_vmcs->shadow_vmcs);
	}
	return loaded_vmcs->shadow_vmcs;
}

static int enter_vmx_operation(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	int r;

	r = alloc_loaded_vmcs(&vmx->nested.vmcs02);
	if (r < 0)
		goto out_vmcs02;

	vmx->nested.cached_vmcs12 = kzalloc(VMCS12_SIZE, GFP_KERNEL_ACCOUNT);
	if (!vmx->nested.cached_vmcs12)
		goto out_cached_vmcs12;

	vmx->nested.cached_shadow_vmcs12 = kzalloc(VMCS12_SIZE, GFP_KERNEL_ACCOUNT);
	if (!vmx->nested.cached_shadow_vmcs12)
		goto out_cached_shadow_vmcs12;

	if (enable_shadow_vmcs && !alloc_shadow_vmcs(vcpu))
		goto out_shadow_vmcs;

	hrtimer_init(&vmx->nested.preemption_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_ABS_PINNED);
	vmx->nested.preemption_timer.function = vmx_preemption_timer_fn;

	vmx->nested.vpid02 = allocate_vpid();

	vmx->nested.vmcs02_initialized = false;
	vmx->nested.vmxon = true;

	if (vmx_pt_mode_is_host_guest()) {
		vmx->pt_desc.guest.ctl = 0;
		pt_update_intercept_for_msr(vmx);
	}

	return 0;

out_shadow_vmcs:
	kfree(vmx->nested.cached_shadow_vmcs12);

out_cached_shadow_vmcs12:
	kfree(vmx->nested.cached_vmcs12);

out_cached_vmcs12:
	free_loaded_vmcs(&vmx->nested.vmcs02);

out_vmcs02:
	return -ENOMEM;
}

/*
 * Emulate the VMXON instruction.
 * Currently, we just remember that VMX is active, and do not save or even
 * inspect the argument to VMXON (the so-called "VMXON pointer") because we
 * do not currently need to store anything in that guest-allocated memory
 * region. Consequently, VMCLEAR and VMPTRLD also do not verify that the their
 * argument is different from the VMXON pointer (which the spec says they do).
 */
static int handle_vmon(struct kvm_vcpu *vcpu)
{
	int ret;
	gpa_t vmptr;
	uint32_t revision;
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	const u64 VMXON_NEEDED_FEATURES = FEAT_CTL_LOCKED
		| FEAT_CTL_VMX_ENABLED_OUTSIDE_SMX;

	/*
	 * The Intel VMX Instruction Reference lists a bunch of bits that are
	 * prerequisite to running VMXON, most notably cr4.VMXE must be set to
	 * 1 (see vmx_set_cr4() for when we allow the guest to set this).
	 * Otherwise, we should fail with #UD.  But most faulting conditions
	 * have already been checked by hardware, prior to the VM-exit for
	 * VMXON.  We do test guest cr4.VMXE because processor CR4 always has
	 * that bit set to 1 in non-root mode.
	 */
	if (!kvm_read_cr4_bits(vcpu, X86_CR4_VMXE)) {
		kvm_queue_exception(vcpu, UD_VECTOR);
		return 1;
	}

	/* CPL=0 must be checked manually. */
	if (vmx_get_cpl(vcpu)) {
		kvm_inject_gp(vcpu, 0);
		return 1;
	}

	if (vmx->nested.vmxon)
		return nested_vmx_fail(vcpu, VMXERR_VMXON_IN_VMX_ROOT_OPERATION);

	if ((vmx->msr_ia32_feature_control & VMXON_NEEDED_FEATURES)
			!= VMXON_NEEDED_FEATURES) {
		kvm_inject_gp(vcpu, 0);
		return 1;
	}

	if (nested_vmx_get_vmptr(vcpu, &vmptr, &ret))
		return ret;

	/*
	 * SDM 3: 24.11.5
	 * The first 4 bytes of VMXON region contain the supported
	 * VMCS revision identifier
	 *
	 * Note - IA32_VMX_BASIC[48] will never be 1 for the nested case;
	 * which replaces physical address width with 32
	 */
	if (!page_address_valid(vcpu, vmptr))
		return nested_vmx_failInvalid(vcpu);

	if (kvm_read_guest(vcpu->kvm, vmptr, &revision, sizeof(revision)) ||
	    revision != VMCS12_REVISION)
		return nested_vmx_failInvalid(vcpu);

	vmx->nested.vmxon_ptr = vmptr;
	ret = enter_vmx_operation(vcpu);
	if (ret)
		return ret;

	return nested_vmx_succeed(vcpu);
}

static inline void nested_release_vmcs12(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (vmx->nested.current_vmptr == -1ull)
		return;

	copy_vmcs02_to_vmcs12_rare(vcpu, get_vmcs12(vcpu));

	if (enable_shadow_vmcs) {
		/* copy to memory all shadowed fields in case
		   they were modified */
		copy_shadow_to_vmcs12(vmx);
		vmx_disable_shadow_vmcs(vmx);
	}
	vmx->nested.posted_intr_nv = -1;

	/* Flush VMCS12 to guest memory */
	kvm_vcpu_write_guest_page(vcpu,
				  vmx->nested.current_vmptr >> PAGE_SHIFT,
				  vmx->nested.cached_vmcs12, 0, VMCS12_SIZE);

	kvm_mmu_free_roots(vcpu, &vcpu->arch.guest_mmu, KVM_MMU_ROOTS_ALL);

	vmx->nested.current_vmptr = -1ull;
}

/* Emulate the VMXOFF instruction */
static int handle_vmoff(struct kvm_vcpu *vcpu)
{
	if (!nested_vmx_check_permission(vcpu))
		return 1;

	free_nested(vcpu);

	/* Process a latched INIT during time CPU was in VMX operation */
	kvm_make_request(KVM_REQ_EVENT, vcpu);

	return nested_vmx_succeed(vcpu);
}

/* Emulate the VMCLEAR instruction */
static int handle_vmclear(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	u32 zero = 0;
	gpa_t vmptr;
	u64 evmcs_gpa;
	int r;

	if (!nested_vmx_check_permission(vcpu))
		return 1;

	if (nested_vmx_get_vmptr(vcpu, &vmptr, &r))
		return r;

	if (!page_address_valid(vcpu, vmptr))
		return nested_vmx_fail(vcpu, VMXERR_VMCLEAR_INVALID_ADDRESS);

	if (vmptr == vmx->nested.vmxon_ptr)
		return nested_vmx_fail(vcpu, VMXERR_VMCLEAR_VMXON_POINTER);

	/*
	 * When Enlightened VMEntry is enabled on the calling CPU we treat
	 * memory area pointer by vmptr as Enlightened VMCS (as there's no good
	 * way to distinguish it from VMCS12) and we must not corrupt it by
	 * writing to the non-existent 'launch_state' field. The area doesn't
	 * have to be the currently active EVMCS on the calling CPU and there's
	 * nothing KVM has to do to transition it from 'active' to 'non-active'
	 * state. It is possible that the area will stay mapped as
	 * vmx->nested.hv_evmcs but this shouldn't be a problem.
	 */
	if (likely(!vmx->nested.enlightened_vmcs_enabled ||
		   !nested_enlightened_vmentry(vcpu, &evmcs_gpa))) {
		if (vmptr == vmx->nested.current_vmptr)
			nested_release_vmcs12(vcpu);

		kvm_vcpu_write_guest(vcpu,
				     vmptr + offsetof(struct vmcs12,
						      launch_state),
				     &zero, sizeof(zero));
	}

	return nested_vmx_succeed(vcpu);
}

/* Emulate the VMLAUNCH instruction */
static int handle_vmlaunch(struct kvm_vcpu *vcpu)
{
	return nested_vmx_run(vcpu, true);
}

/* Emulate the VMRESUME instruction */
static int handle_vmresume(struct kvm_vcpu *vcpu)
{

	return nested_vmx_run(vcpu, false);
}

static int handle_vmread(struct kvm_vcpu *vcpu)
{
	struct vmcs12 *vmcs12 = is_guest_mode(vcpu) ? get_shadow_vmcs12(vcpu)
						    : get_vmcs12(vcpu);
	unsigned long exit_qualification = vmx_get_exit_qual(vcpu);
	u32 instr_info = vmcs_read32(VMX_INSTRUCTION_INFO);
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct x86_exception e;
	unsigned long field;
	u64 value;
	gva_t gva = 0;
	short offset;
	int len, r;

	if (!nested_vmx_check_permission(vcpu))
		return 1;

	/*
	 * In VMX non-root operation, when the VMCS-link pointer is -1ull,
	 * any VMREAD sets the ALU flags for VMfailInvalid.
	 */
	if (vmx->nested.current_vmptr == -1ull ||
	    (is_guest_mode(vcpu) &&
	     get_vmcs12(vcpu)->vmcs_link_pointer == -1ull))
		return nested_vmx_failInvalid(vcpu);

	/* Decode instruction info and find the field to read */
	field = kvm_register_readl(vcpu, (((instr_info) >> 28) & 0xf));

	offset = vmcs_field_to_offset(field);
	if (offset < 0)
		return nested_vmx_fail(vcpu, VMXERR_UNSUPPORTED_VMCS_COMPONENT);

	if (!is_guest_mode(vcpu) && is_vmcs12_ext_field(field))
		copy_vmcs02_to_vmcs12_rare(vcpu, vmcs12);

	/* Read the field, zero-extended to a u64 value */
	value = vmcs12_read_any(vmcs12, field, offset);

	/*
	 * Now copy part of this value to register or memory, as requested.
	 * Note that the number of bits actually copied is 32 or 64 depending
	 * on the guest's mode (32 or 64 bit), not on the given field's length.
	 */
	if (instr_info & BIT(10)) {
		kvm_register_writel(vcpu, (((instr_info) >> 3) & 0xf), value);
	} else {
		len = is_64_bit_mode(vcpu) ? 8 : 4;
		if (get_vmx_mem_address(vcpu, exit_qualification,
					instr_info, true, len, &gva))
			return 1;
		/* _system ok, nested_vmx_check_permission has verified cpl=0 */
		r = kvm_write_guest_virt_system(vcpu, gva, &value, len, &e);
		if (r != X86EMUL_CONTINUE)
			return kvm_handle_memory_failure(vcpu, r, &e);
	}

	return nested_vmx_succeed(vcpu);
}

static bool is_shadow_field_rw(unsigned long field)
{
	switch (field) {
#define SHADOW_FIELD_RW(x, y) case x:
#include "vmcs_shadow_fields.h"
		return true;
	default:
		break;
	}
	return false;
}

static bool is_shadow_field_ro(unsigned long field)
{
	switch (field) {
#define SHADOW_FIELD_RO(x, y) case x:
#include "vmcs_shadow_fields.h"
		return true;
	default:
		break;
	}
	return false;
}

static int handle_vmwrite(struct kvm_vcpu *vcpu)
{
	struct vmcs12 *vmcs12 = is_guest_mode(vcpu) ? get_shadow_vmcs12(vcpu)
						    : get_vmcs12(vcpu);
	unsigned long exit_qualification = vmx_get_exit_qual(vcpu);
	u32 instr_info = vmcs_read32(VMX_INSTRUCTION_INFO);
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct x86_exception e;
	unsigned long field;
	short offset;
	gva_t gva;
	int len, r;

	/*
	 * The value to write might be 32 or 64 bits, depending on L1's long
	 * mode, and eventually we need to write that into a field of several
	 * possible lengths. The code below first zero-extends the value to 64
	 * bit (value), and then copies only the appropriate number of
	 * bits into the vmcs12 field.
	 */
	u64 value = 0;

	if (!nested_vmx_check_permission(vcpu))
		return 1;

	/*
	 * In VMX non-root operation, when the VMCS-link pointer is -1ull,
	 * any VMWRITE sets the ALU flags for VMfailInvalid.
	 */
	if (vmx->nested.current_vmptr == -1ull ||
	    (is_guest_mode(vcpu) &&
	     get_vmcs12(vcpu)->vmcs_link_pointer == -1ull))
		return nested_vmx_failInvalid(vcpu);

	if (instr_info & BIT(10))
		value = kvm_register_readl(vcpu, (((instr_info) >> 3) & 0xf));
	else {
		len = is_64_bit_mode(vcpu) ? 8 : 4;
		if (get_vmx_mem_address(vcpu, exit_qualification,
					instr_info, false, len, &gva))
			return 1;
		r = kvm_read_guest_virt(vcpu, gva, &value, len, &e);
		if (r != X86EMUL_CONTINUE)
			return kvm_handle_memory_failure(vcpu, r, &e);
	}

	field = kvm_register_readl(vcpu, (((instr_info) >> 28) & 0xf));

	offset = vmcs_field_to_offset(field);
	if (offset < 0)
		return nested_vmx_fail(vcpu, VMXERR_UNSUPPORTED_VMCS_COMPONENT);

	/*
	 * If the vCPU supports "VMWRITE to any supported field in the
	 * VMCS," then the "read-only" fields are actually read/write.
	 */
	if (vmcs_field_readonly(field) &&
	    !nested_cpu_has_vmwrite_any_field(vcpu))
		return nested_vmx_fail(vcpu, VMXERR_VMWRITE_READ_ONLY_VMCS_COMPONENT);

	/*
	 * Ensure vmcs12 is up-to-date before any VMWRITE that dirties
	 * vmcs12, else we may crush a field or consume a stale value.
	 */
	if (!is_guest_mode(vcpu) && !is_shadow_field_rw(field))
		copy_vmcs02_to_vmcs12_rare(vcpu, vmcs12);

	/*
	 * Some Intel CPUs intentionally drop the reserved bits of the AR byte
	 * fields on VMWRITE.  Emulate this behavior to ensure consistent KVM
	 * behavior regardless of the underlying hardware, e.g. if an AR_BYTE
	 * field is intercepted for VMWRITE but not VMREAD (in L1), then VMREAD
	 * from L1 will return a different value than VMREAD from L2 (L1 sees
	 * the stripped down value, L2 sees the full value as stored by KVM).
	 */
	if (field >= GUEST_ES_AR_BYTES && field <= GUEST_TR_AR_BYTES)
		value &= 0x1f0ff;

	vmcs12_write_any(vmcs12, field, offset, value);

	/*
	 * Do not track vmcs12 dirty-state if in guest-mode as we actually
	 * dirty shadow vmcs12 instead of vmcs12.  Fields that can be updated
	 * by L1 without a vmexit are always updated in the vmcs02, i.e. don't
	 * "dirty" vmcs12, all others go down the prepare_vmcs02() slow path.
	 */
	if (!is_guest_mode(vcpu) && !is_shadow_field_rw(field)) {
		/*
		 * L1 can read these fields without exiting, ensure the
		 * shadow VMCS is up-to-date.
		 */
		if (enable_shadow_vmcs && is_shadow_field_ro(field)) {
			preempt_disable();
			vmcs_load(vmx->vmcs01.shadow_vmcs);

			__vmcs_writel(field, value);

			vmcs_clear(vmx->vmcs01.shadow_vmcs);
			vmcs_load(vmx->loaded_vmcs->vmcs);
			preempt_enable();
		}
		vmx->nested.dirty_vmcs12 = true;
	}

	return nested_vmx_succeed(vcpu);
}

static void set_current_vmptr(struct vcpu_vmx *vmx, gpa_t vmptr)
{
	vmx->nested.current_vmptr = vmptr;
	if (enable_shadow_vmcs) {
		secondary_exec_controls_setbit(vmx, SECONDARY_EXEC_SHADOW_VMCS);
		vmcs_write64(VMCS_LINK_POINTER,
			     __pa(vmx->vmcs01.shadow_vmcs));
		vmx->nested.need_vmcs12_to_shadow_sync = true;
	}
	vmx->nested.dirty_vmcs12 = true;
}

/* Emulate the VMPTRLD instruction */
static int handle_vmptrld(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	gpa_t vmptr;
	int r;

	if (!nested_vmx_check_permission(vcpu))
		return 1;

	if (nested_vmx_get_vmptr(vcpu, &vmptr, &r))
		return r;

	if (!page_address_valid(vcpu, vmptr))
		return nested_vmx_fail(vcpu, VMXERR_VMPTRLD_INVALID_ADDRESS);

	if (vmptr == vmx->nested.vmxon_ptr)
		return nested_vmx_fail(vcpu, VMXERR_VMPTRLD_VMXON_POINTER);

	/* Forbid normal VMPTRLD if Enlightened version was used */
	if (vmx->nested.hv_evmcs)
		return 1;

	if (vmx->nested.current_vmptr != vmptr) {
		struct kvm_host_map map;
		struct vmcs12 *new_vmcs12;

		if (kvm_vcpu_map(vcpu, gpa_to_gfn(vmptr), &map)) {
			/*
			 * Reads from an unbacked page return all 1s,
			 * which means that the 32 bits located at the
			 * given physical address won't match the required
			 * VMCS12_REVISION identifier.
			 */
			return nested_vmx_fail(vcpu,
				VMXERR_VMPTRLD_INCORRECT_VMCS_REVISION_ID);
		}

		new_vmcs12 = map.hva;

		if (new_vmcs12->hdr.revision_id != VMCS12_REVISION ||
		    (new_vmcs12->hdr.shadow_vmcs &&
		     !nested_cpu_has_vmx_shadow_vmcs(vcpu))) {
			kvm_vcpu_unmap(vcpu, &map, false);
			return nested_vmx_fail(vcpu,
				VMXERR_VMPTRLD_INCORRECT_VMCS_REVISION_ID);
		}

		nested_release_vmcs12(vcpu);

		/*
		 * Load VMCS12 from guest memory since it is not already
		 * cached.
		 */
		memcpy(vmx->nested.cached_vmcs12, new_vmcs12, VMCS12_SIZE);
		kvm_vcpu_unmap(vcpu, &map, false);

		set_current_vmptr(vmx, vmptr);
	}

	return nested_vmx_succeed(vcpu);
}

/* Emulate the VMPTRST instruction */
static int handle_vmptrst(struct kvm_vcpu *vcpu)
{
	unsigned long exit_qual = vmx_get_exit_qual(vcpu);
	u32 instr_info = vmcs_read32(VMX_INSTRUCTION_INFO);
	gpa_t current_vmptr = to_vmx(vcpu)->nested.current_vmptr;
	struct x86_exception e;
	gva_t gva;
	int r;

	if (!nested_vmx_check_permission(vcpu))
		return 1;

	if (unlikely(to_vmx(vcpu)->nested.hv_evmcs))
		return 1;

	if (get_vmx_mem_address(vcpu, exit_qual, instr_info,
				true, sizeof(gpa_t), &gva))
		return 1;
	/* *_system ok, nested_vmx_check_permission has verified cpl=0 */
	r = kvm_write_guest_virt_system(vcpu, gva, (void *)&current_vmptr,
					sizeof(gpa_t), &e);
	if (r != X86EMUL_CONTINUE)
		return kvm_handle_memory_failure(vcpu, r, &e);

	return nested_vmx_succeed(vcpu);
}

#define EPTP_PA_MASK   GENMASK_ULL(51, 12)

static bool nested_ept_root_matches(hpa_t root_hpa, u64 root_eptp, u64 eptp)
{
	return VALID_PAGE(root_hpa) &&
		((root_eptp & EPTP_PA_MASK) == (eptp & EPTP_PA_MASK));
}

/* Emulate the INVEPT instruction */
static int handle_invept(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	u32 vmx_instruction_info, types;
	unsigned long type, roots_to_free;
	struct kvm_mmu *mmu;
	gva_t gva;
	struct x86_exception e;
	struct {
		u64 eptp, gpa;
	} operand;
	int i, r;

	if (!(vmx->nested.msrs.secondary_ctls_high &
	      SECONDARY_EXEC_ENABLE_EPT) ||
	    !(vmx->nested.msrs.ept_caps & VMX_EPT_INVEPT_BIT)) {
		kvm_queue_exception(vcpu, UD_VECTOR);
		return 1;
	}

	if (!nested_vmx_check_permission(vcpu))
		return 1;

	vmx_instruction_info = vmcs_read32(VMX_INSTRUCTION_INFO);
	type = kvm_register_readl(vcpu, (vmx_instruction_info >> 28) & 0xf);

	types = (vmx->nested.msrs.ept_caps >> VMX_EPT_EXTENT_SHIFT) & 6;

	if (type >= 32 || !(types & (1 << type)))
		return nested_vmx_fail(vcpu, VMXERR_INVALID_OPERAND_TO_INVEPT_INVVPID);

	/* According to the Intel VMX instruction reference, the memory
	 * operand is read even if it isn't needed (e.g., for type==global)
	 */
	if (get_vmx_mem_address(vcpu, vmx_get_exit_qual(vcpu),
			vmx_instruction_info, false, sizeof(operand), &gva))
		return 1;
	r = kvm_read_guest_virt(vcpu, gva, &operand, sizeof(operand), &e);
	if (r != X86EMUL_CONTINUE)
		return kvm_handle_memory_failure(vcpu, r, &e);

	/*
	 * Nested EPT roots are always held through guest_mmu,
	 * not root_mmu.
	 */
	mmu = &vcpu->arch.guest_mmu;

	switch (type) {
	case VMX_EPT_EXTENT_CONTEXT:
		if (!nested_vmx_check_eptp(vcpu, operand.eptp))
			return nested_vmx_fail(vcpu,
				VMXERR_INVALID_OPERAND_TO_INVEPT_INVVPID);

		roots_to_free = 0;
		if (nested_ept_root_matches(mmu->root_hpa, mmu->root_pgd,
					    operand.eptp))
			roots_to_free |= KVM_MMU_ROOT_CURRENT;

		for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++) {
			if (nested_ept_root_matches(mmu->prev_roots[i].hpa,
						    mmu->prev_roots[i].pgd,
						    operand.eptp))
				roots_to_free |= KVM_MMU_ROOT_PREVIOUS(i);
		}
		break;
	case VMX_EPT_EXTENT_GLOBAL:
		roots_to_free = KVM_MMU_ROOTS_ALL;
		break;
	default:
		BUG();
		break;
	}

	if (roots_to_free)
		kvm_mmu_free_roots(vcpu, mmu, roots_to_free);

	return nested_vmx_succeed(vcpu);
}

static int handle_invvpid(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	u32 vmx_instruction_info;
	unsigned long type, types;
	gva_t gva;
	struct x86_exception e;
	struct {
		u64 vpid;
		u64 gla;
	} operand;
	u16 vpid02;
	int r;

	if (!(vmx->nested.msrs.secondary_ctls_high &
	      SECONDARY_EXEC_ENABLE_VPID) ||
			!(vmx->nested.msrs.vpid_caps & VMX_VPID_INVVPID_BIT)) {
		kvm_queue_exception(vcpu, UD_VECTOR);
		return 1;
	}

	if (!nested_vmx_check_permission(vcpu))
		return 1;

	vmx_instruction_info = vmcs_read32(VMX_INSTRUCTION_INFO);
	type = kvm_register_readl(vcpu, (vmx_instruction_info >> 28) & 0xf);

	types = (vmx->nested.msrs.vpid_caps &
			VMX_VPID_EXTENT_SUPPORTED_MASK) >> 8;

	if (type >= 32 || !(types & (1 << type)))
		return nested_vmx_fail(vcpu,
			VMXERR_INVALID_OPERAND_TO_INVEPT_INVVPID);

	/* according to the intel vmx instruction reference, the memory
	 * operand is read even if it isn't needed (e.g., for type==global)
	 */
	if (get_vmx_mem_address(vcpu, vmx_get_exit_qual(vcpu),
			vmx_instruction_info, false, sizeof(operand), &gva))
		return 1;
	r = kvm_read_guest_virt(vcpu, gva, &operand, sizeof(operand), &e);
	if (r != X86EMUL_CONTINUE)
		return kvm_handle_memory_failure(vcpu, r, &e);

	if (operand.vpid >> 16)
		return nested_vmx_fail(vcpu,
			VMXERR_INVALID_OPERAND_TO_INVEPT_INVVPID);

	vpid02 = nested_get_vpid02(vcpu);
	switch (type) {
	case VMX_VPID_EXTENT_INDIVIDUAL_ADDR:
		if (!operand.vpid ||
		    is_noncanonical_address(operand.gla, vcpu))
			return nested_vmx_fail(vcpu,
				VMXERR_INVALID_OPERAND_TO_INVEPT_INVVPID);
		vpid_sync_vcpu_addr(vpid02, operand.gla);
		break;
	case VMX_VPID_EXTENT_SINGLE_CONTEXT:
	case VMX_VPID_EXTENT_SINGLE_NON_GLOBAL:
		if (!operand.vpid)
			return nested_vmx_fail(vcpu,
				VMXERR_INVALID_OPERAND_TO_INVEPT_INVVPID);
		vpid_sync_context(vpid02);
		break;
	case VMX_VPID_EXTENT_ALL_CONTEXT:
		vpid_sync_context(vpid02);
		break;
	default:
		WARN_ON_ONCE(1);
		return kvm_skip_emulated_instruction(vcpu);
	}

	/*
	 * Sync the shadow page tables if EPT is disabled, L1 is invalidating
	 * linear mappings for L2 (tagged with L2's VPID).  Free all roots as
	 * VPIDs are not tracked in the MMU role.
	 *
	 * Note, this operates on root_mmu, not guest_mmu, as L1 and L2 share
	 * an MMU when EPT is disabled.
	 *
	 * TODO: sync only the affected SPTEs for INVDIVIDUAL_ADDR.
	 */
	if (!enable_ept)
		kvm_mmu_free_roots(vcpu, &vcpu->arch.root_mmu,
				   KVM_MMU_ROOTS_ALL);

	return nested_vmx_succeed(vcpu);
}

static int nested_vmx_eptp_switching(struct kvm_vcpu *vcpu,
				     struct vmcs12 *vmcs12)
{
	u32 index = kvm_rcx_read(vcpu);
	u64 new_eptp;
	bool accessed_dirty;
	struct kvm_mmu *mmu = vcpu->arch.walk_mmu;

	if (!nested_cpu_has_eptp_switching(vmcs12) ||
	    !nested_cpu_has_ept(vmcs12))
		return 1;

	if (index >= VMFUNC_EPTP_ENTRIES)
		return 1;


	if (kvm_vcpu_read_guest_page(vcpu, vmcs12->eptp_list_address >> PAGE_SHIFT,
				     &new_eptp, index * 8, 8))
		return 1;

	accessed_dirty = !!(new_eptp & VMX_EPTP_AD_ENABLE_BIT);

	/*
	 * If the (L2) guest does a vmfunc to the currently
	 * active ept pointer, we don't have to do anything else
	 */
	if (vmcs12->ept_pointer != new_eptp) {
		if (!nested_vmx_check_eptp(vcpu, new_eptp))
			return 1;

		kvm_mmu_unload(vcpu);
		mmu->ept_ad = accessed_dirty;
		mmu->mmu_role.base.ad_disabled = !accessed_dirty;
		vmcs12->ept_pointer = new_eptp;
		/*
		 * TODO: Check what's the correct approach in case
		 * mmu reload fails. Currently, we just let the next
		 * reload potentially fail
		 */
		kvm_mmu_reload(vcpu);
	}

	return 0;
}

static int handle_vmfunc(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct vmcs12 *vmcs12;
	u32 function = kvm_rax_read(vcpu);

	/*
	 * VMFUNC is only supported for nested guests, but we always enable the
	 * secondary control for simplicity; for non-nested mode, fake that we
	 * didn't by injecting #UD.
	 */
	if (!is_guest_mode(vcpu)) {
		kvm_queue_exception(vcpu, UD_VECTOR);
		return 1;
	}

	vmcs12 = get_vmcs12(vcpu);
	if ((vmcs12->vm_function_control & (1 << function)) == 0)
		goto fail;

	switch (function) {
	case 0:
		if (nested_vmx_eptp_switching(vcpu, vmcs12))
			goto fail;
		break;
	default:
		goto fail;
	}
	return kvm_skip_emulated_instruction(vcpu);

fail:
	nested_vmx_vmexit(vcpu, vmx->exit_reason,
			  vmx_get_intr_info(vcpu),
			  vmx_get_exit_qual(vcpu));
	return 1;
}

/*
 * Return true if an IO instruction with the specified port and size should cause
 * a VM-exit into L1.
 */
bool nested_vmx_check_io_bitmaps(struct kvm_vcpu *vcpu, unsigned int port,
				 int size)
{
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
	gpa_t bitmap, last_bitmap;
	u8 b;

	last_bitmap = (gpa_t)-1;
	b = -1;

	while (size > 0) {
		if (port < 0x8000)
			bitmap = vmcs12->io_bitmap_a;
		else if (port < 0x10000)
			bitmap = vmcs12->io_bitmap_b;
		else
			return true;
		bitmap += (port & 0x7fff) / 8;

		if (last_bitmap != bitmap)
			if (kvm_vcpu_read_guest(vcpu, bitmap, &b, 1))
				return true;
		if (b & (1 << (port & 7)))
			return true;

		port++;
		size--;
		last_bitmap = bitmap;
	}

	return false;
}

static bool nested_vmx_exit_handled_io(struct kvm_vcpu *vcpu,
				       struct vmcs12 *vmcs12)
{
	unsigned long exit_qualification;
	unsigned short port;
	int size;

	if (!nested_cpu_has(vmcs12, CPU_BASED_USE_IO_BITMAPS))
		return nested_cpu_has(vmcs12, CPU_BASED_UNCOND_IO_EXITING);

	exit_qualification = vmx_get_exit_qual(vcpu);

	port = exit_qualification >> 16;
	size = (exit_qualification & 7) + 1;

	return nested_vmx_check_io_bitmaps(vcpu, port, size);
}

/*
 * Return 1 if we should exit from L2 to L1 to handle an MSR access,
 * rather than handle it ourselves in L0. I.e., check whether L1 expressed
 * disinterest in the current event (read or write a specific MSR) by using an
 * MSR bitmap. This may be the case even when L0 doesn't use MSR bitmaps.
 */
static bool nested_vmx_exit_handled_msr(struct kvm_vcpu *vcpu,
	struct vmcs12 *vmcs12, u32 exit_reason)
{
	u32 msr_index = kvm_rcx_read(vcpu);
	gpa_t bitmap;

	if (!nested_cpu_has(vmcs12, CPU_BASED_USE_MSR_BITMAPS))
		return true;

	/*
	 * The MSR_BITMAP page is divided into four 1024-byte bitmaps,
	 * for the four combinations of read/write and low/high MSR numbers.
	 * First we need to figure out which of the four to use:
	 */
	bitmap = vmcs12->msr_bitmap;
	if (exit_reason == EXIT_REASON_MSR_WRITE)
		bitmap += 2048;
	if (msr_index >= 0xc0000000) {
		msr_index -= 0xc0000000;
		bitmap += 1024;
	}

	/* Then read the msr_index'th bit from this bitmap: */
	if (msr_index < 1024*8) {
		unsigned char b;
		if (kvm_vcpu_read_guest(vcpu, bitmap + msr_index/8, &b, 1))
			return true;
		return 1 & (b >> (msr_index & 7));
	} else
		return true; /* let L1 handle the wrong parameter */
}

/*
 * Return 1 if we should exit from L2 to L1 to handle a CR access exit,
 * rather than handle it ourselves in L0. I.e., check if L1 wanted to
 * intercept (via guest_host_mask etc.) the current event.
 */
static bool nested_vmx_exit_handled_cr(struct kvm_vcpu *vcpu,
	struct vmcs12 *vmcs12)
{
	unsigned long exit_qualification = vmx_get_exit_qual(vcpu);
	int cr = exit_qualification & 15;
	int reg;
	unsigned long val;

	switch ((exit_qualification >> 4) & 3) {
	case 0: /* mov to cr */
		reg = (exit_qualification >> 8) & 15;
		val = kvm_register_readl(vcpu, reg);
		switch (cr) {
		case 0:
			if (vmcs12->cr0_guest_host_mask &
			    (val ^ vmcs12->cr0_read_shadow))
				return true;
			break;
		case 3:
			if (nested_cpu_has(vmcs12, CPU_BASED_CR3_LOAD_EXITING))
				return true;
			break;
		case 4:
			if (vmcs12->cr4_guest_host_mask &
			    (vmcs12->cr4_read_shadow ^ val))
				return true;
			break;
		case 8:
			if (nested_cpu_has(vmcs12, CPU_BASED_CR8_LOAD_EXITING))
				return true;
			break;
		}
		break;
	case 2: /* clts */
		if ((vmcs12->cr0_guest_host_mask & X86_CR0_TS) &&
		    (vmcs12->cr0_read_shadow & X86_CR0_TS))
			return true;
		break;
	case 1: /* mov from cr */
		switch (cr) {
		case 3:
			if (vmcs12->cpu_based_vm_exec_control &
			    CPU_BASED_CR3_STORE_EXITING)
				return true;
			break;
		case 8:
			if (vmcs12->cpu_based_vm_exec_control &
			    CPU_BASED_CR8_STORE_EXITING)
				return true;
			break;
		}
		break;
	case 3: /* lmsw */
		/*
		 * lmsw can change bits 1..3 of cr0, and only set bit 0 of
		 * cr0. Other attempted changes are ignored, with no exit.
		 */
		val = (exit_qualification >> LMSW_SOURCE_DATA_SHIFT) & 0x0f;
		if (vmcs12->cr0_guest_host_mask & 0xe &
		    (val ^ vmcs12->cr0_read_shadow))
			return true;
		if ((vmcs12->cr0_guest_host_mask & 0x1) &&
		    !(vmcs12->cr0_read_shadow & 0x1) &&
		    (val & 0x1))
			return true;
		break;
	}
	return false;
}

static bool nested_vmx_exit_handled_vmcs_access(struct kvm_vcpu *vcpu,
	struct vmcs12 *vmcs12, gpa_t bitmap)
{
	u32 vmx_instruction_info;
	unsigned long field;
	u8 b;

	if (!nested_cpu_has_shadow_vmcs(vmcs12))
		return true;

	/* Decode instruction info and find the field to access */
	vmx_instruction_info = vmcs_read32(VMX_INSTRUCTION_INFO);
	field = kvm_register_read(vcpu, (((vmx_instruction_info) >> 28) & 0xf));

	/* Out-of-range fields always cause a VM exit from L2 to L1 */
	if (field >> 15)
		return true;

	if (kvm_vcpu_read_guest(vcpu, bitmap + field/8, &b, 1))
		return true;

	return 1 & (b >> (field & 7));
}

static bool nested_vmx_exit_handled_mtf(struct vmcs12 *vmcs12)
{
	u32 entry_intr_info = vmcs12->vm_entry_intr_info_field;

	if (nested_cpu_has_mtf(vmcs12))
		return true;

	/*
	 * An MTF VM-exit may be injected into the guest by setting the
	 * interruption-type to 7 (other event) and the vector field to 0. Such
	 * is the case regardless of the 'monitor trap flag' VM-execution
	 * control.
	 */
	return entry_intr_info == (INTR_INFO_VALID_MASK
				   | INTR_TYPE_OTHER_EVENT);
}

/*
 * Return true if L0 wants to handle an exit from L2 regardless of whether or not
 * L1 wants the exit.  Only call this when in is_guest_mode (L2).
 */
static bool nested_vmx_l0_wants_exit(struct kvm_vcpu *vcpu, u32 exit_reason)
{
	u32 intr_info;

	switch ((u16)exit_reason) {
	case EXIT_REASON_EXCEPTION_NMI:
		intr_info = vmx_get_intr_info(vcpu);
		if (is_nmi(intr_info))
			return true;
		else if (is_page_fault(intr_info))
			return vcpu->arch.apf.host_apf_flags || !enable_ept;
		else if (is_debug(intr_info) &&
			 vcpu->guest_debug &
			 (KVM_GUESTDBG_SINGLESTEP | KVM_GUESTDBG_USE_HW_BP))
			return true;
		else if (is_breakpoint(intr_info) &&
			 vcpu->guest_debug & KVM_GUESTDBG_USE_SW_BP)
			return true;
		return false;
	case EXIT_REASON_EXTERNAL_INTERRUPT:
		return true;
	case EXIT_REASON_MCE_DURING_VMENTRY:
		return true;
	case EXIT_REASON_EPT_VIOLATION:
		/*
		 * L0 always deals with the EPT violation. If nested EPT is
		 * used, and the nested mmu code discovers that the address is
		 * missing in the guest EPT table (EPT12), the EPT violation
		 * will be injected with nested_ept_inject_page_fault()
		 */
		return true;
	case EXIT_REASON_EPT_MISCONFIG:
		/*
		 * L2 never uses directly L1's EPT, but rather L0's own EPT
		 * table (shadow on EPT) or a merged EPT table that L0 built
		 * (EPT on EPT). So any problems with the structure of the
		 * table is L0's fault.
		 */
		return true;
	case EXIT_REASON_PREEMPTION_TIMER:
		return true;
	case EXIT_REASON_PML_FULL:
		/* We emulate PML support to L1. */
		return true;
	case EXIT_REASON_VMFUNC:
		/* VM functions are emulated through L2->L0 vmexits. */
		return true;
	case EXIT_REASON_ENCLS:
		/* SGX is never exposed to L1 */
		return true;
	default:
		break;
	}
	return false;
}

/*
 * Return 1 if L1 wants to intercept an exit from L2.  Only call this when in
 * is_guest_mode (L2).
 */
static bool nested_vmx_l1_wants_exit(struct kvm_vcpu *vcpu, u32 exit_reason)
{
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
	u32 intr_info;

	switch ((u16)exit_reason) {
	case EXIT_REASON_EXCEPTION_NMI:
		intr_info = vmx_get_intr_info(vcpu);
		if (is_nmi(intr_info))
			return true;
		else if (is_page_fault(intr_info))
			return true;
		return vmcs12->exception_bitmap &
				(1u << (intr_info & INTR_INFO_VECTOR_MASK));
	case EXIT_REASON_EXTERNAL_INTERRUPT:
		return nested_exit_on_intr(vcpu);
	case EXIT_REASON_TRIPLE_FAULT:
		return true;
	case EXIT_REASON_INTERRUPT_WINDOW:
		return nested_cpu_has(vmcs12, CPU_BASED_INTR_WINDOW_EXITING);
	case EXIT_REASON_NMI_WINDOW:
		return nested_cpu_has(vmcs12, CPU_BASED_NMI_WINDOW_EXITING);
	case EXIT_REASON_TASK_SWITCH:
		return true;
	case EXIT_REASON_CPUID:
		return true;
	case EXIT_REASON_HLT:
		return nested_cpu_has(vmcs12, CPU_BASED_HLT_EXITING);
	case EXIT_REASON_INVD:
		return true;
	case EXIT_REASON_INVLPG:
		return nested_cpu_has(vmcs12, CPU_BASED_INVLPG_EXITING);
	case EXIT_REASON_RDPMC:
		return nested_cpu_has(vmcs12, CPU_BASED_RDPMC_EXITING);
	case EXIT_REASON_RDRAND:
		return nested_cpu_has2(vmcs12, SECONDARY_EXEC_RDRAND_EXITING);
	case EXIT_REASON_RDSEED:
		return nested_cpu_has2(vmcs12, SECONDARY_EXEC_RDSEED_EXITING);
	case EXIT_REASON_RDTSC: case EXIT_REASON_RDTSCP:
		return nested_cpu_has(vmcs12, CPU_BASED_RDTSC_EXITING);
	case EXIT_REASON_VMREAD:
		return nested_vmx_exit_handled_vmcs_access(vcpu, vmcs12,
			vmcs12->vmread_bitmap);
	case EXIT_REASON_VMWRITE:
		return nested_vmx_exit_handled_vmcs_access(vcpu, vmcs12,
			vmcs12->vmwrite_bitmap);
	case EXIT_REASON_VMCALL: case EXIT_REASON_VMCLEAR:
	case EXIT_REASON_VMLAUNCH: case EXIT_REASON_VMPTRLD:
	case EXIT_REASON_VMPTRST: case EXIT_REASON_VMRESUME:
	case EXIT_REASON_VMOFF: case EXIT_REASON_VMON:
	case EXIT_REASON_INVEPT: case EXIT_REASON_INVVPID:
		/*
		 * VMX instructions trap unconditionally. This allows L1 to
		 * emulate them for its L2 guest, i.e., allows 3-level nesting!
		 */
		return true;
	case EXIT_REASON_CR_ACCESS:
		return nested_vmx_exit_handled_cr(vcpu, vmcs12);
	case EXIT_REASON_DR_ACCESS:
		return nested_cpu_has(vmcs12, CPU_BASED_MOV_DR_EXITING);
	case EXIT_REASON_IO_INSTRUCTION:
		return nested_vmx_exit_handled_io(vcpu, vmcs12);
	case EXIT_REASON_GDTR_IDTR: case EXIT_REASON_LDTR_TR:
		return nested_cpu_has2(vmcs12, SECONDARY_EXEC_DESC);
	case EXIT_REASON_MSR_READ:
	case EXIT_REASON_MSR_WRITE:
		return nested_vmx_exit_handled_msr(vcpu, vmcs12, exit_reason);
	case EXIT_REASON_INVALID_STATE:
		return true;
	case EXIT_REASON_MWAIT_INSTRUCTION:
		return nested_cpu_has(vmcs12, CPU_BASED_MWAIT_EXITING);
	case EXIT_REASON_MONITOR_TRAP_FLAG:
		return nested_vmx_exit_handled_mtf(vmcs12);
	case EXIT_REASON_MONITOR_INSTRUCTION:
		return nested_cpu_has(vmcs12, CPU_BASED_MONITOR_EXITING);
	case EXIT_REASON_PAUSE_INSTRUCTION:
		return nested_cpu_has(vmcs12, CPU_BASED_PAUSE_EXITING) ||
			nested_cpu_has2(vmcs12,
				SECONDARY_EXEC_PAUSE_LOOP_EXITING);
	case EXIT_REASON_MCE_DURING_VMENTRY:
		return true;
	case EXIT_REASON_TPR_BELOW_THRESHOLD:
		return nested_cpu_has(vmcs12, CPU_BASED_TPR_SHADOW);
	case EXIT_REASON_APIC_ACCESS:
	case EXIT_REASON_APIC_WRITE:
	case EXIT_REASON_EOI_INDUCED:
		/*
		 * The controls for "virtualize APIC accesses," "APIC-
		 * register virtualization," and "virtual-interrupt
		 * delivery" only come from vmcs12.
		 */
		return true;
	case EXIT_REASON_INVPCID:
		return
			nested_cpu_has2(vmcs12, SECONDARY_EXEC_ENABLE_INVPCID) &&
			nested_cpu_has(vmcs12, CPU_BASED_INVLPG_EXITING);
	case EXIT_REASON_WBINVD:
		return nested_cpu_has2(vmcs12, SECONDARY_EXEC_WBINVD_EXITING);
	case EXIT_REASON_XSETBV:
		return true;
	case EXIT_REASON_XSAVES: case EXIT_REASON_XRSTORS:
		/*
		 * This should never happen, since it is not possible to
		 * set XSS to a non-zero value---neither in L1 nor in L2.
		 * If if it were, XSS would have to be checked against
		 * the XSS exit bitmap in vmcs12.
		 */
		return nested_cpu_has2(vmcs12, SECONDARY_EXEC_XSAVES);
	case EXIT_REASON_UMWAIT:
	case EXIT_REASON_TPAUSE:
		return nested_cpu_has2(vmcs12,
			SECONDARY_EXEC_ENABLE_USR_WAIT_PAUSE);
	default:
		return true;
	}
}

/*
 * Conditionally reflect a VM-Exit into L1.  Returns %true if the VM-Exit was
 * reflected into L1.
 */
bool nested_vmx_reflect_vmexit(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	u32 exit_reason = vmx->exit_reason;
	unsigned long exit_qual;
	u32 exit_intr_info;

	WARN_ON_ONCE(vmx->nested.nested_run_pending);

	/*
	 * Late nested VM-Fail shares the same flow as nested VM-Exit since KVM
	 * has already loaded L2's state.
	 */
	if (unlikely(vmx->fail)) {
		trace_kvm_nested_vmenter_failed(
			"hardware VM-instruction error: ",
			vmcs_read32(VM_INSTRUCTION_ERROR));
		exit_intr_info = 0;
		exit_qual = 0;
		goto reflect_vmexit;
	}

	exit_intr_info = vmx_get_intr_info(vcpu);
	exit_qual = vmx_get_exit_qual(vcpu);

	trace_kvm_nested_vmexit(kvm_rip_read(vcpu), exit_reason, exit_qual,
				vmx->idt_vectoring_info, exit_intr_info,
				vmcs_read32(VM_EXIT_INTR_ERROR_CODE),
				KVM_ISA_VMX);

	/* If L0 (KVM) wants the exit, it trumps L1's desires. */
	if (nested_vmx_l0_wants_exit(vcpu, exit_reason))
		return false;

	/* If L1 doesn't want the exit, handle it in L0. */
	if (!nested_vmx_l1_wants_exit(vcpu, exit_reason))
		return false;

	/*
	 * vmcs.VM_EXIT_INTR_INFO is only valid for EXCEPTION_NMI exits.  For
	 * EXTERNAL_INTERRUPT, the value for vmcs12->vm_exit_intr_info would
	 * need to be synthesized by querying the in-kernel LAPIC, but external
	 * interrupts are never reflected to L1 so it's a non-issue.
	 */
	if ((exit_intr_info &
	     (INTR_INFO_VALID_MASK | INTR_INFO_DELIVER_CODE_MASK)) ==
	    (INTR_INFO_VALID_MASK | INTR_INFO_DELIVER_CODE_MASK)) {
		struct vmcs12 *vmcs12 = get_vmcs12(vcpu);

		vmcs12->vm_exit_intr_error_code =
			vmcs_read32(VM_EXIT_INTR_ERROR_CODE);
	}

reflect_vmexit:
	nested_vmx_vmexit(vcpu, exit_reason, exit_intr_info, exit_qual);
	return true;
}

static int vmx_get_nested_state(struct kvm_vcpu *vcpu,
				struct kvm_nested_state __user *user_kvm_nested_state,
				u32 user_data_size)
{
	struct vcpu_vmx *vmx;
	struct vmcs12 *vmcs12;
	struct kvm_nested_state kvm_state = {
		.flags = 0,
		.format = KVM_STATE_NESTED_FORMAT_VMX,
		.size = sizeof(kvm_state),
		.hdr.vmx.flags = 0,
		.hdr.vmx.vmxon_pa = -1ull,
		.hdr.vmx.vmcs12_pa = -1ull,
		.hdr.vmx.preemption_timer_deadline = 0,
	};
	struct kvm_vmx_nested_state_data __user *user_vmx_nested_state =
		&user_kvm_nested_state->data.vmx[0];

	if (!vcpu)
		return kvm_state.size + sizeof(*user_vmx_nested_state);

	vmx = to_vmx(vcpu);
	vmcs12 = get_vmcs12(vcpu);

	if (nested_vmx_allowed(vcpu) &&
	    (vmx->nested.vmxon || vmx->nested.smm.vmxon)) {
		kvm_state.hdr.vmx.vmxon_pa = vmx->nested.vmxon_ptr;
		kvm_state.hdr.vmx.vmcs12_pa = vmx->nested.current_vmptr;

		if (vmx_has_valid_vmcs12(vcpu)) {
			kvm_state.size += sizeof(user_vmx_nested_state->vmcs12);

			if (vmx->nested.hv_evmcs)
				kvm_state.flags |= KVM_STATE_NESTED_EVMCS;

			if (is_guest_mode(vcpu) &&
			    nested_cpu_has_shadow_vmcs(vmcs12) &&
			    vmcs12->vmcs_link_pointer != -1ull)
				kvm_state.size += sizeof(user_vmx_nested_state->shadow_vmcs12);
		}

		if (vmx->nested.smm.vmxon)
			kvm_state.hdr.vmx.smm.flags |= KVM_STATE_NESTED_SMM_VMXON;

		if (vmx->nested.smm.guest_mode)
			kvm_state.hdr.vmx.smm.flags |= KVM_STATE_NESTED_SMM_GUEST_MODE;

		if (is_guest_mode(vcpu)) {
			kvm_state.flags |= KVM_STATE_NESTED_GUEST_MODE;

			if (vmx->nested.nested_run_pending)
				kvm_state.flags |= KVM_STATE_NESTED_RUN_PENDING;

			if (vmx->nested.mtf_pending)
				kvm_state.flags |= KVM_STATE_NESTED_MTF_PENDING;

			if (nested_cpu_has_preemption_timer(vmcs12) &&
			    vmx->nested.has_preemption_timer_deadline) {
				kvm_state.hdr.vmx.flags |=
					KVM_STATE_VMX_PREEMPTION_TIMER_DEADLINE;
				kvm_state.hdr.vmx.preemption_timer_deadline =
					vmx->nested.preemption_timer_deadline;
			}
		}
	}

	if (user_data_size < kvm_state.size)
		goto out;

	if (copy_to_user(user_kvm_nested_state, &kvm_state, sizeof(kvm_state)))
		return -EFAULT;

	if (!vmx_has_valid_vmcs12(vcpu))
		goto out;

	/*
	 * When running L2, the authoritative vmcs12 state is in the
	 * vmcs02. When running L1, the authoritative vmcs12 state is
	 * in the shadow or enlightened vmcs linked to vmcs01, unless
	 * need_vmcs12_to_shadow_sync is set, in which case, the authoritative
	 * vmcs12 state is in the vmcs12 already.
	 */
	if (is_guest_mode(vcpu)) {
		sync_vmcs02_to_vmcs12(vcpu, vmcs12);
		sync_vmcs02_to_vmcs12_rare(vcpu, vmcs12);
	} else if (!vmx->nested.need_vmcs12_to_shadow_sync) {
		if (vmx->nested.hv_evmcs)
			copy_enlightened_to_vmcs12(vmx);
		else if (enable_shadow_vmcs)
			copy_shadow_to_vmcs12(vmx);
	}

	BUILD_BUG_ON(sizeof(user_vmx_nested_state->vmcs12) < VMCS12_SIZE);
	BUILD_BUG_ON(sizeof(user_vmx_nested_state->shadow_vmcs12) < VMCS12_SIZE);

	/*
	 * Copy over the full allocated size of vmcs12 rather than just the size
	 * of the struct.
	 */
	if (copy_to_user(user_vmx_nested_state->vmcs12, vmcs12, VMCS12_SIZE))
		return -EFAULT;

	if (nested_cpu_has_shadow_vmcs(vmcs12) &&
	    vmcs12->vmcs_link_pointer != -1ull) {
		if (copy_to_user(user_vmx_nested_state->shadow_vmcs12,
				 get_shadow_vmcs12(vcpu), VMCS12_SIZE))
			return -EFAULT;
	}
out:
	return kvm_state.size;
}

/*
 * Forcibly leave nested mode in order to be able to reset the VCPU later on.
 */
void vmx_leave_nested(struct kvm_vcpu *vcpu)
{
	if (is_guest_mode(vcpu)) {
		to_vmx(vcpu)->nested.nested_run_pending = 0;
		nested_vmx_vmexit(vcpu, -1, 0, 0);
	}
	free_nested(vcpu);
}

static int vmx_set_nested_state(struct kvm_vcpu *vcpu,
				struct kvm_nested_state __user *user_kvm_nested_state,
				struct kvm_nested_state *kvm_state)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct vmcs12 *vmcs12;
	enum vm_entry_failure_code ignored;
	struct kvm_vmx_nested_state_data __user *user_vmx_nested_state =
		&user_kvm_nested_state->data.vmx[0];
	int ret;

	if (kvm_state->format != KVM_STATE_NESTED_FORMAT_VMX)
		return -EINVAL;

	if (kvm_state->hdr.vmx.vmxon_pa == -1ull) {
		if (kvm_state->hdr.vmx.smm.flags)
			return -EINVAL;

		if (kvm_state->hdr.vmx.vmcs12_pa != -1ull)
			return -EINVAL;

		/*
		 * KVM_STATE_NESTED_EVMCS used to signal that KVM should
		 * enable eVMCS capability on vCPU. However, since then
		 * code was changed such that flag signals vmcs12 should
		 * be copied into eVMCS in guest memory.
		 *
		 * To preserve backwards compatability, allow user
		 * to set this flag even when there is no VMXON region.
		 */
		if (kvm_state->flags & ~KVM_STATE_NESTED_EVMCS)
			return -EINVAL;
	} else {
		if (!nested_vmx_allowed(vcpu))
			return -EINVAL;

		if (!page_address_valid(vcpu, kvm_state->hdr.vmx.vmxon_pa))
			return -EINVAL;
	}

	if ((kvm_state->hdr.vmx.smm.flags & KVM_STATE_NESTED_SMM_GUEST_MODE) &&
	    (kvm_state->flags & KVM_STATE_NESTED_GUEST_MODE))
		return -EINVAL;

	if (kvm_state->hdr.vmx.smm.flags &
	    ~(KVM_STATE_NESTED_SMM_GUEST_MODE | KVM_STATE_NESTED_SMM_VMXON))
		return -EINVAL;

	if (kvm_state->hdr.vmx.flags & ~KVM_STATE_VMX_PREEMPTION_TIMER_DEADLINE)
		return -EINVAL;

	/*
	 * SMM temporarily disables VMX, so we cannot be in guest mode,
	 * nor can VMLAUNCH/VMRESUME be pending.  Outside SMM, SMM flags
	 * must be zero.
	 */
	if (is_smm(vcpu) ?
		(kvm_state->flags &
		 (KVM_STATE_NESTED_GUEST_MODE | KVM_STATE_NESTED_RUN_PENDING))
		: kvm_state->hdr.vmx.smm.flags)
		return -EINVAL;

	if ((kvm_state->hdr.vmx.smm.flags & KVM_STATE_NESTED_SMM_GUEST_MODE) &&
	    !(kvm_state->hdr.vmx.smm.flags & KVM_STATE_NESTED_SMM_VMXON))
		return -EINVAL;

	if ((kvm_state->flags & KVM_STATE_NESTED_EVMCS) &&
		(!nested_vmx_allowed(vcpu) || !vmx->nested.enlightened_vmcs_enabled))
			return -EINVAL;

	vmx_leave_nested(vcpu);

	if (kvm_state->hdr.vmx.vmxon_pa == -1ull)
		return 0;

	vmx->nested.vmxon_ptr = kvm_state->hdr.vmx.vmxon_pa;
	ret = enter_vmx_operation(vcpu);
	if (ret)
		return ret;

	/* Empty 'VMXON' state is permitted if no VMCS loaded */
	if (kvm_state->size < sizeof(*kvm_state) + sizeof(*vmcs12)) {
		/* See vmx_has_valid_vmcs12.  */
		if ((kvm_state->flags & KVM_STATE_NESTED_GUEST_MODE) ||
		    (kvm_state->flags & KVM_STATE_NESTED_EVMCS) ||
		    (kvm_state->hdr.vmx.vmcs12_pa != -1ull))
			return -EINVAL;
		else
			return 0;
	}

	if (kvm_state->hdr.vmx.vmcs12_pa != -1ull) {
		if (kvm_state->hdr.vmx.vmcs12_pa == kvm_state->hdr.vmx.vmxon_pa ||
		    !page_address_valid(vcpu, kvm_state->hdr.vmx.vmcs12_pa))
			return -EINVAL;

		set_current_vmptr(vmx, kvm_state->hdr.vmx.vmcs12_pa);
	} else if (kvm_state->flags & KVM_STATE_NESTED_EVMCS) {
		/*
		 * nested_vmx_handle_enlightened_vmptrld() cannot be called
		 * directly from here as HV_X64_MSR_VP_ASSIST_PAGE may not be
		 * restored yet. EVMCS will be mapped from
		 * nested_get_vmcs12_pages().
		 */
		kvm_make_request(KVM_REQ_GET_VMCS12_PAGES, vcpu);
	} else {
		return -EINVAL;
	}

	if (kvm_state->hdr.vmx.smm.flags & KVM_STATE_NESTED_SMM_VMXON) {
		vmx->nested.smm.vmxon = true;
		vmx->nested.vmxon = false;

		if (kvm_state->hdr.vmx.smm.flags & KVM_STATE_NESTED_SMM_GUEST_MODE)
			vmx->nested.smm.guest_mode = true;
	}

	vmcs12 = get_vmcs12(vcpu);
	if (copy_from_user(vmcs12, user_vmx_nested_state->vmcs12, sizeof(*vmcs12)))
		return -EFAULT;

	if (vmcs12->hdr.revision_id != VMCS12_REVISION)
		return -EINVAL;

	if (!(kvm_state->flags & KVM_STATE_NESTED_GUEST_MODE))
		return 0;

	vmx->nested.nested_run_pending =
		!!(kvm_state->flags & KVM_STATE_NESTED_RUN_PENDING);

	vmx->nested.mtf_pending =
		!!(kvm_state->flags & KVM_STATE_NESTED_MTF_PENDING);

	ret = -EINVAL;
	if (nested_cpu_has_shadow_vmcs(vmcs12) &&
	    vmcs12->vmcs_link_pointer != -1ull) {
		struct vmcs12 *shadow_vmcs12 = get_shadow_vmcs12(vcpu);

		if (kvm_state->size <
		    sizeof(*kvm_state) +
		    sizeof(user_vmx_nested_state->vmcs12) + sizeof(*shadow_vmcs12))
			goto error_guest_mode;

		if (copy_from_user(shadow_vmcs12,
				   user_vmx_nested_state->shadow_vmcs12,
				   sizeof(*shadow_vmcs12))) {
			ret = -EFAULT;
			goto error_guest_mode;
		}

		if (shadow_vmcs12->hdr.revision_id != VMCS12_REVISION ||
		    !shadow_vmcs12->hdr.shadow_vmcs)
			goto error_guest_mode;
	}

	vmx->nested.has_preemption_timer_deadline = false;
	if (kvm_state->hdr.vmx.flags & KVM_STATE_VMX_PREEMPTION_TIMER_DEADLINE) {
		vmx->nested.has_preemption_timer_deadline = true;
		vmx->nested.preemption_timer_deadline =
			kvm_state->hdr.vmx.preemption_timer_deadline;
	}

	if (nested_vmx_check_controls(vcpu, vmcs12) ||
	    nested_vmx_check_host_state(vcpu, vmcs12) ||
	    nested_vmx_check_guest_state(vcpu, vmcs12, &ignored))
		goto error_guest_mode;

	vmx->nested.dirty_vmcs12 = true;
	ret = nested_vmx_enter_non_root_mode(vcpu, false);
	if (ret)
		goto error_guest_mode;

	return 0;

error_guest_mode:
	vmx->nested.nested_run_pending = 0;
	return ret;
}

void nested_vmx_set_vmcs_shadowing_bitmap(void)
{
	if (enable_shadow_vmcs) {
		vmcs_write64(VMREAD_BITMAP, __pa(vmx_vmread_bitmap));
		vmcs_write64(VMWRITE_BITMAP, __pa(vmx_vmwrite_bitmap));
	}
}

/*
 * nested_vmx_setup_ctls_msrs() sets up variables containing the values to be
 * returned for the various VMX controls MSRs when nested VMX is enabled.
 * The same values should also be used to verify that vmcs12 control fields are
 * valid during nested entry from L1 to L2.
 * Each of these control msrs has a low and high 32-bit half: A low bit is on
 * if the corresponding bit in the (32-bit) control field *must* be on, and a
 * bit in the high half is on if the corresponding bit in the control field
 * may be on. See also vmx_control_verify().
 */
void nested_vmx_setup_ctls_msrs(struct nested_vmx_msrs *msrs, u32 ept_caps)
{
	/*
	 * Note that as a general rule, the high half of the MSRs (bits in
	 * the control fields which may be 1) should be initialized by the
	 * intersection of the underlying hardware's MSR (i.e., features which
	 * can be supported) and the list of features we want to expose -
	 * because they are known to be properly supported in our code.
	 * Also, usually, the low half of the MSRs (bits which must be 1) can
	 * be set to 0, meaning that L1 may turn off any of these bits. The
	 * reason is that if one of these bits is necessary, it will appear
	 * in vmcs01 and prepare_vmcs02, when it bitwise-or's the control
	 * fields of vmcs01 and vmcs02, will turn these bits off - and
	 * nested_vmx_l1_wants_exit() will not pass related exits to L1.
	 * These rules have exceptions below.
	 */

	/* pin-based controls */
	rdmsr(MSR_IA32_VMX_PINBASED_CTLS,
		msrs->pinbased_ctls_low,
		msrs->pinbased_ctls_high);
	msrs->pinbased_ctls_low |=
		PIN_BASED_ALWAYSON_WITHOUT_TRUE_MSR;
	msrs->pinbased_ctls_high &=
		PIN_BASED_EXT_INTR_MASK |
		PIN_BASED_NMI_EXITING |
		PIN_BASED_VIRTUAL_NMIS |
		(enable_apicv ? PIN_BASED_POSTED_INTR : 0);
	msrs->pinbased_ctls_high |=
		PIN_BASED_ALWAYSON_WITHOUT_TRUE_MSR |
		PIN_BASED_VMX_PREEMPTION_TIMER;

	/* exit controls */
	rdmsr(MSR_IA32_VMX_EXIT_CTLS,
		msrs->exit_ctls_low,
		msrs->exit_ctls_high);
	msrs->exit_ctls_low =
		VM_EXIT_ALWAYSON_WITHOUT_TRUE_MSR;

	msrs->exit_ctls_high &=
#ifdef CONFIG_X86_64
		VM_EXIT_HOST_ADDR_SPACE_SIZE |
#endif
		VM_EXIT_LOAD_IA32_PAT | VM_EXIT_SAVE_IA32_PAT |
		VM_EXIT_CLEAR_BNDCFGS | VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL;
	msrs->exit_ctls_high |=
		VM_EXIT_ALWAYSON_WITHOUT_TRUE_MSR |
		VM_EXIT_LOAD_IA32_EFER | VM_EXIT_SAVE_IA32_EFER |
		VM_EXIT_SAVE_VMX_PREEMPTION_TIMER | VM_EXIT_ACK_INTR_ON_EXIT;

	/* We support free control of debug control saving. */
	msrs->exit_ctls_low &= ~VM_EXIT_SAVE_DEBUG_CONTROLS;

	/* entry controls */
	rdmsr(MSR_IA32_VMX_ENTRY_CTLS,
		msrs->entry_ctls_low,
		msrs->entry_ctls_high);
	msrs->entry_ctls_low =
		VM_ENTRY_ALWAYSON_WITHOUT_TRUE_MSR;
	msrs->entry_ctls_high &=
#ifdef CONFIG_X86_64
		VM_ENTRY_IA32E_MODE |
#endif
		VM_ENTRY_LOAD_IA32_PAT | VM_ENTRY_LOAD_BNDCFGS |
		VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL;
	msrs->entry_ctls_high |=
		(VM_ENTRY_ALWAYSON_WITHOUT_TRUE_MSR | VM_ENTRY_LOAD_IA32_EFER);

	/* We support free control of debug control loading. */
	msrs->entry_ctls_low &= ~VM_ENTRY_LOAD_DEBUG_CONTROLS;

	/* cpu-based controls */
	rdmsr(MSR_IA32_VMX_PROCBASED_CTLS,
		msrs->procbased_ctls_low,
		msrs->procbased_ctls_high);
	msrs->procbased_ctls_low =
		CPU_BASED_ALWAYSON_WITHOUT_TRUE_MSR;
	msrs->procbased_ctls_high &=
		CPU_BASED_INTR_WINDOW_EXITING |
		CPU_BASED_NMI_WINDOW_EXITING | CPU_BASED_USE_TSC_OFFSETTING |
		CPU_BASED_HLT_EXITING | CPU_BASED_INVLPG_EXITING |
		CPU_BASED_MWAIT_EXITING | CPU_BASED_CR3_LOAD_EXITING |
		CPU_BASED_CR3_STORE_EXITING |
#ifdef CONFIG_X86_64
		CPU_BASED_CR8_LOAD_EXITING | CPU_BASED_CR8_STORE_EXITING |
#endif
		CPU_BASED_MOV_DR_EXITING | CPU_BASED_UNCOND_IO_EXITING |
		CPU_BASED_USE_IO_BITMAPS | CPU_BASED_MONITOR_TRAP_FLAG |
		CPU_BASED_MONITOR_EXITING | CPU_BASED_RDPMC_EXITING |
		CPU_BASED_RDTSC_EXITING | CPU_BASED_PAUSE_EXITING |
		CPU_BASED_TPR_SHADOW | CPU_BASED_ACTIVATE_SECONDARY_CONTROLS;
	/*
	 * We can allow some features even when not supported by the
	 * hardware. For example, L1 can specify an MSR bitmap - and we
	 * can use it to avoid exits to L1 - even when L0 runs L2
	 * without MSR bitmaps.
	 */
	msrs->procbased_ctls_high |=
		CPU_BASED_ALWAYSON_WITHOUT_TRUE_MSR |
		CPU_BASED_USE_MSR_BITMAPS;

	/* We support free control of CR3 access interception. */
	msrs->procbased_ctls_low &=
		~(CPU_BASED_CR3_LOAD_EXITING | CPU_BASED_CR3_STORE_EXITING);

	/*
	 * secondary cpu-based controls.  Do not include those that
	 * depend on CPUID bits, they are added later by
	 * vmx_vcpu_after_set_cpuid.
	 */
	if (msrs->procbased_ctls_high & CPU_BASED_ACTIVATE_SECONDARY_CONTROLS)
		rdmsr(MSR_IA32_VMX_PROCBASED_CTLS2,
		      msrs->secondary_ctls_low,
		      msrs->secondary_ctls_high);

	msrs->secondary_ctls_low = 0;
	msrs->secondary_ctls_high &=
		SECONDARY_EXEC_DESC |
		SECONDARY_EXEC_RDTSCP |
		SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
		SECONDARY_EXEC_WBINVD_EXITING |
		SECONDARY_EXEC_APIC_REGISTER_VIRT |
		SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
		SECONDARY_EXEC_RDRAND_EXITING |
		SECONDARY_EXEC_ENABLE_INVPCID |
		SECONDARY_EXEC_RDSEED_EXITING |
		SECONDARY_EXEC_XSAVES;

	/*
	 * We can emulate "VMCS shadowing," even if the hardware
	 * doesn't support it.
	 */
	msrs->secondary_ctls_high |=
		SECONDARY_EXEC_SHADOW_VMCS;

	if (enable_ept) {
		/* nested EPT: emulate EPT also to L1 */
		msrs->secondary_ctls_high |=
			SECONDARY_EXEC_ENABLE_EPT;
		msrs->ept_caps =
			VMX_EPT_PAGE_WALK_4_BIT |
			VMX_EPT_PAGE_WALK_5_BIT |
			VMX_EPTP_WB_BIT |
			VMX_EPT_INVEPT_BIT |
			VMX_EPT_EXECUTE_ONLY_BIT;

		msrs->ept_caps &= ept_caps;
		msrs->ept_caps |= VMX_EPT_EXTENT_GLOBAL_BIT |
			VMX_EPT_EXTENT_CONTEXT_BIT | VMX_EPT_2MB_PAGE_BIT |
			VMX_EPT_1GB_PAGE_BIT;
		if (enable_ept_ad_bits) {
			msrs->secondary_ctls_high |=
				SECONDARY_EXEC_ENABLE_PML;
			msrs->ept_caps |= VMX_EPT_AD_BIT;
		}
	}

	if (cpu_has_vmx_vmfunc()) {
		msrs->secondary_ctls_high |=
			SECONDARY_EXEC_ENABLE_VMFUNC;
		/*
		 * Advertise EPTP switching unconditionally
		 * since we emulate it
		 */
		if (enable_ept)
			msrs->vmfunc_controls =
				VMX_VMFUNC_EPTP_SWITCHING;
	}

	/*
	 * Old versions of KVM use the single-context version without
	 * checking for support, so declare that it is supported even
	 * though it is treated as global context.  The alternative is
	 * not failing the single-context invvpid, and it is worse.
	 */
	if (enable_vpid) {
		msrs->secondary_ctls_high |=
			SECONDARY_EXEC_ENABLE_VPID;
		msrs->vpid_caps = VMX_VPID_INVVPID_BIT |
			VMX_VPID_EXTENT_SUPPORTED_MASK;
	}

	if (enable_unrestricted_guest)
		msrs->secondary_ctls_high |=
			SECONDARY_EXEC_UNRESTRICTED_GUEST;

	if (flexpriority_enabled)
		msrs->secondary_ctls_high |=
			SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES;

	/* miscellaneous data */
	rdmsr(MSR_IA32_VMX_MISC,
		msrs->misc_low,
		msrs->misc_high);
	msrs->misc_low &= VMX_MISC_SAVE_EFER_LMA;
	msrs->misc_low |=
		MSR_IA32_VMX_MISC_VMWRITE_SHADOW_RO_FIELDS |
		VMX_MISC_EMULATED_PREEMPTION_TIMER_RATE |
		VMX_MISC_ACTIVITY_HLT;
	msrs->misc_high = 0;

	/*
	 * This MSR reports some information about VMX support. We
	 * should return information about the VMX we emulate for the
	 * guest, and the VMCS structure we give it - not about the
	 * VMX support of the underlying hardware.
	 */
	msrs->basic =
		VMCS12_REVISION |
		VMX_BASIC_TRUE_CTLS |
		((u64)VMCS12_SIZE << VMX_BASIC_VMCS_SIZE_SHIFT) |
		(VMX_BASIC_MEM_TYPE_WB << VMX_BASIC_MEM_TYPE_SHIFT);

	if (cpu_has_vmx_basic_inout())
		msrs->basic |= VMX_BASIC_INOUT;

	/*
	 * These MSRs specify bits which the guest must keep fixed on
	 * while L1 is in VMXON mode (in L1's root mode, or running an L2).
	 * We picked the standard core2 setting.
	 */
#define VMXON_CR0_ALWAYSON     (X86_CR0_PE | X86_CR0_PG | X86_CR0_NE)
#define VMXON_CR4_ALWAYSON     X86_CR4_VMXE
	msrs->cr0_fixed0 = VMXON_CR0_ALWAYSON;
	msrs->cr4_fixed0 = VMXON_CR4_ALWAYSON;

	/* These MSRs specify bits which the guest must keep fixed off. */
	rdmsrl(MSR_IA32_VMX_CR0_FIXED1, msrs->cr0_fixed1);
	rdmsrl(MSR_IA32_VMX_CR4_FIXED1, msrs->cr4_fixed1);

	/* highest index: VMX_PREEMPTION_TIMER_VALUE */
	msrs->vmcs_enum = VMCS12_MAX_FIELD_INDEX << 1;
}

void nested_vmx_hardware_unsetup(void)
{
	int i;

	if (enable_shadow_vmcs) {
		for (i = 0; i < VMX_BITMAP_NR; i++)
			free_page((unsigned long)vmx_bitmap[i]);
	}
}

__init int nested_vmx_hardware_setup(int (*exit_handlers[])(struct kvm_vcpu *))
{
	int i;

	if (!cpu_has_vmx_shadow_vmcs())
		enable_shadow_vmcs = 0;
	if (enable_shadow_vmcs) {
		for (i = 0; i < VMX_BITMAP_NR; i++) {
			/*
			 * The vmx_bitmap is not tied to a VM and so should
			 * not be charged to a memcg.
			 */
			vmx_bitmap[i] = (unsigned long *)
				__get_free_page(GFP_KERNEL);
			if (!vmx_bitmap[i]) {
				nested_vmx_hardware_unsetup();
				return -ENOMEM;
			}
		}

		init_vmcs_shadow_fields();
	}

	exit_handlers[EXIT_REASON_VMCLEAR]	= handle_vmclear;
	exit_handlers[EXIT_REASON_VMLAUNCH]	= handle_vmlaunch;
	exit_handlers[EXIT_REASON_VMPTRLD]	= handle_vmptrld;
	exit_handlers[EXIT_REASON_VMPTRST]	= handle_vmptrst;
	exit_handlers[EXIT_REASON_VMREAD]	= handle_vmread;
	exit_handlers[EXIT_REASON_VMRESUME]	= handle_vmresume;
	exit_handlers[EXIT_REASON_VMWRITE]	= handle_vmwrite;
	exit_handlers[EXIT_REASON_VMOFF]	= handle_vmoff;
	exit_handlers[EXIT_REASON_VMON]		= handle_vmon;
	exit_handlers[EXIT_REASON_INVEPT]	= handle_invept;
	exit_handlers[EXIT_REASON_INVVPID]	= handle_invvpid;
	exit_handlers[EXIT_REASON_VMFUNC]	= handle_vmfunc;

	return 0;
}

struct kvm_x86_nested_ops vmx_nested_ops = {
	.check_events = vmx_check_nested_events,
	.hv_timer_pending = nested_vmx_preemption_timer_pending,
	.get_state = vmx_get_nested_state,
	.set_state = vmx_set_nested_state,
	.get_vmcs12_pages = nested_get_vmcs12_pages,
	.write_log_dirty = nested_vmx_write_pml_buffer,
	.enable_evmcs = nested_enable_evmcs,
	.get_evmcs_version = nested_get_evmcs_version,
};
