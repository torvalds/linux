/*
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 */
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

#include "hyperv_net.h"


#define RNDIS_EXT_LEN 100
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
	/* Set 2 pages for rndis requests crossing page boundary */
	struct hv_page_buffer buf[2];

	struct rndis_message request_msg;
	/*
	 * The buffer for the extended info after the RNDIS request message.
	 * It is referenced and sized in a similar way as response_ext.
	 */
	u8 request_ext[RNDIS_EXT_LEN];
};

static void rndis_filter_send_completion(void *ctx);


static struct rndis_device *get_rndis_device(void)
{
	struct rndis_device *device;

	device = kzalloc(sizeof(struct rndis_device), GFP_KERNEL);
	if (!device)
		return NULL;

	spin_lock_init(&device->request_lock);

	INIT_LIST_HEAD(&device->req_list);

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

static void dump_rndis_message(struct hv_device *hv_dev,
			struct rndis_message *rndis_msg)
{
	struct net_device *netdev;
	struct netvsc_device *net_device;

	net_device = hv_get_drvdata(hv_dev);
	netdev = net_device->ndev;

	switch (rndis_msg->ndis_msg_type) {
	case RNDIS_MSG_PACKET:
		netdev_dbg(netdev, "RNDIS_MSG_PACKET (len %u, "
			   "data offset %u data len %u, # oob %u, "
			   "oob offset %u, oob len %u, pkt offset %u, "
			   "pkt len %u\n",
			   rndis_msg->msg_len,
			   rndis_msg->msg.pkt.data_offset,
			   rndis_msg->msg.pkt.data_len,
			   rndis_msg->msg.pkt.num_oob_data_elements,
			   rndis_msg->msg.pkt.oob_data_offset,
			   rndis_msg->msg.pkt.oob_data_len,
			   rndis_msg->msg.pkt.per_pkt_info_offset,
			   rndis_msg->msg.pkt.per_pkt_info_len);
		break;

	case RNDIS_MSG_INIT_C:
		netdev_dbg(netdev, "RNDIS_MSG_INIT_C "
			"(len %u, id 0x%x, status 0x%x, major %d, minor %d, "
			"device flags %d, max xfer size 0x%x, max pkts %u, "
			"pkt aligned %u)\n",
			rndis_msg->msg_len,
			rndis_msg->msg.init_complete.req_id,
			rndis_msg->msg.init_complete.status,
			rndis_msg->msg.init_complete.major_ver,
			rndis_msg->msg.init_complete.minor_ver,
			rndis_msg->msg.init_complete.dev_flags,
			rndis_msg->msg.init_complete.max_xfer_size,
			rndis_msg->msg.init_complete.
			   max_pkt_per_msg,
			rndis_msg->msg.init_complete.
			   pkt_alignment_factor);
		break;

	case RNDIS_MSG_QUERY_C:
		netdev_dbg(netdev, "RNDIS_MSG_QUERY_C "
			"(len %u, id 0x%x, status 0x%x, buf len %u, "
			"buf offset %u)\n",
			rndis_msg->msg_len,
			rndis_msg->msg.query_complete.req_id,
			rndis_msg->msg.query_complete.status,
			rndis_msg->msg.query_complete.
			   info_buflen,
			rndis_msg->msg.query_complete.
			   info_buf_offset);
		break;

	case RNDIS_MSG_SET_C:
		netdev_dbg(netdev,
			"RNDIS_MSG_SET_C (len %u, id 0x%x, status 0x%x)\n",
			rndis_msg->msg_len,
			rndis_msg->msg.set_complete.req_id,
			rndis_msg->msg.set_complete.status);
		break;

	case RNDIS_MSG_INDICATE:
		netdev_dbg(netdev, "RNDIS_MSG_INDICATE "
			"(len %u, status 0x%x, buf len %u, buf offset %u)\n",
			rndis_msg->msg_len,
			rndis_msg->msg.indicate_status.status,
			rndis_msg->msg.indicate_status.status_buflen,
			rndis_msg->msg.indicate_status.status_buf_offset);
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
	int ret;
	struct hv_netvsc_packet *packet;

	/* Setup the packet to send it */
	packet = &req->pkt;

	packet->is_data_pkt = false;
	packet->total_data_buflen = req->request_msg.msg_len;
	packet->page_buf_cnt = 1;

	packet->page_buf[0].pfn = virt_to_phys(&req->request_msg) >>
					PAGE_SHIFT;
	packet->page_buf[0].len = req->request_msg.msg_len;
	packet->page_buf[0].offset =
		(unsigned long)&req->request_msg & (PAGE_SIZE - 1);

	/* Add one page_buf when request_msg crossing page boundary */
	if (packet->page_buf[0].offset + packet->page_buf[0].len > PAGE_SIZE) {
		packet->page_buf_cnt++;
		packet->page_buf[0].len = PAGE_SIZE -
			packet->page_buf[0].offset;
		packet->page_buf[1].pfn = virt_to_phys((void *)&req->request_msg
			+ packet->page_buf[0].len) >> PAGE_SHIFT;
		packet->page_buf[1].offset = 0;
		packet->page_buf[1].len = req->request_msg.msg_len -
			packet->page_buf[0].len;
	}

	packet->completion.send.send_completion = NULL;

	ret = netvsc_send(dev->net_dev->dev, packet);
	return ret;
}

static void rndis_set_link_state(struct rndis_device *rdev,
				 struct rndis_request *request)
{
	u32 link_status;
	struct rndis_query_complete *query_complete;

	query_complete = &request->response_msg.msg.query_complete;

	if (query_complete->status == RNDIS_STATUS_SUCCESS &&
	    query_complete->info_buflen == sizeof(u32)) {
		memcpy(&link_status, (void *)((unsigned long)query_complete +
		       query_complete->info_buf_offset), sizeof(u32));
		rdev->link_state = link_status != 0;
	}
}

static void rndis_filter_receive_response(struct rndis_device *dev,
				       struct rndis_message *resp)
{
	struct rndis_request *request = NULL;
	bool found = false;
	unsigned long flags;
	struct net_device *ndev;

	ndev = dev->net_dev->ndev;

	spin_lock_irqsave(&dev->request_lock, flags);
	list_for_each_entry(request, &dev->req_list, list_ent) {
		/*
		 * All request/response message contains RequestId as the 1st
		 * field
		 */
		if (request->request_msg.msg.init_req.req_id
		    == resp->msg.init_complete.req_id) {
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&dev->request_lock, flags);

	if (found) {
		if (resp->msg_len <=
		    sizeof(struct rndis_message) + RNDIS_EXT_LEN) {
			memcpy(&request->response_msg, resp,
			       resp->msg_len);
			if (request->request_msg.ndis_msg_type ==
			    RNDIS_MSG_QUERY && request->request_msg.msg.
			    query_req.oid == RNDIS_OID_GEN_MEDIA_CONNECT_STATUS)
				rndis_set_link_state(dev, request);
		} else {
			netdev_err(ndev,
				"rndis response buffer overflow "
				"detected (size %u max %zu)\n",
				resp->msg_len,
				sizeof(struct rndis_filter_packet));

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

		complete(&request->wait_event);
	} else {
		netdev_err(ndev,
			"no rndis request found for this response "
			"(id 0x%x res type 0x%x)\n",
			resp->msg.init_complete.req_id,
			resp->ndis_msg_type);
	}
}

static void rndis_filter_receive_indicate_status(struct rndis_device *dev,
					     struct rndis_message *resp)
{
	struct rndis_indicate_status *indicate =
			&resp->msg.indicate_status;

	if (indicate->status == RNDIS_STATUS_MEDIA_CONNECT) {
		netvsc_linkstatus_callback(
			dev->net_dev->dev, 1);
	} else if (indicate->status == RNDIS_STATUS_MEDIA_DISCONNECT) {
		netvsc_linkstatus_callback(
			dev->net_dev->dev, 0);
	} else {
		/*
		 * TODO:
		 */
	}
}

/*
 * Get the Per-Packet-Info with the specified type
 * return NULL if not found.
 */
static inline void *rndis_get_ppi(struct rndis_packet *rpkt, u32 type)
{
	struct rndis_per_packet_info *ppi;
	int len;

	if (rpkt->per_pkt_info_offset == 0)
		return NULL;

	ppi = (struct rndis_per_packet_info *)((ulong)rpkt +
		rpkt->per_pkt_info_offset);
	len = rpkt->per_pkt_info_len;

	while (len > 0) {
		if (ppi->type == type)
			return (void *)((ulong)ppi + ppi->ppi_offset);
		len -= ppi->size;
		ppi = (struct rndis_per_packet_info *)((ulong)ppi + ppi->size);
	}

	return NULL;
}

static void rndis_filter_receive_data(struct rndis_device *dev,
				   struct rndis_message *msg,
				   struct hv_netvsc_packet *pkt)
{
	struct rndis_packet *rndis_pkt;
	u32 data_offset;
	struct ndis_pkt_8021q_info *vlan;

	rndis_pkt = &msg->msg.pkt;

	/* Remove the rndis header and pass it back up the stack */
	data_offset = RNDIS_HEADER_SIZE + rndis_pkt->data_offset;

	pkt->total_data_buflen -= data_offset;

	/*
	 * Make sure we got a valid RNDIS message, now total_data_buflen
	 * should be the data packet size plus the trailer padding size
	 */
	if (pkt->total_data_buflen < rndis_pkt->data_len) {
		netdev_err(dev->net_dev->ndev, "rndis message buffer "
			   "overflow detected (got %u, min %u)"
			   "...dropping this message!\n",
			   pkt->total_data_buflen, rndis_pkt->data_len);
		return;
	}

	/*
	 * Remove the rndis trailer padding from rndis packet message
	 * rndis_pkt->data_len tell us the real data length, we only copy
	 * the data packet to the stack, without the rndis trailer padding
	 */
	pkt->total_data_buflen = rndis_pkt->data_len;
	pkt->data = (void *)((unsigned long)pkt->data + data_offset);

	pkt->is_data_pkt = true;

	vlan = rndis_get_ppi(rndis_pkt, IEEE_8021Q_INFO);
	if (vlan) {
		pkt->vlan_tci = VLAN_TAG_PRESENT | vlan->vlanid |
			(vlan->pri << VLAN_PRIO_SHIFT);
	} else {
		pkt->vlan_tci = 0;
	}

	netvsc_recv_callback(dev->net_dev->dev, pkt);
}

int rndis_filter_receive(struct hv_device *dev,
				struct hv_netvsc_packet	*pkt)
{
	struct netvsc_device *net_dev = hv_get_drvdata(dev);
	struct rndis_device *rndis_dev;
	struct rndis_message *rndis_msg;
	struct net_device *ndev;
	int ret = 0;

	if (!net_dev) {
		ret = -EINVAL;
		goto exit;
	}

	ndev = net_dev->ndev;

	/* Make sure the rndis device state is initialized */
	if (!net_dev->extension) {
		netdev_err(ndev, "got rndis message but no rndis device - "
			  "dropping this message!\n");
		ret = -ENODEV;
		goto exit;
	}

	rndis_dev = (struct rndis_device *)net_dev->extension;
	if (rndis_dev->state == RNDIS_DEV_UNINITIALIZED) {
		netdev_err(ndev, "got rndis message but rndis device "
			   "uninitialized...dropping this message!\n");
		ret = -ENODEV;
		goto exit;
	}

	rndis_msg = pkt->data;

	dump_rndis_message(dev, rndis_msg);

	switch (rndis_msg->ndis_msg_type) {
	case RNDIS_MSG_PACKET:
		/* data msg */
		rndis_filter_receive_data(rndis_dev, rndis_msg, pkt);
		break;

	case RNDIS_MSG_INIT_C:
	case RNDIS_MSG_QUERY_C:
	case RNDIS_MSG_SET_C:
		/* completion msgs */
		rndis_filter_receive_response(rndis_dev, rndis_msg);
		break;

	case RNDIS_MSG_INDICATE:
		/* notification msgs */
		rndis_filter_receive_indicate_status(rndis_dev, rndis_msg);
		break;
	default:
		netdev_err(ndev,
			"unhandled rndis message (type %u len %u)\n",
			   rndis_msg->ndis_msg_type,
			   rndis_msg->msg_len);
		break;
	}

exit:
	if (ret != 0)
		pkt->status = NVSP_STAT_FAIL;

	return ret;
}

static int rndis_filter_query_device(struct rndis_device *dev, u32 oid,
				  void *result, u32 *result_size)
{
	struct rndis_request *request;
	u32 inresult_size = *result_size;
	struct rndis_query_request *query;
	struct rndis_query_complete *query_complete;
	int ret = 0;
	int t;

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

	ret = rndis_filter_send_request(dev, request);
	if (ret != 0)
		goto cleanup;

	t = wait_for_completion_timeout(&request->wait_event, 5*HZ);
	if (t == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	/* Copy the response back */
	query_complete = &request->response_msg.msg.query_complete;

	if (query_complete->info_buflen > inresult_size) {
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

static int rndis_filter_query_device_mac(struct rndis_device *dev)
{
	u32 size = ETH_ALEN;

	return rndis_filter_query_device(dev,
				      RNDIS_OID_802_3_PERMANENT_ADDRESS,
				      dev->hw_mac_adr, &size);
}

#define NWADR_STR "NetworkAddress"
#define NWADR_STRLEN 14

int rndis_filter_set_device_mac(struct hv_device *hdev, char *mac)
{
	struct netvsc_device *nvdev = hv_get_drvdata(hdev);
	struct rndis_device *rdev = nvdev->extension;
	struct net_device *ndev = nvdev->ndev;
	struct rndis_request *request;
	struct rndis_set_request *set;
	struct rndis_config_parameter_info *cpi;
	wchar_t *cfg_nwadr, *cfg_mac;
	struct rndis_set_complete *set_complete;
	char macstr[2*ETH_ALEN+1];
	u32 extlen = sizeof(struct rndis_config_parameter_info) +
		2*NWADR_STRLEN + 4*ETH_ALEN;
	int ret, t;

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

	t = wait_for_completion_timeout(&request->wait_event, 5*HZ);
	if (t == 0) {
		netdev_err(ndev, "timeout before we got a set response...\n");
		/*
		 * can't put_rndis_request, since we may still receive a
		 * send-completion.
		 */
		return -EBUSY;
	} else {
		set_complete = &request->response_msg.msg.set_complete;
		if (set_complete->status != RNDIS_STATUS_SUCCESS) {
			netdev_err(ndev, "Fail to set MAC on host side:0x%x\n",
				   set_complete->status);
			ret = -EINVAL;
		}
	}

cleanup:
	put_rndis_request(rdev, request);
	return ret;
}


static int rndis_filter_query_device_link_status(struct rndis_device *dev)
{
	u32 size = sizeof(u32);
	u32 link_status;
	int ret;

	ret = rndis_filter_query_device(dev,
				      RNDIS_OID_GEN_MEDIA_CONNECT_STATUS,
				      &link_status, &size);

	return ret;
}

int rndis_filter_set_packet_filter(struct rndis_device *dev, u32 new_filter)
{
	struct rndis_request *request;
	struct rndis_set_request *set;
	struct rndis_set_complete *set_complete;
	u32 status;
	int ret, t;
	struct net_device *ndev;

	ndev = dev->net_dev->ndev;

	request = get_rndis_request(dev, RNDIS_MSG_SET,
			RNDIS_MESSAGE_SIZE(struct rndis_set_request) +
			sizeof(u32));
	if (!request) {
		ret = -ENOMEM;
		goto cleanup;
	}

	/* Setup the rndis set */
	set = &request->request_msg.msg.set_req;
	set->oid = RNDIS_OID_GEN_CURRENT_PACKET_FILTER;
	set->info_buflen = sizeof(u32);
	set->info_buf_offset = sizeof(struct rndis_set_request);

	memcpy((void *)(unsigned long)set + sizeof(struct rndis_set_request),
	       &new_filter, sizeof(u32));

	ret = rndis_filter_send_request(dev, request);
	if (ret != 0)
		goto cleanup;

	t = wait_for_completion_timeout(&request->wait_event, 5*HZ);

	if (t == 0) {
		netdev_err(ndev,
			"timeout before we got a set response...\n");
		ret = -ETIMEDOUT;
		/*
		 * We can't deallocate the request since we may still receive a
		 * send completion for it.
		 */
		goto exit;
	} else {
		set_complete = &request->response_msg.msg.set_complete;
		status = set_complete->status;
	}

cleanup:
	if (request)
		put_rndis_request(dev, request);
exit:
	return ret;
}


static int rndis_filter_init_device(struct rndis_device *dev)
{
	struct rndis_request *request;
	struct rndis_initialize_request *init;
	struct rndis_initialize_complete *init_complete;
	u32 status;
	int ret, t;

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


	t = wait_for_completion_timeout(&request->wait_event, 5*HZ);

	if (t == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	init_complete = &request->response_msg.msg.init_complete;
	status = init_complete->status;
	if (status == RNDIS_STATUS_SUCCESS) {
		dev->state = RNDIS_DEV_INITIALIZED;
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

static void rndis_filter_halt_device(struct rndis_device *dev)
{
	struct rndis_request *request;
	struct rndis_halt_request *halt;
	struct netvsc_device *nvdev = dev->net_dev;
	struct hv_device *hdev = nvdev->dev;
	ulong flags;

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
	spin_lock_irqsave(&hdev->channel->inbound_lock, flags);
	nvdev->destroy = true;
	spin_unlock_irqrestore(&hdev->channel->inbound_lock, flags);

	/* Wait for all send completions */
	wait_event(nvdev->wait_drain,
		atomic_read(&nvdev->num_outstanding_sends) == 0);

	if (request)
		put_rndis_request(dev, request);
	return;
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

	ret = rndis_filter_set_packet_filter(dev, 0);
	if (ret == 0)
		dev->state = RNDIS_DEV_INITIALIZED;

	return ret;
}

int rndis_filter_device_add(struct hv_device *dev,
				  void *additional_info)
{
	int ret;
	struct netvsc_device *net_device;
	struct rndis_device *rndis_device;
	struct netvsc_device_info *device_info = additional_info;

	rndis_device = get_rndis_device();
	if (!rndis_device)
		return -ENODEV;

	/*
	 * Let the inner driver handle this first to create the netvsc channel
	 * NOTE! Once the channel is created, we may get a receive callback
	 * (RndisFilterOnReceive()) before this call is completed
	 */
	ret = netvsc_device_add(dev, additional_info);
	if (ret != 0) {
		kfree(rndis_device);
		return ret;
	}


	/* Initialize the rndis device */
	net_device = hv_get_drvdata(dev);

	net_device->extension = rndis_device;
	rndis_device->net_dev = net_device;

	/* Send the rndis initialization message */
	ret = rndis_filter_init_device(rndis_device);
	if (ret != 0) {
		rndis_filter_device_remove(dev);
		return ret;
	}

	/* Get the mac address */
	ret = rndis_filter_query_device_mac(rndis_device);
	if (ret != 0) {
		rndis_filter_device_remove(dev);
		return ret;
	}

	memcpy(device_info->mac_adr, rndis_device->hw_mac_adr, ETH_ALEN);

	rndis_filter_query_device_link_status(rndis_device);

	device_info->link_state = rndis_device->link_state;

	dev_info(&dev->device, "Device MAC %pM link state %s\n",
		 rndis_device->hw_mac_adr,
		 device_info->link_state ? "down" : "up");

	return ret;
}

void rndis_filter_device_remove(struct hv_device *dev)
{
	struct netvsc_device *net_dev = hv_get_drvdata(dev);
	struct rndis_device *rndis_dev = net_dev->extension;

	/* Halt and release the rndis device */
	rndis_filter_halt_device(rndis_dev);

	kfree(rndis_dev);
	net_dev->extension = NULL;

	netvsc_device_remove(dev);
}


int rndis_filter_open(struct hv_device *dev)
{
	struct netvsc_device *net_device = hv_get_drvdata(dev);

	if (!net_device)
		return -EINVAL;

	return rndis_filter_open_device(net_device->extension);
}

int rndis_filter_close(struct hv_device *dev)
{
	struct netvsc_device *nvdev = hv_get_drvdata(dev);

	if (!nvdev)
		return -EINVAL;

	return rndis_filter_close_device(nvdev->extension);
}

int rndis_filter_send(struct hv_device *dev,
			     struct hv_netvsc_packet *pkt)
{
	int ret;
	struct rndis_filter_packet *filter_pkt;
	struct rndis_message *rndis_msg;
	struct rndis_packet *rndis_pkt;
	u32 rndis_msg_size;
	bool isvlan = pkt->vlan_tci & VLAN_TAG_PRESENT;

	/* Add the rndis header */
	filter_pkt = (struct rndis_filter_packet *)pkt->extension;

	rndis_msg = &filter_pkt->msg;
	rndis_msg_size = RNDIS_MESSAGE_SIZE(struct rndis_packet);
	if (isvlan)
		rndis_msg_size += NDIS_VLAN_PPI_SIZE;

	rndis_msg->ndis_msg_type = RNDIS_MSG_PACKET;
	rndis_msg->msg_len = pkt->total_data_buflen +
				      rndis_msg_size;

	rndis_pkt = &rndis_msg->msg.pkt;
	rndis_pkt->data_offset = sizeof(struct rndis_packet);
	if (isvlan)
		rndis_pkt->data_offset += NDIS_VLAN_PPI_SIZE;
	rndis_pkt->data_len = pkt->total_data_buflen;

	if (isvlan) {
		struct rndis_per_packet_info *ppi;
		struct ndis_pkt_8021q_info *vlan;

		rndis_pkt->per_pkt_info_offset = sizeof(struct rndis_packet);
		rndis_pkt->per_pkt_info_len = NDIS_VLAN_PPI_SIZE;

		ppi = (struct rndis_per_packet_info *)((ulong)rndis_pkt +
			rndis_pkt->per_pkt_info_offset);
		ppi->size = NDIS_VLAN_PPI_SIZE;
		ppi->type = IEEE_8021Q_INFO;
		ppi->ppi_offset = sizeof(struct rndis_per_packet_info);

		vlan = (struct ndis_pkt_8021q_info *)((ulong)ppi +
			ppi->ppi_offset);
		vlan->vlanid = pkt->vlan_tci & VLAN_VID_MASK;
		vlan->pri = (pkt->vlan_tci & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
	}

	pkt->is_data_pkt = true;
	pkt->page_buf[0].pfn = virt_to_phys(rndis_msg) >> PAGE_SHIFT;
	pkt->page_buf[0].offset =
			(unsigned long)rndis_msg & (PAGE_SIZE-1);
	pkt->page_buf[0].len = rndis_msg_size;

	/* Add one page_buf if the rndis msg goes beyond page boundary */
	if (pkt->page_buf[0].offset + rndis_msg_size > PAGE_SIZE) {
		int i;
		for (i = pkt->page_buf_cnt; i > 1; i--)
			pkt->page_buf[i] = pkt->page_buf[i-1];
		pkt->page_buf_cnt++;
		pkt->page_buf[0].len = PAGE_SIZE - pkt->page_buf[0].offset;
		pkt->page_buf[1].pfn = virt_to_phys((void *)((ulong)
			rndis_msg + pkt->page_buf[0].len)) >> PAGE_SHIFT;
		pkt->page_buf[1].offset = 0;
		pkt->page_buf[1].len = rndis_msg_size - pkt->page_buf[0].len;
	}

	/* Save the packet send completion and context */
	filter_pkt->completion = pkt->completion.send.send_completion;
	filter_pkt->completion_ctx =
				pkt->completion.send.send_completion_ctx;

	/* Use ours */
	pkt->completion.send.send_completion = rndis_filter_send_completion;
	pkt->completion.send.send_completion_ctx = filter_pkt;

	ret = netvsc_send(dev, pkt);
	if (ret != 0) {
		/*
		 * Reset the completion to originals to allow retries from
		 * above
		 */
		pkt->completion.send.send_completion =
				filter_pkt->completion;
		pkt->completion.send.send_completion_ctx =
				filter_pkt->completion_ctx;
	}

	return ret;
}

static void rndis_filter_send_completion(void *ctx)
{
	struct rndis_filter_packet *filter_pkt = ctx;

	/* Pass it back to the original handler */
	filter_pkt->completion(filter_pkt->completion_ctx);
}
