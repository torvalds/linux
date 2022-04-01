// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2021-2021 Hisilicon Limited.

#include <linux/err.h>

#include "hnae3.h"
#include "hclge_comm_cmd.h"
#include "hclge_comm_tqp_stats.h"

u64 *hclge_comm_tqps_get_stats(struct hnae3_handle *handle, u64 *data)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct hclge_comm_tqp *tqp;
	u64 *buff = data;
	u16 i;

	for (i = 0; i < kinfo->num_tqps; i++) {
		tqp = container_of(kinfo->tqp[i], struct hclge_comm_tqp, q);
		*buff++ = tqp->tqp_stats.rcb_tx_ring_pktnum_rcd;
	}

	for (i = 0; i < kinfo->num_tqps; i++) {
		tqp = container_of(kinfo->tqp[i], struct hclge_comm_tqp, q);
		*buff++ = tqp->tqp_stats.rcb_rx_ring_pktnum_rcd;
	}

	return buff;
}

int hclge_comm_tqps_get_sset_count(struct hnae3_handle *handle)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;

	return kinfo->num_tqps * HCLGE_COMM_QUEUE_PAIR_SIZE;
}

u8 *hclge_comm_tqps_get_strings(struct hnae3_handle *handle, u8 *data)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	u8 *buff = data;
	u16 i;

	for (i = 0; i < kinfo->num_tqps; i++) {
		struct hclge_comm_tqp *tqp =
			container_of(kinfo->tqp[i], struct hclge_comm_tqp, q);
		snprintf(buff, ETH_GSTRING_LEN, "txq%u_pktnum_rcd", tqp->index);
		buff += ETH_GSTRING_LEN;
	}

	for (i = 0; i < kinfo->num_tqps; i++) {
		struct hclge_comm_tqp *tqp =
			container_of(kinfo->tqp[i], struct hclge_comm_tqp, q);
		snprintf(buff, ETH_GSTRING_LEN, "rxq%u_pktnum_rcd", tqp->index);
		buff += ETH_GSTRING_LEN;
	}

	return buff;
}

int hclge_comm_tqps_update_stats(struct hnae3_handle *handle,
				 struct hclge_comm_hw *hw)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct hclge_comm_tqp *tqp;
	struct hclge_desc desc;
	int ret;
	u16 i;

	for (i = 0; i < kinfo->num_tqps; i++) {
		tqp = container_of(kinfo->tqp[i], struct hclge_comm_tqp, q);
		hclge_comm_cmd_setup_basic_desc(&desc, HCLGE_OPC_QUERY_RX_STATS,
						true);

		desc.data[0] = cpu_to_le32(tqp->index);
		ret = hclge_comm_cmd_send(hw, &desc, 1);
		if (ret) {
			dev_err(&hw->cmq.csq.pdev->dev,
				"failed to get tqp stat, ret = %d, tx = %u.\n",
				ret, i);
			return ret;
		}
		tqp->tqp_stats.rcb_rx_ring_pktnum_rcd +=
			le32_to_cpu(desc.data[1]);

		hclge_comm_cmd_setup_basic_desc(&desc, HCLGE_OPC_QUERY_TX_STATS,
						true);

		desc.data[0] = cpu_to_le32(tqp->index & 0x1ff);
		ret = hclge_comm_cmd_send(hw, &desc, 1);
		if (ret) {
			dev_err(&hw->cmq.csq.pdev->dev,
				"failed to get tqp stat, ret = %d, rx = %u.\n",
				ret, i);
			return ret;
		}
		tqp->tqp_stats.rcb_tx_ring_pktnum_rcd +=
			le32_to_cpu(desc.data[1]);
	}

	return 0;
}

void hclge_comm_reset_tqp_stats(struct hnae3_handle *handle)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct hclge_comm_tqp *tqp;
	struct hnae3_queue *queue;
	u16 i;

	for (i = 0; i < kinfo->num_tqps; i++) {
		queue = kinfo->tqp[i];
		tqp = container_of(queue, struct hclge_comm_tqp, q);
		memset(&tqp->tqp_stats, 0, sizeof(tqp->tqp_stats));
	}
}
