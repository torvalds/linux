/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINEAR_H
#define _LINEAR_H

struct dev_info {
	struct md_rdev	*rdev;
	sector_t	end_sector;
};

struct linear_conf
{
	struct rcu_head		rcu;
	sector_t		array_sectors;
	int			raid_disks; /* a copy of mddev->raid_disks */
	struct dev_info		disks[];
};
#endif
