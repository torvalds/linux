// SPDX-License-Identifier: GPL-2.0
/*
 * Thunderbolt Cactus Ridge driver - eeprom access
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#include <linux/crc32.h>
#include <linux/property.h>
#include <linux/slab.h>
#include "tb.h"

/**
 * tb_eeprom_ctl_write() - write control word
 */
static int tb_eeprom_ctl_write(struct tb_switch *sw, struct tb_eeprom_ctl *ctl)
{
	return tb_sw_write(sw, ctl, TB_CFG_SWITCH, sw->cap_plug_events + 4, 1);
}

/**
 * tb_eeprom_ctl_write() - read control word
 */
static int tb_eeprom_ctl_read(struct tb_switch *sw, struct tb_eeprom_ctl *ctl)
{
	return tb_sw_read(sw, ctl, TB_CFG_SWITCH, sw->cap_plug_events + 4, 1);
}

enum tb_eeprom_transfer {
	TB_EEPROM_IN,
	TB_EEPROM_OUT,
};

/**
 * tb_eeprom_active - enable rom access
 *
 * WARNING: Always disable access after usage. Otherwise the controller will
 * fail to reprobe.
 */
static int tb_eeprom_active(struct tb_switch *sw, bool enable)
{
	struct tb_eeprom_ctl ctl;
	int res = tb_eeprom_ctl_read(sw, &ctl);
	if (res)
		return res;
	if (enable) {
		ctl.access_high = 1;
		res = tb_eeprom_ctl_write(sw, &ctl);
		if (res)
			return res;
		ctl.access_low = 0;
		return tb_eeprom_ctl_write(sw, &ctl);
	} else {
		ctl.access_low = 1;
		res = tb_eeprom_ctl_write(sw, &ctl);
		if (res)
			return res;
		ctl.access_high = 0;
		return tb_eeprom_ctl_write(sw, &ctl);
	}
}

/**
 * tb_eeprom_transfer - transfer one bit
 *
 * If TB_EEPROM_IN is passed, then the bit can be retrieved from ctl->data_in.
 * If TB_EEPROM_OUT is passed, then ctl->data_out will be written.
 */
static int tb_eeprom_transfer(struct tb_switch *sw, struct tb_eeprom_ctl *ctl,
			      enum tb_eeprom_transfer direction)
{
	int res;
	if (direction == TB_EEPROM_OUT) {
		res = tb_eeprom_ctl_write(sw, ctl);
		if (res)
			return res;
	}
	ctl->clock = 1;
	res = tb_eeprom_ctl_write(sw, ctl);
	if (res)
		return res;
	if (direction == TB_EEPROM_IN) {
		res = tb_eeprom_ctl_read(sw, ctl);
		if (res)
			return res;
	}
	ctl->clock = 0;
	return tb_eeprom_ctl_write(sw, ctl);
}

/**
 * tb_eeprom_out - write one byte to the bus
 */
static int tb_eeprom_out(struct tb_switch *sw, u8 val)
{
	struct tb_eeprom_ctl ctl;
	int i;
	int res = tb_eeprom_ctl_read(sw, &ctl);
	if (res)
		return res;
	for (i = 0; i < 8; i++) {
		ctl.data_out = val & 0x80;
		res = tb_eeprom_transfer(sw, &ctl, TB_EEPROM_OUT);
		if (res)
			return res;
		val <<= 1;
	}
	return 0;
}

/**
 * tb_eeprom_in - read one byte from the bus
 */
static int tb_eeprom_in(struct tb_switch *sw, u8 *val)
{
	struct tb_eeprom_ctl ctl;
	int i;
	int res = tb_eeprom_ctl_read(sw, &ctl);
	if (res)
		return res;
	*val = 0;
	for (i = 0; i < 8; i++) {
		*val <<= 1;
		res = tb_eeprom_transfer(sw, &ctl, TB_EEPROM_IN);
		if (res)
			return res;
		*val |= ctl.data_in;
	}
	return 0;
}

/**
 * tb_eeprom_read_n - read count bytes from offset into val
 */
static int tb_eeprom_read_n(struct tb_switch *sw, u16 offset, u8 *val,
		size_t count)
{
	int i, res;
	res = tb_eeprom_active(sw, true);
	if (res)
		return res;
	res = tb_eeprom_out(sw, 3);
	if (res)
		return res;
	res = tb_eeprom_out(sw, offset >> 8);
	if (res)
		return res;
	res = tb_eeprom_out(sw, offset);
	if (res)
		return res;
	for (i = 0; i < count; i++) {
		res = tb_eeprom_in(sw, val + i);
		if (res)
			return res;
	}
	return tb_eeprom_active(sw, false);
}

static u8 tb_crc8(u8 *data, int len)
{
	int i, j;
	u8 val = 0xff;
	for (i = 0; i < len; i++) {
		val ^= data[i];
		for (j = 0; j < 8; j++)
			val = (val << 1) ^ ((val & 0x80) ? 7 : 0);
	}
	return val;
}

static u32 tb_crc32(void *data, size_t len)
{
	return ~__crc32c_le(~0, data, len);
}

#define TB_DROM_DATA_START 13
struct tb_drom_header {
	/* BYTE 0 */
	u8 uid_crc8; /* checksum for uid */
	/* BYTES 1-8 */
	u64 uid;
	/* BYTES 9-12 */
	u32 data_crc32; /* checksum for data_len bytes starting at byte 13 */
	/* BYTE 13 */
	u8 device_rom_revision; /* should be <= 1 */
	u16 data_len:10;
	u8 __unknown1:6;
	/* BYTES 16-21 */
	u16 vendor_id;
	u16 model_id;
	u8 model_rev;
	u8 eeprom_rev;
} __packed;

enum tb_drom_entry_type {
	/* force unsigned to prevent "one-bit signed bitfield" warning */
	TB_DROM_ENTRY_GENERIC = 0U,
	TB_DROM_ENTRY_PORT,
};

struct tb_drom_entry_header {
	u8 len;
	u8 index:6;
	bool port_disabled:1; /* only valid if type is TB_DROM_ENTRY_PORT */
	enum tb_drom_entry_type type:1;
} __packed;

struct tb_drom_entry_generic {
	struct tb_drom_entry_header header;
	u8 data[0];
} __packed;

struct tb_drom_entry_port {
	/* BYTES 0-1 */
	struct tb_drom_entry_header header;
	/* BYTE 2 */
	u8 dual_link_port_rid:4;
	u8 link_nr:1;
	u8 unknown1:2;
	bool has_dual_link_port:1;

	/* BYTE 3 */
	u8 dual_link_port_nr:6;
	u8 unknown2:2;

	/* BYTES 4 - 5 TODO decode */
	u8 micro2:4;
	u8 micro1:4;
	u8 micro3;

	/* BYTES 6-7, TODO: verify (find hardware that has these set) */
	u8 peer_port_rid:4;
	u8 unknown3:3;
	bool has_peer_port:1;
	u8 peer_port_nr:6;
	u8 unknown4:2;
} __packed;


/**
 * tb_eeprom_get_drom_offset - get drom offset within eeprom
 */
static int tb_eeprom_get_drom_offset(struct tb_switch *sw, u16 *offset)
{
	struct tb_cap_plug_events cap;
	int res;
	if (!sw->cap_plug_events) {
		tb_sw_warn(sw, "no TB_CAP_PLUG_EVENTS, cannot read eeprom\n");
		return -ENOSYS;
	}
	res = tb_sw_read(sw, &cap, TB_CFG_SWITCH, sw->cap_plug_events,
			     sizeof(cap) / 4);
	if (res)
		return res;

	if (!cap.eeprom_ctl.present || cap.eeprom_ctl.not_present) {
		tb_sw_warn(sw, "no NVM\n");
		return -ENOSYS;
	}

	if (cap.drom_offset > 0xffff) {
		tb_sw_warn(sw, "drom offset is larger than 0xffff: %#x\n",
				cap.drom_offset);
		return -ENXIO;
	}
	*offset = cap.drom_offset;
	return 0;
}

/**
 * tb_drom_read_uid_only - read uid directly from drom
 *
 * Does not use the cached copy in sw->drom. Used during resume to check switch
 * identity.
 */
int tb_drom_read_uid_only(struct tb_switch *sw, u64 *uid)
{
	u8 data[9];
	u16 drom_offset;
	u8 crc;
	int res = tb_eeprom_get_drom_offset(sw, &drom_offset);
	if (res)
		return res;

	if (drom_offset == 0)
		return -ENODEV;

	/* read uid */
	res = tb_eeprom_read_n(sw, drom_offset, data, 9);
	if (res)
		return res;

	crc = tb_crc8(data + 1, 8);
	if (crc != data[0]) {
		tb_sw_warn(sw, "uid crc8 mismatch (expected: %#x, got: %#x)\n",
				data[0], crc);
		return -EIO;
	}

	*uid = *(u64 *)(data+1);
	return 0;
}

static int tb_drom_parse_entry_generic(struct tb_switch *sw,
		struct tb_drom_entry_header *header)
{
	const struct tb_drom_entry_generic *entry =
		(const struct tb_drom_entry_generic *)header;

	switch (header->index) {
	case 1:
		/* Length includes 2 bytes header so remove it before copy */
		sw->vendor_name = kstrndup(entry->data,
			header->len - sizeof(*header), GFP_KERNEL);
		if (!sw->vendor_name)
			return -ENOMEM;
		break;

	case 2:
		sw->device_name = kstrndup(entry->data,
			header->len - sizeof(*header), GFP_KERNEL);
		if (!sw->device_name)
			return -ENOMEM;
		break;
	}

	return 0;
}

static int tb_drom_parse_entry_port(struct tb_switch *sw,
				    struct tb_drom_entry_header *header)
{
	struct tb_port *port;
	int res;
	enum tb_port_type type;

	/*
	 * Some DROMs list more ports than the controller actually has
	 * so we skip those but allow the parser to continue.
	 */
	if (header->index > sw->config.max_port_number) {
		dev_info_once(&sw->dev, "ignoring unnecessary extra entries in DROM\n");
		return 0;
	}

	port = &sw->ports[header->index];
	port->disabled = header->port_disabled;
	if (port->disabled)
		return 0;

	res = tb_port_read(port, &type, TB_CFG_PORT, 2, 1);
	if (res)
		return res;
	type &= 0xffffff;

	if (type == TB_TYPE_PORT) {
		struct tb_drom_entry_port *entry = (void *) header;
		if (header->len != sizeof(*entry)) {
			tb_sw_warn(sw,
				"port entry has size %#x (expected %#zx)\n",
				header->len, sizeof(struct tb_drom_entry_port));
			return -EIO;
		}
		port->link_nr = entry->link_nr;
		if (entry->has_dual_link_port)
			port->dual_link_port =
				&port->sw->ports[entry->dual_link_port_nr];
	}
	return 0;
}

/**
 * tb_drom_parse_entries - parse the linked list of drom entries
 *
 * Drom must have been copied to sw->drom.
 */
static int tb_drom_parse_entries(struct tb_switch *sw)
{
	struct tb_drom_header *header = (void *) sw->drom;
	u16 pos = sizeof(*header);
	u16 drom_size = header->data_len + TB_DROM_DATA_START;
	int res;

	while (pos < drom_size) {
		struct tb_drom_entry_header *entry = (void *) (sw->drom + pos);
		if (pos + 1 == drom_size || pos + entry->len > drom_size
				|| !entry->len) {
			tb_sw_warn(sw, "drom buffer overrun, aborting\n");
			return -EIO;
		}

		switch (entry->type) {
		case TB_DROM_ENTRY_GENERIC:
			res = tb_drom_parse_entry_generic(sw, entry);
			break;
		case TB_DROM_ENTRY_PORT:
			res = tb_drom_parse_entry_port(sw, entry);
			break;
		}
		if (res)
			return res;

		pos += entry->len;
	}
	return 0;
}

/**
 * tb_drom_copy_efi - copy drom supplied by EFI to sw->drom if present
 */
static int tb_drom_copy_efi(struct tb_switch *sw, u16 *size)
{
	struct device *dev = &sw->tb->nhi->pdev->dev;
	int len, res;

	len = device_property_read_u8_array(dev, "ThunderboltDROM", NULL, 0);
	if (len < 0 || len < sizeof(struct tb_drom_header))
		return -EINVAL;

	sw->drom = kmalloc(len, GFP_KERNEL);
	if (!sw->drom)
		return -ENOMEM;

	res = device_property_read_u8_array(dev, "ThunderboltDROM", sw->drom,
									len);
	if (res)
		goto err;

	*size = ((struct tb_drom_header *)sw->drom)->data_len +
							  TB_DROM_DATA_START;
	if (*size > len)
		goto err;

	return 0;

err:
	kfree(sw->drom);
	sw->drom = NULL;
	return -EINVAL;
}

static int tb_drom_copy_nvm(struct tb_switch *sw, u16 *size)
{
	u32 drom_offset;
	int ret;

	if (!sw->dma_port)
		return -ENODEV;

	ret = tb_sw_read(sw, &drom_offset, TB_CFG_SWITCH,
			 sw->cap_plug_events + 12, 1);
	if (ret)
		return ret;

	if (!drom_offset)
		return -ENODEV;

	ret = dma_port_flash_read(sw->dma_port, drom_offset + 14, size,
				  sizeof(*size));
	if (ret)
		return ret;

	/* Size includes CRC8 + UID + CRC32 */
	*size += 1 + 8 + 4;
	sw->drom = kzalloc(*size, GFP_KERNEL);
	if (!sw->drom)
		return -ENOMEM;

	ret = dma_port_flash_read(sw->dma_port, drom_offset, sw->drom, *size);
	if (ret)
		goto err_free;

	/*
	 * Read UID from the minimal DROM because the one in NVM is just
	 * a placeholder.
	 */
	tb_drom_read_uid_only(sw, &sw->uid);
	return 0;

err_free:
	kfree(sw->drom);
	sw->drom = NULL;
	return ret;
}

/**
 * tb_drom_read - copy drom to sw->drom and parse it
 */
int tb_drom_read(struct tb_switch *sw)
{
	u16 drom_offset;
	u16 size;
	u32 crc;
	struct tb_drom_header *header;
	int res;
	if (sw->drom)
		return 0;

	if (tb_route(sw) == 0) {
		/*
		 * Apple's NHI EFI driver supplies a DROM for the root switch
		 * in a device property. Use it if available.
		 */
		if (tb_drom_copy_efi(sw, &size) == 0)
			goto parse;

		/* Non-Apple hardware has the DROM as part of NVM */
		if (tb_drom_copy_nvm(sw, &size) == 0)
			goto parse;

		/*
		 * The root switch contains only a dummy drom (header only,
		 * no entries). Hardcode the configuration here.
		 */
		tb_drom_read_uid_only(sw, &sw->uid);

		sw->ports[1].link_nr = 0;
		sw->ports[2].link_nr = 1;
		sw->ports[1].dual_link_port = &sw->ports[2];
		sw->ports[2].dual_link_port = &sw->ports[1];

		sw->ports[3].link_nr = 0;
		sw->ports[4].link_nr = 1;
		sw->ports[3].dual_link_port = &sw->ports[4];
		sw->ports[4].dual_link_port = &sw->ports[3];

		/* Port 5 is inaccessible on this gen 1 controller */
		if (sw->config.device_id == PCI_DEVICE_ID_INTEL_LIGHT_RIDGE)
			sw->ports[5].disabled = true;

		return 0;
	}

	res = tb_eeprom_get_drom_offset(sw, &drom_offset);
	if (res)
		return res;

	res = tb_eeprom_read_n(sw, drom_offset + 14, (u8 *) &size, 2);
	if (res)
		return res;
	size &= 0x3ff;
	size += TB_DROM_DATA_START;
	tb_sw_dbg(sw, "reading drom (length: %#x)\n", size);
	if (size < sizeof(*header)) {
		tb_sw_warn(sw, "drom too small, aborting\n");
		return -EIO;
	}

	sw->drom = kzalloc(size, GFP_KERNEL);
	if (!sw->drom)
		return -ENOMEM;
	res = tb_eeprom_read_n(sw, drom_offset, sw->drom, size);
	if (res)
		goto err;

parse:
	header = (void *) sw->drom;

	if (header->data_len + TB_DROM_DATA_START != size) {
		tb_sw_warn(sw, "drom size mismatch, aborting\n");
		goto err;
	}

	crc = tb_crc8((u8 *) &header->uid, 8);
	if (crc != header->uid_crc8) {
		tb_sw_warn(sw,
			"drom uid crc8 mismatch (expected: %#x, got: %#x), aborting\n",
			header->uid_crc8, crc);
		goto err;
	}
	if (!sw->uid)
		sw->uid = header->uid;
	sw->vendor = header->vendor_id;
	sw->device = header->model_id;

	crc = tb_crc32(sw->drom + TB_DROM_DATA_START, header->data_len);
	if (crc != header->data_crc32) {
		tb_sw_warn(sw,
			"drom data crc32 mismatch (expected: %#x, got: %#x), continuing\n",
			header->data_crc32, crc);
	}

	if (header->device_rom_revision > 2)
		tb_sw_warn(sw, "drom device_rom_revision %#x unknown\n",
			header->device_rom_revision);

	return tb_drom_parse_entries(sw);
err:
	kfree(sw->drom);
	sw->drom = NULL;
	return -EIO;

}
