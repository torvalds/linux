/*
 * drivers/net/ethernet/mellanox/mlxsw/pci.h
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015 Jiri Pirko <jiri@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MLXSW_PCI_H
#define _MLXSW_PCI_H

#include <linux/bitops.h>

#include "item.h"

#define PCI_DEVICE_ID_MELLANOX_SWITCHX2	0xc738
#define PCI_DEVICE_ID_MELLANOX_SPECTRUM	0xcb84
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

#define MLXSW_PCI_SW_RESET			0xF0010
#define MLXSW_PCI_SW_RESET_RST_BIT		BIT(0)
#define MLXSW_PCI_SW_RESET_TIMEOUT_MSECS	5000

#define MLXSW_PCI_DOORBELL_SDQ_OFFSET		0x000
#define MLXSW_PCI_DOORBELL_RDQ_OFFSET		0x200
#define MLXSW_PCI_DOORBELL_CQ_OFFSET		0x400
#define MLXSW_PCI_DOORBELL_EQ_OFFSET		0x600
#define MLXSW_PCI_DOORBELL_ARM_CQ_OFFSET	0x800
#define MLXSW_PCI_DOORBELL_ARM_EQ_OFFSET	0xA00

#define MLXSW_PCI_DOORBELL(offset, type_offset, num)	\
	((offset) + (type_offset) + (num) * 4)

#define MLXSW_PCI_CQS_MAX	96
#define MLXSW_PCI_EQS_COUNT	2
#define MLXSW_PCI_EQ_ASYNC_NUM	0
#define MLXSW_PCI_EQ_COMP_NUM	1

#define MLXSW_PCI_AQ_PAGES	8
#define MLXSW_PCI_AQ_SIZE	(MLXSW_PCI_PAGE_SIZE * MLXSW_PCI_AQ_PAGES)
#define MLXSW_PCI_WQE_SIZE	32 /* 32 bytes per element */
#define MLXSW_PCI_CQE_SIZE	16 /* 16 bytes per element */
#define MLXSW_PCI_EQE_SIZE	16 /* 16 bytes per element */
#define MLXSW_PCI_WQE_COUNT	(MLXSW_PCI_AQ_SIZE / MLXSW_PCI_WQE_SIZE)
#define MLXSW_PCI_CQE_COUNT	(MLXSW_PCI_AQ_SIZE / MLXSW_PCI_CQE_SIZE)
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

/* pci_cqe_lag
 * Packet arrives from a port which is a LAG
 */
MLXSW_ITEM32(pci, cqe, lag, 0x00, 23, 1);

/* pci_cqe_system_port
 * When lag=0: System port on which the packet was received
 * When lag=1:
 * bits [15:4] LAG ID on which the packet was received
 * bits [3:0] sub_port on which the packet was received
 */
MLXSW_ITEM32(pci, cqe, system_port, 0x00, 0, 16);

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
MLXSW_ITEM32(pci, cqe, trap_id, 0x08, 0, 8);

/* pci_cqe_crc
 * Length include CRC. Indicates the length field includes
 * the packet's CRC.
 */
MLXSW_ITEM32(pci, cqe, crc, 0x0C, 8, 1);

/* pci_cqe_e
 * CQE with Error.
 */
MLXSW_ITEM32(pci, cqe, e, 0x0C, 7, 1);

/* pci_cqe_sr
 * 1 - Send Queue
 * 0 - Receive Queue
 */
MLXSW_ITEM32(pci, cqe, sr, 0x0C, 6, 1);

/* pci_cqe_dqn
 * Descriptor Queue (DQ) Number.
 */
MLXSW_ITEM32(pci, cqe, dqn, 0x0C, 1, 5);

/* pci_cqe_owner
 * Ownership bit.
 */
MLXSW_ITEM32(pci, cqe, owner, 0x0C, 0, 1);

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
 * Completion Queue that triggeret this EQE.
 */
MLXSW_ITEM32(pci, eqe, cqn, 0x0C, 8, 7);

/* pci_eqe_owner
 * Ownership bit.
 */
MLXSW_ITEM32(pci, eqe, owner, 0x0C, 0, 1);

/* pci_eqe_cmd_token
 * Command completion event - token
 */
MLXSW_ITEM32(pci, eqe, cmd_token, 0x08, 16, 16);

/* pci_eqe_cmd_status
 * Command completion event - status
 */
MLXSW_ITEM32(pci, eqe, cmd_status, 0x08, 0, 8);

/* pci_eqe_cmd_out_param_h
 * Command completion event - output parameter - higher part
 */
MLXSW_ITEM32(pci, eqe, cmd_out_param_h, 0x0C, 0, 32);

/* pci_eqe_cmd_out_param_l
 * Command completion event - output parameter - lower part
 */
MLXSW_ITEM32(pci, eqe, cmd_out_param_l, 0x10, 0, 32);

#endif
