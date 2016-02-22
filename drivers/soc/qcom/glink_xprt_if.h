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
#ifndef _SOC_QCOM_GLINK_XPRT_IF_H_
#define _SOC_QCOM_GLINK_XPRT_IF_H_

#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/types.h>

struct glink_core_xprt_ctx;
struct glink_core_if;
struct channel_ctx;
struct glink_core_rx_intent;

enum buf_type {
	LINEAR = 0,
	VECTOR,
};

enum xprt_ids {
	SMEM_XPRT_ID = 100,
	SMD_TRANS_XPRT_ID = 200,
	LLOOP_XPRT_ID = 300,
	MOCK_XPRT_HIGH_ID = 390,
	MOCK_XPRT_ID = 400,
	MOCK_XPRT_LOW_ID = 410,
};

#define GCAP_SIGNALS		BIT(0)
#define GCAP_INTENTLESS		BIT(1)
#define GCAP_TRACER_PKT		BIT(2)
#define GCAP_AUTO_QUEUE_RX_INT	BIT(3)

/**
 * struct glink_core_tx_pkt - Transmit Packet information
 * @list_done:		Index to the channel's transmit queue.
 * @list_done:		Index to the channel's acknowledgment queue.
 * @pkt_priv:		Private information specific to the packet.
 * @data:		Pointer to the buffer containing the data.
 * @riid:		Remote receive intent used to transmit the packet.
 * @rcid:		Remote channel receiving the packet.
 * @size:		Total size of the data in the packet.
 * @tx_len:		Data length to transmit in the current transmit slot.
 * @size_remaining:	Remaining size of the data in the packet.
 * @intent_size:	Receive intent size queued by the remote side.
 * @tracer_pkt:		Flag to indicate if the packet is a tracer packet.
 * @iovec:		Pointer to the vector buffer packet.
 * @vprovider:		Packet-specific virtual buffer provider function.
 * @pprovider:		Packet-specific physical buffer provider function.
 * @pkt_ref:		Active references to the packet.
 */
struct glink_core_tx_pkt {
	struct list_head list_node;
	struct list_head list_done;
	const void *pkt_priv;
	const void *data;
	uint32_t riid;
	uint32_t rcid;
	uint32_t size;
	uint32_t tx_len;
	uint32_t size_remaining;
	size_t intent_size;
	bool tracer_pkt;
	void *iovec;
	void * (*vprovider)(void *iovec, size_t offset, size_t *size);
	void * (*pprovider)(void *iovec, size_t offset, size_t *size);
	struct rwref_lock pkt_ref;
};

/**
 * Note - each call to register the interface must pass a unique
 * instance of this data.
 */
struct glink_transport_if {
	/* Negotiation */
	void (*tx_cmd_version)(struct glink_transport_if *if_ptr,
			uint32_t version,
			uint32_t features);
	void (*tx_cmd_version_ack)(struct glink_transport_if *if_ptr,
			uint32_t version,
			uint32_t features);
	uint32_t (*set_version)(struct glink_transport_if *if_ptr,
			uint32_t version,
			uint32_t features);

	/* channel state */
	int (*tx_cmd_ch_open)(struct glink_transport_if *if_ptr, uint32_t lcid,
			const char *name, uint16_t req_xprt);
	int (*tx_cmd_ch_close)(struct glink_transport_if *if_ptr,
			uint32_t lcid);
	void (*tx_cmd_ch_remote_open_ack)(struct glink_transport_if *if_ptr,
			uint32_t rcid, uint16_t xprt_resp);
	void (*tx_cmd_ch_remote_close_ack)(struct glink_transport_if *if_ptr,
			uint32_t rcid);
	int (*ssr)(struct glink_transport_if *if_ptr);

	/* channel data */
	int (*allocate_rx_intent)(struct glink_transport_if *if_ptr,
				  size_t size,
				  struct glink_core_rx_intent *intent);
	int (*deallocate_rx_intent)(struct glink_transport_if *if_ptr,
				    struct glink_core_rx_intent *intent);
	/* Optional */
	int (*reuse_rx_intent)(struct glink_transport_if *if_ptr,
			       struct glink_core_rx_intent *intent);

	int (*tx_cmd_local_rx_intent)(struct glink_transport_if *if_ptr,
			uint32_t lcid, size_t size, uint32_t liid);
	void (*tx_cmd_local_rx_done)(struct glink_transport_if *if_ptr,
			uint32_t lcid, uint32_t liid, bool reuse);
	int (*tx)(struct glink_transport_if *if_ptr, uint32_t lcid,
			struct glink_core_tx_pkt *pctx);
	int (*tx_cmd_rx_intent_req)(struct glink_transport_if *if_ptr,
			uint32_t lcid, size_t size);
	int (*tx_cmd_remote_rx_intent_req_ack)(
			struct glink_transport_if *if_ptr,
			uint32_t lcid, bool granted);
	int (*tx_cmd_set_sigs)(struct glink_transport_if *if_ptr,
			uint32_t lcid, uint32_t sigs);

	/* Optional.  If NULL at xprt registration, dummies will be used */
	int (*poll)(struct glink_transport_if *if_ptr, uint32_t lcid);
	int (*mask_rx_irq)(struct glink_transport_if *if_ptr, uint32_t lcid,
			bool mask, void *pstruct);
	int (*wait_link_down)(struct glink_transport_if *if_ptr);
	int (*tx_cmd_tracer_pkt)(struct glink_transport_if *if_ptr,
			uint32_t lcid, struct glink_core_tx_pkt *pctx);
	unsigned long (*get_power_vote_ramp_time)(
			struct glink_transport_if *if_ptr, uint32_t state);
	int (*power_vote)(struct glink_transport_if *if_ptr, uint32_t state);
	int (*power_unvote)(struct glink_transport_if *if_ptr);
	/*
	 * Keep data pointers at the end of the structure after all function
	 * pointer to allow for in-place initialization.
	 */

	/* private pointer for core */
	struct glink_core_xprt_ctx *glink_core_priv;

	/* core pointer (set during transport registration) */
	struct glink_core_if *glink_core_if_ptr;
};

#ifdef CONFIG_MSM_GLINK

/**
 * get_tx_vaddr() - Get the virtual address from which the tx has to be done
 * @pctx:	transmit packet context.
 * @offset:	offset into the packet.
 * @tx_size:	pointer to hold the length of the contiguous buffer
 *              space.
 *
 * Return:	Address from which the tx has to be done.
 */
static inline void *get_tx_vaddr(struct glink_core_tx_pkt *pctx, size_t offset,
				 size_t *tx_size)
{
	void *pdata;

	if (pctx->vprovider) {
		return pctx->vprovider((void *)pctx->iovec, offset, tx_size);
	} else if (pctx->pprovider) {
		pdata = pctx->pprovider((void *)pctx->iovec, offset, tx_size);
		return phys_to_virt((unsigned long)pdata);
	}
	return NULL;
}

/**
 * glink_xprt_name_to_id() - convert transport name to id
 * @name:	Name of the transport.
 * @id:		Assigned id.
 *
 * Return: 0 on success or standard Linux error code.
 */
int glink_xprt_name_to_id(const char *name, uint16_t *id);


#else /* CONFIG_MSM_GLINK */
static inline void *get_tx_vaddr(struct glink_core_tx_pkt *pctx, size_t offset,
				 size_t *tx_size)
{
	return NULL;
}

static inline int glink_xprt_name_to_id(const char *name, uint16_t *id)
{
	return -ENODEV;
}

#endif /* CONFIG_MSM_GLINK */
#endif /* _SOC_QCOM_GLINK_XPRT_IF_H_ */
