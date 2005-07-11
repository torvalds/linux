/*
 *  linux/drivers/mtd/onenand/omap-onenand.c
 *
 *  Copyright (c) 2005 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *
 *  Derived from linux/drivers/mtd/nand/omap-nand-flash.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Overview:
 *   This is a device driver for the OneNAND flash device for TI OMAP boards.
 */

#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/onenand.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>
#include <asm/arch/hardware.h>
#include <asm/arch/tc.h>
#include <asm/sizes.h>

#define OMAP_ONENAND_FLASH_START1	OMAP_CS2A_PHYS
#define OMAP_ONENAND_FLASH_START2	OMAP_CS0_PHYS
/*
 * MTD structure for OMAP board
 */
static struct mtd_info *omap_onenand_mtd = NULL;

/*
 * Define partitions for flash devices
 */

#ifdef CONFIG_MTD_PARTITIONS
static struct mtd_partition static_partition[] = {
	{
		.name		= "X-Loader + U-Boot",
		.offset		= 0,
		.size		= SZ_128K,
		.mask_flags	= MTD_WRITEABLE	/* force read-only */
 	},
	{
		.name		= "U-Boot Environment",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_128K,
		.mask_flags	= MTD_WRITEABLE	/* force read-only */
 	},
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 2 * SZ_1M
	},
	{
		.name		= "filesystem0",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_16M,
	},
	{
		.name		= "filesystem1",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
	},
};

const char *part_probes[] = { "cmdlinepart", NULL,  };

#endif

/* Scan to find existance of the device at base.
   This also allocates oob and data internal buffers */
static char onenand_name[] = "onenand";

/*
 * Main initialization routine
 */
static int __init omap_onenand_init (void)
{
	struct onenand_chip *this;
	struct mtd_partition *dynamic_partition = 0;
	int err = 0;

	/* Allocate memory for MTD device structure and private data */
	omap_onenand_mtd = kmalloc (sizeof(struct mtd_info) + sizeof (struct onenand_chip),
				GFP_KERNEL);
	if (!omap_onenand_mtd) {
		printk (KERN_WARNING "Unable to allocate OneNAND MTD device structure.\n");
		err = -ENOMEM;
		goto out;
	}

	/* Get pointer to private data */
	this = (struct onenand_chip *) (&omap_onenand_mtd[1]);

	/* Initialize structures */
	memset((char *) omap_onenand_mtd, 0, sizeof(struct mtd_info) + sizeof(struct onenand_chip));

	/* Link the private data with the MTD structure */
	omap_onenand_mtd->priv = this;

        /* try the first address */
	this->base = ioremap(OMAP_ONENAND_FLASH_START1, SZ_128K);
	omap_onenand_mtd->name = onenand_name;
	if (onenand_scan(omap_onenand_mtd, 1)){
		/* try the second address */
		iounmap(this->base);
		this->base = ioremap(OMAP_ONENAND_FLASH_START2, SZ_128K);
		if (onenand_scan(omap_onenand_mtd, 1)) {
			iounmap(this->base);
                        err = -ENXIO;
                        goto out_mtd;
		}
	}

	/* Register the partitions */
	switch (omap_onenand_mtd->size) {
	case SZ_128M:
	case SZ_64M:
	case SZ_32M:
#ifdef CONFIG_MTD_PARTITIONS
		err = parse_mtd_partitions(omap_onenand_mtd, part_probes,
					&dynamic_partition, 0);
		if (err > 0)
			err = add_mtd_partitions(omap_onenand_mtd,
					dynamic_partition, err);
		else if (1)
			err = add_mtd_partitions(omap_onenand_mtd,
					static_partition,
					ARRAY_SIZE(static_partition));
		else
#endif
			err = add_mtd_device(omap_onenand_mtd);
		if (err)
			goto out_buf;
		break;

	default:
		printk(KERN_WARNING "Unsupported OneNAND device\n");
		err = -ENXIO;
		goto out_buf;
	}

	return 0;

out_buf:
	onenand_release(omap_onenand_mtd);
	iounmap(this->base);
out_mtd:
	kfree(omap_onenand_mtd);
out:
	return err;
}

/*
 * Clean up routine
 */
static void __exit omap_onenand_cleanup (void)
{
	struct onenand_chip *this = omap_onenand_mtd->priv;

	/* onenand_release frees MTD partitions, MTD structure
	   and onenand internal buffers */
	onenand_release(omap_onenand_mtd);
	iounmap(this->base);
	kfree(omap_onenand_mtd);
}

module_init(omap_onenand_init);
module_exit(omap_onenand_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kyungmin Park <kyungmin.park@samsung.com>");
MODULE_DESCRIPTION("Glue layer for OneNAND flash on OMAP boards");
