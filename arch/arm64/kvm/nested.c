// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 - Columbia University and Linaro Ltd.
 * Author: Jintack Lim <jintack.lim@linaro.org>
 */

#include <linux/kvm.h>
#include <linux/kvm_host.h>

#include <asm/kvm_emulate.h>
#include <asm/kvm_nested.h>
#include <asm/sysreg.h>

#include "sys_regs.h"

/* Protection against the sysreg repainting madness... */
#define NV_FTR(r, f)		ID_AA64##r##_EL1_##f

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
		/* Support everything but PtrAuth and Spec Invalidation */
		val &= ~(GENMASK_ULL(63, 56)	|
			 NV_FTR(ISAR1, SPECRES)	|
			 NV_FTR(ISAR1, GPI)	|
			 NV_FTR(ISAR1, GPA)	|
			 NV_FTR(ISAR1, API)	|
			 NV_FTR(ISAR1, APA));
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
		/* Only support SSBS */
		val &= NV_FTR(PFR1, SSBS);
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
