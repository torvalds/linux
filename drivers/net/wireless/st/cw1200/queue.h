/*
 * O(1) TX queue with built-in allocator for ST-Ericsson CW1200 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CW1200_QUEUE_H_INCLUDED
#define CW1200_QUEUE_H_INCLUDED

/* private */ struct cw1200_queue_item;

/* extern */ struct sk_buff;
/* extern */ struct wsm_tx;
/* extern */ struct cw1200_common;
/* extern */ struct ieee80211_tx_queue_stats;
/* extern */ struct cw1200_txpriv;

/* forward */ struct cw1200_queue_stats;

typedef void (*cw1200_queue_skb_dtor_t)(struct cw1200_common *priv,
					struct sk_buff *skb,
					const struct cw1200_txpriv *txpriv);

struct cw1200_queue {
	struct cw1200_queue_stats *stats;
	size_t			capacity;
	size_t			num_queued;
	size_t			num_pending;
	size_t			num_sent;
	struct cw1200_queue_item *pool;
	struct list_head	queue;
	struct list_head	free_pool;
	struct list_head	pending;
	int			tx_locked_cnt;
	int			*link_map_cache;
	bool			overfull;
	spinlock_t		lock; /* Protect queue entry */
	u8			queue_id;
	u8			generation;
	struct timer_list	gc;
	unsigned long		ttl;
};

struct cw1200_queue_stats {
	spinlock_t		lock; /* Protect stats entry */
	int			*link_map_cache;
	int			num_queued;
	size_t			map_capacity;
	wait_queue_head_t	wait_link_id_empty;
	cw1200_queue_skb_dtor_t	skb_dtor;
	struct cw1200_common	*priv;
};

struct cw1200_txpriv {
	u8 link_id;
	u8 raw_link_id;
	u8 tid;
	u8 rate_id;
	u8 offset;
};

int cw1200_queue_stats_init(struct cw1200_queue_stats *stats,
			    size_t map_capacity,
			    cw1200_queue_skb_dtor_t skb_dtor,
			    struct cw1200_common *priv);
int cw1200_queue_init(struct cw1200_queue *queue,
		      struct cw1200_queue_stats *stats,
		      u8 queue_id,
		      size_t capacity,
		      unsigned long ttl);
int cw1200_queue_clear(struct cw1200_queue *queue);
void cw1200_queue_stats_deinit(struct cw1200_queue_stats *stats);
void cw1200_queue_deinit(struct cw1200_queue *queue);

size_t cw1200_queue_get_num_queued(struct cw1200_queue *queue,
				   u32 link_id_map);
int cw1200_queue_put(struct cw1200_queue *queue,
		     struct sk_buff *skb,
		     struct cw1200_txpriv *txpriv);
int cw1200_queue_get(struct cw1200_queue *queue,
		     u32 link_id_map,
		     struct wsm_tx **tx,
		     struct ieee80211_tx_info **tx_info,
		     const struct cw1200_txpriv **txpriv);
int cw1200_queue_requeue(struct cw1200_queue *queue, u32 packet_id);
int cw1200_queue_requeue_all(struct cw1200_queue *queue);
int cw1200_queue_remove(struct cw1200_queue *queue,
			u32 packet_id);
int cw1200_queue_get_skb(struct cw1200_queue *queue, u32 packet_id,
			 struct sk_buff **skb,
			 const struct cw1200_txpriv **txpriv);
void cw1200_queue_lock(struct cw1200_queue *queue);
void cw1200_queue_unlock(struct cw1200_queue *queue);
bool cw1200_queue_get_xmit_timestamp(struct cw1200_queue *queue,
				     unsigned long *timestamp,
				     u32 pending_frame_id);

bool cw1200_queue_stats_is_empty(struct cw1200_queue_stats *stats,
				 u32 link_id_map);

static inline u8 cw1200_queue_get_queue_id(u32 packet_id)
{
	return (packet_id >> 16) & 0xFF;
}

static inline u8 cw1200_queue_get_generation(u32 packet_id)
{
	return (packet_id >>  8) & 0xFF;
}

#endif /* CW1200_QUEUE_H_INCLUDED */
