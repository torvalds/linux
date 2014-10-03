/*
 * Thunderbolt Cactus Ridge driver - capabilities lookup
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#include <linux/slab.h>
#include <linux/errno.h>

#include "tb.h"


struct tb_cap_any {
	union {
		struct tb_cap_basic basic;
		struct tb_cap_extended_short extended_short;
		struct tb_cap_extended_long extended_long;
	};
} __packed;

static bool tb_cap_is_basic(struct tb_cap_any *cap)
{
	/* basic.cap is u8. This checks only the lower 8 bit of cap. */
	return cap->basic.cap != 5;
}

static bool tb_cap_is_long(struct tb_cap_any *cap)
{
	return !tb_cap_is_basic(cap)
	       && cap->extended_short.next == 0
	       && cap->extended_short.length == 0;
}

static enum tb_cap tb_cap(struct tb_cap_any *cap)
{
	if (tb_cap_is_basic(cap))
		return cap->basic.cap;
	else
		/* extended_short/long have cap at the same offset. */
		return cap->extended_short.cap;
}

static u32 tb_cap_next(struct tb_cap_any *cap, u32 offset)
{
	int next;
	if (offset == 1) {
		/*
		 * The first pointer is part of the switch header and always
		 * a simple pointer.
		 */
		next = cap->basic.next;
	} else {
		/*
		 * Somehow Intel decided to use 3 different types of capability
		 * headers. It is not like anyone could have predicted that
		 * single byte offsets are not enough...
		 */
		if (tb_cap_is_basic(cap))
			next = cap->basic.next;
		else if (!tb_cap_is_long(cap))
			next = cap->extended_short.next;
		else
			next = cap->extended_long.next;
	}
	/*
	 * "Hey, we could terminate some capability lists with a null offset
	 *  and others with a pointer to the last element." - "Great idea!"
	 */
	if (next == offset)
		return 0;
	return next;
}

/**
 * tb_find_cap() - find a capability
 *
 * Return: Returns a positive offset if the capability was found and 0 if not.
 * Returns an error code on failure.
 */
int tb_find_cap(struct tb_port *port, enum tb_cfg_space space, enum tb_cap cap)
{
	u32 offset = 1;
	struct tb_cap_any header;
	int res;
	int retries = 10;
	while (retries--) {
		res = tb_port_read(port, &header, space, offset, 1);
		if (res) {
			/* Intel needs some help with linked lists. */
			if (space == TB_CFG_PORT && offset == 0xa
			    && port->config.type == TB_TYPE_DP_HDMI_OUT) {
				offset = 0x39;
				continue;
			}
			return res;
		}
		if (offset != 1) {
			if (tb_cap(&header) == cap)
				return offset;
			if (tb_cap_is_long(&header)) {
				/* tb_cap_extended_long is 2 dwords */
				res = tb_port_read(port, &header, space,
						   offset, 2);
				if (res)
					return res;
			}
		}
		offset = tb_cap_next(&header, offset);
		if (!offset)
			return 0;
	}
	tb_port_WARN(port,
		     "run out of retries while looking for cap %#x in config space %d, last offset: %#x\n",
		     cap, space, offset);
	return -EIO;
}
