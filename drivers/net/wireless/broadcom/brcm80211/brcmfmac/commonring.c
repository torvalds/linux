// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014 Broadcom Corporation
 */

#include <linux/types.h>
#include <linux/netdevice.h>

#include <brcmu_utils.h>
#include <brcmu_wifi.h>

#include "core.h"
#include "commonring.h"

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
		address = commonring->buf_addr;
		commonring->f_ptr = 0;
	}

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
	if (commonring->cr_update_wptr)
		commonring->cr_update_wptr(commonring->cr_ctx);

	*n_items = (commonring->w_ptr >= commonring->r_ptr) ?
				(commonring->w_ptr - commonring->r_ptr) :
				(commonring->depth - commonring->r_ptr);

	if (*n_items == 0)
		return NULL;

	return commonring->buf_addr +
	       (commonring->r_ptr * commonring->item_len);
}


int brcmf_commonring_read_complete(struct brcmf_commonring *commonring,
				   u16 n_items)
{
	commonring->r_ptr += n_items;
	if (commonring->r_ptr == commonring->depth)
		commonring->r_ptr = 0;

	if (commonring->cr_write_rptr)
		return commonring->cr_write_rptr(commonring->cr_ctx);

	return -EIO;
}
