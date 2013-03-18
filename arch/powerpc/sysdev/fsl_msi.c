/*
 * Copyright (C) 2007-2011 Freescale Semiconductor, Inc.
 *
 * Author: Tony Li <tony.li@freescale.com>
 *	   Jason Jin <Jason.jin@freescale.com>
 *
 * The hwirq alloc and free code reuse from sysdev/mpic_msi.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 *
 */
#include <linux/irq.h>
#include <linux/bootmem.h>
#include <linux/msi.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <sysdev/fsl_soc.h>
#include <asm/prom.h>
#include <asm/hw_irq.h>
#include <asm/ppc-pci.h>
#include <asm/mpic.h>
#include <asm/fsl_hcalls.h>

#include "fsl_msi.h"
#include "fsl_pci.h"

static LIST_HEAD(msi_head);

struct fsl_msi_feature {
	u32 fsl_pic_ip;
	u32 msiir_offset; /* Offset of MSIIR, relative to start of MSIR bank */
};

struct fsl_msi_cascade_data {
	struct fsl_msi *msi_data;
	int index;
};

static inline u32 fsl_msi_read(u32 __iomem *base, unsigned int reg)
{
	return in_be32(base + (reg >> 2));
}

/*
 * We do not need this actually. The MSIR register has been read once
 * in the cascade interrupt. So, this MSI interrupt has been acked
*/
static void fsl_msi_end_irq(struct irq_data *d)
{
}

static struct irq_chip fsl_msi_chip = {
	.irq_mask	= mask_msi_irq,
	.irq_unmask	= unmask_msi_irq,
	.irq_ack	= fsl_msi_end_irq,
	.name		= "FSL-MSI",
};

static int fsl_msi_host_map(struct irq_domain *h, unsigned int virq,
				irq_hw_number_t hw)
{
	struct fsl_msi *msi_data = h->host_data;
	struct irq_chip *chip = &fsl_msi_chip;

	irq_set_status_flags(virq, IRQ_TYPE_EDGE_FALLING);

	irq_set_chip_data(virq, msi_data);
	irq_set_chip_and_handler(virq, chip, handle_edge_irq);

	return 0;
}

static const struct irq_domain_ops fsl_msi_host_ops = {
	.map = fsl_msi_host_map,
};

static int fsl_msi_init_allocator(struct fsl_msi *msi_data)
{
	int rc;

	rc = msi_bitmap_alloc(&msi_data->bitmap, NR_MSI_IRQS,
			      msi_data->irqhost->of_node);
	if (rc)
		return rc;

	rc = msi_bitmap_reserve_dt_hwirqs(&msi_data->bitmap);
	if (rc < 0) {
		msi_bitmap_free(&msi_data->bitmap);
		return rc;
	}

	return 0;
}

static int fsl_msi_check_device(struct pci_dev *pdev, int nvec, int type)
{
	if (type == PCI_CAP_ID_MSIX)
		pr_debug("fslmsi: MSI-X untested, trying anyway.\n");

	return 0;
}

static void fsl_teardown_msi_irqs(struct pci_dev *pdev)
{
	struct msi_desc *entry;
	struct fsl_msi *msi_data;

	list_for_each_entry(entry, &pdev->msi_list, list) {
		if (entry->irq == NO_IRQ)
			continue;
		msi_data = irq_get_chip_data(entry->irq);
		irq_set_msi_desc(entry->irq, NULL);
		msi_bitmap_free_hwirqs(&msi_data->bitmap,
				       virq_to_hw(entry->irq), 1);
		irq_dispose_mapping(entry->irq);
	}

	return;
}

static void fsl_compose_msi_msg(struct pci_dev *pdev, int hwirq,
				struct msi_msg *msg,
				struct fsl_msi *fsl_msi_data)
{
	struct fsl_msi *msi_data = fsl_msi_data;
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	u64 address; /* Physical address of the MSIIR */
	int len;
	const __be64 *reg;

	/* If the msi-address-64 property exists, then use it */
	reg = of_get_property(hose->dn, "msi-address-64", &len);
	if (reg && (len == sizeof(u64)))
		address = be64_to_cpup(reg);
	else
		address = fsl_pci_immrbar_base(hose) + msi_data->msiir_offset;

	msg->address_lo = lower_32_bits(address);
	msg->address_hi = upper_32_bits(address);

	msg->data = hwirq;

	pr_debug("%s: allocated srs: %d, ibs: %d\n",
		__func__, hwirq / IRQS_PER_MSI_REG, hwirq % IRQS_PER_MSI_REG);
}

static int fsl_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct device_node *np;
	phandle phandle = 0;
	int rc, hwirq = -ENOMEM;
	unsigned int virq;
	struct msi_desc *entry;
	struct msi_msg msg;
	struct fsl_msi *msi_data;

	/*
	 * If the PCI node has an fsl,msi property, then we need to use it
	 * to find the specific MSI.
	 */
	np = of_parse_phandle(hose->dn, "fsl,msi", 0);
	if (np) {
		if (of_device_is_compatible(np, "fsl,mpic-msi") ||
		    of_device_is_compatible(np, "fsl,vmpic-msi"))
			phandle = np->phandle;
		else {
			dev_err(&pdev->dev,
				"node %s has an invalid fsl,msi phandle %u\n",
				hose->dn->full_name, np->phandle);
			return -EINVAL;
		}
	}

	list_for_each_entry(entry, &pdev->msi_list, list) {
		/*
		 * Loop over all the MSI devices until we find one that has an
		 * available interrupt.
		 */
		list_for_each_entry(msi_data, &msi_head, list) {
			/*
			 * If the PCI node has an fsl,msi property, then we
			 * restrict our search to the corresponding MSI node.
			 * The simplest way is to skip over MSI nodes with the
			 * wrong phandle. Under the Freescale hypervisor, this
			 * has the additional benefit of skipping over MSI
			 * nodes that are not mapped in the PAMU.
			 */
			if (phandle && (phandle != msi_data->phandle))
				continue;

			hwirq = msi_bitmap_alloc_hwirqs(&msi_data->bitmap, 1);
			if (hwirq >= 0)
				break;
		}

		if (hwirq < 0) {
			rc = hwirq;
			dev_err(&pdev->dev, "could not allocate MSI interrupt\n");
			goto out_free;
		}

		virq = irq_create_mapping(msi_data->irqhost, hwirq);

		if (virq == NO_IRQ) {
			dev_err(&pdev->dev, "fail mapping hwirq %i\n", hwirq);
			msi_bitmap_free_hwirqs(&msi_data->bitmap, hwirq, 1);
			rc = -ENOSPC;
			goto out_free;
		}
		/* chip_data is msi_data via host->hostdata in host->map() */
		irq_set_msi_desc(virq, entry);

		fsl_compose_msi_msg(pdev, hwirq, &msg, msi_data);
		write_msi_msg(virq, &msg);
	}
	return 0;

out_free:
	/* free by the caller of this function */
	return rc;
}

static void fsl_msi_cascade(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irq_data *idata = irq_desc_get_irq_data(desc);
	unsigned int cascade_irq;
	struct fsl_msi *msi_data;
	int msir_index = -1;
	u32 msir_value = 0;
	u32 intr_index;
	u32 have_shift = 0;
	struct fsl_msi_cascade_data *cascade_data;

	cascade_data = irq_get_handler_data(irq);
	msi_data = cascade_data->msi_data;

	raw_spin_lock(&desc->lock);
	if ((msi_data->feature &  FSL_PIC_IP_MASK) == FSL_PIC_IP_IPIC) {
		if (chip->irq_mask_ack)
			chip->irq_mask_ack(idata);
		else {
			chip->irq_mask(idata);
			chip->irq_ack(idata);
		}
	}

	if (unlikely(irqd_irq_inprogress(idata)))
		goto unlock;

	msir_index = cascade_data->index;

	if (msir_index >= NR_MSI_REG)
		cascade_irq = NO_IRQ;

	irqd_set_chained_irq_inprogress(idata);
	switch (msi_data->feature & FSL_PIC_IP_MASK) {
	case FSL_PIC_IP_MPIC:
		msir_value = fsl_msi_read(msi_data->msi_regs,
			msir_index * 0x10);
		break;
	case FSL_PIC_IP_IPIC:
		msir_value = fsl_msi_read(msi_data->msi_regs, msir_index * 0x4);
		break;
#ifdef CONFIG_EPAPR_PARAVIRT
	case FSL_PIC_IP_VMPIC: {
		unsigned int ret;
		ret = fh_vmpic_get_msir(virq_to_hw(irq), &msir_value);
		if (ret) {
			pr_err("fsl-msi: fh_vmpic_get_msir() failed for "
			       "irq %u (ret=%u)\n", irq, ret);
			msir_value = 0;
		}
		break;
	}
#endif
	}

	while (msir_value) {
		intr_index = ffs(msir_value) - 1;

		cascade_irq = irq_linear_revmap(msi_data->irqhost,
				msir_index * IRQS_PER_MSI_REG +
					intr_index + have_shift);
		if (cascade_irq != NO_IRQ)
			generic_handle_irq(cascade_irq);
		have_shift += intr_index + 1;
		msir_value = msir_value >> (intr_index + 1);
	}
	irqd_clr_chained_irq_inprogress(idata);

	switch (msi_data->feature & FSL_PIC_IP_MASK) {
	case FSL_PIC_IP_MPIC:
	case FSL_PIC_IP_VMPIC:
		chip->irq_eoi(idata);
		break;
	case FSL_PIC_IP_IPIC:
		if (!irqd_irq_disabled(idata) && chip->irq_unmask)
			chip->irq_unmask(idata);
		break;
	}
unlock:
	raw_spin_unlock(&desc->lock);
}

static int fsl_of_msi_remove(struct platform_device *ofdev)
{
	struct fsl_msi *msi = platform_get_drvdata(ofdev);
	int virq, i;
	struct fsl_msi_cascade_data *cascade_data;

	if (msi->list.prev != NULL)
		list_del(&msi->list);
	for (i = 0; i < NR_MSI_REG; i++) {
		virq = msi->msi_virqs[i];
		if (virq != NO_IRQ) {
			cascade_data = irq_get_handler_data(virq);
			kfree(cascade_data);
			irq_dispose_mapping(virq);
		}
	}
	if (msi->bitmap.bitmap)
		msi_bitmap_free(&msi->bitmap);
	if ((msi->feature & FSL_PIC_IP_MASK) != FSL_PIC_IP_VMPIC)
		iounmap(msi->msi_regs);
	kfree(msi);

	return 0;
}

static int fsl_msi_setup_hwirq(struct fsl_msi *msi, struct platform_device *dev,
			       int offset, int irq_index)
{
	struct fsl_msi_cascade_data *cascade_data = NULL;
	int virt_msir;

	virt_msir = irq_of_parse_and_map(dev->dev.of_node, irq_index);
	if (virt_msir == NO_IRQ) {
		dev_err(&dev->dev, "%s: Cannot translate IRQ index %d\n",
			__func__, irq_index);
		return 0;
	}

	cascade_data = kzalloc(sizeof(struct fsl_msi_cascade_data), GFP_KERNEL);
	if (!cascade_data) {
		dev_err(&dev->dev, "No memory for MSI cascade data\n");
		return -ENOMEM;
	}

	msi->msi_virqs[irq_index] = virt_msir;
	cascade_data->index = offset;
	cascade_data->msi_data = msi;
	irq_set_handler_data(virt_msir, cascade_data);
	irq_set_chained_handler(virt_msir, fsl_msi_cascade);

	return 0;
}

static const struct of_device_id fsl_of_msi_ids[];
static int fsl_of_msi_probe(struct platform_device *dev)
{
	const struct of_device_id *match;
	struct fsl_msi *msi;
	struct resource res;
	int err, i, j, irq_index, count;
	int rc;
	const u32 *p;
	const struct fsl_msi_feature *features;
	int len;
	u32 offset;
	static const u32 all_avail[] = { 0, NR_MSI_IRQS };

	match = of_match_device(fsl_of_msi_ids, &dev->dev);
	if (!match)
		return -EINVAL;
	features = match->data;

	printk(KERN_DEBUG "Setting up Freescale MSI support\n");

	msi = kzalloc(sizeof(struct fsl_msi), GFP_KERNEL);
	if (!msi) {
		dev_err(&dev->dev, "No memory for MSI structure\n");
		return -ENOMEM;
	}
	platform_set_drvdata(dev, msi);

	msi->irqhost = irq_domain_add_linear(dev->dev.of_node,
				      NR_MSI_IRQS, &fsl_msi_host_ops, msi);

	if (msi->irqhost == NULL) {
		dev_err(&dev->dev, "No memory for MSI irqhost\n");
		err = -ENOMEM;
		goto error_out;
	}

	/*
	 * Under the Freescale hypervisor, the msi nodes don't have a 'reg'
	 * property.  Instead, we use hypercalls to access the MSI.
	 */
	if ((features->fsl_pic_ip & FSL_PIC_IP_MASK) != FSL_PIC_IP_VMPIC) {
		err = of_address_to_resource(dev->dev.of_node, 0, &res);
		if (err) {
			dev_err(&dev->dev, "invalid resource for node %s\n",
				dev->dev.of_node->full_name);
			goto error_out;
		}

		msi->msi_regs = ioremap(res.start, resource_size(&res));
		if (!msi->msi_regs) {
			err = -ENOMEM;
			dev_err(&dev->dev, "could not map node %s\n",
				dev->dev.of_node->full_name);
			goto error_out;
		}
		msi->msiir_offset =
			features->msiir_offset + (res.start & 0xfffff);
	}

	msi->feature = features->fsl_pic_ip;

	/*
	 * Remember the phandle, so that we can match with any PCI nodes
	 * that have an "fsl,msi" property.
	 */
	msi->phandle = dev->dev.of_node->phandle;

	rc = fsl_msi_init_allocator(msi);
	if (rc) {
		dev_err(&dev->dev, "Error allocating MSI bitmap\n");
		goto error_out;
	}

	p = of_get_property(dev->dev.of_node, "msi-available-ranges", &len);
	if (p && len % (2 * sizeof(u32)) != 0) {
		dev_err(&dev->dev, "%s: Malformed msi-available-ranges property\n",
			__func__);
		err = -EINVAL;
		goto error_out;
	}

	if (!p) {
		p = all_avail;
		len = sizeof(all_avail);
	}

	for (irq_index = 0, i = 0; i < len / (2 * sizeof(u32)); i++) {
		if (p[i * 2] % IRQS_PER_MSI_REG ||
		    p[i * 2 + 1] % IRQS_PER_MSI_REG) {
			printk(KERN_WARNING "%s: %s: msi available range of %u at %u is not IRQ-aligned\n",
			       __func__, dev->dev.of_node->full_name,
			       p[i * 2 + 1], p[i * 2]);
			err = -EINVAL;
			goto error_out;
		}

		offset = p[i * 2] / IRQS_PER_MSI_REG;
		count = p[i * 2 + 1] / IRQS_PER_MSI_REG;

		for (j = 0; j < count; j++, irq_index++) {
			err = fsl_msi_setup_hwirq(msi, dev, offset + j, irq_index);
			if (err)
				goto error_out;
		}
	}

	list_add_tail(&msi->list, &msi_head);

	/* The multiple setting ppc_md.setup_msi_irqs will not harm things */
	if (!ppc_md.setup_msi_irqs) {
		ppc_md.setup_msi_irqs = fsl_setup_msi_irqs;
		ppc_md.teardown_msi_irqs = fsl_teardown_msi_irqs;
		ppc_md.msi_check_device = fsl_msi_check_device;
	} else if (ppc_md.setup_msi_irqs != fsl_setup_msi_irqs) {
		dev_err(&dev->dev, "Different MSI driver already installed!\n");
		err = -ENODEV;
		goto error_out;
	}
	return 0;
error_out:
	fsl_of_msi_remove(dev);
	return err;
}

static const struct fsl_msi_feature mpic_msi_feature = {
	.fsl_pic_ip = FSL_PIC_IP_MPIC,
	.msiir_offset = 0x140,
};

static const struct fsl_msi_feature ipic_msi_feature = {
	.fsl_pic_ip = FSL_PIC_IP_IPIC,
	.msiir_offset = 0x38,
};

static const struct fsl_msi_feature vmpic_msi_feature = {
	.fsl_pic_ip = FSL_PIC_IP_VMPIC,
	.msiir_offset = 0,
};

static const struct of_device_id fsl_of_msi_ids[] = {
	{
		.compatible = "fsl,mpic-msi",
		.data = &mpic_msi_feature,
	},
	{
		.compatible = "fsl,ipic-msi",
		.data = &ipic_msi_feature,
	},
#ifdef CONFIG_EPAPR_PARAVIRT
	{
		.compatible = "fsl,vmpic-msi",
		.data = &vmpic_msi_feature,
	},
#endif
	{}
};

static struct platform_driver fsl_of_msi_driver = {
	.driver = {
		.name = "fsl-msi",
		.owner = THIS_MODULE,
		.of_match_table = fsl_of_msi_ids,
	},
	.probe = fsl_of_msi_probe,
	.remove = fsl_of_msi_remove,
};

static __init int fsl_of_msi_init(void)
{
	return platform_driver_register(&fsl_of_msi_driver);
}

subsys_initcall(fsl_of_msi_init);
