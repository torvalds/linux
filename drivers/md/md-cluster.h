

#ifndef _MD_CLUSTER_H
#define _MD_CLUSTER_H

#include "md.h"

struct mddev;

struct md_cluster_operations {
	int (*join)(struct mddev *mddev, int nodes);
	int (*leave)(struct mddev *mddev);
	int (*slot_number)(struct mddev *mddev);
	void (*resync_info_update)(struct mddev *mddev, sector_t lo, sector_t hi);
};

#endif /* _MD_CLUSTER_H */
