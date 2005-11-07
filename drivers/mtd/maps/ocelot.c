/*
 * $Id: ocelot.c,v 1.17 2005/11/07 11:14:27 gleixner Exp $
 *
 * Flash on Momenco Ocelot
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#define OCELOT_PLD 0x2c000000
#define FLASH_WINDOW_ADDR 0x2fc00000
#define FLASH_WINDOW_SIZE 0x00080000
#define FLASH_BUSWIDTH 1
#define NVRAM_WINDOW_ADDR 0x2c800000
#define NVRAM_WINDOW_SIZE 0x00007FF0
#define NVRAM_BUSWIDTH 1

static unsigned int cacheflush = 0;

static struct mtd_info *flash_mtd;
static struct mtd_info *nvram_mtd;

static void ocelot_ram_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf)
{
        struct map_info *map = mtd->priv;
	size_t done = 0;

	/* If we use memcpy, it does word-wide writes. Even though we told the
	   GT64120A that it's an 8-bit wide region, word-wide writes don't work.
	   We end up just writing the first byte of the four to all four bytes.
	   So we have this loop instead */
	*retlen = len;
	while(len) {
		__raw_writeb(*(unsigned char *) from, map->virt + to);
		from++;
		to++;
		len--;
	}
}

static struct mtd_partition *parsed_parts;

struct map_info ocelot_flash_map = {
	.name = "Ocelot boot flash",
	.size = FLASH_WINDOW_SIZE,
	.bankwidth = FLASH_BUSWIDTH,
	.phys = FLASH_WINDOW_ADDR,
};

struct map_info ocelot_nvram_map = {
	.name = "Ocelot NVRAM",
	.size = NVRAM_WINDOW_SIZE,
	.bankwidth = NVRAM_BUSWIDTH,
	.phys = NVRAM_WINDOW_ADDR,
};

static const char *probes[] = { "RedBoot", NULL };

static int __init init_ocelot_maps(void)
{
	void *pld;
	int nr_parts;
	unsigned char brd_status;

       	printk(KERN_INFO "Momenco Ocelot MTD mappings: Flash 0x%x at 0x%x, NVRAM 0x%x at 0x%x\n",
	       FLASH_WINDOW_SIZE, FLASH_WINDOW_ADDR, NVRAM_WINDOW_SIZE, NVRAM_WINDOW_ADDR);

	/* First check whether the flash jumper is present */
	pld = ioremap(OCELOT_PLD, 0x10);
	if (!pld) {
		printk(KERN_NOTICE "Failed to ioremap Ocelot PLD\n");
		return -EIO;
	}
	brd_status = readb(pld+4);
	iounmap(pld);

	/* Now ioremap the NVRAM space */
	ocelot_nvram_map.virt = ioremap_nocache(NVRAM_WINDOW_ADDR, NVRAM_WINDOW_SIZE);
	if (!ocelot_nvram_map.virt) {
		printk(KERN_NOTICE "Failed to ioremap Ocelot NVRAM space\n");
		return -EIO;
	}

	simple_map_init(&ocelot_nvram_map);

	/* And do the RAM probe on it to get an MTD device */
	nvram_mtd = do_map_probe("map_ram", &ocelot_nvram_map);
	if (!nvram_mtd) {
		printk("NVRAM probe failed\n");
		goto fail_1;
	}
	nvram_mtd->owner = THIS_MODULE;
	nvram_mtd->erasesize = 16;
	/* Override the write() method */
	nvram_mtd->write = ocelot_ram_write;

	/* Now map the flash space */
	ocelot_flash_map.virt = ioremap_nocache(FLASH_WINDOW_ADDR, FLASH_WINDOW_SIZE);
	if (!ocelot_flash_map.virt) {
		printk(KERN_NOTICE "Failed to ioremap Ocelot flash space\n");
		goto fail_2;
	}
	/* Now the cached version */
	ocelot_flash_map.cached = (unsigned long)__ioremap(FLASH_WINDOW_ADDR, FLASH_WINDOW_SIZE, 0);

	simple_map_init(&ocelot_flash_map);

	/* Only probe for flash if the write jumper is present */
	if (brd_status & 0x40) {
		flash_mtd = do_map_probe("jedec", &ocelot_flash_map);
	} else {
		printk(KERN_NOTICE "Ocelot flash write jumper not present. Treating as ROM\n");
	}
	/* If that failed or the jumper's absent, pretend it's ROM */
	if (!flash_mtd) {
		flash_mtd = do_map_probe("map_rom", &ocelot_flash_map);
		/* If we're treating it as ROM, set the erase size */
		if (flash_mtd)
			flash_mtd->erasesize = 0x10000;
	}
	if (!flash_mtd)
		goto fail3;

	add_mtd_device(nvram_mtd);

	flash_mtd->owner = THIS_MODULE;
	nr_parts = parse_mtd_partitions(flash_mtd, probes, &parsed_parts, 0);

	if (nr_parts > 0)
		add_mtd_partitions(flash_mtd, parsed_parts, nr_parts);
	else
		add_mtd_device(flash_mtd);

	return 0;

 fail3:
	iounmap((void *)ocelot_flash_map.virt);
	if (ocelot_flash_map.cached)
			iounmap((void *)ocelot_flash_map.cached);
 fail_2:
	map_destroy(nvram_mtd);
 fail_1:
	iounmap((void *)ocelot_nvram_map.virt);

	return -ENXIO;
}

static void __exit cleanup_ocelot_maps(void)
{
	del_mtd_device(nvram_mtd);
	map_destroy(nvram_mtd);
	iounmap((void *)ocelot_nvram_map.virt);

	if (parsed_parts)
		del_mtd_partitions(flash_mtd);
	else
		del_mtd_device(flash_mtd);
	map_destroy(flash_mtd);
	iounmap((void *)ocelot_flash_map.virt);
	if (ocelot_flash_map.cached)
		iounmap((void *)ocelot_flash_map.cached);
}

module_init(init_ocelot_maps);
module_exit(cleanup_ocelot_maps);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Red Hat, Inc. - David Woodhouse <dwmw2@cambridge.redhat.com>");
MODULE_DESCRIPTION("MTD map driver for Momenco Ocelot board");
