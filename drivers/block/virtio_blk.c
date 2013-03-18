//#define DEBUG
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/virtio.h>
#include <linux/virtio_blk.h>
#include <linux/scatterlist.h>
#include <linux/string_helpers.h>
#include <scsi/scsi_cmnd.h>
#include <linux/idr.h>

#define PART_BITS 4

static bool use_bio;
module_param(use_bio, bool, S_IRUGO);

static int major;
static DEFINE_IDA(vd_index_ida);

struct workqueue_struct *virtblk_wq;

struct virtio_blk
{
	struct virtio_device *vdev;
	struct virtqueue *vq;
	wait_queue_head_t queue_wait;

	/* The disk structure for the kernel. */
	struct gendisk *disk;

	mempool_t *pool;

	/* Process context for config space updates */
	struct work_struct config_work;

	/* Lock for config space updates */
	struct mutex config_lock;

	/* enable config space updates */
	bool config_enable;

	/* What host tells us, plus 2 for header & tailer. */
	unsigned int sg_elems;

	/* Ida index - used to track minor number allocations. */
	int index;

	/* Scatterlist: can be too big for stack. */
	struct scatterlist sg[/*sg_elems*/];
};

struct virtblk_req
{
	struct request *req;
	struct bio *bio;
	struct virtio_blk_outhdr out_hdr;
	struct virtio_scsi_inhdr in_hdr;
	struct work_struct work;
	struct virtio_blk *vblk;
	int flags;
	u8 status;
	struct scatterlist sg[];
};

enum {
	VBLK_IS_FLUSH		= 1,
	VBLK_REQ_FLUSH		= 2,
	VBLK_REQ_DATA		= 4,
	VBLK_REQ_FUA		= 8,
};

static inline int virtblk_result(struct virtblk_req *vbr)
{
	switch (vbr->status) {
	case VIRTIO_BLK_S_OK:
		return 0;
	case VIRTIO_BLK_S_UNSUPP:
		return -ENOTTY;
	default:
		return -EIO;
	}
}

static inline struct virtblk_req *virtblk_alloc_req(struct virtio_blk *vblk,
						    gfp_t gfp_mask)
{
	struct virtblk_req *vbr;

	vbr = mempool_alloc(vblk->pool, gfp_mask);
	if (!vbr)
		return NULL;

	vbr->vblk = vblk;
	if (use_bio)
		sg_init_table(vbr->sg, vblk->sg_elems);

	return vbr;
}

static void virtblk_add_buf_wait(struct virtio_blk *vblk,
				 struct virtblk_req *vbr,
				 unsigned long out,
				 unsigned long in)
{
	DEFINE_WAIT(wait);

	for (;;) {
		prepare_to_wait_exclusive(&vblk->queue_wait, &wait,
					  TASK_UNINTERRUPTIBLE);

		spin_lock_irq(vblk->disk->queue->queue_lock);
		if (virtqueue_add_buf(vblk->vq, vbr->sg, out, in, vbr,
				      GFP_ATOMIC) < 0) {
			spin_unlock_irq(vblk->disk->queue->queue_lock);
			io_schedule();
		} else {
			virtqueue_kick(vblk->vq);
			spin_unlock_irq(vblk->disk->queue->queue_lock);
			break;
		}

	}

	finish_wait(&vblk->queue_wait, &wait);
}

static inline void virtblk_add_req(struct virtblk_req *vbr,
				   unsigned int out, unsigned int in)
{
	struct virtio_blk *vblk = vbr->vblk;

	spin_lock_irq(vblk->disk->queue->queue_lock);
	if (unlikely(virtqueue_add_buf(vblk->vq, vbr->sg, out, in, vbr,
					GFP_ATOMIC) < 0)) {
		spin_unlock_irq(vblk->disk->queue->queue_lock);
		virtblk_add_buf_wait(vblk, vbr, out, in);
		return;
	}
	virtqueue_kick(vblk->vq);
	spin_unlock_irq(vblk->disk->queue->queue_lock);
}

static int virtblk_bio_send_flush(struct virtblk_req *vbr)
{
	unsigned int out = 0, in = 0;

	vbr->flags |= VBLK_IS_FLUSH;
	vbr->out_hdr.type = VIRTIO_BLK_T_FLUSH;
	vbr->out_hdr.sector = 0;
	vbr->out_hdr.ioprio = 0;
	sg_set_buf(&vbr->sg[out++], &vbr->out_hdr, sizeof(vbr->out_hdr));
	sg_set_buf(&vbr->sg[out + in++], &vbr->status, sizeof(vbr->status));

	virtblk_add_req(vbr, out, in);

	return 0;
}

static int virtblk_bio_send_data(struct virtblk_req *vbr)
{
	struct virtio_blk *vblk = vbr->vblk;
	unsigned int num, out = 0, in = 0;
	struct bio *bio = vbr->bio;

	vbr->flags &= ~VBLK_IS_FLUSH;
	vbr->out_hdr.type = 0;
	vbr->out_hdr.sector = bio->bi_sector;
	vbr->out_hdr.ioprio = bio_prio(bio);

	sg_set_buf(&vbr->sg[out++], &vbr->out_hdr, sizeof(vbr->out_hdr));

	num = blk_bio_map_sg(vblk->disk->queue, bio, vbr->sg + out);

	sg_set_buf(&vbr->sg[num + out + in++], &vbr->status,
		   sizeof(vbr->status));

	if (num) {
		if (bio->bi_rw & REQ_WRITE) {
			vbr->out_hdr.type |= VIRTIO_BLK_T_OUT;
			out += num;
		} else {
			vbr->out_hdr.type |= VIRTIO_BLK_T_IN;
			in += num;
		}
	}

	virtblk_add_req(vbr, out, in);

	return 0;
}

static void virtblk_bio_send_data_work(struct work_struct *work)
{
	struct virtblk_req *vbr;

	vbr = container_of(work, struct virtblk_req, work);

	virtblk_bio_send_data(vbr);
}

static void virtblk_bio_send_flush_work(struct work_struct *work)
{
	struct virtblk_req *vbr;

	vbr = container_of(work, struct virtblk_req, work);

	virtblk_bio_send_flush(vbr);
}

static inline void virtblk_request_done(struct virtblk_req *vbr)
{
	struct virtio_blk *vblk = vbr->vblk;
	struct request *req = vbr->req;
	int error = virtblk_result(vbr);

	if (req->cmd_type == REQ_TYPE_BLOCK_PC) {
		req->resid_len = vbr->in_hdr.residual;
		req->sense_len = vbr->in_hdr.sense_len;
		req->errors = vbr->in_hdr.errors;
	} else if (req->cmd_type == REQ_TYPE_SPECIAL) {
		req->errors = (error != 0);
	}

	__blk_end_request_all(req, error);
	mempool_free(vbr, vblk->pool);
}

static inline void virtblk_bio_flush_done(struct virtblk_req *vbr)
{
	struct virtio_blk *vblk = vbr->vblk;

	if (vbr->flags & VBLK_REQ_DATA) {
		/* Send out the actual write data */
		INIT_WORK(&vbr->work, virtblk_bio_send_data_work);
		queue_work(virtblk_wq, &vbr->work);
	} else {
		bio_endio(vbr->bio, virtblk_result(vbr));
		mempool_free(vbr, vblk->pool);
	}
}

static inline void virtblk_bio_data_done(struct virtblk_req *vbr)
{
	struct virtio_blk *vblk = vbr->vblk;

	if (unlikely(vbr->flags & VBLK_REQ_FUA)) {
		/* Send out a flush before end the bio */
		vbr->flags &= ~VBLK_REQ_DATA;
		INIT_WORK(&vbr->work, virtblk_bio_send_flush_work);
		queue_work(virtblk_wq, &vbr->work);
	} else {
		bio_endio(vbr->bio, virtblk_result(vbr));
		mempool_free(vbr, vblk->pool);
	}
}

static inline void virtblk_bio_done(struct virtblk_req *vbr)
{
	if (unlikely(vbr->flags & VBLK_IS_FLUSH))
		virtblk_bio_flush_done(vbr);
	else
		virtblk_bio_data_done(vbr);
}

static void virtblk_done(struct virtqueue *vq)
{
	struct virtio_blk *vblk = vq->vdev->priv;
	bool bio_done = false, req_done = false;
	struct virtblk_req *vbr;
	unsigned long flags;
	unsigned int len;

	spin_lock_irqsave(vblk->disk->queue->queue_lock, flags);
	do {
		virtqueue_disable_cb(vq);
		while ((vbr = virtqueue_get_buf(vblk->vq, &len)) != NULL) {
			if (vbr->bio) {
				virtblk_bio_done(vbr);
				bio_done = true;
			} else {
				virtblk_request_done(vbr);
				req_done = true;
			}
		}
	} while (!virtqueue_enable_cb(vq));
	/* In case queue is stopped waiting for more buffers. */
	if (req_done)
		blk_start_queue(vblk->disk->queue);
	spin_unlock_irqrestore(vblk->disk->queue->queue_lock, flags);

	if (bio_done)
		wake_up(&vblk->queue_wait);
}

static bool do_req(struct request_queue *q, struct virtio_blk *vblk,
		   struct request *req)
{
	unsigned long num, out = 0, in = 0;
	struct virtblk_req *vbr;

	vbr = virtblk_alloc_req(vblk, GFP_ATOMIC);
	if (!vbr)
		/* When another request finishes we'll try again. */
		return false;

	vbr->req = req;
	vbr->bio = NULL;
	if (req->cmd_flags & REQ_FLUSH) {
		vbr->out_hdr.type = VIRTIO_BLK_T_FLUSH;
		vbr->out_hdr.sector = 0;
		vbr->out_hdr.ioprio = req_get_ioprio(vbr->req);
	} else {
		switch (req->cmd_type) {
		case REQ_TYPE_FS:
			vbr->out_hdr.type = 0;
			vbr->out_hdr.sector = blk_rq_pos(vbr->req);
			vbr->out_hdr.ioprio = req_get_ioprio(vbr->req);
			break;
		case REQ_TYPE_BLOCK_PC:
			vbr->out_hdr.type = VIRTIO_BLK_T_SCSI_CMD;
			vbr->out_hdr.sector = 0;
			vbr->out_hdr.ioprio = req_get_ioprio(vbr->req);
			break;
		case REQ_TYPE_SPECIAL:
			vbr->out_hdr.type = VIRTIO_BLK_T_GET_ID;
			vbr->out_hdr.sector = 0;
			vbr->out_hdr.ioprio = req_get_ioprio(vbr->req);
			break;
		default:
			/* We don't put anything else in the queue. */
			BUG();
		}
	}

	sg_set_buf(&vblk->sg[out++], &vbr->out_hdr, sizeof(vbr->out_hdr));

	/*
	 * If this is a packet command we need a couple of additional headers.
	 * Behind the normal outhdr we put a segment with the scsi command
	 * block, and before the normal inhdr we put the sense data and the
	 * inhdr with additional status information before the normal inhdr.
	 */
	if (vbr->req->cmd_type == REQ_TYPE_BLOCK_PC)
		sg_set_buf(&vblk->sg[out++], vbr->req->cmd, vbr->req->cmd_len);

	num = blk_rq_map_sg(q, vbr->req, vblk->sg + out);

	if (vbr->req->cmd_type == REQ_TYPE_BLOCK_PC) {
		sg_set_buf(&vblk->sg[num + out + in++], vbr->req->sense, SCSI_SENSE_BUFFERSIZE);
		sg_set_buf(&vblk->sg[num + out + in++], &vbr->in_hdr,
			   sizeof(vbr->in_hdr));
	}

	sg_set_buf(&vblk->sg[num + out + in++], &vbr->status,
		   sizeof(vbr->status));

	if (num) {
		if (rq_data_dir(vbr->req) == WRITE) {
			vbr->out_hdr.type |= VIRTIO_BLK_T_OUT;
			out += num;
		} else {
			vbr->out_hdr.type |= VIRTIO_BLK_T_IN;
			in += num;
		}
	}

	if (virtqueue_add_buf(vblk->vq, vblk->sg, out, in, vbr,
			      GFP_ATOMIC) < 0) {
		mempool_free(vbr, vblk->pool);
		return false;
	}

	return true;
}

static void virtblk_request(struct request_queue *q)
{
	struct virtio_blk *vblk = q->queuedata;
	struct request *req;
	unsigned int issued = 0;

	while ((req = blk_peek_request(q)) != NULL) {
		BUG_ON(req->nr_phys_segments + 2 > vblk->sg_elems);

		/* If this request fails, stop queue and wait for something to
		   finish to restart it. */
		if (!do_req(q, vblk, req)) {
			blk_stop_queue(q);
			break;
		}
		blk_start_request(req);
		issued++;
	}

	if (issued)
		virtqueue_kick(vblk->vq);
}

static void virtblk_make_request(struct request_queue *q, struct bio *bio)
{
	struct virtio_blk *vblk = q->queuedata;
	struct virtblk_req *vbr;

	BUG_ON(bio->bi_phys_segments + 2 > vblk->sg_elems);

	vbr = virtblk_alloc_req(vblk, GFP_NOIO);
	if (!vbr) {
		bio_endio(bio, -ENOMEM);
		return;
	}

	vbr->bio = bio;
	vbr->flags = 0;
	if (bio->bi_rw & REQ_FLUSH)
		vbr->flags |= VBLK_REQ_FLUSH;
	if (bio->bi_rw & REQ_FUA)
		vbr->flags |= VBLK_REQ_FUA;
	if (bio->bi_size)
		vbr->flags |= VBLK_REQ_DATA;

	if (unlikely(vbr->flags & VBLK_REQ_FLUSH))
		virtblk_bio_send_flush(vbr);
	else
		virtblk_bio_send_data(vbr);
}

/* return id (s/n) string for *disk to *id_str
 */
static int virtblk_get_id(struct gendisk *disk, char *id_str)
{
	struct virtio_blk *vblk = disk->private_data;
	struct request *req;
	struct bio *bio;
	int err;

	bio = bio_map_kern(vblk->disk->queue, id_str, VIRTIO_BLK_ID_BYTES,
			   GFP_KERNEL);
	if (IS_ERR(bio))
		return PTR_ERR(bio);

	req = blk_make_request(vblk->disk->queue, bio, GFP_KERNEL);
	if (IS_ERR(req)) {
		bio_put(bio);
		return PTR_ERR(req);
	}

	req->cmd_type = REQ_TYPE_SPECIAL;
	err = blk_execute_rq(vblk->disk->queue, vblk->disk, req, false);
	blk_put_request(req);

	return err;
}

static int virtblk_ioctl(struct block_device *bdev, fmode_t mode,
			     unsigned int cmd, unsigned long data)
{
	struct gendisk *disk = bdev->bd_disk;
	struct virtio_blk *vblk = disk->private_data;

	/*
	 * Only allow the generic SCSI ioctls if the host can support it.
	 */
	if (!virtio_has_feature(vblk->vdev, VIRTIO_BLK_F_SCSI))
		return -ENOTTY;

	return scsi_cmd_blk_ioctl(bdev, mode, cmd,
				  (void __user *)data);
}

/* We provide getgeo only to please some old bootloader/partitioning tools */
static int virtblk_getgeo(struct block_device *bd, struct hd_geometry *geo)
{
	struct virtio_blk *vblk = bd->bd_disk->private_data;
	struct virtio_blk_geometry vgeo;
	int err;

	/* see if the host passed in geometry config */
	err = virtio_config_val(vblk->vdev, VIRTIO_BLK_F_GEOMETRY,
				offsetof(struct virtio_blk_config, geometry),
				&vgeo);

	if (!err) {
		geo->heads = vgeo.heads;
		geo->sectors = vgeo.sectors;
		geo->cylinders = vgeo.cylinders;
	} else {
		/* some standard values, similar to sd */
		geo->heads = 1 << 6;
		geo->sectors = 1 << 5;
		geo->cylinders = get_capacity(bd->bd_disk) >> 11;
	}
	return 0;
}

static const struct block_device_operations virtblk_fops = {
	.ioctl  = virtblk_ioctl,
	.owner  = THIS_MODULE,
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

static ssize_t virtblk_serial_show(struct device *dev,
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
DEVICE_ATTR(serial, S_IRUGO, virtblk_serial_show, NULL);

static void virtblk_config_changed_work(struct work_struct *work)
{
	struct virtio_blk *vblk =
		container_of(work, struct virtio_blk, config_work);
	struct virtio_device *vdev = vblk->vdev;
	struct request_queue *q = vblk->disk->queue;
	char cap_str_2[10], cap_str_10[10];
	u64 capacity, size;

	mutex_lock(&vblk->config_lock);
	if (!vblk->config_enable)
		goto done;

	/* Host must always specify the capacity. */
	vdev->config->get(vdev, offsetof(struct virtio_blk_config, capacity),
			  &capacity, sizeof(capacity));

	/* If capacity is too big, truncate with warning. */
	if ((sector_t)capacity != capacity) {
		dev_warn(&vdev->dev, "Capacity %llu too large: truncating\n",
			 (unsigned long long)capacity);
		capacity = (sector_t)-1;
	}

	size = capacity * queue_logical_block_size(q);
	string_get_size(size, STRING_UNITS_2, cap_str_2, sizeof(cap_str_2));
	string_get_size(size, STRING_UNITS_10, cap_str_10, sizeof(cap_str_10));

	dev_notice(&vdev->dev,
		  "new size: %llu %d-byte logical blocks (%s/%s)\n",
		  (unsigned long long)capacity,
		  queue_logical_block_size(q),
		  cap_str_10, cap_str_2);

	set_capacity(vblk->disk, capacity);
	revalidate_disk(vblk->disk);
done:
	mutex_unlock(&vblk->config_lock);
}

static void virtblk_config_changed(struct virtio_device *vdev)
{
	struct virtio_blk *vblk = vdev->priv;

	queue_work(virtblk_wq, &vblk->config_work);
}

static int init_vq(struct virtio_blk *vblk)
{
	int err = 0;

	/* We expect one virtqueue, for output. */
	vblk->vq = virtio_find_single_vq(vblk->vdev, virtblk_done, "requests");
	if (IS_ERR(vblk->vq))
		err = PTR_ERR(vblk->vq);

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

	err = virtio_config_val(vdev, VIRTIO_BLK_F_CONFIG_WCE,
				offsetof(struct virtio_blk_config, wce),
				&writeback);
	if (err)
		writeback = virtio_has_feature(vdev, VIRTIO_BLK_F_WCE);

	return writeback;
}

static void virtblk_update_cache_mode(struct virtio_device *vdev)
{
	u8 writeback = virtblk_get_cache_mode(vdev);
	struct virtio_blk *vblk = vdev->priv;

	if (writeback)
		blk_queue_flush(vblk->disk->queue, REQ_FLUSH);
	else
		blk_queue_flush(vblk->disk->queue, 0);

	revalidate_disk(vblk->disk);
}

static const char *const virtblk_cache_types[] = {
	"write through", "write back"
};

static ssize_t
virtblk_cache_type_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct virtio_blk *vblk = disk->private_data;
	struct virtio_device *vdev = vblk->vdev;
	int i;
	u8 writeback;

	BUG_ON(!virtio_has_feature(vblk->vdev, VIRTIO_BLK_F_CONFIG_WCE));
	for (i = ARRAY_SIZE(virtblk_cache_types); --i >= 0; )
		if (sysfs_streq(buf, virtblk_cache_types[i]))
			break;

	if (i < 0)
		return -EINVAL;

	writeback = i;
	vdev->config->set(vdev,
			  offsetof(struct virtio_blk_config, wce),
			  &writeback, sizeof(writeback));

	virtblk_update_cache_mode(vdev);
	return count;
}

static ssize_t
virtblk_cache_type_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct virtio_blk *vblk = disk->private_data;
	u8 writeback = virtblk_get_cache_mode(vblk->vdev);

	BUG_ON(writeback >= ARRAY_SIZE(virtblk_cache_types));
	return snprintf(buf, 40, "%s\n", virtblk_cache_types[writeback]);
}

static const struct device_attribute dev_attr_cache_type_ro =
	__ATTR(cache_type, S_IRUGO,
	       virtblk_cache_type_show, NULL);
static const struct device_attribute dev_attr_cache_type_rw =
	__ATTR(cache_type, S_IRUGO|S_IWUSR,
	       virtblk_cache_type_show, virtblk_cache_type_store);

static int virtblk_probe(struct virtio_device *vdev)
{
	struct virtio_blk *vblk;
	struct request_queue *q;
	int err, index;
	int pool_size;

	u64 cap;
	u32 v, blk_size, sg_elems, opt_io_size;
	u16 min_io_size;
	u8 physical_block_exp, alignment_offset;

	err = ida_simple_get(&vd_index_ida, 0, minor_to_index(1 << MINORBITS),
			     GFP_KERNEL);
	if (err < 0)
		goto out;
	index = err;

	/* We need to know how many segments before we allocate. */
	err = virtio_config_val(vdev, VIRTIO_BLK_F_SEG_MAX,
				offsetof(struct virtio_blk_config, seg_max),
				&sg_elems);

	/* We need at least one SG element, whatever they say. */
	if (err || !sg_elems)
		sg_elems = 1;

	/* We need an extra sg elements at head and tail. */
	sg_elems += 2;
	vdev->priv = vblk = kmalloc(sizeof(*vblk) +
				    sizeof(vblk->sg[0]) * sg_elems, GFP_KERNEL);
	if (!vblk) {
		err = -ENOMEM;
		goto out_free_index;
	}

	init_waitqueue_head(&vblk->queue_wait);
	vblk->vdev = vdev;
	vblk->sg_elems = sg_elems;
	sg_init_table(vblk->sg, vblk->sg_elems);
	mutex_init(&vblk->config_lock);

	INIT_WORK(&vblk->config_work, virtblk_config_changed_work);
	vblk->config_enable = true;

	err = init_vq(vblk);
	if (err)
		goto out_free_vblk;

	pool_size = sizeof(struct virtblk_req);
	if (use_bio)
		pool_size += sizeof(struct scatterlist) * sg_elems;
	vblk->pool = mempool_create_kmalloc_pool(1, pool_size);
	if (!vblk->pool) {
		err = -ENOMEM;
		goto out_free_vq;
	}

	/* FIXME: How many partitions?  How long is a piece of string? */
	vblk->disk = alloc_disk(1 << PART_BITS);
	if (!vblk->disk) {
		err = -ENOMEM;
		goto out_mempool;
	}

	q = vblk->disk->queue = blk_init_queue(virtblk_request, NULL);
	if (!q) {
		err = -ENOMEM;
		goto out_put_disk;
	}

	if (use_bio)
		blk_queue_make_request(q, virtblk_make_request);
	q->queuedata = vblk;

	virtblk_name_format("vd", index, vblk->disk->disk_name, DISK_NAME_LEN);

	vblk->disk->major = major;
	vblk->disk->first_minor = index_to_minor(index);
	vblk->disk->private_data = vblk;
	vblk->disk->fops = &virtblk_fops;
	vblk->disk->driverfs_dev = &vdev->dev;
	vblk->index = index;

	/* configure queue flush support */
	virtblk_update_cache_mode(vdev);

	/* If disk is read-only in the host, the guest should obey */
	if (virtio_has_feature(vdev, VIRTIO_BLK_F_RO))
		set_disk_ro(vblk->disk, 1);

	/* Host must always specify the capacity. */
	vdev->config->get(vdev, offsetof(struct virtio_blk_config, capacity),
			  &cap, sizeof(cap));

	/* If capacity is too big, truncate with warning. */
	if ((sector_t)cap != cap) {
		dev_warn(&vdev->dev, "Capacity %llu too large: truncating\n",
			 (unsigned long long)cap);
		cap = (sector_t)-1;
	}
	set_capacity(vblk->disk, cap);

	/* We can handle whatever the host told us to handle. */
	blk_queue_max_segments(q, vblk->sg_elems-2);

	/* No need to bounce any requests */
	blk_queue_bounce_limit(q, BLK_BOUNCE_ANY);

	/* No real sector limit. */
	blk_queue_max_hw_sectors(q, -1U);

	/* Host can optionally specify maximum segment size and number of
	 * segments. */
	err = virtio_config_val(vdev, VIRTIO_BLK_F_SIZE_MAX,
				offsetof(struct virtio_blk_config, size_max),
				&v);
	if (!err)
		blk_queue_max_segment_size(q, v);
	else
		blk_queue_max_segment_size(q, -1U);

	/* Host can optionally specify the block size of the device */
	err = virtio_config_val(vdev, VIRTIO_BLK_F_BLK_SIZE,
				offsetof(struct virtio_blk_config, blk_size),
				&blk_size);
	if (!err)
		blk_queue_logical_block_size(q, blk_size);
	else
		blk_size = queue_logical_block_size(q);

	/* Use topology information if available */
	err = virtio_config_val(vdev, VIRTIO_BLK_F_TOPOLOGY,
			offsetof(struct virtio_blk_config, physical_block_exp),
			&physical_block_exp);
	if (!err && physical_block_exp)
		blk_queue_physical_block_size(q,
				blk_size * (1 << physical_block_exp));

	err = virtio_config_val(vdev, VIRTIO_BLK_F_TOPOLOGY,
			offsetof(struct virtio_blk_config, alignment_offset),
			&alignment_offset);
	if (!err && alignment_offset)
		blk_queue_alignment_offset(q, blk_size * alignment_offset);

	err = virtio_config_val(vdev, VIRTIO_BLK_F_TOPOLOGY,
			offsetof(struct virtio_blk_config, min_io_size),
			&min_io_size);
	if (!err && min_io_size)
		blk_queue_io_min(q, blk_size * min_io_size);

	err = virtio_config_val(vdev, VIRTIO_BLK_F_TOPOLOGY,
			offsetof(struct virtio_blk_config, opt_io_size),
			&opt_io_size);
	if (!err && opt_io_size)
		blk_queue_io_opt(q, blk_size * opt_io_size);

	add_disk(vblk->disk);
	err = device_create_file(disk_to_dev(vblk->disk), &dev_attr_serial);
	if (err)
		goto out_del_disk;

	if (virtio_has_feature(vdev, VIRTIO_BLK_F_CONFIG_WCE))
		err = device_create_file(disk_to_dev(vblk->disk),
					 &dev_attr_cache_type_rw);
	else
		err = device_create_file(disk_to_dev(vblk->disk),
					 &dev_attr_cache_type_ro);
	if (err)
		goto out_del_disk;
	return 0;

out_del_disk:
	del_gendisk(vblk->disk);
	blk_cleanup_queue(vblk->disk->queue);
out_put_disk:
	put_disk(vblk->disk);
out_mempool:
	mempool_destroy(vblk->pool);
out_free_vq:
	vdev->config->del_vqs(vdev);
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
	int index = vblk->index;
	int refc;

	/* Prevent config work handler from accessing the device. */
	mutex_lock(&vblk->config_lock);
	vblk->config_enable = false;
	mutex_unlock(&vblk->config_lock);

	del_gendisk(vblk->disk);
	blk_cleanup_queue(vblk->disk->queue);

	/* Stop all the virtqueues. */
	vdev->config->reset(vdev);

	flush_work(&vblk->config_work);

	refc = atomic_read(&disk_to_dev(vblk->disk)->kobj.kref.refcount);
	put_disk(vblk->disk);
	mempool_destroy(vblk->pool);
	vdev->config->del_vqs(vdev);
	kfree(vblk);

	/* Only free device id if we don't have any users */
	if (refc == 1)
		ida_simple_remove(&vd_index_ida, index);
}

#ifdef CONFIG_PM
static int virtblk_freeze(struct virtio_device *vdev)
{
	struct virtio_blk *vblk = vdev->priv;

	/* Ensure we don't receive any more interrupts */
	vdev->config->reset(vdev);

	/* Prevent config work handler from accessing the device. */
	mutex_lock(&vblk->config_lock);
	vblk->config_enable = false;
	mutex_unlock(&vblk->config_lock);

	flush_work(&vblk->config_work);

	spin_lock_irq(vblk->disk->queue->queue_lock);
	blk_stop_queue(vblk->disk->queue);
	spin_unlock_irq(vblk->disk->queue->queue_lock);
	blk_sync_queue(vblk->disk->queue);

	vdev->config->del_vqs(vdev);
	return 0;
}

static int virtblk_restore(struct virtio_device *vdev)
{
	struct virtio_blk *vblk = vdev->priv;
	int ret;

	vblk->config_enable = true;
	ret = init_vq(vdev->priv);
	if (!ret) {
		spin_lock_irq(vblk->disk->queue->queue_lock);
		blk_start_queue(vblk->disk->queue);
		spin_unlock_irq(vblk->disk->queue->queue_lock);
	}
	return ret;
}
#endif

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_BLOCK, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VIRTIO_BLK_F_SEG_MAX, VIRTIO_BLK_F_SIZE_MAX, VIRTIO_BLK_F_GEOMETRY,
	VIRTIO_BLK_F_RO, VIRTIO_BLK_F_BLK_SIZE, VIRTIO_BLK_F_SCSI,
	VIRTIO_BLK_F_WCE, VIRTIO_BLK_F_TOPOLOGY, VIRTIO_BLK_F_CONFIG_WCE
};

static struct virtio_driver virtio_blk = {
	.feature_table		= features,
	.feature_table_size	= ARRAY_SIZE(features),
	.driver.name		= KBUILD_MODNAME,
	.driver.owner		= THIS_MODULE,
	.id_table		= id_table,
	.probe			= virtblk_probe,
	.remove			= virtblk_remove,
	.config_changed		= virtblk_config_changed,
#ifdef CONFIG_PM
	.freeze			= virtblk_freeze,
	.restore		= virtblk_restore,
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
	unregister_blkdev(major, "virtblk");
	unregister_virtio_driver(&virtio_blk);
	destroy_workqueue(virtblk_wq);
}
module_init(init);
module_exit(fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio block driver");
MODULE_LICENSE("GPL");
