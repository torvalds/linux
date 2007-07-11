/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 by Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/init.h>
#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

static char mipsnet_string[] = "mipsnet";

static struct platform_device eth1_device = {
	.name		= mipsnet_string,
	.id		= 0,
};

/*
 * Create a platform device for the GPI port that receives the
 * image data from the embedded camera.
 */
static int __init mipsnet_devinit(void)
{
	int err;

	err = platform_device_register(&eth1_device);
	if (err)
		printk(KERN_ERR "%s: registration failed\n", mipsnet_string);

	return err;
}

device_initcall(mipsnet_devinit);
