// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2021-2021 Hisilicon Limited.
#include <linux/skbuff.h>

#include "hnae3.h"
#include "hclge_comm_rss.h"

u32 hclge_comm_get_rss_key_size(struct hnae3_handle *handle)
{
	return HCLGE_COMM_RSS_KEY_SIZE;
}

void hclge_comm_get_rss_type(struct hnae3_handle *nic,
			     struct hclge_comm_rss_tuple_cfg *rss_tuple_sets)
{
	if (rss_tuple_sets->ipv4_tcp_en ||
	    rss_tuple_sets->ipv4_udp_en ||
	    rss_tuple_sets->ipv4_sctp_en ||
	    rss_tuple_sets->ipv6_tcp_en ||
	    rss_tuple_sets->ipv6_udp_en ||
	    rss_tuple_sets->ipv6_sctp_en)
		nic->kinfo.rss_type = PKT_HASH_TYPE_L4;
	else if (rss_tuple_sets->ipv4_fragment_en ||
		 rss_tuple_sets->ipv6_fragment_en)
		nic->kinfo.rss_type = PKT_HASH_TYPE_L3;
	else
		nic->kinfo.rss_type = PKT_HASH_TYPE_NONE;
}

int hclge_comm_parse_rss_hfunc(struct hclge_comm_rss_cfg *rss_cfg,
			       const u8 hfunc, u8 *hash_algo)
{
	switch (hfunc) {
	case ETH_RSS_HASH_TOP:
		*hash_algo = HCLGE_COMM_RSS_HASH_ALGO_TOEPLITZ;
		return 0;
	case ETH_RSS_HASH_XOR:
		*hash_algo = HCLGE_COMM_RSS_HASH_ALGO_SIMPLE;
		return 0;
	case ETH_RSS_HASH_NO_CHANGE:
		*hash_algo = rss_cfg->rss_algo;
		return 0;
	default:
		return -EINVAL;
	}
}

void hclge_comm_rss_indir_init_cfg(struct hnae3_ae_dev *ae_dev,
				   struct hclge_comm_rss_cfg *rss_cfg)
{
	u16 i;
	/* Initialize RSS indirect table */
	for (i = 0; i < ae_dev->dev_specs.rss_ind_tbl_size; i++)
		rss_cfg->rss_indirection_tbl[i] = i % rss_cfg->rss_size;
}

int hclge_comm_get_rss_tuple(struct hclge_comm_rss_cfg *rss_cfg, int flow_type,
			     u8 *tuple_sets)
{
	switch (flow_type) {
	case TCP_V4_FLOW:
		*tuple_sets = rss_cfg->rss_tuple_sets.ipv4_tcp_en;
		break;
	case UDP_V4_FLOW:
		*tuple_sets = rss_cfg->rss_tuple_sets.ipv4_udp_en;
		break;
	case TCP_V6_FLOW:
		*tuple_sets = rss_cfg->rss_tuple_sets.ipv6_tcp_en;
		break;
	case UDP_V6_FLOW:
		*tuple_sets = rss_cfg->rss_tuple_sets.ipv6_udp_en;
		break;
	case SCTP_V4_FLOW:
		*tuple_sets = rss_cfg->rss_tuple_sets.ipv4_sctp_en;
		break;
	case SCTP_V6_FLOW:
		*tuple_sets = rss_cfg->rss_tuple_sets.ipv6_sctp_en;
		break;
	case IPV4_FLOW:
	case IPV6_FLOW:
		*tuple_sets = HCLGE_COMM_S_IP_BIT | HCLGE_COMM_D_IP_BIT;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void
hclge_comm_append_rss_msb_info(struct hclge_comm_rss_ind_tbl_cmd *req,
			       u16 qid, u32 j)
{
	u8 rss_msb_oft;
	u8 rss_msb_val;

	rss_msb_oft =
		j * HCLGE_COMM_RSS_CFG_TBL_BW_H / BITS_PER_BYTE;
	rss_msb_val = (qid >> HCLGE_COMM_RSS_CFG_TBL_BW_L & 0x1) <<
		(j * HCLGE_COMM_RSS_CFG_TBL_BW_H % BITS_PER_BYTE);
	req->rss_qid_h[rss_msb_oft] |= rss_msb_val;
}

int hclge_comm_set_rss_indir_table(struct hnae3_ae_dev *ae_dev,
				   struct hclge_comm_hw *hw, const u16 *indir)
{
	struct hclge_comm_rss_ind_tbl_cmd *req;
	struct hclge_desc desc;
	u16 rss_cfg_tbl_num;
	int ret;
	u16 qid;
	u16 i;
	u32 j;

	req = (struct hclge_comm_rss_ind_tbl_cmd *)desc.data;
	rss_cfg_tbl_num = ae_dev->dev_specs.rss_ind_tbl_size /
			  HCLGE_COMM_RSS_CFG_TBL_SIZE;

	for (i = 0; i < rss_cfg_tbl_num; i++) {
		hclge_comm_cmd_setup_basic_desc(&desc,
						HCLGE_COMM_OPC_RSS_INDIR_TABLE,
						false);

		req->start_table_index =
			cpu_to_le16(i * HCLGE_COMM_RSS_CFG_TBL_SIZE);
		req->rss_set_bitmap =
			cpu_to_le16(HCLGE_COMM_RSS_SET_BITMAP_MSK);
		for (j = 0; j < HCLGE_COMM_RSS_CFG_TBL_SIZE; j++) {
			qid = indir[i * HCLGE_COMM_RSS_CFG_TBL_SIZE + j];
			req->rss_qid_l[j] = qid & 0xff;
			hclge_comm_append_rss_msb_info(req, qid, j);
		}
		ret = hclge_comm_cmd_send(hw, &desc, 1);
		if (ret) {
			dev_err(&hw->cmq.csq.pdev->dev,
				"failed to configure rss table, ret = %d.\n",
				ret);
			return ret;
		}
	}
	return 0;
}

int hclge_comm_set_rss_input_tuple(struct hnae3_handle *nic,
				   struct hclge_comm_hw *hw, bool is_pf,
				   struct hclge_comm_rss_cfg *rss_cfg)
{
	struct hclge_comm_rss_input_tuple_cmd *req;
	struct hclge_desc desc;
	int ret;

	hclge_comm_cmd_setup_basic_desc(&desc, HCLGE_COMM_OPC_RSS_INPUT_TUPLE,
					false);

	req = (struct hclge_comm_rss_input_tuple_cmd *)desc.data;

	req->ipv4_tcp_en = rss_cfg->rss_tuple_sets.ipv4_tcp_en;
	req->ipv4_udp_en = rss_cfg->rss_tuple_sets.ipv4_udp_en;
	req->ipv4_sctp_en = rss_cfg->rss_tuple_sets.ipv4_sctp_en;
	req->ipv4_fragment_en = rss_cfg->rss_tuple_sets.ipv4_fragment_en;
	req->ipv6_tcp_en = rss_cfg->rss_tuple_sets.ipv6_tcp_en;
	req->ipv6_udp_en = rss_cfg->rss_tuple_sets.ipv6_udp_en;
	req->ipv6_sctp_en = rss_cfg->rss_tuple_sets.ipv6_sctp_en;
	req->ipv6_fragment_en = rss_cfg->rss_tuple_sets.ipv6_fragment_en;

	if (is_pf)
		hclge_comm_get_rss_type(nic, &rss_cfg->rss_tuple_sets);

	ret = hclge_comm_cmd_send(hw, &desc, 1);
	if (ret)
		dev_err(&hw->cmq.csq.pdev->dev,
			"failed to configure rss input, ret = %d.\n", ret);
	return ret;
}

void hclge_comm_get_rss_hash_info(struct hclge_comm_rss_cfg *rss_cfg, u8 *key,
				  u8 *hfunc)
{
	/* Get hash algorithm */
	if (hfunc) {
		switch (rss_cfg->rss_algo) {
		case HCLGE_COMM_RSS_HASH_ALGO_TOEPLITZ:
			*hfunc = ETH_RSS_HASH_TOP;
			break;
		case HCLGE_COMM_RSS_HASH_ALGO_SIMPLE:
			*hfunc = ETH_RSS_HASH_XOR;
			break;
		default:
			*hfunc = ETH_RSS_HASH_UNKNOWN;
			break;
		}
	}

	/* Get the RSS Key required by the user */
	if (key)
		memcpy(key, rss_cfg->rss_hash_key, HCLGE_COMM_RSS_KEY_SIZE);
}

void hclge_comm_get_rss_indir_tbl(struct hclge_comm_rss_cfg *rss_cfg,
				  u32 *indir, u16 rss_ind_tbl_size)
{
	u16 i;

	if (!indir)
		return;

	for (i = 0; i < rss_ind_tbl_size; i++)
		indir[i] = rss_cfg->rss_indirection_tbl[i];
}

int hclge_comm_set_rss_algo_key(struct hclge_comm_hw *hw, const u8 hfunc,
				const u8 *key)
{
	struct hclge_comm_rss_config_cmd *req;
	unsigned int key_offset = 0;
	struct hclge_desc desc;
	int key_counts;
	int key_size;
	int ret;

	key_counts = HCLGE_COMM_RSS_KEY_SIZE;
	req = (struct hclge_comm_rss_config_cmd *)desc.data;

	while (key_counts) {
		hclge_comm_cmd_setup_basic_desc(&desc,
						HCLGE_COMM_OPC_RSS_GENERIC_CFG,
						false);

		req->hash_config |= (hfunc & HCLGE_COMM_RSS_HASH_ALGO_MASK);
		req->hash_config |=
			(key_offset << HCLGE_COMM_RSS_HASH_KEY_OFFSET_B);

		key_size = min(HCLGE_COMM_RSS_HASH_KEY_NUM, key_counts);
		memcpy(req->hash_key,
		       key + key_offset * HCLGE_COMM_RSS_HASH_KEY_NUM,
		       key_size);

		key_counts -= key_size;
		key_offset++;
		ret = hclge_comm_cmd_send(hw, &desc, 1);
		if (ret) {
			dev_err(&hw->cmq.csq.pdev->dev,
				"failed to configure RSS key, ret = %d.\n",
				ret);
			return ret;
		}
	}

	return 0;
}

u8 hclge_comm_get_rss_hash_bits(struct ethtool_rxnfc *nfc)
{
	u8 hash_sets = nfc->data & RXH_L4_B_0_1 ? HCLGE_COMM_S_PORT_BIT : 0;

	if (nfc->data & RXH_L4_B_2_3)
		hash_sets |= HCLGE_COMM_D_PORT_BIT;
	else
		hash_sets &= ~HCLGE_COMM_D_PORT_BIT;

	if (nfc->data & RXH_IP_SRC)
		hash_sets |= HCLGE_COMM_S_IP_BIT;
	else
		hash_sets &= ~HCLGE_COMM_S_IP_BIT;

	if (nfc->data & RXH_IP_DST)
		hash_sets |= HCLGE_COMM_D_IP_BIT;
	else
		hash_sets &= ~HCLGE_COMM_D_IP_BIT;

	if (nfc->flow_type == SCTP_V4_FLOW || nfc->flow_type == SCTP_V6_FLOW)
		hash_sets |= HCLGE_COMM_V_TAG_BIT;

	return hash_sets;
}

int hclge_comm_init_rss_tuple_cmd(struct hclge_comm_rss_cfg *rss_cfg,
				  struct ethtool_rxnfc *nfc,
				  struct hnae3_ae_dev *ae_dev,
				  struct hclge_comm_rss_input_tuple_cmd *req)
{
	u8 tuple_sets;

	req->ipv4_tcp_en = rss_cfg->rss_tuple_sets.ipv4_tcp_en;
	req->ipv4_udp_en = rss_cfg->rss_tuple_sets.ipv4_udp_en;
	req->ipv4_sctp_en = rss_cfg->rss_tuple_sets.ipv4_sctp_en;
	req->ipv4_fragment_en = rss_cfg->rss_tuple_sets.ipv4_fragment_en;
	req->ipv6_tcp_en = rss_cfg->rss_tuple_sets.ipv6_tcp_en;
	req->ipv6_udp_en = rss_cfg->rss_tuple_sets.ipv6_udp_en;
	req->ipv6_sctp_en = rss_cfg->rss_tuple_sets.ipv6_sctp_en;
	req->ipv6_fragment_en = rss_cfg->rss_tuple_sets.ipv6_fragment_en;

	tuple_sets = hclge_comm_get_rss_hash_bits(nfc);
	switch (nfc->flow_type) {
	case TCP_V4_FLOW:
		req->ipv4_tcp_en = tuple_sets;
		break;
	case TCP_V6_FLOW:
		req->ipv6_tcp_en = tuple_sets;
		break;
	case UDP_V4_FLOW:
		req->ipv4_udp_en = tuple_sets;
		break;
	case UDP_V6_FLOW:
		req->ipv6_udp_en = tuple_sets;
		break;
	case SCTP_V4_FLOW:
		req->ipv4_sctp_en = tuple_sets;
		break;
	case SCTP_V6_FLOW:
		if (ae_dev->dev_version <= HNAE3_DEVICE_VERSION_V2 &&
		    (nfc->data & (RXH_L4_B_0_1 | RXH_L4_B_2_3)))
			return -EINVAL;

		req->ipv6_sctp_en = tuple_sets;
		break;
	case IPV4_FLOW:
		req->ipv4_fragment_en = HCLGE_COMM_RSS_INPUT_TUPLE_OTHER;
		break;
	case IPV6_FLOW:
		req->ipv6_fragment_en = HCLGE_COMM_RSS_INPUT_TUPLE_OTHER;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

u64 hclge_comm_convert_rss_tuple(u8 tuple_sets)
{
	u64 tuple_data = 0;

	if (tuple_sets & HCLGE_COMM_D_PORT_BIT)
		tuple_data |= RXH_L4_B_2_3;
	if (tuple_sets & HCLGE_COMM_S_PORT_BIT)
		tuple_data |= RXH_L4_B_0_1;
	if (tuple_sets & HCLGE_COMM_D_IP_BIT)
		tuple_data |= RXH_IP_DST;
	if (tuple_sets & HCLGE_COMM_S_IP_BIT)
		tuple_data |= RXH_IP_SRC;

	return tuple_data;
}
