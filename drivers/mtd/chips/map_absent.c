/*
 * Common code to handle absent "placeholder" devices
 * Copyright 2001 Resilience Corporation <ebrower@resilience.com>
 *
 * This map driver is used to allocate "placeholder" MTD
 * devices on systems that have socketed/removable media.
 * Use of this driver as a fallback preserves the expected
 * registration of MTD device nodes regardless of probe outcome.
 * A usage example is as follows:
 *
 *		my_dev[i] = do_map_probe("cfi", &my_map[i]);
 *		if(NULL == my_dev[i]) {
 *			my_dev[i] = do_map_probe("map_absent", &my_map[i]);
 *		}
 *
 * Any device 'probed' with this driver will return -ENODEV
 * upon open.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>

static int map_absent_read (struct mtd_info *, loff_t, size_t, size_t *, u_char *);
static int map_absent_write (struct mtd_info *, loff_t, size_t, size_t *, const u_char *);
static int map_absent_erase (struct mtd_info *, struct erase_info *);
static void map_absent_sync (struct mtd_info *);
static struct mtd_info *map_absent_probe(struct map_info *map);
static void map_absent_destroy (struct mtd_info *);


static struct mtd_chip_driver map_absent_chipdrv = {
	.probe		= map_absent_probe,
	.destroy	= map_absent_destroy,
	.name		= "map_absent",
	.module		= THIS_MODULE
};

static struct mtd_info *map_absent_probe(struct map_info *map)
{
	struct mtd_info *mtd;

	mtd = kzalloc(sizeof(*mtd), GFP_KERNEL);
	if (!mtd) {
		return NULL;
	}

	map->fldrv 	= &map_absent_chipdrv;
	mtd->priv 	= map;
	mtd->name 	= map->name;
	mtd->type 	= MTD_ABSENT;
	mtd->size 	= map->size;
	mtd->erase 	= map_absent_erase;
	mtd->read 	= map_absent_read;
	mtd->write 	= map_absent_write;
	mtd->sync 	= map_absent_sync;
	mtd->flags 	= 0;
	mtd->erasesize  = PAGE_SIZE;
	mtd->writesize  = 1;

	__module_get(THIS_MODULE);
	return mtd;
}


static int map_absent_read(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	*retlen = 0;
	return -ENODEV;
}

static int map_absent_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf)
{
	*retlen = 0;
	return -ENODEV;
}

static int map_absent_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	return -ENODEV;
}

static void map_absent_sync(struct mtd_info *mtd)
{
	/* nop */
}

static void map_absent_destroy(struct mtd_info *mtd)
{
	/* nop */
}

static int __init map_absent_init(void)
{
	register_mtd_chip_driver(&map_absent_chipdrv);
	return 0;
}

static void __exit map_absent_exit(void)
{
	unregister_mtd_chip_driver(&map_absent_chipdrv);
}

module_init(map_absent_init);
module_exit(map_absent_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Resilience Corporation - Eric Brower <ebrower@resilience.com>");
MODULE_DESCRIPTION("Placeholder MTD chip driver for 'absent' chips");
