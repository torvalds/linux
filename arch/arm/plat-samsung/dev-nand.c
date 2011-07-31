/*
 * S3C series device definition for nand device
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <mach/map.h>
#include <plat/devs.h>
#include <plat/nand.h>

static struct resource s3c_nand_resource[] = {
	[0] = {
		.start = S3C_PA_NAND,
		.end   = S3C_PA_NAND + SZ_1M,
		.flags = IORESOURCE_MEM,
	}
};

struct platform_device s3c_device_nand = {
	.name		  = "s3c2410-nand",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(s3c_nand_resource),
	.resource	  = s3c_nand_resource,
};

EXPORT_SYMBOL(s3c_device_nand);

/**
 * s3c_nand_copy_set() - copy nand set data
 * @set: The new structure, directly copied from the old.
 *
 * Copy all the fields from the NAND set field from what is probably __initdata
 * to new kernel memory. The code returns 0 if the copy happened correctly or
 * an error code for the calling function to display.
 *
 * Note, we currently do not try and look to see if we've already copied the
 * data in a previous set.
 */
static int __init s3c_nand_copy_set(struct s3c2410_nand_set *set)
{
	void *ptr;
	int size;

	size = sizeof(struct mtd_partition) * set->nr_partitions;
	if (size) {
		ptr = kmemdup(set->partitions, size, GFP_KERNEL);
		set->partitions = ptr;

		if (!ptr)
			return -ENOMEM;
	}
	
	if (set->nr_map && set->nr_chips) {
		size = sizeof(int) * set->nr_chips;
		ptr = kmemdup(set->nr_map, size, GFP_KERNEL);
		set->nr_map = ptr;

		if (!ptr)
			return -ENOMEM;
	}

	if (set->ecc_layout) {
		ptr = kmemdup(set->ecc_layout,
			      sizeof(struct nand_ecclayout), GFP_KERNEL);
		set->ecc_layout = ptr;

		if (!ptr)
			return -ENOMEM;
	}
	
	return 0;
}

void __init s3c_nand_set_platdata(struct s3c2410_platform_nand *nand)
{
	struct s3c2410_platform_nand *npd;
	int size;
	int ret;

	/* note, if we get a failure in allocation, we simply drop out of the
	 * function. If there is so little memory available at initialisation
	 * time then there is little chance the system is going to run.
	 */ 

	npd = kmemdup(nand, sizeof(struct s3c2410_platform_nand), GFP_KERNEL);
	if (!npd) {
		printk(KERN_ERR "%s: failed copying platform data\n", __func__);
		return;
	}

	/* now see if we need to copy any of the nand set data */

	size = sizeof(struct s3c2410_nand_set) * npd->nr_sets;
	if (size) {
		struct s3c2410_nand_set *from = npd->sets;
		struct s3c2410_nand_set *to;
		int i;

		to = kmemdup(from, size, GFP_KERNEL);
		npd->sets = to;	/* set, even if we failed */

		if (!to) {
			printk(KERN_ERR "%s: no memory for sets\n", __func__);
			return;
		}
		
		for (i = 0; i < npd->nr_sets; i++) {
			ret = s3c_nand_copy_set(to);
			if (ret) {
				printk(KERN_ERR "%s: failed to copy set %d\n",
				__func__, i);
				return;
			}
			to++;
		}
	}

	s3c_device_nand.dev.platform_data = npd;
}

EXPORT_SYMBOL_GPL(s3c_nand_set_platdata);
