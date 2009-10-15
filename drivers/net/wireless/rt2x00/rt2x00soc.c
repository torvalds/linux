/*
	Copyright (C) 2004 - 2009 rt2x00 SourceForge Project
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2x00soc
	Abstract: rt2x00 generic soc device routines.
 */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "rt2x00.h"
#include "rt2x00soc.h"

static void rt2x00soc_free_reg(struct rt2x00_dev *rt2x00dev)
{
	kfree(rt2x00dev->rf);
	rt2x00dev->rf = NULL;

	kfree(rt2x00dev->eeprom);
	rt2x00dev->eeprom = NULL;
}

static int rt2x00soc_alloc_reg(struct rt2x00_dev *rt2x00dev)
{
	struct platform_device *pdev = to_platform_device(rt2x00dev->dev);
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	rt2x00dev->csr.base = (void __iomem *)KSEG1ADDR(res->start);
	if (!rt2x00dev->csr.base)
		goto exit;

	rt2x00dev->eeprom = kzalloc(rt2x00dev->ops->eeprom_size, GFP_KERNEL);
	if (!rt2x00dev->eeprom)
		goto exit;

	rt2x00dev->rf = kzalloc(rt2x00dev->ops->rf_size, GFP_KERNEL);
	if (!rt2x00dev->rf)
		goto exit;

	return 0;

exit:
	ERROR_PROBE("Failed to allocate registers.\n");
	rt2x00soc_free_reg(rt2x00dev);

	return -ENOMEM;
}

int rt2x00soc_probe(struct platform_device *pdev,
		    const unsigned short chipset,
		    const struct rt2x00_ops *ops)
{
	struct ieee80211_hw *hw;
	struct rt2x00_dev *rt2x00dev;
	int retval;

	hw = ieee80211_alloc_hw(sizeof(struct rt2x00_dev), ops->hw);
	if (!hw) {
		ERROR_PROBE("Failed to allocate hardware.\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, hw);

	rt2x00dev = hw->priv;
	rt2x00dev->dev = &pdev->dev;
	rt2x00dev->ops = ops;
	rt2x00dev->hw = hw;
	rt2x00dev->irq = platform_get_irq(pdev, 0);
	rt2x00dev->name = pdev->dev.driver->name;

	rt2x00_set_chip_rt(rt2x00dev, chipset);

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

int rt2x00soc_remove(struct platform_device *pdev)
{
	struct ieee80211_hw *hw = platform_get_drvdata(pdev);
	struct rt2x00_dev *rt2x00dev = hw->priv;

	/*
	 * Free all allocated data.
	 */
	rt2x00lib_remove_dev(rt2x00dev);
	rt2x00soc_free_reg(rt2x00dev);
	ieee80211_free_hw(hw);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00soc_remove);

#ifdef CONFIG_PM
int rt2x00soc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct ieee80211_hw *hw = platform_get_drvdata(pdev);
	struct rt2x00_dev *rt2x00dev = hw->priv;

	return rt2x00lib_suspend(rt2x00dev, state);
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
