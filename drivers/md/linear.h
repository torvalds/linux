#ifndef _LINEAR_H
#define _LINEAR_H

struct dev_info {
	mdk_rdev_t	*rdev;
	sector_t	end_sector;
};

typedef struct dev_info dev_info_t;

struct linear_private_data
{
	sector_t		array_sectors;
	dev_info_t		disks[0];
	struct rcu_head		rcu;
};


typedef struct linear_private_data linear_conf_t;

#endif
