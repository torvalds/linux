/*
 * Intel I/OAT DMA Linux driver
 * Copyright(c) 2007 - 2009 Intel Corporation.
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
#include <linux/dca.h>
#include <linux/slab.h>
#include "dma.h"
#include "dma_v2.h"
#include "registers.h"
#include "hw.h"

MODULE_VERSION(IOAT_DMA_VERSION);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel Corporation");

static struct pci_device_id ioat_pci_tbl[] = {
	/* I/OAT v1 platforms */
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_CNB)  },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SCNB) },
	{ PCI_VDEVICE(UNISYS, PCI_DEVICE_ID_UNISYS_DMA_DIRECTOR) },

	/* I/OAT v2 platforms */
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB) },

	/* I/OAT v3 platforms */
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_TBG0) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_TBG1) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_TBG2) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_TBG3) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_TBG4) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_TBG5) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_TBG6) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_TBG7) },

	/* I/OAT v3.2 platforms */
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF0) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF1) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF2) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF3) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF4) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF5) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF6) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF7) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF8) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF9) },

	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB0) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB1) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB2) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB3) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB4) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB5) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB6) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB7) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB8) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB9) },

	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB0) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB1) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB2) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB3) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB4) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB5) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB6) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB7) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB8) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB9) },

	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW0) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW1) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW2) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW3) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW4) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW5) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW6) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW7) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW8) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW9) },

	/* I/OAT v3.3 platforms */
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BWD0) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BWD1) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BWD2) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BWD3) },

	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ioat_pci_tbl);

static int ioat_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id);
static void ioat_remove(struct pci_dev *pdev);

static int ioat_dca_enabled = 1;
module_param(ioat_dca_enabled, int, 0644);
MODULE_PARM_DESC(ioat_dca_enabled, "control support of dca service (default: 1)");

struct kmem_cache *ioat2_cache;

#define DRV_NAME "ioatdma"

static struct pci_driver ioat_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= ioat_pci_tbl,
	.probe		= ioat_pci_probe,
	.remove		= ioat_remove,
};

static struct ioatdma_device *
alloc_ioatdma(struct pci_dev *pdev, void __iomem *iobase)
{
	struct device *dev = &pdev->dev;
	struct ioatdma_device *d = devm_kzalloc(dev, sizeof(*d), GFP_KERNEL);

	if (!d)
		return NULL;
	d->pdev = pdev;
	d->reg_base = iobase;
	return d;
}

static int ioat_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	void __iomem * const *iomap;
	struct device *dev = &pdev->dev;
	struct ioatdma_device *device;
	int err;

	err = pcim_enable_device(pdev);
	if (err)
		return err;

	err = pcim_iomap_regions(pdev, 1 << IOAT_MMIO_BAR, DRV_NAME);
	if (err)
		return err;
	iomap = pcim_iomap_table(pdev);
	if (!iomap)
		return -ENOMEM;

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err)
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (err)
		return err;

	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err)
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	if (err)
		return err;

	device = alloc_ioatdma(pdev, iomap[IOAT_MMIO_BAR]);
	if (!device)
		return -ENOMEM;
	pci_set_master(pdev);
	pci_set_drvdata(pdev, device);

	device->version = readb(device->reg_base + IOAT_VER_OFFSET);
	if (device->version == IOAT_VER_1_2)
		err = ioat1_dma_probe(device, ioat_dca_enabled);
	else if (device->version == IOAT_VER_2_0)
		err = ioat2_dma_probe(device, ioat_dca_enabled);
	else if (device->version >= IOAT_VER_3_0)
		err = ioat3_dma_probe(device, ioat_dca_enabled);
	else
		return -ENODEV;

	if (err) {
		dev_err(dev, "Intel(R) I/OAT DMA Engine init failed\n");
		return -ENODEV;
	}

	return 0;
}

static void ioat_remove(struct pci_dev *pdev)
{
	struct ioatdma_device *device = pci_get_drvdata(pdev);

	if (!device)
		return;

	if (device->version >= IOAT_VER_3_0)
		ioat3_dma_remove(device);

	dev_err(&pdev->dev, "Removing dma and dca services\n");
	if (device->dca) {
		unregister_dca_provider(device->dca, &pdev->dev);
		free_dca_provider(device->dca);
		device->dca = NULL;
	}
	ioat_dma_remove(device);
}

static int __init ioat_init_module(void)
{
	int err;

	pr_info("%s: Intel(R) QuickData Technology Driver %s\n",
		DRV_NAME, IOAT_DMA_VERSION);

	ioat2_cache = kmem_cache_create("ioat2", sizeof(struct ioat_ring_ent),
					0, SLAB_HWCACHE_ALIGN, NULL);
	if (!ioat2_cache)
		return -ENOMEM;

	err = pci_register_driver(&ioat_pci_driver);
	if (err)
		kmem_cache_destroy(ioat2_cache);

	return err;
}
module_init(ioat_init_module);

static void __exit ioat_exit_module(void)
{
	pci_unregister_driver(&ioat_pci_driver);
	kmem_cache_destroy(ioat2_cache);
}
module_exit(ioat_exit_module);
