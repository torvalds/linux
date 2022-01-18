// SPDX-License-Identifier: GPL-2.0-only
//#define DEBUG
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/virtio.h>
#include <linux/virtio_blk.h>
#include <linux/scatterlist.h>
#include <linux/string_helpers.h>
#include <linux/idr.h>
#include <linux/blk-mq.h>
#include <linux/blk-mq-virtio.h>
#include <linux/numa.h>
#include <uapi/linux/virtio_ring.h>

#define PART_BITS 4
#define VQ_NAME_LEN 16
#define MAX_DISCARD_SEGMENTS 256u

/* The maximum number of sg elements that fit into a virtqueue */
#define VIRTIO_BLK_MAX_SG_ELEMS 32768

#ifdef CONFIG_ARCH_NO_SG_CHAIN
#define VIRTIO_BLK_INLINE_SG_CNT	0
#else
#define VIRTIO_BLK_INLINE_SG_CNT	2
#endif

static unsigned int num_request_queues;
module_param(num_request_queues, uint, 0644);
MODULE_PARM_DESC(num_request_queues,
		 "Limit the number of request queues to use for blk device. "
		 "0 for no limit. "
		 "Values > nr_cpu_ids truncated to nr_cpu_ids.");

static int major;
static DEFINE_IDA(vd_index_ida);

static struct workqueue_struct *virtblk_wq;

struct virtio_blk_vq {
	struct virtqueue *vq;
	spinlock_t lock;
	char name[VQ_NAME_LEN];
} ____cacheline_aligned_in_smp;

struct virtio_blk {
	/*
	 * This mutex must be held by anything that may run after
	 * virtblk_remove() sets vblk->vdev to NULL.
	 *
	 * blk-mq, virtqueue processing, and sysfs attribute code paths are
	 * shut down before vblk->vdev is set to NULL and therefore do not need
	 * to hold this mutex.
	 */
	struct mutex vdev_mutex;
	struct virtio_device *vdev;

	/* The disk structure for the kernel. */
	struct gendisk *disk;

	/* Block layer tags. */
	struct blk_mq_tag_set tag_set;

	/* Process context for config space updates */
	struct work_struct config_work;

	/*
	 * Tracks references from block_device_operations open/release and
	 * virtio_driver probe/remove so this object can be freed once no
	 * longer in use.
	 */
	refcount_t refs;

	/* What host tells us, plus 2 for header & tailer. */
	unsigned int sg_elems;

	/* Ida index - used to track minor number allocations. */
	int index;

	/* num of vqs */
	int num_vqs;
	struct virtio_blk_vq *vqs;
};

struct virtblk_req {
	struct virtio_blk_outhdr out_hdr;
	u8 status;
	struct sg_table sg_table;
	struct scatterlist sg[];
};

static inline blk_status_t virtblk_result(struct virtblk_req *vbr)
{
	switch (vbr->status) {
	case VIRTIO_BLK_S_OK:
		return BLK_STS_OK;
	case VIRTIO_BLK_S_UNSUPP:
		return BLK_STS_NOTSUPP;
	default:
		return BLK_STS_IOERR;
	}
}

static int virtblk_add_req(struct virtqueue *vq, struct virtblk_req *vbr,
		struct scatterlist *data_sg, bool have_data)
{
	struct scatterlist hdr, status, *sgs[3];
	unsigned int num_out = 0, num_in = 0;

	sg_init_one(&hdr, &vbr->out_hdr, sizeof(vbr->out_hdr));
	sgs[num_out++] = &hdr;

	if (have_data) {
		if (vbr->out_hdr.type & cpu_to_virtio32(vq->vdev, VIRTIO_BLK_T_OUT))
			sgs[num_out++] = data_sg;
		else
			sgs[num_out + num_in++] = data_sg;
	}

	sg_init_one(&status, &vbr->status, sizeof(vbr->status));
	sgs[num_out + num_in++] = &status;

	return virtqueue_add_sgs(vq, sgs, num_out, num_in, vbr, GFP_ATOMIC);
}

static int virtblk_setup_discard_write_zeroes(struct request *req, bool unmap)
{
	unsigned short segments = blk_rq_nr_discard_segments(req);
	unsigned short n = 0;
	struct virtio_blk_discard_write_zeroes *range;
	struct bio *bio;
	u32 flags = 0;

	if (unmap)
		flags |= VIRTIO_BLK_WRITE_ZEROES_FLAG_UNMAP;

	range = kmalloc_array(segments, sizeof(*range), GFP_ATOMIC);
	if (!range)
		return -ENOMEM;

	/*
	 * Single max discard segment means multi-range discard isn't
	 * supported, and block layer only runs contiguity merge like
	 * normal RW request. So we can't reply on bio for retrieving
	 * each range info.
	 */
	if (queue_max_discard_segments(req->q) == 1) {
		range[0].flags = cpu_to_le32(flags);
		range[0].num_sectors = cpu_to_le32(blk_rq_sectors(req));
		range[0].sector = cpu_to_le64(blk_rq_pos(req));
		n = 1;
	} else {
		__rq_for_each_bio(bio, req) {
			u64 sector = bio->bi_iter.bi_sector;
			u32 num_sectors = bio->bi_iter.bi_size >> SECTOR_SHIFT;

			range[n].flags = cpu_to_le32(flags);
			range[n].num_sectors = cpu_to_le32(num_sectors);
			range[n].sector = cpu_to_le64(sector);
			n++;
		}
	}

	WARN_ON_ONCE(n != segments);

	req->special_vec.bv_page = virt_to_page(range);
	req->special_vec.bv_offset = offset_in_page(range);
	req->special_vec.bv_len = sizeof(*range) * segments;
	req->rq_flags |= RQF_SPECIAL_PAYLOAD;

	return 0;
}

static void virtblk_unmap_data(struct request *req, struct virtblk_req *vbr)
{
	if (blk_rq_nr_phys_segments(req))
		sg_free_table_chained(&vbr->sg_table,
				      VIRTIO_BLK_INLINE_SG_CNT);
}

static int virtblk_map_data(struct blk_mq_hw_ctx *hctx, struct request *req,
		struct virtblk_req *vbr)
{
	int err;

	if (!blk_rq_nr_phys_segments(req))
		return 0;

	vbr->sg_table.sgl = vbr->sg;
	err = sg_alloc_table_chained(&vbr->sg_table,
				     blk_rq_nr_phys_segments(req),
				     vbr->sg_table.sgl,
				     VIRTIO_BLK_INLINE_SG_CNT);
	if (unlikely(err))
		return -ENOMEM;

	return blk_rq_map_sg(hctx->queue, req, vbr->sg_table.sgl);
}

static void virtblk_cleanup_cmd(struct request *req)
{
	if (req->rq_flags & RQF_SPECIAL_PAYLOAD)
		kfree(bvec_virt(&req->special_vec));
}

static blk_status_t virtblk_setup_cmd(struct virtio_device *vdev,
				      struct request *req,
				      struct virtblk_req *vbr)
{
	bool unmap = false;
	u32 type;

	vbr->out_hdr.sector = 0;

	switch (req_op(req)) {
	case REQ_OP_READ:
		type = VIRTIO_BLK_T_IN;
		vbr->out_hdr.sector = cpu_to_virtio64(vdev,
						      blk_rq_pos(req));
		break;
	case REQ_OP_WRITE:
		type = VIRTIO_BLK_T_OUT;
		vbr->out_hdr.sector = cpu_to_virtio64(vdev,
						      blk_rq_pos(req));
		break;
	case REQ_OP_FLUSH:
		type = VIRTIO_BLK_T_FLUSH;
		break;
	case REQ_OP_DISCARD:
		type = VIRTIO_BLK_T_DISCARD;
		break;
	case REQ_OP_WRITE_ZEROES:
		type = VIRTIO_BLK_T_WRITE_ZEROES;
		unmap = !(req->cmd_flags & REQ_NOUNMAP);
		break;
	case REQ_OP_DRV_IN:
		type = VIRTIO_BLK_T_GET_ID;
		break;
	default:
		WARN_ON_ONCE(1);
		return BLK_STS_IOERR;
	}

	vbr->out_hdr.type = cpu_to_virtio32(vdev, type);
	vbr->out_hdr.ioprio = cpu_to_virtio32(vdev, req_get_ioprio(req));

	if (type == VIRTIO_BLK_T_DISCARD || type == VIRTIO_BLK_T_WRITE_ZEROES) {
		if (virtblk_setup_discard_write_zeroes(req, unmap))
			return BLK_STS_RESOURCE;
	}

	return 0;
}

static inline void virtblk_request_done(struct request *req)
{
	struct virtblk_req *vbr = blk_mq_rq_to_pdu(req);

	virtblk_unmap_data(req, vbr);
	virtblk_cleanup_cmd(req);
	blk_mq_end_request(req, virtblk_result(vbr));
}

static void virtblk_done(struct virtqueue *vq)
{
	struct virtio_blk *vblk = vq->vdev->priv;
	bool req_done = false;
	int qid = vq->index;
	struct virtblk_req *vbr;
	unsigned long flags;
	unsigned int len;

	spin_lock_irqsave(&vblk->vqs[qid].lock, flags);
	do {
		virtqueue_disable_cb(vq);
		while ((vbr = virtqueue_get_buf(vblk->vqs[qid].vq, &len)) != NULL) {
			struct request *req = blk_mq_rq_from_pdu(vbr);

			if (likely(!blk_should_fake_timeout(req->q)))
				blk_mq_complete_request(req);
			req_done = true;
		}
		if (unlikely(virtqueue_is_broken(vq)))
			break;
	} while (!virtqueue_enable_cb(vq));

	/* In case queue is stopped waiting for more buffers. */
	if (req_done)
		blk_mq_start_stopped_hw_queues(vblk->disk->queue, true);
	spin_unlock_irqrestore(&vblk->vqs[qid].lock, flags);
}

static void virtio_commit_rqs(struct blk_mq_hw_ctx *hctx)
{
	struct virtio_blk *vblk = hctx->queue->queuedata;
	struct virtio_blk_vq *vq = &vblk->vqs[hctx->queue_num];
	bool kick;

	spin_lock_irq(&vq->lock);
	kick = virtqueue_kick_prepare(vq->vq);
	spin_unlock_irq(&vq->lock);

	if (kick)
		virtqueue_notify(vq->vq);
}

static blk_status_t virtio_queue_rq(struct blk_mq_hw_ctx *hctx,
			   const struct blk_mq_queue_data *bd)
{
	struct virtio_blk *vblk = hctx->queue->queuedata;
	struct request *req = bd->rq;
	struct virtblk_req *vbr = blk_mq_rq_to_pdu(req);
	unsigned long flags;
	int num;
	int qid = hctx->queue_num;
	bool notify = false;
	blk_status_t status;
	int err;

	BUG_ON(req->nr_phys_segments + 2 > vblk->sg_elems);

	status = virtblk_setup_cmd(vblk->vdev, req, vbr);
	if (unlikely(status))
		return status;

	blk_mq_start_request(req);

	num = virtblk_map_data(hctx, req, vbr);
	if (unlikely(num < 0)) {
		virtblk_cleanup_cmd(req);
		return BLK_STS_RESOURCE;
	}

	spin_lock_irqsave(&vblk->vqs[qid].lock, flags);
	err = virtblk_add_req(vblk->vqs[qid].vq, vbr, vbr->sg_table.sgl, num);
	if (err) {
		virtqueue_kick(vblk->vqs[qid].vq);
		/* Don't stop the queue if -ENOMEM: we may have failed to
		 * bounce the buffer due to global resource outage.
		 */
		if (err == -ENOSPC)
			blk_mq_stop_hw_queue(hctx);
		spin_unlock_irqrestore(&vblk->vqs[qid].lock, flags);
		virtblk_unmap_data(req, vbr);
		virtblk_cleanup_cmd(req);
		switch (err) {
		case -ENOSPC:
			return BLK_STS_DEV_RESOURCE;
		case -ENOMEM:
			return BLK_STS_RESOURCE;
		default:
			return BLK_STS_IOERR;
		}
	}

	if (bd->last && virtqueue_kick_prepare(vblk->vqs[qid].vq))
		notify = true;
	spin_unlock_irqrestore(&vblk->vqs[qid].lock, flags);

	if (notify)
		virtqueue_notify(vblk->vqs[qid].vq);
	return BLK_STS_OK;
}

/* return id (s/n) string for *disk to *id_str
 */
static int virtblk_get_id(struct gendisk *disk, char *id_str)
{
	struct virtio_blk *vblk = disk->private_data;
	struct request_queue *q = vblk->disk->queue;
	struct request *req;
	int err;

	req = blk_mq_alloc_request(q, REQ_OP_DRV_IN, 0);
	if (IS_ERR(req))
		return PTR_ERR(req);

	err = blk_rq_map_kern(q, req, id_str, VIRTIO_BLK_ID_BYTES, GFP_KERNEL);
	if (err)
		goto out;

	blk_execute_rq(vblk->disk, req, false);
	err = blk_status_to_errno(virtblk_result(blk_mq_rq_to_pdu(req)));
out:
	blk_mq_free_request(req);
	return err;
}

static void virtblk_get(struct virtio_blk *vblk)
{
	refcount_inc(&vblk->refs);
}

static void virtblk_put(struct virtio_blk *vblk)
{
	if (refcount_dec_and_test(&vblk->refs)) {
		ida_simple_remove(&vd_index_ida, vblk->index);
		mutex_destroy(&vblk->vdev_mutex);
		kfree(vblk);
	}
}

static int virtblk_open(struct block_device *bd, fmode_t mode)
{
	struct virtio_blk *vblk = bd->bd_disk->private_data;
	int ret = 0;

	mutex_lock(&vblk->vdev_mutex);

	if (vblk->vdev)
		virtblk_get(vblk);
	else
		ret = -ENXIO;

	mutex_unlock(&vblk->vdev_mutex);
	return ret;
}

static void virtblk_release(struct gendisk *disk, fmode_t mode)
{
	struct virtio_blk *vblk = disk->private_data;

	virtblk_put(vblk);
}

/* We provide getgeo only to please some old bootloader/partitioning tools */
static int virtblk_getgeo(struct block_device *bd, struct hd_geometry *geo)
{
	struct virtio_blk *vblk = bd->bd_disk->private_data;
	int ret = 0;

	mutex_lock(&vblk->vdev_mutex);

	if (!vblk->vdev) {
		ret = -ENXIO;
		goto out;
	}

	/* see if the host passed in geometry config */
	if (virtio_has_feature(vblk->vdev, VIRTIO_BLK_F_GEOMETRY)) {
		virtio_cread(vblk->vdev, struct virtio_blk_config,
			     geometry.cylinders, &geo->cylinders);
		virtio_cread(vblk->vdev, struct virtio_blk_config,
			     geometry.heads, &geo->heads);
		virtio_cread(vblk->vdev, struct virtio_blk_config,
			     geometry.sectors, &geo->sectors);
	} else {
		/* some standard values, similar to sd */
		geo->heads = 1 << 6;
		geo->sectors = 1 << 5;
		geo->cylinders = get_capacity(bd->bd_disk) >> 11;
	}
out:
	mutex_unlock(&vblk->vdev_mutex);
	return ret;
}

static const struct block_device_operations virtblk_fops = {
	.owner  = THIS_MODULE,
	.open = virtblk_open,
	.release = virtblk_release,
	.getgeo = virtblk_getgeo,
};

static int index_to_minor(int index)
{
	return index << PART_BITS;
}

static int minor_to_index(int minor)
{
	return minor >> PART_BITS;
}

static ssize_t serial_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);
	int err;

	/* sysfs gives us a PAGE_SIZE buffer */
	BUILD_BUG_ON(PAGE_SIZE < VIRTIO_BLK_ID_BYTES);

	buf[VIRTIO_BLK_ID_BYTES] = '\0';
	err = virtblk_get_id(disk, buf);
	if (!err)
		return strlen(buf);

	if (err == -EIO) /* Unsupported? Make it empty. */
		return 0;

	return err;
}

static DEVICE_ATTR_RO(serial);

/* The queue's logical block size must be set before calling this */
static void virtblk_update_capacity(struct virtio_blk *vblk, bool resize)
{
	struct virtio_device *vdev = vblk->vdev;
	struct request_queue *q = vblk->disk->queue;
	char cap_str_2[10], cap_str_10[10];
	unsigned long long nblocks;
	u64 capacity;

	/* Host must always specify the capacity. */
	virtio_cread(vdev, struct virtio_blk_config, capacity, &capacity);

	nblocks = DIV_ROUND_UP_ULL(capacity, queue_logical_block_size(q) >> 9);

	string_get_size(nblocks, queue_logical_block_size(q),
			STRING_UNITS_2, cap_str_2, sizeof(cap_str_2));
	string_get_size(nblocks, queue_logical_block_size(q),
			STRING_UNITS_10, cap_str_10, sizeof(cap_str_10));

	dev_notice(&vdev->dev,
		   "[%s] %s%llu %d-byte logical blocks (%s/%s)\n",
		   vblk->disk->disk_name,
		   resize ? "new size: " : "",
		   nblocks,
		   queue_logical_block_size(q),
		   cap_str_10,
		   cap_str_2);

	set_capacity_and_notify(vblk->disk, capacity);
}

static void virtblk_config_changed_work(struct work_struct *work)
{
	struct virtio_blk *vblk =
		container_of(work, struct virtio_blk, config_work);

	virtblk_update_capacity(vblk, true);
}

static void virtblk_config_changed(struct virtio_device *vdev)
{
	struct virtio_blk *vblk = vdev->priv;

	queue_work(virtblk_wq, &vblk->config_work);
}

static int init_vq(struct virtio_blk *vblk)
{
	int err;
	int i;
	vq_callback_t **callbacks;
	const char **names;
	struct virtqueue **vqs;
	unsigned short num_vqs;
	struct virtio_device *vdev = vblk->vdev;
	struct irq_affinity desc = { 0, };

	err = virtio_cread_feature(vdev, VIRTIO_BLK_F_MQ,
				   struct virtio_blk_config, num_queues,
				   &num_vqs);
	if (err)
		num_vqs = 1;
	if (!err && !num_vqs) {
		dev_err(&vdev->dev, "MQ advertised but zero queues reported\n");
		return -EINVAL;
	}

	num_vqs = min_t(unsigned int,
			min_not_zero(num_request_queues, nr_cpu_ids),
			num_vqs);

	vblk->vqs = kmalloc_array(num_vqs, sizeof(*vblk->vqs), GFP_KERNEL);
	if (!vblk->vqs)
		return -ENOMEM;

	names = kmalloc_array(num_vqs, sizeof(*names), GFP_KERNEL);
	callbacks = kmalloc_array(num_vqs, sizeof(*callbacks), GFP_KERNEL);
	vqs = kmalloc_array(num_vqs, sizeof(*vqs), GFP_KERNEL);
	if (!names || !callbacks || !vqs) {
		err = -ENOMEM;
		goto out;
	}

	for (i = 0; i < num_vqs; i++) {
		callbacks[i] = virtblk_done;
		snprintf(vblk->vqs[i].name, VQ_NAME_LEN, "req.%d", i);
		names[i] = vblk->vqs[i].name;
	}

	/* Discover virtqueues and write information to configuration.  */
	err = virtio_find_vqs(vdev, num_vqs, vqs, callbacks, names, &desc);
	if (err)
		goto out;

	for (i = 0; i < num_vqs; i++) {
		spin_lock_init(&vblk->vqs[i].lock);
		vblk->vqs[i].vq = vqs[i];
	}
	vblk->num_vqs = num_vqs;

out:
	kfree(vqs);
	kfree(callbacks);
	kfree(names);
	if (err)
		kfree(vblk->vqs);
	return err;
}

/*
 * Legacy naming scheme used for virtio devices.  We are stuck with it for
 * virtio blk but don't ever use it for any new driver.
 */
static int virtblk_name_format(char *prefix, int index, char *buf, int buflen)
{
	const int base = 'z' - 'a' + 1;
	char *begin = buf + strlen(prefix);
	char *end = buf + buflen;
	char *p;
	int unit;

	p = end - 1;
	*p = '\0';
	unit = base;
	do {
		if (p == begin)
			return -EINVAL;
		*--p = 'a' + (index % unit);
		index = (index / unit) - 1;
	} while (index >= 0);

	memmove(begin, p, end - p);
	memcpy(buf, prefix, strlen(prefix));

	return 0;
}

static int virtblk_get_cache_mode(struct virtio_device *vdev)
{
	u8 writeback;
	int err;

	err = virtio_cread_feature(vdev, VIRTIO_BLK_F_CONFIG_WCE,
				   struct virtio_blk_config, wce,
				   &writeback);

	/*
	 * If WCE is not configurable and flush is not available,
	 * assume no writeback cache is in use.
	 */
	if (err)
		writeback = virtio_has_feature(vdev, VIRTIO_BLK_F_FLUSH);

	return writeback;
}

static void virtblk_update_cache_mode(struct virtio_device *vdev)
{
	u8 writeback = virtblk_get_cache_mode(vdev);
	struct virtio_blk *vblk = vdev->priv;

	blk_queue_write_cache(vblk->disk->queue, writeback, false);
}

static const char *const virtblk_cache_types[] = {
	"write through", "write back"
};

static ssize_t
cache_type_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct virtio_blk *vblk = disk->private_data;
	struct virtio_device *vdev = vblk->vdev;
	int i;

	BUG_ON(!virtio_has_feature(vblk->vdev, VIRTIO_BLK_F_CONFIG_WCE));
	i = sysfs_match_string(virtblk_cache_types, buf);
	if (i < 0)
		return i;

	virtio_cwrite8(vdev, offsetof(struct virtio_blk_config, wce), i);
	virtblk_update_cache_mode(vdev);
	return count;
}

static ssize_t
cache_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct virtio_blk *vblk = disk->private_data;
	u8 writeback = virtblk_get_cache_mode(vblk->vdev);

	BUG_ON(writeback >= ARRAY_SIZE(virtblk_cache_types));
	return sysfs_emit(buf, "%s\n", virtblk_cache_types[writeback]);
}

static DEVICE_ATTR_RW(cache_type);

static struct attribute *virtblk_attrs[] = {
	&dev_attr_serial.attr,
	&dev_attr_cache_type.attr,
	NULL,
};

static umode_t virtblk_attrs_are_visible(struct kobject *kobj,
		struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct gendisk *disk = dev_to_disk(dev);
	struct virtio_blk *vblk = disk->private_data;
	struct virtio_device *vdev = vblk->vdev;

	if (a == &dev_attr_cache_type.attr &&
	    !virtio_has_feature(vdev, VIRTIO_BLK_F_CONFIG_WCE))
		return S_IRUGO;

	return a->mode;
}

static const struct attribute_group virtblk_attr_group = {
	.attrs = virtblk_attrs,
	.is_visible = virtblk_attrs_are_visible,
};

static const struct attribute_group *virtblk_attr_groups[] = {
	&virtblk_attr_group,
	NULL,
};

static int virtblk_map_queues(struct blk_mq_tag_set *set)
{
	struct virtio_blk *vblk = set->driver_data;

	return blk_mq_virtio_map_queues(&set->map[HCTX_TYPE_DEFAULT],
					vblk->vdev, 0);
}

static const struct blk_mq_ops virtio_mq_ops = {
	.queue_rq	= virtio_queue_rq,
	.commit_rqs	= virtio_commit_rqs,
	.complete	= virtblk_request_done,
	.map_queues	= virtblk_map_queues,
};

static unsigned int virtblk_queue_depth;
module_param_named(queue_depth, virtblk_queue_depth, uint, 0444);

static int virtblk_probe(struct virtio_device *vdev)
{
	struct virtio_blk *vblk;
	struct request_queue *q;
	int err, index;

	u32 v, blk_size, max_size, sg_elems, opt_io_size;
	u16 min_io_size;
	u8 physical_block_exp, alignment_offset;
	unsigned int queue_depth;

	if (!vdev->config->get) {
		dev_err(&vdev->dev, "%s failure: config access disabled\n",
			__func__);
		return -EINVAL;
	}

	err = ida_simple_get(&vd_index_ida, 0, minor_to_index(1 << MINORBITS),
			     GFP_KERNEL);
	if (err < 0)
		goto out;
	index = err;

	/* We need to know how many segments before we allocate. */
	err = virtio_cread_feature(vdev, VIRTIO_BLK_F_SEG_MAX,
				   struct virtio_blk_config, seg_max,
				   &sg_elems);

	/* We need at least one SG element, whatever they say. */
	if (err || !sg_elems)
		sg_elems = 1;

	/* Prevent integer overflows and honor max vq size */
	sg_elems = min_t(u32, sg_elems, VIRTIO_BLK_MAX_SG_ELEMS - 2);

	/* We need extra sg elements at head and tail. */
	sg_elems += 2;
	vdev->priv = vblk = kmalloc(sizeof(*vblk), GFP_KERNEL);
	if (!vblk) {
		err = -ENOMEM;
		goto out_free_index;
	}

	/* This reference is dropped in virtblk_remove(). */
	refcount_set(&vblk->refs, 1);
	mutex_init(&vblk->vdev_mutex);

	vblk->vdev = vdev;
	vblk->sg_elems = sg_elems;

	INIT_WORK(&vblk->config_work, virtblk_config_changed_work);

	err = init_vq(vblk);
	if (err)
		goto out_free_vblk;

	/* Default queue sizing is to fill the ring. */
	if (!virtblk_queue_depth) {
		queue_depth = vblk->vqs[0].vq->num_free;
		/* ... but without indirect descs, we use 2 descs per req */
		if (!virtio_has_feature(vdev, VIRTIO_RING_F_INDIRECT_DESC))
			queue_depth /= 2;
	} else {
		queue_depth = virtblk_queue_depth;
	}

	memset(&vblk->tag_set, 0, sizeof(vblk->tag_set));
	vblk->tag_set.ops = &virtio_mq_ops;
	vblk->tag_set.queue_depth = queue_depth;
	vblk->tag_set.numa_node = NUMA_NO_NODE;
	vblk->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
	vblk->tag_set.cmd_size =
		sizeof(struct virtblk_req) +
		sizeof(struct scatterlist) * VIRTIO_BLK_INLINE_SG_CNT;
	vblk->tag_set.driver_data = vblk;
	vblk->tag_set.nr_hw_queues = vblk->num_vqs;

	err = blk_mq_alloc_tag_set(&vblk->tag_set);
	if (err)
		goto out_free_vq;

	vblk->disk = blk_mq_alloc_disk(&vblk->tag_set, vblk);
	if (IS_ERR(vblk->disk)) {
		err = PTR_ERR(vblk->disk);
		goto out_free_tags;
	}
	q = vblk->disk->queue;

	virtblk_name_format("vd", index, vblk->disk->disk_name, DISK_NAME_LEN);

	vblk->disk->major = major;
	vblk->disk->first_minor = index_to_minor(index);
	vblk->disk->minors = 1 << PART_BITS;
	vblk->disk->private_data = vblk;
	vblk->disk->fops = &virtblk_fops;
	vblk->disk->flags |= GENHD_FL_EXT_DEVT;
	vblk->index = index;

	/* configure queue flush support */
	virtblk_update_cache_mode(vdev);

	/* If disk is read-only in the host, the guest should obey */
	if (virtio_has_feature(vdev, VIRTIO_BLK_F_RO))
		set_disk_ro(vblk->disk, 1);

	/* We can handle whatever the host told us to handle. */
	blk_queue_max_segments(q, vblk->sg_elems-2);

	/* No real sector limit. */
	blk_queue_max_hw_sectors(q, -1U);

	max_size = virtio_max_dma_size(vdev);

	/* Host can optionally specify maximum segment size and number of
	 * segments. */
	err = virtio_cread_feature(vdev, VIRTIO_BLK_F_SIZE_MAX,
				   struct virtio_blk_config, size_max, &v);
	if (!err)
		max_size = min(max_size, v);

	blk_queue_max_segment_size(q, max_size);

	/* Host can optionally specify the block size of the device */
	err = virtio_cread_feature(vdev, VIRTIO_BLK_F_BLK_SIZE,
				   struct virtio_blk_config, blk_size,
				   &blk_size);
	if (!err) {
		err = blk_validate_block_size(blk_size);
		if (err) {
			dev_err(&vdev->dev,
				"virtio_blk: invalid block size: 0x%x\n",
				blk_size);
			goto out_cleanup_disk;
		}

		blk_queue_logical_block_size(q, blk_size);
	} else
		blk_size = queue_logical_block_size(q);

	/* Use topology information if available */
	err = virtio_cread_feature(vdev, VIRTIO_BLK_F_TOPOLOGY,
				   struct virtio_blk_config, physical_block_exp,
				   &physical_block_exp);
	if (!err && physical_block_exp)
		blk_queue_physical_block_size(q,
				blk_size * (1 << physical_block_exp));

	err = virtio_cread_feature(vdev, VIRTIO_BLK_F_TOPOLOGY,
				   struct virtio_blk_config, alignment_offset,
				   &alignment_offset);
	if (!err && alignment_offset)
		blk_queue_alignment_offset(q, blk_size * alignment_offset);

	err = virtio_cread_feature(vdev, VIRTIO_BLK_F_TOPOLOGY,
				   struct virtio_blk_config, min_io_size,
				   &min_io_size);
	if (!err && min_io_size)
		blk_queue_io_min(q, blk_size * min_io_size);

	err = virtio_cread_feature(vdev, VIRTIO_BLK_F_TOPOLOGY,
				   struct virtio_blk_config, opt_io_size,
				   &opt_io_size);
	if (!err && opt_io_size)
		blk_queue_io_opt(q, blk_size * opt_io_size);

	if (virtio_has_feature(vdev, VIRTIO_BLK_F_DISCARD)) {
		q->limits.discard_granularity = blk_size;

		virtio_cread(vdev, struct virtio_blk_config,
			     discard_sector_alignment, &v);
		q->limits.discard_alignment = v ? v << SECTOR_SHIFT : 0;

		virtio_cread(vdev, struct virtio_blk_config,
			     max_discard_sectors, &v);
		blk_queue_max_discard_sectors(q, v ? v : UINT_MAX);

		virtio_cread(vdev, struct virtio_blk_config, max_discard_seg,
			     &v);
		blk_queue_max_discard_segments(q,
					       min_not_zero(v,
							    MAX_DISCARD_SEGMENTS));

		blk_queue_flag_set(QUEUE_FLAG_DISCARD, q);
	}

	if (virtio_has_feature(vdev, VIRTIO_BLK_F_WRITE_ZEROES)) {
		virtio_cread(vdev, struct virtio_blk_config,
			     max_write_zeroes_sectors, &v);
		blk_queue_max_write_zeroes_sectors(q, v ? v : UINT_MAX);
	}

	virtblk_update_capacity(vblk, false);
	virtio_device_ready(vdev);

	err = device_add_disk(&vdev->dev, vblk->disk, virtblk_attr_groups);
	if (err)
		goto out_cleanup_disk;

	return 0;

out_cleanup_disk:
	blk_cleanup_disk(vblk->disk);
out_free_tags:
	blk_mq_free_tag_set(&vblk->tag_set);
out_free_vq:
	vdev->config->del_vqs(vdev);
	kfree(vblk->vqs);
out_free_vblk:
	kfree(vblk);
out_free_index:
	ida_simple_remove(&vd_index_ida, index);
out:
	return err;
}

static void virtblk_remove(struct virtio_device *vdev)
{
	struct virtio_blk *vblk = vdev->priv;

	/* Make sure no work handler is accessing the device. */
	flush_work(&vblk->config_work);

	del_gendisk(vblk->disk);
	blk_cleanup_disk(vblk->disk);
	blk_mq_free_tag_set(&vblk->tag_set);

	mutex_lock(&vblk->vdev_mutex);

	/* Stop all the virtqueues. */
	vdev->config->reset(vdev);

	/* Virtqueues are stopped, nothing can use vblk->vdev anymore. */
	vblk->vdev = NULL;

	vdev->config->del_vqs(vdev);
	kfree(vblk->vqs);

	mutex_unlock(&vblk->vdev_mutex);

	virtblk_put(vblk);
}

#ifdef CONFIG_PM_SLEEP
static int virtblk_freeze(struct virtio_device *vdev)
{
	struct virtio_blk *vblk = vdev->priv;

	/* Ensure we don't receive any more interrupts */
	vdev->config->reset(vdev);

	/* Make sure no work handler is accessing the device. */
	flush_work(&vblk->config_work);

	blk_mq_quiesce_queue(vblk->disk->queue);

	vdev->config->del_vqs(vdev);
	kfree(vblk->vqs);

	return 0;
}

static int virtblk_restore(struct virtio_device *vdev)
{
	struct virtio_blk *vblk = vdev->priv;
	int ret;

	ret = init_vq(vdev->priv);
	if (ret)
		return ret;

	virtio_device_ready(vdev);

	blk_mq_unquiesce_queue(vblk->disk->queue);
	return 0;
}
#endif

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_BLOCK, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features_legacy[] = {
	VIRTIO_BLK_F_SEG_MAX, VIRTIO_BLK_F_SIZE_MAX, VIRTIO_BLK_F_GEOMETRY,
	VIRTIO_BLK_F_RO, VIRTIO_BLK_F_BLK_SIZE,
	VIRTIO_BLK_F_FLUSH, VIRTIO_BLK_F_TOPOLOGY, VIRTIO_BLK_F_CONFIG_WCE,
	VIRTIO_BLK_F_MQ, VIRTIO_BLK_F_DISCARD, VIRTIO_BLK_F_WRITE_ZEROES,
}
;
static unsigned int features[] = {
	VIRTIO_BLK_F_SEG_MAX, VIRTIO_BLK_F_SIZE_MAX, VIRTIO_BLK_F_GEOMETRY,
	VIRTIO_BLK_F_RO, VIRTIO_BLK_F_BLK_SIZE,
	VIRTIO_BLK_F_FLUSH, VIRTIO_BLK_F_TOPOLOGY, VIRTIO_BLK_F_CONFIG_WCE,
	VIRTIO_BLK_F_MQ, VIRTIO_BLK_F_DISCARD, VIRTIO_BLK_F_WRITE_ZEROES,
};

static struct virtio_driver virtio_blk = {
	.feature_table			= features,
	.feature_table_size		= ARRAY_SIZE(features),
	.feature_table_legacy		= features_legacy,
	.feature_table_size_legacy	= ARRAY_SIZE(features_legacy),
	.driver.name			= KBUILD_MODNAME,
	.driver.owner			= THIS_MODULE,
	.id_table			= id_table,
	.probe				= virtblk_probe,
	.remove				= virtblk_remove,
	.config_changed			= virtblk_config_changed,
#ifdef CONFIG_PM_SLEEP
	.freeze				= virtblk_freeze,
	.restore			= virtblk_restore,
#endif
};

static int __init init(void)
{
	int error;

	virtblk_wq = alloc_workqueue("virtio-blk", 0, 0);
	if (!virtblk_wq)
		return -ENOMEM;

	major = register_blkdev(0, "virtblk");
	if (major < 0) {
		error = major;
		goto out_destroy_workqueue;
	}

	error = register_virtio_driver(&virtio_blk);
	if (error)
		goto out_unregister_blkdev;
	return 0;

out_unregister_blkdev:
	unregister_blkdev(major, "virtblk");
out_destroy_workqueue:
	destroy_workqueue(virtblk_wq);
	return error;
}

static void __exit fini(void)
{
	unregister_virtio_driver(&virtio_blk);
	unregister_blkdev(major, "virtblk");
	destroy_workqueue(virtblk_wq);
}
module_init(init);
module_exit(fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio block driver");
MODULE_LICENSE("GPL");
