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

struct tb_cap_any {
	union {
		struct tb_cap_basic basic;
		struct tb_cap_extended_short extended_short;
		struct tb_cap_extended_long extended_long;
	};
} __packed;

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

static int __tb_port_find_cap(struct tb_port *port, enum tb_port_cap cap)
{
	u32 offset = 1;

	do {
		struct tb_cap_any header;
		int ret;

		ret = tb_port_read(port, &header, TB_CFG_PORT, offset, 1);
		if (ret)
			return ret;

		if (header.basic.cap == cap)
			return offset;

		offset = header.basic.next;
	} while (offset);

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

static int tb_switch_find_cap(struct tb_switch *sw, enum tb_switch_cap cap)
{
	int offset = sw->config.first_cap_offset;

	while (offset > 0 && offset < CAP_OFFSET_MAX) {
		struct tb_cap_any header;
		int ret;

		ret = tb_sw_read(sw, &header, TB_CFG_SWITCH, offset, 1);
		if (ret)
			return ret;

		if (header.basic.cap == cap)
			return offset;

		offset = header.basic.next;
	}

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
	struct tb_cap_any header;
	int offset;

	offset = tb_switch_find_cap(sw, TB_SWITCH_CAP_VSE);
	if (offset < 0)
		return offset;

	while (offset > 0 && offset < VSE_CAP_OFFSET_MAX) {
		int ret;

		ret = tb_sw_read(sw, &header, TB_CFG_SWITCH, offset, 2);
		if (ret)
			return ret;

		/*
		 * Extended vendor specific capabilities come in two
		 * flavors: short and long. The latter is used when
		 * offset is over 0xff.
		 */
		if (offset >= CAP_OFFSET_MAX) {
			if (header.extended_long.vsec_id == vsec)
				return offset;
			offset = header.extended_long.next;
		} else {
			if (header.extended_short.vsec_id == vsec)
				return offset;
			if (!header.extended_short.length)
				return -ENOENT;
			offset = header.extended_short.next;
		}
	}

	return -ENOENT;
}
