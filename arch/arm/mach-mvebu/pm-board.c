/*
 * Board-level suspend/resume support.
 *
 * Copyright (C) 2014-2015 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include "common.h"

#define ARMADA_PIC_NR_GPIOS 3

static void __iomem *gpio_ctrl;
static int pic_gpios[ARMADA_PIC_NR_GPIOS];
static int pic_raw_gpios[ARMADA_PIC_NR_GPIOS];

static void mvebu_armada_pm_enter(void __iomem *sdram_reg, u32 srcmd)
{
	u32 reg, ackcmd;
	int i;

	/* Put 001 as value on the GPIOs */
	reg = readl(gpio_ctrl);
	for (i = 0; i < ARMADA_PIC_NR_GPIOS; i++)
		reg &= ~BIT(pic_raw_gpios[i]);
	reg |= BIT(pic_raw_gpios[0]);
	writel(reg, gpio_ctrl);

	/* Prepare writing 111 to the GPIOs */
	ackcmd = readl(gpio_ctrl);
	for (i = 0; i < ARMADA_PIC_NR_GPIOS; i++)
		ackcmd |= BIT(pic_raw_gpios[i]);

	srcmd = cpu_to_le32(srcmd);
	ackcmd = cpu_to_le32(ackcmd);

	/*
	 * Wait a while, the PIC needs quite a bit of time between the
	 * two GPIO commands.
	 */
	mdelay(3000);

	asm volatile (
		/* Align to a cache line */
		".balign 32\n\t"

		/* Enter self refresh */
		"str %[srcmd], [%[sdram_reg]]\n\t"

		/*
		 * Wait 100 cycles for DDR to enter self refresh, by
		 * doing 50 times two instructions.
		 */
		"mov r1, #50\n\t"
		"1: subs r1, r1, #1\n\t"
		"bne 1b\n\t"

		/* Issue the command ACK */
		"str %[ackcmd], [%[gpio_ctrl]]\n\t"

		/* Trap the processor */
		"b .\n\t"
		: : [srcmd] "r" (srcmd), [sdram_reg] "r" (sdram_reg),
		  [ackcmd] "r" (ackcmd), [gpio_ctrl] "r" (gpio_ctrl) : "r1");
}

static int __init mvebu_armada_pm_init(void)
{
	struct device_node *np;
	struct device_node *gpio_ctrl_np;
	int ret = 0, i;

	if (!of_machine_is_compatible("marvell,axp-gp"))
		return -ENODEV;

	np = of_find_node_by_name(NULL, "pm_pic");
	if (!np)
		return -ENODEV;

	for (i = 0; i < ARMADA_PIC_NR_GPIOS; i++) {
		char *name;
		struct of_phandle_args args;

		pic_gpios[i] = of_get_named_gpio(np, "ctrl-gpios", i);
		if (pic_gpios[i] < 0) {
			ret = -ENODEV;
			goto out;
		}

		name = kasprintf(GFP_KERNEL, "pic-pin%d", i);
		if (!name) {
			ret = -ENOMEM;
			goto out;
		}

		ret = gpio_request(pic_gpios[i], name);
		if (ret < 0) {
			kfree(name);
			goto out;
		}

		ret = gpio_direction_output(pic_gpios[i], 0);
		if (ret < 0) {
			gpio_free(pic_gpios[i]);
			kfree(name);
			goto out;
		}

		ret = of_parse_phandle_with_fixed_args(np, "ctrl-gpios", 2,
						       i, &args);
		if (ret < 0) {
			gpio_free(pic_gpios[i]);
			kfree(name);
			goto out;
		}

		gpio_ctrl_np = args.np;
		pic_raw_gpios[i] = args.args[0];
	}

	gpio_ctrl = of_iomap(gpio_ctrl_np, 0);
	if (!gpio_ctrl)
		return -ENOMEM;

	mvebu_pm_suspend_init(mvebu_armada_pm_enter);

out:
	of_node_put(np);
	return ret;
}

/*
 * Registering the mvebu_board_pm_enter callback must be done before
 * the platform_suspend_ops will be registered. In the same time we
 * also need to have the gpio devices registered. That's why we use a
 * device_initcall_sync which is called after all the device_initcall
 * (used by the gpio device) but before the late_initcall (used to
 * register the platform_suspend_ops)
 */
device_initcall_sync(mvebu_armada_pm_init);
