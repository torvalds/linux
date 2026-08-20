// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2024 Intel Corporation */

#include "idpf.h"
#include "idpf_ptp.h"
#include "idpf_virtchnl.h"

/**
 * idpf_ptp_get_caps - Send virtchnl get ptp capabilities message
 * @adapter: Driver specific private structure
 *
 * Send virtchnl get PTP capabilities message.
 *
 * Return: 0 on success, -errno on failure.
 */
int idpf_ptp_get_caps(struct idpf_adapter *adapter)
{
	struct virtchnl2_ptp_get_caps send_ptp_caps_msg = {
		.caps = cpu_to_le32(VIRTCHNL2_CAP_PTP_GET_DEVICE_CLK_TIME |
				    VIRTCHNL2_CAP_PTP_GET_DEVICE_CLK_TIME_MB |
				    VIRTCHNL2_CAP_PTP_GET_CROSS_TIME |
				    VIRTCHNL2_CAP_PTP_SET_DEVICE_CLK_TIME_MB |
				    VIRTCHNL2_CAP_PTP_ADJ_DEVICE_CLK_MB |
				    VIRTCHNL2_CAP_PTP_TX_TSTAMPS_MB)
	};
	struct libie_ctlq_xn_send_params xn_params = {
		.chnl_opcode = VIRTCHNL2_OP_PTP_GET_CAPS,
		.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
	};
	struct virtchnl2_ptp_cross_time_reg_offsets cross_tstamp_offsets;
	struct libie_mmio_info *mmio = &adapter->ctlq_ctx.mmio_info;
	struct virtchnl2_ptp_clk_adj_reg_offsets clk_adj_offsets;
	struct virtchnl2_ptp_clk_reg_offsets clock_offsets;
	struct virtchnl2_ptp_get_caps *recv_ptp_caps_msg;
	struct idpf_ptp_secondary_mbx *scnd_mbx;
	struct idpf_ptp *ptp = adapter->ptp;
	enum idpf_ptp_access access_type;
	u32 temp_offset;
	size_t reply_sz;
	int err;

	err = idpf_send_mb_msg_stack(adapter, &xn_params, &send_ptp_caps_msg);
	if (err)
		return err;

	reply_sz = xn_params.recv_mem.iov_len;
	if (reply_sz != sizeof(*recv_ptp_caps_msg)) {
		err = -EIO;
		goto free_resp;
	}

	recv_ptp_caps_msg = xn_params.recv_mem.iov_base;

	ptp->caps = le32_to_cpu(recv_ptp_caps_msg->caps);
	ptp->base_incval = le64_to_cpu(recv_ptp_caps_msg->base_incval);
	ptp->max_adj = le32_to_cpu(recv_ptp_caps_msg->max_adj);

	scnd_mbx = &ptp->secondary_mbx;
	scnd_mbx->peer_mbx_q_id = le16_to_cpu(recv_ptp_caps_msg->peer_mbx_q_id);

	/* if the ptp_mb_q_id holds invalid value (0xffff), the secondary
	 * mailbox is not supported.
	 */
	scnd_mbx->valid = scnd_mbx->peer_mbx_q_id != 0xffff;
	if (scnd_mbx->valid)
		scnd_mbx->peer_id = recv_ptp_caps_msg->peer_id;

	/* Determine the access type for the PTP features */
	idpf_ptp_get_features_access(adapter);

	access_type = ptp->get_dev_clk_time_access;
	if (access_type != IDPF_PTP_DIRECT)
		goto cross_tstamp;

	clock_offsets = recv_ptp_caps_msg->clk_offsets;

	temp_offset = le32_to_cpu(clock_offsets.dev_clk_ns_l);
	ptp->dev_clk_regs.dev_clk_ns_l =
		libie_pci_get_mmio_addr(mmio, temp_offset);
	temp_offset = le32_to_cpu(clock_offsets.dev_clk_ns_h);
	ptp->dev_clk_regs.dev_clk_ns_h =
		libie_pci_get_mmio_addr(mmio, temp_offset);
	temp_offset = le32_to_cpu(clock_offsets.phy_clk_ns_l);
	ptp->dev_clk_regs.phy_clk_ns_l =
		libie_pci_get_mmio_addr(mmio, temp_offset);
	temp_offset = le32_to_cpu(clock_offsets.phy_clk_ns_h);
	ptp->dev_clk_regs.phy_clk_ns_h =
		libie_pci_get_mmio_addr(mmio, temp_offset);
	temp_offset = le32_to_cpu(clock_offsets.cmd_sync_trigger);
	ptp->dev_clk_regs.cmd_sync =
		libie_pci_get_mmio_addr(mmio, temp_offset);

cross_tstamp:
	access_type = ptp->get_cross_tstamp_access;
	if (access_type != IDPF_PTP_DIRECT)
		goto discipline_clock;

	cross_tstamp_offsets = recv_ptp_caps_msg->cross_time_offsets;

	temp_offset = le32_to_cpu(cross_tstamp_offsets.sys_time_ns_l);
	ptp->dev_clk_regs.sys_time_ns_l =
		libie_pci_get_mmio_addr(mmio, temp_offset);
	temp_offset = le32_to_cpu(cross_tstamp_offsets.sys_time_ns_h);
	ptp->dev_clk_regs.sys_time_ns_h =
		libie_pci_get_mmio_addr(mmio, temp_offset);
	temp_offset = le32_to_cpu(cross_tstamp_offsets.cmd_sync_trigger);
	ptp->dev_clk_regs.cmd_sync =
		libie_pci_get_mmio_addr(mmio, temp_offset);

discipline_clock:
	access_type = ptp->adj_dev_clk_time_access;
	if (access_type != IDPF_PTP_DIRECT)
		goto free_resp;

	clk_adj_offsets = recv_ptp_caps_msg->clk_adj_offsets;

	/* Device clock offsets */
	temp_offset = le32_to_cpu(clk_adj_offsets.dev_clk_cmd_type);
	ptp->dev_clk_regs.cmd = libie_pci_get_mmio_addr(mmio, temp_offset);
	temp_offset = le32_to_cpu(clk_adj_offsets.dev_clk_incval_l);
	ptp->dev_clk_regs.incval_l = libie_pci_get_mmio_addr(mmio, temp_offset);
	temp_offset = le32_to_cpu(clk_adj_offsets.dev_clk_incval_h);
	ptp->dev_clk_regs.incval_h = libie_pci_get_mmio_addr(mmio, temp_offset);
	temp_offset = le32_to_cpu(clk_adj_offsets.dev_clk_shadj_l);
	ptp->dev_clk_regs.shadj_l = libie_pci_get_mmio_addr(mmio, temp_offset);
	temp_offset = le32_to_cpu(clk_adj_offsets.dev_clk_shadj_h);
	ptp->dev_clk_regs.shadj_h = libie_pci_get_mmio_addr(mmio, temp_offset);

	/* PHY clock offsets */
	temp_offset = le32_to_cpu(clk_adj_offsets.phy_clk_cmd_type);
	ptp->dev_clk_regs.phy_cmd =
		libie_pci_get_mmio_addr(mmio, temp_offset);
	temp_offset = le32_to_cpu(clk_adj_offsets.phy_clk_incval_l);
	ptp->dev_clk_regs.phy_incval_l =
		libie_pci_get_mmio_addr(mmio, temp_offset);
	temp_offset = le32_to_cpu(clk_adj_offsets.phy_clk_incval_h);
	ptp->dev_clk_regs.phy_incval_h =
		libie_pci_get_mmio_addr(mmio, temp_offset);
	temp_offset = le32_to_cpu(clk_adj_offsets.phy_clk_shadj_l);
	ptp->dev_clk_regs.phy_shadj_l =
		libie_pci_get_mmio_addr(mmio, temp_offset);
	temp_offset = le32_to_cpu(clk_adj_offsets.phy_clk_shadj_h);
	ptp->dev_clk_regs.phy_shadj_h =
		libie_pci_get_mmio_addr(mmio, temp_offset);

free_resp:
	libie_ctlq_release_rx_buf(&xn_params.recv_mem);
	return err;
}

/**
 * idpf_ptp_get_dev_clk_time - Send virtchnl get device clk time message
 * @adapter: Driver specific private structure
 * @dev_clk_time: Pointer to the device clock structure where the value is set
 *
 * Send virtchnl get time message to get the time of the clock.
 *
 * Return: 0 on success, -errno otherwise.
 */
int idpf_ptp_get_dev_clk_time(struct idpf_adapter *adapter,
			      struct idpf_ptp_dev_timers *dev_clk_time)
{
	struct virtchnl2_ptp_get_dev_clk_time *get_dev_clk_time_resp;
	struct virtchnl2_ptp_get_dev_clk_time get_dev_clk_time_msg;
	struct libie_ctlq_xn_send_params xn_params = {
		.chnl_opcode = VIRTCHNL2_OP_PTP_GET_DEV_CLK_TIME,
		.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
	};
	size_t reply_sz;
	u64 dev_time;
	int err;

	err = idpf_send_mb_msg_stack(adapter, &xn_params,
				     &get_dev_clk_time_msg);
	if (err)
		return err;

	reply_sz = xn_params.recv_mem.iov_len;
	if (reply_sz != sizeof(*get_dev_clk_time_resp)) {
		err = -EIO;
		goto free_resp;
	}

	get_dev_clk_time_resp = xn_params.recv_mem.iov_base;
	dev_time = le64_to_cpu(get_dev_clk_time_resp->dev_time_ns);
	dev_clk_time->dev_clk_time_ns = dev_time;

free_resp:
	libie_ctlq_release_rx_buf(&xn_params.recv_mem);
	return err;
}

/**
 * idpf_ptp_get_cross_time - Send virtchnl get cross time message
 * @adapter: Driver specific private structure
 * @cross_time: Pointer to the device clock structure where the value is set
 *
 * Send virtchnl get cross time message to get the time of the clock and the
 * system time.
 *
 * Return: 0 on success, -errno otherwise.
 */
int idpf_ptp_get_cross_time(struct idpf_adapter *adapter,
			    struct idpf_ptp_dev_timers *cross_time)
{
	struct virtchnl2_ptp_get_cross_time cross_time_send, *cross_time_recv;
	struct libie_ctlq_xn_send_params xn_params = {
		.chnl_opcode = VIRTCHNL2_OP_PTP_GET_CROSS_TIME,
		.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
	};
	int err = 0;

	err = idpf_send_mb_msg_stack(adapter, &xn_params, &cross_time_send);
	if (err)
		return err;

	if (xn_params.recv_mem.iov_len != sizeof(*cross_time_recv)) {
		err = -EIO;
		goto free_resp;
	}

	cross_time_recv = xn_params.recv_mem.iov_base;
	cross_time->dev_clk_time_ns = le64_to_cpu(cross_time_recv->dev_time_ns);
	cross_time->sys_time_ns = le64_to_cpu(cross_time_recv->sys_time_ns);

free_resp:
	libie_ctlq_release_rx_buf(&xn_params.recv_mem);
	return err;
}

/**
 * idpf_ptp_set_dev_clk_time - Send virtchnl set device time message
 * @adapter: Driver specific private structure
 * @time: New time value
 *
 * Send virtchnl set time message to set the time of the clock.
 *
 * Return: 0 on success, -errno otherwise.
 */
int idpf_ptp_set_dev_clk_time(struct idpf_adapter *adapter, u64 time)
{
	struct virtchnl2_ptp_set_dev_clk_time set_dev_clk_time_msg = {
		.dev_time_ns = cpu_to_le64(time),
	};
	struct libie_ctlq_xn_send_params xn_params = {
		.chnl_opcode = VIRTCHNL2_OP_PTP_SET_DEV_CLK_TIME,
		.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
	};
	int err;

	err = idpf_send_mb_msg_stack(adapter, &xn_params,
				     &set_dev_clk_time_msg);
	if (!err)
		libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return err;
}

/**
 * idpf_ptp_adj_dev_clk_time - Send virtchnl adj device clock time message
 * @adapter: Driver specific private structure
 * @delta: Offset in nanoseconds to adjust the time by
 *
 * Send virtchnl adj time message to adjust the clock by the indicated delta.
 *
 * Return: 0 on success, -errno otherwise.
 */
int idpf_ptp_adj_dev_clk_time(struct idpf_adapter *adapter, s64 delta)
{
	struct virtchnl2_ptp_adj_dev_clk_time adj_dev_clk_time_msg = {
		.delta = cpu_to_le64(delta),
	};
	struct libie_ctlq_xn_send_params xn_params = {
		.chnl_opcode = VIRTCHNL2_OP_PTP_ADJ_DEV_CLK_TIME,
		.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
	};
	int err;

	err = idpf_send_mb_msg_stack(adapter, &xn_params,
				     &adj_dev_clk_time_msg);
	if (!err)
		libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return err;
}

/**
 * idpf_ptp_adj_dev_clk_fine - Send virtchnl adj time message
 * @adapter: Driver specific private structure
 * @incval: Source timer increment value per clock cycle
 *
 * Send virtchnl adj fine message to adjust the frequency of the clock by
 * incval.
 *
 * Return: 0 on success, -errno otherwise.
 */
int idpf_ptp_adj_dev_clk_fine(struct idpf_adapter *adapter, u64 incval)
{
	struct virtchnl2_ptp_adj_dev_clk_fine adj_dev_clk_fine_msg = {
		.incval = cpu_to_le64(incval),
	};
	struct libie_ctlq_xn_send_params xn_params = {
		.chnl_opcode = VIRTCHNL2_OP_PTP_ADJ_DEV_CLK_FINE,
		.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
	};
	int err;

	err = idpf_send_mb_msg_stack(adapter, &xn_params,
				     &adj_dev_clk_fine_msg);
	if (!err)
		libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return err;
}

/**
 * idpf_ptp_get_vport_tstamps_caps - Send virtchnl to get tstamps caps for vport
 * @vport: Virtual port structure
 *
 * Send virtchnl get vport tstamps caps message to receive the set of tstamp
 * capabilities per vport.
 *
 * Return: 0 on success, -errno otherwise.
 */
int idpf_ptp_get_vport_tstamps_caps(struct idpf_vport *vport)
{
	struct virtchnl2_ptp_get_vport_tx_tstamp_caps send_tx_tstamp_caps;
	struct virtchnl2_ptp_get_vport_tx_tstamp_caps *rcv_tx_tstamp_caps;
	struct virtchnl2_ptp_tx_tstamp_latch_caps tx_tstamp_latch_caps;
	struct idpf_ptp_vport_tx_tstamp_caps *tstamp_caps;
	struct idpf_ptp_tx_tstamp *ptp_tx_tstamp, *tmp;
	struct libie_ctlq_xn_send_params xn_params = {
		.chnl_opcode = VIRTCHNL2_OP_PTP_GET_VPORT_TX_TSTAMP_CAPS,
		.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
	};
	enum idpf_ptp_access tstamp_access, get_dev_clk_access;
	struct idpf_ptp *ptp = vport->adapter->ptp;
	struct list_head *head;
	size_t reply_sz;
	u16 num_latches;
	int err = 0;
	u32 size;

	if (!ptp)
		return -EOPNOTSUPP;

	tstamp_access = ptp->tx_tstamp_access;
	get_dev_clk_access = ptp->get_dev_clk_time_access;
	if (tstamp_access == IDPF_PTP_NONE ||
	    get_dev_clk_access == IDPF_PTP_NONE)
		return -EOPNOTSUPP;

	send_tx_tstamp_caps.vport_id = cpu_to_le32(vport->vport_id);

	err = idpf_send_mb_msg_stack(vport->adapter, &xn_params,
				     &send_tx_tstamp_caps);
	if (err)
		return err;

	rcv_tx_tstamp_caps = xn_params.recv_mem.iov_base;
	reply_sz = xn_params.recv_mem.iov_len;
	if (reply_sz < sizeof(*rcv_tx_tstamp_caps)) {
		err = -EIO;
		goto get_tstamp_caps_out;
	}
	num_latches = le16_to_cpu(rcv_tx_tstamp_caps->num_latches);
	size = struct_size(rcv_tx_tstamp_caps, tstamp_latches, num_latches);
	if (reply_sz != size) {
		err = -EIO;
		goto get_tstamp_caps_out;
	}

	size = struct_size(tstamp_caps, tx_tstamp_status, num_latches);
	tstamp_caps = kzalloc(size, GFP_KERNEL);
	if (!tstamp_caps) {
		err = -ENOMEM;
		goto get_tstamp_caps_out;
	}

	tstamp_caps->access = true;
	tstamp_caps->num_entries = num_latches;

	INIT_LIST_HEAD(&tstamp_caps->latches_in_use);
	INIT_LIST_HEAD(&tstamp_caps->latches_free);

	spin_lock_init(&tstamp_caps->latches_lock);
	spin_lock_init(&tstamp_caps->status_lock);

	tstamp_caps->tstamp_ns_lo_bit = rcv_tx_tstamp_caps->tstamp_ns_lo_bit;

	for (u16 i = 0; i < tstamp_caps->num_entries; i++) {
		__le32 offset_l, offset_h;

		ptp_tx_tstamp = kzalloc_obj(*ptp_tx_tstamp);
		if (!ptp_tx_tstamp) {
			err = -ENOMEM;
			goto err_free_ptp_tx_stamp_list;
		}

		tx_tstamp_latch_caps = rcv_tx_tstamp_caps->tstamp_latches[i];

		if (tstamp_access != IDPF_PTP_DIRECT)
			goto skip_offsets;

		offset_l = tx_tstamp_latch_caps.tx_latch_reg_offset_l;
		offset_h = tx_tstamp_latch_caps.tx_latch_reg_offset_h;
		ptp_tx_tstamp->tx_latch_reg_offset_l = le32_to_cpu(offset_l);
		ptp_tx_tstamp->tx_latch_reg_offset_h = le32_to_cpu(offset_h);

skip_offsets:
		ptp_tx_tstamp->idx = tx_tstamp_latch_caps.index;

		list_add(&ptp_tx_tstamp->list_member,
			 &tstamp_caps->latches_free);

		tstamp_caps->tx_tstamp_status[i].state = IDPF_PTP_FREE;
	}

	vport->tx_tstamp_caps = tstamp_caps;
	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return 0;

err_free_ptp_tx_stamp_list:
	head = &tstamp_caps->latches_free;
	list_for_each_entry_safe(ptp_tx_tstamp, tmp, head, list_member) {
		list_del(&ptp_tx_tstamp->list_member);
		kfree(ptp_tx_tstamp);
	}

	kfree(tstamp_caps);
get_tstamp_caps_out:
	libie_ctlq_release_rx_buf(&xn_params.recv_mem);

	return err;
}

/**
 * idpf_ptp_update_tstamp_tracker - Update the Tx timestamp tracker based on
 *				    the skb compatibility.
 * @caps: Tx timestamp capabilities that monitor the latch status
 * @skb: skb for which the tstamp value is returned through virtchnl message
 * @current_state: Current state of the Tx timestamp latch
 * @expected_state: Expected state of the Tx timestamp latch
 *
 * Find a proper skb tracker for which the Tx timestamp is received and change
 * the state to expected value.
 *
 * Return: true if the tracker has been found and updated, false otherwise.
 */
static bool
idpf_ptp_update_tstamp_tracker(struct idpf_ptp_vport_tx_tstamp_caps *caps,
			       struct sk_buff *skb,
			       enum idpf_ptp_tx_tstamp_state current_state,
			       enum idpf_ptp_tx_tstamp_state expected_state)
{
	bool updated = false;

	spin_lock(&caps->status_lock);
	for (u16 i = 0; i < caps->num_entries; i++) {
		struct idpf_ptp_tx_tstamp_status *status;

		status = &caps->tx_tstamp_status[i];

		if (skb == status->skb && status->state == current_state) {
			status->state = expected_state;
			updated = true;
			break;
		}
	}
	spin_unlock(&caps->status_lock);

	return updated;
}

/**
 * idpf_ptp_get_tstamp_value - Get the Tx timestamp value and provide it
 *			       back to the skb.
 * @vport: Virtual port structure
 * @tstamp_latch: Tx timestamp latch structure fulfilled by the Control Plane
 * @ptp_tx_tstamp: Tx timestamp latch to add to the free list
 *
 * Read the value of the Tx timestamp for a given latch received from the
 * Control Plane, extend it to 64 bit and provide back to the skb.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int
idpf_ptp_get_tstamp_value(struct idpf_vport *vport,
			  struct virtchnl2_ptp_tx_tstamp_latch *tstamp_latch,
			  struct idpf_ptp_tx_tstamp *ptp_tx_tstamp)
{
	struct idpf_ptp_vport_tx_tstamp_caps *tx_tstamp_caps;
	struct skb_shared_hwtstamps shhwtstamps;
	bool state_upd = false;
	u8 tstamp_ns_lo_bit;
	u64 tstamp;

	tx_tstamp_caps = vport->tx_tstamp_caps;
	tstamp_ns_lo_bit = tx_tstamp_caps->tstamp_ns_lo_bit;

	ptp_tx_tstamp->tstamp = le64_to_cpu(tstamp_latch->tstamp);
	ptp_tx_tstamp->tstamp >>= tstamp_ns_lo_bit;

	state_upd = idpf_ptp_update_tstamp_tracker(tx_tstamp_caps,
						   ptp_tx_tstamp->skb,
						   IDPF_PTP_READ_VALUE,
						   IDPF_PTP_FREE);
	if (!state_upd)
		return -EINVAL;

	tstamp = idpf_ptp_extend_ts(vport, ptp_tx_tstamp->tstamp);
	shhwtstamps.hwtstamp = ns_to_ktime(tstamp);
	skb_tstamp_tx(ptp_tx_tstamp->skb, &shhwtstamps);
	consume_skb(ptp_tx_tstamp->skb);
	ptp_tx_tstamp->skb = NULL;

	list_add(&ptp_tx_tstamp->list_member,
		 &tx_tstamp_caps->latches_free);

	u64_stats_update_begin(&vport->tstamp_stats.stats_sync);
	u64_stats_inc(&vport->tstamp_stats.packets);
	u64_stats_update_end(&vport->tstamp_stats.stats_sync);

	return 0;
}

/**
 * idpf_ptp_get_tx_tstamp_async_handler - Async callback for getting Tx tstamps
 * @ctx: adapter pointer
 * @mem: address and size of the response
 * @status: return value of the request
 *
 * Read the tstamps Tx tstamp values from a received message and put them
 * directly to the skb. The number of timestamps to read is specified by
 * the virtchnl message.
 *
 * Return: 0 on success, -errno otherwise.
 */
static void
idpf_ptp_get_tx_tstamp_async_handler(void *ctx, struct kvec *mem, int status)
{
	struct virtchnl2_ptp_get_vport_tx_tstamp_latches *recv_tx_tstamp_msg;
	struct idpf_ptp_vport_tx_tstamp_caps *tx_tstamp_caps;
	struct virtchnl2_ptp_tx_tstamp_latch tstamp_latch;
	struct idpf_ptp_tx_tstamp *tx_tstamp, *tmp;
	struct idpf_vport *tstamp_vport = NULL;
	struct idpf_adapter *adapter = ctx;
	struct list_head *head;
	u16 num_latches;
	u32 vport_id;

	if (status)
		return;

	recv_tx_tstamp_msg = mem->iov_base;
	if (mem->iov_len < sizeof(*recv_tx_tstamp_msg))
		return;

	vport_id = le32_to_cpu(recv_tx_tstamp_msg->vport_id);

	idpf_for_each_vport(adapter, vport) {
		if (!vport)
			continue;

		if (vport->vport_id == vport_id) {
			tstamp_vport = vport;
			break;
		}
	}

	if (!tstamp_vport || !tstamp_vport->tx_tstamp_caps)
		return;

	tx_tstamp_caps = tstamp_vport->tx_tstamp_caps;
	num_latches = le16_to_cpu(recv_tx_tstamp_msg->num_latches);
	if (mem->iov_len < struct_size(recv_tx_tstamp_msg, tstamp_latches,
				       num_latches))
		return;

	spin_lock_bh(&tx_tstamp_caps->latches_lock);
	head = &tx_tstamp_caps->latches_in_use;

	for (u16 i = 0; i < num_latches; i++) {
		tstamp_latch = recv_tx_tstamp_msg->tstamp_latches[i];

		if (!tstamp_latch.valid)
			continue;

		if (list_empty(head))
			goto unlock;

		list_for_each_entry_safe(tx_tstamp, tmp, head, list_member) {
			if (tstamp_latch.index == tx_tstamp->idx) {
				int err;

				list_del(&tx_tstamp->list_member);
				err = idpf_ptp_get_tstamp_value(tstamp_vport,
								&tstamp_latch,
								tx_tstamp);
				if (err)
					goto unlock;

				break;
			}
		}
	}

unlock:
	spin_unlock_bh(&tx_tstamp_caps->latches_lock);
}

/**
 * idpf_ptp_get_tx_tstamp - Send virtchnl get Tx timestamp latches message
 * @vport: Virtual port structure
 *
 * Send virtchnl get Tx tstamp message to read the value of the HW timestamp.
 * The message contains a list of indexes set in the Tx descriptors.
 *
 * Return: 0 on success, -errno otherwise.
 */
int idpf_ptp_get_tx_tstamp(struct idpf_vport *vport)
{
	struct virtchnl2_ptp_get_vport_tx_tstamp_latches *send_tx_tstamp_msg;
	struct idpf_ptp_vport_tx_tstamp_caps *tx_tstamp_caps;
	struct libie_ctlq_xn_send_params xn_params = {
		.chnl_opcode = VIRTCHNL2_OP_PTP_GET_VPORT_TX_TSTAMP,
		.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
		.resp_cb = idpf_ptp_get_tx_tstamp_async_handler,
		.send_ctx = vport->adapter,
	};
	struct idpf_ptp_tx_tstamp *ptp_tx_tstamp;
	struct list_head *head;
	int size, msg_size;
	bool state_upd;
	u16 id = 0;

	tx_tstamp_caps = vport->tx_tstamp_caps;
	head = &tx_tstamp_caps->latches_in_use;

	size = struct_size(send_tx_tstamp_msg, tstamp_latches,
			   tx_tstamp_caps->num_entries);
	send_tx_tstamp_msg = kzalloc(size, GFP_KERNEL);
	if (!send_tx_tstamp_msg)
		return -ENOMEM;

	spin_lock_bh(&tx_tstamp_caps->latches_lock);
	list_for_each_entry(ptp_tx_tstamp, head, list_member) {
		u8 idx;

		state_upd = idpf_ptp_update_tstamp_tracker(tx_tstamp_caps,
							   ptp_tx_tstamp->skb,
							   IDPF_PTP_REQUEST,
							   IDPF_PTP_READ_VALUE);
		if (!state_upd)
			continue;

		idx = ptp_tx_tstamp->idx;
		send_tx_tstamp_msg->tstamp_latches[id].index = idx;
		id++;
	}
	spin_unlock_bh(&tx_tstamp_caps->latches_lock);

	msg_size = struct_size(send_tx_tstamp_msg, tstamp_latches, id);
	send_tx_tstamp_msg->vport_id = cpu_to_le32(vport->vport_id);
	send_tx_tstamp_msg->num_latches = cpu_to_le16(id);

	return idpf_send_mb_msg_kfree(vport->adapter, &xn_params,
				      send_tx_tstamp_msg, msg_size);
}
