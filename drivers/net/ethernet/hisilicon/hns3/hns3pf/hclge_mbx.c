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
			"PF fail to gen resp to VF len %u exceeds max len %u\n",
			resp_data_len,
			HCLGE_MBX_MAX_RESP_DATA_SIZE);
		/* If resp_data_len is too long, set the value to max length
		 * and return the msg to VF
		 */
		resp_data_len = HCLGE_MBX_MAX_RESP_DATA_SIZE;
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

int hclge_inform_reset_assert_to_vf(struct hclge_vport *vport)
{
	struct hclge_dev *hdev = vport->back;
	u16 reset_type;
	u8 msg_data[2];
	u8 dest_vfid;

	BUILD_BUG_ON(HNAE3_MAX_RESET > U16_MAX);

	dest_vfid = (u8)vport->vport_id;

	if (hdev->reset_type == HNAE3_FUNC_RESET)
		reset_type = HNAE3_VF_PF_FUNC_RESET;
	else if (hdev->reset_type == HNAE3_FLR_RESET)
		reset_type = HNAE3_VF_FULL_RESET;
	else
		reset_type = HNAE3_VF_FUNC_RESET;

	memcpy(&msg_data[0], &reset_type, sizeof(u16));

	/* send this requested info to VF */
	return hclge_send_mbx_msg(vport, msg_data, sizeof(msg_data),
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

	hclge_free_vector_ring_chain(&ring_chain);

	return ret;
}

static int hclge_set_vf_promisc_mode(struct hclge_vport *vport,
				     struct hclge_mbx_vf_to_pf_cmd *req)
{
#define HCLGE_MBX_BC_INDEX	1
#define HCLGE_MBX_UC_INDEX	2
#define HCLGE_MBX_MC_INDEX	3

	bool en_bc = req->msg[HCLGE_MBX_BC_INDEX] ? true : false;
	bool en_uc = req->msg[HCLGE_MBX_UC_INDEX] ? true : false;
	bool en_mc = req->msg[HCLGE_MBX_MC_INDEX] ? true : false;
	int ret;

	if (!vport->vf_info.trusted) {
		en_uc = false;
		en_mc = false;
	}

	ret = hclge_set_vport_promisc_mode(vport, en_uc, en_mc, en_bc);
	if (req->mbx_need_resp)
		hclge_gen_resp_to_vf(vport, req, ret, NULL, 0);

	vport->vf_info.promisc_enable = (en_uc || en_mc) ? 1 : 0;

	return ret;
}

void hclge_inform_vf_promisc_info(struct hclge_vport *vport)
{
	u8 dest_vfid = (u8)vport->vport_id;
	u8 msg_data[2];

	memcpy(&msg_data[0], &vport->vf_info.promisc_enable, sizeof(u16));

	hclge_send_mbx_msg(vport, msg_data, sizeof(msg_data),
			   HCLGE_MBX_PUSH_PROMISC_INFO, dest_vfid);
}

static int hclge_set_vf_uc_mac_addr(struct hclge_vport *vport,
				    struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
	const u8 *mac_addr = (const u8 *)(&mbx_req->msg[2]);
	struct hclge_dev *hdev = vport->back;
	int status;

	if (mbx_req->msg[1] == HCLGE_MBX_MAC_VLAN_UC_MODIFY) {
		const u8 *old_addr = (const u8 *)(&mbx_req->msg[8]);

		/* If VF MAC has been configured by the host then it
		 * cannot be overridden by the MAC specified by the VM.
		 */
		if (!is_zero_ether_addr(vport->vf_info.mac) &&
		    !ether_addr_equal(mac_addr, vport->vf_info.mac)) {
			status = -EPERM;
			goto out;
		}

		if (!is_valid_ether_addr(mac_addr)) {
			status = -EINVAL;
			goto out;
		}

		hclge_rm_uc_addr_common(vport, old_addr);
		status = hclge_add_uc_addr_common(vport, mac_addr);
		if (status) {
			hclge_add_uc_addr_common(vport, old_addr);
		} else {
			hclge_rm_vport_mac_table(vport, mac_addr,
						 false, HCLGE_MAC_ADDR_UC);
			hclge_add_vport_mac_table(vport, mac_addr,
						  HCLGE_MAC_ADDR_UC);
		}
	} else if (mbx_req->msg[1] == HCLGE_MBX_MAC_VLAN_UC_ADD) {
		status = hclge_add_uc_addr_common(vport, mac_addr);
		if (!status)
			hclge_add_vport_mac_table(vport, mac_addr,
						  HCLGE_MAC_ADDR_UC);
	} else if (mbx_req->msg[1] == HCLGE_MBX_MAC_VLAN_UC_REMOVE) {
		status = hclge_rm_uc_addr_common(vport, mac_addr);
		if (!status)
			hclge_rm_vport_mac_table(vport, mac_addr,
						 false, HCLGE_MAC_ADDR_UC);
	} else {
		dev_err(&hdev->pdev->dev,
			"failed to set unicast mac addr, unknown subcode %u\n",
			mbx_req->msg[1]);
		return -EIO;
	}

out:
	if (mbx_req->mbx_need_resp & HCLGE_MBX_NEED_RESP_BIT)
		hclge_gen_resp_to_vf(vport, mbx_req, status, NULL, 0);

	return 0;
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
		if (!status)
			hclge_add_vport_mac_table(vport, mac_addr,
						  HCLGE_MAC_ADDR_MC);
	} else if (mbx_req->msg[1] == HCLGE_MBX_MAC_VLAN_MC_REMOVE) {
		status = hclge_rm_mc_addr_common(vport, mac_addr);
		if (!status)
			hclge_rm_vport_mac_table(vport, mac_addr,
						 false, HCLGE_MAC_ADDR_MC);
	} else {
		dev_err(&hdev->pdev->dev,
			"failed to set mcast mac addr, unknown subcode %u\n",
			mbx_req->msg[1]);
		return -EIO;
	}

	if (gen_resp)
		hclge_gen_resp_to_vf(vport, mbx_req, status,
				     &resp_data, resp_len);

	return 0;
}

int hclge_push_vf_port_base_vlan_info(struct hclge_vport *vport, u8 vfid,
				      u16 state, u16 vlan_tag, u16 qos,
				      u16 vlan_proto)
{
#define MSG_DATA_SIZE	8

	u8 msg_data[MSG_DATA_SIZE];

	memcpy(&msg_data[0], &state, sizeof(u16));
	memcpy(&msg_data[2], &vlan_proto, sizeof(u16));
	memcpy(&msg_data[4], &qos, sizeof(u16));
	memcpy(&msg_data[6], &vlan_tag, sizeof(u16));

	return hclge_send_mbx_msg(vport, msg_data, sizeof(msg_data),
				  HCLGE_MBX_PUSH_VLAN_INFO, vfid);
}

static int hclge_set_vf_vlan_cfg(struct hclge_vport *vport,
				 struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
	struct hclge_vf_vlan_cfg *msg_cmd;
	int status = 0;

	msg_cmd = (struct hclge_vf_vlan_cfg *)mbx_req->msg;
	if (msg_cmd->subcode == HCLGE_MBX_VLAN_FILTER) {
		struct hnae3_handle *handle = &vport->nic;
		u16 vlan, proto;
		bool is_kill;

		is_kill = !!msg_cmd->is_kill;
		vlan =  msg_cmd->vlan;
		proto =  msg_cmd->proto;
		status = hclge_set_vlan_filter(handle, cpu_to_be16(proto),
					       vlan, is_kill);
		if (mbx_req->mbx_need_resp)
			return hclge_gen_resp_to_vf(vport, mbx_req, status,
						    NULL, 0);
	} else if (msg_cmd->subcode == HCLGE_MBX_VLAN_RX_OFF_CFG) {
		struct hnae3_handle *handle = &vport->nic;
		bool en = msg_cmd->is_kill ? true : false;

		status = hclge_en_hw_strip_rxvtag(handle, en);
	} else if (mbx_req->msg[1] == HCLGE_MBX_PORT_BASE_VLAN_CFG) {
		struct hclge_vlan_info *vlan_info;
		u16 *state;

		state = (u16 *)&mbx_req->msg[2];
		vlan_info = (struct hclge_vlan_info *)&mbx_req->msg[4];
		status = hclge_update_port_base_vlan_cfg(vport, *state,
							 vlan_info);
	} else if (mbx_req->msg[1] == HCLGE_MBX_GET_PORT_BASE_VLAN_STATE) {
		u8 state;

		state = vport->port_base_vlan_cfg.state;
		status = hclge_gen_resp_to_vf(vport, mbx_req, 0, &state,
					      sizeof(u8));
	}

	return status;
}

static int hclge_set_vf_alive(struct hclge_vport *vport,
			      struct hclge_mbx_vf_to_pf_cmd *mbx_req,
			      bool gen_resp)
{
	bool alive = !!mbx_req->msg[2];
	int ret = 0;

	if (alive)
		ret = hclge_vport_start(vport);
	else
		hclge_vport_stop(vport);

	return ret;
}

static int hclge_get_vf_tcinfo(struct hclge_vport *vport,
			       struct hclge_mbx_vf_to_pf_cmd *mbx_req,
			       bool gen_resp)
{
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	u8 vf_tc_map = 0;
	unsigned int i;
	int ret;

	for (i = 0; i < kinfo->num_tc; i++)
		vf_tc_map |= BIT(i);

	ret = hclge_gen_resp_to_vf(vport, mbx_req, 0, &vf_tc_map,
				   sizeof(vf_tc_map));

	return ret;
}

static int hclge_get_vf_queue_info(struct hclge_vport *vport,
				   struct hclge_mbx_vf_to_pf_cmd *mbx_req,
				   bool gen_resp)
{
#define HCLGE_TQPS_RSS_INFO_LEN		6
	u8 resp_data[HCLGE_TQPS_RSS_INFO_LEN];
	struct hclge_dev *hdev = vport->back;

	/* get the queue related info */
	memcpy(&resp_data[0], &vport->alloc_tqps, sizeof(u16));
	memcpy(&resp_data[2], &vport->nic.kinfo.rss_size, sizeof(u16));
	memcpy(&resp_data[4], &hdev->rx_buf_len, sizeof(u16));

	return hclge_gen_resp_to_vf(vport, mbx_req, 0, resp_data,
				    HCLGE_TQPS_RSS_INFO_LEN);
}

static int hclge_get_vf_mac_addr(struct hclge_vport *vport,
				 struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
	return hclge_gen_resp_to_vf(vport, mbx_req, 0, vport->vf_info.mac,
				    ETH_ALEN);
}

static int hclge_get_vf_queue_depth(struct hclge_vport *vport,
				    struct hclge_mbx_vf_to_pf_cmd *mbx_req,
				    bool gen_resp)
{
#define HCLGE_TQPS_DEPTH_INFO_LEN	4
	u8 resp_data[HCLGE_TQPS_DEPTH_INFO_LEN];
	struct hclge_dev *hdev = vport->back;

	/* get the queue depth info */
	memcpy(&resp_data[0], &hdev->num_tx_desc, sizeof(u16));
	memcpy(&resp_data[2], &hdev->num_rx_desc, sizeof(u16));
	return hclge_gen_resp_to_vf(vport, mbx_req, 0, resp_data,
				    HCLGE_TQPS_DEPTH_INFO_LEN);
}

static int hclge_get_vf_media_type(struct hclge_vport *vport,
				   struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
	struct hclge_dev *hdev = vport->back;
	u8 resp_data[2];

	resp_data[0] = hdev->hw.mac.media_type;
	resp_data[1] = hdev->hw.mac.module_type;
	return hclge_gen_resp_to_vf(vport, mbx_req, 0, resp_data,
				    sizeof(resp_data));
}

static int hclge_get_link_info(struct hclge_vport *vport,
			       struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
#define HCLGE_VF_LINK_STATE_UP		1U
#define HCLGE_VF_LINK_STATE_DOWN	0U

	struct hclge_dev *hdev = vport->back;
	u16 link_status;
	u8 msg_data[8];
	u8 dest_vfid;
	u16 duplex;

	/* mac.link can only be 0 or 1 */
	switch (vport->vf_info.link_state) {
	case IFLA_VF_LINK_STATE_ENABLE:
		link_status = HCLGE_VF_LINK_STATE_UP;
		break;
	case IFLA_VF_LINK_STATE_DISABLE:
		link_status = HCLGE_VF_LINK_STATE_DOWN;
		break;
	case IFLA_VF_LINK_STATE_AUTO:
	default:
		link_status = (u16)hdev->hw.mac.link;
		break;
	}

	duplex = hdev->hw.mac.duplex;
	memcpy(&msg_data[0], &link_status, sizeof(u16));
	memcpy(&msg_data[2], &hdev->hw.mac.speed, sizeof(u32));
	memcpy(&msg_data[6], &duplex, sizeof(u16));
	dest_vfid = mbx_req->mbx_src_vfid;

	/* send this requested info to VF */
	return hclge_send_mbx_msg(vport, msg_data, sizeof(msg_data),
				  HCLGE_MBX_LINK_STAT_CHANGE, dest_vfid);
}

static void hclge_get_link_mode(struct hclge_vport *vport,
				struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
#define HCLGE_SUPPORTED   1
	struct hclge_dev *hdev = vport->back;
	unsigned long advertising;
	unsigned long supported;
	unsigned long send_data;
	u8 msg_data[10];
	u8 dest_vfid;

	advertising = hdev->hw.mac.advertising[0];
	supported = hdev->hw.mac.supported[0];
	dest_vfid = mbx_req->mbx_src_vfid;
	msg_data[0] = mbx_req->msg[2];

	send_data = msg_data[0] == HCLGE_SUPPORTED ? supported : advertising;

	memcpy(&msg_data[2], &send_data, sizeof(unsigned long));
	hclge_send_mbx_msg(vport, msg_data, sizeof(msg_data),
			   HCLGE_MBX_LINK_STAT_MODE, dest_vfid);
}

static void hclge_mbx_reset_vf_queue(struct hclge_vport *vport,
				     struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
	u16 queue_id;

	memcpy(&queue_id, &mbx_req->msg[2], sizeof(queue_id));

	hclge_reset_vf_queue(vport, queue_id);

	/* send response msg to VF after queue reset complete */
	hclge_gen_resp_to_vf(vport, mbx_req, 0, NULL, 0);
}

static void hclge_reset_vf(struct hclge_vport *vport,
			   struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
	struct hclge_dev *hdev = vport->back;
	int ret;

	dev_warn(&hdev->pdev->dev, "PF received VF reset request from VF %u!",
		 vport->vport_id);

	ret = hclge_func_reset_cmd(hdev, vport->vport_id);
	hclge_gen_resp_to_vf(vport, mbx_req, ret, NULL, 0);
}

static void hclge_vf_keep_alive(struct hclge_vport *vport,
				struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
	vport->last_active_jiffies = jiffies;
}

static int hclge_set_vf_mtu(struct hclge_vport *vport,
			    struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
	int ret;
	u32 mtu;

	memcpy(&mtu, &mbx_req->msg[2], sizeof(mtu));
	ret = hclge_set_vport_mtu(vport, mtu);

	return hclge_gen_resp_to_vf(vport, mbx_req, ret, NULL, 0);
}

static int hclge_get_queue_id_in_pf(struct hclge_vport *vport,
				    struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
	u16 queue_id, qid_in_pf;
	u8 resp_data[2];

	memcpy(&queue_id, &mbx_req->msg[2], sizeof(queue_id));
	qid_in_pf = hclge_covert_handle_qid_global(&vport->nic, queue_id);
	memcpy(resp_data, &qid_in_pf, sizeof(qid_in_pf));

	return hclge_gen_resp_to_vf(vport, mbx_req, 0, resp_data,
				    sizeof(resp_data));
}

static int hclge_get_rss_key(struct hclge_vport *vport,
			     struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
#define HCLGE_RSS_MBX_RESP_LEN	8
	u8 resp_data[HCLGE_RSS_MBX_RESP_LEN];
	struct hclge_dev *hdev = vport->back;
	u8 index;

	index = mbx_req->msg[2];

	memcpy(&resp_data[0],
	       &hdev->vport[0].rss_hash_key[index * HCLGE_RSS_MBX_RESP_LEN],
	       HCLGE_RSS_MBX_RESP_LEN);

	return hclge_gen_resp_to_vf(vport, mbx_req, 0, resp_data,
				    HCLGE_RSS_MBX_RESP_LEN);
}

static void hclge_link_fail_parse(struct hclge_dev *hdev, u8 link_fail_code)
{
	switch (link_fail_code) {
	case HCLGE_LF_REF_CLOCK_LOST:
		dev_warn(&hdev->pdev->dev, "Reference clock lost!\n");
		break;
	case HCLGE_LF_XSFP_TX_DISABLE:
		dev_warn(&hdev->pdev->dev, "SFP tx is disabled!\n");
		break;
	case HCLGE_LF_XSFP_ABSENT:
		dev_warn(&hdev->pdev->dev, "SFP is absent!\n");
		break;
	default:
		break;
	}
}

static void hclge_handle_link_change_event(struct hclge_dev *hdev,
					   struct hclge_mbx_vf_to_pf_cmd *req)
{
#define LINK_STATUS_OFFSET	1
#define LINK_FAIL_CODE_OFFSET	2

	hclge_task_schedule(hdev, 0);

	if (!req->msg[LINK_STATUS_OFFSET])
		hclge_link_fail_parse(hdev, req->msg[LINK_FAIL_CODE_OFFSET]);
}

static bool hclge_cmd_crq_empty(struct hclge_hw *hw)
{
	u32 tail = hclge_read_dev(hw, HCLGE_NIC_CRQ_TAIL_REG);

	return tail == hw->cmq.crq.next_to_use;
}

static void hclge_handle_ncsi_error(struct hclge_dev *hdev)
{
	struct hnae3_ae_dev *ae_dev = hdev->ae_dev;

	ae_dev->ops->set_default_reset_request(ae_dev, HNAE3_GLOBAL_RESET);
	dev_warn(&hdev->pdev->dev, "requesting reset due to NCSI error\n");
	ae_dev->ops->reset_event(hdev->pdev, NULL);
}

void hclge_mbx_handler(struct hclge_dev *hdev)
{
	struct hclge_cmq_ring *crq = &hdev->hw.cmq.crq;
	struct hclge_mbx_vf_to_pf_cmd *req;
	struct hclge_vport *vport;
	struct hclge_desc *desc;
	unsigned int flag;
	int ret;

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
				 "dropped invalid mailbox message, code = %u\n",
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
			ret = hclge_set_vf_uc_mac_addr(vport, req);
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
			ret = hclge_set_vf_vlan_cfg(vport, req);
			if (ret)
				dev_err(&hdev->pdev->dev,
					"PF failed(%d) to config VF's VLAN\n",
					ret);
			break;
		case HCLGE_MBX_SET_ALIVE:
			ret = hclge_set_vf_alive(vport, req, false);
			if (ret)
				dev_err(&hdev->pdev->dev,
					"PF failed(%d) to set VF's ALIVE\n",
					ret);
			break;
		case HCLGE_MBX_GET_QINFO:
			ret = hclge_get_vf_queue_info(vport, req, true);
			if (ret)
				dev_err(&hdev->pdev->dev,
					"PF failed(%d) to get Q info for VF\n",
					ret);
			break;
		case HCLGE_MBX_GET_QDEPTH:
			ret = hclge_get_vf_queue_depth(vport, req, true);
			if (ret)
				dev_err(&hdev->pdev->dev,
					"PF failed(%d) to get Q depth for VF\n",
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
		case HCLGE_MBX_KEEP_ALIVE:
			hclge_vf_keep_alive(vport, req);
			break;
		case HCLGE_MBX_SET_MTU:
			ret = hclge_set_vf_mtu(vport, req);
			if (ret)
				dev_err(&hdev->pdev->dev,
					"VF fail(%d) to set mtu\n", ret);
			break;
		case HCLGE_MBX_GET_QID_IN_PF:
			ret = hclge_get_queue_id_in_pf(vport, req);
			if (ret)
				dev_err(&hdev->pdev->dev,
					"PF failed(%d) to get qid for VF\n",
					ret);
			break;
		case HCLGE_MBX_GET_RSS_KEY:
			ret = hclge_get_rss_key(vport, req);
			if (ret)
				dev_err(&hdev->pdev->dev,
					"PF fail(%d) to get rss key for VF\n",
					ret);
			break;
		case HCLGE_MBX_GET_LINK_MODE:
			hclge_get_link_mode(vport, req);
			break;
		case HCLGE_MBX_GET_VF_FLR_STATUS:
			hclge_rm_vport_all_mac_table(vport, true,
						     HCLGE_MAC_ADDR_UC);
			hclge_rm_vport_all_mac_table(vport, true,
						     HCLGE_MAC_ADDR_MC);
			hclge_rm_vport_all_vlan_table(vport, true);
			break;
		case HCLGE_MBX_GET_MEDIA_TYPE:
			ret = hclge_get_vf_media_type(vport, req);
			if (ret)
				dev_err(&hdev->pdev->dev,
					"PF fail(%d) to media type for VF\n",
					ret);
			break;
		case HCLGE_MBX_PUSH_LINK_STATUS:
			hclge_handle_link_change_event(hdev, req);
			break;
		case HCLGE_MBX_GET_MAC_ADDR:
			ret = hclge_get_vf_mac_addr(vport, req);
			if (ret)
				dev_err(&hdev->pdev->dev,
					"PF failed(%d) to get MAC for VF\n",
					ret);
			break;
		case HCLGE_MBX_NCSI_ERROR:
			hclge_handle_ncsi_error(hdev);
			break;
		default:
			dev_err(&hdev->pdev->dev,
				"un-supported mailbox message, code = %u\n",
				req->msg[0]);
			break;
		}
		crq->desc[crq->next_to_use].flag = 0;
		hclge_mbx_ring_ptr_move_crq(crq);
	}

	/* Write back CMDQ_RQ header pointer, M7 need this pointer */
	hclge_write_dev(&hdev->hw, HCLGE_NIC_CRQ_HEAD_REG, crq->next_to_use);
}
