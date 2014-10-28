/* Copyright (c) 2014 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/types.h>
#include <linux/netdevice.h>

#include <brcmu_utils.h>
#include <brcmu_wifi.h>

#include "core.h"
#include "commonring.h"


/* dma flushing needs implementation for mips and arm platforms. Should
 * be put in util. Note, this is not real flushing. It is virtual non
 * cached memory. Only write buffers should have to be drained. Though
 * this may be different depending on platform......
 * SEE ALSO msgbuf.c
 */
#define brcmf_dma_flush(addr, len)
#define brcmf_dma_invalidate_cache(addr, len)


void brcmf_commonring_register_cb(struct brcmf_commonring *commonring,
				  int (*cr_ring_bell)(void *ctx),
				  int (*cr_update_rptr)(void *ctx),
				  int (*cr_update_wptr)(void *ctx),
				  int (*cr_write_rptr)(void *ctx),
				  int (*cr_write_wptr)(void *ctx), void *ctx)
{
	commonring->cr_ring_bell = cr_ring_bell;
	commonring->cr_update_rptr = cr_update_rptr;
	commonring->cr_update_wptr = cr_update_wptr;
	commonring->cr_write_rptr = cr_write_rptr;
	commonring->cr_write_wptr = cr_write_wptr;
	commonring->cr_ctx = ctx;
}


void brcmf_commonring_config(struct brcmf_commonring *commonring, u16 depth,
			     u16 item_len, void *buf_addr)
{
	commonring->depth = depth;
	commonring->item_len = item_len;
	commonring->buf_addr = buf_addr;
	if (!commonring->inited) {
		spin_lock_init(&commonring->lock);
		commonring->inited = true;
	}
	commonring->r_ptr = 0;
	if (commonring->cr_write_rptr)
		commonring->cr_write_rptr(commonring->cr_ctx);
	commonring->w_ptr = 0;
	if (commonring->cr_write_wptr)
		commonring->cr_write_wptr(commonring->cr_ctx);
	commonring->f_ptr = 0;
}


void brcmf_commonring_lock(struct brcmf_commonring *commonring)
		__acquires(&commonring->lock)
{
	unsigned long flags;

	spin_lock_irqsave(&commonring->lock, flags);
	commonring->flags = flags;
}


void brcmf_commonring_unlock(struct brcmf_commonring *commonring)
		__releases(&commonring->lock)
{
	spin_unlock_irqrestore(&commonring->lock, commonring->flags);
}


bool brcmf_commonring_write_available(struct brcmf_commonring *commonring)
{
	u16 available;
	bool retry = true;

again:
	if (commonring->r_ptr <= commonring->w_ptr)
		available = commonring->depth - commonring->w_ptr +
			    commonring->r_ptr;
	else
		available = commonring->r_ptr - commonring->w_ptr;

	if (available > 1) {
		if (!commonring->was_full)
			return true;
		if (available > commonring->depth / 8) {
			commonring->was_full = false;
			return true;
		}
		if (retry) {
			if (commonring->cr_update_rptr)
				commonring->cr_update_rptr(commonring->cr_ctx);
			retry = false;
			goto again;
		}
		return false;
	}

	if (retry) {
		if (commonring->cr_update_rptr)
			commonring->cr_update_rptr(commonring->cr_ctx);
		retry = false;
		goto again;
	}

	commonring->was_full = true;
	return false;
}


void *brcmf_commonring_reserve_for_write(struct brcmf_commonring *commonring)
{
	void *ret_ptr;
	u16 available;
	bool retry = true;

again:
	if (commonring->r_ptr <= commonring->w_ptr)
		available = commonring->depth - commonring->w_ptr +
			    commonring->r_ptr;
	else
		available = commonring->r_ptr - commonring->w_ptr;

	if (available > 1) {
		ret_ptr = commonring->buf_addr +
			  (commonring->w_ptr * commonring->item_len);
		commonring->w_ptr++;
		if (commonring->w_ptr == commonring->depth)
			commonring->w_ptr = 0;
		return ret_ptr;
	}

	if (retry) {
		if (commonring->cr_update_rptr)
			commonring->cr_update_rptr(commonring->cr_ctx);
		retry = false;
		goto again;
	}

	commonring->was_full = true;
	return NULL;
}


void *
brcmf_commonring_reserve_for_write_multiple(struct brcmf_commonring *commonring,
					    u16 n_items, u16 *alloced)
{
	void *ret_ptr;
	u16 available;
	bool retry = true;

again:
	if (commonring->r_ptr <= commonring->w_ptr)
		available = commonring->depth - commonring->w_ptr +
			    commonring->r_ptr;
	else
		available = commonring->r_ptr - commonring->w_ptr;

	if (available > 1) {
		ret_ptr = commonring->buf_addr +
			  (commonring->w_ptr * commonring->item_len);
		*alloced = min_t(u16, n_items, available - 1);
		if (*alloced + commonring->w_ptr > commonring->depth)
			*alloced = commonring->depth - commonring->w_ptr;
		commonring->w_ptr += *alloced;
		if (commonring->w_ptr == commonring->depth)
			commonring->w_ptr = 0;
		return ret_ptr;
	}

	if (retry) {
		if (commonring->cr_update_rptr)
			commonring->cr_update_rptr(commonring->cr_ctx);
		retry = false;
		goto again;
	}

	commonring->was_full = true;
	return NULL;
}


int brcmf_commonring_write_complete(struct brcmf_commonring *commonring)
{
	void *address;

	address = commonring->buf_addr;
	address += (commonring->f_ptr * commonring->item_len);
	if (commonring->f_ptr > commonring->w_ptr) {
		brcmf_dma_flush(address,
				(commonring->depth - commonring->f_ptr) *
				commonring->item_len);
		address = commonring->buf_addr;
		commonring->f_ptr = 0;
	}
	brcmf_dma_flush(address, (commonring->w_ptr - commonring->f_ptr) *
			commonring->item_len);

	commonring->f_ptr = commonring->w_ptr;

	if (commonring->cr_write_wptr)
		commonring->cr_write_wptr(commonring->cr_ctx);
	if (commonring->cr_ring_bell)
		return commonring->cr_ring_bell(commonring->cr_ctx);

	return -EIO;
}


void brcmf_commonring_write_cancel(struct brcmf_commonring *commonring,
				   u16 n_items)
{
	if (commonring->w_ptr == 0)
		commonring->w_ptr = commonring->depth - n_items;
	else
		commonring->w_ptr -= n_items;
}


void *brcmf_commonring_get_read_ptr(struct brcmf_commonring *commonring,
				    u16 *n_items)
{
	void *ret_addr;

	if (commonring->cr_update_wptr)
		commonring->cr_update_wptr(commonring->cr_ctx);

	*n_items = (commonring->w_ptr >= commonring->r_ptr) ?
				(commonring->w_ptr - commonring->r_ptr) :
				(commonring->depth - commonring->r_ptr);

	if (*n_items == 0)
		return NULL;

	ret_addr = commonring->buf_addr +
		   (commonring->r_ptr * commonring->item_len);

	commonring->r_ptr += *n_items;
	if (commonring->r_ptr == commonring->depth)
		commonring->r_ptr = 0;

	brcmf_dma_invalidate_cache(ret_addr, *n_ items * commonring->item_len);

	return ret_addr;
}


int brcmf_commonring_read_complete(struct brcmf_commonring *commonring)
{
	if (commonring->cr_write_rptr)
		return commonring->cr_write_rptr(commonring->cr_ctx);

	return -EIO;
}
