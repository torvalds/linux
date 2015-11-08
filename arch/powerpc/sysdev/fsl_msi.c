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
#include <linux/msi.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <sysdev/fsl_soc.h>
#include <asm/prom.h>
#include <asm/hw_irq.h>
#include <asm/ppc-pci.h>
#include <asm/mpic.h>
#include <asm/fsl_hcalls.h>

#include "fsl_msi.h"
#include "fsl_pci.h"

#define MSIIR_OFFSET_MASK	0xfffff
#define MSIIR_IBS_SHIFT		0
#define MSIIR_SRS_SHIFT		5
#define MSIIR1_IBS_SHIFT	4
#define MSIIR1_SRS_SHIFT	0
#define MSI_SRS_MASK		0xf
#define MSI_IBS_MASK		0x1f

#define msi_hwirq(msi, msir_index, intr_index) \
		((msir_index) << (msi)->srs_shift | \
		 ((intr_index) << (msi)->ibs_shift))

static LIST_HEAD(msi_head);

struct fsl_msi_feature {
	u32 fsl_pic_ip;
	u32 msiir_offset; /* Offset of MSIIR, relative to start of MSIR bank */
};

struct fsl_msi_cascade_data {
	struct fsl_msi *msi_data;
	int index;
	int virq;
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

static void fsl_msi_print_chip(struct irq_data *irqd, struct seq_file *p)
{
	struct fsl_msi *msi_data = irqd->domain->host_data;
	irq_hw_number_t hwirq = irqd_to_hwirq(irqd);
	int cascade_virq, srs;

	srs = (hwirq >> msi_data->srs_shift) & MSI_SRS_MASK;
	cascade_virq = msi_data->cascade_array[srs]->virq;

	seq_printf(p, " fsl-msi-%d", cascade_virq);
}


static struct irq_chip fsl_msi_chip = {
	.irq_mask	= pci_msi_mask_irq,
	.irq_unmask	= pci_msi_unmask_irq,
	.irq_ack	= fsl_msi_end_irq,
	.irq_print_chip = fsl_msi_print_chip,
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
	int rc, hwirq;

	rc = msi_bitmap_alloc(&msi_data->bitmap, NR_MSI_IRQS_MAX,
			      irq_domain_get_of_node(msi_data->irqhost));
	if (rc)
		return rc;

	/*
	 * Reserve all the hwirqs
	 * The available hwirqs will be released in fsl_msi_setup_hwirq()
	 */
	for (hwirq = 0; hwirq < NR_MSI_IRQS_MAX; hwirq++)
		msi_bitmap_reserve_hwirq(&msi_data->bitmap, hwirq);

	return 0;
}

static void fsl_teardown_msi_irqs(struct pci_dev *pdev)
{
	struct msi_desc *entry;
	struct fsl_msi *msi_data;
	irq_hw_number_t hwirq;

	for_each_pci_msi_entry(entry, pdev) {
		if (entry->irq == NO_IRQ)
			continue;
		hwirq = virq_to_hw(entry->irq);
		msi_data = irq_get_chip_data(entry->irq);
		irq_set_msi_desc(entry->irq, NULL);
		irq_dispose_mapping(entry->irq);
		msi_bitmap_free_hwirqs(&msi_data->bitmap, hwirq, 1);
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

	/*
	 * MPIC version 2.0 has erratum PIC1. It causes
	 * that neither MSI nor MSI-X can work fine.
	 * This is a workaround to allow MSI-X to function
	 * properly. It only works for MSI-X, we prevent
	 * MSI on buggy chips in fsl_setup_msi_irqs().
	 */
	if (msi_data->feature & MSI_HW_ERRATA_ENDIAN)
		msg->data = __swab32(hwirq);
	else
		msg->data = hwirq;

	pr_debug("%s: allocated srs: %d, ibs: %d\n", __func__,
		 (hwirq >> msi_data->srs_shift) & MSI_SRS_MASK,
		 (hwirq >> msi_data->ibs_shift) & MSI_IBS_MASK);
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

	if (type == PCI_CAP_ID_MSI) {
		/*
		 * MPIC version 2.0 has erratum PIC1. For now MSI
		 * could not work. So check to prevent MSI from
		 * being used on the board with this erratum.
		 */
		list_for_each_entry(msi_data, &msi_head, list)
			if (msi_data->feature & MSI_HW_ERRATA_ENDIAN)
				return -EINVAL;
	}

	/*
	 * If the PCI node has an fsl,msi property, then we need to use it
	 * to find the specific MSI.
	 */
	np = of_parse_phandle(hose->dn, "fsl,msi", 0);
	if (np) {
		if (of_device_is_compatible(np, "fsl,mpic-msi") ||
		    of_device_is_compatible(np, "fsl,vmpic-msi") ||
		    of_device_is_compatible(np, "fsl,vmpic-msi-v4.3"))
			phandle = np->phandle;
		else {
			dev_err(&pdev->dev,
				"node %s has an invalid fsl,msi phandle %u\n",
				hose->dn->full_name, np->phandle);
			return -EINVAL;
		}
	}

	for_each_pci_msi_entry(entry, pdev) {
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
		pci_write_msi_msg(virq, &msg);
	}
	return 0;

out_free:
	/* free by the caller of this function */
	return rc;
}

static irqreturn_t fsl_msi_cascade(int irq, void *data)
{
	unsigned int cascade_irq;
	struct fsl_msi *msi_data;
	int msir_index = -1;
	u32 msir_value = 0;
	u32 intr_index;
	u32 have_shift = 0;
	struct fsl_msi_cascade_data *cascade_data = data;
	irqreturn_t ret = IRQ_NONE;

	msi_data = cascade_data->msi_data;

	msir_index = cascade_data->index;

	if (msir_index >= NR_MSI_REG_MAX)
		cascade_irq = NO_IRQ;

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
				msi_hwirq(msi_data, msir_index,
					  intr_index + have_shift));
		if (cascade_irq != NO_IRQ) {
			generic_handle_irq(cascade_irq);
			ret = IRQ_HANDLED;
		}
		have_shift += intr_index + 1;
		msir_value = msir_value >> (intr_index + 1);
	}

	return ret;
}

static int fsl_of_msi_remove(struct platform_device *ofdev)
{
	struct fsl_msi *msi = platform_get_drvdata(ofdev);
	int virq, i;

	if (msi->list.prev != NULL)
		list_del(&msi->list);
	for (i = 0; i < NR_MSI_REG_MAX; i++) {
		if (msi->cascade_array[i]) {
			virq = msi->cascade_array[i]->virq;

			BUG_ON(virq == NO_IRQ);

			free_irq(virq, msi->cascade_array[i]);
			kfree(msi->cascade_array[i]);
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

static struct lock_class_key fsl_msi_irq_class;

static int fsl_msi_setup_hwirq(struct fsl_msi *msi, struct platform_device *dev,
			       int offset, int irq_index)
{
	struct fsl_msi_cascade_data *cascade_data = NULL;
	int virt_msir, i, ret;

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
	irq_set_lockdep_class(virt_msir, &fsl_msi_irq_class);
	cascade_data->index = offset;
	cascade_data->msi_data = msi;
	cascade_data->virq = virt_msir;
	msi->cascade_array[irq_index] = cascade_data;

	ret = request_irq(virt_msir, fsl_msi_cascade, IRQF_NO_THREAD,
			  "fsl-msi-cascade", cascade_data);
	if (ret) {
		dev_err(&dev->dev, "failed to request_irq(%d), ret = %d\n",
			virt_msir, ret);
		return ret;
	}

	/* Release the hwirqs corresponding to this MSI register */
	for (i = 0; i < IRQS_PER_MSI_REG; i++)
		msi_bitmap_free_hwirqs(&msi->bitmap,
				       msi_hwirq(msi, offset, i), 1);

	return 0;
}

static const struct of_device_id fsl_of_msi_ids[];
static int fsl_of_msi_probe(struct platform_device *dev)
{
	const struct of_device_id *match;
	struct fsl_msi *msi;
	struct resource res, msiir;
	int err, i, j, irq_index, count;
	const u32 *p;
	const struct fsl_msi_feature *features;
	int len;
	u32 offset;
	struct pci_controller *phb;

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
				      NR_MSI_IRQS_MAX, &fsl_msi_host_ops, msi);

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

		/*
		 * First read the MSIIR/MSIIR1 offset from dts
		 * On failure use the hardcode MSIIR offset
		 */
		if (of_address_to_resource(dev->dev.of_node, 1, &msiir))
			msi->msiir_offset = features->msiir_offset +
					    (res.start & MSIIR_OFFSET_MASK);
		else
			msi->msiir_offset = msiir.start & MSIIR_OFFSET_MASK;
	}

	msi->feature = features->fsl_pic_ip;

	/* For erratum PIC1 on MPIC version 2.0*/
	if ((features->fsl_pic_ip & FSL_PIC_IP_MASK) == FSL_PIC_IP_MPIC
			&& (fsl_mpic_primary_get_version() == 0x0200))
		msi->feature |= MSI_HW_ERRATA_ENDIAN;

	/*
	 * Remember the phandle, so that we can match with any PCI nodes
	 * that have an "fsl,msi" property.
	 */
	msi->phandle = dev->dev.of_node->phandle;

	err = fsl_msi_init_allocator(msi);
	if (err) {
		dev_err(&dev->dev, "Error allocating MSI bitmap\n");
		goto error_out;
	}

	p = of_get_property(dev->dev.of_node, "msi-available-ranges", &len);

	if (of_device_is_compatible(dev->dev.of_node, "fsl,mpic-msi-v4.3") ||
	    of_device_is_compatible(dev->dev.of_node, "fsl,vmpic-msi-v4.3")) {
		msi->srs_shift = MSIIR1_SRS_SHIFT;
		msi->ibs_shift = MSIIR1_IBS_SHIFT;
		if (p)
			dev_warn(&dev->dev, "%s: dose not support msi-available-ranges property\n",
				__func__);

		for (irq_index = 0; irq_index < NR_MSI_REG_MSIIR1;
		     irq_index++) {
			err = fsl_msi_setup_hwirq(msi, dev,
						  irq_index, irq_index);
			if (err)
				goto error_out;
		}
	} else {
		static const u32 all_avail[] =
			{ 0, NR_MSI_REG_MSIIR * IRQS_PER_MSI_REG };

		msi->srs_shift = MSIIR_SRS_SHIFT;
		msi->ibs_shift = MSIIR_IBS_SHIFT;

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
				pr_warn("%s: %s: msi available range of %u at %u is not IRQ-aligned\n",
				       __func__, dev->dev.of_node->full_name,
				       p[i * 2 + 1], p[i * 2]);
				err = -EINVAL;
				goto error_out;
			}

			offset = p[i * 2] / IRQS_PER_MSI_REG;
			count = p[i * 2 + 1] / IRQS_PER_MSI_REG;

			for (j = 0; j < count; j++, irq_index++) {
				err = fsl_msi_setup_hwirq(msi, dev, offset + j,
							  irq_index);
				if (err)
					goto error_out;
			}
		}
	}

	list_add_tail(&msi->list, &msi_head);

	/*
	 * Apply the MSI ops to all the controllers.
	 * It doesn't hurt to reassign the same ops,
	 * but bail out if we find another MSI driver.
	 */
	list_for_each_entry(phb, &hose_list, list_node) {
		if (!phb->controller_ops.setup_msi_irqs) {
			phb->controller_ops.setup_msi_irqs = fsl_setup_msi_irqs;
			phb->controller_ops.teardown_msi_irqs = fsl_teardown_msi_irqs;
		} else if (phb->controller_ops.setup_msi_irqs != fsl_setup_msi_irqs) {
			dev_err(&dev->dev, "Different MSI driver already installed!\n");
			err = -ENODEV;
			goto error_out;
		}
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
		.compatible = "fsl,mpic-msi-v4.3",
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
	{
		.compatible = "fsl,vmpic-msi-v4.3",
		.data = &vmpic_msi_feature,
	},
#endif
	{}
};

static struct platform_driver fsl_of_msi_driver = {
	.driver = {
		.name = "fsl-msi",
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
