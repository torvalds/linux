/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 */

/* File hw_atl_a0_internal.h: Definition of Atlantic A0 chip specific
 * constants.
 */

#ifndef HW_ATL_A0_INTERNAL_H
#define HW_ATL_A0_INTERNAL_H

#include "../aq_common.h"

#define HW_ATL_A0_MTU_JUMBO 9014U

#define HW_ATL_A0_TX_RINGS 4U
#define HW_ATL_A0_RX_RINGS 4U

#define HW_ATL_A0_RINGS_MAX 32U
#define HW_ATL_A0_TXD_SIZE  16U
#define HW_ATL_A0_RXD_SIZE  16U

#define HW_ATL_A0_MAC      0U
#define HW_ATL_A0_MAC_MIN  1U
#define HW_ATL_A0_MAC_MAX  33U

/* interrupts */
#define HW_ATL_A0_ERR_INT 8U
#define HW_ATL_A0_INT_MASK  0xFFFFFFFFU

#define HW_ATL_A0_TXD_CTL2_LEN        0xFFFFC000U
#define HW_ATL_A0_TXD_CTL2_CTX_EN     0x00002000U
#define HW_ATL_A0_TXD_CTL2_CTX_IDX    0x00001000U

#define HW_ATL_A0_TXD_CTL_DESC_TYPE_TXD   0x00000001U
#define HW_ATL_A0_TXD_CTL_DESC_TYPE_TXC   0x00000002U
#define HW_ATL_A0_TXD_CTL_BLEN        0x000FFFF0U
#define HW_ATL_A0_TXD_CTL_DD          0x00100000U
#define HW_ATL_A0_TXD_CTL_EOP         0x00200000U

#define HW_ATL_A0_TXD_CTL_CMD_X       0x3FC00000U

#define HW_ATL_A0_TXD_CTL_CMD_VLAN    BIT(22)
#define HW_ATL_A0_TXD_CTL_CMD_FCS     BIT(23)
#define HW_ATL_A0_TXD_CTL_CMD_IPCSO   BIT(24)
#define HW_ATL_A0_TXD_CTL_CMD_TUCSO   BIT(25)
#define HW_ATL_A0_TXD_CTL_CMD_LSO     BIT(26)
#define HW_ATL_A0_TXD_CTL_CMD_WB      BIT(27)
#define HW_ATL_A0_TXD_CTL_CMD_VXLAN   BIT(28)

#define HW_ATL_A0_TXD_CTL_CMD_IPV6    BIT(21)
#define HW_ATL_A0_TXD_CTL_CMD_TCP     BIT(22)

#define HW_ATL_A0_MPI_CONTROL_ADR     0x0368U
#define HW_ATL_A0_MPI_STATE_ADR       0x036CU

#define HW_ATL_A0_MPI_SPEED_MSK       0xFFFFU
#define HW_ATL_A0_MPI_SPEED_SHIFT     16U

#define HW_ATL_A0_TXBUF_MAX 160U
#define HW_ATL_A0_RXBUF_MAX 320U

#define HW_ATL_A0_RSS_REDIRECTION_MAX 64U
#define HW_ATL_A0_RSS_REDIRECTION_BITS 3U

#define HW_ATL_A0_TC_MAX 1U
#define HW_ATL_A0_RSS_MAX 8U

#define HW_ATL_A0_FW_SEMA_RAM           0x2U

#define HW_ATL_A0_RXD_DD    0x1U
#define HW_ATL_A0_RXD_NCEA0 0x1U

#define HW_ATL_A0_RXD_WB_STAT2_EOP     0x0002U

#define HW_ATL_A0_UCP_0X370_REG  0x370U

#define HW_ATL_A0_FW_VER_EXPECTED 0x01050006U

#define HW_ATL_A0_MIN_RXD \
	(ALIGN(AQ_CFG_SKB_FRAGS_MAX + 1U, AQ_HW_RXD_MULTIPLE))
#define HW_ATL_A0_MIN_TXD \
	(ALIGN(AQ_CFG_SKB_FRAGS_MAX + 1U, AQ_HW_TXD_MULTIPLE))

#define HW_ATL_A0_MAX_RXD 8184U
#define HW_ATL_A0_MAX_TXD 8184U

#endif /* HW_ATL_A0_INTERNAL_H */
