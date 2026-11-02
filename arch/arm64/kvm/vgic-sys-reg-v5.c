// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, 2026 Arm Ltd.
 */

/*
 * VGICv5 system registers handling functions for AArch64 mode
 */

#include <linux/irqchip/arm-gic-v5.h>

#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/wordpart.h>

#include <asm/kvm_emulate.h>

#include "vgic/vgic.h"
#include "sys_regs.h"

#define ICC_PPI_PRIORITYR_PRIORITY_MASK		REPEAT_BYTE(0x1f)

static int set_gic_apr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
		       u64 val)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;

	/* The upper 32 bits are RES0 */
	cpu_if->vgic_apr = val & ~ICC_APR_EL1_RES0;

	return 0;
}

static int get_gic_apr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
		       u64 *val)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;

	*val = cpu_if->vgic_apr;

	return 0;
}

static int set_gic_cr0(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
		       u64 val)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;

	/*
	 * We only support setting the ICC_CR0_EL1.En bit, which is actually
	 * stored in the VMCR.
	 */
	FIELD_MODIFY(FEAT_GCIE_ICH_VMCR_EL2_EN, &cpu_if->vgic_vmcr,
		     FIELD_GET(ICC_CR0_EL1_EN, val));

	return 0;
}

static int get_gic_cr0(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
		       u64 *val)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;

	/*
	 * PID only applies if EL3 is present. Same applies to IPPT. Hence,
	 * those fields are always presented as 0.
	 *
	 * We always present the link as connected and idle:
	 *     (LINK = 1, LINK_IDLE = 1).
	 */
	*val = FIELD_PREP(ICC_CR0_EL1_EN,
			  FIELD_GET(FEAT_GCIE_ICH_VMCR_EL2_EN, cpu_if->vgic_vmcr));
	*val |= ICC_CR0_EL1_LINK_MASK;
	*val |= ICC_CR0_EL1_LINK_IDLE_MASK;

	return 0;
}

static int set_gic_pcr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
		       u64 val)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;

	/* Set the VPMR field in the VMCR */
	FIELD_MODIFY(FEAT_GCIE_ICH_VMCR_EL2_VPMR, &cpu_if->vgic_vmcr,
		     FIELD_GET(ICC_PCR_EL1_PRIORITY, val));

	return 0;
}

static int get_gic_pcr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
		       u64 *val)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;

	*val = FIELD_PREP(ICC_PCR_EL1_PRIORITY,
			  FIELD_GET(FEAT_GCIE_ICH_VMCR_EL2_VPMR, cpu_if->vgic_vmcr));

	return 0;
}

static int set_gic_icsr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			u64 val)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;

	cpu_if->vgic_icsr = val & ~ICC_ICSR_EL1_RES0;

	return 0;
}

static int get_gic_icsr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			u64 *val)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;

	*val = cpu_if->vgic_icsr;

	return 0;
}

/*
 * Helper macro to iterate over a range of PPIs and execute some code (to either
 * extract or set the vgic_irq state). This is used when `get`-ing the PPI
 * ENABLER, ACTIVER, PENDR and when setting the PRIORITYR state.
 *
 * vcpu: Pointer to struct kvm_vcpu (to which these PPIs belong)
 * r: The register index. 0 or 1 for all except PRIORITYR (which is 0-15)
 * nr: The number of PPIs iterated over. 64 for all but PRIORITYR (which is 8)
 * code: The code snippet to execute for each vgic_irq
 */
#define for_ppi_state(vcpu, r, nr, code)				\
	do {								\
		struct kvm_vcpu *__vcpu = (vcpu);			\
		int __r = (r);						\
		int __nr = (nr);					\
									\
		for (int i = 0; i < __nr; i++) {			\
			u32 id = vgic_v5_make_ppi(__r * __nr + i);	\
			struct vgic_irq *irq;				\
									\
			irq = vgic_get_vcpu_irq(__vcpu, id);		\
			scoped_guard(raw_spinlock_irqsave, &irq->irq_lock) { \
				code;					\
			}						\
			vgic_put_irq(__vcpu->kvm, irq);			\
		}							\
	} while (0)

static int set_gic_ppi_enabler(struct kvm_vcpu *vcpu,
			       const struct sys_reg_desc *r, u64 val)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;
	int i, start, end, reg = r->Op2 % 2;

	/*
	 * If we're only handling architected PPIs and the guest writes to the
	 * enable for the non-architected PPIs, we just return as there's
	 * nothing to do at all. We don't even allocate the storage for them in
	 * this case.
	 */
	if (VGIC_V5_NR_PRIVATE_IRQS == 64 && reg == 1)
		return 0;

	/*
	 * Merge the raw guest write into our bitmap at an offset of either 0 or
	 * 64.
	 *
	 * Note that there is *NO* masking applied - the enable state is written
	 * unfiltered. The assumption is that userspace uses this interface to
	 * set initial state before the guest runs, and then the exposed PPI
	 * mask is applied later, when vgic_v5_finalize_ppi_state() runs on
	 * first entry to each vCPU. If userspace chooses to set the enabler
	 * state later, it is fully capable of breaking the illusion we provided
	 * to the guest by exposing register state (and PPIs) to the guest that
	 * were not initially exposed. Good luck!
	 */
	bitmap_write(cpu_if->vgic_ppi_enabler, val, 64 * reg, 64);

	/*
	 * Sync the change in enable states to the vgic_irqs for the written
	 * register slice.
	 */
	start = reg * 64;
	end = min(start + 64, VGIC_V5_NR_PRIVATE_IRQS);
	for (i = start; i < end; i++) {
		u32 intid = vgic_v5_make_ppi(i);
		struct vgic_irq *irq;

		irq = vgic_get_vcpu_irq(vcpu, intid);

		scoped_guard(raw_spinlock_irqsave, &irq->irq_lock)
			irq->enabled = test_bit(i, cpu_if->vgic_ppi_enabler);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return 0;
}

static int get_gic_ppi_enabler(struct kvm_vcpu *vcpu,
			       const struct sys_reg_desc *r, u64 *val)
{
	unsigned long enabler = 0;
	int reg = r->Op2 % 2;

	/* If we only support architected PPIs, return 0 */
	if (VGIC_V5_NR_PRIVATE_IRQS == 64 && reg == 1) {
		*val = 0;
		return 0;
	}

	/* Iterate over each struct vgic_irq to build the ENABLER value. */
	for_ppi_state(vcpu, reg, 64, __assign_bit(i % 64, &enabler, irq->enabled));

	*val = enabler;

	return 0;
}

static int set_gic_ppi_activer(struct kvm_vcpu *vcpu,
			       const struct sys_reg_desc *r, u64 val)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;
	int i, start, end, reg = r->Op2 % 2;

	if (VGIC_V5_NR_PRIVATE_IRQS == 64 && reg == 1)
		return 0;

	/*
	 * Store the raw guest write. The exposed PPI mask is applied later,
	 * when vgic_v5_finalize_ppi_state() runs on first entry to each
	 * vCPU. See comment on set_gic_ppi_enabler() for details.
	 */
	bitmap_write(cpu_if->vgic_ppi_activer, val, 64 * reg, 64);

	start = reg * 64;
	end = min(start + 64, VGIC_V5_NR_PRIVATE_IRQS);
	for (i = start; i < end; i++) {
		u32 intid = vgic_v5_make_ppi(i);
		struct vgic_irq *irq;

		irq = vgic_get_vcpu_irq(vcpu, intid);

		scoped_guard(raw_spinlock_irqsave, &irq->irq_lock)
			irq->active = test_bit(i, cpu_if->vgic_ppi_activer);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return 0;
}

static int get_gic_ppi_activer(struct kvm_vcpu *vcpu,
			       const struct sys_reg_desc *r, u64 *val)
{
	unsigned long activer = 0;
	int reg = r->Op2 % 2;

	/* If we only support architected PPIs, return 0 */
	if (VGIC_V5_NR_PRIVATE_IRQS == 64 && reg == 1) {
		*val = 0;
		return 0;
	}

	/* Iterate over each struct vgic_irq to build the ACTIVER value. */
	for_ppi_state(vcpu, reg, 64, __assign_bit(i % 64, &activer, irq->active));

	*val = activer;

	return 0;
}

static int set_gic_ppi_pendr(struct kvm_vcpu *vcpu,
			     const struct sys_reg_desc *r, u64 val)
{
	int i, start, end, reg = r->Op2 % 2;

	/* If we only support architected PPIs, return */
	if (VGIC_V5_NR_PRIVATE_IRQS == 64 && reg == 1)
		return 0;

	/*
	 * Update each struct vgic_irq with the pending state, treating Level
	 * and Edge interrupts differently. The exposed PPI mask is applied
	 * later, when vgic_v5_finalize_ppi_state() runs on first entry to each
	 * vCPU. See comment on set_gic_ppi_enabler() for details.
	 */
	start = reg * 64;
	end = min(start + 64, VGIC_V5_NR_PRIVATE_IRQS);
	for (i = start; i < end; i++) {
		u32 intid = vgic_v5_make_ppi(i);
		struct vgic_irq *irq;

		irq = vgic_get_vcpu_irq(vcpu, intid);

		scoped_guard(raw_spinlock_irqsave, &irq->irq_lock) {
			bool level = !!(val & BIT_ULL(i - start));

			if (irq->config == VGIC_CONFIG_LEVEL)
				irq->line_level = level;
			else
				irq->pending_latch = level;
		}

		vgic_put_irq(vcpu->kvm, irq);
	}

	/*
	 * The pending state is generated from the vgic_irqs on each guest
	 * entry. Therefore, we don't store the raw value written anywhere in
	 * the case of userspace PPI_PENDRx_EL1 writes.
	 */

	return 0;
}

static int get_gic_ppi_pendr(struct kvm_vcpu *vcpu,
			     const struct sys_reg_desc *r, u64 *val)
{
	unsigned long pendr = 0;
	int reg = r->Op2 % 2;

	/* If we only support architected PPIs, return 0 */
	if (VGIC_V5_NR_PRIVATE_IRQS == 64 && reg == 1) {
		*val = 0;
		return 0;
	}

	/* Iterate over each struct vgic_irq to build the PENDR value. */
	for_ppi_state(vcpu, reg, 64, {
		if (irq_is_pending(irq))
			__assign_bit(i % 64, &pendr, 1);
	});

	*val = pendr;

	return 0;
}

static int set_gic_ppi_priorityr(struct kvm_vcpu *vcpu,
				 const struct sys_reg_desc *r, u64 val)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;
	int reg = ((r->CRm & 0x1) << 3) + r->Op2;

	/* If we only support architected PPIs, return */
	if (VGIC_V5_NR_PRIVATE_IRQS == 64 && reg > 7)
		return 0;

	val &= ICC_PPI_PRIORITYR_PRIORITY_MASK;

	/*
	 * Although priorities are not regularly synced back to the vgic_irq
	 * state, they are explicitly synced back here. This is to ensure that
	 * any pending PPIs are evaluated correctly when first running the guest
	 * after setting the state.
	 */
	for_ppi_state(vcpu, reg, 8,
		      irq->priority = (u8)(val >> (8 * i));
		);

	/*
	 * Update the state that will be written to the ICH_PPI_PRIORITYRx_EL2
	 * on next guest entry.
	 */
	cpu_if->vgic_ppi_priorityr[reg] = val;

	return 0;
}

static int get_gic_ppi_priorityr(struct kvm_vcpu *vcpu,
				 const struct sys_reg_desc *r, u64 *val)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;
	int reg = ((r->CRm & 0x1) << 3) + r->Op2;

	/* If we only support architected PPIs, return 0 */
	if (VGIC_V5_NR_PRIVATE_IRQS == 64 && reg > 7) {
		*val = 0;
		return 0;
	}

	/*
	 * The priorities are only synced back to the vgic_irq state when the
	 * vcpu is entering WFI (KVM only needs to know the priorities when
	 * evaluating if there are pending PPI interrupts for a vcpu). The raw
	 * register ICH_PPI_PRIORITYRx_EL1 state is simply saved and restored
	 * blindly. This state is just returned as it contains the most recent
	 * priorities written by the guest.
	 */
	*val = cpu_if->vgic_ppi_priorityr[reg];

	return 0;
}

/*
 * The following registers are NOT supported:
 *
 * - ICC_HAPR_EL1
 *	The value of this is directly generated by the GICv5 hardware based on
 *	the ICC_APR_EL1 when the guest is running.
 * - ICC_IAFFIDR_EL1
 *	The IAFFID for a GICv5 VPE is the same as the VPE ID, which is the index
 *	into the in-memory VPE Table. This is not configurable, and instead we
 *	rely on userspace recreating the VPEs in the same order prior to
 *	restoring guest state.
 * - ICC_PPI_CACTIVER<n>_EL1
 *	Only raw state writes are supported via the S(et) variant.
 * - ICC_PPI_CPENDR<n>_EL1
 *	Only raw state writes are supported via the S(et) variant.
 */
static const struct sys_reg_desc gic_v5_icc_reg_descs[] = {
	{ SYS_DESC(SYS_ICC_ICSR_EL1),
	  .set_user = set_gic_icsr, .get_user = get_gic_icsr, },
	{ SYS_DESC(SYS_ICC_PPI_ENABLER0_EL1),
	  .set_user = set_gic_ppi_enabler, .get_user = get_gic_ppi_enabler, },
	{ SYS_DESC(SYS_ICC_PPI_ENABLER1_EL1),
	  .set_user = set_gic_ppi_enabler, .get_user = get_gic_ppi_enabler, },
	/*
	 * Only ICC_SACTIVER<n>_EL1 is exposed to the guest. This is treated as
	 * a *RAW* write of register state for writes.
	 */
	{ SYS_DESC(SYS_ICC_PPI_SACTIVER0_EL1),
	  .set_user = set_gic_ppi_activer, .get_user = get_gic_ppi_activer, },
	{ SYS_DESC(SYS_ICC_PPI_SACTIVER1_EL1),
	  .set_user = set_gic_ppi_activer, .get_user = get_gic_ppi_activer, },
	/*
	 * Only ICC_SPENDR<n>_EL1 is exposed to the guest. This is treated as
	 * a *RAW* write of register state for writes.
	 */
	{ SYS_DESC(SYS_ICC_PPI_SPENDR0_EL1),
	  .set_user = set_gic_ppi_pendr, .get_user = get_gic_ppi_pendr, },
	{ SYS_DESC(SYS_ICC_PPI_SPENDR1_EL1),
	  .set_user = set_gic_ppi_pendr, .get_user = get_gic_ppi_pendr, },
	{ SYS_DESC(SYS_ICC_PPI_PRIORITYR0_EL1),
	  .set_user = set_gic_ppi_priorityr, .get_user = get_gic_ppi_priorityr, },
	{ SYS_DESC(SYS_ICC_PPI_PRIORITYR1_EL1),
	  .set_user = set_gic_ppi_priorityr, .get_user = get_gic_ppi_priorityr, },
	{ SYS_DESC(SYS_ICC_PPI_PRIORITYR2_EL1),
	  .set_user = set_gic_ppi_priorityr, .get_user = get_gic_ppi_priorityr, },
	{ SYS_DESC(SYS_ICC_PPI_PRIORITYR3_EL1),
	  .set_user = set_gic_ppi_priorityr, .get_user = get_gic_ppi_priorityr, },
	{ SYS_DESC(SYS_ICC_PPI_PRIORITYR4_EL1),
	  .set_user = set_gic_ppi_priorityr, .get_user = get_gic_ppi_priorityr, },
	{ SYS_DESC(SYS_ICC_PPI_PRIORITYR5_EL1),
	  .set_user = set_gic_ppi_priorityr, .get_user = get_gic_ppi_priorityr, },
	{ SYS_DESC(SYS_ICC_PPI_PRIORITYR6_EL1),
	  .set_user = set_gic_ppi_priorityr, .get_user = get_gic_ppi_priorityr, },
	{ SYS_DESC(SYS_ICC_PPI_PRIORITYR7_EL1),
	  .set_user = set_gic_ppi_priorityr, .get_user = get_gic_ppi_priorityr, },
	{ SYS_DESC(SYS_ICC_PPI_PRIORITYR8_EL1),
	  .set_user = set_gic_ppi_priorityr, .get_user = get_gic_ppi_priorityr, },
	{ SYS_DESC(SYS_ICC_PPI_PRIORITYR9_EL1),
	  .set_user = set_gic_ppi_priorityr, .get_user = get_gic_ppi_priorityr, },
	{ SYS_DESC(SYS_ICC_PPI_PRIORITYR10_EL1),
	  .set_user = set_gic_ppi_priorityr, .get_user = get_gic_ppi_priorityr, },
	{ SYS_DESC(SYS_ICC_PPI_PRIORITYR11_EL1),
	  .set_user = set_gic_ppi_priorityr, .get_user = get_gic_ppi_priorityr, },
	{ SYS_DESC(SYS_ICC_PPI_PRIORITYR12_EL1),
	  .set_user = set_gic_ppi_priorityr, .get_user = get_gic_ppi_priorityr, },
	{ SYS_DESC(SYS_ICC_PPI_PRIORITYR13_EL1),
	  .set_user = set_gic_ppi_priorityr, .get_user = get_gic_ppi_priorityr, },
	{ SYS_DESC(SYS_ICC_PPI_PRIORITYR14_EL1),
	  .set_user = set_gic_ppi_priorityr, .get_user = get_gic_ppi_priorityr, },
	{ SYS_DESC(SYS_ICC_PPI_PRIORITYR15_EL1),
	  .set_user = set_gic_ppi_priorityr, .get_user = get_gic_ppi_priorityr, },
	{ SYS_DESC(SYS_ICC_APR_EL1),
	  .set_user = set_gic_apr, .get_user = get_gic_apr, },
	{ SYS_DESC(SYS_ICC_CR0_EL1),
	  .set_user = set_gic_cr0, .get_user = get_gic_cr0, },
	{ SYS_DESC(SYS_ICC_PCR_EL1),
	  .set_user = set_gic_pcr, .get_user = get_gic_pcr, },
};

const struct sys_reg_desc *vgic_v5_get_sysreg_table(unsigned int *sz)
{
	*sz = ARRAY_SIZE(gic_v5_icc_reg_descs);
	return gic_v5_icc_reg_descs;
}

static u64 attr_to_id(u64 attr)
{
	return ARM64_SYS_REG(FIELD_GET(KVM_REG_ARM_VGIC_SYSREG_OP0_MASK, attr),
			     FIELD_GET(KVM_REG_ARM_VGIC_SYSREG_OP1_MASK, attr),
			     FIELD_GET(KVM_REG_ARM_VGIC_SYSREG_CRN_MASK, attr),
			     FIELD_GET(KVM_REG_ARM_VGIC_SYSREG_CRM_MASK, attr),
			     FIELD_GET(KVM_REG_ARM_VGIC_SYSREG_OP2_MASK, attr));
}

int vgic_v5_has_cpu_sysregs_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	const struct sys_reg_desc *r;

	r = get_reg_by_id(attr_to_id(attr->attr), gic_v5_icc_reg_descs,
			  ARRAY_SIZE(gic_v5_icc_reg_descs));

	if (r && !sysreg_hidden(vcpu, r))
		return 0;

	return -ENXIO;
}

int vgic_v5_cpu_sysregs_uaccess(struct kvm_vcpu *vcpu,
				struct kvm_device_attr *attr,
				bool is_write)
{
	struct kvm_one_reg reg = {
		.id	= attr_to_id(attr->attr),
		.addr	= attr->addr,
	};

	if (is_write)
		return kvm_sys_reg_set_user(vcpu, &reg, gic_v5_icc_reg_descs,
					    ARRAY_SIZE(gic_v5_icc_reg_descs));
	else
		return kvm_sys_reg_get_user(vcpu, &reg, gic_v5_icc_reg_descs,
					    ARRAY_SIZE(gic_v5_icc_reg_descs));
}
