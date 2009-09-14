
/* cnic.c: Broadcom CNIC core network driver.
 *
 * Copyright (c) 2006-2009 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 */

#ifndef CNIC_DEFS_H
#define CNIC_DEFS_H

/* KWQ (kernel work queue) request op codes */
#define L2_KWQE_OPCODE_VALUE_FLUSH                  (4)

#define L4_KWQE_OPCODE_VALUE_CONNECT1               (50)
#define L4_KWQE_OPCODE_VALUE_CONNECT2               (51)
#define L4_KWQE_OPCODE_VALUE_CONNECT3               (52)
#define L4_KWQE_OPCODE_VALUE_RESET                  (53)
#define L4_KWQE_OPCODE_VALUE_CLOSE                  (54)
#define L4_KWQE_OPCODE_VALUE_UPDATE_SECRET          (60)
#define L4_KWQE_OPCODE_VALUE_INIT_ULP               (61)

#define L4_KWQE_OPCODE_VALUE_OFFLOAD_PG             (1)
#define L4_KWQE_OPCODE_VALUE_UPDATE_PG              (9)
#define L4_KWQE_OPCODE_VALUE_UPLOAD_PG              (14)

#define L5CM_RAMROD_CMD_ID_BASE			(0x80)
#define L5CM_RAMROD_CMD_ID_TCP_CONNECT		(L5CM_RAMROD_CMD_ID_BASE + 3)
#define L5CM_RAMROD_CMD_ID_CLOSE		(L5CM_RAMROD_CMD_ID_BASE + 12)
#define L5CM_RAMROD_CMD_ID_ABORT		(L5CM_RAMROD_CMD_ID_BASE + 13)
#define L5CM_RAMROD_CMD_ID_SEARCHER_DELETE	(L5CM_RAMROD_CMD_ID_BASE + 14)
#define L5CM_RAMROD_CMD_ID_TERMINATE_OFFLOAD	(L5CM_RAMROD_CMD_ID_BASE + 15)

/* KCQ (kernel completion queue) response op codes */
#define L4_KCQE_OPCODE_VALUE_CLOSE_COMP             (53)
#define L4_KCQE_OPCODE_VALUE_RESET_COMP             (54)
#define L4_KCQE_OPCODE_VALUE_FW_TCP_UPDATE          (55)
#define L4_KCQE_OPCODE_VALUE_CONNECT_COMPLETE       (56)
#define L4_KCQE_OPCODE_VALUE_RESET_RECEIVED         (57)
#define L4_KCQE_OPCODE_VALUE_CLOSE_RECEIVED         (58)
#define L4_KCQE_OPCODE_VALUE_INIT_ULP               (61)

#define L4_KCQE_OPCODE_VALUE_OFFLOAD_PG             (1)
#define L4_KCQE_OPCODE_VALUE_UPDATE_PG              (9)
#define L4_KCQE_OPCODE_VALUE_UPLOAD_PG              (14)

/* KCQ (kernel completion queue) completion status */
#define L4_KCQE_COMPLETION_STATUS_SUCCESS		    (0)
#define L4_KCQE_COMPLETION_STATUS_TIMEOUT        (0x93)

#define L4_LAYER_CODE (4)
#define L2_LAYER_CODE (2)

/*
 * L4 KCQ CQE
 */
struct l4_kcq {
	u32 cid;
	u32 pg_cid;
	u32 conn_id;
	u32 pg_host_opaque;
#if defined(__BIG_ENDIAN)
	u16 status;
	u16 reserved1;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved1;
	u16 status;
#endif
	u32 reserved2[2];
#if defined(__BIG_ENDIAN)
	u8 flags;
#define L4_KCQ_RESERVED3 (0x7<<0)
#define L4_KCQ_RESERVED3_SHIFT 0
#define L4_KCQ_RAMROD_COMPLETION (0x1<<3) /* Everest only */
#define L4_KCQ_RAMROD_COMPLETION_SHIFT 3
#define L4_KCQ_LAYER_CODE (0x7<<4)
#define L4_KCQ_LAYER_CODE_SHIFT 4
#define L4_KCQ_RESERVED4 (0x1<<7)
#define L4_KCQ_RESERVED4_SHIFT 7
	u8 op_code;
	u16 qe_self_seq;
#elif defined(__LITTLE_ENDIAN)
	u16 qe_self_seq;
	u8 op_code;
	u8 flags;
#define L4_KCQ_RESERVED3 (0xF<<0)
#define L4_KCQ_RESERVED3_SHIFT 0
#define L4_KCQ_RAMROD_COMPLETION (0x1<<3) /* Everest only */
#define L4_KCQ_RAMROD_COMPLETION_SHIFT 3
#define L4_KCQ_LAYER_CODE (0x7<<4)
#define L4_KCQ_LAYER_CODE_SHIFT 4
#define L4_KCQ_RESERVED4 (0x1<<7)
#define L4_KCQ_RESERVED4_SHIFT 7
#endif
};


/*
 * L4 KCQ CQE PG upload
 */
struct l4_kcq_upload_pg {
	u32 pg_cid;
#if defined(__BIG_ENDIAN)
	u16 pg_status;
	u16 pg_ipid_count;
#elif defined(__LITTLE_ENDIAN)
	u16 pg_ipid_count;
	u16 pg_status;
#endif
	u32 reserved1[5];
#if defined(__BIG_ENDIAN)
	u8 flags;
#define L4_KCQ_UPLOAD_PG_RESERVED3 (0xF<<0)
#define L4_KCQ_UPLOAD_PG_RESERVED3_SHIFT 0
#define L4_KCQ_UPLOAD_PG_LAYER_CODE (0x7<<4)
#define L4_KCQ_UPLOAD_PG_LAYER_CODE_SHIFT 4
#define L4_KCQ_UPLOAD_PG_RESERVED4 (0x1<<7)
#define L4_KCQ_UPLOAD_PG_RESERVED4_SHIFT 7
	u8 op_code;
	u16 qe_self_seq;
#elif defined(__LITTLE_ENDIAN)
	u16 qe_self_seq;
	u8 op_code;
	u8 flags;
#define L4_KCQ_UPLOAD_PG_RESERVED3 (0xF<<0)
#define L4_KCQ_UPLOAD_PG_RESERVED3_SHIFT 0
#define L4_KCQ_UPLOAD_PG_LAYER_CODE (0x7<<4)
#define L4_KCQ_UPLOAD_PG_LAYER_CODE_SHIFT 4
#define L4_KCQ_UPLOAD_PG_RESERVED4 (0x1<<7)
#define L4_KCQ_UPLOAD_PG_RESERVED4_SHIFT 7
#endif
};


/*
 * Gracefully close the connection request
 */
struct l4_kwq_close_req {
#if defined(__BIG_ENDIAN)
	u8 flags;
#define L4_KWQ_CLOSE_REQ_RESERVED1 (0xF<<0)
#define L4_KWQ_CLOSE_REQ_RESERVED1_SHIFT 0
#define L4_KWQ_CLOSE_REQ_LAYER_CODE (0x7<<4)
#define L4_KWQ_CLOSE_REQ_LAYER_CODE_SHIFT 4
#define L4_KWQ_CLOSE_REQ_LINKED_WITH_NEXT (0x1<<7)
#define L4_KWQ_CLOSE_REQ_LINKED_WITH_NEXT_SHIFT 7
	u8 op_code;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	u8 op_code;
	u8 flags;
#define L4_KWQ_CLOSE_REQ_RESERVED1 (0xF<<0)
#define L4_KWQ_CLOSE_REQ_RESERVED1_SHIFT 0
#define L4_KWQ_CLOSE_REQ_LAYER_CODE (0x7<<4)
#define L4_KWQ_CLOSE_REQ_LAYER_CODE_SHIFT 4
#define L4_KWQ_CLOSE_REQ_LINKED_WITH_NEXT (0x1<<7)
#define L4_KWQ_CLOSE_REQ_LINKED_WITH_NEXT_SHIFT 7
#endif
	u32 cid;
	u32 reserved2[6];
};


/*
 * The first request to be passed in order to establish connection in option2
 */
struct l4_kwq_connect_req1 {
#if defined(__BIG_ENDIAN)
	u8 flags;
#define L4_KWQ_CONNECT_REQ1_RESERVED1 (0xF<<0)
#define L4_KWQ_CONNECT_REQ1_RESERVED1_SHIFT 0
#define L4_KWQ_CONNECT_REQ1_LAYER_CODE (0x7<<4)
#define L4_KWQ_CONNECT_REQ1_LAYER_CODE_SHIFT 4
#define L4_KWQ_CONNECT_REQ1_LINKED_WITH_NEXT (0x1<<7)
#define L4_KWQ_CONNECT_REQ1_LINKED_WITH_NEXT_SHIFT 7
	u8 op_code;
	u8 reserved0;
	u8 conn_flags;
#define L4_KWQ_CONNECT_REQ1_IS_PG_HOST_OPAQUE (0x1<<0)
#define L4_KWQ_CONNECT_REQ1_IS_PG_HOST_OPAQUE_SHIFT 0
#define L4_KWQ_CONNECT_REQ1_IP_V6 (0x1<<1)
#define L4_KWQ_CONNECT_REQ1_IP_V6_SHIFT 1
#define L4_KWQ_CONNECT_REQ1_PASSIVE_FLAG (0x1<<2)
#define L4_KWQ_CONNECT_REQ1_PASSIVE_FLAG_SHIFT 2
#define L4_KWQ_CONNECT_REQ1_RSRV (0x1F<<3)
#define L4_KWQ_CONNECT_REQ1_RSRV_SHIFT 3
#elif defined(__LITTLE_ENDIAN)
	u8 conn_flags;
#define L4_KWQ_CONNECT_REQ1_IS_PG_HOST_OPAQUE (0x1<<0)
#define L4_KWQ_CONNECT_REQ1_IS_PG_HOST_OPAQUE_SHIFT 0
#define L4_KWQ_CONNECT_REQ1_IP_V6 (0x1<<1)
#define L4_KWQ_CONNECT_REQ1_IP_V6_SHIFT 1
#define L4_KWQ_CONNECT_REQ1_PASSIVE_FLAG (0x1<<2)
#define L4_KWQ_CONNECT_REQ1_PASSIVE_FLAG_SHIFT 2
#define L4_KWQ_CONNECT_REQ1_RSRV (0x1F<<3)
#define L4_KWQ_CONNECT_REQ1_RSRV_SHIFT 3
	u8 reserved0;
	u8 op_code;
	u8 flags;
#define L4_KWQ_CONNECT_REQ1_RESERVED1 (0xF<<0)
#define L4_KWQ_CONNECT_REQ1_RESERVED1_SHIFT 0
#define L4_KWQ_CONNECT_REQ1_LAYER_CODE (0x7<<4)
#define L4_KWQ_CONNECT_REQ1_LAYER_CODE_SHIFT 4
#define L4_KWQ_CONNECT_REQ1_LINKED_WITH_NEXT (0x1<<7)
#define L4_KWQ_CONNECT_REQ1_LINKED_WITH_NEXT_SHIFT 7
#endif
	u32 cid;
	u32 pg_cid;
	u32 src_ip;
	u32 dst_ip;
#if defined(__BIG_ENDIAN)
	u16 dst_port;
	u16 src_port;
#elif defined(__LITTLE_ENDIAN)
	u16 src_port;
	u16 dst_port;
#endif
#if defined(__BIG_ENDIAN)
	u8 rsrv1[3];
	u8 tcp_flags;
#define L4_KWQ_CONNECT_REQ1_NO_DELAY_ACK (0x1<<0)
#define L4_KWQ_CONNECT_REQ1_NO_DELAY_ACK_SHIFT 0
#define L4_KWQ_CONNECT_REQ1_KEEP_ALIVE (0x1<<1)
#define L4_KWQ_CONNECT_REQ1_KEEP_ALIVE_SHIFT 1
#define L4_KWQ_CONNECT_REQ1_NAGLE_ENABLE (0x1<<2)
#define L4_KWQ_CONNECT_REQ1_NAGLE_ENABLE_SHIFT 2
#define L4_KWQ_CONNECT_REQ1_TIME_STAMP (0x1<<3)
#define L4_KWQ_CONNECT_REQ1_TIME_STAMP_SHIFT 3
#define L4_KWQ_CONNECT_REQ1_SACK (0x1<<4)
#define L4_KWQ_CONNECT_REQ1_SACK_SHIFT 4
#define L4_KWQ_CONNECT_REQ1_SEG_SCALING (0x1<<5)
#define L4_KWQ_CONNECT_REQ1_SEG_SCALING_SHIFT 5
#define L4_KWQ_CONNECT_REQ1_RESERVED2 (0x3<<6)
#define L4_KWQ_CONNECT_REQ1_RESERVED2_SHIFT 6
#elif defined(__LITTLE_ENDIAN)
	u8 tcp_flags;
#define L4_KWQ_CONNECT_REQ1_NO_DELAY_ACK (0x1<<0)
#define L4_KWQ_CONNECT_REQ1_NO_DELAY_ACK_SHIFT 0
#define L4_KWQ_CONNECT_REQ1_KEEP_ALIVE (0x1<<1)
#define L4_KWQ_CONNECT_REQ1_KEEP_ALIVE_SHIFT 1
#define L4_KWQ_CONNECT_REQ1_NAGLE_ENABLE (0x1<<2)
#define L4_KWQ_CONNECT_REQ1_NAGLE_ENABLE_SHIFT 2
#define L4_KWQ_CONNECT_REQ1_TIME_STAMP (0x1<<3)
#define L4_KWQ_CONNECT_REQ1_TIME_STAMP_SHIFT 3
#define L4_KWQ_CONNECT_REQ1_SACK (0x1<<4)
#define L4_KWQ_CONNECT_REQ1_SACK_SHIFT 4
#define L4_KWQ_CONNECT_REQ1_SEG_SCALING (0x1<<5)
#define L4_KWQ_CONNECT_REQ1_SEG_SCALING_SHIFT 5
#define L4_KWQ_CONNECT_REQ1_RESERVED2 (0x3<<6)
#define L4_KWQ_CONNECT_REQ1_RESERVED2_SHIFT 6
	u8 rsrv1[3];
#endif
	u32 rsrv2;
};


/*
 * The second ( optional )request to be passed in order to establish
 * connection in option2 - for IPv6 only
 */
struct l4_kwq_connect_req2 {
#if defined(__BIG_ENDIAN)
	u8 flags;
#define L4_KWQ_CONNECT_REQ2_RESERVED1 (0xF<<0)
#define L4_KWQ_CONNECT_REQ2_RESERVED1_SHIFT 0
#define L4_KWQ_CONNECT_REQ2_LAYER_CODE (0x7<<4)
#define L4_KWQ_CONNECT_REQ2_LAYER_CODE_SHIFT 4
#define L4_KWQ_CONNECT_REQ2_LINKED_WITH_NEXT (0x1<<7)
#define L4_KWQ_CONNECT_REQ2_LINKED_WITH_NEXT_SHIFT 7
	u8 op_code;
	u8 reserved0;
	u8 rsrv;
#elif defined(__LITTLE_ENDIAN)
	u8 rsrv;
	u8 reserved0;
	u8 op_code;
	u8 flags;
#define L4_KWQ_CONNECT_REQ2_RESERVED1 (0xF<<0)
#define L4_KWQ_CONNECT_REQ2_RESERVED1_SHIFT 0
#define L4_KWQ_CONNECT_REQ2_LAYER_CODE (0x7<<4)
#define L4_KWQ_CONNECT_REQ2_LAYER_CODE_SHIFT 4
#define L4_KWQ_CONNECT_REQ2_LINKED_WITH_NEXT (0x1<<7)
#define L4_KWQ_CONNECT_REQ2_LINKED_WITH_NEXT_SHIFT 7
#endif
	u32 reserved2;
	u32 src_ip_v6_2;
	u32 src_ip_v6_3;
	u32 src_ip_v6_4;
	u32 dst_ip_v6_2;
	u32 dst_ip_v6_3;
	u32 dst_ip_v6_4;
};


/*
 * The third ( and last )request to be passed in order to establish
 * connection in option2
 */
struct l4_kwq_connect_req3 {
#if defined(__BIG_ENDIAN)
	u8 flags;
#define L4_KWQ_CONNECT_REQ3_RESERVED1 (0xF<<0)
#define L4_KWQ_CONNECT_REQ3_RESERVED1_SHIFT 0
#define L4_KWQ_CONNECT_REQ3_LAYER_CODE (0x7<<4)
#define L4_KWQ_CONNECT_REQ3_LAYER_CODE_SHIFT 4
#define L4_KWQ_CONNECT_REQ3_LINKED_WITH_NEXT (0x1<<7)
#define L4_KWQ_CONNECT_REQ3_LINKED_WITH_NEXT_SHIFT 7
	u8 op_code;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	u8 op_code;
	u8 flags;
#define L4_KWQ_CONNECT_REQ3_RESERVED1 (0xF<<0)
#define L4_KWQ_CONNECT_REQ3_RESERVED1_SHIFT 0
#define L4_KWQ_CONNECT_REQ3_LAYER_CODE (0x7<<4)
#define L4_KWQ_CONNECT_REQ3_LAYER_CODE_SHIFT 4
#define L4_KWQ_CONNECT_REQ3_LINKED_WITH_NEXT (0x1<<7)
#define L4_KWQ_CONNECT_REQ3_LINKED_WITH_NEXT_SHIFT 7
#endif
	u32 ka_timeout;
	u32 ka_interval ;
#if defined(__BIG_ENDIAN)
	u8 snd_seq_scale;
	u8 ttl;
	u8 tos;
	u8 ka_max_probe_count;
#elif defined(__LITTLE_ENDIAN)
	u8 ka_max_probe_count;
	u8 tos;
	u8 ttl;
	u8 snd_seq_scale;
#endif
#if defined(__BIG_ENDIAN)
	u16 pmtu;
	u16 mss;
#elif defined(__LITTLE_ENDIAN)
	u16 mss;
	u16 pmtu;
#endif
	u32 rcv_buf;
	u32 snd_buf;
	u32 seed;
};


/*
 * a KWQE request to offload a PG connection
 */
struct l4_kwq_offload_pg {
#if defined(__BIG_ENDIAN)
	u8 flags;
#define L4_KWQ_OFFLOAD_PG_RESERVED1 (0xF<<0)
#define L4_KWQ_OFFLOAD_PG_RESERVED1_SHIFT 0
#define L4_KWQ_OFFLOAD_PG_LAYER_CODE (0x7<<4)
#define L4_KWQ_OFFLOAD_PG_LAYER_CODE_SHIFT 4
#define L4_KWQ_OFFLOAD_PG_LINKED_WITH_NEXT (0x1<<7)
#define L4_KWQ_OFFLOAD_PG_LINKED_WITH_NEXT_SHIFT 7
	u8 op_code;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	u8 op_code;
	u8 flags;
#define L4_KWQ_OFFLOAD_PG_RESERVED1 (0xF<<0)
#define L4_KWQ_OFFLOAD_PG_RESERVED1_SHIFT 0
#define L4_KWQ_OFFLOAD_PG_LAYER_CODE (0x7<<4)
#define L4_KWQ_OFFLOAD_PG_LAYER_CODE_SHIFT 4
#define L4_KWQ_OFFLOAD_PG_LINKED_WITH_NEXT (0x1<<7)
#define L4_KWQ_OFFLOAD_PG_LINKED_WITH_NEXT_SHIFT 7
#endif
#if defined(__BIG_ENDIAN)
	u8 l2hdr_nbytes;
	u8 pg_flags;
#define L4_KWQ_OFFLOAD_PG_SNAP_ENCAP (0x1<<0)
#define L4_KWQ_OFFLOAD_PG_SNAP_ENCAP_SHIFT 0
#define L4_KWQ_OFFLOAD_PG_VLAN_TAGGING (0x1<<1)
#define L4_KWQ_OFFLOAD_PG_VLAN_TAGGING_SHIFT 1
#define L4_KWQ_OFFLOAD_PG_RESERVED2 (0x3F<<2)
#define L4_KWQ_OFFLOAD_PG_RESERVED2_SHIFT 2
	u8 da0;
	u8 da1;
#elif defined(__LITTLE_ENDIAN)
	u8 da1;
	u8 da0;
	u8 pg_flags;
#define L4_KWQ_OFFLOAD_PG_SNAP_ENCAP (0x1<<0)
#define L4_KWQ_OFFLOAD_PG_SNAP_ENCAP_SHIFT 0
#define L4_KWQ_OFFLOAD_PG_VLAN_TAGGING (0x1<<1)
#define L4_KWQ_OFFLOAD_PG_VLAN_TAGGING_SHIFT 1
#define L4_KWQ_OFFLOAD_PG_RESERVED2 (0x3F<<2)
#define L4_KWQ_OFFLOAD_PG_RESERVED2_SHIFT 2
	u8 l2hdr_nbytes;
#endif
#if defined(__BIG_ENDIAN)
	u8 da2;
	u8 da3;
	u8 da4;
	u8 da5;
#elif defined(__LITTLE_ENDIAN)
	u8 da5;
	u8 da4;
	u8 da3;
	u8 da2;
#endif
#if defined(__BIG_ENDIAN)
	u8 sa0;
	u8 sa1;
	u8 sa2;
	u8 sa3;
#elif defined(__LITTLE_ENDIAN)
	u8 sa3;
	u8 sa2;
	u8 sa1;
	u8 sa0;
#endif
#if defined(__BIG_ENDIAN)
	u8 sa4;
	u8 sa5;
	u16 etype;
#elif defined(__LITTLE_ENDIAN)
	u16 etype;
	u8 sa5;
	u8 sa4;
#endif
#if defined(__BIG_ENDIAN)
	u16 vlan_tag;
	u16 ipid_start;
#elif defined(__LITTLE_ENDIAN)
	u16 ipid_start;
	u16 vlan_tag;
#endif
#if defined(__BIG_ENDIAN)
	u16 ipid_count;
	u16 reserved3;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved3;
	u16 ipid_count;
#endif
	u32 host_opaque;
};


/*
 * Abortively close the connection request
 */
struct l4_kwq_reset_req {
#if defined(__BIG_ENDIAN)
	u8 flags;
#define L4_KWQ_RESET_REQ_RESERVED1 (0xF<<0)
#define L4_KWQ_RESET_REQ_RESERVED1_SHIFT 0
#define L4_KWQ_RESET_REQ_LAYER_CODE (0x7<<4)
#define L4_KWQ_RESET_REQ_LAYER_CODE_SHIFT 4
#define L4_KWQ_RESET_REQ_LINKED_WITH_NEXT (0x1<<7)
#define L4_KWQ_RESET_REQ_LINKED_WITH_NEXT_SHIFT 7
	u8 op_code;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	u8 op_code;
	u8 flags;
#define L4_KWQ_RESET_REQ_RESERVED1 (0xF<<0)
#define L4_KWQ_RESET_REQ_RESERVED1_SHIFT 0
#define L4_KWQ_RESET_REQ_LAYER_CODE (0x7<<4)
#define L4_KWQ_RESET_REQ_LAYER_CODE_SHIFT 4
#define L4_KWQ_RESET_REQ_LINKED_WITH_NEXT (0x1<<7)
#define L4_KWQ_RESET_REQ_LINKED_WITH_NEXT_SHIFT 7
#endif
	u32 cid;
	u32 reserved2[6];
};


/*
 * a KWQE request to update a PG connection
 */
struct l4_kwq_update_pg {
#if defined(__BIG_ENDIAN)
	u8 flags;
#define L4_KWQ_UPDATE_PG_RESERVED1 (0xF<<0)
#define L4_KWQ_UPDATE_PG_RESERVED1_SHIFT 0
#define L4_KWQ_UPDATE_PG_LAYER_CODE (0x7<<4)
#define L4_KWQ_UPDATE_PG_LAYER_CODE_SHIFT 4
#define L4_KWQ_UPDATE_PG_LINKED_WITH_NEXT (0x1<<7)
#define L4_KWQ_UPDATE_PG_LINKED_WITH_NEXT_SHIFT 7
	u8 opcode;
	u16 oper16;
#elif defined(__LITTLE_ENDIAN)
	u16 oper16;
	u8 opcode;
	u8 flags;
#define L4_KWQ_UPDATE_PG_RESERVED1 (0xF<<0)
#define L4_KWQ_UPDATE_PG_RESERVED1_SHIFT 0
#define L4_KWQ_UPDATE_PG_LAYER_CODE (0x7<<4)
#define L4_KWQ_UPDATE_PG_LAYER_CODE_SHIFT 4
#define L4_KWQ_UPDATE_PG_LINKED_WITH_NEXT (0x1<<7)
#define L4_KWQ_UPDATE_PG_LINKED_WITH_NEXT_SHIFT 7
#endif
	u32 pg_cid;
	u32 pg_host_opaque;
#if defined(__BIG_ENDIAN)
	u8 pg_valids;
#define L4_KWQ_UPDATE_PG_VALIDS_IPID_COUNT (0x1<<0)
#define L4_KWQ_UPDATE_PG_VALIDS_IPID_COUNT_SHIFT 0
#define L4_KWQ_UPDATE_PG_VALIDS_DA (0x1<<1)
#define L4_KWQ_UPDATE_PG_VALIDS_DA_SHIFT 1
#define L4_KWQ_UPDATE_PG_RESERVERD2 (0x3F<<2)
#define L4_KWQ_UPDATE_PG_RESERVERD2_SHIFT 2
	u8 pg_unused_a;
	u16 pg_ipid_count;
#elif defined(__LITTLE_ENDIAN)
	u16 pg_ipid_count;
	u8 pg_unused_a;
	u8 pg_valids;
#define L4_KWQ_UPDATE_PG_VALIDS_IPID_COUNT (0x1<<0)
#define L4_KWQ_UPDATE_PG_VALIDS_IPID_COUNT_SHIFT 0
#define L4_KWQ_UPDATE_PG_VALIDS_DA (0x1<<1)
#define L4_KWQ_UPDATE_PG_VALIDS_DA_SHIFT 1
#define L4_KWQ_UPDATE_PG_RESERVERD2 (0x3F<<2)
#define L4_KWQ_UPDATE_PG_RESERVERD2_SHIFT 2
#endif
#if defined(__BIG_ENDIAN)
	u16 reserverd3;
	u8 da0;
	u8 da1;
#elif defined(__LITTLE_ENDIAN)
	u8 da1;
	u8 da0;
	u16 reserverd3;
#endif
#if defined(__BIG_ENDIAN)
	u8 da2;
	u8 da3;
	u8 da4;
	u8 da5;
#elif defined(__LITTLE_ENDIAN)
	u8 da5;
	u8 da4;
	u8 da3;
	u8 da2;
#endif
	u32 reserved4;
	u32 reserved5;
};


/*
 * a KWQE request to upload a PG or L4 context
 */
struct l4_kwq_upload {
#if defined(__BIG_ENDIAN)
	u8 flags;
#define L4_KWQ_UPLOAD_RESERVED1 (0xF<<0)
#define L4_KWQ_UPLOAD_RESERVED1_SHIFT 0
#define L4_KWQ_UPLOAD_LAYER_CODE (0x7<<4)
#define L4_KWQ_UPLOAD_LAYER_CODE_SHIFT 4
#define L4_KWQ_UPLOAD_LINKED_WITH_NEXT (0x1<<7)
#define L4_KWQ_UPLOAD_LINKED_WITH_NEXT_SHIFT 7
	u8 opcode;
	u16 oper16;
#elif defined(__LITTLE_ENDIAN)
	u16 oper16;
	u8 opcode;
	u8 flags;
#define L4_KWQ_UPLOAD_RESERVED1 (0xF<<0)
#define L4_KWQ_UPLOAD_RESERVED1_SHIFT 0
#define L4_KWQ_UPLOAD_LAYER_CODE (0x7<<4)
#define L4_KWQ_UPLOAD_LAYER_CODE_SHIFT 4
#define L4_KWQ_UPLOAD_LINKED_WITH_NEXT (0x1<<7)
#define L4_KWQ_UPLOAD_LINKED_WITH_NEXT_SHIFT 7
#endif
	u32 cid;
	u32 reserved2[6];
};

#endif /* CNIC_DEFS_H */
