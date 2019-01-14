/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include "hnae.h"

#define cls_to_ae_dev(dev) container_of(dev, struct hnae_ae_dev, cls_dev)

static struct class *hnae_class;

static void
hnae_list_add(spinlock_t *lock, struct list_head *node, struct list_head *head)
{
	unsigned long flags;

	spin_lock_irqsave(lock, flags);
	list_add_tail_rcu(node, head);
	spin_unlock_irqrestore(lock, flags);
}

static void hnae_list_del(spinlock_t *lock, struct list_head *node)
{
	unsigned long flags;

	spin_lock_irqsave(lock, flags);
	list_del_rcu(node);
	spin_unlock_irqrestore(lock, flags);
}

static int hnae_alloc_buffer(struct hnae_ring *ring, struct hnae_desc_cb *cb)
{
	unsigned int order = hnae_page_order(ring);
	struct page *p = dev_alloc_pages(order);

	if (!p)
		return -ENOMEM;

	cb->priv = p;
	cb->page_offset = 0;
	cb->reuse_flag = 0;
	cb->buf  = page_address(p);
	cb->length = hnae_page_size(ring);
	cb->type = DESC_TYPE_PAGE;

	return 0;
}

static void hnae_free_buffer(struct hnae_ring *ring, struct hnae_desc_cb *cb)
{
	if (unlikely(!cb->priv))
		return;

	if (cb->type == DESC_TYPE_SKB)
		dev_kfree_skb_any((struct sk_buff *)cb->priv);
	else if (unlikely(is_rx_ring(ring)))
		put_page((struct page *)cb->priv);

	cb->priv = NULL;
}

static int hnae_map_buffer(struct hnae_ring *ring, struct hnae_desc_cb *cb)
{
	cb->dma = dma_map_page(ring_to_dev(ring), cb->priv, 0,
			       cb->length, ring_to_dma_dir(ring));

	if (dma_mapping_error(ring_to_dev(ring), cb->dma))
		return -EIO;

	return 0;
}

static void hnae_unmap_buffer(struct hnae_ring *ring, struct hnae_desc_cb *cb)
{
	if (cb->type == DESC_TYPE_SKB)
		dma_unmap_single(ring_to_dev(ring), cb->dma, cb->length,
				 ring_to_dma_dir(ring));
	else if (cb->length)
		dma_unmap_page(ring_to_dev(ring), cb->dma, cb->length,
			       ring_to_dma_dir(ring));
}

static struct hnae_buf_ops hnae_bops = {
	.alloc_buffer = hnae_alloc_buffer,
	.free_buffer = hnae_free_buffer,
	.map_buffer = hnae_map_buffer,
	.unmap_buffer = hnae_unmap_buffer,
};

static int __ae_match(struct device *dev, const void *data)
{
	struct hnae_ae_dev *hdev = cls_to_ae_dev(dev);

	if (dev_of_node(hdev->dev))
		return (data == &hdev->dev->of_node->fwnode);
	else if (is_acpi_node(hdev->dev->fwnode))
		return (data == hdev->dev->fwnode);

	dev_err(dev, "__ae_match cannot read cfg data from OF or acpi\n");
	return 0;
}

static struct hnae_ae_dev *find_ae(const struct fwnode_handle *fwnode)
{
	struct device *dev;

	WARN_ON(!fwnode);

	dev = class_find_device(hnae_class, NULL, fwnode, __ae_match);

	return dev ? cls_to_ae_dev(dev) : NULL;
}

static void hnae_free_buffers(struct hnae_ring *ring)
{
	int i;

	for (i = 0; i < ring->desc_num; i++)
		hnae_free_buffer_detach(ring, i);
}

/* Allocate memory for raw pkg, and map with dma */
static int hnae_alloc_buffers(struct hnae_ring *ring)
{
	int i, j, ret;

	for (i = 0; i < ring->desc_num; i++) {
		ret = hnae_alloc_buffer_attach(ring, i);
		if (ret)
			goto out_buffer_fail;
	}

	return 0;

out_buffer_fail:
	for (j = i - 1; j >= 0; j--)
		hnae_free_buffer_detach(ring, j);
	return ret;
}

/* free desc along with its attached buffer */
static void hnae_free_desc(struct hnae_ring *ring)
{
	hnae_free_buffers(ring);
	dma_unmap_single(ring_to_dev(ring), ring->desc_dma_addr,
			 ring->desc_num * sizeof(ring->desc[0]),
			 ring_to_dma_dir(ring));
	ring->desc_dma_addr = 0;
	kfree(ring->desc);
	ring->desc = NULL;
}

/* alloc desc, without buffer attached */
static int hnae_alloc_desc(struct hnae_ring *ring)
{
	int size = ring->desc_num * sizeof(ring->desc[0]);

	ring->desc = kzalloc(size, GFP_KERNEL);
	if (!ring->desc)
		return -ENOMEM;

	ring->desc_dma_addr = dma_map_single(ring_to_dev(ring),
		ring->desc, size, ring_to_dma_dir(ring));
	if (dma_mapping_error(ring_to_dev(ring), ring->desc_dma_addr)) {
		ring->desc_dma_addr = 0;
		kfree(ring->desc);
		ring->desc = NULL;
		return -ENOMEM;
	}

	return 0;
}

/* fini ring, also free the buffer for the ring */
static void hnae_fini_ring(struct hnae_ring *ring)
{
	hnae_free_desc(ring);
	kfree(ring->desc_cb);
	ring->desc_cb = NULL;
	ring->next_to_clean = 0;
	ring->next_to_use = 0;
}

/* init ring, and with buffer for rx ring */
static int
hnae_init_ring(struct hnae_queue *q, struct hnae_ring *ring, int flags)
{
	int ret;

	if (ring->desc_num <= 0 || ring->buf_size <= 0)
		return -EINVAL;

	ring->q = q;
	ring->flags = flags;
	spin_lock_init(&ring->lock);
	ring->coal_param = q->handle->coal_param;
	assert(!ring->desc && !ring->desc_cb && !ring->desc_dma_addr);

	/* not matter for tx or rx ring, the ntc and ntc start from 0 */
	assert(ring->next_to_use == 0);
	assert(ring->next_to_clean == 0);

	ring->desc_cb = kcalloc(ring->desc_num, sizeof(ring->desc_cb[0]),
			GFP_KERNEL);
	if (!ring->desc_cb) {
		ret = -ENOMEM;
		goto out;
	}

	ret = hnae_alloc_desc(ring);
	if (ret)
		goto out_with_desc_cb;

	if (is_rx_ring(ring)) {
		ret = hnae_alloc_buffers(ring);
		if (ret)
			goto out_with_desc;
	}

	return 0;

out_with_desc:
	hnae_free_desc(ring);
out_with_desc_cb:
	kfree(ring->desc_cb);
	ring->desc_cb = NULL;
out:
	return ret;
}

static int hnae_init_queue(struct hnae_handle *h, struct hnae_queue *q,
			   struct hnae_ae_dev *dev)
{
	int ret;

	q->dev = dev;
	q->handle = h;

	ret = hnae_init_ring(q, &q->tx_ring, q->tx_ring.flags | RINGF_DIR);
	if (ret)
		goto out;

	ret = hnae_init_ring(q, &q->rx_ring, q->rx_ring.flags & ~RINGF_DIR);
	if (ret)
		goto out_with_tx_ring;

	if (dev->ops->init_queue)
		dev->ops->init_queue(q);

	return 0;

out_with_tx_ring:
	hnae_fini_ring(&q->tx_ring);
out:
	return ret;
}

static void hnae_fini_queue(struct hnae_queue *q)
{
	if (q->dev->ops->fini_queue)
		q->dev->ops->fini_queue(q);

	hnae_fini_ring(&q->tx_ring);
	hnae_fini_ring(&q->rx_ring);
}

/**
 * ae_chain - define ae chain head
 */
static RAW_NOTIFIER_HEAD(ae_chain);

int hnae_register_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_register(&ae_chain, nb);
}
EXPORT_SYMBOL(hnae_register_notifier);

void hnae_unregister_notifier(struct notifier_block *nb)
{
	if (raw_notifier_chain_unregister(&ae_chain, nb))
		dev_err(NULL, "notifier chain unregister fail\n");
}
EXPORT_SYMBOL(hnae_unregister_notifier);

int hnae_reinit_handle(struct hnae_handle *handle)
{
	int i, j;
	int ret;

	for (i = 0; i < handle->q_num; i++) /* free ring*/
		hnae_fini_queue(handle->qs[i]);

	if (handle->dev->ops->reset)
		handle->dev->ops->reset(handle);

	for (i = 0; i < handle->q_num; i++) {/* reinit ring*/
		ret = hnae_init_queue(handle, handle->qs[i], handle->dev);
		if (ret)
			goto out_when_init_queue;
	}
	return 0;
out_when_init_queue:
	for (j = i - 1; j >= 0; j--)
		hnae_fini_queue(handle->qs[j]);
	return ret;
}
EXPORT_SYMBOL(hnae_reinit_handle);

/* hnae_get_handle - get a handle from the AE
 * @owner_dev: the dev use this handle
 * @ae_id: the id of the ae to be used
 * @ae_opts: the options set for the handle
 * @bops: the callbacks for buffer management
 *
 * return handle ptr or ERR_PTR
 */
struct hnae_handle *hnae_get_handle(struct device *owner_dev,
				    const struct fwnode_handle	*fwnode,
				    u32 port_id,
				    struct hnae_buf_ops *bops)
{
	struct hnae_ae_dev *dev;
	struct hnae_handle *handle;
	int i, j;
	int ret;

	dev = find_ae(fwnode);
	if (!dev)
		return ERR_PTR(-ENODEV);

	handle = dev->ops->get_handle(dev, port_id);
	if (IS_ERR(handle)) {
		put_device(&dev->cls_dev);
		return handle;
	}

	handle->dev = dev;
	handle->owner_dev = owner_dev;
	handle->bops = bops ? bops : &hnae_bops;
	handle->eport_id = port_id;

	for (i = 0; i < handle->q_num; i++) {
		ret = hnae_init_queue(handle, handle->qs[i], dev);
		if (ret)
			goto out_when_init_queue;
	}

	__module_get(dev->owner);

	hnae_list_add(&dev->lock, &handle->node, &dev->handle_list);

	return handle;

out_when_init_queue:
	for (j = i - 1; j >= 0; j--)
		hnae_fini_queue(handle->qs[j]);

	put_device(&dev->cls_dev);

	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL(hnae_get_handle);

void hnae_put_handle(struct hnae_handle *h)
{
	struct hnae_ae_dev *dev = h->dev;
	int i;

	for (i = 0; i < h->q_num; i++)
		hnae_fini_queue(h->qs[i]);

	if (h->dev->ops->reset)
		h->dev->ops->reset(h);

	hnae_list_del(&dev->lock, &h->node);

	if (dev->ops->put_handle)
		dev->ops->put_handle(h);

	module_put(dev->owner);

	put_device(&dev->cls_dev);
}
EXPORT_SYMBOL(hnae_put_handle);

static void hnae_release(struct device *dev)
{
}

/**
 * hnae_ae_register - register a AE engine to hnae framework
 * @hdev: the hnae ae engine device
 * @owner:  the module who provides this dev
 * NOTE: the duplicated name will not be checked
 */
int hnae_ae_register(struct hnae_ae_dev *hdev, struct module *owner)
{
	static atomic_t id = ATOMIC_INIT(-1);
	int ret;

	if (!hdev->dev)
		return -ENODEV;

	if (!hdev->ops || !hdev->ops->get_handle ||
	    !hdev->ops->toggle_ring_irq ||
	    !hdev->ops->get_status || !hdev->ops->adjust_link)
		return -EINVAL;

	hdev->owner = owner;
	hdev->id = (int)atomic_inc_return(&id);
	hdev->cls_dev.parent = hdev->dev;
	hdev->cls_dev.class = hnae_class;
	hdev->cls_dev.release = hnae_release;
	(void)dev_set_name(&hdev->cls_dev, "hnae%d", hdev->id);
	ret = device_register(&hdev->cls_dev);
	if (ret)
		return ret;

	__module_get(THIS_MODULE);

	INIT_LIST_HEAD(&hdev->handle_list);
	spin_lock_init(&hdev->lock);

	ret = raw_notifier_call_chain(&ae_chain, HNAE_AE_REGISTER, NULL);
	if (ret)
		dev_dbg(hdev->dev,
			"has not notifier for AE: %s\n", hdev->name);

	return 0;
}
EXPORT_SYMBOL(hnae_ae_register);

/**
 * hnae_ae_unregister - unregisters a HNAE AE engine
 * @cdev: the device to unregister
 */
void hnae_ae_unregister(struct hnae_ae_dev *hdev)
{
	device_unregister(&hdev->cls_dev);
	module_put(THIS_MODULE);
}
EXPORT_SYMBOL(hnae_ae_unregister);

static int __init hnae_init(void)
{
	hnae_class = class_create(THIS_MODULE, "hnae");
	return PTR_ERR_OR_ZERO(hnae_class);
}

static void __exit hnae_exit(void)
{
	class_destroy(hnae_class);
}

subsys_initcall(hnae_init);
module_exit(hnae_exit);

MODULE_AUTHOR("Hisilicon, Inc.");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hisilicon Network Acceleration Engine Framework");

/* vi: set tw=78 noet: */
