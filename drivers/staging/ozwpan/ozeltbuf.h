/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#ifndef _OZELTBUF_H
#define _OZELTBUF_H

#include "ozprotocol.h"

/*-----------------------------------------------------------------------------
 */
struct oz_pd;
typedef void (*oz_elt_callback_t)(struct oz_pd *pd, long context);

struct oz_elt_stream {
	struct list_head link;
	struct list_head elt_list;
	atomic_t ref_count;
	unsigned buf_count;
	unsigned max_buf_count;
	u8 frame_number;
	u8 id;
};

#define OZ_MAX_ELT_PAYLOAD	255
struct oz_elt_info {
	struct list_head link;
	struct list_head link_order;
	u8 flags;
	u8 app_id;
	oz_elt_callback_t callback;
	long context;
	struct oz_elt_stream *stream;
	u8 data[sizeof(struct oz_elt) + OZ_MAX_ELT_PAYLOAD];
	int length;
	unsigned magic;
};
/* Flags values */
#define OZ_EI_F_MARKED		0x1

struct oz_elt_buf {
	spinlock_t lock;
	struct list_head stream_list;
	struct list_head order_list;
	struct list_head isoc_list;
	struct list_head *elt_pool;
	int free_elts;
	int max_free_elts;
	u8 tx_seq_num[OZ_NB_APPS];
};

int oz_elt_buf_init(struct oz_elt_buf *buf);
void oz_elt_buf_term(struct oz_elt_buf *buf);
struct oz_elt_info *oz_elt_info_alloc(struct oz_elt_buf *buf);
void oz_elt_info_free(struct oz_elt_buf *buf, struct oz_elt_info *ei);
void oz_elt_info_free_chain(struct oz_elt_buf *buf, struct list_head *list);
int oz_elt_stream_create(struct oz_elt_buf *buf, u8 id, int max_buf_count);
int oz_elt_stream_delete(struct oz_elt_buf *buf, u8 id);
void oz_elt_stream_get(struct oz_elt_stream *st);
void oz_elt_stream_put(struct oz_elt_stream *st);
int oz_queue_elt_info(struct oz_elt_buf *buf, u8 isoc, u8 id,
		struct oz_elt_info *ei);
int oz_select_elts_for_tx(struct oz_elt_buf *buf, u8 isoc, unsigned *len,
		unsigned max_len, struct list_head *list);
int oz_are_elts_available(struct oz_elt_buf *buf);
void oz_trim_elt_pool(struct oz_elt_buf *buf);

#endif /* _OZELTBUF_H */

