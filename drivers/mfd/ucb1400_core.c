/*
 * Core functions for:
 *  Philips UCB1400 multifunction chip
 *
 * Based on ucb1400_ts.c:
 *  Author:	Nicolas Pitre
 *  Created:	September 25, 2006
 *  Copyright:	MontaVista Software, Inc.
 *
 * Spliting done by: Marek Vasut <marek.vasut@gmail.com>
 * If something doesn't work and it worked before spliting, e-mail me,
 * dont bother Nicolas please ;-)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This code is heavily based on ucb1x00-*.c copyrighted by Russell King
 * covering the UCB1100, UCB1200 and UCB1300..  Support for the UCB1400 has
 * been made separate from ucb1x00-core/ucb1x00-ts on Russell's request.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/ucb1400.h>

unsigned int ucb1400_adc_read(struct snd_ac97 *ac97, u16 adc_channel,
		int adcsync)
{
	unsigned int val;

	if (adcsync)
		adc_channel |= UCB_ADC_SYNC_ENA;

	ucb1400_reg_write(ac97, UCB_ADC_CR, UCB_ADC_ENA | adc_channel);
	ucb1400_reg_write(ac97, UCB_ADC_CR, UCB_ADC_ENA | adc_channel |
			UCB_ADC_START);

	while (!((val = ucb1400_reg_read(ac97, UCB_ADC_DATA))
				& UCB_ADC_DAT_VALID))
		schedule_timeout_uninterruptible(1);

	return val & UCB_ADC_DAT_MASK;
}
EXPORT_SYMBOL_GPL(ucb1400_adc_read);

static int ucb1400_core_probe(struct device *dev)
{
	int err;
	struct ucb1400 *ucb;
	struct ucb1400_ts ucb_ts;
	struct ucb1400_gpio ucb_gpio;
	struct snd_ac97 *ac97;
	struct ucb1400_pdata *pdata = dev_get_platdata(dev);

	memset(&ucb_ts, 0, sizeof(ucb_ts));
	memset(&ucb_gpio, 0, sizeof(ucb_gpio));

	ucb = kzalloc(sizeof(struct ucb1400), GFP_KERNEL);
	if (!ucb) {
		err = -ENOMEM;
		goto err;
	}

	dev_set_drvdata(dev, ucb);

	ac97 = to_ac97_t(dev);

	ucb_ts.id = ucb1400_reg_read(ac97, UCB_ID);
	if (ucb_ts.id != UCB_ID_1400) {
		err = -ENODEV;
		goto err0;
	}

	/* GPIO */
	ucb_gpio.ac97 = ac97;
	if (pdata) {
		ucb_gpio.gpio_setup = pdata->gpio_setup;
		ucb_gpio.gpio_teardown = pdata->gpio_teardown;
		ucb_gpio.gpio_offset = pdata->gpio_offset;
	}
	ucb->ucb1400_gpio = platform_device_alloc("ucb1400_gpio", -1);
	if (!ucb->ucb1400_gpio) {
		err = -ENOMEM;
		goto err0;
	}
	err = platform_device_add_data(ucb->ucb1400_gpio, &ucb_gpio,
					sizeof(ucb_gpio));
	if (err)
		goto err1;
	err = platform_device_add(ucb->ucb1400_gpio);
	if (err)
		goto err1;

	/* TOUCHSCREEN */
	ucb_ts.ac97 = ac97;

	if (pdata != NULL && pdata->irq >= 0)
		ucb_ts.irq = pdata->irq;
	else
		ucb_ts.irq = -1;

	ucb->ucb1400_ts = platform_device_alloc("ucb1400_ts", -1);
	if (!ucb->ucb1400_ts) {
		err = -ENOMEM;
		goto err2;
	}
	err = platform_device_add_data(ucb->ucb1400_ts, &ucb_ts,
					sizeof(ucb_ts));
	if (err)
		goto err3;
	err = platform_device_add(ucb->ucb1400_ts);
	if (err)
		goto err3;

	return 0;

err3:
	platform_device_put(ucb->ucb1400_ts);
err2:
	platform_device_del(ucb->ucb1400_gpio);
err1:
	platform_device_put(ucb->ucb1400_gpio);
err0:
	kfree(ucb);
err:
	return err;
}

static int ucb1400_core_remove(struct device *dev)
{
	struct ucb1400 *ucb = dev_get_drvdata(dev);

	platform_device_unregister(ucb->ucb1400_ts);
	platform_device_unregister(ucb->ucb1400_gpio);

	kfree(ucb);
	return 0;
}

static struct device_driver ucb1400_core_driver = {
	.name	= "ucb1400_core",
	.bus	= &ac97_bus_type,
	.probe	= ucb1400_core_probe,
	.remove	= ucb1400_core_remove,
};

static int __init ucb1400_core_init(void)
{
	return driver_register(&ucb1400_core_driver);
}

static void __exit ucb1400_core_exit(void)
{
	driver_unregister(&ucb1400_core_driver);
}

module_init(ucb1400_core_init);
module_exit(ucb1400_core_exit);

MODULE_DESCRIPTION("Philips UCB1400 driver");
MODULE_LICENSE("GPL");
