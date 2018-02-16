/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File hw_atl_b0_internal.h: Definition of Atlantic B0 chip specific
 * constants.
 */

#ifndef HW_ATL_B0_INTERNAL_H
#define HW_ATL_B0_INTERNAL_H

#include "../aq_common.h"

#define HW_ATL_B0_MTU_JUMBO  16352U
#define HW_ATL_B0_MTU        1514U

#define HW_ATL_B0_TX_RINGS 4U
#define HW_ATL_B0_RX_RINGS 4U

#define HW_ATL_B0_RINGS_MAX 32U
#define HW_ATL_B0_TXD_SIZE       (16U)
#define HW_ATL_B0_RXD_SIZE       (16U)

#define HW_ATL_B0_MAC      0U
#define HW_ATL_B0_MAC_MIN  1U
#define HW_ATL_B0_MAC_MAX  33U

/* UCAST/MCAST filters */
#define HW_ATL_B0_UCAST_FILTERS_MAX 38
#define HW_ATL_B0_MCAST_FILTERS_MAX 8

/* interrupts */
#define HW_ATL_B0_ERR_INT 8U
#define HW_ATL_B0_INT_MASK  (0xFFFFFFFFU)

#define HW_ATL_B0_TXD_CTL2_LEN        (0xFFFFC000)
#define HW_ATL_B0_TXD_CTL2_CTX_EN     (0x00002000)
#define HW_ATL_B0_TXD_CTL2_CTX_IDX    (0x00001000)

#define HW_ATL_B0_TXD_CTL_DESC_TYPE_TXD   (0x00000001)
#define HW_ATL_B0_TXD_CTL_DESC_TYPE_TXC   (0x00000002)
#define HW_ATL_B0_TXD_CTL_BLEN        (0x000FFFF0)
#define HW_ATL_B0_TXD_CTL_DD          (0x00100000)
#define HW_ATL_B0_TXD_CTL_EOP         (0x00200000)

#define HW_ATL_B0_TXD_CTL_CMD_X       (0x3FC00000)

#define HW_ATL_B0_TXD_CTL_CMD_VLAN    BIT(22)
#define HW_ATL_B0_TXD_CTL_CMD_FCS     BIT(23)
#define HW_ATL_B0_TXD_CTL_CMD_IPCSO   BIT(24)
#define HW_ATL_B0_TXD_CTL_CMD_TUCSO   BIT(25)
#define HW_ATL_B0_TXD_CTL_CMD_LSO     BIT(26)
#define HW_ATL_B0_TXD_CTL_CMD_WB      BIT(27)
#define HW_ATL_B0_TXD_CTL_CMD_VXLAN   BIT(28)

#define HW_ATL_B0_TXD_CTL_CMD_IPV6    BIT(21)
#define HW_ATL_B0_TXD_CTL_CMD_TCP     BIT(22)

#define HW_ATL_B0_MPI_CONTROL_ADR       0x0368U
#define HW_ATL_B0_MPI_STATE_ADR         0x036CU

#define HW_ATL_B0_MPI_SPEED_MSK         0xFFFFU
#define HW_ATL_B0_MPI_SPEED_SHIFT       16U

#define HW_ATL_B0_RATE_10G              BIT(0)
#define HW_ATL_B0_RATE_5G               BIT(1)
#define HW_ATL_B0_RATE_2G5              BIT(3)
#define HW_ATL_B0_RATE_1G               BIT(4)
#define HW_ATL_B0_RATE_100M             BIT(5)

#define HW_ATL_B0_TXBUF_MAX  160U
#define HW_ATL_B0_RXBUF_MAX  320U

#define HW_ATL_B0_RSS_REDIRECTION_MAX 64U
#define HW_ATL_B0_RSS_REDIRECTION_BITS 3U
#define HW_ATL_B0_RSS_HASHKEY_BITS 320U

#define HW_ATL_B0_TCRSS_4_8  1
#define HW_ATL_B0_TC_MAX 1U
#define HW_ATL_B0_RSS_MAX 8U

#define HW_ATL_B0_LRO_RXD_MAX 2U
#define HW_ATL_B0_RS_SLIP_ENABLED  0U

/* (256k -1(max pay_len) - 54(header)) */
#define HAL_ATL_B0_LSO_MAX_SEGMENT_SIZE 262089U

/* (256k -1(max pay_len) - 74(header)) */
#define HAL_ATL_B0_LSO_IPV6_MAX_SEGMENT_SIZE 262069U

#define HW_ATL_B0_CHIP_REVISION_B0      0xA0U
#define HW_ATL_B0_CHIP_REVISION_UNKNOWN 0xFFU

#define HW_ATL_B0_FW_SEMA_RAM           0x2U

#define HW_ATL_B0_TXC_LEN_TUNLEN    (0x0000FF00)
#define HW_ATL_B0_TXC_LEN_OUTLEN    (0xFFFF0000)

#define HW_ATL_B0_TXC_CTL_DESC_TYPE (0x00000007)
#define HW_ATL_B0_TXC_CTL_CTX_ID    (0x00000008)
#define HW_ATL_B0_TXC_CTL_VLAN      (0x000FFFF0)
#define HW_ATL_B0_TXC_CTL_CMD       (0x00F00000)
#define HW_ATL_B0_TXC_CTL_L2LEN     (0x7F000000)

#define HW_ATL_B0_TXC_CTL_L3LEN     (0x80000000)	/* L3LEN lsb */
#define HW_ATL_B0_TXC_LEN2_L3LEN    (0x000000FF)	/* L3LE upper bits */
#define HW_ATL_B0_TXC_LEN2_L4LEN    (0x0000FF00)
#define HW_ATL_B0_TXC_LEN2_MSSLEN   (0xFFFF0000)

#define HW_ATL_B0_RXD_DD    (0x1)
#define HW_ATL_B0_RXD_NCEA0 (0x1)

#define HW_ATL_B0_RXD_WB_STAT_RSSTYPE (0x0000000F)
#define HW_ATL_B0_RXD_WB_STAT_PKTTYPE (0x00000FF0)
#define HW_ATL_B0_RXD_WB_STAT_RXCTRL  (0x00180000)
#define HW_ATL_B0_RXD_WB_STAT_SPLHDR  (0x00200000)
#define HW_ATL_B0_RXD_WB_STAT_HDRLEN  (0xFFC00000)

#define HW_ATL_B0_RXD_WB_STAT2_DD      (0x0001)
#define HW_ATL_B0_RXD_WB_STAT2_EOP     (0x0002)
#define HW_ATL_B0_RXD_WB_STAT2_RXSTAT  (0x003C)
#define HW_ATL_B0_RXD_WB_STAT2_MACERR  (0x0004)
#define HW_ATL_B0_RXD_WB_STAT2_IP4ERR  (0x0008)
#define HW_ATL_B0_RXD_WB_STAT2_TCPUPDERR  (0x0010)
#define HW_ATL_B0_RXD_WB_STAT2_RXESTAT (0x0FC0)
#define HW_ATL_B0_RXD_WB_STAT2_RSCCNT  (0xF000)

#define L2_FILTER_ACTION_DISCARD (0x0)
#define L2_FILTER_ACTION_HOST    (0x1)

#define HW_ATL_B0_UCP_0X370_REG  (0x370)

#define HW_ATL_B0_FLUSH() AQ_HW_READ_REG(self, 0x10)

#define HW_ATL_B0_FW_VER_EXPECTED 0x01050006U

#define HW_ATL_INTR_MODER_MAX  0x1FF
#define HW_ATL_INTR_MODER_MIN  0xFF

/* HW layer capabilities */

#endif /* HW_ATL_B0_INTERNAL_H */
