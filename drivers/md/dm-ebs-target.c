/*
 * Copyright (C) 2020 Red Hat GmbH
 *
 * This file is released under the GPL.
 *
 * Device-mapper target to emulate smaller logical block
 * size on backing devices exposing (natively) larger ones.
 *
 * E.g. 512 byte sector emulation on 4K native disks.
 */

#include "dm.h"
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/dm-bufio.h>

#define DM_MSG_PREFIX "ebs"

static void ebs_dtr(struct dm_target *ti);

/* Emulated block size context. */
struct ebs_c {
	struct dm_dev *dev;		/* Underlying device to emulate block size on. */
	struct dm_bufio_client *bufio;	/* Use dm-bufio for read and read-modify-write processing. */
	struct workqueue_struct *wq;	/* Workqueue for ^ processing of bios. */
	struct work_struct ws;		/* Work item used for ^. */
	struct bio_list bios_in;	/* Worker bios input list. */
	spinlock_t lock;		/* Guard bios input list above. */
	sector_t start;			/* <start> table line argument, see ebs_ctr below. */
	unsigned int e_bs;		/* Emulated block size in sectors exposed to upper layer. */
	unsigned int u_bs;		/* Underlying block size in sectors retrievd from/set on lower layer device. */
	unsigned char block_shift;	/* bitshift sectors -> blocks used in dm-bufio API. */
	bool u_bs_set:1;		/* Flag to indicate underlying block size is set on table line. */
};

static inline sector_t __sector_to_block(struct ebs_c *ec, sector_t sector)
{
	return sector >> ec->block_shift;
}

static inline sector_t __block_mod(sector_t sector, unsigned int bs)
{
	return sector & (bs - 1);
}

/* Return number of blocks for a bio, accounting for misalignement of start and end sectors. */
static inline unsigned int __nr_blocks(struct ebs_c *ec, struct bio *bio)
{
	sector_t end_sector = __block_mod(bio->bi_iter.bi_sector, ec->u_bs) + bio_sectors(bio);

	return __sector_to_block(ec, end_sector) + (__block_mod(end_sector, ec->u_bs) ? 1 : 0);
}

static inline bool __ebs_check_bs(unsigned int bs)
{
	return bs && is_power_of_2(bs);
}

/*
 * READ/WRITE:
 *
 * copy blocks between bufio blocks and bio vector's (partial/overlapping) pages.
 */
static int __ebs_rw_bvec(struct ebs_c *ec, int rw, struct bio_vec *bv, struct bvec_iter *iter)
{
	int r = 0;
	unsigned char *ba, *pa;
	unsigned int cur_len;
	unsigned int bv_len = bv->bv_len;
	unsigned int buf_off = to_bytes(__block_mod(iter->bi_sector, ec->u_bs));
	sector_t block = __sector_to_block(ec, iter->bi_sector);
	struct dm_buffer *b;

	if (unlikely(!bv->bv_page || !bv_len))
		return -EIO;

	pa = page_address(bv->bv_page) + bv->bv_offset;

	/* Handle overlapping page <-> blocks */
	while (bv_len) {
		cur_len = min(dm_bufio_get_block_size(ec->bufio) - buf_off, bv_len);

		/* Avoid reading for writes in case bio vector's page overwrites block completely. */
		if (rw == READ || buf_off || bv_len < dm_bufio_get_block_size(ec->bufio))
			ba = dm_bufio_read(ec->bufio, block, &b);
		else
			ba = dm_bufio_new(ec->bufio, block, &b);

		if (unlikely(IS_ERR(ba))) {
			/*
			 * Carry on with next buffer, if any, to issue all possible
			 * data but return error.
			 */
			r = PTR_ERR(ba);
		} else {
			/* Copy data to/from bio to buffer if read/new was successful above. */
			ba += buf_off;
			if (rw == READ) {
				memcpy(pa, ba, cur_len);
				flush_dcache_page(bv->bv_page);
			} else {
				flush_dcache_page(bv->bv_page);
				memcpy(ba, pa, cur_len);
				dm_bufio_mark_partial_buffer_dirty(b, buf_off, buf_off + cur_len);
			}

			dm_bufio_release(b);
		}

		pa += cur_len;
		bv_len -= cur_len;
		buf_off = 0;
		block++;
	}

	return r;
}

/* READ/WRITE: iterate bio vector's copying between (partial) pages and bufio blocks. */
static int __ebs_rw_bio(struct ebs_c *ec, int rw, struct bio *bio)
{
	int r = 0, rr;
	struct bio_vec bv;
	struct bvec_iter iter;

	bio_for_each_bvec(bv, bio, iter) {
		rr = __ebs_rw_bvec(ec, rw, &bv, &iter);
		if (rr)
			r = rr;
	}

	return r;
}

/*
 * Discard bio's blocks, i.e. pass discards down.
 *
 * Avoid discarding partial blocks at beginning and end;
 * return 0 in case no blocks can be discarded as a result.
 */
static int __ebs_discard_bio(struct ebs_c *ec, struct bio *bio)
{
	sector_t block, blocks, sector = bio->bi_iter.bi_sector;

	block = __sector_to_block(ec, sector);
	blocks = __nr_blocks(ec, bio);

	/*
	 * Partial first underlying block (__nr_blocks() may have
	 * resulted in one block).
	 */
	if (__block_mod(sector, ec->u_bs)) {
		block++;
		blocks--;
	}

	/* Partial last underlying block if any. */
	if (blocks && __block_mod(bio_end_sector(bio), ec->u_bs))
		blocks--;

	return blocks ? dm_bufio_issue_discard(ec->bufio, block, blocks) : 0;
}

/* Release blocks them from the bufio cache. */
static void __ebs_forget_bio(struct ebs_c *ec, struct bio *bio)
{
	sector_t blocks, sector = bio->bi_iter.bi_sector;

	blocks = __nr_blocks(ec, bio);

	dm_bufio_forget_buffers(ec->bufio, __sector_to_block(ec, sector), blocks);
}

/* Worker funtion to process incoming bios. */
static void __ebs_process_bios(struct work_struct *ws)
{
	int r;
	bool write = false;
	sector_t block1, block2;
	struct ebs_c *ec = container_of(ws, struct ebs_c, ws);
	struct bio *bio;
	struct bio_list bios;

	bio_list_init(&bios);

	spin_lock_irq(&ec->lock);
	bios = ec->bios_in;
	bio_list_init(&ec->bios_in);
	spin_unlock_irq(&ec->lock);

	/* Prefetch all read and any mis-aligned write buffers */
	bio_list_for_each(bio, &bios) {
		block1 = __sector_to_block(ec, bio->bi_iter.bi_sector);
		if (bio_op(bio) == REQ_OP_READ)
			dm_bufio_prefetch(ec->bufio, block1, __nr_blocks(ec, bio));
		else if (bio_op(bio) == REQ_OP_WRITE && !(bio->bi_opf & REQ_PREFLUSH)) {
			block2 = __sector_to_block(ec, bio_end_sector(bio));
			if (__block_mod(bio->bi_iter.bi_sector, ec->u_bs))
				dm_bufio_prefetch(ec->bufio, block1, 1);
			if (__block_mod(bio_end_sector(bio), ec->u_bs) && block2 != block1)
				dm_bufio_prefetch(ec->bufio, block2, 1);
		}
	}

	bio_list_for_each(bio, &bios) {
		r = -EIO;
		if (bio_op(bio) == REQ_OP_READ)
			r = __ebs_rw_bio(ec, READ, bio);
		else if (bio_op(bio) == REQ_OP_WRITE) {
			write = true;
			r = __ebs_rw_bio(ec, WRITE, bio);
		} else if (bio_op(bio) == REQ_OP_DISCARD) {
			__ebs_forget_bio(ec, bio);
			r = __ebs_discard_bio(ec, bio);
		}

		if (r < 0)
			bio->bi_status = errno_to_blk_status(r);
	}

	/*
	 * We write dirty buffers after processing I/O on them
	 * but before we endio thus addressing REQ_FUA/REQ_SYNC.
	 */
	r = write ? dm_bufio_write_dirty_buffers(ec->bufio) : 0;

	while ((bio = bio_list_pop(&bios))) {
		/* Any other request is endioed. */
		if (unlikely(r && bio_op(bio) == REQ_OP_WRITE))
			bio_io_error(bio);
		else
			bio_endio(bio);
	}
}

/*
 * Construct an emulated block size mapping: <dev_path> <offset> <ebs> [<ubs>]
 *
 * <dev_path>: path of the underlying device
 * <offset>: offset in 512 bytes sectors into <dev_path>
 * <ebs>: emulated block size in units of 512 bytes exposed to the upper layer
 * [<ubs>]: underlying block size in units of 512 bytes imposed on the lower layer;
 * 	    optional, if not supplied, retrieve logical block size from underlying device
 */
static int ebs_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	int r;
	unsigned short tmp1;
	unsigned long long tmp;
	char dummy;
	struct ebs_c *ec;

	if (argc < 3 || argc > 4) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}

	ec = ti->private = kzalloc(sizeof(*ec), GFP_KERNEL);
	if (!ec) {
		ti->error = "Cannot allocate ebs context";
		return -ENOMEM;
	}

	r = -EINVAL;
	if (sscanf(argv[1], "%llu%c", &tmp, &dummy) != 1 ||
	    tmp != (sector_t)tmp ||
	    (sector_t)tmp >= ti->len) {
		ti->error = "Invalid device offset sector";
		goto bad;
	}
	ec->start = tmp;

	if (sscanf(argv[2], "%hu%c", &tmp1, &dummy) != 1 ||
	    !__ebs_check_bs(tmp1) ||
	    to_bytes(tmp1) > PAGE_SIZE) {
		ti->error = "Invalid emulated block size";
		goto bad;
	}
	ec->e_bs = tmp1;

	if (argc > 3) {
		if (sscanf(argv[3], "%hu%c", &tmp1, &dummy) != 1 || !__ebs_check_bs(tmp1)) {
			ti->error = "Invalid underlying block size";
			goto bad;
		}
		ec->u_bs = tmp1;
		ec->u_bs_set = true;
	} else
		ec->u_bs_set = false;

	r = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &ec->dev);
	if (r) {
		ti->error = "Device lookup failed";
		ec->dev = NULL;
		goto bad;
	}

	r = -EINVAL;
	if (!ec->u_bs_set) {
		ec->u_bs = to_sector(bdev_logical_block_size(ec->dev->bdev));
		if (!__ebs_check_bs(ec->u_bs)) {
			ti->error = "Invalid retrieved underlying block size";
			goto bad;
		}
	}

	if (!ec->u_bs_set && ec->e_bs == ec->u_bs)
		DMINFO("Emulation superfluous: emulated equal to underlying block size");

	if (__block_mod(ec->start, ec->u_bs)) {
		ti->error = "Device offset must be multiple of underlying block size";
		goto bad;
	}

	ec->bufio = dm_bufio_client_create(ec->dev->bdev, to_bytes(ec->u_bs), 1, 0, NULL, NULL);
	if (IS_ERR(ec->bufio)) {
		ti->error = "Cannot create dm bufio client";
		r = PTR_ERR(ec->bufio);
		ec->bufio = NULL;
		goto bad;
	}

	ec->wq = alloc_ordered_workqueue("dm-" DM_MSG_PREFIX, WQ_MEM_RECLAIM);
	if (!ec->wq) {
		ti->error = "Cannot create dm-" DM_MSG_PREFIX " workqueue";
		r = -ENOMEM;
		goto bad;
	}

	ec->block_shift = __ffs(ec->u_bs);
	INIT_WORK(&ec->ws, &__ebs_process_bios);
	bio_list_init(&ec->bios_in);
	spin_lock_init(&ec->lock);

	ti->num_flush_bios = 1;
	ti->num_discard_bios = 1;
	ti->num_secure_erase_bios = 0;
	ti->num_write_same_bios = 0;
	ti->num_write_zeroes_bios = 0;
	return 0;
bad:
	ebs_dtr(ti);
	return r;
}

static void ebs_dtr(struct dm_target *ti)
{
	struct ebs_c *ec = ti->private;

	if (ec->wq)
		destroy_workqueue(ec->wq);
	if (ec->bufio)
		dm_bufio_client_destroy(ec->bufio);
	if (ec->dev)
		dm_put_device(ti, ec->dev);
	kfree(ec);
}

static int ebs_map(struct dm_target *ti, struct bio *bio)
{
	struct ebs_c *ec = ti->private;

	bio_set_dev(bio, ec->dev->bdev);
	bio->bi_iter.bi_sector = ec->start + dm_target_offset(ti, bio->bi_iter.bi_sector);

	if (unlikely(bio->bi_opf & REQ_OP_FLUSH))
		return DM_MAPIO_REMAPPED;
	/*
	 * Only queue for bufio processing in case of partial or overlapping buffers
	 * -or-
	 * emulation with ebs == ubs aiming for tests of dm-bufio overhead.
	 */
	if (likely(__block_mod(bio->bi_iter.bi_sector, ec->u_bs) ||
		   __block_mod(bio_end_sector(bio), ec->u_bs) ||
		   ec->e_bs == ec->u_bs)) {
		spin_lock_irq(&ec->lock);
		bio_list_add(&ec->bios_in, bio);
		spin_unlock_irq(&ec->lock);

		queue_work(ec->wq, &ec->ws);

		return DM_MAPIO_SUBMITTED;
	}

	/* Forget any buffer content relative to this direct backing device I/O. */
	__ebs_forget_bio(ec, bio);

	return DM_MAPIO_REMAPPED;
}

static void ebs_status(struct dm_target *ti, status_type_t type,
		       unsigned status_flags, char *result, unsigned maxlen)
{
	struct ebs_c *ec = ti->private;

	switch (type) {
	case STATUSTYPE_INFO:
		*result = '\0';
		break;
	case STATUSTYPE_TABLE:
		snprintf(result, maxlen, ec->u_bs_set ? "%s %llu %u %u" : "%s %llu %u",
			 ec->dev->name, (unsigned long long) ec->start, ec->e_bs, ec->u_bs);
		break;
	}
}

static int ebs_prepare_ioctl(struct dm_target *ti, struct block_device **bdev)
{
	struct ebs_c *ec = ti->private;
	struct dm_dev *dev = ec->dev;

	/*
	 * Only pass ioctls through if the device sizes match exactly.
	 */
	*bdev = dev->bdev;
	return !!(ec->start || ti->len != i_size_read(dev->bdev->bd_inode) >> SECTOR_SHIFT);
}

static void ebs_io_hints(struct dm_target *ti, struct queue_limits *limits)
{
	struct ebs_c *ec = ti->private;

	limits->logical_block_size = to_bytes(ec->e_bs);
	limits->physical_block_size = to_bytes(ec->u_bs);
	limits->alignment_offset = limits->physical_block_size;
	blk_limits_io_min(limits, limits->logical_block_size);
}

static int ebs_iterate_devices(struct dm_target *ti,
				  iterate_devices_callout_fn fn, void *data)
{
	struct ebs_c *ec = ti->private;

	return fn(ti, ec->dev, ec->start, ti->len, data);
}

static struct target_type ebs_target = {
	.name		 = "ebs",
	.version	 = {1, 0, 1},
	.features	 = DM_TARGET_PASSES_INTEGRITY,
	.module		 = THIS_MODULE,
	.ctr		 = ebs_ctr,
	.dtr		 = ebs_dtr,
	.map		 = ebs_map,
	.status		 = ebs_status,
	.io_hints	 = ebs_io_hints,
	.prepare_ioctl	 = ebs_prepare_ioctl,
	.iterate_devices = ebs_iterate_devices,
};

static int __init dm_ebs_init(void)
{
	int r = dm_register_target(&ebs_target);

	if (r < 0)
		DMERR("register failed %d", r);

	return r;
}

static void dm_ebs_exit(void)
{
	dm_unregister_target(&ebs_target);
}

module_init(dm_ebs_init);
module_exit(dm_ebs_exit);

MODULE_AUTHOR("Heinz Mauelshagen <dm-devel@redhat.com>");
MODULE_DESCRIPTION(DM_NAME " emulated block size target");
MODULE_LICENSE("GPL");
