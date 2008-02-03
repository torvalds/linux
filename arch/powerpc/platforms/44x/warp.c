/*
 * PIKA Warp(tm) board specific routines
 *
 * Copyright (c) 2008 PIKA Technologies
 *   Sean MacLennan <smaclennan@pikatech.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/kthread.h>

#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/time.h>
#include <asm/uic.h>

#include "44x.h"


static __initdata struct of_device_id warp_of_bus[] = {
	{ .compatible = "ibm,plb4", },
	{ .compatible = "ibm,opb", },
	{ .compatible = "ibm,ebc", },
	{},
};

static int __init warp_device_probe(void)
{
	of_platform_bus_probe(NULL, warp_of_bus, NULL);
	return 0;
}
machine_device_initcall(warp, warp_device_probe);

static int __init warp_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "pika,warp");
}

define_machine(warp) {
	.name		= "Warp",
	.probe 		= warp_probe,
	.progress 	= udbg_progress,
	.init_IRQ 	= uic_init_tree,
	.get_irq 	= uic_get_irq,
	.restart	= ppc44x_reset_system,
	.calibrate_decr = generic_calibrate_decr,
};


#define LED_GREEN (0x80000000 >> 0)
#define LED_RED   (0x80000000 >> 1)


/* This is for the power LEDs 1 = on, 0 = off, -1 = leave alone */
void warp_set_power_leds(int green, int red)
{
	static void __iomem *gpio_base = NULL;
	unsigned leds;

	if (gpio_base == NULL) {
		struct device_node *np;

		/* Power LEDS are on the second GPIO controller */
		np = of_find_compatible_node(NULL, NULL, "ibm,gpio-440EP");
		if (np)
			np = of_find_compatible_node(np, NULL, "ibm,gpio-440EP");
		if (np == NULL) {
			printk(KERN_ERR __FILE__ ": Unable to find gpio\n");
			return;
		}

		gpio_base = of_iomap(np, 0);
		of_node_put(np);
		if (gpio_base == NULL) {
			printk(KERN_ERR __FILE__ ": Unable to map gpio");
			return;
		}
	}

	leds = in_be32(gpio_base);

	switch (green) {
	case 0: leds &= ~LED_GREEN; break;
	case 1: leds |=  LED_GREEN; break;
	}
	switch (red) {
	case 0: leds &= ~LED_RED; break;
	case 1: leds |=  LED_RED; break;
	}

	out_be32(gpio_base, leds);
}
EXPORT_SYMBOL(warp_set_power_leds);


#ifdef CONFIG_SENSORS_AD7414
static int pika_dtm_thread(void __iomem *fpga)
{
	extern int ad7414_get_temp(int index);

	while (!kthread_should_stop()) {
		int temp = ad7414_get_temp(0);

		out_be32(fpga, temp);

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
	}

	return 0;
}

static int __init pika_dtm_start(void)
{
	struct task_struct *dtm_thread;
	struct device_node *np;
	struct resource res;
	void __iomem *fpga;

	np = of_find_compatible_node(NULL, NULL, "pika,fpga");
	if (np == NULL)
		return -ENOENT;

	/* We do not call of_iomap here since it would map in the entire
	 * fpga space, which is over 8k.
	 */
	if (of_address_to_resource(np, 0, &res)) {
		of_node_put(np);
		return -ENOENT;
	}
	of_node_put(np);

	fpga = ioremap(res.start + 0x20, 4);
	if (fpga == NULL)
		return -ENOENT;

	dtm_thread = kthread_run(pika_dtm_thread, fpga + 0x20, "pika-dtm");
	if (IS_ERR(dtm_thread)) {
		iounmap(fpga);
		return PTR_ERR(dtm_thread);
	}

	return 0;
}
device_initcall(pika_dtm_start);
#endif
