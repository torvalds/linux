// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2016-2017 Hisilicon Limited.

#include "hclge_main.h"
#include "hclge_mbx.h"
#include "hnae3.h"
#include "hclge_comm_rss.h"

#define CREATE_TRACE_POINTS
#include "hclge_trace.h"

static u16 hclge_errno_to_resp(int errno)
{
	int resp = abs(errno);

	/* The status for pf to vf msg cmd is u16, constrainted by HW.
	 * We need to keep the same type with it.
	 * The intput errno is the stander error code, it's safely to
	 * use a u16 to store the abs(errno).
	 */
	return (u16)resp;
}

/* hclge_gen_resp_to_vf: used to generate a synchronous response to VF when PF
 * receives a mailbox message from VF.
 * @vport: pointer to struct hclge_vport
 * @vf_to_pf_req: pointer to hclge_mbx_vf_to_pf_cmd of the original mailbox
 *		  message
 * @resp_status: indicate to VF whether its request success(0) or failed.
 */
static int hclge_gen_resp_to_vf(struct hclge_vport *vport,
				struct hclge_mbx_vf_to_pf_cmd *vf_to_pf_req,
				struct hclge_respond_to_vf_msg *resp_msg)
{
	struct hclge_mbx_pf_to_vf_cmd *resp_pf_to_vf;
	struct hclge_dev *hdev = vport->back;
	enum hclge_comm_cmd_status status;
	struct hclge_desc desc;
	u16 resp;

	resp_pf_to_vf = (struct hclge_mbx_pf_to_vf_cmd *)desc.data;

	if (resp_msg->len > HCLGE_MBX_MAX_RESP_DATA_SIZE) {
		dev_err(&hdev->pdev->dev,
			"PF fail to gen resp to VF len %u exceeds max len %u\n",
			resp_msg->len,
			HCLGE_MBX_MAX_RESP_DATA_SIZE);
		/* If resp_msg->len is too long, set the value to max length
		 * and return the msg to VF
		 */
		resp_msg->len = HCLGE_MBX_MAX_RESP_DATA_SIZE;
	}

	hclge_cmd_setup_basic_desc(&desc, HCLGEVF_OPC_MBX_PF_TO_VF, false);

	resp_pf_to_vf->dest_vfid = vf_to_pf_req->mbx_src_vfid;
	resp_pf_to_vf->msg_len = vf_to_pf_req->msg_len;
	resp_pf_to_vf->match_id = vf_to_pf_req->match_id;

	resp_pf_to_vf->msg.code = cpu_to_le16(HCLGE_MBX_PF_VF_RESP);
	resp_pf_to_vf->msg.vf_mbx_msg_code =
				cpu_to_le16(vf_to_pf_req->msg.code);
	resp_pf_to_vf->msg.vf_mbx_msg_subcode =
				cpu_to_le16(vf_to_pf_req->msg.subcode);
	resp = hclge_errno_to_resp(resp_msg->status);
	if (resp < SHRT_MAX) {
		resp_pf_to_vf->msg.resp_status = cpu_to_le16(resp);
	} else {
		dev_warn(&hdev->pdev->dev,
			 "failed to send response to VF, response status %u is out-of-bound\n",
			 resp);
		resp_pf_to_vf->msg.resp_status = cpu_to_le16(EIO);
	}

	if (resp_msg->len > 0)
		memcpy(resp_pf_to_vf->msg.resp_data, resp_msg->data,
		       resp_msg->len);

	trace_hclge_pf_mbx_send(hdev, resp_pf_to_vf);

	status = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (status)
		dev_err(&hdev->pdev->dev,
			"failed to send response to VF, status: %d, vfid: %u, code: %u, subcode: %u.\n",
			status, vf_to_pf_req->mbx_src_vfid,
			vf_to_pf_req->msg.code, vf_to_pf_req->msg.subcode);

	return status;
}

static int hclge_send_mbx_msg(struct hclge_vport *vport, u8 *msg, u16 msg_len,
			      u16 mbx_opcode, u8 dest_vfid)
{
	struct hclge_mbx_pf_to_vf_cmd *resp_pf_to_vf;
	struct hclge_dev *hdev = vport->back;
	enum hclge_comm_cmd_status status;
	struct hclge_desc desc;

	if (msg_len > HCLGE_MBX_MAX_MSG_SIZE) {
		dev_err(&hdev->pdev->dev,
			"msg data length(=%u) exceeds maximum(=%u)\n",
			msg_len, HCLGE_MBX_MAX_MSG_SIZE);
		return -EMSGSIZE;
	}

	resp_pf_to_vf = (struct hclge_mbx_pf_to_vf_cmd *)desc.data;

	hclge_cmd_setup_basic_desc(&desc, HCLGEVF_OPC_MBX_PF_TO_VF, false);

	resp_pf_to_vf->dest_vfid = dest_vfid;
	resp_pf_to_vf->msg_len = msg_len;
	resp_pf_to_vf->msg.code = cpu_to_le16(mbx_opcode);

	memcpy(resp_pf_to_vf->msg.msg_data, msg, msg_len);

	trace_hclge_pf_mbx_send(hdev, resp_pf_to_vf);

	status = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (status)
		dev_err(&hdev->pdev->dev,
			"failed to send mailbox to VF, status: %d, vfid: %u, opcode: %u\n",
			status, dest_vfid, mbx_opcode);

	return status;
}

int hclge_inform_vf_reset(struct hclge_vport *vport, u16 reset_type)
{
	__le16 msg_data;
	u8 dest_vfid;

	dest_vfid = (u8)vport->vport_id;
	msg_data = cpu_to_le16(reset_type);

	/* send this requested info to VF */
	return hclge_send_mbx_msg(vport, (u8 *)&msg_data, sizeof(msg_data),
				  HCLGE_MBX_ASSERTING_RESET, dest_vfid);
}

int hclge_inform_reset_assert_to_vf(struct hclge_vport *vport)
{
	struct hclge_dev *hdev = vport->back;
	u16 reset_type;

	BUILD_BUG_ON(HNAE3_MAX_RESET > U16_MAX);

	if (hdev->reset_type == HNAE3_FUNC_RESET)
		reset_type = HNAE3_VF_PF_FUNC_RESET;
	else if (hdev->reset_type == HNAE3_FLR_RESET)
		reset_type = HNAE3_VF_FULL_RESET;
	else
		reset_type = HNAE3_VF_FUNC_RESET;

	return hclge_inform_vf_reset(vport, reset_type);
}

static void hclge_free_vector_ring_chain(struct hnae3_ring_chain_node *head)
{
	struct hnae3_ring_chain_node *chain_tmp, *chain;

	chain = head->next;

	while (chain) {
		chain_tmp = chain->next;
		kfree_sensitive(chain);
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
	struct hclge_dev *hdev = vport->back;
	int ring_num;
	int i;

	ring_num = req->msg.ring_num;

	if (ring_num > HCLGE_MBX_MAX_RING_CHAIN_PARAM_NUM)
		return -EINVAL;

	for (i = 0; i < ring_num; i++) {
		if (req->msg.param[i].tqp_index >= vport->nic.kinfo.rss_size) {
			dev_err(&hdev->pdev->dev, "tqp index(%u) is out of range(0-%u)\n",
				req->msg.param[i].tqp_index,
				vport->nic.kinfo.rss_size - 1U);
			return -EINVAL;
		}
	}

	hnae3_set_bit(ring_chain->flag, HNAE3_RING_TYPE_B,
		      req->msg.param[0].ring_type);
	ring_chain->tqp_index =
		hclge_get_queue_id(vport->nic.kinfo.tqp
				   [req->msg.param[0].tqp_index]);
	hnae3_set_field(ring_chain->int_gl_idx, HNAE3_RING_GL_IDX_M,
			HNAE3_RING_GL_IDX_S, req->msg.param[0].int_gl_index);

	cur_chain = ring_chain;

	for (i = 1; i < ring_num; i++) {
		new_chain = kzalloc(sizeof(*new_chain), GFP_KERNEL);
		if (!new_chain)
			goto err;

		hnae3_set_bit(new_chain->flag, HNAE3_RING_TYPE_B,
			      req->msg.param[i].ring_type);

		new_chain->tqp_index =
		hclge_get_queue_id(vport->nic.kinfo.tqp
			[req->msg.param[i].tqp_index]);

		hnae3_set_field(new_chain->int_gl_idx, HNAE3_RING_GL_IDX_M,
				HNAE3_RING_GL_IDX_S,
				req->msg.param[i].int_gl_index);

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
	int vector_id = req->msg.vector_id;
	int ret;

	memset(&ring_chain, 0, sizeof(ring_chain));
	ret = hclge_get_ring_chain_from_mbx(req, &ring_chain, vport);
	if (ret)
		return ret;

	ret = hclge_bind_ring_with_vector(vport, vector_id, en, &ring_chain);

	hclge_free_vector_ring_chain(&ring_chain);

	return ret;
}

static int hclge_query_ring_vector_map(struct hclge_vport *vport,
				       struct hnae3_ring_chain_node *ring_chain,
				       struct hclge_desc *desc)
{
	struct hclge_ctrl_vector_chain_cmd *req =
		(struct hclge_ctrl_vector_chain_cmd *)desc->data;
	struct hclge_dev *hdev = vport->back;
	u16 tqp_type_and_id;
	int status;

	hclge_cmd_setup_basic_desc(desc, HCLGE_OPC_ADD_RING_TO_VECTOR, true);

	tqp_type_and_id = le16_to_cpu(req->tqp_type_and_id[0]);
	hnae3_set_field(tqp_type_and_id, HCLGE_INT_TYPE_M, HCLGE_INT_TYPE_S,
			hnae3_get_bit(ring_chain->flag, HNAE3_RING_TYPE_B));
	hnae3_set_field(tqp_type_and_id, HCLGE_TQP_ID_M, HCLGE_TQP_ID_S,
			ring_chain->tqp_index);
	req->tqp_type_and_id[0] = cpu_to_le16(tqp_type_and_id);
	req->vfid = vport->vport_id;

	status = hclge_cmd_send(&hdev->hw, desc, 1);
	if (status)
		dev_err(&hdev->pdev->dev,
			"Get VF ring vector map info fail, status is %d.\n",
			status);

	return status;
}

static int hclge_get_vf_ring_vector_map(struct hclge_vport *vport,
					struct hclge_mbx_vf_to_pf_cmd *req,
					struct hclge_respond_to_vf_msg *resp)
{
#define HCLGE_LIMIT_RING_NUM			1
#define HCLGE_RING_TYPE_OFFSET			0
#define HCLGE_TQP_INDEX_OFFSET			1
#define HCLGE_INT_GL_INDEX_OFFSET		2
#define HCLGE_VECTOR_ID_OFFSET			3
#define HCLGE_RING_VECTOR_MAP_INFO_LEN		4
	struct hnae3_ring_chain_node ring_chain;
	struct hclge_desc desc;
	struct hclge_ctrl_vector_chain_cmd *data =
		(struct hclge_ctrl_vector_chain_cmd *)desc.data;
	u16 tqp_type_and_id;
	u8 int_gl_index;
	int ret;

	req->msg.ring_num = HCLGE_LIMIT_RING_NUM;

	memset(&ring_chain, 0, sizeof(ring_chain));
	ret = hclge_get_ring_chain_from_mbx(req, &ring_chain, vport);
	if (ret)
		return ret;

	ret = hclge_query_ring_vector_map(vport, &ring_chain, &desc);
	if (ret) {
		hclge_free_vector_ring_chain(&ring_chain);
		return ret;
	}

	tqp_type_and_id = le16_to_cpu(data->tqp_type_and_id[0]);
	int_gl_index = hnae3_get_field(tqp_type_and_id,
				       HCLGE_INT_GL_IDX_M, HCLGE_INT_GL_IDX_S);

	resp->data[HCLGE_RING_TYPE_OFFSET] = req->msg.param[0].ring_type;
	resp->data[HCLGE_TQP_INDEX_OFFSET] = req->msg.param[0].tqp_index;
	resp->data[HCLGE_INT_GL_INDEX_OFFSET] = int_gl_index;
	resp->data[HCLGE_VECTOR_ID_OFFSET] = data->int_vector_id_l;
	resp->len = HCLGE_RING_VECTOR_MAP_INFO_LEN;

	hclge_free_vector_ring_chain(&ring_chain);

	return ret;
}

static void hclge_set_vf_promisc_mode(struct hclge_vport *vport,
				      struct hclge_mbx_vf_to_pf_cmd *req)
{
	struct hnae3_handle *handle = &vport->nic;
	struct hclge_dev *hdev = vport->back;

	vport->vf_info.request_uc_en = req->msg.en_uc;
	vport->vf_info.request_mc_en = req->msg.en_mc;
	vport->vf_info.request_bc_en = req->msg.en_bc;

	if (req->msg.en_limit_promisc)
		set_bit(HNAE3_PFLAG_LIMIT_PROMISC, &handle->priv_flags);
	else
		clear_bit(HNAE3_PFLAG_LIMIT_PROMISC,
			  &handle->priv_flags);

	set_bit(HCLGE_VPORT_STATE_PROMISC_CHANGE, &vport->state);
	hclge_task_schedule(hdev, 0);
}

static int hclge_set_vf_uc_mac_addr(struct hclge_vport *vport,
				    struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
#define HCLGE_MBX_VF_OLD_MAC_ADDR_OFFSET	6

	const u8 *mac_addr = (const u8 *)(mbx_req->msg.data);
	struct hclge_dev *hdev = vport->back;
	int status;

	if (mbx_req->msg.subcode == HCLGE_MBX_MAC_VLAN_UC_MODIFY) {
		const u8 *old_addr = (const u8 *)
		(&mbx_req->msg.data[HCLGE_MBX_VF_OLD_MAC_ADDR_OFFSET]);

		/* If VF MAC has been configured by the host then it
		 * cannot be overridden by the MAC specified by the VM.
		 */
		if (!is_zero_ether_addr(vport->vf_info.mac) &&
		    !ether_addr_equal(mac_addr, vport->vf_info.mac))
			return -EPERM;

		if (!is_valid_ether_addr(mac_addr))
			return -EINVAL;

		spin_lock_bh(&vport->mac_list_lock);
		status = hclge_update_mac_node_for_dev_addr(vport, old_addr,
							    mac_addr);
		spin_unlock_bh(&vport->mac_list_lock);
		hclge_task_schedule(hdev, 0);
	} else if (mbx_req->msg.subcode == HCLGE_MBX_MAC_VLAN_UC_ADD) {
		status = hclge_update_mac_list(vport, HCLGE_MAC_TO_ADD,
					       HCLGE_MAC_ADDR_UC, mac_addr);
	} else if (mbx_req->msg.subcode == HCLGE_MBX_MAC_VLAN_UC_REMOVE) {
		status = hclge_update_mac_list(vport, HCLGE_MAC_TO_DEL,
					       HCLGE_MAC_ADDR_UC, mac_addr);
	} else {
		dev_err(&hdev->pdev->dev,
			"failed to set unicast mac addr, unknown subcode %u\n",
			mbx_req->msg.subcode);
		return -EIO;
	}

	return status;
}

static int hclge_set_vf_mc_mac_addr(struct hclge_vport *vport,
				    struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
	const u8 *mac_addr = (const u8 *)(mbx_req->msg.data);
	struct hclge_dev *hdev = vport->back;

	if (mbx_req->msg.subcode == HCLGE_MBX_MAC_VLAN_MC_ADD) {
		hclge_update_mac_list(vport, HCLGE_MAC_TO_ADD,
				      HCLGE_MAC_ADDR_MC, mac_addr);
	} else if (mbx_req->msg.subcode == HCLGE_MBX_MAC_VLAN_MC_REMOVE) {
		hclge_update_mac_list(vport, HCLGE_MAC_TO_DEL,
				      HCLGE_MAC_ADDR_MC, mac_addr);
	} else {
		dev_err(&hdev->pdev->dev,
			"failed to set mcast mac addr, unknown subcode %u\n",
			mbx_req->msg.subcode);
		return -EIO;
	}

	return 0;
}

int hclge_push_vf_port_base_vlan_info(struct hclge_vport *vport, u8 vfid,
				      u16 state,
				      struct hclge_vlan_info *vlan_info)
{
	struct hclge_mbx_port_base_vlan base_vlan;

	base_vlan.state = cpu_to_le16(state);
	base_vlan.vlan_proto = cpu_to_le16(vlan_info->vlan_proto);
	base_vlan.qos = cpu_to_le16(vlan_info->qos);
	base_vlan.vlan_tag = cpu_to_le16(vlan_info->vlan_tag);

	return hclge_send_mbx_msg(vport, (u8 *)&base_vlan, sizeof(base_vlan),
				  HCLGE_MBX_PUSH_VLAN_INFO, vfid);
}

static int hclge_set_vf_vlan_cfg(struct hclge_vport *vport,
				 struct hclge_mbx_vf_to_pf_cmd *mbx_req,
				 struct hclge_respond_to_vf_msg *resp_msg)
{
#define HCLGE_MBX_VLAN_STATE_OFFSET	0
#define HCLGE_MBX_VLAN_INFO_OFFSET	2

	struct hnae3_handle *handle = &vport->nic;
	struct hclge_dev *hdev = vport->back;
	struct hclge_vf_vlan_cfg *msg_cmd;
	__be16 proto;
	u16 vlan_id;

	msg_cmd = (struct hclge_vf_vlan_cfg *)&mbx_req->msg;
	switch (msg_cmd->subcode) {
	case HCLGE_MBX_VLAN_FILTER:
		proto = cpu_to_be16(le16_to_cpu(msg_cmd->proto));
		vlan_id = le16_to_cpu(msg_cmd->vlan);
		return hclge_set_vlan_filter(handle, proto, vlan_id,
					     msg_cmd->is_kill);
	case HCLGE_MBX_VLAN_RX_OFF_CFG:
		return hclge_en_hw_strip_rxvtag(handle, msg_cmd->enable);
	case HCLGE_MBX_GET_PORT_BASE_VLAN_STATE:
		/* vf does not need to know about the port based VLAN state
		 * on device HNAE3_DEVICE_VERSION_V3. So always return disable
		 * on device HNAE3_DEVICE_VERSION_V3 if vf queries the port
		 * based VLAN state.
		 */
		resp_msg->data[0] =
			hdev->ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V3 ?
			HNAE3_PORT_BASE_VLAN_DISABLE :
			vport->port_base_vlan_cfg.state;
		resp_msg->len = sizeof(u8);
		return 0;
	case HCLGE_MBX_ENABLE_VLAN_FILTER:
		return hclge_enable_vport_vlan_filter(vport, msg_cmd->enable);
	default:
		return 0;
	}
}

static int hclge_set_vf_alive(struct hclge_vport *vport,
			      struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
	bool alive = !!mbx_req->msg.data[0];
	int ret = 0;

	if (alive)
		ret = hclge_vport_start(vport);
	else
		hclge_vport_stop(vport);

	return ret;
}

static void hclge_get_basic_info(struct hclge_vport *vport,
				 struct hclge_respond_to_vf_msg *resp_msg)
{
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hnae3_ae_dev *ae_dev = vport->back->ae_dev;
	struct hclge_basic_info *basic_info;
	unsigned int i;
	u32 pf_caps;

	basic_info = (struct hclge_basic_info *)resp_msg->data;
	for (i = 0; i < kinfo->tc_info.num_tc; i++)
		basic_info->hw_tc_map |= BIT(i);

	pf_caps = le32_to_cpu(basic_info->pf_caps);
	if (test_bit(HNAE3_DEV_SUPPORT_VLAN_FLTR_MDF_B, ae_dev->caps))
		hnae3_set_bit(pf_caps, HNAE3_PF_SUPPORT_VLAN_FLTR_MDF_B, 1);

	basic_info->pf_caps = cpu_to_le32(pf_caps);
	resp_msg->len = HCLGE_MBX_MAX_RESP_DATA_SIZE;
}

static void hclge_get_vf_queue_info(struct hclge_vport *vport,
				    struct hclge_respond_to_vf_msg *resp_msg)
{
#define HCLGE_TQPS_RSS_INFO_LEN		6

	struct hclge_mbx_vf_queue_info *queue_info;
	struct hclge_dev *hdev = vport->back;

	/* get the queue related info */
	queue_info = (struct hclge_mbx_vf_queue_info *)resp_msg->data;
	queue_info->num_tqps = cpu_to_le16(vport->alloc_tqps);
	queue_info->rss_size = cpu_to_le16(vport->nic.kinfo.rss_size);
	queue_info->rx_buf_len = cpu_to_le16(hdev->rx_buf_len);
	resp_msg->len = HCLGE_TQPS_RSS_INFO_LEN;
}

static void hclge_get_vf_mac_addr(struct hclge_vport *vport,
				  struct hclge_respond_to_vf_msg *resp_msg)
{
	ether_addr_copy(resp_msg->data, vport->vf_info.mac);
	resp_msg->len = ETH_ALEN;
}

static void hclge_get_vf_queue_depth(struct hclge_vport *vport,
				     struct hclge_respond_to_vf_msg *resp_msg)
{
#define HCLGE_TQPS_DEPTH_INFO_LEN	4

	struct hclge_mbx_vf_queue_depth *queue_depth;
	struct hclge_dev *hdev = vport->back;

	/* get the queue depth info */
	queue_depth = (struct hclge_mbx_vf_queue_depth *)resp_msg->data;
	queue_depth->num_tx_desc = cpu_to_le16(hdev->num_tx_desc);
	queue_depth->num_rx_desc = cpu_to_le16(hdev->num_rx_desc);

	resp_msg->len = HCLGE_TQPS_DEPTH_INFO_LEN;
}

static void hclge_get_vf_media_type(struct hclge_vport *vport,
				    struct hclge_respond_to_vf_msg *resp_msg)
{
#define HCLGE_VF_MEDIA_TYPE_OFFSET	0
#define HCLGE_VF_MODULE_TYPE_OFFSET	1
#define HCLGE_VF_MEDIA_TYPE_LENGTH	2

	struct hclge_dev *hdev = vport->back;

	resp_msg->data[HCLGE_VF_MEDIA_TYPE_OFFSET] =
		hdev->hw.mac.media_type;
	resp_msg->data[HCLGE_VF_MODULE_TYPE_OFFSET] =
		hdev->hw.mac.module_type;
	resp_msg->len = HCLGE_VF_MEDIA_TYPE_LENGTH;
}

int hclge_push_vf_link_status(struct hclge_vport *vport)
{
#define HCLGE_VF_LINK_STATE_UP		1U
#define HCLGE_VF_LINK_STATE_DOWN	0U

	struct hclge_mbx_link_status link_info;
	struct hclge_dev *hdev = vport->back;
	u16 link_status;

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

	link_info.link_status = cpu_to_le16(link_status);
	link_info.speed = cpu_to_le32(hdev->hw.mac.speed);
	link_info.duplex = cpu_to_le16(hdev->hw.mac.duplex);
	link_info.flag = HCLGE_MBX_PUSH_LINK_STATUS_EN;

	/* send this requested info to VF */
	return hclge_send_mbx_msg(vport, (u8 *)&link_info, sizeof(link_info),
				  HCLGE_MBX_LINK_STAT_CHANGE, vport->vport_id);
}

static void hclge_get_link_mode(struct hclge_vport *vport,
				struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
#define HCLGE_SUPPORTED   1
	struct hclge_mbx_link_mode link_mode;
	struct hclge_dev *hdev = vport->back;
	unsigned long advertising;
	unsigned long supported;
	unsigned long send_data;
	u8 dest_vfid;

	advertising = hdev->hw.mac.advertising[0];
	supported = hdev->hw.mac.supported[0];
	dest_vfid = mbx_req->mbx_src_vfid;
	send_data = mbx_req->msg.data[0] == HCLGE_SUPPORTED ? supported :
							      advertising;
	link_mode.idx = cpu_to_le16((u16)mbx_req->msg.data[0]);
	link_mode.link_mode = cpu_to_le64(send_data);

	hclge_send_mbx_msg(vport, (u8 *)&link_mode, sizeof(link_mode),
			   HCLGE_MBX_LINK_STAT_MODE, dest_vfid);
}

static int hclge_mbx_reset_vf_queue(struct hclge_vport *vport,
				    struct hclge_mbx_vf_to_pf_cmd *mbx_req,
				    struct hclge_respond_to_vf_msg *resp_msg)
{
#define HCLGE_RESET_ALL_QUEUE_DONE	1U
	struct hnae3_handle *handle = &vport->nic;
	struct hclge_dev *hdev = vport->back;
	u16 queue_id;
	int ret;

	queue_id = le16_to_cpu(*(__le16 *)mbx_req->msg.data);
	resp_msg->data[0] = HCLGE_RESET_ALL_QUEUE_DONE;
	resp_msg->len = sizeof(u8);

	/* pf will reset vf's all queues at a time. So it is unnecessary
	 * to reset queues if queue_id > 0, just return success.
	 */
	if (queue_id > 0)
		return 0;

	ret = hclge_reset_tqp(handle);
	if (ret)
		dev_err(&hdev->pdev->dev, "failed to reset vf %u queue, ret = %d\n",
			vport->vport_id - HCLGE_VF_VPORT_START_NUM, ret);

	return ret;
}

static int hclge_reset_vf(struct hclge_vport *vport)
{
	struct hclge_dev *hdev = vport->back;

	dev_warn(&hdev->pdev->dev, "PF received VF reset request from VF %u!",
		 vport->vport_id - HCLGE_VF_VPORT_START_NUM);

	return hclge_func_reset_cmd(hdev, vport->vport_id);
}

static void hclge_notify_vf_config(struct hclge_vport *vport)
{
	struct hclge_dev *hdev = vport->back;
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(hdev->pdev);
	struct hclge_port_base_vlan_config *vlan_cfg;
	int ret;

	hclge_push_vf_link_status(vport);
	if (test_bit(HCLGE_VPORT_NEED_NOTIFY_RESET, &vport->need_notify)) {
		ret = hclge_inform_vf_reset(vport, HNAE3_VF_PF_FUNC_RESET);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"failed to inform VF %u reset!",
				vport->vport_id - HCLGE_VF_VPORT_START_NUM);
			return;
		}
		vport->need_notify = 0;
		return;
	}

	if (ae_dev->dev_version < HNAE3_DEVICE_VERSION_V3 &&
	    test_bit(HCLGE_VPORT_NEED_NOTIFY_VF_VLAN, &vport->need_notify)) {
		vlan_cfg = &vport->port_base_vlan_cfg;
		ret = hclge_push_vf_port_base_vlan_info(&hdev->vport[0],
							vport->vport_id,
							vlan_cfg->state,
							&vlan_cfg->vlan_info);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"failed to inform VF %u port base vlan!",
				vport->vport_id - HCLGE_VF_VPORT_START_NUM);
			return;
		}
		clear_bit(HCLGE_VPORT_NEED_NOTIFY_VF_VLAN, &vport->need_notify);
	}
}

static void hclge_vf_keep_alive(struct hclge_vport *vport)
{
	struct hclge_dev *hdev = vport->back;

	vport->last_active_jiffies = jiffies;

	if (test_bit(HCLGE_VPORT_STATE_INITED, &vport->state) &&
	    !test_bit(HCLGE_VPORT_STATE_ALIVE, &vport->state)) {
		set_bit(HCLGE_VPORT_STATE_ALIVE, &vport->state);
		dev_info(&hdev->pdev->dev, "VF %u is alive!",
			 vport->vport_id - HCLGE_VF_VPORT_START_NUM);
		hclge_notify_vf_config(vport);
	}
}

static int hclge_set_vf_mtu(struct hclge_vport *vport,
			    struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
	struct hclge_mbx_mtu_info *mtu_info;
	u32 mtu;

	mtu_info = (struct hclge_mbx_mtu_info *)mbx_req->msg.data;
	mtu = le32_to_cpu(mtu_info->mtu);

	return hclge_set_vport_mtu(vport, mtu);
}

static int hclge_get_queue_id_in_pf(struct hclge_vport *vport,
				    struct hclge_mbx_vf_to_pf_cmd *mbx_req,
				    struct hclge_respond_to_vf_msg *resp_msg)
{
	struct hnae3_handle *handle = &vport->nic;
	struct hclge_dev *hdev = vport->back;
	u16 queue_id, qid_in_pf;

	queue_id = le16_to_cpu(*(__le16 *)mbx_req->msg.data);
	if (queue_id >= handle->kinfo.num_tqps) {
		dev_err(&hdev->pdev->dev, "Invalid queue id(%u) from VF %u\n",
			queue_id, mbx_req->mbx_src_vfid);
		return -EINVAL;
	}

	qid_in_pf = hclge_covert_handle_qid_global(&vport->nic, queue_id);
	*(__le16 *)resp_msg->data = cpu_to_le16(qid_in_pf);
	resp_msg->len = sizeof(qid_in_pf);
	return 0;
}

static int hclge_get_rss_key(struct hclge_vport *vport,
			     struct hclge_mbx_vf_to_pf_cmd *mbx_req,
			     struct hclge_respond_to_vf_msg *resp_msg)
{
#define HCLGE_RSS_MBX_RESP_LEN	8
	struct hclge_dev *hdev = vport->back;
	struct hclge_comm_rss_cfg *rss_cfg;
	u8 index;

	index = mbx_req->msg.data[0];
	rss_cfg = &hdev->rss_cfg;

	/* Check the query index of rss_hash_key from VF, make sure no
	 * more than the size of rss_hash_key.
	 */
	if (((index + 1) * HCLGE_RSS_MBX_RESP_LEN) >
	      sizeof(rss_cfg->rss_hash_key)) {
		dev_warn(&hdev->pdev->dev,
			 "failed to get the rss hash key, the index(%u) invalid !\n",
			 index);
		return -EINVAL;
	}

	memcpy(resp_msg->data,
	       &rss_cfg->rss_hash_key[index * HCLGE_RSS_MBX_RESP_LEN],
	       HCLGE_RSS_MBX_RESP_LEN);
	resp_msg->len = HCLGE_RSS_MBX_RESP_LEN;
	return 0;
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
	hclge_task_schedule(hdev, 0);

	if (!req->msg.subcode)
		hclge_link_fail_parse(hdev, req->msg.data[0]);
}

static bool hclge_cmd_crq_empty(struct hclge_hw *hw)
{
	u32 tail = hclge_read_dev(hw, HCLGE_COMM_NIC_CRQ_TAIL_REG);

	return tail == hw->hw.cmq.crq.next_to_use;
}

static void hclge_handle_ncsi_error(struct hclge_dev *hdev)
{
	struct hnae3_ae_dev *ae_dev = hdev->ae_dev;

	ae_dev->ops->set_default_reset_request(ae_dev, HNAE3_GLOBAL_RESET);
	dev_warn(&hdev->pdev->dev, "requesting reset due to NCSI error\n");
	ae_dev->ops->reset_event(hdev->pdev, NULL);
}

static void hclge_handle_vf_tbl(struct hclge_vport *vport,
				struct hclge_mbx_vf_to_pf_cmd *mbx_req)
{
	struct hclge_dev *hdev = vport->back;
	struct hclge_vf_vlan_cfg *msg_cmd;

	msg_cmd = (struct hclge_vf_vlan_cfg *)&mbx_req->msg;
	if (msg_cmd->subcode == HCLGE_MBX_VPORT_LIST_CLEAR) {
		hclge_rm_vport_all_mac_table(vport, true, HCLGE_MAC_ADDR_UC);
		hclge_rm_vport_all_mac_table(vport, true, HCLGE_MAC_ADDR_MC);
		hclge_rm_vport_all_vlan_table(vport, true);
	} else {
		dev_warn(&hdev->pdev->dev, "Invalid cmd(%u)\n",
			 msg_cmd->subcode);
	}
}

static int
hclge_mbx_map_ring_to_vector_handler(struct hclge_mbx_ops_param *param)
{
	return hclge_map_unmap_ring_to_vf_vector(param->vport, true,
						 param->req);
}

static int
hclge_mbx_unmap_ring_to_vector_handler(struct hclge_mbx_ops_param *param)
{
	return hclge_map_unmap_ring_to_vf_vector(param->vport, false,
						 param->req);
}

static int
hclge_mbx_get_ring_vector_map_handler(struct hclge_mbx_ops_param *param)
{
	int ret;

	ret = hclge_get_vf_ring_vector_map(param->vport, param->req,
					   param->resp_msg);
	if (ret)
		dev_err(&param->vport->back->pdev->dev,
			"PF fail(%d) to get VF ring vector map\n",
			ret);
	return ret;
}

static int hclge_mbx_set_promisc_mode_handler(struct hclge_mbx_ops_param *param)
{
	hclge_set_vf_promisc_mode(param->vport, param->req);
	return 0;
}

static int hclge_mbx_set_unicast_handler(struct hclge_mbx_ops_param *param)
{
	int ret;

	ret = hclge_set_vf_uc_mac_addr(param->vport, param->req);
	if (ret)
		dev_err(&param->vport->back->pdev->dev,
			"PF fail(%d) to set VF UC MAC Addr\n",
			ret);
	return ret;
}

static int hclge_mbx_set_multicast_handler(struct hclge_mbx_ops_param *param)
{
	int ret;

	ret = hclge_set_vf_mc_mac_addr(param->vport, param->req);
	if (ret)
		dev_err(&param->vport->back->pdev->dev,
			"PF fail(%d) to set VF MC MAC Addr\n",
			ret);
	return ret;
}

static int hclge_mbx_set_vlan_handler(struct hclge_mbx_ops_param *param)
{
	int ret;

	ret = hclge_set_vf_vlan_cfg(param->vport, param->req, param->resp_msg);
	if (ret)
		dev_err(&param->vport->back->pdev->dev,
			"PF failed(%d) to config VF's VLAN\n",
			ret);
	return ret;
}

static int hclge_mbx_set_alive_handler(struct hclge_mbx_ops_param *param)
{
	int ret;

	ret = hclge_set_vf_alive(param->vport, param->req);
	if (ret)
		dev_err(&param->vport->back->pdev->dev,
			"PF failed(%d) to set VF's ALIVE\n",
			ret);
	return ret;
}

static int hclge_mbx_get_qinfo_handler(struct hclge_mbx_ops_param *param)
{
	hclge_get_vf_queue_info(param->vport, param->resp_msg);
	return 0;
}

static int hclge_mbx_get_qdepth_handler(struct hclge_mbx_ops_param *param)
{
	hclge_get_vf_queue_depth(param->vport, param->resp_msg);
	return 0;
}

static int hclge_mbx_get_basic_info_handler(struct hclge_mbx_ops_param *param)
{
	hclge_get_basic_info(param->vport, param->resp_msg);
	return 0;
}

static int hclge_mbx_get_link_status_handler(struct hclge_mbx_ops_param *param)
{
	int ret;

	ret = hclge_push_vf_link_status(param->vport);
	if (ret)
		dev_err(&param->vport->back->pdev->dev,
			"failed to inform link stat to VF, ret = %d\n",
			ret);
	return ret;
}

static int hclge_mbx_queue_reset_handler(struct hclge_mbx_ops_param *param)
{
	return hclge_mbx_reset_vf_queue(param->vport, param->req,
					param->resp_msg);
}

static int hclge_mbx_reset_handler(struct hclge_mbx_ops_param *param)
{
	return hclge_reset_vf(param->vport);
}

static int hclge_mbx_keep_alive_handler(struct hclge_mbx_ops_param *param)
{
	hclge_vf_keep_alive(param->vport);
	return 0;
}

static int hclge_mbx_set_mtu_handler(struct hclge_mbx_ops_param *param)
{
	int ret;

	ret = hclge_set_vf_mtu(param->vport, param->req);
	if (ret)
		dev_err(&param->vport->back->pdev->dev,
			"VF fail(%d) to set mtu\n", ret);
	return ret;
}

static int hclge_mbx_get_qid_in_pf_handler(struct hclge_mbx_ops_param *param)
{
	return hclge_get_queue_id_in_pf(param->vport, param->req,
					param->resp_msg);
}

static int hclge_mbx_get_rss_key_handler(struct hclge_mbx_ops_param *param)
{
	return hclge_get_rss_key(param->vport, param->req, param->resp_msg);
}

static int hclge_mbx_get_link_mode_handler(struct hclge_mbx_ops_param *param)
{
	hclge_get_link_mode(param->vport, param->req);
	return 0;
}

static int
hclge_mbx_get_vf_flr_status_handler(struct hclge_mbx_ops_param *param)
{
	hclge_rm_vport_all_mac_table(param->vport, false,
				     HCLGE_MAC_ADDR_UC);
	hclge_rm_vport_all_mac_table(param->vport, false,
				     HCLGE_MAC_ADDR_MC);
	hclge_rm_vport_all_vlan_table(param->vport, false);
	return 0;
}

static int hclge_mbx_vf_uninit_handler(struct hclge_mbx_ops_param *param)
{
	hclge_rm_vport_all_mac_table(param->vport, true,
				     HCLGE_MAC_ADDR_UC);
	hclge_rm_vport_all_mac_table(param->vport, true,
				     HCLGE_MAC_ADDR_MC);
	hclge_rm_vport_all_vlan_table(param->vport, true);
	param->vport->mps = 0;
	return 0;
}

static int hclge_mbx_get_media_type_handler(struct hclge_mbx_ops_param *param)
{
	hclge_get_vf_media_type(param->vport, param->resp_msg);
	return 0;
}

static int hclge_mbx_push_link_status_handler(struct hclge_mbx_ops_param *param)
{
	hclge_handle_link_change_event(param->vport->back, param->req);
	return 0;
}

static int hclge_mbx_get_mac_addr_handler(struct hclge_mbx_ops_param *param)
{
	hclge_get_vf_mac_addr(param->vport, param->resp_msg);
	return 0;
}

static int hclge_mbx_ncsi_error_handler(struct hclge_mbx_ops_param *param)
{
	hclge_handle_ncsi_error(param->vport->back);
	return 0;
}

static int hclge_mbx_handle_vf_tbl_handler(struct hclge_mbx_ops_param *param)
{
	hclge_handle_vf_tbl(param->vport, param->req);
	return 0;
}

static const hclge_mbx_ops_fn hclge_mbx_ops_list[HCLGE_MBX_OPCODE_MAX] = {
	[HCLGE_MBX_RESET]   = hclge_mbx_reset_handler,
	[HCLGE_MBX_SET_UNICAST] = hclge_mbx_set_unicast_handler,
	[HCLGE_MBX_SET_MULTICAST] = hclge_mbx_set_multicast_handler,
	[HCLGE_MBX_SET_VLAN] = hclge_mbx_set_vlan_handler,
	[HCLGE_MBX_MAP_RING_TO_VECTOR] = hclge_mbx_map_ring_to_vector_handler,
	[HCLGE_MBX_UNMAP_RING_TO_VECTOR] = hclge_mbx_unmap_ring_to_vector_handler,
	[HCLGE_MBX_SET_PROMISC_MODE] = hclge_mbx_set_promisc_mode_handler,
	[HCLGE_MBX_GET_QINFO] = hclge_mbx_get_qinfo_handler,
	[HCLGE_MBX_GET_QDEPTH] = hclge_mbx_get_qdepth_handler,
	[HCLGE_MBX_GET_BASIC_INFO] = hclge_mbx_get_basic_info_handler,
	[HCLGE_MBX_GET_RSS_KEY] = hclge_mbx_get_rss_key_handler,
	[HCLGE_MBX_GET_MAC_ADDR] = hclge_mbx_get_mac_addr_handler,
	[HCLGE_MBX_GET_LINK_STATUS] = hclge_mbx_get_link_status_handler,
	[HCLGE_MBX_QUEUE_RESET] = hclge_mbx_queue_reset_handler,
	[HCLGE_MBX_KEEP_ALIVE] = hclge_mbx_keep_alive_handler,
	[HCLGE_MBX_SET_ALIVE] = hclge_mbx_set_alive_handler,
	[HCLGE_MBX_SET_MTU] = hclge_mbx_set_mtu_handler,
	[HCLGE_MBX_GET_QID_IN_PF] = hclge_mbx_get_qid_in_pf_handler,
	[HCLGE_MBX_GET_LINK_MODE] = hclge_mbx_get_link_mode_handler,
	[HCLGE_MBX_GET_MEDIA_TYPE] = hclge_mbx_get_media_type_handler,
	[HCLGE_MBX_VF_UNINIT] = hclge_mbx_vf_uninit_handler,
	[HCLGE_MBX_HANDLE_VF_TBL] = hclge_mbx_handle_vf_tbl_handler,
	[HCLGE_MBX_GET_RING_VECTOR_MAP] = hclge_mbx_get_ring_vector_map_handler,
	[HCLGE_MBX_GET_VF_FLR_STATUS] = hclge_mbx_get_vf_flr_status_handler,
	[HCLGE_MBX_PUSH_LINK_STATUS] = hclge_mbx_push_link_status_handler,
	[HCLGE_MBX_NCSI_ERROR] = hclge_mbx_ncsi_error_handler,
};

static void hclge_mbx_request_handling(struct hclge_mbx_ops_param *param)
{
	hclge_mbx_ops_fn cmd_func = NULL;
	struct hclge_dev *hdev;
	int ret = 0;

	hdev = param->vport->back;
	cmd_func = hclge_mbx_ops_list[param->req->msg.code];
	if (cmd_func)
		ret = cmd_func(param);
	else
		dev_err(&hdev->pdev->dev,
			"un-supported mailbox message, code = %u\n",
			param->req->msg.code);

	/* PF driver should not reply IMP */
	if (hnae3_get_bit(param->req->mbx_need_resp, HCLGE_MBX_NEED_RESP_B) &&
	    param->req->msg.code < HCLGE_MBX_GET_VF_FLR_STATUS) {
		param->resp_msg->status = ret;
		if (time_is_before_jiffies(hdev->last_mbx_scheduled +
					   HCLGE_MBX_SCHED_TIMEOUT))
			dev_warn(&hdev->pdev->dev,
				 "resp vport%u mbx(%u,%u) late\n",
				 param->req->mbx_src_vfid,
				 param->req->msg.code,
				 param->req->msg.subcode);

		hclge_gen_resp_to_vf(param->vport, param->req, param->resp_msg);
	}
}

void hclge_mbx_handler(struct hclge_dev *hdev)
{
	struct hclge_comm_cmq_ring *crq = &hdev->hw.hw.cmq.crq;
	struct hclge_respond_to_vf_msg resp_msg;
	struct hclge_mbx_vf_to_pf_cmd *req;
	struct hclge_mbx_ops_param param;
	struct hclge_desc *desc;
	unsigned int flag;

	param.resp_msg = &resp_msg;
	/* handle all the mailbox requests in the queue */
	while (!hclge_cmd_crq_empty(&hdev->hw)) {
		if (test_bit(HCLGE_COMM_STATE_CMD_DISABLE,
			     &hdev->hw.hw.comm_state)) {
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
				 req->msg.code);

			/* dropping/not processing this invalid message */
			crq->desc[crq->next_to_use].flag = 0;
			hclge_mbx_ring_ptr_move_crq(crq);
			continue;
		}

		trace_hclge_pf_mbx_get(hdev, req);

		/* clear the resp_msg before processing every mailbox message */
		memset(&resp_msg, 0, sizeof(resp_msg));
		param.vport = &hdev->vport[req->mbx_src_vfid];
		param.req = req;
		hclge_mbx_request_handling(&param);

		crq->desc[crq->next_to_use].flag = 0;
		hclge_mbx_ring_ptr_move_crq(crq);
	}

	/* Write back CMDQ_RQ header pointer, M7 need this pointer */
	hclge_write_dev(&hdev->hw, HCLGE_COMM_NIC_CRQ_HEAD_REG,
			crq->next_to_use);
}
