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
#include <linux/of.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>

static int maprom_read (struct mtd_info *, loff_t, size_t, size_t *, u_char *);
static int maprom_write (struct mtd_info *, loff_t, size_t, size_t *, const u_char *);
static void maprom_nop (struct mtd_info *);
static struct mtd_info *map_rom_probe(struct map_info *map);
static int maprom_erase (struct mtd_info *mtd, struct erase_info *info);
static int maprom_point (struct mtd_info *mtd, loff_t from, size_t len,
			 size_t *retlen, void **virt, resource_size_t *phys);
static int maprom_unpoint(struct mtd_info *mtd, loff_t from, size_t len);


static struct mtd_chip_driver maprom_chipdrv = {
	.probe	= map_rom_probe,
	.name	= "map_rom",
	.module	= THIS_MODULE
};

static unsigned int default_erasesize(struct map_info *map)
{
	const __be32 *erase_size = NULL;

	erase_size = of_get_property(map->device_node, "erase-size", NULL);

	return !erase_size ? map->size : be32_to_cpu(*erase_size);
}

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
	mtd->_point = maprom_point;
	mtd->_unpoint = maprom_unpoint;
	mtd->_read = maprom_read;
	mtd->_write = maprom_write;
	mtd->_sync = maprom_nop;
	mtd->_erase = maprom_erase;
	mtd->flags = MTD_CAP_ROM;
	mtd->erasesize = default_erasesize(map);
	mtd->writesize = 1;
	mtd->writebufsize = 1;

	__module_get(THIS_MODULE);
	return mtd;
}


static int maprom_point(struct mtd_info *mtd, loff_t from, size_t len,
			size_t *retlen, void **virt, resource_size_t *phys)
{
	struct map_info *map = mtd->priv;

	if (!map->virt)
		return -EINVAL;
	*virt = map->virt + from;
	if (phys)
		*phys = map->phys + from;
	*retlen = len;
	return 0;
}

static int maprom_unpoint(struct mtd_info *mtd, loff_t from, size_t len)
{
	return 0;
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
	return -EROFS;
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
