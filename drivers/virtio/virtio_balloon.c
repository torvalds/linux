/* Virtio balloon implementation, inspired by Dor Loar and Marcelo
 * Tosatti's implementations.
 *
 *  Copyright 2008 Rusty Russell IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
//#define DEBUG
#include <linux/virtio.h>
#include <linux/virtio_balloon.h>
#include <linux/swap.h>
#include <linux/kthread.h>
#include <linux/freezer.h>

struct virtio_balloon
{
	struct virtio_device *vdev;
	struct virtqueue *inflate_vq, *deflate_vq;

	/* Where the ballooning thread waits for config to change. */
	wait_queue_head_t config_change;

	/* The thread servicing the balloon. */
	struct task_struct *thread;

	/* Waiting for host to ack the pages we released. */
	struct completion acked;

	/* Do we have to tell Host *before* we reuse pages? */
	bool tell_host_first;

	/* The pages we've told the Host we're not using. */
	unsigned int num_pages;
	struct list_head pages;

	/* The array of pfns we tell the Host about. */
	unsigned int num_pfns;
	u32 pfns[256];
};

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_BALLOON, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static void balloon_ack(struct virtqueue *vq)
{
	struct virtio_balloon *vb;
	unsigned int len;

	vb = vq->vq_ops->get_buf(vq, &len);
	if (vb)
		complete(&vb->acked);
}

static void tell_host(struct virtio_balloon *vb, struct virtqueue *vq)
{
	struct scatterlist sg;

	sg_init_one(&sg, vb->pfns, sizeof(vb->pfns[0]) * vb->num_pfns);

	init_completion(&vb->acked);

	/* We should always be able to add one buffer to an empty queue. */
	if (vq->vq_ops->add_buf(vq, &sg, 1, 0, vb) != 0)
		BUG();
	vq->vq_ops->kick(vq);

	/* When host has read buffer, this completes via balloon_ack */
	wait_for_completion(&vb->acked);
}

static void fill_balloon(struct virtio_balloon *vb, size_t num)
{
	/* We can only do one array worth at a time. */
	num = min(num, ARRAY_SIZE(vb->pfns));

	for (vb->num_pfns = 0; vb->num_pfns < num; vb->num_pfns++) {
		struct page *page = alloc_page(GFP_HIGHUSER | __GFP_NORETRY);
		if (!page) {
			if (printk_ratelimit())
				dev_printk(KERN_INFO, &vb->vdev->dev,
					   "Out of puff! Can't get %zu pages\n",
					   num);
			/* Sleep for at least 1/5 of a second before retry. */
			msleep(200);
			break;
		}
		vb->pfns[vb->num_pfns] = page_to_pfn(page);
		totalram_pages--;
		vb->num_pages++;
		list_add(&page->lru, &vb->pages);
	}

	/* Didn't get any?  Oh well. */
	if (vb->num_pfns == 0)
		return;

	tell_host(vb, vb->inflate_vq);
}

static void release_pages_by_pfn(const u32 pfns[], unsigned int num)
{
	unsigned int i;

	for (i = 0; i < num; i++) {
		__free_page(pfn_to_page(pfns[i]));
		totalram_pages++;
	}
}

static void leak_balloon(struct virtio_balloon *vb, size_t num)
{
	struct page *page;

	/* We can only do one array worth at a time. */
	num = min(num, ARRAY_SIZE(vb->pfns));

	for (vb->num_pfns = 0; vb->num_pfns < num; vb->num_pfns++) {
		page = list_first_entry(&vb->pages, struct page, lru);
		list_del(&page->lru);
		vb->pfns[vb->num_pfns] = page_to_pfn(page);
		vb->num_pages--;
	}

	if (vb->tell_host_first) {
		tell_host(vb, vb->deflate_vq);
		release_pages_by_pfn(vb->pfns, vb->num_pfns);
	} else {
		release_pages_by_pfn(vb->pfns, vb->num_pfns);
		tell_host(vb, vb->deflate_vq);
	}
}

static void virtballoon_changed(struct virtio_device *vdev)
{
	struct virtio_balloon *vb = vdev->priv;

	wake_up(&vb->config_change);
}

static inline int towards_target(struct virtio_balloon *vb)
{
	u32 v;
	__virtio_config_val(vb->vdev,
			    offsetof(struct virtio_balloon_config, num_pages),
			    &v);
	return v - vb->num_pages;
}

static void update_balloon_size(struct virtio_balloon *vb)
{
	__le32 actual = cpu_to_le32(vb->num_pages);

	vb->vdev->config->set(vb->vdev,
			      offsetof(struct virtio_balloon_config, actual),
			      &actual, sizeof(actual));
}

static int balloon(void *_vballoon)
{
	struct virtio_balloon *vb = _vballoon;

	set_freezable();
	while (!kthread_should_stop()) {
		int diff;

		try_to_freeze();
		wait_event_interruptible(vb->config_change,
					 (diff = towards_target(vb)) != 0
					 || kthread_should_stop());
		if (diff > 0)
			fill_balloon(vb, diff);
		else if (diff < 0)
			leak_balloon(vb, -diff);
		update_balloon_size(vb);
	}
	return 0;
}

static int virtballoon_probe(struct virtio_device *vdev)
{
	struct virtio_balloon *vb;
	int err;

	vdev->priv = vb = kmalloc(sizeof(*vb), GFP_KERNEL);
	if (!vb) {
		err = -ENOMEM;
		goto out;
	}

	INIT_LIST_HEAD(&vb->pages);
	vb->num_pages = 0;
	init_waitqueue_head(&vb->config_change);
	vb->vdev = vdev;

	/* We expect two virtqueues. */
	vb->inflate_vq = vdev->config->find_vq(vdev, 0, balloon_ack);
	if (IS_ERR(vb->inflate_vq)) {
		err = PTR_ERR(vb->inflate_vq);
		goto out_free_vb;
	}

	vb->deflate_vq = vdev->config->find_vq(vdev, 1, balloon_ack);
	if (IS_ERR(vb->deflate_vq)) {
		err = PTR_ERR(vb->deflate_vq);
		goto out_del_inflate_vq;
	}

	vb->thread = kthread_run(balloon, vb, "vballoon");
	if (IS_ERR(vb->thread)) {
		err = PTR_ERR(vb->thread);
		goto out_del_deflate_vq;
	}

	vb->tell_host_first
		= vdev->config->feature(vdev, VIRTIO_BALLOON_F_MUST_TELL_HOST);

	return 0;

out_del_deflate_vq:
	vdev->config->del_vq(vb->deflate_vq);
out_del_inflate_vq:
	vdev->config->del_vq(vb->inflate_vq);
out_free_vb:
	kfree(vb);
out:
	return err;
}

static void virtballoon_remove(struct virtio_device *vdev)
{
	struct virtio_balloon *vb = vdev->priv;

	kthread_stop(vb->thread);

	/* There might be pages left in the balloon: free them. */
	while (vb->num_pages)
		leak_balloon(vb, vb->num_pages);

	/* Now we reset the device so we can clean up the queues. */
	vdev->config->reset(vdev);

	vdev->config->del_vq(vb->deflate_vq);
	vdev->config->del_vq(vb->inflate_vq);
	kfree(vb);
}

static struct virtio_driver virtio_balloon = {
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table =	id_table,
	.probe =	virtballoon_probe,
	.remove =	__devexit_p(virtballoon_remove),
	.config_changed = virtballoon_changed,
};

static int __init init(void)
{
	return register_virtio_driver(&virtio_balloon);
}

static void __exit fini(void)
{
	unregister_virtio_driver(&virtio_balloon);
}
module_init(init);
module_exit(fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio balloon driver");
MODULE_LICENSE("GPL");
