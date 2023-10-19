/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2022 Linaro Ltd.
 */
#ifndef _IPA_ENDPOINT_H_
#define _IPA_ENDPOINT_H_

#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/if_ether.h>

#include "gsi.h"
#include "ipa_reg.h"

struct net_device;
struct sk_buff;

struct ipa;
struct ipa_gsi_endpoint_data;

/* Non-zero granularity of counter used to implement aggregation timeout */
#define IPA_AGGR_GRANULARITY		500	/* microseconds */

#define IPA_MTU			ETH_DATA_LEN

enum ipa_endpoint_name {
	IPA_ENDPOINT_AP_COMMAND_TX,
	IPA_ENDPOINT_AP_LAN_RX,
	IPA_ENDPOINT_AP_MODEM_TX,
	IPA_ENDPOINT_AP_MODEM_RX,
	IPA_ENDPOINT_MODEM_COMMAND_TX,
	IPA_ENDPOINT_MODEM_LAN_TX,
	IPA_ENDPOINT_MODEM_LAN_RX,
	IPA_ENDPOINT_MODEM_AP_TX,
	IPA_ENDPOINT_MODEM_AP_RX,
	IPA_ENDPOINT_MODEM_DL_NLO_TX,
	IPA_ENDPOINT_COUNT,	/* Number of names (not an index) */
};

#define IPA_ENDPOINT_MAX		32	/* Max supported by driver */

/**
 * struct ipa_endpoint_tx - Endpoint configuration for TX endpoints
 * @seq_type:		primary packet processing sequencer type
 * @seq_rep_type:	sequencer type for replication processing
 * @status_endpoint:	endpoint to which status elements are sent
 *
 * The @status_endpoint is only valid if the endpoint's @status_enable
 * flag is set.
 */
struct ipa_endpoint_tx {
	enum ipa_seq_type seq_type;
	enum ipa_seq_rep_type seq_rep_type;
	enum ipa_endpoint_name status_endpoint;
};

/**
 * struct ipa_endpoint_rx - Endpoint configuration for RX endpoints
 * @buffer_size:	requested receive buffer size (bytes)
 * @pad_align:		power-of-2 boundary to which packet payload is aligned
 * @aggr_time_limit:	time before aggregation closes (microseconds)
 * @aggr_hard_limit:	whether aggregation closes before or after boundary
 * @aggr_close_eof:	whether aggregation closes on end-of-frame
 * @holb_drop:		whether to drop packets to avoid head-of-line blocking
 *
 * The actual size of the receive buffer is rounded up if necessary
 * to be a power-of-2 number of pages.
 *
 * With each packet it transfers, the IPA hardware can perform certain
 * transformations of its packet data.  One of these is adding pad bytes
 * to the end of the packet data so the result ends on a power-of-2 boundary.
 *
 * It is also able to aggregate multiple packets into a single receive buffer.
 * Aggregation is "open" while a buffer is being filled, and "closes" when
 * certain criteria are met.
 *
 * A time limit can be specified to close aggregation.  Aggregation will be
 * closed if this period passes after data is first written into a receive
 * buffer.  If not specified, no time limit is imposed.
 *
 * Insufficient space available in the receive buffer can close aggregation.
 * The aggregation byte limit defines the point (in units of 1024 bytes) in
 * the buffer where aggregation closes.  With a "soft" aggregation limit,
 * aggregation closes when a packet written to the buffer *crosses* that
 * aggregation limit.  With a "hard" aggregation limit, aggregation will
 * close *before* writing a packet that would cross that boundary.
 */
struct ipa_endpoint_rx {
	u32 buffer_size;
	u32 pad_align;
	u32 aggr_time_limit;
	bool aggr_hard_limit;
	bool aggr_close_eof;
	bool holb_drop;
};

/**
 * struct ipa_endpoint_config - IPA endpoint hardware configuration
 * @resource_group:	resource group to assign endpoint to
 * @checksum:		whether checksum offload is enabled
 * @qmap:		whether endpoint uses QMAP protocol
 * @aggregation:	whether endpoint supports aggregation
 * @status_enable:	whether endpoint uses status elements
 * @dma_mode:		whether endpoint operates in DMA mode
 * @dma_endpoint:	peer endpoint, if operating in DMA mode
 * @tx:			TX-specific endpoint information (see above)
 * @rx:			RX-specific endpoint information (see above)
 */
struct ipa_endpoint_config {
	u32 resource_group;
	bool checksum;
	bool qmap;
	bool aggregation;
	bool status_enable;
	bool dma_mode;
	enum ipa_endpoint_name dma_endpoint;
	union {
		struct ipa_endpoint_tx tx;
		struct ipa_endpoint_rx rx;
	};
};

/**
 * enum ipa_replenish_flag:	RX buffer replenish flags
 *
 * @IPA_REPLENISH_ENABLED:	Whether receive buffer replenishing is enabled
 * @IPA_REPLENISH_ACTIVE:	Whether replenishing is underway
 * @IPA_REPLENISH_COUNT:	Number of defined replenish flags
 */
enum ipa_replenish_flag {
	IPA_REPLENISH_ENABLED,
	IPA_REPLENISH_ACTIVE,
	IPA_REPLENISH_COUNT,	/* Number of flags (must be last) */
};

/**
 * struct ipa_endpoint - IPA endpoint information
 * @ipa:		IPA pointer
 * @ee_id:		Execution environmnent endpoint is associated with
 * @channel_id:		GSI channel used by the endpoint
 * @endpoint_id:	IPA endpoint number
 * @toward_ipa:		Endpoint direction (true = TX, false = RX)
 * @config:		Default endpoint configuration
 * @skb_frag_max:	Maximum allowed number of TX SKB fragments
 * @evt_ring_id:	GSI event ring used by the endpoint
 * @netdev:		Network device pointer, if endpoint uses one
 * @replenish_flags:	Replenishing state flags
 * @replenish_count:	Total number of replenish transactions committed
 * @replenish_work:	Work item used for repeated replenish failures
 */
struct ipa_endpoint {
	struct ipa *ipa;
	enum gsi_ee_id ee_id;
	u32 channel_id;
	u32 endpoint_id;
	bool toward_ipa;
	struct ipa_endpoint_config config;

	u32 skb_frag_max;	/* Used for netdev TX only */
	u32 evt_ring_id;

	/* Net device this endpoint is associated with, if any */
	struct net_device *netdev;

	/* Receive buffer replenishing for RX endpoints */
	DECLARE_BITMAP(replenish_flags, IPA_REPLENISH_COUNT);
	u64 replenish_count;
	struct delayed_work replenish_work;		/* global wq */
};

void ipa_endpoint_modem_hol_block_clear_all(struct ipa *ipa);

void ipa_endpoint_modem_pause_all(struct ipa *ipa, bool enable);

int ipa_endpoint_modem_exception_reset_all(struct ipa *ipa);

int ipa_endpoint_skb_tx(struct ipa_endpoint *endpoint, struct sk_buff *skb);

int ipa_endpoint_enable_one(struct ipa_endpoint *endpoint);
void ipa_endpoint_disable_one(struct ipa_endpoint *endpoint);

void ipa_endpoint_suspend_one(struct ipa_endpoint *endpoint);
void ipa_endpoint_resume_one(struct ipa_endpoint *endpoint);

void ipa_endpoint_suspend(struct ipa *ipa);
void ipa_endpoint_resume(struct ipa *ipa);

void ipa_endpoint_setup(struct ipa *ipa);
void ipa_endpoint_teardown(struct ipa *ipa);

int ipa_endpoint_config(struct ipa *ipa);
void ipa_endpoint_deconfig(struct ipa *ipa);

void ipa_endpoint_default_route_set(struct ipa *ipa, u32 endpoint_id);
void ipa_endpoint_default_route_clear(struct ipa *ipa);

u32 ipa_endpoint_init(struct ipa *ipa, u32 count,
		      const struct ipa_gsi_endpoint_data *data);
void ipa_endpoint_exit(struct ipa *ipa);

void ipa_endpoint_trans_complete(struct ipa_endpoint *ipa,
				 struct gsi_trans *trans);
void ipa_endpoint_trans_release(struct ipa_endpoint *ipa,
				struct gsi_trans *trans);

#endif /* _IPA_ENDPOINT_H_ */
