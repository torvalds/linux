// SPDX-License-Identifier: GPL-2.0
/*
 * Thunderbolt driver - capabilities lookup
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2018, Intel Corporation
 */

#include <linux/slab.h>
#include <linux/errno.h>

#include "tb.h"

#define CAP_OFFSET_MAX		0xff
#define VSE_CAP_OFFSET_MAX	0xffff
#define TMU_ACCESS_EN		BIT(20)

static int tb_port_enable_tmu(struct tb_port *port, bool enable)
{
	struct tb_switch *sw = port->sw;
	u32 value, offset;
	int ret;

	/*
	 * Legacy devices need to have TMU access enabled before port
	 * space can be fully accessed.
	 */
	if (tb_switch_is_light_ridge(sw))
		offset = 0x26;
	else if (tb_switch_is_eagle_ridge(sw))
		offset = 0x2a;
	else
		return 0;

	ret = tb_sw_read(sw, &value, TB_CFG_SWITCH, offset, 1);
	if (ret)
		return ret;

	if (enable)
		value |= TMU_ACCESS_EN;
	else
		value &= ~TMU_ACCESS_EN;

	return tb_sw_write(sw, &value, TB_CFG_SWITCH, offset, 1);
}

static void tb_port_dummy_read(struct tb_port *port)
{
	/*
	 * When reading from next capability pointer location in port
	 * config space the read data is not cleared on LR. To avoid
	 * reading stale data on next read perform one dummy read after
	 * port capabilities are walked.
	 */
	if (tb_switch_is_light_ridge(port->sw)) {
		u32 dummy;

		tb_port_read(port, &dummy, TB_CFG_PORT, 0, 1);
	}
}

/**
 * tb_port_next_cap() - Return next capability in the linked list
 * @port: Port to find the capability for
 * @offset: Previous capability offset (%0 for start)
 *
 * Returns dword offset of the next capability in port config space
 * capability list and returns it. Passing %0 returns the first entry in
 * the capability list. If no next capability is found returns %0. In case
 * of failure returns negative errno.
 */
int tb_port_next_cap(struct tb_port *port, unsigned int offset)
{
	struct tb_cap_any header;
	int ret;

	if (!offset)
		return port->config.first_cap_offset;

	ret = tb_port_read(port, &header, TB_CFG_PORT, offset, 1);
	if (ret)
		return ret;

	return header.basic.next;
}

static int __tb_port_find_cap(struct tb_port *port, enum tb_port_cap cap)
{
	int offset = 0;

	do {
		struct tb_cap_any header;
		int ret;

		offset = tb_port_next_cap(port, offset);
		if (offset < 0)
			return offset;

		ret = tb_port_read(port, &header, TB_CFG_PORT, offset, 1);
		if (ret)
			return ret;

		if (header.basic.cap == cap)
			return offset;
	} while (offset > 0);

	return -ENOENT;
}

/**
 * tb_port_find_cap() - Find port capability
 * @port: Port to find the capability for
 * @cap: Capability to look
 *
 * Returns offset to start of capability or %-ENOENT if no such
 * capability was found. Negative errno is returned if there was an
 * error.
 */
int tb_port_find_cap(struct tb_port *port, enum tb_port_cap cap)
{
	int ret;

	ret = tb_port_enable_tmu(port, true);
	if (ret)
		return ret;

	ret = __tb_port_find_cap(port, cap);

	tb_port_dummy_read(port);
	tb_port_enable_tmu(port, false);

	return ret;
}

/**
 * tb_switch_next_cap() - Return next capability in the linked list
 * @sw: Switch to find the capability for
 * @offset: Previous capability offset (%0 for start)
 *
 * Finds dword offset of the next capability in router config space
 * capability list and returns it. Passing %0 returns the first entry in
 * the capability list. If no next capability is found returns %0. In case
 * of failure returns negative errno.
 */
int tb_switch_next_cap(struct tb_switch *sw, unsigned int offset)
{
	struct tb_cap_any header;
	int ret;

	if (!offset)
		return sw->config.first_cap_offset;

	ret = tb_sw_read(sw, &header, TB_CFG_SWITCH, offset, 2);
	if (ret)
		return ret;

	switch (header.basic.cap) {
	case TB_SWITCH_CAP_TMU:
		ret = header.basic.next;
		break;

	case TB_SWITCH_CAP_VSE:
		if (!header.extended_short.length)
			ret = header.extended_long.next;
		else
			ret = header.extended_short.next;
		break;

	default:
		tb_sw_dbg(sw, "unknown capability %#x at %#x\n",
			  header.basic.cap, offset);
		ret = -EINVAL;
		break;
	}

	return ret >= VSE_CAP_OFFSET_MAX ? 0 : ret;
}

/**
 * tb_switch_find_cap() - Find switch capability
 * @sw Switch to find the capability for
 * @cap: Capability to look
 *
 * Returns offset to start of capability or %-ENOENT if no such
 * capability was found. Negative errno is returned if there was an
 * error.
 */
int tb_switch_find_cap(struct tb_switch *sw, enum tb_switch_cap cap)
{
	int offset = 0;

	do {
		struct tb_cap_any header;
		int ret;

		offset = tb_switch_next_cap(sw, offset);
		if (offset < 0)
			return offset;

		ret = tb_sw_read(sw, &header, TB_CFG_SWITCH, offset, 1);
		if (ret)
			return ret;

		if (header.basic.cap == cap)
			return offset;
	} while (offset);

	return -ENOENT;
}

/**
 * tb_switch_find_vse_cap() - Find switch vendor specific capability
 * @sw: Switch to find the capability for
 * @vsec: Vendor specific capability to look
 *
 * Functions enumerates vendor specific capabilities (VSEC) of a switch
 * and returns offset when capability matching @vsec is found. If no
 * such capability is found returns %-ENOENT. In case of error returns
 * negative errno.
 */
int tb_switch_find_vse_cap(struct tb_switch *sw, enum tb_switch_vse_cap vsec)
{
	int offset = 0;

	do {
		struct tb_cap_any header;
		int ret;

		offset = tb_switch_next_cap(sw, offset);
		if (offset < 0)
			return offset;

		ret = tb_sw_read(sw, &header, TB_CFG_SWITCH, offset, 1);
		if (ret)
			return ret;

		if (header.extended_short.cap == TB_SWITCH_CAP_VSE &&
		    header.extended_short.vsec_id == vsec)
			return offset;
	} while (offset);

	return -ENOENT;
}
