/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright (c) 2021-2021 Hisilicon Limited.

#ifndef __HCLGE_COMM_RSS_H
#define __HCLGE_COMM_RSS_H
#include <linux/types.h>

#include "hnae3.h"

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

#endif
