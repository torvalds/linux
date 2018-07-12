// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2016-2017 Hisilicon Limited.

#include <linux/etherdevice.h>
#include <net/rtnetlink.h>
#include "hclgevf_cmd.h"
#include "hclgevf_main.h"
#include "hclge_mbx.h"
#include "hnae3.h"

#define HCLGEVF_NAME	"hclgevf"

static int hclgevf_init_hdev(struct hclgevf_dev *hdev);
static void hclgevf_uninit_hdev(struct hclgevf_dev *hdev);
static struct hnae3_ae_algo ae_algovf;

static const struct pci_device_id ae_algovf_pci_tbl[] = {
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_100G_VF), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_100G_RDMA_DCB_PFC_VF), 0},
	/* required last entry */
	{0, }
};

MODULE_DEVICE_TABLE(pci, ae_algovf_pci_tbl);

static inline struct hclgevf_dev *hclgevf_ae_get_hdev(
	struct hnae3_handle *handle)
{
	return container_of(handle, struct hclgevf_dev, nic);
}

static int hclgevf_tqps_update_stats(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hnae3_queue *queue;
	struct hclgevf_desc desc;
	struct hclgevf_tqp *tqp;
	int status;
	int i;

	for (i = 0; i < hdev->num_tqps; i++) {
		queue = handle->kinfo.tqp[i];
		tqp = container_of(queue, struct hclgevf_tqp, q);
		hclgevf_cmd_setup_basic_desc(&desc,
					     HCLGEVF_OPC_QUERY_RX_STATUS,
					     true);

		desc.data[0] = cpu_to_le32(tqp->index & 0x1ff);
		status = hclgevf_cmd_send(&hdev->hw, &desc, 1);
		if (status) {
			dev_err(&hdev->pdev->dev,
				"Query tqp stat fail, status = %d,queue = %d\n",
				status,	i);
			return status;
		}
		tqp->tqp_stats.rcb_rx_ring_pktnum_rcd +=
			le32_to_cpu(desc.data[1]);

		hclgevf_cmd_setup_basic_desc(&desc, HCLGEVF_OPC_QUERY_TX_STATUS,
					     true);

		desc.data[0] = cpu_to_le32(tqp->index & 0x1ff);
		status = hclgevf_cmd_send(&hdev->hw, &desc, 1);
		if (status) {
			dev_err(&hdev->pdev->dev,
				"Query tqp stat fail, status = %d,queue = %d\n",
				status, i);
			return status;
		}
		tqp->tqp_stats.rcb_tx_ring_pktnum_rcd +=
			le32_to_cpu(desc.data[1]);
	}

	return 0;
}

static u64 *hclgevf_tqps_get_stats(struct hnae3_handle *handle, u64 *data)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hclgevf_tqp *tqp;
	u64 *buff = data;
	int i;

	for (i = 0; i < hdev->num_tqps; i++) {
		tqp = container_of(handle->kinfo.tqp[i], struct hclgevf_tqp, q);
		*buff++ = tqp->tqp_stats.rcb_tx_ring_pktnum_rcd;
	}
	for (i = 0; i < kinfo->num_tqps; i++) {
		tqp = container_of(handle->kinfo.tqp[i], struct hclgevf_tqp, q);
		*buff++ = tqp->tqp_stats.rcb_rx_ring_pktnum_rcd;
	}

	return buff;
}

static int hclgevf_tqps_get_sset_count(struct hnae3_handle *handle, int strset)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	return hdev->num_tqps * 2;
}

static u8 *hclgevf_tqps_get_strings(struct hnae3_handle *handle, u8 *data)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	u8 *buff = data;
	int i = 0;

	for (i = 0; i < hdev->num_tqps; i++) {
		struct hclgevf_tqp *tqp = container_of(handle->kinfo.tqp[i],
			struct hclgevf_tqp, q);
		snprintf(buff, ETH_GSTRING_LEN, "txq#%d_pktnum_rcd",
			 tqp->index);
		buff += ETH_GSTRING_LEN;
	}

	for (i = 0; i < hdev->num_tqps; i++) {
		struct hclgevf_tqp *tqp = container_of(handle->kinfo.tqp[i],
			struct hclgevf_tqp, q);
		snprintf(buff, ETH_GSTRING_LEN, "rxq#%d_pktnum_rcd",
			 tqp->index);
		buff += ETH_GSTRING_LEN;
	}

	return buff;
}

static void hclgevf_update_stats(struct hnae3_handle *handle,
				 struct net_device_stats *net_stats)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	int status;

	status = hclgevf_tqps_update_stats(handle);
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
		return hclgevf_tqps_get_sset_count(handle, strset);

	return 0;
}

static void hclgevf_get_strings(struct hnae3_handle *handle, u32 strset,
				u8 *data)
{
	u8 *p = (char *)data;

	if (strset == ETH_SS_STATS)
		p = hclgevf_tqps_get_strings(handle, p);
}

static void hclgevf_get_stats(struct hnae3_handle *handle, u64 *data)
{
	hclgevf_tqps_get_stats(handle, data);
}

static int hclgevf_get_tc_info(struct hclgevf_dev *hdev)
{
	u8 resp_msg;
	int status;

	status = hclgevf_send_mbx_msg(hdev, HCLGE_MBX_GET_TCINFO, 0, NULL, 0,
				      true, &resp_msg, sizeof(u8));
	if (status) {
		dev_err(&hdev->pdev->dev,
			"VF request to get TC info from PF failed %d",
			status);
		return status;
	}

	hdev->hw_tc_map = resp_msg;

	return 0;
}

static int hclge_get_queue_info(struct hclgevf_dev *hdev)
{
#define HCLGEVF_TQPS_RSS_INFO_LEN	8
	u8 resp_msg[HCLGEVF_TQPS_RSS_INFO_LEN];
	int status;

	status = hclgevf_send_mbx_msg(hdev, HCLGE_MBX_GET_QINFO, 0, NULL, 0,
				      true, resp_msg,
				      HCLGEVF_TQPS_RSS_INFO_LEN);
	if (status) {
		dev_err(&hdev->pdev->dev,
			"VF request to get tqp info from PF failed %d",
			status);
		return status;
	}

	memcpy(&hdev->num_tqps, &resp_msg[0], sizeof(u16));
	memcpy(&hdev->rss_size_max, &resp_msg[2], sizeof(u16));
	memcpy(&hdev->num_desc, &resp_msg[4], sizeof(u16));
	memcpy(&hdev->rx_buf_len, &resp_msg[6], sizeof(u16));

	return 0;
}

static int hclgevf_alloc_tqps(struct hclgevf_dev *hdev)
{
	struct hclgevf_tqp *tqp;
	int i;

	/* if this is on going reset then we need to re-allocate the TPQs
	 * since we cannot assume we would get same number of TPQs back from PF
	 */
	if (hclgevf_dev_ongoing_reset(hdev))
		devm_kfree(&hdev->pdev->dev, hdev->htqp);

	hdev->htqp = devm_kcalloc(&hdev->pdev->dev, hdev->num_tqps,
				  sizeof(struct hclgevf_tqp), GFP_KERNEL);
	if (!hdev->htqp)
		return -ENOMEM;

	tqp = hdev->htqp;

	for (i = 0; i < hdev->num_tqps; i++) {
		tqp->dev = &hdev->pdev->dev;
		tqp->index = i;

		tqp->q.ae_algo = &ae_algovf;
		tqp->q.buf_size = hdev->rx_buf_len;
		tqp->q.desc_num = hdev->num_desc;
		tqp->q.io_base = hdev->hw.io_base + HCLGEVF_TQP_REG_OFFSET +
			i * HCLGEVF_TQP_REG_SIZE;

		tqp++;
	}

	return 0;
}

static int hclgevf_knic_setup(struct hclgevf_dev *hdev)
{
	struct hnae3_handle *nic = &hdev->nic;
	struct hnae3_knic_private_info *kinfo;
	u16 new_tqps = hdev->num_tqps;
	int i;

	kinfo = &nic->kinfo;
	kinfo->num_tc = 0;
	kinfo->num_desc = hdev->num_desc;
	kinfo->rx_buf_len = hdev->rx_buf_len;
	for (i = 0; i < HCLGEVF_MAX_TC_NUM; i++)
		if (hdev->hw_tc_map & BIT(i))
			kinfo->num_tc++;

	kinfo->rss_size
		= min_t(u16, hdev->rss_size_max, new_tqps / kinfo->num_tc);
	new_tqps = kinfo->rss_size * kinfo->num_tc;
	kinfo->num_tqps = min(new_tqps, hdev->num_tqps);

	/* if this is on going reset then we need to re-allocate the hnae queues
	 * as well since number of TPQs from PF might have changed.
	 */
	if (hclgevf_dev_ongoing_reset(hdev))
		devm_kfree(&hdev->pdev->dev, kinfo->tqp);

	kinfo->tqp = devm_kcalloc(&hdev->pdev->dev, kinfo->num_tqps,
				  sizeof(struct hnae3_queue *), GFP_KERNEL);
	if (!kinfo->tqp)
		return -ENOMEM;

	for (i = 0; i < kinfo->num_tqps; i++) {
		hdev->htqp[i].q.handle = &hdev->nic;
		hdev->htqp[i].q.tqp_index = i;
		kinfo->tqp[i] = &hdev->htqp[i].q;
	}

	return 0;
}

static void hclgevf_request_link_info(struct hclgevf_dev *hdev)
{
	int status;
	u8 resp_msg;

	status = hclgevf_send_mbx_msg(hdev, HCLGE_MBX_GET_LINK_STATUS, 0, NULL,
				      0, false, &resp_msg, sizeof(u8));
	if (status)
		dev_err(&hdev->pdev->dev,
			"VF failed to fetch link status(%d) from PF", status);
}

void hclgevf_update_link_status(struct hclgevf_dev *hdev, int link_state)
{
	struct hnae3_handle *handle = &hdev->nic;
	struct hnae3_client *client;

	client = handle->client;

	if (link_state != hdev->hw.mac.link) {
		client->ops->link_status_change(handle, !!link_state);
		hdev->hw.mac.link = link_state;
	}
}

static int hclgevf_set_handle_info(struct hclgevf_dev *hdev)
{
	struct hnae3_handle *nic = &hdev->nic;
	int ret;

	nic->ae_algo = &ae_algovf;
	nic->pdev = hdev->pdev;
	nic->numa_node_mask = hdev->numa_node_mask;
	nic->flags |= HNAE3_SUPPORT_VF;

	if (hdev->ae_dev->dev_type != HNAE3_DEV_KNIC) {
		dev_err(&hdev->pdev->dev, "unsupported device type %d\n",
			hdev->ae_dev->dev_type);
		return -EINVAL;
	}

	ret = hclgevf_knic_setup(hdev);
	if (ret)
		dev_err(&hdev->pdev->dev, "VF knic setup failed %d\n",
			ret);
	return ret;
}

static void hclgevf_free_vector(struct hclgevf_dev *hdev, int vector_id)
{
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

	vector_num = min(hdev->num_msi_left, vector_num);

	for (j = 0; j < vector_num; j++) {
		for (i = HCLGEVF_MISC_VECTOR_NUM + 1; i < hdev->num_msi; i++) {
			if (hdev->vector_status[i] == HCLGEVF_INVALID_VPORT) {
				vector->vector = pci_irq_vector(hdev->pdev, i);
				vector->io_addr = hdev->hw.io_base +
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

static u32 hclgevf_get_rss_key_size(struct hnae3_handle *handle)
{
	return HCLGEVF_RSS_KEY_SIZE;
}

static u32 hclgevf_get_rss_indir_size(struct hnae3_handle *handle)
{
	return HCLGEVF_RSS_IND_TBL_SIZE;
}

static int hclgevf_set_rss_indir_table(struct hclgevf_dev *hdev)
{
	const u8 *indir = hdev->rss_cfg.rss_indirection_tbl;
	struct hclgevf_rss_indirection_table_cmd *req;
	struct hclgevf_desc desc;
	int status;
	int i, j;

	req = (struct hclgevf_rss_indirection_table_cmd *)desc.data;

	for (i = 0; i < HCLGEVF_RSS_CFG_TBL_NUM; i++) {
		hclgevf_cmd_setup_basic_desc(&desc, HCLGEVF_OPC_RSS_INDIR_TABLE,
					     false);
		req->start_table_index = i * HCLGEVF_RSS_CFG_TBL_SIZE;
		req->rss_set_bitmap = HCLGEVF_RSS_SET_BITMAP_MSK;
		for (j = 0; j < HCLGEVF_RSS_CFG_TBL_SIZE; j++)
			req->rss_result[j] =
				indir[i * HCLGEVF_RSS_CFG_TBL_SIZE + j];

		status = hclgevf_cmd_send(&hdev->hw, &desc, 1);
		if (status) {
			dev_err(&hdev->pdev->dev,
				"VF failed(=%d) to set RSS indirection table\n",
				status);
			return status;
		}
	}

	return 0;
}

static int hclgevf_set_rss_tc_mode(struct hclgevf_dev *hdev,  u16 rss_size)
{
	struct hclgevf_rss_tc_mode_cmd *req;
	u16 tc_offset[HCLGEVF_MAX_TC_NUM];
	u16 tc_valid[HCLGEVF_MAX_TC_NUM];
	u16 tc_size[HCLGEVF_MAX_TC_NUM];
	struct hclgevf_desc desc;
	u16 roundup_size;
	int status;
	int i;

	req = (struct hclgevf_rss_tc_mode_cmd *)desc.data;

	roundup_size = roundup_pow_of_two(rss_size);
	roundup_size = ilog2(roundup_size);

	for (i = 0; i < HCLGEVF_MAX_TC_NUM; i++) {
		tc_valid[i] = !!(hdev->hw_tc_map & BIT(i));
		tc_size[i] = roundup_size;
		tc_offset[i] = rss_size * i;
	}

	hclgevf_cmd_setup_basic_desc(&desc, HCLGEVF_OPC_RSS_TC_MODE, false);
	for (i = 0; i < HCLGEVF_MAX_TC_NUM; i++) {
		hnae_set_bit(req->rss_tc_mode[i], HCLGEVF_RSS_TC_VALID_B,
			     (tc_valid[i] & 0x1));
		hnae_set_field(req->rss_tc_mode[i], HCLGEVF_RSS_TC_SIZE_M,
			       HCLGEVF_RSS_TC_SIZE_S, tc_size[i]);
		hnae_set_field(req->rss_tc_mode[i], HCLGEVF_RSS_TC_OFFSET_M,
			       HCLGEVF_RSS_TC_OFFSET_S, tc_offset[i]);
	}
	status = hclgevf_cmd_send(&hdev->hw, &desc, 1);
	if (status)
		dev_err(&hdev->pdev->dev,
			"VF failed(=%d) to set rss tc mode\n", status);

	return status;
}

static int hclgevf_get_rss_hw_cfg(struct hnae3_handle *handle, u8 *hash,
				  u8 *key)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hclgevf_rss_config_cmd *req;
	int lkup_times = key ? 3 : 1;
	struct hclgevf_desc desc;
	int key_offset;
	int key_size;
	int status;

	req = (struct hclgevf_rss_config_cmd *)desc.data;
	lkup_times = (lkup_times == 3) ? 3 : ((hash) ? 1 : 0);

	for (key_offset = 0; key_offset < lkup_times; key_offset++) {
		hclgevf_cmd_setup_basic_desc(&desc,
					     HCLGEVF_OPC_RSS_GENERIC_CONFIG,
					     true);
		req->hash_config |= (key_offset << HCLGEVF_RSS_HASH_KEY_OFFSET);

		status = hclgevf_cmd_send(&hdev->hw, &desc, 1);
		if (status) {
			dev_err(&hdev->pdev->dev,
				"failed to get hardware RSS cfg, status = %d\n",
				status);
			return status;
		}

		if (key_offset == 2)
			key_size =
			HCLGEVF_RSS_KEY_SIZE - HCLGEVF_RSS_HASH_KEY_NUM * 2;
		else
			key_size = HCLGEVF_RSS_HASH_KEY_NUM;

		if (key)
			memcpy(key + key_offset * HCLGEVF_RSS_HASH_KEY_NUM,
			       req->hash_key,
			       key_size);
	}

	if (hash) {
		if ((req->hash_config & 0xf) == HCLGEVF_RSS_HASH_ALGO_TOEPLITZ)
			*hash = ETH_RSS_HASH_TOP;
		else
			*hash = ETH_RSS_HASH_UNKNOWN;
	}

	return 0;
}

static int hclgevf_get_rss(struct hnae3_handle *handle, u32 *indir, u8 *key,
			   u8 *hfunc)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hclgevf_rss_cfg *rss_cfg = &hdev->rss_cfg;
	int i;

	if (indir)
		for (i = 0; i < HCLGEVF_RSS_IND_TBL_SIZE; i++)
			indir[i] = rss_cfg->rss_indirection_tbl[i];

	return hclgevf_get_rss_hw_cfg(handle, hfunc, key);
}

static int hclgevf_set_rss(struct hnae3_handle *handle, const u32 *indir,
			   const  u8 *key, const  u8 hfunc)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hclgevf_rss_cfg *rss_cfg = &hdev->rss_cfg;
	int i;

	/* update the shadow RSS table with user specified qids */
	for (i = 0; i < HCLGEVF_RSS_IND_TBL_SIZE; i++)
		rss_cfg->rss_indirection_tbl[i] = indir[i];

	/* update the hardware */
	return hclgevf_set_rss_indir_table(hdev);
}

static int hclgevf_get_tc_size(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hclgevf_rss_cfg *rss_cfg = &hdev->rss_cfg;

	return rss_cfg->rss_size;
}

static int hclgevf_bind_ring_to_vector(struct hnae3_handle *handle, bool en,
				       int vector,
				       struct hnae3_ring_chain_node *ring_chain)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hnae3_ring_chain_node *node;
	struct hclge_mbx_vf_to_pf_cmd *req;
	struct hclgevf_desc desc;
	int i = 0, vector_id;
	int status;
	u8 type;

	req = (struct hclge_mbx_vf_to_pf_cmd *)desc.data;
	vector_id = hclgevf_get_vector_index(hdev, vector);
	if (vector_id < 0) {
		dev_err(&handle->pdev->dev,
			"Get vector index fail. ret =%d\n", vector_id);
		return vector_id;
	}

	for (node = ring_chain; node; node = node->next) {
		int idx_offset = HCLGE_MBX_RING_MAP_BASIC_MSG_NUM +
					HCLGE_MBX_RING_NODE_VARIABLE_NUM * i;

		if (i == 0) {
			hclgevf_cmd_setup_basic_desc(&desc,
						     HCLGEVF_OPC_MBX_VF_TO_PF,
						     false);
			type = en ?
				HCLGE_MBX_MAP_RING_TO_VECTOR :
				HCLGE_MBX_UNMAP_RING_TO_VECTOR;
			req->msg[0] = type;
			req->msg[1] = vector_id;
		}

		req->msg[idx_offset] =
				hnae_get_bit(node->flag, HNAE3_RING_TYPE_B);
		req->msg[idx_offset + 1] = node->tqp_index;
		req->msg[idx_offset + 2] = hnae_get_field(node->int_gl_idx,
							  HNAE3_RING_GL_IDX_M,
							  HNAE3_RING_GL_IDX_S);

		i++;
		if ((i == (HCLGE_MBX_VF_MSG_DATA_NUM -
		     HCLGE_MBX_RING_MAP_BASIC_MSG_NUM) /
		     HCLGE_MBX_RING_NODE_VARIABLE_NUM) ||
		    !node->next) {
			req->msg[2] = i;

			status = hclgevf_cmd_send(&hdev->hw, &desc, 1);
			if (status) {
				dev_err(&hdev->pdev->dev,
					"Map TQP fail, status is %d.\n",
					status);
				return status;
			}
			i = 0;
			hclgevf_cmd_setup_basic_desc(&desc,
						     HCLGEVF_OPC_MBX_VF_TO_PF,
						     false);
			req->msg[0] = type;
			req->msg[1] = vector_id;
		}
	}

	return 0;
}

static int hclgevf_map_ring_to_vector(struct hnae3_handle *handle, int vector,
				      struct hnae3_ring_chain_node *ring_chain)
{
	return hclgevf_bind_ring_to_vector(handle, true, vector, ring_chain);
}

static int hclgevf_unmap_ring_from_vector(
				struct hnae3_handle *handle,
				int vector,
				struct hnae3_ring_chain_node *ring_chain)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	int ret, vector_id;

	vector_id = hclgevf_get_vector_index(hdev, vector);
	if (vector_id < 0) {
		dev_err(&handle->pdev->dev,
			"Get vector index fail. ret =%d\n", vector_id);
		return vector_id;
	}

	ret = hclgevf_bind_ring_to_vector(handle, false, vector, ring_chain);
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

	hclgevf_free_vector(hdev, vector);

	return 0;
}

static int hclgevf_cmd_set_promisc_mode(struct hclgevf_dev *hdev, u32 en)
{
	struct hclge_mbx_vf_to_pf_cmd *req;
	struct hclgevf_desc desc;
	int status;

	req = (struct hclge_mbx_vf_to_pf_cmd *)desc.data;

	hclgevf_cmd_setup_basic_desc(&desc, HCLGEVF_OPC_MBX_VF_TO_PF, false);
	req->msg[0] = HCLGE_MBX_SET_PROMISC_MODE;
	req->msg[1] = en;

	status = hclgevf_cmd_send(&hdev->hw, &desc, 1);
	if (status)
		dev_err(&hdev->pdev->dev,
			"Set promisc mode fail, status is %d.\n", status);

	return status;
}

static void hclgevf_set_promisc_mode(struct hnae3_handle *handle, u32 en)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	hclgevf_cmd_set_promisc_mode(hdev, en);
}

static int hclgevf_tqp_enable(struct hclgevf_dev *hdev, int tqp_id,
			      int stream_id, bool enable)
{
	struct hclgevf_cfg_com_tqp_queue_cmd *req;
	struct hclgevf_desc desc;
	int status;

	req = (struct hclgevf_cfg_com_tqp_queue_cmd *)desc.data;

	hclgevf_cmd_setup_basic_desc(&desc, HCLGEVF_OPC_CFG_COM_TQP_QUEUE,
				     false);
	req->tqp_id = cpu_to_le16(tqp_id & HCLGEVF_RING_ID_MASK);
	req->stream_id = cpu_to_le16(stream_id);
	req->enable |= enable << HCLGEVF_TQP_ENABLE_B;

	status = hclgevf_cmd_send(&hdev->hw, &desc, 1);
	if (status)
		dev_err(&hdev->pdev->dev,
			"TQP enable fail, status =%d.\n", status);

	return status;
}

static int hclgevf_get_queue_id(struct hnae3_queue *queue)
{
	struct hclgevf_tqp *tqp = container_of(queue, struct hclgevf_tqp, q);

	return tqp->index;
}

static void hclgevf_reset_tqp_stats(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	struct hnae3_queue *queue;
	struct hclgevf_tqp *tqp;
	int i;

	for (i = 0; i < hdev->num_tqps; i++) {
		queue = handle->kinfo.tqp[i];
		tqp = container_of(queue, struct hclgevf_tqp, q);
		memset(&tqp->tqp_stats, 0, sizeof(tqp->tqp_stats));
	}
}

static int hclgevf_cfg_func_mta_filter(struct hnae3_handle *handle, bool en)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	u8 msg[2] = {0};

	msg[0] = en;
	return hclgevf_send_mbx_msg(hdev, HCLGE_MBX_SET_MULTICAST,
				    HCLGE_MBX_MAC_VLAN_MC_FUNC_MTA_ENABLE,
				    msg, 1, false, NULL, 0);
}

static void hclgevf_get_mac_addr(struct hnae3_handle *handle, u8 *p)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	ether_addr_copy(p, hdev->hw.mac.mac_addr);
}

static int hclgevf_set_mac_addr(struct hnae3_handle *handle, void *p,
				bool is_first)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	u8 *old_mac_addr = (u8 *)hdev->hw.mac.mac_addr;
	u8 *new_mac_addr = (u8 *)p;
	u8 msg_data[ETH_ALEN * 2];
	u16 subcode;
	int status;

	ether_addr_copy(msg_data, new_mac_addr);
	ether_addr_copy(&msg_data[ETH_ALEN], old_mac_addr);

	subcode = is_first ? HCLGE_MBX_MAC_VLAN_UC_ADD :
			HCLGE_MBX_MAC_VLAN_UC_MODIFY;

	status = hclgevf_send_mbx_msg(hdev, HCLGE_MBX_SET_UNICAST,
				      subcode, msg_data, ETH_ALEN * 2,
				      true, NULL, 0);
	if (!status)
		ether_addr_copy(hdev->hw.mac.mac_addr, new_mac_addr);

	return status;
}

static int hclgevf_add_uc_addr(struct hnae3_handle *handle,
			       const unsigned char *addr)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	return hclgevf_send_mbx_msg(hdev, HCLGE_MBX_SET_UNICAST,
				    HCLGE_MBX_MAC_VLAN_UC_ADD,
				    addr, ETH_ALEN, false, NULL, 0);
}

static int hclgevf_rm_uc_addr(struct hnae3_handle *handle,
			      const unsigned char *addr)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	return hclgevf_send_mbx_msg(hdev, HCLGE_MBX_SET_UNICAST,
				    HCLGE_MBX_MAC_VLAN_UC_REMOVE,
				    addr, ETH_ALEN, false, NULL, 0);
}

static int hclgevf_add_mc_addr(struct hnae3_handle *handle,
			       const unsigned char *addr)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	return hclgevf_send_mbx_msg(hdev, HCLGE_MBX_SET_MULTICAST,
				    HCLGE_MBX_MAC_VLAN_MC_ADD,
				    addr, ETH_ALEN, false, NULL, 0);
}

static int hclgevf_rm_mc_addr(struct hnae3_handle *handle,
			      const unsigned char *addr)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	return hclgevf_send_mbx_msg(hdev, HCLGE_MBX_SET_MULTICAST,
				    HCLGE_MBX_MAC_VLAN_MC_REMOVE,
				    addr, ETH_ALEN, false, NULL, 0);
}

static int hclgevf_set_vlan_filter(struct hnae3_handle *handle,
				   __be16 proto, u16 vlan_id,
				   bool is_kill)
{
#define HCLGEVF_VLAN_MBX_MSG_LEN 5
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	u8 msg_data[HCLGEVF_VLAN_MBX_MSG_LEN];

	if (vlan_id > 4095)
		return -EINVAL;

	if (proto != htons(ETH_P_8021Q))
		return -EPROTONOSUPPORT;

	msg_data[0] = is_kill;
	memcpy(&msg_data[1], &vlan_id, sizeof(vlan_id));
	memcpy(&msg_data[3], &proto, sizeof(proto));
	return hclgevf_send_mbx_msg(hdev, HCLGE_MBX_SET_VLAN,
				    HCLGE_MBX_VLAN_FILTER, msg_data,
				    HCLGEVF_VLAN_MBX_MSG_LEN, false, NULL, 0);
}

static void hclgevf_reset_tqp(struct hnae3_handle *handle, u16 queue_id)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	u8 msg_data[2];
	int ret;

	memcpy(&msg_data[0], &queue_id, sizeof(queue_id));

	/* disable vf queue before send queue reset msg to PF */
	ret = hclgevf_tqp_enable(hdev, queue_id, 0, false);
	if (ret)
		return;

	hclgevf_send_mbx_msg(hdev, HCLGE_MBX_QUEUE_RESET, 0, msg_data,
			     2, true, NULL, 0);
}

static int hclgevf_notify_client(struct hclgevf_dev *hdev,
				 enum hnae3_reset_notify_type type)
{
	struct hnae3_client *client = hdev->nic_client;
	struct hnae3_handle *handle = &hdev->nic;

	if (!client->ops->reset_notify)
		return -EOPNOTSUPP;

	return client->ops->reset_notify(handle, type);
}

static int hclgevf_reset_wait(struct hclgevf_dev *hdev)
{
#define HCLGEVF_RESET_WAIT_MS	500
#define HCLGEVF_RESET_WAIT_CNT	20
	u32 val, cnt = 0;

	/* wait to check the hardware reset completion status */
	val = hclgevf_read_dev(&hdev->hw, HCLGEVF_FUN_RST_ING);
	while (hnae_get_bit(val, HCLGEVF_FUN_RST_ING_B) &&
			    (cnt < HCLGEVF_RESET_WAIT_CNT)) {
		msleep(HCLGEVF_RESET_WAIT_MS);
		val = hclgevf_read_dev(&hdev->hw, HCLGEVF_FUN_RST_ING);
		cnt++;
	}

	/* hardware completion status should be available by this time */
	if (cnt >= HCLGEVF_RESET_WAIT_CNT) {
		dev_warn(&hdev->pdev->dev,
			 "could'nt get reset done status from h/w, timeout!\n");
		return -EBUSY;
	}

	/* we will wait a bit more to let reset of the stack to complete. This
	 * might happen in case reset assertion was made by PF. Yes, this also
	 * means we might end up waiting bit more even for VF reset.
	 */
	msleep(5000);

	return 0;
}

static int hclgevf_reset_stack(struct hclgevf_dev *hdev)
{
	int ret;

	/* uninitialize the nic client */
	hclgevf_notify_client(hdev, HNAE3_UNINIT_CLIENT);

	/* re-initialize the hclge device */
	ret = hclgevf_init_hdev(hdev);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"hclge device re-init failed, VF is disabled!\n");
		return ret;
	}

	/* bring up the nic client again */
	hclgevf_notify_client(hdev, HNAE3_INIT_CLIENT);

	return 0;
}

static int hclgevf_reset(struct hclgevf_dev *hdev)
{
	int ret;

	rtnl_lock();

	/* bring down the nic to stop any ongoing TX/RX */
	hclgevf_notify_client(hdev, HNAE3_DOWN_CLIENT);

	/* check if VF could successfully fetch the hardware reset completion
	 * status from the hardware
	 */
	ret = hclgevf_reset_wait(hdev);
	if (ret) {
		/* can't do much in this situation, will disable VF */
		dev_err(&hdev->pdev->dev,
			"VF failed(=%d) to fetch H/W reset completion status\n",
			ret);

		dev_warn(&hdev->pdev->dev, "VF reset failed, disabling VF!\n");
		hclgevf_notify_client(hdev, HNAE3_UNINIT_CLIENT);

		rtnl_unlock();
		return ret;
	}

	/* now, re-initialize the nic client and ae device*/
	ret = hclgevf_reset_stack(hdev);
	if (ret)
		dev_err(&hdev->pdev->dev, "failed to reset VF stack\n");

	/* bring up the nic to enable TX/RX again */
	hclgevf_notify_client(hdev, HNAE3_UP_CLIENT);

	rtnl_unlock();

	return ret;
}

static int hclgevf_do_reset(struct hclgevf_dev *hdev)
{
	int status;
	u8 respmsg;

	status = hclgevf_send_mbx_msg(hdev, HCLGE_MBX_RESET, 0, NULL,
				      0, false, &respmsg, sizeof(u8));
	if (status)
		dev_err(&hdev->pdev->dev,
			"VF reset request to PF failed(=%d)\n", status);

	return status;
}

static void hclgevf_reset_event(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	dev_info(&hdev->pdev->dev, "received reset request from VF enet\n");

	handle->reset_level = HNAE3_VF_RESET;

	/* reset of this VF requested */
	set_bit(HCLGEVF_RESET_REQUESTED, &hdev->reset_state);
	hclgevf_reset_task_schedule(hdev);

	handle->last_reset_time = jiffies;
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
	vector->addr = hdev->hw.io_base + HCLGEVF_MISC_VECTOR_REG_BASE;
	/* vector status always valid for Vector 0 */
	hdev->vector_status[HCLGEVF_MISC_VECTOR_NUM] = 0;
	hdev->vector_irq[HCLGEVF_MISC_VECTOR_NUM] = vector->vector_irq;

	hdev->num_msi_left -= 1;
	hdev->num_msi_used += 1;
}

void hclgevf_reset_task_schedule(struct hclgevf_dev *hdev)
{
	if (!test_bit(HCLGEVF_STATE_RST_SERVICE_SCHED, &hdev->state) &&
	    !test_bit(HCLGEVF_STATE_RST_HANDLING, &hdev->state)) {
		set_bit(HCLGEVF_STATE_RST_SERVICE_SCHED, &hdev->state);
		schedule_work(&hdev->rst_service_task);
	}
}

void hclgevf_mbx_task_schedule(struct hclgevf_dev *hdev)
{
	if (!test_bit(HCLGEVF_STATE_MBX_SERVICE_SCHED, &hdev->state) &&
	    !test_bit(HCLGEVF_STATE_MBX_HANDLING, &hdev->state)) {
		set_bit(HCLGEVF_STATE_MBX_SERVICE_SCHED, &hdev->state);
		schedule_work(&hdev->mbx_service_task);
	}
}

static void hclgevf_task_schedule(struct hclgevf_dev *hdev)
{
	if (!test_bit(HCLGEVF_STATE_DOWN, &hdev->state)  &&
	    !test_and_set_bit(HCLGEVF_STATE_SERVICE_SCHED, &hdev->state))
		schedule_work(&hdev->service_task);
}

static void hclgevf_deferred_task_schedule(struct hclgevf_dev *hdev)
{
	/* if we have any pending mailbox event then schedule the mbx task */
	if (hdev->mbx_event_pending)
		hclgevf_mbx_task_schedule(hdev);

	if (test_bit(HCLGEVF_RESET_PENDING, &hdev->reset_state))
		hclgevf_reset_task_schedule(hdev);
}

static void hclgevf_service_timer(struct timer_list *t)
{
	struct hclgevf_dev *hdev = from_timer(hdev, t, service_timer);

	mod_timer(&hdev->service_timer, jiffies + 5 * HZ);

	hclgevf_task_schedule(hdev);
}

static void hclgevf_reset_service_task(struct work_struct *work)
{
	struct hclgevf_dev *hdev =
		container_of(work, struct hclgevf_dev, rst_service_task);
	int ret;

	if (test_and_set_bit(HCLGEVF_STATE_RST_HANDLING, &hdev->state))
		return;

	clear_bit(HCLGEVF_STATE_RST_SERVICE_SCHED, &hdev->state);

	if (test_and_clear_bit(HCLGEVF_RESET_PENDING,
			       &hdev->reset_state)) {
		/* PF has initmated that it is about to reset the hardware.
		 * We now have to poll & check if harware has actually completed
		 * the reset sequence. On hardware reset completion, VF needs to
		 * reset the client and ae device.
		 */
		hdev->reset_attempts = 0;

		ret = hclgevf_reset(hdev);
		if (ret)
			dev_err(&hdev->pdev->dev, "VF stack reset failed.\n");
	} else if (test_and_clear_bit(HCLGEVF_RESET_REQUESTED,
				      &hdev->reset_state)) {
		/* we could be here when either of below happens:
		 * 1. reset was initiated due to watchdog timeout due to
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
		 */

		/* if we are never geting into pending state it means either:
		 * 1. PF is not receiving our request which could be due to IMP
		 *    reset
		 * 2. PF is screwed
		 * We cannot do much for 2. but to check first we can try reset
		 * our PCIe + stack and see if it alleviates the problem.
		 */
		if (hdev->reset_attempts > 3) {
			/* prepare for full reset of stack + pcie interface */
			hdev->nic.reset_level = HNAE3_VF_FULL_RESET;

			/* "defer" schedule the reset task again */
			set_bit(HCLGEVF_RESET_PENDING, &hdev->reset_state);
		} else {
			hdev->reset_attempts++;

			/* request PF for resetting this VF via mailbox */
			ret = hclgevf_do_reset(hdev);
			if (ret)
				dev_warn(&hdev->pdev->dev,
					 "VF rst fail, stack will call\n");
		}
	}

	clear_bit(HCLGEVF_STATE_RST_HANDLING, &hdev->state);
}

static void hclgevf_mailbox_service_task(struct work_struct *work)
{
	struct hclgevf_dev *hdev;

	hdev = container_of(work, struct hclgevf_dev, mbx_service_task);

	if (test_and_set_bit(HCLGEVF_STATE_MBX_HANDLING, &hdev->state))
		return;

	clear_bit(HCLGEVF_STATE_MBX_SERVICE_SCHED, &hdev->state);

	hclgevf_mbx_async_handler(hdev);

	clear_bit(HCLGEVF_STATE_MBX_HANDLING, &hdev->state);
}

static void hclgevf_service_task(struct work_struct *work)
{
	struct hclgevf_dev *hdev;

	hdev = container_of(work, struct hclgevf_dev, service_task);

	/* request the link status from the PF. PF would be able to tell VF
	 * about such updates in future so we might remove this later
	 */
	hclgevf_request_link_info(hdev);

	hclgevf_deferred_task_schedule(hdev);

	clear_bit(HCLGEVF_STATE_SERVICE_SCHED, &hdev->state);
}

static void hclgevf_clear_event_cause(struct hclgevf_dev *hdev, u32 regclr)
{
	hclgevf_write_dev(&hdev->hw, HCLGEVF_VECTOR0_CMDQ_SRC_REG, regclr);
}

static bool hclgevf_check_event_cause(struct hclgevf_dev *hdev, u32 *clearval)
{
	u32 cmdq_src_reg;

	/* fetch the events from their corresponding regs */
	cmdq_src_reg = hclgevf_read_dev(&hdev->hw,
					HCLGEVF_VECTOR0_CMDQ_SRC_REG);

	/* check for vector0 mailbox(=CMDQ RX) event source */
	if (BIT(HCLGEVF_VECTOR0_RX_CMDQ_INT_B) & cmdq_src_reg) {
		cmdq_src_reg &= ~BIT(HCLGEVF_VECTOR0_RX_CMDQ_INT_B);
		*clearval = cmdq_src_reg;
		return true;
	}

	dev_dbg(&hdev->pdev->dev, "vector 0 interrupt from unknown source\n");

	return false;
}

static void hclgevf_enable_vector(struct hclgevf_misc_vector *vector, bool en)
{
	writel(en ? 1 : 0, vector->addr);
}

static irqreturn_t hclgevf_misc_irq_handle(int irq, void *data)
{
	struct hclgevf_dev *hdev = data;
	u32 clearval;

	hclgevf_enable_vector(&hdev->misc_vector, false);
	if (!hclgevf_check_event_cause(hdev, &clearval))
		goto skip_sched;

	hclgevf_mbx_handler(hdev);

	hclgevf_clear_event_cause(hdev, clearval);

skip_sched:
	hclgevf_enable_vector(&hdev->misc_vector, true);

	return IRQ_HANDLED;
}

static int hclgevf_configure(struct hclgevf_dev *hdev)
{
	int ret;

	/* get queue configuration from PF */
	ret = hclge_get_queue_info(hdev);
	if (ret)
		return ret;
	/* get tc configuration from PF */
	return hclgevf_get_tc_info(hdev);
}

static int hclgevf_alloc_hdev(struct hnae3_ae_dev *ae_dev)
{
	struct pci_dev *pdev = ae_dev->pdev;
	struct hclgevf_dev *hdev = ae_dev->priv;

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

	roce->rinfo.num_vectors = HCLGEVF_ROCEE_VECTOR_NUM;

	if (hdev->num_msi_left < roce->rinfo.num_vectors ||
	    hdev->num_msi_left == 0)
		return -EINVAL;

	roce->rinfo.base_vector =
		hdev->vector_status[hdev->num_msi_used];

	roce->rinfo.netdev = nic->kinfo.netdev;
	roce->rinfo.roce_io_base = hdev->hw.io_base;

	roce->pdev = nic->pdev;
	roce->ae_algo = nic->ae_algo;
	roce->numa_node_mask = nic->numa_node_mask;

	return 0;
}

static int hclgevf_rss_init_hw(struct hclgevf_dev *hdev)
{
	struct hclgevf_rss_cfg *rss_cfg = &hdev->rss_cfg;
	int i, ret;

	rss_cfg->rss_size = hdev->rss_size_max;

	/* Initialize RSS indirect table for each vport */
	for (i = 0; i < HCLGEVF_RSS_IND_TBL_SIZE; i++)
		rss_cfg->rss_indirection_tbl[i] = i % hdev->rss_size_max;

	ret = hclgevf_set_rss_indir_table(hdev);
	if (ret)
		return ret;

	return hclgevf_set_rss_tc_mode(hdev, hdev->rss_size_max);
}

static int hclgevf_init_vlan_config(struct hclgevf_dev *hdev)
{
	/* other vlan config(like, VLAN TX/RX offload) would also be added
	 * here later
	 */
	return hclgevf_set_vlan_filter(&hdev->nic, htons(ETH_P_8021Q), 0,
				       false);
}

static int hclgevf_ae_start(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	int i, queue_id;

	for (i = 0; i < handle->kinfo.num_tqps; i++) {
		/* ring enable */
		queue_id = hclgevf_get_queue_id(handle->kinfo.tqp[i]);
		if (queue_id < 0) {
			dev_warn(&hdev->pdev->dev,
				 "Get invalid queue id, ignore it\n");
			continue;
		}

		hclgevf_tqp_enable(hdev, queue_id, 0, true);
	}

	/* reset tqp stats */
	hclgevf_reset_tqp_stats(handle);

	hclgevf_request_link_info(hdev);

	clear_bit(HCLGEVF_STATE_DOWN, &hdev->state);
	mod_timer(&hdev->service_timer, jiffies + HZ);

	return 0;
}

static void hclgevf_ae_stop(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	int i, queue_id;

	for (i = 0; i < hdev->num_tqps; i++) {
		/* Ring disable */
		queue_id = hclgevf_get_queue_id(handle->kinfo.tqp[i]);
		if (queue_id < 0) {
			dev_warn(&hdev->pdev->dev,
				 "Get invalid queue id, ignore it\n");
			continue;
		}

		hclgevf_tqp_enable(hdev, queue_id, 0, false);
	}

	/* reset tqp stats */
	hclgevf_reset_tqp_stats(handle);
	del_timer_sync(&hdev->service_timer);
	cancel_work_sync(&hdev->service_task);
	hclgevf_update_link_status(hdev, 0);
}

static void hclgevf_state_init(struct hclgevf_dev *hdev)
{
	/* if this is on going reset then skip this initialization */
	if (hclgevf_dev_ongoing_reset(hdev))
		return;

	/* setup tasks for the MBX */
	INIT_WORK(&hdev->mbx_service_task, hclgevf_mailbox_service_task);
	clear_bit(HCLGEVF_STATE_MBX_SERVICE_SCHED, &hdev->state);
	clear_bit(HCLGEVF_STATE_MBX_HANDLING, &hdev->state);

	/* setup tasks for service timer */
	timer_setup(&hdev->service_timer, hclgevf_service_timer, 0);

	INIT_WORK(&hdev->service_task, hclgevf_service_task);
	clear_bit(HCLGEVF_STATE_SERVICE_SCHED, &hdev->state);

	INIT_WORK(&hdev->rst_service_task, hclgevf_reset_service_task);

	mutex_init(&hdev->mbx_resp.mbx_mutex);

	/* bring the device down */
	set_bit(HCLGEVF_STATE_DOWN, &hdev->state);
}

static void hclgevf_state_uninit(struct hclgevf_dev *hdev)
{
	set_bit(HCLGEVF_STATE_DOWN, &hdev->state);

	if (hdev->service_timer.function)
		del_timer_sync(&hdev->service_timer);
	if (hdev->service_task.func)
		cancel_work_sync(&hdev->service_task);
	if (hdev->mbx_service_task.func)
		cancel_work_sync(&hdev->mbx_service_task);
	if (hdev->rst_service_task.func)
		cancel_work_sync(&hdev->rst_service_task);

	mutex_destroy(&hdev->mbx_resp.mbx_mutex);
}

static int hclgevf_init_msi(struct hclgevf_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	int vectors;
	int i;

	/* if this is on going reset then skip this initialization */
	if (hclgevf_dev_ongoing_reset(hdev))
		return 0;

	hdev->num_msi = HCLGEVF_MAX_VF_VECTOR_NUM;

	vectors = pci_alloc_irq_vectors(pdev, 1, hdev->num_msi,
					PCI_IRQ_MSI | PCI_IRQ_MSIX);
	if (vectors < 0) {
		dev_err(&pdev->dev,
			"failed(%d) to allocate MSI/MSI-X vectors\n",
			vectors);
		return vectors;
	}
	if (vectors < hdev->num_msi)
		dev_warn(&hdev->pdev->dev,
			 "requested %d MSI/MSI-X, but allocated %d MSI/MSI-X\n",
			 hdev->num_msi, vectors);

	hdev->num_msi = vectors;
	hdev->num_msi_left = vectors;
	hdev->base_msi_vector = pdev->irq;

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
		pci_free_irq_vectors(pdev);
		return -ENOMEM;
	}

	return 0;
}

static void hclgevf_uninit_msi(struct hclgevf_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;

	pci_free_irq_vectors(pdev);
}

static int hclgevf_misc_irq_init(struct hclgevf_dev *hdev)
{
	int ret = 0;

	/* if this is on going reset then skip this initialization */
	if (hclgevf_dev_ongoing_reset(hdev))
		return 0;

	hclgevf_get_misc_vector(hdev);

	ret = request_irq(hdev->misc_vector.vector_irq, hclgevf_misc_irq_handle,
			  0, "hclgevf_cmd", hdev);
	if (ret) {
		dev_err(&hdev->pdev->dev, "VF failed to request misc irq(%d)\n",
			hdev->misc_vector.vector_irq);
		return ret;
	}

	/* enable misc. vector(vector 0) */
	hclgevf_enable_vector(&hdev->misc_vector, true);

	return ret;
}

static void hclgevf_misc_irq_uninit(struct hclgevf_dev *hdev)
{
	/* disable misc vector(vector 0) */
	hclgevf_enable_vector(&hdev->misc_vector, false);
	free_irq(hdev->misc_vector.vector_irq, hdev);
	hclgevf_free_vector(hdev, 0);
}

static int hclgevf_init_instance(struct hclgevf_dev *hdev,
				 struct hnae3_client *client)
{
	int ret;

	switch (client->type) {
	case HNAE3_CLIENT_KNIC:
		hdev->nic_client = client;
		hdev->nic.client = client;

		ret = client->ops->init_instance(&hdev->nic);
		if (ret)
			return ret;

		if (hdev->roce_client && hnae3_dev_roce_supported(hdev)) {
			struct hnae3_client *rc = hdev->roce_client;

			ret = hclgevf_init_roce_base_info(hdev);
			if (ret)
				return ret;
			ret = rc->ops->init_instance(&hdev->roce);
			if (ret)
				return ret;
		}
		break;
	case HNAE3_CLIENT_UNIC:
		hdev->nic_client = client;
		hdev->nic.client = client;

		ret = client->ops->init_instance(&hdev->nic);
		if (ret)
			return ret;
		break;
	case HNAE3_CLIENT_ROCE:
		hdev->roce_client = client;
		hdev->roce.client = client;

		if (hdev->roce_client && hnae3_dev_roce_supported(hdev)) {
			ret = hclgevf_init_roce_base_info(hdev);
			if (ret)
				return ret;

			ret = client->ops->init_instance(&hdev->roce);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static void hclgevf_uninit_instance(struct hclgevf_dev *hdev,
				    struct hnae3_client *client)
{
	/* un-init roce, if it exists */
	if (hdev->roce_client)
		hdev->roce_client->ops->uninit_instance(&hdev->roce, 0);

	/* un-init nic/unic, if this was not called by roce client */
	if ((client->ops->uninit_instance) &&
	    (client->type != HNAE3_CLIENT_ROCE))
		client->ops->uninit_instance(&hdev->nic, 0);
}

static int hclgevf_register_client(struct hnae3_client *client,
				   struct hnae3_ae_dev *ae_dev)
{
	struct hclgevf_dev *hdev = ae_dev->priv;

	return hclgevf_init_instance(hdev, client);
}

static void hclgevf_unregister_client(struct hnae3_client *client,
				      struct hnae3_ae_dev *ae_dev)
{
	struct hclgevf_dev *hdev = ae_dev->priv;

	hclgevf_uninit_instance(hdev, client);
}

static int hclgevf_pci_init(struct hclgevf_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	struct hclgevf_hw *hw;
	int ret;

	/* check if we need to skip initialization of pci. This will happen if
	 * device is undergoing VF reset. Otherwise, we would need to
	 * re-initialize pci interface again i.e. when device is not going
	 * through *any* reset or actually undergoing full reset.
	 */
	if (hclgevf_dev_ongoing_reset(hdev))
		return 0;

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable PCI device\n");
		goto err_no_drvdata;
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
	hw->hdev = hdev;
	hw->io_base = pci_iomap(pdev, 2, 0);
	if (!hw->io_base) {
		dev_err(&pdev->dev, "can't map configuration register space\n");
		ret = -ENOMEM;
		goto err_clr_master;
	}

	return 0;

err_clr_master:
	pci_clear_master(pdev);
	pci_release_regions(pdev);
err_disable_device:
	pci_disable_device(pdev);
err_no_drvdata:
	pci_set_drvdata(pdev, NULL);
	return ret;
}

static void hclgevf_pci_uninit(struct hclgevf_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;

	pci_iounmap(pdev, hdev->hw.io_base);
	pci_clear_master(pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static int hclgevf_init_hdev(struct hclgevf_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	int ret;

	/* check if device is on-going full reset(i.e. pcie as well) */
	if (hclgevf_dev_ongoing_full_reset(hdev)) {
		dev_warn(&pdev->dev, "device is going full reset\n");
		hclgevf_uninit_hdev(hdev);
	}

	ret = hclgevf_pci_init(hdev);
	if (ret) {
		dev_err(&pdev->dev, "PCI initialization failed\n");
		return ret;
	}

	ret = hclgevf_init_msi(hdev);
	if (ret) {
		dev_err(&pdev->dev, "failed(%d) to init MSI/MSI-X\n", ret);
		goto err_irq_init;
	}

	hclgevf_state_init(hdev);

	ret = hclgevf_misc_irq_init(hdev);
	if (ret) {
		dev_err(&pdev->dev, "failed(%d) to init Misc IRQ(vector0)\n",
			ret);
		goto err_misc_irq_init;
	}

	ret = hclgevf_cmd_init(hdev);
	if (ret)
		goto err_cmd_init;

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
	if (ret) {
		dev_err(&pdev->dev, "failed(%d) to set handle info\n", ret);
		goto err_config;
	}

	/* Initialize VF's MTA */
	hdev->accept_mta_mc = true;
	ret = hclgevf_cfg_func_mta_filter(&hdev->nic, hdev->accept_mta_mc);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed(%d) to set mta filter mode\n", ret);
		goto err_config;
	}

	/* Initialize RSS for this VF */
	ret = hclgevf_rss_init_hw(hdev);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed(%d) to initialize RSS\n", ret);
		goto err_config;
	}

	ret = hclgevf_init_vlan_config(hdev);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed(%d) to initialize VLAN config\n", ret);
		goto err_config;
	}

	pr_info("finished initializing %s driver\n", HCLGEVF_DRIVER_NAME);

	return 0;

err_config:
	hclgevf_cmd_uninit(hdev);
err_cmd_init:
	hclgevf_misc_irq_uninit(hdev);
err_misc_irq_init:
	hclgevf_state_uninit(hdev);
	hclgevf_uninit_msi(hdev);
err_irq_init:
	hclgevf_pci_uninit(hdev);
	return ret;
}

static void hclgevf_uninit_hdev(struct hclgevf_dev *hdev)
{
	hclgevf_cmd_uninit(hdev);
	hclgevf_misc_irq_uninit(hdev);
	hclgevf_state_uninit(hdev);
	hclgevf_uninit_msi(hdev);
	hclgevf_pci_uninit(hdev);
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
	if (ret)
		dev_err(&pdev->dev, "hclge device initialization failed\n");

	return ret;
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

	return min_t(u32, hdev->rss_size_max * kinfo->num_tc, hdev->num_tqps);
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
	ch->combined_count = hdev->num_tqps;
}

static void hclgevf_get_tqps_and_rss_info(struct hnae3_handle *handle,
					  u16 *free_tqps, u16 *max_rss_size)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	*free_tqps = 0;
	*max_rss_size = hdev->rss_size_max;
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

static const struct hnae3_ae_ops hclgevf_ops = {
	.init_ae_dev = hclgevf_init_ae_dev,
	.uninit_ae_dev = hclgevf_uninit_ae_dev,
	.init_client_instance = hclgevf_register_client,
	.uninit_client_instance = hclgevf_unregister_client,
	.start = hclgevf_ae_start,
	.stop = hclgevf_ae_stop,
	.map_ring_to_vector = hclgevf_map_ring_to_vector,
	.unmap_ring_from_vector = hclgevf_unmap_ring_from_vector,
	.get_vector = hclgevf_get_vector,
	.put_vector = hclgevf_put_vector,
	.reset_queue = hclgevf_reset_tqp,
	.set_promisc_mode = hclgevf_set_promisc_mode,
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
	.get_rss_key_size = hclgevf_get_rss_key_size,
	.get_rss_indir_size = hclgevf_get_rss_indir_size,
	.get_rss = hclgevf_get_rss,
	.set_rss = hclgevf_set_rss,
	.get_tc_size = hclgevf_get_tc_size,
	.get_fw_version = hclgevf_get_fw_version,
	.set_vlan_filter = hclgevf_set_vlan_filter,
	.reset_event = hclgevf_reset_event,
	.get_channels = hclgevf_get_channels,
	.get_tqps_and_rss_info = hclgevf_get_tqps_and_rss_info,
	.get_status = hclgevf_get_status,
	.get_ksettings_an_result = hclgevf_get_ksettings_an_result,
};

static struct hnae3_ae_algo ae_algovf = {
	.ops = &hclgevf_ops,
	.name = HCLGEVF_NAME,
	.pdev_id_table = ae_algovf_pci_tbl,
};

static int hclgevf_init(void)
{
	pr_info("%s is initializing\n", HCLGEVF_NAME);

	return hnae3_register_ae_algo(&ae_algovf);
}

static void hclgevf_exit(void)
{
	hnae3_unregister_ae_algo(&ae_algovf);
}
module_init(hclgevf_init);
module_exit(hclgevf_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huawei Tech. Co., Ltd.");
MODULE_DESCRIPTION("HCLGEVF Driver");
MODULE_VERSION(HCLGEVF_MOD_VERSION);
