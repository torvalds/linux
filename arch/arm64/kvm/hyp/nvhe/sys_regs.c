// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Google LLC
 * Author: Fuad Tabba <tabba@google.com>
 */

#include <linux/irqchip/arm-gic-v3.h>

#include <asm/kvm_asm.h>
#include <asm/kvm_mmu.h>

#include <hyp/adjust_pc.h>

#include <nvhe/pkvm.h>

#include "../../sys_regs.h"

/*
 * Copies of the host's CPU features registers holding sanitized values at hyp.
 */
u64 id_aa64pfr0_el1_sys_val;
u64 id_aa64pfr1_el1_sys_val;
u64 id_aa64isar0_el1_sys_val;
u64 id_aa64isar1_el1_sys_val;
u64 id_aa64isar2_el1_sys_val;
u64 id_aa64mmfr0_el1_sys_val;
u64 id_aa64mmfr1_el1_sys_val;
u64 id_aa64mmfr2_el1_sys_val;
u64 id_aa64smfr0_el1_sys_val;

struct pvm_ftr_bits {
	bool		sign;
	u8		shift;
	u8		width;
	u8		max_val;
	bool (*vm_supported)(const struct kvm *kvm);
};

#define __MAX_FEAT_FUNC(id, fld, max, func, sgn)				\
	{									\
		.sign = sgn,							\
		.shift = id##_##fld##_SHIFT,					\
		.width = id##_##fld##_WIDTH,					\
		.max_val = id##_##fld##_##max,					\
		.vm_supported = func,						\
	}

#define MAX_FEAT_FUNC(id, fld, max, func)					\
	__MAX_FEAT_FUNC(id, fld, max, func, id##_##fld##_SIGNED)

#define MAX_FEAT(id, fld, max)							\
	MAX_FEAT_FUNC(id, fld, max, NULL)

#define MAX_FEAT_ENUM(id, fld, max)						\
	__MAX_FEAT_FUNC(id, fld, max, NULL, false)

#define FEAT_END {	.width = 0,	}

static bool vm_has_ptrauth(const struct kvm *kvm)
{
	if (!IS_ENABLED(CONFIG_ARM64_PTR_AUTH))
		return false;

	return (cpus_have_final_cap(ARM64_HAS_ADDRESS_AUTH) ||
		cpus_have_final_cap(ARM64_HAS_GENERIC_AUTH)) &&
		kvm_vcpu_has_feature(kvm, KVM_ARM_VCPU_PTRAUTH_GENERIC);
}

static bool vm_has_sve(const struct kvm *kvm)
{
	return system_supports_sve() && kvm_vcpu_has_feature(kvm, KVM_ARM_VCPU_SVE);
}

/*
 * Definitions for features to be allowed or restricted for protected guests.
 *
 * Each field in the masks represents the highest supported value for the
 * feature. If a feature field is not present, it is not supported. Moreover,
 * these are used to generate the guest's view of the feature registers.
 *
 * The approach for protected VMs is to at least support features that are:
 * - Needed by common Linux distributions (e.g., floating point)
 * - Trivial to support, e.g., supporting the feature does not introduce or
 * require tracking of additional state in KVM
 * - Cannot be trapped or prevent the guest from using anyway
 */

static const struct pvm_ftr_bits pvmid_aa64pfr0[] = {
	MAX_FEAT(ID_AA64PFR0_EL1, EL0, IMP),
	MAX_FEAT(ID_AA64PFR0_EL1, EL1, IMP),
	MAX_FEAT(ID_AA64PFR0_EL1, EL2, IMP),
	MAX_FEAT(ID_AA64PFR0_EL1, EL3, IMP),
	MAX_FEAT(ID_AA64PFR0_EL1, FP, FP16),
	MAX_FEAT(ID_AA64PFR0_EL1, AdvSIMD, FP16),
	MAX_FEAT(ID_AA64PFR0_EL1, GIC, IMP),
	MAX_FEAT_FUNC(ID_AA64PFR0_EL1, SVE, IMP, vm_has_sve),
	MAX_FEAT(ID_AA64PFR0_EL1, RAS, IMP),
	MAX_FEAT(ID_AA64PFR0_EL1, DIT, IMP),
	MAX_FEAT(ID_AA64PFR0_EL1, CSV2, IMP),
	MAX_FEAT(ID_AA64PFR0_EL1, CSV3, IMP),
	FEAT_END
};

static const struct pvm_ftr_bits pvmid_aa64pfr1[] = {
	MAX_FEAT(ID_AA64PFR1_EL1, BT, IMP),
	MAX_FEAT(ID_AA64PFR1_EL1, SSBS, SSBS2),
	MAX_FEAT_ENUM(ID_AA64PFR1_EL1, MTE_frac, NI),
	FEAT_END
};

static const struct pvm_ftr_bits pvmid_aa64mmfr0[] = {
	MAX_FEAT_ENUM(ID_AA64MMFR0_EL1, PARANGE, 40),
	MAX_FEAT_ENUM(ID_AA64MMFR0_EL1, ASIDBITS, 16),
	MAX_FEAT(ID_AA64MMFR0_EL1, BIGEND, IMP),
	MAX_FEAT(ID_AA64MMFR0_EL1, SNSMEM, IMP),
	MAX_FEAT(ID_AA64MMFR0_EL1, BIGENDEL0, IMP),
	MAX_FEAT(ID_AA64MMFR0_EL1, EXS, IMP),
	FEAT_END
};

static const struct pvm_ftr_bits pvmid_aa64mmfr1[] = {
	MAX_FEAT(ID_AA64MMFR1_EL1, HAFDBS, DBM),
	MAX_FEAT_ENUM(ID_AA64MMFR1_EL1, VMIDBits, 16),
	MAX_FEAT(ID_AA64MMFR1_EL1, HPDS, HPDS2),
	MAX_FEAT(ID_AA64MMFR1_EL1, PAN, PAN3),
	MAX_FEAT(ID_AA64MMFR1_EL1, SpecSEI, IMP),
	MAX_FEAT(ID_AA64MMFR1_EL1, ETS, IMP),
	MAX_FEAT(ID_AA64MMFR1_EL1, CMOW, IMP),
	FEAT_END
};

static const struct pvm_ftr_bits pvmid_aa64mmfr2[] = {
	MAX_FEAT(ID_AA64MMFR2_EL1, CnP, IMP),
	MAX_FEAT(ID_AA64MMFR2_EL1, UAO, IMP),
	MAX_FEAT(ID_AA64MMFR2_EL1, IESB, IMP),
	MAX_FEAT(ID_AA64MMFR2_EL1, AT, IMP),
	MAX_FEAT_ENUM(ID_AA64MMFR2_EL1, IDS, 0x18),
	MAX_FEAT(ID_AA64MMFR2_EL1, TTL, IMP),
	MAX_FEAT(ID_AA64MMFR2_EL1, BBM, 2),
	MAX_FEAT(ID_AA64MMFR2_EL1, E0PD, IMP),
	FEAT_END
};

static const struct pvm_ftr_bits pvmid_aa64isar1[] = {
	MAX_FEAT(ID_AA64ISAR1_EL1, DPB, DPB2),
	MAX_FEAT_FUNC(ID_AA64ISAR1_EL1, APA, PAuth, vm_has_ptrauth),
	MAX_FEAT_FUNC(ID_AA64ISAR1_EL1, API, PAuth, vm_has_ptrauth),
	MAX_FEAT(ID_AA64ISAR1_EL1, JSCVT, IMP),
	MAX_FEAT(ID_AA64ISAR1_EL1, FCMA, IMP),
	MAX_FEAT(ID_AA64ISAR1_EL1, LRCPC, LRCPC3),
	MAX_FEAT(ID_AA64ISAR1_EL1, GPA, IMP),
	MAX_FEAT(ID_AA64ISAR1_EL1, GPI, IMP),
	MAX_FEAT(ID_AA64ISAR1_EL1, FRINTTS, IMP),
	MAX_FEAT(ID_AA64ISAR1_EL1, SB, IMP),
	MAX_FEAT(ID_AA64ISAR1_EL1, SPECRES, COSP_RCTX),
	MAX_FEAT(ID_AA64ISAR1_EL1, BF16, EBF16),
	MAX_FEAT(ID_AA64ISAR1_EL1, DGH, IMP),
	MAX_FEAT(ID_AA64ISAR1_EL1, I8MM, IMP),
	FEAT_END
};

static const struct pvm_ftr_bits pvmid_aa64isar2[] = {
	MAX_FEAT_FUNC(ID_AA64ISAR2_EL1, GPA3, IMP, vm_has_ptrauth),
	MAX_FEAT_FUNC(ID_AA64ISAR2_EL1, APA3, PAuth, vm_has_ptrauth),
	MAX_FEAT(ID_AA64ISAR2_EL1, ATS1A, IMP),
	FEAT_END
};

/*
 * None of the features in ID_AA64DFR0_EL1 nor ID_AA64MMFR4_EL1 are supported.
 * However, both have Not-Implemented values that are non-zero. Define them
 * so they can be used when getting the value of these registers.
 */
#define ID_AA64DFR0_EL1_NONZERO_NI					\
(									\
	SYS_FIELD_PREP_ENUM(ID_AA64DFR0_EL1, DoubleLock, NI)	|	\
	SYS_FIELD_PREP_ENUM(ID_AA64DFR0_EL1, MTPMU, NI)			\
)

#define ID_AA64MMFR4_EL1_NONZERO_NI					\
	SYS_FIELD_PREP_ENUM(ID_AA64MMFR4_EL1, E2H0, NI)

/*
 * Returns the value of the feature registers based on the system register
 * value, the vcpu support for the revelant features, and the additional
 * restrictions for protected VMs.
 */
static u64 get_restricted_features(const struct kvm_vcpu *vcpu,
				   u64 sys_reg_val,
				   const struct pvm_ftr_bits restrictions[])
{
	u64 val = 0UL;
	int i;

	for (i = 0; restrictions[i].width != 0; i++) {
		bool (*vm_supported)(const struct kvm *) = restrictions[i].vm_supported;
		bool sign = restrictions[i].sign;
		int shift = restrictions[i].shift;
		int width = restrictions[i].width;
		u64 min_signed = (1UL << width) - 1UL;
		u64 sign_bit = 1UL << (width - 1);
		u64 mask = GENMASK_ULL(width + shift - 1, shift);
		u64 sys_val = (sys_reg_val & mask) >> shift;
		u64 pvm_max = restrictions[i].max_val;

		if (vm_supported && !vm_supported(vcpu->kvm))
			val |= (sign ? min_signed : 0) << shift;
		else if (sign && (sys_val >= sign_bit || pvm_max >= sign_bit))
			val |= max(sys_val, pvm_max) << shift;
		else
			val |= min(sys_val, pvm_max) << shift;
	}

	return val;
}

static u64 pvm_calc_id_reg(const struct kvm_vcpu *vcpu, u32 id)
{
	switch (id) {
	case SYS_ID_AA64PFR0_EL1:
		return get_restricted_features(vcpu, id_aa64pfr0_el1_sys_val, pvmid_aa64pfr0);
	case SYS_ID_AA64PFR1_EL1:
		return get_restricted_features(vcpu, id_aa64pfr1_el1_sys_val, pvmid_aa64pfr1);
	case SYS_ID_AA64ISAR0_EL1:
		return id_aa64isar0_el1_sys_val;
	case SYS_ID_AA64ISAR1_EL1:
		return get_restricted_features(vcpu, id_aa64isar1_el1_sys_val, pvmid_aa64isar1);
	case SYS_ID_AA64ISAR2_EL1:
		return get_restricted_features(vcpu, id_aa64isar2_el1_sys_val, pvmid_aa64isar2);
	case SYS_ID_AA64MMFR0_EL1:
		return get_restricted_features(vcpu, id_aa64mmfr0_el1_sys_val, pvmid_aa64mmfr0);
	case SYS_ID_AA64MMFR1_EL1:
		return get_restricted_features(vcpu, id_aa64mmfr1_el1_sys_val, pvmid_aa64mmfr1);
	case SYS_ID_AA64MMFR2_EL1:
		return get_restricted_features(vcpu, id_aa64mmfr2_el1_sys_val, pvmid_aa64mmfr2);
	case SYS_ID_AA64DFR0_EL1:
		return ID_AA64DFR0_EL1_NONZERO_NI;
	case SYS_ID_AA64MMFR4_EL1:
		return ID_AA64MMFR4_EL1_NONZERO_NI;
	default:
		/* Unhandled ID register, RAZ */
		return 0;
	}
}

/*
 * Inject an unknown/undefined exception to an AArch64 guest while most of its
 * sysregs are live.
 */
static void inject_undef64(struct kvm_vcpu *vcpu)
{
	u64 esr = (ESR_ELx_EC_UNKNOWN << ESR_ELx_EC_SHIFT);

	*vcpu_pc(vcpu) = read_sysreg_el2(SYS_ELR);
	*vcpu_cpsr(vcpu) = read_sysreg_el2(SYS_SPSR);

	kvm_pend_exception(vcpu, EXCEPT_AA64_EL1_SYNC);

	__kvm_adjust_pc(vcpu);

	write_sysreg_el1(esr, SYS_ESR);
	write_sysreg_el1(read_sysreg_el2(SYS_ELR), SYS_ELR);
	write_sysreg_el2(*vcpu_pc(vcpu), SYS_ELR);
	write_sysreg_el2(*vcpu_cpsr(vcpu), SYS_SPSR);
}

static u64 read_id_reg(const struct kvm_vcpu *vcpu,
		       struct sys_reg_desc const *r)
{
	struct kvm *kvm = vcpu->kvm;
	u32 reg = reg_to_encoding(r);

	if (WARN_ON_ONCE(!test_bit(KVM_ARCH_FLAG_ID_REGS_INITIALIZED, &kvm->arch.flags)))
		return 0;

	if (reg >= sys_reg(3, 0, 0, 1, 0) && reg <= sys_reg(3, 0, 0, 7, 7))
		return kvm->arch.id_regs[IDREG_IDX(reg)];

	return 0;
}

/* Handler to RAZ/WI sysregs */
static bool pvm_access_raz_wi(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			      const struct sys_reg_desc *r)
{
	if (!p->is_write)
		p->regval = 0;

	return true;
}

/*
 * Accessor for AArch32 feature id registers.
 *
 * The value of these registers is "unknown" according to the spec if AArch32
 * isn't supported.
 */
static bool pvm_access_id_aarch32(struct kvm_vcpu *vcpu,
				  struct sys_reg_params *p,
				  const struct sys_reg_desc *r)
{
	if (p->is_write) {
		inject_undef64(vcpu);
		return false;
	}

	return pvm_access_raz_wi(vcpu, p, r);
}

/*
 * Accessor for AArch64 feature id registers.
 *
 * If access is allowed, set the regval to the protected VM's view of the
 * register and return true.
 * Otherwise, inject an undefined exception and return false.
 */
static bool pvm_access_id_aarch64(struct kvm_vcpu *vcpu,
				  struct sys_reg_params *p,
				  const struct sys_reg_desc *r)
{
	if (p->is_write) {
		inject_undef64(vcpu);
		return false;
	}

	p->regval = read_id_reg(vcpu, r);
	return true;
}

static bool pvm_gic_read_sre(struct kvm_vcpu *vcpu,
			     struct sys_reg_params *p,
			     const struct sys_reg_desc *r)
{
	/* pVMs only support GICv3. 'nuf said. */
	if (!p->is_write)
		p->regval = ICC_SRE_EL1_DIB | ICC_SRE_EL1_DFB | ICC_SRE_EL1_SRE;

	return true;
}

/* Mark the specified system register as an AArch32 feature id register. */
#define AARCH32(REG) { SYS_DESC(REG), .access = pvm_access_id_aarch32 }

/* Mark the specified system register as an AArch64 feature id register. */
#define AARCH64(REG) { SYS_DESC(REG), .access = pvm_access_id_aarch64 }

/*
 * sys_reg_desc initialiser for architecturally unallocated cpufeature ID
 * register with encoding Op0=3, Op1=0, CRn=0, CRm=crm, Op2=op2
 * (1 <= crm < 8, 0 <= Op2 < 8).
 */
#define ID_UNALLOCATED(crm, op2) {			\
	Op0(3), Op1(0), CRn(0), CRm(crm), Op2(op2),	\
	.access = pvm_access_id_aarch64,		\
}

/* Mark the specified system register as Read-As-Zero/Write-Ignored */
#define RAZ_WI(REG) { SYS_DESC(REG), .access = pvm_access_raz_wi }

/* Mark the specified system register as not being handled in hyp. */
#define HOST_HANDLED(REG) { SYS_DESC(REG), .access = NULL }

/*
 * Architected system registers.
 * Important: Must be sorted ascending by Op0, Op1, CRn, CRm, Op2
 *
 * NOTE: Anything not explicitly listed here is *restricted by default*, i.e.,
 * it will lead to injecting an exception into the guest.
 */
static const struct sys_reg_desc pvm_sys_reg_descs[] = {
	/* Cache maintenance by set/way operations are restricted. */

	/* Debug and Trace Registers are restricted. */

	/* AArch64 mappings of the AArch32 ID registers */
	/* CRm=1 */
	AARCH32(SYS_ID_PFR0_EL1),
	AARCH32(SYS_ID_PFR1_EL1),
	AARCH32(SYS_ID_DFR0_EL1),
	AARCH32(SYS_ID_AFR0_EL1),
	AARCH32(SYS_ID_MMFR0_EL1),
	AARCH32(SYS_ID_MMFR1_EL1),
	AARCH32(SYS_ID_MMFR2_EL1),
	AARCH32(SYS_ID_MMFR3_EL1),

	/* CRm=2 */
	AARCH32(SYS_ID_ISAR0_EL1),
	AARCH32(SYS_ID_ISAR1_EL1),
	AARCH32(SYS_ID_ISAR2_EL1),
	AARCH32(SYS_ID_ISAR3_EL1),
	AARCH32(SYS_ID_ISAR4_EL1),
	AARCH32(SYS_ID_ISAR5_EL1),
	AARCH32(SYS_ID_MMFR4_EL1),
	AARCH32(SYS_ID_ISAR6_EL1),

	/* CRm=3 */
	AARCH32(SYS_MVFR0_EL1),
	AARCH32(SYS_MVFR1_EL1),
	AARCH32(SYS_MVFR2_EL1),
	ID_UNALLOCATED(3,3),
	AARCH32(SYS_ID_PFR2_EL1),
	AARCH32(SYS_ID_DFR1_EL1),
	AARCH32(SYS_ID_MMFR5_EL1),
	ID_UNALLOCATED(3,7),

	/* AArch64 ID registers */
	/* CRm=4 */
	AARCH64(SYS_ID_AA64PFR0_EL1),
	AARCH64(SYS_ID_AA64PFR1_EL1),
	ID_UNALLOCATED(4,2),
	ID_UNALLOCATED(4,3),
	AARCH64(SYS_ID_AA64ZFR0_EL1),
	ID_UNALLOCATED(4,5),
	ID_UNALLOCATED(4,6),
	ID_UNALLOCATED(4,7),
	AARCH64(SYS_ID_AA64DFR0_EL1),
	AARCH64(SYS_ID_AA64DFR1_EL1),
	ID_UNALLOCATED(5,2),
	ID_UNALLOCATED(5,3),
	AARCH64(SYS_ID_AA64AFR0_EL1),
	AARCH64(SYS_ID_AA64AFR1_EL1),
	ID_UNALLOCATED(5,6),
	ID_UNALLOCATED(5,7),
	AARCH64(SYS_ID_AA64ISAR0_EL1),
	AARCH64(SYS_ID_AA64ISAR1_EL1),
	AARCH64(SYS_ID_AA64ISAR2_EL1),
	ID_UNALLOCATED(6,3),
	ID_UNALLOCATED(6,4),
	ID_UNALLOCATED(6,5),
	ID_UNALLOCATED(6,6),
	ID_UNALLOCATED(6,7),
	AARCH64(SYS_ID_AA64MMFR0_EL1),
	AARCH64(SYS_ID_AA64MMFR1_EL1),
	AARCH64(SYS_ID_AA64MMFR2_EL1),
	ID_UNALLOCATED(7,3),
	ID_UNALLOCATED(7,4),
	ID_UNALLOCATED(7,5),
	ID_UNALLOCATED(7,6),
	ID_UNALLOCATED(7,7),

	/* Scalable Vector Registers are restricted. */

	RAZ_WI(SYS_ERRIDR_EL1),
	RAZ_WI(SYS_ERRSELR_EL1),
	RAZ_WI(SYS_ERXFR_EL1),
	RAZ_WI(SYS_ERXCTLR_EL1),
	RAZ_WI(SYS_ERXSTATUS_EL1),
	RAZ_WI(SYS_ERXADDR_EL1),
	RAZ_WI(SYS_ERXMISC0_EL1),
	RAZ_WI(SYS_ERXMISC1_EL1),

	/* Performance Monitoring Registers are restricted. */

	/* Limited Ordering Regions Registers are restricted. */

	HOST_HANDLED(SYS_ICC_SGI1R_EL1),
	HOST_HANDLED(SYS_ICC_ASGI1R_EL1),
	HOST_HANDLED(SYS_ICC_SGI0R_EL1),
	{ SYS_DESC(SYS_ICC_SRE_EL1), .access = pvm_gic_read_sre, },

	HOST_HANDLED(SYS_CCSIDR_EL1),
	HOST_HANDLED(SYS_CLIDR_EL1),
	HOST_HANDLED(SYS_CSSELR_EL1),
	HOST_HANDLED(SYS_CTR_EL0),

	/* Performance Monitoring Registers are restricted. */

	/* Activity Monitoring Registers are restricted. */

	HOST_HANDLED(SYS_CNTP_TVAL_EL0),
	HOST_HANDLED(SYS_CNTP_CTL_EL0),
	HOST_HANDLED(SYS_CNTP_CVAL_EL0),

	/* Performance Monitoring Registers are restricted. */
};

/*
 * Initializes feature registers for protected vms.
 */
void kvm_init_pvm_id_regs(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_arch *ka = &kvm->arch;
	u32 r;

	hyp_assert_lock_held(&vm_table_lock);

	if (test_bit(KVM_ARCH_FLAG_ID_REGS_INITIALIZED, &kvm->arch.flags))
		return;

	/*
	 * Initialize only AArch64 id registers since AArch32 isn't supported
	 * for protected VMs.
	 */
	for (r = sys_reg(3, 0, 0, 4, 0); r <= sys_reg(3, 0, 0, 7, 7); r += sys_reg(0, 0, 0, 0, 1))
		ka->id_regs[IDREG_IDX(r)] = pvm_calc_id_reg(vcpu, r);

	set_bit(KVM_ARCH_FLAG_ID_REGS_INITIALIZED, &kvm->arch.flags);
}

/*
 * Checks that the sysreg table is unique and in-order.
 *
 * Returns 0 if the table is consistent, or 1 otherwise.
 */
int kvm_check_pvm_sysreg_table(void)
{
	unsigned int i;

	for (i = 1; i < ARRAY_SIZE(pvm_sys_reg_descs); i++) {
		if (cmp_sys_reg(&pvm_sys_reg_descs[i-1], &pvm_sys_reg_descs[i]) >= 0)
			return 1;
	}

	return 0;
}

/*
 * Handler for protected VM MSR, MRS or System instruction execution.
 *
 * Returns true if the hypervisor has handled the exit, and control should go
 * back to the guest, or false if it hasn't, to be handled by the host.
 */
bool kvm_handle_pvm_sysreg(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	const struct sys_reg_desc *r;
	struct sys_reg_params params;
	unsigned long esr = kvm_vcpu_get_esr(vcpu);
	int Rt = kvm_vcpu_sys_get_rt(vcpu);

	params = esr_sys64_to_params(esr);
	params.regval = vcpu_get_reg(vcpu, Rt);

	r = find_reg(&params, pvm_sys_reg_descs, ARRAY_SIZE(pvm_sys_reg_descs));

	/* Undefined (RESTRICTED). */
	if (r == NULL) {
		inject_undef64(vcpu);
		return true;
	}

	/* Handled by the host (HOST_HANDLED) */
	if (r->access == NULL)
		return false;

	/* Handled by hyp: skip instruction if instructed to do so. */
	if (r->access(vcpu, &params, r))
		__kvm_skip_instr(vcpu);

	if (!params.is_write)
		vcpu_set_reg(vcpu, Rt, params.regval);

	return true;
}

/*
 * Handler for protected VM restricted exceptions.
 *
 * Inject an undefined exception into the guest and return true to indicate that
 * the hypervisor has handled the exit, and control should go back to the guest.
 */
bool kvm_handle_pvm_restricted(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	inject_undef64(vcpu);
	return true;
}
