// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2016-2017 Hisilicon Limited.

#include "hclge_main.h"
#include "hclge_mbx.h"
#include "hnae3.h"

/* hclge_gen_resp_to_vf: used to generate a synchronous response to VF when PF
 * receives a mailbox message from VF.
 * @vport: pointer to struct hclge_vport
 * @vf_to_pf_req: pointer to hclge_mbx_vf_to_pf_cmd of the original mailbox
 *		  message
 * @resp_status: indicate to VF whether its request success(0) or failed.
 */
static int hclge_gen_resp_to_vf(struct hclge_vport *vport,
				struct hclge_mbx_vf_to_pf_cmd *vf_to_pf_req,
				int resp_status,
				u8 *resp_data, u16 resp_data_len)
{
	struct hclge_mbx_pf_to_vf_cmd *resp_pf_to_vf;
	struct hclge_dev *hdev = vport->back;
	enum hclge_cmd_status status;
	struct hclge_desc desc;

	resp_pf_to_vf = (struct hclge_mbx_pf_to_vf_cmd *)desc.data;

	if (resp_data_len > HCLGE_MBX_MAX_RESP_DATA_SIZE) {
		dev_err(&hdev->pdev->dev,
			"PF fail to gen resp to VF len %d exceeds max len %d\n",
			resp_data_len,
			HCLGE_MBX_MAX_RESP_DATA_SIZE);
	}

	hclge_cmd_setup_basic_desc(&desc, HCLGEVF_OPC_MBX_PF_TO_VF, false);

	resp_pf_to_vf->dest_vfid = vf_to_pf_req->mbx_src_vfid;
	resp_pf_to_vf->msg_len = vf_to_pf_req->msg_len;

	resp_pf_to_vf->msg[0] = HCLGE_MBX_PF_VF_RESP;
	resp_pf_to_vf->msg[1] = vf_to_pf_req->msg[0];
	resp_pf_to_vf->msg[2] = vf_to_pf_req->msg[1];
	resp_pf_to_vf->msg[3] = (resp_status == 0) ? 0 : 1;

	if (resp_data && resp_data_len > 0)
		memcpy(&resp_pf_to_vf->msg[4], resp_data, resp_data_len);

	status = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (status)
		dev_err(&hdev->pdev->dev,
			"PF failed(=%d) to send response to VF\n", status);

	return status;
}

static int hclge_send_mbx_msg(struct hclge_vport *vport, u8 *msg, u16 msg_len,
			      u16 mbx_opcode, u8 dest_vfid)
{
	struct hclge_mbx_pf_to_vf_cmd *resp_pf_to_vf;
	struct hclge_dev *hdev = vport->back;
	enum hclge_cmd_status status;
	struct hclge_desc desc;

	resp_pf_to_vf = (struct hclge_mbx_pf_to_vf_cmd *)desc.data;

	hclge_cmd_setup_basic_desc(&desc, HCLGEVF_OPC_MBX_PF_TO_VF, false);

	resp_pf_to_vf->dest_vfid = dest_vfid;
	resp_pf_to_vf->msg_len = msg_len;
	resp_pf_to_vf->msg[0] = mbx_opcode;

	memcpy(&resp_pf_to_vf->msg[1], msg, msg_len);

	status = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (status)
		dev_err(&hdev->pdev->dev,
			"PF failed(=%d) to send mailbox message to VF\n",
			status);

	return status;
}

static int hclge_inform_reset_assert_to_vf(struct hclge_vport *vport)
{
	u8 msg_data[2];
	u8 dest_vfid;

	dest_vfid = (u8)vport->vport_id;

	/* send this requested info to VF */
	return hclge_send_mbx_msg(vport, msg_data, sizeof(u8),
				  HCLGE_MBX_ASSERTING_RESET, dest_vfid);
}

static void hclge_free_vector_ring_chain(struct hnae3_ring_chain_node *head)
{
	struct hnae3_ring_chain_node *chain_tmp, *chain;

	chain = head->next;

	while (chain) {
		chain_tmp = chain->next;
		kzfree(chain);
		chain = chain_tmp;
	}
}

/* hclge_get_ring_chain_from_mbx: get ring type & tqp id & int_gl idx
 * from mailbox message
 * msg[0]: opcode
 * msg[1]: <not relevant to this function>
 * msg[2]: ring_num
 * msg[3]: first ring type (TX|RX)
 * msg[4]: first tqp id
 * msg[5]: first int_gl idx
 * msg[6] ~ msg[14]: other ring type, tqp id and int_gl idx
 */
static int hclge_get_ring_chain_from_mbx(
			struct hclge_mbx_vf_to_pf_cmd *req,
			struct hnae3_ring_chain_node *ring_chain,
			struct hclge_vport *vport)
{
	struct hnae3_ring_chain_node *cur_chain, *new_chain;
	int ring_num;
	int i;

	ring_num = req->msg[2];

	if (ring_num > ((HCLGE_MBX_VF_MSG_DATA_NUM -
		HCLGE_MBX_RING_MAP_BASIC_MSG_NUM) /
		HCLGE_MBX_RING_NODE_VARIABLE_NUM))
		return -ENOMEM;

	hnae3_set_bit(ring_chain->flag, HNAE3_RING_TYPE_B, req->msg[3]);
	ring_chain->tqp_index =
			hclge_get_queue_id(vport->nic.kinfo.tqp[req->msg[4]]);
	hnae3_set_field(ring_chain->int_gl_idx, HNAE3_RING_GL_IDX_M,
			HNAE3_RING_GL_IDX_S,
			req->msg[5]);

	cur_chain = ring_chain;

	for (i = 1; i < ring_num; i++) {
		new_chain = kzalloc(sizeof(*new_chain), GFP_KERNEL);
		if (!new_chain)
			goto err;

		hnae3_set_bit(new_chain->flag, HNAE3_RING_TYPE_B,
			      req->msg[HCLGE_MBX_RING_NODE_VARIABLE_NUM * i +
			      HCLGE_MBX_RING_MAP_BASIC_MSG_NUM]);

		new_chain->tqp_index =
		hclge_get_queue_id(vport->nic.kinfo.tqp
			[req->msg[HCLGE_MBX_RING_NODE_VARIABLE_NUM * i +
			HCLGE_MBX_RING_MAP_BASIC_MSG_NUM + 1]]);

		hnae3_set_field(new_chain->int_gl_idx, HNAE3_RING_GL_IDX_M,
				HNAE3_RING_GL_IDX_S,
				req->msg[HCLGE_MBX_RING_NODE_VARIABLE_NUM * i +
				HCLGE_MBX_RING_MAP_BASIC_MSG_NUM + 2]);

		cur_chain->next = new_chain;
		cur_chain = new_chain;
	}

	return 0;
err:
	hclge_free_vector_ring_chain(ring_chain);
	return -ENOMEM;
}

static int hclge_map_unmap_ring_to_vf_vector(struct hclge_vport *vport, bool en,
					     struct hclge_mbx_vf_to_pf_cmd *req)
{
	struct hnae3_ring_chain_node ring_chain;
	int vector_id = req->msg[1];
	int ret;

	memset(&ring_chain, 0, sizeof(ring_chain));
	ret = hclge_get_ring_chain_from_mbx(req, &ring_chain, vport);
	if (ret)
		return ret;

	ret = hclge_bind_ring_with_vector(vport, vector_id, en, &ring_chain);
	if (ret)
		return ret;

	hclge_free_vector_ring_chain(&ring_chain);

	return 0;
}

static int hclge_set_vf_promisc_mode(struct hclge_vport *vport,
				     struct hclge_mbx_vf_to_pf_cmd *req)
{
	bool en_uc = req->msg[1] ? true : false;
	bool en_mc = req->msg[2] ? true : false;
	struct hclge_promisc_param param;

	/* always enable broadcast promisc bit */
	hclge_promisc_param_init(&param, en_uc, en_mc, true, vport->vport_id);
	return hclge_cmd_set_promisc_mode(vport->back, &param);
}

static int hclge_set_vf_uc_mac_addr(struct hclge_vport *vport,
				    struct hclge_mbx_vf_to_pf_cmd *mbx_req,
				    bool gen_resp)
{
	const u8 *mac_addr = (const u8 *)(&mbx_req->msg[2]);
	struct hclge_dev *hdev = vport->back;
	int status;

	if (mbx_req->msg[1] == HCLGE_MBX_MAC_VLAN_UC_MODIFY) {
		const u8 *old_addr = (const u8 *)(&mbx_req->msg[8]);

		hclge_rm_uc_addr_common(vport, old_addr);
		status = hclge_add_uc_addr_common(vport, mac_addr);
		if (status)
			hclge_add_uc_addr_common(vport, old_addr);
	} else if (mbx_req->msg[1] == HCLGE_MBX_MAC_VLAN_UC_ADD) {
		status = hclge_add_uc_addr_common(vport, mac_addr);
	} else if (mbx_req->msg[1] == HCLGE_MBX_MAC_VLAN_UC_REMOVE) {
		status = hclge_rm_uc_addr_common(vport, mac_addr);
	} else {
		dev_err(&hdev->pdev->dev,
			"failed to set unicast mac addr, unknown subcode %d\n",
			mbx_req->msg[1]);
		return -EIO;
	}

	if (gen_resp)
		hclge_gen_resp_to_vf(vport, mbx_req, status, NULL, 0);

	return 0;
}

static int hclge_set_vf_mc_mta_status(struct hclge_vport *vport,
				      u8 *msg, u8 idx, bool is_end)
{
#define HCLGE_MTA_STATUS_MSG_SIZE 13
#define HCLGE_MTA_STATUS_MSG_BITS \
				(HCLGE_MTA_STATUS_MSG_SIZE * BITS_PER_BYTE)
#define HCLGE_MTA_STATUS_MSG_END_BITS \
				(HCLGE_MTA_TBL_SIZE % HCLGE_MTA_STATUS_MSG_BITS)
	unsigned long status[BITS_TO_LONGS(HCLGE_MTA_STATUS_MSG_BITS)];
	u16 tbl_cnt;
	u16 tbl_idx;
	u8 msg_ofs;
	u8 msg_bit;

	tbl_cnt = is_end ? HCLGE_MTA_STATUS_MSG_END_BITS :
			HCLGE_MTA_STATUS_MSG_BITS;

	/* set msg field */
	msg_ofs = 0;
	msg_bit = 0;
	memset(status, 0, sizeof(status));
	for (tbl_idx = 0; tbl_idx < tbl_cnt; tbl_idx++) {
		if (msg[msg_ofs] & BIT(msg_bit))
			set_bit(tbl_idx, status);

		msg_bit++;
		if (msg_bit == BITS_PER_BYTE) {
			msg_bit = 0;
			msg_ofs++;
		}
	}

	return hclge_update_mta_status_common(vport,
					status, idx * HCLGE_MTA_STATUS_MSG_BITS,
					tbl_cnt, is_end);
}

static int hclge_set_vf_mc_mac_addr(struct hclge_vport *vport,
				    struct hclge_mbx_vf_to_pf_cmd *mbx_req,
				    bool gen_resp)
{
	const u8 *mac_addr = (const u8 *)(&mbx_req->msg[2]);
	struct hclge_dev *hdev = vport->back;
	u8 resp_len = 0;
	u8 resp_data;
	int status;

	if (mbx_req->msg[1] == HCLGE_MBX_MAC_VLAN_MC_ADD) {
		status = hclge_add_mc_addr_common(vport, mac_addr);
	} else if (mbx_req->msg[1] == HCLGE_MBX_MAC_VLAN_MC_REMOVE) {
		status = hclge_rm_mc_addr_common(vport, mac_addr);
	} else if (mbx_req->msg[1] == HCLGE_MBX_MAC_VLAN_MC_FUNC_MTA_ENABLE) {
		u8 func_id = vport->vport_id;
		bool enable = mbx_req->msg[2];

		status = hclge_cfg_func_mta_filter(hdev, func_id, enable);
	} else if (mbx_req->msg[1] == HCLGE_MBX_MAC_VLAN_MTA_TYPE_READ) {
		resp_data = hdev->mta_mac_sel_type;
		resp_len = sizeof(u8);
		gen_resp = true;
		status = 0;
	} else if (mbx_req->msg[1] == HCLGE_MBX_MAC_VLAN_MTA_STATUS_UPDATE) {
		/* mta status update msg format
		 * msg[2.6 : 2.0]  msg index
		 * msg[2.7]        msg is end
		 * msg[15 : 3]     mta status bits[103 : 0]
		 */
		bool is_end = (mbx_req->msg[2] & 0x80) ? true : false;

		status = hclge_set_vf_mc_mta_status(vport, &mbx_req->msg[3],
						    mbx_req->msg[2] & 0x7F,
						    is_end);
	} else {
		dev_err(&hdev->pdev->dev,
			"failed to set mcast mac addr, unknown subcode %d\n",
			mbx_req->msg[1]);
		return -EIO;
	}

	if (gen_resp)
		hclge_gen_resp_to_vf(vport, mbx_req, status,
				     &resp_data, resp_len);

	return 0;
}

static int hclge_set_vf_vlan_cfg(struct hclge_vport *vport,
				 struct hclge_mbx_vf_to_pf_cmd *mbx_req,
				 bool gen_resp)
{
	int status = 0;

	if (mbx_req->msg[1] == HCLGE_MBX_VLAN_FILTER) {
		struct hnae3_handle *handle = &vport->nic;
		u16 vlan, proto;
		bool is_kill;

		is_kill = !!mbx_req->msg[2];
		memcpy(&vlan, &mbx_req->msg[3], sizeof(vlan));
		memcpy(&proto, &mbx_req->msg[5], sizeof(proto));
		status = hclge_set_vlan_filter(handle, cpu_to_be16(proto),
					       vlan, is_kill);
	} else if (mbx_req->msg[1] == HCLGE_MBX_VLAN_RX_OFF_CFG) {
		struct hnae3_handle *handle = &vport->nic;
		bool en = mbx_req->msg[2] ? true : false;

		status = hclge_en_hw_strip_rxvtag(handle, en);
	}

	if (gen_resp)
		status = hclge_gen_resp_to_vf(vport, mbx_req, status, NULL, 0);

	return status;
}

static int hclge_get_vf_tcinfo(struct hclge_vport *vport,
			       struct hclge_mbx_vf_to_pf_cmd *mbx_req,
			       bool gen_resp)
{
	struct hclge_dev *hdev = vport->back;
	int ret;

	ret = hclge_gen_resp_to_vf(vport, mbx_req, 0, &hdev->hw_tc_map,
				   sizeof(u8));

	return ret;
}

static int hclge_get_vf_queue_info(struct hclge_vport *vport,
				   struct hclge_mbx_vf_to_pf_cmd *mbx_req,
				   bool gen_resp)
{
#define HCLGE_TQPS_RSS_INFO_LEN		8
	u8 resp_data[HCLGE_TQPS_RSS_INFO_LEN];
	struct hclge_dev *hdev = vport->back;

	/* get the queue related info */
	memcpy(&resp_data[0], &vport->alloc_tqps, sizeof(u16));
	memcpy(&resp_data[2], &vport->nic.kinfo.rss_size, sizeof(u16));
	memcpy(&resp_data[4], &hdev->num_desc, sizeof(u16));
	memcpy(&resp_data[6], &hdev->rx_buf_len, sizeof(u16));

	return hclge_gen_resp_to_vf(vport, mbx_req, 0, resp_data,
				    HCLGE_TQPS_RSS_INFO_LEN);
}

static int hclge_get_link_info(struct hclge_vport *vport,
			       struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
	struct hclge_dev *hdev = vport->back;
	u16 link_status;
	u8 msg_data[8];
	u8 dest_vfid;
	u16 duplex;

	/* mac.link can only be 0 or 1 */
	link_status = (u16)hdev->hw.mac.link;
	duplex = hdev->hw.mac.duplex;
	memcpy(&msg_data[0], &link_status, sizeof(u16));
	memcpy(&msg_data[2], &hdev->hw.mac.speed, sizeof(u32));
	memcpy(&msg_data[6], &duplex, sizeof(u16));
	dest_vfid = mbx_req->mbx_src_vfid;

	/* send this requested info to VF */
	return hclge_send_mbx_msg(vport, msg_data, sizeof(msg_data),
				  HCLGE_MBX_LINK_STAT_CHANGE, dest_vfid);
}

static void hclge_mbx_reset_vf_queue(struct hclge_vport *vport,
				     struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
	u16 queue_id;

	memcpy(&queue_id, &mbx_req->msg[2], sizeof(queue_id));

	hclge_reset_vf_queue(vport, queue_id);

	/* send response msg to VF after queue reset complete*/
	hclge_gen_resp_to_vf(vport, mbx_req, 0, NULL, 0);
}

static void hclge_reset_vf(struct hclge_vport *vport,
			   struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
	struct hclge_dev *hdev = vport->back;
	int ret;

	dev_warn(&hdev->pdev->dev, "PF received VF reset request from VF %d!",
		 mbx_req->mbx_src_vfid);

	/* Acknowledge VF that PF is now about to assert the reset for the VF.
	 * On receiving this message VF will get into pending state and will
	 * start polling for the hardware reset completion status.
	 */
	ret = hclge_inform_reset_assert_to_vf(vport);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"PF fail(%d) to inform VF(%d)of reset, reset failed!\n",
			ret, vport->vport_id);
		return;
	}

	dev_warn(&hdev->pdev->dev, "PF is now resetting VF %d.\n",
		 mbx_req->mbx_src_vfid);
	/* reset this virtual function */
	hclge_func_reset_cmd(hdev, mbx_req->mbx_src_vfid);
}

static bool hclge_cmd_crq_empty(struct hclge_hw *hw)
{
	u32 tail = hclge_read_dev(hw, HCLGE_NIC_CRQ_TAIL_REG);

	return tail == hw->cmq.crq.next_to_use;
}

void hclge_mbx_handler(struct hclge_dev *hdev)
{
	struct hclge_cmq_ring *crq = &hdev->hw.cmq.crq;
	struct hclge_mbx_vf_to_pf_cmd *req;
	struct hclge_vport *vport;
	struct hclge_desc *desc;
	int ret, flag;

	/* handle all the mailbox requests in the queue */
	while (!hclge_cmd_crq_empty(&hdev->hw)) {
		if (test_bit(HCLGE_STATE_CMD_DISABLE, &hdev->state)) {
			dev_warn(&hdev->pdev->dev,
				 "command queue needs re-initializing\n");
			return;
		}

		desc = &crq->desc[crq->next_to_use];
		req = (struct hclge_mbx_vf_to_pf_cmd *)desc->data;

		flag = le16_to_cpu(crq->desc[crq->next_to_use].flag);
		if (unlikely(!hnae3_get_bit(flag, HCLGE_CMDQ_RX_OUTVLD_B))) {
			dev_warn(&hdev->pdev->dev,
				 "dropped invalid mailbox message, code = %d\n",
				 req->msg[0]);

			/* dropping/not processing this invalid message */
			crq->desc[crq->next_to_use].flag = 0;
			hclge_mbx_ring_ptr_move_crq(crq);
			continue;
		}

		vport = &hdev->vport[req->mbx_src_vfid];

		switch (req->msg[0]) {
		case HCLGE_MBX_MAP_RING_TO_VECTOR:
			ret = hclge_map_unmap_ring_to_vf_vector(vport, true,
								req);
			break;
		case HCLGE_MBX_UNMAP_RING_TO_VECTOR:
			ret = hclge_map_unmap_ring_to_vf_vector(vport, false,
								req);
			break;
		case HCLGE_MBX_SET_PROMISC_MODE:
			ret = hclge_set_vf_promisc_mode(vport, req);
			if (ret)
				dev_err(&hdev->pdev->dev,
					"PF fail(%d) to set VF promisc mode\n",
					ret);
			break;
		case HCLGE_MBX_SET_UNICAST:
			ret = hclge_set_vf_uc_mac_addr(vport, req, true);
			if (ret)
				dev_err(&hdev->pdev->dev,
					"PF fail(%d) to set VF UC MAC Addr\n",
					ret);
			break;
		case HCLGE_MBX_SET_MULTICAST:
			ret = hclge_set_vf_mc_mac_addr(vport, req, false);
			if (ret)
				dev_err(&hdev->pdev->dev,
					"PF fail(%d) to set VF MC MAC Addr\n",
					ret);
			break;
		case HCLGE_MBX_SET_VLAN:
			ret = hclge_set_vf_vlan_cfg(vport, req, false);
			if (ret)
				dev_err(&hdev->pdev->dev,
					"PF failed(%d) to config VF's VLAN\n",
					ret);
			break;
		case HCLGE_MBX_GET_QINFO:
			ret = hclge_get_vf_queue_info(vport, req, true);
			if (ret)
				dev_err(&hdev->pdev->dev,
					"PF failed(%d) to get Q info for VF\n",
					ret);
			break;
		case HCLGE_MBX_GET_TCINFO:
			ret = hclge_get_vf_tcinfo(vport, req, true);
			if (ret)
				dev_err(&hdev->pdev->dev,
					"PF failed(%d) to get TC info for VF\n",
					ret);
			break;
		case HCLGE_MBX_GET_LINK_STATUS:
			ret = hclge_get_link_info(vport, req);
			if (ret)
				dev_err(&hdev->pdev->dev,
					"PF fail(%d) to get link stat for VF\n",
					ret);
			break;
		case HCLGE_MBX_QUEUE_RESET:
			hclge_mbx_reset_vf_queue(vport, req);
			break;
		case HCLGE_MBX_RESET:
			hclge_reset_vf(vport, req);
			break;
		default:
			dev_err(&hdev->pdev->dev,
				"un-supported mailbox message, code = %d\n",
				req->msg[0]);
			break;
		}
		crq->desc[crq->next_to_use].flag = 0;
		hclge_mbx_ring_ptr_move_crq(crq);
	}

	/* Write back CMDQ_RQ header pointer, M7 need this pointer */
	hclge_write_dev(&hdev->hw, HCLGE_NIC_CRQ_HEAD_REG, crq->next_to_use);
}
