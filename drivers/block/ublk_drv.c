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
#include <linux/io_uring/cmd.h>
#include <linux/blk-mq.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <linux/task_work.h>
#include <linux/namei.h>
#include <linux/kref.h>
#include <uapi/linux/ublk_cmd.h>

#define UBLK_MINORS		(1U << MINORBITS)

#define UBLK_INVALID_BUF_IDX 	((u16)-1)

/* private ioctl command mirror */
#define UBLK_CMD_DEL_DEV_ASYNC	_IOC_NR(UBLK_U_CMD_DEL_DEV_ASYNC)
#define UBLK_CMD_UPDATE_SIZE	_IOC_NR(UBLK_U_CMD_UPDATE_SIZE)
#define UBLK_CMD_QUIESCE_DEV	_IOC_NR(UBLK_U_CMD_QUIESCE_DEV)

#define UBLK_IO_REGISTER_IO_BUF		_IOC_NR(UBLK_U_IO_REGISTER_IO_BUF)
#define UBLK_IO_UNREGISTER_IO_BUF	_IOC_NR(UBLK_U_IO_UNREGISTER_IO_BUF)

/* All UBLK_F_* have to be included into UBLK_F_ALL */
#define UBLK_F_ALL (UBLK_F_SUPPORT_ZERO_COPY \
		| UBLK_F_URING_CMD_COMP_IN_TASK \
		| UBLK_F_NEED_GET_DATA \
		| UBLK_F_USER_RECOVERY \
		| UBLK_F_USER_RECOVERY_REISSUE \
		| UBLK_F_UNPRIVILEGED_DEV \
		| UBLK_F_CMD_IOCTL_ENCODE \
		| UBLK_F_USER_COPY \
		| UBLK_F_ZONED \
		| UBLK_F_USER_RECOVERY_FAIL_IO \
		| UBLK_F_UPDATE_SIZE \
		| UBLK_F_AUTO_BUF_REG \
		| UBLK_F_QUIESCE \
		| UBLK_F_PER_IO_DAEMON \
		| UBLK_F_BUF_REG_OFF_DAEMON)

#define UBLK_F_ALL_RECOVERY_FLAGS (UBLK_F_USER_RECOVERY \
		| UBLK_F_USER_RECOVERY_REISSUE \
		| UBLK_F_USER_RECOVERY_FAIL_IO)

/* All UBLK_PARAM_TYPE_* should be included here */
#define UBLK_PARAM_TYPE_ALL                                \
	(UBLK_PARAM_TYPE_BASIC | UBLK_PARAM_TYPE_DISCARD | \
	 UBLK_PARAM_TYPE_DEVT | UBLK_PARAM_TYPE_ZONED |    \
	 UBLK_PARAM_TYPE_DMA_ALIGN | UBLK_PARAM_TYPE_SEGMENT)

struct ublk_uring_cmd_pdu {
	/*
	 * Store requests in same batch temporarily for queuing them to
	 * daemon context.
	 *
	 * It should have been stored to request payload, but we do want
	 * to avoid extra pre-allocation, and uring_cmd payload is always
	 * free for us
	 */
	union {
		struct request *req;
		struct request *req_list;
	};

	/*
	 * The following two are valid in this cmd whole lifetime, and
	 * setup in ublk uring_cmd handler
	 */
	struct ublk_queue *ubq;

	u16 tag;
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
 * UBLK_IO_FLAG_NEED_GET_DATA is set because IO command requires
 * get data buffer address from ublksrv.
 *
 * Then, bio data could be copied into this data buffer for a WRITE request
 * after the IO command is issued again and UBLK_IO_FLAG_NEED_GET_DATA is unset.
 */
#define UBLK_IO_FLAG_NEED_GET_DATA 0x08

/*
 * request buffer is registered automatically, so we have to unregister it
 * before completing this request.
 *
 * io_uring will unregister buffer automatically for us during exiting.
 */
#define UBLK_IO_FLAG_AUTO_BUF_REG 	0x10

/* atomic RW with ubq->cancel_lock */
#define UBLK_IO_FLAG_CANCELED	0x80000000

/*
 * Initialize refcount to a large number to include any registered buffers.
 * UBLK_IO_COMMIT_AND_FETCH_REQ will release these references minus those for
 * any buffers registered on the io daemon task.
 */
#define UBLK_REFCOUNT_INIT (REFCOUNT_MAX / 2)

struct ublk_io {
	/* userspace buffer address from io cmd */
	union {
		__u64	addr;
		struct ublk_auto_buf_reg buf;
	};
	unsigned int flags;
	int res;

	union {
		/* valid if UBLK_IO_FLAG_ACTIVE is set */
		struct io_uring_cmd *cmd;
		/* valid if UBLK_IO_FLAG_OWNED_BY_SRV is set */
		struct request *req;
	};

	struct task_struct *task;

	/*
	 * The number of uses of this I/O by the ublk server
	 * if user copy or zero copy are enabled:
	 * - UBLK_REFCOUNT_INIT from dispatch to the server
	 *   until UBLK_IO_COMMIT_AND_FETCH_REQ
	 * - 1 for each inflight ublk_ch_{read,write}_iter() call
	 * - 1 for each io_uring registered buffer not registered on task
	 * The I/O can only be completed once all references are dropped.
	 * User copy and buffer registration operations are only permitted
	 * if the reference count is nonzero.
	 */
	refcount_t ref;
	/* Count of buffers registered on task and not yet unregistered */
	unsigned task_registered_buffers;

	void *buf_ctx_handle;
} ____cacheline_aligned_in_smp;

struct ublk_queue {
	int q_id;
	int q_depth;

	unsigned long flags;
	struct ublksrv_io_desc *io_cmd_buf;

	bool force_abort;
	bool canceling;
	bool fail_io; /* copy of dev->state == UBLK_S_DEV_FAIL_IO */
	spinlock_t		cancel_lock;
	struct ublk_device *dev;
	struct ublk_io ios[];
};

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

	spinlock_t		lock;
	struct mm_struct	*mm;

	struct ublk_params	params;

	struct completion	completion;
	u32			nr_io_ready;
	bool 			unprivileged_daemons;
	struct mutex cancel_mutex;
	bool canceling;
	pid_t 	ublksrv_tgid;
	struct delayed_work	exit_work;
};

/* header of ublk_params */
struct ublk_params_header {
	__u32	len;
	__u32	types;
};

static void ublk_io_release(void *priv);
static void ublk_stop_dev_unlocked(struct ublk_device *ub);
static void ublk_abort_queue(struct ublk_device *ub, struct ublk_queue *ubq);
static inline struct request *__ublk_check_and_get_req(struct ublk_device *ub,
		u16 q_id, u16 tag, struct ublk_io *io, size_t offset);
static inline unsigned int ublk_req_build_flags(struct request *req);

static inline struct ublksrv_io_desc *
ublk_get_iod(const struct ublk_queue *ubq, unsigned tag)
{
	return &ubq->io_cmd_buf[tag];
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

struct ublk_zoned_report_desc {
	__u64 sector;
	__u32 operation;
	__u32 nr_zones;
};

static DEFINE_XARRAY(ublk_zoned_report_descs);

static int ublk_zoned_insert_report_desc(const struct request *req,
		struct ublk_zoned_report_desc *desc)
{
	return xa_insert(&ublk_zoned_report_descs, (unsigned long)req,
			    desc, GFP_KERNEL);
}

static struct ublk_zoned_report_desc *ublk_zoned_erase_report_desc(
		const struct request *req)
{
	return xa_erase(&ublk_zoned_report_descs, (unsigned long)req);
}

static struct ublk_zoned_report_desc *ublk_zoned_get_report_desc(
		const struct request *req)
{
	return xa_load(&ublk_zoned_report_descs, (unsigned long)req);
}

static int ublk_get_nr_zones(const struct ublk_device *ub)
{
	const struct ublk_param_basic *p = &ub->params.basic;

	/* Zone size is a power of 2 */
	return p->dev_sectors >> ilog2(p->chunk_sectors);
}

static int ublk_revalidate_disk_zones(struct ublk_device *ub)
{
	return blk_revalidate_disk_zones(ub->ub_disk);
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

static void ublk_dev_param_zoned_apply(struct ublk_device *ub)
{
	ub->ub_disk->nr_zones = ublk_get_nr_zones(ub);
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
		struct ublk_zoned_report_desc desc;
		blk_status_t status;

		memset(buffer, 0, buffer_length);

		req = blk_mq_alloc_request(disk->queue, REQ_OP_DRV_IN, 0);
		if (IS_ERR(req)) {
			ret = PTR_ERR(req);
			goto out;
		}

		desc.operation = UBLK_IO_OP_REPORT_ZONES;
		desc.sector = sector;
		desc.nr_zones = zones_in_request;
		ret = ublk_zoned_insert_report_desc(req, &desc);
		if (ret)
			goto free_req;

		ret = blk_rq_map_kern(req, buffer, buffer_length, GFP_KERNEL);
		if (ret)
			goto erase_desc;

		status = blk_execute_rq(req, 0);
		ret = blk_status_to_errno(status);
erase_desc:
		ublk_zoned_erase_report_desc(req);
free_req:
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
	struct ublk_zoned_report_desc *desc;
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
		desc = ublk_zoned_get_report_desc(req);
		if (!desc)
			return BLK_STS_IOERR;
		ublk_op = desc->operation;
		switch (ublk_op) {
		case UBLK_IO_OP_REPORT_ZONES:
			iod->op_flags = ublk_op | ublk_req_build_flags(req);
			iod->nr_zones = desc->nr_zones;
			iod->start_sector = desc->sector;
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

static void ublk_dev_param_zoned_apply(struct ublk_device *ub)
{
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

static inline void __ublk_complete_rq(struct request *req, struct ublk_io *io,
				      bool need_map);

static dev_t ublk_chr_devt;
static const struct class ublk_chr_class = {
	.name = "ublk-char",
};

static DEFINE_IDR(ublk_index_idr);
static DEFINE_SPINLOCK(ublk_idr_lock);
static wait_queue_head_t ublk_idr_wq;	/* wait until one idr is freed */

static DEFINE_MUTEX(ublk_ctl_mutex);


#define UBLK_MAX_UBLKS UBLK_MINORS

/*
 * Max unprivileged ublk devices allowed to add
 *
 * It can be extended to one per-user limit in future or even controlled
 * by cgroup.
 */
static unsigned int unprivileged_ublks_max = 64;
static unsigned int unprivileged_ublks_added; /* protected by ublk_ctl_mutex */

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
	const struct ublk_param_basic *p = &ub->params.basic;

	if (p->attrs & UBLK_ATTR_READ_ONLY)
		set_disk_ro(ub->ub_disk, true);

	set_capacity(ub->ub_disk, p->dev_sectors);
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

	if (ub->params.types & UBLK_PARAM_TYPE_DMA_ALIGN) {
		const struct ublk_param_dma_align *p = &ub->params.dma;

		if (p->alignment >= PAGE_SIZE)
			return -EINVAL;

		if (!is_power_of_2(p->alignment + 1))
			return -EINVAL;
	}

	if (ub->params.types & UBLK_PARAM_TYPE_SEGMENT) {
		const struct ublk_param_segment *p = &ub->params.seg;

		if (!is_power_of_2(p->seg_boundary_mask + 1))
			return -EINVAL;

		if (p->seg_boundary_mask + 1 < UBLK_MIN_SEGMENT_SIZE)
			return -EINVAL;
		if (p->max_segment_size < UBLK_MIN_SEGMENT_SIZE)
			return -EINVAL;
	}

	return 0;
}

static void ublk_apply_params(struct ublk_device *ub)
{
	ublk_dev_param_basic_apply(ub);

	if (ub->params.types & UBLK_PARAM_TYPE_ZONED)
		ublk_dev_param_zoned_apply(ub);
}

static inline bool ublk_support_zero_copy(const struct ublk_queue *ubq)
{
	return ubq->flags & UBLK_F_SUPPORT_ZERO_COPY;
}

static inline bool ublk_dev_support_zero_copy(const struct ublk_device *ub)
{
	return ub->dev_info.flags & UBLK_F_SUPPORT_ZERO_COPY;
}

static inline bool ublk_support_auto_buf_reg(const struct ublk_queue *ubq)
{
	return ubq->flags & UBLK_F_AUTO_BUF_REG;
}

static inline bool ublk_dev_support_auto_buf_reg(const struct ublk_device *ub)
{
	return ub->dev_info.flags & UBLK_F_AUTO_BUF_REG;
}

static inline bool ublk_support_user_copy(const struct ublk_queue *ubq)
{
	return ubq->flags & UBLK_F_USER_COPY;
}

static inline bool ublk_dev_support_user_copy(const struct ublk_device *ub)
{
	return ub->dev_info.flags & UBLK_F_USER_COPY;
}

static inline bool ublk_need_map_io(const struct ublk_queue *ubq)
{
	return !ublk_support_user_copy(ubq) && !ublk_support_zero_copy(ubq) &&
		!ublk_support_auto_buf_reg(ubq);
}

static inline bool ublk_dev_need_map_io(const struct ublk_device *ub)
{
	return !ublk_dev_support_user_copy(ub) &&
	       !ublk_dev_support_zero_copy(ub) &&
	       !ublk_dev_support_auto_buf_reg(ub);
}

static inline bool ublk_need_req_ref(const struct ublk_queue *ubq)
{
	/*
	 * read()/write() is involved in user copy, so request reference
	 * has to be grabbed
	 *
	 * for zero copy, request buffer need to be registered to io_uring
	 * buffer table, so reference is needed
	 *
	 * For auto buffer register, ublk server still may issue
	 * UBLK_IO_COMMIT_AND_FETCH_REQ before one registered buffer is used up,
	 * so reference is required too.
	 */
	return ublk_support_user_copy(ubq) || ublk_support_zero_copy(ubq) ||
		ublk_support_auto_buf_reg(ubq);
}

static inline bool ublk_dev_need_req_ref(const struct ublk_device *ub)
{
	return ublk_dev_support_user_copy(ub) ||
	       ublk_dev_support_zero_copy(ub) ||
	       ublk_dev_support_auto_buf_reg(ub);
}

static inline void ublk_init_req_ref(const struct ublk_queue *ubq,
		struct ublk_io *io)
{
	if (ublk_need_req_ref(ubq))
		refcount_set(&io->ref, UBLK_REFCOUNT_INIT);
}

static inline bool ublk_get_req_ref(struct ublk_io *io)
{
	return refcount_inc_not_zero(&io->ref);
}

static inline void ublk_put_req_ref(struct ublk_io *io, struct request *req)
{
	if (!refcount_dec_and_test(&io->ref))
		return;

	/* ublk_need_map_io() and ublk_need_req_ref() are mutually exclusive */
	__ublk_complete_rq(req, io, false);
}

static inline bool ublk_sub_req_ref(struct ublk_io *io)
{
	unsigned sub_refs = UBLK_REFCOUNT_INIT - io->task_registered_buffers;

	io->task_registered_buffers = 0;
	return refcount_sub_and_test(sub_refs, &io->ref);
}

static inline bool ublk_need_get_data(const struct ublk_queue *ubq)
{
	return ubq->flags & UBLK_F_NEED_GET_DATA;
}

static inline bool ublk_dev_need_get_data(const struct ublk_device *ub)
{
	return ub->dev_info.flags & UBLK_F_NEED_GET_DATA;
}

/* Called in slow path only, keep it noinline for trace purpose */
static noinline struct ublk_device *ublk_get_device(struct ublk_device *ub)
{
	if (kobject_get_unless_zero(&ub->cdev_dev.kobj))
		return ub;
	return NULL;
}

/* Called in slow path only, keep it noinline for trace purpose */
static noinline void ublk_put_device(struct ublk_device *ub)
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

static inline struct ublksrv_io_desc *
ublk_queue_cmd_buf(struct ublk_device *ub, int q_id)
{
	return ublk_get_queue(ub, q_id)->io_cmd_buf;
}

static inline int __ublk_queue_cmd_buf_size(int depth)
{
	return round_up(depth * sizeof(struct ublksrv_io_desc), PAGE_SIZE);
}

static inline int ublk_queue_cmd_buf_size(struct ublk_device *ub)
{
	return __ublk_queue_cmd_buf_size(ub->dev_info.queue_depth);
}

static int ublk_max_cmd_buf_size(void)
{
	return __ublk_queue_cmd_buf_size(UBLK_MAX_QUEUE_DEPTH);
}

/*
 * Should I/O outstanding to the ublk server when it exits be reissued?
 * If not, outstanding I/O will get errors.
 */
static inline bool ublk_nosrv_should_reissue_outstanding(struct ublk_device *ub)
{
	return (ub->dev_info.flags & UBLK_F_USER_RECOVERY) &&
	       (ub->dev_info.flags & UBLK_F_USER_RECOVERY_REISSUE);
}

/*
 * Should I/O issued while there is no ublk server queue? If not, I/O
 * issued while there is no ublk server will get errors.
 */
static inline bool ublk_nosrv_dev_should_queue_io(struct ublk_device *ub)
{
	return (ub->dev_info.flags & UBLK_F_USER_RECOVERY) &&
	       !(ub->dev_info.flags & UBLK_F_USER_RECOVERY_FAIL_IO);
}

/*
 * Same as ublk_nosrv_dev_should_queue_io, but uses a queue-local copy
 * of the device flags for smaller cache footprint - better for fast
 * paths.
 */
static inline bool ublk_nosrv_should_queue_io(struct ublk_queue *ubq)
{
	return (ubq->flags & UBLK_F_USER_RECOVERY) &&
	       !(ubq->flags & UBLK_F_USER_RECOVERY_FAIL_IO);
}

/*
 * Should ublk devices be stopped (i.e. no recovery possible) when the
 * ublk server exits? If not, devices can be used again by a future
 * incarnation of a ublk server via the start_recovery/end_recovery
 * commands.
 */
static inline bool ublk_nosrv_should_stop_dev(struct ublk_device *ub)
{
	return !(ub->dev_info.flags & UBLK_F_USER_RECOVERY);
}

static inline bool ublk_dev_in_recoverable_state(struct ublk_device *ub)
{
	return ub->dev_info.state == UBLK_S_DEV_QUIESCED ||
	       ub->dev_info.state == UBLK_S_DEV_FAIL_IO;
}

static void ublk_free_disk(struct gendisk *disk)
{
	struct ublk_device *ub = disk->private_data;

	clear_bit(UB_STATE_USED, &ub->state);
	ublk_put_device(ub);
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
		       const struct ublk_io *io)
{
	const unsigned int rq_bytes = blk_rq_bytes(req);

	if (!ublk_need_map_io(ubq))
		return rq_bytes;

	/*
	 * no zero copy, we delay copy WRITE request data into ublksrv
	 * context and the big benefit is that pinning pages in current
	 * context is pretty fast, see ublk_pin_user_pages
	 */
	if (ublk_need_map_req(req)) {
		struct iov_iter iter;
		const int dir = ITER_DEST;

		import_ubuf(dir, u64_to_user_ptr(io->addr), rq_bytes, &iter);
		return ublk_copy_user_pages(req, 0, &iter, dir);
	}
	return rq_bytes;
}

static int ublk_unmap_io(bool need_map,
		const struct request *req,
		const struct ublk_io *io)
{
	const unsigned int rq_bytes = blk_rq_bytes(req);

	if (!need_map)
		return rq_bytes;

	if (ublk_need_unmap_req(req)) {
		struct iov_iter iter;
		const int dir = ITER_SOURCE;

		WARN_ON_ONCE(io->res > rq_bytes);

		import_ubuf(dir, u64_to_user_ptr(io->addr), io->res, &iter);
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
	u32 ublk_op;

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
	return io_uring_cmd_to_pdu(ioucmd, struct ublk_uring_cmd_pdu);
}

/* todo: handle partial completion */
static inline void __ublk_complete_rq(struct request *req, struct ublk_io *io,
				      bool need_map)
{
	unsigned int unmapped_bytes;
	blk_status_t res = BLK_STS_OK;

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
	unmapped_bytes = ublk_unmap_io(need_map, req, io);

	/*
	 * Extremely impossible since we got data filled in just before
	 *
	 * Re-read simply for this unlikely case.
	 */
	if (unlikely(unmapped_bytes < io->res))
		io->res = unmapped_bytes;

	if (blk_update_request(req, BLK_STS_OK, io->res))
		blk_mq_requeue_request(req, true);
	else if (likely(!blk_should_fake_timeout(req->q)))
		__blk_mq_end_request(req, BLK_STS_OK);

	return;
exit:
	blk_mq_end_request(req, res);
}

static struct io_uring_cmd *__ublk_prep_compl_io_cmd(struct ublk_io *io,
						     struct request *req)
{
	/* read cmd first because req will overwrite it */
	struct io_uring_cmd *cmd = io->cmd;

	/* mark this cmd owned by ublksrv */
	io->flags |= UBLK_IO_FLAG_OWNED_BY_SRV;

	/*
	 * clear ACTIVE since we are done with this sqe/cmd slot
	 * We can only accept io cmd in case of being not active.
	 */
	io->flags &= ~UBLK_IO_FLAG_ACTIVE;

	io->req = req;
	return cmd;
}

static void ublk_complete_io_cmd(struct ublk_io *io, struct request *req,
				 int res, unsigned issue_flags)
{
	struct io_uring_cmd *cmd = __ublk_prep_compl_io_cmd(io, req);

	/* tell ublksrv one io request is coming */
	io_uring_cmd_done(cmd, res, issue_flags);
}

#define UBLK_REQUEUE_DELAY_MS	3

static inline void __ublk_abort_rq(struct ublk_queue *ubq,
		struct request *rq)
{
	/* We cannot process this rq so just requeue it. */
	if (ublk_nosrv_dev_should_queue_io(ubq->dev))
		blk_mq_requeue_request(rq, false);
	else
		blk_mq_end_request(rq, BLK_STS_IOERR);
}

static void
ublk_auto_buf_reg_fallback(const struct ublk_queue *ubq, struct ublk_io *io)
{
	unsigned tag = io - ubq->ios;
	struct ublksrv_io_desc *iod = ublk_get_iod(ubq, tag);

	iod->op_flags |= UBLK_IO_F_NEED_REG_BUF;
}

static bool ublk_auto_buf_reg(const struct ublk_queue *ubq, struct request *req,
			      struct ublk_io *io, unsigned int issue_flags)
{
	int ret;

	ret = io_buffer_register_bvec(io->cmd, req, ublk_io_release,
				      io->buf.index, issue_flags);
	if (ret) {
		if (io->buf.flags & UBLK_AUTO_BUF_REG_FALLBACK) {
			ublk_auto_buf_reg_fallback(ubq, io);
			return true;
		}
		blk_mq_end_request(req, BLK_STS_IOERR);
		return false;
	}

	io->task_registered_buffers = 1;
	io->buf_ctx_handle = io_uring_cmd_ctx_handle(io->cmd);
	io->flags |= UBLK_IO_FLAG_AUTO_BUF_REG;
	return true;
}

static bool ublk_prep_auto_buf_reg(struct ublk_queue *ubq,
				   struct request *req, struct ublk_io *io,
				   unsigned int issue_flags)
{
	ublk_init_req_ref(ubq, io);
	if (ublk_support_auto_buf_reg(ubq) && ublk_rq_has_data(req))
		return ublk_auto_buf_reg(ubq, req, io, issue_flags);

	return true;
}

static bool ublk_start_io(const struct ublk_queue *ubq, struct request *req,
			  struct ublk_io *io)
{
	unsigned mapped_bytes = ublk_map_io(ubq, req, io);

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
			return false;
		}

		ublk_get_iod(ubq, req->tag)->nr_sectors =
			mapped_bytes >> 9;
	}

	return true;
}

static void ublk_dispatch_req(struct ublk_queue *ubq,
			      struct request *req,
			      unsigned int issue_flags)
{
	int tag = req->tag;
	struct ublk_io *io = &ubq->ios[tag];

	pr_devel("%s: complete: qid %d tag %d io_flags %x addr %llx\n",
			__func__, ubq->q_id, req->tag, io->flags,
			ublk_get_iod(ubq, req->tag)->addr);

	/*
	 * Task is exiting if either:
	 *
	 * (1) current != io->task.
	 * io_uring_cmd_complete_in_task() tries to run task_work
	 * in a workqueue if cmd's task is PF_EXITING.
	 *
	 * (2) current->flags & PF_EXITING.
	 */
	if (unlikely(current != io->task || current->flags & PF_EXITING)) {
		__ublk_abort_rq(ubq, req);
		return;
	}

	if (ublk_need_get_data(ubq) && ublk_need_map_req(req)) {
		/*
		 * We have not handled UBLK_IO_NEED_GET_DATA command yet,
		 * so immediately pass UBLK_IO_RES_NEED_GET_DATA to ublksrv
		 * and notify it.
		 */
		io->flags |= UBLK_IO_FLAG_NEED_GET_DATA;
		pr_devel("%s: need get data. qid %d tag %d io_flags %x\n",
				__func__, ubq->q_id, req->tag, io->flags);
		ublk_complete_io_cmd(io, req, UBLK_IO_RES_NEED_GET_DATA,
				     issue_flags);
		return;
	}

	if (!ublk_start_io(ubq, req, io))
		return;

	if (ublk_prep_auto_buf_reg(ubq, req, io, issue_flags))
		ublk_complete_io_cmd(io, req, UBLK_IO_RES_OK, issue_flags);
}

static void ublk_cmd_tw_cb(struct io_uring_cmd *cmd,
			   unsigned int issue_flags)
{
	struct ublk_uring_cmd_pdu *pdu = ublk_get_uring_cmd_pdu(cmd);
	struct ublk_queue *ubq = pdu->ubq;

	ublk_dispatch_req(ubq, pdu->req, issue_flags);
}

static void ublk_queue_cmd(struct ublk_queue *ubq, struct request *rq)
{
	struct io_uring_cmd *cmd = ubq->ios[rq->tag].cmd;
	struct ublk_uring_cmd_pdu *pdu = ublk_get_uring_cmd_pdu(cmd);

	pdu->req = rq;
	io_uring_cmd_complete_in_task(cmd, ublk_cmd_tw_cb);
}

static void ublk_cmd_list_tw_cb(struct io_uring_cmd *cmd,
		unsigned int issue_flags)
{
	struct ublk_uring_cmd_pdu *pdu = ublk_get_uring_cmd_pdu(cmd);
	struct request *rq = pdu->req_list;
	struct request *next;

	do {
		next = rq->rq_next;
		rq->rq_next = NULL;
		ublk_dispatch_req(rq->mq_hctx->driver_data, rq, issue_flags);
		rq = next;
	} while (rq);
}

static void ublk_queue_cmd_list(struct ublk_io *io, struct rq_list *l)
{
	struct io_uring_cmd *cmd = io->cmd;
	struct ublk_uring_cmd_pdu *pdu = ublk_get_uring_cmd_pdu(cmd);

	pdu->req_list = rq_list_peek(l);
	rq_list_init(l);
	io_uring_cmd_complete_in_task(cmd, ublk_cmd_list_tw_cb);
}

static enum blk_eh_timer_return ublk_timeout(struct request *rq)
{
	struct ublk_queue *ubq = rq->mq_hctx->driver_data;
	pid_t tgid = ubq->dev->ublksrv_tgid;
	struct task_struct *p;
	struct pid *pid;

	if (!(ubq->flags & UBLK_F_UNPRIVILEGED_DEV))
		return BLK_EH_RESET_TIMER;

	if (unlikely(!tgid))
		return BLK_EH_RESET_TIMER;

	rcu_read_lock();
	pid = find_vpid(tgid);
	p = pid_task(pid, PIDTYPE_PID);
	if (p)
		send_sig(SIGKILL, p, 0);
	rcu_read_unlock();
	return BLK_EH_DONE;
}

static blk_status_t ublk_prep_req(struct ublk_queue *ubq, struct request *rq,
				  bool check_cancel)
{
	blk_status_t res;

	if (unlikely(READ_ONCE(ubq->fail_io)))
		return BLK_STS_TARGET;

	/* With recovery feature enabled, force_abort is set in
	 * ublk_stop_dev() before calling del_gendisk(). We have to
	 * abort all requeued and new rqs here to let del_gendisk()
	 * move on. Besides, we cannot not call io_uring_cmd_complete_in_task()
	 * to avoid UAF on io_uring ctx.
	 *
	 * Note: force_abort is guaranteed to be seen because it is set
	 * before request queue is unqiuesced.
	 */
	if (ublk_nosrv_should_queue_io(ubq) &&
	    unlikely(READ_ONCE(ubq->force_abort)))
		return BLK_STS_IOERR;

	if (check_cancel && unlikely(ubq->canceling))
		return BLK_STS_IOERR;

	/* fill iod to slot in io cmd buffer */
	res = ublk_setup_iod(ubq, rq);
	if (unlikely(res != BLK_STS_OK))
		return BLK_STS_IOERR;

	blk_mq_start_request(rq);
	return BLK_STS_OK;
}

static blk_status_t ublk_queue_rq(struct blk_mq_hw_ctx *hctx,
		const struct blk_mq_queue_data *bd)
{
	struct ublk_queue *ubq = hctx->driver_data;
	struct request *rq = bd->rq;
	blk_status_t res;

	res = ublk_prep_req(ubq, rq, false);
	if (res != BLK_STS_OK)
		return res;

	/*
	 * ->canceling has to be handled after ->force_abort and ->fail_io
	 * is dealt with, otherwise this request may not be failed in case
	 * of recovery, and cause hang when deleting disk
	 */
	if (unlikely(ubq->canceling)) {
		__ublk_abort_rq(ubq, rq);
		return BLK_STS_OK;
	}

	ublk_queue_cmd(ubq, rq);
	return BLK_STS_OK;
}

static inline bool ublk_belong_to_same_batch(const struct ublk_io *io,
					     const struct ublk_io *io2)
{
	return (io_uring_cmd_ctx_handle(io->cmd) ==
		io_uring_cmd_ctx_handle(io2->cmd)) &&
		(io->task == io2->task);
}

static void ublk_queue_rqs(struct rq_list *rqlist)
{
	struct rq_list requeue_list = { };
	struct rq_list submit_list = { };
	struct ublk_io *io = NULL;
	struct request *req;

	while ((req = rq_list_pop(rqlist))) {
		struct ublk_queue *this_q = req->mq_hctx->driver_data;
		struct ublk_io *this_io = &this_q->ios[req->tag];

		if (ublk_prep_req(this_q, req, true) != BLK_STS_OK) {
			rq_list_add_tail(&requeue_list, req);
			continue;
		}

		if (io && !ublk_belong_to_same_batch(io, this_io) &&
				!rq_list_empty(&submit_list))
			ublk_queue_cmd_list(io, &submit_list);
		io = this_io;
		rq_list_add_tail(&submit_list, req);
	}

	if (!rq_list_empty(&submit_list))
		ublk_queue_cmd_list(io, &submit_list);
	*rqlist = requeue_list;
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
	.queue_rqs      = ublk_queue_rqs,
	.init_hctx	= ublk_init_hctx,
	.timeout	= ublk_timeout,
};

static void ublk_queue_reinit(struct ublk_device *ub, struct ublk_queue *ubq)
{
	int i;

	for (i = 0; i < ubq->q_depth; i++) {
		struct ublk_io *io = &ubq->ios[i];

		/*
		 * UBLK_IO_FLAG_CANCELED is kept for avoiding to touch
		 * io->cmd
		 */
		io->flags &= UBLK_IO_FLAG_CANCELED;
		io->cmd = NULL;
		io->addr = 0;

		/*
		 * old task is PF_EXITING, put it now
		 *
		 * It could be NULL in case of closing one quiesced
		 * device.
		 */
		if (io->task) {
			put_task_struct(io->task);
			io->task = NULL;
		}

		WARN_ON_ONCE(refcount_read(&io->ref));
		WARN_ON_ONCE(io->task_registered_buffers);
	}
}

static int ublk_ch_open(struct inode *inode, struct file *filp)
{
	struct ublk_device *ub = container_of(inode->i_cdev,
			struct ublk_device, cdev);

	if (test_and_set_bit(UB_STATE_OPEN, &ub->state))
		return -EBUSY;
	filp->private_data = ub;
	ub->ublksrv_tgid = current->tgid;
	return 0;
}

static void ublk_reset_ch_dev(struct ublk_device *ub)
{
	int i;

	for (i = 0; i < ub->dev_info.nr_hw_queues; i++)
		ublk_queue_reinit(ub, ublk_get_queue(ub, i));

	/* set to NULL, otherwise new tasks cannot mmap io_cmd_buf */
	ub->mm = NULL;
	ub->nr_io_ready = 0;
	ub->unprivileged_daemons = false;
	ub->ublksrv_tgid = -1;
}

static struct gendisk *ublk_get_disk(struct ublk_device *ub)
{
	struct gendisk *disk;

	spin_lock(&ub->lock);
	disk = ub->ub_disk;
	if (disk)
		get_device(disk_to_dev(disk));
	spin_unlock(&ub->lock);

	return disk;
}

static void ublk_put_disk(struct gendisk *disk)
{
	if (disk)
		put_device(disk_to_dev(disk));
}

/*
 * Use this function to ensure that ->canceling is consistently set for
 * the device and all queues. Do not set these flags directly.
 *
 * Caller must ensure that:
 * - cancel_mutex is held. This ensures that there is no concurrent
 *   access to ub->canceling and no concurrent writes to ubq->canceling.
 * - there are no concurrent reads of ubq->canceling from the queue_rq
 *   path. This can be done by quiescing the queue, or through other
 *   means.
 */
static void ublk_set_canceling(struct ublk_device *ub, bool canceling)
	__must_hold(&ub->cancel_mutex)
{
	int i;

	ub->canceling = canceling;
	for (i = 0; i < ub->dev_info.nr_hw_queues; i++)
		ublk_get_queue(ub, i)->canceling = canceling;
}

static bool ublk_check_and_reset_active_ref(struct ublk_device *ub)
{
	int i, j;

	if (!(ub->dev_info.flags & (UBLK_F_SUPPORT_ZERO_COPY |
					UBLK_F_AUTO_BUF_REG)))
		return false;

	for (i = 0; i < ub->dev_info.nr_hw_queues; i++) {
		struct ublk_queue *ubq = ublk_get_queue(ub, i);

		for (j = 0; j < ubq->q_depth; j++) {
			struct ublk_io *io = &ubq->ios[j];
			unsigned int refs = refcount_read(&io->ref) +
				io->task_registered_buffers;

			/*
			 * UBLK_REFCOUNT_INIT or zero means no active
			 * reference
			 */
			if (refs != UBLK_REFCOUNT_INIT && refs != 0)
				return true;

			/* reset to zero if the io hasn't active references */
			refcount_set(&io->ref, 0);
			io->task_registered_buffers = 0;
		}
	}
	return false;
}

static void ublk_ch_release_work_fn(struct work_struct *work)
{
	struct ublk_device *ub =
		container_of(work, struct ublk_device, exit_work.work);
	struct gendisk *disk;
	int i;

	/*
	 * For zero-copy and auto buffer register modes, I/O references
	 * might not be dropped naturally when the daemon is killed, but
	 * io_uring guarantees that registered bvec kernel buffers are
	 * unregistered finally when freeing io_uring context, then the
	 * active references are dropped.
	 *
	 * Wait until active references are dropped for avoiding use-after-free
	 *
	 * registered buffer may be unregistered in io_ring's release hander,
	 * so have to wait by scheduling work function for avoiding the two
	 * file release dependency.
	 */
	if (ublk_check_and_reset_active_ref(ub)) {
		schedule_delayed_work(&ub->exit_work, 1);
		return;
	}

	/*
	 * disk isn't attached yet, either device isn't live, or it has
	 * been removed already, so we needn't to do anything
	 */
	disk = ublk_get_disk(ub);
	if (!disk)
		goto out;

	/*
	 * All uring_cmd are done now, so abort any request outstanding to
	 * the ublk server
	 *
	 * This can be done in lockless way because ublk server has been
	 * gone
	 *
	 * More importantly, we have to provide forward progress guarantee
	 * without holding ub->mutex, otherwise control task grabbing
	 * ub->mutex triggers deadlock
	 *
	 * All requests may be inflight, so ->canceling may not be set, set
	 * it now.
	 */
	mutex_lock(&ub->cancel_mutex);
	ublk_set_canceling(ub, true);
	for (i = 0; i < ub->dev_info.nr_hw_queues; i++)
		ublk_abort_queue(ub, ublk_get_queue(ub, i));
	mutex_unlock(&ub->cancel_mutex);
	blk_mq_kick_requeue_list(disk->queue);

	/*
	 * All infligh requests have been completed or requeued and any new
	 * request will be failed or requeued via `->canceling` now, so it is
	 * fine to grab ub->mutex now.
	 */
	mutex_lock(&ub->mutex);

	/* double check after grabbing lock */
	if (!ub->ub_disk)
		goto unlock;

	/*
	 * Transition the device to the nosrv state. What exactly this
	 * means depends on the recovery flags
	 */
	if (ublk_nosrv_should_stop_dev(ub)) {
		/*
		 * Allow any pending/future I/O to pass through quickly
		 * with an error. This is needed because del_gendisk
		 * waits for all pending I/O to complete
		 */
		for (i = 0; i < ub->dev_info.nr_hw_queues; i++)
			WRITE_ONCE(ublk_get_queue(ub, i)->force_abort, true);

		ublk_stop_dev_unlocked(ub);
	} else {
		if (ublk_nosrv_dev_should_queue_io(ub)) {
			/* ->canceling is set and all requests are aborted */
			ub->dev_info.state = UBLK_S_DEV_QUIESCED;
		} else {
			ub->dev_info.state = UBLK_S_DEV_FAIL_IO;
			for (i = 0; i < ub->dev_info.nr_hw_queues; i++)
				WRITE_ONCE(ublk_get_queue(ub, i)->fail_io, true);
		}
	}
unlock:
	mutex_unlock(&ub->mutex);
	ublk_put_disk(disk);

	/* all uring_cmd has been done now, reset device & ubq */
	ublk_reset_ch_dev(ub);
out:
	clear_bit(UB_STATE_OPEN, &ub->state);

	/* put the reference grabbed in ublk_ch_release() */
	ublk_put_device(ub);
}

static int ublk_ch_release(struct inode *inode, struct file *filp)
{
	struct ublk_device *ub = filp->private_data;

	/*
	 * Grab ublk device reference, so it won't be gone until we are
	 * really released from work function.
	 */
	ublk_get_device(ub);

	INIT_DELAYED_WORK(&ub->exit_work, ublk_ch_release_work_fn);
	schedule_delayed_work(&ub->exit_work, 0);
	return 0;
}

/* map pre-allocated per-queue cmd buffer to ublksrv daemon */
static int ublk_ch_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct ublk_device *ub = filp->private_data;
	size_t sz = vma->vm_end - vma->vm_start;
	unsigned max_sz = ublk_max_cmd_buf_size();
	unsigned long pfn, end, phys_off = vma->vm_pgoff << PAGE_SHIFT;
	int q_id, ret = 0;

	spin_lock(&ub->lock);
	if (!ub->mm)
		ub->mm = current->mm;
	if (current->mm != ub->mm)
		ret = -EINVAL;
	spin_unlock(&ub->lock);

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

	if (sz != ublk_queue_cmd_buf_size(ub))
		return -EINVAL;

	pfn = virt_to_phys(ublk_queue_cmd_buf(ub, q_id)) >> PAGE_SHIFT;
	return remap_pfn_range(vma, vma->vm_start, pfn, sz, vma->vm_page_prot);
}

static void __ublk_fail_req(struct ublk_device *ub, struct ublk_io *io,
		struct request *req)
{
	WARN_ON_ONCE(io->flags & UBLK_IO_FLAG_ACTIVE);

	if (ublk_nosrv_should_reissue_outstanding(ub))
		blk_mq_requeue_request(req, false);
	else {
		io->res = -EIO;
		__ublk_complete_rq(req, io, ublk_dev_need_map_io(ub));
	}
}

/*
 * Called from ublk char device release handler, when any uring_cmd is
 * done, meantime request queue is "quiesced" since all inflight requests
 * can't be completed because ublk server is dead.
 *
 * So no one can hold our request IO reference any more, simply ignore the
 * reference, and complete the request immediately
 */
static void ublk_abort_queue(struct ublk_device *ub, struct ublk_queue *ubq)
{
	int i;

	for (i = 0; i < ubq->q_depth; i++) {
		struct ublk_io *io = &ubq->ios[i];

		if (io->flags & UBLK_IO_FLAG_OWNED_BY_SRV)
			__ublk_fail_req(ub, io, io->req);
	}
}

static void ublk_start_cancel(struct ublk_device *ub)
{
	struct gendisk *disk = ublk_get_disk(ub);

	/* Our disk has been dead */
	if (!disk)
		return;

	mutex_lock(&ub->cancel_mutex);
	if (ub->canceling)
		goto out;
	/*
	 * Now we are serialized with ublk_queue_rq()
	 *
	 * Make sure that ubq->canceling is set when queue is frozen,
	 * because ublk_queue_rq() has to rely on this flag for avoiding to
	 * touch completed uring_cmd
	 */
	blk_mq_quiesce_queue(disk->queue);
	ublk_set_canceling(ub, true);
	blk_mq_unquiesce_queue(disk->queue);
out:
	mutex_unlock(&ub->cancel_mutex);
	ublk_put_disk(disk);
}

static void ublk_cancel_cmd(struct ublk_queue *ubq, unsigned tag,
		unsigned int issue_flags)
{
	struct ublk_io *io = &ubq->ios[tag];
	struct ublk_device *ub = ubq->dev;
	struct request *req;
	bool done;

	if (!(io->flags & UBLK_IO_FLAG_ACTIVE))
		return;

	/*
	 * Don't try to cancel this command if the request is started for
	 * avoiding race between io_uring_cmd_done() and
	 * io_uring_cmd_complete_in_task().
	 *
	 * Either the started request will be aborted via __ublk_abort_rq(),
	 * then this uring_cmd is canceled next time, or it will be done in
	 * task work function ublk_dispatch_req() because io_uring guarantees
	 * that ublk_dispatch_req() is always called
	 */
	req = blk_mq_tag_to_rq(ub->tag_set.tags[ubq->q_id], tag);
	if (req && blk_mq_request_started(req) && req->tag == tag)
		return;

	spin_lock(&ubq->cancel_lock);
	done = !!(io->flags & UBLK_IO_FLAG_CANCELED);
	if (!done)
		io->flags |= UBLK_IO_FLAG_CANCELED;
	spin_unlock(&ubq->cancel_lock);

	if (!done)
		io_uring_cmd_done(io->cmd, UBLK_IO_RES_ABORT, issue_flags);
}

/*
 * The ublk char device won't be closed when calling cancel fn, so both
 * ublk device and queue are guaranteed to be live
 *
 * Two-stage cancel:
 *
 * - make every active uring_cmd done in ->cancel_fn()
 *
 * - aborting inflight ublk IO requests in ublk char device release handler,
 *   which depends on 1st stage because device can only be closed iff all
 *   uring_cmd are done
 *
 * Do _not_ try to acquire ub->mutex before all inflight requests are
 * aborted, otherwise deadlock may be caused.
 */
static void ublk_uring_cmd_cancel_fn(struct io_uring_cmd *cmd,
		unsigned int issue_flags)
{
	struct ublk_uring_cmd_pdu *pdu = ublk_get_uring_cmd_pdu(cmd);
	struct ublk_queue *ubq = pdu->ubq;
	struct task_struct *task;
	struct ublk_io *io;

	if (WARN_ON_ONCE(!ubq))
		return;

	if (WARN_ON_ONCE(pdu->tag >= ubq->q_depth))
		return;

	task = io_uring_cmd_get_task(cmd);
	io = &ubq->ios[pdu->tag];
	if (WARN_ON_ONCE(task && task != io->task))
		return;

	ublk_start_cancel(ubq->dev);

	WARN_ON_ONCE(io->cmd != cmd);
	ublk_cancel_cmd(ubq, pdu->tag, issue_flags);
}

static inline bool ublk_dev_ready(const struct ublk_device *ub)
{
	u32 total = (u32)ub->dev_info.nr_hw_queues * ub->dev_info.queue_depth;

	return ub->nr_io_ready == total;
}

static void ublk_cancel_queue(struct ublk_queue *ubq)
{
	int i;

	for (i = 0; i < ubq->q_depth; i++)
		ublk_cancel_cmd(ubq, i, IO_URING_F_UNLOCKED);
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

static void ublk_force_abort_dev(struct ublk_device *ub)
{
	int i;

	pr_devel("%s: force abort ub: dev_id %d state %s\n",
			__func__, ub->dev_info.dev_id,
			ub->dev_info.state == UBLK_S_DEV_LIVE ?
			"LIVE" : "QUIESCED");
	blk_mq_quiesce_queue(ub->ub_disk->queue);
	if (ub->dev_info.state == UBLK_S_DEV_LIVE)
		ublk_wait_tagset_rqs_idle(ub);

	for (i = 0; i < ub->dev_info.nr_hw_queues; i++)
		ublk_get_queue(ub, i)->force_abort = true;
	blk_mq_unquiesce_queue(ub->ub_disk->queue);
	/* We may have requeued some rqs in ublk_quiesce_queue() */
	blk_mq_kick_requeue_list(ub->ub_disk->queue);
}

static struct gendisk *ublk_detach_disk(struct ublk_device *ub)
{
	struct gendisk *disk;

	/* Sync with ublk_abort_queue() by holding the lock */
	spin_lock(&ub->lock);
	disk = ub->ub_disk;
	ub->dev_info.state = UBLK_S_DEV_DEAD;
	ub->dev_info.ublksrv_pid = -1;
	ub->ub_disk = NULL;
	spin_unlock(&ub->lock);

	return disk;
}

static void ublk_stop_dev_unlocked(struct ublk_device *ub)
	__must_hold(&ub->mutex)
{
	struct gendisk *disk;

	if (ub->dev_info.state == UBLK_S_DEV_DEAD)
		return;

	if (ublk_nosrv_dev_should_queue_io(ub))
		ublk_force_abort_dev(ub);
	del_gendisk(ub->ub_disk);
	disk = ublk_detach_disk(ub);
	put_disk(disk);
}

static void ublk_stop_dev(struct ublk_device *ub)
{
	mutex_lock(&ub->mutex);
	ublk_stop_dev_unlocked(ub);
	mutex_unlock(&ub->mutex);
	ublk_cancel_dev(ub);
}

/* reset ublk io_uring queue & io flags */
static void ublk_reset_io_flags(struct ublk_device *ub)
{
	int i, j;

	for (i = 0; i < ub->dev_info.nr_hw_queues; i++) {
		struct ublk_queue *ubq = ublk_get_queue(ub, i);

		/* UBLK_IO_FLAG_CANCELED can be cleared now */
		spin_lock(&ubq->cancel_lock);
		for (j = 0; j < ubq->q_depth; j++)
			ubq->ios[j].flags &= ~UBLK_IO_FLAG_CANCELED;
		spin_unlock(&ubq->cancel_lock);
		ubq->fail_io = false;
	}
	mutex_lock(&ub->cancel_mutex);
	ublk_set_canceling(ub, false);
	mutex_unlock(&ub->cancel_mutex);
}

/* device can only be started after all IOs are ready */
static void ublk_mark_io_ready(struct ublk_device *ub)
	__must_hold(&ub->mutex)
{
	if (!ub->unprivileged_daemons && !capable(CAP_SYS_ADMIN))
		ub->unprivileged_daemons = true;

	ub->nr_io_ready++;
	if (ublk_dev_ready(ub)) {
		/* now we are ready for handling ublk io request */
		ublk_reset_io_flags(ub);
		complete_all(&ub->completion);
	}
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

static inline int ublk_set_auto_buf_reg(struct ublk_io *io, struct io_uring_cmd *cmd)
{
	io->buf = ublk_sqe_addr_to_auto_buf_reg(READ_ONCE(cmd->sqe->addr));

	if (io->buf.reserved0 || io->buf.reserved1)
		return -EINVAL;

	if (io->buf.flags & ~UBLK_AUTO_BUF_REG_F_MASK)
		return -EINVAL;
	return 0;
}

static int ublk_handle_auto_buf_reg(struct ublk_io *io,
				    struct io_uring_cmd *cmd,
				    u16 *buf_idx)
{
	if (io->flags & UBLK_IO_FLAG_AUTO_BUF_REG) {
		io->flags &= ~UBLK_IO_FLAG_AUTO_BUF_REG;

		/*
		 * `UBLK_F_AUTO_BUF_REG` only works iff `UBLK_IO_FETCH_REQ`
		 * and `UBLK_IO_COMMIT_AND_FETCH_REQ` are issued from same
		 * `io_ring_ctx`.
		 *
		 * If this uring_cmd's io_ring_ctx isn't same with the
		 * one for registering the buffer, it is ublk server's
		 * responsibility for unregistering the buffer, otherwise
		 * this ublk request gets stuck.
		 */
		if (io->buf_ctx_handle == io_uring_cmd_ctx_handle(cmd))
			*buf_idx = io->buf.index;
	}

	return ublk_set_auto_buf_reg(io, cmd);
}

/* Once we return, `io->req` can't be used any more */
static inline struct request *
ublk_fill_io_cmd(struct ublk_io *io, struct io_uring_cmd *cmd)
{
	struct request *req = io->req;

	io->cmd = cmd;
	io->flags |= UBLK_IO_FLAG_ACTIVE;
	/* now this cmd slot is owned by ublk driver */
	io->flags &= ~UBLK_IO_FLAG_OWNED_BY_SRV;

	return req;
}

static inline int
ublk_config_io_buf(const struct ublk_device *ub, struct ublk_io *io,
		   struct io_uring_cmd *cmd, unsigned long buf_addr,
		   u16 *buf_idx)
{
	if (ublk_dev_support_auto_buf_reg(ub))
		return ublk_handle_auto_buf_reg(io, cmd, buf_idx);

	io->addr = buf_addr;
	return 0;
}

static inline void ublk_prep_cancel(struct io_uring_cmd *cmd,
				    unsigned int issue_flags,
				    struct ublk_queue *ubq, unsigned int tag)
{
	struct ublk_uring_cmd_pdu *pdu = ublk_get_uring_cmd_pdu(cmd);

	/*
	 * Safe to refer to @ubq since ublk_queue won't be died until its
	 * commands are completed
	 */
	pdu->ubq = ubq;
	pdu->tag = tag;
	io_uring_cmd_mark_cancelable(cmd, issue_flags);
}

static void ublk_io_release(void *priv)
{
	struct request *rq = priv;
	struct ublk_queue *ubq = rq->mq_hctx->driver_data;
	struct ublk_io *io = &ubq->ios[rq->tag];

	/*
	 * task_registered_buffers may be 0 if buffers were registered off task
	 * but unregistered on task. Or after UBLK_IO_COMMIT_AND_FETCH_REQ.
	 */
	if (current == io->task && io->task_registered_buffers)
		io->task_registered_buffers--;
	else
		ublk_put_req_ref(io, rq);
}

static int ublk_register_io_buf(struct io_uring_cmd *cmd,
				struct ublk_device *ub,
				u16 q_id, u16 tag,
				struct ublk_io *io,
				unsigned int index, unsigned int issue_flags)
{
	struct request *req;
	int ret;

	if (!ublk_dev_support_zero_copy(ub))
		return -EINVAL;

	req = __ublk_check_and_get_req(ub, q_id, tag, io, 0);
	if (!req)
		return -EINVAL;

	ret = io_buffer_register_bvec(cmd, req, ublk_io_release, index,
				      issue_flags);
	if (ret) {
		ublk_put_req_ref(io, req);
		return ret;
	}

	return 0;
}

static int
ublk_daemon_register_io_buf(struct io_uring_cmd *cmd,
			    struct ublk_device *ub,
			    u16 q_id, u16 tag, struct ublk_io *io,
			    unsigned index, unsigned issue_flags)
{
	unsigned new_registered_buffers;
	struct request *req = io->req;
	int ret;

	/*
	 * Ensure there are still references for ublk_sub_req_ref() to release.
	 * If not, fall back on the thread-safe buffer registration.
	 */
	new_registered_buffers = io->task_registered_buffers + 1;
	if (unlikely(new_registered_buffers >= UBLK_REFCOUNT_INIT))
		return ublk_register_io_buf(cmd, ub, q_id, tag, io, index,
					    issue_flags);

	if (!ublk_dev_support_zero_copy(ub) || !ublk_rq_has_data(req))
		return -EINVAL;

	ret = io_buffer_register_bvec(cmd, req, ublk_io_release, index,
				      issue_flags);
	if (ret)
		return ret;

	io->task_registered_buffers = new_registered_buffers;
	return 0;
}

static int ublk_unregister_io_buf(struct io_uring_cmd *cmd,
				  const struct ublk_device *ub,
				  unsigned int index, unsigned int issue_flags)
{
	if (!(ub->dev_info.flags & UBLK_F_SUPPORT_ZERO_COPY))
		return -EINVAL;

	return io_buffer_unregister_bvec(cmd, index, issue_flags);
}

static int ublk_check_fetch_buf(const struct ublk_device *ub, __u64 buf_addr)
{
	if (ublk_dev_need_map_io(ub)) {
		/*
		 * FETCH_RQ has to provide IO buffer if NEED GET
		 * DATA is not enabled
		 */
		if (!buf_addr && !ublk_dev_need_get_data(ub))
			return -EINVAL;
	} else if (buf_addr) {
		/* User copy requires addr to be unset */
		return -EINVAL;
	}
	return 0;
}

static int ublk_fetch(struct io_uring_cmd *cmd, struct ublk_device *ub,
		      struct ublk_io *io, __u64 buf_addr)
{
	int ret = 0;

	/*
	 * When handling FETCH command for setting up ublk uring queue,
	 * ub->mutex is the innermost lock, and we won't block for handling
	 * FETCH, so it is fine even for IO_URING_F_NONBLOCK.
	 */
	mutex_lock(&ub->mutex);
	/* UBLK_IO_FETCH_REQ is only allowed before dev is setup */
	if (ublk_dev_ready(ub)) {
		ret = -EBUSY;
		goto out;
	}

	/* allow each command to be FETCHed at most once */
	if (io->flags & UBLK_IO_FLAG_ACTIVE) {
		ret = -EINVAL;
		goto out;
	}

	WARN_ON_ONCE(io->flags & UBLK_IO_FLAG_OWNED_BY_SRV);

	ublk_fill_io_cmd(io, cmd);
	ret = ublk_config_io_buf(ub, io, cmd, buf_addr, NULL);
	if (ret)
		goto out;

	WRITE_ONCE(io->task, get_task_struct(current));
	ublk_mark_io_ready(ub);
out:
	mutex_unlock(&ub->mutex);
	return ret;
}

static int ublk_check_commit_and_fetch(const struct ublk_device *ub,
				       struct ublk_io *io, __u64 buf_addr)
{
	struct request *req = io->req;

	if (ublk_dev_need_map_io(ub)) {
		/*
		 * COMMIT_AND_FETCH_REQ has to provide IO buffer if
		 * NEED GET DATA is not enabled or it is Read IO.
		 */
		if (!buf_addr && (!ublk_dev_need_get_data(ub) ||
					req_op(req) == REQ_OP_READ))
			return -EINVAL;
	} else if (req_op(req) != REQ_OP_ZONE_APPEND && buf_addr) {
		/*
		 * User copy requires addr to be unset when command is
		 * not zone append
		 */
		return -EINVAL;
	}

	return 0;
}

static bool ublk_need_complete_req(const struct ublk_device *ub,
				   struct ublk_io *io)
{
	if (ublk_dev_need_req_ref(ub))
		return ublk_sub_req_ref(io);
	return true;
}

static bool ublk_get_data(const struct ublk_queue *ubq, struct ublk_io *io,
			  struct request *req)
{
	/*
	 * We have handled UBLK_IO_NEED_GET_DATA command,
	 * so clear UBLK_IO_FLAG_NEED_GET_DATA now and just
	 * do the copy work.
	 */
	io->flags &= ~UBLK_IO_FLAG_NEED_GET_DATA;
	/* update iod->addr because ublksrv may have passed a new io buffer */
	ublk_get_iod(ubq, req->tag)->addr = io->addr;
	pr_devel("%s: update iod->addr: qid %d tag %d io_flags %x addr %llx\n",
			__func__, ubq->q_id, req->tag, io->flags,
			ublk_get_iod(ubq, req->tag)->addr);

	return ublk_start_io(ubq, req, io);
}

static int ublk_ch_uring_cmd_local(struct io_uring_cmd *cmd,
		unsigned int issue_flags)
{
	/* May point to userspace-mapped memory */
	const struct ublksrv_io_cmd *ub_src = io_uring_sqe_cmd(cmd->sqe);
	u16 buf_idx = UBLK_INVALID_BUF_IDX;
	struct ublk_device *ub = cmd->file->private_data;
	struct ublk_queue *ubq;
	struct ublk_io *io;
	u32 cmd_op = cmd->cmd_op;
	u16 q_id = READ_ONCE(ub_src->q_id);
	u16 tag = READ_ONCE(ub_src->tag);
	s32 result = READ_ONCE(ub_src->result);
	u64 addr = READ_ONCE(ub_src->addr); /* unioned with zone_append_lba */
	struct request *req;
	int ret;
	bool compl;

	WARN_ON_ONCE(issue_flags & IO_URING_F_UNLOCKED);

	pr_devel("%s: received: cmd op %d queue %d tag %d result %d\n",
			__func__, cmd->cmd_op, q_id, tag, result);

	ret = ublk_check_cmd_op(cmd_op);
	if (ret)
		goto out;

	/*
	 * io_buffer_unregister_bvec() doesn't access the ubq or io,
	 * so no need to validate the q_id, tag, or task
	 */
	if (_IOC_NR(cmd_op) == UBLK_IO_UNREGISTER_IO_BUF)
		return ublk_unregister_io_buf(cmd, ub, addr, issue_flags);

	ret = -EINVAL;
	if (q_id >= ub->dev_info.nr_hw_queues)
		goto out;

	ubq = ublk_get_queue(ub, q_id);

	if (tag >= ub->dev_info.queue_depth)
		goto out;

	io = &ubq->ios[tag];
	/* UBLK_IO_FETCH_REQ can be handled on any task, which sets io->task */
	if (unlikely(_IOC_NR(cmd_op) == UBLK_IO_FETCH_REQ)) {
		ret = ublk_check_fetch_buf(ub, addr);
		if (ret)
			goto out;
		ret = ublk_fetch(cmd, ub, io, addr);
		if (ret)
			goto out;

		ublk_prep_cancel(cmd, issue_flags, ubq, tag);
		return -EIOCBQUEUED;
	}

	if (READ_ONCE(io->task) != current) {
		/*
		 * ublk_register_io_buf() accesses only the io's refcount,
		 * so can be handled on any task
		 */
		if (_IOC_NR(cmd_op) == UBLK_IO_REGISTER_IO_BUF)
			return ublk_register_io_buf(cmd, ub, q_id, tag, io,
						    addr, issue_flags);

		goto out;
	}

	/* there is pending io cmd, something must be wrong */
	if (!(io->flags & UBLK_IO_FLAG_OWNED_BY_SRV)) {
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

	switch (_IOC_NR(cmd_op)) {
	case UBLK_IO_REGISTER_IO_BUF:
		return ublk_daemon_register_io_buf(cmd, ub, q_id, tag, io, addr,
						   issue_flags);
	case UBLK_IO_COMMIT_AND_FETCH_REQ:
		ret = ublk_check_commit_and_fetch(ub, io, addr);
		if (ret)
			goto out;
		io->res = result;
		req = ublk_fill_io_cmd(io, cmd);
		ret = ublk_config_io_buf(ub, io, cmd, addr, &buf_idx);
		compl = ublk_need_complete_req(ub, io);

		/* can't touch 'ublk_io' any more */
		if (buf_idx != UBLK_INVALID_BUF_IDX)
			io_buffer_unregister_bvec(cmd, buf_idx, issue_flags);
		if (req_op(req) == REQ_OP_ZONE_APPEND)
			req->__sector = addr;
		if (compl)
			__ublk_complete_rq(req, io, ublk_dev_need_map_io(ub));

		if (ret)
			goto out;
		break;
	case UBLK_IO_NEED_GET_DATA:
		/*
		 * ublk_get_data() may fail and fallback to requeue, so keep
		 * uring_cmd active first and prepare for handling new requeued
		 * request
		 */
		req = ublk_fill_io_cmd(io, cmd);
		ret = ublk_config_io_buf(ub, io, cmd, addr, NULL);
		WARN_ON_ONCE(ret);
		if (likely(ublk_get_data(ubq, io, req))) {
			__ublk_prep_compl_io_cmd(io, req);
			return UBLK_IO_RES_OK;
		}
		break;
	default:
		goto out;
	}
	ublk_prep_cancel(cmd, issue_flags, ubq, tag);
	return -EIOCBQUEUED;

 out:
	pr_devel("%s: complete: cmd op %d, tag %d ret %x io_flags %x\n",
			__func__, cmd_op, tag, ret, io->flags);
	return ret;
}

static inline struct request *__ublk_check_and_get_req(struct ublk_device *ub,
		u16 q_id, u16 tag, struct ublk_io *io, size_t offset)
{
	struct request *req;

	/*
	 * can't use io->req in case of concurrent UBLK_IO_COMMIT_AND_FETCH_REQ,
	 * which would overwrite it with io->cmd
	 */
	req = blk_mq_tag_to_rq(ub->tag_set.tags[q_id], tag);
	if (!req)
		return NULL;

	if (!ublk_get_req_ref(io))
		return NULL;

	if (unlikely(!blk_mq_request_started(req) || req->tag != tag))
		goto fail_put;

	if (!ublk_rq_has_data(req))
		goto fail_put;

	if (offset > blk_rq_bytes(req))
		goto fail_put;

	return req;
fail_put:
	ublk_put_req_ref(io, req);
	return NULL;
}

static void ublk_ch_uring_cmd_cb(struct io_uring_cmd *cmd,
		unsigned int issue_flags)
{
	int ret = ublk_ch_uring_cmd_local(cmd, issue_flags);

	if (ret != -EIOCBQUEUED)
		io_uring_cmd_done(cmd, ret, issue_flags);
}

static int ublk_ch_uring_cmd(struct io_uring_cmd *cmd, unsigned int issue_flags)
{
	if (unlikely(issue_flags & IO_URING_F_CANCEL)) {
		ublk_uring_cmd_cancel_fn(cmd, issue_flags);
		return 0;
	}

	/* well-implemented server won't run into unlocked */
	if (unlikely(issue_flags & IO_URING_F_UNLOCKED)) {
		io_uring_cmd_complete_in_task(cmd, ublk_ch_uring_cmd_cb);
		return -EIOCBQUEUED;
	}

	return ublk_ch_uring_cmd_local(cmd, issue_flags);
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
		struct iov_iter *iter, size_t *off, int dir,
		struct ublk_io **io)
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
	if (!ublk_dev_support_user_copy(ub))
		return ERR_PTR(-EACCES);

	if (tag >= ub->dev_info.queue_depth)
		return ERR_PTR(-EINVAL);

	*io = &ubq->ios[tag];
	req = __ublk_check_and_get_req(ub, q_id, tag, *io, buf_off);
	if (!req)
		return ERR_PTR(-EINVAL);

	if (!req->mq_hctx || !req->mq_hctx->driver_data)
		goto fail;

	if (!ublk_check_ubuf_dir(req, dir))
		goto fail;

	*off = buf_off;
	return req;
fail:
	ublk_put_req_ref(*io, req);
	return ERR_PTR(-EACCES);
}

static ssize_t ublk_ch_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct request *req;
	struct ublk_io *io;
	size_t buf_off;
	size_t ret;

	req = ublk_check_and_get_req(iocb, to, &buf_off, ITER_DEST, &io);
	if (IS_ERR(req))
		return PTR_ERR(req);

	ret = ublk_copy_user_pages(req, buf_off, to, ITER_DEST);
	ublk_put_req_ref(io, req);

	return ret;
}

static ssize_t ublk_ch_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct request *req;
	struct ublk_io *io;
	size_t buf_off;
	size_t ret;

	req = ublk_check_and_get_req(iocb, from, &buf_off, ITER_SOURCE, &io);
	if (IS_ERR(req))
		return PTR_ERR(req);

	ret = ublk_copy_user_pages(req, buf_off, from, ITER_SOURCE);
	ublk_put_req_ref(io, req);

	return ret;
}

static const struct file_operations ublk_ch_fops = {
	.owner = THIS_MODULE,
	.open = ublk_ch_open,
	.release = ublk_ch_release,
	.read_iter = ublk_ch_read_iter,
	.write_iter = ublk_ch_write_iter,
	.uring_cmd = ublk_ch_uring_cmd,
	.mmap = ublk_ch_mmap,
};

static void ublk_deinit_queue(struct ublk_device *ub, int q_id)
{
	int size = ublk_queue_cmd_buf_size(ub);
	struct ublk_queue *ubq = ublk_get_queue(ub, q_id);
	int i;

	for (i = 0; i < ubq->q_depth; i++) {
		struct ublk_io *io = &ubq->ios[i];
		if (io->task)
			put_task_struct(io->task);
		WARN_ON_ONCE(refcount_read(&io->ref));
		WARN_ON_ONCE(io->task_registered_buffers);
	}

	if (ubq->io_cmd_buf)
		free_pages((unsigned long)ubq->io_cmd_buf, get_order(size));
}

static int ublk_init_queue(struct ublk_device *ub, int q_id)
{
	struct ublk_queue *ubq = ublk_get_queue(ub, q_id);
	gfp_t gfp_flags = GFP_KERNEL | __GFP_ZERO;
	void *ptr;
	int size;

	spin_lock_init(&ubq->cancel_lock);
	ubq->flags = ub->dev_info.flags;
	ubq->q_id = q_id;
	ubq->q_depth = ub->dev_info.queue_depth;
	size = ublk_queue_cmd_buf_size(ub);

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
	kvfree(ub->__queues);
}

static int ublk_init_queues(struct ublk_device *ub)
{
	int nr_queues = ub->dev_info.nr_hw_queues;
	int depth = ub->dev_info.queue_depth;
	int ubq_size = sizeof(struct ublk_queue) + depth * sizeof(struct ublk_io);
	int i, ret = -ENOMEM;

	ub->queue_size = ubq_size;
	ub->__queues = kvcalloc(nr_queues, ubq_size, GFP_KERNEL);
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
		err = idr_alloc(&ublk_index_idr, ub, 0, UBLK_MAX_UBLKS,
				GFP_NOWAIT);
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
	mutex_destroy(&ub->cancel_mutex);
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

	if (ub->dev_info.flags & UBLK_F_UNPRIVILEGED_DEV)
		unprivileged_ublks_added++;
	return 0;
 fail:
	put_device(dev);
	return ret;
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
	ub->tag_set.driver_data = ub;
	return blk_mq_alloc_tag_set(&ub->tag_set);
}

static void ublk_remove(struct ublk_device *ub)
{
	bool unprivileged;

	ublk_stop_dev(ub);
	cdev_device_del(&ub->cdev, &ub->cdev_dev);
	unprivileged = ub->dev_info.flags & UBLK_F_UNPRIVILEGED_DEV;
	ublk_put_device(ub);

	if (unprivileged)
		unprivileged_ublks_added--;
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

static int ublk_ctrl_start_dev(struct ublk_device *ub,
		const struct ublksrv_ctrl_cmd *header)
{
	const struct ublk_param_basic *p = &ub->params.basic;
	int ublksrv_pid = (int)header->data[0];
	struct queue_limits lim = {
		.logical_block_size	= 1 << p->logical_bs_shift,
		.physical_block_size	= 1 << p->physical_bs_shift,
		.io_min			= 1 << p->io_min_shift,
		.io_opt			= 1 << p->io_opt_shift,
		.max_hw_sectors		= p->max_sectors,
		.chunk_sectors		= p->chunk_sectors,
		.virt_boundary_mask	= p->virt_boundary_mask,
		.max_segments		= USHRT_MAX,
		.max_segment_size	= UINT_MAX,
		.dma_alignment		= 3,
	};
	struct gendisk *disk;
	int ret = -EINVAL;

	if (ublksrv_pid <= 0)
		return -EINVAL;
	if (!(ub->params.types & UBLK_PARAM_TYPE_BASIC))
		return -EINVAL;

	if (ub->params.types & UBLK_PARAM_TYPE_DISCARD) {
		const struct ublk_param_discard *pd = &ub->params.discard;

		lim.discard_alignment = pd->discard_alignment;
		lim.discard_granularity = pd->discard_granularity;
		lim.max_hw_discard_sectors = pd->max_discard_sectors;
		lim.max_write_zeroes_sectors = pd->max_write_zeroes_sectors;
		lim.max_discard_segments = pd->max_discard_segments;
	}

	if (ub->params.types & UBLK_PARAM_TYPE_ZONED) {
		const struct ublk_param_zoned *p = &ub->params.zoned;

		if (!IS_ENABLED(CONFIG_BLK_DEV_ZONED))
			return -EOPNOTSUPP;

		lim.features |= BLK_FEAT_ZONED;
		lim.max_active_zones = p->max_active_zones;
		lim.max_open_zones =  p->max_open_zones;
		lim.max_hw_zone_append_sectors = p->max_zone_append_sectors;
	}

	if (ub->params.basic.attrs & UBLK_ATTR_VOLATILE_CACHE) {
		lim.features |= BLK_FEAT_WRITE_CACHE;
		if (ub->params.basic.attrs & UBLK_ATTR_FUA)
			lim.features |= BLK_FEAT_FUA;
	}

	if (ub->params.basic.attrs & UBLK_ATTR_ROTATIONAL)
		lim.features |= BLK_FEAT_ROTATIONAL;

	if (ub->params.types & UBLK_PARAM_TYPE_DMA_ALIGN)
		lim.dma_alignment = ub->params.dma.alignment;

	if (ub->params.types & UBLK_PARAM_TYPE_SEGMENT) {
		lim.seg_boundary_mask = ub->params.seg.seg_boundary_mask;
		lim.max_segment_size = ub->params.seg.max_segment_size;
		lim.max_segments = ub->params.seg.max_segments;
	}

	if (wait_for_completion_interruptible(&ub->completion) != 0)
		return -EINTR;

	if (ub->ublksrv_tgid != ublksrv_pid)
		return -EINVAL;

	mutex_lock(&ub->mutex);
	if (ub->dev_info.state == UBLK_S_DEV_LIVE ||
	    test_bit(UB_STATE_USED, &ub->state)) {
		ret = -EEXIST;
		goto out_unlock;
	}

	disk = blk_mq_alloc_disk(&ub->tag_set, &lim, NULL);
	if (IS_ERR(disk)) {
		ret = PTR_ERR(disk);
		goto out_unlock;
	}
	sprintf(disk->disk_name, "ublkb%d", ub->ub_number);
	disk->fops = &ub_fops;
	disk->private_data = ub;

	ub->dev_info.ublksrv_pid = ublksrv_pid;
	ub->ub_disk = disk;

	ublk_apply_params(ub);

	/* don't probe partitions if any daemon task is un-trusted */
	if (ub->unprivileged_daemons)
		set_bit(GD_SUPPRESS_PART_SCAN, &disk->state);

	ublk_get_device(ub);
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
		ublk_detach_disk(ub);
		ublk_put_device(ub);
	}
	if (ret)
		put_disk(disk);
out_unlock:
	mutex_unlock(&ub->mutex);
	return ret;
}

static int ublk_ctrl_get_queue_affinity(struct ublk_device *ub,
		const struct ublksrv_ctrl_cmd *header)
{
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

static int ublk_ctrl_add_dev(const struct ublksrv_ctrl_cmd *header)
{
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

	if (info.queue_depth > UBLK_MAX_QUEUE_DEPTH || !info.queue_depth ||
	    info.nr_hw_queues > UBLK_MAX_NR_QUEUES || !info.nr_hw_queues)
		return -EINVAL;

	if (capable(CAP_SYS_ADMIN))
		info.flags &= ~UBLK_F_UNPRIVILEGED_DEV;
	else if (!(info.flags & UBLK_F_UNPRIVILEGED_DEV))
		return -EPERM;

	/* forbid nonsense combinations of recovery flags */
	switch (info.flags & UBLK_F_ALL_RECOVERY_FLAGS) {
	case 0:
	case UBLK_F_USER_RECOVERY:
	case (UBLK_F_USER_RECOVERY | UBLK_F_USER_RECOVERY_REISSUE):
	case (UBLK_F_USER_RECOVERY | UBLK_F_USER_RECOVERY_FAIL_IO):
		break;
	default:
		pr_warn("%s: invalid recovery flags %llx\n", __func__,
			info.flags & UBLK_F_ALL_RECOVERY_FLAGS);
		return -EINVAL;
	}

	if ((info.flags & UBLK_F_QUIESCE) && !(info.flags & UBLK_F_USER_RECOVERY)) {
		pr_warn("UBLK_F_QUIESCE requires UBLK_F_USER_RECOVERY\n");
		return -EINVAL;
	}

	/*
	 * unprivileged device can't be trusted, but RECOVERY and
	 * RECOVERY_REISSUE still may hang error handling, so can't
	 * support recovery features for unprivileged ublk now
	 *
	 * TODO: provide forward progress for RECOVERY handler, so that
	 * unprivileged device can benefit from it
	 */
	if (info.flags & UBLK_F_UNPRIVILEGED_DEV) {
		info.flags &= ~(UBLK_F_USER_RECOVERY_REISSUE |
				UBLK_F_USER_RECOVERY);

		/*
		 * For USER_COPY, we depends on userspace to fill request
		 * buffer by pwrite() to ublk char device, which can't be
		 * used for unprivileged device
		 *
		 * Same with zero copy or auto buffer register.
		 */
		if (info.flags & (UBLK_F_USER_COPY | UBLK_F_SUPPORT_ZERO_COPY |
					UBLK_F_AUTO_BUF_REG))
			return -EINVAL;
	}

	/* the created device is always owned by current user */
	ublk_store_owner_uid_gid(&info.owner_uid, &info.owner_gid);

	if (header->dev_id != info.dev_id) {
		pr_warn("%s: dev id not match %u %u\n",
			__func__, header->dev_id, info.dev_id);
		return -EINVAL;
	}

	if (header->dev_id != U32_MAX && header->dev_id >= UBLK_MAX_UBLKS) {
		pr_warn("%s: dev id is too large. Max supported is %d\n",
			__func__, UBLK_MAX_UBLKS - 1);
		return -EINVAL;
	}

	ublk_dump_dev_info(&info);

	ret = mutex_lock_killable(&ublk_ctl_mutex);
	if (ret)
		return ret;

	ret = -EACCES;
	if ((info.flags & UBLK_F_UNPRIVILEGED_DEV) &&
	    unprivileged_ublks_added >= unprivileged_ublks_max)
		goto out_unlock;

	ret = -ENOMEM;
	ub = kzalloc(sizeof(*ub), GFP_KERNEL);
	if (!ub)
		goto out_unlock;
	mutex_init(&ub->mutex);
	spin_lock_init(&ub->lock);
	mutex_init(&ub->cancel_mutex);

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
		UBLK_F_URING_CMD_COMP_IN_TASK |
		UBLK_F_PER_IO_DAEMON |
		UBLK_F_BUF_REG_OFF_DAEMON;

	/* GET_DATA isn't needed any more with USER_COPY or ZERO COPY */
	if (ub->dev_info.flags & (UBLK_F_USER_COPY | UBLK_F_SUPPORT_ZERO_COPY |
				UBLK_F_AUTO_BUF_REG))
		ub->dev_info.flags &= ~UBLK_F_NEED_GET_DATA;

	/*
	 * Zoned storage support requires reuse `ublksrv_io_cmd->addr` for
	 * returning write_append_lba, which is only allowed in case of
	 * user copy or zero copy
	 */
	if (ublk_dev_is_zoned(ub) &&
	    (!IS_ENABLED(CONFIG_BLK_DEV_ZONED) || !(ub->dev_info.flags &
	     (UBLK_F_USER_COPY | UBLK_F_SUPPORT_ZERO_COPY)))) {
		ret = -EINVAL;
		goto out_free_dev_number;
	}

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
	mutex_destroy(&ub->cancel_mutex);
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

static int ublk_ctrl_del_dev(struct ublk_device **p_ub, bool wait)
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
	if (wait && wait_event_interruptible(ublk_idr_wq, ublk_idr_freed(idx)))
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
	return 0;
}

static int ublk_ctrl_get_dev_info(struct ublk_device *ub,
		const struct ublksrv_ctrl_cmd *header)
{
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
		const struct ublksrv_ctrl_cmd *header)
{
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
		const struct ublksrv_ctrl_cmd *header)
{
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

	mutex_lock(&ub->mutex);
	if (test_bit(UB_STATE_USED, &ub->state)) {
		/*
		 * Parameters can only be changed when device hasn't
		 * been started yet
		 */
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

static int ublk_ctrl_start_recovery(struct ublk_device *ub,
		const struct ublksrv_ctrl_cmd *header)
{
	int ret = -EINVAL;

	mutex_lock(&ub->mutex);
	if (ublk_nosrv_should_stop_dev(ub))
		goto out_unlock;
	/*
	 * START_RECOVERY is only allowd after:
	 *
	 * (1) UB_STATE_OPEN is not set, which means the dying process is exited
	 *     and related io_uring ctx is freed so file struct of /dev/ublkcX is
	 *     released.
	 *
	 * and one of the following holds
	 *
	 * (2) UBLK_S_DEV_QUIESCED is set, which means the quiesce_work:
	 *     (a)has quiesced request queue
	 *     (b)has requeued every inflight rqs whose io_flags is ACTIVE
	 *     (c)has requeued/aborted every inflight rqs whose io_flags is NOT ACTIVE
	 *     (d)has completed/camceled all ioucmds owned by ther dying process
	 *
	 * (3) UBLK_S_DEV_FAIL_IO is set, which means the queue is not
	 *     quiesced, but all I/O is being immediately errored
	 */
	if (test_bit(UB_STATE_OPEN, &ub->state) || !ublk_dev_in_recoverable_state(ub)) {
		ret = -EBUSY;
		goto out_unlock;
	}
	pr_devel("%s: start recovery for dev id %d.\n", __func__, header->dev_id);
	init_completion(&ub->completion);
	ret = 0;
 out_unlock:
	mutex_unlock(&ub->mutex);
	return ret;
}

static int ublk_ctrl_end_recovery(struct ublk_device *ub,
		const struct ublksrv_ctrl_cmd *header)
{
	int ublksrv_pid = (int)header->data[0];
	int ret = -EINVAL;

	pr_devel("%s: Waiting for all FETCH_REQs, dev id %d...\n", __func__,
		 header->dev_id);

	if (wait_for_completion_interruptible(&ub->completion))
		return -EINTR;

	pr_devel("%s: All FETCH_REQs received, dev id %d\n", __func__,
		 header->dev_id);

	if (ub->ublksrv_tgid != ublksrv_pid)
		return -EINVAL;

	mutex_lock(&ub->mutex);
	if (ublk_nosrv_should_stop_dev(ub))
		goto out_unlock;

	if (!ublk_dev_in_recoverable_state(ub)) {
		ret = -EBUSY;
		goto out_unlock;
	}
	ub->dev_info.ublksrv_pid = ublksrv_pid;
	ub->dev_info.state = UBLK_S_DEV_LIVE;
	pr_devel("%s: new ublksrv_pid %d, dev id %d\n",
			__func__, ublksrv_pid, header->dev_id);
	blk_mq_kick_requeue_list(ub->ub_disk->queue);
	ret = 0;
 out_unlock:
	mutex_unlock(&ub->mutex);
	return ret;
}

static int ublk_ctrl_get_features(const struct ublksrv_ctrl_cmd *header)
{
	void __user *argp = (void __user *)(unsigned long)header->addr;
	u64 features = UBLK_F_ALL;

	if (header->len != UBLK_FEATURES_LEN || !header->addr)
		return -EINVAL;

	if (copy_to_user(argp, &features, UBLK_FEATURES_LEN))
		return -EFAULT;

	return 0;
}

static void ublk_ctrl_set_size(struct ublk_device *ub, const struct ublksrv_ctrl_cmd *header)
{
	struct ublk_param_basic *p = &ub->params.basic;
	u64 new_size = header->data[0];

	mutex_lock(&ub->mutex);
	p->dev_sectors = new_size;
	set_capacity_and_notify(ub->ub_disk, p->dev_sectors);
	mutex_unlock(&ub->mutex);
}

struct count_busy {
	const struct ublk_queue *ubq;
	unsigned int nr_busy;
};

static bool ublk_count_busy_req(struct request *rq, void *data)
{
	struct count_busy *idle = data;

	if (!blk_mq_request_started(rq) && rq->mq_hctx->driver_data == idle->ubq)
		idle->nr_busy += 1;
	return true;
}

/* uring_cmd is guaranteed to be active if the associated request is idle */
static bool ubq_has_idle_io(const struct ublk_queue *ubq)
{
	struct count_busy data = {
		.ubq = ubq,
	};

	blk_mq_tagset_busy_iter(&ubq->dev->tag_set, ublk_count_busy_req, &data);
	return data.nr_busy < ubq->q_depth;
}

/* Wait until each hw queue has at least one idle IO */
static int ublk_wait_for_idle_io(struct ublk_device *ub,
				 unsigned int timeout_ms)
{
	unsigned int elapsed = 0;
	int ret;

	while (elapsed < timeout_ms && !signal_pending(current)) {
		unsigned int queues_cancelable = 0;
		int i;

		for (i = 0; i < ub->dev_info.nr_hw_queues; i++) {
			struct ublk_queue *ubq = ublk_get_queue(ub, i);

			queues_cancelable += !!ubq_has_idle_io(ubq);
		}

		/*
		 * Each queue needs at least one active command for
		 * notifying ublk server
		 */
		if (queues_cancelable == ub->dev_info.nr_hw_queues)
			break;

		msleep(UBLK_REQUEUE_DELAY_MS);
		elapsed += UBLK_REQUEUE_DELAY_MS;
	}

	if (signal_pending(current))
		ret = -EINTR;
	else if (elapsed >= timeout_ms)
		ret = -EBUSY;
	else
		ret = 0;

	return ret;
}

static int ublk_ctrl_quiesce_dev(struct ublk_device *ub,
				 const struct ublksrv_ctrl_cmd *header)
{
	/* zero means wait forever */
	u64 timeout_ms = header->data[0];
	struct gendisk *disk;
	int ret = -ENODEV;

	if (!(ub->dev_info.flags & UBLK_F_QUIESCE))
		return -EOPNOTSUPP;

	mutex_lock(&ub->mutex);
	disk = ublk_get_disk(ub);
	if (!disk)
		goto unlock;
	if (ub->dev_info.state == UBLK_S_DEV_DEAD)
		goto put_disk;

	ret = 0;
	/* already in expected state */
	if (ub->dev_info.state != UBLK_S_DEV_LIVE)
		goto put_disk;

	/* Mark the device as canceling */
	mutex_lock(&ub->cancel_mutex);
	blk_mq_quiesce_queue(disk->queue);
	ublk_set_canceling(ub, true);
	blk_mq_unquiesce_queue(disk->queue);
	mutex_unlock(&ub->cancel_mutex);

	if (!timeout_ms)
		timeout_ms = UINT_MAX;
	ret = ublk_wait_for_idle_io(ub, timeout_ms);

put_disk:
	ublk_put_disk(disk);
unlock:
	mutex_unlock(&ub->mutex);

	/* Cancel pending uring_cmd */
	if (!ret)
		ublk_cancel_dev(ub);
	return ret;
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
	case UBLK_CMD_UPDATE_SIZE:
	case UBLK_CMD_QUIESCE_DEV:
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
		ret = ublk_ctrl_get_features(header);
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
		ret = ublk_ctrl_start_dev(ub, header);
		break;
	case UBLK_CMD_STOP_DEV:
		ret = ublk_ctrl_stop_dev(ub);
		break;
	case UBLK_CMD_GET_DEV_INFO:
	case UBLK_CMD_GET_DEV_INFO2:
		ret = ublk_ctrl_get_dev_info(ub, header);
		break;
	case UBLK_CMD_ADD_DEV:
		ret = ublk_ctrl_add_dev(header);
		break;
	case UBLK_CMD_DEL_DEV:
		ret = ublk_ctrl_del_dev(&ub, true);
		break;
	case UBLK_CMD_DEL_DEV_ASYNC:
		ret = ublk_ctrl_del_dev(&ub, false);
		break;
	case UBLK_CMD_GET_QUEUE_AFFINITY:
		ret = ublk_ctrl_get_queue_affinity(ub, header);
		break;
	case UBLK_CMD_GET_PARAMS:
		ret = ublk_ctrl_get_params(ub, header);
		break;
	case UBLK_CMD_SET_PARAMS:
		ret = ublk_ctrl_set_params(ub, header);
		break;
	case UBLK_CMD_START_USER_RECOVERY:
		ret = ublk_ctrl_start_recovery(ub, header);
		break;
	case UBLK_CMD_END_USER_RECOVERY:
		ret = ublk_ctrl_end_recovery(ub, header);
		break;
	case UBLK_CMD_UPDATE_SIZE:
		ublk_ctrl_set_size(ub, header);
		ret = 0;
		break;
	case UBLK_CMD_QUIESCE_DEV:
		ret = ublk_ctrl_quiesce_dev(ub, header);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

 put_dev:
	if (ub)
		ublk_put_device(ub);
 out:
	pr_devel("%s: cmd done ret %d cmd_op %x, dev id %d qid %d\n",
			__func__, ret, cmd->cmd_op, header->dev_id, header->queue_id);
	return ret;
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
	BUILD_BUG_ON(sizeof(struct ublk_auto_buf_reg) != 8);

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

static int ublk_set_max_unprivileged_ublks(const char *buf,
					   const struct kernel_param *kp)
{
	return param_set_uint_minmax(buf, kp, 0, UBLK_MAX_UBLKS);
}

static int ublk_get_max_unprivileged_ublks(char *buf,
					   const struct kernel_param *kp)
{
	return sysfs_emit(buf, "%u\n", unprivileged_ublks_max);
}

static const struct kernel_param_ops ublk_max_unprivileged_ublks_ops = {
	.set = ublk_set_max_unprivileged_ublks,
	.get = ublk_get_max_unprivileged_ublks,
};

module_param_cb(ublks_max, &ublk_max_unprivileged_ublks_ops,
		&unprivileged_ublks_max, 0644);
MODULE_PARM_DESC(ublks_max, "max number of unprivileged ublk devices allowed to add(default: 64)");

MODULE_AUTHOR("Ming Lei <ming.lei@redhat.com>");
MODULE_DESCRIPTION("Userspace block device");
MODULE_LICENSE("GPL");
