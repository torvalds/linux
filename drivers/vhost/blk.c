/*
 * Copyright (C) 2011 Taobao, Inc.
 * Author: Liu Yuan <tailai.ly@taobao.com>
 *
 * Copyright (C) 2012 Red Hat, Inc.
 * Author: Asias He <asias@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 * virtio-blk server in host kernel.
 */

#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/vhost.h>
#include <linux/virtio_blk.h>
#include <linux/mutex.h>
#include <linux/file.h>
#include <linux/kthread.h>
#include <linux/blkdev.h>
#include <linux/llist.h>

#include "vhost.c"
#include "vhost.h"
#include "blk.h"

static DEFINE_IDA(vhost_blk_index_ida);

enum {
	VHOST_BLK_VQ_REQ = 0,
	VHOST_BLK_VQ_MAX = 1,
};

struct req_page_list {
	struct page **pages;
	int pages_nr;
};

#define NR_INLINE 16

struct vhost_blk_req {
	struct req_page_list inline_pl[NR_INLINE];
	struct page *inline_page[NR_INLINE];
	struct bio *inline_bio[NR_INLINE];
	struct req_page_list *pl;
	int during_flush;
	bool use_inline;

	struct llist_node llnode;

	struct vhost_blk *blk;

	struct iovec *iov;
	int iov_nr;

	struct bio **bio;
	atomic_t bio_nr;

	struct iovec status[1];

	sector_t sector;
	int write;
	u16 head;
	long len;
};

struct vhost_blk_virtqueue {
	struct vhost_virtqueue vq;
};

struct vhost_blk {
	struct vhost_blk_virtqueue vqs[VHOST_BLK_VQ_MAX];
	wait_queue_head_t flush_wait;
	struct iovec iov[UIO_MAXIOV];
	struct vhost_blk_req *reqs;
	struct llist_head llhead;
	atomic_t req_inflight[2];
	struct vhost_work work;
	spinlock_t flush_lock;
	struct vhost_dev dev;
	int during_flush;
	u16 reqs_nr;
	int index;
};

static int move_iovec(struct iovec *from, struct iovec *to,
		      size_t len, int iov_count)
{
	int seg = 0;
	size_t size;

	while (len && seg < iov_count) {
		if (from->iov_len == 0) {
			++from;
			continue;
		}
		size = min(from->iov_len, len);
		to->iov_base = from->iov_base;
		to->iov_len = size;
		from->iov_len -= size;
		from->iov_base += size;
		len -= size;
		++from;
		++to;
		++seg;
	}
	return seg;
}

static inline int iov_num_pages(struct iovec *iov)
{
	return (PAGE_ALIGN((unsigned long)iov->iov_base + iov->iov_len) -
	       ((unsigned long)iov->iov_base & PAGE_MASK)) >> PAGE_SHIFT;
}

static inline int vhost_blk_set_status(struct vhost_blk_req *req, u8 status)
{
	struct vhost_blk *blk = req->blk;
	int ret;

	ret = memcpy_toiovecend(req->status, &status, 0, sizeof(status));

	if (ret) {
		vq_err(&blk->vqs[VHOST_BLK_VQ_REQ].vq, "Failed to write status\n");
		return -EFAULT;
	}

	return 0;
}

static void vhost_blk_req_done(struct bio *bio, int err)
{
	struct vhost_blk_req *req = bio->bi_private;
	struct vhost_blk *blk = req->blk;

	if (err)
		req->len = err;

	if (atomic_dec_and_test(&req->bio_nr)) {
		llist_add(&req->llnode, &blk->llhead);
		vhost_work_queue(&blk->dev, &blk->work);
	}

	bio_put(bio);
}

static void vhost_blk_req_unmap(struct vhost_blk_req *req)
{
	struct req_page_list *pl;
	int i, j;

	if (req->pl) {
		for (i = 0; i < req->iov_nr; i++) {
			pl = &req->pl[i];
			for (j = 0; j < pl->pages_nr; j++) {
				if (!req->write)
					set_page_dirty_lock(pl->pages[j]);
				put_page(pl->pages[j]);
			}
		}
	}

	if (!req->use_inline)
		kfree(req->pl);
}

static int vhost_blk_bio_make(struct vhost_blk_req *req,
			      struct block_device *bdev)
{
	int pages_nr_total, i, j, ret;
	struct iovec *iov = req->iov;
	int iov_nr = req->iov_nr;
	struct page **pages, *page;
	struct bio *bio = NULL;
	int bio_nr = 0;
	void *buf;

	pages_nr_total = 0;
	for (i = 0; i < iov_nr; i++)
		pages_nr_total += iov_num_pages(&iov[i]);

	if (unlikely(req->write == WRITE_FLUSH)) {
		req->use_inline = true;
		req->pl = NULL;
		req->bio = req->inline_bio;

		bio = bio_alloc(GFP_KERNEL, 1);
		if (!bio)
			return -ENOMEM;

		bio->bi_sector  = req->sector;
		bio->bi_bdev    = bdev;
		bio->bi_private = req;
		bio->bi_end_io  = vhost_blk_req_done;
		req->bio[bio_nr++] = bio;

		goto out;
	}

	if (pages_nr_total > NR_INLINE) {
		int pl_len, page_len, bio_len;

		req->use_inline = false;
		pl_len = iov_nr * sizeof(req->pl[0]);
		page_len = pages_nr_total * sizeof(struct page *);
		bio_len = pages_nr_total * sizeof(struct bio *);

		buf = kmalloc(pl_len + page_len + bio_len, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		req->pl	= buf;
		pages = buf + pl_len;
		req->bio = buf + pl_len + page_len;
	} else {
		req->use_inline = true;
		req->pl = req->inline_pl;
		pages = req->inline_page;
		req->bio = req->inline_bio;
	}

	for (req->iov_nr = i = 0; i < iov_nr; i++) {
		int pages_nr = iov_num_pages(&iov[i]);
		unsigned long iov_base, iov_len;
		struct req_page_list *pl;

		iov_base = (unsigned long)iov[i].iov_base;
		iov_len  = (unsigned long)iov[i].iov_len;

		/* TODO: Limit the total number of pages pinned */
		ret = get_user_pages_fast(iov_base, pages_nr,
					  !req->write, pages);
		/* No pages were pinned */
		if (ret < 0)
			goto fail;

		req->iov_nr++;
		pl = &req->pl[i];
		pl->pages_nr = ret;
		pl->pages = pages;

		/* Less pages pinned than wanted */
		if (ret != pages_nr)
			goto fail;

		for (j = 0; j < pages_nr; j++) {
			unsigned int off, len;
			page = pages[j];
			off = iov_base & ~PAGE_MASK;
			len = PAGE_SIZE - off;
			if (len > iov_len)
				len = iov_len;

			while (!bio || bio_add_page(bio, page, len, off) <= 0) {
				bio = bio_alloc(GFP_KERNEL, pages_nr_total);
				if (!bio)
					goto fail;
				bio->bi_sector  = req->sector;
				bio->bi_bdev    = bdev;
				bio->bi_private = req;
				bio->bi_end_io  = vhost_blk_req_done;
				req->bio[bio_nr++] = bio;
			}
			req->sector	+= len >> 9;
			iov_base	+= len;
			iov_len		-= len;
		}

		pages += pages_nr;
	}
out:
	atomic_set(&req->bio_nr, bio_nr);
	return 0;

fail:
	for (i = 0; i < bio_nr; i++)
		bio_put(req->bio[i]);
	vhost_blk_req_unmap(req);
	return -ENOMEM;
}

static inline void vhost_blk_bio_send(struct vhost_blk_req *req)
{
	struct blk_plug plug;
	int i, bio_nr;

	bio_nr = atomic_read(&req->bio_nr);
	blk_start_plug(&plug);
	for (i = 0; i < bio_nr; i++)
		submit_bio(req->write, req->bio[i]);
	blk_finish_plug(&plug);
}

static int vhost_blk_req_submit(struct vhost_blk_req *req, struct file *file)
{

	struct inode *inode = file->f_mapping->host;
	struct block_device *bdev = inode->i_bdev;
	int ret;

	ret = vhost_blk_bio_make(req, bdev);
	if (ret < 0)
		return ret;

	vhost_blk_bio_send(req);

	spin_lock(&req->blk->flush_lock);
	req->during_flush = req->blk->during_flush;
	atomic_inc(&req->blk->req_inflight[req->during_flush]);
	spin_unlock(&req->blk->flush_lock);

	return ret;
}

static int vhost_blk_req_handle(struct vhost_virtqueue *vq,
				struct virtio_blk_outhdr *hdr,
				u16 head, u16 out, u16 in,
				struct file *file)
{
	struct vhost_blk *blk = container_of(vq->dev, struct vhost_blk, dev);
	unsigned char id[VIRTIO_BLK_ID_BYTES];
	struct vhost_blk_req *req;
	int ret, len;
	u8 status;

	req		= &blk->reqs[head];
	req->head	= head;
	req->blk	= blk;
	req->sector	= hdr->sector;
	req->iov	= blk->iov;

	req->len	= iov_length(vq->iov, out + in) - sizeof(status);
	req->iov_nr	= move_iovec(vq->iov, req->iov, req->len, out + in);

	move_iovec(vq->iov, req->status, sizeof(status), out + in);

	switch (hdr->type) {
	case VIRTIO_BLK_T_OUT:
		req->write = WRITE;
		ret = vhost_blk_req_submit(req, file);
		break;
	case VIRTIO_BLK_T_IN:
		req->write = READ;
		ret = vhost_blk_req_submit(req, file);
		break;
	case VIRTIO_BLK_T_FLUSH:
		req->write = WRITE_FLUSH;
		ret = vhost_blk_req_submit(req, file);
		break;
	case VIRTIO_BLK_T_GET_ID:
		ret = snprintf(id, VIRTIO_BLK_ID_BYTES,
			       "vhost-blk%d", blk->index);
		if (ret < 0)
			break;
		len = ret;
		ret = memcpy_toiovecend(req->iov, id, 0, len);
		status = ret < 0 ? VIRTIO_BLK_S_IOERR : VIRTIO_BLK_S_OK;
		ret = vhost_blk_set_status(req, status);
		if (ret)
			break;
		vhost_add_used_and_signal(&blk->dev, vq, head, len);
		break;
	default:
		vq_err(vq, "Unsupported request type %d\n", hdr->type);
		status = VIRTIO_BLK_S_UNSUPP;
		ret = vhost_blk_set_status(req, status);
		if (ret)
			break;
		vhost_add_used_and_signal(&blk->dev, vq, head, 0);
	}

	return ret;
}

/* Guest kick us for I/O submit */
static void vhost_blk_handle_guest_kick(struct vhost_work *work)
{
	struct virtio_blk_outhdr hdr;
	struct vhost_virtqueue *vq;
	struct vhost_blk *blk;
	struct iovec hdr_iov;
	int in, out, ret;
	struct file *f;
	u16 head;

	vq = container_of(work, struct vhost_virtqueue, poll.work);
	blk = container_of(vq->dev, struct vhost_blk, dev);

	/* TODO: check that we are running from vhost_worker? */
	f = rcu_dereference_check(vq->private_data, 1);
	if (!f)
		return;

	vhost_disable_notify(&blk->dev, vq);
	for (;;) {
		head = vhost_get_vq_desc(&blk->dev, vq, vq->iov,
					 ARRAY_SIZE(vq->iov),
					 &out, &in, NULL, NULL);
		if (unlikely(head < 0))
			break;

		if (unlikely(head == vq->num)) {
			if (unlikely(vhost_enable_notify(&blk->dev, vq))) {
				vhost_disable_notify(&blk->dev, vq);
				continue;
			}
			break;
		}
		move_iovec(vq->iov, &hdr_iov, sizeof(hdr), out);
		ret = memcpy_fromiovecend((unsigned char *)&hdr, &hdr_iov, 0,
					   sizeof(hdr));
		if (ret < 0) {
			vq_err(vq, "Failed to get block header!\n");
			vhost_discard_vq_desc(vq, 1);
			break;
		}

		if (vhost_blk_req_handle(vq, &hdr, head, out, in, f) < 0)
			break;

		if (!llist_empty(&blk->llhead)) {
			vhost_poll_queue(&vq->poll);
			break;
		}
	}
}

/* Host kick us for I/O completion */
static void vhost_blk_handle_host_kick(struct vhost_work *work)
{

	struct vhost_virtqueue *vq;
	struct vhost_blk_req *req;
	struct llist_node *llnode;
	struct vhost_blk *blk;
	bool added, zero;
	u8 status;
	int ret;

	blk = container_of(work, struct vhost_blk, work);
	vq = &blk->vqs[VHOST_BLK_VQ_REQ].vq;

	llnode = llist_del_all(&blk->llhead);
	added = false;
	while (llnode) {
		req = llist_entry(llnode, struct vhost_blk_req, llnode);
		llnode = llist_next(llnode);

		vhost_blk_req_unmap(req);

		status = req->len >= 0 ?  VIRTIO_BLK_S_OK : VIRTIO_BLK_S_IOERR;
		ret = vhost_blk_set_status(req, status);
		if (unlikely(ret))
			continue;
		vhost_add_used(&blk->vqs[VHOST_BLK_VQ_REQ].vq, req->head, req->len);
		added = true;

		spin_lock(&req->blk->flush_lock);
		zero = atomic_dec_and_test(
				&req->blk->req_inflight[req->during_flush]);
		if (zero && !req->during_flush)
			wake_up(&blk->flush_wait);
		spin_unlock(&req->blk->flush_lock);

	}
	if (likely(added))
		vhost_signal(&blk->dev, &blk->vqs[VHOST_BLK_VQ_REQ].vq);
}

static void vhost_blk_flush(struct vhost_blk *blk)
{
	spin_lock(&blk->flush_lock);
	blk->during_flush = 1;
	spin_unlock(&blk->flush_lock);

	vhost_poll_flush(&blk->vqs[VHOST_BLK_VQ_REQ].vq.poll);
	vhost_work_flush(&blk->dev, &blk->work);
	/*
	 * Wait until requests fired before the flush to be finished
	 * req_inflight[0] is used to track the requests fired before the flush
	 * req_inflight[1] is used to track the requests fired during the flush
	 */
	wait_event(blk->flush_wait, !atomic_read(&blk->req_inflight[0]));

	spin_lock(&blk->flush_lock);
	blk->during_flush = 0;
	spin_unlock(&blk->flush_lock);
}

static inline void vhost_blk_stop(struct vhost_blk *blk, struct file **file)
{
	struct vhost_virtqueue *vq = &blk->vqs[VHOST_BLK_VQ_REQ].vq;
	struct file *f;

	mutex_lock(&vq->mutex);
	f = rcu_dereference_protected(vq->private_data,
				      lockdep_is_held(&vq->mutex));
	rcu_assign_pointer(vq->private_data, NULL);
	mutex_unlock(&vq->mutex);

	*file = f;
}

static int vhost_blk_open(struct inode *inode, struct file *file)
{
	struct vhost_blk *blk;
	struct vhost_virtqueue **vqs;
	int ret;

	blk = kzalloc(sizeof(*blk), GFP_KERNEL);
	if (!blk) {
		ret = -ENOMEM;
		goto out;
	}

	vqs = kmalloc(VHOST_BLK_VQ_MAX * sizeof(*vqs), GFP_KERNEL);
	if (!vqs) {
		kfree(blk);
		return -ENOMEM;
	}

	ret = ida_simple_get(&vhost_blk_index_ida, 0, 0, GFP_KERNEL);
	if (ret < 0)
		goto out_dev;
	blk->index = ret;

	vqs[VHOST_BLK_VQ_REQ] = &blk->vqs[VHOST_BLK_VQ_REQ].vq;
	blk->vqs[VHOST_BLK_VQ_REQ].vq.handle_kick = vhost_blk_handle_guest_kick;
	atomic_set(&blk->req_inflight[0], 0);
	atomic_set(&blk->req_inflight[1], 0);
	blk->during_flush = 0;
	spin_lock_init(&blk->flush_lock);
	init_waitqueue_head(&blk->flush_wait);

	ret = vhost_dev_init(&blk->dev, vqs, VHOST_BLK_VQ_MAX);
	if (ret < 0)
		goto out_dev;
	file->private_data = blk;

	vhost_work_init(&blk->work, vhost_blk_handle_host_kick);

	return ret;
out_dev:
	kfree(blk);
out:
	return ret;
}

static int vhost_blk_release(struct inode *inode, struct file *f)
{
	struct vhost_blk *blk = f->private_data;
	struct file *file;

	ida_simple_remove(&vhost_blk_index_ida, blk->index);
	vhost_blk_stop(blk, &file);
	vhost_blk_flush(blk);
	vhost_dev_cleanup(&blk->dev, false);
	if (file)
		fput(file);
	kfree(blk->reqs);
	kfree(blk->dev.vqs);
	kfree(blk);

	return 0;
}

static int vhost_blk_set_features(struct vhost_blk *blk, u64 features)
{
	mutex_lock(&blk->dev.mutex);
	blk->dev.acked_features = features;
	vhost_blk_flush(blk);
	mutex_unlock(&blk->dev.mutex);

	return 0;
}

static long vhost_blk_set_backend(struct vhost_blk *blk, unsigned index, int fd)
{
	struct vhost_virtqueue *vq = &blk->vqs[VHOST_BLK_VQ_REQ].vq;
	struct file *file, *oldfile;
	struct inode *inode;
	int ret;

	mutex_lock(&blk->dev.mutex);
	ret = vhost_dev_check_owner(&blk->dev);
	if (ret)
		goto out_dev;

	if (index >= VHOST_BLK_VQ_MAX) {
		ret = -ENOBUFS;
		goto out_dev;
	}

	mutex_lock(&vq->mutex);

	if (!vhost_vq_access_ok(vq)) {
		ret = -EFAULT;
		goto out_vq;
	}

	file = fget(fd);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto out_vq;
	}

	/* Only raw block device is supported for now */
	inode = file->f_mapping->host;
	if (!S_ISBLK(inode->i_mode)) {
		ret = -EFAULT;
		goto out_file;
	}

	oldfile = rcu_dereference_protected(vq->private_data,
			lockdep_is_held(&vq->mutex));
	if (file != oldfile) {
		rcu_assign_pointer(vq->private_data, file);

		ret = vhost_init_used(vq);
		if (ret)
			goto out_file;
	}

	mutex_unlock(&vq->mutex);

	if (oldfile) {
		vhost_blk_flush(blk);
		fput(oldfile);
	}

	mutex_unlock(&blk->dev.mutex);
	return 0;

out_file:
	fput(file);
out_vq:
	mutex_unlock(&vq->mutex);
out_dev:
	mutex_unlock(&blk->dev.mutex);
	return ret;
}

static long vhost_blk_reset_owner(struct vhost_blk *blk)
{
	struct vhost_memory *memory;
	struct file *file = NULL;
	int err;

	mutex_lock(&blk->dev.mutex);
	err = vhost_dev_check_owner(&blk->dev);
	if (err)
		goto done;
	memory = vhost_dev_reset_owner_prepare();
	if (!memory) {
		err = -ENOMEM;
		goto done;
	}
	vhost_blk_stop(blk, &file);
	vhost_blk_flush(blk);
	vhost_dev_reset_owner(&blk->dev, memory);
done:
	mutex_unlock(&blk->dev.mutex);
	if (file)
		fput(file);
	return err;
}

static int vhost_blk_setup(struct vhost_blk *blk)
{
	blk->reqs_nr = blk->vqs[VHOST_BLK_VQ_REQ].vq.num;

	blk->reqs = kmalloc(sizeof(struct vhost_blk_req) * blk->reqs_nr,
			    GFP_KERNEL);
	if (!blk->reqs)
		return -ENOMEM;

	return 0;
}

static long vhost_blk_ioctl(struct file *f, unsigned int ioctl,
			    unsigned long arg)
{
	struct vhost_blk *blk = f->private_data;
	void __user *argp = (void __user *)arg;
	struct vhost_vring_file backend;
	u64 __user *featurep = argp;
	u64 features;
	int ret;

	switch (ioctl) {
	case VHOST_BLK_SET_BACKEND:
		if (copy_from_user(&backend, argp, sizeof(backend)))
			return -EFAULT;
		return vhost_blk_set_backend(blk, backend.index, backend.fd);
	case VHOST_GET_FEATURES:
		features = VHOST_BLK_FEATURES;
		if (copy_to_user(featurep, &features, sizeof(features)))
			return -EFAULT;
		return 0;
	case VHOST_SET_FEATURES:
		if (copy_from_user(&features, featurep, sizeof(features)))
			return -EFAULT;
		if (features & ~VHOST_BLK_FEATURES)
			return -EOPNOTSUPP;
		return vhost_blk_set_features(blk, features);
	case VHOST_RESET_OWNER:
		return vhost_blk_reset_owner(blk);
	default:
		mutex_lock(&blk->dev.mutex);
		ret = vhost_dev_ioctl(&blk->dev, ioctl, argp);
		if (ret == -ENOIOCTLCMD) {
			ret = vhost_vring_ioctl(&blk->dev, ioctl, argp);
			if (!ret && ioctl == VHOST_SET_VRING_NUM)
				ret = vhost_blk_setup(blk);
		} else {
			vhost_blk_flush(blk);
		}
		mutex_unlock(&blk->dev.mutex);
		return ret;
	}
}

static const struct file_operations vhost_blk_fops = {
	.owner          = THIS_MODULE,
	.open           = vhost_blk_open,
	.release        = vhost_blk_release,
	.llseek		= noop_llseek,
	.unlocked_ioctl = vhost_blk_ioctl,
};

static struct miscdevice vhost_blk_misc = {
	MISC_DYNAMIC_MINOR,
	"vhost-blk",
	&vhost_blk_fops,
};

static int vhost_blk_init(void)
{
	return misc_register(&vhost_blk_misc);
}

static void vhost_blk_exit(void)
{
	misc_deregister(&vhost_blk_misc);
}

module_init(vhost_blk_init);
module_exit(vhost_blk_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Asias He");
MODULE_DESCRIPTION("Host kernel accelerator for virtio_blk");
