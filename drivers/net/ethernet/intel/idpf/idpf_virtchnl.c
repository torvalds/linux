// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include <linux/export.h>
#include <linux/net/intel/libie/pci.h>
#include <net/libeth/rx.h>

#include "idpf.h"
#include "idpf_virtchnl.h"
#include "idpf_ptp.h"

/**
 * idpf_vid_to_vport - Translate vport id to vport pointer
 * @adapter: private data struct
 * @v_id: vport id to translate
 *
 * Returns vport matching v_id, NULL if not found.
 */
static
struct idpf_vport *idpf_vid_to_vport(struct idpf_adapter *adapter, u32 v_id)
{
	u16 num_max_vports = idpf_get_max_vports(adapter);
	int i;

	for (i = 0; i < num_max_vports; i++)
		if (adapter->vport_ids[i] == v_id)
			return adapter->vports[i];

	return NULL;
}

/**
 * idpf_handle_event_link - Handle link event message
 * @adapter: private data struct
 * @v2e: virtchnl event message
 */
static void idpf_handle_event_link(struct idpf_adapter *adapter,
				   const struct virtchnl2_event *v2e)
{
	struct idpf_netdev_priv *np;
	struct idpf_vport *vport;

	vport = idpf_vid_to_vport(adapter, le32_to_cpu(v2e->vport_id));
	if (!vport) {
		dev_err_ratelimited(&adapter->pdev->dev, "Failed to find vport_id %d for link event\n",
				    v2e->vport_id);
		return;
	}
	np = netdev_priv(vport->netdev);

	np->link_speed_mbps = le32_to_cpu(v2e->link_speed);

	if (vport->link_up == v2e->link_status)
		return;

	vport->link_up = v2e->link_status;

	if (!test_bit(IDPF_VPORT_UP, np->state))
		return;

	if (vport->link_up) {
		netif_tx_start_all_queues(vport->netdev);
		netif_carrier_on(vport->netdev);
	} else {
		netif_tx_stop_all_queues(vport->netdev);
		netif_carrier_off(vport->netdev);
	}
}

/**
 * idpf_recv_event_msg - Receive virtchnl event message
 * @ctx: control queue context
 * @ctlq_msg: message to copy from
 *
 * Receive virtchnl event message
 */
void idpf_recv_event_msg(struct libie_ctlq_ctx *ctx,
			 struct libie_ctlq_msg *ctlq_msg)
{
	struct kvec *buff = &ctlq_msg->recv_mem;
	int payload_size = buff->iov_len;
	struct idpf_adapter *adapter;
	struct virtchnl2_event *v2e;
	u32 event;

	adapter = container_of(ctx, struct idpf_adapter, ctlq_ctx);
	if (ctlq_msg->chnl_opcode != VIRTCHNL2_OP_EVENT) {
		dev_dbg(&adapter->pdev->dev,
			"Unhandled message with opcode %u from CP\n",
			ctlq_msg->chnl_opcode);
		goto free_rx_buf;
	}

	if (payload_size < sizeof(*v2e)) {
		dev_err_ratelimited(&adapter->pdev->dev, "Failed to receive valid payload for event msg (op %d len %d)\n",
				    ctlq_msg->chnl_opcode,
				    payload_size);
		goto free_rx_buf;
	}

	v2e = (struct virtchnl2_event *)buff->iov_base;
	event = le32_to_cpu(v2e->event);

	switch (event) {
	case VIRTCHNL2_EVENT_LINK_CHANGE:
		idpf_handle_event_link(adapter, v2e);
		break;
	default:
		dev_err(&adapter->pdev->dev,
			"Unknown event %d from PF\n", event);
		break;
	}

free_rx_buf:
	libie_ctlq_release_rx_buf(buff);
}

/**
 * idpf_mb_clean - Reclaim the send mailbox queue entries
 * @asq: send control queue info
 * @deinit: release all buffers before destroying the queue
 *
 * This is a helper function to clean the send mailbox queue entries.
 */
static void idpf_mb_clean(struct libie_ctlq_info *asq, bool deinit)
{
	libie_ctlq_xn_send_clean(asq, kfree, deinit);
}

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
/**
 * idpf_ptp_is_mb_msg - Check if the message is PTP-related
 * @op: virtchnl opcode
 *
 * Return: true if msg is PTP-related, false otherwise.
 */
static bool idpf_ptp_is_mb_msg(u32 op)
{
	switch (op) {
	case VIRTCHNL2_OP_PTP_GET_DEV_CLK_TIME:
	case VIRTCHNL2_OP_PTP_GET_CROSS_TIME:
	case VIRTCHNL2_OP_PTP_SET_DEV_CLK_TIME:
	case VIRTCHNL2_OP_PTP_ADJ_DEV_CLK_FINE:
	case VIRTCHNL2_OP_PTP_ADJ_DEV_CLK_TIME:
	case VIRTCHNL2_OP_PTP_GET_VPORT_TX_TSTAMP_CAPS:
	case VIRTCHNL2_OP_PTP_GET_VPORT_TX_TSTAMP:
		return true;
	default:
		return false;
	}
}

/**
 * idpf_prepare_ptp_mb_msg - Prepare PTP related message
 *
 * @adapter: Driver specific private structure
 * @op: virtchnl opcode
 * @ctlq_msg: Corresponding control queue message
 */
static void idpf_prepare_ptp_mb_msg(struct idpf_adapter *adapter, u32 op,
				    struct libie_ctlq_msg *ctlq_msg)
{
	/* If the message is PTP-related and the secondary mailbox is available,
	 * send the message through the secondary mailbox.
	 */
	if (!idpf_ptp_is_mb_msg(op) || !adapter->ptp->secondary_mbx.valid)
		return;

	ctlq_msg->opcode = LIBIE_CTLQ_SEND_MSG_TO_PEER;
	ctlq_msg->func_id = adapter->ptp->secondary_mbx.peer_mbx_q_id;
	ctlq_msg->flags = FIELD_PREP(LIBIE_CTLQ_DESC_FLAG_HOST_ID,
				     adapter->ptp->secondary_mbx.peer_id);
}
#else /* !CONFIG_PTP_1588_CLOCK */
static void idpf_prepare_ptp_mb_msg(struct idpf_adapter *adapter, u32 op,
				    struct libie_ctlq_msg *ctlq_msg)
{ }
#endif /* CONFIG_PTP_1588_CLOCK */

/**
 * idpf_send_mb_msg - send mailbox message to the device control plane
 * @adapter: driver specific private structure
 * @xn_params: Xn send parameters to fill
 * @send_buf: buffer to send
 * @send_buf_size: size of the send buffer
 *
 * Fill the Xn parameters with the required info to send a virtchnl message.
 * The send buffer is DMA mapped in the libie to avoid memcpy.
 *
 * Cleanup the mailbox queue entries of the previously sent message to
 * unmap and release the buffer.
 *
 * Return: 0 if the request was successful, -%EBUSY if reset is detected
 *	   or Tx control queue is full, other negative error code on failure.
 */
int idpf_send_mb_msg(struct idpf_adapter *adapter,
		     struct libie_ctlq_xn_send_params *xn_params,
		     void *send_buf, size_t send_buf_size)
{
	struct libie_ctlq_msg ctlq_msg = {};

	if (idpf_is_reset_detected(adapter)) {
		if (!libie_cp_can_send_onstack(send_buf_size))
			kfree(send_buf);

		return -EBUSY;
	}

	idpf_prepare_ptp_mb_msg(adapter, xn_params->chnl_opcode, &ctlq_msg);
	xn_params->ctlq_msg = ctlq_msg.opcode ? &ctlq_msg : NULL;

	xn_params->send_buf.iov_base = send_buf;
	xn_params->send_buf.iov_len = send_buf_size;
	xn_params->xnm = adapter->xnm;
	xn_params->ctlq = xn_params->ctlq ? xn_params->ctlq : adapter->asq;
	xn_params->rel_tx_buf = kfree;

	idpf_mb_clean(xn_params->ctlq, false);

	return libie_ctlq_xn_send(xn_params);
}

/**
 * idpf_send_mb_msg_kfree - send mailbox message and free the send buffer
 * @adapter: driver specific private structure
 * @xn_params: Xn send parameters to fill
 * @send_buf: buffer to send, can be released with kfree()
 * @send_buf_size: size of the send buffer
 *
 * libie_cp functions consume only buffers above certain size,
 * smaller buffers are assumed to be on the stack. However, for some
 * commands with variable message size it makes sense to always use kzalloc(),
 * which means we have to free smaller buffers ourselves.
 *
 * Return: 0 if no unexpected errors were encountered,
 *	   negative error code otherwise.
 */
int idpf_send_mb_msg_kfree(struct idpf_adapter *adapter,
			   struct libie_ctlq_xn_send_params *xn_params,
			   void *send_buf, size_t send_buf_size)
{
	int err = idpf_send_mb_msg(adapter, xn_params, send_buf, send_buf_size);

	if (libie_cp_can_send_onstack(send_buf_size))
		kfree(send_buf);

	return err;
}

/**
 * idpf_send_vf_reset_msg - send one way VF reset message
 * @adapter: driver specific private structure
 */
void idpf_send_vf_reset_msg(struct idpf_adapter *adapter)
{
	struct libie_ctlq_info *ctlq = adapter->asq;

	/* Forcefully claim send queue slot */
	idpf_mb_clean(ctlq, true);

	scoped_guard(spinlock, &ctlq->lock) {
		*ctlq->tx_msg[ctlq->next_to_use] = (struct libie_ctlq_msg) {
			.opcode = LIBIE_CTLQ_SEND_MSG_TO_CP,
			.chnl_opcode = VIRTCHNL2_OP_RESET_VF,
		};

		libie_ctlq_send(adapter->asq, 1);
	}
}

struct idpf_chunked_msg_params {
	u32			(*prepare_msg)(u32 vport_id, void *buf,
					       const void *pos, u32 num);

	const void		*chunks;
	u32			num_chunks;

	u32			chunk_sz;
	u32			config_sz;

	u32			vc_op;
	u32			vport_id;
};

struct idpf_queue_set *idpf_alloc_queue_set(struct idpf_adapter *adapter,
					    struct idpf_q_vec_rsrc *qv_rsrc,
					    u32 vport_id, u32 num)
{
	struct idpf_queue_set *qp;

	qp = kzalloc_flex(*qp, qs, num);
	if (!qp)
		return NULL;

	qp->adapter = adapter;
	qp->qv_rsrc = qv_rsrc;
	qp->vport_id = vport_id;
	qp->num = num;

	return qp;
}

/**
 * idpf_send_chunked_msg - send VC message consisting of chunks
 * @adapter: Driver specific private structure
 * @params: message params
 *
 * Helper function for preparing a message describing queues to be enabled
 * or disabled.
 *
 * Return: the total size of the prepared message.
 */
static int idpf_send_chunked_msg(struct idpf_adapter *adapter,
				 const struct idpf_chunked_msg_params *params)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= params->vc_op,
	};
	const void *pos = params->chunks;
	u32 totqs = params->num_chunks;
	u32 vid = params->vport_id;
	u32 num_chunks, num_msgs;

	num_chunks = IDPF_NUM_CHUNKS_PER_MSG(params->config_sz,
					     params->chunk_sz);
	num_msgs = DIV_ROUND_UP(totqs, num_chunks);

	for (u32 i = 0; i < num_msgs; i++) {
		u32 buf_sz;
		void *buf;
		int err;

		num_chunks = min(num_chunks, totqs);
		buf_sz = params->config_sz + num_chunks * params->chunk_sz;
		buf = kzalloc(buf_sz, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		if (params->prepare_msg(vid, buf, pos, num_chunks) != buf_sz) {
			kfree(buf);
			return -EINVAL;
		}

		err = idpf_send_mb_msg_kfree(adapter, &xn_params, buf, buf_sz);
		if (err)
			return err;

		libie_ctlq_release_rx_buf(&xn_params.recv_mem);
		xn_params.recv_mem = (struct kvec) {};
		pos += num_chunks * params->chunk_sz;
		totqs -= num_chunks;
	}

	return 0;
}

/**
 * idpf_wait_for_marker_event_set - wait for software marker response for
 *				    selected Tx queues
 * @qs: set of the Tx queues
 *
 * Return: 0 success, -errno on failure.
 */
static int idpf_wait_for_marker_event_set(const struct idpf_queue_set *qs)
{
	struct net_device *netdev;
	struct idpf_tx_queue *txq;
	bool markers_rcvd = true;

	for (u32 i = 0; i < qs->num; i++) {
		switch (qs->qs[i].type) {
		case VIRTCHNL2_QUEUE_TYPE_TX:
			txq = qs->qs[i].txq;

			netdev = txq->netdev;

			idpf_queue_set(SW_MARKER, txq);
			idpf_wait_for_sw_marker_completion(txq);
			markers_rcvd &= !idpf_queue_has(SW_MARKER, txq);
			break;
		default:
			break;
		}
	}

	if (!markers_rcvd) {
		netdev_warn(netdev,
			    "Failed to receive marker packets\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/**
 * idpf_wait_for_marker_event - wait for software marker response
 * @vport: virtual port data structure
 *
 * Return: 0 success, negative on failure.
 **/
static int idpf_wait_for_marker_event(struct idpf_vport *vport)
{
	struct idpf_queue_set *qs __free(kfree) = NULL;

	qs = idpf_alloc_queue_set(vport->adapter, &vport->dflt_qv_rsrc,
				  vport->vport_id, vport->num_txq);
	if (!qs)
		return -ENOMEM;

	for (u32 i = 0; i < qs->num; i++) {
		qs->qs[i].type = VIRTCHNL2_QUEUE_TYPE_TX;
		qs->qs[i].txq = vport->txqs[i];
	}

	return idpf_wait_for_marker_event_set(qs);
}

/**
 * idpf_send_ver_msg - send virtchnl version message
 * @adapter: Driver specific private structure
 *
 * Send virtchnl version message.  Returns 0 on success, negative on failure.
 */
static int idpf_send_ver_msg(struct idpf_adapter *adapter)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= VIRTCHNL2_OP_VERSION,
	};
	struct virtchnl2_version_info *vvi_recv;
	struct virtchnl2_version_info vvi;
	u32 major, minor;
	int err;

	if (adapter->virt_ver_maj) {
		vvi.major = cpu_to_le32(adapter->virt_ver_maj);
		vvi.minor = cpu_to_le32(adapter->virt_ver_min);
	} else {
		vvi.major = cpu_to_le32(IDPF_VIRTCHNL_VERSION_MAJOR);
		vvi.minor = cpu_to_le32(IDPF_VIRTCHNL_VERSION_MINOR);
	}

	err = idpf_send_mb_msg_stack(adapter, &xn_params, &vvi);
	if (err)
		return err;

	if (xn_params.recv_mem.iov_len < sizeof(*vvi_recv)) {
		err = -EIO;
		goto free_rx_buf;
	}

	vvi_recv = xn_params.recv_mem.iov_base;
	major = le32_to_cpu(vvi_recv->major);
	minor = le32_to_cpu(vvi_recv->minor);

	if (major > IDPF_VIRTCHNL_VERSION_MAJOR) {
		dev_warn(&adapter->pdev->dev, "Virtchnl major version greater than supported\n");
		err = -EINVAL;
		goto free_rx_buf;
	}

	if (major == IDPF_VIRTCHNL_VERSION_MAJOR &&
	    minor > IDPF_VIRTCHNL_VERSION_MINOR)
		dev_warn(&adapter->pdev->dev, "Virtchnl minor version didn't match\n");

	/* If we have a mismatch, resend version to update receiver on what
	 * version we will use.
	 */
	if (!adapter->virt_ver_maj &&
	    major != IDPF_VIRTCHNL_VERSION_MAJOR &&
	    minor != IDPF_VIRTCHNL_VERSION_MINOR)
		err = -EAGAIN;

	adapter->virt_ver_maj = major;
	adapter->virt_ver_min = minor;

free_rx_buf:
	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return err;
}

/**
 * idpf_send_get_caps_msg - Send virtchnl get capabilities message
 * @adapter: Driver specific private structure
 *
 * Send virtchl get capabilities message. Returns 0 on success, negative on
 * failure.
 */
static int idpf_send_get_caps_msg(struct idpf_adapter *adapter)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= VIRTCHNL2_OP_GET_CAPS,
	};
	struct virtchnl2_get_capabilities caps = {};
	int err;

	caps.csum_caps =
		cpu_to_le32(VIRTCHNL2_CAP_TX_CSUM_L3_IPV4	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV4_TCP	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV4_UDP	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV4_SCTP	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV6_TCP	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV6_UDP	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV6_SCTP	|
			    VIRTCHNL2_CAP_RX_CSUM_L3_IPV4	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV4_TCP	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV4_UDP	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV4_SCTP	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV6_TCP	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV6_UDP	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV6_SCTP	|
			    VIRTCHNL2_CAP_TX_CSUM_L3_SINGLE_TUNNEL |
			    VIRTCHNL2_CAP_RX_CSUM_L3_SINGLE_TUNNEL |
			    VIRTCHNL2_CAP_TX_CSUM_L4_SINGLE_TUNNEL |
			    VIRTCHNL2_CAP_RX_CSUM_L4_SINGLE_TUNNEL |
			    VIRTCHNL2_CAP_RX_CSUM_GENERIC);

	caps.seg_caps =
		cpu_to_le32(VIRTCHNL2_CAP_SEG_IPV4_TCP		|
			    VIRTCHNL2_CAP_SEG_IPV4_UDP		|
			    VIRTCHNL2_CAP_SEG_IPV4_SCTP		|
			    VIRTCHNL2_CAP_SEG_IPV6_TCP		|
			    VIRTCHNL2_CAP_SEG_IPV6_UDP		|
			    VIRTCHNL2_CAP_SEG_IPV6_SCTP		|
			    VIRTCHNL2_CAP_SEG_TX_SINGLE_TUNNEL);

	caps.rss_caps =
		cpu_to_le64(VIRTCHNL2_FLOW_IPV4_TCP		|
			    VIRTCHNL2_FLOW_IPV4_UDP		|
			    VIRTCHNL2_FLOW_IPV4_SCTP		|
			    VIRTCHNL2_FLOW_IPV4_OTHER		|
			    VIRTCHNL2_FLOW_IPV6_TCP		|
			    VIRTCHNL2_FLOW_IPV6_UDP		|
			    VIRTCHNL2_FLOW_IPV6_SCTP		|
			    VIRTCHNL2_FLOW_IPV6_OTHER);

	caps.hsplit_caps =
		cpu_to_le32(VIRTCHNL2_CAP_RX_HSPLIT_AT_L4V4	|
			    VIRTCHNL2_CAP_RX_HSPLIT_AT_L4V6);

	caps.rsc_caps =
		cpu_to_le32(VIRTCHNL2_CAP_RSC_IPV4_TCP		|
			    VIRTCHNL2_CAP_RSC_IPV6_TCP);

	caps.other_caps =
		cpu_to_le64(VIRTCHNL2_CAP_SRIOV			|
			    VIRTCHNL2_CAP_RDMA                  |
			    VIRTCHNL2_CAP_LAN_MEMORY_REGIONS	|
			    VIRTCHNL2_CAP_MACFILTER		|
			    VIRTCHNL2_CAP_SPLITQ_QSCHED		|
			    VIRTCHNL2_CAP_PROMISC		|
			    VIRTCHNL2_CAP_LOOPBACK		|
			    VIRTCHNL2_CAP_PTP);

	err = idpf_send_mb_msg_stack(adapter, &xn_params, &caps);
	if (err)
		return err;

	if (xn_params.recv_mem.iov_len < sizeof(adapter->caps)) {
		err = -EIO;
		goto free_rx_buf;
	}

	memcpy(&adapter->caps, xn_params.recv_mem.iov_base,
	       sizeof(adapter->caps));

free_rx_buf:
	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return err;
}

/**
 * idpf_mmio_region_non_static - Check if region is not static
 * @mmio_info: PCI resources info
 * @reg: region to check
 *
 * Return: %true if region can be received though virtchnl command,
 *	   %false if region is related to mailbox or resetting
 */
bool idpf_mmio_region_non_static(struct libie_mmio_info *mmio_info,
				 struct libie_pci_mmio_region *reg)
{
	struct idpf_adapter *adapter =
		container_of(mmio_info, struct idpf_adapter,
			     ctlq_ctx.mmio_info);

	for (uint i = 0; i < IDPF_MMIO_REG_NUM_STATIC; i++) {
		if (reg->bar_idx == 0 &&
		    reg->offset == adapter->dev_ops.static_reg_info[i].start)
			return false;
	}

	return true;
}

/**
 * idpf_decfg_lan_memory_regions - Unmap non-static memory regions
 * @adapter: Driver specific private structure
 */
static void idpf_decfg_lan_memory_regions(struct idpf_adapter *adapter)
{
	libie_pci_unmap_fltr_regs(&adapter->ctlq_ctx.mmio_info,
				  idpf_mmio_region_non_static);
}

/**
 * idpf_cfg_lan_memory_regions - Get (via virtchnl) and map LAN memory regions
 * @adapter: Driver specific private struct
 *
 * Return: 0 on success or error code on failure.
 */
static int idpf_cfg_lan_memory_regions(struct idpf_adapter *adapter)
{
	struct virtchnl2_get_lan_memory_regions *send_regions, *rcvd_regions;
	struct libie_ctlq_xn_send_params xn_params = {
		.chnl_opcode = VIRTCHNL2_OP_GET_LAN_MEMORY_REGIONS,
		.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
	};
	size_t send_sz, reply_sz, size;
	int num_regions;
	int err = 0;

	send_sz = sizeof(struct virtchnl2_get_lan_memory_regions) +
		  sizeof(struct virtchnl2_mem_region);
	send_regions = kzalloc(send_sz, GFP_KERNEL);
	if (!send_regions)
		return -ENOMEM;

	send_regions->num_memory_regions = cpu_to_le16(1);
	err = idpf_send_mb_msg_kfree(adapter, &xn_params, send_regions,
				     send_sz);
	if (err)
		return err;

	rcvd_regions = xn_params.recv_mem.iov_base;
	reply_sz = xn_params.recv_mem.iov_len;
	if (reply_sz < sizeof(*rcvd_regions)) {
		err = -EIO;
		goto rel_rx_buf;
	}
	num_regions = le16_to_cpu(rcvd_regions->num_memory_regions);
	size = struct_size(rcvd_regions, mem_reg, num_regions);
	if (reply_sz < size) {
		err = -EIO;
		goto rel_rx_buf;
	}

	for (int i = 0; i < num_regions; i++) {
		struct libie_mmio_info *mmio = &adapter->ctlq_ctx.mmio_info;
		resource_size_t offset, len;

		offset = le64_to_cpu(rcvd_regions->mem_reg[i].start_offset);
		len = le64_to_cpu(rcvd_regions->mem_reg[i].size);
		if (len && !libie_pci_map_mmio_region(mmio, offset, len)) {
			idpf_decfg_lan_memory_regions(adapter);
			err = -EIO;
			goto rel_rx_buf;
		}
	}

rel_rx_buf:
	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return err;
}

/**
 * idpf_map_remaining_mmio_regs - map MMIO regions outside mbx and rstat
 * @adapter: Driver specific private structure
 *
 * Called when idpf_cfg_lan_memory_regions is not supported. This will
 * calculate the offsets and sizes for the regions before, in between, and
 * after the mailbox and rstat MMIO mappings, and map those ranges.
 *
 * Return: 0 on success or error code on failure.
 */
static int idpf_map_remaining_mmio_regs(struct idpf_adapter *adapter)
{
	struct resource *rstat_reg = &adapter->dev_ops.static_reg_info[1];
	struct resource *mbx_reg = &adapter->dev_ops.static_reg_info[0];
	struct libie_mmio_info *mmio = &adapter->ctlq_ctx.mmio_info;
	resource_size_t reg_start, size;
	bool ok = true;

	/* Region preceding mailbox */
	size = mbx_reg->start;
	ok &= !size || libie_pci_map_mmio_region(mmio, 0, size);

	/* Region between mailbox and rstat */
	reg_start = mbx_reg->end + 1;
	size = rstat_reg->start - reg_start;
	ok &= !size || libie_pci_map_mmio_region(mmio, reg_start, size);

	/* Region after rstat */
	reg_start = rstat_reg->end + 1;
	size = pci_resource_len(adapter->pdev, 0) - reg_start;
	ok &= !size || libie_pci_map_mmio_region(mmio, reg_start, size);

	if (!ok) {
		idpf_decfg_lan_memory_regions(adapter);
		return -ENOMEM;
	}

	return 0;
}

/**
 * idpf_add_del_fsteer_filters - Send virtchnl add/del Flow Steering message
 * @adapter: adapter info struct
 * @rule: Flow steering rule to add/delete
 * @opcode: VIRTCHNL2_OP_ADD_FLOW_RULE to add filter, or
 *          VIRTCHNL2_OP_DEL_FLOW_RULE to delete. All other values are invalid.
 *
 * Send ADD/DELETE flow steering virtchnl message and receive the result.
 *
 * Return: 0 on success, negative on failure.
 */
int idpf_add_del_fsteer_filters(struct idpf_adapter *adapter,
				struct virtchnl2_flow_rule_add_del *rule,
				enum virtchnl2_op opcode)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.chnl_opcode = opcode,
		.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
	};
	struct virtchnl2_flow_rule_add_del *rx_rule;
	int rule_count = le32_to_cpu(rule->count);
	size_t send_sz;
	int err;

	if (opcode != VIRTCHNL2_OP_ADD_FLOW_RULE &&
	    opcode != VIRTCHNL2_OP_DEL_FLOW_RULE) {
		kfree(rule);
		return -EINVAL;
	}

	send_sz = struct_size(rule, rule_info, rule_count);
	err = idpf_send_mb_msg_kfree(adapter, &xn_params, rule, send_sz);
	if (err)
		return err;

	if (xn_params.recv_mem.iov_len < send_sz) {
		err = -EIO;
		goto rel_rx;
	}

	rx_rule = xn_params.recv_mem.iov_base;
	for (int i = 0; i < rule_count; i++) {
		if (rx_rule->rule_info[i].status !=
		    cpu_to_le32(VIRTCHNL2_FLOW_RULE_SUCCESS)) {
			err = -EIO;
			goto rel_rx;
		}
	}

rel_rx:
	libie_ctlq_release_rx_buf(&xn_params.recv_mem);
	return err;
}

/**
 * idpf_vport_alloc_max_qs - Allocate max queues for a vport
 * @adapter: Driver specific private structure
 * @max_q: vport max queue structure
 */
int idpf_vport_alloc_max_qs(struct idpf_adapter *adapter,
			    struct idpf_vport_max_q *max_q)
{
	struct idpf_avail_queue_info *avail_queues = &adapter->avail_queues;
	struct virtchnl2_get_capabilities *caps = &adapter->caps;
	u16 default_vports = idpf_get_default_vports(adapter);
	u32 max_rx_q, max_tx_q, max_buf_q, max_compl_q;

	mutex_lock(&adapter->queue_lock);

	/* Caps are device-wide. Give each vport an equal piece */
	max_rx_q = le16_to_cpu(caps->max_rx_q) / default_vports;
	max_tx_q = le16_to_cpu(caps->max_tx_q) / default_vports;
	max_buf_q = le16_to_cpu(caps->max_rx_bufq) / default_vports;
	max_compl_q = le16_to_cpu(caps->max_tx_complq) / default_vports;

	if (adapter->num_alloc_vports >= default_vports) {
		max_rx_q = IDPF_MIN_Q;
		max_tx_q = IDPF_MIN_Q;
	}

	/*
	 * Harmonize the numbers. The current implementation always creates
	 * `IDPF_MAX_BUFQS_PER_RXQ_GRP` buffer queues for each Rx queue and
	 * one completion queue for each Tx queue for best performance.
	 * If less buffer or completion queues is available, cap the number
	 * of the corresponding Rx/Tx queues.
	 */
	max_rx_q = min(max_rx_q, max_buf_q / IDPF_MAX_BUFQS_PER_RXQ_GRP);
	max_tx_q = min(max_tx_q, max_compl_q);

	max_q->max_rxq = max_rx_q;
	max_q->max_txq = max_tx_q;
	max_q->max_bufq = max_rx_q * IDPF_MAX_BUFQS_PER_RXQ_GRP;
	max_q->max_complq = max_tx_q;

	if (avail_queues->avail_rxq < max_q->max_rxq ||
	    avail_queues->avail_txq < max_q->max_txq ||
	    avail_queues->avail_bufq < max_q->max_bufq ||
	    avail_queues->avail_complq < max_q->max_complq) {
		mutex_unlock(&adapter->queue_lock);

		return -EINVAL;
	}

	avail_queues->avail_rxq -= max_q->max_rxq;
	avail_queues->avail_txq -= max_q->max_txq;
	avail_queues->avail_bufq -= max_q->max_bufq;
	avail_queues->avail_complq -= max_q->max_complq;

	mutex_unlock(&adapter->queue_lock);

	return 0;
}

/**
 * idpf_vport_dealloc_max_qs - Deallocate max queues of a vport
 * @adapter: Driver specific private structure
 * @max_q: vport max queue structure
 */
void idpf_vport_dealloc_max_qs(struct idpf_adapter *adapter,
			       struct idpf_vport_max_q *max_q)
{
	struct idpf_avail_queue_info *avail_queues;

	mutex_lock(&adapter->queue_lock);
	avail_queues = &adapter->avail_queues;

	avail_queues->avail_rxq += max_q->max_rxq;
	avail_queues->avail_txq += max_q->max_txq;
	avail_queues->avail_bufq += max_q->max_bufq;
	avail_queues->avail_complq += max_q->max_complq;

	mutex_unlock(&adapter->queue_lock);
}

/**
 * idpf_init_avail_queues - Initialize available queues on the device
 * @adapter: Driver specific private structure
 */
static void idpf_init_avail_queues(struct idpf_adapter *adapter)
{
	struct idpf_avail_queue_info *avail_queues = &adapter->avail_queues;
	struct virtchnl2_get_capabilities *caps = &adapter->caps;

	avail_queues->avail_rxq = le16_to_cpu(caps->max_rx_q);
	avail_queues->avail_txq = le16_to_cpu(caps->max_tx_q);
	avail_queues->avail_bufq = le16_to_cpu(caps->max_rx_bufq);
	avail_queues->avail_complq = le16_to_cpu(caps->max_tx_complq);
}

/**
 * idpf_vport_init_queue_reg_chunks - initialize queue register chunks
 * @vport_config: persistent vport structure to store the queue register info
 * @schunks: source chunks to copy data from
 *
 * Return: 0 on success, negative on failure.
 */
static int
idpf_vport_init_queue_reg_chunks(struct idpf_vport_config *vport_config,
				 struct virtchnl2_queue_reg_chunks *schunks)
{
	struct idpf_queue_id_reg_info *q_info = &vport_config->qid_reg_info;
	u16 num_chunks = le16_to_cpu(schunks->num_chunks);

	kfree(q_info->queue_chunks);

	q_info->queue_chunks = kzalloc_objs(*q_info->queue_chunks, num_chunks);
	if (!q_info->queue_chunks) {
		q_info->num_chunks = 0;
		return -ENOMEM;
	}

	q_info->num_chunks = num_chunks;

	for (u16 i = 0; i < num_chunks; i++) {
		struct idpf_queue_id_reg_chunk *dchunk = &q_info->queue_chunks[i];
		struct virtchnl2_queue_reg_chunk *schunk = &schunks->chunks[i];

		dchunk->qtail_reg_start = le64_to_cpu(schunk->qtail_reg_start);
		dchunk->qtail_reg_spacing = le32_to_cpu(schunk->qtail_reg_spacing);
		dchunk->type = le32_to_cpu(schunk->type);
		dchunk->start_queue_id = le32_to_cpu(schunk->start_queue_id);
		dchunk->num_queues = le32_to_cpu(schunk->num_queues);
	}

	return 0;
}

/**
 * idpf_get_reg_intr_vecs - Get vector queue register offset
 * @adapter: adapter structure to get the vector chunks
 * @reg_vals: Register offsets to store in
 * @num_vecs: number of entries the @reg_vals array can hold
 *
 * Return: number of registers that got populated
 */
int idpf_get_reg_intr_vecs(struct idpf_adapter *adapter,
			   struct idpf_vec_regs *reg_vals, int num_vecs)
{
	struct virtchnl2_vector_chunks *chunks;
	struct idpf_vec_regs reg_val;
	u16 num_vchunks, num_vec;
	int num_regs = 0, i, j;

	chunks = &adapter->req_vec_chunks->vchunks;
	num_vchunks = le16_to_cpu(chunks->num_vchunks);

	for (j = 0; j < num_vchunks; j++) {
		struct virtchnl2_vector_chunk *chunk;
		u32 dynctl_reg_spacing;
		u32 itrn_reg_spacing;

		chunk = &chunks->vchunks[j];
		num_vec = le16_to_cpu(chunk->num_vectors);
		reg_val.dyn_ctl_reg = le32_to_cpu(chunk->dynctl_reg_start);
		reg_val.itrn_reg = le32_to_cpu(chunk->itrn_reg_start);
		reg_val.itrn_index_spacing = le32_to_cpu(chunk->itrn_index_spacing);

		dynctl_reg_spacing = le32_to_cpu(chunk->dynctl_reg_spacing);
		itrn_reg_spacing = le32_to_cpu(chunk->itrn_reg_spacing);

		for (i = 0; i < num_vec && num_regs < num_vecs; i++) {
			reg_vals[num_regs].dyn_ctl_reg = reg_val.dyn_ctl_reg;
			reg_vals[num_regs].itrn_reg = reg_val.itrn_reg;
			reg_vals[num_regs].itrn_index_spacing =
						reg_val.itrn_index_spacing;

			reg_val.dyn_ctl_reg += dynctl_reg_spacing;
			reg_val.itrn_reg += itrn_reg_spacing;
			num_regs++;
		}
	}

	return num_regs;
}

/**
 * idpf_vport_get_q_reg - Get the queue registers for the vport
 * @reg_vals: register values needing to be set
 * @num_regs: amount we expect to fill
 * @q_type: queue model
 * @chunks: queue regs received over mailbox
 *
 * This function parses the queue register offsets from the queue register
 * chunk information, with a specific queue type and stores it into the array
 * passed as an argument. It returns the actual number of queue registers that
 * are filled.
 */
static int idpf_vport_get_q_reg(u32 *reg_vals, int num_regs, u32 q_type,
				struct idpf_queue_id_reg_info *chunks)
{
	u16 num_chunks = chunks->num_chunks;
	int reg_filled = 0, i;
	u32 reg_val;

	while (num_chunks--) {
		struct idpf_queue_id_reg_chunk *chunk;
		u16 num_q;

		chunk = &chunks->queue_chunks[num_chunks];
		if (chunk->type != q_type)
			continue;

		num_q = chunk->num_queues;
		reg_val = chunk->qtail_reg_start;
		for (i = 0; i < num_q && reg_filled < num_regs ; i++) {
			reg_vals[reg_filled++] = reg_val;
			reg_val += chunk->qtail_reg_spacing;
		}
	}

	return reg_filled;
}

/**
 * __idpf_queue_reg_init - initialize queue registers
 * @vport: virtual port structure
 * @rsrc: pointer to queue and vector resources
 * @reg_vals: registers we are initializing
 * @num_regs: how many registers there are in total
 * @q_type: queue model
 *
 * Return number of queues that are initialized
 */
static int __idpf_queue_reg_init(struct idpf_vport *vport,
				 struct idpf_q_vec_rsrc *rsrc, u32 *reg_vals,
				 int num_regs, u32 q_type)
{
	struct libie_mmio_info *mmio = &vport->adapter->ctlq_ctx.mmio_info;
	int i, j, k = 0;

	switch (q_type) {
	case VIRTCHNL2_QUEUE_TYPE_TX:
		for (i = 0; i < rsrc->num_txq_grp; i++) {
			struct idpf_txq_group *tx_qgrp = &rsrc->txq_grps[i];

			for (j = 0; j < tx_qgrp->num_txq && k < num_regs; j++, k++)
				tx_qgrp->txqs[j]->tail =
					libie_pci_get_mmio_addr(mmio,
								reg_vals[k]);
		}
		break;
	case VIRTCHNL2_QUEUE_TYPE_RX:
		for (i = 0; i < rsrc->num_rxq_grp; i++) {
			struct idpf_rxq_group *rx_qgrp = &rsrc->rxq_grps[i];
			u16 num_rxq = rx_qgrp->singleq.num_rxq;

			for (j = 0; j < num_rxq && k < num_regs; j++, k++) {
				struct idpf_rx_queue *q;

				q = rx_qgrp->singleq.rxqs[j];
				q->tail = libie_pci_get_mmio_addr(mmio,
								  reg_vals[k]);
			}
		}
		break;
	case VIRTCHNL2_QUEUE_TYPE_RX_BUFFER:
		for (i = 0; i < rsrc->num_rxq_grp; i++) {
			struct idpf_rxq_group *rx_qgrp = &rsrc->rxq_grps[i];
			u8 num_bufqs = rsrc->num_bufqs_per_qgrp;

			for (j = 0; j < num_bufqs && k < num_regs; j++, k++) {
				struct idpf_buf_queue *q;

				q = &rx_qgrp->splitq.bufq_sets[j].bufq;
				q->tail = libie_pci_get_mmio_addr(mmio,
								  reg_vals[k]);
			}
		}
		break;
	default:
		break;
	}

	return k;
}

/**
 * idpf_queue_reg_init - initialize queue registers
 * @vport: virtual port structure
 * @rsrc: pointer to queue and vector resources
 * @chunks: queue registers received over mailbox
 *
 * Return: 0 on success, negative on failure
 */
int idpf_queue_reg_init(struct idpf_vport *vport,
			struct idpf_q_vec_rsrc *rsrc,
			struct idpf_queue_id_reg_info *chunks)
{
	int num_regs, ret = 0;
	u32 *reg_vals;

	/* We may never deal with more than 256 same type of queues */
	reg_vals = kzalloc(sizeof(void *) * IDPF_LARGE_MAX_Q, GFP_KERNEL);
	if (!reg_vals)
		return -ENOMEM;

	/* Initialize Tx queue tail register address */
	num_regs = idpf_vport_get_q_reg(reg_vals, IDPF_LARGE_MAX_Q,
					VIRTCHNL2_QUEUE_TYPE_TX,
					chunks);
	if (num_regs < rsrc->num_txq) {
		ret = -EINVAL;
		goto free_reg_vals;
	}

	num_regs = __idpf_queue_reg_init(vport, rsrc, reg_vals, num_regs,
					 VIRTCHNL2_QUEUE_TYPE_TX);
	if (num_regs < rsrc->num_txq) {
		ret = -EINVAL;
		goto free_reg_vals;
	}

	/* Initialize Rx/buffer queue tail register address based on Rx queue
	 * model
	 */
	if (idpf_is_queue_model_split(rsrc->rxq_model)) {
		num_regs = idpf_vport_get_q_reg(reg_vals, IDPF_LARGE_MAX_Q,
						VIRTCHNL2_QUEUE_TYPE_RX_BUFFER,
						chunks);
		if (num_regs < rsrc->num_bufq) {
			ret = -EINVAL;
			goto free_reg_vals;
		}

		num_regs = __idpf_queue_reg_init(vport, rsrc, reg_vals, num_regs,
						 VIRTCHNL2_QUEUE_TYPE_RX_BUFFER);
		if (num_regs < rsrc->num_bufq) {
			ret = -EINVAL;
			goto free_reg_vals;
		}
	} else {
		num_regs = idpf_vport_get_q_reg(reg_vals, IDPF_LARGE_MAX_Q,
						VIRTCHNL2_QUEUE_TYPE_RX,
						chunks);
		if (num_regs < rsrc->num_rxq) {
			ret = -EINVAL;
			goto free_reg_vals;
		}

		num_regs = __idpf_queue_reg_init(vport, rsrc, reg_vals, num_regs,
						 VIRTCHNL2_QUEUE_TYPE_RX);
		if (num_regs < rsrc->num_rxq) {
			ret = -EINVAL;
			goto free_reg_vals;
		}
	}

free_reg_vals:
	kfree(reg_vals);

	return ret;
}

/**
 * idpf_send_create_vport_msg - Send virtchnl create vport message
 * @adapter: Driver specific private structure
 * @max_q: vport max queue info
 *
 * send virtchnl creae vport message
 *
 * Returns 0 on success, negative on failure
 */
int idpf_send_create_vport_msg(struct idpf_adapter *adapter,
			       struct idpf_vport_max_q *max_q)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= VIRTCHNL2_OP_CREATE_VPORT,
	};
	struct virtchnl2_create_vport *vport_msg;
	u16 idx = adapter->next_vport;
	int err, buf_size;

	buf_size = sizeof(struct virtchnl2_create_vport);
	vport_msg = kzalloc(buf_size, GFP_KERNEL);
	if (!vport_msg)
		return -ENOMEM;

	vport_msg->vport_type = cpu_to_le16(VIRTCHNL2_VPORT_TYPE_DEFAULT);
	vport_msg->vport_index = cpu_to_le16(idx);

	if (adapter->req_tx_splitq || !IS_ENABLED(CONFIG_IDPF_SINGLEQ))
		vport_msg->txq_model = cpu_to_le16(VIRTCHNL2_QUEUE_MODEL_SPLIT);
	else
		vport_msg->txq_model = cpu_to_le16(VIRTCHNL2_QUEUE_MODEL_SINGLE);

	if (adapter->req_rx_splitq || !IS_ENABLED(CONFIG_IDPF_SINGLEQ))
		vport_msg->rxq_model = cpu_to_le16(VIRTCHNL2_QUEUE_MODEL_SPLIT);
	else
		vport_msg->rxq_model = cpu_to_le16(VIRTCHNL2_QUEUE_MODEL_SINGLE);

	err = idpf_vport_calc_total_qs(adapter, idx, vport_msg, max_q);
	if (err) {
		dev_err(&adapter->pdev->dev, "Enough queues are not available");
		goto rel_buf;
	}

	if (!adapter->vport_params_recvd[idx]) {
		adapter->vport_params_recvd[idx] =
			kzalloc(LIBIE_CTLQ_MAX_BUF_LEN, GFP_KERNEL);
		if (!adapter->vport_params_recvd[idx]) {
			err = -ENOMEM;
			goto rel_buf;
		}
	}

	err = idpf_send_mb_msg_kfree(adapter, &xn_params, vport_msg,
				     sizeof(*vport_msg));
	if (err) {
		kfree(adapter->vport_params_recvd[idx]);
		adapter->vport_params_recvd[idx] = NULL;
		return err;
	}

	memcpy(adapter->vport_params_recvd[idx], xn_params.recv_mem.iov_base,
	       xn_params.recv_mem.iov_len);

	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return 0;

rel_buf:
	kfree(vport_msg);

	return err;
}

/**
 * idpf_check_supported_desc_ids - Verify we have required descriptor support
 * @vport: virtual port structure
 *
 * Return 0 on success, error on failure
 */
int idpf_check_supported_desc_ids(struct idpf_vport *vport)
{
	struct idpf_q_vec_rsrc *rsrc = &vport->dflt_qv_rsrc;
	struct idpf_adapter *adapter = vport->adapter;
	struct virtchnl2_create_vport *vport_msg;
	u64 rx_desc_ids, tx_desc_ids;

	vport_msg = adapter->vport_params_recvd[vport->idx];

	if (!IS_ENABLED(CONFIG_IDPF_SINGLEQ) &&
	    (vport_msg->rxq_model == VIRTCHNL2_QUEUE_MODEL_SINGLE ||
	     vport_msg->txq_model == VIRTCHNL2_QUEUE_MODEL_SINGLE)) {
		pci_err(adapter->pdev, "singleq mode requested, but not compiled-in\n");
		return -EOPNOTSUPP;
	}

	rx_desc_ids = le64_to_cpu(vport_msg->rx_desc_ids);
	tx_desc_ids = le64_to_cpu(vport_msg->tx_desc_ids);

	if (idpf_is_queue_model_split(rsrc->rxq_model)) {
		if (!(rx_desc_ids & VIRTCHNL2_RXDID_2_FLEX_SPLITQ_M)) {
			dev_info(&adapter->pdev->dev, "Minimum RX descriptor support not provided, using the default\n");
			vport_msg->rx_desc_ids = cpu_to_le64(VIRTCHNL2_RXDID_2_FLEX_SPLITQ_M);
		}
	} else {
		if (!(rx_desc_ids & VIRTCHNL2_RXDID_2_FLEX_SQ_NIC_M))
			rsrc->base_rxd = true;
	}

	if (!idpf_is_queue_model_split(rsrc->txq_model))
		return 0;

	if ((tx_desc_ids & MIN_SUPPORT_TXDID) != MIN_SUPPORT_TXDID) {
		dev_info(&adapter->pdev->dev, "Minimum TX descriptor support not provided, using the default\n");
		vport_msg->tx_desc_ids = cpu_to_le64(MIN_SUPPORT_TXDID);
	}

	return 0;
}

/**
 * idpf_send_destroy_vport_msg - Send virtchnl destroy vport message
 * @adapter: adapter pointer used to send virtchnl message
 * @vport_id: vport identifier used while preparing the virtchnl message
 *
 * Return: 0 on success, negative on failure.
 */
int idpf_send_destroy_vport_msg(struct idpf_adapter *adapter, u32 vport_id)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= VIRTCHNL2_OP_DESTROY_VPORT,
	};
	struct virtchnl2_vport v_id;
	int err;

	v_id.vport_id = cpu_to_le32(vport_id);

	err = idpf_send_mb_msg_stack(adapter, &xn_params, &v_id);
	if (err)
		return err;

	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return 0;
}

/**
 * idpf_send_enable_vport_msg - Send virtchnl enable vport message
 * @adapter: adapter pointer used to send virtchnl message
 * @vport_id: vport identifier used while preparing the virtchnl message
 *
 * Return: 0 on success, negative on failure.
 */
int idpf_send_enable_vport_msg(struct idpf_adapter *adapter, u32 vport_id)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= VIRTCHNL2_OP_ENABLE_VPORT,
	};
	struct virtchnl2_vport v_id;
	int err;

	v_id.vport_id = cpu_to_le32(vport_id);

	err = idpf_send_mb_msg_stack(adapter, &xn_params, &v_id);
	if (err)
		return err;

	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return 0;
}

/**
 * idpf_send_disable_vport_msg - Send virtchnl disable vport message
 * @adapter: adapter pointer used to send virtchnl message
 * @vport_id: vport identifier used while preparing the virtchnl message
 *
 * Return: 0 on success, negative on failure.
 */
int idpf_send_disable_vport_msg(struct idpf_adapter *adapter, u32 vport_id)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= VIRTCHNL2_OP_DISABLE_VPORT,
	};
	struct virtchnl2_vport v_id;
	int err;

	v_id.vport_id = cpu_to_le32(vport_id);

	err = idpf_send_mb_msg_stack(adapter, &xn_params, &v_id);
	if (err)
		return err;

	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return 0;
}

/**
 * idpf_fill_txq_config_chunk - fill chunk describing the Tx queue
 * @rsrc: pointer to queue and vector resources
 * @q: Tx queue to be inserted into VC chunk
 * @qi: pointer to the buffer containing the VC chunk
 */
static void idpf_fill_txq_config_chunk(const struct idpf_q_vec_rsrc *rsrc,
				       const struct idpf_tx_queue *q,
				       struct virtchnl2_txq_info *qi)
{
	u32 val;

	qi->queue_id = cpu_to_le32(q->q_id);
	qi->model = cpu_to_le16(rsrc->txq_model);
	qi->type = cpu_to_le32(VIRTCHNL2_QUEUE_TYPE_TX);
	qi->ring_len = cpu_to_le16(q->desc_count);
	qi->dma_ring_addr = cpu_to_le64(q->dma);
	qi->relative_queue_id = cpu_to_le16(q->rel_q_id);

	if (!idpf_is_queue_model_split(rsrc->txq_model)) {
		qi->sched_mode = cpu_to_le16(VIRTCHNL2_TXQ_SCHED_MODE_QUEUE);
		return;
	}

	if (idpf_queue_has(XDP, q))
		val = q->complq->q_id;
	else
		val = q->txq_grp->complq->q_id;

	qi->tx_compl_queue_id = cpu_to_le16(val);

	if (idpf_queue_has(FLOW_SCH_EN, q))
		val = VIRTCHNL2_TXQ_SCHED_MODE_FLOW;
	else
		val = VIRTCHNL2_TXQ_SCHED_MODE_QUEUE;

	qi->sched_mode = cpu_to_le16(val);
}

/**
 * idpf_fill_complq_config_chunk - fill chunk describing the completion queue
 * @rsrc: pointer to queue and vector resources
 * @q: completion queue to be inserted into VC chunk
 * @qi: pointer to the buffer containing the VC chunk
 */
static void idpf_fill_complq_config_chunk(const struct idpf_q_vec_rsrc *rsrc,
					  const struct idpf_compl_queue *q,
					  struct virtchnl2_txq_info *qi)
{
	u32 val;

	qi->queue_id = cpu_to_le32(q->q_id);
	qi->model = cpu_to_le16(rsrc->txq_model);
	qi->type = cpu_to_le32(VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION);
	qi->ring_len = cpu_to_le16(q->desc_count);
	qi->dma_ring_addr = cpu_to_le64(q->dma);

	if (idpf_queue_has(FLOW_SCH_EN, q))
		val = VIRTCHNL2_TXQ_SCHED_MODE_FLOW;
	else
		val = VIRTCHNL2_TXQ_SCHED_MODE_QUEUE;

	qi->sched_mode = cpu_to_le16(val);
}

/**
 * idpf_prepare_cfg_txqs_msg - prepare message to configure selected Tx queues
 * @vport_id: ID of virtual port queues are associated with
 * @buf: buffer containing the message
 * @pos: pointer to the first chunk describing the tx queue
 * @num_chunks: number of chunks in the message
 *
 * Helper function for preparing the message describing configuration of
 * Tx queues.
 *
 * Return: the total size of the prepared message.
 */
static u32 idpf_prepare_cfg_txqs_msg(u32 vport_id, void *buf, const void *pos,
				     u32 num_chunks)
{
	struct virtchnl2_config_tx_queues *ctq = buf;

	ctq->vport_id = cpu_to_le32(vport_id);
	ctq->num_qinfo = cpu_to_le16(num_chunks);
	memcpy(ctq->qinfo, pos, num_chunks * sizeof(*ctq->qinfo));

	return struct_size(ctq, qinfo, num_chunks);
}

/**
 * idpf_send_config_tx_queue_set_msg - send virtchnl config Tx queues
 *				       message for selected queues
 * @qs: set of the Tx queues to configure
 *
 * Send config queues virtchnl message for queues contained in the @qs array.
 * The @qs array can contain Tx queues (or completion queues) only.
 *
 * Return: 0 on success, -errno on failure.
 */
static int idpf_send_config_tx_queue_set_msg(const struct idpf_queue_set *qs)
{
	struct virtchnl2_txq_info *qi __free(kfree) = NULL;
	struct idpf_chunked_msg_params params = {
		.vport_id	= qs->vport_id,
		.vc_op		= VIRTCHNL2_OP_CONFIG_TX_QUEUES,
		.prepare_msg	= idpf_prepare_cfg_txqs_msg,
		.config_sz	= sizeof(struct virtchnl2_config_tx_queues),
		.chunk_sz	= sizeof(*qi),
	};

	qi = kzalloc_objs(*qi, qs->num);
	if (!qi)
		return -ENOMEM;

	params.chunks = qi;

	for (u32 i = 0; i < qs->num; i++) {
		if (qs->qs[i].type == VIRTCHNL2_QUEUE_TYPE_TX)
			idpf_fill_txq_config_chunk(qs->qv_rsrc, qs->qs[i].txq,
						   &qi[params.num_chunks++]);
		else if (qs->qs[i].type == VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION)
			idpf_fill_complq_config_chunk(qs->qv_rsrc,
						      qs->qs[i].complq,
						      &qi[params.num_chunks++]);
	}

	return idpf_send_chunked_msg(qs->adapter, &params);
}

/**
 * idpf_send_config_tx_queues_msg - send virtchnl config Tx queues message
 * @adapter: adapter pointer used to send virtchnl message
 * @rsrc: pointer to queue and vector resources
 * @vport_id: vport identifier used while preparing the virtchnl message
 *
 * Return: 0 on success, -errno on failure.
 */
static int idpf_send_config_tx_queues_msg(struct idpf_adapter *adapter,
					  struct idpf_q_vec_rsrc *rsrc,
					  u32 vport_id)
{
	struct idpf_queue_set *qs __free(kfree) = NULL;
	u32 totqs = rsrc->num_txq + rsrc->num_complq;
	u32 k = 0;

	qs = idpf_alloc_queue_set(adapter, rsrc, vport_id, totqs);
	if (!qs)
		return -ENOMEM;

	/* Populate the queue info buffer with all queue context info */
	for (u32 i = 0; i < rsrc->num_txq_grp; i++) {
		const struct idpf_txq_group *tx_qgrp = &rsrc->txq_grps[i];

		for (u32 j = 0; j < tx_qgrp->num_txq; j++) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_TX;
			qs->qs[k++].txq = tx_qgrp->txqs[j];
		}

		if (idpf_is_queue_model_split(rsrc->txq_model)) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION;
			qs->qs[k++].complq = tx_qgrp->complq;
		}
	}

	/* Make sure accounting agrees */
	if (k != totqs)
		return -EINVAL;

	return idpf_send_config_tx_queue_set_msg(qs);
}

/**
 * idpf_fill_rxq_config_chunk - fill chunk describing the Rx queue
 * @rsrc: pointer to queue and vector resources
 * @q: Rx queue to be inserted into VC chunk
 * @qi: pointer to the buffer containing the VC chunk
 */
static void idpf_fill_rxq_config_chunk(const struct idpf_q_vec_rsrc *rsrc,
				       struct idpf_rx_queue *q,
				       struct virtchnl2_rxq_info *qi)
{
	const struct idpf_bufq_set *sets;

	qi->queue_id = cpu_to_le32(q->q_id);
	qi->model = cpu_to_le16(rsrc->rxq_model);
	qi->type = cpu_to_le32(VIRTCHNL2_QUEUE_TYPE_RX);
	qi->ring_len = cpu_to_le16(q->desc_count);
	qi->dma_ring_addr = cpu_to_le64(q->dma);
	qi->max_pkt_size = cpu_to_le32(q->rx_max_pkt_size);
	qi->rx_buffer_low_watermark = cpu_to_le16(q->rx_buffer_low_watermark);
	qi->qflags = cpu_to_le16(VIRTCHNL2_RX_DESC_SIZE_32BYTE);
	if (idpf_queue_has(RSC_EN, q))
		qi->qflags |= cpu_to_le16(VIRTCHNL2_RXQ_RSC);

	if (!idpf_is_queue_model_split(rsrc->rxq_model)) {
		qi->data_buffer_size = cpu_to_le32(q->rx_buf_size);
		qi->desc_ids = cpu_to_le64(q->rxdids);

		return;
	}

	sets = q->bufq_sets;

	/*
	 * In splitq mode, RxQ buffer size should be set to that of the first
	 * buffer queue associated with this RxQ.
	 */
	q->rx_buf_size = sets[0].bufq.rx_buf_size;
	qi->data_buffer_size = cpu_to_le32(q->rx_buf_size);

	qi->rx_bufq1_id = cpu_to_le16(sets[0].bufq.q_id);
	if (rsrc->num_bufqs_per_qgrp > IDPF_SINGLE_BUFQ_PER_RXQ_GRP) {
		qi->bufq2_ena = IDPF_BUFQ2_ENA;
		qi->rx_bufq2_id = cpu_to_le16(sets[1].bufq.q_id);
	}

	q->rx_hbuf_size = sets[0].bufq.rx_hbuf_size;

	if (idpf_queue_has(HSPLIT_EN, q)) {
		qi->qflags |= cpu_to_le16(VIRTCHNL2_RXQ_HDR_SPLIT);
		qi->hdr_buffer_size = cpu_to_le16(q->rx_hbuf_size);
	}

	qi->desc_ids = cpu_to_le64(VIRTCHNL2_RXDID_2_FLEX_SPLITQ_M);
}

/**
 * idpf_fill_bufq_config_chunk - fill chunk describing the buffer queue
 * @rsrc: pointer to queue and vector resources
 * @q: buffer queue to be inserted into VC chunk
 * @qi: pointer to the buffer containing the VC chunk
 */
static void idpf_fill_bufq_config_chunk(const struct idpf_q_vec_rsrc *rsrc,
					const struct idpf_buf_queue *q,
					struct virtchnl2_rxq_info *qi)
{
	qi->queue_id = cpu_to_le32(q->q_id);
	qi->model = cpu_to_le16(rsrc->rxq_model);
	qi->type = cpu_to_le32(VIRTCHNL2_QUEUE_TYPE_RX_BUFFER);
	qi->ring_len = cpu_to_le16(q->desc_count);
	qi->dma_ring_addr = cpu_to_le64(q->dma);
	qi->data_buffer_size = cpu_to_le32(q->rx_buf_size);
	qi->rx_buffer_low_watermark = cpu_to_le16(q->rx_buffer_low_watermark);
	qi->desc_ids = cpu_to_le64(VIRTCHNL2_RXDID_2_FLEX_SPLITQ_M);
	qi->buffer_notif_stride = IDPF_RX_BUF_STRIDE;
	if (idpf_queue_has(RSC_EN, q))
		qi->qflags = cpu_to_le16(VIRTCHNL2_RXQ_RSC);

	if (idpf_queue_has(HSPLIT_EN, q)) {
		qi->qflags |= cpu_to_le16(VIRTCHNL2_RXQ_HDR_SPLIT);
		qi->hdr_buffer_size = cpu_to_le16(q->rx_hbuf_size);
	}
}

/**
 * idpf_prepare_cfg_rxqs_msg - prepare message to configure selected Rx queues
 * @vport_id: ID of virtual port queues are associated with
 * @buf: buffer containing the message
 * @pos: pointer to the first chunk describing the rx queue
 * @num_chunks: number of chunks in the message
 *
 * Helper function for preparing the message describing configuration of
 * Rx queues.
 *
 * Return: the total size of the prepared message.
 */
static u32 idpf_prepare_cfg_rxqs_msg(u32 vport_id, void *buf, const void *pos,
				     u32 num_chunks)
{
	struct virtchnl2_config_rx_queues *crq = buf;

	crq->vport_id = cpu_to_le32(vport_id);
	crq->num_qinfo = cpu_to_le16(num_chunks);
	memcpy(crq->qinfo, pos, num_chunks * sizeof(*crq->qinfo));

	return struct_size(crq, qinfo, num_chunks);
}

/**
 * idpf_send_config_rx_queue_set_msg - send virtchnl config Rx queues message
 *				       for selected queues.
 * @qs: set of the Rx queues to configure
 *
 * Send config queues virtchnl message for queues contained in the @qs array.
 * The @qs array can contain Rx queues (or buffer queues) only.
 *
 * Return: 0 on success, -errno on failure.
 */
static int idpf_send_config_rx_queue_set_msg(const struct idpf_queue_set *qs)
{
	struct virtchnl2_rxq_info *qi __free(kfree) = NULL;
	struct idpf_chunked_msg_params params = {
		.vport_id	= qs->vport_id,
		.vc_op		= VIRTCHNL2_OP_CONFIG_RX_QUEUES,
		.prepare_msg	= idpf_prepare_cfg_rxqs_msg,
		.config_sz	= sizeof(struct virtchnl2_config_rx_queues),
		.chunk_sz	= sizeof(*qi),
	};

	qi = kzalloc_objs(*qi, qs->num);
	if (!qi)
		return -ENOMEM;

	params.chunks = qi;

	for (u32 i = 0; i < qs->num; i++) {
		if (qs->qs[i].type == VIRTCHNL2_QUEUE_TYPE_RX)
			idpf_fill_rxq_config_chunk(qs->qv_rsrc, qs->qs[i].rxq,
						   &qi[params.num_chunks++]);
		else if (qs->qs[i].type == VIRTCHNL2_QUEUE_TYPE_RX_BUFFER)
			idpf_fill_bufq_config_chunk(qs->qv_rsrc, qs->qs[i].bufq,
						    &qi[params.num_chunks++]);
	}

	return idpf_send_chunked_msg(qs->adapter, &params);
}

/**
 * idpf_send_config_rx_queues_msg - send virtchnl config Rx queues message
 * @adapter: adapter pointer used to send virtchnl message
 * @rsrc: pointer to queue and vector resources
 * @vport_id: vport identifier used while preparing the virtchnl message
 *
 * Return: 0 on success, -errno on failure.
 */
static int idpf_send_config_rx_queues_msg(struct idpf_adapter *adapter,
					  struct idpf_q_vec_rsrc *rsrc,
					  u32 vport_id)
{
	bool splitq = idpf_is_queue_model_split(rsrc->rxq_model);
	struct idpf_queue_set *qs __free(kfree) = NULL;
	u32 totqs = rsrc->num_rxq + rsrc->num_bufq;
	u32 k = 0;

	qs = idpf_alloc_queue_set(adapter, rsrc, vport_id, totqs);
	if (!qs)
		return -ENOMEM;

	/* Populate the queue info buffer with all queue context info */
	for (u32 i = 0; i < rsrc->num_rxq_grp; i++) {
		const struct idpf_rxq_group *rx_qgrp = &rsrc->rxq_grps[i];
		u32 num_rxq;

		if (!splitq) {
			num_rxq = rx_qgrp->singleq.num_rxq;
			goto rxq;
		}

		for (u32 j = 0; j < rsrc->num_bufqs_per_qgrp; j++) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_RX_BUFFER;
			qs->qs[k++].bufq = &rx_qgrp->splitq.bufq_sets[j].bufq;
		}

		num_rxq = rx_qgrp->splitq.num_rxq_sets;

rxq:
		for (u32 j = 0; j < num_rxq; j++) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_RX;

			if (splitq)
				qs->qs[k++].rxq =
					&rx_qgrp->splitq.rxq_sets[j]->rxq;
			else
				qs->qs[k++].rxq = rx_qgrp->singleq.rxqs[j];
		}
	}

	/* Make sure accounting agrees */
	if (k != totqs)
		return -EINVAL;

	return idpf_send_config_rx_queue_set_msg(qs);
}

/**
 * idpf_prepare_ena_dis_qs_msg - prepare message to enable/disable selected
 *				 queues
 * @vport_id: ID of virtual port queues are associated with
 * @buf: buffer containing the message
 * @pos: pointer to the first chunk describing the queue
 * @num_chunks: number of chunks in the message
 *
 * Helper function for preparing the message describing queues to be enabled
 * or disabled.
 *
 * Return: the total size of the prepared message.
 */
static u32 idpf_prepare_ena_dis_qs_msg(u32 vport_id, void *buf, const void *pos,
				       u32 num_chunks)
{
	struct virtchnl2_del_ena_dis_queues *eq = buf;

	eq->vport_id = cpu_to_le32(vport_id);
	eq->chunks.num_chunks = cpu_to_le16(num_chunks);
	memcpy(eq->chunks.chunks, pos,
	       num_chunks * sizeof(*eq->chunks.chunks));

	return struct_size(eq, chunks.chunks, num_chunks);
}

/**
 * idpf_send_ena_dis_queue_set_msg - send virtchnl enable or disable queues
 *				     message for selected queues
 * @qs: set of the queues to enable or disable
 * @en: whether to enable or disable queues
 *
 * Send enable or disable queues virtchnl message for queues contained
 * in the @qs array.
 * The @qs array can contain pointers to both Rx and Tx queues.
 *
 * Return: 0 on success, -errno on failure.
 */
static int idpf_send_ena_dis_queue_set_msg(const struct idpf_queue_set *qs,
					   bool en)
{
	struct virtchnl2_queue_chunk *qc __free(kfree) = NULL;
	struct idpf_chunked_msg_params params = {
		.vport_id	= qs->vport_id,
		.vc_op		= en ? VIRTCHNL2_OP_ENABLE_QUEUES :
				       VIRTCHNL2_OP_DISABLE_QUEUES,
		.prepare_msg	= idpf_prepare_ena_dis_qs_msg,
		.config_sz	= sizeof(struct virtchnl2_del_ena_dis_queues),
		.chunk_sz	= sizeof(*qc),
		.num_chunks	= qs->num,
	};

	qc = kzalloc_objs(*qc, qs->num);
	if (!qc)
		return -ENOMEM;

	params.chunks = qc;

	for (u32 i = 0; i < qs->num; i++) {
		const struct idpf_queue_ptr *q = &qs->qs[i];
		u32 qid;

		qc[i].type = cpu_to_le32(q->type);
		qc[i].num_queues = cpu_to_le32(IDPF_NUMQ_PER_CHUNK);

		switch (q->type) {
		case VIRTCHNL2_QUEUE_TYPE_RX:
			qid = q->rxq->q_id;
			break;
		case VIRTCHNL2_QUEUE_TYPE_TX:
			qid = q->txq->q_id;
			break;
		case VIRTCHNL2_QUEUE_TYPE_RX_BUFFER:
			qid = q->bufq->q_id;
			break;
		case VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION:
			qid = q->complq->q_id;
			break;
		default:
			return -EINVAL;
		}

		qc[i].start_queue_id = cpu_to_le32(qid);
	}

	return idpf_send_chunked_msg(qs->adapter, &params);
}

/**
 * idpf_send_ena_dis_queues_msg - send virtchnl enable or disable queues
 *				  message
 * @adapter: adapter pointer used to send virtchnl message
 * @rsrc: pointer to queue and vector resources
 * @vport_id: vport identifier used while preparing the virtchnl message
 * @en: whether to enable or disable queues
 *
 * Return: 0 on success, -errno on failure.
 */
static int idpf_send_ena_dis_queues_msg(struct idpf_adapter *adapter,
					struct idpf_q_vec_rsrc *rsrc,
					u32 vport_id, bool en)
{
	struct idpf_queue_set *qs __free(kfree) = NULL;
	u32 num_txq, num_q, k = 0;
	bool split;

	num_txq = rsrc->num_txq + rsrc->num_complq;
	num_q = num_txq + rsrc->num_rxq + rsrc->num_bufq;

	qs = idpf_alloc_queue_set(adapter, rsrc, vport_id, num_q);
	if (!qs)
		return -ENOMEM;

	split = idpf_is_queue_model_split(rsrc->txq_model);

	for (u32 i = 0; i < rsrc->num_txq_grp; i++) {
		const struct idpf_txq_group *tx_qgrp = &rsrc->txq_grps[i];

		for (u32 j = 0; j < tx_qgrp->num_txq; j++) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_TX;
			qs->qs[k++].txq = tx_qgrp->txqs[j];
		}

		if (!split)
			continue;

		qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION;
		qs->qs[k++].complq = tx_qgrp->complq;
	}

	if (k != num_txq)
		return -EINVAL;

	split = idpf_is_queue_model_split(rsrc->rxq_model);

	for (u32 i = 0; i < rsrc->num_rxq_grp; i++) {
		const struct idpf_rxq_group *rx_qgrp = &rsrc->rxq_grps[i];
		u32 num_rxq;

		if (split)
			num_rxq = rx_qgrp->splitq.num_rxq_sets;
		else
			num_rxq = rx_qgrp->singleq.num_rxq;

		for (u32 j = 0; j < num_rxq; j++) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_RX;

			if (split)
				qs->qs[k++].rxq =
					&rx_qgrp->splitq.rxq_sets[j]->rxq;
			else
				qs->qs[k++].rxq = rx_qgrp->singleq.rxqs[j];
		}

		if (!split)
			continue;

		for (u32 j = 0; j < rsrc->num_bufqs_per_qgrp; j++) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_RX_BUFFER;
			qs->qs[k++].bufq = &rx_qgrp->splitq.bufq_sets[j].bufq;
		}
	}

	if (k != num_q)
		return -EINVAL;

	return idpf_send_ena_dis_queue_set_msg(qs, en);
}

/**
 * idpf_prep_map_unmap_queue_set_vector_msg - prepare message to map or unmap
 *					      queue set to the interrupt vector
 * @vport_id: ID of virtual port queues are associated with
 * @buf: buffer containing the message
 * @pos: pointer to the first chunk describing the vector mapping
 * @num_chunks: number of chunks in the message
 *
 * Helper function for preparing the message describing mapping queues to
 * q_vectors.
 *
 * Return: the total size of the prepared message.
 */
static u32
idpf_prep_map_unmap_queue_set_vector_msg(u32 vport_id, void *buf,
					 const void *pos, u32 num_chunks)
{
	struct virtchnl2_queue_vector_maps *vqvm = buf;

	vqvm->vport_id = cpu_to_le32(vport_id);
	vqvm->num_qv_maps = cpu_to_le16(num_chunks);
	memcpy(vqvm->qv_maps, pos, num_chunks * sizeof(*vqvm->qv_maps));

	return struct_size(vqvm, qv_maps, num_chunks);
}

/**
 * idpf_send_map_unmap_queue_set_vector_msg - send virtchnl map or unmap
 *					      queue set vector message
 * @qs: set of the queues to map or unmap
 * @map: true for map and false for unmap
 *
 * Return: 0 on success, -errno on failure.
 */
static int
idpf_send_map_unmap_queue_set_vector_msg(const struct idpf_queue_set *qs,
					 bool map)
{
	struct virtchnl2_queue_vector *vqv __free(kfree) = NULL;
	struct idpf_chunked_msg_params params = {
		.vport_id	= qs->vport_id,
		.vc_op		= map ? VIRTCHNL2_OP_MAP_QUEUE_VECTOR :
					VIRTCHNL2_OP_UNMAP_QUEUE_VECTOR,
		.prepare_msg	= idpf_prep_map_unmap_queue_set_vector_msg,
		.config_sz	= sizeof(struct virtchnl2_queue_vector_maps),
		.chunk_sz	= sizeof(*vqv),
		.num_chunks	= qs->num,
	};
	bool split;

	vqv = kzalloc_objs(*vqv, qs->num);
	if (!vqv)
		return -ENOMEM;

	params.chunks = vqv;

	split = idpf_is_queue_model_split(qs->qv_rsrc->txq_model);

	for (u32 i = 0; i < qs->num; i++) {
		const struct idpf_queue_ptr *q = &qs->qs[i];
		const struct idpf_q_vector *vec;
		u32 qid, v_idx, itr_idx;

		vqv[i].queue_type = cpu_to_le32(q->type);

		switch (q->type) {
		case VIRTCHNL2_QUEUE_TYPE_RX:
			qid = q->rxq->q_id;

			if (idpf_queue_has(NOIRQ, q->rxq))
				vec = NULL;
			else
				vec = q->rxq->q_vector;

			if (vec) {
				v_idx = vec->v_idx;
				itr_idx = vec->rx_itr_idx;
			} else {
				v_idx = qs->qv_rsrc->noirq_v_idx;
				itr_idx = VIRTCHNL2_ITR_IDX_0;
			}
			break;
		case VIRTCHNL2_QUEUE_TYPE_TX:
			qid = q->txq->q_id;

			if (idpf_queue_has(NOIRQ, q->txq))
				vec = NULL;
			else if (idpf_queue_has(XDP, q->txq))
				vec = q->txq->complq->q_vector;
			else if (split)
				vec = q->txq->txq_grp->complq->q_vector;
			else
				vec = q->txq->q_vector;

			if (vec) {
				v_idx = vec->v_idx;
				itr_idx = vec->tx_itr_idx;
			} else {
				v_idx = qs->qv_rsrc->noirq_v_idx;
				itr_idx = VIRTCHNL2_ITR_IDX_1;
			}
			break;
		default:
			return -EINVAL;
		}

		vqv[i].queue_id = cpu_to_le32(qid);
		vqv[i].vector_id = cpu_to_le16(v_idx);
		vqv[i].itr_idx = cpu_to_le32(itr_idx);
	}

	return idpf_send_chunked_msg(qs->adapter, &params);
}

/**
 * idpf_send_map_unmap_queue_vector_msg - send virtchnl map or unmap queue
 *					  vector message
 * @adapter: adapter pointer used to send virtchnl message
 * @rsrc: pointer to queue and vector resources
 * @vport_id: vport identifier used while preparing the virtchnl message
 * @map: true for map and false for unmap
 *
 * Return: 0 on success, -errno on failure.
 */
int idpf_send_map_unmap_queue_vector_msg(struct idpf_adapter *adapter,
					 struct idpf_q_vec_rsrc *rsrc,
					 u32 vport_id, bool map)
{
	struct idpf_queue_set *qs __free(kfree) = NULL;
	u32 num_q = rsrc->num_txq + rsrc->num_rxq;
	u32 k = 0;

	qs = idpf_alloc_queue_set(adapter, rsrc, vport_id, num_q);
	if (!qs)
		return -ENOMEM;

	for (u32 i = 0; i < rsrc->num_txq_grp; i++) {
		const struct idpf_txq_group *tx_qgrp = &rsrc->txq_grps[i];

		for (u32 j = 0; j < tx_qgrp->num_txq; j++) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_TX;
			qs->qs[k++].txq = tx_qgrp->txqs[j];
		}
	}

	if (k != rsrc->num_txq)
		return -EINVAL;

	for (u32 i = 0; i < rsrc->num_rxq_grp; i++) {
		const struct idpf_rxq_group *rx_qgrp = &rsrc->rxq_grps[i];
		u32 num_rxq;

		if (idpf_is_queue_model_split(rsrc->rxq_model))
			num_rxq = rx_qgrp->splitq.num_rxq_sets;
		else
			num_rxq = rx_qgrp->singleq.num_rxq;

		for (u32 j = 0; j < num_rxq; j++) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_RX;

			if (idpf_is_queue_model_split(rsrc->rxq_model))
				qs->qs[k++].rxq =
					&rx_qgrp->splitq.rxq_sets[j]->rxq;
			else
				qs->qs[k++].rxq = rx_qgrp->singleq.rxqs[j];
		}
	}

	if (k != num_q)
		return -EINVAL;

	return idpf_send_map_unmap_queue_set_vector_msg(qs, map);
}

/**
 * idpf_send_enable_queue_set_msg - send enable queues virtchnl message for
 *				    selected queues
 * @qs: set of the queues
 *
 * Send enable queues virtchnl message for queues contained in the @qs array.
 *
 * Return: 0 on success, -errno on failure.
 */
int idpf_send_enable_queue_set_msg(const struct idpf_queue_set *qs)
{
	return idpf_send_ena_dis_queue_set_msg(qs, true);
}

/**
 * idpf_send_disable_queue_set_msg - send disable queues virtchnl message for
 *				     selected queues
 * @qs: set of the queues
 *
 * Return: 0 on success, -errno on failure.
 */
int idpf_send_disable_queue_set_msg(const struct idpf_queue_set *qs)
{
	int err;

	err = idpf_send_ena_dis_queue_set_msg(qs, false);
	if (err)
		return err;

	return idpf_wait_for_marker_event_set(qs);
}

/**
 * idpf_send_config_queue_set_msg - send virtchnl config queues message for
 *				    selected queues
 * @qs: set of the queues
 *
 * Send config queues virtchnl message for queues contained in the @qs array.
 * The @qs array can contain both Rx or Tx queues.
 *
 * Return: 0 on success, -errno on failure.
 */
int idpf_send_config_queue_set_msg(const struct idpf_queue_set *qs)
{
	int err;

	err = idpf_send_config_tx_queue_set_msg(qs);
	if (err)
		return err;

	return idpf_send_config_rx_queue_set_msg(qs);
}

/**
 * idpf_send_enable_queues_msg - send enable queues virtchnl message
 * @vport: Virtual port private data structure
 *
 * Will send enable queues virtchnl message.  Returns 0 on success, negative on
 * failure.
 */
int idpf_send_enable_queues_msg(struct idpf_vport *vport)
{
	return idpf_send_ena_dis_queues_msg(vport->adapter,
					    &vport->dflt_qv_rsrc,
					    vport->vport_id, true);
}

/**
 * idpf_send_disable_queues_msg - send disable queues virtchnl message
 * @vport: Virtual port private data structure
 *
 * Will send disable queues virtchnl message.  Returns 0 on success, negative
 * on failure.
 */
int idpf_send_disable_queues_msg(struct idpf_vport *vport)
{
	int err;

	err = idpf_send_ena_dis_queues_msg(vport->adapter,
					   &vport->dflt_qv_rsrc,
					   vport->vport_id, false);
	if (err)
		return err;

	return idpf_wait_for_marker_event(vport);
}

/**
 * idpf_convert_reg_to_queue_chunks - Copy queue chunk information to the right
 * structure
 * @dchunks: Destination chunks to store data to
 * @schunks: Source chunks to copy data from
 * @num_chunks: number of chunks to copy
 */
static void idpf_convert_reg_to_queue_chunks(struct virtchnl2_queue_chunk *dchunks,
					     struct idpf_queue_id_reg_chunk *schunks,
					     u16 num_chunks)
{
	u16 i;

	for (i = 0; i < num_chunks; i++) {
		dchunks[i].type = cpu_to_le32(schunks[i].type);
		dchunks[i].start_queue_id = cpu_to_le32(schunks[i].start_queue_id);
		dchunks[i].num_queues = cpu_to_le32(schunks[i].num_queues);
	}
}

/**
 * idpf_send_delete_queues_msg - send delete queues virtchnl message
 * @adapter: adapter pointer used to send virtchnl message
 * @chunks: queue ids received over mailbox
 * @vport_id: vport identifier used while preparing the virtchnl message
 *
 * Return: 0 on success, negative on failure.
 */
int idpf_send_delete_queues_msg(struct idpf_adapter *adapter,
				struct idpf_queue_id_reg_info *chunks,
				u32 vport_id)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= VIRTCHNL2_OP_DEL_QUEUES,
	};
	struct virtchnl2_del_ena_dis_queues *eq;
	ssize_t buf_size;
	u16 num_chunks;
	int err;

	num_chunks = chunks->num_chunks;
	buf_size = struct_size(eq, chunks.chunks, num_chunks);

	eq = kzalloc(buf_size, GFP_KERNEL);
	if (!eq)
		return -ENOMEM;

	eq->vport_id = cpu_to_le32(vport_id);
	eq->chunks.num_chunks = cpu_to_le16(num_chunks);

	idpf_convert_reg_to_queue_chunks(eq->chunks.chunks, chunks->queue_chunks,
					 num_chunks);

	err = idpf_send_mb_msg_kfree(adapter, &xn_params, eq, buf_size);
	if (err)
		return err;

	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return 0;
}

/**
 * idpf_send_config_queues_msg - Send config queues virtchnl message
 * @adapter: adapter pointer used to send virtchnl message
 * @rsrc: pointer to queue and vector resources
 * @vport_id: vport identifier used while preparing the virtchnl message
 *
 * Return: 0 on success, negative on failure.
 */
int idpf_send_config_queues_msg(struct idpf_adapter *adapter,
				struct idpf_q_vec_rsrc *rsrc,
				u32 vport_id)
{
	int err;

	err = idpf_send_config_tx_queues_msg(adapter, rsrc, vport_id);
	if (err)
		return err;

	return idpf_send_config_rx_queues_msg(adapter, rsrc, vport_id);
}

/**
 * idpf_send_add_queues_msg - Send virtchnl add queues message
 * @adapter: adapter pointer used to send virtchnl message
 * @vport_config: vport persistent structure to store the queue chunk info
 * @rsrc: pointer to queue and vector resources
 * @vport_id: vport identifier used while preparing the virtchnl message
 *
 * Return: 0 on success, negative on failure.
 */
int idpf_send_add_queues_msg(struct idpf_adapter *adapter,
			     struct idpf_vport_config *vport_config,
			     struct idpf_q_vec_rsrc *rsrc,
			     u32 vport_id)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= VIRTCHNL2_OP_ADD_QUEUES,
	};
	struct virtchnl2_add_queues *vc_msg;
	struct virtchnl2_add_queues aq = {};
	size_t size;
	int err;

	aq.vport_id = cpu_to_le32(vport_id);
	aq.num_tx_q = cpu_to_le16(rsrc->num_txq);
	aq.num_tx_complq = cpu_to_le16(rsrc->num_complq);
	aq.num_rx_q = cpu_to_le16(rsrc->num_rxq);
	aq.num_rx_bufq = cpu_to_le16(rsrc->num_bufq);

	err = idpf_send_mb_msg_stack(adapter, &xn_params, &aq);
	if (err)
		return err;

	vc_msg = xn_params.recv_mem.iov_base;
	if (xn_params.recv_mem.iov_len < sizeof(*vc_msg)) {
		err = -EIO;
		goto free_rx_buf;
	}

	/* compare vc_msg num queues with vport num queues */
	if (le16_to_cpu(vc_msg->num_tx_q) != rsrc->num_txq ||
	    le16_to_cpu(vc_msg->num_rx_q) != rsrc->num_rxq ||
	    le16_to_cpu(vc_msg->num_tx_complq) != rsrc->num_complq ||
	    le16_to_cpu(vc_msg->num_rx_bufq) != rsrc->num_bufq) {
		err = -EINVAL;
		goto free_rx_buf;
	}

	size = struct_size(vc_msg, chunks.chunks,
			   le16_to_cpu(vc_msg->chunks.num_chunks));
	if (xn_params.recv_mem.iov_len < size) {
		err = -EIO;
		goto free_rx_buf;
	}

	err = idpf_vport_init_queue_reg_chunks(vport_config, &vc_msg->chunks);

free_rx_buf:
	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return err;
}

/**
 * idpf_send_alloc_vectors_msg - Send virtchnl alloc vectors message
 * @adapter: Driver specific private structure
 * @num_vectors: number of vectors to be allocated
 *
 * Returns 0 on success, negative on failure.
 */
int idpf_send_alloc_vectors_msg(struct idpf_adapter *adapter, u16 num_vectors)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= VIRTCHNL2_OP_ALLOC_VECTORS,
	};
	struct virtchnl2_alloc_vectors *rcvd_vec;
	struct virtchnl2_alloc_vectors ac = {};
	u16 num_vchunks;
	int size, err;

	ac.num_vectors = cpu_to_le16(num_vectors);

	err = idpf_send_mb_msg_stack(adapter, &xn_params, &ac);
	if (err)
		return err;

	rcvd_vec = xn_params.recv_mem.iov_base;
	if (xn_params.recv_mem.iov_len < sizeof(*rcvd_vec)) {
		err = -EIO;
		goto free_rx_buf;
	}

	num_vchunks = le16_to_cpu(rcvd_vec->vchunks.num_vchunks);
	size = struct_size(rcvd_vec, vchunks.vchunks, num_vchunks);
	if (xn_params.recv_mem.iov_len < size) {
		err = -EIO;
		goto free_rx_buf;
	}

	kfree(adapter->req_vec_chunks);
	adapter->req_vec_chunks = kmemdup(rcvd_vec, size, GFP_KERNEL);
	if (!adapter->req_vec_chunks) {
		err = -ENOMEM;
		goto free_rx_buf;
	}

	if (le16_to_cpu(adapter->req_vec_chunks->num_vectors) < num_vectors) {
		kfree(adapter->req_vec_chunks);
		adapter->req_vec_chunks = NULL;
		err = -EINVAL;
	}

free_rx_buf:
	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return err;
}

/**
 * idpf_send_dealloc_vectors_msg - Send virtchnl de allocate vectors message
 * @adapter: Driver specific private structure
 *
 * Returns 0 on success, negative on failure.
 */
int idpf_send_dealloc_vectors_msg(struct idpf_adapter *adapter)
{
	struct virtchnl2_alloc_vectors *ac = adapter->req_vec_chunks;
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= VIRTCHNL2_OP_DEALLOC_VECTORS,
	};
	struct virtchnl2_vector_chunks *vcs;
	int buf_size, err;

	buf_size = struct_size(&ac->vchunks, vchunks,
			       le16_to_cpu(ac->vchunks.num_vchunks));
	vcs = kmemdup(&ac->vchunks, buf_size, GFP_KERNEL);
	if (!vcs)
		return -ENOMEM;

	err = idpf_send_mb_msg_kfree(adapter, &xn_params, vcs, buf_size);
	if (err)
		return err;

	kfree(adapter->req_vec_chunks);
	adapter->req_vec_chunks = NULL;

	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return 0;
}

/**
 * idpf_get_max_vfs - Get max number of vfs supported
 * @adapter: Driver specific private structure
 *
 * Returns max number of VFs
 */
static int idpf_get_max_vfs(struct idpf_adapter *adapter)
{
	return le16_to_cpu(adapter->caps.max_sriov_vfs);
}

/**
 * idpf_send_set_sriov_vfs_msg - Send virtchnl set sriov vfs message
 * @adapter: Driver specific private structure
 * @num_vfs: number of virtual functions to be created
 *
 * Returns 0 on success, negative on failure.
 */
int idpf_send_set_sriov_vfs_msg(struct idpf_adapter *adapter, u16 num_vfs)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= VIRTCHNL2_OP_SET_SRIOV_VFS,
	};
	struct virtchnl2_sriov_vfs_info svi = {};
	int err;

	svi.num_vfs = cpu_to_le16(num_vfs);

	err = idpf_send_mb_msg_stack(adapter, &xn_params, &svi);
	if (err)
		return err;

	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return 0;
}

/**
 * idpf_send_get_stats_msg - Send virtchnl get statistics message
 * @np: netdev private structure
 * @port_stats: structure to store the vport statistics
 *
 * Return: 0 on success, negative on failure.
 */
int idpf_send_get_stats_msg(struct idpf_netdev_priv *np,
			    struct idpf_port_stats *port_stats)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= VIRTCHNL2_OP_GET_STATS,
	};
	struct rtnl_link_stats64 *netstats = &np->netstats;
	struct virtchnl2_vport_stats *stats_recv;
	struct virtchnl2_vport_stats stats_msg = {};
	int err;


	/* Don't send get_stats message if the link is down */
	if (!test_bit(IDPF_VPORT_UP, np->state))
		return 0;

	stats_msg.vport_id = cpu_to_le32(np->vport_id);

	err = idpf_send_mb_msg_stack(np->adapter, &xn_params, &stats_msg);
	if (err)
		return err;

	if (xn_params.recv_mem.iov_len < sizeof(*stats_recv)) {
		err = -EIO;
		goto free_rx_buf;
	}

	stats_recv = xn_params.recv_mem.iov_base;

	spin_lock_bh(&np->stats_lock);

	netstats->rx_packets = le64_to_cpu(stats_recv->rx_unicast) +
			       le64_to_cpu(stats_recv->rx_multicast) +
			       le64_to_cpu(stats_recv->rx_broadcast);
	netstats->tx_packets = le64_to_cpu(stats_recv->tx_unicast) +
			       le64_to_cpu(stats_recv->tx_multicast) +
			       le64_to_cpu(stats_recv->tx_broadcast);
	netstats->rx_bytes = le64_to_cpu(stats_recv->rx_bytes);
	netstats->tx_bytes = le64_to_cpu(stats_recv->tx_bytes);
	netstats->rx_errors = le64_to_cpu(stats_recv->rx_errors);
	netstats->tx_errors = le64_to_cpu(stats_recv->tx_errors);
	netstats->rx_dropped = le64_to_cpu(stats_recv->rx_discards);
	netstats->tx_dropped = le64_to_cpu(stats_recv->tx_discards);

	port_stats->vport_stats = *stats_recv;

	spin_unlock_bh(&np->stats_lock);

free_rx_buf:
	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return err;
}

/**
 * idpf_send_set_rss_lut_msg - Send virtchnl set RSS lut message
 * @adapter: adapter pointer used to send virtchnl message
 * @rss_data: pointer to RSS key and lut info
 * @vport_id: vport identifier used while preparing the virtchnl message
 *
 * When rxhash is disabled, RSS LUT will be configured with zeros. If rxhash
 * is enabled, the LUT values stored in driver's soft copy will be used to setup
 * the HW.
 *
 * Return: 0 on success, negative on failure.
 */
int idpf_send_set_rss_lut_msg(struct idpf_adapter *adapter,
			      struct idpf_rss_data *rss_data, u32 vport_id)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= VIRTCHNL2_OP_SET_RSS_LUT,
	};
	struct virtchnl2_rss_lut *rl;
	struct idpf_vport *vport;
	int buf_size, i, err;
	bool rxhash_ena;

	vport = idpf_vid_to_vport(adapter, vport_id);
	if (!vport)
		return -EINVAL;

	rxhash_ena = idpf_is_feature_ena(vport, NETIF_F_RXHASH);

	buf_size = struct_size(rl, lut, rss_data->rss_lut_size);
	rl = kzalloc(buf_size, GFP_KERNEL);
	if (!rl)
		return -ENOMEM;

	rl->vport_id = cpu_to_le32(vport_id);
	rl->lut_entries = cpu_to_le16(rss_data->rss_lut_size);
	for (i = 0; i < rss_data->rss_lut_size; i++)
		rl->lut[i] = rxhash_ena ? cpu_to_le32(rss_data->rss_lut[i]) : 0;

	err = idpf_send_mb_msg_kfree(adapter, &xn_params, rl, buf_size);
	if (err)
		return err;

	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return err;
}

/**
 * idpf_send_set_rss_key_msg - Send virtchnl set RSS key message
 * @adapter: adapter pointer used to send virtchnl message
 * @rss_data: pointer to RSS key and lut info
 * @vport_id: vport identifier used while preparing the virtchnl message
 *
 * Return: 0 on success, negative on failure
 */
int idpf_send_set_rss_key_msg(struct idpf_adapter *adapter,
			      struct idpf_rss_data *rss_data, u32 vport_id)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= VIRTCHNL2_OP_SET_RSS_KEY,
	};
	struct virtchnl2_rss_key *rk;
	int i, buf_size, err;

	buf_size = struct_size(rk, key_flex, rss_data->rss_key_size);
	rk = kzalloc(buf_size, GFP_KERNEL);
	if (!rk)
		return -ENOMEM;

	rk->vport_id = cpu_to_le32(vport_id);
	rk->key_len = cpu_to_le16(rss_data->rss_key_size);
	for (i = 0; i < rss_data->rss_key_size; i++)
		rk->key_flex[i] = rss_data->rss_key[i];

	err = idpf_send_mb_msg_kfree(adapter, &xn_params, rk, buf_size);
	if (err)
		return err;

	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return err;
}

/**
 * idpf_fill_ptype_lookup - Fill L3 specific fields in ptype lookup table
 * @ptype: ptype lookup table
 * @pstate: state machine for ptype lookup table
 * @ipv4: ipv4 or ipv6
 * @frag: fragmentation allowed
 *
 */
static void idpf_fill_ptype_lookup(struct libeth_rx_pt *ptype,
				   struct idpf_ptype_state *pstate,
				   bool ipv4, bool frag)
{
	if (!pstate->outer_ip || !pstate->outer_frag) {
		pstate->outer_ip = true;

		if (ipv4)
			ptype->outer_ip = LIBETH_RX_PT_OUTER_IPV4;
		else
			ptype->outer_ip = LIBETH_RX_PT_OUTER_IPV6;

		if (frag) {
			ptype->outer_frag = LIBETH_RX_PT_FRAG;
			pstate->outer_frag = true;
		}
	} else {
		ptype->tunnel_type = LIBETH_RX_PT_TUNNEL_IP_IP;
		pstate->tunnel_state = IDPF_PTYPE_TUNNEL_IP;

		if (ipv4)
			ptype->tunnel_end_prot = LIBETH_RX_PT_TUNNEL_END_IPV4;
		else
			ptype->tunnel_end_prot = LIBETH_RX_PT_TUNNEL_END_IPV6;

		if (frag)
			ptype->tunnel_end_frag = LIBETH_RX_PT_FRAG;
	}
}

static void idpf_finalize_ptype_lookup(struct libeth_rx_pt *ptype)
{
	if (ptype->payload_layer == LIBETH_RX_PT_PAYLOAD_L2 &&
	    ptype->inner_prot)
		ptype->payload_layer = LIBETH_RX_PT_PAYLOAD_L4;
	else if (ptype->payload_layer == LIBETH_RX_PT_PAYLOAD_L2 &&
		 ptype->outer_ip)
		ptype->payload_layer = LIBETH_RX_PT_PAYLOAD_L3;
	else if (ptype->outer_ip == LIBETH_RX_PT_OUTER_L2)
		ptype->payload_layer = LIBETH_RX_PT_PAYLOAD_L2;
	else
		ptype->payload_layer = LIBETH_RX_PT_PAYLOAD_NONE;

	libeth_rx_pt_gen_hash_type(ptype);
}

/**
 * idpf_parse_protocol_ids - parse protocol IDs for a given packet type
 * @ptype: packet type to parse
 * @rx_pt: store the parsed packet type info into
 */
static void idpf_parse_protocol_ids(struct virtchnl2_ptype *ptype,
				    struct libeth_rx_pt *rx_pt)
{
	struct idpf_ptype_state pstate = {};

	for (u32 j = 0; j < ptype->proto_id_count; j++) {
		u16 id = le16_to_cpu(ptype->proto_id[j]);

		switch (id) {
		case VIRTCHNL2_PROTO_HDR_GRE:
			if (pstate.tunnel_state == IDPF_PTYPE_TUNNEL_IP) {
				rx_pt->tunnel_type =
					LIBETH_RX_PT_TUNNEL_IP_GRENAT;
				pstate.tunnel_state |=
					IDPF_PTYPE_TUNNEL_IP_GRENAT;
			}
			break;
		case VIRTCHNL2_PROTO_HDR_MAC:
			rx_pt->outer_ip = LIBETH_RX_PT_OUTER_L2;
			if (pstate.tunnel_state == IDPF_TUN_IP_GRE) {
				rx_pt->tunnel_type =
					LIBETH_RX_PT_TUNNEL_IP_GRENAT_MAC;
				pstate.tunnel_state |=
					IDPF_PTYPE_TUNNEL_IP_GRENAT_MAC;
			}
			break;
		case VIRTCHNL2_PROTO_HDR_IPV4:
			idpf_fill_ptype_lookup(rx_pt, &pstate, true, false);
			break;
		case VIRTCHNL2_PROTO_HDR_IPV6:
			idpf_fill_ptype_lookup(rx_pt, &pstate, false, false);
			break;
		case VIRTCHNL2_PROTO_HDR_IPV4_FRAG:
			idpf_fill_ptype_lookup(rx_pt, &pstate, true, true);
			break;
		case VIRTCHNL2_PROTO_HDR_IPV6_FRAG:
			idpf_fill_ptype_lookup(rx_pt, &pstate, false, true);
			break;
		case VIRTCHNL2_PROTO_HDR_UDP:
			rx_pt->inner_prot = LIBETH_RX_PT_INNER_UDP;
			break;
		case VIRTCHNL2_PROTO_HDR_TCP:
			rx_pt->inner_prot = LIBETH_RX_PT_INNER_TCP;
			break;
		case VIRTCHNL2_PROTO_HDR_SCTP:
			rx_pt->inner_prot = LIBETH_RX_PT_INNER_SCTP;
			break;
		case VIRTCHNL2_PROTO_HDR_ICMP:
			rx_pt->inner_prot = LIBETH_RX_PT_INNER_ICMP;
			break;
		case VIRTCHNL2_PROTO_HDR_PAY:
			rx_pt->payload_layer = LIBETH_RX_PT_PAYLOAD_L2;
			break;
		case VIRTCHNL2_PROTO_HDR_ICMPV6:
		case VIRTCHNL2_PROTO_HDR_IPV6_EH:
		case VIRTCHNL2_PROTO_HDR_PRE_MAC:
		case VIRTCHNL2_PROTO_HDR_POST_MAC:
		case VIRTCHNL2_PROTO_HDR_ETHERTYPE:
		case VIRTCHNL2_PROTO_HDR_SVLAN:
		case VIRTCHNL2_PROTO_HDR_CVLAN:
		case VIRTCHNL2_PROTO_HDR_MPLS:
		case VIRTCHNL2_PROTO_HDR_MMPLS:
		case VIRTCHNL2_PROTO_HDR_PTP:
		case VIRTCHNL2_PROTO_HDR_CTRL:
		case VIRTCHNL2_PROTO_HDR_LLDP:
		case VIRTCHNL2_PROTO_HDR_ARP:
		case VIRTCHNL2_PROTO_HDR_ECP:
		case VIRTCHNL2_PROTO_HDR_EAPOL:
		case VIRTCHNL2_PROTO_HDR_PPPOD:
		case VIRTCHNL2_PROTO_HDR_PPPOE:
		case VIRTCHNL2_PROTO_HDR_IGMP:
		case VIRTCHNL2_PROTO_HDR_AH:
		case VIRTCHNL2_PROTO_HDR_ESP:
		case VIRTCHNL2_PROTO_HDR_IKE:
		case VIRTCHNL2_PROTO_HDR_NATT_KEEP:
		case VIRTCHNL2_PROTO_HDR_L2TPV2:
		case VIRTCHNL2_PROTO_HDR_L2TPV2_CONTROL:
		case VIRTCHNL2_PROTO_HDR_L2TPV3:
		case VIRTCHNL2_PROTO_HDR_GTP:
		case VIRTCHNL2_PROTO_HDR_GTP_EH:
		case VIRTCHNL2_PROTO_HDR_GTPCV2:
		case VIRTCHNL2_PROTO_HDR_GTPC_TEID:
		case VIRTCHNL2_PROTO_HDR_GTPU:
		case VIRTCHNL2_PROTO_HDR_GTPU_UL:
		case VIRTCHNL2_PROTO_HDR_GTPU_DL:
		case VIRTCHNL2_PROTO_HDR_ECPRI:
		case VIRTCHNL2_PROTO_HDR_VRRP:
		case VIRTCHNL2_PROTO_HDR_OSPF:
		case VIRTCHNL2_PROTO_HDR_TUN:
		case VIRTCHNL2_PROTO_HDR_NVGRE:
		case VIRTCHNL2_PROTO_HDR_VXLAN:
		case VIRTCHNL2_PROTO_HDR_VXLAN_GPE:
		case VIRTCHNL2_PROTO_HDR_GENEVE:
		case VIRTCHNL2_PROTO_HDR_NSH:
		case VIRTCHNL2_PROTO_HDR_QUIC:
		case VIRTCHNL2_PROTO_HDR_PFCP:
		case VIRTCHNL2_PROTO_HDR_PFCP_NODE:
		case VIRTCHNL2_PROTO_HDR_PFCP_SESSION:
		case VIRTCHNL2_PROTO_HDR_RTP:
		case VIRTCHNL2_PROTO_HDR_NO_PROTO:
			break;
		default:
			break;
		}
	}
}

/**
 * idpf_send_get_rx_ptype_msg - Send virtchnl for ptype info
 * @adapter: driver specific private structure
 *
 * Return: 0 on success, negative on failure.
 */
static int idpf_send_get_rx_ptype_msg(struct idpf_adapter *adapter)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= VIRTCHNL2_OP_GET_PTYPE_INFO,
	};
	struct virtchnl2_get_ptype_info *get_ptype_info;
	struct virtchnl2_get_ptype_info *ptype_info;
	int err = 0, max_ptype = IDPF_RX_MAX_PTYPE;
	int buf_size = sizeof(*get_ptype_info);
	struct libeth_rx_pt *singleq_pt_lkup;
	struct libeth_rx_pt *splitq_pt_lkup;
	int ptypes_recvd = 0, ptype_offset;
	u16 next_ptype_id = 0;

	singleq_pt_lkup = kzalloc_objs(*singleq_pt_lkup, IDPF_RX_MAX_BASE_PTYPE);
	if (!singleq_pt_lkup)
		return -ENOMEM;

	splitq_pt_lkup = kzalloc_objs(*splitq_pt_lkup, max_ptype);
	if (!splitq_pt_lkup) {
		err = -ENOMEM;
		goto free_singleq;
	}

	while (next_ptype_id < max_ptype) {
		u16 num_ptypes;

		get_ptype_info = kzalloc(buf_size, GFP_KERNEL);
		if (!get_ptype_info) {
			err = -ENOMEM;
			goto free_splitq;
		}

		get_ptype_info->start_ptype_id = cpu_to_le16(next_ptype_id);

		if ((next_ptype_id + IDPF_RX_MAX_PTYPES_PER_BUF) > max_ptype)
			num_ptypes = max_ptype - next_ptype_id;
		else
			num_ptypes = IDPF_RX_MAX_PTYPES_PER_BUF;

		get_ptype_info->num_ptypes = cpu_to_le16(num_ptypes);
		err = idpf_send_mb_msg_kfree(adapter, &xn_params,
					     get_ptype_info, buf_size);
		if (err)
			goto free_splitq;

		ptype_info = xn_params.recv_mem.iov_base;
		if (xn_params.recv_mem.iov_len < sizeof(*ptype_info)) {
			err = -EIO;
			goto free_rx_buf;
		}
		ptypes_recvd += le16_to_cpu(ptype_info->num_ptypes);
		if (ptypes_recvd > max_ptype) {
			err = -EINVAL;
			goto free_rx_buf;
		}

		next_ptype_id = next_ptype_id + num_ptypes;
		ptype_offset = IDPF_RX_PTYPE_HDR_SZ;

		for (u16 i = 0; i < le16_to_cpu(ptype_info->num_ptypes); i++) {
			struct libeth_rx_pt rx_pt = {};
			struct virtchnl2_ptype *ptype;
			u16 pt_10, pt_8;

			ptype = (struct virtchnl2_ptype *)
					((u8 *)ptype_info + ptype_offset);
			if (xn_params.recv_mem.iov_len <
			    ptype_offset + sizeof(struct virtchnl2_ptype)) {
				err = -EINVAL;
				goto free_rx_buf;
			}

			pt_10 = le16_to_cpu(ptype->ptype_id_10);
			pt_8 = ptype->ptype_id_8;

			ptype_offset += IDPF_GET_PTYPE_SIZE(ptype);
			if (xn_params.recv_mem.iov_len < ptype_offset) {
				err = -EINVAL;
				goto free_rx_buf;
			}

			/* 0xFFFF indicates end of ptypes */
			if (pt_10 == IDPF_INVALID_PTYPE_ID)
				goto out;
			if (pt_10 >= max_ptype) {
				err = -EINVAL;
				goto free_rx_buf;
			}

			idpf_parse_protocol_ids(ptype, &rx_pt);
			idpf_finalize_ptype_lookup(&rx_pt);

			/* For a given protocol ID stack, the ptype value might
			 * vary between ptype_id_10 and ptype_id_8. So store
			 * them separately for splitq and singleq. Also skip
			 * the repeated ptypes in case of singleq.
			 */
			splitq_pt_lkup[pt_10] = rx_pt;
			if (!singleq_pt_lkup[pt_8].outer_ip)
				singleq_pt_lkup[pt_8] = rx_pt;
		}

		libie_ctlq_release_rx_buf(&xn_params.recv_mem);
		xn_params.recv_mem = (struct kvec) {};
	}

out:
	adapter->splitq_pt_lkup = splitq_pt_lkup;
	adapter->singleq_pt_lkup = singleq_pt_lkup;
	splitq_pt_lkup = NULL;
	singleq_pt_lkup = NULL;
free_rx_buf:
	libie_ctlq_release_rx_buf(&xn_params.recv_mem);
free_splitq:
	kfree(splitq_pt_lkup);
free_singleq:
	kfree(singleq_pt_lkup);

	return err;
}

/**
 * idpf_rel_rx_pt_lkup - release RX ptype lookup table
 * @adapter: adapter pointer to get the lookup table
 */
static void idpf_rel_rx_pt_lkup(struct idpf_adapter *adapter)
{
	kfree(adapter->splitq_pt_lkup);
	adapter->splitq_pt_lkup = NULL;

	kfree(adapter->singleq_pt_lkup);
	adapter->singleq_pt_lkup = NULL;
}

/**
 * idpf_send_ena_dis_loopback_msg - Send virtchnl enable/disable loopback
 *				    message
 * @adapter: adapter pointer used to send virtchnl message
 * @vport_id: vport identifier used while preparing the virtchnl message
 * @loopback_ena: flag to enable or disable loopback
 *
 * Return: 0 on success, negative on failure.
 */
int idpf_send_ena_dis_loopback_msg(struct idpf_adapter *adapter, u32 vport_id,
				   bool loopback_ena)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= VIRTCHNL2_OP_LOOPBACK,
	};
	struct virtchnl2_loopback loopback;
	int err;

	loopback.vport_id = cpu_to_le32(vport_id);
	loopback.enable = loopback_ena;

	err = idpf_send_mb_msg_stack(adapter, &xn_params, &loopback);
	if (err)
		return err;

	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return 0;
}

/**
 * idpf_init_dflt_mbx - Setup default mailbox parameters and make request
 * @adapter: adapter info struct
 *
 * Returns 0 on success, negative otherwise
 */
int idpf_init_dflt_mbx(struct idpf_adapter *adapter)
{
	struct libie_ctlq_ctx *ctx = &adapter->ctlq_ctx;
	struct libie_ctlq_create_info ctlq_info[] = {
		{
			.type = LIBIE_CTLQ_TYPE_TX,
			.id = LIBIE_CTLQ_MBX_ID,
			.len = IDPF_DFLT_MBX_Q_LEN,
		},
		{
			.type = LIBIE_CTLQ_TYPE_RX,
			.id = LIBIE_CTLQ_MBX_ID,
			.len = IDPF_DFLT_MBX_Q_LEN,
		}
	};
	struct libie_ctlq_xn_init_params params = {
		.num_qs = IDPF_NUM_DFLT_MBX_Q,
		.cctlq_info = ctlq_info,
		.ctx = ctx,
	};
	int err;

	adapter->dev_ops.reg_ops.ctlq_reg_init(&ctx->mmio_info,
					       params.cctlq_info);

	err = libie_ctlq_xn_init(&params);
	if (err)
		return err;

	adapter->asq = libie_find_ctlq(ctx, LIBIE_CTLQ_TYPE_TX,
				       LIBIE_CTLQ_MBX_ID);
	adapter->arq = libie_find_ctlq(ctx, LIBIE_CTLQ_TYPE_RX,
				       LIBIE_CTLQ_MBX_ID);
	if (!adapter->asq || !adapter->arq) {
		adapter->asq = NULL;
		adapter->arq = NULL;
		libie_ctlq_xn_deinit(params.xnm, ctx);
		return -ENOENT;
	}

	adapter->xnm = params.xnm;
	adapter->state = __IDPF_VER_CHECK;

	queue_delayed_work(adapter->mbx_wq, &adapter->mbx_task, 0);

	return 0;
}

/**
 * idpf_deinit_dflt_mbx - Free up ctlqs setup
 * @adapter: Driver specific private data structure
 */
void idpf_deinit_dflt_mbx(struct idpf_adapter *adapter)
{
	idpf_mb_intr_rel_irq(adapter);
	cancel_delayed_work_sync(&adapter->mbx_task);

	if (adapter->xnm) {
		libie_ctlq_xn_shutdown(adapter->xnm);
		idpf_mb_clean(adapter->asq, true);
		libie_ctlq_xn_deinit(adapter->xnm, &adapter->ctlq_ctx);
	}

	adapter->arq = NULL;
	adapter->asq = NULL;
	adapter->xnm = NULL;
}

/**
 * idpf_vport_params_buf_rel - Release memory for MailBox resources
 * @adapter: Driver specific private data structure
 *
 * Will release memory to hold the vport parameters received on MailBox
 */
static void idpf_vport_params_buf_rel(struct idpf_adapter *adapter)
{
	kfree(adapter->vport_params_recvd);
	adapter->vport_params_recvd = NULL;
	kfree(adapter->vport_ids);
	adapter->vport_ids = NULL;
}

/**
 * idpf_vport_params_buf_alloc - Allocate memory for MailBox resources
 * @adapter: Driver specific private data structure
 *
 * Will alloc memory to hold the vport parameters received on MailBox
 */
static int idpf_vport_params_buf_alloc(struct idpf_adapter *adapter)
{
	u16 num_max_vports = idpf_get_max_vports(adapter);

	adapter->vport_params_recvd = kzalloc_objs(*adapter->vport_params_recvd,
						   num_max_vports);
	if (!adapter->vport_params_recvd)
		return -ENOMEM;

	adapter->vport_ids = kcalloc(num_max_vports, sizeof(u32), GFP_KERNEL);
	if (!adapter->vport_ids)
		goto err_mem;

	if (adapter->vport_config)
		return 0;

	adapter->vport_config = kzalloc_objs(*adapter->vport_config,
					     num_max_vports);
	if (!adapter->vport_config)
		goto err_mem;

	return 0;

err_mem:
	idpf_vport_params_buf_rel(adapter);

	return -ENOMEM;
}

/**
 * idpf_vc_core_init - Initialize state machine and get driver specific
 * resources
 * @adapter: Driver specific private structure
 *
 * This function will initialize the state machine and request all necessary
 * resources required by the device driver. Once the state machine is
 * initialized, allocate memory to store vport specific information and also
 * requests required interrupts.
 *
 * Returns 0 on success, -EAGAIN function will get called again,
 * otherwise negative on failure.
 */
int idpf_vc_core_init(struct idpf_adapter *adapter)
{
	int task_delay = 30;
	u16 num_max_vports;
	int err = 0;

	while (adapter->state != __IDPF_INIT_SW) {
		switch (adapter->state) {
		case __IDPF_VER_CHECK:
			err = idpf_send_ver_msg(adapter);
			switch (err) {
			case 0:
				/* success, move state machine forward */
				adapter->state = __IDPF_GET_CAPS;
				fallthrough;
			case -EAGAIN:
				goto restart;
			default:
				/* Something bad happened, try again but only a
				 * few times.
				 */
				goto init_failed;
			}
		case __IDPF_GET_CAPS:
			err = idpf_send_get_caps_msg(adapter);
			if (err)
				goto init_failed;
			adapter->state = __IDPF_INIT_SW;
			break;
		default:
			dev_err(&adapter->pdev->dev, "Device is in bad state: %d\n",
				adapter->state);
			err = -EINVAL;
			goto init_failed;
		}
		break;
restart:
		/* Give enough time before proceeding further with
		 * state machine
		 */
		msleep(task_delay);
	}

	if (idpf_is_cap_ena(adapter, IDPF_OTHER_CAPS, VIRTCHNL2_CAP_LAN_MEMORY_REGIONS)) {
		err = idpf_cfg_lan_memory_regions(adapter);
		if (err) {
			dev_err(&adapter->pdev->dev, "Failed to configure LAN memory regions: %d\n",
				err);
			return -EINVAL;
		}
	} else {
		/* Fallback to mapping the remaining regions of the entire BAR */
		err = idpf_map_remaining_mmio_regs(adapter);
		if (err) {
			dev_err(&adapter->pdev->dev, "Failed to configure BAR0 region(s): %d\n",
				err);
			return err;
		}
	}

	pci_sriov_set_totalvfs(adapter->pdev, idpf_get_max_vfs(adapter));
	num_max_vports = idpf_get_max_vports(adapter);
	adapter->vports = kzalloc_objs(*adapter->vports, num_max_vports);
	if (!adapter->vports) {
		err = -ENOMEM;
		goto decfg_regions;
	}

	if (!adapter->netdevs) {
		adapter->netdevs = kzalloc_objs(struct net_device *,
						num_max_vports);
		if (!adapter->netdevs) {
			err = -ENOMEM;
			goto err_netdev_alloc;
		}
	}

	err = idpf_vport_params_buf_alloc(adapter);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to alloc vport params buffer: %d\n",
			err);
		goto err_netdev_alloc;
	}

	/* Set max_vports only after vports, netdevs and vport_config buffers
	 * are allocated to make sure max_vport bound loops don't end up
	 * crashing, following allocation errors on init.
	 */
	adapter->max_vports = num_max_vports;

	/* Start the mailbox task before requesting vectors. This will ensure
	 * vector information response from mailbox is handled
	 */
	queue_delayed_work(adapter->mbx_wq, &adapter->mbx_task, 0);

	queue_delayed_work(adapter->serv_wq, &adapter->serv_task,
			   msecs_to_jiffies(5 * (adapter->pdev->devfn & 0x07)));

	err = idpf_intr_req(adapter);
	if (err) {
		dev_err(&adapter->pdev->dev, "failed to enable interrupt vectors: %d\n",
			err);
		goto err_intr_req;
	}

	err = idpf_send_get_rx_ptype_msg(adapter);
	if (err) {
		dev_err(&adapter->pdev->dev, "failed to get RX ptypes: %d\n",
			err);
		goto intr_rel;
	}

	err = idpf_ptp_init(adapter);
	if (err)
		pci_err(adapter->pdev, "PTP init failed, err=%pe\n",
			ERR_PTR(err));

	idpf_init_avail_queues(adapter);

	/* Skew the delay for init tasks for each function based on fn number
	 * to prevent every function from making the same call simultaneously.
	 */
	queue_delayed_work(adapter->init_wq, &adapter->init_task,
			   msecs_to_jiffies(5 * (adapter->pdev->devfn & 0x07)));

	set_bit(IDPF_VC_CORE_INIT, adapter->flags);

	return 0;

intr_rel:
	idpf_intr_rel(adapter);
err_intr_req:
	cancel_delayed_work_sync(&adapter->serv_task);
	cancel_delayed_work_sync(&adapter->mbx_task);
	idpf_vport_params_buf_rel(adapter);
err_netdev_alloc:
	kfree(adapter->vports);
	adapter->vports = NULL;
decfg_regions:
	idpf_decfg_lan_memory_regions(adapter);
	return err;

init_failed:
	/* Don't retry if we're trying to go down, just bail. */
	if (test_bit(IDPF_REMOVE_IN_PROG, adapter->flags))
		return err;

	if (++adapter->mb_wait_count > IDPF_MB_MAX_ERR) {
		dev_err(&adapter->pdev->dev, "Failed to establish mailbox communications with hardware\n");

		return -EFAULT;
	}
	/* If it reached here, it is possible that mailbox queue initialization
	 * register writes might not have taken effect. Retry to initialize
	 * the mailbox again
	 */
	adapter->state = __IDPF_VER_CHECK;
	libie_ctlq_xn_shutdown(adapter->xnm);
	set_bit(IDPF_HR_DRV_LOAD, adapter->flags);
	queue_delayed_work(adapter->vc_event_wq, &adapter->vc_event_task,
			   msecs_to_jiffies(task_delay));

	return -EAGAIN;
}

/**
 * idpf_vc_core_deinit - Device deinit routine
 * @adapter: Driver specific private structure
 *
 */
void idpf_vc_core_deinit(struct idpf_adapter *adapter)
{
	bool remove_in_prog;

	if (!test_bit(IDPF_VC_CORE_INIT, adapter->flags))
		return;

	/* Avoid transaction timeouts when called during reset */
	remove_in_prog = test_bit(IDPF_REMOVE_IN_PROG, adapter->flags);
	if (!remove_in_prog)
		libie_ctlq_xn_shutdown(adapter->xnm);

	idpf_ptp_release(adapter);
	idpf_deinit_task(adapter);
	idpf_idc_deinit_core_aux_device(adapter);
	idpf_rel_rx_pt_lkup(adapter);
	idpf_intr_rel(adapter);

	if (remove_in_prog)
		libie_ctlq_xn_shutdown(adapter->xnm);

	cancel_delayed_work_sync(&adapter->serv_task);
	cancel_delayed_work_sync(&adapter->mbx_task);

	idpf_vport_params_buf_rel(adapter);

	kfree(adapter->vports);
	adapter->vports = NULL;

	idpf_decfg_lan_memory_regions(adapter);
	clear_bit(IDPF_VC_CORE_INIT, adapter->flags);
}

/**
 * idpf_vport_alloc_vec_indexes - Get relative vector indexes
 * @vport: virtual port data struct
 * @rsrc: pointer to queue and vector resources
 *
 * This function requests the vector information required for the vport and
 * stores the vector indexes received from the 'global vector distribution'
 * in the vport's queue vectors array.
 *
 * Return: 0 on success, error on failure
 */
int idpf_vport_alloc_vec_indexes(struct idpf_vport *vport,
				 struct idpf_q_vec_rsrc *rsrc)
{
	struct idpf_vector_info vec_info;
	int num_alloc_vecs;
	u32 req;

	vec_info.num_curr_vecs = rsrc->num_q_vectors;
	if (vec_info.num_curr_vecs)
		vec_info.num_curr_vecs += IDPF_RESERVED_VECS;

	/* XDPSQs are all bound to the NOIRQ vector from IDPF_RESERVED_VECS */
	req = max(rsrc->num_txq - vport->num_xdp_txq, rsrc->num_rxq) +
	      IDPF_RESERVED_VECS;
	vec_info.num_req_vecs = req;

	vec_info.default_vport = vport->default_vport;
	vec_info.index = vport->idx;

	num_alloc_vecs = idpf_req_rel_vector_indexes(vport->adapter,
						     rsrc->q_vector_idxs,
						     &vec_info);
	if (num_alloc_vecs <= 0) {
		dev_err(&vport->adapter->pdev->dev, "Vector distribution failed: %d\n",
			num_alloc_vecs);
		return -EINVAL;
	}

	rsrc->num_q_vectors = num_alloc_vecs - IDPF_RESERVED_VECS;

	return 0;
}

/**
 * idpf_vport_init - Initialize virtual port
 * @vport: virtual port to be initialized
 * @max_q: vport max queue info
 *
 * Will initialize vport with the info received through MB earlier
 *
 * Return: 0 on success, negative on failure.
 */
int idpf_vport_init(struct idpf_vport *vport, struct idpf_vport_max_q *max_q)
{
	struct idpf_q_vec_rsrc *rsrc = &vport->dflt_qv_rsrc;
	struct idpf_adapter *adapter = vport->adapter;
	struct virtchnl2_create_vport *vport_msg;
	struct idpf_vport_config *vport_config;
	u16 tx_itr[] = {2, 8, 64, 128, 256};
	u16 rx_itr[] = {2, 8, 32, 96, 128};
	struct idpf_rss_data *rss_data;
	u16 idx = vport->idx;
	int err;

	vport_config = adapter->vport_config[idx];
	rss_data = &vport_config->user_config.rss_data;
	vport_msg = adapter->vport_params_recvd[idx];

	err = idpf_vport_init_queue_reg_chunks(vport_config,
					       &vport_msg->chunks);
	if (err)
		return err;

	vport_config->max_q.max_txq = max_q->max_txq;
	vport_config->max_q.max_rxq = max_q->max_rxq;
	vport_config->max_q.max_complq = max_q->max_complq;
	vport_config->max_q.max_bufq = max_q->max_bufq;

	rsrc->txq_model = le16_to_cpu(vport_msg->txq_model);
	rsrc->rxq_model = le16_to_cpu(vport_msg->rxq_model);
	vport->vport_type = le16_to_cpu(vport_msg->vport_type);
	vport->vport_id = le32_to_cpu(vport_msg->vport_id);

	rss_data->rss_key_size = min_t(u16, NETDEV_RSS_KEY_LEN,
				       le16_to_cpu(vport_msg->rss_key_size));
	rss_data->rss_lut_size = le16_to_cpu(vport_msg->rss_lut_size);

	ether_addr_copy(vport->default_mac_addr, vport_msg->default_mac_addr);
	vport->max_mtu = le16_to_cpu(vport_msg->max_mtu) - LIBETH_RX_LL_LEN;

	/* Initialize Tx and Rx profiles for Dynamic Interrupt Moderation */
	memcpy(vport->rx_itr_profile, rx_itr, IDPF_DIM_PROFILE_SLOTS);
	memcpy(vport->tx_itr_profile, tx_itr, IDPF_DIM_PROFILE_SLOTS);

	idpf_vport_set_hsplit(vport, ETHTOOL_TCP_DATA_SPLIT_ENABLED);

	idpf_vport_init_num_qs(vport, vport_msg, rsrc);
	idpf_vport_calc_num_q_desc(vport, rsrc);
	idpf_vport_calc_num_q_groups(rsrc);
	idpf_vport_alloc_vec_indexes(vport, rsrc);

	vport->crc_enable = adapter->crc_enable;

	if (!(vport_msg->vport_flags &
	      cpu_to_le16(VIRTCHNL2_VPORT_UPLINK_PORT)))
		return 0;

	err = idpf_ptp_get_vport_tstamps_caps(vport);
	if (err) {
		/* Do not error on timestamp failure */
		pci_dbg(vport->adapter->pdev, "Tx timestamping not supported\n");
		return 0;
	}

	INIT_WORK(&vport->tstamp_task, idpf_tstamp_task);

	return 0;
}

/**
 * idpf_get_vec_ids - Initialize vector id from Mailbox parameters
 * @adapter: adapter structure to get the mailbox vector id
 * @vecids: Array of vector ids
 * @num_vecids: number of vector ids
 * @chunks: vector ids received over mailbox
 *
 * Will initialize the mailbox vector id which is received from the
 * get capabilities and data queue vector ids with ids received as
 * mailbox parameters.
 * Returns number of ids filled
 */
int idpf_get_vec_ids(struct idpf_adapter *adapter,
		     u16 *vecids, int num_vecids,
		     struct virtchnl2_vector_chunks *chunks)
{
	u16 num_chunks = le16_to_cpu(chunks->num_vchunks);
	int num_vecid_filled = 0;
	int i, j;

	vecids[num_vecid_filled] = adapter->mb_vector.v_idx;
	num_vecid_filled++;

	for (j = 0; j < num_chunks; j++) {
		struct virtchnl2_vector_chunk *chunk;
		u16 start_vecid, num_vec;

		chunk = &chunks->vchunks[j];
		num_vec = le16_to_cpu(chunk->num_vectors);
		start_vecid = le16_to_cpu(chunk->start_vector_id);

		for (i = 0; i < num_vec; i++) {
			if ((num_vecid_filled + i) < num_vecids) {
				vecids[num_vecid_filled + i] = start_vecid;
				start_vecid++;
			} else {
				break;
			}
		}
		num_vecid_filled = num_vecid_filled + i;
	}

	return num_vecid_filled;
}

/**
 * idpf_vport_get_queue_ids - Initialize queue id from Mailbox parameters
 * @qids: Array of queue ids
 * @num_qids: number of queue ids
 * @q_type: queue model
 * @chunks: queue ids received over mailbox
 *
 * Will initialize all queue ids with ids received as mailbox parameters
 * Returns number of ids filled
 */
static int idpf_vport_get_queue_ids(u32 *qids, int num_qids, u16 q_type,
				    struct idpf_queue_id_reg_info *chunks)
{
	u16 num_chunks = chunks->num_chunks;
	u32 num_q_id_filled = 0, i;
	u32 start_q_id, num_q;

	while (num_chunks--) {
		struct idpf_queue_id_reg_chunk *chunk;

		chunk = &chunks->queue_chunks[num_chunks];
		if (chunk->type != q_type)
			continue;

		num_q = chunk->num_queues;
		start_q_id = chunk->start_queue_id;

		for (i = 0; i < num_q; i++) {
			if ((num_q_id_filled + i) < num_qids) {
				qids[num_q_id_filled + i] = start_q_id;
				start_q_id++;
			} else {
				break;
			}
		}
		num_q_id_filled = num_q_id_filled + i;
	}

	return num_q_id_filled;
}

/**
 * __idpf_vport_queue_ids_init - Initialize queue ids from Mailbox parameters
 * @vport: virtual port for which the queues ids are initialized
 * @rsrc: pointer to queue and vector resources
 * @qids: queue ids
 * @num_qids: number of queue ids
 * @q_type: type of queue
 *
 * Will initialize all queue ids with ids received as mailbox
 * parameters. Returns number of queue ids initialized.
 */
static int __idpf_vport_queue_ids_init(struct idpf_vport *vport,
				       struct idpf_q_vec_rsrc *rsrc,
				       const u32 *qids,
				       int num_qids,
				       u32 q_type)
{
	int i, j, k = 0;

	switch (q_type) {
	case VIRTCHNL2_QUEUE_TYPE_TX:
		for (i = 0; i < rsrc->num_txq_grp; i++) {
			struct idpf_txq_group *tx_qgrp = &rsrc->txq_grps[i];

			for (j = 0; j < tx_qgrp->num_txq && k < num_qids; j++, k++)
				tx_qgrp->txqs[j]->q_id = qids[k];
		}
		break;
	case VIRTCHNL2_QUEUE_TYPE_RX:
		for (i = 0; i < rsrc->num_rxq_grp; i++) {
			struct idpf_rxq_group *rx_qgrp = &rsrc->rxq_grps[i];
			u16 num_rxq;

			if (idpf_is_queue_model_split(rsrc->rxq_model))
				num_rxq = rx_qgrp->splitq.num_rxq_sets;
			else
				num_rxq = rx_qgrp->singleq.num_rxq;

			for (j = 0; j < num_rxq && k < num_qids; j++, k++) {
				struct idpf_rx_queue *q;

				if (idpf_is_queue_model_split(rsrc->rxq_model))
					q = &rx_qgrp->splitq.rxq_sets[j]->rxq;
				else
					q = rx_qgrp->singleq.rxqs[j];
				q->q_id = qids[k];
			}
		}
		break;
	case VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION:
		for (i = 0; i < rsrc->num_txq_grp && k < num_qids; i++, k++) {
			struct idpf_txq_group *tx_qgrp = &rsrc->txq_grps[i];

			tx_qgrp->complq->q_id = qids[k];
		}
		break;
	case VIRTCHNL2_QUEUE_TYPE_RX_BUFFER:
		for (i = 0; i < rsrc->num_rxq_grp; i++) {
			struct idpf_rxq_group *rx_qgrp = &rsrc->rxq_grps[i];
			u8 num_bufqs = rsrc->num_bufqs_per_qgrp;

			for (j = 0; j < num_bufqs && k < num_qids; j++, k++) {
				struct idpf_buf_queue *q;

				q = &rx_qgrp->splitq.bufq_sets[j].bufq;
				q->q_id = qids[k];
			}
		}
		break;
	default:
		break;
	}

	return k;
}

/**
 * idpf_vport_queue_ids_init - Initialize queue ids from Mailbox parameters
 * @vport: virtual port for which the queues ids are initialized
 * @rsrc: pointer to queue and vector resources
 * @chunks: queue ids received over mailbox
 *
 * Will initialize all queue ids with ids received as mailbox parameters.
 *
 * Return: 0 on success, negative if all the queues are not initialized.
 */
int idpf_vport_queue_ids_init(struct idpf_vport *vport,
			      struct idpf_q_vec_rsrc *rsrc,
			      struct idpf_queue_id_reg_info *chunks)
{
	int num_ids, err = 0;
	u16 q_type;
	u32 *qids;

	qids = kcalloc(IDPF_MAX_QIDS, sizeof(u32), GFP_KERNEL);
	if (!qids)
		return -ENOMEM;

	num_ids = idpf_vport_get_queue_ids(qids, IDPF_MAX_QIDS,
					   VIRTCHNL2_QUEUE_TYPE_TX,
					   chunks);
	if (num_ids < rsrc->num_txq) {
		err = -EINVAL;
		goto mem_rel;
	}
	num_ids = __idpf_vport_queue_ids_init(vport, rsrc, qids, num_ids,
					      VIRTCHNL2_QUEUE_TYPE_TX);
	if (num_ids < rsrc->num_txq) {
		err = -EINVAL;
		goto mem_rel;
	}

	num_ids = idpf_vport_get_queue_ids(qids, IDPF_MAX_QIDS,
					   VIRTCHNL2_QUEUE_TYPE_RX,
					   chunks);
	if (num_ids < rsrc->num_rxq) {
		err = -EINVAL;
		goto mem_rel;
	}
	num_ids = __idpf_vport_queue_ids_init(vport, rsrc, qids, num_ids,
					      VIRTCHNL2_QUEUE_TYPE_RX);
	if (num_ids < rsrc->num_rxq) {
		err = -EINVAL;
		goto mem_rel;
	}

	if (!idpf_is_queue_model_split(rsrc->txq_model))
		goto check_rxq;

	q_type = VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION;
	num_ids = idpf_vport_get_queue_ids(qids, IDPF_MAX_QIDS, q_type, chunks);
	if (num_ids < rsrc->num_complq) {
		err = -EINVAL;
		goto mem_rel;
	}
	num_ids = __idpf_vport_queue_ids_init(vport, rsrc, qids,
					      num_ids, q_type);
	if (num_ids < rsrc->num_complq) {
		err = -EINVAL;
		goto mem_rel;
	}

check_rxq:
	if (!idpf_is_queue_model_split(rsrc->rxq_model))
		goto mem_rel;

	q_type = VIRTCHNL2_QUEUE_TYPE_RX_BUFFER;
	num_ids = idpf_vport_get_queue_ids(qids, IDPF_MAX_QIDS, q_type, chunks);
	if (num_ids < rsrc->num_bufq) {
		err = -EINVAL;
		goto mem_rel;
	}
	num_ids = __idpf_vport_queue_ids_init(vport, rsrc, qids,
					      num_ids, q_type);
	if (num_ids < rsrc->num_bufq)
		err = -EINVAL;

mem_rel:
	kfree(qids);

	return err;
}

/**
 * idpf_vport_adjust_qs - Adjust to new requested queues
 * @vport: virtual port data struct
 * @rsrc: pointer to queue and vector resources
 *
 * Renegotiate queues.  Returns 0 on success, negative on failure.
 */
int idpf_vport_adjust_qs(struct idpf_vport *vport, struct idpf_q_vec_rsrc *rsrc)
{
	struct virtchnl2_create_vport vport_msg;
	int err;

	vport_msg.txq_model = cpu_to_le16(rsrc->txq_model);
	vport_msg.rxq_model = cpu_to_le16(rsrc->rxq_model);
	err = idpf_vport_calc_total_qs(vport->adapter, vport->idx, &vport_msg,
				       NULL);
	if (err)
		return err;

	idpf_vport_init_num_qs(vport, &vport_msg, rsrc);
	idpf_vport_calc_num_q_groups(rsrc);

	return 0;
}

/**
 * idpf_is_capability_ena - Default implementation of capability checking
 * @adapter: Private data struct
 * @all: all or one flag
 * @field: caps field to check for flags
 * @flag: flag to check
 *
 * Return true if all capabilities are supported, false otherwise
 */
bool idpf_is_capability_ena(struct idpf_adapter *adapter, bool all,
			    enum idpf_cap_field field, u64 flag)
{
	u8 *caps = (u8 *)&adapter->caps;
	u32 *cap_field;

	if (!caps)
		return false;

	if (field == IDPF_BASE_CAPS)
		return false;

	cap_field = (u32 *)(caps + field);

	if (all)
		return (*cap_field & flag) == flag;
	else
		return !!(*cap_field & flag);
}

/**
 * idpf_vport_is_cap_ena - Check if vport capability is enabled
 * @vport: Private data struct
 * @flag: flag(s) to check
 *
 * Return: true if the capability is supported, false otherwise
 */
bool idpf_vport_is_cap_ena(struct idpf_vport *vport, u16 flag)
{
	struct virtchnl2_create_vport *vport_msg;

	vport_msg = vport->adapter->vport_params_recvd[vport->idx];

	return !!(le16_to_cpu(vport_msg->vport_flags) & flag);
}

/**
 * idpf_sideband_flow_type_ena - Check if steering is enabled for flow type
 * @vport: Private data struct
 * @flow_type: flow type to check (from ethtool.h)
 *
 * Return: true if sideband filters are allowed for @flow_type, false otherwise
 */
bool idpf_sideband_flow_type_ena(struct idpf_vport *vport, u32 flow_type)
{
	struct virtchnl2_create_vport *vport_msg;
	__le64 caps;

	vport_msg = vport->adapter->vport_params_recvd[vport->idx];
	caps = vport_msg->sideband_flow_caps;

	switch (flow_type) {
	case TCP_V4_FLOW:
		return !!(caps & cpu_to_le64(VIRTCHNL2_FLOW_IPV4_TCP));
	case UDP_V4_FLOW:
		return !!(caps & cpu_to_le64(VIRTCHNL2_FLOW_IPV4_UDP));
	default:
		return false;
	}
}

/**
 * idpf_sideband_action_ena - Check if steering is enabled for action
 * @vport: Private data struct
 * @fsp: flow spec
 *
 * Return: true if sideband filters are allowed for @fsp, false otherwise
 */
bool idpf_sideband_action_ena(struct idpf_vport *vport,
			      struct ethtool_rx_flow_spec *fsp)
{
	struct virtchnl2_create_vport *vport_msg;
	unsigned int supp_actions;

	vport_msg = vport->adapter->vport_params_recvd[vport->idx];
	supp_actions = le32_to_cpu(vport_msg->sideband_flow_actions);

	/* Actions Drop/Wake are not supported */
	if (fsp->ring_cookie == RX_CLS_FLOW_DISC ||
	    fsp->ring_cookie == RX_CLS_FLOW_WAKE)
		return false;

	return !!(supp_actions & VIRTCHNL2_ACTION_QUEUE);
}

unsigned int idpf_fsteer_max_rules(struct idpf_vport *vport)
{
	struct virtchnl2_create_vport *vport_msg;

	vport_msg = vport->adapter->vport_params_recvd[vport->idx];
	return le32_to_cpu(vport_msg->flow_steer_max_rules);
}

/**
 * idpf_get_vport_id: Get vport id
 * @vport: virtual port structure
 *
 * Return vport id from the adapter persistent data
 */
u32 idpf_get_vport_id(struct idpf_vport *vport)
{
	struct virtchnl2_create_vport *vport_msg;

	vport_msg = vport->adapter->vport_params_recvd[vport->idx];

	return le32_to_cpu(vport_msg->vport_id);
}

static void idpf_set_mac_type(const u8 *default_mac_addr,
			      struct virtchnl2_mac_addr *mac_addr)
{
	bool is_primary;

	is_primary = ether_addr_equal(default_mac_addr, mac_addr->addr);
	mac_addr->type = is_primary ? VIRTCHNL2_MAC_ADDR_PRIMARY :
				      VIRTCHNL2_MAC_ADDR_EXTRA;
}

/**
 * idpf_mac_filter_async_handler - Async callback for mac filters
 * @ctx: controlq context structure
 * @buff: response buffer pointer and size
 * @status: async call return value
 *
 * In some scenarios driver can't sleep and wait for a reply (e.g.: stack is
 * holding rtnl_lock) when adding a new mac filter. It puts us in a difficult
 * situation to deal with errors returned on the reply. The best we can
 * ultimately do is remove it from our list of mac filters and report the
 * error.
 */
static void idpf_mac_filter_async_handler(void *ctx,
					  struct kvec *buff,
					  int status)
{
	struct virtchnl2_mac_addr_list *ma_list;
	struct idpf_vport_config *vport_config;
	struct virtchnl2_mac_addr *mac_addr;
	struct idpf_adapter *adapter = ctx;
	struct idpf_mac_filter *f, *tmp;
	struct list_head *ma_list_head;
	struct idpf_vport *vport;
	u16 num_entries;
	int i;

	/* if success we're done, we're only here if something bad happened */
	if (!status || status == -ETIMEDOUT)
		return;

	ma_list = buff->iov_base;
	/* make sure at least struct is there */
	if (buff->iov_len < sizeof(*ma_list))
		goto invalid_payload;

	mac_addr = ma_list->mac_addr_list;
	num_entries = le16_to_cpu(ma_list->num_mac_addr);
	/* we should have received a buffer at least this big */
	if (buff->iov_len < struct_size(ma_list, mac_addr_list, num_entries))
		goto invalid_payload;

	vport = idpf_vid_to_vport(adapter, le32_to_cpu(ma_list->vport_id));
	if (!vport)
		goto invalid_payload;

	vport_config = adapter->vport_config[le32_to_cpu(ma_list->vport_id)];
	ma_list_head = &vport_config->user_config.mac_filter_list;

	/* We can't do much to reconcile bad filters at this point, however we
	 * should at least remove them from our list one way or the other so we
	 * have some idea what good filters we have.
	 */
	spin_lock_bh(&vport_config->mac_filter_list_lock);
	list_for_each_entry_safe(f, tmp, ma_list_head, list)
		for (i = 0; i < num_entries; i++)
			if (ether_addr_equal(mac_addr[i].addr, f->macaddr))
				list_del(&f->list);
	spin_unlock_bh(&vport_config->mac_filter_list_lock);
	dev_err_ratelimited(&adapter->pdev->dev, "Received error %d on sending MAC filter request\n",
			    status);
	return;

invalid_payload:
	dev_err_ratelimited(&adapter->pdev->dev, "Received invalid MAC filter payload (len %zd)\n",
			    buff->iov_len);
}

/**
 * idpf_add_del_mac_filters - Add/del mac filters
 * @adapter: adapter pointer used to send virtchnl message
 * @vport_config: persistent vport structure to get the MAC filter list
 * @default_mac_addr: default MAC address to compare with
 * @vport_id: vport identifier used while preparing the virtchnl message
 * @add: Add or delete flag
 * @async: Don't wait for return message
 *
 * Return: 0 on success, error on failure.
 **/
int idpf_add_del_mac_filters(struct idpf_adapter *adapter,
			     struct idpf_vport_config *vport_config,
			     const u8 *default_mac_addr, u32 vport_id,
			     bool add, bool async)
{
	struct virtchnl2_mac_addr *mac_addr __free(kfree) = NULL;
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= add ? VIRTCHNL2_OP_ADD_MAC_ADDR :
					VIRTCHNL2_OP_DEL_MAC_ADDR,
	};
	struct virtchnl2_mac_addr_list *ma_list;
	u32 num_msgs, total_filters = 0;
	struct idpf_mac_filter *f;
	int i = 0;

	if (async) {
		xn_params.resp_cb = idpf_mac_filter_async_handler;
		xn_params.send_ctx = adapter;
	}

	spin_lock_bh(&vport_config->mac_filter_list_lock);

	/* Find the number of newly added filters */
	list_for_each_entry(f, &vport_config->user_config.mac_filter_list,
			    list) {
		if (add && f->add)
			total_filters++;
		else if (!add && f->remove)
			total_filters++;
	}

	if (!total_filters) {
		spin_unlock_bh(&vport_config->mac_filter_list_lock);

		return 0;
	}

	/* Fill all the new filters into virtchannel message */
	mac_addr = kzalloc_objs(struct virtchnl2_mac_addr, total_filters,
				GFP_ATOMIC);
	if (!mac_addr) {
		spin_unlock_bh(&vport_config->mac_filter_list_lock);

		return -ENOMEM;
	}

	list_for_each_entry(f, &vport_config->user_config.mac_filter_list,
			    list) {
		if (add && f->add) {
			ether_addr_copy(mac_addr[i].addr, f->macaddr);
			idpf_set_mac_type(default_mac_addr, &mac_addr[i]);
			i++;
			f->add = false;
			if (i == total_filters)
				break;
		}
		if (!add && f->remove) {
			ether_addr_copy(mac_addr[i].addr, f->macaddr);
			idpf_set_mac_type(default_mac_addr, &mac_addr[i]);
			i++;
			f->remove = false;
			if (i == total_filters)
				break;
		}
	}

	spin_unlock_bh(&vport_config->mac_filter_list_lock);

	/* Chunk up the filters into multiple messages to avoid
	 * sending a control queue message buffer that is too large
	 */
	num_msgs = DIV_ROUND_UP(total_filters, IDPF_NUM_FILTERS_PER_MSG);

	for (u32 i = 0, k = 0; i < num_msgs; i++) {
		u32 entries_size, num_entries;
		size_t buf_size;
		int err;

		num_entries = min_t(u32, total_filters,
				    IDPF_NUM_FILTERS_PER_MSG);
		entries_size = sizeof(struct virtchnl2_mac_addr) * num_entries;
		buf_size = struct_size(ma_list, mac_addr_list, num_entries);

		ma_list = kzalloc(buf_size, GFP_ATOMIC);
		if (!ma_list)
			return -ENOMEM;

		ma_list->vport_id = cpu_to_le32(vport_id);
		ma_list->num_mac_addr = cpu_to_le16(num_entries);
		memcpy(ma_list->mac_addr_list, &mac_addr[k], entries_size);

		err = idpf_send_mb_msg_kfree(adapter, &xn_params, ma_list,
					     buf_size);
		if (err)
			return err;

		if (!async)
			libie_ctlq_release_rx_buf(&xn_params.recv_mem);

		k += num_entries;
		total_filters -= num_entries;
	}

	return 0;
}

/**
 * idpf_promiscuous_async_handler - async callback for promiscuous mode
 * @ctx: controlq context structure
 * @buff: response buffer pointer and size
 * @status: async call return value
 *
 * Nobody is waiting for the promiscuous virtchnl message response. Print
 * an error message if something went wrong and return.
 */
static void idpf_promiscuous_async_handler(void *ctx,
					   struct kvec *buff,
					   int status)
{
	struct idpf_adapter *adapter = ctx;

	if (status)
		dev_err_ratelimited(&adapter->pdev->dev, "Failed to set promiscuous mode: %d\n",
				    status);
}

/**
 * idpf_set_promiscuous - set promiscuous and send message to mailbox
 * @adapter: Driver specific private structure
 * @config_data: Vport specific config data
 * @vport_id: Vport identifier
 *
 * Request to enable promiscuous mode for the vport. Message is sent
 * asynchronously and won't wait for response.  Returns 0 on success, negative
 * on failure;
 */
int idpf_set_promiscuous(struct idpf_adapter *adapter,
			 struct idpf_vport_user_config_data *config_data,
			 u32 vport_id)
{
	struct libie_ctlq_xn_send_params xn_params = {
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.chnl_opcode	= VIRTCHNL2_OP_CONFIG_PROMISCUOUS_MODE,
		.resp_cb	= idpf_promiscuous_async_handler,
		.send_ctx	= adapter,
	};
	struct virtchnl2_promisc_info vpi;
	u16 flags = 0;

	if (test_bit(__IDPF_PROMISC_UC, config_data->user_flags))
		flags |= VIRTCHNL2_UNICAST_PROMISC;
	if (test_bit(__IDPF_PROMISC_MC, config_data->user_flags))
		flags |= VIRTCHNL2_MULTICAST_PROMISC;

	vpi.vport_id = cpu_to_le32(vport_id);
	vpi.flags = cpu_to_le16(flags);

	return idpf_send_mb_msg_stack(adapter, &xn_params, &vpi);
}

/**
 * idpf_idc_rdma_vc_send_sync - virtchnl send callback for IDC registered drivers
 * @cdev_info: IDC core device info pointer
 * @send_msg: message to send
 * @msg_size: size of message to send
 * @recv_msg: message to populate on reception of response
 * @recv_len: on input, maximum response size; on success, actual response size
 *
 * Return: 0 on success or error code on failure.
 */
int idpf_idc_rdma_vc_send_sync(struct iidc_rdma_core_dev_info *cdev_info,
			       u8 *send_msg, u16 msg_size,
			       u8 *recv_msg, u16 *recv_len)
{
	struct idpf_adapter *adapter = pci_get_drvdata(cdev_info->pdev);
	struct libie_ctlq_xn_send_params xn_params = {
		.chnl_opcode = VIRTCHNL2_OP_RDMA,
		.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
	};
	u8 on_stack_buf[LIBIE_CP_TX_COPYBREAK];
	void *send_buf;
	int err;

	if (!recv_msg || !recv_len || msg_size > LIBIE_CTLQ_MAX_BUF_LEN)
		return -EINVAL;

	if (!libie_cp_can_send_onstack(msg_size)) {
		send_buf = kzalloc(msg_size, GFP_KERNEL);
		if (!send_buf)
			return -ENOMEM;
	} else {
		send_buf = on_stack_buf;
	}

	memcpy(send_buf, send_msg, msg_size);
	err = idpf_send_mb_msg(adapter, &xn_params, send_buf, msg_size);
	if (err)
		return err;

	if (xn_params.recv_mem.iov_len > *recv_len) {
		err = -EINVAL;
		goto rel_buf;
	}

	*recv_len = xn_params.recv_mem.iov_len;
	memcpy(recv_msg, xn_params.recv_mem.iov_base, *recv_len);
rel_buf:
	libie_ctlq_release_rx_buf(&xn_params.recv_mem);
	return err;
}
EXPORT_SYMBOL_GPL(idpf_idc_rdma_vc_send_sync);
