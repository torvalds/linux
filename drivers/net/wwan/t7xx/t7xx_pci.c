// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 *
 * Contributors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *  Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *  Eliot Lee <eliot.lee@intel.com>
 *  Moises Veleta <moises.veleta@intel.com>
 */

#include <linux/atomic.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "t7xx_mhccif.h"
#include "t7xx_modem_ops.h"
#include "t7xx_pci.h"
#include "t7xx_pcie_mac.h"
#include "t7xx_reg.h"

#define T7XX_PCI_IREG_BASE		0
#define T7XX_PCI_EREG_BASE		2

static int t7xx_request_irq(struct pci_dev *pdev)
{
	struct t7xx_pci_dev *t7xx_dev;
	int ret, i;

	t7xx_dev = pci_get_drvdata(pdev);

	for (i = 0; i < EXT_INT_NUM; i++) {
		const char *irq_descr;
		int irq_vec;

		if (!t7xx_dev->intr_handler[i])
			continue;

		irq_descr = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s_%d",
					   dev_driver_string(&pdev->dev), i);
		if (!irq_descr) {
			ret = -ENOMEM;
			break;
		}

		irq_vec = pci_irq_vector(pdev, i);
		ret = request_threaded_irq(irq_vec, t7xx_dev->intr_handler[i],
					   t7xx_dev->intr_thread[i], 0, irq_descr,
					   t7xx_dev->callback_param[i]);
		if (ret) {
			dev_err(&pdev->dev, "Failed to request IRQ: %d\n", ret);
			break;
		}
	}

	if (ret) {
		while (i--) {
			if (!t7xx_dev->intr_handler[i])
				continue;

			free_irq(pci_irq_vector(pdev, i), t7xx_dev->callback_param[i]);
		}
	}

	return ret;
}

static int t7xx_setup_msix(struct t7xx_pci_dev *t7xx_dev)
{
	struct pci_dev *pdev = t7xx_dev->pdev;
	int ret;

	/* Only using 6 interrupts, but HW-design requires power-of-2 IRQs allocation */
	ret = pci_alloc_irq_vectors(pdev, EXT_INT_NUM, EXT_INT_NUM, PCI_IRQ_MSIX);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to allocate MSI-X entry: %d\n", ret);
		return ret;
	}

	ret = t7xx_request_irq(pdev);
	if (ret) {
		pci_free_irq_vectors(pdev);
		return ret;
	}

	t7xx_pcie_set_mac_msix_cfg(t7xx_dev, EXT_INT_NUM);
	return 0;
}

static int t7xx_interrupt_init(struct t7xx_pci_dev *t7xx_dev)
{
	int ret, i;

	if (!t7xx_dev->pdev->msix_cap)
		return -EINVAL;

	ret = t7xx_setup_msix(t7xx_dev);
	if (ret)
		return ret;

	/* IPs enable interrupts when ready */
	for (i = 0; i < EXT_INT_NUM; i++)
		t7xx_pcie_mac_set_int(t7xx_dev, i);

	return 0;
}

static void t7xx_pci_infracfg_ao_calc(struct t7xx_pci_dev *t7xx_dev)
{
	t7xx_dev->base_addr.infracfg_ao_base = t7xx_dev->base_addr.pcie_ext_reg_base +
					      INFRACFG_AO_DEV_CHIP -
					      t7xx_dev->base_addr.pcie_dev_reg_trsl_addr;
}

static int t7xx_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct t7xx_pci_dev *t7xx_dev;
	int ret;

	t7xx_dev = devm_kzalloc(&pdev->dev, sizeof(*t7xx_dev), GFP_KERNEL);
	if (!t7xx_dev)
		return -ENOMEM;

	pci_set_drvdata(pdev, t7xx_dev);
	t7xx_dev->pdev = pdev;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = pcim_iomap_regions(pdev, BIT(T7XX_PCI_IREG_BASE) | BIT(T7XX_PCI_EREG_BASE),
				 pci_name(pdev));
	if (ret) {
		dev_err(&pdev->dev, "Could not request BARs: %d\n", ret);
		return -ENOMEM;
	}

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pdev->dev, "Could not set PCI DMA mask: %d\n", ret);
		return ret;
	}

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pdev->dev, "Could not set consistent PCI DMA mask: %d\n", ret);
		return ret;
	}

	IREG_BASE(t7xx_dev) = pcim_iomap_table(pdev)[T7XX_PCI_IREG_BASE];
	t7xx_dev->base_addr.pcie_ext_reg_base = pcim_iomap_table(pdev)[T7XX_PCI_EREG_BASE];

	t7xx_pcie_mac_atr_init(t7xx_dev);
	t7xx_pci_infracfg_ao_calc(t7xx_dev);
	t7xx_mhccif_init(t7xx_dev);

	ret = t7xx_md_init(t7xx_dev);
	if (ret)
		return ret;

	t7xx_pcie_mac_interrupts_dis(t7xx_dev);

	ret = t7xx_interrupt_init(t7xx_dev);
	if (ret) {
		t7xx_md_exit(t7xx_dev);
		return ret;
	}

	t7xx_pcie_mac_set_int(t7xx_dev, MHCCIF_INT);
	t7xx_pcie_mac_interrupts_en(t7xx_dev);

	return 0;
}

static void t7xx_pci_remove(struct pci_dev *pdev)
{
	struct t7xx_pci_dev *t7xx_dev;
	int i;

	t7xx_dev = pci_get_drvdata(pdev);
	t7xx_md_exit(t7xx_dev);

	for (i = 0; i < EXT_INT_NUM; i++) {
		if (!t7xx_dev->intr_handler[i])
			continue;

		free_irq(pci_irq_vector(pdev, i), t7xx_dev->callback_param[i]);
	}

	pci_free_irq_vectors(t7xx_dev->pdev);
}

static const struct pci_device_id t7xx_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x4d75) },
	{ }
};
MODULE_DEVICE_TABLE(pci, t7xx_pci_table);

static struct pci_driver t7xx_pci_driver = {
	.name = "mtk_t7xx",
	.id_table = t7xx_pci_table,
	.probe = t7xx_pci_probe,
	.remove = t7xx_pci_remove,
};

module_pci_driver(t7xx_pci_driver);

MODULE_AUTHOR("MediaTek Inc");
MODULE_DESCRIPTION("MediaTek PCIe 5G WWAN modem T7xx driver");
MODULE_LICENSE("GPL");
