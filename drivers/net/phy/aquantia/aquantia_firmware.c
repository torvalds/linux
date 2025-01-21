// SPDX-License-Identifier: GPL-2.0

#include <linux/bitfield.h>
#include <linux/of.h>
#include <linux/firmware.h>
#include <linux/crc-itu-t.h>
#include <linux/nvmem-consumer.h>

#include <linux/unaligned.h>

#include "aquantia.h"

#define UP_RESET_SLEEP		100

/* addresses of memory segments in the phy */
#define DRAM_BASE_ADDR		0x3FFE0000
#define IRAM_BASE_ADDR		0x40000000

/* firmware image format constants */
#define VERSION_STRING_SIZE		0x40
#define VERSION_STRING_OFFSET		0x0200
/* primary offset is written at an offset from the start of the fw blob */
#define PRIMARY_OFFSET_OFFSET		0x8
/* primary offset needs to be then added to a base offset */
#define PRIMARY_OFFSET_SHIFT		12
#define PRIMARY_OFFSET(x)		((x) << PRIMARY_OFFSET_SHIFT)
#define HEADER_OFFSET			0x300

struct aqr_fw_header {
	u32 padding;
	u8 iram_offset[3];
	u8 iram_size[3];
	u8 dram_offset[3];
	u8 dram_size[3];
} __packed;

enum aqr_fw_src {
	AQR_FW_SRC_NVMEM = 0,
	AQR_FW_SRC_FS,
};

static const char * const aqr_fw_src_string[] = {
	[AQR_FW_SRC_NVMEM] = "NVMEM",
	[AQR_FW_SRC_FS] = "FS",
};

/* AQR firmware doesn't have fixed offsets for iram and dram section
 * but instead provide an header with the offset to use on reading
 * and parsing the firmware.
 *
 * AQR firmware can't be trusted and each offset is validated to be
 * not negative and be in the size of the firmware itself.
 */
static bool aqr_fw_validate_get(size_t size, size_t offset, size_t get_size)
{
	return offset + get_size <= size;
}

static int aqr_fw_get_be16(const u8 *data, size_t offset, size_t size, u16 *value)
{
	if (!aqr_fw_validate_get(size, offset, sizeof(u16)))
		return -EINVAL;

	*value = get_unaligned_be16(data + offset);

	return 0;
}

static int aqr_fw_get_le16(const u8 *data, size_t offset, size_t size, u16 *value)
{
	if (!aqr_fw_validate_get(size, offset, sizeof(u16)))
		return -EINVAL;

	*value = get_unaligned_le16(data + offset);

	return 0;
}

static int aqr_fw_get_le24(const u8 *data, size_t offset, size_t size, u32 *value)
{
	if (!aqr_fw_validate_get(size, offset, sizeof(u8) * 3))
		return -EINVAL;

	*value = get_unaligned_le24(data + offset);

	return 0;
}

/* load data into the phy's memory */
static int aqr_fw_load_memory(struct phy_device *phydev, u32 addr,
			      const u8 *data, size_t len)
{
	u16 crc = 0, up_crc;
	size_t pos;

	phy_write_mmd(phydev, MDIO_MMD_VEND1,
		      VEND1_GLOBAL_MAILBOX_INTERFACE1,
		      VEND1_GLOBAL_MAILBOX_INTERFACE1_CRC_RESET);
	phy_write_mmd(phydev, MDIO_MMD_VEND1,
		      VEND1_GLOBAL_MAILBOX_INTERFACE3,
		      VEND1_GLOBAL_MAILBOX_INTERFACE3_MSW_ADDR(addr));
	phy_write_mmd(phydev, MDIO_MMD_VEND1,
		      VEND1_GLOBAL_MAILBOX_INTERFACE4,
		      VEND1_GLOBAL_MAILBOX_INTERFACE4_LSW_ADDR(addr));

	/* We assume and enforce the size to be word aligned.
	 * If a firmware that is not word aligned is found, please report upstream.
	 */
	for (pos = 0; pos < len; pos += sizeof(u32)) {
		u8 crc_data[4];
		u32 word;

		/* FW data is always stored in little-endian */
		word = get_unaligned_le32((const u32 *)(data + pos));

		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLOBAL_MAILBOX_INTERFACE5,
			      VEND1_GLOBAL_MAILBOX_INTERFACE5_MSW_DATA(word));
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLOBAL_MAILBOX_INTERFACE6,
			      VEND1_GLOBAL_MAILBOX_INTERFACE6_LSW_DATA(word));

		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLOBAL_MAILBOX_INTERFACE1,
			      VEND1_GLOBAL_MAILBOX_INTERFACE1_EXECUTE |
			      VEND1_GLOBAL_MAILBOX_INTERFACE1_WRITE);

		/* Word is swapped internally and MAILBOX CRC is calculated
		 * using big-endian order. Mimic what the PHY does to have a
		 * matching CRC...
		 */
		crc_data[0] = word >> 24;
		crc_data[1] = word >> 16;
		crc_data[2] = word >> 8;
		crc_data[3] = word;

		/* ...calculate CRC as we load data... */
		crc = crc_itu_t(crc, crc_data, sizeof(crc_data));
	}
	/* ...gets CRC from MAILBOX after we have loaded the entire section... */
	up_crc = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLOBAL_MAILBOX_INTERFACE2);
	/* ...and make sure it does match our calculated CRC */
	if (crc != up_crc) {
		phydev_err(phydev, "CRC mismatch: calculated 0x%04x PHY 0x%04x\n",
			   crc, up_crc);
		return -EINVAL;
	}

	return 0;
}

static int aqr_fw_boot(struct phy_device *phydev, const u8 *data, size_t size,
		       enum aqr_fw_src fw_src)
{
	u16 calculated_crc, read_crc, read_primary_offset;
	u32 iram_offset = 0, iram_size = 0;
	u32 dram_offset = 0, dram_size = 0;
	char version[VERSION_STRING_SIZE];
	u32 primary_offset = 0;
	int ret;

	/* extract saved CRC at the end of the fw
	 * CRC is saved in big-endian as PHY is BE
	 */
	ret = aqr_fw_get_be16(data, size - sizeof(u16), size, &read_crc);
	if (ret) {
		phydev_err(phydev, "bad firmware CRC in firmware\n");
		return ret;
	}
	calculated_crc = crc_itu_t(0, data, size - sizeof(u16));
	if (read_crc != calculated_crc) {
		phydev_err(phydev, "bad firmware CRC: file 0x%04x calculated 0x%04x\n",
			   read_crc, calculated_crc);
		return -EINVAL;
	}

	/* Get the primary offset to extract DRAM and IRAM sections. */
	ret = aqr_fw_get_le16(data, PRIMARY_OFFSET_OFFSET, size, &read_primary_offset);
	if (ret) {
		phydev_err(phydev, "bad primary offset in firmware\n");
		return ret;
	}
	primary_offset = PRIMARY_OFFSET(read_primary_offset);

	/* Find the DRAM and IRAM sections within the firmware file.
	 * Make sure the fw_header is correctly in the firmware.
	 */
	if (!aqr_fw_validate_get(size, primary_offset + HEADER_OFFSET,
				 sizeof(struct aqr_fw_header))) {
		phydev_err(phydev, "bad fw_header in firmware\n");
		return -EINVAL;
	}

	/* offset are in LE and values needs to be converted to cpu endian */
	ret = aqr_fw_get_le24(data, primary_offset + HEADER_OFFSET +
			      offsetof(struct aqr_fw_header, iram_offset),
			      size, &iram_offset);
	if (ret) {
		phydev_err(phydev, "bad iram offset in firmware\n");
		return ret;
	}
	ret = aqr_fw_get_le24(data, primary_offset + HEADER_OFFSET +
			      offsetof(struct aqr_fw_header, iram_size),
			      size, &iram_size);
	if (ret) {
		phydev_err(phydev, "invalid iram size in firmware\n");
		return ret;
	}
	ret = aqr_fw_get_le24(data, primary_offset + HEADER_OFFSET +
			      offsetof(struct aqr_fw_header, dram_offset),
			      size, &dram_offset);
	if (ret) {
		phydev_err(phydev, "bad dram offset in firmware\n");
		return ret;
	}
	ret = aqr_fw_get_le24(data, primary_offset + HEADER_OFFSET +
			      offsetof(struct aqr_fw_header, dram_size),
			      size, &dram_size);
	if (ret) {
		phydev_err(phydev, "invalid dram size in firmware\n");
		return ret;
	}

	/* Increment the offset with the primary offset.
	 * Validate iram/dram offset and size.
	 */
	iram_offset += primary_offset;
	if (iram_size % sizeof(u32)) {
		phydev_err(phydev, "iram size if not aligned to word size. Please report this upstream!\n");
		return -EINVAL;
	}
	if (!aqr_fw_validate_get(size, iram_offset, iram_size)) {
		phydev_err(phydev, "invalid iram offset for iram size\n");
		return -EINVAL;
	}

	dram_offset += primary_offset;
	if (dram_size % sizeof(u32)) {
		phydev_err(phydev, "dram size if not aligned to word size. Please report this upstream!\n");
		return -EINVAL;
	}
	if (!aqr_fw_validate_get(size, dram_offset, dram_size)) {
		phydev_err(phydev, "invalid iram offset for iram size\n");
		return -EINVAL;
	}

	phydev_dbg(phydev, "primary %d IRAM offset=%d size=%d DRAM offset=%d size=%d\n",
		   primary_offset, iram_offset, iram_size, dram_offset, dram_size);

	if (!aqr_fw_validate_get(size, dram_offset + VERSION_STRING_OFFSET,
				 VERSION_STRING_SIZE)) {
		phydev_err(phydev, "invalid version in firmware\n");
		return -EINVAL;
	}
	strscpy(version, (char *)data + dram_offset + VERSION_STRING_OFFSET,
		VERSION_STRING_SIZE);
	if (version[0] == '\0') {
		phydev_err(phydev, "invalid version in firmware\n");
		return -EINVAL;
	}
	phydev_info(phydev, "loading firmware version '%s' from '%s'\n", version,
		    aqr_fw_src_string[fw_src]);

	/* stall the microcprocessor */
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLOBAL_CONTROL2,
		      VEND1_GLOBAL_CONTROL2_UP_RUN_STALL | VEND1_GLOBAL_CONTROL2_UP_RUN_STALL_OVD);

	phydev_dbg(phydev, "loading DRAM 0x%08x from offset=%d size=%d\n",
		   DRAM_BASE_ADDR, dram_offset, dram_size);
	ret = aqr_fw_load_memory(phydev, DRAM_BASE_ADDR, data + dram_offset,
				 dram_size);
	if (ret)
		return ret;

	phydev_dbg(phydev, "loading IRAM 0x%08x from offset=%d size=%d\n",
		   IRAM_BASE_ADDR, iram_offset, iram_size);
	ret = aqr_fw_load_memory(phydev, IRAM_BASE_ADDR, data + iram_offset,
				 iram_size);
	if (ret)
		return ret;

	/* make sure soft reset and low power mode are clear */
	phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLOBAL_SC,
			   VEND1_GLOBAL_SC_SOFT_RESET | VEND1_GLOBAL_SC_LOW_POWER);

	/* Release the microprocessor. UP_RESET must be held for 100 usec. */
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLOBAL_CONTROL2,
		      VEND1_GLOBAL_CONTROL2_UP_RUN_STALL |
		      VEND1_GLOBAL_CONTROL2_UP_RUN_STALL_OVD |
		      VEND1_GLOBAL_CONTROL2_UP_RUN_STALL_RST);
	usleep_range(UP_RESET_SLEEP, UP_RESET_SLEEP * 2);

	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLOBAL_CONTROL2,
		      VEND1_GLOBAL_CONTROL2_UP_RUN_STALL_OVD);

	return 0;
}

static int aqr_firmware_load_nvmem(struct phy_device *phydev)
{
	struct nvmem_cell *cell;
	size_t size;
	u8 *buf;
	int ret;

	cell = nvmem_cell_get(&phydev->mdio.dev, "firmware");
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	buf = nvmem_cell_read(cell, &size);
	if (IS_ERR(buf)) {
		ret = PTR_ERR(buf);
		goto exit;
	}

	ret = aqr_fw_boot(phydev, buf, size, AQR_FW_SRC_NVMEM);
	if (ret)
		phydev_err(phydev, "firmware loading failed: %d\n", ret);

	kfree(buf);
exit:
	nvmem_cell_put(cell);

	return ret;
}

static int aqr_firmware_load_fs(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	const struct firmware *fw;
	const char *fw_name;
	int ret;

	ret = of_property_read_string(dev->of_node, "firmware-name",
				      &fw_name);
	if (ret)
		return ret;

	ret = request_firmware(&fw, fw_name, dev);
	if (ret) {
		phydev_err(phydev, "failed to find FW file %s (%d)\n",
			   fw_name, ret);
		return ret;
	}

	ret = aqr_fw_boot(phydev, fw->data, fw->size, AQR_FW_SRC_FS);
	if (ret)
		phydev_err(phydev, "firmware loading failed: %d\n", ret);

	release_firmware(fw);

	return ret;
}

int aqr_firmware_load(struct phy_device *phydev)
{
	int ret;

	/* Check if the firmware is not already loaded by polling
	 * the current version returned by the PHY.
	 */
	ret = aqr_wait_reset_complete(phydev);
	switch (ret) {
	case 0:
		/* Some firmware is loaded => do nothing */
		return 0;
	case -ETIMEDOUT:
		/* VEND1_GLOBAL_FW_ID still reads 0 after 2 seconds of polling.
		 * We don't have full confidence that no firmware is loaded (in
		 * theory it might just not have loaded yet), but we will
		 * assume that, and load a new image.
		 */
		ret = aqr_firmware_load_nvmem(phydev);
		if (!ret)
			return ret;

		ret = aqr_firmware_load_fs(phydev);
		if (ret)
			return ret;
		break;
	default:
		/* PHY read error, propagate it to the caller */
		return ret;
	}

	return 0;
}
