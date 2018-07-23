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

	vmbus_sendpacket(dev->channel, init_pkt,
			       sizeof(struct nvsp_message),
			       (unsigned long)init_pkt,
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
		ret = rndis_set_subchannel(rdev->ndev, nvdev);
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
	atomic_set(&net_device->open_cnt, 0);
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

	for (i = 0; i < VRSS_CHANNEL_MAX; i++)
		vfree(nvdev->chan_table[i].mrc.slots);

	kfree(nvdev);
}

static void free_netvsc_device_rcu(struct netvsc_device *nvdev)
{
	call_rcu(&nvdev->rcu, free_netvsc_device);
}

static void netvsc_revoke_recv_buf(struct hv_device *device,
				   struct netvsc_device *net_device)
{
	struct net_device *ndev = hv_get_drvdata(device);
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

		ret = vmbus_sendpacket(device->channel,
				       revoke_packet,
				       sizeof(struct nvsp_message),
				       (unsigned long)revoke_packet,
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
				   struct netvsc_device *net_device)
{
	struct net_device *ndev = hv_get_drvdata(device);
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

		ret = vmbus_sendpacket(device->channel,
				       revoke_packet,
				       sizeof(struct nvsp_message),
				       (unsigned long)revoke_packet,
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
				       struct netvsc_device *net_device)
{
	struct net_device *ndev = hv_get_drvdata(device);
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
				       struct netvsc_device *net_device)
{
	struct net_device *ndev = hv_get_drvdata(device);
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

	/* Setup receive completion ring */
	net_device->recv_completion_cnt
		= round_up(net_device->recv_section_cnt + 1,
			   PAGE_SIZE / sizeof(u64));
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
	netvsc_revoke_recv_buf(device, net_device);
	netvsc_revoke_send_buf(device, net_device);
	netvsc_teardown_recv_gpadl(device, net_device);
	netvsc_teardown_send_gpadl(device, net_device);

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

	ret = vmbus_sendpacket(device->channel, init_packet,
				sizeof(struct nvsp_message),
				(unsigned long)init_packet,
				VM_PKT_DATA_INBAND, 0);

	return ret;
}

static int netvsc_connect_vsp(struct hv_device *device,
			      struct netvsc_device *net_device,
			      const struct netvsc_device_info *device_info)
{
	const u32 ver_list[] = {
		NVSP_PROTOCOL_VERSION_1, NVSP_PROTOCOL_VERSION_2,
		NVSP_PROTOCOL_VERSION_4, NVSP_PROTOCOL_VERSION_5
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

	/* Send the init request */
	ret = vmbus_sendpacket(device->channel, init_packet,
				sizeof(struct nvsp_message),
				(unsigned long)init_packet,
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
	netvsc_revoke_recv_buf(device, net_device);
	if (vmbus_proto_version < VERSION_WIN10)
		netvsc_teardown_recv_gpadl(device, net_device);

	netvsc_revoke_send_buf(device, net_device);
	if (vmbus_proto_version < VERSION_WIN10)
		netvsc_teardown_send_gpadl(device, net_device);

	RCU_INIT_POINTER(net_device_ctx->nvdev, NULL);

	/* And disassociate NAPI context from device */
	for (i = 0; i < net_device->num_chn; i++)
		netif_napi_del(&net_device->chan_table[i].napi);

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
		netvsc_teardown_recv_gpadl(device, net_device);
		netvsc_teardown_send_gpadl(device, net_device);
	}

	/* Release all resources */
	free_netvsc_device_rcu(net_device);
}

#define RING_AVAIL_PERCENT_HIWATER 20
#define RING_AVAIL_PERCENT_LOWATER 10

/*
 * Get the percentage of available bytes to write in the ring.
 * The return value is in range from 0 to 100.
 */
static inline u32 hv_ringbuf_avail_percent(
		struct hv_ring_buffer_info *ring_info)
{
	u32 avail_read, avail_write;

	hv_get_ringbuffer_availbytes(ring_info, &avail_read, &avail_write);

	return avail_write * 100 / ring_info->ring_datasize;
}

static inline void netvsc_free_send_slot(struct netvsc_device *net_device,
					 u32 index)
{
	sync_change_bit(index, net_device->send_section_map);
}

static void netvsc_send_tx_complete(struct netvsc_device *net_device,
				    struct vmbus_channel *incoming_channel,
				    struct hv_device *device,
				    const struct vmpacket_descriptor *desc,
				    int budget)
{
	struct sk_buff *skb = (struct sk_buff *)(unsigned long)desc->trans_id;
	struct net_device *ndev = hv_get_drvdata(device);
	struct vmbus_channel *channel = device->channel;
	u16 q_idx = 0;
	int queue_sends;

	/* Notify the layer above us */
	if (likely(skb)) {
		const struct hv_netvsc_packet *packet
			= (struct hv_netvsc_packet *)skb->cb;
		u32 send_index = packet->send_buf_index;
		struct netvsc_stats *tx_stats;

		if (send_index != NETVSC_INVALID_INDEX)
			netvsc_free_send_slot(net_device, send_index);
		q_idx = packet->q_idx;
		channel = incoming_channel;

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

		if (netif_tx_queue_stopped(txq) &&
		    (hv_ringbuf_avail_percent(&channel->outbound) > RING_AVAIL_PERCENT_HIWATER ||
		     queue_sends < 1)) {
			netif_tx_wake_queue(txq);
		}
	}
}

static void netvsc_send_completion(struct netvsc_device *net_device,
				   struct vmbus_channel *incoming_channel,
				   struct hv_device *device,
				   const struct vmpacket_descriptor *desc,
				   int budget)
{
	struct nvsp_message *nvsp_packet = hv_pkt_data(desc);
	struct net_device *ndev = hv_get_drvdata(device);

	switch (nvsp_packet->hdr.msg_type) {
	case NVSP_MSG_TYPE_INIT_COMPLETE:
	case NVSP_MSG1_TYPE_SEND_RECV_BUF_COMPLETE:
	case NVSP_MSG1_TYPE_SEND_SEND_BUF_COMPLETE:
	case NVSP_MSG5_TYPE_SUBCHANNEL:
		/* Copy the response back */
		memcpy(&net_device->channel_init_pkt, nvsp_packet,
		       sizeof(struct nvsp_message));
		complete(&net_device->channel_init_wait);
		break;

	case NVSP_MSG1_TYPE_SEND_RNDIS_PKT_COMPLETE:
		netvsc_send_tx_complete(net_device, incoming_channel,
					device, desc, budget);
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
	u32 msg_size = 0;
	u32 padding = 0;
	u32 remain = packet->total_data_buflen % net_device->pkt_align;
	u32 page_count = packet->cp_partial ? packet->rmsg_pgcnt :
		packet->page_buf_cnt;

	/* Add padding */
	remain = packet->total_data_buflen & (net_device->pkt_align - 1);
	if (xmit_more && remain) {
		padding = net_device->pkt_align - remain;
		rndis_msg->msg_len += padding;
		packet->total_data_buflen += padding;
	}

	for (i = 0; i < page_count; i++) {
		char *src = phys_to_virt(pb[i].pfn << PAGE_SHIFT);
		u32 offset = pb[i].offset;
		u32 len = pb[i].len;

		memcpy(dest, (src + offset), len);
		msg_size += len;
		dest += len;
	}

	if (padding) {
		memset(dest, 0, padding);
		msg_size += padding;
	}
}

static inline int netvsc_send_pkt(
	struct hv_device *device,
	struct hv_netvsc_packet *packet,
	struct netvsc_device *net_device,
	struct hv_page_buffer *pb,
	struct sk_buff *skb)
{
	struct nvsp_message nvmsg;
	struct nvsp_1_message_send_rndis_packet * const rpkt =
		&nvmsg.msg.v1_msg.send_rndis_pkt;
	struct netvsc_channel * const nvchan =
		&net_device->chan_table[packet->q_idx];
	struct vmbus_channel *out_channel = nvchan->channel;
	struct net_device *ndev = hv_get_drvdata(device);
	struct netdev_queue *txq = netdev_get_tx_queue(ndev, packet->q_idx);
	u64 req_id;
	int ret;
	u32 ring_avail = hv_ringbuf_avail_percent(&out_channel->outbound);

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

		if (ring_avail < RING_AVAIL_PERCENT_LOWATER)
			netif_tx_stop_queue(txq);
	} else if (ret == -EAGAIN) {
		netif_tx_stop_queue(txq);
		if (atomic_read(&nvchan->queue_sends) < 1) {
			netif_tx_wake_queue(txq);
			ret = -ENOSPC;
		}
	} else {
		netdev_err(ndev,
			   "Unable to send packet pages %u len %u, ret %d\n",
			   packet->page_buf_cnt, packet->total_data_buflen,
			   ret);
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
		struct sk_buff *skb)
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

	/* Send control message directly without accessing msd (Multi-Send
	 * Data) field which may be changed during data packet processing.
	 */
	if (!skb)
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
	xmit_more = skb->xmit_more &&
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
			  struct net_device_context *net_device_ctx,
			  struct hv_device *device,
			  struct vmbus_channel *channel,
			  const struct vmpacket_descriptor *desc,
			  struct nvsp_message *nvsp)
{
	const struct vmtransfer_page_packet_header *vmxferpage_packet
		= container_of(desc, const struct vmtransfer_page_packet_header, d);
	u16 q_idx = channel->offermsg.offer.sub_channel_index;
	char *recv_buf = net_device->recv_buf;
	u32 status = NVSP_STAT_SUCCESS;
	int i;
	int count = 0;

	/* Make sure this is a valid nvsp packet */
	if (unlikely(nvsp->hdr.msg_type != NVSP_MSG1_TYPE_SEND_RNDIS_PKT)) {
		netif_err(net_device_ctx, rx_err, ndev,
			  "Unknown nvsp packet type received %u\n",
			  nvsp->hdr.msg_type);
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

	/* Each range represents 1 RNDIS pkt that contains 1 ethernet frame */
	for (i = 0; i < count; i++) {
		void *data = recv_buf
			+ vmxferpage_packet->ranges[i].byte_offset;
		u32 buflen = vmxferpage_packet->ranges[i].byte_count;

		/* Pass it to the upper layer */
		status = rndis_filter_receive(ndev, net_device, device,
					      channel, data, buflen);
	}

	enq_receive_complete(ndev, net_device, q_idx,
			     vmxferpage_packet->d.trans_id, status);

	return count;
}

static void netvsc_send_table(struct hv_device *hdev,
			      struct nvsp_message *nvmsg)
{
	struct net_device *ndev = hv_get_drvdata(hdev);
	struct net_device_context *net_device_ctx = netdev_priv(ndev);
	int i;
	u32 count, *tab;

	count = nvmsg->msg.v5_msg.send_table.count;
	if (count != VRSS_SEND_TAB_SIZE) {
		netdev_err(ndev, "Received wrong send-table size:%u\n", count);
		return;
	}

	tab = (u32 *)((unsigned long)&nvmsg->msg.v5_msg.send_table +
		      nvmsg->msg.v5_msg.send_table.offset);

	for (i = 0; i < count; i++)
		net_device_ctx->tx_table[i] = tab[i];
}

static void netvsc_send_vf(struct net_device_context *net_device_ctx,
			   struct nvsp_message *nvmsg)
{
	net_device_ctx->vf_alloc = nvmsg->msg.v4_msg.vf_assoc.allocated;
	net_device_ctx->vf_serial = nvmsg->msg.v4_msg.vf_assoc.serial;
}

static inline void netvsc_receive_inband(struct hv_device *hdev,
				 struct net_device_context *net_device_ctx,
				 struct nvsp_message *nvmsg)
{
	switch (nvmsg->hdr.msg_type) {
	case NVSP_MSG5_TYPE_SEND_INDIRECTION_TABLE:
		netvsc_send_table(hdev, nvmsg);
		break;

	case NVSP_MSG4_TYPE_SEND_VF_ASSOCIATION:
		netvsc_send_vf(net_device_ctx, nvmsg);
		break;
	}
}

static int netvsc_process_raw_pkt(struct hv_device *device,
				  struct vmbus_channel *channel,
				  struct netvsc_device *net_device,
				  struct net_device *ndev,
				  const struct vmpacket_descriptor *desc,
				  int budget)
{
	struct net_device_context *net_device_ctx = netdev_priv(ndev);
	struct nvsp_message *nvmsg = hv_pkt_data(desc);

	switch (desc->type) {
	case VM_PKT_COMP:
		netvsc_send_completion(net_device, channel, device,
				       desc, budget);
		break;

	case VM_PKT_DATA_USING_XFER_PAGES:
		return netvsc_receive(ndev, net_device, net_device_ctx,
				      device, channel, desc, nvmsg);
		break;

	case VM_PKT_DATA_INBAND:
		netvsc_receive_inband(device, net_device_ctx, nvmsg);
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

	/* If starting a new interval */
	if (!nvchan->desc)
		nvchan->desc = hv_pkt_iter_first(channel);

	while (nvchan->desc && work_done < budget) {
		work_done += netvsc_process_raw_pkt(device, channel, net_device,
						    ndev, nvchan->desc, budget);
		nvchan->desc = hv_pkt_iter_next(channel, nvchan->desc);
	}

	/* If send of pending receive completions suceeded
	 *   and did not exhaust NAPI budget this time
	 *   and not doing busy poll
	 * then re-enable host interrupts
	 *     and reschedule if ring is not empty.
	 */
	if (send_recv_completions(ndev, net_device, nvchan) == 0 &&
	    work_done < budget &&
	    napi_complete_done(napi, work_done) &&
	    hv_end_read(&channel->inbound) &&
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
		/* disable interupts from host */
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
	int ring_size = device_info->ring_size;
	struct netvsc_device *net_device;
	struct net_device *ndev = hv_get_drvdata(device);
	struct net_device_context *net_device_ctx = netdev_priv(ndev);

	net_device = alloc_net_device();
	if (!net_device)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < VRSS_SEND_TAB_SIZE; i++)
		net_device_ctx->tx_table[i] = 0;

	net_device->ring_size = ring_size;

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
	}

	/* Enable NAPI handler before init callbacks */
	netif_napi_add(ndev, &net_device->chan_table[0].napi,
		       netvsc_poll, NAPI_POLL_WEIGHT);

	/* Open the channel */
	ret = vmbus_open(device->channel, ring_size * PAGE_SIZE,
			 ring_size * PAGE_SIZE, NULL, 0,
			 netvsc_channel_cb,
			 net_device->chan_table);

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
	free_netvsc_device(&net_device->rcu);

	return ERR_PTR(ret);
}
