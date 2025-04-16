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
	struct virtchnl2_ptp_get_caps *recv_ptp_caps_msg __free(kfree) = NULL;
	struct virtchnl2_ptp_get_caps send_ptp_caps_msg = {
		.caps = cpu_to_le32(VIRTCHNL2_CAP_PTP_GET_DEVICE_CLK_TIME |
				    VIRTCHNL2_CAP_PTP_GET_DEVICE_CLK_TIME_MB |
				    VIRTCHNL2_CAP_PTP_GET_CROSS_TIME |
				    VIRTCHNL2_CAP_PTP_GET_CROSS_TIME_MB)
	};
	struct idpf_vc_xn_params xn_params = {
		.vc_op = VIRTCHNL2_OP_PTP_GET_CAPS,
		.send_buf.iov_base = &send_ptp_caps_msg,
		.send_buf.iov_len = sizeof(send_ptp_caps_msg),
		.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
	};
	struct virtchnl2_ptp_clk_reg_offsets clock_offsets;
	struct idpf_ptp_secondary_mbx *scnd_mbx;
	struct idpf_ptp *ptp = adapter->ptp;
	enum idpf_ptp_access access_type;
	u32 temp_offset;
	int reply_sz;

	recv_ptp_caps_msg = kzalloc(sizeof(struct virtchnl2_ptp_get_caps),
				    GFP_KERNEL);
	if (!recv_ptp_caps_msg)
		return -ENOMEM;

	xn_params.recv_buf.iov_base = recv_ptp_caps_msg;
	xn_params.recv_buf.iov_len = sizeof(*recv_ptp_caps_msg);

	reply_sz = idpf_vc_xn_exec(adapter, &xn_params);
	if (reply_sz < 0)
		return reply_sz;
	else if (reply_sz != sizeof(*recv_ptp_caps_msg))
		return -EIO;

	ptp->caps = le32_to_cpu(recv_ptp_caps_msg->caps);

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
		return 0;

	clock_offsets = recv_ptp_caps_msg->clk_offsets;

	temp_offset = le32_to_cpu(clock_offsets.dev_clk_ns_l);
	ptp->dev_clk_regs.dev_clk_ns_l = idpf_get_reg_addr(adapter,
							   temp_offset);
	temp_offset = le32_to_cpu(clock_offsets.dev_clk_ns_h);
	ptp->dev_clk_regs.dev_clk_ns_h = idpf_get_reg_addr(adapter,
							   temp_offset);
	temp_offset = le32_to_cpu(clock_offsets.phy_clk_ns_l);
	ptp->dev_clk_regs.phy_clk_ns_l = idpf_get_reg_addr(adapter,
							   temp_offset);
	temp_offset = le32_to_cpu(clock_offsets.phy_clk_ns_h);
	ptp->dev_clk_regs.phy_clk_ns_h = idpf_get_reg_addr(adapter,
							   temp_offset);
	temp_offset = le32_to_cpu(clock_offsets.cmd_sync_trigger);
	ptp->dev_clk_regs.cmd_sync = idpf_get_reg_addr(adapter, temp_offset);

	return 0;
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
	struct virtchnl2_ptp_get_dev_clk_time get_dev_clk_time_msg;
	struct idpf_vc_xn_params xn_params = {
		.vc_op = VIRTCHNL2_OP_PTP_GET_DEV_CLK_TIME,
		.send_buf.iov_base = &get_dev_clk_time_msg,
		.send_buf.iov_len = sizeof(get_dev_clk_time_msg),
		.recv_buf.iov_base = &get_dev_clk_time_msg,
		.recv_buf.iov_len = sizeof(get_dev_clk_time_msg),
		.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
	};
	int reply_sz;
	u64 dev_time;

	reply_sz = idpf_vc_xn_exec(adapter, &xn_params);
	if (reply_sz < 0)
		return reply_sz;
	if (reply_sz != sizeof(get_dev_clk_time_msg))
		return -EIO;

	dev_time = le64_to_cpu(get_dev_clk_time_msg.dev_time_ns);
	dev_clk_time->dev_clk_time_ns = dev_time;

	return 0;
}
