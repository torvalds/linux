// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Google LLC
 * Author: Fuad Tabba <tabba@google.com>
 */

#include <linux/irqchip/arm-gic-v3.h>

#include <asm/kvm_asm.h>
#include <asm/kvm_mmu.h>

#include <hyp/adjust_pc.h>

#include <nvhe/fixed_config.h>

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

/*
 * Inject an unknown/undefined exception to an AArch64 guest while most of its
 * sysregs are live.
 */
static void inject_undef64(struct kvm_vcpu *vcpu)
{
	u64 esr = (ESR_ELx_EC_UNKNOWN << ESR_ELx_EC_SHIFT);

	*vcpu_pc(vcpu) = read_sysreg_el2(SYS_ELR);
	*vcpu_cpsr(vcpu) = read_sysreg_el2(SYS_SPSR);

	vcpu->arch.flags |= (KVM_ARM64_EXCEPT_AA64_EL1 |
			     KVM_ARM64_EXCEPT_AA64_ELx_SYNC |
			     KVM_ARM64_PENDING_EXCEPTION);

	__kvm_adjust_pc(vcpu);

	write_sysreg_el1(esr, SYS_ESR);
	write_sysreg_el1(read_sysreg_el2(SYS_ELR), SYS_ELR);
	write_sysreg_el2(*vcpu_pc(vcpu), SYS_ELR);
	write_sysreg_el2(*vcpu_cpsr(vcpu), SYS_SPSR);
}

/*
 * Returns the restricted features values of the feature register based on the
 * limitations in restrict_fields.
 * A feature id field value of 0b0000 does not impose any restrictions.
 * Note: Use only for unsigned feature field values.
 */
static u64 get_restricted_features_unsigned(u64 sys_reg_val,
					    u64 restrict_fields)
{
	u64 value = 0UL;
	u64 mask = GENMASK_ULL(ARM64_FEATURE_FIELD_BITS - 1, 0);

	/*
	 * According to the Arm Architecture Reference Manual, feature fields
	 * use increasing values to indicate increases in functionality.
	 * Iterate over the restricted feature fields and calculate the minimum
	 * unsigned value between the one supported by the system, and what the
	 * value is being restricted to.
	 */
	while (sys_reg_val && restrict_fields) {
		value |= min(sys_reg_val & mask, restrict_fields & mask);
		sys_reg_val &= ~mask;
		restrict_fields &= ~mask;
		mask <<= ARM64_FEATURE_FIELD_BITS;
	}

	return value;
}

/*
 * Functions that return the value of feature id registers for protected VMs
 * based on allowed features, system features, and KVM support.
 */

static u64 get_pvm_id_aa64pfr0(const struct kvm_vcpu *vcpu)
{
	const struct kvm *kvm = (const struct kvm *)kern_hyp_va(vcpu->kvm);
	u64 set_mask = 0;
	u64 allow_mask = PVM_ID_AA64PFR0_ALLOW;

	set_mask |= get_restricted_features_unsigned(id_aa64pfr0_el1_sys_val,
		PVM_ID_AA64PFR0_RESTRICT_UNSIGNED);

	/* Spectre and Meltdown mitigation in KVM */
	set_mask |= FIELD_PREP(ARM64_FEATURE_MASK(ID_AA64PFR0_CSV2),
			       (u64)kvm->arch.pfr0_csv2);
	set_mask |= FIELD_PREP(ARM64_FEATURE_MASK(ID_AA64PFR0_CSV3),
			       (u64)kvm->arch.pfr0_csv3);

	return (id_aa64pfr0_el1_sys_val & allow_mask) | set_mask;
}

static u64 get_pvm_id_aa64pfr1(const struct kvm_vcpu *vcpu)
{
	const struct kvm *kvm = (const struct kvm *)kern_hyp_va(vcpu->kvm);
	u64 allow_mask = PVM_ID_AA64PFR1_ALLOW;

	if (!kvm_has_mte(kvm))
		allow_mask &= ~ARM64_FEATURE_MASK(ID_AA64PFR1_MTE);

	return id_aa64pfr1_el1_sys_val & allow_mask;
}

static u64 get_pvm_id_aa64zfr0(const struct kvm_vcpu *vcpu)
{
	/*
	 * No support for Scalable Vectors, therefore, hyp has no sanitized
	 * copy of the feature id register.
	 */
	BUILD_BUG_ON(PVM_ID_AA64ZFR0_ALLOW != 0ULL);
	return 0;
}

static u64 get_pvm_id_aa64dfr0(const struct kvm_vcpu *vcpu)
{
	/*
	 * No support for debug, including breakpoints, and watchpoints,
	 * therefore, pKVM has no sanitized copy of the feature id register.
	 */
	BUILD_BUG_ON(PVM_ID_AA64DFR0_ALLOW != 0ULL);
	return 0;
}

static u64 get_pvm_id_aa64dfr1(const struct kvm_vcpu *vcpu)
{
	/*
	 * No support for debug, therefore, hyp has no sanitized copy of the
	 * feature id register.
	 */
	BUILD_BUG_ON(PVM_ID_AA64DFR1_ALLOW != 0ULL);
	return 0;
}

static u64 get_pvm_id_aa64afr0(const struct kvm_vcpu *vcpu)
{
	/*
	 * No support for implementation defined features, therefore, hyp has no
	 * sanitized copy of the feature id register.
	 */
	BUILD_BUG_ON(PVM_ID_AA64AFR0_ALLOW != 0ULL);
	return 0;
}

static u64 get_pvm_id_aa64afr1(const struct kvm_vcpu *vcpu)
{
	/*
	 * No support for implementation defined features, therefore, hyp has no
	 * sanitized copy of the feature id register.
	 */
	BUILD_BUG_ON(PVM_ID_AA64AFR1_ALLOW != 0ULL);
	return 0;
}

static u64 get_pvm_id_aa64isar0(const struct kvm_vcpu *vcpu)
{
	return id_aa64isar0_el1_sys_val & PVM_ID_AA64ISAR0_ALLOW;
}

static u64 get_pvm_id_aa64isar1(const struct kvm_vcpu *vcpu)
{
	u64 allow_mask = PVM_ID_AA64ISAR1_ALLOW;

	if (!vcpu_has_ptrauth(vcpu))
		allow_mask &= ~(ARM64_FEATURE_MASK(ID_AA64ISAR1_APA) |
				ARM64_FEATURE_MASK(ID_AA64ISAR1_API) |
				ARM64_FEATURE_MASK(ID_AA64ISAR1_GPA) |
				ARM64_FEATURE_MASK(ID_AA64ISAR1_GPI));

	return id_aa64isar1_el1_sys_val & allow_mask;
}

static u64 get_pvm_id_aa64isar2(const struct kvm_vcpu *vcpu)
{
	u64 allow_mask = PVM_ID_AA64ISAR2_ALLOW;

	if (!vcpu_has_ptrauth(vcpu))
		allow_mask &= ~(ARM64_FEATURE_MASK(ID_AA64ISAR2_APA3) |
				ARM64_FEATURE_MASK(ID_AA64ISAR2_GPA3));

	return id_aa64isar2_el1_sys_val & allow_mask;
}

static u64 get_pvm_id_aa64mmfr0(const struct kvm_vcpu *vcpu)
{
	u64 set_mask;

	set_mask = get_restricted_features_unsigned(id_aa64mmfr0_el1_sys_val,
		PVM_ID_AA64MMFR0_RESTRICT_UNSIGNED);

	return (id_aa64mmfr0_el1_sys_val & PVM_ID_AA64MMFR0_ALLOW) | set_mask;
}

static u64 get_pvm_id_aa64mmfr1(const struct kvm_vcpu *vcpu)
{
	return id_aa64mmfr1_el1_sys_val & PVM_ID_AA64MMFR1_ALLOW;
}

static u64 get_pvm_id_aa64mmfr2(const struct kvm_vcpu *vcpu)
{
	return id_aa64mmfr2_el1_sys_val & PVM_ID_AA64MMFR2_ALLOW;
}

/* Read a sanitized cpufeature ID register by its encoding */
u64 pvm_read_id_reg(const struct kvm_vcpu *vcpu, u32 id)
{
	switch (id) {
	case SYS_ID_AA64PFR0_EL1:
		return get_pvm_id_aa64pfr0(vcpu);
	case SYS_ID_AA64PFR1_EL1:
		return get_pvm_id_aa64pfr1(vcpu);
	case SYS_ID_AA64ZFR0_EL1:
		return get_pvm_id_aa64zfr0(vcpu);
	case SYS_ID_AA64DFR0_EL1:
		return get_pvm_id_aa64dfr0(vcpu);
	case SYS_ID_AA64DFR1_EL1:
		return get_pvm_id_aa64dfr1(vcpu);
	case SYS_ID_AA64AFR0_EL1:
		return get_pvm_id_aa64afr0(vcpu);
	case SYS_ID_AA64AFR1_EL1:
		return get_pvm_id_aa64afr1(vcpu);
	case SYS_ID_AA64ISAR0_EL1:
		return get_pvm_id_aa64isar0(vcpu);
	case SYS_ID_AA64ISAR1_EL1:
		return get_pvm_id_aa64isar1(vcpu);
	case SYS_ID_AA64ISAR2_EL1:
		return get_pvm_id_aa64isar2(vcpu);
	case SYS_ID_AA64MMFR0_EL1:
		return get_pvm_id_aa64mmfr0(vcpu);
	case SYS_ID_AA64MMFR1_EL1:
		return get_pvm_id_aa64mmfr1(vcpu);
	case SYS_ID_AA64MMFR2_EL1:
		return get_pvm_id_aa64mmfr2(vcpu);
	default:
		/* Unhandled ID register, RAZ */
		return 0;
	}
}

static u64 read_id_reg(const struct kvm_vcpu *vcpu,
		       struct sys_reg_desc const *r)
{
	return pvm_read_id_reg(vcpu, reg_to_encoding(r));
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

	/*
	 * No support for AArch32 guests, therefore, pKVM has no sanitized copy
	 * of AArch32 feature id registers.
	 */
	BUILD_BUG_ON(FIELD_GET(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1),
		     PVM_ID_AA64PFR0_RESTRICT_UNSIGNED) > ID_AA64PFR0_ELx_64BIT_ONLY);

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
