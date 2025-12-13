// SPDX-License-Identifier: GPL-2.0+
/*
 * APM X-Gene MSI Driver
 *
 * Copyright (c) 2014, Applied Micro Circuits Corporation
 * Author: Tanmay Inamdar <tinamdar@apm.com>
 *	   Duc Dang <dhdang@apm.com>
 */
#include <linux/bitfield.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqchip/irq-msi-lib.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/of_pci.h>

#define MSI_IR0			0x000000
#define MSI_INT0		0x800000
#define IDX_PER_GROUP		8
#define IRQS_PER_IDX		16
#define NR_HW_IRQS		16
#define NR_MSI_BITS		(IDX_PER_GROUP * IRQS_PER_IDX * NR_HW_IRQS)
#define NR_MSI_VEC		(NR_MSI_BITS / num_possible_cpus())

#define MSI_GROUP_MASK		GENMASK(22, 19)
#define MSI_INDEX_MASK		GENMASK(18, 16)
#define MSI_INTR_MASK		GENMASK(19, 16)

#define MSInRx_HWIRQ_MASK	GENMASK(6, 4)
#define DATA_HWIRQ_MASK		GENMASK(3, 0)

struct xgene_msi {
	struct irq_domain	*inner_domain;
	u64			msi_addr;
	void __iomem		*msi_regs;
	unsigned long		*bitmap;
	struct mutex		bitmap_lock;
	unsigned int		gic_irq[NR_HW_IRQS];
};

/* Global data */
static struct xgene_msi *xgene_msi_ctrl;

/*
 * X-Gene v1 has 16 frames of MSI termination registers MSInIRx, where n is
 * frame number (0..15), x is index of registers in each frame (0..7).  Each
 * 32b register is at the beginning of a 64kB region, each frame occupying
 * 512kB (and the whole thing 8MB of PA space).
 *
 * Each register supports 16 MSI vectors (0..15) to generate interrupts. A
 * write to the MSInIRx from the PCI side generates an interrupt. A read
 * from the MSInRx on the CPU side returns a bitmap of the pending MSIs in
 * the lower 16 bits. A side effect of this read is that all pending
 * interrupts are acknowledged and cleared).
 *
 * Additionally, each MSI termination frame has 1 MSIINTn register (n is
 * 0..15) to indicate the MSI pending status caused by any of its 8
 * termination registers, reported as a bitmap in the lower 8 bits. Each 32b
 * register is at the beginning of a 64kB region (and overall occupying an
 * extra 1MB).
 *
 * There is one GIC IRQ assigned for each MSI termination frame, 16 in
 * total.
 *
 * The register layout is as follows:
 * MSI0IR0			base_addr
 * MSI0IR1			base_addr +  0x10000
 * ...				...
 * MSI0IR6			base_addr +  0x60000
 * MSI0IR7			base_addr +  0x70000
 * MSI1IR0			base_addr +  0x80000
 * MSI1IR1			base_addr +  0x90000
 * ...				...
 * MSI1IR7			base_addr +  0xF0000
 * MSI2IR0			base_addr + 0x100000
 * ...				...
 * MSIFIR0			base_addr + 0x780000
 * MSIFIR1			base_addr + 0x790000
 * ...				...
 * MSIFIR7			base_addr + 0x7F0000
 * MSIINT0			base_addr + 0x800000
 * MSIINT1			base_addr + 0x810000
 * ...				...
 * MSIINTF			base_addr + 0x8F0000
 */

/* MSInIRx read helper */
static u32 xgene_msi_ir_read(struct xgene_msi *msi, u32 msi_grp, u32 msir_idx)
{
	return readl_relaxed(msi->msi_regs + MSI_IR0 +
			     (FIELD_PREP(MSI_GROUP_MASK, msi_grp) |
			      FIELD_PREP(MSI_INDEX_MASK, msir_idx)));
}

/* MSIINTn read helper */
static u32 xgene_msi_int_read(struct xgene_msi *msi, u32 msi_grp)
{
	return readl_relaxed(msi->msi_regs + MSI_INT0 +
			     FIELD_PREP(MSI_INTR_MASK, msi_grp));
}

/*
 * In order to allow an MSI to be moved from one CPU to another without
 * having to repaint both the address and the data (which cannot be done
 * atomically), we statically partitions the MSI frames between CPUs. Given
 * that XGene-1 has 8 CPUs, each CPU gets two frames assigned to it
 *
 * We adopt the convention that when an MSI is moved, it is configured to
 * target the same register number in the congruent frame assigned to the
 * new target CPU. This reserves a given MSI across all CPUs, and reduces
 * the MSI capacity from 2048 to 256.
 *
 * Effectively, this amounts to:
 * - hwirq[7]::cpu[2:0] is the target frame number (n in MSInIRx)
 * - hwirq[6:4] is the register index in any given frame (x in MSInIRx)
 * - hwirq[3:0] is the MSI data
 */
static irq_hw_number_t compute_hwirq(u8 frame, u8 index, u8 data)
{
	return (FIELD_PREP(BIT(7), FIELD_GET(BIT(3), frame))	|
		FIELD_PREP(MSInRx_HWIRQ_MASK, index)		|
		FIELD_PREP(DATA_HWIRQ_MASK, data));
}

static void xgene_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct xgene_msi *msi = irq_data_get_irq_chip_data(data);
	u64 target_addr;
	u32 frame, msir;
	int cpu;

	cpu	= cpumask_first(irq_data_get_effective_affinity_mask(data));
	msir	= FIELD_GET(MSInRx_HWIRQ_MASK, data->hwirq);
	frame	= FIELD_PREP(BIT(3), FIELD_GET(BIT(7), data->hwirq)) | cpu;

	target_addr = msi->msi_addr;
	target_addr += (FIELD_PREP(MSI_GROUP_MASK, frame) |
			FIELD_PREP(MSI_INTR_MASK, msir));

	msg->address_hi = upper_32_bits(target_addr);
	msg->address_lo = lower_32_bits(target_addr);
	msg->data = FIELD_GET(DATA_HWIRQ_MASK, data->hwirq);
}

static int xgene_msi_set_affinity(struct irq_data *irqdata,
				  const struct cpumask *mask, bool force)
{
	int target_cpu = cpumask_first(mask);

	irq_data_update_effective_affinity(irqdata, cpumask_of(target_cpu));

	/* Force the core code to regenerate the message */
	return IRQ_SET_MASK_OK;
}

static struct irq_chip xgene_msi_bottom_irq_chip = {
	.name			= "MSI",
	.irq_set_affinity       = xgene_msi_set_affinity,
	.irq_compose_msi_msg	= xgene_compose_msi_msg,
};

static int xgene_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				  unsigned int nr_irqs, void *args)
{
	struct xgene_msi *msi = domain->host_data;
	irq_hw_number_t hwirq;

	mutex_lock(&msi->bitmap_lock);

	hwirq = find_first_zero_bit(msi->bitmap, NR_MSI_VEC);
	if (hwirq < NR_MSI_VEC)
		set_bit(hwirq, msi->bitmap);

	mutex_unlock(&msi->bitmap_lock);

	if (hwirq >= NR_MSI_VEC)
		return -ENOSPC;

	irq_domain_set_info(domain, virq, hwirq,
			    &xgene_msi_bottom_irq_chip, domain->host_data,
			    handle_simple_irq, NULL, NULL);
	irqd_set_resend_when_in_progress(irq_get_irq_data(virq));

	return 0;
}

static void xgene_irq_domain_free(struct irq_domain *domain,
				  unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct xgene_msi *msi = irq_data_get_irq_chip_data(d);

	mutex_lock(&msi->bitmap_lock);

	clear_bit(d->hwirq, msi->bitmap);

	mutex_unlock(&msi->bitmap_lock);

	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
}

static const struct irq_domain_ops xgene_msi_domain_ops = {
	.alloc  = xgene_irq_domain_alloc,
	.free   = xgene_irq_domain_free,
};

static const struct msi_parent_ops xgene_msi_parent_ops = {
	.supported_flags	= (MSI_GENERIC_FLAGS_MASK	|
				   MSI_FLAG_PCI_MSIX),
	.required_flags		= (MSI_FLAG_USE_DEF_DOM_OPS	|
				   MSI_FLAG_USE_DEF_CHIP_OPS),
	.bus_select_token	= DOMAIN_BUS_PCI_MSI,
	.init_dev_msi_info	= msi_lib_init_dev_msi_info,
};

static int xgene_allocate_domains(struct device_node *node,
				  struct xgene_msi *msi)
{
	struct irq_domain_info info = {
		.fwnode		= of_fwnode_handle(node),
		.ops		= &xgene_msi_domain_ops,
		.size		= NR_MSI_VEC,
		.host_data	= msi,
	};

	msi->inner_domain = msi_create_parent_irq_domain(&info, &xgene_msi_parent_ops);
	return msi->inner_domain ? 0 : -ENOMEM;
}

static int xgene_msi_init_allocator(struct device *dev)
{
	xgene_msi_ctrl->bitmap = devm_bitmap_zalloc(dev, NR_MSI_VEC, GFP_KERNEL);
	if (!xgene_msi_ctrl->bitmap)
		return -ENOMEM;

	mutex_init(&xgene_msi_ctrl->bitmap_lock);

	return 0;
}

static void xgene_msi_isr(struct irq_desc *desc)
{
	unsigned int *irqp = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct xgene_msi *xgene_msi = xgene_msi_ctrl;
	unsigned long grp_pending;
	int msir_idx;
	u32 msi_grp;

	chained_irq_enter(chip, desc);

	msi_grp = irqp - xgene_msi->gic_irq;

	grp_pending = xgene_msi_int_read(xgene_msi, msi_grp);

	for_each_set_bit(msir_idx, &grp_pending, IDX_PER_GROUP) {
		unsigned long msir;
		int intr_idx;

		msir = xgene_msi_ir_read(xgene_msi, msi_grp, msir_idx);

		for_each_set_bit(intr_idx, &msir, IRQS_PER_IDX) {
			irq_hw_number_t hwirq;
			int ret;

			hwirq = compute_hwirq(msi_grp, msir_idx, intr_idx);
			ret = generic_handle_domain_irq(xgene_msi->inner_domain,
							hwirq);
			WARN_ON_ONCE(ret);
		}
	}

	chained_irq_exit(chip, desc);
}

static void xgene_msi_remove(struct platform_device *pdev)
{
	for (int i = 0; i < NR_HW_IRQS; i++) {
		unsigned int irq = xgene_msi_ctrl->gic_irq[i];
		if (!irq)
			continue;
		irq_set_chained_handler_and_data(irq, NULL, NULL);
	}

	if (xgene_msi_ctrl->inner_domain)
		irq_domain_remove(xgene_msi_ctrl->inner_domain);
}

static int xgene_msi_handler_setup(struct platform_device *pdev)
{
	struct xgene_msi *xgene_msi = xgene_msi_ctrl;
	int i;

	for (i = 0; i < NR_HW_IRQS; i++) {
		u32 msi_val;
		int irq, err;

		/*
		 * MSInIRx registers are read-to-clear; before registering
		 * interrupt handlers, read all of them to clear spurious
		 * interrupts that may occur before the driver is probed.
		 */
		for (int msi_idx = 0; msi_idx < IDX_PER_GROUP; msi_idx++)
			xgene_msi_ir_read(xgene_msi, i, msi_idx);

		/* Read MSIINTn to confirm */
		msi_val = xgene_msi_int_read(xgene_msi, i);
		if (msi_val) {
			dev_err(&pdev->dev, "Failed to clear spurious IRQ\n");
			return -EINVAL;
		}

		irq = platform_get_irq(pdev, i);
		if (irq < 0)
			return irq;

		xgene_msi->gic_irq[i] = irq;

		/*
		 * Statically allocate MSI GIC IRQs to each CPU core.
		 * With 8-core X-Gene v1, 2 MSI GIC IRQs are allocated
		 * to each core.
		 */
		irq_set_status_flags(irq, IRQ_NO_BALANCING);
		err = irq_set_affinity(irq, cpumask_of(i % num_possible_cpus()));
		if (err) {
			pr_err("failed to set affinity for GIC IRQ");
			return err;
		}

		irq_set_chained_handler_and_data(irq, xgene_msi_isr,
						 &xgene_msi_ctrl->gic_irq[i]);
	}

	return 0;
}

static const struct of_device_id xgene_msi_match_table[] = {
	{.compatible = "apm,xgene1-msi"},
	{},
};

static int xgene_msi_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct xgene_msi *xgene_msi;
	int rc;

	xgene_msi_ctrl = devm_kzalloc(&pdev->dev, sizeof(*xgene_msi_ctrl),
				      GFP_KERNEL);
	if (!xgene_msi_ctrl)
		return -ENOMEM;

	xgene_msi = xgene_msi_ctrl;

	xgene_msi->msi_regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(xgene_msi->msi_regs)) {
		rc = PTR_ERR(xgene_msi->msi_regs);
		goto error;
	}
	xgene_msi->msi_addr = res->start;

	rc = xgene_msi_init_allocator(&pdev->dev);
	if (rc) {
		dev_err(&pdev->dev, "Error allocating MSI bitmap\n");
		goto error;
	}

	rc = xgene_allocate_domains(dev_of_node(&pdev->dev), xgene_msi);
	if (rc) {
		dev_err(&pdev->dev, "Failed to allocate MSI domain\n");
		goto error;
	}

	rc = xgene_msi_handler_setup(pdev);
	if (rc)
		goto error;

	dev_info(&pdev->dev, "APM X-Gene PCIe MSI driver loaded\n");

	return 0;
error:
	xgene_msi_remove(pdev);
	return rc;
}

static struct platform_driver xgene_msi_driver = {
	.driver = {
		.name = "xgene-msi",
		.of_match_table = xgene_msi_match_table,
	},
	.probe = xgene_msi_probe,
	.remove = xgene_msi_remove,
};
builtin_platform_driver(xgene_msi_driver);
