// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Raspberry Pi Ltd., All Rights Reserved.
 * Copyright (c) 2024 SUSE
 */

#include <linux/bitmap.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include <linux/irqchip/irq-msi-lib.h>

#define MIP_INT_RAISE		0x00
#define MIP_INT_CLEAR		0x10
#define MIP_INT_CFGL_HOST	0x20
#define MIP_INT_CFGH_HOST	0x30
#define MIP_INT_MASKL_HOST	0x40
#define MIP_INT_MASKH_HOST	0x50
#define MIP_INT_MASKL_VPU	0x60
#define MIP_INT_MASKH_VPU	0x70
#define MIP_INT_STATUSL_HOST	0x80
#define MIP_INT_STATUSH_HOST	0x90
#define MIP_INT_STATUSL_VPU	0xa0
#define MIP_INT_STATUSH_VPU	0xb0

/**
 * struct mip_priv - MSI-X interrupt controller data
 * @lock:	Used to protect bitmap alloc/free
 * @base:	Base address of MMIO area
 * @msg_addr:	PCIe MSI-X address
 * @msi_base:	MSI base
 * @num_msis:	Count of MSIs
 * @msi_offset:	MSI offset
 * @bitmap:	A bitmap for hwirqs
 * @parent:	Parent domain (GIC)
 * @dev:	A device pointer
 */
struct mip_priv {
	spinlock_t		lock;
	void __iomem		*base;
	u64			msg_addr;
	u32			msi_base;
	u32			num_msis;
	u32			msi_offset;
	unsigned long		*bitmap;
	struct irq_domain	*parent;
	struct device		*dev;
};

static void mip_compose_msi_msg(struct irq_data *d, struct msi_msg *msg)
{
	struct mip_priv *mip = irq_data_get_irq_chip_data(d);

	msg->address_hi = upper_32_bits(mip->msg_addr);
	msg->address_lo = lower_32_bits(mip->msg_addr);
	msg->data = d->hwirq;
}

static struct irq_chip mip_middle_irq_chip = {
	.name			= "MIP",
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.irq_set_type		= irq_chip_set_type_parent,
	.irq_compose_msi_msg	= mip_compose_msi_msg,
};

static int mip_alloc_hwirq(struct mip_priv *mip, unsigned int nr_irqs)
{
	guard(spinlock)(&mip->lock);
	return bitmap_find_free_region(mip->bitmap, mip->num_msis, ilog2(nr_irqs));
}

static void mip_free_hwirq(struct mip_priv *mip, unsigned int hwirq,
			   unsigned int nr_irqs)
{
	guard(spinlock)(&mip->lock);
	bitmap_release_region(mip->bitmap, hwirq, ilog2(nr_irqs));
}

static int mip_middle_domain_alloc(struct irq_domain *domain, unsigned int virq,
				   unsigned int nr_irqs, void *arg)
{
	struct mip_priv *mip = domain->host_data;
	struct irq_fwspec fwspec = {0};
	unsigned int hwirq, i;
	struct irq_data *irqd;
	int irq, ret;

	irq = mip_alloc_hwirq(mip, nr_irqs);
	if (irq < 0)
		return irq;

	hwirq = irq + mip->msi_offset;

	fwspec.fwnode = domain->parent->fwnode;
	fwspec.param_count = 3;
	fwspec.param[0] = 0;
	fwspec.param[1] = hwirq + mip->msi_base;
	fwspec.param[2] = IRQ_TYPE_EDGE_RISING;

	ret = irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, &fwspec);
	if (ret)
		goto err_free_hwirq;

	for (i = 0; i < nr_irqs; i++) {
		irqd = irq_domain_get_irq_data(domain->parent, virq + i);
		irqd->chip->irq_set_type(irqd, IRQ_TYPE_EDGE_RISING);

		ret = irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
						    &mip_middle_irq_chip, mip);
		if (ret)
			goto err_free;

		irqd = irq_get_irq_data(virq + i);
		irqd_set_single_target(irqd);
		irqd_set_affinity_on_activate(irqd);
	}

	return 0;

err_free:
	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
err_free_hwirq:
	mip_free_hwirq(mip, irq, nr_irqs);
	return ret;
}

static void mip_middle_domain_free(struct irq_domain *domain, unsigned int virq,
				   unsigned int nr_irqs)
{
	struct irq_data *irqd = irq_domain_get_irq_data(domain, virq);
	struct mip_priv *mip;
	unsigned int hwirq;

	if (!irqd)
		return;

	mip = irq_data_get_irq_chip_data(irqd);
	hwirq = irqd_to_hwirq(irqd);
	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
	mip_free_hwirq(mip, hwirq - mip->msi_offset, nr_irqs);
}

static const struct irq_domain_ops mip_middle_domain_ops = {
	.select		= msi_lib_irq_domain_select,
	.alloc		= mip_middle_domain_alloc,
	.free		= mip_middle_domain_free,
};

#define MIP_MSI_FLAGS_REQUIRED	(MSI_FLAG_USE_DEF_DOM_OPS |	\
				 MSI_FLAG_USE_DEF_CHIP_OPS |	\
				 MSI_FLAG_PCI_MSI_MASK_PARENT)

#define MIP_MSI_FLAGS_SUPPORTED	(MSI_GENERIC_FLAGS_MASK |	\
				 MSI_FLAG_MULTI_PCI_MSI |	\
				 MSI_FLAG_PCI_MSIX)

static const struct msi_parent_ops mip_msi_parent_ops = {
	.supported_flags	= MIP_MSI_FLAGS_SUPPORTED,
	.required_flags		= MIP_MSI_FLAGS_REQUIRED,
	.chip_flags		= MSI_CHIP_FLAG_SET_EOI | MSI_CHIP_FLAG_SET_ACK,
	.bus_select_token       = DOMAIN_BUS_GENERIC_MSI,
	.bus_select_mask	= MATCH_PCI_MSI,
	.prefix			= "MIP-MSI-",
	.init_dev_msi_info	= msi_lib_init_dev_msi_info,
};

static int mip_init_domains(struct mip_priv *mip, struct device_node *np)
{
	struct irq_domain_info info = {
		.fwnode		= of_fwnode_handle(np),
		.ops		= &mip_middle_domain_ops,
		.host_data	= mip,
		.size		= mip->num_msis,
		.parent		= mip->parent,
		.dev		= mip->dev,
	};

	if (!msi_create_parent_irq_domain(&info, &mip_msi_parent_ops))
		return -ENOMEM;

	/*
	 * All MSI-X unmasked for the host, masked for the VPU, and edge-triggered.
	 */
	writel(0, mip->base + MIP_INT_MASKL_HOST);
	writel(0, mip->base + MIP_INT_MASKH_HOST);
	writel(~0, mip->base + MIP_INT_MASKL_VPU);
	writel(~0, mip->base + MIP_INT_MASKH_VPU);
	writel(~0, mip->base + MIP_INT_CFGL_HOST);
	writel(~0, mip->base + MIP_INT_CFGH_HOST);

	return 0;
}

static int mip_parse_dt(struct mip_priv *mip, struct device_node *np)
{
	struct of_phandle_args args;
	u64 size;
	int ret;

	ret = of_property_read_u32(np, "brcm,msi-offset", &mip->msi_offset);
	if (ret)
		mip->msi_offset = 0;

	ret = of_parse_phandle_with_args(np, "msi-ranges", "#interrupt-cells",
					 0, &args);
	if (ret)
		return ret;

	ret = of_property_read_u32_index(np, "msi-ranges", args.args_count + 1,
					 &mip->num_msis);
	if (ret)
		goto err_put;

	ret = of_property_read_reg(np, 1, &mip->msg_addr, &size);
	if (ret)
		goto err_put;

	mip->msi_base = args.args[1];

	mip->parent = irq_find_host(args.np);
	if (!mip->parent)
		ret = -EINVAL;

err_put:
	of_node_put(args.np);
	return ret;
}

static int __init mip_of_msi_init(struct device_node *node, struct device_node *parent)
{
	struct platform_device *pdev;
	struct mip_priv *mip;
	int ret;

	pdev = of_find_device_by_node(node);
	of_node_put(node);
	if (!pdev)
		return -EPROBE_DEFER;

	mip = kzalloc(sizeof(*mip), GFP_KERNEL);
	if (!mip)
		return -ENOMEM;

	spin_lock_init(&mip->lock);
	mip->dev = &pdev->dev;

	ret = mip_parse_dt(mip, node);
	if (ret)
		goto err_priv;

	mip->base = of_iomap(node, 0);
	if (!mip->base) {
		ret = -ENXIO;
		goto err_priv;
	}

	mip->bitmap = bitmap_zalloc(mip->num_msis, GFP_KERNEL);
	if (!mip->bitmap) {
		ret = -ENOMEM;
		goto err_base;
	}

	ret = mip_init_domains(mip, node);
	if (ret)
		goto err_map;

	dev_dbg(&pdev->dev, "MIP: MSI-X count: %u, base: %u, offset: %u, msg_addr: %llx\n",
		mip->num_msis, mip->msi_base, mip->msi_offset, mip->msg_addr);

	return 0;

err_map:
	bitmap_free(mip->bitmap);
err_base:
	iounmap(mip->base);
err_priv:
	kfree(mip);
	return ret;
}

IRQCHIP_PLATFORM_DRIVER_BEGIN(mip_msi)
IRQCHIP_MATCH("brcm,bcm2712-mip", mip_of_msi_init)
IRQCHIP_PLATFORM_DRIVER_END(mip_msi)
MODULE_DESCRIPTION("Broadcom BCM2712 MSI-X interrupt controller");
MODULE_AUTHOR("Phil Elwell <phil@raspberrypi.com>");
MODULE_AUTHOR("Stanimir Varbanov <svarbanov@suse.de>");
MODULE_LICENSE("GPL");
