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

	if (!dev->zone_capacity)
		dev->zone_capacity = dev->zone_size;

	if (dev->zone_capacity > dev->zone_size) {
		pr_err("null_blk: zone capacity (%lu MB) larger than zone size (%lu MB)\n",
					dev->zone_capacity, dev->zone_size);
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

	/* Max active zones has to be < nbr of seq zones in order to be enforceable */
	if (dev->zone_max_active >= dev->nr_zones - dev->zone_nr_conv) {
		dev->zone_max_active = 0;
		pr_info("zone_max_active limit disabled, limit >= zone count\n");
	}

	/* Max open zones has to be <= max active zones */
	if (dev->zone_max_active && dev->zone_max_open > dev->zone_max_active) {
		dev->zone_max_open = dev->zone_max_active;
		pr_info("changed the maximum number of open zones to %u\n",
			dev->nr_zones);
	} else if (dev->zone_max_open >= dev->nr_zones - dev->zone_nr_conv) {
		dev->zone_max_open = 0;
		pr_info("zone_max_open limit disabled, limit >= zone count\n");
	}

	for (i = 0; i <  dev->zone_nr_conv; i++) {
		struct blk_zone *zone = &dev->zones[i];

		zone->start = sector;
		zone->len = dev->zone_size_sects;
		zone->capacity = zone->len;
		zone->wp = zone->start + zone->len;
		zone->type = BLK_ZONE_TYPE_CONVENTIONAL;
		zone->cond = BLK_ZONE_COND_NOT_WP;

		sector += dev->zone_size_sects;
	}

	for (i = dev->zone_nr_conv; i < dev->nr_zones; i++) {
		struct blk_zone *zone = &dev->zones[i];

		zone->start = zone->wp = sector;
		zone->len = dev->zone_size_sects;
		zone->capacity = dev->zone_capacity << ZONE_SIZE_SHIFT;
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
	blk_queue_max_open_zones(q, dev->zone_max_open);
	blk_queue_max_active_zones(q, dev->zone_max_active);

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

static blk_status_t null_close_zone(struct nullb_device *dev, struct blk_zone *zone)
{
	if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL)
		return BLK_STS_IOERR;

	switch (zone->cond) {
	case BLK_ZONE_COND_CLOSED:
		/* close operation on closed is not an error */
		return BLK_STS_OK;
	case BLK_ZONE_COND_IMP_OPEN:
		dev->nr_zones_imp_open--;
		break;
	case BLK_ZONE_COND_EXP_OPEN:
		dev->nr_zones_exp_open--;
		break;
	case BLK_ZONE_COND_EMPTY:
	case BLK_ZONE_COND_FULL:
	default:
		return BLK_STS_IOERR;
	}

	if (zone->wp == zone->start) {
		zone->cond = BLK_ZONE_COND_EMPTY;
	} else {
		zone->cond = BLK_ZONE_COND_CLOSED;
		dev->nr_zones_closed++;
	}

	return BLK_STS_OK;
}

static void null_close_first_imp_zone(struct nullb_device *dev)
{
	unsigned int i;

	for (i = dev->zone_nr_conv; i < dev->nr_zones; i++) {
		if (dev->zones[i].cond == BLK_ZONE_COND_IMP_OPEN) {
			null_close_zone(dev, &dev->zones[i]);
			return;
		}
	}
}

static bool null_can_set_active(struct nullb_device *dev)
{
	if (!dev->zone_max_active)
		return true;

	return dev->nr_zones_exp_open + dev->nr_zones_imp_open +
	       dev->nr_zones_closed < dev->zone_max_active;
}

static bool null_can_open(struct nullb_device *dev)
{
	if (!dev->zone_max_open)
		return true;

	if (dev->nr_zones_exp_open + dev->nr_zones_imp_open < dev->zone_max_open)
		return true;

	if (dev->nr_zones_imp_open && null_can_set_active(dev)) {
		null_close_first_imp_zone(dev);
		return true;
	}

	return false;
}

/*
 * This function matches the manage open zone resources function in the ZBC standard,
 * with the addition of max active zones support (added in the ZNS standard).
 *
 * The function determines if a zone can transition to implicit open or explicit open,
 * while maintaining the max open zone (and max active zone) limit(s). It may close an
 * implicit open zone in order to make additional zone resources available.
 *
 * ZBC states that an implicit open zone shall be closed only if there is not
 * room within the open limit. However, with the addition of an active limit,
 * it is not certain that closing an implicit open zone will allow a new zone
 * to be opened, since we might already be at the active limit capacity.
 */
static bool null_has_zone_resources(struct nullb_device *dev, struct blk_zone *zone)
{
	switch (zone->cond) {
	case BLK_ZONE_COND_EMPTY:
		if (!null_can_set_active(dev))
			return false;
		fallthrough;
	case BLK_ZONE_COND_CLOSED:
		return null_can_open(dev);
	default:
		/* Should never be called for other states */
		WARN_ON(1);
		return false;
	}
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
	case BLK_ZONE_COND_CLOSED:
		if (!null_has_zone_resources(dev, zone))
			return BLK_STS_IOERR;
		break;
	case BLK_ZONE_COND_IMP_OPEN:
	case BLK_ZONE_COND_EXP_OPEN:
		break;
	default:
		/* Invalid zone condition */
		return BLK_STS_IOERR;
	}

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

	if (zone->wp + nr_sectors > zone->start + zone->capacity)
		return BLK_STS_IOERR;

	if (zone->cond == BLK_ZONE_COND_CLOSED) {
		dev->nr_zones_closed--;
		dev->nr_zones_imp_open++;
	} else if (zone->cond == BLK_ZONE_COND_EMPTY) {
		dev->nr_zones_imp_open++;
	}
	if (zone->cond != BLK_ZONE_COND_EXP_OPEN)
		zone->cond = BLK_ZONE_COND_IMP_OPEN;

	ret = null_process_cmd(cmd, REQ_OP_WRITE, sector, nr_sectors);
	if (ret != BLK_STS_OK)
		return ret;

	zone->wp += nr_sectors;
	if (zone->wp == zone->start + zone->capacity) {
		if (zone->cond == BLK_ZONE_COND_EXP_OPEN)
			dev->nr_zones_exp_open--;
		else if (zone->cond == BLK_ZONE_COND_IMP_OPEN)
			dev->nr_zones_imp_open--;
		zone->cond = BLK_ZONE_COND_FULL;
	}
	return BLK_STS_OK;
}

static blk_status_t null_open_zone(struct nullb_device *dev, struct blk_zone *zone)
{
	if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL)
		return BLK_STS_IOERR;

	switch (zone->cond) {
	case BLK_ZONE_COND_EXP_OPEN:
		/* open operation on exp open is not an error */
		return BLK_STS_OK;
	case BLK_ZONE_COND_EMPTY:
		if (!null_has_zone_resources(dev, zone))
			return BLK_STS_IOERR;
		break;
	case BLK_ZONE_COND_IMP_OPEN:
		dev->nr_zones_imp_open--;
		break;
	case BLK_ZONE_COND_CLOSED:
		if (!null_has_zone_resources(dev, zone))
			return BLK_STS_IOERR;
		dev->nr_zones_closed--;
		break;
	case BLK_ZONE_COND_FULL:
	default:
		return BLK_STS_IOERR;
	}

	zone->cond = BLK_ZONE_COND_EXP_OPEN;
	dev->nr_zones_exp_open++;

	return BLK_STS_OK;
}

static blk_status_t null_finish_zone(struct nullb_device *dev, struct blk_zone *zone)
{
	if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL)
		return BLK_STS_IOERR;

	switch (zone->cond) {
	case BLK_ZONE_COND_FULL:
		/* finish operation on full is not an error */
		return BLK_STS_OK;
	case BLK_ZONE_COND_EMPTY:
		if (!null_has_zone_resources(dev, zone))
			return BLK_STS_IOERR;
		break;
	case BLK_ZONE_COND_IMP_OPEN:
		dev->nr_zones_imp_open--;
		break;
	case BLK_ZONE_COND_EXP_OPEN:
		dev->nr_zones_exp_open--;
		break;
	case BLK_ZONE_COND_CLOSED:
		if (!null_has_zone_resources(dev, zone))
			return BLK_STS_IOERR;
		dev->nr_zones_closed--;
		break;
	default:
		return BLK_STS_IOERR;
	}

	zone->cond = BLK_ZONE_COND_FULL;
	zone->wp = zone->start + zone->len;

	return BLK_STS_OK;
}

static blk_status_t null_reset_zone(struct nullb_device *dev, struct blk_zone *zone)
{
	if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL)
		return BLK_STS_IOERR;

	switch (zone->cond) {
	case BLK_ZONE_COND_EMPTY:
		/* reset operation on empty is not an error */
		return BLK_STS_OK;
	case BLK_ZONE_COND_IMP_OPEN:
		dev->nr_zones_imp_open--;
		break;
	case BLK_ZONE_COND_EXP_OPEN:
		dev->nr_zones_exp_open--;
		break;
	case BLK_ZONE_COND_CLOSED:
		dev->nr_zones_closed--;
		break;
	case BLK_ZONE_COND_FULL:
		break;
	default:
		return BLK_STS_IOERR;
	}

	zone->cond = BLK_ZONE_COND_EMPTY;
	zone->wp = zone->start;

	return BLK_STS_OK;
}

static blk_status_t null_zone_mgmt(struct nullb_cmd *cmd, enum req_opf op,
				   sector_t sector)
{
	struct nullb_device *dev = cmd->nq->dev;
	unsigned int zone_no = null_zone_no(dev, sector);
	struct blk_zone *zone = &dev->zones[zone_no];
	blk_status_t ret = BLK_STS_OK;
	size_t i;

	switch (op) {
	case REQ_OP_ZONE_RESET_ALL:
		for (i = dev->zone_nr_conv; i < dev->nr_zones; i++)
			null_reset_zone(dev, &dev->zones[i]);
		break;
	case REQ_OP_ZONE_RESET:
		ret = null_reset_zone(dev, zone);
		break;
	case REQ_OP_ZONE_OPEN:
		ret = null_open_zone(dev, zone);
		break;
	case REQ_OP_ZONE_CLOSE:
		ret = null_close_zone(dev, zone);
		break;
	case REQ_OP_ZONE_FINISH:
		ret = null_finish_zone(dev, zone);
		break;
	default:
		return BLK_STS_NOTSUPP;
	}

	if (ret == BLK_STS_OK)
		trace_nullb_zone_op(cmd, zone_no, zone->cond);

	return ret;
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
