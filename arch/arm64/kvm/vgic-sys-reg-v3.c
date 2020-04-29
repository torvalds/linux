// SPDX-License-Identifier: GPL-2.0-only
/*
 * VGIC system registers handling functions for AArch64 mode
 */

#include <linux/irqchip/arm-gic-v3.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <asm/kvm_emulate.h>
#include "vgic.h"
#include "sys_regs.h"

static bool access_gic_ctlr(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			    const struct sys_reg_desc *r)
{
	u32 host_pri_bits, host_id_bits, host_seis, host_a3v, seis, a3v;
	struct vgic_cpu *vgic_v3_cpu = &vcpu->arch.vgic_cpu;
	struct vgic_vmcr vmcr;
	u64 val;

	vgic_get_vmcr(vcpu, &vmcr);
	if (p->is_write) {
		val = p->regval;

		/*
		 * Disallow restoring VM state if not supported by this
		 * hardware.
		 */
		host_pri_bits = ((val & ICC_CTLR_EL1_PRI_BITS_MASK) >>
				 ICC_CTLR_EL1_PRI_BITS_SHIFT) + 1;
		if (host_pri_bits > vgic_v3_cpu->num_pri_bits)
			return false;

		vgic_v3_cpu->num_pri_bits = host_pri_bits;

		host_id_bits = (val & ICC_CTLR_EL1_ID_BITS_MASK) >>
				ICC_CTLR_EL1_ID_BITS_SHIFT;
		if (host_id_bits > vgic_v3_cpu->num_id_bits)
			return false;

		vgic_v3_cpu->num_id_bits = host_id_bits;

		host_seis = ((kvm_vgic_global_state.ich_vtr_el2 &
			     ICH_VTR_SEIS_MASK) >> ICH_VTR_SEIS_SHIFT);
		seis = (val & ICC_CTLR_EL1_SEIS_MASK) >>
			ICC_CTLR_EL1_SEIS_SHIFT;
		if (host_seis != seis)
			return false;

		host_a3v = ((kvm_vgic_global_state.ich_vtr_el2 &
			    ICH_VTR_A3V_MASK) >> ICH_VTR_A3V_SHIFT);
		a3v = (val & ICC_CTLR_EL1_A3V_MASK) >> ICC_CTLR_EL1_A3V_SHIFT;
		if (host_a3v != a3v)
			return false;

		/*
		 * Here set VMCR.CTLR in ICC_CTLR_EL1 layout.
		 * The vgic_set_vmcr() will convert to ICH_VMCR layout.
		 */
		vmcr.cbpr = (val & ICC_CTLR_EL1_CBPR_MASK) >> ICC_CTLR_EL1_CBPR_SHIFT;
		vmcr.eoim = (val & ICC_CTLR_EL1_EOImode_MASK) >> ICC_CTLR_EL1_EOImode_SHIFT;
		vgic_set_vmcr(vcpu, &vmcr);
	} else {
		val = 0;
		val |= (vgic_v3_cpu->num_pri_bits - 1) <<
			ICC_CTLR_EL1_PRI_BITS_SHIFT;
		val |= vgic_v3_cpu->num_id_bits << ICC_CTLR_EL1_ID_BITS_SHIFT;
		val |= ((kvm_vgic_global_state.ich_vtr_el2 &
			ICH_VTR_SEIS_MASK) >> ICH_VTR_SEIS_SHIFT) <<
			ICC_CTLR_EL1_SEIS_SHIFT;
		val |= ((kvm_vgic_global_state.ich_vtr_el2 &
			ICH_VTR_A3V_MASK) >> ICH_VTR_A3V_SHIFT) <<
			ICC_CTLR_EL1_A3V_SHIFT;
		/*
		 * The VMCR.CTLR value is in ICC_CTLR_EL1 layout.
		 * Extract it directly using ICC_CTLR_EL1 reg definitions.
		 */
		val |= (vmcr.cbpr << ICC_CTLR_EL1_CBPR_SHIFT) & ICC_CTLR_EL1_CBPR_MASK;
		val |= (vmcr.eoim << ICC_CTLR_EL1_EOImode_SHIFT) & ICC_CTLR_EL1_EOImode_MASK;

		p->regval = val;
	}

	return true;
}

static bool access_gic_pmr(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	struct vgic_vmcr vmcr;

	vgic_get_vmcr(vcpu, &vmcr);
	if (p->is_write) {
		vmcr.pmr = (p->regval & ICC_PMR_EL1_MASK) >> ICC_PMR_EL1_SHIFT;
		vgic_set_vmcr(vcpu, &vmcr);
	} else {
		p->regval = (vmcr.pmr << ICC_PMR_EL1_SHIFT) & ICC_PMR_EL1_MASK;
	}

	return true;
}

static bool access_gic_bpr0(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			    const struct sys_reg_desc *r)
{
	struct vgic_vmcr vmcr;

	vgic_get_vmcr(vcpu, &vmcr);
	if (p->is_write) {
		vmcr.bpr = (p->regval & ICC_BPR0_EL1_MASK) >>
			    ICC_BPR0_EL1_SHIFT;
		vgic_set_vmcr(vcpu, &vmcr);
	} else {
		p->regval = (vmcr.bpr << ICC_BPR0_EL1_SHIFT) &
			     ICC_BPR0_EL1_MASK;
	}

	return true;
}

static bool access_gic_bpr1(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			    const struct sys_reg_desc *r)
{
	struct vgic_vmcr vmcr;

	if (!p->is_write)
		p->regval = 0;

	vgic_get_vmcr(vcpu, &vmcr);
	if (!vmcr.cbpr) {
		if (p->is_write) {
			vmcr.abpr = (p->regval & ICC_BPR1_EL1_MASK) >>
				     ICC_BPR1_EL1_SHIFT;
			vgic_set_vmcr(vcpu, &vmcr);
		} else {
			p->regval = (vmcr.abpr << ICC_BPR1_EL1_SHIFT) &
				     ICC_BPR1_EL1_MASK;
		}
	} else {
		if (!p->is_write)
			p->regval = min((vmcr.bpr + 1), 7U);
	}

	return true;
}

static bool access_gic_grpen0(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			      const struct sys_reg_desc *r)
{
	struct vgic_vmcr vmcr;

	vgic_get_vmcr(vcpu, &vmcr);
	if (p->is_write) {
		vmcr.grpen0 = (p->regval & ICC_IGRPEN0_EL1_MASK) >>
			       ICC_IGRPEN0_EL1_SHIFT;
		vgic_set_vmcr(vcpu, &vmcr);
	} else {
		p->regval = (vmcr.grpen0 << ICC_IGRPEN0_EL1_SHIFT) &
			     ICC_IGRPEN0_EL1_MASK;
	}

	return true;
}

static bool access_gic_grpen1(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			      const struct sys_reg_desc *r)
{
	struct vgic_vmcr vmcr;

	vgic_get_vmcr(vcpu, &vmcr);
	if (p->is_write) {
		vmcr.grpen1 = (p->regval & ICC_IGRPEN1_EL1_MASK) >>
			       ICC_IGRPEN1_EL1_SHIFT;
		vgic_set_vmcr(vcpu, &vmcr);
	} else {
		p->regval = (vmcr.grpen1 << ICC_IGRPEN1_EL1_SHIFT) &
			     ICC_IGRPEN1_EL1_MASK;
	}

	return true;
}

static void vgic_v3_access_apr_reg(struct kvm_vcpu *vcpu,
				   struct sys_reg_params *p, u8 apr, u8 idx)
{
	struct vgic_v3_cpu_if *vgicv3 = &vcpu->arch.vgic_cpu.vgic_v3;
	uint32_t *ap_reg;

	if (apr)
		ap_reg = &vgicv3->vgic_ap1r[idx];
	else
		ap_reg = &vgicv3->vgic_ap0r[idx];

	if (p->is_write)
		*ap_reg = p->regval;
	else
		p->regval = *ap_reg;
}

static bool access_gic_aprn(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			    const struct sys_reg_desc *r, u8 apr)
{
	u8 idx = r->Op2 & 3;

	if (idx > vgic_v3_max_apr_idx(vcpu))
		goto err;

	vgic_v3_access_apr_reg(vcpu, p, apr, idx);
	return true;
err:
	if (!p->is_write)
		p->regval = 0;

	return false;
}

static bool access_gic_ap0r(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			    const struct sys_reg_desc *r)

{
	return access_gic_aprn(vcpu, p, r, 0);
}

static bool access_gic_ap1r(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			    const struct sys_reg_desc *r)
{
	return access_gic_aprn(vcpu, p, r, 1);
}

static bool access_gic_sre(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	struct vgic_v3_cpu_if *vgicv3 = &vcpu->arch.vgic_cpu.vgic_v3;

	/* Validate SRE bit */
	if (p->is_write) {
		if (!(p->regval & ICC_SRE_EL1_SRE))
			return false;
	} else {
		p->regval = vgicv3->vgic_sre;
	}

	return true;
}
static const struct sys_reg_desc gic_v3_icc_reg_descs[] = {
	{ SYS_DESC(SYS_ICC_PMR_EL1), access_gic_pmr },
	{ SYS_DESC(SYS_ICC_BPR0_EL1), access_gic_bpr0 },
	{ SYS_DESC(SYS_ICC_AP0R0_EL1), access_gic_ap0r },
	{ SYS_DESC(SYS_ICC_AP0R1_EL1), access_gic_ap0r },
	{ SYS_DESC(SYS_ICC_AP0R2_EL1), access_gic_ap0r },
	{ SYS_DESC(SYS_ICC_AP0R3_EL1), access_gic_ap0r },
	{ SYS_DESC(SYS_ICC_AP1R0_EL1), access_gic_ap1r },
	{ SYS_DESC(SYS_ICC_AP1R1_EL1), access_gic_ap1r },
	{ SYS_DESC(SYS_ICC_AP1R2_EL1), access_gic_ap1r },
	{ SYS_DESC(SYS_ICC_AP1R3_EL1), access_gic_ap1r },
	{ SYS_DESC(SYS_ICC_BPR1_EL1), access_gic_bpr1 },
	{ SYS_DESC(SYS_ICC_CTLR_EL1), access_gic_ctlr },
	{ SYS_DESC(SYS_ICC_SRE_EL1), access_gic_sre },
	{ SYS_DESC(SYS_ICC_IGRPEN0_EL1), access_gic_grpen0 },
	{ SYS_DESC(SYS_ICC_IGRPEN1_EL1), access_gic_grpen1 },
};

int vgic_v3_has_cpu_sysregs_attr(struct kvm_vcpu *vcpu, bool is_write, u64 id,
				u64 *reg)
{
	struct sys_reg_params params;
	u64 sysreg = (id & KVM_DEV_ARM_VGIC_SYSREG_MASK) | KVM_REG_SIZE_U64;

	params.regval = *reg;
	params.is_write = is_write;
	params.is_aarch32 = false;
	params.is_32bit = false;

	if (find_reg_by_id(sysreg, &params, gic_v3_icc_reg_descs,
			      ARRAY_SIZE(gic_v3_icc_reg_descs)))
		return 0;

	return -ENXIO;
}

int vgic_v3_cpu_sysregs_uaccess(struct kvm_vcpu *vcpu, bool is_write, u64 id,
				u64 *reg)
{
	struct sys_reg_params params;
	const struct sys_reg_desc *r;
	u64 sysreg = (id & KVM_DEV_ARM_VGIC_SYSREG_MASK) | KVM_REG_SIZE_U64;

	if (is_write)
		params.regval = *reg;
	params.is_write = is_write;
	params.is_aarch32 = false;
	params.is_32bit = false;

	r = find_reg_by_id(sysreg, &params, gic_v3_icc_reg_descs,
			   ARRAY_SIZE(gic_v3_icc_reg_descs));
	if (!r)
		return -ENXIO;

	if (!r->access(vcpu, &params, r))
		return -EINVAL;

	if (!is_write)
		*reg = params.regval;

	return 0;
}
