// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Google LLC
 * Author: Fuad Tabba <tabba@google.com>
 */

#include <linux/kvm_host.h>
#include <linux/mm.h>

#include <kvm/arm_hypercalls.h>
#include <kvm/arm_psci.h>

#include <asm/kvm_emulate.h>

#include <nvhe/arm-smccc.h>
#include <nvhe/mem_protect.h>
#include <nvhe/memory.h>
#include <nvhe/mm.h>
#include <nvhe/pkvm.h>
#include <nvhe/trap_handler.h>

/* Used by icache_is_vpipt(). */
unsigned long __icache_flags;

/* Used by kvm_get_vttbr(). */
unsigned int kvm_arm_vmid_bits;

unsigned int kvm_host_sve_max_vl;

/*
 * The currently loaded hyp vCPU for each physical CPU. Used only when
 * protected KVM is enabled, but for both protected and non-protected VMs.
 */
static DEFINE_PER_CPU(struct pkvm_hyp_vcpu *, loaded_hyp_vcpu);

/*
 * Host fp state for all cpus. This could include the host simd state, as well
 * as the sve and sme states if supported. Written to when the guest accesses
 * its own FPSIMD state, and read when the guest state is live and we need to
 * switch back to the host.
 *
 * Only valid when (fp_state == FP_STATE_GUEST_OWNED) in the hyp vCPU structure.
 */
void *host_fp_state;

static void *__get_host_fpsimd_bytes(void)
{
	void *state = host_fp_state +
		      size_mul(pkvm_host_fp_state_size(), hyp_smp_processor_id());

	if (state < host_fp_state)
		return NULL;

	return state;
}

struct user_fpsimd_state *get_host_fpsimd_state(struct kvm_vcpu *vcpu)
{
	if (likely(!is_protected_kvm_enabled()))
		return vcpu->arch.host_fpsimd_state;

	WARN_ON(system_supports_sve());
	return __get_host_fpsimd_bytes();
}

struct kvm_host_sve_state *get_host_sve_state(struct kvm_vcpu *vcpu)
{
	WARN_ON(!system_supports_sve());
	WARN_ON(!is_protected_kvm_enabled());
	return __get_host_fpsimd_bytes();
}

/*
 * Set trap register values based on features in ID_AA64PFR0.
 */
static void pvm_init_traps_aa64pfr0(struct kvm_vcpu *vcpu)
{
	const u64 feature_ids = pvm_read_id_reg(vcpu, SYS_ID_AA64PFR0_EL1);
	u64 hcr_set = HCR_RW;
	u64 hcr_clear = 0;
	u64 cptr_set = 0;

	/* Protected KVM does not support AArch32 guests. */
	BUILD_BUG_ON(FIELD_GET(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_EL0),
		PVM_ID_AA64PFR0_RESTRICT_UNSIGNED) != ID_AA64PFR0_EL1_ELx_64BIT_ONLY);
	BUILD_BUG_ON(FIELD_GET(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_EL1),
		PVM_ID_AA64PFR0_RESTRICT_UNSIGNED) != ID_AA64PFR0_EL1_ELx_64BIT_ONLY);

	/*
	 * Linux guests assume support for floating-point and Advanced SIMD. Do
	 * not change the trapping behavior for these from the KVM default.
	 */
	BUILD_BUG_ON(!FIELD_GET(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_FP),
				PVM_ID_AA64PFR0_ALLOW));
	BUILD_BUG_ON(!FIELD_GET(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_AdvSIMD),
				PVM_ID_AA64PFR0_ALLOW));

	/* Trap RAS unless all current versions are supported */
	if (FIELD_GET(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_RAS), feature_ids) <
	    ID_AA64PFR0_EL1_RAS_V1P1) {
		hcr_set |= HCR_TERR | HCR_TEA;
		hcr_clear |= HCR_FIEN;
	}

	/* Trap AMU */
	if (!FIELD_GET(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_AMU), feature_ids)) {
		hcr_clear |= HCR_AMVOFFEN;
		cptr_set |= CPTR_EL2_TAM;
	}

	/* Trap SVE */
	if (!FIELD_GET(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_SVE), feature_ids))
		cptr_set |= CPTR_EL2_TZ;

	vcpu->arch.hcr_el2 |= hcr_set;
	vcpu->arch.hcr_el2 &= ~hcr_clear;
	vcpu->arch.cptr_el2 |= cptr_set;
}

/*
 * Set trap register values based on features in ID_AA64PFR1.
 */
static void pvm_init_traps_aa64pfr1(struct kvm_vcpu *vcpu)
{
	const u64 feature_ids = pvm_read_id_reg(vcpu, SYS_ID_AA64PFR1_EL1);
	u64 hcr_set = 0;
	u64 hcr_clear = 0;

	/* Memory Tagging: Trap and Treat as Untagged if not supported. */
	if (!FIELD_GET(ARM64_FEATURE_MASK(ID_AA64PFR1_EL1_MTE), feature_ids)) {
		hcr_set |= HCR_TID5;
		hcr_clear |= HCR_DCT | HCR_ATA;
	}

	vcpu->arch.hcr_el2 |= hcr_set;
	vcpu->arch.hcr_el2 &= ~hcr_clear;
}

/*
 * Set trap register values based on features in ID_AA64DFR0.
 */
static void pvm_init_traps_aa64dfr0(struct kvm_vcpu *vcpu)
{
	const u64 feature_ids = pvm_read_id_reg(vcpu, SYS_ID_AA64DFR0_EL1);
	u64 mdcr_set = 0;
	u64 mdcr_clear = 0;
	u64 cptr_set = 0;

	/* Trap/constrain PMU */
	if (!FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_PMUVer), feature_ids)) {
		mdcr_set |= MDCR_EL2_TPM | MDCR_EL2_TPMCR;
		mdcr_clear |= MDCR_EL2_HPME | MDCR_EL2_MTPME |
			      MDCR_EL2_HPMN_MASK;
	}

	/* Trap Debug */
	if (!FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_DebugVer), feature_ids))
		mdcr_set |= MDCR_EL2_TDRA | MDCR_EL2_TDA;

	/* Trap OS Double Lock */
	if (!FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_DoubleLock), feature_ids))
		mdcr_set |= MDCR_EL2_TDOSA;

	/* Trap SPE */
	if (!FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_PMSVer), feature_ids)) {
		mdcr_set |= MDCR_EL2_TPMS;
		mdcr_clear |= MDCR_EL2_E2PB_MASK << MDCR_EL2_E2PB_SHIFT;
	}

	/* Trap Trace Filter */
	if (!FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_TraceFilt), feature_ids))
		mdcr_set |= MDCR_EL2_TTRF;

	/* Trap Trace */
	if (!FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_TraceVer), feature_ids))
		cptr_set |= CPTR_EL2_TTA;

	vcpu->arch.mdcr_el2 |= mdcr_set;
	vcpu->arch.mdcr_el2 &= ~mdcr_clear;
	vcpu->arch.cptr_el2 |= cptr_set;
}

/*
 * Set trap register values based on features in ID_AA64MMFR0.
 */
static void pvm_init_traps_aa64mmfr0(struct kvm_vcpu *vcpu)
{
	const u64 feature_ids = pvm_read_id_reg(vcpu, SYS_ID_AA64MMFR0_EL1);
	u64 mdcr_set = 0;

	/* Trap Debug Communications Channel registers */
	if (!FIELD_GET(ARM64_FEATURE_MASK(ID_AA64MMFR0_EL1_FGT), feature_ids))
		mdcr_set |= MDCR_EL2_TDCC;

	vcpu->arch.mdcr_el2 |= mdcr_set;
}

/*
 * Set trap register values based on features in ID_AA64MMFR1.
 */
static void pvm_init_traps_aa64mmfr1(struct kvm_vcpu *vcpu)
{
	const u64 feature_ids = pvm_read_id_reg(vcpu, SYS_ID_AA64MMFR1_EL1);
	u64 hcr_set = 0;

	/* Trap LOR */
	if (!FIELD_GET(ARM64_FEATURE_MASK(ID_AA64MMFR1_EL1_LO), feature_ids))
		hcr_set |= HCR_TLOR;

	vcpu->arch.hcr_el2 |= hcr_set;
}

/*
 * Set baseline trap register values.
 */
static void pvm_init_trap_regs(struct kvm_vcpu *vcpu)
{
	/*
	 * Always trap:
	 * - Feature id registers: to control features exposed to guests
	 * - Implementation-defined features
	 */
	vcpu->arch.hcr_el2 = HCR_GUEST_FLAGS |
			     HCR_TID3 | HCR_TACR | HCR_TIDCP | HCR_TID1;

	if (cpus_have_const_cap(ARM64_HAS_RAS_EXTN)) {
		/* route synchronous external abort exceptions to EL2 */
		vcpu->arch.hcr_el2 |= HCR_TEA;
		/* trap error record accesses */
		vcpu->arch.hcr_el2 |= HCR_TERR;
	}

	if (cpus_have_const_cap(ARM64_HAS_STAGE2_FWB))
		vcpu->arch.hcr_el2 |= HCR_FWB;

	if (cpus_have_const_cap(ARM64_MISMATCHED_CACHE_TYPE))
		vcpu->arch.hcr_el2 |= HCR_TID2;
}

/*
 * Initialize trap register values for protected VMs.
 */
static void pkvm_vcpu_init_traps(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	hyp_vcpu->vcpu.arch.cptr_el2 = CPTR_EL2_DEFAULT;
	hyp_vcpu->vcpu.arch.mdcr_el2 = 0;

	if (!pkvm_hyp_vcpu_is_protected(hyp_vcpu)) {
		u64 hcr = READ_ONCE(hyp_vcpu->host_vcpu->arch.hcr_el2);

		hyp_vcpu->vcpu.arch.hcr_el2 = HCR_GUEST_FLAGS | hcr;
		return;
	}

	pvm_init_trap_regs(&hyp_vcpu->vcpu);
	pvm_init_traps_aa64pfr0(&hyp_vcpu->vcpu);
	pvm_init_traps_aa64pfr1(&hyp_vcpu->vcpu);
	pvm_init_traps_aa64dfr0(&hyp_vcpu->vcpu);
	pvm_init_traps_aa64mmfr0(&hyp_vcpu->vcpu);
	pvm_init_traps_aa64mmfr1(&hyp_vcpu->vcpu);
}

/*
 * Start the VM table handle at the offset defined instead of at 0.
 * Mainly for sanity checking and debugging.
 */
#define HANDLE_OFFSET 0x1000

static unsigned int vm_handle_to_idx(pkvm_handle_t handle)
{
	return handle - HANDLE_OFFSET;
}

static pkvm_handle_t idx_to_vm_handle(unsigned int idx)
{
	return idx + HANDLE_OFFSET;
}

/*
 * Spinlock for protecting state related to the VM table. Protects writes
 * to 'vm_table' and 'nr_table_entries' as well as reads and writes to
 * 'last_hyp_vcpu_lookup'.
 */
static DEFINE_HYP_SPINLOCK(vm_table_lock);

/*
 * The table of VM entries for protected VMs in hyp.
 * Allocated at hyp initialization and setup.
 */
static struct pkvm_hyp_vm **vm_table;

void pkvm_hyp_vm_table_init(void *tbl)
{
	WARN_ON(vm_table);
	vm_table = tbl;
}

void pkvm_hyp_host_fp_init(void *host_fp)
{
	WARN_ON(host_fp_state);
	host_fp_state = host_fp;
}

/*
 * Return the hyp vm structure corresponding to the handle.
 */
static struct pkvm_hyp_vm *get_vm_by_handle(pkvm_handle_t handle)
{
	unsigned int idx = vm_handle_to_idx(handle);

	if (unlikely(idx >= KVM_MAX_PVMS))
		return NULL;

	return vm_table[idx];
}

int __pkvm_reclaim_dying_guest_page(pkvm_handle_t handle, u64 pfn, u64 ipa)
{
	struct pkvm_hyp_vm *hyp_vm;
	int ret = -EINVAL;

	hyp_spin_lock(&vm_table_lock);
	hyp_vm = get_vm_by_handle(handle);
	if (!hyp_vm || !hyp_vm->is_dying)
		goto unlock;

	ret = __pkvm_host_reclaim_page(hyp_vm, pfn, ipa);
	if (ret)
		goto unlock;

	drain_hyp_pool(hyp_vm, &hyp_vm->host_kvm->arch.pkvm.teardown_stage2_mc);
unlock:
	hyp_spin_unlock(&vm_table_lock);

	return ret;
}

struct pkvm_hyp_vcpu *pkvm_load_hyp_vcpu(pkvm_handle_t handle,
					 unsigned int vcpu_idx)
{
	struct pkvm_hyp_vcpu *hyp_vcpu = NULL;
	struct pkvm_hyp_vm *hyp_vm;

	/* Cannot load a new vcpu without putting the old one first. */
	if (__this_cpu_read(loaded_hyp_vcpu))
		return NULL;

	hyp_spin_lock(&vm_table_lock);
	hyp_vm = get_vm_by_handle(handle);
	if (!hyp_vm || hyp_vm->is_dying || hyp_vm->nr_vcpus <= vcpu_idx)
		goto unlock;

	hyp_vcpu = hyp_vm->vcpus[vcpu_idx];

	/* Ensure vcpu isn't loaded on more than one cpu simultaneously. */
	if (unlikely(hyp_vcpu->loaded_hyp_vcpu)) {
		hyp_vcpu = NULL;
		goto unlock;
	}

	hyp_vcpu->loaded_hyp_vcpu = this_cpu_ptr(&loaded_hyp_vcpu);
	hyp_page_ref_inc(hyp_virt_to_page(hyp_vm));
unlock:
	hyp_spin_unlock(&vm_table_lock);

	if (hyp_vcpu)
		__this_cpu_write(loaded_hyp_vcpu, hyp_vcpu);
	return hyp_vcpu;
}

void pkvm_put_hyp_vcpu(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct pkvm_hyp_vm *hyp_vm = pkvm_hyp_vcpu_to_hyp_vm(hyp_vcpu);

	hyp_spin_lock(&vm_table_lock);
	hyp_vcpu->loaded_hyp_vcpu = NULL;
	__this_cpu_write(loaded_hyp_vcpu, NULL);
	hyp_page_ref_dec(hyp_virt_to_page(hyp_vm));
	hyp_spin_unlock(&vm_table_lock);
}

struct pkvm_hyp_vcpu *pkvm_get_loaded_hyp_vcpu(void)
{
	return __this_cpu_read(loaded_hyp_vcpu);
}

static void pkvm_vcpu_init_features_from_host(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	DECLARE_BITMAP(allowed_features, KVM_VCPU_MAX_FEATURES);

	/* No restrictions for non-protected VMs. */
	if (!pkvm_hyp_vcpu_is_protected(hyp_vcpu)) {
		bitmap_copy(hyp_vcpu->vcpu.arch.features,
			    host_vcpu->arch.features,
			    KVM_VCPU_MAX_FEATURES);
		return;
	}

	bitmap_zero(allowed_features, KVM_VCPU_MAX_FEATURES);

	/*
	 * For protected vms, always allow:
	 * - CPU starting in poweroff state
	 * - PSCI v0.2
	 */
	set_bit(KVM_ARM_VCPU_POWER_OFF, allowed_features);
	set_bit(KVM_ARM_VCPU_PSCI_0_2, allowed_features);

	/*
	 * Check if remaining features are allowed:
	 * - Performance Monitoring
	 * - Scalable Vectors
	 * - Pointer Authentication
	 */
	if (FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_PMUVer), PVM_ID_AA64DFR0_ALLOW))
		set_bit(KVM_ARM_VCPU_PMU_V3, allowed_features);

	if (FIELD_GET(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_SVE), PVM_ID_AA64PFR0_ALLOW))
		set_bit(KVM_ARM_VCPU_SVE, allowed_features);

	if (FIELD_GET(ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_API), PVM_ID_AA64ISAR1_ALLOW) &&
	    FIELD_GET(ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_APA), PVM_ID_AA64ISAR1_ALLOW))
		set_bit(KVM_ARM_VCPU_PTRAUTH_ADDRESS, allowed_features);

	if (FIELD_GET(ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_GPI), PVM_ID_AA64ISAR1_ALLOW) &&
	    FIELD_GET(ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_GPA), PVM_ID_AA64ISAR1_ALLOW))
		set_bit(KVM_ARM_VCPU_PTRAUTH_GENERIC, allowed_features);

	bitmap_and(hyp_vcpu->vcpu.arch.features, host_vcpu->arch.features,
		   allowed_features, KVM_VCPU_MAX_FEATURES);

	/*
	 * Now sanitise the configuration flags that we have inherited
	 * from the host, as they may refer to features that protected
	 * mode doesn't support.
	 */
	if (!vcpu_has_feature(&hyp_vcpu->vcpu,(KVM_ARM_VCPU_SVE))) {
		vcpu_clear_flag(&hyp_vcpu->vcpu, GUEST_HAS_SVE);
		vcpu_clear_flag(&hyp_vcpu->vcpu, VCPU_SVE_FINALIZED);
	}

	if (!vcpu_has_feature(&hyp_vcpu->vcpu, KVM_ARM_VCPU_PTRAUTH_ADDRESS) ||
	    !vcpu_has_feature(&hyp_vcpu->vcpu, KVM_ARM_VCPU_PTRAUTH_GENERIC))
		vcpu_clear_flag(&hyp_vcpu->vcpu, GUEST_HAS_PTRAUTH);
}

static int pkvm_vcpu_init_ptrauth(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	int ret = 0;

	if (test_bit(KVM_ARM_VCPU_PTRAUTH_ADDRESS, vcpu->arch.features) ||
	    test_bit(KVM_ARM_VCPU_PTRAUTH_GENERIC, vcpu->arch.features))
		ret = kvm_vcpu_enable_ptrauth(vcpu);

	return ret;
}

static int pkvm_vcpu_init_psci(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct vcpu_reset_state *reset_state = &hyp_vcpu->vcpu.arch.reset_state;
	struct pkvm_hyp_vm *hyp_vm = pkvm_hyp_vcpu_to_hyp_vm(hyp_vcpu);

	if (test_bit(KVM_ARM_VCPU_POWER_OFF, hyp_vcpu->vcpu.arch.features)) {
		reset_state->reset = false;
		hyp_vcpu->power_state = PSCI_0_2_AFFINITY_LEVEL_OFF;
	} else if (pkvm_hyp_vm_has_pvmfw(hyp_vm)) {
		if (hyp_vm->pvmfw_entry_vcpu)
			return -EINVAL;

		hyp_vm->pvmfw_entry_vcpu = hyp_vcpu;
		reset_state->reset = true;
		hyp_vcpu->power_state = PSCI_0_2_AFFINITY_LEVEL_ON_PENDING;
	} else {
		struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;

		reset_state->pc = READ_ONCE(host_vcpu->arch.ctxt.regs.pc);
		reset_state->r0 = READ_ONCE(host_vcpu->arch.ctxt.regs.regs[0]);
		reset_state->reset = true;
		hyp_vcpu->power_state = PSCI_0_2_AFFINITY_LEVEL_ON_PENDING;
	}

	return 0;
}

static void unpin_host_vcpu(struct kvm_vcpu *host_vcpu)
{
	if (host_vcpu)
		hyp_unpin_shared_mem(host_vcpu, host_vcpu + 1);
}

static void unpin_host_sve_state(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	void *sve_state;

	if (!test_bit(KVM_ARM_VCPU_SVE, hyp_vcpu->vcpu.arch.features))
		return;

	sve_state = kern_hyp_va(hyp_vcpu->vcpu.arch.sve_state);
	hyp_unpin_shared_mem(sve_state,
			     sve_state + vcpu_sve_state_size(&hyp_vcpu->vcpu));
}

static void unpin_host_vcpus(struct pkvm_hyp_vcpu *hyp_vcpus[],
			     unsigned int nr_vcpus)
{
	int i;

	for (i = 0; i < nr_vcpus; i++) {
		struct pkvm_hyp_vcpu *hyp_vcpu = hyp_vcpus[i];

		unpin_host_vcpu(hyp_vcpu->host_vcpu);
		unpin_host_sve_state(hyp_vcpu);
	}
}

static size_t pkvm_get_last_ran_size(void)
{
	return array_size(hyp_nr_cpus, sizeof(int));
}

static void init_pkvm_hyp_vm(struct kvm *host_kvm, struct pkvm_hyp_vm *hyp_vm,
			     int *last_ran, unsigned int nr_vcpus)
{
	u64 pvmfw_load_addr = PVMFW_INVALID_LOAD_ADDR;

	hyp_vm->host_kvm = host_kvm;
	hyp_vm->kvm.created_vcpus = nr_vcpus;
	hyp_vm->kvm.arch.vtcr = host_mmu.arch.vtcr;
	hyp_vm->kvm.arch.pkvm.enabled = READ_ONCE(host_kvm->arch.pkvm.enabled);

	if (hyp_vm->kvm.arch.pkvm.enabled)
		pvmfw_load_addr = READ_ONCE(host_kvm->arch.pkvm.pvmfw_load_addr);
	hyp_vm->kvm.arch.pkvm.pvmfw_load_addr = pvmfw_load_addr;

	hyp_vm->kvm.arch.mmu.last_vcpu_ran = (int __percpu *)last_ran;
	memset(last_ran, -1, pkvm_get_last_ran_size());
}

static int init_pkvm_hyp_vcpu(struct pkvm_hyp_vcpu *hyp_vcpu,
			      struct pkvm_hyp_vm *hyp_vm,
			      struct kvm_vcpu *host_vcpu,
			      unsigned int vcpu_idx)
{
	int ret = 0;

	if (hyp_pin_shared_mem(host_vcpu, host_vcpu + 1))
		return -EBUSY;

	if (host_vcpu->vcpu_idx != vcpu_idx) {
		ret = -EINVAL;
		goto done;
	}

	hyp_vcpu->host_vcpu = host_vcpu;

	hyp_vcpu->vcpu.kvm = &hyp_vm->kvm;
	hyp_vcpu->vcpu.vcpu_id = READ_ONCE(host_vcpu->vcpu_id);
	hyp_vcpu->vcpu.vcpu_idx = vcpu_idx;

	hyp_vcpu->vcpu.arch.hw_mmu = &hyp_vm->kvm.arch.mmu;
	hyp_vcpu->vcpu.arch.cflags = READ_ONCE(host_vcpu->arch.cflags);
	hyp_vcpu->vcpu.arch.mp_state.mp_state = KVM_MP_STATE_STOPPED;
	hyp_vcpu->vcpu.arch.debug_ptr = &host_vcpu->arch.vcpu_debug_state;

	pkvm_vcpu_init_features_from_host(hyp_vcpu);

	ret = pkvm_vcpu_init_ptrauth(hyp_vcpu);
	if (ret)
		goto done;

	ret = pkvm_vcpu_init_psci(hyp_vcpu);
	if (ret)
		goto done;

	if (test_bit(KVM_ARM_VCPU_SVE, hyp_vcpu->vcpu.arch.features)) {
		size_t sve_state_size;
		void *sve_state;

		hyp_vcpu->vcpu.arch.sve_state = READ_ONCE(host_vcpu->arch.sve_state);
		hyp_vcpu->vcpu.arch.sve_max_vl = READ_ONCE(host_vcpu->arch.sve_max_vl);

		sve_state = kern_hyp_va(hyp_vcpu->vcpu.arch.sve_state);
		sve_state_size = vcpu_sve_state_size(&hyp_vcpu->vcpu);

		if (!hyp_vcpu->vcpu.arch.sve_state || !sve_state_size ||
		    hyp_pin_shared_mem(sve_state, sve_state + sve_state_size)) {
			clear_bit(KVM_ARM_VCPU_SVE, hyp_vcpu->vcpu.arch.features);
			hyp_vcpu->vcpu.arch.sve_state = NULL;
			hyp_vcpu->vcpu.arch.sve_max_vl = 0;
			ret = -EINVAL;
			goto done;
		}
	}

	pkvm_vcpu_init_traps(hyp_vcpu);
	kvm_reset_pvm_sys_regs(&hyp_vcpu->vcpu);
done:
	if (ret)
		unpin_host_vcpu(host_vcpu);
	return ret;
}

static int find_free_vm_table_entry(struct kvm *host_kvm)
{
	int i;

	for (i = 0; i < KVM_MAX_PVMS; ++i) {
		if (!vm_table[i])
			return i;
	}

	return -ENOMEM;
}

/*
 * Allocate a VM table entry and insert a pointer to the new vm.
 *
 * Return a unique handle to the protected VM on success,
 * negative error code on failure.
 */
static pkvm_handle_t insert_vm_table_entry(struct kvm *host_kvm,
					   struct pkvm_hyp_vm *hyp_vm)
{
	struct kvm_s2_mmu *mmu = &hyp_vm->kvm.arch.mmu;
	int idx;

	hyp_assert_lock_held(&vm_table_lock);

	/*
	 * Initializing protected state might have failed, yet a malicious
	 * host could trigger this function. Thus, ensure that 'vm_table'
	 * exists.
	 */
	if (unlikely(!vm_table))
		return -EINVAL;

	idx = find_free_vm_table_entry(host_kvm);
	if (idx < 0)
		return idx;

	hyp_vm->kvm.arch.pkvm.handle = idx_to_vm_handle(idx);

	/* VMID 0 is reserved for the host */
	atomic64_set(&mmu->vmid.id, idx + 1);

	mmu->arch = &hyp_vm->kvm.arch;
	mmu->pgt = &hyp_vm->pgt;

	vm_table[idx] = hyp_vm;
	return hyp_vm->kvm.arch.pkvm.handle;
}

/*
 * Deallocate and remove the VM table entry corresponding to the handle.
 */
static void remove_vm_table_entry(pkvm_handle_t handle)
{
	hyp_assert_lock_held(&vm_table_lock);
	vm_table[vm_handle_to_idx(handle)] = NULL;
}

static size_t pkvm_get_hyp_vm_size(unsigned int nr_vcpus)
{
	return size_add(sizeof(struct pkvm_hyp_vm),
		size_mul(sizeof(struct pkvm_hyp_vcpu *), nr_vcpus));
}

static void *map_donated_memory_noclear(unsigned long host_va, size_t size)
{
	void *va = (void *)kern_hyp_va(host_va);

	if (!PAGE_ALIGNED(va))
		return NULL;

	if (__pkvm_host_donate_hyp(hyp_virt_to_pfn(va),
				   PAGE_ALIGN(size) >> PAGE_SHIFT))
		return NULL;

	return va;
}

static void *map_donated_memory(unsigned long host_va, size_t size)
{
	void *va = map_donated_memory_noclear(host_va, size);

	if (va)
		memset(va, 0, size);

	return va;
}

static void __unmap_donated_memory(void *va, size_t size)
{
	kvm_flush_dcache_to_poc(va, size);
	WARN_ON(__pkvm_hyp_donate_host(hyp_virt_to_pfn(va),
				       PAGE_ALIGN(size) >> PAGE_SHIFT));
}

static void unmap_donated_memory(void *va, size_t size)
{
	if (!va)
		return;

	memset(va, 0, size);
	__unmap_donated_memory(va, size);
}

static void unmap_donated_memory_noclear(void *va, size_t size)
{
	if (!va)
		return;

	__unmap_donated_memory(va, size);
}

/*
 * Initialize the hypervisor copy of the protected VM state using the
 * memory donated by the host.
 *
 * Unmaps the donated memory from the host at stage 2.
 *
 * host_kvm: A pointer to the host's struct kvm.
 * vm_hva: The host va of the area being donated for the VM state.
 *	   Must be page aligned.
 * pgd_hva: The host va of the area being donated for the stage-2 PGD for
 *	    the VM. Must be page aligned. Its size is implied by the VM's
 *	    VTCR.
 * last_ran_hva: The host va of the area being donated for hyp to use to track
 *		 the most recent physical cpu on which each vcpu has run.
 * Return a unique handle to the protected VM on success,
 * negative error code on failure.
 */
int __pkvm_init_vm(struct kvm *host_kvm, unsigned long vm_hva,
		   unsigned long pgd_hva, unsigned long last_ran_hva)
{
	struct pkvm_hyp_vm *hyp_vm = NULL;
	int *last_ran = NULL;
	size_t vm_size, pgd_size, last_ran_size;
	unsigned int nr_vcpus;
	void *pgd = NULL;
	int ret;

	ret = hyp_pin_shared_mem(host_kvm, host_kvm + 1);
	if (ret)
		return ret;

	nr_vcpus = READ_ONCE(host_kvm->created_vcpus);
	if (nr_vcpus < 1) {
		ret = -EINVAL;
		goto err_unpin_kvm;
	}

	vm_size = pkvm_get_hyp_vm_size(nr_vcpus);
	last_ran_size = pkvm_get_last_ran_size();
	pgd_size = kvm_pgtable_stage2_pgd_size(host_mmu.arch.vtcr);

	ret = -ENOMEM;

	hyp_vm = map_donated_memory(vm_hva, vm_size);
	if (!hyp_vm)
		goto err_remove_mappings;

	last_ran = map_donated_memory(last_ran_hva, last_ran_size);
	if (!last_ran)
		goto err_remove_mappings;

	pgd = map_donated_memory_noclear(pgd_hva, pgd_size);
	if (!pgd)
		goto err_remove_mappings;

	init_pkvm_hyp_vm(host_kvm, hyp_vm, last_ran, nr_vcpus);

	hyp_spin_lock(&vm_table_lock);
	ret = insert_vm_table_entry(host_kvm, hyp_vm);
	if (ret < 0)
		goto err_unlock;

	ret = kvm_guest_prepare_stage2(hyp_vm, pgd);
	if (ret)
		goto err_remove_vm_table_entry;
	hyp_spin_unlock(&vm_table_lock);

	return hyp_vm->kvm.arch.pkvm.handle;

err_remove_vm_table_entry:
	remove_vm_table_entry(hyp_vm->kvm.arch.pkvm.handle);
err_unlock:
	hyp_spin_unlock(&vm_table_lock);
err_remove_mappings:
	unmap_donated_memory(hyp_vm, vm_size);
	unmap_donated_memory(last_ran, last_ran_size);
	unmap_donated_memory(pgd, pgd_size);
err_unpin_kvm:
	hyp_unpin_shared_mem(host_kvm, host_kvm + 1);
	return ret;
}

/*
 * Initialize the hypervisor copy of the protected vCPU state using the
 * memory donated by the host.
 *
 * handle: The handle for the protected vm.
 * host_vcpu: A pointer to the corresponding host vcpu.
 * vcpu_hva: The host va of the area being donated for the vcpu state.
 *	     Must be page aligned. The size of the area must be equal to
 *	     the page-aligned size of 'struct pkvm_hyp_vcpu'.
 * Return 0 on success, negative error code on failure.
 */
int __pkvm_init_vcpu(pkvm_handle_t handle, struct kvm_vcpu *host_vcpu,
		     unsigned long vcpu_hva)
{
	struct pkvm_hyp_vcpu *hyp_vcpu;
	struct pkvm_hyp_vm *hyp_vm;
	unsigned int idx;
	int ret;

	hyp_vcpu = map_donated_memory(vcpu_hva, sizeof(*hyp_vcpu));
	if (!hyp_vcpu)
		return -ENOMEM;

	hyp_spin_lock(&vm_table_lock);

	hyp_vm = get_vm_by_handle(handle);
	if (!hyp_vm) {
		ret = -ENOENT;
		goto unlock;
	}

	idx = hyp_vm->nr_vcpus;
	if (idx >= hyp_vm->kvm.created_vcpus) {
		ret = -EINVAL;
		goto unlock;
	}

	ret = init_pkvm_hyp_vcpu(hyp_vcpu, hyp_vm, host_vcpu, idx);
	if (ret)
		goto unlock;

	hyp_vm->vcpus[idx] = hyp_vcpu;
	hyp_vm->nr_vcpus++;
unlock:
	hyp_spin_unlock(&vm_table_lock);

	if (ret)
		unmap_donated_memory(hyp_vcpu, sizeof(*hyp_vcpu));

	return ret;
}

static void
teardown_donated_memory(struct kvm_hyp_memcache *mc, void *addr, size_t size)
{
	void *start;

	size = PAGE_ALIGN(size);
	memset(addr, 0, size);

	for (start = addr; start < addr + size; start += PAGE_SIZE)
		push_hyp_memcache(mc, start, hyp_virt_to_phys);

	unmap_donated_memory_noclear(addr, size);
}

int __pkvm_start_teardown_vm(pkvm_handle_t handle)
{
	struct pkvm_hyp_vm *hyp_vm;
	int ret = 0;

	hyp_spin_lock(&vm_table_lock);
	hyp_vm = get_vm_by_handle(handle);
	if (!hyp_vm) {
		ret = -ENOENT;
		goto unlock;
	} else if (WARN_ON(hyp_page_count(hyp_vm))) {
		ret = -EBUSY;
		goto unlock;
	} else if (hyp_vm->is_dying) {
		ret = -EINVAL;
		goto unlock;
	}

	hyp_vm->is_dying = true;

unlock:
	hyp_spin_unlock(&vm_table_lock);

	return ret;
}

int __pkvm_finalize_teardown_vm(pkvm_handle_t handle)
{
	struct kvm_hyp_memcache *mc, *stage2_mc;
	size_t vm_size, last_ran_size;
	int __percpu *last_vcpu_ran;
	struct pkvm_hyp_vm *hyp_vm;
	struct kvm *host_kvm;
	unsigned int idx;
	int err;

	hyp_spin_lock(&vm_table_lock);
	hyp_vm = get_vm_by_handle(handle);
	if (!hyp_vm) {
		err = -ENOENT;
		goto err_unlock;
	} else if (!hyp_vm->is_dying) {
		err = -EBUSY;
		goto err_unlock;
	}

	host_kvm = hyp_vm->host_kvm;

	/* Ensure the VMID is clean before it can be reallocated */
	__kvm_tlb_flush_vmid(&hyp_vm->kvm.arch.mmu);
	remove_vm_table_entry(handle);
	hyp_spin_unlock(&vm_table_lock);

	mc = &host_kvm->arch.pkvm.teardown_mc;
	stage2_mc = &host_kvm->arch.pkvm.teardown_stage2_mc;

	destroy_hyp_vm_pgt(hyp_vm);
	drain_hyp_pool(hyp_vm, stage2_mc);
	unpin_host_vcpus(hyp_vm->vcpus, hyp_vm->nr_vcpus);

	/* Push the metadata pages to the teardown memcache */
	for (idx = 0; idx < hyp_vm->nr_vcpus; ++idx) {
		struct pkvm_hyp_vcpu *hyp_vcpu = hyp_vm->vcpus[idx];
		struct kvm_hyp_memcache *vcpu_mc;
		void *addr;

		vcpu_mc = &hyp_vcpu->vcpu.arch.pkvm_memcache;
		while (vcpu_mc->nr_pages) {
			addr = pop_hyp_memcache(vcpu_mc, hyp_phys_to_virt);
			push_hyp_memcache(stage2_mc, addr, hyp_virt_to_phys);
			unmap_donated_memory_noclear(addr, PAGE_SIZE);
		}

		teardown_donated_memory(mc, hyp_vcpu, sizeof(*hyp_vcpu));
	}

	last_vcpu_ran = hyp_vm->kvm.arch.mmu.last_vcpu_ran;
	last_ran_size = pkvm_get_last_ran_size();
	teardown_donated_memory(mc, (__force void *)last_vcpu_ran,
				last_ran_size);

	vm_size = pkvm_get_hyp_vm_size(hyp_vm->kvm.created_vcpus);
	teardown_donated_memory(mc, hyp_vm, vm_size);
	hyp_unpin_shared_mem(host_kvm, host_kvm + 1);
	return 0;

err_unlock:
	hyp_spin_unlock(&vm_table_lock);
	return err;
}

int pkvm_load_pvmfw_pages(struct pkvm_hyp_vm *vm, u64 ipa, phys_addr_t phys,
			  u64 size)
{
	struct kvm_protected_vm *pkvm = &vm->kvm.arch.pkvm;
	u64 npages, offset = ipa - pkvm->pvmfw_load_addr;
	void *src = hyp_phys_to_virt(pvmfw_base) + offset;

	if (offset >= pvmfw_size)
		return -EINVAL;

	size = min(size, pvmfw_size - offset);
	if (!PAGE_ALIGNED(size) || !PAGE_ALIGNED(src))
		return -EINVAL;

	npages = size >> PAGE_SHIFT;
	while (npages--) {
		/*
		 * No need for cache maintenance here, as the pgtable code will
		 * take care of this when installing the pte in the guest's
		 * stage-2 page table.
		 */
		memcpy(hyp_fixmap_map(phys), src, PAGE_SIZE);
		hyp_fixmap_unmap();

		src += PAGE_SIZE;
		phys += PAGE_SIZE;
	}

	return 0;
}

void pkvm_poison_pvmfw_pages(void)
{
	u64 npages = pvmfw_size >> PAGE_SHIFT;
	phys_addr_t addr = pvmfw_base;

	while (npages--) {
		hyp_poison_page(addr);
		addr += PAGE_SIZE;
	}
}

/*
 * This function sets the registers on the vcpu to their architecturally defined
 * reset values.
 *
 * Note: Can only be called by the vcpu on itself, after it has been turned on.
 */
void pkvm_reset_vcpu(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct vcpu_reset_state *reset_state = &hyp_vcpu->vcpu.arch.reset_state;
	struct pkvm_hyp_vm *hyp_vm = pkvm_hyp_vcpu_to_hyp_vm(hyp_vcpu);

	WARN_ON(!reset_state->reset);

	pkvm_vcpu_init_ptrauth(hyp_vcpu);
	kvm_reset_vcpu_core(&hyp_vcpu->vcpu);
	kvm_reset_pvm_sys_regs(&hyp_vcpu->vcpu);

	/* Must be done after reseting sys registers. */
	kvm_reset_vcpu_psci(&hyp_vcpu->vcpu, reset_state);
	if (hyp_vm->pvmfw_entry_vcpu == hyp_vcpu) {
		struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
		u64 entry = hyp_vm->kvm.arch.pkvm.pvmfw_load_addr;
		int i;

		/* X0 - X14 provided by the VMM (preserved) */
		for (i = 0; i <= 14; ++i) {
			u64 val = vcpu_get_reg(host_vcpu, i);

			vcpu_set_reg(&hyp_vcpu->vcpu, i, val);
		}

		/* X15: Boot protocol version */
		vcpu_set_reg(&hyp_vcpu->vcpu, 15, 0);

		/* PC: IPA of pvmfw base */
		*vcpu_pc(&hyp_vcpu->vcpu) = entry;
		hyp_vm->pvmfw_entry_vcpu = NULL;

		/* Auto enroll MMIO guard */
		set_bit(KVM_ARCH_FLAG_MMIO_GUARD, &hyp_vm->kvm.arch.flags);
	}

	reset_state->reset = false;

	hyp_vcpu->exit_code = 0;

	WARN_ON(hyp_vcpu->power_state != PSCI_0_2_AFFINITY_LEVEL_ON_PENDING);
	WRITE_ONCE(hyp_vcpu->vcpu.arch.mp_state.mp_state, KVM_MP_STATE_RUNNABLE);
	WRITE_ONCE(hyp_vcpu->power_state, PSCI_0_2_AFFINITY_LEVEL_ON);
}

struct pkvm_hyp_vcpu *pkvm_mpidr_to_hyp_vcpu(struct pkvm_hyp_vm *hyp_vm,
					     u64 mpidr)
{
	int i;

	mpidr &= MPIDR_HWID_BITMASK;

	for (i = 0; i < hyp_vm->nr_vcpus; i++) {
		struct pkvm_hyp_vcpu *hyp_vcpu = hyp_vm->vcpus[i];

		if (mpidr == kvm_vcpu_get_mpidr_aff(&hyp_vcpu->vcpu))
			return hyp_vcpu;
	}

	return NULL;
}

/*
 * Returns true if the hypervisor has handled the PSCI call, and control should
 * go back to the guest, or false if the host needs to do some additional work
 * (i.e., wake up the vcpu).
 */
static bool pvm_psci_vcpu_on(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct pkvm_hyp_vm *hyp_vm = pkvm_hyp_vcpu_to_hyp_vm(hyp_vcpu);
	struct vcpu_reset_state *reset_state;
	struct pkvm_hyp_vcpu *target;
	unsigned long cpu_id, ret;
	int power_state;

	cpu_id = smccc_get_arg1(&hyp_vcpu->vcpu);
	if (!kvm_psci_valid_affinity(&hyp_vcpu->vcpu, cpu_id)) {
		ret = PSCI_RET_INVALID_PARAMS;
		goto error;
	}

	target = pkvm_mpidr_to_hyp_vcpu(hyp_vm, cpu_id);
	if (!target) {
		ret = PSCI_RET_INVALID_PARAMS;
		goto error;
	}

	/*
	 * Make sure the requested vcpu is not on to begin with.
	 * Atomic to avoid race between vcpus trying to power on the same vcpu.
	 */
	power_state = cmpxchg(&target->power_state,
			      PSCI_0_2_AFFINITY_LEVEL_OFF,
			      PSCI_0_2_AFFINITY_LEVEL_ON_PENDING);
	switch (power_state) {
	case PSCI_0_2_AFFINITY_LEVEL_ON_PENDING:
		ret = PSCI_RET_ON_PENDING;
		goto error;
	case PSCI_0_2_AFFINITY_LEVEL_ON:
		ret = PSCI_RET_ALREADY_ON;
		goto error;
	case PSCI_0_2_AFFINITY_LEVEL_OFF:
		break;
	default:
		ret = PSCI_RET_INTERNAL_FAILURE;
		goto error;
	}

	reset_state = &target->vcpu.arch.reset_state;
	reset_state->pc = smccc_get_arg2(&hyp_vcpu->vcpu);
	reset_state->r0 = smccc_get_arg3(&hyp_vcpu->vcpu);
	/* Propagate caller endianness */
	reset_state->be = kvm_vcpu_is_be(&hyp_vcpu->vcpu);
	reset_state->reset = true;

	/*
	 * Return to the host, which should make the KVM_REQ_VCPU_RESET request
	 * as well as kvm_vcpu_wake_up() to schedule the vcpu.
	 */
	return false;

error:
	/* If there's an error go back straight to the guest. */
	smccc_set_retval(&hyp_vcpu->vcpu, ret, 0, 0, 0);
	return true;
}

static bool pvm_psci_vcpu_affinity_info(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	unsigned long target_affinity_mask, target_affinity, lowest_affinity_level;
	struct pkvm_hyp_vm *hyp_vm = pkvm_hyp_vcpu_to_hyp_vm(hyp_vcpu);
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	unsigned long mpidr, ret;
	int i, matching_cpus = 0;

	target_affinity = smccc_get_arg1(vcpu);
	lowest_affinity_level = smccc_get_arg2(vcpu);
	if (!kvm_psci_valid_affinity(vcpu, target_affinity)) {
		ret = PSCI_RET_INVALID_PARAMS;
		goto done;
	}

	/* Determine target affinity mask */
	target_affinity_mask = psci_affinity_mask(lowest_affinity_level);
	if (!target_affinity_mask) {
		ret = PSCI_RET_INVALID_PARAMS;
		goto done;
	}

	/* Ignore other bits of target affinity */
	target_affinity &= target_affinity_mask;
	ret = PSCI_0_2_AFFINITY_LEVEL_OFF;

	/*
	 * If at least one vcpu matching target affinity is ON then return ON,
	 * then if at least one is PENDING_ON then return PENDING_ON.
	 * Otherwise, return OFF.
	 */
	for (i = 0; i < hyp_vm->nr_vcpus; i++) {
		struct pkvm_hyp_vcpu *target = hyp_vm->vcpus[i];

		mpidr = kvm_vcpu_get_mpidr_aff(&target->vcpu);

		if ((mpidr & target_affinity_mask) == target_affinity) {
			int power_state;

			matching_cpus++;
			power_state = READ_ONCE(target->power_state);
			switch (power_state) {
			case PSCI_0_2_AFFINITY_LEVEL_ON_PENDING:
				ret = PSCI_0_2_AFFINITY_LEVEL_ON_PENDING;
				break;
			case PSCI_0_2_AFFINITY_LEVEL_ON:
				ret = PSCI_0_2_AFFINITY_LEVEL_ON;
				goto done;
			case PSCI_0_2_AFFINITY_LEVEL_OFF:
				break;
			default:
				ret = PSCI_RET_INTERNAL_FAILURE;
				goto done;
			}
		}
	}

	if (!matching_cpus)
		ret = PSCI_RET_INVALID_PARAMS;

done:
	/* Nothing to be handled by the host. Go back to the guest. */
	smccc_set_retval(vcpu, ret, 0, 0, 0);
	return true;
}

/*
 * Returns true if the hypervisor has handled the PSCI call, and control should
 * go back to the guest, or false if the host needs to do some additional work
 * (e.g., turn off and update vcpu scheduling status).
 */
static bool pvm_psci_vcpu_off(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;

	WARN_ON(vcpu->arch.mp_state.mp_state == KVM_MP_STATE_STOPPED);
	WARN_ON(hyp_vcpu->power_state != PSCI_0_2_AFFINITY_LEVEL_ON);

	WRITE_ONCE(vcpu->arch.mp_state.mp_state, KVM_MP_STATE_STOPPED);
	WRITE_ONCE(hyp_vcpu->power_state, PSCI_0_2_AFFINITY_LEVEL_OFF);

	/* Return to the host so that it can finish powering off the vcpu. */
	return false;
}

static bool pvm_psci_version(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	/* Nothing to be handled by the host. Go back to the guest. */
	smccc_set_retval(&hyp_vcpu->vcpu, KVM_ARM_PSCI_1_1, 0, 0, 0);
	return true;
}

static bool pvm_psci_not_supported(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	/* Nothing to be handled by the host. Go back to the guest. */
	smccc_set_retval(&hyp_vcpu->vcpu, PSCI_RET_NOT_SUPPORTED, 0, 0, 0);
	return true;
}

static bool pvm_psci_features(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	u32 feature = smccc_get_arg1(vcpu);
	unsigned long val;

	switch (feature) {
	case PSCI_0_2_FN_PSCI_VERSION:
	case PSCI_0_2_FN_CPU_SUSPEND:
	case PSCI_0_2_FN64_CPU_SUSPEND:
	case PSCI_0_2_FN_CPU_OFF:
	case PSCI_0_2_FN_CPU_ON:
	case PSCI_0_2_FN64_CPU_ON:
	case PSCI_0_2_FN_AFFINITY_INFO:
	case PSCI_0_2_FN64_AFFINITY_INFO:
	case PSCI_0_2_FN_SYSTEM_OFF:
	case PSCI_0_2_FN_SYSTEM_RESET:
	case PSCI_1_0_FN_PSCI_FEATURES:
	case PSCI_1_1_FN_SYSTEM_RESET2:
	case PSCI_1_1_FN64_SYSTEM_RESET2:
	case ARM_SMCCC_VERSION_FUNC_ID:
		val = PSCI_RET_SUCCESS;
		break;
	default:
		val = PSCI_RET_NOT_SUPPORTED;
		break;
	}

	/* Nothing to be handled by the host. Go back to the guest. */
	smccc_set_retval(vcpu, val, 0, 0, 0);
	return true;
}

static bool pkvm_handle_psci(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	u32 psci_fn = smccc_get_function(vcpu);

	switch (psci_fn) {
	case PSCI_0_2_FN_CPU_ON:
		kvm_psci_narrow_to_32bit(vcpu);
		fallthrough;
	case PSCI_0_2_FN64_CPU_ON:
		return pvm_psci_vcpu_on(hyp_vcpu);
	case PSCI_0_2_FN_CPU_OFF:
		return pvm_psci_vcpu_off(hyp_vcpu);
	case PSCI_0_2_FN_AFFINITY_INFO:
		kvm_psci_narrow_to_32bit(vcpu);
		fallthrough;
	case PSCI_0_2_FN64_AFFINITY_INFO:
		return pvm_psci_vcpu_affinity_info(hyp_vcpu);
	case PSCI_0_2_FN_PSCI_VERSION:
		return pvm_psci_version(hyp_vcpu);
	case PSCI_1_0_FN_PSCI_FEATURES:
		return pvm_psci_features(hyp_vcpu);
	case PSCI_0_2_FN_SYSTEM_RESET:
	case PSCI_0_2_FN_CPU_SUSPEND:
	case PSCI_0_2_FN64_CPU_SUSPEND:
	case PSCI_0_2_FN_SYSTEM_OFF:
	case PSCI_1_1_FN_SYSTEM_RESET2:
	case PSCI_1_1_FN64_SYSTEM_RESET2:
		return false; /* Handled by the host. */
	default:
		break;
	}

	return pvm_psci_not_supported(hyp_vcpu);
}

static u64 __pkvm_memshare_page_req(struct pkvm_hyp_vcpu *hyp_vcpu, u64 ipa)
{
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	u64 elr;

	/* Fake up a data abort (Level 3 translation fault on write) */
	vcpu->arch.fault.esr_el2 = (u32)ESR_ELx_EC_DABT_LOW << ESR_ELx_EC_SHIFT |
				   ESR_ELx_WNR | ESR_ELx_FSC_FAULT |
				   FIELD_PREP(ESR_ELx_FSC_LEVEL, 3);

	/* Shuffle the IPA around into the HPFAR */
	vcpu->arch.fault.hpfar_el2 = (ipa >> 8) & HPFAR_MASK;

	/* This is a virtual address. 0's good. Let's go with 0. */
	vcpu->arch.fault.far_el2 = 0;

	/* Rewind the ELR so we return to the HVC once the IPA is mapped */
	elr = read_sysreg(elr_el2);
	elr -= 4;
	write_sysreg(elr, elr_el2);

	return ARM_EXCEPTION_TRAP;
}

static bool pkvm_memshare_call(struct pkvm_hyp_vcpu *hyp_vcpu, u64 *exit_code)
{
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	u64 ipa = smccc_get_arg1(vcpu);
	u64 arg2 = smccc_get_arg2(vcpu);
	u64 arg3 = smccc_get_arg3(vcpu);
	int err;

	if (arg2 || arg3)
		goto out_guest_err;

	err = __pkvm_guest_share_host(hyp_vcpu, ipa);
	switch (err) {
	case 0:
		/* Success! Now tell the host. */
		goto out_host;
	case -EFAULT:
		/*
		 * Convert the exception into a data abort so that the page
		 * being shared is mapped into the guest next time.
		 */
		*exit_code = __pkvm_memshare_page_req(hyp_vcpu, ipa);
		goto out_host;
	}

out_guest_err:
	smccc_set_retval(vcpu, SMCCC_RET_INVALID_PARAMETER, 0, 0, 0);
	return true;

out_host:
	return false;
}

static bool pkvm_memunshare_call(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	u64 ipa = smccc_get_arg1(vcpu);
	u64 arg2 = smccc_get_arg2(vcpu);
	u64 arg3 = smccc_get_arg3(vcpu);
	int err;

	if (arg2 || arg3)
		goto out_guest_err;

	err = __pkvm_guest_unshare_host(hyp_vcpu, ipa);
	if (err)
		goto out_guest_err;

	return false;

out_guest_err:
	smccc_set_retval(vcpu, SMCCC_RET_INVALID_PARAMETER, 0, 0, 0);
	return true;
}

static bool pkvm_meminfo_call(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	u64 arg1 = smccc_get_arg1(vcpu);
	u64 arg2 = smccc_get_arg2(vcpu);
	u64 arg3 = smccc_get_arg3(vcpu);

	if (arg1 || arg2 || arg3)
		goto out_guest_err;

	smccc_set_retval(vcpu, PAGE_SIZE, 0, 0, 0);
	return true;

out_guest_err:
	smccc_set_retval(vcpu, SMCCC_RET_INVALID_PARAMETER, 0, 0, 0);
	return true;
}

static bool pkvm_memrelinquish_call(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	u64 ipa = smccc_get_arg1(vcpu);
	u64 arg2 = smccc_get_arg2(vcpu);
	u64 arg3 = smccc_get_arg3(vcpu);
	u64 pa = 0;
	int ret;

	if (arg2 || arg3)
		goto out_guest_err;

	ret = __pkvm_guest_relinquish_to_host(hyp_vcpu, ipa, &pa);
	if (ret)
		goto out_guest_err;

	if (pa != 0) {
		/* Now pass to host. */
		return false;
	}

	/* This was a NOP as no page was actually mapped at the IPA. */
	smccc_set_retval(vcpu, 0, 0, 0, 0);
	return true;

out_guest_err:
	smccc_set_retval(vcpu, SMCCC_RET_INVALID_PARAMETER, 0, 0, 0);
	return true;
}

static bool pkvm_install_ioguard_page(struct pkvm_hyp_vcpu *hyp_vcpu, u64 *exit_code)
{
	u64 retval = SMCCC_RET_SUCCESS;
	u64 ipa = smccc_get_arg1(&hyp_vcpu->vcpu);
	int ret;

	ret = __pkvm_install_ioguard_page(hyp_vcpu, ipa);
	if (ret == -ENOMEM) {
		/*
		 * We ran out of memcache, let's ask for more. Cancel
		 * the effects of the HVC that took us here, and
		 * forward the hypercall to the host for page donation
		 * purposes.
		 */
		write_sysreg_el2(read_sysreg_el2(SYS_ELR) - 4, SYS_ELR);
		return false;
	}

	if (ret)
		retval = SMCCC_RET_INVALID_PARAMETER;

	smccc_set_retval(&hyp_vcpu->vcpu, retval, 0, 0, 0);
	return true;
}

bool smccc_trng_available;

static bool pkvm_forward_trng(struct kvm_vcpu *vcpu)
{
	u32 fn = smccc_get_function(vcpu);
	struct arm_smccc_res res;
	unsigned long arg1 = 0;

	/*
	 * Forward TRNG calls to EL3, as we can't trust the host to handle
	 * these for us.
	 */
	switch (fn) {
	case ARM_SMCCC_TRNG_FEATURES:
	case ARM_SMCCC_TRNG_RND32:
	case ARM_SMCCC_TRNG_RND64:
		arg1 = smccc_get_arg1(vcpu);
		fallthrough;
	case ARM_SMCCC_TRNG_VERSION:
	case ARM_SMCCC_TRNG_GET_UUID:
		arm_smccc_1_1_smc(fn, arg1, &res);
		smccc_set_retval(vcpu, res.a0, res.a1, res.a2, res.a3);
		memzero_explicit(&res, sizeof(res));
		break;
	}

	return true;
}

/*
 * Handler for protected VM HVC calls.
 *
 * Returns true if the hypervisor has handled the exit, and control should go
 * back to the guest, or false if it hasn't.
 */
bool kvm_handle_pvm_hvc64(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	u64 val[4] = { SMCCC_RET_NOT_SUPPORTED };
	u32 fn = smccc_get_function(vcpu);
	struct pkvm_hyp_vcpu *hyp_vcpu;

	hyp_vcpu = container_of(vcpu, struct pkvm_hyp_vcpu, vcpu);

	switch (fn) {
	case ARM_SMCCC_VERSION_FUNC_ID:
		/* Nothing to be handled by the host. Go back to the guest. */
		val[0] = ARM_SMCCC_VERSION_1_1;
		break;
	case ARM_SMCCC_VENDOR_HYP_CALL_UID_FUNC_ID:
		val[0] = ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_0;
		val[1] = ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_1;
		val[2] = ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_2;
		val[3] = ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_3;
		break;
	case ARM_SMCCC_VENDOR_HYP_KVM_FEATURES_FUNC_ID:
		val[0] = BIT(ARM_SMCCC_KVM_FUNC_FEATURES);
		val[0] |= BIT(ARM_SMCCC_KVM_FUNC_HYP_MEMINFO);
		val[0] |= BIT(ARM_SMCCC_KVM_FUNC_MEM_SHARE);
		val[0] |= BIT(ARM_SMCCC_KVM_FUNC_MEM_UNSHARE);
		val[0] |= BIT(ARM_SMCCC_KVM_FUNC_MEM_RELINQUISH);
		val[0] |= BIT(ARM_SMCCC_KVM_FUNC_MMIO_GUARD_INFO);
		val[0] |= BIT(ARM_SMCCC_KVM_FUNC_MMIO_GUARD_ENROLL);
		val[0] |= BIT(ARM_SMCCC_KVM_FUNC_MMIO_GUARD_MAP);
		val[0] |= BIT(ARM_SMCCC_KVM_FUNC_MMIO_GUARD_UNMAP);
		break;
	case ARM_SMCCC_VENDOR_HYP_KVM_MMIO_GUARD_ENROLL_FUNC_ID:
		set_bit(KVM_ARCH_FLAG_MMIO_GUARD, &vcpu->kvm->arch.flags);
		val[0] = SMCCC_RET_SUCCESS;
		break;
	case ARM_SMCCC_VENDOR_HYP_KVM_MMIO_GUARD_MAP_FUNC_ID:
		return pkvm_install_ioguard_page(hyp_vcpu, exit_code);
	case ARM_SMCCC_VENDOR_HYP_KVM_MMIO_GUARD_UNMAP_FUNC_ID:
		if (__pkvm_remove_ioguard_page(hyp_vcpu, vcpu_get_reg(vcpu, 1)))
			val[0] = SMCCC_RET_INVALID_PARAMETER;
		else
			val[0] = SMCCC_RET_SUCCESS;
		break;
	case ARM_SMCCC_VENDOR_HYP_KVM_MMIO_GUARD_INFO_FUNC_ID:
	case ARM_SMCCC_VENDOR_HYP_KVM_HYP_MEMINFO_FUNC_ID:
		return pkvm_meminfo_call(hyp_vcpu);
	case ARM_SMCCC_VENDOR_HYP_KVM_MEM_SHARE_FUNC_ID:
		return pkvm_memshare_call(hyp_vcpu, exit_code);
	case ARM_SMCCC_VENDOR_HYP_KVM_MEM_UNSHARE_FUNC_ID:
		return pkvm_memunshare_call(hyp_vcpu);
	case ARM_SMCCC_VENDOR_HYP_KVM_MEM_RELINQUISH_FUNC_ID:
		return pkvm_memrelinquish_call(hyp_vcpu);
	case ARM_SMCCC_TRNG_VERSION ... ARM_SMCCC_TRNG_RND32:
	case ARM_SMCCC_TRNG_RND64:
		if (smccc_trng_available)
			return pkvm_forward_trng(vcpu);
		break;
	default:
		return pkvm_handle_psci(hyp_vcpu);
	}

	smccc_set_retval(vcpu, val[0], val[1], val[2], val[3]);
	return true;
}

/*
 * Handler for non-protected VM HVC calls.
 *
 * Returns true if the hypervisor has handled the exit, and control should go
 * back to the guest, or false if it hasn't.
 */
bool kvm_hyp_handle_hvc64(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	u32 fn = smccc_get_function(vcpu);
	struct pkvm_hyp_vcpu *hyp_vcpu;

	hyp_vcpu = container_of(vcpu, struct pkvm_hyp_vcpu, vcpu);

	switch (fn) {
	case ARM_SMCCC_VENDOR_HYP_KVM_HYP_MEMINFO_FUNC_ID:
		return pkvm_meminfo_call(hyp_vcpu);
	case ARM_SMCCC_VENDOR_HYP_KVM_MEM_RELINQUISH_FUNC_ID:
		return pkvm_memrelinquish_call(hyp_vcpu);
	}

	return false;
}
