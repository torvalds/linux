// SPDX-License-Identifier: GPL-2.0-only
/*
 * Partial Parity Log for closing the RAID5 write hole
 * Copyright (c) 2017, Intel Corporation.
 */

#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/crc32c.h>
#include <linux/async_tx.h>
#include <linux/raid/md_p.h>
#include "md.h"
#include "raid5.h"
#include "raid5-log.h"

/*
 * PPL consists of a 4KB header (struct ppl_header) and at least 128KB for
 * partial parity data. The header contains an array of entries
 * (struct ppl_header_entry) which describe the logged write requests.
 * Partial parity for the entries comes after the header, written in the same
 * sequence as the entries:
 *
 * Header
 *   entry0
 *   ...
 *   entryN
 * PP data
 *   PP for entry0
 *   ...
 *   PP for entryN
 *
 * An entry describes one or more consecutive stripe_heads, up to a full
 * stripe. The modifed raid data chunks form an m-by-n matrix, where m is the
 * number of stripe_heads in the entry and n is the number of modified data
 * disks. Every stripe_head in the entry must write to the same data disks.
 * An example of a valid case described by a single entry (writes to the first
 * stripe of a 4 disk array, 16k chunk size):
 *
 * sh->sector   dd0   dd1   dd2    ppl
 *            +-----+-----+-----+
 * 0          | --- | --- | --- | +----+
 * 8          | -W- | -W- | --- | | pp |   data_sector = 8
 * 16         | -W- | -W- | --- | | pp |   data_size = 3 * 2 * 4k
 * 24         | -W- | -W- | --- | | pp |   pp_size = 3 * 4k
 *            +-----+-----+-----+ +----+
 *
 * data_sector is the first raid sector of the modified data, data_size is the
 * total size of modified data and pp_size is the size of partial parity for
 * this entry. Entries for full stripe writes contain no partial parity
 * (pp_size = 0), they only mark the stripes for which parity should be
 * recalculated after an unclean shutdown. Every entry holds a checksum of its
 * partial parity, the header also has a checksum of the header itself.
 *
 * A write request is always logged to the PPL instance stored on the parity
 * disk of the corresponding stripe. For each member disk there is one ppl_log
 * used to handle logging for this disk, independently from others. They are
 * grouped in child_logs array in struct ppl_conf, which is assigned to
 * r5conf->log_private.
 *
 * ppl_io_unit represents a full PPL write, header_page contains the ppl_header.
 * PPL entries for logged stripes are added in ppl_log_stripe(). A stripe_head
 * can be appended to the last entry if it meets the conditions for a valid
 * entry described above, otherwise a new entry is added. Checksums of entries
 * are calculated incrementally as stripes containing partial parity are being
 * added. ppl_submit_iounit() calculates the checksum of the header and submits
 * a bio containing the header page and partial parity pages (sh->ppl_page) for
 * all stripes of the io_unit. When the PPL write completes, the stripes
 * associated with the io_unit are released and raid5d starts writing their data
 * and parity. When all stripes are written, the io_unit is freed and the next
 * can be submitted.
 *
 * An io_unit is used to gather stripes until it is submitted or becomes full
 * (if the maximum number of entries or size of PPL is reached). Another io_unit
 * can't be submitted until the previous has completed (PPL and stripe
 * data+parity is written). The log->io_list tracks all io_units of a log
 * (for a single member disk). New io_units are added to the end of the list
 * and the first io_unit is submitted, if it is not submitted already.
 * The current io_unit accepting new stripes is always at the end of the list.
 *
 * If write-back cache is enabled for any of the disks in the array, its data
 * must be flushed before next io_unit is submitted.
 */

#define PPL_SPACE_SIZE (128 * 1024)

struct ppl_conf {
	struct mddev *mddev;

	/* array of child logs, one for each raid disk */
	struct ppl_log *child_logs;
	int count;

	int block_size;		/* the logical block size used for data_sector
				 * in ppl_header_entry */
	u32 signature;		/* raid array identifier */
	atomic64_t seq;		/* current log write sequence number */

	struct kmem_cache *io_kc;
	mempool_t io_pool;
	struct bio_set bs;
	struct bio_set flush_bs;

	/* used only for recovery */
	int recovered_entries;
	int mismatch_count;

	/* stripes to retry if failed to allocate io_unit */
	struct list_head no_mem_stripes;
	spinlock_t no_mem_stripes_lock;

	unsigned short write_hint;
};

struct ppl_log {
	struct ppl_conf *ppl_conf;	/* shared between all log instances */

	struct md_rdev *rdev;		/* array member disk associated with
					 * this log instance */
	struct mutex io_mutex;
	struct ppl_io_unit *current_io;	/* current io_unit accepting new data
					 * always at the end of io_list */
	spinlock_t io_list_lock;
	struct list_head io_list;	/* all io_units of this log */

	sector_t next_io_sector;
	unsigned int entry_space;
	bool use_multippl;
	bool wb_cache_on;
	unsigned long disk_flush_bitmap;
};

#define PPL_IO_INLINE_BVECS 32

struct ppl_io_unit {
	struct ppl_log *log;

	struct page *header_page;	/* for ppl_header */

	unsigned int entries_count;	/* number of entries in ppl_header */
	unsigned int pp_size;		/* total size current of partial parity */

	u64 seq;			/* sequence number of this log write */
	struct list_head log_sibling;	/* log->io_list */

	struct list_head stripe_list;	/* stripes added to the io_unit */
	atomic_t pending_stripes;	/* how many stripes not written to raid */
	atomic_t pending_flushes;	/* how many disk flushes are in progress */

	bool submitted;			/* true if write to log started */

	/* inline bio and its biovec for submitting the iounit */
	struct bio bio;
	struct bio_vec biovec[PPL_IO_INLINE_BVECS];
};

struct dma_async_tx_descriptor *
ops_run_partial_parity(struct stripe_head *sh, struct raid5_percpu *percpu,
		       struct dma_async_tx_descriptor *tx)
{
	int disks = sh->disks;
	struct page **srcs = percpu->scribble;
	int count = 0, pd_idx = sh->pd_idx, i;
	struct async_submit_ctl submit;

	pr_debug("%s: stripe %llu\n", __func__, (unsigned long long)sh->sector);

	/*
	 * Partial parity is the XOR of stripe data chunks that are not changed
	 * during the write request. Depending on available data
	 * (read-modify-write vs. reconstruct-write case) we calculate it
	 * differently.
	 */
	if (sh->reconstruct_state == reconstruct_state_prexor_drain_run) {
		/*
		 * rmw: xor old data and parity from updated disks
		 * This is calculated earlier by ops_run_prexor5() so just copy
		 * the parity dev page.
		 */
		srcs[count++] = sh->dev[pd_idx].page;
	} else if (sh->reconstruct_state == reconstruct_state_drain_run) {
		/* rcw: xor data from all not updated disks */
		for (i = disks; i--;) {
			struct r5dev *dev = &sh->dev[i];
			if (test_bit(R5_UPTODATE, &dev->flags))
				srcs[count++] = dev->page;
		}
	} else {
		return tx;
	}

	init_async_submit(&submit, ASYNC_TX_FENCE|ASYNC_TX_XOR_ZERO_DST, tx,
			  NULL, sh, (void *) (srcs + sh->disks + 2));

	if (count == 1)
		tx = async_memcpy(sh->ppl_page, srcs[0], 0, 0, PAGE_SIZE,
				  &submit);
	else
		tx = async_xor(sh->ppl_page, srcs, 0, count, PAGE_SIZE,
			       &submit);

	return tx;
}

static void *ppl_io_pool_alloc(gfp_t gfp_mask, void *pool_data)
{
	struct kmem_cache *kc = pool_data;
	struct ppl_io_unit *io;

	io = kmem_cache_alloc(kc, gfp_mask);
	if (!io)
		return NULL;

	io->header_page = alloc_page(gfp_mask);
	if (!io->header_page) {
		kmem_cache_free(kc, io);
		return NULL;
	}

	return io;
}

static void ppl_io_pool_free(void *element, void *pool_data)
{
	struct kmem_cache *kc = pool_data;
	struct ppl_io_unit *io = element;

	__free_page(io->header_page);
	kmem_cache_free(kc, io);
}

static struct ppl_io_unit *ppl_new_iounit(struct ppl_log *log,
					  struct stripe_head *sh)
{
	struct ppl_conf *ppl_conf = log->ppl_conf;
	struct ppl_io_unit *io;
	struct ppl_header *pplhdr;
	struct page *header_page;

	io = mempool_alloc(&ppl_conf->io_pool, GFP_NOWAIT);
	if (!io)
		return NULL;

	header_page = io->header_page;
	memset(io, 0, sizeof(*io));
	io->header_page = header_page;

	io->log = log;
	INIT_LIST_HEAD(&io->log_sibling);
	INIT_LIST_HEAD(&io->stripe_list);
	atomic_set(&io->pending_stripes, 0);
	atomic_set(&io->pending_flushes, 0);
	bio_init(&io->bio, io->biovec, PPL_IO_INLINE_BVECS);

	pplhdr = page_address(io->header_page);
	clear_page(pplhdr);
	memset(pplhdr->reserved, 0xff, PPL_HDR_RESERVED);
	pplhdr->signature = cpu_to_le32(ppl_conf->signature);

	io->seq = atomic64_add_return(1, &ppl_conf->seq);
	pplhdr->generation = cpu_to_le64(io->seq);

	return io;
}

static int ppl_log_stripe(struct ppl_log *log, struct stripe_head *sh)
{
	struct ppl_io_unit *io = log->current_io;
	struct ppl_header_entry *e = NULL;
	struct ppl_header *pplhdr;
	int i;
	sector_t data_sector = 0;
	int data_disks = 0;
	struct r5conf *conf = sh->raid_conf;

	pr_debug("%s: stripe: %llu\n", __func__, (unsigned long long)sh->sector);

	/* check if current io_unit is full */
	if (io && (io->pp_size == log->entry_space ||
		   io->entries_count == PPL_HDR_MAX_ENTRIES)) {
		pr_debug("%s: add io_unit blocked by seq: %llu\n",
			 __func__, io->seq);
		io = NULL;
	}

	/* add a new unit if there is none or the current is full */
	if (!io) {
		io = ppl_new_iounit(log, sh);
		if (!io)
			return -ENOMEM;
		spin_lock_irq(&log->io_list_lock);
		list_add_tail(&io->log_sibling, &log->io_list);
		spin_unlock_irq(&log->io_list_lock);

		log->current_io = io;
	}

	for (i = 0; i < sh->disks; i++) {
		struct r5dev *dev = &sh->dev[i];

		if (i != sh->pd_idx && test_bit(R5_Wantwrite, &dev->flags)) {
			if (!data_disks || dev->sector < data_sector)
				data_sector = dev->sector;
			data_disks++;
		}
	}
	BUG_ON(!data_disks);

	pr_debug("%s: seq: %llu data_sector: %llu data_disks: %d\n", __func__,
		 io->seq, (unsigned long long)data_sector, data_disks);

	pplhdr = page_address(io->header_page);

	if (io->entries_count > 0) {
		struct ppl_header_entry *last =
				&pplhdr->entries[io->entries_count - 1];
		struct stripe_head *sh_last = list_last_entry(
				&io->stripe_list, struct stripe_head, log_list);
		u64 data_sector_last = le64_to_cpu(last->data_sector);
		u32 data_size_last = le32_to_cpu(last->data_size);

		/*
		 * Check if we can append the stripe to the last entry. It must
		 * be just after the last logged stripe and write to the same
		 * disks. Use bit shift and logarithm to avoid 64-bit division.
		 */
		if ((sh->sector == sh_last->sector + STRIPE_SECTORS) &&
		    (data_sector >> ilog2(conf->chunk_sectors) ==
		     data_sector_last >> ilog2(conf->chunk_sectors)) &&
		    ((data_sector - data_sector_last) * data_disks ==
		     data_size_last >> 9))
			e = last;
	}

	if (!e) {
		e = &pplhdr->entries[io->entries_count++];
		e->data_sector = cpu_to_le64(data_sector);
		e->parity_disk = cpu_to_le32(sh->pd_idx);
		e->checksum = cpu_to_le32(~0);
	}

	le32_add_cpu(&e->data_size, data_disks << PAGE_SHIFT);

	/* don't write any PP if full stripe write */
	if (!test_bit(STRIPE_FULL_WRITE, &sh->state)) {
		le32_add_cpu(&e->pp_size, PAGE_SIZE);
		io->pp_size += PAGE_SIZE;
		e->checksum = cpu_to_le32(crc32c_le(le32_to_cpu(e->checksum),
						    page_address(sh->ppl_page),
						    PAGE_SIZE));
	}

	list_add_tail(&sh->log_list, &io->stripe_list);
	atomic_inc(&io->pending_stripes);
	sh->ppl_io = io;

	return 0;
}

int ppl_write_stripe(struct r5conf *conf, struct stripe_head *sh)
{
	struct ppl_conf *ppl_conf = conf->log_private;
	struct ppl_io_unit *io = sh->ppl_io;
	struct ppl_log *log;

	if (io || test_bit(STRIPE_SYNCING, &sh->state) || !sh->ppl_page ||
	    !test_bit(R5_Wantwrite, &sh->dev[sh->pd_idx].flags) ||
	    !test_bit(R5_Insync, &sh->dev[sh->pd_idx].flags)) {
		clear_bit(STRIPE_LOG_TRAPPED, &sh->state);
		return -EAGAIN;
	}

	log = &ppl_conf->child_logs[sh->pd_idx];

	mutex_lock(&log->io_mutex);

	if (!log->rdev || test_bit(Faulty, &log->rdev->flags)) {
		mutex_unlock(&log->io_mutex);
		return -EAGAIN;
	}

	set_bit(STRIPE_LOG_TRAPPED, &sh->state);
	clear_bit(STRIPE_DELAYED, &sh->state);
	atomic_inc(&sh->count);

	if (ppl_log_stripe(log, sh)) {
		spin_lock_irq(&ppl_conf->no_mem_stripes_lock);
		list_add_tail(&sh->log_list, &ppl_conf->no_mem_stripes);
		spin_unlock_irq(&ppl_conf->no_mem_stripes_lock);
	}

	mutex_unlock(&log->io_mutex);

	return 0;
}

static void ppl_log_endio(struct bio *bio)
{
	struct ppl_io_unit *io = bio->bi_private;
	struct ppl_log *log = io->log;
	struct ppl_conf *ppl_conf = log->ppl_conf;
	struct stripe_head *sh, *next;

	pr_debug("%s: seq: %llu\n", __func__, io->seq);

	if (bio->bi_status)
		md_error(ppl_conf->mddev, log->rdev);

	list_for_each_entry_safe(sh, next, &io->stripe_list, log_list) {
		list_del_init(&sh->log_list);

		set_bit(STRIPE_HANDLE, &sh->state);
		raid5_release_stripe(sh);
	}
}

static void ppl_submit_iounit_bio(struct ppl_io_unit *io, struct bio *bio)
{
	char b[BDEVNAME_SIZE];

	pr_debug("%s: seq: %llu size: %u sector: %llu dev: %s\n",
		 __func__, io->seq, bio->bi_iter.bi_size,
		 (unsigned long long)bio->bi_iter.bi_sector,
		 bio_devname(bio, b));

	submit_bio(bio);
}

static void ppl_submit_iounit(struct ppl_io_unit *io)
{
	struct ppl_log *log = io->log;
	struct ppl_conf *ppl_conf = log->ppl_conf;
	struct ppl_header *pplhdr = page_address(io->header_page);
	struct bio *bio = &io->bio;
	struct stripe_head *sh;
	int i;

	bio->bi_private = io;

	if (!log->rdev || test_bit(Faulty, &log->rdev->flags)) {
		ppl_log_endio(bio);
		return;
	}

	for (i = 0; i < io->entries_count; i++) {
		struct ppl_header_entry *e = &pplhdr->entries[i];

		pr_debug("%s: seq: %llu entry: %d data_sector: %llu pp_size: %u data_size: %u\n",
			 __func__, io->seq, i, le64_to_cpu(e->data_sector),
			 le32_to_cpu(e->pp_size), le32_to_cpu(e->data_size));

		e->data_sector = cpu_to_le64(le64_to_cpu(e->data_sector) >>
					     ilog2(ppl_conf->block_size >> 9));
		e->checksum = cpu_to_le32(~le32_to_cpu(e->checksum));
	}

	pplhdr->entries_count = cpu_to_le32(io->entries_count);
	pplhdr->checksum = cpu_to_le32(~crc32c_le(~0, pplhdr, PPL_HEADER_SIZE));

	/* Rewind the buffer if current PPL is larger then remaining space */
	if (log->use_multippl &&
	    log->rdev->ppl.sector + log->rdev->ppl.size - log->next_io_sector <
	    (PPL_HEADER_SIZE + io->pp_size) >> 9)
		log->next_io_sector = log->rdev->ppl.sector;


	bio->bi_end_io = ppl_log_endio;
	bio->bi_opf = REQ_OP_WRITE | REQ_FUA;
	bio_set_dev(bio, log->rdev->bdev);
	bio->bi_iter.bi_sector = log->next_io_sector;
	bio_add_page(bio, io->header_page, PAGE_SIZE, 0);
	bio->bi_write_hint = ppl_conf->write_hint;

	pr_debug("%s: log->current_io_sector: %llu\n", __func__,
	    (unsigned long long)log->next_io_sector);

	if (log->use_multippl)
		log->next_io_sector += (PPL_HEADER_SIZE + io->pp_size) >> 9;

	WARN_ON(log->disk_flush_bitmap != 0);

	list_for_each_entry(sh, &io->stripe_list, log_list) {
		for (i = 0; i < sh->disks; i++) {
			struct r5dev *dev = &sh->dev[i];

			if ((ppl_conf->child_logs[i].wb_cache_on) &&
			    (test_bit(R5_Wantwrite, &dev->flags))) {
				set_bit(i, &log->disk_flush_bitmap);
			}
		}

		/* entries for full stripe writes have no partial parity */
		if (test_bit(STRIPE_FULL_WRITE, &sh->state))
			continue;

		if (!bio_add_page(bio, sh->ppl_page, PAGE_SIZE, 0)) {
			struct bio *prev = bio;

			bio = bio_alloc_bioset(GFP_NOIO, BIO_MAX_PAGES,
					       &ppl_conf->bs);
			bio->bi_opf = prev->bi_opf;
			bio->bi_write_hint = prev->bi_write_hint;
			bio_copy_dev(bio, prev);
			bio->bi_iter.bi_sector = bio_end_sector(prev);
			bio_add_page(bio, sh->ppl_page, PAGE_SIZE, 0);

			bio_chain(bio, prev);
			ppl_submit_iounit_bio(io, prev);
		}
	}

	ppl_submit_iounit_bio(io, bio);
}

static void ppl_submit_current_io(struct ppl_log *log)
{
	struct ppl_io_unit *io;

	spin_lock_irq(&log->io_list_lock);

	io = list_first_entry_or_null(&log->io_list, struct ppl_io_unit,
				      log_sibling);
	if (io && io->submitted)
		io = NULL;

	spin_unlock_irq(&log->io_list_lock);

	if (io) {
		io->submitted = true;

		if (io == log->current_io)
			log->current_io = NULL;

		ppl_submit_iounit(io);
	}
}

void ppl_write_stripe_run(struct r5conf *conf)
{
	struct ppl_conf *ppl_conf = conf->log_private;
	struct ppl_log *log;
	int i;

	for (i = 0; i < ppl_conf->count; i++) {
		log = &ppl_conf->child_logs[i];

		mutex_lock(&log->io_mutex);
		ppl_submit_current_io(log);
		mutex_unlock(&log->io_mutex);
	}
}

static void ppl_io_unit_finished(struct ppl_io_unit *io)
{
	struct ppl_log *log = io->log;
	struct ppl_conf *ppl_conf = log->ppl_conf;
	struct r5conf *conf = ppl_conf->mddev->private;
	unsigned long flags;

	pr_debug("%s: seq: %llu\n", __func__, io->seq);

	local_irq_save(flags);

	spin_lock(&log->io_list_lock);
	list_del(&io->log_sibling);
	spin_unlock(&log->io_list_lock);

	mempool_free(io, &ppl_conf->io_pool);

	spin_lock(&ppl_conf->no_mem_stripes_lock);
	if (!list_empty(&ppl_conf->no_mem_stripes)) {
		struct stripe_head *sh;

		sh = list_first_entry(&ppl_conf->no_mem_stripes,
				      struct stripe_head, log_list);
		list_del_init(&sh->log_list);
		set_bit(STRIPE_HANDLE, &sh->state);
		raid5_release_stripe(sh);
	}
	spin_unlock(&ppl_conf->no_mem_stripes_lock);

	local_irq_restore(flags);

	wake_up(&conf->wait_for_quiescent);
}

static void ppl_flush_endio(struct bio *bio)
{
	struct ppl_io_unit *io = bio->bi_private;
	struct ppl_log *log = io->log;
	struct ppl_conf *ppl_conf = log->ppl_conf;
	struct r5conf *conf = ppl_conf->mddev->private;
	char b[BDEVNAME_SIZE];

	pr_debug("%s: dev: %s\n", __func__, bio_devname(bio, b));

	if (bio->bi_status) {
		struct md_rdev *rdev;

		rcu_read_lock();
		rdev = md_find_rdev_rcu(conf->mddev, bio_dev(bio));
		if (rdev)
			md_error(rdev->mddev, rdev);
		rcu_read_unlock();
	}

	bio_put(bio);

	if (atomic_dec_and_test(&io->pending_flushes)) {
		ppl_io_unit_finished(io);
		md_wakeup_thread(conf->mddev->thread);
	}
}

static void ppl_do_flush(struct ppl_io_unit *io)
{
	struct ppl_log *log = io->log;
	struct ppl_conf *ppl_conf = log->ppl_conf;
	struct r5conf *conf = ppl_conf->mddev->private;
	int raid_disks = conf->raid_disks;
	int flushed_disks = 0;
	int i;

	atomic_set(&io->pending_flushes, raid_disks);

	for_each_set_bit(i, &log->disk_flush_bitmap, raid_disks) {
		struct md_rdev *rdev;
		struct block_device *bdev = NULL;

		rcu_read_lock();
		rdev = rcu_dereference(conf->disks[i].rdev);
		if (rdev && !test_bit(Faulty, &rdev->flags))
			bdev = rdev->bdev;
		rcu_read_unlock();

		if (bdev) {
			struct bio *bio;
			char b[BDEVNAME_SIZE];

			bio = bio_alloc_bioset(GFP_NOIO, 0, &ppl_conf->flush_bs);
			bio_set_dev(bio, bdev);
			bio->bi_private = io;
			bio->bi_opf = REQ_OP_WRITE | REQ_PREFLUSH;
			bio->bi_end_io = ppl_flush_endio;

			pr_debug("%s: dev: %s\n", __func__,
				 bio_devname(bio, b));

			submit_bio(bio);
			flushed_disks++;
		}
	}

	log->disk_flush_bitmap = 0;

	for (i = flushed_disks ; i < raid_disks; i++) {
		if (atomic_dec_and_test(&io->pending_flushes))
			ppl_io_unit_finished(io);
	}
}

static inline bool ppl_no_io_unit_submitted(struct r5conf *conf,
					    struct ppl_log *log)
{
	struct ppl_io_unit *io;

	io = list_first_entry_or_null(&log->io_list, struct ppl_io_unit,
				      log_sibling);

	return !io || !io->submitted;
}

void ppl_quiesce(struct r5conf *conf, int quiesce)
{
	struct ppl_conf *ppl_conf = conf->log_private;
	int i;

	if (quiesce) {
		for (i = 0; i < ppl_conf->count; i++) {
			struct ppl_log *log = &ppl_conf->child_logs[i];

			spin_lock_irq(&log->io_list_lock);
			wait_event_lock_irq(conf->wait_for_quiescent,
					    ppl_no_io_unit_submitted(conf, log),
					    log->io_list_lock);
			spin_unlock_irq(&log->io_list_lock);
		}
	}
}

int ppl_handle_flush_request(struct r5l_log *log, struct bio *bio)
{
	if (bio->bi_iter.bi_size == 0) {
		bio_endio(bio);
		return 0;
	}
	bio->bi_opf &= ~REQ_PREFLUSH;
	return -EAGAIN;
}

void ppl_stripe_write_finished(struct stripe_head *sh)
{
	struct ppl_io_unit *io;

	io = sh->ppl_io;
	sh->ppl_io = NULL;

	if (io && atomic_dec_and_test(&io->pending_stripes)) {
		if (io->log->disk_flush_bitmap)
			ppl_do_flush(io);
		else
			ppl_io_unit_finished(io);
	}
}

static void ppl_xor(int size, struct page *page1, struct page *page2)
{
	struct async_submit_ctl submit;
	struct dma_async_tx_descriptor *tx;
	struct page *xor_srcs[] = { page1, page2 };

	init_async_submit(&submit, ASYNC_TX_ACK|ASYNC_TX_XOR_DROP_DST,
			  NULL, NULL, NULL, NULL);
	tx = async_xor(page1, xor_srcs, 0, 2, size, &submit);

	async_tx_quiesce(&tx);
}

/*
 * PPL recovery strategy: xor partial parity and data from all modified data
 * disks within a stripe and write the result as the new stripe parity. If all
 * stripe data disks are modified (full stripe write), no partial parity is
 * available, so just xor the data disks.
 *
 * Recovery of a PPL entry shall occur only if all modified data disks are
 * available and read from all of them succeeds.
 *
 * A PPL entry applies to a stripe, partial parity size for an entry is at most
 * the size of the chunk. Examples of possible cases for a single entry:
 *
 * case 0: single data disk write:
 *   data0    data1    data2     ppl        parity
 * +--------+--------+--------+           +--------------------+
 * | ------ | ------ | ------ | +----+    | (no change)        |
 * | ------ | -data- | ------ | | pp | -> | data1 ^ pp         |
 * | ------ | -data- | ------ | | pp | -> | data1 ^ pp         |
 * | ------ | ------ | ------ | +----+    | (no change)        |
 * +--------+--------+--------+           +--------------------+
 * pp_size = data_size
 *
 * case 1: more than one data disk write:
 *   data0    data1    data2     ppl        parity
 * +--------+--------+--------+           +--------------------+
 * | ------ | ------ | ------ | +----+    | (no change)        |
 * | -data- | -data- | ------ | | pp | -> | data0 ^ data1 ^ pp |
 * | -data- | -data- | ------ | | pp | -> | data0 ^ data1 ^ pp |
 * | ------ | ------ | ------ | +----+    | (no change)        |
 * +--------+--------+--------+           +--------------------+
 * pp_size = data_size / modified_data_disks
 *
 * case 2: write to all data disks (also full stripe write):
 *   data0    data1    data2                parity
 * +--------+--------+--------+           +--------------------+
 * | ------ | ------ | ------ |           | (no change)        |
 * | -data- | -data- | -data- | --------> | xor all data       |
 * | ------ | ------ | ------ | --------> | (no change)        |
 * | ------ | ------ | ------ |           | (no change)        |
 * +--------+--------+--------+           +--------------------+
 * pp_size = 0
 *
 * The following cases are possible only in other implementations. The recovery
 * code can handle them, but they are not generated at runtime because they can
 * be reduced to cases 0, 1 and 2:
 *
 * case 3:
 *   data0    data1    data2     ppl        parity
 * +--------+--------+--------+ +----+    +--------------------+
 * | ------ | -data- | -data- | | pp |    | data1 ^ data2 ^ pp |
 * | ------ | -data- | -data- | | pp | -> | data1 ^ data2 ^ pp |
 * | -data- | -data- | -data- | | -- | -> | xor all data       |
 * | -data- | -data- | ------ | | pp |    | data0 ^ data1 ^ pp |
 * +--------+--------+--------+ +----+    +--------------------+
 * pp_size = chunk_size
 *
 * case 4:
 *   data0    data1    data2     ppl        parity
 * +--------+--------+--------+ +----+    +--------------------+
 * | ------ | -data- | ------ | | pp |    | data1 ^ pp         |
 * | ------ | ------ | ------ | | -- | -> | (no change)        |
 * | ------ | ------ | ------ | | -- | -> | (no change)        |
 * | -data- | ------ | ------ | | pp |    | data0 ^ pp         |
 * +--------+--------+--------+ +----+    +--------------------+
 * pp_size = chunk_size
 */
static int ppl_recover_entry(struct ppl_log *log, struct ppl_header_entry *e,
			     sector_t ppl_sector)
{
	struct ppl_conf *ppl_conf = log->ppl_conf;
	struct mddev *mddev = ppl_conf->mddev;
	struct r5conf *conf = mddev->private;
	int block_size = ppl_conf->block_size;
	struct page *page1;
	struct page *page2;
	sector_t r_sector_first;
	sector_t r_sector_last;
	int strip_sectors;
	int data_disks;
	int i;
	int ret = 0;
	char b[BDEVNAME_SIZE];
	unsigned int pp_size = le32_to_cpu(e->pp_size);
	unsigned int data_size = le32_to_cpu(e->data_size);

	page1 = alloc_page(GFP_KERNEL);
	page2 = alloc_page(GFP_KERNEL);

	if (!page1 || !page2) {
		ret = -ENOMEM;
		goto out;
	}

	r_sector_first = le64_to_cpu(e->data_sector) * (block_size >> 9);

	if ((pp_size >> 9) < conf->chunk_sectors) {
		if (pp_size > 0) {
			data_disks = data_size / pp_size;
			strip_sectors = pp_size >> 9;
		} else {
			data_disks = conf->raid_disks - conf->max_degraded;
			strip_sectors = (data_size >> 9) / data_disks;
		}
		r_sector_last = r_sector_first +
				(data_disks - 1) * conf->chunk_sectors +
				strip_sectors;
	} else {
		data_disks = conf->raid_disks - conf->max_degraded;
		strip_sectors = conf->chunk_sectors;
		r_sector_last = r_sector_first + (data_size >> 9);
	}

	pr_debug("%s: array sector first: %llu last: %llu\n", __func__,
		 (unsigned long long)r_sector_first,
		 (unsigned long long)r_sector_last);

	/* if start and end is 4k aligned, use a 4k block */
	if (block_size == 512 &&
	    (r_sector_first & (STRIPE_SECTORS - 1)) == 0 &&
	    (r_sector_last & (STRIPE_SECTORS - 1)) == 0)
		block_size = STRIPE_SIZE;

	/* iterate through blocks in strip */
	for (i = 0; i < strip_sectors; i += (block_size >> 9)) {
		bool update_parity = false;
		sector_t parity_sector;
		struct md_rdev *parity_rdev;
		struct stripe_head sh;
		int disk;
		int indent = 0;

		pr_debug("%s:%*s iter %d start\n", __func__, indent, "", i);
		indent += 2;

		memset(page_address(page1), 0, PAGE_SIZE);

		/* iterate through data member disks */
		for (disk = 0; disk < data_disks; disk++) {
			int dd_idx;
			struct md_rdev *rdev;
			sector_t sector;
			sector_t r_sector = r_sector_first + i +
					    (disk * conf->chunk_sectors);

			pr_debug("%s:%*s data member disk %d start\n",
				 __func__, indent, "", disk);
			indent += 2;

			if (r_sector >= r_sector_last) {
				pr_debug("%s:%*s array sector %llu doesn't need parity update\n",
					 __func__, indent, "",
					 (unsigned long long)r_sector);
				indent -= 2;
				continue;
			}

			update_parity = true;

			/* map raid sector to member disk */
			sector = raid5_compute_sector(conf, r_sector, 0,
						      &dd_idx, NULL);
			pr_debug("%s:%*s processing array sector %llu => data member disk %d, sector %llu\n",
				 __func__, indent, "",
				 (unsigned long long)r_sector, dd_idx,
				 (unsigned long long)sector);

			rdev = conf->disks[dd_idx].rdev;
			if (!rdev || (!test_bit(In_sync, &rdev->flags) &&
				      sector >= rdev->recovery_offset)) {
				pr_debug("%s:%*s data member disk %d missing\n",
					 __func__, indent, "", dd_idx);
				update_parity = false;
				break;
			}

			pr_debug("%s:%*s reading data member disk %s sector %llu\n",
				 __func__, indent, "", bdevname(rdev->bdev, b),
				 (unsigned long long)sector);
			if (!sync_page_io(rdev, sector, block_size, page2,
					REQ_OP_READ, 0, false)) {
				md_error(mddev, rdev);
				pr_debug("%s:%*s read failed!\n", __func__,
					 indent, "");
				ret = -EIO;
				goto out;
			}

			ppl_xor(block_size, page1, page2);

			indent -= 2;
		}

		if (!update_parity)
			continue;

		if (pp_size > 0) {
			pr_debug("%s:%*s reading pp disk sector %llu\n",
				 __func__, indent, "",
				 (unsigned long long)(ppl_sector + i));
			if (!sync_page_io(log->rdev,
					ppl_sector - log->rdev->data_offset + i,
					block_size, page2, REQ_OP_READ, 0,
					false)) {
				pr_debug("%s:%*s read failed!\n", __func__,
					 indent, "");
				md_error(mddev, log->rdev);
				ret = -EIO;
				goto out;
			}

			ppl_xor(block_size, page1, page2);
		}

		/* map raid sector to parity disk */
		parity_sector = raid5_compute_sector(conf, r_sector_first + i,
				0, &disk, &sh);
		BUG_ON(sh.pd_idx != le32_to_cpu(e->parity_disk));
		parity_rdev = conf->disks[sh.pd_idx].rdev;

		BUG_ON(parity_rdev->bdev->bd_dev != log->rdev->bdev->bd_dev);
		pr_debug("%s:%*s write parity at sector %llu, disk %s\n",
			 __func__, indent, "",
			 (unsigned long long)parity_sector,
			 bdevname(parity_rdev->bdev, b));
		if (!sync_page_io(parity_rdev, parity_sector, block_size,
				page1, REQ_OP_WRITE, 0, false)) {
			pr_debug("%s:%*s parity write error!\n", __func__,
				 indent, "");
			md_error(mddev, parity_rdev);
			ret = -EIO;
			goto out;
		}
	}
out:
	if (page1)
		__free_page(page1);
	if (page2)
		__free_page(page2);
	return ret;
}

static int ppl_recover(struct ppl_log *log, struct ppl_header *pplhdr,
		       sector_t offset)
{
	struct ppl_conf *ppl_conf = log->ppl_conf;
	struct md_rdev *rdev = log->rdev;
	struct mddev *mddev = rdev->mddev;
	sector_t ppl_sector = rdev->ppl.sector + offset +
			      (PPL_HEADER_SIZE >> 9);
	struct page *page;
	int i;
	int ret = 0;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	/* iterate through all PPL entries saved */
	for (i = 0; i < le32_to_cpu(pplhdr->entries_count); i++) {
		struct ppl_header_entry *e = &pplhdr->entries[i];
		u32 pp_size = le32_to_cpu(e->pp_size);
		sector_t sector = ppl_sector;
		int ppl_entry_sectors = pp_size >> 9;
		u32 crc, crc_stored;

		pr_debug("%s: disk: %d entry: %d ppl_sector: %llu pp_size: %u\n",
			 __func__, rdev->raid_disk, i,
			 (unsigned long long)ppl_sector, pp_size);

		crc = ~0;
		crc_stored = le32_to_cpu(e->checksum);

		/* read parial parity for this entry and calculate its checksum */
		while (pp_size) {
			int s = pp_size > PAGE_SIZE ? PAGE_SIZE : pp_size;

			if (!sync_page_io(rdev, sector - rdev->data_offset,
					s, page, REQ_OP_READ, 0, false)) {
				md_error(mddev, rdev);
				ret = -EIO;
				goto out;
			}

			crc = crc32c_le(crc, page_address(page), s);

			pp_size -= s;
			sector += s >> 9;
		}

		crc = ~crc;

		if (crc != crc_stored) {
			/*
			 * Don't recover this entry if the checksum does not
			 * match, but keep going and try to recover other
			 * entries.
			 */
			pr_debug("%s: ppl entry crc does not match: stored: 0x%x calculated: 0x%x\n",
				 __func__, crc_stored, crc);
			ppl_conf->mismatch_count++;
		} else {
			ret = ppl_recover_entry(log, e, ppl_sector);
			if (ret)
				goto out;
			ppl_conf->recovered_entries++;
		}

		ppl_sector += ppl_entry_sectors;
	}

	/* flush the disk cache after recovery if necessary */
	ret = blkdev_issue_flush(rdev->bdev, GFP_KERNEL, NULL);
out:
	__free_page(page);
	return ret;
}

static int ppl_write_empty_header(struct ppl_log *log)
{
	struct page *page;
	struct ppl_header *pplhdr;
	struct md_rdev *rdev = log->rdev;
	int ret = 0;

	pr_debug("%s: disk: %d ppl_sector: %llu\n", __func__,
		 rdev->raid_disk, (unsigned long long)rdev->ppl.sector);

	page = alloc_page(GFP_NOIO | __GFP_ZERO);
	if (!page)
		return -ENOMEM;

	pplhdr = page_address(page);
	/* zero out PPL space to avoid collision with old PPLs */
	blkdev_issue_zeroout(rdev->bdev, rdev->ppl.sector,
			    log->rdev->ppl.size, GFP_NOIO, 0);
	memset(pplhdr->reserved, 0xff, PPL_HDR_RESERVED);
	pplhdr->signature = cpu_to_le32(log->ppl_conf->signature);
	pplhdr->checksum = cpu_to_le32(~crc32c_le(~0, pplhdr, PAGE_SIZE));

	if (!sync_page_io(rdev, rdev->ppl.sector - rdev->data_offset,
			  PPL_HEADER_SIZE, page, REQ_OP_WRITE | REQ_SYNC |
			  REQ_FUA, 0, false)) {
		md_error(rdev->mddev, rdev);
		ret = -EIO;
	}

	__free_page(page);
	return ret;
}

static int ppl_load_distributed(struct ppl_log *log)
{
	struct ppl_conf *ppl_conf = log->ppl_conf;
	struct md_rdev *rdev = log->rdev;
	struct mddev *mddev = rdev->mddev;
	struct page *page, *page2, *tmp;
	struct ppl_header *pplhdr = NULL, *prev_pplhdr = NULL;
	u32 crc, crc_stored;
	u32 signature;
	int ret = 0, i;
	sector_t pplhdr_offset = 0, prev_pplhdr_offset = 0;

	pr_debug("%s: disk: %d\n", __func__, rdev->raid_disk);
	/* read PPL headers, find the recent one */
	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	page2 = alloc_page(GFP_KERNEL);
	if (!page2) {
		__free_page(page);
		return -ENOMEM;
	}

	/* searching ppl area for latest ppl */
	while (pplhdr_offset < rdev->ppl.size - (PPL_HEADER_SIZE >> 9)) {
		if (!sync_page_io(rdev,
				  rdev->ppl.sector - rdev->data_offset +
				  pplhdr_offset, PAGE_SIZE, page, REQ_OP_READ,
				  0, false)) {
			md_error(mddev, rdev);
			ret = -EIO;
			/* if not able to read - don't recover any PPL */
			pplhdr = NULL;
			break;
		}
		pplhdr = page_address(page);

		/* check header validity */
		crc_stored = le32_to_cpu(pplhdr->checksum);
		pplhdr->checksum = 0;
		crc = ~crc32c_le(~0, pplhdr, PAGE_SIZE);

		if (crc_stored != crc) {
			pr_debug("%s: ppl header crc does not match: stored: 0x%x calculated: 0x%x (offset: %llu)\n",
				 __func__, crc_stored, crc,
				 (unsigned long long)pplhdr_offset);
			pplhdr = prev_pplhdr;
			pplhdr_offset = prev_pplhdr_offset;
			break;
		}

		signature = le32_to_cpu(pplhdr->signature);

		if (mddev->external) {
			/*
			 * For external metadata the header signature is set and
			 * validated in userspace.
			 */
			ppl_conf->signature = signature;
		} else if (ppl_conf->signature != signature) {
			pr_debug("%s: ppl header signature does not match: stored: 0x%x configured: 0x%x (offset: %llu)\n",
				 __func__, signature, ppl_conf->signature,
				 (unsigned long long)pplhdr_offset);
			pplhdr = prev_pplhdr;
			pplhdr_offset = prev_pplhdr_offset;
			break;
		}

		if (prev_pplhdr && le64_to_cpu(prev_pplhdr->generation) >
		    le64_to_cpu(pplhdr->generation)) {
			/* previous was newest */
			pplhdr = prev_pplhdr;
			pplhdr_offset = prev_pplhdr_offset;
			break;
		}

		prev_pplhdr_offset = pplhdr_offset;
		prev_pplhdr = pplhdr;

		tmp = page;
		page = page2;
		page2 = tmp;

		/* calculate next potential ppl offset */
		for (i = 0; i < le32_to_cpu(pplhdr->entries_count); i++)
			pplhdr_offset +=
			    le32_to_cpu(pplhdr->entries[i].pp_size) >> 9;
		pplhdr_offset += PPL_HEADER_SIZE >> 9;
	}

	/* no valid ppl found */
	if (!pplhdr)
		ppl_conf->mismatch_count++;
	else
		pr_debug("%s: latest PPL found at offset: %llu, with generation: %llu\n",
		    __func__, (unsigned long long)pplhdr_offset,
		    le64_to_cpu(pplhdr->generation));

	/* attempt to recover from log if we are starting a dirty array */
	if (pplhdr && !mddev->pers && mddev->recovery_cp != MaxSector)
		ret = ppl_recover(log, pplhdr, pplhdr_offset);

	/* write empty header if we are starting the array */
	if (!ret && !mddev->pers)
		ret = ppl_write_empty_header(log);

	__free_page(page);
	__free_page(page2);

	pr_debug("%s: return: %d mismatch_count: %d recovered_entries: %d\n",
		 __func__, ret, ppl_conf->mismatch_count,
		 ppl_conf->recovered_entries);
	return ret;
}

static int ppl_load(struct ppl_conf *ppl_conf)
{
	int ret = 0;
	u32 signature = 0;
	bool signature_set = false;
	int i;

	for (i = 0; i < ppl_conf->count; i++) {
		struct ppl_log *log = &ppl_conf->child_logs[i];

		/* skip missing drive */
		if (!log->rdev)
			continue;

		ret = ppl_load_distributed(log);
		if (ret)
			break;

		/*
		 * For external metadata we can't check if the signature is
		 * correct on a single drive, but we can check if it is the same
		 * on all drives.
		 */
		if (ppl_conf->mddev->external) {
			if (!signature_set) {
				signature = ppl_conf->signature;
				signature_set = true;
			} else if (signature != ppl_conf->signature) {
				pr_warn("md/raid:%s: PPL header signature does not match on all member drives\n",
					mdname(ppl_conf->mddev));
				ret = -EINVAL;
				break;
			}
		}
	}

	pr_debug("%s: return: %d mismatch_count: %d recovered_entries: %d\n",
		 __func__, ret, ppl_conf->mismatch_count,
		 ppl_conf->recovered_entries);
	return ret;
}

static void __ppl_exit_log(struct ppl_conf *ppl_conf)
{
	clear_bit(MD_HAS_PPL, &ppl_conf->mddev->flags);
	clear_bit(MD_HAS_MULTIPLE_PPLS, &ppl_conf->mddev->flags);

	kfree(ppl_conf->child_logs);

	bioset_exit(&ppl_conf->bs);
	bioset_exit(&ppl_conf->flush_bs);
	mempool_exit(&ppl_conf->io_pool);
	kmem_cache_destroy(ppl_conf->io_kc);

	kfree(ppl_conf);
}

void ppl_exit_log(struct r5conf *conf)
{
	struct ppl_conf *ppl_conf = conf->log_private;

	if (ppl_conf) {
		__ppl_exit_log(ppl_conf);
		conf->log_private = NULL;
	}
}

static int ppl_validate_rdev(struct md_rdev *rdev)
{
	char b[BDEVNAME_SIZE];
	int ppl_data_sectors;
	int ppl_size_new;

	/*
	 * The configured PPL size must be enough to store
	 * the header and (at the very least) partial parity
	 * for one stripe. Round it down to ensure the data
	 * space is cleanly divisible by stripe size.
	 */
	ppl_data_sectors = rdev->ppl.size - (PPL_HEADER_SIZE >> 9);

	if (ppl_data_sectors > 0)
		ppl_data_sectors = rounddown(ppl_data_sectors, STRIPE_SECTORS);

	if (ppl_data_sectors <= 0) {
		pr_warn("md/raid:%s: PPL space too small on %s\n",
			mdname(rdev->mddev), bdevname(rdev->bdev, b));
		return -ENOSPC;
	}

	ppl_size_new = ppl_data_sectors + (PPL_HEADER_SIZE >> 9);

	if ((rdev->ppl.sector < rdev->data_offset &&
	     rdev->ppl.sector + ppl_size_new > rdev->data_offset) ||
	    (rdev->ppl.sector >= rdev->data_offset &&
	     rdev->data_offset + rdev->sectors > rdev->ppl.sector)) {
		pr_warn("md/raid:%s: PPL space overlaps with data on %s\n",
			mdname(rdev->mddev), bdevname(rdev->bdev, b));
		return -EINVAL;
	}

	if (!rdev->mddev->external &&
	    ((rdev->ppl.offset > 0 && rdev->ppl.offset < (rdev->sb_size >> 9)) ||
	     (rdev->ppl.offset <= 0 && rdev->ppl.offset + ppl_size_new > 0))) {
		pr_warn("md/raid:%s: PPL space overlaps with superblock on %s\n",
			mdname(rdev->mddev), bdevname(rdev->bdev, b));
		return -EINVAL;
	}

	rdev->ppl.size = ppl_size_new;

	return 0;
}

static void ppl_init_child_log(struct ppl_log *log, struct md_rdev *rdev)
{
	struct request_queue *q;

	if ((rdev->ppl.size << 9) >= (PPL_SPACE_SIZE +
				      PPL_HEADER_SIZE) * 2) {
		log->use_multippl = true;
		set_bit(MD_HAS_MULTIPLE_PPLS,
			&log->ppl_conf->mddev->flags);
		log->entry_space = PPL_SPACE_SIZE;
	} else {
		log->use_multippl = false;
		log->entry_space = (log->rdev->ppl.size << 9) -
				   PPL_HEADER_SIZE;
	}
	log->next_io_sector = rdev->ppl.sector;

	q = bdev_get_queue(rdev->bdev);
	if (test_bit(QUEUE_FLAG_WC, &q->queue_flags))
		log->wb_cache_on = true;
}

int ppl_init_log(struct r5conf *conf)
{
	struct ppl_conf *ppl_conf;
	struct mddev *mddev = conf->mddev;
	int ret = 0;
	int max_disks;
	int i;

	pr_debug("md/raid:%s: enabling distributed Partial Parity Log\n",
		 mdname(conf->mddev));

	if (PAGE_SIZE != 4096)
		return -EINVAL;

	if (mddev->level != 5) {
		pr_warn("md/raid:%s PPL is not compatible with raid level %d\n",
			mdname(mddev), mddev->level);
		return -EINVAL;
	}

	if (mddev->bitmap_info.file || mddev->bitmap_info.offset) {
		pr_warn("md/raid:%s PPL is not compatible with bitmap\n",
			mdname(mddev));
		return -EINVAL;
	}

	if (test_bit(MD_HAS_JOURNAL, &mddev->flags)) {
		pr_warn("md/raid:%s PPL is not compatible with journal\n",
			mdname(mddev));
		return -EINVAL;
	}

	max_disks = sizeof_field(struct ppl_log, disk_flush_bitmap) *
		BITS_PER_BYTE;
	if (conf->raid_disks > max_disks) {
		pr_warn("md/raid:%s PPL doesn't support over %d disks in the array\n",
			mdname(mddev), max_disks);
		return -EINVAL;
	}

	ppl_conf = kzalloc(sizeof(struct ppl_conf), GFP_KERNEL);
	if (!ppl_conf)
		return -ENOMEM;

	ppl_conf->mddev = mddev;

	ppl_conf->io_kc = KMEM_CACHE(ppl_io_unit, 0);
	if (!ppl_conf->io_kc) {
		ret = -ENOMEM;
		goto err;
	}

	ret = mempool_init(&ppl_conf->io_pool, conf->raid_disks, ppl_io_pool_alloc,
			   ppl_io_pool_free, ppl_conf->io_kc);
	if (ret)
		goto err;

	ret = bioset_init(&ppl_conf->bs, conf->raid_disks, 0, BIOSET_NEED_BVECS);
	if (ret)
		goto err;

	ret = bioset_init(&ppl_conf->flush_bs, conf->raid_disks, 0, 0);
	if (ret)
		goto err;

	ppl_conf->count = conf->raid_disks;
	ppl_conf->child_logs = kcalloc(ppl_conf->count, sizeof(struct ppl_log),
				       GFP_KERNEL);
	if (!ppl_conf->child_logs) {
		ret = -ENOMEM;
		goto err;
	}

	atomic64_set(&ppl_conf->seq, 0);
	INIT_LIST_HEAD(&ppl_conf->no_mem_stripes);
	spin_lock_init(&ppl_conf->no_mem_stripes_lock);
	ppl_conf->write_hint = RWH_WRITE_LIFE_NOT_SET;

	if (!mddev->external) {
		ppl_conf->signature = ~crc32c_le(~0, mddev->uuid, sizeof(mddev->uuid));
		ppl_conf->block_size = 512;
	} else {
		ppl_conf->block_size = queue_logical_block_size(mddev->queue);
	}

	for (i = 0; i < ppl_conf->count; i++) {
		struct ppl_log *log = &ppl_conf->child_logs[i];
		struct md_rdev *rdev = conf->disks[i].rdev;

		mutex_init(&log->io_mutex);
		spin_lock_init(&log->io_list_lock);
		INIT_LIST_HEAD(&log->io_list);

		log->ppl_conf = ppl_conf;
		log->rdev = rdev;

		if (rdev) {
			ret = ppl_validate_rdev(rdev);
			if (ret)
				goto err;

			ppl_init_child_log(log, rdev);
		}
	}

	/* load and possibly recover the logs from the member disks */
	ret = ppl_load(ppl_conf);

	if (ret) {
		goto err;
	} else if (!mddev->pers && mddev->recovery_cp == 0 &&
		   ppl_conf->recovered_entries > 0 &&
		   ppl_conf->mismatch_count == 0) {
		/*
		 * If we are starting a dirty array and the recovery succeeds
		 * without any issues, set the array as clean.
		 */
		mddev->recovery_cp = MaxSector;
		set_bit(MD_SB_CHANGE_CLEAN, &mddev->sb_flags);
	} else if (mddev->pers && ppl_conf->mismatch_count > 0) {
		/* no mismatch allowed when enabling PPL for a running array */
		ret = -EINVAL;
		goto err;
	}

	conf->log_private = ppl_conf;
	set_bit(MD_HAS_PPL, &ppl_conf->mddev->flags);

	return 0;
err:
	__ppl_exit_log(ppl_conf);
	return ret;
}

int ppl_modify_log(struct r5conf *conf, struct md_rdev *rdev, bool add)
{
	struct ppl_conf *ppl_conf = conf->log_private;
	struct ppl_log *log;
	int ret = 0;
	char b[BDEVNAME_SIZE];

	if (!rdev)
		return -EINVAL;

	pr_debug("%s: disk: %d operation: %s dev: %s\n",
		 __func__, rdev->raid_disk, add ? "add" : "remove",
		 bdevname(rdev->bdev, b));

	if (rdev->raid_disk < 0)
		return 0;

	if (rdev->raid_disk >= ppl_conf->count)
		return -ENODEV;

	log = &ppl_conf->child_logs[rdev->raid_disk];

	mutex_lock(&log->io_mutex);
	if (add) {
		ret = ppl_validate_rdev(rdev);
		if (!ret) {
			log->rdev = rdev;
			ret = ppl_write_empty_header(log);
			ppl_init_child_log(log, rdev);
		}
	} else {
		log->rdev = NULL;
	}
	mutex_unlock(&log->io_mutex);

	return ret;
}

static ssize_t
ppl_write_hint_show(struct mddev *mddev, char *buf)
{
	size_t ret = 0;
	struct r5conf *conf;
	struct ppl_conf *ppl_conf = NULL;

	spin_lock(&mddev->lock);
	conf = mddev->private;
	if (conf && raid5_has_ppl(conf))
		ppl_conf = conf->log_private;
	ret = sprintf(buf, "%d\n", ppl_conf ? ppl_conf->write_hint : 0);
	spin_unlock(&mddev->lock);

	return ret;
}

static ssize_t
ppl_write_hint_store(struct mddev *mddev, const char *page, size_t len)
{
	struct r5conf *conf;
	struct ppl_conf *ppl_conf;
	int err = 0;
	unsigned short new;

	if (len >= PAGE_SIZE)
		return -EINVAL;
	if (kstrtou16(page, 10, &new))
		return -EINVAL;

	err = mddev_lock(mddev);
	if (err)
		return err;

	conf = mddev->private;
	if (!conf) {
		err = -ENODEV;
	} else if (raid5_has_ppl(conf)) {
		ppl_conf = conf->log_private;
		if (!ppl_conf)
			err = -EINVAL;
		else
			ppl_conf->write_hint = new;
	} else {
		err = -EINVAL;
	}

	mddev_unlock(mddev);

	return err ?: len;
}

struct md_sysfs_entry
ppl_write_hint = __ATTR(ppl_write_hint, S_IRUGO | S_IWUSR,
			ppl_write_hint_show,
			ppl_write_hint_store);
