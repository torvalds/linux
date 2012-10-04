/*
 * arch/arm/plat-orion/mpp.c
 *
 * MPP functions for Marvell orion SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mbus.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <mach/hardware.h>
#include <plat/orion-gpio.h>
#include <plat/mpp.h>

/* Address of the ith MPP control register */
static __init unsigned long mpp_ctrl_addr(unsigned int i,
					  unsigned long dev_bus)
{
	return dev_bus + (i) * 4;
}


void __init orion_mpp_conf(unsigned int *mpp_list, unsigned int variant_mask,
			   unsigned int mpp_max, unsigned int dev_bus)
{
	unsigned int mpp_nr_regs = (1 + mpp_max/8);
	u32 mpp_ctrl[mpp_nr_regs];
	int i;

	printk(KERN_DEBUG "initial MPP regs:");
	for (i = 0; i < mpp_nr_regs; i++) {
		mpp_ctrl[i] = readl(mpp_ctrl_addr(i, dev_bus));
		printk(" %08x", mpp_ctrl[i]);
	}
	printk("\n");

	for ( ; *mpp_list; mpp_list++) {
		unsigned int num = MPP_NUM(*mpp_list);
		unsigned int sel = MPP_SEL(*mpp_list);
		int shift, gpio_mode;

		if (num > mpp_max) {
			printk(KERN_ERR "orion_mpp_conf: invalid MPP "
					"number (%u)\n", num);
			continue;
		}
		if (variant_mask & !(*mpp_list & variant_mask)) {
			printk(KERN_WARNING
			       "orion_mpp_conf: requested MPP%u config "
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

		orion_gpio_set_valid(num, gpio_mode);
	}

	printk(KERN_DEBUG "  final MPP regs:");
	for (i = 0; i < mpp_nr_regs; i++) {
		writel(mpp_ctrl[i], mpp_ctrl_addr(i, dev_bus));
		printk(" %08x", mpp_ctrl[i]);
	}
	printk("\n");
}
