// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC
 * Author: Marc Zyngier <maz@kernel.org>
 */

#include <linux/kvm_host.h>
#include <asm/sysreg.h>

struct reg_bits_to_feat_map {
	u64		bits;

#define	NEVER_FGU	BIT(0)	/* Can trap, but never UNDEF */
#define	CALL_FUNC	BIT(1)	/* Needs to evaluate tons of crap */
#define	FIXED_VALUE	BIT(2)	/* RAZ/WI or RAO/WI in KVM */
	unsigned long	flags;

	union {
		struct {
			u8	regidx;
			u8	shift;
			u8	width;
			bool	sign;
			s8	lo_lim;
		};
		bool	(*match)(struct kvm *);
		bool	(*fval)(struct kvm *, u64 *);
	};
};

#define __NEEDS_FEAT_3(m, f, id, fld, lim)		\
	{						\
		.bits	= (m),				\
		.flags = (f),				\
		.regidx	= IDREG_IDX(SYS_ ## id),	\
		.shift	= id ##_## fld ## _SHIFT,	\
		.width	= id ##_## fld ## _WIDTH,	\
		.sign	= id ##_## fld ## _SIGNED,	\
		.lo_lim	= id ##_## fld ##_## lim	\
	}

#define __NEEDS_FEAT_2(m, f, fun, dummy)		\
	{						\
		.bits	= (m),				\
		.flags = (f) | CALL_FUNC,		\
		.fval = (fun),				\
	}

#define __NEEDS_FEAT_1(m, f, fun)			\
	{						\
		.bits	= (m),				\
		.flags = (f) | CALL_FUNC,		\
		.match = (fun),				\
	}

#define NEEDS_FEAT_FLAG(m, f, ...)			\
	CONCATENATE(__NEEDS_FEAT_, COUNT_ARGS(__VA_ARGS__))(m, f, __VA_ARGS__)

#define NEEDS_FEAT_FIXED(m, ...)			\
	NEEDS_FEAT_FLAG(m, FIXED_VALUE, __VA_ARGS__, 0)

#define NEEDS_FEAT(m, ...)	NEEDS_FEAT_FLAG(m, 0, __VA_ARGS__)

#define FEAT_SPE		ID_AA64DFR0_EL1, PMSVer, IMP
#define FEAT_SPE_FnE		ID_AA64DFR0_EL1, PMSVer, V1P2
#define FEAT_BRBE		ID_AA64DFR0_EL1, BRBE, IMP
#define FEAT_TRC_SR		ID_AA64DFR0_EL1, TraceVer, IMP
#define FEAT_PMUv3		ID_AA64DFR0_EL1, PMUVer, IMP
#define FEAT_PMUv3p9		ID_AA64DFR0_EL1, PMUVer, V3P9
#define FEAT_TRBE		ID_AA64DFR0_EL1, TraceBuffer, IMP
#define FEAT_TRBEv1p1		ID_AA64DFR0_EL1, TraceBuffer, TRBE_V1P1
#define FEAT_DoubleLock		ID_AA64DFR0_EL1, DoubleLock, IMP
#define FEAT_TRF		ID_AA64DFR0_EL1, TraceFilt, IMP
#define FEAT_AA32EL0		ID_AA64PFR0_EL1, EL0, AARCH32
#define FEAT_AA32EL1		ID_AA64PFR0_EL1, EL1, AARCH32
#define FEAT_AA64EL1		ID_AA64PFR0_EL1, EL1, IMP
#define FEAT_AA64EL3		ID_AA64PFR0_EL1, EL3, IMP
#define FEAT_AIE		ID_AA64MMFR3_EL1, AIE, IMP
#define FEAT_S2POE		ID_AA64MMFR3_EL1, S2POE, IMP
#define FEAT_S1POE		ID_AA64MMFR3_EL1, S1POE, IMP
#define FEAT_S1PIE		ID_AA64MMFR3_EL1, S1PIE, IMP
#define FEAT_THE		ID_AA64PFR1_EL1, THE, IMP
#define FEAT_SME		ID_AA64PFR1_EL1, SME, IMP
#define FEAT_GCS		ID_AA64PFR1_EL1, GCS, IMP
#define FEAT_LS64		ID_AA64ISAR1_EL1, LS64, LS64
#define FEAT_LS64_V		ID_AA64ISAR1_EL1, LS64, LS64_V
#define FEAT_LS64_ACCDATA	ID_AA64ISAR1_EL1, LS64, LS64_ACCDATA
#define FEAT_RAS		ID_AA64PFR0_EL1, RAS, IMP
#define FEAT_RASv2		ID_AA64PFR0_EL1, RAS, V2
#define FEAT_GICv3		ID_AA64PFR0_EL1, GIC, IMP
#define FEAT_LOR		ID_AA64MMFR1_EL1, LO, IMP
#define FEAT_SPEv1p4		ID_AA64DFR0_EL1, PMSVer, V1P4
#define FEAT_SPEv1p5		ID_AA64DFR0_EL1, PMSVer, V1P5
#define FEAT_ATS1A		ID_AA64ISAR2_EL1, ATS1A, IMP
#define FEAT_SPECRES2		ID_AA64ISAR1_EL1, SPECRES, COSP_RCTX
#define FEAT_SPECRES		ID_AA64ISAR1_EL1, SPECRES, IMP
#define FEAT_TLBIRANGE		ID_AA64ISAR0_EL1, TLB, RANGE
#define FEAT_TLBIOS		ID_AA64ISAR0_EL1, TLB, OS
#define FEAT_PAN2		ID_AA64MMFR1_EL1, PAN, PAN2
#define FEAT_DPB2		ID_AA64ISAR1_EL1, DPB, DPB2
#define FEAT_AMUv1		ID_AA64PFR0_EL1, AMU, IMP
#define FEAT_AMUv1p1		ID_AA64PFR0_EL1, AMU, V1P1
#define FEAT_CMOW		ID_AA64MMFR1_EL1, CMOW, IMP
#define FEAT_D128		ID_AA64MMFR3_EL1, D128, IMP
#define FEAT_DoubleFault2	ID_AA64PFR1_EL1, DF2, IMP
#define FEAT_FPMR		ID_AA64PFR2_EL1, FPMR, IMP
#define FEAT_MOPS		ID_AA64ISAR2_EL1, MOPS, IMP
#define FEAT_NMI		ID_AA64PFR1_EL1, NMI, IMP
#define FEAT_SCTLR2		ID_AA64MMFR3_EL1, SCTLRX, IMP
#define FEAT_SYSREG128		ID_AA64ISAR2_EL1, SYSREG_128, IMP
#define FEAT_TCR2		ID_AA64MMFR3_EL1, TCRX, IMP
#define FEAT_XS			ID_AA64ISAR1_EL1, XS, IMP
#define FEAT_EVT		ID_AA64MMFR2_EL1, EVT, IMP
#define FEAT_EVT_TTLBxS		ID_AA64MMFR2_EL1, EVT, TTLBxS
#define FEAT_MTE2		ID_AA64PFR1_EL1, MTE, MTE2
#define FEAT_RME		ID_AA64PFR0_EL1, RME, IMP
#define FEAT_MPAM		ID_AA64PFR0_EL1, MPAM, 1
#define FEAT_S2FWB		ID_AA64MMFR2_EL1, FWB, IMP
#define FEAT_TME		ID_AA64ISAR0_EL1, TME, IMP
#define FEAT_TWED		ID_AA64MMFR1_EL1, TWED, IMP
#define FEAT_E2H0		ID_AA64MMFR4_EL1, E2H0, IMP
#define FEAT_SRMASK		ID_AA64MMFR4_EL1, SRMASK, IMP
#define FEAT_PoPS		ID_AA64MMFR4_EL1, PoPS, IMP
#define FEAT_PFAR		ID_AA64PFR1_EL1, PFAR, IMP
#define FEAT_Debugv8p9		ID_AA64DFR0_EL1, PMUVer, V3P9
#define FEAT_PMUv3_SS		ID_AA64DFR0_EL1, PMSS, IMP
#define FEAT_SEBEP		ID_AA64DFR0_EL1, SEBEP, IMP
#define FEAT_EBEP		ID_AA64DFR1_EL1, EBEP, IMP
#define FEAT_ITE		ID_AA64DFR1_EL1, ITE, IMP
#define FEAT_PMUv3_ICNTR	ID_AA64DFR1_EL1, PMICNTR, IMP
#define FEAT_SPMU		ID_AA64DFR1_EL1, SPMU, IMP
#define FEAT_SPE_nVM		ID_AA64DFR2_EL1, SPE_nVM, IMP
#define FEAT_STEP2		ID_AA64DFR2_EL1, STEP, IMP
#define FEAT_SYSREG128		ID_AA64ISAR2_EL1, SYSREG_128, IMP
#define FEAT_CPA2		ID_AA64ISAR3_EL1, CPA, CPA2

static bool not_feat_aa64el3(struct kvm *kvm)
{
	return !kvm_has_feat(kvm, FEAT_AA64EL3);
}

static bool feat_nv2(struct kvm *kvm)
{
	return ((kvm_has_feat(kvm, ID_AA64MMFR4_EL1, NV_frac, NV2_ONLY) &&
		 kvm_has_feat_enum(kvm, ID_AA64MMFR2_EL1, NV, NI)) ||
		kvm_has_feat(kvm, ID_AA64MMFR2_EL1, NV, NV2));
}

static bool feat_nv2_e2h0_ni(struct kvm *kvm)
{
	return feat_nv2(kvm) && !kvm_has_feat(kvm, FEAT_E2H0);
}

static bool feat_rasv1p1(struct kvm *kvm)
{
	return (kvm_has_feat(kvm, ID_AA64PFR0_EL1, RAS, V1P1) ||
		(kvm_has_feat_enum(kvm, ID_AA64PFR0_EL1, RAS, IMP) &&
		 kvm_has_feat(kvm, ID_AA64PFR1_EL1, RAS_frac, RASv1p1)));
}

static bool feat_csv2_2_csv2_1p2(struct kvm *kvm)
{
	return (kvm_has_feat(kvm,  ID_AA64PFR0_EL1, CSV2, CSV2_2) ||
		(kvm_has_feat(kvm, ID_AA64PFR1_EL1, CSV2_frac, CSV2_1p2) &&
		 kvm_has_feat_enum(kvm,  ID_AA64PFR0_EL1, CSV2, IMP)));
}

static bool feat_pauth(struct kvm *kvm)
{
	return kvm_has_pauth(kvm, PAuth);
}

static bool feat_pauth_lr(struct kvm *kvm)
{
	return kvm_has_pauth(kvm, PAuth_LR);
}

static bool feat_aderr(struct kvm *kvm)
{
	return (kvm_has_feat(kvm, ID_AA64MMFR3_EL1, ADERR, FEAT_ADERR) &&
		kvm_has_feat(kvm, ID_AA64MMFR3_EL1, SDERR, FEAT_ADERR));
}

static bool feat_anerr(struct kvm *kvm)
{
	return (kvm_has_feat(kvm, ID_AA64MMFR3_EL1, ANERR, FEAT_ANERR) &&
		kvm_has_feat(kvm, ID_AA64MMFR3_EL1, SNERR, FEAT_ANERR));
}

static bool feat_sme_smps(struct kvm *kvm)
{
	/*
	 * Revists this if KVM ever supports SME -- this really should
	 * look at the guest's view of SMIDR_EL1. Funnily enough, this
	 * is not captured in the JSON file, but only as a note in the
	 * ARM ARM.
	 */
	return (kvm_has_feat(kvm, FEAT_SME) &&
		(read_sysreg_s(SYS_SMIDR_EL1) & SMIDR_EL1_SMPS));
}

static bool feat_spe_fds(struct kvm *kvm)
{
	/*
	 * Revists this if KVM ever supports SPE -- this really should
	 * look at the guest's view of PMSIDR_EL1.
	 */
	return (kvm_has_feat(kvm, FEAT_SPEv1p4) &&
		(read_sysreg_s(SYS_PMSIDR_EL1) & PMSIDR_EL1_FDS));
}

static bool feat_trbe_mpam(struct kvm *kvm)
{
	/*
	 * Revists this if KVM ever supports both MPAM and TRBE --
	 * this really should look at the guest's view of TRBIDR_EL1.
	 */
	return (kvm_has_feat(kvm, FEAT_TRBE) &&
		kvm_has_feat(kvm, FEAT_MPAM) &&
		(read_sysreg_s(SYS_TRBIDR_EL1) & TRBIDR_EL1_MPAM));
}

static bool feat_ebep_pmuv3_ss(struct kvm *kvm)
{
	return kvm_has_feat(kvm, FEAT_EBEP) || kvm_has_feat(kvm, FEAT_PMUv3_SS);
}

static bool compute_hcr_rw(struct kvm *kvm, u64 *bits)
{
	/* This is purely academic: AArch32 and NV are mutually exclusive */
	if (bits) {
		if (kvm_has_feat(kvm, FEAT_AA32EL1))
			*bits &= ~HCR_EL2_RW;
		else
			*bits |= HCR_EL2_RW;
	}

	return true;
}

static bool compute_hcr_e2h(struct kvm *kvm, u64 *bits)
{
	if (bits) {
		if (kvm_has_feat(kvm, FEAT_E2H0))
			*bits &= ~HCR_EL2_E2H;
		else
			*bits |= HCR_EL2_E2H;
	}

	return true;
}

static const struct reg_bits_to_feat_map hfgrtr_feat_map[] = {
	NEEDS_FEAT(HFGRTR_EL2_nAMAIR2_EL1	|
		   HFGRTR_EL2_nMAIR2_EL1,
		   FEAT_AIE),
	NEEDS_FEAT(HFGRTR_EL2_nS2POR_EL1, FEAT_S2POE),
	NEEDS_FEAT(HFGRTR_EL2_nPOR_EL1		|
		   HFGRTR_EL2_nPOR_EL0,
		   FEAT_S1POE),
	NEEDS_FEAT(HFGRTR_EL2_nPIR_EL1		|
		   HFGRTR_EL2_nPIRE0_EL1,
		   FEAT_S1PIE),
	NEEDS_FEAT(HFGRTR_EL2_nRCWMASK_EL1, FEAT_THE),
	NEEDS_FEAT(HFGRTR_EL2_nTPIDR2_EL0	|
		   HFGRTR_EL2_nSMPRI_EL1,
		   FEAT_SME),
	NEEDS_FEAT(HFGRTR_EL2_nGCS_EL1		|
		   HFGRTR_EL2_nGCS_EL0,
		   FEAT_GCS),
	NEEDS_FEAT(HFGRTR_EL2_nACCDATA_EL1, FEAT_LS64_ACCDATA),
	NEEDS_FEAT(HFGRTR_EL2_ERXADDR_EL1	|
		   HFGRTR_EL2_ERXMISCn_EL1	|
		   HFGRTR_EL2_ERXSTATUS_EL1	|
		   HFGRTR_EL2_ERXCTLR_EL1	|
		   HFGRTR_EL2_ERXFR_EL1		|
		   HFGRTR_EL2_ERRSELR_EL1	|
		   HFGRTR_EL2_ERRIDR_EL1,
		   FEAT_RAS),
	NEEDS_FEAT(HFGRTR_EL2_ERXPFGCDN_EL1	|
		   HFGRTR_EL2_ERXPFGCTL_EL1	|
		   HFGRTR_EL2_ERXPFGF_EL1,
		   feat_rasv1p1),
	NEEDS_FEAT(HFGRTR_EL2_ICC_IGRPENn_EL1, FEAT_GICv3),
	NEEDS_FEAT(HFGRTR_EL2_SCXTNUM_EL0	|
		   HFGRTR_EL2_SCXTNUM_EL1,
		   feat_csv2_2_csv2_1p2),
	NEEDS_FEAT(HFGRTR_EL2_LORSA_EL1		|
		   HFGRTR_EL2_LORN_EL1		|
		   HFGRTR_EL2_LORID_EL1		|
		   HFGRTR_EL2_LOREA_EL1		|
		   HFGRTR_EL2_LORC_EL1,
		   FEAT_LOR),
	NEEDS_FEAT(HFGRTR_EL2_APIBKey		|
		   HFGRTR_EL2_APIAKey		|
		   HFGRTR_EL2_APGAKey		|
		   HFGRTR_EL2_APDBKey		|
		   HFGRTR_EL2_APDAKey,
		   feat_pauth),
	NEEDS_FEAT_FLAG(HFGRTR_EL2_VBAR_EL1	|
			HFGRTR_EL2_TTBR1_EL1	|
			HFGRTR_EL2_TTBR0_EL1	|
			HFGRTR_EL2_TPIDR_EL0	|
			HFGRTR_EL2_TPIDRRO_EL0	|
			HFGRTR_EL2_TPIDR_EL1	|
			HFGRTR_EL2_TCR_EL1	|
			HFGRTR_EL2_SCTLR_EL1	|
			HFGRTR_EL2_REVIDR_EL1	|
			HFGRTR_EL2_PAR_EL1	|
			HFGRTR_EL2_MPIDR_EL1	|
			HFGRTR_EL2_MIDR_EL1	|
			HFGRTR_EL2_MAIR_EL1	|
			HFGRTR_EL2_ISR_EL1	|
			HFGRTR_EL2_FAR_EL1	|
			HFGRTR_EL2_ESR_EL1	|
			HFGRTR_EL2_DCZID_EL0	|
			HFGRTR_EL2_CTR_EL0	|
			HFGRTR_EL2_CSSELR_EL1	|
			HFGRTR_EL2_CPACR_EL1	|
			HFGRTR_EL2_CONTEXTIDR_EL1|
			HFGRTR_EL2_CLIDR_EL1	|
			HFGRTR_EL2_CCSIDR_EL1	|
			HFGRTR_EL2_AMAIR_EL1	|
			HFGRTR_EL2_AIDR_EL1	|
			HFGRTR_EL2_AFSR1_EL1	|
			HFGRTR_EL2_AFSR0_EL1,
			NEVER_FGU, FEAT_AA64EL1),
};

static const struct reg_bits_to_feat_map hfgwtr_feat_map[] = {
	NEEDS_FEAT(HFGWTR_EL2_nAMAIR2_EL1	|
		   HFGWTR_EL2_nMAIR2_EL1,
		   FEAT_AIE),
	NEEDS_FEAT(HFGWTR_EL2_nS2POR_EL1, FEAT_S2POE),
	NEEDS_FEAT(HFGWTR_EL2_nPOR_EL1		|
		   HFGWTR_EL2_nPOR_EL0,
		   FEAT_S1POE),
	NEEDS_FEAT(HFGWTR_EL2_nPIR_EL1		|
		   HFGWTR_EL2_nPIRE0_EL1,
		   FEAT_S1PIE),
	NEEDS_FEAT(HFGWTR_EL2_nRCWMASK_EL1, FEAT_THE),
	NEEDS_FEAT(HFGWTR_EL2_nTPIDR2_EL0	|
		   HFGWTR_EL2_nSMPRI_EL1,
		   FEAT_SME),
	NEEDS_FEAT(HFGWTR_EL2_nGCS_EL1		|
		   HFGWTR_EL2_nGCS_EL0,
		   FEAT_GCS),
	NEEDS_FEAT(HFGWTR_EL2_nACCDATA_EL1, FEAT_LS64_ACCDATA),
	NEEDS_FEAT(HFGWTR_EL2_ERXADDR_EL1	|
		   HFGWTR_EL2_ERXMISCn_EL1	|
		   HFGWTR_EL2_ERXSTATUS_EL1	|
		   HFGWTR_EL2_ERXCTLR_EL1	|
		   HFGWTR_EL2_ERRSELR_EL1,
		   FEAT_RAS),
	NEEDS_FEAT(HFGWTR_EL2_ERXPFGCDN_EL1	|
		   HFGWTR_EL2_ERXPFGCTL_EL1,
		   feat_rasv1p1),
	NEEDS_FEAT(HFGWTR_EL2_ICC_IGRPENn_EL1, FEAT_GICv3),
	NEEDS_FEAT(HFGWTR_EL2_SCXTNUM_EL0	|
		   HFGWTR_EL2_SCXTNUM_EL1,
		   feat_csv2_2_csv2_1p2),
	NEEDS_FEAT(HFGWTR_EL2_LORSA_EL1		|
		   HFGWTR_EL2_LORN_EL1		|
		   HFGWTR_EL2_LOREA_EL1		|
		   HFGWTR_EL2_LORC_EL1,
		   FEAT_LOR),
	NEEDS_FEAT(HFGWTR_EL2_APIBKey		|
		   HFGWTR_EL2_APIAKey		|
		   HFGWTR_EL2_APGAKey		|
		   HFGWTR_EL2_APDBKey		|
		   HFGWTR_EL2_APDAKey,
		   feat_pauth),
	NEEDS_FEAT_FLAG(HFGWTR_EL2_VBAR_EL1	|
			HFGWTR_EL2_TTBR1_EL1	|
			HFGWTR_EL2_TTBR0_EL1	|
			HFGWTR_EL2_TPIDR_EL0	|
			HFGWTR_EL2_TPIDRRO_EL0	|
			HFGWTR_EL2_TPIDR_EL1	|
			HFGWTR_EL2_TCR_EL1	|
			HFGWTR_EL2_SCTLR_EL1	|
			HFGWTR_EL2_PAR_EL1	|
			HFGWTR_EL2_MAIR_EL1	|
			HFGWTR_EL2_FAR_EL1	|
			HFGWTR_EL2_ESR_EL1	|
			HFGWTR_EL2_CSSELR_EL1	|
			HFGWTR_EL2_CPACR_EL1	|
			HFGWTR_EL2_CONTEXTIDR_EL1|
			HFGWTR_EL2_AMAIR_EL1	|
			HFGWTR_EL2_AFSR1_EL1	|
			HFGWTR_EL2_AFSR0_EL1,
			NEVER_FGU, FEAT_AA64EL1),
};

static const struct reg_bits_to_feat_map hdfgrtr_feat_map[] = {
	NEEDS_FEAT(HDFGRTR_EL2_PMBIDR_EL1	|
		   HDFGRTR_EL2_PMSLATFR_EL1	|
		   HDFGRTR_EL2_PMSIRR_EL1	|
		   HDFGRTR_EL2_PMSIDR_EL1	|
		   HDFGRTR_EL2_PMSICR_EL1	|
		   HDFGRTR_EL2_PMSFCR_EL1	|
		   HDFGRTR_EL2_PMSEVFR_EL1	|
		   HDFGRTR_EL2_PMSCR_EL1	|
		   HDFGRTR_EL2_PMBSR_EL1	|
		   HDFGRTR_EL2_PMBPTR_EL1	|
		   HDFGRTR_EL2_PMBLIMITR_EL1,
		   FEAT_SPE),
	NEEDS_FEAT(HDFGRTR_EL2_nPMSNEVFR_EL1, FEAT_SPE_FnE),
	NEEDS_FEAT(HDFGRTR_EL2_nBRBDATA		|
		   HDFGRTR_EL2_nBRBCTL		|
		   HDFGRTR_EL2_nBRBIDR,
		   FEAT_BRBE),
	NEEDS_FEAT(HDFGRTR_EL2_TRCVICTLR	|
		   HDFGRTR_EL2_TRCSTATR		|
		   HDFGRTR_EL2_TRCSSCSRn	|
		   HDFGRTR_EL2_TRCSEQSTR	|
		   HDFGRTR_EL2_TRCPRGCTLR	|
		   HDFGRTR_EL2_TRCOSLSR		|
		   HDFGRTR_EL2_TRCIMSPECn	|
		   HDFGRTR_EL2_TRCID		|
		   HDFGRTR_EL2_TRCCNTVRn	|
		   HDFGRTR_EL2_TRCCLAIM		|
		   HDFGRTR_EL2_TRCAUXCTLR	|
		   HDFGRTR_EL2_TRCAUTHSTATUS	|
		   HDFGRTR_EL2_TRC,
		   FEAT_TRC_SR),
	NEEDS_FEAT(HDFGRTR_EL2_PMCEIDn_EL0	|
		   HDFGRTR_EL2_PMUSERENR_EL0	|
		   HDFGRTR_EL2_PMMIR_EL1	|
		   HDFGRTR_EL2_PMSELR_EL0	|
		   HDFGRTR_EL2_PMOVS		|
		   HDFGRTR_EL2_PMINTEN		|
		   HDFGRTR_EL2_PMCNTEN		|
		   HDFGRTR_EL2_PMCCNTR_EL0	|
		   HDFGRTR_EL2_PMCCFILTR_EL0	|
		   HDFGRTR_EL2_PMEVTYPERn_EL0	|
		   HDFGRTR_EL2_PMEVCNTRn_EL0,
		   FEAT_PMUv3),
	NEEDS_FEAT(HDFGRTR_EL2_TRBTRG_EL1	|
		   HDFGRTR_EL2_TRBSR_EL1	|
		   HDFGRTR_EL2_TRBPTR_EL1	|
		   HDFGRTR_EL2_TRBMAR_EL1	|
		   HDFGRTR_EL2_TRBLIMITR_EL1	|
		   HDFGRTR_EL2_TRBIDR_EL1	|
		   HDFGRTR_EL2_TRBBASER_EL1,
		   FEAT_TRBE),
	NEEDS_FEAT_FLAG(HDFGRTR_EL2_OSDLR_EL1, NEVER_FGU,
			FEAT_DoubleLock),
	NEEDS_FEAT_FLAG(HDFGRTR_EL2_OSECCR_EL1	|
			HDFGRTR_EL2_OSLSR_EL1	|
			HDFGRTR_EL2_DBGPRCR_EL1	|
			HDFGRTR_EL2_DBGAUTHSTATUS_EL1|
			HDFGRTR_EL2_DBGCLAIM	|
			HDFGRTR_EL2_MDSCR_EL1	|
			HDFGRTR_EL2_DBGWVRn_EL1	|
			HDFGRTR_EL2_DBGWCRn_EL1	|
			HDFGRTR_EL2_DBGBVRn_EL1	|
			HDFGRTR_EL2_DBGBCRn_EL1,
			NEVER_FGU, FEAT_AA64EL1)
};

static const struct reg_bits_to_feat_map hdfgwtr_feat_map[] = {
	NEEDS_FEAT(HDFGWTR_EL2_PMSLATFR_EL1	|
		   HDFGWTR_EL2_PMSIRR_EL1	|
		   HDFGWTR_EL2_PMSICR_EL1	|
		   HDFGWTR_EL2_PMSFCR_EL1	|
		   HDFGWTR_EL2_PMSEVFR_EL1	|
		   HDFGWTR_EL2_PMSCR_EL1	|
		   HDFGWTR_EL2_PMBSR_EL1	|
		   HDFGWTR_EL2_PMBPTR_EL1	|
		   HDFGWTR_EL2_PMBLIMITR_EL1,
		   FEAT_SPE),
	NEEDS_FEAT(HDFGWTR_EL2_nPMSNEVFR_EL1, FEAT_SPE_FnE),
	NEEDS_FEAT(HDFGWTR_EL2_nBRBDATA		|
		   HDFGWTR_EL2_nBRBCTL,
		   FEAT_BRBE),
	NEEDS_FEAT(HDFGWTR_EL2_TRCVICTLR	|
		   HDFGWTR_EL2_TRCSSCSRn	|
		   HDFGWTR_EL2_TRCSEQSTR	|
		   HDFGWTR_EL2_TRCPRGCTLR	|
		   HDFGWTR_EL2_TRCOSLAR		|
		   HDFGWTR_EL2_TRCIMSPECn	|
		   HDFGWTR_EL2_TRCCNTVRn	|
		   HDFGWTR_EL2_TRCCLAIM		|
		   HDFGWTR_EL2_TRCAUXCTLR	|
		   HDFGWTR_EL2_TRC,
		   FEAT_TRC_SR),
	NEEDS_FEAT(HDFGWTR_EL2_PMUSERENR_EL0	|
		   HDFGWTR_EL2_PMCR_EL0		|
		   HDFGWTR_EL2_PMSWINC_EL0	|
		   HDFGWTR_EL2_PMSELR_EL0	|
		   HDFGWTR_EL2_PMOVS		|
		   HDFGWTR_EL2_PMINTEN		|
		   HDFGWTR_EL2_PMCNTEN		|
		   HDFGWTR_EL2_PMCCNTR_EL0	|
		   HDFGWTR_EL2_PMCCFILTR_EL0	|
		   HDFGWTR_EL2_PMEVTYPERn_EL0	|
		   HDFGWTR_EL2_PMEVCNTRn_EL0,
		   FEAT_PMUv3),
	NEEDS_FEAT(HDFGWTR_EL2_TRBTRG_EL1	|
		   HDFGWTR_EL2_TRBSR_EL1	|
		   HDFGWTR_EL2_TRBPTR_EL1	|
		   HDFGWTR_EL2_TRBMAR_EL1	|
		   HDFGWTR_EL2_TRBLIMITR_EL1	|
		   HDFGWTR_EL2_TRBBASER_EL1,
		   FEAT_TRBE),
	NEEDS_FEAT_FLAG(HDFGWTR_EL2_OSDLR_EL1,
			NEVER_FGU, FEAT_DoubleLock),
	NEEDS_FEAT_FLAG(HDFGWTR_EL2_OSECCR_EL1	|
			HDFGWTR_EL2_OSLAR_EL1	|
			HDFGWTR_EL2_DBGPRCR_EL1	|
			HDFGWTR_EL2_DBGCLAIM	|
			HDFGWTR_EL2_MDSCR_EL1	|
			HDFGWTR_EL2_DBGWVRn_EL1	|
			HDFGWTR_EL2_DBGWCRn_EL1	|
			HDFGWTR_EL2_DBGBVRn_EL1	|
			HDFGWTR_EL2_DBGBCRn_EL1,
			NEVER_FGU, FEAT_AA64EL1),
	NEEDS_FEAT(HDFGWTR_EL2_TRFCR_EL1, FEAT_TRF),
};


static const struct reg_bits_to_feat_map hfgitr_feat_map[] = {
	NEEDS_FEAT(HFGITR_EL2_PSBCSYNC, FEAT_SPEv1p5),
	NEEDS_FEAT(HFGITR_EL2_ATS1E1A, FEAT_ATS1A),
	NEEDS_FEAT(HFGITR_EL2_COSPRCTX, FEAT_SPECRES2),
	NEEDS_FEAT(HFGITR_EL2_nGCSEPP		|
		   HFGITR_EL2_nGCSSTR_EL1	|
		   HFGITR_EL2_nGCSPUSHM_EL1,
		   FEAT_GCS),
	NEEDS_FEAT(HFGITR_EL2_nBRBIALL		|
		   HFGITR_EL2_nBRBINJ,
		   FEAT_BRBE),
	NEEDS_FEAT(HFGITR_EL2_CPPRCTX		|
		   HFGITR_EL2_DVPRCTX		|
		   HFGITR_EL2_CFPRCTX,
		   FEAT_SPECRES),
	NEEDS_FEAT(HFGITR_EL2_TLBIRVAALE1	|
		   HFGITR_EL2_TLBIRVALE1	|
		   HFGITR_EL2_TLBIRVAAE1	|
		   HFGITR_EL2_TLBIRVAE1		|
		   HFGITR_EL2_TLBIRVAALE1IS	|
		   HFGITR_EL2_TLBIRVALE1IS	|
		   HFGITR_EL2_TLBIRVAAE1IS	|
		   HFGITR_EL2_TLBIRVAE1IS	|
		   HFGITR_EL2_TLBIRVAALE1OS	|
		   HFGITR_EL2_TLBIRVALE1OS	|
		   HFGITR_EL2_TLBIRVAAE1OS	|
		   HFGITR_EL2_TLBIRVAE1OS,
		   FEAT_TLBIRANGE),
	NEEDS_FEAT(HFGITR_EL2_TLBIVAALE1OS	|
		   HFGITR_EL2_TLBIVALE1OS	|
		   HFGITR_EL2_TLBIVAAE1OS	|
		   HFGITR_EL2_TLBIASIDE1OS	|
		   HFGITR_EL2_TLBIVAE1OS	|
		   HFGITR_EL2_TLBIVMALLE1OS,
		   FEAT_TLBIOS),
	NEEDS_FEAT(HFGITR_EL2_ATS1E1WP		|
		   HFGITR_EL2_ATS1E1RP,
		   FEAT_PAN2),
	NEEDS_FEAT(HFGITR_EL2_DCCVADP, FEAT_DPB2),
	NEEDS_FEAT_FLAG(HFGITR_EL2_DCCVAC	|
			HFGITR_EL2_SVC_EL1	|
			HFGITR_EL2_SVC_EL0	|
			HFGITR_EL2_ERET		|
			HFGITR_EL2_TLBIVAALE1	|
			HFGITR_EL2_TLBIVALE1	|
			HFGITR_EL2_TLBIVAAE1	|
			HFGITR_EL2_TLBIASIDE1	|
			HFGITR_EL2_TLBIVAE1	|
			HFGITR_EL2_TLBIVMALLE1	|
			HFGITR_EL2_TLBIVAALE1IS	|
			HFGITR_EL2_TLBIVALE1IS	|
			HFGITR_EL2_TLBIVAAE1IS	|
			HFGITR_EL2_TLBIASIDE1IS	|
			HFGITR_EL2_TLBIVAE1IS	|
			HFGITR_EL2_TLBIVMALLE1IS|
			HFGITR_EL2_ATS1E0W	|
			HFGITR_EL2_ATS1E0R	|
			HFGITR_EL2_ATS1E1W	|
			HFGITR_EL2_ATS1E1R	|
			HFGITR_EL2_DCZVA	|
			HFGITR_EL2_DCCIVAC	|
			HFGITR_EL2_DCCVAP	|
			HFGITR_EL2_DCCVAU	|
			HFGITR_EL2_DCCISW	|
			HFGITR_EL2_DCCSW	|
			HFGITR_EL2_DCISW	|
			HFGITR_EL2_DCIVAC	|
			HFGITR_EL2_ICIVAU	|
			HFGITR_EL2_ICIALLU	|
			HFGITR_EL2_ICIALLUIS,
			NEVER_FGU, FEAT_AA64EL1),
};

static const struct reg_bits_to_feat_map hafgrtr_feat_map[] = {
	NEEDS_FEAT(HAFGRTR_EL2_AMEVTYPER115_EL0	|
		   HAFGRTR_EL2_AMEVTYPER114_EL0	|
		   HAFGRTR_EL2_AMEVTYPER113_EL0	|
		   HAFGRTR_EL2_AMEVTYPER112_EL0	|
		   HAFGRTR_EL2_AMEVTYPER111_EL0	|
		   HAFGRTR_EL2_AMEVTYPER110_EL0	|
		   HAFGRTR_EL2_AMEVTYPER19_EL0	|
		   HAFGRTR_EL2_AMEVTYPER18_EL0	|
		   HAFGRTR_EL2_AMEVTYPER17_EL0	|
		   HAFGRTR_EL2_AMEVTYPER16_EL0	|
		   HAFGRTR_EL2_AMEVTYPER15_EL0	|
		   HAFGRTR_EL2_AMEVTYPER14_EL0	|
		   HAFGRTR_EL2_AMEVTYPER13_EL0	|
		   HAFGRTR_EL2_AMEVTYPER12_EL0	|
		   HAFGRTR_EL2_AMEVTYPER11_EL0	|
		   HAFGRTR_EL2_AMEVTYPER10_EL0	|
		   HAFGRTR_EL2_AMEVCNTR115_EL0	|
		   HAFGRTR_EL2_AMEVCNTR114_EL0	|
		   HAFGRTR_EL2_AMEVCNTR113_EL0	|
		   HAFGRTR_EL2_AMEVCNTR112_EL0	|
		   HAFGRTR_EL2_AMEVCNTR111_EL0	|
		   HAFGRTR_EL2_AMEVCNTR110_EL0	|
		   HAFGRTR_EL2_AMEVCNTR19_EL0	|
		   HAFGRTR_EL2_AMEVCNTR18_EL0	|
		   HAFGRTR_EL2_AMEVCNTR17_EL0	|
		   HAFGRTR_EL2_AMEVCNTR16_EL0	|
		   HAFGRTR_EL2_AMEVCNTR15_EL0	|
		   HAFGRTR_EL2_AMEVCNTR14_EL0	|
		   HAFGRTR_EL2_AMEVCNTR13_EL0	|
		   HAFGRTR_EL2_AMEVCNTR12_EL0	|
		   HAFGRTR_EL2_AMEVCNTR11_EL0	|
		   HAFGRTR_EL2_AMEVCNTR10_EL0	|
		   HAFGRTR_EL2_AMCNTEN1		|
		   HAFGRTR_EL2_AMCNTEN0		|
		   HAFGRTR_EL2_AMEVCNTR03_EL0	|
		   HAFGRTR_EL2_AMEVCNTR02_EL0	|
		   HAFGRTR_EL2_AMEVCNTR01_EL0	|
		   HAFGRTR_EL2_AMEVCNTR00_EL0,
		   FEAT_AMUv1),
};

static const struct reg_bits_to_feat_map hfgitr2_feat_map[] = {
	NEEDS_FEAT(HFGITR2_EL2_nDCCIVAPS, FEAT_PoPS),
	NEEDS_FEAT(HFGITR2_EL2_TSBCSYNC, FEAT_TRBEv1p1)
};

static const struct reg_bits_to_feat_map hfgrtr2_feat_map[] = {
	NEEDS_FEAT(HFGRTR2_EL2_nPFAR_EL1, FEAT_PFAR),
	NEEDS_FEAT(HFGRTR2_EL2_nERXGSR_EL1, FEAT_RASv2),
	NEEDS_FEAT(HFGRTR2_EL2_nACTLRALIAS_EL1	|
		   HFGRTR2_EL2_nACTLRMASK_EL1	|
		   HFGRTR2_EL2_nCPACRALIAS_EL1	|
		   HFGRTR2_EL2_nCPACRMASK_EL1	|
		   HFGRTR2_EL2_nSCTLR2MASK_EL1	|
		   HFGRTR2_EL2_nSCTLRALIAS2_EL1	|
		   HFGRTR2_EL2_nSCTLRALIAS_EL1	|
		   HFGRTR2_EL2_nSCTLRMASK_EL1	|
		   HFGRTR2_EL2_nTCR2ALIAS_EL1	|
		   HFGRTR2_EL2_nTCR2MASK_EL1	|
		   HFGRTR2_EL2_nTCRALIAS_EL1	|
		   HFGRTR2_EL2_nTCRMASK_EL1,
		   FEAT_SRMASK),
	NEEDS_FEAT(HFGRTR2_EL2_nRCWSMASK_EL1, FEAT_THE),
};

static const struct reg_bits_to_feat_map hfgwtr2_feat_map[] = {
	NEEDS_FEAT(HFGWTR2_EL2_nPFAR_EL1, FEAT_PFAR),
	NEEDS_FEAT(HFGWTR2_EL2_nACTLRALIAS_EL1	|
		   HFGWTR2_EL2_nACTLRMASK_EL1	|
		   HFGWTR2_EL2_nCPACRALIAS_EL1	|
		   HFGWTR2_EL2_nCPACRMASK_EL1	|
		   HFGWTR2_EL2_nSCTLR2MASK_EL1	|
		   HFGWTR2_EL2_nSCTLRALIAS2_EL1	|
		   HFGWTR2_EL2_nSCTLRALIAS_EL1	|
		   HFGWTR2_EL2_nSCTLRMASK_EL1	|
		   HFGWTR2_EL2_nTCR2ALIAS_EL1	|
		   HFGWTR2_EL2_nTCR2MASK_EL1	|
		   HFGWTR2_EL2_nTCRALIAS_EL1	|
		   HFGWTR2_EL2_nTCRMASK_EL1,
		   FEAT_SRMASK),
	NEEDS_FEAT(HFGWTR2_EL2_nRCWSMASK_EL1, FEAT_THE),
};

static const struct reg_bits_to_feat_map hdfgrtr2_feat_map[] = {
	NEEDS_FEAT(HDFGRTR2_EL2_nMDSELR_EL1, FEAT_Debugv8p9),
	NEEDS_FEAT(HDFGRTR2_EL2_nPMECR_EL1, feat_ebep_pmuv3_ss),
	NEEDS_FEAT(HDFGRTR2_EL2_nTRCITECR_EL1, FEAT_ITE),
	NEEDS_FEAT(HDFGRTR2_EL2_nPMICFILTR_EL0	|
		   HDFGRTR2_EL2_nPMICNTR_EL0,
		   FEAT_PMUv3_ICNTR),
	NEEDS_FEAT(HDFGRTR2_EL2_nPMUACR_EL1, FEAT_PMUv3p9),
	NEEDS_FEAT(HDFGRTR2_EL2_nPMSSCR_EL1	|
		   HDFGRTR2_EL2_nPMSSDATA,
		   FEAT_PMUv3_SS),
	NEEDS_FEAT(HDFGRTR2_EL2_nPMIAR_EL1, FEAT_SEBEP),
	NEEDS_FEAT(HDFGRTR2_EL2_nPMSDSFR_EL1, feat_spe_fds),
	NEEDS_FEAT(HDFGRTR2_EL2_nPMBMAR_EL1, FEAT_SPE_nVM),
	NEEDS_FEAT(HDFGRTR2_EL2_nSPMACCESSR_EL1	|
		   HDFGRTR2_EL2_nSPMCNTEN	|
		   HDFGRTR2_EL2_nSPMCR_EL0	|
		   HDFGRTR2_EL2_nSPMDEVAFF_EL1	|
		   HDFGRTR2_EL2_nSPMEVCNTRn_EL0	|
		   HDFGRTR2_EL2_nSPMEVTYPERn_EL0|
		   HDFGRTR2_EL2_nSPMID		|
		   HDFGRTR2_EL2_nSPMINTEN	|
		   HDFGRTR2_EL2_nSPMOVS		|
		   HDFGRTR2_EL2_nSPMSCR_EL1	|
		   HDFGRTR2_EL2_nSPMSELR_EL0,
		   FEAT_SPMU),
	NEEDS_FEAT(HDFGRTR2_EL2_nMDSTEPOP_EL1, FEAT_STEP2),
	NEEDS_FEAT(HDFGRTR2_EL2_nTRBMPAM_EL1, feat_trbe_mpam),
};

static const struct reg_bits_to_feat_map hdfgwtr2_feat_map[] = {
	NEEDS_FEAT(HDFGWTR2_EL2_nMDSELR_EL1, FEAT_Debugv8p9),
	NEEDS_FEAT(HDFGWTR2_EL2_nPMECR_EL1, feat_ebep_pmuv3_ss),
	NEEDS_FEAT(HDFGWTR2_EL2_nTRCITECR_EL1, FEAT_ITE),
	NEEDS_FEAT(HDFGWTR2_EL2_nPMICFILTR_EL0	|
		   HDFGWTR2_EL2_nPMICNTR_EL0,
		   FEAT_PMUv3_ICNTR),
	NEEDS_FEAT(HDFGWTR2_EL2_nPMUACR_EL1	|
		   HDFGWTR2_EL2_nPMZR_EL0,
		   FEAT_PMUv3p9),
	NEEDS_FEAT(HDFGWTR2_EL2_nPMSSCR_EL1, FEAT_PMUv3_SS),
	NEEDS_FEAT(HDFGWTR2_EL2_nPMIAR_EL1, FEAT_SEBEP),
	NEEDS_FEAT(HDFGWTR2_EL2_nPMSDSFR_EL1, feat_spe_fds),
	NEEDS_FEAT(HDFGWTR2_EL2_nPMBMAR_EL1, FEAT_SPE_nVM),
	NEEDS_FEAT(HDFGWTR2_EL2_nSPMACCESSR_EL1	|
		   HDFGWTR2_EL2_nSPMCNTEN	|
		   HDFGWTR2_EL2_nSPMCR_EL0	|
		   HDFGWTR2_EL2_nSPMEVCNTRn_EL0	|
		   HDFGWTR2_EL2_nSPMEVTYPERn_EL0|
		   HDFGWTR2_EL2_nSPMINTEN	|
		   HDFGWTR2_EL2_nSPMOVS		|
		   HDFGWTR2_EL2_nSPMSCR_EL1	|
		   HDFGWTR2_EL2_nSPMSELR_EL0,
		   FEAT_SPMU),
	NEEDS_FEAT(HDFGWTR2_EL2_nMDSTEPOP_EL1, FEAT_STEP2),
	NEEDS_FEAT(HDFGWTR2_EL2_nTRBMPAM_EL1, feat_trbe_mpam),
};

static const struct reg_bits_to_feat_map hcrx_feat_map[] = {
	NEEDS_FEAT(HCRX_EL2_PACMEn, feat_pauth_lr),
	NEEDS_FEAT(HCRX_EL2_EnFPM, FEAT_FPMR),
	NEEDS_FEAT(HCRX_EL2_GCSEn, FEAT_GCS),
	NEEDS_FEAT(HCRX_EL2_EnIDCP128, FEAT_SYSREG128),
	NEEDS_FEAT(HCRX_EL2_EnSDERR, feat_aderr),
	NEEDS_FEAT(HCRX_EL2_TMEA, FEAT_DoubleFault2),
	NEEDS_FEAT(HCRX_EL2_EnSNERR, feat_anerr),
	NEEDS_FEAT(HCRX_EL2_D128En, FEAT_D128),
	NEEDS_FEAT(HCRX_EL2_PTTWI, FEAT_THE),
	NEEDS_FEAT(HCRX_EL2_SCTLR2En, FEAT_SCTLR2),
	NEEDS_FEAT(HCRX_EL2_TCR2En, FEAT_TCR2),
	NEEDS_FEAT(HCRX_EL2_MSCEn		|
		   HCRX_EL2_MCE2,
		   FEAT_MOPS),
	NEEDS_FEAT(HCRX_EL2_CMOW, FEAT_CMOW),
	NEEDS_FEAT(HCRX_EL2_VFNMI		|
		   HCRX_EL2_VINMI		|
		   HCRX_EL2_TALLINT,
		   FEAT_NMI),
	NEEDS_FEAT(HCRX_EL2_SMPME, feat_sme_smps),
	NEEDS_FEAT(HCRX_EL2_FGTnXS		|
		   HCRX_EL2_FnXS,
		   FEAT_XS),
	NEEDS_FEAT(HCRX_EL2_EnASR, FEAT_LS64_V),
	NEEDS_FEAT(HCRX_EL2_EnALS, FEAT_LS64),
	NEEDS_FEAT(HCRX_EL2_EnAS0, FEAT_LS64_ACCDATA),
};

static const struct reg_bits_to_feat_map hcr_feat_map[] = {
	NEEDS_FEAT(HCR_EL2_TID0, FEAT_AA32EL0),
	NEEDS_FEAT_FIXED(HCR_EL2_RW, compute_hcr_rw),
	NEEDS_FEAT(HCR_EL2_HCD, not_feat_aa64el3),
	NEEDS_FEAT(HCR_EL2_AMO		|
		   HCR_EL2_BSU		|
		   HCR_EL2_CD		|
		   HCR_EL2_DC		|
		   HCR_EL2_FB		|
		   HCR_EL2_FMO		|
		   HCR_EL2_ID		|
		   HCR_EL2_IMO		|
		   HCR_EL2_MIOCNCE	|
		   HCR_EL2_PTW		|
		   HCR_EL2_SWIO		|
		   HCR_EL2_TACR		|
		   HCR_EL2_TDZ		|
		   HCR_EL2_TGE		|
		   HCR_EL2_TID1		|
		   HCR_EL2_TID2		|
		   HCR_EL2_TID3		|
		   HCR_EL2_TIDCP	|
		   HCR_EL2_TPCP		|
		   HCR_EL2_TPU		|
		   HCR_EL2_TRVM		|
		   HCR_EL2_TSC		|
		   HCR_EL2_TSW		|
		   HCR_EL2_TTLB		|
		   HCR_EL2_TVM		|
		   HCR_EL2_TWE		|
		   HCR_EL2_TWI		|
		   HCR_EL2_VF		|
		   HCR_EL2_VI		|
		   HCR_EL2_VM		|
		   HCR_EL2_VSE,
		   FEAT_AA64EL1),
	NEEDS_FEAT(HCR_EL2_AMVOFFEN, FEAT_AMUv1p1),
	NEEDS_FEAT(HCR_EL2_EnSCXT, feat_csv2_2_csv2_1p2),
	NEEDS_FEAT(HCR_EL2_TICAB	|
		   HCR_EL2_TID4		|
		   HCR_EL2_TOCU,
		   FEAT_EVT),
	NEEDS_FEAT(HCR_EL2_TTLBIS	|
		   HCR_EL2_TTLBOS,
		   FEAT_EVT_TTLBxS),
	NEEDS_FEAT(HCR_EL2_TLOR, FEAT_LOR),
	NEEDS_FEAT(HCR_EL2_ATA		|
		   HCR_EL2_DCT		|
		   HCR_EL2_TID5,
		   FEAT_MTE2),
	NEEDS_FEAT(HCR_EL2_AT		| /* Ignore the original FEAT_NV */
		   HCR_EL2_NV2		|
		   HCR_EL2_NV,
		   feat_nv2),
	NEEDS_FEAT(HCR_EL2_NV1, feat_nv2_e2h0_ni), /* Missing from JSON */
	NEEDS_FEAT(HCR_EL2_API		|
		   HCR_EL2_APK,
		   feat_pauth),
	NEEDS_FEAT(HCR_EL2_TEA		|
		   HCR_EL2_TERR,
		   FEAT_RAS),
	NEEDS_FEAT(HCR_EL2_FIEN, feat_rasv1p1),
	NEEDS_FEAT(HCR_EL2_GPF, FEAT_RME),
	NEEDS_FEAT(HCR_EL2_FWB, FEAT_S2FWB),
	NEEDS_FEAT(HCR_EL2_TME, FEAT_TME),
	NEEDS_FEAT(HCR_EL2_TWEDEL	|
		   HCR_EL2_TWEDEn,
		   FEAT_TWED),
	NEEDS_FEAT_FIXED(HCR_EL2_E2H, compute_hcr_e2h),
};

static const struct reg_bits_to_feat_map sctlr2_feat_map[] = {
	NEEDS_FEAT(SCTLR2_EL1_NMEA	|
		   SCTLR2_EL1_EASE,
		   FEAT_DoubleFault2),
	NEEDS_FEAT(SCTLR2_EL1_EnADERR, feat_aderr),
	NEEDS_FEAT(SCTLR2_EL1_EnANERR, feat_anerr),
	NEEDS_FEAT(SCTLR2_EL1_EnIDCP128, FEAT_SYSREG128),
	NEEDS_FEAT(SCTLR2_EL1_EnPACM	|
		   SCTLR2_EL1_EnPACM0,
		   feat_pauth_lr),
	NEEDS_FEAT(SCTLR2_EL1_CPTA	|
		   SCTLR2_EL1_CPTA0	|
		   SCTLR2_EL1_CPTM	|
		   SCTLR2_EL1_CPTM0,
		   FEAT_CPA2),
};

static void __init check_feat_map(const struct reg_bits_to_feat_map *map,
				  int map_size, u64 res0, const char *str)
{
	u64 mask = 0;

	for (int i = 0; i < map_size; i++)
		mask |= map[i].bits;

	if (mask != ~res0)
		kvm_err("Undefined %s behaviour, bits %016llx\n",
			str, mask ^ ~res0);
}

void __init check_feature_map(void)
{
	check_feat_map(hfgrtr_feat_map, ARRAY_SIZE(hfgrtr_feat_map),
		       hfgrtr_masks.res0, hfgrtr_masks.str);
	check_feat_map(hfgwtr_feat_map, ARRAY_SIZE(hfgwtr_feat_map),
		       hfgwtr_masks.res0, hfgwtr_masks.str);
	check_feat_map(hfgitr_feat_map, ARRAY_SIZE(hfgitr_feat_map),
		       hfgitr_masks.res0, hfgitr_masks.str);
	check_feat_map(hdfgrtr_feat_map, ARRAY_SIZE(hdfgrtr_feat_map),
		       hdfgrtr_masks.res0, hdfgrtr_masks.str);
	check_feat_map(hdfgwtr_feat_map, ARRAY_SIZE(hdfgwtr_feat_map),
		       hdfgwtr_masks.res0, hdfgwtr_masks.str);
	check_feat_map(hafgrtr_feat_map, ARRAY_SIZE(hafgrtr_feat_map),
		       hafgrtr_masks.res0, hafgrtr_masks.str);
	check_feat_map(hcrx_feat_map, ARRAY_SIZE(hcrx_feat_map),
		       __HCRX_EL2_RES0, "HCRX_EL2");
	check_feat_map(hcr_feat_map, ARRAY_SIZE(hcr_feat_map),
		       HCR_EL2_RES0, "HCR_EL2");
	check_feat_map(sctlr2_feat_map, ARRAY_SIZE(sctlr2_feat_map),
		       SCTLR2_EL1_RES0, "SCTLR2_EL1");
}

static bool idreg_feat_match(struct kvm *kvm, const struct reg_bits_to_feat_map *map)
{
	u64 regval = kvm->arch.id_regs[map->regidx];
	u64 regfld = (regval >> map->shift) & GENMASK(map->width - 1, 0);

	if (map->sign) {
		s64 sfld = sign_extend64(regfld, map->width - 1);
		s64 slim = sign_extend64(map->lo_lim, map->width - 1);
		return sfld >= slim;
	} else {
		return regfld >= map->lo_lim;
	}
}

static u64 __compute_fixed_bits(struct kvm *kvm,
				const struct reg_bits_to_feat_map *map,
				int map_size,
				u64 *fixed_bits,
				unsigned long require,
				unsigned long exclude)
{
	u64 val = 0;

	for (int i = 0; i < map_size; i++) {
		bool match;

		if ((map[i].flags & require) != require)
			continue;

		if (map[i].flags & exclude)
			continue;

		if (map[i].flags & CALL_FUNC)
			match = (map[i].flags & FIXED_VALUE) ?
				map[i].fval(kvm, fixed_bits) :
				map[i].match(kvm);
		else
			match = idreg_feat_match(kvm, &map[i]);

		if (!match || (map[i].flags & FIXED_VALUE))
			val |= map[i].bits;
	}

	return val;
}

static u64 compute_res0_bits(struct kvm *kvm,
			     const struct reg_bits_to_feat_map *map,
			     int map_size,
			     unsigned long require,
			     unsigned long exclude)
{
	return __compute_fixed_bits(kvm, map, map_size, NULL,
				    require, exclude | FIXED_VALUE);
}

static u64 compute_fixed_bits(struct kvm *kvm,
			      const struct reg_bits_to_feat_map *map,
			      int map_size,
			      u64 *fixed_bits,
			      unsigned long require,
			      unsigned long exclude)
{
	return __compute_fixed_bits(kvm, map, map_size, fixed_bits,
				    require | FIXED_VALUE, exclude);
}

void compute_fgu(struct kvm *kvm, enum fgt_group_id fgt)
{
	u64 val = 0;

	switch (fgt) {
	case HFGRTR_GROUP:
		val |= compute_res0_bits(kvm, hfgrtr_feat_map,
					 ARRAY_SIZE(hfgrtr_feat_map),
					 0, NEVER_FGU);
		val |= compute_res0_bits(kvm, hfgwtr_feat_map,
					 ARRAY_SIZE(hfgwtr_feat_map),
					 0, NEVER_FGU);
		break;
	case HFGITR_GROUP:
		val |= compute_res0_bits(kvm, hfgitr_feat_map,
					 ARRAY_SIZE(hfgitr_feat_map),
					 0, NEVER_FGU);
		break;
	case HDFGRTR_GROUP:
		val |= compute_res0_bits(kvm, hdfgrtr_feat_map,
					 ARRAY_SIZE(hdfgrtr_feat_map),
					 0, NEVER_FGU);
		val |= compute_res0_bits(kvm, hdfgwtr_feat_map,
					 ARRAY_SIZE(hdfgwtr_feat_map),
					 0, NEVER_FGU);
		break;
	case HAFGRTR_GROUP:
		val |= compute_res0_bits(kvm, hafgrtr_feat_map,
					 ARRAY_SIZE(hafgrtr_feat_map),
					 0, NEVER_FGU);
		break;
	case HFGRTR2_GROUP:
		val |= compute_res0_bits(kvm, hfgrtr2_feat_map,
					 ARRAY_SIZE(hfgrtr2_feat_map),
					 0, NEVER_FGU);
		val |= compute_res0_bits(kvm, hfgwtr2_feat_map,
					 ARRAY_SIZE(hfgwtr2_feat_map),
					 0, NEVER_FGU);
		break;
	case HFGITR2_GROUP:
		val |= compute_res0_bits(kvm, hfgitr2_feat_map,
					 ARRAY_SIZE(hfgitr2_feat_map),
					 0, NEVER_FGU);
		break;
	case HDFGRTR2_GROUP:
		val |= compute_res0_bits(kvm, hdfgrtr2_feat_map,
					 ARRAY_SIZE(hdfgrtr2_feat_map),
					 0, NEVER_FGU);
		val |= compute_res0_bits(kvm, hdfgwtr2_feat_map,
					 ARRAY_SIZE(hdfgwtr2_feat_map),
					 0, NEVER_FGU);
		break;
	default:
		BUG();
	}

	kvm->arch.fgu[fgt] = val;
}

void get_reg_fixed_bits(struct kvm *kvm, enum vcpu_sysreg reg, u64 *res0, u64 *res1)
{
	u64 fixed = 0, mask;

	switch (reg) {
	case HFGRTR_EL2:
		*res0 = compute_res0_bits(kvm, hfgrtr_feat_map,
					  ARRAY_SIZE(hfgrtr_feat_map), 0, 0);
		*res0 |= hfgrtr_masks.res0;
		*res1 = HFGRTR_EL2_RES1;
		break;
	case HFGWTR_EL2:
		*res0 = compute_res0_bits(kvm, hfgwtr_feat_map,
					  ARRAY_SIZE(hfgwtr_feat_map), 0, 0);
		*res0 |= hfgwtr_masks.res0;
		*res1 = HFGWTR_EL2_RES1;
		break;
	case HFGITR_EL2:
		*res0 = compute_res0_bits(kvm, hfgitr_feat_map,
					  ARRAY_SIZE(hfgitr_feat_map), 0, 0);
		*res0 |= hfgitr_masks.res0;
		*res1 = HFGITR_EL2_RES1;
		break;
	case HDFGRTR_EL2:
		*res0 = compute_res0_bits(kvm, hdfgrtr_feat_map,
					  ARRAY_SIZE(hdfgrtr_feat_map), 0, 0);
		*res0 |= hdfgrtr_masks.res0;
		*res1 = HDFGRTR_EL2_RES1;
		break;
	case HDFGWTR_EL2:
		*res0 = compute_res0_bits(kvm, hdfgwtr_feat_map,
					  ARRAY_SIZE(hdfgwtr_feat_map), 0, 0);
		*res0 |= hdfgwtr_masks.res0;
		*res1 = HDFGWTR_EL2_RES1;
		break;
	case HAFGRTR_EL2:
		*res0 = compute_res0_bits(kvm, hafgrtr_feat_map,
					  ARRAY_SIZE(hafgrtr_feat_map), 0, 0);
		*res0 |= hafgrtr_masks.res0;
		*res1 = HAFGRTR_EL2_RES1;
		break;
	case HFGRTR2_EL2:
		*res0 = compute_res0_bits(kvm, hfgrtr2_feat_map,
					  ARRAY_SIZE(hfgrtr2_feat_map), 0, 0);
		*res0 |= hfgrtr2_masks.res0;
		*res1 = HFGRTR2_EL2_RES1;
		break;
	case HFGWTR2_EL2:
		*res0 = compute_res0_bits(kvm, hfgwtr2_feat_map,
					  ARRAY_SIZE(hfgwtr2_feat_map), 0, 0);
		*res0 |= hfgwtr2_masks.res0;
		*res1 = HFGWTR2_EL2_RES1;
		break;
	case HFGITR2_EL2:
		*res0 = compute_res0_bits(kvm, hfgitr2_feat_map,
					  ARRAY_SIZE(hfgitr2_feat_map), 0, 0);
		*res0 |= hfgitr2_masks.res0;
		*res1 = HFGITR2_EL2_RES1;
		break;
	case HDFGRTR2_EL2:
		*res0 = compute_res0_bits(kvm, hdfgrtr2_feat_map,
					  ARRAY_SIZE(hdfgrtr2_feat_map), 0, 0);
		*res0 |= hdfgrtr2_masks.res0;
		*res1 = HDFGRTR2_EL2_RES1;
		break;
	case HDFGWTR2_EL2:
		*res0 = compute_res0_bits(kvm, hdfgwtr2_feat_map,
					  ARRAY_SIZE(hdfgwtr2_feat_map), 0, 0);
		*res0 |= hdfgwtr2_masks.res0;
		*res1 = HDFGWTR2_EL2_RES1;
		break;
	case HCRX_EL2:
		*res0 = compute_res0_bits(kvm, hcrx_feat_map,
					  ARRAY_SIZE(hcrx_feat_map), 0, 0);
		*res0 |= __HCRX_EL2_RES0;
		*res1 = __HCRX_EL2_RES1;
		break;
	case HCR_EL2:
		mask = compute_fixed_bits(kvm, hcr_feat_map,
					  ARRAY_SIZE(hcr_feat_map), &fixed,
					  0, 0);
		*res0 = compute_res0_bits(kvm, hcr_feat_map,
					  ARRAY_SIZE(hcr_feat_map), 0, 0);
		*res0 |= HCR_EL2_RES0 | (mask & ~fixed);
		*res1 = HCR_EL2_RES1 | (mask & fixed);
		break;
	case SCTLR2_EL1:
	case SCTLR2_EL2:
		*res0 = compute_res0_bits(kvm, sctlr2_feat_map,
					  ARRAY_SIZE(sctlr2_feat_map), 0, 0);
		*res0 |= SCTLR2_EL1_RES0;
		*res1 = SCTLR2_EL1_RES1;
		break;
	default:
		WARN_ON_ONCE(1);
		*res0 = *res1 = 0;
		break;
	}
}
