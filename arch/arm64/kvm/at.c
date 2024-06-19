// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 - Linaro Ltd
 * Author: Jintack Lim <jintack.lim@linaro.org>
 */

#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>

struct mmu_config {
	u64	ttbr0;
	u64	ttbr1;
	u64	tcr;
	u64	mair;
	u64	sctlr;
	u64	vttbr;
	u64	vtcr;
	u64	hcr;
};

static void __mmu_config_save(struct mmu_config *config)
{
	config->ttbr0	= read_sysreg_el1(SYS_TTBR0);
	config->ttbr1	= read_sysreg_el1(SYS_TTBR1);
	config->tcr	= read_sysreg_el1(SYS_TCR);
	config->mair	= read_sysreg_el1(SYS_MAIR);
	config->sctlr	= read_sysreg_el1(SYS_SCTLR);
	config->vttbr	= read_sysreg(vttbr_el2);
	config->vtcr	= read_sysreg(vtcr_el2);
	config->hcr	= read_sysreg(hcr_el2);
}

static void __mmu_config_restore(struct mmu_config *config)
{
	write_sysreg(config->hcr,	hcr_el2);

	/*
	 * ARM errata 1165522 and 1530923 require TGE to be 1 before
	 * we update the guest state.
	 */
	asm(ALTERNATIVE("nop", "isb", ARM64_WORKAROUND_SPECULATIVE_AT));

	write_sysreg_el1(config->ttbr0,	SYS_TTBR0);
	write_sysreg_el1(config->ttbr1,	SYS_TTBR1);
	write_sysreg_el1(config->tcr,	SYS_TCR);
	write_sysreg_el1(config->mair,	SYS_MAIR);
	write_sysreg_el1(config->sctlr,	SYS_SCTLR);
	write_sysreg(config->vttbr,	vttbr_el2);
	write_sysreg(config->vtcr,	vtcr_el2);
}

static bool at_s1e1p_fast(struct kvm_vcpu *vcpu, u32 op, u64 vaddr)
{
	u64 host_pan;
	bool fail;

	host_pan = read_sysreg_s(SYS_PSTATE_PAN);
	write_sysreg_s(*vcpu_cpsr(vcpu) & PSTATE_PAN, SYS_PSTATE_PAN);

	switch (op) {
	case OP_AT_S1E1RP:
		fail = __kvm_at(OP_AT_S1E1RP, vaddr);
		break;
	case OP_AT_S1E1WP:
		fail = __kvm_at(OP_AT_S1E1WP, vaddr);
		break;
	}

	write_sysreg_s(host_pan, SYS_PSTATE_PAN);

	return fail;
}

#define MEMATTR(ic, oc)		(MEMATTR_##oc << 4 | MEMATTR_##ic)
#define MEMATTR_NC		0b0100
#define MEMATTR_Wt		0b1000
#define MEMATTR_Wb		0b1100
#define MEMATTR_WbRaWa		0b1111

#define MEMATTR_IS_DEVICE(m)	(((m) & GENMASK(7, 4)) == 0)

static u8 s2_memattr_to_attr(u8 memattr)
{
	memattr &= 0b1111;

	switch (memattr) {
	case 0b0000:
	case 0b0001:
	case 0b0010:
	case 0b0011:
		return memattr << 2;
	case 0b0100:
		return MEMATTR(Wb, Wb);
	case 0b0101:
		return MEMATTR(NC, NC);
	case 0b0110:
		return MEMATTR(Wt, NC);
	case 0b0111:
		return MEMATTR(Wb, NC);
	case 0b1000:
		/* Reserved, assume NC */
		return MEMATTR(NC, NC);
	case 0b1001:
		return MEMATTR(NC, Wt);
	case 0b1010:
		return MEMATTR(Wt, Wt);
	case 0b1011:
		return MEMATTR(Wb, Wt);
	case 0b1100:
		/* Reserved, assume NC */
		return MEMATTR(NC, NC);
	case 0b1101:
		return MEMATTR(NC, Wb);
	case 0b1110:
		return MEMATTR(Wt, Wb);
	case 0b1111:
		return MEMATTR(Wb, Wb);
	default:
		unreachable();
	}
}

static u8 combine_s1_s2_attr(u8 s1, u8 s2)
{
	bool transient;
	u8 final = 0;

	/* Upgrade transient s1 to non-transient to simplify things */
	switch (s1) {
	case 0b0001 ... 0b0011:	/* Normal, Write-Through Transient */
		transient = true;
		s1 = MEMATTR_Wt | (s1 & GENMASK(1,0));
		break;
	case 0b0101 ... 0b0111:	/* Normal, Write-Back Transient */
		transient = true;
		s1 = MEMATTR_Wb | (s1 & GENMASK(1,0));
		break;
	default:
		transient = false;
	}

	/* S2CombineS1AttrHints() */
	if ((s1 & GENMASK(3, 2)) == MEMATTR_NC ||
	    (s2 & GENMASK(3, 2)) == MEMATTR_NC)
		final = MEMATTR_NC;
	else if ((s1 & GENMASK(3, 2)) == MEMATTR_Wt ||
		 (s2 & GENMASK(3, 2)) == MEMATTR_Wt)
		final = MEMATTR_Wt;
	else
		final = MEMATTR_Wb;

	if (final != MEMATTR_NC) {
		/* Inherit RaWa hints form S1 */
		if (transient) {
			switch (s1 & GENMASK(3, 2)) {
			case MEMATTR_Wt:
				final = 0;
				break;
			case MEMATTR_Wb:
				final = MEMATTR_NC;
				break;
			}
		}

		final |= s1 & GENMASK(1, 0);
	}

	return final;
}

#define ATTR_NSH	0b00
#define ATTR_RSV	0b01
#define ATTR_OSH	0b10
#define ATTR_ISH	0b11

static u8 compute_sh(u8 attr, u64 desc)
{
	u8 sh;

	/* Any form of device, as well as NC has SH[1:0]=0b10 */
	if (MEMATTR_IS_DEVICE(attr) || attr == MEMATTR(NC, NC))
		return ATTR_OSH;

	sh = FIELD_GET(PTE_SHARED, desc);
	if (sh == ATTR_RSV)		/* Reserved, mapped to NSH */
		sh = ATTR_NSH;

	return sh;
}

static u64 compute_par_s12(struct kvm_vcpu *vcpu, u64 s1_par,
			   struct kvm_s2_trans *tr)
{
	u8 s1_parattr, s2_memattr, final_attr;
	u64 par;

	/* If S2 has failed to translate, report the damage */
	if (tr->esr) {
		par = SYS_PAR_EL1_RES1;
		par |= SYS_PAR_EL1_F;
		par |= SYS_PAR_EL1_S;
		par |= FIELD_PREP(SYS_PAR_EL1_FST, tr->esr);
		return par;
	}

	s1_parattr = FIELD_GET(SYS_PAR_EL1_ATTR, s1_par);
	s2_memattr = FIELD_GET(GENMASK(5, 2), tr->desc);

	if (__vcpu_sys_reg(vcpu, HCR_EL2) & HCR_FWB) {
		if (!kvm_has_feat(vcpu->kvm, ID_AA64PFR2_EL1, MTEPERM, IMP))
			s2_memattr &= ~BIT(3);

		/* Combination of R_VRJSW and R_RHWZM */
		switch (s2_memattr) {
		case 0b0101:
			if (MEMATTR_IS_DEVICE(s1_parattr))
				final_attr = s1_parattr;
			else
				final_attr = MEMATTR(NC, NC);
			break;
		case 0b0110:
		case 0b1110:
			final_attr = MEMATTR(WbRaWa, WbRaWa);
			break;
		case 0b0111:
		case 0b1111:
			/* Preserve S1 attribute */
			final_attr = s1_parattr;
			break;
		case 0b0100:
		case 0b1100:
		case 0b1101:
			/* Reserved, do something non-silly */
			final_attr = s1_parattr;
			break;
		default:
			/* MemAttr[2]=0, Device from S2 */
			final_attr = s2_memattr & GENMASK(1,0) << 2;
		}
	} else {
		/* Combination of R_HMNDG, R_TNHFM and R_GQFSF */
		u8 s2_parattr = s2_memattr_to_attr(s2_memattr);

		if (MEMATTR_IS_DEVICE(s1_parattr) ||
		    MEMATTR_IS_DEVICE(s2_parattr)) {
			final_attr = min(s1_parattr, s2_parattr);
		} else {
			/* At this stage, this is memory vs memory */
			final_attr  = combine_s1_s2_attr(s1_parattr & 0xf,
							 s2_parattr & 0xf);
			final_attr |= combine_s1_s2_attr(s1_parattr >> 4,
							 s2_parattr >> 4) << 4;
		}
	}

	if ((__vcpu_sys_reg(vcpu, HCR_EL2) & HCR_CD) &&
	    !MEMATTR_IS_DEVICE(final_attr))
		final_attr = MEMATTR(NC, NC);

	par  = FIELD_PREP(SYS_PAR_EL1_ATTR, final_attr);
	par |= tr->output & GENMASK(47, 12);
	par |= FIELD_PREP(SYS_PAR_EL1_SH,
			  compute_sh(final_attr, tr->desc));

	return par;
}

/*
 * Return the PAR_EL1 value as the result of a valid translation.
 *
 * If the translation is unsuccessful, the value may only contain
 * PAR_EL1.F, and cannot be taken at face value. It isn't an
 * indication of the translation having failed, only that the fast
 * path did not succeed, *unless* it indicates a S1 permission fault.
 */
static u64 __kvm_at_s1e01_fast(struct kvm_vcpu *vcpu, u32 op, u64 vaddr)
{
	struct mmu_config config;
	struct kvm_s2_mmu *mmu;
	bool fail;
	u64 par;

	par = SYS_PAR_EL1_F;

	/*
	 * We've trapped, so everything is live on the CPU. As we will
	 * be switching contexts behind everybody's back, disable
	 * interrupts while holding the mmu lock.
	 */
	guard(write_lock_irqsave)(&vcpu->kvm->mmu_lock);

	/*
	 * If HCR_EL2.{E2H,TGE} == {1,1}, the MMU context is already
	 * the right one (as we trapped from vEL2). If not, save the
	 * full MMU context.
	 */
	if (vcpu_el2_e2h_is_set(vcpu) && vcpu_el2_tge_is_set(vcpu))
		goto skip_mmu_switch;

	/*
	 * Obtaining the S2 MMU for a L2 is horribly racy, and we may not
	 * find it (recycled by another vcpu, for example). When this
	 * happens, admit defeat immediately and use the SW (slow) path.
	 */
	mmu = lookup_s2_mmu(vcpu);
	if (!mmu)
		return par;

	__mmu_config_save(&config);

	write_sysreg_el1(vcpu_read_sys_reg(vcpu, TTBR0_EL1),	SYS_TTBR0);
	write_sysreg_el1(vcpu_read_sys_reg(vcpu, TTBR1_EL1),	SYS_TTBR1);
	write_sysreg_el1(vcpu_read_sys_reg(vcpu, TCR_EL1),	SYS_TCR);
	write_sysreg_el1(vcpu_read_sys_reg(vcpu, MAIR_EL1),	SYS_MAIR);
	write_sysreg_el1(vcpu_read_sys_reg(vcpu, SCTLR_EL1),	SYS_SCTLR);
	__load_stage2(mmu, mmu->arch);

skip_mmu_switch:
	/* Clear TGE, enable S2 translation, we're rolling */
	write_sysreg((config.hcr & ~HCR_TGE) | HCR_VM,	hcr_el2);
	isb();

	switch (op) {
	case OP_AT_S1E1RP:
	case OP_AT_S1E1WP:
		fail = at_s1e1p_fast(vcpu, op, vaddr);
		break;
	case OP_AT_S1E1R:
		fail = __kvm_at(OP_AT_S1E1R, vaddr);
		break;
	case OP_AT_S1E1W:
		fail = __kvm_at(OP_AT_S1E1W, vaddr);
		break;
	case OP_AT_S1E0R:
		fail = __kvm_at(OP_AT_S1E0R, vaddr);
		break;
	case OP_AT_S1E0W:
		fail = __kvm_at(OP_AT_S1E0W, vaddr);
		break;
	default:
		WARN_ON_ONCE(1);
		fail = true;
		break;
	}

	if (!fail)
		par = read_sysreg_par();

	if (!(vcpu_el2_e2h_is_set(vcpu) && vcpu_el2_tge_is_set(vcpu)))
		__mmu_config_restore(&config);

	return par;
}

void __kvm_at_s1e01(struct kvm_vcpu *vcpu, u32 op, u64 vaddr)
{
	u64 par = __kvm_at_s1e01_fast(vcpu, op, vaddr);

	vcpu_write_sys_reg(vcpu, par, PAR_EL1);
}

void __kvm_at_s1e2(struct kvm_vcpu *vcpu, u32 op, u64 vaddr)
{
	u64 par;

	/*
	 * We've trapped, so everything is live on the CPU. As we will be
	 * switching context behind everybody's back, disable interrupts...
	 */
	scoped_guard(write_lock_irqsave, &vcpu->kvm->mmu_lock) {
		struct kvm_s2_mmu *mmu;
		u64 val, hcr;
		bool fail;

		mmu = &vcpu->kvm->arch.mmu;

		val = hcr = read_sysreg(hcr_el2);
		val &= ~HCR_TGE;
		val |= HCR_VM;

		if (!vcpu_el2_e2h_is_set(vcpu))
			val |= HCR_NV | HCR_NV1;

		write_sysreg(val, hcr_el2);
		isb();

		par = SYS_PAR_EL1_F;

		switch (op) {
		case OP_AT_S1E2R:
			fail = __kvm_at(OP_AT_S1E1R, vaddr);
			break;
		case OP_AT_S1E2W:
			fail = __kvm_at(OP_AT_S1E1W, vaddr);
			break;
		default:
			WARN_ON_ONCE(1);
			fail = true;
		}

		isb();

		if (!fail)
			par = read_sysreg_par();

		write_sysreg(hcr, hcr_el2);
		isb();
	}

	vcpu_write_sys_reg(vcpu, par, PAR_EL1);
}

void __kvm_at_s12(struct kvm_vcpu *vcpu, u32 op, u64 vaddr)
{
	struct kvm_s2_trans out = {};
	u64 ipa, par;
	bool write;
	int ret;

	/* Do the stage-1 translation */
	switch (op) {
	case OP_AT_S12E1R:
		op = OP_AT_S1E1R;
		write = false;
		break;
	case OP_AT_S12E1W:
		op = OP_AT_S1E1W;
		write = true;
		break;
	case OP_AT_S12E0R:
		op = OP_AT_S1E0R;
		write = false;
		break;
	case OP_AT_S12E0W:
		op = OP_AT_S1E0W;
		write = true;
		break;
	default:
		WARN_ON_ONCE(1);
		return;
	}

	__kvm_at_s1e01(vcpu, op, vaddr);
	par = vcpu_read_sys_reg(vcpu, PAR_EL1);
	if (par & SYS_PAR_EL1_F)
		return;

	/*
	 * If we only have a single stage of translation (E2H=0 or
	 * TGE=1), exit early. Same thing if {VM,DC}=={0,0}.
	 */
	if (!vcpu_el2_e2h_is_set(vcpu) || vcpu_el2_tge_is_set(vcpu) ||
	    !(vcpu_read_sys_reg(vcpu, HCR_EL2) & (HCR_VM | HCR_DC)))
		return;

	/* Do the stage-2 translation */
	ipa = (par & GENMASK_ULL(47, 12)) | (vaddr & GENMASK_ULL(11, 0));
	out.esr = 0;
	ret = kvm_walk_nested_s2(vcpu, ipa, &out);
	if (ret < 0)
		return;

	/* Check the access permission */
	if (!out.esr &&
	    ((!write && !out.readable) || (write && !out.writable)))
		out.esr = ESR_ELx_FSC_PERM | (out.level & 0x3);

	par = compute_par_s12(vcpu, par, &out);
	vcpu_write_sys_reg(vcpu, par, PAR_EL1);
}
