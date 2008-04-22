/*
 * Copyright (c) 2005 Ammasso, Inc. All rights reserved.
 * Copyright (c) 2005 Open Grid Computing, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef _C2_WR_H_
#define _C2_WR_H_

#ifdef CCDEBUG
#define CCWR_MAGIC		0xb07700b0
#endif

#define C2_QP_NO_ATTR_CHANGE 0xFFFFFFFF

/* Maximum allowed size in bytes of private_data exchange
 * on connect.
 */
#define C2_MAX_PRIVATE_DATA_SIZE 200

/*
 * These types are shared among the adapter, host, and CCIL consumer.
 */
enum c2_cq_notification_type {
	C2_CQ_NOTIFICATION_TYPE_NONE = 1,
	C2_CQ_NOTIFICATION_TYPE_NEXT,
	C2_CQ_NOTIFICATION_TYPE_NEXT_SE
};

enum c2_setconfig_cmd {
	C2_CFG_ADD_ADDR = 1,
	C2_CFG_DEL_ADDR = 2,
	C2_CFG_ADD_ROUTE = 3,
	C2_CFG_DEL_ROUTE = 4
};

enum c2_getconfig_cmd {
	C2_GETCONFIG_ROUTES = 1,
	C2_GETCONFIG_ADDRS
};

/*
 *  CCIL Work Request Identifiers
 */
enum c2wr_ids {
	CCWR_RNIC_OPEN = 1,
	CCWR_RNIC_QUERY,
	CCWR_RNIC_SETCONFIG,
	CCWR_RNIC_GETCONFIG,
	CCWR_RNIC_CLOSE,
	CCWR_CQ_CREATE,
	CCWR_CQ_QUERY,
	CCWR_CQ_MODIFY,
	CCWR_CQ_DESTROY,
	CCWR_QP_CONNECT,
	CCWR_PD_ALLOC,
	CCWR_PD_DEALLOC,
	CCWR_SRQ_CREATE,
	CCWR_SRQ_QUERY,
	CCWR_SRQ_MODIFY,
	CCWR_SRQ_DESTROY,
	CCWR_QP_CREATE,
	CCWR_QP_QUERY,
	CCWR_QP_MODIFY,
	CCWR_QP_DESTROY,
	CCWR_NSMR_STAG_ALLOC,
	CCWR_NSMR_REGISTER,
	CCWR_NSMR_PBL,
	CCWR_STAG_DEALLOC,
	CCWR_NSMR_REREGISTER,
	CCWR_SMR_REGISTER,
	CCWR_MR_QUERY,
	CCWR_MW_ALLOC,
	CCWR_MW_QUERY,
	CCWR_EP_CREATE,
	CCWR_EP_GETOPT,
	CCWR_EP_SETOPT,
	CCWR_EP_DESTROY,
	CCWR_EP_BIND,
	CCWR_EP_CONNECT,
	CCWR_EP_LISTEN,
	CCWR_EP_SHUTDOWN,
	CCWR_EP_LISTEN_CREATE,
	CCWR_EP_LISTEN_DESTROY,
	CCWR_EP_QUERY,
	CCWR_CR_ACCEPT,
	CCWR_CR_REJECT,
	CCWR_CONSOLE,
	CCWR_TERM,
	CCWR_FLASH_INIT,
	CCWR_FLASH,
	CCWR_BUF_ALLOC,
	CCWR_BUF_FREE,
	CCWR_FLASH_WRITE,
	CCWR_INIT,		/* WARNING: Don't move this ever again! */



	/* Add new IDs here */



	/*
	 * WARNING: CCWR_LAST must always be the last verbs id defined!
	 *          All the preceding IDs are fixed, and must not change.
	 *          You can add new IDs, but must not remove or reorder
	 *          any IDs. If you do, YOU will ruin any hope of
	 *          compatability between versions.
	 */
	CCWR_LAST,

	/*
	 * Start over at 1 so that arrays indexed by user wr id's
	 * begin at 1.  This is OK since the verbs and user wr id's
	 * are always used on disjoint sets of queues.
	 */
	/*
	 * The order of the CCWR_SEND_XX verbs must
	 * match the order of the RDMA_OPs
	 */
	CCWR_SEND = 1,
	CCWR_SEND_INV,
	CCWR_SEND_SE,
	CCWR_SEND_SE_INV,
	CCWR_RDMA_WRITE,
	CCWR_RDMA_READ,
	CCWR_RDMA_READ_INV,
	CCWR_MW_BIND,
	CCWR_NSMR_FASTREG,
	CCWR_STAG_INVALIDATE,
	CCWR_RECV,
	CCWR_NOP,
	CCWR_UNIMPL,
/* WARNING: This must always be the last user wr id defined! */
};
#define RDMA_SEND_OPCODE_FROM_WR_ID(x)   (x+2)

/*
 * SQ/RQ Work Request Types
 */
enum c2_wr_type {
	C2_WR_TYPE_SEND = CCWR_SEND,
	C2_WR_TYPE_SEND_SE = CCWR_SEND_SE,
	C2_WR_TYPE_SEND_INV = CCWR_SEND_INV,
	C2_WR_TYPE_SEND_SE_INV = CCWR_SEND_SE_INV,
	C2_WR_TYPE_RDMA_WRITE = CCWR_RDMA_WRITE,
	C2_WR_TYPE_RDMA_READ = CCWR_RDMA_READ,
	C2_WR_TYPE_RDMA_READ_INV_STAG = CCWR_RDMA_READ_INV,
	C2_WR_TYPE_BIND_MW = CCWR_MW_BIND,
	C2_WR_TYPE_FASTREG_NSMR = CCWR_NSMR_FASTREG,
	C2_WR_TYPE_INV_STAG = CCWR_STAG_INVALIDATE,
	C2_WR_TYPE_RECV = CCWR_RECV,
	C2_WR_TYPE_NOP = CCWR_NOP,
};

struct c2_netaddr {
	__be32 ip_addr;
	__be32 netmask;
	u32 mtu;
};

struct c2_route {
	u32 ip_addr;		/* 0 indicates the default route */
	u32 netmask;		/* netmask associated with dst */
	u32 flags;
	union {
		u32 ipaddr;	/* address of the nexthop interface */
		u8 enaddr[6];
	} nexthop;
};

/*
 * A Scatter Gather Entry.
 */
struct c2_data_addr {
	__be32 stag;
	__be32 length;
	__be64 to;
};

/*
 * MR and MW flags used by the consumer, RI, and RNIC.
 */
enum c2_mm_flags {
	MEM_REMOTE = 0x0001,	/* allow mw binds with remote access. */
	MEM_VA_BASED = 0x0002,	/* Not Zero-based */
	MEM_PBL_COMPLETE = 0x0004,	/* PBL array is complete in this msg */
	MEM_LOCAL_READ = 0x0008,	/* allow local reads */
	MEM_LOCAL_WRITE = 0x0010,	/* allow local writes */
	MEM_REMOTE_READ = 0x0020,	/* allow remote reads */
	MEM_REMOTE_WRITE = 0x0040,	/* allow remote writes */
	MEM_WINDOW_BIND = 0x0080,	/* binds allowed */
	MEM_SHARED = 0x0100,	/* set if MR is shared */
	MEM_STAG_VALID = 0x0200	/* set if STAG is in valid state */
};

/*
 * CCIL API ACF flags defined in terms of the low level mem flags.
 * This minimizes translation needed in the user API
 */
enum c2_acf {
	C2_ACF_LOCAL_READ = MEM_LOCAL_READ,
	C2_ACF_LOCAL_WRITE = MEM_LOCAL_WRITE,
	C2_ACF_REMOTE_READ = MEM_REMOTE_READ,
	C2_ACF_REMOTE_WRITE = MEM_REMOTE_WRITE,
	C2_ACF_WINDOW_BIND = MEM_WINDOW_BIND
};

/*
 * Image types of objects written to flash
 */
#define C2_FLASH_IMG_BITFILE 1
#define C2_FLASH_IMG_OPTION_ROM 2
#define C2_FLASH_IMG_VPD 3

/*
 *  to fix bug 1815 we define the max size allowable of the
 *  terminate message (per the IETF spec).Refer to the IETF
 *  protocal specification, section 12.1.6, page 64)
 *  The message is prefixed by 20 types of DDP info.
 *
 *  Then the message has 6 bytes for the terminate control
 *  and DDP segment length info plus a DDP header (either
 *  14 or 18 byts) plus 28 bytes for the RDMA header.
 *  Thus the max size in:
 *  20 + (6 + 18 + 28) = 72
 */
#define C2_MAX_TERMINATE_MESSAGE_SIZE (72)

/*
 * Build String Length.  It must be the same as C2_BUILD_STR_LEN in ccil_api.h
 */
#define WR_BUILD_STR_LEN 64

/*
 * WARNING:  All of these structs need to align any 64bit types on
 * 64 bit boundaries!  64bit types include u64 and u64.
 */

/*
 * Clustercore Work Request Header.  Be sensitive to field layout
 * and alignment.
 */
struct c2wr_hdr {
	/* wqe_count is part of the cqe.  It is put here so the
	 * adapter can write to it while the wr is pending without
	 * clobbering part of the wr.  This word need not be dma'd
	 * from the host to adapter by libccil, but we copy it anyway
	 * to make the memcpy to the adapter better aligned.
	 */
	__be32 wqe_count;

	/* Put these fields next so that later 32- and 64-bit
	 * quantities are naturally aligned.
	 */
	u8 id;
	u8 result;		/* adapter -> host */
	u8 sge_count;		/* host -> adapter */
	u8 flags;		/* host -> adapter */

	u64 context;
#ifdef CCMSGMAGIC
	u32 magic;
	u32 pad;
#endif
} __attribute__((packed));

/*
 *------------------------ RNIC ------------------------
 */

/*
 * WR_RNIC_OPEN
 */

/*
 * Flags for the RNIC WRs
 */
enum c2_rnic_flags {
	RNIC_IRD_STATIC = 0x0001,
	RNIC_ORD_STATIC = 0x0002,
	RNIC_QP_STATIC = 0x0004,
	RNIC_SRQ_SUPPORTED = 0x0008,
	RNIC_PBL_BLOCK_MODE = 0x0010,
	RNIC_SRQ_MODEL_ARRIVAL = 0x0020,
	RNIC_CQ_OVF_DETECTED = 0x0040,
	RNIC_PRIV_MODE = 0x0080
};

struct c2wr_rnic_open_req {
	struct c2wr_hdr hdr;
	u64 user_context;
	__be16 flags;		/* See enum c2_rnic_flags */
	__be16 port_num;
} __attribute__((packed));

struct c2wr_rnic_open_rep {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
} __attribute__((packed));

union c2wr_rnic_open {
	struct c2wr_rnic_open_req req;
	struct c2wr_rnic_open_rep rep;
} __attribute__((packed));

struct c2wr_rnic_query_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
} __attribute__((packed));

/*
 * WR_RNIC_QUERY
 */
struct c2wr_rnic_query_rep {
	struct c2wr_hdr hdr;
	u64 user_context;
	__be32 vendor_id;
	__be32 part_number;
	__be32 hw_version;
	__be32 fw_ver_major;
	__be32 fw_ver_minor;
	__be32 fw_ver_patch;
	char fw_ver_build_str[WR_BUILD_STR_LEN];
	__be32 max_qps;
	__be32 max_qp_depth;
	u32 max_srq_depth;
	u32 max_send_sgl_depth;
	u32 max_rdma_sgl_depth;
	__be32 max_cqs;
	__be32 max_cq_depth;
	u32 max_cq_event_handlers;
	__be32 max_mrs;
	u32 max_pbl_depth;
	__be32 max_pds;
	__be32 max_global_ird;
	u32 max_global_ord;
	__be32 max_qp_ird;
	__be32 max_qp_ord;
	u32 flags;
	__be32 max_mws;
	u32 pbe_range_low;
	u32 pbe_range_high;
	u32 max_srqs;
	u32 page_size;
} __attribute__((packed));

union c2wr_rnic_query {
	struct c2wr_rnic_query_req req;
	struct c2wr_rnic_query_rep rep;
} __attribute__((packed));

/*
 * WR_RNIC_GETCONFIG
 */

struct c2wr_rnic_getconfig_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 option;		/* see c2_getconfig_cmd_t */
	u64 reply_buf;
	u32 reply_buf_len;
} __attribute__((packed)) ;

struct c2wr_rnic_getconfig_rep {
	struct c2wr_hdr hdr;
	u32 option;		/* see c2_getconfig_cmd_t */
	u32 count_len;		/* length of the number of addresses configured */
} __attribute__((packed)) ;

union c2wr_rnic_getconfig {
	struct c2wr_rnic_getconfig_req req;
	struct c2wr_rnic_getconfig_rep rep;
} __attribute__((packed)) ;

/*
 * WR_RNIC_SETCONFIG
 */
struct c2wr_rnic_setconfig_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	__be32 option;		/* See c2_setconfig_cmd_t */
	/* variable data and pad. See c2_netaddr and c2_route */
	u8 data[0];
} __attribute__((packed)) ;

struct c2wr_rnic_setconfig_rep {
	struct c2wr_hdr hdr;
} __attribute__((packed)) ;

union c2wr_rnic_setconfig {
	struct c2wr_rnic_setconfig_req req;
	struct c2wr_rnic_setconfig_rep rep;
} __attribute__((packed)) ;

/*
 * WR_RNIC_CLOSE
 */
struct c2wr_rnic_close_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
} __attribute__((packed)) ;

struct c2wr_rnic_close_rep {
	struct c2wr_hdr hdr;
} __attribute__((packed)) ;

union c2wr_rnic_close {
	struct c2wr_rnic_close_req req;
	struct c2wr_rnic_close_rep rep;
} __attribute__((packed)) ;

/*
 *------------------------ CQ ------------------------
 */
struct c2wr_cq_create_req {
	struct c2wr_hdr hdr;
	__be64 shared_ht;
	u64 user_context;
	__be64 msg_pool;
	u32 rnic_handle;
	__be32 msg_size;
	__be32 depth;
} __attribute__((packed)) ;

struct c2wr_cq_create_rep {
	struct c2wr_hdr hdr;
	__be32 mq_index;
	__be32 adapter_shared;
	u32 cq_handle;
} __attribute__((packed)) ;

union c2wr_cq_create {
	struct c2wr_cq_create_req req;
	struct c2wr_cq_create_rep rep;
} __attribute__((packed)) ;

struct c2wr_cq_modify_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 cq_handle;
	u32 new_depth;
	u64 new_msg_pool;
} __attribute__((packed)) ;

struct c2wr_cq_modify_rep {
	struct c2wr_hdr hdr;
} __attribute__((packed)) ;

union c2wr_cq_modify {
	struct c2wr_cq_modify_req req;
	struct c2wr_cq_modify_rep rep;
} __attribute__((packed)) ;

struct c2wr_cq_destroy_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 cq_handle;
} __attribute__((packed)) ;

struct c2wr_cq_destroy_rep {
	struct c2wr_hdr hdr;
} __attribute__((packed)) ;

union c2wr_cq_destroy {
	struct c2wr_cq_destroy_req req;
	struct c2wr_cq_destroy_rep rep;
} __attribute__((packed)) ;

/*
 *------------------------ PD ------------------------
 */
struct c2wr_pd_alloc_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 pd_id;
} __attribute__((packed)) ;

struct c2wr_pd_alloc_rep {
	struct c2wr_hdr hdr;
} __attribute__((packed)) ;

union c2wr_pd_alloc {
	struct c2wr_pd_alloc_req req;
	struct c2wr_pd_alloc_rep rep;
} __attribute__((packed)) ;

struct c2wr_pd_dealloc_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 pd_id;
} __attribute__((packed)) ;

struct c2wr_pd_dealloc_rep {
	struct c2wr_hdr hdr;
} __attribute__((packed)) ;

union c2wr_pd_dealloc {
	struct c2wr_pd_dealloc_req req;
	struct c2wr_pd_dealloc_rep rep;
} __attribute__((packed)) ;

/*
 *------------------------ SRQ ------------------------
 */
struct c2wr_srq_create_req {
	struct c2wr_hdr hdr;
	u64 shared_ht;
	u64 user_context;
	u32 rnic_handle;
	u32 srq_depth;
	u32 srq_limit;
	u32 sgl_depth;
	u32 pd_id;
} __attribute__((packed)) ;

struct c2wr_srq_create_rep {
	struct c2wr_hdr hdr;
	u32 srq_depth;
	u32 sgl_depth;
	u32 msg_size;
	u32 mq_index;
	u32 mq_start;
	u32 srq_handle;
} __attribute__((packed)) ;

union c2wr_srq_create {
	struct c2wr_srq_create_req req;
	struct c2wr_srq_create_rep rep;
} __attribute__((packed)) ;

struct c2wr_srq_destroy_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 srq_handle;
} __attribute__((packed)) ;

struct c2wr_srq_destroy_rep {
	struct c2wr_hdr hdr;
} __attribute__((packed)) ;

union c2wr_srq_destroy {
	struct c2wr_srq_destroy_req req;
	struct c2wr_srq_destroy_rep rep;
} __attribute__((packed)) ;

/*
 *------------------------ QP ------------------------
 */
enum c2wr_qp_flags {
	QP_RDMA_READ = 0x00000001,	/* RDMA read enabled? */
	QP_RDMA_WRITE = 0x00000002,	/* RDMA write enabled? */
	QP_MW_BIND = 0x00000004,	/* MWs enabled */
	QP_ZERO_STAG = 0x00000008,	/* enabled? */
	QP_REMOTE_TERMINATION = 0x00000010,	/* remote end terminated */
	QP_RDMA_READ_RESPONSE = 0x00000020	/* Remote RDMA read  */
	    /* enabled? */
};

struct c2wr_qp_create_req {
	struct c2wr_hdr hdr;
	__be64 shared_sq_ht;
	__be64 shared_rq_ht;
	u64 user_context;
	u32 rnic_handle;
	u32 sq_cq_handle;
	u32 rq_cq_handle;
	__be32 sq_depth;
	__be32 rq_depth;
	u32 srq_handle;
	u32 srq_limit;
	__be32 flags;		/* see enum c2wr_qp_flags */
	__be32 send_sgl_depth;
	__be32 recv_sgl_depth;
	__be32 rdma_write_sgl_depth;
	__be32 ord;
	__be32 ird;
	u32 pd_id;
} __attribute__((packed)) ;

struct c2wr_qp_create_rep {
	struct c2wr_hdr hdr;
	__be32 sq_depth;
	__be32 rq_depth;
	u32 send_sgl_depth;
	u32 recv_sgl_depth;
	u32 rdma_write_sgl_depth;
	u32 ord;
	u32 ird;
	__be32 sq_msg_size;
	__be32 sq_mq_index;
	__be32 sq_mq_start;
	__be32 rq_msg_size;
	__be32 rq_mq_index;
	__be32 rq_mq_start;
	u32 qp_handle;
} __attribute__((packed)) ;

union c2wr_qp_create {
	struct c2wr_qp_create_req req;
	struct c2wr_qp_create_rep rep;
} __attribute__((packed)) ;

struct c2wr_qp_query_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 qp_handle;
} __attribute__((packed)) ;

struct c2wr_qp_query_rep {
	struct c2wr_hdr hdr;
	u64 user_context;
	u32 rnic_handle;
	u32 sq_depth;
	u32 rq_depth;
	u32 send_sgl_depth;
	u32 rdma_write_sgl_depth;
	u32 recv_sgl_depth;
	u32 ord;
	u32 ird;
	u16 qp_state;
	u16 flags;		/* see c2wr_qp_flags_t */
	u32 qp_id;
	u32 local_addr;
	u32 remote_addr;
	u16 local_port;
	u16 remote_port;
	u32 terminate_msg_length;	/* 0 if not present */
	u8 data[0];
	/* Terminate Message in-line here. */
} __attribute__((packed)) ;

union c2wr_qp_query {
	struct c2wr_qp_query_req req;
	struct c2wr_qp_query_rep rep;
} __attribute__((packed)) ;

struct c2wr_qp_modify_req {
	struct c2wr_hdr hdr;
	u64 stream_msg;
	u32 stream_msg_length;
	u32 rnic_handle;
	u32 qp_handle;
	__be32 next_qp_state;
	__be32 ord;
	__be32 ird;
	__be32 sq_depth;
	__be32 rq_depth;
	u32 llp_ep_handle;
} __attribute__((packed)) ;

struct c2wr_qp_modify_rep {
	struct c2wr_hdr hdr;
	u32 ord;
	u32 ird;
	u32 sq_depth;
	u32 rq_depth;
	u32 sq_msg_size;
	u32 sq_mq_index;
	u32 sq_mq_start;
	u32 rq_msg_size;
	u32 rq_mq_index;
	u32 rq_mq_start;
} __attribute__((packed)) ;

union c2wr_qp_modify {
	struct c2wr_qp_modify_req req;
	struct c2wr_qp_modify_rep rep;
} __attribute__((packed)) ;

struct c2wr_qp_destroy_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 qp_handle;
} __attribute__((packed)) ;

struct c2wr_qp_destroy_rep {
	struct c2wr_hdr hdr;
} __attribute__((packed)) ;

union c2wr_qp_destroy {
	struct c2wr_qp_destroy_req req;
	struct c2wr_qp_destroy_rep rep;
} __attribute__((packed)) ;

/*
 * The CCWR_QP_CONNECT msg is posted on the verbs request queue.  It can
 * only be posted when a QP is in IDLE state.  After the connect request is
 * submitted to the LLP, the adapter moves the QP to CONNECT_PENDING state.
 * No synchronous reply from adapter to this WR.  The results of
 * connection are passed back in an async event CCAE_ACTIVE_CONNECT_RESULTS
 * See c2wr_ae_active_connect_results_t
 */
struct c2wr_qp_connect_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 qp_handle;
	__be32 remote_addr;
	__be16 remote_port;
	u16 pad;
	__be32 private_data_length;
	u8 private_data[0];	/* Private data in-line. */
} __attribute__((packed)) ;

struct c2wr_qp_connect {
	struct c2wr_qp_connect_req req;
	/* no synchronous reply.         */
} __attribute__((packed)) ;


/*
 *------------------------ MM ------------------------
 */

struct c2wr_nsmr_stag_alloc_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 pbl_depth;
	u32 pd_id;
	u32 flags;
} __attribute__((packed)) ;

struct c2wr_nsmr_stag_alloc_rep {
	struct c2wr_hdr hdr;
	u32 pbl_depth;
	u32 stag_index;
} __attribute__((packed)) ;

union c2wr_nsmr_stag_alloc {
	struct c2wr_nsmr_stag_alloc_req req;
	struct c2wr_nsmr_stag_alloc_rep rep;
} __attribute__((packed)) ;

struct c2wr_nsmr_register_req {
	struct c2wr_hdr hdr;
	__be64 va;
	u32 rnic_handle;
	__be16 flags;
	u8 stag_key;
	u8 pad;
	u32 pd_id;
	__be32 pbl_depth;
	__be32 pbe_size;
	__be32 fbo;
	__be32 length;
	__be32 addrs_length;
	/* array of paddrs (must be aligned on a 64bit boundary) */
	__be64 paddrs[0];
} __attribute__((packed)) ;

struct c2wr_nsmr_register_rep {
	struct c2wr_hdr hdr;
	u32 pbl_depth;
	__be32 stag_index;
} __attribute__((packed)) ;

union c2wr_nsmr_register {
	struct c2wr_nsmr_register_req req;
	struct c2wr_nsmr_register_rep rep;
} __attribute__((packed)) ;

struct c2wr_nsmr_pbl_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	__be32 flags;
	__be32 stag_index;
	__be32 addrs_length;
	/* array of paddrs (must be aligned on a 64bit boundary) */
	__be64 paddrs[0];
} __attribute__((packed)) ;

struct c2wr_nsmr_pbl_rep {
	struct c2wr_hdr hdr;
} __attribute__((packed)) ;

union c2wr_nsmr_pbl {
	struct c2wr_nsmr_pbl_req req;
	struct c2wr_nsmr_pbl_rep rep;
} __attribute__((packed)) ;

struct c2wr_mr_query_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 stag_index;
} __attribute__((packed)) ;

struct c2wr_mr_query_rep {
	struct c2wr_hdr hdr;
	u8 stag_key;
	u8 pad[3];
	u32 pd_id;
	u32 flags;
	u32 pbl_depth;
} __attribute__((packed)) ;

union c2wr_mr_query {
	struct c2wr_mr_query_req req;
	struct c2wr_mr_query_rep rep;
} __attribute__((packed)) ;

struct c2wr_mw_query_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 stag_index;
} __attribute__((packed)) ;

struct c2wr_mw_query_rep {
	struct c2wr_hdr hdr;
	u8 stag_key;
	u8 pad[3];
	u32 pd_id;
	u32 flags;
} __attribute__((packed)) ;

union c2wr_mw_query {
	struct c2wr_mw_query_req req;
	struct c2wr_mw_query_rep rep;
} __attribute__((packed)) ;


struct c2wr_stag_dealloc_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	__be32 stag_index;
} __attribute__((packed)) ;

struct c2wr_stag_dealloc_rep {
	struct c2wr_hdr hdr;
} __attribute__((packed)) ;

union c2wr_stag_dealloc {
	struct c2wr_stag_dealloc_req req;
	struct c2wr_stag_dealloc_rep rep;
} __attribute__((packed)) ;

struct c2wr_nsmr_reregister_req {
	struct c2wr_hdr hdr;
	u64 va;
	u32 rnic_handle;
	u16 flags;
	u8 stag_key;
	u8 pad;
	u32 stag_index;
	u32 pd_id;
	u32 pbl_depth;
	u32 pbe_size;
	u32 fbo;
	u32 length;
	u32 addrs_length;
	u32 pad1;
	/* array of paddrs (must be aligned on a 64bit boundary) */
	u64 paddrs[0];
} __attribute__((packed)) ;

struct c2wr_nsmr_reregister_rep {
	struct c2wr_hdr hdr;
	u32 pbl_depth;
	u32 stag_index;
} __attribute__((packed)) ;

union c2wr_nsmr_reregister {
	struct c2wr_nsmr_reregister_req req;
	struct c2wr_nsmr_reregister_rep rep;
} __attribute__((packed)) ;

struct c2wr_smr_register_req {
	struct c2wr_hdr hdr;
	u64 va;
	u32 rnic_handle;
	u16 flags;
	u8 stag_key;
	u8 pad;
	u32 stag_index;
	u32 pd_id;
} __attribute__((packed)) ;

struct c2wr_smr_register_rep {
	struct c2wr_hdr hdr;
	u32 stag_index;
} __attribute__((packed)) ;

union c2wr_smr_register {
	struct c2wr_smr_register_req req;
	struct c2wr_smr_register_rep rep;
} __attribute__((packed)) ;

struct c2wr_mw_alloc_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 pd_id;
} __attribute__((packed)) ;

struct c2wr_mw_alloc_rep {
	struct c2wr_hdr hdr;
	u32 stag_index;
} __attribute__((packed)) ;

union c2wr_mw_alloc {
	struct c2wr_mw_alloc_req req;
	struct c2wr_mw_alloc_rep rep;
} __attribute__((packed)) ;

/*
 *------------------------ WRs -----------------------
 */

struct c2wr_user_hdr {
	struct c2wr_hdr hdr;		/* Has status and WR Type */
} __attribute__((packed)) ;

enum c2_qp_state {
	C2_QP_STATE_IDLE = 0x01,
	C2_QP_STATE_CONNECTING = 0x02,
	C2_QP_STATE_RTS = 0x04,
	C2_QP_STATE_CLOSING = 0x08,
	C2_QP_STATE_TERMINATE = 0x10,
	C2_QP_STATE_ERROR = 0x20,
};

/* Completion queue entry. */
struct c2wr_ce {
	struct c2wr_hdr hdr;		/* Has status and WR Type */
	u64 qp_user_context;	/* c2_user_qp_t * */
	u32 qp_state;		/* Current QP State */
	u32 handle;		/* QPID or EP Handle */
	__be32 bytes_rcvd;		/* valid for RECV WCs */
	u32 stag;
} __attribute__((packed)) ;


/*
 * Flags used for all post-sq WRs.  These must fit in the flags
 * field of the struct c2wr_hdr (eight bits).
 */
enum {
	SQ_SIGNALED = 0x01,
	SQ_READ_FENCE = 0x02,
	SQ_FENCE = 0x04,
};

/*
 * Common fields for all post-sq WRs.  Namely the standard header and a
 * secondary header with fields common to all post-sq WRs.
 */
struct c2_sq_hdr {
	struct c2wr_user_hdr user_hdr;
} __attribute__((packed));

/*
 * Same as above but for post-rq WRs.
 */
struct c2_rq_hdr {
	struct c2wr_user_hdr user_hdr;
} __attribute__((packed));

/*
 * use the same struct for all sends.
 */
struct c2wr_send_req {
	struct c2_sq_hdr sq_hdr;
	__be32 sge_len;
	__be32 remote_stag;
	u8 data[0];		/* SGE array */
} __attribute__((packed));

union c2wr_send {
	struct c2wr_send_req req;
	struct c2wr_ce rep;
} __attribute__((packed));

struct c2wr_rdma_write_req {
	struct c2_sq_hdr sq_hdr;
	__be64 remote_to;
	__be32 remote_stag;
	__be32 sge_len;
	u8 data[0];		/* SGE array */
} __attribute__((packed));

union c2wr_rdma_write {
	struct c2wr_rdma_write_req req;
	struct c2wr_ce rep;
} __attribute__((packed));

struct c2wr_rdma_read_req {
	struct c2_sq_hdr sq_hdr;
	__be64 local_to;
	__be64 remote_to;
	__be32 local_stag;
	__be32 remote_stag;
	__be32 length;
} __attribute__((packed));

union c2wr_rdma_read {
	struct c2wr_rdma_read_req req;
	struct c2wr_ce rep;
} __attribute__((packed));

struct c2wr_mw_bind_req {
	struct c2_sq_hdr sq_hdr;
	u64 va;
	u8 stag_key;
	u8 pad[3];
	u32 mw_stag_index;
	u32 mr_stag_index;
	u32 length;
	u32 flags;
} __attribute__((packed));

union c2wr_mw_bind {
	struct c2wr_mw_bind_req req;
	struct c2wr_ce rep;
} __attribute__((packed));

struct c2wr_nsmr_fastreg_req {
	struct c2_sq_hdr sq_hdr;
	u64 va;
	u8 stag_key;
	u8 pad[3];
	u32 stag_index;
	u32 pbe_size;
	u32 fbo;
	u32 length;
	u32 addrs_length;
	/* array of paddrs (must be aligned on a 64bit boundary) */
	u64 paddrs[0];
} __attribute__((packed));

union c2wr_nsmr_fastreg {
	struct c2wr_nsmr_fastreg_req req;
	struct c2wr_ce rep;
} __attribute__((packed));

struct c2wr_stag_invalidate_req {
	struct c2_sq_hdr sq_hdr;
	u8 stag_key;
	u8 pad[3];
	u32 stag_index;
} __attribute__((packed));

union c2wr_stag_invalidate {
	struct c2wr_stag_invalidate_req req;
	struct c2wr_ce rep;
} __attribute__((packed));

union c2wr_sqwr {
	struct c2_sq_hdr sq_hdr;
	struct c2wr_send_req send;
	struct c2wr_send_req send_se;
	struct c2wr_send_req send_inv;
	struct c2wr_send_req send_se_inv;
	struct c2wr_rdma_write_req rdma_write;
	struct c2wr_rdma_read_req rdma_read;
	struct c2wr_mw_bind_req mw_bind;
	struct c2wr_nsmr_fastreg_req nsmr_fastreg;
	struct c2wr_stag_invalidate_req stag_inv;
} __attribute__((packed));


/*
 * RQ WRs
 */
struct c2wr_rqwr {
	struct c2_rq_hdr rq_hdr;
	u8 data[0];		/* array of SGEs */
} __attribute__((packed));

union c2wr_recv {
	struct c2wr_rqwr req;
	struct c2wr_ce rep;
} __attribute__((packed));

/*
 * All AEs start with this header.  Most AEs only need to convey the
 * information in the header.  Some, like LLP connection events, need
 * more info.  The union typdef c2wr_ae_t has all the possible AEs.
 *
 * hdr.context is the user_context from the rnic_open WR.  NULL If this
 * is not affiliated with an rnic
 *
 * hdr.id is the AE identifier (eg;  CCAE_REMOTE_SHUTDOWN,
 * CCAE_LLP_CLOSE_COMPLETE)
 *
 * resource_type is one of:  C2_RES_IND_QP, C2_RES_IND_CQ, C2_RES_IND_SRQ
 *
 * user_context is the context passed down when the host created the resource.
 */
struct c2wr_ae_hdr {
	struct c2wr_hdr hdr;
	u64 user_context;	/* user context for this res. */
	__be32 resource_type;	/* see enum c2_resource_indicator */
	__be32 resource;	/* handle for resource */
	__be32 qp_state;	/* current QP State */
} __attribute__((packed));

/*
 * After submitting the CCAE_ACTIVE_CONNECT_RESULTS message on the AEQ,
 * the adapter moves the QP into RTS state
 */
struct c2wr_ae_active_connect_results {
	struct c2wr_ae_hdr ae_hdr;
	__be32 laddr;
	__be32 raddr;
	__be16 lport;
	__be16 rport;
	__be32 private_data_length;
	u8 private_data[0];	/* data is in-line in the msg. */
} __attribute__((packed));

/*
 * When connections are established by the stack (and the private data
 * MPA frame is received), the adapter will generate an event to the host.
 * The details of the connection, any private data, and the new connection
 * request handle is passed up via the CCAE_CONNECTION_REQUEST msg on the
 * AE queue:
 */
struct c2wr_ae_connection_request {
	struct c2wr_ae_hdr ae_hdr;
	u32 cr_handle;		/* connreq handle (sock ptr) */
	__be32 laddr;
	__be32 raddr;
	__be16 lport;
	__be16 rport;
	__be32 private_data_length;
	u8 private_data[0];	/* data is in-line in the msg. */
} __attribute__((packed));

union c2wr_ae {
	struct c2wr_ae_hdr ae_generic;
	struct c2wr_ae_active_connect_results ae_active_connect_results;
	struct c2wr_ae_connection_request ae_connection_request;
} __attribute__((packed));

struct c2wr_init_req {
	struct c2wr_hdr hdr;
	__be64 hint_count;
	__be64 q0_host_shared;
	__be64 q1_host_shared;
	__be64 q1_host_msg_pool;
	__be64 q2_host_shared;
	__be64 q2_host_msg_pool;
} __attribute__((packed));

struct c2wr_init_rep {
	struct c2wr_hdr hdr;
} __attribute__((packed));

union c2wr_init {
	struct c2wr_init_req req;
	struct c2wr_init_rep rep;
} __attribute__((packed));

/*
 * For upgrading flash.
 */

struct c2wr_flash_init_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
} __attribute__((packed));

struct c2wr_flash_init_rep {
	struct c2wr_hdr hdr;
	u32 adapter_flash_buf_offset;
	u32 adapter_flash_len;
} __attribute__((packed));

union c2wr_flash_init {
	struct c2wr_flash_init_req req;
	struct c2wr_flash_init_rep rep;
} __attribute__((packed));

struct c2wr_flash_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 len;
} __attribute__((packed));

struct c2wr_flash_rep {
	struct c2wr_hdr hdr;
	u32 status;
} __attribute__((packed));

union c2wr_flash {
	struct c2wr_flash_req req;
	struct c2wr_flash_rep rep;
} __attribute__((packed));

struct c2wr_buf_alloc_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 size;
} __attribute__((packed));

struct c2wr_buf_alloc_rep {
	struct c2wr_hdr hdr;
	u32 offset;		/* 0 if mem not available */
	u32 size;		/* 0 if mem not available */
} __attribute__((packed));

union c2wr_buf_alloc {
	struct c2wr_buf_alloc_req req;
	struct c2wr_buf_alloc_rep rep;
} __attribute__((packed));

struct c2wr_buf_free_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 offset;		/* Must match value from alloc */
	u32 size;		/* Must match value from alloc */
} __attribute__((packed));

struct c2wr_buf_free_rep {
	struct c2wr_hdr hdr;
} __attribute__((packed));

union c2wr_buf_free {
	struct c2wr_buf_free_req req;
	struct c2wr_ce rep;
} __attribute__((packed));

struct c2wr_flash_write_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 offset;
	u32 size;
	u32 type;
	u32 flags;
} __attribute__((packed));

struct c2wr_flash_write_rep {
	struct c2wr_hdr hdr;
	u32 status;
} __attribute__((packed));

union c2wr_flash_write {
	struct c2wr_flash_write_req req;
	struct c2wr_flash_write_rep rep;
} __attribute__((packed));

/*
 * Messages for LLP connection setup.
 */

/*
 * Listen Request.  This allocates a listening endpoint to allow passive
 * connection setup.  Newly established LLP connections are passed up
 * via an AE.  See c2wr_ae_connection_request_t
 */
struct c2wr_ep_listen_create_req {
	struct c2wr_hdr hdr;
	u64 user_context;	/* returned in AEs. */
	u32 rnic_handle;
	__be32 local_addr;		/* local addr, or 0  */
	__be16 local_port;		/* 0 means "pick one" */
	u16 pad;
	__be32 backlog;		/* tradional tcp listen bl */
} __attribute__((packed));

struct c2wr_ep_listen_create_rep {
	struct c2wr_hdr hdr;
	u32 ep_handle;		/* handle to new listening ep */
	u16 local_port;		/* resulting port... */
	u16 pad;
} __attribute__((packed));

union c2wr_ep_listen_create {
	struct c2wr_ep_listen_create_req req;
	struct c2wr_ep_listen_create_rep rep;
} __attribute__((packed));

struct c2wr_ep_listen_destroy_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 ep_handle;
} __attribute__((packed));

struct c2wr_ep_listen_destroy_rep {
	struct c2wr_hdr hdr;
} __attribute__((packed));

union c2wr_ep_listen_destroy {
	struct c2wr_ep_listen_destroy_req req;
	struct c2wr_ep_listen_destroy_rep rep;
} __attribute__((packed));

struct c2wr_ep_query_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 ep_handle;
} __attribute__((packed));

struct c2wr_ep_query_rep {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 local_addr;
	u32 remote_addr;
	u16 local_port;
	u16 remote_port;
} __attribute__((packed));

union c2wr_ep_query {
	struct c2wr_ep_query_req req;
	struct c2wr_ep_query_rep rep;
} __attribute__((packed));


/*
 * The host passes this down to indicate acceptance of a pending iWARP
 * connection.  The cr_handle was obtained from the CONNECTION_REQUEST
 * AE passed up by the adapter.  See c2wr_ae_connection_request_t.
 */
struct c2wr_cr_accept_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 qp_handle;		/* QP to bind to this LLP conn */
	u32 ep_handle;		/* LLP  handle to accept */
	__be32 private_data_length;
	u8 private_data[0];	/* data in-line in msg. */
} __attribute__((packed));

/*
 * adapter sends reply when private data is successfully submitted to
 * the LLP.
 */
struct c2wr_cr_accept_rep {
	struct c2wr_hdr hdr;
} __attribute__((packed));

union c2wr_cr_accept {
	struct c2wr_cr_accept_req req;
	struct c2wr_cr_accept_rep rep;
} __attribute__((packed));

/*
 * The host sends this down if a given iWARP connection request was
 * rejected by the consumer.  The cr_handle was obtained from a
 * previous c2wr_ae_connection_request_t AE sent by the adapter.
 */
struct  c2wr_cr_reject_req {
	struct c2wr_hdr hdr;
	u32 rnic_handle;
	u32 ep_handle;		/* LLP handle to reject */
} __attribute__((packed));

/*
 * Dunno if this is needed, but we'll add it for now.  The adapter will
 * send the reject_reply after the LLP endpoint has been destroyed.
 */
struct  c2wr_cr_reject_rep {
	struct c2wr_hdr hdr;
} __attribute__((packed));

union c2wr_cr_reject {
	struct c2wr_cr_reject_req req;
	struct c2wr_cr_reject_rep rep;
} __attribute__((packed));

/*
 * console command.  Used to implement a debug console over the verbs
 * request and reply queues.
 */

/*
 * Console request message.  It contains:
 *	- message hdr with id = CCWR_CONSOLE
 *	- the physaddr/len of host memory to be used for the reply.
 *	- the command string.  eg:  "netstat -s" or "zoneinfo"
 */
struct c2wr_console_req {
	struct c2wr_hdr hdr;		/* id = CCWR_CONSOLE */
	u64 reply_buf;		/* pinned host buf for reply */
	u32 reply_buf_len;	/* length of reply buffer */
	u8 command[0];		/* NUL terminated ascii string */
	/* containing the command req */
} __attribute__((packed));

/*
 * flags used in the console reply.
 */
enum c2_console_flags {
	CONS_REPLY_TRUNCATED = 0x00000001	/* reply was truncated */
} __attribute__((packed));

/*
 * Console reply message.
 * hdr.result contains the c2_status_t error if the reply was _not_ generated,
 * or C2_OK if the reply was generated.
 */
struct c2wr_console_rep {
	struct c2wr_hdr hdr;		/* id = CCWR_CONSOLE */
	u32 flags;
} __attribute__((packed));

union c2wr_console {
	struct c2wr_console_req req;
	struct c2wr_console_rep rep;
} __attribute__((packed));


/*
 * Giant union with all WRs.  Makes life easier...
 */
union c2wr {
	struct c2wr_hdr hdr;
	struct c2wr_user_hdr user_hdr;
	union c2wr_rnic_open rnic_open;
	union c2wr_rnic_query rnic_query;
	union c2wr_rnic_getconfig rnic_getconfig;
	union c2wr_rnic_setconfig rnic_setconfig;
	union c2wr_rnic_close rnic_close;
	union c2wr_cq_create cq_create;
	union c2wr_cq_modify cq_modify;
	union c2wr_cq_destroy cq_destroy;
	union c2wr_pd_alloc pd_alloc;
	union c2wr_pd_dealloc pd_dealloc;
	union c2wr_srq_create srq_create;
	union c2wr_srq_destroy srq_destroy;
	union c2wr_qp_create qp_create;
	union c2wr_qp_query qp_query;
	union c2wr_qp_modify qp_modify;
	union c2wr_qp_destroy qp_destroy;
	struct c2wr_qp_connect qp_connect;
	union c2wr_nsmr_stag_alloc nsmr_stag_alloc;
	union c2wr_nsmr_register nsmr_register;
	union c2wr_nsmr_pbl nsmr_pbl;
	union c2wr_mr_query mr_query;
	union c2wr_mw_query mw_query;
	union c2wr_stag_dealloc stag_dealloc;
	union c2wr_sqwr sqwr;
	struct c2wr_rqwr rqwr;
	struct c2wr_ce ce;
	union c2wr_ae ae;
	union c2wr_init init;
	union c2wr_ep_listen_create ep_listen_create;
	union c2wr_ep_listen_destroy ep_listen_destroy;
	union c2wr_cr_accept cr_accept;
	union c2wr_cr_reject cr_reject;
	union c2wr_console console;
	union c2wr_flash_init flash_init;
	union c2wr_flash flash;
	union c2wr_buf_alloc buf_alloc;
	union c2wr_buf_free buf_free;
	union c2wr_flash_write flash_write;
} __attribute__((packed));


/*
 * Accessors for the wr fields that are packed together tightly to
 * reduce the wr message size.  The wr arguments are void* so that
 * either a struct c2wr*, a struct c2wr_hdr*, or a pointer to any of the types
 * in the struct c2wr union can be passed in.
 */
static __inline__ u8 c2_wr_get_id(void *wr)
{
	return ((struct c2wr_hdr *) wr)->id;
}
static __inline__ void c2_wr_set_id(void *wr, u8 id)
{
	((struct c2wr_hdr *) wr)->id = id;
}
static __inline__ u8 c2_wr_get_result(void *wr)
{
	return ((struct c2wr_hdr *) wr)->result;
}
static __inline__ void c2_wr_set_result(void *wr, u8 result)
{
	((struct c2wr_hdr *) wr)->result = result;
}
static __inline__ u8 c2_wr_get_flags(void *wr)
{
	return ((struct c2wr_hdr *) wr)->flags;
}
static __inline__ void c2_wr_set_flags(void *wr, u8 flags)
{
	((struct c2wr_hdr *) wr)->flags = flags;
}
static __inline__ u8 c2_wr_get_sge_count(void *wr)
{
	return ((struct c2wr_hdr *) wr)->sge_count;
}
static __inline__ void c2_wr_set_sge_count(void *wr, u8 sge_count)
{
	((struct c2wr_hdr *) wr)->sge_count = sge_count;
}
static __inline__ __be32 c2_wr_get_wqe_count(void *wr)
{
	return ((struct c2wr_hdr *) wr)->wqe_count;
}
static __inline__ void c2_wr_set_wqe_count(void *wr, u32 wqe_count)
{
	((struct c2wr_hdr *) wr)->wqe_count = wqe_count;
}

#endif				/* _C2_WR_H_ */
