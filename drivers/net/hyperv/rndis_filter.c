// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 */
#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/nls.h>
#include <linux/vmalloc.h>
#include <linux/rtnetlink.h>
#include <linux/ucs2_string.h>
#include <linux/string.h>

#include "hyperv_net.h"
#include "netvsc_trace.h"

static void rndis_set_multicast(struct work_struct *w);

#define RNDIS_EXT_LEN HV_HYP_PAGE_SIZE
struct rndis_request {
	struct list_head list_ent;
	struct completion  wait_event;

	struct rndis_message response_msg;
	/*
	 * The buffer for extended info after the RNDIS response message. It's
	 * referenced based on the data offset in the RNDIS message. Its size
	 * is enough for current needs, and should be sufficient for the near
	 * future.
	 */
	u8 response_ext[RNDIS_EXT_LEN];

	/* Simplify allocation by having a netvsc packet inline */
	struct hv_netvsc_packet	pkt;

	struct rndis_message request_msg;
	/*
	 * The buffer for the extended info after the RNDIS request message.
	 * It is referenced and sized in a similar way as response_ext.
	 */
	u8 request_ext[RNDIS_EXT_LEN];
};

static const u8 netvsc_hash_key[NETVSC_HASH_KEYLEN] = {
	0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
	0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
	0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
	0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
	0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa
};

static struct rndis_device *get_rndis_device(void)
{
	struct rndis_device *device;

	device = kzalloc(sizeof(struct rndis_device), GFP_KERNEL);
	if (!device)
		return NULL;

	spin_lock_init(&device->request_lock);

	INIT_LIST_HEAD(&device->req_list);
	INIT_WORK(&device->mcast_work, rndis_set_multicast);

	device->state = RNDIS_DEV_UNINITIALIZED;

	return device;
}

static struct rndis_request *get_rndis_request(struct rndis_device *dev,
					     u32 msg_type,
					     u32 msg_len)
{
	struct rndis_request *request;
	struct rndis_message *rndis_msg;
	struct rndis_set_request *set;
	unsigned long flags;

	request = kzalloc(sizeof(struct rndis_request), GFP_KERNEL);
	if (!request)
		return NULL;

	init_completion(&request->wait_event);

	rndis_msg = &request->request_msg;
	rndis_msg->ndis_msg_type = msg_type;
	rndis_msg->msg_len = msg_len;

	request->pkt.q_idx = 0;

	/*
	 * Set the request id. This field is always after the rndis header for
	 * request/response packet types so we just used the SetRequest as a
	 * template
	 */
	set = &rndis_msg->msg.set_req;
	set->req_id = atomic_inc_return(&dev->new_req_id);

	/* Add to the request list */
	spin_lock_irqsave(&dev->request_lock, flags);
	list_add_tail(&request->list_ent, &dev->req_list);
	spin_unlock_irqrestore(&dev->request_lock, flags);

	return request;
}

static void put_rndis_request(struct rndis_device *dev,
			    struct rndis_request *req)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->request_lock, flags);
	list_del(&req->list_ent);
	spin_unlock_irqrestore(&dev->request_lock, flags);

	kfree(req);
}

static void dump_rndis_message(struct net_device *netdev,
			       const struct rndis_message *rndis_msg,
			       const void *data)
{
	switch (rndis_msg->ndis_msg_type) {
	case RNDIS_MSG_PACKET:
		if (rndis_msg->msg_len - RNDIS_HEADER_SIZE >= sizeof(struct rndis_packet)) {
			const struct rndis_packet *pkt = data + RNDIS_HEADER_SIZE;
			netdev_dbg(netdev, "RNDIS_MSG_PACKET (len %u, "
				   "data offset %u data len %u, # oob %u, "
				   "oob offset %u, oob len %u, pkt offset %u, "
				   "pkt len %u\n",
				   rndis_msg->msg_len,
				   pkt->data_offset,
				   pkt->data_len,
				   pkt->num_oob_data_elements,
				   pkt->oob_data_offset,
				   pkt->oob_data_len,
				   pkt->per_pkt_info_offset,
				   pkt->per_pkt_info_len);
		}
		break;

	case RNDIS_MSG_INIT_C:
		if (rndis_msg->msg_len - RNDIS_HEADER_SIZE >=
				sizeof(struct rndis_initialize_complete)) {
			const struct rndis_initialize_complete *init_complete =
				data + RNDIS_HEADER_SIZE;
			netdev_dbg(netdev, "RNDIS_MSG_INIT_C "
				"(len %u, id 0x%x, status 0x%x, major %d, minor %d, "
				"device flags %d, max xfer size 0x%x, max pkts %u, "
				"pkt aligned %u)\n",
				rndis_msg->msg_len,
				init_complete->req_id,
				init_complete->status,
				init_complete->major_ver,
				init_complete->minor_ver,
				init_complete->dev_flags,
				init_complete->max_xfer_size,
				init_complete->max_pkt_per_msg,
				init_complete->pkt_alignment_factor);
		}
		break;

	case RNDIS_MSG_QUERY_C:
		if (rndis_msg->msg_len - RNDIS_HEADER_SIZE >=
				sizeof(struct rndis_query_complete)) {
			const struct rndis_query_complete *query_complete =
				data + RNDIS_HEADER_SIZE;
			netdev_dbg(netdev, "RNDIS_MSG_QUERY_C "
				"(len %u, id 0x%x, status 0x%x, buf len %u, "
				"buf offset %u)\n",
				rndis_msg->msg_len,
				query_complete->req_id,
				query_complete->status,
				query_complete->info_buflen,
				query_complete->info_buf_offset);
		}
		break;

	case RNDIS_MSG_SET_C:
		if (rndis_msg->msg_len - RNDIS_HEADER_SIZE + sizeof(struct rndis_set_complete)) {
			const struct rndis_set_complete *set_complete =
				data + RNDIS_HEADER_SIZE;
			netdev_dbg(netdev,
				"RNDIS_MSG_SET_C (len %u, id 0x%x, status 0x%x)\n",
				rndis_msg->msg_len,
				set_complete->req_id,
				set_complete->status);
		}
		break;

	case RNDIS_MSG_INDICATE:
		if (rndis_msg->msg_len - RNDIS_HEADER_SIZE >=
				sizeof(struct rndis_indicate_status)) {
			const struct rndis_indicate_status *indicate_status =
				data + RNDIS_HEADER_SIZE;
			netdev_dbg(netdev, "RNDIS_MSG_INDICATE "
				"(len %u, status 0x%x, buf len %u, buf offset %u)\n",
				rndis_msg->msg_len,
				indicate_status->status,
				indicate_status->status_buflen,
				indicate_status->status_buf_offset);
		}
		break;

	default:
		netdev_dbg(netdev, "0x%x (len %u)\n",
			rndis_msg->ndis_msg_type,
			rndis_msg->msg_len);
		break;
	}
}

static int rndis_filter_send_request(struct rndis_device *dev,
				  struct rndis_request *req)
{
	struct hv_netvsc_packet *packet;
	struct hv_page_buffer page_buf[2];
	struct hv_page_buffer *pb = page_buf;
	int ret;

	/* Setup the packet to send it */
	packet = &req->pkt;

	packet->total_data_buflen = req->request_msg.msg_len;
	packet->page_buf_cnt = 1;

	pb[0].pfn = virt_to_phys(&req->request_msg) >>
					HV_HYP_PAGE_SHIFT;
	pb[0].len = req->request_msg.msg_len;
	pb[0].offset = offset_in_hvpage(&req->request_msg);

	/* Add one page_buf when request_msg crossing page boundary */
	if (pb[0].offset + pb[0].len > HV_HYP_PAGE_SIZE) {
		packet->page_buf_cnt++;
		pb[0].len = HV_HYP_PAGE_SIZE -
			pb[0].offset;
		pb[1].pfn = virt_to_phys((void *)&req->request_msg
			+ pb[0].len) >> HV_HYP_PAGE_SHIFT;
		pb[1].offset = 0;
		pb[1].len = req->request_msg.msg_len -
			pb[0].len;
	}

	trace_rndis_send(dev->ndev, 0, &req->request_msg);

	rcu_read_lock_bh();
	ret = netvsc_send(dev->ndev, packet, NULL, pb, NULL, false);
	rcu_read_unlock_bh();

	return ret;
}

static void rndis_set_link_state(struct rndis_device *rdev,
				 struct rndis_request *request)
{
	u32 link_status;
	struct rndis_query_complete *query_complete;
	u32 msg_len = request->response_msg.msg_len;

	/* Ensure the packet is big enough to access its fields */
	if (msg_len - RNDIS_HEADER_SIZE < sizeof(struct rndis_query_complete))
		return;

	query_complete = &request->response_msg.msg.query_complete;

	if (query_complete->status == RNDIS_STATUS_SUCCESS &&
	    query_complete->info_buflen >= sizeof(u32) &&
	    query_complete->info_buf_offset >= sizeof(*query_complete) &&
	    msg_len - RNDIS_HEADER_SIZE >= query_complete->info_buf_offset &&
	    msg_len - RNDIS_HEADER_SIZE - query_complete->info_buf_offset
			>= query_complete->info_buflen) {
		memcpy(&link_status, (void *)((unsigned long)query_complete +
		       query_complete->info_buf_offset), sizeof(u32));
		rdev->link_state = link_status != 0;
	}
}

static void rndis_filter_receive_response(struct net_device *ndev,
					  struct netvsc_device *nvdev,
					  struct rndis_message *resp,
					  void *data)
{
	u32 *req_id = &resp->msg.init_complete.req_id;
	struct rndis_device *dev = nvdev->extension;
	struct rndis_request *request = NULL;
	bool found = false;
	unsigned long flags;

	/* This should never happen, it means control message
	 * response received after device removed.
	 */
	if (dev->state == RNDIS_DEV_UNINITIALIZED) {
		netdev_err(ndev,
			   "got rndis message uninitialized\n");
		return;
	}

	/* Ensure the packet is big enough to read req_id. Req_id is the 1st
	 * field in any request/response message, so the payload should have at
	 * least sizeof(u32) bytes
	 */
	if (resp->msg_len - RNDIS_HEADER_SIZE < sizeof(u32)) {
		netdev_err(ndev, "rndis msg_len too small: %u\n",
			   resp->msg_len);
		return;
	}

	/* Copy the request ID into nvchan->recv_buf */
	*req_id = *(u32 *)(data + RNDIS_HEADER_SIZE);

	spin_lock_irqsave(&dev->request_lock, flags);
	list_for_each_entry(request, &dev->req_list, list_ent) {
		/*
		 * All request/response message contains RequestId as the 1st
		 * field
		 */
		if (request->request_msg.msg.init_req.req_id == *req_id) {
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&dev->request_lock, flags);

	if (found) {
		if (resp->msg_len <=
		    sizeof(struct rndis_message) + RNDIS_EXT_LEN) {
			memcpy(&request->response_msg, resp, RNDIS_HEADER_SIZE + sizeof(*req_id));
			unsafe_memcpy((void *)&request->response_msg + RNDIS_HEADER_SIZE + sizeof(*req_id),
			       data + RNDIS_HEADER_SIZE + sizeof(*req_id),
			       resp->msg_len - RNDIS_HEADER_SIZE - sizeof(*req_id),
			       "request->response_msg is followed by a padding of RNDIS_EXT_LEN inside rndis_request");
			if (request->request_msg.ndis_msg_type ==
			    RNDIS_MSG_QUERY && request->request_msg.msg.
			    query_req.oid == RNDIS_OID_GEN_MEDIA_CONNECT_STATUS)
				rndis_set_link_state(dev, request);
		} else {
			netdev_err(ndev,
				"rndis response buffer overflow "
				"detected (size %u max %zu)\n",
				resp->msg_len,
				sizeof(struct rndis_message));

			if (resp->ndis_msg_type ==
			    RNDIS_MSG_RESET_C) {
				/* does not have a request id field */
				request->response_msg.msg.reset_complete.
					status = RNDIS_STATUS_BUFFER_OVERFLOW;
			} else {
				request->response_msg.msg.
				init_complete.status =
					RNDIS_STATUS_BUFFER_OVERFLOW;
			}
		}

		netvsc_dma_unmap(((struct net_device_context *)
			netdev_priv(ndev))->device_ctx, &request->pkt);
		complete(&request->wait_event);
	} else {
		netdev_err(ndev,
			"no rndis request found for this response "
			"(id 0x%x res type 0x%x)\n",
			*req_id,
			resp->ndis_msg_type);
	}
}

/*
 * Get the Per-Packet-Info with the specified type
 * return NULL if not found.
 */
static inline void *rndis_get_ppi(struct net_device *ndev,
				  struct rndis_packet *rpkt,
				  u32 rpkt_len, u32 type, u8 internal,
				  u32 ppi_size, void *data)
{
	struct rndis_per_packet_info *ppi;
	int len;

	if (rpkt->per_pkt_info_offset == 0)
		return NULL;

	/* Validate info_offset and info_len */
	if (rpkt->per_pkt_info_offset < sizeof(struct rndis_packet) ||
	    rpkt->per_pkt_info_offset > rpkt_len) {
		netdev_err(ndev, "Invalid per_pkt_info_offset: %u\n",
			   rpkt->per_pkt_info_offset);
		return NULL;
	}

	if (rpkt->per_pkt_info_len < sizeof(*ppi) ||
	    rpkt->per_pkt_info_len > rpkt_len - rpkt->per_pkt_info_offset) {
		netdev_err(ndev, "Invalid per_pkt_info_len: %u\n",
			   rpkt->per_pkt_info_len);
		return NULL;
	}

	ppi = (struct rndis_per_packet_info *)((ulong)rpkt +
		rpkt->per_pkt_info_offset);
	/* Copy the PPIs into nvchan->recv_buf */
	memcpy(ppi, data + RNDIS_HEADER_SIZE + rpkt->per_pkt_info_offset, rpkt->per_pkt_info_len);
	len = rpkt->per_pkt_info_len;

	while (len > 0) {
		/* Validate ppi_offset and ppi_size */
		if (ppi->size > len) {
			netdev_err(ndev, "Invalid ppi size: %u\n", ppi->size);
			continue;
		}

		if (ppi->ppi_offset >= ppi->size) {
			netdev_err(ndev, "Invalid ppi_offset: %u\n", ppi->ppi_offset);
			continue;
		}

		if (ppi->type == type && ppi->internal == internal) {
			/* ppi->size should be big enough to hold the returned object. */
			if (ppi->size - ppi->ppi_offset < ppi_size) {
				netdev_err(ndev, "Invalid ppi: size %u ppi_offset %u\n",
					   ppi->size, ppi->ppi_offset);
				continue;
			}
			return (void *)((ulong)ppi + ppi->ppi_offset);
		}
		len -= ppi->size;
		ppi = (struct rndis_per_packet_info *)((ulong)ppi + ppi->size);
	}

	return NULL;
}

static inline
void rsc_add_data(struct netvsc_channel *nvchan,
		  const struct ndis_pkt_8021q_info *vlan,
		  const struct ndis_tcp_ip_checksum_info *csum_info,
		  const u32 *hash_info,
		  void *data, u32 len)
{
	u32 cnt = nvchan->rsc.cnt;

	if (cnt) {
		nvchan->rsc.pktlen += len;
	} else {
		/* The data/values pointed by vlan, csum_info and hash_info are shared
		 * across the different 'fragments' of the RSC packet; store them into
		 * the packet itself.
		 */
		if (vlan != NULL) {
			memcpy(&nvchan->rsc.vlan, vlan, sizeof(*vlan));
			nvchan->rsc.ppi_flags |= NVSC_RSC_VLAN;
		} else {
			nvchan->rsc.ppi_flags &= ~NVSC_RSC_VLAN;
		}
		if (csum_info != NULL) {
			memcpy(&nvchan->rsc.csum_info, csum_info, sizeof(*csum_info));
			nvchan->rsc.ppi_flags |= NVSC_RSC_CSUM_INFO;
		} else {
			nvchan->rsc.ppi_flags &= ~NVSC_RSC_CSUM_INFO;
		}
		nvchan->rsc.pktlen = len;
		if (hash_info != NULL) {
			nvchan->rsc.hash_info = *hash_info;
			nvchan->rsc.ppi_flags |= NVSC_RSC_HASH_INFO;
		} else {
			nvchan->rsc.ppi_flags &= ~NVSC_RSC_HASH_INFO;
		}
	}

	nvchan->rsc.data[cnt] = data;
	nvchan->rsc.len[cnt] = len;
	nvchan->rsc.cnt++;
}

static int rndis_filter_receive_data(struct net_device *ndev,
				     struct netvsc_device *nvdev,
				     struct netvsc_channel *nvchan,
				     struct rndis_message *msg,
				     void *data, u32 data_buflen)
{
	struct rndis_packet *rndis_pkt = &msg->msg.pkt;
	const struct ndis_tcp_ip_checksum_info *csum_info;
	const struct ndis_pkt_8021q_info *vlan;
	const struct rndis_pktinfo_id *pktinfo_id;
	const u32 *hash_info;
	u32 data_offset, rpkt_len;
	bool rsc_more = false;
	int ret;

	/* Ensure data_buflen is big enough to read header fields */
	if (data_buflen < RNDIS_HEADER_SIZE + sizeof(struct rndis_packet)) {
		netdev_err(ndev, "invalid rndis pkt, data_buflen too small: %u\n",
			   data_buflen);
		return NVSP_STAT_FAIL;
	}

	/* Copy the RNDIS packet into nvchan->recv_buf */
	memcpy(rndis_pkt, data + RNDIS_HEADER_SIZE, sizeof(*rndis_pkt));

	/* Validate rndis_pkt offset */
	if (rndis_pkt->data_offset >= data_buflen - RNDIS_HEADER_SIZE) {
		netdev_err(ndev, "invalid rndis packet offset: %u\n",
			   rndis_pkt->data_offset);
		return NVSP_STAT_FAIL;
	}

	/* Remove the rndis header and pass it back up the stack */
	data_offset = RNDIS_HEADER_SIZE + rndis_pkt->data_offset;

	rpkt_len = data_buflen - RNDIS_HEADER_SIZE;
	data_buflen -= data_offset;

	/*
	 * Make sure we got a valid RNDIS message, now total_data_buflen
	 * should be the data packet size plus the trailer padding size
	 */
	if (unlikely(data_buflen < rndis_pkt->data_len)) {
		netdev_err(ndev, "rndis message buffer "
			   "overflow detected (got %u, min %u)"
			   "...dropping this message!\n",
			   data_buflen, rndis_pkt->data_len);
		return NVSP_STAT_FAIL;
	}

	vlan = rndis_get_ppi(ndev, rndis_pkt, rpkt_len, IEEE_8021Q_INFO, 0, sizeof(*vlan),
			     data);

	csum_info = rndis_get_ppi(ndev, rndis_pkt, rpkt_len, TCPIP_CHKSUM_PKTINFO, 0,
				  sizeof(*csum_info), data);

	hash_info = rndis_get_ppi(ndev, rndis_pkt, rpkt_len, NBL_HASH_VALUE, 0,
				  sizeof(*hash_info), data);

	pktinfo_id = rndis_get_ppi(ndev, rndis_pkt, rpkt_len, RNDIS_PKTINFO_ID, 1,
				   sizeof(*pktinfo_id), data);

	/* Identify RSC frags, drop erroneous packets */
	if (pktinfo_id && (pktinfo_id->flag & RNDIS_PKTINFO_SUBALLOC)) {
		if (pktinfo_id->flag & RNDIS_PKTINFO_1ST_FRAG)
			nvchan->rsc.cnt = 0;
		else if (nvchan->rsc.cnt == 0)
			goto drop;

		rsc_more = true;

		if (pktinfo_id->flag & RNDIS_PKTINFO_LAST_FRAG)
			rsc_more = false;

		if (rsc_more && nvchan->rsc.is_last)
			goto drop;
	} else {
		nvchan->rsc.cnt = 0;
	}

	if (unlikely(nvchan->rsc.cnt >= NVSP_RSC_MAX))
		goto drop;

	/* Put data into per channel structure.
	 * Also, remove the rndis trailer padding from rndis packet message
	 * rndis_pkt->data_len tell us the real data length, we only copy
	 * the data packet to the stack, without the rndis trailer padding
	 */
	rsc_add_data(nvchan, vlan, csum_info, hash_info,
		     data + data_offset, rndis_pkt->data_len);

	if (rsc_more)
		return NVSP_STAT_SUCCESS;

	ret = netvsc_recv_callback(ndev, nvdev, nvchan);
	nvchan->rsc.cnt = 0;

	return ret;

drop:
	return NVSP_STAT_FAIL;
}

int rndis_filter_receive(struct net_device *ndev,
			 struct netvsc_device *net_dev,
			 struct netvsc_channel *nvchan,
			 void *data, u32 buflen)
{
	struct net_device_context *net_device_ctx = netdev_priv(ndev);
	struct rndis_message *rndis_msg = nvchan->recv_buf;

	if (buflen < RNDIS_HEADER_SIZE) {
		netdev_err(ndev, "Invalid rndis_msg (buflen: %u)\n", buflen);
		return NVSP_STAT_FAIL;
	}

	/* Copy the RNDIS msg header into nvchan->recv_buf */
	memcpy(rndis_msg, data, RNDIS_HEADER_SIZE);

	/* Validate incoming rndis_message packet */
	if (rndis_msg->msg_len < RNDIS_HEADER_SIZE ||
	    buflen < rndis_msg->msg_len) {
		netdev_err(ndev, "Invalid rndis_msg (buflen: %u, msg_len: %u)\n",
			   buflen, rndis_msg->msg_len);
		return NVSP_STAT_FAIL;
	}

	if (netif_msg_rx_status(net_device_ctx))
		dump_rndis_message(ndev, rndis_msg, data);

	switch (rndis_msg->ndis_msg_type) {
	case RNDIS_MSG_PACKET:
		return rndis_filter_receive_data(ndev, net_dev, nvchan,
						 rndis_msg, data, buflen);
	case RNDIS_MSG_INIT_C:
	case RNDIS_MSG_QUERY_C:
	case RNDIS_MSG_SET_C:
		/* completion msgs */
		rndis_filter_receive_response(ndev, net_dev, rndis_msg, data);
		break;

	case RNDIS_MSG_INDICATE:
		/* notification msgs */
		netvsc_linkstatus_callback(ndev, rndis_msg, data, buflen);
		break;
	default:
		netdev_err(ndev,
			"unhandled rndis message (type %u len %u)\n",
			   rndis_msg->ndis_msg_type,
			   rndis_msg->msg_len);
		return NVSP_STAT_FAIL;
	}

	return NVSP_STAT_SUCCESS;
}

static int rndis_filter_query_device(struct rndis_device *dev,
				     struct netvsc_device *nvdev,
				     u32 oid, void *result, u32 *result_size)
{
	struct rndis_request *request;
	u32 inresult_size = *result_size;
	struct rndis_query_request *query;
	struct rndis_query_complete *query_complete;
	u32 msg_len;
	int ret = 0;

	if (!result)
		return -EINVAL;

	*result_size = 0;
	request = get_rndis_request(dev, RNDIS_MSG_QUERY,
			RNDIS_MESSAGE_SIZE(struct rndis_query_request));
	if (!request) {
		ret = -ENOMEM;
		goto cleanup;
	}

	/* Setup the rndis query */
	query = &request->request_msg.msg.query_req;
	query->oid = oid;
	query->info_buf_offset = sizeof(struct rndis_query_request);
	query->info_buflen = 0;
	query->dev_vc_handle = 0;

	if (oid == OID_TCP_OFFLOAD_HARDWARE_CAPABILITIES) {
		struct ndis_offload *hwcaps;
		u32 nvsp_version = nvdev->nvsp_version;
		u8 ndis_rev;
		size_t size;

		if (nvsp_version >= NVSP_PROTOCOL_VERSION_5) {
			ndis_rev = NDIS_OFFLOAD_PARAMETERS_REVISION_3;
			size = NDIS_OFFLOAD_SIZE;
		} else if (nvsp_version >= NVSP_PROTOCOL_VERSION_4) {
			ndis_rev = NDIS_OFFLOAD_PARAMETERS_REVISION_2;
			size = NDIS_OFFLOAD_SIZE_6_1;
		} else {
			ndis_rev = NDIS_OFFLOAD_PARAMETERS_REVISION_1;
			size = NDIS_OFFLOAD_SIZE_6_0;
		}

		request->request_msg.msg_len += size;
		query->info_buflen = size;
		hwcaps = (struct ndis_offload *)
			((unsigned long)query + query->info_buf_offset);

		hwcaps->header.type = NDIS_OBJECT_TYPE_OFFLOAD;
		hwcaps->header.revision = ndis_rev;
		hwcaps->header.size = size;

	} else if (oid == OID_GEN_RECEIVE_SCALE_CAPABILITIES) {
		struct ndis_recv_scale_cap *cap;

		request->request_msg.msg_len +=
			sizeof(struct ndis_recv_scale_cap);
		query->info_buflen = sizeof(struct ndis_recv_scale_cap);
		cap = (struct ndis_recv_scale_cap *)((unsigned long)query +
						     query->info_buf_offset);
		cap->hdr.type = NDIS_OBJECT_TYPE_RSS_CAPABILITIES;
		cap->hdr.rev = NDIS_RECEIVE_SCALE_CAPABILITIES_REVISION_2;
		cap->hdr.size = sizeof(struct ndis_recv_scale_cap);
	}

	ret = rndis_filter_send_request(dev, request);
	if (ret != 0)
		goto cleanup;

	wait_for_completion(&request->wait_event);

	/* Copy the response back */
	query_complete = &request->response_msg.msg.query_complete;
	msg_len = request->response_msg.msg_len;

	/* Ensure the packet is big enough to access its fields */
	if (msg_len - RNDIS_HEADER_SIZE < sizeof(struct rndis_query_complete)) {
		ret = -1;
		goto cleanup;
	}

	if (query_complete->info_buflen > inresult_size ||
	    query_complete->info_buf_offset < sizeof(*query_complete) ||
	    msg_len - RNDIS_HEADER_SIZE < query_complete->info_buf_offset ||
	    msg_len - RNDIS_HEADER_SIZE - query_complete->info_buf_offset
			< query_complete->info_buflen) {
		ret = -1;
		goto cleanup;
	}

	memcpy(result,
	       (void *)((unsigned long)query_complete +
			 query_complete->info_buf_offset),
	       query_complete->info_buflen);

	*result_size = query_complete->info_buflen;

cleanup:
	if (request)
		put_rndis_request(dev, request);

	return ret;
}

/* Get the hardware offload capabilities */
static int
rndis_query_hwcaps(struct rndis_device *dev, struct netvsc_device *net_device,
		   struct ndis_offload *caps)
{
	u32 caps_len = sizeof(*caps);
	int ret;

	memset(caps, 0, sizeof(*caps));

	ret = rndis_filter_query_device(dev, net_device,
					OID_TCP_OFFLOAD_HARDWARE_CAPABILITIES,
					caps, &caps_len);
	if (ret)
		return ret;

	if (caps->header.type != NDIS_OBJECT_TYPE_OFFLOAD) {
		netdev_warn(dev->ndev, "invalid NDIS objtype %#x\n",
			    caps->header.type);
		return -EINVAL;
	}

	if (caps->header.revision < NDIS_OFFLOAD_PARAMETERS_REVISION_1) {
		netdev_warn(dev->ndev, "invalid NDIS objrev %x\n",
			    caps->header.revision);
		return -EINVAL;
	}

	if (caps->header.size > caps_len ||
	    caps->header.size < NDIS_OFFLOAD_SIZE_6_0) {
		netdev_warn(dev->ndev,
			    "invalid NDIS objsize %u, data size %u\n",
			    caps->header.size, caps_len);
		return -EINVAL;
	}

	return 0;
}

static int rndis_filter_query_device_mac(struct rndis_device *dev,
					 struct netvsc_device *net_device)
{
	u32 size = ETH_ALEN;

	return rndis_filter_query_device(dev, net_device,
				      RNDIS_OID_802_3_PERMANENT_ADDRESS,
				      dev->hw_mac_adr, &size);
}

#define NWADR_STR "NetworkAddress"
#define NWADR_STRLEN 14

int rndis_filter_set_device_mac(struct netvsc_device *nvdev,
				const char *mac)
{
	struct rndis_device *rdev = nvdev->extension;
	struct rndis_request *request;
	struct rndis_set_request *set;
	struct rndis_config_parameter_info *cpi;
	wchar_t *cfg_nwadr, *cfg_mac;
	struct rndis_set_complete *set_complete;
	char macstr[2*ETH_ALEN+1];
	u32 extlen = sizeof(struct rndis_config_parameter_info) +
		2*NWADR_STRLEN + 4*ETH_ALEN;
	int ret;

	request = get_rndis_request(rdev, RNDIS_MSG_SET,
		RNDIS_MESSAGE_SIZE(struct rndis_set_request) + extlen);
	if (!request)
		return -ENOMEM;

	set = &request->request_msg.msg.set_req;
	set->oid = RNDIS_OID_GEN_RNDIS_CONFIG_PARAMETER;
	set->info_buflen = extlen;
	set->info_buf_offset = sizeof(struct rndis_set_request);
	set->dev_vc_handle = 0;

	cpi = (struct rndis_config_parameter_info *)((ulong)set +
		set->info_buf_offset);
	cpi->parameter_name_offset =
		sizeof(struct rndis_config_parameter_info);
	/* Multiply by 2 because host needs 2 bytes (utf16) for each char */
	cpi->parameter_name_length = 2*NWADR_STRLEN;
	cpi->parameter_type = RNDIS_CONFIG_PARAM_TYPE_STRING;
	cpi->parameter_value_offset =
		cpi->parameter_name_offset + cpi->parameter_name_length;
	/* Multiply by 4 because each MAC byte displayed as 2 utf16 chars */
	cpi->parameter_value_length = 4*ETH_ALEN;

	cfg_nwadr = (wchar_t *)((ulong)cpi + cpi->parameter_name_offset);
	cfg_mac = (wchar_t *)((ulong)cpi + cpi->parameter_value_offset);
	ret = utf8s_to_utf16s(NWADR_STR, NWADR_STRLEN, UTF16_HOST_ENDIAN,
			      cfg_nwadr, NWADR_STRLEN);
	if (ret < 0)
		goto cleanup;
	snprintf(macstr, 2*ETH_ALEN+1, "%pm", mac);
	ret = utf8s_to_utf16s(macstr, 2*ETH_ALEN, UTF16_HOST_ENDIAN,
			      cfg_mac, 2*ETH_ALEN);
	if (ret < 0)
		goto cleanup;

	ret = rndis_filter_send_request(rdev, request);
	if (ret != 0)
		goto cleanup;

	wait_for_completion(&request->wait_event);

	set_complete = &request->response_msg.msg.set_complete;
	if (set_complete->status != RNDIS_STATUS_SUCCESS)
		ret = -EIO;

cleanup:
	put_rndis_request(rdev, request);
	return ret;
}

int
rndis_filter_set_offload_params(struct net_device *ndev,
				struct netvsc_device *nvdev,
				struct ndis_offload_params *req_offloads)
{
	struct rndis_device *rdev = nvdev->extension;
	struct rndis_request *request;
	struct rndis_set_request *set;
	struct ndis_offload_params *offload_params;
	struct rndis_set_complete *set_complete;
	u32 extlen = sizeof(struct ndis_offload_params);
	int ret;
	u32 vsp_version = nvdev->nvsp_version;

	if (vsp_version <= NVSP_PROTOCOL_VERSION_4) {
		extlen = VERSION_4_OFFLOAD_SIZE;
		/* On NVSP_PROTOCOL_VERSION_4 and below, we do not support
		 * UDP checksum offload.
		 */
		req_offloads->udp_ip_v4_csum = 0;
		req_offloads->udp_ip_v6_csum = 0;
	}

	request = get_rndis_request(rdev, RNDIS_MSG_SET,
		RNDIS_MESSAGE_SIZE(struct rndis_set_request) + extlen);
	if (!request)
		return -ENOMEM;

	set = &request->request_msg.msg.set_req;
	set->oid = OID_TCP_OFFLOAD_PARAMETERS;
	set->info_buflen = extlen;
	set->info_buf_offset = sizeof(struct rndis_set_request);
	set->dev_vc_handle = 0;

	offload_params = (struct ndis_offload_params *)((ulong)set +
				set->info_buf_offset);
	*offload_params = *req_offloads;
	offload_params->header.type = NDIS_OBJECT_TYPE_DEFAULT;
	offload_params->header.revision = NDIS_OFFLOAD_PARAMETERS_REVISION_3;
	offload_params->header.size = extlen;

	ret = rndis_filter_send_request(rdev, request);
	if (ret != 0)
		goto cleanup;

	wait_for_completion(&request->wait_event);
	set_complete = &request->response_msg.msg.set_complete;
	if (set_complete->status != RNDIS_STATUS_SUCCESS) {
		netdev_err(ndev, "Fail to set offload on host side:0x%x\n",
			   set_complete->status);
		ret = -EINVAL;
	}

cleanup:
	put_rndis_request(rdev, request);
	return ret;
}

static int rndis_set_rss_param_msg(struct rndis_device *rdev,
				   const u8 *rss_key, u16 flag)
{
	struct net_device *ndev = rdev->ndev;
	struct net_device_context *ndc = netdev_priv(ndev);
	struct rndis_request *request;
	struct rndis_set_request *set;
	struct rndis_set_complete *set_complete;
	u32 extlen = sizeof(struct ndis_recv_scale_param) +
		     4 * ITAB_NUM + NETVSC_HASH_KEYLEN;
	struct ndis_recv_scale_param *rssp;
	u32 *itab;
	u8 *keyp;
	int i, ret;

	request = get_rndis_request(
			rdev, RNDIS_MSG_SET,
			RNDIS_MESSAGE_SIZE(struct rndis_set_request) + extlen);
	if (!request)
		return -ENOMEM;

	set = &request->request_msg.msg.set_req;
	set->oid = OID_GEN_RECEIVE_SCALE_PARAMETERS;
	set->info_buflen = extlen;
	set->info_buf_offset = sizeof(struct rndis_set_request);
	set->dev_vc_handle = 0;

	rssp = (struct ndis_recv_scale_param *)(set + 1);
	rssp->hdr.type = NDIS_OBJECT_TYPE_RSS_PARAMETERS;
	rssp->hdr.rev = NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_2;
	rssp->hdr.size = sizeof(struct ndis_recv_scale_param);
	rssp->flag = flag;
	rssp->hashinfo = NDIS_HASH_FUNC_TOEPLITZ | NDIS_HASH_IPV4 |
			 NDIS_HASH_TCP_IPV4 | NDIS_HASH_IPV6 |
			 NDIS_HASH_TCP_IPV6;
	rssp->indirect_tabsize = 4*ITAB_NUM;
	rssp->indirect_taboffset = sizeof(struct ndis_recv_scale_param);
	rssp->hashkey_size = NETVSC_HASH_KEYLEN;
	rssp->hashkey_offset = rssp->indirect_taboffset +
			       rssp->indirect_tabsize;

	/* Set indirection table entries */
	itab = (u32 *)(rssp + 1);
	for (i = 0; i < ITAB_NUM; i++)
		itab[i] = ndc->rx_table[i];

	/* Set hask key values */
	keyp = (u8 *)((unsigned long)rssp + rssp->hashkey_offset);
	memcpy(keyp, rss_key, NETVSC_HASH_KEYLEN);

	ret = rndis_filter_send_request(rdev, request);
	if (ret != 0)
		goto cleanup;

	wait_for_completion(&request->wait_event);
	set_complete = &request->response_msg.msg.set_complete;
	if (set_complete->status == RNDIS_STATUS_SUCCESS) {
		if (!(flag & NDIS_RSS_PARAM_FLAG_DISABLE_RSS) &&
		    !(flag & NDIS_RSS_PARAM_FLAG_HASH_KEY_UNCHANGED))
			memcpy(rdev->rss_key, rss_key, NETVSC_HASH_KEYLEN);

	} else {
		netdev_err(ndev, "Fail to set RSS parameters:0x%x\n",
			   set_complete->status);
		ret = -EINVAL;
	}

cleanup:
	put_rndis_request(rdev, request);
	return ret;
}

int rndis_filter_set_rss_param(struct rndis_device *rdev,
			       const u8 *rss_key)
{
	/* Disable RSS before change */
	rndis_set_rss_param_msg(rdev, rss_key,
				NDIS_RSS_PARAM_FLAG_DISABLE_RSS);

	return rndis_set_rss_param_msg(rdev, rss_key, 0);
}

static int rndis_filter_query_device_link_status(struct rndis_device *dev,
						 struct netvsc_device *net_device)
{
	u32 size = sizeof(u32);
	u32 link_status;

	return rndis_filter_query_device(dev, net_device,
					 RNDIS_OID_GEN_MEDIA_CONNECT_STATUS,
					 &link_status, &size);
}

static int rndis_filter_query_link_speed(struct rndis_device *dev,
					 struct netvsc_device *net_device)
{
	u32 size = sizeof(u32);
	u32 link_speed;
	struct net_device_context *ndc;
	int ret;

	ret = rndis_filter_query_device(dev, net_device,
					RNDIS_OID_GEN_LINK_SPEED,
					&link_speed, &size);

	if (!ret) {
		ndc = netdev_priv(dev->ndev);

		/* The link speed reported from host is in 100bps unit, so
		 * we convert it to Mbps here.
		 */
		ndc->speed = link_speed / 10000;
	}

	return ret;
}

static int rndis_filter_set_packet_filter(struct rndis_device *dev,
					  u32 new_filter)
{
	struct rndis_request *request;
	struct rndis_set_request *set;
	int ret;

	if (dev->filter == new_filter)
		return 0;

	request = get_rndis_request(dev, RNDIS_MSG_SET,
			RNDIS_MESSAGE_SIZE(struct rndis_set_request) +
			sizeof(u32));
	if (!request)
		return -ENOMEM;

	/* Setup the rndis set */
	set = &request->request_msg.msg.set_req;
	set->oid = RNDIS_OID_GEN_CURRENT_PACKET_FILTER;
	set->info_buflen = sizeof(u32);
	set->info_buf_offset = offsetof(typeof(*set), info_buf);
	memcpy(set->info_buf, &new_filter, sizeof(u32));

	ret = rndis_filter_send_request(dev, request);
	if (ret == 0) {
		wait_for_completion(&request->wait_event);
		dev->filter = new_filter;
	}

	put_rndis_request(dev, request);

	return ret;
}

static void rndis_set_multicast(struct work_struct *w)
{
	struct rndis_device *rdev
		= container_of(w, struct rndis_device, mcast_work);
	u32 filter = NDIS_PACKET_TYPE_DIRECTED;
	unsigned int flags = rdev->ndev->flags;

	if (flags & IFF_PROMISC) {
		filter = NDIS_PACKET_TYPE_PROMISCUOUS;
	} else {
		if (!netdev_mc_empty(rdev->ndev) || (flags & IFF_ALLMULTI))
			filter |= NDIS_PACKET_TYPE_ALL_MULTICAST;
		if (flags & IFF_BROADCAST)
			filter |= NDIS_PACKET_TYPE_BROADCAST;
	}

	rndis_filter_set_packet_filter(rdev, filter);
}

void rndis_filter_update(struct netvsc_device *nvdev)
{
	struct rndis_device *rdev = nvdev->extension;

	schedule_work(&rdev->mcast_work);
}

static int rndis_filter_init_device(struct rndis_device *dev,
				    struct netvsc_device *nvdev)
{
	struct rndis_request *request;
	struct rndis_initialize_request *init;
	struct rndis_initialize_complete *init_complete;
	u32 status;
	int ret;

	request = get_rndis_request(dev, RNDIS_MSG_INIT,
			RNDIS_MESSAGE_SIZE(struct rndis_initialize_request));
	if (!request) {
		ret = -ENOMEM;
		goto cleanup;
	}

	/* Setup the rndis set */
	init = &request->request_msg.msg.init_req;
	init->major_ver = RNDIS_MAJOR_VERSION;
	init->minor_ver = RNDIS_MINOR_VERSION;
	init->max_xfer_size = 0x4000;

	dev->state = RNDIS_DEV_INITIALIZING;

	ret = rndis_filter_send_request(dev, request);
	if (ret != 0) {
		dev->state = RNDIS_DEV_UNINITIALIZED;
		goto cleanup;
	}

	wait_for_completion(&request->wait_event);

	init_complete = &request->response_msg.msg.init_complete;
	status = init_complete->status;
	if (status == RNDIS_STATUS_SUCCESS) {
		dev->state = RNDIS_DEV_INITIALIZED;
		nvdev->max_pkt = init_complete->max_pkt_per_msg;
		nvdev->pkt_align = 1 << init_complete->pkt_alignment_factor;
		ret = 0;
	} else {
		dev->state = RNDIS_DEV_UNINITIALIZED;
		ret = -EINVAL;
	}

cleanup:
	if (request)
		put_rndis_request(dev, request);

	return ret;
}

static bool netvsc_device_idle(const struct netvsc_device *nvdev)
{
	int i;

	for (i = 0; i < nvdev->num_chn; i++) {
		const struct netvsc_channel *nvchan = &nvdev->chan_table[i];

		if (nvchan->mrc.first != nvchan->mrc.next)
			return false;

		if (atomic_read(&nvchan->queue_sends) > 0)
			return false;
	}

	return true;
}

static void rndis_filter_halt_device(struct netvsc_device *nvdev,
				     struct rndis_device *dev)
{
	struct rndis_request *request;
	struct rndis_halt_request *halt;

	/* Attempt to do a rndis device halt */
	request = get_rndis_request(dev, RNDIS_MSG_HALT,
				RNDIS_MESSAGE_SIZE(struct rndis_halt_request));
	if (!request)
		goto cleanup;

	/* Setup the rndis set */
	halt = &request->request_msg.msg.halt_req;
	halt->req_id = atomic_inc_return(&dev->new_req_id);

	/* Ignore return since this msg is optional. */
	rndis_filter_send_request(dev, request);

	dev->state = RNDIS_DEV_UNINITIALIZED;

cleanup:
	nvdev->destroy = true;

	/* Force flag to be ordered before waiting */
	wmb();

	/* Wait for all send completions */
	wait_event(nvdev->wait_drain, netvsc_device_idle(nvdev));

	if (request)
		put_rndis_request(dev, request);
}

static int rndis_filter_open_device(struct rndis_device *dev)
{
	int ret;

	if (dev->state != RNDIS_DEV_INITIALIZED)
		return 0;

	ret = rndis_filter_set_packet_filter(dev,
					 NDIS_PACKET_TYPE_BROADCAST |
					 NDIS_PACKET_TYPE_ALL_MULTICAST |
					 NDIS_PACKET_TYPE_DIRECTED);
	if (ret == 0)
		dev->state = RNDIS_DEV_DATAINITIALIZED;

	return ret;
}

static int rndis_filter_close_device(struct rndis_device *dev)
{
	int ret;

	if (dev->state != RNDIS_DEV_DATAINITIALIZED)
		return 0;

	/* Make sure rndis_set_multicast doesn't re-enable filter! */
	cancel_work_sync(&dev->mcast_work);

	ret = rndis_filter_set_packet_filter(dev, 0);
	if (ret == -ENODEV)
		ret = 0;

	if (ret == 0)
		dev->state = RNDIS_DEV_INITIALIZED;

	return ret;
}

static void netvsc_sc_open(struct vmbus_channel *new_sc)
{
	struct net_device *ndev =
		hv_get_drvdata(new_sc->primary_channel->device_obj);
	struct net_device_context *ndev_ctx = netdev_priv(ndev);
	struct netvsc_device *nvscdev;
	u16 chn_index = new_sc->offermsg.offer.sub_channel_index;
	struct netvsc_channel *nvchan;
	int ret;

	/* This is safe because this callback only happens when
	 * new device is being setup and waiting on the channel_init_wait.
	 */
	nvscdev = rcu_dereference_raw(ndev_ctx->nvdev);
	if (!nvscdev || chn_index >= nvscdev->num_chn)
		return;

	nvchan = nvscdev->chan_table + chn_index;

	/* Because the device uses NAPI, all the interrupt batching and
	 * control is done via Net softirq, not the channel handling
	 */
	set_channel_read_mode(new_sc, HV_CALL_ISR);

	/* Set the channel before opening.*/
	nvchan->channel = new_sc;

	new_sc->next_request_id_callback = vmbus_next_request_id;
	new_sc->request_addr_callback = vmbus_request_addr;
	new_sc->rqstor_size = netvsc_rqstor_size(netvsc_ring_bytes);
	new_sc->max_pkt_size = NETVSC_MAX_PKT_SIZE;

	ret = vmbus_open(new_sc, netvsc_ring_bytes,
			 netvsc_ring_bytes, NULL, 0,
			 netvsc_channel_cb, nvchan);
	if (ret == 0)
		napi_enable(&nvchan->napi);
	else
		netdev_notice(ndev, "sub channel open failed: %d\n", ret);

	if (atomic_inc_return(&nvscdev->open_chn) == nvscdev->num_chn)
		wake_up(&nvscdev->subchan_open);
}

/* Open sub-channels after completing the handling of the device probe.
 * This breaks overlap of processing the host message for the
 * new primary channel with the initialization of sub-channels.
 */
int rndis_set_subchannel(struct net_device *ndev,
			 struct netvsc_device *nvdev,
			 struct netvsc_device_info *dev_info)
{
	struct nvsp_message *init_packet = &nvdev->channel_init_pkt;
	struct net_device_context *ndev_ctx = netdev_priv(ndev);
	struct hv_device *hv_dev = ndev_ctx->device_ctx;
	struct rndis_device *rdev = nvdev->extension;
	int i, ret;

	ASSERT_RTNL();

	memset(init_packet, 0, sizeof(struct nvsp_message));
	init_packet->hdr.msg_type = NVSP_MSG5_TYPE_SUBCHANNEL;
	init_packet->msg.v5_msg.subchn_req.op = NVSP_SUBCHANNEL_ALLOCATE;
	init_packet->msg.v5_msg.subchn_req.num_subchannels =
						nvdev->num_chn - 1;
	trace_nvsp_send(ndev, init_packet);

	ret = vmbus_sendpacket(hv_dev->channel, init_packet,
			       sizeof(struct nvsp_message),
			       (unsigned long)init_packet,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret) {
		netdev_err(ndev, "sub channel allocate send failed: %d\n", ret);
		return ret;
	}

	wait_for_completion(&nvdev->channel_init_wait);
	if (init_packet->msg.v5_msg.subchn_comp.status != NVSP_STAT_SUCCESS) {
		netdev_err(ndev, "sub channel request failed\n");
		return -EIO;
	}

	/* Check that number of allocated sub channel is within the expected range */
	if (init_packet->msg.v5_msg.subchn_comp.num_subchannels > nvdev->num_chn - 1) {
		netdev_err(ndev, "invalid number of allocated sub channel\n");
		return -EINVAL;
	}
	nvdev->num_chn = 1 +
		init_packet->msg.v5_msg.subchn_comp.num_subchannels;

	/* wait for all sub channels to open */
	wait_event(nvdev->subchan_open,
		   atomic_read(&nvdev->open_chn) == nvdev->num_chn);

	for (i = 0; i < VRSS_SEND_TAB_SIZE; i++)
		ndev_ctx->tx_table[i] = i % nvdev->num_chn;

	/* ignore failures from setting rss parameters, still have channels */
	if (dev_info)
		rndis_filter_set_rss_param(rdev, dev_info->rss_key);
	else
		rndis_filter_set_rss_param(rdev, netvsc_hash_key);

	netif_set_real_num_tx_queues(ndev, nvdev->num_chn);
	netif_set_real_num_rx_queues(ndev, nvdev->num_chn);

	return 0;
}

static int rndis_netdev_set_hwcaps(struct rndis_device *rndis_device,
				   struct netvsc_device *nvdev)
{
	struct net_device *net = rndis_device->ndev;
	struct net_device_context *net_device_ctx = netdev_priv(net);
	struct ndis_offload hwcaps;
	struct ndis_offload_params offloads;
	unsigned int gso_max_size = GSO_LEGACY_MAX_SIZE;
	int ret;

	/* Find HW offload capabilities */
	ret = rndis_query_hwcaps(rndis_device, nvdev, &hwcaps);
	if (ret != 0)
		return ret;

	/* A value of zero means "no change"; now turn on what we want. */
	memset(&offloads, 0, sizeof(struct ndis_offload_params));

	/* Linux does not care about IP checksum, always does in kernel */
	offloads.ip_v4_csum = NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED;

	/* Reset previously set hw_features flags */
	net->hw_features &= ~NETVSC_SUPPORTED_HW_FEATURES;
	net_device_ctx->tx_checksum_mask = 0;

	/* Compute tx offload settings based on hw capabilities */
	net->hw_features |= NETIF_F_RXCSUM;
	net->hw_features |= NETIF_F_SG;
	net->hw_features |= NETIF_F_RXHASH;

	if ((hwcaps.csum.ip4_txcsum & NDIS_TXCSUM_ALL_TCP4) == NDIS_TXCSUM_ALL_TCP4) {
		/* Can checksum TCP */
		net->hw_features |= NETIF_F_IP_CSUM;
		net_device_ctx->tx_checksum_mask |= TRANSPORT_INFO_IPV4_TCP;

		offloads.tcp_ip_v4_csum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;

		if (hwcaps.lsov2.ip4_encap & NDIS_OFFLOAD_ENCAP_8023) {
			offloads.lso_v2_ipv4 = NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED;
			net->hw_features |= NETIF_F_TSO;

			if (hwcaps.lsov2.ip4_maxsz < gso_max_size)
				gso_max_size = hwcaps.lsov2.ip4_maxsz;
		}

		if (hwcaps.csum.ip4_txcsum & NDIS_TXCSUM_CAP_UDP4) {
			offloads.udp_ip_v4_csum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
			net_device_ctx->tx_checksum_mask |= TRANSPORT_INFO_IPV4_UDP;
		}
	}

	if ((hwcaps.csum.ip6_txcsum & NDIS_TXCSUM_ALL_TCP6) == NDIS_TXCSUM_ALL_TCP6) {
		net->hw_features |= NETIF_F_IPV6_CSUM;

		offloads.tcp_ip_v6_csum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
		net_device_ctx->tx_checksum_mask |= TRANSPORT_INFO_IPV6_TCP;

		if ((hwcaps.lsov2.ip6_encap & NDIS_OFFLOAD_ENCAP_8023) &&
		    (hwcaps.lsov2.ip6_opts & NDIS_LSOV2_CAP_IP6) == NDIS_LSOV2_CAP_IP6) {
			offloads.lso_v2_ipv6 = NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED;
			net->hw_features |= NETIF_F_TSO6;

			if (hwcaps.lsov2.ip6_maxsz < gso_max_size)
				gso_max_size = hwcaps.lsov2.ip6_maxsz;
		}

		if (hwcaps.csum.ip6_txcsum & NDIS_TXCSUM_CAP_UDP6) {
			offloads.udp_ip_v6_csum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
			net_device_ctx->tx_checksum_mask |= TRANSPORT_INFO_IPV6_UDP;
		}
	}

	if (hwcaps.rsc.ip4 && hwcaps.rsc.ip6) {
		net->hw_features |= NETIF_F_LRO;

		if (net->features & NETIF_F_LRO) {
			offloads.rsc_ip_v4 = NDIS_OFFLOAD_PARAMETERS_RSC_ENABLED;
			offloads.rsc_ip_v6 = NDIS_OFFLOAD_PARAMETERS_RSC_ENABLED;
		} else {
			offloads.rsc_ip_v4 = NDIS_OFFLOAD_PARAMETERS_RSC_DISABLED;
			offloads.rsc_ip_v6 = NDIS_OFFLOAD_PARAMETERS_RSC_DISABLED;
		}
	}

	/* In case some hw_features disappeared we need to remove them from
	 * net->features list as they're no longer supported.
	 */
	net->features &= ~NETVSC_SUPPORTED_HW_FEATURES | net->hw_features;

	netif_set_tso_max_size(net, gso_max_size);

	ret = rndis_filter_set_offload_params(net, nvdev, &offloads);

	return ret;
}

static void rndis_get_friendly_name(struct net_device *net,
				    struct rndis_device *rndis_device,
				    struct netvsc_device *net_device)
{
	ucs2_char_t wname[256];
	unsigned long len;
	u8 ifalias[256];
	u32 size;

	size = sizeof(wname);
	if (rndis_filter_query_device(rndis_device, net_device,
				      RNDIS_OID_GEN_FRIENDLY_NAME,
				      wname, &size) != 0)
		return;	/* ignore if host does not support */

	if (size == 0)
		return;	/* name not set */

	/* Convert Windows Unicode string to UTF-8 */
	len = ucs2_as_utf8(ifalias, wname, sizeof(ifalias));

	/* ignore the default value from host */
	if (strcmp(ifalias, "Network Adapter") != 0)
		dev_set_alias(net, ifalias, len);
}

struct netvsc_device *rndis_filter_device_add(struct hv_device *dev,
				      struct netvsc_device_info *device_info)
{
	struct net_device *net = hv_get_drvdata(dev);
	struct net_device_context *ndc = netdev_priv(net);
	struct netvsc_device *net_device;
	struct rndis_device *rndis_device;
	struct ndis_recv_scale_cap rsscap;
	u32 rsscap_size = sizeof(struct ndis_recv_scale_cap);
	u32 mtu, size;
	u32 num_possible_rss_qs;
	int i, ret;

	rndis_device = get_rndis_device();
	if (!rndis_device)
		return ERR_PTR(-ENODEV);

	/* Let the inner driver handle this first to create the netvsc channel
	 * NOTE! Once the channel is created, we may get a receive callback
	 * (RndisFilterOnReceive()) before this call is completed
	 */
	net_device = netvsc_device_add(dev, device_info);
	if (IS_ERR(net_device)) {
		kfree(rndis_device);
		return net_device;
	}

	/* Initialize the rndis device */
	net_device->max_chn = 1;
	net_device->num_chn = 1;

	net_device->extension = rndis_device;
	rndis_device->ndev = net;

	/* Send the rndis initialization message */
	ret = rndis_filter_init_device(rndis_device, net_device);
	if (ret != 0)
		goto err_dev_remv;

	/* Get the MTU from the host */
	size = sizeof(u32);
	ret = rndis_filter_query_device(rndis_device, net_device,
					RNDIS_OID_GEN_MAXIMUM_FRAME_SIZE,
					&mtu, &size);
	if (ret == 0 && size == sizeof(u32) && mtu < net->mtu)
		net->mtu = mtu;

	/* Get the mac address */
	ret = rndis_filter_query_device_mac(rndis_device, net_device);
	if (ret != 0)
		goto err_dev_remv;

	memcpy(device_info->mac_adr, rndis_device->hw_mac_adr, ETH_ALEN);

	/* Get friendly name as ifalias*/
	if (!net->ifalias)
		rndis_get_friendly_name(net, rndis_device, net_device);

	/* Query and set hardware capabilities */
	ret = rndis_netdev_set_hwcaps(rndis_device, net_device);
	if (ret != 0)
		goto err_dev_remv;

	rndis_filter_query_device_link_status(rndis_device, net_device);

	netdev_dbg(net, "Device MAC %pM link state %s\n",
		   rndis_device->hw_mac_adr,
		   rndis_device->link_state ? "down" : "up");

	if (net_device->nvsp_version < NVSP_PROTOCOL_VERSION_5)
		goto out;

	rndis_filter_query_link_speed(rndis_device, net_device);

	/* vRSS setup */
	memset(&rsscap, 0, rsscap_size);
	ret = rndis_filter_query_device(rndis_device, net_device,
					OID_GEN_RECEIVE_SCALE_CAPABILITIES,
					&rsscap, &rsscap_size);
	if (ret || rsscap.num_recv_que < 2)
		goto out;

	/* This guarantees that num_possible_rss_qs <= num_online_cpus */
	num_possible_rss_qs = min_t(u32, num_online_cpus(),
				    rsscap.num_recv_que);

	net_device->max_chn = min_t(u32, VRSS_CHANNEL_MAX, num_possible_rss_qs);

	/* We will use the given number of channels if available. */
	net_device->num_chn = min(net_device->max_chn, device_info->num_chn);

	if (!netif_is_rxfh_configured(net)) {
		for (i = 0; i < ITAB_NUM; i++)
			ndc->rx_table[i] = ethtool_rxfh_indir_default(
						i, net_device->num_chn);
	}

	atomic_set(&net_device->open_chn, 1);
	vmbus_set_sc_create_callback(dev->channel, netvsc_sc_open);

	for (i = 1; i < net_device->num_chn; i++) {
		ret = netvsc_alloc_recv_comp_ring(net_device, i);
		if (ret) {
			while (--i != 0)
				vfree(net_device->chan_table[i].mrc.slots);
			goto out;
		}
	}

	for (i = 1; i < net_device->num_chn; i++)
		netif_napi_add(net, &net_device->chan_table[i].napi,
			       netvsc_poll);

	return net_device;

out:
	/* setting up multiple channels failed */
	net_device->max_chn = 1;
	net_device->num_chn = 1;
	return net_device;

err_dev_remv:
	rndis_filter_device_remove(dev, net_device);
	return ERR_PTR(ret);
}

void rndis_filter_device_remove(struct hv_device *dev,
				struct netvsc_device *net_dev)
{
	struct rndis_device *rndis_dev = net_dev->extension;

	/* Halt and release the rndis device */
	rndis_filter_halt_device(net_dev, rndis_dev);

	netvsc_device_remove(dev);
}

int rndis_filter_open(struct netvsc_device *nvdev)
{
	if (!nvdev)
		return -EINVAL;

	return rndis_filter_open_device(nvdev->extension);
}

int rndis_filter_close(struct netvsc_device *nvdev)
{
	if (!nvdev)
		return -EINVAL;

	return rndis_filter_close_device(nvdev->extension);
}
