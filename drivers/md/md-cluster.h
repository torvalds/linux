

#ifndef _MD_CLUSTER_H
#define _MD_CLUSTER_H

#include "md.h"

struct mddev;

struct md_cluster_operations {
	int (*join)(struct mddev *mddev);
	int (*leave)(struct mddev *mddev);
};

#endif /* _MD_CLUSTER_H */
