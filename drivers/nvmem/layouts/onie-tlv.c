// SPDX-License-Identifier: GPL-2.0-only
/*
 * ONIE tlv NVMEM cells provider
 *
 * Copyright (C) 2022 Open Compute Group ONIE
 * Author: Miquel Raynal <miquel.raynal@bootlin.com>
 * Based on the nvmem driver written by: Vadym Kochan <vadym.kochan@plvision.eu>
 * Inspired by the first layout written by: Rafał Miłecki <rafal@milecki.pl>
 */

#include <linux/crc32.h>
#include <linux/etherdevice.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>

#define ONIE_TLV_MAX_LEN 2048
#define ONIE_TLV_CRC_FIELD_SZ 6
#define ONIE_TLV_CRC_SZ 4
#define ONIE_TLV_HDR_ID	"TlvInfo"

struct onie_tlv_hdr {
	u8 id[8];
	u8 version;
	__be16 data_len;
} __packed;

struct onie_tlv {
	u8 type;
	u8 len;
} __packed;

static const char *onie_tlv_cell_name(u8 type)
{
	switch (type) {
	case 0x21:
		return "product-name";
	case 0x22:
		return "part-number";
	case 0x23:
		return "serial-number";
	case 0x24:
		return "mac-address";
	case 0x25:
		return "manufacture-date";
	case 0x26:
		return "device-version";
	case 0x27:
		return "label-revision";
	case 0x28:
		return "platform-name";
	case 0x29:
		return "onie-version";
	case 0x2A:
		return "num-macs";
	case 0x2B:
		return "manufacturer";
	case 0x2C:
		return "country-code";
	case 0x2D:
		return "vendor";
	case 0x2E:
		return "diag-version";
	case 0x2F:
		return "service-tag";
	case 0xFD:
		return "vendor-extension";
	case 0xFE:
		return "crc32";
	default:
		break;
	}

	return NULL;
}

static int onie_tlv_mac_read_cb(void *priv, const char *id, int index,
				unsigned int offset, void *buf,
				size_t bytes)
{
	eth_addr_add(buf, index);

	return 0;
}

static nvmem_cell_post_process_t onie_tlv_read_cb(u8 type, u8 *buf)
{
	switch (type) {
	case 0x24:
		return &onie_tlv_mac_read_cb;
	default:
		break;
	}

	return NULL;
}

static int onie_tlv_add_cells(struct device *dev, struct nvmem_device *nvmem,
			      size_t data_len, u8 *data)
{
	struct nvmem_cell_info cell = {};
	struct device_node *layout;
	struct onie_tlv tlv;
	unsigned int hdr_len = sizeof(struct onie_tlv_hdr);
	unsigned int offset = 0;
	int ret;

	layout = of_nvmem_layout_get_container(nvmem);
	if (!layout)
		return -ENOENT;

	while (offset < data_len) {
		memcpy(&tlv, data + offset, sizeof(tlv));
		if (offset + tlv.len >= data_len) {
			dev_err(dev, "Out of bounds field (0x%x bytes at 0x%x)\n",
				tlv.len, hdr_len + offset);
			break;
		}

		cell.name = onie_tlv_cell_name(tlv.type);
		if (!cell.name)
			continue;

		cell.offset = hdr_len + offset + sizeof(tlv.type) + sizeof(tlv.len);
		cell.bytes = tlv.len;
		cell.np = of_get_child_by_name(layout, cell.name);
		cell.read_post_process = onie_tlv_read_cb(tlv.type, data + offset + sizeof(tlv));

		ret = nvmem_add_one_cell(nvmem, &cell);
		if (ret) {
			of_node_put(layout);
			return ret;
		}

		offset += sizeof(tlv) + tlv.len;
	}

	of_node_put(layout);

	return 0;
}

static bool onie_tlv_hdr_is_valid(struct device *dev, struct onie_tlv_hdr *hdr)
{
	if (memcmp(hdr->id, ONIE_TLV_HDR_ID, sizeof(hdr->id))) {
		dev_err(dev, "Invalid header\n");
		return false;
	}

	if (hdr->version != 0x1) {
		dev_err(dev, "Invalid version number\n");
		return false;
	}

	return true;
}

static bool onie_tlv_crc_is_valid(struct device *dev, size_t table_len, u8 *table)
{
	struct onie_tlv crc_hdr;
	u32 read_crc, calc_crc;
	__be32 crc_be;

	memcpy(&crc_hdr, table + table_len - ONIE_TLV_CRC_FIELD_SZ, sizeof(crc_hdr));
	if (crc_hdr.type != 0xfe || crc_hdr.len != ONIE_TLV_CRC_SZ) {
		dev_err(dev, "Invalid CRC field\n");
		return false;
	}

	/* The table contains a JAMCRC, which is XOR'ed compared to the original
	 * CRC32 implementation as known in the Ethernet world.
	 */
	memcpy(&crc_be, table + table_len - ONIE_TLV_CRC_SZ, ONIE_TLV_CRC_SZ);
	read_crc = be32_to_cpu(crc_be);
	calc_crc = crc32(~0, table, table_len - ONIE_TLV_CRC_SZ) ^ 0xFFFFFFFF;
	if (read_crc != calc_crc) {
		dev_err(dev, "Invalid CRC read: 0x%08x, expected: 0x%08x\n",
			read_crc, calc_crc);
		return false;
	}

	return true;
}

static int onie_tlv_parse_table(struct nvmem_layout *layout)
{
	struct nvmem_device *nvmem = layout->nvmem;
	struct device *dev = &layout->dev;
	struct onie_tlv_hdr hdr;
	size_t table_len, data_len, hdr_len;
	u8 *table, *data;
	int ret;

	ret = nvmem_device_read(nvmem, 0, sizeof(hdr), &hdr);
	if (ret < 0)
		return ret;

	if (!onie_tlv_hdr_is_valid(dev, &hdr)) {
		dev_err(dev, "Invalid ONIE TLV header\n");
		return -EINVAL;
	}

	hdr_len = sizeof(hdr.id) + sizeof(hdr.version) + sizeof(hdr.data_len);
	data_len = be16_to_cpu(hdr.data_len);
	table_len = hdr_len + data_len;
	if (table_len > ONIE_TLV_MAX_LEN) {
		dev_err(dev, "Invalid ONIE TLV data length\n");
		return -EINVAL;
	}

	table = devm_kmalloc(dev, table_len, GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	ret = nvmem_device_read(nvmem, 0, table_len, table);
	if (ret != table_len)
		return ret;

	if (!onie_tlv_crc_is_valid(dev, table_len, table))
		return -EINVAL;

	data = table + hdr_len;
	ret = onie_tlv_add_cells(dev, nvmem, data_len, data);
	if (ret)
		return ret;

	return 0;
}

static int onie_tlv_probe(struct nvmem_layout *layout)
{
	layout->add_cells = onie_tlv_parse_table;

	return nvmem_layout_register(layout);
}

static void onie_tlv_remove(struct nvmem_layout *layout)
{
	nvmem_layout_unregister(layout);
}

static const struct of_device_id onie_tlv_of_match_table[] = {
	{ .compatible = "onie,tlv-layout", },
	{},
};
MODULE_DEVICE_TABLE(of, onie_tlv_of_match_table);

static struct nvmem_layout_driver onie_tlv_layout = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "onie-tlv-layout",
		.of_match_table = onie_tlv_of_match_table,
	},
	.probe = onie_tlv_probe,
	.remove = onie_tlv_remove,
};
module_nvmem_layout_driver(onie_tlv_layout);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Miquel Raynal <miquel.raynal@bootlin.com>");
MODULE_DESCRIPTION("NVMEM layout driver for Onie TLV table parsing");
