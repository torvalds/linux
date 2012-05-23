/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 */

#include <linux/init.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/io.h>

#include <lantiq_soc.h>

/*
 * By attaching hardware latches to the EBU it is possible to create output
 * only gpios. This driver configures a special memory address, which when
 * written to outputs 16 bit to the latches.
 */

#define LTQ_EBU_BUSCON	0x1e7ff		/* 16 bit access, slowest timing */
#define LTQ_EBU_WP	0x80000000	/* write protect bit */

/* we keep a shadow value of the last value written to the ebu */
static int ltq_ebu_gpio_shadow = 0x0;
static void __iomem *ltq_ebu_gpio_membase;

static void ltq_ebu_apply(void)
{
	unsigned long flags;

	spin_lock_irqsave(&ebu_lock, flags);
	ltq_ebu_w32(LTQ_EBU_BUSCON, LTQ_EBU_BUSCON1);
	*((__u16 *)ltq_ebu_gpio_membase) = ltq_ebu_gpio_shadow;
	ltq_ebu_w32(LTQ_EBU_BUSCON | LTQ_EBU_WP, LTQ_EBU_BUSCON1);
	spin_unlock_irqrestore(&ebu_lock, flags);
}

static void ltq_ebu_set(struct gpio_chip *chip, unsigned offset, int value)
{
	if (value)
		ltq_ebu_gpio_shadow |= (1 << offset);
	else
		ltq_ebu_gpio_shadow &= ~(1 << offset);
	ltq_ebu_apply();
}

static int ltq_ebu_direction_output(struct gpio_chip *chip, unsigned offset,
	int value)
{
	ltq_ebu_set(chip, offset, value);

	return 0;
}

static struct gpio_chip ltq_ebu_chip = {
	.label = "ltq_ebu",
	.direction_output = ltq_ebu_direction_output,
	.set = ltq_ebu_set,
	.base = 72,
	.ngpio = 16,
	.can_sleep = 1,
	.owner = THIS_MODULE,
};

static int ltq_ebu_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!res) {
		dev_err(&pdev->dev, "failed to get memory resource\n");
		return -ENOENT;
	}

	res = devm_request_mem_region(&pdev->dev, res->start,
		resource_size(res), dev_name(&pdev->dev));
	if (!res) {
		dev_err(&pdev->dev, "failed to request memory resource\n");
		return -EBUSY;
	}

	ltq_ebu_gpio_membase = devm_ioremap_nocache(&pdev->dev, res->start,
		resource_size(res));
	if (!ltq_ebu_gpio_membase) {
		dev_err(&pdev->dev, "Failed to ioremap mem region\n");
		return -ENOMEM;
	}

	/* grab the default shadow value passed form the platform code */
	ltq_ebu_gpio_shadow = (unsigned int) pdev->dev.platform_data;

	/* tell the ebu controller which memory address we will be using */
	ltq_ebu_w32(pdev->resource->start | 0x1, LTQ_EBU_ADDRSEL1);

	/* write protect the region */
	ltq_ebu_w32(LTQ_EBU_BUSCON | LTQ_EBU_WP, LTQ_EBU_BUSCON1);

	ret = gpiochip_add(&ltq_ebu_chip);
	if (!ret)
		ltq_ebu_apply();
	return ret;
}

static struct platform_driver ltq_ebu_driver = {
	.probe = ltq_ebu_probe,
	.driver = {
		.name = "ltq_ebu",
		.owner = THIS_MODULE,
	},
};

static int __init ltq_ebu_init(void)
{
	int ret = platform_driver_register(&ltq_ebu_driver);

	if (ret)
		pr_info("ltq_ebu : Error registering platform driver!");
	return ret;
}

postcore_initcall(ltq_ebu_init);
