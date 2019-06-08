/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * The software is provided "as is"; the copyright holders disclaim
 * all warranties and liabilities, to the extent permitted by
 * applicable law.
 */

/* A trivial fmc driver that can load a gateware file and reports interrupts */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/fmc.h>

static struct fmc_driver t_drv; /* initialized later */

static irqreturn_t t_handler(int irq, void *dev_id)
{
	struct fmc_device *fmc = dev_id;

	fmc_irq_ack(fmc);
	dev_info(&fmc->dev, "received irq %i\n", irq);
	return IRQ_HANDLED;
}

static struct fmc_gpio t_gpio[] = {
	{
		.gpio = FMC_GPIO_IRQ(0),
		.mode = GPIOF_DIR_IN,
		.irqmode = IRQF_TRIGGER_RISING,
	}, {
		.gpio = FMC_GPIO_IRQ(1),
		.mode = GPIOF_DIR_IN,
		.irqmode = IRQF_TRIGGER_RISING,
	}
};

static int t_probe(struct fmc_device *fmc)
{
	int ret;
	int index = 0;

	index = fmc_validate(fmc, &t_drv);
	if (index < 0)
		return -EINVAL; /* not our device: invalid */

	ret = fmc_irq_request(fmc, t_handler, "fmc-trivial", IRQF_SHARED);
	if (ret < 0)
		return ret;
	/* ignore error code of call below, we really don't care */
	fmc_gpio_config(fmc, t_gpio, ARRAY_SIZE(t_gpio));

	ret = fmc_reprogram(fmc, &t_drv, "", 0);
	if (ret == -EPERM) /* programming not supported */
		ret = 0;
	if (ret < 0)
		fmc_irq_free(fmc);

	/* FIXME: reprogram LM32 too */
	return ret;
}

static int t_remove(struct fmc_device *fmc)
{
	fmc_irq_free(fmc);
	return 0;
}

static struct fmc_driver t_drv = {
	.version = FMC_VERSION,
	.driver.name = KBUILD_MODNAME,
	.probe = t_probe,
	.remove = t_remove,
	/* no table, as the current match just matches everything */
};

 /* We accept the generic parameters */
FMC_PARAM_BUSID(t_drv);
FMC_PARAM_GATEWARE(t_drv);

static int t_init(void)
{
	int ret;

	ret = fmc_driver_register(&t_drv);
	return ret;
}

static void t_exit(void)
{
	fmc_driver_unregister(&t_drv);
}

module_init(t_init);
module_exit(t_exit);

MODULE_LICENSE("Dual BSD/GPL");
