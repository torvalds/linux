/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021, Intel Corporation. */

#ifndef _IAVF_ADV_RSS_H_
#define _IAVF_ADV_RSS_H_

struct iavf_adapter;

/* State of advanced RSS configuration */
enum iavf_adv_rss_state_t {
	IAVF_ADV_RSS_ADD_REQUEST,	/* User requests to add RSS */
	IAVF_ADV_RSS_ADD_PENDING,	/* RSS pending add by the PF */
	IAVF_ADV_RSS_DEL_REQUEST,	/* Driver requests to delete RSS */
	IAVF_ADV_RSS_DEL_PENDING,	/* RSS pending delete by the PF */
	IAVF_ADV_RSS_ACTIVE,		/* RSS configuration is active */
};

enum iavf_adv_rss_flow_seg_hdr {
	IAVF_ADV_RSS_FLOW_SEG_HDR_NONE	= 0x00000000,
	IAVF_ADV_RSS_FLOW_SEG_HDR_IPV4	= 0x00000001,
	IAVF_ADV_RSS_FLOW_SEG_HDR_IPV6	= 0x00000002,
	IAVF_ADV_RSS_FLOW_SEG_HDR_TCP	= 0x00000004,
	IAVF_ADV_RSS_FLOW_SEG_HDR_UDP	= 0x00000008,
	IAVF_ADV_RSS_FLOW_SEG_HDR_SCTP	= 0x00000010,
};

#define IAVF_ADV_RSS_FLOW_SEG_HDR_L3		\
	(IAVF_ADV_RSS_FLOW_SEG_HDR_IPV4	|	\
	 IAVF_ADV_RSS_FLOW_SEG_HDR_IPV6)

#define IAVF_ADV_RSS_FLOW_SEG_HDR_L4		\
	(IAVF_ADV_RSS_FLOW_SEG_HDR_TCP |	\
	 IAVF_ADV_RSS_FLOW_SEG_HDR_UDP |	\
	 IAVF_ADV_RSS_FLOW_SEG_HDR_SCTP)

enum iavf_adv_rss_flow_field {
	/* L3 */
	IAVF_ADV_RSS_FLOW_FIELD_IDX_IPV4_SA,
	IAVF_ADV_RSS_FLOW_FIELD_IDX_IPV4_DA,
	IAVF_ADV_RSS_FLOW_FIELD_IDX_IPV6_SA,
	IAVF_ADV_RSS_FLOW_FIELD_IDX_IPV6_DA,
	/* L4 */
	IAVF_ADV_RSS_FLOW_FIELD_IDX_TCP_SRC_PORT,
	IAVF_ADV_RSS_FLOW_FIELD_IDX_TCP_DST_PORT,
	IAVF_ADV_RSS_FLOW_FIELD_IDX_UDP_SRC_PORT,
	IAVF_ADV_RSS_FLOW_FIELD_IDX_UDP_DST_PORT,
	IAVF_ADV_RSS_FLOW_FIELD_IDX_SCTP_SRC_PORT,
	IAVF_ADV_RSS_FLOW_FIELD_IDX_SCTP_DST_PORT,

	/* The total number of enums must not exceed 64 */
	IAVF_ADV_RSS_FLOW_FIELD_IDX_MAX
};

#define IAVF_ADV_RSS_HASH_INVALID	0
#define IAVF_ADV_RSS_HASH_FLD_IPV4_SA	\
	BIT_ULL(IAVF_ADV_RSS_FLOW_FIELD_IDX_IPV4_SA)
#define IAVF_ADV_RSS_HASH_FLD_IPV6_SA	\
	BIT_ULL(IAVF_ADV_RSS_FLOW_FIELD_IDX_IPV6_SA)
#define IAVF_ADV_RSS_HASH_FLD_IPV4_DA	\
	BIT_ULL(IAVF_ADV_RSS_FLOW_FIELD_IDX_IPV4_DA)
#define IAVF_ADV_RSS_HASH_FLD_IPV6_DA	\
	BIT_ULL(IAVF_ADV_RSS_FLOW_FIELD_IDX_IPV6_DA)
#define IAVF_ADV_RSS_HASH_FLD_TCP_SRC_PORT	\
	BIT_ULL(IAVF_ADV_RSS_FLOW_FIELD_IDX_TCP_SRC_PORT)
#define IAVF_ADV_RSS_HASH_FLD_TCP_DST_PORT	\
	BIT_ULL(IAVF_ADV_RSS_FLOW_FIELD_IDX_TCP_DST_PORT)
#define IAVF_ADV_RSS_HASH_FLD_UDP_SRC_PORT	\
	BIT_ULL(IAVF_ADV_RSS_FLOW_FIELD_IDX_UDP_SRC_PORT)
#define IAVF_ADV_RSS_HASH_FLD_UDP_DST_PORT	\
	BIT_ULL(IAVF_ADV_RSS_FLOW_FIELD_IDX_UDP_DST_PORT)
#define IAVF_ADV_RSS_HASH_FLD_SCTP_SRC_PORT	\
	BIT_ULL(IAVF_ADV_RSS_FLOW_FIELD_IDX_SCTP_SRC_PORT)
#define IAVF_ADV_RSS_HASH_FLD_SCTP_DST_PORT	\
	BIT_ULL(IAVF_ADV_RSS_FLOW_FIELD_IDX_SCTP_DST_PORT)

/* bookkeeping of advanced RSS configuration */
struct iavf_adv_rss {
	enum iavf_adv_rss_state_t state;
	struct list_head list;

	u32 packet_hdrs;
	u64 hash_flds;

	struct virtchnl_rss_cfg cfg_msg;
};

int
iavf_fill_adv_rss_cfg_msg(struct virtchnl_rss_cfg *rss_cfg,
			  u32 packet_hdrs, u64 hash_flds);
struct iavf_adv_rss *
iavf_find_adv_rss_cfg_by_hdrs(struct iavf_adapter *adapter, u32 packet_hdrs);
void
iavf_print_adv_rss_cfg(struct iavf_adapter *adapter, struct iavf_adv_rss *rss,
		       const char *action, const char *result);
#endif /* _IAVF_ADV_RSS_H_ */
