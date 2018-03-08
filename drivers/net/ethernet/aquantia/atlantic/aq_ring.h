/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File aq_ring.h: Declaration of functions for Rx/Tx rings. */

#ifndef AQ_RING_H
#define AQ_RING_H

#include "aq_common.h"

struct page;
struct aq_nic_cfg_s;

/*           TxC       SOP        DX         EOP
 *         +----------+----------+----------+-----------
 *   8bytes|len l3,l4 | pa       | pa       | pa
 *         +----------+----------+----------+-----------
 * 4/8bytes|len pkt   |len pkt   |          | skb
 *         +----------+----------+----------+-----------
 * 4/8bytes|is_txc    |len,flags |len       |len,is_eop
 *         +----------+----------+----------+-----------
 *
 *  This aq_ring_buff_s doesn't have endianness dependency.
 *  It is __packed for cache line optimizations.
 */
struct __packed aq_ring_buff_s {
	union {
		/* RX */
		struct {
			u32 rss_hash;
			u16 next;
			u8 is_hash_l4;
			u8 rsvd1;
			struct page *page;
		};
		/* EOP */
		struct {
			dma_addr_t pa_eop;
			struct sk_buff *skb;
		};
		/* DX */
		struct {
			dma_addr_t pa;
		};
		/* SOP */
		struct {
			dma_addr_t pa_sop;
			u32 len_pkt_sop;
		};
		/* TxC */
		struct {
			u32 mss;
			u8 len_l2;
			u8 len_l3;
			u8 len_l4;
			u8 is_ipv6:1;
			u8 rsvd2:7;
			u32 len_pkt;
		};
	};
	union {
		struct {
			u16 len;
			u32 is_ip_cso:1;
			u32 is_udp_cso:1;
			u32 is_tcp_cso:1;
			u32 is_cso_err:1;
			u32 is_sop:1;
			u32 is_eop:1;
			u32 is_txc:1;
			u32 is_mapped:1;
			u32 is_cleaned:1;
			u32 is_error:1;
			u32 rsvd3:6;
			u16 eop_index;
			u16 rsvd4;
		};
		u64 flags;
	};
};

struct aq_ring_stats_rx_s {
	u64 errors;
	u64 packets;
	u64 bytes;
	u64 lro_packets;
	u64 jumbo_packets;
};

struct aq_ring_stats_tx_s {
	u64 errors;
	u64 packets;
	u64 bytes;
	u64 queue_restarts;
};

union aq_ring_stats_s {
	struct aq_ring_stats_rx_s rx;
	struct aq_ring_stats_tx_s tx;
};

struct aq_ring_s {
	struct aq_ring_buff_s *buff_ring;
	u8 *dx_ring;		/* descriptors ring, dma shared mem */
	struct aq_nic_s *aq_nic;
	unsigned int idx;	/* for HW layer registers operations */
	unsigned int hw_head;
	unsigned int sw_head;
	unsigned int sw_tail;
	unsigned int size;	/* descriptors number */
	unsigned int dx_size;	/* TX or RX descriptor size,  */
				/* stored here for fater math */
	union aq_ring_stats_s stats;
	dma_addr_t dx_ring_pa;
};

struct aq_ring_param_s {
	unsigned int vec_idx;
	unsigned int cpu;
	cpumask_t affinity_mask;
};

static inline unsigned int aq_ring_next_dx(struct aq_ring_s *self,
					   unsigned int dx)
{
	return (++dx >= self->size) ? 0U : dx;
}

static inline unsigned int aq_ring_avail_dx(struct aq_ring_s *self)
{
	return (((self->sw_tail >= self->sw_head)) ?
		(self->size - 1) - self->sw_tail + self->sw_head :
		self->sw_head - self->sw_tail - 1);
}

struct aq_ring_s *aq_ring_tx_alloc(struct aq_ring_s *self,
				   struct aq_nic_s *aq_nic,
				   unsigned int idx,
				   struct aq_nic_cfg_s *aq_nic_cfg);
struct aq_ring_s *aq_ring_rx_alloc(struct aq_ring_s *self,
				   struct aq_nic_s *aq_nic,
				   unsigned int idx,
				   struct aq_nic_cfg_s *aq_nic_cfg);
int aq_ring_init(struct aq_ring_s *self);
void aq_ring_rx_deinit(struct aq_ring_s *self);
void aq_ring_free(struct aq_ring_s *self);
void aq_ring_update_queue_state(struct aq_ring_s *ring);
void aq_ring_queue_wake(struct aq_ring_s *ring);
void aq_ring_queue_stop(struct aq_ring_s *ring);
void aq_ring_tx_clean(struct aq_ring_s *self);
int aq_ring_rx_clean(struct aq_ring_s *self,
		     struct napi_struct *napi,
		     int *work_done,
		     int budget);
int aq_ring_rx_fill(struct aq_ring_s *self);

#endif /* AQ_RING_H */
