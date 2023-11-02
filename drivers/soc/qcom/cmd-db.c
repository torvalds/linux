/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2016-2018, 2020, The Linux Foundation. All rights reserved. */

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/types.h>

#include <soc/qcom/cmd-db.h>

#define NUM_PRIORITY		2
#define MAX_SLV_ID		8
#define SLAVE_ID_MASK		0x7
#define SLAVE_ID_SHIFT		16

/**
 * struct entry_header: header for each entry in cmddb
 *
 * @id: resource's identifier
 * @priority: unused
 * @addr: the address of the resource
 * @len: length of the data
 * @offset: offset from :@data_offset, start of the data
 */
struct entry_header {
	u8 id[8];
	__le32 priority[NUM_PRIORITY];
	__le32 addr;
	__le16 len;
	__le16 offset;
};

/**
 * struct rsc_hdr: resource header information
 *
 * @slv_id: id for the resource
 * @header_offset: entry's header at offset from the end of the cmd_db_header
 * @data_offset: entry's data at offset from the end of the cmd_db_header
 * @cnt: number of entries for HW type
 * @version: MSB is major, LSB is minor
 * @reserved: reserved for future use.
 */
struct rsc_hdr {
	__le16 slv_id;
	__le16 header_offset;
	__le16 data_offset;
	__le16 cnt;
	__le16 version;
	__le16 reserved[3];
};

/**
 * struct cmd_db_header: The DB header information
 *
 * @version: The cmd db version
 * @magic: constant expected in the database
 * @header: array of resources
 * @checksum: checksum for the header. Unused.
 * @reserved: reserved memory
 * @data: driver specific data
 */
struct cmd_db_header {
	__le32 version;
	u8 magic[4];
	struct rsc_hdr header[MAX_SLV_ID];
	__le32 checksum;
	__le32 reserved;
	u8 data[];
};

/**
 * DOC: Description of the Command DB database.
 *
 * At the start of the command DB memory is the cmd_db_header structure.
 * The cmd_db_header holds the version, checksum, magic key as well as an
 * array for header for each slave (depicted by the rsc_header). Each h/w
 * based accelerator is a 'slave' (shared resource) and has slave id indicating
 * the type of accelerator. The rsc_header is the header for such individual
 * slaves of a given type. The entries for each of these slaves begin at the
 * rsc_hdr.header_offset. In addition each slave could have auxiliary data
 * that may be needed by the driver. The data for the slave starts at the
 * entry_header.offset to the location pointed to by the rsc_hdr.data_offset.
 *
 * Drivers have a stringified key to a slave/resource. They can query the slave
 * information and get the slave id and the auxiliary data and the length of the
 * data. Using this information, they can format the request to be sent to the
 * h/w accelerator and request a resource state.
 */

static const u8 CMD_DB_MAGIC[] = { 0xdb, 0x30, 0x03, 0x0c };

static bool cmd_db_magic_matches(const struct cmd_db_header *header)
{
	const u8 *magic = header->magic;

	return memcmp(magic, CMD_DB_MAGIC, ARRAY_SIZE(CMD_DB_MAGIC)) == 0;
}

static struct cmd_db_header *cmd_db_header;

static inline const void *rsc_to_entry_header(const struct rsc_hdr *hdr)
{
	u16 offset = le16_to_cpu(hdr->header_offset);

	return cmd_db_header->data + offset;
}

static inline void *
rsc_offset(const struct rsc_hdr *hdr, const struct entry_header *ent)
{
	u16 offset = le16_to_cpu(hdr->data_offset);
	u16 loffset = le16_to_cpu(ent->offset);

	return cmd_db_header->data + offset + loffset;
}

/**
 * cmd_db_ready - Indicates if command DB is available
 *
 * Return: 0 on success, errno otherwise
 */
int cmd_db_ready(void)
{
	if (cmd_db_header == NULL)
		return -EPROBE_DEFER;
	else if (!cmd_db_magic_matches(cmd_db_header))
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(cmd_db_ready);

static int cmd_db_get_header(const char *id, const struct entry_header **eh,
			     const struct rsc_hdr **rh)
{
	const struct rsc_hdr *rsc_hdr;
	const struct entry_header *ent;
	int ret, i, j;
	u8 query[sizeof(ent->id)] __nonstring;

	ret = cmd_db_ready();
	if (ret)
		return ret;

	/*
	 * Pad out query string to same length as in DB. NOTE: the output
	 * query string is not necessarily '\0' terminated if it bumps up
	 * against the max size. That's OK and expected.
	 */
	strncpy(query, id, sizeof(query));

	for (i = 0; i < MAX_SLV_ID; i++) {
		rsc_hdr = &cmd_db_header->header[i];
		if (!rsc_hdr->slv_id)
			break;

		ent = rsc_to_entry_header(rsc_hdr);
		for (j = 0; j < le16_to_cpu(rsc_hdr->cnt); j++, ent++) {
			if (memcmp(ent->id, query, sizeof(ent->id)) == 0) {
				if (eh)
					*eh = ent;
				if (rh)
					*rh = rsc_hdr;
				return 0;
			}
		}
	}

	return -ENODEV;
}

/**
 * cmd_db_read_addr() - Query command db for resource id address.
 *
 * @id: resource id to query for address
 *
 * Return: resource address on success, 0 on error
 *
 * This is used to retrieve resource address based on resource
 * id.
 */
u32 cmd_db_read_addr(const char *id)
{
	int ret;
	const struct entry_header *ent;

	ret = cmd_db_get_header(id, &ent, NULL);

	return ret < 0 ? 0 : le32_to_cpu(ent->addr);
}
EXPORT_SYMBOL_GPL(cmd_db_read_addr);

/**
 * cmd_db_read_aux_data() - Query command db for aux data.
 *
 *  @id: Resource to retrieve AUX Data on
 *  @len: size of data buffer returned
 *
 *  Return: pointer to data on success, error pointer otherwise
 */
const void *cmd_db_read_aux_data(const char *id, size_t *len)
{
	int ret;
	const struct entry_header *ent;
	const struct rsc_hdr *rsc_hdr;

	ret = cmd_db_get_header(id, &ent, &rsc_hdr);
	if (ret)
		return ERR_PTR(ret);

	if (len)
		*len = le16_to_cpu(ent->len);

	return rsc_offset(rsc_hdr, ent);
}
EXPORT_SYMBOL_GPL(cmd_db_read_aux_data);

/**
 * cmd_db_read_slave_id - Get the slave ID for a given resource address
 *
 * @id: Resource id to query the DB for version
 *
 * Return: cmd_db_hw_type enum on success, CMD_DB_HW_INVALID on error
 */
enum cmd_db_hw_type cmd_db_read_slave_id(const char *id)
{
	int ret;
	const struct entry_header *ent;
	u32 addr;

	ret = cmd_db_get_header(id, &ent, NULL);
	if (ret < 0)
		return CMD_DB_HW_INVALID;

	addr = le32_to_cpu(ent->addr);
	return (addr >> SLAVE_ID_SHIFT) & SLAVE_ID_MASK;
}
EXPORT_SYMBOL_GPL(cmd_db_read_slave_id);

#ifdef CONFIG_DEBUG_FS
static int cmd_db_debugfs_dump(struct seq_file *seq, void *p)
{
	int i, j;
	const struct rsc_hdr *rsc;
	const struct entry_header *ent;
	const char *name;
	u16 len, version;
	u8 major, minor;

	seq_puts(seq, "Command DB DUMP\n");

	for (i = 0; i < MAX_SLV_ID; i++) {
		rsc = &cmd_db_header->header[i];
		if (!rsc->slv_id)
			break;

		switch (le16_to_cpu(rsc->slv_id)) {
		case CMD_DB_HW_ARC:
			name = "ARC";
			break;
		case CMD_DB_HW_VRM:
			name = "VRM";
			break;
		case CMD_DB_HW_BCM:
			name = "BCM";
			break;
		default:
			name = "Unknown";
			break;
		}

		version = le16_to_cpu(rsc->version);
		major = version >> 8;
		minor = version;

		seq_printf(seq, "Slave %s (v%u.%u)\n", name, major, minor);
		seq_puts(seq, "-------------------------\n");

		ent = rsc_to_entry_header(rsc);
		for (j = 0; j < le16_to_cpu(rsc->cnt); j++, ent++) {
			seq_printf(seq, "0x%05x: %*pEp", le32_to_cpu(ent->addr),
				   (int)strnlen(ent->id, sizeof(ent->id)), ent->id);

			len = le16_to_cpu(ent->len);
			if (len) {
				seq_printf(seq, " [%*ph]",
					   len, rsc_offset(rsc, ent));
			}
			seq_putc(seq, '\n');
		}
	}

	return 0;
}

static int open_cmd_db_debugfs(struct inode *inode, struct file *file)
{
	return single_open(file, cmd_db_debugfs_dump, inode->i_private);
}
#endif

static const struct file_operations cmd_db_debugfs_ops = {
#ifdef CONFIG_DEBUG_FS
	.open = open_cmd_db_debugfs,
#endif
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int cmd_db_dev_probe(struct platform_device *pdev)
{
	struct reserved_mem *rmem;
	int ret = 0;

	rmem = of_reserved_mem_lookup(pdev->dev.of_node);
	if (!rmem) {
		dev_err(&pdev->dev, "failed to acquire memory region\n");
		return -EINVAL;
	}

	cmd_db_header = memremap(rmem->base, rmem->size, MEMREMAP_WB);
	if (!cmd_db_header) {
		ret = -ENOMEM;
		cmd_db_header = NULL;
		return ret;
	}

	if (!cmd_db_magic_matches(cmd_db_header)) {
		dev_err(&pdev->dev, "Invalid Command DB Magic\n");
		return -EINVAL;
	}

	debugfs_create_file("cmd-db", 0400, NULL, NULL, &cmd_db_debugfs_ops);

	device_set_pm_not_required(&pdev->dev);

	return 0;
}

static const struct of_device_id cmd_db_match_table[] = {
	{ .compatible = "qcom,cmd-db" },
	{ }
};
MODULE_DEVICE_TABLE(of, cmd_db_match_table);

static struct platform_driver cmd_db_dev_driver = {
	.probe  = cmd_db_dev_probe,
	.driver = {
		   .name = "cmd-db",
		   .of_match_table = cmd_db_match_table,
		   .suppress_bind_attrs = true,
	},
};

static int __init cmd_db_device_init(void)
{
	return platform_driver_register(&cmd_db_dev_driver);
}
arch_initcall(cmd_db_device_init);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Command DB Driver");
MODULE_LICENSE("GPL v2");
