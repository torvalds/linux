

#ifndef _MD_CLUSTER_H
#define _MD_CLUSTER_H

#include "md.h"

struct mddev;
struct md_rdev;

struct md_cluster_operations {
	int (*join)(struct mddev *mddev, int nodes);
	int (*leave)(struct mddev *mddev);
	int (*slot_number)(struct mddev *mddev);
	void (*resync_info_update)(struct mddev *mddev, sector_t lo, sector_t hi);
	int (*resync_start)(struct mddev *mddev, sector_t lo, sector_t hi);
	void (*resync_finish)(struct mddev *mddev);
	int (*metadata_update_start)(struct mddev *mddev);
	int (*metadata_update_finish)(struct mddev *mddev);
	int (*metadata_update_cancel)(struct mddev *mddev);
	int (*area_resyncing)(struct mddev *mddev, sector_t lo, sector_t hi);
	int (*add_new_disk_start)(struct mddev *mddev, struct md_rdev *rdev);
	int (*add_new_disk_finish)(struct mddev *mddev);
	int (*new_disk_ack)(struct mddev *mddev, bool ack);
	int (*remove_disk)(struct mddev *mddev, struct md_rdev *rdev);
	int (*gather_bitmaps)(struct md_rdev *rdev);
};

#endif /* _MD_CLUSTER_H */
