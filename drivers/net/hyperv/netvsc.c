// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/vmalloc.h>
#include <linux/rtnetlink.h>
#include <linux/prefetch.h>

#include <asm/sync_bitops.h>

#include "hyperv_net.h"
#include "netvsc_trace.h"

/*
 * Switch the data path from the synthetic interface to the VF
 * interface.
 */
void netvsc_switch_datapath(struct net_device *ndev, bool vf)
{
	struct net_device_context *net_device_ctx = netdev_priv(ndev);
	struct hv_device *dev = net_device_ctx->device_ctx;
	struct netvsc_device *nv_dev = rtnl_dereference(net_device_ctx->nvdev);
	struct nvsp_message *init_pkt = &nv_dev->channel_init_pkt;

	memset(init_pkt, 0, sizeof(struct nvsp_message));
	init_pkt->hdr.msg_type = NVSP_MSG4_TYPE_SWITCH_DATA_PATH;
	if (vf)
		init_pkt->msg.v4_msg.active_dp.active_datapath =
			NVSP_DATAPATH_VF;
	else
		init_pkt->msg.v4_msg.active_dp.active_datapath =
			NVSP_DATAPATH_SYNTHETIC;

	trace_nvsp_send(ndev, init_pkt);

	vmbus_sendpacket(dev->channel, init_pkt,
			       sizeof(struct nvsp_message),
			       VMBUS_RQST_ID_NO_RESPONSE,
			       VM_PKT_DATA_INBAND, 0);
}

/* Worker to setup sub channels on initial setup
 * Initial hotplug event occurs in softirq context
 * and can't wait for channels.
 */
static void netvsc_subchan_work(struct work_struct *w)
{
	struct netvsc_device *nvdev =
		container_of(w, struct netvsc_device, subchan_work);
	struct rndis_device *rdev;
	int i, ret;

	/* Avoid deadlock with device removal already under RTNL */
	if (!rtnl_trylock()) {
		schedule_work(w);
		return;
	}

	rdev = nvdev->extension;
	if (rdev) {
		ret = rndis_set_subchannel(rdev->ndev, nvdev, NULL);
		if (ret == 0) {
			netif_device_attach(rdev->ndev);
		} else {
			/* fallback to only primary channel */
			for (i = 1; i < nvdev->num_chn; i++)
				netif_napi_del(&nvdev->chan_table[i].napi);

			nvdev->max_chn = 1;
			nvdev->num_chn = 1;
		}
	}

	rtnl_unlock();
}

static struct netvsc_device *alloc_net_device(void)
{
	struct netvsc_device *net_device;

	net_device = kzalloc(sizeof(struct netvsc_device), GFP_KERNEL);
	if (!net_device)
		return NULL;

	init_waitqueue_head(&net_device->wait_drain);
	net_device->destroy = false;
	net_device->tx_disable = true;

	net_device->max_pkt = RNDIS_MAX_PKT_DEFAULT;
	net_device->pkt_align = RNDIS_PKT_ALIGN_DEFAULT;

	init_completion(&net_device->channel_init_wait);
	init_waitqueue_head(&net_device->subchan_open);
	INIT_WORK(&net_device->subchan_work, netvsc_subchan_work);

	return net_device;
}

static void free_netvsc_device(struct rcu_head *head)
{
	struct netvsc_device *nvdev
		= container_of(head, struct netvsc_device, rcu);
	int i;

	kfree(nvdev->extension);
	vfree(nvdev->recv_buf);
	vfree(nvdev->send_buf);
	kfree(nvdev->send_section_map);

	for (i = 0; i < VRSS_CHANNEL_MAX; i++) {
		xdp_rxq_info_unreg(&nvdev->chan_table[i].xdp_rxq);
		vfree(nvdev->chan_table[i].mrc.slots);
	}

	kfree(nvdev);
}

static void free_netvsc_device_rcu(struct netvsc_device *nvdev)
{
	call_rcu(&nvdev->rcu, free_netvsc_device);
}

static void netvsc_revoke_recv_buf(struct hv_device *device,
				   struct netvsc_device *net_device,
				   struct net_device *ndev)
{
	struct nvsp_message *revoke_packet;
	int ret;

	/*
	 * If we got a section count, it means we received a
	 * SendReceiveBufferComplete msg (ie sent
	 * NvspMessage1TypeSendReceiveBuffer msg) therefore, we need
	 * to send a revoke msg here
	 */
	if (net_device->recv_section_cnt) {
		/* Send the revoke receive buffer */
		revoke_packet = &net_device->revoke_packet;
		memset(revoke_packet, 0, sizeof(struct nvsp_message));

		revoke_packet->hdr.msg_type =
			NVSP_MSG1_TYPE_REVOKE_RECV_BUF;
		revoke_packet->msg.v1_msg.
		revoke_recv_buf.id = NETVSC_RECEIVE_BUFFER_ID;

		trace_nvsp_send(ndev, revoke_packet);

		ret = vmbus_sendpacket(device->channel,
				       revoke_packet,
				       sizeof(struct nvsp_message),
				       VMBUS_RQST_ID_NO_RESPONSE,
				       VM_PKT_DATA_INBAND, 0);
		/* If the failure is because the channel is rescinded;
		 * ignore the failure since we cannot send on a rescinded
		 * channel. This would allow us to properly cleanup
		 * even when the channel is rescinded.
		 */
		if (device->channel->rescind)
			ret = 0;
		/*
		 * If we failed here, we might as well return and
		 * have a leak rather than continue and a bugchk
		 */
		if (ret != 0) {
			netdev_err(ndev, "unable to send "
				"revoke receive buffer to netvsp\n");
			return;
		}
		net_device->recv_section_cnt = 0;
	}
}

static void netvsc_revoke_send_buf(struct hv_device *device,
				   struct netvsc_device *net_device,
				   struct net_device *ndev)
{
	struct nvsp_message *revoke_packet;
	int ret;

	/* Deal with the send buffer we may have setup.
	 * If we got a  send section size, it means we received a
	 * NVSP_MSG1_TYPE_SEND_SEND_BUF_COMPLETE msg (ie sent
	 * NVSP_MSG1_TYPE_SEND_SEND_BUF msg) therefore, we need
	 * to send a revoke msg here
	 */
	if (net_device->send_section_cnt) {
		/* Send the revoke receive buffer */
		revoke_packet = &net_device->revoke_packet;
		memset(revoke_packet, 0, sizeof(struct nvsp_message));

		revoke_packet->hdr.msg_type =
			NVSP_MSG1_TYPE_REVOKE_SEND_BUF;
		revoke_packet->msg.v1_msg.revoke_send_buf.id =
			NETVSC_SEND_BUFFER_ID;

		trace_nvsp_send(ndev, revoke_packet);

		ret = vmbus_sendpacket(device->channel,
				       revoke_packet,
				       sizeof(struct nvsp_message),
				       VMBUS_RQST_ID_NO_RESPONSE,
				       VM_PKT_DATA_INBAND, 0);

		/* If the failure is because the channel is rescinded;
		 * ignore the failure since we cannot send on a rescinded
		 * channel. This would allow us to properly cleanup
		 * even when the channel is rescinded.
		 */
		if (device->channel->rescind)
			ret = 0;

		/* If we failed here, we might as well return and
		 * have a leak rather than continue and a bugchk
		 */
		if (ret != 0) {
			netdev_err(ndev, "unable to send "
				   "revoke send buffer to netvsp\n");
			return;
		}
		net_device->send_section_cnt = 0;
	}
}

static void netvsc_teardown_recv_gpadl(struct hv_device *device,
				       struct netvsc_device *net_device,
				       struct net_device *ndev)
{
	int ret;

	if (net_device->recv_buf_gpadl_handle) {
		ret = vmbus_teardown_gpadl(device->channel,
					   net_device->recv_buf_gpadl_handle);

		/* If we failed here, we might as well return and have a leak
		 * rather than continue and a bugchk
		 */
		if (ret != 0) {
			netdev_err(ndev,
				   "unable to teardown receive buffer's gpadl\n");
			return;
		}
		net_device->recv_buf_gpadl_handle = 0;
	}
}

static void netvsc_teardown_send_gpadl(struct hv_device *device,
				       struct netvsc_device *net_device,
				       struct net_device *ndev)
{
	int ret;

	if (net_device->send_buf_gpadl_handle) {
		ret = vmbus_teardown_gpadl(device->channel,
					   net_device->send_buf_gpadl_handle);

		/* If we failed here, we might as well return and have a leak
		 * rather than continue and a bugchk
		 */
		if (ret != 0) {
			netdev_err(ndev,
				   "unable to teardown send buffer's gpadl\n");
			return;
		}
		net_device->send_buf_gpadl_handle = 0;
	}
}

int netvsc_alloc_recv_comp_ring(struct netvsc_device *net_device, u32 q_idx)
{
	struct netvsc_channel *nvchan = &net_device->chan_table[q_idx];
	int node = cpu_to_node(nvchan->channel->target_cpu);
	size_t size;

	size = net_device->recv_completion_cnt * sizeof(struct recv_comp_data);
	nvchan->mrc.slots = vzalloc_node(size, node);
	if (!nvchan->mrc.slots)
		nvchan->mrc.slots = vzalloc(size);

	return nvchan->mrc.slots ? 0 : -ENOMEM;
}

static int netvsc_init_buf(struct hv_device *device,
			   struct netvsc_device *net_device,
			   const struct netvsc_device_info *device_info)
{
	struct nvsp_1_message_send_receive_buffer_complete *resp;
	struct net_device *ndev = hv_get_drvdata(device);
	struct nvsp_message *init_packet;
	unsigned int buf_size;
	size_t map_words;
	int ret = 0;

	/* Get receive buffer area. */
	buf_size = device_info->recv_sections * device_info->recv_section_size;
	buf_size = roundup(buf_size, PAGE_SIZE);

	/* Legacy hosts only allow smaller receive buffer */
	if (net_device->nvsp_version <= NVSP_PROTOCOL_VERSION_2)
		buf_size = min_t(unsigned int, buf_size,
				 NETVSC_RECEIVE_BUFFER_SIZE_LEGACY);

	net_device->recv_buf = vzalloc(buf_size);
	if (!net_device->recv_buf) {
		netdev_err(ndev,
			   "unable to allocate receive buffer of size %u\n",
			   buf_size);
		ret = -ENOMEM;
		goto cleanup;
	}

	net_device->recv_buf_size = buf_size;

	/*
	 * Establish the gpadl handle for this buffer on this
	 * channel.  Note: This call uses the vmbus connection rather
	 * than the channel to establish the gpadl handle.
	 */
	ret = vmbus_establish_gpadl(device->channel, net_device->recv_buf,
				    buf_size,
				    &net_device->recv_buf_gpadl_handle);
	if (ret != 0) {
		netdev_err(ndev,
			"unable to establish receive buffer's gpadl\n");
		goto cleanup;
	}

	/* Notify the NetVsp of the gpadl handle */
	init_packet = &net_device->channel_init_pkt;
	memset(init_packet, 0, sizeof(struct nvsp_message));
	init_packet->hdr.msg_type = NVSP_MSG1_TYPE_SEND_RECV_BUF;
	init_packet->msg.v1_msg.send_recv_buf.
		gpadl_handle = net_device->recv_buf_gpadl_handle;
	init_packet->msg.v1_msg.
		send_recv_buf.id = NETVSC_RECEIVE_BUFFER_ID;

	trace_nvsp_send(ndev, init_packet);

	/* Send the gpadl notification request */
	ret = vmbus_sendpacket(device->channel, init_packet,
			       sizeof(struct nvsp_message),
			       (unsigned long)init_packet,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0) {
		netdev_err(ndev,
			"unable to send receive buffer's gpadl to netvsp\n");
		goto cleanup;
	}

	wait_for_completion(&net_device->channel_init_wait);

	/* Check the response */
	resp = &init_packet->msg.v1_msg.send_recv_buf_complete;
	if (resp->status != NVSP_STAT_SUCCESS) {
		netdev_err(ndev,
			   "Unable to complete receive buffer initialization with NetVsp - status %d\n",
			   resp->status);
		ret = -EINVAL;
		goto cleanup;
	}

	/* Parse the response */
	netdev_dbg(ndev, "Receive sections: %u sub_allocs: size %u count: %u\n",
		   resp->num_sections, resp->sections[0].sub_alloc_size,
		   resp->sections[0].num_sub_allocs);

	/* There should only be one section for the entire receive buffer */
	if (resp->num_sections != 1 || resp->sections[0].offset != 0) {
		ret = -EINVAL;
		goto cleanup;
	}

	net_device->recv_section_size = resp->sections[0].sub_alloc_size;
	net_device->recv_section_cnt = resp->sections[0].num_sub_allocs;

	/* Ensure buffer will not overflow */
	if (net_device->recv_section_size < NETVSC_MTU_MIN || (u64)net_device->recv_section_size *
	    (u64)net_device->recv_section_cnt > (u64)buf_size) {
		netdev_err(ndev, "invalid recv_section_size %u\n",
			   net_device->recv_section_size);
		ret = -EINVAL;
		goto cleanup;
	}

	/* Setup receive completion ring.
	 * Add 1 to the recv_section_cnt because at least one entry in a
	 * ring buffer has to be empty.
	 */
	net_device->recv_completion_cnt = net_device->recv_section_cnt + 1;
	ret = netvsc_alloc_recv_comp_ring(net_device, 0);
	if (ret)
		goto cleanup;

	/* Now setup the send buffer. */
	buf_size = device_info->send_sections * device_info->send_section_size;
	buf_size = round_up(buf_size, PAGE_SIZE);

	net_device->send_buf = vzalloc(buf_size);
	if (!net_device->send_buf) {
		netdev_err(ndev, "unable to allocate send buffer of size %u\n",
			   buf_size);
		ret = -ENOMEM;
		goto cleanup;
	}

	/* Establish the gpadl handle for this buffer on this
	 * channel.  Note: This call uses the vmbus connection rather
	 * than the channel to establish the gpadl handle.
	 */
	ret = vmbus_establish_gpadl(device->channel, net_device->send_buf,
				    buf_size,
				    &net_device->send_buf_gpadl_handle);
	if (ret != 0) {
		netdev_err(ndev,
			   "unable to establish send buffer's gpadl\n");
		goto cleanup;
	}

	/* Notify the NetVsp of the gpadl handle */
	init_packet = &net_device->channel_init_pkt;
	memset(init_packet, 0, sizeof(struct nvsp_message));
	init_packet->hdr.msg_type = NVSP_MSG1_TYPE_SEND_SEND_BUF;
	init_packet->msg.v1_msg.send_send_buf.gpadl_handle =
		net_device->send_buf_gpadl_handle;
	init_packet->msg.v1_msg.send_send_buf.id = NETVSC_SEND_BUFFER_ID;

	trace_nvsp_send(ndev, init_packet);

	/* Send the gpadl notification request */
	ret = vmbus_sendpacket(device->channel, init_packet,
			       sizeof(struct nvsp_message),
			       (unsigned long)init_packet,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0) {
		netdev_err(ndev,
			   "unable to send send buffer's gpadl to netvsp\n");
		goto cleanup;
	}

	wait_for_completion(&net_device->channel_init_wait);

	/* Check the response */
	if (init_packet->msg.v1_msg.
	    send_send_buf_complete.status != NVSP_STAT_SUCCESS) {
		netdev_err(ndev, "Unable to complete send buffer "
			   "initialization with NetVsp - status %d\n",
			   init_packet->msg.v1_msg.
			   send_send_buf_complete.status);
		ret = -EINVAL;
		goto cleanup;
	}

	/* Parse the response */
	net_device->send_section_size = init_packet->msg.
				v1_msg.send_send_buf_complete.section_size;
	if (net_device->send_section_size < NETVSC_MTU_MIN) {
		netdev_err(ndev, "invalid send_section_size %u\n",
			   net_device->send_section_size);
		ret = -EINVAL;
		goto cleanup;
	}

	/* Section count is simply the size divided by the section size. */
	net_device->send_section_cnt = buf_size / net_device->send_section_size;

	netdev_dbg(ndev, "Send section size: %d, Section count:%d\n",
		   net_device->send_section_size, net_device->send_section_cnt);

	/* Setup state for managing the send buffer. */
	map_words = DIV_ROUND_UP(net_device->send_section_cnt, BITS_PER_LONG);

	net_device->send_section_map = kcalloc(map_words, sizeof(ulong), GFP_KERNEL);
	if (net_device->send_section_map == NULL) {
		ret = -ENOMEM;
		goto cleanup;
	}

	goto exit;

cleanup:
	netvsc_revoke_recv_buf(device, net_device, ndev);
	netvsc_revoke_send_buf(device, net_device, ndev);
	netvsc_teardown_recv_gpadl(device, net_device, ndev);
	netvsc_teardown_send_gpadl(device, net_device, ndev);

exit:
	return ret;
}

/* Negotiate NVSP protocol version */
static int negotiate_nvsp_ver(struct hv_device *device,
			      struct netvsc_device *net_device,
			      struct nvsp_message *init_packet,
			      u32 nvsp_ver)
{
	struct net_device *ndev = hv_get_drvdata(device);
	int ret;

	memset(init_packet, 0, sizeof(struct nvsp_message));
	init_packet->hdr.msg_type = NVSP_MSG_TYPE_INIT;
	init_packet->msg.init_msg.init.min_protocol_ver = nvsp_ver;
	init_packet->msg.init_msg.init.max_protocol_ver = nvsp_ver;
	trace_nvsp_send(ndev, init_packet);

	/* Send the init request */
	ret = vmbus_sendpacket(device->channel, init_packet,
			       sizeof(struct nvsp_message),
			       (unsigned long)init_packet,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);

	if (ret != 0)
		return ret;

	wait_for_completion(&net_device->channel_init_wait);

	if (init_packet->msg.init_msg.init_complete.status !=
	    NVSP_STAT_SUCCESS)
		return -EINVAL;

	if (nvsp_ver == NVSP_PROTOCOL_VERSION_1)
		return 0;

	/* NVSPv2 or later: Send NDIS config */
	memset(init_packet, 0, sizeof(struct nvsp_message));
	init_packet->hdr.msg_type = NVSP_MSG2_TYPE_SEND_NDIS_CONFIG;
	init_packet->msg.v2_msg.send_ndis_config.mtu = ndev->mtu + ETH_HLEN;
	init_packet->msg.v2_msg.send_ndis_config.capability.ieee8021q = 1;

	if (nvsp_ver >= NVSP_PROTOCOL_VERSION_5) {
		init_packet->msg.v2_msg.send_ndis_config.capability.sriov = 1;

		/* Teaming bit is needed to receive link speed updates */
		init_packet->msg.v2_msg.send_ndis_config.capability.teaming = 1;
	}

	if (nvsp_ver >= NVSP_PROTOCOL_VERSION_61)
		init_packet->msg.v2_msg.send_ndis_config.capability.rsc = 1;

	trace_nvsp_send(ndev, init_packet);

	ret = vmbus_sendpacket(device->channel, init_packet,
				sizeof(struct nvsp_message),
				VMBUS_RQST_ID_NO_RESPONSE,
				VM_PKT_DATA_INBAND, 0);

	return ret;
}

static int netvsc_connect_vsp(struct hv_device *device,
			      struct netvsc_device *net_device,
			      const struct netvsc_device_info *device_info)
{
	struct net_device *ndev = hv_get_drvdata(device);
	static const u32 ver_list[] = {
		NVSP_PROTOCOL_VERSION_1, NVSP_PROTOCOL_VERSION_2,
		NVSP_PROTOCOL_VERSION_4, NVSP_PROTOCOL_VERSION_5,
		NVSP_PROTOCOL_VERSION_6, NVSP_PROTOCOL_VERSION_61
	};
	struct nvsp_message *init_packet;
	int ndis_version, i, ret;

	init_packet = &net_device->channel_init_pkt;

	/* Negotiate the latest NVSP protocol supported */
	for (i = ARRAY_SIZE(ver_list) - 1; i >= 0; i--)
		if (negotiate_nvsp_ver(device, net_device, init_packet,
				       ver_list[i])  == 0) {
			net_device->nvsp_version = ver_list[i];
			break;
		}

	if (i < 0) {
		ret = -EPROTO;
		goto cleanup;
	}

	pr_debug("Negotiated NVSP version:%x\n", net_device->nvsp_version);

	/* Send the ndis version */
	memset(init_packet, 0, sizeof(struct nvsp_message));

	if (net_device->nvsp_version <= NVSP_PROTOCOL_VERSION_4)
		ndis_version = 0x00060001;
	else
		ndis_version = 0x0006001e;

	init_packet->hdr.msg_type = NVSP_MSG1_TYPE_SEND_NDIS_VER;
	init_packet->msg.v1_msg.
		send_ndis_ver.ndis_major_ver =
				(ndis_version & 0xFFFF0000) >> 16;
	init_packet->msg.v1_msg.
		send_ndis_ver.ndis_minor_ver =
				ndis_version & 0xFFFF;

	trace_nvsp_send(ndev, init_packet);

	/* Send the init request */
	ret = vmbus_sendpacket(device->channel, init_packet,
				sizeof(struct nvsp_message),
				VMBUS_RQST_ID_NO_RESPONSE,
				VM_PKT_DATA_INBAND, 0);
	if (ret != 0)
		goto cleanup;


	ret = netvsc_init_buf(device, net_device, device_info);

cleanup:
	return ret;
}

/*
 * netvsc_device_remove - Callback when the root bus device is removed
 */
void netvsc_device_remove(struct hv_device *device)
{
	struct net_device *ndev = hv_get_drvdata(device);
	struct net_device_context *net_device_ctx = netdev_priv(ndev);
	struct netvsc_device *net_device
		= rtnl_dereference(net_device_ctx->nvdev);
	int i;

	/*
	 * Revoke receive buffer. If host is pre-Win2016 then tear down
	 * receive buffer GPADL. Do the same for send buffer.
	 */
	netvsc_revoke_recv_buf(device, net_device, ndev);
	if (vmbus_proto_version < VERSION_WIN10)
		netvsc_teardown_recv_gpadl(device, net_device, ndev);

	netvsc_revoke_send_buf(device, net_device, ndev);
	if (vmbus_proto_version < VERSION_WIN10)
		netvsc_teardown_send_gpadl(device, net_device, ndev);

	RCU_INIT_POINTER(net_device_ctx->nvdev, NULL);

	/* Disable NAPI and disassociate its context from the device. */
	for (i = 0; i < net_device->num_chn; i++) {
		/* See also vmbus_reset_channel_cb(). */
		napi_disable(&net_device->chan_table[i].napi);
		netif_napi_del(&net_device->chan_table[i].napi);
	}

	/*
	 * At this point, no one should be accessing net_device
	 * except in here
	 */
	netdev_dbg(ndev, "net device safe to remove\n");

	/* Now, we can close the channel safely */
	vmbus_close(device->channel);

	/*
	 * If host is Win2016 or higher then we do the GPADL tear down
	 * here after VMBus is closed.
	*/
	if (vmbus_proto_version >= VERSION_WIN10) {
		netvsc_teardown_recv_gpadl(device, net_device, ndev);
		netvsc_teardown_send_gpadl(device, net_device, ndev);
	}

	/* Release all resources */
	free_netvsc_device_rcu(net_device);
}

#define RING_AVAIL_PERCENT_HIWATER 20
#define RING_AVAIL_PERCENT_LOWATER 10

static inline void netvsc_free_send_slot(struct netvsc_device *net_device,
					 u32 index)
{
	sync_change_bit(index, net_device->send_section_map);
}

static void netvsc_send_tx_complete(struct net_device *ndev,
				    struct netvsc_device *net_device,
				    struct vmbus_channel *channel,
				    const struct vmpacket_descriptor *desc,
				    int budget)
{
	struct net_device_context *ndev_ctx = netdev_priv(ndev);
	struct sk_buff *skb;
	u16 q_idx = 0;
	int queue_sends;
	u64 cmd_rqst;

	cmd_rqst = vmbus_request_addr(&channel->requestor, (u64)desc->trans_id);
	if (cmd_rqst == VMBUS_RQST_ERROR) {
		netdev_err(ndev, "Incorrect transaction id\n");
		return;
	}

	skb = (struct sk_buff *)(unsigned long)cmd_rqst;

	/* Notify the layer above us */
	if (likely(skb)) {
		const struct hv_netvsc_packet *packet
			= (struct hv_netvsc_packet *)skb->cb;
		u32 send_index = packet->send_buf_index;
		struct netvsc_stats *tx_stats;

		if (send_index != NETVSC_INVALID_INDEX)
			netvsc_free_send_slot(net_device, send_index);
		q_idx = packet->q_idx;

		tx_stats = &net_device->chan_table[q_idx].tx_stats;

		u64_stats_update_begin(&tx_stats->syncp);
		tx_stats->packets += packet->total_packets;
		tx_stats->bytes += packet->total_bytes;
		u64_stats_update_end(&tx_stats->syncp);

		napi_consume_skb(skb, budget);
	}

	queue_sends =
		atomic_dec_return(&net_device->chan_table[q_idx].queue_sends);

	if (unlikely(net_device->destroy)) {
		if (queue_sends == 0)
			wake_up(&net_device->wait_drain);
	} else {
		struct netdev_queue *txq = netdev_get_tx_queue(ndev, q_idx);

		if (netif_tx_queue_stopped(txq) && !net_device->tx_disable &&
		    (hv_get_avail_to_write_percent(&channel->outbound) >
		     RING_AVAIL_PERCENT_HIWATER || queue_sends < 1)) {
			netif_tx_wake_queue(txq);
			ndev_ctx->eth_stats.wake_queue++;
		}
	}
}

static void netvsc_send_completion(struct net_device *ndev,
				   struct netvsc_device *net_device,
				   struct vmbus_channel *incoming_channel,
				   const struct vmpacket_descriptor *desc,
				   int budget)
{
	const struct nvsp_message *nvsp_packet = hv_pkt_data(desc);
	u32 msglen = hv_pkt_datalen(desc);

	/* Ensure packet is big enough to read header fields */
	if (msglen < sizeof(struct nvsp_message_header)) {
		netdev_err(ndev, "nvsp_message length too small: %u\n", msglen);
		return;
	}

	switch (nvsp_packet->hdr.msg_type) {
	case NVSP_MSG_TYPE_INIT_COMPLETE:
		if (msglen < sizeof(struct nvsp_message_header) +
				sizeof(struct nvsp_message_init_complete)) {
			netdev_err(ndev, "nvsp_msg length too small: %u\n",
				   msglen);
			return;
		}
		fallthrough;

	case NVSP_MSG1_TYPE_SEND_RECV_BUF_COMPLETE:
		if (msglen < sizeof(struct nvsp_message_header) +
				sizeof(struct nvsp_1_message_send_receive_buffer_complete)) {
			netdev_err(ndev, "nvsp_msg1 length too small: %u\n",
				   msglen);
			return;
		}
		fallthrough;

	case NVSP_MSG1_TYPE_SEND_SEND_BUF_COMPLETE:
		if (msglen < sizeof(struct nvsp_message_header) +
				sizeof(struct nvsp_1_message_send_send_buffer_complete)) {
			netdev_err(ndev, "nvsp_msg1 length too small: %u\n",
				   msglen);
			return;
		}
		fallthrough;

	case NVSP_MSG5_TYPE_SUBCHANNEL:
		if (msglen < sizeof(struct nvsp_message_header) +
				sizeof(struct nvsp_5_subchannel_complete)) {
			netdev_err(ndev, "nvsp_msg5 length too small: %u\n",
				   msglen);
			return;
		}
		/* Copy the response back */
		memcpy(&net_device->channel_init_pkt, nvsp_packet,
		       sizeof(struct nvsp_message));
		complete(&net_device->channel_init_wait);
		break;

	case NVSP_MSG1_TYPE_SEND_RNDIS_PKT_COMPLETE:
		netvsc_send_tx_complete(ndev, net_device, incoming_channel,
					desc, budget);
		break;

	default:
		netdev_err(ndev,
			   "Unknown send completion type %d received!!\n",
			   nvsp_packet->hdr.msg_type);
	}
}

static u32 netvsc_get_next_send_section(struct netvsc_device *net_device)
{
	unsigned long *map_addr = net_device->send_section_map;
	unsigned int i;

	for_each_clear_bit(i, map_addr, net_device->send_section_cnt) {
		if (sync_test_and_set_bit(i, map_addr) == 0)
			return i;
	}

	return NETVSC_INVALID_INDEX;
}

static void netvsc_copy_to_send_buf(struct netvsc_device *net_device,
				    unsigned int section_index,
				    u32 pend_size,
				    struct hv_netvsc_packet *packet,
				    struct rndis_message *rndis_msg,
				    struct hv_page_buffer *pb,
				    bool xmit_more)
{
	char *start = net_device->send_buf;
	char *dest = start + (section_index * net_device->send_section_size)
		     + pend_size;
	int i;
	u32 padding = 0;
	u32 page_count = packet->cp_partial ? packet->rmsg_pgcnt :
		packet->page_buf_cnt;
	u32 remain;

	/* Add padding */
	remain = packet->total_data_buflen & (net_device->pkt_align - 1);
	if (xmit_more && remain) {
		padding = net_device->pkt_align - remain;
		rndis_msg->msg_len += padding;
		packet->total_data_buflen += padding;
	}

	for (i = 0; i < page_count; i++) {
		char *src = phys_to_virt(pb[i].pfn << HV_HYP_PAGE_SHIFT);
		u32 offset = pb[i].offset;
		u32 len = pb[i].len;

		memcpy(dest, (src + offset), len);
		dest += len;
	}

	if (padding)
		memset(dest, 0, padding);
}

static inline int netvsc_send_pkt(
	struct hv_device *device,
	struct hv_netvsc_packet *packet,
	struct netvsc_device *net_device,
	struct hv_page_buffer *pb,
	struct sk_buff *skb)
{
	struct nvsp_message nvmsg;
	struct nvsp_1_message_send_rndis_packet *rpkt =
		&nvmsg.msg.v1_msg.send_rndis_pkt;
	struct netvsc_channel * const nvchan =
		&net_device->chan_table[packet->q_idx];
	struct vmbus_channel *out_channel = nvchan->channel;
	struct net_device *ndev = hv_get_drvdata(device);
	struct net_device_context *ndev_ctx = netdev_priv(ndev);
	struct netdev_queue *txq = netdev_get_tx_queue(ndev, packet->q_idx);
	u64 req_id;
	int ret;
	u32 ring_avail = hv_get_avail_to_write_percent(&out_channel->outbound);

	nvmsg.hdr.msg_type = NVSP_MSG1_TYPE_SEND_RNDIS_PKT;
	if (skb)
		rpkt->channel_type = 0;		/* 0 is RMC_DATA */
	else
		rpkt->channel_type = 1;		/* 1 is RMC_CONTROL */

	rpkt->send_buf_section_index = packet->send_buf_index;
	if (packet->send_buf_index == NETVSC_INVALID_INDEX)
		rpkt->send_buf_section_size = 0;
	else
		rpkt->send_buf_section_size = packet->total_data_buflen;

	req_id = (ulong)skb;

	if (out_channel->rescind)
		return -ENODEV;

	trace_nvsp_send_pkt(ndev, out_channel, rpkt);

	if (packet->page_buf_cnt) {
		if (packet->cp_partial)
			pb += packet->rmsg_pgcnt;

		ret = vmbus_sendpacket_pagebuffer(out_channel,
						  pb, packet->page_buf_cnt,
						  &nvmsg, sizeof(nvmsg),
						  req_id);
	} else {
		ret = vmbus_sendpacket(out_channel,
				       &nvmsg, sizeof(nvmsg),
				       req_id, VM_PKT_DATA_INBAND,
				       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	}

	if (ret == 0) {
		atomic_inc_return(&nvchan->queue_sends);

		if (ring_avail < RING_AVAIL_PERCENT_LOWATER) {
			netif_tx_stop_queue(txq);
			ndev_ctx->eth_stats.stop_queue++;
		}
	} else if (ret == -EAGAIN) {
		netif_tx_stop_queue(txq);
		ndev_ctx->eth_stats.stop_queue++;
	} else {
		netdev_err(ndev,
			   "Unable to send packet pages %u len %u, ret %d\n",
			   packet->page_buf_cnt, packet->total_data_buflen,
			   ret);
	}

	if (netif_tx_queue_stopped(txq) &&
	    atomic_read(&nvchan->queue_sends) < 1 &&
	    !net_device->tx_disable) {
		netif_tx_wake_queue(txq);
		ndev_ctx->eth_stats.wake_queue++;
		if (ret == -EAGAIN)
			ret = -ENOSPC;
	}

	return ret;
}

/* Move packet out of multi send data (msd), and clear msd */
static inline void move_pkt_msd(struct hv_netvsc_packet **msd_send,
				struct sk_buff **msd_skb,
				struct multi_send_data *msdp)
{
	*msd_skb = msdp->skb;
	*msd_send = msdp->pkt;
	msdp->skb = NULL;
	msdp->pkt = NULL;
	msdp->count = 0;
}

/* RCU already held by caller */
int netvsc_send(struct net_device *ndev,
		struct hv_netvsc_packet *packet,
		struct rndis_message *rndis_msg,
		struct hv_page_buffer *pb,
		struct sk_buff *skb,
		bool xdp_tx)
{
	struct net_device_context *ndev_ctx = netdev_priv(ndev);
	struct netvsc_device *net_device
		= rcu_dereference_bh(ndev_ctx->nvdev);
	struct hv_device *device = ndev_ctx->device_ctx;
	int ret = 0;
	struct netvsc_channel *nvchan;
	u32 pktlen = packet->total_data_buflen, msd_len = 0;
	unsigned int section_index = NETVSC_INVALID_INDEX;
	struct multi_send_data *msdp;
	struct hv_netvsc_packet *msd_send = NULL, *cur_send = NULL;
	struct sk_buff *msd_skb = NULL;
	bool try_batch, xmit_more;

	/* If device is rescinded, return error and packet will get dropped. */
	if (unlikely(!net_device || net_device->destroy))
		return -ENODEV;

	nvchan = &net_device->chan_table[packet->q_idx];
	packet->send_buf_index = NETVSC_INVALID_INDEX;
	packet->cp_partial = false;

	/* Send a control message or XDP packet directly without accessing
	 * msd (Multi-Send Data) field which may be changed during data packet
	 * processing.
	 */
	if (!skb || xdp_tx)
		return netvsc_send_pkt(device, packet, net_device, pb, skb);

	/* batch packets in send buffer if possible */
	msdp = &nvchan->msd;
	if (msdp->pkt)
		msd_len = msdp->pkt->total_data_buflen;

	try_batch =  msd_len > 0 && msdp->count < net_device->max_pkt;
	if (try_batch && msd_len + pktlen + net_device->pkt_align <
	    net_device->send_section_size) {
		section_index = msdp->pkt->send_buf_index;

	} else if (try_batch && msd_len + packet->rmsg_size <
		   net_device->send_section_size) {
		section_index = msdp->pkt->send_buf_index;
		packet->cp_partial = true;

	} else if (pktlen + net_device->pkt_align <
		   net_device->send_section_size) {
		section_index = netvsc_get_next_send_section(net_device);
		if (unlikely(section_index == NETVSC_INVALID_INDEX)) {
			++ndev_ctx->eth_stats.tx_send_full;
		} else {
			move_pkt_msd(&msd_send, &msd_skb, msdp);
			msd_len = 0;
		}
	}

	/* Keep aggregating only if stack says more data is coming
	 * and not doing mixed modes send and not flow blocked
	 */
	xmit_more = netdev_xmit_more() &&
		!packet->cp_partial &&
		!netif_xmit_stopped(netdev_get_tx_queue(ndev, packet->q_idx));

	if (section_index != NETVSC_INVALID_INDEX) {
		netvsc_copy_to_send_buf(net_device,
					section_index, msd_len,
					packet, rndis_msg, pb, xmit_more);

		packet->send_buf_index = section_index;

		if (packet->cp_partial) {
			packet->page_buf_cnt -= packet->rmsg_pgcnt;
			packet->total_data_buflen = msd_len + packet->rmsg_size;
		} else {
			packet->page_buf_cnt = 0;
			packet->total_data_buflen += msd_len;
		}

		if (msdp->pkt) {
			packet->total_packets += msdp->pkt->total_packets;
			packet->total_bytes += msdp->pkt->total_bytes;
		}

		if (msdp->skb)
			dev_consume_skb_any(msdp->skb);

		if (xmit_more) {
			msdp->skb = skb;
			msdp->pkt = packet;
			msdp->count++;
		} else {
			cur_send = packet;
			msdp->skb = NULL;
			msdp->pkt = NULL;
			msdp->count = 0;
		}
	} else {
		move_pkt_msd(&msd_send, &msd_skb, msdp);
		cur_send = packet;
	}

	if (msd_send) {
		int m_ret = netvsc_send_pkt(device, msd_send, net_device,
					    NULL, msd_skb);

		if (m_ret != 0) {
			netvsc_free_send_slot(net_device,
					      msd_send->send_buf_index);
			dev_kfree_skb_any(msd_skb);
		}
	}

	if (cur_send)
		ret = netvsc_send_pkt(device, cur_send, net_device, pb, skb);

	if (ret != 0 && section_index != NETVSC_INVALID_INDEX)
		netvsc_free_send_slot(net_device, section_index);

	return ret;
}

/* Send pending recv completions */
static int send_recv_completions(struct net_device *ndev,
				 struct netvsc_device *nvdev,
				 struct netvsc_channel *nvchan)
{
	struct multi_recv_comp *mrc = &nvchan->mrc;
	struct recv_comp_msg {
		struct nvsp_message_header hdr;
		u32 status;
	}  __packed;
	struct recv_comp_msg msg = {
		.hdr.msg_type = NVSP_MSG1_TYPE_SEND_RNDIS_PKT_COMPLETE,
	};
	int ret;

	while (mrc->first != mrc->next) {
		const struct recv_comp_data *rcd
			= mrc->slots + mrc->first;

		msg.status = rcd->status;
		ret = vmbus_sendpacket(nvchan->channel, &msg, sizeof(msg),
				       rcd->tid, VM_PKT_COMP, 0);
		if (unlikely(ret)) {
			struct net_device_context *ndev_ctx = netdev_priv(ndev);

			++ndev_ctx->eth_stats.rx_comp_busy;
			return ret;
		}

		if (++mrc->first == nvdev->recv_completion_cnt)
			mrc->first = 0;
	}

	/* receive completion ring has been emptied */
	if (unlikely(nvdev->destroy))
		wake_up(&nvdev->wait_drain);

	return 0;
}

/* Count how many receive completions are outstanding */
static void recv_comp_slot_avail(const struct netvsc_device *nvdev,
				 const struct multi_recv_comp *mrc,
				 u32 *filled, u32 *avail)
{
	u32 count = nvdev->recv_completion_cnt;

	if (mrc->next >= mrc->first)
		*filled = mrc->next - mrc->first;
	else
		*filled = (count - mrc->first) + mrc->next;

	*avail = count - *filled - 1;
}

/* Add receive complete to ring to send to host. */
static void enq_receive_complete(struct net_device *ndev,
				 struct netvsc_device *nvdev, u16 q_idx,
				 u64 tid, u32 status)
{
	struct netvsc_channel *nvchan = &nvdev->chan_table[q_idx];
	struct multi_recv_comp *mrc = &nvchan->mrc;
	struct recv_comp_data *rcd;
	u32 filled, avail;

	recv_comp_slot_avail(nvdev, mrc, &filled, &avail);

	if (unlikely(filled > NAPI_POLL_WEIGHT)) {
		send_recv_completions(ndev, nvdev, nvchan);
		recv_comp_slot_avail(nvdev, mrc, &filled, &avail);
	}

	if (unlikely(!avail)) {
		netdev_err(ndev, "Recv_comp full buf q:%hd, tid:%llx\n",
			   q_idx, tid);
		return;
	}

	rcd = mrc->slots + mrc->next;
	rcd->tid = tid;
	rcd->status = status;

	if (++mrc->next == nvdev->recv_completion_cnt)
		mrc->next = 0;
}

static int netvsc_receive(struct net_device *ndev,
			  struct netvsc_device *net_device,
			  struct netvsc_channel *nvchan,
			  const struct vmpacket_descriptor *desc)
{
	struct net_device_context *net_device_ctx = netdev_priv(ndev);
	struct vmbus_channel *channel = nvchan->channel;
	const struct vmtransfer_page_packet_header *vmxferpage_packet
		= container_of(desc, const struct vmtransfer_page_packet_header, d);
	const struct nvsp_message *nvsp = hv_pkt_data(desc);
	u32 msglen = hv_pkt_datalen(desc);
	u16 q_idx = channel->offermsg.offer.sub_channel_index;
	char *recv_buf = net_device->recv_buf;
	u32 status = NVSP_STAT_SUCCESS;
	int i;
	int count = 0;

	/* Ensure packet is big enough to read header fields */
	if (msglen < sizeof(struct nvsp_message_header)) {
		netif_err(net_device_ctx, rx_err, ndev,
			  "invalid nvsp header, length too small: %u\n",
			  msglen);
		return 0;
	}

	/* Make sure this is a valid nvsp packet */
	if (unlikely(nvsp->hdr.msg_type != NVSP_MSG1_TYPE_SEND_RNDIS_PKT)) {
		netif_err(net_device_ctx, rx_err, ndev,
			  "Unknown nvsp packet type received %u\n",
			  nvsp->hdr.msg_type);
		return 0;
	}

	/* Validate xfer page pkt header */
	if ((desc->offset8 << 3) < sizeof(struct vmtransfer_page_packet_header)) {
		netif_err(net_device_ctx, rx_err, ndev,
			  "Invalid xfer page pkt, offset too small: %u\n",
			  desc->offset8 << 3);
		return 0;
	}

	if (unlikely(vmxferpage_packet->xfer_pageset_id != NETVSC_RECEIVE_BUFFER_ID)) {
		netif_err(net_device_ctx, rx_err, ndev,
			  "Invalid xfer page set id - expecting %x got %x\n",
			  NETVSC_RECEIVE_BUFFER_ID,
			  vmxferpage_packet->xfer_pageset_id);
		return 0;
	}

	count = vmxferpage_packet->range_cnt;

	/* Check count for a valid value */
	if (NETVSC_XFER_HEADER_SIZE(count) > desc->offset8 << 3) {
		netif_err(net_device_ctx, rx_err, ndev,
			  "Range count is not valid: %d\n",
			  count);
		return 0;
	}

	/* Each range represents 1 RNDIS pkt that contains 1 ethernet frame */
	for (i = 0; i < count; i++) {
		u32 offset = vmxferpage_packet->ranges[i].byte_offset;
		u32 buflen = vmxferpage_packet->ranges[i].byte_count;
		void *data;
		int ret;

		if (unlikely(offset > net_device->recv_buf_size ||
			     buflen > net_device->recv_buf_size - offset)) {
			nvchan->rsc.cnt = 0;
			status = NVSP_STAT_FAIL;
			netif_err(net_device_ctx, rx_err, ndev,
				  "Packet offset:%u + len:%u too big\n",
				  offset, buflen);

			continue;
		}

		data = recv_buf + offset;

		nvchan->rsc.is_last = (i == count - 1);

		trace_rndis_recv(ndev, q_idx, data);

		/* Pass it to the upper layer */
		ret = rndis_filter_receive(ndev, net_device,
					   nvchan, data, buflen);

		if (unlikely(ret != NVSP_STAT_SUCCESS)) {
			/* Drop incomplete packet */
			nvchan->rsc.cnt = 0;
			status = NVSP_STAT_FAIL;
		}
	}

	enq_receive_complete(ndev, net_device, q_idx,
			     vmxferpage_packet->d.trans_id, status);

	return count;
}

static void netvsc_send_table(struct net_device *ndev,
			      struct netvsc_device *nvscdev,
			      const struct nvsp_message *nvmsg,
			      u32 msglen)
{
	struct net_device_context *net_device_ctx = netdev_priv(ndev);
	u32 count, offset, *tab;
	int i;

	/* Ensure packet is big enough to read send_table fields */
	if (msglen < sizeof(struct nvsp_message_header) +
		     sizeof(struct nvsp_5_send_indirect_table)) {
		netdev_err(ndev, "nvsp_v5_msg length too small: %u\n", msglen);
		return;
	}

	count = nvmsg->msg.v5_msg.send_table.count;
	offset = nvmsg->msg.v5_msg.send_table.offset;

	if (count != VRSS_SEND_TAB_SIZE) {
		netdev_err(ndev, "Received wrong send-table size:%u\n", count);
		return;
	}

	/* If negotiated version <= NVSP_PROTOCOL_VERSION_6, the offset may be
	 * wrong due to a host bug. So fix the offset here.
	 */
	if (nvscdev->nvsp_version <= NVSP_PROTOCOL_VERSION_6 &&
	    msglen >= sizeof(struct nvsp_message_header) +
	    sizeof(union nvsp_6_message_uber) + count * sizeof(u32))
		offset = sizeof(struct nvsp_message_header) +
			 sizeof(union nvsp_6_message_uber);

	/* Boundary check for all versions */
	if (offset > msglen - count * sizeof(u32)) {
		netdev_err(ndev, "Received send-table offset too big:%u\n",
			   offset);
		return;
	}

	tab = (void *)nvmsg + offset;

	for (i = 0; i < count; i++)
		net_device_ctx->tx_table[i] = tab[i];
}

static void netvsc_send_vf(struct net_device *ndev,
			   const struct nvsp_message *nvmsg,
			   u32 msglen)
{
	struct net_device_context *net_device_ctx = netdev_priv(ndev);

	/* Ensure packet is big enough to read its fields */
	if (msglen < sizeof(struct nvsp_message_header) +
		     sizeof(struct nvsp_4_send_vf_association)) {
		netdev_err(ndev, "nvsp_v4_msg length too small: %u\n", msglen);
		return;
	}

	net_device_ctx->vf_alloc = nvmsg->msg.v4_msg.vf_assoc.allocated;
	net_device_ctx->vf_serial = nvmsg->msg.v4_msg.vf_assoc.serial;
	netdev_info(ndev, "VF slot %u %s\n",
		    net_device_ctx->vf_serial,
		    net_device_ctx->vf_alloc ? "added" : "removed");
}

static void netvsc_receive_inband(struct net_device *ndev,
				  struct netvsc_device *nvscdev,
				  const struct vmpacket_descriptor *desc)
{
	const struct nvsp_message *nvmsg = hv_pkt_data(desc);
	u32 msglen = hv_pkt_datalen(desc);

	/* Ensure packet is big enough to read header fields */
	if (msglen < sizeof(struct nvsp_message_header)) {
		netdev_err(ndev, "inband nvsp_message length too small: %u\n", msglen);
		return;
	}

	switch (nvmsg->hdr.msg_type) {
	case NVSP_MSG5_TYPE_SEND_INDIRECTION_TABLE:
		netvsc_send_table(ndev, nvscdev, nvmsg, msglen);
		break;

	case NVSP_MSG4_TYPE_SEND_VF_ASSOCIATION:
		netvsc_send_vf(ndev, nvmsg, msglen);
		break;
	}
}

static int netvsc_process_raw_pkt(struct hv_device *device,
				  struct netvsc_channel *nvchan,
				  struct netvsc_device *net_device,
				  struct net_device *ndev,
				  const struct vmpacket_descriptor *desc,
				  int budget)
{
	struct vmbus_channel *channel = nvchan->channel;
	const struct nvsp_message *nvmsg = hv_pkt_data(desc);

	trace_nvsp_recv(ndev, channel, nvmsg);

	switch (desc->type) {
	case VM_PKT_COMP:
		netvsc_send_completion(ndev, net_device, channel, desc, budget);
		break;

	case VM_PKT_DATA_USING_XFER_PAGES:
		return netvsc_receive(ndev, net_device, nvchan, desc);
		break;

	case VM_PKT_DATA_INBAND:
		netvsc_receive_inband(ndev, net_device, desc);
		break;

	default:
		netdev_err(ndev, "unhandled packet type %d, tid %llx\n",
			   desc->type, desc->trans_id);
		break;
	}

	return 0;
}

static struct hv_device *netvsc_channel_to_device(struct vmbus_channel *channel)
{
	struct vmbus_channel *primary = channel->primary_channel;

	return primary ? primary->device_obj : channel->device_obj;
}

/* Network processing softirq
 * Process data in incoming ring buffer from host
 * Stops when ring is empty or budget is met or exceeded.
 */
int netvsc_poll(struct napi_struct *napi, int budget)
{
	struct netvsc_channel *nvchan
		= container_of(napi, struct netvsc_channel, napi);
	struct netvsc_device *net_device = nvchan->net_device;
	struct vmbus_channel *channel = nvchan->channel;
	struct hv_device *device = netvsc_channel_to_device(channel);
	struct net_device *ndev = hv_get_drvdata(device);
	int work_done = 0;
	int ret;

	/* If starting a new interval */
	if (!nvchan->desc)
		nvchan->desc = hv_pkt_iter_first(channel);

	while (nvchan->desc && work_done < budget) {
		work_done += netvsc_process_raw_pkt(device, nvchan, net_device,
						    ndev, nvchan->desc, budget);
		nvchan->desc = hv_pkt_iter_next(channel, nvchan->desc);
	}

	/* Send any pending receive completions */
	ret = send_recv_completions(ndev, net_device, nvchan);

	/* If it did not exhaust NAPI budget this time
	 *  and not doing busy poll
	 * then re-enable host interrupts
	 *  and reschedule if ring is not empty
	 *   or sending receive completion failed.
	 */
	if (work_done < budget &&
	    napi_complete_done(napi, work_done) &&
	    (ret || hv_end_read(&channel->inbound)) &&
	    napi_schedule_prep(napi)) {
		hv_begin_read(&channel->inbound);
		__napi_schedule(napi);
	}

	/* Driver may overshoot since multiple packets per descriptor */
	return min(work_done, budget);
}

/* Call back when data is available in host ring buffer.
 * Processing is deferred until network softirq (NAPI)
 */
void netvsc_channel_cb(void *context)
{
	struct netvsc_channel *nvchan = context;
	struct vmbus_channel *channel = nvchan->channel;
	struct hv_ring_buffer_info *rbi = &channel->inbound;

	/* preload first vmpacket descriptor */
	prefetch(hv_get_ring_buffer(rbi) + rbi->priv_read_index);

	if (napi_schedule_prep(&nvchan->napi)) {
		/* disable interrupts from host */
		hv_begin_read(rbi);

		__napi_schedule_irqoff(&nvchan->napi);
	}
}

/*
 * netvsc_device_add - Callback when the device belonging to this
 * driver is added
 */
struct netvsc_device *netvsc_device_add(struct hv_device *device,
				const struct netvsc_device_info *device_info)
{
	int i, ret = 0;
	struct netvsc_device *net_device;
	struct net_device *ndev = hv_get_drvdata(device);
	struct net_device_context *net_device_ctx = netdev_priv(ndev);

	net_device = alloc_net_device();
	if (!net_device)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < VRSS_SEND_TAB_SIZE; i++)
		net_device_ctx->tx_table[i] = 0;

	/* Because the device uses NAPI, all the interrupt batching and
	 * control is done via Net softirq, not the channel handling
	 */
	set_channel_read_mode(device->channel, HV_CALL_ISR);

	/* If we're reopening the device we may have multiple queues, fill the
	 * chn_table with the default channel to use it before subchannels are
	 * opened.
	 * Initialize the channel state before we open;
	 * we can be interrupted as soon as we open the channel.
	 */

	for (i = 0; i < VRSS_CHANNEL_MAX; i++) {
		struct netvsc_channel *nvchan = &net_device->chan_table[i];

		nvchan->channel = device->channel;
		nvchan->net_device = net_device;
		u64_stats_init(&nvchan->tx_stats.syncp);
		u64_stats_init(&nvchan->rx_stats.syncp);

		ret = xdp_rxq_info_reg(&nvchan->xdp_rxq, ndev, i, 0);

		if (ret) {
			netdev_err(ndev, "xdp_rxq_info_reg fail: %d\n", ret);
			goto cleanup2;
		}

		ret = xdp_rxq_info_reg_mem_model(&nvchan->xdp_rxq,
						 MEM_TYPE_PAGE_SHARED, NULL);

		if (ret) {
			netdev_err(ndev, "xdp reg_mem_model fail: %d\n", ret);
			goto cleanup2;
		}
	}

	/* Enable NAPI handler before init callbacks */
	netif_napi_add(ndev, &net_device->chan_table[0].napi,
		       netvsc_poll, NAPI_POLL_WEIGHT);

	/* Open the channel */
	device->channel->rqstor_size = netvsc_rqstor_size(netvsc_ring_bytes);
	ret = vmbus_open(device->channel, netvsc_ring_bytes,
			 netvsc_ring_bytes,  NULL, 0,
			 netvsc_channel_cb, net_device->chan_table);

	if (ret != 0) {
		netdev_err(ndev, "unable to open channel: %d\n", ret);
		goto cleanup;
	}

	/* Channel is opened */
	netdev_dbg(ndev, "hv_netvsc channel opened successfully\n");

	napi_enable(&net_device->chan_table[0].napi);

	/* Connect with the NetVsp */
	ret = netvsc_connect_vsp(device, net_device, device_info);
	if (ret != 0) {
		netdev_err(ndev,
			"unable to connect to NetVSP - %d\n", ret);
		goto close;
	}

	/* Writing nvdev pointer unlocks netvsc_send(), make sure chn_table is
	 * populated.
	 */
	rcu_assign_pointer(net_device_ctx->nvdev, net_device);

	return net_device;

close:
	RCU_INIT_POINTER(net_device_ctx->nvdev, NULL);
	napi_disable(&net_device->chan_table[0].napi);

	/* Now, we can close the channel safely */
	vmbus_close(device->channel);

cleanup:
	netif_napi_del(&net_device->chan_table[0].napi);

cleanup2:
	free_netvsc_device(&net_device->rcu);

	return ERR_PTR(ret);
}
