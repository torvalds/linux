/*
 * arch/arm/mach-kirkwood/mpp.c
 *
 * MPP functions for Marvell Kirkwood SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <plat/mpp.h>
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

	pr_err("MPP setup: unknown kirkwood variant (dev %#x rev %#x)\n",
	       dev, rev);
	return 0;
}

void __init kirkwood_mpp_conf(unsigned int *mpp_list)
{
	orion_mpp_conf(mpp_list, kirkwood_variant(),
		       MPP_MAX, DEV_BUS_VIRT_BASE);
}
