// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 - Columbia University and Linaro Ltd.
 * Author: Jintack Lim <jintack.lim@linaro.org>
 */

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
			size_mul(sizeof(*kvm->arch.nested_mmus), kvm->arch.nested_mmus_size),
			size_mul(sizeof(*kvm->arch.nested_mmus), num_mmus),
			GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!tmp)
		return -ENOMEM;

	/*
	 * If we went through a realocation, adjust the MMU back-pointers in
	 * the previously initialised kvm_pgtable structures.
	 */
	if (kvm->arch.nested_mmus != tmp)
		for (int i = 0; i < kvm->arch.nested_mmus_size; i++)
			tmp[i].pgt->mmu = &tmp[i];

	for (int i = kvm->arch.nested_mmus_size; !ret && i < num_mmus; i++)
		ret = init_nested_s2_mmu(kvm, &tmp[i]);

	if (ret) {
		for (int i = kvm->arch.nested_mmus_size; i < num_mmus; i++)
			kvm_free_stage2_pgd(&tmp[i]);

		return ret;
	}

	kvm->arch.nested_mmus_size = num_mmus;
	kvm->arch.nested_mmus = tmp;

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

static unsigned int ps_to_output_size(unsigned int ps)
{
	switch (ps) {
	case 0: return 32;
	case 1: return 36;
	case 2: return 40;
	case 3: return 42;
	case 4: return 44;
	case 5:
	default:
		return 48;
	}
}

static u32 compute_fsc(int level, u32 fsc)
{
	return fsc | (level & 0x3);
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
			out->upper_attr = desc;
			return 1;
		}

		/* We're at the final level or block translation level */
		if ((desc & 3) == 1 || level == 3)
			break;

		if (check_output_size(wi, desc)) {
			out->esr = compute_fsc(level, ESR_ELx_FSC_ADDRSZ);
			out->upper_attr = desc;
			return 1;
		}

		base_addr = desc & GENMASK_ULL(47, wi->pgshift);

		level += 1;
		addr_top = addr_bottom - 1;
	}

	if (level < first_block_level) {
		out->esr = compute_fsc(level, ESR_ELx_FSC_FAULT);
		out->upper_attr = desc;
		return 1;
	}

	/*
	 * We don't use the contiguous bit in the stage-2 ptes, so skip check
	 * for misprogramming of the contiguous bit.
	 */

	if (check_output_size(wi, desc)) {
		out->esr = compute_fsc(level, ESR_ELx_FSC_ADDRSZ);
		out->upper_attr = desc;
		return 1;
	}

	if (!(desc & BIT(10))) {
		out->esr = compute_fsc(level, ESR_ELx_FSC_ACCESS);
		out->upper_attr = desc;
		return 1;
	}

	/* Calculate and return the result */
	paddr = (desc & GENMASK_ULL(47, addr_bottom)) |
		(ipa & GENMASK_ULL(addr_bottom - 1, 0));
	out->output = paddr;
	out->block_size = 1UL << ((3 - level) * stride + wi->pgshift);
	out->readable = desc & (0b01 << 6);
	out->writable = desc & (0b10 << 6);
	out->level = level;
	out->upper_attr = desc & GENMASK_ULL(63, 52);
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

	/* Clear the old state */
	if (kvm_s2_mmu_valid(s2_mmu))
		kvm_stage2_unmap_range(s2_mmu, 0, kvm_phys_size(s2_mmu));

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
	if (kvm_is_nested_s2_mmu(vcpu->kvm, vcpu->arch.hw_mmu)) {
		atomic_dec(&vcpu->arch.hw_mmu->refcnt);
		vcpu->arch.hw_mmu = NULL;
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
	kfree(kvm->arch.nested_mmus);
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
static u64 limit_nv_id_reg(u32 id, u64 val)
{
	u64 tmp;

	switch (id) {
	case SYS_ID_AA64ISAR0_EL1:
		/* Support everything but TME, O.S. and Range TLBIs */
		val &= ~(NV_FTR(ISAR0, TLB)		|
			 NV_FTR(ISAR0, TME));
		break;

	case SYS_ID_AA64ISAR1_EL1:
		/* Support everything but Spec Invalidation */
		val &= ~(GENMASK_ULL(63, 56)	|
			 NV_FTR(ISAR1, SPECRES));
		break;

	case SYS_ID_AA64PFR0_EL1:
		/* No AMU, MPAM, S-EL2, RAS or SVE */
		val &= ~(GENMASK_ULL(55, 52)	|
			 NV_FTR(PFR0, AMU)	|
			 NV_FTR(PFR0, MPAM)	|
			 NV_FTR(PFR0, SEL2)	|
			 NV_FTR(PFR0, RAS)	|
			 NV_FTR(PFR0, SVE)	|
			 NV_FTR(PFR0, EL3)	|
			 NV_FTR(PFR0, EL2)	|
			 NV_FTR(PFR0, EL1));
		/* 64bit EL1/EL2/EL3 only */
		val |= FIELD_PREP(NV_FTR(PFR0, EL1), 0b0001);
		val |= FIELD_PREP(NV_FTR(PFR0, EL2), 0b0001);
		val |= FIELD_PREP(NV_FTR(PFR0, EL3), 0b0001);
		break;

	case SYS_ID_AA64PFR1_EL1:
		/* Only support BTI, SSBS, CSV2_frac */
		val &= (NV_FTR(PFR1, BT)	|
			NV_FTR(PFR1, SSBS)	|
			NV_FTR(PFR1, CSV2_frac));
		break;

	case SYS_ID_AA64MMFR0_EL1:
		/* Hide ECV, ExS, Secure Memory */
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
		break;

	case SYS_ID_AA64MMFR1_EL1:
		val &= (NV_FTR(MMFR1, HCX)	|
			NV_FTR(MMFR1, PAN)	|
			NV_FTR(MMFR1, LO)	|
			NV_FTR(MMFR1, HPDS)	|
			NV_FTR(MMFR1, VH)	|
			NV_FTR(MMFR1, VMIDBits));
		break;

	case SYS_ID_AA64MMFR2_EL1:
		val &= ~(NV_FTR(MMFR2, BBM)	|
			 NV_FTR(MMFR2, TTL)	|
			 GENMASK_ULL(47, 44)	|
			 NV_FTR(MMFR2, ST)	|
			 NV_FTR(MMFR2, CCIDX)	|
			 NV_FTR(MMFR2, VARange));

		/* Force TTL support */
		val |= FIELD_PREP(NV_FTR(MMFR2, TTL), 0b0001);
		break;

	case SYS_ID_AA64MMFR4_EL1:
		val = 0;
		if (!cpus_have_final_cap(ARM64_HAS_HCR_NV1))
			val |= FIELD_PREP(NV_FTR(MMFR4, E2H0),
					  ID_AA64MMFR4_EL1_E2H0_NI_NV1);
		break;

	case SYS_ID_AA64DFR0_EL1:
		/* Only limited support for PMU, Debug, BPs and WPs */
		val &= (NV_FTR(DFR0, PMUVer)	|
			NV_FTR(DFR0, WRPs)	|
			NV_FTR(DFR0, BRPs)	|
			NV_FTR(DFR0, DebugVer));

		/* Cap Debug to ARMv8.1 */
		tmp = FIELD_GET(NV_FTR(DFR0, DebugVer), val);
		if (tmp > 0b0111) {
			val &= ~NV_FTR(DFR0, DebugVer);
			val |= FIELD_PREP(NV_FTR(DFR0, DebugVer), 0b0111);
		}
		break;

	default:
		/* Unknown register, just wipe it clean */
		val = 0;
		break;
	}

	return val;
}

u64 kvm_vcpu_sanitise_vncr_reg(const struct kvm_vcpu *vcpu, enum vcpu_sysreg sr)
{
	u64 v = ctxt_sys_reg(&vcpu->arch.ctxt, sr);
	struct kvm_sysreg_masks *masks;

	masks = vcpu->kvm->arch.sysreg_masks;

	if (masks) {
		sr -= __VNCR_START__;

		v &= ~masks->mask[sr].res0;
		v |= masks->mask[sr].res1;
	}

	return v;
}

static void set_sysreg_masks(struct kvm *kvm, int sr, u64 res0, u64 res1)
{
	int i = sr - __VNCR_START__;

	kvm->arch.sysreg_masks->mask[i].res0 = res0;
	kvm->arch.sysreg_masks->mask[i].res1 = res1;
}

int kvm_init_nv_sysregs(struct kvm *kvm)
{
	u64 res0, res1;
	int ret = 0;

	mutex_lock(&kvm->arch.config_lock);

	if (kvm->arch.sysreg_masks)
		goto out;

	kvm->arch.sysreg_masks = kzalloc(sizeof(*(kvm->arch.sysreg_masks)),
					 GFP_KERNEL);
	if (!kvm->arch.sysreg_masks) {
		ret = -ENOMEM;
		goto out;
	}

	for (int i = 0; i < KVM_ARM_ID_REG_NUM; i++)
		kvm->arch.id_regs[i] = limit_nv_id_reg(IDX_IDREG(i),
						       kvm->arch.id_regs[i]);

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
	if (!(__vcpu_has_feature(&kvm->arch, KVM_ARM_VCPU_PTRAUTH_ADDRESS) &&
	      __vcpu_has_feature(&kvm->arch, KVM_ARM_VCPU_PTRAUTH_GENERIC)))
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
	if (!kvm_has_feat(kvm, ID_AA64MMFR3_EL1, TCRX, IMP))
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
	if (!(__vcpu_has_feature(&kvm->arch, KVM_ARM_VCPU_PTRAUTH_ADDRESS) &&
	      __vcpu_has_feature(&kvm->arch, KVM_ARM_VCPU_PTRAUTH_GENERIC)))
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
	if (!kvm_has_feat(kvm, ID_AA64MMFR3_EL1, S1PIE, IMP))
		res0 |= (HFGxTR_EL2_nPIRE0_EL1 | HFGxTR_EL2_nPIR_EL1);
	if (!kvm_has_feat(kvm, ID_AA64MMFR3_EL1, S1POE, IMP))
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
out:
	mutex_unlock(&kvm->arch.config_lock);

	return ret;
}
