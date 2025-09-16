// SPDX-License-Identifier: GPL-2.0-only
/*
 * VGIC system registers handling functions for AArch64 mode
 */

#include <linux/irqchip/arm-gic-v3.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <asm/kvm_emulate.h>
#include "vgic/vgic.h"
#include "sys_regs.h"

static int set_gic_ctlr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			u64 val)
{
	u32 host_pri_bits, host_id_bits, host_seis, host_a3v, seis, a3v;
	struct vgic_cpu *vgic_v3_cpu = &vcpu->arch.vgic_cpu;
	struct vgic_vmcr vmcr;

	vgic_get_vmcr(vcpu, &vmcr);

	/*
	 * Disallow restoring VM state if not supported by this
	 * hardware.
	 */
	host_pri_bits = FIELD_GET(ICC_CTLR_EL1_PRI_BITS_MASK, val) + 1;
	if (host_pri_bits > vgic_v3_cpu->num_pri_bits)
		return -EINVAL;

	vgic_v3_cpu->num_pri_bits = host_pri_bits;

	host_id_bits = FIELD_GET(ICC_CTLR_EL1_ID_BITS_MASK, val);
	if (host_id_bits > vgic_v3_cpu->num_id_bits)
		return -EINVAL;

	vgic_v3_cpu->num_id_bits = host_id_bits;

	host_seis = FIELD_GET(ICH_VTR_EL2_SEIS, kvm_vgic_global_state.ich_vtr_el2);
	seis = FIELD_GET(ICC_CTLR_EL1_SEIS_MASK, val);
	if (host_seis != seis)
		return -EINVAL;

	host_a3v = FIELD_GET(ICH_VTR_EL2_A3V, kvm_vgic_global_state.ich_vtr_el2);
	a3v = FIELD_GET(ICC_CTLR_EL1_A3V_MASK, val);
	if (host_a3v != a3v)
		return -EINVAL;

	/*
	 * Here set VMCR.CTLR in ICC_CTLR_EL1 layout.
	 * The vgic_set_vmcr() will convert to ICH_VMCR layout.
	 */
	vmcr.cbpr = FIELD_GET(ICC_CTLR_EL1_CBPR_MASK, val);
	vmcr.eoim = FIELD_GET(ICC_CTLR_EL1_EOImode_MASK, val);
	vgic_set_vmcr(vcpu, &vmcr);

	return 0;
}

static int get_gic_ctlr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			u64 *valp)
{
	struct vgic_cpu *vgic_v3_cpu = &vcpu->arch.vgic_cpu;
	struct vgic_vmcr vmcr;
	u64 val;

	vgic_get_vmcr(vcpu, &vmcr);
	val = 0;
	val |= FIELD_PREP(ICC_CTLR_EL1_PRI_BITS_MASK, vgic_v3_cpu->num_pri_bits - 1);
	val |= FIELD_PREP(ICC_CTLR_EL1_ID_BITS_MASK, vgic_v3_cpu->num_id_bits);
	val |= FIELD_PREP(ICC_CTLR_EL1_SEIS_MASK,
			  FIELD_GET(ICH_VTR_EL2_SEIS,
				    kvm_vgic_global_state.ich_vtr_el2));
	val |= FIELD_PREP(ICC_CTLR_EL1_A3V_MASK,
			  FIELD_GET(ICH_VTR_EL2_A3V, kvm_vgic_global_state.ich_vtr_el2));
	/*
	 * The VMCR.CTLR value is in ICC_CTLR_EL1 layout.
	 * Extract it directly using ICC_CTLR_EL1 reg definitions.
	 */
	val |= FIELD_PREP(ICC_CTLR_EL1_CBPR_MASK, vmcr.cbpr);
	val |= FIELD_PREP(ICC_CTLR_EL1_EOImode_MASK, vmcr.eoim);

	*valp = val;

	return 0;
}

static int set_gic_pmr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
		       u64 val)
{
	struct vgic_vmcr vmcr;

	vgic_get_vmcr(vcpu, &vmcr);
	vmcr.pmr = FIELD_GET(ICC_PMR_EL1_MASK, val);
	vgic_set_vmcr(vcpu, &vmcr);

	return 0;
}

static int get_gic_pmr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
		       u64 *val)
{
	struct vgic_vmcr vmcr;

	vgic_get_vmcr(vcpu, &vmcr);
	*val = FIELD_PREP(ICC_PMR_EL1_MASK, vmcr.pmr);

	return 0;
}

static int set_gic_bpr0(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			u64 val)
{
	struct vgic_vmcr vmcr;

	vgic_get_vmcr(vcpu, &vmcr);
	vmcr.bpr = FIELD_GET(ICC_BPR0_EL1_MASK, val);
	vgic_set_vmcr(vcpu, &vmcr);

	return 0;
}

static int get_gic_bpr0(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			u64 *val)
{
	struct vgic_vmcr vmcr;

	vgic_get_vmcr(vcpu, &vmcr);
	*val = FIELD_PREP(ICC_BPR0_EL1_MASK, vmcr.bpr);

	return 0;
}

static int set_gic_bpr1(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			u64 val)
{
	struct vgic_vmcr vmcr;

	vgic_get_vmcr(vcpu, &vmcr);
	if (!vmcr.cbpr) {
		vmcr.abpr = FIELD_GET(ICC_BPR1_EL1_MASK, val);
		vgic_set_vmcr(vcpu, &vmcr);
	}

	return 0;
}

static int get_gic_bpr1(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			u64 *val)
{
	struct vgic_vmcr vmcr;

	vgic_get_vmcr(vcpu, &vmcr);
	if (!vmcr.cbpr)
		*val = FIELD_PREP(ICC_BPR1_EL1_MASK, vmcr.abpr);
	else
		*val = min((vmcr.bpr + 1), 7U);


	return 0;
}

static int set_gic_grpen0(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			  u64 val)
{
	struct vgic_vmcr vmcr;

	vgic_get_vmcr(vcpu, &vmcr);
	vmcr.grpen0 = FIELD_GET(ICC_IGRPEN0_EL1_MASK, val);
	vgic_set_vmcr(vcpu, &vmcr);

	return 0;
}

static int get_gic_grpen0(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			  u64 *val)
{
	struct vgic_vmcr vmcr;

	vgic_get_vmcr(vcpu, &vmcr);
	*val = FIELD_PREP(ICC_IGRPEN0_EL1_MASK, vmcr.grpen0);

	return 0;
}

static int set_gic_grpen1(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			  u64 val)
{
	struct vgic_vmcr vmcr;

	vgic_get_vmcr(vcpu, &vmcr);
	vmcr.grpen1 = FIELD_GET(ICC_IGRPEN1_EL1_MASK, val);
	vgic_set_vmcr(vcpu, &vmcr);

	return 0;
}

static int get_gic_grpen1(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			  u64 *val)
{
	struct vgic_vmcr vmcr;

	vgic_get_vmcr(vcpu, &vmcr);
	*val = FIELD_GET(ICC_IGRPEN1_EL1_MASK, vmcr.grpen1);

	return 0;
}

static void set_apr_reg(struct kvm_vcpu *vcpu, u64 val, u8 apr, u8 idx)
{
	struct vgic_v3_cpu_if *vgicv3 = &vcpu->arch.vgic_cpu.vgic_v3;

	if (apr)
		vgicv3->vgic_ap1r[idx] = val;
	else
		vgicv3->vgic_ap0r[idx] = val;
}

static u64 get_apr_reg(struct kvm_vcpu *vcpu, u8 apr, u8 idx)
{
	struct vgic_v3_cpu_if *vgicv3 = &vcpu->arch.vgic_cpu.vgic_v3;

	if (apr)
		return vgicv3->vgic_ap1r[idx];
	else
		return vgicv3->vgic_ap0r[idx];
}

static int set_gic_ap0r(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			u64 val)

{
	u8 idx = r->Op2 & 3;

	if (idx > vgic_v3_max_apr_idx(vcpu))
		return -EINVAL;

	set_apr_reg(vcpu, val, 0, idx);
	return 0;
}

static int get_gic_ap0r(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			u64 *val)
{
	u8 idx = r->Op2 & 3;

	if (idx > vgic_v3_max_apr_idx(vcpu))
		return -EINVAL;

	*val = get_apr_reg(vcpu, 0, idx);

	return 0;
}

static int set_gic_ap1r(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			u64 val)

{
	u8 idx = r->Op2 & 3;

	if (idx > vgic_v3_max_apr_idx(vcpu))
		return -EINVAL;

	set_apr_reg(vcpu, val, 1, idx);
	return 0;
}

static int get_gic_ap1r(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			u64 *val)
{
	u8 idx = r->Op2 & 3;

	if (idx > vgic_v3_max_apr_idx(vcpu))
		return -EINVAL;

	*val = get_apr_reg(vcpu, 1, idx);

	return 0;
}

static int set_gic_sre(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
		       u64 val)
{
	/* Validate SRE bit */
	if (!(val & ICC_SRE_EL1_SRE))
		return -EINVAL;

	return 0;
}

static int get_gic_sre(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
		       u64 *val)
{
	struct vgic_v3_cpu_if *vgicv3 = &vcpu->arch.vgic_cpu.vgic_v3;

	*val = vgicv3->vgic_sre;

	return 0;
}

static int set_gic_ich_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			   u64 val)
{
	__vcpu_assign_sys_reg(vcpu, r->reg, val);
	return 0;
}

static int get_gic_ich_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			    u64 *val)
{
	*val = __vcpu_sys_reg(vcpu, r->reg);
	return 0;
}

static int set_gic_ich_apr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			   u64 val)
{
	u8 idx = r->Op2 & 3;

	if (idx > vgic_v3_max_apr_idx(vcpu))
		return -EINVAL;

	return set_gic_ich_reg(vcpu, r, val);
}

static int get_gic_ich_apr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			    u64 *val)
{
	u8 idx = r->Op2 & 3;

	if (idx > vgic_v3_max_apr_idx(vcpu))
		return -EINVAL;

	return get_gic_ich_reg(vcpu, r, val);
}

static int set_gic_icc_sre(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			   u64 val)
{
	if (val != KVM_ICC_SRE_EL2)
		return -EINVAL;
	return 0;
}

static int get_gic_icc_sre(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			   u64 *val)
{
	*val = KVM_ICC_SRE_EL2;
	return 0;
}

static int set_gic_ich_vtr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			   u64 val)
{
	if (val != kvm_get_guest_vtr_el2())
		return -EINVAL;
	return 0;
}

static int get_gic_ich_vtr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			   u64 *val)
{
	*val = kvm_get_guest_vtr_el2();
	return 0;
}

static unsigned int el2_visibility(const struct kvm_vcpu *vcpu,
				   const struct sys_reg_desc *rd)
{
	return vcpu_has_nv(vcpu) ? 0 : REG_HIDDEN;
}

#define __EL2_REG(r, acc, i)			\
	{					\
		SYS_DESC(SYS_ ## r),		\
		.get_user = get_gic_ ## acc,	\
		.set_user = set_gic_ ## acc,	\
		.reg = i,			\
		.visibility = el2_visibility,	\
	}

#define EL2_REG(r, acc)		__EL2_REG(r, acc, r)

#define EL2_REG_RO(r, acc)	__EL2_REG(r, acc, 0)

static const struct sys_reg_desc gic_v3_icc_reg_descs[] = {
	{ SYS_DESC(SYS_ICC_PMR_EL1),
	  .set_user = set_gic_pmr, .get_user = get_gic_pmr, },
	{ SYS_DESC(SYS_ICC_BPR0_EL1),
	  .set_user = set_gic_bpr0, .get_user = get_gic_bpr0, },
	{ SYS_DESC(SYS_ICC_AP0R0_EL1),
	  .set_user = set_gic_ap0r, .get_user = get_gic_ap0r, },
	{ SYS_DESC(SYS_ICC_AP0R1_EL1),
	  .set_user = set_gic_ap0r, .get_user = get_gic_ap0r, },
	{ SYS_DESC(SYS_ICC_AP0R2_EL1),
	  .set_user = set_gic_ap0r, .get_user = get_gic_ap0r, },
	{ SYS_DESC(SYS_ICC_AP0R3_EL1),
	  .set_user = set_gic_ap0r, .get_user = get_gic_ap0r, },
	{ SYS_DESC(SYS_ICC_AP1R0_EL1),
	  .set_user = set_gic_ap1r, .get_user = get_gic_ap1r, },
	{ SYS_DESC(SYS_ICC_AP1R1_EL1),
	  .set_user = set_gic_ap1r, .get_user = get_gic_ap1r, },
	{ SYS_DESC(SYS_ICC_AP1R2_EL1),
	  .set_user = set_gic_ap1r, .get_user = get_gic_ap1r, },
	{ SYS_DESC(SYS_ICC_AP1R3_EL1),
	  .set_user = set_gic_ap1r, .get_user = get_gic_ap1r, },
	{ SYS_DESC(SYS_ICC_BPR1_EL1),
	  .set_user = set_gic_bpr1, .get_user = get_gic_bpr1, },
	{ SYS_DESC(SYS_ICC_CTLR_EL1),
	  .set_user = set_gic_ctlr, .get_user = get_gic_ctlr, },
	{ SYS_DESC(SYS_ICC_SRE_EL1),
	  .set_user = set_gic_sre, .get_user = get_gic_sre, },
	{ SYS_DESC(SYS_ICC_IGRPEN0_EL1),
	  .set_user = set_gic_grpen0, .get_user = get_gic_grpen0, },
	{ SYS_DESC(SYS_ICC_IGRPEN1_EL1),
	  .set_user = set_gic_grpen1, .get_user = get_gic_grpen1, },
	EL2_REG(ICH_AP0R0_EL2, ich_apr),
	EL2_REG(ICH_AP0R1_EL2, ich_apr),
	EL2_REG(ICH_AP0R2_EL2, ich_apr),
	EL2_REG(ICH_AP0R3_EL2, ich_apr),
	EL2_REG(ICH_AP1R0_EL2, ich_apr),
	EL2_REG(ICH_AP1R1_EL2, ich_apr),
	EL2_REG(ICH_AP1R2_EL2, ich_apr),
	EL2_REG(ICH_AP1R3_EL2, ich_apr),
	EL2_REG_RO(ICC_SRE_EL2, icc_sre),
	EL2_REG(ICH_HCR_EL2, ich_reg),
	EL2_REG_RO(ICH_VTR_EL2, ich_vtr),
	EL2_REG(ICH_VMCR_EL2, ich_reg),
	EL2_REG(ICH_LR0_EL2, ich_reg),
	EL2_REG(ICH_LR1_EL2, ich_reg),
	EL2_REG(ICH_LR2_EL2, ich_reg),
	EL2_REG(ICH_LR3_EL2, ich_reg),
	EL2_REG(ICH_LR4_EL2, ich_reg),
	EL2_REG(ICH_LR5_EL2, ich_reg),
	EL2_REG(ICH_LR6_EL2, ich_reg),
	EL2_REG(ICH_LR7_EL2, ich_reg),
	EL2_REG(ICH_LR8_EL2, ich_reg),
	EL2_REG(ICH_LR9_EL2, ich_reg),
	EL2_REG(ICH_LR10_EL2, ich_reg),
	EL2_REG(ICH_LR11_EL2, ich_reg),
	EL2_REG(ICH_LR12_EL2, ich_reg),
	EL2_REG(ICH_LR13_EL2, ich_reg),
	EL2_REG(ICH_LR14_EL2, ich_reg),
	EL2_REG(ICH_LR15_EL2, ich_reg),
};

const struct sys_reg_desc *vgic_v3_get_sysreg_table(unsigned int *sz)
{
	*sz = ARRAY_SIZE(gic_v3_icc_reg_descs);
	return gic_v3_icc_reg_descs;
}

static u64 attr_to_id(u64 attr)
{
	return ARM64_SYS_REG(FIELD_GET(KVM_REG_ARM_VGIC_SYSREG_OP0_MASK, attr),
			     FIELD_GET(KVM_REG_ARM_VGIC_SYSREG_OP1_MASK, attr),
			     FIELD_GET(KVM_REG_ARM_VGIC_SYSREG_CRN_MASK, attr),
			     FIELD_GET(KVM_REG_ARM_VGIC_SYSREG_CRM_MASK, attr),
			     FIELD_GET(KVM_REG_ARM_VGIC_SYSREG_OP2_MASK, attr));
}

int vgic_v3_has_cpu_sysregs_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	const struct sys_reg_desc *r;

	r = get_reg_by_id(attr_to_id(attr->attr), gic_v3_icc_reg_descs,
			  ARRAY_SIZE(gic_v3_icc_reg_descs));

	if (r && !sysreg_hidden(vcpu, r))
		return 0;

	return -ENXIO;
}

int vgic_v3_cpu_sysregs_uaccess(struct kvm_vcpu *vcpu,
				struct kvm_device_attr *attr,
				bool is_write)
{
	struct kvm_one_reg reg = {
		.id	= attr_to_id(attr->attr),
		.addr	= attr->addr,
	};

	if (is_write)
		return kvm_sys_reg_set_user(vcpu, &reg, gic_v3_icc_reg_descs,
					    ARRAY_SIZE(gic_v3_icc_reg_descs));
	else
		return kvm_sys_reg_get_user(vcpu, &reg, gic_v3_icc_reg_descs,
					    ARRAY_SIZE(gic_v3_icc_reg_descs));
}
