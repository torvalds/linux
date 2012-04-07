/*
 * Map for flash chips on Wind River PowerQUICC II SBC82xx board.
 *
 * Copyright (C) 2004 Red Hat, Inc.
 *
 * Author: David Woodhouse <dwmw2@infradead.org>
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/immap_cpm2.h>

static struct mtd_info *sbcmtd[3];

struct map_info sbc82xx_flash_map[3] = {
	{.name = "Boot flash"},
	{.name = "Alternate boot flash"},
	{.name = "User flash"}
};

static struct mtd_partition smallflash_parts[] = {
	{
		.name =		"space",
		.size =		0x100000,
		.offset =	0,
	}, {
		.name =		"bootloader",
		.size =		MTDPART_SIZ_FULL,
		.offset =	MTDPART_OFS_APPEND,
	}
};

static struct mtd_partition bigflash_parts[] = {
	{
		.name =		"bootloader",
		.size =		0x00100000,
		.offset =	0,
	}, {
		.name =		"file system",
		.size =		0x01f00000,
		.offset =	MTDPART_OFS_APPEND,
	}, {
		.name =		"boot config",
		.size =		0x00100000,
		.offset =	MTDPART_OFS_APPEND,
	}, {
		.name =		"space",
		.size =		0x01f00000,
		.offset =	MTDPART_OFS_APPEND,
	}
};

static const char *part_probes[] __initdata = {"cmdlinepart", "RedBoot", NULL};

#define init_sbc82xx_one_flash(map, br, or)			\
do {								\
	(map).phys = (br & 1) ? (br & 0xffff8000) : 0;		\
	(map).size = (br & 1) ? (~(or & 0xffff8000) + 1) : 0;	\
	switch (br & 0x00001800) {				\
	case 0x00000000:					\
	case 0x00000800:	(map).bankwidth = 1;	break;	\
	case 0x00001000:	(map).bankwidth = 2;	break;	\
	case 0x00001800:	(map).bankwidth = 4;	break;	\
	}							\
} while (0);

static int __init init_sbc82xx_flash(void)
{
	volatile memctl_cpm2_t *mc = &cpm2_immr->im_memctl;
	int bigflash;
	int i;

#ifdef CONFIG_SBC8560
	mc = ioremap(0xff700000 + 0x5000, sizeof(memctl_cpm2_t));
#else
	mc = &cpm2_immr->im_memctl;
#endif

	bigflash = 1;
	if ((mc->memc_br0 & 0x00001800) == 0x00001800)
		bigflash = 0;

	init_sbc82xx_one_flash(sbc82xx_flash_map[0], mc->memc_br0, mc->memc_or0);
	init_sbc82xx_one_flash(sbc82xx_flash_map[1], mc->memc_br6, mc->memc_or6);
	init_sbc82xx_one_flash(sbc82xx_flash_map[2], mc->memc_br1, mc->memc_or1);

#ifdef CONFIG_SBC8560
	iounmap((void *) mc);
#endif

	for (i=0; i<3; i++) {
		int8_t flashcs[3] = { 0, 6, 1 };
		int nr_parts;
		struct mtd_partition *defparts;

		printk(KERN_NOTICE "PowerQUICC II %s (%ld MiB on CS%d",
		       sbc82xx_flash_map[i].name,
		       (sbc82xx_flash_map[i].size >> 20),
		       flashcs[i]);
		if (!sbc82xx_flash_map[i].phys) {
			/* We know it can't be at zero. */
			printk("): disabled by bootloader.\n");
			continue;
		}
		printk(" at %08lx)\n",  sbc82xx_flash_map[i].phys);

		sbc82xx_flash_map[i].virt = ioremap(sbc82xx_flash_map[i].phys,
						    sbc82xx_flash_map[i].size);

		if (!sbc82xx_flash_map[i].virt) {
			printk("Failed to ioremap\n");
			continue;
		}

		simple_map_init(&sbc82xx_flash_map[i]);

		sbcmtd[i] = do_map_probe("cfi_probe", &sbc82xx_flash_map[i]);

		if (!sbcmtd[i])
			continue;

		sbcmtd[i]->owner = THIS_MODULE;

		/* No partitioning detected. Use default */
		if (i == 2) {
			defparts = NULL;
			nr_parts = 0;
		} else if (i == bigflash) {
			defparts = bigflash_parts;
			nr_parts = ARRAY_SIZE(bigflash_parts);
		} else {
			defparts = smallflash_parts;
			nr_parts = ARRAY_SIZE(smallflash_parts);
		}

		mtd_device_parse_register(sbcmtd[i], part_probes, NULL,
					  defparts, nr_parts);
	}
	return 0;
}

static void __exit cleanup_sbc82xx_flash(void)
{
	int i;

	for (i=0; i<3; i++) {
		if (!sbcmtd[i])
			continue;

		mtd_device_unregister(sbcmtd[i]);

		map_destroy(sbcmtd[i]);

		iounmap((void *)sbc82xx_flash_map[i].virt);
		sbc82xx_flash_map[i].virt = 0;
	}
}

module_init(init_sbc82xx_flash);
module_exit(cleanup_sbc82xx_flash);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("Flash map driver for WindRiver PowerQUICC II");
