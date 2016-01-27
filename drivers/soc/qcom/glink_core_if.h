/* Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _SOC_QCOM_GLINK_CORE_IF_H_
#define _SOC_QCOM_GLINK_CORE_IF_H_

#include <linux/of.h>
#include <linux/types.h>
#include "glink_private.h"

/* Local Channel state */
enum local_channel_state_e {
	GLINK_CHANNEL_CLOSED = 0,
	GLINK_CHANNEL_OPENING,
	GLINK_CHANNEL_OPENED,
	GLINK_CHANNEL_CLOSING,
};

/* Transport Negotiation State */
enum transport_state_e {
	GLINK_XPRT_DOWN,
	GLINK_XPRT_NEGOTIATING,
	GLINK_XPRT_OPENED,
	GLINK_XPRT_FAILED,
};

struct channel_ctx;
struct glink_core_xprt_ctx;
struct glink_transport_if;
struct glink_core_version;

/**
 * struct glink_core_version - Individual version element
 *
 * version:	supported version
 * features:	all supported features for version
 */
struct glink_core_version {
	uint32_t version;
	uint32_t features;

	uint32_t (*negotiate_features)(struct glink_transport_if *if_ptr,
			const struct glink_core_version *version_ptr,
			uint32_t features);
};

/**
 * RX intent
 *
 * data:	pointer to the data (may be NULL for zero-copy)
 * id:		remote or local intent ID
 * pkt_size:	total size of packet
 * write_offset: next write offset (initially 0)
 * intent_size:	size of the original intent (do not modify)
 * tracer_pkt:	Flag to indicate if the data is a tracer packet
 * iovec:	Pointer to vector buffer if the transport passes a vector buffer
 * vprovider:	Virtual address-space buffer provider for a vector buffer
 * pprovider:	Physical address-space buffer provider for a vector buffer
 * pkt_priv:	G-Link core owned packet-private data
 * list:	G-Link core owned list node
 * bounce_buf:	Pointer to the temporary/internal bounce buffer
 */
struct glink_core_rx_intent {
	void *data;
	uint32_t id;
	size_t pkt_size;
	size_t write_offset;
	size_t intent_size;
	bool tracer_pkt;
	void *iovec;
	void * (*vprovider)(void *iovec, size_t offset, size_t *size);
	void * (*pprovider)(void *iovec, size_t offset, size_t *size);

	/* G-Link-Core-owned elements - please ignore */
	struct list_head list;
	const void *pkt_priv;
	void *bounce_buf;
};

/**
 * struct glink_core_flow_info - Flow specific Information
 * @mtu_tx_time_us:	Time to transmit an MTU in microseconds.
 * @power_state:	Power state associated with the traffic flow.
 */
struct glink_core_flow_info {
	unsigned long mtu_tx_time_us;
	uint32_t power_state;
};

/**
 * struct glink_core_transport_cfg - configuration of a new transport
 * @name:		Name of the transport.
 * @edge:		Subsystem the transport connects to.
 * @versions:		Array of transport versions supported.
 * @versions_entries:	Number of entries in @versions.
 * @max_cid:		Maximum number of channel identifiers supported.
 * @max_iid:		Maximum number of intent identifiers supported.
 * @mtu:		MTU supported by this transport.
 * @num_flows:		Number of traffic flows/priority buckets.
 * @flow_info:		Information about each flow/priority.
 * @token_count:	Number of tokens per assignment.
 */
struct glink_core_transport_cfg {
	const char *name;
	const char *edge;
	const struct glink_core_version *versions;
	size_t versions_entries;
	uint32_t max_cid;
	uint32_t max_iid;

	size_t mtu;
	uint32_t num_flows;
	struct glink_core_flow_info *flow_info;
	uint32_t token_count;
};

struct glink_core_if {
	/* Negotiation */
	void (*link_up)(struct glink_transport_if *if_ptr);
	void (*link_down)(struct glink_transport_if *if_ptr);
	void (*rx_cmd_version)(struct glink_transport_if *if_ptr,
			uint32_t version,
			uint32_t features);
	void (*rx_cmd_version_ack)(struct glink_transport_if *if_ptr,
			uint32_t version,
			uint32_t features);

	/* channel management */
	void (*rx_cmd_ch_remote_open)(struct glink_transport_if *if_ptr,
			uint32_t rcid, const char *name, uint16_t req_xprt);
	void (*rx_cmd_ch_open_ack)(struct glink_transport_if *if_ptr,
			uint32_t lcid, uint16_t xprt_resp);
	void (*rx_cmd_ch_remote_close)(struct glink_transport_if *if_ptr,
			uint32_t rcid);
	void (*rx_cmd_ch_close_ack)(struct glink_transport_if *if_ptr,
			uint32_t lcid);

	/* channel data */
	struct glink_core_rx_intent * (*rx_get_pkt_ctx)(
			struct glink_transport_if *if_ptr,
			uint32_t rcid, uint32_t liid);
	void (*rx_put_pkt_ctx)(struct glink_transport_if *if_ptr, uint32_t rcid,
			struct glink_core_rx_intent *intent_ptr, bool complete);
	void (*rx_cmd_remote_rx_intent_put)(struct glink_transport_if *if_ptr,
			uint32_t rcid, uint32_t riid, size_t size);
	void (*rx_cmd_tx_done)(struct glink_transport_if *if_ptr, uint32_t rcid,
			uint32_t riid, bool reuse);
	void (*rx_cmd_remote_rx_intent_req)(struct glink_transport_if *if_ptr,
			uint32_t rcid, size_t size);
	void (*rx_cmd_rx_intent_req_ack)(struct glink_transport_if *if_ptr,
			uint32_t rcid, bool granted);
	void (*rx_cmd_remote_sigs)(struct glink_transport_if *if_ptr,
			uint32_t rcid, uint32_t sigs);

	/* channel scheduling */
	void (*tx_resume)(struct glink_transport_if *if_ptr);
};

int glink_core_register_transport(struct glink_transport_if *if_ptr,
		struct glink_core_transport_cfg *cfg);

void glink_core_unregister_transport(struct glink_transport_if *if_ptr);

/**
 * of_get_glink_core_qos_cfg() - Parse the qos related dt entries
 * @phandle:	The handle to the qos related node in DT.
 * @cfg:	The transport configuration to be filled.
 *
 * Return: 0 on Success, standard Linux error otherwise.
 */
int of_get_glink_core_qos_cfg(struct device_node *phandle,
				struct glink_core_transport_cfg *cfg);

/**
 * rx_linear_vbuf_provider() - Virtual Buffer Provider for linear buffers
 * iovec:	Pointer to the beginning of the linear buffer.
 * offset:	Offset into the buffer whose address is needed.
 * size:	Pointer to hold the length of the contiguous buffer space.
 *
 * This function is used when a linear buffer is received while the client has
 * registered to receive vector buffers.
 *
 * Return: Address of the buffer which is at offset "offset" from the beginning
 *         of the buffer.
 */
static inline void *rx_linear_vbuf_provider(void *iovec, size_t offset,
					    size_t *size)
{
	struct glink_core_rx_intent *rx_info =
		(struct glink_core_rx_intent *)iovec;

	if (unlikely(!iovec || !size))
		return NULL;

	if (unlikely(offset >= rx_info->pkt_size))
		return NULL;

	if (unlikely(OVERFLOW_ADD_UNSIGNED(void *, rx_info->data, offset)))
		return NULL;

	*size = rx_info->pkt_size - offset;
	return rx_info->data + offset;
}

#endif /* _SOC_QCOM_GLINK_CORE_IF_H_ */
