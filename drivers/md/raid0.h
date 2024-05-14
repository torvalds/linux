/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RAID0_H
#define _RAID0_H

struct strip_zone {
	sector_t zone_end;	/* Start of the next zone (in sectors) */
	sector_t dev_start;	/* Zone offset in real dev (in sectors) */
	int	 nb_dev;	/* # of devices attached to the zone */
	int	 disk_shift;	/* start disk for the original layout */
};

/* Linux 3.14 (20d0189b101) made an unintended change to
 * the RAID0 layout for multi-zone arrays (where devices aren't all
 * the same size.
 * RAID0_ORIG_LAYOUT restores the original layout
 * RAID0_ALT_MULTIZONE_LAYOUT uses the altered layout
 * The layouts are identical when there is only one zone (all
 * devices the same size).
 */

enum r0layout {
	RAID0_ORIG_LAYOUT = 1,
	RAID0_ALT_MULTIZONE_LAYOUT = 2,
};
struct r0conf {
	struct strip_zone	*strip_zone;
	struct md_rdev		**devlist; /* lists of rdevs, pointed to
					    * by strip_zone->dev */
	int			nr_strip_zones;
	enum r0layout		layout;
};

#endif
