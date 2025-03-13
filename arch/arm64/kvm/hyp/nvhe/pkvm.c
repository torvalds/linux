// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Google LLC
 * Author: Fuad Tabba <tabba@google.com>
 */

#include <linux/kvm_host.h>
#include <linux/mm.h>

#include <asm/kvm_emulate.h>

#include <nvhe/mem_protect.h>
#include <nvhe/memory.h>
#include <nvhe/pkvm.h>
#include <nvhe/trap_handler.h>

/* Used by icache_is_aliasing(). */
unsigned long __icache_flags;

/* Used by kvm_get_vttbr(). */
unsigned int kvm_arm_vmid_bits;

unsigned int kvm_host_sve_max_vl;

/*
 * The currently loaded hyp vCPU for each physical CPU. Used only when
 * protected KVM is enabled, but for both protected and non-protected VMs.
 */
static DEFINE_PER_CPU(struct pkvm_hyp_vcpu *, loaded_hyp_vcpu);

static void pkvm_vcpu_reset_hcr(struct kvm_vcpu *vcpu)
{
	vcpu->arch.hcr_el2 = HCR_GUEST_FLAGS;

	if (has_hvhe())
		vcpu->arch.hcr_el2 |= HCR_E2H;

	if (cpus_have_final_cap(ARM64_HAS_RAS_EXTN)) {
		/* route synchronous external abort exceptions to EL2 */
		vcpu->arch.hcr_el2 |= HCR_TEA;
		/* trap error record accesses */
		vcpu->arch.hcr_el2 |= HCR_TERR;
	}

	if (cpus_have_final_cap(ARM64_HAS_STAGE2_FWB))
		vcpu->arch.hcr_el2 |= HCR_FWB;

	if (cpus_have_final_cap(ARM64_HAS_EVT) &&
	    !cpus_have_final_cap(ARM64_MISMATCHED_CACHE_TYPE))
		vcpu->arch.hcr_el2 |= HCR_TID4;
	else
		vcpu->arch.hcr_el2 |= HCR_TID2;

	if (vcpu_has_ptrauth(vcpu))
		vcpu->arch.hcr_el2 |= (HCR_API | HCR_APK);

	if (kvm_has_mte(vcpu->kvm))
		vcpu->arch.hcr_el2 |= HCR_ATA;
}

static void pvm_init_traps_hcr(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	u64 val = vcpu->arch.hcr_el2;

	/* No support for AArch32. */
	val |= HCR_RW;

	/*
	 * Always trap:
	 * - Feature id registers: to control features exposed to guests
	 * - Implementation-defined features
	 */
	val |= HCR_TACR | HCR_TIDCP | HCR_TID3 | HCR_TID1;

	if (!kvm_has_feat(kvm, ID_AA64PFR0_EL1, RAS, IMP)) {
		val |= HCR_TERR | HCR_TEA;
		val &= ~(HCR_FIEN);
	}

	if (!kvm_has_feat(kvm, ID_AA64PFR0_EL1, AMU, IMP))
		val &= ~(HCR_AMVOFFEN);

	if (!kvm_has_feat(kvm, ID_AA64PFR1_EL1, MTE, IMP)) {
		val |= HCR_TID5;
		val &= ~(HCR_DCT | HCR_ATA);
	}

	if (!kvm_has_feat(kvm, ID_AA64MMFR1_EL1, LO, IMP))
		val |= HCR_TLOR;

	vcpu->arch.hcr_el2 = val;
}

static void pvm_init_traps_mdcr(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	u64 val = vcpu->arch.mdcr_el2;

	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, PMUVer, IMP)) {
		val |= MDCR_EL2_TPM | MDCR_EL2_TPMCR;
		val &= ~(MDCR_EL2_HPME | MDCR_EL2_MTPME | MDCR_EL2_HPMN_MASK);
	}

	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, DebugVer, IMP))
		val |= MDCR_EL2_TDRA | MDCR_EL2_TDA;

	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, DoubleLock, IMP))
		val |= MDCR_EL2_TDOSA;

	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, PMSVer, IMP)) {
		val |= MDCR_EL2_TPMS;
		val &= ~MDCR_EL2_E2PB_MASK;
	}

	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, TraceFilt, IMP))
		val |= MDCR_EL2_TTRF;

	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, ExtTrcBuff, IMP))
		val |= MDCR_EL2_E2TB_MASK;

	/* Trap Debug Communications Channel registers */
	if (!kvm_has_feat(kvm, ID_AA64MMFR0_EL1, FGT, IMP))
		val |= MDCR_EL2_TDCC;

	vcpu->arch.mdcr_el2 = val;
}

/*
 * Check that cpu features that are neither trapped nor supported are not
 * enabled for protected VMs.
 */
static int pkvm_check_pvm_cpu_features(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;

	/* Protected KVM does not support AArch32 guests. */
	if (kvm_has_feat(kvm, ID_AA64PFR0_EL1, EL0, AARCH32) ||
	    kvm_has_feat(kvm, ID_AA64PFR0_EL1, EL1, AARCH32))
		return -EINVAL;

	/*
	 * Linux guests assume support for floating-point and Advanced SIMD. Do
	 * not change the trapping behavior for these from the KVM default.
	 */
	if (!kvm_has_feat(kvm, ID_AA64PFR0_EL1, FP, IMP) ||
	    !kvm_has_feat(kvm, ID_AA64PFR0_EL1, AdvSIMD, IMP))
		return -EINVAL;

	/* No SME support in KVM right now. Check to catch if it changes. */
	if (kvm_has_feat(kvm, ID_AA64PFR1_EL1, SME, IMP))
		return -EINVAL;

	return 0;
}

/*
 * Initialize trap register values in protected mode.
 */
static int pkvm_vcpu_init_traps(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	int ret;

	vcpu->arch.mdcr_el2 = 0;

	pkvm_vcpu_reset_hcr(vcpu);

	if ((!pkvm_hyp_vcpu_is_protected(hyp_vcpu)))
		return 0;

	ret = pkvm_check_pvm_cpu_features(vcpu);
	if (ret)
		return ret;

	pvm_init_traps_hcr(vcpu);
	pvm_init_traps_mdcr(vcpu);

	return 0;
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
 * to 'vm_table', 'nr_table_entries', and other per-vm state on initialization.
 * Also protects reads and writes to 'last_hyp_vcpu_lookup'.
 */
DEFINE_HYP_SPINLOCK(vm_table_lock);

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
	if (!hyp_vm || hyp_vm->nr_vcpus <= vcpu_idx)
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

struct pkvm_hyp_vm *get_pkvm_hyp_vm(pkvm_handle_t handle)
{
	struct pkvm_hyp_vm *hyp_vm;

	hyp_spin_lock(&vm_table_lock);
	hyp_vm = get_vm_by_handle(handle);
	if (hyp_vm)
		hyp_page_ref_inc(hyp_virt_to_page(hyp_vm));
	hyp_spin_unlock(&vm_table_lock);

	return hyp_vm;
}

void put_pkvm_hyp_vm(struct pkvm_hyp_vm *hyp_vm)
{
	hyp_spin_lock(&vm_table_lock);
	hyp_page_ref_dec(hyp_virt_to_page(hyp_vm));
	hyp_spin_unlock(&vm_table_lock);
}

struct pkvm_hyp_vm *get_np_pkvm_hyp_vm(pkvm_handle_t handle)
{
	struct pkvm_hyp_vm *hyp_vm = get_pkvm_hyp_vm(handle);

	if (hyp_vm && pkvm_hyp_vm_is_protected(hyp_vm)) {
		put_pkvm_hyp_vm(hyp_vm);
		hyp_vm = NULL;
	}

	return hyp_vm;
}

static void pkvm_init_features_from_host(struct pkvm_hyp_vm *hyp_vm, const struct kvm *host_kvm)
{
	struct kvm *kvm = &hyp_vm->kvm;
	unsigned long host_arch_flags = READ_ONCE(host_kvm->arch.flags);
	DECLARE_BITMAP(allowed_features, KVM_VCPU_MAX_FEATURES);

	if (test_bit(KVM_ARCH_FLAG_MTE_ENABLED, &host_kvm->arch.flags))
		set_bit(KVM_ARCH_FLAG_MTE_ENABLED, &kvm->arch.flags);

	/* No restrictions for non-protected VMs. */
	if (!kvm_vm_is_protected(kvm)) {
		hyp_vm->kvm.arch.flags = host_arch_flags;

		bitmap_copy(kvm->arch.vcpu_features,
			    host_kvm->arch.vcpu_features,
			    KVM_VCPU_MAX_FEATURES);
		return;
	}

	bitmap_zero(allowed_features, KVM_VCPU_MAX_FEATURES);

	set_bit(KVM_ARM_VCPU_PSCI_0_2, allowed_features);

	if (kvm_pvm_ext_allowed(KVM_CAP_ARM_PMU_V3))
		set_bit(KVM_ARM_VCPU_PMU_V3, allowed_features);

	if (kvm_pvm_ext_allowed(KVM_CAP_ARM_PTRAUTH_ADDRESS))
		set_bit(KVM_ARM_VCPU_PTRAUTH_ADDRESS, allowed_features);

	if (kvm_pvm_ext_allowed(KVM_CAP_ARM_PTRAUTH_GENERIC))
		set_bit(KVM_ARM_VCPU_PTRAUTH_GENERIC, allowed_features);

	if (kvm_pvm_ext_allowed(KVM_CAP_ARM_SVE)) {
		set_bit(KVM_ARM_VCPU_SVE, allowed_features);
		kvm->arch.flags |= host_arch_flags & BIT(KVM_ARCH_FLAG_GUEST_HAS_SVE);
	}

	bitmap_and(kvm->arch.vcpu_features, host_kvm->arch.vcpu_features,
		   allowed_features, KVM_VCPU_MAX_FEATURES);
}

static void unpin_host_vcpu(struct kvm_vcpu *host_vcpu)
{
	if (host_vcpu)
		hyp_unpin_shared_mem(host_vcpu, host_vcpu + 1);
}

static void unpin_host_vcpus(struct pkvm_hyp_vcpu *hyp_vcpus[],
			     unsigned int nr_vcpus)
{
	int i;

	for (i = 0; i < nr_vcpus; i++)
		unpin_host_vcpu(hyp_vcpus[i]->host_vcpu);
}

static void init_pkvm_hyp_vm(struct kvm *host_kvm, struct pkvm_hyp_vm *hyp_vm,
			     unsigned int nr_vcpus)
{
	hyp_vm->host_kvm = host_kvm;
	hyp_vm->kvm.created_vcpus = nr_vcpus;
	hyp_vm->kvm.arch.mmu.vtcr = host_mmu.arch.mmu.vtcr;
	hyp_vm->kvm.arch.pkvm.enabled = READ_ONCE(host_kvm->arch.pkvm.enabled);
	hyp_vm->kvm.arch.flags = 0;
	pkvm_init_features_from_host(hyp_vm, host_kvm);
}

static void pkvm_vcpu_init_sve(struct pkvm_hyp_vcpu *hyp_vcpu, struct kvm_vcpu *host_vcpu)
{
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;

	if (!vcpu_has_feature(vcpu, KVM_ARM_VCPU_SVE))
		vcpu_clear_flag(vcpu, VCPU_SVE_FINALIZED);
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

	if (pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		kvm_init_pvm_id_regs(&hyp_vcpu->vcpu);

	ret = pkvm_vcpu_init_traps(hyp_vcpu);
	if (ret)
		goto done;

	pkvm_vcpu_init_sve(hyp_vcpu, host_vcpu);
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
 *
 * Return a unique handle to the protected VM on success,
 * negative error code on failure.
 */
int __pkvm_init_vm(struct kvm *host_kvm, unsigned long vm_hva,
		   unsigned long pgd_hva)
{
	struct pkvm_hyp_vm *hyp_vm = NULL;
	size_t vm_size, pgd_size;
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
	pgd_size = kvm_pgtable_stage2_pgd_size(host_mmu.arch.mmu.vtcr);

	ret = -ENOMEM;

	hyp_vm = map_donated_memory(vm_hva, vm_size);
	if (!hyp_vm)
		goto err_remove_mappings;

	pgd = map_donated_memory_noclear(pgd_hva, pgd_size);
	if (!pgd)
		goto err_remove_mappings;

	init_pkvm_hyp_vm(host_kvm, hyp_vm, nr_vcpus);

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

	if (ret) {
		unmap_donated_memory(hyp_vcpu, sizeof(*hyp_vcpu));
		return ret;
	}

	return 0;
}

static void
teardown_donated_memory(struct kvm_hyp_memcache *mc, void *addr, size_t size)
{
	size = PAGE_ALIGN(size);
	memset(addr, 0, size);

	for (void *start = addr; start < addr + size; start += PAGE_SIZE)
		push_hyp_memcache(mc, start, hyp_virt_to_phys);

	unmap_donated_memory_noclear(addr, size);
}

int __pkvm_teardown_vm(pkvm_handle_t handle)
{
	struct kvm_hyp_memcache *mc;
	struct pkvm_hyp_vm *hyp_vm;
	struct kvm *host_kvm;
	unsigned int idx;
	size_t vm_size;
	int err;

	hyp_spin_lock(&vm_table_lock);
	hyp_vm = get_vm_by_handle(handle);
	if (!hyp_vm) {
		err = -ENOENT;
		goto err_unlock;
	}

	if (WARN_ON(hyp_page_count(hyp_vm))) {
		err = -EBUSY;
		goto err_unlock;
	}

	host_kvm = hyp_vm->host_kvm;

	/* Ensure the VMID is clean before it can be reallocated */
	__kvm_tlb_flush_vmid(&hyp_vm->kvm.arch.mmu);
	remove_vm_table_entry(handle);
	hyp_spin_unlock(&vm_table_lock);

	/* Reclaim guest pages (including page-table pages) */
	mc = &host_kvm->arch.pkvm.teardown_mc;
	reclaim_guest_pages(hyp_vm, mc);
	unpin_host_vcpus(hyp_vm->vcpus, hyp_vm->nr_vcpus);

	/* Push the metadata pages to the teardown memcache */
	for (idx = 0; idx < hyp_vm->nr_vcpus; ++idx) {
		struct pkvm_hyp_vcpu *hyp_vcpu = hyp_vm->vcpus[idx];
		struct kvm_hyp_memcache *vcpu_mc = &hyp_vcpu->vcpu.arch.pkvm_memcache;

		while (vcpu_mc->nr_pages) {
			void *addr = pop_hyp_memcache(vcpu_mc, hyp_phys_to_virt);

			push_hyp_memcache(mc, addr, hyp_virt_to_phys);
			unmap_donated_memory_noclear(addr, PAGE_SIZE);
		}

		teardown_donated_memory(mc, hyp_vcpu, sizeof(*hyp_vcpu));
	}

	vm_size = pkvm_get_hyp_vm_size(hyp_vm->kvm.created_vcpus);
	teardown_donated_memory(mc, hyp_vm, vm_size);
	hyp_unpin_shared_mem(host_kvm, host_kvm + 1);
	return 0;

err_unlock:
	hyp_spin_unlock(&vm_table_lock);
	return err;
}
