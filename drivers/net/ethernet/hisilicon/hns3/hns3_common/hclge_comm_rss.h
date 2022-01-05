/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright (c) 2021-2021 Hisilicon Limited.

#ifndef __HCLGE_COMM_RSS_H
#define __HCLGE_COMM_RSS_H
#include <linux/types.h>

#include "hnae3.h"

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

struct hclge_comm_rss_cfg {
	u8 rss_hash_key[HCLGE_COMM_RSS_KEY_SIZE]; /* user configured hash keys */

	/* shadow table */
	u16 *rss_indirection_tbl;
	u32 rss_algo;

	struct hclge_comm_rss_tuple_cfg rss_tuple_sets;
	u32 rss_size;
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
				  u32 *indir, u16 rss_ind_tbl_size);
u8 hclge_comm_get_rss_hash_bits(struct ethtool_rxnfc *nfc);
u64 hclge_comm_convert_rss_tuple(u8 tuple_sets);

#endif
