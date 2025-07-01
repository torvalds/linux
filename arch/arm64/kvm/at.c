// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 - Linaro Ltd
 * Author: Jintack Lim <jintack.lim@linaro.org>
 */

#include <linux/kvm_host.h>

#include <asm/esr.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>

static void fail_s1_walk(struct s1_walk_result *wr, u8 fst, bool s1ptw)
{
	wr->fst		= fst;
	wr->ptw		= s1ptw;
	wr->s2		= s1ptw;
	wr->failed	= true;
}

#define S1_MMU_DISABLED		(-127)

static int get_ia_size(struct s1_walk_info *wi)
{
	return 64 - wi->txsz;
}

/* Return true if the IPA is out of the OA range */
static bool check_output_size(u64 ipa, struct s1_walk_info *wi)
{
	return wi->max_oa_bits < 48 && (ipa & GENMASK_ULL(47, wi->max_oa_bits));
}

/* Return the translation regime that applies to an AT instruction */
static enum trans_regime compute_translation_regime(struct kvm_vcpu *vcpu, u32 op)
{
	/*
	 * We only get here from guest EL2, so the translation
	 * regime AT applies to is solely defined by {E2H,TGE}.
	 */
	switch (op) {
	case OP_AT_S1E2R:
	case OP_AT_S1E2W:
	case OP_AT_S1E2A:
		return vcpu_el2_e2h_is_set(vcpu) ? TR_EL20 : TR_EL2;
		break;
	default:
		return (vcpu_el2_e2h_is_set(vcpu) &&
			vcpu_el2_tge_is_set(vcpu)) ? TR_EL20 : TR_EL10;
	}
}

static bool s1pie_enabled(struct kvm_vcpu *vcpu, enum trans_regime regime)
{
	if (!kvm_has_s1pie(vcpu->kvm))
		return false;

	switch (regime) {
	case TR_EL2:
	case TR_EL20:
		return vcpu_read_sys_reg(vcpu, TCR2_EL2) & TCR2_EL2_PIE;
	case TR_EL10:
		return  (__vcpu_sys_reg(vcpu, HCRX_EL2) & HCRX_EL2_TCR2En) &&
			(__vcpu_sys_reg(vcpu, TCR2_EL1) & TCR2_EL1_PIE);
	default:
		BUG();
	}
}

static void compute_s1poe(struct kvm_vcpu *vcpu, struct s1_walk_info *wi)
{
	u64 val;

	if (!kvm_has_s1poe(vcpu->kvm)) {
		wi->poe = wi->e0poe = false;
		return;
	}

	switch (wi->regime) {
	case TR_EL2:
	case TR_EL20:
		val = vcpu_read_sys_reg(vcpu, TCR2_EL2);
		wi->poe = val & TCR2_EL2_POE;
		wi->e0poe = (wi->regime == TR_EL20) && (val & TCR2_EL2_E0POE);
		break;
	case TR_EL10:
		if (__vcpu_sys_reg(vcpu, HCRX_EL2) & HCRX_EL2_TCR2En) {
			wi->poe = wi->e0poe = false;
			return;
		}

		val = __vcpu_sys_reg(vcpu, TCR2_EL1);
		wi->poe = val & TCR2_EL1_POE;
		wi->e0poe = val & TCR2_EL1_E0POE;
	}
}

static int setup_s1_walk(struct kvm_vcpu *vcpu, struct s1_walk_info *wi,
			 struct s1_walk_result *wr, u64 va)
{
	u64 hcr, sctlr, tcr, tg, ps, ia_bits, ttbr;
	unsigned int stride, x;
	bool va55, tbi, lva;

	hcr = __vcpu_sys_reg(vcpu, HCR_EL2);

	va55 = va & BIT(55);

	if (wi->regime == TR_EL2 && va55)
		goto addrsz;

	wi->s2 = wi->regime == TR_EL10 && (hcr & (HCR_VM | HCR_DC));

	switch (wi->regime) {
	case TR_EL10:
		sctlr	= vcpu_read_sys_reg(vcpu, SCTLR_EL1);
		tcr	= vcpu_read_sys_reg(vcpu, TCR_EL1);
		ttbr	= (va55 ?
			   vcpu_read_sys_reg(vcpu, TTBR1_EL1) :
			   vcpu_read_sys_reg(vcpu, TTBR0_EL1));
		break;
	case TR_EL2:
	case TR_EL20:
		sctlr	= vcpu_read_sys_reg(vcpu, SCTLR_EL2);
		tcr	= vcpu_read_sys_reg(vcpu, TCR_EL2);
		ttbr	= (va55 ?
			   vcpu_read_sys_reg(vcpu, TTBR1_EL2) :
			   vcpu_read_sys_reg(vcpu, TTBR0_EL2));
		break;
	default:
		BUG();
	}

	tbi = (wi->regime == TR_EL2 ?
	       FIELD_GET(TCR_EL2_TBI, tcr) :
	       (va55 ?
		FIELD_GET(TCR_TBI1, tcr) :
		FIELD_GET(TCR_TBI0, tcr)));

	if (!tbi && (u64)sign_extend64(va, 55) != va)
		goto addrsz;

	va = (u64)sign_extend64(va, 55);

	/* Let's put the MMU disabled case aside immediately */
	switch (wi->regime) {
	case TR_EL10:
		/*
		 * If dealing with the EL1&0 translation regime, 3 things
		 * can disable the S1 translation:
		 *
		 * - HCR_EL2.DC = 1
		 * - HCR_EL2.{E2H,TGE} = {0,1}
		 * - SCTLR_EL1.M = 0
		 *
		 * The TGE part is interesting. If we have decided that this
		 * is EL1&0, then it means that either {E2H,TGE} == {1,0} or
		 * {0,x}, and we only need to test for TGE == 1.
		 */
		if (hcr & (HCR_DC | HCR_TGE)) {
			wr->level = S1_MMU_DISABLED;
			break;
		}
		fallthrough;
	case TR_EL2:
	case TR_EL20:
		if (!(sctlr & SCTLR_ELx_M))
			wr->level = S1_MMU_DISABLED;
		break;
	}

	if (wr->level == S1_MMU_DISABLED) {
		if (va >= BIT(kvm_get_pa_bits(vcpu->kvm)))
			goto addrsz;

		wr->pa = va;
		return 0;
	}

	wi->be = sctlr & SCTLR_ELx_EE;

	wi->hpd  = kvm_has_feat(vcpu->kvm, ID_AA64MMFR1_EL1, HPDS, IMP);
	wi->hpd &= (wi->regime == TR_EL2 ?
		    FIELD_GET(TCR_EL2_HPD, tcr) :
		    (va55 ?
		     FIELD_GET(TCR_HPD1, tcr) :
		     FIELD_GET(TCR_HPD0, tcr)));
	/* R_JHSVW */
	wi->hpd |= s1pie_enabled(vcpu, wi->regime);

	/* Do we have POE? */
	compute_s1poe(vcpu, wi);

	/* R_BVXDG */
	wi->hpd |= (wi->poe || wi->e0poe);

	/* Someone was silly enough to encode TG0/TG1 differently */
	if (va55) {
		wi->txsz = FIELD_GET(TCR_T1SZ_MASK, tcr);
		tg = FIELD_GET(TCR_TG1_MASK, tcr);

		switch (tg << TCR_TG1_SHIFT) {
		case TCR_TG1_4K:
			wi->pgshift = 12;	 break;
		case TCR_TG1_16K:
			wi->pgshift = 14;	 break;
		case TCR_TG1_64K:
		default:	    /* IMPDEF: treat any other value as 64k */
			wi->pgshift = 16;	 break;
		}
	} else {
		wi->txsz = FIELD_GET(TCR_T0SZ_MASK, tcr);
		tg = FIELD_GET(TCR_TG0_MASK, tcr);

		switch (tg << TCR_TG0_SHIFT) {
		case TCR_TG0_4K:
			wi->pgshift = 12;	 break;
		case TCR_TG0_16K:
			wi->pgshift = 14;	 break;
		case TCR_TG0_64K:
		default:	    /* IMPDEF: treat any other value as 64k */
			wi->pgshift = 16;	 break;
		}
	}

	/* R_PLCGL, R_YXNYW */
	if (!kvm_has_feat_enum(vcpu->kvm, ID_AA64MMFR2_EL1, ST, 48_47)) {
		if (wi->txsz > 39)
			goto transfault_l0;
	} else {
		if (wi->txsz > 48 || (BIT(wi->pgshift) == SZ_64K && wi->txsz > 47))
			goto transfault_l0;
	}

	/* R_GTJBY, R_SXWGM */
	switch (BIT(wi->pgshift)) {
	case SZ_4K:
		lva = kvm_has_feat(vcpu->kvm, ID_AA64MMFR0_EL1, TGRAN4, 52_BIT);
		lva &= tcr & (wi->regime == TR_EL2 ? TCR_EL2_DS : TCR_DS);
		break;
	case SZ_16K:
		lva = kvm_has_feat(vcpu->kvm, ID_AA64MMFR0_EL1, TGRAN16, 52_BIT);
		lva &= tcr & (wi->regime == TR_EL2 ? TCR_EL2_DS : TCR_DS);
		break;
	case SZ_64K:
		lva = kvm_has_feat(vcpu->kvm, ID_AA64MMFR2_EL1, VARange, 52);
		break;
	}

	if ((lva && wi->txsz < 12) || (!lva && wi->txsz < 16))
		goto transfault_l0;

	ia_bits = get_ia_size(wi);

	/* R_YYVYV, I_THCZK */
	if ((!va55 && va > GENMASK(ia_bits - 1, 0)) ||
	    (va55 && va < GENMASK(63, ia_bits)))
		goto transfault_l0;

	/* I_ZFSYQ */
	if (wi->regime != TR_EL2 &&
	    (tcr & (va55 ? TCR_EPD1_MASK : TCR_EPD0_MASK)))
		goto transfault_l0;

	/* R_BNDVG and following statements */
	if (kvm_has_feat(vcpu->kvm, ID_AA64MMFR2_EL1, E0PD, IMP) &&
	    wi->as_el0 && (tcr & (va55 ? TCR_E0PD1 : TCR_E0PD0)))
		goto transfault_l0;

	/* AArch64.S1StartLevel() */
	stride = wi->pgshift - 3;
	wi->sl = 3 - (((ia_bits - 1) - wi->pgshift) / stride);

	ps = (wi->regime == TR_EL2 ?
	      FIELD_GET(TCR_EL2_PS_MASK, tcr) : FIELD_GET(TCR_IPS_MASK, tcr));

	wi->max_oa_bits = min(get_kvm_ipa_limit(), ps_to_output_size(ps));

	/* Compute minimal alignment */
	x = 3 + ia_bits - ((3 - wi->sl) * stride + wi->pgshift);

	wi->baddr = ttbr & TTBRx_EL1_BADDR;

	/* R_VPBBF */
	if (check_output_size(wi->baddr, wi))
		goto addrsz;

	wi->baddr &= GENMASK_ULL(wi->max_oa_bits - 1, x);

	return 0;

addrsz:				/* Address Size Fault level 0 */
	fail_s1_walk(wr, ESR_ELx_FSC_ADDRSZ_L(0), false);
	return -EFAULT;

transfault_l0:			/* Translation Fault level 0 */
	fail_s1_walk(wr, ESR_ELx_FSC_FAULT_L(0), false);
	return -EFAULT;
}

static int walk_s1(struct kvm_vcpu *vcpu, struct s1_walk_info *wi,
		   struct s1_walk_result *wr, u64 va)
{
	u64 va_top, va_bottom, baddr, desc;
	int level, stride, ret;

	level = wi->sl;
	stride = wi->pgshift - 3;
	baddr = wi->baddr;

	va_top = get_ia_size(wi) - 1;

	while (1) {
		u64 index, ipa;

		va_bottom = (3 - level) * stride + wi->pgshift;
		index = (va & GENMASK_ULL(va_top, va_bottom)) >> (va_bottom - 3);

		ipa = baddr | index;

		if (wi->s2) {
			struct kvm_s2_trans s2_trans = {};

			ret = kvm_walk_nested_s2(vcpu, ipa, &s2_trans);
			if (ret) {
				fail_s1_walk(wr,
					     (s2_trans.esr & ~ESR_ELx_FSC_LEVEL) | level,
					     true);
				return ret;
			}

			if (!kvm_s2_trans_readable(&s2_trans)) {
				fail_s1_walk(wr, ESR_ELx_FSC_PERM_L(level),
					     true);

				return -EPERM;
			}

			ipa = kvm_s2_trans_output(&s2_trans);
		}

		ret = kvm_read_guest(vcpu->kvm, ipa, &desc, sizeof(desc));
		if (ret) {
			fail_s1_walk(wr, ESR_ELx_FSC_SEA_TTW(level), false);
			return ret;
		}

		if (wi->be)
			desc = be64_to_cpu((__force __be64)desc);
		else
			desc = le64_to_cpu((__force __le64)desc);

		/* Invalid descriptor */
		if (!(desc & BIT(0)))
			goto transfault;

		/* Block mapping, check validity down the line */
		if (!(desc & BIT(1)))
			break;

		/* Page mapping */
		if (level == 3)
			break;

		/* Table handling */
		if (!wi->hpd) {
			wr->APTable  |= FIELD_GET(S1_TABLE_AP, desc);
			wr->UXNTable |= FIELD_GET(PMD_TABLE_UXN, desc);
			wr->PXNTable |= FIELD_GET(PMD_TABLE_PXN, desc);
		}

		baddr = desc & GENMASK_ULL(47, wi->pgshift);

		/* Check for out-of-range OA */
		if (check_output_size(baddr, wi))
			goto addrsz;

		/* Prepare for next round */
		va_top = va_bottom - 1;
		level++;
	}

	/* Block mapping, check the validity of the level */
	if (!(desc & BIT(1))) {
		bool valid_block = false;

		switch (BIT(wi->pgshift)) {
		case SZ_4K:
			valid_block = level == 1 || level == 2;
			break;
		case SZ_16K:
		case SZ_64K:
			valid_block = level == 2;
			break;
		}

		if (!valid_block)
			goto transfault;
	}

	if (check_output_size(desc & GENMASK(47, va_bottom), wi))
		goto addrsz;

	if (!(desc & PTE_AF)) {
		fail_s1_walk(wr, ESR_ELx_FSC_ACCESS_L(level), false);
		return -EACCES;
	}

	va_bottom += contiguous_bit_shift(desc, wi, level);

	wr->failed = false;
	wr->level = level;
	wr->desc = desc;
	wr->pa = desc & GENMASK(47, va_bottom);
	wr->pa |= va & GENMASK_ULL(va_bottom - 1, 0);

	wr->nG = (wi->regime != TR_EL2) && (desc & PTE_NG);
	if (wr->nG) {
		u64 asid_ttbr, tcr;

		switch (wi->regime) {
		case TR_EL10:
			tcr = vcpu_read_sys_reg(vcpu, TCR_EL1);
			asid_ttbr = ((tcr & TCR_A1) ?
				     vcpu_read_sys_reg(vcpu, TTBR1_EL1) :
				     vcpu_read_sys_reg(vcpu, TTBR0_EL1));
			break;
		case TR_EL20:
			tcr = vcpu_read_sys_reg(vcpu, TCR_EL2);
			asid_ttbr = ((tcr & TCR_A1) ?
				     vcpu_read_sys_reg(vcpu, TTBR1_EL2) :
				     vcpu_read_sys_reg(vcpu, TTBR0_EL2));
			break;
		default:
			BUG();
		}

		wr->asid = FIELD_GET(TTBR_ASID_MASK, asid_ttbr);
		if (!kvm_has_feat_enum(vcpu->kvm, ID_AA64MMFR0_EL1, ASIDBITS, 16) ||
		    !(tcr & TCR_ASID16))
			wr->asid &= GENMASK(7, 0);
	}

	return 0;

addrsz:
	fail_s1_walk(wr, ESR_ELx_FSC_ADDRSZ_L(level), false);
	return -EINVAL;
transfault:
	fail_s1_walk(wr, ESR_ELx_FSC_FAULT_L(level), false);
	return -ENOENT;
}

struct mmu_config {
	u64	ttbr0;
	u64	ttbr1;
	u64	tcr;
	u64	mair;
	u64	tcr2;
	u64	pir;
	u64	pire0;
	u64	por_el0;
	u64	por_el1;
	u64	sctlr;
	u64	vttbr;
	u64	vtcr;
};

static void __mmu_config_save(struct mmu_config *config)
{
	config->ttbr0	= read_sysreg_el1(SYS_TTBR0);
	config->ttbr1	= read_sysreg_el1(SYS_TTBR1);
	config->tcr	= read_sysreg_el1(SYS_TCR);
	config->mair	= read_sysreg_el1(SYS_MAIR);
	if (cpus_have_final_cap(ARM64_HAS_TCR2)) {
		config->tcr2	= read_sysreg_el1(SYS_TCR2);
		if (cpus_have_final_cap(ARM64_HAS_S1PIE)) {
			config->pir	= read_sysreg_el1(SYS_PIR);
			config->pire0	= read_sysreg_el1(SYS_PIRE0);
		}
		if (system_supports_poe()) {
			config->por_el1	= read_sysreg_el1(SYS_POR);
			config->por_el0	= read_sysreg_s(SYS_POR_EL0);
		}
	}
	config->sctlr	= read_sysreg_el1(SYS_SCTLR);
	config->vttbr	= read_sysreg(vttbr_el2);
	config->vtcr	= read_sysreg(vtcr_el2);
}

static void __mmu_config_restore(struct mmu_config *config)
{
	/*
	 * ARM errata 1165522 and 1530923 require TGE to be 1 before
	 * we update the guest state.
	 */
	asm(ALTERNATIVE("nop", "isb", ARM64_WORKAROUND_SPECULATIVE_AT));

	write_sysreg_el1(config->ttbr0,	SYS_TTBR0);
	write_sysreg_el1(config->ttbr1,	SYS_TTBR1);
	write_sysreg_el1(config->tcr,	SYS_TCR);
	write_sysreg_el1(config->mair,	SYS_MAIR);
	if (cpus_have_final_cap(ARM64_HAS_TCR2)) {
		write_sysreg_el1(config->tcr2, SYS_TCR2);
		if (cpus_have_final_cap(ARM64_HAS_S1PIE)) {
			write_sysreg_el1(config->pir, SYS_PIR);
			write_sysreg_el1(config->pire0, SYS_PIRE0);
		}
		if (system_supports_poe()) {
			write_sysreg_el1(config->por_el1, SYS_POR);
			write_sysreg_s(config->por_el0, SYS_POR_EL0);
		}
	}
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

static u8 combine_sh(u8 s1_sh, u8 s2_sh)
{
	if (s1_sh == ATTR_OSH || s2_sh == ATTR_OSH)
		return ATTR_OSH;
	if (s1_sh == ATTR_ISH || s2_sh == ATTR_ISH)
		return ATTR_ISH;

	return ATTR_NSH;
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
			/*
			 * MemAttr[2]=0, Device from S2.
			 *
			 * FWB does not influence the way that stage 1
			 * memory types and attributes are combined
			 * with stage 2 Device type and attributes.
			 */
			final_attr = min(s2_memattr_to_attr(s2_memattr),
					 s1_parattr);
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
			  combine_sh(FIELD_GET(SYS_PAR_EL1_SH, s1_par),
				     compute_sh(final_attr, tr->desc)));

	return par;
}

static u64 compute_par_s1(struct kvm_vcpu *vcpu, struct s1_walk_result *wr,
			  enum trans_regime regime)
{
	u64 par;

	if (wr->failed) {
		par = SYS_PAR_EL1_RES1;
		par |= SYS_PAR_EL1_F;
		par |= FIELD_PREP(SYS_PAR_EL1_FST, wr->fst);
		par |= wr->ptw ? SYS_PAR_EL1_PTW : 0;
		par |= wr->s2 ? SYS_PAR_EL1_S : 0;
	} else if (wr->level == S1_MMU_DISABLED) {
		/* MMU off or HCR_EL2.DC == 1 */
		par  = SYS_PAR_EL1_NSE;
		par |= wr->pa & GENMASK_ULL(47, 12);

		if (regime == TR_EL10 &&
		    (__vcpu_sys_reg(vcpu, HCR_EL2) & HCR_DC)) {
			par |= FIELD_PREP(SYS_PAR_EL1_ATTR,
					  MEMATTR(WbRaWa, WbRaWa));
			par |= FIELD_PREP(SYS_PAR_EL1_SH, ATTR_NSH);
		} else {
			par |= FIELD_PREP(SYS_PAR_EL1_ATTR, 0); /* nGnRnE */
			par |= FIELD_PREP(SYS_PAR_EL1_SH, ATTR_OSH);
		}
	} else {
		u64 mair, sctlr;
		u8 sh;

		par  = SYS_PAR_EL1_NSE;

		mair = (regime == TR_EL10 ?
			vcpu_read_sys_reg(vcpu, MAIR_EL1) :
			vcpu_read_sys_reg(vcpu, MAIR_EL2));

		mair >>= FIELD_GET(PTE_ATTRINDX_MASK, wr->desc) * 8;
		mair &= 0xff;

		sctlr = (regime == TR_EL10 ?
			 vcpu_read_sys_reg(vcpu, SCTLR_EL1) :
			 vcpu_read_sys_reg(vcpu, SCTLR_EL2));

		/* Force NC for memory if SCTLR_ELx.C is clear */
		if (!(sctlr & SCTLR_EL1_C) && !MEMATTR_IS_DEVICE(mair))
			mair = MEMATTR(NC, NC);

		par |= FIELD_PREP(SYS_PAR_EL1_ATTR, mair);
		par |= wr->pa & GENMASK_ULL(47, 12);

		sh = compute_sh(mair, wr->desc);
		par |= FIELD_PREP(SYS_PAR_EL1_SH, sh);
	}

	return par;
}

static bool pan3_enabled(struct kvm_vcpu *vcpu, enum trans_regime regime)
{
	u64 sctlr;

	if (!kvm_has_feat(vcpu->kvm, ID_AA64MMFR1_EL1, PAN, PAN3))
		return false;

	if (s1pie_enabled(vcpu, regime))
		return true;

	if (regime == TR_EL10)
		sctlr = vcpu_read_sys_reg(vcpu, SCTLR_EL1);
	else
		sctlr = vcpu_read_sys_reg(vcpu, SCTLR_EL2);

	return sctlr & SCTLR_EL1_EPAN;
}

static void compute_s1_direct_permissions(struct kvm_vcpu *vcpu,
					  struct s1_walk_info *wi,
					  struct s1_walk_result *wr)
{
	bool wxn;

	/* Non-hierarchical part of AArch64.S1DirectBasePermissions() */
	if (wi->regime != TR_EL2) {
		switch (FIELD_GET(PTE_USER | PTE_RDONLY, wr->desc)) {
		case 0b00:
			wr->pr = wr->pw = true;
			wr->ur = wr->uw = false;
			break;
		case 0b01:
			wr->pr = wr->pw = wr->ur = wr->uw = true;
			break;
		case 0b10:
			wr->pr = true;
			wr->pw = wr->ur = wr->uw = false;
			break;
		case 0b11:
			wr->pr = wr->ur = true;
			wr->pw = wr->uw = false;
			break;
		}

		/* We don't use px for anything yet, but hey... */
		wr->px = !((wr->desc & PTE_PXN) || wr->uw);
		wr->ux = !(wr->desc & PTE_UXN);
	} else {
		wr->ur = wr->uw = wr->ux = false;

		if (!(wr->desc & PTE_RDONLY)) {
			wr->pr = wr->pw = true;
		} else {
			wr->pr = true;
			wr->pw = false;
		}

		/* XN maps to UXN */
		wr->px = !(wr->desc & PTE_UXN);
	}

	switch (wi->regime) {
	case TR_EL2:
	case TR_EL20:
		wxn = (vcpu_read_sys_reg(vcpu, SCTLR_EL2) & SCTLR_ELx_WXN);
		break;
	case TR_EL10:
		wxn = (__vcpu_sys_reg(vcpu, SCTLR_EL1) & SCTLR_ELx_WXN);
		break;
	}

	wr->pwxn = wr->uwxn = wxn;
	wr->pov = wi->poe;
	wr->uov = wi->e0poe;
}

static void compute_s1_hierarchical_permissions(struct kvm_vcpu *vcpu,
						struct s1_walk_info *wi,
						struct s1_walk_result *wr)
{
	/* Hierarchical part of AArch64.S1DirectBasePermissions() */
	if (wi->regime != TR_EL2) {
		switch (wr->APTable) {
		case 0b00:
			break;
		case 0b01:
			wr->ur = wr->uw = false;
			break;
		case 0b10:
			wr->pw = wr->uw = false;
			break;
		case 0b11:
			wr->pw = wr->ur = wr->uw = false;
			break;
		}

		wr->px &= !wr->PXNTable;
		wr->ux &= !wr->UXNTable;
	} else {
		if (wr->APTable & BIT(1))
			wr->pw = false;

		/* XN maps to UXN */
		wr->px &= !wr->UXNTable;
	}
}

#define perm_idx(v, r, i)	((vcpu_read_sys_reg((v), (r)) >> ((i) * 4)) & 0xf)

#define set_priv_perms(wr, r, w, x)	\
	do {				\
		(wr)->pr = (r);		\
		(wr)->pw = (w);		\
		(wr)->px = (x);		\
	} while (0)

#define set_unpriv_perms(wr, r, w, x)	\
	do {				\
		(wr)->ur = (r);		\
		(wr)->uw = (w);		\
		(wr)->ux = (x);		\
	} while (0)

#define set_priv_wxn(wr, v)		\
	do {				\
		(wr)->pwxn = (v);	\
	} while (0)

#define set_unpriv_wxn(wr, v)		\
	do {				\
		(wr)->uwxn = (v);	\
	} while (0)

/* Similar to AArch64.S1IndirectBasePermissions(), without GCS  */
#define set_perms(w, wr, ip)						\
	do {								\
		/* R_LLZDZ */						\
		switch ((ip)) {						\
		case 0b0000:						\
			set_ ## w ## _perms((wr), false, false, false);	\
			break;						\
		case 0b0001:						\
			set_ ## w ## _perms((wr), true , false, false);	\
			break;						\
		case 0b0010:						\
			set_ ## w ## _perms((wr), false, false, true );	\
			break;						\
		case 0b0011:						\
			set_ ## w ## _perms((wr), true , false, true );	\
			break;						\
		case 0b0100:						\
			set_ ## w ## _perms((wr), false, false, false);	\
			break;						\
		case 0b0101:						\
			set_ ## w ## _perms((wr), true , true , false);	\
			break;						\
		case 0b0110:						\
			set_ ## w ## _perms((wr), true , true , true );	\
			break;						\
		case 0b0111:						\
			set_ ## w ## _perms((wr), true , true , true );	\
			break;						\
		case 0b1000:						\
			set_ ## w ## _perms((wr), true , false, false);	\
			break;						\
		case 0b1001:						\
			set_ ## w ## _perms((wr), true , false, false);	\
			break;						\
		case 0b1010:						\
			set_ ## w ## _perms((wr), true , false, true );	\
			break;						\
		case 0b1011:						\
			set_ ## w ## _perms((wr), false, false, false);	\
			break;						\
		case 0b1100:						\
			set_ ## w ## _perms((wr), true , true , false);	\
			break;						\
		case 0b1101:						\
			set_ ## w ## _perms((wr), false, false, false);	\
			break;						\
		case 0b1110:						\
			set_ ## w ## _perms((wr), true , true , true );	\
			break;						\
		case 0b1111:						\
			set_ ## w ## _perms((wr), false, false, false);	\
			break;						\
		}							\
									\
		/* R_HJYGR */						\
		set_ ## w ## _wxn((wr), ((ip) == 0b0110));		\
									\
	} while (0)

static void compute_s1_indirect_permissions(struct kvm_vcpu *vcpu,
					    struct s1_walk_info *wi,
					    struct s1_walk_result *wr)
{
	u8 up, pp, idx;

	idx = pte_pi_index(wr->desc);

	switch (wi->regime) {
	case TR_EL10:
		pp = perm_idx(vcpu, PIR_EL1, idx);
		up = perm_idx(vcpu, PIRE0_EL1, idx);
		break;
	case TR_EL20:
		pp = perm_idx(vcpu, PIR_EL2, idx);
		up = perm_idx(vcpu, PIRE0_EL2, idx);
		break;
	case TR_EL2:
		pp = perm_idx(vcpu, PIR_EL2, idx);
		up = 0;
		break;
	}

	set_perms(priv, wr, pp);

	if (wi->regime != TR_EL2)
		set_perms(unpriv, wr, up);
	else
		set_unpriv_perms(wr, false, false, false);

	wr->pov = wi->poe && !(pp & BIT(3));
	wr->uov = wi->e0poe && !(up & BIT(3));

	/* R_VFPJF */
	if (wr->px && wr->uw) {
		set_priv_perms(wr, false, false, false);
		set_unpriv_perms(wr, false, false, false);
	}
}

static void compute_s1_overlay_permissions(struct kvm_vcpu *vcpu,
					   struct s1_walk_info *wi,
					   struct s1_walk_result *wr)
{
	u8 idx, pov_perms, uov_perms;

	idx = FIELD_GET(PTE_PO_IDX_MASK, wr->desc);

	if (wr->pov) {
		switch (wi->regime) {
		case TR_EL10:
			pov_perms = perm_idx(vcpu, POR_EL1, idx);
			break;
		case TR_EL20:
			pov_perms = perm_idx(vcpu, POR_EL2, idx);
			break;
		case TR_EL2:
			pov_perms = perm_idx(vcpu, POR_EL2, idx);
			break;
		}

		if (pov_perms & ~POE_RWX)
			pov_perms = POE_NONE;

		wr->pr &= pov_perms & POE_R;
		wr->pw &= pov_perms & POE_W;
		wr->px &= pov_perms & POE_X;
	}

	if (wr->uov) {
		switch (wi->regime) {
		case TR_EL10:
			uov_perms = perm_idx(vcpu, POR_EL0, idx);
			break;
		case TR_EL20:
			uov_perms = perm_idx(vcpu, POR_EL0, idx);
			break;
		case TR_EL2:
			uov_perms = 0;
			break;
		}

		if (uov_perms & ~POE_RWX)
			uov_perms = POE_NONE;

		wr->ur &= uov_perms & POE_R;
		wr->uw &= uov_perms & POE_W;
		wr->ux &= uov_perms & POE_X;
	}
}

static void compute_s1_permissions(struct kvm_vcpu *vcpu,
				   struct s1_walk_info *wi,
				   struct s1_walk_result *wr)
{
	bool pan;

	if (!s1pie_enabled(vcpu, wi->regime))
		compute_s1_direct_permissions(vcpu, wi, wr);
	else
		compute_s1_indirect_permissions(vcpu, wi, wr);

	if (!wi->hpd)
		compute_s1_hierarchical_permissions(vcpu, wi, wr);

	compute_s1_overlay_permissions(vcpu, wi, wr);

	/* R_QXXPC */
	if (wr->pwxn) {
		if (!wr->pov && wr->pw)
			wr->px = false;
		if (wr->pov && wr->px)
			wr->pw = false;
	}

	/* R_NPBXC */
	if (wr->uwxn) {
		if (!wr->uov && wr->uw)
			wr->ux = false;
		if (wr->uov && wr->ux)
			wr->uw = false;
	}

	pan = wi->pan && (wr->ur || wr->uw ||
			  (pan3_enabled(vcpu, wi->regime) && wr->ux));
	wr->pw &= !pan;
	wr->pr &= !pan;
}

static u64 handle_at_slow(struct kvm_vcpu *vcpu, u32 op, u64 vaddr)
{
	struct s1_walk_result wr = {};
	struct s1_walk_info wi = {};
	bool perm_fail = false;
	int ret, idx;

	wi.regime = compute_translation_regime(vcpu, op);
	wi.as_el0 = (op == OP_AT_S1E0R || op == OP_AT_S1E0W);
	wi.pan = (op == OP_AT_S1E1RP || op == OP_AT_S1E1WP) &&
		 (*vcpu_cpsr(vcpu) & PSR_PAN_BIT);

	ret = setup_s1_walk(vcpu, &wi, &wr, vaddr);
	if (ret)
		goto compute_par;

	if (wr.level == S1_MMU_DISABLED)
		goto compute_par;

	idx = srcu_read_lock(&vcpu->kvm->srcu);

	ret = walk_s1(vcpu, &wi, &wr, vaddr);

	srcu_read_unlock(&vcpu->kvm->srcu, idx);

	if (ret)
		goto compute_par;

	compute_s1_permissions(vcpu, &wi, &wr);

	switch (op) {
	case OP_AT_S1E1RP:
	case OP_AT_S1E1R:
	case OP_AT_S1E2R:
		perm_fail = !wr.pr;
		break;
	case OP_AT_S1E1WP:
	case OP_AT_S1E1W:
	case OP_AT_S1E2W:
		perm_fail = !wr.pw;
		break;
	case OP_AT_S1E0R:
		perm_fail = !wr.ur;
		break;
	case OP_AT_S1E0W:
		perm_fail = !wr.uw;
		break;
	case OP_AT_S1E1A:
	case OP_AT_S1E2A:
		break;
	default:
		BUG();
	}

	if (perm_fail)
		fail_s1_walk(&wr, ESR_ELx_FSC_PERM_L(wr.level), false);

compute_par:
	return compute_par_s1(vcpu, &wr, wi.regime);
}

/*
 * Return the PAR_EL1 value as the result of a valid translation.
 *
 * If the translation is unsuccessful, the value may only contain
 * PAR_EL1.F, and cannot be taken at face value. It isn't an
 * indication of the translation having failed, only that the fast
 * path did not succeed, *unless* it indicates a S1 permission or
 * access fault.
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
	if (kvm_has_tcr2(vcpu->kvm)) {
		write_sysreg_el1(vcpu_read_sys_reg(vcpu, TCR2_EL1), SYS_TCR2);
		if (kvm_has_s1pie(vcpu->kvm)) {
			write_sysreg_el1(vcpu_read_sys_reg(vcpu, PIR_EL1), SYS_PIR);
			write_sysreg_el1(vcpu_read_sys_reg(vcpu, PIRE0_EL1), SYS_PIRE0);
		}
		if (kvm_has_s1poe(vcpu->kvm)) {
			write_sysreg_el1(vcpu_read_sys_reg(vcpu, POR_EL1), SYS_POR);
			write_sysreg_s(vcpu_read_sys_reg(vcpu, POR_EL0), SYS_POR_EL0);
		}
	}
	write_sysreg_el1(vcpu_read_sys_reg(vcpu, SCTLR_EL1),	SYS_SCTLR);
	__load_stage2(mmu, mmu->arch);

skip_mmu_switch:
	/* Temporarily switch back to guest context */
	write_sysreg_hcr(vcpu->arch.hcr_el2);
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
	case OP_AT_S1E1A:
		fail = __kvm_at(OP_AT_S1E1A, vaddr);
		break;
	default:
		WARN_ON_ONCE(1);
		fail = true;
		break;
	}

	if (!fail)
		par = read_sysreg_par();

	write_sysreg_hcr(HCR_HOST_VHE_FLAGS);

	if (!(vcpu_el2_e2h_is_set(vcpu) && vcpu_el2_tge_is_set(vcpu)))
		__mmu_config_restore(&config);

	return par;
}

static bool par_check_s1_perm_fault(u64 par)
{
	u8 fst = FIELD_GET(SYS_PAR_EL1_FST, par);

	return  ((fst & ESR_ELx_FSC_TYPE) == ESR_ELx_FSC_PERM &&
		 !(par & SYS_PAR_EL1_S));
}

static bool par_check_s1_access_fault(u64 par)
{
	u8 fst = FIELD_GET(SYS_PAR_EL1_FST, par);

	return  ((fst & ESR_ELx_FSC_TYPE) == ESR_ELx_FSC_ACCESS &&
		 !(par & SYS_PAR_EL1_S));
}

void __kvm_at_s1e01(struct kvm_vcpu *vcpu, u32 op, u64 vaddr)
{
	u64 par = __kvm_at_s1e01_fast(vcpu, op, vaddr);

	/*
	 * If PAR_EL1 reports that AT failed on a S1 permission or access
	 * fault, we know for sure that the PTW was able to walk the S1
	 * tables and there's nothing else to do.
	 *
	 * If AT failed for any other reason, then we must walk the guest S1
	 * to emulate the instruction.
	 */
	if ((par & SYS_PAR_EL1_F) &&
	    !par_check_s1_perm_fault(par) &&
	    !par_check_s1_access_fault(par))
		par = handle_at_slow(vcpu, op, vaddr);

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
		u64 val, hcr;
		bool fail;

		val = hcr = read_sysreg(hcr_el2);
		val &= ~HCR_TGE;
		val |= HCR_VM;

		if (!vcpu_el2_e2h_is_set(vcpu))
			val |= HCR_NV | HCR_NV1;

		write_sysreg_hcr(val);
		isb();

		par = SYS_PAR_EL1_F;

		switch (op) {
		case OP_AT_S1E2R:
			fail = __kvm_at(OP_AT_S1E1R, vaddr);
			break;
		case OP_AT_S1E2W:
			fail = __kvm_at(OP_AT_S1E1W, vaddr);
			break;
		case OP_AT_S1E2A:
			fail = __kvm_at(OP_AT_S1E1A, vaddr);
			break;
		default:
			WARN_ON_ONCE(1);
			fail = true;
		}

		isb();

		if (!fail)
			par = read_sysreg_par();

		write_sysreg_hcr(hcr);
		isb();
	}

	/* We failed the translation, let's replay it in slow motion */
	if ((par & SYS_PAR_EL1_F) && !par_check_s1_perm_fault(par))
		par = handle_at_slow(vcpu, op, vaddr);

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
		out.esr = ESR_ELx_FSC_PERM_L(out.level & 0x3);

	par = compute_par_s12(vcpu, par, &out);
	vcpu_write_sys_reg(vcpu, par, PAR_EL1);
}

/*
 * Translate a VA for a given EL in a given translation regime, with
 * or without PAN. This requires wi->{regime, as_el0, pan} to be
 * set. The rest of the wi and wr should be 0-initialised.
 */
int __kvm_translate_va(struct kvm_vcpu *vcpu, struct s1_walk_info *wi,
		       struct s1_walk_result *wr, u64 va)
{
	int ret;

	ret = setup_s1_walk(vcpu, wi, wr, va);
	if (ret)
		return ret;

	if (wr->level == S1_MMU_DISABLED) {
		wr->ur = wr->uw = wr->ux = true;
		wr->pr = wr->pw = wr->px = true;
	} else {
		ret = walk_s1(vcpu, wi, wr, va);
		if (ret)
			return ret;

		compute_s1_permissions(vcpu, wi, wr);
	}

	return 0;
}
