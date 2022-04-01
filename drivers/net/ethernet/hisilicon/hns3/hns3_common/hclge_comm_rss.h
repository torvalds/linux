/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright (c) 2021-2021 Hisilicon Limited.

#ifndef __HCLGE_COMM_RSS_H
#define __HCLGE_COMM_RSS_H
#include <linux/types.h>

#include "hnae3.h"
#include "hclge_comm_cmd.h"

#define HCLGE_COMM_RSS_HASH_ALGO_TOEPLITZ	0
#define HCLGE_COMM_RSS_HASH_ALGO_SIMPLE		1
#define HCLGE_COMM_RSS_HASH_ALGO_SYMMETRIC	2

#define HCLGE_COMM_RSS_INPUT_TUPLE_OTHER	GENMASK(3, 0)
#define HCLGE_COMM_RSS_INPUT_TUPLE_SCTP		GENMASK(4, 0)

#define HCLGE_COMM_D_PORT_BIT		BIT(0)
#define HCLGE_COMM_S_PORT_BIT		BIT(1)
#define HCLGE_COMM_D_IP_BIT		BIT(2)
#define HCLGE_COMM_S_IP_BIT		BIT(3)
#define HCLGE_COMM_V_TAG_BIT		BIT(4)
#define HCLGE_COMM_RSS_INPUT_TUPLE_SCTP_NO_PORT	\
	(HCLGE_COMM_D_IP_BIT | HCLGE_COMM_S_IP_BIT | HCLGE_COMM_V_TAG_BIT)
#define HCLGE_COMM_MAX_TC_NUM		8

#define HCLGE_COMM_RSS_TC_OFFSET_S		0
#define HCLGE_COMM_RSS_TC_OFFSET_M		GENMASK(10, 0)
#define HCLGE_COMM_RSS_TC_SIZE_MSB_B	11
#define HCLGE_COMM_RSS_TC_SIZE_S		12
#define HCLGE_COMM_RSS_TC_SIZE_M		GENMASK(14, 12)
#define HCLGE_COMM_RSS_TC_VALID_B		15
#define HCLGE_COMM_RSS_TC_SIZE_MSB_OFFSET	3

struct hclge_comm_rss_tuple_cfg {
	u8 ipv4_tcp_en;
	u8 ipv4_udp_en;
	u8 ipv4_sctp_en;
	u8 ipv4_fragment_en;
	u8 ipv6_tcp_en;
	u8 ipv6_udp_en;
	u8 ipv6_sctp_en;
	u8 ipv6_fragment_en;
};

#define HCLGE_COMM_RSS_KEY_SIZE		40
#define HCLGE_COMM_RSS_CFG_TBL_SIZE	16
#define HCLGE_COMM_RSS_CFG_TBL_BW_H	2U
#define HCLGE_COMM_RSS_CFG_TBL_BW_L	8U
#define HCLGE_COMM_RSS_CFG_TBL_SIZE_H	4
#define HCLGE_COMM_RSS_SET_BITMAP_MSK	GENMASK(15, 0)
#define HCLGE_COMM_RSS_HASH_ALGO_MASK	GENMASK(3, 0)
#define HCLGE_COMM_RSS_HASH_KEY_OFFSET_B	4

#define HCLGE_COMM_RSS_HASH_KEY_NUM	16
struct hclge_comm_rss_config_cmd {
	u8 hash_config;
	u8 rsv[7];
	u8 hash_key[HCLGE_COMM_RSS_HASH_KEY_NUM];
};

struct hclge_comm_rss_cfg {
	u8 rss_hash_key[HCLGE_COMM_RSS_KEY_SIZE]; /* user configured hash keys */

	/* shadow table */
	u16 *rss_indirection_tbl;
	u32 rss_algo;

	struct hclge_comm_rss_tuple_cfg rss_tuple_sets;
	u32 rss_size;
};

struct hclge_comm_rss_input_tuple_cmd {
	u8 ipv4_tcp_en;
	u8 ipv4_udp_en;
	u8 ipv4_sctp_en;
	u8 ipv4_fragment_en;
	u8 ipv6_tcp_en;
	u8 ipv6_udp_en;
	u8 ipv6_sctp_en;
	u8 ipv6_fragment_en;
	u8 rsv[16];
};

struct hclge_comm_rss_ind_tbl_cmd {
	__le16 start_table_index;
	__le16 rss_set_bitmap;
	u8 rss_qid_h[HCLGE_COMM_RSS_CFG_TBL_SIZE_H];
	u8 rss_qid_l[HCLGE_COMM_RSS_CFG_TBL_SIZE];
};

struct hclge_comm_rss_tc_mode_cmd {
	__le16 rss_tc_mode[HCLGE_COMM_MAX_TC_NUM];
	u8 rsv[8];
};

u32 hclge_comm_get_rss_key_size(struct hnae3_handle *handle);
void hclge_comm_get_rss_type(struct hnae3_handle *nic,
			     struct hclge_comm_rss_tuple_cfg *rss_tuple_sets);
void hclge_comm_rss_indir_init_cfg(struct hnae3_ae_dev *ae_dev,
				   struct hclge_comm_rss_cfg *rss_cfg);
int hclge_comm_get_rss_tuple(struct hclge_comm_rss_cfg *rss_cfg, int flow_type,
			     u8 *tuple_sets);
int hclge_comm_parse_rss_hfunc(struct hclge_comm_rss_cfg *rss_cfg,
			       const u8 hfunc, u8 *hash_algo);
void hclge_comm_get_rss_hash_info(struct hclge_comm_rss_cfg *rss_cfg, u8 *key,
				  u8 *hfunc);
void hclge_comm_get_rss_indir_tbl(struct hclge_comm_rss_cfg *rss_cfg,
				  u32 *indir, __le16 rss_ind_tbl_size);
int hclge_comm_set_rss_algo_key(struct hclge_comm_hw *hw, const u8 hfunc,
				const u8 *key);
int hclge_comm_init_rss_tuple_cmd(struct hclge_comm_rss_cfg *rss_cfg,
				  struct ethtool_rxnfc *nfc,
				  struct hnae3_ae_dev *ae_dev,
				  struct hclge_comm_rss_input_tuple_cmd *req);
u64 hclge_comm_convert_rss_tuple(u8 tuple_sets);
int hclge_comm_set_rss_input_tuple(struct hnae3_handle *nic,
				   struct hclge_comm_hw *hw, bool is_pf,
				   struct hclge_comm_rss_cfg *rss_cfg);
int hclge_comm_set_rss_indir_table(struct hnae3_ae_dev *ae_dev,
				   struct hclge_comm_hw *hw, const u16 *indir);
int hclge_comm_rss_init_cfg(struct hnae3_handle *nic,
			    struct hnae3_ae_dev *ae_dev,
			    struct hclge_comm_rss_cfg *rss_cfg);
void hclge_comm_get_rss_tc_info(u16 rss_size, u8 hw_tc_map, u16 *tc_offset,
				u16 *tc_valid, u16 *tc_size);
int hclge_comm_set_rss_tc_mode(struct hclge_comm_hw *hw, u16 *tc_offset,
			       u16 *tc_valid, u16 *tc_size);
int hclge_comm_set_rss_hash_key(struct hclge_comm_rss_cfg *rss_cfg,
				struct hclge_comm_hw *hw, const u8 *key,
				const u8 hfunc);
int hclge_comm_set_rss_tuple(struct hnae3_ae_dev *ae_dev,
			     struct hclge_comm_hw *hw,
			     struct hclge_comm_rss_cfg *rss_cfg,
			     struct ethtool_rxnfc *nfc);
#endif
