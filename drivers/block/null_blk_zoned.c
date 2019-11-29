// SPDX-License-Identifier: GPL-2.0
#include <linux/vmalloc.h>
#include "null_blk.h"

/* zone_size in MBs to sectors. */
#define ZONE_SIZE_SHIFT		11

static inline unsigned int null_zone_no(struct nullb_device *dev, sector_t sect)
{
	return sect >> ilog2(dev->zone_size_sects);
}

int null_zone_init(struct nullb_device *dev)
{
	sector_t dev_size = (sector_t)dev->size * 1024 * 1024;
	sector_t sector = 0;
	unsigned int i;

	if (!is_power_of_2(dev->zone_size)) {
		pr_err("zone_size must be power-of-two\n");
		return -EINVAL;
	}

	dev->zone_size_sects = dev->zone_size << ZONE_SIZE_SHIFT;
	dev->nr_zones = dev_size >>
				(SECTOR_SHIFT + ilog2(dev->zone_size_sects));
	dev->zones = kvmalloc_array(dev->nr_zones, sizeof(struct blk_zone),
			GFP_KERNEL | __GFP_ZERO);
	if (!dev->zones)
		return -ENOMEM;

	if (dev->zone_nr_conv >= dev->nr_zones) {
		dev->zone_nr_conv = dev->nr_zones - 1;
		pr_info("changed the number of conventional zones to %u",
			dev->zone_nr_conv);
	}

	for (i = 0; i <  dev->zone_nr_conv; i++) {
		struct blk_zone *zone = &dev->zones[i];

		zone->start = sector;
		zone->len = dev->zone_size_sects;
		zone->wp = zone->start + zone->len;
		zone->type = BLK_ZONE_TYPE_CONVENTIONAL;
		zone->cond = BLK_ZONE_COND_NOT_WP;

		sector += dev->zone_size_sects;
	}

	for (i = dev->zone_nr_conv; i < dev->nr_zones; i++) {
		struct blk_zone *zone = &dev->zones[i];

		zone->start = zone->wp = sector;
		zone->len = dev->zone_size_sects;
		zone->type = BLK_ZONE_TYPE_SEQWRITE_REQ;
		zone->cond = BLK_ZONE_COND_EMPTY;

		sector += dev->zone_size_sects;
	}

	return 0;
}

void null_zone_exit(struct nullb_device *dev)
{
	kvfree(dev->zones);
}

int null_zone_report(struct gendisk *disk, sector_t sector,
		     struct blk_zone *zones, unsigned int *nr_zones)
{
	struct nullb *nullb = disk->private_data;
	struct nullb_device *dev = nullb->dev;
	unsigned int zno, nrz = 0;

	zno = null_zone_no(dev, sector);
	if (zno < dev->nr_zones) {
		nrz = min_t(unsigned int, *nr_zones, dev->nr_zones - zno);
		memcpy(zones, &dev->zones[zno], nrz * sizeof(struct blk_zone));
	}

	*nr_zones = nrz;

	return 0;
}

static blk_status_t null_zone_write(struct nullb_cmd *cmd, sector_t sector,
		     unsigned int nr_sectors)
{
	struct nullb_device *dev = cmd->nq->dev;
	unsigned int zno = null_zone_no(dev, sector);
	struct blk_zone *zone = &dev->zones[zno];

	switch (zone->cond) {
	case BLK_ZONE_COND_FULL:
		/* Cannot write to a full zone */
		cmd->error = BLK_STS_IOERR;
		return BLK_STS_IOERR;
	case BLK_ZONE_COND_EMPTY:
	case BLK_ZONE_COND_IMP_OPEN:
		/* Writes must be at the write pointer position */
		if (sector != zone->wp)
			return BLK_STS_IOERR;

		if (zone->cond == BLK_ZONE_COND_EMPTY)
			zone->cond = BLK_ZONE_COND_IMP_OPEN;

		zone->wp += nr_sectors;
		if (zone->wp == zone->start + zone->len)
			zone->cond = BLK_ZONE_COND_FULL;
		break;
	case BLK_ZONE_COND_NOT_WP:
		break;
	default:
		/* Invalid zone condition */
		return BLK_STS_IOERR;
	}
	return BLK_STS_OK;
}

static blk_status_t null_zone_reset(struct nullb_cmd *cmd, sector_t sector)
{
	struct nullb_device *dev = cmd->nq->dev;
	unsigned int zno = null_zone_no(dev, sector);
	struct blk_zone *zone = &dev->zones[zno];
	size_t i;

	switch (req_op(cmd->rq)) {
	case REQ_OP_ZONE_RESET_ALL:
		for (i = 0; i < dev->nr_zones; i++) {
			if (zone[i].type == BLK_ZONE_TYPE_CONVENTIONAL)
				continue;
			zone[i].cond = BLK_ZONE_COND_EMPTY;
			zone[i].wp = zone[i].start;
		}
		break;
	case REQ_OP_ZONE_RESET:
		if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL)
			return BLK_STS_IOERR;

		zone->cond = BLK_ZONE_COND_EMPTY;
		zone->wp = zone->start;
		break;
	default:
		return BLK_STS_NOTSUPP;
	}
	return BLK_STS_OK;
}

blk_status_t null_handle_zoned(struct nullb_cmd *cmd, enum req_opf op,
			       sector_t sector, sector_t nr_sectors)
{
	switch (op) {
	case REQ_OP_WRITE:
		return null_zone_write(cmd, sector, nr_sectors);
	case REQ_OP_ZONE_RESET:
	case REQ_OP_ZONE_RESET_ALL:
		return null_zone_reset(cmd, sector);
	default:
		return BLK_STS_OK;
	}
}
