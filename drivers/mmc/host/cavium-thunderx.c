/*
 * Driver for MMC and SSD cards for Cavium ThunderX SOCs.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2016 Cavium Inc.
 */
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/mmc/mmc.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include "cavium.h"

static void thunder_mmc_acquire_bus(struct cvm_mmc_host *host)
{
	down(&host->mmc_serializer);
}

static void thunder_mmc_release_bus(struct cvm_mmc_host *host)
{
	up(&host->mmc_serializer);
}

static void thunder_mmc_int_enable(struct cvm_mmc_host *host, u64 val)
{
	writeq(val, host->base + MIO_EMM_INT(host));
	writeq(val, host->base + MIO_EMM_INT_EN_SET(host));
}

static int thunder_mmc_register_interrupts(struct cvm_mmc_host *host,
					   struct pci_dev *pdev)
{
	int nvec, ret, i;

	nvec = pci_alloc_irq_vectors(pdev, 1, 9, PCI_IRQ_MSIX);
	if (nvec < 0)
		return nvec;

	/* register interrupts */
	for (i = 0; i < nvec; i++) {
		ret = devm_request_irq(&pdev->dev, pci_irq_vector(pdev, i),
				       cvm_mmc_interrupt,
				       0, cvm_mmc_irq_names[i], host);
		if (ret)
			return ret;
	}
	return 0;
}

static int thunder_mmc_probe(struct pci_dev *pdev,
			     const struct pci_device_id *id)
{
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct device_node *child_node;
	struct cvm_mmc_host *host;
	int ret, i = 0;

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	pci_set_drvdata(pdev, host);
	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pci_request_regions(pdev, KBUILD_MODNAME);
	if (ret)
		return ret;

	host->base = pcim_iomap(pdev, 0, pci_resource_len(pdev, 0));
	if (!host->base)
		return -EINVAL;

	/* On ThunderX these are identical */
	host->dma_base = host->base;

	host->reg_off = 0x2000;
	host->reg_off_dma = 0x160;

	host->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(host->clk))
		return PTR_ERR(host->clk);

	ret = clk_prepare_enable(host->clk);
	if (ret)
		return ret;
	host->sys_freq = clk_get_rate(host->clk);

	spin_lock_init(&host->irq_handler_lock);
	sema_init(&host->mmc_serializer, 1);

	host->dev = dev;
	host->acquire_bus = thunder_mmc_acquire_bus;
	host->release_bus = thunder_mmc_release_bus;
	host->int_enable = thunder_mmc_int_enable;

	host->use_sg = true;
	host->big_dma_addr = true;
	host->need_irq_handler_lock = true;
	host->last_slot = -1;

	ret = dma_set_mask(dev, DMA_BIT_MASK(48));
	if (ret)
		goto error;

	/*
	 * Clear out any pending interrupts that may be left over from
	 * bootloader. Writing 1 to the bits clears them.
	 */
	writeq(127, host->base + MIO_EMM_INT_EN(host));
	writeq(3, host->base + MIO_EMM_DMA_INT_ENA_W1C(host));
	/* Clear DMA FIFO */
	writeq(BIT_ULL(16), host->base + MIO_EMM_DMA_FIFO_CFG(host));

	ret = thunder_mmc_register_interrupts(host, pdev);
	if (ret)
		goto error;

	for_each_child_of_node(node, child_node) {
		/*
		 * mmc_of_parse and devm* require one device per slot.
		 * Create a dummy device per slot and set the node pointer to
		 * the slot. The easiest way to get this is using
		 * of_platform_device_create.
		 */
		if (of_device_is_compatible(child_node, "mmc-slot")) {
			host->slot_pdev[i] = of_platform_device_create(child_node, NULL,
								       &pdev->dev);
			if (!host->slot_pdev[i])
				continue;

			ret = cvm_mmc_of_slot_probe(&host->slot_pdev[i]->dev, host);
			if (ret)
				goto error;
		}
		i++;
	}
	dev_info(dev, "probed\n");
	return 0;

error:
	for (i = 0; i < CAVIUM_MAX_MMC; i++) {
		if (host->slot[i])
			cvm_mmc_of_slot_remove(host->slot[i]);
		if (host->slot_pdev[i]) {
			get_device(&host->slot_pdev[i]->dev);
			of_platform_device_destroy(&host->slot_pdev[i]->dev, NULL);
			put_device(&host->slot_pdev[i]->dev);
		}
	}
	clk_disable_unprepare(host->clk);
	return ret;
}

static void thunder_mmc_remove(struct pci_dev *pdev)
{
	struct cvm_mmc_host *host = pci_get_drvdata(pdev);
	u64 dma_cfg;
	int i;

	for (i = 0; i < CAVIUM_MAX_MMC; i++)
		if (host->slot[i])
			cvm_mmc_of_slot_remove(host->slot[i]);

	dma_cfg = readq(host->dma_base + MIO_EMM_DMA_CFG(host));
	dma_cfg &= ~MIO_EMM_DMA_CFG_EN;
	writeq(dma_cfg, host->dma_base + MIO_EMM_DMA_CFG(host));

	clk_disable_unprepare(host->clk);
}

static const struct pci_device_id thunder_mmc_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, 0xa010) },
	{ 0, }  /* end of table */
};

static struct pci_driver thunder_mmc_driver = {
	.name = KBUILD_MODNAME,
	.id_table = thunder_mmc_id_table,
	.probe = thunder_mmc_probe,
	.remove = thunder_mmc_remove,
};

module_pci_driver(thunder_mmc_driver);

MODULE_AUTHOR("Cavium Inc.");
MODULE_DESCRIPTION("Cavium ThunderX eMMC Driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, thunder_mmc_id_table);
