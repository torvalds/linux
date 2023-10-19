/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MULTIPATH_H
#define _MULTIPATH_H

struct multipath_info {
	struct md_rdev	*rdev;
};

struct mpconf {
	struct mddev			*mddev;
	struct multipath_info	*multipaths;
	int			raid_disks;
	spinlock_t		device_lock;
	struct list_head	retry_list;

	mempool_t		pool;
};

/*
 * this is our 'private' 'collective' MULTIPATH buffer head.
 * it contains information about what kind of IO operations were started
 * for this MULTIPATH operation, and about their status:
 */

struct multipath_bh {
	struct mddev			*mddev;
	struct bio		*master_bio;
	struct bio		bio;
	int			path;
	struct list_head	retry_list;
};
#endif
