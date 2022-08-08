// SPDX-License-Identifier: GPL-2.0+
/*
 * comedi_buf.c
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2002 Frank Mori Hess <fmhess@users.sourceforge.net>
 */

#include <linux/vmalloc.h>
#include <linux/slab.h>

#include "comedidev.h"
#include "comedi_internal.h"

#ifdef PAGE_KERNEL_NOCACHE
#define COMEDI_PAGE_PROTECTION		PAGE_KERNEL_NOCACHE
#else
#define COMEDI_PAGE_PROTECTION		PAGE_KERNEL
#endif

static void comedi_buf_map_kref_release(struct kref *kref)
{
	struct comedi_buf_map *bm =
		container_of(kref, struct comedi_buf_map, refcount);
	struct comedi_buf_page *buf;
	unsigned int i;

	if (bm->page_list) {
		if (bm->dma_dir != DMA_NONE) {
			/*
			 * DMA buffer was allocated as a single block.
			 * Address is in page_list[0].
			 */
			buf = &bm->page_list[0];
			dma_free_coherent(bm->dma_hw_dev,
					  PAGE_SIZE * bm->n_pages,
					  buf->virt_addr, buf->dma_addr);
		} else {
			for (i = 0; i < bm->n_pages; i++) {
				buf = &bm->page_list[i];
				ClearPageReserved(virt_to_page(buf->virt_addr));
				free_page((unsigned long)buf->virt_addr);
			}
		}
		vfree(bm->page_list);
	}
	if (bm->dma_dir != DMA_NONE)
		put_device(bm->dma_hw_dev);
	kfree(bm);
}

static void __comedi_buf_free(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct comedi_async *async = s->async;
	struct comedi_buf_map *bm;
	unsigned long flags;

	if (async->prealloc_buf) {
		if (s->async_dma_dir == DMA_NONE)
			vunmap(async->prealloc_buf);
		async->prealloc_buf = NULL;
		async->prealloc_bufsz = 0;
	}

	spin_lock_irqsave(&s->spin_lock, flags);
	bm = async->buf_map;
	async->buf_map = NULL;
	spin_unlock_irqrestore(&s->spin_lock, flags);
	comedi_buf_map_put(bm);
}

static struct comedi_buf_map *
comedi_buf_map_alloc(struct comedi_device *dev, enum dma_data_direction dma_dir,
		     unsigned int n_pages)
{
	struct comedi_buf_map *bm;
	struct comedi_buf_page *buf;
	unsigned int i;

	bm = kzalloc(sizeof(*bm), GFP_KERNEL);
	if (!bm)
		return NULL;

	kref_init(&bm->refcount);
	bm->dma_dir = dma_dir;
	if (bm->dma_dir != DMA_NONE) {
		/* Need ref to hardware device to free buffer later. */
		bm->dma_hw_dev = get_device(dev->hw_dev);
	}

	bm->page_list = vzalloc(sizeof(*buf) * n_pages);
	if (!bm->page_list)
		goto err;

	if (bm->dma_dir != DMA_NONE) {
		void *virt_addr;
		dma_addr_t dma_addr;

		/*
		 * Currently, the DMA buffer needs to be allocated as a
		 * single block so that it can be mmap()'ed.
		 */
		virt_addr = dma_alloc_coherent(bm->dma_hw_dev,
					       PAGE_SIZE * n_pages, &dma_addr,
					       GFP_KERNEL);
		if (!virt_addr)
			goto err;

		for (i = 0; i < n_pages; i++) {
			buf = &bm->page_list[i];
			buf->virt_addr = virt_addr + (i << PAGE_SHIFT);
			buf->dma_addr = dma_addr + (i << PAGE_SHIFT);
		}

		bm->n_pages = i;
	} else {
		for (i = 0; i < n_pages; i++) {
			buf = &bm->page_list[i];
			buf->virt_addr = (void *)get_zeroed_page(GFP_KERNEL);
			if (!buf->virt_addr)
				break;

			SetPageReserved(virt_to_page(buf->virt_addr));
		}

		bm->n_pages = i;
		if (i < n_pages)
			goto err;
	}

	return bm;

err:
	comedi_buf_map_put(bm);
	return NULL;
}

static void __comedi_buf_alloc(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       unsigned int n_pages)
{
	struct comedi_async *async = s->async;
	struct page **pages = NULL;
	struct comedi_buf_map *bm;
	struct comedi_buf_page *buf;
	unsigned long flags;
	unsigned int i;

	if (!IS_ENABLED(CONFIG_HAS_DMA) && s->async_dma_dir != DMA_NONE) {
		dev_err(dev->class_dev,
			"dma buffer allocation not supported\n");
		return;
	}

	bm = comedi_buf_map_alloc(dev, s->async_dma_dir, n_pages);
	if (!bm)
		return;

	spin_lock_irqsave(&s->spin_lock, flags);
	async->buf_map = bm;
	spin_unlock_irqrestore(&s->spin_lock, flags);

	if (bm->dma_dir != DMA_NONE) {
		/*
		 * DMA buffer was allocated as a single block.
		 * Address is in page_list[0].
		 */
		buf = &bm->page_list[0];
		async->prealloc_buf = buf->virt_addr;
	} else {
		pages = vmalloc(sizeof(struct page *) * n_pages);
		if (!pages)
			return;

		for (i = 0; i < n_pages; i++) {
			buf = &bm->page_list[i];
			pages[i] = virt_to_page(buf->virt_addr);
		}

		/* vmap the pages to prealloc_buf */
		async->prealloc_buf = vmap(pages, n_pages, VM_MAP,
					   COMEDI_PAGE_PROTECTION);

		vfree(pages);
	}
}

void comedi_buf_map_get(struct comedi_buf_map *bm)
{
	if (bm)
		kref_get(&bm->refcount);
}

int comedi_buf_map_put(struct comedi_buf_map *bm)
{
	if (bm)
		return kref_put(&bm->refcount, comedi_buf_map_kref_release);
	return 1;
}

/* helper for "access" vm operation */
int comedi_buf_map_access(struct comedi_buf_map *bm, unsigned long offset,
			  void *buf, int len, int write)
{
	unsigned int pgoff = offset_in_page(offset);
	unsigned long pg = offset >> PAGE_SHIFT;
	int done = 0;

	while (done < len && pg < bm->n_pages) {
		int l = min_t(int, len - done, PAGE_SIZE - pgoff);
		void *b = bm->page_list[pg].virt_addr + pgoff;

		if (write)
			memcpy(b, buf, l);
		else
			memcpy(buf, b, l);
		buf += l;
		done += l;
		pg++;
		pgoff = 0;
	}
	return done;
}

/* returns s->async->buf_map and increments its kref refcount */
struct comedi_buf_map *
comedi_buf_map_from_subdev_get(struct comedi_subdevice *s)
{
	struct comedi_async *async = s->async;
	struct comedi_buf_map *bm = NULL;
	unsigned long flags;

	if (!async)
		return NULL;

	spin_lock_irqsave(&s->spin_lock, flags);
	bm = async->buf_map;
	/* only want it if buffer pages allocated */
	if (bm && bm->n_pages)
		comedi_buf_map_get(bm);
	else
		bm = NULL;
	spin_unlock_irqrestore(&s->spin_lock, flags);

	return bm;
}

bool comedi_buf_is_mmapped(struct comedi_subdevice *s)
{
	struct comedi_buf_map *bm = s->async->buf_map;

	return bm && (kref_read(&bm->refcount) > 1);
}

int comedi_buf_alloc(struct comedi_device *dev, struct comedi_subdevice *s,
		     unsigned long new_size)
{
	struct comedi_async *async = s->async;

	lockdep_assert_held(&dev->mutex);

	/* Round up new_size to multiple of PAGE_SIZE */
	new_size = (new_size + PAGE_SIZE - 1) & PAGE_MASK;

	/* if no change is required, do nothing */
	if (async->prealloc_buf && async->prealloc_bufsz == new_size)
		return 0;

	/* deallocate old buffer */
	__comedi_buf_free(dev, s);

	/* allocate new buffer */
	if (new_size) {
		unsigned int n_pages = new_size >> PAGE_SHIFT;

		__comedi_buf_alloc(dev, s, n_pages);

		if (!async->prealloc_buf) {
			/* allocation failed */
			__comedi_buf_free(dev, s);
			return -ENOMEM;
		}
	}
	async->prealloc_bufsz = new_size;

	return 0;
}

void comedi_buf_reset(struct comedi_subdevice *s)
{
	struct comedi_async *async = s->async;

	async->buf_write_alloc_count = 0;
	async->buf_write_count = 0;
	async->buf_read_alloc_count = 0;
	async->buf_read_count = 0;

	async->buf_write_ptr = 0;
	async->buf_read_ptr = 0;

	async->cur_chan = 0;
	async->scans_done = 0;
	async->scan_progress = 0;
	async->munge_chan = 0;
	async->munge_count = 0;
	async->munge_ptr = 0;

	async->events = 0;
}

static unsigned int comedi_buf_write_n_unalloc(struct comedi_subdevice *s)
{
	struct comedi_async *async = s->async;
	unsigned int free_end = async->buf_read_count + async->prealloc_bufsz;

	return free_end - async->buf_write_alloc_count;
}

unsigned int comedi_buf_write_n_available(struct comedi_subdevice *s)
{
	struct comedi_async *async = s->async;
	unsigned int free_end = async->buf_read_count + async->prealloc_bufsz;

	return free_end - async->buf_write_count;
}

/**
 * comedi_buf_write_alloc() - Reserve buffer space for writing
 * @s: COMEDI subdevice.
 * @nbytes: Maximum space to reserve in bytes.
 *
 * Reserve up to @nbytes bytes of space to be written in the COMEDI acquisition
 * data buffer associated with the subdevice.  The amount reserved is limited
 * by the space available.
 *
 * Return: The amount of space reserved in bytes.
 */
unsigned int comedi_buf_write_alloc(struct comedi_subdevice *s,
				    unsigned int nbytes)
{
	struct comedi_async *async = s->async;
	unsigned int unalloc = comedi_buf_write_n_unalloc(s);

	if (nbytes > unalloc)
		nbytes = unalloc;

	async->buf_write_alloc_count += nbytes;

	/*
	 * ensure the async buffer 'counts' are read and updated
	 * before we write data to the write-alloc'ed buffer space
	 */
	smp_mb();

	return nbytes;
}
EXPORT_SYMBOL_GPL(comedi_buf_write_alloc);

/*
 * munging is applied to data by core as it passes between user
 * and kernel space
 */
static unsigned int comedi_buf_munge(struct comedi_subdevice *s,
				     unsigned int num_bytes)
{
	struct comedi_async *async = s->async;
	unsigned int count = 0;
	const unsigned int num_sample_bytes = comedi_bytes_per_sample(s);

	if (!s->munge || (async->cmd.flags & CMDF_RAWDATA)) {
		async->munge_count += num_bytes;
		return num_bytes;
	}

	/* don't munge partial samples */
	num_bytes -= num_bytes % num_sample_bytes;
	while (count < num_bytes) {
		int block_size = num_bytes - count;
		unsigned int buf_end;

		buf_end = async->prealloc_bufsz - async->munge_ptr;
		if (block_size > buf_end)
			block_size = buf_end;

		s->munge(s->device, s,
			 async->prealloc_buf + async->munge_ptr,
			 block_size, async->munge_chan);

		/*
		 * ensure data is munged in buffer before the
		 * async buffer munge_count is incremented
		 */
		smp_wmb();

		async->munge_chan += block_size / num_sample_bytes;
		async->munge_chan %= async->cmd.chanlist_len;
		async->munge_count += block_size;
		async->munge_ptr += block_size;
		async->munge_ptr %= async->prealloc_bufsz;
		count += block_size;
	}

	return count;
}

unsigned int comedi_buf_write_n_allocated(struct comedi_subdevice *s)
{
	struct comedi_async *async = s->async;

	return async->buf_write_alloc_count - async->buf_write_count;
}

/**
 * comedi_buf_write_free() - Free buffer space after it is written
 * @s: COMEDI subdevice.
 * @nbytes: Maximum space to free in bytes.
 *
 * Free up to @nbytes bytes of space previously reserved for writing in the
 * COMEDI acquisition data buffer associated with the subdevice.  The amount of
 * space freed is limited to the amount that was reserved.  The freed space is
 * assumed to have been filled with sample data by the writer.
 *
 * If the samples in the freed space need to be "munged", do so here.  The
 * freed space becomes available for allocation by the reader.
 *
 * Return: The amount of space freed in bytes.
 */
unsigned int comedi_buf_write_free(struct comedi_subdevice *s,
				   unsigned int nbytes)
{
	struct comedi_async *async = s->async;
	unsigned int allocated = comedi_buf_write_n_allocated(s);

	if (nbytes > allocated)
		nbytes = allocated;

	async->buf_write_count += nbytes;
	async->buf_write_ptr += nbytes;
	comedi_buf_munge(s, async->buf_write_count - async->munge_count);
	if (async->buf_write_ptr >= async->prealloc_bufsz)
		async->buf_write_ptr %= async->prealloc_bufsz;

	return nbytes;
}
EXPORT_SYMBOL_GPL(comedi_buf_write_free);

/**
 * comedi_buf_read_n_available() - Determine amount of readable buffer space
 * @s: COMEDI subdevice.
 *
 * Determine the amount of readable buffer space in the COMEDI acquisition data
 * buffer associated with the subdevice.  The readable buffer space is that
 * which has been freed by the writer and "munged" to the sample data format
 * expected by COMEDI if necessary.
 *
 * Return: The amount of readable buffer space.
 */
unsigned int comedi_buf_read_n_available(struct comedi_subdevice *s)
{
	struct comedi_async *async = s->async;
	unsigned int num_bytes;

	if (!async)
		return 0;

	num_bytes = async->munge_count - async->buf_read_count;

	/*
	 * ensure the async buffer 'counts' are read before we
	 * attempt to read data from the buffer
	 */
	smp_rmb();

	return num_bytes;
}
EXPORT_SYMBOL_GPL(comedi_buf_read_n_available);

/**
 * comedi_buf_read_alloc() - Reserve buffer space for reading
 * @s: COMEDI subdevice.
 * @nbytes: Maximum space to reserve in bytes.
 *
 * Reserve up to @nbytes bytes of previously written and "munged" buffer space
 * for reading in the COMEDI acquisition data buffer associated with the
 * subdevice.  The amount reserved is limited to the space available.  The
 * reader can read from the reserved space and then free it.  A reader is also
 * allowed to read from the space before reserving it as long as it determines
 * the amount of readable data available, but the space needs to be marked as
 * reserved before it can be freed.
 *
 * Return: The amount of space reserved in bytes.
 */
unsigned int comedi_buf_read_alloc(struct comedi_subdevice *s,
				   unsigned int nbytes)
{
	struct comedi_async *async = s->async;
	unsigned int available;

	available = async->munge_count - async->buf_read_alloc_count;
	if (nbytes > available)
		nbytes = available;

	async->buf_read_alloc_count += nbytes;

	/*
	 * ensure the async buffer 'counts' are read before we
	 * attempt to read data from the read-alloc'ed buffer space
	 */
	smp_rmb();

	return nbytes;
}
EXPORT_SYMBOL_GPL(comedi_buf_read_alloc);

static unsigned int comedi_buf_read_n_allocated(struct comedi_async *async)
{
	return async->buf_read_alloc_count - async->buf_read_count;
}

/**
 * comedi_buf_read_free() - Free buffer space after it has been read
 * @s: COMEDI subdevice.
 * @nbytes: Maximum space to free in bytes.
 *
 * Free up to @nbytes bytes of buffer space previously reserved for reading in
 * the COMEDI acquisition data buffer associated with the subdevice.  The
 * amount of space freed is limited to the amount that was reserved.
 *
 * The freed space becomes available for allocation by the writer.
 *
 * Return: The amount of space freed in bytes.
 */
unsigned int comedi_buf_read_free(struct comedi_subdevice *s,
				  unsigned int nbytes)
{
	struct comedi_async *async = s->async;
	unsigned int allocated;

	/*
	 * ensure data has been read out of buffer before
	 * the async read count is incremented
	 */
	smp_mb();

	allocated = comedi_buf_read_n_allocated(async);
	if (nbytes > allocated)
		nbytes = allocated;

	async->buf_read_count += nbytes;
	async->buf_read_ptr += nbytes;
	async->buf_read_ptr %= async->prealloc_bufsz;
	return nbytes;
}
EXPORT_SYMBOL_GPL(comedi_buf_read_free);

static void comedi_buf_memcpy_to(struct comedi_subdevice *s,
				 const void *data, unsigned int num_bytes)
{
	struct comedi_async *async = s->async;
	unsigned int write_ptr = async->buf_write_ptr;

	while (num_bytes) {
		unsigned int block_size;

		if (write_ptr + num_bytes > async->prealloc_bufsz)
			block_size = async->prealloc_bufsz - write_ptr;
		else
			block_size = num_bytes;

		memcpy(async->prealloc_buf + write_ptr, data, block_size);

		data += block_size;
		num_bytes -= block_size;

		write_ptr = 0;
	}
}

static void comedi_buf_memcpy_from(struct comedi_subdevice *s,
				   void *dest, unsigned int nbytes)
{
	void *src;
	struct comedi_async *async = s->async;
	unsigned int read_ptr = async->buf_read_ptr;

	while (nbytes) {
		unsigned int block_size;

		src = async->prealloc_buf + read_ptr;

		if (nbytes >= async->prealloc_bufsz - read_ptr)
			block_size = async->prealloc_bufsz - read_ptr;
		else
			block_size = nbytes;

		memcpy(dest, src, block_size);
		nbytes -= block_size;
		dest += block_size;
		read_ptr = 0;
	}
}

/**
 * comedi_buf_write_samples() - Write sample data to COMEDI buffer
 * @s: COMEDI subdevice.
 * @data: Pointer to source samples.
 * @nsamples: Number of samples to write.
 *
 * Write up to @nsamples samples to the COMEDI acquisition data buffer
 * associated with the subdevice, mark it as written and update the
 * acquisition scan progress.  If there is not enough room for the specified
 * number of samples, the number of samples written is limited to the number
 * that will fit and the %COMEDI_CB_OVERFLOW event flag is set to cause the
 * acquisition to terminate with an overrun error.  Set the %COMEDI_CB_BLOCK
 * event flag if any samples are written to cause waiting tasks to be woken
 * when the event flags are processed.
 *
 * Return: The amount of data written in bytes.
 */
unsigned int comedi_buf_write_samples(struct comedi_subdevice *s,
				      const void *data, unsigned int nsamples)
{
	unsigned int max_samples;
	unsigned int nbytes;

	/*
	 * Make sure there is enough room in the buffer for all the samples.
	 * If not, clamp the nsamples to the number that will fit, flag the
	 * buffer overrun and add the samples that fit.
	 */
	max_samples = comedi_bytes_to_samples(s, comedi_buf_write_n_unalloc(s));
	if (nsamples > max_samples) {
		dev_warn(s->device->class_dev, "buffer overrun\n");
		s->async->events |= COMEDI_CB_OVERFLOW;
		nsamples = max_samples;
	}

	if (nsamples == 0)
		return 0;

	nbytes = comedi_buf_write_alloc(s,
					comedi_samples_to_bytes(s, nsamples));
	comedi_buf_memcpy_to(s, data, nbytes);
	comedi_buf_write_free(s, nbytes);
	comedi_inc_scan_progress(s, nbytes);
	s->async->events |= COMEDI_CB_BLOCK;

	return nbytes;
}
EXPORT_SYMBOL_GPL(comedi_buf_write_samples);

/**
 * comedi_buf_read_samples() - Read sample data from COMEDI buffer
 * @s: COMEDI subdevice.
 * @data: Pointer to destination.
 * @nsamples: Maximum number of samples to read.
 *
 * Read up to @nsamples samples from the COMEDI acquisition data buffer
 * associated with the subdevice, mark it as read and update the acquisition
 * scan progress.  Limit the number of samples read to the number available.
 * Set the %COMEDI_CB_BLOCK event flag if any samples are read to cause waiting
 * tasks to be woken when the event flags are processed.
 *
 * Return: The amount of data read in bytes.
 */
unsigned int comedi_buf_read_samples(struct comedi_subdevice *s,
				     void *data, unsigned int nsamples)
{
	unsigned int max_samples;
	unsigned int nbytes;

	/* clamp nsamples to the number of full samples available */
	max_samples = comedi_bytes_to_samples(s,
					      comedi_buf_read_n_available(s));
	if (nsamples > max_samples)
		nsamples = max_samples;

	if (nsamples == 0)
		return 0;

	nbytes = comedi_buf_read_alloc(s,
				       comedi_samples_to_bytes(s, nsamples));
	comedi_buf_memcpy_from(s, data, nbytes);
	comedi_buf_read_free(s, nbytes);
	comedi_inc_scan_progress(s, nbytes);
	s->async->events |= COMEDI_CB_BLOCK;

	return nbytes;
}
EXPORT_SYMBOL_GPL(comedi_buf_read_samples);
