/* SPDX-License-Identifier: GPL-2.0 */


#ifndef _MD_CLUSTER_H
#define _MD_CLUSTER_H

#include "md.h"

struct mddev;
struct md_rdev;

struct md_cluster_operations {
	int (*join)(struct mddev *mddev, int nodes);
	int (*leave)(struct mddev *mddev);
	int (*slot_number)(struct mddev *mddev);
	int (*resync_info_update)(struct mddev *mddev, sector_t lo, sector_t hi);
	int (*metadata_update_start)(struct mddev *mddev);
	int (*metadata_update_finish)(struct mddev *mddev);
	void (*metadata_update_cancel)(struct mddev *mddev);
	int (*resync_start)(struct mddev *mddev);
	int (*resync_finish)(struct mddev *mddev);
	int (*area_resyncing)(struct mddev *mddev, int direction, sector_t lo, sector_t hi);
	int (*add_new_disk)(struct mddev *mddev, struct md_rdev *rdev);
	void (*add_new_disk_cancel)(struct mddev *mddev);
	int (*new_disk_ack)(struct mddev *mddev, bool ack);
	int (*remove_disk)(struct mddev *mddev, struct md_rdev *rdev);
	void (*load_bitmaps)(struct mddev *mddev, int total_slots);
	int (*gather_bitmaps)(struct md_rdev *rdev);
	int (*lock_all_bitmaps)(struct mddev *mddev);
	void (*unlock_all_bitmaps)(struct mddev *mddev);
	void (*update_size)(struct mddev *mddev, sector_t old_dev_sectors);
};

#endif /* _MD_CLUSTER_H */
