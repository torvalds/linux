//#define DEBUG
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/virtio.h>
#include <linux/virtio_blk.h>
#include <linux/scatterlist.h>

#define VIRTIO_MAX_SG	(3+MAX_PHYS_SEGMENTS)

static unsigned char virtblk_index = 'a';
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

static bool blk_done(struct virtqueue *vq)
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
	return true;
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

static struct block_device_operations virtblk_fops = {
	.ioctl = virtblk_ioctl,
	.owner = THIS_MODULE,
};

static int virtblk_probe(struct virtio_device *vdev)
{
	struct virtio_blk *vblk;
	int err, major;
	void *token;
	unsigned int len;
	u64 cap;
	u32 v;

	vdev->priv = vblk = kmalloc(sizeof(*vblk), GFP_KERNEL);
	if (!vblk) {
		err = -ENOMEM;
		goto out;
	}

	INIT_LIST_HEAD(&vblk->reqs);
	spin_lock_init(&vblk->lock);
	vblk->vdev = vdev;

	/* We expect one virtqueue, for output. */
	vblk->vq = vdev->config->find_vq(vdev, blk_done);
	if (IS_ERR(vblk->vq)) {
		err = PTR_ERR(vblk->vq);
		goto out_free_vblk;
	}

	vblk->pool = mempool_create_kmalloc_pool(1,sizeof(struct virtblk_req));
	if (!vblk->pool) {
		err = -ENOMEM;
		goto out_free_vq;
	}

	major = register_blkdev(0, "virtblk");
	if (major < 0) {
		err = major;
		goto out_mempool;
	}

	/* FIXME: How many partitions?  How long is a piece of string? */
	vblk->disk = alloc_disk(1 << 4);
	if (!vblk->disk) {
		err = -ENOMEM;
		goto out_unregister_blkdev;
	}

	vblk->disk->queue = blk_init_queue(do_virtblk_request, &vblk->lock);
	if (!vblk->disk->queue) {
		err = -ENOMEM;
		goto out_put_disk;
	}

	sprintf(vblk->disk->disk_name, "vd%c", virtblk_index++);
	vblk->disk->major = major;
	vblk->disk->first_minor = 0;
	vblk->disk->private_data = vblk;
	vblk->disk->fops = &virtblk_fops;

	/* If barriers are supported, tell block layer that queue is ordered */
	token = vdev->config->find(vdev, VIRTIO_CONFIG_BLK_F, &len);
	if (virtio_use_bit(vdev, token, len, VIRTIO_BLK_F_BARRIER))
		blk_queue_ordered(vblk->disk->queue, QUEUE_ORDERED_TAG, NULL);

	err = virtio_config_val(vdev, VIRTIO_CONFIG_BLK_F_CAPACITY, &cap);
	if (err) {
		dev_err(&vdev->dev, "Bad/missing capacity in config\n");
		goto out_put_disk;
	}

	/* If capacity is too big, truncate with warning. */
	if ((sector_t)cap != cap) {
		dev_warn(&vdev->dev, "Capacity %llu too large: truncating\n",
			 (unsigned long long)cap);
		cap = (sector_t)-1;
	}
	set_capacity(vblk->disk, cap);

	err = virtio_config_val(vdev, VIRTIO_CONFIG_BLK_F_SIZE_MAX, &v);
	if (!err)
		blk_queue_max_segment_size(vblk->disk->queue, v);
	else if (err != -ENOENT) {
		dev_err(&vdev->dev, "Bad SIZE_MAX in config\n");
		goto out_put_disk;
	}

	err = virtio_config_val(vdev, VIRTIO_CONFIG_BLK_F_SEG_MAX, &v);
	if (!err)
		blk_queue_max_hw_segments(vblk->disk->queue, v);
	else if (err != -ENOENT) {
		dev_err(&vdev->dev, "Bad SEG_MAX in config\n");
		goto out_put_disk;
	}

	add_disk(vblk->disk);
	return 0;

out_put_disk:
	put_disk(vblk->disk);
out_unregister_blkdev:
	unregister_blkdev(major, "virtblk");
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

	BUG_ON(!list_empty(&vblk->reqs));
	blk_cleanup_queue(vblk->disk->queue);
	put_disk(vblk->disk);
	unregister_blkdev(major, "virtblk");
	mempool_destroy(vblk->pool);
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
	return register_virtio_driver(&virtio_blk);
}

static void __exit fini(void)
{
	unregister_virtio_driver(&virtio_blk);
}
module_init(init);
module_exit(fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio block driver");
MODULE_LICENSE("GPL");
