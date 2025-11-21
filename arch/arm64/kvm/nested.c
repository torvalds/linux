// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 - Columbia University and Linaro Ltd.
 * Author: Jintack Lim <jintack.lim@linaro.org>
 */

#include <linux/bitfield.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>

#include <asm/fixmap.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_nested.h>
#include <asm/sysreg.h>

#include "sys_regs.h"

struct vncr_tlb {
	/* The guest's VNCR_EL2 */
	u64			gva;
	struct s1_walk_info	wi;
	struct s1_walk_result	wr;

	u64			hpa;

	/* -1 when not mapped on a CPU */
	int			cpu;

	/*
	 * true if the TLB is valid. Can only be changed with the
	 * mmu_lock held.
	 */
	bool			valid;
};

/*
 * Ratio of live shadow S2 MMU per vcpu. This is a trade-off between
 * memory usage and potential number of different sets of S2 PTs in
 * the guests. Running out of S2 MMUs only affects performance (we
 * will invalidate them more often).
 */
#define S2_MMU_PER_VCPU		2

void kvm_init_nested(struct kvm *kvm)
{
	kvm->arch.nested_mmus = NULL;
	kvm->arch.nested_mmus_size = 0;
	atomic_set(&kvm->arch.vncr_map_count, 0);
}

static int init_nested_s2_mmu(struct kvm *kvm, struct kvm_s2_mmu *mmu)
{
	/*
	 * We only initialise the IPA range on the canonical MMU, which
	 * defines the contract between KVM and userspace on where the
	 * "hardware" is in the IPA space. This affects the validity of MMIO
	 * exits forwarded to userspace, for example.
	 *
	 * For nested S2s, we use the PARange as exposed to the guest, as it
	 * is allowed to use it at will to expose whatever memory map it
	 * wants to its own guests as it would be on real HW.
	 */
	return kvm_init_stage2_mmu(kvm, mmu, kvm_get_pa_bits(kvm));
}

int kvm_vcpu_init_nested(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_s2_mmu *tmp;
	int num_mmus, ret = 0;

	if (test_bit(KVM_ARM_VCPU_HAS_EL2_E2H0, kvm->arch.vcpu_features) &&
	    !cpus_have_final_cap(ARM64_HAS_HCR_NV1))
		return -EINVAL;

	if (!vcpu->arch.ctxt.vncr_array)
		vcpu->arch.ctxt.vncr_array = (u64 *)__get_free_page(GFP_KERNEL_ACCOUNT |
								    __GFP_ZERO);

	if (!vcpu->arch.ctxt.vncr_array)
		return -ENOMEM;

	/*
	 * Let's treat memory allocation failures as benign: If we fail to
	 * allocate anything, return an error and keep the allocated array
	 * alive. Userspace may try to recover by intializing the vcpu
	 * again, and there is no reason to affect the whole VM for this.
	 */
	num_mmus = atomic_read(&kvm->online_vcpus) * S2_MMU_PER_VCPU;
	tmp = kvrealloc(kvm->arch.nested_mmus,
			size_mul(sizeof(*kvm->arch.nested_mmus), num_mmus),
			GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!tmp)
		return -ENOMEM;

	swap(kvm->arch.nested_mmus, tmp);

	/*
	 * If we went through a realocation, adjust the MMU back-pointers in
	 * the previously initialised kvm_pgtable structures.
	 */
	if (kvm->arch.nested_mmus != tmp)
		for (int i = 0; i < kvm->arch.nested_mmus_size; i++)
			kvm->arch.nested_mmus[i].pgt->mmu = &kvm->arch.nested_mmus[i];

	for (int i = kvm->arch.nested_mmus_size; !ret && i < num_mmus; i++)
		ret = init_nested_s2_mmu(kvm, &kvm->arch.nested_mmus[i]);

	if (ret) {
		for (int i = kvm->arch.nested_mmus_size; i < num_mmus; i++)
			kvm_free_stage2_pgd(&kvm->arch.nested_mmus[i]);

		free_page((unsigned long)vcpu->arch.ctxt.vncr_array);
		vcpu->arch.ctxt.vncr_array = NULL;

		return ret;
	}

	kvm->arch.nested_mmus_size = num_mmus;

	return 0;
}

struct s2_walk_info {
	int	     (*read_desc)(phys_addr_t pa, u64 *desc, void *data);
	void	     *data;
	u64	     baddr;
	unsigned int max_oa_bits;
	unsigned int pgshift;
	unsigned int sl;
	unsigned int t0sz;
	bool	     be;
};

static u32 compute_fsc(int level, u32 fsc)
{
	return fsc | (level & 0x3);
}

static int esr_s2_fault(struct kvm_vcpu *vcpu, int level, u32 fsc)
{
	u32 esr;

	esr = kvm_vcpu_get_esr(vcpu) & ~ESR_ELx_FSC;
	esr |= compute_fsc(level, fsc);
	return esr;
}

static int get_ia_size(struct s2_walk_info *wi)
{
	return 64 - wi->t0sz;
}

static int check_base_s2_limits(struct s2_walk_info *wi,
				int level, int input_size, int stride)
{
	int start_size, ia_size;

	ia_size = get_ia_size(wi);

	/* Check translation limits */
	switch (BIT(wi->pgshift)) {
	case SZ_64K:
		if (level == 0 || (level == 1 && ia_size <= 42))
			return -EFAULT;
		break;
	case SZ_16K:
		if (level == 0 || (level == 1 && ia_size <= 40))
			return -EFAULT;
		break;
	case SZ_4K:
		if (level < 0 || (level == 0 && ia_size <= 42))
			return -EFAULT;
		break;
	}

	/* Check input size limits */
	if (input_size > ia_size)
		return -EFAULT;

	/* Check number of entries in starting level table */
	start_size = input_size - ((3 - level) * stride + wi->pgshift);
	if (start_size < 1 || start_size > stride + 4)
		return -EFAULT;

	return 0;
}

/* Check if output is within boundaries */
static int check_output_size(struct s2_walk_info *wi, phys_addr_t output)
{
	unsigned int output_size = wi->max_oa_bits;

	if (output_size != 48 && (output & GENMASK_ULL(47, output_size)))
		return -1;

	return 0;
}

/*
 * This is essentially a C-version of the pseudo code from the ARM ARM
 * AArch64.TranslationTableWalk  function.  I strongly recommend looking at
 * that pseudocode in trying to understand this.
 *
 * Must be called with the kvm->srcu read lock held
 */
static int walk_nested_s2_pgd(phys_addr_t ipa,
			      struct s2_walk_info *wi, struct kvm_s2_trans *out)
{
	int first_block_level, level, stride, input_size, base_lower_bound;
	phys_addr_t base_addr;
	unsigned int addr_top, addr_bottom;
	u64 desc;  /* page table entry */
	int ret;
	phys_addr_t paddr;

	switch (BIT(wi->pgshift)) {
	default:
	case SZ_64K:
	case SZ_16K:
		level = 3 - wi->sl;
		first_block_level = 2;
		break;
	case SZ_4K:
		level = 2 - wi->sl;
		first_block_level = 1;
		break;
	}

	stride = wi->pgshift - 3;
	input_size = get_ia_size(wi);
	if (input_size > 48 || input_size < 25)
		return -EFAULT;

	ret = check_base_s2_limits(wi, level, input_size, stride);
	if (WARN_ON(ret))
		return ret;

	base_lower_bound = 3 + input_size - ((3 - level) * stride +
			   wi->pgshift);
	base_addr = wi->baddr & GENMASK_ULL(47, base_lower_bound);

	if (check_output_size(wi, base_addr)) {
		out->esr = compute_fsc(level, ESR_ELx_FSC_ADDRSZ);
		return 1;
	}

	addr_top = input_size - 1;

	while (1) {
		phys_addr_t index;

		addr_bottom = (3 - level) * stride + wi->pgshift;
		index = (ipa & GENMASK_ULL(addr_top, addr_bottom))
			>> (addr_bottom - 3);

		paddr = base_addr | index;
		ret = wi->read_desc(paddr, &desc, wi->data);
		if (ret < 0)
			return ret;

		/*
		 * Handle reversedescriptors if endianness differs between the
		 * host and the guest hypervisor.
		 */
		if (wi->be)
			desc = be64_to_cpu((__force __be64)desc);
		else
			desc = le64_to_cpu((__force __le64)desc);

		/* Check for valid descriptor at this point */
		if (!(desc & 1) || ((desc & 3) == 1 && level == 3)) {
			out->esr = compute_fsc(level, ESR_ELx_FSC_FAULT);
			out->desc = desc;
			return 1;
		}

		/* We're at the final level or block translation level */
		if ((desc & 3) == 1 || level == 3)
			break;

		if (check_output_size(wi, desc)) {
			out->esr = compute_fsc(level, ESR_ELx_FSC_ADDRSZ);
			out->desc = desc;
			return 1;
		}

		base_addr = desc & GENMASK_ULL(47, wi->pgshift);

		level += 1;
		addr_top = addr_bottom - 1;
	}

	if (level < first_block_level) {
		out->esr = compute_fsc(level, ESR_ELx_FSC_FAULT);
		out->desc = desc;
		return 1;
	}

	if (check_output_size(wi, desc)) {
		out->esr = compute_fsc(level, ESR_ELx_FSC_ADDRSZ);
		out->desc = desc;
		return 1;
	}

	if (!(desc & BIT(10))) {
		out->esr = compute_fsc(level, ESR_ELx_FSC_ACCESS);
		out->desc = desc;
		return 1;
	}

	addr_bottom += contiguous_bit_shift(desc, wi, level);

	/* Calculate and return the result */
	paddr = (desc & GENMASK_ULL(47, addr_bottom)) |
		(ipa & GENMASK_ULL(addr_bottom - 1, 0));
	out->output = paddr;
	out->block_size = 1UL << ((3 - level) * stride + wi->pgshift);
	out->readable = desc & (0b01 << 6);
	out->writable = desc & (0b10 << 6);
	out->level = level;
	out->desc = desc;
	return 0;
}

static int read_guest_s2_desc(phys_addr_t pa, u64 *desc, void *data)
{
	struct kvm_vcpu *vcpu = data;

	return kvm_read_guest(vcpu->kvm, pa, desc, sizeof(*desc));
}

static void vtcr_to_walk_info(u64 vtcr, struct s2_walk_info *wi)
{
	wi->t0sz = vtcr & TCR_EL2_T0SZ_MASK;

	switch (vtcr & VTCR_EL2_TG0_MASK) {
	case VTCR_EL2_TG0_4K:
		wi->pgshift = 12;	 break;
	case VTCR_EL2_TG0_16K:
		wi->pgshift = 14;	 break;
	case VTCR_EL2_TG0_64K:
	default:	    /* IMPDEF: treat any other value as 64k */
		wi->pgshift = 16;	 break;
	}

	wi->sl = FIELD_GET(VTCR_EL2_SL0_MASK, vtcr);
	/* Global limit for now, should eventually be per-VM */
	wi->max_oa_bits = min(get_kvm_ipa_limit(),
			      ps_to_output_size(FIELD_GET(VTCR_EL2_PS_MASK, vtcr), false));
}

int kvm_walk_nested_s2(struct kvm_vcpu *vcpu, phys_addr_t gipa,
		       struct kvm_s2_trans *result)
{
	u64 vtcr = vcpu_read_sys_reg(vcpu, VTCR_EL2);
	struct s2_walk_info wi;
	int ret;

	result->esr = 0;

	if (!vcpu_has_nv(vcpu))
		return 0;

	wi.read_desc = read_guest_s2_desc;
	wi.data = vcpu;
	wi.baddr = vcpu_read_sys_reg(vcpu, VTTBR_EL2);

	vtcr_to_walk_info(vtcr, &wi);

	wi.be = vcpu_read_sys_reg(vcpu, SCTLR_EL2) & SCTLR_ELx_EE;

	ret = walk_nested_s2_pgd(gipa, &wi, result);
	if (ret)
		result->esr |= (kvm_vcpu_get_esr(vcpu) & ~ESR_ELx_FSC);

	return ret;
}

static unsigned int ttl_to_size(u8 ttl)
{
	int level = ttl & 3;
	int gran = (ttl >> 2) & 3;
	unsigned int max_size = 0;

	switch (gran) {
	case TLBI_TTL_TG_4K:
		switch (level) {
		case 0:
			break;
		case 1:
			max_size = SZ_1G;
			break;
		case 2:
			max_size = SZ_2M;
			break;
		case 3:
			max_size = SZ_4K;
			break;
		}
		break;
	case TLBI_TTL_TG_16K:
		switch (level) {
		case 0:
		case 1:
			break;
		case 2:
			max_size = SZ_32M;
			break;
		case 3:
			max_size = SZ_16K;
			break;
		}
		break;
	case TLBI_TTL_TG_64K:
		switch (level) {
		case 0:
		case 1:
			/* No 52bit IPA support */
			break;
		case 2:
			max_size = SZ_512M;
			break;
		case 3:
			max_size = SZ_64K;
			break;
		}
		break;
	default:			/* No size information */
		break;
	}

	return max_size;
}

static u8 pgshift_level_to_ttl(u16 shift, u8 level)
{
	u8 ttl;

	switch(shift) {
	case 12:
		ttl = TLBI_TTL_TG_4K;
		break;
	case 14:
		ttl = TLBI_TTL_TG_16K;
		break;
	case 16:
		ttl = TLBI_TTL_TG_64K;
		break;
	default:
		BUG();
	}

	ttl <<= 2;
	ttl |= level & 3;

	return ttl;
}

/*
 * Compute the equivalent of the TTL field by parsing the shadow PT.  The
 * granule size is extracted from the cached VTCR_EL2.TG0 while the level is
 * retrieved from first entry carrying the level as a tag.
 */
static u8 get_guest_mapping_ttl(struct kvm_s2_mmu *mmu, u64 addr)
{
	u64 tmp, sz = 0, vtcr = mmu->tlb_vtcr;
	kvm_pte_t pte;
	u8 ttl, level;

	lockdep_assert_held_write(&kvm_s2_mmu_to_kvm(mmu)->mmu_lock);

	switch (vtcr & VTCR_EL2_TG0_MASK) {
	case VTCR_EL2_TG0_4K:
		ttl = (TLBI_TTL_TG_4K << 2);
		break;
	case VTCR_EL2_TG0_16K:
		ttl = (TLBI_TTL_TG_16K << 2);
		break;
	case VTCR_EL2_TG0_64K:
	default:	    /* IMPDEF: treat any other value as 64k */
		ttl = (TLBI_TTL_TG_64K << 2);
		break;
	}

	tmp = addr;

again:
	/* Iteratively compute the block sizes for a particular granule size */
	switch (vtcr & VTCR_EL2_TG0_MASK) {
	case VTCR_EL2_TG0_4K:
		if	(sz < SZ_4K)	sz = SZ_4K;
		else if (sz < SZ_2M)	sz = SZ_2M;
		else if (sz < SZ_1G)	sz = SZ_1G;
		else			sz = 0;
		break;
	case VTCR_EL2_TG0_16K:
		if	(sz < SZ_16K)	sz = SZ_16K;
		else if (sz < SZ_32M)	sz = SZ_32M;
		else			sz = 0;
		break;
	case VTCR_EL2_TG0_64K:
	default:	    /* IMPDEF: treat any other value as 64k */
		if	(sz < SZ_64K)	sz = SZ_64K;
		else if (sz < SZ_512M)	sz = SZ_512M;
		else			sz = 0;
		break;
	}

	if (sz == 0)
		return 0;

	tmp &= ~(sz - 1);
	if (kvm_pgtable_get_leaf(mmu->pgt, tmp, &pte, NULL))
		goto again;
	if (!(pte & PTE_VALID))
		goto again;
	level = FIELD_GET(KVM_NV_GUEST_MAP_SZ, pte);
	if (!level)
		goto again;

	ttl |= level;

	/*
	 * We now have found some level information in the shadow S2. Check
	 * that the resulting range is actually including the original IPA.
	 */
	sz = ttl_to_size(ttl);
	if (addr < (tmp + sz))
		return ttl;

	return 0;
}

unsigned long compute_tlb_inval_range(struct kvm_s2_mmu *mmu, u64 val)
{
	struct kvm *kvm = kvm_s2_mmu_to_kvm(mmu);
	unsigned long max_size;
	u8 ttl;

	ttl = FIELD_GET(TLBI_TTL_MASK, val);

	if (!ttl || !kvm_has_feat(kvm, ID_AA64MMFR2_EL1, TTL, IMP)) {
		/* No TTL, check the shadow S2 for a hint */
		u64 addr = (val & GENMASK_ULL(35, 0)) << 12;
		ttl = get_guest_mapping_ttl(mmu, addr);
	}

	max_size = ttl_to_size(ttl);

	if (!max_size) {
		/* Compute the maximum extent of the invalidation */
		switch (mmu->tlb_vtcr & VTCR_EL2_TG0_MASK) {
		case VTCR_EL2_TG0_4K:
			max_size = SZ_1G;
			break;
		case VTCR_EL2_TG0_16K:
			max_size = SZ_32M;
			break;
		case VTCR_EL2_TG0_64K:
		default:    /* IMPDEF: treat any other value as 64k */
			/*
			 * No, we do not support 52bit IPA in nested yet. Once
			 * we do, this should be 4TB.
			 */
			max_size = SZ_512M;
			break;
		}
	}

	WARN_ON(!max_size);
	return max_size;
}

/*
 * We can have multiple *different* MMU contexts with the same VMID:
 *
 * - S2 being enabled or not, hence differing by the HCR_EL2.VM bit
 *
 * - Multiple vcpus using private S2s (huh huh...), hence differing by the
 *   VBBTR_EL2.BADDR address
 *
 * - A combination of the above...
 *
 * We can always identify which MMU context to pick at run-time.  However,
 * TLB invalidation involving a VMID must take action on all the TLBs using
 * this particular VMID. This translates into applying the same invalidation
 * operation to all the contexts that are using this VMID. Moar phun!
 */
void kvm_s2_mmu_iterate_by_vmid(struct kvm *kvm, u16 vmid,
				const union tlbi_info *info,
				void (*tlbi_callback)(struct kvm_s2_mmu *,
						      const union tlbi_info *))
{
	write_lock(&kvm->mmu_lock);

	for (int i = 0; i < kvm->arch.nested_mmus_size; i++) {
		struct kvm_s2_mmu *mmu = &kvm->arch.nested_mmus[i];

		if (!kvm_s2_mmu_valid(mmu))
			continue;

		if (vmid == get_vmid(mmu->tlb_vttbr))
			tlbi_callback(mmu, info);
	}

	write_unlock(&kvm->mmu_lock);
}

struct kvm_s2_mmu *lookup_s2_mmu(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	bool nested_stage2_enabled;
	u64 vttbr, vtcr, hcr;

	lockdep_assert_held_write(&kvm->mmu_lock);

	vttbr = vcpu_read_sys_reg(vcpu, VTTBR_EL2);
	vtcr = vcpu_read_sys_reg(vcpu, VTCR_EL2);
	hcr = vcpu_read_sys_reg(vcpu, HCR_EL2);

	nested_stage2_enabled = hcr & HCR_VM;

	/* Don't consider the CnP bit for the vttbr match */
	vttbr &= ~VTTBR_CNP_BIT;

	/*
	 * Two possibilities when looking up a S2 MMU context:
	 *
	 * - either S2 is enabled in the guest, and we need a context that is
	 *   S2-enabled and matches the full VTTBR (VMID+BADDR) and VTCR,
	 *   which makes it safe from a TLB conflict perspective (a broken
	 *   guest won't be able to generate them),
	 *
	 * - or S2 is disabled, and we need a context that is S2-disabled
	 *   and matches the VMID only, as all TLBs are tagged by VMID even
	 *   if S2 translation is disabled.
	 */
	for (int i = 0; i < kvm->arch.nested_mmus_size; i++) {
		struct kvm_s2_mmu *mmu = &kvm->arch.nested_mmus[i];

		if (!kvm_s2_mmu_valid(mmu))
			continue;

		if (nested_stage2_enabled &&
		    mmu->nested_stage2_enabled &&
		    vttbr == mmu->tlb_vttbr &&
		    vtcr == mmu->tlb_vtcr)
			return mmu;

		if (!nested_stage2_enabled &&
		    !mmu->nested_stage2_enabled &&
		    get_vmid(vttbr) == get_vmid(mmu->tlb_vttbr))
			return mmu;
	}
	return NULL;
}

static struct kvm_s2_mmu *get_s2_mmu_nested(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_s2_mmu *s2_mmu;
	int i;

	lockdep_assert_held_write(&vcpu->kvm->mmu_lock);

	s2_mmu = lookup_s2_mmu(vcpu);
	if (s2_mmu)
		goto out;

	/*
	 * Make sure we don't always search from the same point, or we
	 * will always reuse a potentially active context, leaving
	 * free contexts unused.
	 */
	for (i = kvm->arch.nested_mmus_next;
	     i < (kvm->arch.nested_mmus_size + kvm->arch.nested_mmus_next);
	     i++) {
		s2_mmu = &kvm->arch.nested_mmus[i % kvm->arch.nested_mmus_size];

		if (atomic_read(&s2_mmu->refcnt) == 0)
			break;
	}
	BUG_ON(atomic_read(&s2_mmu->refcnt)); /* We have struct MMUs to spare */

	/* Set the scene for the next search */
	kvm->arch.nested_mmus_next = (i + 1) % kvm->arch.nested_mmus_size;

	/* Make sure we don't forget to do the laundry */
	if (kvm_s2_mmu_valid(s2_mmu))
		s2_mmu->pending_unmap = true;

	/*
	 * The virtual VMID (modulo CnP) will be used as a key when matching
	 * an existing kvm_s2_mmu.
	 *
	 * We cache VTCR at allocation time, once and for all. It'd be great
	 * if the guest didn't screw that one up, as this is not very
	 * forgiving...
	 */
	s2_mmu->tlb_vttbr = vcpu_read_sys_reg(vcpu, VTTBR_EL2) & ~VTTBR_CNP_BIT;
	s2_mmu->tlb_vtcr = vcpu_read_sys_reg(vcpu, VTCR_EL2);
	s2_mmu->nested_stage2_enabled = vcpu_read_sys_reg(vcpu, HCR_EL2) & HCR_VM;

out:
	atomic_inc(&s2_mmu->refcnt);

	/*
	 * Set the vCPU request to perform an unmap, even if the pending unmap
	 * originates from another vCPU. This guarantees that the MMU has been
	 * completely unmapped before any vCPU actually uses it, and allows
	 * multiple vCPUs to lend a hand with completing the unmap.
	 */
	if (s2_mmu->pending_unmap)
		kvm_make_request(KVM_REQ_NESTED_S2_UNMAP, vcpu);

	return s2_mmu;
}

void kvm_init_nested_s2_mmu(struct kvm_s2_mmu *mmu)
{
	/* CnP being set denotes an invalid entry */
	mmu->tlb_vttbr = VTTBR_CNP_BIT;
	mmu->nested_stage2_enabled = false;
	atomic_set(&mmu->refcnt, 0);
}

void kvm_vcpu_load_hw_mmu(struct kvm_vcpu *vcpu)
{
	/*
	 * If the vCPU kept its reference on the MMU after the last put,
	 * keep rolling with it.
	 */
	if (is_hyp_ctxt(vcpu)) {
		if (!vcpu->arch.hw_mmu)
			vcpu->arch.hw_mmu = &vcpu->kvm->arch.mmu;
	} else {
		if (!vcpu->arch.hw_mmu) {
			scoped_guard(write_lock, &vcpu->kvm->mmu_lock)
				vcpu->arch.hw_mmu = get_s2_mmu_nested(vcpu);
		}

		if (__vcpu_sys_reg(vcpu, HCR_EL2) & HCR_NV)
			kvm_make_request(KVM_REQ_MAP_L1_VNCR_EL2, vcpu);
	}
}

void kvm_vcpu_put_hw_mmu(struct kvm_vcpu *vcpu)
{
	/* Unconditionally drop the VNCR mapping if we have one */
	if (host_data_test_flag(L1_VNCR_MAPPED)) {
		BUG_ON(vcpu->arch.vncr_tlb->cpu != smp_processor_id());
		BUG_ON(is_hyp_ctxt(vcpu));

		clear_fixmap(vncr_fixmap(vcpu->arch.vncr_tlb->cpu));
		vcpu->arch.vncr_tlb->cpu = -1;
		host_data_clear_flag(L1_VNCR_MAPPED);
		atomic_dec(&vcpu->kvm->arch.vncr_map_count);
	}

	/*
	 * Keep a reference on the associated stage-2 MMU if the vCPU is
	 * scheduling out and not in WFI emulation, suggesting it is likely to
	 * reuse the MMU sometime soon.
	 */
	if (vcpu->scheduled_out && !vcpu_get_flag(vcpu, IN_WFI))
		return;

	if (kvm_is_nested_s2_mmu(vcpu->kvm, vcpu->arch.hw_mmu))
		atomic_dec(&vcpu->arch.hw_mmu->refcnt);

	vcpu->arch.hw_mmu = NULL;
}

/*
 * Returns non-zero if permission fault is handled by injecting it to the next
 * level hypervisor.
 */
int kvm_s2_handle_perm_fault(struct kvm_vcpu *vcpu, struct kvm_s2_trans *trans)
{
	bool forward_fault = false;

	trans->esr = 0;

	if (!kvm_vcpu_trap_is_permission_fault(vcpu))
		return 0;

	if (kvm_vcpu_trap_is_iabt(vcpu)) {
		forward_fault = !kvm_s2_trans_executable(trans);
	} else {
		bool write_fault = kvm_is_write_fault(vcpu);

		forward_fault = ((write_fault && !trans->writable) ||
				 (!write_fault && !trans->readable));
	}

	if (forward_fault)
		trans->esr = esr_s2_fault(vcpu, trans->level, ESR_ELx_FSC_PERM);

	return forward_fault;
}

int kvm_inject_s2_fault(struct kvm_vcpu *vcpu, u64 esr_el2)
{
	vcpu_write_sys_reg(vcpu, vcpu->arch.fault.far_el2, FAR_EL2);
	vcpu_write_sys_reg(vcpu, vcpu->arch.fault.hpfar_el2, HPFAR_EL2);

	return kvm_inject_nested_sync(vcpu, esr_el2);
}

static void invalidate_vncr(struct vncr_tlb *vt)
{
	vt->valid = false;
	if (vt->cpu != -1)
		clear_fixmap(vncr_fixmap(vt->cpu));
}

static void kvm_invalidate_vncr_ipa(struct kvm *kvm, u64 start, u64 end)
{
	struct kvm_vcpu *vcpu;
	unsigned long i;

	lockdep_assert_held_write(&kvm->mmu_lock);

	if (!kvm_has_feat(kvm, ID_AA64MMFR4_EL1, NV_frac, NV2_ONLY))
		return;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		struct vncr_tlb *vt = vcpu->arch.vncr_tlb;
		u64 ipa_start, ipa_end, ipa_size;

		/*
		 * Careful here: We end-up here from an MMU notifier,
		 * and this can race against a vcpu not being onlined
		 * yet, without the pseudo-TLB being allocated.
		 *
		 * Skip those, as they obviously don't participate in
		 * the invalidation at this stage.
		 */
		if (!vt)
			continue;

		if (!vt->valid)
			continue;

		ipa_size = ttl_to_size(pgshift_level_to_ttl(vt->wi.pgshift,
							    vt->wr.level));
		ipa_start = vt->wr.pa & ~(ipa_size - 1);
		ipa_end = ipa_start + ipa_size;

		if (ipa_end <= start || ipa_start >= end)
			continue;

		invalidate_vncr(vt);
	}
}

struct s1e2_tlbi_scope {
	enum {
		TLBI_ALL,
		TLBI_VA,
		TLBI_VAA,
		TLBI_ASID,
	} type;

	u16 asid;
	u64 va;
	u64 size;
};

static void invalidate_vncr_va(struct kvm *kvm,
			       struct s1e2_tlbi_scope *scope)
{
	struct kvm_vcpu *vcpu;
	unsigned long i;

	lockdep_assert_held_write(&kvm->mmu_lock);

	kvm_for_each_vcpu(i, vcpu, kvm) {
		struct vncr_tlb *vt = vcpu->arch.vncr_tlb;
		u64 va_start, va_end, va_size;

		if (!vt->valid)
			continue;

		va_size = ttl_to_size(pgshift_level_to_ttl(vt->wi.pgshift,
							   vt->wr.level));
		va_start = vt->gva & ~(va_size - 1);
		va_end = va_start + va_size;

		switch (scope->type) {
		case TLBI_ALL:
			break;

		case TLBI_VA:
			if (va_end <= scope->va ||
			    va_start >= (scope->va + scope->size))
				continue;
			if (vt->wr.nG && vt->wr.asid != scope->asid)
				continue;
			break;

		case TLBI_VAA:
			if (va_end <= scope->va ||
			    va_start >= (scope->va + scope->size))
				continue;
			break;

		case TLBI_ASID:
			if (!vt->wr.nG || vt->wr.asid != scope->asid)
				continue;
			break;
		}

		invalidate_vncr(vt);
	}
}

#define tlbi_va_s1_to_va(v)	(u64)sign_extend64((v) << 12, 48)

static void compute_s1_tlbi_range(struct kvm_vcpu *vcpu, u32 inst, u64 val,
				  struct s1e2_tlbi_scope *scope)
{
	switch (inst) {
	case OP_TLBI_ALLE2:
	case OP_TLBI_ALLE2IS:
	case OP_TLBI_ALLE2OS:
	case OP_TLBI_VMALLE1:
	case OP_TLBI_VMALLE1IS:
	case OP_TLBI_VMALLE1OS:
	case OP_TLBI_ALLE2NXS:
	case OP_TLBI_ALLE2ISNXS:
	case OP_TLBI_ALLE2OSNXS:
	case OP_TLBI_VMALLE1NXS:
	case OP_TLBI_VMALLE1ISNXS:
	case OP_TLBI_VMALLE1OSNXS:
		scope->type = TLBI_ALL;
		break;
	case OP_TLBI_VAE2:
	case OP_TLBI_VAE2IS:
	case OP_TLBI_VAE2OS:
	case OP_TLBI_VAE1:
	case OP_TLBI_VAE1IS:
	case OP_TLBI_VAE1OS:
	case OP_TLBI_VAE2NXS:
	case OP_TLBI_VAE2ISNXS:
	case OP_TLBI_VAE2OSNXS:
	case OP_TLBI_VAE1NXS:
	case OP_TLBI_VAE1ISNXS:
	case OP_TLBI_VAE1OSNXS:
	case OP_TLBI_VALE2:
	case OP_TLBI_VALE2IS:
	case OP_TLBI_VALE2OS:
	case OP_TLBI_VALE1:
	case OP_TLBI_VALE1IS:
	case OP_TLBI_VALE1OS:
	case OP_TLBI_VALE2NXS:
	case OP_TLBI_VALE2ISNXS:
	case OP_TLBI_VALE2OSNXS:
	case OP_TLBI_VALE1NXS:
	case OP_TLBI_VALE1ISNXS:
	case OP_TLBI_VALE1OSNXS:
		scope->type = TLBI_VA;
		scope->size = ttl_to_size(FIELD_GET(TLBI_TTL_MASK, val));
		if (!scope->size)
			scope->size = SZ_1G;
		scope->va = tlbi_va_s1_to_va(val) & ~(scope->size - 1);
		scope->asid = FIELD_GET(TLBIR_ASID_MASK, val);
		break;
	case OP_TLBI_ASIDE1:
	case OP_TLBI_ASIDE1IS:
	case OP_TLBI_ASIDE1OS:
	case OP_TLBI_ASIDE1NXS:
	case OP_TLBI_ASIDE1ISNXS:
	case OP_TLBI_ASIDE1OSNXS:
		scope->type = TLBI_ASID;
		scope->asid = FIELD_GET(TLBIR_ASID_MASK, val);
		break;
	case OP_TLBI_VAAE1:
	case OP_TLBI_VAAE1IS:
	case OP_TLBI_VAAE1OS:
	case OP_TLBI_VAAE1NXS:
	case OP_TLBI_VAAE1ISNXS:
	case OP_TLBI_VAAE1OSNXS:
	case OP_TLBI_VAALE1:
	case OP_TLBI_VAALE1IS:
	case OP_TLBI_VAALE1OS:
	case OP_TLBI_VAALE1NXS:
	case OP_TLBI_VAALE1ISNXS:
	case OP_TLBI_VAALE1OSNXS:
		scope->type = TLBI_VAA;
		scope->size = ttl_to_size(FIELD_GET(TLBI_TTL_MASK, val));
		if (!scope->size)
			scope->size = SZ_1G;
		scope->va = tlbi_va_s1_to_va(val) & ~(scope->size - 1);
		break;
	case OP_TLBI_RVAE2:
	case OP_TLBI_RVAE2IS:
	case OP_TLBI_RVAE2OS:
	case OP_TLBI_RVAE1:
	case OP_TLBI_RVAE1IS:
	case OP_TLBI_RVAE1OS:
	case OP_TLBI_RVAE2NXS:
	case OP_TLBI_RVAE2ISNXS:
	case OP_TLBI_RVAE2OSNXS:
	case OP_TLBI_RVAE1NXS:
	case OP_TLBI_RVAE1ISNXS:
	case OP_TLBI_RVAE1OSNXS:
	case OP_TLBI_RVALE2:
	case OP_TLBI_RVALE2IS:
	case OP_TLBI_RVALE2OS:
	case OP_TLBI_RVALE1:
	case OP_TLBI_RVALE1IS:
	case OP_TLBI_RVALE1OS:
	case OP_TLBI_RVALE2NXS:
	case OP_TLBI_RVALE2ISNXS:
	case OP_TLBI_RVALE2OSNXS:
	case OP_TLBI_RVALE1NXS:
	case OP_TLBI_RVALE1ISNXS:
	case OP_TLBI_RVALE1OSNXS:
		scope->type = TLBI_VA;
		scope->va = decode_range_tlbi(val, &scope->size, &scope->asid);
		break;
	case OP_TLBI_RVAAE1:
	case OP_TLBI_RVAAE1IS:
	case OP_TLBI_RVAAE1OS:
	case OP_TLBI_RVAAE1NXS:
	case OP_TLBI_RVAAE1ISNXS:
	case OP_TLBI_RVAAE1OSNXS:
	case OP_TLBI_RVAALE1:
	case OP_TLBI_RVAALE1IS:
	case OP_TLBI_RVAALE1OS:
	case OP_TLBI_RVAALE1NXS:
	case OP_TLBI_RVAALE1ISNXS:
	case OP_TLBI_RVAALE1OSNXS:
		scope->type = TLBI_VAA;
		scope->va = decode_range_tlbi(val, &scope->size, NULL);
		break;
	}
}

void kvm_handle_s1e2_tlbi(struct kvm_vcpu *vcpu, u32 inst, u64 val)
{
	struct s1e2_tlbi_scope scope = {};

	compute_s1_tlbi_range(vcpu, inst, val, &scope);

	guard(write_lock)(&vcpu->kvm->mmu_lock);
	invalidate_vncr_va(vcpu->kvm, &scope);
}

void kvm_nested_s2_wp(struct kvm *kvm)
{
	int i;

	lockdep_assert_held_write(&kvm->mmu_lock);

	for (i = 0; i < kvm->arch.nested_mmus_size; i++) {
		struct kvm_s2_mmu *mmu = &kvm->arch.nested_mmus[i];

		if (kvm_s2_mmu_valid(mmu))
			kvm_stage2_wp_range(mmu, 0, kvm_phys_size(mmu));
	}

	kvm_invalidate_vncr_ipa(kvm, 0, BIT(kvm->arch.mmu.pgt->ia_bits));
}

void kvm_nested_s2_unmap(struct kvm *kvm, bool may_block)
{
	int i;

	lockdep_assert_held_write(&kvm->mmu_lock);

	for (i = 0; i < kvm->arch.nested_mmus_size; i++) {
		struct kvm_s2_mmu *mmu = &kvm->arch.nested_mmus[i];

		if (kvm_s2_mmu_valid(mmu))
			kvm_stage2_unmap_range(mmu, 0, kvm_phys_size(mmu), may_block);
	}

	kvm_invalidate_vncr_ipa(kvm, 0, BIT(kvm->arch.mmu.pgt->ia_bits));
}

void kvm_nested_s2_flush(struct kvm *kvm)
{
	int i;

	lockdep_assert_held_write(&kvm->mmu_lock);

	for (i = 0; i < kvm->arch.nested_mmus_size; i++) {
		struct kvm_s2_mmu *mmu = &kvm->arch.nested_mmus[i];

		if (kvm_s2_mmu_valid(mmu))
			kvm_stage2_flush_range(mmu, 0, kvm_phys_size(mmu));
	}
}

void kvm_arch_flush_shadow_all(struct kvm *kvm)
{
	int i;

	for (i = 0; i < kvm->arch.nested_mmus_size; i++) {
		struct kvm_s2_mmu *mmu = &kvm->arch.nested_mmus[i];

		if (!WARN_ON(atomic_read(&mmu->refcnt)))
			kvm_free_stage2_pgd(mmu);
	}
	kvfree(kvm->arch.nested_mmus);
	kvm->arch.nested_mmus = NULL;
	kvm->arch.nested_mmus_size = 0;
	kvm_uninit_stage2_mmu(kvm);
}

/*
 * Dealing with VNCR_EL2 exposed by the *guest* is a complicated matter:
 *
 * - We introduce an internal representation of a vcpu-private TLB,
 *   representing the mapping between the guest VA contained in VNCR_EL2,
 *   the IPA the guest's EL2 PTs point to, and the actual PA this lives at.
 *
 * - On translation fault from a nested VNCR access, we create such a TLB.
 *   If there is no mapping to describe, the guest inherits the fault.
 *   Crucially, no actual mapping is done at this stage.
 *
 * - On vcpu_load() in a non-HYP context with HCR_EL2.NV==1, if the above
 *   TLB exists, we map it in the fixmap for this CPU, and run with it. We
 *   have to respect the permissions dictated by the guest, but not the
 *   memory type (FWB is a must).
 *
 * - Note that we usually don't do a vcpu_load() on the back of a fault
 *   (unless we are preempted), so the resolution of a translation fault
 *   must go via a request that will map the VNCR page in the fixmap.
 *   vcpu_load() might as well use the same mechanism.
 *
 * - On vcpu_put() in a non-HYP context with HCR_EL2.NV==1, if the TLB was
 *   mapped, we unmap it. Yes it is that simple. The TLB still exists
 *   though, and may be reused at a later load.
 *
 * - On permission fault, we simply forward the fault to the guest's EL2.
 *   Get out of my way.
 *
 * - On any TLBI for the EL2&0 translation regime, we must find any TLB that
 *   intersects with the TLBI request, invalidate it, and unmap the page
 *   from the fixmap. Because we need to look at all the vcpu-private TLBs,
 *   this requires some wide-ranging locking to ensure that nothing races
 *   against it. This may require some refcounting to avoid the search when
 *   no such TLB is present.
 *
 * - On MMU notifiers, we must invalidate our TLB in a similar way, but
 *   looking at the IPA instead. The funny part is that there may not be a
 *   stage-2 mapping for this page if L1 hasn't accessed it using LD/ST
 *   instructions.
 */

int kvm_vcpu_allocate_vncr_tlb(struct kvm_vcpu *vcpu)
{
	if (!kvm_has_feat(vcpu->kvm, ID_AA64MMFR4_EL1, NV_frac, NV2_ONLY))
		return 0;

	vcpu->arch.vncr_tlb = kzalloc(sizeof(*vcpu->arch.vncr_tlb),
				      GFP_KERNEL_ACCOUNT);
	if (!vcpu->arch.vncr_tlb)
		return -ENOMEM;

	return 0;
}

static u64 read_vncr_el2(struct kvm_vcpu *vcpu)
{
	return (u64)sign_extend64(__vcpu_sys_reg(vcpu, VNCR_EL2), 48);
}

static int kvm_translate_vncr(struct kvm_vcpu *vcpu, bool *is_gmem)
{
	struct kvm_memory_slot *memslot;
	bool write_fault, writable;
	unsigned long mmu_seq;
	struct vncr_tlb *vt;
	struct page *page;
	u64 va, pfn, gfn;
	int ret;

	vt = vcpu->arch.vncr_tlb;

	/*
	 * If we're about to walk the EL2 S1 PTs, we must invalidate the
	 * current TLB, as it could be sampled from another vcpu doing a
	 * TLBI *IS. A real CPU wouldn't do that, but we only keep a single
	 * translation, so not much of a choice.
	 *
	 * We also prepare the next walk wilst we're at it.
	 */
	scoped_guard(write_lock, &vcpu->kvm->mmu_lock) {
		invalidate_vncr(vt);

		vt->wi = (struct s1_walk_info) {
			.regime	= TR_EL20,
			.as_el0	= false,
			.pan	= false,
		};
		vt->wr = (struct s1_walk_result){};
	}

	guard(srcu)(&vcpu->kvm->srcu);

	va =  read_vncr_el2(vcpu);

	ret = __kvm_translate_va(vcpu, &vt->wi, &vt->wr, va);
	if (ret)
		return ret;

	write_fault = kvm_is_write_fault(vcpu);

	mmu_seq = vcpu->kvm->mmu_invalidate_seq;
	smp_rmb();

	gfn = vt->wr.pa >> PAGE_SHIFT;
	memslot = gfn_to_memslot(vcpu->kvm, gfn);
	if (!memslot)
		return -EFAULT;

	*is_gmem = kvm_slot_has_gmem(memslot);
	if (!*is_gmem) {
		pfn = __kvm_faultin_pfn(memslot, gfn, write_fault ? FOLL_WRITE : 0,
					&writable, &page);
		if (is_error_noslot_pfn(pfn) || (write_fault && !writable))
			return -EFAULT;
	} else {
		ret = kvm_gmem_get_pfn(vcpu->kvm, memslot, gfn, &pfn, &page, NULL);
		if (ret) {
			kvm_prepare_memory_fault_exit(vcpu, vt->wr.pa, PAGE_SIZE,
					      write_fault, false, false);
			return ret;
		}
	}

	scoped_guard(write_lock, &vcpu->kvm->mmu_lock) {
		if (mmu_invalidate_retry(vcpu->kvm, mmu_seq))
			return -EAGAIN;

		vt->gva = va;
		vt->hpa = pfn << PAGE_SHIFT;
		vt->valid = true;
		vt->cpu = -1;

		kvm_make_request(KVM_REQ_MAP_L1_VNCR_EL2, vcpu);
		kvm_release_faultin_page(vcpu->kvm, page, false, vt->wr.pw);
	}

	if (vt->wr.pw)
		mark_page_dirty(vcpu->kvm, gfn);

	return 0;
}

static void inject_vncr_perm(struct kvm_vcpu *vcpu)
{
	struct vncr_tlb *vt = vcpu->arch.vncr_tlb;
	u64 esr = kvm_vcpu_get_esr(vcpu);

	/* Adjust the fault level to reflect that of the guest's */
	esr &= ~ESR_ELx_FSC;
	esr |= FIELD_PREP(ESR_ELx_FSC,
			  ESR_ELx_FSC_PERM_L(vt->wr.level));

	kvm_inject_nested_sync(vcpu, esr);
}

static bool kvm_vncr_tlb_lookup(struct kvm_vcpu *vcpu)
{
	struct vncr_tlb *vt = vcpu->arch.vncr_tlb;

	lockdep_assert_held_read(&vcpu->kvm->mmu_lock);

	if (!vt->valid)
		return false;

	if (read_vncr_el2(vcpu) != vt->gva)
		return false;

	if (vt->wr.nG) {
		u64 tcr = vcpu_read_sys_reg(vcpu, TCR_EL2);
		u64 ttbr = ((tcr & TCR_A1) ?
			    vcpu_read_sys_reg(vcpu, TTBR1_EL2) :
			    vcpu_read_sys_reg(vcpu, TTBR0_EL2));
		u16 asid;

		asid = FIELD_GET(TTBR_ASID_MASK, ttbr);
		if (!kvm_has_feat_enum(vcpu->kvm, ID_AA64MMFR0_EL1, ASIDBITS, 16) ||
		    !(tcr & TCR_ASID16))
			asid &= GENMASK(7, 0);

		return asid == vt->wr.asid;
	}

	return true;
}

int kvm_handle_vncr_abort(struct kvm_vcpu *vcpu)
{
	struct vncr_tlb *vt = vcpu->arch.vncr_tlb;
	u64 esr = kvm_vcpu_get_esr(vcpu);

	WARN_ON_ONCE(!(esr & ESR_ELx_VNCR));

	if (kvm_vcpu_abt_issea(vcpu))
		return kvm_handle_guest_sea(vcpu);

	if (esr_fsc_is_permission_fault(esr)) {
		inject_vncr_perm(vcpu);
	} else if (esr_fsc_is_translation_fault(esr)) {
		bool valid, is_gmem = false;
		int ret;

		scoped_guard(read_lock, &vcpu->kvm->mmu_lock)
			valid = kvm_vncr_tlb_lookup(vcpu);

		if (!valid)
			ret = kvm_translate_vncr(vcpu, &is_gmem);
		else
			ret = -EPERM;

		switch (ret) {
		case -EAGAIN:
			/* Let's try again... */
			break;
		case -ENOMEM:
			/*
			 * For guest_memfd, this indicates that it failed to
			 * create a folio to back the memory. Inform userspace.
			 */
			if (is_gmem)
				return 0;
			/* Otherwise, let's try again... */
			break;
		case -EFAULT:
		case -EIO:
		case -EHWPOISON:
			if (is_gmem)
				return 0;
			fallthrough;
		case -EINVAL:
		case -ENOENT:
		case -EACCES:
			/*
			 * Translation failed, inject the corresponding
			 * exception back to EL2.
			 */
			BUG_ON(!vt->wr.failed);

			esr &= ~ESR_ELx_FSC;
			esr |= FIELD_PREP(ESR_ELx_FSC, vt->wr.fst);

			kvm_inject_nested_sync(vcpu, esr);
			break;
		case -EPERM:
			/* Hack to deal with POE until we get kernel support */
			inject_vncr_perm(vcpu);
			break;
		case 0:
			break;
		}
	} else {
		WARN_ONCE(1, "Unhandled VNCR abort, ESR=%llx\n", esr);
	}

	return 1;
}

static void kvm_map_l1_vncr(struct kvm_vcpu *vcpu)
{
	struct vncr_tlb *vt = vcpu->arch.vncr_tlb;
	pgprot_t prot;

	guard(preempt)();
	guard(read_lock)(&vcpu->kvm->mmu_lock);

	/*
	 * The request to map VNCR may have raced against some other
	 * event, such as an interrupt, and may not be valid anymore.
	 */
	if (is_hyp_ctxt(vcpu))
		return;

	/*
	 * Check that the pseudo-TLB is valid and that VNCR_EL2 still
	 * contains the expected value. If it doesn't, we simply bail out
	 * without a mapping -- a transformed MSR/MRS will generate the
	 * fault and allows us to populate the pseudo-TLB.
	 */
	if (!vt->valid)
		return;

	if (read_vncr_el2(vcpu) != vt->gva)
		return;

	if (vt->wr.nG) {
		u64 tcr = vcpu_read_sys_reg(vcpu, TCR_EL2);
		u64 ttbr = ((tcr & TCR_A1) ?
			    vcpu_read_sys_reg(vcpu, TTBR1_EL2) :
			    vcpu_read_sys_reg(vcpu, TTBR0_EL2));
		u16 asid;

		asid = FIELD_GET(TTBR_ASID_MASK, ttbr);
		if (!kvm_has_feat_enum(vcpu->kvm, ID_AA64MMFR0_EL1, ASIDBITS, 16) ||
		    !(tcr & TCR_ASID16))
			asid &= GENMASK(7, 0);

		if (asid != vt->wr.asid)
			return;
	}

	vt->cpu = smp_processor_id();

	if (vt->wr.pw && vt->wr.pr)
		prot = PAGE_KERNEL;
	else if (vt->wr.pr)
		prot = PAGE_KERNEL_RO;
	else
		prot = PAGE_NONE;

	/*
	 * We can't map write-only (or no permission at all) in the kernel,
	 * but the guest can do it if using POE, so we'll have to turn a
	 * translation fault into a permission fault at runtime.
	 * FIXME: WO doesn't work at all, need POE support in the kernel.
	 */
	if (pgprot_val(prot) != pgprot_val(PAGE_NONE)) {
		__set_fixmap(vncr_fixmap(vt->cpu), vt->hpa, prot);
		host_data_set_flag(L1_VNCR_MAPPED);
		atomic_inc(&vcpu->kvm->arch.vncr_map_count);
	}
}

#define has_tgran_2(__r, __sz)						\
	({								\
		u64 _s1, _s2, _mmfr0 = __r;				\
									\
		_s2 = SYS_FIELD_GET(ID_AA64MMFR0_EL1,			\
				    TGRAN##__sz##_2, _mmfr0);		\
									\
		_s1 = SYS_FIELD_GET(ID_AA64MMFR0_EL1,			\
				    TGRAN##__sz, _mmfr0);		\
									\
		((_s2 != ID_AA64MMFR0_EL1_TGRAN##__sz##_2_NI &&		\
		  _s2 != ID_AA64MMFR0_EL1_TGRAN##__sz##_2_TGRAN##__sz) || \
		 (_s2 == ID_AA64MMFR0_EL1_TGRAN##__sz##_2_TGRAN##__sz && \
		  _s1 != ID_AA64MMFR0_EL1_TGRAN##__sz##_NI));		\
	})
/*
 * Our emulated CPU doesn't support all the possible features. For the
 * sake of simplicity (and probably mental sanity), wipe out a number
 * of feature bits we don't intend to support for the time being.
 * This list should get updated as new features get added to the NV
 * support, and new extension to the architecture.
 */
u64 limit_nv_id_reg(struct kvm *kvm, u32 reg, u64 val)
{
	u64 orig_val = val;

	switch (reg) {
	case SYS_ID_AA64ISAR0_EL1:
		/* Support everything but TME */
		val &= ~ID_AA64ISAR0_EL1_TME;
		break;

	case SYS_ID_AA64ISAR1_EL1:
		/* Support everything but LS64 and Spec Invalidation */
		val &= ~(ID_AA64ISAR1_EL1_LS64	|
			 ID_AA64ISAR1_EL1_SPECRES);
		break;

	case SYS_ID_AA64PFR0_EL1:
		/* No RME, AMU, MPAM, or S-EL2 */
		val &= ~(ID_AA64PFR0_EL1_RME	|
			 ID_AA64PFR0_EL1_AMU	|
			 ID_AA64PFR0_EL1_MPAM	|
			 ID_AA64PFR0_EL1_SEL2	|
			 ID_AA64PFR0_EL1_EL3	|
			 ID_AA64PFR0_EL1_EL2	|
			 ID_AA64PFR0_EL1_EL1	|
			 ID_AA64PFR0_EL1_EL0);
		/* 64bit only at any EL */
		val |= SYS_FIELD_PREP_ENUM(ID_AA64PFR0_EL1, EL0, IMP);
		val |= SYS_FIELD_PREP_ENUM(ID_AA64PFR0_EL1, EL1, IMP);
		val |= SYS_FIELD_PREP_ENUM(ID_AA64PFR0_EL1, EL2, IMP);
		val |= SYS_FIELD_PREP_ENUM(ID_AA64PFR0_EL1, EL3, IMP);
		break;

	case SYS_ID_AA64PFR1_EL1:
		/* Only support BTI, SSBS, CSV2_frac */
		val &= ~(ID_AA64PFR1_EL1_PFAR		|
			 ID_AA64PFR1_EL1_MTEX		|
			 ID_AA64PFR1_EL1_THE		|
			 ID_AA64PFR1_EL1_GCS		|
			 ID_AA64PFR1_EL1_MTE_frac	|
			 ID_AA64PFR1_EL1_NMI		|
			 ID_AA64PFR1_EL1_SME		|
			 ID_AA64PFR1_EL1_RES0		|
			 ID_AA64PFR1_EL1_MPAM_frac	|
			 ID_AA64PFR1_EL1_MTE);
		break;

	case SYS_ID_AA64MMFR0_EL1:
		/* Hide ExS, Secure Memory */
		val &= ~(ID_AA64MMFR0_EL1_EXS		|
			 ID_AA64MMFR0_EL1_TGRAN4_2	|
			 ID_AA64MMFR0_EL1_TGRAN16_2	|
			 ID_AA64MMFR0_EL1_TGRAN64_2	|
			 ID_AA64MMFR0_EL1_SNSMEM);

		/* Hide CNTPOFF if present */
		val = ID_REG_LIMIT_FIELD_ENUM(val, ID_AA64MMFR0_EL1, ECV, IMP);

		/* Disallow unsupported S2 page sizes */
		switch (PAGE_SIZE) {
		case SZ_64K:
			val |= SYS_FIELD_PREP_ENUM(ID_AA64MMFR0_EL1, TGRAN16_2, NI);
			fallthrough;
		case SZ_16K:
			val |= SYS_FIELD_PREP_ENUM(ID_AA64MMFR0_EL1, TGRAN4_2, NI);
			fallthrough;
		case SZ_4K:
			/* Support everything */
			break;
		}

		/*
		 * Since we can't support a guest S2 page size smaller
		 * than the host's own page size (due to KVM only
		 * populating its own S2 using the kernel's page
		 * size), advertise the limitation using FEAT_GTG.
		 */
		switch (PAGE_SIZE) {
		case SZ_4K:
			if (has_tgran_2(orig_val, 4))
				val |= SYS_FIELD_PREP_ENUM(ID_AA64MMFR0_EL1, TGRAN4_2, IMP);
			fallthrough;
		case SZ_16K:
			if (has_tgran_2(orig_val, 16))
				val |= SYS_FIELD_PREP_ENUM(ID_AA64MMFR0_EL1, TGRAN16_2, IMP);
			fallthrough;
		case SZ_64K:
			if (has_tgran_2(orig_val, 64))
				val |= SYS_FIELD_PREP_ENUM(ID_AA64MMFR0_EL1, TGRAN64_2, IMP);
			break;
		}

		/* Cap PARange to 48bits */
		val = ID_REG_LIMIT_FIELD_ENUM(val, ID_AA64MMFR0_EL1, PARANGE, 48);
		break;

	case SYS_ID_AA64MMFR1_EL1:
		val &= ~(ID_AA64MMFR1_EL1_CMOW		|
			 ID_AA64MMFR1_EL1_nTLBPA	|
			 ID_AA64MMFR1_EL1_ETS		|
			 ID_AA64MMFR1_EL1_XNX		|
			 ID_AA64MMFR1_EL1_HAFDBS);
		/* FEAT_E2H0 implies no VHE */
		if (test_bit(KVM_ARM_VCPU_HAS_EL2_E2H0, kvm->arch.vcpu_features))
			val &= ~ID_AA64MMFR1_EL1_VH;
		break;

	case SYS_ID_AA64MMFR2_EL1:
		val &= ~(ID_AA64MMFR2_EL1_BBM	|
			 ID_AA64MMFR2_EL1_TTL	|
			 GENMASK_ULL(47, 44)	|
			 ID_AA64MMFR2_EL1_ST	|
			 ID_AA64MMFR2_EL1_CCIDX	|
			 ID_AA64MMFR2_EL1_VARange);

		/* Force TTL support */
		val |= SYS_FIELD_PREP_ENUM(ID_AA64MMFR2_EL1, TTL, IMP);
		break;

	case SYS_ID_AA64MMFR4_EL1:
		/*
		 * You get EITHER
		 *
		 * - FEAT_VHE without FEAT_E2H0
		 * - FEAT_NV limited to FEAT_NV2
		 * - HCR_EL2.NV1 being RES0
		 *
		 * OR
		 *
		 * - FEAT_E2H0 without FEAT_VHE nor FEAT_NV
		 *
		 * Life is too short for anything else.
		 */
		if (test_bit(KVM_ARM_VCPU_HAS_EL2_E2H0, kvm->arch.vcpu_features)) {
			val = 0;
		} else {
			val = SYS_FIELD_PREP_ENUM(ID_AA64MMFR4_EL1, NV_frac, NV2_ONLY);
			val |= SYS_FIELD_PREP_ENUM(ID_AA64MMFR4_EL1, E2H0, NI_NV1);
		}
		break;

	case SYS_ID_AA64DFR0_EL1:
		/* Only limited support for PMU, Debug, BPs, WPs, and HPMN0 */
		val &= ~(ID_AA64DFR0_EL1_ExtTrcBuff	|
			 ID_AA64DFR0_EL1_BRBE		|
			 ID_AA64DFR0_EL1_MTPMU		|
			 ID_AA64DFR0_EL1_TraceBuffer	|
			 ID_AA64DFR0_EL1_TraceFilt	|
			 ID_AA64DFR0_EL1_PMSVer		|
			 ID_AA64DFR0_EL1_CTX_CMPs	|
			 ID_AA64DFR0_EL1_SEBEP		|
			 ID_AA64DFR0_EL1_PMSS		|
			 ID_AA64DFR0_EL1_TraceVer);

		/*
		 * FEAT_Debugv8p9 requires support for extended breakpoints /
		 * watchpoints.
		 */
		val = ID_REG_LIMIT_FIELD_ENUM(val, ID_AA64DFR0_EL1, DebugVer, V8P8);
		break;
	}

	return val;
}

u64 kvm_vcpu_apply_reg_masks(const struct kvm_vcpu *vcpu,
			     enum vcpu_sysreg sr, u64 v)
{
	struct kvm_sysreg_masks *masks;

	masks = vcpu->kvm->arch.sysreg_masks;

	if (masks) {
		sr -= __SANITISED_REG_START__;

		v &= ~masks->mask[sr].res0;
		v |= masks->mask[sr].res1;
	}

	return v;
}

static __always_inline void set_sysreg_masks(struct kvm *kvm, int sr, u64 res0, u64 res1)
{
	int i = sr - __SANITISED_REG_START__;

	BUILD_BUG_ON(!__builtin_constant_p(sr));
	BUILD_BUG_ON(sr < __SANITISED_REG_START__);
	BUILD_BUG_ON(sr >= NR_SYS_REGS);

	kvm->arch.sysreg_masks->mask[i].res0 = res0;
	kvm->arch.sysreg_masks->mask[i].res1 = res1;
}

int kvm_init_nv_sysregs(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	u64 res0, res1;

	lockdep_assert_held(&kvm->arch.config_lock);

	if (kvm->arch.sysreg_masks)
		goto out;

	kvm->arch.sysreg_masks = kzalloc(sizeof(*(kvm->arch.sysreg_masks)),
					 GFP_KERNEL_ACCOUNT);
	if (!kvm->arch.sysreg_masks)
		return -ENOMEM;

	/* VTTBR_EL2 */
	res0 = res1 = 0;
	if (!kvm_has_feat_enum(kvm, ID_AA64MMFR1_EL1, VMIDBits, 16))
		res0 |= GENMASK(63, 56);
	if (!kvm_has_feat(kvm, ID_AA64MMFR2_EL1, CnP, IMP))
		res0 |= VTTBR_CNP_BIT;
	set_sysreg_masks(kvm, VTTBR_EL2, res0, res1);

	/* VTCR_EL2 */
	res0 = GENMASK(63, 32) | GENMASK(30, 20);
	res1 = BIT(31);
	set_sysreg_masks(kvm, VTCR_EL2, res0, res1);

	/* VMPIDR_EL2 */
	res0 = GENMASK(63, 40) | GENMASK(30, 24);
	res1 = BIT(31);
	set_sysreg_masks(kvm, VMPIDR_EL2, res0, res1);

	/* HCR_EL2 */
	get_reg_fixed_bits(kvm, HCR_EL2, &res0, &res1);
	set_sysreg_masks(kvm, HCR_EL2, res0, res1);

	/* HCRX_EL2 */
	get_reg_fixed_bits(kvm, HCRX_EL2, &res0, &res1);
	set_sysreg_masks(kvm, HCRX_EL2, res0, res1);

	/* HFG[RW]TR_EL2 */
	get_reg_fixed_bits(kvm, HFGRTR_EL2, &res0, &res1);
	set_sysreg_masks(kvm, HFGRTR_EL2, res0, res1);
	get_reg_fixed_bits(kvm, HFGWTR_EL2, &res0, &res1);
	set_sysreg_masks(kvm, HFGWTR_EL2, res0, res1);

	/* HDFG[RW]TR_EL2 */
	get_reg_fixed_bits(kvm, HDFGRTR_EL2, &res0, &res1);
	set_sysreg_masks(kvm, HDFGRTR_EL2, res0, res1);
	get_reg_fixed_bits(kvm, HDFGWTR_EL2, &res0, &res1);
	set_sysreg_masks(kvm, HDFGWTR_EL2, res0, res1);

	/* HFGITR_EL2 */
	get_reg_fixed_bits(kvm, HFGITR_EL2, &res0, &res1);
	set_sysreg_masks(kvm, HFGITR_EL2, res0, res1);

	/* HAFGRTR_EL2 - not a lot to see here */
	get_reg_fixed_bits(kvm, HAFGRTR_EL2, &res0, &res1);
	set_sysreg_masks(kvm, HAFGRTR_EL2, res0, res1);

	/* HFG[RW]TR2_EL2 */
	get_reg_fixed_bits(kvm, HFGRTR2_EL2, &res0, &res1);
	set_sysreg_masks(kvm, HFGRTR2_EL2, res0, res1);
	get_reg_fixed_bits(kvm, HFGWTR2_EL2, &res0, &res1);
	set_sysreg_masks(kvm, HFGWTR2_EL2, res0, res1);

	/* HDFG[RW]TR2_EL2 */
	get_reg_fixed_bits(kvm, HDFGRTR2_EL2, &res0, &res1);
	set_sysreg_masks(kvm, HDFGRTR2_EL2, res0, res1);
	get_reg_fixed_bits(kvm, HDFGWTR2_EL2, &res0, &res1);
	set_sysreg_masks(kvm, HDFGWTR2_EL2, res0, res1);

	/* HFGITR2_EL2 */
	get_reg_fixed_bits(kvm, HFGITR2_EL2, &res0, &res1);
	set_sysreg_masks(kvm, HFGITR2_EL2, res0, res1);

	/* TCR2_EL2 */
	get_reg_fixed_bits(kvm, TCR2_EL2, &res0, &res1);
	set_sysreg_masks(kvm, TCR2_EL2, res0, res1);

	/* SCTLR_EL1 */
	get_reg_fixed_bits(kvm, SCTLR_EL1, &res0, &res1);
	set_sysreg_masks(kvm, SCTLR_EL1, res0, res1);

	/* SCTLR2_ELx */
	get_reg_fixed_bits(kvm, SCTLR2_EL1, &res0, &res1);
	set_sysreg_masks(kvm, SCTLR2_EL1, res0, res1);
	get_reg_fixed_bits(kvm, SCTLR2_EL2, &res0, &res1);
	set_sysreg_masks(kvm, SCTLR2_EL2, res0, res1);

	/* MDCR_EL2 */
	get_reg_fixed_bits(kvm, MDCR_EL2, &res0, &res1);
	set_sysreg_masks(kvm, MDCR_EL2, res0, res1);

	/* CNTHCTL_EL2 */
	res0 = GENMASK(63, 20);
	res1 = 0;
	if (!kvm_has_feat(kvm, ID_AA64PFR0_EL1, RME, IMP))
		res0 |= CNTHCTL_CNTPMASK | CNTHCTL_CNTVMASK;
	if (!kvm_has_feat(kvm, ID_AA64MMFR0_EL1, ECV, CNTPOFF)) {
		res0 |= CNTHCTL_ECV;
		if (!kvm_has_feat(kvm, ID_AA64MMFR0_EL1, ECV, IMP))
			res0 |= (CNTHCTL_EL1TVT | CNTHCTL_EL1TVCT |
				 CNTHCTL_EL1NVPCT | CNTHCTL_EL1NVVCT);
	}
	if (!kvm_has_feat(kvm, ID_AA64MMFR1_EL1, VH, IMP))
		res0 |= GENMASK(11, 8);
	set_sysreg_masks(kvm, CNTHCTL_EL2, res0, res1);

	/* ICH_HCR_EL2 */
	res0 = ICH_HCR_EL2_RES0;
	res1 = ICH_HCR_EL2_RES1;
	if (!(kvm_vgic_global_state.ich_vtr_el2 & ICH_VTR_EL2_TDS))
		res0 |= ICH_HCR_EL2_TDIR;
	/* No GICv4 is presented to the guest */
	res0 |= ICH_HCR_EL2_DVIM | ICH_HCR_EL2_vSGIEOICount;
	set_sysreg_masks(kvm, ICH_HCR_EL2, res0, res1);

	/* VNCR_EL2 */
	set_sysreg_masks(kvm, VNCR_EL2, VNCR_EL2_RES0, VNCR_EL2_RES1);

out:
	for (enum vcpu_sysreg sr = __SANITISED_REG_START__; sr < NR_SYS_REGS; sr++)
		__vcpu_rmw_sys_reg(vcpu, sr, |=, 0);

	return 0;
}

void check_nested_vcpu_requests(struct kvm_vcpu *vcpu)
{
	if (kvm_check_request(KVM_REQ_NESTED_S2_UNMAP, vcpu)) {
		struct kvm_s2_mmu *mmu = vcpu->arch.hw_mmu;

		write_lock(&vcpu->kvm->mmu_lock);
		if (mmu->pending_unmap) {
			kvm_stage2_unmap_range(mmu, 0, kvm_phys_size(mmu), true);
			mmu->pending_unmap = false;
		}
		write_unlock(&vcpu->kvm->mmu_lock);
	}

	if (kvm_check_request(KVM_REQ_MAP_L1_VNCR_EL2, vcpu))
		kvm_map_l1_vncr(vcpu);

	/* Must be last, as may switch context! */
	if (kvm_check_request(KVM_REQ_GUEST_HYP_IRQ_PENDING, vcpu))
		kvm_inject_nested_irq(vcpu);
}

/*
 * One of the many architectural bugs in FEAT_NV2 is that the guest hypervisor
 * can write to HCR_EL2 behind our back, potentially changing the exception
 * routing / masking for even the host context.
 *
 * What follows is some slop to (1) react to exception routing / masking and (2)
 * preserve the pending SError state across translation regimes.
 */
void kvm_nested_flush_hwstate(struct kvm_vcpu *vcpu)
{
	if (!vcpu_has_nv(vcpu))
		return;

	if (unlikely(vcpu_test_and_clear_flag(vcpu, NESTED_SERROR_PENDING)))
		kvm_inject_serror_esr(vcpu, vcpu_get_vsesr(vcpu));
}

void kvm_nested_sync_hwstate(struct kvm_vcpu *vcpu)
{
	unsigned long *hcr = vcpu_hcr(vcpu);

	if (!vcpu_has_nv(vcpu))
		return;

	/*
	 * We previously decided that an SError was deliverable to the guest.
	 * Reap the pending state from HCR_EL2 and...
	 */
	if (unlikely(__test_and_clear_bit(__ffs(HCR_VSE), hcr)))
		vcpu_set_flag(vcpu, NESTED_SERROR_PENDING);

	/*
	 * Re-attempt SError injection in case the deliverability has changed,
	 * which is necessary to faithfully emulate WFI the case of a pending
	 * SError being a wakeup condition.
	 */
	if (unlikely(vcpu_test_and_clear_flag(vcpu, NESTED_SERROR_PENDING)))
		kvm_inject_serror_esr(vcpu, vcpu_get_vsesr(vcpu));
}

/*
 * KVM unconditionally sets most of these traps anyway but use an allowlist
 * to document the guest hypervisor traps that may take precedence and guard
 * against future changes to the non-nested trap configuration.
 */
#define NV_MDCR_GUEST_INCLUDE	(MDCR_EL2_TDE	|	\
				 MDCR_EL2_TDA	|	\
				 MDCR_EL2_TDRA	|	\
				 MDCR_EL2_TTRF	|	\
				 MDCR_EL2_TPMS	|	\
				 MDCR_EL2_TPM	|	\
				 MDCR_EL2_TPMCR	|	\
				 MDCR_EL2_TDCC	|	\
				 MDCR_EL2_TDOSA)

void kvm_nested_setup_mdcr_el2(struct kvm_vcpu *vcpu)
{
	u64 guest_mdcr = __vcpu_sys_reg(vcpu, MDCR_EL2);

	if (is_nested_ctxt(vcpu))
		vcpu->arch.mdcr_el2 |= (guest_mdcr & NV_MDCR_GUEST_INCLUDE);
	/*
	 * In yet another example where FEAT_NV2 is fscking broken, accesses
	 * to MDSCR_EL1 are redirected to the VNCR despite having an effect
	 * at EL2. Use a big hammer to apply sanity.
	 *
	 * Unless of course we have FEAT_FGT, in which case we can precisely
	 * trap MDSCR_EL1.
	 */
	else if (!cpus_have_final_cap(ARM64_HAS_FGT))
		vcpu->arch.mdcr_el2 |= MDCR_EL2_TDA;
}
