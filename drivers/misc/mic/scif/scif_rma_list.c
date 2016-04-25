/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Intel SCIF driver.
 *
 */
#include "scif_main.h"
#include <linux/mmu_notifier.h>
#include <linux/highmem.h>

/*
 * scif_insert_tcw:
 *
 * Insert a temp window to the temp registration list sorted by va_for_temp.
 * RMA lock must be held.
 */
void scif_insert_tcw(struct scif_window *window, struct list_head *head)
{
	struct scif_window *curr = NULL;
	struct scif_window *prev = list_entry(head, struct scif_window, list);
	struct list_head *item;

	INIT_LIST_HEAD(&window->list);
	/* Compare with tail and if the entry is new tail add it to the end */
	if (!list_empty(head)) {
		curr = list_entry(head->prev, struct scif_window, list);
		if (curr->va_for_temp < window->va_for_temp) {
			list_add_tail(&window->list, head);
			return;
		}
	}
	list_for_each(item, head) {
		curr = list_entry(item, struct scif_window, list);
		if (curr->va_for_temp > window->va_for_temp)
			break;
		prev = curr;
	}
	list_add(&window->list, &prev->list);
}

/*
 * scif_insert_window:
 *
 * Insert a window to the self registration list sorted by offset.
 * RMA lock must be held.
 */
void scif_insert_window(struct scif_window *window, struct list_head *head)
{
	struct scif_window *curr = NULL, *prev = NULL;
	struct list_head *item;

	INIT_LIST_HEAD(&window->list);
	list_for_each(item, head) {
		curr = list_entry(item, struct scif_window, list);
		if (curr->offset > window->offset)
			break;
		prev = curr;
	}
	if (!prev)
		list_add(&window->list, head);
	else
		list_add(&window->list, &prev->list);
	scif_set_window_ref(window, window->nr_pages);
}

/*
 * scif_query_tcw:
 *
 * Query the temp cached registration list of ep for an overlapping window
 * in case of permission mismatch, destroy the previous window. if permissions
 * match and overlap is partial, destroy the window but return the new range
 * RMA lock must be held.
 */
int scif_query_tcw(struct scif_endpt *ep, struct scif_rma_req *req)
{
	struct list_head *item, *temp, *head = req->head;
	struct scif_window *window;
	u64 start_va_window, start_va_req = req->va_for_temp;
	u64 end_va_window, end_va_req = start_va_req + req->nr_bytes;

	if (!req->nr_bytes)
		return -EINVAL;
	/*
	 * Avoid traversing the entire list to find out that there
	 * is no entry that matches
	 */
	if (!list_empty(head)) {
		window = list_last_entry(head, struct scif_window, list);
		end_va_window = window->va_for_temp +
			(window->nr_pages << PAGE_SHIFT);
		if (start_va_req > end_va_window)
			return -ENXIO;
	}
	list_for_each_safe(item, temp, head) {
		window = list_entry(item, struct scif_window, list);
		start_va_window = window->va_for_temp;
		end_va_window = window->va_for_temp +
			(window->nr_pages << PAGE_SHIFT);
		if (start_va_req < start_va_window &&
		    end_va_req < start_va_window)
			break;
		if (start_va_req >= end_va_window)
			continue;
		if ((window->prot & req->prot) == req->prot) {
			if (start_va_req >= start_va_window &&
			    end_va_req <= end_va_window) {
				*req->out_window = window;
				return 0;
			}
			/* expand window */
			if (start_va_req < start_va_window) {
				req->nr_bytes +=
					start_va_window - start_va_req;
				req->va_for_temp = start_va_window;
			}
			if (end_va_req >= end_va_window)
				req->nr_bytes += end_va_window - end_va_req;
		}
		/* Destroy the old window to create a new one */
		__scif_rma_destroy_tcw_helper(window);
		break;
	}
	return -ENXIO;
}

/*
 * scif_query_window:
 *
 * Query the registration list and check if a valid contiguous
 * range of windows exist.
 * RMA lock must be held.
 */
int scif_query_window(struct scif_rma_req *req)
{
	struct list_head *item;
	struct scif_window *window;
	s64 end_offset, offset = req->offset;
	u64 tmp_min, nr_bytes_left = req->nr_bytes;

	if (!req->nr_bytes)
		return -EINVAL;

	list_for_each(item, req->head) {
		window = list_entry(item, struct scif_window, list);
		end_offset = window->offset +
			(window->nr_pages << PAGE_SHIFT);
		if (offset < window->offset)
			/* Offset not found! */
			return -ENXIO;
		if (offset >= end_offset)
			continue;
		/* Check read/write protections. */
		if ((window->prot & req->prot) != req->prot)
			return -EPERM;
		if (nr_bytes_left == req->nr_bytes)
			/* Store the first window */
			*req->out_window = window;
		tmp_min = min((u64)end_offset - offset, nr_bytes_left);
		nr_bytes_left -= tmp_min;
		offset += tmp_min;
		/*
		 * Range requested encompasses
		 * multiple windows contiguously.
		 */
		if (!nr_bytes_left) {
			/* Done for partial window */
			if (req->type == SCIF_WINDOW_PARTIAL ||
			    req->type == SCIF_WINDOW_SINGLE)
				return 0;
			/* Extra logic for full windows */
			if (offset == end_offset)
				/* Spanning multiple whole windows */
				return 0;
				/* Not spanning multiple whole windows */
			return -ENXIO;
		}
		if (req->type == SCIF_WINDOW_SINGLE)
			break;
	}
	dev_err(scif_info.mdev.this_device,
		"%s %d ENXIO\n", __func__, __LINE__);
	return -ENXIO;
}

/*
 * scif_rma_list_unregister:
 *
 * Traverse the self registration list starting from window:
 * 1) Call scif_unregister_window(..)
 * RMA lock must be held.
 */
int scif_rma_list_unregister(struct scif_window *window,
			     s64 offset, int nr_pages)
{
	struct scif_endpt *ep = (struct scif_endpt *)window->ep;
	struct list_head *head = &ep->rma_info.reg_list;
	s64 end_offset;
	int err = 0;
	int loop_nr_pages;
	struct scif_window *_window;

	list_for_each_entry_safe_from(window, _window, head, list) {
		end_offset = window->offset + (window->nr_pages << PAGE_SHIFT);
		loop_nr_pages = min((int)((end_offset - offset) >> PAGE_SHIFT),
				    nr_pages);
		err = scif_unregister_window(window);
		if (err)
			return err;
		nr_pages -= loop_nr_pages;
		offset += (loop_nr_pages << PAGE_SHIFT);
		if (!nr_pages)
			break;
	}
	return 0;
}

/*
 * scif_unmap_all_window:
 *
 * Traverse all the windows in the self registration list and:
 * 1) Delete any DMA mappings created
 */
void scif_unmap_all_windows(scif_epd_t epd)
{
	struct list_head *item, *tmp;
	struct scif_window *window;
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	struct list_head *head = &ep->rma_info.reg_list;

	mutex_lock(&ep->rma_info.rma_lock);
	list_for_each_safe(item, tmp, head) {
		window = list_entry(item, struct scif_window, list);
		scif_unmap_window(ep->remote_dev, window);
	}
	mutex_unlock(&ep->rma_info.rma_lock);
}

/*
 * scif_unregister_all_window:
 *
 * Traverse all the windows in the self registration list and:
 * 1) Call scif_unregister_window(..)
 * RMA lock must be held.
 */
int scif_unregister_all_windows(scif_epd_t epd)
{
	struct list_head *item, *tmp;
	struct scif_window *window;
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	struct list_head *head = &ep->rma_info.reg_list;
	int err = 0;

	mutex_lock(&ep->rma_info.rma_lock);
retry:
	item = NULL;
	tmp = NULL;
	list_for_each_safe(item, tmp, head) {
		window = list_entry(item, struct scif_window, list);
		ep->rma_info.async_list_del = 0;
		err = scif_unregister_window(window);
		if (err)
			dev_err(scif_info.mdev.this_device,
				"%s %d err %d\n",
				__func__, __LINE__, err);
		/*
		 * Need to restart list traversal if there has been
		 * an asynchronous list entry deletion.
		 */
		if (ACCESS_ONCE(ep->rma_info.async_list_del))
			goto retry;
	}
	mutex_unlock(&ep->rma_info.rma_lock);
	if (!list_empty(&ep->rma_info.mmn_list)) {
		spin_lock(&scif_info.rmalock);
		list_add_tail(&ep->mmu_list, &scif_info.mmu_notif_cleanup);
		spin_unlock(&scif_info.rmalock);
		schedule_work(&scif_info.mmu_notif_work);
	}
	return err;
}
