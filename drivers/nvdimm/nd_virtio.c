// SPDX-License-Identifier: GPL-2.0
/*
 * virtio_pmem.c: Virtio pmem Driver
 *
 * Discovers persistent memory range information
 * from host and provides a virtio based flushing
 * interface.
 */
#include "virtio_pmem.h"
#include "nd.h"

struct virtio_pmem_flush_work {
	struct work_struct work;
	struct nd_region *nd_region;
	struct bio *bio;
};

static void virtio_pmem_req_release(struct kref *kref)
{
	struct virtio_pmem_request *req;

	req = container_of(kref, struct virtio_pmem_request, kref);
	kfree(req);
}

static void virtio_pmem_signal_done(struct virtio_pmem_request *req)
{
	/* Pairs with smp_load_acquire() in virtio_pmem_req_done(). */
	smp_store_release(&req->done, true);
	wake_up(&req->host_acked);
}

static bool virtio_pmem_req_done(struct virtio_pmem_request *req)
{
	/* Pairs with smp_store_release() in virtio_pmem_signal_done(). */
	return smp_load_acquire(&req->done);
}

static void virtio_pmem_complete_err(struct virtio_pmem_request *req)
{
	req->resp.ret = cpu_to_le32(1);
	virtio_pmem_signal_done(req);
}

static void virtio_pmem_wake_one_waiter(struct virtio_pmem *vpmem)
{
	struct virtio_pmem_request *req_buf;

	if (list_empty(&vpmem->req_list))
		return;

	req_buf = list_first_entry(&vpmem->req_list,
				   struct virtio_pmem_request, list);
	list_del_init(&req_buf->list);
	WRITE_ONCE(req_buf->wq_buf_avail, true);
	wake_up(&req_buf->wq_buf);
}

static void virtio_pmem_wake_all_waiters(struct virtio_pmem *vpmem)
{
	struct virtio_pmem_request *req, *tmp;

	list_for_each_entry_safe(req, tmp, &vpmem->req_list, list) {
		list_del_init(&req->list);
		WRITE_ONCE(req->wq_buf_avail, true);
		wake_up(&req->wq_buf);
	}
}

static void virtio_pmem_clear_inflight(struct virtio_pmem *vpmem,
				       struct virtio_pmem_request *req)
{
	if (vpmem->req_inflight == req)
		vpmem->req_inflight = NULL;
}

static void virtio_pmem_wake_inflight(struct virtio_pmem *vpmem)
{
	struct virtio_pmem_request *req = vpmem->req_inflight;

	if (req)
		wake_up(&req->host_acked);
}

void virtio_pmem_mark_broken(struct virtio_pmem *vpmem)
{
	if (!READ_ONCE(vpmem->broken)) {
		WRITE_ONCE(vpmem->broken, true);
		dev_err_once(&vpmem->vdev->dev, "virtqueue is broken\n");
	}

	virtio_pmem_wake_inflight(vpmem);
	virtio_pmem_wake_all_waiters(vpmem);
}
EXPORT_SYMBOL_GPL(virtio_pmem_mark_broken);

void virtio_pmem_drain(struct virtio_pmem *vpmem)
{
	struct virtio_pmem_request *req;
	unsigned int len;

	if (!vpmem->req_vq)
		return;

	while ((req = virtqueue_get_buf(vpmem->req_vq, &len)) != NULL) {
		virtio_pmem_clear_inflight(vpmem, req);
		virtio_pmem_complete_err(req);
		kref_put(&req->kref, virtio_pmem_req_release);
	}

	while ((req = virtqueue_detach_unused_buf(vpmem->req_vq)) != NULL) {
		virtio_pmem_clear_inflight(vpmem, req);
		virtio_pmem_complete_err(req);
		kref_put(&req->kref, virtio_pmem_req_release);
	}
}
EXPORT_SYMBOL_GPL(virtio_pmem_drain);

 /* The interrupt handler */
void virtio_pmem_host_ack(struct virtqueue *vq)
{
	struct virtio_pmem *vpmem = vq->vdev->priv;
	struct virtio_pmem_request *req_data;
	unsigned long flags;
	unsigned int len;

	spin_lock_irqsave(&vpmem->pmem_lock, flags);
	while ((req_data = virtqueue_get_buf(vq, &len)) != NULL) {
		virtio_pmem_clear_inflight(vpmem, req_data);
		virtio_pmem_wake_one_waiter(vpmem);
		if (READ_ONCE(vpmem->broken))
			virtio_pmem_complete_err(req_data);
		else
			virtio_pmem_signal_done(req_data);
		kref_put(&req_data->kref, virtio_pmem_req_release);
	}
	spin_unlock_irqrestore(&vpmem->pmem_lock, flags);
}
EXPORT_SYMBOL_GPL(virtio_pmem_host_ack);

 /* The request submission function */
static int virtio_pmem_flush(struct nd_region *nd_region)
{
	struct virtio_device *vdev = nd_region->provider_data;
	struct virtio_pmem *vpmem  = vdev->priv;
	struct virtio_pmem_request *req_data;
	struct scatterlist *sgs[2], sg, ret;
	unsigned long flags;
	int err, err1;

	guard(mutex)(&vpmem->flush_lock);

	/*
	 * Don't bother to submit the request to the device if the device is
	 * not activated.
	 */
	if (vdev->config->get_status(vdev) & VIRTIO_CONFIG_S_NEEDS_RESET) {
		dev_info(&vdev->dev, "virtio pmem device needs a reset\n");
		return -EIO;
	}

	if (READ_ONCE(vpmem->broken))
		return -EIO;

	req_data = kmalloc_obj(*req_data, GFP_NOIO);
	if (!req_data)
		return -ENOMEM;

	kref_init(&req_data->kref);
	WRITE_ONCE(req_data->done, false);
	init_waitqueue_head(&req_data->host_acked);
	init_waitqueue_head(&req_data->wq_buf);
	INIT_LIST_HEAD(&req_data->list);
	req_data->req.type = cpu_to_le32(VIRTIO_PMEM_REQ_TYPE_FLUSH);
	sg_init_one(&sg, &req_data->req, sizeof(req_data->req));
	sgs[0] = &sg;
	sg_init_one(&ret, &req_data->resp.ret, sizeof(req_data->resp));
	sgs[1] = &ret;

	spin_lock_irqsave(&vpmem->pmem_lock, flags);
	/*
	 * If virtqueue_add_sgs returns -ENOSPC then req_vq virtual
	 * queue does not have free descriptor. We add the request
	 * to req_list and wait for host_ack to wake us up when free
	 * slots are available.
	 */
	for (;;) {
		if (READ_ONCE(vpmem->broken)) {
			err = -EIO;
			break;
		}

		err = virtqueue_add_sgs(vpmem->req_vq, sgs, 1, 1, req_data,
					GFP_ATOMIC);
		if (!err) {
			/*
			 * Take the virtqueue reference while @pmem_lock is
			 * held so completion cannot run concurrently.
			 */
			kref_get(&req_data->kref);
			vpmem->req_inflight = req_data;
			break;
		}

		if (err != -ENOSPC)
			break;

		dev_info_ratelimited(&vdev->dev,
				     "failed to send command to virtio pmem device, no free slots in the virtqueue\n");
		WRITE_ONCE(req_data->wq_buf_avail, false);
		list_add_tail(&req_data->list, &vpmem->req_list);
		spin_unlock_irqrestore(&vpmem->pmem_lock, flags);

		/* A host response results in "host_ack" getting called */
		wait_event(req_data->wq_buf,
			   READ_ONCE(req_data->wq_buf_avail) ||
			   READ_ONCE(vpmem->broken));
		spin_lock_irqsave(&vpmem->pmem_lock, flags);

		if (READ_ONCE(vpmem->broken))
			break;
	}

	if (READ_ONCE(vpmem->broken))
		err = -EIO;
	if (err == -EIO || virtqueue_is_broken(vpmem->req_vq))
		virtio_pmem_mark_broken(vpmem);

	err1 = true;
	if (!err && !READ_ONCE(vpmem->broken)) {
		err1 = virtqueue_kick(vpmem->req_vq);
		if (!err1)
			virtio_pmem_mark_broken(vpmem);
	}
	spin_unlock_irqrestore(&vpmem->pmem_lock, flags);
	/*
	 * virtqueue_add_sgs failed with error different than -ENOSPC, we can't
	 * do anything about that.
	 */
	if (READ_ONCE(vpmem->broken) || err || !err1) {
		dev_info(&vdev->dev, "failed to send command to virtio pmem device\n");
		err = -EIO;
	} else {
		/* A host response results in "host_ack" getting called */
		wait_event(req_data->host_acked,
			   virtio_pmem_req_done(req_data) ||
			   READ_ONCE(vpmem->broken));
		if (virtio_pmem_req_done(req_data))
			err = le32_to_cpu(req_data->resp.ret);
		else
			err = -EIO;
	}

	kref_put(&req_data->kref, virtio_pmem_req_release);
	return err;
};

static void virtio_pmem_flush_work(struct work_struct *work)
{
	struct virtio_pmem_flush_work *flush;
	int err;

	flush = container_of(work, struct virtio_pmem_flush_work, work);
	err = virtio_pmem_flush(flush->nd_region);
	if (err > 0)
		err = -EIO;
	if (err)
		flush->bio->bi_status = errno_to_blk_status(err);
	bio_endio(flush->bio);
	kfree(flush);
}

/* The asynchronous flush callback function */
int async_pmem_flush(struct nd_region *nd_region, struct bio *bio)
{
	struct virtio_device *vdev = nd_region->provider_data;
	struct virtio_pmem *vpmem = vdev->priv;
	struct virtio_pmem_flush_work *flush;
	unsigned long flags;
	int err;

	if (bio && bio->bi_iter.bi_sector != -1) {
		flush = kmalloc_obj(*flush, GFP_NOIO);
		if (!flush)
			return -ENOMEM;

		INIT_WORK(&flush->work, virtio_pmem_flush_work);
		flush->nd_region = nd_region;
		flush->bio = bio;

		spin_lock_irqsave(&vpmem->pmem_lock, flags);
		if (READ_ONCE(vpmem->broken)) {
			spin_unlock_irqrestore(&vpmem->pmem_lock, flags);
			kfree(flush);
			return -EIO;
		}
		queue_work(vpmem->flush_wq, &flush->work);
		spin_unlock_irqrestore(&vpmem->pmem_lock, flags);
		return NVDIMM_FLUSH_ASYNC;
	}

	err = virtio_pmem_flush(nd_region);
	if (err > 0)
		return -EIO;

	return err;
};
EXPORT_SYMBOL_GPL(async_pmem_flush);
MODULE_DESCRIPTION("Virtio Persistent Memory Driver");
MODULE_LICENSE("GPL");
