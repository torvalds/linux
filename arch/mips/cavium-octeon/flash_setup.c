/*
 *   Octeon Bootbus flash setup
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007, 2008 Cavium Networks
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/semaphore.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/mtd/partitions.h>

#include <asm/octeon/octeon.h>

static struct map_info flash_map;
static struct mtd_info *mymtd;
static const char *part_probe_types[] = {
	"cmdlinepart",
#ifdef CONFIG_MTD_REDBOOT_PARTS
	"RedBoot",
#endif
	NULL
};

static map_word octeon_flash_map_read(struct map_info *map, unsigned long ofs)
{
	map_word r;

	down(&octeon_bootbus_sem);
	r = inline_map_read(map, ofs);
	up(&octeon_bootbus_sem);

	return r;
}

static void octeon_flash_map_write(struct map_info *map, const map_word datum,
				   unsigned long ofs)
{
	down(&octeon_bootbus_sem);
	inline_map_write(map, datum, ofs);
	up(&octeon_bootbus_sem);
}

static void octeon_flash_map_copy_from(struct map_info *map, void *to,
				       unsigned long from, ssize_t len)
{
	down(&octeon_bootbus_sem);
	inline_map_copy_from(map, to, from, len);
	up(&octeon_bootbus_sem);
}

static void octeon_flash_map_copy_to(struct map_info *map, unsigned long to,
				     const void *from, ssize_t len)
{
	down(&octeon_bootbus_sem);
	inline_map_copy_to(map, to, from, len);
	up(&octeon_bootbus_sem);
}

/*
 * Module/ driver initialization.
 *
 * Returns Zero on success
 */
static int octeon_flash_probe(struct platform_device *pdev)
{
	union cvmx_mio_boot_reg_cfgx region_cfg;
	u32 cs;
	int r;
	struct device_node *np = pdev->dev.of_node;

	r = of_property_read_u32(np, "reg", &cs);
	if (r)
		return r;

	/*
	 * Read the bootbus region 0 setup to determine the base
	 * address of the flash.
	 */
	region_cfg.u64 = cvmx_read_csr(CVMX_MIO_BOOT_REG_CFGX(cs));
	if (region_cfg.s.en) {
		/*
		 * The bootloader always takes the flash and sets its
		 * address so the entire flash fits below
		 * 0x1fc00000. This way the flash aliases to
		 * 0x1fc00000 for booting. Software can access the
		 * full flash at the true address, while core boot can
		 * access 4MB.
		 */
		/* Use this name so old part lines work */
		flash_map.name = "phys_mapped_flash";
		flash_map.phys = region_cfg.s.base << 16;
		flash_map.size = 0x1fc00000 - flash_map.phys;
		/* 8-bit bus (0 + 1) or 16-bit bus (1 + 1) */
		flash_map.bankwidth = region_cfg.s.width + 1;
		flash_map.virt = ioremap(flash_map.phys, flash_map.size);
		pr_notice("Bootbus flash: Setting flash for %luMB flash at "
			  "0x%08llx\n", flash_map.size >> 20, flash_map.phys);
		WARN_ON(!map_bankwidth_supported(flash_map.bankwidth));
		flash_map.read = octeon_flash_map_read;
		flash_map.write = octeon_flash_map_write;
		flash_map.copy_from = octeon_flash_map_copy_from;
		flash_map.copy_to = octeon_flash_map_copy_to;
		mymtd = do_map_probe("cfi_probe", &flash_map);
		if (mymtd) {
			mymtd->owner = THIS_MODULE;
			mtd_device_parse_register(mymtd, part_probe_types,
						  NULL, NULL, 0);
		} else {
			pr_err("Failed to register MTD device for flash\n");
		}
	}
	return 0;
}

static const struct of_device_id of_flash_match[] = {
	{
		.compatible	= "cfi-flash",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, of_flash_match);

static struct platform_driver of_flash_driver = {
	.driver = {
		.name = "octeon-of-flash",
		.of_match_table = of_flash_match,
	},
	.probe		= octeon_flash_probe,
};

static int octeon_flash_init(void)
{
	return platform_driver_register(&of_flash_driver);
}
late_initcall(octeon_flash_init);

MODULE_LICENSE("GPL");
