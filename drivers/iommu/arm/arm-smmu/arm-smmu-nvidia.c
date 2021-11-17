// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2019-2020 NVIDIA CORPORATION.  All rights reserved.

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <soc/tegra/mc.h>

#include "arm-smmu.h"

/*
 * Tegra194 has three ARM MMU-500 Instances.
 * Two of them are used together and must be programmed identically for
 * interleaved IOVA accesses across them and translates accesses from
 * non-isochronous HW devices.
 * Third one is used for translating accesses from isochronous HW devices.
 *
 * In addition, the SMMU driver needs to coordinate with the memory controller
 * driver to ensure that the right SID override is programmed for any given
 * memory client. This is necessary to allow for use-case such as seamlessly
 * handing over the display controller configuration from the firmware to the
 * kernel.
 *
 * This implementation supports programming of the two instances that must
 * be programmed identically and takes care of invoking the memory controller
 * driver for SID override programming after devices have been attached to an
 * SMMU instance.
 */
#define MAX_SMMU_INSTANCES 2

struct nvidia_smmu {
	struct arm_smmu_device smmu;
	void __iomem *bases[MAX_SMMU_INSTANCES];
	unsigned int num_instances;
	struct tegra_mc *mc;
};

static inline struct nvidia_smmu *to_nvidia_smmu(struct arm_smmu_device *smmu)
{
	return container_of(smmu, struct nvidia_smmu, smmu);
}

static inline void __iomem *nvidia_smmu_page(struct arm_smmu_device *smmu,
					     unsigned int inst, int page)
{
	struct nvidia_smmu *nvidia_smmu;

	nvidia_smmu = container_of(smmu, struct nvidia_smmu, smmu);
	return nvidia_smmu->bases[inst] + (page << smmu->pgshift);
}

static u32 nvidia_smmu_read_reg(struct arm_smmu_device *smmu,
				int page, int offset)
{
	void __iomem *reg = nvidia_smmu_page(smmu, 0, page) + offset;

	return readl_relaxed(reg);
}

static void nvidia_smmu_write_reg(struct arm_smmu_device *smmu,
				  int page, int offset, u32 val)
{
	struct nvidia_smmu *nvidia = to_nvidia_smmu(smmu);
	unsigned int i;

	for (i = 0; i < nvidia->num_instances; i++) {
		void __iomem *reg = nvidia_smmu_page(smmu, i, page) + offset;

		writel_relaxed(val, reg);
	}
}

static u64 nvidia_smmu_read_reg64(struct arm_smmu_device *smmu,
				  int page, int offset)
{
	void __iomem *reg = nvidia_smmu_page(smmu, 0, page) + offset;

	return readq_relaxed(reg);
}

static void nvidia_smmu_write_reg64(struct arm_smmu_device *smmu,
				    int page, int offset, u64 val)
{
	struct nvidia_smmu *nvidia = to_nvidia_smmu(smmu);
	unsigned int i;

	for (i = 0; i < nvidia->num_instances; i++) {
		void __iomem *reg = nvidia_smmu_page(smmu, i, page) + offset;

		writeq_relaxed(val, reg);
	}
}

static void nvidia_smmu_tlb_sync(struct arm_smmu_device *smmu, int page,
				 int sync, int status)
{
	struct nvidia_smmu *nvidia = to_nvidia_smmu(smmu);
	unsigned int delay;

	arm_smmu_writel(smmu, page, sync, 0);

	for (delay = 1; delay < TLB_LOOP_TIMEOUT; delay *= 2) {
		unsigned int spin_cnt;

		for (spin_cnt = TLB_SPIN_COUNT; spin_cnt > 0; spin_cnt--) {
			u32 val = 0;
			unsigned int i;

			for (i = 0; i < nvidia->num_instances; i++) {
				void __iomem *reg;

				reg = nvidia_smmu_page(smmu, i, page) + status;
				val |= readl_relaxed(reg);
			}

			if (!(val & ARM_SMMU_sTLBGSTATUS_GSACTIVE))
				return;

			cpu_relax();
		}

		udelay(delay);
	}

	dev_err_ratelimited(smmu->dev,
			    "TLB sync timed out -- SMMU may be deadlocked\n");
}

static int nvidia_smmu_reset(struct arm_smmu_device *smmu)
{
	struct nvidia_smmu *nvidia = to_nvidia_smmu(smmu);
	unsigned int i;

	for (i = 0; i < nvidia->num_instances; i++) {
		u32 val;
		void __iomem *reg = nvidia_smmu_page(smmu, i, ARM_SMMU_GR0) +
				    ARM_SMMU_GR0_sGFSR;

		/* clear global FSR */
		val = readl_relaxed(reg);
		writel_relaxed(val, reg);
	}

	return 0;
}

static irqreturn_t nvidia_smmu_global_fault_inst(int irq,
						 struct arm_smmu_device *smmu,
						 int inst)
{
	u32 gfsr, gfsynr0, gfsynr1, gfsynr2;
	void __iomem *gr0_base = nvidia_smmu_page(smmu, inst, 0);

	gfsr = readl_relaxed(gr0_base + ARM_SMMU_GR0_sGFSR);
	if (!gfsr)
		return IRQ_NONE;

	gfsynr0 = readl_relaxed(gr0_base + ARM_SMMU_GR0_sGFSYNR0);
	gfsynr1 = readl_relaxed(gr0_base + ARM_SMMU_GR0_sGFSYNR1);
	gfsynr2 = readl_relaxed(gr0_base + ARM_SMMU_GR0_sGFSYNR2);

	dev_err_ratelimited(smmu->dev,
			    "Unexpected global fault, this could be serious\n");
	dev_err_ratelimited(smmu->dev,
			    "\tGFSR 0x%08x, GFSYNR0 0x%08x, GFSYNR1 0x%08x, GFSYNR2 0x%08x\n",
			    gfsr, gfsynr0, gfsynr1, gfsynr2);

	writel_relaxed(gfsr, gr0_base + ARM_SMMU_GR0_sGFSR);
	return IRQ_HANDLED;
}

static irqreturn_t nvidia_smmu_global_fault(int irq, void *dev)
{
	unsigned int inst;
	irqreturn_t ret = IRQ_NONE;
	struct arm_smmu_device *smmu = dev;
	struct nvidia_smmu *nvidia = to_nvidia_smmu(smmu);

	for (inst = 0; inst < nvidia->num_instances; inst++) {
		irqreturn_t irq_ret;

		irq_ret = nvidia_smmu_global_fault_inst(irq, smmu, inst);
		if (irq_ret == IRQ_HANDLED)
			ret = IRQ_HANDLED;
	}

	return ret;
}

static irqreturn_t nvidia_smmu_context_fault_bank(int irq,
						  struct arm_smmu_device *smmu,
						  int idx, int inst)
{
	u32 fsr, fsynr, cbfrsynra;
	unsigned long iova;
	void __iomem *gr1_base = nvidia_smmu_page(smmu, inst, 1);
	void __iomem *cb_base = nvidia_smmu_page(smmu, inst, smmu->numpage + idx);

	fsr = readl_relaxed(cb_base + ARM_SMMU_CB_FSR);
	if (!(fsr & ARM_SMMU_FSR_FAULT))
		return IRQ_NONE;

	fsynr = readl_relaxed(cb_base + ARM_SMMU_CB_FSYNR0);
	iova = readq_relaxed(cb_base + ARM_SMMU_CB_FAR);
	cbfrsynra = readl_relaxed(gr1_base + ARM_SMMU_GR1_CBFRSYNRA(idx));

	dev_err_ratelimited(smmu->dev,
			    "Unhandled context fault: fsr=0x%x, iova=0x%08lx, fsynr=0x%x, cbfrsynra=0x%x, cb=%d\n",
			    fsr, iova, fsynr, cbfrsynra, idx);

	writel_relaxed(fsr, cb_base + ARM_SMMU_CB_FSR);
	return IRQ_HANDLED;
}

static irqreturn_t nvidia_smmu_context_fault(int irq, void *dev)
{
	int idx;
	unsigned int inst;
	irqreturn_t ret = IRQ_NONE;
	struct arm_smmu_device *smmu;
	struct iommu_domain *domain = dev;
	struct arm_smmu_domain *smmu_domain;
	struct nvidia_smmu *nvidia;

	smmu_domain = container_of(domain, struct arm_smmu_domain, domain);
	smmu = smmu_domain->smmu;
	nvidia = to_nvidia_smmu(smmu);

	for (inst = 0; inst < nvidia->num_instances; inst++) {
		irqreturn_t irq_ret;

		/*
		 * Interrupt line is shared between all contexts.
		 * Check for faults across all contexts.
		 */
		for (idx = 0; idx < smmu->num_context_banks; idx++) {
			irq_ret = nvidia_smmu_context_fault_bank(irq, smmu,
								 idx, inst);
			if (irq_ret == IRQ_HANDLED)
				ret = IRQ_HANDLED;
		}
	}

	return ret;
}

static void nvidia_smmu_probe_finalize(struct arm_smmu_device *smmu, struct device *dev)
{
	struct nvidia_smmu *nvidia = to_nvidia_smmu(smmu);
	int err;

	err = tegra_mc_probe_device(nvidia->mc, dev);
	if (err < 0)
		dev_err(smmu->dev, "memory controller probe failed for %s: %d\n",
			dev_name(dev), err);
}

static const struct arm_smmu_impl nvidia_smmu_impl = {
	.read_reg = nvidia_smmu_read_reg,
	.write_reg = nvidia_smmu_write_reg,
	.read_reg64 = nvidia_smmu_read_reg64,
	.write_reg64 = nvidia_smmu_write_reg64,
	.reset = nvidia_smmu_reset,
	.tlb_sync = nvidia_smmu_tlb_sync,
	.global_fault = nvidia_smmu_global_fault,
	.context_fault = nvidia_smmu_context_fault,
	.probe_finalize = nvidia_smmu_probe_finalize,
};

static const struct arm_smmu_impl nvidia_smmu_single_impl = {
	.probe_finalize = nvidia_smmu_probe_finalize,
};

struct arm_smmu_device *nvidia_smmu_impl_init(struct arm_smmu_device *smmu)
{
	struct resource *res;
	struct device *dev = smmu->dev;
	struct nvidia_smmu *nvidia_smmu;
	struct platform_device *pdev = to_platform_device(dev);
	unsigned int i;

	nvidia_smmu = devm_krealloc(dev, smmu, sizeof(*nvidia_smmu), GFP_KERNEL);
	if (!nvidia_smmu)
		return ERR_PTR(-ENOMEM);

	nvidia_smmu->mc = devm_tegra_memory_controller_get(dev);
	if (IS_ERR(nvidia_smmu->mc))
		return ERR_CAST(nvidia_smmu->mc);

	/* Instance 0 is ioremapped by arm-smmu.c. */
	nvidia_smmu->bases[0] = smmu->base;
	nvidia_smmu->num_instances++;

	for (i = 1; i < MAX_SMMU_INSTANCES; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			break;

		nvidia_smmu->bases[i] = devm_ioremap_resource(dev, res);
		if (IS_ERR(nvidia_smmu->bases[i]))
			return ERR_CAST(nvidia_smmu->bases[i]);

		nvidia_smmu->num_instances++;
	}

	if (nvidia_smmu->num_instances == 1)
		nvidia_smmu->smmu.impl = &nvidia_smmu_single_impl;
	else
		nvidia_smmu->smmu.impl = &nvidia_smmu_impl;

	return &nvidia_smmu->smmu;
}
