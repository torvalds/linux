
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

#define FCOE_KCQE_OPCODE_INIT_FUNC			(0x10)
#define FCOE_KCQE_OPCODE_DESTROY_FUNC			(0x11)
#define FCOE_KCQE_OPCODE_STAT_FUNC			(0x12)
#define FCOE_KCQE_OPCODE_OFFLOAD_CONN			(0x15)
#define FCOE_KCQE_OPCODE_ENABLE_CONN			(0x16)
#define FCOE_KCQE_OPCODE_DISABLE_CONN			(0x17)
#define FCOE_KCQE_OPCODE_DESTROY_CONN			(0x18)
#define FCOE_KCQE_OPCODE_CQ_EVENT_NOTIFICATION  (0x20)
#define FCOE_KCQE_OPCODE_FCOE_ERROR				(0x21)

#define FCOE_RAMROD_CMD_ID_INIT			(FCOE_KCQE_OPCODE_INIT_FUNC)
#define FCOE_RAMROD_CMD_ID_DESTROY		(FCOE_KCQE_OPCODE_DESTROY_FUNC)
#define FCOE_RAMROD_CMD_ID_OFFLOAD_CONN		(FCOE_KCQE_OPCODE_OFFLOAD_CONN)
#define FCOE_RAMROD_CMD_ID_ENABLE_CONN		(FCOE_KCQE_OPCODE_ENABLE_CONN)
#define FCOE_RAMROD_CMD_ID_DISABLE_CONN		(FCOE_KCQE_OPCODE_DISABLE_CONN)
#define FCOE_RAMROD_CMD_ID_DESTROY_CONN		(FCOE_KCQE_OPCODE_DESTROY_CONN)
#define FCOE_RAMROD_CMD_ID_STAT			(FCOE_KCQE_OPCODE_STAT_FUNC)
#define FCOE_RAMROD_CMD_ID_TERMINATE_CONN	(0x81)

#define FCOE_KWQE_OPCODE_INIT1                  (0)
#define FCOE_KWQE_OPCODE_INIT2                  (1)
#define FCOE_KWQE_OPCODE_INIT3                  (2)
#define FCOE_KWQE_OPCODE_OFFLOAD_CONN1  (3)
#define FCOE_KWQE_OPCODE_OFFLOAD_CONN2  (4)
#define FCOE_KWQE_OPCODE_OFFLOAD_CONN3  (5)
#define FCOE_KWQE_OPCODE_OFFLOAD_CONN4  (6)
#define FCOE_KWQE_OPCODE_ENABLE_CONN	(7)
#define FCOE_KWQE_OPCODE_DISABLE_CONN	(8)
#define FCOE_KWQE_OPCODE_DESTROY_CONN	(9)
#define FCOE_KWQE_OPCODE_DESTROY		(10)
#define FCOE_KWQE_OPCODE_STAT			(11)

#define FCOE_KCQE_COMPLETION_STATUS_CTX_ALLOC_FAILURE	(0x3)

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
 * Parameters initialized during offloaded according to FLOGI/PLOGI/PRLI and used in FCoE context section
 */
struct ustorm_fcoe_params {
#if defined(__BIG_ENDIAN)
	u16 fcoe_conn_id;
	u16 flags;
#define USTORM_FCOE_PARAMS_B_MUL_N_PORT_IDS (0x1<<0)
#define USTORM_FCOE_PARAMS_B_MUL_N_PORT_IDS_SHIFT 0
#define USTORM_FCOE_PARAMS_B_E_D_TOV_RES (0x1<<1)
#define USTORM_FCOE_PARAMS_B_E_D_TOV_RES_SHIFT 1
#define USTORM_FCOE_PARAMS_B_CONT_INCR_SEQ_CNT (0x1<<2)
#define USTORM_FCOE_PARAMS_B_CONT_INCR_SEQ_CNT_SHIFT 2
#define USTORM_FCOE_PARAMS_B_CONF_REQ (0x1<<3)
#define USTORM_FCOE_PARAMS_B_CONF_REQ_SHIFT 3
#define USTORM_FCOE_PARAMS_B_REC_VALID (0x1<<4)
#define USTORM_FCOE_PARAMS_B_REC_VALID_SHIFT 4
#define USTORM_FCOE_PARAMS_B_CQ_TOGGLE_BIT (0x1<<5)
#define USTORM_FCOE_PARAMS_B_CQ_TOGGLE_BIT_SHIFT 5
#define USTORM_FCOE_PARAMS_B_XFRQ_TOGGLE_BIT (0x1<<6)
#define USTORM_FCOE_PARAMS_B_XFRQ_TOGGLE_BIT_SHIFT 6
#define USTORM_FCOE_PARAMS_B_C2_VALID (0x1<<7)
#define USTORM_FCOE_PARAMS_B_C2_VALID_SHIFT 7
#define USTORM_FCOE_PARAMS_B_ACK_0 (0x1<<8)
#define USTORM_FCOE_PARAMS_B_ACK_0_SHIFT 8
#define USTORM_FCOE_PARAMS_RSRV0 (0x7F<<9)
#define USTORM_FCOE_PARAMS_RSRV0_SHIFT 9
#elif defined(__LITTLE_ENDIAN)
	u16 flags;
#define USTORM_FCOE_PARAMS_B_MUL_N_PORT_IDS (0x1<<0)
#define USTORM_FCOE_PARAMS_B_MUL_N_PORT_IDS_SHIFT 0
#define USTORM_FCOE_PARAMS_B_E_D_TOV_RES (0x1<<1)
#define USTORM_FCOE_PARAMS_B_E_D_TOV_RES_SHIFT 1
#define USTORM_FCOE_PARAMS_B_CONT_INCR_SEQ_CNT (0x1<<2)
#define USTORM_FCOE_PARAMS_B_CONT_INCR_SEQ_CNT_SHIFT 2
#define USTORM_FCOE_PARAMS_B_CONF_REQ (0x1<<3)
#define USTORM_FCOE_PARAMS_B_CONF_REQ_SHIFT 3
#define USTORM_FCOE_PARAMS_B_REC_VALID (0x1<<4)
#define USTORM_FCOE_PARAMS_B_REC_VALID_SHIFT 4
#define USTORM_FCOE_PARAMS_B_CQ_TOGGLE_BIT (0x1<<5)
#define USTORM_FCOE_PARAMS_B_CQ_TOGGLE_BIT_SHIFT 5
#define USTORM_FCOE_PARAMS_B_XFRQ_TOGGLE_BIT (0x1<<6)
#define USTORM_FCOE_PARAMS_B_XFRQ_TOGGLE_BIT_SHIFT 6
#define USTORM_FCOE_PARAMS_B_C2_VALID (0x1<<7)
#define USTORM_FCOE_PARAMS_B_C2_VALID_SHIFT 7
#define USTORM_FCOE_PARAMS_B_ACK_0 (0x1<<8)
#define USTORM_FCOE_PARAMS_B_ACK_0_SHIFT 8
#define USTORM_FCOE_PARAMS_RSRV0 (0x7F<<9)
#define USTORM_FCOE_PARAMS_RSRV0_SHIFT 9
	u16 fcoe_conn_id;
#endif
#if defined(__BIG_ENDIAN)
	u8 hc_csdm_byte_en;
	u8 func_id;
	u8 port_id;
	u8 vnic_id;
#elif defined(__LITTLE_ENDIAN)
	u8 vnic_id;
	u8 port_id;
	u8 func_id;
	u8 hc_csdm_byte_en;
#endif
#if defined(__BIG_ENDIAN)
	u16 rx_total_conc_seqs;
	u16 rx_max_fc_pay_len;
#elif defined(__LITTLE_ENDIAN)
	u16 rx_max_fc_pay_len;
	u16 rx_total_conc_seqs;
#endif
#if defined(__BIG_ENDIAN)
	u16 ox_id;
	u16 rx_max_conc_seqs;
#elif defined(__LITTLE_ENDIAN)
	u16 rx_max_conc_seqs;
	u16 ox_id;
#endif
};

/*
 * FCoE 16-bits index structure
 */
struct fcoe_idx16_fields {
	u16 fields;
#define FCOE_IDX16_FIELDS_IDX (0x7FFF<<0)
#define FCOE_IDX16_FIELDS_IDX_SHIFT 0
#define FCOE_IDX16_FIELDS_MSB (0x1<<15)
#define FCOE_IDX16_FIELDS_MSB_SHIFT 15
};

/*
 * FCoE 16-bits index union
 */
union fcoe_idx16_field_union {
	struct fcoe_idx16_fields fields;
	u16 val;
};

/*
 * 4 regs size
 */
struct fcoe_bd_ctx {
	u32 buf_addr_hi;
	u32 buf_addr_lo;
#if defined(__BIG_ENDIAN)
	u16 rsrv0;
	u16 buf_len;
#elif defined(__LITTLE_ENDIAN)
	u16 buf_len;
	u16 rsrv0;
#endif
#if defined(__BIG_ENDIAN)
	u16 rsrv1;
	u16 flags;
#elif defined(__LITTLE_ENDIAN)
	u16 flags;
	u16 rsrv1;
#endif
};

/*
 * Parameters required for placement according to SGL
 */
struct ustorm_fcoe_data_place {
#if defined(__BIG_ENDIAN)
	u16 cached_sge_off;
	u8 cached_num_sges;
	u8 cached_sge_idx;
#elif defined(__LITTLE_ENDIAN)
	u8 cached_sge_idx;
	u8 cached_num_sges;
	u16 cached_sge_off;
#endif
	struct fcoe_bd_ctx cached_sge[3];
};

struct fcoe_task_ctx_entry_txwr_rxrd {
#if defined(__BIG_ENDIAN)
	u16 verify_tx_seq;
	u8 init_flags;
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_TASK_TYPE (0x7<<0)
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_TASK_TYPE_SHIFT 0
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_DEV_TYPE (0x1<<3)
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_DEV_TYPE_SHIFT 3
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_CLASS_TYPE (0x1<<4)
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_CLASS_TYPE_SHIFT 4
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_SINGLE_SGE (0x1<<5)
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_SINGLE_SGE_SHIFT 5
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_RSRV5 (0x3<<6)
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_RSRV5_SHIFT 6
	u8 tx_flags;
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_TX_STATE (0xF<<0)
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_TX_STATE_SHIFT 0
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_RSRV4 (0xF<<4)
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_RSRV4_SHIFT 4
#elif defined(__LITTLE_ENDIAN)
	u8 tx_flags;
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_TX_STATE (0xF<<0)
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_TX_STATE_SHIFT 0
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_RSRV4 (0xF<<4)
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_RSRV4_SHIFT 4
	u8 init_flags;
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_TASK_TYPE (0x7<<0)
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_TASK_TYPE_SHIFT 0
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_DEV_TYPE (0x1<<3)
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_DEV_TYPE_SHIFT 3
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_CLASS_TYPE (0x1<<4)
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_CLASS_TYPE_SHIFT 4
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_SINGLE_SGE (0x1<<5)
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_SINGLE_SGE_SHIFT 5
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_RSRV5 (0x3<<6)
#define FCOE_TASK_CTX_ENTRY_TXWR_RXRD_RSRV5_SHIFT 6
	u16 verify_tx_seq;
#endif
};

struct fcoe_fcp_cmd_payload {
	u32 opaque[8];
};

struct fcoe_fc_hdr {
#if defined(__BIG_ENDIAN)
	u8 cs_ctl;
	u8 s_id[3];
#elif defined(__LITTLE_ENDIAN)
	u8 s_id[3];
	u8 cs_ctl;
#endif
#if defined(__BIG_ENDIAN)
	u8 r_ctl;
	u8 d_id[3];
#elif defined(__LITTLE_ENDIAN)
	u8 d_id[3];
	u8 r_ctl;
#endif
#if defined(__BIG_ENDIAN)
	u8 seq_id;
	u8 df_ctl;
	u16 seq_cnt;
#elif defined(__LITTLE_ENDIAN)
	u16 seq_cnt;
	u8 df_ctl;
	u8 seq_id;
#endif
#if defined(__BIG_ENDIAN)
	u8 type;
	u8 f_ctl[3];
#elif defined(__LITTLE_ENDIAN)
	u8 f_ctl[3];
	u8 type;
#endif
	u32 parameters;
#if defined(__BIG_ENDIAN)
	u16 ox_id;
	u16 rx_id;
#elif defined(__LITTLE_ENDIAN)
	u16 rx_id;
	u16 ox_id;
#endif
};

struct fcoe_fc_frame {
	struct fcoe_fc_hdr fc_hdr;
	u32 reserved0[2];
};

union fcoe_cmd_flow_info {
	struct fcoe_fcp_cmd_payload fcp_cmd_payload;
	struct fcoe_fc_frame mp_fc_frame;
};

struct fcoe_read_flow_info {
	struct fcoe_fc_hdr fc_data_in_hdr;
	u32 reserved[2];
};

struct fcoe_fcp_xfr_rdy_payload {
	u32 burst_len;
	u32 data_ro;
};

struct fcoe_write_flow_info {
	struct fcoe_fc_hdr fc_data_out_hdr;
	struct fcoe_fcp_xfr_rdy_payload fcp_xfr_payload;
};

struct fcoe_fcp_rsp_flags {
	u8 flags;
#define FCOE_FCP_RSP_FLAGS_FCP_RSP_LEN_VALID (0x1<<0)
#define FCOE_FCP_RSP_FLAGS_FCP_RSP_LEN_VALID_SHIFT 0
#define FCOE_FCP_RSP_FLAGS_FCP_SNS_LEN_VALID (0x1<<1)
#define FCOE_FCP_RSP_FLAGS_FCP_SNS_LEN_VALID_SHIFT 1
#define FCOE_FCP_RSP_FLAGS_FCP_RESID_OVER (0x1<<2)
#define FCOE_FCP_RSP_FLAGS_FCP_RESID_OVER_SHIFT 2
#define FCOE_FCP_RSP_FLAGS_FCP_RESID_UNDER (0x1<<3)
#define FCOE_FCP_RSP_FLAGS_FCP_RESID_UNDER_SHIFT 3
#define FCOE_FCP_RSP_FLAGS_FCP_CONF_REQ (0x1<<4)
#define FCOE_FCP_RSP_FLAGS_FCP_CONF_REQ_SHIFT 4
#define FCOE_FCP_RSP_FLAGS_FCP_BIDI_FLAGS (0x7<<5)
#define FCOE_FCP_RSP_FLAGS_FCP_BIDI_FLAGS_SHIFT 5
};

struct fcoe_fcp_rsp_payload {
	struct regpair reserved0;
	u32 fcp_resid;
#if defined(__BIG_ENDIAN)
	u16 retry_delay_timer;
	struct fcoe_fcp_rsp_flags fcp_flags;
	u8 scsi_status_code;
#elif defined(__LITTLE_ENDIAN)
	u8 scsi_status_code;
	struct fcoe_fcp_rsp_flags fcp_flags;
	u16 retry_delay_timer;
#endif
	u32 fcp_rsp_len;
	u32 fcp_sns_len;
};

/*
 * Fixed size structure in order to plant it in Union structure
 */
struct fcoe_fcp_rsp_union {
	struct fcoe_fcp_rsp_payload payload;
	struct regpair reserved0;
};

/*
 * Fixed size structure in order to plant it in Union structure
 */
struct fcoe_abts_rsp_union {
	u32 r_ctl;
	u32 abts_rsp_payload[7];
};

union fcoe_rsp_flow_info {
	struct fcoe_fcp_rsp_union fcp_rsp;
	struct fcoe_abts_rsp_union abts_rsp;
};

struct fcoe_cleanup_flow_info {
#if defined(__BIG_ENDIAN)
	u16 reserved1;
	u16 task_id;
#elif defined(__LITTLE_ENDIAN)
	u16 task_id;
	u16 reserved1;
#endif
	u32 reserved2[7];
};

/*
 * 32 bytes used for general purposes
 */
union fcoe_general_task_ctx {
	union fcoe_cmd_flow_info cmd_info;
	struct fcoe_read_flow_info read_info;
	struct fcoe_write_flow_info write_info;
	union fcoe_rsp_flow_info rsp_info;
	struct fcoe_cleanup_flow_info cleanup_info;
	u32 comp_info[8];
};

struct fcoe_s_stat_ctx {
	u8 flags;
#define FCOE_S_STAT_CTX_ACTIVE (0x1<<0)
#define FCOE_S_STAT_CTX_ACTIVE_SHIFT 0
#define FCOE_S_STAT_CTX_ACK_ABORT_SEQ_COND (0x1<<1)
#define FCOE_S_STAT_CTX_ACK_ABORT_SEQ_COND_SHIFT 1
#define FCOE_S_STAT_CTX_ABTS_PERFORMED (0x1<<2)
#define FCOE_S_STAT_CTX_ABTS_PERFORMED_SHIFT 2
#define FCOE_S_STAT_CTX_SEQ_TIMEOUT (0x1<<3)
#define FCOE_S_STAT_CTX_SEQ_TIMEOUT_SHIFT 3
#define FCOE_S_STAT_CTX_P_RJT (0x1<<4)
#define FCOE_S_STAT_CTX_P_RJT_SHIFT 4
#define FCOE_S_STAT_CTX_ACK_EOFT (0x1<<5)
#define FCOE_S_STAT_CTX_ACK_EOFT_SHIFT 5
#define FCOE_S_STAT_CTX_RSRV1 (0x3<<6)
#define FCOE_S_STAT_CTX_RSRV1_SHIFT 6
};

/*
 * Common section. Both TX and RX processing might write and read from it in different flows
 */
struct fcoe_task_ctx_entry_tx_rx_cmn {
	u32 data_2_trns;
	union fcoe_general_task_ctx general;
#if defined(__BIG_ENDIAN)
	u16 tx_low_seq_cnt;
	struct fcoe_s_stat_ctx tx_s_stat;
	u8 tx_seq_id;
#elif defined(__LITTLE_ENDIAN)
	u8 tx_seq_id;
	struct fcoe_s_stat_ctx tx_s_stat;
	u16 tx_low_seq_cnt;
#endif
	u32 common_flags;
#define FCOE_TASK_CTX_ENTRY_TX_RX_CMN_CID (0xFFFFFF<<0)
#define FCOE_TASK_CTX_ENTRY_TX_RX_CMN_CID_SHIFT 0
#define FCOE_TASK_CTX_ENTRY_TX_RX_CMN_VALID (0x1<<24)
#define FCOE_TASK_CTX_ENTRY_TX_RX_CMN_VALID_SHIFT 24
#define FCOE_TASK_CTX_ENTRY_TX_RX_CMN_SEQ_INIT (0x1<<25)
#define FCOE_TASK_CTX_ENTRY_TX_RX_CMN_SEQ_INIT_SHIFT 25
#define FCOE_TASK_CTX_ENTRY_TX_RX_CMN_PEND_XFER (0x1<<26)
#define FCOE_TASK_CTX_ENTRY_TX_RX_CMN_PEND_XFER_SHIFT 26
#define FCOE_TASK_CTX_ENTRY_TX_RX_CMN_PEND_CONF (0x1<<27)
#define FCOE_TASK_CTX_ENTRY_TX_RX_CMN_PEND_CONF_SHIFT 27
#define FCOE_TASK_CTX_ENTRY_TX_RX_CMN_EXP_FIRST_FRAME (0x1<<28)
#define FCOE_TASK_CTX_ENTRY_TX_RX_CMN_EXP_FIRST_FRAME_SHIFT 28
#define FCOE_TASK_CTX_ENTRY_TX_RX_CMN_RSRV (0x7<<29)
#define FCOE_TASK_CTX_ENTRY_TX_RX_CMN_RSRV_SHIFT 29
};

struct fcoe_task_ctx_entry_rxwr_txrd {
#if defined(__BIG_ENDIAN)
	u16 rx_id;
	u16 rx_flags;
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_RX_STATE (0xF<<0)
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_RX_STATE_SHIFT 0
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_NUM_RQ_WQE (0x7<<4)
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_NUM_RQ_WQE_SHIFT 4
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_CONF_REQ (0x1<<7)
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_CONF_REQ_SHIFT 7
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_MISS_FRAME (0x1<<8)
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_MISS_FRAME_SHIFT 8
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_RESERVED0 (0x7F<<9)
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_RESERVED0_SHIFT 9
#elif defined(__LITTLE_ENDIAN)
	u16 rx_flags;
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_RX_STATE (0xF<<0)
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_RX_STATE_SHIFT 0
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_NUM_RQ_WQE (0x7<<4)
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_NUM_RQ_WQE_SHIFT 4
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_CONF_REQ (0x1<<7)
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_CONF_REQ_SHIFT 7
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_MISS_FRAME (0x1<<8)
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_MISS_FRAME_SHIFT 8
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_RESERVED0 (0x7F<<9)
#define FCOE_TASK_CTX_ENTRY_RXWR_TXRD_RESERVED0_SHIFT 9
	u16 rx_id;
#endif
};

struct fcoe_seq_ctx {
#if defined(__BIG_ENDIAN)
	u16 low_seq_cnt;
	struct fcoe_s_stat_ctx s_stat;
	u8 seq_id;
#elif defined(__LITTLE_ENDIAN)
	u8 seq_id;
	struct fcoe_s_stat_ctx s_stat;
	u16 low_seq_cnt;
#endif
#if defined(__BIG_ENDIAN)
	u16 err_seq_cnt;
	u16 high_seq_cnt;
#elif defined(__LITTLE_ENDIAN)
	u16 high_seq_cnt;
	u16 err_seq_cnt;
#endif
	u32 low_exp_ro;
	u32 high_exp_ro;
};

struct fcoe_single_sge_ctx {
	struct regpair cur_buf_addr;
#if defined(__BIG_ENDIAN)
	u16 reserved0;
	u16 cur_buf_rem;
#elif defined(__LITTLE_ENDIAN)
	u16 cur_buf_rem;
	u16 reserved0;
#endif
};

struct fcoe_mul_sges_ctx {
	struct regpair cur_sge_addr;
#if defined(__BIG_ENDIAN)
	u8 sgl_size;
	u8 cur_sge_idx;
	u16 cur_sge_off;
#elif defined(__LITTLE_ENDIAN)
	u16 cur_sge_off;
	u8 cur_sge_idx;
	u8 sgl_size;
#endif
};

union fcoe_sgl_ctx {
	struct fcoe_single_sge_ctx single_sge;
	struct fcoe_mul_sges_ctx mul_sges;
};

struct fcoe_task_ctx_entry_rx_only {
	struct fcoe_seq_ctx seq_ctx;
	struct fcoe_seq_ctx ooo_seq_ctx;
	u32 rsrv3;
	union fcoe_sgl_ctx sgl_ctx;
};

struct ustorm_fcoe_task_ctx_entry_rd {
	struct fcoe_task_ctx_entry_txwr_rxrd tx_wr_rx_rd;
	struct fcoe_task_ctx_entry_tx_rx_cmn cmn;
	struct fcoe_task_ctx_entry_rxwr_txrd rx_wr_tx_rd;
	struct fcoe_task_ctx_entry_rx_only rx_wr;
	u32 reserved;
};

/*
 * Ustorm FCoE Storm Context
 */
struct ustorm_fcoe_st_context {
	struct ustorm_fcoe_params fcoe_params;
	struct regpair task_addr;
	struct regpair cq_base_addr;
	struct regpair rq_pbl_base;
	struct regpair rq_cur_page_addr;
	struct regpair confq_pbl_base_addr;
	struct regpair conn_db_base;
	struct regpair xfrq_base_addr;
	struct regpair lcq_base_addr;
#if defined(__BIG_ENDIAN)
	union fcoe_idx16_field_union rq_cons;
	union fcoe_idx16_field_union rq_prod;
#elif defined(__LITTLE_ENDIAN)
	union fcoe_idx16_field_union rq_prod;
	union fcoe_idx16_field_union rq_cons;
#endif
#if defined(__BIG_ENDIAN)
	u16 xfrq_prod;
	u16 cq_cons;
#elif defined(__LITTLE_ENDIAN)
	u16 cq_cons;
	u16 xfrq_prod;
#endif
#if defined(__BIG_ENDIAN)
	u16 lcq_cons;
	u16 hc_cram_address;
#elif defined(__LITTLE_ENDIAN)
	u16 hc_cram_address;
	u16 lcq_cons;
#endif
#if defined(__BIG_ENDIAN)
	u16 sq_xfrq_lcq_confq_size;
	u16 confq_prod;
#elif defined(__LITTLE_ENDIAN)
	u16 confq_prod;
	u16 sq_xfrq_lcq_confq_size;
#endif
#if defined(__BIG_ENDIAN)
	u8 hc_csdm_agg_int;
	u8 flags;
#define USTORM_FCOE_ST_CONTEXT_MID_SEQ_PROC_FLAG (0x1<<0)
#define USTORM_FCOE_ST_CONTEXT_MID_SEQ_PROC_FLAG_SHIFT 0
#define USTORM_FCOE_ST_CONTEXT_CACHED_CONN_FLAG (0x1<<1)
#define USTORM_FCOE_ST_CONTEXT_CACHED_CONN_FLAG_SHIFT 1
#define USTORM_FCOE_ST_CONTEXT_CACHED_TCE_FLAG (0x1<<2)
#define USTORM_FCOE_ST_CONTEXT_CACHED_TCE_FLAG_SHIFT 2
#define USTORM_FCOE_ST_CONTEXT_RSRV1 (0x1F<<3)
#define USTORM_FCOE_ST_CONTEXT_RSRV1_SHIFT 3
	u8 available_rqes;
	u8 sp_q_flush_cnt;
#elif defined(__LITTLE_ENDIAN)
	u8 sp_q_flush_cnt;
	u8 available_rqes;
	u8 flags;
#define USTORM_FCOE_ST_CONTEXT_MID_SEQ_PROC_FLAG (0x1<<0)
#define USTORM_FCOE_ST_CONTEXT_MID_SEQ_PROC_FLAG_SHIFT 0
#define USTORM_FCOE_ST_CONTEXT_CACHED_CONN_FLAG (0x1<<1)
#define USTORM_FCOE_ST_CONTEXT_CACHED_CONN_FLAG_SHIFT 1
#define USTORM_FCOE_ST_CONTEXT_CACHED_TCE_FLAG (0x1<<2)
#define USTORM_FCOE_ST_CONTEXT_CACHED_TCE_FLAG_SHIFT 2
#define USTORM_FCOE_ST_CONTEXT_RSRV1 (0x1F<<3)
#define USTORM_FCOE_ST_CONTEXT_RSRV1_SHIFT 3
	u8 hc_csdm_agg_int;
#endif
	struct ustorm_fcoe_data_place data_place;
	struct ustorm_fcoe_task_ctx_entry_rd tce;
};

/*
 * The FCoE non-aggregative context of Tstorm
 */
struct tstorm_fcoe_st_context {
	struct regpair reserved0;
	struct regpair reserved1;
};

/*
 * The fcoe aggregative context section of Xstorm
 */
struct xstorm_fcoe_extra_ag_context_section {
#if defined(__BIG_ENDIAN)
	u8 tcp_agg_vars1;
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED51 (0x3<<0)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED51_SHIFT 0
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED (0x3<<2)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_SHIFT 2
#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_PBF_TX_SEQ_ACK_CF (0x3<<4)
#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_PBF_TX_SEQ_ACK_CF_SHIFT 4
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_CLEAR_DA_TIMER_EN (0x1<<6)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_CLEAR_DA_TIMER_EN_SHIFT 6
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_DA_EXPIRATION_FLAG (0x1<<7)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_DA_EXPIRATION_FLAG_SHIFT 7
	u8 __reserved_da_cnt;
	u16 __mtu;
#elif defined(__LITTLE_ENDIAN)
	u16 __mtu;
	u8 __reserved_da_cnt;
	u8 tcp_agg_vars1;
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED51 (0x3<<0)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED51_SHIFT 0
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED (0x3<<2)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_SHIFT 2
#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_PBF_TX_SEQ_ACK_CF (0x3<<4)
#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_PBF_TX_SEQ_ACK_CF_SHIFT 4
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_CLEAR_DA_TIMER_EN (0x1<<6)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_CLEAR_DA_TIMER_EN_SHIFT 6
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_DA_EXPIRATION_FLAG (0x1<<7)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_DA_EXPIRATION_FLAG_SHIFT 7
#endif
	u32 __task_addr_lo;
	u32 __task_addr_hi;
	u32 __reserved55;
	u32 __tx_prods;
#if defined(__BIG_ENDIAN)
	u8 __agg_val8_th;
	u8 __agg_val8;
	u16 tcp_agg_vars2;
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED57 (0x1<<0)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED57_SHIFT 0
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED58 (0x1<<1)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED58_SHIFT 1
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED59 (0x1<<2)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED59_SHIFT 2
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX3_FLAG (0x1<<3)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX3_FLAG_SHIFT 3
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX4_FLAG (0x1<<4)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX4_FLAG_SHIFT 4
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED60 (0x1<<5)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED60_SHIFT 5
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_ACK_TO_FE_UPDATED_EN (0x1<<6)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_ACK_TO_FE_UPDATED_EN_SHIFT 6
#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_PBF_TX_SEQ_ACK_CF_EN (0x1<<7)
#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_PBF_TX_SEQ_ACK_CF_EN_SHIFT 7
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_TX_FIN_FLAG_EN (0x1<<8)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_TX_FIN_FLAG_EN_SHIFT 8
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG (0x1<<9)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT 9
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SET_RTO_CF (0x3<<10)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SET_RTO_CF_SHIFT 10
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF (0x3<<12)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF_SHIFT 12
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX8_CF (0x3<<14)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX8_CF_SHIFT 14
#elif defined(__LITTLE_ENDIAN)
	u16 tcp_agg_vars2;
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED57 (0x1<<0)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED57_SHIFT 0
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED58 (0x1<<1)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED58_SHIFT 1
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED59 (0x1<<2)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED59_SHIFT 2
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX3_FLAG (0x1<<3)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX3_FLAG_SHIFT 3
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX4_FLAG (0x1<<4)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX4_FLAG_SHIFT 4
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED60 (0x1<<5)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED60_SHIFT 5
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_ACK_TO_FE_UPDATED_EN (0x1<<6)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_ACK_TO_FE_UPDATED_EN_SHIFT 6
#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_PBF_TX_SEQ_ACK_CF_EN (0x1<<7)
#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_PBF_TX_SEQ_ACK_CF_EN_SHIFT 7
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_TX_FIN_FLAG_EN (0x1<<8)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_TX_FIN_FLAG_EN_SHIFT 8
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG (0x1<<9)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT 9
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SET_RTO_CF (0x3<<10)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SET_RTO_CF_SHIFT 10
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF (0x3<<12)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF_SHIFT 12
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX8_CF (0x3<<14)
#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX8_CF_SHIFT 14
	u8 __agg_val8;
	u8 __agg_val8_th;
#endif
	u32 __sq_base_addr_lo;
	u32 __sq_base_addr_hi;
	u32 __xfrq_base_addr_lo;
	u32 __xfrq_base_addr_hi;
#if defined(__BIG_ENDIAN)
	u16 __xfrq_cons;
	u16 __xfrq_prod;
#elif defined(__LITTLE_ENDIAN)
	u16 __xfrq_prod;
	u16 __xfrq_cons;
#endif
#if defined(__BIG_ENDIAN)
	u8 __tcp_agg_vars5;
	u8 __tcp_agg_vars4;
	u8 __tcp_agg_vars3;
	u8 __reserved_force_pure_ack_cnt;
#elif defined(__LITTLE_ENDIAN)
	u8 __reserved_force_pure_ack_cnt;
	u8 __tcp_agg_vars3;
	u8 __tcp_agg_vars4;
	u8 __tcp_agg_vars5;
#endif
	u32 __tcp_agg_vars6;
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
 * The fcoe aggregative context of Xstorm
 */
struct xstorm_fcoe_ag_context {
#if defined(__BIG_ENDIAN)
	u16 agg_val1;
	u8 agg_vars1;
#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0 (0x1<<0)
#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT 0
#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1 (0x1<<1)
#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT 1
#define __XSTORM_FCOE_AG_CONTEXT_RESERVED51 (0x1<<2)
#define __XSTORM_FCOE_AG_CONTEXT_RESERVED51_SHIFT 2
#define __XSTORM_FCOE_AG_CONTEXT_RESERVED52 (0x1<<3)
#define __XSTORM_FCOE_AG_CONTEXT_RESERVED52_SHIFT 3
#define __XSTORM_FCOE_AG_CONTEXT_MORE_TO_SEND_EN (0x1<<4)
#define __XSTORM_FCOE_AG_CONTEXT_MORE_TO_SEND_EN_SHIFT 4
#define XSTORM_FCOE_AG_CONTEXT_NAGLE_EN (0x1<<5)
#define XSTORM_FCOE_AG_CONTEXT_NAGLE_EN_SHIFT 5
#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG (0x1<<6)
#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_SHIFT 6
#define __XSTORM_FCOE_AG_CONTEXT_RESERVED_UNA_GT_NXT_EN (0x1<<7)
#define __XSTORM_FCOE_AG_CONTEXT_RESERVED_UNA_GT_NXT_EN_SHIFT 7
	u8 __state;
#elif defined(__LITTLE_ENDIAN)
	u8 __state;
	u8 agg_vars1;
#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0 (0x1<<0)
#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT 0
#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1 (0x1<<1)
#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT 1
#define __XSTORM_FCOE_AG_CONTEXT_RESERVED51 (0x1<<2)
#define __XSTORM_FCOE_AG_CONTEXT_RESERVED51_SHIFT 2
#define __XSTORM_FCOE_AG_CONTEXT_RESERVED52 (0x1<<3)
#define __XSTORM_FCOE_AG_CONTEXT_RESERVED52_SHIFT 3
#define __XSTORM_FCOE_AG_CONTEXT_MORE_TO_SEND_EN (0x1<<4)
#define __XSTORM_FCOE_AG_CONTEXT_MORE_TO_SEND_EN_SHIFT 4
#define XSTORM_FCOE_AG_CONTEXT_NAGLE_EN (0x1<<5)
#define XSTORM_FCOE_AG_CONTEXT_NAGLE_EN_SHIFT 5
#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG (0x1<<6)
#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_SHIFT 6
#define __XSTORM_FCOE_AG_CONTEXT_RESERVED_UNA_GT_NXT_EN (0x1<<7)
#define __XSTORM_FCOE_AG_CONTEXT_RESERVED_UNA_GT_NXT_EN_SHIFT 7
	u16 agg_val1;
#endif
#if defined(__BIG_ENDIAN)
	u8 cdu_reserved;
	u8 __agg_vars4;
	u8 agg_vars3;
#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2 (0x3F<<0)
#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2_SHIFT 0
#define __XSTORM_FCOE_AG_CONTEXT_AUX19_CF (0x3<<6)
#define __XSTORM_FCOE_AG_CONTEXT_AUX19_CF_SHIFT 6
	u8 agg_vars2;
#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF (0x3<<0)
#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_SHIFT 0
#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_EN (0x1<<2)
#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_EN_SHIFT 2
#define __XSTORM_FCOE_AG_CONTEXT_AUX8_FLAG (0x1<<3)
#define __XSTORM_FCOE_AG_CONTEXT_AUX8_FLAG_SHIFT 3
#define __XSTORM_FCOE_AG_CONTEXT_AUX9_FLAG (0x1<<4)
#define __XSTORM_FCOE_AG_CONTEXT_AUX9_FLAG_SHIFT 4
#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE1 (0x3<<5)
#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE1_SHIFT 5
#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_EN (0x1<<7)
#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_EN_SHIFT 7
#elif defined(__LITTLE_ENDIAN)
	u8 agg_vars2;
#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF (0x3<<0)
#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_SHIFT 0
#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_EN (0x1<<2)
#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_EN_SHIFT 2
#define __XSTORM_FCOE_AG_CONTEXT_AUX8_FLAG (0x1<<3)
#define __XSTORM_FCOE_AG_CONTEXT_AUX8_FLAG_SHIFT 3
#define __XSTORM_FCOE_AG_CONTEXT_AUX9_FLAG (0x1<<4)
#define __XSTORM_FCOE_AG_CONTEXT_AUX9_FLAG_SHIFT 4
#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE1 (0x3<<5)
#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE1_SHIFT 5
#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_EN (0x1<<7)
#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_EN_SHIFT 7
	u8 agg_vars3;
#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2 (0x3F<<0)
#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2_SHIFT 0
#define __XSTORM_FCOE_AG_CONTEXT_AUX19_CF (0x3<<6)
#define __XSTORM_FCOE_AG_CONTEXT_AUX19_CF_SHIFT 6
	u8 __agg_vars4;
	u8 cdu_reserved;
#endif
	u32 more_to_send;
#if defined(__BIG_ENDIAN)
	u16 agg_vars5;
#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE5 (0x3<<0)
#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE5_SHIFT 0
#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0 (0x3F<<2)
#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0_SHIFT 2
#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1 (0x3F<<8)
#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1_SHIFT 8
#define __XSTORM_FCOE_AG_CONTEXT_CONFQ_DEC_RULE (0x3<<14)
#define __XSTORM_FCOE_AG_CONTEXT_CONFQ_DEC_RULE_SHIFT 14
	u16 sq_cons;
#elif defined(__LITTLE_ENDIAN)
	u16 sq_cons;
	u16 agg_vars5;
#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE5 (0x3<<0)
#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE5_SHIFT 0
#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0 (0x3F<<2)
#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0_SHIFT 2
#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1 (0x3F<<8)
#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1_SHIFT 8
#define __XSTORM_FCOE_AG_CONTEXT_CONFQ_DEC_RULE (0x3<<14)
#define __XSTORM_FCOE_AG_CONTEXT_CONFQ_DEC_RULE_SHIFT 14
#endif
	struct xstorm_fcoe_extra_ag_context_section __extra_section;
#if defined(__BIG_ENDIAN)
	u16 agg_vars7;
#define __XSTORM_FCOE_AG_CONTEXT_AGG_VAL11_DECISION_RULE (0x7<<0)
#define __XSTORM_FCOE_AG_CONTEXT_AGG_VAL11_DECISION_RULE_SHIFT 0
#define __XSTORM_FCOE_AG_CONTEXT_AUX13_FLAG (0x1<<3)
#define __XSTORM_FCOE_AG_CONTEXT_AUX13_FLAG_SHIFT 3
#define __XSTORM_FCOE_AG_CONTEXT_QUEUE0_CF (0x3<<4)
#define __XSTORM_FCOE_AG_CONTEXT_QUEUE0_CF_SHIFT 4
#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE3 (0x3<<6)
#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE3_SHIFT 6
#define XSTORM_FCOE_AG_CONTEXT_AUX1_CF (0x3<<8)
#define XSTORM_FCOE_AG_CONTEXT_AUX1_CF_SHIFT 8
#define __XSTORM_FCOE_AG_CONTEXT_RESERVED62 (0x1<<10)
#define __XSTORM_FCOE_AG_CONTEXT_RESERVED62_SHIFT 10
#define __XSTORM_FCOE_AG_CONTEXT_AUX1_CF_EN (0x1<<11)
#define __XSTORM_FCOE_AG_CONTEXT_AUX1_CF_EN_SHIFT 11
#define __XSTORM_FCOE_AG_CONTEXT_AUX10_FLAG (0x1<<12)
#define __XSTORM_FCOE_AG_CONTEXT_AUX10_FLAG_SHIFT 12
#define __XSTORM_FCOE_AG_CONTEXT_AUX11_FLAG (0x1<<13)
#define __XSTORM_FCOE_AG_CONTEXT_AUX11_FLAG_SHIFT 13
#define __XSTORM_FCOE_AG_CONTEXT_AUX12_FLAG (0x1<<14)
#define __XSTORM_FCOE_AG_CONTEXT_AUX12_FLAG_SHIFT 14
#define __XSTORM_FCOE_AG_CONTEXT_AUX2_FLAG (0x1<<15)
#define __XSTORM_FCOE_AG_CONTEXT_AUX2_FLAG_SHIFT 15
	u8 agg_val3_th;
	u8 agg_vars6;
#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE6 (0x7<<0)
#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE6_SHIFT 0
#define __XSTORM_FCOE_AG_CONTEXT_XFRQ_DEC_RULE (0x7<<3)
#define __XSTORM_FCOE_AG_CONTEXT_XFRQ_DEC_RULE_SHIFT 3
#define __XSTORM_FCOE_AG_CONTEXT_SQ_DEC_RULE (0x3<<6)
#define __XSTORM_FCOE_AG_CONTEXT_SQ_DEC_RULE_SHIFT 6
#elif defined(__LITTLE_ENDIAN)
	u8 agg_vars6;
#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE6 (0x7<<0)
#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE6_SHIFT 0
#define __XSTORM_FCOE_AG_CONTEXT_XFRQ_DEC_RULE (0x7<<3)
#define __XSTORM_FCOE_AG_CONTEXT_XFRQ_DEC_RULE_SHIFT 3
#define __XSTORM_FCOE_AG_CONTEXT_SQ_DEC_RULE (0x3<<6)
#define __XSTORM_FCOE_AG_CONTEXT_SQ_DEC_RULE_SHIFT 6
	u8 agg_val3_th;
	u16 agg_vars7;
#define __XSTORM_FCOE_AG_CONTEXT_AGG_VAL11_DECISION_RULE (0x7<<0)
#define __XSTORM_FCOE_AG_CONTEXT_AGG_VAL11_DECISION_RULE_SHIFT 0
#define __XSTORM_FCOE_AG_CONTEXT_AUX13_FLAG (0x1<<3)
#define __XSTORM_FCOE_AG_CONTEXT_AUX13_FLAG_SHIFT 3
#define __XSTORM_FCOE_AG_CONTEXT_QUEUE0_CF (0x3<<4)
#define __XSTORM_FCOE_AG_CONTEXT_QUEUE0_CF_SHIFT 4
#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE3 (0x3<<6)
#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE3_SHIFT 6
#define XSTORM_FCOE_AG_CONTEXT_AUX1_CF (0x3<<8)
#define XSTORM_FCOE_AG_CONTEXT_AUX1_CF_SHIFT 8
#define __XSTORM_FCOE_AG_CONTEXT_RESERVED62 (0x1<<10)
#define __XSTORM_FCOE_AG_CONTEXT_RESERVED62_SHIFT 10
#define __XSTORM_FCOE_AG_CONTEXT_AUX1_CF_EN (0x1<<11)
#define __XSTORM_FCOE_AG_CONTEXT_AUX1_CF_EN_SHIFT 11
#define __XSTORM_FCOE_AG_CONTEXT_AUX10_FLAG (0x1<<12)
#define __XSTORM_FCOE_AG_CONTEXT_AUX10_FLAG_SHIFT 12
#define __XSTORM_FCOE_AG_CONTEXT_AUX11_FLAG (0x1<<13)
#define __XSTORM_FCOE_AG_CONTEXT_AUX11_FLAG_SHIFT 13
#define __XSTORM_FCOE_AG_CONTEXT_AUX12_FLAG (0x1<<14)
#define __XSTORM_FCOE_AG_CONTEXT_AUX12_FLAG_SHIFT 14
#define __XSTORM_FCOE_AG_CONTEXT_AUX2_FLAG (0x1<<15)
#define __XSTORM_FCOE_AG_CONTEXT_AUX2_FLAG_SHIFT 15
#endif
#if defined(__BIG_ENDIAN)
	u16 __agg_val11_th;
	u16 __agg_val11;
#elif defined(__LITTLE_ENDIAN)
	u16 __agg_val11;
	u16 __agg_val11_th;
#endif
#if defined(__BIG_ENDIAN)
	u8 __reserved1;
	u8 __agg_val6_th;
	u16 __confq_tx_prod;
#elif defined(__LITTLE_ENDIAN)
	u16 __confq_tx_prod;
	u8 __agg_val6_th;
	u8 __reserved1;
#endif
#if defined(__BIG_ENDIAN)
	u16 confq_cons;
	u16 confq_prod;
#elif defined(__LITTLE_ENDIAN)
	u16 confq_prod;
	u16 confq_cons;
#endif
	u32 agg_vars8;
#define __XSTORM_FCOE_AG_CONTEXT_CACHE_WQE_IDX (0xFFFFFF<<0)
#define __XSTORM_FCOE_AG_CONTEXT_CACHE_WQE_IDX_SHIFT 0
#define XSTORM_FCOE_AG_CONTEXT_AGG_MISC3 (0xFF<<24)
#define XSTORM_FCOE_AG_CONTEXT_AGG_MISC3_SHIFT 24
#if defined(__BIG_ENDIAN)
	u16 ox_id;
	u16 sq_prod;
#elif defined(__LITTLE_ENDIAN)
	u16 sq_prod;
	u16 ox_id;
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
	u16 __pbf_tx_seq_ack;
	u16 agg_limit1;
#elif defined(__LITTLE_ENDIAN)
	u16 agg_limit1;
	u16 __pbf_tx_seq_ack;
#endif
	u32 completion_seq;
	u32 confq_pbl_base_lo;
	u32 confq_pbl_base_hi;
};

/*
 * The fcoe extra aggregative context section of Tstorm
 */
struct tstorm_fcoe_extra_ag_context_section {
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
	u32 __lcq_prod;
	u32 rtt_seq;
	u32 rtt_time;
	u32 __reserved66;
	u32 wnd_right_edge;
	u32 tcp_agg_vars1;
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_FIN_SENT_FLAG (0x1<<0)
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_FIN_SENT_FLAG_SHIFT 0
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG (0x1<<1)
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG_SHIFT 1
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_WND_UPD_CF (0x3<<2)
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_WND_UPD_CF_SHIFT 2
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TIMEOUT_CF (0x3<<4)
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TIMEOUT_CF_SHIFT 4
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_WND_UPD_CF_EN (0x1<<6)
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_WND_UPD_CF_EN_SHIFT 6
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TIMEOUT_CF_EN (0x1<<7)
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TIMEOUT_CF_EN_SHIFT 7
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN (0x1<<8)
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN_SHIFT 8
#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_LCQ_SND_EN (0x1<<9)
#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_LCQ_SND_EN_SHIFT 9
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG (0x1<<10)
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT 10
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_FLAG (0x1<<11)
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_FLAG_SHIFT 11
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_CF_EN (0x1<<12)
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_CF_EN_SHIFT 12
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_CF_EN (0x1<<13)
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_CF_EN_SHIFT 13
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_CF (0x3<<14)
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_CF_SHIFT 14
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_CF (0x3<<16)
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_CF_SHIFT 16
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TX_BLOCKED (0x1<<18)
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TX_BLOCKED_SHIFT 18
#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX10_CF_EN (0x1<<19)
#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX10_CF_EN_SHIFT 19
#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX11_CF_EN (0x1<<20)
#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX11_CF_EN_SHIFT 20
#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX12_CF_EN (0x1<<21)
#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX12_CF_EN_SHIFT 21
#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED1 (0x3<<22)
#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED1_SHIFT 22
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ (0xF<<24)
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ_SHIFT 24
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ (0xF<<28)
#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ_SHIFT 28
	u32 snd_max;
	u32 __lcq_cons;
	u32 __reserved2;
};

/*
 * The fcoe aggregative context of Tstorm
 */
struct tstorm_fcoe_ag_context {
#if defined(__BIG_ENDIAN)
	u16 ulp_credit;
	u8 agg_vars1;
#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0 (0x1<<0)
#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT 0
#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1 (0x1<<1)
#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT 1
#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2 (0x1<<2)
#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2_SHIFT 2
#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3 (0x1<<3)
#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3_SHIFT 3
#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF (0x3<<4)
#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_SHIFT 4
#define __TSTORM_FCOE_AG_CONTEXT_AUX3_FLAG (0x1<<6)
#define __TSTORM_FCOE_AG_CONTEXT_AUX3_FLAG_SHIFT 6
#define __TSTORM_FCOE_AG_CONTEXT_AUX4_FLAG (0x1<<7)
#define __TSTORM_FCOE_AG_CONTEXT_AUX4_FLAG_SHIFT 7
	u8 state;
#elif defined(__LITTLE_ENDIAN)
	u8 state;
	u8 agg_vars1;
#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0 (0x1<<0)
#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT 0
#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1 (0x1<<1)
#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT 1
#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2 (0x1<<2)
#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2_SHIFT 2
#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3 (0x1<<3)
#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3_SHIFT 3
#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF (0x3<<4)
#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_SHIFT 4
#define __TSTORM_FCOE_AG_CONTEXT_AUX3_FLAG (0x1<<6)
#define __TSTORM_FCOE_AG_CONTEXT_AUX3_FLAG_SHIFT 6
#define __TSTORM_FCOE_AG_CONTEXT_AUX4_FLAG (0x1<<7)
#define __TSTORM_FCOE_AG_CONTEXT_AUX4_FLAG_SHIFT 7
	u16 ulp_credit;
#endif
#if defined(__BIG_ENDIAN)
	u16 __agg_val4;
	u16 agg_vars2;
#define __TSTORM_FCOE_AG_CONTEXT_AUX5_FLAG (0x1<<0)
#define __TSTORM_FCOE_AG_CONTEXT_AUX5_FLAG_SHIFT 0
#define __TSTORM_FCOE_AG_CONTEXT_AUX6_FLAG (0x1<<1)
#define __TSTORM_FCOE_AG_CONTEXT_AUX6_FLAG_SHIFT 1
#define __TSTORM_FCOE_AG_CONTEXT_AUX4_CF (0x3<<2)
#define __TSTORM_FCOE_AG_CONTEXT_AUX4_CF_SHIFT 2
#define __TSTORM_FCOE_AG_CONTEXT_AUX5_CF (0x3<<4)
#define __TSTORM_FCOE_AG_CONTEXT_AUX5_CF_SHIFT 4
#define __TSTORM_FCOE_AG_CONTEXT_AUX6_CF (0x3<<6)
#define __TSTORM_FCOE_AG_CONTEXT_AUX6_CF_SHIFT 6
#define __TSTORM_FCOE_AG_CONTEXT_AUX7_CF (0x3<<8)
#define __TSTORM_FCOE_AG_CONTEXT_AUX7_CF_SHIFT 8
#define __TSTORM_FCOE_AG_CONTEXT_AUX7_FLAG (0x1<<10)
#define __TSTORM_FCOE_AG_CONTEXT_AUX7_FLAG_SHIFT 10
#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_EN (0x1<<11)
#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_EN_SHIFT 11
#define TSTORM_FCOE_AG_CONTEXT_AUX4_CF_EN (0x1<<12)
#define TSTORM_FCOE_AG_CONTEXT_AUX4_CF_EN_SHIFT 12
#define TSTORM_FCOE_AG_CONTEXT_AUX5_CF_EN (0x1<<13)
#define TSTORM_FCOE_AG_CONTEXT_AUX5_CF_EN_SHIFT 13
#define TSTORM_FCOE_AG_CONTEXT_AUX6_CF_EN (0x1<<14)
#define TSTORM_FCOE_AG_CONTEXT_AUX6_CF_EN_SHIFT 14
#define TSTORM_FCOE_AG_CONTEXT_AUX7_CF_EN (0x1<<15)
#define TSTORM_FCOE_AG_CONTEXT_AUX7_CF_EN_SHIFT 15
#elif defined(__LITTLE_ENDIAN)
	u16 agg_vars2;
#define __TSTORM_FCOE_AG_CONTEXT_AUX5_FLAG (0x1<<0)
#define __TSTORM_FCOE_AG_CONTEXT_AUX5_FLAG_SHIFT 0
#define __TSTORM_FCOE_AG_CONTEXT_AUX6_FLAG (0x1<<1)
#define __TSTORM_FCOE_AG_CONTEXT_AUX6_FLAG_SHIFT 1
#define __TSTORM_FCOE_AG_CONTEXT_AUX4_CF (0x3<<2)
#define __TSTORM_FCOE_AG_CONTEXT_AUX4_CF_SHIFT 2
#define __TSTORM_FCOE_AG_CONTEXT_AUX5_CF (0x3<<4)
#define __TSTORM_FCOE_AG_CONTEXT_AUX5_CF_SHIFT 4
#define __TSTORM_FCOE_AG_CONTEXT_AUX6_CF (0x3<<6)
#define __TSTORM_FCOE_AG_CONTEXT_AUX6_CF_SHIFT 6
#define __TSTORM_FCOE_AG_CONTEXT_AUX7_CF (0x3<<8)
#define __TSTORM_FCOE_AG_CONTEXT_AUX7_CF_SHIFT 8
#define __TSTORM_FCOE_AG_CONTEXT_AUX7_FLAG (0x1<<10)
#define __TSTORM_FCOE_AG_CONTEXT_AUX7_FLAG_SHIFT 10
#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_EN (0x1<<11)
#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_EN_SHIFT 11
#define TSTORM_FCOE_AG_CONTEXT_AUX4_CF_EN (0x1<<12)
#define TSTORM_FCOE_AG_CONTEXT_AUX4_CF_EN_SHIFT 12
#define TSTORM_FCOE_AG_CONTEXT_AUX5_CF_EN (0x1<<13)
#define TSTORM_FCOE_AG_CONTEXT_AUX5_CF_EN_SHIFT 13
#define TSTORM_FCOE_AG_CONTEXT_AUX6_CF_EN (0x1<<14)
#define TSTORM_FCOE_AG_CONTEXT_AUX6_CF_EN_SHIFT 14
#define TSTORM_FCOE_AG_CONTEXT_AUX7_CF_EN (0x1<<15)
#define TSTORM_FCOE_AG_CONTEXT_AUX7_CF_EN_SHIFT 15
	u16 __agg_val4;
#endif
	struct tstorm_fcoe_extra_ag_context_section __extra_section;
};

/*
 * The fcoe aggregative context of Ustorm
 */
struct ustorm_fcoe_ag_context {
#if defined(__BIG_ENDIAN)
	u8 __aux_counter_flags;
	u8 agg_vars2;
#define USTORM_FCOE_AG_CONTEXT_TX_CF (0x3<<0)
#define USTORM_FCOE_AG_CONTEXT_TX_CF_SHIFT 0
#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF (0x3<<2)
#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_SHIFT 2
#define USTORM_FCOE_AG_CONTEXT_AGG_MISC4_RULE (0x7<<4)
#define USTORM_FCOE_AG_CONTEXT_AGG_MISC4_RULE_SHIFT 4
#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL2_MASK (0x1<<7)
#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL2_MASK_SHIFT 7
	u8 agg_vars1;
#define __USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0 (0x1<<0)
#define __USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT 0
#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1 (0x1<<1)
#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT 1
#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2 (0x1<<2)
#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2_SHIFT 2
#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3 (0x1<<3)
#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3_SHIFT 3
#define USTORM_FCOE_AG_CONTEXT_INV_CF (0x3<<4)
#define USTORM_FCOE_AG_CONTEXT_INV_CF_SHIFT 4
#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF (0x3<<6)
#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_SHIFT 6
	u8 state;
#elif defined(__LITTLE_ENDIAN)
	u8 state;
	u8 agg_vars1;
#define __USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0 (0x1<<0)
#define __USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT 0
#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1 (0x1<<1)
#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT 1
#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2 (0x1<<2)
#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2_SHIFT 2
#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3 (0x1<<3)
#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3_SHIFT 3
#define USTORM_FCOE_AG_CONTEXT_INV_CF (0x3<<4)
#define USTORM_FCOE_AG_CONTEXT_INV_CF_SHIFT 4
#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF (0x3<<6)
#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_SHIFT 6
	u8 agg_vars2;
#define USTORM_FCOE_AG_CONTEXT_TX_CF (0x3<<0)
#define USTORM_FCOE_AG_CONTEXT_TX_CF_SHIFT 0
#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF (0x3<<2)
#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_SHIFT 2
#define USTORM_FCOE_AG_CONTEXT_AGG_MISC4_RULE (0x7<<4)
#define USTORM_FCOE_AG_CONTEXT_AGG_MISC4_RULE_SHIFT 4
#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL2_MASK (0x1<<7)
#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL2_MASK_SHIFT 7
	u8 __aux_counter_flags;
#endif
#if defined(__BIG_ENDIAN)
	u8 cdu_usage;
	u8 agg_misc2;
	u16 pbf_tx_seq_ack;
#elif defined(__LITTLE_ENDIAN)
	u16 pbf_tx_seq_ack;
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
	u32 expired_task_id;
	u32 agg_misc4_th;
#if defined(__BIG_ENDIAN)
	u16 cq_prod;
	u16 cq_cons;
#elif defined(__LITTLE_ENDIAN)
	u16 cq_cons;
	u16 cq_prod;
#endif
#if defined(__BIG_ENDIAN)
	u16 __reserved2;
	u8 decision_rules;
#define USTORM_FCOE_AG_CONTEXT_CQ_DEC_RULE (0x7<<0)
#define USTORM_FCOE_AG_CONTEXT_CQ_DEC_RULE_SHIFT 0
#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL3_RULE (0x7<<3)
#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL3_RULE_SHIFT 3
#define USTORM_FCOE_AG_CONTEXT_CQ_ARM_N_FLAG (0x1<<6)
#define USTORM_FCOE_AG_CONTEXT_CQ_ARM_N_FLAG_SHIFT 6
#define __USTORM_FCOE_AG_CONTEXT_RESERVED1 (0x1<<7)
#define __USTORM_FCOE_AG_CONTEXT_RESERVED1_SHIFT 7
	u8 decision_rule_enable_bits;
#define __USTORM_FCOE_AG_CONTEXT_RESERVED_INV_CF_EN (0x1<<0)
#define __USTORM_FCOE_AG_CONTEXT_RESERVED_INV_CF_EN_SHIFT 0
#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_EN (0x1<<1)
#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_EN_SHIFT 1
#define USTORM_FCOE_AG_CONTEXT_TX_CF_EN (0x1<<2)
#define USTORM_FCOE_AG_CONTEXT_TX_CF_EN_SHIFT 2
#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_EN (0x1<<3)
#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_EN_SHIFT 3
#define __USTORM_FCOE_AG_CONTEXT_AUX1_CF_EN (0x1<<4)
#define __USTORM_FCOE_AG_CONTEXT_AUX1_CF_EN_SHIFT 4
#define __USTORM_FCOE_AG_CONTEXT_QUEUE0_CF_EN (0x1<<5)
#define __USTORM_FCOE_AG_CONTEXT_QUEUE0_CF_EN_SHIFT 5
#define __USTORM_FCOE_AG_CONTEXT_AUX3_CF_EN (0x1<<6)
#define __USTORM_FCOE_AG_CONTEXT_AUX3_CF_EN_SHIFT 6
#define __USTORM_FCOE_AG_CONTEXT_DQ_CF_EN (0x1<<7)
#define __USTORM_FCOE_AG_CONTEXT_DQ_CF_EN_SHIFT 7
#elif defined(__LITTLE_ENDIAN)
	u8 decision_rule_enable_bits;
#define __USTORM_FCOE_AG_CONTEXT_RESERVED_INV_CF_EN (0x1<<0)
#define __USTORM_FCOE_AG_CONTEXT_RESERVED_INV_CF_EN_SHIFT 0
#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_EN (0x1<<1)
#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_EN_SHIFT 1
#define USTORM_FCOE_AG_CONTEXT_TX_CF_EN (0x1<<2)
#define USTORM_FCOE_AG_CONTEXT_TX_CF_EN_SHIFT 2
#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_EN (0x1<<3)
#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_EN_SHIFT 3
#define __USTORM_FCOE_AG_CONTEXT_AUX1_CF_EN (0x1<<4)
#define __USTORM_FCOE_AG_CONTEXT_AUX1_CF_EN_SHIFT 4
#define __USTORM_FCOE_AG_CONTEXT_QUEUE0_CF_EN (0x1<<5)
#define __USTORM_FCOE_AG_CONTEXT_QUEUE0_CF_EN_SHIFT 5
#define __USTORM_FCOE_AG_CONTEXT_AUX3_CF_EN (0x1<<6)
#define __USTORM_FCOE_AG_CONTEXT_AUX3_CF_EN_SHIFT 6
#define __USTORM_FCOE_AG_CONTEXT_DQ_CF_EN (0x1<<7)
#define __USTORM_FCOE_AG_CONTEXT_DQ_CF_EN_SHIFT 7
	u8 decision_rules;
#define USTORM_FCOE_AG_CONTEXT_CQ_DEC_RULE (0x7<<0)
#define USTORM_FCOE_AG_CONTEXT_CQ_DEC_RULE_SHIFT 0
#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL3_RULE (0x7<<3)
#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL3_RULE_SHIFT 3
#define USTORM_FCOE_AG_CONTEXT_CQ_ARM_N_FLAG (0x1<<6)
#define USTORM_FCOE_AG_CONTEXT_CQ_ARM_N_FLAG_SHIFT 6
#define __USTORM_FCOE_AG_CONTEXT_RESERVED1 (0x1<<7)
#define __USTORM_FCOE_AG_CONTEXT_RESERVED1_SHIFT 7
	u16 __reserved2;
#endif
};

/*
 * Ethernet context section
 */
struct xstorm_fcoe_eth_context_section {
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
#define XSTORM_FCOE_ETH_CONTEXT_SECTION_VLAN_ID (0xFFF<<0)
#define XSTORM_FCOE_ETH_CONTEXT_SECTION_VLAN_ID_SHIFT 0
#define XSTORM_FCOE_ETH_CONTEXT_SECTION_CFI (0x1<<12)
#define XSTORM_FCOE_ETH_CONTEXT_SECTION_CFI_SHIFT 12
#define XSTORM_FCOE_ETH_CONTEXT_SECTION_PRIORITY (0x7<<13)
#define XSTORM_FCOE_ETH_CONTEXT_SECTION_PRIORITY_SHIFT 13
#elif defined(__LITTLE_ENDIAN)
	u16 params;
#define XSTORM_FCOE_ETH_CONTEXT_SECTION_VLAN_ID (0xFFF<<0)
#define XSTORM_FCOE_ETH_CONTEXT_SECTION_VLAN_ID_SHIFT 0
#define XSTORM_FCOE_ETH_CONTEXT_SECTION_CFI (0x1<<12)
#define XSTORM_FCOE_ETH_CONTEXT_SECTION_CFI_SHIFT 12
#define XSTORM_FCOE_ETH_CONTEXT_SECTION_PRIORITY (0x7<<13)
#define XSTORM_FCOE_ETH_CONTEXT_SECTION_PRIORITY_SHIFT 13
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
 * Flags used in FCoE context section - 1 byte
 */
struct xstorm_fcoe_context_flags {
	u8 flags;
#define XSTORM_FCOE_CONTEXT_FLAGS_B_PROC_Q (0x3<<0)
#define XSTORM_FCOE_CONTEXT_FLAGS_B_PROC_Q_SHIFT 0
#define XSTORM_FCOE_CONTEXT_FLAGS_B_MID_SEQ (0x1<<2)
#define XSTORM_FCOE_CONTEXT_FLAGS_B_MID_SEQ_SHIFT 2
#define XSTORM_FCOE_CONTEXT_FLAGS_B_EXCHANGE_CLEANUP_DEFFERED (0x1<<3)
#define XSTORM_FCOE_CONTEXT_FLAGS_B_EXCHANGE_CLEANUP_DEFFERED_SHIFT 3
#define XSTORM_FCOE_CONTEXT_FLAGS_B_REC_SUPPORT (0x1<<4)
#define XSTORM_FCOE_CONTEXT_FLAGS_B_REC_SUPPORT_SHIFT 4
#define XSTORM_FCOE_CONTEXT_FLAGS_B_SQ_TOGGLE (0x1<<5)
#define XSTORM_FCOE_CONTEXT_FLAGS_B_SQ_TOGGLE_SHIFT 5
#define XSTORM_FCOE_CONTEXT_FLAGS_B_XFRQ_TOGGLE (0x1<<6)
#define XSTORM_FCOE_CONTEXT_FLAGS_B_XFRQ_TOGGLE_SHIFT 6
#define XSTORM_FCOE_CONTEXT_FLAGS_B_ABTS_DEFFERED (0x1<<7)
#define XSTORM_FCOE_CONTEXT_FLAGS_B_ABTS_DEFFERED_SHIFT 7
};

/*
 * FCoE SQ element
 */
struct fcoe_sqe {
	u16 wqe;
#define FCOE_SQE_TASK_ID (0x7FFF<<0)
#define FCOE_SQE_TASK_ID_SHIFT 0
#define FCOE_SQE_TOGGLE_BIT (0x1<<15)
#define FCOE_SQE_TOGGLE_BIT_SHIFT 15
};

/*
 * FCoE XFRQ element
 */
struct fcoe_xfrqe {
	u16 wqe;
#define FCOE_XFRQE_TASK_ID (0x7FFF<<0)
#define FCOE_XFRQE_TASK_ID_SHIFT 0
#define FCOE_XFRQE_TOGGLE_BIT (0x1<<15)
#define FCOE_XFRQE_TOGGLE_BIT_SHIFT 15
};

/*
 * FCoE SQ\XFRQ element
 */
struct fcoe_cached_wqe {
#if defined(__BIG_ENDIAN)
	struct fcoe_xfrqe xfrqe;
	struct fcoe_sqe sqe;
#elif defined(__LITTLE_ENDIAN)
	struct fcoe_sqe sqe;
	struct fcoe_xfrqe xfrqe;
#endif
};

struct fcoe_task_ctx_entry_tx_only {
	union fcoe_sgl_ctx sgl_ctx;
};

struct xstorm_fcoe_task_ctx_entry_rd {
	struct fcoe_task_ctx_entry_tx_only tx_wr;
	struct fcoe_task_ctx_entry_txwr_rxrd tx_wr_rx_rd;
	struct fcoe_task_ctx_entry_tx_rx_cmn cmn;
	struct fcoe_task_ctx_entry_rxwr_txrd rx_wr_tx_rd;
};

/*
 * Cached SGEs
 */
struct common_fcoe_sgl {
	struct fcoe_bd_ctx sge[2];
};

/*
 * FCP_DATA parameters required for transmission
 */
struct xstorm_fcoe_fcp_data {
	u32 io_rem;
#if defined(__BIG_ENDIAN)
	u16 cached_sge_off;
	u8 cached_num_sges;
	u8 cached_sge_idx;
#elif defined(__LITTLE_ENDIAN)
	u8 cached_sge_idx;
	u8 cached_num_sges;
	u16 cached_sge_off;
#endif
	struct common_fcoe_sgl cached_sgl;
};

/*
 * FCoE context section
 */
struct xstorm_fcoe_context_section {
#if defined(__BIG_ENDIAN)
	u8 vlan_flag;
	u8 s_id[3];
#elif defined(__LITTLE_ENDIAN)
	u8 s_id[3];
	u8 vlan_flag;
#endif
#if defined(__BIG_ENDIAN)
	u8 func_id;
	u8 d_id[3];
#elif defined(__LITTLE_ENDIAN)
	u8 d_id[3];
	u8 func_id;
#endif
#if defined(__BIG_ENDIAN)
	u16 sq_xfrq_lcq_confq_size;
	u16 tx_max_fc_pay_len;
#elif defined(__LITTLE_ENDIAN)
	u16 tx_max_fc_pay_len;
	u16 sq_xfrq_lcq_confq_size;
#endif
	u32 lcq_prod;
#if defined(__BIG_ENDIAN)
	u8 port_id;
	u8 tx_max_conc_seqs_c3;
	u8 seq_id;
	struct xstorm_fcoe_context_flags tx_flags;
#elif defined(__LITTLE_ENDIAN)
	struct xstorm_fcoe_context_flags tx_flags;
	u8 seq_id;
	u8 tx_max_conc_seqs_c3;
	u8 port_id;
#endif
#if defined(__BIG_ENDIAN)
	u16 verify_tx_seq;
	u8 func_mode;
	u8 vnic_id;
#elif defined(__LITTLE_ENDIAN)
	u8 vnic_id;
	u8 func_mode;
	u16 verify_tx_seq;
#endif
	struct regpair confq_curr_page_addr;
	struct fcoe_cached_wqe cached_wqe[8];
	struct regpair lcq_base_addr;
	struct xstorm_fcoe_task_ctx_entry_rd tce;
	struct xstorm_fcoe_fcp_data fcp_data;
#if defined(__BIG_ENDIAN)
	u16 fcoe_tx_stat_params_ram_addr;
	u16 cmng_port_ram_addr;
#elif defined(__LITTLE_ENDIAN)
	u16 cmng_port_ram_addr;
	u16 fcoe_tx_stat_params_ram_addr;
#endif
#if defined(__BIG_ENDIAN)
	u8 fcp_cmd_pb_cmd_size;
	u8 eth_hdr_size;
	u16 pbf_addr;
#elif defined(__LITTLE_ENDIAN)
	u16 pbf_addr;
	u8 eth_hdr_size;
	u8 fcp_cmd_pb_cmd_size;
#endif
#if defined(__BIG_ENDIAN)
	u8 reserved2[2];
	u8 cos;
	u8 dcb_version;
#elif defined(__LITTLE_ENDIAN)
	u8 dcb_version;
	u8 cos;
	u8 reserved2[2];
#endif
	u32 reserved3;
	struct regpair reserved4[2];
};

/*
 * Xstorm FCoE Storm Context
 */
struct xstorm_fcoe_st_context {
	struct xstorm_fcoe_eth_context_section eth;
	struct xstorm_fcoe_context_section fcoe;
};

/*
 * Fcoe connection context
 */
struct fcoe_context {
	struct ustorm_fcoe_st_context ustorm_st_context;
	struct tstorm_fcoe_st_context tstorm_st_context;
	struct xstorm_fcoe_ag_context xstorm_ag_context;
	struct tstorm_fcoe_ag_context tstorm_ag_context;
	struct ustorm_fcoe_ag_context ustorm_ag_context;
	struct timers_block_context timers_context;
	struct xstorm_fcoe_st_context xstorm_st_context;
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
 * FCoE KCQ CQE parameters
 */
union fcoe_kcqe_params {
	u32 reserved0[4];
};

/*
 * FCoE KCQ CQE
 */
struct fcoe_kcqe {
	u32 fcoe_conn_id;
	u32 completion_status;
	u32 fcoe_conn_context_id;
	union fcoe_kcqe_params params;
#if defined(__BIG_ENDIAN)
	u8 flags;
#define FCOE_KCQE_RESERVED0 (0x7<<0)
#define FCOE_KCQE_RESERVED0_SHIFT 0
#define FCOE_KCQE_RAMROD_COMPLETION (0x1<<3)
#define FCOE_KCQE_RAMROD_COMPLETION_SHIFT 3
#define FCOE_KCQE_LAYER_CODE (0x7<<4)
#define FCOE_KCQE_LAYER_CODE_SHIFT 4
#define FCOE_KCQE_LINKED_WITH_NEXT (0x1<<7)
#define FCOE_KCQE_LINKED_WITH_NEXT_SHIFT 7
	u8 op_code;
	u16 qe_self_seq;
#elif defined(__LITTLE_ENDIAN)
	u16 qe_self_seq;
	u8 op_code;
	u8 flags;
#define FCOE_KCQE_RESERVED0 (0x7<<0)
#define FCOE_KCQE_RESERVED0_SHIFT 0
#define FCOE_KCQE_RAMROD_COMPLETION (0x1<<3)
#define FCOE_KCQE_RAMROD_COMPLETION_SHIFT 3
#define FCOE_KCQE_LAYER_CODE (0x7<<4)
#define FCOE_KCQE_LAYER_CODE_SHIFT 4
#define FCOE_KCQE_LINKED_WITH_NEXT (0x1<<7)
#define FCOE_KCQE_LINKED_WITH_NEXT_SHIFT 7
#endif
};

/*
 * FCoE KWQE header
 */
struct fcoe_kwqe_header {
#if defined(__BIG_ENDIAN)
	u8 flags;
#define FCOE_KWQE_HEADER_RESERVED0 (0xF<<0)
#define FCOE_KWQE_HEADER_RESERVED0_SHIFT 0
#define FCOE_KWQE_HEADER_LAYER_CODE (0x7<<4)
#define FCOE_KWQE_HEADER_LAYER_CODE_SHIFT 4
#define FCOE_KWQE_HEADER_RESERVED1 (0x1<<7)
#define FCOE_KWQE_HEADER_RESERVED1_SHIFT 7
	u8 op_code;
#elif defined(__LITTLE_ENDIAN)
	u8 op_code;
	u8 flags;
#define FCOE_KWQE_HEADER_RESERVED0 (0xF<<0)
#define FCOE_KWQE_HEADER_RESERVED0_SHIFT 0
#define FCOE_KWQE_HEADER_LAYER_CODE (0x7<<4)
#define FCOE_KWQE_HEADER_LAYER_CODE_SHIFT 4
#define FCOE_KWQE_HEADER_RESERVED1 (0x1<<7)
#define FCOE_KWQE_HEADER_RESERVED1_SHIFT 7
#endif
};

/*
 * FCoE firmware init request 1
 */
struct fcoe_kwqe_init1 {
#if defined(__BIG_ENDIAN)
	struct fcoe_kwqe_header hdr;
	u16 num_tasks;
#elif defined(__LITTLE_ENDIAN)
	u16 num_tasks;
	struct fcoe_kwqe_header hdr;
#endif
	u32 task_list_pbl_addr_lo;
	u32 task_list_pbl_addr_hi;
	u32 dummy_buffer_addr_lo;
	u32 dummy_buffer_addr_hi;
#if defined(__BIG_ENDIAN)
	u16 rq_num_wqes;
	u16 sq_num_wqes;
#elif defined(__LITTLE_ENDIAN)
	u16 sq_num_wqes;
	u16 rq_num_wqes;
#endif
#if defined(__BIG_ENDIAN)
	u16 cq_num_wqes;
	u16 rq_buffer_log_size;
#elif defined(__LITTLE_ENDIAN)
	u16 rq_buffer_log_size;
	u16 cq_num_wqes;
#endif
#if defined(__BIG_ENDIAN)
	u8 flags;
#define FCOE_KWQE_INIT1_LOG_PAGE_SIZE (0xF<<0)
#define FCOE_KWQE_INIT1_LOG_PAGE_SIZE_SHIFT 0
#define FCOE_KWQE_INIT1_LOG_CACHED_PBES_PER_FUNC (0x7<<4)
#define FCOE_KWQE_INIT1_LOG_CACHED_PBES_PER_FUNC_SHIFT 4
#define FCOE_KWQE_INIT1_RESERVED1 (0x1<<7)
#define FCOE_KWQE_INIT1_RESERVED1_SHIFT 7
	u8 num_sessions_log;
	u16 mtu;
#elif defined(__LITTLE_ENDIAN)
	u16 mtu;
	u8 num_sessions_log;
	u8 flags;
#define FCOE_KWQE_INIT1_LOG_PAGE_SIZE (0xF<<0)
#define FCOE_KWQE_INIT1_LOG_PAGE_SIZE_SHIFT 0
#define FCOE_KWQE_INIT1_LOG_CACHED_PBES_PER_FUNC (0x7<<4)
#define FCOE_KWQE_INIT1_LOG_CACHED_PBES_PER_FUNC_SHIFT 4
#define FCOE_KWQE_INIT1_RESERVED1 (0x1<<7)
#define FCOE_KWQE_INIT1_RESERVED1_SHIFT 7
#endif
};

/*
 * FCoE firmware init request 2
 */
struct fcoe_kwqe_init2 {
#if defined(__BIG_ENDIAN)
	struct fcoe_kwqe_header hdr;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	struct fcoe_kwqe_header hdr;
#endif
	u32 hash_tbl_pbl_addr_lo;
	u32 hash_tbl_pbl_addr_hi;
	u32 t2_hash_tbl_addr_lo;
	u32 t2_hash_tbl_addr_hi;
	u32 t2_ptr_hash_tbl_addr_lo;
	u32 t2_ptr_hash_tbl_addr_hi;
	u32 free_list_count;
};

/*
 * FCoE firmware init request 3
 */
struct fcoe_kwqe_init3 {
#if defined(__BIG_ENDIAN)
	struct fcoe_kwqe_header hdr;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	struct fcoe_kwqe_header hdr;
#endif
	u32 error_bit_map_lo;
	u32 error_bit_map_hi;
#if defined(__BIG_ENDIAN)
	u8 reserved21[3];
	u8 cached_session_enable;
#elif defined(__LITTLE_ENDIAN)
	u8 cached_session_enable;
	u8 reserved21[3];
#endif
	u32 reserved2[4];
};

/*
 * FCoE connection offload request 1
 */
struct fcoe_kwqe_conn_offload1 {
#if defined(__BIG_ENDIAN)
	struct fcoe_kwqe_header hdr;
	u16 fcoe_conn_id;
#elif defined(__LITTLE_ENDIAN)
	u16 fcoe_conn_id;
	struct fcoe_kwqe_header hdr;
#endif
	u32 sq_addr_lo;
	u32 sq_addr_hi;
	u32 rq_pbl_addr_lo;
	u32 rq_pbl_addr_hi;
	u32 rq_first_pbe_addr_lo;
	u32 rq_first_pbe_addr_hi;
#if defined(__BIG_ENDIAN)
	u16 reserved0;
	u16 rq_prod;
#elif defined(__LITTLE_ENDIAN)
	u16 rq_prod;
	u16 reserved0;
#endif
};

/*
 * FCoE connection offload request 2
 */
struct fcoe_kwqe_conn_offload2 {
#if defined(__BIG_ENDIAN)
	struct fcoe_kwqe_header hdr;
	u16 tx_max_fc_pay_len;
#elif defined(__LITTLE_ENDIAN)
	u16 tx_max_fc_pay_len;
	struct fcoe_kwqe_header hdr;
#endif
	u32 cq_addr_lo;
	u32 cq_addr_hi;
	u32 xferq_addr_lo;
	u32 xferq_addr_hi;
	u32 conn_db_addr_lo;
	u32 conn_db_addr_hi;
	u32 reserved1;
};

/*
 * FCoE connection offload request 3
 */
struct fcoe_kwqe_conn_offload3 {
#if defined(__BIG_ENDIAN)
	struct fcoe_kwqe_header hdr;
	u16 vlan_tag;
#define FCOE_KWQE_CONN_OFFLOAD3_VLAN_ID (0xFFF<<0)
#define FCOE_KWQE_CONN_OFFLOAD3_VLAN_ID_SHIFT 0
#define FCOE_KWQE_CONN_OFFLOAD3_CFI (0x1<<12)
#define FCOE_KWQE_CONN_OFFLOAD3_CFI_SHIFT 12
#define FCOE_KWQE_CONN_OFFLOAD3_PRIORITY (0x7<<13)
#define FCOE_KWQE_CONN_OFFLOAD3_PRIORITY_SHIFT 13
#elif defined(__LITTLE_ENDIAN)
	u16 vlan_tag;
#define FCOE_KWQE_CONN_OFFLOAD3_VLAN_ID (0xFFF<<0)
#define FCOE_KWQE_CONN_OFFLOAD3_VLAN_ID_SHIFT 0
#define FCOE_KWQE_CONN_OFFLOAD3_CFI (0x1<<12)
#define FCOE_KWQE_CONN_OFFLOAD3_CFI_SHIFT 12
#define FCOE_KWQE_CONN_OFFLOAD3_PRIORITY (0x7<<13)
#define FCOE_KWQE_CONN_OFFLOAD3_PRIORITY_SHIFT 13
	struct fcoe_kwqe_header hdr;
#endif
#if defined(__BIG_ENDIAN)
	u8 tx_max_conc_seqs_c3;
	u8 s_id[3];
#elif defined(__LITTLE_ENDIAN)
	u8 s_id[3];
	u8 tx_max_conc_seqs_c3;
#endif
#if defined(__BIG_ENDIAN)
	u8 flags;
#define FCOE_KWQE_CONN_OFFLOAD3_B_MUL_N_PORT_IDS (0x1<<0)
#define FCOE_KWQE_CONN_OFFLOAD3_B_MUL_N_PORT_IDS_SHIFT 0
#define FCOE_KWQE_CONN_OFFLOAD3_B_E_D_TOV_RES (0x1<<1)
#define FCOE_KWQE_CONN_OFFLOAD3_B_E_D_TOV_RES_SHIFT 1
#define FCOE_KWQE_CONN_OFFLOAD3_B_CONT_INCR_SEQ_CNT (0x1<<2)
#define FCOE_KWQE_CONN_OFFLOAD3_B_CONT_INCR_SEQ_CNT_SHIFT 2
#define FCOE_KWQE_CONN_OFFLOAD3_B_CONF_REQ (0x1<<3)
#define FCOE_KWQE_CONN_OFFLOAD3_B_CONF_REQ_SHIFT 3
#define FCOE_KWQE_CONN_OFFLOAD3_B_REC_VALID (0x1<<4)
#define FCOE_KWQE_CONN_OFFLOAD3_B_REC_VALID_SHIFT 4
#define FCOE_KWQE_CONN_OFFLOAD3_B_C2_VALID (0x1<<5)
#define FCOE_KWQE_CONN_OFFLOAD3_B_C2_VALID_SHIFT 5
#define FCOE_KWQE_CONN_OFFLOAD3_B_ACK_0 (0x1<<6)
#define FCOE_KWQE_CONN_OFFLOAD3_B_ACK_0_SHIFT 6
#define FCOE_KWQE_CONN_OFFLOAD3_B_VLAN_FLAG (0x1<<7)
#define FCOE_KWQE_CONN_OFFLOAD3_B_VLAN_FLAG_SHIFT 7
	u8 d_id[3];
#elif defined(__LITTLE_ENDIAN)
	u8 d_id[3];
	u8 flags;
#define FCOE_KWQE_CONN_OFFLOAD3_B_MUL_N_PORT_IDS (0x1<<0)
#define FCOE_KWQE_CONN_OFFLOAD3_B_MUL_N_PORT_IDS_SHIFT 0
#define FCOE_KWQE_CONN_OFFLOAD3_B_E_D_TOV_RES (0x1<<1)
#define FCOE_KWQE_CONN_OFFLOAD3_B_E_D_TOV_RES_SHIFT 1
#define FCOE_KWQE_CONN_OFFLOAD3_B_CONT_INCR_SEQ_CNT (0x1<<2)
#define FCOE_KWQE_CONN_OFFLOAD3_B_CONT_INCR_SEQ_CNT_SHIFT 2
#define FCOE_KWQE_CONN_OFFLOAD3_B_CONF_REQ (0x1<<3)
#define FCOE_KWQE_CONN_OFFLOAD3_B_CONF_REQ_SHIFT 3
#define FCOE_KWQE_CONN_OFFLOAD3_B_REC_VALID (0x1<<4)
#define FCOE_KWQE_CONN_OFFLOAD3_B_REC_VALID_SHIFT 4
#define FCOE_KWQE_CONN_OFFLOAD3_B_C2_VALID (0x1<<5)
#define FCOE_KWQE_CONN_OFFLOAD3_B_C2_VALID_SHIFT 5
#define FCOE_KWQE_CONN_OFFLOAD3_B_ACK_0 (0x1<<6)
#define FCOE_KWQE_CONN_OFFLOAD3_B_ACK_0_SHIFT 6
#define FCOE_KWQE_CONN_OFFLOAD3_B_VLAN_FLAG (0x1<<7)
#define FCOE_KWQE_CONN_OFFLOAD3_B_VLAN_FLAG_SHIFT 7
#endif
	u32 reserved;
	u32 confq_first_pbe_addr_lo;
	u32 confq_first_pbe_addr_hi;
#if defined(__BIG_ENDIAN)
	u16 rx_max_fc_pay_len;
	u16 tx_total_conc_seqs;
#elif defined(__LITTLE_ENDIAN)
	u16 tx_total_conc_seqs;
	u16 rx_max_fc_pay_len;
#endif
#if defined(__BIG_ENDIAN)
	u8 rx_open_seqs_exch_c3;
	u8 rx_max_conc_seqs_c3;
	u16 rx_total_conc_seqs;
#elif defined(__LITTLE_ENDIAN)
	u16 rx_total_conc_seqs;
	u8 rx_max_conc_seqs_c3;
	u8 rx_open_seqs_exch_c3;
#endif
};

/*
 * FCoE connection offload request 4
 */
struct fcoe_kwqe_conn_offload4 {
#if defined(__BIG_ENDIAN)
	struct fcoe_kwqe_header hdr;
	u8 reserved2;
	u8 e_d_tov_timer_val;
#elif defined(__LITTLE_ENDIAN)
	u8 e_d_tov_timer_val;
	u8 reserved2;
	struct fcoe_kwqe_header hdr;
#endif
	u8 src_mac_addr_lo32[4];
#if defined(__BIG_ENDIAN)
	u8 dst_mac_addr_hi16[2];
	u8 src_mac_addr_hi16[2];
#elif defined(__LITTLE_ENDIAN)
	u8 src_mac_addr_hi16[2];
	u8 dst_mac_addr_hi16[2];
#endif
	u8 dst_mac_addr_lo32[4];
	u32 lcq_addr_lo;
	u32 lcq_addr_hi;
	u32 confq_pbl_base_addr_lo;
	u32 confq_pbl_base_addr_hi;
};

/*
 * FCoE connection enable request
 */
struct fcoe_kwqe_conn_enable_disable {
#if defined(__BIG_ENDIAN)
	struct fcoe_kwqe_header hdr;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	struct fcoe_kwqe_header hdr;
#endif
	u8 src_mac_addr_lo32[4];
#if defined(__BIG_ENDIAN)
	u16 vlan_tag;
#define FCOE_KWQE_CONN_ENABLE_DISABLE_VLAN_ID (0xFFF<<0)
#define FCOE_KWQE_CONN_ENABLE_DISABLE_VLAN_ID_SHIFT 0
#define FCOE_KWQE_CONN_ENABLE_DISABLE_CFI (0x1<<12)
#define FCOE_KWQE_CONN_ENABLE_DISABLE_CFI_SHIFT 12
#define FCOE_KWQE_CONN_ENABLE_DISABLE_PRIORITY (0x7<<13)
#define FCOE_KWQE_CONN_ENABLE_DISABLE_PRIORITY_SHIFT 13
	u8 src_mac_addr_hi16[2];
#elif defined(__LITTLE_ENDIAN)
	u8 src_mac_addr_hi16[2];
	u16 vlan_tag;
#define FCOE_KWQE_CONN_ENABLE_DISABLE_VLAN_ID (0xFFF<<0)
#define FCOE_KWQE_CONN_ENABLE_DISABLE_VLAN_ID_SHIFT 0
#define FCOE_KWQE_CONN_ENABLE_DISABLE_CFI (0x1<<12)
#define FCOE_KWQE_CONN_ENABLE_DISABLE_CFI_SHIFT 12
#define FCOE_KWQE_CONN_ENABLE_DISABLE_PRIORITY (0x7<<13)
#define FCOE_KWQE_CONN_ENABLE_DISABLE_PRIORITY_SHIFT 13
#endif
	u8 dst_mac_addr_lo32[4];
#if defined(__BIG_ENDIAN)
	u16 reserved1;
	u8 dst_mac_addr_hi16[2];
#elif defined(__LITTLE_ENDIAN)
	u8 dst_mac_addr_hi16[2];
	u16 reserved1;
#endif
#if defined(__BIG_ENDIAN)
	u8 vlan_flag;
	u8 s_id[3];
#elif defined(__LITTLE_ENDIAN)
	u8 s_id[3];
	u8 vlan_flag;
#endif
#if defined(__BIG_ENDIAN)
	u8 reserved3;
	u8 d_id[3];
#elif defined(__LITTLE_ENDIAN)
	u8 d_id[3];
	u8 reserved3;
#endif
	u32 context_id;
	u32 conn_id;
	u32 reserved4;
};

/*
 * FCoE connection destroy request
 */
struct fcoe_kwqe_conn_destroy {
#if defined(__BIG_ENDIAN)
	struct fcoe_kwqe_header hdr;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	struct fcoe_kwqe_header hdr;
#endif
	u32 context_id;
	u32 conn_id;
	u32 reserved1[5];
};

/*
 * FCoe destroy request
 */
struct fcoe_kwqe_destroy {
#if defined(__BIG_ENDIAN)
	struct fcoe_kwqe_header hdr;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	struct fcoe_kwqe_header hdr;
#endif
	u32 reserved1[7];
};

/*
 * FCoe statistics request
 */
struct fcoe_kwqe_stat {
#if defined(__BIG_ENDIAN)
	struct fcoe_kwqe_header hdr;
	u16 reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16 reserved0;
	struct fcoe_kwqe_header hdr;
#endif
	u32 stat_params_addr_lo;
	u32 stat_params_addr_hi;
	u32 reserved1[5];
};

/*
 * FCoE KWQ WQE
 */
union fcoe_kwqe {
	struct fcoe_kwqe_init1 init1;
	struct fcoe_kwqe_init2 init2;
	struct fcoe_kwqe_init3 init3;
	struct fcoe_kwqe_conn_offload1 conn_offload1;
	struct fcoe_kwqe_conn_offload2 conn_offload2;
	struct fcoe_kwqe_conn_offload3 conn_offload3;
	struct fcoe_kwqe_conn_offload4 conn_offload4;
	struct fcoe_kwqe_conn_enable_disable conn_enable_disable;
	struct fcoe_kwqe_conn_destroy conn_destroy;
	struct fcoe_kwqe_destroy destroy;
	struct fcoe_kwqe_stat statistics;
};

struct fcoe_task_ctx_entry {
	struct fcoe_task_ctx_entry_tx_only tx_wr_only;
	struct fcoe_task_ctx_entry_txwr_rxrd tx_wr_rx_rd;
	struct fcoe_task_ctx_entry_tx_rx_cmn cmn;
	struct fcoe_task_ctx_entry_rxwr_txrd rx_wr_tx_rd;
	struct fcoe_task_ctx_entry_rx_only rx_wr_only;
	u32 reserved[4];
};

/*
 * FCoE connection enable\disable params passed by driver to FW in FCoE enable ramrod
 */
struct fcoe_conn_enable_disable_ramrod_params {
	struct fcoe_kwqe_conn_enable_disable enable_disable_kwqe;
};


/*
 * FCoE connection offload params passed by driver to FW in FCoE offload ramrod
 */
struct fcoe_conn_offload_ramrod_params {
	struct fcoe_kwqe_conn_offload1 offload_kwqe1;
	struct fcoe_kwqe_conn_offload2 offload_kwqe2;
	struct fcoe_kwqe_conn_offload3 offload_kwqe3;
	struct fcoe_kwqe_conn_offload4 offload_kwqe4;
};

/*
 * FCoE init params passed by driver to FW in FCoE init ramrod
 */
struct fcoe_init_ramrod_params {
	struct fcoe_kwqe_init1 init_kwqe1;
	struct fcoe_kwqe_init2 init_kwqe2;
	struct fcoe_kwqe_init3 init_kwqe3;
	struct regpair eq_addr;
	struct regpair eq_next_page_addr;
#if defined(__BIG_ENDIAN)
	u16 sb_num;
	u16 eq_prod;
#elif defined(__LITTLE_ENDIAN)
	u16 eq_prod;
	u16 sb_num;
#endif
#if defined(__BIG_ENDIAN)
	u16 reserved1;
	u8 reserved0;
	u8 sb_id;
#elif defined(__LITTLE_ENDIAN)
	u8 sb_id;
	u8 reserved0;
	u16 reserved1;
#endif
};


/*
 * FCoE statistics params buffer passed by driver to FW in FCoE statistics ramrod
 */
struct fcoe_stat_ramrod_params {
	struct fcoe_kwqe_stat stat_kwqe;
};


/*
 * FCoE 16-bits vlan structure
 */
struct fcoe_vlan_fields {
	u16 fields;
#define FCOE_VLAN_FIELDS_VID (0xFFF<<0)
#define FCOE_VLAN_FIELDS_VID_SHIFT 0
#define FCOE_VLAN_FIELDS_CLI (0x1<<12)
#define FCOE_VLAN_FIELDS_CLI_SHIFT 12
#define FCOE_VLAN_FIELDS_PRI (0x7<<13)
#define FCOE_VLAN_FIELDS_PRI_SHIFT 13
};


/*
 * FCoE 16-bits vlan union
 */
union fcoe_vlan_field_union {
	struct fcoe_vlan_fields fields;
	u16 val;
};

/*
 * Parameters used for Class 2 verifications
 */
struct ustorm_fcoe_c2_params {
#if defined(__BIG_ENDIAN)
	u16 e2e_credit;
	u16 con_seq;
#elif defined(__LITTLE_ENDIAN)
	u16 con_seq;
	u16 e2e_credit;
#endif
#if defined(__BIG_ENDIAN)
	u16 ackq_prod;
	u16 open_seq_per_exch;
#elif defined(__LITTLE_ENDIAN)
	u16 open_seq_per_exch;
	u16 ackq_prod;
#endif
	struct regpair ackq_pbl_base;
	struct regpair ackq_cur_seg;
};

/*
 * Parameters used for Class 2 verifications
 */
struct xstorm_fcoe_c2_params {
#if defined(__BIG_ENDIAN)
	u16 reserved0;
	u8 ackq_x_prod;
	u8 max_conc_seqs_c2;
#elif defined(__LITTLE_ENDIAN)
	u8 max_conc_seqs_c2;
	u8 ackq_x_prod;
	u16 reserved0;
#endif
	struct regpair ackq_pbl_base;
	struct regpair ackq_cur_seg;
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
