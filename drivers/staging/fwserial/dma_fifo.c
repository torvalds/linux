/*
 * DMA-able FIFO implementation
 *
 * Copyright (C) 2012 Peter Hurley <peter@hurleysoftware.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/bug.h>

#include "dma_fifo.h"

#ifdef DEBUG_TRACING
#define df_trace(s, args...) pr_debug(s, ##args)
#else
#define df_trace(s, args...)
#endif

#define FAIL(fifo, condition, format...) ({				\
	fifo->corrupt = !!(condition);					\
	WARN(fifo->corrupt, format);					\
})

/*
 * private helper fn to determine if check is in open interval (lo,hi)
 */
static bool addr_check(unsigned check, unsigned lo, unsigned hi)
{
	return check - (lo + 1) < (hi - 1) - lo;
}

/**
 * dma_fifo_init: initialize the fifo to a valid but inoperative state
 * @fifo: address of in-place "struct dma_fifo" object
 */
void dma_fifo_init(struct dma_fifo *fifo)
{
	memset(fifo, 0, sizeof(*fifo));
	INIT_LIST_HEAD(&fifo->pending);
}

/**
 * dma_fifo_alloc - initialize and allocate dma_fifo
 * @fifo: address of in-place "struct dma_fifo" object
 * @size: 'apparent' size, in bytes, of fifo
 * @align: dma alignment to maintain (should be at least cpu cache alignment),
 *         must be power of 2
 * @tx_limit: maximum # of bytes transmissable per dma (rounded down to
 *            multiple of alignment, but at least align size)
 * @open_limit: maximum # of outstanding dma transactions allowed
 * @gfp_mask: get_free_pages mask, passed to kmalloc()
 *
 * The 'apparent' size will be rounded up to next greater aligned size.
 * Returns 0 if no error, otherwise an error code
 */
int dma_fifo_alloc(struct dma_fifo *fifo, int size, unsigned align,
		   int tx_limit, int open_limit, gfp_t gfp_mask)
{
	int capacity;

	if (!is_power_of_2(align) || size < 0)
		return -EINVAL;

	size = round_up(size, align);
	capacity = size + align * open_limit + align * DMA_FIFO_GUARD;
	fifo->data = kmalloc(capacity, gfp_mask);
	if (!fifo->data)
		return -ENOMEM;

	fifo->in = 0;
	fifo->out = 0;
	fifo->done = 0;
	fifo->size = size;
	fifo->avail = size;
	fifo->align = align;
	fifo->tx_limit = max_t(int, round_down(tx_limit, align), align);
	fifo->open = 0;
	fifo->open_limit = open_limit;
	fifo->guard = size + align * open_limit;
	fifo->capacity = capacity;
	fifo->corrupt = 0;

	return 0;
}

/**
 * dma_fifo_free - frees the fifo
 * @fifo: address of in-place "struct dma_fifo" to free
 *
 * Also reinits the fifo to a valid but inoperative state. This
 * allows the fifo to be reused with a different target requiring
 * different fifo parameters.
 */
void dma_fifo_free(struct dma_fifo *fifo)
{
	struct dma_pending *pending, *next;

	if (fifo->data == NULL)
		return;

	list_for_each_entry_safe(pending, next, &fifo->pending, link)
		list_del_init(&pending->link);
	kfree(fifo->data);
	fifo->data = NULL;
}

/**
 * dma_fifo_reset - dumps the fifo contents and reinits for reuse
 * @fifo: address of in-place "struct dma_fifo" to reset
 */
void dma_fifo_reset(struct dma_fifo *fifo)
{
	struct dma_pending *pending, *next;

	if (fifo->data == NULL)
		return;

	list_for_each_entry_safe(pending, next, &fifo->pending, link)
		list_del_init(&pending->link);
	fifo->in = 0;
	fifo->out = 0;
	fifo->done = 0;
	fifo->avail = fifo->size;
	fifo->open = 0;
	fifo->corrupt = 0;
}

/**
 * dma_fifo_in - copies data into the fifo
 * @fifo: address of in-place "struct dma_fifo" to write to
 * @src: buffer to copy from
 * @n: # of bytes to copy
 *
 * Returns the # of bytes actually copied, which can be less than requested if
 * the fifo becomes full. If < 0, return is error code.
 */
int dma_fifo_in(struct dma_fifo *fifo, const void *src, int n)
{
	int ofs, l;

	if (fifo->data == NULL)
		return -ENOENT;
	if (fifo->corrupt)
		return -ENXIO;

	if (n > fifo->avail)
		n = fifo->avail;
	if (n <= 0)
		return 0;

	ofs = fifo->in % fifo->capacity;
	l = min(n, fifo->capacity - ofs);
	memcpy(fifo->data + ofs, src, l);
	memcpy(fifo->data, src + l, n - l);

	if (FAIL(fifo, addr_check(fifo->done, fifo->in, fifo->in + n) ||
			fifo->avail < n,
			"fifo corrupt: in:%u out:%u done:%u n:%d avail:%d",
			fifo->in, fifo->out, fifo->done, n, fifo->avail))
		return -ENXIO;

	fifo->in += n;
	fifo->avail -= n;

	df_trace("in:%u out:%u done:%u n:%d avail:%d", fifo->in, fifo->out,
		 fifo->done, n, fifo->avail);

	return n;
}

/**
 * dma_fifo_out_pend - gets address/len of next avail read and marks as pended
 * @fifo: address of in-place "struct dma_fifo" to read from
 * @pended: address of structure to fill with read address/len
 *          The data/len fields will be NULL/0 if no dma is pended.
 *
 * Returns the # of used bytes remaining in fifo (ie, if > 0, more data
 * remains in the fifo that was not pended). If < 0, return is error code.
 */
int dma_fifo_out_pend(struct dma_fifo *fifo, struct dma_pending *pended)
{
	unsigned len, n, ofs, l, limit;

	if (fifo->data == NULL)
		return -ENOENT;
	if (fifo->corrupt)
		return -ENXIO;

	pended->len = 0;
	pended->data = NULL;
	pended->out = fifo->out;

	len = fifo->in - fifo->out;
	if (!len)
		return -ENODATA;
	if (fifo->open == fifo->open_limit)
		return -EAGAIN;

	n = len;
	ofs = fifo->out % fifo->capacity;
	l = fifo->capacity - ofs;
	limit = min_t(unsigned, l, fifo->tx_limit);
	if (n > limit) {
		n = limit;
		fifo->out += limit;
	} else if (ofs + n > fifo->guard) {
		fifo->out += l;
		fifo->in = fifo->out;
	} else {
		fifo->out += round_up(n, fifo->align);
		fifo->in = fifo->out;
	}

	df_trace("in: %u out: %u done: %u n: %d len: %u avail: %d", fifo->in,
		 fifo->out, fifo->done, n, len, fifo->avail);

	pended->len = n;
	pended->data = fifo->data + ofs;
	pended->next = fifo->out;
	list_add_tail(&pended->link, &fifo->pending);
	++fifo->open;

	if (FAIL(fifo, fifo->open > fifo->open_limit,
			"past open limit:%d (limit:%d)",
			fifo->open, fifo->open_limit))
		return -ENXIO;
	if (FAIL(fifo, fifo->out & (fifo->align - 1),
			"fifo out unaligned:%u (align:%u)",
			fifo->out, fifo->align))
		return -ENXIO;

	return len - n;
}

/**
 * dma_fifo_out_complete - marks pended dma as completed
 * @fifo: address of in-place "struct dma_fifo" which was read from
 * @complete: address of structure for previously pended dma to mark completed
 */
int dma_fifo_out_complete(struct dma_fifo *fifo, struct dma_pending *complete)
{
	struct dma_pending *pending, *next, *tmp;

	if (fifo->data == NULL)
		return -ENOENT;
	if (fifo->corrupt)
		return -ENXIO;
	if (list_empty(&fifo->pending) && fifo->open == 0)
		return -EINVAL;

	if (FAIL(fifo, list_empty(&fifo->pending) != (fifo->open == 0),
			"pending list disagrees with open count:%d",
			fifo->open))
		return -ENXIO;

	tmp = complete->data;
	*tmp = *complete;
	list_replace(&complete->link, &tmp->link);
	dp_mark_completed(tmp);

	/* Only update the fifo in the original pended order */
	list_for_each_entry_safe(pending, next, &fifo->pending, link) {
		if (!dp_is_completed(pending)) {
			df_trace("still pending: saved out: %u len: %d",
				 pending->out, pending->len);
			break;
		}

		if (FAIL(fifo, pending->out != fifo->done ||
				addr_check(fifo->in, fifo->done, pending->next),
				"in:%u out:%u done:%u saved:%u next:%u",
				fifo->in, fifo->out, fifo->done, pending->out,
				pending->next))
			return -ENXIO;

		list_del_init(&pending->link);
		fifo->done = pending->next;
		fifo->avail += pending->len;
		--fifo->open;

		df_trace("in: %u out: %u done: %u len: %u avail: %d", fifo->in,
			 fifo->out, fifo->done, pending->len, fifo->avail);
	}

	if (FAIL(fifo, fifo->open < 0, "open dma:%d < 0", fifo->open))
		return -ENXIO;
	if (FAIL(fifo, fifo->avail > fifo->size, "fifo avail:%d > size:%d",
			fifo->avail, fifo->size))
		return -ENXIO;

	return 0;
}
