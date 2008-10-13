/*
 * NOR Flash memory access on TI Toto board
 *
 * jzhang@ti.com (C) 2003 Texas Instruments.
 *
 *  (C) 2002 MontVista Software, Inc.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/hardware.h>
#include <asm/io.h>


#ifndef CONFIG_ARCH_OMAP
#error This is for OMAP architecture only
#endif

//these lines need be moved to a hardware header file
#define OMAP_TOTO_FLASH_BASE 0xd8000000
#define OMAP_TOTO_FLASH_SIZE 0x80000

static struct map_info omap_toto_map_flash = {
	.name =		"OMAP Toto flash",
	.bankwidth =	2,
	.virt =		(void __iomem *)OMAP_TOTO_FLASH_BASE,
};


static struct mtd_partition toto_flash_partitions[] = {
	{
		.name =		"BootLoader",
		.size =		0x00040000,     /* hopefully u-boot will stay 128k + 128*/
		.offset =	0,
		.mask_flags =	MTD_WRITEABLE,  /* force read-only */
	}, {
		.name =		"ReservedSpace",
		.size =		0x00030000,
		.offset =	MTDPART_OFS_APPEND,
		//mask_flags:	MTD_WRITEABLE,  /* force read-only */
	}, {
		.name =		"EnvArea",      /* bottom 64KiB for env vars */
		.size =		MTDPART_SIZ_FULL,
		.offset =	MTDPART_OFS_APPEND,
	}
};

static struct mtd_partition *parsed_parts;

static struct mtd_info *flash_mtd;

static int __init init_flash (void)
{

	struct mtd_partition *parts;
	int nb_parts = 0;
	int parsed_nr_parts = 0;
	const char *part_type;

	/*
	 * Static partition definition selection
	 */
	part_type = "static";

 	parts = toto_flash_partitions;
	nb_parts = ARRAY_SIZE(toto_flash_partitions);
	omap_toto_map_flash.size = OMAP_TOTO_FLASH_SIZE;
	omap_toto_map_flash.phys = virt_to_phys(OMAP_TOTO_FLASH_BASE);

	simple_map_init(&omap_toto_map_flash);
	/*
	 * Now let's probe for the actual flash.  Do it here since
	 * specific machine settings might have been set above.
	 */
	printk(KERN_NOTICE "OMAP toto flash: probing %d-bit flash bus\n",
		omap_toto_map_flash.bankwidth*8);
	flash_mtd = do_map_probe("jedec_probe", &omap_toto_map_flash);
	if (!flash_mtd)
		return -ENXIO;

 	if (parsed_nr_parts > 0) {
		parts = parsed_parts;
		nb_parts = parsed_nr_parts;
	}

	if (nb_parts == 0) {
		printk(KERN_NOTICE "OMAP toto flash: no partition info available,"
			"registering whole flash at once\n");
		if (add_mtd_device(flash_mtd)){
            return -ENXIO;
        }
	} else {
		printk(KERN_NOTICE "Using %s partition definition\n",
			part_type);
		return add_mtd_partitions(flash_mtd, parts, nb_parts);
	}
	return 0;
}

int __init omap_toto_mtd_init(void)
{
	int status;

 	if (status = init_flash()) {
		printk(KERN_ERR "OMAP Toto Flash: unable to init map for toto flash\n");
	}
    return status;
}

static void  __exit omap_toto_mtd_cleanup(void)
{
	if (flash_mtd) {
		del_mtd_partitions(flash_mtd);
		map_destroy(flash_mtd);
		kfree(parsed_parts);
	}
}

module_init(omap_toto_mtd_init);
module_exit(omap_toto_mtd_cleanup);

MODULE_AUTHOR("Jian Zhang");
MODULE_DESCRIPTION("OMAP Toto board map driver");
MODULE_LICENSE("GPL");
