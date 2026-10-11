// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 - Google LLC
 * Author: Quentin Perret <qperret@google.com>
 */
#ifndef __ARM64_KVM_PKVM_H__
#define __ARM64_KVM_PKVM_H__

#include <linux/arm_ffa.h>
#include <linux/kvm_host.h>
#include <linux/memblock.h>
#include <linux/scatterlist.h>
#include <asm/kvm_pgtable.h>

/* Maximum number of VMs that can co-exist under pKVM. */
#define KVM_MAX_PVMS 255

#define HYP_MEMBLOCK_REGIONS 128

void pkvm_selftests(void);
int pkvm_init_host_vm(struct kvm *kvm);
int pkvm_create_hyp_vm(struct kvm *kvm);
bool pkvm_hyp_vm_is_created(struct kvm *kvm);
void pkvm_destroy_hyp_vm(struct kvm *kvm);
int pkvm_create_hyp_vcpu(struct kvm_vcpu *vcpu);

/*
 * Check whether the specific capability is allowed in pKVM.
 *
 * Certain features are allowed only for non-protected VMs in pKVM, which is why
 * this takes the VM (kvm) as a parameter.
 */
static inline bool kvm_pkvm_ext_allowed(struct kvm *kvm, long ext)
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
	case KVM_CAP_ARM_PTRAUTH_ADDRESS:
	case KVM_CAP_ARM_PTRAUTH_GENERIC:
	case KVM_CAP_ONE_REG:
	case KVM_CAP_MP_STATE:
	case KVM_CAP_VCPU_EVENTS:
	case KVM_CAP_VCPU_ATTRIBUTES:
	case KVM_CAP_IMMEDIATE_EXIT:
	case KVM_CAP_IOEVENTFD:
	case KVM_CAP_IRQFD_RESAMPLE:
	case KVM_CAP_ARM_IRQ_LINE_LAYOUT_2:
	case KVM_CAP_ARM_INJECT_SERROR_ESR:
		return true;
	case KVM_CAP_ARM_MTE:
	case KVM_CAP_ARM_EAGER_SPLIT_CHUNK_SIZE:
	case KVM_CAP_ARM_SUPPORTED_BLOCK_SIZES:
	case KVM_CAP_PRE_FAULT_MEMORY:
		return false;
	default:
		return !kvm || (kvm->arch.vm_flavor == VM_PKVM);
	}
}

/*
 * The vCPU features a protected VM may use: checked by the host at
 * KVM_ARM_VCPU_INIT, applied by EL2 when the hyp VM is created.
 */
static inline void kvm_pkvm_vcpu_allowed_features(struct kvm *kvm,
						  unsigned long *allowed)
{
	bitmap_zero(allowed, KVM_VCPU_MAX_FEATURES);

	set_bit(KVM_ARM_VCPU_PSCI_0_2, allowed);

	if (kvm_pkvm_ext_allowed(kvm, KVM_CAP_ARM_PMU_V3))
		set_bit(KVM_ARM_VCPU_PMU_V3, allowed);

	if (kvm_pkvm_ext_allowed(kvm, KVM_CAP_ARM_PTRAUTH_ADDRESS))
		set_bit(KVM_ARM_VCPU_PTRAUTH_ADDRESS, allowed);

	if (kvm_pkvm_ext_allowed(kvm, KVM_CAP_ARM_PTRAUTH_GENERIC))
		set_bit(KVM_ARM_VCPU_PTRAUTH_GENERIC, allowed);

	if (kvm_pkvm_ext_allowed(kvm, KVM_CAP_ARM_SVE))
		set_bit(KVM_ARM_VCPU_SVE, allowed);
}

/*
 * Check whether the KVM VM IOCTL is allowed in pKVM.
 *
 * Certain features are allowed only for non-protected VMs in pKVM, which is why
 * this takes the VM (kvm) as a parameter.
 */
static inline bool kvm_pkvm_ioctl_allowed(struct kvm *kvm, unsigned int ioctl)
{
	long ext;
	int r;

	r = kvm_get_cap_for_kvm_ioctl(ioctl, &ext);
	if (r < 0)
		return false;

	return kvm_pkvm_ext_allowed(kvm, ext);
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

#ifdef CONFIG_NVHE_EL2_DEBUG
static inline unsigned long pkvm_selftest_pages(void) { return 32; }
#else
static inline unsigned long pkvm_selftest_pages(void) { return 0; }
#endif

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

	return SVE_SIG_REGS_SIZE(sve_vq_from_vl(kvm_host_sve_max_vl));
}

struct pkvm_mapping {
	struct rb_node node;
	u64 gfn;
	u64 pfn;
	struct {
		u64 nr_pages:48;
		u64 nc:1;
	};
	u64 __subtree_last;	/* Internal member for interval tree */
};

enum pkvm_hyp_req_type {
	PKVM_HYP_NO_REQ = 0,
	PKVM_HYP_REQ_HYP_ALLOC,
	PKVM_HYP_REQ_HYP_ALLOC_SELFTEST,
	__PKVM_HYP_REQ_TYPE_MAX,
};

#define PKVM_HYP_REQ_SMCCC_ARG_SIZE_MAX \
	(sizeof(struct arm_smccc_res) - offsetof(struct arm_smccc_res, a2) - 1)

struct pkvm_hyp_req {
	u8 type;
	union {
		struct {
			u32	nr_pages;
		} mem;
		struct {
			/* Helper for SMCCC encoding/decoding */
			u8	args[PKVM_HYP_REQ_SMCCC_ARG_SIZE_MAX];
		} args;
	};
};

static inline size_t pkvm_hyp_req_arg_size(u8 type)
{
	struct pkvm_hyp_req *req;

	switch (type) {
	case PKVM_HYP_NO_REQ:
		return 0;
	case PKVM_HYP_REQ_HYP_ALLOC:
	case PKVM_HYP_REQ_HYP_ALLOC_SELFTEST:
		return sizeof(req->mem);
	default:
		WARN_ON(1);
	}

	return 0;
}

/* Encode the pending pkvm_hyp_req type into the SMCCC args */
static inline void
pkvm_hyp_req_to_smccc(struct kvm_cpu_context *host_ctxt, struct pkvm_hyp_req *req)
{
	u8 *dst, type = req->type;
	size_t size;

	if (type == PKVM_HYP_NO_REQ || type >= __PKVM_HYP_REQ_TYPE_MAX) {
		host_ctxt->regs.regs[2] = 0;
		return;
	}

	size = pkvm_hyp_req_arg_size(type);
	if (WARN_ON(size > PKVM_HYP_REQ_SMCCC_ARG_SIZE_MAX))
		return;

	dst = (u8 *)&host_ctxt->regs.regs[2];
	*dst = type;

	memcpy(dst + 1, &req->args, size);
}

/* Return true if a pkvm_hyp_req has been decoded from the SMCCC args */
static inline bool smccc_to_pkvm_hyp_req(struct pkvm_hyp_req *req, struct arm_smccc_res *res)
{
	u8 *src = (u8 *)res + offsetof(struct arm_smccc_res, a2);
	u8 type = *src;

	if (type == PKVM_HYP_NO_REQ || type >= __PKVM_HYP_REQ_TYPE_MAX)
		return false;

	req->type = type;
	memcpy(&req->args, src + 1, pkvm_hyp_req_arg_size(type));

	return true;
}

int __pkvm_handle_smccc_req(struct arm_smccc_res *res);

/**
 * pkvm_call_hyp_req() - Issue an HVC that can return hypervisor requests
 * @f: Hypervisor function symbol to call.
 * @...: Arguments to pass to the hypercall.
 *
 * Re-issue an HVC and process any pending hypervisor request until completion
 * or error.
 *
 * Only use this helper for HVCs whose hypervisor handlers format their return
 * registers with pkvm_hyp_req_to_smccc().
 *
 * Return: Result of the hypercall or a negative error if the hyp request
 * handling failed.
 */
#define pkvm_call_hyp_req(f, ...)								\
({												\
	struct arm_smccc_res __res;								\
	int __ret;										\
	do {											\
		__ret = -1;									\
		arm_smccc_1_1_hvc(KVM_HOST_SMCCC_FUNC(f), ##__VA_ARGS__, &__res);		\
		if (WARN_ON(__res.a0 != SMCCC_RET_SUCCESS))					\
			break;									\
		__ret = __res.a1;								\
		if (!__ret)									\
			break;									\
		__ret = __pkvm_handle_smccc_req(&__res);					\
	} while (!__ret);									\
	__ret;											\
})

int pkvm_pgtable_stage2_init(struct kvm_pgtable *pgt, struct kvm_s2_mmu *mmu,
			     struct kvm_pgtable_mm_ops *mm_ops);
void pkvm_pgtable_stage2_destroy_range(struct kvm_pgtable *pgt,
					u64 addr, u64 size);
void pkvm_pgtable_stage2_destroy_pgd(struct kvm_pgtable *pgt);
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
