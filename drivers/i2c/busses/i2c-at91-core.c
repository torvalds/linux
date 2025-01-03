// SPDX-License-Identifier: GPL-2.0
/*
 *  i2c Support for Atmel's AT91 Two-Wire Interface (TWI)
 *
 *  Copyright (C) 2011 Weinmann Medical GmbH
 *  Author: Nikolaus Voss <n.voss@weinmann.de>
 *
 *  Evolved from original work by:
 *  Copyright (C) 2004 Rick Bronson
 *  Converted to 2.6 by Andrew Victor <andrew@sanpeople.com>
 *
 *  Borrowed heavily from original work by:
 *  Copyright (C) 2000 Philip Edelbrock <phil@stimpy.netroedge.com>
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/consumer.h>

#include "i2c-at91.h"

unsigned at91_twi_read(struct at91_twi_dev *dev, unsigned reg)
{
	return readl_relaxed(dev->base + reg);
}

void at91_twi_write(struct at91_twi_dev *dev, unsigned reg, unsigned val)
{
	writel_relaxed(val, dev->base + reg);
}

void at91_disable_twi_interrupts(struct at91_twi_dev *dev)
{
	at91_twi_write(dev, AT91_TWI_IDR, AT91_TWI_INT_MASK);
}

void at91_twi_irq_save(struct at91_twi_dev *dev)
{
	dev->imr = at91_twi_read(dev, AT91_TWI_IMR) & AT91_TWI_INT_MASK;
	at91_disable_twi_interrupts(dev);
}

void at91_twi_irq_restore(struct at91_twi_dev *dev)
{
	at91_twi_write(dev, AT91_TWI_IER, dev->imr);
}

void at91_init_twi_bus(struct at91_twi_dev *dev)
{
	at91_disable_twi_interrupts(dev);
	at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_SWRST);
	if (dev->slave_detected)
		at91_init_twi_bus_slave(dev);
	else
		at91_init_twi_bus_master(dev);
}

static struct at91_twi_pdata at91rm9200_config = {
	.clk_max_div = 5,
	.clk_offset = 3,
	.has_unre_flag = true,
};

static struct at91_twi_pdata at91sam9261_config = {
	.clk_max_div = 5,
	.clk_offset = 4,
};

static struct at91_twi_pdata at91sam9260_config = {
	.clk_max_div = 7,
	.clk_offset = 4,
};

static struct at91_twi_pdata at91sam9g20_config = {
	.clk_max_div = 7,
	.clk_offset = 4,
};

static struct at91_twi_pdata at91sam9g10_config = {
	.clk_max_div = 7,
	.clk_offset = 4,
};

static const struct platform_device_id at91_twi_devtypes[] = {
	{
		.name = "i2c-at91rm9200",
		.driver_data = (unsigned long) &at91rm9200_config,
	}, {
		.name = "i2c-at91sam9261",
		.driver_data = (unsigned long) &at91sam9261_config,
	}, {
		.name = "i2c-at91sam9260",
		.driver_data = (unsigned long) &at91sam9260_config,
	}, {
		.name = "i2c-at91sam9g20",
		.driver_data = (unsigned long) &at91sam9g20_config,
	}, {
		.name = "i2c-at91sam9g10",
		.driver_data = (unsigned long) &at91sam9g10_config,
	}, {
		/* sentinel */
	}
};

#if defined(CONFIG_OF)
static struct at91_twi_pdata at91sam9x5_config = {
	.clk_max_div = 7,
	.clk_offset = 4,
};

static struct at91_twi_pdata sama5d4_config = {
	.clk_max_div = 7,
	.clk_offset = 4,
	.has_hold_field = true,
	.has_dig_filtr = true,
};

static struct at91_twi_pdata sama5d2_config = {
	.clk_max_div = 7,
	.clk_offset = 3,
	.has_unre_flag = true,
	.has_alt_cmd = true,
	.has_hold_field = true,
	.has_dig_filtr = true,
	.has_adv_dig_filtr = true,
	.has_ana_filtr = true,
	.has_clear_cmd = false,	/* due to errata, CLEAR cmd is not working */
};

static struct at91_twi_pdata sam9x60_config = {
	.clk_max_div = 7,
	.clk_offset = 3,
	.has_unre_flag = true,
	.has_alt_cmd = true,
	.has_hold_field = true,
	.has_dig_filtr = true,
	.has_adv_dig_filtr = true,
	.has_ana_filtr = true,
	.has_clear_cmd = true,
};

static const struct of_device_id atmel_twi_dt_ids[] = {
	{
		.compatible = "atmel,at91rm9200-i2c",
		.data = &at91rm9200_config,
	}, {
		.compatible = "atmel,at91sam9260-i2c",
		.data = &at91sam9260_config,
	}, {
		.compatible = "atmel,at91sam9261-i2c",
		.data = &at91sam9261_config,
	}, {
		.compatible = "atmel,at91sam9g20-i2c",
		.data = &at91sam9g20_config,
	}, {
		.compatible = "atmel,at91sam9g10-i2c",
		.data = &at91sam9g10_config,
	}, {
		.compatible = "atmel,at91sam9x5-i2c",
		.data = &at91sam9x5_config,
	}, {
		.compatible = "atmel,sama5d4-i2c",
		.data = &sama5d4_config,
	}, {
		.compatible = "atmel,sama5d2-i2c",
		.data = &sama5d2_config,
	}, {
		.compatible = "microchip,sam9x60-i2c",
		.data = &sam9x60_config,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, atmel_twi_dt_ids);
#endif

static struct at91_twi_pdata *at91_twi_get_driver_data(
					struct platform_device *pdev)
{
	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(atmel_twi_dt_ids, pdev->dev.of_node);
		if (!match)
			return NULL;
		return (struct at91_twi_pdata *)match->data;
	}
	return (struct at91_twi_pdata *) platform_get_device_id(pdev)->driver_data;
}

static int at91_twi_probe(struct platform_device *pdev)
{
	struct at91_twi_dev *dev;
	struct resource *mem;
	int rc;
	u32 phy_addr;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->dev = &pdev->dev;

	dev->base = devm_platform_get_and_ioremap_resource(pdev, 0, &mem);
	if (IS_ERR(dev->base))
		return PTR_ERR(dev->base);
	phy_addr = mem->start;

	dev->pdata = at91_twi_get_driver_data(pdev);
	if (!dev->pdata)
		return -ENODEV;

	dev->irq = platform_get_irq(pdev, 0);
	if (dev->irq < 0)
		return dev->irq;

	platform_set_drvdata(pdev, dev);

	dev->clk = devm_clk_get_enabled(dev->dev, NULL);
	if (IS_ERR(dev->clk))
		return dev_err_probe(dev->dev, PTR_ERR(dev->clk),
				     "failed to enable clock\n");

	snprintf(dev->adapter.name, sizeof(dev->adapter.name), "AT91");
	i2c_set_adapdata(&dev->adapter, dev);
	dev->adapter.owner = THIS_MODULE;
	dev->adapter.class = I2C_CLASS_DEPRECATED;
	dev->adapter.dev.parent = dev->dev;
	dev->adapter.nr = pdev->id;
	dev->adapter.timeout = AT91_I2C_TIMEOUT;
	dev->adapter.dev.of_node = pdev->dev.of_node;

	dev->slave_detected = i2c_detect_slave_mode(&pdev->dev);

	if (dev->slave_detected)
		rc = at91_twi_probe_slave(pdev, phy_addr, dev);
	else
		rc = at91_twi_probe_master(pdev, phy_addr, dev);
	if (rc)
		return rc;

	at91_init_twi_bus(dev);

	pm_runtime_set_autosuspend_delay(dev->dev, AUTOSUSPEND_TIMEOUT);
	pm_runtime_use_autosuspend(dev->dev);
	pm_runtime_set_active(dev->dev);
	pm_runtime_enable(dev->dev);

	rc = i2c_add_numbered_adapter(&dev->adapter);
	if (rc) {
		pm_runtime_disable(dev->dev);
		pm_runtime_set_suspended(dev->dev);

		return rc;
	}

	dev_info(dev->dev, "AT91 i2c bus driver (hw version: %#x).\n",
		 at91_twi_read(dev, AT91_TWI_VER));
	return 0;
}

static void at91_twi_remove(struct platform_device *pdev)
{
	struct at91_twi_dev *dev = platform_get_drvdata(pdev);

	i2c_del_adapter(&dev->adapter);

	pm_runtime_disable(dev->dev);
	pm_runtime_set_suspended(dev->dev);
}

static int __maybe_unused at91_twi_runtime_suspend(struct device *dev)
{
	struct at91_twi_dev *twi_dev = dev_get_drvdata(dev);

	clk_disable_unprepare(twi_dev->clk);

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int __maybe_unused at91_twi_runtime_resume(struct device *dev)
{
	struct at91_twi_dev *twi_dev = dev_get_drvdata(dev);

	pinctrl_pm_select_default_state(dev);

	return clk_prepare_enable(twi_dev->clk);
}

static int __maybe_unused at91_twi_suspend_noirq(struct device *dev)
{
	if (!pm_runtime_status_suspended(dev))
		at91_twi_runtime_suspend(dev);

	return 0;
}

static int __maybe_unused at91_twi_resume_noirq(struct device *dev)
{
	struct at91_twi_dev *twi_dev = dev_get_drvdata(dev);
	int ret;

	if (!pm_runtime_status_suspended(dev)) {
		ret = at91_twi_runtime_resume(dev);
		if (ret)
			return ret;
	}

	pm_runtime_mark_last_busy(dev);
	pm_request_autosuspend(dev);

	at91_init_twi_bus(twi_dev);

	return 0;
}

static const struct dev_pm_ops __maybe_unused at91_twi_pm = {
	.suspend_noirq	= at91_twi_suspend_noirq,
	.resume_noirq	= at91_twi_resume_noirq,
	.runtime_suspend	= at91_twi_runtime_suspend,
	.runtime_resume		= at91_twi_runtime_resume,
};

static struct platform_driver at91_twi_driver = {
	.probe		= at91_twi_probe,
	.remove		= at91_twi_remove,
	.id_table	= at91_twi_devtypes,
	.driver		= {
		.name	= "at91_i2c",
		.of_match_table = of_match_ptr(atmel_twi_dt_ids),
		.pm	= pm_ptr(&at91_twi_pm),
	},
};

static int __init at91_twi_init(void)
{
	return platform_driver_register(&at91_twi_driver);
}

static void __exit at91_twi_exit(void)
{
	platform_driver_unregister(&at91_twi_driver);
}

subsys_initcall(at91_twi_init);
module_exit(at91_twi_exit);

MODULE_AUTHOR("Nikolaus Voss <n.voss@weinmann.de>");
MODULE_DESCRIPTION("I2C (TWI) driver for Atmel AT91");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:at91_i2c");
