/*
* Copyright (c) 2006 - 2009 Intel Corporation.  All rights reserved.
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

#ifndef __NES_HW_H
#define __NES_HW_H

#include <linux/inet_lro.h>

#define NES_PHY_TYPE_CX4       1
#define NES_PHY_TYPE_1G        2
#define NES_PHY_TYPE_ARGUS     4
#define NES_PHY_TYPE_PUMA_1G   5
#define NES_PHY_TYPE_PUMA_10G  6
#define NES_PHY_TYPE_GLADIUS   7
#define NES_PHY_TYPE_SFP_D     8
#define NES_PHY_TYPE_KR	       9

#define NES_MULTICAST_PF_MAX 8
#define NES_A0 3

#define NES_ENABLE_PAU 0x07000001
#define NES_DISABLE_PAU 0x07000000
#define NES_PAU_COUNTER 10
#define NES_CQP_OPCODE_MASK 0x3f

enum pci_regs {
	NES_INT_STAT = 0x0000,
	NES_INT_MASK = 0x0004,
	NES_INT_PENDING = 0x0008,
	NES_INTF_INT_STAT = 0x000C,
	NES_INTF_INT_MASK = 0x0010,
	NES_TIMER_STAT = 0x0014,
	NES_PERIODIC_CONTROL = 0x0018,
	NES_ONE_SHOT_CONTROL = 0x001C,
	NES_EEPROM_COMMAND = 0x0020,
	NES_EEPROM_DATA = 0x0024,
	NES_FLASH_COMMAND = 0x0028,
	NES_FLASH_DATA  = 0x002C,
	NES_SOFTWARE_RESET = 0x0030,
	NES_CQ_ACK = 0x0034,
	NES_WQE_ALLOC = 0x0040,
	NES_CQE_ALLOC = 0x0044,
	NES_AEQ_ALLOC = 0x0048
};

enum indexed_regs {
	NES_IDX_CREATE_CQP_LOW = 0x0000,
	NES_IDX_CREATE_CQP_HIGH = 0x0004,
	NES_IDX_QP_CONTROL = 0x0040,
	NES_IDX_FLM_CONTROL = 0x0080,
	NES_IDX_INT_CPU_STATUS = 0x00a0,
	NES_IDX_GPR_TRIGGER = 0x00bc,
	NES_IDX_GPIO_CONTROL = 0x00f0,
	NES_IDX_GPIO_DATA = 0x00f4,
	NES_IDX_GPR2 = 0x010c,
	NES_IDX_TCP_CONFIG0 = 0x01e4,
	NES_IDX_TCP_TIMER_CONFIG = 0x01ec,
	NES_IDX_TCP_NOW = 0x01f0,
	NES_IDX_QP_MAX_CFG_SIZES = 0x0200,
	NES_IDX_QP_CTX_SIZE = 0x0218,
	NES_IDX_TCP_TIMER_SIZE0 = 0x0238,
	NES_IDX_TCP_TIMER_SIZE1 = 0x0240,
	NES_IDX_ARP_CACHE_SIZE = 0x0258,
	NES_IDX_CQ_CTX_SIZE = 0x0260,
	NES_IDX_MRT_SIZE = 0x0278,
	NES_IDX_PBL_REGION_SIZE = 0x0280,
	NES_IDX_IRRQ_COUNT = 0x02b0,
	NES_IDX_RX_WINDOW_BUFFER_PAGE_TABLE_SIZE = 0x02f0,
	NES_IDX_RX_WINDOW_BUFFER_SIZE = 0x0300,
	NES_IDX_DST_IP_ADDR = 0x0400,
	NES_IDX_PCIX_DIAG = 0x08e8,
	NES_IDX_MPP_DEBUG = 0x0a00,
	NES_IDX_PORT_RX_DISCARDS = 0x0a30,
	NES_IDX_PORT_TX_DISCARDS = 0x0a34,
	NES_IDX_MPP_LB_DEBUG = 0x0b00,
	NES_IDX_DENALI_CTL_22 = 0x1058,
	NES_IDX_MAC_TX_CONTROL = 0x2000,
	NES_IDX_MAC_TX_CONFIG = 0x2004,
	NES_IDX_MAC_TX_PAUSE_QUANTA = 0x2008,
	NES_IDX_MAC_RX_CONTROL = 0x200c,
	NES_IDX_MAC_RX_CONFIG = 0x2010,
	NES_IDX_MAC_EXACT_MATCH_BOTTOM = 0x201c,
	NES_IDX_MAC_MDIO_CONTROL = 0x2084,
	NES_IDX_MAC_TX_OCTETS_LOW = 0x2100,
	NES_IDX_MAC_TX_OCTETS_HIGH = 0x2104,
	NES_IDX_MAC_TX_FRAMES_LOW = 0x2108,
	NES_IDX_MAC_TX_FRAMES_HIGH = 0x210c,
	NES_IDX_MAC_TX_PAUSE_FRAMES = 0x2118,
	NES_IDX_MAC_TX_ERRORS = 0x2138,
	NES_IDX_MAC_RX_OCTETS_LOW = 0x213c,
	NES_IDX_MAC_RX_OCTETS_HIGH = 0x2140,
	NES_IDX_MAC_RX_FRAMES_LOW = 0x2144,
	NES_IDX_MAC_RX_FRAMES_HIGH = 0x2148,
	NES_IDX_MAC_RX_BC_FRAMES_LOW = 0x214c,
	NES_IDX_MAC_RX_MC_FRAMES_HIGH = 0x2150,
	NES_IDX_MAC_RX_PAUSE_FRAMES = 0x2154,
	NES_IDX_MAC_RX_SHORT_FRAMES = 0x2174,
	NES_IDX_MAC_RX_OVERSIZED_FRAMES = 0x2178,
	NES_IDX_MAC_RX_JABBER_FRAMES = 0x217c,
	NES_IDX_MAC_RX_CRC_ERR_FRAMES = 0x2180,
	NES_IDX_MAC_RX_LENGTH_ERR_FRAMES = 0x2184,
	NES_IDX_MAC_RX_SYMBOL_ERR_FRAMES = 0x2188,
	NES_IDX_MAC_INT_STATUS = 0x21f0,
	NES_IDX_MAC_INT_MASK = 0x21f4,
	NES_IDX_PHY_PCS_CONTROL_STATUS0 = 0x2800,
	NES_IDX_PHY_PCS_CONTROL_STATUS1 = 0x2a00,
	NES_IDX_ETH_SERDES_COMMON_CONTROL0 = 0x2808,
	NES_IDX_ETH_SERDES_COMMON_CONTROL1 = 0x2a08,
	NES_IDX_ETH_SERDES_COMMON_STATUS0 = 0x280c,
	NES_IDX_ETH_SERDES_COMMON_STATUS1 = 0x2a0c,
	NES_IDX_ETH_SERDES_TX_EMP0 = 0x2810,
	NES_IDX_ETH_SERDES_TX_EMP1 = 0x2a10,
	NES_IDX_ETH_SERDES_TX_DRIVE0 = 0x2814,
	NES_IDX_ETH_SERDES_TX_DRIVE1 = 0x2a14,
	NES_IDX_ETH_SERDES_RX_MODE0 = 0x2818,
	NES_IDX_ETH_SERDES_RX_MODE1 = 0x2a18,
	NES_IDX_ETH_SERDES_RX_SIGDET0 = 0x281c,
	NES_IDX_ETH_SERDES_RX_SIGDET1 = 0x2a1c,
	NES_IDX_ETH_SERDES_BYPASS0 = 0x2820,
	NES_IDX_ETH_SERDES_BYPASS1 = 0x2a20,
	NES_IDX_ETH_SERDES_LOOPBACK_CONTROL0 = 0x2824,
	NES_IDX_ETH_SERDES_LOOPBACK_CONTROL1 = 0x2a24,
	NES_IDX_ETH_SERDES_RX_EQ_CONTROL0 = 0x2828,
	NES_IDX_ETH_SERDES_RX_EQ_CONTROL1 = 0x2a28,
	NES_IDX_ETH_SERDES_RX_EQ_STATUS0 = 0x282c,
	NES_IDX_ETH_SERDES_RX_EQ_STATUS1 = 0x2a2c,
	NES_IDX_ETH_SERDES_CDR_RESET0 = 0x2830,
	NES_IDX_ETH_SERDES_CDR_RESET1 = 0x2a30,
	NES_IDX_ETH_SERDES_CDR_CONTROL0 = 0x2834,
	NES_IDX_ETH_SERDES_CDR_CONTROL1 = 0x2a34,
	NES_IDX_ETH_SERDES_TX_HIGHZ_LANE_MODE0 = 0x2838,
	NES_IDX_ETH_SERDES_TX_HIGHZ_LANE_MODE1 = 0x2a38,
	NES_IDX_ENDNODE0_NSTAT_RX_DISCARD = 0x3080,
	NES_IDX_ENDNODE0_NSTAT_RX_OCTETS_LO = 0x3000,
	NES_IDX_ENDNODE0_NSTAT_RX_OCTETS_HI = 0x3004,
	NES_IDX_ENDNODE0_NSTAT_RX_FRAMES_LO = 0x3008,
	NES_IDX_ENDNODE0_NSTAT_RX_FRAMES_HI = 0x300c,
	NES_IDX_ENDNODE0_NSTAT_TX_OCTETS_LO = 0x7000,
	NES_IDX_ENDNODE0_NSTAT_TX_OCTETS_HI = 0x7004,
	NES_IDX_ENDNODE0_NSTAT_TX_FRAMES_LO = 0x7008,
	NES_IDX_ENDNODE0_NSTAT_TX_FRAMES_HI = 0x700c,
	NES_IDX_WQM_CONFIG0 = 0x5000,
	NES_IDX_WQM_CONFIG1 = 0x5004,
	NES_IDX_CM_CONFIG = 0x5100,
	NES_IDX_NIC_LOGPORT_TO_PHYPORT = 0x6000,
	NES_IDX_NIC_PHYPORT_TO_USW = 0x6008,
	NES_IDX_NIC_ACTIVE = 0x6010,
	NES_IDX_NIC_UNICAST_ALL = 0x6018,
	NES_IDX_NIC_MULTICAST_ALL = 0x6020,
	NES_IDX_NIC_MULTICAST_ENABLE = 0x6028,
	NES_IDX_NIC_BROADCAST_ON = 0x6030,
	NES_IDX_USED_CHUNKS_TX = 0x60b0,
	NES_IDX_TX_POOL_SIZE = 0x60b8,
	NES_IDX_QUAD_HASH_TABLE_SIZE = 0x6148,
	NES_IDX_PERFECT_FILTER_LOW = 0x6200,
	NES_IDX_PERFECT_FILTER_HIGH = 0x6204,
	NES_IDX_IPV4_TCP_REXMITS = 0x7080,
	NES_IDX_DEBUG_ERROR_CONTROL_STATUS = 0x913c,
	NES_IDX_DEBUG_ERROR_MASKS0 = 0x9140,
	NES_IDX_DEBUG_ERROR_MASKS1 = 0x9144,
	NES_IDX_DEBUG_ERROR_MASKS2 = 0x9148,
	NES_IDX_DEBUG_ERROR_MASKS3 = 0x914c,
	NES_IDX_DEBUG_ERROR_MASKS4 = 0x9150,
	NES_IDX_DEBUG_ERROR_MASKS5 = 0x9154,
};

#define NES_IDX_MAC_TX_CONFIG_ENABLE_PAUSE   1
#define NES_IDX_MPP_DEBUG_PORT_DISABLE_PAUSE (1 << 17)

enum nes_cqp_opcodes {
	NES_CQP_CREATE_QP = 0x00,
	NES_CQP_MODIFY_QP = 0x01,
	NES_CQP_DESTROY_QP = 0x02,
	NES_CQP_CREATE_CQ = 0x03,
	NES_CQP_MODIFY_CQ = 0x04,
	NES_CQP_DESTROY_CQ = 0x05,
	NES_CQP_ALLOCATE_STAG = 0x09,
	NES_CQP_REGISTER_STAG = 0x0a,
	NES_CQP_QUERY_STAG = 0x0b,
	NES_CQP_REGISTER_SHARED_STAG = 0x0c,
	NES_CQP_DEALLOCATE_STAG = 0x0d,
	NES_CQP_MANAGE_ARP_CACHE = 0x0f,
	NES_CQP_DOWNLOAD_SEGMENT = 0x10,
	NES_CQP_SUSPEND_QPS = 0x11,
	NES_CQP_UPLOAD_CONTEXT = 0x13,
	NES_CQP_CREATE_CEQ = 0x16,
	NES_CQP_DESTROY_CEQ = 0x18,
	NES_CQP_CREATE_AEQ = 0x19,
	NES_CQP_DESTROY_AEQ = 0x1b,
	NES_CQP_LMI_ACCESS = 0x20,
	NES_CQP_FLUSH_WQES = 0x22,
	NES_CQP_MANAGE_APBVT = 0x23,
	NES_CQP_MANAGE_QUAD_HASH = 0x25
};

enum nes_cqp_wqe_word_idx {
	NES_CQP_WQE_OPCODE_IDX = 0,
	NES_CQP_WQE_ID_IDX = 1,
	NES_CQP_WQE_COMP_CTX_LOW_IDX = 2,
	NES_CQP_WQE_COMP_CTX_HIGH_IDX = 3,
	NES_CQP_WQE_COMP_SCRATCH_LOW_IDX = 4,
	NES_CQP_WQE_COMP_SCRATCH_HIGH_IDX = 5,
};

enum nes_cqp_wqe_word_download_idx { /* format differs from other cqp ops */
	NES_CQP_WQE_DL_OPCODE_IDX = 0,
	NES_CQP_WQE_DL_COMP_CTX_LOW_IDX = 1,
	NES_CQP_WQE_DL_COMP_CTX_HIGH_IDX = 2,
	NES_CQP_WQE_DL_LENGTH_0_TOTAL_IDX = 3
	/* For index values 4-15 use NES_NIC_SQ_WQE_ values */
};

enum nes_cqp_cq_wqeword_idx {
	NES_CQP_CQ_WQE_PBL_LOW_IDX = 6,
	NES_CQP_CQ_WQE_PBL_HIGH_IDX = 7,
	NES_CQP_CQ_WQE_CQ_CONTEXT_LOW_IDX = 8,
	NES_CQP_CQ_WQE_CQ_CONTEXT_HIGH_IDX = 9,
	NES_CQP_CQ_WQE_DOORBELL_INDEX_HIGH_IDX = 10,
};

enum nes_cqp_stag_wqeword_idx {
	NES_CQP_STAG_WQE_PBL_BLK_COUNT_IDX = 1,
	NES_CQP_STAG_WQE_LEN_HIGH_PD_IDX = 6,
	NES_CQP_STAG_WQE_LEN_LOW_IDX = 7,
	NES_CQP_STAG_WQE_STAG_IDX = 8,
	NES_CQP_STAG_WQE_VA_LOW_IDX = 10,
	NES_CQP_STAG_WQE_VA_HIGH_IDX = 11,
	NES_CQP_STAG_WQE_PA_LOW_IDX = 12,
	NES_CQP_STAG_WQE_PA_HIGH_IDX = 13,
	NES_CQP_STAG_WQE_PBL_LEN_IDX = 14
};

#define NES_CQP_OP_LOGICAL_PORT_SHIFT 26
#define NES_CQP_OP_IWARP_STATE_SHIFT 28
#define NES_CQP_OP_TERMLEN_SHIFT     28

enum nes_cqp_qp_bits {
	NES_CQP_QP_ARP_VALID = (1<<8),
	NES_CQP_QP_WINBUF_VALID = (1<<9),
	NES_CQP_QP_CONTEXT_VALID = (1<<10),
	NES_CQP_QP_ORD_VALID = (1<<11),
	NES_CQP_QP_WINBUF_DATAIND_EN = (1<<12),
	NES_CQP_QP_VIRT_WQS = (1<<13),
	NES_CQP_QP_DEL_HTE = (1<<14),
	NES_CQP_QP_CQS_VALID = (1<<15),
	NES_CQP_QP_TYPE_TSA = 0,
	NES_CQP_QP_TYPE_IWARP = (1<<16),
	NES_CQP_QP_TYPE_CQP = (4<<16),
	NES_CQP_QP_TYPE_NIC = (5<<16),
	NES_CQP_QP_MSS_CHG = (1<<20),
	NES_CQP_QP_STATIC_RESOURCES = (1<<21),
	NES_CQP_QP_IGNORE_MW_BOUND = (1<<22),
	NES_CQP_QP_VWQ_USE_LMI = (1<<23),
	NES_CQP_QP_IWARP_STATE_IDLE = (1<<NES_CQP_OP_IWARP_STATE_SHIFT),
	NES_CQP_QP_IWARP_STATE_RTS = (2<<NES_CQP_OP_IWARP_STATE_SHIFT),
	NES_CQP_QP_IWARP_STATE_CLOSING = (3<<NES_CQP_OP_IWARP_STATE_SHIFT),
	NES_CQP_QP_IWARP_STATE_TERMINATE = (5<<NES_CQP_OP_IWARP_STATE_SHIFT),
	NES_CQP_QP_IWARP_STATE_ERROR = (6<<NES_CQP_OP_IWARP_STATE_SHIFT),
	NES_CQP_QP_IWARP_STATE_MASK = (7<<NES_CQP_OP_IWARP_STATE_SHIFT),
	NES_CQP_QP_TERM_DONT_SEND_FIN = (1<<24),
	NES_CQP_QP_TERM_DONT_SEND_TERM_MSG = (1<<25),
	NES_CQP_QP_RESET = (1<<31),
};

enum nes_cqp_qp_wqe_word_idx {
	NES_CQP_QP_WQE_CONTEXT_LOW_IDX = 6,
	NES_CQP_QP_WQE_CONTEXT_HIGH_IDX = 7,
	NES_CQP_QP_WQE_FLUSH_SQ_CODE = 8,
	NES_CQP_QP_WQE_FLUSH_RQ_CODE = 9,
	NES_CQP_QP_WQE_NEW_MSS_IDX = 15,
};

enum nes_nic_ctx_bits {
	NES_NIC_CTX_RQ_SIZE_32 = (3<<8),
	NES_NIC_CTX_RQ_SIZE_512 = (3<<8),
	NES_NIC_CTX_SQ_SIZE_32 = (1<<10),
	NES_NIC_CTX_SQ_SIZE_512 = (3<<10),
};

enum nes_nic_qp_ctx_word_idx {
	NES_NIC_CTX_MISC_IDX = 0,
	NES_NIC_CTX_SQ_LOW_IDX = 2,
	NES_NIC_CTX_SQ_HIGH_IDX = 3,
	NES_NIC_CTX_RQ_LOW_IDX = 4,
	NES_NIC_CTX_RQ_HIGH_IDX = 5,
};

enum nes_cqp_cq_bits {
	NES_CQP_CQ_CEQE_MASK = (1<<9),
	NES_CQP_CQ_CEQ_VALID = (1<<10),
	NES_CQP_CQ_RESIZE = (1<<11),
	NES_CQP_CQ_CHK_OVERFLOW = (1<<12),
	NES_CQP_CQ_4KB_CHUNK = (1<<14),
	NES_CQP_CQ_VIRT = (1<<15),
};

enum nes_cqp_stag_bits {
	NES_CQP_STAG_VA_TO = (1<<9),
	NES_CQP_STAG_DEALLOC_PBLS = (1<<10),
	NES_CQP_STAG_PBL_BLK_SIZE = (1<<11),
	NES_CQP_STAG_MR = (1<<13),
	NES_CQP_STAG_RIGHTS_LOCAL_READ = (1<<16),
	NES_CQP_STAG_RIGHTS_LOCAL_WRITE = (1<<17),
	NES_CQP_STAG_RIGHTS_REMOTE_READ = (1<<18),
	NES_CQP_STAG_RIGHTS_REMOTE_WRITE = (1<<19),
	NES_CQP_STAG_RIGHTS_WINDOW_BIND = (1<<20),
	NES_CQP_STAG_REM_ACC_EN = (1<<21),
	NES_CQP_STAG_LEAVE_PENDING = (1<<31),
};

enum nes_cqp_ceq_wqeword_idx {
	NES_CQP_CEQ_WQE_ELEMENT_COUNT_IDX = 1,
	NES_CQP_CEQ_WQE_PBL_LOW_IDX = 6,
	NES_CQP_CEQ_WQE_PBL_HIGH_IDX = 7,
};

enum nes_cqp_ceq_bits {
	NES_CQP_CEQ_4KB_CHUNK = (1<<14),
	NES_CQP_CEQ_VIRT = (1<<15),
};

enum nes_cqp_aeq_wqeword_idx {
	NES_CQP_AEQ_WQE_ELEMENT_COUNT_IDX = 1,
	NES_CQP_AEQ_WQE_PBL_LOW_IDX = 6,
	NES_CQP_AEQ_WQE_PBL_HIGH_IDX = 7,
};

enum nes_cqp_aeq_bits {
	NES_CQP_AEQ_4KB_CHUNK = (1<<14),
	NES_CQP_AEQ_VIRT = (1<<15),
};

enum nes_cqp_lmi_wqeword_idx {
	NES_CQP_LMI_WQE_LMI_OFFSET_IDX = 1,
	NES_CQP_LMI_WQE_FRAG_LOW_IDX = 8,
	NES_CQP_LMI_WQE_FRAG_HIGH_IDX = 9,
	NES_CQP_LMI_WQE_FRAG_LEN_IDX = 10,
};

enum nes_cqp_arp_wqeword_idx {
	NES_CQP_ARP_WQE_MAC_ADDR_LOW_IDX = 6,
	NES_CQP_ARP_WQE_MAC_HIGH_IDX = 7,
	NES_CQP_ARP_WQE_REACHABILITY_MAX_IDX = 1,
};

enum nes_cqp_upload_wqeword_idx {
	NES_CQP_UPLOAD_WQE_CTXT_LOW_IDX = 6,
	NES_CQP_UPLOAD_WQE_CTXT_HIGH_IDX = 7,
	NES_CQP_UPLOAD_WQE_HTE_IDX = 8,
};

enum nes_cqp_arp_bits {
	NES_CQP_ARP_VALID = (1<<8),
	NES_CQP_ARP_PERM = (1<<9),
};

enum nes_cqp_flush_bits {
	NES_CQP_FLUSH_SQ = (1<<30),
	NES_CQP_FLUSH_RQ = (1<<31),
	NES_CQP_FLUSH_MAJ_MIN = (1<<28),
};

enum nes_cqe_opcode_bits {
	NES_CQE_STAG_VALID = (1<<6),
	NES_CQE_ERROR = (1<<7),
	NES_CQE_SQ = (1<<8),
	NES_CQE_SE = (1<<9),
	NES_CQE_PSH = (1<<29),
	NES_CQE_FIN = (1<<30),
	NES_CQE_VALID = (1<<31),
};


enum nes_cqe_word_idx {
	NES_CQE_PAYLOAD_LENGTH_IDX = 0,
	NES_CQE_COMP_COMP_CTX_LOW_IDX = 2,
	NES_CQE_COMP_COMP_CTX_HIGH_IDX = 3,
	NES_CQE_INV_STAG_IDX = 4,
	NES_CQE_QP_ID_IDX = 5,
	NES_CQE_ERROR_CODE_IDX = 6,
	NES_CQE_OPCODE_IDX = 7,
};

enum nes_ceqe_word_idx {
	NES_CEQE_CQ_CTX_LOW_IDX = 0,
	NES_CEQE_CQ_CTX_HIGH_IDX = 1,
};

enum nes_ceqe_status_bit {
	NES_CEQE_VALID = (1<<31),
};

enum nes_int_bits {
	NES_INT_CEQ0 = (1<<0),
	NES_INT_CEQ1 = (1<<1),
	NES_INT_CEQ2 = (1<<2),
	NES_INT_CEQ3 = (1<<3),
	NES_INT_CEQ4 = (1<<4),
	NES_INT_CEQ5 = (1<<5),
	NES_INT_CEQ6 = (1<<6),
	NES_INT_CEQ7 = (1<<7),
	NES_INT_CEQ8 = (1<<8),
	NES_INT_CEQ9 = (1<<9),
	NES_INT_CEQ10 = (1<<10),
	NES_INT_CEQ11 = (1<<11),
	NES_INT_CEQ12 = (1<<12),
	NES_INT_CEQ13 = (1<<13),
	NES_INT_CEQ14 = (1<<14),
	NES_INT_CEQ15 = (1<<15),
	NES_INT_AEQ0 = (1<<16),
	NES_INT_AEQ1 = (1<<17),
	NES_INT_AEQ2 = (1<<18),
	NES_INT_AEQ3 = (1<<19),
	NES_INT_AEQ4 = (1<<20),
	NES_INT_AEQ5 = (1<<21),
	NES_INT_AEQ6 = (1<<22),
	NES_INT_AEQ7 = (1<<23),
	NES_INT_MAC0 = (1<<24),
	NES_INT_MAC1 = (1<<25),
	NES_INT_MAC2 = (1<<26),
	NES_INT_MAC3 = (1<<27),
	NES_INT_TSW = (1<<28),
	NES_INT_TIMER = (1<<29),
	NES_INT_INTF = (1<<30),
};

enum nes_intf_int_bits {
	NES_INTF_INT_PCIERR = (1<<0),
	NES_INTF_PERIODIC_TIMER = (1<<2),
	NES_INTF_ONE_SHOT_TIMER = (1<<3),
	NES_INTF_INT_CRITERR = (1<<14),
	NES_INTF_INT_AEQ0_OFLOW = (1<<16),
	NES_INTF_INT_AEQ1_OFLOW = (1<<17),
	NES_INTF_INT_AEQ2_OFLOW = (1<<18),
	NES_INTF_INT_AEQ3_OFLOW = (1<<19),
	NES_INTF_INT_AEQ4_OFLOW = (1<<20),
	NES_INTF_INT_AEQ5_OFLOW = (1<<21),
	NES_INTF_INT_AEQ6_OFLOW = (1<<22),
	NES_INTF_INT_AEQ7_OFLOW = (1<<23),
	NES_INTF_INT_AEQ_OFLOW = (0xff<<16),
};

enum nes_mac_int_bits {
	NES_MAC_INT_LINK_STAT_CHG = (1<<1),
	NES_MAC_INT_XGMII_EXT = (1<<2),
	NES_MAC_INT_TX_UNDERFLOW = (1<<6),
	NES_MAC_INT_TX_ERROR = (1<<7),
};

enum nes_cqe_allocate_bits {
	NES_CQE_ALLOC_INC_SELECT = (1<<28),
	NES_CQE_ALLOC_NOTIFY_NEXT = (1<<29),
	NES_CQE_ALLOC_NOTIFY_SE = (1<<30),
	NES_CQE_ALLOC_RESET = (1<<31),
};

enum nes_nic_rq_wqe_word_idx {
	NES_NIC_RQ_WQE_LENGTH_1_0_IDX = 0,
	NES_NIC_RQ_WQE_LENGTH_3_2_IDX = 1,
	NES_NIC_RQ_WQE_FRAG0_LOW_IDX = 2,
	NES_NIC_RQ_WQE_FRAG0_HIGH_IDX = 3,
	NES_NIC_RQ_WQE_FRAG1_LOW_IDX = 4,
	NES_NIC_RQ_WQE_FRAG1_HIGH_IDX = 5,
	NES_NIC_RQ_WQE_FRAG2_LOW_IDX = 6,
	NES_NIC_RQ_WQE_FRAG2_HIGH_IDX = 7,
	NES_NIC_RQ_WQE_FRAG3_LOW_IDX = 8,
	NES_NIC_RQ_WQE_FRAG3_HIGH_IDX = 9,
};

enum nes_nic_sq_wqe_word_idx {
	NES_NIC_SQ_WQE_MISC_IDX = 0,
	NES_NIC_SQ_WQE_TOTAL_LENGTH_IDX = 1,
	NES_NIC_SQ_WQE_LSO_INFO_IDX = 2,
	NES_NIC_SQ_WQE_LENGTH_0_TAG_IDX = 3,
	NES_NIC_SQ_WQE_LENGTH_2_1_IDX = 4,
	NES_NIC_SQ_WQE_LENGTH_4_3_IDX = 5,
	NES_NIC_SQ_WQE_FRAG0_LOW_IDX = 6,
	NES_NIC_SQ_WQE_FRAG0_HIGH_IDX = 7,
	NES_NIC_SQ_WQE_FRAG1_LOW_IDX = 8,
	NES_NIC_SQ_WQE_FRAG1_HIGH_IDX = 9,
	NES_NIC_SQ_WQE_FRAG2_LOW_IDX = 10,
	NES_NIC_SQ_WQE_FRAG2_HIGH_IDX = 11,
	NES_NIC_SQ_WQE_FRAG3_LOW_IDX = 12,
	NES_NIC_SQ_WQE_FRAG3_HIGH_IDX = 13,
	NES_NIC_SQ_WQE_FRAG4_LOW_IDX = 14,
	NES_NIC_SQ_WQE_FRAG4_HIGH_IDX = 15,
};

enum nes_iwarp_sq_wqe_word_idx {
	NES_IWARP_SQ_WQE_MISC_IDX = 0,
	NES_IWARP_SQ_WQE_TOTAL_PAYLOAD_IDX = 1,
	NES_IWARP_SQ_WQE_COMP_CTX_LOW_IDX = 2,
	NES_IWARP_SQ_WQE_COMP_CTX_HIGH_IDX = 3,
	NES_IWARP_SQ_WQE_COMP_SCRATCH_LOW_IDX = 4,
	NES_IWARP_SQ_WQE_COMP_SCRATCH_HIGH_IDX = 5,
	NES_IWARP_SQ_WQE_INV_STAG_LOW_IDX = 7,
	NES_IWARP_SQ_WQE_RDMA_TO_LOW_IDX = 8,
	NES_IWARP_SQ_WQE_RDMA_TO_HIGH_IDX = 9,
	NES_IWARP_SQ_WQE_RDMA_LENGTH_IDX = 10,
	NES_IWARP_SQ_WQE_RDMA_STAG_IDX = 11,
	NES_IWARP_SQ_WQE_IMM_DATA_START_IDX = 12,
	NES_IWARP_SQ_WQE_FRAG0_LOW_IDX = 16,
	NES_IWARP_SQ_WQE_FRAG0_HIGH_IDX = 17,
	NES_IWARP_SQ_WQE_LENGTH0_IDX = 18,
	NES_IWARP_SQ_WQE_STAG0_IDX = 19,
	NES_IWARP_SQ_WQE_FRAG1_LOW_IDX = 20,
	NES_IWARP_SQ_WQE_FRAG1_HIGH_IDX = 21,
	NES_IWARP_SQ_WQE_LENGTH1_IDX = 22,
	NES_IWARP_SQ_WQE_STAG1_IDX = 23,
	NES_IWARP_SQ_WQE_FRAG2_LOW_IDX = 24,
	NES_IWARP_SQ_WQE_FRAG2_HIGH_IDX = 25,
	NES_IWARP_SQ_WQE_LENGTH2_IDX = 26,
	NES_IWARP_SQ_WQE_STAG2_IDX = 27,
	NES_IWARP_SQ_WQE_FRAG3_LOW_IDX = 28,
	NES_IWARP_SQ_WQE_FRAG3_HIGH_IDX = 29,
	NES_IWARP_SQ_WQE_LENGTH3_IDX = 30,
	NES_IWARP_SQ_WQE_STAG3_IDX = 31,
};

enum nes_iwarp_sq_bind_wqe_word_idx {
	NES_IWARP_SQ_BIND_WQE_MR_IDX = 6,
	NES_IWARP_SQ_BIND_WQE_MW_IDX = 7,
	NES_IWARP_SQ_BIND_WQE_LENGTH_LOW_IDX = 8,
	NES_IWARP_SQ_BIND_WQE_LENGTH_HIGH_IDX = 9,
	NES_IWARP_SQ_BIND_WQE_VA_FBO_LOW_IDX = 10,
	NES_IWARP_SQ_BIND_WQE_VA_FBO_HIGH_IDX = 11,
};

enum nes_iwarp_sq_fmr_wqe_word_idx {
	NES_IWARP_SQ_FMR_WQE_MR_STAG_IDX = 7,
	NES_IWARP_SQ_FMR_WQE_LENGTH_LOW_IDX = 8,
	NES_IWARP_SQ_FMR_WQE_LENGTH_HIGH_IDX = 9,
	NES_IWARP_SQ_FMR_WQE_VA_FBO_LOW_IDX = 10,
	NES_IWARP_SQ_FMR_WQE_VA_FBO_HIGH_IDX = 11,
	NES_IWARP_SQ_FMR_WQE_PBL_ADDR_LOW_IDX = 12,
	NES_IWARP_SQ_FMR_WQE_PBL_ADDR_HIGH_IDX = 13,
	NES_IWARP_SQ_FMR_WQE_PBL_LENGTH_IDX = 14,
};

enum nes_iwarp_sq_fmr_opcodes {
	NES_IWARP_SQ_FMR_WQE_ZERO_BASED			= (1<<6),
	NES_IWARP_SQ_FMR_WQE_PAGE_SIZE_4K		= (0<<7),
	NES_IWARP_SQ_FMR_WQE_PAGE_SIZE_2M		= (1<<7),
	NES_IWARP_SQ_FMR_WQE_RIGHTS_ENABLE_LOCAL_READ	= (1<<16),
	NES_IWARP_SQ_FMR_WQE_RIGHTS_ENABLE_LOCAL_WRITE 	= (1<<17),
	NES_IWARP_SQ_FMR_WQE_RIGHTS_ENABLE_REMOTE_READ 	= (1<<18),
	NES_IWARP_SQ_FMR_WQE_RIGHTS_ENABLE_REMOTE_WRITE = (1<<19),
	NES_IWARP_SQ_FMR_WQE_RIGHTS_ENABLE_WINDOW_BIND 	= (1<<20),
};

#define NES_IWARP_SQ_FMR_WQE_MR_LENGTH_HIGH_MASK	0xFF;

enum nes_iwarp_sq_locinv_wqe_word_idx {
	NES_IWARP_SQ_LOCINV_WQE_INV_STAG_IDX = 6,
};

enum nes_iwarp_rq_wqe_word_idx {
	NES_IWARP_RQ_WQE_TOTAL_PAYLOAD_IDX = 1,
	NES_IWARP_RQ_WQE_COMP_CTX_LOW_IDX = 2,
	NES_IWARP_RQ_WQE_COMP_CTX_HIGH_IDX = 3,
	NES_IWARP_RQ_WQE_COMP_SCRATCH_LOW_IDX = 4,
	NES_IWARP_RQ_WQE_COMP_SCRATCH_HIGH_IDX = 5,
	NES_IWARP_RQ_WQE_FRAG0_LOW_IDX = 8,
	NES_IWARP_RQ_WQE_FRAG0_HIGH_IDX = 9,
	NES_IWARP_RQ_WQE_LENGTH0_IDX = 10,
	NES_IWARP_RQ_WQE_STAG0_IDX = 11,
	NES_IWARP_RQ_WQE_FRAG1_LOW_IDX = 12,
	NES_IWARP_RQ_WQE_FRAG1_HIGH_IDX = 13,
	NES_IWARP_RQ_WQE_LENGTH1_IDX = 14,
	NES_IWARP_RQ_WQE_STAG1_IDX = 15,
	NES_IWARP_RQ_WQE_FRAG2_LOW_IDX = 16,
	NES_IWARP_RQ_WQE_FRAG2_HIGH_IDX = 17,
	NES_IWARP_RQ_WQE_LENGTH2_IDX = 18,
	NES_IWARP_RQ_WQE_STAG2_IDX = 19,
	NES_IWARP_RQ_WQE_FRAG3_LOW_IDX = 20,
	NES_IWARP_RQ_WQE_FRAG3_HIGH_IDX = 21,
	NES_IWARP_RQ_WQE_LENGTH3_IDX = 22,
	NES_IWARP_RQ_WQE_STAG3_IDX = 23,
};

enum nes_nic_sq_wqe_bits {
	NES_NIC_SQ_WQE_PHDR_CS_READY =  (1<<21),
	NES_NIC_SQ_WQE_LSO_ENABLE = (1<<22),
	NES_NIC_SQ_WQE_TAGVALUE_ENABLE = (1<<23),
	NES_NIC_SQ_WQE_DISABLE_CHKSUM = (1<<30),
	NES_NIC_SQ_WQE_COMPLETION = (1<<31),
};

enum nes_nic_cqe_word_idx {
	NES_NIC_CQE_ACCQP_ID_IDX = 0,
	NES_NIC_CQE_HASH_RCVNXT = 1,
	NES_NIC_CQE_TAG_PKT_TYPE_IDX = 2,
	NES_NIC_CQE_MISC_IDX = 3,
};

#define NES_PKT_TYPE_APBVT_BITS 0xC112
#define NES_PKT_TYPE_APBVT_MASK 0xff3e

#define NES_PKT_TYPE_PVALID_BITS 0x10000000
#define NES_PKT_TYPE_PVALID_MASK 0x30000000

#define NES_PKT_TYPE_TCPV4_BITS 0x0110
#define NES_PKT_TYPE_TCPV4_MASK 0x3f30

#define NES_PKT_TYPE_UDPV4_BITS 0x0210
#define NES_PKT_TYPE_UDPV4_MASK 0x3f30

#define NES_PKT_TYPE_IPV4_BITS  0x0010
#define NES_PKT_TYPE_IPV4_MASK  0x3f30

#define NES_PKT_TYPE_OTHER_BITS 0x0000
#define NES_PKT_TYPE_OTHER_MASK 0x0030

#define NES_NIC_CQE_ERRV_SHIFT 16
enum nes_nic_ev_bits {
	NES_NIC_ERRV_BITS_MODE = (1<<0),
	NES_NIC_ERRV_BITS_IPV4_CSUM_ERR = (1<<1),
	NES_NIC_ERRV_BITS_TCPUDP_CSUM_ERR = (1<<2),
	NES_NIC_ERRV_BITS_WQE_OVERRUN = (1<<3),
	NES_NIC_ERRV_BITS_IPH_ERR = (1<<4),
};

enum nes_nic_cqe_bits {
	NES_NIC_CQE_ERRV_MASK = (0xff<<NES_NIC_CQE_ERRV_SHIFT),
	NES_NIC_CQE_SQ = (1<<24),
	NES_NIC_CQE_ACCQP_PORT = (1<<28),
	NES_NIC_CQE_ACCQP_VALID = (1<<29),
	NES_NIC_CQE_TAG_VALID = (1<<30),
	NES_NIC_CQE_VALID = (1<<31),
};

enum nes_aeqe_word_idx {
	NES_AEQE_COMP_CTXT_LOW_IDX = 0,
	NES_AEQE_COMP_CTXT_HIGH_IDX = 1,
	NES_AEQE_COMP_QP_CQ_ID_IDX = 2,
	NES_AEQE_MISC_IDX = 3,
};

enum nes_aeqe_bits {
	NES_AEQE_QP = (1<<16),
	NES_AEQE_CQ = (1<<17),
	NES_AEQE_SQ = (1<<18),
	NES_AEQE_INBOUND_RDMA = (1<<19),
	NES_AEQE_IWARP_STATE_MASK = (7<<20),
	NES_AEQE_TCP_STATE_MASK = (0xf<<24),
	NES_AEQE_Q2_DATA_WRITTEN = (0x3<<28),
	NES_AEQE_VALID = (1<<31),
};

#define NES_AEQE_IWARP_STATE_SHIFT	20
#define NES_AEQE_TCP_STATE_SHIFT	24
#define NES_AEQE_Q2_DATA_ETHERNET       (1<<28)
#define NES_AEQE_Q2_DATA_MPA            (1<<29)

enum nes_aeqe_iwarp_state {
	NES_AEQE_IWARP_STATE_NON_EXISTANT = 0,
	NES_AEQE_IWARP_STATE_IDLE = 1,
	NES_AEQE_IWARP_STATE_RTS = 2,
	NES_AEQE_IWARP_STATE_CLOSING = 3,
	NES_AEQE_IWARP_STATE_TERMINATE = 5,
	NES_AEQE_IWARP_STATE_ERROR = 6
};

enum nes_aeqe_tcp_state {
	NES_AEQE_TCP_STATE_NON_EXISTANT = 0,
	NES_AEQE_TCP_STATE_CLOSED = 1,
	NES_AEQE_TCP_STATE_LISTEN = 2,
	NES_AEQE_TCP_STATE_SYN_SENT = 3,
	NES_AEQE_TCP_STATE_SYN_RCVD = 4,
	NES_AEQE_TCP_STATE_ESTABLISHED = 5,
	NES_AEQE_TCP_STATE_CLOSE_WAIT = 6,
	NES_AEQE_TCP_STATE_FIN_WAIT_1 = 7,
	NES_AEQE_TCP_STATE_CLOSING = 8,
	NES_AEQE_TCP_STATE_LAST_ACK = 9,
	NES_AEQE_TCP_STATE_FIN_WAIT_2 = 10,
	NES_AEQE_TCP_STATE_TIME_WAIT = 11
};

enum nes_aeqe_aeid {
	NES_AEQE_AEID_AMP_UNALLOCATED_STAG                            = 0x0102,
	NES_AEQE_AEID_AMP_INVALID_STAG                                = 0x0103,
	NES_AEQE_AEID_AMP_BAD_QP                                      = 0x0104,
	NES_AEQE_AEID_AMP_BAD_PD                                      = 0x0105,
	NES_AEQE_AEID_AMP_BAD_STAG_KEY                                = 0x0106,
	NES_AEQE_AEID_AMP_BAD_STAG_INDEX                              = 0x0107,
	NES_AEQE_AEID_AMP_BOUNDS_VIOLATION                            = 0x0108,
	NES_AEQE_AEID_AMP_RIGHTS_VIOLATION                            = 0x0109,
	NES_AEQE_AEID_AMP_TO_WRAP                                     = 0x010a,
	NES_AEQE_AEID_AMP_FASTREG_SHARED                              = 0x010b,
	NES_AEQE_AEID_AMP_FASTREG_VALID_STAG                          = 0x010c,
	NES_AEQE_AEID_AMP_FASTREG_MW_STAG                             = 0x010d,
	NES_AEQE_AEID_AMP_FASTREG_INVALID_RIGHTS                      = 0x010e,
	NES_AEQE_AEID_AMP_FASTREG_PBL_TABLE_OVERFLOW                  = 0x010f,
	NES_AEQE_AEID_AMP_FASTREG_INVALID_LENGTH                      = 0x0110,
	NES_AEQE_AEID_AMP_INVALIDATE_SHARED                           = 0x0111,
	NES_AEQE_AEID_AMP_INVALIDATE_NO_REMOTE_ACCESS_RIGHTS          = 0x0112,
	NES_AEQE_AEID_AMP_INVALIDATE_MR_WITH_BOUND_WINDOWS            = 0x0113,
	NES_AEQE_AEID_AMP_MWBIND_VALID_STAG                           = 0x0114,
	NES_AEQE_AEID_AMP_MWBIND_OF_MR_STAG                           = 0x0115,
	NES_AEQE_AEID_AMP_MWBIND_TO_ZERO_BASED_STAG                   = 0x0116,
	NES_AEQE_AEID_AMP_MWBIND_TO_MW_STAG                           = 0x0117,
	NES_AEQE_AEID_AMP_MWBIND_INVALID_RIGHTS                       = 0x0118,
	NES_AEQE_AEID_AMP_MWBIND_INVALID_BOUNDS                       = 0x0119,
	NES_AEQE_AEID_AMP_MWBIND_TO_INVALID_PARENT                    = 0x011a,
	NES_AEQE_AEID_AMP_MWBIND_BIND_DISABLED                        = 0x011b,
	NES_AEQE_AEID_BAD_CLOSE                                       = 0x0201,
	NES_AEQE_AEID_RDMAP_ROE_BAD_LLP_CLOSE                         = 0x0202,
	NES_AEQE_AEID_CQ_OPERATION_ERROR                              = 0x0203,
	NES_AEQE_AEID_PRIV_OPERATION_DENIED                           = 0x0204,
	NES_AEQE_AEID_RDMA_READ_WHILE_ORD_ZERO                        = 0x0205,
	NES_AEQE_AEID_STAG_ZERO_INVALID                               = 0x0206,
	NES_AEQE_AEID_DDP_INVALID_MSN_GAP_IN_MSN                      = 0x0301,
	NES_AEQE_AEID_DDP_INVALID_MSN_RANGE_IS_NOT_VALID              = 0x0302,
	NES_AEQE_AEID_DDP_UBE_DDP_MESSAGE_TOO_LONG_FOR_AVAILABLE_BUFFER = 0x0303,
	NES_AEQE_AEID_DDP_UBE_INVALID_DDP_VERSION                     = 0x0304,
	NES_AEQE_AEID_DDP_UBE_INVALID_MO                              = 0x0305,
	NES_AEQE_AEID_DDP_UBE_INVALID_MSN_NO_BUFFER_AVAILABLE         = 0x0306,
	NES_AEQE_AEID_DDP_UBE_INVALID_QN                              = 0x0307,
	NES_AEQE_AEID_DDP_NO_L_BIT                                    = 0x0308,
	NES_AEQE_AEID_RDMAP_ROE_INVALID_RDMAP_VERSION                 = 0x0311,
	NES_AEQE_AEID_RDMAP_ROE_UNEXPECTED_OPCODE                     = 0x0312,
	NES_AEQE_AEID_ROE_INVALID_RDMA_READ_REQUEST                   = 0x0313,
	NES_AEQE_AEID_ROE_INVALID_RDMA_WRITE_OR_READ_RESP             = 0x0314,
	NES_AEQE_AEID_INVALID_ARP_ENTRY                               = 0x0401,
	NES_AEQE_AEID_INVALID_TCP_OPTION_RCVD                         = 0x0402,
	NES_AEQE_AEID_STALE_ARP_ENTRY                                 = 0x0403,
	NES_AEQE_AEID_LLP_CLOSE_COMPLETE                              = 0x0501,
	NES_AEQE_AEID_LLP_CONNECTION_RESET                            = 0x0502,
	NES_AEQE_AEID_LLP_FIN_RECEIVED                                = 0x0503,
	NES_AEQE_AEID_LLP_RECEIVED_MARKER_AND_LENGTH_FIELDS_DONT_MATCH =  0x0504,
	NES_AEQE_AEID_LLP_RECEIVED_MPA_CRC_ERROR                      = 0x0505,
	NES_AEQE_AEID_LLP_SEGMENT_TOO_LARGE                           = 0x0506,
	NES_AEQE_AEID_LLP_SEGMENT_TOO_SMALL                           = 0x0507,
	NES_AEQE_AEID_LLP_SYN_RECEIVED                                = 0x0508,
	NES_AEQE_AEID_LLP_TERMINATE_RECEIVED                          = 0x0509,
	NES_AEQE_AEID_LLP_TOO_MANY_RETRIES                            = 0x050a,
	NES_AEQE_AEID_LLP_TOO_MANY_KEEPALIVE_RETRIES                  = 0x050b,
	NES_AEQE_AEID_RESET_SENT                                      = 0x0601,
	NES_AEQE_AEID_TERMINATE_SENT                                  = 0x0602,
	NES_AEQE_AEID_DDP_LCE_LOCAL_CATASTROPHIC                      = 0x0700
};

enum nes_iwarp_sq_opcodes {
	NES_IWARP_SQ_WQE_WRPDU = (1<<15),
	NES_IWARP_SQ_WQE_PSH = (1<<21),
	NES_IWARP_SQ_WQE_STREAMING = (1<<23),
	NES_IWARP_SQ_WQE_IMM_DATA = (1<<28),
	NES_IWARP_SQ_WQE_READ_FENCE = (1<<29),
	NES_IWARP_SQ_WQE_LOCAL_FENCE = (1<<30),
	NES_IWARP_SQ_WQE_SIGNALED_COMPL = (1<<31),
};

enum nes_iwarp_sq_wqe_bits {
	NES_IWARP_SQ_OP_RDMAW = 0,
	NES_IWARP_SQ_OP_RDMAR = 1,
	NES_IWARP_SQ_OP_SEND = 3,
	NES_IWARP_SQ_OP_SENDINV = 4,
	NES_IWARP_SQ_OP_SENDSE = 5,
	NES_IWARP_SQ_OP_SENDSEINV = 6,
	NES_IWARP_SQ_OP_BIND = 8,
	NES_IWARP_SQ_OP_FAST_REG = 9,
	NES_IWARP_SQ_OP_LOCINV = 10,
	NES_IWARP_SQ_OP_RDMAR_LOCINV = 11,
	NES_IWARP_SQ_OP_NOP = 12,
};

enum nes_iwarp_cqe_major_code {
	NES_IWARP_CQE_MAJOR_FLUSH = 1,
	NES_IWARP_CQE_MAJOR_DRV = 0x8000
};

enum nes_iwarp_cqe_minor_code {
	NES_IWARP_CQE_MINOR_FLUSH = 1
};

#define NES_EEPROM_READ_REQUEST (1<<16)
#define NES_MAC_ADDR_VALID      (1<<20)

/*
 * NES index registers init values.
 */
struct nes_init_values {
	u32 index;
	u32 data;
	u8  wrt;
};

/*
 * NES registers in BAR0.
 */
struct nes_pci_regs {
	u32 int_status;
	u32 int_mask;
	u32 int_pending;
	u32 intf_int_status;
	u32 intf_int_mask;
	u32 other_regs[59];	 /* pad out to 256 bytes for now */
};

#define NES_CQP_SQ_SIZE    128
#define NES_CCQ_SIZE       128
#define NES_NIC_WQ_SIZE    512
#define NES_NIC_CTX_SIZE   ((NES_NIC_CTX_RQ_SIZE_512) | (NES_NIC_CTX_SQ_SIZE_512))
#define NES_NIC_BACK_STORE 0x00038000

struct nes_device;

struct nes_hw_nic_qp_context {
	__le32 context_words[6];
};

struct nes_hw_nic_sq_wqe {
	__le32 wqe_words[16];
};

struct nes_hw_nic_rq_wqe {
	__le32 wqe_words[16];
};

struct nes_hw_nic_cqe {
	__le32 cqe_words[4];
};

struct nes_hw_cqp_qp_context {
	__le32 context_words[4];
};

struct nes_hw_cqp_wqe {
	__le32 wqe_words[16];
};

struct nes_hw_qp_wqe {
	__le32 wqe_words[32];
};

struct nes_hw_cqe {
	__le32 cqe_words[8];
};

struct nes_hw_ceqe {
	__le32 ceqe_words[2];
};

struct nes_hw_aeqe {
	__le32 aeqe_words[4];
};

struct nes_cqp_request {
	union {
		u64 cqp_callback_context;
		void *cqp_callback_pointer;
	};
	wait_queue_head_t     waitq;
	struct nes_hw_cqp_wqe cqp_wqe;
	struct list_head      list;
	atomic_t              refcount;
	void (*cqp_callback)(struct nes_device *nesdev, struct nes_cqp_request *cqp_request);
	u16                   major_code;
	u16                   minor_code;
	u8                    waiting;
	u8                    request_done;
	u8                    dynamic;
	u8                    callback;
};

struct nes_hw_cqp {
	struct nes_hw_cqp_wqe *sq_vbase;
	dma_addr_t            sq_pbase;
	spinlock_t            lock;
	wait_queue_head_t     waitq;
	u16                   qp_id;
	u16                   sq_head;
	u16                   sq_tail;
	u16                   sq_size;
};

#define NES_FIRST_FRAG_SIZE 128
struct nes_first_frag {
	u8 buffer[NES_FIRST_FRAG_SIZE];
};

struct nes_hw_nic {
	struct nes_first_frag    *first_frag_vbase;	/* virtual address of first frags */
	struct nes_hw_nic_sq_wqe *sq_vbase;			/* virtual address of sq */
	struct nes_hw_nic_rq_wqe *rq_vbase;			/* virtual address of rq */
	struct sk_buff           *tx_skb[NES_NIC_WQ_SIZE];
	struct sk_buff           *rx_skb[NES_NIC_WQ_SIZE];
	dma_addr_t frag_paddr[NES_NIC_WQ_SIZE];
	unsigned long first_frag_overflow[BITS_TO_LONGS(NES_NIC_WQ_SIZE)];
	dma_addr_t sq_pbase;			/* PCI memory for host rings */
	dma_addr_t rq_pbase;			/* PCI memory for host rings */

	u16 qp_id;
	u16 sq_head;
	u16 sq_tail;
	u16 sq_size;
	u16 rq_head;
	u16 rq_tail;
	u16 rq_size;
	u8 replenishing_rq;
	u8 reserved;

	spinlock_t rq_lock;
};

struct nes_hw_nic_cq {
	struct nes_hw_nic_cqe volatile *cq_vbase;	/* PCI memory for host rings */
	void (*ce_handler)(struct nes_device *nesdev, struct nes_hw_nic_cq *cq);
	dma_addr_t cq_pbase;	/* PCI memory for host rings */
	int rx_cqes_completed;
	int cqe_allocs_pending;
	int rx_pkts_indicated;
	u16 cq_head;
	u16 cq_size;
	u16 cq_number;
	u8  cqes_pending;
};

struct nes_hw_qp {
	struct nes_hw_qp_wqe *sq_vbase;		/* PCI memory for host rings */
	struct nes_hw_qp_wqe *rq_vbase;		/* PCI memory for host rings */
	void                 *q2_vbase;			/* PCI memory for host rings */
	dma_addr_t sq_pbase;	/* PCI memory for host rings */
	dma_addr_t rq_pbase;	/* PCI memory for host rings */
	dma_addr_t q2_pbase;	/* PCI memory for host rings */
	u32 qp_id;
	u16 sq_head;
	u16 sq_tail;
	u16 sq_size;
	u16 rq_head;
	u16 rq_tail;
	u16 rq_size;
	u8  rq_encoded_size;
	u8  sq_encoded_size;
};

struct nes_hw_cq {
	struct nes_hw_cqe *cq_vbase;	/* PCI memory for host rings */
	void (*ce_handler)(struct nes_device *nesdev, struct nes_hw_cq *cq);
	dma_addr_t cq_pbase;	/* PCI memory for host rings */
	u16 cq_head;
	u16 cq_size;
	u16 cq_number;
};

struct nes_hw_ceq {
	struct nes_hw_ceqe volatile *ceq_vbase;	/* PCI memory for host rings */
	dma_addr_t ceq_pbase;	/* PCI memory for host rings */
	u16 ceq_head;
	u16 ceq_size;
};

struct nes_hw_aeq {
	struct nes_hw_aeqe volatile *aeq_vbase;	/* PCI memory for host rings */
	dma_addr_t aeq_pbase;	/* PCI memory for host rings */
	u16 aeq_head;
	u16 aeq_size;
};

struct nic_qp_map {
	u8 qpid;
	u8 nic_index;
	u8 logical_port;
	u8 is_hnic;
};

#define	NES_CQP_ARP_AEQ_INDEX_MASK  0x000f0000
#define	NES_CQP_ARP_AEQ_INDEX_SHIFT 16

#define NES_CQP_APBVT_ADD			0x00008000
#define NES_CQP_APBVT_NIC_SHIFT		16

#define NES_ARP_ADD     1
#define NES_ARP_DELETE  2
#define NES_ARP_RESOLVE 3

#define NES_MAC_SW_IDLE      0
#define NES_MAC_SW_INTERRUPT 1
#define NES_MAC_SW_MH        2

struct nes_arp_entry {
	u32 ip_addr;
	u8  mac_addr[ETH_ALEN];
};

#define NES_NIC_FAST_TIMER          96
#define NES_NIC_FAST_TIMER_LOW      40
#define NES_NIC_FAST_TIMER_HIGH     1000
#define DEFAULT_NES_QL_HIGH         256
#define DEFAULT_NES_QL_LOW          16
#define DEFAULT_NES_QL_TARGET       64
#define DEFAULT_JUMBO_NES_QL_LOW    12
#define DEFAULT_JUMBO_NES_QL_TARGET 40
#define DEFAULT_JUMBO_NES_QL_HIGH   128
#define NES_NIC_CQ_DOWNWARD_TREND   16
#define NES_PFT_SIZE		    48

#define NES_MGT_WQ_COUNT 32
#define NES_MGT_CTX_SIZE ((NES_NIC_CTX_RQ_SIZE_32) | (NES_NIC_CTX_SQ_SIZE_32))
#define NES_MGT_QP_OFFSET 36
#define NES_MGT_QP_COUNT 4

struct nes_hw_tune_timer {
    /* u16 cq_count; */
    u16 threshold_low;
    u16 threshold_target;
    u16 threshold_high;
    u16 timer_in_use;
    u16 timer_in_use_old;
    u16 timer_in_use_min;
    u16 timer_in_use_max;
    u8  timer_direction_upward;
    u8  timer_direction_downward;
    u16 cq_count_old;
    u8  cq_direction_downward;
};

#define NES_TIMER_INT_LIMIT         2
#define NES_TIMER_INT_LIMIT_DYNAMIC 10
#define NES_TIMER_ENABLE_LIMIT      4
#define NES_MAX_LINK_INTERRUPTS     128
#define NES_MAX_LINK_CHECK          200
#define NES_MAX_LRO_DESCRIPTORS     32
#define NES_LRO_MAX_AGGR            64

struct nes_adapter {
	u64              fw_ver;
	unsigned long    *allocated_qps;
	unsigned long    *allocated_cqs;
	unsigned long    *allocated_mrs;
	unsigned long    *allocated_pds;
	unsigned long    *allocated_arps;
	struct nes_qp    **qp_table;
	struct workqueue_struct *work_q;

	struct list_head list;
	struct list_head active_listeners;
	/* list of the netdev's associated with each logical port */
	struct list_head nesvnic_list[4];

	struct timer_list  mh_timer;
	struct timer_list  lc_timer;
	struct work_struct work;
	spinlock_t         resource_lock;
	spinlock_t         phy_lock;
	spinlock_t         pbl_lock;
	spinlock_t         periodic_timer_lock;

	struct nes_arp_entry arp_table[NES_MAX_ARP_TABLE_SIZE];

	/* Adapter CEQ and AEQs */
	struct nes_hw_ceq ceq[16];
	struct nes_hw_aeq aeq[8];

	struct nes_hw_tune_timer tune_timer;

	unsigned long doorbell_start;

	u32 hw_rev;
	u32 vendor_id;
	u32 vendor_part_id;
	u32 device_cap_flags;
	u32 tick_delta;
	u32 timer_int_req;
	u32 arp_table_size;
	u32 next_arp_index;

	u32 max_mr;
	u32 max_256pbl;
	u32 max_4kpbl;
	u32 free_256pbl;
	u32 free_4kpbl;
	u32 max_mr_size;
	u32 max_qp;
	u32 next_qp;
	u32 max_irrq;
	u32 max_qp_wr;
	u32 max_sge;
	u32 max_cq;
	u32 next_cq;
	u32 max_cqe;
	u32 max_pd;
	u32 base_pd;
	u32 next_pd;
	u32 hte_index_mask;

	/* EEPROM information */
	u32 rx_pool_size;
	u32 tx_pool_size;
	u32 rx_threshold;
	u32 tcp_timer_core_clk_divisor;
	u32 iwarp_config;
	u32 cm_config;
	u32 sws_timer_config;
	u32 tcp_config1;
	u32 wqm_wat;
	u32 core_clock;
	u32 firmware_version;
	u32 eeprom_version;

	u32 nic_rx_eth_route_err;

	u32 et_rx_coalesce_usecs;
	u32 et_rx_max_coalesced_frames;
	u32 et_rx_coalesce_usecs_irq;
	u32 et_rx_max_coalesced_frames_irq;
	u32 et_pkt_rate_low;
	u32 et_rx_coalesce_usecs_low;
	u32 et_rx_max_coalesced_frames_low;
	u32 et_pkt_rate_high;
	u32 et_rx_coalesce_usecs_high;
	u32 et_rx_max_coalesced_frames_high;
	u32 et_rate_sample_interval;
	u32 timer_int_limit;
	u32 wqm_quanta;
	u8 allow_unaligned_fpdus;

	/* Adapter base MAC address */
	u32 mac_addr_low;
	u16 mac_addr_high;

	u16 firmware_eeprom_offset;
	u16 software_eeprom_offset;

	u16 max_irrq_wr;

	/* pd config for each port */
	u16 pd_config_size[4];
	u16 pd_config_base[4];

	u16 link_interrupt_count[4];
	u8 crit_error_count[32];

	/* the phy index for each port */
	u8  phy_index[4];
	u8  mac_sw_state[4];
	u8  mac_link_down[4];
	u8  phy_type[4];
	u8  log_port;

	/* PCI information */
	unsigned int  devfn;
	unsigned char bus_number;
	unsigned char OneG_Mode;

	unsigned char ref_count;
	u8            netdev_count;
	u8            netdev_max;	/* from host nic address count in EEPROM */
	u8            port_count;
	u8            virtwq;
	u8            send_term_ok;
	u8            et_use_adaptive_rx_coalesce;
	u8            adapter_fcn_count;
	u8 pft_mcast_map[NES_PFT_SIZE];
};

struct nes_pbl {
	u64              *pbl_vbase;
	dma_addr_t       pbl_pbase;
	struct page      *page;
	unsigned long    user_base;
	u32              pbl_size;
	struct list_head list;
	/* TODO: need to add list for two level tables */
};

#define NES_4K_PBL_CHUNK_SIZE	4096

struct nes_fast_mr_wqe_pbl {
	u64		*kva;
	dma_addr_t	paddr;
};

struct nes_ib_fast_reg_page_list {
	struct ib_fast_reg_page_list	ibfrpl;
	struct nes_fast_mr_wqe_pbl 	nes_wqe_pbl;
	u64 				pbl;
};

struct nes_listener {
	struct work_struct      work;
	struct workqueue_struct *wq;
	struct nes_vnic         *nesvnic;
	struct iw_cm_id         *cm_id;
	struct list_head        list;
	unsigned long           socket;
	u8                      accept_failed;
};

struct nes_ib_device;

#define NES_EVENT_DELAY msecs_to_jiffies(100)

struct nes_vnic {
	struct nes_ib_device *nesibdev;
	u64 sq_full;
	u64 tso_requests;
	u64 segmented_tso_requests;
	u64 linearized_skbs;
	u64 tx_sw_dropped;
	u64 endnode_nstat_rx_discard;
	u64 endnode_nstat_rx_octets;
	u64 endnode_nstat_rx_frames;
	u64 endnode_nstat_tx_octets;
	u64 endnode_nstat_tx_frames;
	u64 endnode_ipv4_tcp_retransmits;
	/* void *mem; */
	struct nes_device *nesdev;
	struct net_device *netdev;
	atomic_t          rx_skbs_needed;
	atomic_t          rx_skb_timer_running;
	int               budget;
	u32               msg_enable;
	/* u32 tx_avail; */
	__be32            local_ipaddr;
	struct napi_struct   napi;
	spinlock_t           tx_lock;	/* could use netdev tx lock? */
	struct timer_list    rq_wqes_timer;
	u32                  nic_mem_size;
	void                 *nic_vbase;
	dma_addr_t           nic_pbase;
	struct nes_hw_nic    nic;
	struct nes_hw_nic_cq nic_cq;
	u32    mcrq_qp_id;
	struct nes_ucontext *mcrq_ucontext;
	struct nes_cqp_request* (*get_cqp_request)(struct nes_device *nesdev);
	void (*post_cqp_request)(struct nes_device*, struct nes_cqp_request *);
	int (*mcrq_mcast_filter)( struct nes_vnic* nesvnic, __u8* dmi_addr );
	struct net_device_stats netstats;
	/* used to put the netdev on the adapters logical port list */
	struct list_head list;
	u16 max_frame_size;
	u8  netdev_open;
	u8  linkup;
	u8  logical_port;
	u8  netdev_index;  /* might not be needed, indexes nesdev->netdev */
	u8  perfect_filter_index;
	u8  nic_index;
	u8  qp_nic_index[4];
	u8  next_qp_nic_index;
	u8  of_device_registered;
	u8  rdma_enabled;
	u32 lro_max_aggr;
	struct net_lro_mgr lro_mgr;
	struct net_lro_desc lro_desc[NES_MAX_LRO_DESCRIPTORS];
	struct timer_list event_timer;
	enum ib_event_type delayed_event;
	enum ib_event_type last_dispatched_event;
	spinlock_t port_ibevent_lock;
	u32 mgt_mem_size;
	void *mgt_vbase;
	dma_addr_t mgt_pbase;
	struct nes_vnic_mgt *mgtvnic[NES_MGT_QP_COUNT];
	struct task_struct *mgt_thread;
	wait_queue_head_t mgt_wait_queue;
	struct sk_buff_head mgt_skb_list;

};

struct nes_ib_device {
	struct ib_device ibdev;
	struct nes_vnic *nesvnic;

	/* Virtual RNIC Limits */
	u32 max_mr;
	u32 max_qp;
	u32 max_cq;
	u32 max_pd;
	u32 num_mr;
	u32 num_qp;
	u32 num_cq;
	u32 num_pd;
};

enum nes_hdrct_flags {
	DDP_LEN_FLAG                    = 0x80,
	DDP_HDR_FLAG                    = 0x40,
	RDMA_HDR_FLAG                   = 0x20
};

enum nes_term_layers {
	LAYER_RDMA			= 0,
	LAYER_DDP			= 1,
	LAYER_MPA			= 2
};

enum nes_term_error_types {
	RDMAP_CATASTROPHIC		= 0,
	RDMAP_REMOTE_PROT		= 1,
	RDMAP_REMOTE_OP			= 2,
	DDP_CATASTROPHIC		= 0,
	DDP_TAGGED_BUFFER		= 1,
	DDP_UNTAGGED_BUFFER		= 2,
	DDP_LLP				= 3
};

enum nes_term_rdma_errors {
	RDMAP_INV_STAG			= 0x00,
	RDMAP_INV_BOUNDS		= 0x01,
	RDMAP_ACCESS			= 0x02,
	RDMAP_UNASSOC_STAG		= 0x03,
	RDMAP_TO_WRAP			= 0x04,
	RDMAP_INV_RDMAP_VER		= 0x05,
	RDMAP_UNEXPECTED_OP		= 0x06,
	RDMAP_CATASTROPHIC_LOCAL	= 0x07,
	RDMAP_CATASTROPHIC_GLOBAL	= 0x08,
	RDMAP_CANT_INV_STAG		= 0x09,
	RDMAP_UNSPECIFIED		= 0xff
};

enum nes_term_ddp_errors {
	DDP_CATASTROPHIC_LOCAL		= 0x00,
	DDP_TAGGED_INV_STAG		= 0x00,
	DDP_TAGGED_BOUNDS		= 0x01,
	DDP_TAGGED_UNASSOC_STAG		= 0x02,
	DDP_TAGGED_TO_WRAP		= 0x03,
	DDP_TAGGED_INV_DDP_VER		= 0x04,
	DDP_UNTAGGED_INV_QN		= 0x01,
	DDP_UNTAGGED_INV_MSN_NO_BUF	= 0x02,
	DDP_UNTAGGED_INV_MSN_RANGE	= 0x03,
	DDP_UNTAGGED_INV_MO		= 0x04,
	DDP_UNTAGGED_INV_TOO_LONG	= 0x05,
	DDP_UNTAGGED_INV_DDP_VER	= 0x06
};

enum nes_term_mpa_errors {
	MPA_CLOSED			= 0x01,
	MPA_CRC				= 0x02,
	MPA_MARKER			= 0x03,
	MPA_REQ_RSP			= 0x04,
};

struct nes_terminate_hdr {
	u8 layer_etype;
	u8 error_code;
	u8 hdrct;
	u8 rsvd;
};

/* Used to determine how to fill in terminate error codes */
#define IWARP_OPCODE_WRITE		0
#define IWARP_OPCODE_READREQ		1
#define IWARP_OPCODE_READRSP		2
#define IWARP_OPCODE_SEND		3
#define IWARP_OPCODE_SEND_INV		4
#define IWARP_OPCODE_SEND_SE		5
#define IWARP_OPCODE_SEND_SE_INV	6
#define IWARP_OPCODE_TERM		7

/* These values are used only during terminate processing */
#define TERM_DDP_LEN_TAGGED	14
#define TERM_DDP_LEN_UNTAGGED	18
#define TERM_RDMA_LEN		28
#define RDMA_OPCODE_MASK	0x0f
#define RDMA_READ_REQ_OPCODE	1
#define BAD_FRAME_OFFSET	64
#define CQE_MAJOR_DRV		0x8000

/* Used for link status recheck after interrupt processing */
#define NES_LINK_RECHECK_DELAY	msecs_to_jiffies(50)
#define NES_LINK_RECHECK_MAX	60

#endif		/* __NES_HW_H */
