// SPDX-License-Identifier: GPL-2.0

#include <linux/crc8.h>
#include <linux/etherdevice.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <uapi/linux/if_ether.h>

#define SL28VPD_MAGIC 'V'

struct sl28vpd_header {
	u8 magic;
	u8 version;
} __packed;

struct sl28vpd_v1 {
	struct sl28vpd_header header;
	char serial_number[15];
	u8 base_mac_address[ETH_ALEN];
	u8 crc8;
} __packed;

static int sl28vpd_mac_address_pp(void *priv, const char *id, int index,
				  unsigned int offset, void *buf,
				  size_t bytes)
{
	if (bytes != ETH_ALEN)
		return -EINVAL;

	if (index < 0)
		return -EINVAL;

	if (!is_valid_ether_addr(buf))
		return -EINVAL;

	eth_addr_add(buf, index);

	return 0;
}

static const struct nvmem_cell_info sl28vpd_v1_entries[] = {
	{
		.name = "serial-number",
		.offset = offsetof(struct sl28vpd_v1, serial_number),
		.bytes = sizeof_field(struct sl28vpd_v1, serial_number),
	},
	{
		.name = "base-mac-address",
		.offset = offsetof(struct sl28vpd_v1, base_mac_address),
		.bytes = sizeof_field(struct sl28vpd_v1, base_mac_address),
		.read_post_process = sl28vpd_mac_address_pp,
	},
};

static int sl28vpd_v1_check_crc(struct device *dev, struct nvmem_device *nvmem)
{
	struct sl28vpd_v1 data_v1;
	u8 table[CRC8_TABLE_SIZE];
	int ret;
	u8 crc;

	crc8_populate_msb(table, 0x07);

	ret = nvmem_device_read(nvmem, 0, sizeof(data_v1), &data_v1);
	if (ret < 0)
		return ret;
	else if (ret != sizeof(data_v1))
		return -EIO;

	crc = crc8(table, (void *)&data_v1, sizeof(data_v1) - 1, 0);

	if (crc != data_v1.crc8) {
		dev_err(dev,
			"Checksum is invalid (got %02x, expected %02x).\n",
			crc, data_v1.crc8);
		return -EINVAL;
	}

	return 0;
}

static int sl28vpd_add_cells(struct device *dev, struct nvmem_device *nvmem)
{
	const struct nvmem_cell_info *pinfo;
	struct nvmem_cell_info info = {0};
	struct device_node *layout_np;
	struct sl28vpd_header hdr;
	int ret, i;

	/* check header */
	ret = nvmem_device_read(nvmem, 0, sizeof(hdr), &hdr);
	if (ret < 0)
		return ret;
	else if (ret != sizeof(hdr))
		return -EIO;

	if (hdr.magic != SL28VPD_MAGIC) {
		dev_err(dev, "Invalid magic value (%02x)\n", hdr.magic);
		return -EINVAL;
	}

	if (hdr.version != 1) {
		dev_err(dev, "Version %d is unsupported.\n", hdr.version);
		return -EINVAL;
	}

	ret = sl28vpd_v1_check_crc(dev, nvmem);
	if (ret)
		return ret;

	layout_np = of_nvmem_layout_get_container(nvmem);
	if (!layout_np)
		return -ENOENT;

	for (i = 0; i < ARRAY_SIZE(sl28vpd_v1_entries); i++) {
		pinfo = &sl28vpd_v1_entries[i];

		info.name = pinfo->name;
		info.offset = pinfo->offset;
		info.bytes = pinfo->bytes;
		info.read_post_process = pinfo->read_post_process;
		info.np = of_get_child_by_name(layout_np, pinfo->name);

		ret = nvmem_add_one_cell(nvmem, &info);
		if (ret) {
			of_node_put(layout_np);
			return ret;
		}
	}

	of_node_put(layout_np);

	return 0;
}

static const struct of_device_id sl28vpd_of_match_table[] = {
	{ .compatible = "kontron,sl28-vpd" },
	{},
};
MODULE_DEVICE_TABLE(of, sl28vpd_of_match_table);

static struct nvmem_layout sl28vpd_layout = {
	.name = "sl28-vpd",
	.of_match_table = sl28vpd_of_match_table,
	.add_cells = sl28vpd_add_cells,
};
module_nvmem_layout_driver(sl28vpd_layout);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Walle <michael@walle.cc>");
MODULE_DESCRIPTION("NVMEM layout driver for the VPD of Kontron sl28 boards");
