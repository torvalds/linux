// SPDX-License-Identifier: GPL-2.0
#include <linux/vmalloc.h>
#include "null_blk.h"

#define CREATE_TRACE_POINTS
#include "null_blk_trace.h"

/* zone_size in MBs to sectors. */
#define ZONE_SIZE_SHIFT		11

static inline unsigned int null_zone_no(struct nullb_device *dev, sector_t sect)
{
	return sect >> ilog2(dev->zone_size_sects);
}

int null_init_zoned_dev(struct nullb_device *dev, struct request_queue *q)
{
	sector_t dev_size = (sector_t)dev->size * 1024 * 1024;
	sector_t sector = 0;
	unsigned int i;

	if (!is_power_of_2(dev->zone_size)) {
		pr_err("zone_size must be power-of-two\n");
		return -EINVAL;
	}
	if (dev->zone_size > dev->size) {
		pr_err("Zone size larger than device capacity\n");
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

	q->limits.zoned = BLK_ZONED_HM;
	blk_queue_flag_set(QUEUE_FLAG_ZONE_RESETALL, q);
	blk_queue_required_elevator_features(q, ELEVATOR_F_ZBD_SEQ_WRITE);

	return 0;
}

int null_register_zoned_dev(struct nullb *nullb)
{
	struct nullb_device *dev = nullb->dev;
	struct request_queue *q = nullb->q;

	if (queue_is_mq(q)) {
		int ret = blk_revalidate_disk_zones(nullb->disk, NULL);

		if (ret)
			return ret;
	} else {
		blk_queue_chunk_sectors(q, dev->zone_size_sects);
		q->nr_zones = blkdev_nr_zones(nullb->disk);
	}

	blk_queue_max_zone_append_sectors(q, dev->zone_size_sects);

	return 0;
}

void null_free_zoned_dev(struct nullb_device *dev)
{
	kvfree(dev->zones);
}

int null_report_zones(struct gendisk *disk, sector_t sector,
		unsigned int nr_zones, report_zones_cb cb, void *data)
{
	struct nullb *nullb = disk->private_data;
	struct nullb_device *dev = nullb->dev;
	unsigned int first_zone, i;
	struct blk_zone zone;
	int error;

	first_zone = null_zone_no(dev, sector);
	if (first_zone >= dev->nr_zones)
		return 0;

	nr_zones = min(nr_zones, dev->nr_zones - first_zone);
	trace_nullb_report_zones(nullb, nr_zones);

	for (i = 0; i < nr_zones; i++) {
		/*
		 * Stacked DM target drivers will remap the zone information by
		 * modifying the zone information passed to the report callback.
		 * So use a local copy to avoid corruption of the device zone
		 * array.
		 */
		memcpy(&zone, &dev->zones[first_zone + i],
		       sizeof(struct blk_zone));
		error = cb(&zone, i, data);
		if (error)
			return error;
	}

	return nr_zones;
}

size_t null_zone_valid_read_len(struct nullb *nullb,
				sector_t sector, unsigned int len)
{
	struct nullb_device *dev = nullb->dev;
	struct blk_zone *zone = &dev->zones[null_zone_no(dev, sector)];
	unsigned int nr_sectors = len >> SECTOR_SHIFT;

	/* Read must be below the write pointer position */
	if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL ||
	    sector + nr_sectors <= zone->wp)
		return len;

	if (sector > zone->wp)
		return 0;

	return (zone->wp - sector) << SECTOR_SHIFT;
}

static blk_status_t null_zone_write(struct nullb_cmd *cmd, sector_t sector,
				    unsigned int nr_sectors, bool append)
{
	struct nullb_device *dev = cmd->nq->dev;
	unsigned int zno = null_zone_no(dev, sector);
	struct blk_zone *zone = &dev->zones[zno];
	blk_status_t ret;

	trace_nullb_zone_op(cmd, zno, zone->cond);

	if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL)
		return null_process_cmd(cmd, REQ_OP_WRITE, sector, nr_sectors);

	switch (zone->cond) {
	case BLK_ZONE_COND_FULL:
		/* Cannot write to a full zone */
		return BLK_STS_IOERR;
	case BLK_ZONE_COND_EMPTY:
	case BLK_ZONE_COND_IMP_OPEN:
	case BLK_ZONE_COND_EXP_OPEN:
	case BLK_ZONE_COND_CLOSED:
		/*
		 * Regular writes must be at the write pointer position.
		 * Zone append writes are automatically issued at the write
		 * pointer and the position returned using the request or BIO
		 * sector.
		 */
		if (append) {
			sector = zone->wp;
			if (cmd->bio)
				cmd->bio->bi_iter.bi_sector = sector;
			else
				cmd->rq->__sector = sector;
		} else if (sector != zone->wp) {
			return BLK_STS_IOERR;
		}

		if (zone->cond != BLK_ZONE_COND_EXP_OPEN)
			zone->cond = BLK_ZONE_COND_IMP_OPEN;

		ret = null_process_cmd(cmd, REQ_OP_WRITE, sector, nr_sectors);
		if (ret != BLK_STS_OK)
			return ret;

		zone->wp += nr_sectors;
		if (zone->wp == zone->start + zone->len)
			zone->cond = BLK_ZONE_COND_FULL;
		return BLK_STS_OK;
	default:
		/* Invalid zone condition */
		return BLK_STS_IOERR;
	}
}

static blk_status_t null_zone_mgmt(struct nullb_cmd *cmd, enum req_opf op,
				   sector_t sector)
{
	struct nullb_device *dev = cmd->nq->dev;
	unsigned int zone_no = null_zone_no(dev, sector);
	struct blk_zone *zone = &dev->zones[zone_no];
	size_t i;

	switch (op) {
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
	case REQ_OP_ZONE_OPEN:
		if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL)
			return BLK_STS_IOERR;
		if (zone->cond == BLK_ZONE_COND_FULL)
			return BLK_STS_IOERR;

		zone->cond = BLK_ZONE_COND_EXP_OPEN;
		break;
	case REQ_OP_ZONE_CLOSE:
		if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL)
			return BLK_STS_IOERR;
		if (zone->cond == BLK_ZONE_COND_FULL)
			return BLK_STS_IOERR;

		if (zone->wp == zone->start)
			zone->cond = BLK_ZONE_COND_EMPTY;
		else
			zone->cond = BLK_ZONE_COND_CLOSED;
		break;
	case REQ_OP_ZONE_FINISH:
		if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL)
			return BLK_STS_IOERR;

		zone->cond = BLK_ZONE_COND_FULL;
		zone->wp = zone->start + zone->len;
		break;
	default:
		return BLK_STS_NOTSUPP;
	}

	trace_nullb_zone_op(cmd, zone_no, zone->cond);
	return BLK_STS_OK;
}

blk_status_t null_process_zoned_cmd(struct nullb_cmd *cmd, enum req_opf op,
				    sector_t sector, sector_t nr_sectors)
{
	switch (op) {
	case REQ_OP_WRITE:
		return null_zone_write(cmd, sector, nr_sectors, false);
	case REQ_OP_ZONE_APPEND:
		return null_zone_write(cmd, sector, nr_sectors, true);
	case REQ_OP_ZONE_RESET:
	case REQ_OP_ZONE_RESET_ALL:
	case REQ_OP_ZONE_OPEN:
	case REQ_OP_ZONE_CLOSE:
	case REQ_OP_ZONE_FINISH:
		return null_zone_mgmt(cmd, op, sector);
	default:
		return null_process_cmd(cmd, op, sector, nr_sectors);
	}
}
