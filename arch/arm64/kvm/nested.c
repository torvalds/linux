// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 - Columbia University and Linaro Ltd.
 * Author: Jintack Lim <jintack.lim@linaro.org>
 */

#include <linux/bitfield.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>

#include <asm/kvm_arm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_nested.h>
#include <asm/sysreg.h>

#include "sys_regs.h"

/* Protection against the sysreg repainting madness... */
#define NV_FTR(r, f)		ID_AA64##r##_EL1_##f

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
			      ps_to_output_size(FIELD_GET(VTCR_EL2_PS_MASK, vtcr)));
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
	 * The vCPU kept its reference on the MMU after the last put, keep
	 * rolling with it.
	 */
	if (vcpu->arch.hw_mmu)
		return;

	if (is_hyp_ctxt(vcpu)) {
		vcpu->arch.hw_mmu = &vcpu->kvm->arch.mmu;
	} else {
		write_lock(&vcpu->kvm->mmu_lock);
		vcpu->arch.hw_mmu = get_s2_mmu_nested(vcpu);
		write_unlock(&vcpu->kvm->mmu_lock);
	}
}

void kvm_vcpu_put_hw_mmu(struct kvm_vcpu *vcpu)
{
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

void kvm_nested_s2_wp(struct kvm *kvm)
{
	int i;

	lockdep_assert_held_write(&kvm->mmu_lock);

	for (i = 0; i < kvm->arch.nested_mmus_size; i++) {
		struct kvm_s2_mmu *mmu = &kvm->arch.nested_mmus[i];

		if (kvm_s2_mmu_valid(mmu))
			kvm_stage2_wp_range(mmu, 0, kvm_phys_size(mmu));
	}
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
 * Our emulated CPU doesn't support all the possible features. For the
 * sake of simplicity (and probably mental sanity), wipe out a number
 * of feature bits we don't intend to support for the time being.
 * This list should get updated as new features get added to the NV
 * support, and new extension to the architecture.
 */
static void limit_nv_id_regs(struct kvm *kvm)
{
	u64 val, tmp;

	/* Support everything but TME */
	val = kvm_read_vm_id_reg(kvm, SYS_ID_AA64ISAR0_EL1);
	val &= ~NV_FTR(ISAR0, TME);
	kvm_set_vm_id_reg(kvm, SYS_ID_AA64ISAR0_EL1, val);

	/* Support everything but Spec Invalidation and LS64 */
	val = kvm_read_vm_id_reg(kvm, SYS_ID_AA64ISAR1_EL1);
	val &= ~(NV_FTR(ISAR1, LS64)	|
		 NV_FTR(ISAR1, SPECRES));
	kvm_set_vm_id_reg(kvm, SYS_ID_AA64ISAR1_EL1, val);

	/* No AMU, MPAM, S-EL2, or RAS */
	val = kvm_read_vm_id_reg(kvm, SYS_ID_AA64PFR0_EL1);
	val &= ~(GENMASK_ULL(55, 52)	|
		 NV_FTR(PFR0, AMU)	|
		 NV_FTR(PFR0, MPAM)	|
		 NV_FTR(PFR0, SEL2)	|
		 NV_FTR(PFR0, RAS)	|
		 NV_FTR(PFR0, EL3)	|
		 NV_FTR(PFR0, EL2)	|
		 NV_FTR(PFR0, EL1)	|
		 NV_FTR(PFR0, EL0));
	/* 64bit only at any EL */
	val |= FIELD_PREP(NV_FTR(PFR0, EL0), 0b0001);
	val |= FIELD_PREP(NV_FTR(PFR0, EL1), 0b0001);
	val |= FIELD_PREP(NV_FTR(PFR0, EL2), 0b0001);
	val |= FIELD_PREP(NV_FTR(PFR0, EL3), 0b0001);
	kvm_set_vm_id_reg(kvm, SYS_ID_AA64PFR0_EL1, val);

	/* Only support BTI, SSBS, CSV2_frac */
	val = kvm_read_vm_id_reg(kvm, SYS_ID_AA64PFR1_EL1);
	val &= (NV_FTR(PFR1, BT)	|
		NV_FTR(PFR1, SSBS)	|
		NV_FTR(PFR1, CSV2_frac));
	kvm_set_vm_id_reg(kvm, SYS_ID_AA64PFR1_EL1, val);

	/* Hide ECV, ExS, Secure Memory */
	val = kvm_read_vm_id_reg(kvm, SYS_ID_AA64MMFR0_EL1);
	val &= ~(NV_FTR(MMFR0, ECV)		|
		 NV_FTR(MMFR0, EXS)		|
		 NV_FTR(MMFR0, TGRAN4_2)	|
		 NV_FTR(MMFR0, TGRAN16_2)	|
		 NV_FTR(MMFR0, TGRAN64_2)	|
		 NV_FTR(MMFR0, SNSMEM));

	/* Disallow unsupported S2 page sizes */
	switch (PAGE_SIZE) {
	case SZ_64K:
		val |= FIELD_PREP(NV_FTR(MMFR0, TGRAN16_2), 0b0001);
		fallthrough;
	case SZ_16K:
		val |= FIELD_PREP(NV_FTR(MMFR0, TGRAN4_2), 0b0001);
		fallthrough;
	case SZ_4K:
		/* Support everything */
		break;
	}
	/*
	 * Since we can't support a guest S2 page size smaller than
	 * the host's own page size (due to KVM only populating its
	 * own S2 using the kernel's page size), advertise the
	 * limitation using FEAT_GTG.
	 */
	switch (PAGE_SIZE) {
	case SZ_4K:
		val |= FIELD_PREP(NV_FTR(MMFR0, TGRAN4_2), 0b0010);
		fallthrough;
	case SZ_16K:
		val |= FIELD_PREP(NV_FTR(MMFR0, TGRAN16_2), 0b0010);
		fallthrough;
	case SZ_64K:
		val |= FIELD_PREP(NV_FTR(MMFR0, TGRAN64_2), 0b0010);
		break;
	}
	/* Cap PARange to 48bits */
	tmp = FIELD_GET(NV_FTR(MMFR0, PARANGE), val);
	if (tmp > 0b0101) {
		val &= ~NV_FTR(MMFR0, PARANGE);
		val |= FIELD_PREP(NV_FTR(MMFR0, PARANGE), 0b0101);
	}
	kvm_set_vm_id_reg(kvm, SYS_ID_AA64MMFR0_EL1, val);

	val = kvm_read_vm_id_reg(kvm, SYS_ID_AA64MMFR1_EL1);
	val &= (NV_FTR(MMFR1, HCX)	|
		NV_FTR(MMFR1, PAN)	|
		NV_FTR(MMFR1, LO)	|
		NV_FTR(MMFR1, HPDS)	|
		NV_FTR(MMFR1, VH)	|
		NV_FTR(MMFR1, VMIDBits));
	kvm_set_vm_id_reg(kvm, SYS_ID_AA64MMFR1_EL1, val);

	val = kvm_read_vm_id_reg(kvm, SYS_ID_AA64MMFR2_EL1);
	val &= ~(NV_FTR(MMFR2, BBM)	|
		 NV_FTR(MMFR2, TTL)	|
		 GENMASK_ULL(47, 44)	|
		 NV_FTR(MMFR2, ST)	|
		 NV_FTR(MMFR2, CCIDX)	|
		 NV_FTR(MMFR2, VARange));

	/* Force TTL support */
	val |= FIELD_PREP(NV_FTR(MMFR2, TTL), 0b0001);
	kvm_set_vm_id_reg(kvm, SYS_ID_AA64MMFR2_EL1, val);

	val = 0;
	if (!cpus_have_final_cap(ARM64_HAS_HCR_NV1))
		val |= FIELD_PREP(NV_FTR(MMFR4, E2H0),
				  ID_AA64MMFR4_EL1_E2H0_NI_NV1);
	kvm_set_vm_id_reg(kvm, SYS_ID_AA64MMFR4_EL1, val);

	/* Only limited support for PMU, Debug, BPs, WPs, and HPMN0 */
	val = kvm_read_vm_id_reg(kvm, SYS_ID_AA64DFR0_EL1);
	val &= (NV_FTR(DFR0, PMUVer)	|
		NV_FTR(DFR0, WRPs)	|
		NV_FTR(DFR0, BRPs)	|
		NV_FTR(DFR0, DebugVer)	|
		NV_FTR(DFR0, HPMN0));

	/* Cap Debug to ARMv8.1 */
	tmp = FIELD_GET(NV_FTR(DFR0, DebugVer), val);
	if (tmp > 0b0111) {
		val &= ~NV_FTR(DFR0, DebugVer);
		val |= FIELD_PREP(NV_FTR(DFR0, DebugVer), 0b0111);
	}
	kvm_set_vm_id_reg(kvm, SYS_ID_AA64DFR0_EL1, val);
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

	limit_nv_id_regs(kvm);

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
	res0 = BIT(48);
	res1 = HCR_RW;
	if (!kvm_has_feat(kvm, ID_AA64MMFR1_EL1, TWED, IMP))
		res0 |= GENMASK(63, 59);
	if (!kvm_has_feat(kvm, ID_AA64PFR1_EL1, MTE, MTE2))
		res0 |= (HCR_TID5 | HCR_DCT | HCR_ATA);
	if (!kvm_has_feat(kvm, ID_AA64MMFR2_EL1, EVT, TTLBxS))
		res0 |= (HCR_TTLBIS | HCR_TTLBOS);
	if (!kvm_has_feat(kvm, ID_AA64PFR0_EL1, CSV2, CSV2_2) &&
	    !kvm_has_feat(kvm, ID_AA64PFR1_EL1, CSV2_frac, CSV2_1p2))
		res0 |= HCR_ENSCXT;
	if (!kvm_has_feat(kvm, ID_AA64MMFR2_EL1, EVT, IMP))
		res0 |= (HCR_TOCU | HCR_TICAB | HCR_TID4);
	if (!kvm_has_feat(kvm, ID_AA64PFR0_EL1, AMU, V1P1))
		res0 |= HCR_AMVOFFEN;
	if (!kvm_has_feat(kvm, ID_AA64PFR0_EL1, RAS, V1P1))
		res0 |= HCR_FIEN;
	if (!kvm_has_feat(kvm, ID_AA64MMFR2_EL1, FWB, IMP))
		res0 |= HCR_FWB;
	if (!kvm_has_feat(kvm, ID_AA64MMFR2_EL1, NV, NV2))
		res0 |= HCR_NV2;
	if (!kvm_has_feat(kvm, ID_AA64MMFR2_EL1, NV, IMP))
		res0 |= (HCR_AT | HCR_NV1 | HCR_NV);
	if (!(kvm_vcpu_has_feature(kvm, KVM_ARM_VCPU_PTRAUTH_ADDRESS) &&
	      kvm_vcpu_has_feature(kvm, KVM_ARM_VCPU_PTRAUTH_GENERIC)))
		res0 |= (HCR_API | HCR_APK);
	if (!kvm_has_feat(kvm, ID_AA64ISAR0_EL1, TME, IMP))
		res0 |= BIT(39);
	if (!kvm_has_feat(kvm, ID_AA64PFR0_EL1, RAS, IMP))
		res0 |= (HCR_TEA | HCR_TERR);
	if (!kvm_has_feat(kvm, ID_AA64MMFR1_EL1, LO, IMP))
		res0 |= HCR_TLOR;
	if (!kvm_has_feat(kvm, ID_AA64MMFR4_EL1, E2H0, IMP))
		res1 |= HCR_E2H;
	set_sysreg_masks(kvm, HCR_EL2, res0, res1);

	/* HCRX_EL2 */
	res0 = HCRX_EL2_RES0;
	res1 = HCRX_EL2_RES1;
	if (!kvm_has_feat(kvm, ID_AA64ISAR3_EL1, PACM, TRIVIAL_IMP))
		res0 |= HCRX_EL2_PACMEn;
	if (!kvm_has_feat(kvm, ID_AA64PFR2_EL1, FPMR, IMP))
		res0 |= HCRX_EL2_EnFPM;
	if (!kvm_has_feat(kvm, ID_AA64PFR1_EL1, GCS, IMP))
		res0 |= HCRX_EL2_GCSEn;
	if (!kvm_has_feat(kvm, ID_AA64ISAR2_EL1, SYSREG_128, IMP))
		res0 |= HCRX_EL2_EnIDCP128;
	if (!kvm_has_feat(kvm, ID_AA64MMFR3_EL1, ADERR, DEV_ASYNC))
		res0 |= (HCRX_EL2_EnSDERR | HCRX_EL2_EnSNERR);
	if (!kvm_has_feat(kvm, ID_AA64PFR1_EL1, DF2, IMP))
		res0 |= HCRX_EL2_TMEA;
	if (!kvm_has_feat(kvm, ID_AA64MMFR3_EL1, D128, IMP))
		res0 |= HCRX_EL2_D128En;
	if (!kvm_has_feat(kvm, ID_AA64PFR1_EL1, THE, IMP))
		res0 |= HCRX_EL2_PTTWI;
	if (!kvm_has_feat(kvm, ID_AA64MMFR3_EL1, SCTLRX, IMP))
		res0 |= HCRX_EL2_SCTLR2En;
	if (!kvm_has_tcr2(kvm))
		res0 |= HCRX_EL2_TCR2En;
	if (!kvm_has_feat(kvm, ID_AA64ISAR2_EL1, MOPS, IMP))
		res0 |= (HCRX_EL2_MSCEn | HCRX_EL2_MCE2);
	if (!kvm_has_feat(kvm, ID_AA64MMFR1_EL1, CMOW, IMP))
		res0 |= HCRX_EL2_CMOW;
	if (!kvm_has_feat(kvm, ID_AA64PFR1_EL1, NMI, IMP))
		res0 |= (HCRX_EL2_VFNMI | HCRX_EL2_VINMI | HCRX_EL2_TALLINT);
	if (!kvm_has_feat(kvm, ID_AA64PFR1_EL1, SME, IMP) ||
	    !(read_sysreg_s(SYS_SMIDR_EL1) & SMIDR_EL1_SMPS))
		res0 |= HCRX_EL2_SMPME;
	if (!kvm_has_feat(kvm, ID_AA64ISAR1_EL1, XS, IMP))
		res0 |= (HCRX_EL2_FGTnXS | HCRX_EL2_FnXS);
	if (!kvm_has_feat(kvm, ID_AA64ISAR1_EL1, LS64, LS64_V))
		res0 |= HCRX_EL2_EnASR;
	if (!kvm_has_feat(kvm, ID_AA64ISAR1_EL1, LS64, LS64))
		res0 |= HCRX_EL2_EnALS;
	if (!kvm_has_feat(kvm, ID_AA64ISAR1_EL1, LS64, LS64_ACCDATA))
		res0 |= HCRX_EL2_EnAS0;
	set_sysreg_masks(kvm, HCRX_EL2, res0, res1);

	/* HFG[RW]TR_EL2 */
	res0 = res1 = 0;
	if (!(kvm_vcpu_has_feature(kvm, KVM_ARM_VCPU_PTRAUTH_ADDRESS) &&
	      kvm_vcpu_has_feature(kvm, KVM_ARM_VCPU_PTRAUTH_GENERIC)))
		res0 |= (HFGxTR_EL2_APDAKey | HFGxTR_EL2_APDBKey |
			 HFGxTR_EL2_APGAKey | HFGxTR_EL2_APIAKey |
			 HFGxTR_EL2_APIBKey);
	if (!kvm_has_feat(kvm, ID_AA64MMFR1_EL1, LO, IMP))
		res0 |= (HFGxTR_EL2_LORC_EL1 | HFGxTR_EL2_LOREA_EL1 |
			 HFGxTR_EL2_LORID_EL1 | HFGxTR_EL2_LORN_EL1 |
			 HFGxTR_EL2_LORSA_EL1);
	if (!kvm_has_feat(kvm, ID_AA64PFR0_EL1, CSV2, CSV2_2) &&
	    !kvm_has_feat(kvm, ID_AA64PFR1_EL1, CSV2_frac, CSV2_1p2))
		res0 |= (HFGxTR_EL2_SCXTNUM_EL1 | HFGxTR_EL2_SCXTNUM_EL0);
	if (!kvm_has_feat(kvm, ID_AA64PFR0_EL1, GIC, IMP))
		res0 |= HFGxTR_EL2_ICC_IGRPENn_EL1;
	if (!kvm_has_feat(kvm, ID_AA64PFR0_EL1, RAS, IMP))
		res0 |= (HFGxTR_EL2_ERRIDR_EL1 | HFGxTR_EL2_ERRSELR_EL1 |
			 HFGxTR_EL2_ERXFR_EL1 | HFGxTR_EL2_ERXCTLR_EL1 |
			 HFGxTR_EL2_ERXSTATUS_EL1 | HFGxTR_EL2_ERXMISCn_EL1 |
			 HFGxTR_EL2_ERXPFGF_EL1 | HFGxTR_EL2_ERXPFGCTL_EL1 |
			 HFGxTR_EL2_ERXPFGCDN_EL1 | HFGxTR_EL2_ERXADDR_EL1);
	if (!kvm_has_feat(kvm, ID_AA64ISAR1_EL1, LS64, LS64_ACCDATA))
		res0 |= HFGxTR_EL2_nACCDATA_EL1;
	if (!kvm_has_feat(kvm, ID_AA64PFR1_EL1, GCS, IMP))
		res0 |= (HFGxTR_EL2_nGCS_EL0 | HFGxTR_EL2_nGCS_EL1);
	if (!kvm_has_feat(kvm, ID_AA64PFR1_EL1, SME, IMP))
		res0 |= (HFGxTR_EL2_nSMPRI_EL1 | HFGxTR_EL2_nTPIDR2_EL0);
	if (!kvm_has_feat(kvm, ID_AA64PFR1_EL1, THE, IMP))
		res0 |= HFGxTR_EL2_nRCWMASK_EL1;
	if (!kvm_has_s1pie(kvm))
		res0 |= (HFGxTR_EL2_nPIRE0_EL1 | HFGxTR_EL2_nPIR_EL1);
	if (!kvm_has_s1poe(kvm))
		res0 |= (HFGxTR_EL2_nPOR_EL0 | HFGxTR_EL2_nPOR_EL1);
	if (!kvm_has_feat(kvm, ID_AA64MMFR3_EL1, S2POE, IMP))
		res0 |= HFGxTR_EL2_nS2POR_EL1;
	if (!kvm_has_feat(kvm, ID_AA64MMFR3_EL1, AIE, IMP))
		res0 |= (HFGxTR_EL2_nMAIR2_EL1 | HFGxTR_EL2_nAMAIR2_EL1);
	set_sysreg_masks(kvm, HFGRTR_EL2, res0 | __HFGRTR_EL2_RES0, res1);
	set_sysreg_masks(kvm, HFGWTR_EL2, res0 | __HFGWTR_EL2_RES0, res1);

	/* HDFG[RW]TR_EL2 */
	res0 = res1 = 0;
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, DoubleLock, IMP))
		res0 |= HDFGRTR_EL2_OSDLR_EL1;
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, PMUVer, IMP))
		res0 |= (HDFGRTR_EL2_PMEVCNTRn_EL0 | HDFGRTR_EL2_PMEVTYPERn_EL0 |
			 HDFGRTR_EL2_PMCCFILTR_EL0 | HDFGRTR_EL2_PMCCNTR_EL0 |
			 HDFGRTR_EL2_PMCNTEN | HDFGRTR_EL2_PMINTEN |
			 HDFGRTR_EL2_PMOVS | HDFGRTR_EL2_PMSELR_EL0 |
			 HDFGRTR_EL2_PMMIR_EL1 | HDFGRTR_EL2_PMUSERENR_EL0 |
			 HDFGRTR_EL2_PMCEIDn_EL0);
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, PMSVer, IMP))
		res0 |= (HDFGRTR_EL2_PMBLIMITR_EL1 | HDFGRTR_EL2_PMBPTR_EL1 |
			 HDFGRTR_EL2_PMBSR_EL1 | HDFGRTR_EL2_PMSCR_EL1 |
			 HDFGRTR_EL2_PMSEVFR_EL1 | HDFGRTR_EL2_PMSFCR_EL1 |
			 HDFGRTR_EL2_PMSICR_EL1 | HDFGRTR_EL2_PMSIDR_EL1 |
			 HDFGRTR_EL2_PMSIRR_EL1 | HDFGRTR_EL2_PMSLATFR_EL1 |
			 HDFGRTR_EL2_PMBIDR_EL1);
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, TraceVer, IMP))
		res0 |= (HDFGRTR_EL2_TRC | HDFGRTR_EL2_TRCAUTHSTATUS |
			 HDFGRTR_EL2_TRCAUXCTLR | HDFGRTR_EL2_TRCCLAIM |
			 HDFGRTR_EL2_TRCCNTVRn | HDFGRTR_EL2_TRCID |
			 HDFGRTR_EL2_TRCIMSPECn | HDFGRTR_EL2_TRCOSLSR |
			 HDFGRTR_EL2_TRCPRGCTLR | HDFGRTR_EL2_TRCSEQSTR |
			 HDFGRTR_EL2_TRCSSCSRn | HDFGRTR_EL2_TRCSTATR |
			 HDFGRTR_EL2_TRCVICTLR);
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, TraceBuffer, IMP))
		res0 |= (HDFGRTR_EL2_TRBBASER_EL1 | HDFGRTR_EL2_TRBIDR_EL1 |
			 HDFGRTR_EL2_TRBLIMITR_EL1 | HDFGRTR_EL2_TRBMAR_EL1 |
			 HDFGRTR_EL2_TRBPTR_EL1 | HDFGRTR_EL2_TRBSR_EL1 |
			 HDFGRTR_EL2_TRBTRG_EL1);
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, BRBE, IMP))
		res0 |= (HDFGRTR_EL2_nBRBIDR | HDFGRTR_EL2_nBRBCTL |
			 HDFGRTR_EL2_nBRBDATA);
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, PMSVer, V1P2))
		res0 |= HDFGRTR_EL2_nPMSNEVFR_EL1;
	set_sysreg_masks(kvm, HDFGRTR_EL2, res0 | HDFGRTR_EL2_RES0, res1);

	/* Reuse the bits from the read-side and add the write-specific stuff */
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, PMUVer, IMP))
		res0 |= (HDFGWTR_EL2_PMCR_EL0 | HDFGWTR_EL2_PMSWINC_EL0);
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, TraceVer, IMP))
		res0 |= HDFGWTR_EL2_TRCOSLAR;
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, TraceFilt, IMP))
		res0 |= HDFGWTR_EL2_TRFCR_EL1;
	set_sysreg_masks(kvm, HFGWTR_EL2, res0 | HDFGWTR_EL2_RES0, res1);

	/* HFGITR_EL2 */
	res0 = HFGITR_EL2_RES0;
	res1 = HFGITR_EL2_RES1;
	if (!kvm_has_feat(kvm, ID_AA64ISAR1_EL1, DPB, DPB2))
		res0 |= HFGITR_EL2_DCCVADP;
	if (!kvm_has_feat(kvm, ID_AA64MMFR1_EL1, PAN, PAN2))
		res0 |= (HFGITR_EL2_ATS1E1RP | HFGITR_EL2_ATS1E1WP);
	if (!kvm_has_feat(kvm, ID_AA64ISAR0_EL1, TLB, OS))
		res0 |= (HFGITR_EL2_TLBIRVAALE1OS | HFGITR_EL2_TLBIRVALE1OS |
			 HFGITR_EL2_TLBIRVAAE1OS | HFGITR_EL2_TLBIRVAE1OS |
			 HFGITR_EL2_TLBIVAALE1OS | HFGITR_EL2_TLBIVALE1OS |
			 HFGITR_EL2_TLBIVAAE1OS | HFGITR_EL2_TLBIASIDE1OS |
			 HFGITR_EL2_TLBIVAE1OS | HFGITR_EL2_TLBIVMALLE1OS);
	if (!kvm_has_feat(kvm, ID_AA64ISAR0_EL1, TLB, RANGE))
		res0 |= (HFGITR_EL2_TLBIRVAALE1 | HFGITR_EL2_TLBIRVALE1 |
			 HFGITR_EL2_TLBIRVAAE1 | HFGITR_EL2_TLBIRVAE1 |
			 HFGITR_EL2_TLBIRVAALE1IS | HFGITR_EL2_TLBIRVALE1IS |
			 HFGITR_EL2_TLBIRVAAE1IS | HFGITR_EL2_TLBIRVAE1IS |
			 HFGITR_EL2_TLBIRVAALE1OS | HFGITR_EL2_TLBIRVALE1OS |
			 HFGITR_EL2_TLBIRVAAE1OS | HFGITR_EL2_TLBIRVAE1OS);
	if (!kvm_has_feat(kvm, ID_AA64ISAR1_EL1, SPECRES, IMP))
		res0 |= (HFGITR_EL2_CFPRCTX | HFGITR_EL2_DVPRCTX |
			 HFGITR_EL2_CPPRCTX);
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, BRBE, IMP))
		res0 |= (HFGITR_EL2_nBRBINJ | HFGITR_EL2_nBRBIALL);
	if (!kvm_has_feat(kvm, ID_AA64PFR1_EL1, GCS, IMP))
		res0 |= (HFGITR_EL2_nGCSPUSHM_EL1 | HFGITR_EL2_nGCSSTR_EL1 |
			 HFGITR_EL2_nGCSEPP);
	if (!kvm_has_feat(kvm, ID_AA64ISAR1_EL1, SPECRES, COSP_RCTX))
		res0 |= HFGITR_EL2_COSPRCTX;
	if (!kvm_has_feat(kvm, ID_AA64ISAR2_EL1, ATS1A, IMP))
		res0 |= HFGITR_EL2_ATS1E1A;
	set_sysreg_masks(kvm, HFGITR_EL2, res0, res1);

	/* HAFGRTR_EL2 - not a lot to see here */
	res0 = HAFGRTR_EL2_RES0;
	res1 = HAFGRTR_EL2_RES1;
	if (!kvm_has_feat(kvm, ID_AA64PFR0_EL1, AMU, V1P1))
		res0 |= ~(res0 | res1);
	set_sysreg_masks(kvm, HAFGRTR_EL2, res0, res1);

	/* TCR2_EL2 */
	res0 = TCR2_EL2_RES0;
	res1 = TCR2_EL2_RES1;
	if (!kvm_has_feat(kvm, ID_AA64MMFR3_EL1, D128, IMP))
		res0 |= (TCR2_EL2_DisCH0 | TCR2_EL2_DisCH1 | TCR2_EL2_D128);
	if (!kvm_has_feat(kvm, ID_AA64MMFR3_EL1, MEC, IMP))
		res0 |= TCR2_EL2_AMEC1 | TCR2_EL2_AMEC0;
	if (!kvm_has_feat(kvm, ID_AA64MMFR1_EL1, HAFDBS, HAFT))
		res0 |= TCR2_EL2_HAFT;
	if (!kvm_has_feat(kvm, ID_AA64PFR1_EL1, THE, IMP))
		res0 |= TCR2_EL2_PTTWI | TCR2_EL2_PnCH;
	if (!kvm_has_feat(kvm, ID_AA64MMFR3_EL1, AIE, IMP))
		res0 |= TCR2_EL2_AIE;
	if (!kvm_has_s1poe(kvm))
		res0 |= TCR2_EL2_POE | TCR2_EL2_E0POE;
	if (!kvm_has_s1pie(kvm))
		res0 |= TCR2_EL2_PIE;
	if (!kvm_has_feat(kvm, ID_AA64MMFR1_EL1, VH, IMP))
		res0 |= (TCR2_EL2_E0POE | TCR2_EL2_D128 |
			 TCR2_EL2_AMEC1 | TCR2_EL2_DisCH0 | TCR2_EL2_DisCH1);
	set_sysreg_masks(kvm, TCR2_EL2, res0, res1);

	/* SCTLR_EL1 */
	res0 = SCTLR_EL1_RES0;
	res1 = SCTLR_EL1_RES1;
	if (!kvm_has_feat(kvm, ID_AA64MMFR1_EL1, PAN, PAN3))
		res0 |= SCTLR_EL1_EPAN;
	set_sysreg_masks(kvm, SCTLR_EL1, res0, res1);

	/* MDCR_EL2 */
	res0 = MDCR_EL2_RES0;
	res1 = MDCR_EL2_RES1;
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, PMUVer, IMP))
		res0 |= (MDCR_EL2_HPMN | MDCR_EL2_TPMCR |
			 MDCR_EL2_TPM | MDCR_EL2_HPME);
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, PMSVer, IMP))
		res0 |= MDCR_EL2_E2PB | MDCR_EL2_TPMS;
	if (!kvm_has_feat(kvm, ID_AA64DFR1_EL1, SPMU, IMP))
		res0 |= MDCR_EL2_EnSPM;
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, PMUVer, V3P1))
		res0 |= MDCR_EL2_HPMD;
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, TraceFilt, IMP))
		res0 |= MDCR_EL2_TTRF;
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, PMUVer, V3P5))
		res0 |= MDCR_EL2_HCCD | MDCR_EL2_HLP;
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, TraceBuffer, IMP))
		res0 |= MDCR_EL2_E2TB;
	if (!kvm_has_feat(kvm, ID_AA64MMFR0_EL1, FGT, IMP))
		res0 |= MDCR_EL2_TDCC;
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, MTPMU, IMP) ||
	    kvm_has_feat(kvm, ID_AA64PFR0_EL1, EL3, IMP))
		res0 |= MDCR_EL2_MTPME;
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, PMUVer, V3P7))
		res0 |= MDCR_EL2_HPMFZO;
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, PMSS, IMP))
		res0 |= MDCR_EL2_PMSSE;
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, PMSVer, V1P2))
		res0 |= MDCR_EL2_HPMFZS;
	if (!kvm_has_feat(kvm, ID_AA64DFR1_EL1, EBEP, IMP))
		res0 |= MDCR_EL2_PMEE;
	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, DebugVer, V8P9))
		res0 |= MDCR_EL2_EBWE;
	if (!kvm_has_feat(kvm, ID_AA64DFR2_EL1, STEP, IMP))
		res0 |= MDCR_EL2_EnSTEPOP;
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

out:
	for (enum vcpu_sysreg sr = __SANITISED_REG_START__; sr < NR_SYS_REGS; sr++)
		(void)__vcpu_sys_reg(vcpu, sr);

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
}
