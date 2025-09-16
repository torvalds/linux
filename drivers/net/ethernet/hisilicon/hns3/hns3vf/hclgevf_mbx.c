// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2016-2017 Hisilicon Limited.

#include "hclge_mbx.h"
#include "hclgevf_main.h"
#include "hnae3.h"

#define CREATE_TRACE_POINTS
#include "hclgevf_trace.h"

static int hclgevf_resp_to_errno(u16 resp_code)
{
	return resp_code ? -resp_code : 0;
}

#define HCLGEVF_MBX_MATCH_ID_START	1
static void hclgevf_reset_mbx_resp_status(struct hclgevf_dev *hdev)
{
	/* this function should be called with mbx_resp.mbx_mutex held
	 * to protect the received_response from race condition
	 */
	hdev->mbx_resp.received_resp  = false;
	hdev->mbx_resp.origin_mbx_msg = 0;
	hdev->mbx_resp.resp_status    = 0;
	hdev->mbx_resp.match_id++;
	/* Update match_id and ensure the value of match_id is not zero */
	if (hdev->mbx_resp.match_id == 0)
		hdev->mbx_resp.match_id = HCLGEVF_MBX_MATCH_ID_START;
	memset(hdev->mbx_resp.additional_info, 0, HCLGE_MBX_MAX_RESP_DATA_SIZE);
}

/* hclgevf_get_mbx_resp: used to get a response from PF after VF sends a mailbox
 * message to PF.
 * @hdev: pointer to struct hclgevf_dev
 * @code0: the message opcode VF send to PF.
 * @code1: the message sub-opcode VF send to PF.
 * @resp_data: pointer to store response data from PF to VF.
 * @resp_len: the length of resp_data from PF to VF.
 */
static int hclgevf_get_mbx_resp(struct hclgevf_dev *hdev, u16 code0, u16 code1,
				u8 *resp_data, u16 resp_len)
{
#define HCLGEVF_MAX_TRY_TIMES	500
#define HCLGEVF_SLEEP_USECOND	1000
	struct hclgevf_mbx_resp_status *mbx_resp;
	u16 r_code0, r_code1;
	int i = 0;

	if (resp_len > HCLGE_MBX_MAX_RESP_DATA_SIZE) {
		dev_err(&hdev->pdev->dev,
			"VF mbx response len(=%u) exceeds maximum(=%u)\n",
			resp_len,
			HCLGE_MBX_MAX_RESP_DATA_SIZE);
		return -EINVAL;
	}

	while ((!hdev->mbx_resp.received_resp) && (i < HCLGEVF_MAX_TRY_TIMES)) {
		if (test_bit(HCLGE_COMM_STATE_CMD_DISABLE,
			     &hdev->hw.hw.comm_state))
			return -EIO;

		usleep_range(HCLGEVF_SLEEP_USECOND, HCLGEVF_SLEEP_USECOND * 2);
		i++;
	}

	/* ensure additional_info will be seen after received_resp */
	smp_rmb();

	if (i >= HCLGEVF_MAX_TRY_TIMES) {
		dev_err(&hdev->pdev->dev,
			"VF could not get mbx(%u,%u) resp(=%d) from PF in %d tries\n",
			code0, code1, hdev->mbx_resp.received_resp, i);
		return -EIO;
	}

	mbx_resp = &hdev->mbx_resp;
	r_code0 = (u16)(mbx_resp->origin_mbx_msg >> 16);
	r_code1 = (u16)(mbx_resp->origin_mbx_msg & 0xff);

	if (mbx_resp->resp_status)
		return mbx_resp->resp_status;

	if (resp_data)
		memcpy(resp_data, &mbx_resp->additional_info[0], resp_len);

	hclgevf_reset_mbx_resp_status(hdev);

	if (!(r_code0 == code0 && r_code1 == code1 && !mbx_resp->resp_status)) {
		dev_err(&hdev->pdev->dev,
			"VF could not match resp code(code0=%u,code1=%u), %d\n",
			code0, code1, mbx_resp->resp_status);
		dev_err(&hdev->pdev->dev,
			"VF could not match resp r_code(r_code0=%u,r_code1=%u)\n",
			r_code0, r_code1);
		return -EIO;
	}

	return 0;
}

int hclgevf_send_mbx_msg(struct hclgevf_dev *hdev,
			 struct hclge_vf_to_pf_msg *send_msg, bool need_resp,
			 u8 *resp_data, u16 resp_len)
{
	struct hclge_mbx_vf_to_pf_cmd *req;
	struct hclge_desc desc;
	int status;

	req = (struct hclge_mbx_vf_to_pf_cmd *)desc.data;

	if (!send_msg) {
		dev_err(&hdev->pdev->dev,
			"failed to send mbx, msg is NULL\n");
		return -EINVAL;
	}

	hclgevf_cmd_setup_basic_desc(&desc, HCLGEVF_OPC_MBX_VF_TO_PF, false);
	if (need_resp)
		hnae3_set_bit(req->mbx_need_resp, HCLGE_MBX_NEED_RESP_B, 1);

	memcpy(&req->msg, send_msg, sizeof(struct hclge_vf_to_pf_msg));

	if (test_bit(HCLGEVF_STATE_NIC_REGISTERED, &hdev->state))
		trace_hclge_vf_mbx_send(hdev, req);

	/* synchronous send */
	if (need_resp) {
		mutex_lock(&hdev->mbx_resp.mbx_mutex);
		hclgevf_reset_mbx_resp_status(hdev);
		req->match_id = cpu_to_le16(hdev->mbx_resp.match_id);
		status = hclgevf_cmd_send(&hdev->hw, &desc, 1);
		if (status) {
			dev_err(&hdev->pdev->dev,
				"VF failed(=%d) to send mbx message to PF\n",
				status);
			mutex_unlock(&hdev->mbx_resp.mbx_mutex);
			return status;
		}

		status = hclgevf_get_mbx_resp(hdev, send_msg->code,
					      send_msg->subcode, resp_data,
					      resp_len);
		mutex_unlock(&hdev->mbx_resp.mbx_mutex);
	} else {
		/* asynchronous send */
		status = hclgevf_cmd_send(&hdev->hw, &desc, 1);
		if (status) {
			dev_err(&hdev->pdev->dev,
				"VF failed(=%d) to send mbx message to PF\n",
				status);
			return status;
		}
	}

	return status;
}

static bool hclgevf_cmd_crq_empty(struct hclgevf_hw *hw)
{
	u32 tail = hclgevf_read_dev(hw, HCLGE_COMM_NIC_CRQ_TAIL_REG);

	return tail == (u32)hw->hw.cmq.crq.next_to_use;
}

static void hclgevf_handle_mbx_response(struct hclgevf_dev *hdev,
					struct hclge_mbx_pf_to_vf_cmd *req)
{
	u16 vf_mbx_msg_subcode = le16_to_cpu(req->msg.vf_mbx_msg_subcode);
	u16 vf_mbx_msg_code = le16_to_cpu(req->msg.vf_mbx_msg_code);
	struct hclgevf_mbx_resp_status *resp = &hdev->mbx_resp;
	u16 resp_status = le16_to_cpu(req->msg.resp_status);
	u16 match_id = le16_to_cpu(req->match_id);

	if (resp->received_resp)
		dev_warn(&hdev->pdev->dev,
			"VF mbx resp flag not clear(%u)\n",
			 vf_mbx_msg_code);

	resp->origin_mbx_msg = (vf_mbx_msg_code << 16);
	resp->origin_mbx_msg |= vf_mbx_msg_subcode;
	resp->resp_status = hclgevf_resp_to_errno(resp_status);
	memcpy(resp->additional_info, req->msg.resp_data,
	       HCLGE_MBX_MAX_RESP_DATA_SIZE * sizeof(u8));

	/* ensure additional_info will be seen before setting received_resp */
	smp_wmb();

	if (match_id) {
		/* If match_id is not zero, it means PF support match_id.
		 * if the match_id is right, VF get the right response, or
		 * ignore the response. and driver will clear hdev->mbx_resp
		 * when send next message which need response.
		 */
		if (match_id == resp->match_id)
			resp->received_resp = true;
	} else {
		resp->received_resp = true;
	}
}

static void hclgevf_handle_mbx_msg(struct hclgevf_dev *hdev,
				   struct hclge_mbx_pf_to_vf_cmd *req)
{
	/* we will drop the async msg if we find ARQ as full
	 * and continue with next message
	 */
	if (atomic_read(&hdev->arq.count) >=
	    HCLGE_MBX_MAX_ARQ_MSG_NUM) {
		dev_warn(&hdev->pdev->dev,
			 "Async Q full, dropping msg(%u)\n",
			 le16_to_cpu(req->msg.code));
		return;
	}

	/* tail the async message in arq */
	memcpy(hdev->arq.msg_q[hdev->arq.tail], &req->msg,
	       HCLGE_MBX_MAX_ARQ_MSG_SIZE * sizeof(u16));
	hclge_mbx_tail_ptr_move_arq(hdev->arq);
	atomic_inc(&hdev->arq.count);

	hclgevf_mbx_task_schedule(hdev);
}

void hclgevf_mbx_handler(struct hclgevf_dev *hdev)
{
	struct hclge_mbx_pf_to_vf_cmd *req;
	struct hclge_comm_cmq_ring *crq;
	struct hclge_desc *desc;
	u16 flag;
	u16 code;

	crq = &hdev->hw.hw.cmq.crq;

	while (!hclgevf_cmd_crq_empty(&hdev->hw)) {
		if (test_bit(HCLGE_COMM_STATE_CMD_DISABLE,
			     &hdev->hw.hw.comm_state)) {
			dev_info(&hdev->pdev->dev, "vf crq need init\n");
			return;
		}

		desc = &crq->desc[crq->next_to_use];
		req = (struct hclge_mbx_pf_to_vf_cmd *)desc->data;

		flag = le16_to_cpu(crq->desc[crq->next_to_use].flag);
		code = le16_to_cpu(req->msg.code);
		if (unlikely(!hnae3_get_bit(flag, HCLGEVF_CMDQ_RX_OUTVLD_B))) {
			dev_warn(&hdev->pdev->dev,
				 "dropped invalid mailbox message, code = %u\n",
				 code);

			/* dropping/not processing this invalid message */
			crq->desc[crq->next_to_use].flag = 0;
			hclge_mbx_ring_ptr_move_crq(crq);
			continue;
		}

		trace_hclge_vf_mbx_get(hdev, req);

		/* synchronous messages are time critical and need preferential
		 * treatment. Therefore, we need to acknowledge all the sync
		 * responses as quickly as possible so that waiting tasks do not
		 * timeout and simultaneously queue the async messages for later
		 * prcessing in context of mailbox task i.e. the slow path.
		 */
		switch (code) {
		case HCLGE_MBX_PF_VF_RESP:
			hclgevf_handle_mbx_response(hdev, req);
			break;
		case HCLGE_MBX_LINK_STAT_CHANGE:
		case HCLGE_MBX_ASSERTING_RESET:
		case HCLGE_MBX_LINK_STAT_MODE:
		case HCLGE_MBX_PUSH_VLAN_INFO:
		case HCLGE_MBX_PUSH_PROMISC_INFO:
			hclgevf_handle_mbx_msg(hdev, req);
			break;
		default:
			dev_err(&hdev->pdev->dev,
				"VF received unsupported(%u) mbx msg from PF\n",
				code);
			break;
		}
		crq->desc[crq->next_to_use].flag = 0;
		hclge_mbx_ring_ptr_move_crq(crq);
	}

	/* Write back CMDQ_RQ header pointer, M7 need this pointer */
	hclgevf_write_dev(&hdev->hw, HCLGE_COMM_NIC_CRQ_HEAD_REG,
			  crq->next_to_use);
}

static void hclgevf_parse_promisc_info(struct hclgevf_dev *hdev,
				       u16 promisc_info)
{
	if (!promisc_info)
		dev_info(&hdev->pdev->dev,
			 "Promisc mode is closed by host for being untrusted.\n");
}

void hclgevf_mbx_async_handler(struct hclgevf_dev *hdev)
{
	struct hclge_mbx_port_base_vlan *vlan_info;
	struct hclge_mbx_link_status *link_info;
	struct hclge_mbx_link_mode *link_mode;
	enum hnae3_reset_type reset_type;
	u16 link_status, state;
	__le16 *msg_q;
	u16 opcode;
	u8 duplex;
	u32 speed;
	u32 tail;
	u8 flag;
	u16 idx;

	tail = hdev->arq.tail;

	/* process all the async queue messages */
	while (tail != hdev->arq.head) {
		if (test_bit(HCLGE_COMM_STATE_CMD_DISABLE,
			     &hdev->hw.hw.comm_state)) {
			dev_info(&hdev->pdev->dev,
				 "vf crq need init in async\n");
			return;
		}

		msg_q = hdev->arq.msg_q[hdev->arq.head];
		opcode = le16_to_cpu(msg_q[0]);
		switch (opcode) {
		case HCLGE_MBX_LINK_STAT_CHANGE:
			link_info = (struct hclge_mbx_link_status *)(msg_q + 1);
			link_status = le16_to_cpu(link_info->link_status);
			speed = le32_to_cpu(link_info->speed);
			duplex = (u8)le16_to_cpu(link_info->duplex);
			flag = link_info->flag;

			/* update upper layer with new link link status */
			hclgevf_update_speed_duplex(hdev, speed, duplex);
			hclgevf_update_link_status(hdev, link_status);

			if (flag & HCLGE_MBX_PUSH_LINK_STATUS_EN)
				set_bit(HCLGEVF_STATE_PF_PUSH_LINK_STATUS,
					&hdev->state);

			break;
		case HCLGE_MBX_LINK_STAT_MODE:
			link_mode = (struct hclge_mbx_link_mode *)(msg_q + 1);
			idx = le16_to_cpu(link_mode->idx);
			if (idx)
				hdev->hw.mac.supported =
					le64_to_cpu(link_mode->link_mode);
			else
				hdev->hw.mac.advertising =
					le64_to_cpu(link_mode->link_mode);
			break;
		case HCLGE_MBX_ASSERTING_RESET:
			/* PF has asserted reset hence VF should go in pending
			 * state and poll for the hardware reset status till it
			 * has been completely reset. After this stack should
			 * eventually be re-initialized.
			 */
			reset_type =
				(enum hnae3_reset_type)le16_to_cpu(msg_q[1]);
			set_bit(reset_type, &hdev->reset_pending);
			set_bit(HCLGEVF_RESET_PENDING, &hdev->reset_state);
			hclgevf_reset_task_schedule(hdev);

			break;
		case HCLGE_MBX_PUSH_VLAN_INFO:
			vlan_info =
				(struct hclge_mbx_port_base_vlan *)(msg_q + 1);
			state = le16_to_cpu(vlan_info->state);
			hclgevf_update_port_base_vlan_info(hdev, state,
							   vlan_info);
			break;
		case HCLGE_MBX_PUSH_PROMISC_INFO:
			hclgevf_parse_promisc_info(hdev, le16_to_cpu(msg_q[1]));
			break;
		default:
			dev_err(&hdev->pdev->dev,
				"fetched unsupported(%u) message from arq\n",
				opcode);
			break;
		}

		hclge_mbx_head_ptr_move_arq(hdev->arq);
		atomic_dec(&hdev->arq.count);
		msg_q = hdev->arq.msg_q[hdev->arq.head];
	}
}
