/*
 * Copyright (C) 2015 Shaohua Li <shli@fb.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/raid/md_p.h>
#include <linux/crc32.h>
#include <linux/random.h>
#include "md.h"
#include "raid5.h"

/*
 * metadata/data stored in disk with 4k size unit (a block) regardless
 * underneath hardware sector size. only works with PAGE_SIZE == 4096
 */
#define BLOCK_SECTORS (8)

struct r5l_log {
	struct md_rdev *rdev;

	u32 uuid_checksum;

	sector_t device_size;		/* log device size, round to
					 * BLOCK_SECTORS */

	sector_t last_checkpoint;	/* log tail. where recovery scan
					 * starts from */
	u64 last_cp_seq;		/* log tail sequence */

	sector_t log_start;		/* log head. where new data appends */
	u64 seq;			/* log head sequence */

	struct mutex io_mutex;
	struct r5l_io_unit *current_io;	/* current io_unit accepting new data */

	spinlock_t io_list_lock;
	struct list_head running_ios;	/* io_units which are still running,
					 * and have not yet been completely
					 * written to the log */
	struct list_head io_end_ios;	/* io_units which have been completely
					 * written to the log but not yet written
					 * to the RAID */

	struct kmem_cache *io_kc;

	struct list_head no_space_stripes; /* pending stripes, log has no space */
	spinlock_t no_space_stripes_lock;
};

/*
 * an IO range starts from a meta data block and end at the next meta data
 * block. The io unit's the meta data block tracks data/parity followed it. io
 * unit is written to log disk with normal write, as we always flush log disk
 * first and then start move data to raid disks, there is no requirement to
 * write io unit with FLUSH/FUA
 */
struct r5l_io_unit {
	struct r5l_log *log;

	struct page *meta_page;	/* store meta block */
	int meta_offset;	/* current offset in meta_page */

	struct bio_list bios;
	atomic_t pending_io;	/* pending bios not written to log yet */
	struct bio *current_bio;/* current_bio accepting new data */

	atomic_t pending_stripe;/* how many stripes not flushed to raid */
	u64 seq;		/* seq number of the metablock */
	sector_t log_start;	/* where the io_unit starts */
	sector_t log_end;	/* where the io_unit ends */
	struct list_head log_sibling; /* log->running_ios */
	struct list_head stripe_list; /* stripes added to the io_unit */

	int state;
	wait_queue_head_t wait_state;
};

/* r5l_io_unit state */
enum r5l_io_unit_state {
	IO_UNIT_RUNNING = 0,	/* accepting new IO */
	IO_UNIT_IO_START = 1,	/* io_unit bio start writing to log,
				 * don't accepting new bio */
	IO_UNIT_IO_END = 2,	/* io_unit bio finish writing to log */
	IO_UNIT_STRIPE_START = 3, /* stripes of io_unit are flushing to raid */
	IO_UNIT_STRIPE_END = 4,	/* stripes data finished writing to raid */
};

static sector_t r5l_ring_add(struct r5l_log *log, sector_t start, sector_t inc)
{
	start += inc;
	if (start >= log->device_size)
		start = start - log->device_size;
	return start;
}

static sector_t r5l_ring_distance(struct r5l_log *log, sector_t start,
				  sector_t end)
{
	if (end >= start)
		return end - start;
	else
		return end + log->device_size - start;
}

static bool r5l_has_free_space(struct r5l_log *log, sector_t size)
{
	sector_t used_size;

	used_size = r5l_ring_distance(log, log->last_checkpoint,
					log->log_start);

	return log->device_size > used_size + size;
}

static struct r5l_io_unit *r5l_alloc_io_unit(struct r5l_log *log)
{
	struct r5l_io_unit *io;
	/* We can't handle memory allocate failure so far */
	gfp_t gfp = GFP_NOIO | __GFP_NOFAIL;

	io = kmem_cache_zalloc(log->io_kc, gfp);
	io->log = log;
	io->meta_page = alloc_page(gfp | __GFP_ZERO);

	bio_list_init(&io->bios);
	INIT_LIST_HEAD(&io->log_sibling);
	INIT_LIST_HEAD(&io->stripe_list);
	io->state = IO_UNIT_RUNNING;
	init_waitqueue_head(&io->wait_state);
	return io;
}

static void r5l_free_io_unit(struct r5l_log *log, struct r5l_io_unit *io)
{
	__free_page(io->meta_page);
	kmem_cache_free(log->io_kc, io);
}

static void r5l_move_io_unit_list(struct list_head *from, struct list_head *to,
				  enum r5l_io_unit_state state)
{
	struct r5l_io_unit *io;

	while (!list_empty(from)) {
		io = list_first_entry(from, struct r5l_io_unit, log_sibling);
		/* don't change list order */
		if (io->state >= state)
			list_move_tail(&io->log_sibling, to);
		else
			break;
	}
}

static void r5l_wake_reclaim(struct r5l_log *log, sector_t space);
static void __r5l_set_io_unit_state(struct r5l_io_unit *io,
				    enum r5l_io_unit_state state)
{
	struct r5l_log *log = io->log;

	if (WARN_ON(io->state >= state))
		return;
	io->state = state;
	if (state == IO_UNIT_IO_END)
		r5l_move_io_unit_list(&log->running_ios, &log->io_end_ios,
				      IO_UNIT_IO_END);
	wake_up(&io->wait_state);
}

static void r5l_set_io_unit_state(struct r5l_io_unit *io,
				  enum r5l_io_unit_state state)
{
	struct r5l_log *log = io->log;
	unsigned long flags;

	spin_lock_irqsave(&log->io_list_lock, flags);
	__r5l_set_io_unit_state(io, state);
	spin_unlock_irqrestore(&log->io_list_lock, flags);
}

/* XXX: totally ignores I/O errors */
static void r5l_log_endio(struct bio *bio)
{
	struct r5l_io_unit *io = bio->bi_private;
	struct r5l_log *log = io->log;

	bio_put(bio);

	if (!atomic_dec_and_test(&io->pending_io))
		return;

	r5l_set_io_unit_state(io, IO_UNIT_IO_END);
	md_wakeup_thread(log->rdev->mddev->thread);
}

static void r5l_submit_current_io(struct r5l_log *log)
{
	struct r5l_io_unit *io = log->current_io;
	struct r5l_meta_block *block;
	struct bio *bio;
	u32 crc;

	if (!io)
		return;

	block = page_address(io->meta_page);
	block->meta_size = cpu_to_le32(io->meta_offset);
	crc = crc32_le(log->uuid_checksum, (void *)block, PAGE_SIZE);
	block->checksum = cpu_to_le32(crc);

	log->current_io = NULL;
	r5l_set_io_unit_state(io, IO_UNIT_IO_START);

	while ((bio = bio_list_pop(&io->bios))) {
		/* all IO must start from rdev->data_offset */
		bio->bi_iter.bi_sector += log->rdev->data_offset;
		submit_bio(WRITE, bio);
	}
}

static struct r5l_io_unit *r5l_new_meta(struct r5l_log *log)
{
	struct r5l_io_unit *io;
	struct r5l_meta_block *block;
	struct bio *bio;

	io = r5l_alloc_io_unit(log);

	block = page_address(io->meta_page);
	block->magic = cpu_to_le32(R5LOG_MAGIC);
	block->version = R5LOG_VERSION;
	block->seq = cpu_to_le64(log->seq);
	block->position = cpu_to_le64(log->log_start);

	io->log_start = log->log_start;
	io->meta_offset = sizeof(struct r5l_meta_block);
	io->seq = log->seq;

	bio = bio_kmalloc(GFP_NOIO | __GFP_NOFAIL, BIO_MAX_PAGES);
	io->current_bio = bio;
	bio->bi_rw = WRITE;
	bio->bi_bdev = log->rdev->bdev;
	bio->bi_iter.bi_sector = log->log_start;
	bio_add_page(bio, io->meta_page, PAGE_SIZE, 0);
	bio->bi_end_io = r5l_log_endio;
	bio->bi_private = io;

	bio_list_add(&io->bios, bio);
	atomic_inc(&io->pending_io);

	log->seq++;
	log->log_start = r5l_ring_add(log, log->log_start, BLOCK_SECTORS);
	io->log_end = log->log_start;
	/* current bio hit disk end */
	if (log->log_start == 0)
		io->current_bio = NULL;

	spin_lock_irq(&log->io_list_lock);
	list_add_tail(&io->log_sibling, &log->running_ios);
	spin_unlock_irq(&log->io_list_lock);

	return io;
}

static int r5l_get_meta(struct r5l_log *log, unsigned int payload_size)
{
	struct r5l_io_unit *io;

	io = log->current_io;
	if (io && io->meta_offset + payload_size > PAGE_SIZE)
		r5l_submit_current_io(log);
	io = log->current_io;
	if (io)
		return 0;

	log->current_io = r5l_new_meta(log);
	return 0;
}

static void r5l_append_payload_meta(struct r5l_log *log, u16 type,
				    sector_t location,
				    u32 checksum1, u32 checksum2,
				    bool checksum2_valid)
{
	struct r5l_io_unit *io = log->current_io;
	struct r5l_payload_data_parity *payload;

	payload = page_address(io->meta_page) + io->meta_offset;
	payload->header.type = cpu_to_le16(type);
	payload->header.flags = cpu_to_le16(0);
	payload->size = cpu_to_le32((1 + !!checksum2_valid) <<
				    (PAGE_SHIFT - 9));
	payload->location = cpu_to_le64(location);
	payload->checksum[0] = cpu_to_le32(checksum1);
	if (checksum2_valid)
		payload->checksum[1] = cpu_to_le32(checksum2);

	io->meta_offset += sizeof(struct r5l_payload_data_parity) +
		sizeof(__le32) * (1 + !!checksum2_valid);
}

static void r5l_append_payload_page(struct r5l_log *log, struct page *page)
{
	struct r5l_io_unit *io = log->current_io;

alloc_bio:
	if (!io->current_bio) {
		struct bio *bio;

		bio = bio_kmalloc(GFP_NOIO | __GFP_NOFAIL, BIO_MAX_PAGES);
		bio->bi_rw = WRITE;
		bio->bi_bdev = log->rdev->bdev;
		bio->bi_iter.bi_sector = log->log_start;
		bio->bi_end_io = r5l_log_endio;
		bio->bi_private = io;
		bio_list_add(&io->bios, bio);
		atomic_inc(&io->pending_io);
		io->current_bio = bio;
	}
	if (!bio_add_page(io->current_bio, page, PAGE_SIZE, 0)) {
		io->current_bio = NULL;
		goto alloc_bio;
	}
	log->log_start = r5l_ring_add(log, log->log_start,
				      BLOCK_SECTORS);
	/* current bio hit disk end */
	if (log->log_start == 0)
		io->current_bio = NULL;

	io->log_end = log->log_start;
}

static void r5l_log_stripe(struct r5l_log *log, struct stripe_head *sh,
			   int data_pages, int parity_pages)
{
	int i;
	int meta_size;
	struct r5l_io_unit *io;

	meta_size =
		((sizeof(struct r5l_payload_data_parity) + sizeof(__le32))
		 * data_pages) +
		sizeof(struct r5l_payload_data_parity) +
		sizeof(__le32) * parity_pages;

	r5l_get_meta(log, meta_size);
	io = log->current_io;

	for (i = 0; i < sh->disks; i++) {
		if (!test_bit(R5_Wantwrite, &sh->dev[i].flags))
			continue;
		if (i == sh->pd_idx || i == sh->qd_idx)
			continue;
		r5l_append_payload_meta(log, R5LOG_PAYLOAD_DATA,
					raid5_compute_blocknr(sh, i, 0),
					sh->dev[i].log_checksum, 0, false);
		r5l_append_payload_page(log, sh->dev[i].page);
	}

	if (sh->qd_idx >= 0) {
		r5l_append_payload_meta(log, R5LOG_PAYLOAD_PARITY,
					sh->sector, sh->dev[sh->pd_idx].log_checksum,
					sh->dev[sh->qd_idx].log_checksum, true);
		r5l_append_payload_page(log, sh->dev[sh->pd_idx].page);
		r5l_append_payload_page(log, sh->dev[sh->qd_idx].page);
	} else {
		r5l_append_payload_meta(log, R5LOG_PAYLOAD_PARITY,
					sh->sector, sh->dev[sh->pd_idx].log_checksum,
					0, false);
		r5l_append_payload_page(log, sh->dev[sh->pd_idx].page);
	}

	list_add_tail(&sh->log_list, &io->stripe_list);
	atomic_inc(&io->pending_stripe);
	sh->log_io = io;
}

/*
 * running in raid5d, where reclaim could wait for raid5d too (when it flushes
 * data from log to raid disks), so we shouldn't wait for reclaim here
 */
int r5l_write_stripe(struct r5l_log *log, struct stripe_head *sh)
{
	int write_disks = 0;
	int data_pages, parity_pages;
	int meta_size;
	int reserve;
	int i;

	if (!log)
		return -EAGAIN;
	/* Don't support stripe batch */
	if (sh->log_io || !test_bit(R5_Wantwrite, &sh->dev[sh->pd_idx].flags) ||
	    test_bit(STRIPE_SYNCING, &sh->state)) {
		/* the stripe is written to log, we start writing it to raid */
		clear_bit(STRIPE_LOG_TRAPPED, &sh->state);
		return -EAGAIN;
	}

	for (i = 0; i < sh->disks; i++) {
		void *addr;

		if (!test_bit(R5_Wantwrite, &sh->dev[i].flags))
			continue;
		write_disks++;
		/* checksum is already calculated in last run */
		if (test_bit(STRIPE_LOG_TRAPPED, &sh->state))
			continue;
		addr = kmap_atomic(sh->dev[i].page);
		sh->dev[i].log_checksum = crc32_le(log->uuid_checksum,
						   addr, PAGE_SIZE);
		kunmap_atomic(addr);
	}
	parity_pages = 1 + !!(sh->qd_idx >= 0);
	data_pages = write_disks - parity_pages;

	meta_size =
		((sizeof(struct r5l_payload_data_parity) + sizeof(__le32))
		 * data_pages) +
		sizeof(struct r5l_payload_data_parity) +
		sizeof(__le32) * parity_pages;
	/* Doesn't work with very big raid array */
	if (meta_size + sizeof(struct r5l_meta_block) > PAGE_SIZE)
		return -EINVAL;

	set_bit(STRIPE_LOG_TRAPPED, &sh->state);
	atomic_inc(&sh->count);

	mutex_lock(&log->io_mutex);
	/* meta + data */
	reserve = (1 + write_disks) << (PAGE_SHIFT - 9);
	if (r5l_has_free_space(log, reserve))
		r5l_log_stripe(log, sh, data_pages, parity_pages);
	else {
		spin_lock(&log->no_space_stripes_lock);
		list_add_tail(&sh->log_list, &log->no_space_stripes);
		spin_unlock(&log->no_space_stripes_lock);

		r5l_wake_reclaim(log, reserve);
	}
	mutex_unlock(&log->io_mutex);

	return 0;
}

void r5l_write_stripe_run(struct r5l_log *log)
{
	if (!log)
		return;
	mutex_lock(&log->io_mutex);
	r5l_submit_current_io(log);
	mutex_unlock(&log->io_mutex);
}

/* This will run after log space is reclaimed */
static void r5l_run_no_space_stripes(struct r5l_log *log)
{
	struct stripe_head *sh;

	spin_lock(&log->no_space_stripes_lock);
	while (!list_empty(&log->no_space_stripes)) {
		sh = list_first_entry(&log->no_space_stripes,
				      struct stripe_head, log_list);
		list_del_init(&sh->log_list);
		set_bit(STRIPE_HANDLE, &sh->state);
		raid5_release_stripe(sh);
	}
	spin_unlock(&log->no_space_stripes_lock);
}

static void r5l_wake_reclaim(struct r5l_log *log, sector_t space)
{
	/* will implement later */
}

static int r5l_recovery_log(struct r5l_log *log)
{
	/* fake recovery */
	log->seq = log->last_cp_seq + 1;
	log->log_start = r5l_ring_add(log, log->last_checkpoint, BLOCK_SECTORS);
	return 0;
}

static void r5l_write_super(struct r5l_log *log, sector_t cp)
{
	struct mddev *mddev = log->rdev->mddev;

	log->rdev->journal_tail = cp;
	set_bit(MD_CHANGE_DEVS, &mddev->flags);
}

static int r5l_load_log(struct r5l_log *log)
{
	struct md_rdev *rdev = log->rdev;
	struct page *page;
	struct r5l_meta_block *mb;
	sector_t cp = log->rdev->journal_tail;
	u32 stored_crc, expected_crc;
	bool create_super = false;
	int ret;

	/* Make sure it's valid */
	if (cp >= rdev->sectors || round_down(cp, BLOCK_SECTORS) != cp)
		cp = 0;
	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	if (!sync_page_io(rdev, cp, PAGE_SIZE, page, READ, false)) {
		ret = -EIO;
		goto ioerr;
	}
	mb = page_address(page);

	if (le32_to_cpu(mb->magic) != R5LOG_MAGIC ||
	    mb->version != R5LOG_VERSION) {
		create_super = true;
		goto create;
	}
	stored_crc = le32_to_cpu(mb->checksum);
	mb->checksum = 0;
	expected_crc = crc32_le(log->uuid_checksum, (void *)mb, PAGE_SIZE);
	if (stored_crc != expected_crc) {
		create_super = true;
		goto create;
	}
	if (le64_to_cpu(mb->position) != cp) {
		create_super = true;
		goto create;
	}
create:
	if (create_super) {
		log->last_cp_seq = prandom_u32();
		cp = 0;
		/*
		 * Make sure super points to correct address. Log might have
		 * data very soon. If super hasn't correct log tail address,
		 * recovery can't find the log
		 */
		r5l_write_super(log, cp);
	} else
		log->last_cp_seq = le64_to_cpu(mb->seq);

	log->device_size = round_down(rdev->sectors, BLOCK_SECTORS);
	log->last_checkpoint = cp;

	__free_page(page);

	return r5l_recovery_log(log);
ioerr:
	__free_page(page);
	return ret;
}

int r5l_init_log(struct r5conf *conf, struct md_rdev *rdev)
{
	struct r5l_log *log;

	if (PAGE_SIZE != 4096)
		return -EINVAL;
	log = kzalloc(sizeof(*log), GFP_KERNEL);
	if (!log)
		return -ENOMEM;
	log->rdev = rdev;

	log->uuid_checksum = crc32_le(~0, (void *)rdev->mddev->uuid,
				      sizeof(rdev->mddev->uuid));

	mutex_init(&log->io_mutex);

	spin_lock_init(&log->io_list_lock);
	INIT_LIST_HEAD(&log->running_ios);

	log->io_kc = KMEM_CACHE(r5l_io_unit, 0);
	if (!log->io_kc)
		goto io_kc;

	INIT_LIST_HEAD(&log->no_space_stripes);
	spin_lock_init(&log->no_space_stripes_lock);

	if (r5l_load_log(log))
		goto error;

	conf->log = log;
	return 0;
error:
	kmem_cache_destroy(log->io_kc);
io_kc:
	kfree(log);
	return -EINVAL;
}

void r5l_exit_log(struct r5l_log *log)
{
	kmem_cache_destroy(log->io_kc);
	kfree(log);
}
