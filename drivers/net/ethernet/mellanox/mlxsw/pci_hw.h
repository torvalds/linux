/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2015-2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_PCI_HW_H
#define _MLXSW_PCI_HW_H

#include <linux/bitops.h>

#include "item.h"

#define MLXSW_PCI_BAR0_SIZE		(1024 * 1024) /* 1MB */
#define MLXSW_PCI_PAGE_SIZE		4096

#define MLXSW_PCI_CIR_BASE			0x71000
#define MLXSW_PCI_CIR_IN_PARAM_HI		MLXSW_PCI_CIR_BASE
#define MLXSW_PCI_CIR_IN_PARAM_LO		(MLXSW_PCI_CIR_BASE + 0x04)
#define MLXSW_PCI_CIR_IN_MODIFIER		(MLXSW_PCI_CIR_BASE + 0x08)
#define MLXSW_PCI_CIR_OUT_PARAM_HI		(MLXSW_PCI_CIR_BASE + 0x0C)
#define MLXSW_PCI_CIR_OUT_PARAM_LO		(MLXSW_PCI_CIR_BASE + 0x10)
#define MLXSW_PCI_CIR_TOKEN			(MLXSW_PCI_CIR_BASE + 0x14)
#define MLXSW_PCI_CIR_CTRL			(MLXSW_PCI_CIR_BASE + 0x18)
#define MLXSW_PCI_CIR_CTRL_GO_BIT		BIT(23)
#define MLXSW_PCI_CIR_CTRL_EVREQ_BIT		BIT(22)
#define MLXSW_PCI_CIR_CTRL_OPCODE_MOD_SHIFT	12
#define MLXSW_PCI_CIR_CTRL_STATUS_SHIFT		24
#define MLXSW_PCI_CIR_TIMEOUT_MSECS		1000

#define MLXSW_PCI_SW_RESET_TIMEOUT_MSECS	900000
#define MLXSW_PCI_SW_RESET_WAIT_MSECS		200
#define MLXSW_PCI_FW_READY			0xA1844
#define MLXSW_PCI_FW_READY_MASK			0xFFFF
#define MLXSW_PCI_FW_READY_MAGIC		0x5E

#define MLXSW_PCI_DOORBELL_SDQ_OFFSET		0x000
#define MLXSW_PCI_DOORBELL_RDQ_OFFSET		0x200
#define MLXSW_PCI_DOORBELL_CQ_OFFSET		0x400
#define MLXSW_PCI_DOORBELL_EQ_OFFSET		0x600
#define MLXSW_PCI_DOORBELL_ARM_CQ_OFFSET	0x800
#define MLXSW_PCI_DOORBELL_ARM_EQ_OFFSET	0xA00

#define MLXSW_PCI_DOORBELL(offset, type_offset, num)	\
	((offset) + (type_offset) + (num) * 4)

#define MLXSW_PCI_FREE_RUNNING_CLOCK_H(offset)	(offset)
#define MLXSW_PCI_FREE_RUNNING_CLOCK_L(offset)	((offset) + 4)

#define MLXSW_PCI_CQS_MAX	96
#define MLXSW_PCI_EQS_COUNT	2
#define MLXSW_PCI_EQ_ASYNC_NUM	0
#define MLXSW_PCI_EQ_COMP_NUM	1

#define MLXSW_PCI_SDQS_MIN	2 /* EMAD and control traffic */
#define MLXSW_PCI_SDQ_EMAD_INDEX	0
#define MLXSW_PCI_SDQ_EMAD_TC	0
#define MLXSW_PCI_SDQ_CTL_TC	3

#define MLXSW_PCI_AQ_PAGES	8
#define MLXSW_PCI_AQ_SIZE	(MLXSW_PCI_PAGE_SIZE * MLXSW_PCI_AQ_PAGES)
#define MLXSW_PCI_WQE_SIZE	32 /* 32 bytes per element */
#define MLXSW_PCI_CQE01_SIZE	16 /* 16 bytes per element */
#define MLXSW_PCI_CQE2_SIZE	32 /* 32 bytes per element */
#define MLXSW_PCI_CQE_SIZE_MAX	MLXSW_PCI_CQE2_SIZE
#define MLXSW_PCI_EQE_SIZE	16 /* 16 bytes per element */
#define MLXSW_PCI_WQE_COUNT	(MLXSW_PCI_AQ_SIZE / MLXSW_PCI_WQE_SIZE)
#define MLXSW_PCI_CQE01_COUNT	(MLXSW_PCI_AQ_SIZE / MLXSW_PCI_CQE01_SIZE)
#define MLXSW_PCI_CQE2_COUNT	(MLXSW_PCI_AQ_SIZE / MLXSW_PCI_CQE2_SIZE)
#define MLXSW_PCI_EQE_COUNT	(MLXSW_PCI_AQ_SIZE / MLXSW_PCI_EQE_SIZE)
#define MLXSW_PCI_EQE_UPDATE_COUNT	0x80

#define MLXSW_PCI_WQE_SG_ENTRIES	3
#define MLXSW_PCI_WQE_TYPE_ETHERNET	0xA

/* pci_wqe_c
 * If set it indicates that a completion should be reported upon
 * execution of this descriptor.
 */
MLXSW_ITEM32(pci, wqe, c, 0x00, 31, 1);

/* pci_wqe_lp
 * Local Processing, set if packet should be processed by the local
 * switch hardware:
 * For Ethernet EMAD (Direct Route and non Direct Route) -
 * must be set if packet destination is local device
 * For InfiniBand CTL - must be set if packet destination is local device
 * Otherwise it must be clear
 * Local Process packets must not exceed the size of 2K (including payload
 * and headers).
 */
MLXSW_ITEM32(pci, wqe, lp, 0x00, 30, 1);

/* pci_wqe_type
 * Packet type.
 */
MLXSW_ITEM32(pci, wqe, type, 0x00, 23, 4);

/* pci_wqe_byte_count
 * Size of i-th scatter/gather entry, 0 if entry is unused.
 */
MLXSW_ITEM16_INDEXED(pci, wqe, byte_count, 0x02, 0, 14, 0x02, 0x00, false);

/* pci_wqe_address
 * Physical address of i-th scatter/gather entry.
 * Gather Entries must be 2Byte aligned.
 */
MLXSW_ITEM64_INDEXED(pci, wqe, address, 0x08, 0, 64, 0x8, 0x0, false);

enum mlxsw_pci_cqe_v {
	MLXSW_PCI_CQE_V0,
	MLXSW_PCI_CQE_V1,
	MLXSW_PCI_CQE_V2,
};

#define mlxsw_pci_cqe_item_helpers(name, v0, v1, v2)				\
static inline u32 mlxsw_pci_cqe_##name##_get(enum mlxsw_pci_cqe_v v, char *cqe)	\
{										\
	switch (v) {								\
	default:								\
	case MLXSW_PCI_CQE_V0:							\
		return mlxsw_pci_cqe##v0##_##name##_get(cqe);			\
	case MLXSW_PCI_CQE_V1:							\
		return mlxsw_pci_cqe##v1##_##name##_get(cqe);			\
	case MLXSW_PCI_CQE_V2:							\
		return mlxsw_pci_cqe##v2##_##name##_get(cqe);			\
	}									\
}										\
static inline void mlxsw_pci_cqe_##name##_set(enum mlxsw_pci_cqe_v v,		\
					      char *cqe, u32 val)		\
{										\
	switch (v) {								\
	default:								\
	case MLXSW_PCI_CQE_V0:							\
		mlxsw_pci_cqe##v0##_##name##_set(cqe, val);			\
		break;								\
	case MLXSW_PCI_CQE_V1:							\
		mlxsw_pci_cqe##v1##_##name##_set(cqe, val);			\
		break;								\
	case MLXSW_PCI_CQE_V2:							\
		mlxsw_pci_cqe##v2##_##name##_set(cqe, val);			\
		break;								\
	}									\
}

/* pci_cqe_lag
 * Packet arrives from a port which is a LAG
 */
MLXSW_ITEM32(pci, cqe0, lag, 0x00, 23, 1);
MLXSW_ITEM32(pci, cqe12, lag, 0x00, 24, 1);
mlxsw_pci_cqe_item_helpers(lag, 0, 12, 12);

/* pci_cqe_system_port/lag_id
 * When lag=0: System port on which the packet was received
 * When lag=1:
 * bits [15:4] LAG ID on which the packet was received
 * bits [3:0] sub_port on which the packet was received
 */
MLXSW_ITEM32(pci, cqe, system_port, 0x00, 0, 16);
MLXSW_ITEM32(pci, cqe0, lag_id, 0x00, 4, 12);
MLXSW_ITEM32(pci, cqe12, lag_id, 0x00, 0, 16);
mlxsw_pci_cqe_item_helpers(lag_id, 0, 12, 12);
MLXSW_ITEM32(pci, cqe0, lag_subport, 0x00, 0, 4);
MLXSW_ITEM32(pci, cqe12, lag_subport, 0x00, 16, 8);
mlxsw_pci_cqe_item_helpers(lag_subport, 0, 12, 12);

/* pci_cqe_wqe_counter
 * WQE count of the WQEs completed on the associated dqn
 */
MLXSW_ITEM32(pci, cqe, wqe_counter, 0x04, 16, 16);

/* pci_cqe_byte_count
 * Byte count of received packets including additional two
 * Reserved Bytes that are append to the end of the frame.
 * Reserved for Send CQE.
 */
MLXSW_ITEM32(pci, cqe, byte_count, 0x04, 0, 14);

/* pci_cqe_trap_id
 * Trap ID that captured the packet.
 */
MLXSW_ITEM32(pci, cqe, trap_id, 0x08, 0, 10);

/* pci_cqe_crc
 * Length include CRC. Indicates the length field includes
 * the packet's CRC.
 */
MLXSW_ITEM32(pci, cqe0, crc, 0x0C, 8, 1);
MLXSW_ITEM32(pci, cqe12, crc, 0x0C, 9, 1);
mlxsw_pci_cqe_item_helpers(crc, 0, 12, 12);

/* pci_cqe_e
 * CQE with Error.
 */
MLXSW_ITEM32(pci, cqe0, e, 0x0C, 7, 1);
MLXSW_ITEM32(pci, cqe12, e, 0x00, 27, 1);
mlxsw_pci_cqe_item_helpers(e, 0, 12, 12);

/* pci_cqe_sr
 * 1 - Send Queue
 * 0 - Receive Queue
 */
MLXSW_ITEM32(pci, cqe0, sr, 0x0C, 6, 1);
MLXSW_ITEM32(pci, cqe12, sr, 0x00, 26, 1);
mlxsw_pci_cqe_item_helpers(sr, 0, 12, 12);

/* pci_cqe_dqn
 * Descriptor Queue (DQ) Number.
 */
MLXSW_ITEM32(pci, cqe0, dqn, 0x0C, 1, 5);
MLXSW_ITEM32(pci, cqe12, dqn, 0x0C, 1, 6);
mlxsw_pci_cqe_item_helpers(dqn, 0, 12, 12);

/* pci_cqe_user_def_val_orig_pkt_len
 * When trap_id is an ACL: User defined value from policy engine action.
 */
MLXSW_ITEM32(pci, cqe2, user_def_val_orig_pkt_len, 0x14, 0, 20);

/* pci_cqe_mirror_reason
 * Mirror reason.
 */
MLXSW_ITEM32(pci, cqe2, mirror_reason, 0x18, 24, 8);

/* pci_cqe_owner
 * Ownership bit.
 */
MLXSW_ITEM32(pci, cqe01, owner, 0x0C, 0, 1);
MLXSW_ITEM32(pci, cqe2, owner, 0x1C, 0, 1);
mlxsw_pci_cqe_item_helpers(owner, 01, 01, 2);

/* pci_eqe_event_type
 * Event type.
 */
MLXSW_ITEM32(pci, eqe, event_type, 0x0C, 24, 8);
#define MLXSW_PCI_EQE_EVENT_TYPE_COMP	0x00
#define MLXSW_PCI_EQE_EVENT_TYPE_CMD	0x0A

/* pci_eqe_event_sub_type
 * Event type.
 */
MLXSW_ITEM32(pci, eqe, event_sub_type, 0x0C, 16, 8);

/* pci_eqe_cqn
 * Completion Queue that triggered this EQE.
 */
MLXSW_ITEM32(pci, eqe, cqn, 0x0C, 8, 7);

/* pci_eqe_owner
 * Ownership bit.
 */
MLXSW_ITEM32(pci, eqe, owner, 0x0C, 0, 1);

/* pci_eqe_cmd_token
 * Command completion event - token
 */
MLXSW_ITEM32(pci, eqe, cmd_token, 0x00, 16, 16);

/* pci_eqe_cmd_status
 * Command completion event - status
 */
MLXSW_ITEM32(pci, eqe, cmd_status, 0x00, 0, 8);

/* pci_eqe_cmd_out_param_h
 * Command completion event - output parameter - higher part
 */
MLXSW_ITEM32(pci, eqe, cmd_out_param_h, 0x04, 0, 32);

/* pci_eqe_cmd_out_param_l
 * Command completion event - output parameter - lower part
 */
MLXSW_ITEM32(pci, eqe, cmd_out_param_l, 0x08, 0, 32);

#endif
