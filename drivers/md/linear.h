#ifndef _LINEAR_H
#define _LINEAR_H

struct dev_info {
	struct md_rdev	*rdev;
	sector_t	end_sector;
};

struct linear_private_data
{
	struct rcu_head		rcu;
	sector_t		array_sectors;
	struct dev_info		disks[0];
};


typedef struct linear_private_data linear_conf_t;

#endif
