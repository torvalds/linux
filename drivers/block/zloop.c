// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Christoph Hellwig.
 * Copyright (c) 2025, Western Digital Corporation or its affiliates.
 *
 * Zoned Loop Device driver - exports a zoned block device using one file per
 * zone as backing storage.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/blk-mq.h>
#include <linux/blkzoned.h>
#include <linux/pagemap.h>
#include <linux/miscdevice.h>
#include <linux/falloc.h>
#include <linux/mutex.h>
#include <linux/parser.h>
#include <linux/seq_file.h>

/*
 * Options for adding (and removing) a device.
 */
enum {
	ZLOOP_OPT_ERR			= 0,
	ZLOOP_OPT_ID			= (1 << 0),
	ZLOOP_OPT_CAPACITY		= (1 << 1),
	ZLOOP_OPT_ZONE_SIZE		= (1 << 2),
	ZLOOP_OPT_ZONE_CAPACITY		= (1 << 3),
	ZLOOP_OPT_NR_CONV_ZONES		= (1 << 4),
	ZLOOP_OPT_BASE_DIR		= (1 << 5),
	ZLOOP_OPT_NR_QUEUES		= (1 << 6),
	ZLOOP_OPT_QUEUE_DEPTH		= (1 << 7),
	ZLOOP_OPT_BUFFERED_IO		= (1 << 8),
};

static const match_table_t zloop_opt_tokens = {
	{ ZLOOP_OPT_ID,			"id=%d"	},
	{ ZLOOP_OPT_CAPACITY,		"capacity_mb=%u"	},
	{ ZLOOP_OPT_ZONE_SIZE,		"zone_size_mb=%u"	},
	{ ZLOOP_OPT_ZONE_CAPACITY,	"zone_capacity_mb=%u"	},
	{ ZLOOP_OPT_NR_CONV_ZONES,	"conv_zones=%u"		},
	{ ZLOOP_OPT_BASE_DIR,		"base_dir=%s"		},
	{ ZLOOP_OPT_NR_QUEUES,		"nr_queues=%u"		},
	{ ZLOOP_OPT_QUEUE_DEPTH,	"queue_depth=%u"	},
	{ ZLOOP_OPT_BUFFERED_IO,	"buffered_io"		},
	{ ZLOOP_OPT_ERR,		NULL			}
};

/* Default values for the "add" operation. */
#define ZLOOP_DEF_ID			-1
#define ZLOOP_DEF_ZONE_SIZE		((256ULL * SZ_1M) >> SECTOR_SHIFT)
#define ZLOOP_DEF_NR_ZONES		64
#define ZLOOP_DEF_NR_CONV_ZONES		8
#define ZLOOP_DEF_BASE_DIR		"/var/local/zloop"
#define ZLOOP_DEF_NR_QUEUES		1
#define ZLOOP_DEF_QUEUE_DEPTH		128
#define ZLOOP_DEF_BUFFERED_IO		false

/* Arbitrary limit on the zone size (16GB). */
#define ZLOOP_MAX_ZONE_SIZE_MB		16384

struct zloop_options {
	unsigned int		mask;
	int			id;
	sector_t		capacity;
	sector_t		zone_size;
	sector_t		zone_capacity;
	unsigned int		nr_conv_zones;
	char			*base_dir;
	unsigned int		nr_queues;
	unsigned int		queue_depth;
	bool			buffered_io;
};

/*
 * Device states.
 */
enum {
	Zlo_creating = 0,
	Zlo_live,
	Zlo_deleting,
};

enum zloop_zone_flags {
	ZLOOP_ZONE_CONV = 0,
	ZLOOP_ZONE_SEQ_ERROR,
};

struct zloop_zone {
	struct file		*file;

	unsigned long		flags;
	struct mutex		lock;
	enum blk_zone_cond	cond;
	sector_t		start;
	sector_t		wp;

	gfp_t			old_gfp_mask;
};

struct zloop_device {
	unsigned int		id;
	unsigned int		state;

	struct blk_mq_tag_set	tag_set;
	struct gendisk		*disk;

	struct workqueue_struct *workqueue;
	bool			buffered_io;

	const char		*base_dir;
	struct file		*data_dir;

	unsigned int		zone_shift;
	sector_t		zone_size;
	sector_t		zone_capacity;
	unsigned int		nr_zones;
	unsigned int		nr_conv_zones;
	unsigned int		block_size;

	struct zloop_zone	zones[] __counted_by(nr_zones);
};

struct zloop_cmd {
	struct work_struct	work;
	atomic_t		ref;
	sector_t		sector;
	sector_t		nr_sectors;
	long			ret;
	struct kiocb		iocb;
	struct bio_vec		*bvec;
};

static DEFINE_IDR(zloop_index_idr);
static DEFINE_MUTEX(zloop_ctl_mutex);

static unsigned int rq_zone_no(struct request *rq)
{
	struct zloop_device *zlo = rq->q->queuedata;

	return blk_rq_pos(rq) >> zlo->zone_shift;
}

static int zloop_update_seq_zone(struct zloop_device *zlo, unsigned int zone_no)
{
	struct zloop_zone *zone = &zlo->zones[zone_no];
	struct kstat stat;
	sector_t file_sectors;
	int ret;

	lockdep_assert_held(&zone->lock);

	ret = vfs_getattr(&zone->file->f_path, &stat, STATX_SIZE, 0);
	if (ret < 0) {
		pr_err("Failed to get zone %u file stat (err=%d)\n",
		       zone_no, ret);
		set_bit(ZLOOP_ZONE_SEQ_ERROR, &zone->flags);
		return ret;
	}

	file_sectors = stat.size >> SECTOR_SHIFT;
	if (file_sectors > zlo->zone_capacity) {
		pr_err("Zone %u file too large (%llu sectors > %llu)\n",
		       zone_no, file_sectors, zlo->zone_capacity);
		return -EINVAL;
	}

	if (file_sectors & ((zlo->block_size >> SECTOR_SHIFT) - 1)) {
		pr_err("Zone %u file size not aligned to block size %u\n",
		       zone_no, zlo->block_size);
		return -EINVAL;
	}

	if (!file_sectors) {
		zone->cond = BLK_ZONE_COND_EMPTY;
		zone->wp = zone->start;
	} else if (file_sectors == zlo->zone_capacity) {
		zone->cond = BLK_ZONE_COND_FULL;
		zone->wp = zone->start + zlo->zone_size;
	} else {
		zone->cond = BLK_ZONE_COND_CLOSED;
		zone->wp = zone->start + file_sectors;
	}

	return 0;
}

static int zloop_open_zone(struct zloop_device *zlo, unsigned int zone_no)
{
	struct zloop_zone *zone = &zlo->zones[zone_no];
	int ret = 0;

	if (test_bit(ZLOOP_ZONE_CONV, &zone->flags))
		return -EIO;

	mutex_lock(&zone->lock);

	if (test_and_clear_bit(ZLOOP_ZONE_SEQ_ERROR, &zone->flags)) {
		ret = zloop_update_seq_zone(zlo, zone_no);
		if (ret)
			goto unlock;
	}

	switch (zone->cond) {
	case BLK_ZONE_COND_EXP_OPEN:
		break;
	case BLK_ZONE_COND_EMPTY:
	case BLK_ZONE_COND_CLOSED:
	case BLK_ZONE_COND_IMP_OPEN:
		zone->cond = BLK_ZONE_COND_EXP_OPEN;
		break;
	case BLK_ZONE_COND_FULL:
	default:
		ret = -EIO;
		break;
	}

unlock:
	mutex_unlock(&zone->lock);

	return ret;
}

static int zloop_close_zone(struct zloop_device *zlo, unsigned int zone_no)
{
	struct zloop_zone *zone = &zlo->zones[zone_no];
	int ret = 0;

	if (test_bit(ZLOOP_ZONE_CONV, &zone->flags))
		return -EIO;

	mutex_lock(&zone->lock);

	if (test_and_clear_bit(ZLOOP_ZONE_SEQ_ERROR, &zone->flags)) {
		ret = zloop_update_seq_zone(zlo, zone_no);
		if (ret)
			goto unlock;
	}

	switch (zone->cond) {
	case BLK_ZONE_COND_CLOSED:
		break;
	case BLK_ZONE_COND_IMP_OPEN:
	case BLK_ZONE_COND_EXP_OPEN:
		if (zone->wp == zone->start)
			zone->cond = BLK_ZONE_COND_EMPTY;
		else
			zone->cond = BLK_ZONE_COND_CLOSED;
		break;
	case BLK_ZONE_COND_EMPTY:
	case BLK_ZONE_COND_FULL:
	default:
		ret = -EIO;
		break;
	}

unlock:
	mutex_unlock(&zone->lock);

	return ret;
}

static int zloop_reset_zone(struct zloop_device *zlo, unsigned int zone_no)
{
	struct zloop_zone *zone = &zlo->zones[zone_no];
	int ret = 0;

	if (test_bit(ZLOOP_ZONE_CONV, &zone->flags))
		return -EIO;

	mutex_lock(&zone->lock);

	if (!test_bit(ZLOOP_ZONE_SEQ_ERROR, &zone->flags) &&
	    zone->cond == BLK_ZONE_COND_EMPTY)
		goto unlock;

	if (vfs_truncate(&zone->file->f_path, 0)) {
		set_bit(ZLOOP_ZONE_SEQ_ERROR, &zone->flags);
		ret = -EIO;
		goto unlock;
	}

	zone->cond = BLK_ZONE_COND_EMPTY;
	zone->wp = zone->start;
	clear_bit(ZLOOP_ZONE_SEQ_ERROR, &zone->flags);

unlock:
	mutex_unlock(&zone->lock);

	return ret;
}

static int zloop_reset_all_zones(struct zloop_device *zlo)
{
	unsigned int i;
	int ret;

	for (i = zlo->nr_conv_zones; i < zlo->nr_zones; i++) {
		ret = zloop_reset_zone(zlo, i);
		if (ret)
			return ret;
	}

	return 0;
}

static int zloop_finish_zone(struct zloop_device *zlo, unsigned int zone_no)
{
	struct zloop_zone *zone = &zlo->zones[zone_no];
	int ret = 0;

	if (test_bit(ZLOOP_ZONE_CONV, &zone->flags))
		return -EIO;

	mutex_lock(&zone->lock);

	if (!test_bit(ZLOOP_ZONE_SEQ_ERROR, &zone->flags) &&
	    zone->cond == BLK_ZONE_COND_FULL)
		goto unlock;

	if (vfs_truncate(&zone->file->f_path, zlo->zone_size << SECTOR_SHIFT)) {
		set_bit(ZLOOP_ZONE_SEQ_ERROR, &zone->flags);
		ret = -EIO;
		goto unlock;
	}

	zone->cond = BLK_ZONE_COND_FULL;
	zone->wp = zone->start + zlo->zone_size;
	clear_bit(ZLOOP_ZONE_SEQ_ERROR, &zone->flags);

 unlock:
	mutex_unlock(&zone->lock);

	return ret;
}

static void zloop_put_cmd(struct zloop_cmd *cmd)
{
	struct request *rq = blk_mq_rq_from_pdu(cmd);

	if (!atomic_dec_and_test(&cmd->ref))
		return;
	kfree(cmd->bvec);
	cmd->bvec = NULL;
	if (likely(!blk_should_fake_timeout(rq->q)))
		blk_mq_complete_request(rq);
}

static void zloop_rw_complete(struct kiocb *iocb, long ret)
{
	struct zloop_cmd *cmd = container_of(iocb, struct zloop_cmd, iocb);

	cmd->ret = ret;
	zloop_put_cmd(cmd);
}

static void zloop_rw(struct zloop_cmd *cmd)
{
	struct request *rq = blk_mq_rq_from_pdu(cmd);
	struct zloop_device *zlo = rq->q->queuedata;
	unsigned int zone_no = rq_zone_no(rq);
	sector_t sector = blk_rq_pos(rq);
	sector_t nr_sectors = blk_rq_sectors(rq);
	bool is_append = req_op(rq) == REQ_OP_ZONE_APPEND;
	bool is_write = req_op(rq) == REQ_OP_WRITE || is_append;
	int rw = is_write ? ITER_SOURCE : ITER_DEST;
	struct req_iterator rq_iter;
	struct zloop_zone *zone;
	struct iov_iter iter;
	struct bio_vec tmp;
	sector_t zone_end;
	int nr_bvec = 0;
	int ret;

	atomic_set(&cmd->ref, 2);
	cmd->sector = sector;
	cmd->nr_sectors = nr_sectors;
	cmd->ret = 0;

	/* We should never get an I/O beyond the device capacity. */
	if (WARN_ON_ONCE(zone_no >= zlo->nr_zones)) {
		ret = -EIO;
		goto out;
	}
	zone = &zlo->zones[zone_no];
	zone_end = zone->start + zlo->zone_capacity;

	/*
	 * The block layer should never send requests that are not fully
	 * contained within the zone.
	 */
	if (WARN_ON_ONCE(sector + nr_sectors > zone->start + zlo->zone_size)) {
		ret = -EIO;
		goto out;
	}

	if (test_and_clear_bit(ZLOOP_ZONE_SEQ_ERROR, &zone->flags)) {
		mutex_lock(&zone->lock);
		ret = zloop_update_seq_zone(zlo, zone_no);
		mutex_unlock(&zone->lock);
		if (ret)
			goto out;
	}

	if (!test_bit(ZLOOP_ZONE_CONV, &zone->flags) && is_write) {
		mutex_lock(&zone->lock);

		if (is_append) {
			sector = zone->wp;
			cmd->sector = sector;
		}

		/*
		 * Write operations must be aligned to the write pointer and
		 * fully contained within the zone capacity.
		 */
		if (sector != zone->wp || zone->wp + nr_sectors > zone_end) {
			pr_err("Zone %u: unaligned write: sect %llu, wp %llu\n",
			       zone_no, sector, zone->wp);
			ret = -EIO;
			goto unlock;
		}

		/* Implicitly open the target zone. */
		if (zone->cond == BLK_ZONE_COND_CLOSED ||
		    zone->cond == BLK_ZONE_COND_EMPTY)
			zone->cond = BLK_ZONE_COND_IMP_OPEN;

		/*
		 * Advance the write pointer of sequential zones. If the write
		 * fails, the wp position will be corrected when the next I/O
		 * copmpletes.
		 */
		zone->wp += nr_sectors;
		if (zone->wp == zone_end)
			zone->cond = BLK_ZONE_COND_FULL;
	}

	rq_for_each_bvec(tmp, rq, rq_iter)
		nr_bvec++;

	if (rq->bio != rq->biotail) {
		struct bio_vec *bvec;

		cmd->bvec = kmalloc_array(nr_bvec, sizeof(*cmd->bvec), GFP_NOIO);
		if (!cmd->bvec) {
			ret = -EIO;
			goto unlock;
		}

		/*
		 * The bios of the request may be started from the middle of
		 * the 'bvec' because of bio splitting, so we can't directly
		 * copy bio->bi_iov_vec to new bvec. The rq_for_each_bvec
		 * API will take care of all details for us.
		 */
		bvec = cmd->bvec;
		rq_for_each_bvec(tmp, rq, rq_iter) {
			*bvec = tmp;
			bvec++;
		}
		iov_iter_bvec(&iter, rw, cmd->bvec, nr_bvec, blk_rq_bytes(rq));
	} else {
		/*
		 * Same here, this bio may be started from the middle of the
		 * 'bvec' because of bio splitting, so offset from the bvec
		 * must be passed to iov iterator
		 */
		iov_iter_bvec(&iter, rw,
			__bvec_iter_bvec(rq->bio->bi_io_vec, rq->bio->bi_iter),
					nr_bvec, blk_rq_bytes(rq));
		iter.iov_offset = rq->bio->bi_iter.bi_bvec_done;
	}

	cmd->iocb.ki_pos = (sector - zone->start) << SECTOR_SHIFT;
	cmd->iocb.ki_filp = zone->file;
	cmd->iocb.ki_complete = zloop_rw_complete;
	if (!zlo->buffered_io)
		cmd->iocb.ki_flags = IOCB_DIRECT;
	cmd->iocb.ki_ioprio = IOPRIO_PRIO_VALUE(IOPRIO_CLASS_NONE, 0);

	if (rw == ITER_SOURCE)
		ret = zone->file->f_op->write_iter(&cmd->iocb, &iter);
	else
		ret = zone->file->f_op->read_iter(&cmd->iocb, &iter);
unlock:
	if (!test_bit(ZLOOP_ZONE_CONV, &zone->flags) && is_write)
		mutex_unlock(&zone->lock);
out:
	if (ret != -EIOCBQUEUED)
		zloop_rw_complete(&cmd->iocb, ret);
	zloop_put_cmd(cmd);
}

static void zloop_handle_cmd(struct zloop_cmd *cmd)
{
	struct request *rq = blk_mq_rq_from_pdu(cmd);
	struct zloop_device *zlo = rq->q->queuedata;

	switch (req_op(rq)) {
	case REQ_OP_READ:
	case REQ_OP_WRITE:
	case REQ_OP_ZONE_APPEND:
		/*
		 * zloop_rw() always executes asynchronously or completes
		 * directly.
		 */
		zloop_rw(cmd);
		return;
	case REQ_OP_FLUSH:
		/*
		 * Sync the entire FS containing the zone files instead of
		 * walking all files
		 */
		cmd->ret = sync_filesystem(file_inode(zlo->data_dir)->i_sb);
		break;
	case REQ_OP_ZONE_RESET:
		cmd->ret = zloop_reset_zone(zlo, rq_zone_no(rq));
		break;
	case REQ_OP_ZONE_RESET_ALL:
		cmd->ret = zloop_reset_all_zones(zlo);
		break;
	case REQ_OP_ZONE_FINISH:
		cmd->ret = zloop_finish_zone(zlo, rq_zone_no(rq));
		break;
	case REQ_OP_ZONE_OPEN:
		cmd->ret = zloop_open_zone(zlo, rq_zone_no(rq));
		break;
	case REQ_OP_ZONE_CLOSE:
		cmd->ret = zloop_close_zone(zlo, rq_zone_no(rq));
		break;
	default:
		WARN_ON_ONCE(1);
		pr_err("Unsupported operation %d\n", req_op(rq));
		cmd->ret = -EOPNOTSUPP;
		break;
	}

	blk_mq_complete_request(rq);
}

static void zloop_cmd_workfn(struct work_struct *work)
{
	struct zloop_cmd *cmd = container_of(work, struct zloop_cmd, work);
	int orig_flags = current->flags;

	current->flags |= PF_LOCAL_THROTTLE | PF_MEMALLOC_NOIO;
	zloop_handle_cmd(cmd);
	current->flags = orig_flags;
}

static void zloop_complete_rq(struct request *rq)
{
	struct zloop_cmd *cmd = blk_mq_rq_to_pdu(rq);
	struct zloop_device *zlo = rq->q->queuedata;
	unsigned int zone_no = cmd->sector >> zlo->zone_shift;
	struct zloop_zone *zone = &zlo->zones[zone_no];
	blk_status_t sts = BLK_STS_OK;

	switch (req_op(rq)) {
	case REQ_OP_READ:
		if (cmd->ret < 0)
			pr_err("Zone %u: failed read sector %llu, %llu sectors\n",
			       zone_no, cmd->sector, cmd->nr_sectors);

		if (cmd->ret >= 0 && cmd->ret != blk_rq_bytes(rq)) {
			/* short read */
			struct bio *bio;

			__rq_for_each_bio(bio, rq)
				zero_fill_bio(bio);
		}
		break;
	case REQ_OP_WRITE:
	case REQ_OP_ZONE_APPEND:
		if (cmd->ret < 0)
			pr_err("Zone %u: failed %swrite sector %llu, %llu sectors\n",
			       zone_no,
			       req_op(rq) == REQ_OP_WRITE ? "" : "append ",
			       cmd->sector, cmd->nr_sectors);

		if (cmd->ret >= 0 && cmd->ret != blk_rq_bytes(rq)) {
			pr_err("Zone %u: partial write %ld/%u B\n",
			       zone_no, cmd->ret, blk_rq_bytes(rq));
			cmd->ret = -EIO;
		}

		if (cmd->ret < 0 && !test_bit(ZLOOP_ZONE_CONV, &zone->flags)) {
			/*
			 * A write to a sequential zone file failed: mark the
			 * zone as having an error. This will be corrected and
			 * cleared when the next IO is submitted.
			 */
			set_bit(ZLOOP_ZONE_SEQ_ERROR, &zone->flags);
			break;
		}
		if (req_op(rq) == REQ_OP_ZONE_APPEND)
			rq->__sector = cmd->sector;

		break;
	default:
		break;
	}

	if (cmd->ret < 0)
		sts = errno_to_blk_status(cmd->ret);
	blk_mq_end_request(rq, sts);
}

static blk_status_t zloop_queue_rq(struct blk_mq_hw_ctx *hctx,
				   const struct blk_mq_queue_data *bd)
{
	struct request *rq = bd->rq;
	struct zloop_cmd *cmd = blk_mq_rq_to_pdu(rq);
	struct zloop_device *zlo = rq->q->queuedata;

	if (zlo->state == Zlo_deleting)
		return BLK_STS_IOERR;

	blk_mq_start_request(rq);

	INIT_WORK(&cmd->work, zloop_cmd_workfn);
	queue_work(zlo->workqueue, &cmd->work);

	return BLK_STS_OK;
}

static const struct blk_mq_ops zloop_mq_ops = {
	.queue_rq       = zloop_queue_rq,
	.complete	= zloop_complete_rq,
};

static int zloop_open(struct gendisk *disk, blk_mode_t mode)
{
	struct zloop_device *zlo = disk->private_data;
	int ret;

	ret = mutex_lock_killable(&zloop_ctl_mutex);
	if (ret)
		return ret;

	if (zlo->state != Zlo_live)
		ret = -ENXIO;
	mutex_unlock(&zloop_ctl_mutex);
	return ret;
}

static int zloop_report_zones(struct gendisk *disk, sector_t sector,
		unsigned int nr_zones, report_zones_cb cb, void *data)
{
	struct zloop_device *zlo = disk->private_data;
	struct blk_zone blkz = {};
	unsigned int first, i;
	int ret;

	first = disk_zone_no(disk, sector);
	if (first >= zlo->nr_zones)
		return 0;
	nr_zones = min(nr_zones, zlo->nr_zones - first);

	for (i = 0; i < nr_zones; i++) {
		unsigned int zone_no = first + i;
		struct zloop_zone *zone = &zlo->zones[zone_no];

		mutex_lock(&zone->lock);

		if (test_and_clear_bit(ZLOOP_ZONE_SEQ_ERROR, &zone->flags)) {
			ret = zloop_update_seq_zone(zlo, zone_no);
			if (ret) {
				mutex_unlock(&zone->lock);
				return ret;
			}
		}

		blkz.start = zone->start;
		blkz.len = zlo->zone_size;
		blkz.wp = zone->wp;
		blkz.cond = zone->cond;
		if (test_bit(ZLOOP_ZONE_CONV, &zone->flags)) {
			blkz.type = BLK_ZONE_TYPE_CONVENTIONAL;
			blkz.capacity = zlo->zone_size;
		} else {
			blkz.type = BLK_ZONE_TYPE_SEQWRITE_REQ;
			blkz.capacity = zlo->zone_capacity;
		}

		mutex_unlock(&zone->lock);

		ret = cb(&blkz, i, data);
		if (ret)
			return ret;
	}

	return nr_zones;
}

static void zloop_free_disk(struct gendisk *disk)
{
	struct zloop_device *zlo = disk->private_data;
	unsigned int i;

	blk_mq_free_tag_set(&zlo->tag_set);

	for (i = 0; i < zlo->nr_zones; i++) {
		struct zloop_zone *zone = &zlo->zones[i];

		mapping_set_gfp_mask(zone->file->f_mapping,
				zone->old_gfp_mask);
		fput(zone->file);
	}

	fput(zlo->data_dir);
	destroy_workqueue(zlo->workqueue);
	kfree(zlo->base_dir);
	kvfree(zlo);
}

static const struct block_device_operations zloop_fops = {
	.owner			= THIS_MODULE,
	.open			= zloop_open,
	.report_zones		= zloop_report_zones,
	.free_disk		= zloop_free_disk,
};

__printf(3, 4)
static struct file *zloop_filp_open_fmt(int oflags, umode_t mode,
		const char *fmt, ...)
{
	struct file *file;
	va_list ap;
	char *p;

	va_start(ap, fmt);
	p = kvasprintf(GFP_KERNEL, fmt, ap);
	va_end(ap);

	if (!p)
		return ERR_PTR(-ENOMEM);
	file = filp_open(p, oflags, mode);
	kfree(p);
	return file;
}

static int zloop_get_block_size(struct zloop_device *zlo,
				struct zloop_zone *zone)
{
	struct block_device *sb_bdev = zone->file->f_mapping->host->i_sb->s_bdev;
	struct kstat st;

	/*
	 * If the FS block size is lower than or equal to 4K, use that as the
	 * device block size. Otherwise, fallback to the FS direct IO alignment
	 * constraint if that is provided, and to the FS underlying device
	 * physical block size if the direct IO alignment is unknown.
	 */
	if (file_inode(zone->file)->i_sb->s_blocksize <= SZ_4K)
		zlo->block_size = file_inode(zone->file)->i_sb->s_blocksize;
	else if (!vfs_getattr(&zone->file->f_path, &st, STATX_DIOALIGN, 0) &&
		 (st.result_mask & STATX_DIOALIGN))
		zlo->block_size = st.dio_offset_align;
	else if (sb_bdev)
		zlo->block_size = bdev_physical_block_size(sb_bdev);
	else
		zlo->block_size = SECTOR_SIZE;

	if (zlo->zone_capacity & ((zlo->block_size >> SECTOR_SHIFT) - 1)) {
		pr_err("Zone capacity is not aligned to block size %u\n",
		       zlo->block_size);
		return -EINVAL;
	}

	return 0;
}

static int zloop_init_zone(struct zloop_device *zlo, struct zloop_options *opts,
			   unsigned int zone_no, bool restore)
{
	struct zloop_zone *zone = &zlo->zones[zone_no];
	int oflags = O_RDWR;
	struct kstat stat;
	sector_t file_sectors;
	int ret;

	mutex_init(&zone->lock);
	zone->start = (sector_t)zone_no << zlo->zone_shift;

	if (!restore)
		oflags |= O_CREAT;

	if (!opts->buffered_io)
		oflags |= O_DIRECT;

	if (zone_no < zlo->nr_conv_zones) {
		/* Conventional zone file. */
		set_bit(ZLOOP_ZONE_CONV, &zone->flags);
		zone->cond = BLK_ZONE_COND_NOT_WP;
		zone->wp = U64_MAX;

		zone->file = zloop_filp_open_fmt(oflags, 0600, "%s/%u/cnv-%06u",
					zlo->base_dir, zlo->id, zone_no);
		if (IS_ERR(zone->file)) {
			pr_err("Failed to open zone %u file %s/%u/cnv-%06u (err=%ld)",
			       zone_no, zlo->base_dir, zlo->id, zone_no,
			       PTR_ERR(zone->file));
			return PTR_ERR(zone->file);
		}

		if (!zlo->block_size) {
			ret = zloop_get_block_size(zlo, zone);
			if (ret)
				return ret;
		}

		ret = vfs_getattr(&zone->file->f_path, &stat, STATX_SIZE, 0);
		if (ret < 0) {
			pr_err("Failed to get zone %u file stat\n", zone_no);
			return ret;
		}
		file_sectors = stat.size >> SECTOR_SHIFT;

		if (restore && file_sectors != zlo->zone_size) {
			pr_err("Invalid conventional zone %u file size (%llu sectors != %llu)\n",
			       zone_no, file_sectors, zlo->zone_capacity);
			return ret;
		}

		ret = vfs_truncate(&zone->file->f_path,
				   zlo->zone_size << SECTOR_SHIFT);
		if (ret < 0) {
			pr_err("Failed to truncate zone %u file (err=%d)\n",
			       zone_no, ret);
			return ret;
		}

		return 0;
	}

	/* Sequential zone file. */
	zone->file = zloop_filp_open_fmt(oflags, 0600, "%s/%u/seq-%06u",
					 zlo->base_dir, zlo->id, zone_no);
	if (IS_ERR(zone->file)) {
		pr_err("Failed to open zone %u file %s/%u/seq-%06u (err=%ld)",
		       zone_no, zlo->base_dir, zlo->id, zone_no,
		       PTR_ERR(zone->file));
		return PTR_ERR(zone->file);
	}

	if (!zlo->block_size) {
		ret = zloop_get_block_size(zlo, zone);
		if (ret)
			return ret;
	}

	zloop_get_block_size(zlo, zone);

	mutex_lock(&zone->lock);
	ret = zloop_update_seq_zone(zlo, zone_no);
	mutex_unlock(&zone->lock);

	return ret;
}

static bool zloop_dev_exists(struct zloop_device *zlo)
{
	struct file *cnv, *seq;
	bool exists;

	cnv = zloop_filp_open_fmt(O_RDONLY, 0600, "%s/%u/cnv-%06u",
				  zlo->base_dir, zlo->id, 0);
	seq = zloop_filp_open_fmt(O_RDONLY, 0600, "%s/%u/seq-%06u",
				  zlo->base_dir, zlo->id, 0);
	exists = !IS_ERR(cnv) || !IS_ERR(seq);

	if (!IS_ERR(cnv))
		fput(cnv);
	if (!IS_ERR(seq))
		fput(seq);

	return exists;
}

static int zloop_ctl_add(struct zloop_options *opts)
{
	struct queue_limits lim = {
		.max_hw_sectors		= SZ_1M >> SECTOR_SHIFT,
		.max_hw_zone_append_sectors = SZ_1M >> SECTOR_SHIFT,
		.chunk_sectors		= opts->zone_size,
		.features		= BLK_FEAT_ZONED,
	};
	unsigned int nr_zones, i, j;
	struct zloop_device *zlo;
	int ret = -EINVAL;
	bool restore;

	__module_get(THIS_MODULE);

	nr_zones = opts->capacity >> ilog2(opts->zone_size);
	if (opts->nr_conv_zones >= nr_zones) {
		pr_err("Invalid number of conventional zones %u\n",
		       opts->nr_conv_zones);
		goto out;
	}

	zlo = kvzalloc(struct_size(zlo, zones, nr_zones), GFP_KERNEL);
	if (!zlo) {
		ret = -ENOMEM;
		goto out;
	}
	zlo->state = Zlo_creating;

	ret = mutex_lock_killable(&zloop_ctl_mutex);
	if (ret)
		goto out_free_dev;

	/* Allocate id, if @opts->id >= 0, we're requesting that specific id */
	if (opts->id >= 0) {
		ret = idr_alloc(&zloop_index_idr, zlo,
				  opts->id, opts->id + 1, GFP_KERNEL);
		if (ret == -ENOSPC)
			ret = -EEXIST;
	} else {
		ret = idr_alloc(&zloop_index_idr, zlo, 0, 0, GFP_KERNEL);
	}
	mutex_unlock(&zloop_ctl_mutex);
	if (ret < 0)
		goto out_free_dev;

	zlo->id = ret;
	zlo->zone_shift = ilog2(opts->zone_size);
	zlo->zone_size = opts->zone_size;
	if (opts->zone_capacity)
		zlo->zone_capacity = opts->zone_capacity;
	else
		zlo->zone_capacity = zlo->zone_size;
	zlo->nr_zones = nr_zones;
	zlo->nr_conv_zones = opts->nr_conv_zones;
	zlo->buffered_io = opts->buffered_io;

	zlo->workqueue = alloc_workqueue("zloop%d", WQ_UNBOUND | WQ_FREEZABLE,
				opts->nr_queues * opts->queue_depth, zlo->id);
	if (!zlo->workqueue) {
		ret = -ENOMEM;
		goto out_free_idr;
	}

	if (opts->base_dir)
		zlo->base_dir = kstrdup(opts->base_dir, GFP_KERNEL);
	else
		zlo->base_dir = kstrdup(ZLOOP_DEF_BASE_DIR, GFP_KERNEL);
	if (!zlo->base_dir) {
		ret = -ENOMEM;
		goto out_destroy_workqueue;
	}

	zlo->data_dir = zloop_filp_open_fmt(O_RDONLY | O_DIRECTORY, 0, "%s/%u",
					    zlo->base_dir, zlo->id);
	if (IS_ERR(zlo->data_dir)) {
		ret = PTR_ERR(zlo->data_dir);
		pr_warn("Failed to open directory %s/%u (err=%d)\n",
			zlo->base_dir, zlo->id, ret);
		goto out_free_base_dir;
	}

	/*
	 * If we already have zone files, we are restoring a device created by a
	 * previous add operation. In this case, zloop_init_zone() will check
	 * that the zone files are consistent with the zone configuration given.
	 */
	restore = zloop_dev_exists(zlo);
	for (i = 0; i < nr_zones; i++) {
		ret = zloop_init_zone(zlo, opts, i, restore);
		if (ret)
			goto out_close_files;
	}

	lim.physical_block_size = zlo->block_size;
	lim.logical_block_size = zlo->block_size;

	zlo->tag_set.ops = &zloop_mq_ops;
	zlo->tag_set.nr_hw_queues = opts->nr_queues;
	zlo->tag_set.queue_depth = opts->queue_depth;
	zlo->tag_set.numa_node = NUMA_NO_NODE;
	zlo->tag_set.cmd_size = sizeof(struct zloop_cmd);
	zlo->tag_set.driver_data = zlo;

	ret = blk_mq_alloc_tag_set(&zlo->tag_set);
	if (ret) {
		pr_err("blk_mq_alloc_tag_set failed (err=%d)\n", ret);
		goto out_close_files;
	}

	zlo->disk = blk_mq_alloc_disk(&zlo->tag_set, &lim, zlo);
	if (IS_ERR(zlo->disk)) {
		pr_err("blk_mq_alloc_disk failed (err=%d)\n", ret);
		ret = PTR_ERR(zlo->disk);
		goto out_cleanup_tags;
	}
	zlo->disk->flags = GENHD_FL_NO_PART;
	zlo->disk->fops = &zloop_fops;
	zlo->disk->private_data = zlo;
	sprintf(zlo->disk->disk_name, "zloop%d", zlo->id);
	set_capacity(zlo->disk, (u64)lim.chunk_sectors * zlo->nr_zones);

	ret = blk_revalidate_disk_zones(zlo->disk);
	if (ret)
		goto out_cleanup_disk;

	ret = add_disk(zlo->disk);
	if (ret) {
		pr_err("add_disk failed (err=%d)\n", ret);
		goto out_cleanup_disk;
	}

	mutex_lock(&zloop_ctl_mutex);
	zlo->state = Zlo_live;
	mutex_unlock(&zloop_ctl_mutex);

	pr_info("Added device %d: %u zones of %llu MB, %u B block size\n",
		zlo->id, zlo->nr_zones,
		((sector_t)zlo->zone_size << SECTOR_SHIFT) >> 20,
		zlo->block_size);

	return 0;

out_cleanup_disk:
	put_disk(zlo->disk);
out_cleanup_tags:
	blk_mq_free_tag_set(&zlo->tag_set);
out_close_files:
	for (j = 0; j < i; j++) {
		struct zloop_zone *zone = &zlo->zones[j];

		if (!IS_ERR_OR_NULL(zone->file))
			fput(zone->file);
	}
	fput(zlo->data_dir);
out_free_base_dir:
	kfree(zlo->base_dir);
out_destroy_workqueue:
	destroy_workqueue(zlo->workqueue);
out_free_idr:
	mutex_lock(&zloop_ctl_mutex);
	idr_remove(&zloop_index_idr, zlo->id);
	mutex_unlock(&zloop_ctl_mutex);
out_free_dev:
	kvfree(zlo);
out:
	module_put(THIS_MODULE);
	if (ret == -ENOENT)
		ret = -EINVAL;
	return ret;
}

static int zloop_ctl_remove(struct zloop_options *opts)
{
	struct zloop_device *zlo;
	int ret;

	if (!(opts->mask & ZLOOP_OPT_ID)) {
		pr_err("No ID specified\n");
		return -EINVAL;
	}

	ret = mutex_lock_killable(&zloop_ctl_mutex);
	if (ret)
		return ret;

	zlo = idr_find(&zloop_index_idr, opts->id);
	if (!zlo || zlo->state == Zlo_creating) {
		ret = -ENODEV;
	} else if (zlo->state == Zlo_deleting) {
		ret = -EINVAL;
	} else {
		idr_remove(&zloop_index_idr, zlo->id);
		zlo->state = Zlo_deleting;
	}

	mutex_unlock(&zloop_ctl_mutex);
	if (ret)
		return ret;

	del_gendisk(zlo->disk);
	put_disk(zlo->disk);

	pr_info("Removed device %d\n", opts->id);

	module_put(THIS_MODULE);

	return 0;
}

static int zloop_parse_options(struct zloop_options *opts, const char *buf)
{
	substring_t args[MAX_OPT_ARGS];
	char *options, *o, *p;
	unsigned int token;
	int ret = 0;

	/* Set defaults. */
	opts->mask = 0;
	opts->id = ZLOOP_DEF_ID;
	opts->capacity = ZLOOP_DEF_ZONE_SIZE * ZLOOP_DEF_NR_ZONES;
	opts->zone_size = ZLOOP_DEF_ZONE_SIZE;
	opts->nr_conv_zones = ZLOOP_DEF_NR_CONV_ZONES;
	opts->nr_queues = ZLOOP_DEF_NR_QUEUES;
	opts->queue_depth = ZLOOP_DEF_QUEUE_DEPTH;
	opts->buffered_io = ZLOOP_DEF_BUFFERED_IO;

	if (!buf)
		return 0;

	/* Skip leading spaces before the options. */
	while (isspace(*buf))
		buf++;

	options = o = kstrdup(buf, GFP_KERNEL);
	if (!options)
		return -ENOMEM;

	/* Parse the options, doing only some light invalid value checks. */
	while ((p = strsep(&o, ",\n")) != NULL) {
		if (!*p)
			continue;

		token = match_token(p, zloop_opt_tokens, args);
		opts->mask |= token;
		switch (token) {
		case ZLOOP_OPT_ID:
			if (match_int(args, &opts->id)) {
				ret = -EINVAL;
				goto out;
			}
			break;
		case ZLOOP_OPT_CAPACITY:
			if (match_uint(args, &token)) {
				ret = -EINVAL;
				goto out;
			}
			if (!token) {
				pr_err("Invalid capacity\n");
				ret = -EINVAL;
				goto out;
			}
			opts->capacity =
				((sector_t)token * SZ_1M) >> SECTOR_SHIFT;
			break;
		case ZLOOP_OPT_ZONE_SIZE:
			if (match_uint(args, &token)) {
				ret = -EINVAL;
				goto out;
			}
			if (!token || token > ZLOOP_MAX_ZONE_SIZE_MB ||
			    !is_power_of_2(token)) {
				pr_err("Invalid zone size %u\n", token);
				ret = -EINVAL;
				goto out;
			}
			opts->zone_size =
				((sector_t)token * SZ_1M) >> SECTOR_SHIFT;
			break;
		case ZLOOP_OPT_ZONE_CAPACITY:
			if (match_uint(args, &token)) {
				ret = -EINVAL;
				goto out;
			}
			if (!token) {
				pr_err("Invalid zone capacity\n");
				ret = -EINVAL;
				goto out;
			}
			opts->zone_capacity =
				((sector_t)token * SZ_1M) >> SECTOR_SHIFT;
			break;
		case ZLOOP_OPT_NR_CONV_ZONES:
			if (match_uint(args, &token)) {
				ret = -EINVAL;
				goto out;
			}
			opts->nr_conv_zones = token;
			break;
		case ZLOOP_OPT_BASE_DIR:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			kfree(opts->base_dir);
			opts->base_dir = p;
			break;
		case ZLOOP_OPT_NR_QUEUES:
			if (match_uint(args, &token)) {
				ret = -EINVAL;
				goto out;
			}
			if (!token) {
				pr_err("Invalid number of queues\n");
				ret = -EINVAL;
				goto out;
			}
			opts->nr_queues = min(token, num_online_cpus());
			break;
		case ZLOOP_OPT_QUEUE_DEPTH:
			if (match_uint(args, &token)) {
				ret = -EINVAL;
				goto out;
			}
			if (!token) {
				pr_err("Invalid queue depth\n");
				ret = -EINVAL;
				goto out;
			}
			opts->queue_depth = token;
			break;
		case ZLOOP_OPT_BUFFERED_IO:
			opts->buffered_io = true;
			break;
		case ZLOOP_OPT_ERR:
		default:
			pr_warn("unknown parameter or missing value '%s'\n", p);
			ret = -EINVAL;
			goto out;
		}
	}

	ret = -EINVAL;
	if (opts->capacity <= opts->zone_size) {
		pr_err("Invalid capacity\n");
		goto out;
	}

	if (opts->zone_capacity > opts->zone_size) {
		pr_err("Invalid zone capacity\n");
		goto out;
	}

	ret = 0;
out:
	kfree(options);
	return ret;
}

enum {
	ZLOOP_CTL_ADD,
	ZLOOP_CTL_REMOVE,
};

static struct zloop_ctl_op {
	int		code;
	const char	*name;
} zloop_ctl_ops[] = {
	{ ZLOOP_CTL_ADD,	"add" },
	{ ZLOOP_CTL_REMOVE,	"remove" },
	{ -1,	NULL },
};

static ssize_t zloop_ctl_write(struct file *file, const char __user *ubuf,
			       size_t count, loff_t *pos)
{
	struct zloop_options opts = { };
	struct zloop_ctl_op *op;
	const char *buf, *opts_buf;
	int i, ret;

	if (count > PAGE_SIZE)
		return -ENOMEM;

	buf = memdup_user_nul(ubuf, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	for (i = 0; i < ARRAY_SIZE(zloop_ctl_ops); i++) {
		op = &zloop_ctl_ops[i];
		if (!op->name) {
			pr_err("Invalid operation\n");
			ret = -EINVAL;
			goto out;
		}
		if (!strncmp(buf, op->name, strlen(op->name)))
			break;
	}

	if (count <= strlen(op->name))
		opts_buf = NULL;
	else
		opts_buf = buf + strlen(op->name);

	ret = zloop_parse_options(&opts, opts_buf);
	if (ret) {
		pr_err("Failed to parse options\n");
		goto out;
	}

	switch (op->code) {
	case ZLOOP_CTL_ADD:
		ret = zloop_ctl_add(&opts);
		break;
	case ZLOOP_CTL_REMOVE:
		ret = zloop_ctl_remove(&opts);
		break;
	default:
		pr_err("Invalid operation\n");
		ret = -EINVAL;
		goto out;
	}

out:
	kfree(opts.base_dir);
	kfree(buf);
	return ret ? ret : count;
}

static int zloop_ctl_show(struct seq_file *seq_file, void *private)
{
	const struct match_token *tok;
	int i;

	/* Add operation */
	seq_printf(seq_file, "%s ", zloop_ctl_ops[0].name);
	for (i = 0; i < ARRAY_SIZE(zloop_opt_tokens); i++) {
		tok = &zloop_opt_tokens[i];
		if (!tok->pattern)
			break;
		if (i)
			seq_putc(seq_file, ',');
		seq_puts(seq_file, tok->pattern);
	}
	seq_putc(seq_file, '\n');

	/* Remove operation */
	seq_puts(seq_file, zloop_ctl_ops[1].name);
	seq_puts(seq_file, " id=%d\n");

	return 0;
}

static int zloop_ctl_open(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return single_open(file, zloop_ctl_show, NULL);
}

static int zloop_ctl_release(struct inode *inode, struct file *file)
{
	return single_release(inode, file);
}

static const struct file_operations zloop_ctl_fops = {
	.owner		= THIS_MODULE,
	.open		= zloop_ctl_open,
	.release	= zloop_ctl_release,
	.write		= zloop_ctl_write,
	.read		= seq_read,
};

static struct miscdevice zloop_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "zloop-control",
	.fops		= &zloop_ctl_fops,
};

static int __init zloop_init(void)
{
	int ret;

	ret = misc_register(&zloop_misc);
	if (ret) {
		pr_err("Failed to register misc device: %d\n", ret);
		return ret;
	}
	pr_info("Module loaded\n");

	return 0;
}

static void __exit zloop_exit(void)
{
	misc_deregister(&zloop_misc);
	idr_destroy(&zloop_index_idr);
}

module_init(zloop_init);
module_exit(zloop_exit);

MODULE_DESCRIPTION("Zoned loopback device");
MODULE_LICENSE("GPL");
