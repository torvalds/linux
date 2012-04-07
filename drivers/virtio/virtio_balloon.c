/*
 * Virtio balloon implementation, inspired by Dor Laor and Marcelo
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

#include <linux/virtio.h>
#include <linux/virtio_balloon.h>
#include <linux/swap.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>

struct virtio_balloon
{
	struct virtio_device *vdev;
	struct virtqueue *inflate_vq, *deflate_vq, *stats_vq;

	/* Where the ballooning thread waits for config to change. */
	wait_queue_head_t config_change;

	/* The thread servicing the balloon. */
	struct task_struct *thread;

	/* Waiting for host to ack the pages we released. */
	struct completion acked;

	/* The pages we've told the Host we're not using. */
	unsigned int num_pages;
	struct list_head pages;

	/* The array of pfns we tell the Host about. */
	unsigned int num_pfns;
	u32 pfns[256];

	/* Memory statistics */
	int need_stats_update;
	struct virtio_balloon_stat stats[VIRTIO_BALLOON_S_NR];
};

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_BALLOON, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static u32 page_to_balloon_pfn(struct page *page)
{
	unsigned long pfn = page_to_pfn(page);

	BUILD_BUG_ON(PAGE_SHIFT < VIRTIO_BALLOON_PFN_SHIFT);
	/* Convert pfn from Linux page size to balloon page size. */
	return pfn >> (PAGE_SHIFT - VIRTIO_BALLOON_PFN_SHIFT);
}

static void balloon_ack(struct virtqueue *vq)
{
	struct virtio_balloon *vb;
	unsigned int len;

	vb = virtqueue_get_buf(vq, &len);
	if (vb)
		complete(&vb->acked);
}

static void tell_host(struct virtio_balloon *vb, struct virtqueue *vq)
{
	struct scatterlist sg;

	sg_init_one(&sg, vb->pfns, sizeof(vb->pfns[0]) * vb->num_pfns);

	init_completion(&vb->acked);

	/* We should always be able to add one buffer to an empty queue. */
	if (virtqueue_add_buf(vq, &sg, 1, 0, vb, GFP_KERNEL) < 0)
		BUG();
	virtqueue_kick(vq);

	/* When host has read buffer, this completes via balloon_ack */
	wait_for_completion(&vb->acked);
}

static void fill_balloon(struct virtio_balloon *vb, size_t num)
{
	/* We can only do one array worth at a time. */
	num = min(num, ARRAY_SIZE(vb->pfns));

	for (vb->num_pfns = 0; vb->num_pfns < num; vb->num_pfns++) {
		struct page *page = alloc_page(GFP_HIGHUSER | __GFP_NORETRY |
					__GFP_NOMEMALLOC | __GFP_NOWARN);
		if (!page) {
			if (printk_ratelimit())
				dev_printk(KERN_INFO, &vb->vdev->dev,
					   "Out of puff! Can't get %zu pages\n",
					   num);
			/* Sleep for at least 1/5 of a second before retry. */
			msleep(200);
			break;
		}
		vb->pfns[vb->num_pfns] = page_to_balloon_pfn(page);
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
		vb->pfns[vb->num_pfns] = page_to_balloon_pfn(page);
		vb->num_pages--;
	}

	/*
	 * Note that if
	 * virtio_has_feature(vdev, VIRTIO_BALLOON_F_MUST_TELL_HOST);
	 * is true, we *have* to do it in this order
	 */
	tell_host(vb, vb->deflate_vq);
	release_pages_by_pfn(vb->pfns, vb->num_pfns);
}

static inline void update_stat(struct virtio_balloon *vb, int idx,
			       u16 tag, u64 val)
{
	BUG_ON(idx >= VIRTIO_BALLOON_S_NR);
	vb->stats[idx].tag = tag;
	vb->stats[idx].val = val;
}

#define pages_to_bytes(x) ((u64)(x) << PAGE_SHIFT)

static void update_balloon_stats(struct virtio_balloon *vb)
{
	unsigned long events[NR_VM_EVENT_ITEMS];
	struct sysinfo i;
	int idx = 0;

	all_vm_events(events);
	si_meminfo(&i);

	update_stat(vb, idx++, VIRTIO_BALLOON_S_SWAP_IN,
				pages_to_bytes(events[PSWPIN]));
	update_stat(vb, idx++, VIRTIO_BALLOON_S_SWAP_OUT,
				pages_to_bytes(events[PSWPOUT]));
	update_stat(vb, idx++, VIRTIO_BALLOON_S_MAJFLT, events[PGMAJFAULT]);
	update_stat(vb, idx++, VIRTIO_BALLOON_S_MINFLT, events[PGFAULT]);
	update_stat(vb, idx++, VIRTIO_BALLOON_S_MEMFREE,
				pages_to_bytes(i.freeram));
	update_stat(vb, idx++, VIRTIO_BALLOON_S_MEMTOT,
				pages_to_bytes(i.totalram));
}

/*
 * While most virtqueues communicate guest-initiated requests to the hypervisor,
 * the stats queue operates in reverse.  The driver initializes the virtqueue
 * with a single buffer.  From that point forward, all conversations consist of
 * a hypervisor request (a call to this function) which directs us to refill
 * the virtqueue with a fresh stats buffer.  Since stats collection can sleep,
 * we notify our kthread which does the actual work via stats_handle_request().
 */
static void stats_request(struct virtqueue *vq)
{
	struct virtio_balloon *vb;
	unsigned int len;

	vb = virtqueue_get_buf(vq, &len);
	if (!vb)
		return;
	vb->need_stats_update = 1;
	wake_up(&vb->config_change);
}

static void stats_handle_request(struct virtio_balloon *vb)
{
	struct virtqueue *vq;
	struct scatterlist sg;

	vb->need_stats_update = 0;
	update_balloon_stats(vb);

	vq = vb->stats_vq;
	sg_init_one(&sg, vb->stats, sizeof(vb->stats));
	if (virtqueue_add_buf(vq, &sg, 1, 0, vb, GFP_KERNEL) < 0)
		BUG();
	virtqueue_kick(vq);
}

static void virtballoon_changed(struct virtio_device *vdev)
{
	struct virtio_balloon *vb = vdev->priv;

	wake_up(&vb->config_change);
}

static inline s64 towards_target(struct virtio_balloon *vb)
{
	u32 v;
	vb->vdev->config->get(vb->vdev,
			      offsetof(struct virtio_balloon_config, num_pages),
			      &v, sizeof(v));
	return (s64)v - vb->num_pages;
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
		s64 diff;

		try_to_freeze();
		wait_event_interruptible(vb->config_change,
					 (diff = towards_target(vb)) != 0
					 || vb->need_stats_update
					 || kthread_should_stop()
					 || freezing(current));
		if (vb->need_stats_update)
			stats_handle_request(vb);
		if (diff > 0)
			fill_balloon(vb, diff);
		else if (diff < 0)
			leak_balloon(vb, -diff);
		update_balloon_size(vb);
	}
	return 0;
}

static int init_vqs(struct virtio_balloon *vb)
{
	struct virtqueue *vqs[3];
	vq_callback_t *callbacks[] = { balloon_ack, balloon_ack, stats_request };
	const char *names[] = { "inflate", "deflate", "stats" };
	int err, nvqs;

	/*
	 * We expect two virtqueues: inflate and deflate, and
	 * optionally stat.
	 */
	nvqs = virtio_has_feature(vb->vdev, VIRTIO_BALLOON_F_STATS_VQ) ? 3 : 2;
	err = vb->vdev->config->find_vqs(vb->vdev, nvqs, vqs, callbacks, names);
	if (err)
		return err;

	vb->inflate_vq = vqs[0];
	vb->deflate_vq = vqs[1];
	if (virtio_has_feature(vb->vdev, VIRTIO_BALLOON_F_STATS_VQ)) {
		struct scatterlist sg;
		vb->stats_vq = vqs[2];

		/*
		 * Prime this virtqueue with one buffer so the hypervisor can
		 * use it to signal us later.
		 */
		sg_init_one(&sg, vb->stats, sizeof vb->stats);
		if (virtqueue_add_buf(vb->stats_vq, &sg, 1, 0, vb, GFP_KERNEL)
		    < 0)
			BUG();
		virtqueue_kick(vb->stats_vq);
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
	vb->need_stats_update = 0;

	err = init_vqs(vb);
	if (err)
		goto out_free_vb;

	vb->thread = kthread_run(balloon, vb, "vballoon");
	if (IS_ERR(vb->thread)) {
		err = PTR_ERR(vb->thread);
		goto out_del_vqs;
	}

	return 0;

out_del_vqs:
	vdev->config->del_vqs(vdev);
out_free_vb:
	kfree(vb);
out:
	return err;
}

static void __devexit virtballoon_remove(struct virtio_device *vdev)
{
	struct virtio_balloon *vb = vdev->priv;

	kthread_stop(vb->thread);

	/* There might be pages left in the balloon: free them. */
	while (vb->num_pages)
		leak_balloon(vb, vb->num_pages);

	/* Now we reset the device so we can clean up the queues. */
	vdev->config->reset(vdev);

	vdev->config->del_vqs(vdev);
	kfree(vb);
}

#ifdef CONFIG_PM
static int virtballoon_freeze(struct virtio_device *vdev)
{
	struct virtio_balloon *vb = vdev->priv;

	/*
	 * The kthread is already frozen by the PM core before this
	 * function is called.
	 */

	while (vb->num_pages)
		leak_balloon(vb, vb->num_pages);
	update_balloon_size(vb);

	/* Ensure we don't get any more requests from the host */
	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);
	return 0;
}

static int restore_common(struct virtio_device *vdev)
{
	struct virtio_balloon *vb = vdev->priv;
	int ret;

	ret = init_vqs(vdev->priv);
	if (ret)
		return ret;

	fill_balloon(vb, towards_target(vb));
	update_balloon_size(vb);
	return 0;
}

static int virtballoon_restore(struct virtio_device *vdev)
{
	return restore_common(vdev);
}
#endif

static unsigned int features[] = {
	VIRTIO_BALLOON_F_MUST_TELL_HOST,
	VIRTIO_BALLOON_F_STATS_VQ,
};

static struct virtio_driver virtio_balloon_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table =	id_table,
	.probe =	virtballoon_probe,
	.remove =	__devexit_p(virtballoon_remove),
	.config_changed = virtballoon_changed,
#ifdef CONFIG_PM
	.freeze	=	virtballoon_freeze,
	.restore =	virtballoon_restore,
#endif
};

static int __init init(void)
{
	return register_virtio_driver(&virtio_balloon_driver);
}

static void __exit fini(void)
{
	unregister_virtio_driver(&virtio_balloon_driver);
}
module_init(init);
module_exit(fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio balloon driver");
MODULE_LICENSE("GPL");
