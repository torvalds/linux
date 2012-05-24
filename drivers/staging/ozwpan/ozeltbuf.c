/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include "ozconfig.h"
#include "ozprotocol.h"
#include "ozeltbuf.h"
#include "ozpd.h"
#include "oztrace.h"
/*------------------------------------------------------------------------------
 */
#define OZ_ELT_INFO_MAGIC_USED	0x35791057
#define OZ_ELT_INFO_MAGIC_FREE	0x78940102
/*------------------------------------------------------------------------------
 * Context: softirq-serialized
 */
int oz_elt_buf_init(struct oz_elt_buf *buf)
{
	memset(buf, 0, sizeof(struct oz_elt_buf));
	INIT_LIST_HEAD(&buf->stream_list);
	INIT_LIST_HEAD(&buf->order_list);
	INIT_LIST_HEAD(&buf->isoc_list);
	buf->max_free_elts = 32;
	spin_lock_init(&buf->lock);
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: softirq or process
 */
void oz_elt_buf_term(struct oz_elt_buf *buf)
{
	struct list_head *e;
	int i;
	/* Free any elements in the order or isoc lists. */
	for (i = 0; i < 2; i++) {
		struct list_head *list;
		if (i)
			list = &buf->order_list;
		else
			list = &buf->isoc_list;
		e = list->next;
		while (e != list) {
			struct oz_elt_info *ei =
				container_of(e, struct oz_elt_info, link_order);
			e = e->next;
			kfree(ei);
		}
	}
	/* Free any elelment in the pool. */
	while (buf->elt_pool) {
		struct oz_elt_info *ei =
			container_of(buf->elt_pool, struct oz_elt_info, link);
		buf->elt_pool = buf->elt_pool->next;
		kfree(ei);
	}
	buf->free_elts = 0;
}
/*------------------------------------------------------------------------------
 * Context: softirq or process
 */
struct oz_elt_info *oz_elt_info_alloc(struct oz_elt_buf *buf)
{
	struct oz_elt_info *ei = 0;
	spin_lock_bh(&buf->lock);
	if (buf->free_elts && buf->elt_pool) {
		ei = container_of(buf->elt_pool, struct oz_elt_info, link);
		buf->elt_pool = ei->link.next;
		buf->free_elts--;
		spin_unlock_bh(&buf->lock);
		if (ei->magic != OZ_ELT_INFO_MAGIC_FREE) {
			oz_trace("oz_elt_info_alloc: ei with bad magic: 0x%x\n",
				ei->magic);
		}
	} else {
		spin_unlock_bh(&buf->lock);
		ei = kmalloc(sizeof(struct oz_elt_info), GFP_ATOMIC);
	}
	if (ei) {
		ei->flags = 0;
		ei->app_id = 0;
		ei->callback = 0;
		ei->context = 0;
		ei->stream = 0;
		ei->magic = OZ_ELT_INFO_MAGIC_USED;
		INIT_LIST_HEAD(&ei->link);
		INIT_LIST_HEAD(&ei->link_order);
	}
	return ei;
}
/*------------------------------------------------------------------------------
 * Precondition: oz_elt_buf.lock must be held.
 * Context: softirq or process
 */
void oz_elt_info_free(struct oz_elt_buf *buf, struct oz_elt_info *ei)
{
	if (ei) {
		if (ei->magic == OZ_ELT_INFO_MAGIC_USED) {
			buf->free_elts++;
			ei->link.next = buf->elt_pool;
			buf->elt_pool = &ei->link;
			ei->magic = OZ_ELT_INFO_MAGIC_FREE;
		} else {
			oz_trace("oz_elt_info_free: bad magic ei: %p"
				" magic: 0x%x\n",
				ei, ei->magic);
		}
	}
}
/*------------------------------------------------------------------------------
 * Context: softirq
 */
void oz_elt_info_free_chain(struct oz_elt_buf *buf, struct list_head *list)
{
	struct list_head *e;
	e = list->next;
	spin_lock_bh(&buf->lock);
	while (e != list) {
		struct oz_elt_info *ei;
		ei = container_of(e, struct oz_elt_info, link);
		e = e->next;
		oz_elt_info_free(buf, ei);
	}
	spin_unlock_bh(&buf->lock);
}
/*------------------------------------------------------------------------------
 */
int oz_elt_stream_create(struct oz_elt_buf *buf, u8 id, int max_buf_count)
{
	struct oz_elt_stream *st;

	oz_trace("oz_elt_stream_create(0x%x)\n", id);

	st = kzalloc(sizeof(struct oz_elt_stream), GFP_ATOMIC | __GFP_ZERO);
	if (st == 0)
		return -ENOMEM;
	atomic_set(&st->ref_count, 1);
	st->id = id;
	st->max_buf_count = max_buf_count;
	INIT_LIST_HEAD(&st->elt_list);
	spin_lock_bh(&buf->lock);
	list_add_tail(&st->link, &buf->stream_list);
	spin_unlock_bh(&buf->lock);
	return 0;
}
/*------------------------------------------------------------------------------
 */
int oz_elt_stream_delete(struct oz_elt_buf *buf, u8 id)
{
	struct list_head *e;
	struct oz_elt_stream *st;
	oz_trace("oz_elt_stream_delete(0x%x)\n", id);
	spin_lock_bh(&buf->lock);
	e = buf->stream_list.next;
	while (e != &buf->stream_list) {
		st = container_of(e, struct oz_elt_stream, link);
		if (st->id == id) {
			list_del(e);
			break;
		}
		st = 0;
	}
	if (!st) {
		spin_unlock_bh(&buf->lock);
		return -1;
	}
	e = st->elt_list.next;
	while (e != &st->elt_list) {
		struct oz_elt_info *ei =
			container_of(e, struct oz_elt_info, link);
		e = e->next;
		list_del_init(&ei->link);
		list_del_init(&ei->link_order);
		st->buf_count -= ei->length;
		oz_trace2(OZ_TRACE_STREAM, "Stream down: %d  %d %d\n",
			st->buf_count,
			ei->length, atomic_read(&st->ref_count));
		oz_elt_stream_put(st);
		oz_elt_info_free(buf, ei);
	}
	spin_unlock_bh(&buf->lock);
	oz_elt_stream_put(st);
	return 0;
}
/*------------------------------------------------------------------------------
 */
void oz_elt_stream_get(struct oz_elt_stream *st)
{
	atomic_inc(&st->ref_count);
}
/*------------------------------------------------------------------------------
 */
void oz_elt_stream_put(struct oz_elt_stream *st)
{
	if (atomic_dec_and_test(&st->ref_count)) {
		oz_trace("Stream destroyed\n");
		kfree(st);
	}
}
/*------------------------------------------------------------------------------
 * Precondition: Element buffer lock must be held.
 * If this function fails the caller is responsible for deallocating the elt
 * info structure.
 */
int oz_queue_elt_info(struct oz_elt_buf *buf, u8 isoc, u8 id,
	struct oz_elt_info *ei)
{
	struct oz_elt_stream *st = 0;
	struct list_head *e;
	if (id) {
		list_for_each(e, &buf->stream_list) {
			st = container_of(e, struct oz_elt_stream, link);
			if (st->id == id)
				break;
		}
		if (e == &buf->stream_list) {
			/* Stream specified but stream not known so fail.
			 * Caller deallocates element info. */
			return -1;
		}
	}
	if (st) {
		/* If this is an ISOC fixed element that needs a frame number
		 * then insert that now. Earlier we stored the unit count in
		 * this field.
		 */
		struct oz_isoc_fixed *body = (struct oz_isoc_fixed *)
			&ei->data[sizeof(struct oz_elt)];
		if ((body->app_id == OZ_APPID_USB) && (body->type
			== OZ_USB_ENDPOINT_DATA) &&
			(body->format == OZ_DATA_F_ISOC_FIXED)) {
			u8 unit_count = body->frame_number;
			body->frame_number = st->frame_number;
			st->frame_number += unit_count;
		}
		/* Claim stream and update accounts */
		oz_elt_stream_get(st);
		ei->stream = st;
		st->buf_count += ei->length;
		/* Add to list in stream. */
		list_add_tail(&ei->link, &st->elt_list);
		oz_trace2(OZ_TRACE_STREAM, "Stream up: %d  %d\n",
			st->buf_count, ei->length);
		/* Check if we have too much buffered for this stream. If so
		 * start dropping elements until we are back in bounds.
		 */
		while ((st->buf_count > st->max_buf_count) &&
			!list_empty(&st->elt_list)) {
			struct oz_elt_info *ei2 =
				list_first_entry(&st->elt_list,
					struct oz_elt_info, link);
			list_del_init(&ei2->link);
			list_del_init(&ei2->link_order);
			st->buf_count -= ei2->length;
			oz_elt_info_free(buf, ei2);
			oz_elt_stream_put(st);
		}
	}
	list_add_tail(&ei->link_order, isoc ?
		&buf->isoc_list : &buf->order_list);
	return 0;
}
/*------------------------------------------------------------------------------
 */
int oz_select_elts_for_tx(struct oz_elt_buf *buf, u8 isoc, unsigned *len,
		unsigned max_len, struct list_head *list)
{
	int count = 0;
	struct list_head *e;
	struct list_head *el;
	struct oz_elt_info *ei;
	spin_lock_bh(&buf->lock);
	if (isoc)
		el = &buf->isoc_list;
	else
		el = &buf->order_list;
	e = el->next;
	while (e != el) {
		struct oz_app_hdr *app_hdr;
		ei = container_of(e, struct oz_elt_info, link_order);
		e = e->next;
		if ((*len + ei->length) <= max_len) {
			app_hdr = (struct oz_app_hdr *)
				&ei->data[sizeof(struct oz_elt)];
			app_hdr->elt_seq_num = buf->tx_seq_num[ei->app_id]++;
			if (buf->tx_seq_num[ei->app_id] == 0)
				buf->tx_seq_num[ei->app_id] = 1;
			*len += ei->length;
			list_del(&ei->link);
			list_del(&ei->link_order);
			if (ei->stream) {
				ei->stream->buf_count -= ei->length;
				oz_trace2(OZ_TRACE_STREAM,
					"Stream down: %d  %d\n",
					ei->stream->buf_count, ei->length);
				oz_elt_stream_put(ei->stream);
				ei->stream = 0;
			}
			INIT_LIST_HEAD(&ei->link_order);
			list_add_tail(&ei->link, list);
			count++;
		} else {
			break;
		}
	}
	spin_unlock_bh(&buf->lock);
	return count;
}
/*------------------------------------------------------------------------------
 */
int oz_are_elts_available(struct oz_elt_buf *buf)
{
	return buf->order_list.next != &buf->order_list;
}
/*------------------------------------------------------------------------------
 */
void oz_trim_elt_pool(struct oz_elt_buf *buf)
{
	struct list_head *free = 0;
	struct list_head *e;
	spin_lock_bh(&buf->lock);
	while (buf->free_elts > buf->max_free_elts) {
		e = buf->elt_pool;
		buf->elt_pool = e->next;
		e->next = free;
		free = e;
		buf->free_elts--;
	}
	spin_unlock_bh(&buf->lock);
	while (free) {
		struct oz_elt_info *ei =
			container_of(free, struct oz_elt_info, link);
		free = free->next;
		kfree(ei);
	}
}
