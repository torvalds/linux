// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2016-2017 Hisilicon Limited.

#include <linux/etherdevice.h>
#include <linux/iopoll.h>
#include <net/rtnetlink.h>
#include "hclgevf_cmd.h"
#include "hclgevf_main.h"
#include "hclge_mbx.h"
#include "hnae3.h"
#include "hclgevf_devlink.h"
#include "hclge_comm_rss.h"

#define HCLGEVF_NAME	"hclgevf"

#define HCLGEVF_RESET_MAX_FAIL_CNT	5

static int hclgevf_reset_hdev(struct hclgevf_dev *hdev);
static void hclgevf_task_schedule(struct hclgevf_dev *hdev,
				  unsigned long delay);

static struct hnae3_ae_algo ae_algovf;

static struct workqueue_struct *hclgevf_wq;

static const struct pci_device_id ae_algovf_pci_tbl[] = {
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_VF), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_RDMA_DCB_PFC_VF),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	/* required last entry */
	{0, }
};

MODULE_DEVICE_TABLE(pci, ae_algovf_pci_tbl);

static const u32 cmdq_reg_addr_list[] = {HCLGE_COMM_NIC_CSQ_BASEADDR_L_REG,
					 HCLGE_COMM_NIC_CSQ_BASEADDR_H_REG,
					 HCLGE_COMM_NIC_CSQ_DEPTH_REG,
					 HCLGE_COMM_NIC_CSQ_TAIL_REG,
					 HCLGE_COMM_NIC_CSQ_HEAD_REG,
					 HCLGE_COMM_NIC_CRQ_BASEADDR_L_REG,
					 HCLGE_COMM_NIC_CRQ_BASEADDR_H_REG,
					 HCLGE_COMM_NIC_CRQ_DEPTH_REG,
					 HCLGE_COMM_NIC_CRQ_TAIL_REG,
					 HCLGE_COMM_NIC_CRQ_HEAD_REG,
					 HCLGE_COMM_VECTOR0_CMDQ_SRC_REG,
					 HCLGE_COMM_VECTOR0_CMDQ_STATE_REG,
					 HCLGE_COMM_CMDQ_INTR_EN_REG,
					 HCLGE_COMM_CMDQ_INTR_GEN_REG};

static const u32 common_reg_addr_list[] = {HCLGEVF_MISC_VECTOR_REG_BASE,
					   HCLGEVF_RST_ING,
					   HCLGEVF_GRO_EN_REG};

static const u32 ring_reg_addr_list[] = {HCLGEVF_RING_RX_ADDR_L_REG,
					 HCLGEVF_RING_RX_ADDR_H_REG,
					 HCLGEVF_RING_RX_BD_NUM_REG,
					 HCLGEVF_RING_RX_BD_LENGTH_REG,
					 HCLGEVF_RING_RX_MERGE_EN_REG,
					 HCLGEVF_RING_RX_TAIL_REG,
					 HCLGEVF_RING_RX_HEAD_REG,
					 HCLGEVF_RING_RX_FBD_NUM_REG,
					 HCLGEVF_RING_RX_OFFSET_REG,
					 HCLGEVF_RING_RX_FBD_OFFSET_REG,
					 HCLGEVF_RING_RX_STASH_REG,
					 HCLGEVF_RING_RX_BD_ERR_REG,
					 HCLGEVF_RING_TX_ADDR_L_REG,
					 HCLGEVF_RING_TX_ADDR_H_REG,
					 HCLGEVF_RING_TX_BD_NUM_REG,
					 HCLGEVF_RING_TX_PRIORITY_REG,
					 HCLGEVF_RING_TX_TC_REG,
					 HCLGEVF_RING_TX_MERGE_EN_REG,
					 HCLGEVF_RING_TX_TAIL_REG,
					 HCLGEVF_RING_TX_HEAD_REG,
					 HCLGEVF_RING_TX_FBD_NUM_REG,
					 HCLGEVF_RING_TX_OFFSET_REG,
					 HCLGEVF_RING_TX_EBD_NUM_REG,
					 HCLGEVF_RING_TX_EBD_OFFSET_REG,
					 HCLGEVF_RING_TX_BD_ERR_REG,
					 HCLGEVF_RING_EN_REG};

static const u32 tqp_intr_reg_addr_list[] = {HCLGEVF_TQP_INTR_CTRL_REG,
					     HCLGEVF_TQP_INTR_GL0_REG,
					     HCLGEVF_TQP_INTR_GL1_REG,
					     HCLGEVF_TQP_INTR_GL2_REG,
					     HCLGEVF_TQP_INTR_RL_REG};

/* hclgevf_cmd_send - send command to command queue
 * @hw: pointer to the hw struct
 * @desc: prefilled descriptor for describing the command
 * @num : the number of descriptors to be sent
 *
 * This is the main send command for command queue, it
 * sends the queue, cleans the queue, etc
 */
int hclgevf_cmd_send(struct hclgevf_hw *hw, struct hclge_desc *desc, int num)
{
	return hclge_comm_cmd_send(&hw->hw, desc, num);
}

void hclgevf_arq_init(struct hclgevf_dev *hdev)
{
	struct hclge_comm_cmq *cmdq = &hdev->hw.hw.cmq;

	spin_lock(&cmdq->crq.lock);
	/* initialize the pointers of async rx queue of mailbox */
	hdev->arq.hdev = hdev;
	hdev->arq.head = 0;
	hdev->arq.tail = 0;
	atomic_set(&hdev->arq.count, 0);
	spin_unlock(&cmdq->crq.lock);
}

static struct hclgevf_dev *hclgevf_ae_get_hdev(struct hnae3_handle *handle)
{
	if (!handle->client)
		return container_of(handle, struct hclgevf_dev, nic);
	else if (handle->client->type == HNAE3_CLIENT_ROCE)
		return container_of(handle, struct hclgevf_dev, roce);
	else
		return container_of(handle, struct hclgevf_dev, nic);
}

static void hclgevf_update_stats(struct hnae3_handle *handle,
				 struct net_device_stats *net_stats)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	int status;

	status = hclge_comm_tqps_update_stats(handle, &hdev->hw.hw);
	if (status)
		dev_err(&hdev->pdev->dev,
			"VF update of TQPS stats fail, status = %d.\n",
			status);
}

static int hclgevf_get_sset_count(struct hnae3_handle *handle, int strset)
{
	if (strset == ETH_SS_TEST)
		return -EOPNOTSUPP;
	else if (strset == ETH_SS_STATS)
		return hclge_comm_tqps_get_sset_count(handle);

	return 0;
}

static void hclgevf_get_strings(struct hnae3_handle *handle, u32 strset,
				u8 *data)
{
	u8 *p = (char *)data;

	if (strset == ETH_SS_STATS)
		p = hclge_comm_tqps_get_strings(handle, p);
}

static void hclgevf_get_stats(struct hnae3_handle *handle, u64 *data)
{
	hclge_comm_tqps_get_stats(handle, data);
}

static void hclgevf_build_send_msg(struct hclge_vf_to_pf_msg *msg, u8 code,
				   u8 subcode)
{
	if (msg) {
		memset(msg, 0, sizeof(struct hclge_vf_to_pf_msg));
		msg->code = code;
		msg->subcode = subcode;
	}
}

static int hclgevf_get_basic_info(struct hclgevf_dev *hdev)
{
	struct hnae3_ae_dev *ae_dev = hdev->ae_dev;
	u8 resp_msg[HCLGE_MBX_MAX_RESP_DATA_SIZE];
	struct hclge_basic_info *basic_info;
	struct hclge_vf_to_pf_msg send_msg;
	unsigned long caps;
	int status;

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_GET_BASIC_INFO, 0);
	status = hclgevf_send_mbx_msg(hdev, &send_msg, true, resp_msg,
				      sizeof(resp_msg));
	if (status) {
		dev_err(&hdev->pdev->dev,
			"failed to get basic info from pf, ret = %d", status);
		return status;
	}

	basic_info = (struct hclge_basic_info *)resp_msg;

	hdev->hw_tc_map = basic_info->hw_tc_map;
	hdev->mbx_api_version = basic_info->mbx_api_version;
	caps = basic_info->pf_caps;
	if (test_bit(HNAE3_PF_SUPPORT_VLAN_FLTR_MDF_B, &caps))
		set_bit(HNAE3_DEV_SUPPORT_VLAN_FLTR_MDF_B, ae_dev->caps);

	return 0;
}

static int hclgevf_get_port_base_vlan_filter_state(struct hclgevf_dev *hdev)
{
	struct hnae3_handle *nic = &hdev->nic;
	struct hclge_vf_to_pf_msg send_msg;
	u8 resp_msg;
	int ret;

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_SET_VLAN,
			       HCLGE_MBX_GET_PORT_BASE_VLAN_STATE);
	ret = hclgevf_send_mbx_msg(hdev, &send_msg, true, &resp_msg,
				   sizeof(u8));
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"VF request to get port based vlan state failed %d",
			ret);
		return ret;
	}

	nic->port_base_vlan_state = resp_msg;

	return 0;
}

static int hclgevf_get_queue_info(struct hclgevf_dev *hdev)
{
#define HCLGEVF_TQPS_RSS_INFO_LEN	6
#define HCLGEVF_TQPS_ALLOC_OFFSET	0
#define HCLGEVF_TQPS_RSS_SIZE_OFFSET	2
#define HCLGEVF_TQPS_RX_BUFFER_LEN_OFFSET	4

	u8 resp_msg[HCLGEVF_TQPS_RSS_INFO_LEN];
	struct hclge_vf_to_pf_msg send_msg;
	int status;

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_GET_QINFO, 0);
	status = hclgevf_send_mbx_msg(hdev, &send_msg, true, resp_msg,
				      HCLGEVF_TQPS_RSS_INFO_LEN);
	if (status) {
		dev_err(&hdev->pdev->dev,
			"VF request to get tqp info from PF failed %d",
			status);
		return status;
	}

	memcpy(&hdev->num_tqps, &resp_msg[HCLGEVF_TQPS_ALLOC_OFFSET],
	       sizeof(u16));
	memcpy(&hdev->rss_size_max, &resp_msg[HCLGEVF_TQPS_RSS_SIZE_OFFSET],
	       sizeof(u16));
	memcpy(&hdev->rx_buf_len, &resp_msg[HCLGEVF_TQPS_RX_BUFFER_LEN_OFFSET],
	       sizeof(u16));

	return 0;
}

static int hclgevf_get_queue_depth(struct hclgevf_dev *hdev)
{
#define HCLGEVF_TQPS_DEPTH_INFO_LEN	4
#define HCLGEVF_TQPS_NUM_TX_DESC_OFFSET	0
#define HCLGEVF_TQPS_NUM_RX_DESC_OFFSET	2

	u8 resp_msg[HCLGEVF_TQPS_DEPTH_INFO_LEN];
	struct hclge_vf_to_pf_msg send_msg;
	int ret;

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_GET_QDEPTH, 0);
	ret = hclgevf_send_mbx_msg(hdev, &send_msg, true, resp_msg,
				   HCLGEVF_TQPS_DEPTH_INFO_LEN);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"VF request to get tqp depth info from PF failed %d",
			ret);
		return ret;
	}

	memcpy(&hdev->num_tx_desc, &resp_msg[HCLGEVF_TQPS_NUM_TX_DESC_OFFSET],
	       sizeof(u16));
	memcpy(&hdev->num_rx_desc, &resp_msg[HCLGEVF_TQPS_NUM_RX_DESC_OFFSET],
	       sizeof(u16));

	return 0;
}

static u16 hclgevf_get_qid_global(struct hnae3_handle *handle, u16 queue_id)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hclge_vf_to_pf_msg send_msg;
	u16 qid_in_pf = 0;
	u8 resp_data[2];
	int ret;

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_GET_QID_IN_PF, 0);
	memcpy(send_msg.data, &queue_id, sizeof(queue_id));
	ret = hclgevf_send_mbx_msg(hdev, &send_msg, true, resp_data,
				   sizeof(resp_data));
	if (!ret)
		qid_in_pf = *(u16 *)resp_data;

	return qid_in_pf;
}

static int hclgevf_get_pf_media_type(struct hclgevf_dev *hdev)
{
	struct hclge_vf_to_pf_msg send_msg;
	u8 resp_msg[2];
	int ret;

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_GET_MEDIA_TYPE, 0);
	ret = hclgevf_send_mbx_msg(hdev, &send_msg, true, resp_msg,
				   sizeof(resp_msg));
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"VF request to get the pf port media type failed %d",
			ret);
		return ret;
	}

	hdev->hw.mac.media_type = resp_msg[0];
	hdev->hw.mac.module_type = resp_msg[1];

	return 0;
}

static int hclgevf_alloc_tqps(struct hclgevf_dev *hdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(hdev->pdev);
	struct hclge_comm_tqp *tqp;
	int i;

	hdev->htqp = devm_kcalloc(&hdev->pdev->dev, hdev->num_tqps,
				  sizeof(struct hclge_comm_tqp), GFP_KERNEL);
	if (!hdev->htqp)
		return -ENOMEM;

	tqp = hdev->htqp;

	for (i = 0; i < hdev->num_tqps; i++) {
		tqp->dev = &hdev->pdev->dev;
		tqp->index = i;

		tqp->q.ae_algo = &ae_algovf;
		tqp->q.buf_size = hdev->rx_buf_len;
		tqp->q.tx_desc_num = hdev->num_tx_desc;
		tqp->q.rx_desc_num = hdev->num_rx_desc;

		/* need an extended offset to configure queues >=
		 * HCLGEVF_TQP_MAX_SIZE_DEV_V2.
		 */
		if (i < HCLGEVF_TQP_MAX_SIZE_DEV_V2)
			tqp->q.io_base = hdev->hw.hw.io_base +
					 HCLGEVF_TQP_REG_OFFSET +
					 i * HCLGEVF_TQP_REG_SIZE;
		else
			tqp->q.io_base = hdev->hw.hw.io_base +
					 HCLGEVF_TQP_REG_OFFSET +
					 HCLGEVF_TQP_EXT_REG_OFFSET +
					 (i - HCLGEVF_TQP_MAX_SIZE_DEV_V2) *
					 HCLGEVF_TQP_REG_SIZE;

		/* when device supports tx push and has device memory,
		 * the queue can execute push mode or doorbell mode on
		 * device memory.
		 */
		if (test_bit(HNAE3_DEV_SUPPORT_TX_PUSH_B, ae_dev->caps))
			tqp->q.mem_base = hdev->hw.hw.mem_base +
					  HCLGEVF_TQP_MEM_OFFSET(hdev, i);

		tqp++;
	}

	return 0;
}

static int hclgevf_knic_setup(struct hclgevf_dev *hdev)
{
	struct hnae3_handle *nic = &hdev->nic;
	struct hnae3_knic_private_info *kinfo;
	u16 new_tqps = hdev->num_tqps;
	unsigned int i;
	u8 num_tc = 0;

	kinfo = &nic->kinfo;
	kinfo->num_tx_desc = hdev->num_tx_desc;
	kinfo->num_rx_desc = hdev->num_rx_desc;
	kinfo->rx_buf_len = hdev->rx_buf_len;
	for (i = 0; i < HCLGE_COMM_MAX_TC_NUM; i++)
		if (hdev->hw_tc_map & BIT(i))
			num_tc++;

	num_tc = num_tc ? num_tc : 1;
	kinfo->tc_info.num_tc = num_tc;
	kinfo->rss_size = min_t(u16, hdev->rss_size_max, new_tqps / num_tc);
	new_tqps = kinfo->rss_size * num_tc;
	kinfo->num_tqps = min(new_tqps, hdev->num_tqps);

	kinfo->tqp = devm_kcalloc(&hdev->pdev->dev, kinfo->num_tqps,
				  sizeof(struct hnae3_queue *), GFP_KERNEL);
	if (!kinfo->tqp)
		return -ENOMEM;

	for (i = 0; i < kinfo->num_tqps; i++) {
		hdev->htqp[i].q.handle = &hdev->nic;
		hdev->htqp[i].q.tqp_index = i;
		kinfo->tqp[i] = &hdev->htqp[i].q;
	}

	/* after init the max rss_size and tqps, adjust the default tqp numbers
	 * and rss size with the actual vector numbers
	 */
	kinfo->num_tqps = min_t(u16, hdev->num_nic_msix - 1, kinfo->num_tqps);
	kinfo->rss_size = min_t(u16, kinfo->num_tqps / num_tc,
				kinfo->rss_size);

	return 0;
}

static void hclgevf_request_link_info(struct hclgevf_dev *hdev)
{
	struct hclge_vf_to_pf_msg send_msg;
	int status;

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_GET_LINK_STATUS, 0);
	status = hclgevf_send_mbx_msg(hdev, &send_msg, false, NULL, 0);
	if (status)
		dev_err(&hdev->pdev->dev,
			"VF failed to fetch link status(%d) from PF", status);
}

void hclgevf_update_link_status(struct hclgevf_dev *hdev, int link_state)
{
	struct hnae3_handle *rhandle = &hdev->roce;
	struct hnae3_handle *handle = &hdev->nic;
	struct hnae3_client *rclient;
	struct hnae3_client *client;

	if (test_and_set_bit(HCLGEVF_STATE_LINK_UPDATING, &hdev->state))
		return;

	client = handle->client;
	rclient = hdev->roce_client;

	link_state =
		test_bit(HCLGEVF_STATE_DOWN, &hdev->state) ? 0 : link_state;
	if (link_state != hdev->hw.mac.link) {
		hdev->hw.mac.link = link_state;
		client->ops->link_status_change(handle, !!link_state);
		if (rclient && rclient->ops->link_status_change)
			rclient->ops->link_status_change(rhandle, !!link_state);
	}

	clear_bit(HCLGEVF_STATE_LINK_UPDATING, &hdev->state);
}

static void hclgevf_update_link_mode(struct hclgevf_dev *hdev)
{
#define HCLGEVF_ADVERTISING	0
#define HCLGEVF_SUPPORTED	1

	struct hclge_vf_to_pf_msg send_msg;

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_GET_LINK_MODE, 0);
	send_msg.data[0] = HCLGEVF_ADVERTISING;
	hclgevf_send_mbx_msg(hdev, &send_msg, false, NULL, 0);
	send_msg.data[0] = HCLGEVF_SUPPORTED;
	hclgevf_send_mbx_msg(hdev, &send_msg, false, NULL, 0);
}

static int hclgevf_set_handle_info(struct hclgevf_dev *hdev)
{
	struct hnae3_handle *nic = &hdev->nic;
	int ret;

	nic->ae_algo = &ae_algovf;
	nic->pdev = hdev->pdev;
	nic->numa_node_mask = hdev->numa_node_mask;
	nic->flags |= HNAE3_SUPPORT_VF;
	nic->kinfo.io_base = hdev->hw.hw.io_base;

	ret = hclgevf_knic_setup(hdev);
	if (ret)
		dev_err(&hdev->pdev->dev, "VF knic setup failed %d\n",
			ret);
	return ret;
}

static void hclgevf_free_vector(struct hclgevf_dev *hdev, int vector_id)
{
	if (hdev->vector_status[vector_id] == HCLGEVF_INVALID_VPORT) {
		dev_warn(&hdev->pdev->dev,
			 "vector(vector_id %d) has been freed.\n", vector_id);
		return;
	}

	hdev->vector_status[vector_id] = HCLGEVF_INVALID_VPORT;
	hdev->num_msi_left += 1;
	hdev->num_msi_used -= 1;
}

static int hclgevf_get_vector(struct hnae3_handle *handle, u16 vector_num,
			      struct hnae3_vector_info *vector_info)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hnae3_vector_info *vector = vector_info;
	int alloc = 0;
	int i, j;

	vector_num = min_t(u16, hdev->num_nic_msix - 1, vector_num);
	vector_num = min(hdev->num_msi_left, vector_num);

	for (j = 0; j < vector_num; j++) {
		for (i = HCLGEVF_MISC_VECTOR_NUM + 1; i < hdev->num_msi; i++) {
			if (hdev->vector_status[i] == HCLGEVF_INVALID_VPORT) {
				vector->vector = pci_irq_vector(hdev->pdev, i);
				vector->io_addr = hdev->hw.hw.io_base +
					HCLGEVF_VECTOR_REG_BASE +
					(i - 1) * HCLGEVF_VECTOR_REG_OFFSET;
				hdev->vector_status[i] = 0;
				hdev->vector_irq[i] = vector->vector;

				vector++;
				alloc++;

				break;
			}
		}
	}
	hdev->num_msi_left -= alloc;
	hdev->num_msi_used += alloc;

	return alloc;
}

static int hclgevf_get_vector_index(struct hclgevf_dev *hdev, int vector)
{
	int i;

	for (i = 0; i < hdev->num_msi; i++)
		if (vector == hdev->vector_irq[i])
			return i;

	return -EINVAL;
}

/* for revision 0x20, vf shared the same rss config with pf */
static int hclgevf_get_rss_hash_key(struct hclgevf_dev *hdev)
{
#define HCLGEVF_RSS_MBX_RESP_LEN	8
	struct hclge_comm_rss_cfg *rss_cfg = &hdev->rss_cfg;
	u8 resp_msg[HCLGEVF_RSS_MBX_RESP_LEN];
	struct hclge_vf_to_pf_msg send_msg;
	u16 msg_num, hash_key_index;
	u8 index;
	int ret;

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_GET_RSS_KEY, 0);
	msg_num = (HCLGE_COMM_RSS_KEY_SIZE + HCLGEVF_RSS_MBX_RESP_LEN - 1) /
			HCLGEVF_RSS_MBX_RESP_LEN;
	for (index = 0; index < msg_num; index++) {
		send_msg.data[0] = index;
		ret = hclgevf_send_mbx_msg(hdev, &send_msg, true, resp_msg,
					   HCLGEVF_RSS_MBX_RESP_LEN);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"VF get rss hash key from PF failed, ret=%d",
				ret);
			return ret;
		}

		hash_key_index = HCLGEVF_RSS_MBX_RESP_LEN * index;
		if (index == msg_num - 1)
			memcpy(&rss_cfg->rss_hash_key[hash_key_index],
			       &resp_msg[0],
			       HCLGE_COMM_RSS_KEY_SIZE - hash_key_index);
		else
			memcpy(&rss_cfg->rss_hash_key[hash_key_index],
			       &resp_msg[0], HCLGEVF_RSS_MBX_RESP_LEN);
	}

	return 0;
}

static int hclgevf_get_rss(struct hnae3_handle *handle, u32 *indir, u8 *key,
			   u8 *hfunc)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hclge_comm_rss_cfg *rss_cfg = &hdev->rss_cfg;
	int ret;

	if (hdev->ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V2) {
		hclge_comm_get_rss_hash_info(rss_cfg, key, hfunc);
	} else {
		if (hfunc)
			*hfunc = ETH_RSS_HASH_TOP;
		if (key) {
			ret = hclgevf_get_rss_hash_key(hdev);
			if (ret)
				return ret;
			memcpy(key, rss_cfg->rss_hash_key,
			       HCLGE_COMM_RSS_KEY_SIZE);
		}
	}

	hclge_comm_get_rss_indir_tbl(rss_cfg, indir,
				     hdev->ae_dev->dev_specs.rss_ind_tbl_size);

	return 0;
}

static int hclgevf_set_rss(struct hnae3_handle *handle, const u32 *indir,
			   const u8 *key, const u8 hfunc)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hclge_comm_rss_cfg *rss_cfg = &hdev->rss_cfg;
	int ret, i;

	if (hdev->ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V2) {
		ret = hclge_comm_set_rss_hash_key(rss_cfg, &hdev->hw.hw, key,
						  hfunc);
		if (ret)
			return ret;
	}

	/* update the shadow RSS table with user specified qids */
	for (i = 0; i < hdev->ae_dev->dev_specs.rss_ind_tbl_size; i++)
		rss_cfg->rss_indirection_tbl[i] = indir[i];

	/* update the hardware */
	return hclge_comm_set_rss_indir_table(hdev->ae_dev, &hdev->hw.hw,
					      rss_cfg->rss_indirection_tbl);
}

static int hclgevf_set_rss_tuple(struct hnae3_handle *handle,
				 struct ethtool_rxnfc *nfc)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	int ret;

	if (hdev->ae_dev->dev_version < HNAE3_DEVICE_VERSION_V2)
		return -EOPNOTSUPP;

	ret = hclge_comm_set_rss_tuple(hdev->ae_dev, &hdev->hw.hw,
				       &hdev->rss_cfg, nfc);
	if (ret)
		dev_err(&hdev->pdev->dev,
		"failed to set rss tuple, ret = %d.\n", ret);

	return ret;
}

static int hclgevf_get_rss_tuple(struct hnae3_handle *handle,
				 struct ethtool_rxnfc *nfc)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	u8 tuple_sets;
	int ret;

	if (hdev->ae_dev->dev_version < HNAE3_DEVICE_VERSION_V2)
		return -EOPNOTSUPP;

	nfc->data = 0;

	ret = hclge_comm_get_rss_tuple(&hdev->rss_cfg, nfc->flow_type,
				       &tuple_sets);
	if (ret || !tuple_sets)
		return ret;

	nfc->data = hclge_comm_convert_rss_tuple(tuple_sets);

	return 0;
}

static int hclgevf_get_tc_size(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hclge_comm_rss_cfg *rss_cfg = &hdev->rss_cfg;

	return rss_cfg->rss_size;
}

static int hclgevf_bind_ring_to_vector(struct hnae3_handle *handle, bool en,
				       int vector_id,
				       struct hnae3_ring_chain_node *ring_chain)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hclge_vf_to_pf_msg send_msg;
	struct hnae3_ring_chain_node *node;
	int status;
	int i = 0;

	memset(&send_msg, 0, sizeof(send_msg));
	send_msg.code = en ? HCLGE_MBX_MAP_RING_TO_VECTOR :
		HCLGE_MBX_UNMAP_RING_TO_VECTOR;
	send_msg.vector_id = vector_id;

	for (node = ring_chain; node; node = node->next) {
		send_msg.param[i].ring_type =
				hnae3_get_bit(node->flag, HNAE3_RING_TYPE_B);

		send_msg.param[i].tqp_index = node->tqp_index;
		send_msg.param[i].int_gl_index =
					hnae3_get_field(node->int_gl_idx,
							HNAE3_RING_GL_IDX_M,
							HNAE3_RING_GL_IDX_S);

		i++;
		if (i == HCLGE_MBX_MAX_RING_CHAIN_PARAM_NUM || !node->next) {
			send_msg.ring_num = i;

			status = hclgevf_send_mbx_msg(hdev, &send_msg, false,
						      NULL, 0);
			if (status) {
				dev_err(&hdev->pdev->dev,
					"Map TQP fail, status is %d.\n",
					status);
				return status;
			}
			i = 0;
		}
	}

	return 0;
}

static int hclgevf_map_ring_to_vector(struct hnae3_handle *handle, int vector,
				      struct hnae3_ring_chain_node *ring_chain)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	int vector_id;

	vector_id = hclgevf_get_vector_index(hdev, vector);
	if (vector_id < 0) {
		dev_err(&handle->pdev->dev,
			"Get vector index fail. ret =%d\n", vector_id);
		return vector_id;
	}

	return hclgevf_bind_ring_to_vector(handle, true, vector_id, ring_chain);
}

static int hclgevf_unmap_ring_from_vector(
				struct hnae3_handle *handle,
				int vector,
				struct hnae3_ring_chain_node *ring_chain)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	int ret, vector_id;

	if (test_bit(HCLGEVF_STATE_RST_HANDLING, &hdev->state))
		return 0;

	vector_id = hclgevf_get_vector_index(hdev, vector);
	if (vector_id < 0) {
		dev_err(&handle->pdev->dev,
			"Get vector index fail. ret =%d\n", vector_id);
		return vector_id;
	}

	ret = hclgevf_bind_ring_to_vector(handle, false, vector_id, ring_chain);
	if (ret)
		dev_err(&handle->pdev->dev,
			"Unmap ring from vector fail. vector=%d, ret =%d\n",
			vector_id,
			ret);

	return ret;
}

static int hclgevf_put_vector(struct hnae3_handle *handle, int vector)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	int vector_id;

	vector_id = hclgevf_get_vector_index(hdev, vector);
	if (vector_id < 0) {
		dev_err(&handle->pdev->dev,
			"hclgevf_put_vector get vector index fail. ret =%d\n",
			vector_id);
		return vector_id;
	}

	hclgevf_free_vector(hdev, vector_id);

	return 0;
}

static int hclgevf_cmd_set_promisc_mode(struct hclgevf_dev *hdev,
					bool en_uc_pmc, bool en_mc_pmc,
					bool en_bc_pmc)
{
	struct hnae3_handle *handle = &hdev->nic;
	struct hclge_vf_to_pf_msg send_msg;
	int ret;

	memset(&send_msg, 0, sizeof(send_msg));
	send_msg.code = HCLGE_MBX_SET_PROMISC_MODE;
	send_msg.en_bc = en_bc_pmc ? 1 : 0;
	send_msg.en_uc = en_uc_pmc ? 1 : 0;
	send_msg.en_mc = en_mc_pmc ? 1 : 0;
	send_msg.en_limit_promisc = test_bit(HNAE3_PFLAG_LIMIT_PROMISC,
					     &handle->priv_flags) ? 1 : 0;

	ret = hclgevf_send_mbx_msg(hdev, &send_msg, false, NULL, 0);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"Set promisc mode fail, status is %d.\n", ret);

	return ret;
}

static int hclgevf_set_promisc_mode(struct hnae3_handle *handle, bool en_uc_pmc,
				    bool en_mc_pmc)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	bool en_bc_pmc;

	en_bc_pmc = hdev->ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V2;

	return hclgevf_cmd_set_promisc_mode(hdev, en_uc_pmc, en_mc_pmc,
					    en_bc_pmc);
}

static void hclgevf_request_update_promisc_mode(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	set_bit(HCLGEVF_STATE_PROMISC_CHANGED, &hdev->state);
	hclgevf_task_schedule(hdev, 0);
}

static void hclgevf_sync_promisc_mode(struct hclgevf_dev *hdev)
{
	struct hnae3_handle *handle = &hdev->nic;
	bool en_uc_pmc = handle->netdev_flags & HNAE3_UPE;
	bool en_mc_pmc = handle->netdev_flags & HNAE3_MPE;
	int ret;

	if (test_bit(HCLGEVF_STATE_PROMISC_CHANGED, &hdev->state)) {
		ret = hclgevf_set_promisc_mode(handle, en_uc_pmc, en_mc_pmc);
		if (!ret)
			clear_bit(HCLGEVF_STATE_PROMISC_CHANGED, &hdev->state);
	}
}

static int hclgevf_tqp_enable_cmd_send(struct hclgevf_dev *hdev, u16 tqp_id,
				       u16 stream_id, bool enable)
{
	struct hclgevf_cfg_com_tqp_queue_cmd *req;
	struct hclge_desc desc;

	req = (struct hclgevf_cfg_com_tqp_queue_cmd *)desc.data;

	hclgevf_cmd_setup_basic_desc(&desc, HCLGE_OPC_CFG_COM_TQP_QUEUE, false);
	req->tqp_id = cpu_to_le16(tqp_id & HCLGEVF_RING_ID_MASK);
	req->stream_id = cpu_to_le16(stream_id);
	if (enable)
		req->enable |= 1U << HCLGEVF_TQP_ENABLE_B;

	return hclgevf_cmd_send(&hdev->hw, &desc, 1);
}

static int hclgevf_tqp_enable(struct hnae3_handle *handle, bool enable)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	int ret;
	u16 i;

	for (i = 0; i < handle->kinfo.num_tqps; i++) {
		ret = hclgevf_tqp_enable_cmd_send(hdev, i, 0, enable);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclgevf_get_host_mac_addr(struct hclgevf_dev *hdev, u8 *p)
{
	struct hclge_vf_to_pf_msg send_msg;
	u8 host_mac[ETH_ALEN];
	int status;

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_GET_MAC_ADDR, 0);
	status = hclgevf_send_mbx_msg(hdev, &send_msg, true, host_mac,
				      ETH_ALEN);
	if (status) {
		dev_err(&hdev->pdev->dev,
			"fail to get VF MAC from host %d", status);
		return status;
	}

	ether_addr_copy(p, host_mac);

	return 0;
}

static void hclgevf_get_mac_addr(struct hnae3_handle *handle, u8 *p)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	u8 host_mac_addr[ETH_ALEN];

	if (hclgevf_get_host_mac_addr(hdev, host_mac_addr))
		return;

	hdev->has_pf_mac = !is_zero_ether_addr(host_mac_addr);
	if (hdev->has_pf_mac)
		ether_addr_copy(p, host_mac_addr);
	else
		ether_addr_copy(p, hdev->hw.mac.mac_addr);
}

static int hclgevf_set_mac_addr(struct hnae3_handle *handle, const void *p,
				bool is_first)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	u8 *old_mac_addr = (u8 *)hdev->hw.mac.mac_addr;
	struct hclge_vf_to_pf_msg send_msg;
	u8 *new_mac_addr = (u8 *)p;
	int status;

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_SET_UNICAST, 0);
	send_msg.subcode = HCLGE_MBX_MAC_VLAN_UC_MODIFY;
	ether_addr_copy(send_msg.data, new_mac_addr);
	if (is_first && !hdev->has_pf_mac)
		eth_zero_addr(&send_msg.data[ETH_ALEN]);
	else
		ether_addr_copy(&send_msg.data[ETH_ALEN], old_mac_addr);
	status = hclgevf_send_mbx_msg(hdev, &send_msg, true, NULL, 0);
	if (!status)
		ether_addr_copy(hdev->hw.mac.mac_addr, new_mac_addr);

	return status;
}

static struct hclgevf_mac_addr_node *
hclgevf_find_mac_node(struct list_head *list, const u8 *mac_addr)
{
	struct hclgevf_mac_addr_node *mac_node, *tmp;

	list_for_each_entry_safe(mac_node, tmp, list, node)
		if (ether_addr_equal(mac_addr, mac_node->mac_addr))
			return mac_node;

	return NULL;
}

static void hclgevf_update_mac_node(struct hclgevf_mac_addr_node *mac_node,
				    enum HCLGEVF_MAC_NODE_STATE state)
{
	switch (state) {
	/* from set_rx_mode or tmp_add_list */
	case HCLGEVF_MAC_TO_ADD:
		if (mac_node->state == HCLGEVF_MAC_TO_DEL)
			mac_node->state = HCLGEVF_MAC_ACTIVE;
		break;
	/* only from set_rx_mode */
	case HCLGEVF_MAC_TO_DEL:
		if (mac_node->state == HCLGEVF_MAC_TO_ADD) {
			list_del(&mac_node->node);
			kfree(mac_node);
		} else {
			mac_node->state = HCLGEVF_MAC_TO_DEL;
		}
		break;
	/* only from tmp_add_list, the mac_node->state won't be
	 * HCLGEVF_MAC_ACTIVE
	 */
	case HCLGEVF_MAC_ACTIVE:
		if (mac_node->state == HCLGEVF_MAC_TO_ADD)
			mac_node->state = HCLGEVF_MAC_ACTIVE;
		break;
	}
}

static int hclgevf_update_mac_list(struct hnae3_handle *handle,
				   enum HCLGEVF_MAC_NODE_STATE state,
				   enum HCLGEVF_MAC_ADDR_TYPE mac_type,
				   const unsigned char *addr)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hclgevf_mac_addr_node *mac_node;
	struct list_head *list;

	list = (mac_type == HCLGEVF_MAC_ADDR_UC) ?
	       &hdev->mac_table.uc_mac_list : &hdev->mac_table.mc_mac_list;

	spin_lock_bh(&hdev->mac_table.mac_list_lock);

	/* if the mac addr is already in the mac list, no need to add a new
	 * one into it, just check the mac addr state, convert it to a new
	 * new state, or just remove it, or do nothing.
	 */
	mac_node = hclgevf_find_mac_node(list, addr);
	if (mac_node) {
		hclgevf_update_mac_node(mac_node, state);
		spin_unlock_bh(&hdev->mac_table.mac_list_lock);
		return 0;
	}
	/* if this address is never added, unnecessary to delete */
	if (state == HCLGEVF_MAC_TO_DEL) {
		spin_unlock_bh(&hdev->mac_table.mac_list_lock);
		return -ENOENT;
	}

	mac_node = kzalloc(sizeof(*mac_node), GFP_ATOMIC);
	if (!mac_node) {
		spin_unlock_bh(&hdev->mac_table.mac_list_lock);
		return -ENOMEM;
	}

	mac_node->state = state;
	ether_addr_copy(mac_node->mac_addr, addr);
	list_add_tail(&mac_node->node, list);

	spin_unlock_bh(&hdev->mac_table.mac_list_lock);
	return 0;
}

static int hclgevf_add_uc_addr(struct hnae3_handle *handle,
			       const unsigned char *addr)
{
	return hclgevf_update_mac_list(handle, HCLGEVF_MAC_TO_ADD,
				       HCLGEVF_MAC_ADDR_UC, addr);
}

static int hclgevf_rm_uc_addr(struct hnae3_handle *handle,
			      const unsigned char *addr)
{
	return hclgevf_update_mac_list(handle, HCLGEVF_MAC_TO_DEL,
				       HCLGEVF_MAC_ADDR_UC, addr);
}

static int hclgevf_add_mc_addr(struct hnae3_handle *handle,
			       const unsigned char *addr)
{
	return hclgevf_update_mac_list(handle, HCLGEVF_MAC_TO_ADD,
				       HCLGEVF_MAC_ADDR_MC, addr);
}

static int hclgevf_rm_mc_addr(struct hnae3_handle *handle,
			      const unsigned char *addr)
{
	return hclgevf_update_mac_list(handle, HCLGEVF_MAC_TO_DEL,
				       HCLGEVF_MAC_ADDR_MC, addr);
}

static int hclgevf_add_del_mac_addr(struct hclgevf_dev *hdev,
				    struct hclgevf_mac_addr_node *mac_node,
				    enum HCLGEVF_MAC_ADDR_TYPE mac_type)
{
	struct hclge_vf_to_pf_msg send_msg;
	u8 code, subcode;

	if (mac_type == HCLGEVF_MAC_ADDR_UC) {
		code = HCLGE_MBX_SET_UNICAST;
		if (mac_node->state == HCLGEVF_MAC_TO_ADD)
			subcode = HCLGE_MBX_MAC_VLAN_UC_ADD;
		else
			subcode = HCLGE_MBX_MAC_VLAN_UC_REMOVE;
	} else {
		code = HCLGE_MBX_SET_MULTICAST;
		if (mac_node->state == HCLGEVF_MAC_TO_ADD)
			subcode = HCLGE_MBX_MAC_VLAN_MC_ADD;
		else
			subcode = HCLGE_MBX_MAC_VLAN_MC_REMOVE;
	}

	hclgevf_build_send_msg(&send_msg, code, subcode);
	ether_addr_copy(send_msg.data, mac_node->mac_addr);
	return hclgevf_send_mbx_msg(hdev, &send_msg, false, NULL, 0);
}

static void hclgevf_config_mac_list(struct hclgevf_dev *hdev,
				    struct list_head *list,
				    enum HCLGEVF_MAC_ADDR_TYPE mac_type)
{
	char format_mac_addr[HNAE3_FORMAT_MAC_ADDR_LEN];
	struct hclgevf_mac_addr_node *mac_node, *tmp;
	int ret;

	list_for_each_entry_safe(mac_node, tmp, list, node) {
		ret = hclgevf_add_del_mac_addr(hdev, mac_node, mac_type);
		if  (ret) {
			hnae3_format_mac_addr(format_mac_addr,
					      mac_node->mac_addr);
			dev_err(&hdev->pdev->dev,
				"failed to configure mac %s, state = %d, ret = %d\n",
				format_mac_addr, mac_node->state, ret);
			return;
		}
		if (mac_node->state == HCLGEVF_MAC_TO_ADD) {
			mac_node->state = HCLGEVF_MAC_ACTIVE;
		} else {
			list_del(&mac_node->node);
			kfree(mac_node);
		}
	}
}

static void hclgevf_sync_from_add_list(struct list_head *add_list,
				       struct list_head *mac_list)
{
	struct hclgevf_mac_addr_node *mac_node, *tmp, *new_node;

	list_for_each_entry_safe(mac_node, tmp, add_list, node) {
		/* if the mac address from tmp_add_list is not in the
		 * uc/mc_mac_list, it means have received a TO_DEL request
		 * during the time window of sending mac config request to PF
		 * If mac_node state is ACTIVE, then change its state to TO_DEL,
		 * then it will be removed at next time. If is TO_ADD, it means
		 * send TO_ADD request failed, so just remove the mac node.
		 */
		new_node = hclgevf_find_mac_node(mac_list, mac_node->mac_addr);
		if (new_node) {
			hclgevf_update_mac_node(new_node, mac_node->state);
			list_del(&mac_node->node);
			kfree(mac_node);
		} else if (mac_node->state == HCLGEVF_MAC_ACTIVE) {
			mac_node->state = HCLGEVF_MAC_TO_DEL;
			list_move_tail(&mac_node->node, mac_list);
		} else {
			list_del(&mac_node->node);
			kfree(mac_node);
		}
	}
}

static void hclgevf_sync_from_del_list(struct list_head *del_list,
				       struct list_head *mac_list)
{
	struct hclgevf_mac_addr_node *mac_node, *tmp, *new_node;

	list_for_each_entry_safe(mac_node, tmp, del_list, node) {
		new_node = hclgevf_find_mac_node(mac_list, mac_node->mac_addr);
		if (new_node) {
			/* If the mac addr is exist in the mac list, it means
			 * received a new request TO_ADD during the time window
			 * of sending mac addr configurrequest to PF, so just
			 * change the mac state to ACTIVE.
			 */
			new_node->state = HCLGEVF_MAC_ACTIVE;
			list_del(&mac_node->node);
			kfree(mac_node);
		} else {
			list_move_tail(&mac_node->node, mac_list);
		}
	}
}

static void hclgevf_clear_list(struct list_head *list)
{
	struct hclgevf_mac_addr_node *mac_node, *tmp;

	list_for_each_entry_safe(mac_node, tmp, list, node) {
		list_del(&mac_node->node);
		kfree(mac_node);
	}
}

static void hclgevf_sync_mac_list(struct hclgevf_dev *hdev,
				  enum HCLGEVF_MAC_ADDR_TYPE mac_type)
{
	struct hclgevf_mac_addr_node *mac_node, *tmp, *new_node;
	struct list_head tmp_add_list, tmp_del_list;
	struct list_head *list;

	INIT_LIST_HEAD(&tmp_add_list);
	INIT_LIST_HEAD(&tmp_del_list);

	/* move the mac addr to the tmp_add_list and tmp_del_list, then
	 * we can add/delete these mac addr outside the spin lock
	 */
	list = (mac_type == HCLGEVF_MAC_ADDR_UC) ?
		&hdev->mac_table.uc_mac_list : &hdev->mac_table.mc_mac_list;

	spin_lock_bh(&hdev->mac_table.mac_list_lock);

	list_for_each_entry_safe(mac_node, tmp, list, node) {
		switch (mac_node->state) {
		case HCLGEVF_MAC_TO_DEL:
			list_move_tail(&mac_node->node, &tmp_del_list);
			break;
		case HCLGEVF_MAC_TO_ADD:
			new_node = kzalloc(sizeof(*new_node), GFP_ATOMIC);
			if (!new_node)
				goto stop_traverse;

			ether_addr_copy(new_node->mac_addr, mac_node->mac_addr);
			new_node->state = mac_node->state;
			list_add_tail(&new_node->node, &tmp_add_list);
			break;
		default:
			break;
		}
	}

stop_traverse:
	spin_unlock_bh(&hdev->mac_table.mac_list_lock);

	/* delete first, in order to get max mac table space for adding */
	hclgevf_config_mac_list(hdev, &tmp_del_list, mac_type);
	hclgevf_config_mac_list(hdev, &tmp_add_list, mac_type);

	/* if some mac addresses were added/deleted fail, move back to the
	 * mac_list, and retry at next time.
	 */
	spin_lock_bh(&hdev->mac_table.mac_list_lock);

	hclgevf_sync_from_del_list(&tmp_del_list, list);
	hclgevf_sync_from_add_list(&tmp_add_list, list);

	spin_unlock_bh(&hdev->mac_table.mac_list_lock);
}

static void hclgevf_sync_mac_table(struct hclgevf_dev *hdev)
{
	hclgevf_sync_mac_list(hdev, HCLGEVF_MAC_ADDR_UC);
	hclgevf_sync_mac_list(hdev, HCLGEVF_MAC_ADDR_MC);
}

static void hclgevf_uninit_mac_list(struct hclgevf_dev *hdev)
{
	spin_lock_bh(&hdev->mac_table.mac_list_lock);

	hclgevf_clear_list(&hdev->mac_table.uc_mac_list);
	hclgevf_clear_list(&hdev->mac_table.mc_mac_list);

	spin_unlock_bh(&hdev->mac_table.mac_list_lock);
}

static int hclgevf_enable_vlan_filter(struct hnae3_handle *handle, bool enable)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hnae3_ae_dev *ae_dev = hdev->ae_dev;
	struct hclge_vf_to_pf_msg send_msg;

	if (!test_bit(HNAE3_DEV_SUPPORT_VLAN_FLTR_MDF_B, ae_dev->caps))
		return -EOPNOTSUPP;

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_SET_VLAN,
			       HCLGE_MBX_ENABLE_VLAN_FILTER);
	send_msg.data[0] = enable ? 1 : 0;

	return hclgevf_send_mbx_msg(hdev, &send_msg, true, NULL, 0);
}

static int hclgevf_set_vlan_filter(struct hnae3_handle *handle,
				   __be16 proto, u16 vlan_id,
				   bool is_kill)
{
#define HCLGEVF_VLAN_MBX_IS_KILL_OFFSET	0
#define HCLGEVF_VLAN_MBX_VLAN_ID_OFFSET	1
#define HCLGEVF_VLAN_MBX_PROTO_OFFSET	3

	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hclge_vf_to_pf_msg send_msg;
	int ret;

	if (vlan_id > HCLGEVF_MAX_VLAN_ID)
		return -EINVAL;

	if (proto != htons(ETH_P_8021Q))
		return -EPROTONOSUPPORT;

	/* When device is resetting or reset failed, firmware is unable to
	 * handle mailbox. Just record the vlan id, and remove it after
	 * reset finished.
	 */
	if ((test_bit(HCLGEVF_STATE_RST_HANDLING, &hdev->state) ||
	     test_bit(HCLGEVF_STATE_RST_FAIL, &hdev->state)) && is_kill) {
		set_bit(vlan_id, hdev->vlan_del_fail_bmap);
		return -EBUSY;
	}

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_SET_VLAN,
			       HCLGE_MBX_VLAN_FILTER);
	send_msg.data[HCLGEVF_VLAN_MBX_IS_KILL_OFFSET] = is_kill;
	memcpy(&send_msg.data[HCLGEVF_VLAN_MBX_VLAN_ID_OFFSET], &vlan_id,
	       sizeof(vlan_id));
	memcpy(&send_msg.data[HCLGEVF_VLAN_MBX_PROTO_OFFSET], &proto,
	       sizeof(proto));
	/* when remove hw vlan filter failed, record the vlan id,
	 * and try to remove it from hw later, to be consistence
	 * with stack.
	 */
	ret = hclgevf_send_mbx_msg(hdev, &send_msg, true, NULL, 0);
	if (is_kill && ret)
		set_bit(vlan_id, hdev->vlan_del_fail_bmap);

	return ret;
}

static void hclgevf_sync_vlan_filter(struct hclgevf_dev *hdev)
{
#define HCLGEVF_MAX_SYNC_COUNT	60
	struct hnae3_handle *handle = &hdev->nic;
	int ret, sync_cnt = 0;
	u16 vlan_id;

	vlan_id = find_first_bit(hdev->vlan_del_fail_bmap, VLAN_N_VID);
	while (vlan_id != VLAN_N_VID) {
		ret = hclgevf_set_vlan_filter(handle, htons(ETH_P_8021Q),
					      vlan_id, true);
		if (ret)
			return;

		clear_bit(vlan_id, hdev->vlan_del_fail_bmap);
		sync_cnt++;
		if (sync_cnt >= HCLGEVF_MAX_SYNC_COUNT)
			return;

		vlan_id = find_first_bit(hdev->vlan_del_fail_bmap, VLAN_N_VID);
	}
}

static int hclgevf_en_hw_strip_rxvtag(struct hnae3_handle *handle, bool enable)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hclge_vf_to_pf_msg send_msg;

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_SET_VLAN,
			       HCLGE_MBX_VLAN_RX_OFF_CFG);
	send_msg.data[0] = enable ? 1 : 0;
	return hclgevf_send_mbx_msg(hdev, &send_msg, false, NULL, 0);
}

static int hclgevf_reset_tqp(struct hnae3_handle *handle)
{
#define HCLGEVF_RESET_ALL_QUEUE_DONE	1U
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hclge_vf_to_pf_msg send_msg;
	u8 return_status = 0;
	int ret;
	u16 i;

	/* disable vf queue before send queue reset msg to PF */
	ret = hclgevf_tqp_enable(handle, false);
	if (ret) {
		dev_err(&hdev->pdev->dev, "failed to disable tqp, ret = %d\n",
			ret);
		return ret;
	}

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_QUEUE_RESET, 0);

	ret = hclgevf_send_mbx_msg(hdev, &send_msg, true, &return_status,
				   sizeof(return_status));
	if (ret || return_status == HCLGEVF_RESET_ALL_QUEUE_DONE)
		return ret;

	for (i = 1; i < handle->kinfo.num_tqps; i++) {
		hclgevf_build_send_msg(&send_msg, HCLGE_MBX_QUEUE_RESET, 0);
		memcpy(send_msg.data, &i, sizeof(i));
		ret = hclgevf_send_mbx_msg(hdev, &send_msg, true, NULL, 0);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclgevf_set_mtu(struct hnae3_handle *handle, int new_mtu)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hclge_vf_to_pf_msg send_msg;

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_SET_MTU, 0);
	memcpy(send_msg.data, &new_mtu, sizeof(new_mtu));
	return hclgevf_send_mbx_msg(hdev, &send_msg, true, NULL, 0);
}

static int hclgevf_notify_client(struct hclgevf_dev *hdev,
				 enum hnae3_reset_notify_type type)
{
	struct hnae3_client *client = hdev->nic_client;
	struct hnae3_handle *handle = &hdev->nic;
	int ret;

	if (!test_bit(HCLGEVF_STATE_NIC_REGISTERED, &hdev->state) ||
	    !client)
		return 0;

	if (!client->ops->reset_notify)
		return -EOPNOTSUPP;

	ret = client->ops->reset_notify(handle, type);
	if (ret)
		dev_err(&hdev->pdev->dev, "notify nic client failed %d(%d)\n",
			type, ret);

	return ret;
}

static int hclgevf_notify_roce_client(struct hclgevf_dev *hdev,
				      enum hnae3_reset_notify_type type)
{
	struct hnae3_client *client = hdev->roce_client;
	struct hnae3_handle *handle = &hdev->roce;
	int ret;

	if (!test_bit(HCLGEVF_STATE_ROCE_REGISTERED, &hdev->state) || !client)
		return 0;

	if (!client->ops->reset_notify)
		return -EOPNOTSUPP;

	ret = client->ops->reset_notify(handle, type);
	if (ret)
		dev_err(&hdev->pdev->dev, "notify roce client failed %d(%d)",
			type, ret);
	return ret;
}

static int hclgevf_reset_wait(struct hclgevf_dev *hdev)
{
#define HCLGEVF_RESET_WAIT_US	20000
#define HCLGEVF_RESET_WAIT_CNT	2000
#define HCLGEVF_RESET_WAIT_TIMEOUT_US	\
	(HCLGEVF_RESET_WAIT_US * HCLGEVF_RESET_WAIT_CNT)

	u32 val;
	int ret;

	if (hdev->reset_type == HNAE3_VF_RESET)
		ret = readl_poll_timeout(hdev->hw.hw.io_base +
					 HCLGEVF_VF_RST_ING, val,
					 !(val & HCLGEVF_VF_RST_ING_BIT),
					 HCLGEVF_RESET_WAIT_US,
					 HCLGEVF_RESET_WAIT_TIMEOUT_US);
	else
		ret = readl_poll_timeout(hdev->hw.hw.io_base +
					 HCLGEVF_RST_ING, val,
					 !(val & HCLGEVF_RST_ING_BITS),
					 HCLGEVF_RESET_WAIT_US,
					 HCLGEVF_RESET_WAIT_TIMEOUT_US);

	/* hardware completion status should be available by this time */
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"couldn't get reset done status from h/w, timeout!\n");
		return ret;
	}

	/* we will wait a bit more to let reset of the stack to complete. This
	 * might happen in case reset assertion was made by PF. Yes, this also
	 * means we might end up waiting bit more even for VF reset.
	 */
	msleep(5000);

	return 0;
}

static void hclgevf_reset_handshake(struct hclgevf_dev *hdev, bool enable)
{
	u32 reg_val;

	reg_val = hclgevf_read_dev(&hdev->hw, HCLGE_COMM_NIC_CSQ_DEPTH_REG);
	if (enable)
		reg_val |= HCLGEVF_NIC_SW_RST_RDY;
	else
		reg_val &= ~HCLGEVF_NIC_SW_RST_RDY;

	hclgevf_write_dev(&hdev->hw, HCLGE_COMM_NIC_CSQ_DEPTH_REG,
			  reg_val);
}

static int hclgevf_reset_stack(struct hclgevf_dev *hdev)
{
	int ret;

	/* uninitialize the nic client */
	ret = hclgevf_notify_client(hdev, HNAE3_UNINIT_CLIENT);
	if (ret)
		return ret;

	/* re-initialize the hclge device */
	ret = hclgevf_reset_hdev(hdev);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"hclge device re-init failed, VF is disabled!\n");
		return ret;
	}

	/* bring up the nic client again */
	ret = hclgevf_notify_client(hdev, HNAE3_INIT_CLIENT);
	if (ret)
		return ret;

	/* clear handshake status with IMP */
	hclgevf_reset_handshake(hdev, false);

	/* bring up the nic to enable TX/RX again */
	return hclgevf_notify_client(hdev, HNAE3_UP_CLIENT);
}

static int hclgevf_reset_prepare_wait(struct hclgevf_dev *hdev)
{
#define HCLGEVF_RESET_SYNC_TIME 100

	if (hdev->reset_type == HNAE3_VF_FUNC_RESET) {
		struct hclge_vf_to_pf_msg send_msg;
		int ret;

		hclgevf_build_send_msg(&send_msg, HCLGE_MBX_RESET, 0);
		ret = hclgevf_send_mbx_msg(hdev, &send_msg, true, NULL, 0);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"failed to assert VF reset, ret = %d\n", ret);
			return ret;
		}
		hdev->rst_stats.vf_func_rst_cnt++;
	}

	set_bit(HCLGE_COMM_STATE_CMD_DISABLE, &hdev->hw.hw.comm_state);
	/* inform hardware that preparatory work is done */
	msleep(HCLGEVF_RESET_SYNC_TIME);
	hclgevf_reset_handshake(hdev, true);
	dev_info(&hdev->pdev->dev, "prepare reset(%d) wait done\n",
		 hdev->reset_type);

	return 0;
}

static void hclgevf_dump_rst_info(struct hclgevf_dev *hdev)
{
	dev_info(&hdev->pdev->dev, "VF function reset count: %u\n",
		 hdev->rst_stats.vf_func_rst_cnt);
	dev_info(&hdev->pdev->dev, "FLR reset count: %u\n",
		 hdev->rst_stats.flr_rst_cnt);
	dev_info(&hdev->pdev->dev, "VF reset count: %u\n",
		 hdev->rst_stats.vf_rst_cnt);
	dev_info(&hdev->pdev->dev, "reset done count: %u\n",
		 hdev->rst_stats.rst_done_cnt);
	dev_info(&hdev->pdev->dev, "HW reset done count: %u\n",
		 hdev->rst_stats.hw_rst_done_cnt);
	dev_info(&hdev->pdev->dev, "reset count: %u\n",
		 hdev->rst_stats.rst_cnt);
	dev_info(&hdev->pdev->dev, "reset fail count: %u\n",
		 hdev->rst_stats.rst_fail_cnt);
	dev_info(&hdev->pdev->dev, "vector0 interrupt enable status: 0x%x\n",
		 hclgevf_read_dev(&hdev->hw, HCLGEVF_MISC_VECTOR_REG_BASE));
	dev_info(&hdev->pdev->dev, "vector0 interrupt status: 0x%x\n",
		 hclgevf_read_dev(&hdev->hw, HCLGE_COMM_VECTOR0_CMDQ_STATE_REG));
	dev_info(&hdev->pdev->dev, "handshake status: 0x%x\n",
		 hclgevf_read_dev(&hdev->hw, HCLGE_COMM_NIC_CSQ_DEPTH_REG));
	dev_info(&hdev->pdev->dev, "function reset status: 0x%x\n",
		 hclgevf_read_dev(&hdev->hw, HCLGEVF_RST_ING));
	dev_info(&hdev->pdev->dev, "hdev state: 0x%lx\n", hdev->state);
}

static void hclgevf_reset_err_handle(struct hclgevf_dev *hdev)
{
	/* recover handshake status with IMP when reset fail */
	hclgevf_reset_handshake(hdev, true);
	hdev->rst_stats.rst_fail_cnt++;
	dev_err(&hdev->pdev->dev, "failed to reset VF(%u)\n",
		hdev->rst_stats.rst_fail_cnt);

	if (hdev->rst_stats.rst_fail_cnt < HCLGEVF_RESET_MAX_FAIL_CNT)
		set_bit(hdev->reset_type, &hdev->reset_pending);

	if (hclgevf_is_reset_pending(hdev)) {
		set_bit(HCLGEVF_RESET_PENDING, &hdev->reset_state);
		hclgevf_reset_task_schedule(hdev);
	} else {
		set_bit(HCLGEVF_STATE_RST_FAIL, &hdev->state);
		hclgevf_dump_rst_info(hdev);
	}
}

static int hclgevf_reset_prepare(struct hclgevf_dev *hdev)
{
	int ret;

	hdev->rst_stats.rst_cnt++;

	/* perform reset of the stack & ae device for a client */
	ret = hclgevf_notify_roce_client(hdev, HNAE3_DOWN_CLIENT);
	if (ret)
		return ret;

	rtnl_lock();
	/* bring down the nic to stop any ongoing TX/RX */
	ret = hclgevf_notify_client(hdev, HNAE3_DOWN_CLIENT);
	rtnl_unlock();
	if (ret)
		return ret;

	return hclgevf_reset_prepare_wait(hdev);
}

static int hclgevf_reset_rebuild(struct hclgevf_dev *hdev)
{
	int ret;

	hdev->rst_stats.hw_rst_done_cnt++;
	ret = hclgevf_notify_roce_client(hdev, HNAE3_UNINIT_CLIENT);
	if (ret)
		return ret;

	rtnl_lock();
	/* now, re-initialize the nic client and ae device */
	ret = hclgevf_reset_stack(hdev);
	rtnl_unlock();
	if (ret) {
		dev_err(&hdev->pdev->dev, "failed to reset VF stack\n");
		return ret;
	}

	ret = hclgevf_notify_roce_client(hdev, HNAE3_INIT_CLIENT);
	/* ignore RoCE notify error if it fails HCLGEVF_RESET_MAX_FAIL_CNT - 1
	 * times
	 */
	if (ret &&
	    hdev->rst_stats.rst_fail_cnt < HCLGEVF_RESET_MAX_FAIL_CNT - 1)
		return ret;

	ret = hclgevf_notify_roce_client(hdev, HNAE3_UP_CLIENT);
	if (ret)
		return ret;

	hdev->last_reset_time = jiffies;
	hdev->rst_stats.rst_done_cnt++;
	hdev->rst_stats.rst_fail_cnt = 0;
	clear_bit(HCLGEVF_STATE_RST_FAIL, &hdev->state);

	return 0;
}

static void hclgevf_reset(struct hclgevf_dev *hdev)
{
	if (hclgevf_reset_prepare(hdev))
		goto err_reset;

	/* check if VF could successfully fetch the hardware reset completion
	 * status from the hardware
	 */
	if (hclgevf_reset_wait(hdev)) {
		/* can't do much in this situation, will disable VF */
		dev_err(&hdev->pdev->dev,
			"failed to fetch H/W reset completion status\n");
		goto err_reset;
	}

	if (hclgevf_reset_rebuild(hdev))
		goto err_reset;

	return;

err_reset:
	hclgevf_reset_err_handle(hdev);
}

static enum hnae3_reset_type hclgevf_get_reset_level(struct hclgevf_dev *hdev,
						     unsigned long *addr)
{
	enum hnae3_reset_type rst_level = HNAE3_NONE_RESET;

	/* return the highest priority reset level amongst all */
	if (test_bit(HNAE3_VF_RESET, addr)) {
		rst_level = HNAE3_VF_RESET;
		clear_bit(HNAE3_VF_RESET, addr);
		clear_bit(HNAE3_VF_PF_FUNC_RESET, addr);
		clear_bit(HNAE3_VF_FUNC_RESET, addr);
	} else if (test_bit(HNAE3_VF_FULL_RESET, addr)) {
		rst_level = HNAE3_VF_FULL_RESET;
		clear_bit(HNAE3_VF_FULL_RESET, addr);
		clear_bit(HNAE3_VF_FUNC_RESET, addr);
	} else if (test_bit(HNAE3_VF_PF_FUNC_RESET, addr)) {
		rst_level = HNAE3_VF_PF_FUNC_RESET;
		clear_bit(HNAE3_VF_PF_FUNC_RESET, addr);
		clear_bit(HNAE3_VF_FUNC_RESET, addr);
	} else if (test_bit(HNAE3_VF_FUNC_RESET, addr)) {
		rst_level = HNAE3_VF_FUNC_RESET;
		clear_bit(HNAE3_VF_FUNC_RESET, addr);
	} else if (test_bit(HNAE3_FLR_RESET, addr)) {
		rst_level = HNAE3_FLR_RESET;
		clear_bit(HNAE3_FLR_RESET, addr);
	}

	return rst_level;
}

static void hclgevf_reset_event(struct pci_dev *pdev,
				struct hnae3_handle *handle)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);
	struct hclgevf_dev *hdev = ae_dev->priv;

	dev_info(&hdev->pdev->dev, "received reset request from VF enet\n");

	if (hdev->default_reset_request)
		hdev->reset_level =
			hclgevf_get_reset_level(hdev,
						&hdev->default_reset_request);
	else
		hdev->reset_level = HNAE3_VF_FUNC_RESET;

	/* reset of this VF requested */
	set_bit(HCLGEVF_RESET_REQUESTED, &hdev->reset_state);
	hclgevf_reset_task_schedule(hdev);

	hdev->last_reset_time = jiffies;
}

static void hclgevf_set_def_reset_request(struct hnae3_ae_dev *ae_dev,
					  enum hnae3_reset_type rst_type)
{
	struct hclgevf_dev *hdev = ae_dev->priv;

	set_bit(rst_type, &hdev->default_reset_request);
}

static void hclgevf_enable_vector(struct hclgevf_misc_vector *vector, bool en)
{
	writel(en ? 1 : 0, vector->addr);
}

static void hclgevf_reset_prepare_general(struct hnae3_ae_dev *ae_dev,
					  enum hnae3_reset_type rst_type)
{
#define HCLGEVF_RESET_RETRY_WAIT_MS	500
#define HCLGEVF_RESET_RETRY_CNT		5

	struct hclgevf_dev *hdev = ae_dev->priv;
	int retry_cnt = 0;
	int ret;

	while (retry_cnt++ < HCLGEVF_RESET_RETRY_CNT) {
		down(&hdev->reset_sem);
		set_bit(HCLGEVF_STATE_RST_HANDLING, &hdev->state);
		hdev->reset_type = rst_type;
		ret = hclgevf_reset_prepare(hdev);
		if (!ret && !hdev->reset_pending)
			break;

		dev_err(&hdev->pdev->dev,
			"failed to prepare to reset, ret=%d, reset_pending:0x%lx, retry_cnt:%d\n",
			ret, hdev->reset_pending, retry_cnt);
		clear_bit(HCLGEVF_STATE_RST_HANDLING, &hdev->state);
		up(&hdev->reset_sem);
		msleep(HCLGEVF_RESET_RETRY_WAIT_MS);
	}

	/* disable misc vector before reset done */
	hclgevf_enable_vector(&hdev->misc_vector, false);

	if (hdev->reset_type == HNAE3_FLR_RESET)
		hdev->rst_stats.flr_rst_cnt++;
}

static void hclgevf_reset_done(struct hnae3_ae_dev *ae_dev)
{
	struct hclgevf_dev *hdev = ae_dev->priv;
	int ret;

	hclgevf_enable_vector(&hdev->misc_vector, true);

	ret = hclgevf_reset_rebuild(hdev);
	if (ret)
		dev_warn(&hdev->pdev->dev, "fail to rebuild, ret=%d\n",
			 ret);

	hdev->reset_type = HNAE3_NONE_RESET;
	clear_bit(HCLGEVF_STATE_RST_HANDLING, &hdev->state);
	up(&hdev->reset_sem);
}

static u32 hclgevf_get_fw_version(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	return hdev->fw_version;
}

static void hclgevf_get_misc_vector(struct hclgevf_dev *hdev)
{
	struct hclgevf_misc_vector *vector = &hdev->misc_vector;

	vector->vector_irq = pci_irq_vector(hdev->pdev,
					    HCLGEVF_MISC_VECTOR_NUM);
	vector->addr = hdev->hw.hw.io_base + HCLGEVF_MISC_VECTOR_REG_BASE;
	/* vector status always valid for Vector 0 */
	hdev->vector_status[HCLGEVF_MISC_VECTOR_NUM] = 0;
	hdev->vector_irq[HCLGEVF_MISC_VECTOR_NUM] = vector->vector_irq;

	hdev->num_msi_left -= 1;
	hdev->num_msi_used += 1;
}

void hclgevf_reset_task_schedule(struct hclgevf_dev *hdev)
{
	if (!test_bit(HCLGEVF_STATE_REMOVING, &hdev->state) &&
	    test_bit(HCLGEVF_STATE_SERVICE_INITED, &hdev->state) &&
	    !test_and_set_bit(HCLGEVF_STATE_RST_SERVICE_SCHED,
			      &hdev->state))
		mod_delayed_work(hclgevf_wq, &hdev->service_task, 0);
}

void hclgevf_mbx_task_schedule(struct hclgevf_dev *hdev)
{
	if (!test_bit(HCLGEVF_STATE_REMOVING, &hdev->state) &&
	    !test_and_set_bit(HCLGEVF_STATE_MBX_SERVICE_SCHED,
			      &hdev->state))
		mod_delayed_work(hclgevf_wq, &hdev->service_task, 0);
}

static void hclgevf_task_schedule(struct hclgevf_dev *hdev,
				  unsigned long delay)
{
	if (!test_bit(HCLGEVF_STATE_REMOVING, &hdev->state) &&
	    !test_bit(HCLGEVF_STATE_RST_FAIL, &hdev->state))
		mod_delayed_work(hclgevf_wq, &hdev->service_task, delay);
}

static void hclgevf_reset_service_task(struct hclgevf_dev *hdev)
{
#define	HCLGEVF_MAX_RESET_ATTEMPTS_CNT	3

	if (!test_and_clear_bit(HCLGEVF_STATE_RST_SERVICE_SCHED, &hdev->state))
		return;

	down(&hdev->reset_sem);
	set_bit(HCLGEVF_STATE_RST_HANDLING, &hdev->state);

	if (test_and_clear_bit(HCLGEVF_RESET_PENDING,
			       &hdev->reset_state)) {
		/* PF has intimated that it is about to reset the hardware.
		 * We now have to poll & check if hardware has actually
		 * completed the reset sequence. On hardware reset completion,
		 * VF needs to reset the client and ae device.
		 */
		hdev->reset_attempts = 0;

		hdev->last_reset_time = jiffies;
		hdev->reset_type =
			hclgevf_get_reset_level(hdev, &hdev->reset_pending);
		if (hdev->reset_type != HNAE3_NONE_RESET)
			hclgevf_reset(hdev);
	} else if (test_and_clear_bit(HCLGEVF_RESET_REQUESTED,
				      &hdev->reset_state)) {
		/* we could be here when either of below happens:
		 * 1. reset was initiated due to watchdog timeout caused by
		 *    a. IMP was earlier reset and our TX got choked down and
		 *       which resulted in watchdog reacting and inducing VF
		 *       reset. This also means our cmdq would be unreliable.
		 *    b. problem in TX due to other lower layer(example link
		 *       layer not functioning properly etc.)
		 * 2. VF reset might have been initiated due to some config
		 *    change.
		 *
		 * NOTE: Theres no clear way to detect above cases than to react
		 * to the response of PF for this reset request. PF will ack the
		 * 1b and 2. cases but we will not get any intimation about 1a
		 * from PF as cmdq would be in unreliable state i.e. mailbox
		 * communication between PF and VF would be broken.
		 *
		 * if we are never geting into pending state it means either:
		 * 1. PF is not receiving our request which could be due to IMP
		 *    reset
		 * 2. PF is screwed
		 * We cannot do much for 2. but to check first we can try reset
		 * our PCIe + stack and see if it alleviates the problem.
		 */
		if (hdev->reset_attempts > HCLGEVF_MAX_RESET_ATTEMPTS_CNT) {
			/* prepare for full reset of stack + pcie interface */
			set_bit(HNAE3_VF_FULL_RESET, &hdev->reset_pending);

			/* "defer" schedule the reset task again */
			set_bit(HCLGEVF_RESET_PENDING, &hdev->reset_state);
		} else {
			hdev->reset_attempts++;

			set_bit(hdev->reset_level, &hdev->reset_pending);
			set_bit(HCLGEVF_RESET_PENDING, &hdev->reset_state);
		}
		hclgevf_reset_task_schedule(hdev);
	}

	hdev->reset_type = HNAE3_NONE_RESET;
	clear_bit(HCLGEVF_STATE_RST_HANDLING, &hdev->state);
	up(&hdev->reset_sem);
}

static void hclgevf_mailbox_service_task(struct hclgevf_dev *hdev)
{
	if (!test_and_clear_bit(HCLGEVF_STATE_MBX_SERVICE_SCHED, &hdev->state))
		return;

	if (test_and_set_bit(HCLGEVF_STATE_MBX_HANDLING, &hdev->state))
		return;

	hclgevf_mbx_async_handler(hdev);

	clear_bit(HCLGEVF_STATE_MBX_HANDLING, &hdev->state);
}

static void hclgevf_keep_alive(struct hclgevf_dev *hdev)
{
	struct hclge_vf_to_pf_msg send_msg;
	int ret;

	if (test_bit(HCLGE_COMM_STATE_CMD_DISABLE, &hdev->hw.hw.comm_state))
		return;

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_KEEP_ALIVE, 0);
	ret = hclgevf_send_mbx_msg(hdev, &send_msg, false, NULL, 0);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"VF sends keep alive cmd failed(=%d)\n", ret);
}

static void hclgevf_periodic_service_task(struct hclgevf_dev *hdev)
{
	unsigned long delta = round_jiffies_relative(HZ);
	struct hnae3_handle *handle = &hdev->nic;

	if (test_bit(HCLGEVF_STATE_RST_FAIL, &hdev->state))
		return;

	if (time_is_after_jiffies(hdev->last_serv_processed + HZ)) {
		delta = jiffies - hdev->last_serv_processed;

		if (delta < round_jiffies_relative(HZ)) {
			delta = round_jiffies_relative(HZ) - delta;
			goto out;
		}
	}

	hdev->serv_processed_cnt++;
	if (!(hdev->serv_processed_cnt % HCLGEVF_KEEP_ALIVE_TASK_INTERVAL))
		hclgevf_keep_alive(hdev);

	if (test_bit(HCLGEVF_STATE_DOWN, &hdev->state)) {
		hdev->last_serv_processed = jiffies;
		goto out;
	}

	if (!(hdev->serv_processed_cnt % HCLGEVF_STATS_TIMER_INTERVAL))
		hclge_comm_tqps_update_stats(handle, &hdev->hw.hw);

	/* VF does not need to request link status when this bit is set, because
	 * PF will push its link status to VFs when link status changed.
	 */
	if (!test_bit(HCLGEVF_STATE_PF_PUSH_LINK_STATUS, &hdev->state))
		hclgevf_request_link_info(hdev);

	hclgevf_update_link_mode(hdev);

	hclgevf_sync_vlan_filter(hdev);

	hclgevf_sync_mac_table(hdev);

	hclgevf_sync_promisc_mode(hdev);

	hdev->last_serv_processed = jiffies;

out:
	hclgevf_task_schedule(hdev, delta);
}

static void hclgevf_service_task(struct work_struct *work)
{
	struct hclgevf_dev *hdev = container_of(work, struct hclgevf_dev,
						service_task.work);

	hclgevf_reset_service_task(hdev);
	hclgevf_mailbox_service_task(hdev);
	hclgevf_periodic_service_task(hdev);

	/* Handle reset and mbx again in case periodical task delays the
	 * handling by calling hclgevf_task_schedule() in
	 * hclgevf_periodic_service_task()
	 */
	hclgevf_reset_service_task(hdev);
	hclgevf_mailbox_service_task(hdev);
}

static void hclgevf_clear_event_cause(struct hclgevf_dev *hdev, u32 regclr)
{
	hclgevf_write_dev(&hdev->hw, HCLGE_COMM_VECTOR0_CMDQ_SRC_REG, regclr);
}

static enum hclgevf_evt_cause hclgevf_check_evt_cause(struct hclgevf_dev *hdev,
						      u32 *clearval)
{
	u32 val, cmdq_stat_reg, rst_ing_reg;

	/* fetch the events from their corresponding regs */
	cmdq_stat_reg = hclgevf_read_dev(&hdev->hw,
					 HCLGE_COMM_VECTOR0_CMDQ_STATE_REG);
	if (BIT(HCLGEVF_VECTOR0_RST_INT_B) & cmdq_stat_reg) {
		rst_ing_reg = hclgevf_read_dev(&hdev->hw, HCLGEVF_RST_ING);
		dev_info(&hdev->pdev->dev,
			 "receive reset interrupt 0x%x!\n", rst_ing_reg);
		set_bit(HNAE3_VF_RESET, &hdev->reset_pending);
		set_bit(HCLGEVF_RESET_PENDING, &hdev->reset_state);
		set_bit(HCLGE_COMM_STATE_CMD_DISABLE, &hdev->hw.hw.comm_state);
		*clearval = ~(1U << HCLGEVF_VECTOR0_RST_INT_B);
		hdev->rst_stats.vf_rst_cnt++;
		/* set up VF hardware reset status, its PF will clear
		 * this status when PF has initialized done.
		 */
		val = hclgevf_read_dev(&hdev->hw, HCLGEVF_VF_RST_ING);
		hclgevf_write_dev(&hdev->hw, HCLGEVF_VF_RST_ING,
				  val | HCLGEVF_VF_RST_ING_BIT);
		return HCLGEVF_VECTOR0_EVENT_RST;
	}

	/* check for vector0 mailbox(=CMDQ RX) event source */
	if (BIT(HCLGEVF_VECTOR0_RX_CMDQ_INT_B) & cmdq_stat_reg) {
		/* for revision 0x21, clearing interrupt is writing bit 0
		 * to the clear register, writing bit 1 means to keep the
		 * old value.
		 * for revision 0x20, the clear register is a read & write
		 * register, so we should just write 0 to the bit we are
		 * handling, and keep other bits as cmdq_stat_reg.
		 */
		if (hdev->ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V2)
			*clearval = ~(1U << HCLGEVF_VECTOR0_RX_CMDQ_INT_B);
		else
			*clearval = cmdq_stat_reg &
				    ~BIT(HCLGEVF_VECTOR0_RX_CMDQ_INT_B);

		return HCLGEVF_VECTOR0_EVENT_MBX;
	}

	/* print other vector0 event source */
	dev_info(&hdev->pdev->dev,
		 "vector 0 interrupt from unknown source, cmdq_src = %#x\n",
		 cmdq_stat_reg);

	return HCLGEVF_VECTOR0_EVENT_OTHER;
}

static irqreturn_t hclgevf_misc_irq_handle(int irq, void *data)
{
	enum hclgevf_evt_cause event_cause;
	struct hclgevf_dev *hdev = data;
	u32 clearval;

	hclgevf_enable_vector(&hdev->misc_vector, false);
	event_cause = hclgevf_check_evt_cause(hdev, &clearval);
	if (event_cause != HCLGEVF_VECTOR0_EVENT_OTHER)
		hclgevf_clear_event_cause(hdev, clearval);

	switch (event_cause) {
	case HCLGEVF_VECTOR0_EVENT_RST:
		hclgevf_reset_task_schedule(hdev);
		break;
	case HCLGEVF_VECTOR0_EVENT_MBX:
		hclgevf_mbx_handler(hdev);
		break;
	default:
		break;
	}

	hclgevf_enable_vector(&hdev->misc_vector, true);

	return IRQ_HANDLED;
}

static int hclgevf_configure(struct hclgevf_dev *hdev)
{
	int ret;

	hdev->gro_en = true;

	ret = hclgevf_get_basic_info(hdev);
	if (ret)
		return ret;

	/* get current port based vlan state from PF */
	ret = hclgevf_get_port_base_vlan_filter_state(hdev);
	if (ret)
		return ret;

	/* get queue configuration from PF */
	ret = hclgevf_get_queue_info(hdev);
	if (ret)
		return ret;

	/* get queue depth info from PF */
	ret = hclgevf_get_queue_depth(hdev);
	if (ret)
		return ret;

	return hclgevf_get_pf_media_type(hdev);
}

static int hclgevf_alloc_hdev(struct hnae3_ae_dev *ae_dev)
{
	struct pci_dev *pdev = ae_dev->pdev;
	struct hclgevf_dev *hdev;

	hdev = devm_kzalloc(&pdev->dev, sizeof(*hdev), GFP_KERNEL);
	if (!hdev)
		return -ENOMEM;

	hdev->pdev = pdev;
	hdev->ae_dev = ae_dev;
	ae_dev->priv = hdev;

	return 0;
}

static int hclgevf_init_roce_base_info(struct hclgevf_dev *hdev)
{
	struct hnae3_handle *roce = &hdev->roce;
	struct hnae3_handle *nic = &hdev->nic;

	roce->rinfo.num_vectors = hdev->num_roce_msix;

	if (hdev->num_msi_left < roce->rinfo.num_vectors ||
	    hdev->num_msi_left == 0)
		return -EINVAL;

	roce->rinfo.base_vector = hdev->roce_base_msix_offset;

	roce->rinfo.netdev = nic->kinfo.netdev;
	roce->rinfo.roce_io_base = hdev->hw.hw.io_base;
	roce->rinfo.roce_mem_base = hdev->hw.hw.mem_base;

	roce->pdev = nic->pdev;
	roce->ae_algo = nic->ae_algo;
	roce->numa_node_mask = nic->numa_node_mask;

	return 0;
}

static int hclgevf_config_gro(struct hclgevf_dev *hdev)
{
	struct hclgevf_cfg_gro_status_cmd *req;
	struct hclge_desc desc;
	int ret;

	if (!hnae3_dev_gro_supported(hdev))
		return 0;

	hclgevf_cmd_setup_basic_desc(&desc, HCLGE_OPC_GRO_GENERIC_CONFIG,
				     false);
	req = (struct hclgevf_cfg_gro_status_cmd *)desc.data;

	req->gro_en = hdev->gro_en ? 1 : 0;

	ret = hclgevf_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"VF GRO hardware config cmd failed, ret = %d.\n", ret);

	return ret;
}

static int hclgevf_rss_init_hw(struct hclgevf_dev *hdev)
{
	struct hclge_comm_rss_cfg *rss_cfg = &hdev->rss_cfg;
	u16 tc_offset[HCLGE_COMM_MAX_TC_NUM];
	u16 tc_valid[HCLGE_COMM_MAX_TC_NUM];
	u16 tc_size[HCLGE_COMM_MAX_TC_NUM];
	int ret;

	if (hdev->ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V2) {
		ret = hclge_comm_set_rss_algo_key(&hdev->hw.hw,
						  rss_cfg->rss_algo,
						  rss_cfg->rss_hash_key);
		if (ret)
			return ret;

		ret = hclge_comm_set_rss_input_tuple(&hdev->nic, &hdev->hw.hw,
						     false, rss_cfg);
		if (ret)
			return ret;
	}

	ret = hclge_comm_set_rss_indir_table(hdev->ae_dev, &hdev->hw.hw,
					     rss_cfg->rss_indirection_tbl);
	if (ret)
		return ret;

	hclge_comm_get_rss_tc_info(rss_cfg->rss_size, hdev->hw_tc_map,
				   tc_offset, tc_valid, tc_size);

	return hclge_comm_set_rss_tc_mode(&hdev->hw.hw, tc_offset,
					  tc_valid, tc_size);
}

static int hclgevf_init_vlan_config(struct hclgevf_dev *hdev)
{
	struct hnae3_handle *nic = &hdev->nic;
	int ret;

	ret = hclgevf_en_hw_strip_rxvtag(nic, true);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to enable rx vlan offload, ret = %d\n", ret);
		return ret;
	}

	return hclgevf_set_vlan_filter(&hdev->nic, htons(ETH_P_8021Q), 0,
				       false);
}

static void hclgevf_flush_link_update(struct hclgevf_dev *hdev)
{
#define HCLGEVF_FLUSH_LINK_TIMEOUT	100000

	unsigned long last = hdev->serv_processed_cnt;
	int i = 0;

	while (test_bit(HCLGEVF_STATE_LINK_UPDATING, &hdev->state) &&
	       i++ < HCLGEVF_FLUSH_LINK_TIMEOUT &&
	       last == hdev->serv_processed_cnt)
		usleep_range(1, 1);
}

static void hclgevf_set_timer_task(struct hnae3_handle *handle, bool enable)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	if (enable) {
		hclgevf_task_schedule(hdev, 0);
	} else {
		set_bit(HCLGEVF_STATE_DOWN, &hdev->state);

		/* flush memory to make sure DOWN is seen by service task */
		smp_mb__before_atomic();
		hclgevf_flush_link_update(hdev);
	}
}

static int hclgevf_ae_start(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	clear_bit(HCLGEVF_STATE_DOWN, &hdev->state);
	clear_bit(HCLGEVF_STATE_PF_PUSH_LINK_STATUS, &hdev->state);

	hclge_comm_reset_tqp_stats(handle);

	hclgevf_request_link_info(hdev);

	hclgevf_update_link_mode(hdev);

	return 0;
}

static void hclgevf_ae_stop(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	set_bit(HCLGEVF_STATE_DOWN, &hdev->state);

	if (hdev->reset_type != HNAE3_VF_RESET)
		hclgevf_reset_tqp(handle);

	hclge_comm_reset_tqp_stats(handle);
	hclgevf_update_link_status(hdev, 0);
}

static int hclgevf_set_alive(struct hnae3_handle *handle, bool alive)
{
#define HCLGEVF_STATE_ALIVE	1
#define HCLGEVF_STATE_NOT_ALIVE	0

	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hclge_vf_to_pf_msg send_msg;

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_SET_ALIVE, 0);
	send_msg.data[0] = alive ? HCLGEVF_STATE_ALIVE :
				HCLGEVF_STATE_NOT_ALIVE;
	return hclgevf_send_mbx_msg(hdev, &send_msg, false, NULL, 0);
}

static int hclgevf_client_start(struct hnae3_handle *handle)
{
	return hclgevf_set_alive(handle, true);
}

static void hclgevf_client_stop(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	int ret;

	ret = hclgevf_set_alive(handle, false);
	if (ret)
		dev_warn(&hdev->pdev->dev,
			 "%s failed %d\n", __func__, ret);
}

static void hclgevf_state_init(struct hclgevf_dev *hdev)
{
	clear_bit(HCLGEVF_STATE_MBX_SERVICE_SCHED, &hdev->state);
	clear_bit(HCLGEVF_STATE_MBX_HANDLING, &hdev->state);
	clear_bit(HCLGEVF_STATE_RST_FAIL, &hdev->state);

	INIT_DELAYED_WORK(&hdev->service_task, hclgevf_service_task);

	mutex_init(&hdev->mbx_resp.mbx_mutex);
	sema_init(&hdev->reset_sem, 1);

	spin_lock_init(&hdev->mac_table.mac_list_lock);
	INIT_LIST_HEAD(&hdev->mac_table.uc_mac_list);
	INIT_LIST_HEAD(&hdev->mac_table.mc_mac_list);

	/* bring the device down */
	set_bit(HCLGEVF_STATE_DOWN, &hdev->state);
}

static void hclgevf_state_uninit(struct hclgevf_dev *hdev)
{
	set_bit(HCLGEVF_STATE_DOWN, &hdev->state);
	set_bit(HCLGEVF_STATE_REMOVING, &hdev->state);

	if (hdev->service_task.work.func)
		cancel_delayed_work_sync(&hdev->service_task);

	mutex_destroy(&hdev->mbx_resp.mbx_mutex);
}

static int hclgevf_init_msi(struct hclgevf_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	int vectors;
	int i;

	if (hnae3_dev_roce_supported(hdev))
		vectors = pci_alloc_irq_vectors(pdev,
						hdev->roce_base_msix_offset + 1,
						hdev->num_msi,
						PCI_IRQ_MSIX);
	else
		vectors = pci_alloc_irq_vectors(pdev, HNAE3_MIN_VECTOR_NUM,
						hdev->num_msi,
						PCI_IRQ_MSI | PCI_IRQ_MSIX);

	if (vectors < 0) {
		dev_err(&pdev->dev,
			"failed(%d) to allocate MSI/MSI-X vectors\n",
			vectors);
		return vectors;
	}
	if (vectors < hdev->num_msi)
		dev_warn(&hdev->pdev->dev,
			 "requested %u MSI/MSI-X, but allocated %d MSI/MSI-X\n",
			 hdev->num_msi, vectors);

	hdev->num_msi = vectors;
	hdev->num_msi_left = vectors;

	hdev->vector_status = devm_kcalloc(&pdev->dev, hdev->num_msi,
					   sizeof(u16), GFP_KERNEL);
	if (!hdev->vector_status) {
		pci_free_irq_vectors(pdev);
		return -ENOMEM;
	}

	for (i = 0; i < hdev->num_msi; i++)
		hdev->vector_status[i] = HCLGEVF_INVALID_VPORT;

	hdev->vector_irq = devm_kcalloc(&pdev->dev, hdev->num_msi,
					sizeof(int), GFP_KERNEL);
	if (!hdev->vector_irq) {
		devm_kfree(&pdev->dev, hdev->vector_status);
		pci_free_irq_vectors(pdev);
		return -ENOMEM;
	}

	return 0;
}

static void hclgevf_uninit_msi(struct hclgevf_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;

	devm_kfree(&pdev->dev, hdev->vector_status);
	devm_kfree(&pdev->dev, hdev->vector_irq);
	pci_free_irq_vectors(pdev);
}

static int hclgevf_misc_irq_init(struct hclgevf_dev *hdev)
{
	int ret;

	hclgevf_get_misc_vector(hdev);

	snprintf(hdev->misc_vector.name, HNAE3_INT_NAME_LEN, "%s-misc-%s",
		 HCLGEVF_NAME, pci_name(hdev->pdev));
	ret = request_irq(hdev->misc_vector.vector_irq, hclgevf_misc_irq_handle,
			  0, hdev->misc_vector.name, hdev);
	if (ret) {
		dev_err(&hdev->pdev->dev, "VF failed to request misc irq(%d)\n",
			hdev->misc_vector.vector_irq);
		return ret;
	}

	hclgevf_clear_event_cause(hdev, 0);

	/* enable misc. vector(vector 0) */
	hclgevf_enable_vector(&hdev->misc_vector, true);

	return ret;
}

static void hclgevf_misc_irq_uninit(struct hclgevf_dev *hdev)
{
	/* disable misc vector(vector 0) */
	hclgevf_enable_vector(&hdev->misc_vector, false);
	synchronize_irq(hdev->misc_vector.vector_irq);
	free_irq(hdev->misc_vector.vector_irq, hdev);
	hclgevf_free_vector(hdev, 0);
}

static void hclgevf_info_show(struct hclgevf_dev *hdev)
{
	struct device *dev = &hdev->pdev->dev;

	dev_info(dev, "VF info begin:\n");

	dev_info(dev, "Task queue pairs numbers: %u\n", hdev->num_tqps);
	dev_info(dev, "Desc num per TX queue: %u\n", hdev->num_tx_desc);
	dev_info(dev, "Desc num per RX queue: %u\n", hdev->num_rx_desc);
	dev_info(dev, "Numbers of vports: %u\n", hdev->num_alloc_vport);
	dev_info(dev, "HW tc map: 0x%x\n", hdev->hw_tc_map);
	dev_info(dev, "PF media type of this VF: %u\n",
		 hdev->hw.mac.media_type);

	dev_info(dev, "VF info end.\n");
}

static int hclgevf_init_nic_client_instance(struct hnae3_ae_dev *ae_dev,
					    struct hnae3_client *client)
{
	struct hclgevf_dev *hdev = ae_dev->priv;
	int rst_cnt = hdev->rst_stats.rst_cnt;
	int ret;

	ret = client->ops->init_instance(&hdev->nic);
	if (ret)
		return ret;

	set_bit(HCLGEVF_STATE_NIC_REGISTERED, &hdev->state);
	if (test_bit(HCLGEVF_STATE_RST_HANDLING, &hdev->state) ||
	    rst_cnt != hdev->rst_stats.rst_cnt) {
		clear_bit(HCLGEVF_STATE_NIC_REGISTERED, &hdev->state);

		client->ops->uninit_instance(&hdev->nic, 0);
		return -EBUSY;
	}

	hnae3_set_client_init_flag(client, ae_dev, 1);

	if (netif_msg_drv(&hdev->nic))
		hclgevf_info_show(hdev);

	return 0;
}

static int hclgevf_init_roce_client_instance(struct hnae3_ae_dev *ae_dev,
					     struct hnae3_client *client)
{
	struct hclgevf_dev *hdev = ae_dev->priv;
	int ret;

	if (!hnae3_dev_roce_supported(hdev) || !hdev->roce_client ||
	    !hdev->nic_client)
		return 0;

	ret = hclgevf_init_roce_base_info(hdev);
	if (ret)
		return ret;

	ret = client->ops->init_instance(&hdev->roce);
	if (ret)
		return ret;

	set_bit(HCLGEVF_STATE_ROCE_REGISTERED, &hdev->state);
	hnae3_set_client_init_flag(client, ae_dev, 1);

	return 0;
}

static int hclgevf_init_client_instance(struct hnae3_client *client,
					struct hnae3_ae_dev *ae_dev)
{
	struct hclgevf_dev *hdev = ae_dev->priv;
	int ret;

	switch (client->type) {
	case HNAE3_CLIENT_KNIC:
		hdev->nic_client = client;
		hdev->nic.client = client;

		ret = hclgevf_init_nic_client_instance(ae_dev, client);
		if (ret)
			goto clear_nic;

		ret = hclgevf_init_roce_client_instance(ae_dev,
							hdev->roce_client);
		if (ret)
			goto clear_roce;

		break;
	case HNAE3_CLIENT_ROCE:
		if (hnae3_dev_roce_supported(hdev)) {
			hdev->roce_client = client;
			hdev->roce.client = client;
		}

		ret = hclgevf_init_roce_client_instance(ae_dev, client);
		if (ret)
			goto clear_roce;

		break;
	default:
		return -EINVAL;
	}

	return 0;

clear_nic:
	hdev->nic_client = NULL;
	hdev->nic.client = NULL;
	return ret;
clear_roce:
	hdev->roce_client = NULL;
	hdev->roce.client = NULL;
	return ret;
}

static void hclgevf_uninit_client_instance(struct hnae3_client *client,
					   struct hnae3_ae_dev *ae_dev)
{
	struct hclgevf_dev *hdev = ae_dev->priv;

	/* un-init roce, if it exists */
	if (hdev->roce_client) {
		while (test_bit(HCLGEVF_STATE_RST_HANDLING, &hdev->state))
			msleep(HCLGEVF_WAIT_RESET_DONE);
		clear_bit(HCLGEVF_STATE_ROCE_REGISTERED, &hdev->state);

		hdev->roce_client->ops->uninit_instance(&hdev->roce, 0);
		hdev->roce_client = NULL;
		hdev->roce.client = NULL;
	}

	/* un-init nic/unic, if this was not called by roce client */
	if (client->ops->uninit_instance && hdev->nic_client &&
	    client->type != HNAE3_CLIENT_ROCE) {
		while (test_bit(HCLGEVF_STATE_RST_HANDLING, &hdev->state))
			msleep(HCLGEVF_WAIT_RESET_DONE);
		clear_bit(HCLGEVF_STATE_NIC_REGISTERED, &hdev->state);

		client->ops->uninit_instance(&hdev->nic, 0);
		hdev->nic_client = NULL;
		hdev->nic.client = NULL;
	}
}

static int hclgevf_dev_mem_map(struct hclgevf_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	struct hclgevf_hw *hw = &hdev->hw;

	/* for device does not have device memory, return directly */
	if (!(pci_select_bars(pdev, IORESOURCE_MEM) & BIT(HCLGEVF_MEM_BAR)))
		return 0;

	hw->hw.mem_base =
		devm_ioremap_wc(&pdev->dev,
				pci_resource_start(pdev, HCLGEVF_MEM_BAR),
				pci_resource_len(pdev, HCLGEVF_MEM_BAR));
	if (!hw->hw.mem_base) {
		dev_err(&pdev->dev, "failed to map device memory\n");
		return -EFAULT;
	}

	return 0;
}

static int hclgevf_pci_init(struct hclgevf_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	struct hclgevf_hw *hw;
	int ret;

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable PCI device\n");
		return ret;
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pdev->dev, "can't set consistent PCI DMA, exiting");
		goto err_disable_device;
	}

	ret = pci_request_regions(pdev, HCLGEVF_DRIVER_NAME);
	if (ret) {
		dev_err(&pdev->dev, "PCI request regions failed %d\n", ret);
		goto err_disable_device;
	}

	pci_set_master(pdev);
	hw = &hdev->hw;
	hw->hw.io_base = pci_iomap(pdev, 2, 0);
	if (!hw->hw.io_base) {
		dev_err(&pdev->dev, "can't map configuration register space\n");
		ret = -ENOMEM;
		goto err_clr_master;
	}

	ret = hclgevf_dev_mem_map(hdev);
	if (ret)
		goto err_unmap_io_base;

	return 0;

err_unmap_io_base:
	pci_iounmap(pdev, hdev->hw.hw.io_base);
err_clr_master:
	pci_clear_master(pdev);
	pci_release_regions(pdev);
err_disable_device:
	pci_disable_device(pdev);

	return ret;
}

static void hclgevf_pci_uninit(struct hclgevf_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;

	if (hdev->hw.hw.mem_base)
		devm_iounmap(&pdev->dev, hdev->hw.hw.mem_base);

	pci_iounmap(pdev, hdev->hw.hw.io_base);
	pci_clear_master(pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static int hclgevf_query_vf_resource(struct hclgevf_dev *hdev)
{
	struct hclgevf_query_res_cmd *req;
	struct hclge_desc desc;
	int ret;

	hclgevf_cmd_setup_basic_desc(&desc, HCLGE_OPC_QUERY_VF_RSRC, true);
	ret = hclgevf_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"query vf resource failed, ret = %d.\n", ret);
		return ret;
	}

	req = (struct hclgevf_query_res_cmd *)desc.data;

	if (hnae3_dev_roce_supported(hdev)) {
		hdev->roce_base_msix_offset =
		hnae3_get_field(le16_to_cpu(req->msixcap_localid_ba_rocee),
				HCLGEVF_MSIX_OFT_ROCEE_M,
				HCLGEVF_MSIX_OFT_ROCEE_S);
		hdev->num_roce_msix =
		hnae3_get_field(le16_to_cpu(req->vf_intr_vector_number),
				HCLGEVF_VEC_NUM_M, HCLGEVF_VEC_NUM_S);

		/* nic's msix numbers is always equals to the roce's. */
		hdev->num_nic_msix = hdev->num_roce_msix;

		/* VF should have NIC vectors and Roce vectors, NIC vectors
		 * are queued before Roce vectors. The offset is fixed to 64.
		 */
		hdev->num_msi = hdev->num_roce_msix +
				hdev->roce_base_msix_offset;
	} else {
		hdev->num_msi =
		hnae3_get_field(le16_to_cpu(req->vf_intr_vector_number),
				HCLGEVF_VEC_NUM_M, HCLGEVF_VEC_NUM_S);

		hdev->num_nic_msix = hdev->num_msi;
	}

	if (hdev->num_nic_msix < HNAE3_MIN_VECTOR_NUM) {
		dev_err(&hdev->pdev->dev,
			"Just %u msi resources, not enough for vf(min:2).\n",
			hdev->num_nic_msix);
		return -EINVAL;
	}

	return 0;
}

static void hclgevf_set_default_dev_specs(struct hclgevf_dev *hdev)
{
#define HCLGEVF_MAX_NON_TSO_BD_NUM			8U

	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(hdev->pdev);

	ae_dev->dev_specs.max_non_tso_bd_num =
					HCLGEVF_MAX_NON_TSO_BD_NUM;
	ae_dev->dev_specs.rss_ind_tbl_size = HCLGEVF_RSS_IND_TBL_SIZE;
	ae_dev->dev_specs.rss_key_size = HCLGE_COMM_RSS_KEY_SIZE;
	ae_dev->dev_specs.max_int_gl = HCLGEVF_DEF_MAX_INT_GL;
	ae_dev->dev_specs.max_frm_size = HCLGEVF_MAC_MAX_FRAME;
}

static void hclgevf_parse_dev_specs(struct hclgevf_dev *hdev,
				    struct hclge_desc *desc)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(hdev->pdev);
	struct hclgevf_dev_specs_0_cmd *req0;
	struct hclgevf_dev_specs_1_cmd *req1;

	req0 = (struct hclgevf_dev_specs_0_cmd *)desc[0].data;
	req1 = (struct hclgevf_dev_specs_1_cmd *)desc[1].data;

	ae_dev->dev_specs.max_non_tso_bd_num = req0->max_non_tso_bd_num;
	ae_dev->dev_specs.rss_ind_tbl_size =
					le16_to_cpu(req0->rss_ind_tbl_size);
	ae_dev->dev_specs.int_ql_max = le16_to_cpu(req0->int_ql_max);
	ae_dev->dev_specs.rss_key_size = le16_to_cpu(req0->rss_key_size);
	ae_dev->dev_specs.max_int_gl = le16_to_cpu(req1->max_int_gl);
	ae_dev->dev_specs.max_frm_size = le16_to_cpu(req1->max_frm_size);
}

static void hclgevf_check_dev_specs(struct hclgevf_dev *hdev)
{
	struct hnae3_dev_specs *dev_specs = &hdev->ae_dev->dev_specs;

	if (!dev_specs->max_non_tso_bd_num)
		dev_specs->max_non_tso_bd_num = HCLGEVF_MAX_NON_TSO_BD_NUM;
	if (!dev_specs->rss_ind_tbl_size)
		dev_specs->rss_ind_tbl_size = HCLGEVF_RSS_IND_TBL_SIZE;
	if (!dev_specs->rss_key_size)
		dev_specs->rss_key_size = HCLGE_COMM_RSS_KEY_SIZE;
	if (!dev_specs->max_int_gl)
		dev_specs->max_int_gl = HCLGEVF_DEF_MAX_INT_GL;
	if (!dev_specs->max_frm_size)
		dev_specs->max_frm_size = HCLGEVF_MAC_MAX_FRAME;
}

static int hclgevf_query_dev_specs(struct hclgevf_dev *hdev)
{
	struct hclge_desc desc[HCLGEVF_QUERY_DEV_SPECS_BD_NUM];
	int ret;
	int i;

	/* set default specifications as devices lower than version V3 do not
	 * support querying specifications from firmware.
	 */
	if (hdev->ae_dev->dev_version < HNAE3_DEVICE_VERSION_V3) {
		hclgevf_set_default_dev_specs(hdev);
		return 0;
	}

	for (i = 0; i < HCLGEVF_QUERY_DEV_SPECS_BD_NUM - 1; i++) {
		hclgevf_cmd_setup_basic_desc(&desc[i],
					     HCLGE_OPC_QUERY_DEV_SPECS, true);
		desc[i].flag |= cpu_to_le16(HCLGE_COMM_CMD_FLAG_NEXT);
	}
	hclgevf_cmd_setup_basic_desc(&desc[i], HCLGE_OPC_QUERY_DEV_SPECS, true);

	ret = hclgevf_cmd_send(&hdev->hw, desc, HCLGEVF_QUERY_DEV_SPECS_BD_NUM);
	if (ret)
		return ret;

	hclgevf_parse_dev_specs(hdev, desc);
	hclgevf_check_dev_specs(hdev);

	return 0;
}

static int hclgevf_pci_reset(struct hclgevf_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	int ret = 0;

	if (hdev->reset_type == HNAE3_VF_FULL_RESET &&
	    test_bit(HCLGEVF_STATE_IRQ_INITED, &hdev->state)) {
		hclgevf_misc_irq_uninit(hdev);
		hclgevf_uninit_msi(hdev);
		clear_bit(HCLGEVF_STATE_IRQ_INITED, &hdev->state);
	}

	if (!test_bit(HCLGEVF_STATE_IRQ_INITED, &hdev->state)) {
		pci_set_master(pdev);
		ret = hclgevf_init_msi(hdev);
		if (ret) {
			dev_err(&pdev->dev,
				"failed(%d) to init MSI/MSI-X\n", ret);
			return ret;
		}

		ret = hclgevf_misc_irq_init(hdev);
		if (ret) {
			hclgevf_uninit_msi(hdev);
			dev_err(&pdev->dev, "failed(%d) to init Misc IRQ(vector0)\n",
				ret);
			return ret;
		}

		set_bit(HCLGEVF_STATE_IRQ_INITED, &hdev->state);
	}

	return ret;
}

static int hclgevf_clear_vport_list(struct hclgevf_dev *hdev)
{
	struct hclge_vf_to_pf_msg send_msg;

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_HANDLE_VF_TBL,
			       HCLGE_MBX_VPORT_LIST_CLEAR);
	return hclgevf_send_mbx_msg(hdev, &send_msg, false, NULL, 0);
}

static void hclgevf_init_rxd_adv_layout(struct hclgevf_dev *hdev)
{
	if (hnae3_ae_dev_rxd_adv_layout_supported(hdev->ae_dev))
		hclgevf_write_dev(&hdev->hw, HCLGEVF_RXD_ADV_LAYOUT_EN_REG, 1);
}

static void hclgevf_uninit_rxd_adv_layout(struct hclgevf_dev *hdev)
{
	if (hnae3_ae_dev_rxd_adv_layout_supported(hdev->ae_dev))
		hclgevf_write_dev(&hdev->hw, HCLGEVF_RXD_ADV_LAYOUT_EN_REG, 0);
}

static int hclgevf_reset_hdev(struct hclgevf_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	int ret;

	ret = hclgevf_pci_reset(hdev);
	if (ret) {
		dev_err(&pdev->dev, "pci reset failed %d\n", ret);
		return ret;
	}

	hclgevf_arq_init(hdev);
	ret = hclge_comm_cmd_init(hdev->ae_dev, &hdev->hw.hw,
				  &hdev->fw_version, false,
				  hdev->reset_pending);
	if (ret) {
		dev_err(&pdev->dev, "cmd failed %d\n", ret);
		return ret;
	}

	ret = hclgevf_rss_init_hw(hdev);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed(%d) to initialize RSS\n", ret);
		return ret;
	}

	ret = hclgevf_config_gro(hdev);
	if (ret)
		return ret;

	ret = hclgevf_init_vlan_config(hdev);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed(%d) to initialize VLAN config\n", ret);
		return ret;
	}

	set_bit(HCLGEVF_STATE_PROMISC_CHANGED, &hdev->state);

	hclgevf_init_rxd_adv_layout(hdev);

	dev_info(&hdev->pdev->dev, "Reset done\n");

	return 0;
}

static int hclgevf_init_hdev(struct hclgevf_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	int ret;

	ret = hclgevf_pci_init(hdev);
	if (ret)
		return ret;

	ret = hclgevf_devlink_init(hdev);
	if (ret)
		goto err_devlink_init;

	ret = hclge_comm_cmd_queue_init(hdev->pdev, &hdev->hw.hw);
	if (ret)
		goto err_cmd_queue_init;

	hclgevf_arq_init(hdev);
	ret = hclge_comm_cmd_init(hdev->ae_dev, &hdev->hw.hw,
				  &hdev->fw_version, false,
				  hdev->reset_pending);
	if (ret)
		goto err_cmd_init;

	/* Get vf resource */
	ret = hclgevf_query_vf_resource(hdev);
	if (ret)
		goto err_cmd_init;

	ret = hclgevf_query_dev_specs(hdev);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to query dev specifications, ret = %d\n", ret);
		goto err_cmd_init;
	}

	ret = hclgevf_init_msi(hdev);
	if (ret) {
		dev_err(&pdev->dev, "failed(%d) to init MSI/MSI-X\n", ret);
		goto err_cmd_init;
	}

	hclgevf_state_init(hdev);
	hdev->reset_level = HNAE3_VF_FUNC_RESET;
	hdev->reset_type = HNAE3_NONE_RESET;

	ret = hclgevf_misc_irq_init(hdev);
	if (ret)
		goto err_misc_irq_init;

	set_bit(HCLGEVF_STATE_IRQ_INITED, &hdev->state);

	ret = hclgevf_configure(hdev);
	if (ret) {
		dev_err(&pdev->dev, "failed(%d) to fetch configuration\n", ret);
		goto err_config;
	}

	ret = hclgevf_alloc_tqps(hdev);
	if (ret) {
		dev_err(&pdev->dev, "failed(%d) to allocate TQPs\n", ret);
		goto err_config;
	}

	ret = hclgevf_set_handle_info(hdev);
	if (ret)
		goto err_config;

	ret = hclgevf_config_gro(hdev);
	if (ret)
		goto err_config;

	/* Initialize RSS for this VF */
	ret = hclge_comm_rss_init_cfg(&hdev->nic, hdev->ae_dev,
				      &hdev->rss_cfg);
	if (ret) {
		dev_err(&pdev->dev, "failed to init rss cfg, ret = %d\n", ret);
		goto err_config;
	}

	ret = hclgevf_rss_init_hw(hdev);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed(%d) to initialize RSS\n", ret);
		goto err_config;
	}

	/* ensure vf tbl list as empty before init*/
	ret = hclgevf_clear_vport_list(hdev);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to clear tbl list configuration, ret = %d.\n",
			ret);
		goto err_config;
	}

	ret = hclgevf_init_vlan_config(hdev);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed(%d) to initialize VLAN config\n", ret);
		goto err_config;
	}

	hclgevf_init_rxd_adv_layout(hdev);

	set_bit(HCLGEVF_STATE_SERVICE_INITED, &hdev->state);

	hdev->last_reset_time = jiffies;
	dev_info(&hdev->pdev->dev, "finished initializing %s driver\n",
		 HCLGEVF_DRIVER_NAME);

	hclgevf_task_schedule(hdev, round_jiffies_relative(HZ));

	return 0;

err_config:
	hclgevf_misc_irq_uninit(hdev);
err_misc_irq_init:
	hclgevf_state_uninit(hdev);
	hclgevf_uninit_msi(hdev);
err_cmd_init:
	hclge_comm_cmd_uninit(hdev->ae_dev, &hdev->hw.hw);
err_cmd_queue_init:
	hclgevf_devlink_uninit(hdev);
err_devlink_init:
	hclgevf_pci_uninit(hdev);
	clear_bit(HCLGEVF_STATE_IRQ_INITED, &hdev->state);
	return ret;
}

static void hclgevf_uninit_hdev(struct hclgevf_dev *hdev)
{
	struct hclge_vf_to_pf_msg send_msg;

	hclgevf_state_uninit(hdev);
	hclgevf_uninit_rxd_adv_layout(hdev);

	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_VF_UNINIT, 0);
	hclgevf_send_mbx_msg(hdev, &send_msg, false, NULL, 0);

	if (test_bit(HCLGEVF_STATE_IRQ_INITED, &hdev->state)) {
		hclgevf_misc_irq_uninit(hdev);
		hclgevf_uninit_msi(hdev);
	}

	hclge_comm_cmd_uninit(hdev->ae_dev, &hdev->hw.hw);
	hclgevf_devlink_uninit(hdev);
	hclgevf_pci_uninit(hdev);
	hclgevf_uninit_mac_list(hdev);
}

static int hclgevf_init_ae_dev(struct hnae3_ae_dev *ae_dev)
{
	struct pci_dev *pdev = ae_dev->pdev;
	int ret;

	ret = hclgevf_alloc_hdev(ae_dev);
	if (ret) {
		dev_err(&pdev->dev, "hclge device allocation failed\n");
		return ret;
	}

	ret = hclgevf_init_hdev(ae_dev->priv);
	if (ret) {
		dev_err(&pdev->dev, "hclge device initialization failed\n");
		return ret;
	}

	return 0;
}

static void hclgevf_uninit_ae_dev(struct hnae3_ae_dev *ae_dev)
{
	struct hclgevf_dev *hdev = ae_dev->priv;

	hclgevf_uninit_hdev(hdev);
	ae_dev->priv = NULL;
}

static u32 hclgevf_get_max_channels(struct hclgevf_dev *hdev)
{
	struct hnae3_handle *nic = &hdev->nic;
	struct hnae3_knic_private_info *kinfo = &nic->kinfo;

	return min_t(u32, hdev->rss_size_max,
		     hdev->num_tqps / kinfo->tc_info.num_tc);
}

/**
 * hclgevf_get_channels - Get the current channels enabled and max supported.
 * @handle: hardware information for network interface
 * @ch: ethtool channels structure
 *
 * We don't support separate tx and rx queues as channels. The other count
 * represents how many queues are being used for control. max_combined counts
 * how many queue pairs we can support. They may not be mapped 1 to 1 with
 * q_vectors since we support a lot more queue pairs than q_vectors.
 **/
static void hclgevf_get_channels(struct hnae3_handle *handle,
				 struct ethtool_channels *ch)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	ch->max_combined = hclgevf_get_max_channels(hdev);
	ch->other_count = 0;
	ch->max_other = 0;
	ch->combined_count = handle->kinfo.rss_size;
}

static void hclgevf_get_tqps_and_rss_info(struct hnae3_handle *handle,
					  u16 *alloc_tqps, u16 *max_rss_size)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	*alloc_tqps = hdev->num_tqps;
	*max_rss_size = hdev->rss_size_max;
}

static void hclgevf_update_rss_size(struct hnae3_handle *handle,
				    u32 new_tqps_num)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	u16 max_rss_size;

	kinfo->req_rss_size = new_tqps_num;

	max_rss_size = min_t(u16, hdev->rss_size_max,
			     hdev->num_tqps / kinfo->tc_info.num_tc);

	/* Use the user's configuration when it is not larger than
	 * max_rss_size, otherwise, use the maximum specification value.
	 */
	if (kinfo->req_rss_size != kinfo->rss_size && kinfo->req_rss_size &&
	    kinfo->req_rss_size <= max_rss_size)
		kinfo->rss_size = kinfo->req_rss_size;
	else if (kinfo->rss_size > max_rss_size ||
		 (!kinfo->req_rss_size && kinfo->rss_size < max_rss_size))
		kinfo->rss_size = max_rss_size;

	kinfo->num_tqps = kinfo->tc_info.num_tc * kinfo->rss_size;
}

static int hclgevf_set_channels(struct hnae3_handle *handle, u32 new_tqps_num,
				bool rxfh_configured)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	u16 tc_offset[HCLGE_COMM_MAX_TC_NUM];
	u16 tc_valid[HCLGE_COMM_MAX_TC_NUM];
	u16 tc_size[HCLGE_COMM_MAX_TC_NUM];
	u16 cur_rss_size = kinfo->rss_size;
	u16 cur_tqps = kinfo->num_tqps;
	u32 *rss_indir;
	unsigned int i;
	int ret;

	hclgevf_update_rss_size(handle, new_tqps_num);

	hclge_comm_get_rss_tc_info(cur_rss_size, hdev->hw_tc_map,
				   tc_offset, tc_valid, tc_size);
	ret = hclge_comm_set_rss_tc_mode(&hdev->hw.hw, tc_offset,
					 tc_valid, tc_size);
	if (ret)
		return ret;

	/* RSS indirection table has been configured by user */
	if (rxfh_configured)
		goto out;

	/* Reinitializes the rss indirect table according to the new RSS size */
	rss_indir = kcalloc(hdev->ae_dev->dev_specs.rss_ind_tbl_size,
			    sizeof(u32), GFP_KERNEL);
	if (!rss_indir)
		return -ENOMEM;

	for (i = 0; i < hdev->ae_dev->dev_specs.rss_ind_tbl_size; i++)
		rss_indir[i] = i % kinfo->rss_size;

	hdev->rss_cfg.rss_size = kinfo->rss_size;

	ret = hclgevf_set_rss(handle, rss_indir, NULL, 0);
	if (ret)
		dev_err(&hdev->pdev->dev, "set rss indir table fail, ret=%d\n",
			ret);

	kfree(rss_indir);

out:
	if (!ret)
		dev_info(&hdev->pdev->dev,
			 "Channels changed, rss_size from %u to %u, tqps from %u to %u",
			 cur_rss_size, kinfo->rss_size,
			 cur_tqps, kinfo->rss_size * kinfo->tc_info.num_tc);

	return ret;
}

static int hclgevf_get_status(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	return hdev->hw.mac.link;
}

static void hclgevf_get_ksettings_an_result(struct hnae3_handle *handle,
					    u8 *auto_neg, u32 *speed,
					    u8 *duplex)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	if (speed)
		*speed = hdev->hw.mac.speed;
	if (duplex)
		*duplex = hdev->hw.mac.duplex;
	if (auto_neg)
		*auto_neg = AUTONEG_DISABLE;
}

void hclgevf_update_speed_duplex(struct hclgevf_dev *hdev, u32 speed,
				 u8 duplex)
{
	hdev->hw.mac.speed = speed;
	hdev->hw.mac.duplex = duplex;
}

static int hclgevf_gro_en(struct hnae3_handle *handle, bool enable)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	bool gro_en_old = hdev->gro_en;
	int ret;

	hdev->gro_en = enable;
	ret = hclgevf_config_gro(hdev);
	if (ret)
		hdev->gro_en = gro_en_old;

	return ret;
}

static void hclgevf_get_media_type(struct hnae3_handle *handle, u8 *media_type,
				   u8 *module_type)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	if (media_type)
		*media_type = hdev->hw.mac.media_type;

	if (module_type)
		*module_type = hdev->hw.mac.module_type;
}

static bool hclgevf_get_hw_reset_stat(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	return !!hclgevf_read_dev(&hdev->hw, HCLGEVF_RST_ING);
}

static bool hclgevf_get_cmdq_stat(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	return test_bit(HCLGE_COMM_STATE_CMD_DISABLE, &hdev->hw.hw.comm_state);
}

static bool hclgevf_ae_dev_resetting(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	return test_bit(HCLGEVF_STATE_RST_HANDLING, &hdev->state);
}

static unsigned long hclgevf_ae_dev_reset_cnt(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	return hdev->rst_stats.hw_rst_done_cnt;
}

static void hclgevf_get_link_mode(struct hnae3_handle *handle,
				  unsigned long *supported,
				  unsigned long *advertising)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	*supported = hdev->hw.mac.supported;
	*advertising = hdev->hw.mac.advertising;
}

#define MAX_SEPARATE_NUM	4
#define SEPARATOR_VALUE		0xFDFCFBFA
#define REG_NUM_PER_LINE	4
#define REG_LEN_PER_LINE	(REG_NUM_PER_LINE * sizeof(u32))

static int hclgevf_get_regs_len(struct hnae3_handle *handle)
{
	int cmdq_lines, common_lines, ring_lines, tqp_intr_lines;
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	cmdq_lines = sizeof(cmdq_reg_addr_list) / REG_LEN_PER_LINE + 1;
	common_lines = sizeof(common_reg_addr_list) / REG_LEN_PER_LINE + 1;
	ring_lines = sizeof(ring_reg_addr_list) / REG_LEN_PER_LINE + 1;
	tqp_intr_lines = sizeof(tqp_intr_reg_addr_list) / REG_LEN_PER_LINE + 1;

	return (cmdq_lines + common_lines + ring_lines * hdev->num_tqps +
		tqp_intr_lines * (hdev->num_msi_used - 1)) * REG_LEN_PER_LINE;
}

static void hclgevf_get_regs(struct hnae3_handle *handle, u32 *version,
			     void *data)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	int i, j, reg_um, separator_num;
	u32 *reg = data;

	*version = hdev->fw_version;

	/* fetching per-VF registers values from VF PCIe register space */
	reg_um = sizeof(cmdq_reg_addr_list) / sizeof(u32);
	separator_num = MAX_SEPARATE_NUM - reg_um % REG_NUM_PER_LINE;
	for (i = 0; i < reg_um; i++)
		*reg++ = hclgevf_read_dev(&hdev->hw, cmdq_reg_addr_list[i]);
	for (i = 0; i < separator_num; i++)
		*reg++ = SEPARATOR_VALUE;

	reg_um = sizeof(common_reg_addr_list) / sizeof(u32);
	separator_num = MAX_SEPARATE_NUM - reg_um % REG_NUM_PER_LINE;
	for (i = 0; i < reg_um; i++)
		*reg++ = hclgevf_read_dev(&hdev->hw, common_reg_addr_list[i]);
	for (i = 0; i < separator_num; i++)
		*reg++ = SEPARATOR_VALUE;

	reg_um = sizeof(ring_reg_addr_list) / sizeof(u32);
	separator_num = MAX_SEPARATE_NUM - reg_um % REG_NUM_PER_LINE;
	for (j = 0; j < hdev->num_tqps; j++) {
		for (i = 0; i < reg_um; i++)
			*reg++ = hclgevf_read_dev(&hdev->hw,
						  ring_reg_addr_list[i] +
						  0x200 * j);
		for (i = 0; i < separator_num; i++)
			*reg++ = SEPARATOR_VALUE;
	}

	reg_um = sizeof(tqp_intr_reg_addr_list) / sizeof(u32);
	separator_num = MAX_SEPARATE_NUM - reg_um % REG_NUM_PER_LINE;
	for (j = 0; j < hdev->num_msi_used - 1; j++) {
		for (i = 0; i < reg_um; i++)
			*reg++ = hclgevf_read_dev(&hdev->hw,
						  tqp_intr_reg_addr_list[i] +
						  4 * j);
		for (i = 0; i < separator_num; i++)
			*reg++ = SEPARATOR_VALUE;
	}
}

void hclgevf_update_port_base_vlan_info(struct hclgevf_dev *hdev, u16 state,
					u8 *port_base_vlan_info, u8 data_size)
{
	struct hnae3_handle *nic = &hdev->nic;
	struct hclge_vf_to_pf_msg send_msg;
	int ret;

	rtnl_lock();

	if (test_bit(HCLGEVF_STATE_RST_HANDLING, &hdev->state) ||
	    test_bit(HCLGEVF_STATE_RST_FAIL, &hdev->state)) {
		dev_warn(&hdev->pdev->dev,
			 "is resetting when updating port based vlan info\n");
		rtnl_unlock();
		return;
	}

	ret = hclgevf_notify_client(hdev, HNAE3_DOWN_CLIENT);
	if (ret) {
		rtnl_unlock();
		return;
	}

	/* send msg to PF and wait update port based vlan info */
	hclgevf_build_send_msg(&send_msg, HCLGE_MBX_SET_VLAN,
			       HCLGE_MBX_PORT_BASE_VLAN_CFG);
	memcpy(send_msg.data, port_base_vlan_info, data_size);
	ret = hclgevf_send_mbx_msg(hdev, &send_msg, false, NULL, 0);
	if (!ret) {
		if (state == HNAE3_PORT_BASE_VLAN_DISABLE)
			nic->port_base_vlan_state = state;
		else
			nic->port_base_vlan_state = HNAE3_PORT_BASE_VLAN_ENABLE;
	}

	hclgevf_notify_client(hdev, HNAE3_UP_CLIENT);
	rtnl_unlock();
}

static const struct hnae3_ae_ops hclgevf_ops = {
	.init_ae_dev = hclgevf_init_ae_dev,
	.uninit_ae_dev = hclgevf_uninit_ae_dev,
	.reset_prepare = hclgevf_reset_prepare_general,
	.reset_done = hclgevf_reset_done,
	.init_client_instance = hclgevf_init_client_instance,
	.uninit_client_instance = hclgevf_uninit_client_instance,
	.start = hclgevf_ae_start,
	.stop = hclgevf_ae_stop,
	.client_start = hclgevf_client_start,
	.client_stop = hclgevf_client_stop,
	.map_ring_to_vector = hclgevf_map_ring_to_vector,
	.unmap_ring_from_vector = hclgevf_unmap_ring_from_vector,
	.get_vector = hclgevf_get_vector,
	.put_vector = hclgevf_put_vector,
	.reset_queue = hclgevf_reset_tqp,
	.get_mac_addr = hclgevf_get_mac_addr,
	.set_mac_addr = hclgevf_set_mac_addr,
	.add_uc_addr = hclgevf_add_uc_addr,
	.rm_uc_addr = hclgevf_rm_uc_addr,
	.add_mc_addr = hclgevf_add_mc_addr,
	.rm_mc_addr = hclgevf_rm_mc_addr,
	.get_stats = hclgevf_get_stats,
	.update_stats = hclgevf_update_stats,
	.get_strings = hclgevf_get_strings,
	.get_sset_count = hclgevf_get_sset_count,
	.get_rss_key_size = hclge_comm_get_rss_key_size,
	.get_rss = hclgevf_get_rss,
	.set_rss = hclgevf_set_rss,
	.get_rss_tuple = hclgevf_get_rss_tuple,
	.set_rss_tuple = hclgevf_set_rss_tuple,
	.get_tc_size = hclgevf_get_tc_size,
	.get_fw_version = hclgevf_get_fw_version,
	.set_vlan_filter = hclgevf_set_vlan_filter,
	.enable_vlan_filter = hclgevf_enable_vlan_filter,
	.enable_hw_strip_rxvtag = hclgevf_en_hw_strip_rxvtag,
	.reset_event = hclgevf_reset_event,
	.set_default_reset_request = hclgevf_set_def_reset_request,
	.set_channels = hclgevf_set_channels,
	.get_channels = hclgevf_get_channels,
	.get_tqps_and_rss_info = hclgevf_get_tqps_and_rss_info,
	.get_regs_len = hclgevf_get_regs_len,
	.get_regs = hclgevf_get_regs,
	.get_status = hclgevf_get_status,
	.get_ksettings_an_result = hclgevf_get_ksettings_an_result,
	.get_media_type = hclgevf_get_media_type,
	.get_hw_reset_stat = hclgevf_get_hw_reset_stat,
	.ae_dev_resetting = hclgevf_ae_dev_resetting,
	.ae_dev_reset_cnt = hclgevf_ae_dev_reset_cnt,
	.set_gro_en = hclgevf_gro_en,
	.set_mtu = hclgevf_set_mtu,
	.get_global_queue_id = hclgevf_get_qid_global,
	.set_timer_task = hclgevf_set_timer_task,
	.get_link_mode = hclgevf_get_link_mode,
	.set_promisc_mode = hclgevf_set_promisc_mode,
	.request_update_promisc_mode = hclgevf_request_update_promisc_mode,
	.get_cmdq_stat = hclgevf_get_cmdq_stat,
};

static struct hnae3_ae_algo ae_algovf = {
	.ops = &hclgevf_ops,
	.pdev_id_table = ae_algovf_pci_tbl,
};

static int hclgevf_init(void)
{
	pr_info("%s is initializing\n", HCLGEVF_NAME);

	hclgevf_wq = alloc_workqueue("%s", WQ_UNBOUND, 0, HCLGEVF_NAME);
	if (!hclgevf_wq) {
		pr_err("%s: failed to create workqueue\n", HCLGEVF_NAME);
		return -ENOMEM;
	}

	hnae3_register_ae_algo(&ae_algovf);

	return 0;
}

static void hclgevf_exit(void)
{
	hnae3_unregister_ae_algo(&ae_algovf);
	destroy_workqueue(hclgevf_wq);
}
module_init(hclgevf_init);
module_exit(hclgevf_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huawei Tech. Co., Ltd.");
MODULE_DESCRIPTION("HCLGEVF Driver");
MODULE_VERSION(HCLGEVF_MOD_VERSION);
