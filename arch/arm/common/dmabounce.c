/*
 *  arch/arm/common/dmabounce.c
 *
 *  Special dma_{map/unmap/dma_sync}_* routines for systems that have
 *  limited DMA windows. These functions utilize bounce buffers to
 *  copy data to/from buffers located outside the DMA region. This
 *  only works for systems in which DMA memory is at the bottom of
 *  RAM and the remainder of memory is at the top an the DMA memory
 *  can be marked as ZONE_DMA. Anything beyond that such as discontigous
 *  DMA windows will require custom implementations that reserve memory
 *  areas at early bootup.
 *
 *  Original version by Brad Parker (brad@heeltoe.com)
 *  Re-written by Christopher Hoover <ch@murgatroid.com>
 *  Made generic by Deepak Saxena <dsaxena@plexity.net>
 *
 *  Copyright (C) 2002 Hewlett Packard Company.
 *  Copyright (C) 2004 MontaVista Software, Inc.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/list.h>

#undef DEBUG

#undef STATS
#ifdef STATS
#define DO_STATS(X) do { X ; } while (0)
#else
#define DO_STATS(X) do { } while (0)
#endif

/* ************************************************** */

struct safe_buffer {
	struct list_head node;

	/* original request */
	void		*ptr;
	size_t		size;
	int		direction;

	/* safe buffer info */
	struct dma_pool *pool;
	void		*safe;
	dma_addr_t	safe_dma_addr;
};

struct dmabounce_device_info {
	struct list_head node;

	struct device *dev;
	struct dma_pool *small_buffer_pool;
	struct dma_pool *large_buffer_pool;
	struct list_head safe_buffers;
	unsigned long small_buffer_size, large_buffer_size;
#ifdef STATS
	unsigned long sbp_allocs;
	unsigned long lbp_allocs;
	unsigned long total_allocs;
	unsigned long map_op_count;
	unsigned long bounce_count;
#endif
};

static LIST_HEAD(dmabounce_devs);

#ifdef STATS
static void print_alloc_stats(struct dmabounce_device_info *device_info)
{
	printk(KERN_INFO
		"%s: dmabounce: sbp: %lu, lbp: %lu, other: %lu, total: %lu\n",
		device_info->dev->bus_id,
		device_info->sbp_allocs, device_info->lbp_allocs,
		device_info->total_allocs - device_info->sbp_allocs -
			device_info->lbp_allocs,
		device_info->total_allocs);
}
#endif

/* find the given device in the dmabounce device list */
static inline struct dmabounce_device_info *
find_dmabounce_dev(struct device *dev)
{
	struct list_head *entry;

	list_for_each(entry, &dmabounce_devs) {
		struct dmabounce_device_info *d =
			list_entry(entry, struct dmabounce_device_info, node);

		if (d->dev == dev)
			return d;
	}
	return NULL;
}


/* allocate a 'safe' buffer and keep track of it */
static inline struct safe_buffer *
alloc_safe_buffer(struct dmabounce_device_info *device_info, void *ptr,
			size_t size, enum dma_data_direction dir)
{
	struct safe_buffer *buf;
	struct dma_pool *pool;
	struct device *dev = device_info->dev;
	void *safe;
	dma_addr_t safe_dma_addr;

	dev_dbg(dev, "%s(ptr=%p, size=%d, dir=%d)\n",
		__func__, ptr, size, dir);

	DO_STATS ( device_info->total_allocs++ );

	buf = kmalloc(sizeof(struct safe_buffer), GFP_ATOMIC);
	if (buf == NULL) {
		dev_warn(dev, "%s: kmalloc failed\n", __func__);
		return NULL;
	}

	if (size <= device_info->small_buffer_size) {
		pool = device_info->small_buffer_pool;
		safe = dma_pool_alloc(pool, GFP_ATOMIC, &safe_dma_addr);

		DO_STATS ( device_info->sbp_allocs++ );
	} else if (size <= device_info->large_buffer_size) {
		pool = device_info->large_buffer_pool;
		safe = dma_pool_alloc(pool, GFP_ATOMIC, &safe_dma_addr);

		DO_STATS ( device_info->lbp_allocs++ );
	} else {
		pool = NULL;
		safe = dma_alloc_coherent(dev, size, &safe_dma_addr, GFP_ATOMIC);
	}

	if (safe == NULL) {
		dev_warn(device_info->dev,
			"%s: could not alloc dma memory (size=%d)\n",
		       __func__, size);
		kfree(buf);
		return NULL;
	}

#ifdef STATS
	if (device_info->total_allocs % 1000 == 0)
		print_alloc_stats(device_info);
#endif

	buf->ptr = ptr;
	buf->size = size;
	buf->direction = dir;
	buf->pool = pool;
	buf->safe = safe;
	buf->safe_dma_addr = safe_dma_addr;

	list_add(&buf->node, &device_info->safe_buffers);

	return buf;
}

/* determine if a buffer is from our "safe" pool */
static inline struct safe_buffer *
find_safe_buffer(struct dmabounce_device_info *device_info, dma_addr_t safe_dma_addr)
{
	struct list_head *entry;

	list_for_each(entry, &device_info->safe_buffers) {
		struct safe_buffer *b =
			list_entry(entry, struct safe_buffer, node);

		if (b->safe_dma_addr == safe_dma_addr)
			return b;
	}

	return NULL;
}

static inline void
free_safe_buffer(struct dmabounce_device_info *device_info, struct safe_buffer *buf)
{
	dev_dbg(device_info->dev, "%s(buf=%p)\n", __func__, buf);

	list_del(&buf->node);

	if (buf->pool)
		dma_pool_free(buf->pool, buf->safe, buf->safe_dma_addr);
	else
		dma_free_coherent(device_info->dev, buf->size, buf->safe,
				    buf->safe_dma_addr);

	kfree(buf);
}

/* ************************************************** */

#ifdef STATS

static void print_map_stats(struct dmabounce_device_info *device_info)
{
	printk(KERN_INFO
		"%s: dmabounce: map_op_count=%lu, bounce_count=%lu\n",
		device_info->dev->bus_id,
		device_info->map_op_count, device_info->bounce_count);
}
#endif

static inline dma_addr_t
map_single(struct device *dev, void *ptr, size_t size,
		enum dma_data_direction dir)
{
	struct dmabounce_device_info *device_info = find_dmabounce_dev(dev);
	dma_addr_t dma_addr;
	int needs_bounce = 0;

	if (device_info)
		DO_STATS ( device_info->map_op_count++ );

	dma_addr = virt_to_dma(dev, ptr);

	if (dev->dma_mask) {
		unsigned long mask = *dev->dma_mask;
		unsigned long limit;

		limit = (mask + 1) & ~mask;
		if (limit && size > limit) {
			dev_err(dev, "DMA mapping too big (requested %#x "
				"mask %#Lx)\n", size, *dev->dma_mask);
			return ~0;
		}

		/*
		 * Figure out if we need to bounce from the DMA mask.
		 */
		needs_bounce = (dma_addr | (dma_addr + size - 1)) & ~mask;
	}

	if (device_info && (needs_bounce || dma_needs_bounce(dev, dma_addr, size))) {
		struct safe_buffer *buf;

		buf = alloc_safe_buffer(device_info, ptr, size, dir);
		if (buf == 0) {
			dev_err(dev, "%s: unable to map unsafe buffer %p!\n",
			       __func__, ptr);
			return 0;
		}

		dev_dbg(dev,
			"%s: unsafe buffer %p (phy=%p) mapped to %p (phy=%p)\n",
			__func__, buf->ptr, (void *) virt_to_dma(dev, buf->ptr),
			buf->safe, (void *) buf->safe_dma_addr);

		if ((dir == DMA_TO_DEVICE) ||
		    (dir == DMA_BIDIRECTIONAL)) {
			dev_dbg(dev, "%s: copy unsafe %p to safe %p, size %d\n",
				__func__, ptr, buf->safe, size);
			memcpy(buf->safe, ptr, size);
		}
		consistent_sync(buf->safe, size, dir);

		dma_addr = buf->safe_dma_addr;
	} else {
		consistent_sync(ptr, size, dir);
	}

	return dma_addr;
}

static inline void
unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
		enum dma_data_direction dir)
{
	struct dmabounce_device_info *device_info = find_dmabounce_dev(dev);
	struct safe_buffer *buf = NULL;

	/*
	 * Trying to unmap an invalid mapping
	 */
	if (dma_addr == ~0) {
		dev_err(dev, "Trying to unmap invalid mapping\n");
		return;
	}

	if (device_info)
		buf = find_safe_buffer(device_info, dma_addr);

	if (buf) {
		BUG_ON(buf->size != size);

		dev_dbg(dev,
			"%s: unsafe buffer %p (phy=%p) mapped to %p (phy=%p)\n",
			__func__, buf->ptr, (void *) virt_to_dma(dev, buf->ptr),
			buf->safe, (void *) buf->safe_dma_addr);


		DO_STATS ( device_info->bounce_count++ );

		if ((dir == DMA_FROM_DEVICE) ||
		    (dir == DMA_BIDIRECTIONAL)) {
			dev_dbg(dev,
				"%s: copy back safe %p to unsafe %p size %d\n",
				__func__, buf->safe, buf->ptr, size);
			memcpy(buf->ptr, buf->safe, size);
		}
		free_safe_buffer(device_info, buf);
	}
}

static inline void
sync_single(struct device *dev, dma_addr_t dma_addr, size_t size,
		enum dma_data_direction dir)
{
	struct dmabounce_device_info *device_info = find_dmabounce_dev(dev);
	struct safe_buffer *buf = NULL;

	if (device_info)
		buf = find_safe_buffer(device_info, dma_addr);

	if (buf) {
		/*
		 * Both of these checks from original code need to be
		 * commented out b/c some drivers rely on the following:
		 *
		 * 1) Drivers may map a large chunk of memory into DMA space
		 *    but only sync a small portion of it. Good example is
		 *    allocating a large buffer, mapping it, and then
		 *    breaking it up into small descriptors. No point
		 *    in syncing the whole buffer if you only have to
		 *    touch one descriptor.
		 *
		 * 2) Buffers that are mapped as DMA_BIDIRECTIONAL are
		 *    usually only synced in one dir at a time.
		 *
		 * See drivers/net/eepro100.c for examples of both cases.
		 *
		 * -ds
		 *
		 * BUG_ON(buf->size != size);
		 * BUG_ON(buf->direction != dir);
		 */

		dev_dbg(dev,
			"%s: unsafe buffer %p (phy=%p) mapped to %p (phy=%p)\n",
			__func__, buf->ptr, (void *) virt_to_dma(dev, buf->ptr),
			buf->safe, (void *) buf->safe_dma_addr);

		DO_STATS ( device_info->bounce_count++ );

		switch (dir) {
		case DMA_FROM_DEVICE:
			dev_dbg(dev,
				"%s: copy back safe %p to unsafe %p size %d\n",
				__func__, buf->safe, buf->ptr, size);
			memcpy(buf->ptr, buf->safe, size);
			break;
		case DMA_TO_DEVICE:
			dev_dbg(dev,
				"%s: copy out unsafe %p to safe %p, size %d\n",
				__func__,buf->ptr, buf->safe, size);
			memcpy(buf->safe, buf->ptr, size);
			break;
		case DMA_BIDIRECTIONAL:
			BUG();	/* is this allowed?  what does it mean? */
		default:
			BUG();
		}
		consistent_sync(buf->safe, size, dir);
	} else {
		consistent_sync(dma_to_virt(dev, dma_addr), size, dir);
	}
}

/* ************************************************** */

/*
 * see if a buffer address is in an 'unsafe' range.  if it is
 * allocate a 'safe' buffer and copy the unsafe buffer into it.
 * substitute the safe buffer for the unsafe one.
 * (basically move the buffer from an unsafe area to a safe one)
 */
dma_addr_t
dma_map_single(struct device *dev, void *ptr, size_t size,
		enum dma_data_direction dir)
{
	unsigned long flags;
	dma_addr_t dma_addr;

	dev_dbg(dev, "%s(ptr=%p,size=%d,dir=%x)\n",
		__func__, ptr, size, dir);

	BUG_ON(dir == DMA_NONE);

	local_irq_save(flags);

	dma_addr = map_single(dev, ptr, size, dir);

	local_irq_restore(flags);

	return dma_addr;
}

/*
 * see if a mapped address was really a "safe" buffer and if so, copy
 * the data from the safe buffer back to the unsafe buffer and free up
 * the safe buffer.  (basically return things back to the way they
 * should be)
 */

void
dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
			enum dma_data_direction dir)
{
	unsigned long flags;

	dev_dbg(dev, "%s(ptr=%p,size=%d,dir=%x)\n",
		__func__, (void *) dma_addr, size, dir);

	BUG_ON(dir == DMA_NONE);

	local_irq_save(flags);

	unmap_single(dev, dma_addr, size, dir);

	local_irq_restore(flags);
}

int
dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction dir)
{
	unsigned long flags;
	int i;

	dev_dbg(dev, "%s(sg=%p,nents=%d,dir=%x)\n",
		__func__, sg, nents, dir);

	BUG_ON(dir == DMA_NONE);

	local_irq_save(flags);

	for (i = 0; i < nents; i++, sg++) {
		struct page *page = sg->page;
		unsigned int offset = sg->offset;
		unsigned int length = sg->length;
		void *ptr = page_address(page) + offset;

		sg->dma_address =
			map_single(dev, ptr, length, dir);
	}

	local_irq_restore(flags);

	return nents;
}

void
dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction dir)
{
	unsigned long flags;
	int i;

	dev_dbg(dev, "%s(sg=%p,nents=%d,dir=%x)\n",
		__func__, sg, nents, dir);

	BUG_ON(dir == DMA_NONE);

	local_irq_save(flags);

	for (i = 0; i < nents; i++, sg++) {
		dma_addr_t dma_addr = sg->dma_address;
		unsigned int length = sg->length;

		unmap_single(dev, dma_addr, length, dir);
	}

	local_irq_restore(flags);
}

void
dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_addr, size_t size,
				enum dma_data_direction dir)
{
	unsigned long flags;

	dev_dbg(dev, "%s(ptr=%p,size=%d,dir=%x)\n",
		__func__, (void *) dma_addr, size, dir);

	local_irq_save(flags);

	sync_single(dev, dma_addr, size, dir);

	local_irq_restore(flags);
}

void
dma_sync_single_for_device(struct device *dev, dma_addr_t dma_addr, size_t size,
				enum dma_data_direction dir)
{
	unsigned long flags;

	dev_dbg(dev, "%s(ptr=%p,size=%d,dir=%x)\n",
		__func__, (void *) dma_addr, size, dir);

	local_irq_save(flags);

	sync_single(dev, dma_addr, size, dir);

	local_irq_restore(flags);
}

void
dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg, int nents,
			enum dma_data_direction dir)
{
	unsigned long flags;
	int i;

	dev_dbg(dev, "%s(sg=%p,nents=%d,dir=%x)\n",
		__func__, sg, nents, dir);

	BUG_ON(dir == DMA_NONE);

	local_irq_save(flags);

	for (i = 0; i < nents; i++, sg++) {
		dma_addr_t dma_addr = sg->dma_address;
		unsigned int length = sg->length;

		sync_single(dev, dma_addr, length, dir);
	}

	local_irq_restore(flags);
}

void
dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg, int nents,
			enum dma_data_direction dir)
{
	unsigned long flags;
	int i;

	dev_dbg(dev, "%s(sg=%p,nents=%d,dir=%x)\n",
		__func__, sg, nents, dir);

	BUG_ON(dir == DMA_NONE);

	local_irq_save(flags);

	for (i = 0; i < nents; i++, sg++) {
		dma_addr_t dma_addr = sg->dma_address;
		unsigned int length = sg->length;

		sync_single(dev, dma_addr, length, dir);
	}

	local_irq_restore(flags);
}

int
dmabounce_register_dev(struct device *dev, unsigned long small_buffer_size,
			unsigned long large_buffer_size)
{
	struct dmabounce_device_info *device_info;

	device_info = kmalloc(sizeof(struct dmabounce_device_info), GFP_ATOMIC);
	if (!device_info) {
		printk(KERN_ERR
			"Could not allocated dmabounce_device_info for %s",
			dev->bus_id);
		return -ENOMEM;
	}

	device_info->small_buffer_pool =
		dma_pool_create("small_dmabounce_pool",
				dev,
				small_buffer_size,
				0 /* byte alignment */,
				0 /* no page-crossing issues */);
	if (!device_info->small_buffer_pool) {
		printk(KERN_ERR
			"dmabounce: could not allocate small DMA pool for %s\n",
			dev->bus_id);
		kfree(device_info);
		return -ENOMEM;
	}

	if (large_buffer_size) {
		device_info->large_buffer_pool =
			dma_pool_create("large_dmabounce_pool",
					dev,
					large_buffer_size,
					0 /* byte alignment */,
					0 /* no page-crossing issues */);
		if (!device_info->large_buffer_pool) {
		printk(KERN_ERR
			"dmabounce: could not allocate large DMA pool for %s\n",
			dev->bus_id);
			dma_pool_destroy(device_info->small_buffer_pool);

			return -ENOMEM;
		}
	}

	device_info->dev = dev;
	device_info->small_buffer_size = small_buffer_size;
	device_info->large_buffer_size = large_buffer_size;
	INIT_LIST_HEAD(&device_info->safe_buffers);

#ifdef STATS
	device_info->sbp_allocs = 0;
	device_info->lbp_allocs = 0;
	device_info->total_allocs = 0;
	device_info->map_op_count = 0;
	device_info->bounce_count = 0;
#endif

	list_add(&device_info->node, &dmabounce_devs);

	printk(KERN_INFO "dmabounce: registered device %s on %s bus\n",
		dev->bus_id, dev->bus->name);

	return 0;
}

void
dmabounce_unregister_dev(struct device *dev)
{
	struct dmabounce_device_info *device_info = find_dmabounce_dev(dev);

	if (!device_info) {
		printk(KERN_WARNING
			"%s: Never registered with dmabounce but attempting" \
			"to unregister!\n", dev->bus_id);
		return;
	}

	if (!list_empty(&device_info->safe_buffers)) {
		printk(KERN_ERR
			"%s: Removing from dmabounce with pending buffers!\n",
			dev->bus_id);
		BUG();
	}

	if (device_info->small_buffer_pool)
		dma_pool_destroy(device_info->small_buffer_pool);
	if (device_info->large_buffer_pool)
		dma_pool_destroy(device_info->large_buffer_pool);

#ifdef STATS
	print_alloc_stats(device_info);
	print_map_stats(device_info);
#endif

	list_del(&device_info->node);

	kfree(device_info);

	printk(KERN_INFO "dmabounce: device %s on %s bus unregistered\n",
		dev->bus_id, dev->bus->name);
}


EXPORT_SYMBOL(dma_map_single);
EXPORT_SYMBOL(dma_unmap_single);
EXPORT_SYMBOL(dma_map_sg);
EXPORT_SYMBOL(dma_unmap_sg);
EXPORT_SYMBOL(dma_sync_single);
EXPORT_SYMBOL(dma_sync_sg);
EXPORT_SYMBOL(dmabounce_register_dev);
EXPORT_SYMBOL(dmabounce_unregister_dev);

MODULE_AUTHOR("Christopher Hoover <ch@hpl.hp.com>, Deepak Saxena <dsaxena@plexity.net>");
MODULE_DESCRIPTION("Special dma_{map/unmap/dma_sync}_* routines for systems with limited DMA windows");
MODULE_LICENSE("GPL");
