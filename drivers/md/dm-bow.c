/*
 * Copyright (C) 2018 Google Limited.
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include "dm-core.h"

#include <linux/crc32.h>
#include <linux/dm-bufio.h>
#include <linux/module.h>

#define DM_MSG_PREFIX "bow"

struct log_entry {
	u64 source;
	u64 dest;
	u32 size;
	u32 checksum;
} __packed;

struct log_sector {
	u32 magic;
	u16 header_version;
	u16 header_size;
	u32 block_size;
	u32 count;
	u32 sequence;
	sector_t sector0;
	struct log_entry entries[];
} __packed;

/*
 * MAGIC is BOW in ascii
 */
#define MAGIC 0x00574f42
#define HEADER_VERSION 0x0100

/*
 * A sorted set of ranges representing the state of the data on the device.
 * Use an rb_tree for fast lookup of a given sector
 * Consecutive ranges are always of different type - operations on this
 * set must merge matching consecutive ranges.
 *
 * Top range is always of type TOP
 */
struct bow_range {
	struct rb_node		node;
	sector_t		sector;
	enum {
		INVALID,	/* Type not set */
		SECTOR0,	/* First sector - holds log record */
		SECTOR0_CURRENT,/* Live contents of sector0 */
		UNCHANGED,	/* Original contents */
		TRIMMED,	/* Range has been trimmed */
		CHANGED,	/* Range has been changed */
		BACKUP,		/* Range is being used as a backup */
		TOP,		/* Final range - sector is size of device */
	} type;
	struct list_head	trimmed_list; /* list of TRIMMED ranges */
};

static const char * const readable_type[] = {
	"Invalid",
	"Sector0",
	"Sector0_current",
	"Unchanged",
	"Free",
	"Changed",
	"Backup",
	"Top",
};

enum state {
	TRIM,
	CHECKPOINT,
	COMMITTED,
};

struct bow_context {
	struct dm_dev *dev;
	u32 block_size;
	u32 block_shift;
	struct workqueue_struct *workqueue;
	struct dm_bufio_client *bufio;
	struct mutex ranges_lock; /* Hold to access this struct and/or ranges */
	struct rb_root ranges;
	struct dm_kobject_holder kobj_holder;	/* for sysfs attributes */
	atomic_t state; /* One of the enum state values above */
	u64 trims_total;
	struct log_sector *log_sector;
	struct list_head trimmed_list;
	bool forward_trims;
};

sector_t range_top(struct bow_range *br)
{
	return container_of(rb_next(&br->node), struct bow_range, node)
		->sector;
}

u64 range_size(struct bow_range *br)
{
	return (range_top(br) - br->sector) * SECTOR_SIZE;
}

static sector_t bvec_top(struct bvec_iter *bi_iter)
{
	return bi_iter->bi_sector + bi_iter->bi_size / SECTOR_SIZE;
}

/*
 * Find the first range that overlaps with bi_iter
 * bi_iter is set to the size of the overlapping sub-range
 */
static struct bow_range *find_first_overlapping_range(struct rb_root *ranges,
						      struct bvec_iter *bi_iter)
{
	struct rb_node *node = ranges->rb_node;
	struct bow_range *br;

	while (node) {
		br = container_of(node, struct bow_range, node);

		if (br->sector <= bi_iter->bi_sector
		    && bi_iter->bi_sector < range_top(br))
			break;

		if (bi_iter->bi_sector < br->sector)
			node = node->rb_left;
		else
			node = node->rb_right;
	}

	WARN_ON(!node);
	if (!node)
		return NULL;

	if (range_top(br) - bi_iter->bi_sector
	    < bi_iter->bi_size >> SECTOR_SHIFT)
		bi_iter->bi_size = (range_top(br) - bi_iter->bi_sector)
			<< SECTOR_SHIFT;

	return br;
}

void add_before(struct rb_root *ranges, struct bow_range *new_br,
		struct bow_range *existing)
{
	struct rb_node *parent = &(existing->node);
	struct rb_node **link = &(parent->rb_left);

	while (*link) {
		parent = *link;
		link = &((*link)->rb_right);
	}

	rb_link_node(&new_br->node, parent, link);
	rb_insert_color(&new_br->node, ranges);
}

/*
 * Given a range br returned by find_first_overlapping_range, split br into a
 * leading range, a range matching the bi_iter and a trailing range.
 * Leading and trailing may end up size 0 and will then be deleted. The
 * new range matching the bi_iter is then returned and should have its type
 * and type specific fields populated.
 * If bi_iter runs off the end of the range, bi_iter is truncated accordingly
 */
static int split_range(struct bow_context *bc, struct bow_range **br,
		       struct bvec_iter *bi_iter)
{
	struct bow_range *new_br;

	if (bi_iter->bi_sector < (*br)->sector) {
		WARN_ON(true);
		return BLK_STS_IOERR;
	}

	if (bi_iter->bi_sector > (*br)->sector) {
		struct bow_range *leading_br =
			kzalloc(sizeof(*leading_br), GFP_KERNEL);

		if (!leading_br)
			return BLK_STS_RESOURCE;

		*leading_br = **br;
		if (leading_br->type == TRIMMED)
			list_add(&leading_br->trimmed_list, &bc->trimmed_list);

		add_before(&bc->ranges, leading_br, *br);
		(*br)->sector = bi_iter->bi_sector;
	}

	if (bvec_top(bi_iter) >= range_top(*br)) {
		bi_iter->bi_size = (range_top(*br) - (*br)->sector)
					* SECTOR_SIZE;
		return BLK_STS_OK;
	}

	/* new_br will be the beginning, existing br will be the tail */
	new_br = kzalloc(sizeof(*new_br), GFP_KERNEL);
	if (!new_br)
		return BLK_STS_RESOURCE;

	new_br->sector = (*br)->sector;
	(*br)->sector = bvec_top(bi_iter);
	add_before(&bc->ranges, new_br, *br);
	*br = new_br;

	return BLK_STS_OK;
}

/*
 * Sets type of a range. May merge range into surrounding ranges
 * Since br may be invalidated, always sets br to NULL to prevent
 * usage after this is called
 */
static void set_type(struct bow_context *bc, struct bow_range **br, int type)
{
	struct bow_range *prev = container_of(rb_prev(&(*br)->node),
						      struct bow_range, node);
	struct bow_range *next = container_of(rb_next(&(*br)->node),
						      struct bow_range, node);

	if ((*br)->type == TRIMMED) {
		bc->trims_total -= range_size(*br);
		list_del(&(*br)->trimmed_list);
	}

	if (type == TRIMMED) {
		bc->trims_total += range_size(*br);
		list_add(&(*br)->trimmed_list, &bc->trimmed_list);
	}

	(*br)->type = type;

	if (next->type == type) {
		if (type == TRIMMED)
			list_del(&next->trimmed_list);
		rb_erase(&next->node, &bc->ranges);
		kfree(next);
	}

	if (prev->type == type) {
		if (type == TRIMMED)
			list_del(&(*br)->trimmed_list);
		rb_erase(&(*br)->node, &bc->ranges);
		kfree(*br);
	}

	*br = NULL;
}

static struct bow_range *find_free_range(struct bow_context *bc)
{
	if (list_empty(&bc->trimmed_list)) {
		DMERR("Unable to find free space to back up to");
		return NULL;
	}

	return list_first_entry(&bc->trimmed_list, struct bow_range,
				trimmed_list);
}

static sector_t sector_to_page(struct bow_context const *bc, sector_t sector)
{
	WARN_ON(sector % (bc->block_size / SECTOR_SIZE) != 0);
	return sector >> (bc->block_shift - SECTOR_SHIFT);
}

static int copy_data(struct bow_context const *bc,
		     struct bow_range *source, struct bow_range *dest,
		     u32 *checksum)
{
	int i;

	if (range_size(source) != range_size(dest)) {
		WARN_ON(1);
		return BLK_STS_IOERR;
	}

	if (checksum)
		*checksum = sector_to_page(bc, source->sector);

	for (i = 0; i < range_size(source) >> bc->block_shift; ++i) {
		struct dm_buffer *read_buffer, *write_buffer;
		u8 *read, *write;
		sector_t page = sector_to_page(bc, source->sector) + i;

		read = dm_bufio_read(bc->bufio, page, &read_buffer);
		if (IS_ERR(read)) {
			DMERR("Cannot read page %lu", page);
			return PTR_ERR(read);
		}

		if (checksum)
			*checksum = crc32(*checksum, read, bc->block_size);

		write = dm_bufio_new(bc->bufio,
				     sector_to_page(bc, dest->sector) + i,
				     &write_buffer);
		if (IS_ERR(write)) {
			DMERR("Cannot write sector");
			dm_bufio_release(read_buffer);
			return PTR_ERR(write);
		}

		memcpy(write, read, bc->block_size);

		dm_bufio_mark_buffer_dirty(write_buffer);
		dm_bufio_release(write_buffer);
		dm_bufio_release(read_buffer);
	}

	dm_bufio_write_dirty_buffers(bc->bufio);
	return BLK_STS_OK;
}

/****** logging functions ******/

static int add_log_entry(struct bow_context *bc, sector_t source, sector_t dest,
			 unsigned int size, u32 checksum);

static int backup_log_sector(struct bow_context *bc)
{
	struct bow_range *first_br, *free_br;
	struct bvec_iter bi_iter;
	u32 checksum = 0;
	int ret;

	first_br = container_of(rb_first(&bc->ranges), struct bow_range, node);

	if (first_br->type != SECTOR0) {
		WARN_ON(1);
		return BLK_STS_IOERR;
	}

	if (range_size(first_br) != bc->block_size) {
		WARN_ON(1);
		return BLK_STS_IOERR;
	}

	free_br = find_free_range(bc);
	/* No space left - return this error to userspace */
	if (!free_br)
		return BLK_STS_NOSPC;
	bi_iter.bi_sector = free_br->sector;
	bi_iter.bi_size = bc->block_size;
	ret = split_range(bc, &free_br, &bi_iter);
	if (ret)
		return ret;
	if (bi_iter.bi_size != bc->block_size) {
		WARN_ON(1);
		return BLK_STS_IOERR;
	}

	ret = copy_data(bc, first_br, free_br, &checksum);
	if (ret)
		return ret;

	bc->log_sector->count = 0;
	bc->log_sector->sequence++;
	ret = add_log_entry(bc, first_br->sector, free_br->sector,
			    range_size(first_br), checksum);
	if (ret)
		return ret;

	set_type(bc, &free_br, BACKUP);
	return BLK_STS_OK;
}

static int add_log_entry(struct bow_context *bc, sector_t source, sector_t dest,
			 unsigned int size, u32 checksum)
{
	struct dm_buffer *sector_buffer;
	u8 *sector;

	if (sizeof(struct log_sector)
	    + sizeof(struct log_entry) * (bc->log_sector->count + 1)
		> bc->block_size) {
		int ret = backup_log_sector(bc);

		if (ret)
			return ret;
	}

	sector = dm_bufio_new(bc->bufio, 0, &sector_buffer);
	if (IS_ERR(sector)) {
		DMERR("Cannot write boot sector");
		dm_bufio_release(sector_buffer);
		return BLK_STS_NOSPC;
	}

	bc->log_sector->entries[bc->log_sector->count].source = source;
	bc->log_sector->entries[bc->log_sector->count].dest = dest;
	bc->log_sector->entries[bc->log_sector->count].size = size;
	bc->log_sector->entries[bc->log_sector->count].checksum = checksum;
	bc->log_sector->count++;

	memcpy(sector, bc->log_sector, bc->block_size);
	dm_bufio_mark_buffer_dirty(sector_buffer);
	dm_bufio_release(sector_buffer);
	dm_bufio_write_dirty_buffers(bc->bufio);
	return BLK_STS_OK;
}

static int prepare_log(struct bow_context *bc)
{
	struct bow_range *free_br, *first_br;
	struct bvec_iter bi_iter;
	u32 checksum = 0;
	int ret;

	/* Carve out first sector as log sector */
	first_br = container_of(rb_first(&bc->ranges), struct bow_range, node);
	if (first_br->type != UNCHANGED) {
		WARN_ON(1);
		return BLK_STS_IOERR;
	}

	if (range_size(first_br) < bc->block_size) {
		WARN_ON(1);
		return BLK_STS_IOERR;
	}
	bi_iter.bi_sector = 0;
	bi_iter.bi_size = bc->block_size;
	ret = split_range(bc, &first_br, &bi_iter);
	if (ret)
		return ret;
	first_br->type = SECTOR0;
	if (range_size(first_br) != bc->block_size) {
		WARN_ON(1);
		return BLK_STS_IOERR;
	}

	/* Find free sector for active sector0 reads/writes */
	free_br = find_free_range(bc);
	if (!free_br)
		return BLK_STS_NOSPC;
	bi_iter.bi_sector = free_br->sector;
	bi_iter.bi_size = bc->block_size;
	ret = split_range(bc, &free_br, &bi_iter);
	if (ret)
		return ret;
	free_br->type = SECTOR0_CURRENT;

	/* Copy data */
	ret = copy_data(bc, first_br, free_br, NULL);
	if (ret)
		return ret;

	bc->log_sector->sector0 = free_br->sector;

	/* Find free sector to back up original sector zero */
	free_br = find_free_range(bc);
	if (!free_br)
		return BLK_STS_NOSPC;
	bi_iter.bi_sector = free_br->sector;
	bi_iter.bi_size = bc->block_size;
	ret = split_range(bc, &free_br, &bi_iter);
	if (ret)
		return ret;

	/* Back up */
	ret = copy_data(bc, first_br, free_br, &checksum);
	if (ret)
		return ret;

	/*
	 * Set up our replacement boot sector - it will get written when we
	 * add the first log entry, which we do immediately
	 */
	bc->log_sector->magic = MAGIC;
	bc->log_sector->header_version = HEADER_VERSION;
	bc->log_sector->header_size = sizeof(*bc->log_sector);
	bc->log_sector->block_size = bc->block_size;
	bc->log_sector->count = 0;
	bc->log_sector->sequence = 0;

	/* Add log entry */
	ret = add_log_entry(bc, first_br->sector, free_br->sector,
			    range_size(first_br), checksum);
	if (ret)
		return ret;

	set_type(bc, &free_br, BACKUP);
	return BLK_STS_OK;
}

static struct bow_range *find_sector0_current(struct bow_context *bc)
{
	struct bvec_iter bi_iter;

	bi_iter.bi_sector = bc->log_sector->sector0;
	bi_iter.bi_size = bc->block_size;
	return find_first_overlapping_range(&bc->ranges, &bi_iter);
}

/****** sysfs interface functions ******/

static ssize_t state_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	struct bow_context *bc = container_of(kobj, struct bow_context,
					      kobj_holder.kobj);

	return scnprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&bc->state));
}

static ssize_t state_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t count)
{
	struct bow_context *bc = container_of(kobj, struct bow_context,
					      kobj_holder.kobj);
	enum state state, original_state;
	int ret;

	state = buf[0] - '0';
	if (state < TRIM || state > COMMITTED) {
		DMERR("State value %d out of range", state);
		return -EINVAL;
	}

	mutex_lock(&bc->ranges_lock);
	original_state = atomic_read(&bc->state);
	if (state != original_state + 1) {
		DMERR("Invalid state change from %d to %d",
		      original_state, state);
		ret = -EINVAL;
		goto bad;
	}

	DMINFO("Switching to state %s", state == CHECKPOINT ? "Checkpoint"
	       : state == COMMITTED ? "Committed" : "Unknown");

	if (state == CHECKPOINT) {
		ret = prepare_log(bc);
		if (ret) {
			DMERR("Failed to switch to checkpoint state");
			goto bad;
		}
	} else if (state == COMMITTED) {
		struct bow_range *br = find_sector0_current(bc);
		struct bow_range *sector0_br =
			container_of(rb_first(&bc->ranges), struct bow_range,
				     node);

		ret = copy_data(bc, br, sector0_br, 0);
		if (ret) {
			DMERR("Failed to switch to committed state");
			goto bad;
		}
	}
	atomic_inc(&bc->state);
	ret = count;

bad:
	mutex_unlock(&bc->ranges_lock);
	return ret;
}

static ssize_t free_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	struct bow_context *bc = container_of(kobj, struct bow_context,
					      kobj_holder.kobj);
	u64 trims_total;

	mutex_lock(&bc->ranges_lock);
	trims_total = bc->trims_total;
	mutex_unlock(&bc->ranges_lock);

	return scnprintf(buf, PAGE_SIZE, "%llu\n", trims_total);
}

static struct kobj_attribute attr_state = __ATTR_RW(state);
static struct kobj_attribute attr_free = __ATTR_RO(free);

static struct attribute *bow_attrs[] = {
	&attr_state.attr,
	&attr_free.attr,
	NULL
};

static struct kobj_type bow_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.default_attrs = bow_attrs,
	.release = dm_kobject_release
};

/****** constructor/destructor ******/

static void dm_bow_dtr(struct dm_target *ti)
{
	struct bow_context *bc = (struct bow_context *) ti->private;
	struct kobject *kobj;

	while (rb_first(&bc->ranges)) {
		struct bow_range *br = container_of(rb_first(&bc->ranges),
						    struct bow_range, node);

		rb_erase(&br->node, &bc->ranges);
		kfree(br);
	}
	if (bc->workqueue)
		destroy_workqueue(bc->workqueue);
	if (bc->bufio)
		dm_bufio_client_destroy(bc->bufio);

	kobj = &bc->kobj_holder.kobj;
	if (kobj->state_initialized) {
		kobject_put(kobj);
		wait_for_completion(dm_get_completion_from_kobject(kobj));
	}

	kfree(bc->log_sector);
	kfree(bc);
}

static int dm_bow_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct bow_context *bc;
	struct bow_range *br;
	int ret;
	struct mapped_device *md = dm_table_get_md(ti->table);

	if (argc != 1) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}

	bc = kzalloc(sizeof(*bc), GFP_KERNEL);
	if (!bc) {
		ti->error = "Cannot allocate bow context";
		return -ENOMEM;
	}

	ti->num_flush_bios = 1;
	ti->num_discard_bios = 1;
	ti->num_write_same_bios = 1;
	ti->private = bc;

	ret = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table),
			    &bc->dev);
	if (ret) {
		ti->error = "Device lookup failed";
		goto bad;
	}

	if (bc->dev->bdev->bd_queue->limits.max_discard_sectors == 0) {
		bc->dev->bdev->bd_queue->limits.discard_granularity = 1 << 12;
		bc->dev->bdev->bd_queue->limits.max_hw_discard_sectors = 1 << 15;
		bc->dev->bdev->bd_queue->limits.max_discard_sectors = 1 << 15;
		bc->forward_trims = false;
	} else {
		bc->forward_trims = true;
	}

	bc->block_size = bc->dev->bdev->bd_queue->limits.logical_block_size;
	bc->block_shift = ilog2(bc->block_size);
	bc->log_sector = kzalloc(bc->block_size, GFP_KERNEL);
	if (!bc->log_sector) {
		ti->error = "Cannot allocate log sector";
		goto bad;
	}

	init_completion(&bc->kobj_holder.completion);
	ret = kobject_init_and_add(&bc->kobj_holder.kobj, &bow_ktype,
				   &disk_to_dev(dm_disk(md))->kobj, "%s",
				   "bow");
	if (ret) {
		ti->error = "Cannot create sysfs node";
		goto bad;
	}

	mutex_init(&bc->ranges_lock);
	bc->ranges = RB_ROOT;
	bc->bufio = dm_bufio_client_create(bc->dev->bdev, bc->block_size, 1, 0,
					   NULL, NULL);
	if (IS_ERR(bc->bufio)) {
		ti->error = "Cannot initialize dm-bufio";
		ret = PTR_ERR(bc->bufio);
		bc->bufio = NULL;
		goto bad;
	}

	bc->workqueue = alloc_workqueue("dm-bow",
					WQ_CPU_INTENSIVE | WQ_MEM_RECLAIM
					| WQ_UNBOUND, num_online_cpus());
	if (!bc->workqueue) {
		ti->error = "Cannot allocate workqueue";
		ret = -ENOMEM;
		goto bad;
	}

	INIT_LIST_HEAD(&bc->trimmed_list);

	br = kzalloc(sizeof(*br), GFP_KERNEL);
	if (!br) {
		ti->error = "Cannot allocate ranges";
		ret = -ENOMEM;
		goto bad;
	}

	br->sector = ti->len;
	br->type = TOP;
	rb_link_node(&br->node, NULL, &bc->ranges.rb_node);
	rb_insert_color(&br->node, &bc->ranges);

	br = kzalloc(sizeof(*br), GFP_KERNEL);
	if (!br) {
		ti->error = "Cannot allocate ranges";
		ret = -ENOMEM;
		goto bad;
	}

	br->sector = 0;
	br->type = UNCHANGED;
	rb_link_node(&br->node, bc->ranges.rb_node,
		     &bc->ranges.rb_node->rb_left);
	rb_insert_color(&br->node, &bc->ranges);

	ti->discards_supported = true;

	return 0;

bad:
	dm_bow_dtr(ti);
	return ret;
}

/****** Handle writes ******/

static int prepare_unchanged_range(struct bow_context *bc, struct bow_range *br,
				   struct bvec_iter *bi_iter,
				   bool record_checksum)
{
	struct bow_range *backup_br;
	struct bvec_iter backup_bi;
	sector_t log_source, log_dest;
	unsigned int log_size;
	u32 checksum = 0;
	int ret;
	int original_type;
	sector_t sector0;

	/* Find a free range */
	backup_br = find_free_range(bc);
	if (!backup_br)
		return BLK_STS_NOSPC;

	/* Carve out a backup range. This may be smaller than the br given */
	backup_bi.bi_sector = backup_br->sector;
	backup_bi.bi_size = min(range_size(backup_br), (u64) bi_iter->bi_size);
	ret = split_range(bc, &backup_br, &backup_bi);
	if (ret)
		return ret;

	/*
	 * Carve out a changed range. This will not be smaller than the backup
	 * br since the backup br is smaller than the source range and iterator
	 */
	bi_iter->bi_size = backup_bi.bi_size;
	ret = split_range(bc, &br, bi_iter);
	if (ret)
		return ret;
	if (range_size(br) != range_size(backup_br)) {
		WARN_ON(1);
		return BLK_STS_IOERR;
	}


	/* Copy data over */
	ret = copy_data(bc, br, backup_br, record_checksum ? &checksum : NULL);
	if (ret)
		return ret;

	/* Add an entry to the log */
	log_source = br->sector;
	log_dest = backup_br->sector;
	log_size = range_size(br);

	/*
	 * Set the types. Note that since set_type also amalgamates ranges
	 * we have to set both sectors to their final type before calling
	 * set_type on either
	 */
	original_type = br->type;
	sector0 = backup_br->sector;
	if (backup_br->type == TRIMMED)
		list_del(&backup_br->trimmed_list);
	backup_br->type = br->type == SECTOR0_CURRENT ? SECTOR0_CURRENT
						      : BACKUP;
	br->type = CHANGED;
	set_type(bc, &backup_br, backup_br->type);

	/*
	 * Add the log entry after marking the backup sector, since adding a log
	 * can cause another backup
	 */
	ret = add_log_entry(bc, log_source, log_dest, log_size, checksum);
	if (ret) {
		br->type = original_type;
		return ret;
	}

	/* Now it is safe to mark this backup successful */
	if (original_type == SECTOR0_CURRENT)
		bc->log_sector->sector0 = sector0;

	set_type(bc, &br, br->type);
	return ret;
}

static int prepare_free_range(struct bow_context *bc, struct bow_range *br,
			      struct bvec_iter *bi_iter)
{
	int ret;

	ret = split_range(bc, &br, bi_iter);
	if (ret)
		return ret;
	set_type(bc, &br, CHANGED);
	return BLK_STS_OK;
}

static int prepare_changed_range(struct bow_context *bc, struct bow_range *br,
				 struct bvec_iter *bi_iter)
{
	/* Nothing to do ... */
	return BLK_STS_OK;
}

static int prepare_one_range(struct bow_context *bc,
			     struct bvec_iter *bi_iter)
{
	struct bow_range *br = find_first_overlapping_range(&bc->ranges,
							    bi_iter);
	switch (br->type) {
	case CHANGED:
		return prepare_changed_range(bc, br, bi_iter);

	case TRIMMED:
		return prepare_free_range(bc, br, bi_iter);

	case UNCHANGED:
	case BACKUP:
		return prepare_unchanged_range(bc, br, bi_iter, true);

	/*
	 * We cannot track the checksum for the active sector0, since it
	 * may change at any point.
	 */
	case SECTOR0_CURRENT:
		return prepare_unchanged_range(bc, br, bi_iter, false);

	case SECTOR0:	/* Handled in the dm_bow_map */
	case TOP:	/* Illegal - top is off the end of the device */
	default:
		WARN_ON(1);
		return BLK_STS_IOERR;
	}
}

struct write_work {
	struct work_struct work;
	struct bow_context *bc;
	struct bio *bio;
};

static void bow_write(struct work_struct *work)
{
	struct write_work *ww = container_of(work, struct write_work, work);
	struct bow_context *bc = ww->bc;
	struct bio *bio = ww->bio;
	struct bvec_iter bi_iter = bio->bi_iter;
	int ret = BLK_STS_OK;

	kfree(ww);

	mutex_lock(&bc->ranges_lock);
	do {
		ret = prepare_one_range(bc, &bi_iter);
		bi_iter.bi_sector += bi_iter.bi_size / SECTOR_SIZE;
		bi_iter.bi_size = bio->bi_iter.bi_size
			- (bi_iter.bi_sector - bio->bi_iter.bi_sector)
			  * SECTOR_SIZE;
	} while (!ret && bi_iter.bi_size);

	mutex_unlock(&bc->ranges_lock);

	if (!ret) {
		bio_set_dev(bio, bc->dev->bdev);
		submit_bio(bio);
	} else {
		DMERR("Write failure with error %d", -ret);
		bio->bi_status = ret;
		bio_endio(bio);
	}
}

static int queue_write(struct bow_context *bc, struct bio *bio)
{
	struct write_work *ww = kmalloc(sizeof(*ww), GFP_NOIO | __GFP_NORETRY
					| __GFP_NOMEMALLOC | __GFP_NOWARN);
	if (!ww) {
		DMERR("Failed to allocate write_work");
		return -ENOMEM;
	}

	INIT_WORK(&ww->work, bow_write);
	ww->bc = bc;
	ww->bio = bio;
	queue_work(bc->workqueue, &ww->work);
	return DM_MAPIO_SUBMITTED;
}

static int handle_sector0(struct bow_context *bc, struct bio *bio)
{
	int ret = DM_MAPIO_REMAPPED;

	if (bio->bi_iter.bi_size > bc->block_size) {
		struct bio * split = bio_split(bio,
					       bc->block_size >> SECTOR_SHIFT,
					       GFP_NOIO,
					       &fs_bio_set);
		if (!split) {
			DMERR("Failed to split bio");
			bio->bi_status = BLK_STS_RESOURCE;
			bio_endio(bio);
			return DM_MAPIO_SUBMITTED;
		}

		bio_chain(split, bio);
		split->bi_iter.bi_sector = bc->log_sector->sector0;
		bio_set_dev(split, bc->dev->bdev);
		submit_bio(split);

		if (bio_data_dir(bio) == WRITE)
			ret = queue_write(bc, bio);
	} else {
		bio->bi_iter.bi_sector = bc->log_sector->sector0;
	}

	return ret;
}

static int add_trim(struct bow_context *bc, struct bio *bio)
{
	struct bow_range *br;
	struct bvec_iter bi_iter = bio->bi_iter;

	DMDEBUG("add_trim: %lu, %u",
		bio->bi_iter.bi_sector, bio->bi_iter.bi_size);

	do {
		br = find_first_overlapping_range(&bc->ranges, &bi_iter);

		switch (br->type) {
		case UNCHANGED:
			if (!split_range(bc, &br, &bi_iter))
				set_type(bc, &br, TRIMMED);
			break;

		case TRIMMED:
			/* Nothing to do */
			break;

		default:
			/* No other case is legal in TRIM state */
			WARN_ON(true);
			break;
		}

		bi_iter.bi_sector += bi_iter.bi_size / SECTOR_SIZE;
		bi_iter.bi_size = bio->bi_iter.bi_size
			- (bi_iter.bi_sector - bio->bi_iter.bi_sector)
			  * SECTOR_SIZE;

	} while (bi_iter.bi_size);

	bio_endio(bio);
	return DM_MAPIO_SUBMITTED;
}

static int remove_trim(struct bow_context *bc, struct bio *bio)
{
	struct bow_range *br;
	struct bvec_iter bi_iter = bio->bi_iter;

	DMDEBUG("remove_trim: %lu, %u",
		bio->bi_iter.bi_sector, bio->bi_iter.bi_size);

	do {
		br = find_first_overlapping_range(&bc->ranges, &bi_iter);

		switch (br->type) {
		case UNCHANGED:
			/* Nothing to do */
			break;

		case TRIMMED:
			if (!split_range(bc, &br, &bi_iter))
				set_type(bc, &br, UNCHANGED);
			break;

		default:
			/* No other case is legal in TRIM state */
			WARN_ON(true);
			break;
		}

		bi_iter.bi_sector += bi_iter.bi_size / SECTOR_SIZE;
		bi_iter.bi_size = bio->bi_iter.bi_size
			- (bi_iter.bi_sector - bio->bi_iter.bi_sector)
			  * SECTOR_SIZE;

	} while (bi_iter.bi_size);

	return DM_MAPIO_REMAPPED;
}

int remap_unless_illegal_trim(struct bow_context *bc, struct bio *bio)
{
	if (!bc->forward_trims && bio_op(bio) == REQ_OP_DISCARD) {
		bio->bi_status = BLK_STS_NOTSUPP;
		bio_endio(bio);
		return DM_MAPIO_SUBMITTED;
	} else {
		bio_set_dev(bio, bc->dev->bdev);
		return DM_MAPIO_REMAPPED;
	}
}

/****** dm interface ******/

static int dm_bow_map(struct dm_target *ti, struct bio *bio)
{
	int ret = DM_MAPIO_REMAPPED;
	struct bow_context *bc = ti->private;

	if (likely(bc->state.counter == COMMITTED))
		return remap_unless_illegal_trim(bc, bio);

	if (bio_data_dir(bio) == READ && bio->bi_iter.bi_sector != 0)
		return remap_unless_illegal_trim(bc, bio);

	if (atomic_read(&bc->state) != COMMITTED) {
		enum state state;

		mutex_lock(&bc->ranges_lock);
		state = atomic_read(&bc->state);
		if (state == TRIM) {
			if (bio_op(bio) == REQ_OP_DISCARD)
				ret = add_trim(bc, bio);
			else if (bio_data_dir(bio) == WRITE)
				ret = remove_trim(bc, bio);
			else
				/* pass-through */;
		} else if (state == CHECKPOINT) {
			if (bio->bi_iter.bi_sector == 0)
				ret = handle_sector0(bc, bio);
			else if (bio_data_dir(bio) == WRITE)
				ret = queue_write(bc, bio);
			else
				/* pass-through */;
		} else {
			/* pass-through */
		}
		mutex_unlock(&bc->ranges_lock);
	}

	if (ret == DM_MAPIO_REMAPPED)
		return remap_unless_illegal_trim(bc, bio);

	return ret;
}

static void dm_bow_tablestatus(struct dm_target *ti, char *result,
			       unsigned int maxlen)
{
	char *end = result + maxlen;
	struct bow_context *bc = ti->private;
	struct rb_node *i;
	int trimmed_list_length = 0;
	int trimmed_range_count = 0;
	struct bow_range *br;

	if (maxlen == 0)
		return;
	result[0] = 0;

	list_for_each_entry(br, &bc->trimmed_list, trimmed_list)
		if (br->type == TRIMMED) {
			++trimmed_list_length;
		} else {
			scnprintf(result, end - result,
				  "ERROR: non-trimmed entry in trimmed_list");
			return;
		}

	if (!rb_first(&bc->ranges)) {
		scnprintf(result, end - result, "ERROR: Empty ranges");
		return;
	}

	if (container_of(rb_first(&bc->ranges), struct bow_range, node)
	    ->sector) {
		scnprintf(result, end - result,
			 "ERROR: First range does not start at sector 0");
		return;
	}

	for (i = rb_first(&bc->ranges); i; i = rb_next(i)) {
		struct bow_range *br = container_of(i, struct bow_range, node);

		result += scnprintf(result, end - result, "%s: %lu",
				    readable_type[br->type], br->sector);
		if (result >= end)
			return;

		result += scnprintf(result, end - result, "\n");
		if (result >= end)
			return;

		if (br->type == TRIMMED)
			++trimmed_range_count;

		if (br->type == TOP) {
			if (br->sector != ti->len) {
				scnprintf(result, end - result,
					 "\nERROR: Top sector is incorrect");
			}

			if (&br->node != rb_last(&bc->ranges)) {
				scnprintf(result, end - result,
					  "\nERROR: Top sector is not last");
			}

			break;
		}

		if (!rb_next(i)) {
			scnprintf(result, end - result,
				  "\nERROR: Last range not of type TOP");
			return;
		}

		if (br->sector > range_top(br)) {
			scnprintf(result, end - result,
				  "\nERROR: sectors out of order");
			return;
		}
	}

	if (trimmed_range_count != trimmed_list_length)
		scnprintf(result, end - result,
			  "\nERROR: not all trimmed ranges in trimmed list");
}

static void dm_bow_status(struct dm_target *ti, status_type_t type,
			  unsigned int status_flags, char *result,
			  unsigned int maxlen)
{
	switch (type) {
	case STATUSTYPE_INFO:
		if (maxlen)
			result[0] = 0;
		break;

	case STATUSTYPE_TABLE:
		dm_bow_tablestatus(ti, result, maxlen);
		break;
	}
}

int dm_bow_prepare_ioctl(struct dm_target *ti, struct block_device **bdev)
{
	struct bow_context *bc = ti->private;
	struct dm_dev *dev = bc->dev;

	*bdev = dev->bdev;
	/* Only pass ioctls through if the device sizes match exactly. */
	return ti->len != i_size_read(dev->bdev->bd_inode) >> SECTOR_SHIFT;
}

static int dm_bow_iterate_devices(struct dm_target *ti,
				  iterate_devices_callout_fn fn, void *data)
{
	struct bow_context *bc = ti->private;

	return fn(ti, bc->dev, 0, ti->len, data);
}

static struct target_type bow_target = {
	.name   = "bow",
	.version = {1, 1, 1},
	.module = THIS_MODULE,
	.ctr    = dm_bow_ctr,
	.dtr    = dm_bow_dtr,
	.map    = dm_bow_map,
	.status = dm_bow_status,
	.prepare_ioctl  = dm_bow_prepare_ioctl,
	.iterate_devices = dm_bow_iterate_devices,
};

int __init dm_bow_init(void)
{
	int r = dm_register_target(&bow_target);

	if (r < 0)
		DMERR("registering bow failed %d", r);
	return r;
}

void dm_bow_exit(void)
{
	dm_unregister_target(&bow_target);
}

MODULE_LICENSE("GPL");

module_init(dm_bow_init);
module_exit(dm_bow_exit);
