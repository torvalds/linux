/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021, Intel Corporation. */

#ifndef _IAVF_FDIR_H_
#define _IAVF_FDIR_H_

struct iavf_adapter;

/* State of Flow Director filter
 *
 * *_REQUEST states are used to mark filter to be sent to PF driver to perform
 * an action (either add or delete filter). *_PENDING states are an indication
 * that request was sent to PF and the driver is waiting for response.
 *
 * Both DELETE and DISABLE states are being used to delete a filter in PF.
 * The difference is that after a successful response filter in DEL_PENDING
 * state is being deleted from VF driver as well and filter in DIS_PENDING state
 * is being changed to INACTIVE state.
 */
enum iavf_fdir_fltr_state_t {
	IAVF_FDIR_FLTR_ADD_REQUEST,	/* User requests to add filter */
	IAVF_FDIR_FLTR_ADD_PENDING,	/* Filter pending add by the PF */
	IAVF_FDIR_FLTR_DEL_REQUEST,	/* User requests to delete filter */
	IAVF_FDIR_FLTR_DEL_PENDING,	/* Filter pending delete by the PF */
	IAVF_FDIR_FLTR_DIS_REQUEST,	/* Filter scheduled to be disabled */
	IAVF_FDIR_FLTR_DIS_PENDING,	/* Filter pending disable by the PF */
	IAVF_FDIR_FLTR_INACTIVE,	/* Filter inactive on link down */
	IAVF_FDIR_FLTR_ACTIVE,		/* Filter is active */
};

enum iavf_fdir_flow_type {
	/* NONE - used for undef/error */
	IAVF_FDIR_FLOW_NONE = 0,
	IAVF_FDIR_FLOW_IPV4_TCP,
	IAVF_FDIR_FLOW_IPV4_UDP,
	IAVF_FDIR_FLOW_IPV4_SCTP,
	IAVF_FDIR_FLOW_IPV4_AH,
	IAVF_FDIR_FLOW_IPV4_ESP,
	IAVF_FDIR_FLOW_IPV4_OTHER,
	IAVF_FDIR_FLOW_IPV6_TCP,
	IAVF_FDIR_FLOW_IPV6_UDP,
	IAVF_FDIR_FLOW_IPV6_SCTP,
	IAVF_FDIR_FLOW_IPV6_AH,
	IAVF_FDIR_FLOW_IPV6_ESP,
	IAVF_FDIR_FLOW_IPV6_OTHER,
	IAVF_FDIR_FLOW_NON_IP_L2,
	/* MAX - this must be last and add anything new just above it */
	IAVF_FDIR_FLOW_PTYPE_MAX,
};

/* Must not exceed the array element number of '__be32 data[2]' in the ethtool
 * 'struct ethtool_rx_flow_spec.m_ext.data[2]' to express the flex-byte (word).
 */
#define IAVF_FLEX_WORD_NUM	2

struct iavf_flex_word {
	u16 offset;
	u16 word;
};

struct iavf_ipv4_addrs {
	__be32 src_ip;
	__be32 dst_ip;
};

struct iavf_ipv6_addrs {
	struct in6_addr src_ip;
	struct in6_addr dst_ip;
};

struct iavf_fdir_eth {
	__be16 etype;
};

struct iavf_fdir_ip {
	union {
		struct iavf_ipv4_addrs v4_addrs;
		struct iavf_ipv6_addrs v6_addrs;
	};
	__be16 src_port;
	__be16 dst_port;
	__be32 l4_header;	/* first 4 bytes of the layer 4 header */
	__be32 spi;		/* security parameter index for AH/ESP */
	union {
		u8 tos;
		u8 tclass;
	};
	u8 proto;
};

struct iavf_fdir_extra {
	u32 usr_def[IAVF_FLEX_WORD_NUM];
};

/* bookkeeping of Flow Director filters */
struct iavf_fdir_fltr {
	enum iavf_fdir_fltr_state_t state;
	struct list_head list;

	enum iavf_fdir_flow_type flow_type;

	struct iavf_fdir_eth eth_data;
	struct iavf_fdir_eth eth_mask;

	struct iavf_fdir_ip ip_data;
	struct iavf_fdir_ip ip_mask;

	struct iavf_fdir_extra ext_data;
	struct iavf_fdir_extra ext_mask;

	enum virtchnl_action action;

	/* flex byte filter data */
	u8 ip_ver; /* used to adjust the flex offset, 4 : IPv4, 6 : IPv6 */
	u8 flex_cnt;
	struct iavf_flex_word flex_words[IAVF_FLEX_WORD_NUM];

	u32 flow_id;

	u32 loc;	/* Rule location inside the flow table */
	u32 q_index;

	struct virtchnl_fdir_add vc_add_msg;
};

int iavf_validate_fdir_fltr_masks(struct iavf_adapter *adapter,
				  struct iavf_fdir_fltr *fltr);
int iavf_fill_fdir_add_msg(struct iavf_adapter *adapter, struct iavf_fdir_fltr *fltr);
void iavf_print_fdir_fltr(struct iavf_adapter *adapter, struct iavf_fdir_fltr *fltr);
bool iavf_fdir_is_dup_fltr(struct iavf_adapter *adapter, struct iavf_fdir_fltr *fltr);
void iavf_fdir_list_add_fltr(struct iavf_adapter *adapter, struct iavf_fdir_fltr *fltr);
struct iavf_fdir_fltr *iavf_find_fdir_fltr_by_loc(struct iavf_adapter *adapter, u32 loc);
#endif /* _IAVF_FDIR_H_ */
