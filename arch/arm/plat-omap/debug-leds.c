/*
 * linux/arch/arm/plat-omap/debug-leds.c
 *
 * Copyright 2011 by Bryan Wu <bryan.wu@canonical.com>
 * Copyright 2003 by Texas Instruments Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/io.h>
#include <linux/platform_data/gpio-omap.h>
#include <linux/slab.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>

#include "fpga.h"

/* Many OMAP development platforms reuse the same "debug board"; these
 * platforms include H2, H3, H4, and Perseus2.  There are 16 LEDs on the
 * debug board (all green), accessed through FPGA registers.
 */

static struct h2p2_dbg_fpga __iomem *fpga;

static u16 fpga_led_state;

struct dbg_led {
	struct led_classdev	cdev;
	u16			mask;
};

static const struct {
	const char *name;
	const char *trigger;
} dbg_leds[] = {
	{ "dbg:d4", "heartbeat", },
	{ "dbg:d5", "cpu0", },
	{ "dbg:d6", "default-on", },
	{ "dbg:d7", },
	{ "dbg:d8", },
	{ "dbg:d9", },
	{ "dbg:d10", },
	{ "dbg:d11", },
	{ "dbg:d12", },
	{ "dbg:d13", },
	{ "dbg:d14", },
	{ "dbg:d15", },
	{ "dbg:d16", },
	{ "dbg:d17", },
	{ "dbg:d18", },
	{ "dbg:d19", },
};

/*
 * The triggers lines up below will only be used if the
 * LED triggers are compiled in.
 */
static void dbg_led_set(struct led_classdev *cdev,
			      enum led_brightness b)
{
	struct dbg_led *led = container_of(cdev, struct dbg_led, cdev);
	u16 reg;

	reg = __raw_readw(&fpga->leds);
	if (b != LED_OFF)
		reg |= led->mask;
	else
		reg &= ~led->mask;
	__raw_writew(reg, &fpga->leds);
}

static enum led_brightness dbg_led_get(struct led_classdev *cdev)
{
	struct dbg_led *led = container_of(cdev, struct dbg_led, cdev);
	u16 reg;

	reg = __raw_readw(&fpga->leds);
	return (reg & led->mask) ? LED_FULL : LED_OFF;
}

static int fpga_probe(struct platform_device *pdev)
{
	struct resource	*iomem;
	int i;

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iomem)
		return -ENODEV;

	fpga = ioremap(iomem->start, H2P2_DBG_FPGA_SIZE);
	__raw_writew(0xff, &fpga->leds);

	for (i = 0; i < ARRAY_SIZE(dbg_leds); i++) {
		struct dbg_led *led;

		led = kzalloc(sizeof(*led), GFP_KERNEL);
		if (!led)
			break;

		led->cdev.name = dbg_leds[i].name;
		led->cdev.brightness_set = dbg_led_set;
		led->cdev.brightness_get = dbg_led_get;
		led->cdev.default_trigger = dbg_leds[i].trigger;
		led->mask = BIT(i);

		if (led_classdev_register(NULL, &led->cdev) < 0) {
			kfree(led);
			break;
		}
	}

	return 0;
}

static int fpga_suspend_noirq(struct device *dev)
{
	fpga_led_state = __raw_readw(&fpga->leds);
	__raw_writew(0xff, &fpga->leds);

	return 0;
}

static int fpga_resume_noirq(struct device *dev)
{
	__raw_writew(~fpga_led_state, &fpga->leds);
	return 0;
}

static const struct dev_pm_ops fpga_dev_pm_ops = {
	.suspend_noirq = fpga_suspend_noirq,
	.resume_noirq = fpga_resume_noirq,
};

static struct platform_driver led_driver = {
	.driver.name	= "omap_dbg_led",
	.driver.pm	= &fpga_dev_pm_ops,
	.probe		= fpga_probe,
};

static int __init fpga_init(void)
{
	if (machine_is_omap_h4()
			|| machine_is_omap_h3()
			|| machine_is_omap_h2()
			|| machine_is_omap_perseus2()
			)
		return platform_driver_register(&led_driver);
	return 0;
}
fs_initcall(fpga_init);
