/*
 * FireDTV driver (formerly known as FireSAT)
 *
 * Copyright (C) 2004 Andreas Monitzer <andy@monitzer.com>
 * Copyright (C) 2008 Henrik Kurelid <henrik@kurelid.se>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include <asm/byteorder.h>

#include <ieee1394.h>
#include <nodemgr.h>

#include "avc.h"
#include "cmp.h"
#include "firedtv.h"

#define CMP_OUTPUT_PLUG_CONTROL_REG_0	0xfffff0000904ULL

static int cmp_read(struct firedtv *fdtv, void *buf, u64 addr, size_t len)
{
	int ret;

	if (mutex_lock_interruptible(&fdtv->avc_mutex))
		return -EINTR;

	ret = hpsb_node_read(fdtv->ud->ne, addr, buf, len);
	if (ret < 0)
		dev_err(&fdtv->ud->device, "CMP: read I/O error\n");

	mutex_unlock(&fdtv->avc_mutex);
	return ret;
}

static int cmp_lock(struct firedtv *fdtv, void *data, u64 addr, __be32 arg,
		    int ext_tcode)
{
	int ret;

	if (mutex_lock_interruptible(&fdtv->avc_mutex))
		return -EINTR;

	ret = hpsb_node_lock(fdtv->ud->ne, addr, ext_tcode, data,
			     (__force quadlet_t)arg);
	if (ret < 0)
		dev_err(&fdtv->ud->device, "CMP: lock I/O error\n");

	mutex_unlock(&fdtv->avc_mutex);
	return ret;
}

static inline u32 get_opcr(__be32 opcr, u32 mask, u32 shift)
{
	return (be32_to_cpu(opcr) >> shift) & mask;
}

static inline void set_opcr(__be32 *opcr, u32 value, u32 mask, u32 shift)
{
	*opcr &= ~cpu_to_be32(mask << shift);
	*opcr |= cpu_to_be32((value & mask) << shift);
}

#define get_opcr_online(v)		get_opcr((v), 0x1, 31)
#define get_opcr_p2p_connections(v)	get_opcr((v), 0x3f, 24)
#define get_opcr_channel(v)		get_opcr((v), 0x3f, 16)

#define set_opcr_p2p_connections(p, v)	set_opcr((p), (v), 0x3f, 24)
#define set_opcr_channel(p, v)		set_opcr((p), (v), 0x3f, 16)
#define set_opcr_data_rate(p, v)	set_opcr((p), (v), 0x3, 14)
#define set_opcr_overhead_id(p, v)	set_opcr((p), (v), 0xf, 10)

int cmp_establish_pp_connection(struct firedtv *fdtv, int plug, int channel)
{
	__be32 old_opcr, opcr;
	u64 opcr_address = CMP_OUTPUT_PLUG_CONTROL_REG_0 + (plug << 2);
	int attempts = 0;
	int ret;

	ret = cmp_read(fdtv, &opcr, opcr_address, 4);
	if (ret < 0)
		return ret;

repeat:
	if (!get_opcr_online(opcr)) {
		dev_err(&fdtv->ud->device, "CMP: output offline\n");
		return -EBUSY;
	}

	old_opcr = opcr;

	if (get_opcr_p2p_connections(opcr)) {
		if (get_opcr_channel(opcr) != channel) {
			dev_err(&fdtv->ud->device,
				"CMP: cannot change channel\n");
			return -EBUSY;
		}
		dev_info(&fdtv->ud->device,
			 "CMP: overlaying existing connection\n");

		/* We don't allocate isochronous resources. */
	} else {
		set_opcr_channel(&opcr, channel);
		set_opcr_data_rate(&opcr, IEEE1394_SPEED_400);

		/* FIXME: this is for the worst case - optimize */
		set_opcr_overhead_id(&opcr, 0);

		/* FIXME: allocate isochronous channel and bandwidth at IRM */
	}

	set_opcr_p2p_connections(&opcr, get_opcr_p2p_connections(opcr) + 1);

	ret = cmp_lock(fdtv, &opcr, opcr_address, old_opcr, 2);
	if (ret < 0)
		return ret;

	if (old_opcr != opcr) {
		/*
		 * FIXME: if old_opcr.P2P_Connections > 0,
		 * deallocate isochronous channel and bandwidth at IRM
		 */

		if (++attempts < 6) /* arbitrary limit */
			goto repeat;
		return -EBUSY;
	}

	return 0;
}

void cmp_break_pp_connection(struct firedtv *fdtv, int plug, int channel)
{
	__be32 old_opcr, opcr;
	u64 opcr_address = CMP_OUTPUT_PLUG_CONTROL_REG_0 + (plug << 2);
	int attempts = 0;

	if (cmp_read(fdtv, &opcr, opcr_address, 4) < 0)
		return;

repeat:
	if (!get_opcr_online(opcr) || !get_opcr_p2p_connections(opcr) ||
	    get_opcr_channel(opcr) != channel) {
		dev_err(&fdtv->ud->device, "CMP: no connection to break\n");
		return;
	}

	old_opcr = opcr;
	set_opcr_p2p_connections(&opcr, get_opcr_p2p_connections(opcr) - 1);

	if (cmp_lock(fdtv, &opcr, opcr_address, old_opcr, 2) < 0)
		return;

	if (old_opcr != opcr) {
		/*
		 * FIXME: if old_opcr.P2P_Connections == 1, i.e. we were last
		 * owner, deallocate isochronous channel and bandwidth at IRM
		 */

		if (++attempts < 6) /* arbitrary limit */
			goto repeat;
	}
}
