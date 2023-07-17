// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Google LLC
 * Author: Fuad Tabba <tabba@google.com>
 */

#include <linux/kvm_host.h>
#include <linux/mm.h>
#include <nvhe/fixed_config.h>
#include <nvhe/mem_protect.h>
#include <nvhe/memory.h>
#include <nvhe/pkvm.h>
#include <nvhe/trap_handler.h>

/* Used by icache_is_vpipt(). */
unsigned long __icache_flags;

/* Used by kvm_get_vttbr(). */
unsigned int kvm_arm_vmid_bits;

/*
 * Set trap register values based on features in ID_AA64PFR0.
 */
static void pvm_init_traps_aa64pfr0(struct kvm_vcpu *vcpu)
{
	const u64 feature_ids = pvm_read_id_reg(vcpu, SYS_ID_AA64PFR0_EL1);
	u64 hcr_set = HCR_RW;
	u64 hcr_clear = 0;
	u64 cptr_set = 0;
	u64 cptr_clear = 0;

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

	if (has_hvhe())
		hcr_set |= HCR_E2H;

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
	if (!FIELD_GET(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_SVE), feature_ids)) {
		if (has_hvhe())
			cptr_clear |= CPACR_EL1_ZEN_EL0EN | CPACR_EL1_ZEN_EL1EN;
		else
			cptr_set |= CPTR_EL2_TZ;
	}

	vcpu->arch.hcr_el2 |= hcr_set;
	vcpu->arch.hcr_el2 &= ~hcr_clear;
	vcpu->arch.cptr_el2 |= cptr_set;
	vcpu->arch.cptr_el2 &= ~cptr_clear;
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
		mdcr_set |= MDCR_EL2_TDRA | MDCR_EL2_TDA | MDCR_EL2_TDE;

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
	if (!FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_TraceVer), feature_ids)) {
		if (has_hvhe())
			cptr_set |= CPACR_EL1_TTA;
		else
			cptr_set |= CPTR_EL2_TTA;
	}

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
	const u64 hcr_trap_feat_regs = HCR_TID3;
	const u64 hcr_trap_impdef = HCR_TACR | HCR_TIDCP | HCR_TID1;

	/*
	 * Always trap:
	 * - Feature id registers: to control features exposed to guests
	 * - Implementation-defined features
	 */
	vcpu->arch.hcr_el2 |= hcr_trap_feat_regs | hcr_trap_impdef;

	/* Clear res0 and set res1 bits to trap potential new features. */
	vcpu->arch.hcr_el2 &= ~(HCR_RES0);
	vcpu->arch.mdcr_el2 &= ~(MDCR_EL2_RES0);
	if (!has_hvhe()) {
		vcpu->arch.cptr_el2 |= CPTR_NVHE_EL2_RES1;
		vcpu->arch.cptr_el2 &= ~(CPTR_NVHE_EL2_RES0);
	}
}

/*
 * Initialize trap register values for protected VMs.
 */
void __pkvm_vcpu_init_traps(struct kvm_vcpu *vcpu)
{
	pvm_init_trap_regs(vcpu);
	pvm_init_traps_aa64pfr0(vcpu);
	pvm_init_traps_aa64pfr1(vcpu);
	pvm_init_traps_aa64dfr0(vcpu);
	pvm_init_traps_aa64mmfr0(vcpu);
	pvm_init_traps_aa64mmfr1(vcpu);
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

	hyp_spin_lock(&vm_table_lock);
	hyp_vm = get_vm_by_handle(handle);
	if (!hyp_vm || hyp_vm->nr_vcpus <= vcpu_idx)
		goto unlock;

	hyp_vcpu = hyp_vm->vcpus[vcpu_idx];
	hyp_page_ref_inc(hyp_virt_to_page(hyp_vm));
unlock:
	hyp_spin_unlock(&vm_table_lock);
	return hyp_vcpu;
}

void pkvm_put_hyp_vcpu(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct pkvm_hyp_vm *hyp_vm = pkvm_hyp_vcpu_to_hyp_vm(hyp_vcpu);

	hyp_spin_lock(&vm_table_lock);
	hyp_page_ref_dec(hyp_virt_to_page(hyp_vm));
	hyp_spin_unlock(&vm_table_lock);
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
	hyp_vm->kvm.arch.vtcr = host_mmu.arch.vtcr;
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
	pgd_size = kvm_pgtable_stage2_pgd_size(host_mmu.arch.vtcr);

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

	if (ret)
		unmap_donated_memory(hyp_vcpu, sizeof(*hyp_vcpu));

	return ret;
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
