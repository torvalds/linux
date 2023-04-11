// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2022 Rafał Miłecki <rafal@milecki.pl>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/of.h>
#include <linux/slab.h>

#define TPLINK_SAFELOADER_DATA_OFFSET		4
#define TPLINK_SAFELOADER_MAX_PARTS		32

struct safeloader_cmn_header {
	__be32 size;
	uint32_t unused;
} __packed;

static void *mtd_parser_tplink_safeloader_read_table(struct mtd_info *mtd)
{
	struct safeloader_cmn_header hdr;
	struct device_node *np;
	size_t bytes_read;
	size_t size;
	u32 offset;
	char *buf;
	int err;

	np = mtd_get_of_node(mtd);
	if (mtd_is_partition(mtd))
		of_node_get(np);
	else
		np = of_get_child_by_name(np, "partitions");

	if (of_property_read_u32(np, "partitions-table-offset", &offset)) {
		pr_err("Failed to get partitions table offset\n");
		goto err_put;
	}

	err = mtd_read(mtd, offset, sizeof(hdr), &bytes_read, (uint8_t *)&hdr);
	if (err && !mtd_is_bitflip(err)) {
		pr_err("Failed to read from %s at 0x%x\n", mtd->name, offset);
		goto err_put;
	}

	size = be32_to_cpu(hdr.size);

	buf = kmalloc(size + 1, GFP_KERNEL);
	if (!buf)
		goto err_put;

	err = mtd_read(mtd, offset + sizeof(hdr), size, &bytes_read, buf);
	if (err && !mtd_is_bitflip(err)) {
		pr_err("Failed to read from %s at 0x%zx\n", mtd->name, offset + sizeof(hdr));
		goto err_kfree;
	}

	buf[size] = '\0';

	of_node_put(np);

	return buf;

err_kfree:
	kfree(buf);
err_put:
	of_node_put(np);
	return NULL;
}

static int mtd_parser_tplink_safeloader_parse(struct mtd_info *mtd,
					      const struct mtd_partition **pparts,
					      struct mtd_part_parser_data *data)
{
	struct mtd_partition *parts;
	char name[65];
	size_t offset;
	size_t bytes;
	char *buf;
	int idx;
	int err;

	parts = kcalloc(TPLINK_SAFELOADER_MAX_PARTS, sizeof(*parts), GFP_KERNEL);
	if (!parts) {
		err = -ENOMEM;
		goto err_out;
	}

	buf = mtd_parser_tplink_safeloader_read_table(mtd);
	if (!buf) {
		err = -ENOENT;
		goto err_free_parts;
	}

	for (idx = 0, offset = TPLINK_SAFELOADER_DATA_OFFSET;
	     idx < TPLINK_SAFELOADER_MAX_PARTS &&
	     sscanf(buf + offset, "partition %64s base 0x%llx size 0x%llx%zn\n",
		    name, &parts[idx].offset, &parts[idx].size, &bytes) == 3;
	     idx++, offset += bytes + 1) {
		parts[idx].name = kstrdup(name, GFP_KERNEL);
		if (!parts[idx].name) {
			err = -ENOMEM;
			goto err_free;
		}
	}

	if (idx == TPLINK_SAFELOADER_MAX_PARTS)
		pr_warn("Reached maximum number of partitions!\n");

	kfree(buf);

	*pparts = parts;

	return idx;

err_free:
	for (idx -= 1; idx >= 0; idx--)
		kfree(parts[idx].name);
err_free_parts:
	kfree(parts);
err_out:
	return err;
};

static void mtd_parser_tplink_safeloader_cleanup(const struct mtd_partition *pparts,
						 int nr_parts)
{
	int i;

	for (i = 0; i < nr_parts; i++)
		kfree(pparts[i].name);

	kfree(pparts);
}

static const struct of_device_id mtd_parser_tplink_safeloader_of_match_table[] = {
	{ .compatible = "tplink,safeloader-partitions" },
	{},
};
MODULE_DEVICE_TABLE(of, mtd_parser_tplink_safeloader_of_match_table);

static struct mtd_part_parser mtd_parser_tplink_safeloader = {
	.parse_fn = mtd_parser_tplink_safeloader_parse,
	.cleanup = mtd_parser_tplink_safeloader_cleanup,
	.name = "tplink-safeloader",
	.of_match_table = mtd_parser_tplink_safeloader_of_match_table,
};
module_mtd_part_parser(mtd_parser_tplink_safeloader);

MODULE_LICENSE("GPL");
