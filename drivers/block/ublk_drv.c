// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Userspace block device - block device which IO is handled from userspace
 *
 * Take full use of io_uring passthrough command for communicating with
 * ublk userspace daemon(ublksrvd) for handling basic IO request.
 *
 * Copyright 2022 Ming Lei <ming.lei@redhat.com>
 *
 * (part of code stolen from loop.c)
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/wait.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/mutex.h>
#include <linux/writeback.h>
#include <linux/completion.h>
#include <linux/highmem.h>
#include <linux/sysfs.h>
#include <linux/miscdevice.h>
#include <linux/falloc.h>
#include <linux/uio.h>
#include <linux/ioprio.h>
#include <linux/sched/mm.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/io_uring.h>
#include <linux/blk-mq.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <linux/task_work.h>
#include <linux/namei.h>
#include <linux/kref.h>
#include <uapi/linux/ublk_cmd.h>

#define UBLK_MINORS		(1U << MINORBITS)

/* All UBLK_F_* have to be included into UBLK_F_ALL */
#define UBLK_F_ALL (UBLK_F_SUPPORT_ZERO_COPY \
		| UBLK_F_URING_CMD_COMP_IN_TASK \
		| UBLK_F_NEED_GET_DATA \
		| UBLK_F_USER_RECOVERY \
		| UBLK_F_USER_RECOVERY_REISSUE \
		| UBLK_F_UNPRIVILEGED_DEV \
		| UBLK_F_CMD_IOCTL_ENCODE \
		| UBLK_F_USER_COPY \
		| UBLK_F_ZONED)

/* All UBLK_PARAM_TYPE_* should be included here */
#define UBLK_PARAM_TYPE_ALL                                \
	(UBLK_PARAM_TYPE_BASIC | UBLK_PARAM_TYPE_DISCARD | \
	 UBLK_PARAM_TYPE_DEVT | UBLK_PARAM_TYPE_ZONED)

struct ublk_rq_data {
	struct llist_node node;

	struct kref ref;
	__u64 sector;
	__u32 operation;
	__u32 nr_zones;
};

struct ublk_uring_cmd_pdu {
	struct ublk_queue *ubq;
};

/*
 * io command is active: sqe cmd is received, and its cqe isn't done
 *
 * If the flag is set, the io command is owned by ublk driver, and waited
 * for incoming blk-mq request from the ublk block device.
 *
 * If the flag is cleared, the io command will be completed, and owned by
 * ublk server.
 */
#define UBLK_IO_FLAG_ACTIVE	0x01

/*
 * IO command is completed via cqe, and it is being handled by ublksrv, and
 * not committed yet
 *
 * Basically exclusively with UBLK_IO_FLAG_ACTIVE, so can be served for
 * cross verification
 */
#define UBLK_IO_FLAG_OWNED_BY_SRV 0x02

/*
 * IO command is aborted, so this flag is set in case of
 * !UBLK_IO_FLAG_ACTIVE.
 *
 * After this flag is observed, any pending or new incoming request
 * associated with this io command will be failed immediately
 */
#define UBLK_IO_FLAG_ABORTED 0x04

/*
 * UBLK_IO_FLAG_NEED_GET_DATA is set because IO command requires
 * get data buffer address from ublksrv.
 *
 * Then, bio data could be copied into this data buffer for a WRITE request
 * after the IO command is issued again and UBLK_IO_FLAG_NEED_GET_DATA is unset.
 */
#define UBLK_IO_FLAG_NEED_GET_DATA 0x08

struct ublk_io {
	/* userspace buffer address from io cmd */
	__u64	addr;
	unsigned int flags;
	int res;

	struct io_uring_cmd *cmd;
};

struct ublk_queue {
	int q_id;
	int q_depth;

	unsigned long flags;
	struct task_struct	*ubq_daemon;
	char *io_cmd_buf;

	struct llist_head	io_cmds;

	unsigned long io_addr;	/* mapped vm address */
	unsigned int max_io_sz;
	bool force_abort;
	bool timeout;
	unsigned short nr_io_ready;	/* how many ios setup */
	struct ublk_device *dev;
	struct ublk_io ios[];
};

#define UBLK_DAEMON_MONITOR_PERIOD	(5 * HZ)

struct ublk_device {
	struct gendisk		*ub_disk;

	char	*__queues;

	unsigned int	queue_size;
	struct ublksrv_ctrl_dev_info	dev_info;

	struct blk_mq_tag_set	tag_set;

	struct cdev		cdev;
	struct device		cdev_dev;

#define UB_STATE_OPEN		0
#define UB_STATE_USED		1
#define UB_STATE_DELETED	2
	unsigned long		state;
	int			ub_number;

	struct mutex		mutex;

	spinlock_t		mm_lock;
	struct mm_struct	*mm;

	struct ublk_params	params;

	struct completion	completion;
	unsigned int		nr_queues_ready;
	unsigned int		nr_privileged_daemon;

	/*
	 * Our ubq->daemon may be killed without any notification, so
	 * monitor each queue's daemon periodically
	 */
	struct delayed_work	monitor_work;
	struct work_struct	quiesce_work;
	struct work_struct	stop_work;
};

/* header of ublk_params */
struct ublk_params_header {
	__u32	len;
	__u32	types;
};

static inline unsigned int ublk_req_build_flags(struct request *req);
static inline struct ublksrv_io_desc *ublk_get_iod(struct ublk_queue *ubq,
						   int tag);

static inline bool ublk_dev_is_user_copy(const struct ublk_device *ub)
{
	return ub->dev_info.flags & UBLK_F_USER_COPY;
}

static inline bool ublk_dev_is_zoned(const struct ublk_device *ub)
{
	return ub->dev_info.flags & UBLK_F_ZONED;
}

static inline bool ublk_queue_is_zoned(struct ublk_queue *ubq)
{
	return ubq->flags & UBLK_F_ZONED;
}

#ifdef CONFIG_BLK_DEV_ZONED

static int ublk_get_nr_zones(const struct ublk_device *ub)
{
	const struct ublk_param_basic *p = &ub->params.basic;

	/* Zone size is a power of 2 */
	return p->dev_sectors >> ilog2(p->chunk_sectors);
}

static int ublk_revalidate_disk_zones(struct ublk_device *ub)
{
	return blk_revalidate_disk_zones(ub->ub_disk, NULL);
}

static int ublk_dev_param_zoned_validate(const struct ublk_device *ub)
{
	const struct ublk_param_zoned *p = &ub->params.zoned;
	int nr_zones;

	if (!ublk_dev_is_zoned(ub))
		return -EINVAL;

	if (!p->max_zone_append_sectors)
		return -EINVAL;

	nr_zones = ublk_get_nr_zones(ub);

	if (p->max_active_zones > nr_zones)
		return -EINVAL;

	if (p->max_open_zones > nr_zones)
		return -EINVAL;

	return 0;
}

static int ublk_dev_param_zoned_apply(struct ublk_device *ub)
{
	const struct ublk_param_zoned *p = &ub->params.zoned;

	disk_set_zoned(ub->ub_disk, BLK_ZONED_HM);
	blk_queue_flag_set(QUEUE_FLAG_ZONE_RESETALL, ub->ub_disk->queue);
	blk_queue_required_elevator_features(ub->ub_disk->queue,
					     ELEVATOR_F_ZBD_SEQ_WRITE);
	disk_set_max_active_zones(ub->ub_disk, p->max_active_zones);
	disk_set_max_open_zones(ub->ub_disk, p->max_open_zones);
	blk_queue_max_zone_append_sectors(ub->ub_disk->queue, p->max_zone_append_sectors);

	ub->ub_disk->nr_zones = ublk_get_nr_zones(ub);

	return 0;
}

/* Based on virtblk_alloc_report_buffer */
static void *ublk_alloc_report_buffer(struct ublk_device *ublk,
				      unsigned int nr_zones, size_t *buflen)
{
	struct request_queue *q = ublk->ub_disk->queue;
	size_t bufsize;
	void *buf;

	nr_zones = min_t(unsigned int, nr_zones,
			 ublk->ub_disk->nr_zones);

	bufsize = nr_zones * sizeof(struct blk_zone);
	bufsize =
		min_t(size_t, bufsize, queue_max_hw_sectors(q) << SECTOR_SHIFT);

	while (bufsize >= sizeof(struct blk_zone)) {
		buf = kvmalloc(bufsize, GFP_KERNEL | __GFP_NORETRY);
		if (buf) {
			*buflen = bufsize;
			return buf;
		}
		bufsize >>= 1;
	}

	*buflen = 0;
	return NULL;
}

static int ublk_report_zones(struct gendisk *disk, sector_t sector,
		      unsigned int nr_zones, report_zones_cb cb, void *data)
{
	struct ublk_device *ub = disk->private_data;
	unsigned int zone_size_sectors = disk->queue->limits.chunk_sectors;
	unsigned int first_zone = sector >> ilog2(zone_size_sectors);
	unsigned int done_zones = 0;
	unsigned int max_zones_per_request;
	int ret;
	struct blk_zone *buffer;
	size_t buffer_length;

	nr_zones = min_t(unsigned int, ub->ub_disk->nr_zones - first_zone,
			 nr_zones);

	buffer = ublk_alloc_report_buffer(ub, nr_zones, &buffer_length);
	if (!buffer)
		return -ENOMEM;

	max_zones_per_request = buffer_length / sizeof(struct blk_zone);

	while (done_zones < nr_zones) {
		unsigned int remaining_zones = nr_zones - done_zones;
		unsigned int zones_in_request =
			min_t(unsigned int, remaining_zones, max_zones_per_request);
		struct request *req;
		struct ublk_rq_data *pdu;
		blk_status_t status;

		memset(buffer, 0, buffer_length);

		req = blk_mq_alloc_request(disk->queue, REQ_OP_DRV_IN, 0);
		if (IS_ERR(req)) {
			ret = PTR_ERR(req);
			goto out;
		}

		pdu = blk_mq_rq_to_pdu(req);
		pdu->operation = UBLK_IO_OP_REPORT_ZONES;
		pdu->sector = sector;
		pdu->nr_zones = zones_in_request;

		ret = blk_rq_map_kern(disk->queue, req, buffer, buffer_length,
					GFP_KERNEL);
		if (ret) {
			blk_mq_free_request(req);
			goto out;
		}

		status = blk_execute_rq(req, 0);
		ret = blk_status_to_errno(status);
		blk_mq_free_request(req);
		if (ret)
			goto out;

		for (unsigned int i = 0; i < zones_in_request; i++) {
			struct blk_zone *zone = buffer + i;

			/* A zero length zone means no more zones in this response */
			if (!zone->len)
				break;

			ret = cb(zone, i, data);
			if (ret)
				goto out;

			done_zones++;
			sector += zone_size_sectors;

		}
	}

	ret = done_zones;

out:
	kvfree(buffer);
	return ret;
}

static blk_status_t ublk_setup_iod_zoned(struct ublk_queue *ubq,
					 struct request *req)
{
	struct ublksrv_io_desc *iod = ublk_get_iod(ubq, req->tag);
	struct ublk_io *io = &ubq->ios[req->tag];
	struct ublk_rq_data *pdu = blk_mq_rq_to_pdu(req);
	u32 ublk_op;

	switch (req_op(req)) {
	case REQ_OP_ZONE_OPEN:
		ublk_op = UBLK_IO_OP_ZONE_OPEN;
		break;
	case REQ_OP_ZONE_CLOSE:
		ublk_op = UBLK_IO_OP_ZONE_CLOSE;
		break;
	case REQ_OP_ZONE_FINISH:
		ublk_op = UBLK_IO_OP_ZONE_FINISH;
		break;
	case REQ_OP_ZONE_RESET:
		ublk_op = UBLK_IO_OP_ZONE_RESET;
		break;
	case REQ_OP_ZONE_APPEND:
		ublk_op = UBLK_IO_OP_ZONE_APPEND;
		break;
	case REQ_OP_ZONE_RESET_ALL:
		ublk_op = UBLK_IO_OP_ZONE_RESET_ALL;
		break;
	case REQ_OP_DRV_IN:
		ublk_op = pdu->operation;
		switch (ublk_op) {
		case UBLK_IO_OP_REPORT_ZONES:
			iod->op_flags = ublk_op | ublk_req_build_flags(req);
			iod->nr_zones = pdu->nr_zones;
			iod->start_sector = pdu->sector;
			return BLK_STS_OK;
		default:
			return BLK_STS_IOERR;
		}
	case REQ_OP_DRV_OUT:
		/* We do not support drv_out */
		return BLK_STS_NOTSUPP;
	default:
		return BLK_STS_IOERR;
	}

	iod->op_flags = ublk_op | ublk_req_build_flags(req);
	iod->nr_sectors = blk_rq_sectors(req);
	iod->start_sector = blk_rq_pos(req);
	iod->addr = io->addr;

	return BLK_STS_OK;
}

#else

#define ublk_report_zones (NULL)

static int ublk_dev_param_zoned_validate(const struct ublk_device *ub)
{
	return -EOPNOTSUPP;
}

static int ublk_dev_param_zoned_apply(struct ublk_device *ub)
{
	return -EOPNOTSUPP;
}

static int ublk_revalidate_disk_zones(struct ublk_device *ub)
{
	return 0;
}

static blk_status_t ublk_setup_iod_zoned(struct ublk_queue *ubq,
					 struct request *req)
{
	return BLK_STS_NOTSUPP;
}

#endif

static inline void __ublk_complete_rq(struct request *req);
static void ublk_complete_rq(struct kref *ref);

static dev_t ublk_chr_devt;
static const struct class ublk_chr_class = {
	.name = "ublk-char",
};

static DEFINE_IDR(ublk_index_idr);
static DEFINE_SPINLOCK(ublk_idr_lock);
static wait_queue_head_t ublk_idr_wq;	/* wait until one idr is freed */

static DEFINE_MUTEX(ublk_ctl_mutex);

/*
 * Max ublk devices allowed to add
 *
 * It can be extended to one per-user limit in future or even controlled
 * by cgroup.
 */
static unsigned int ublks_max = 64;
static unsigned int ublks_added;	/* protected by ublk_ctl_mutex */

static struct miscdevice ublk_misc;

static inline unsigned ublk_pos_to_hwq(loff_t pos)
{
	return ((pos - UBLKSRV_IO_BUF_OFFSET) >> UBLK_QID_OFF) &
		UBLK_QID_BITS_MASK;
}

static inline unsigned ublk_pos_to_buf_off(loff_t pos)
{
	return (pos - UBLKSRV_IO_BUF_OFFSET) & UBLK_IO_BUF_BITS_MASK;
}

static inline unsigned ublk_pos_to_tag(loff_t pos)
{
	return ((pos - UBLKSRV_IO_BUF_OFFSET) >> UBLK_TAG_OFF) &
		UBLK_TAG_BITS_MASK;
}

static void ublk_dev_param_basic_apply(struct ublk_device *ub)
{
	struct request_queue *q = ub->ub_disk->queue;
	const struct ublk_param_basic *p = &ub->params.basic;

	blk_queue_logical_block_size(q, 1 << p->logical_bs_shift);
	blk_queue_physical_block_size(q, 1 << p->physical_bs_shift);
	blk_queue_io_min(q, 1 << p->io_min_shift);
	blk_queue_io_opt(q, 1 << p->io_opt_shift);

	blk_queue_write_cache(q, p->attrs & UBLK_ATTR_VOLATILE_CACHE,
			p->attrs & UBLK_ATTR_FUA);
	if (p->attrs & UBLK_ATTR_ROTATIONAL)
		blk_queue_flag_clear(QUEUE_FLAG_NONROT, q);
	else
		blk_queue_flag_set(QUEUE_FLAG_NONROT, q);

	blk_queue_max_hw_sectors(q, p->max_sectors);
	blk_queue_chunk_sectors(q, p->chunk_sectors);
	blk_queue_virt_boundary(q, p->virt_boundary_mask);

	if (p->attrs & UBLK_ATTR_READ_ONLY)
		set_disk_ro(ub->ub_disk, true);

	set_capacity(ub->ub_disk, p->dev_sectors);
}

static void ublk_dev_param_discard_apply(struct ublk_device *ub)
{
	struct request_queue *q = ub->ub_disk->queue;
	const struct ublk_param_discard *p = &ub->params.discard;

	q->limits.discard_alignment = p->discard_alignment;
	q->limits.discard_granularity = p->discard_granularity;
	blk_queue_max_discard_sectors(q, p->max_discard_sectors);
	blk_queue_max_write_zeroes_sectors(q,
			p->max_write_zeroes_sectors);
	blk_queue_max_discard_segments(q, p->max_discard_segments);
}

static int ublk_validate_params(const struct ublk_device *ub)
{
	/* basic param is the only one which must be set */
	if (ub->params.types & UBLK_PARAM_TYPE_BASIC) {
		const struct ublk_param_basic *p = &ub->params.basic;

		if (p->logical_bs_shift > PAGE_SHIFT || p->logical_bs_shift < 9)
			return -EINVAL;

		if (p->logical_bs_shift > p->physical_bs_shift)
			return -EINVAL;

		if (p->max_sectors > (ub->dev_info.max_io_buf_bytes >> 9))
			return -EINVAL;

		if (ublk_dev_is_zoned(ub) && !p->chunk_sectors)
			return -EINVAL;
	} else
		return -EINVAL;

	if (ub->params.types & UBLK_PARAM_TYPE_DISCARD) {
		const struct ublk_param_discard *p = &ub->params.discard;

		/* So far, only support single segment discard */
		if (p->max_discard_sectors && p->max_discard_segments != 1)
			return -EINVAL;

		if (!p->discard_granularity)
			return -EINVAL;
	}

	/* dev_t is read-only */
	if (ub->params.types & UBLK_PARAM_TYPE_DEVT)
		return -EINVAL;

	if (ub->params.types & UBLK_PARAM_TYPE_ZONED)
		return ublk_dev_param_zoned_validate(ub);
	else if (ublk_dev_is_zoned(ub))
		return -EINVAL;

	return 0;
}

static int ublk_apply_params(struct ublk_device *ub)
{
	if (!(ub->params.types & UBLK_PARAM_TYPE_BASIC))
		return -EINVAL;

	ublk_dev_param_basic_apply(ub);

	if (ub->params.types & UBLK_PARAM_TYPE_DISCARD)
		ublk_dev_param_discard_apply(ub);

	if (ub->params.types & UBLK_PARAM_TYPE_ZONED)
		return ublk_dev_param_zoned_apply(ub);

	return 0;
}

static inline bool ublk_support_user_copy(const struct ublk_queue *ubq)
{
	return ubq->flags & UBLK_F_USER_COPY;
}

static inline bool ublk_need_req_ref(const struct ublk_queue *ubq)
{
	/*
	 * read()/write() is involved in user copy, so request reference
	 * has to be grabbed
	 */
	return ublk_support_user_copy(ubq);
}

static inline void ublk_init_req_ref(const struct ublk_queue *ubq,
		struct request *req)
{
	if (ublk_need_req_ref(ubq)) {
		struct ublk_rq_data *data = blk_mq_rq_to_pdu(req);

		kref_init(&data->ref);
	}
}

static inline bool ublk_get_req_ref(const struct ublk_queue *ubq,
		struct request *req)
{
	if (ublk_need_req_ref(ubq)) {
		struct ublk_rq_data *data = blk_mq_rq_to_pdu(req);

		return kref_get_unless_zero(&data->ref);
	}

	return true;
}

static inline void ublk_put_req_ref(const struct ublk_queue *ubq,
		struct request *req)
{
	if (ublk_need_req_ref(ubq)) {
		struct ublk_rq_data *data = blk_mq_rq_to_pdu(req);

		kref_put(&data->ref, ublk_complete_rq);
	} else {
		__ublk_complete_rq(req);
	}
}

static inline bool ublk_need_get_data(const struct ublk_queue *ubq)
{
	return ubq->flags & UBLK_F_NEED_GET_DATA;
}

static struct ublk_device *ublk_get_device(struct ublk_device *ub)
{
	if (kobject_get_unless_zero(&ub->cdev_dev.kobj))
		return ub;
	return NULL;
}

static void ublk_put_device(struct ublk_device *ub)
{
	put_device(&ub->cdev_dev);
}

static inline struct ublk_queue *ublk_get_queue(struct ublk_device *dev,
		int qid)
{
       return (struct ublk_queue *)&(dev->__queues[qid * dev->queue_size]);
}

static inline bool ublk_rq_has_data(const struct request *rq)
{
	return bio_has_data(rq->bio);
}

static inline struct ublksrv_io_desc *ublk_get_iod(struct ublk_queue *ubq,
		int tag)
{
	return (struct ublksrv_io_desc *)
		&(ubq->io_cmd_buf[tag * sizeof(struct ublksrv_io_desc)]);
}

static inline char *ublk_queue_cmd_buf(struct ublk_device *ub, int q_id)
{
	return ublk_get_queue(ub, q_id)->io_cmd_buf;
}

static inline int ublk_queue_cmd_buf_size(struct ublk_device *ub, int q_id)
{
	struct ublk_queue *ubq = ublk_get_queue(ub, q_id);

	return round_up(ubq->q_depth * sizeof(struct ublksrv_io_desc),
			PAGE_SIZE);
}

static inline bool ublk_queue_can_use_recovery_reissue(
		struct ublk_queue *ubq)
{
	return (ubq->flags & UBLK_F_USER_RECOVERY) &&
			(ubq->flags & UBLK_F_USER_RECOVERY_REISSUE);
}

static inline bool ublk_queue_can_use_recovery(
		struct ublk_queue *ubq)
{
	return ubq->flags & UBLK_F_USER_RECOVERY;
}

static inline bool ublk_can_use_recovery(struct ublk_device *ub)
{
	return ub->dev_info.flags & UBLK_F_USER_RECOVERY;
}

static void ublk_free_disk(struct gendisk *disk)
{
	struct ublk_device *ub = disk->private_data;

	clear_bit(UB_STATE_USED, &ub->state);
	put_device(&ub->cdev_dev);
}

static void ublk_store_owner_uid_gid(unsigned int *owner_uid,
		unsigned int *owner_gid)
{
	kuid_t uid;
	kgid_t gid;

	current_uid_gid(&uid, &gid);

	*owner_uid = from_kuid(&init_user_ns, uid);
	*owner_gid = from_kgid(&init_user_ns, gid);
}

static int ublk_open(struct gendisk *disk, blk_mode_t mode)
{
	struct ublk_device *ub = disk->private_data;

	if (capable(CAP_SYS_ADMIN))
		return 0;

	/*
	 * If it is one unprivileged device, only owner can open
	 * the disk. Otherwise it could be one trap made by one
	 * evil user who grants this disk's privileges to other
	 * users deliberately.
	 *
	 * This way is reasonable too given anyone can create
	 * unprivileged device, and no need other's grant.
	 */
	if (ub->dev_info.flags & UBLK_F_UNPRIVILEGED_DEV) {
		unsigned int curr_uid, curr_gid;

		ublk_store_owner_uid_gid(&curr_uid, &curr_gid);

		if (curr_uid != ub->dev_info.owner_uid || curr_gid !=
				ub->dev_info.owner_gid)
			return -EPERM;
	}

	return 0;
}

static const struct block_device_operations ub_fops = {
	.owner =	THIS_MODULE,
	.open =		ublk_open,
	.free_disk =	ublk_free_disk,
	.report_zones =	ublk_report_zones,
};

#define UBLK_MAX_PIN_PAGES	32

struct ublk_io_iter {
	struct page *pages[UBLK_MAX_PIN_PAGES];
	struct bio *bio;
	struct bvec_iter iter;
};

/* return how many pages are copied */
static void ublk_copy_io_pages(struct ublk_io_iter *data,
		size_t total, size_t pg_off, int dir)
{
	unsigned done = 0;
	unsigned pg_idx = 0;

	while (done < total) {
		struct bio_vec bv = bio_iter_iovec(data->bio, data->iter);
		unsigned int bytes = min3(bv.bv_len, (unsigned)total - done,
				(unsigned)(PAGE_SIZE - pg_off));
		void *bv_buf = bvec_kmap_local(&bv);
		void *pg_buf = kmap_local_page(data->pages[pg_idx]);

		if (dir == ITER_DEST)
			memcpy(pg_buf + pg_off, bv_buf, bytes);
		else
			memcpy(bv_buf, pg_buf + pg_off, bytes);

		kunmap_local(pg_buf);
		kunmap_local(bv_buf);

		/* advance page array */
		pg_off += bytes;
		if (pg_off == PAGE_SIZE) {
			pg_idx += 1;
			pg_off = 0;
		}

		done += bytes;

		/* advance bio */
		bio_advance_iter_single(data->bio, &data->iter, bytes);
		if (!data->iter.bi_size) {
			data->bio = data->bio->bi_next;
			if (data->bio == NULL)
				break;
			data->iter = data->bio->bi_iter;
		}
	}
}

static bool ublk_advance_io_iter(const struct request *req,
		struct ublk_io_iter *iter, unsigned int offset)
{
	struct bio *bio = req->bio;

	for_each_bio(bio) {
		if (bio->bi_iter.bi_size > offset) {
			iter->bio = bio;
			iter->iter = bio->bi_iter;
			bio_advance_iter(iter->bio, &iter->iter, offset);
			return true;
		}
		offset -= bio->bi_iter.bi_size;
	}
	return false;
}

/*
 * Copy data between request pages and io_iter, and 'offset'
 * is the start point of linear offset of request.
 */
static size_t ublk_copy_user_pages(const struct request *req,
		unsigned offset, struct iov_iter *uiter, int dir)
{
	struct ublk_io_iter iter;
	size_t done = 0;

	if (!ublk_advance_io_iter(req, &iter, offset))
		return 0;

	while (iov_iter_count(uiter) && iter.bio) {
		unsigned nr_pages;
		ssize_t len;
		size_t off;
		int i;

		len = iov_iter_get_pages2(uiter, iter.pages,
				iov_iter_count(uiter),
				UBLK_MAX_PIN_PAGES, &off);
		if (len <= 0)
			return done;

		ublk_copy_io_pages(&iter, len, off, dir);
		nr_pages = DIV_ROUND_UP(len + off, PAGE_SIZE);
		for (i = 0; i < nr_pages; i++) {
			if (dir == ITER_DEST)
				set_page_dirty(iter.pages[i]);
			put_page(iter.pages[i]);
		}
		done += len;
	}

	return done;
}

static inline bool ublk_need_map_req(const struct request *req)
{
	return ublk_rq_has_data(req) && req_op(req) == REQ_OP_WRITE;
}

static inline bool ublk_need_unmap_req(const struct request *req)
{
	return ublk_rq_has_data(req) &&
	       (req_op(req) == REQ_OP_READ || req_op(req) == REQ_OP_DRV_IN);
}

static int ublk_map_io(const struct ublk_queue *ubq, const struct request *req,
		struct ublk_io *io)
{
	const unsigned int rq_bytes = blk_rq_bytes(req);

	if (ublk_support_user_copy(ubq))
		return rq_bytes;

	/*
	 * no zero copy, we delay copy WRITE request data into ublksrv
	 * context and the big benefit is that pinning pages in current
	 * context is pretty fast, see ublk_pin_user_pages
	 */
	if (ublk_need_map_req(req)) {
		struct iov_iter iter;
		struct iovec iov;
		const int dir = ITER_DEST;

		import_single_range(dir, u64_to_user_ptr(io->addr), rq_bytes,
				&iov, &iter);

		return ublk_copy_user_pages(req, 0, &iter, dir);
	}
	return rq_bytes;
}

static int ublk_unmap_io(const struct ublk_queue *ubq,
		const struct request *req,
		struct ublk_io *io)
{
	const unsigned int rq_bytes = blk_rq_bytes(req);

	if (ublk_support_user_copy(ubq))
		return rq_bytes;

	if (ublk_need_unmap_req(req)) {
		struct iov_iter iter;
		struct iovec iov;
		const int dir = ITER_SOURCE;

		WARN_ON_ONCE(io->res > rq_bytes);

		import_single_range(dir, u64_to_user_ptr(io->addr), io->res,
				&iov, &iter);
		return ublk_copy_user_pages(req, 0, &iter, dir);
	}
	return rq_bytes;
}

static inline unsigned int ublk_req_build_flags(struct request *req)
{
	unsigned flags = 0;

	if (req->cmd_flags & REQ_FAILFAST_DEV)
		flags |= UBLK_IO_F_FAILFAST_DEV;

	if (req->cmd_flags & REQ_FAILFAST_TRANSPORT)
		flags |= UBLK_IO_F_FAILFAST_TRANSPORT;

	if (req->cmd_flags & REQ_FAILFAST_DRIVER)
		flags |= UBLK_IO_F_FAILFAST_DRIVER;

	if (req->cmd_flags & REQ_META)
		flags |= UBLK_IO_F_META;

	if (req->cmd_flags & REQ_FUA)
		flags |= UBLK_IO_F_FUA;

	if (req->cmd_flags & REQ_NOUNMAP)
		flags |= UBLK_IO_F_NOUNMAP;

	if (req->cmd_flags & REQ_SWAP)
		flags |= UBLK_IO_F_SWAP;

	return flags;
}

static blk_status_t ublk_setup_iod(struct ublk_queue *ubq, struct request *req)
{
	struct ublksrv_io_desc *iod = ublk_get_iod(ubq, req->tag);
	struct ublk_io *io = &ubq->ios[req->tag];
	enum req_op op = req_op(req);
	u32 ublk_op;

	if (!ublk_queue_is_zoned(ubq) &&
	    (op_is_zone_mgmt(op) || op == REQ_OP_ZONE_APPEND))
		return BLK_STS_IOERR;

	switch (req_op(req)) {
	case REQ_OP_READ:
		ublk_op = UBLK_IO_OP_READ;
		break;
	case REQ_OP_WRITE:
		ublk_op = UBLK_IO_OP_WRITE;
		break;
	case REQ_OP_FLUSH:
		ublk_op = UBLK_IO_OP_FLUSH;
		break;
	case REQ_OP_DISCARD:
		ublk_op = UBLK_IO_OP_DISCARD;
		break;
	case REQ_OP_WRITE_ZEROES:
		ublk_op = UBLK_IO_OP_WRITE_ZEROES;
		break;
	default:
		if (ublk_queue_is_zoned(ubq))
			return ublk_setup_iod_zoned(ubq, req);
		return BLK_STS_IOERR;
	}

	/* need to translate since kernel may change */
	iod->op_flags = ublk_op | ublk_req_build_flags(req);
	iod->nr_sectors = blk_rq_sectors(req);
	iod->start_sector = blk_rq_pos(req);
	iod->addr = io->addr;

	return BLK_STS_OK;
}

static inline struct ublk_uring_cmd_pdu *ublk_get_uring_cmd_pdu(
		struct io_uring_cmd *ioucmd)
{
	return (struct ublk_uring_cmd_pdu *)&ioucmd->pdu;
}

static inline bool ubq_daemon_is_dying(struct ublk_queue *ubq)
{
	return ubq->ubq_daemon->flags & PF_EXITING;
}

/* todo: handle partial completion */
static inline void __ublk_complete_rq(struct request *req)
{
	struct ublk_queue *ubq = req->mq_hctx->driver_data;
	struct ublk_io *io = &ubq->ios[req->tag];
	unsigned int unmapped_bytes;
	blk_status_t res = BLK_STS_OK;

	/* called from ublk_abort_queue() code path */
	if (io->flags & UBLK_IO_FLAG_ABORTED) {
		res = BLK_STS_IOERR;
		goto exit;
	}

	/* failed read IO if nothing is read */
	if (!io->res && req_op(req) == REQ_OP_READ)
		io->res = -EIO;

	if (io->res < 0) {
		res = errno_to_blk_status(io->res);
		goto exit;
	}

	/*
	 * FLUSH, DISCARD or WRITE_ZEROES usually won't return bytes returned, so end them
	 * directly.
	 *
	 * Both the two needn't unmap.
	 */
	if (req_op(req) != REQ_OP_READ && req_op(req) != REQ_OP_WRITE &&
	    req_op(req) != REQ_OP_DRV_IN)
		goto exit;

	/* for READ request, writing data in iod->addr to rq buffers */
	unmapped_bytes = ublk_unmap_io(ubq, req, io);

	/*
	 * Extremely impossible since we got data filled in just before
	 *
	 * Re-read simply for this unlikely case.
	 */
	if (unlikely(unmapped_bytes < io->res))
		io->res = unmapped_bytes;

	if (blk_update_request(req, BLK_STS_OK, io->res))
		blk_mq_requeue_request(req, true);
	else
		__blk_mq_end_request(req, BLK_STS_OK);

	return;
exit:
	blk_mq_end_request(req, res);
}

static void ublk_complete_rq(struct kref *ref)
{
	struct ublk_rq_data *data = container_of(ref, struct ublk_rq_data,
			ref);
	struct request *req = blk_mq_rq_from_pdu(data);

	__ublk_complete_rq(req);
}

/*
 * Since __ublk_rq_task_work always fails requests immediately during
 * exiting, __ublk_fail_req() is only called from abort context during
 * exiting. So lock is unnecessary.
 *
 * Also aborting may not be started yet, keep in mind that one failed
 * request may be issued by block layer again.
 */
static void __ublk_fail_req(struct ublk_queue *ubq, struct ublk_io *io,
		struct request *req)
{
	WARN_ON_ONCE(io->flags & UBLK_IO_FLAG_ACTIVE);

	if (!(io->flags & UBLK_IO_FLAG_ABORTED)) {
		io->flags |= UBLK_IO_FLAG_ABORTED;
		if (ublk_queue_can_use_recovery_reissue(ubq))
			blk_mq_requeue_request(req, false);
		else
			ublk_put_req_ref(ubq, req);
	}
}

static void ubq_complete_io_cmd(struct ublk_io *io, int res,
				unsigned issue_flags)
{
	/* mark this cmd owned by ublksrv */
	io->flags |= UBLK_IO_FLAG_OWNED_BY_SRV;

	/*
	 * clear ACTIVE since we are done with this sqe/cmd slot
	 * We can only accept io cmd in case of being not active.
	 */
	io->flags &= ~UBLK_IO_FLAG_ACTIVE;

	/* tell ublksrv one io request is coming */
	io_uring_cmd_done(io->cmd, res, 0, issue_flags);
}

#define UBLK_REQUEUE_DELAY_MS	3

static inline void __ublk_abort_rq(struct ublk_queue *ubq,
		struct request *rq)
{
	/* We cannot process this rq so just requeue it. */
	if (ublk_queue_can_use_recovery(ubq))
		blk_mq_requeue_request(rq, false);
	else
		blk_mq_end_request(rq, BLK_STS_IOERR);

	mod_delayed_work(system_wq, &ubq->dev->monitor_work, 0);
}

static inline void __ublk_rq_task_work(struct request *req,
				       unsigned issue_flags)
{
	struct ublk_queue *ubq = req->mq_hctx->driver_data;
	int tag = req->tag;
	struct ublk_io *io = &ubq->ios[tag];
	unsigned int mapped_bytes;

	pr_devel("%s: complete: op %d, qid %d tag %d io_flags %x addr %llx\n",
			__func__, io->cmd->cmd_op, ubq->q_id, req->tag, io->flags,
			ublk_get_iod(ubq, req->tag)->addr);

	/*
	 * Task is exiting if either:
	 *
	 * (1) current != ubq_daemon.
	 * io_uring_cmd_complete_in_task() tries to run task_work
	 * in a workqueue if ubq_daemon(cmd's task) is PF_EXITING.
	 *
	 * (2) current->flags & PF_EXITING.
	 */
	if (unlikely(current != ubq->ubq_daemon || current->flags & PF_EXITING)) {
		__ublk_abort_rq(ubq, req);
		return;
	}

	if (ublk_need_get_data(ubq) && ublk_need_map_req(req)) {
		/*
		 * We have not handled UBLK_IO_NEED_GET_DATA command yet,
		 * so immepdately pass UBLK_IO_RES_NEED_GET_DATA to ublksrv
		 * and notify it.
		 */
		if (!(io->flags & UBLK_IO_FLAG_NEED_GET_DATA)) {
			io->flags |= UBLK_IO_FLAG_NEED_GET_DATA;
			pr_devel("%s: need get data. op %d, qid %d tag %d io_flags %x\n",
					__func__, io->cmd->cmd_op, ubq->q_id,
					req->tag, io->flags);
			ubq_complete_io_cmd(io, UBLK_IO_RES_NEED_GET_DATA, issue_flags);
			return;
		}
		/*
		 * We have handled UBLK_IO_NEED_GET_DATA command,
		 * so clear UBLK_IO_FLAG_NEED_GET_DATA now and just
		 * do the copy work.
		 */
		io->flags &= ~UBLK_IO_FLAG_NEED_GET_DATA;
		/* update iod->addr because ublksrv may have passed a new io buffer */
		ublk_get_iod(ubq, req->tag)->addr = io->addr;
		pr_devel("%s: update iod->addr: op %d, qid %d tag %d io_flags %x addr %llx\n",
				__func__, io->cmd->cmd_op, ubq->q_id, req->tag, io->flags,
				ublk_get_iod(ubq, req->tag)->addr);
	}

	mapped_bytes = ublk_map_io(ubq, req, io);

	/* partially mapped, update io descriptor */
	if (unlikely(mapped_bytes != blk_rq_bytes(req))) {
		/*
		 * Nothing mapped, retry until we succeed.
		 *
		 * We may never succeed in mapping any bytes here because
		 * of OOM. TODO: reserve one buffer with single page pinned
		 * for providing forward progress guarantee.
		 */
		if (unlikely(!mapped_bytes)) {
			blk_mq_requeue_request(req, false);
			blk_mq_delay_kick_requeue_list(req->q,
					UBLK_REQUEUE_DELAY_MS);
			return;
		}

		ublk_get_iod(ubq, req->tag)->nr_sectors =
			mapped_bytes >> 9;
	}

	ublk_init_req_ref(ubq, req);
	ubq_complete_io_cmd(io, UBLK_IO_RES_OK, issue_flags);
}

static inline void ublk_forward_io_cmds(struct ublk_queue *ubq,
					unsigned issue_flags)
{
	struct llist_node *io_cmds = llist_del_all(&ubq->io_cmds);
	struct ublk_rq_data *data, *tmp;

	io_cmds = llist_reverse_order(io_cmds);
	llist_for_each_entry_safe(data, tmp, io_cmds, node)
		__ublk_rq_task_work(blk_mq_rq_from_pdu(data), issue_flags);
}

static inline void ublk_abort_io_cmds(struct ublk_queue *ubq)
{
	struct llist_node *io_cmds = llist_del_all(&ubq->io_cmds);
	struct ublk_rq_data *data, *tmp;

	llist_for_each_entry_safe(data, tmp, io_cmds, node)
		__ublk_abort_rq(ubq, blk_mq_rq_from_pdu(data));
}

static void ublk_rq_task_work_cb(struct io_uring_cmd *cmd, unsigned issue_flags)
{
	struct ublk_uring_cmd_pdu *pdu = ublk_get_uring_cmd_pdu(cmd);
	struct ublk_queue *ubq = pdu->ubq;

	ublk_forward_io_cmds(ubq, issue_flags);
}

static void ublk_queue_cmd(struct ublk_queue *ubq, struct request *rq)
{
	struct ublk_rq_data *data = blk_mq_rq_to_pdu(rq);
	struct ublk_io *io;

	if (!llist_add(&data->node, &ubq->io_cmds))
		return;

	io = &ubq->ios[rq->tag];
	/*
	 * If the check pass, we know that this is a re-issued request aborted
	 * previously in monitor_work because the ubq_daemon(cmd's task) is
	 * PF_EXITING. We cannot call io_uring_cmd_complete_in_task() anymore
	 * because this ioucmd's io_uring context may be freed now if no inflight
	 * ioucmd exists. Otherwise we may cause null-deref in ctx->fallback_work.
	 *
	 * Note: monitor_work sets UBLK_IO_FLAG_ABORTED and ends this request(releasing
	 * the tag). Then the request is re-started(allocating the tag) and we are here.
	 * Since releasing/allocating a tag implies smp_mb(), finding UBLK_IO_FLAG_ABORTED
	 * guarantees that here is a re-issued request aborted previously.
	 */
	if (unlikely(io->flags & UBLK_IO_FLAG_ABORTED)) {
		ublk_abort_io_cmds(ubq);
	} else {
		struct io_uring_cmd *cmd = io->cmd;
		struct ublk_uring_cmd_pdu *pdu = ublk_get_uring_cmd_pdu(cmd);

		pdu->ubq = ubq;
		io_uring_cmd_complete_in_task(cmd, ublk_rq_task_work_cb);
	}
}

static enum blk_eh_timer_return ublk_timeout(struct request *rq)
{
	struct ublk_queue *ubq = rq->mq_hctx->driver_data;

	if (ubq->flags & UBLK_F_UNPRIVILEGED_DEV) {
		if (!ubq->timeout) {
			send_sig(SIGKILL, ubq->ubq_daemon, 0);
			ubq->timeout = true;
		}

		return BLK_EH_DONE;
	}

	return BLK_EH_RESET_TIMER;
}

static blk_status_t ublk_queue_rq(struct blk_mq_hw_ctx *hctx,
		const struct blk_mq_queue_data *bd)
{
	struct ublk_queue *ubq = hctx->driver_data;
	struct request *rq = bd->rq;
	blk_status_t res;

	/* fill iod to slot in io cmd buffer */
	res = ublk_setup_iod(ubq, rq);
	if (unlikely(res != BLK_STS_OK))
		return BLK_STS_IOERR;

	/* With recovery feature enabled, force_abort is set in
	 * ublk_stop_dev() before calling del_gendisk(). We have to
	 * abort all requeued and new rqs here to let del_gendisk()
	 * move on. Besides, we cannot not call io_uring_cmd_complete_in_task()
	 * to avoid UAF on io_uring ctx.
	 *
	 * Note: force_abort is guaranteed to be seen because it is set
	 * before request queue is unqiuesced.
	 */
	if (ublk_queue_can_use_recovery(ubq) && unlikely(ubq->force_abort))
		return BLK_STS_IOERR;

	blk_mq_start_request(bd->rq);

	if (unlikely(ubq_daemon_is_dying(ubq))) {
		__ublk_abort_rq(ubq, rq);
		return BLK_STS_OK;
	}

	ublk_queue_cmd(ubq, rq);

	return BLK_STS_OK;
}

static int ublk_init_hctx(struct blk_mq_hw_ctx *hctx, void *driver_data,
		unsigned int hctx_idx)
{
	struct ublk_device *ub = driver_data;
	struct ublk_queue *ubq = ublk_get_queue(ub, hctx->queue_num);

	hctx->driver_data = ubq;
	return 0;
}

static const struct blk_mq_ops ublk_mq_ops = {
	.queue_rq       = ublk_queue_rq,
	.init_hctx	= ublk_init_hctx,
	.timeout	= ublk_timeout,
};

static int ublk_ch_open(struct inode *inode, struct file *filp)
{
	struct ublk_device *ub = container_of(inode->i_cdev,
			struct ublk_device, cdev);

	if (test_and_set_bit(UB_STATE_OPEN, &ub->state))
		return -EBUSY;
	filp->private_data = ub;
	return 0;
}

static int ublk_ch_release(struct inode *inode, struct file *filp)
{
	struct ublk_device *ub = filp->private_data;

	clear_bit(UB_STATE_OPEN, &ub->state);
	return 0;
}

/* map pre-allocated per-queue cmd buffer to ublksrv daemon */
static int ublk_ch_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct ublk_device *ub = filp->private_data;
	size_t sz = vma->vm_end - vma->vm_start;
	unsigned max_sz = UBLK_MAX_QUEUE_DEPTH * sizeof(struct ublksrv_io_desc);
	unsigned long pfn, end, phys_off = vma->vm_pgoff << PAGE_SHIFT;
	int q_id, ret = 0;

	spin_lock(&ub->mm_lock);
	if (!ub->mm)
		ub->mm = current->mm;
	if (current->mm != ub->mm)
		ret = -EINVAL;
	spin_unlock(&ub->mm_lock);

	if (ret)
		return ret;

	if (vma->vm_flags & VM_WRITE)
		return -EPERM;

	end = UBLKSRV_CMD_BUF_OFFSET + ub->dev_info.nr_hw_queues * max_sz;
	if (phys_off < UBLKSRV_CMD_BUF_OFFSET || phys_off >= end)
		return -EINVAL;

	q_id = (phys_off - UBLKSRV_CMD_BUF_OFFSET) / max_sz;
	pr_devel("%s: qid %d, pid %d, addr %lx pg_off %lx sz %lu\n",
			__func__, q_id, current->pid, vma->vm_start,
			phys_off, (unsigned long)sz);

	if (sz != ublk_queue_cmd_buf_size(ub, q_id))
		return -EINVAL;

	pfn = virt_to_phys(ublk_queue_cmd_buf(ub, q_id)) >> PAGE_SHIFT;
	return remap_pfn_range(vma, vma->vm_start, pfn, sz, vma->vm_page_prot);
}

static void ublk_commit_completion(struct ublk_device *ub,
		const struct ublksrv_io_cmd *ub_cmd)
{
	u32 qid = ub_cmd->q_id, tag = ub_cmd->tag;
	struct ublk_queue *ubq = ublk_get_queue(ub, qid);
	struct ublk_io *io = &ubq->ios[tag];
	struct request *req;

	/* now this cmd slot is owned by nbd driver */
	io->flags &= ~UBLK_IO_FLAG_OWNED_BY_SRV;
	io->res = ub_cmd->result;

	/* find the io request and complete */
	req = blk_mq_tag_to_rq(ub->tag_set.tags[qid], tag);
	if (WARN_ON_ONCE(unlikely(!req)))
		return;

	if (req_op(req) == REQ_OP_ZONE_APPEND)
		req->__sector = ub_cmd->zone_append_lba;

	if (likely(!blk_should_fake_timeout(req->q)))
		ublk_put_req_ref(ubq, req);
}

/*
 * When ->ubq_daemon is exiting, either new request is ended immediately,
 * or any queued io command is drained, so it is safe to abort queue
 * lockless
 */
static void ublk_abort_queue(struct ublk_device *ub, struct ublk_queue *ubq)
{
	int i;

	if (!ublk_get_device(ub))
		return;

	for (i = 0; i < ubq->q_depth; i++) {
		struct ublk_io *io = &ubq->ios[i];

		if (!(io->flags & UBLK_IO_FLAG_ACTIVE)) {
			struct request *rq;

			/*
			 * Either we fail the request or ublk_rq_task_work_fn
			 * will do it
			 */
			rq = blk_mq_tag_to_rq(ub->tag_set.tags[ubq->q_id], i);
			if (rq)
				__ublk_fail_req(ubq, io, rq);
		}
	}
	ublk_put_device(ub);
}

static void ublk_daemon_monitor_work(struct work_struct *work)
{
	struct ublk_device *ub =
		container_of(work, struct ublk_device, monitor_work.work);
	int i;

	for (i = 0; i < ub->dev_info.nr_hw_queues; i++) {
		struct ublk_queue *ubq = ublk_get_queue(ub, i);

		if (ubq_daemon_is_dying(ubq)) {
			if (ublk_queue_can_use_recovery(ubq))
				schedule_work(&ub->quiesce_work);
			else
				schedule_work(&ub->stop_work);

			/* abort queue is for making forward progress */
			ublk_abort_queue(ub, ubq);
		}
	}

	/*
	 * We can't schedule monitor work after ub's state is not UBLK_S_DEV_LIVE.
	 * after ublk_remove() or __ublk_quiesce_dev() is started.
	 *
	 * No need ub->mutex, monitor work are canceled after state is marked
	 * as not LIVE, so new state is observed reliably.
	 */
	if (ub->dev_info.state == UBLK_S_DEV_LIVE)
		schedule_delayed_work(&ub->monitor_work,
				UBLK_DAEMON_MONITOR_PERIOD);
}

static inline bool ublk_queue_ready(struct ublk_queue *ubq)
{
	return ubq->nr_io_ready == ubq->q_depth;
}

static void ublk_cmd_cancel_cb(struct io_uring_cmd *cmd, unsigned issue_flags)
{
	io_uring_cmd_done(cmd, UBLK_IO_RES_ABORT, 0, issue_flags);
}

static void ublk_cancel_queue(struct ublk_queue *ubq)
{
	int i;

	if (!ublk_queue_ready(ubq))
		return;

	for (i = 0; i < ubq->q_depth; i++) {
		struct ublk_io *io = &ubq->ios[i];

		if (io->flags & UBLK_IO_FLAG_ACTIVE)
			io_uring_cmd_complete_in_task(io->cmd,
						      ublk_cmd_cancel_cb);
	}

	/* all io commands are canceled */
	ubq->nr_io_ready = 0;
}

/* Cancel all pending commands, must be called after del_gendisk() returns */
static void ublk_cancel_dev(struct ublk_device *ub)
{
	int i;

	for (i = 0; i < ub->dev_info.nr_hw_queues; i++)
		ublk_cancel_queue(ublk_get_queue(ub, i));
}

static bool ublk_check_inflight_rq(struct request *rq, void *data)
{
	bool *idle = data;

	if (blk_mq_request_started(rq)) {
		*idle = false;
		return false;
	}
	return true;
}

static void ublk_wait_tagset_rqs_idle(struct ublk_device *ub)
{
	bool idle;

	WARN_ON_ONCE(!blk_queue_quiesced(ub->ub_disk->queue));
	while (true) {
		idle = true;
		blk_mq_tagset_busy_iter(&ub->tag_set,
				ublk_check_inflight_rq, &idle);
		if (idle)
			break;
		msleep(UBLK_REQUEUE_DELAY_MS);
	}
}

static void __ublk_quiesce_dev(struct ublk_device *ub)
{
	pr_devel("%s: quiesce ub: dev_id %d state %s\n",
			__func__, ub->dev_info.dev_id,
			ub->dev_info.state == UBLK_S_DEV_LIVE ?
			"LIVE" : "QUIESCED");
	blk_mq_quiesce_queue(ub->ub_disk->queue);
	ublk_wait_tagset_rqs_idle(ub);
	ub->dev_info.state = UBLK_S_DEV_QUIESCED;
	ublk_cancel_dev(ub);
	/* we are going to release task_struct of ubq_daemon and resets
	 * ->ubq_daemon to NULL. So in monitor_work, check on ubq_daemon causes UAF.
	 * Besides, monitor_work is not necessary in QUIESCED state since we have
	 * already scheduled quiesce_work and quiesced all ubqs.
	 *
	 * Do not let monitor_work schedule itself if state it QUIESCED. And we cancel
	 * it here and re-schedule it in END_USER_RECOVERY to avoid UAF.
	 */
	cancel_delayed_work_sync(&ub->monitor_work);
}

static void ublk_quiesce_work_fn(struct work_struct *work)
{
	struct ublk_device *ub =
		container_of(work, struct ublk_device, quiesce_work);

	mutex_lock(&ub->mutex);
	if (ub->dev_info.state != UBLK_S_DEV_LIVE)
		goto unlock;
	__ublk_quiesce_dev(ub);
 unlock:
	mutex_unlock(&ub->mutex);
}

static void ublk_unquiesce_dev(struct ublk_device *ub)
{
	int i;

	pr_devel("%s: unquiesce ub: dev_id %d state %s\n",
			__func__, ub->dev_info.dev_id,
			ub->dev_info.state == UBLK_S_DEV_LIVE ?
			"LIVE" : "QUIESCED");
	/* quiesce_work has run. We let requeued rqs be aborted
	 * before running fallback_wq. "force_abort" must be seen
	 * after request queue is unqiuesced. Then del_gendisk()
	 * can move on.
	 */
	for (i = 0; i < ub->dev_info.nr_hw_queues; i++)
		ublk_get_queue(ub, i)->force_abort = true;

	blk_mq_unquiesce_queue(ub->ub_disk->queue);
	/* We may have requeued some rqs in ublk_quiesce_queue() */
	blk_mq_kick_requeue_list(ub->ub_disk->queue);
}

static void ublk_stop_dev(struct ublk_device *ub)
{
	mutex_lock(&ub->mutex);
	if (ub->dev_info.state == UBLK_S_DEV_DEAD)
		goto unlock;
	if (ublk_can_use_recovery(ub)) {
		if (ub->dev_info.state == UBLK_S_DEV_LIVE)
			__ublk_quiesce_dev(ub);
		ublk_unquiesce_dev(ub);
	}
	del_gendisk(ub->ub_disk);
	ub->dev_info.state = UBLK_S_DEV_DEAD;
	ub->dev_info.ublksrv_pid = -1;
	put_disk(ub->ub_disk);
	ub->ub_disk = NULL;
 unlock:
	ublk_cancel_dev(ub);
	mutex_unlock(&ub->mutex);
	cancel_delayed_work_sync(&ub->monitor_work);
}

/* device can only be started after all IOs are ready */
static void ublk_mark_io_ready(struct ublk_device *ub, struct ublk_queue *ubq)
{
	mutex_lock(&ub->mutex);
	ubq->nr_io_ready++;
	if (ublk_queue_ready(ubq)) {
		ubq->ubq_daemon = current;
		get_task_struct(ubq->ubq_daemon);
		ub->nr_queues_ready++;

		if (capable(CAP_SYS_ADMIN))
			ub->nr_privileged_daemon++;
	}
	if (ub->nr_queues_ready == ub->dev_info.nr_hw_queues)
		complete_all(&ub->completion);
	mutex_unlock(&ub->mutex);
}

static void ublk_handle_need_get_data(struct ublk_device *ub, int q_id,
		int tag)
{
	struct ublk_queue *ubq = ublk_get_queue(ub, q_id);
	struct request *req = blk_mq_tag_to_rq(ub->tag_set.tags[q_id], tag);

	ublk_queue_cmd(ubq, req);
}

static inline int ublk_check_cmd_op(u32 cmd_op)
{
	u32 ioc_type = _IOC_TYPE(cmd_op);

	if (!IS_ENABLED(CONFIG_BLKDEV_UBLK_LEGACY_OPCODES) && ioc_type != 'u')
		return -EOPNOTSUPP;

	if (ioc_type != 'u' && ioc_type != 0)
		return -EOPNOTSUPP;

	return 0;
}

static inline void ublk_fill_io_cmd(struct ublk_io *io,
		struct io_uring_cmd *cmd, unsigned long buf_addr)
{
	io->cmd = cmd;
	io->flags |= UBLK_IO_FLAG_ACTIVE;
	io->addr = buf_addr;
}

static int __ublk_ch_uring_cmd(struct io_uring_cmd *cmd,
			       unsigned int issue_flags,
			       const struct ublksrv_io_cmd *ub_cmd)
{
	struct ublk_device *ub = cmd->file->private_data;
	struct ublk_queue *ubq;
	struct ublk_io *io;
	u32 cmd_op = cmd->cmd_op;
	unsigned tag = ub_cmd->tag;
	int ret = -EINVAL;
	struct request *req;

	pr_devel("%s: received: cmd op %d queue %d tag %d result %d\n",
			__func__, cmd->cmd_op, ub_cmd->q_id, tag,
			ub_cmd->result);

	if (ub_cmd->q_id >= ub->dev_info.nr_hw_queues)
		goto out;

	ubq = ublk_get_queue(ub, ub_cmd->q_id);
	if (!ubq || ub_cmd->q_id != ubq->q_id)
		goto out;

	if (ubq->ubq_daemon && ubq->ubq_daemon != current)
		goto out;

	if (tag >= ubq->q_depth)
		goto out;

	io = &ubq->ios[tag];

	/* there is pending io cmd, something must be wrong */
	if (io->flags & UBLK_IO_FLAG_ACTIVE) {
		ret = -EBUSY;
		goto out;
	}

	/*
	 * ensure that the user issues UBLK_IO_NEED_GET_DATA
	 * iff the driver have set the UBLK_IO_FLAG_NEED_GET_DATA.
	 */
	if ((!!(io->flags & UBLK_IO_FLAG_NEED_GET_DATA))
			^ (_IOC_NR(cmd_op) == UBLK_IO_NEED_GET_DATA))
		goto out;

	ret = ublk_check_cmd_op(cmd_op);
	if (ret)
		goto out;

	ret = -EINVAL;
	switch (_IOC_NR(cmd_op)) {
	case UBLK_IO_FETCH_REQ:
		/* UBLK_IO_FETCH_REQ is only allowed before queue is setup */
		if (ublk_queue_ready(ubq)) {
			ret = -EBUSY;
			goto out;
		}
		/*
		 * The io is being handled by server, so COMMIT_RQ is expected
		 * instead of FETCH_REQ
		 */
		if (io->flags & UBLK_IO_FLAG_OWNED_BY_SRV)
			goto out;

		if (!ublk_support_user_copy(ubq)) {
			/*
			 * FETCH_RQ has to provide IO buffer if NEED GET
			 * DATA is not enabled
			 */
			if (!ub_cmd->addr && !ublk_need_get_data(ubq))
				goto out;
		} else if (ub_cmd->addr) {
			/* User copy requires addr to be unset */
			ret = -EINVAL;
			goto out;
		}

		ublk_fill_io_cmd(io, cmd, ub_cmd->addr);
		ublk_mark_io_ready(ub, ubq);
		break;
	case UBLK_IO_COMMIT_AND_FETCH_REQ:
		req = blk_mq_tag_to_rq(ub->tag_set.tags[ub_cmd->q_id], tag);

		if (!(io->flags & UBLK_IO_FLAG_OWNED_BY_SRV))
			goto out;

		if (!ublk_support_user_copy(ubq)) {
			/*
			 * COMMIT_AND_FETCH_REQ has to provide IO buffer if
			 * NEED GET DATA is not enabled or it is Read IO.
			 */
			if (!ub_cmd->addr && (!ublk_need_get_data(ubq) ||
						req_op(req) == REQ_OP_READ))
				goto out;
		} else if (req_op(req) != REQ_OP_ZONE_APPEND && ub_cmd->addr) {
			/*
			 * User copy requires addr to be unset when command is
			 * not zone append
			 */
			ret = -EINVAL;
			goto out;
		}

		ublk_fill_io_cmd(io, cmd, ub_cmd->addr);
		ublk_commit_completion(ub, ub_cmd);
		break;
	case UBLK_IO_NEED_GET_DATA:
		if (!(io->flags & UBLK_IO_FLAG_OWNED_BY_SRV))
			goto out;
		ublk_fill_io_cmd(io, cmd, ub_cmd->addr);
		ublk_handle_need_get_data(ub, ub_cmd->q_id, ub_cmd->tag);
		break;
	default:
		goto out;
	}
	return -EIOCBQUEUED;

 out:
	io_uring_cmd_done(cmd, ret, 0, issue_flags);
	pr_devel("%s: complete: cmd op %d, tag %d ret %x io_flags %x\n",
			__func__, cmd_op, tag, ret, io->flags);
	return -EIOCBQUEUED;
}

static inline struct request *__ublk_check_and_get_req(struct ublk_device *ub,
		struct ublk_queue *ubq, int tag, size_t offset)
{
	struct request *req;

	if (!ublk_need_req_ref(ubq))
		return NULL;

	req = blk_mq_tag_to_rq(ub->tag_set.tags[ubq->q_id], tag);
	if (!req)
		return NULL;

	if (!ublk_get_req_ref(ubq, req))
		return NULL;

	if (unlikely(!blk_mq_request_started(req) || req->tag != tag))
		goto fail_put;

	if (!ublk_rq_has_data(req))
		goto fail_put;

	if (offset > blk_rq_bytes(req))
		goto fail_put;

	return req;
fail_put:
	ublk_put_req_ref(ubq, req);
	return NULL;
}

static int ublk_ch_uring_cmd(struct io_uring_cmd *cmd, unsigned int issue_flags)
{
	/*
	 * Not necessary for async retry, but let's keep it simple and always
	 * copy the values to avoid any potential reuse.
	 */
	const struct ublksrv_io_cmd *ub_src = io_uring_sqe_cmd(cmd->sqe);
	const struct ublksrv_io_cmd ub_cmd = {
		.q_id = READ_ONCE(ub_src->q_id),
		.tag = READ_ONCE(ub_src->tag),
		.result = READ_ONCE(ub_src->result),
		.addr = READ_ONCE(ub_src->addr)
	};

	return __ublk_ch_uring_cmd(cmd, issue_flags, &ub_cmd);
}

static inline bool ublk_check_ubuf_dir(const struct request *req,
		int ubuf_dir)
{
	/* copy ubuf to request pages */
	if ((req_op(req) == REQ_OP_READ || req_op(req) == REQ_OP_DRV_IN) &&
	    ubuf_dir == ITER_SOURCE)
		return true;

	/* copy request pages to ubuf */
	if ((req_op(req) == REQ_OP_WRITE ||
	     req_op(req) == REQ_OP_ZONE_APPEND) &&
	    ubuf_dir == ITER_DEST)
		return true;

	return false;
}

static struct request *ublk_check_and_get_req(struct kiocb *iocb,
		struct iov_iter *iter, size_t *off, int dir)
{
	struct ublk_device *ub = iocb->ki_filp->private_data;
	struct ublk_queue *ubq;
	struct request *req;
	size_t buf_off;
	u16 tag, q_id;

	if (!ub)
		return ERR_PTR(-EACCES);

	if (!user_backed_iter(iter))
		return ERR_PTR(-EACCES);

	if (ub->dev_info.state == UBLK_S_DEV_DEAD)
		return ERR_PTR(-EACCES);

	tag = ublk_pos_to_tag(iocb->ki_pos);
	q_id = ublk_pos_to_hwq(iocb->ki_pos);
	buf_off = ublk_pos_to_buf_off(iocb->ki_pos);

	if (q_id >= ub->dev_info.nr_hw_queues)
		return ERR_PTR(-EINVAL);

	ubq = ublk_get_queue(ub, q_id);
	if (!ubq)
		return ERR_PTR(-EINVAL);

	if (tag >= ubq->q_depth)
		return ERR_PTR(-EINVAL);

	req = __ublk_check_and_get_req(ub, ubq, tag, buf_off);
	if (!req)
		return ERR_PTR(-EINVAL);

	if (!req->mq_hctx || !req->mq_hctx->driver_data)
		goto fail;

	if (!ublk_check_ubuf_dir(req, dir))
		goto fail;

	*off = buf_off;
	return req;
fail:
	ublk_put_req_ref(ubq, req);
	return ERR_PTR(-EACCES);
}

static ssize_t ublk_ch_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct ublk_queue *ubq;
	struct request *req;
	size_t buf_off;
	size_t ret;

	req = ublk_check_and_get_req(iocb, to, &buf_off, ITER_DEST);
	if (IS_ERR(req))
		return PTR_ERR(req);

	ret = ublk_copy_user_pages(req, buf_off, to, ITER_DEST);
	ubq = req->mq_hctx->driver_data;
	ublk_put_req_ref(ubq, req);

	return ret;
}

static ssize_t ublk_ch_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct ublk_queue *ubq;
	struct request *req;
	size_t buf_off;
	size_t ret;

	req = ublk_check_and_get_req(iocb, from, &buf_off, ITER_SOURCE);
	if (IS_ERR(req))
		return PTR_ERR(req);

	ret = ublk_copy_user_pages(req, buf_off, from, ITER_SOURCE);
	ubq = req->mq_hctx->driver_data;
	ublk_put_req_ref(ubq, req);

	return ret;
}

static const struct file_operations ublk_ch_fops = {
	.owner = THIS_MODULE,
	.open = ublk_ch_open,
	.release = ublk_ch_release,
	.llseek = no_llseek,
	.read_iter = ublk_ch_read_iter,
	.write_iter = ublk_ch_write_iter,
	.uring_cmd = ublk_ch_uring_cmd,
	.mmap = ublk_ch_mmap,
};

static void ublk_deinit_queue(struct ublk_device *ub, int q_id)
{
	int size = ublk_queue_cmd_buf_size(ub, q_id);
	struct ublk_queue *ubq = ublk_get_queue(ub, q_id);

	if (ubq->ubq_daemon)
		put_task_struct(ubq->ubq_daemon);
	if (ubq->io_cmd_buf)
		free_pages((unsigned long)ubq->io_cmd_buf, get_order(size));
}

static int ublk_init_queue(struct ublk_device *ub, int q_id)
{
	struct ublk_queue *ubq = ublk_get_queue(ub, q_id);
	gfp_t gfp_flags = GFP_KERNEL | __GFP_ZERO;
	void *ptr;
	int size;

	ubq->flags = ub->dev_info.flags;
	ubq->q_id = q_id;
	ubq->q_depth = ub->dev_info.queue_depth;
	size = ublk_queue_cmd_buf_size(ub, q_id);

	ptr = (void *) __get_free_pages(gfp_flags, get_order(size));
	if (!ptr)
		return -ENOMEM;

	ubq->io_cmd_buf = ptr;
	ubq->dev = ub;
	return 0;
}

static void ublk_deinit_queues(struct ublk_device *ub)
{
	int nr_queues = ub->dev_info.nr_hw_queues;
	int i;

	if (!ub->__queues)
		return;

	for (i = 0; i < nr_queues; i++)
		ublk_deinit_queue(ub, i);
	kfree(ub->__queues);
}

static int ublk_init_queues(struct ublk_device *ub)
{
	int nr_queues = ub->dev_info.nr_hw_queues;
	int depth = ub->dev_info.queue_depth;
	int ubq_size = sizeof(struct ublk_queue) + depth * sizeof(struct ublk_io);
	int i, ret = -ENOMEM;

	ub->queue_size = ubq_size;
	ub->__queues = kcalloc(nr_queues, ubq_size, GFP_KERNEL);
	if (!ub->__queues)
		return ret;

	for (i = 0; i < nr_queues; i++) {
		if (ublk_init_queue(ub, i))
			goto fail;
	}

	init_completion(&ub->completion);
	return 0;

 fail:
	ublk_deinit_queues(ub);
	return ret;
}

static int ublk_alloc_dev_number(struct ublk_device *ub, int idx)
{
	int i = idx;
	int err;

	spin_lock(&ublk_idr_lock);
	/* allocate id, if @id >= 0, we're requesting that specific id */
	if (i >= 0) {
		err = idr_alloc(&ublk_index_idr, ub, i, i + 1, GFP_NOWAIT);
		if (err == -ENOSPC)
			err = -EEXIST;
	} else {
		err = idr_alloc(&ublk_index_idr, ub, 0, 0, GFP_NOWAIT);
	}
	spin_unlock(&ublk_idr_lock);

	if (err >= 0)
		ub->ub_number = err;

	return err;
}

static void ublk_free_dev_number(struct ublk_device *ub)
{
	spin_lock(&ublk_idr_lock);
	idr_remove(&ublk_index_idr, ub->ub_number);
	wake_up_all(&ublk_idr_wq);
	spin_unlock(&ublk_idr_lock);
}

static void ublk_cdev_rel(struct device *dev)
{
	struct ublk_device *ub = container_of(dev, struct ublk_device, cdev_dev);

	blk_mq_free_tag_set(&ub->tag_set);
	ublk_deinit_queues(ub);
	ublk_free_dev_number(ub);
	mutex_destroy(&ub->mutex);
	kfree(ub);
}

static int ublk_add_chdev(struct ublk_device *ub)
{
	struct device *dev = &ub->cdev_dev;
	int minor = ub->ub_number;
	int ret;

	dev->parent = ublk_misc.this_device;
	dev->devt = MKDEV(MAJOR(ublk_chr_devt), minor);
	dev->class = &ublk_chr_class;
	dev->release = ublk_cdev_rel;
	device_initialize(dev);

	ret = dev_set_name(dev, "ublkc%d", minor);
	if (ret)
		goto fail;

	cdev_init(&ub->cdev, &ublk_ch_fops);
	ret = cdev_device_add(&ub->cdev, dev);
	if (ret)
		goto fail;

	ublks_added++;
	return 0;
 fail:
	put_device(dev);
	return ret;
}

static void ublk_stop_work_fn(struct work_struct *work)
{
	struct ublk_device *ub =
		container_of(work, struct ublk_device, stop_work);

	ublk_stop_dev(ub);
}

/* align max io buffer size with PAGE_SIZE */
static void ublk_align_max_io_size(struct ublk_device *ub)
{
	unsigned int max_io_bytes = ub->dev_info.max_io_buf_bytes;

	ub->dev_info.max_io_buf_bytes =
		round_down(max_io_bytes, PAGE_SIZE);
}

static int ublk_add_tag_set(struct ublk_device *ub)
{
	ub->tag_set.ops = &ublk_mq_ops;
	ub->tag_set.nr_hw_queues = ub->dev_info.nr_hw_queues;
	ub->tag_set.queue_depth = ub->dev_info.queue_depth;
	ub->tag_set.numa_node = NUMA_NO_NODE;
	ub->tag_set.cmd_size = sizeof(struct ublk_rq_data);
	ub->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
	ub->tag_set.driver_data = ub;
	return blk_mq_alloc_tag_set(&ub->tag_set);
}

static void ublk_remove(struct ublk_device *ub)
{
	ublk_stop_dev(ub);
	cancel_work_sync(&ub->stop_work);
	cancel_work_sync(&ub->quiesce_work);
	cdev_device_del(&ub->cdev, &ub->cdev_dev);
	put_device(&ub->cdev_dev);
	ublks_added--;
}

static struct ublk_device *ublk_get_device_from_id(int idx)
{
	struct ublk_device *ub = NULL;

	if (idx < 0)
		return NULL;

	spin_lock(&ublk_idr_lock);
	ub = idr_find(&ublk_index_idr, idx);
	if (ub)
		ub = ublk_get_device(ub);
	spin_unlock(&ublk_idr_lock);

	return ub;
}

static int ublk_ctrl_start_dev(struct ublk_device *ub, struct io_uring_cmd *cmd)
{
	const struct ublksrv_ctrl_cmd *header = io_uring_sqe_cmd(cmd->sqe);
	int ublksrv_pid = (int)header->data[0];
	struct gendisk *disk;
	int ret = -EINVAL;

	if (ublksrv_pid <= 0)
		return -EINVAL;

	if (wait_for_completion_interruptible(&ub->completion) != 0)
		return -EINTR;

	schedule_delayed_work(&ub->monitor_work, UBLK_DAEMON_MONITOR_PERIOD);

	mutex_lock(&ub->mutex);
	if (ub->dev_info.state == UBLK_S_DEV_LIVE ||
	    test_bit(UB_STATE_USED, &ub->state)) {
		ret = -EEXIST;
		goto out_unlock;
	}

	disk = blk_mq_alloc_disk(&ub->tag_set, NULL);
	if (IS_ERR(disk)) {
		ret = PTR_ERR(disk);
		goto out_unlock;
	}
	sprintf(disk->disk_name, "ublkb%d", ub->ub_number);
	disk->fops = &ub_fops;
	disk->private_data = ub;

	ub->dev_info.ublksrv_pid = ublksrv_pid;
	ub->ub_disk = disk;

	ret = ublk_apply_params(ub);
	if (ret)
		goto out_put_disk;

	/* don't probe partitions if any one ubq daemon is un-trusted */
	if (ub->nr_privileged_daemon != ub->nr_queues_ready)
		set_bit(GD_SUPPRESS_PART_SCAN, &disk->state);

	get_device(&ub->cdev_dev);
	ub->dev_info.state = UBLK_S_DEV_LIVE;

	if (ublk_dev_is_zoned(ub)) {
		ret = ublk_revalidate_disk_zones(ub);
		if (ret)
			goto out_put_cdev;
	}

	ret = add_disk(disk);
	if (ret)
		goto out_put_cdev;

	set_bit(UB_STATE_USED, &ub->state);

out_put_cdev:
	if (ret) {
		ub->dev_info.state = UBLK_S_DEV_DEAD;
		ublk_put_device(ub);
	}
out_put_disk:
	if (ret)
		put_disk(disk);
out_unlock:
	mutex_unlock(&ub->mutex);
	return ret;
}

static int ublk_ctrl_get_queue_affinity(struct ublk_device *ub,
		struct io_uring_cmd *cmd)
{
	const struct ublksrv_ctrl_cmd *header = io_uring_sqe_cmd(cmd->sqe);
	void __user *argp = (void __user *)(unsigned long)header->addr;
	cpumask_var_t cpumask;
	unsigned long queue;
	unsigned int retlen;
	unsigned int i;
	int ret;

	if (header->len * BITS_PER_BYTE < nr_cpu_ids)
		return -EINVAL;
	if (header->len & (sizeof(unsigned long)-1))
		return -EINVAL;
	if (!header->addr)
		return -EINVAL;

	queue = header->data[0];
	if (queue >= ub->dev_info.nr_hw_queues)
		return -EINVAL;

	if (!zalloc_cpumask_var(&cpumask, GFP_KERNEL))
		return -ENOMEM;

	for_each_possible_cpu(i) {
		if (ub->tag_set.map[HCTX_TYPE_DEFAULT].mq_map[i] == queue)
			cpumask_set_cpu(i, cpumask);
	}

	ret = -EFAULT;
	retlen = min_t(unsigned short, header->len, cpumask_size());
	if (copy_to_user(argp, cpumask, retlen))
		goto out_free_cpumask;
	if (retlen != header->len &&
	    clear_user(argp + retlen, header->len - retlen))
		goto out_free_cpumask;

	ret = 0;
out_free_cpumask:
	free_cpumask_var(cpumask);
	return ret;
}

static inline void ublk_dump_dev_info(struct ublksrv_ctrl_dev_info *info)
{
	pr_devel("%s: dev id %d flags %llx\n", __func__,
			info->dev_id, info->flags);
	pr_devel("\t nr_hw_queues %d queue_depth %d\n",
			info->nr_hw_queues, info->queue_depth);
}

static int ublk_ctrl_add_dev(struct io_uring_cmd *cmd)
{
	const struct ublksrv_ctrl_cmd *header = io_uring_sqe_cmd(cmd->sqe);
	void __user *argp = (void __user *)(unsigned long)header->addr;
	struct ublksrv_ctrl_dev_info info;
	struct ublk_device *ub;
	int ret = -EINVAL;

	if (header->len < sizeof(info) || !header->addr)
		return -EINVAL;
	if (header->queue_id != (u16)-1) {
		pr_warn("%s: queue_id is wrong %x\n",
			__func__, header->queue_id);
		return -EINVAL;
	}

	if (copy_from_user(&info, argp, sizeof(info)))
		return -EFAULT;

	if (capable(CAP_SYS_ADMIN))
		info.flags &= ~UBLK_F_UNPRIVILEGED_DEV;
	else if (!(info.flags & UBLK_F_UNPRIVILEGED_DEV))
		return -EPERM;

	/*
	 * unprivileged device can't be trusted, but RECOVERY and
	 * RECOVERY_REISSUE still may hang error handling, so can't
	 * support recovery features for unprivileged ublk now
	 *
	 * TODO: provide forward progress for RECOVERY handler, so that
	 * unprivileged device can benefit from it
	 */
	if (info.flags & UBLK_F_UNPRIVILEGED_DEV)
		info.flags &= ~(UBLK_F_USER_RECOVERY_REISSUE |
				UBLK_F_USER_RECOVERY);

	/* the created device is always owned by current user */
	ublk_store_owner_uid_gid(&info.owner_uid, &info.owner_gid);

	if (header->dev_id != info.dev_id) {
		pr_warn("%s: dev id not match %u %u\n",
			__func__, header->dev_id, info.dev_id);
		return -EINVAL;
	}

	ublk_dump_dev_info(&info);

	ret = mutex_lock_killable(&ublk_ctl_mutex);
	if (ret)
		return ret;

	ret = -EACCES;
	if (ublks_added >= ublks_max)
		goto out_unlock;

	ret = -ENOMEM;
	ub = kzalloc(sizeof(*ub), GFP_KERNEL);
	if (!ub)
		goto out_unlock;
	mutex_init(&ub->mutex);
	spin_lock_init(&ub->mm_lock);
	INIT_WORK(&ub->quiesce_work, ublk_quiesce_work_fn);
	INIT_WORK(&ub->stop_work, ublk_stop_work_fn);
	INIT_DELAYED_WORK(&ub->monitor_work, ublk_daemon_monitor_work);

	ret = ublk_alloc_dev_number(ub, header->dev_id);
	if (ret < 0)
		goto out_free_ub;

	memcpy(&ub->dev_info, &info, sizeof(info));

	/* update device id */
	ub->dev_info.dev_id = ub->ub_number;

	/*
	 * 64bit flags will be copied back to userspace as feature
	 * negotiation result, so have to clear flags which driver
	 * doesn't support yet, then userspace can get correct flags
	 * (features) to handle.
	 */
	ub->dev_info.flags &= UBLK_F_ALL;

	ub->dev_info.flags |= UBLK_F_CMD_IOCTL_ENCODE |
		UBLK_F_URING_CMD_COMP_IN_TASK;

	/* GET_DATA isn't needed any more with USER_COPY */
	if (ublk_dev_is_user_copy(ub))
		ub->dev_info.flags &= ~UBLK_F_NEED_GET_DATA;

	/* Zoned storage support requires user copy feature */
	if (ublk_dev_is_zoned(ub) &&
	    (!IS_ENABLED(CONFIG_BLK_DEV_ZONED) || !ublk_dev_is_user_copy(ub))) {
		ret = -EINVAL;
		goto out_free_dev_number;
	}

	/* We are not ready to support zero copy */
	ub->dev_info.flags &= ~UBLK_F_SUPPORT_ZERO_COPY;

	ub->dev_info.nr_hw_queues = min_t(unsigned int,
			ub->dev_info.nr_hw_queues, nr_cpu_ids);
	ublk_align_max_io_size(ub);

	ret = ublk_init_queues(ub);
	if (ret)
		goto out_free_dev_number;

	ret = ublk_add_tag_set(ub);
	if (ret)
		goto out_deinit_queues;

	ret = -EFAULT;
	if (copy_to_user(argp, &ub->dev_info, sizeof(info)))
		goto out_free_tag_set;

	/*
	 * Add the char dev so that ublksrv daemon can be setup.
	 * ublk_add_chdev() will cleanup everything if it fails.
	 */
	ret = ublk_add_chdev(ub);
	goto out_unlock;

out_free_tag_set:
	blk_mq_free_tag_set(&ub->tag_set);
out_deinit_queues:
	ublk_deinit_queues(ub);
out_free_dev_number:
	ublk_free_dev_number(ub);
out_free_ub:
	mutex_destroy(&ub->mutex);
	kfree(ub);
out_unlock:
	mutex_unlock(&ublk_ctl_mutex);
	return ret;
}

static inline bool ublk_idr_freed(int id)
{
	void *ptr;

	spin_lock(&ublk_idr_lock);
	ptr = idr_find(&ublk_index_idr, id);
	spin_unlock(&ublk_idr_lock);

	return ptr == NULL;
}

static int ublk_ctrl_del_dev(struct ublk_device **p_ub)
{
	struct ublk_device *ub = *p_ub;
	int idx = ub->ub_number;
	int ret;

	ret = mutex_lock_killable(&ublk_ctl_mutex);
	if (ret)
		return ret;

	if (!test_bit(UB_STATE_DELETED, &ub->state)) {
		ublk_remove(ub);
		set_bit(UB_STATE_DELETED, &ub->state);
	}

	/* Mark the reference as consumed */
	*p_ub = NULL;
	ublk_put_device(ub);
	mutex_unlock(&ublk_ctl_mutex);

	/*
	 * Wait until the idr is removed, then it can be reused after
	 * DEL_DEV command is returned.
	 *
	 * If we returns because of user interrupt, future delete command
	 * may come:
	 *
	 * - the device number isn't freed, this device won't or needn't
	 *   be deleted again, since UB_STATE_DELETED is set, and device
	 *   will be released after the last reference is dropped
	 *
	 * - the device number is freed already, we will not find this
	 *   device via ublk_get_device_from_id()
	 */
	if (wait_event_interruptible(ublk_idr_wq, ublk_idr_freed(idx)))
		return -EINTR;
	return 0;
}

static inline void ublk_ctrl_cmd_dump(struct io_uring_cmd *cmd)
{
	const struct ublksrv_ctrl_cmd *header = io_uring_sqe_cmd(cmd->sqe);

	pr_devel("%s: cmd_op %x, dev id %d qid %d data %llx buf %llx len %u\n",
			__func__, cmd->cmd_op, header->dev_id, header->queue_id,
			header->data[0], header->addr, header->len);
}

static int ublk_ctrl_stop_dev(struct ublk_device *ub)
{
	ublk_stop_dev(ub);
	cancel_work_sync(&ub->stop_work);
	cancel_work_sync(&ub->quiesce_work);

	return 0;
}

static int ublk_ctrl_get_dev_info(struct ublk_device *ub,
		struct io_uring_cmd *cmd)
{
	const struct ublksrv_ctrl_cmd *header = io_uring_sqe_cmd(cmd->sqe);
	void __user *argp = (void __user *)(unsigned long)header->addr;

	if (header->len < sizeof(struct ublksrv_ctrl_dev_info) || !header->addr)
		return -EINVAL;

	if (copy_to_user(argp, &ub->dev_info, sizeof(ub->dev_info)))
		return -EFAULT;

	return 0;
}

/* TYPE_DEVT is readonly, so fill it up before returning to userspace */
static void ublk_ctrl_fill_params_devt(struct ublk_device *ub)
{
	ub->params.devt.char_major = MAJOR(ub->cdev_dev.devt);
	ub->params.devt.char_minor = MINOR(ub->cdev_dev.devt);

	if (ub->ub_disk) {
		ub->params.devt.disk_major = MAJOR(disk_devt(ub->ub_disk));
		ub->params.devt.disk_minor = MINOR(disk_devt(ub->ub_disk));
	} else {
		ub->params.devt.disk_major = 0;
		ub->params.devt.disk_minor = 0;
	}
	ub->params.types |= UBLK_PARAM_TYPE_DEVT;
}

static int ublk_ctrl_get_params(struct ublk_device *ub,
		struct io_uring_cmd *cmd)
{
	const struct ublksrv_ctrl_cmd *header = io_uring_sqe_cmd(cmd->sqe);
	void __user *argp = (void __user *)(unsigned long)header->addr;
	struct ublk_params_header ph;
	int ret;

	if (header->len <= sizeof(ph) || !header->addr)
		return -EINVAL;

	if (copy_from_user(&ph, argp, sizeof(ph)))
		return -EFAULT;

	if (ph.len > header->len || !ph.len)
		return -EINVAL;

	if (ph.len > sizeof(struct ublk_params))
		ph.len = sizeof(struct ublk_params);

	mutex_lock(&ub->mutex);
	ublk_ctrl_fill_params_devt(ub);
	if (copy_to_user(argp, &ub->params, ph.len))
		ret = -EFAULT;
	else
		ret = 0;
	mutex_unlock(&ub->mutex);

	return ret;
}

static int ublk_ctrl_set_params(struct ublk_device *ub,
		struct io_uring_cmd *cmd)
{
	const struct ublksrv_ctrl_cmd *header = io_uring_sqe_cmd(cmd->sqe);
	void __user *argp = (void __user *)(unsigned long)header->addr;
	struct ublk_params_header ph;
	int ret = -EFAULT;

	if (header->len <= sizeof(ph) || !header->addr)
		return -EINVAL;

	if (copy_from_user(&ph, argp, sizeof(ph)))
		return -EFAULT;

	if (ph.len > header->len || !ph.len || !ph.types)
		return -EINVAL;

	if (ph.len > sizeof(struct ublk_params))
		ph.len = sizeof(struct ublk_params);

	/* parameters can only be changed when device isn't live */
	mutex_lock(&ub->mutex);
	if (ub->dev_info.state == UBLK_S_DEV_LIVE) {
		ret = -EACCES;
	} else if (copy_from_user(&ub->params, argp, ph.len)) {
		ret = -EFAULT;
	} else {
		/* clear all we don't support yet */
		ub->params.types &= UBLK_PARAM_TYPE_ALL;
		ret = ublk_validate_params(ub);
		if (ret)
			ub->params.types = 0;
	}
	mutex_unlock(&ub->mutex);

	return ret;
}

static void ublk_queue_reinit(struct ublk_device *ub, struct ublk_queue *ubq)
{
	int i;

	WARN_ON_ONCE(!(ubq->ubq_daemon && ubq_daemon_is_dying(ubq)));
	/* All old ioucmds have to be completed */
	WARN_ON_ONCE(ubq->nr_io_ready);
	/* old daemon is PF_EXITING, put it now */
	put_task_struct(ubq->ubq_daemon);
	/* We have to reset it to NULL, otherwise ub won't accept new FETCH_REQ */
	ubq->ubq_daemon = NULL;
	ubq->timeout = false;

	for (i = 0; i < ubq->q_depth; i++) {
		struct ublk_io *io = &ubq->ios[i];

		/* forget everything now and be ready for new FETCH_REQ */
		io->flags = 0;
		io->cmd = NULL;
		io->addr = 0;
	}
}

static int ublk_ctrl_start_recovery(struct ublk_device *ub,
		struct io_uring_cmd *cmd)
{
	const struct ublksrv_ctrl_cmd *header = io_uring_sqe_cmd(cmd->sqe);
	int ret = -EINVAL;
	int i;

	mutex_lock(&ub->mutex);
	if (!ublk_can_use_recovery(ub))
		goto out_unlock;
	/*
	 * START_RECOVERY is only allowd after:
	 *
	 * (1) UB_STATE_OPEN is not set, which means the dying process is exited
	 *     and related io_uring ctx is freed so file struct of /dev/ublkcX is
	 *     released.
	 *
	 * (2) UBLK_S_DEV_QUIESCED is set, which means the quiesce_work:
	 *     (a)has quiesced request queue
	 *     (b)has requeued every inflight rqs whose io_flags is ACTIVE
	 *     (c)has requeued/aborted every inflight rqs whose io_flags is NOT ACTIVE
	 *     (d)has completed/camceled all ioucmds owned by ther dying process
	 */
	if (test_bit(UB_STATE_OPEN, &ub->state) ||
			ub->dev_info.state != UBLK_S_DEV_QUIESCED) {
		ret = -EBUSY;
		goto out_unlock;
	}
	pr_devel("%s: start recovery for dev id %d.\n", __func__, header->dev_id);
	for (i = 0; i < ub->dev_info.nr_hw_queues; i++)
		ublk_queue_reinit(ub, ublk_get_queue(ub, i));
	/* set to NULL, otherwise new ubq_daemon cannot mmap the io_cmd_buf */
	ub->mm = NULL;
	ub->nr_queues_ready = 0;
	ub->nr_privileged_daemon = 0;
	init_completion(&ub->completion);
	ret = 0;
 out_unlock:
	mutex_unlock(&ub->mutex);
	return ret;
}

static int ublk_ctrl_end_recovery(struct ublk_device *ub,
		struct io_uring_cmd *cmd)
{
	const struct ublksrv_ctrl_cmd *header = io_uring_sqe_cmd(cmd->sqe);
	int ublksrv_pid = (int)header->data[0];
	int ret = -EINVAL;

	pr_devel("%s: Waiting for new ubq_daemons(nr: %d) are ready, dev id %d...\n",
			__func__, ub->dev_info.nr_hw_queues, header->dev_id);
	/* wait until new ubq_daemon sending all FETCH_REQ */
	if (wait_for_completion_interruptible(&ub->completion))
		return -EINTR;

	pr_devel("%s: All new ubq_daemons(nr: %d) are ready, dev id %d\n",
			__func__, ub->dev_info.nr_hw_queues, header->dev_id);

	mutex_lock(&ub->mutex);
	if (!ublk_can_use_recovery(ub))
		goto out_unlock;

	if (ub->dev_info.state != UBLK_S_DEV_QUIESCED) {
		ret = -EBUSY;
		goto out_unlock;
	}
	ub->dev_info.ublksrv_pid = ublksrv_pid;
	pr_devel("%s: new ublksrv_pid %d, dev id %d\n",
			__func__, ublksrv_pid, header->dev_id);
	blk_mq_unquiesce_queue(ub->ub_disk->queue);
	pr_devel("%s: queue unquiesced, dev id %d.\n",
			__func__, header->dev_id);
	blk_mq_kick_requeue_list(ub->ub_disk->queue);
	ub->dev_info.state = UBLK_S_DEV_LIVE;
	schedule_delayed_work(&ub->monitor_work, UBLK_DAEMON_MONITOR_PERIOD);
	ret = 0;
 out_unlock:
	mutex_unlock(&ub->mutex);
	return ret;
}

static int ublk_ctrl_get_features(struct io_uring_cmd *cmd)
{
	const struct ublksrv_ctrl_cmd *header = io_uring_sqe_cmd(cmd->sqe);
	void __user *argp = (void __user *)(unsigned long)header->addr;
	u64 features = UBLK_F_ALL & ~UBLK_F_SUPPORT_ZERO_COPY;

	if (header->len != UBLK_FEATURES_LEN || !header->addr)
		return -EINVAL;

	if (copy_to_user(argp, &features, UBLK_FEATURES_LEN))
		return -EFAULT;

	return 0;
}

/*
 * All control commands are sent via /dev/ublk-control, so we have to check
 * the destination device's permission
 */
static int ublk_char_dev_permission(struct ublk_device *ub,
		const char *dev_path, int mask)
{
	int err;
	struct path path;
	struct kstat stat;

	err = kern_path(dev_path, LOOKUP_FOLLOW, &path);
	if (err)
		return err;

	err = vfs_getattr(&path, &stat, STATX_TYPE, AT_STATX_SYNC_AS_STAT);
	if (err)
		goto exit;

	err = -EPERM;
	if (stat.rdev != ub->cdev_dev.devt || !S_ISCHR(stat.mode))
		goto exit;

	err = inode_permission(&nop_mnt_idmap,
			d_backing_inode(path.dentry), mask);
exit:
	path_put(&path);
	return err;
}

static int ublk_ctrl_uring_cmd_permission(struct ublk_device *ub,
		struct io_uring_cmd *cmd)
{
	struct ublksrv_ctrl_cmd *header = (struct ublksrv_ctrl_cmd *)io_uring_sqe_cmd(cmd->sqe);
	bool unprivileged = ub->dev_info.flags & UBLK_F_UNPRIVILEGED_DEV;
	void __user *argp = (void __user *)(unsigned long)header->addr;
	char *dev_path = NULL;
	int ret = 0;
	int mask;

	if (!unprivileged) {
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		/*
		 * The new added command of UBLK_CMD_GET_DEV_INFO2 includes
		 * char_dev_path in payload too, since userspace may not
		 * know if the specified device is created as unprivileged
		 * mode.
		 */
		if (_IOC_NR(cmd->cmd_op) != UBLK_CMD_GET_DEV_INFO2)
			return 0;
	}

	/*
	 * User has to provide the char device path for unprivileged ublk
	 *
	 * header->addr always points to the dev path buffer, and
	 * header->dev_path_len records length of dev path buffer.
	 */
	if (!header->dev_path_len || header->dev_path_len > PATH_MAX)
		return -EINVAL;

	if (header->len < header->dev_path_len)
		return -EINVAL;

	dev_path = memdup_user_nul(argp, header->dev_path_len);
	if (IS_ERR(dev_path))
		return PTR_ERR(dev_path);

	ret = -EINVAL;
	switch (_IOC_NR(cmd->cmd_op)) {
	case UBLK_CMD_GET_DEV_INFO:
	case UBLK_CMD_GET_DEV_INFO2:
	case UBLK_CMD_GET_QUEUE_AFFINITY:
	case UBLK_CMD_GET_PARAMS:
	case (_IOC_NR(UBLK_U_CMD_GET_FEATURES)):
		mask = MAY_READ;
		break;
	case UBLK_CMD_START_DEV:
	case UBLK_CMD_STOP_DEV:
	case UBLK_CMD_ADD_DEV:
	case UBLK_CMD_DEL_DEV:
	case UBLK_CMD_SET_PARAMS:
	case UBLK_CMD_START_USER_RECOVERY:
	case UBLK_CMD_END_USER_RECOVERY:
		mask = MAY_READ | MAY_WRITE;
		break;
	default:
		goto exit;
	}

	ret = ublk_char_dev_permission(ub, dev_path, mask);
	if (!ret) {
		header->len -= header->dev_path_len;
		header->addr += header->dev_path_len;
	}
	pr_devel("%s: dev id %d cmd_op %x uid %d gid %d path %s ret %d\n",
			__func__, ub->ub_number, cmd->cmd_op,
			ub->dev_info.owner_uid, ub->dev_info.owner_gid,
			dev_path, ret);
exit:
	kfree(dev_path);
	return ret;
}

static int ublk_ctrl_uring_cmd(struct io_uring_cmd *cmd,
		unsigned int issue_flags)
{
	const struct ublksrv_ctrl_cmd *header = io_uring_sqe_cmd(cmd->sqe);
	struct ublk_device *ub = NULL;
	u32 cmd_op = cmd->cmd_op;
	int ret = -EINVAL;

	if (issue_flags & IO_URING_F_NONBLOCK)
		return -EAGAIN;

	ublk_ctrl_cmd_dump(cmd);

	if (!(issue_flags & IO_URING_F_SQE128))
		goto out;

	ret = ublk_check_cmd_op(cmd_op);
	if (ret)
		goto out;

	if (cmd_op == UBLK_U_CMD_GET_FEATURES) {
		ret = ublk_ctrl_get_features(cmd);
		goto out;
	}

	if (_IOC_NR(cmd_op) != UBLK_CMD_ADD_DEV) {
		ret = -ENODEV;
		ub = ublk_get_device_from_id(header->dev_id);
		if (!ub)
			goto out;

		ret = ublk_ctrl_uring_cmd_permission(ub, cmd);
		if (ret)
			goto put_dev;
	}

	switch (_IOC_NR(cmd_op)) {
	case UBLK_CMD_START_DEV:
		ret = ublk_ctrl_start_dev(ub, cmd);
		break;
	case UBLK_CMD_STOP_DEV:
		ret = ublk_ctrl_stop_dev(ub);
		break;
	case UBLK_CMD_GET_DEV_INFO:
	case UBLK_CMD_GET_DEV_INFO2:
		ret = ublk_ctrl_get_dev_info(ub, cmd);
		break;
	case UBLK_CMD_ADD_DEV:
		ret = ublk_ctrl_add_dev(cmd);
		break;
	case UBLK_CMD_DEL_DEV:
		ret = ublk_ctrl_del_dev(&ub);
		break;
	case UBLK_CMD_GET_QUEUE_AFFINITY:
		ret = ublk_ctrl_get_queue_affinity(ub, cmd);
		break;
	case UBLK_CMD_GET_PARAMS:
		ret = ublk_ctrl_get_params(ub, cmd);
		break;
	case UBLK_CMD_SET_PARAMS:
		ret = ublk_ctrl_set_params(ub, cmd);
		break;
	case UBLK_CMD_START_USER_RECOVERY:
		ret = ublk_ctrl_start_recovery(ub, cmd);
		break;
	case UBLK_CMD_END_USER_RECOVERY:
		ret = ublk_ctrl_end_recovery(ub, cmd);
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}

 put_dev:
	if (ub)
		ublk_put_device(ub);
 out:
	io_uring_cmd_done(cmd, ret, 0, issue_flags);
	pr_devel("%s: cmd done ret %d cmd_op %x, dev id %d qid %d\n",
			__func__, ret, cmd->cmd_op, header->dev_id, header->queue_id);
	return -EIOCBQUEUED;
}

static const struct file_operations ublk_ctl_fops = {
	.open		= nonseekable_open,
	.uring_cmd      = ublk_ctrl_uring_cmd,
	.owner		= THIS_MODULE,
	.llseek		= noop_llseek,
};

static struct miscdevice ublk_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "ublk-control",
	.fops		= &ublk_ctl_fops,
};

static int __init ublk_init(void)
{
	int ret;

	BUILD_BUG_ON((u64)UBLKSRV_IO_BUF_OFFSET +
			UBLKSRV_IO_BUF_TOTAL_SIZE < UBLKSRV_IO_BUF_OFFSET);

	init_waitqueue_head(&ublk_idr_wq);

	ret = misc_register(&ublk_misc);
	if (ret)
		return ret;

	ret = alloc_chrdev_region(&ublk_chr_devt, 0, UBLK_MINORS, "ublk-char");
	if (ret)
		goto unregister_mis;

	ret = class_register(&ublk_chr_class);
	if (ret)
		goto free_chrdev_region;

	return 0;

free_chrdev_region:
	unregister_chrdev_region(ublk_chr_devt, UBLK_MINORS);
unregister_mis:
	misc_deregister(&ublk_misc);
	return ret;
}

static void __exit ublk_exit(void)
{
	struct ublk_device *ub;
	int id;

	idr_for_each_entry(&ublk_index_idr, ub, id)
		ublk_remove(ub);

	class_unregister(&ublk_chr_class);
	misc_deregister(&ublk_misc);

	idr_destroy(&ublk_index_idr);
	unregister_chrdev_region(ublk_chr_devt, UBLK_MINORS);
}

module_init(ublk_init);
module_exit(ublk_exit);

module_param(ublks_max, int, 0444);
MODULE_PARM_DESC(ublks_max, "max number of ublk devices allowed to add(default: 64)");

MODULE_AUTHOR("Ming Lei <ming.lei@redhat.com>");
MODULE_LICENSE("GPL");
