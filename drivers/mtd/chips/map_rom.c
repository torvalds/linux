/*
 * Common code to handle map devices which are simple ROM
 * (C) 2000 Red Hat. GPL'd.
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

static int maprom_read (struct mtd_info *, loff_t, size_t, size_t *, u_char *);
static int maprom_write (struct mtd_info *, loff_t, size_t, size_t *, const u_char *);
static void maprom_nop (struct mtd_info *);
static struct mtd_info *map_rom_probe(struct map_info *map);
static int maprom_erase (struct mtd_info *mtd, struct erase_info *info);
static unsigned long maprom_unmapped_area(struct mtd_info *, unsigned long,
					  unsigned long, unsigned long);

static struct mtd_chip_driver maprom_chipdrv = {
	.probe	= map_rom_probe,
	.name	= "map_rom",
	.module	= THIS_MODULE
};

static struct mtd_info *map_rom_probe(struct map_info *map)
{
	struct mtd_info *mtd;

	mtd = kzalloc(sizeof(*mtd), GFP_KERNEL);
	if (!mtd)
		return NULL;

	map->fldrv = &maprom_chipdrv;
	mtd->priv = map;
	mtd->name = map->name;
	mtd->type = MTD_ROM;
	mtd->size = map->size;
	mtd->get_unmapped_area = maprom_unmapped_area;
	mtd->read = maprom_read;
	mtd->write = maprom_write;
	mtd->sync = maprom_nop;
	mtd->erase = maprom_erase;
	mtd->flags = MTD_CAP_ROM;
	mtd->erasesize = map->size;
	mtd->writesize = 1;

	__module_get(THIS_MODULE);
	return mtd;
}


/*
 * Allow NOMMU mmap() to directly map the device (if not NULL)
 * - return the address to which the offset maps
 * - return -ENOSYS to indicate refusal to do the mapping
 */
static unsigned long maprom_unmapped_area(struct mtd_info *mtd,
					  unsigned long len,
					  unsigned long offset,
					  unsigned long flags)
{
	struct map_info *map = mtd->priv;
	return (unsigned long) map->virt + offset;
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

static int maprom_erase (struct mtd_info *mtd, struct erase_info *info)
{
	/* We do our best 8) */
	return -EROFS;
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
