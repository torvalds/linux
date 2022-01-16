// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm SMEM NAND flash partition parser
 *
 * Copyright (C) 2020, Linaro Ltd.
 */

#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/slab.h>
#include <linux/soc/qcom/smem.h>

#define SMEM_AARM_PARTITION_TABLE	9
#define SMEM_APPS			0

#define SMEM_FLASH_PART_MAGIC1		0x55ee73aa
#define SMEM_FLASH_PART_MAGIC2		0xe35ebddb
#define SMEM_FLASH_PTABLE_V3		3
#define SMEM_FLASH_PTABLE_V4		4
#define SMEM_FLASH_PTABLE_MAX_PARTS_V3	16
#define SMEM_FLASH_PTABLE_MAX_PARTS_V4	48
#define SMEM_FLASH_PTABLE_HDR_LEN	(4 * sizeof(u32))
#define SMEM_FLASH_PTABLE_NAME_SIZE	16

/**
 * struct smem_flash_pentry - SMEM Flash partition entry
 * @name: Name of the partition
 * @offset: Offset in blocks
 * @length: Length of the partition in blocks
 * @attr: Flags for this partition
 */
struct smem_flash_pentry {
	char name[SMEM_FLASH_PTABLE_NAME_SIZE];
	__le32 offset;
	__le32 length;
	u8 attr;
} __packed __aligned(4);

/**
 * struct smem_flash_ptable - SMEM Flash partition table
 * @magic1: Partition table Magic 1
 * @magic2: Partition table Magic 2
 * @version: Partition table version
 * @numparts: Number of partitions in this ptable
 * @pentry: Flash partition entries belonging to this ptable
 */
struct smem_flash_ptable {
	__le32 magic1;
	__le32 magic2;
	__le32 version;
	__le32 numparts;
	struct smem_flash_pentry pentry[SMEM_FLASH_PTABLE_MAX_PARTS_V4];
} __packed __aligned(4);

static int parse_qcomsmem_part(struct mtd_info *mtd,
			       const struct mtd_partition **pparts,
			       struct mtd_part_parser_data *data)
{
	size_t len = SMEM_FLASH_PTABLE_HDR_LEN;
	int ret, i, j, tmpparts, numparts = 0;
	struct smem_flash_pentry *pentry;
	struct smem_flash_ptable *ptable;
	struct mtd_partition *parts;
	char *name, *c;

	if (IS_ENABLED(CONFIG_MTD_SPI_NOR_USE_4K_SECTORS)
			&& mtd->type == MTD_NORFLASH) {
		pr_err("%s: SMEM partition parser is incompatible with 4K sectors\n",
				mtd->name);
		return -EINVAL;
	}

	pr_debug("Parsing partition table info from SMEM\n");
	ptable = qcom_smem_get(SMEM_APPS, SMEM_AARM_PARTITION_TABLE, &len);
	if (IS_ERR(ptable)) {
		pr_err("Error reading partition table header\n");
		return PTR_ERR(ptable);
	}

	/* Verify ptable magic */
	if (le32_to_cpu(ptable->magic1) != SMEM_FLASH_PART_MAGIC1 ||
	    le32_to_cpu(ptable->magic2) != SMEM_FLASH_PART_MAGIC2) {
		pr_err("Partition table magic verification failed\n");
		return -EINVAL;
	}

	/* Ensure that # of partitions is less than the max we have allocated */
	tmpparts = le32_to_cpu(ptable->numparts);
	if (tmpparts > SMEM_FLASH_PTABLE_MAX_PARTS_V4) {
		pr_err("Partition numbers exceed the max limit\n");
		return -EINVAL;
	}

	/* Find out length of partition data based on table version */
	if (le32_to_cpu(ptable->version) <= SMEM_FLASH_PTABLE_V3) {
		len = SMEM_FLASH_PTABLE_HDR_LEN + SMEM_FLASH_PTABLE_MAX_PARTS_V3 *
			sizeof(struct smem_flash_pentry);
	} else if (le32_to_cpu(ptable->version) == SMEM_FLASH_PTABLE_V4) {
		len = SMEM_FLASH_PTABLE_HDR_LEN + SMEM_FLASH_PTABLE_MAX_PARTS_V4 *
			sizeof(struct smem_flash_pentry);
	} else {
		pr_err("Unknown ptable version (%d)", le32_to_cpu(ptable->version));
		return -EINVAL;
	}

	/*
	 * Now that the partition table header has been parsed, verified
	 * and the length of the partition table calculated, read the
	 * complete partition table
	 */
	ptable = qcom_smem_get(SMEM_APPS, SMEM_AARM_PARTITION_TABLE, &len);
	if (IS_ERR(ptable)) {
		pr_err("Error reading partition table\n");
		return PTR_ERR(ptable);
	}

	for (i = 0; i < tmpparts; i++) {
		pentry = &ptable->pentry[i];
		if (pentry->name[0] != '\0')
			numparts++;
	}

	parts = kcalloc(numparts, sizeof(*parts), GFP_KERNEL);
	if (!parts)
		return -ENOMEM;

	for (i = 0, j = 0; i < tmpparts; i++) {
		pentry = &ptable->pentry[i];
		if (pentry->name[0] == '\0')
			continue;

		name = kstrdup(pentry->name, GFP_KERNEL);
		if (!name) {
			ret = -ENOMEM;
			goto out_free_parts;
		}

		/* Convert name to lower case */
		for (c = name; *c != '\0'; c++)
			*c = tolower(*c);

		parts[j].name = name;
		parts[j].offset = le32_to_cpu(pentry->offset) * mtd->erasesize;
		parts[j].mask_flags = pentry->attr;
		parts[j].size = le32_to_cpu(pentry->length) * mtd->erasesize;
		pr_debug("%d: %s offs=0x%08x size=0x%08x attr:0x%08x\n",
			 i, pentry->name, le32_to_cpu(pentry->offset),
			 le32_to_cpu(pentry->length), pentry->attr);
		j++;
	}

	pr_debug("SMEM partition table found: ver: %d len: %d\n",
		 le32_to_cpu(ptable->version), tmpparts);
	*pparts = parts;

	return numparts;

out_free_parts:
	while (--j >= 0)
		kfree(parts[j].name);
	kfree(parts);
	*pparts = NULL;

	return ret;
}

static void parse_qcomsmem_cleanup(const struct mtd_partition *pparts,
				   int nr_parts)
{
	int i;

	for (i = 0; i < nr_parts; i++)
		kfree(pparts[i].name);
}

static const struct of_device_id qcomsmem_of_match_table[] = {
	{ .compatible = "qcom,smem-part" },
	{},
};
MODULE_DEVICE_TABLE(of, qcomsmem_of_match_table);

static struct mtd_part_parser mtd_parser_qcomsmem = {
	.parse_fn = parse_qcomsmem_part,
	.cleanup = parse_qcomsmem_cleanup,
	.name = "qcomsmem",
	.of_match_table = qcomsmem_of_match_table,
};
module_mtd_part_parser(mtd_parser_qcomsmem);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_DESCRIPTION("Qualcomm SMEM NAND flash partition parser");
