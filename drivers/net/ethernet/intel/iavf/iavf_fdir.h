/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021, Intel Corporation. */

#ifndef _IAVF_FDIR_H_
#define _IAVF_FDIR_H_

struct iavf_adapter;

/* State of Flow Director filter */
enum iavf_fdir_fltr_state_t {
	IAVF_FDIR_FLTR_ADD_REQUEST,	/* User requests to add filter */
	IAVF_FDIR_FLTR_ADD_PENDING,	/* Filter pending add by the PF */
	IAVF_FDIR_FLTR_DEL_REQUEST,	/* User requests to delete filter */
	IAVF_FDIR_FLTR_DEL_PENDING,	/* Filter pending delete by the PF */
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
	/* MAX - this must be last and add anything new just above it */
	IAVF_FDIR_FLOW_PTYPE_MAX,
};

struct iavf_ipv4_addrs {
	__be32 src_ip;
	__be32 dst_ip;
};

struct iavf_fdir_ip {
	union {
		struct iavf_ipv4_addrs v4_addrs;
	};
	__be16 src_port;
	__be16 dst_port;
	__be32 l4_header;	/* first 4 bytes of the layer 4 header */
	__be32 spi;		/* security parameter index for AH/ESP */
	union {
		u8 tos;
	};
	u8 proto;
};
/* bookkeeping of Flow Director filters */
struct iavf_fdir_fltr {
	enum iavf_fdir_fltr_state_t state;
	struct list_head list;

	enum iavf_fdir_flow_type flow_type;

	struct iavf_fdir_ip ip_data;
	struct iavf_fdir_ip ip_mask;

	enum virtchnl_action action;
	u32 flow_id;

	u32 loc;	/* Rule location inside the flow table */
	u32 q_index;

	struct virtchnl_fdir_add vc_add_msg;
};

int iavf_fill_fdir_add_msg(struct iavf_adapter *adapter, struct iavf_fdir_fltr *fltr);
void iavf_print_fdir_fltr(struct iavf_adapter *adapter, struct iavf_fdir_fltr *fltr);
bool iavf_fdir_is_dup_fltr(struct iavf_adapter *adapter, struct iavf_fdir_fltr *fltr);
void iavf_fdir_list_add_fltr(struct iavf_adapter *adapter, struct iavf_fdir_fltr *fltr);
struct iavf_fdir_fltr *iavf_find_fdir_fltr_by_loc(struct iavf_adapter *adapter, u32 loc);
#endif /* _IAVF_FDIR_H_ */
