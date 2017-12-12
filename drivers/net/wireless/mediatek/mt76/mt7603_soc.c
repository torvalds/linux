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
#include <linux/platform_device.h>

#include "mt7603.h"

static int
mt76_wmac_probe(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct mt7603_dev *dev;
	void __iomem *mem_base;
	int irq;
	int ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Failed to get device IRQ\n");
		return irq;
	}

	mem_base = devm_ioremap_resource(&pdev->dev, res);
	if (!mem_base) {
		dev_err(&pdev->dev, "Failed to get memory resource\n");
		return -EINVAL;
	}

	dev = mt7603_alloc_device(&pdev->dev);
	if (!dev)
		return -ENOMEM;

	mt76_mmio_init(&dev->mt76, mem_base);

	dev->mt76.rev = (mt76_rr(dev, MT_HW_CHIPID) << 16) |
			(mt76_rr(dev, MT_HW_REV) & 0xff);
	dev_printk(KERN_INFO, dev->mt76.dev, "ASIC revision: %04x\n", dev->mt76.rev);

	ret = devm_request_irq(dev->mt76.dev, irq, mt7603_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto error;

	ret = mt7603_register_device(dev);
	if (ret)
		goto error;

	return 0;
error:
	ieee80211_free_hw(mt76_hw(dev));
	return ret;
}

static int
mt76_wmac_remove(struct platform_device *pdev)
{
	struct mt76_dev *mdev = platform_get_drvdata(pdev);
	struct mt7603_dev *dev = container_of(mdev, struct mt7603_dev, mt76);

	mt7603_unregister_device(dev);

	return 0;
}

static const struct of_device_id of_wmac_match[] = {
	{ .compatible = "mediatek,mt7628-wmac" },
	{},
};

MODULE_DEVICE_TABLE(of, of_wmac_match);
MODULE_FIRMWARE(MT7628_FIRMWARE_E1);
MODULE_FIRMWARE(MT7628_FIRMWARE_E2);

struct platform_driver mt76_wmac_driver = {
	.probe		= mt76_wmac_probe,
	.remove		= mt76_wmac_remove,
	.driver = {
		.name = "mt76_wmac",
		.of_match_table = of_wmac_match,
	},
};
