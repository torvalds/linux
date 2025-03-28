// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright © 2022-2024 Rivos Inc.
 * Copyright © 2023 FORTH-ICS/CARV
 *
 * RISCV IOMMU as a PCIe device
 *
 * Authors
 *	Tomasz Jeznach <tjeznach@rivosinc.com>
 *	Nick Kossifidis <mick@ics.forth.gr>
 */

#include <linux/compiler.h>
#include <linux/init.h>
#include <linux/iommu.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include "iommu-bits.h"
#include "iommu.h"

/* QEMU RISC-V IOMMU implementation */
#define PCI_DEVICE_ID_REDHAT_RISCV_IOMMU     0x0014

/* Rivos Inc. assigned PCI Vendor and Device IDs */
#ifndef PCI_VENDOR_ID_RIVOS
#define PCI_VENDOR_ID_RIVOS                  0x1efd
#endif

#define PCI_DEVICE_ID_RIVOS_RISCV_IOMMU_GA   0x0008

static int riscv_iommu_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct riscv_iommu_device *iommu;
	int rc, vec;

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM))
		return -ENODEV;

	if (pci_resource_len(pdev, 0) < RISCV_IOMMU_REG_SIZE)
		return -ENODEV;

	rc = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
	if (rc)
		return dev_err_probe(dev, rc, "pcim_iomap_regions failed\n");

	iommu = devm_kzalloc(dev, sizeof(*iommu), GFP_KERNEL);
	if (!iommu)
		return -ENOMEM;

	iommu->dev = dev;
	iommu->reg = pcim_iomap_table(pdev)[0];

	pci_set_master(pdev);
	dev_set_drvdata(dev, iommu);

	/* Check device reported capabilities / features. */
	iommu->caps = riscv_iommu_readq(iommu, RISCV_IOMMU_REG_CAPABILITIES);
	iommu->fctl = riscv_iommu_readl(iommu, RISCV_IOMMU_REG_FCTL);

	/* The PCI driver only uses MSIs, make sure the IOMMU supports this */
	switch (FIELD_GET(RISCV_IOMMU_CAPABILITIES_IGS, iommu->caps)) {
	case RISCV_IOMMU_CAPABILITIES_IGS_MSI:
	case RISCV_IOMMU_CAPABILITIES_IGS_BOTH:
		break;
	default:
		return dev_err_probe(dev, -ENODEV,
				     "unable to use message-signaled interrupts\n");
	}

	/* Allocate and assign IRQ vectors for the various events */
	rc = pci_alloc_irq_vectors(pdev, 1, RISCV_IOMMU_INTR_COUNT,
				   PCI_IRQ_MSIX | PCI_IRQ_MSI);
	if (rc <= 0)
		return dev_err_probe(dev, -ENODEV,
				     "unable to allocate irq vectors\n");

	iommu->irqs_count = rc;
	for (vec = 0; vec < iommu->irqs_count; vec++)
		iommu->irqs[vec] = msi_get_virq(dev, vec);

	/* Enable message-signaled interrupts, fctl.WSI */
	if (iommu->fctl & RISCV_IOMMU_FCTL_WSI) {
		iommu->fctl ^= RISCV_IOMMU_FCTL_WSI;
		riscv_iommu_writel(iommu, RISCV_IOMMU_REG_FCTL, iommu->fctl);
	}

	return riscv_iommu_init(iommu);
}

static void riscv_iommu_pci_remove(struct pci_dev *pdev)
{
	struct riscv_iommu_device *iommu = dev_get_drvdata(&pdev->dev);

	riscv_iommu_remove(iommu);
}

static void riscv_iommu_pci_shutdown(struct pci_dev *pdev)
{
	struct riscv_iommu_device *iommu = dev_get_drvdata(&pdev->dev);

	riscv_iommu_disable(iommu);
}

static const struct pci_device_id riscv_iommu_pci_tbl[] = {
	{PCI_VDEVICE(REDHAT, PCI_DEVICE_ID_REDHAT_RISCV_IOMMU), 0},
	{PCI_VDEVICE(RIVOS, PCI_DEVICE_ID_RIVOS_RISCV_IOMMU_GA), 0},
	{0,}
};

static struct pci_driver riscv_iommu_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = riscv_iommu_pci_tbl,
	.probe = riscv_iommu_pci_probe,
	.remove = riscv_iommu_pci_remove,
	.shutdown = riscv_iommu_pci_shutdown,
	.driver = {
		.suppress_bind_attrs = true,
	},
};

builtin_pci_driver(riscv_iommu_pci_driver);
