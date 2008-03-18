//#define DEBUG
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/virtio.h>
#include <linux/virtio_blk.h>
#include <linux/scatterlist.h>

#define VIRTIO_MAX_SG	(3+MAX_PHYS_SEGMENTS)
#define PART_BITS 4

static int major, index;

struct virtio_blk
{
	spinlock_t lock;

	struct virtio_device *vdev;
	struct virtqueue *vq;

	/* The disk structure for the kernel. */
	struct gendisk *disk;

	/* Request tracking. */
	struct list_head reqs;

	mempool_t *pool;

	/* Scatterlist: can be too big for stack. */
	struct scatterlist sg[VIRTIO_MAX_SG];
};

struct virtblk_req
{
	struct list_head list;
	struct request *req;
	struct virtio_blk_outhdr out_hdr;
	struct virtio_blk_inhdr in_hdr;
};

static void blk_done(struct virtqueue *vq)
{
	struct virtio_blk *vblk = vq->vdev->priv;
	struct virtblk_req *vbr;
	unsigned int len;
	unsigned long flags;

	spin_lock_irqsave(&vblk->lock, flags);
	while ((vbr = vblk->vq->vq_ops->get_buf(vblk->vq, &len)) != NULL) {
		int uptodate;
		switch (vbr->in_hdr.status) {
		case VIRTIO_BLK_S_OK:
			uptodate = 1;
			break;
		case VIRTIO_BLK_S_UNSUPP:
			uptodate = -ENOTTY;
			break;
		default:
			uptodate = 0;
			break;
		}

		end_dequeued_request(vbr->req, uptodate);
		list_del(&vbr->list);
		mempool_free(vbr, vblk->pool);
	}
	/* In case queue is stopped waiting for more buffers. */
	blk_start_queue(vblk->disk->queue);
	spin_unlock_irqrestore(&vblk->lock, flags);
}

static bool do_req(struct request_queue *q, struct virtio_blk *vblk,
		   struct request *req)
{
	unsigned long num, out, in;
	struct virtblk_req *vbr;

	vbr = mempool_alloc(vblk->pool, GFP_ATOMIC);
	if (!vbr)
		/* When another request finishes we'll try again. */
		return false;

	vbr->req = req;
	if (blk_fs_request(vbr->req)) {
		vbr->out_hdr.type = 0;
		vbr->out_hdr.sector = vbr->req->sector;
		vbr->out_hdr.ioprio = vbr->req->ioprio;
	} else if (blk_pc_request(vbr->req)) {
		vbr->out_hdr.type = VIRTIO_BLK_T_SCSI_CMD;
		vbr->out_hdr.sector = 0;
		vbr->out_hdr.ioprio = vbr->req->ioprio;
	} else {
		/* We don't put anything else in the queue. */
		BUG();
	}

	if (blk_barrier_rq(vbr->req))
		vbr->out_hdr.type |= VIRTIO_BLK_T_BARRIER;

	/* This init could be done at vblk creation time */
	sg_init_table(vblk->sg, VIRTIO_MAX_SG);
	sg_set_buf(&vblk->sg[0], &vbr->out_hdr, sizeof(vbr->out_hdr));
	num = blk_rq_map_sg(q, vbr->req, vblk->sg+1);
	sg_set_buf(&vblk->sg[num+1], &vbr->in_hdr, sizeof(vbr->in_hdr));

	if (rq_data_dir(vbr->req) == WRITE) {
		vbr->out_hdr.type |= VIRTIO_BLK_T_OUT;
		out = 1 + num;
		in = 1;
	} else {
		vbr->out_hdr.type |= VIRTIO_BLK_T_IN;
		out = 1;
		in = 1 + num;
	}

	if (vblk->vq->vq_ops->add_buf(vblk->vq, vblk->sg, out, in, vbr)) {
		mempool_free(vbr, vblk->pool);
		return false;
	}

	list_add_tail(&vbr->list, &vblk->reqs);
	return true;
}

static void do_virtblk_request(struct request_queue *q)
{
	struct virtio_blk *vblk = NULL;
	struct request *req;
	unsigned int issued = 0;

	while ((req = elv_next_request(q)) != NULL) {
		vblk = req->rq_disk->private_data;
		BUG_ON(req->nr_phys_segments > ARRAY_SIZE(vblk->sg));

		/* If this request fails, stop queue and wait for something to
		   finish to restart it. */
		if (!do_req(q, vblk, req)) {
			blk_stop_queue(q);
			break;
		}
		blkdev_dequeue_request(req);
		issued++;
	}

	if (issued)
		vblk->vq->vq_ops->kick(vblk->vq);
}

static int virtblk_ioctl(struct inode *inode, struct file *filp,
			 unsigned cmd, unsigned long data)
{
	return scsi_cmd_ioctl(filp, inode->i_bdev->bd_disk->queue,
			      inode->i_bdev->bd_disk, cmd,
			      (void __user *)data);
}

/* We provide getgeo only to please some old bootloader/partitioning tools */
static int virtblk_getgeo(struct block_device *bd, struct hd_geometry *geo)
{
	/* some standard values, similar to sd */
	geo->heads = 1 << 6;
	geo->sectors = 1 << 5;
	geo->cylinders = get_capacity(bd->bd_disk) >> 11;
	return 0;
}

static struct block_device_operations virtblk_fops = {
	.ioctl  = virtblk_ioctl,
	.owner  = THIS_MODULE,
	.getgeo = virtblk_getgeo,
};

static int index_to_minor(int index)
{
	return index << PART_BITS;
}

static int virtblk_probe(struct virtio_device *vdev)
{
	struct virtio_blk *vblk;
	int err;
	u64 cap;
	u32 v;

	if (index_to_minor(index) >= 1 << MINORBITS)
		return -ENOSPC;

	vdev->priv = vblk = kmalloc(sizeof(*vblk), GFP_KERNEL);
	if (!vblk) {
		err = -ENOMEM;
		goto out;
	}

	INIT_LIST_HEAD(&vblk->reqs);
	spin_lock_init(&vblk->lock);
	vblk->vdev = vdev;

	/* We expect one virtqueue, for output. */
	vblk->vq = vdev->config->find_vq(vdev, 0, blk_done);
	if (IS_ERR(vblk->vq)) {
		err = PTR_ERR(vblk->vq);
		goto out_free_vblk;
	}

	vblk->pool = mempool_create_kmalloc_pool(1,sizeof(struct virtblk_req));
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

	vblk->disk->queue = blk_init_queue(do_virtblk_request, &vblk->lock);
	if (!vblk->disk->queue) {
		err = -ENOMEM;
		goto out_put_disk;
	}

	if (index < 26) {
		sprintf(vblk->disk->disk_name, "vd%c", 'a' + index % 26);
	} else if (index < (26 + 1) * 26) {
		sprintf(vblk->disk->disk_name, "vd%c%c",
			'a' + index / 26 - 1, 'a' + index % 26);
	} else {
		const unsigned int m1 = (index / 26 - 1) / 26 - 1;
		const unsigned int m2 = (index / 26 - 1) % 26;
		const unsigned int m3 =  index % 26;
		sprintf(vblk->disk->disk_name, "vd%c%c%c",
			'a' + m1, 'a' + m2, 'a' + m3);
	}

	vblk->disk->major = major;
	vblk->disk->first_minor = index_to_minor(index);
	vblk->disk->private_data = vblk;
	vblk->disk->fops = &virtblk_fops;
	vblk->disk->driverfs_dev = &vdev->dev;
	index++;

	/* If barriers are supported, tell block layer that queue is ordered */
	if (vdev->config->feature(vdev, VIRTIO_BLK_F_BARRIER))
		blk_queue_ordered(vblk->disk->queue, QUEUE_ORDERED_TAG, NULL);

	/* Host must always specify the capacity. */
	__virtio_config_val(vdev, offsetof(struct virtio_blk_config, capacity),
			    &cap);

	/* If capacity is too big, truncate with warning. */
	if ((sector_t)cap != cap) {
		dev_warn(&vdev->dev, "Capacity %llu too large: truncating\n",
			 (unsigned long long)cap);
		cap = (sector_t)-1;
	}
	set_capacity(vblk->disk, cap);

	/* Host can optionally specify maximum segment size and number of
	 * segments. */
	err = virtio_config_val(vdev, VIRTIO_BLK_F_SIZE_MAX,
				offsetof(struct virtio_blk_config, size_max),
				&v);
	if (!err)
		blk_queue_max_segment_size(vblk->disk->queue, v);

	err = virtio_config_val(vdev, VIRTIO_BLK_F_SEG_MAX,
				offsetof(struct virtio_blk_config, seg_max),
				&v);
	if (!err)
		blk_queue_max_hw_segments(vblk->disk->queue, v);

	add_disk(vblk->disk);
	return 0;

out_put_disk:
	put_disk(vblk->disk);
out_mempool:
	mempool_destroy(vblk->pool);
out_free_vq:
	vdev->config->del_vq(vblk->vq);
out_free_vblk:
	kfree(vblk);
out:
	return err;
}

static void virtblk_remove(struct virtio_device *vdev)
{
	struct virtio_blk *vblk = vdev->priv;
	int major = vblk->disk->major;

	/* Nothing should be pending. */
	BUG_ON(!list_empty(&vblk->reqs));

	/* Stop all the virtqueues. */
	vdev->config->reset(vdev);

	blk_cleanup_queue(vblk->disk->queue);
	put_disk(vblk->disk);
	unregister_blkdev(major, "virtblk");
	mempool_destroy(vblk->pool);
	vdev->config->del_vq(vblk->vq);
	kfree(vblk);
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_BLOCK, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver virtio_blk = {
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table =	id_table,
	.probe =	virtblk_probe,
	.remove =	__devexit_p(virtblk_remove),
};

static int __init init(void)
{
	major = register_blkdev(0, "virtblk");
	if (major < 0)
		return major;
	return register_virtio_driver(&virtio_blk);
}

static void __exit fini(void)
{
	unregister_blkdev(major, "virtblk");
	unregister_virtio_driver(&virtio_blk);
}
module_init(init);
module_exit(fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio block driver");
MODULE_LICENSE("GPL");
