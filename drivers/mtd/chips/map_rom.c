/*
 * Common code to handle map devices which are simple ROM
 * (C) 2000 Red Hat. GPL'd.
 * $Id: map_rom.c,v 1.23 2005/01/05 18:05:12 dwmw2 Exp $
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/compatmac.h>

static int maprom_read (struct mtd_info *, loff_t, size_t, size_t *, u_char *);
static int maprom_write (struct mtd_info *, loff_t, size_t, size_t *, const u_char *);
static void maprom_nop (struct mtd_info *);
static struct mtd_info *map_rom_probe(struct map_info *map);

static struct mtd_chip_driver maprom_chipdrv = {
	.probe	= map_rom_probe,
	.name	= "map_rom",
	.module	= THIS_MODULE
};

static struct mtd_info *map_rom_probe(struct map_info *map)
{
	struct mtd_info *mtd;

	mtd = kmalloc(sizeof(*mtd), GFP_KERNEL);
	if (!mtd)
		return NULL;

	memset(mtd, 0, sizeof(*mtd));

	map->fldrv = &maprom_chipdrv;
	mtd->priv = map;
	mtd->name = map->name;
	mtd->type = MTD_ROM;
	mtd->size = map->size;
	mtd->read = maprom_read;
	mtd->write = maprom_write;
	mtd->sync = maprom_nop;
	mtd->flags = MTD_CAP_ROM;
	mtd->erasesize = map->size;

	__module_get(THIS_MODULE);
	return mtd;
}


static int maprom_read (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	struct map_info *map = mtd->priv;

	map_copy_from(map, buf, from, len);
	*retlen = len;
	return 0;
}

static void maprom_nop(struct mtd_info *mtd)
{
	/* Nothing to see here */
}

static int maprom_write (struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf)
{
	printk(KERN_NOTICE "maprom_write called\n");
	return -EIO;
}

static int __init map_rom_init(void)
{
	register_mtd_chip_driver(&maprom_chipdrv);
	return 0;
}

static void __exit map_rom_exit(void)
{
	unregister_mtd_chip_driver(&maprom_chipdrv);
}

module_init(map_rom_init);
module_exit(map_rom_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("MTD chip driver for ROM chips");
