/*
 * Adding PCI-E MSI support for PPC4XX SoCs.
 *
 * Copyright (c) 2010, Applied Micro Circuits Corporation
 * Authors:	Tirumala R Marri <tmarri@apm.com>
 *		Feng Kan <fkan@apm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/msi.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <asm/prom.h>
#include <asm/hw_irq.h>
#include <asm/ppc-pci.h>
#include <asm/dcr.h>
#include <asm/dcr-regs.h>
#include <asm/msi_bitmap.h>

#define PEIH_TERMADH	0x00
#define PEIH_TERMADL	0x08
#define PEIH_MSIED	0x10
#define PEIH_MSIMK	0x18
#define PEIH_MSIASS	0x20
#define PEIH_FLUSH0	0x30
#define PEIH_FLUSH1	0x38
#define PEIH_CNTRST	0x48

static int msi_irqs;

struct ppc4xx_msi {
	u32 msi_addr_lo;
	u32 msi_addr_hi;
	void __iomem *msi_regs;
	int *msi_virqs;
	struct msi_bitmap bitmap;
	struct device_node *msi_dev;
};

static struct ppc4xx_msi ppc4xx_msi;

static int ppc4xx_msi_init_allocator(struct platform_device *dev,
		struct ppc4xx_msi *msi_data)
{
	int err;

	err = msi_bitmap_alloc(&msi_data->bitmap, msi_irqs,
			      dev->dev.of_node);
	if (err)
		return err;

	err = msi_bitmap_reserve_dt_hwirqs(&msi_data->bitmap);
	if (err < 0) {
		msi_bitmap_free(&msi_data->bitmap);
		return err;
	}

	return 0;
}

static int ppc4xx_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	int int_no = -ENOMEM;
	unsigned int virq;
	struct msi_msg msg;
	struct msi_desc *entry;
	struct ppc4xx_msi *msi_data = &ppc4xx_msi;

	dev_dbg(&dev->dev, "PCIE-MSI:%s called. vec %x type %d\n",
		__func__, nvec, type);
	if (type == PCI_CAP_ID_MSIX)
		pr_debug("ppc4xx msi: MSI-X untested, trying anyway.\n");

	msi_data->msi_virqs = kmalloc((msi_irqs) * sizeof(int), GFP_KERNEL);
	if (!msi_data->msi_virqs)
		return -ENOMEM;

	for_each_pci_msi_entry(entry, dev) {
		int_no = msi_bitmap_alloc_hwirqs(&msi_data->bitmap, 1);
		if (int_no >= 0)
			break;
		if (int_no < 0) {
			pr_debug("%s: fail allocating msi interrupt\n",
					__func__);
		}
		virq = irq_of_parse_and_map(msi_data->msi_dev, int_no);
		if (!virq) {
			dev_err(&dev->dev, "%s: fail mapping irq\n", __func__);
			msi_bitmap_free_hwirqs(&msi_data->bitmap, int_no, 1);
			return -ENOSPC;
		}
		dev_dbg(&dev->dev, "%s: virq = %d\n", __func__, virq);

		/* Setup msi address space */
		msg.address_hi = msi_data->msi_addr_hi;
		msg.address_lo = msi_data->msi_addr_lo;

		irq_set_msi_desc(virq, entry);
		msg.data = int_no;
		pci_write_msi_msg(virq, &msg);
	}
	return 0;
}

void ppc4xx_teardown_msi_irqs(struct pci_dev *dev)
{
	struct msi_desc *entry;
	struct ppc4xx_msi *msi_data = &ppc4xx_msi;
	irq_hw_number_t hwirq;

	dev_dbg(&dev->dev, "PCIE-MSI: tearing down msi irqs\n");

	for_each_pci_msi_entry(entry, dev) {
		if (!entry->irq)
			continue;
		hwirq = virq_to_hw(entry->irq);
		irq_set_msi_desc(entry->irq, NULL);
		irq_dispose_mapping(entry->irq);
		msi_bitmap_free_hwirqs(&msi_data->bitmap, hwirq, 1);
	}
}

static int ppc4xx_setup_pcieh_hw(struct platform_device *dev,
				 struct resource res, struct ppc4xx_msi *msi)
{
	const u32 *msi_data;
	const u32 *msi_mask;
	const u32 *sdr_addr;
	dma_addr_t msi_phys;
	void *msi_virt;

	sdr_addr = of_get_property(dev->dev.of_node, "sdr-base", NULL);
	if (!sdr_addr)
		return -1;

	mtdcri(SDR0, *sdr_addr, upper_32_bits(res.start));	/*HIGH addr */
	mtdcri(SDR0, *sdr_addr + 1, lower_32_bits(res.start));	/* Low addr */

	msi->msi_dev = of_find_node_by_name(NULL, "ppc4xx-msi");
	if (!msi->msi_dev)
		return -ENODEV;

	msi->msi_regs = of_iomap(msi->msi_dev, 0);
	if (!msi->msi_regs) {
		dev_err(&dev->dev, "of_iomap problem failed\n");
		return -ENOMEM;
	}
	dev_dbg(&dev->dev, "PCIE-MSI: msi register mapped 0x%x 0x%x\n",
		(u32) (msi->msi_regs + PEIH_TERMADH), (u32) (msi->msi_regs));

	msi_virt = dma_alloc_coherent(&dev->dev, 64, &msi_phys, GFP_KERNEL);
	if (!msi_virt)
		return -ENOMEM;
	msi->msi_addr_hi = upper_32_bits(msi_phys);
	msi->msi_addr_lo = lower_32_bits(msi_phys & 0xffffffff);
	dev_dbg(&dev->dev, "PCIE-MSI: msi address high 0x%x, low 0x%x\n",
		msi->msi_addr_hi, msi->msi_addr_lo);

	/* Progam the Interrupt handler Termination addr registers */
	out_be32(msi->msi_regs + PEIH_TERMADH, msi->msi_addr_hi);
	out_be32(msi->msi_regs + PEIH_TERMADL, msi->msi_addr_lo);

	msi_data = of_get_property(dev->dev.of_node, "msi-data", NULL);
	if (!msi_data)
		return -1;
	msi_mask = of_get_property(dev->dev.of_node, "msi-mask", NULL);
	if (!msi_mask)
		return -1;
	/* Program MSI Expected data and Mask bits */
	out_be32(msi->msi_regs + PEIH_MSIED, *msi_data);
	out_be32(msi->msi_regs + PEIH_MSIMK, *msi_mask);

	dma_free_coherent(&dev->dev, 64, msi_virt, msi_phys);

	return 0;
}

static int ppc4xx_of_msi_remove(struct platform_device *dev)
{
	struct ppc4xx_msi *msi = dev->dev.platform_data;
	int i;
	int virq;

	for (i = 0; i < msi_irqs; i++) {
		virq = msi->msi_virqs[i];
		if (virq)
			irq_dispose_mapping(virq);
	}

	if (msi->bitmap.bitmap)
		msi_bitmap_free(&msi->bitmap);
	iounmap(msi->msi_regs);
	of_node_put(msi->msi_dev);
	kfree(msi);

	return 0;
}

static int ppc4xx_msi_probe(struct platform_device *dev)
{
	struct ppc4xx_msi *msi;
	struct resource res;
	int err = 0;
	struct pci_controller *phb;

	dev_dbg(&dev->dev, "PCIE-MSI: Setting up MSI support...\n");

	msi = kzalloc(sizeof(struct ppc4xx_msi), GFP_KERNEL);
	if (!msi) {
		dev_err(&dev->dev, "No memory for MSI structure\n");
		return -ENOMEM;
	}
	dev->dev.platform_data = msi;

	/* Get MSI ranges */
	err = of_address_to_resource(dev->dev.of_node, 0, &res);
	if (err) {
		dev_err(&dev->dev, "%s resource error!\n",
			dev->dev.of_node->full_name);
		goto error_out;
	}

	msi_irqs = of_irq_count(dev->dev.of_node);
	if (!msi_irqs)
		return -ENODEV;

	if (ppc4xx_setup_pcieh_hw(dev, res, msi))
		goto error_out;

	err = ppc4xx_msi_init_allocator(dev, msi);
	if (err) {
		dev_err(&dev->dev, "Error allocating MSI bitmap\n");
		goto error_out;
	}
	ppc4xx_msi = *msi;

	list_for_each_entry(phb, &hose_list, list_node) {
		phb->controller_ops.setup_msi_irqs = ppc4xx_setup_msi_irqs;
		phb->controller_ops.teardown_msi_irqs = ppc4xx_teardown_msi_irqs;
	}
	return err;

error_out:
	ppc4xx_of_msi_remove(dev);
	return err;
}
static const struct of_device_id ppc4xx_msi_ids[] = {
	{
		.compatible = "amcc,ppc4xx-msi",
	},
	{}
};
static struct platform_driver ppc4xx_msi_driver = {
	.probe = ppc4xx_msi_probe,
	.remove = ppc4xx_of_msi_remove,
	.driver = {
		   .name = "ppc4xx-msi",
		   .of_match_table = ppc4xx_msi_ids,
		   },

};

static __init int ppc4xx_msi_init(void)
{
	return platform_driver_register(&ppc4xx_msi_driver);
}

subsys_initcall(ppc4xx_msi_init);
