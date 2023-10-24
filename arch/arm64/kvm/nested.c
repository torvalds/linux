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
void access_nested_id_reg(struct kvm_vcpu *v, struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	u32 id = reg_to_encoding(r);
	u64 val, tmp;

	val = p->regval;

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

	p->regval = val;
}
