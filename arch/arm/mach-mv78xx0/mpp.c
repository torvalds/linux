/*
 * arch/arm/mach-mv78x00/mpp.c
 *
 * MPP functions for Marvell MV78x00 SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <plat/mpp.h>
#include <mach/hardware.h>
#include "common.h"
#include "mpp.h"

static unsigned int __init mv78xx0_variant(void)
{
	u32 dev, rev;

	mv78xx0_pcie_id(&dev, &rev);

	if (dev == MV78100_DEV_ID && rev >= MV78100_REV_A0)
		return MPP_78100_A0_MASK;

	printk(KERN_ERR "MPP setup: unknown mv78x00 variant "
			"(dev %#x rev %#x)\n", dev, rev);
	return 0;
}

void __init mv78xx0_mpp_conf(unsigned int *mpp_list)
{
	orion_mpp_conf(mpp_list, mv78xx0_variant(),
		       MPP_MAX, DEV_BUS_VIRT_BASE);
}
