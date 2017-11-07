/*
 * Common code to handle map devices which are simple RAM
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


static int mapram_read (struct mtd_info *, loff_t, size_t, size_t *, u_char *);
static int mapram_write (struct mtd_info *, loff_t, size_t, size_t *, const u_char *);
static int mapram_erase (struct mtd_info *, struct erase_info *);
static void mapram_nop (struct mtd_info *);
static struct mtd_info *map_ram_probe(struct map_info *map);
static int mapram_point (struct mtd_info *mtd, loff_t from, size_t len,
			 size_t *retlen, void **virt, resource_size_t *phys);
static int mapram_unpoint(struct mtd_info *mtd, loff_t from, size_t len);


static struct mtd_chip_driver mapram_chipdrv = {
	.probe	= map_ram_probe,
	.name	= "map_ram",
	.module	= THIS_MODULE
};

static struct mtd_info *map_ram_probe(struct map_info *map)
{
	struct mtd_info *mtd;

	/* Check the first byte is RAM */
#if 0
	map_write8(map, 0x55, 0);
	if (map_read8(map, 0) != 0x55)
		return NULL;

	map_write8(map, 0xAA, 0);
	if (map_read8(map, 0) != 0xAA)
		return NULL;

	/* Check the last byte is RAM */
	map_write8(map, 0x55, map->size-1);
	if (map_read8(map, map->size-1) != 0x55)
		return NULL;

	map_write8(map, 0xAA, map->size-1);
	if (map_read8(map, map->size-1) != 0xAA)
		return NULL;
#endif
	/* OK. It seems to be RAM. */

	mtd = kzalloc(sizeof(*mtd), GFP_KERNEL);
	if (!mtd)
		return NULL;

	map->fldrv = &mapram_chipdrv;
	mtd->priv = map;
	mtd->name = map->name;
	mtd->type = MTD_RAM;
	mtd->size = map->size;
	mtd->_erase = mapram_erase;
	mtd->_read = mapram_read;
	mtd->_write = mapram_write;
	mtd->_panic_write = mapram_write;
	mtd->_point = mapram_point;
	mtd->_sync = mapram_nop;
	mtd->_unpoint = mapram_unpoint;
	mtd->flags = MTD_CAP_RAM;
	mtd->writesize = 1;

	mtd->erasesize = PAGE_SIZE;
 	while(mtd->size & (mtd->erasesize - 1))
		mtd->erasesize >>= 1;

	__module_get(THIS_MODULE);
	return mtd;
}

static int mapram_point(struct mtd_info *mtd, loff_t from, size_t len,
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

static int mapram_unpoint(struct mtd_info *mtd, loff_t from, size_t len)
{
	return 0;
}

static int mapram_read (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	struct map_info *map = mtd->priv;

	map_copy_from(map, buf, from, len);
	*retlen = len;
	return 0;
}

static int mapram_write (struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf)
{
	struct map_info *map = mtd->priv;

	map_copy_to(map, to, buf, len);
	*retlen = len;
	return 0;
}

static int mapram_erase (struct mtd_info *mtd, struct erase_info *instr)
{
	/* Yeah, it's inefficient. Who cares? It's faster than a _real_
	   flash erase. */
	struct map_info *map = mtd->priv;
	map_word allff;
	unsigned long i;

	allff = map_word_ff(map);
	for (i=0; i<instr->len; i += map_bankwidth(map))
		map_write(map, allff, instr->addr + i);
	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);
	return 0;
}

static void mapram_nop(struct mtd_info *mtd)
{
	/* Nothing to see here */
}

static int __init map_ram_init(void)
{
	register_mtd_chip_driver(&mapram_chipdrv);
	return 0;
}

static void __exit map_ram_exit(void)
{
	unregister_mtd_chip_driver(&mapram_chipdrv);
}

module_init(map_ram_init);
module_exit(map_ram_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("MTD chip driver for RAM chips");
