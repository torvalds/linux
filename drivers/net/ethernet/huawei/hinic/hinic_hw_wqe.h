/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#ifndef HINIC_HW_WQE_H
#define HINIC_HW_WQE_H

#include "hinic_common.h"

#define HINIC_CMDQ_CTRL_PI_SHIFT                        0
#define HINIC_CMDQ_CTRL_CMD_SHIFT                       16
#define HINIC_CMDQ_CTRL_MOD_SHIFT                       24
#define HINIC_CMDQ_CTRL_ACK_TYPE_SHIFT                  29
#define HINIC_CMDQ_CTRL_HW_BUSY_BIT_SHIFT               31

#define HINIC_CMDQ_CTRL_PI_MASK                         0xFFFF
#define HINIC_CMDQ_CTRL_CMD_MASK                        0xFF
#define HINIC_CMDQ_CTRL_MOD_MASK                        0x1F
#define HINIC_CMDQ_CTRL_ACK_TYPE_MASK                   0x3
#define HINIC_CMDQ_CTRL_HW_BUSY_BIT_MASK                0x1

#define HINIC_CMDQ_CTRL_SET(val, member)                        \
			(((u32)(val) & HINIC_CMDQ_CTRL_##member##_MASK) \
			 << HINIC_CMDQ_CTRL_##member##_SHIFT)

#define HINIC_CMDQ_CTRL_GET(val, member)                        \
			(((val) >> HINIC_CMDQ_CTRL_##member##_SHIFT) \
			 & HINIC_CMDQ_CTRL_##member##_MASK)

#define HINIC_CMDQ_WQE_HEADER_BUFDESC_LEN_SHIFT         0
#define HINIC_CMDQ_WQE_HEADER_COMPLETE_FMT_SHIFT        15
#define HINIC_CMDQ_WQE_HEADER_DATA_FMT_SHIFT            22
#define HINIC_CMDQ_WQE_HEADER_COMPLETE_REQ_SHIFT        23
#define HINIC_CMDQ_WQE_HEADER_COMPLETE_SECT_LEN_SHIFT   27
#define HINIC_CMDQ_WQE_HEADER_CTRL_LEN_SHIFT            29
#define HINIC_CMDQ_WQE_HEADER_TOGGLED_WRAPPED_SHIFT     31

#define HINIC_CMDQ_WQE_HEADER_BUFDESC_LEN_MASK          0xFF
#define HINIC_CMDQ_WQE_HEADER_COMPLETE_FMT_MASK         0x1
#define HINIC_CMDQ_WQE_HEADER_DATA_FMT_MASK             0x1
#define HINIC_CMDQ_WQE_HEADER_COMPLETE_REQ_MASK         0x1
#define HINIC_CMDQ_WQE_HEADER_COMPLETE_SECT_LEN_MASK    0x3
#define HINIC_CMDQ_WQE_HEADER_CTRL_LEN_MASK             0x3
#define HINIC_CMDQ_WQE_HEADER_TOGGLED_WRAPPED_MASK      0x1

#define HINIC_CMDQ_WQE_HEADER_SET(val, member)                  \
			(((u32)(val) & HINIC_CMDQ_WQE_HEADER_##member##_MASK) \
			 << HINIC_CMDQ_WQE_HEADER_##member##_SHIFT)

#define HINIC_CMDQ_WQE_HEADER_GET(val, member)                  \
			(((val) >> HINIC_CMDQ_WQE_HEADER_##member##_SHIFT) \
			 & HINIC_CMDQ_WQE_HEADER_##member##_MASK)

#define HINIC_SQ_CTRL_BUFDESC_SECT_LEN_SHIFT           0
#define HINIC_SQ_CTRL_TASKSECT_LEN_SHIFT               16
#define HINIC_SQ_CTRL_DATA_FORMAT_SHIFT                22
#define HINIC_SQ_CTRL_LEN_SHIFT                        29

#define HINIC_SQ_CTRL_BUFDESC_SECT_LEN_MASK            0xFF
#define HINIC_SQ_CTRL_TASKSECT_LEN_MASK                0x1F
#define HINIC_SQ_CTRL_DATA_FORMAT_MASK                 0x1
#define HINIC_SQ_CTRL_LEN_MASK                         0x3

#define HINIC_SQ_CTRL_QUEUE_INFO_PLDOFF_SHIFT          2
#define HINIC_SQ_CTRL_QUEUE_INFO_UFO_SHIFT             10
#define HINIC_SQ_CTRL_QUEUE_INFO_TSO_SHIFT             11
#define HINIC_SQ_CTRL_QUEUE_INFO_TCPUDP_CS_SHIFT       12
#define HINIC_SQ_CTRL_QUEUE_INFO_MSS_SHIFT             13
#define HINIC_SQ_CTRL_QUEUE_INFO_SCTP_SHIFT            27
#define HINIC_SQ_CTRL_QUEUE_INFO_UC_SHIFT              28
#define HINIC_SQ_CTRL_QUEUE_INFO_PRI_SHIFT             29

#define HINIC_SQ_CTRL_QUEUE_INFO_PLDOFF_MASK           0xFF
#define HINIC_SQ_CTRL_QUEUE_INFO_UFO_MASK              0x1
#define HINIC_SQ_CTRL_QUEUE_INFO_TSO_MASK              0x1
#define HINIC_SQ_CTRL_QUEUE_INFO_TCPUDP_CS_MASK	       0x1
#define HINIC_SQ_CTRL_QUEUE_INFO_MSS_MASK              0x3FFF
#define HINIC_SQ_CTRL_QUEUE_INFO_SCTP_MASK             0x1
#define HINIC_SQ_CTRL_QUEUE_INFO_UC_MASK               0x1
#define HINIC_SQ_CTRL_QUEUE_INFO_PRI_MASK              0x7

#define HINIC_SQ_CTRL_SET(val, member)          \
		(((u32)(val) & HINIC_SQ_CTRL_##member##_MASK) \
		 << HINIC_SQ_CTRL_##member##_SHIFT)

#define HINIC_SQ_CTRL_GET(val, member)          \
		(((val) >> HINIC_SQ_CTRL_##member##_SHIFT) \
		 & HINIC_SQ_CTRL_##member##_MASK)

#define HINIC_SQ_CTRL_CLEAR(val, member)	\
		((u32)(val) & (~(HINIC_SQ_CTRL_##member##_MASK \
		 << HINIC_SQ_CTRL_##member##_SHIFT)))

#define HINIC_SQ_TASK_INFO0_L2HDR_LEN_SHIFT     0
#define HINIC_SQ_TASK_INFO0_L4_OFFLOAD_SHIFT    8
#define HINIC_SQ_TASK_INFO0_INNER_L3TYPE_SHIFT  10
#define HINIC_SQ_TASK_INFO0_VLAN_OFFLOAD_SHIFT  12
#define HINIC_SQ_TASK_INFO0_PARSE_FLAG_SHIFT    13
/* 1 bit reserved */
#define HINIC_SQ_TASK_INFO0_TSO_FLAG_SHIFT      15
#define HINIC_SQ_TASK_INFO0_VLAN_TAG_SHIFT      16

#define HINIC_SQ_TASK_INFO0_L2HDR_LEN_MASK      0xFF
#define HINIC_SQ_TASK_INFO0_L4_OFFLOAD_MASK     0x3
#define HINIC_SQ_TASK_INFO0_INNER_L3TYPE_MASK   0x3
#define HINIC_SQ_TASK_INFO0_VLAN_OFFLOAD_MASK   0x1
#define HINIC_SQ_TASK_INFO0_PARSE_FLAG_MASK     0x1
/* 1 bit reserved */
#define HINIC_SQ_TASK_INFO0_TSO_FLAG_MASK       0x1
#define HINIC_SQ_TASK_INFO0_VLAN_TAG_MASK       0xFFFF

#define HINIC_SQ_TASK_INFO0_SET(val, member)    \
		(((u32)(val) & HINIC_SQ_TASK_INFO0_##member##_MASK) <<  \
		 HINIC_SQ_TASK_INFO0_##member##_SHIFT)

/* 8 bits reserved */
#define HINIC_SQ_TASK_INFO1_MEDIA_TYPE_SHIFT    8
#define HINIC_SQ_TASK_INFO1_INNER_L4LEN_SHIFT   16
#define HINIC_SQ_TASK_INFO1_INNER_L3LEN_SHIFT   24

/* 8 bits reserved */
#define HINIC_SQ_TASK_INFO1_MEDIA_TYPE_MASK     0xFF
#define HINIC_SQ_TASK_INFO1_INNER_L4LEN_MASK    0xFF
#define HINIC_SQ_TASK_INFO1_INNER_L3LEN_MASK    0xFF

#define HINIC_SQ_TASK_INFO1_SET(val, member)    \
		(((u32)(val) & HINIC_SQ_TASK_INFO1_##member##_MASK) <<  \
		 HINIC_SQ_TASK_INFO1_##member##_SHIFT)

#define HINIC_SQ_TASK_INFO2_TUNNEL_L4LEN_SHIFT  0
#define HINIC_SQ_TASK_INFO2_OUTER_L3LEN_SHIFT   8
#define HINIC_SQ_TASK_INFO2_TUNNEL_L4TYPE_SHIFT 16
/* 1 bit reserved */
#define HINIC_SQ_TASK_INFO2_OUTER_L3TYPE_SHIFT  24
/* 8 bits reserved */

#define HINIC_SQ_TASK_INFO2_TUNNEL_L4LEN_MASK   0xFF
#define HINIC_SQ_TASK_INFO2_OUTER_L3LEN_MASK    0xFF
#define HINIC_SQ_TASK_INFO2_TUNNEL_L4TYPE_MASK  0x7
/* 1 bit reserved */
#define HINIC_SQ_TASK_INFO2_OUTER_L3TYPE_MASK   0x3
/* 8 bits reserved */

#define HINIC_SQ_TASK_INFO2_SET(val, member)    \
		(((u32)(val) & HINIC_SQ_TASK_INFO2_##member##_MASK) <<  \
		 HINIC_SQ_TASK_INFO2_##member##_SHIFT)

/* 31 bits reserved */
#define HINIC_SQ_TASK_INFO4_L2TYPE_SHIFT        31

/* 31 bits reserved */
#define HINIC_SQ_TASK_INFO4_L2TYPE_MASK         0x1

#define HINIC_SQ_TASK_INFO4_SET(val, member)    \
		(((u32)(val) & HINIC_SQ_TASK_INFO4_##member##_MASK) << \
		 HINIC_SQ_TASK_INFO4_##member##_SHIFT)

#define HINIC_RQ_CQE_STATUS_RXDONE_SHIFT        31

#define HINIC_RQ_CQE_STATUS_RXDONE_MASK         0x1

#define HINIC_RQ_CQE_STATUS_CSUM_ERR_SHIFT	0

#define HINIC_RQ_CQE_STATUS_CSUM_ERR_MASK	0xFFFFU

#define HINIC_RQ_CQE_STATUS_GET(val, member)    \
		(((val) >> HINIC_RQ_CQE_STATUS_##member##_SHIFT) & \
		 HINIC_RQ_CQE_STATUS_##member##_MASK)

#define HINIC_RQ_CQE_STATUS_CLEAR(val, member)  \
		((val) & (~(HINIC_RQ_CQE_STATUS_##member##_MASK << \
		 HINIC_RQ_CQE_STATUS_##member##_SHIFT)))

#define HINIC_RQ_CQE_SGE_LEN_SHIFT              16

#define HINIC_RQ_CQE_SGE_LEN_MASK               0xFFFF

#define HINIC_RQ_CQE_SGE_GET(val, member)       \
		(((val) >> HINIC_RQ_CQE_SGE_##member##_SHIFT) & \
		 HINIC_RQ_CQE_SGE_##member##_MASK)

#define HINIC_RQ_CTRL_BUFDESC_SECT_LEN_SHIFT    0
#define HINIC_RQ_CTRL_COMPLETE_FORMAT_SHIFT     15
#define HINIC_RQ_CTRL_COMPLETE_LEN_SHIFT        27
#define HINIC_RQ_CTRL_LEN_SHIFT                 29

#define HINIC_RQ_CTRL_BUFDESC_SECT_LEN_MASK     0xFF
#define HINIC_RQ_CTRL_COMPLETE_FORMAT_MASK      0x1
#define HINIC_RQ_CTRL_COMPLETE_LEN_MASK         0x3
#define HINIC_RQ_CTRL_LEN_MASK                  0x3

#define HINIC_RQ_CTRL_SET(val, member)          \
		(((u32)(val) & HINIC_RQ_CTRL_##member##_MASK) << \
		 HINIC_RQ_CTRL_##member##_SHIFT)

#define HINIC_SQ_WQE_SIZE(nr_sges)              \
		(sizeof(struct hinic_sq_ctrl) + \
		 sizeof(struct hinic_sq_task) + \
		 (nr_sges) * sizeof(struct hinic_sq_bufdesc))

#define HINIC_SCMD_DATA_LEN                     16

#define HINIC_MAX_SQ_BUFDESCS                   17

#define HINIC_SQ_WQE_MAX_SIZE                   320
#define HINIC_RQ_WQE_SIZE                       32

#define HINIC_MSS_DEFAULT		        0x3E00
#define HINIC_MSS_MIN		                0x50

enum hinic_l4offload_type {
	HINIC_L4_OFF_DISABLE            = 0,
	HINIC_TCP_OFFLOAD_ENABLE        = 1,
	HINIC_SCTP_OFFLOAD_ENABLE       = 2,
	HINIC_UDP_OFFLOAD_ENABLE        = 3,
};

enum hinic_vlan_offload {
	HINIC_VLAN_OFF_DISABLE = 0,
	HINIC_VLAN_OFF_ENABLE  = 1,
};

enum hinic_pkt_parsed {
	HINIC_PKT_NOT_PARSED = 0,
	HINIC_PKT_PARSED     = 1,
};

enum hinic_l3_offload_type {
	L3TYPE_UNKNOWN = 0,
	IPV6_PKT = 1,
	IPV4_PKT_NO_CHKSUM_OFFLOAD = 2,
	IPV4_PKT_WITH_CHKSUM_OFFLOAD = 3,
};

enum hinic_l4_offload_type {
	OFFLOAD_DISABLE     = 0,
	TCP_OFFLOAD_ENABLE  = 1,
	SCTP_OFFLOAD_ENABLE = 2,
	UDP_OFFLOAD_ENABLE  = 3,
};

enum hinic_l4_tunnel_type {
	NOT_TUNNEL,
	TUNNEL_UDP_NO_CSUM,
	TUNNEL_UDP_CSUM,
};

enum hinic_outer_l3type {
	HINIC_OUTER_L3TYPE_UNKNOWN              = 0,
	HINIC_OUTER_L3TYPE_IPV6                 = 1,
	HINIC_OUTER_L3TYPE_IPV4_NO_CHKSUM       = 2,
	HINIC_OUTER_L3TYPE_IPV4_CHKSUM          = 3,
};

enum hinic_media_type {
	HINIC_MEDIA_UNKNOWN = 0,
};

enum hinic_l2type {
	HINIC_L2TYPE_ETH = 0,
};

enum hinc_tunnel_l4type {
	HINIC_TUNNEL_L4TYPE_UNKNOWN = 0,
};

struct hinic_cmdq_header {
	u32     header_info;
	u32     saved_data;
};

struct hinic_status {
	u32 status_info;
};

struct hinic_ctrl {
	u32 ctrl_info;
};

struct hinic_sge_resp {
	struct hinic_sge        sge;
	u32                     rsvd;
};

struct hinic_cmdq_completion {
	/* HW Format */
	union {
		struct hinic_sge_resp   sge_resp;
		u64                     direct_resp;
	};
};

struct hinic_scmd_bufdesc {
	u32     buf_len;
	u32     rsvd;
	u8      data[HINIC_SCMD_DATA_LEN];
};

struct hinic_lcmd_bufdesc {
	struct hinic_sge        sge;
	u32                     rsvd1;
	u64                     rsvd2;
	u64                     rsvd3;
};

struct hinic_cmdq_wqe_scmd {
	struct hinic_cmdq_header        header;
	u64                             rsvd;
	struct hinic_status             status;
	struct hinic_ctrl               ctrl;
	struct hinic_cmdq_completion    completion;
	struct hinic_scmd_bufdesc       buf_desc;
};

struct hinic_cmdq_wqe_lcmd {
	struct hinic_cmdq_header        header;
	struct hinic_status             status;
	struct hinic_ctrl               ctrl;
	struct hinic_cmdq_completion    completion;
	struct hinic_lcmd_bufdesc       buf_desc;
};

struct hinic_cmdq_direct_wqe {
	struct hinic_cmdq_wqe_scmd      wqe_scmd;
};

struct hinic_cmdq_wqe {
	/* HW Format */
	union {
		struct hinic_cmdq_direct_wqe    direct_wqe;
		struct hinic_cmdq_wqe_lcmd      wqe_lcmd;
	};
};

struct hinic_sq_ctrl {
	u32     ctrl_info;
	u32     queue_info;
};

struct hinic_sq_task {
	u32     pkt_info0;
	u32     pkt_info1;
	u32     pkt_info2;
	u32     ufo_v6_identify;
	u32     pkt_info4;
	u32     zero_pad;
};

struct hinic_sq_bufdesc {
	struct hinic_sge sge;
	u32     rsvd;
};

struct hinic_sq_wqe {
	struct hinic_sq_ctrl            ctrl;
	struct hinic_sq_task            task;
	struct hinic_sq_bufdesc         buf_descs[HINIC_MAX_SQ_BUFDESCS];
};

struct hinic_rq_cqe {
	u32     status;
	u32     len;

	u32     rsvd2;
	u32     rsvd3;
	u32     rsvd4;
	u32     rsvd5;
	u32     rsvd6;
	u32     rsvd7;
};

struct hinic_rq_ctrl {
	u32     ctrl_info;
};

struct hinic_rq_cqe_sect {
	struct hinic_sge        sge;
	u32                     rsvd;
};

struct hinic_rq_bufdesc {
	u32     hi_addr;
	u32     lo_addr;
};

struct hinic_rq_wqe {
	struct hinic_rq_ctrl            ctrl;
	u32                             rsvd;
	struct hinic_rq_cqe_sect        cqe_sect;
	struct hinic_rq_bufdesc         buf_desc;
};

struct hinic_hw_wqe {
	/* HW Format */
	union {
		struct hinic_cmdq_wqe   cmdq_wqe;
		struct hinic_sq_wqe     sq_wqe;
		struct hinic_rq_wqe     rq_wqe;
	};
};

#endif
