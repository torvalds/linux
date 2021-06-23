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

#include <nvhe/mm.h>

#define for_each_s2mpu(i) \
	for ((i) = &kvm_hyp_s2mpus[0]; (i) != &kvm_hyp_s2mpus[kvm_hyp_nr_s2mpus]; (i)++)

#define for_each_powered_s2mpu(i) \
	for_each_s2mpu((i)) if (is_powered_on((i)))

size_t __ro_after_init		kvm_hyp_nr_s2mpus;
struct s2mpu __ro_after_init	*kvm_hyp_s2mpus;

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
};
