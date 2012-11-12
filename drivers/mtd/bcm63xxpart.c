/*
 * BCM63XX CFE image tag parser
 *
 * Copyright © 2006-2008  Florian Fainelli <florian@openwrt.org>
 *			  Mike Albon <malbon@openwrt.org>
 * Copyright © 2009-2010  Daniel Dickinson <openwrt@cshore.neomailbox.net>
 * Copyright © 2011-2012  Jonas Gorski <jonas.gorski@gmail.com>
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

#include <linux/crc32.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <asm/mach-bcm63xx/bcm963xx_tag.h>
#include <asm/mach-bcm63xx/board_bcm963xx.h>

#define BCM63XX_EXTENDED_SIZE	0xBFC00000	/* Extended flash address */

#define BCM63XX_MIN_CFE_SIZE	0x10000		/* always at least 64KiB */
#define BCM63XX_MIN_NVRAM_SIZE	0x10000		/* always at least 64KiB */

#define BCM63XX_CFE_MAGIC_OFFSET 0x4e0

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
	ret = mtd_read(master, BCM63XX_CFE_MAGIC_OFFSET, 8, &retlen,
		       (void *)buf);
	buf[retlen] = 0;

	return strncmp("CFE1CFE1", buf, 8);
}

static int bcm63xx_parse_cfe_partitions(struct mtd_info *master,
					struct mtd_partition **pparts,
					struct mtd_part_parser_data *data)
{
	/* CFE, NVRAM and global Linux are always present */
	int nrparts = 3, curpart = 0;
	struct bcm_tag *buf;
	struct mtd_partition *parts;
	int ret;
	size_t retlen;
	unsigned int rootfsaddr, kerneladdr, spareaddr;
	unsigned int rootfslen, kernellen, sparelen, totallen;
	unsigned int cfelen, nvramlen;
	int i;
	u32 computed_crc;
	bool rootfs_first = false;

	if (bcm63xx_detect_cfe(master))
		return -EINVAL;

	cfelen = max_t(uint32_t, master->erasesize, BCM63XX_MIN_CFE_SIZE);
	nvramlen = max_t(uint32_t, master->erasesize, BCM63XX_MIN_NVRAM_SIZE);

	/* Allocate memory for buffer */
	buf = vmalloc(sizeof(struct bcm_tag));
	if (!buf)
		return -ENOMEM;

	/* Get the tag */
	ret = mtd_read(master, cfelen, sizeof(struct bcm_tag), &retlen,
		       (void *)buf);

	if (retlen != sizeof(struct bcm_tag)) {
		vfree(buf);
		return -EIO;
	}

	computed_crc = crc32_le(IMAGETAG_CRC_START, (u8 *)buf,
				offsetof(struct bcm_tag, header_crc));
	if (computed_crc == buf->header_crc) {
		char *boardid = &(buf->board_id[0]);
		char *tagversion = &(buf->tag_version[0]);

		sscanf(buf->flash_image_start, "%u", &rootfsaddr);
		sscanf(buf->kernel_address, "%u", &kerneladdr);
		sscanf(buf->kernel_length, "%u", &kernellen);
		sscanf(buf->total_length, "%u", &totallen);

		pr_info("CFE boot tag found with version %s and board type %s\n",
			tagversion, boardid);

		kerneladdr = kerneladdr - BCM63XX_EXTENDED_SIZE;
		rootfsaddr = rootfsaddr - BCM63XX_EXTENDED_SIZE;
		spareaddr = roundup(totallen, master->erasesize) + cfelen;
		sparelen = master->size - spareaddr - nvramlen;

		if (rootfsaddr < kerneladdr) {
			/* default Broadcom layout */
			rootfslen = kerneladdr - rootfsaddr;
			rootfs_first = true;
		} else {
			/* OpenWrt layout */
			rootfsaddr = kerneladdr + kernellen;
			rootfslen = spareaddr - rootfsaddr;
		}
	} else {
		pr_warn("CFE boot tag CRC invalid (expected %08x, actual %08x)\n",
			buf->header_crc, computed_crc);
		kernellen = 0;
		rootfslen = 0;
		rootfsaddr = 0;
		spareaddr = cfelen;
		sparelen = master->size - cfelen - nvramlen;
	}

	/* Determine number of partitions */
	if (rootfslen > 0)
		nrparts++;

	if (kernellen > 0)
		nrparts++;

	/* Ask kernel for more memory */
	parts = kzalloc(sizeof(*parts) * nrparts + 10 * nrparts, GFP_KERNEL);
	if (!parts) {
		vfree(buf);
		return -ENOMEM;
	}

	/* Start building partition list */
	parts[curpart].name = "CFE";
	parts[curpart].offset = 0;
	parts[curpart].size = cfelen;
	curpart++;

	if (kernellen > 0) {
		int kernelpart = curpart;

		if (rootfslen > 0 && rootfs_first)
			kernelpart++;
		parts[kernelpart].name = "kernel";
		parts[kernelpart].offset = kerneladdr;
		parts[kernelpart].size = kernellen;
		curpart++;
	}

	if (rootfslen > 0) {
		int rootfspart = curpart;

		if (kernellen > 0 && rootfs_first)
			rootfspart--;
		parts[rootfspart].name = "rootfs";
		parts[rootfspart].offset = rootfsaddr;
		parts[rootfspart].size = rootfslen;
		if (sparelen > 0  && !rootfs_first)
			parts[rootfspart].size += sparelen;
		curpart++;
	}

	parts[curpart].name = "nvram";
	parts[curpart].offset = master->size - nvramlen;
	parts[curpart].size = nvramlen;

	/* Global partition "linux" to make easy firmware upgrade */
	curpart++;
	parts[curpart].name = "linux";
	parts[curpart].offset = cfelen;
	parts[curpart].size = master->size - cfelen - nvramlen;

	for (i = 0; i < nrparts; i++)
		pr_info("Partition %d is %s offset %lx and length %lx\n", i,
			parts[i].name, (long unsigned int)(parts[i].offset),
			(long unsigned int)(parts[i].size));

	pr_info("Spare partition is offset %x and length %x\n",	spareaddr,
		sparelen);

	*pparts = parts;
	vfree(buf);

	return nrparts;
};

static struct mtd_part_parser bcm63xx_cfe_parser = {
	.owner = THIS_MODULE,
	.parse_fn = bcm63xx_parse_cfe_partitions,
	.name = "bcm63xxpart",
};

static int __init bcm63xx_cfe_parser_init(void)
{
	return register_mtd_parser(&bcm63xx_cfe_parser);
}

static void __exit bcm63xx_cfe_parser_exit(void)
{
	deregister_mtd_parser(&bcm63xx_cfe_parser);
}

module_init(bcm63xx_cfe_parser_init);
module_exit(bcm63xx_cfe_parser_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Dickinson <openwrt@cshore.neomailbox.net>");
MODULE_AUTHOR("Florian Fainelli <florian@openwrt.org>");
MODULE_AUTHOR("Mike Albon <malbon@openwrt.org>");
MODULE_AUTHOR("Jonas Gorski <jonas.gorski@gmail.com");
MODULE_DESCRIPTION("MTD partitioning for BCM63XX CFE bootloaders");
