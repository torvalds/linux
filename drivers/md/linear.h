#ifndef _LINEAR_H
#define _LINEAR_H

struct dev_info {
	mdk_rdev_t	*rdev;
	sector_t	num_sectors;
	sector_t	start_sector;
};

typedef struct dev_info dev_info_t;

struct linear_private_data
{
	struct linear_private_data *prev;	/* earlier version */
	dev_info_t		**hash_table;
	sector_t		spacing;
	sector_t		array_sectors;
	int			sector_shift;	/* shift before dividing
						 * by spacing
						 */
	dev_info_t		disks[0];
};


typedef struct linear_private_data linear_conf_t;

#define mddev_to_conf(mddev) ((linear_conf_t *) mddev->private)

#endif
