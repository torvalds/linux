// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, 2026 Arm Ltd.
 */

#include <kvm/arm_vgic.h>

#include <linux/bitops.h>
#include <linux/irqchip/arm-vgic-info.h>
#include <linux/irqdomain.h>
#include <linux/kvm_host.h>
#include <linux/uaccess.h>

#include "vgic-v5-tables.h"
#include "vgic.h"

#define ppi_caps	kvm_vgic_global_state.vgic_v5_ppi_caps
#define irs_caps	kvm_vgic_global_state.vgic_v5_irs_caps

static int vgic_v5_irs_assign_vmt(bool two_level, u8 vm_id_bits, phys_addr_t vmt_base);
static int vgic_v5_irs_clear_vmt(void);

/*
 * Not all PPIs are guaranteed to be implemented for GICv5. Deterermine which
 * ones are, and generate a mask.
 */
static void vgic_v5_get_implemented_ppis(void)
{
	/*
	 * If we have KVM, we have EL2, which means that we have support for the
	 * EL1 and EL2 Physical & Virtual timers.
	 */
	__set_bit(GICV5_ARCH_PPI_CNTHP, ppi_caps.impl_ppi_mask);
	__set_bit(GICV5_ARCH_PPI_CNTV, ppi_caps.impl_ppi_mask);
	__set_bit(GICV5_ARCH_PPI_CNTHV, ppi_caps.impl_ppi_mask);
	__set_bit(GICV5_ARCH_PPI_CNTP, ppi_caps.impl_ppi_mask);

	/* The SW_PPI should be available */
	__set_bit(GICV5_ARCH_PPI_SW_PPI, ppi_caps.impl_ppi_mask);

	/* The PMUIRQ is available if we have the PMU */
	__assign_bit(GICV5_ARCH_PPI_PMUIRQ, ppi_caps.impl_ppi_mask, system_supports_pmuv3());
}

static u32 irs_readl_relaxed(const u32 reg_offset)
{
	return readl_relaxed(irs_caps.irs_base + reg_offset);
}

static void irs_writel_relaxed(const u32 val, const u32 reg_offset)
{
	writel_relaxed(val, irs_caps.irs_base + reg_offset);
}

static u64 irs_readq_relaxed(const u32 reg_offset)
{
	return readq_relaxed(irs_caps.irs_base + reg_offset);
}

static void irs_writeq_relaxed(const u64 val, const u32 reg_offset)
{
	writeq_relaxed(val, irs_caps.irs_base + reg_offset);
}

static void vgic_v5_irs_cache_id_regs(const struct gic_kvm_info *info)
{
	irs_caps.irs_base = info->gicv5_irs.base;
	irs_caps.non_coherent = info->gicv5_irs.non_coherent;

	irs_caps.idr2 = irs_readl_relaxed(GICV5_IRS_IDR2);
	irs_caps.idr3 = irs_readl_relaxed(GICV5_IRS_IDR3);
	irs_caps.idr4 = irs_readl_relaxed(GICV5_IRS_IDR4);
}

/*
 * Probe for a vGICv5 compatible interrupt controller, returning 0 on success.
 */
int vgic_v5_probe(const struct gic_kvm_info *info)
{
	bool v5_registered = false;
	int ret;

	kvm_vgic_global_state.type = VGIC_V5;
	kvm_vgic_global_state.max_gic_vcpus = 0;
	kvm_vgic_global_state.max_gicv5_vcpus = 0;

	kvm_vgic_global_state.vcpu_base = 0;
	kvm_vgic_global_state.vctrl_base = NULL;
	kvm_vgic_global_state.can_emulate_gicv2 = false;
	kvm_vgic_global_state.has_gicv4 = false;
	kvm_vgic_global_state.has_gicv4_1 = false;

	/*
	 * GICv5 is currently not supported in Protected mode. Skip the
	 * registration of GICv5 completely to make sure no guests can create a
	 * GICv5-based guest.
	 */
	if (is_protected_kvm_enabled()) {
		kvm_info("GICv5-based guests are not supported with pKVM\n");
		goto skip_v5;
	}

	if (!gicv5_global_data.lpi_domain) {
		kvm_err("GICv5 LPI domain unavailable\n");
		return -ENODEV;
	}

	vgic_v5_irs_cache_id_regs(info);
	vgic_v5_get_implemented_ppis();

	/*
	 * Even if the HW supports more per-VM vCPUs, artificially cap as we
	 * can't use them all.
	 */
	kvm_vgic_global_state.max_gicv5_vcpus = min(vgic_v5_irs_max_vpes(&irs_caps),
						    VGIC_V5_MAX_CPUS);

	/*
	 * GICv5 requires a set of tables to be allocated in order to manage
	 * VMs. We allocate them in advance here, which alas means that we
	 * already have to make a decisions regarding the maximum number of VMs
	 * we want to run. For now, we match the maximum number offered by the
	 * hardware, but this might not be a wise choice in the long term.
	 */
	ret = vgic_v5_vmt_allocate(kvm_vgic_global_state.max_gicv5_vcpus);
	if (ret) {
		kvm_err("Failed to allocate the GICv5 VM tables; no GICv5 support\n");
		return -ENODEV;
	}

	/*
	 * We've now allocated the VM table, but the host's IRS doesn't know
	 * about it yet. Provide the base address of the VMT to the IRS, as well
	 * as the number of ID bits that it covers and the structure used
	 * (linear/two-level).
	 */
	ret = vgic_v5_irs_assign_vmt(vgic_v5_irs_two_level_vmt_support(&irs_caps),
				     ilog2(vgic_v5_irs_max_vms(&irs_caps)),
				     vgic_v5_get_vmt_base());
	if (ret) {
		kvm_err("Failed to assign the GICv5 VM tables to the IRS; no GICv5 support\n");
		if (!vgic_v5_irs_clear_vmt())
			vgic_v5_vmt_free();
		return -ENODEV;
	}

	ret = kvm_register_vgic_device(KVM_DEV_TYPE_ARM_VGIC_V5);
	if (ret) {
		kvm_err("Cannot register GICv5 KVM device.\n");
		/*
		 * Don't free the VMT itself if the hardware still has a valid
		 * pointer to it.
		 */
		if (!vgic_v5_irs_clear_vmt())
			vgic_v5_vmt_free();
		return -ENODEV;
	}

	v5_registered = true;
	kvm_vgic_global_state.max_gic_vcpus =
		kvm_vgic_global_state.max_gicv5_vcpus;
	kvm_info("GCIE system register CPU interface\n");

skip_v5:
	/* If we don't support the GICv3 compat mode we're done. */
	if (!cpus_have_final_cap(ARM64_HAS_GICV5_LEGACY)) {
		if (!v5_registered)
			return -ENODEV;
		return 0;
	}

	kvm_vgic_global_state.has_gcie_v3_compat = true;

	/*
	 * The ListRegs field is 5 bits, but there is an architectural
	 * maximum of 16 list registers. Just ignore bit 4...
	 */
	kvm_vgic_global_state.nr_lr = (vgic_ich_vtr() & 0xf) + 1;

	ret = kvm_register_vgic_device(KVM_DEV_TYPE_ARM_VGIC_V3);
	if (ret) {
		kvm_err("Cannot register GICv3-legacy KVM device.\n");
		/* vGICv5 should still work */
		return v5_registered ? 0 : ret;
	}

	kvm_vgic_global_state.max_gic_vcpus = max(kvm_vgic_global_state.max_gic_vcpus,
						  VGIC_V3_MAX_CPUS);

	static_branch_enable(&kvm_vgic_global_state.gicv3_cpuif);
	kvm_info("GCIE legacy system register CPU interface\n");

	vgic_v3_enable_cpuif_traps();

	return 0;
}

/*
 * Wait for completion of a change in any of IRS_VMT_BASER, IRS_VMAP_L2_VMTR,
 * IRS_VMAP_VMR, IRS_VMAP_VPER, IRS_VMAP_VISTR, IRS_VMAP_L2_VISTR.
 */
static int vgic_v5_irs_wait_for_vm_op(void)
{
	return gicv5_wait_for_op_atomic(irs_caps.irs_base,
					GICV5_IRS_VMT_STATUSR,
					GICV5_IRS_VMT_STATUSR_IDLE,
					NULL);
}

/*
 * Wait for completion of a change in any of IRS_VPE_SELR, IRS_VPE_DBR,
 * IRS_VPE_CR0.
 */
static int vgic_v5_irs_wait_for_vpe_op(void)
{
	return gicv5_wait_for_op_atomic(irs_caps.irs_base,
					GICV5_IRS_VPE_STATUSR,
					GICV5_IRS_VPE_STATUSR_IDLE,
					NULL);
}

/*
 * Wait for a write to IRS_SAVE_VMR to complete.
 */
static int vgic_v5_irs_wait_for_save_vm_op(u32 *statusr)
{
	return gicv5_wait_for_op_atomic(irs_caps.irs_base,
					GICV5_IRS_SAVE_VM_STATUSR,
					GICV5_IRS_SAVE_VM_STATUSR_IDLE,
					statusr);
}

static int vgic_v5_irs_write_vm_mmio_reg(u64 val, u32 offset)
{
	int ret;

	lockdep_assert_held(&vgic_v5_irs_lock);

	/* Make sure that we are idle to begin with */
	ret = vgic_v5_irs_wait_for_vm_op();
	if (ret)
		return ret;

	irs_writeq_relaxed(val, offset);

	return vgic_v5_irs_wait_for_vm_op();
}

static int vgic_v5_irs_assign_vmt(bool two_level, u8 vm_id_bits,
				  phys_addr_t vmt_base)
{
	u64 vmt_baser;
	u32 vmt_cfgr;
	int ret;

	guard(raw_spinlock_irqsave)(&vgic_v5_irs_lock);

	ret = vgic_v5_irs_wait_for_vm_op();
	if (ret)
		return ret;

	vmt_baser = irs_readq_relaxed(GICV5_IRS_VMT_BASER);
	if (!!FIELD_GET(GICV5_IRS_VMT_BASER_VALID, vmt_baser))
		return -EBUSY;

	vmt_cfgr = FIELD_PREP(GICV5_IRS_VMT_CFGR_VM_ID_BITS, vm_id_bits);
	if (two_level)
		vmt_cfgr |= FIELD_PREP(GICV5_IRS_VMT_CFGR_STRUCTURE,
				       GICV5_IRS_VMT_CFGR_STRUCTURE_TWO_LEVEL);

	irs_writel_relaxed(vmt_cfgr, GICV5_IRS_VMT_CFGR);

	/* The base address is intentionally only masked and not shifted */
	vmt_baser = FIELD_PREP(GICV5_IRS_VMT_BASER_VALID, true) |
		    (vmt_base & GICV5_IRS_VMT_BASER_ADDR);
	irs_writeq_relaxed(vmt_baser, GICV5_IRS_VMT_BASER);

	return vgic_v5_irs_wait_for_vm_op();
}

static int vgic_v5_irs_clear_vmt(void)
{
	guard(raw_spinlock_irqsave)(&vgic_v5_irs_lock);

	return vgic_v5_irs_write_vm_mmio_reg(0, GICV5_IRS_VMT_BASER);
}

static int vgic_v5_irs_vmap_l2_vmt(u16 vm_id)
{
	u64 val = FIELD_PREP(GICV5_IRS_VMAP_L2_VMTR_VM_ID, vm_id) |
		GICV5_IRS_VMAP_L2_VMTR_M;

	return vgic_v5_irs_write_vm_mmio_reg(val, GICV5_IRS_VMAP_L2_VMTR);
}

static int __vgic_v5_irs_vmap_vm(u16 vm_id, bool unmap)
{
	u64 val = FIELD_PREP(GICV5_IRS_VMAP_VMR_VM_ID, vm_id) |
		FIELD_PREP(GICV5_IRS_VMAP_VMR_U, unmap) |
		GICV5_IRS_VMAP_VMR_M;

	return vgic_v5_irs_write_vm_mmio_reg(val, GICV5_IRS_VMAP_VMR);
}

static int vgic_v5_irs_set_vm_valid(u16 vm_id)
{
	return __vgic_v5_irs_vmap_vm(vm_id, false);
}

static int vgic_v5_irs_set_vm_invalid(u16 vm_id)
{
	return __vgic_v5_irs_vmap_vm(vm_id, true);
}

static int __vgic_v5_irs_update_vist_validity(u16 vm_id, bool spi_ist, bool unmap)
{
	u8 type = spi_ist ? 0b011 : 0b010;
	u64 val = FIELD_PREP(GICV5_IRS_VMAP_VISTR_TYPE, type) |
		FIELD_PREP(GICV5_IRS_VMAP_VISTR_VM_ID, vm_id) |
		FIELD_PREP(GICV5_IRS_VMAP_VISTR_U, unmap) |
		GICV5_IRS_VMAP_VISTR_M;

	return vgic_v5_irs_write_vm_mmio_reg(val, GICV5_IRS_VMAP_VISTR);
}

static int vgic_v5_irs_set_vist_valid(u16 vm_id, bool spi_ist)
{
	return __vgic_v5_irs_update_vist_validity(vm_id, spi_ist, false);
}

/*
 * LPI ISTs can be invalidated explicitly. SPI ISTs are invalidated by making
 * the VMTE invalid during teardown.
 */
static int vgic_v5_irs_set_vist_invalid(u16 vm_id, bool spi_ist)
{
	return __vgic_v5_irs_update_vist_validity(vm_id, spi_ist, true);
}

static int vgic_v5_irs_set_up_vpe(u16 vm_id, u16 vpe_id,
				  irq_hw_number_t db_hwirq)
{
	u64 vmap_vper, dbr, selr;
	u32 statusr, cr0;
	int ret;

	lockdep_assert_held(&vgic_v5_irs_lock);

	/* Make sure that we are idle to begin with */
	ret = vgic_v5_irs_wait_for_vm_op();
	if (ret)
		return ret;

	/* Mark the VPE as valid */
	vmap_vper = FIELD_PREP(GICV5_IRS_VMAP_VPER_VPE_ID, vpe_id) |
		    FIELD_PREP(GICV5_IRS_VMAP_VPER_VM_ID, vm_id) |
		    GICV5_IRS_VMAP_VPER_M;
	irs_writeq_relaxed(vmap_vper, GICV5_IRS_VMAP_VPER);

	/* Wait for the VPE to be marked valid in the VPET */
	ret = vgic_v5_irs_wait_for_vm_op();
	if (ret)
		return ret;

	selr = FIELD_PREP(GICV5_IRS_VPE_SELR_VPE_ID, vpe_id) |
	       FIELD_PREP(GICV5_IRS_VPE_SELR_VM_ID, vm_id) |
	       GICV5_IRS_VPE_SELR_S;
	irs_writeq_relaxed(selr, GICV5_IRS_VPE_SELR);

	ret = vgic_v5_irs_wait_for_vpe_op();
	if (ret)
		return ret;

	statusr = irs_readl_relaxed(GICV5_IRS_VPE_STATUSR);
	if (!FIELD_GET(GICV5_IRS_VPE_STATUSR_V, statusr))
		return -EINVAL;

	/* Set targeted only routing (disable 1ofN vPE selection) */
	cr0 = GICV5_IRS_VPE_CR0_DPS;
	irs_writel_relaxed(cr0, GICV5_IRS_VPE_CR0);

	ret = vgic_v5_irs_wait_for_vpe_op();
	if (ret)
		return ret;

	/*
	 * The VPE has not yet run. Therefore, make sure that all interrupts
	 * will generate a doorbell.
	 */
	dbr = FIELD_PREP(GICV5_IRS_VPE_DBR_INTID, db_hwirq) |
	      GICV5_IRS_VPE_DBR_DBV;
	irs_writeq_relaxed(dbr, GICV5_IRS_VPE_DBR);

	ret = vgic_v5_irs_wait_for_vpe_op();
	if (ret)
		return ret;

	return 0;
}

static int vgic_v5_irs_save_vm_op(u16 vm_id, bool save, u32 *statusr)
{
	u64 save_vmr;
	int ret;

	save_vmr = FIELD_PREP(GICV5_IRS_SAVE_VMR_VM_ID, vm_id);
	save_vmr |= GICV5_IRS_SAVE_VMR_Q;
	save_vmr |= FIELD_PREP(GICV5_IRS_SAVE_VMR_S, save);

	guard(raw_spinlock_irqsave)(&vgic_v5_irs_lock);

	/* Make sure that we are idle to begin with. */
	ret = vgic_v5_irs_wait_for_save_vm_op(NULL);
	if (ret)
		return ret;

	irs_writeq_relaxed(save_vmr, GICV5_IRS_SAVE_VMR);

	return vgic_v5_irs_wait_for_save_vm_op(statusr);
}

static irqreturn_t db_handler(int irq, void *data)
{
	struct kvm_vcpu *vcpu = data;

	WRITE_ONCE(vcpu->arch.vgic_cpu.vgic_v5.gicv5_vpe.db_fired, true);

	kvm_make_request(KVM_REQ_IRQ_PENDING, vcpu);
	kvm_vcpu_kick(vcpu);

	return IRQ_HANDLED;
}

static int vgic_v5_send_command(struct kvm_vcpu *vcpu, enum gicv5_vcpu_cmd cmd)
{
	int irq = vgic_v5_vpe_db(vcpu);

	if (!irq)
		return -ENXIO;

	return irq_set_vcpu_affinity(irq, &cmd);
}

static int vgic_v5_db_set_vcpu_affinity(struct irq_data *data, void *vcpu_info)
{
	struct vgic_v5_vm *vm = data->domain->host_data;
	enum gicv5_vcpu_cmd *cmd = vcpu_info;
	unsigned int vcpu_idx = data->hwirq;
	struct kvm_vcpu *vcpu;
	u16 vpe_id;

	guard(raw_spinlock_irqsave)(&vgic_v5_irs_lock);

	switch (*cmd) {
	case VMT_L2_MAP:
		return vgic_v5_irs_vmap_l2_vmt(vm->vm_id);
	case VMTE_MAKE_VALID:
		return vgic_v5_irs_set_vm_valid(vm->vm_id);
	case VMTE_MAKE_INVALID:
		return vgic_v5_irs_set_vm_invalid(vm->vm_id);
	case VPE_MAKE_VALID:
		/*
		 * The index in the doorbell domain aligns with our flat
		 * vcpu_idx index. However, we need the actual VPE ID, which
		 * means we first need to resolve the actual vcpu.
		 */
		vcpu = kvm_get_vcpu(vm->kvm, vcpu_idx);
		if (!vcpu)
			return -EINVAL;

		vpe_id = vgic_v5_vpe_id(vcpu);

		/*
		 * We need the actual LPI ID which lives in the top-most parent
		 * domain. This hwirq won't include the type (LPI) but that's
		 * not required for the IRS_VPE_DBR.
		 */
		while (data->parent_data)
			data = data->parent_data;
		return vgic_v5_irs_set_up_vpe(vm->vm_id, vpe_id, data->hwirq);
	case SPI_VIST_MAKE_VALID:
		return vgic_v5_irs_set_vist_valid(vm->vm_id, true);
	case LPI_VIST_MAKE_VALID:
		return vgic_v5_irs_set_vist_valid(vm->vm_id, false);
	case LPI_VIST_MAKE_INVALID:
		return vgic_v5_irs_set_vist_invalid(vm->vm_id, false);
	default:
		return -EINVAL;
	}
}

/*
 * This set of irq_chip functions is specific for doorbells.
 */
static const struct irq_chip vgic_v5_db_irq_chip = {
	.name = "GICv5-DB",
	.irq_mask = irq_chip_mask_parent,
	.irq_unmask = irq_chip_unmask_parent,
	.irq_eoi = irq_chip_eoi_parent,
	.irq_set_affinity = irq_chip_set_affinity_parent,
	.irq_get_irqchip_state = irq_chip_get_parent_state,
	.irq_set_irqchip_state = irq_chip_set_parent_state,
	.irq_set_vcpu_affinity = vgic_v5_db_set_vcpu_affinity,
	.flags = IRQCHIP_SET_TYPE_MASKED | IRQCHIP_SKIP_SET_WAKE |
		 IRQCHIP_MASK_ON_SUSPEND,
};

static void vgic_v5_irq_db_domain_free(struct irq_domain *domain,
				       unsigned int virq, unsigned int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++) {
		struct irq_data *d = irq_domain_get_irq_data(domain, virq + i);

		irq_set_handler(virq + i, NULL);
		irq_domain_reset_irq_data(d);
	}

	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
}

static int vgic_v5_irq_db_domain_alloc(struct irq_domain *domain,
				       unsigned int virq, unsigned int nr_irqs,
				       void *arg)
{
	const struct irq_chip *chip = &vgic_v5_db_irq_chip;
	struct irq_data *irqd;
	int ret;

	ret = irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, NULL);
	if (ret)
		return ret;

	for (int i = 0; i < nr_irqs; i++) {
		irq_domain_set_hwirq_and_chip(domain, virq + i, i, chip,
					      domain->host_data);
		irqd = irq_desc_get_irq_data(irq_to_desc(virq + i));
		irqd_set_single_target(irqd);
	}

	return 0;
}

static const struct irq_domain_ops vgic_v5_irq_db_domain_ops = {
	.alloc = vgic_v5_irq_db_domain_alloc,
	.free = vgic_v5_irq_db_domain_free,
};

static int vgic_v5_create_per_vm_domain(struct kvm *kvm)
{
	struct vgic_v5_vm *vm = &kvm->arch.vgic.gicv5_vm;
	int nr_vcpus = atomic_read(&kvm->online_vcpus);
	int id = task_pid_nr(current);
	int ret, db_virq = 0;

	vm->fwnode = irq_domain_alloc_named_id_fwnode("GICv5-vpe-db", id);
	if (!vm->fwnode)
		return -ENOMEM;

	vm->domain = irq_domain_create_hierarchy(gicv5_global_data.lpi_domain,
						 0, nr_vcpus, vm->fwnode,
						 &vgic_v5_irq_db_domain_ops, vm);
	if (!vm->domain) {
		ret = -ENOMEM;
		goto err;
	}

	db_virq = irq_domain_alloc_irqs(vm->domain, nr_vcpus, NUMA_NO_NODE, vm);
	if (db_virq <= 0) {
		ret = db_virq;
		goto err;
	}

	kvm->arch.vgic.gicv5_vm.vpe_db_base = db_virq;

	return 0;

err:
	if (vm->domain)
		irq_domain_remove(vm->domain);
	if (vm->fwnode)
		irq_domain_free_fwnode(vm->fwnode);

	kvm->arch.vgic.gicv5_vm.vpe_db_base = 0;
	vm->domain = NULL;
	vm->fwnode = NULL;

	return ret;
}

static void vgic_v5_teardown_per_vm_domain(struct vgic_v5_vm *vm)
{
	if (!vm->domain)
		return;

	irq_domain_remove(vm->domain);
	irq_domain_free_fwnode(vm->fwnode);
	vm->domain = NULL;
	vm->fwnode = NULL;
}

void vgic_v5_reset(struct kvm_vcpu *vcpu)
{
	/*
	 * We always present 16-bits of ID space to the guest, irrespective of
	 * the host allowing more.
	 */
	vcpu->arch.vgic_cpu.num_id_bits = ICC_IDR0_EL1_ID_BITS_16BITS;

	/*
	 * The GICv5 architeture only supports 5-bits of priority in the
	 * CPUIF (but potentially fewer in the IRS).
	 */
	vcpu->arch.vgic_cpu.num_pri_bits = 5;

	/* Make the VPE valid in the VPET */
	if (WARN_ON(vgic_v5_send_command(vcpu, VPE_MAKE_VALID)))
		return;
}

static void vgic_v5_free_doorbells(struct kvm *kvm, unsigned int nr_dbs)
{
	struct vgic_v5_vm *vm = &kvm->arch.vgic.gicv5_vm;
	struct kvm_vcpu *vcpu;
	unsigned long i;
	int db;

	for (i = 0; i < nr_dbs; i++) {
		vcpu = kvm_get_vcpu(kvm, i);
		db = vgic_v5_vpe_db(vcpu);
		if (!db)
			continue;

		free_irq(db, vcpu);
		vcpu->arch.vgic_cpu.vgic_v5.gicv5_vpe.db = 0;
	}

	if (vm->vpe_db_base) {
		irq_domain_free_irqs(vm->vpe_db_base,
				     atomic_read(&kvm->online_vcpus));
		vm->vpe_db_base = 0;
	}
}

void vgic_v5_teardown(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct kvm_vcpu *vcpu, *vcpu0;
	bool release_vm_id = true;
	unsigned long i;
	int rc;

	lockdep_assert_held(&kvm->arch.config_lock);

	/*
	 * If the VM's ID isn't valid, then we either failed init very early or
	 * we've been called a second time. Nothing to do here in either case.
	 */
	if (kvm->arch.vgic.gicv5_vm.vm_id == VGIC_V5_VM_ID_INVAL)
		return;

	if (kvm->arch.vgic.gicv5_vm.vmte_allocated) {
		/* Make the VM invalid  */
		vcpu0 = kvm_get_vcpu(kvm, 0);
		rc = vgic_v5_send_command(vcpu0, VMTE_MAKE_INVALID);
		if (rc) {
			kvm_err("could not make VMTE invalid\n");
			release_vm_id = false;
			goto out_free_doorbells;
		}

		kvm_for_each_vcpu(i, vcpu, kvm) {
			if (vgic_v5_vmte_free_vpe(vcpu)) {
				kvm_err("Failed to free VPE\n");
				release_vm_id = false;
				goto out_free_doorbells;
			}
		}

		if (vgic_v5_vmte_release(kvm)) {
			kvm_err("Failed to release VM 0x%x\n", dist->gicv5_vm.vm_id);
			release_vm_id = false;
		}
	}

out_free_doorbells:
	vgic_v5_free_doorbells(kvm, atomic_read(&kvm->online_vcpus));
	vgic_v5_teardown_per_vm_domain(&kvm->arch.vgic.gicv5_vm);

	/*
	 * We only release the VM ID itself if we didn't fail earlier. It does
	 * mean that we might lose the VM ID (and associated VMTE, etc), but
	 * given that we've failed to tear them down correctly there's no way to
	 * safely reuse them. The VM ID allocating IDA will make sure we don't
	 * accidentally reuse this partially torn down state.
	 */
	if (release_vm_id)
		vgic_v5_release_vm_id(kvm);
}

/*
 * Claim and populate a VMTE (optionally making a new L2 VMT valid), create VPE
 * doorbells, allocate VPET and populate for each VPE. Finally, we also init the
 * vIRS, which means allocating and making the virtual SPI IST valid.
 *
 * Note: We do need to put the cart before the horse here. The VPE doorbells are
 * our conduit for communication with the IRS, which means we need to have those
 * before making the VMTE valid.
 *
 * On failure, we clean up in the teardown path (vgic_v5_teardown()).
 */
int vgic_v5_init(struct kvm *kvm)
{
	struct kvm_vcpu *vcpu, *vcpu0;
	int nr_vcpus, ret = 0;
	unsigned int db_virq;
	unsigned long i;

	lockdep_assert_held(&kvm->arch.config_lock);

	nr_vcpus = atomic_read(&kvm->online_vcpus);
	if (nr_vcpus == 0)
		return -ENODEV;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (vcpu_has_nv(vcpu)) {
			kvm_err("Nested GICv5 VMs are currently unsupported\n");
			return -EINVAL;
		}
	}

	/* We only allow userspace to drive the SW_PPI, if it is implemented. */
	bitmap_zero(kvm->arch.vgic.gicv5_vm.userspace_ppis,
		    VGIC_V5_NR_PRIVATE_IRQS);
	__set_bit(GICV5_ARCH_PPI_SW_PPI, kvm->arch.vgic.gicv5_vm.userspace_ppis);
	bitmap_and(kvm->arch.vgic.gicv5_vm.userspace_ppis,
		   kvm->arch.vgic.gicv5_vm.userspace_ppis,
		   ppi_caps.impl_ppi_mask, VGIC_V5_NR_PRIVATE_IRQS);

	ret = vgic_v5_allocate_vm_id(kvm);
	if (ret)
		return ret;

	/*
	 * Stash a backpointer to struct kvm. It is required to resolve the VPE
	 * ID from the doorbell index, which matches the flat vcpu_idx and not
	 * the vcpu_id.
	 */
	kvm->arch.vgic.gicv5_vm.kvm = kvm;

	ret = vgic_v5_create_per_vm_domain(kvm);
	if (ret)
		goto err;

	db_virq = kvm->arch.vgic.gicv5_vm.vpe_db_base;
	kvm_for_each_vcpu(i, vcpu, kvm) {
		ret = request_irq(db_virq + i, db_handler, 0, "vcpu", vcpu);
		if (ret)
			goto err;

		/* Stash it with the VCPU for easy retrieval */
		vcpu->arch.vgic_cpu.vgic_v5.gicv5_vpe.db = db_virq + i;
	}

	/* Populate VMTE (with VPET and VM descriptor) */
	ret = vgic_v5_vmte_init(kvm);
	if (ret)
		goto err;

	/* We pick the first vcpu to make the VMTE valid - any would do */
	vcpu0 = kvm_get_vcpu(kvm, 0);
	ret = vgic_v5_send_command(vcpu0, VMTE_MAKE_VALID);
	if (ret)
		goto err;

	/* Populate the VPETE for each VPE. */
	kvm_for_each_vcpu(i, vcpu, kvm) {
		ret = vgic_v5_vmte_alloc_vpe(vcpu);
		if (ret)
			goto err;
	}

	/* Init IRS (and alloc SPI IST) */
	ret = kvm_vgic_v5_irs_init(kvm, kvm->arch.vgic.nr_spis);
	if (ret)
		goto err;

	return 0;

err:
	/*
	 * Explicitly tear everything down on failure. The teardown function is
	 * written to handle any partial state we might have, so we don't need
	 * to do any clean-up first. Teardown will be called a second time on VM
	 * destruction, but that's fine - it is better to leave things in a
	 * clean state now, and doubly so because userspace could actually go
	 * and retry init.
	 */
	vgic_v5_teardown(kvm);

	return ret;
}

int vgic_v5_map_resources(struct kvm *kvm)
{
	if (!vgic_initialized(kvm))
		return -EBUSY;
	if (kvm->arch.vgic.vgic_v5_irs_data->lpi_ist_restore_pending)
		return -EINVAL;

	return 0;
}

int vgic_v5_finalize_ppi_state(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm	= vcpu->kvm;
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;
	int i;

	if (!vgic_is_v5(kvm))
		return 0;

	guard(mutex)(&kvm->arch.config_lock);

	/*
	 * Discover the set of PPIs that are exposed to the guest once per VM.
	 * Once known, apply that mask to each VCPU's restored PPI state as the
	 * VCPUs are first run.
	 */
	if (!test_bit(GICV5_ARCH_PPI_SW_PPI, kvm->arch.vgic.gicv5_vm.vgic_ppi_mask)) {
		bitmap_zero(kvm->arch.vgic.gicv5_vm.vgic_ppi_mask,
			    VGIC_V5_NR_PRIVATE_IRQS);
		bitmap_zero(kvm->arch.vgic.gicv5_vm.vgic_ppi_hmr,
			    VGIC_V5_NR_PRIVATE_IRQS);

		for_each_set_bit(i, ppi_caps.impl_ppi_mask, VGIC_V5_NR_PRIVATE_IRQS) {
			const u32 intid = vgic_v5_make_ppi(i);
			struct vgic_irq *irq;

			irq = vgic_get_vcpu_irq(vcpu, intid);

			/* Expose PPIs with an owner or the SW_PPI, only */
			scoped_guard(raw_spinlock_irqsave, &irq->irq_lock) {
				if (irq->owner || i == GICV5_ARCH_PPI_SW_PPI) {
					__set_bit(i, kvm->arch.vgic.gicv5_vm.vgic_ppi_mask);
					__assign_bit(i, kvm->arch.vgic.gicv5_vm.vgic_ppi_hmr,
						     irq->config == VGIC_CONFIG_LEVEL);
				}
			}

			vgic_put_irq(kvm, irq);
		}
	}

	/*
	 * Apply the mask to Enable, Active. Skip pending as that's calculated
	 * on guest entry.
	 */
	bitmap_and(cpu_if->vgic_ppi_enabler, cpu_if->vgic_ppi_enabler,
		   kvm->arch.vgic.gicv5_vm.vgic_ppi_mask, VGIC_V5_NR_PRIVATE_IRQS);
	bitmap_and(cpu_if->vgic_ppi_activer, cpu_if->vgic_ppi_activer,
		   kvm->arch.vgic.gicv5_vm.vgic_ppi_mask, VGIC_V5_NR_PRIVATE_IRQS);

	/* Also update the vgic_irqs */
	for (i = 0; i < VGIC_V5_NR_PRIVATE_IRQS; i++) {
		bool visible = test_bit(i, kvm->arch.vgic.gicv5_vm.vgic_ppi_mask);
		const u32 intid = vgic_v5_make_ppi(i);
		struct vgic_irq *irq;

		irq = vgic_get_vcpu_irq(vcpu, intid);

		scoped_guard(raw_spinlock_irqsave, &irq->irq_lock) {
			if (!visible) {
				irq->enabled = false;
				irq->active = false;
				irq->pending_latch = false;
				irq->line_level = false;
			} else {
				irq->enabled = test_bit(i, cpu_if->vgic_ppi_enabler);
				irq->active = test_bit(i, cpu_if->vgic_ppi_activer);
			}
		}

		vgic_put_irq(kvm, irq);
	}

	return 0;
}

static u32 vgic_v5_get_effective_priority_mask(struct kvm_vcpu *vcpu)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;
	u32 highest_ap, priority_mask, apr;

	/*
	 * If the guest's CPU has not opted to receive interrupts, then the
	 * effective running priority is the highest priority. Just return 0
	 * (the highest priority).
	 */
	if (!FIELD_GET(FEAT_GCIE_ICH_VMCR_EL2_EN, cpu_if->vgic_vmcr))
		return 0;

	/*
	 * Counting the number of trailing zeros gives the current active
	 * priority. Explicitly use the 32-bit version here as we have 32
	 * priorities. 32 then means that there are no active priorities.
	 */
	apr = cpu_if->vgic_apr;
	highest_ap = apr ? __builtin_ctz(apr) : 32;

	/*
	 * An interrupt is of sufficient priority if it is equal to or
	 * greater than the priority mask. Add 1 to the priority mask
	 * (i.e., lower priority) to match the APR logic before taking
	 * the min. This gives us the lowest priority that is masked.
	 */
	priority_mask = FIELD_GET(FEAT_GCIE_ICH_VMCR_EL2_VPMR, cpu_if->vgic_vmcr);

	return min(highest_ap, priority_mask + 1);
}

/*
 * For GICv5, the PPIs are mostly directly managed by the hardware. We (the
 * hypervisor) handle the pending, active, enable state save/restore, but
 * don't need the PPIs to be queued on a per-VCPU AP list. Therefore,
 * unlock, kick the vcpu and return.
 */
bool vgic_v5_ppi_queue_irq_unlock(struct kvm *kvm, struct vgic_irq *irq,
				  unsigned long flags)
	__releases(&irq->irq_lock)
{
	struct kvm_vcpu *vcpu;

	lockdep_assert_held(&irq->irq_lock);

	vcpu = irq->target_vcpu;

	raw_spin_unlock_irqrestore(&irq->irq_lock, flags);

	/* Directly kick the target VCPU to make sure it sees the IRQ */
	kvm_make_request(KVM_REQ_IRQ_PENDING, vcpu);
	kvm_vcpu_kick(vcpu);

	return true;
}

/*
 * Sets/clears the corresponding bit in the ICH_PPI_DVIR register.
 */
void vgic_v5_set_ppi_dvi(struct kvm_vcpu *vcpu, struct vgic_irq *irq, bool dvi)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;
	u32 ppi;

	lockdep_assert_held(&irq->irq_lock);

	ppi = vgic_v5_get_hwirq_id(irq->intid);
	assign_bit(ppi, cpu_if->vgic_ppi_dvir, dvi);
}

static const struct irq_ops vgic_v5_ppi_irq_ops = {
	.queue_irq_unlock = vgic_v5_ppi_queue_irq_unlock,
	.set_direct_injection = vgic_v5_set_ppi_dvi,
};

void vgic_v5_set_ppi_ops(struct kvm_vcpu *vcpu, u32 vintid)
{
	kvm_vgic_set_irq_ops(vcpu, vintid, &vgic_v5_ppi_irq_ops);
}

/*
 * Sync back the PPI priorities to the vgic_irq shadow state for any interrupts
 * exposed to the guest (skipping all others).
 */
static void vgic_v5_sync_ppi_priorities(struct kvm_vcpu *vcpu)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;
	u64 priorityr;
	int i;

	/*
	 * We have up to 16 PPI Priority regs, but only have a few interrupts
	 * that the guest is allowed to use. Limit our sync of PPI priorities to
	 * those actually exposed to the guest by first iterating over the mask
	 * of exposed PPIs.
	 */
	for_each_visible_v5_ppi(i, vcpu->kvm) {
		u32 intid = vgic_v5_make_ppi(i);
		struct vgic_irq *irq;
		int pri_idx, pri_reg, pri_bit;
		u8 priority;

		/*
		 * Determine which priority register and the field within it to
		 * extract.
		 */
		pri_reg = i / 8;
		pri_idx = i % 8;
		pri_bit = pri_idx * 8;

		priorityr = cpu_if->vgic_ppi_priorityr[pri_reg];
		priority = field_get(GENMASK(pri_bit + 4, pri_bit), priorityr);

		irq = vgic_get_vcpu_irq(vcpu, intid);

		scoped_guard(raw_spinlock_irqsave, &irq->irq_lock)
			irq->priority = priority;

		vgic_put_irq(vcpu->kvm, irq);
	}
}

bool vgic_v5_has_pending_ppi(struct kvm_vcpu *vcpu)
{
	unsigned int priority_mask;
	int i;

	priority_mask = vgic_v5_get_effective_priority_mask(vcpu);

	/*
	 * If the combined priority mask is 0, nothing can be signalled! In the
	 * case where the guest has disabled interrupt delivery for the vcpu
	 * (via ICV_CR0_EL1.EN->ICH_VMCR_EL2.EN), we calculate the priority mask
	 * as 0 too (the highest possible priority).
	 */
	if (!priority_mask)
		return false;

	for_each_visible_v5_ppi(i, vcpu->kvm) {
		u32 intid = vgic_v5_make_ppi(i);
		bool has_pending = false;
		struct vgic_irq *irq;

		irq = vgic_get_vcpu_irq(vcpu, intid);

		scoped_guard(raw_spinlock_irqsave, &irq->irq_lock)
			if (irq->enabled && irq->priority < priority_mask)
				has_pending = irq->hw ? vgic_get_phys_line_level(irq) : irq_is_pending(irq);

		vgic_put_irq(vcpu->kvm, irq);

		if (has_pending)
			return true;
	}

	return false;
}

/*
 * Detect any PPIs state changes, and propagate the state with KVM's
 * shadow structures.
 */
void vgic_v5_fold_ppi_state(struct kvm_vcpu *vcpu)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;
	unsigned long *activer, *pendr;
	int i;

	activer = host_data_ptr(vgic_v5_ppi_state)->activer_exit;
	pendr = host_data_ptr(vgic_v5_ppi_state)->pendr;

	for_each_visible_v5_ppi(i, vcpu->kvm) {
		u32 intid = vgic_v5_make_ppi(i);
		struct vgic_irq *irq;

		irq = vgic_get_vcpu_irq(vcpu, intid);

		scoped_guard(raw_spinlock_irqsave, &irq->irq_lock) {
			irq->active = test_bit(i, activer);

			/* This is an OR to avoid losing incoming edges! */
			if (irq->config == VGIC_CONFIG_EDGE)
				irq->pending_latch |= test_bit(i, pendr);
		}

		vgic_put_irq(vcpu->kvm, irq);
	}

	/*
	 * Re-inject the exit state as entry state next time!
	 *
	 * Note that the write of the Enable state is trapped, and hence there
	 * is nothing to explcitly sync back here as we already have the latest
	 * copy by definition.
	 */
	bitmap_copy(cpu_if->vgic_ppi_activer, activer, VGIC_V5_NR_PRIVATE_IRQS);
}

void vgic_v5_flush_ppi_state(struct kvm_vcpu *vcpu)
{
	DECLARE_BITMAP(pendr, VGIC_V5_NR_PRIVATE_IRQS);
	int i;

	/*
	 * Time to enter the guest - we first need to build the guest's
	 * ICC_PPI_PENDRx_EL1, however.
	 */
	bitmap_zero(pendr, VGIC_V5_NR_PRIVATE_IRQS);
	for_each_visible_v5_ppi(i, vcpu->kvm) {
		u32 intid = vgic_v5_make_ppi(i);
		struct vgic_irq *irq;

		irq = vgic_get_vcpu_irq(vcpu, intid);

		scoped_guard(raw_spinlock_irqsave, &irq->irq_lock) {
			__assign_bit(i, pendr, irq_is_pending(irq));
			if (irq->config == VGIC_CONFIG_EDGE)
				irq->pending_latch = false;
		}

		vgic_put_irq(vcpu->kvm, irq);
	}

	/*
	 * Copy the shadow state to the pending reg that will be written to the
	 * ICH_PPI_PENDRx_EL2 regs. While the guest is running we track any
	 * incoming changes to the pending state in the vgic_irq structures. The
	 * incoming changes are merged with the outgoing changes on the return
	 * path.
	 */
	bitmap_copy(host_data_ptr(vgic_v5_ppi_state)->pendr, pendr,
		    VGIC_V5_NR_PRIVATE_IRQS);
}

static bool vgic_v5_set_spi_pending_state(struct kvm_vcpu *vcpu,
					  struct vgic_irq *irq)
{
	vgic_v5_set_irq_pend(irq->target_vcpu, irq);
	return true;
}

bool vgic_v5_spi_queue_irq_unlock(struct kvm *kvm,
				  struct vgic_irq *irq,
				  unsigned long flags)
	__releases(&irq->irq_lock)
{
	lockdep_assert_held(&irq->irq_lock);

	raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
	return true;
}

static const struct irq_ops vgic_v5_spi_irq_ops = {
	.set_pending_state = vgic_v5_set_spi_pending_state,
	.queue_irq_unlock = vgic_v5_spi_queue_irq_unlock,
};

void vgic_v5_set_spi_ops(struct vgic_irq *irq)
{
	if (WARN_ON(!irq) || WARN_ON(irq->ops))
		return;

	irq->ops = &vgic_v5_spi_irq_ops;
}

/* Set the pending state for GICv5 SPIs and LPIs */
void vgic_v5_set_irq_pend(struct kvm_vcpu *vcpu, struct vgic_irq *irq)
{
	if (WARN_ON(__irq_is_ppi(KVM_DEV_TYPE_ARM_VGIC_V5, irq->intid)))
		return;

	kvm_call_hyp(__vgic_v5_vdpend, irq->intid, irq_is_pending(irq),
		     vcpu->kvm->arch.vgic.gicv5_vm.vm_id);
}

void vgic_v5_load(struct kvm_vcpu *vcpu)
{
	bool irichppidis = !READ_ONCE(vcpu->kvm->arch.vgic.enabled);
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;
	u16 vm = vgic_v5_vm_id(vcpu->kvm);
	u16 vpe = vgic_v5_vpe_id(vcpu);

	/*
	 * On the WFI path, vgic_load is called a second time. The first is when
	 * scheduling in the vcpu thread again, and the second is when leaving
	 * WFI. Skip the second instance as it serves no purpose and just
	 * restores the same state again.
	 */
	if (cpu_if->gicv5_vpe.resident)
		return;

	kvm_call_hyp(__vgic_v5_restore_vmcr_apr, cpu_if);

	cpu_if->vgic_contextr = FIELD_PREP(ICH_CONTEXTR_EL2_V, true) |
				FIELD_PREP(ICH_CONTEXTR_EL2_IRICHPPIDIS, irichppidis) |
				FIELD_PREP(ICH_CONTEXTR_EL2_VPE, vpe) |
				FIELD_PREP(ICH_CONTEXTR_EL2_VM, vm);

	kvm_call_hyp(__vgic_v5_make_resident, cpu_if);

	/* Failed to make the VPE resident? Bang! */
	if (WARN_ON(!!FIELD_GET(ICH_CONTEXTR_EL2_F, cpu_if->vgic_contextr)))
		kvm_vm_dead(vcpu->kvm);
}

void vgic_v5_put(struct kvm_vcpu *vcpu)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;

	/*
	 * Do nothing if we're not resident. This can happen in the WFI path
	 * where we do a vgic_put in the WFI path and again later when
	 * descheduling the thread. We risk losing VMCR state if we sync it
	 * twice, so instead return early in this case.
	 */
	if (!cpu_if->gicv5_vpe.resident)
		return;

	kvm_call_hyp(__vgic_v5_save_apr, cpu_if);

	cpu_if->vgic_contextr = 0;

	/* Request a doorbell if entering WFI, unless the IRS is disabled */
	if (vcpu_get_flag(vcpu, IN_WFI) &&
	    READ_ONCE(vcpu->kvm->arch.vgic.enabled)) {
		u32 priority_mask;
		int dbpm;

		/*
		 * Find the virtual running priority and use this to calculate
		 * the doorbell priority mask. We combine the highest active
		 * priority and the CPU's priority mask. The guest can't handle
		 * interrupts with priorities less than or equal to the virtual
		 * running priority, so there's literally no point in waking the
		 * guest for these.
		 *
		 * The priority needs to be higher than the mask to signal, so
		 * pick the next higher priority (subtract 1).
		 */
		priority_mask = vgic_v5_get_effective_priority_mask(vcpu);

		/*
		 * Request a doorbell *unless* the priority is 0, indicating
		 * that no interrupt can wake the CPU up.
		 */
		if (priority_mask) {
			int db_irq = vgic_v5_vpe_db(vcpu);
			struct irq_data *d = irq_get_irq_data(db_irq);
			const struct cpumask *aff = irq_data_get_effective_affinity_mask(d);
			int cpu = smp_processor_id();

			dbpm = priority_mask - 1;
			cpu_if->vgic_contextr = FIELD_PREP(ICH_CONTEXTR_EL2_DB, 1) |
						FIELD_PREP(ICH_CONTEXTR_EL2_DBPM, dbpm);

			/*
			 * Make the doorbell affine to this CPU, if it isn't
			 * already. Actively check the cpumask first as it is
			 * cheaper than changing the affinity every time.
			 */
			if (!cpumask_test_cpu(cpu, aff)) {
				int ret;

				ret = irq_set_affinity(db_irq, cpumask_of(cpu));
				if (ret)
					kvm_debug_ratelimited("Failed to move doorbell IRQ %d to CPU %d: %d\n",
							      db_irq, cpu, ret);
			}
		}
	}

	kvm_call_hyp(__vgic_v5_make_non_resident, cpu_if);

	/* The shadow priority is only updated on entering WFI */
	if (vcpu_get_flag(vcpu, IN_WFI))
		vgic_v5_sync_ppi_priorities(vcpu);
}

void vgic_v5_get_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcrp)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;
	u64 vmcr = cpu_if->vgic_vmcr;

	vmcrp->en = FIELD_GET(FEAT_GCIE_ICH_VMCR_EL2_EN, vmcr);
	vmcrp->pmr = FIELD_GET(FEAT_GCIE_ICH_VMCR_EL2_VPMR, vmcr);
}

void vgic_v5_set_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcrp)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;
	u64 vmcr;

	vmcr = FIELD_PREP(FEAT_GCIE_ICH_VMCR_EL2_VPMR, vmcrp->pmr) |
	       FIELD_PREP(FEAT_GCIE_ICH_VMCR_EL2_EN, vmcrp->en);

	cpu_if->vgic_vmcr = vmcr;
}

void vgic_v5_restore_state(struct kvm_vcpu *vcpu)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;

	__vgic_v5_restore_state(cpu_if);
	__vgic_v5_restore_ppi_state(cpu_if);
	dsb(sy);
}

void vgic_v5_save_state(struct kvm_vcpu *vcpu)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;

	__vgic_v5_save_state(cpu_if);
	__vgic_v5_save_ppi_state(cpu_if);
	dsb(sy);
}

static int vgic_v5_irs_status_is_quiesced(u32 statusr)
{
	if (statusr & GICV5_IRS_SAVE_VM_STATUSR_Q)
		return 0;

	return -EBUSY;
}

static int vgic_v5_irs_is_quiesced(u16 vm_id)
{
	u32 statusr;
	int ret;

	/*
	 * Ensure that the IST snapshot is complete before asking the IRS
	 * whether the VM remained quiescent while it was copied.
	 */
	mb();

	ret = vgic_v5_irs_save_vm_op(vm_id, false, &statusr);
	if (ret)
		return ret;

	return vgic_v5_irs_status_is_quiesced(statusr);
}

static int vgic_v5_copy_ist_attr(struct kvm_device_attr *attr,
				 struct kvm_vgic_v5_ist *ist_attr)
{
	void __user *uaddr = (void __user *)(unsigned long)attr->addr;

	if (!uaddr)
		return -EINVAL;

	if (copy_from_user(ist_attr, uaddr, sizeof(*ist_attr)))
		return -EFAULT;

	return 0;
}

static int vgic_v5_validate_ist_user_buffer(__u64 addr, __u64 size,
					    size_t expected)
{
	if (!addr || size != expected)
		return -EINVAL;

	return 0;
}

static int vgic_v5_validate_ist_attr(struct kvm *kvm,
				     const struct kvm_vgic_v5_ist *ist_attr)
{
	unsigned int id_bits;
	int ret;

	/* We always have SPIs to save */
	ret = vgic_v5_validate_ist_user_buffer(ist_attr->spi_ist_addr,
					ist_attr->spi_ist_size,
					kvm->arch.vgic.nr_spis * sizeof(__u32));
	if (ret)
		return ret;

	/* We don't always have LPIs to save */
	ret = vgic_v5_irs_lpi_ist_id_bits(kvm, &id_bits);
	if (ret < 0)
		return ret;

	/* No LPI IST */
	if (!ret) {
		if (ist_attr->lpi_ist_addr || ist_attr->lpi_ist_size)
			return -EINVAL;

		return 0;
	}

	return vgic_v5_validate_ist_user_buffer(ist_attr->lpi_ist_addr,
					       ist_attr->lpi_ist_size,
					       BIT(id_bits) * sizeof(__u32));
}

int vgic_v5_irs_save_ists(struct kvm *kvm, struct kvm_device_attr *attr)
{
	struct kvm_vgic_v5_ist ist_attr;
	u16 vm_id = vgic_v5_vm_id(kvm);
	u32 statusr;
	int ret = 0;

	mutex_lock(&kvm->lock);

	if (kvm_trylock_all_vcpus(kvm)) {
		mutex_unlock(&kvm->lock);
		return -EBUSY;
	}

	mutex_lock(&kvm->arch.config_lock);

	if (!vgic_initialized(kvm)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	ret = vgic_v5_copy_ist_attr(attr, &ist_attr);
	if (ret)
		goto out_unlock;

	ret = vgic_v5_validate_ist_attr(kvm, &ist_attr);
	if (ret)
		goto out_unlock;

	ret = vgic_v5_irs_save_vm_op(vm_id, true, &statusr);
	if (ret) {
		kvm_err("Failed to save GICv5 IRS VM state: %d\n", ret);
		goto out_unlock;
	}

	ret = vgic_v5_irs_status_is_quiesced(statusr);
	if (ret)
		goto out_unlock;

	/* Save the SPI IST to the userspace buffer. */
	ret = vgic_v5_save_spi_ist(kvm, &ist_attr);
	if (ret)
		goto out_unlock;

	ret = vgic_v5_irs_is_quiesced(vm_id);
	if (ret)
		goto out_unlock;

	/* Save the LPI IST to the userspace buffer. */
	ret = vgic_v5_save_lpi_ist(kvm, &ist_attr);
	if (ret)
		goto out_unlock;

	ret = vgic_v5_irs_is_quiesced(vm_id);
	if (ret)
		goto out_unlock;

out_unlock:
	mutex_unlock(&kvm->arch.config_lock);
	kvm_unlock_all_vcpus(kvm);
	mutex_unlock(&kvm->lock);

	return ret;
}

/* Allocate the LPI IST to restore into */
static int vgic_v5_restore_lpi_ist_alloc(struct kvm *kvm, bool *allocated)
{
	unsigned int id_bits;
	int ret;

	*allocated = false;

	ret = vgic_v5_irs_lpi_ist_id_bits(kvm, &id_bits);
	if (ret <= 0)
		return ret;

	ret = vgic_v5_lpi_ist_alloc(kvm, id_bits);
	if (ret)
		return ret;

	*allocated = true;

	return 0;
}

/*
 * Clean up the LPI IST if we allocated it, and restore the VMTE to the
 * original, valid state.
 */
static void vgic_v5_restore_cleanup(struct kvm *kvm,
				    struct kvm_vcpu *vcpu,
				    bool lpi_ist_allocated)
{
	/*
	 * We are on the restore failure path, so we do a best-effort
	 * cleanup. These commands might fail, but at this stage this is the
	 * best we can realistically do.
	 */
	if (lpi_ist_allocated) {
		if (!vgic_v5_send_command(vcpu, VMTE_MAKE_INVALID))
			vgic_v5_lpi_ist_free(kvm);
	}

	vgic_v5_send_command(vcpu, VMTE_MAKE_VALID);
}

int vgic_v5_irs_restore_ists(struct kvm *kvm, struct kvm_device_attr *attr)
{
	bool lpi_ist_allocated = false, vmte_invalid = false;
	struct kvm_vcpu *vcpu0 = kvm_get_vcpu(kvm, 0);
	struct kvm_vgic_v5_ist ist_attr;
	int ret = 0;

	mutex_lock(&kvm->lock);

	if (kvm_trylock_all_vcpus(kvm)) {
		mutex_unlock(&kvm->lock);
		return -EBUSY;
	}

	mutex_lock(&kvm->arch.config_lock);

	if (!vgic_initialized(kvm)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	if (kvm_vm_has_ran_once(kvm)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	ret = vgic_v5_copy_ist_attr(attr, &ist_attr);
	if (ret)
		goto out_unlock;

	ret = vgic_v5_validate_ist_attr(kvm, &ist_attr);
	if (ret)
		goto out_unlock;

	ret = vgic_v5_lpi_ist_exists(kvm);
	if (ret) {
		if (ret > 0)
			ret = -EBUSY;
		goto out_unlock;
	}

	/*
	 * If the guest has previously allocated an IST (which we check based on
	 * the IRS_IST_BASER), extract the number of LPI ID bits from the
	 * IRS_IST_CFGR. Else, do nothing.
	 *
	 * We do this before making the VMTE invalid as we rely on
	 * IRS_VMAP_VISTR to mark the IST as valid in the VMTE. This can only
	 * happen while the VMTE is valid.
	 */
	ret = vgic_v5_restore_lpi_ist_alloc(kvm, &lpi_ist_allocated);
	if (ret)
		goto out_unlock;

	/*
	 * Host ISTs are updated while the VMTE is invalid, so the GIC cannot
	 * observe partially restored state.
	 */
	ret = vgic_v5_send_command(vcpu0, VMTE_MAKE_INVALID);
	if (ret) {
		/*
		 * If invalidation fails, the restore cannot safely update host
		 * IST state.
		 */
		goto out_unlock;
	}
	vmte_invalid = true;

	/* Restore the SPI IST from the userspace buffer. */
	ret = vgic_v5_restore_spi_ist(kvm, &ist_attr);
	if (ret)
		goto out_unlock;

	/* Restore the LPI IST from the userspace buffer. */
	if (lpi_ist_allocated) {
		ret = vgic_v5_restore_lpi_ist(kvm, &ist_attr);
		if (ret)
			goto out_unlock;
	}

	/* And make the VM Valid again */
	ret = vgic_v5_send_command(vcpu0, VMTE_MAKE_VALID);
	if (ret)
		goto out_unlock;
	vmte_invalid = false;

	/*
	 * As part of restoring the ISTs, and previously pending interrupts have
	 * been tracked and made non-pending. Now that the ISTs have been
	 * restored, and the VM is valid again, restore the pending interrupts.
	 */
	ret = vgic_v5_restore_pending_irqs(kvm);
	if (ret)
		goto out_unlock;

	kvm->arch.vgic.vgic_v5_irs_data->lpi_ist_restore_pending = false;

out_unlock:
	if (ret && (vmte_invalid || lpi_ist_allocated)) {
		vgic_v5_discard_pending_irqs(kvm);
		vgic_v5_restore_cleanup(kvm, vcpu0, lpi_ist_allocated);
	}

	mutex_unlock(&kvm->arch.config_lock);
	kvm_unlock_all_vcpus(kvm);
	mutex_unlock(&kvm->lock);

	return ret;
}
