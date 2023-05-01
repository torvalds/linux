// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * switchdev.c
 *
 *	Authors:
 *	Hans J. Schultz		<netdev@kapio-technology.com>
 *
 */

#include <net/switchdev.h>
#include "chip.h"
#include "global1.h"
#include "switchdev.h"

struct mv88e6xxx_fid_search_ctx {
	u16 fid_search;
	u16 vid_found;
};

static int __mv88e6xxx_find_vid(struct mv88e6xxx_chip *chip,
				const struct mv88e6xxx_vtu_entry *entry,
				void *priv)
{
	struct mv88e6xxx_fid_search_ctx *ctx = priv;

	if (ctx->fid_search == entry->fid) {
		ctx->vid_found = entry->vid;
		return 1;
	}

	return 0;
}

static int mv88e6xxx_find_vid(struct mv88e6xxx_chip *chip, u16 fid, u16 *vid)
{
	struct mv88e6xxx_fid_search_ctx ctx;
	int err;

	ctx.fid_search = fid;
	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_vtu_walk(chip, __mv88e6xxx_find_vid, &ctx);
	mv88e6xxx_reg_unlock(chip);
	if (err < 0)
		return err;
	if (err == 1)
		*vid = ctx.vid_found;
	else
		return -ENOENT;

	return 0;
}

int mv88e6xxx_handle_miss_violation(struct mv88e6xxx_chip *chip, int port,
				    struct mv88e6xxx_atu_entry *entry, u16 fid)
{
	struct switchdev_notifier_fdb_info info = {
		.addr = entry->mac,
		.locked = true,
	};
	struct net_device *brport;
	struct dsa_port *dp;
	u16 vid;
	int err;

	err = mv88e6xxx_find_vid(chip, fid, &vid);
	if (err)
		return err;

	info.vid = vid;
	dp = dsa_to_port(chip->ds, port);

	rtnl_lock();
	brport = dsa_port_to_bridge_port(dp);
	if (!brport) {
		rtnl_unlock();
		return -ENODEV;
	}
	err = call_switchdev_notifiers(SWITCHDEV_FDB_ADD_TO_BRIDGE,
				       brport, &info.info, NULL);
	rtnl_unlock();

	return err;
}
