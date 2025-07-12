// SPDX-License-Identifier: GPL-2.0-or-later
/*
	Copyright (C) 2004 - 2009 Ivo van Doorn <IvDoorn@gmail.com>
	Copyright (C) 2004 - 2009 Felix Fietkau <nbd@openwrt.org>
	<http://rt2x00.serialmonkey.com>

 */

/*
	Module: rt2x00soc
	Abstract: rt2x00 generic soc device routines.
 */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "rt2x00.h"
#include "rt2x00soc.h"

static void rt2x00soc_free_reg(struct rt2x00_dev *rt2x00dev)
{
	kfree(rt2x00dev->rf);
	rt2x00dev->rf = NULL;

	kfree(rt2x00dev->eeprom);
	rt2x00dev->eeprom = NULL;

	iounmap(rt2x00dev->csr.base);
}

static int rt2x00soc_alloc_reg(struct rt2x00_dev *rt2x00dev)
{
	struct platform_device *pdev = to_platform_device(rt2x00dev->dev);
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	rt2x00dev->csr.base = ioremap(res->start, resource_size(res));
	if (!rt2x00dev->csr.base)
		return -ENOMEM;

	rt2x00dev->eeprom = kzalloc(rt2x00dev->ops->eeprom_size, GFP_KERNEL);
	if (!rt2x00dev->eeprom)
		goto exit;

	rt2x00dev->rf = kzalloc(rt2x00dev->ops->rf_size, GFP_KERNEL);
	if (!rt2x00dev->rf)
		goto exit;

	return 0;

exit:
	rt2x00_probe_err("Failed to allocate registers\n");
	rt2x00soc_free_reg(rt2x00dev);

	return -ENOMEM;
}

int rt2x00soc_probe(struct platform_device *pdev, const struct rt2x00_ops *ops)
{
	struct ieee80211_hw *hw;
	struct rt2x00_dev *rt2x00dev;
	int retval;

	hw = ieee80211_alloc_hw(sizeof(struct rt2x00_dev), ops->hw);
	if (!hw) {
		rt2x00_probe_err("Failed to allocate hardware\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, hw);

	rt2x00dev = hw->priv;
	rt2x00dev->dev = &pdev->dev;
	rt2x00dev->ops = ops;
	rt2x00dev->hw = hw;
	rt2x00dev->irq = platform_get_irq(pdev, 0);
	rt2x00dev->name = pdev->dev.driver->name;

	rt2x00dev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(rt2x00dev->clk))
		rt2x00dev->clk = NULL;

	rt2x00_set_chip_intf(rt2x00dev, RT2X00_CHIP_INTF_SOC);

	retval = rt2x00soc_alloc_reg(rt2x00dev);
	if (retval)
		goto exit_free_device;

	retval = rt2x00lib_probe_dev(rt2x00dev);
	if (retval)
		goto exit_free_reg;

	return 0;

exit_free_reg:
	rt2x00soc_free_reg(rt2x00dev);

exit_free_device:
	ieee80211_free_hw(hw);

	return retval;
}
EXPORT_SYMBOL_GPL(rt2x00soc_probe);

void rt2x00soc_remove(struct platform_device *pdev)
{
	struct ieee80211_hw *hw = platform_get_drvdata(pdev);
	struct rt2x00_dev *rt2x00dev = hw->priv;

	/*
	 * Free all allocated data.
	 */
	rt2x00lib_remove_dev(rt2x00dev);
	rt2x00soc_free_reg(rt2x00dev);
	ieee80211_free_hw(hw);
}
EXPORT_SYMBOL_GPL(rt2x00soc_remove);

#ifdef CONFIG_PM
int rt2x00soc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct ieee80211_hw *hw = platform_get_drvdata(pdev);
	struct rt2x00_dev *rt2x00dev = hw->priv;

	return rt2x00lib_suspend(rt2x00dev);
}
EXPORT_SYMBOL_GPL(rt2x00soc_suspend);

int rt2x00soc_resume(struct platform_device *pdev)
{
	struct ieee80211_hw *hw = platform_get_drvdata(pdev);
	struct rt2x00_dev *rt2x00dev = hw->priv;

	return rt2x00lib_resume(rt2x00dev);
}
EXPORT_SYMBOL_GPL(rt2x00soc_resume);
#endif /* CONFIG_PM */

/*
 * rt2x00soc module information.
 */
MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("rt2x00 soc library");
MODULE_LICENSE("GPL");
