#ifndef _RAID0_H
#define _RAID0_H

struct strip_zone
{
	sector_t zone_end;	/* Start of the next zone (in sectors) */
	sector_t dev_start;	/* Zone offset in real dev (in sectors) */
	int nb_dev;		/* # of devices attached to the zone */
};

struct raid0_private_data
{
	struct strip_zone *strip_zone;
	mdk_rdev_t **devlist; /* lists of rdevs, pointed to by strip_zone->dev */
	int nr_strip_zones;
};

typedef struct raid0_private_data raid0_conf_t;

#endif
