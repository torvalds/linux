/* Virtio ring implementation.
 *
 *  Copyright 2007 Rusty Russell IBM Corporation
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
#include <linux/virtio_ring.h>
#include <linux/device.h>

#ifdef DEBUG
/* For development, we want to crash whenever the ring is screwed. */
#define BAD_RING(vq, fmt...)			\
	do { dev_err(&vq->vq.vdev->dev, fmt); BUG(); } while(0)
#define START_USE(vq) \
	do { if ((vq)->in_use) panic("in_use = %i\n", (vq)->in_use); (vq)->in_use = __LINE__; mb(); } while(0)
#define END_USE(vq) \
	do { BUG_ON(!(vq)->in_use); (vq)->in_use = 0; mb(); } while(0)
#else
#define BAD_RING(vq, fmt...)			\
	do { dev_err(&vq->vq.vdev->dev, fmt); (vq)->broken = true; } while(0)
#define START_USE(vq)
#define END_USE(vq)
#endif

struct vring_virtqueue
{
	struct virtqueue vq;

	/* Actual memory layout for this queue */
	struct vring vring;

	/* Other side has made a mess, don't try any more. */
	bool broken;

	/* Number of free buffers */
	unsigned int num_free;
	/* Head of free buffer list. */
	unsigned int free_head;
	/* Number we've added since last sync. */
	unsigned int num_added;

	/* Last used index we've seen. */
	u16 last_used_idx;

	/* How to notify other side. FIXME: commonalize hcalls! */
	void (*notify)(struct virtqueue *vq);

#ifdef DEBUG
	/* They're supposed to lock for us. */
	unsigned int in_use;
#endif

	/* Tokens for callbacks. */
	void *data[];
};

#define to_vvq(_vq) container_of(_vq, struct vring_virtqueue, vq)

static int vring_add_buf(struct virtqueue *_vq,
			 struct scatterlist sg[],
			 unsigned int out,
			 unsigned int in,
			 void *data)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	unsigned int i, avail, head, uninitialized_var(prev);

	BUG_ON(data == NULL);
	BUG_ON(out + in > vq->vring.num);
	BUG_ON(out + in == 0);

	START_USE(vq);

	if (vq->num_free < out + in) {
		pr_debug("Can't add buf len %i - avail = %i\n",
			 out + in, vq->num_free);
		/* We notify *even if* VRING_USED_F_NO_NOTIFY is set here. */
		vq->notify(&vq->vq);
		END_USE(vq);
		return -ENOSPC;
	}

	/* We're about to use some buffers from the free list. */
	vq->num_free -= out + in;

	head = vq->free_head;
	for (i = vq->free_head; out; i = vq->vring.desc[i].next, out--) {
		vq->vring.desc[i].flags = VRING_DESC_F_NEXT;
		vq->vring.desc[i].addr = sg_phys(sg);
		vq->vring.desc[i].len = sg->length;
		prev = i;
		sg++;
	}
	for (; in; i = vq->vring.desc[i].next, in--) {
		vq->vring.desc[i].flags = VRING_DESC_F_NEXT|VRING_DESC_F_WRITE;
		vq->vring.desc[i].addr = sg_phys(sg);
		vq->vring.desc[i].len = sg->length;
		prev = i;
		sg++;
	}
	/* Last one doesn't continue. */
	vq->vring.desc[prev].flags &= ~VRING_DESC_F_NEXT;

	/* Update free pointer */
	vq->free_head = i;

	/* Set token. */
	vq->data[head] = data;

	/* Put entry in available array (but don't update avail->idx until they
	 * do sync).  FIXME: avoid modulus here? */
	avail = (vq->vring.avail->idx + vq->num_added++) % vq->vring.num;
	vq->vring.avail->ring[avail] = head;

	pr_debug("Added buffer head %i to %p\n", head, vq);
	END_USE(vq);
	return 0;
}

static void vring_kick(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	START_USE(vq);
	/* Descriptors and available array need to be set before we expose the
	 * new available array entries. */
	wmb();

	vq->vring.avail->idx += vq->num_added;
	vq->num_added = 0;

	/* Need to update avail index before checking if we should notify */
	mb();

	if (!(vq->vring.used->flags & VRING_USED_F_NO_NOTIFY))
		/* Prod other side to tell it about changes. */
		vq->notify(&vq->vq);

	END_USE(vq);
}

static void detach_buf(struct vring_virtqueue *vq, unsigned int head)
{
	unsigned int i;

	/* Clear data ptr. */
	vq->data[head] = NULL;

	/* Put back on free list: find end */
	i = head;
	while (vq->vring.desc[i].flags & VRING_DESC_F_NEXT) {
		i = vq->vring.desc[i].next;
		vq->num_free++;
	}

	vq->vring.desc[i].next = vq->free_head;
	vq->free_head = head;
	/* Plus final descriptor */
	vq->num_free++;
}

static inline bool more_used(const struct vring_virtqueue *vq)
{
	return vq->last_used_idx != vq->vring.used->idx;
}

static void *vring_get_buf(struct virtqueue *_vq, unsigned int *len)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	void *ret;
	unsigned int i;

	START_USE(vq);

	if (!more_used(vq)) {
		pr_debug("No more buffers in queue\n");
		END_USE(vq);
		return NULL;
	}

	i = vq->vring.used->ring[vq->last_used_idx%vq->vring.num].id;
	*len = vq->vring.used->ring[vq->last_used_idx%vq->vring.num].len;

	if (unlikely(i >= vq->vring.num)) {
		BAD_RING(vq, "id %u out of range\n", i);
		return NULL;
	}
	if (unlikely(!vq->data[i])) {
		BAD_RING(vq, "id %u is not a head!\n", i);
		return NULL;
	}

	/* detach_buf clears data, so grab it now. */
	ret = vq->data[i];
	detach_buf(vq, i);
	vq->last_used_idx++;
	END_USE(vq);
	return ret;
}

static void vring_disable_cb(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	START_USE(vq);
	BUG_ON(vq->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT);
	vq->vring.avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;
	END_USE(vq);
}

static bool vring_enable_cb(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	START_USE(vq);
	BUG_ON(!(vq->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT));

	/* We optimistically turn back on interrupts, then check if there was
	 * more to do. */
	vq->vring.avail->flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
	mb();
	if (unlikely(more_used(vq))) {
		vq->vring.avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;
		END_USE(vq);
		return false;
	}

	END_USE(vq);
	return true;
}

irqreturn_t vring_interrupt(int irq, void *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	if (!more_used(vq)) {
		pr_debug("virtqueue interrupt with no work for %p\n", vq);
		return IRQ_NONE;
	}

	if (unlikely(vq->broken))
		return IRQ_HANDLED;

	/* Other side may have missed us turning off the interrupt,
	 * but we should preserve disable semantic for virtio users. */
	if (unlikely(vq->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT)) {
		pr_debug("virtqueue interrupt after disable for %p\n", vq);
		return IRQ_HANDLED;
	}

	pr_debug("virtqueue callback for %p (%p)\n", vq, vq->vq.callback);
	if (vq->vq.callback)
		vq->vq.callback(&vq->vq);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(vring_interrupt);

static struct virtqueue_ops vring_vq_ops = {
	.add_buf = vring_add_buf,
	.get_buf = vring_get_buf,
	.kick = vring_kick,
	.disable_cb = vring_disable_cb,
	.enable_cb = vring_enable_cb,
};

struct virtqueue *vring_new_virtqueue(unsigned int num,
				      struct virtio_device *vdev,
				      void *pages,
				      void (*notify)(struct virtqueue *),
				      void (*callback)(struct virtqueue *))
{
	struct vring_virtqueue *vq;
	unsigned int i;

	/* We assume num is a power of 2. */
	if (num & (num - 1)) {
		dev_warn(&vdev->dev, "Bad virtqueue length %u\n", num);
		return NULL;
	}

	vq = kmalloc(sizeof(*vq) + sizeof(void *)*num, GFP_KERNEL);
	if (!vq)
		return NULL;

	vring_init(&vq->vring, num, pages, PAGE_SIZE);
	vq->vq.callback = callback;
	vq->vq.vdev = vdev;
	vq->vq.vq_ops = &vring_vq_ops;
	vq->notify = notify;
	vq->broken = false;
	vq->last_used_idx = 0;
	vq->num_added = 0;
#ifdef DEBUG
	vq->in_use = false;
#endif

	/* No callback?  Tell other side not to bother us. */
	if (!callback)
		vq->vring.avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;

	/* Put everything in free lists. */
	vq->num_free = num;
	vq->free_head = 0;
	for (i = 0; i < num-1; i++)
		vq->vring.desc[i].next = i+1;

	return &vq->vq;
}
EXPORT_SYMBOL_GPL(vring_new_virtqueue);

void vring_del_virtqueue(struct virtqueue *vq)
{
	kfree(to_vvq(vq));
}
EXPORT_SYMBOL_GPL(vring_del_virtqueue);

MODULE_LICENSE("GPL");
