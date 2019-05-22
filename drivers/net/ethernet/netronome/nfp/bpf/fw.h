/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#ifndef NFP_BPF_FW_H
#define NFP_BPF_FW_H 1

#include <linux/bitops.h>
#include <linux/types.h>
#include "../ccm.h"

/* Kernel's enum bpf_reg_type is not uABI so people may change it breaking
 * our FW ABI.  In that case we will do translation in the driver.
 */
#define NFP_BPF_SCALAR_VALUE		1
#define NFP_BPF_MAP_VALUE		4
#define NFP_BPF_STACK			6
#define NFP_BPF_PACKET_DATA		8

enum bpf_cap_tlv_type {
	NFP_BPF_CAP_TYPE_FUNC		= 1,
	NFP_BPF_CAP_TYPE_ADJUST_HEAD	= 2,
	NFP_BPF_CAP_TYPE_MAPS		= 3,
	NFP_BPF_CAP_TYPE_RANDOM		= 4,
	NFP_BPF_CAP_TYPE_QUEUE_SELECT	= 5,
	NFP_BPF_CAP_TYPE_ADJUST_TAIL	= 6,
	NFP_BPF_CAP_TYPE_ABI_VERSION	= 7,
};

struct nfp_bpf_cap_tlv_func {
	__le32 func_id;
	__le32 func_addr;
};

struct nfp_bpf_cap_tlv_adjust_head {
	__le32 flags;
	__le32 off_min;
	__le32 off_max;
	__le32 guaranteed_sub;
	__le32 guaranteed_add;
};

#define NFP_BPF_ADJUST_HEAD_NO_META	BIT(0)

struct nfp_bpf_cap_tlv_maps {
	__le32 types;
	__le32 max_maps;
	__le32 max_elems;
	__le32 max_key_sz;
	__le32 max_val_sz;
	__le32 max_elem_sz;
};

/*
 * Types defined for map related control messages
 */

/* BPF ABIv2 fixed-length control message fields */
#define CMSG_MAP_KEY_LW			16
#define CMSG_MAP_VALUE_LW		16

enum nfp_bpf_cmsg_status {
	CMSG_RC_SUCCESS			= 0,
	CMSG_RC_ERR_MAP_FD		= 1,
	CMSG_RC_ERR_MAP_NOENT		= 2,
	CMSG_RC_ERR_MAP_ERR		= 3,
	CMSG_RC_ERR_MAP_PARSE		= 4,
	CMSG_RC_ERR_MAP_EXIST		= 5,
	CMSG_RC_ERR_MAP_NOMEM		= 6,
	CMSG_RC_ERR_MAP_E2BIG		= 7,
};

struct cmsg_reply_map_simple {
	struct nfp_ccm_hdr hdr;
	__be32 rc;
};

struct cmsg_req_map_alloc_tbl {
	struct nfp_ccm_hdr hdr;
	__be32 key_size;		/* in bytes */
	__be32 value_size;		/* in bytes */
	__be32 max_entries;
	__be32 map_type;
	__be32 map_flags;		/* reserved */
};

struct cmsg_reply_map_alloc_tbl {
	struct cmsg_reply_map_simple reply_hdr;
	__be32 tid;
};

struct cmsg_req_map_free_tbl {
	struct nfp_ccm_hdr hdr;
	__be32 tid;
};

struct cmsg_reply_map_free_tbl {
	struct cmsg_reply_map_simple reply_hdr;
	__be32 count;
};

struct cmsg_req_map_op {
	struct nfp_ccm_hdr hdr;
	__be32 tid;
	__be32 count;
	__be32 flags;
	u8 data[0];
};

struct cmsg_reply_map_op {
	struct cmsg_reply_map_simple reply_hdr;
	__be32 count;
	__be32 resv;
	u8 data[0];
};

struct cmsg_bpf_event {
	struct nfp_ccm_hdr hdr;
	__be32 cpu_id;
	__be64 map_ptr;
	__be32 data_size;
	__be32 pkt_size;
	u8 data[0];
};
#endif
