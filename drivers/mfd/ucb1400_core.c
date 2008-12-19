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
 * If something doesnt work and it worked before spliting, e-mail me,
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
#include <linux/ucb1400.h>

static int ucb1400_core_probe(struct device *dev)
{
	int err;
	struct ucb1400 *ucb;
	struct ucb1400_ts ucb_ts;
	struct snd_ac97 *ac97;

	memset(&ucb_ts, 0, sizeof(ucb_ts));

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

	/* TOUCHSCREEN */
	ucb_ts.ac97 = ac97;
	ucb->ucb1400_ts = platform_device_alloc("ucb1400_ts", -1);
	if (!ucb->ucb1400_ts) {
		err = -ENOMEM;
		goto err0;
	}
	err = platform_device_add_data(ucb->ucb1400_ts, &ucb_ts,
					sizeof(ucb_ts));
	if (err)
		goto err1;
	err = platform_device_add(ucb->ucb1400_ts);
	if (err)
		goto err1;

	return 0;

err1:
	platform_device_put(ucb->ucb1400_ts);
err0:
	kfree(ucb);
err:
	return err;
}

static int ucb1400_core_remove(struct device *dev)
{
	struct ucb1400 *ucb = dev_get_drvdata(dev);

	platform_device_unregister(ucb->ucb1400_ts);
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
