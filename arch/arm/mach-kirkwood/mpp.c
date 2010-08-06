/*
 * arch/arm/mach-kirkwood/mpp.c
 *
 * MPP functions for Marvell Kirkwood SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mbus.h>
#include <linux/io.h>
#include <asm/gpio.h>
#include <mach/hardware.h>
#include "common.h"
#include "mpp.h"

static unsigned int __init kirkwood_variant(void)
{
	u32 dev, rev;

	kirkwood_pcie_id(&dev, &rev);

	if ((dev == MV88F6281_DEV_ID && rev >= MV88F6281_REV_A0) ||
	    (dev == MV88F6282_DEV_ID))
		return MPP_F6281_MASK;
	if (dev == MV88F6192_DEV_ID && rev >= MV88F6192_REV_A0)
		return MPP_F6192_MASK;
	if (dev == MV88F6180_DEV_ID)
		return MPP_F6180_MASK;

	printk(KERN_ERR "MPP setup: unknown kirkwood variant "
			"(dev %#x rev %#x)\n", dev, rev);
	return 0;
}

#define MPP_CTRL(i)	(DEV_BUS_VIRT_BASE + (i) * 4)
#define MPP_NR_REGS	(1 + MPP_MAX/8)

void __init kirkwood_mpp_conf(unsigned int *mpp_list)
{
	u32 mpp_ctrl[MPP_NR_REGS];
	unsigned int variant_mask;
	int i;

	variant_mask = kirkwood_variant();
	if (!variant_mask)
		return;

	/* Initialize gpiolib. */
	orion_gpio_init();

	printk(KERN_DEBUG "initial MPP regs:");
	for (i = 0; i < MPP_NR_REGS; i++) {
		mpp_ctrl[i] = readl(MPP_CTRL(i));
		printk(" %08x", mpp_ctrl[i]);
	}
	printk("\n");

	while (*mpp_list) {
		unsigned int num = MPP_NUM(*mpp_list);
		unsigned int sel = MPP_SEL(*mpp_list);
		int shift, gpio_mode;

		if (num > MPP_MAX) {
			printk(KERN_ERR "kirkwood_mpp_conf: invalid MPP "
					"number (%u)\n", num);
			continue;
		}
		if (!(*mpp_list & variant_mask)) {
			printk(KERN_WARNING
			       "kirkwood_mpp_conf: requested MPP%u config "
			       "unavailable on this hardware\n", num);
			continue;
		}

		shift = (num & 7) << 2;
		mpp_ctrl[num / 8] &= ~(0xf << shift);
		mpp_ctrl[num / 8] |= sel << shift;

		gpio_mode = 0;
		if (*mpp_list & MPP_INPUT_MASK)
			gpio_mode |= GPIO_INPUT_OK;
		if (*mpp_list & MPP_OUTPUT_MASK)
			gpio_mode |= GPIO_OUTPUT_OK;
		if (sel != 0)
			gpio_mode = 0;
		orion_gpio_set_valid(num, gpio_mode);

		mpp_list++;
	}

	printk(KERN_DEBUG "  final MPP regs:");
	for (i = 0; i < MPP_NR_REGS; i++) {
		writel(mpp_ctrl[i], MPP_CTRL(i));
		printk(" %08x", mpp_ctrl[i]);
	}
	printk("\n");
}
