// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * This is a test "dust" device, which fails reads on specified
 * sectors, emulating the behavior of a hard disk drive sending
 * a "Read Medium Error" sense.
 *
 */

#include <linux/device-mapper.h>
#include <linux/module.h>
#include <linux/rbtree.h>

#define DM_MSG_PREFIX "dust"

struct badblock {
	struct rb_node node;
	sector_t bb;
	unsigned char wr_fail_cnt;
};

struct dust_device {
	struct dm_dev *dev;
	struct rb_root badblocklist;
	unsigned long long badblock_count;
	spinlock_t dust_lock;
	unsigned int blksz;
	int sect_per_block_shift;
	unsigned int sect_per_block;
	sector_t start;
	bool fail_read_on_bb:1;
	bool quiet_mode:1;
};

static struct badblock *dust_rb_search(struct rb_root *root, sector_t blk)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct badblock *bblk = rb_entry(node, struct badblock, node);

		if (bblk->bb > blk)
			node = node->rb_left;
		else if (bblk->bb < blk)
			node = node->rb_right;
		else
			return bblk;
	}

	return NULL;
}

static bool dust_rb_insert(struct rb_root *root, struct badblock *new)
{
	struct badblock *bblk;
	struct rb_node **link = &root->rb_node, *parent = NULL;
	sector_t value = new->bb;

	while (*link) {
		parent = *link;
		bblk = rb_entry(parent, struct badblock, node);

		if (bblk->bb > value)
			link = &(*link)->rb_left;
		else if (bblk->bb < value)
			link = &(*link)->rb_right;
		else
			return false;
	}

	rb_link_node(&new->node, parent, link);
	rb_insert_color(&new->node, root);

	return true;
}

static int dust_remove_block(struct dust_device *dd, unsigned long long block)
{
	struct badblock *bblock;
	unsigned long flags;

	spin_lock_irqsave(&dd->dust_lock, flags);
	bblock = dust_rb_search(&dd->badblocklist, block);

	if (bblock == NULL) {
		if (!dd->quiet_mode) {
			DMERR("%s: block %llu not found in badblocklist",
			      __func__, block);
		}
		spin_unlock_irqrestore(&dd->dust_lock, flags);
		return -EINVAL;
	}

	rb_erase(&bblock->node, &dd->badblocklist);
	dd->badblock_count--;
	if (!dd->quiet_mode)
		DMINFO("%s: badblock removed at block %llu", __func__, block);
	kfree(bblock);
	spin_unlock_irqrestore(&dd->dust_lock, flags);

	return 0;
}

static int dust_add_block(struct dust_device *dd, unsigned long long block,
			  unsigned char wr_fail_cnt)
{
	struct badblock *bblock;
	unsigned long flags;

	bblock = kmalloc(sizeof(*bblock), GFP_KERNEL);
	if (bblock == NULL) {
		if (!dd->quiet_mode)
			DMERR("%s: badblock allocation failed", __func__);
		return -ENOMEM;
	}

	spin_lock_irqsave(&dd->dust_lock, flags);
	bblock->bb = block;
	bblock->wr_fail_cnt = wr_fail_cnt;
	if (!dust_rb_insert(&dd->badblocklist, bblock)) {
		if (!dd->quiet_mode) {
			DMERR("%s: block %llu already in badblocklist",
			      __func__, block);
		}
		spin_unlock_irqrestore(&dd->dust_lock, flags);
		kfree(bblock);
		return -EINVAL;
	}

	dd->badblock_count++;
	if (!dd->quiet_mode) {
		DMINFO("%s: badblock added at block %llu with write fail count %hhu",
		       __func__, block, wr_fail_cnt);
	}
	spin_unlock_irqrestore(&dd->dust_lock, flags);

	return 0;
}

static int dust_query_block(struct dust_device *dd, unsigned long long block)
{
	struct badblock *bblock;
	unsigned long flags;

	spin_lock_irqsave(&dd->dust_lock, flags);
	bblock = dust_rb_search(&dd->badblocklist, block);
	if (bblock != NULL)
		DMINFO("%s: block %llu found in badblocklist", __func__, block);
	else
		DMINFO("%s: block %llu not found in badblocklist", __func__, block);
	spin_unlock_irqrestore(&dd->dust_lock, flags);

	return 0;
}

static int __dust_map_read(struct dust_device *dd, sector_t thisblock)
{
	struct badblock *bblk = dust_rb_search(&dd->badblocklist, thisblock);

	if (bblk)
		return DM_MAPIO_KILL;

	return DM_MAPIO_REMAPPED;
}

static int dust_map_read(struct dust_device *dd, sector_t thisblock,
			 bool fail_read_on_bb)
{
	unsigned long flags;
	int r = DM_MAPIO_REMAPPED;

	if (fail_read_on_bb) {
		thisblock >>= dd->sect_per_block_shift;
		spin_lock_irqsave(&dd->dust_lock, flags);
		r = __dust_map_read(dd, thisblock);
		spin_unlock_irqrestore(&dd->dust_lock, flags);
	}

	return r;
}

static int __dust_map_write(struct dust_device *dd, sector_t thisblock)
{
	struct badblock *bblk = dust_rb_search(&dd->badblocklist, thisblock);

	if (bblk && bblk->wr_fail_cnt > 0) {
		bblk->wr_fail_cnt--;
		return DM_MAPIO_KILL;
	}

	if (bblk) {
		rb_erase(&bblk->node, &dd->badblocklist);
		dd->badblock_count--;
		kfree(bblk);
		if (!dd->quiet_mode) {
			sector_div(thisblock, dd->sect_per_block);
			DMINFO("block %llu removed from badblocklist by write",
			       (unsigned long long)thisblock);
		}
	}

	return DM_MAPIO_REMAPPED;
}

static int dust_map_write(struct dust_device *dd, sector_t thisblock,
			  bool fail_read_on_bb)
{
	unsigned long flags;
	int ret = DM_MAPIO_REMAPPED;

	if (fail_read_on_bb) {
		thisblock >>= dd->sect_per_block_shift;
		spin_lock_irqsave(&dd->dust_lock, flags);
		ret = __dust_map_write(dd, thisblock);
		spin_unlock_irqrestore(&dd->dust_lock, flags);
	}

	return ret;
}

static int dust_map(struct dm_target *ti, struct bio *bio)
{
	struct dust_device *dd = ti->private;
	int r;

	bio_set_dev(bio, dd->dev->bdev);
	bio->bi_iter.bi_sector = dd->start + dm_target_offset(ti, bio->bi_iter.bi_sector);

	if (bio_data_dir(bio) == READ)
		r = dust_map_read(dd, bio->bi_iter.bi_sector, dd->fail_read_on_bb);
	else
		r = dust_map_write(dd, bio->bi_iter.bi_sector, dd->fail_read_on_bb);

	return r;
}

static bool __dust_clear_badblocks(struct rb_root *tree,
				   unsigned long long count)
{
	struct rb_node *node = NULL, *nnode = NULL;

	nnode = rb_first(tree);
	if (nnode == NULL) {
		BUG_ON(count != 0);
		return false;
	}

	while (nnode) {
		node = nnode;
		nnode = rb_next(node);
		rb_erase(node, tree);
		count--;
		kfree(node);
	}
	BUG_ON(count != 0);
	BUG_ON(tree->rb_node != NULL);

	return true;
}

static int dust_clear_badblocks(struct dust_device *dd)
{
	unsigned long flags;
	struct rb_root badblocklist;
	unsigned long long badblock_count;

	spin_lock_irqsave(&dd->dust_lock, flags);
	badblocklist = dd->badblocklist;
	badblock_count = dd->badblock_count;
	dd->badblocklist = RB_ROOT;
	dd->badblock_count = 0;
	spin_unlock_irqrestore(&dd->dust_lock, flags);

	if (!__dust_clear_badblocks(&badblocklist, badblock_count))
		DMINFO("%s: no badblocks found", __func__);
	else
		DMINFO("%s: badblocks cleared", __func__);

	return 0;
}

/*
 * Target parameters:
 *
 * <device_path> <offset> <blksz>
 *
 * device_path: path to the block device
 * offset: offset to data area from start of device_path
 * blksz: block size (minimum 512, maximum 1073741824, must be a power of 2)
 */
static int dust_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct dust_device *dd;
	unsigned long long tmp;
	char dummy;
	unsigned int blksz;
	unsigned int sect_per_block;
	sector_t DUST_MAX_BLKSZ_SECTORS = 2097152;
	sector_t max_block_sectors = min(ti->len, DUST_MAX_BLKSZ_SECTORS);

	if (argc != 3) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}

	if (kstrtouint(argv[2], 10, &blksz) || !blksz) {
		ti->error = "Invalid block size parameter";
		return -EINVAL;
	}

	if (blksz < 512) {
		ti->error = "Block size must be at least 512";
		return -EINVAL;
	}

	if (!is_power_of_2(blksz)) {
		ti->error = "Block size must be a power of 2";
		return -EINVAL;
	}

	if (to_sector(blksz) > max_block_sectors) {
		ti->error = "Block size is too large";
		return -EINVAL;
	}

	sect_per_block = (blksz >> SECTOR_SHIFT);

	if (sscanf(argv[1], "%llu%c", &tmp, &dummy) != 1 || tmp != (sector_t)tmp) {
		ti->error = "Invalid device offset sector";
		return -EINVAL;
	}

	dd = kzalloc(sizeof(struct dust_device), GFP_KERNEL);
	if (dd == NULL) {
		ti->error = "Cannot allocate context";
		return -ENOMEM;
	}

	if (dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &dd->dev)) {
		ti->error = "Device lookup failed";
		kfree(dd);
		return -EINVAL;
	}

	dd->sect_per_block = sect_per_block;
	dd->blksz = blksz;
	dd->start = tmp;

	dd->sect_per_block_shift = __ffs(sect_per_block);

	/*
	 * Whether to fail a read on a "bad" block.
	 * Defaults to false; enabled later by message.
	 */
	dd->fail_read_on_bb = false;

	/*
	 * Initialize bad block list rbtree.
	 */
	dd->badblocklist = RB_ROOT;
	dd->badblock_count = 0;
	spin_lock_init(&dd->dust_lock);

	dd->quiet_mode = false;

	BUG_ON(dm_set_target_max_io_len(ti, dd->sect_per_block) != 0);

	ti->num_discard_bios = 1;
	ti->num_flush_bios = 1;
	ti->private = dd;

	return 0;
}

static void dust_dtr(struct dm_target *ti)
{
	struct dust_device *dd = ti->private;

	__dust_clear_badblocks(&dd->badblocklist, dd->badblock_count);
	dm_put_device(ti, dd->dev);
	kfree(dd);
}

static int dust_message(struct dm_target *ti, unsigned int argc, char **argv,
			char *result_buf, unsigned int maxlen)
{
	struct dust_device *dd = ti->private;
	sector_t size = i_size_read(dd->dev->bdev->bd_inode) >> SECTOR_SHIFT;
	bool invalid_msg = false;
	int r = -EINVAL;
	unsigned long long tmp, block;
	unsigned char wr_fail_cnt;
	unsigned int tmp_ui;
	unsigned long flags;
	char dummy;

	if (argc == 1) {
		if (!strcasecmp(argv[0], "addbadblock") ||
		    !strcasecmp(argv[0], "removebadblock") ||
		    !strcasecmp(argv[0], "queryblock")) {
			DMERR("%s requires an additional argument", argv[0]);
		} else if (!strcasecmp(argv[0], "disable")) {
			DMINFO("disabling read failures on bad sectors");
			dd->fail_read_on_bb = false;
			r = 0;
		} else if (!strcasecmp(argv[0], "enable")) {
			DMINFO("enabling read failures on bad sectors");
			dd->fail_read_on_bb = true;
			r = 0;
		} else if (!strcasecmp(argv[0], "countbadblocks")) {
			spin_lock_irqsave(&dd->dust_lock, flags);
			DMINFO("countbadblocks: %llu badblock(s) found",
			       dd->badblock_count);
			spin_unlock_irqrestore(&dd->dust_lock, flags);
			r = 0;
		} else if (!strcasecmp(argv[0], "clearbadblocks")) {
			r = dust_clear_badblocks(dd);
		} else if (!strcasecmp(argv[0], "quiet")) {
			if (!dd->quiet_mode)
				dd->quiet_mode = true;
			else
				dd->quiet_mode = false;
			r = 0;
		} else {
			invalid_msg = true;
		}
	} else if (argc == 2) {
		if (sscanf(argv[1], "%llu%c", &tmp, &dummy) != 1)
			return r;

		block = tmp;
		sector_div(size, dd->sect_per_block);
		if (block > size) {
			DMERR("selected block value out of range");
			return r;
		}

		if (!strcasecmp(argv[0], "addbadblock"))
			r = dust_add_block(dd, block, 0);
		else if (!strcasecmp(argv[0], "removebadblock"))
			r = dust_remove_block(dd, block);
		else if (!strcasecmp(argv[0], "queryblock"))
			r = dust_query_block(dd, block);
		else
			invalid_msg = true;

	} else if (argc == 3) {
		if (sscanf(argv[1], "%llu%c", &tmp, &dummy) != 1)
			return r;

		if (sscanf(argv[2], "%u%c", &tmp_ui, &dummy) != 1)
			return r;

		block = tmp;
		if (tmp_ui > 255) {
			DMERR("selected write fail count out of range");
			return r;
		}
		wr_fail_cnt = tmp_ui;
		sector_div(size, dd->sect_per_block);
		if (block > size) {
			DMERR("selected block value out of range");
			return r;
		}

		if (!strcasecmp(argv[0], "addbadblock"))
			r = dust_add_block(dd, block, wr_fail_cnt);
		else
			invalid_msg = true;

	} else
		DMERR("invalid number of arguments '%d'", argc);

	if (invalid_msg)
		DMERR("unrecognized message '%s' received", argv[0]);

	return r;
}

static void dust_status(struct dm_target *ti, status_type_t type,
			unsigned int status_flags, char *result, unsigned int maxlen)
{
	struct dust_device *dd = ti->private;
	unsigned int sz = 0;

	switch (type) {
	case STATUSTYPE_INFO:
		DMEMIT("%s %s %s", dd->dev->name,
		       dd->fail_read_on_bb ? "fail_read_on_bad_block" : "bypass",
		       dd->quiet_mode ? "quiet" : "verbose");
		break;

	case STATUSTYPE_TABLE:
		DMEMIT("%s %llu %u", dd->dev->name,
		       (unsigned long long)dd->start, dd->blksz);
		break;
	}
}

static int dust_prepare_ioctl(struct dm_target *ti, struct block_device **bdev)
{
	struct dust_device *dd = ti->private;
	struct dm_dev *dev = dd->dev;

	*bdev = dev->bdev;

	/*
	 * Only pass ioctls through if the device sizes match exactly.
	 */
	if (dd->start ||
	    ti->len != i_size_read(dev->bdev->bd_inode) >> SECTOR_SHIFT)
		return 1;

	return 0;
}

static int dust_iterate_devices(struct dm_target *ti, iterate_devices_callout_fn fn,
				void *data)
{
	struct dust_device *dd = ti->private;

	return fn(ti, dd->dev, dd->start, ti->len, data);
}

static struct target_type dust_target = {
	.name = "dust",
	.version = {1, 0, 0},
	.module = THIS_MODULE,
	.ctr = dust_ctr,
	.dtr = dust_dtr,
	.iterate_devices = dust_iterate_devices,
	.map = dust_map,
	.message = dust_message,
	.status = dust_status,
	.prepare_ioctl = dust_prepare_ioctl,
};

static int __init dm_dust_init(void)
{
	int r = dm_register_target(&dust_target);

	if (r < 0)
		DMERR("dm_register_target failed %d", r);

	return r;
}

static void __exit dm_dust_exit(void)
{
	dm_unregister_target(&dust_target);
}

module_init(dm_dust_init);
module_exit(dm_dust_exit);

MODULE_DESCRIPTION(DM_NAME " dust test target");
MODULE_AUTHOR("Bryan Gurney <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
