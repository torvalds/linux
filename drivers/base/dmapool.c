
#include <linux/device.h>
#include <linux/mm.h>
#include <asm/io.h>		/* Needed for i386 to build */
#include <asm/scatterlist.h>	/* Needed for i386 to build */
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/poison.h>

/*
 * Pool allocator ... wraps the dma_alloc_coherent page allocator, so
 * small blocks are easily used by drivers for bus mastering controllers.
 * This should probably be sharing the guts of the slab allocator.
 */

struct dma_pool {	/* the pool */
	struct list_head	page_list;
	spinlock_t		lock;
	size_t			blocks_per_page;
	size_t			size;
	struct device		*dev;
	size_t			allocation;
	char			name [32];
	wait_queue_head_t	waitq;
	struct list_head	pools;
};

struct dma_page {	/* cacheable header for 'allocation' bytes */
	struct list_head	page_list;
	void			*vaddr;
	dma_addr_t		dma;
	unsigned		in_use;
	unsigned long		bitmap [0];
};

#define	POOL_TIMEOUT_JIFFIES	((100 /* msec */ * HZ) / 1000)

static DECLARE_MUTEX (pools_lock);

static ssize_t
show_pools (struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned temp;
	unsigned size;
	char *next;
	struct dma_page *page;
	struct dma_pool *pool;

	next = buf;
	size = PAGE_SIZE;

	temp = scnprintf(next, size, "poolinfo - 0.1\n");
	size -= temp;
	next += temp;

	down (&pools_lock);
	list_for_each_entry(pool, &dev->dma_pools, pools) {
		unsigned pages = 0;
		unsigned blocks = 0;

		list_for_each_entry(page, &pool->page_list, page_list) {
			pages++;
			blocks += page->in_use;
		}

		/* per-pool info, no real statistics yet */
		temp = scnprintf(next, size, "%-16s %4u %4Zu %4Zu %2u\n",
				pool->name,
				blocks, pages * pool->blocks_per_page,
				pool->size, pages);
		size -= temp;
		next += temp;
	}
	up (&pools_lock);

	return PAGE_SIZE - size;
}
static DEVICE_ATTR (pools, S_IRUGO, show_pools, NULL);

/**
 * dma_pool_create - Creates a pool of consistent memory blocks, for dma.
 * @name: name of pool, for diagnostics
 * @dev: device that will be doing the DMA
 * @size: size of the blocks in this pool.
 * @align: alignment requirement for blocks; must be a power of two
 * @allocation: returned blocks won't cross this boundary (or zero)
 * Context: !in_interrupt()
 *
 * Returns a dma allocation pool with the requested characteristics, or
 * null if one can't be created.  Given one of these pools, dma_pool_alloc()
 * may be used to allocate memory.  Such memory will all have "consistent"
 * DMA mappings, accessible by the device and its driver without using
 * cache flushing primitives.  The actual size of blocks allocated may be
 * larger than requested because of alignment.
 *
 * If allocation is nonzero, objects returned from dma_pool_alloc() won't
 * cross that size boundary.  This is useful for devices which have
 * addressing restrictions on individual DMA transfers, such as not crossing
 * boundaries of 4KBytes.
 */
struct dma_pool *
dma_pool_create (const char *name, struct device *dev,
	size_t size, size_t align, size_t allocation)
{
	struct dma_pool		*retval;

	if (align == 0)
		align = 1;
	if (size == 0)
		return NULL;
	else if (size < align)
		size = align;
	else if ((size % align) != 0) {
		size += align + 1;
		size &= ~(align - 1);
	}

	if (allocation == 0) {
		if (PAGE_SIZE < size)
			allocation = size;
		else
			allocation = PAGE_SIZE;
		// FIXME: round up for less fragmentation
	} else if (allocation < size)
		return NULL;

	if (!(retval = kmalloc (sizeof *retval, GFP_KERNEL)))
		return retval;

	strlcpy (retval->name, name, sizeof retval->name);

	retval->dev = dev;

	INIT_LIST_HEAD (&retval->page_list);
	spin_lock_init (&retval->lock);
	retval->size = size;
	retval->allocation = allocation;
	retval->blocks_per_page = allocation / size;
	init_waitqueue_head (&retval->waitq);

	if (dev) {
		int ret;

		down (&pools_lock);
		if (list_empty (&dev->dma_pools))
			ret = device_create_file (dev, &dev_attr_pools);
		else
			ret = 0;
		/* note:  not currently insisting "name" be unique */
		if (!ret)
			list_add (&retval->pools, &dev->dma_pools);
		else {
			kfree(retval);
			retval = NULL;
		}
		up (&pools_lock);
	} else
		INIT_LIST_HEAD (&retval->pools);

	return retval;
}


static struct dma_page *
pool_alloc_page (struct dma_pool *pool, gfp_t mem_flags)
{
	struct dma_page	*page;
	int		mapsize;

	mapsize = pool->blocks_per_page;
	mapsize = (mapsize + BITS_PER_LONG - 1) / BITS_PER_LONG;
	mapsize *= sizeof (long);

	page = (struct dma_page *) kmalloc (mapsize + sizeof *page, mem_flags);
	if (!page)
		return NULL;
	page->vaddr = dma_alloc_coherent (pool->dev,
					    pool->allocation,
					    &page->dma,
					    mem_flags);
	if (page->vaddr) {
		memset (page->bitmap, 0xff, mapsize);	// bit set == free
#ifdef	CONFIG_DEBUG_SLAB
		memset (page->vaddr, POOL_POISON_FREED, pool->allocation);
#endif
		list_add (&page->page_list, &pool->page_list);
		page->in_use = 0;
	} else {
		kfree (page);
		page = NULL;
	}
	return page;
}


static inline int
is_page_busy (int blocks, unsigned long *bitmap)
{
	while (blocks > 0) {
		if (*bitmap++ != ~0UL)
			return 1;
		blocks -= BITS_PER_LONG;
	}
	return 0;
}

static void
pool_free_page (struct dma_pool *pool, struct dma_page *page)
{
	dma_addr_t	dma = page->dma;

#ifdef	CONFIG_DEBUG_SLAB
	memset (page->vaddr, POOL_POISON_FREED, pool->allocation);
#endif
	dma_free_coherent (pool->dev, pool->allocation, page->vaddr, dma);
	list_del (&page->page_list);
	kfree (page);
}


/**
 * dma_pool_destroy - destroys a pool of dma memory blocks.
 * @pool: dma pool that will be destroyed
 * Context: !in_interrupt()
 *
 * Caller guarantees that no more memory from the pool is in use,
 * and that nothing will try to use the pool after this call.
 */
void
dma_pool_destroy (struct dma_pool *pool)
{
	down (&pools_lock);
	list_del (&pool->pools);
	if (pool->dev && list_empty (&pool->dev->dma_pools))
		device_remove_file (pool->dev, &dev_attr_pools);
	up (&pools_lock);

	while (!list_empty (&pool->page_list)) {
		struct dma_page		*page;
		page = list_entry (pool->page_list.next,
				struct dma_page, page_list);
		if (is_page_busy (pool->blocks_per_page, page->bitmap)) {
			if (pool->dev)
				dev_err(pool->dev, "dma_pool_destroy %s, %p busy\n",
					pool->name, page->vaddr);
			else
				printk (KERN_ERR "dma_pool_destroy %s, %p busy\n",
					pool->name, page->vaddr);
			/* leak the still-in-use consistent memory */
			list_del (&page->page_list);
			kfree (page);
		} else
			pool_free_page (pool, page);
	}

	kfree (pool);
}


/**
 * dma_pool_alloc - get a block of consistent memory
 * @pool: dma pool that will produce the block
 * @mem_flags: GFP_* bitmask
 * @handle: pointer to dma address of block
 *
 * This returns the kernel virtual address of a currently unused block,
 * and reports its dma address through the handle.
 * If such a memory block can't be allocated, null is returned.
 */
void *
dma_pool_alloc (struct dma_pool *pool, gfp_t mem_flags, dma_addr_t *handle)
{
	unsigned long		flags;
	struct dma_page		*page;
	int			map, block;
	size_t			offset;
	void			*retval;

restart:
	spin_lock_irqsave (&pool->lock, flags);
	list_for_each_entry(page, &pool->page_list, page_list) {
		int		i;
		/* only cachable accesses here ... */
		for (map = 0, i = 0;
				i < pool->blocks_per_page;
				i += BITS_PER_LONG, map++) {
			if (page->bitmap [map] == 0)
				continue;
			block = ffz (~ page->bitmap [map]);
			if ((i + block) < pool->blocks_per_page) {
				clear_bit (block, &page->bitmap [map]);
				offset = (BITS_PER_LONG * map) + block;
				offset *= pool->size;
				goto ready;
			}
		}
	}
	if (!(page = pool_alloc_page (pool, GFP_ATOMIC))) {
		if (mem_flags & __GFP_WAIT) {
			DECLARE_WAITQUEUE (wait, current);

			current->state = TASK_INTERRUPTIBLE;
			add_wait_queue (&pool->waitq, &wait);
			spin_unlock_irqrestore (&pool->lock, flags);

			schedule_timeout (POOL_TIMEOUT_JIFFIES);

			remove_wait_queue (&pool->waitq, &wait);
			goto restart;
		}
		retval = NULL;
		goto done;
	}

	clear_bit (0, &page->bitmap [0]);
	offset = 0;
ready:
	page->in_use++;
	retval = offset + page->vaddr;
	*handle = offset + page->dma;
#ifdef	CONFIG_DEBUG_SLAB
	memset (retval, POOL_POISON_ALLOCATED, pool->size);
#endif
done:
	spin_unlock_irqrestore (&pool->lock, flags);
	return retval;
}


static struct dma_page *
pool_find_page (struct dma_pool *pool, dma_addr_t dma)
{
	unsigned long		flags;
	struct dma_page		*page;

	spin_lock_irqsave (&pool->lock, flags);
	list_for_each_entry(page, &pool->page_list, page_list) {
		if (dma < page->dma)
			continue;
		if (dma < (page->dma + pool->allocation))
			goto done;
	}
	page = NULL;
done:
	spin_unlock_irqrestore (&pool->lock, flags);
	return page;
}


/**
 * dma_pool_free - put block back into dma pool
 * @pool: the dma pool holding the block
 * @vaddr: virtual address of block
 * @dma: dma address of block
 *
 * Caller promises neither device nor driver will again touch this block
 * unless it is first re-allocated.
 */
void
dma_pool_free (struct dma_pool *pool, void *vaddr, dma_addr_t dma)
{
	struct dma_page		*page;
	unsigned long		flags;
	int			map, block;

	if ((page = pool_find_page (pool, dma)) == 0) {
		if (pool->dev)
			dev_err(pool->dev, "dma_pool_free %s, %p/%lx (bad dma)\n",
				pool->name, vaddr, (unsigned long) dma);
		else
			printk (KERN_ERR "dma_pool_free %s, %p/%lx (bad dma)\n",
				pool->name, vaddr, (unsigned long) dma);
		return;
	}

	block = dma - page->dma;
	block /= pool->size;
	map = block / BITS_PER_LONG;
	block %= BITS_PER_LONG;

#ifdef	CONFIG_DEBUG_SLAB
	if (((dma - page->dma) + (void *)page->vaddr) != vaddr) {
		if (pool->dev)
			dev_err(pool->dev, "dma_pool_free %s, %p (bad vaddr)/%Lx\n",
				pool->name, vaddr, (unsigned long long) dma);
		else
			printk (KERN_ERR "dma_pool_free %s, %p (bad vaddr)/%Lx\n",
				pool->name, vaddr, (unsigned long long) dma);
		return;
	}
	if (page->bitmap [map] & (1UL << block)) {
		if (pool->dev)
			dev_err(pool->dev, "dma_pool_free %s, dma %Lx already free\n",
				pool->name, (unsigned long long)dma);
		else
			printk (KERN_ERR "dma_pool_free %s, dma %Lx already free\n",
				pool->name, (unsigned long long)dma);
		return;
	}
	memset (vaddr, POOL_POISON_FREED, pool->size);
#endif

	spin_lock_irqsave (&pool->lock, flags);
	page->in_use--;
	set_bit (block, &page->bitmap [map]);
	if (waitqueue_active (&pool->waitq))
		wake_up (&pool->waitq);
	/*
	 * Resist a temptation to do
	 *    if (!is_page_busy(bpp, page->bitmap)) pool_free_page(pool, page);
	 * Better have a few empty pages hang around.
	 */
	spin_unlock_irqrestore (&pool->lock, flags);
}


EXPORT_SYMBOL (dma_pool_create);
EXPORT_SYMBOL (dma_pool_destroy);
EXPORT_SYMBOL (dma_pool_alloc);
EXPORT_SYMBOL (dma_pool_free);
