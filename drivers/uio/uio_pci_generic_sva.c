// SPDX-License-Identifier: GPL-2.0
/*
 * UIO PCI Express sva driver
 *
 * Copyright (c) 2025 Beijing Institute of Open Source Chip (BOSC)
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/uio_driver.h>
#include <linux/iommu.h>

struct uio_pci_sva_dev {
	struct pci_dev *pdev;
	struct uio_info info;
	struct iommu_sva *sva_handle;
	int pasid;
};

static irqreturn_t irq_handler(int irq, struct uio_info *dev_info)
{
	return IRQ_HANDLED;
}

static int uio_pci_sva_open(struct uio_info *info, struct inode *inode)
{
	struct iommu_sva *handle;
	struct uio_pci_sva_dev *udev = info->priv;
	struct iommu_domain *domain;

	if (!udev || !udev->pdev)
		return -ENODEV;

	domain = iommu_get_domain_for_dev(&udev->pdev->dev);
	if (domain)
		iommu_detach_device(domain, &udev->pdev->dev);

	handle = iommu_sva_bind_device(&udev->pdev->dev, current->mm);
	if (IS_ERR(handle))
		return -EINVAL;

	udev->pasid = iommu_sva_get_pasid(handle);

	udev->sva_handle = handle;

	return 0;
}

static int uio_pci_sva_release(struct uio_info *info, struct inode *inode)
{
	struct uio_pci_sva_dev *udev = info->priv;

	if (!udev || !udev->pdev)
		return -ENODEV;

	iommu_sva_unbind_device(udev->sva_handle);

	return 0;
}

static int probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct uio_pci_sva_dev *udev;
	int ret, i, irq = 0;

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_device failed: %d\n", ret);
		return ret;
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret)
		goto out_disable;

	pci_set_master(pdev);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSIX | PCI_IRQ_MSI);
	if (ret > 0) {
		irq = pci_irq_vector(pdev, 0);
		if (irq < 0) {
			dev_err(&pdev->dev, "Failed to get MSI vector\n");
			ret = irq;
			goto out_disable;
		}
	} else
		dev_warn(&pdev->dev,
			 "No IRQ vectors available (%d), using polling\n", ret);

	udev = devm_kzalloc(&pdev->dev, sizeof(struct uio_pci_sva_dev),
			    GFP_KERNEL);
	if (!udev) {
		ret =  -ENOMEM;
		goto out_disable;
	}

	udev->pdev = pdev;
	udev->info.name = "uio_pci_sva";
	udev->info.version = "0.0.1";
	udev->info.open = uio_pci_sva_open;
	udev->info.release = uio_pci_sva_release;
	udev->info.irq = irq;
	udev->info.handler = irq_handler;
	udev->info.priv = udev;

	for (i = 0; i < MAX_UIO_MAPS; i++) {
		struct resource *r = &pdev->resource[i];
		struct uio_mem *uiomem = &udev->info.mem[i];

		if (r->flags != (IORESOURCE_SIZEALIGN | IORESOURCE_MEM))
			continue;

		if (uiomem >= &udev->info.mem[MAX_UIO_MAPS]) {
			dev_warn(&pdev->dev, "Do not support more than %d iomem\n",
				 MAX_UIO_MAPS);
			break;
		}

		uiomem->memtype = UIO_MEM_PHYS;
		uiomem->addr = r->start & PAGE_MASK;
		uiomem->offs = r->start & ~PAGE_MASK;
		uiomem->size =
			(uiomem->offs + resource_size(r) + PAGE_SIZE - 1) &
			PAGE_MASK;
		uiomem->name = r->name;
	}

	ret = devm_uio_register_device(&pdev->dev, &udev->info);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register uio device\n");
		goto out_free;
	}

	pci_set_drvdata(pdev, udev);

	return 0;

out_free:
	kfree(udev);
out_disable:
	pci_disable_device(pdev);

	return ret;
}

static void remove(struct pci_dev *pdev)
{
	struct uio_pci_sva_dev *udev = pci_get_drvdata(pdev);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
	kfree(udev);
}

static ssize_t pasid_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct uio_pci_sva_dev *udev = pci_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", udev->pasid);
}
static DEVICE_ATTR_RO(pasid);

static struct attribute *uio_pci_sva_attrs[] = {
	&dev_attr_pasid.attr,
	NULL
};

static const struct attribute_group uio_pci_sva_attr_group = {
	.attrs = uio_pci_sva_attrs,
};

static const struct attribute_group *uio_pci_sva_attr_groups[] = {
	&uio_pci_sva_attr_group,
	NULL
};

static struct pci_driver uio_pci_generic_sva_driver = {
	.name = "uio_pci_sva",
	.dev_groups = uio_pci_sva_attr_groups,
	.id_table = NULL,
	.probe = probe,
	.remove = remove,
};

module_pci_driver(uio_pci_generic_sva_driver);
MODULE_VERSION("0.0.01");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Yaxing Guo <guoyaxing@bosc.ac.cn>");
MODULE_DESCRIPTION("Generic UIO sva driver for PCI");
