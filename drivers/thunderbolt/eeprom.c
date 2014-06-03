/*
 * Thunderbolt Cactus Ridge driver - eeprom access
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

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

int tb_eeprom_read_uid(struct tb_switch *sw, u64 *uid)
{
	u8 data[9];
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

	/* read uid */
	res = tb_eeprom_read_n(sw, cap.drom_offset, data, 9);
	if (res)
		return res;
	/* TODO: check checksum in data[0] */
	*uid = *(u64 *)(data+1);
	return 0;
}



