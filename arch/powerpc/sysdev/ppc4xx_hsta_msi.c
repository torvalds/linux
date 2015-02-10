/*
 * MSI support for PPC4xx SoCs using High Speed Transfer Assist (HSTA) for
 * generation of the interrupt.
 *
 * Copyright © 2013 Alistair Popple <alistair@popple.id.au> IBM Corporation
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/semaphore.h>
#include <asm/msi_bitmap.h>

struct ppc4xx_hsta_msi {
	struct device *dev;

	/* The ioremapped HSTA MSI IO space */
	u32 __iomem *data;

	/* Physical address of HSTA MSI IO space */
	u64 address;
	struct msi_bitmap bmp;

	/* An array mapping offsets to hardware IRQs */
	int *irq_map;

	/* Number of hwirqs supported */
	int irq_count;
};
static struct ppc4xx_hsta_msi ppc4xx_hsta_msi;

static int hsta_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	struct msi_msg msg;
	struct msi_desc *entry;
	int irq, hwirq;
	u64 addr;

	/* We don't support MSI-X */
	if (type == PCI_CAP_ID_MSIX) {
		pr_debug("%s: MSI-X not supported.\n", __func__);
		return -EINVAL;
	}

	list_for_each_entry(entry, &dev->msi_list, list) {
		irq = msi_bitmap_alloc_hwirqs(&ppc4xx_hsta_msi.bmp, 1);
		if (irq < 0) {
			pr_debug("%s: Failed to allocate msi interrupt\n",
				 __func__);
			return irq;
		}

		hwirq = ppc4xx_hsta_msi.irq_map[irq];
		if (hwirq == NO_IRQ) {
			pr_err("%s: Failed mapping irq %d\n", __func__, irq);
			return -EINVAL;
		}

		/*
		 * HSTA generates interrupts on writes to 128-bit aligned
		 * addresses.
		 */
		addr = ppc4xx_hsta_msi.address + irq*0x10;
		msg.address_hi = upper_32_bits(addr);
		msg.address_lo = lower_32_bits(addr);

		/* Data is not used by the HSTA. */
		msg.data = 0;

		pr_debug("%s: Setup irq %d (0x%0llx)\n", __func__, hwirq,
			 (((u64) msg.address_hi) << 32) | msg.address_lo);

		if (irq_set_msi_desc(hwirq, entry)) {
			pr_err(
			"%s: Invalid hwirq %d specified in device tree\n",
			__func__, hwirq);
			msi_bitmap_free_hwirqs(&ppc4xx_hsta_msi.bmp, irq, 1);
			return -EINVAL;
		}
		pci_write_msi_msg(hwirq, &msg);
	}

	return 0;
}

static int hsta_find_hwirq_offset(int hwirq)
{
	int irq;

	/* Find the offset given the hwirq */
	for (irq = 0; irq < ppc4xx_hsta_msi.irq_count; irq++)
		if (ppc4xx_hsta_msi.irq_map[irq] == hwirq)
			return irq;

	return -EINVAL;
}

static void hsta_teardown_msi_irqs(struct pci_dev *dev)
{
	struct msi_desc *entry;
	int irq;

	list_for_each_entry(entry, &dev->msi_list, list) {
		if (entry->irq == NO_IRQ)
			continue;

		irq = hsta_find_hwirq_offset(entry->irq);

		/* entry->irq should always be in irq_map */
		BUG_ON(irq < 0);
		irq_set_msi_desc(entry->irq, NULL);
		msi_bitmap_free_hwirqs(&ppc4xx_hsta_msi.bmp, irq, 1);
		pr_debug("%s: Teardown IRQ %u (index %u)\n", __func__,
			 entry->irq, irq);
	}
}

static int hsta_msi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *mem;
	int irq, ret, irq_count;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (IS_ERR(mem)) {
		dev_err(dev, "Unable to get mmio space\n");
		return -EINVAL;
	}

	irq_count = of_irq_count(dev->of_node);
	if (!irq_count) {
		dev_err(dev, "Unable to find IRQ range\n");
		return -EINVAL;
	}

	ppc4xx_hsta_msi.dev = dev;
	ppc4xx_hsta_msi.address = mem->start;
	ppc4xx_hsta_msi.data = ioremap(mem->start, resource_size(mem));
	ppc4xx_hsta_msi.irq_count = irq_count;
	if (IS_ERR(ppc4xx_hsta_msi.data)) {
		dev_err(dev, "Unable to map memory\n");
		return -ENOMEM;
	}

	ret = msi_bitmap_alloc(&ppc4xx_hsta_msi.bmp, irq_count, dev->of_node);
	if (ret)
		goto out;

	ppc4xx_hsta_msi.irq_map = kmalloc(sizeof(int) * irq_count, GFP_KERNEL);
	if (IS_ERR(ppc4xx_hsta_msi.irq_map)) {
		ret = -ENOMEM;
		goto out1;
	}

	/* Setup a mapping from irq offsets to hardware irq numbers */
	for (irq = 0; irq < irq_count; irq++) {
		ppc4xx_hsta_msi.irq_map[irq] =
			irq_of_parse_and_map(dev->of_node, irq);
		if (ppc4xx_hsta_msi.irq_map[irq] == NO_IRQ) {
			dev_err(dev, "Unable to map IRQ\n");
			ret = -EINVAL;
			goto out2;
		}
	}

	ppc_md.setup_msi_irqs = hsta_setup_msi_irqs;
	ppc_md.teardown_msi_irqs = hsta_teardown_msi_irqs;
	return 0;

out2:
	kfree(ppc4xx_hsta_msi.irq_map);

out1:
	msi_bitmap_free(&ppc4xx_hsta_msi.bmp);

out:
	iounmap(ppc4xx_hsta_msi.data);
	return ret;
}

static const struct of_device_id hsta_msi_ids[] = {
	{
		.compatible = "ibm,hsta-msi",
	},
	{}
};

static struct platform_driver hsta_msi_driver = {
	.probe = hsta_msi_probe,
	.driver = {
		.name = "hsta-msi",
		.of_match_table = hsta_msi_ids,
	},
};

static int hsta_msi_init(void)
{
	return platform_driver_register(&hsta_msi_driver);
}
subsys_initcall(hsta_msi_init);
