/* SPDX-License-Identifier: ISC */

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
	struct mt76_dev *mdev;
	int irq;
	int ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	mem_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mem_base)) {
		dev_err(&pdev->dev, "Failed to get memory resource\n");
		return PTR_ERR(mem_base);
	}

	mdev = mt76_alloc_device(&pdev->dev, sizeof(*dev), &mt7603_ops,
				 &mt7603_drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt7603_dev, mt76);
	mt76_mmio_init(mdev, mem_base);

	mdev->rev = (mt76_rr(dev, MT_HW_CHIPID) << 16) |
		    (mt76_rr(dev, MT_HW_REV) & 0xff);
	dev_info(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	ret = devm_request_irq(mdev->dev, irq, mt7603_irq_handler,
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
