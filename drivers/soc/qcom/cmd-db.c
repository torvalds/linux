/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2016-2018, The Linux Foundation. All rights reserved. */

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <soc/qcom/cmd-db.h>

#define NUM_PRIORITY		2
#define MAX_SLV_ID		8
#define CMD_DB_MAGIC		0x0C0330DBUL
#define SLAVE_ID_MASK		0x7
#define SLAVE_ID_SHIFT		16

#define ENTRY_HEADER(hdr)	((void *)cmd_db_header +	\
				sizeof(*cmd_db_header) +	\
				hdr->header_offset)

#define RSC_OFFSET(hdr, ent)	((void *)cmd_db_header +	\
				sizeof(*cmd_db_header) +	\
				hdr.data_offset + ent.offset)

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
	u64 id;
	u32 priority[NUM_PRIORITY];
	u32 addr;
	u16 len;
	u16 offset;
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
	u16 slv_id;
	u16 header_offset;
	u16 data_offset;
	u16 cnt;
	u16 version;
	u16 reserved[3];
};

/**
 * struct cmd_db_header: The DB header information
 *
 * @version: The cmd db version
 * @magic_number: constant expected in the database
 * @header: array of resources
 * @checksum: checksum for the header. Unused.
 * @reserved: reserved memory
 * @data: driver specific data
 */
struct cmd_db_header {
	u32 version;
	u32 magic_num;
	struct rsc_hdr header[MAX_SLV_ID];
	u32 checksum;
	u32 reserved;
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

static struct cmd_db_header *cmd_db_header;

/**
 * cmd_db_ready - Indicates if command DB is available
 *
 * Return: 0 on success, errno otherwise
 */
int cmd_db_ready(void)
{
	if (cmd_db_header == NULL)
		return -EPROBE_DEFER;
	else if (cmd_db_header->magic_num != CMD_DB_MAGIC)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(cmd_db_ready);

static u64 cmd_db_get_u64_id(const char *id)
{
	u64 rsc_id = 0;
	u8 *ch = (u8 *)&rsc_id;

	strncpy(ch, id, min(strlen(id), sizeof(rsc_id)));

	return rsc_id;
}

static int cmd_db_get_header(u64 query, struct entry_header *eh,
			     struct rsc_hdr *rh)
{
	struct rsc_hdr *rsc_hdr;
	struct entry_header *ent;
	int ret, i, j;

	ret = cmd_db_ready();
	if (ret)
		return ret;

	if (!eh || !rh)
		return -EINVAL;

	for (i = 0; i < MAX_SLV_ID; i++) {
		rsc_hdr = &cmd_db_header->header[i];
		if (!rsc_hdr->slv_id)
			break;

		ent = ENTRY_HEADER(rsc_hdr);
		for (j = 0; j < rsc_hdr->cnt; j++, ent++) {
			if (ent->id == query)
				break;
		}

		if (j < rsc_hdr->cnt) {
			memcpy(eh, ent, sizeof(*ent));
			memcpy(rh, rsc_hdr, sizeof(*rh));
			return 0;
		}
	}

	return -ENODEV;
}

static int cmd_db_get_header_by_rsc_id(const char *id,
				       struct entry_header *ent_hdr,
				       struct rsc_hdr *rsc_hdr)
{
	u64 rsc_id = cmd_db_get_u64_id(id);

	return cmd_db_get_header(rsc_id, ent_hdr, rsc_hdr);
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
	struct entry_header ent;
	struct rsc_hdr rsc_hdr;

	ret = cmd_db_get_header_by_rsc_id(id, &ent, &rsc_hdr);

	return ret < 0 ? 0 : ent.addr;
}
EXPORT_SYMBOL(cmd_db_read_addr);

/**
 * cmd_db_read_aux_data() - Query command db for aux data.
 *
 *  @id: Resource to retrieve AUX Data on.
 *  @data: Data buffer to copy returned aux data to. Returns size on NULL
 *  @len: Caller provides size of data buffer passed in.
 *
 *  Return: size of data on success, errno otherwise
 */
int cmd_db_read_aux_data(const char *id, u8 *data, size_t len)
{
	int ret;
	struct entry_header ent;
	struct rsc_hdr rsc_hdr;

	if (!data)
		return -EINVAL;

	ret = cmd_db_get_header_by_rsc_id(id, &ent, &rsc_hdr);
	if (ret)
		return ret;

	if (len < ent.len)
		return -EINVAL;

	len = min_t(u16, ent.len, len);
	memcpy(data, RSC_OFFSET(rsc_hdr, ent), len);

	return len;
}
EXPORT_SYMBOL(cmd_db_read_aux_data);

/**
 * cmd_db_read_aux_data_len - Get the length of the auxiliary data stored in DB.
 *
 * @id: Resource to retrieve AUX Data.
 *
 * Return: size on success, 0 on error
 */
size_t cmd_db_read_aux_data_len(const char *id)
{
	int ret;
	struct entry_header ent;
	struct rsc_hdr rsc_hdr;

	ret = cmd_db_get_header_by_rsc_id(id, &ent, &rsc_hdr);

	return ret < 0 ? 0 : ent.len;
}
EXPORT_SYMBOL(cmd_db_read_aux_data_len);

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
	struct entry_header ent;
	struct rsc_hdr rsc_hdr;

	ret = cmd_db_get_header_by_rsc_id(id, &ent, &rsc_hdr);

	return ret < 0 ? CMD_DB_HW_INVALID :
		       (ent.addr >> SLAVE_ID_SHIFT) & SLAVE_ID_MASK;
}
EXPORT_SYMBOL(cmd_db_read_slave_id);

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
	if (IS_ERR_OR_NULL(cmd_db_header)) {
		ret = PTR_ERR(cmd_db_header);
		cmd_db_header = NULL;
		return ret;
	}

	if (cmd_db_header->magic_num != CMD_DB_MAGIC) {
		dev_err(&pdev->dev, "Invalid Command DB Magic\n");
		return -EINVAL;
	}

	return 0;
}

static const struct of_device_id cmd_db_match_table[] = {
	{ .compatible = "qcom,cmd-db" },
	{ },
};

static struct platform_driver cmd_db_dev_driver = {
	.probe  = cmd_db_dev_probe,
	.driver = {
		   .name = "cmd-db",
		   .of_match_table = cmd_db_match_table,
	},
};

static int __init cmd_db_device_init(void)
{
	return platform_driver_register(&cmd_db_dev_driver);
}
arch_initcall(cmd_db_device_init);
