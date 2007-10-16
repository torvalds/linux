/*
 * Intel I/OAT DMA Linux driver
 * Copyright(c) 2004 - 2007 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 */

/*
 * This driver supports an Intel I/OAT DMA engine, which does asynchronous
 * copy operations.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include "ioatdma.h"
#include "ioatdma_registers.h"
#include "ioatdma_hw.h"

MODULE_VERSION("1.24");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Intel Corporation");

static struct pci_device_id ioat_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_IOAT) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_IOAT_CNB)  },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_IOAT_SCNB) },
	{ PCI_DEVICE(PCI_VENDOR_ID_UNISYS, PCI_DEVICE_ID_UNISYS_DMA_DIRECTOR) },
	{ 0, }
};

struct ioat_device {
	struct pci_dev		*pdev;
	void __iomem		*iobase;
	struct ioatdma_device	*dma;
};

static int __devinit ioat_probe(struct pci_dev *pdev,
				const struct pci_device_id *id);
#ifdef IOAT_DMA_REMOVE
static void __devexit ioat_remove(struct pci_dev *pdev);
#endif

static int ioat_setup_functionality(struct pci_dev *pdev, void __iomem *iobase)
{
	struct ioat_device *device = pci_get_drvdata(pdev);
	u8 version;
	int err = 0;

	version = readb(iobase + IOAT_VER_OFFSET);
	switch (version) {
	case IOAT_VER_1_2:
		device->dma = ioat_dma_probe(pdev, iobase);
		break;
	default:
		err = -ENODEV;
		break;
	}
	return err;
}

static void ioat_shutdown_functionality(struct pci_dev *pdev)
{
	struct ioat_device *device = pci_get_drvdata(pdev);

	if (device->dma) {
		ioat_dma_remove(device->dma);
		device->dma = NULL;
	}
}

static struct pci_driver ioat_pci_drv = {
	.name		= "ioatdma",
	.id_table	= ioat_pci_tbl,
	.probe		= ioat_probe,
	.shutdown	= ioat_shutdown_functionality,
#ifdef IOAT_DMA_REMOVE
	.remove		= __devexit_p(ioat_remove),
#endif
};

static int __devinit ioat_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)
{
	void __iomem *iobase;
	struct ioat_device *device;
	unsigned long mmio_start, mmio_len;
	int err;

	err = pci_enable_device(pdev);
	if (err)
		goto err_enable_device;

	err = pci_request_regions(pdev, ioat_pci_drv.name);
	if (err)
		goto err_request_regions;

	err = pci_set_dma_mask(pdev, DMA_64BIT_MASK);
	if (err)
		err = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	if (err)
		goto err_set_dma_mask;

	err = pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK);
	if (err)
		err = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
	if (err)
		goto err_set_dma_mask;

	mmio_start = pci_resource_start(pdev, 0);
	mmio_len = pci_resource_len(pdev, 0);
	iobase = ioremap(mmio_start, mmio_len);
	if (!iobase) {
		err = -ENOMEM;
		goto err_ioremap;
	}

	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (!device) {
		err = -ENOMEM;
		goto err_kzalloc;
	}
	device->pdev = pdev;
	pci_set_drvdata(pdev, device);
	device->iobase = iobase;

	pci_set_master(pdev);

	err = ioat_setup_functionality(pdev, iobase);
	if (err)
		goto err_version;

	return 0;

err_version:
	kfree(device);
err_kzalloc:
	iounmap(iobase);
err_ioremap:
err_set_dma_mask:
	pci_release_regions(pdev);
	pci_disable_device(pdev);
err_request_regions:
err_enable_device:
	return err;
}

#ifdef IOAT_DMA_REMOVE
/*
 * It is unsafe to remove this module: if removed while a requested
 * dma is outstanding, esp. from tcp, it is possible to hang while
 * waiting for something that will never finish, thus hanging at
 * least one cpu.  However, if you're feeling lucky and need to do
 * some testing, this usually works just fine.
 */
static void __devexit ioat_remove(struct pci_dev *pdev)
{
	struct ioat_device *device = pci_get_drvdata(pdev);

	ioat_shutdown_functionality(pdev);

	kfree(device);

	iounmap(device->iobase);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}
#endif

static int __init ioat_init_module(void)
{
	return pci_register_driver(&ioat_pci_drv);
}
module_init(ioat_init_module);

static void __exit ioat_exit_module(void)
{
	pci_unregister_driver(&ioat_pci_drv);
}
module_exit(ioat_exit_module);
