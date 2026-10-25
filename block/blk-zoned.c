// SPDX-License-Identifier: GPL-2.0
/*
 * Zoned block device handling
 *
 * Copyright (c) 2015, Hannes Reinecke
 * Copyright (c) 2015, SUSE Linux GmbH
 *
 * Copyright (c) 2016, Damien Le Moal
 * Copyright (c) 2016, Western Digital
 * Copyright (c) 2024, Western Digital Corporation or its affiliates.
 */

#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/spinlock.h>
#include <linux/refcount.h>
#include <linux/mempool.h>
#include <linux/kthread.h>
#include <linux/freezer.h>

#include <trace/events/block.h>

#include "blk.h"
#include "blk-mq-sched.h"
#include "blk-mq-debugfs.h"

#define ZONE_COND_NAME(name) [BLK_ZONE_COND_##name] = #name
static const char *const zone_cond_name[] = {
	ZONE_COND_NAME(NOT_WP),
	ZONE_COND_NAME(EMPTY),
	ZONE_COND_NAME(IMP_OPEN),
	ZONE_COND_NAME(EXP_OPEN),
	ZONE_COND_NAME(CLOSED),
	ZONE_COND_NAME(READONLY),
	ZONE_COND_NAME(FULL),
	ZONE_COND_NAME(OFFLINE),
	ZONE_COND_NAME(ACTIVE),
};
#undef ZONE_COND_NAME

/*
 * Internal and compact representation of enum blk_zone_cond values for zone
 * conditions. All these values fit into 4-bits, allowing using the high order
 * bits as the zone type.
 */
enum blk_zstate {
	BLK_ZSTATE_NOT_WP	= 0x00,
	BLK_ZSTATE_EMPTY	= 0x01,
	BLK_ZSTATE_IMP_OPEN	= 0x02,
	BLK_ZSTATE_EXP_OPEN	= 0x03,
	BLK_ZSTATE_CLOSED	= 0x04,
	BLK_ZSTATE_READONLY	= 0x05,
	BLK_ZSTATE_FULL		= 0x06,
	BLK_ZSTATE_OFFLINE	= 0x07,
	BLK_ZSTATE_ACTIVE	= 0x08,

	BLK_ZSTATE_COND_MASK	= 0x0F,

	/* Conventional zone. */
	BLK_ZFLAG_CONV		= 0x80,
	BLK_ZSTATE_FLAGS_MASK	= ~BLK_ZSTATE_COND_MASK,
};

/*
 * Lookup table and helper to convert enum blk_zstate conditions into enum
 * blk_zone_condition values.
 */
static const u8 blk_zstate2zcond[] = {
	[BLK_ZSTATE_NOT_WP]	= BLK_ZONE_COND_NOT_WP,
	[BLK_ZSTATE_EMPTY]	= BLK_ZONE_COND_EMPTY,
	[BLK_ZSTATE_IMP_OPEN]	= BLK_ZONE_COND_IMP_OPEN,
	[BLK_ZSTATE_EXP_OPEN]	= BLK_ZONE_COND_EXP_OPEN,
	[BLK_ZSTATE_CLOSED]	= BLK_ZONE_COND_CLOSED,
	[BLK_ZSTATE_READONLY]	= BLK_ZONE_COND_READONLY,
	[BLK_ZSTATE_FULL]	= BLK_ZONE_COND_FULL,
	[BLK_ZSTATE_OFFLINE]	= BLK_ZONE_COND_OFFLINE,
	[BLK_ZSTATE_ACTIVE]	= BLK_ZONE_COND_ACTIVE,
};

static inline enum blk_zone_cond blk_zstate_to_zone_cond(enum blk_zstate zs)
{
	u8 idx = zs & BLK_ZSTATE_COND_MASK;

	if (WARN_ON_ONCE(idx >= ARRAY_SIZE(blk_zstate2zcond)))
		return 0;

	return blk_zstate2zcond[idx];
}

/*
 * Lookup table and helper to convert an enum blk_zone_condition into an enum
 * blk_zstate condition value. To keep the lookup table small, the
 * BLK_ZONE_COND_ACTIVE condition is not added and handled separately.
 */
static const u8 blk_zcond2zstate[] = {
	[BLK_ZONE_COND_NOT_WP]		= BLK_ZSTATE_NOT_WP,
	[BLK_ZONE_COND_EMPTY]		= BLK_ZSTATE_EMPTY,
	[BLK_ZONE_COND_IMP_OPEN]	= BLK_ZSTATE_ACTIVE,
	[BLK_ZONE_COND_EXP_OPEN]	= BLK_ZSTATE_ACTIVE,
	[BLK_ZONE_COND_CLOSED]		= BLK_ZSTATE_ACTIVE,
	[BLK_ZONE_COND_READONLY]	= BLK_ZSTATE_READONLY,
	[BLK_ZONE_COND_FULL]		= BLK_ZSTATE_FULL,
	[BLK_ZONE_COND_OFFLINE]		= BLK_ZSTATE_OFFLINE,
};

static inline enum blk_zstate blk_zone_cond_to_zstate(enum blk_zone_cond cond)
{
	if (cond == BLK_ZONE_COND_ACTIVE)
		return BLK_ZSTATE_ACTIVE;

	if (WARN_ON_ONCE(cond >= ARRAY_SIZE(blk_zcond2zstate)))
		return 0;

	return blk_zcond2zstate[cond];
}

/*
 * Combine an enum blk_zone_condition and zone flags into a zones_state array
 * entry.
 */
static inline void blk_zstate_set(u8 *zones_state, unsigned int nr_zones,
			unsigned int zno, enum blk_zone_cond cond, u8 flags)
{
	if (zones_state && zno < nr_zones)
		zones_state[zno] = flags | blk_zone_cond_to_zstate(cond);
}

static inline u8 blk_zstate_flags(enum blk_zstate zs)
{
	return zs & BLK_ZSTATE_FLAGS_MASK;
}

static inline bool blk_zstate_is_conv(enum blk_zstate zs)
{
	return blk_zstate_flags(zs) & BLK_ZFLAG_CONV;
}

/*
 * Per-zone write plug.
 * @node: hlist_node structure for managing the plug using a hash table.
 * @entry: list_head structure for listing the plug in the disk list of active
 *         zone write plugs.
 * @bio_list: The list of BIOs that are currently plugged.
 * @bio_work: Work struct to handle issuing of plugged BIOs
 * @rcu_head: RCU head to free zone write plugs with an RCU grace period.
 * @disk: The gendisk the plug belongs to.
 * @lock: Spinlock to atomically manipulate the plug.
 * @ref: Zone write plug reference counter. A zone write plug reference is
 *       always at least 1 when the plug is hashed in the disk plug hash table.
 *       The reference is incremented whenever a new BIO needing plugging is
 *       submitted and when a function needs to manipulate a plug. The
 *       reference count is decremented whenever a plugged BIO completes and
 *       when a function that referenced the plug returns. The initial
 *       reference is dropped whenever the zone of the zone write plug is reset,
 *       finished and when the zone becomes full (last write BIO to the zone
 *       completes).
 * @flags: Flags indicating the plug state.
 * @zone_no: The number of the zone the plug is managing.
 * @wp_offset: The zone write pointer location relative to the start of the zone
 *             as a number of 512B sectors.
 * @cond: Condition of the zone
 */
struct blk_zone_wplug {
	struct hlist_node	node;
	struct list_head	entry;
	struct bio_list		bio_list;
	struct work_struct	bio_work;
	struct rcu_head		rcu_head;
	struct gendisk		*disk;
	spinlock_t		lock;
	refcount_t		ref;
	unsigned int		flags;
	unsigned int		zone_no;
	unsigned int		wp_offset;
	enum blk_zone_cond	cond;
};

static inline bool disk_need_zone_resources(struct gendisk *disk)
{
	/*
	 * All request-based zoned devices need zone resources so that the
	 * block layer can automatically handle write BIO plugging. BIO-based
	 * device drivers (e.g. DM devices) are normally responsible for
	 * handling zone write ordering and do not need zone resources, unless
	 * the driver requires zone append emulation.
	 */
	return queue_is_mq(disk->queue) ||
		queue_emulates_zone_append(disk->queue);
}

static inline unsigned int disk_zone_wplugs_hash_size(struct gendisk *disk)
{
	return 1U << disk->zone_wplugs_hash_bits;
}

/*
 * Zone write plug flags bits:
 *  - BLK_ZONE_WPLUG_PLUGGED: Indicates that the zone write plug is plugged,
 *    that is, that write BIOs are being throttled due to a write BIO already
 *    being executed or the zone write plug bio list is not empty.
 *  - BLK_ZONE_WPLUG_NEED_WP_UPDATE: Indicates that we lost track of a zone
 *    write pointer offset and need to update it.
 *  - BLK_ZONE_WPLUG_DEAD: Indicates that the zone write plug will be
 *    removed from the disk hash table of zone write plugs when the last
 *    reference on the zone write plug is dropped. If set, this flag also
 *    indicates that the initial extra reference on the zone write plug was
 *    dropped, meaning that the reference count indicates the current number of
 *    active users (code context or BIOs and requests in flight). This flag is
 *    set when a zone is reset, finished or becomes full.
 */
#define BLK_ZONE_WPLUG_PLUGGED		(1U << 0)
#define BLK_ZONE_WPLUG_NEED_WP_UPDATE	(1U << 1)
#define BLK_ZONE_WPLUG_DEAD		(1U << 2)

/**
 * blk_zone_cond_str - Return a zone condition name string
 * @zone_cond: a zone condition BLK_ZONE_COND_name
 *
 * Convert a BLK_ZONE_COND_name zone condition into the string "name". Useful
 * for the debugging and tracing zone conditions. For an invalid zone
 * conditions, the string "UNKNOWN" is returned.
 */
const char *blk_zone_cond_str(enum blk_zone_cond zone_cond)
{
	static const char *zone_cond_str = "UNKNOWN";

	if (zone_cond < ARRAY_SIZE(zone_cond_name) && zone_cond_name[zone_cond])
		zone_cond_str = zone_cond_name[zone_cond];

	return zone_cond_str;
}
EXPORT_SYMBOL_GPL(blk_zone_cond_str);

static void disk_zone_set_cond(struct gendisk *disk, sector_t sector,
			       enum blk_zone_cond cond)
{
	unsigned int zno = disk_zone_no(disk, sector);
	u8 *zones_state;

	rcu_read_lock();
	zones_state = rcu_dereference(disk->zones_state);
	if (likely(zones_state && zno < disk->nr_zones))
		blk_zstate_set(zones_state, disk->nr_zones, zno, cond,
			       blk_zstate_flags(zones_state[zno]));
	rcu_read_unlock();
}

static inline u8 disk_zone_get_state(struct gendisk *disk, sector_t sector)
{
	unsigned int zno = disk_zone_no(disk, sector);
	u8 *zones_state, zs;

	rcu_read_lock();
	zones_state = rcu_dereference(disk->zones_state);
	if (likely(zones_state && zno < disk->nr_zones))
		zs = zones_state[zno];
	else
		zs = BLK_ZFLAG_CONV;
	rcu_read_unlock();

	return zs;
}

static enum blk_zone_cond disk_zone_get_cond(struct gendisk *disk,
					     sector_t sector)
{
	u8 zs = disk_zone_get_state(disk, sector);

	return blk_zstate_to_zone_cond(zs);
}

static inline bool
disk_zone_cond_is_offline_or_readonly(enum blk_zone_cond cond)
{
	return cond == BLK_ZONE_COND_READONLY ||
		cond == BLK_ZONE_COND_OFFLINE;
}

static inline bool disk_zone_is_offline_or_readonly(struct gendisk *disk,
						    sector_t sector)
{
	enum blk_zone_cond cond = disk_zone_get_cond(disk, sector);

	return disk_zone_cond_is_offline_or_readonly(cond);
}

static bool disk_zone_is_seq(struct gendisk *disk, sector_t sector)
{
	u8 zs = disk_zone_get_state(disk, sector);

	return !blk_zstate_is_conv(zs);
}

/**
 * bdev_zone_is_seq - check if a sector belongs to a sequential write zone
 * @bdev:       block device to check
 * @sector:     sector number
 *
 * Check if @sector on @bdev is contained in a sequential write required zone.
 */
bool bdev_zone_is_seq(struct block_device *bdev, sector_t sector)
{
	if (!bdev_is_zoned(bdev))
		return false;

	return disk_zone_is_seq(bdev->bd_disk, sector);
}
EXPORT_SYMBOL_GPL(bdev_zone_is_seq);

/**
 * bdev_zone_mgmt_allowed - check if management operations are allowed on a zone
 * @bdev:       block device to check
 * @sector:     sector number
 *
 * Check if the zone containing @sector on @bdev can be a target for a zone
 * management operation, that is, if the zone is a sequential write required
 * zone that is not offline nor read-only.
 */
bool bdev_zone_mgmt_allowed(struct block_device *bdev, sector_t sector)
{
	enum blk_zone_cond cond;
	u8 zs;

	if (!bdev_is_zoned(bdev))
		return false;

	zs = disk_zone_get_state(bdev->bd_disk, sector);
	if (blk_zstate_is_conv(zs))
		return false;

	cond = blk_zstate_to_zone_cond(zs);
	return !disk_zone_cond_is_offline_or_readonly(cond);
}

/*
 * Zone report arguments for block device drivers report_zones operation.
 * @cb: report_zones_cb callback for each reported zone.
 * @data: Private data passed to report_zones_cb.
 */
struct blk_report_zones_args {
	report_zones_cb cb;
	void		*data;
	bool		report_active;
};

static int blkdev_do_report_zones(struct block_device *bdev, sector_t sector,
				  unsigned int nr_zones,
				  struct blk_report_zones_args *args)
{
	struct gendisk *disk = bdev->bd_disk;

	if (!bdev_is_zoned(bdev) || WARN_ON_ONCE(!disk->fops->report_zones))
		return -EOPNOTSUPP;

	if (!nr_zones || sector >= get_capacity(disk))
		return 0;

	return disk->fops->report_zones(disk, sector, nr_zones, args);
}

/**
 * blkdev_report_zones - Get zones information
 * @bdev:	Target block device
 * @sector:	Sector from which to report zones
 * @nr_zones:	Maximum number of zones to report
 * @cb:		Callback function called for each reported zone
 * @data:	Private data for the callback
 *
 * Description:
 *    Get zone information starting from the zone containing @sector for at most
 *    @nr_zones, and call @cb for each zone reported by the device.
 *    To report all zones in a device starting from @sector, the BLK_ALL_ZONES
 *    constant can be passed to @nr_zones.
 *    Returns the number of zones reported by the device, or a negative errno
 *    value in case of failure.
 *
 *    Note: The caller must use memalloc_noXX_save/restore() calls to control
 *    memory allocations done within this function.
 */
int blkdev_report_zones(struct block_device *bdev, sector_t sector,
			unsigned int nr_zones, report_zones_cb cb, void *data)
{
	struct blk_report_zones_args args = {
		.cb = cb,
		.data = data,
	};

	return blkdev_do_report_zones(bdev, sector, nr_zones, &args);
}
EXPORT_SYMBOL_GPL(blkdev_report_zones);

static int blkdev_zone_reset_all(struct block_device *bdev)
{
	struct bio bio;

	bio_init(&bio, bdev, NULL, 0, REQ_OP_ZONE_RESET_ALL | REQ_SYNC);
	trace_blkdev_zone_mgmt(&bio, 0);
	return submit_bio_wait(&bio);
}

/**
 * blkdev_zone_mgmt - Execute a zone management operation on a range of zones
 * @bdev:	Target block device
 * @op:		Operation to be performed on the zones
 * @sector:	Start sector of the first zone to operate on
 * @nr_sectors:	Number of sectors, should be at least the length of one zone and
 *		must be zone size aligned.
 *
 * Description:
 *    Perform the specified operation on the range of zones specified by
 *    @sector..@sector+@nr_sectors. Specifying the entire disk sector range
 *    is valid, but the specified range should not contain conventional zones.
 *    The operation to execute on each zone can be a zone reset, open, close
 *    or finish request.
 */
int blkdev_zone_mgmt(struct block_device *bdev, enum req_op op,
		     sector_t sector, sector_t nr_sectors)
{
	sector_t zone_sectors = bdev_zone_sectors(bdev);
	sector_t capacity = bdev_nr_sectors(bdev);
	sector_t end_sector = sector + nr_sectors;
	struct bio *bio = NULL;
	int ret = 0;

	if (!bdev_is_zoned(bdev))
		return -EOPNOTSUPP;

	if (bdev_read_only(bdev))
		return -EPERM;

	if (!op_is_zone_mgmt(op))
		return -EOPNOTSUPP;

	if (end_sector <= sector || end_sector > capacity)
		/* Out of range */
		return -EINVAL;

	/* Check alignment (handle eventual smaller last zone) */
	if (!bdev_is_zone_start(bdev, sector))
		return -EINVAL;

	if (!bdev_is_zone_start(bdev, nr_sectors) && end_sector != capacity)
		return -EINVAL;

	/*
	 * In the case of a zone reset operation over all zones, use
	 * REQ_OP_ZONE_RESET_ALL.
	 */
	if (op == REQ_OP_ZONE_RESET && sector == 0 && nr_sectors == capacity)
		return blkdev_zone_reset_all(bdev);

	while (sector < end_sector) {
		bio = blk_next_bio(bio, bdev, 0, op | REQ_SYNC, GFP_KERNEL);
		bio->bi_iter.bi_sector = sector;
		sector += zone_sectors;

		/* This may take a while, so be nice to others */
		cond_resched();
	}

	trace_blkdev_zone_mgmt(bio, nr_sectors);
	ret = submit_bio_wait(bio);
	bio_put(bio);

	return ret;
}
EXPORT_SYMBOL_GPL(blkdev_zone_mgmt);

struct zone_report_args {
	struct blk_zone __user *zones;
};

static int blkdev_copy_zone_to_user(struct blk_zone *zone, unsigned int idx,
				    void *data)
{
	struct zone_report_args *args = data;

	if (copy_to_user(&args->zones[idx], zone, sizeof(struct blk_zone)))
		return -EFAULT;
	return 0;
}

/*
 * Mask of valid input flags for BLKREPORTZONEV2 ioctl.
 */
#define BLK_ZONE_REPV2_INPUT_FLAGS	BLK_ZONE_REP_CACHED

/*
 * BLKREPORTZONE and BLKREPORTZONEV2 ioctl processing.
 * Called from blkdev_ioctl.
 */
int blkdev_report_zones_ioctl(struct block_device *bdev, unsigned int cmd,
		unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct zone_report_args args;
	struct blk_zone_report rep;
	int ret;

	if (!argp)
		return -EINVAL;

	if (!bdev_is_zoned(bdev))
		return -ENOTTY;

	if (copy_from_user(&rep, argp, sizeof(struct blk_zone_report)))
		return -EFAULT;

	if (!rep.nr_zones)
		return -EINVAL;

	args.zones = argp + sizeof(struct blk_zone_report);

	switch (cmd) {
	case BLKREPORTZONE:
		ret = blkdev_report_zones(bdev, rep.sector, rep.nr_zones,
					  blkdev_copy_zone_to_user, &args);
		break;
	case BLKREPORTZONEV2:
		if (rep.flags & ~BLK_ZONE_REPV2_INPUT_FLAGS)
			return -EINVAL;
		ret = blkdev_report_zones_cached(bdev, rep.sector, rep.nr_zones,
					 blkdev_copy_zone_to_user, &args);
		break;
	default:
		return -EINVAL;
	}

	if (ret < 0)
		return ret;

	rep.nr_zones = ret;
	rep.flags = BLK_ZONE_REP_CAPACITY;
	if (copy_to_user(argp, &rep, sizeof(struct blk_zone_report)))
		return -EFAULT;
	return 0;
}

static int blkdev_reset_zone(struct block_device *bdev, blk_mode_t mode,
			     struct blk_zone_range *zrange)
{
	loff_t start, end;
	int ret = -EINVAL;

	inode_lock(bdev->bd_mapping->host);
	filemap_invalidate_lock(bdev->bd_mapping);
	if (zrange->sector + zrange->nr_sectors <= zrange->sector ||
	    zrange->sector + zrange->nr_sectors > get_capacity(bdev->bd_disk))
		/* Out of range */
		goto out_unlock;

	start = zrange->sector << SECTOR_SHIFT;
	end = ((zrange->sector + zrange->nr_sectors) << SECTOR_SHIFT) - 1;

	ret = truncate_bdev_range(bdev, mode, start, end);
	if (ret)
		goto out_unlock;

	ret = blkdev_zone_mgmt(bdev, REQ_OP_ZONE_RESET, zrange->sector,
			       zrange->nr_sectors);
out_unlock:
	filemap_invalidate_unlock(bdev->bd_mapping);
	inode_unlock(bdev->bd_mapping->host);
	return ret;
}

/*
 * BLKRESETZONE, BLKOPENZONE, BLKCLOSEZONE and BLKFINISHZONE ioctl processing.
 * Called from blkdev_ioctl.
 */
int blkdev_zone_mgmt_ioctl(struct block_device *bdev, blk_mode_t mode,
			   unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct blk_zone_range zrange;
	enum req_op op;

	if (!argp)
		return -EINVAL;

	if (!bdev_is_zoned(bdev))
		return -ENOTTY;

	if (!(mode & BLK_OPEN_WRITE))
		return -EBADF;

	if (copy_from_user(&zrange, argp, sizeof(struct blk_zone_range)))
		return -EFAULT;

	switch (cmd) {
	case BLKRESETZONE:
		return blkdev_reset_zone(bdev, mode, &zrange);
	case BLKOPENZONE:
		op = REQ_OP_ZONE_OPEN;
		break;
	case BLKCLOSEZONE:
		op = REQ_OP_ZONE_CLOSE;
		break;
	case BLKFINISHZONE:
		op = REQ_OP_ZONE_FINISH;
		break;
	default:
		return -ENOTTY;
	}

	return blkdev_zone_mgmt(bdev, op, zrange.sector, zrange.nr_sectors);
}

static bool disk_zone_is_last(struct gendisk *disk, struct blk_zone *zone)
{
	return zone->start + zone->len >= get_capacity(disk);
}

static bool disk_zone_wplug_is_full(struct gendisk *disk,
				    struct blk_zone_wplug *zwplug)
{
	if (zwplug->zone_no < disk->nr_zones - 1)
		return zwplug->wp_offset >= disk->zone_capacity;
	return zwplug->wp_offset >= disk->last_zone_capacity;
}

static bool disk_zone_wplug_is_offline_or_readonly(struct blk_zone_wplug *zwplug)
{
	return disk_zone_cond_is_offline_or_readonly(zwplug->cond);
}

static bool disk_insert_zone_wplug(struct gendisk *disk,
				   struct blk_zone_wplug *zwplug)
{
	struct blk_zone_wplug *zwplg;
	unsigned long flags;
	u8 *zones_state;
	unsigned int idx =
		hash_32(zwplug->zone_no, disk->zone_wplugs_hash_bits);

	/*
	 * Add the new zone write plug to the hash table, but carefully as we
	 * are racing with other submission context, so we may already have a
	 * zone write plug for the same zone.
	 */
	spin_lock_irqsave(&disk->zone_wplugs_hash_lock, flags);
	hlist_for_each_entry_rcu(zwplg, &disk->zone_wplugs_hash[idx], node) {
		if (zwplg->zone_no == zwplug->zone_no) {
			spin_unlock_irqrestore(&disk->zone_wplugs_hash_lock,
					       flags);
			return false;
		}
	}

	/*
	 * Set the zone condition: if we do not yet have a zones_state array
	 * attached to the disk, then this is a zone write plug insert from the
	 * first call to blk_revalidate_disk_zones(), in which case the zone is
	 * necessarilly in the active condition.
	 */
	zones_state = rcu_dereference_check(disk->zones_state,
				lockdep_is_held(&disk->zone_wplugs_hash_lock));
	if (zones_state)
		zwplug->cond =
			blk_zstate_to_zone_cond(zones_state[zwplug->zone_no]);
	else
		zwplug->cond = BLK_ZONE_COND_ACTIVE;

	hlist_add_head_rcu(&zwplug->node, &disk->zone_wplugs_hash[idx]);
	atomic_inc(&disk->nr_zone_wplugs);
	spin_unlock_irqrestore(&disk->zone_wplugs_hash_lock, flags);

	return true;
}

static struct blk_zone_wplug *disk_get_hashed_zone_wplug(struct gendisk *disk,
							 sector_t sector)
{
	unsigned int zno = disk_zone_no(disk, sector);
	unsigned int idx = hash_32(zno, disk->zone_wplugs_hash_bits);
	struct blk_zone_wplug *zwplug;

	rcu_read_lock();

	hlist_for_each_entry_rcu(zwplug, &disk->zone_wplugs_hash[idx], node) {
		if (zwplug->zone_no == zno &&
		    refcount_inc_not_zero(&zwplug->ref)) {
			rcu_read_unlock();
			return zwplug;
		}
	}

	rcu_read_unlock();

	return NULL;
}

static inline struct blk_zone_wplug *disk_get_zone_wplug(struct gendisk *disk,
							 sector_t sector)
{
	if (!atomic_read(&disk->nr_zone_wplugs))
		return NULL;

	return disk_get_hashed_zone_wplug(disk, sector);
}

static void disk_for_all_zone_wplugs(struct gendisk *disk,
				     void (*actor)(struct blk_zone_wplug *,
						   void *),
				     void *data)
{
	struct blk_zone_wplug *zwplug;
	unsigned int i;

	if (!disk->zone_wplugs_hash)
		return;

	rcu_read_lock();
	for (i = 0; i < disk_zone_wplugs_hash_size(disk); i++) {
		hlist_for_each_entry_rcu(zwplug, &disk->zone_wplugs_hash[i],
					 node)
			actor(zwplug, data);
	}
	rcu_read_unlock();
}

static void disk_free_zone_wplug_rcu(struct rcu_head *rcu_head)
{
	struct blk_zone_wplug *zwplug =
		container_of(rcu_head, struct blk_zone_wplug, rcu_head);

	mempool_free(zwplug, zwplug->disk->zone_wplugs_pool);
}

static void disk_free_zone_wplug(struct blk_zone_wplug *zwplug)
{
	struct gendisk *disk = zwplug->disk;
	unsigned long flags;

	WARN_ON_ONCE(!(zwplug->flags & BLK_ZONE_WPLUG_DEAD));
	WARN_ON_ONCE(zwplug->flags & BLK_ZONE_WPLUG_PLUGGED);
	WARN_ON_ONCE(!bio_list_empty(&zwplug->bio_list));

	spin_lock_irqsave(&disk->zone_wplugs_hash_lock, flags);
	blk_zstate_set(rcu_dereference_check(disk->zones_state,
				lockdep_is_held(&disk->zone_wplugs_hash_lock)),
		       disk->nr_zones, zwplug->zone_no, zwplug->cond, 0);
	hlist_del_init_rcu(&zwplug->node);
	atomic_dec(&disk->nr_zone_wplugs);
	spin_unlock_irqrestore(&disk->zone_wplugs_hash_lock, flags);

	call_rcu(&zwplug->rcu_head, disk_free_zone_wplug_rcu);
}

static inline void disk_put_zone_wplug(struct blk_zone_wplug *zwplug)
{
	if (refcount_dec_and_test(&zwplug->ref))
		disk_free_zone_wplug(zwplug);
}

static inline void blk_zone_wplug_bio_io_error(struct blk_zone_wplug *zwplug,
					       struct bio *bio)
{
	struct request_queue *q = zwplug->disk->queue;

	bio_clear_flag(bio, BIO_ZONE_WRITE_PLUGGING);
	bio_io_error(bio);
	disk_put_zone_wplug(zwplug);
	/* Drop the reference taken by disk_zone_wplug_add_bio(). */
	blk_queue_exit(q);
}

/*
 * Abort (fail) all plugged BIOs of a zone write plug.
 */
static void disk_zone_wplug_abort(struct blk_zone_wplug *zwplug)
{
	struct gendisk *disk = zwplug->disk;
	struct bio *bio;

	lockdep_assert_held(&zwplug->lock);

	if (bio_list_empty(&zwplug->bio_list))
		return;

	pr_warn_ratelimited("%s: zone %u: Aborting plugged BIOs\n",
			    zwplug->disk->disk_name, zwplug->zone_no);
	while ((bio = bio_list_pop(&zwplug->bio_list)))
		blk_zone_wplug_bio_io_error(zwplug, bio);

	zwplug->flags &= ~BLK_ZONE_WPLUG_PLUGGED;

	/*
	 * If we are using the per disk zone write plugs worker thread, remove
	 * the zone write plug from the work list and drop the reference we
	 * took when the zone write plug was added to that list.
	 */
	if (blk_queue_zoned_qd1_writes(disk->queue)) {
		spin_lock(&disk->zone_wplugs_list_lock);
		if (!list_empty(&zwplug->entry)) {
			list_del_init(&zwplug->entry);
			disk_put_zone_wplug(zwplug);
		}
		spin_unlock(&disk->zone_wplugs_list_lock);
	}
}

/*
 * Flag the zone write plug as dead and drop the initial reference we got when
 * the zone write plug was added to the hash table. The zone write plug will be
 * unhashed when its last reference is dropped.
 */
static void disk_mark_zone_wplug_dead(struct blk_zone_wplug *zwplug)
{
	lockdep_assert_held(&zwplug->lock);

	if (!(zwplug->flags & BLK_ZONE_WPLUG_DEAD)) {
		zwplug->flags |= BLK_ZONE_WPLUG_DEAD;
		disk_put_zone_wplug(zwplug);
	}
}

static inline bool disk_check_zone_wplug_dead(struct blk_zone_wplug *zwplug)
{
	if (disk_zone_wplug_is_offline_or_readonly(zwplug)) {
		disk_zone_wplug_abort(zwplug);
		disk_mark_zone_wplug_dead(zwplug);
		return true;
	}

	if (!(zwplug->flags & BLK_ZONE_WPLUG_DEAD))
		return false;

	/*
	 * If a new write is received right after a zone reset completes and
	 * while the disk_zone_wplugs_worker() thread has not yet released the
	 * reference on the zone write plug after processing the last write to
	 * the zone, then the new write BIO will see the zone write plug marked
	 * as dead. This case is however a false positive and a perfectly valid
	 * pattern. In such case, restore the zone write plug to a live one.
	 */
	if (!zwplug->wp_offset && bio_list_empty(&zwplug->bio_list)) {
		zwplug->flags &= ~BLK_ZONE_WPLUG_DEAD;
		refcount_inc(&zwplug->ref);
		return false;
	}

	return true;
}

static bool disk_zone_wplug_submit_bio(struct gendisk *disk,
				       struct blk_zone_wplug *zwplug);

static void blk_zone_wplug_bio_work(struct work_struct *work)
{
	struct blk_zone_wplug *zwplug =
		container_of(work, struct blk_zone_wplug, bio_work);

	disk_zone_wplug_submit_bio(zwplug->disk, zwplug);

	/* Drop the reference we took in disk_zone_wplug_schedule_work(). */
	disk_put_zone_wplug(zwplug);
}

/*
 * Get a zone write plug for the zone containing @sector.
 * If the plug does not exist, it is allocated and inserted in the disk hash
 * table.
 */
static struct blk_zone_wplug *disk_get_or_alloc_zone_wplug(struct gendisk *disk,
					sector_t sector, gfp_t gfp_mask)
{
	unsigned int zno = disk_zone_no(disk, sector);
	struct blk_zone_wplug *zwplug;

again:
	zwplug = disk_get_zone_wplug(disk, sector);
	if (zwplug)
		return zwplug;

	/*
	 * Allocate and initialize a zone write plug with an extra reference
	 * so that it is not freed when the zone write plug becomes idle without
	 * the zone being full.
	 */
	zwplug = mempool_alloc(disk->zone_wplugs_pool, gfp_mask);
	if (!zwplug)
		return NULL;

	INIT_HLIST_NODE(&zwplug->node);
	refcount_set(&zwplug->ref, 2);
	spin_lock_init(&zwplug->lock);
	zwplug->flags = 0;
	zwplug->zone_no = zno;
	zwplug->wp_offset = bdev_offset_from_zone_start(disk->part0, sector);
	bio_list_init(&zwplug->bio_list);
	INIT_WORK(&zwplug->bio_work, blk_zone_wplug_bio_work);
	INIT_LIST_HEAD(&zwplug->entry);
	zwplug->disk = disk;

	/*
	 * Insert the new zone write plug in the hash table. This can fail only
	 * if another context already inserted a plug. Retry from the beginning
	 * in such case.
	 */
	if (!disk_insert_zone_wplug(disk, zwplug)) {
		mempool_free(zwplug, disk->zone_wplugs_pool);
		goto again;
	}

	return zwplug;
}

/*
 * Update a zone write plug condition based on the write pointer offset.
 */
static void disk_zone_wplug_update_cond(struct gendisk *disk,
					struct blk_zone_wplug *zwplug)
{
	lockdep_assert_held(&zwplug->lock);

	if (disk_zone_wplug_is_full(disk, zwplug))
		zwplug->cond = BLK_ZONE_COND_FULL;
	else if (!zwplug->wp_offset)
		zwplug->cond = BLK_ZONE_COND_EMPTY;
	else
		zwplug->cond = BLK_ZONE_COND_ACTIVE;
}

/*
 * Set a zone write plug write pointer offset to the specified value.
 * This aborts all plugged BIOs, which is fine as this function is called for
 * a zone reset operation, a zone finish operation or if the zone needs a wp
 * update from a report zone after a write error.
 */
static void disk_zone_wplug_set_wp_offset(struct gendisk *disk,
					  struct blk_zone_wplug *zwplug,
					  unsigned int wp_offset)
{
	lockdep_assert_held(&zwplug->lock);

	/* Update the zone write pointer and abort all plugged BIOs. */
	zwplug->flags &= ~BLK_ZONE_WPLUG_NEED_WP_UPDATE;
	if (!disk_zone_wplug_is_offline_or_readonly(zwplug)) {
		zwplug->wp_offset = wp_offset;
		disk_zone_wplug_update_cond(disk, zwplug);
	}

	disk_zone_wplug_abort(zwplug);
	if (!zwplug->wp_offset || disk_zone_wplug_is_full(disk, zwplug))
		disk_mark_zone_wplug_dead(zwplug);
}

static unsigned int blk_zone_wp_offset(struct blk_zone *zone)
{
	switch (zone->cond) {
	case BLK_ZONE_COND_IMP_OPEN:
	case BLK_ZONE_COND_EXP_OPEN:
	case BLK_ZONE_COND_CLOSED:
	case BLK_ZONE_COND_ACTIVE:
		return zone->wp - zone->start;
	case BLK_ZONE_COND_EMPTY:
		return 0;
	case BLK_ZONE_COND_FULL:
	case BLK_ZONE_COND_NOT_WP:
	case BLK_ZONE_COND_OFFLINE:
	case BLK_ZONE_COND_READONLY:
	default:
		/*
		 * Conventional, full, offline and read-only zones do not have
		 * a valid write pointer.
		 */
		return UINT_MAX;
	}
}

static unsigned int disk_zone_wplug_sync_state(struct gendisk *disk,
					       struct blk_zone *zone)
{
	struct blk_zone_wplug *zwplug;
	unsigned int wp_offset = blk_zone_wp_offset(zone);

	zwplug = disk_get_zone_wplug(disk, zone->start);
	if (zwplug) {
		unsigned long flags;

		spin_lock_irqsave(&zwplug->lock, flags);
		if (disk_zone_cond_is_offline_or_readonly(zone->cond)) {
			zwplug->flags &= ~BLK_ZONE_WPLUG_NEED_WP_UPDATE;
			zwplug->cond = zone->cond;
			zwplug->wp_offset = UINT_MAX;
			disk_mark_zone_wplug_dead(zwplug);
		}
		if (zwplug->flags & BLK_ZONE_WPLUG_NEED_WP_UPDATE)
			disk_zone_wplug_set_wp_offset(disk, zwplug, wp_offset);
		spin_unlock_irqrestore(&zwplug->lock, flags);
		disk_put_zone_wplug(zwplug);
	}

	return wp_offset;
}

/**
 * disk_report_zone - Report one zone
 * @disk:	Target disk
 * @zone:	The zone to report
 * @idx:	The index of the zone in the overall zone report
 * @args:	report zones callback and data
 *
 * Description:
 *    Helper function for block device drivers to report one zone of a zone
 *    report initiated with blkdev_report_zones(). The zone being reported is
 *    specified by @zone and used to update, if necessary, the zone write plug
 *    information for the zone. If @args specifies a user callback function,
 *    this callback is executed.
 */
int disk_report_zone(struct gendisk *disk, struct blk_zone *zone,
		     unsigned int idx, struct blk_report_zones_args *args)
{
	if (args && args->report_active) {
		/*
		 * If we come here, then this is a report zones as a fallback
		 * for a cached report. So collapse the implicit open, explicit
		 * open and closed conditions into the active zone condition.
		 */
		switch (zone->cond) {
		case BLK_ZONE_COND_IMP_OPEN:
		case BLK_ZONE_COND_EXP_OPEN:
		case BLK_ZONE_COND_CLOSED:
			zone->cond = BLK_ZONE_COND_ACTIVE;
			break;
		default:
			break;
		}
	}

	if (disk->zone_wplugs_hash)
		disk_zone_wplug_sync_state(disk, zone);

	if (args && args->cb)
		return args->cb(zone, idx, args->data);

	return 0;
}
EXPORT_SYMBOL_GPL(disk_report_zone);

static int blkdev_report_zone_cb(struct blk_zone *zone, unsigned int idx,
				 void *data)
{
	memcpy(data, zone, sizeof(struct blk_zone));
	return 0;
}

static int blkdev_report_zone_fallback(struct block_device *bdev,
				       sector_t sector, struct blk_zone *zone)
{
	struct blk_report_zones_args args = {
		.cb = blkdev_report_zone_cb,
		.data = zone,
		.report_active = true,
	};
	int error;

	error = blkdev_do_report_zones(bdev, sector, 1, &args);
	if (error < 0)
		return error;
	if (error == 0)
		return -EIO;
	return 0;
}

/*
 * For devices that natively support zone append operations, we do not use zone
 * write plugging for zone append writes, which makes the zone condition
 * tracking invalid once zone append was used.  In that case fall back to a
 * regular report zones to get correct information.
 */
static inline bool blkdev_has_cached_report_zones(struct block_device *bdev)
{
	return disk_need_zone_resources(bdev->bd_disk) &&
		(bdev_emulates_zone_append(bdev) ||
		 !test_bit(GD_ZONE_APPEND_USED, &bdev->bd_disk->state));
}

/**
 * blkdev_get_zone_info - Get a single zone information from cached data
 * @bdev:   Target block device
 * @sector: Sector contained by the target zone
 * @zone:   zone structure to return the zone information
 *
 * Description:
 *    Get the zone information for the zone containing @sector using the zone
 *    write plug of the target zone, if one exist, or the disk zone condition
 *    array otherwise. The zone condition may be reported as being
 *    the BLK_ZONE_COND_ACTIVE condition for a zone that is in the implicit
 *    open, explicit open or closed condition.
 *
 *    Returns 0 on success and a negative error code on failure.
 */
int blkdev_get_zone_info(struct block_device *bdev, sector_t sector,
			 struct blk_zone *zone)
{
	struct gendisk *disk = bdev->bd_disk;
	sector_t zone_sectors = bdev_zone_sectors(bdev);
	unsigned int zno = disk_zone_no(disk, sector);
	struct blk_zone_wplug *zwplug;
	unsigned long flags;
	u8 *zones_state, zs;

	if (!bdev_is_zoned(bdev))
		return -EOPNOTSUPP;

	if (sector >= get_capacity(disk) || zno >= disk->nr_zones)
		return -EINVAL;

	memset(zone, 0, sizeof(*zone));
	sector = bdev_zone_start(bdev, sector);

	if (!blkdev_has_cached_report_zones(bdev))
		goto fallback;

	rcu_read_lock();
	zones_state = rcu_dereference(disk->zones_state);
	if (!disk->zone_wplugs_hash || !zones_state) {
		rcu_read_unlock();
		goto fallback;
	}

	zs = zones_state[zno];
	zone->cond = blk_zstate_to_zone_cond(zs);
	if (blk_zstate_is_conv(zs))
		zone->type = BLK_ZONE_TYPE_CONVENTIONAL;
	else
		zone->type = BLK_ZONE_TYPE_SEQWRITE_REQ;
	rcu_read_unlock();

	zone->start = sector;
	zone->len = zone_sectors;

	/*
	 * If this is a conventional zone, we do not have a zone write plug and
	 * can report the zone immediately.
	 */
	if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL) {
		zone->capacity = zone_sectors;
		zone->wp = ULLONG_MAX;
		return 0;
	}

	/*
	 * This is a sequential write required zone. If the zone is read-only or
	 * offline, only set the zone write pointer to an invalid value and
	 * report the zone.
	 */
	if (disk_zone_is_last(disk, zone))
		zone->capacity = disk->last_zone_capacity;
	else
		zone->capacity = disk->zone_capacity;

	if (disk_zone_cond_is_offline_or_readonly(zone->cond)) {
		zone->wp = ULLONG_MAX;
		return 0;
	}

	/*
	 * If the zone does not have a zone write plug, it is either full or
	 * empty, as we otherwise would have a zone write plug for it. In this
	 * case, set the write pointer accordingly and report the zone.
	 * Otherwise, if we have a zone write plug, use it.
	 */
	zwplug = disk_get_zone_wplug(disk, sector);
	if (!zwplug) {
		if (zone->cond == BLK_ZONE_COND_FULL)
			zone->wp = ULLONG_MAX;
		else
			zone->wp = sector;
		return 0;
	}

	spin_lock_irqsave(&zwplug->lock, flags);
	if (zwplug->flags & BLK_ZONE_WPLUG_NEED_WP_UPDATE) {
		spin_unlock_irqrestore(&zwplug->lock, flags);
		disk_put_zone_wplug(zwplug);
		return blkdev_report_zone_fallback(bdev, sector, zone);
	}
	zone->cond = zwplug->cond;
	zone->wp = sector + zwplug->wp_offset;
	spin_unlock_irqrestore(&zwplug->lock, flags);

	disk_put_zone_wplug(zwplug);

	return 0;

fallback:
	return blkdev_report_zone_fallback(bdev, sector, zone);
}
EXPORT_SYMBOL_GPL(blkdev_get_zone_info);

/**
 * blkdev_report_zones_cached - Get cached zones information
 * @bdev:     Target block device
 * @sector:   Sector from which to report zones
 * @nr_zones: Maximum number of zones to report
 * @cb:       Callback function called for each reported zone
 * @data:     Private data for the callback function
 *
 * Description:
 *    Similar to blkdev_report_zones() but instead of calling into the low level
 *    device driver to get the zone report from the device, use
 *    blkdev_get_zone_info() to generate the report from the disk zone write
 *    plugs and zones condition array. Since calling this function without a
 *    callback does not make sense, @cb must be specified.
 */
int blkdev_report_zones_cached(struct block_device *bdev, sector_t sector,
			unsigned int nr_zones, report_zones_cb cb, void *data)
{
	struct gendisk *disk = bdev->bd_disk;
	sector_t capacity = get_capacity(disk);
	sector_t zone_sectors = bdev_zone_sectors(bdev);
	unsigned int idx = 0;
	struct blk_zone zone;
	int ret;

	if (!cb || !bdev_is_zoned(bdev) ||
	    WARN_ON_ONCE(!disk->fops->report_zones))
		return -EOPNOTSUPP;

	if (!nr_zones || sector >= capacity)
		return 0;

	if (!blkdev_has_cached_report_zones(bdev)) {
		struct blk_report_zones_args args = {
			.cb = cb,
			.data = data,
			.report_active = true,
		};

		return blkdev_do_report_zones(bdev, sector, nr_zones, &args);
	}

	for (sector = bdev_zone_start(bdev, sector);
	     sector < capacity && idx < nr_zones;
	     sector += zone_sectors, idx++) {
		ret = blkdev_get_zone_info(bdev, sector, &zone);
		if (ret)
			return ret;

		ret = cb(&zone, idx, data);
		if (ret)
			return ret;
	}

	return idx;
}
EXPORT_SYMBOL_GPL(blkdev_report_zones_cached);

static void blk_zone_reset_bio_endio(struct bio *bio)
{
	struct gendisk *disk = bio->bi_bdev->bd_disk;
	sector_t sector = bio->bi_iter.bi_sector;
	struct blk_zone_wplug *zwplug;

	/*
	 * If we have a zone write plug, set its write pointer offset to 0.
	 * This will abort all BIOs plugged for the target zone. It is fine as
	 * resetting zones while writes are still in-flight will result in the
	 * writes failing anyway.
	 */
	zwplug = disk_get_zone_wplug(disk, sector);
	if (zwplug) {
		unsigned long flags;

		spin_lock_irqsave(&zwplug->lock, flags);
		disk_zone_wplug_set_wp_offset(disk, zwplug, 0);
		spin_unlock_irqrestore(&zwplug->lock, flags);
		disk_put_zone_wplug(zwplug);
	} else {
		disk_zone_set_cond(disk, sector, BLK_ZONE_COND_EMPTY);
	}
}

static void disk_zone_wplug_reset_wp(struct blk_zone_wplug *zwplug, void *data)
{
	unsigned long flags;

	spin_lock_irqsave(&zwplug->lock, flags);
	disk_zone_wplug_set_wp_offset(zwplug->disk, zwplug, 0);
	spin_unlock_irqrestore(&zwplug->lock, flags);
}

static void blk_zone_reset_all_bio_endio(struct bio *bio)
{
	struct gendisk *disk = bio->bi_bdev->bd_disk;
	sector_t sector;

	/* Update the condition of all zone write plugs. */
	if (atomic_read(&disk->nr_zone_wplugs))
		disk_for_all_zone_wplugs(disk, disk_zone_wplug_reset_wp, NULL);

	/* Update the cached zone conditions. */
	for (sector = 0; sector < get_capacity(disk);
	     sector += bdev_zone_sectors(bio->bi_bdev)) {
		if (!disk_zone_is_seq(disk, sector) ||
		    disk_zone_is_offline_or_readonly(disk, sector))
			continue;
		disk_zone_set_cond(disk, sector, BLK_ZONE_COND_EMPTY);
	}
	clear_bit(GD_ZONE_APPEND_USED, &disk->state);
}

static void blk_zone_finish_bio_endio(struct bio *bio)
{
	struct block_device *bdev = bio->bi_bdev;
	struct gendisk *disk = bdev->bd_disk;
	sector_t sector = bio->bi_iter.bi_sector;
	struct blk_zone_wplug *zwplug;

	/*
	 * If we have a zone write plug, set its write pointer offset to the
	 * zone size. This will abort all BIOs plugged for the target zone. It
	 * is fine as resetting zones while writes are still in-flight will
	 * result in the writes failing anyway.
	 */
	zwplug = disk_get_zone_wplug(disk, sector);
	if (zwplug) {
		unsigned long flags;

		spin_lock_irqsave(&zwplug->lock, flags);
		disk_zone_wplug_set_wp_offset(disk, zwplug,
					      bdev_zone_sectors(bdev));
		spin_unlock_irqrestore(&zwplug->lock, flags);
		disk_put_zone_wplug(zwplug);
	} else {
		disk_zone_set_cond(disk, sector, BLK_ZONE_COND_FULL);
	}
}

void blk_zone_mgmt_bio_endio(struct bio *bio)
{
	/* If the BIO failed, we have nothing to do. */
	if (bio->bi_status != BLK_STS_OK)
		return;

	switch (bio_op(bio)) {
	case REQ_OP_ZONE_RESET:
		blk_zone_reset_bio_endio(bio);
		return;
	case REQ_OP_ZONE_RESET_ALL:
		blk_zone_reset_all_bio_endio(bio);
		return;
	case REQ_OP_ZONE_FINISH:
		blk_zone_finish_bio_endio(bio);
		return;
	default:
		return;
	}
}

static void disk_zone_wplug_schedule_work(struct gendisk *disk,
					  struct blk_zone_wplug *zwplug)
{
	lockdep_assert_held(&zwplug->lock);

	/*
	 * Schedule the submission of the next plugged BIO. Taking a reference
	 * to the zone write plug is required as the bio_work belongs to the
	 * plug, and thus we must ensure that the write plug does not go away
	 * while the work is being scheduled but has not run yet.
	 * blk_zone_wplug_bio_work() will release the reference we take here,
	 * and we also drop this reference if the work is already scheduled.
	 */
	WARN_ON_ONCE(!(zwplug->flags & BLK_ZONE_WPLUG_PLUGGED));
	WARN_ON_ONCE(blk_queue_zoned_qd1_writes(disk->queue));
	refcount_inc(&zwplug->ref);
	if (!queue_work(disk->zone_wplugs_wq, &zwplug->bio_work))
		disk_put_zone_wplug(zwplug);
}

static inline void disk_zone_wplug_add_bio(struct gendisk *disk,
				struct blk_zone_wplug *zwplug,
				struct bio *bio, unsigned int nr_segs)
{
	/*
	 * Grab an extra reference on the BIO request queue usage counter.
	 * This reference will be reused to submit a request for the BIO for
	 * blk-mq devices and dropped when the BIO is failed and after
	 * it is issued in the case of BIO-based devices.
	 */
	percpu_ref_get(&bio->bi_bdev->bd_disk->queue->q_usage_counter);

	/*
	 * The BIO is being plugged and thus will have to wait for the on-going
	 * write and for all other writes already plugged. So polling makes
	 * no sense.
	 */
	bio_clear_polled(bio);

	/*
	 * Reuse the poll cookie field to store the number of segments when
	 * split to the hardware limits.
	 */
	bio->__bi_nr_segments = nr_segs;

	/*
	 * We always receive BIOs after they are split and ready to be issued.
	 * The block layer passes the parts of a split BIO in order, and the
	 * user must also issue write sequentially. So simply add the new BIO
	 * at the tail of the list to preserve the sequential write order.
	 */
	bio_list_add(&zwplug->bio_list, bio);
	trace_disk_zone_wplug_add_bio(zwplug->disk->queue, zwplug->zone_no,
				      bio->bi_iter.bi_sector, bio_sectors(bio));

	/*
	 * If we are using the disk zone write plugs worker instead of the per
	 * zone write plug BIO work, add the zone write plug to the work list
	 * if it is not already there. Make sure to also get an extra reference
	 * on the zone write plug so that it does not go away until it is
	 * removed from the work list.
	 */
	if (blk_queue_zoned_qd1_writes(disk->queue)) {
		spin_lock(&disk->zone_wplugs_list_lock);
		if (list_empty(&zwplug->entry)) {
			list_add_tail(&zwplug->entry, &disk->zone_wplugs_list);
			refcount_inc(&zwplug->ref);
		}
		spin_unlock(&disk->zone_wplugs_list_lock);
	}
}

/*
 * Called from bio_attempt_back_merge() when a BIO was merged with a request.
 */
void blk_zone_write_plug_bio_merged(struct bio *bio)
{
	struct gendisk *disk = bio->bi_bdev->bd_disk;
	struct blk_zone_wplug *zwplug;
	unsigned long flags;

	/*
	 * If the BIO was already plugged, then we were called through
	 * blk_zone_write_plug_init_request() -> blk_attempt_bio_merge().
	 * For this case, we already hold a reference on the zone write plug for
	 * the BIO and blk_zone_write_plug_init_request() will handle the
	 * zone write pointer offset update.
	 */
	if (bio_flagged(bio, BIO_ZONE_WRITE_PLUGGING))
		return;

	bio_set_flag(bio, BIO_ZONE_WRITE_PLUGGING);

	/*
	 * Get a reference on the zone write plug of the target zone and advance
	 * the zone write pointer offset. Given that this is a merge, we already
	 * have at least one request and one BIO referencing the zone write
	 * plug. So this should not fail.
	 */
	zwplug = disk_get_zone_wplug(disk, bio->bi_iter.bi_sector);
	if (WARN_ON_ONCE(!zwplug))
		return;

	spin_lock_irqsave(&zwplug->lock, flags);
	zwplug->wp_offset += bio_sectors(bio);
	disk_zone_wplug_update_cond(disk, zwplug);
	spin_unlock_irqrestore(&zwplug->lock, flags);
}

/*
 * Attempt to merge plugged BIOs with a newly prepared request for a BIO that
 * already went through zone write plugging (either a new BIO or one that was
 * unplugged).
 */
void blk_zone_write_plug_init_request(struct request *req)
{
	sector_t req_back_sector = blk_rq_pos(req) + blk_rq_sectors(req);
	struct request_queue *q = req->q;
	struct gendisk *disk = q->disk;
	struct blk_zone_wplug *zwplug =
		disk_get_zone_wplug(disk, blk_rq_pos(req));
	unsigned long flags;
	struct bio *bio;

	if (WARN_ON_ONCE(!zwplug))
		return;

	/*
	 * Indicate that completion of this request needs to be handled with
	 * blk_zone_write_plug_finish_request(), which will drop the reference
	 * on the zone write plug we took above on entry to this function.
	 */
	req->rq_flags |= RQF_ZONE_WRITE_PLUGGING;

	if (blk_queue_nomerges(q))
		return;

	/*
	 * Walk through the list of plugged BIOs to check if they can be merged
	 * into the back of the request.
	 */
	spin_lock_irqsave(&zwplug->lock, flags);
	while (!disk_zone_wplug_is_full(disk, zwplug)) {
		bio = bio_list_peek(&zwplug->bio_list);
		if (!bio)
			break;

		if (bio->bi_iter.bi_sector != req_back_sector ||
		    !blk_rq_merge_ok(req, bio))
			break;

		WARN_ON_ONCE(bio_op(bio) != REQ_OP_WRITE_ZEROES &&
			     !bio->__bi_nr_segments);

		bio_list_pop(&zwplug->bio_list);
		if (bio_attempt_back_merge(req, bio, bio->__bi_nr_segments) !=
		    BIO_MERGE_OK) {
			bio_list_add_head(&zwplug->bio_list, bio);
			break;
		}

		/* Drop the reference taken by disk_zone_wplug_add_bio(). */
		blk_queue_exit(q);
		zwplug->wp_offset += bio_sectors(bio);
		disk_zone_wplug_update_cond(disk, zwplug);

		req_back_sector += bio_sectors(bio);
	}
	spin_unlock_irqrestore(&zwplug->lock, flags);
}

/*
 * Check and prepare a BIO for submission by incrementing the write pointer
 * offset of its zone write plug and changing zone append operations into
 * regular write when zone append emulation is needed.
 */
static bool blk_zone_wplug_prepare_bio(struct blk_zone_wplug *zwplug,
				       struct bio *bio)
{
	struct gendisk *disk = bio->bi_bdev->bd_disk;

	lockdep_assert_held(&zwplug->lock);

	/*
	 * If we lost track of the zone write pointer due to a write error,
	 * the user must either execute a report zones, reset the zone or finish
	 * the to recover a reliable write pointer position. Fail BIOs if the
	 * user did not do that as we cannot handle emulated zone append
	 * otherwise.
	 */
	if (zwplug->flags & BLK_ZONE_WPLUG_NEED_WP_UPDATE)
		return false;

	/*
	 * Check that the user is not attempting to write to a full, read-only
	 * or offline zone. We know such BIOs will fail, so there is no point
	 * in issuing them.
	 */
	if (disk_zone_wplug_is_full(disk, zwplug) ||
	    disk_zone_wplug_is_offline_or_readonly(zwplug))
		return false;

	if (bio_op(bio) == REQ_OP_ZONE_APPEND) {
		/*
		 * Use a regular write starting at the current write pointer.
		 * Similarly to native zone append operations, do not allow
		 * merging.
		 */
		bio->bi_opf &= ~REQ_OP_MASK;
		bio->bi_opf |= REQ_OP_WRITE | REQ_NOMERGE;
		bio->bi_iter.bi_sector += zwplug->wp_offset;

		/*
		 * Remember that this BIO is in fact a zone append operation
		 * so that we can restore its operation code on completion.
		 */
		bio_set_flag(bio, BIO_EMULATES_ZONE_APPEND);
	} else {
		/*
		 * Check for non-sequential writes early as we know that BIOs
		 * with a start sector not unaligned to the zone write pointer
		 * will fail.
		 */
		if (bio_offset_from_zone_start(bio) != zwplug->wp_offset)
			return false;
	}

	/* Advance the zone write pointer offset. */
	zwplug->wp_offset += bio_sectors(bio);
	disk_zone_wplug_update_cond(disk, zwplug);

	return true;
}

static bool blk_zone_wplug_handle_write(struct bio *bio, unsigned int nr_segs)
{
	struct gendisk *disk = bio->bi_bdev->bd_disk;
	sector_t sector = bio->bi_iter.bi_sector;
	struct blk_zone_wplug *zwplug;
	gfp_t gfp_mask = GFP_NOIO;
	unsigned long flags;

	/*
	 * BIOs must be fully contained within a zone so that we use the correct
	 * zone write plug for the entire BIO. For blk-mq devices, the block
	 * layer should already have done any splitting required to ensure this
	 * and this BIO should thus not be straddling zone boundaries. For
	 * BIO-based devices, it is the responsibility of the driver to split
	 * the bio before submitting it.
	 */
	if (WARN_ON_ONCE(bio_straddles_zones(bio))) {
		bio_io_error(bio);
		return true;
	}

	/* Conventional zones do not need write plugging. */
	if (!disk_zone_is_seq(disk, sector)) {
		/* Zone append to conventional zones is not allowed. */
		if (bio_op(bio) == REQ_OP_ZONE_APPEND) {
			bio_io_error(bio);
			return true;
		}
		return false;
	}

	if (bio->bi_opf & REQ_NOWAIT)
		gfp_mask = GFP_NOWAIT;

	zwplug = disk_get_or_alloc_zone_wplug(disk, sector, gfp_mask);
	if (!zwplug) {
		if (bio->bi_opf & REQ_NOWAIT)
			bio_wouldblock_error(bio);
		else
			bio_io_error(bio);
		return true;
	}

	spin_lock_irqsave(&zwplug->lock, flags);

	/*
	 * Check if we got a zone write plug marked as dead. If yes, then the
	 * user is likely issuing writes to a full zone, or without
	 * synchronizing with zone reset or zone finish operations. In such
	 * case, fail the BIO to signal this invalid usage.
	 */
	if (disk_check_zone_wplug_dead(zwplug)) {
		spin_unlock_irqrestore(&zwplug->lock, flags);
		disk_put_zone_wplug(zwplug);
		bio_io_error(bio);
		return true;
	}

	/* Indicate that this BIO is being handled using zone write plugging. */
	bio_set_flag(bio, BIO_ZONE_WRITE_PLUGGING);

	/*
	 * Add REQ_NOWAIT BIOs to the plug list to ensure that we will not see a
	 * BLK_STS_AGAIN failure if we let the caller submit the BIO.
	 */
	if (bio->bi_opf & REQ_NOWAIT) {
		bio->bi_opf &= ~REQ_NOWAIT;
		goto queue_bio;
	}

	/*
	 * For rotational devices, we will use the gendisk zone write plugs
	 * work instead of the per zone write plug BIO work, so queue the BIO.
	 */
	if (blk_queue_zoned_qd1_writes(disk->queue))
		goto queue_bio;

	/* If the zone is already plugged, add the BIO to the BIO plug list. */
	if (zwplug->flags & BLK_ZONE_WPLUG_PLUGGED)
		goto queue_bio;

	if (!blk_zone_wplug_prepare_bio(zwplug, bio)) {
		spin_unlock_irqrestore(&zwplug->lock, flags);
		bio_io_error(bio);
		return true;
	}

	/* Otherwise, plug and let the caller submit the BIO. */
	zwplug->flags |= BLK_ZONE_WPLUG_PLUGGED;

	spin_unlock_irqrestore(&zwplug->lock, flags);

	return false;

queue_bio:
	disk_zone_wplug_add_bio(disk, zwplug, bio, nr_segs);

	if (!(zwplug->flags & BLK_ZONE_WPLUG_PLUGGED)) {
		zwplug->flags |= BLK_ZONE_WPLUG_PLUGGED;
		if (blk_queue_zoned_qd1_writes(disk->queue))
			wake_up_process(disk->zone_wplugs_worker);
		else
			disk_zone_wplug_schedule_work(disk, zwplug);
	}

	spin_unlock_irqrestore(&zwplug->lock, flags);

	return true;
}

static void blk_zone_wplug_handle_native_zone_append(struct bio *bio)
{
	struct gendisk *disk = bio->bi_bdev->bd_disk;
	struct blk_zone_wplug *zwplug;
	unsigned long flags;

	if (!test_bit(GD_ZONE_APPEND_USED, &disk->state))
		set_bit(GD_ZONE_APPEND_USED, &disk->state);

	/*
	 * We have native support for zone append operations, so we are not
	 * going to handle @bio through plugging. However, we may already have a
	 * zone write plug for the target zone if that zone was previously
	 * partially written using regular writes. In such case, we risk leaving
	 * the plug in the disk hash table if the zone is fully written using
	 * zone append operations. Avoid this by removing the zone write plug.
	 */
	zwplug = disk_get_zone_wplug(disk, bio->bi_iter.bi_sector);
	if (likely(!zwplug))
		return;

	spin_lock_irqsave(&zwplug->lock, flags);

	/*
	 * We are about to remove the zone write plug. But if the user
	 * (mistakenly) has issued regular writes together with native zone
	 * append, we must aborts the writes as otherwise the plugged BIOs would
	 * not be executed by the plug BIO work as disk_get_zone_wplug() will
	 * return NULL after the plug is removed. Aborting the plugged write
	 * BIOs is consistent with the fact that these writes will most likely
	 * fail anyway as there is no ordering guarantees between zone append
	 * operations and regular write operations.
	 */
	if (!bio_list_empty(&zwplug->bio_list)) {
		pr_warn_ratelimited("%s: zone %u: Invalid mix of zone append and regular writes\n",
				    disk->disk_name, zwplug->zone_no);
		disk_zone_wplug_abort(zwplug);
	}
	disk_mark_zone_wplug_dead(zwplug);
	spin_unlock_irqrestore(&zwplug->lock, flags);

	disk_put_zone_wplug(zwplug);
}

/**
 * blk_zone_plug_bio - Handle a zone write BIO with zone write plugging
 * @bio: The BIO being submitted
 * @nr_segs: The number of physical segments of @bio
 *
 * Handle write, write zeroes and zone append operations requiring emulation
 * using zone write plugging.
 *
 * Return true whenever @bio execution needs to be delayed through the zone
 * write plug. Otherwise, return false to let the submission path process
 * @bio normally.
 */
bool blk_zone_plug_bio(struct bio *bio, unsigned int nr_segs)
{
	struct block_device *bdev = bio->bi_bdev;

	if (WARN_ON_ONCE(!bdev->bd_disk->zone_wplugs_hash))
		return false;

	/*
	 * Regular writes and write zeroes need to be handled through the target
	 * zone write plug. This includes writes with REQ_FUA | REQ_PREFLUSH
	 * which may need to go through the flush machinery depending on the
	 * target device capabilities. Plugging such writes is fine as the flush
	 * machinery operates at the request level, below the plug, and
	 * completion of the flush sequence will go through the regular BIO
	 * completion, which will handle zone write plugging.
	 * Zone append operations for devices that requested emulation must
	 * also be plugged so that these BIOs can be changed into regular
	 * write BIOs.
	 * Zone reset, reset all and finish commands need special treatment
	 * to correctly track the write pointer offset of zones. These commands
	 * are not plugged as we do not need serialization with write
	 * operations. It is the responsibility of the user to not issue reset
	 * and finish commands when write operations are in flight.
	 */
	switch (bio_op(bio)) {
	case REQ_OP_ZONE_APPEND:
		if (!bdev_emulates_zone_append(bdev)) {
			blk_zone_wplug_handle_native_zone_append(bio);
			return false;
		}
		fallthrough;
	case REQ_OP_WRITE:
	case REQ_OP_WRITE_ZEROES:
		return blk_zone_wplug_handle_write(bio, nr_segs);
	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(blk_zone_plug_bio);

static void disk_zone_wplug_unplug_bio(struct gendisk *disk,
				       struct blk_zone_wplug *zwplug)
{
	unsigned long flags;

	spin_lock_irqsave(&zwplug->lock, flags);

	/*
	 * For rotational devices, signal the BIO completion to the zone write
	 * plug work. Otherwise, schedule submission of the next plugged BIO
	 * if we have one.
	 */
	if (bio_list_empty(&zwplug->bio_list))
		zwplug->flags &= ~BLK_ZONE_WPLUG_PLUGGED;

	if (blk_queue_zoned_qd1_writes(disk->queue))
		complete(&disk->zone_wplugs_worker_bio_done);
	else if (!bio_list_empty(&zwplug->bio_list))
		disk_zone_wplug_schedule_work(disk, zwplug);

	if (!zwplug->wp_offset || disk_zone_wplug_is_full(disk, zwplug))
		disk_mark_zone_wplug_dead(zwplug);

	spin_unlock_irqrestore(&zwplug->lock, flags);
}

void blk_zone_append_update_request_bio(struct request *rq, struct bio *bio)
{
	/*
	 * For zone append requests, the request sector indicates the location
	 * at which the BIO data was written. Return this value to the BIO
	 * issuer through the BIO iter sector.
	 * For plugged zone writes, which include emulated zone append, we need
	 * the original BIO sector so that blk_zone_write_plug_bio_endio() can
	 * lookup the zone write plug.
	 */
	bio->bi_iter.bi_sector = rq->__sector;
	trace_blk_zone_append_update_request_bio(rq);
}

void blk_zone_write_plug_bio_endio(struct bio *bio)
{
	struct gendisk *disk = bio->bi_bdev->bd_disk;
	struct blk_zone_wplug *zwplug =
		disk_get_zone_wplug(disk, bio->bi_iter.bi_sector);
	unsigned long flags;

	if (WARN_ON_ONCE(!zwplug))
		return;

	/* Make sure we do not see this BIO again by clearing the plug flag. */
	bio_clear_flag(bio, BIO_ZONE_WRITE_PLUGGING);

	/*
	 * If this is a regular write emulating a zone append operation,
	 * restore the original operation code.
	 */
	if (bio_flagged(bio, BIO_EMULATES_ZONE_APPEND)) {
		bio->bi_opf &= ~REQ_OP_MASK;
		bio->bi_opf |= REQ_OP_ZONE_APPEND;
		bio_clear_flag(bio, BIO_EMULATES_ZONE_APPEND);
	}

	/*
	 * If the BIO failed, abort all plugged BIOs and mark the plug as
	 * needing a write pointer update.
	 */
	if (bio->bi_status != BLK_STS_OK) {
		spin_lock_irqsave(&zwplug->lock, flags);
		disk_zone_wplug_abort(zwplug);
		zwplug->flags |= BLK_ZONE_WPLUG_NEED_WP_UPDATE;
		spin_unlock_irqrestore(&zwplug->lock, flags);
	}

	/* Drop the reference we took when the BIO was issued. */
	disk_put_zone_wplug(zwplug);

	/*
	 * For BIO-based devices, blk_zone_write_plug_finish_request()
	 * is not called. So we need to schedule execution of the next
	 * plugged BIO here.
	 */
	if (bdev_test_flag(bio->bi_bdev, BD_HAS_SUBMIT_BIO))
		disk_zone_wplug_unplug_bio(disk, zwplug);

	/* Drop the reference we took when entering this function. */
	disk_put_zone_wplug(zwplug);
}

void blk_zone_write_plug_finish_request(struct request *req)
{
	struct gendisk *disk = req->q->disk;
	struct blk_zone_wplug *zwplug;

	zwplug = disk_get_zone_wplug(disk, req->__sector);
	if (WARN_ON_ONCE(!zwplug))
		return;

	req->rq_flags &= ~RQF_ZONE_WRITE_PLUGGING;

	/*
	 * Drop the reference we took when the request was initialized in
	 * blk_zone_write_plug_init_request().
	 */
	disk_put_zone_wplug(zwplug);

	disk_zone_wplug_unplug_bio(disk, zwplug);

	/* Drop the reference we took when entering this function. */
	disk_put_zone_wplug(zwplug);
}

static bool disk_zone_wplug_submit_bio(struct gendisk *disk,
				       struct blk_zone_wplug *zwplug)
{
	struct block_device *bdev;
	unsigned long flags;
	struct bio *bio;
	bool prepared;

	/*
	 * Submit the next plugged BIO. If we do not have any, clear
	 * the plugged flag.
	 */
again:
	spin_lock_irqsave(&zwplug->lock, flags);
	bio = bio_list_pop(&zwplug->bio_list);
	if (!bio) {
		zwplug->flags &= ~BLK_ZONE_WPLUG_PLUGGED;
		spin_unlock_irqrestore(&zwplug->lock, flags);
		return false;
	}

	trace_blk_zone_wplug_bio(zwplug->disk->queue, zwplug->zone_no,
				 bio->bi_iter.bi_sector, bio_sectors(bio));

	prepared = blk_zone_wplug_prepare_bio(zwplug, bio);
	spin_unlock_irqrestore(&zwplug->lock, flags);

	if (!prepared) {
		blk_zone_wplug_bio_io_error(zwplug, bio);
		goto again;
	}

	/*
	 * blk-mq devices will reuse the extra reference on the request queue
	 * usage counter we took when the BIO was plugged, but the submission
	 * path for BIO-based devices will not do that. So drop this extra
	 * reference here.
	 */
	if (blk_queue_zoned_qd1_writes(disk->queue))
		reinit_completion(&disk->zone_wplugs_worker_bio_done);
	bdev = bio->bi_bdev;
	if (bdev_test_flag(bdev, BD_HAS_SUBMIT_BIO)) {
		bdev->bd_disk->fops->submit_bio(bio);
		blk_queue_exit(bdev->bd_disk->queue);
	} else {
		blk_mq_submit_bio(bio);
	}

	return true;
}

static struct blk_zone_wplug *disk_get_zone_wplugs_work(struct gendisk *disk)
{
	struct blk_zone_wplug *zwplug;

	spin_lock_irq(&disk->zone_wplugs_list_lock);
	zwplug = list_first_entry_or_null(&disk->zone_wplugs_list,
					  struct blk_zone_wplug, entry);
	if (zwplug)
		list_del_init(&zwplug->entry);
	spin_unlock_irq(&disk->zone_wplugs_list_lock);

	return zwplug;
}

static int disk_zone_wplugs_worker(void *data)
{
	struct gendisk *disk = data;
	struct blk_zone_wplug *zwplug;
	unsigned int noio_flag;

	noio_flag = memalloc_noio_save();
	set_user_nice(current, MIN_NICE);
	set_freezable();

	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE | TASK_FREEZABLE);

		zwplug = disk_get_zone_wplugs_work(disk);
		if (zwplug) {
			/*
			 * Process all BIOs of this zone write plug and then
			 * drop the reference we took when adding the zone write
			 * plug to the active list.
			 */
			set_current_state(TASK_RUNNING);
			while (disk_zone_wplug_submit_bio(disk, zwplug))
				blk_wait_io(&disk->zone_wplugs_worker_bio_done);
			disk_put_zone_wplug(zwplug);
			continue;
		}

		/*
		 * Only sleep if nothing sets the state to running. Else check
		 * for zone write plugs work again as a newly submitted BIO
		 * might have added a zone write plug to the work list.
		 */
		if (get_current_state() == TASK_RUNNING) {
			try_to_freeze();
		} else {
			if (kthread_should_stop()) {
				set_current_state(TASK_RUNNING);
				break;
			}
			schedule();
		}
	}

	WARN_ON_ONCE(!list_empty(&disk->zone_wplugs_list));
	memalloc_noio_restore(noio_flag);

	return 0;
}

void disk_init_zone_resources(struct gendisk *disk)
{
	mutex_init(&disk->zone_revalidate_mutex);
	atomic_set(&disk->nr_zone_wplugs, 0);
	spin_lock_init(&disk->zone_wplugs_hash_lock);
	spin_lock_init(&disk->zone_wplugs_list_lock);
	INIT_LIST_HEAD(&disk->zone_wplugs_list);
	init_completion(&disk->zone_wplugs_worker_bio_done);
}

static unsigned int disk_get_nr_zones(struct gendisk *disk, sector_t capacity)
{
	struct queue_limits *lim = &disk->queue->limits;
	unsigned long long nr_zones;

	if (!capacity || !lim->chunk_sectors)
		return 0;

	nr_zones = DIV_ROUND_UP_ULL(capacity, lim->chunk_sectors);
	if (nr_zones > UINT_MAX) {
		pr_warn("%s: Too many zones (%llu)\n", disk->disk_name, nr_zones);
		return 0;
	}

	return nr_zones;
}

/*
 * For the size of a disk zone write plug hash table, use the size of the
 * zone write plug mempool, which is the maximum of the disk open zones and
 * active zones limits. But do not exceed 4KB (512 hlist head entries), that is,
 * 9 bits. For a disk that has no limits, mempool size defaults to 128.
 */
#define BLK_ZONE_WPLUG_MAX_HASH_BITS		9
#define BLK_ZONE_WPLUG_DEFAULT_POOL_SIZE	128

static int disk_alloc_zone_resources(struct gendisk *disk, sector_t capacity)
{
	struct queue_limits *lim = &disk->queue->limits;
	unsigned int nr_zones, pool_size, i;
	int ret = -ENOMEM;

	nr_zones = disk_get_nr_zones(disk, capacity);
	if (!nr_zones)
		return -ENODEV;

	/*
	 * If the device has no limit on the maximum number of open and active
	 * zones, use BLK_ZONE_WPLUG_DEFAULT_POOL_SIZE.
	 */
	pool_size = max(lim->max_open_zones, lim->max_active_zones);
	if (!pool_size)
		pool_size = min(BLK_ZONE_WPLUG_DEFAULT_POOL_SIZE, nr_zones);

	disk->zone_wplugs_hash_bits =
		min(ilog2(pool_size) + 1, BLK_ZONE_WPLUG_MAX_HASH_BITS);

	disk->zone_wplugs_hash =
		kzalloc_objs(struct hlist_head,
			     disk_zone_wplugs_hash_size(disk));
	if (!disk->zone_wplugs_hash)
		return -ENOMEM;

	for (i = 0; i < disk_zone_wplugs_hash_size(disk); i++)
		INIT_HLIST_HEAD(&disk->zone_wplugs_hash[i]);

	disk->zone_wplugs_pool = mempool_create_kmalloc_pool(pool_size,
						sizeof(struct blk_zone_wplug));
	if (!disk->zone_wplugs_pool)
		goto free_hash;

	disk->zone_wplugs_worker =
		kthread_create(disk_zone_wplugs_worker, disk,
			       "%s_zwplugs_worker", disk->disk_name);
	if (IS_ERR(disk->zone_wplugs_worker)) {
		ret = PTR_ERR(disk->zone_wplugs_worker);
		disk->zone_wplugs_worker = NULL;
		goto destroy_pool;
	}
	wake_up_process(disk->zone_wplugs_worker);

	disk->zone_wplugs_wq =
		alloc_workqueue("%s_zwplugs",
				WQ_MEM_RECLAIM | WQ_HIGHPRI | WQ_PERCPU,
				pool_size, disk->disk_name);
	if (!disk->zone_wplugs_wq)
		goto stop_worker;

	return 0;

stop_worker:
	kthread_stop(disk->zone_wplugs_worker);
	disk->zone_wplugs_worker = NULL;
destroy_pool:
	mempool_destroy(disk->zone_wplugs_pool);
	disk->zone_wplugs_pool = NULL;
free_hash:
	kfree(disk->zone_wplugs_hash);
	disk->zone_wplugs_hash = NULL;
	disk->zone_wplugs_hash_bits = 0;
	return ret;
}

static void disk_destroy_zone_wplugs_hash_table(struct gendisk *disk)
{
	struct blk_zone_wplug *zwplug;
	unsigned int i;

	if (!disk->zone_wplugs_hash)
		return;

	/* Free all the zone write plugs we have. */
	for (i = 0; i < disk_zone_wplugs_hash_size(disk); i++) {
		while (!hlist_empty(&disk->zone_wplugs_hash[i])) {
			zwplug = hlist_entry(disk->zone_wplugs_hash[i].first,
					     struct blk_zone_wplug, node);
			spin_lock_irq(&zwplug->lock);
			disk_mark_zone_wplug_dead(zwplug);
			spin_unlock_irq(&zwplug->lock);
		}
	}

	WARN_ON_ONCE(atomic_read(&disk->nr_zone_wplugs));
	kfree(disk->zone_wplugs_hash);
	disk->zone_wplugs_hash = NULL;
	disk->zone_wplugs_hash_bits = 0;

	/*
	 * Wait for the zone write plugs to be RCU-freed before destroying the
	 * mempool.
	 */
	rcu_barrier();
	mempool_destroy(disk->zone_wplugs_pool);
	disk->zone_wplugs_pool = NULL;
}

static void disk_set_zones_state_array(struct gendisk *disk, u8 *zones_state)
{
	unsigned long flags;

	spin_lock_irqsave(&disk->zone_wplugs_hash_lock, flags);
	zones_state = rcu_replace_pointer(disk->zones_state, zones_state,
				lockdep_is_held(&disk->zone_wplugs_hash_lock));
	spin_unlock_irqrestore(&disk->zone_wplugs_hash_lock, flags);

	kfree_rcu_mightsleep(zones_state);
}

void disk_release_zone_resources(struct gendisk *disk)
{
	if (disk->zone_wplugs_worker) {
		kthread_stop(disk->zone_wplugs_worker);
		disk->zone_wplugs_worker = NULL;
	}
	WARN_ON_ONCE(!list_empty(&disk->zone_wplugs_list));

	if (disk->zone_wplugs_wq) {
		destroy_workqueue(disk->zone_wplugs_wq);
		disk->zone_wplugs_wq = NULL;
	}

	disk_destroy_zone_wplugs_hash_table(disk);

	disk_set_zones_state_array(disk, NULL);
	disk->zone_capacity = 0;
	disk->last_zone_capacity = 0;
	disk->nr_zones = 0;
	mutex_destroy(&disk->zone_revalidate_mutex);
}

struct blk_revalidate_zone_args {
	struct gendisk	*disk;
	sector_t	capacity;
	u8		*zones_state;
	unsigned int	nr_zones;
	unsigned int	nr_conv_zones;
	unsigned int	zone_capacity;
	unsigned int	last_zone_capacity;
	sector_t	sector;
};

static int disk_init_revalidate_args(struct gendisk *disk,
				     struct blk_revalidate_zone_args *args)
{
	args->disk = disk;
	args->nr_zones = disk_get_nr_zones(disk, args->capacity);
	if (!args->nr_zones)
		return -ENODEV;

	/* Cached zone conditions: 1 byte per zone */
	args->zones_state = kzalloc(args->nr_zones, GFP_NOIO);
	if (!args->zones_state)
		return -ENOMEM;

	return 0;
}

/*
 * Revalidate and update the disk zone resources information and device queue
 * limits.
 */
static int disk_revalidate_zone_resources(struct gendisk *disk,
					  struct blk_revalidate_zone_args *args)
{
	struct request_queue *q = disk->queue;
	unsigned int nr_seq_zones;
	unsigned int pool_size, memflags;
	struct queue_limits lim;
	sector_t capacity;
	int ret = 0;

	lim = queue_limits_start_update(q);

	memflags = blk_mq_freeze_queue(q);

	/*
	 * Using the re-evaluated disk capacity, make sure that the entire disk
	 * has been checked.
	 */
	capacity = get_capacity(disk);
	if (args->capacity != capacity) {
		pr_warn("%s: Capacity has changed (%llu -> %llu)\n",
			disk->disk_name, args->capacity, capacity);
		/* Force a retry if we have a valid (non-zero) capacity. */
		if (capacity)
			ret = -EAGAIN;
		else
			ret = -ENODEV;
		goto unfreeze;
	}

	/* Make sure that all zones have been checked. */
	if (args->sector != capacity) {
		pr_warn("%s: last zone and capacity mismatch (%llu != %llu)\n",
			disk->disk_name, args->sector, capacity);
		ret = -ENODEV;
		goto unfreeze;
	}

	if (args->nr_conv_zones >= args->nr_zones) {
		pr_warn("%s: Invalid number of conventional zones %u / %u\n",
			disk->disk_name, args->nr_conv_zones, args->nr_zones);
		ret = -ENODEV;
		goto unfreeze;
	}

	disk->nr_zones = args->nr_zones;
	disk->zone_capacity = args->zone_capacity;
	disk->last_zone_capacity = args->last_zone_capacity;
	disk_set_zones_state_array(disk, args->zones_state);
	args->zones_state = NULL;

	/*
	 * Some devices can advertise zone resource limits that are larger than
	 * the number of sequential zones of the zoned block device, e.g. a
	 * small ZNS namespace. For such case, assume that the zoned device has
	 * no zone resource limits.
	 */
	nr_seq_zones = disk->nr_zones - args->nr_conv_zones;
	if (lim.max_open_zones >= nr_seq_zones)
		lim.max_open_zones = 0;
	if (lim.max_active_zones >= nr_seq_zones)
		lim.max_active_zones = 0;

	if (!disk->zone_wplugs_pool)
		goto unfreeze;

	/*
	 * If the device has no limit on the maximum number of open and active
	 * zones, set its max open zone limit to the mempool size to indicate
	 * to the user that there is a potential performance impact due to
	 * dynamic zone write plug allocation when simultaneously writing to
	 * more zones than the size of the mempool.
	 */
	pool_size = max(lim.max_open_zones, lim.max_active_zones);
	if (!pool_size)
		pool_size = min(BLK_ZONE_WPLUG_DEFAULT_POOL_SIZE, nr_seq_zones);

	mempool_resize(disk->zone_wplugs_pool, pool_size);

	if (!lim.max_open_zones && !lim.max_active_zones) {
		if (pool_size < nr_seq_zones)
			lim.max_open_zones = pool_size;
		else
			lim.max_open_zones = 0;
	}

unfreeze:
	if (ret)
		queue_limits_cancel_update(q);
	else
		ret = queue_limits_commit_update(q, &lim);

	blk_mq_unfreeze_queue(q, memflags);

	return ret;
}

static void disk_drop_zone_wplug(struct blk_zone_wplug *zwplug, void *data)
{
	unsigned long flags;

	spin_lock_irqsave(&zwplug->lock, flags);
	disk_zone_wplug_abort(zwplug);
	disk_mark_zone_wplug_dead(zwplug);
	spin_unlock_irqrestore(&zwplug->lock, flags);
}

static int disk_revalidate_capacity(struct gendisk *disk,
				    struct blk_revalidate_zone_args *args)
{
	struct queue_limits *lim = &disk->queue->limits;
	sector_t zone_sectors = lim->chunk_sectors;
	unsigned int nr_zones;
	int ret = -ENODEV;

	/* Checks that the device driver indicated a valid zone size. */
	if (!zone_sectors || !is_power_of_2(zone_sectors)) {
		pr_warn("%s: Invalid non power of two zone size (%llu)\n",
			disk->disk_name, zone_sectors);
		goto drop_all_zwplugs;
	}

	args->capacity = get_capacity(disk);
	nr_zones = disk_get_nr_zones(disk, args->capacity);
	if (!args->capacity || !nr_zones)
		goto drop_all_zwplugs;

	/*
	 * Check if the capacity has changed. If it did, assume that the device
	 * was reformatted and that all sequential zones are now empty. So drop
	 * all zone write plug.
	 */
	if (disk->nr_zones && disk->nr_zones != nr_zones) {
		pr_warn("%s: Number of zones changed (%u -> %u)\n",
			disk->disk_name, disk->nr_zones, nr_zones);
		ret = 0;
		goto drop_all_zwplugs;
	}

	return 0;

drop_all_zwplugs:
	disk_for_all_zone_wplugs(disk, disk_drop_zone_wplug, NULL);

	return ret;
}

static int blk_revalidate_conv_zone(struct blk_zone *zone, unsigned int idx,
				    struct blk_revalidate_zone_args *args)
{
	struct gendisk *disk = args->disk;

	/* Check the zone condition. */
	switch (zone->cond) {
	case BLK_ZONE_COND_NOT_WP:
	case BLK_ZONE_COND_OFFLINE:
	case BLK_ZONE_COND_READONLY:
		break;
	default:
		pr_warn("%s: Invalid conv. zone condition 0x%X at sector %llu\n",
			disk->disk_name, zone->cond, zone->start);
		return -ENODEV;
	}

	if (zone->capacity != zone->len) {
		pr_warn("%s: Invalid conventional zone capacity\n",
			disk->disk_name);
		return -ENODEV;
	}

	blk_zstate_set(args->zones_state, args->nr_zones, idx,
		       zone->cond, BLK_ZFLAG_CONV);

	if (disk_zone_is_last(disk, zone))
		args->last_zone_capacity = zone->capacity;

	args->nr_conv_zones++;

	return 0;
}

static int blk_revalidate_seq_zone(struct blk_zone *zone, unsigned int idx,
				   struct blk_revalidate_zone_args *args)
{
	struct gendisk *disk = args->disk;
	struct blk_zone_wplug *zwplug;
	unsigned int wp_offset;

	/* Check the zone condition. */
	switch (zone->cond) {
	case BLK_ZONE_COND_IMP_OPEN:
	case BLK_ZONE_COND_EXP_OPEN:
	case BLK_ZONE_COND_CLOSED:
	case BLK_ZONE_COND_EMPTY:
	case BLK_ZONE_COND_FULL:
	case BLK_ZONE_COND_OFFLINE:
	case BLK_ZONE_COND_READONLY:
		break;
	default:
		pr_warn("%s: Invalid seq. zone condition 0x%X at sector %llu\n",
			disk->disk_name, zone->cond, zone->start);
		return -ENODEV;
	}

	blk_zstate_set(args->zones_state, args->nr_zones, idx, zone->cond, 0);

	/*
	 * Remember the capacity of the first sequential zone and check
	 * if it is constant for all zones, ignoring the last zone as it can be
	 * smaller.
	 */
	if (!args->zone_capacity)
		args->zone_capacity = zone->capacity;
	if (disk_zone_is_last(disk, zone)) {
		args->last_zone_capacity = zone->capacity;
	} else if (zone->capacity != args->zone_capacity) {
		pr_warn("%s: Invalid variable zone capacity\n",
			disk->disk_name);
		return -ENODEV;
	}

	/*
	 * If the device needs zone append emulation, we need to track the
	 * write pointer of all zones that are not empty nor full. So make sure
	 * we have a zone write plug for such zone if the device has a zone
	 * write plug hash table.
	 */
	if (!disk->zone_wplugs_hash)
		return 0;

	wp_offset = disk_zone_wplug_sync_state(disk, zone);
	if (!wp_offset || wp_offset >= zone->capacity)
		return 0;

	zwplug = disk_get_or_alloc_zone_wplug(disk, zone->wp, GFP_NOIO);
	if (!zwplug)
		return -ENOMEM;
	disk_put_zone_wplug(zwplug);

	return 0;
}

/*
 * Helper function to check the validity of zones of a zoned block device.
 */
static int blk_revalidate_zone_cb(struct blk_zone *zone, unsigned int idx,
				  void *data)
{
	struct blk_revalidate_zone_args *args = data;
	struct gendisk *disk = args->disk;
	sector_t zone_sectors = disk->queue->limits.chunk_sectors;
	int ret;

	/* Check for bad zones and holes in the zone report */
	if (zone->start != args->sector) {
		pr_warn("%s: Zone gap at sectors %llu..%llu\n",
			disk->disk_name, args->sector, zone->start);
		return -ENODEV;
	}

	if (zone->start >= args->capacity || !zone->len) {
		pr_warn("%s: Invalid zone start %llu, length %llu\n",
			disk->disk_name, zone->start, zone->len);
		return -ENODEV;
	}

	if (idx >= args->nr_zones) {
		pr_warn("%s: Zone report index %u exceeds zone count %u\n",
			disk->disk_name, idx, args->nr_zones);
		return -EINVAL;
	}

	/*
	 * All zones must have the same size, with the exception on an eventual
	 * smaller last zone.
	 */
	if (!disk_zone_is_last(disk, zone)) {
		if (zone->len != zone_sectors) {
			pr_warn("%s: Invalid zoned device with non constant zone size\n",
				disk->disk_name);
			return -ENODEV;
		}
	} else if (zone->len > zone_sectors) {
		pr_warn("%s: Invalid zoned device with larger last zone size\n",
			disk->disk_name);
		return -ENODEV;
	}

	if (!zone->capacity || zone->capacity > zone->len) {
		pr_warn("%s: Invalid zone capacity\n",
			disk->disk_name);
		return -ENODEV;
	}

	/* Check zone type */
	switch (zone->type) {
	case BLK_ZONE_TYPE_CONVENTIONAL:
		ret = blk_revalidate_conv_zone(zone, idx, args);
		break;
	case BLK_ZONE_TYPE_SEQWRITE_REQ:
		ret = blk_revalidate_seq_zone(zone, idx, args);
		break;
	case BLK_ZONE_TYPE_SEQWRITE_PREF:
	default:
		pr_warn("%s: Invalid zone type 0x%x at sectors %llu\n",
			disk->disk_name, (int)zone->type, zone->start);
		ret = -ENODEV;
	}

	if (!ret)
		args->sector += zone->len;

	return ret;
}

/**
 * blk_revalidate_disk_zones - (re)allocate and initialize zone write plugs
 * @disk:	Target disk
 *
 * Helper function for low-level device drivers to check, (re) allocate and
 * initialize resources used for managing zoned disks. This function should
 * normally be called by blk-mq based drivers when a zoned gendisk is probed
 * and when the zone configuration of the gendisk changes (e.g. after a format).
 * Before calling this function, the device driver must already have set the
 * device zone size (chunk_sector limit) and the max zone append limit.
 * BIO based drivers can also use this function as long as the device queue
 * can be safely frozen.
 */
int blk_revalidate_disk_zones(struct gendisk *disk)
{
	struct blk_revalidate_zone_args args = { };
	struct blk_report_zones_args rep_args = {
		.cb = blk_revalidate_zone_cb,
		.data = &args,
	};
	unsigned int noio_flag;
	int retries = 2;
	int ret;

	if (WARN_ON_ONCE(!blk_queue_is_zoned(disk->queue)))
		return -EIO;

	/*
	 * Serialize calls to this function so that we can safely look at and
	 * eventually change the disk zone information.
	 */
	mutex_lock(&disk->zone_revalidate_mutex);

again:
	ret = disk_revalidate_capacity(disk, &args);
	if (ret)
		goto unlock;

	/*
	 * Allocate zone resources if they are needed and we have not done
	 * so yet, and initialize the revalidation arguments passed to report
	 * zones. Ensure that all memory allocations in this context are done as
	 * if GFP_NOIO was specified.
	 */
	noio_flag = memalloc_noio_save();
	if (disk_need_zone_resources(disk) && !disk->zone_wplugs_hash) {
		ret = disk_alloc_zone_resources(disk, args.capacity);
		if (ret) {
			memalloc_noio_restore(noio_flag);
			goto unlock;
		}
	}

	ret = disk_init_revalidate_args(disk, &args);
	if (ret) {
		memalloc_noio_restore(noio_flag);
		goto unlock;
	}

	ret = disk->fops->report_zones(disk, 0, UINT_MAX, &rep_args);
	if (!ret) {
		pr_warn("%s: No zones reported\n", disk->disk_name);
		ret = -ENODEV;
	}
	memalloc_noio_restore(noio_flag);

	if (ret < 0)
		goto free_args;

	ret = disk_revalidate_zone_resources(disk, &args);
	if (ret)
		goto free_args;

	mutex_unlock(&disk->zone_revalidate_mutex);

	return 0;

free_args:
	kfree(args.zones_state);

	if (ret == -EAGAIN) {
		if (retries) {
			memset(&args, 0, sizeof(args));
			retries--;
			goto again;
		}
		ret = -ENODEV;
	}

	pr_warn("%s: failed to revalidate zones\n", disk->disk_name);
unlock:
	mutex_unlock(&disk->zone_revalidate_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(blk_revalidate_disk_zones);

/**
 * blk_zone_issue_zeroout - zero-fill a block range in a zone
 * @bdev:	blockdev to write
 * @sector:	start sector
 * @nr_sects:	number of sectors to write
 * @gfp_mask:	memory allocation flags (for bio_alloc)
 *
 * Description:
 *  Zero-fill a block range in a zone (@sector must be equal to the zone write
 *  pointer), handling potential errors due to the (initially unknown) lack of
 *  hardware offload (See blkdev_issue_zeroout()).
 */
int blk_zone_issue_zeroout(struct block_device *bdev, sector_t sector,
			   sector_t nr_sects, gfp_t gfp_mask)
{
	struct gendisk *disk = bdev->bd_disk;
	int ret;

	if (WARN_ON_ONCE(!bdev_is_zoned(bdev)))
		return -EIO;

	ret = blkdev_issue_zeroout(bdev, sector, nr_sects, gfp_mask,
				   BLKDEV_ZERO_NOFALLBACK);
	if (ret != -EOPNOTSUPP)
		return ret;

	/*
	 * The failed call to blkdev_issue_zeroout() advanced the zone write
	 * pointer. Undo this using a report zone to update the zone write
	 * pointer to the correct current value.
	 */
	ret = disk->fops->report_zones(disk, sector, 1, NULL);
	if (ret != 1)
		return ret < 0 ? ret : -EIO;

	/*
	 * Retry without BLKDEV_ZERO_NOFALLBACK to force the fallback to a
	 * regular write with zero-pages.
	 */
	return blkdev_issue_zeroout(bdev, sector, nr_sects, gfp_mask, 0);
}
EXPORT_SYMBOL_GPL(blk_zone_issue_zeroout);

#ifdef CONFIG_BLK_DEBUG_FS
static void queue_zone_wplug_show(struct blk_zone_wplug *zwplug,
				  void *data)
{
	struct seq_file *m = data;
	unsigned int zwp_wp_offset, zwp_flags;
	unsigned int zwp_zone_no, zwp_ref;
	unsigned int zwp_bio_list_size;
	enum blk_zone_cond zwp_cond;
	unsigned long flags;

	spin_lock_irqsave(&zwplug->lock, flags);
	zwp_zone_no = zwplug->zone_no;
	zwp_flags = zwplug->flags;
	zwp_ref = refcount_read(&zwplug->ref);
	zwp_cond = zwplug->cond;
	zwp_wp_offset = zwplug->wp_offset;
	zwp_bio_list_size = bio_list_size(&zwplug->bio_list);
	spin_unlock_irqrestore(&zwplug->lock, flags);

	seq_printf(m,
		"Zone no: %u, flags: 0x%x, ref: %u, cond: %s, wp ofst: %u, pending BIO: %u\n",
		zwp_zone_no, zwp_flags, zwp_ref, blk_zone_cond_str(zwp_cond),
		zwp_wp_offset, zwp_bio_list_size);
}

int queue_zone_wplugs_show(void *data, struct seq_file *m)
{
	struct request_queue *q = data;

	disk_for_all_zone_wplugs(q->disk, queue_zone_wplug_show, m);

	return 0;
}

#endif
