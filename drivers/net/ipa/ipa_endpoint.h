/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
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
 * @data:		Endpoint configuration data
 * @trans_tre_max:	Maximum number of TRE descriptors per transaction
 * @evt_ring_id:	GSI event ring used by the endpoint
 * @netdev:		Network device pointer, if endpoint uses one
 * @replenish_flags:	Replenishing state flags
 * @replenish_ready:	Number of replenish transactions without doorbell
 * @replenish_backlog:	Number of buffers needed to fill hardware queue
 * @replenish_work:	Work item used for repeated replenish failures
 */
struct ipa_endpoint {
	struct ipa *ipa;
	enum gsi_ee_id ee_id;
	u32 channel_id;
	u32 endpoint_id;
	bool toward_ipa;
	const struct ipa_endpoint_config_data *data;

	u32 trans_tre_max;
	u32 evt_ring_id;

	/* Net device this endpoint is associated with, if any */
	struct net_device *netdev;

	/* Receive buffer replenishing for RX endpoints */
	DECLARE_BITMAP(replenish_flags, IPA_REPLENISH_COUNT);
	u32 replenish_ready;
	atomic_t replenish_backlog;
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
