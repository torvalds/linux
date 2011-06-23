/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2007 John Crispin <blogic@openwrt.org>
 *
 */

#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include <lantiq_soc.h>

#define LTQ_STP_CON0		0x00
#define LTQ_STP_CON1		0x04
#define LTQ_STP_CPU0		0x08
#define LTQ_STP_CPU1		0x0C
#define LTQ_STP_AR		0x10

#define LTQ_STP_CON_SWU		(1 << 31)
#define LTQ_STP_2HZ		0
#define LTQ_STP_4HZ		(1 << 23)
#define LTQ_STP_8HZ		(2 << 23)
#define LTQ_STP_10HZ		(3 << 23)
#define LTQ_STP_SPEED_MASK	(0xf << 23)
#define LTQ_STP_UPD_FPI		(1 << 31)
#define LTQ_STP_UPD_MASK	(3 << 30)
#define LTQ_STP_ADSL_SRC	(3 << 24)

#define LTQ_STP_GROUP0		(1 << 0)

#define LTQ_STP_RISING		0
#define LTQ_STP_FALLING		(1 << 26)
#define LTQ_STP_EDGE_MASK	(1 << 26)

#define ltq_stp_r32(reg)	__raw_readl(ltq_stp_membase + reg)
#define ltq_stp_w32(val, reg)	__raw_writel(val, ltq_stp_membase + reg)
#define ltq_stp_w32_mask(clear, set, reg) \
		ltq_w32((ltq_r32(ltq_stp_membase + reg) & ~(clear)) | (set), \
		ltq_stp_membase + (reg))

static int ltq_stp_shadow = 0xffff;
static void __iomem *ltq_stp_membase;

static void ltq_stp_set(struct gpio_chip *chip, unsigned offset, int value)
{
	if (value)
		ltq_stp_shadow |= (1 << offset);
	else
		ltq_stp_shadow &= ~(1 << offset);
	ltq_stp_w32(ltq_stp_shadow, LTQ_STP_CPU0);
}

static int ltq_stp_direction_output(struct gpio_chip *chip, unsigned offset,
	int value)
{
	ltq_stp_set(chip, offset, value);

	return 0;
}

static struct gpio_chip ltq_stp_chip = {
	.label = "ltq_stp",
	.direction_output = ltq_stp_direction_output,
	.set = ltq_stp_set,
	.base = 48,
	.ngpio = 24,
	.can_sleep = 1,
	.owner = THIS_MODULE,
};

static int ltq_stp_hw_init(void)
{
	/* the 3 pins used to control the external stp */
	ltq_gpio_request(4, 1, 0, 1, "stp-st");
	ltq_gpio_request(5, 1, 0, 1, "stp-d");
	ltq_gpio_request(6, 1, 0, 1, "stp-sh");

	/* sane defaults */
	ltq_stp_w32(0, LTQ_STP_AR);
	ltq_stp_w32(0, LTQ_STP_CPU0);
	ltq_stp_w32(0, LTQ_STP_CPU1);
	ltq_stp_w32(LTQ_STP_CON_SWU, LTQ_STP_CON0);
	ltq_stp_w32(0, LTQ_STP_CON1);

	/* rising or falling edge */
	ltq_stp_w32_mask(LTQ_STP_EDGE_MASK, LTQ_STP_FALLING, LTQ_STP_CON0);

	/* per default stp 15-0 are set */
	ltq_stp_w32_mask(0, LTQ_STP_GROUP0, LTQ_STP_CON1);

	/* stp are update periodically by the FPI bus */
	ltq_stp_w32_mask(LTQ_STP_UPD_MASK, LTQ_STP_UPD_FPI, LTQ_STP_CON1);

	/* set stp update speed */
	ltq_stp_w32_mask(LTQ_STP_SPEED_MASK, LTQ_STP_8HZ, LTQ_STP_CON1);

	/* tell the hardware that pin (led) 0 and 1 are controlled
	 *  by the dsl arc
	 */
	ltq_stp_w32_mask(0, LTQ_STP_ADSL_SRC, LTQ_STP_CON0);

	ltq_pmu_enable(PMU_LED);
	return 0;
}

static int __devinit ltq_stp_probe(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	int ret = 0;

	if (!res)
		return -ENOENT;
	res = devm_request_mem_region(&pdev->dev, res->start,
		resource_size(res), dev_name(&pdev->dev));
	if (!res) {
		dev_err(&pdev->dev, "failed to request STP memory\n");
		return -EBUSY;
	}
	ltq_stp_membase = devm_ioremap_nocache(&pdev->dev, res->start,
		resource_size(res));
	if (!ltq_stp_membase) {
		dev_err(&pdev->dev, "failed to remap STP memory\n");
		return -ENOMEM;
	}
	ret = gpiochip_add(&ltq_stp_chip);
	if (!ret)
		ret = ltq_stp_hw_init();

	return ret;
}

static struct platform_driver ltq_stp_driver = {
	.probe = ltq_stp_probe,
	.driver = {
		.name = "ltq_stp",
		.owner = THIS_MODULE,
	},
};

int __init ltq_stp_init(void)
{
	int ret = platform_driver_register(&ltq_stp_driver);

	if (ret)
		pr_info("ltq_stp: error registering platfom driver");
	return ret;
}

postcore_initcall(ltq_stp_init);
