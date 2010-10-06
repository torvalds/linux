
/* cnic.c: Broadcom CNIC core network driver.
 *
 * Copyright (c) 2006-2010 Broadcom Corporation
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
#define L2_KWQE_OPCODE_VALUE_VM_FREE_RX_QUEUE       (8)

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
#define L4_KCQE_COMPLETION_STATUS_SUCCESS           (0)
#define L4_KCQE_COMPLETION_STATUS_TIMEOUT           (0x93)

#define L4_KCQE_COMPLETION_STATUS_CTX_ALLOC_FAIL    (0x83)
#define L4_KCQE_COMPLETION_STATUS_OFFLOADED_PG      (0x89)

#define L4_KCQE_OPCODE_VALUE_OOO_EVENT_NOTIFICATION (0xa0)
#define L4_KCQE_OPCODE_VALUE_OOO_FLUSH              (0xa1)

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

/*
 * bnx2x structures
 */

/*
 * The iscsi aggregative context of Cstorm
 */
struct cstorm_iscsi_ag_context {
	u32 agg_vars1;
#define CSTORM_ISCSI_AG_CONTEXT_STATE (0xFF<<0)
#define CSTORM_ISCSI_AG_CONTEXT_STATE_SHIFT 0
#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0 (0x1<<8)
#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT 8
#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1 (0x1<<9)
#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT 9
#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2 (0x1<<10)
#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT 10
#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3 (0x1<<11)
#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT 11
#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED_ULP_RX_SE_CF_EN (0x1<<12)
#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED_ULP_RX_SE_CF_EN_SHIFT 12
#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED_ULP_RX_INV_CF_EN (0x1<<13)
#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED_ULP_RX_INV_CF_EN_SHIFT 13
#define __CSTORM_ISCSI_AG_CONTEXT_PENDING_COMPLETION3_CF (0x3<<14)
#define __CSTORM_ISCSI_AG_CONTEXT_PENDING_COMPLETION3_CF_SHIFT 14
#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED66 (0x3<<16)
#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED66_SHIFT 16
#define __CSTORM_ISCSI_AG_CONTEXT_FIN_RECEIVED_CF_EN (0x1<<18)
#define __CSTORM_ISCSI_AG_CONTEXT_FIN_RECEIVED_CF_EN_SHIFT 18
#define __CSTORM_ISCSI_AG_CONTEXT_PENDING_COMPLETION0_CF_EN (0x1<<19)
#define __CSTORM_ISCSI_AG_CONTEXT_PENDING_COMPLETION0_CF_EN_SHIFT 19
#define __CSTORM_ISCSI_AG_CONTEXT_PENDING_COMPLETION1_CF_EN (0x1<<20)
#define __CSTORM_ISCSI_AG_CONTEXT_PENDING_COMPLETION1_CF_EN_SHIFT 20
#define __CSTORM_ISCSI_AG_CONTEXT_PENDING_COMPLETION2_CF_EN (0x1<<21)
#define __CSTORM_ISCSI_AG_CONTEXT_PENDING_COMPLETION2_CF_EN_SHIFT 21
#define __CSTORM_ISCSI_AG_CONTEXT_PENDING_COMPLETION3_CF_EN (0x1<<22)
#define __CSTORM_ISCSI_AG_CONTEXT_PENDING_COMPLETION3_CF_EN_SHIFT 22
#define __CSTORM_ISCSI_AG_CONTEXT_REL_SEQ_RULE (0x7<<23)
#define __CSTORM_ISCSI_AG_CONTEXT_REL_SEQ_RULE_SHIFT 23
#define CSTORM_ISCSI_AG_CONTEXT_HQ_PROD_RULE (0x3<<26)
#define CSTORM_ISCSI_AG_CONTEXT_HQ_PROD_RULE_SHIFT 26
#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED52 (0x3<<28)
#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED52_SHIFT 28
#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED53 (0x3<<30)
#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED53_SHIFT 30
#if defined(__BIG_ENDIAN)
	u8 __aux1_th;
	u8 __aux1_val;
	u16 __agg_vars2;
#elif defined(__LITTLE_ENDIAN)
	u16 __agg_vars2;
	u8 __aux1_val;
	u8 __aux1_th;
#endif
	u32 rel_seq;
	u32 rel_seq_th;
#if defined(__BIG_ENDIAN)
	u16 hq_cons;
	u16 hq_prod;
#elif defined(__LITTLE_ENDIAN)
	u16 hq_prod;
	u16 hq_cons;
#endif
#if defined(__BIG_ENDIAN)
	u8 __reserved62;
	u8 __reserved61;
	u8 __reserved60;
	u8 __reserved59;
#elif defined(__LITTLE_ENDIAN)
	u8 __reserved59;
	u8 __reserved60;
	u8 __reserved61;
	u8 __reserved62;
#endif
#if defined(__BIG_ENDIAN)
	u16 __reserved64;
	u16 __cq_u_prod0;
#elif defined(__LITTLE_ENDIAN)
	u16 __cq_u_prod0;
	u16 __reserved64;
#endif
	u32 __cq_u_prod1;
#if defined(__BIG_ENDIAN)
	u16 __agg_vars3;
	u16 __cq_u_prod2;
#elif defined(__LITTLE_ENDIAN)
	u16 __cq_u_prod2;
	u16 __agg_vars3;
#endif
#if defined(__BIG_ENDIAN)
	u16 __aux2_th;
	u16 __cq_u_prod3;
#elif defined(__LITTLE_ENDIAN)
	u16 __cq_u_prod3;
	u16 __aux2_th;
#endif
};

/*
 * iSCSI context region, used only in iSCSI
 */
struct ustorm_iscsi_rq_db {
	struct regpair pbl_base;
	struct regpair curr_pbe;
};

/*
 * iSCSI context region, used only in iSCSI
 */
struct ustorm_iscsi_r2tq_db {
	struct regpair pbl_base;
	struct regpair curr_pbe;
};

/*
 * iSCSI context region, used only in iSCSI
 */
struct ustorm_iscsi_cq_db {
#if defined(__BIG_ENDIAN)
	u16 cq_sn;
	u16 prod;
#elif defined(__LITTLE_ENDIAN)
	u16 prod;
	u16 cq_sn;
#endif
	struct regpair curr_pbe;
};

/*
 * iSCSI context region, used only in iSCSI
 */
struct rings_db {
	struct ustorm_iscsi_rq_db rq;
	struct ustorm_iscsi_r2tq_db r2tq;
	struct ustorm_iscsi_cq_db cq[8];
#if defined(__BIG_ENDIAN)
	u16 rq_prod;
	u16 r2tq_prod;
#elif defined(__LITTLE_ENDIAN)
	u16 r2tq_prod;
	u16 rq_prod;
#endif
	struct regpair cq_pbl_base;
};

/*
 * iSCSI context region, used only in iSCSI
 */
struct ustorm_iscsi_placement_db {
	u32 sgl_base_lo;
	u32 sgl_base_hi;
	u32 local_sge_0_address_hi;
	u32 local_sge_0_address_lo;
#if defined(__BIG_ENDIAN)
	u16 curr_sge_offset;
	u16 local_sge_0_size;
#elif defined(__LITTLE_ENDIAN)
	u16 local_sge_0_size;
	u16 curr_sge_offset;
#endif
	u32 local_sge_1_address_hi;
	u32 local_sge_1_address_lo;
#if defined(__BIG_ENDIAN)
	u16 reserved6;
	u16 local_sge_1_size;
#elif defined(__LITTLE_ENDIAN)
	u16 local_sge_1_size;
	u16 reserved6;
#endif
#if defined(__BIG_ENDIAN)
	u8 sgl_size;
	u8 local_sge_index_2b;
	u16 reserved7;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved7;
	u8 local_sge_index_2b;
	u8 sgl_size;
#endif
	u32 rem_pdu;
	u32 place_db_bitfield_1;
#define USTORM_ISCSI_PLACEMENT_DB_REM_PDU_PAYLOAD (0xFFFFFF<<0)
#define USTORM_ISCSI_PLACEMENT_DB_REM_PDU_PAYLOAD_SHIFT 0
#define USTORM_ISCSI_PLACEMENT_DB_CQ_ID (0xFF<<24)
#define USTORM_ISCSI_PLACEMENT_DB_CQ_ID_SHIFT 24
	u32 place_db_bitfield_2;
#define USTORM_ISCSI_PLACEMENT_DB_BYTES_2_TRUNCATE (0xFFFFFF<<0)
#define USTORM_ISCSI_PLACEMENT_DB_BYTES_2_TRUNCATE_SHIFT 0
#define USTORM_ISCSI_PLACEMENT_DB_HOST_SGE_INDEX (0xFF<<24)
#define USTORM_ISCSI_PLACEMENT_DB_HOST_SGE_INDEX_SHIFT 24
	u32 nal;
#define USTORM_ISCSI_PLACEMENT_DB_REM_SGE_SIZE (0xFFFFFF<<0)
#define USTORM_ISCSI_PLACEMENT_DB_REM_SGE_SIZE_SHIFT 0
#define USTORM_ISCSI_PLACEMENT_DB_EXP_PADDING_2B (0x3<<24)
#define USTORM_ISCSI_PLACEMENT_DB_EXP_PADDING_2B_SHIFT 24
#define USTORM_ISCSI_PLACEMENT_DB_EXP_DIGEST_3B (0x7<<26)
#define USTORM_ISCSI_PLACEMENT_DB_EXP_DIGEST_3B_SHIFT 26
#define USTORM_ISCSI_PLACEMENT_DB_NAL_LEN_3B (0x7<<29)
#define USTORM_ISCSI_PLACEMENT_DB_NAL_LEN_3B_SHIFT 29
};

/*
 * Ustorm iSCSI Storm Context
 */
struct ustorm_iscsi_st_context {
	u32 exp_stat_sn;
	u32 exp_data_sn;
	struct rings_db ring;
	struct regpair task_pbl_base;
	struct regpair tce_phy_addr;
	struct ustorm_iscsi_placement_db place_db;
	u32 reserved8;
	u32 rem_rcv_len;
#if defined(__BIG_ENDIAN)
	u16 hdr_itt;
	u16 iscsi_conn_id;
#elif defined(__LITTLE_ENDIAN)
	u16 iscsi_conn_id;
	u16 hdr_itt;
#endif
	u32 nal_bytes;
#if defined(__BIG_ENDIAN)
	u8 hdr_second_byte_union;
	u8 bitfield_0;
#define USTORM_ISCSI_ST_CONTEXT_BMIDDLEOFPDU (0x1<<0)
#define USTORM_ISCSI_ST_CONTEXT_BMIDDLEOFPDU_SHIFT 0
#define USTORM_ISCSI_ST_CONTEXT_BFENCECQE (0x1<<1)
#define USTORM_ISCSI_ST_CONTEXT_BFENCECQE_SHIFT 1
#define USTORM_ISCSI_ST_CONTEXT_BRESETCRC (0x1<<2)
#define USTORM_ISCSI_ST_CONTEXT_BRESETCRC_SHIFT 2
#define USTORM_ISCSI_ST_CONTEXT_RESERVED1 (0x1F<<3)
#define USTORM_ISCSI_ST_CONTEXT_RESERVED1_SHIFT 3
	u8 task_pdu_cache_index;
	u8 task_pbe_cache_index;
#elif defined(__LITTLE_ENDIAN)
	u8 task_pbe_cache_index;
	u8 task_pdu_cache_index;
	u8 bitfield_0;
#define USTORM_ISCSI_ST_CONTEXT_BMIDDLEOFPDU (0x1<<0)
#define USTORM_ISCSI_ST_CONTEXT_BMIDDLEOFPDU_SHIFT 0
#define USTORM_ISCSI_ST_CONTEXT_BFENCECQE (0x1<<1)
#define USTORM_ISCSI_ST_CONTEXT_BFENCECQE_SHIFT 1
#define USTORM_ISCSI_ST_CONTEXT_BRESETCRC (0x1<<2)
#define USTORM_ISCSI_ST_CONTEXT_BRESETCRC_SHIFT 2
#define USTORM_ISCSI_ST_CONTEXT_RESERVED1 (0x1F<<3)
#define USTORM_ISCSI_ST_CONTEXT_RESERVED1_SHIFT 3
	u8 hdr_second_byte_union;
#endif
#if defined(__BIG_ENDIAN)
	u16 reserved3;
	u8 reserved2;
	u8 acDecrement;
#elif defined(__LITTLE_ENDIAN)
	u8 acDecrement;
	u8 reserved2;
	u16 reserved3;
#endif
	u32 task_stat;
#if defined(__BIG_ENDIAN)
	u8 hdr_opcode;
	u8 num_cqs;
	u16 reserved5;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved5;
	u8 num_cqs;
	u8 hdr_opcode;
#endif
	u32 negotiated_rx;
#define USTORM_ISCSI_ST_CONTEXT_MAX_RECV_PDU_LENGTH (0xFFFFFF<<0)
#define USTORM_ISCSI_ST_CONTEXT_MAX_RECV_PDU_LENGTH_SHIFT 0
#define USTORM_ISCSI_ST_CONTEXT_MAX_OUTSTANDING_R2TS (0xFF<<24)
#define USTORM_ISCSI_ST_CONTEXT_MAX_OUTSTANDING_R2TS_SHIFT 24
	u32 negotiated_rx_and_flags;
#define USTORM_ISCSI_ST_CONTEXT_MAX_BURST_LENGTH (0xFFFFFF<<0)
#define USTORM_ISCSI_ST_CONTEXT_MAX_BURST_LENGTH_SHIFT 0
#define USTORM_ISCSI_ST_CONTEXT_B_CQE_POSTED_OR_HEADER_CACHED (0x1<<24)
#define USTORM_ISCSI_ST_CONTEXT_B_CQE_POSTED_OR_HEADER_CACHED_SHIFT 24
#define USTORM_ISCSI_ST_CONTEXT_B_HDR_DIGEST_EN (0x1<<25)
#define USTORM_ISCSI_ST_CONTEXT_B_HDR_DIGEST_EN_SHIFT 25
#define USTORM_ISCSI_ST_CONTEXT_B_DATA_DIGEST_EN (0x1<<26)
#define USTORM_ISCSI_ST_CONTEXT_B_DATA_DIGEST_EN_SHIFT 26
#define USTORM_ISCSI_ST_CONTEXT_B_PROTOCOL_ERROR (0x1<<27)
#define USTORM_ISCSI_ST_CONTEXT_B_PROTOCOL_ERROR_SHIFT 27
#define USTORM_ISCSI_ST_CONTEXT_B_TASK_VALID (0x1<<28)
#define USTORM_ISCSI_ST_CONTEXT_B_TASK_VALID_SHIFT 28
#define USTORM_ISCSI_ST_CONTEXT_TASK_TYPE (0x3<<29)
#define USTORM_ISCSI_ST_CONTEXT_TASK_TYPE_SHIFT 29
#define USTORM_ISCSI_ST_CONTEXT_B_ALL_DATA_ACKED (0x1<<31)
#define USTORM_ISCSI_ST_CONTEXT_B_ALL_DATA_ACKED_SHIFT 31
};

/*
 * TCP context region, shared in TOE, RDMA and ISCSI
 */
struct tstorm_tcp_st_context_section {
	u32 flags1;
#define TSTORM_TCP_ST_CONTEXT_SECTION_RTT_SRTT (0xFFFFFF<<0)
#define TSTORM_TCP_ST_CONTEXT_SECTION_RTT_SRTT_SHIFT 0
#define TSTORM_TCP_ST_CONTEXT_SECTION_PAWS_INVALID (0x1<<24)
#define TSTORM_TCP_ST_CONTEXT_SECTION_PAWS_INVALID_SHIFT 24
#define TSTORM_TCP_ST_CONTEXT_SECTION_TIMESTAMP_EXISTS (0x1<<25)
#define TSTORM_TCP_ST_CONTEXT_SECTION_TIMESTAMP_EXISTS_SHIFT 25
#define TSTORM_TCP_ST_CONTEXT_SECTION_RESERVED0 (0x1<<26)
#define TSTORM_TCP_ST_CONTEXT_SECTION_RESERVED0_SHIFT 26
#define TSTORM_TCP_ST_CONTEXT_SECTION_STOP_RX_PAYLOAD (0x1<<27)
#define TSTORM_TCP_ST_CONTEXT_SECTION_STOP_RX_PAYLOAD_SHIFT 27
#define TSTORM_TCP_ST_CONTEXT_SECTION_KA_ENABLED (0x1<<28)
#define TSTORM_TCP_ST_CONTEXT_SECTION_KA_ENABLED_SHIFT 28
#define TSTORM_TCP_ST_CONTEXT_SECTION_FIRST_RTO_ESTIMATE (0x1<<29)
#define TSTORM_TCP_ST_CONTEXT_SECTION_FIRST_RTO_ESTIMATE_SHIFT 29
#define TSTORM_TCP_ST_CONTEXT_SECTION_MAX_SEG_RETRANSMIT_EN (0x1<<30)
#define TSTORM_TCP_ST_CONTEXT_SECTION_MAX_SEG_RETRANSMIT_EN_SHIFT 30
#define TSTORM_TCP_ST_CONTEXT_SECTION_LAST_ISLE_HAS_FIN (0x1<<31)
#define TSTORM_TCP_ST_CONTEXT_SECTION_LAST_ISLE_HAS_FIN_SHIFT 31
	u32 flags2;
#define TSTORM_TCP_ST_CONTEXT_SECTION_RTT_VARIATION (0xFFFFFF<<0)
#define TSTORM_TCP_ST_CONTEXT_SECTION_RTT_VARIATION_SHIFT 0
#define TSTORM_TCP_ST_CONTEXT_SECTION_DA_EN (0x1<<24)
#define TSTORM_TCP_ST_CONTEXT_SECTION_DA_EN_SHIFT 24
#define TSTORM_TCP_ST_CONTEXT_SECTION_DA_COUNTER_EN (0x1<<25)
#define TSTORM_TCP_ST_CONTEXT_SECTION_DA_COUNTER_EN_SHIFT 25
#define __TSTORM_TCP_ST_CONTEXT_SECTION_KA_PROBE_SENT (0x1<<26)
#define __TSTORM_TCP_ST_CONTEXT_SECTION_KA_PROBE_SENT_SHIFT 26
#define __TSTORM_TCP_ST_CONTEXT_SECTION_PERSIST_PROBE_SENT (0x1<<27)
#define __TSTORM_TCP_ST_CONTEXT_SECTION_PERSIST_PROBE_SENT_SHIFT 27
#define TSTORM_TCP_ST_CONTEXT_SECTION_UPDATE_L2_STATSTICS (0x1<<28)
#define TSTORM_TCP_ST_CONTEXT_SECTION_UPDATE_L2_STATSTICS_SHIFT 28
#define TSTORM_TCP_ST_CONTEXT_SECTION_UPDATE_L4_STATSTICS (0x1<<29)
#define TSTORM_TCP_ST_CONTEXT_SECTION_UPDATE_L4_STATSTICS_SHIFT 29
#define __TSTORM_TCP_ST_CONTEXT_SECTION_IN_WINDOW_RST_ATTACK (0x1<<30)
#define __TSTORM_TCP_ST_CONTEXT_SECTION_IN_WINDOW_RST_ATTACK_SHIFT 30
#define __TSTORM_TCP_ST_CONTEXT_SECTION_IN_WINDOW_SYN_ATTACK (0x1<<31)
#define __TSTORM_TCP_ST_CONTEXT_SECTION_IN_WINDOW_SYN_ATTACK_SHIFT 31
#if defined(__BIG_ENDIAN)
	u16 mss;
	u8 tcp_sm_state;
	u8 rto_exp;
#elif defined(__LITTLE_ENDIAN)
	u8 rto_exp;
	u8 tcp_sm_state;
	u16 mss;
#endif
	u32 rcv_nxt;
	u32 timestamp_recent;
	u32 timestamp_recent_time;
	u32 cwnd;
	u32 ss_thresh;
	u32 cwnd_accum;
	u32 prev_seg_seq;
	u32 expected_rel_seq;
	u32 recover;
#if defined(__BIG_ENDIAN)
	u8 retransmit_count;
	u8 ka_max_probe_count;
	u8 persist_probe_count;
	u8 ka_probe_count;
#elif defined(__LITTLE_ENDIAN)
	u8 ka_probe_count;
	u8 persist_probe_count;
	u8 ka_max_probe_count;
	u8 retransmit_count;
#endif
#if defined(__BIG_ENDIAN)
	u8 statistics_counter_id;
	u8 ooo_support_mode;
	u8 snd_wnd_scale;
	u8 dup_ack_count;
#elif defined(__LITTLE_ENDIAN)
	u8 dup_ack_count;
	u8 snd_wnd_scale;
	u8 ooo_support_mode;
	u8 statistics_counter_id;
#endif
	u32 retransmit_start_time;
	u32 ka_timeout;
	u32 ka_interval;
	u32 isle_start_seq;
	u32 isle_end_seq;
#if defined(__BIG_ENDIAN)
	u16 second_isle_address;
	u16 recent_seg_wnd;
#elif defined(__LITTLE_ENDIAN)
	u16 recent_seg_wnd;
	u16 second_isle_address;
#endif
#if defined(__BIG_ENDIAN)
	u8 max_isles_ever_happened;
	u8 isles_number;
	u16 last_isle_address;
#elif defined(__LITTLE_ENDIAN)
	u16 last_isle_address;
	u8 isles_number;
	u8 max_isles_ever_happened;
#endif
	u32 max_rt_time;
#if defined(__BIG_ENDIAN)
	u16 lsb_mac_address;
	u16 vlan_id;
#elif defined(__LITTLE_ENDIAN)
	u16 vlan_id;
	u16 lsb_mac_address;
#endif
	u32 msb_mac_address;
	u32 rightmost_received_seq;
};

/*
 * Termination variables
 */
struct iscsi_term_vars {
	u8 BitMap;
#define ISCSI_TERM_VARS_TCP_STATE (0xF<<0)
#define ISCSI_TERM_VARS_TCP_STATE_SHIFT 0
#define ISCSI_TERM_VARS_FIN_RECEIVED_SBIT (0x1<<4)
#define ISCSI_TERM_VARS_FIN_RECEIVED_SBIT_SHIFT 4
#define ISCSI_TERM_VARS_ACK_ON_FIN_RECEIVED_SBIT (0x1<<5)
#define ISCSI_TERM_VARS_ACK_ON_FIN_RECEIVED_SBIT_SHIFT 5
#define ISCSI_TERM_VARS_TERM_ON_CHIP (0x1<<6)
#define ISCSI_TERM_VARS_TERM_ON_CHIP_SHIFT 6
#define ISCSI_TERM_VARS_RSRV (0x1<<7)
#define ISCSI_TERM_VARS_RSRV_SHIFT 7
};

/*
 * iSCSI context region, used only in iSCSI
 */
struct tstorm_iscsi_st_context_section {
#if defined(__BIG_ENDIAN)
	u16 rem_tcp_data_len;
	u16 brb_offset;
#elif defined(__LITTLE_ENDIAN)
	u16 brb_offset;
	u16 rem_tcp_data_len;
#endif
	u32 b2nh;
#if defined(__BIG_ENDIAN)
	u16 rq_cons;
	u8 flags;
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_HDR_DIGEST_EN (0x1<<0)
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_HDR_DIGEST_EN_SHIFT 0
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DATA_DIGEST_EN (0x1<<1)
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DATA_DIGEST_EN_SHIFT 1
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_PARTIAL_HEADER (0x1<<2)
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_PARTIAL_HEADER_SHIFT 2
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_FULL_FEATURE (0x1<<3)
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_FULL_FEATURE_SHIFT 3
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DROP_ALL_PDUS (0x1<<4)
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DROP_ALL_PDUS_SHIFT 4
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_FLAGS_RSRV (0x7<<5)
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_FLAGS_RSRV_SHIFT 5
	u8 hdr_bytes_2_fetch;
#elif defined(__LITTLE_ENDIAN)
	u8 hdr_bytes_2_fetch;
	u8 flags;
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_HDR_DIGEST_EN (0x1<<0)
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_HDR_DIGEST_EN_SHIFT 0
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DATA_DIGEST_EN (0x1<<1)
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DATA_DIGEST_EN_SHIFT 1
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_PARTIAL_HEADER (0x1<<2)
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_PARTIAL_HEADER_SHIFT 2
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_FULL_FEATURE (0x1<<3)
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_FULL_FEATURE_SHIFT 3
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DROP_ALL_PDUS (0x1<<4)
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DROP_ALL_PDUS_SHIFT 4
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_FLAGS_RSRV (0x7<<5)
#define TSTORM_ISCSI_ST_CONTEXT_SECTION_FLAGS_RSRV_SHIFT 5
	u16 rq_cons;
#endif
	struct regpair rq_db_phy_addr;
#if defined(__BIG_ENDIAN)
	struct iscsi_term_vars term_vars;
	u8 scratchpad_idx;
	u16 iscsi_conn_id;
#elif defined(__LITTLE_ENDIAN)
	u16 iscsi_conn_id;
	u8 scratchpad_idx;
	struct iscsi_term_vars term_vars;
#endif
	u32 process_nxt;
};

/*
 * The iSCSI non-aggregative context of Tstorm
 */
struct tstorm_iscsi_st_context {
	struct tstorm_tcp_st_context_section tcp;
	struct tstorm_iscsi_st_context_section iscsi;
};

/*
 * The tcp aggregative context section of Xstorm
 */
struct xstorm_tcp_tcp_ag_context_section {
#if defined(__BIG_ENDIAN)
	u8 __tcp_agg_vars1;
	u8 __da_cnt;
	u16 mss;
#elif defined(__LITTLE_ENDIAN)
	u16 mss;
	u8 __da_cnt;
	u8 __tcp_agg_vars1;
#endif
	u32 snd_nxt;
	u32 tx_wnd;
	u32 snd_una;
	u32 local_adv_wnd;
#if defined(__BIG_ENDIAN)
	u8 __agg_val8_th;
	u8 __agg_val8;
	u16 tcp_agg_vars2;
#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG (0x1<<0)
#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_SHIFT 0
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED (0x1<<1)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED_SHIFT 1
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE (0x1<<2)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE_SHIFT 2
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX3_FLAG (0x1<<3)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX3_FLAG_SHIFT 3
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX4_FLAG (0x1<<4)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX4_FLAG_SHIFT 4
#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_ENABLE (0x1<<5)
#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_ENABLE_SHIFT 5
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN (0x1<<6)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN_SHIFT 6
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX3_CF_EN (0x1<<7)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX3_CF_EN_SHIFT 7
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN (0x1<<8)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN_SHIFT 8
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG (0x1<<9)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT 9
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_RTO_CF (0x3<<10)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_RTO_CF_SHIFT 10
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF (0x3<<12)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF_SHIFT 12
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX8_CF (0x3<<14)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX8_CF_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 tcp_agg_vars2;
#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG (0x1<<0)
#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_SHIFT 0
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED (0x1<<1)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED_SHIFT 1
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE (0x1<<2)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE_SHIFT 2
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX3_FLAG (0x1<<3)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX3_FLAG_SHIFT 3
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX4_FLAG (0x1<<4)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX4_FLAG_SHIFT 4
#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_ENABLE (0x1<<5)
#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_ENABLE_SHIFT 5
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN (0x1<<6)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN_SHIFT 6
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX3_CF_EN (0x1<<7)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX3_CF_EN_SHIFT 7
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN (0x1<<8)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN_SHIFT 8
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG (0x1<<9)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT 9
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_RTO_CF (0x3<<10)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_RTO_CF_SHIFT 10
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF (0x3<<12)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF_SHIFT 12
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX8_CF (0x3<<14)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX8_CF_SHIFT 14
	u8 __agg_val8;
	u8 __agg_val8_th;
#endif
	u32 ack_to_far_end;
	u32 rto_timer;
	u32 ka_timer;
	u32 ts_to_echo;
#if defined(__BIG_ENDIAN)
	u16 __agg_val7_th;
	u16 __agg_val7;
#elif defined(__LITTLE_ENDIAN)
	u16 __agg_val7;
	u16 __agg_val7_th;
#endif
#if defined(__BIG_ENDIAN)
	u8 __tcp_agg_vars5;
	u8 __tcp_agg_vars4;
	u8 __tcp_agg_vars3;
	u8 __force_pure_ack_cnt;
#elif defined(__LITTLE_ENDIAN)
	u8 __force_pure_ack_cnt;
	u8 __tcp_agg_vars3;
	u8 __tcp_agg_vars4;
	u8 __tcp_agg_vars5;
#endif
	u32 tcp_agg_vars6;
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_CF_EN (0x1<<0)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_CF_EN_SHIFT 0
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX8_CF_EN (0x1<<1)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX8_CF_EN_SHIFT 1
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX9_CF_EN (0x1<<2)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX9_CF_EN_SHIFT 2
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN (0x1<<3)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN_SHIFT 3
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX6_FLAG (0x1<<4)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX6_FLAG_SHIFT 4
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX7_FLAG (0x1<<5)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX7_FLAG_SHIFT 5
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX5_CF (0x3<<6)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX5_CF_SHIFT 6
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX9_CF (0x3<<8)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX9_CF_SHIFT 8
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF (0x3<<10)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF_SHIFT 10
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX11_CF (0x3<<12)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX11_CF_SHIFT 12
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX12_CF (0x3<<14)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX12_CF_SHIFT 14
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX13_CF (0x3<<16)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX13_CF_SHIFT 16
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX14_CF (0x3<<18)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX14_CF_SHIFT 18
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX15_CF (0x3<<20)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX15_CF_SHIFT 20
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX16_CF (0x3<<22)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX16_CF_SHIFT 22
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX17_CF (0x3<<24)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX17_CF_SHIFT 24
#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ECE_FLAG (0x1<<26)
#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ECE_FLAG_SHIFT 26
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_RESERVED71 (0x1<<27)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_RESERVED71_SHIFT 27
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_FORCE_PURE_ACK_CNT_DIRTY (0x1<<28)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_FORCE_PURE_ACK_CNT_DIRTY_SHIFT 28
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TCP_AUTO_STOP_FLAG (0x1<<29)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TCP_AUTO_STOP_FLAG_SHIFT 29
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DO_TS_UPDATE_FLAG (0x1<<30)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DO_TS_UPDATE_FLAG_SHIFT 30
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_CANCEL_RETRANSMIT_FLAG (0x1<<31)
#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_CANCEL_RETRANSMIT_FLAG_SHIFT 31
#if defined(__BIG_ENDIAN)
	u16 __agg_misc6;
	u16 __tcp_agg_vars7;
#elif defined(__LITTLE_ENDIAN)
	u16 __tcp_agg_vars7;
	u16 __agg_misc6;
#endif
	u32 __agg_val10;
	u32 __agg_val10_th;
#if defined(__BIG_ENDIAN)
	u16 __reserved3;
	u8 __reserved2;
	u8 __da_only_cnt;
#elif defined(__LITTLE_ENDIAN)
	u8 __da_only_cnt;
	u8 __reserved2;
	u16 __reserved3;
#endif
};

/*
 * The iscsi aggregative context of Xstorm
 */
struct xstorm_iscsi_ag_context {
#if defined(__BIG_ENDIAN)
	u16 agg_val1;
	u8 agg_vars1;
#define __XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0 (0x1<<0)
#define __XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT 0
#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1 (0x1<<1)
#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT 1
#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2 (0x1<<2)
#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT 2
#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3 (0x1<<3)
#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT 3
#define __XSTORM_ISCSI_AG_CONTEXT_MORE_TO_SEND_EN (0x1<<4)
#define __XSTORM_ISCSI_AG_CONTEXT_MORE_TO_SEND_EN_SHIFT 4
#define XSTORM_ISCSI_AG_CONTEXT_NAGLE_EN (0x1<<5)
#define XSTORM_ISCSI_AG_CONTEXT_NAGLE_EN_SHIFT 5
#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG (0x1<<6)
#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_SHIFT 6
#define __XSTORM_ISCSI_AG_CONTEXT_UNA_GT_NXT_EN (0x1<<7)
#define __XSTORM_ISCSI_AG_CONTEXT_UNA_GT_NXT_EN_SHIFT 7
	u8 state;
#elif defined(__LITTLE_ENDIAN)
	u8 state;
	u8 agg_vars1;
#define __XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0 (0x1<<0)
#define __XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT 0
#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1 (0x1<<1)
#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT 1
#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2 (0x1<<2)
#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT 2
#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3 (0x1<<3)
#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT 3
#define __XSTORM_ISCSI_AG_CONTEXT_MORE_TO_SEND_EN (0x1<<4)
#define __XSTORM_ISCSI_AG_CONTEXT_MORE_TO_SEND_EN_SHIFT 4
#define XSTORM_ISCSI_AG_CONTEXT_NAGLE_EN (0x1<<5)
#define XSTORM_ISCSI_AG_CONTEXT_NAGLE_EN_SHIFT 5
#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG (0x1<<6)
#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_SHIFT 6
#define __XSTORM_ISCSI_AG_CONTEXT_UNA_GT_NXT_EN (0x1<<7)
#define __XSTORM_ISCSI_AG_CONTEXT_UNA_GT_NXT_EN_SHIFT 7
	u16 agg_val1;
#endif
#if defined(__BIG_ENDIAN)
	u8 cdu_reserved;
	u8 __agg_vars4;
	u8 agg_vars3;
#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM2 (0x3F<<0)
#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM2_SHIFT 0
#define __XSTORM_ISCSI_AG_CONTEXT_RX_TS_EN_CF (0x3<<6)
#define __XSTORM_ISCSI_AG_CONTEXT_RX_TS_EN_CF_SHIFT 6
	u8 agg_vars2;
#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF (0x3<<0)
#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_SHIFT 0
#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_EN (0x1<<2)
#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_EN_SHIFT 2
#define __XSTORM_ISCSI_AG_CONTEXT_AUX8_FLAG (0x1<<3)
#define __XSTORM_ISCSI_AG_CONTEXT_AUX8_FLAG_SHIFT 3
#define __XSTORM_ISCSI_AG_CONTEXT_AUX9_FLAG (0x1<<4)
#define __XSTORM_ISCSI_AG_CONTEXT_AUX9_FLAG_SHIFT 4
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE1 (0x3<<5)
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE1_SHIFT 5
#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_EN (0x1<<7)
#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_EN_SHIFT 7
#elif defined(__LITTLE_ENDIAN)
	u8 agg_vars2;
#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF (0x3<<0)
#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_SHIFT 0
#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_EN (0x1<<2)
#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_EN_SHIFT 2
#define __XSTORM_ISCSI_AG_CONTEXT_AUX8_FLAG (0x1<<3)
#define __XSTORM_ISCSI_AG_CONTEXT_AUX8_FLAG_SHIFT 3
#define __XSTORM_ISCSI_AG_CONTEXT_AUX9_FLAG (0x1<<4)
#define __XSTORM_ISCSI_AG_CONTEXT_AUX9_FLAG_SHIFT 4
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE1 (0x3<<5)
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE1_SHIFT 5
#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_EN (0x1<<7)
#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_EN_SHIFT 7
	u8 agg_vars3;
#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM2 (0x3F<<0)
#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM2_SHIFT 0
#define __XSTORM_ISCSI_AG_CONTEXT_RX_TS_EN_CF (0x3<<6)
#define __XSTORM_ISCSI_AG_CONTEXT_RX_TS_EN_CF_SHIFT 6
	u8 __agg_vars4;
	u8 cdu_reserved;
#endif
	u32 more_to_send;
#if defined(__BIG_ENDIAN)
	u16 agg_vars5;
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE5 (0x3<<0)
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE5_SHIFT 0
#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM0 (0x3F<<2)
#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM0_SHIFT 2
#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM1 (0x3F<<8)
#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM1_SHIFT 8
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE2 (0x3<<14)
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE2_SHIFT 14
	u16 sq_cons;
#elif defined(__LITTLE_ENDIAN)
	u16 sq_cons;
	u16 agg_vars5;
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE5 (0x3<<0)
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE5_SHIFT 0
#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM0 (0x3F<<2)
#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM0_SHIFT 2
#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM1 (0x3F<<8)
#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM1_SHIFT 8
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE2 (0x3<<14)
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE2_SHIFT 14
#endif
	struct xstorm_tcp_tcp_ag_context_section tcp;
#if defined(__BIG_ENDIAN)
	u16 agg_vars7;
#define __XSTORM_ISCSI_AG_CONTEXT_AGG_VAL11_DECISION_RULE (0x7<<0)
#define __XSTORM_ISCSI_AG_CONTEXT_AGG_VAL11_DECISION_RULE_SHIFT 0
#define __XSTORM_ISCSI_AG_CONTEXT_AUX13_FLAG (0x1<<3)
#define __XSTORM_ISCSI_AG_CONTEXT_AUX13_FLAG_SHIFT 3
#define __XSTORM_ISCSI_AG_CONTEXT_STORMS_SYNC_CF (0x3<<4)
#define __XSTORM_ISCSI_AG_CONTEXT_STORMS_SYNC_CF_SHIFT 4
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE3 (0x3<<6)
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE3_SHIFT 6
#define XSTORM_ISCSI_AG_CONTEXT_AUX1_CF (0x3<<8)
#define XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_SHIFT 8
#define __XSTORM_ISCSI_AG_CONTEXT_COMPLETION_SEQ_DECISION_MASK (0x1<<10)
#define __XSTORM_ISCSI_AG_CONTEXT_COMPLETION_SEQ_DECISION_MASK_SHIFT 10
#define __XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_EN (0x1<<11)
#define __XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_EN_SHIFT 11
#define __XSTORM_ISCSI_AG_CONTEXT_AUX10_FLAG (0x1<<12)
#define __XSTORM_ISCSI_AG_CONTEXT_AUX10_FLAG_SHIFT 12
#define __XSTORM_ISCSI_AG_CONTEXT_AUX11_FLAG (0x1<<13)
#define __XSTORM_ISCSI_AG_CONTEXT_AUX11_FLAG_SHIFT 13
#define __XSTORM_ISCSI_AG_CONTEXT_AUX12_FLAG (0x1<<14)
#define __XSTORM_ISCSI_AG_CONTEXT_AUX12_FLAG_SHIFT 14
#define __XSTORM_ISCSI_AG_CONTEXT_RX_WND_SCL_EN (0x1<<15)
#define __XSTORM_ISCSI_AG_CONTEXT_RX_WND_SCL_EN_SHIFT 15
	u8 agg_val3_th;
	u8 agg_vars6;
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE6 (0x7<<0)
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE6_SHIFT 0
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE7 (0x7<<3)
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE7_SHIFT 3
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE4 (0x3<<6)
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE4_SHIFT 6
#elif defined(__LITTLE_ENDIAN)
	u8 agg_vars6;
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE6 (0x7<<0)
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE6_SHIFT 0
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE7 (0x7<<3)
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE7_SHIFT 3
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE4 (0x3<<6)
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE4_SHIFT 6
	u8 agg_val3_th;
	u16 agg_vars7;
#define __XSTORM_ISCSI_AG_CONTEXT_AGG_VAL11_DECISION_RULE (0x7<<0)
#define __XSTORM_ISCSI_AG_CONTEXT_AGG_VAL11_DECISION_RULE_SHIFT 0
#define __XSTORM_ISCSI_AG_CONTEXT_AUX13_FLAG (0x1<<3)
#define __XSTORM_ISCSI_AG_CONTEXT_AUX13_FLAG_SHIFT 3
#define __XSTORM_ISCSI_AG_CONTEXT_STORMS_SYNC_CF (0x3<<4)
#define __XSTORM_ISCSI_AG_CONTEXT_STORMS_SYNC_CF_SHIFT 4
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE3 (0x3<<6)
#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE3_SHIFT 6
#define XSTORM_ISCSI_AG_CONTEXT_AUX1_CF (0x3<<8)
#define XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_SHIFT 8
#define __XSTORM_ISCSI_AG_CONTEXT_COMPLETION_SEQ_DECISION_MASK (0x1<<10)
#define __XSTORM_ISCSI_AG_CONTEXT_COMPLETION_SEQ_DECISION_MASK_SHIFT 10
#define __XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_EN (0x1<<11)
#define __XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_EN_SHIFT 11
#define __XSTORM_ISCSI_AG_CONTEXT_AUX10_FLAG (0x1<<12)
#define __XSTORM_ISCSI_AG_CONTEXT_AUX10_FLAG_SHIFT 12
#define __XSTORM_ISCSI_AG_CONTEXT_AUX11_FLAG (0x1<<13)
#define __XSTORM_ISCSI_AG_CONTEXT_AUX11_FLAG_SHIFT 13
#define __XSTORM_ISCSI_AG_CONTEXT_AUX12_FLAG (0x1<<14)
#define __XSTORM_ISCSI_AG_CONTEXT_AUX12_FLAG_SHIFT 14
#define __XSTORM_ISCSI_AG_CONTEXT_RX_WND_SCL_EN (0x1<<15)
#define __XSTORM_ISCSI_AG_CONTEXT_RX_WND_SCL_EN_SHIFT 15
#endif
#if defined(__BIG_ENDIAN)
	u16 __agg_val11_th;
	u16 __gen_data;
#elif defined(__LITTLE_ENDIAN)
	u16 __gen_data;
	u16 __agg_val11_th;
#endif
#if defined(__BIG_ENDIAN)
	u8 __reserved1;
	u8 __agg_val6_th;
	u16 __agg_val9;
#elif defined(__LITTLE_ENDIAN)
	u16 __agg_val9;
	u8 __agg_val6_th;
	u8 __reserved1;
#endif
#if defined(__BIG_ENDIAN)
	u16 hq_prod;
	u16 hq_cons;
#elif defined(__LITTLE_ENDIAN)
	u16 hq_cons;
	u16 hq_prod;
#endif
	u32 agg_vars8;
#define XSTORM_ISCSI_AG_CONTEXT_AGG_MISC2 (0xFFFFFF<<0)
#define XSTORM_ISCSI_AG_CONTEXT_AGG_MISC2_SHIFT 0
#define XSTORM_ISCSI_AG_CONTEXT_AGG_MISC3 (0xFF<<24)
#define XSTORM_ISCSI_AG_CONTEXT_AGG_MISC3_SHIFT 24
#if defined(__BIG_ENDIAN)
	u16 r2tq_prod;
	u16 sq_prod;
#elif defined(__LITTLE_ENDIAN)
	u16 sq_prod;
	u16 r2tq_prod;
#endif
#if defined(__BIG_ENDIAN)
	u8 agg_val3;
	u8 agg_val6;
	u8 agg_val5_th;
	u8 agg_val5;
#elif defined(__LITTLE_ENDIAN)
	u8 agg_val5;
	u8 agg_val5_th;
	u8 agg_val6;
	u8 agg_val3;
#endif
#if defined(__BIG_ENDIAN)
	u16 __agg_misc1;
	u16 agg_limit1;
#elif defined(__LITTLE_ENDIAN)
	u16 agg_limit1;
	u16 __agg_misc1;
#endif
	u32 hq_cons_tcp_seq;
	u32 exp_stat_sn;
	u32 rst_seq_num;
};

/*
 * The tcp aggregative context section of Tstorm
 */
struct tstorm_tcp_tcp_ag_context_section {
	u32 __agg_val1;
#if defined(__BIG_ENDIAN)
	u8 __tcp_agg_vars2;
	u8 __agg_val3;
	u16 __agg_val2;
#elif defined(__LITTLE_ENDIAN)
	u16 __agg_val2;
	u8 __agg_val3;
	u8 __tcp_agg_vars2;
#endif
#if defined(__BIG_ENDIAN)
	u16 __agg_val5;
	u8 __agg_val6;
	u8 __tcp_agg_vars3;
#elif defined(__LITTLE_ENDIAN)
	u8 __tcp_agg_vars3;
	u8 __agg_val6;
	u16 __agg_val5;
#endif
	u32 snd_nxt;
	u32 rtt_seq;
	u32 rtt_time;
	u32 __reserved66;
	u32 wnd_right_edge;
	u32 tcp_agg_vars1;
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_FIN_SENT_FLAG (0x1<<0)
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_FIN_SENT_FLAG_SHIFT 0
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG (0x1<<1)
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG_SHIFT 1
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_WND_UPD_CF (0x3<<2)
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_WND_UPD_CF_SHIFT 2
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF (0x3<<4)
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_SHIFT 4
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_WND_UPD_CF_EN (0x1<<6)
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_WND_UPD_CF_EN_SHIFT 6
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_EN (0x1<<7)
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_EN_SHIFT 7
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN (0x1<<8)
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN_SHIFT 8
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_SND_NXT_EN (0x1<<9)
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_SND_NXT_EN_SHIFT 9
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG (0x1<<10)
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT 10
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_FLAG (0x1<<11)
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_FLAG_SHIFT 11
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_CF_EN (0x1<<12)
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_CF_EN_SHIFT 12
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_CF_EN (0x1<<13)
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_CF_EN_SHIFT 13
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_CF (0x3<<14)
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_CF_SHIFT 14
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_CF (0x3<<16)
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_CF_SHIFT 16
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_BLOCKED (0x1<<18)
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_BLOCKED_SHIFT 18
#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN (0x1<<19)
#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN_SHIFT 19
#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX11_CF_EN (0x1<<20)
#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX11_CF_EN_SHIFT 20
#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX12_CF_EN (0x1<<21)
#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX12_CF_EN_SHIFT 21
#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RESERVED1 (0x3<<22)
#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RESERVED1_SHIFT 22
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ (0xF<<24)
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ_SHIFT 24
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ (0xF<<28)
#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ_SHIFT 28
	u32 snd_max;
	u32 snd_una;
	u32 __reserved2;
};

/*
 * The iscsi aggregative context of Tstorm
 */
struct tstorm_iscsi_ag_context {
#if defined(__BIG_ENDIAN)
	u16 ulp_credit;
	u8 agg_vars1;
#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0 (0x1<<0)
#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT 0
#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1 (0x1<<1)
#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT 1
#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2 (0x1<<2)
#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT 2
#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3 (0x1<<3)
#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT 3
#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF (0x3<<4)
#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_SHIFT 4
#define __TSTORM_ISCSI_AG_CONTEXT_AUX3_FLAG (0x1<<6)
#define __TSTORM_ISCSI_AG_CONTEXT_AUX3_FLAG_SHIFT 6
#define __TSTORM_ISCSI_AG_CONTEXT_ACK_ON_FIN_SENT_FLAG (0x1<<7)
#define __TSTORM_ISCSI_AG_CONTEXT_ACK_ON_FIN_SENT_FLAG_SHIFT 7
	u8 state;
#elif defined(__LITTLE_ENDIAN)
	u8 state;
	u8 agg_vars1;
#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0 (0x1<<0)
#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT 0
#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1 (0x1<<1)
#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT 1
#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2 (0x1<<2)
#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT 2
#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3 (0x1<<3)
#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT 3
#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF (0x3<<4)
#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_SHIFT 4
#define __TSTORM_ISCSI_AG_CONTEXT_AUX3_FLAG (0x1<<6)
#define __TSTORM_ISCSI_AG_CONTEXT_AUX3_FLAG_SHIFT 6
#define __TSTORM_ISCSI_AG_CONTEXT_ACK_ON_FIN_SENT_FLAG (0x1<<7)
#define __TSTORM_ISCSI_AG_CONTEXT_ACK_ON_FIN_SENT_FLAG_SHIFT 7
	u16 ulp_credit;
#endif
#if defined(__BIG_ENDIAN)
	u16 __agg_val4;
	u16 agg_vars2;
#define __TSTORM_ISCSI_AG_CONTEXT_MSL_TIMER_SET_FLAG (0x1<<0)
#define __TSTORM_ISCSI_AG_CONTEXT_MSL_TIMER_SET_FLAG_SHIFT 0
#define __TSTORM_ISCSI_AG_CONTEXT_FIN_SENT_FIRST_FLAG (0x1<<1)
#define __TSTORM_ISCSI_AG_CONTEXT_FIN_SENT_FIRST_FLAG_SHIFT 1
#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF (0x3<<2)
#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_SHIFT 2
#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF (0x3<<4)
#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_SHIFT 4
#define __TSTORM_ISCSI_AG_CONTEXT_AUX6_CF (0x3<<6)
#define __TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_SHIFT 6
#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_CF (0x3<<8)
#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_SHIFT 8
#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_FLAG (0x1<<10)
#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_FLAG_SHIFT 10
#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN (0x1<<11)
#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN_SHIFT 11
#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_EN (0x1<<12)
#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_EN_SHIFT 12
#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_EN (0x1<<13)
#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_EN_SHIFT 13
#define TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_EN (0x1<<14)
#define TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_EN_SHIFT 14
#define TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_EN (0x1<<15)
#define TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_EN_SHIFT 15
#elif defined(__LITTLE_ENDIAN)
	u16 agg_vars2;
#define __TSTORM_ISCSI_AG_CONTEXT_MSL_TIMER_SET_FLAG (0x1<<0)
#define __TSTORM_ISCSI_AG_CONTEXT_MSL_TIMER_SET_FLAG_SHIFT 0
#define __TSTORM_ISCSI_AG_CONTEXT_FIN_SENT_FIRST_FLAG (0x1<<1)
#define __TSTORM_ISCSI_AG_CONTEXT_FIN_SENT_FIRST_FLAG_SHIFT 1
#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF (0x3<<2)
#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_SHIFT 2
#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF (0x3<<4)
#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_SHIFT 4
#define __TSTORM_ISCSI_AG_CONTEXT_AUX6_CF (0x3<<6)
#define __TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_SHIFT 6
#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_CF (0x3<<8)
#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_SHIFT 8
#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_FLAG (0x1<<10)
#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_FLAG_SHIFT 10
#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN (0x1<<11)
#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN_SHIFT 11
#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_EN (0x1<<12)
#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_EN_SHIFT 12
#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_EN (0x1<<13)
#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_EN_SHIFT 13
#define TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_EN (0x1<<14)
#define TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_EN_SHIFT 14
#define TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_EN (0x1<<15)
#define TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_EN_SHIFT 15
	u16 __agg_val4;
#endif
	struct tstorm_tcp_tcp_ag_context_section tcp;
};

/*
 * The iscsi aggregative context of Ustorm
 */
struct ustorm_iscsi_ag_context {
#if defined(__BIG_ENDIAN)
	u8 __aux_counter_flags;
	u8 agg_vars2;
#define USTORM_ISCSI_AG_CONTEXT_TX_CF (0x3<<0)
#define USTORM_ISCSI_AG_CONTEXT_TX_CF_SHIFT 0
#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF (0x3<<2)
#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_SHIFT 2
#define USTORM_ISCSI_AG_CONTEXT_AGG_MISC4_RULE (0x7<<4)
#define USTORM_ISCSI_AG_CONTEXT_AGG_MISC4_RULE_SHIFT 4
#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_MASK (0x1<<7)
#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_MASK_SHIFT 7
	u8 agg_vars1;
#define __USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0 (0x1<<0)
#define __USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT 0
#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1 (0x1<<1)
#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT 1
#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2 (0x1<<2)
#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT 2
#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3 (0x1<<3)
#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT 3
#define USTORM_ISCSI_AG_CONTEXT_INV_CF (0x3<<4)
#define USTORM_ISCSI_AG_CONTEXT_INV_CF_SHIFT 4
#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF (0x3<<6)
#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_SHIFT 6
	u8 state;
#elif defined(__LITTLE_ENDIAN)
	u8 state;
	u8 agg_vars1;
#define __USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0 (0x1<<0)
#define __USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT 0
#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1 (0x1<<1)
#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT 1
#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2 (0x1<<2)
#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT 2
#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3 (0x1<<3)
#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT 3
#define USTORM_ISCSI_AG_CONTEXT_INV_CF (0x3<<4)
#define USTORM_ISCSI_AG_CONTEXT_INV_CF_SHIFT 4
#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF (0x3<<6)
#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_SHIFT 6
	u8 agg_vars2;
#define USTORM_ISCSI_AG_CONTEXT_TX_CF (0x3<<0)
#define USTORM_ISCSI_AG_CONTEXT_TX_CF_SHIFT 0
#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF (0x3<<2)
#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_SHIFT 2
#define USTORM_ISCSI_AG_CONTEXT_AGG_MISC4_RULE (0x7<<4)
#define USTORM_ISCSI_AG_CONTEXT_AGG_MISC4_RULE_SHIFT 4
#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_MASK (0x1<<7)
#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_MASK_SHIFT 7
	u8 __aux_counter_flags;
#endif
#if defined(__BIG_ENDIAN)
	u8 cdu_usage;
	u8 agg_misc2;
	u16 __cq_local_comp_itt_val;
#elif defined(__LITTLE_ENDIAN)
	u16 __cq_local_comp_itt_val;
	u8 agg_misc2;
	u8 cdu_usage;
#endif
	u32 agg_misc4;
#if defined(__BIG_ENDIAN)
	u8 agg_val3_th;
	u8 agg_val3;
	u16 agg_misc3;
#elif defined(__LITTLE_ENDIAN)
	u16 agg_misc3;
	u8 agg_val3;
	u8 agg_val3_th;
#endif
	u32 agg_val1;
	u32 agg_misc4_th;
#if defined(__BIG_ENDIAN)
	u16 agg_val2_th;
	u16 agg_val2;
#elif defined(__LITTLE_ENDIAN)
	u16 agg_val2;
	u16 agg_val2_th;
#endif
#if defined(__BIG_ENDIAN)
	u16 __reserved2;
	u8 decision_rules;
#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_RULE (0x7<<0)
#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_RULE_SHIFT 0
#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL3_RULE (0x7<<3)
#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL3_RULE_SHIFT 3
#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG (0x1<<6)
#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG_SHIFT 6
#define __USTORM_ISCSI_AG_CONTEXT_RESERVED1 (0x1<<7)
#define __USTORM_ISCSI_AG_CONTEXT_RESERVED1_SHIFT 7
	u8 decision_rule_enable_bits;
#define USTORM_ISCSI_AG_CONTEXT_INV_CF_EN (0x1<<0)
#define USTORM_ISCSI_AG_CONTEXT_INV_CF_EN_SHIFT 0
#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_EN (0x1<<1)
#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_EN_SHIFT 1
#define USTORM_ISCSI_AG_CONTEXT_TX_CF_EN (0x1<<2)
#define USTORM_ISCSI_AG_CONTEXT_TX_CF_EN_SHIFT 2
#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_EN (0x1<<3)
#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_EN_SHIFT 3
#define __USTORM_ISCSI_AG_CONTEXT_CQ_LOCAL_COMP_CF_EN (0x1<<4)
#define __USTORM_ISCSI_AG_CONTEXT_CQ_LOCAL_COMP_CF_EN_SHIFT 4
#define __USTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN (0x1<<5)
#define __USTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN_SHIFT 5
#define __USTORM_ISCSI_AG_CONTEXT_AUX3_CF_EN (0x1<<6)
#define __USTORM_ISCSI_AG_CONTEXT_AUX3_CF_EN_SHIFT 6
#define __USTORM_ISCSI_AG_CONTEXT_DQ_CF_EN (0x1<<7)
#define __USTORM_ISCSI_AG_CONTEXT_DQ_CF_EN_SHIFT 7
#elif defined(__LITTLE_ENDIAN)
	u8 decision_rule_enable_bits;
#define USTORM_ISCSI_AG_CONTEXT_INV_CF_EN (0x1<<0)
#define USTORM_ISCSI_AG_CONTEXT_INV_CF_EN_SHIFT 0
#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_EN (0x1<<1)
#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_EN_SHIFT 1
#define USTORM_ISCSI_AG_CONTEXT_TX_CF_EN (0x1<<2)
#define USTORM_ISCSI_AG_CONTEXT_TX_CF_EN_SHIFT 2
#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_EN (0x1<<3)
#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_EN_SHIFT 3
#define __USTORM_ISCSI_AG_CONTEXT_CQ_LOCAL_COMP_CF_EN (0x1<<4)
#define __USTORM_ISCSI_AG_CONTEXT_CQ_LOCAL_COMP_CF_EN_SHIFT 4
#define __USTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN (0x1<<5)
#define __USTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN_SHIFT 5
#define __USTORM_ISCSI_AG_CONTEXT_AUX3_CF_EN (0x1<<6)
#define __USTORM_ISCSI_AG_CONTEXT_AUX3_CF_EN_SHIFT 6
#define __USTORM_ISCSI_AG_CONTEXT_DQ_CF_EN (0x1<<7)
#define __USTORM_ISCSI_AG_CONTEXT_DQ_CF_EN_SHIFT 7
	u8 decision_rules;
#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_RULE (0x7<<0)
#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_RULE_SHIFT 0
#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL3_RULE (0x7<<3)
#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL3_RULE_SHIFT 3
#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG (0x1<<6)
#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG_SHIFT 6
#define __USTORM_ISCSI_AG_CONTEXT_RESERVED1 (0x1<<7)
#define __USTORM_ISCSI_AG_CONTEXT_RESERVED1_SHIFT 7
	u16 __reserved2;
#endif
};

/*
 * Ethernet context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_eth_context_section {
#if defined(__BIG_ENDIAN)
	u8 remote_addr_4;
	u8 remote_addr_5;
	u8 local_addr_0;
	u8 local_addr_1;
#elif defined(__LITTLE_ENDIAN)
	u8 local_addr_1;
	u8 local_addr_0;
	u8 remote_addr_5;
	u8 remote_addr_4;
#endif
#if defined(__BIG_ENDIAN)
	u8 remote_addr_0;
	u8 remote_addr_1;
	u8 remote_addr_2;
	u8 remote_addr_3;
#elif defined(__LITTLE_ENDIAN)
	u8 remote_addr_3;
	u8 remote_addr_2;
	u8 remote_addr_1;
	u8 remote_addr_0;
#endif
#if defined(__BIG_ENDIAN)
	u16 reserved_vlan_type;
	u16 params;
#define XSTORM_ETH_CONTEXT_SECTION_VLAN_ID (0xFFF<<0)
#define XSTORM_ETH_CONTEXT_SECTION_VLAN_ID_SHIFT 0
#define XSTORM_ETH_CONTEXT_SECTION_CFI (0x1<<12)
#define XSTORM_ETH_CONTEXT_SECTION_CFI_SHIFT 12
#define XSTORM_ETH_CONTEXT_SECTION_PRIORITY (0x7<<13)
#define XSTORM_ETH_CONTEXT_SECTION_PRIORITY_SHIFT 13
#elif defined(__LITTLE_ENDIAN)
	u16 params;
#define XSTORM_ETH_CONTEXT_SECTION_VLAN_ID (0xFFF<<0)
#define XSTORM_ETH_CONTEXT_SECTION_VLAN_ID_SHIFT 0
#define XSTORM_ETH_CONTEXT_SECTION_CFI (0x1<<12)
#define XSTORM_ETH_CONTEXT_SECTION_CFI_SHIFT 12
#define XSTORM_ETH_CONTEXT_SECTION_PRIORITY (0x7<<13)
#define XSTORM_ETH_CONTEXT_SECTION_PRIORITY_SHIFT 13
	u16 reserved_vlan_type;
#endif
#if defined(__BIG_ENDIAN)
	u8 local_addr_2;
	u8 local_addr_3;
	u8 local_addr_4;
	u8 local_addr_5;
#elif defined(__LITTLE_ENDIAN)
	u8 local_addr_5;
	u8 local_addr_4;
	u8 local_addr_3;
	u8 local_addr_2;
#endif
};

/*
 * IpV4 context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_ip_v4_context_section {
#if defined(__BIG_ENDIAN)
	u16 __pbf_hdr_cmd_rsvd_id;
	u16 __pbf_hdr_cmd_rsvd_flags_offset;
#elif defined(__LITTLE_ENDIAN)
	u16 __pbf_hdr_cmd_rsvd_flags_offset;
	u16 __pbf_hdr_cmd_rsvd_id;
#endif
#if defined(__BIG_ENDIAN)
	u8 __pbf_hdr_cmd_rsvd_ver_ihl;
	u8 tos;
	u16 __pbf_hdr_cmd_rsvd_length;
#elif defined(__LITTLE_ENDIAN)
	u16 __pbf_hdr_cmd_rsvd_length;
	u8 tos;
	u8 __pbf_hdr_cmd_rsvd_ver_ihl;
#endif
	u32 ip_local_addr;
#if defined(__BIG_ENDIAN)
	u8 ttl;
	u8 __pbf_hdr_cmd_rsvd_protocol;
	u16 __pbf_hdr_cmd_rsvd_csum;
#elif defined(__LITTLE_ENDIAN)
	u16 __pbf_hdr_cmd_rsvd_csum;
	u8 __pbf_hdr_cmd_rsvd_protocol;
	u8 ttl;
#endif
	u32 __pbf_hdr_cmd_rsvd_1;
	u32 ip_remote_addr;
};

/*
 * context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_padded_ip_v4_context_section {
	struct xstorm_ip_v4_context_section ip_v4;
	u32 reserved1[4];
};

/*
 * IpV6 context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_ip_v6_context_section {
#if defined(__BIG_ENDIAN)
	u16 pbf_hdr_cmd_rsvd_payload_len;
	u8 pbf_hdr_cmd_rsvd_nxt_hdr;
	u8 hop_limit;
#elif defined(__LITTLE_ENDIAN)
	u8 hop_limit;
	u8 pbf_hdr_cmd_rsvd_nxt_hdr;
	u16 pbf_hdr_cmd_rsvd_payload_len;
#endif
	u32 priority_flow_label;
#define XSTORM_IP_V6_CONTEXT_SECTION_FLOW_LABEL (0xFFFFF<<0)
#define XSTORM_IP_V6_CONTEXT_SECTION_FLOW_LABEL_SHIFT 0
#define XSTORM_IP_V6_CONTEXT_SECTION_TRAFFIC_CLASS (0xFF<<20)
#define XSTORM_IP_V6_CONTEXT_SECTION_TRAFFIC_CLASS_SHIFT 20
#define XSTORM_IP_V6_CONTEXT_SECTION_PBF_HDR_CMD_RSVD_VER (0xF<<28)
#define XSTORM_IP_V6_CONTEXT_SECTION_PBF_HDR_CMD_RSVD_VER_SHIFT 28
	u32 ip_local_addr_lo_hi;
	u32 ip_local_addr_lo_lo;
	u32 ip_local_addr_hi_hi;
	u32 ip_local_addr_hi_lo;
	u32 ip_remote_addr_lo_hi;
	u32 ip_remote_addr_lo_lo;
	u32 ip_remote_addr_hi_hi;
	u32 ip_remote_addr_hi_lo;
};

union xstorm_ip_context_section_types {
	struct xstorm_padded_ip_v4_context_section padded_ip_v4;
	struct xstorm_ip_v6_context_section ip_v6;
};

/*
 * TCP context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_tcp_context_section {
	u32 snd_max;
#if defined(__BIG_ENDIAN)
	u16 remote_port;
	u16 local_port;
#elif defined(__LITTLE_ENDIAN)
	u16 local_port;
	u16 remote_port;
#endif
#if defined(__BIG_ENDIAN)
	u8 original_nagle_1b;
	u8 ts_enabled;
	u16 tcp_params;
#define XSTORM_TCP_CONTEXT_SECTION_TOTAL_HEADER_SIZE (0xFF<<0)
#define XSTORM_TCP_CONTEXT_SECTION_TOTAL_HEADER_SIZE_SHIFT 0
#define __XSTORM_TCP_CONTEXT_SECTION_ECT_BIT (0x1<<8)
#define __XSTORM_TCP_CONTEXT_SECTION_ECT_BIT_SHIFT 8
#define __XSTORM_TCP_CONTEXT_SECTION_ECN_ENABLED (0x1<<9)
#define __XSTORM_TCP_CONTEXT_SECTION_ECN_ENABLED_SHIFT 9
#define XSTORM_TCP_CONTEXT_SECTION_SACK_ENABLED (0x1<<10)
#define XSTORM_TCP_CONTEXT_SECTION_SACK_ENABLED_SHIFT 10
#define XSTORM_TCP_CONTEXT_SECTION_SMALL_WIN_ADV (0x1<<11)
#define XSTORM_TCP_CONTEXT_SECTION_SMALL_WIN_ADV_SHIFT 11
#define XSTORM_TCP_CONTEXT_SECTION_FIN_SENT_FLAG (0x1<<12)
#define XSTORM_TCP_CONTEXT_SECTION_FIN_SENT_FLAG_SHIFT 12
#define XSTORM_TCP_CONTEXT_SECTION_WINDOW_SATURATED (0x1<<13)
#define XSTORM_TCP_CONTEXT_SECTION_WINDOW_SATURATED_SHIFT 13
#define XSTORM_TCP_CONTEXT_SECTION_SLOWPATH_QUEUES_FLUSH_COUNTER (0x3<<14)
#define XSTORM_TCP_CONTEXT_SECTION_SLOWPATH_QUEUES_FLUSH_COUNTER_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 tcp_params;
#define XSTORM_TCP_CONTEXT_SECTION_TOTAL_HEADER_SIZE (0xFF<<0)
#define XSTORM_TCP_CONTEXT_SECTION_TOTAL_HEADER_SIZE_SHIFT 0
#define __XSTORM_TCP_CONTEXT_SECTION_ECT_BIT (0x1<<8)
#define __XSTORM_TCP_CONTEXT_SECTION_ECT_BIT_SHIFT 8
#define __XSTORM_TCP_CONTEXT_SECTION_ECN_ENABLED (0x1<<9)
#define __XSTORM_TCP_CONTEXT_SECTION_ECN_ENABLED_SHIFT 9
#define XSTORM_TCP_CONTEXT_SECTION_SACK_ENABLED (0x1<<10)
#define XSTORM_TCP_CONTEXT_SECTION_SACK_ENABLED_SHIFT 10
#define XSTORM_TCP_CONTEXT_SECTION_SMALL_WIN_ADV (0x1<<11)
#define XSTORM_TCP_CONTEXT_SECTION_SMALL_WIN_ADV_SHIFT 11
#define XSTORM_TCP_CONTEXT_SECTION_FIN_SENT_FLAG (0x1<<12)
#define XSTORM_TCP_CONTEXT_SECTION_FIN_SENT_FLAG_SHIFT 12
#define XSTORM_TCP_CONTEXT_SECTION_WINDOW_SATURATED (0x1<<13)
#define XSTORM_TCP_CONTEXT_SECTION_WINDOW_SATURATED_SHIFT 13
#define XSTORM_TCP_CONTEXT_SECTION_SLOWPATH_QUEUES_FLUSH_COUNTER (0x3<<14)
#define XSTORM_TCP_CONTEXT_SECTION_SLOWPATH_QUEUES_FLUSH_COUNTER_SHIFT 14
	u8 ts_enabled;
	u8 original_nagle_1b;
#endif
#if defined(__BIG_ENDIAN)
	u16 pseudo_csum;
	u16 window_scaling_factor;
#elif defined(__LITTLE_ENDIAN)
	u16 window_scaling_factor;
	u16 pseudo_csum;
#endif
	u32 reserved2;
	u32 ts_time_diff;
	u32 __next_timer_expir;
};

/*
 * Common context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_common_context_section {
	struct xstorm_eth_context_section ethernet;
	union xstorm_ip_context_section_types ip_union;
	struct xstorm_tcp_context_section tcp;
#if defined(__BIG_ENDIAN)
	u16 reserved;
	u8 statistics_params;
#define XSTORM_COMMON_CONTEXT_SECTION_UPDATE_L2_STATSTICS (0x1<<0)
#define XSTORM_COMMON_CONTEXT_SECTION_UPDATE_L2_STATSTICS_SHIFT 0
#define XSTORM_COMMON_CONTEXT_SECTION_UPDATE_L4_STATSTICS (0x1<<1)
#define XSTORM_COMMON_CONTEXT_SECTION_UPDATE_L4_STATSTICS_SHIFT 1
#define XSTORM_COMMON_CONTEXT_SECTION_STATISTICS_COUNTER_ID (0x1F<<2)
#define XSTORM_COMMON_CONTEXT_SECTION_STATISTICS_COUNTER_ID_SHIFT 2
#define XSTORM_COMMON_CONTEXT_SECTION_DCB_EXISTS (0x1<<7)
#define XSTORM_COMMON_CONTEXT_SECTION_DCB_EXISTS_SHIFT 7
	u8 ip_version_1b;
#elif defined(__LITTLE_ENDIAN)
	u8 ip_version_1b;
	u8 statistics_params;
#define XSTORM_COMMON_CONTEXT_SECTION_UPDATE_L2_STATSTICS (0x1<<0)
#define XSTORM_COMMON_CONTEXT_SECTION_UPDATE_L2_STATSTICS_SHIFT 0
#define XSTORM_COMMON_CONTEXT_SECTION_UPDATE_L4_STATSTICS (0x1<<1)
#define XSTORM_COMMON_CONTEXT_SECTION_UPDATE_L4_STATSTICS_SHIFT 1
#define XSTORM_COMMON_CONTEXT_SECTION_STATISTICS_COUNTER_ID (0x1F<<2)
#define XSTORM_COMMON_CONTEXT_SECTION_STATISTICS_COUNTER_ID_SHIFT 2
#define XSTORM_COMMON_CONTEXT_SECTION_DCB_EXISTS (0x1<<7)
#define XSTORM_COMMON_CONTEXT_SECTION_DCB_EXISTS_SHIFT 7
	u16 reserved;
#endif
};

/*
 * Flags used in ISCSI context section
 */
struct xstorm_iscsi_context_flags {
	u8 flags;
#define XSTORM_ISCSI_CONTEXT_FLAGS_B_IMMEDIATE_DATA (0x1<<0)
#define XSTORM_ISCSI_CONTEXT_FLAGS_B_IMMEDIATE_DATA_SHIFT 0
#define XSTORM_ISCSI_CONTEXT_FLAGS_B_INITIAL_R2T (0x1<<1)
#define XSTORM_ISCSI_CONTEXT_FLAGS_B_INITIAL_R2T_SHIFT 1
#define XSTORM_ISCSI_CONTEXT_FLAGS_B_EN_HEADER_DIGEST (0x1<<2)
#define XSTORM_ISCSI_CONTEXT_FLAGS_B_EN_HEADER_DIGEST_SHIFT 2
#define XSTORM_ISCSI_CONTEXT_FLAGS_B_EN_DATA_DIGEST (0x1<<3)
#define XSTORM_ISCSI_CONTEXT_FLAGS_B_EN_DATA_DIGEST_SHIFT 3
#define XSTORM_ISCSI_CONTEXT_FLAGS_B_HQ_BD_WRITTEN (0x1<<4)
#define XSTORM_ISCSI_CONTEXT_FLAGS_B_HQ_BD_WRITTEN_SHIFT 4
#define XSTORM_ISCSI_CONTEXT_FLAGS_B_LAST_OP_SQ (0x1<<5)
#define XSTORM_ISCSI_CONTEXT_FLAGS_B_LAST_OP_SQ_SHIFT 5
#define XSTORM_ISCSI_CONTEXT_FLAGS_B_UPDATE_SND_NXT (0x1<<6)
#define XSTORM_ISCSI_CONTEXT_FLAGS_B_UPDATE_SND_NXT_SHIFT 6
#define XSTORM_ISCSI_CONTEXT_FLAGS_RESERVED4 (0x1<<7)
#define XSTORM_ISCSI_CONTEXT_FLAGS_RESERVED4_SHIFT 7
};

struct iscsi_task_context_entry_x {
	u32 data_out_buffer_offset;
	u32 itt;
	u32 data_sn;
};

struct iscsi_task_context_entry_xuc_x_write_only {
	u32 tx_r2t_sn;
};

struct iscsi_task_context_entry_xuc_xu_write_both {
	u32 sgl_base_lo;
	u32 sgl_base_hi;
#if defined(__BIG_ENDIAN)
	u8 sgl_size;
	u8 sge_index;
	u16 sge_offset;
#elif defined(__LITTLE_ENDIAN)
	u16 sge_offset;
	u8 sge_index;
	u8 sgl_size;
#endif
};

/*
 * iSCSI context section
 */
struct xstorm_iscsi_context_section {
	u32 first_burst_length;
	u32 max_send_pdu_length;
	struct regpair sq_pbl_base;
	struct regpair sq_curr_pbe;
	struct regpair hq_pbl_base;
	struct regpair hq_curr_pbe_base;
	struct regpair r2tq_pbl_base;
	struct regpair r2tq_curr_pbe_base;
	struct regpair task_pbl_base;
#if defined(__BIG_ENDIAN)
	u16 data_out_count;
	struct xstorm_iscsi_context_flags flags;
	u8 task_pbl_cache_idx;
#elif defined(__LITTLE_ENDIAN)
	u8 task_pbl_cache_idx;
	struct xstorm_iscsi_context_flags flags;
	u16 data_out_count;
#endif
	u32 seq_more_2_send;
	u32 pdu_more_2_send;
	struct iscsi_task_context_entry_x temp_tce_x;
	struct iscsi_task_context_entry_xuc_x_write_only temp_tce_x_wr;
	struct iscsi_task_context_entry_xuc_xu_write_both temp_tce_xu_wr;
	struct regpair lun;
	u32 exp_data_transfer_len_ttt;
	u32 pdu_data_2_rxmit;
	u32 rxmit_bytes_2_dr;
#if defined(__BIG_ENDIAN)
	u16 rxmit_sge_offset;
	u16 hq_rxmit_cons;
#elif defined(__LITTLE_ENDIAN)
	u16 hq_rxmit_cons;
	u16 rxmit_sge_offset;
#endif
#if defined(__BIG_ENDIAN)
	u16 r2tq_cons;
	u8 rxmit_flags;
#define XSTORM_ISCSI_CONTEXT_SECTION_B_NEW_HQ_BD (0x1<<0)
#define XSTORM_ISCSI_CONTEXT_SECTION_B_NEW_HQ_BD_SHIFT 0
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PDU_HDR (0x1<<1)
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PDU_HDR_SHIFT 1
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_END_PDU (0x1<<2)
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_END_PDU_SHIFT 2
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_DR (0x1<<3)
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_DR_SHIFT 3
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_START_DR (0x1<<4)
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_START_DR_SHIFT 4
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PADDING (0x3<<5)
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PADDING_SHIFT 5
#define XSTORM_ISCSI_CONTEXT_SECTION_B_ISCSI_CONT_FAST_RXMIT (0x1<<7)
#define XSTORM_ISCSI_CONTEXT_SECTION_B_ISCSI_CONT_FAST_RXMIT_SHIFT 7
	u8 rxmit_sge_idx;
#elif defined(__LITTLE_ENDIAN)
	u8 rxmit_sge_idx;
	u8 rxmit_flags;
#define XSTORM_ISCSI_CONTEXT_SECTION_B_NEW_HQ_BD (0x1<<0)
#define XSTORM_ISCSI_CONTEXT_SECTION_B_NEW_HQ_BD_SHIFT 0
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PDU_HDR (0x1<<1)
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PDU_HDR_SHIFT 1
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_END_PDU (0x1<<2)
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_END_PDU_SHIFT 2
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_DR (0x1<<3)
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_DR_SHIFT 3
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_START_DR (0x1<<4)
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_START_DR_SHIFT 4
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PADDING (0x3<<5)
#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PADDING_SHIFT 5
#define XSTORM_ISCSI_CONTEXT_SECTION_B_ISCSI_CONT_FAST_RXMIT (0x1<<7)
#define XSTORM_ISCSI_CONTEXT_SECTION_B_ISCSI_CONT_FAST_RXMIT_SHIFT 7
	u16 r2tq_cons;
#endif
	u32 hq_rxmit_tcp_seq;
};

/*
 * Xstorm iSCSI Storm Context
 */
struct xstorm_iscsi_st_context {
	struct xstorm_common_context_section common;
	struct xstorm_iscsi_context_section iscsi;
};

/*
 * CQ DB CQ producer and pending completion counter
 */
struct iscsi_cq_db_prod_pnd_cmpltn_cnt {
#if defined(__BIG_ENDIAN)
	u16 cntr;
	u16 prod;
#elif defined(__LITTLE_ENDIAN)
	u16 prod;
	u16 cntr;
#endif
};

/*
 * CQ DB pending completion ITT array
 */
struct iscsi_cq_db_prod_pnd_cmpltn_cnt_arr {
	struct iscsi_cq_db_prod_pnd_cmpltn_cnt prod_pend_comp[8];
};

/*
 * Cstorm CQ sequence to notify array, updated by driver
 */
struct iscsi_cq_db_sqn_2_notify_arr {
	u16 sqn[8];
};

/*
 * Cstorm iSCSI Storm Context
 */
struct cstorm_iscsi_st_context {
	struct iscsi_cq_db_prod_pnd_cmpltn_cnt_arr cq_c_prod_pend_comp_ctr_arr;
	struct iscsi_cq_db_sqn_2_notify_arr cq_c_prod_sqn_arr;
	struct iscsi_cq_db_sqn_2_notify_arr cq_c_sqn_2_notify_arr;
	struct regpair hq_pbl_base;
	struct regpair hq_curr_pbe;
	struct regpair task_pbl_base;
	struct regpair cq_db_base;
#if defined(__BIG_ENDIAN)
	u16 hq_bd_itt;
	u16 iscsi_conn_id;
#elif defined(__LITTLE_ENDIAN)
	u16 iscsi_conn_id;
	u16 hq_bd_itt;
#endif
	u32 hq_bd_data_segment_len;
	u32 hq_bd_buffer_offset;
#if defined(__BIG_ENDIAN)
	u8 timer_entry_idx;
	u8 cq_proc_en_bit_map;
	u8 cq_pend_comp_itt_valid_bit_map;
	u8 hq_bd_opcode;
#elif defined(__LITTLE_ENDIAN)
	u8 hq_bd_opcode;
	u8 cq_pend_comp_itt_valid_bit_map;
	u8 cq_proc_en_bit_map;
	u8 timer_entry_idx;
#endif
	u32 hq_tcp_seq;
#if defined(__BIG_ENDIAN)
	u16 flags;
#define CSTORM_ISCSI_ST_CONTEXT_DATA_DIGEST_EN (0x1<<0)
#define CSTORM_ISCSI_ST_CONTEXT_DATA_DIGEST_EN_SHIFT 0
#define CSTORM_ISCSI_ST_CONTEXT_HDR_DIGEST_EN (0x1<<1)
#define CSTORM_ISCSI_ST_CONTEXT_HDR_DIGEST_EN_SHIFT 1
#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_CTXT_VALID (0x1<<2)
#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_CTXT_VALID_SHIFT 2
#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_LCL_CMPLN_FLG (0x1<<3)
#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_LCL_CMPLN_FLG_SHIFT 3
#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_WRITE_TASK (0x1<<4)
#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_WRITE_TASK_SHIFT 4
#define CSTORM_ISCSI_ST_CONTEXT_CTRL_FLAGS_RSRV (0x7FF<<5)
#define CSTORM_ISCSI_ST_CONTEXT_CTRL_FLAGS_RSRV_SHIFT 5
	u16 hq_cons;
#elif defined(__LITTLE_ENDIAN)
	u16 hq_cons;
	u16 flags;
#define CSTORM_ISCSI_ST_CONTEXT_DATA_DIGEST_EN (0x1<<0)
#define CSTORM_ISCSI_ST_CONTEXT_DATA_DIGEST_EN_SHIFT 0
#define CSTORM_ISCSI_ST_CONTEXT_HDR_DIGEST_EN (0x1<<1)
#define CSTORM_ISCSI_ST_CONTEXT_HDR_DIGEST_EN_SHIFT 1
#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_CTXT_VALID (0x1<<2)
#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_CTXT_VALID_SHIFT 2
#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_LCL_CMPLN_FLG (0x1<<3)
#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_LCL_CMPLN_FLG_SHIFT 3
#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_WRITE_TASK (0x1<<4)
#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_WRITE_TASK_SHIFT 4
#define CSTORM_ISCSI_ST_CONTEXT_CTRL_FLAGS_RSRV (0x7FF<<5)
#define CSTORM_ISCSI_ST_CONTEXT_CTRL_FLAGS_RSRV_SHIFT 5
#endif
	struct regpair rsrv1;
};

/*
 * Iscsi connection context
 */
struct iscsi_context {
	struct ustorm_iscsi_st_context ustorm_st_context;
	struct tstorm_iscsi_st_context tstorm_st_context;
	struct xstorm_iscsi_ag_context xstorm_ag_context;
	struct tstorm_iscsi_ag_context tstorm_ag_context;
	struct cstorm_iscsi_ag_context cstorm_ag_context;
	struct ustorm_iscsi_ag_context ustorm_ag_context;
	struct timers_block_context timers_context;
	struct regpair upb_context;
	struct xstorm_iscsi_st_context xstorm_st_context;
	struct regpair xpb_context;
	struct cstorm_iscsi_st_context cstorm_st_context;
};

/*
 * Buffer per connection, used in Tstorm
 */
struct iscsi_conn_buf {
	struct regpair reserved[8];
};

/*
 * ipv6 structure
 */
struct ip_v6_addr {
	u32 ip_addr_lo_lo;
	u32 ip_addr_lo_hi;
	u32 ip_addr_hi_lo;
	u32 ip_addr_hi_hi;
};

/*
 * l5cm- connection identification params
 */
struct l5cm_conn_addr_params {
	u32 pmtu;
#if defined(__BIG_ENDIAN)
	u8 remote_addr_3;
	u8 remote_addr_2;
	u8 remote_addr_1;
	u8 remote_addr_0;
#elif defined(__LITTLE_ENDIAN)
	u8 remote_addr_0;
	u8 remote_addr_1;
	u8 remote_addr_2;
	u8 remote_addr_3;
#endif
#if defined(__BIG_ENDIAN)
	u16 params;
#define L5CM_CONN_ADDR_PARAMS_IP_VERSION (0x1<<0)
#define L5CM_CONN_ADDR_PARAMS_IP_VERSION_SHIFT 0
#define L5CM_CONN_ADDR_PARAMS_RSRV (0x7FFF<<1)
#define L5CM_CONN_ADDR_PARAMS_RSRV_SHIFT 1
	u8 remote_addr_5;
	u8 remote_addr_4;
#elif defined(__LITTLE_ENDIAN)
	u8 remote_addr_4;
	u8 remote_addr_5;
	u16 params;
#define L5CM_CONN_ADDR_PARAMS_IP_VERSION (0x1<<0)
#define L5CM_CONN_ADDR_PARAMS_IP_VERSION_SHIFT 0
#define L5CM_CONN_ADDR_PARAMS_RSRV (0x7FFF<<1)
#define L5CM_CONN_ADDR_PARAMS_RSRV_SHIFT 1
#endif
	struct ip_v6_addr local_ip_addr;
	struct ip_v6_addr remote_ip_addr;
	u32 ipv6_flow_label_20b;
	u32 reserved1;
#if defined(__BIG_ENDIAN)
	u16 remote_tcp_port;
	u16 local_tcp_port;
#elif defined(__LITTLE_ENDIAN)
	u16 local_tcp_port;
	u16 remote_tcp_port;
#endif
};

/*
 * l5cm-xstorm connection buffer
 */
struct l5cm_xstorm_conn_buffer {
#if defined(__BIG_ENDIAN)
	u16 rsrv1;
	u16 params;
#define L5CM_XSTORM_CONN_BUFFER_NAGLE_ENABLE (0x1<<0)
#define L5CM_XSTORM_CONN_BUFFER_NAGLE_ENABLE_SHIFT 0
#define L5CM_XSTORM_CONN_BUFFER_RSRV (0x7FFF<<1)
#define L5CM_XSTORM_CONN_BUFFER_RSRV_SHIFT 1
#elif defined(__LITTLE_ENDIAN)
	u16 params;
#define L5CM_XSTORM_CONN_BUFFER_NAGLE_ENABLE (0x1<<0)
#define L5CM_XSTORM_CONN_BUFFER_NAGLE_ENABLE_SHIFT 0
#define L5CM_XSTORM_CONN_BUFFER_RSRV (0x7FFF<<1)
#define L5CM_XSTORM_CONN_BUFFER_RSRV_SHIFT 1
	u16 rsrv1;
#endif
#if defined(__BIG_ENDIAN)
	u16 mss;
	u16 pseudo_header_checksum;
#elif defined(__LITTLE_ENDIAN)
	u16 pseudo_header_checksum;
	u16 mss;
#endif
	u32 rcv_buf;
	u32 rsrv2;
	struct regpair context_addr;
};

/*
 * l5cm-tstorm connection buffer
 */
struct l5cm_tstorm_conn_buffer {
	u32 snd_buf;
	u32 rcv_buf;
#if defined(__BIG_ENDIAN)
	u16 params;
#define L5CM_TSTORM_CONN_BUFFER_DELAYED_ACK_ENABLE (0x1<<0)
#define L5CM_TSTORM_CONN_BUFFER_DELAYED_ACK_ENABLE_SHIFT 0
#define L5CM_TSTORM_CONN_BUFFER_RSRV (0x7FFF<<1)
#define L5CM_TSTORM_CONN_BUFFER_RSRV_SHIFT 1
	u8 ka_max_probe_count;
	u8 ka_enable;
#elif defined(__LITTLE_ENDIAN)
	u8 ka_enable;
	u8 ka_max_probe_count;
	u16 params;
#define L5CM_TSTORM_CONN_BUFFER_DELAYED_ACK_ENABLE (0x1<<0)
#define L5CM_TSTORM_CONN_BUFFER_DELAYED_ACK_ENABLE_SHIFT 0
#define L5CM_TSTORM_CONN_BUFFER_RSRV (0x7FFF<<1)
#define L5CM_TSTORM_CONN_BUFFER_RSRV_SHIFT 1
#endif
	u32 ka_timeout;
	u32 ka_interval;
	u32 max_rt_time;
};

/*
 * l5cm connection buffer for active side
 */
struct l5cm_active_conn_buffer {
	struct l5cm_conn_addr_params conn_addr_buf;
	struct l5cm_xstorm_conn_buffer xstorm_conn_buffer;
	struct l5cm_tstorm_conn_buffer tstorm_conn_buffer;
};

/*
 * l5cm slow path element
 */
struct l5cm_packet_size {
	u32 size;
	u32 rsrv;
};

/*
 * l5cm connection parameters
 */
union l5cm_reduce_param_union {
	u32 opaque1;
	u32 opaque2;
};

/*
 * l5cm connection parameters
 */
struct l5cm_reduce_conn {
	union l5cm_reduce_param_union opaque1;
	u32 opaque2;
};

/*
 * l5cm slow path element
 */
union l5cm_specific_data {
	u8 protocol_data[8];
	struct regpair phy_address;
	struct l5cm_packet_size packet_size;
	struct l5cm_reduce_conn reduced_conn;
};

/*
 * l5 slow path element
 */
struct l5cm_spe {
	struct spe_hdr hdr;
	union l5cm_specific_data data;
};

/*
 * Tstorm Tcp flags
 */
struct tstorm_l5cm_tcp_flags {
	u16 flags;
#define TSTORM_L5CM_TCP_FLAGS_VLAN_ID (0xFFF<<0)
#define TSTORM_L5CM_TCP_FLAGS_VLAN_ID_SHIFT 0
#define TSTORM_L5CM_TCP_FLAGS_RSRV0 (0x1<<12)
#define TSTORM_L5CM_TCP_FLAGS_RSRV0_SHIFT 12
#define TSTORM_L5CM_TCP_FLAGS_TS_ENABLED (0x1<<13)
#define TSTORM_L5CM_TCP_FLAGS_TS_ENABLED_SHIFT 13
#define TSTORM_L5CM_TCP_FLAGS_RSRV1 (0x3<<14)
#define TSTORM_L5CM_TCP_FLAGS_RSRV1_SHIFT 14
};

/*
 * Xstorm Tcp flags
 */
struct xstorm_l5cm_tcp_flags {
	u8 flags;
#define XSTORM_L5CM_TCP_FLAGS_ENC_ENABLED (0x1<<0)
#define XSTORM_L5CM_TCP_FLAGS_ENC_ENABLED_SHIFT 0
#define XSTORM_L5CM_TCP_FLAGS_TS_ENABLED (0x1<<1)
#define XSTORM_L5CM_TCP_FLAGS_TS_ENABLED_SHIFT 1
#define XSTORM_L5CM_TCP_FLAGS_WND_SCL_EN (0x1<<2)
#define XSTORM_L5CM_TCP_FLAGS_WND_SCL_EN_SHIFT 2
#define XSTORM_L5CM_TCP_FLAGS_RSRV (0x1F<<3)
#define XSTORM_L5CM_TCP_FLAGS_RSRV_SHIFT 3
};

#endif /* CNIC_DEFS_H */
