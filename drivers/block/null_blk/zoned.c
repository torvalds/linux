// SPDX-License-Identifier: GPL-2.0
#include <linux/vmalloc.h>
#include <linux/bitmap.h>
#include "null_blk.h"

#define CREATE_TRACE_POINTS
#include "trace.h"

#undef pr_fmt
#define pr_fmt(fmt)	"null_blk: " fmt

#define NULL_ZONE_INVALID_WP	((sector_t)-1)

static inline sector_t mb_to_sects(unsigned long mb)
{
	return ((sector_t)mb * SZ_1M) >> SECTOR_SHIFT;
}

static inline unsigned int null_zone_no(struct nullb_device *dev, sector_t sect)
{
	return sect >> ilog2(dev->zone_size_sects);
}

static inline void null_init_zone_lock(struct nullb_device *dev,
				       struct nullb_zone *zone)
{
	if (!dev->memory_backed)
		spin_lock_init(&zone->spinlock);
	else
		mutex_init(&zone->mutex);
}

static inline void null_lock_zone(struct nullb_device *dev,
				  struct nullb_zone *zone)
{
	if (!dev->memory_backed)
		spin_lock_irq(&zone->spinlock);
	else
		mutex_lock(&zone->mutex);
}

static inline void null_unlock_zone(struct nullb_device *dev,
				    struct nullb_zone *zone)
{
	if (!dev->memory_backed)
		spin_unlock_irq(&zone->spinlock);
	else
		mutex_unlock(&zone->mutex);
}

int null_init_zoned_dev(struct nullb_device *dev,
			struct queue_limits *lim)
{
	sector_t dev_capacity_sects, zone_capacity_sects;
	struct nullb_zone *zone;
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
		pr_err("zone capacity (%lu MB) larger than zone size (%lu MB)\n",
		       dev->zone_capacity, dev->zone_size);
		return -EINVAL;
	}

	/*
	 * If a smaller zone capacity was requested, do not allow a smaller last
	 * zone at the same time as such zone configuration does not correspond
	 * to any real zoned device.
	 */
	if (dev->zone_capacity != dev->zone_size &&
	    dev->size & (dev->zone_size - 1)) {
		pr_err("A smaller last zone is not allowed with zone capacity smaller than zone size.\n");
		return -EINVAL;
	}

	zone_capacity_sects = mb_to_sects(dev->zone_capacity);
	dev_capacity_sects = mb_to_sects(dev->size);
	dev->zone_size_sects = mb_to_sects(dev->zone_size);
	dev->nr_zones = round_up(dev_capacity_sects, dev->zone_size_sects)
		>> ilog2(dev->zone_size_sects);

	dev->zones = kvmalloc_array(dev->nr_zones, sizeof(struct nullb_zone),
				    GFP_KERNEL | __GFP_ZERO);
	if (!dev->zones)
		return -ENOMEM;

	spin_lock_init(&dev->zone_res_lock);

	if (dev->zone_nr_conv >= dev->nr_zones) {
		dev->zone_nr_conv = dev->nr_zones - 1;
		pr_info("changed the number of conventional zones to %u",
			dev->zone_nr_conv);
	}

	dev->zone_append_max_sectors =
		min(ALIGN_DOWN(dev->zone_append_max_sectors,
			       dev->blocksize >> SECTOR_SHIFT),
		    zone_capacity_sects);

	/* Max active zones has to be < nbr of seq zones in order to be enforceable */
	if (dev->zone_max_active >= dev->nr_zones - dev->zone_nr_conv) {
		dev->zone_max_active = 0;
		pr_info("zone_max_active limit disabled, limit >= zone count\n");
	}

	/* Max open zones has to be <= max active zones */
	if (dev->zone_max_active && dev->zone_max_open > dev->zone_max_active) {
		dev->zone_max_open = dev->zone_max_active;
		pr_info("changed the maximum number of open zones to %u\n",
			dev->zone_max_open);
	} else if (dev->zone_max_open >= dev->nr_zones - dev->zone_nr_conv) {
		dev->zone_max_open = 0;
		pr_info("zone_max_open limit disabled, limit >= zone count\n");
	}
	dev->need_zone_res_mgmt = dev->zone_max_active || dev->zone_max_open;
	dev->imp_close_zone_no = dev->zone_nr_conv;

	for (i = 0; i <  dev->zone_nr_conv; i++) {
		zone = &dev->zones[i];

		null_init_zone_lock(dev, zone);
		zone->start = sector;
		zone->len = dev->zone_size_sects;
		zone->capacity = zone->len;
		zone->wp = zone->start + zone->len;
		zone->type = BLK_ZONE_TYPE_CONVENTIONAL;
		zone->cond = BLK_ZONE_COND_NOT_WP;

		sector += dev->zone_size_sects;
	}

	for (i = dev->zone_nr_conv; i < dev->nr_zones; i++) {
		zone = &dev->zones[i];

		null_init_zone_lock(dev, zone);
		zone->start = zone->wp = sector;
		if (zone->start + dev->zone_size_sects > dev_capacity_sects)
			zone->len = dev_capacity_sects - zone->start;
		else
			zone->len = dev->zone_size_sects;
		zone->capacity =
			min_t(sector_t, zone->len, zone_capacity_sects);
		zone->type = BLK_ZONE_TYPE_SEQWRITE_REQ;
		zone->cond = BLK_ZONE_COND_EMPTY;

		sector += dev->zone_size_sects;
	}

	lim->zoned = true;
	lim->chunk_sectors = dev->zone_size_sects;
	lim->max_zone_append_sectors = dev->zone_append_max_sectors;
	lim->max_open_zones = dev->zone_max_open;
	lim->max_active_zones = dev->zone_max_active;
	return 0;
}

int null_register_zoned_dev(struct nullb *nullb)
{
	struct request_queue *q = nullb->q;
	struct gendisk *disk = nullb->disk;

	blk_queue_flag_set(QUEUE_FLAG_ZONE_RESETALL, q);
	disk->nr_zones = bdev_nr_zones(disk->part0);

	pr_info("%s: using %s zone append\n",
		disk->disk_name,
		queue_emulates_zone_append(q) ? "emulated" : "native");

	return blk_revalidate_disk_zones(disk);
}

void null_free_zoned_dev(struct nullb_device *dev)
{
	kvfree(dev->zones);
	dev->zones = NULL;
}

int null_report_zones(struct gendisk *disk, sector_t sector,
		unsigned int nr_zones, report_zones_cb cb, void *data)
{
	struct nullb *nullb = disk->private_data;
	struct nullb_device *dev = nullb->dev;
	unsigned int first_zone, i;
	struct nullb_zone *zone;
	struct blk_zone blkz;
	int error;

	first_zone = null_zone_no(dev, sector);
	if (first_zone >= dev->nr_zones)
		return 0;

	nr_zones = min(nr_zones, dev->nr_zones - first_zone);
	trace_nullb_report_zones(nullb, nr_zones);

	memset(&blkz, 0, sizeof(struct blk_zone));
	zone = &dev->zones[first_zone];
	for (i = 0; i < nr_zones; i++, zone++) {
		/*
		 * Stacked DM target drivers will remap the zone information by
		 * modifying the zone information passed to the report callback.
		 * So use a local copy to avoid corruption of the device zone
		 * array.
		 */
		null_lock_zone(dev, zone);
		blkz.start = zone->start;
		blkz.len = zone->len;
		blkz.wp = zone->wp;
		blkz.type = zone->type;
		blkz.cond = zone->cond;
		blkz.capacity = zone->capacity;
		null_unlock_zone(dev, zone);

		error = cb(&blkz, i, data);
		if (error)
			return error;
	}

	return nr_zones;
}

/*
 * This is called in the case of memory backing from null_process_cmd()
 * with the target zone already locked.
 */
size_t null_zone_valid_read_len(struct nullb *nullb,
				sector_t sector, unsigned int len)
{
	struct nullb_device *dev = nullb->dev;
	struct nullb_zone *zone = &dev->zones[null_zone_no(dev, sector)];
	unsigned int nr_sectors = len >> SECTOR_SHIFT;

	/* Read must be below the write pointer position */
	if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL ||
	    sector + nr_sectors <= zone->wp)
		return len;

	if (sector > zone->wp)
		return 0;

	return (zone->wp - sector) << SECTOR_SHIFT;
}

static void null_close_imp_open_zone(struct nullb_device *dev)
{
	struct nullb_zone *zone;
	unsigned int zno, i;

	zno = dev->imp_close_zone_no;
	if (zno >= dev->nr_zones)
		zno = dev->zone_nr_conv;

	for (i = dev->zone_nr_conv; i < dev->nr_zones; i++) {
		zone = &dev->zones[zno];
		zno++;
		if (zno >= dev->nr_zones)
			zno = dev->zone_nr_conv;

		if (zone->cond == BLK_ZONE_COND_IMP_OPEN) {
			dev->nr_zones_imp_open--;
			if (zone->wp == zone->start) {
				zone->cond = BLK_ZONE_COND_EMPTY;
			} else {
				zone->cond = BLK_ZONE_COND_CLOSED;
				dev->nr_zones_closed++;
			}
			dev->imp_close_zone_no = zno;
			return;
		}
	}
}

static blk_status_t null_check_active(struct nullb_device *dev)
{
	if (!dev->zone_max_active)
		return BLK_STS_OK;

	if (dev->nr_zones_exp_open + dev->nr_zones_imp_open +
			dev->nr_zones_closed < dev->zone_max_active)
		return BLK_STS_OK;

	return BLK_STS_ZONE_ACTIVE_RESOURCE;
}

static blk_status_t null_check_open(struct nullb_device *dev)
{
	if (!dev->zone_max_open)
		return BLK_STS_OK;

	if (dev->nr_zones_exp_open + dev->nr_zones_imp_open < dev->zone_max_open)
		return BLK_STS_OK;

	if (dev->nr_zones_imp_open) {
		if (null_check_active(dev) == BLK_STS_OK) {
			null_close_imp_open_zone(dev);
			return BLK_STS_OK;
		}
	}

	return BLK_STS_ZONE_OPEN_RESOURCE;
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
static blk_status_t null_check_zone_resources(struct nullb_device *dev,
					      struct nullb_zone *zone)
{
	blk_status_t ret;

	switch (zone->cond) {
	case BLK_ZONE_COND_EMPTY:
		ret = null_check_active(dev);
		if (ret != BLK_STS_OK)
			return ret;
		fallthrough;
	case BLK_ZONE_COND_CLOSED:
		return null_check_open(dev);
	default:
		/* Should never be called for other states */
		WARN_ON(1);
		return BLK_STS_IOERR;
	}
}

static blk_status_t null_zone_write(struct nullb_cmd *cmd, sector_t sector,
				    unsigned int nr_sectors, bool append)
{
	struct nullb_device *dev = cmd->nq->dev;
	unsigned int zno = null_zone_no(dev, sector);
	struct nullb_zone *zone = &dev->zones[zno];
	blk_status_t ret;

	trace_nullb_zone_op(cmd, zno, zone->cond);

	if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL) {
		if (append)
			return BLK_STS_IOERR;
		return null_process_cmd(cmd, REQ_OP_WRITE, sector, nr_sectors);
	}

	null_lock_zone(dev, zone);

	/*
	 * Regular writes must be at the write pointer position. Zone append
	 * writes are automatically issued at the write pointer and the position
	 * returned using the request sector. Note that we do not check the zone
	 * condition because for FULL, READONLY and OFFLINE zones, the sector
	 * check against the zone write pointer will always result in failing
	 * the command.
	 */
	if (append) {
		if (WARN_ON_ONCE(!dev->zone_append_max_sectors) ||
		    zone->wp == NULL_ZONE_INVALID_WP) {
			ret = BLK_STS_IOERR;
			goto unlock_zone;
		}
		sector = zone->wp;
		blk_mq_rq_from_pdu(cmd)->__sector = sector;
	}

	if (sector != zone->wp ||
	    zone->wp + nr_sectors > zone->start + zone->capacity) {
		ret = BLK_STS_IOERR;
		goto unlock_zone;
	}

	if (zone->cond == BLK_ZONE_COND_CLOSED ||
	    zone->cond == BLK_ZONE_COND_EMPTY) {
		if (dev->need_zone_res_mgmt) {
			spin_lock(&dev->zone_res_lock);

			ret = null_check_zone_resources(dev, zone);
			if (ret != BLK_STS_OK) {
				spin_unlock(&dev->zone_res_lock);
				goto unlock_zone;
			}
			if (zone->cond == BLK_ZONE_COND_CLOSED) {
				dev->nr_zones_closed--;
				dev->nr_zones_imp_open++;
			} else if (zone->cond == BLK_ZONE_COND_EMPTY) {
				dev->nr_zones_imp_open++;
			}

			spin_unlock(&dev->zone_res_lock);
		}

		zone->cond = BLK_ZONE_COND_IMP_OPEN;
	}

	ret = null_process_cmd(cmd, REQ_OP_WRITE, sector, nr_sectors);
	if (ret != BLK_STS_OK)
		goto unlock_zone;

	zone->wp += nr_sectors;
	if (zone->wp == zone->start + zone->capacity) {
		if (dev->need_zone_res_mgmt) {
			spin_lock(&dev->zone_res_lock);
			if (zone->cond == BLK_ZONE_COND_EXP_OPEN)
				dev->nr_zones_exp_open--;
			else if (zone->cond == BLK_ZONE_COND_IMP_OPEN)
				dev->nr_zones_imp_open--;
			spin_unlock(&dev->zone_res_lock);
		}
		zone->cond = BLK_ZONE_COND_FULL;
	}

	ret = BLK_STS_OK;

unlock_zone:
	null_unlock_zone(dev, zone);

	return ret;
}

static blk_status_t null_open_zone(struct nullb_device *dev,
				   struct nullb_zone *zone)
{
	blk_status_t ret = BLK_STS_OK;

	if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL)
		return BLK_STS_IOERR;

	switch (zone->cond) {
	case BLK_ZONE_COND_EXP_OPEN:
		/* Open operation on exp open is not an error */
		return BLK_STS_OK;
	case BLK_ZONE_COND_EMPTY:
	case BLK_ZONE_COND_IMP_OPEN:
	case BLK_ZONE_COND_CLOSED:
		break;
	case BLK_ZONE_COND_FULL:
	default:
		return BLK_STS_IOERR;
	}

	if (dev->need_zone_res_mgmt) {
		spin_lock(&dev->zone_res_lock);

		switch (zone->cond) {
		case BLK_ZONE_COND_EMPTY:
			ret = null_check_zone_resources(dev, zone);
			if (ret != BLK_STS_OK) {
				spin_unlock(&dev->zone_res_lock);
				return ret;
			}
			break;
		case BLK_ZONE_COND_IMP_OPEN:
			dev->nr_zones_imp_open--;
			break;
		case BLK_ZONE_COND_CLOSED:
			ret = null_check_zone_resources(dev, zone);
			if (ret != BLK_STS_OK) {
				spin_unlock(&dev->zone_res_lock);
				return ret;
			}
			dev->nr_zones_closed--;
			break;
		default:
			break;
		}

		dev->nr_zones_exp_open++;

		spin_unlock(&dev->zone_res_lock);
	}

	zone->cond = BLK_ZONE_COND_EXP_OPEN;

	return BLK_STS_OK;
}

static blk_status_t null_close_zone(struct nullb_device *dev,
				    struct nullb_zone *zone)
{
	if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL)
		return BLK_STS_IOERR;

	switch (zone->cond) {
	case BLK_ZONE_COND_CLOSED:
		/* close operation on closed is not an error */
		return BLK_STS_OK;
	case BLK_ZONE_COND_IMP_OPEN:
	case BLK_ZONE_COND_EXP_OPEN:
		break;
	case BLK_ZONE_COND_EMPTY:
	case BLK_ZONE_COND_FULL:
	default:
		return BLK_STS_IOERR;
	}

	if (dev->need_zone_res_mgmt) {
		spin_lock(&dev->zone_res_lock);

		switch (zone->cond) {
		case BLK_ZONE_COND_IMP_OPEN:
			dev->nr_zones_imp_open--;
			break;
		case BLK_ZONE_COND_EXP_OPEN:
			dev->nr_zones_exp_open--;
			break;
		default:
			break;
		}

		if (zone->wp > zone->start)
			dev->nr_zones_closed++;

		spin_unlock(&dev->zone_res_lock);
	}

	if (zone->wp == zone->start)
		zone->cond = BLK_ZONE_COND_EMPTY;
	else
		zone->cond = BLK_ZONE_COND_CLOSED;

	return BLK_STS_OK;
}

static blk_status_t null_finish_zone(struct nullb_device *dev,
				     struct nullb_zone *zone)
{
	blk_status_t ret = BLK_STS_OK;

	if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL)
		return BLK_STS_IOERR;

	if (dev->need_zone_res_mgmt) {
		spin_lock(&dev->zone_res_lock);

		switch (zone->cond) {
		case BLK_ZONE_COND_FULL:
			/* Finish operation on full is not an error */
			spin_unlock(&dev->zone_res_lock);
			return BLK_STS_OK;
		case BLK_ZONE_COND_EMPTY:
			ret = null_check_zone_resources(dev, zone);
			if (ret != BLK_STS_OK) {
				spin_unlock(&dev->zone_res_lock);
				return ret;
			}
			break;
		case BLK_ZONE_COND_IMP_OPEN:
			dev->nr_zones_imp_open--;
			break;
		case BLK_ZONE_COND_EXP_OPEN:
			dev->nr_zones_exp_open--;
			break;
		case BLK_ZONE_COND_CLOSED:
			ret = null_check_zone_resources(dev, zone);
			if (ret != BLK_STS_OK) {
				spin_unlock(&dev->zone_res_lock);
				return ret;
			}
			dev->nr_zones_closed--;
			break;
		default:
			spin_unlock(&dev->zone_res_lock);
			return BLK_STS_IOERR;
		}

		spin_unlock(&dev->zone_res_lock);
	}

	zone->cond = BLK_ZONE_COND_FULL;
	zone->wp = zone->start + zone->len;

	return BLK_STS_OK;
}

static blk_status_t null_reset_zone(struct nullb_device *dev,
				    struct nullb_zone *zone)
{
	if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL)
		return BLK_STS_IOERR;

	if (dev->need_zone_res_mgmt) {
		spin_lock(&dev->zone_res_lock);

		switch (zone->cond) {
		case BLK_ZONE_COND_IMP_OPEN:
			dev->nr_zones_imp_open--;
			break;
		case BLK_ZONE_COND_EXP_OPEN:
			dev->nr_zones_exp_open--;
			break;
		case BLK_ZONE_COND_CLOSED:
			dev->nr_zones_closed--;
			break;
		case BLK_ZONE_COND_EMPTY:
		case BLK_ZONE_COND_FULL:
			break;
		default:
			spin_unlock(&dev->zone_res_lock);
			return BLK_STS_IOERR;
		}

		spin_unlock(&dev->zone_res_lock);
	}

	zone->cond = BLK_ZONE_COND_EMPTY;
	zone->wp = zone->start;

	if (dev->memory_backed)
		return null_handle_discard(dev, zone->start, zone->len);

	return BLK_STS_OK;
}

static blk_status_t null_zone_mgmt(struct nullb_cmd *cmd, enum req_op op,
				   sector_t sector)
{
	struct nullb_device *dev = cmd->nq->dev;
	unsigned int zone_no;
	struct nullb_zone *zone;
	blk_status_t ret;
	size_t i;

	if (op == REQ_OP_ZONE_RESET_ALL) {
		for (i = dev->zone_nr_conv; i < dev->nr_zones; i++) {
			zone = &dev->zones[i];
			null_lock_zone(dev, zone);
			if (zone->cond != BLK_ZONE_COND_EMPTY &&
			    zone->cond != BLK_ZONE_COND_READONLY &&
			    zone->cond != BLK_ZONE_COND_OFFLINE) {
				null_reset_zone(dev, zone);
				trace_nullb_zone_op(cmd, i, zone->cond);
			}
			null_unlock_zone(dev, zone);
		}
		return BLK_STS_OK;
	}

	zone_no = null_zone_no(dev, sector);
	zone = &dev->zones[zone_no];

	null_lock_zone(dev, zone);

	if (zone->cond == BLK_ZONE_COND_READONLY ||
	    zone->cond == BLK_ZONE_COND_OFFLINE) {
		ret = BLK_STS_IOERR;
		goto unlock;
	}

	switch (op) {
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
		ret = BLK_STS_NOTSUPP;
		break;
	}

	if (ret == BLK_STS_OK)
		trace_nullb_zone_op(cmd, zone_no, zone->cond);

unlock:
	null_unlock_zone(dev, zone);

	return ret;
}

blk_status_t null_process_zoned_cmd(struct nullb_cmd *cmd, enum req_op op,
				    sector_t sector, sector_t nr_sectors)
{
	struct nullb_device *dev;
	struct nullb_zone *zone;
	blk_status_t sts;

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
		dev = cmd->nq->dev;
		zone = &dev->zones[null_zone_no(dev, sector)];
		if (zone->cond == BLK_ZONE_COND_OFFLINE)
			return BLK_STS_IOERR;

		null_lock_zone(dev, zone);
		sts = null_process_cmd(cmd, op, sector, nr_sectors);
		null_unlock_zone(dev, zone);
		return sts;
	}
}

/*
 * Set a zone in the read-only or offline condition.
 */
static void null_set_zone_cond(struct nullb_device *dev,
			       struct nullb_zone *zone, enum blk_zone_cond cond)
{
	if (WARN_ON_ONCE(cond != BLK_ZONE_COND_READONLY &&
			 cond != BLK_ZONE_COND_OFFLINE))
		return;

	null_lock_zone(dev, zone);

	/*
	 * If the read-only condition is requested again to zones already in
	 * read-only condition, restore back normal empty condition. Do the same
	 * if the offline condition is requested for offline zones. Otherwise,
	 * set the specified zone condition to the zones. Finish the zones
	 * beforehand to free up zone resources.
	 */
	if (zone->cond == cond) {
		zone->cond = BLK_ZONE_COND_EMPTY;
		zone->wp = zone->start;
		if (dev->memory_backed)
			null_handle_discard(dev, zone->start, zone->len);
	} else {
		if (zone->cond != BLK_ZONE_COND_READONLY &&
		    zone->cond != BLK_ZONE_COND_OFFLINE)
			null_finish_zone(dev, zone);
		zone->cond = cond;
		zone->wp = NULL_ZONE_INVALID_WP;
	}

	null_unlock_zone(dev, zone);
}

/*
 * Identify a zone from the sector written to configfs file. Then set zone
 * condition to the zone.
 */
ssize_t zone_cond_store(struct nullb_device *dev, const char *page,
			size_t count, enum blk_zone_cond cond)
{
	unsigned long long sector;
	unsigned int zone_no;
	int ret;

	if (!dev->zoned) {
		pr_err("null_blk device is not zoned\n");
		return -EINVAL;
	}

	if (!dev->zones) {
		pr_err("null_blk device is not yet powered\n");
		return -EINVAL;
	}

	ret = kstrtoull(page, 0, &sector);
	if (ret < 0)
		return ret;

	zone_no = null_zone_no(dev, sector);
	if (zone_no >= dev->nr_zones) {
		pr_err("Sector out of range\n");
		return -EINVAL;
	}

	if (dev->zones[zone_no].type == BLK_ZONE_TYPE_CONVENTIONAL) {
		pr_err("Can not change condition of conventional zones\n");
		return -EINVAL;
	}

	null_set_zone_cond(dev, &dev->zones[zone_no], cond);

	return count;
}
