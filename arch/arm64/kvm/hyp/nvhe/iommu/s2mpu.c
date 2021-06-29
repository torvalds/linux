// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#include <linux/kvm_host.h>

#include <asm/kvm_asm.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_s2mpu.h>

#include <linux/arm-smccc.h>

#include <nvhe/mm.h>
#include <nvhe/spinlock.h>
#include <nvhe/trap_handler.h>

#define SMC_CMD_PREPARE_PD_ONOFF	0x82000410
#define SMC_MODE_POWER_UP		1

#define for_each_s2mpu(i) \
	for ((i) = &kvm_hyp_s2mpus[0]; (i) != &kvm_hyp_s2mpus[kvm_hyp_nr_s2mpus]; (i)++)

#define for_each_powered_s2mpu(i) \
	for_each_s2mpu((i)) if (is_powered_on((i)))

size_t __ro_after_init		kvm_hyp_nr_s2mpus;
struct s2mpu __ro_after_init	*kvm_hyp_s2mpus;

static hyp_spinlock_t		s2mpu_lock;

static bool is_version(struct s2mpu *dev, u32 version)
{
	return (dev->version & VERSION_CHECK_MASK) == version;
}

static bool is_powered_on(struct s2mpu *dev)
{
	switch (dev->power_state) {
	case S2MPU_POWER_ALWAYS_ON:
	case S2MPU_POWER_ON:
		return true;
	case S2MPU_POWER_OFF:
		return false;
	default:
		BUG();
	}
}

static bool is_in_power_domain(struct s2mpu *dev, u64 power_domain_id)
{
	switch (dev->power_state) {
	case S2MPU_POWER_ALWAYS_ON:
		return false;
	case S2MPU_POWER_ON:
	case S2MPU_POWER_OFF:
		return dev->power_domain_id == power_domain_id;
	default:
		BUG();
	}
}

/*
 * Write CONTEXT_CFG_VALID_VID configuration before touching L1ENTRY* registers.
 * Writes to those registers are ignored unless there is a context ID allocated
 * to the corresponding VID (v9 only).
 */
static void __set_context_ids(struct s2mpu *dev)
{
	if (!is_version(dev, S2MPU_VERSION_9))
		return;

	writel_relaxed(dev->context_cfg_valid_vid,
		       dev->va + REG_NS_CONTEXT_CFG_VALID_VID);
}

static void __set_control_regs(struct s2mpu *dev)
{
	u32 ctrl0 = 0, irq_vids;

	/*
	 * Note: We set the values of CTRL0, CTRL1 and CFG registers here but we
	 * still rely on the correctness of their reset values. S2MPUs *must*
	 * reset to a state where all DMA traffic is blocked until the hypervisor
	 * writes its configuration to the S2MPU. A malicious EL1 could otherwise
	 * attempt to bypass the permission checks in the window between powering
	 * on the S2MPU and this function being called.
	 */

	/* Enable the S2MPU, otherwise all traffic would be allowed through. */
	ctrl0 |= CTRL0_ENABLE;

	/*
	 * Enable interrupts on fault for all VIDs. The IRQ must also be
	 * specified in DT to get unmasked in the GIC.
	 */
	ctrl0 |= CTRL0_INTERRUPT_ENABLE;
	irq_vids = ALL_VIDS_BITMAP;

	/* Return SLVERR/DECERR to device on permission fault. */
	ctrl0 |= is_version(dev, S2MPU_VERSION_9) ? CTRL0_FAULT_RESP_TYPE_DECERR
						  : CTRL0_FAULT_RESP_TYPE_SLVERR;

	writel_relaxed(irq_vids, dev->va + REG_NS_INTERRUPT_ENABLE_PER_VID_SET);
	writel_relaxed(0, dev->va + REG_NS_CFG);
	writel_relaxed(0, dev->va + REG_NS_CTRL1);
	writel_relaxed(ctrl0, dev->va + REG_NS_CTRL0);
}

static void __all_invalidation(struct s2mpu *dev)
{
	writel_relaxed(INVALIDATION_INVALIDATE,
		       dev->va + REG_NS_ALL_INVALIDATION);
}

static void __set_l1entry_attr_with_prot(struct s2mpu *dev, unsigned int gb,
					 unsigned int vid, enum mpt_prot prot)
{
	writel_relaxed(L1ENTRY_ATTR_1G(prot),
		       dev->va + REG_NS_L1ENTRY_ATTR(vid, gb));
}

/**
 * Initialize S2MPU device and set all GB regions to 1G granularity with
 * given protection bits.
 */
static void initialize_with_prot(struct s2mpu *dev, enum mpt_prot prot)
{
	unsigned int gb, vid;

	/* Must write CONTEXT_CFG_VALID_VID before setting L1ENTRY registers. */
	__set_context_ids(dev);

	for_each_gb_and_vid(gb, vid)
		__set_l1entry_attr_with_prot(dev, gb, vid, prot);
	__all_invalidation(dev);

	/* Set control registers, enable the S2MPU. */
	__set_control_regs(dev);
}

static bool s2mpu_host_smc_handler(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(u64, fn, host_ctxt, 0);
	DECLARE_REG(u64, mode, host_ctxt, 1);
	DECLARE_REG(u64, domain_id, host_ctxt, 2);
	DECLARE_REG(u64, group, host_ctxt, 3);

	struct arm_smccc_res res;
	struct s2mpu *dev;

	if (fn != SMC_CMD_PREPARE_PD_ONOFF)
		return false; /* SMC not handled */

	/*
	 * Host is notifying EL3 that a power domain was turned on/off.
	 * Use this SMC as a trigger to program the S2MPUs.
	 * Note that the host may be malicious and issue this SMC arbitrarily.
	 *
	 * Power on:
	 * It is paramount that the S2MPU reset state is enabled and blocking
	 * all traffic. That way the host is forced to issue a power-on SMC to
	 * unblock the S2MPUs.
	 *
	 * Power down:
	 * A power-down SMC is a hint for hyp to stop updating the S2MPU, lest
	 * writes to powered-down MMIO registers produce SErrors in the host.
	 * However, hyp must perform one last update - putting the S2MPUs back
	 * to their blocking reset state - in case the host does not actually
	 * power them down and continues issuing DMA traffic.
	 */

	hyp_spin_lock(&s2mpu_lock);
	arm_smccc_1_1_smc(fn, mode, domain_id, group, &res);
	if (res.a0 == SMCCC_RET_SUCCESS) {
		for_each_s2mpu(dev) {
			if (!is_in_power_domain(dev, domain_id))
				continue;

			if (mode == SMC_MODE_POWER_UP) {
				dev->power_state = S2MPU_POWER_ON;
				initialize_with_prot(dev, MPT_PROT_RW);
			} else {
				initialize_with_prot(dev, MPT_PROT_NONE);
				dev->power_state = S2MPU_POWER_OFF;
			}
		}
	}
	hyp_spin_unlock(&s2mpu_lock);

	cpu_reg(host_ctxt, 0) = res.a0;
	return true;  /* SMC handled */
}

static int s2mpu_init(void)
{
	struct s2mpu *dev;
	int ret;

	/* Map data structures in EL2 stage-1. */
	ret = pkvm_create_mappings(kvm_hyp_s2mpus,
				   kvm_hyp_s2mpus + kvm_hyp_nr_s2mpus,
				   PAGE_HYP);
	if (ret)
		return ret;

	/* Map S2MPU MMIO regions in EL2 stage-1. */
	for_each_s2mpu(dev) {
		ret = __pkvm_create_private_mapping(
			dev->pa, S2MPU_MMIO_SIZE, PAGE_HYP_DEVICE,(unsigned long *)(&dev->va));
		if (ret)
			return ret;
	}

	/*
	 * Program all S2MPUs powered on at boot. Note that they may not be in
	 * the blocking reset state as the bootloader may have programmed them.
	 */
	for_each_powered_s2mpu(dev)
		initialize_with_prot(dev, MPT_PROT_RW);
	return 0;
}

const struct kvm_iommu_ops kvm_s2mpu_ops = (struct kvm_iommu_ops){
	.init = s2mpu_init,
	.host_smc_handler = s2mpu_host_smc_handler,
};
