/*
 * Flash memory access on 4G Systems MTX-1 boards
 *
 * $Id: mtx-1_flash.c,v 1.2 2005/11/07 11:14:27 gleixner Exp $
 *
 * (C) 2005 Bruno Randolf <bruno.randolf@4g-systems.biz>
 * (C) 2005 Jörn Engel <joern@wohnheim.fh-wedel.de>
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>

static struct map_info mtx1_map = {
	.name = "MTX-1 flash",
	.bankwidth = 4,
	.size = 0x2000000,
	.phys = 0x1E000000,
};

static struct mtd_partition mtx1_partitions[] = {
        {
                .name = "filesystem",
                .size = 0x01C00000,
                .offset = 0,
        },{
                .name = "yamon",
                .size = 0x00100000,
                .offset = MTDPART_OFS_APPEND,
                .mask_flags = MTD_WRITEABLE,
        },{
                .name = "kernel",
                .size = 0x002c0000,
                .offset = MTDPART_OFS_APPEND,
        },{
                .name = "yamon env",
                .size = 0x00040000,
                .offset = MTDPART_OFS_APPEND,
        }
};

static struct mtd_info *mtx1_mtd;

int __init mtx1_mtd_init(void)
{
	int ret = -ENXIO;

	simple_map_init(&mtx1_map);

	mtx1_map.virt = ioremap(mtx1_map.phys, mtx1_map.size);
	if (!mtx1_map.virt)
		return -EIO;

	mtx1_mtd = do_map_probe("cfi_probe", &mtx1_map);
	if (!mtx1_mtd)
		goto err;

	mtx1_mtd->owner = THIS_MODULE;

	ret = add_mtd_partitions(mtx1_mtd, mtx1_partitions,
			ARRAY_SIZE(mtx1_partitions));
	if (ret)
		goto err;

	return 0;

err:
       iounmap(mtx1_map.virt);
       return ret;
}

static void __exit mtx1_mtd_cleanup(void)
{
	if (mtx1_mtd) {
		del_mtd_partitions(mtx1_mtd);
		map_destroy(mtx1_mtd);
	}
	if (mtx1_map.virt)
		iounmap(mtx1_map.virt);
}

module_init(mtx1_mtd_init);
module_exit(mtx1_mtd_cleanup);

MODULE_AUTHOR("Bruno Randolf <bruno.randolf@4g-systems.biz>");
MODULE_DESCRIPTION("MTX-1 flash map");
MODULE_LICENSE("GPL");
