// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 - Google LLC
 * Author: Quentin Perret <qperret@google.com>
 */
#ifndef __ARM64_KVM_PKVM_H__
#define __ARM64_KVM_PKVM_H__

#include <linux/arm_ffa.h>
#include <linux/memblock.h>
#include <linux/scatterlist.h>
#include <asm/kvm_pgtable.h>

/* Maximum number of VMs that can co-exist under pKVM. */
#define KVM_MAX_PVMS 255

#define HYP_MEMBLOCK_REGIONS 128

int pkvm_init_host_vm(struct kvm *kvm);
int pkvm_create_hyp_vm(struct kvm *kvm);
void pkvm_destroy_hyp_vm(struct kvm *kvm);

/*
 * This functions as an allow-list of protected VM capabilities.
 * Features not explicitly allowed by this function are denied.
 */
static inline bool kvm_pvm_ext_allowed(long ext)
{
	switch (ext) {
	case KVM_CAP_IRQCHIP:
	case KVM_CAP_ARM_PSCI:
	case KVM_CAP_ARM_PSCI_0_2:
	case KVM_CAP_NR_VCPUS:
	case KVM_CAP_MAX_VCPUS:
	case KVM_CAP_MAX_VCPU_ID:
	case KVM_CAP_MSI_DEVID:
	case KVM_CAP_ARM_VM_IPA_SIZE:
	case KVM_CAP_ARM_PMU_V3:
	case KVM_CAP_ARM_SVE:
	case KVM_CAP_ARM_PTRAUTH_ADDRESS:
	case KVM_CAP_ARM_PTRAUTH_GENERIC:
		return true;
	default:
		return false;
	}
}

extern struct memblock_region kvm_nvhe_sym(hyp_memory)[];
extern unsigned int kvm_nvhe_sym(hyp_memblock_nr);

static inline unsigned long
hyp_vmemmap_memblock_size(struct memblock_region *reg, size_t vmemmap_entry_size)
{
	unsigned long nr_pages = reg->size >> PAGE_SHIFT;
	unsigned long start, end;

	start = (reg->base >> PAGE_SHIFT) * vmemmap_entry_size;
	end = start + nr_pages * vmemmap_entry_size;
	start = ALIGN_DOWN(start, PAGE_SIZE);
	end = ALIGN(end, PAGE_SIZE);

	return end - start;
}

static inline unsigned long hyp_vmemmap_pages(size_t vmemmap_entry_size)
{
	unsigned long res = 0, i;

	for (i = 0; i < kvm_nvhe_sym(hyp_memblock_nr); i++) {
		res += hyp_vmemmap_memblock_size(&kvm_nvhe_sym(hyp_memory)[i],
						 vmemmap_entry_size);
	}

	return res >> PAGE_SHIFT;
}

static inline unsigned long hyp_vm_table_pages(void)
{
	return PAGE_ALIGN(KVM_MAX_PVMS * sizeof(void *)) >> PAGE_SHIFT;
}

static inline unsigned long __hyp_pgtable_max_pages(unsigned long nr_pages)
{
	unsigned long total = 0;
	int i;

	/* Provision the worst case scenario */
	for (i = KVM_PGTABLE_FIRST_LEVEL; i <= KVM_PGTABLE_LAST_LEVEL; i++) {
		nr_pages = DIV_ROUND_UP(nr_pages, PTRS_PER_PTE);
		total += nr_pages;
	}

	return total;
}

static inline unsigned long __hyp_pgtable_total_pages(void)
{
	unsigned long res = 0, i;

	/* Cover all of memory with page-granularity */
	for (i = 0; i < kvm_nvhe_sym(hyp_memblock_nr); i++) {
		struct memblock_region *reg = &kvm_nvhe_sym(hyp_memory)[i];
		res += __hyp_pgtable_max_pages(reg->size >> PAGE_SHIFT);
	}

	return res;
}

static inline unsigned long hyp_s1_pgtable_pages(void)
{
	unsigned long res;

	res = __hyp_pgtable_total_pages();

	/* Allow 1 GiB for private mappings */
	res += __hyp_pgtable_max_pages(SZ_1G >> PAGE_SHIFT);

	return res;
}

static inline unsigned long host_s2_pgtable_pages(void)
{
	unsigned long res;

	/*
	 * Include an extra 16 pages to safely upper-bound the worst case of
	 * concatenated pgds.
	 */
	res = __hyp_pgtable_total_pages() + 16;

	/* Allow 1 GiB for MMIO mappings */
	res += __hyp_pgtable_max_pages(SZ_1G >> PAGE_SHIFT);

	return res;
}

#define KVM_FFA_MBOX_NR_PAGES	1

static inline unsigned long hyp_ffa_proxy_pages(void)
{
	size_t desc_max;

	/*
	 * The hypervisor FFA proxy needs enough memory to buffer a fragmented
	 * descriptor returned from EL3 in response to a RETRIEVE_REQ call.
	 */
	desc_max = sizeof(struct ffa_mem_region) +
		   sizeof(struct ffa_mem_region_attributes) +
		   sizeof(struct ffa_composite_mem_region) +
		   SG_MAX_SEGMENTS * sizeof(struct ffa_mem_region_addr_range);

	/* Plus a page each for the hypervisor's RX and TX mailboxes. */
	return (2 * KVM_FFA_MBOX_NR_PAGES) + DIV_ROUND_UP(desc_max, PAGE_SIZE);
}

static inline size_t pkvm_host_sve_state_size(void)
{
	if (!system_supports_sve())
		return 0;

	return size_add(sizeof(struct cpu_sve_state),
			SVE_SIG_REGS_SIZE(sve_vq_from_vl(kvm_host_sve_max_vl)));
}

struct pkvm_mapping {
	struct rb_node node;
	u64 gfn;
	u64 pfn;
};

int pkvm_pgtable_stage2_init(struct kvm_pgtable *pgt, struct kvm_s2_mmu *mmu,
			     struct kvm_pgtable_mm_ops *mm_ops);
void pkvm_pgtable_stage2_destroy(struct kvm_pgtable *pgt);
int pkvm_pgtable_stage2_map(struct kvm_pgtable *pgt, u64 addr, u64 size, u64 phys,
			    enum kvm_pgtable_prot prot, void *mc,
			    enum kvm_pgtable_walk_flags flags);
int pkvm_pgtable_stage2_unmap(struct kvm_pgtable *pgt, u64 addr, u64 size);
int pkvm_pgtable_stage2_wrprotect(struct kvm_pgtable *pgt, u64 addr, u64 size);
int pkvm_pgtable_stage2_flush(struct kvm_pgtable *pgt, u64 addr, u64 size);
bool pkvm_pgtable_stage2_test_clear_young(struct kvm_pgtable *pgt, u64 addr, u64 size, bool mkold);
int pkvm_pgtable_stage2_relax_perms(struct kvm_pgtable *pgt, u64 addr, enum kvm_pgtable_prot prot,
				    enum kvm_pgtable_walk_flags flags);
void pkvm_pgtable_stage2_mkyoung(struct kvm_pgtable *pgt, u64 addr,
				 enum kvm_pgtable_walk_flags flags);
int pkvm_pgtable_stage2_split(struct kvm_pgtable *pgt, u64 addr, u64 size,
			      struct kvm_mmu_memory_cache *mc);
void pkvm_pgtable_stage2_free_unlinked(struct kvm_pgtable_mm_ops *mm_ops, void *pgtable, s8 level);
kvm_pte_t *pkvm_pgtable_stage2_create_unlinked(struct kvm_pgtable *pgt, u64 phys, s8 level,
					       enum kvm_pgtable_prot prot, void *mc,
					       bool force_pte);
#endif	/* __ARM64_KVM_PKVM_H__ */
