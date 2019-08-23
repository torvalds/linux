// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BCM63XX CFE image tag parser
 *
 * Copyright © 2006-2008  Florian Fainelli <florian@openwrt.org>
 *			  Mike Albon <malbon@openwrt.org>
 * Copyright © 2009-2010  Daniel Dickinson <openwrt@cshore.neomailbox.net>
 * Copyright © 2011-2013  Jonas Gorski <jonas.gorski@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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

/* Ensure strings read from flash structs are null terminated */
#define STR_NULL_TERMINATE(x) \
	do { char *_str = (x); _str[sizeof(x) - 1] = 0; } while (0)

static int bcm963xx_read_imagetag(struct mtd_info *master, const char *name,
	loff_t tag_offset, struct bcm_tag *buf)
{
	int ret;
	size_t retlen;
	u32 computed_crc;

	ret = mtd_read(master, tag_offset, sizeof(*buf), &retlen, (void *)buf);
	if (ret)
		return ret;

	if (retlen != sizeof(*buf))
		return -EIO;

	computed_crc = crc32_le(IMAGETAG_CRC_START, (u8 *)buf,
				offsetof(struct bcm_tag, header_crc));
	if (computed_crc == buf->header_crc) {
		STR_NULL_TERMINATE(buf->board_id);
		STR_NULL_TERMINATE(buf->tag_version);

		pr_info("%s: CFE image tag found at 0x%llx with version %s, board type %s\n",
			name, tag_offset, buf->tag_version, buf->board_id);

		return 0;
	}

	pr_warn("%s: CFE image tag at 0x%llx CRC invalid (expected %08x, actual %08x)\n",
		name, tag_offset, buf->header_crc, computed_crc);
	return -EINVAL;
}

static int bcm963xx_parse_imagetag_partitions(struct mtd_info *master,
					const struct mtd_partition **pparts,
					struct mtd_part_parser_data *data)
{
	/* CFE, NVRAM and global Linux are always present */
	int nrparts = 0, curpart = 0;
	struct bcm_tag *buf = NULL;
	struct mtd_partition *parts;
	int ret;
	unsigned int rootfsaddr, kerneladdr, spareaddr, offset;
	unsigned int rootfslen, kernellen, sparelen, totallen;
	int i;
	bool rootfs_first = false;

	buf = vmalloc(sizeof(struct bcm_tag));
	if (!buf)
		return -ENOMEM;

	/* Get the tag */
	ret = bcm963xx_read_imagetag(master, "rootfs", 0, buf);
	if (!ret) {
		STR_NULL_TERMINATE(buf->flash_image_start);
		if (kstrtouint(buf->flash_image_start, 10, &rootfsaddr) ||
				rootfsaddr < BCM963XX_EXTENDED_SIZE) {
			pr_err("invalid rootfs address: %*ph\n",
				(int)sizeof(buf->flash_image_start),
				buf->flash_image_start);
			goto out;
		}

		STR_NULL_TERMINATE(buf->kernel_address);
		if (kstrtouint(buf->kernel_address, 10, &kerneladdr) ||
				kerneladdr < BCM963XX_EXTENDED_SIZE) {
			pr_err("invalid kernel address: %*ph\n",
				(int)sizeof(buf->kernel_address),
				buf->kernel_address);
			goto out;
		}

		STR_NULL_TERMINATE(buf->kernel_length);
		if (kstrtouint(buf->kernel_length, 10, &kernellen)) {
			pr_err("invalid kernel length: %*ph\n",
				(int)sizeof(buf->kernel_length),
				buf->kernel_length);
			goto out;
		}

		STR_NULL_TERMINATE(buf->total_length);
		if (kstrtouint(buf->total_length, 10, &totallen)) {
			pr_err("invalid total length: %*ph\n",
				(int)sizeof(buf->total_length),
				buf->total_length);
			goto out;
		}

		/*
		 * Addresses are flash absolute, so convert to partition
		 * relative addresses. Assume either kernel or rootfs will
		 * directly follow the image tag.
		 */
		if (rootfsaddr < kerneladdr)
			offset = rootfsaddr - sizeof(struct bcm_tag);
		else
			offset = kerneladdr - sizeof(struct bcm_tag);

		kerneladdr = kerneladdr - offset;
		rootfsaddr = rootfsaddr - offset;
		spareaddr = roundup(totallen, master->erasesize);

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
		goto out;
	}
	sparelen = master->size - spareaddr;

	/* Determine number of partitions */
	if (rootfslen > 0)
		nrparts++;

	if (kernellen > 0)
		nrparts++;

	parts = kzalloc(sizeof(*parts) * nrparts + 10 * nrparts, GFP_KERNEL);
	if (!parts) {
		ret = -ENOMEM;
		goto out;
	}

	/* Start building partition list */
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

	for (i = 0; i < nrparts; i++)
		pr_info("Partition %d is %s offset %llx and length %llx\n", i,
			parts[i].name, parts[i].offset,	parts[i].size);

	pr_info("Spare partition is offset %x and length %x\n",	spareaddr,
		sparelen);

	*pparts = parts;
	ret = 0;

out:
	vfree(buf);

	if (ret)
		return ret;

	return nrparts;
}

static const struct of_device_id parse_bcm963xx_imagetag_match_table[] = {
	{ .compatible = "brcm,bcm963xx-imagetag" },
	{},
};
MODULE_DEVICE_TABLE(of, parse_bcm963xx_imagetag_match_table);

static struct mtd_part_parser bcm963xx_imagetag_parser = {
	.parse_fn = bcm963xx_parse_imagetag_partitions,
	.name = "bcm963xx-imagetag",
	.of_match_table = parse_bcm963xx_imagetag_match_table,
};
module_mtd_part_parser(bcm963xx_imagetag_parser);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Dickinson <openwrt@cshore.neomailbox.net>");
MODULE_AUTHOR("Florian Fainelli <florian@openwrt.org>");
MODULE_AUTHOR("Mike Albon <malbon@openwrt.org>");
MODULE_AUTHOR("Jonas Gorski <jonas.gorski@gmail.com>");
MODULE_DESCRIPTION("MTD parser for BCM963XX CFE Image Tag partitions");
