/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mt76x2.h"

static const struct pci_device_id mt76pci_device_table[] = {
	{ PCI_DEVICE(0x14c3, 0x7662) },
	{ PCI_DEVICE(0x14c3, 0x7612) },
	{ PCI_DEVICE(0x14c3, 0x7602) },
	{ },
};

static int
mt76pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct mt76x02_dev *dev;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	dev = mt76x2_alloc_device(&pdev->dev);
	if (!dev)
		return -ENOMEM;

	mt76_mmio_init(&dev->mt76, pcim_iomap_table(pdev)[0]);
	mt76x2_reset_wlan(dev, false);

	dev->mt76.rev = mt76_rr(dev, MT_ASIC_VERSION);
	dev_info(dev->mt76.dev, "ASIC revision: %08x\n", dev->mt76.rev);

	ret = devm_request_irq(dev->mt76.dev, pdev->irq, mt76x02_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto error;

	ret = mt76x2_register_device(dev);
	if (ret)
		goto error;

	/* Fix up ASPM configuration */

	/* RG_SSUSB_G1_CDR_BIR_LTR = 0x9 */
	mt76_rmw_field(dev, 0x15a10, 0x1f << 16, 0x9);

	/* RG_SSUSB_G1_CDR_BIC_LTR = 0xf */
	mt76_rmw_field(dev, 0x15a0c, 0xf << 28, 0xf);

	/* RG_SSUSB_CDR_BR_PE1D = 0x3 */
	mt76_rmw_field(dev, 0x15c58, 0x3 << 6, 0x3);

	return 0;

error:
	ieee80211_free_hw(mt76_hw(dev));
	return ret;
}

static void
mt76pci_remove(struct pci_dev *pdev)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);

	mt76_unregister_device(mdev);
	mt76x2_cleanup(dev);
	ieee80211_free_hw(mdev->hw);
}

MODULE_DEVICE_TABLE(pci, mt76pci_device_table);
MODULE_FIRMWARE(MT7662_FIRMWARE);
MODULE_FIRMWARE(MT7662_ROM_PATCH);
MODULE_LICENSE("Dual BSD/GPL");

static struct pci_driver mt76pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt76pci_device_table,
	.probe		= mt76pci_probe,
	.remove		= mt76pci_remove,
};

module_pci_driver(mt76pci_driver);
