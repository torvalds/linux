/*****************************************************************************
 *                                                                           *
 * File: regs.h                                                              *
 * $Revision: 1.8 $                                                          *
 * $Date: 2005/06/21 18:29:48 $                                              *
 * Description:                                                              *
 *  part of the Chelsio 10Gb Ethernet Driver.                                *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify      *
 * it under the terms of the GNU General Public License, version 2, as       *
 * published by the Free Software Foundation.                                *
 *                                                                           *
 * You should have received a copy of the GNU General Public License along   *
 * with this program; if not, write to the Free Software Foundation, Inc.,   *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.                 *
 *                                                                           *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED    *
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF      *
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.                     *
 *                                                                           *
 * http://www.chelsio.com                                                    *
 *                                                                           *
 * Copyright (c) 2003 - 2005 Chelsio Communications, Inc.                    *
 * All rights reserved.                                                      *
 *                                                                           *
 * Maintainers: maintainers@chelsio.com                                      *
 *                                                                           *
 * Authors: Dimitrios Michailidis   <dm@chelsio.com>                         *
 *          Tina Yang               <tainay@chelsio.com>                     *
 *          Felix Marti             <felix@chelsio.com>                      *
 *          Scott Bardone           <sbardone@chelsio.com>                   *
 *          Kurt Ottaway            <kottaway@chelsio.com>                   *
 *          Frank DiMambro          <frank@chelsio.com>                      *
 *                                                                           *
 * History:                                                                  *
 *                                                                           *
 ****************************************************************************/

#ifndef _CXGB_REGS_H_
#define _CXGB_REGS_H_

/* SGE registers */
#define A_SG_CONTROL 0x0

#define S_CMDQ0_ENABLE    0
#define V_CMDQ0_ENABLE(x) ((x) << S_CMDQ0_ENABLE)
#define F_CMDQ0_ENABLE    V_CMDQ0_ENABLE(1U)

#define S_CMDQ1_ENABLE    1
#define V_CMDQ1_ENABLE(x) ((x) << S_CMDQ1_ENABLE)
#define F_CMDQ1_ENABLE    V_CMDQ1_ENABLE(1U)

#define S_FL0_ENABLE    2
#define V_FL0_ENABLE(x) ((x) << S_FL0_ENABLE)
#define F_FL0_ENABLE    V_FL0_ENABLE(1U)

#define S_FL1_ENABLE    3
#define V_FL1_ENABLE(x) ((x) << S_FL1_ENABLE)
#define F_FL1_ENABLE    V_FL1_ENABLE(1U)

#define S_CPL_ENABLE    4
#define V_CPL_ENABLE(x) ((x) << S_CPL_ENABLE)
#define F_CPL_ENABLE    V_CPL_ENABLE(1U)

#define S_RESPONSE_QUEUE_ENABLE    5
#define V_RESPONSE_QUEUE_ENABLE(x) ((x) << S_RESPONSE_QUEUE_ENABLE)
#define F_RESPONSE_QUEUE_ENABLE    V_RESPONSE_QUEUE_ENABLE(1U)

#define S_CMDQ_PRIORITY    6
#define M_CMDQ_PRIORITY    0x3
#define V_CMDQ_PRIORITY(x) ((x) << S_CMDQ_PRIORITY)
#define G_CMDQ_PRIORITY(x) (((x) >> S_CMDQ_PRIORITY) & M_CMDQ_PRIORITY)

#define S_DISABLE_CMDQ1_GTS    9
#define V_DISABLE_CMDQ1_GTS(x) ((x) << S_DISABLE_CMDQ1_GTS)
#define F_DISABLE_CMDQ1_GTS    V_DISABLE_CMDQ1_GTS(1U)

#define S_DISABLE_FL0_GTS    10
#define V_DISABLE_FL0_GTS(x) ((x) << S_DISABLE_FL0_GTS)
#define F_DISABLE_FL0_GTS    V_DISABLE_FL0_GTS(1U)

#define S_DISABLE_FL1_GTS    11
#define V_DISABLE_FL1_GTS(x) ((x) << S_DISABLE_FL1_GTS)
#define F_DISABLE_FL1_GTS    V_DISABLE_FL1_GTS(1U)

#define S_ENABLE_BIG_ENDIAN    12
#define V_ENABLE_BIG_ENDIAN(x) ((x) << S_ENABLE_BIG_ENDIAN)
#define F_ENABLE_BIG_ENDIAN    V_ENABLE_BIG_ENDIAN(1U)

#define S_ISCSI_COALESCE    14
#define V_ISCSI_COALESCE(x) ((x) << S_ISCSI_COALESCE)
#define F_ISCSI_COALESCE    V_ISCSI_COALESCE(1U)

#define S_RX_PKT_OFFSET    15
#define V_RX_PKT_OFFSET(x) ((x) << S_RX_PKT_OFFSET)

#define S_VLAN_XTRACT    18
#define V_VLAN_XTRACT(x) ((x) << S_VLAN_XTRACT)
#define F_VLAN_XTRACT    V_VLAN_XTRACT(1U)

#define A_SG_DOORBELL 0x4
#define A_SG_CMD0BASELWR 0x8
#define A_SG_CMD0BASEUPR 0xc
#define A_SG_CMD1BASELWR 0x10
#define A_SG_CMD1BASEUPR 0x14
#define A_SG_FL0BASELWR 0x18
#define A_SG_FL0BASEUPR 0x1c
#define A_SG_FL1BASELWR 0x20
#define A_SG_FL1BASEUPR 0x24
#define A_SG_CMD0SIZE 0x28
#define A_SG_FL0SIZE 0x2c
#define A_SG_RSPSIZE 0x30
#define A_SG_RSPBASELWR 0x34
#define A_SG_RSPBASEUPR 0x38
#define A_SG_FLTHRESHOLD 0x3c
#define A_SG_RSPQUEUECREDIT 0x40
#define A_SG_SLEEPING 0x48
#define A_SG_INTRTIMER 0x4c
#define A_SG_CMD1SIZE 0xb0
#define A_SG_FL1SIZE 0xb4
#define A_SG_INT_ENABLE 0xb8

#define S_RESPQ_EXHAUSTED    0
#define V_RESPQ_EXHAUSTED(x) ((x) << S_RESPQ_EXHAUSTED)
#define F_RESPQ_EXHAUSTED    V_RESPQ_EXHAUSTED(1U)

#define S_RESPQ_OVERFLOW    1
#define V_RESPQ_OVERFLOW(x) ((x) << S_RESPQ_OVERFLOW)
#define F_RESPQ_OVERFLOW    V_RESPQ_OVERFLOW(1U)

#define S_FL_EXHAUSTED    2
#define V_FL_EXHAUSTED(x) ((x) << S_FL_EXHAUSTED)
#define F_FL_EXHAUSTED    V_FL_EXHAUSTED(1U)

#define S_PACKET_TOO_BIG    3
#define V_PACKET_TOO_BIG(x) ((x) << S_PACKET_TOO_BIG)
#define F_PACKET_TOO_BIG    V_PACKET_TOO_BIG(1U)

#define S_PACKET_MISMATCH    4
#define V_PACKET_MISMATCH(x) ((x) << S_PACKET_MISMATCH)
#define F_PACKET_MISMATCH    V_PACKET_MISMATCH(1U)

#define A_SG_INT_CAUSE 0xbc
#define A_SG_RESPACCUTIMER 0xc0

/* MC3 registers */

#define S_READY    1
#define V_READY(x) ((x) << S_READY)
#define F_READY    V_READY(1U)

/* MC4 registers */

#define A_MC4_CFG 0x180
#define S_MC4_SLOW    25
#define V_MC4_SLOW(x) ((x) << S_MC4_SLOW)
#define F_MC4_SLOW    V_MC4_SLOW(1U)

/* TPI registers */

#define A_TPI_ADDR 0x280
#define A_TPI_WR_DATA 0x284
#define A_TPI_RD_DATA 0x288
#define A_TPI_CSR 0x28c

#define S_TPIWR    0
#define V_TPIWR(x) ((x) << S_TPIWR)
#define F_TPIWR    V_TPIWR(1U)

#define S_TPIRDY    1
#define V_TPIRDY(x) ((x) << S_TPIRDY)
#define F_TPIRDY    V_TPIRDY(1U)

#define A_TPI_PAR 0x29c

#define S_TPIPAR    0
#define M_TPIPAR    0x7f
#define V_TPIPAR(x) ((x) << S_TPIPAR)
#define G_TPIPAR(x) (((x) >> S_TPIPAR) & M_TPIPAR)

/* TP registers */

#define A_TP_IN_CONFIG 0x300

#define S_TP_IN_CSPI_CPL    3
#define V_TP_IN_CSPI_CPL(x) ((x) << S_TP_IN_CSPI_CPL)
#define F_TP_IN_CSPI_CPL    V_TP_IN_CSPI_CPL(1U)

#define S_TP_IN_CSPI_CHECK_IP_CSUM    5
#define V_TP_IN_CSPI_CHECK_IP_CSUM(x) ((x) << S_TP_IN_CSPI_CHECK_IP_CSUM)
#define F_TP_IN_CSPI_CHECK_IP_CSUM    V_TP_IN_CSPI_CHECK_IP_CSUM(1U)

#define S_TP_IN_CSPI_CHECK_TCP_CSUM    6
#define V_TP_IN_CSPI_CHECK_TCP_CSUM(x) ((x) << S_TP_IN_CSPI_CHECK_TCP_CSUM)
#define F_TP_IN_CSPI_CHECK_TCP_CSUM    V_TP_IN_CSPI_CHECK_TCP_CSUM(1U)

#define S_TP_IN_ESPI_ETHERNET    8
#define V_TP_IN_ESPI_ETHERNET(x) ((x) << S_TP_IN_ESPI_ETHERNET)
#define F_TP_IN_ESPI_ETHERNET    V_TP_IN_ESPI_ETHERNET(1U)

#define S_TP_IN_ESPI_CHECK_IP_CSUM    12
#define V_TP_IN_ESPI_CHECK_IP_CSUM(x) ((x) << S_TP_IN_ESPI_CHECK_IP_CSUM)
#define F_TP_IN_ESPI_CHECK_IP_CSUM    V_TP_IN_ESPI_CHECK_IP_CSUM(1U)

#define S_TP_IN_ESPI_CHECK_TCP_CSUM    13
#define V_TP_IN_ESPI_CHECK_TCP_CSUM(x) ((x) << S_TP_IN_ESPI_CHECK_TCP_CSUM)
#define F_TP_IN_ESPI_CHECK_TCP_CSUM    V_TP_IN_ESPI_CHECK_TCP_CSUM(1U)

#define S_OFFLOAD_DISABLE    14
#define V_OFFLOAD_DISABLE(x) ((x) << S_OFFLOAD_DISABLE)
#define F_OFFLOAD_DISABLE    V_OFFLOAD_DISABLE(1U)

#define A_TP_OUT_CONFIG 0x304

#define S_TP_OUT_CSPI_CPL    2
#define V_TP_OUT_CSPI_CPL(x) ((x) << S_TP_OUT_CSPI_CPL)
#define F_TP_OUT_CSPI_CPL    V_TP_OUT_CSPI_CPL(1U)

#define S_TP_OUT_ESPI_ETHERNET    6
#define V_TP_OUT_ESPI_ETHERNET(x) ((x) << S_TP_OUT_ESPI_ETHERNET)
#define F_TP_OUT_ESPI_ETHERNET    V_TP_OUT_ESPI_ETHERNET(1U)

#define S_TP_OUT_ESPI_GENERATE_IP_CSUM    10
#define V_TP_OUT_ESPI_GENERATE_IP_CSUM(x) ((x) << S_TP_OUT_ESPI_GENERATE_IP_CSUM)
#define F_TP_OUT_ESPI_GENERATE_IP_CSUM    V_TP_OUT_ESPI_GENERATE_IP_CSUM(1U)

#define S_TP_OUT_ESPI_GENERATE_TCP_CSUM    11
#define V_TP_OUT_ESPI_GENERATE_TCP_CSUM(x) ((x) << S_TP_OUT_ESPI_GENERATE_TCP_CSUM)
#define F_TP_OUT_ESPI_GENERATE_TCP_CSUM    V_TP_OUT_ESPI_GENERATE_TCP_CSUM(1U)

#define A_TP_GLOBAL_CONFIG 0x308

#define S_IP_TTL    0
#define M_IP_TTL    0xff
#define V_IP_TTL(x) ((x) << S_IP_TTL)

#define S_TCP_CSUM    11
#define V_TCP_CSUM(x) ((x) << S_TCP_CSUM)
#define F_TCP_CSUM    V_TCP_CSUM(1U)

#define S_UDP_CSUM    12
#define V_UDP_CSUM(x) ((x) << S_UDP_CSUM)
#define F_UDP_CSUM    V_UDP_CSUM(1U)

#define S_IP_CSUM    13
#define V_IP_CSUM(x) ((x) << S_IP_CSUM)
#define F_IP_CSUM    V_IP_CSUM(1U)

#define S_PATH_MTU    15
#define V_PATH_MTU(x) ((x) << S_PATH_MTU)
#define F_PATH_MTU    V_PATH_MTU(1U)

#define S_5TUPLE_LOOKUP    17
#define V_5TUPLE_LOOKUP(x) ((x) << S_5TUPLE_LOOKUP)

#define S_SYN_COOKIE_PARAMETER    26
#define V_SYN_COOKIE_PARAMETER(x) ((x) << S_SYN_COOKIE_PARAMETER)

#define A_TP_PC_CONFIG 0x348
#define S_DIS_TX_FILL_WIN_PUSH    12
#define V_DIS_TX_FILL_WIN_PUSH(x) ((x) << S_DIS_TX_FILL_WIN_PUSH)
#define F_DIS_TX_FILL_WIN_PUSH    V_DIS_TX_FILL_WIN_PUSH(1U)

#define S_TP_PC_REV    30
#define M_TP_PC_REV    0x3
#define G_TP_PC_REV(x) (((x) >> S_TP_PC_REV) & M_TP_PC_REV)
#define A_TP_RESET 0x44c
#define S_TP_RESET    0
#define V_TP_RESET(x) ((x) << S_TP_RESET)
#define F_TP_RESET    V_TP_RESET(1U)

#define A_TP_INT_ENABLE 0x470
#define A_TP_INT_CAUSE 0x474
#define A_TP_TX_DROP_CONFIG 0x4b8

#define S_ENABLE_TX_DROP    31
#define V_ENABLE_TX_DROP(x) ((x) << S_ENABLE_TX_DROP)
#define F_ENABLE_TX_DROP    V_ENABLE_TX_DROP(1U)

#define S_ENABLE_TX_ERROR    30
#define V_ENABLE_TX_ERROR(x) ((x) << S_ENABLE_TX_ERROR)
#define F_ENABLE_TX_ERROR    V_ENABLE_TX_ERROR(1U)

#define S_DROP_TICKS_CNT    4
#define V_DROP_TICKS_CNT(x) ((x) << S_DROP_TICKS_CNT)

#define S_NUM_PKTS_DROPPED    0
#define V_NUM_PKTS_DROPPED(x) ((x) << S_NUM_PKTS_DROPPED)

/* CSPI registers */

#define S_DIP4ERR    0
#define V_DIP4ERR(x) ((x) << S_DIP4ERR)
#define F_DIP4ERR    V_DIP4ERR(1U)

#define S_RXDROP    1
#define V_RXDROP(x) ((x) << S_RXDROP)
#define F_RXDROP    V_RXDROP(1U)

#define S_TXDROP    2
#define V_TXDROP(x) ((x) << S_TXDROP)
#define F_TXDROP    V_TXDROP(1U)

#define S_RXOVERFLOW    3
#define V_RXOVERFLOW(x) ((x) << S_RXOVERFLOW)
#define F_RXOVERFLOW    V_RXOVERFLOW(1U)

#define S_RAMPARITYERR    4
#define V_RAMPARITYERR(x) ((x) << S_RAMPARITYERR)
#define F_RAMPARITYERR    V_RAMPARITYERR(1U)

/* ESPI registers */

#define A_ESPI_SCH_TOKEN0 0x880
#define A_ESPI_SCH_TOKEN1 0x884
#define A_ESPI_SCH_TOKEN2 0x888
#define A_ESPI_SCH_TOKEN3 0x88c
#define A_ESPI_RX_FIFO_ALMOST_EMPTY_WATERMARK 0x890
#define A_ESPI_RX_FIFO_ALMOST_FULL_WATERMARK 0x894
#define A_ESPI_CALENDAR_LENGTH 0x898
#define A_PORT_CONFIG 0x89c

#define S_RX_NPORTS    0
#define V_RX_NPORTS(x) ((x) << S_RX_NPORTS)

#define S_TX_NPORTS    8
#define V_TX_NPORTS(x) ((x) << S_TX_NPORTS)

#define A_ESPI_FIFO_STATUS_ENABLE 0x8a0

#define S_RXSTATUSENABLE    0
#define V_RXSTATUSENABLE(x) ((x) << S_RXSTATUSENABLE)
#define F_RXSTATUSENABLE    V_RXSTATUSENABLE(1U)

#define S_INTEL1010MODE    4
#define V_INTEL1010MODE(x) ((x) << S_INTEL1010MODE)
#define F_INTEL1010MODE    V_INTEL1010MODE(1U)

#define A_ESPI_MAXBURST1_MAXBURST2 0x8a8
#define A_ESPI_TRAIN 0x8ac
#define A_ESPI_INTR_STATUS 0x8c8

#define S_DIP2PARITYERR    5
#define V_DIP2PARITYERR(x) ((x) << S_DIP2PARITYERR)
#define F_DIP2PARITYERR    V_DIP2PARITYERR(1U)

#define A_ESPI_INTR_ENABLE 0x8cc
#define A_RX_DROP_THRESHOLD 0x8d0
#define A_ESPI_RX_RESET 0x8ec
#define A_ESPI_MISC_CONTROL 0x8f0

#define S_OUT_OF_SYNC_COUNT    0
#define V_OUT_OF_SYNC_COUNT(x) ((x) << S_OUT_OF_SYNC_COUNT)

#define S_DIP2_PARITY_ERR_THRES    5
#define V_DIP2_PARITY_ERR_THRES(x) ((x) << S_DIP2_PARITY_ERR_THRES)

#define S_DIP4_THRES    9
#define V_DIP4_THRES(x) ((x) << S_DIP4_THRES)

#define S_MONITORED_PORT_NUM    25
#define V_MONITORED_PORT_NUM(x) ((x) << S_MONITORED_PORT_NUM)

#define S_MONITORED_DIRECTION    27
#define V_MONITORED_DIRECTION(x) ((x) << S_MONITORED_DIRECTION)
#define F_MONITORED_DIRECTION    V_MONITORED_DIRECTION(1U)

#define S_MONITORED_INTERFACE    28
#define V_MONITORED_INTERFACE(x) ((x) << S_MONITORED_INTERFACE)
#define F_MONITORED_INTERFACE    V_MONITORED_INTERFACE(1U)

#define A_ESPI_DIP2_ERR_COUNT 0x8f4
#define A_ESPI_CMD_ADDR 0x8f8

#define S_WRITE_DATA    0
#define V_WRITE_DATA(x) ((x) << S_WRITE_DATA)

#define S_REGISTER_OFFSET    8
#define V_REGISTER_OFFSET(x) ((x) << S_REGISTER_OFFSET)

#define S_CHANNEL_ADDR    12
#define V_CHANNEL_ADDR(x) ((x) << S_CHANNEL_ADDR)

#define S_MODULE_ADDR    16
#define V_MODULE_ADDR(x) ((x) << S_MODULE_ADDR)

#define S_BUNDLE_ADDR    20
#define V_BUNDLE_ADDR(x) ((x) << S_BUNDLE_ADDR)

#define S_SPI4_COMMAND    24
#define V_SPI4_COMMAND(x) ((x) << S_SPI4_COMMAND)

#define A_ESPI_GOSTAT 0x8fc
#define S_ESPI_CMD_BUSY    8
#define V_ESPI_CMD_BUSY(x) ((x) << S_ESPI_CMD_BUSY)
#define F_ESPI_CMD_BUSY    V_ESPI_CMD_BUSY(1U)

/* PL registers */

#define A_PL_ENABLE 0xa00

#define S_PL_INTR_SGE_ERR    0
#define V_PL_INTR_SGE_ERR(x) ((x) << S_PL_INTR_SGE_ERR)
#define F_PL_INTR_SGE_ERR    V_PL_INTR_SGE_ERR(1U)

#define S_PL_INTR_SGE_DATA    1
#define V_PL_INTR_SGE_DATA(x) ((x) << S_PL_INTR_SGE_DATA)
#define F_PL_INTR_SGE_DATA    V_PL_INTR_SGE_DATA(1U)

#define S_PL_INTR_TP    6
#define V_PL_INTR_TP(x) ((x) << S_PL_INTR_TP)
#define F_PL_INTR_TP    V_PL_INTR_TP(1U)

#define S_PL_INTR_ESPI    8
#define V_PL_INTR_ESPI(x) ((x) << S_PL_INTR_ESPI)
#define F_PL_INTR_ESPI    V_PL_INTR_ESPI(1U)

#define S_PL_INTR_PCIX    10
#define V_PL_INTR_PCIX(x) ((x) << S_PL_INTR_PCIX)
#define F_PL_INTR_PCIX    V_PL_INTR_PCIX(1U)

#define S_PL_INTR_EXT    11
#define V_PL_INTR_EXT(x) ((x) << S_PL_INTR_EXT)
#define F_PL_INTR_EXT    V_PL_INTR_EXT(1U)

#define A_PL_CAUSE 0xa04

/* MC5 registers */

#define A_MC5_CONFIG 0xc04

#define S_TCAM_RESET    1
#define V_TCAM_RESET(x) ((x) << S_TCAM_RESET)
#define F_TCAM_RESET    V_TCAM_RESET(1U)

#define S_M_BUS_ENABLE    5
#define V_M_BUS_ENABLE(x) ((x) << S_M_BUS_ENABLE)
#define F_M_BUS_ENABLE    V_M_BUS_ENABLE(1U)

/* PCICFG registers */

#define A_PCICFG_PM_CSR 0x44
#define A_PCICFG_VPD_ADDR 0x4a

#define S_VPD_OP_FLAG    15
#define V_VPD_OP_FLAG(x) ((x) << S_VPD_OP_FLAG)
#define F_VPD_OP_FLAG    V_VPD_OP_FLAG(1U)

#define A_PCICFG_VPD_DATA 0x4c

#define A_PCICFG_INTR_ENABLE 0xf4
#define A_PCICFG_INTR_CAUSE 0xf8

#define A_PCICFG_MODE 0xfc

#define S_PCI_MODE_64BIT    0
#define V_PCI_MODE_64BIT(x) ((x) << S_PCI_MODE_64BIT)
#define F_PCI_MODE_64BIT    V_PCI_MODE_64BIT(1U)

#define S_PCI_MODE_PCIX    5
#define V_PCI_MODE_PCIX(x) ((x) << S_PCI_MODE_PCIX)
#define F_PCI_MODE_PCIX    V_PCI_MODE_PCIX(1U)

#define S_PCI_MODE_CLK    6
#define M_PCI_MODE_CLK    0x3
#define G_PCI_MODE_CLK(x) (((x) >> S_PCI_MODE_CLK) & M_PCI_MODE_CLK)

#endif /* _CXGB_REGS_H_ */
