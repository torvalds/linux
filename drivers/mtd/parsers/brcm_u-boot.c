// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2022 Rafał Miłecki <rafal@milecki.pl>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#define BRCM_U_BOOT_MAX_OFFSET		0x200000
#define BRCM_U_BOOT_STEP		0x1000

#define BRCM_U_BOOT_MAX_PARTS		2

#define BRCM_U_BOOT_MAGIC		0x75456e76	/* uEnv */

struct brcm_u_boot_header {
	__le32 magic;
	__le32 length;
} __packed;

static const char *names[BRCM_U_BOOT_MAX_PARTS] = {
	"u-boot-env",
	"u-boot-env-backup",
};

static int brcm_u_boot_parse(struct mtd_info *mtd,
			     const struct mtd_partition **pparts,
			     struct mtd_part_parser_data *data)
{
	struct brcm_u_boot_header header;
	struct mtd_partition *parts;
	size_t bytes_read;
	size_t offset;
	int err;
	int i = 0;

	parts = kcalloc(BRCM_U_BOOT_MAX_PARTS, sizeof(*parts), GFP_KERNEL);
	if (!parts)
		return -ENOMEM;

	for (offset = 0;
	     offset < min_t(size_t, mtd->size, BRCM_U_BOOT_MAX_OFFSET);
	     offset += BRCM_U_BOOT_STEP) {
		err = mtd_read(mtd, offset, sizeof(header), &bytes_read, (uint8_t *)&header);
		if (err && !mtd_is_bitflip(err)) {
			pr_err("Failed to read from %s at 0x%zx: %d\n", mtd->name, offset, err);
			continue;
		}

		if (le32_to_cpu(header.magic) != BRCM_U_BOOT_MAGIC)
			continue;

		parts[i].name = names[i];
		parts[i].offset = offset;
		parts[i].size = sizeof(header) + le32_to_cpu(header.length);
		i++;
		pr_info("offset:0x%zx magic:0x%08x BINGO\n", offset, header.magic);

		if (i == BRCM_U_BOOT_MAX_PARTS)
			break;
	}

	*pparts = parts;

	return i;
};

static const struct of_device_id brcm_u_boot_of_match_table[] = {
	{ .compatible = "brcm,u-boot" },
	{},
};
MODULE_DEVICE_TABLE(of, brcm_u_boot_of_match_table);

static struct mtd_part_parser brcm_u_boot_mtd_parser = {
	.parse_fn = brcm_u_boot_parse,
	.name = "brcm_u-boot",
	.of_match_table = brcm_u_boot_of_match_table,
};
module_mtd_part_parser(brcm_u_boot_mtd_parser);

MODULE_LICENSE("GPL");
