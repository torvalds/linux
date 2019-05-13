/*
 * BCM63XX CFE image tag parser
 *
 * Copyright © 2006-2008  Florian Fainelli <florian@openwrt.org>
 *			  Mike Albon <malbon@openwrt.org>
 * Copyright © 2009-2010  Daniel Dickinson <openwrt@cshore.neomailbox.net>
 * Copyright © 2011-2013  Jonas Gorski <jonas.gorski@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bcm963xx_nvram.h>
#include <linux/bcm963xx_tag.h>
#include <linux/crc32.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/of.h>

#define BCM963XX_CFE_BLOCK_SIZE		SZ_64K	/* always at least 64KiB */

#define BCM963XX_CFE_MAGIC_OFFSET	0x4e0
#define BCM963XX_CFE_VERSION_OFFSET	0x570
#define BCM963XX_NVRAM_OFFSET		0x580

/* Ensure strings read from flash structs are null terminated */
#define STR_NULL_TERMINATE(x) \
	do { char *_str = (x); _str[sizeof(x) - 1] = 0; } while (0)

static int bcm63xx_detect_cfe(struct mtd_info *master)
{
	char buf[9];
	int ret;
	size_t retlen;

	ret = mtd_read(master, BCM963XX_CFE_VERSION_OFFSET, 5, &retlen,
		       (void *)buf);
	buf[retlen] = 0;

	if (ret)
		return ret;

	if (strncmp("cfe-v", buf, 5) == 0)
		return 0;

	/* very old CFE's do not have the cfe-v string, so check for magic */
	ret = mtd_read(master, BCM963XX_CFE_MAGIC_OFFSET, 8, &retlen,
		       (void *)buf);
	buf[retlen] = 0;

	return strncmp("CFE1CFE1", buf, 8);
}

static int bcm63xx_read_nvram(struct mtd_info *master,
	struct bcm963xx_nvram *nvram)
{
	u32 actual_crc, expected_crc;
	size_t retlen;
	int ret;

	/* extract nvram data */
	ret = mtd_read(master, BCM963XX_NVRAM_OFFSET, BCM963XX_NVRAM_V5_SIZE,
			&retlen, (void *)nvram);
	if (ret)
		return ret;

	ret = bcm963xx_nvram_checksum(nvram, &expected_crc, &actual_crc);
	if (ret)
		pr_warn("nvram checksum failed, contents may be invalid (expected %08x, got %08x)\n",
			expected_crc, actual_crc);

	if (!nvram->psi_size)
		nvram->psi_size = BCM963XX_DEFAULT_PSI_SIZE;

	return 0;
}

static const char * const bcm63xx_cfe_part_types[] = {
	"bcm963xx-imagetag",
	NULL,
};

static int bcm63xx_parse_cfe_nor_partitions(struct mtd_info *master,
	const struct mtd_partition **pparts, struct bcm963xx_nvram *nvram)
{
	struct mtd_partition *parts;
	int nrparts = 3, curpart = 0;
	unsigned int cfelen, nvramlen;
	unsigned int cfe_erasesize;
	int i;

	cfe_erasesize = max_t(uint32_t, master->erasesize,
			      BCM963XX_CFE_BLOCK_SIZE);

	cfelen = cfe_erasesize;
	nvramlen = nvram->psi_size * SZ_1K;
	nvramlen = roundup(nvramlen, cfe_erasesize);

	parts = kzalloc(sizeof(*parts) * nrparts + 10 * nrparts, GFP_KERNEL);
	if (!parts)
		return -ENOMEM;

	/* Start building partition list */
	parts[curpart].name = "CFE";
	parts[curpart].offset = 0;
	parts[curpart].size = cfelen;
	curpart++;

	parts[curpart].name = "nvram";
	parts[curpart].offset = master->size - nvramlen;
	parts[curpart].size = nvramlen;
	curpart++;

	/* Global partition "linux" to make easy firmware upgrade */
	parts[curpart].name = "linux";
	parts[curpart].offset = cfelen;
	parts[curpart].size = master->size - cfelen - nvramlen;
	parts[curpart].types = bcm63xx_cfe_part_types;

	for (i = 0; i < nrparts; i++)
		pr_info("Partition %d is %s offset %llx and length %llx\n", i,
			parts[i].name, parts[i].offset,	parts[i].size);

	*pparts = parts;

	return nrparts;
}

static int bcm63xx_parse_cfe_partitions(struct mtd_info *master,
					const struct mtd_partition **pparts,
					struct mtd_part_parser_data *data)
{
	struct bcm963xx_nvram *nvram = NULL;
	int ret;

	if (bcm63xx_detect_cfe(master))
		return -EINVAL;

	nvram = vzalloc(sizeof(*nvram));
	if (!nvram)
		return -ENOMEM;

	ret = bcm63xx_read_nvram(master, nvram);
	if (ret)
		goto out;

	if (!mtd_type_is_nand(master))
		ret = bcm63xx_parse_cfe_nor_partitions(master, pparts, nvram);
	else
		ret = -EINVAL;

out:
	vfree(nvram);
	return ret;
};

static const struct of_device_id parse_bcm63xx_cfe_match_table[] = {
	{ .compatible = "brcm,bcm963xx-cfe-nor-partitions" },
	{},
};
MODULE_DEVICE_TABLE(of, parse_bcm63xx_cfe_match_table);

static struct mtd_part_parser bcm63xx_cfe_parser = {
	.parse_fn = bcm63xx_parse_cfe_partitions,
	.name = "bcm63xxpart",
	.of_match_table = parse_bcm63xx_cfe_match_table,
};
module_mtd_part_parser(bcm63xx_cfe_parser);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Dickinson <openwrt@cshore.neomailbox.net>");
MODULE_AUTHOR("Florian Fainelli <florian@openwrt.org>");
MODULE_AUTHOR("Mike Albon <malbon@openwrt.org>");
MODULE_AUTHOR("Jonas Gorski <jonas.gorski@gmail.com");
MODULE_DESCRIPTION("MTD partitioning for BCM63XX CFE bootloaders");
