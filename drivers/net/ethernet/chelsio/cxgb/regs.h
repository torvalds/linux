/* SPDX-License-Identifier: GPL-2.0-only */
/*****************************************************************************
 *                                                                           *
 * File: regs.h                                                              *
 * $Revision: 1.8 $                                                          *
 * $Date: 2005/06/21 18:29:48 $                                              *
 * Description:                                                              *
 *  part of the Chelsio 10Gb Ethernet Driver.                                *
 *                                                                           *
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

#define S_DISABLE_CMDQ0_GTS    8
#define V_DISABLE_CMDQ0_GTS(x) ((x) << S_DISABLE_CMDQ0_GTS)
#define F_DISABLE_CMDQ0_GTS    V_DISABLE_CMDQ0_GTS(1U)

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

#define S_FL_SELECTION_CRITERIA    13
#define V_FL_SELECTION_CRITERIA(x) ((x) << S_FL_SELECTION_CRITERIA)
#define F_FL_SELECTION_CRITERIA    V_FL_SELECTION_CRITERIA(1U)

#define S_ISCSI_COALESCE    14
#define V_ISCSI_COALESCE(x) ((x) << S_ISCSI_COALESCE)
#define F_ISCSI_COALESCE    V_ISCSI_COALESCE(1U)

#define S_RX_PKT_OFFSET    15
#define M_RX_PKT_OFFSET    0x7
#define V_RX_PKT_OFFSET(x) ((x) << S_RX_PKT_OFFSET)
#define G_RX_PKT_OFFSET(x) (((x) >> S_RX_PKT_OFFSET) & M_RX_PKT_OFFSET)

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

#define S_CMDQ0_SIZE    0
#define M_CMDQ0_SIZE    0x1ffff
#define V_CMDQ0_SIZE(x) ((x) << S_CMDQ0_SIZE)
#define G_CMDQ0_SIZE(x) (((x) >> S_CMDQ0_SIZE) & M_CMDQ0_SIZE)

#define A_SG_FL0SIZE 0x2c

#define S_FL0_SIZE    0
#define M_FL0_SIZE    0x1ffff
#define V_FL0_SIZE(x) ((x) << S_FL0_SIZE)
#define G_FL0_SIZE(x) (((x) >> S_FL0_SIZE) & M_FL0_SIZE)

#define A_SG_RSPSIZE 0x30

#define S_RESPQ_SIZE    0
#define M_RESPQ_SIZE    0x1ffff
#define V_RESPQ_SIZE(x) ((x) << S_RESPQ_SIZE)
#define G_RESPQ_SIZE(x) (((x) >> S_RESPQ_SIZE) & M_RESPQ_SIZE)

#define A_SG_RSPBASELWR 0x34
#define A_SG_RSPBASEUPR 0x38
#define A_SG_FLTHRESHOLD 0x3c

#define S_FL_THRESHOLD    0
#define M_FL_THRESHOLD    0xffff
#define V_FL_THRESHOLD(x) ((x) << S_FL_THRESHOLD)
#define G_FL_THRESHOLD(x) (((x) >> S_FL_THRESHOLD) & M_FL_THRESHOLD)

#define A_SG_RSPQUEUECREDIT 0x40

#define S_RESPQ_CREDIT    0
#define M_RESPQ_CREDIT    0x1ffff
#define V_RESPQ_CREDIT(x) ((x) << S_RESPQ_CREDIT)
#define G_RESPQ_CREDIT(x) (((x) >> S_RESPQ_CREDIT) & M_RESPQ_CREDIT)

#define A_SG_SLEEPING 0x48

#define S_SLEEPING    0
#define M_SLEEPING    0xffff
#define V_SLEEPING(x) ((x) << S_SLEEPING)
#define G_SLEEPING(x) (((x) >> S_SLEEPING) & M_SLEEPING)

#define A_SG_INTRTIMER 0x4c

#define S_INTERRUPT_TIMER_COUNT    0
#define M_INTERRUPT_TIMER_COUNT    0xffffff
#define V_INTERRUPT_TIMER_COUNT(x) ((x) << S_INTERRUPT_TIMER_COUNT)
#define G_INTERRUPT_TIMER_COUNT(x) (((x) >> S_INTERRUPT_TIMER_COUNT) & M_INTERRUPT_TIMER_COUNT)

#define A_SG_CMD0PTR 0x50

#define S_CMDQ0_POINTER    0
#define M_CMDQ0_POINTER    0xffff
#define V_CMDQ0_POINTER(x) ((x) << S_CMDQ0_POINTER)
#define G_CMDQ0_POINTER(x) (((x) >> S_CMDQ0_POINTER) & M_CMDQ0_POINTER)

#define S_CURRENT_GENERATION_BIT    16
#define V_CURRENT_GENERATION_BIT(x) ((x) << S_CURRENT_GENERATION_BIT)
#define F_CURRENT_GENERATION_BIT    V_CURRENT_GENERATION_BIT(1U)

#define A_SG_CMD1PTR 0x54

#define S_CMDQ1_POINTER    0
#define M_CMDQ1_POINTER    0xffff
#define V_CMDQ1_POINTER(x) ((x) << S_CMDQ1_POINTER)
#define G_CMDQ1_POINTER(x) (((x) >> S_CMDQ1_POINTER) & M_CMDQ1_POINTER)

#define A_SG_FL0PTR 0x58

#define S_FL0_POINTER    0
#define M_FL0_POINTER    0xffff
#define V_FL0_POINTER(x) ((x) << S_FL0_POINTER)
#define G_FL0_POINTER(x) (((x) >> S_FL0_POINTER) & M_FL0_POINTER)

#define A_SG_FL1PTR 0x5c

#define S_FL1_POINTER    0
#define M_FL1_POINTER    0xffff
#define V_FL1_POINTER(x) ((x) << S_FL1_POINTER)
#define G_FL1_POINTER(x) (((x) >> S_FL1_POINTER) & M_FL1_POINTER)

#define A_SG_VERSION 0x6c

#define S_DAY    0
#define M_DAY    0x1f
#define V_DAY(x) ((x) << S_DAY)
#define G_DAY(x) (((x) >> S_DAY) & M_DAY)

#define S_MONTH    5
#define M_MONTH    0xf
#define V_MONTH(x) ((x) << S_MONTH)
#define G_MONTH(x) (((x) >> S_MONTH) & M_MONTH)

#define A_SG_CMD1SIZE 0xb0

#define S_CMDQ1_SIZE    0
#define M_CMDQ1_SIZE    0x1ffff
#define V_CMDQ1_SIZE(x) ((x) << S_CMDQ1_SIZE)
#define G_CMDQ1_SIZE(x) (((x) >> S_CMDQ1_SIZE) & M_CMDQ1_SIZE)

#define A_SG_FL1SIZE 0xb4

#define S_FL1_SIZE    0
#define M_FL1_SIZE    0x1ffff
#define V_FL1_SIZE(x) ((x) << S_FL1_SIZE)
#define G_FL1_SIZE(x) (((x) >> S_FL1_SIZE) & M_FL1_SIZE)

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
#define A_MC3_CFG 0x100

#define S_CLK_ENABLE    0
#define V_CLK_ENABLE(x) ((x) << S_CLK_ENABLE)
#define F_CLK_ENABLE    V_CLK_ENABLE(1U)

#define S_READY    1
#define V_READY(x) ((x) << S_READY)
#define F_READY    V_READY(1U)

#define S_READ_TO_WRITE_DELAY    2
#define M_READ_TO_WRITE_DELAY    0x7
#define V_READ_TO_WRITE_DELAY(x) ((x) << S_READ_TO_WRITE_DELAY)
#define G_READ_TO_WRITE_DELAY(x) (((x) >> S_READ_TO_WRITE_DELAY) & M_READ_TO_WRITE_DELAY)

#define S_WRITE_TO_READ_DELAY    5
#define M_WRITE_TO_READ_DELAY    0x7
#define V_WRITE_TO_READ_DELAY(x) ((x) << S_WRITE_TO_READ_DELAY)
#define G_WRITE_TO_READ_DELAY(x) (((x) >> S_WRITE_TO_READ_DELAY) & M_WRITE_TO_READ_DELAY)

#define S_MC3_BANK_CYCLE    8
#define M_MC3_BANK_CYCLE    0xf
#define V_MC3_BANK_CYCLE(x) ((x) << S_MC3_BANK_CYCLE)
#define G_MC3_BANK_CYCLE(x) (((x) >> S_MC3_BANK_CYCLE) & M_MC3_BANK_CYCLE)

#define S_REFRESH_CYCLE    12
#define M_REFRESH_CYCLE    0xf
#define V_REFRESH_CYCLE(x) ((x) << S_REFRESH_CYCLE)
#define G_REFRESH_CYCLE(x) (((x) >> S_REFRESH_CYCLE) & M_REFRESH_CYCLE)

#define S_PRECHARGE_CYCLE    16
#define M_PRECHARGE_CYCLE    0x3
#define V_PRECHARGE_CYCLE(x) ((x) << S_PRECHARGE_CYCLE)
#define G_PRECHARGE_CYCLE(x) (((x) >> S_PRECHARGE_CYCLE) & M_PRECHARGE_CYCLE)

#define S_ACTIVE_TO_READ_WRITE_DELAY    18
#define V_ACTIVE_TO_READ_WRITE_DELAY(x) ((x) << S_ACTIVE_TO_READ_WRITE_DELAY)
#define F_ACTIVE_TO_READ_WRITE_DELAY    V_ACTIVE_TO_READ_WRITE_DELAY(1U)

#define S_ACTIVE_TO_PRECHARGE_DELAY    19
#define M_ACTIVE_TO_PRECHARGE_DELAY    0x7
#define V_ACTIVE_TO_PRECHARGE_DELAY(x) ((x) << S_ACTIVE_TO_PRECHARGE_DELAY)
#define G_ACTIVE_TO_PRECHARGE_DELAY(x) (((x) >> S_ACTIVE_TO_PRECHARGE_DELAY) & M_ACTIVE_TO_PRECHARGE_DELAY)

#define S_WRITE_RECOVERY_DELAY    22
#define M_WRITE_RECOVERY_DELAY    0x3
#define V_WRITE_RECOVERY_DELAY(x) ((x) << S_WRITE_RECOVERY_DELAY)
#define G_WRITE_RECOVERY_DELAY(x) (((x) >> S_WRITE_RECOVERY_DELAY) & M_WRITE_RECOVERY_DELAY)

#define S_DENSITY    24
#define M_DENSITY    0x3
#define V_DENSITY(x) ((x) << S_DENSITY)
#define G_DENSITY(x) (((x) >> S_DENSITY) & M_DENSITY)

#define S_ORGANIZATION    26
#define V_ORGANIZATION(x) ((x) << S_ORGANIZATION)
#define F_ORGANIZATION    V_ORGANIZATION(1U)

#define S_BANKS    27
#define V_BANKS(x) ((x) << S_BANKS)
#define F_BANKS    V_BANKS(1U)

#define S_UNREGISTERED    28
#define V_UNREGISTERED(x) ((x) << S_UNREGISTERED)
#define F_UNREGISTERED    V_UNREGISTERED(1U)

#define S_MC3_WIDTH    29
#define M_MC3_WIDTH    0x3
#define V_MC3_WIDTH(x) ((x) << S_MC3_WIDTH)
#define G_MC3_WIDTH(x) (((x) >> S_MC3_WIDTH) & M_MC3_WIDTH)

#define S_MC3_SLOW    31
#define V_MC3_SLOW(x) ((x) << S_MC3_SLOW)
#define F_MC3_SLOW    V_MC3_SLOW(1U)

#define A_MC3_MODE 0x104

#define S_MC3_MODE    0
#define M_MC3_MODE    0x3fff
#define V_MC3_MODE(x) ((x) << S_MC3_MODE)
#define G_MC3_MODE(x) (((x) >> S_MC3_MODE) & M_MC3_MODE)

#define S_BUSY    31
#define V_BUSY(x) ((x) << S_BUSY)
#define F_BUSY    V_BUSY(1U)

#define A_MC3_EXT_MODE 0x108

#define S_MC3_EXTENDED_MODE    0
#define M_MC3_EXTENDED_MODE    0x3fff
#define V_MC3_EXTENDED_MODE(x) ((x) << S_MC3_EXTENDED_MODE)
#define G_MC3_EXTENDED_MODE(x) (((x) >> S_MC3_EXTENDED_MODE) & M_MC3_EXTENDED_MODE)

#define A_MC3_PRECHARG 0x10c
#define A_MC3_REFRESH 0x110

#define S_REFRESH_ENABLE    0
#define V_REFRESH_ENABLE(x) ((x) << S_REFRESH_ENABLE)
#define F_REFRESH_ENABLE    V_REFRESH_ENABLE(1U)

#define S_REFRESH_DIVISOR    1
#define M_REFRESH_DIVISOR    0x3fff
#define V_REFRESH_DIVISOR(x) ((x) << S_REFRESH_DIVISOR)
#define G_REFRESH_DIVISOR(x) (((x) >> S_REFRESH_DIVISOR) & M_REFRESH_DIVISOR)

#define A_MC3_STROBE 0x114

#define S_MASTER_DLL_RESET    0
#define V_MASTER_DLL_RESET(x) ((x) << S_MASTER_DLL_RESET)
#define F_MASTER_DLL_RESET    V_MASTER_DLL_RESET(1U)

#define S_MASTER_DLL_TAP_COUNT    1
#define M_MASTER_DLL_TAP_COUNT    0xff
#define V_MASTER_DLL_TAP_COUNT(x) ((x) << S_MASTER_DLL_TAP_COUNT)
#define G_MASTER_DLL_TAP_COUNT(x) (((x) >> S_MASTER_DLL_TAP_COUNT) & M_MASTER_DLL_TAP_COUNT)

#define S_MASTER_DLL_LOCKED    9
#define V_MASTER_DLL_LOCKED(x) ((x) << S_MASTER_DLL_LOCKED)
#define F_MASTER_DLL_LOCKED    V_MASTER_DLL_LOCKED(1U)

#define S_MASTER_DLL_MAX_TAP_COUNT    10
#define V_MASTER_DLL_MAX_TAP_COUNT(x) ((x) << S_MASTER_DLL_MAX_TAP_COUNT)
#define F_MASTER_DLL_MAX_TAP_COUNT    V_MASTER_DLL_MAX_TAP_COUNT(1U)

#define S_MASTER_DLL_TAP_COUNT_OFFSET    11
#define M_MASTER_DLL_TAP_COUNT_OFFSET    0x3f
#define V_MASTER_DLL_TAP_COUNT_OFFSET(x) ((x) << S_MASTER_DLL_TAP_COUNT_OFFSET)
#define G_MASTER_DLL_TAP_COUNT_OFFSET(x) (((x) >> S_MASTER_DLL_TAP_COUNT_OFFSET) & M_MASTER_DLL_TAP_COUNT_OFFSET)

#define S_SLAVE_DLL_RESET    11
#define V_SLAVE_DLL_RESET(x) ((x) << S_SLAVE_DLL_RESET)
#define F_SLAVE_DLL_RESET    V_SLAVE_DLL_RESET(1U)

#define S_SLAVE_DLL_DELTA    12
#define M_SLAVE_DLL_DELTA    0xf
#define V_SLAVE_DLL_DELTA(x) ((x) << S_SLAVE_DLL_DELTA)
#define G_SLAVE_DLL_DELTA(x) (((x) >> S_SLAVE_DLL_DELTA) & M_SLAVE_DLL_DELTA)

#define S_SLAVE_DELAY_LINE_MANUAL_TAP_COUNT    17
#define M_SLAVE_DELAY_LINE_MANUAL_TAP_COUNT    0x3f
#define V_SLAVE_DELAY_LINE_MANUAL_TAP_COUNT(x) ((x) << S_SLAVE_DELAY_LINE_MANUAL_TAP_COUNT)
#define G_SLAVE_DELAY_LINE_MANUAL_TAP_COUNT(x) (((x) >> S_SLAVE_DELAY_LINE_MANUAL_TAP_COUNT) & M_SLAVE_DELAY_LINE_MANUAL_TAP_COUNT)

#define S_SLAVE_DELAY_LINE_MANUAL_TAP_COUNT_ENABLE    23
#define V_SLAVE_DELAY_LINE_MANUAL_TAP_COUNT_ENABLE(x) ((x) << S_SLAVE_DELAY_LINE_MANUAL_TAP_COUNT_ENABLE)
#define F_SLAVE_DELAY_LINE_MANUAL_TAP_COUNT_ENABLE    V_SLAVE_DELAY_LINE_MANUAL_TAP_COUNT_ENABLE(1U)

#define S_SLAVE_DELAY_LINE_TAP_COUNT    24
#define M_SLAVE_DELAY_LINE_TAP_COUNT    0x3f
#define V_SLAVE_DELAY_LINE_TAP_COUNT(x) ((x) << S_SLAVE_DELAY_LINE_TAP_COUNT)
#define G_SLAVE_DELAY_LINE_TAP_COUNT(x) (((x) >> S_SLAVE_DELAY_LINE_TAP_COUNT) & M_SLAVE_DELAY_LINE_TAP_COUNT)

#define A_MC3_ECC_CNTL 0x118

#define S_ECC_GENERATION_ENABLE    0
#define V_ECC_GENERATION_ENABLE(x) ((x) << S_ECC_GENERATION_ENABLE)
#define F_ECC_GENERATION_ENABLE    V_ECC_GENERATION_ENABLE(1U)

#define S_ECC_CHECK_ENABLE    1
#define V_ECC_CHECK_ENABLE(x) ((x) << S_ECC_CHECK_ENABLE)
#define F_ECC_CHECK_ENABLE    V_ECC_CHECK_ENABLE(1U)

#define S_CORRECTABLE_ERROR_COUNT    2
#define M_CORRECTABLE_ERROR_COUNT    0xff
#define V_CORRECTABLE_ERROR_COUNT(x) ((x) << S_CORRECTABLE_ERROR_COUNT)
#define G_CORRECTABLE_ERROR_COUNT(x) (((x) >> S_CORRECTABLE_ERROR_COUNT) & M_CORRECTABLE_ERROR_COUNT)

#define S_UNCORRECTABLE_ERROR_COUNT    10
#define M_UNCORRECTABLE_ERROR_COUNT    0xff
#define V_UNCORRECTABLE_ERROR_COUNT(x) ((x) << S_UNCORRECTABLE_ERROR_COUNT)
#define G_UNCORRECTABLE_ERROR_COUNT(x) (((x) >> S_UNCORRECTABLE_ERROR_COUNT) & M_UNCORRECTABLE_ERROR_COUNT)

#define A_MC3_CE_ADDR 0x11c

#define S_MC3_CE_ADDR    4
#define M_MC3_CE_ADDR    0xfffffff
#define V_MC3_CE_ADDR(x) ((x) << S_MC3_CE_ADDR)
#define G_MC3_CE_ADDR(x) (((x) >> S_MC3_CE_ADDR) & M_MC3_CE_ADDR)

#define A_MC3_CE_DATA0 0x120
#define A_MC3_CE_DATA1 0x124
#define A_MC3_CE_DATA2 0x128
#define A_MC3_CE_DATA3 0x12c
#define A_MC3_CE_DATA4 0x130
#define A_MC3_UE_ADDR 0x134

#define S_MC3_UE_ADDR    4
#define M_MC3_UE_ADDR    0xfffffff
#define V_MC3_UE_ADDR(x) ((x) << S_MC3_UE_ADDR)
#define G_MC3_UE_ADDR(x) (((x) >> S_MC3_UE_ADDR) & M_MC3_UE_ADDR)

#define A_MC3_UE_DATA0 0x138
#define A_MC3_UE_DATA1 0x13c
#define A_MC3_UE_DATA2 0x140
#define A_MC3_UE_DATA3 0x144
#define A_MC3_UE_DATA4 0x148
#define A_MC3_BD_ADDR 0x14c
#define A_MC3_BD_DATA0 0x150
#define A_MC3_BD_DATA1 0x154
#define A_MC3_BD_DATA2 0x158
#define A_MC3_BD_DATA3 0x15c
#define A_MC3_BD_DATA4 0x160
#define A_MC3_BD_OP 0x164

#define S_BACK_DOOR_OPERATION    0
#define V_BACK_DOOR_OPERATION(x) ((x) << S_BACK_DOOR_OPERATION)
#define F_BACK_DOOR_OPERATION    V_BACK_DOOR_OPERATION(1U)

#define A_MC3_BIST_ADDR_BEG 0x168
#define A_MC3_BIST_ADDR_END 0x16c
#define A_MC3_BIST_DATA 0x170
#define A_MC3_BIST_OP 0x174

#define S_OP    0
#define V_OP(x) ((x) << S_OP)
#define F_OP    V_OP(1U)

#define S_DATA_PATTERN    1
#define M_DATA_PATTERN    0x3
#define V_DATA_PATTERN(x) ((x) << S_DATA_PATTERN)
#define G_DATA_PATTERN(x) (((x) >> S_DATA_PATTERN) & M_DATA_PATTERN)

#define S_CONTINUOUS    3
#define V_CONTINUOUS(x) ((x) << S_CONTINUOUS)
#define F_CONTINUOUS    V_CONTINUOUS(1U)

#define A_MC3_INT_ENABLE 0x178

#define S_MC3_CORR_ERR    0
#define V_MC3_CORR_ERR(x) ((x) << S_MC3_CORR_ERR)
#define F_MC3_CORR_ERR    V_MC3_CORR_ERR(1U)

#define S_MC3_UNCORR_ERR    1
#define V_MC3_UNCORR_ERR(x) ((x) << S_MC3_UNCORR_ERR)
#define F_MC3_UNCORR_ERR    V_MC3_UNCORR_ERR(1U)

#define S_MC3_PARITY_ERR    2
#define M_MC3_PARITY_ERR    0xff
#define V_MC3_PARITY_ERR(x) ((x) << S_MC3_PARITY_ERR)
#define G_MC3_PARITY_ERR(x) (((x) >> S_MC3_PARITY_ERR) & M_MC3_PARITY_ERR)

#define S_MC3_ADDR_ERR    10
#define V_MC3_ADDR_ERR(x) ((x) << S_MC3_ADDR_ERR)
#define F_MC3_ADDR_ERR    V_MC3_ADDR_ERR(1U)

#define A_MC3_INT_CAUSE 0x17c

/* MC4 registers */
#define A_MC4_CFG 0x180

#define S_POWER_UP    0
#define V_POWER_UP(x) ((x) << S_POWER_UP)
#define F_POWER_UP    V_POWER_UP(1U)

#define S_MC4_BANK_CYCLE    8
#define M_MC4_BANK_CYCLE    0x7
#define V_MC4_BANK_CYCLE(x) ((x) << S_MC4_BANK_CYCLE)
#define G_MC4_BANK_CYCLE(x) (((x) >> S_MC4_BANK_CYCLE) & M_MC4_BANK_CYCLE)

#define S_MC4_NARROW    24
#define V_MC4_NARROW(x) ((x) << S_MC4_NARROW)
#define F_MC4_NARROW    V_MC4_NARROW(1U)

#define S_MC4_SLOW    25
#define V_MC4_SLOW(x) ((x) << S_MC4_SLOW)
#define F_MC4_SLOW    V_MC4_SLOW(1U)

#define S_MC4A_WIDTH    24
#define M_MC4A_WIDTH    0x3
#define V_MC4A_WIDTH(x) ((x) << S_MC4A_WIDTH)
#define G_MC4A_WIDTH(x) (((x) >> S_MC4A_WIDTH) & M_MC4A_WIDTH)

#define S_MC4A_SLOW    26
#define V_MC4A_SLOW(x) ((x) << S_MC4A_SLOW)
#define F_MC4A_SLOW    V_MC4A_SLOW(1U)

#define A_MC4_MODE 0x184

#define S_MC4_MODE    0
#define M_MC4_MODE    0x7fff
#define V_MC4_MODE(x) ((x) << S_MC4_MODE)
#define G_MC4_MODE(x) (((x) >> S_MC4_MODE) & M_MC4_MODE)

#define A_MC4_EXT_MODE 0x188

#define S_MC4_EXTENDED_MODE    0
#define M_MC4_EXTENDED_MODE    0x7fff
#define V_MC4_EXTENDED_MODE(x) ((x) << S_MC4_EXTENDED_MODE)
#define G_MC4_EXTENDED_MODE(x) (((x) >> S_MC4_EXTENDED_MODE) & M_MC4_EXTENDED_MODE)

#define A_MC4_REFRESH 0x190
#define A_MC4_STROBE 0x194
#define A_MC4_ECC_CNTL 0x198
#define A_MC4_CE_ADDR 0x19c

#define S_MC4_CE_ADDR    4
#define M_MC4_CE_ADDR    0xffffff
#define V_MC4_CE_ADDR(x) ((x) << S_MC4_CE_ADDR)
#define G_MC4_CE_ADDR(x) (((x) >> S_MC4_CE_ADDR) & M_MC4_CE_ADDR)

#define A_MC4_CE_DATA0 0x1a0
#define A_MC4_CE_DATA1 0x1a4
#define A_MC4_CE_DATA2 0x1a8
#define A_MC4_CE_DATA3 0x1ac
#define A_MC4_CE_DATA4 0x1b0
#define A_MC4_UE_ADDR 0x1b4

#define S_MC4_UE_ADDR    4
#define M_MC4_UE_ADDR    0xffffff
#define V_MC4_UE_ADDR(x) ((x) << S_MC4_UE_ADDR)
#define G_MC4_UE_ADDR(x) (((x) >> S_MC4_UE_ADDR) & M_MC4_UE_ADDR)

#define A_MC4_UE_DATA0 0x1b8
#define A_MC4_UE_DATA1 0x1bc
#define A_MC4_UE_DATA2 0x1c0
#define A_MC4_UE_DATA3 0x1c4
#define A_MC4_UE_DATA4 0x1c8
#define A_MC4_BD_ADDR 0x1cc

#define S_MC4_BACK_DOOR_ADDR    0
#define M_MC4_BACK_DOOR_ADDR    0xfffffff
#define V_MC4_BACK_DOOR_ADDR(x) ((x) << S_MC4_BACK_DOOR_ADDR)
#define G_MC4_BACK_DOOR_ADDR(x) (((x) >> S_MC4_BACK_DOOR_ADDR) & M_MC4_BACK_DOOR_ADDR)

#define A_MC4_BD_DATA0 0x1d0
#define A_MC4_BD_DATA1 0x1d4
#define A_MC4_BD_DATA2 0x1d8
#define A_MC4_BD_DATA3 0x1dc
#define A_MC4_BD_DATA4 0x1e0
#define A_MC4_BD_OP 0x1e4

#define S_OPERATION    0
#define V_OPERATION(x) ((x) << S_OPERATION)
#define F_OPERATION    V_OPERATION(1U)

#define A_MC4_BIST_ADDR_BEG 0x1e8
#define A_MC4_BIST_ADDR_END 0x1ec
#define A_MC4_BIST_DATA 0x1f0
#define A_MC4_BIST_OP 0x1f4
#define A_MC4_INT_ENABLE 0x1f8

#define S_MC4_CORR_ERR    0
#define V_MC4_CORR_ERR(x) ((x) << S_MC4_CORR_ERR)
#define F_MC4_CORR_ERR    V_MC4_CORR_ERR(1U)

#define S_MC4_UNCORR_ERR    1
#define V_MC4_UNCORR_ERR(x) ((x) << S_MC4_UNCORR_ERR)
#define F_MC4_UNCORR_ERR    V_MC4_UNCORR_ERR(1U)

#define S_MC4_ADDR_ERR    2
#define V_MC4_ADDR_ERR(x) ((x) << S_MC4_ADDR_ERR)
#define F_MC4_ADDR_ERR    V_MC4_ADDR_ERR(1U)

#define A_MC4_INT_CAUSE 0x1fc

/* TPI registers */
#define A_TPI_ADDR 0x280

#define S_TPI_ADDRESS    0
#define M_TPI_ADDRESS    0xffffff
#define V_TPI_ADDRESS(x) ((x) << S_TPI_ADDRESS)
#define G_TPI_ADDRESS(x) (((x) >> S_TPI_ADDRESS) & M_TPI_ADDRESS)

#define A_TPI_WR_DATA 0x284
#define A_TPI_RD_DATA 0x288
#define A_TPI_CSR 0x28c

#define S_TPIWR    0
#define V_TPIWR(x) ((x) << S_TPIWR)
#define F_TPIWR    V_TPIWR(1U)

#define S_TPIRDY    1
#define V_TPIRDY(x) ((x) << S_TPIRDY)
#define F_TPIRDY    V_TPIRDY(1U)

#define S_INT_DIR    31
#define V_INT_DIR(x) ((x) << S_INT_DIR)
#define F_INT_DIR    V_INT_DIR(1U)

#define A_TPI_PAR 0x29c

#define S_TPIPAR    0
#define M_TPIPAR    0x7f
#define V_TPIPAR(x) ((x) << S_TPIPAR)
#define G_TPIPAR(x) (((x) >> S_TPIPAR) & M_TPIPAR)


/* TP registers */
#define A_TP_IN_CONFIG 0x300

#define S_TP_IN_CSPI_TUNNEL    0
#define V_TP_IN_CSPI_TUNNEL(x) ((x) << S_TP_IN_CSPI_TUNNEL)
#define F_TP_IN_CSPI_TUNNEL    V_TP_IN_CSPI_TUNNEL(1U)

#define S_TP_IN_CSPI_ETHERNET    1
#define V_TP_IN_CSPI_ETHERNET(x) ((x) << S_TP_IN_CSPI_ETHERNET)
#define F_TP_IN_CSPI_ETHERNET    V_TP_IN_CSPI_ETHERNET(1U)

#define S_TP_IN_CSPI_CPL    3
#define V_TP_IN_CSPI_CPL(x) ((x) << S_TP_IN_CSPI_CPL)
#define F_TP_IN_CSPI_CPL    V_TP_IN_CSPI_CPL(1U)

#define S_TP_IN_CSPI_POS    4
#define V_TP_IN_CSPI_POS(x) ((x) << S_TP_IN_CSPI_POS)
#define F_TP_IN_CSPI_POS    V_TP_IN_CSPI_POS(1U)

#define S_TP_IN_CSPI_CHECK_IP_CSUM    5
#define V_TP_IN_CSPI_CHECK_IP_CSUM(x) ((x) << S_TP_IN_CSPI_CHECK_IP_CSUM)
#define F_TP_IN_CSPI_CHECK_IP_CSUM    V_TP_IN_CSPI_CHECK_IP_CSUM(1U)

#define S_TP_IN_CSPI_CHECK_TCP_CSUM    6
#define V_TP_IN_CSPI_CHECK_TCP_CSUM(x) ((x) << S_TP_IN_CSPI_CHECK_TCP_CSUM)
#define F_TP_IN_CSPI_CHECK_TCP_CSUM    V_TP_IN_CSPI_CHECK_TCP_CSUM(1U)

#define S_TP_IN_ESPI_TUNNEL    7
#define V_TP_IN_ESPI_TUNNEL(x) ((x) << S_TP_IN_ESPI_TUNNEL)
#define F_TP_IN_ESPI_TUNNEL    V_TP_IN_ESPI_TUNNEL(1U)

#define S_TP_IN_ESPI_ETHERNET    8
#define V_TP_IN_ESPI_ETHERNET(x) ((x) << S_TP_IN_ESPI_ETHERNET)
#define F_TP_IN_ESPI_ETHERNET    V_TP_IN_ESPI_ETHERNET(1U)

#define S_TP_IN_ESPI_CPL    10
#define V_TP_IN_ESPI_CPL(x) ((x) << S_TP_IN_ESPI_CPL)
#define F_TP_IN_ESPI_CPL    V_TP_IN_ESPI_CPL(1U)

#define S_TP_IN_ESPI_POS    11
#define V_TP_IN_ESPI_POS(x) ((x) << S_TP_IN_ESPI_POS)
#define F_TP_IN_ESPI_POS    V_TP_IN_ESPI_POS(1U)

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

#define S_TP_OUT_C_ETH    0
#define V_TP_OUT_C_ETH(x) ((x) << S_TP_OUT_C_ETH)
#define F_TP_OUT_C_ETH    V_TP_OUT_C_ETH(1U)

#define S_TP_OUT_CSPI_CPL    2
#define V_TP_OUT_CSPI_CPL(x) ((x) << S_TP_OUT_CSPI_CPL)
#define F_TP_OUT_CSPI_CPL    V_TP_OUT_CSPI_CPL(1U)

#define S_TP_OUT_CSPI_POS    3
#define V_TP_OUT_CSPI_POS(x) ((x) << S_TP_OUT_CSPI_POS)
#define F_TP_OUT_CSPI_POS    V_TP_OUT_CSPI_POS(1U)

#define S_TP_OUT_CSPI_GENERATE_IP_CSUM    4
#define V_TP_OUT_CSPI_GENERATE_IP_CSUM(x) ((x) << S_TP_OUT_CSPI_GENERATE_IP_CSUM)
#define F_TP_OUT_CSPI_GENERATE_IP_CSUM    V_TP_OUT_CSPI_GENERATE_IP_CSUM(1U)

#define S_TP_OUT_CSPI_GENERATE_TCP_CSUM    5
#define V_TP_OUT_CSPI_GENERATE_TCP_CSUM(x) ((x) << S_TP_OUT_CSPI_GENERATE_TCP_CSUM)
#define F_TP_OUT_CSPI_GENERATE_TCP_CSUM    V_TP_OUT_CSPI_GENERATE_TCP_CSUM(1U)

#define S_TP_OUT_ESPI_ETHERNET    6
#define V_TP_OUT_ESPI_ETHERNET(x) ((x) << S_TP_OUT_ESPI_ETHERNET)
#define F_TP_OUT_ESPI_ETHERNET    V_TP_OUT_ESPI_ETHERNET(1U)

#define S_TP_OUT_ESPI_TAG_ETHERNET    7
#define V_TP_OUT_ESPI_TAG_ETHERNET(x) ((x) << S_TP_OUT_ESPI_TAG_ETHERNET)
#define F_TP_OUT_ESPI_TAG_ETHERNET    V_TP_OUT_ESPI_TAG_ETHERNET(1U)

#define S_TP_OUT_ESPI_CPL    8
#define V_TP_OUT_ESPI_CPL(x) ((x) << S_TP_OUT_ESPI_CPL)
#define F_TP_OUT_ESPI_CPL    V_TP_OUT_ESPI_CPL(1U)

#define S_TP_OUT_ESPI_POS    9
#define V_TP_OUT_ESPI_POS(x) ((x) << S_TP_OUT_ESPI_POS)
#define F_TP_OUT_ESPI_POS    V_TP_OUT_ESPI_POS(1U)

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
#define G_IP_TTL(x) (((x) >> S_IP_TTL) & M_IP_TTL)

#define S_TCAM_SERVER_REGION_USAGE    8
#define M_TCAM_SERVER_REGION_USAGE    0x3
#define V_TCAM_SERVER_REGION_USAGE(x) ((x) << S_TCAM_SERVER_REGION_USAGE)
#define G_TCAM_SERVER_REGION_USAGE(x) (((x) >> S_TCAM_SERVER_REGION_USAGE) & M_TCAM_SERVER_REGION_USAGE)

#define S_QOS_MAPPING    10
#define V_QOS_MAPPING(x) ((x) << S_QOS_MAPPING)
#define F_QOS_MAPPING    V_QOS_MAPPING(1U)

#define S_TCP_CSUM    11
#define V_TCP_CSUM(x) ((x) << S_TCP_CSUM)
#define F_TCP_CSUM    V_TCP_CSUM(1U)

#define S_UDP_CSUM    12
#define V_UDP_CSUM(x) ((x) << S_UDP_CSUM)
#define F_UDP_CSUM    V_UDP_CSUM(1U)

#define S_IP_CSUM    13
#define V_IP_CSUM(x) ((x) << S_IP_CSUM)
#define F_IP_CSUM    V_IP_CSUM(1U)

#define S_IP_ID_SPLIT    14
#define V_IP_ID_SPLIT(x) ((x) << S_IP_ID_SPLIT)
#define F_IP_ID_SPLIT    V_IP_ID_SPLIT(1U)

#define S_PATH_MTU    15
#define V_PATH_MTU(x) ((x) << S_PATH_MTU)
#define F_PATH_MTU    V_PATH_MTU(1U)

#define S_5TUPLE_LOOKUP    17
#define M_5TUPLE_LOOKUP    0x3
#define V_5TUPLE_LOOKUP(x) ((x) << S_5TUPLE_LOOKUP)
#define G_5TUPLE_LOOKUP(x) (((x) >> S_5TUPLE_LOOKUP) & M_5TUPLE_LOOKUP)

#define S_IP_FRAGMENT_DROP    19
#define V_IP_FRAGMENT_DROP(x) ((x) << S_IP_FRAGMENT_DROP)
#define F_IP_FRAGMENT_DROP    V_IP_FRAGMENT_DROP(1U)

#define S_PING_DROP    20
#define V_PING_DROP(x) ((x) << S_PING_DROP)
#define F_PING_DROP    V_PING_DROP(1U)

#define S_PROTECT_MODE    21
#define V_PROTECT_MODE(x) ((x) << S_PROTECT_MODE)
#define F_PROTECT_MODE    V_PROTECT_MODE(1U)

#define S_SYN_COOKIE_ALGORITHM    22
#define V_SYN_COOKIE_ALGORITHM(x) ((x) << S_SYN_COOKIE_ALGORITHM)
#define F_SYN_COOKIE_ALGORITHM    V_SYN_COOKIE_ALGORITHM(1U)

#define S_ATTACK_FILTER    23
#define V_ATTACK_FILTER(x) ((x) << S_ATTACK_FILTER)
#define F_ATTACK_FILTER    V_ATTACK_FILTER(1U)

#define S_INTERFACE_TYPE    24
#define V_INTERFACE_TYPE(x) ((x) << S_INTERFACE_TYPE)
#define F_INTERFACE_TYPE    V_INTERFACE_TYPE(1U)

#define S_DISABLE_RX_FLOW_CONTROL    25
#define V_DISABLE_RX_FLOW_CONTROL(x) ((x) << S_DISABLE_RX_FLOW_CONTROL)
#define F_DISABLE_RX_FLOW_CONTROL    V_DISABLE_RX_FLOW_CONTROL(1U)

#define S_SYN_COOKIE_PARAMETER    26
#define M_SYN_COOKIE_PARAMETER    0x3f
#define V_SYN_COOKIE_PARAMETER(x) ((x) << S_SYN_COOKIE_PARAMETER)
#define G_SYN_COOKIE_PARAMETER(x) (((x) >> S_SYN_COOKIE_PARAMETER) & M_SYN_COOKIE_PARAMETER)

#define A_TP_GLOBAL_RX_CREDITS 0x30c
#define A_TP_CM_SIZE 0x310
#define A_TP_CM_MM_BASE 0x314

#define S_CM_MEMMGR_BASE    0
#define M_CM_MEMMGR_BASE    0xfffffff
#define V_CM_MEMMGR_BASE(x) ((x) << S_CM_MEMMGR_BASE)
#define G_CM_MEMMGR_BASE(x) (((x) >> S_CM_MEMMGR_BASE) & M_CM_MEMMGR_BASE)

#define A_TP_CM_TIMER_BASE 0x318

#define S_CM_TIMER_BASE    0
#define M_CM_TIMER_BASE    0xfffffff
#define V_CM_TIMER_BASE(x) ((x) << S_CM_TIMER_BASE)
#define G_CM_TIMER_BASE(x) (((x) >> S_CM_TIMER_BASE) & M_CM_TIMER_BASE)

#define A_TP_PM_SIZE 0x31c
#define A_TP_PM_TX_BASE 0x320
#define A_TP_PM_DEFRAG_BASE 0x324
#define A_TP_PM_RX_BASE 0x328
#define A_TP_PM_RX_PG_SIZE 0x32c
#define A_TP_PM_RX_MAX_PGS 0x330
#define A_TP_PM_TX_PG_SIZE 0x334
#define A_TP_PM_TX_MAX_PGS 0x338
#define A_TP_TCP_OPTIONS 0x340

#define S_TIMESTAMP    0
#define M_TIMESTAMP    0x3
#define V_TIMESTAMP(x) ((x) << S_TIMESTAMP)
#define G_TIMESTAMP(x) (((x) >> S_TIMESTAMP) & M_TIMESTAMP)

#define S_WINDOW_SCALE    2
#define M_WINDOW_SCALE    0x3
#define V_WINDOW_SCALE(x) ((x) << S_WINDOW_SCALE)
#define G_WINDOW_SCALE(x) (((x) >> S_WINDOW_SCALE) & M_WINDOW_SCALE)

#define S_SACK    4
#define M_SACK    0x3
#define V_SACK(x) ((x) << S_SACK)
#define G_SACK(x) (((x) >> S_SACK) & M_SACK)

#define S_ECN    6
#define M_ECN    0x3
#define V_ECN(x) ((x) << S_ECN)
#define G_ECN(x) (((x) >> S_ECN) & M_ECN)

#define S_SACK_ALGORITHM    8
#define M_SACK_ALGORITHM    0x3
#define V_SACK_ALGORITHM(x) ((x) << S_SACK_ALGORITHM)
#define G_SACK_ALGORITHM(x) (((x) >> S_SACK_ALGORITHM) & M_SACK_ALGORITHM)

#define S_MSS    10
#define V_MSS(x) ((x) << S_MSS)
#define F_MSS    V_MSS(1U)

#define S_DEFAULT_PEER_MSS    16
#define M_DEFAULT_PEER_MSS    0xffff
#define V_DEFAULT_PEER_MSS(x) ((x) << S_DEFAULT_PEER_MSS)
#define G_DEFAULT_PEER_MSS(x) (((x) >> S_DEFAULT_PEER_MSS) & M_DEFAULT_PEER_MSS)

#define A_TP_DACK_CONFIG 0x344

#define S_DACK_MODE    0
#define V_DACK_MODE(x) ((x) << S_DACK_MODE)
#define F_DACK_MODE    V_DACK_MODE(1U)

#define S_DACK_AUTO_MGMT    1
#define V_DACK_AUTO_MGMT(x) ((x) << S_DACK_AUTO_MGMT)
#define F_DACK_AUTO_MGMT    V_DACK_AUTO_MGMT(1U)

#define S_DACK_AUTO_CAREFUL    2
#define V_DACK_AUTO_CAREFUL(x) ((x) << S_DACK_AUTO_CAREFUL)
#define F_DACK_AUTO_CAREFUL    V_DACK_AUTO_CAREFUL(1U)

#define S_DACK_MSS_SELECTOR    3
#define M_DACK_MSS_SELECTOR    0x3
#define V_DACK_MSS_SELECTOR(x) ((x) << S_DACK_MSS_SELECTOR)
#define G_DACK_MSS_SELECTOR(x) (((x) >> S_DACK_MSS_SELECTOR) & M_DACK_MSS_SELECTOR)

#define S_DACK_BYTE_THRESHOLD    5
#define M_DACK_BYTE_THRESHOLD    0xfffff
#define V_DACK_BYTE_THRESHOLD(x) ((x) << S_DACK_BYTE_THRESHOLD)
#define G_DACK_BYTE_THRESHOLD(x) (((x) >> S_DACK_BYTE_THRESHOLD) & M_DACK_BYTE_THRESHOLD)

#define A_TP_PC_CONFIG 0x348

#define S_TP_ACCESS_LATENCY    0
#define M_TP_ACCESS_LATENCY    0xf
#define V_TP_ACCESS_LATENCY(x) ((x) << S_TP_ACCESS_LATENCY)
#define G_TP_ACCESS_LATENCY(x) (((x) >> S_TP_ACCESS_LATENCY) & M_TP_ACCESS_LATENCY)

#define S_HELD_FIN_DISABLE    4
#define V_HELD_FIN_DISABLE(x) ((x) << S_HELD_FIN_DISABLE)
#define F_HELD_FIN_DISABLE    V_HELD_FIN_DISABLE(1U)

#define S_DDP_FC_ENABLE    5
#define V_DDP_FC_ENABLE(x) ((x) << S_DDP_FC_ENABLE)
#define F_DDP_FC_ENABLE    V_DDP_FC_ENABLE(1U)

#define S_RDMA_ERR_ENABLE    6
#define V_RDMA_ERR_ENABLE(x) ((x) << S_RDMA_ERR_ENABLE)
#define F_RDMA_ERR_ENABLE    V_RDMA_ERR_ENABLE(1U)

#define S_FAST_PDU_DELIVERY    7
#define V_FAST_PDU_DELIVERY(x) ((x) << S_FAST_PDU_DELIVERY)
#define F_FAST_PDU_DELIVERY    V_FAST_PDU_DELIVERY(1U)

#define S_CLEAR_FIN    8
#define V_CLEAR_FIN(x) ((x) << S_CLEAR_FIN)
#define F_CLEAR_FIN    V_CLEAR_FIN(1U)

#define S_DIS_TX_FILL_WIN_PUSH    12
#define V_DIS_TX_FILL_WIN_PUSH(x) ((x) << S_DIS_TX_FILL_WIN_PUSH)
#define F_DIS_TX_FILL_WIN_PUSH    V_DIS_TX_FILL_WIN_PUSH(1U)

#define S_TP_PC_REV    30
#define M_TP_PC_REV    0x3
#define V_TP_PC_REV(x) ((x) << S_TP_PC_REV)
#define G_TP_PC_REV(x) (((x) >> S_TP_PC_REV) & M_TP_PC_REV)

#define A_TP_BACKOFF0 0x350

#define S_ELEMENT0    0
#define M_ELEMENT0    0xff
#define V_ELEMENT0(x) ((x) << S_ELEMENT0)
#define G_ELEMENT0(x) (((x) >> S_ELEMENT0) & M_ELEMENT0)

#define S_ELEMENT1    8
#define M_ELEMENT1    0xff
#define V_ELEMENT1(x) ((x) << S_ELEMENT1)
#define G_ELEMENT1(x) (((x) >> S_ELEMENT1) & M_ELEMENT1)

#define S_ELEMENT2    16
#define M_ELEMENT2    0xff
#define V_ELEMENT2(x) ((x) << S_ELEMENT2)
#define G_ELEMENT2(x) (((x) >> S_ELEMENT2) & M_ELEMENT2)

#define S_ELEMENT3    24
#define M_ELEMENT3    0xff
#define V_ELEMENT3(x) ((x) << S_ELEMENT3)
#define G_ELEMENT3(x) (((x) >> S_ELEMENT3) & M_ELEMENT3)

#define A_TP_BACKOFF1 0x354
#define A_TP_BACKOFF2 0x358
#define A_TP_BACKOFF3 0x35c
#define A_TP_PARA_REG0 0x360

#define S_VAR_MULT    0
#define M_VAR_MULT    0xf
#define V_VAR_MULT(x) ((x) << S_VAR_MULT)
#define G_VAR_MULT(x) (((x) >> S_VAR_MULT) & M_VAR_MULT)

#define S_VAR_GAIN    4
#define M_VAR_GAIN    0xf
#define V_VAR_GAIN(x) ((x) << S_VAR_GAIN)
#define G_VAR_GAIN(x) (((x) >> S_VAR_GAIN) & M_VAR_GAIN)

#define S_SRTT_GAIN    8
#define M_SRTT_GAIN    0xf
#define V_SRTT_GAIN(x) ((x) << S_SRTT_GAIN)
#define G_SRTT_GAIN(x) (((x) >> S_SRTT_GAIN) & M_SRTT_GAIN)

#define S_RTTVAR_INIT    12
#define M_RTTVAR_INIT    0xf
#define V_RTTVAR_INIT(x) ((x) << S_RTTVAR_INIT)
#define G_RTTVAR_INIT(x) (((x) >> S_RTTVAR_INIT) & M_RTTVAR_INIT)

#define S_DUP_THRESH    20
#define M_DUP_THRESH    0xf
#define V_DUP_THRESH(x) ((x) << S_DUP_THRESH)
#define G_DUP_THRESH(x) (((x) >> S_DUP_THRESH) & M_DUP_THRESH)

#define S_INIT_CONG_WIN    24
#define M_INIT_CONG_WIN    0x7
#define V_INIT_CONG_WIN(x) ((x) << S_INIT_CONG_WIN)
#define G_INIT_CONG_WIN(x) (((x) >> S_INIT_CONG_WIN) & M_INIT_CONG_WIN)

#define A_TP_PARA_REG1 0x364

#define S_INITIAL_SLOW_START_THRESHOLD    0
#define M_INITIAL_SLOW_START_THRESHOLD    0xffff
#define V_INITIAL_SLOW_START_THRESHOLD(x) ((x) << S_INITIAL_SLOW_START_THRESHOLD)
#define G_INITIAL_SLOW_START_THRESHOLD(x) (((x) >> S_INITIAL_SLOW_START_THRESHOLD) & M_INITIAL_SLOW_START_THRESHOLD)

#define S_RECEIVE_BUFFER_SIZE    16
#define M_RECEIVE_BUFFER_SIZE    0xffff
#define V_RECEIVE_BUFFER_SIZE(x) ((x) << S_RECEIVE_BUFFER_SIZE)
#define G_RECEIVE_BUFFER_SIZE(x) (((x) >> S_RECEIVE_BUFFER_SIZE) & M_RECEIVE_BUFFER_SIZE)

#define A_TP_PARA_REG2 0x368

#define S_RX_COALESCE_SIZE    0
#define M_RX_COALESCE_SIZE    0xffff
#define V_RX_COALESCE_SIZE(x) ((x) << S_RX_COALESCE_SIZE)
#define G_RX_COALESCE_SIZE(x) (((x) >> S_RX_COALESCE_SIZE) & M_RX_COALESCE_SIZE)

#define S_MAX_RX_SIZE    16
#define M_MAX_RX_SIZE    0xffff
#define V_MAX_RX_SIZE(x) ((x) << S_MAX_RX_SIZE)
#define G_MAX_RX_SIZE(x) (((x) >> S_MAX_RX_SIZE) & M_MAX_RX_SIZE)

#define A_TP_PARA_REG3 0x36c

#define S_RX_COALESCING_PSH_DELIVER    0
#define V_RX_COALESCING_PSH_DELIVER(x) ((x) << S_RX_COALESCING_PSH_DELIVER)
#define F_RX_COALESCING_PSH_DELIVER    V_RX_COALESCING_PSH_DELIVER(1U)

#define S_RX_COALESCING_ENABLE    1
#define V_RX_COALESCING_ENABLE(x) ((x) << S_RX_COALESCING_ENABLE)
#define F_RX_COALESCING_ENABLE    V_RX_COALESCING_ENABLE(1U)

#define S_TAHOE_ENABLE    2
#define V_TAHOE_ENABLE(x) ((x) << S_TAHOE_ENABLE)
#define F_TAHOE_ENABLE    V_TAHOE_ENABLE(1U)

#define S_MAX_REORDER_FRAGMENTS    12
#define M_MAX_REORDER_FRAGMENTS    0x7
#define V_MAX_REORDER_FRAGMENTS(x) ((x) << S_MAX_REORDER_FRAGMENTS)
#define G_MAX_REORDER_FRAGMENTS(x) (((x) >> S_MAX_REORDER_FRAGMENTS) & M_MAX_REORDER_FRAGMENTS)

#define A_TP_TIMER_RESOLUTION 0x390

#define S_DELAYED_ACK_TIMER_RESOLUTION    0
#define M_DELAYED_ACK_TIMER_RESOLUTION    0x3f
#define V_DELAYED_ACK_TIMER_RESOLUTION(x) ((x) << S_DELAYED_ACK_TIMER_RESOLUTION)
#define G_DELAYED_ACK_TIMER_RESOLUTION(x) (((x) >> S_DELAYED_ACK_TIMER_RESOLUTION) & M_DELAYED_ACK_TIMER_RESOLUTION)

#define S_GENERIC_TIMER_RESOLUTION    16
#define M_GENERIC_TIMER_RESOLUTION    0x3f
#define V_GENERIC_TIMER_RESOLUTION(x) ((x) << S_GENERIC_TIMER_RESOLUTION)
#define G_GENERIC_TIMER_RESOLUTION(x) (((x) >> S_GENERIC_TIMER_RESOLUTION) & M_GENERIC_TIMER_RESOLUTION)

#define A_TP_2MSL 0x394

#define S_2MSL    0
#define M_2MSL    0x3fffffff
#define V_2MSL(x) ((x) << S_2MSL)
#define G_2MSL(x) (((x) >> S_2MSL) & M_2MSL)

#define A_TP_RXT_MIN 0x398

#define S_RETRANSMIT_TIMER_MIN    0
#define M_RETRANSMIT_TIMER_MIN    0xffff
#define V_RETRANSMIT_TIMER_MIN(x) ((x) << S_RETRANSMIT_TIMER_MIN)
#define G_RETRANSMIT_TIMER_MIN(x) (((x) >> S_RETRANSMIT_TIMER_MIN) & M_RETRANSMIT_TIMER_MIN)

#define A_TP_RXT_MAX 0x39c

#define S_RETRANSMIT_TIMER_MAX    0
#define M_RETRANSMIT_TIMER_MAX    0x3fffffff
#define V_RETRANSMIT_TIMER_MAX(x) ((x) << S_RETRANSMIT_TIMER_MAX)
#define G_RETRANSMIT_TIMER_MAX(x) (((x) >> S_RETRANSMIT_TIMER_MAX) & M_RETRANSMIT_TIMER_MAX)

#define A_TP_PERS_MIN 0x3a0

#define S_PERSIST_TIMER_MIN    0
#define M_PERSIST_TIMER_MIN    0xffff
#define V_PERSIST_TIMER_MIN(x) ((x) << S_PERSIST_TIMER_MIN)
#define G_PERSIST_TIMER_MIN(x) (((x) >> S_PERSIST_TIMER_MIN) & M_PERSIST_TIMER_MIN)

#define A_TP_PERS_MAX 0x3a4

#define S_PERSIST_TIMER_MAX    0
#define M_PERSIST_TIMER_MAX    0x3fffffff
#define V_PERSIST_TIMER_MAX(x) ((x) << S_PERSIST_TIMER_MAX)
#define G_PERSIST_TIMER_MAX(x) (((x) >> S_PERSIST_TIMER_MAX) & M_PERSIST_TIMER_MAX)

#define A_TP_KEEP_IDLE 0x3ac

#define S_KEEP_ALIVE_IDLE_TIME    0
#define M_KEEP_ALIVE_IDLE_TIME    0x3fffffff
#define V_KEEP_ALIVE_IDLE_TIME(x) ((x) << S_KEEP_ALIVE_IDLE_TIME)
#define G_KEEP_ALIVE_IDLE_TIME(x) (((x) >> S_KEEP_ALIVE_IDLE_TIME) & M_KEEP_ALIVE_IDLE_TIME)

#define A_TP_KEEP_INTVL 0x3b0

#define S_KEEP_ALIVE_INTERVAL_TIME    0
#define M_KEEP_ALIVE_INTERVAL_TIME    0x3fffffff
#define V_KEEP_ALIVE_INTERVAL_TIME(x) ((x) << S_KEEP_ALIVE_INTERVAL_TIME)
#define G_KEEP_ALIVE_INTERVAL_TIME(x) (((x) >> S_KEEP_ALIVE_INTERVAL_TIME) & M_KEEP_ALIVE_INTERVAL_TIME)

#define A_TP_INIT_SRTT 0x3b4

#define S_INITIAL_SRTT    0
#define M_INITIAL_SRTT    0xffff
#define V_INITIAL_SRTT(x) ((x) << S_INITIAL_SRTT)
#define G_INITIAL_SRTT(x) (((x) >> S_INITIAL_SRTT) & M_INITIAL_SRTT)

#define A_TP_DACK_TIME 0x3b8

#define S_DELAYED_ACK_TIME    0
#define M_DELAYED_ACK_TIME    0x7ff
#define V_DELAYED_ACK_TIME(x) ((x) << S_DELAYED_ACK_TIME)
#define G_DELAYED_ACK_TIME(x) (((x) >> S_DELAYED_ACK_TIME) & M_DELAYED_ACK_TIME)

#define A_TP_FINWAIT2_TIME 0x3bc

#define S_FINWAIT2_TIME    0
#define M_FINWAIT2_TIME    0x3fffffff
#define V_FINWAIT2_TIME(x) ((x) << S_FINWAIT2_TIME)
#define G_FINWAIT2_TIME(x) (((x) >> S_FINWAIT2_TIME) & M_FINWAIT2_TIME)

#define A_TP_FAST_FINWAIT2_TIME 0x3c0

#define S_FAST_FINWAIT2_TIME    0
#define M_FAST_FINWAIT2_TIME    0x3fffffff
#define V_FAST_FINWAIT2_TIME(x) ((x) << S_FAST_FINWAIT2_TIME)
#define G_FAST_FINWAIT2_TIME(x) (((x) >> S_FAST_FINWAIT2_TIME) & M_FAST_FINWAIT2_TIME)

#define A_TP_SHIFT_CNT 0x3c4

#define S_KEEPALIVE_MAX    0
#define M_KEEPALIVE_MAX    0xff
#define V_KEEPALIVE_MAX(x) ((x) << S_KEEPALIVE_MAX)
#define G_KEEPALIVE_MAX(x) (((x) >> S_KEEPALIVE_MAX) & M_KEEPALIVE_MAX)

#define S_WINDOWPROBE_MAX    8
#define M_WINDOWPROBE_MAX    0xff
#define V_WINDOWPROBE_MAX(x) ((x) << S_WINDOWPROBE_MAX)
#define G_WINDOWPROBE_MAX(x) (((x) >> S_WINDOWPROBE_MAX) & M_WINDOWPROBE_MAX)

#define S_RETRANSMISSION_MAX    16
#define M_RETRANSMISSION_MAX    0xff
#define V_RETRANSMISSION_MAX(x) ((x) << S_RETRANSMISSION_MAX)
#define G_RETRANSMISSION_MAX(x) (((x) >> S_RETRANSMISSION_MAX) & M_RETRANSMISSION_MAX)

#define S_SYN_MAX    24
#define M_SYN_MAX    0xff
#define V_SYN_MAX(x) ((x) << S_SYN_MAX)
#define G_SYN_MAX(x) (((x) >> S_SYN_MAX) & M_SYN_MAX)

#define A_TP_QOS_REG0 0x3e0

#define S_L3_VALUE    0
#define M_L3_VALUE    0x3f
#define V_L3_VALUE(x) ((x) << S_L3_VALUE)
#define G_L3_VALUE(x) (((x) >> S_L3_VALUE) & M_L3_VALUE)

#define A_TP_QOS_REG1 0x3e4
#define A_TP_QOS_REG2 0x3e8
#define A_TP_QOS_REG3 0x3ec
#define A_TP_QOS_REG4 0x3f0
#define A_TP_QOS_REG5 0x3f4
#define A_TP_QOS_REG6 0x3f8
#define A_TP_QOS_REG7 0x3fc
#define A_TP_MTU_REG0 0x404
#define A_TP_MTU_REG1 0x408
#define A_TP_MTU_REG2 0x40c
#define A_TP_MTU_REG3 0x410
#define A_TP_MTU_REG4 0x414
#define A_TP_MTU_REG5 0x418
#define A_TP_MTU_REG6 0x41c
#define A_TP_MTU_REG7 0x420
#define A_TP_RESET 0x44c

#define S_TP_RESET    0
#define V_TP_RESET(x) ((x) << S_TP_RESET)
#define F_TP_RESET    V_TP_RESET(1U)

#define S_CM_MEMMGR_INIT    1
#define V_CM_MEMMGR_INIT(x) ((x) << S_CM_MEMMGR_INIT)
#define F_CM_MEMMGR_INIT    V_CM_MEMMGR_INIT(1U)

#define A_TP_MIB_INDEX 0x450
#define A_TP_MIB_DATA 0x454
#define A_TP_SYNC_TIME_HI 0x458
#define A_TP_SYNC_TIME_LO 0x45c
#define A_TP_CM_MM_RX_FLST_BASE 0x460

#define S_CM_MEMMGR_RX_FREE_LIST_BASE    0
#define M_CM_MEMMGR_RX_FREE_LIST_BASE    0xfffffff
#define V_CM_MEMMGR_RX_FREE_LIST_BASE(x) ((x) << S_CM_MEMMGR_RX_FREE_LIST_BASE)
#define G_CM_MEMMGR_RX_FREE_LIST_BASE(x) (((x) >> S_CM_MEMMGR_RX_FREE_LIST_BASE) & M_CM_MEMMGR_RX_FREE_LIST_BASE)

#define A_TP_CM_MM_TX_FLST_BASE 0x464

#define S_CM_MEMMGR_TX_FREE_LIST_BASE    0
#define M_CM_MEMMGR_TX_FREE_LIST_BASE    0xfffffff
#define V_CM_MEMMGR_TX_FREE_LIST_BASE(x) ((x) << S_CM_MEMMGR_TX_FREE_LIST_BASE)
#define G_CM_MEMMGR_TX_FREE_LIST_BASE(x) (((x) >> S_CM_MEMMGR_TX_FREE_LIST_BASE) & M_CM_MEMMGR_TX_FREE_LIST_BASE)

#define A_TP_CM_MM_P_FLST_BASE 0x468

#define S_CM_MEMMGR_PSTRUCT_FREE_LIST_BASE    0
#define M_CM_MEMMGR_PSTRUCT_FREE_LIST_BASE    0xfffffff
#define V_CM_MEMMGR_PSTRUCT_FREE_LIST_BASE(x) ((x) << S_CM_MEMMGR_PSTRUCT_FREE_LIST_BASE)
#define G_CM_MEMMGR_PSTRUCT_FREE_LIST_BASE(x) (((x) >> S_CM_MEMMGR_PSTRUCT_FREE_LIST_BASE) & M_CM_MEMMGR_PSTRUCT_FREE_LIST_BASE)

#define A_TP_CM_MM_MAX_P 0x46c

#define S_CM_MEMMGR_MAX_PSTRUCT    0
#define M_CM_MEMMGR_MAX_PSTRUCT    0xfffffff
#define V_CM_MEMMGR_MAX_PSTRUCT(x) ((x) << S_CM_MEMMGR_MAX_PSTRUCT)
#define G_CM_MEMMGR_MAX_PSTRUCT(x) (((x) >> S_CM_MEMMGR_MAX_PSTRUCT) & M_CM_MEMMGR_MAX_PSTRUCT)

#define A_TP_INT_ENABLE 0x470

#define S_TX_FREE_LIST_EMPTY    0
#define V_TX_FREE_LIST_EMPTY(x) ((x) << S_TX_FREE_LIST_EMPTY)
#define F_TX_FREE_LIST_EMPTY    V_TX_FREE_LIST_EMPTY(1U)

#define S_RX_FREE_LIST_EMPTY    1
#define V_RX_FREE_LIST_EMPTY(x) ((x) << S_RX_FREE_LIST_EMPTY)
#define F_RX_FREE_LIST_EMPTY    V_RX_FREE_LIST_EMPTY(1U)

#define A_TP_INT_CAUSE 0x474
#define A_TP_TIMER_SEPARATOR 0x4a4

#define S_DISABLE_PAST_TIMER_INSERTION    0
#define V_DISABLE_PAST_TIMER_INSERTION(x) ((x) << S_DISABLE_PAST_TIMER_INSERTION)
#define F_DISABLE_PAST_TIMER_INSERTION    V_DISABLE_PAST_TIMER_INSERTION(1U)

#define S_MODULATION_TIMER_SEPARATOR    1
#define M_MODULATION_TIMER_SEPARATOR    0x7fff
#define V_MODULATION_TIMER_SEPARATOR(x) ((x) << S_MODULATION_TIMER_SEPARATOR)
#define G_MODULATION_TIMER_SEPARATOR(x) (((x) >> S_MODULATION_TIMER_SEPARATOR) & M_MODULATION_TIMER_SEPARATOR)

#define S_GLOBAL_TIMER_SEPARATOR    16
#define M_GLOBAL_TIMER_SEPARATOR    0xffff
#define V_GLOBAL_TIMER_SEPARATOR(x) ((x) << S_GLOBAL_TIMER_SEPARATOR)
#define G_GLOBAL_TIMER_SEPARATOR(x) (((x) >> S_GLOBAL_TIMER_SEPARATOR) & M_GLOBAL_TIMER_SEPARATOR)

#define A_TP_CM_FC_MODE 0x4b0
#define A_TP_PC_CONGESTION_CNTL 0x4b4
#define A_TP_TX_DROP_CONFIG 0x4b8

#define S_ENABLE_TX_DROP    31
#define V_ENABLE_TX_DROP(x) ((x) << S_ENABLE_TX_DROP)
#define F_ENABLE_TX_DROP    V_ENABLE_TX_DROP(1U)

#define S_ENABLE_TX_ERROR    30
#define V_ENABLE_TX_ERROR(x) ((x) << S_ENABLE_TX_ERROR)
#define F_ENABLE_TX_ERROR    V_ENABLE_TX_ERROR(1U)

#define S_DROP_TICKS_CNT    4
#define M_DROP_TICKS_CNT    0x3ffffff
#define V_DROP_TICKS_CNT(x) ((x) << S_DROP_TICKS_CNT)
#define G_DROP_TICKS_CNT(x) (((x) >> S_DROP_TICKS_CNT) & M_DROP_TICKS_CNT)

#define S_NUM_PKTS_DROPPED    0
#define M_NUM_PKTS_DROPPED    0xf
#define V_NUM_PKTS_DROPPED(x) ((x) << S_NUM_PKTS_DROPPED)
#define G_NUM_PKTS_DROPPED(x) (((x) >> S_NUM_PKTS_DROPPED) & M_NUM_PKTS_DROPPED)

#define A_TP_TX_DROP_COUNT 0x4bc

/* RAT registers */
#define A_RAT_ROUTE_CONTROL 0x580

#define S_USE_ROUTE_TABLE    0
#define V_USE_ROUTE_TABLE(x) ((x) << S_USE_ROUTE_TABLE)
#define F_USE_ROUTE_TABLE    V_USE_ROUTE_TABLE(1U)

#define S_ENABLE_CSPI    1
#define V_ENABLE_CSPI(x) ((x) << S_ENABLE_CSPI)
#define F_ENABLE_CSPI    V_ENABLE_CSPI(1U)

#define S_ENABLE_PCIX    2
#define V_ENABLE_PCIX(x) ((x) << S_ENABLE_PCIX)
#define F_ENABLE_PCIX    V_ENABLE_PCIX(1U)

#define A_RAT_ROUTE_TABLE_INDEX 0x584

#define S_ROUTE_TABLE_INDEX    0
#define M_ROUTE_TABLE_INDEX    0xf
#define V_ROUTE_TABLE_INDEX(x) ((x) << S_ROUTE_TABLE_INDEX)
#define G_ROUTE_TABLE_INDEX(x) (((x) >> S_ROUTE_TABLE_INDEX) & M_ROUTE_TABLE_INDEX)

#define A_RAT_ROUTE_TABLE_DATA 0x588
#define A_RAT_NO_ROUTE 0x58c

#define S_CPL_OPCODE    0
#define M_CPL_OPCODE    0xff
#define V_CPL_OPCODE(x) ((x) << S_CPL_OPCODE)
#define G_CPL_OPCODE(x) (((x) >> S_CPL_OPCODE) & M_CPL_OPCODE)

#define A_RAT_INTR_ENABLE 0x590

#define S_ZEROROUTEERROR    0
#define V_ZEROROUTEERROR(x) ((x) << S_ZEROROUTEERROR)
#define F_ZEROROUTEERROR    V_ZEROROUTEERROR(1U)

#define S_CSPIFRAMINGERROR    1
#define V_CSPIFRAMINGERROR(x) ((x) << S_CSPIFRAMINGERROR)
#define F_CSPIFRAMINGERROR    V_CSPIFRAMINGERROR(1U)

#define S_SGEFRAMINGERROR    2
#define V_SGEFRAMINGERROR(x) ((x) << S_SGEFRAMINGERROR)
#define F_SGEFRAMINGERROR    V_SGEFRAMINGERROR(1U)

#define S_TPFRAMINGERROR    3
#define V_TPFRAMINGERROR(x) ((x) << S_TPFRAMINGERROR)
#define F_TPFRAMINGERROR    V_TPFRAMINGERROR(1U)

#define A_RAT_INTR_CAUSE 0x594

/* CSPI registers */
#define A_CSPI_RX_AE_WM 0x810
#define A_CSPI_RX_AF_WM 0x814
#define A_CSPI_CALENDAR_LEN 0x818

#define S_CALENDARLENGTH    0
#define M_CALENDARLENGTH    0xffff
#define V_CALENDARLENGTH(x) ((x) << S_CALENDARLENGTH)
#define G_CALENDARLENGTH(x) (((x) >> S_CALENDARLENGTH) & M_CALENDARLENGTH)

#define A_CSPI_FIFO_STATUS_ENABLE 0x820

#define S_FIFOSTATUSENABLE    0
#define V_FIFOSTATUSENABLE(x) ((x) << S_FIFOSTATUSENABLE)
#define F_FIFOSTATUSENABLE    V_FIFOSTATUSENABLE(1U)

#define A_CSPI_MAXBURST1_MAXBURST2 0x828

#define S_MAXBURST1    0
#define M_MAXBURST1    0xffff
#define V_MAXBURST1(x) ((x) << S_MAXBURST1)
#define G_MAXBURST1(x) (((x) >> S_MAXBURST1) & M_MAXBURST1)

#define S_MAXBURST2    16
#define M_MAXBURST2    0xffff
#define V_MAXBURST2(x) ((x) << S_MAXBURST2)
#define G_MAXBURST2(x) (((x) >> S_MAXBURST2) & M_MAXBURST2)

#define A_CSPI_TRAIN 0x82c

#define S_CSPI_TRAIN_ALPHA    0
#define M_CSPI_TRAIN_ALPHA    0xffff
#define V_CSPI_TRAIN_ALPHA(x) ((x) << S_CSPI_TRAIN_ALPHA)
#define G_CSPI_TRAIN_ALPHA(x) (((x) >> S_CSPI_TRAIN_ALPHA) & M_CSPI_TRAIN_ALPHA)

#define S_CSPI_TRAIN_DATA_MAXT    16
#define M_CSPI_TRAIN_DATA_MAXT    0xffff
#define V_CSPI_TRAIN_DATA_MAXT(x) ((x) << S_CSPI_TRAIN_DATA_MAXT)
#define G_CSPI_TRAIN_DATA_MAXT(x) (((x) >> S_CSPI_TRAIN_DATA_MAXT) & M_CSPI_TRAIN_DATA_MAXT)

#define A_CSPI_INTR_STATUS 0x848

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

#define A_CSPI_INTR_ENABLE 0x84c

/* ESPI registers */
#define A_ESPI_SCH_TOKEN0 0x880

#define S_SCHTOKEN0    0
#define M_SCHTOKEN0    0xffff
#define V_SCHTOKEN0(x) ((x) << S_SCHTOKEN0)
#define G_SCHTOKEN0(x) (((x) >> S_SCHTOKEN0) & M_SCHTOKEN0)

#define A_ESPI_SCH_TOKEN1 0x884

#define S_SCHTOKEN1    0
#define M_SCHTOKEN1    0xffff
#define V_SCHTOKEN1(x) ((x) << S_SCHTOKEN1)
#define G_SCHTOKEN1(x) (((x) >> S_SCHTOKEN1) & M_SCHTOKEN1)

#define A_ESPI_SCH_TOKEN2 0x888

#define S_SCHTOKEN2    0
#define M_SCHTOKEN2    0xffff
#define V_SCHTOKEN2(x) ((x) << S_SCHTOKEN2)
#define G_SCHTOKEN2(x) (((x) >> S_SCHTOKEN2) & M_SCHTOKEN2)

#define A_ESPI_SCH_TOKEN3 0x88c

#define S_SCHTOKEN3    0
#define M_SCHTOKEN3    0xffff
#define V_SCHTOKEN3(x) ((x) << S_SCHTOKEN3)
#define G_SCHTOKEN3(x) (((x) >> S_SCHTOKEN3) & M_SCHTOKEN3)

#define A_ESPI_RX_FIFO_ALMOST_EMPTY_WATERMARK 0x890

#define S_ALMOSTEMPTY    0
#define M_ALMOSTEMPTY    0xffff
#define V_ALMOSTEMPTY(x) ((x) << S_ALMOSTEMPTY)
#define G_ALMOSTEMPTY(x) (((x) >> S_ALMOSTEMPTY) & M_ALMOSTEMPTY)

#define A_ESPI_RX_FIFO_ALMOST_FULL_WATERMARK 0x894

#define S_ALMOSTFULL    0
#define M_ALMOSTFULL    0xffff
#define V_ALMOSTFULL(x) ((x) << S_ALMOSTFULL)
#define G_ALMOSTFULL(x) (((x) >> S_ALMOSTFULL) & M_ALMOSTFULL)

#define A_ESPI_CALENDAR_LENGTH 0x898
#define A_PORT_CONFIG 0x89c

#define S_RX_NPORTS    0
#define M_RX_NPORTS    0xff
#define V_RX_NPORTS(x) ((x) << S_RX_NPORTS)
#define G_RX_NPORTS(x) (((x) >> S_RX_NPORTS) & M_RX_NPORTS)

#define S_TX_NPORTS    8
#define M_TX_NPORTS    0xff
#define V_TX_NPORTS(x) ((x) << S_TX_NPORTS)
#define G_TX_NPORTS(x) (((x) >> S_TX_NPORTS) & M_TX_NPORTS)

#define A_ESPI_FIFO_STATUS_ENABLE 0x8a0

#define S_RXSTATUSENABLE    0
#define V_RXSTATUSENABLE(x) ((x) << S_RXSTATUSENABLE)
#define F_RXSTATUSENABLE    V_RXSTATUSENABLE(1U)

#define S_TXDROPENABLE    1
#define V_TXDROPENABLE(x) ((x) << S_TXDROPENABLE)
#define F_TXDROPENABLE    V_TXDROPENABLE(1U)

#define S_RXENDIANMODE    2
#define V_RXENDIANMODE(x) ((x) << S_RXENDIANMODE)
#define F_RXENDIANMODE    V_RXENDIANMODE(1U)

#define S_TXENDIANMODE    3
#define V_TXENDIANMODE(x) ((x) << S_TXENDIANMODE)
#define F_TXENDIANMODE    V_TXENDIANMODE(1U)

#define S_INTEL1010MODE    4
#define V_INTEL1010MODE(x) ((x) << S_INTEL1010MODE)
#define F_INTEL1010MODE    V_INTEL1010MODE(1U)

#define A_ESPI_MAXBURST1_MAXBURST2 0x8a8
#define A_ESPI_TRAIN 0x8ac

#define S_MAXTRAINALPHA    0
#define M_MAXTRAINALPHA    0xffff
#define V_MAXTRAINALPHA(x) ((x) << S_MAXTRAINALPHA)
#define G_MAXTRAINALPHA(x) (((x) >> S_MAXTRAINALPHA) & M_MAXTRAINALPHA)

#define S_MAXTRAINDATA    16
#define M_MAXTRAINDATA    0xffff
#define V_MAXTRAINDATA(x) ((x) << S_MAXTRAINDATA)
#define G_MAXTRAINDATA(x) (((x) >> S_MAXTRAINDATA) & M_MAXTRAINDATA)

#define A_RAM_STATUS 0x8b0

#define S_RXFIFOPARITYERROR    0
#define M_RXFIFOPARITYERROR    0x3ff
#define V_RXFIFOPARITYERROR(x) ((x) << S_RXFIFOPARITYERROR)
#define G_RXFIFOPARITYERROR(x) (((x) >> S_RXFIFOPARITYERROR) & M_RXFIFOPARITYERROR)

#define S_TXFIFOPARITYERROR    10
#define M_TXFIFOPARITYERROR    0x3ff
#define V_TXFIFOPARITYERROR(x) ((x) << S_TXFIFOPARITYERROR)
#define G_TXFIFOPARITYERROR(x) (((x) >> S_TXFIFOPARITYERROR) & M_TXFIFOPARITYERROR)

#define S_RXFIFOOVERFLOW    20
#define M_RXFIFOOVERFLOW    0x3ff
#define V_RXFIFOOVERFLOW(x) ((x) << S_RXFIFOOVERFLOW)
#define G_RXFIFOOVERFLOW(x) (((x) >> S_RXFIFOOVERFLOW) & M_RXFIFOOVERFLOW)

#define A_TX_DROP_COUNT0 0x8b4

#define S_TXPORT0DROPCNT    0
#define M_TXPORT0DROPCNT    0xffff
#define V_TXPORT0DROPCNT(x) ((x) << S_TXPORT0DROPCNT)
#define G_TXPORT0DROPCNT(x) (((x) >> S_TXPORT0DROPCNT) & M_TXPORT0DROPCNT)

#define S_TXPORT1DROPCNT    16
#define M_TXPORT1DROPCNT    0xffff
#define V_TXPORT1DROPCNT(x) ((x) << S_TXPORT1DROPCNT)
#define G_TXPORT1DROPCNT(x) (((x) >> S_TXPORT1DROPCNT) & M_TXPORT1DROPCNT)

#define A_TX_DROP_COUNT1 0x8b8

#define S_TXPORT2DROPCNT    0
#define M_TXPORT2DROPCNT    0xffff
#define V_TXPORT2DROPCNT(x) ((x) << S_TXPORT2DROPCNT)
#define G_TXPORT2DROPCNT(x) (((x) >> S_TXPORT2DROPCNT) & M_TXPORT2DROPCNT)

#define S_TXPORT3DROPCNT    16
#define M_TXPORT3DROPCNT    0xffff
#define V_TXPORT3DROPCNT(x) ((x) << S_TXPORT3DROPCNT)
#define G_TXPORT3DROPCNT(x) (((x) >> S_TXPORT3DROPCNT) & M_TXPORT3DROPCNT)

#define A_RX_DROP_COUNT0 0x8bc

#define S_RXPORT0DROPCNT    0
#define M_RXPORT0DROPCNT    0xffff
#define V_RXPORT0DROPCNT(x) ((x) << S_RXPORT0DROPCNT)
#define G_RXPORT0DROPCNT(x) (((x) >> S_RXPORT0DROPCNT) & M_RXPORT0DROPCNT)

#define S_RXPORT1DROPCNT    16
#define M_RXPORT1DROPCNT    0xffff
#define V_RXPORT1DROPCNT(x) ((x) << S_RXPORT1DROPCNT)
#define G_RXPORT1DROPCNT(x) (((x) >> S_RXPORT1DROPCNT) & M_RXPORT1DROPCNT)

#define A_RX_DROP_COUNT1 0x8c0

#define S_RXPORT2DROPCNT    0
#define M_RXPORT2DROPCNT    0xffff
#define V_RXPORT2DROPCNT(x) ((x) << S_RXPORT2DROPCNT)
#define G_RXPORT2DROPCNT(x) (((x) >> S_RXPORT2DROPCNT) & M_RXPORT2DROPCNT)

#define S_RXPORT3DROPCNT    16
#define M_RXPORT3DROPCNT    0xffff
#define V_RXPORT3DROPCNT(x) ((x) << S_RXPORT3DROPCNT)
#define G_RXPORT3DROPCNT(x) (((x) >> S_RXPORT3DROPCNT) & M_RXPORT3DROPCNT)

#define A_DIP4_ERROR_COUNT 0x8c4

#define S_DIP4ERRORCNT    0
#define M_DIP4ERRORCNT    0xfff
#define V_DIP4ERRORCNT(x) ((x) << S_DIP4ERRORCNT)
#define G_DIP4ERRORCNT(x) (((x) >> S_DIP4ERRORCNT) & M_DIP4ERRORCNT)

#define S_DIP4ERRORCNTSHADOW    12
#define M_DIP4ERRORCNTSHADOW    0xfff
#define V_DIP4ERRORCNTSHADOW(x) ((x) << S_DIP4ERRORCNTSHADOW)
#define G_DIP4ERRORCNTSHADOW(x) (((x) >> S_DIP4ERRORCNTSHADOW) & M_DIP4ERRORCNTSHADOW)

#define S_TRICN_RX_TRAIN_ERR    24
#define V_TRICN_RX_TRAIN_ERR(x) ((x) << S_TRICN_RX_TRAIN_ERR)
#define F_TRICN_RX_TRAIN_ERR    V_TRICN_RX_TRAIN_ERR(1U)

#define S_TRICN_RX_TRAINING    25
#define V_TRICN_RX_TRAINING(x) ((x) << S_TRICN_RX_TRAINING)
#define F_TRICN_RX_TRAINING    V_TRICN_RX_TRAINING(1U)

#define S_TRICN_RX_TRAIN_OK    26
#define V_TRICN_RX_TRAIN_OK(x) ((x) << S_TRICN_RX_TRAIN_OK)
#define F_TRICN_RX_TRAIN_OK    V_TRICN_RX_TRAIN_OK(1U)

#define A_ESPI_INTR_STATUS 0x8c8

#define S_DIP2PARITYERR    5
#define V_DIP2PARITYERR(x) ((x) << S_DIP2PARITYERR)
#define F_DIP2PARITYERR    V_DIP2PARITYERR(1U)

#define A_ESPI_INTR_ENABLE 0x8cc
#define A_RX_DROP_THRESHOLD 0x8d0
#define A_ESPI_RX_RESET 0x8ec

#define S_ESPI_RX_LNK_RST    0
#define V_ESPI_RX_LNK_RST(x) ((x) << S_ESPI_RX_LNK_RST)
#define F_ESPI_RX_LNK_RST    V_ESPI_RX_LNK_RST(1U)

#define S_ESPI_RX_CORE_RST    1
#define V_ESPI_RX_CORE_RST(x) ((x) << S_ESPI_RX_CORE_RST)
#define F_ESPI_RX_CORE_RST    V_ESPI_RX_CORE_RST(1U)

#define S_RX_CLK_STATUS    2
#define V_RX_CLK_STATUS(x) ((x) << S_RX_CLK_STATUS)
#define F_RX_CLK_STATUS    V_RX_CLK_STATUS(1U)

#define A_ESPI_MISC_CONTROL 0x8f0

#define S_OUT_OF_SYNC_COUNT    0
#define M_OUT_OF_SYNC_COUNT    0xf
#define V_OUT_OF_SYNC_COUNT(x) ((x) << S_OUT_OF_SYNC_COUNT)
#define G_OUT_OF_SYNC_COUNT(x) (((x) >> S_OUT_OF_SYNC_COUNT) & M_OUT_OF_SYNC_COUNT)

#define S_DIP2_COUNT_MODE_ENABLE    4
#define V_DIP2_COUNT_MODE_ENABLE(x) ((x) << S_DIP2_COUNT_MODE_ENABLE)
#define F_DIP2_COUNT_MODE_ENABLE    V_DIP2_COUNT_MODE_ENABLE(1U)

#define S_DIP2_PARITY_ERR_THRES    5
#define M_DIP2_PARITY_ERR_THRES    0xf
#define V_DIP2_PARITY_ERR_THRES(x) ((x) << S_DIP2_PARITY_ERR_THRES)
#define G_DIP2_PARITY_ERR_THRES(x) (((x) >> S_DIP2_PARITY_ERR_THRES) & M_DIP2_PARITY_ERR_THRES)

#define S_DIP4_THRES    9
#define M_DIP4_THRES    0xfff
#define V_DIP4_THRES(x) ((x) << S_DIP4_THRES)
#define G_DIP4_THRES(x) (((x) >> S_DIP4_THRES) & M_DIP4_THRES)

#define S_DIP4_THRES_ENABLE    21
#define V_DIP4_THRES_ENABLE(x) ((x) << S_DIP4_THRES_ENABLE)
#define F_DIP4_THRES_ENABLE    V_DIP4_THRES_ENABLE(1U)

#define S_FORCE_DISABLE_STATUS    22
#define V_FORCE_DISABLE_STATUS(x) ((x) << S_FORCE_DISABLE_STATUS)
#define F_FORCE_DISABLE_STATUS    V_FORCE_DISABLE_STATUS(1U)

#define S_DYNAMIC_DESKEW    23
#define V_DYNAMIC_DESKEW(x) ((x) << S_DYNAMIC_DESKEW)
#define F_DYNAMIC_DESKEW    V_DYNAMIC_DESKEW(1U)

#define S_MONITORED_PORT_NUM    25
#define M_MONITORED_PORT_NUM    0x3
#define V_MONITORED_PORT_NUM(x) ((x) << S_MONITORED_PORT_NUM)
#define G_MONITORED_PORT_NUM(x) (((x) >> S_MONITORED_PORT_NUM) & M_MONITORED_PORT_NUM)

#define S_MONITORED_DIRECTION    27
#define V_MONITORED_DIRECTION(x) ((x) << S_MONITORED_DIRECTION)
#define F_MONITORED_DIRECTION    V_MONITORED_DIRECTION(1U)

#define S_MONITORED_INTERFACE    28
#define V_MONITORED_INTERFACE(x) ((x) << S_MONITORED_INTERFACE)
#define F_MONITORED_INTERFACE    V_MONITORED_INTERFACE(1U)

#define A_ESPI_DIP2_ERR_COUNT 0x8f4

#define S_DIP2_ERR_CNT    0
#define M_DIP2_ERR_CNT    0xf
#define V_DIP2_ERR_CNT(x) ((x) << S_DIP2_ERR_CNT)
#define G_DIP2_ERR_CNT(x) (((x) >> S_DIP2_ERR_CNT) & M_DIP2_ERR_CNT)

#define A_ESPI_CMD_ADDR 0x8f8

#define S_WRITE_DATA    0
#define M_WRITE_DATA    0xff
#define V_WRITE_DATA(x) ((x) << S_WRITE_DATA)
#define G_WRITE_DATA(x) (((x) >> S_WRITE_DATA) & M_WRITE_DATA)

#define S_REGISTER_OFFSET    8
#define M_REGISTER_OFFSET    0xf
#define V_REGISTER_OFFSET(x) ((x) << S_REGISTER_OFFSET)
#define G_REGISTER_OFFSET(x) (((x) >> S_REGISTER_OFFSET) & M_REGISTER_OFFSET)

#define S_CHANNEL_ADDR    12
#define M_CHANNEL_ADDR    0xf
#define V_CHANNEL_ADDR(x) ((x) << S_CHANNEL_ADDR)
#define G_CHANNEL_ADDR(x) (((x) >> S_CHANNEL_ADDR) & M_CHANNEL_ADDR)

#define S_MODULE_ADDR    16
#define M_MODULE_ADDR    0x3
#define V_MODULE_ADDR(x) ((x) << S_MODULE_ADDR)
#define G_MODULE_ADDR(x) (((x) >> S_MODULE_ADDR) & M_MODULE_ADDR)

#define S_BUNDLE_ADDR    20
#define M_BUNDLE_ADDR    0x3
#define V_BUNDLE_ADDR(x) ((x) << S_BUNDLE_ADDR)
#define G_BUNDLE_ADDR(x) (((x) >> S_BUNDLE_ADDR) & M_BUNDLE_ADDR)

#define S_SPI4_COMMAND    24
#define M_SPI4_COMMAND    0xff
#define V_SPI4_COMMAND(x) ((x) << S_SPI4_COMMAND)
#define G_SPI4_COMMAND(x) (((x) >> S_SPI4_COMMAND) & M_SPI4_COMMAND)

#define A_ESPI_GOSTAT 0x8fc

#define S_READ_DATA    0
#define M_READ_DATA    0xff
#define V_READ_DATA(x) ((x) << S_READ_DATA)
#define G_READ_DATA(x) (((x) >> S_READ_DATA) & M_READ_DATA)

#define S_ESPI_CMD_BUSY    8
#define V_ESPI_CMD_BUSY(x) ((x) << S_ESPI_CMD_BUSY)
#define F_ESPI_CMD_BUSY    V_ESPI_CMD_BUSY(1U)

#define S_ERROR_ACK    9
#define V_ERROR_ACK(x) ((x) << S_ERROR_ACK)
#define F_ERROR_ACK    V_ERROR_ACK(1U)

#define S_UNMAPPED_ERR    10
#define V_UNMAPPED_ERR(x) ((x) << S_UNMAPPED_ERR)
#define F_UNMAPPED_ERR    V_UNMAPPED_ERR(1U)

#define S_TRANSACTION_TIMER    16
#define M_TRANSACTION_TIMER    0xff
#define V_TRANSACTION_TIMER(x) ((x) << S_TRANSACTION_TIMER)
#define G_TRANSACTION_TIMER(x) (((x) >> S_TRANSACTION_TIMER) & M_TRANSACTION_TIMER)


/* ULP registers */
#define A_ULP_ULIMIT 0x980
#define A_ULP_TAGMASK 0x984
#define A_ULP_HREG_INDEX 0x988
#define A_ULP_HREG_DATA 0x98c
#define A_ULP_INT_ENABLE 0x990
#define A_ULP_INT_CAUSE 0x994

#define S_HREG_PAR_ERR    0
#define V_HREG_PAR_ERR(x) ((x) << S_HREG_PAR_ERR)
#define F_HREG_PAR_ERR    V_HREG_PAR_ERR(1U)

#define S_EGRS_DATA_PAR_ERR    1
#define V_EGRS_DATA_PAR_ERR(x) ((x) << S_EGRS_DATA_PAR_ERR)
#define F_EGRS_DATA_PAR_ERR    V_EGRS_DATA_PAR_ERR(1U)

#define S_INGRS_DATA_PAR_ERR    2
#define V_INGRS_DATA_PAR_ERR(x) ((x) << S_INGRS_DATA_PAR_ERR)
#define F_INGRS_DATA_PAR_ERR    V_INGRS_DATA_PAR_ERR(1U)

#define S_PM_INTR    3
#define V_PM_INTR(x) ((x) << S_PM_INTR)
#define F_PM_INTR    V_PM_INTR(1U)

#define S_PM_E2C_SYNC_ERR    4
#define V_PM_E2C_SYNC_ERR(x) ((x) << S_PM_E2C_SYNC_ERR)
#define F_PM_E2C_SYNC_ERR    V_PM_E2C_SYNC_ERR(1U)

#define S_PM_C2E_SYNC_ERR    5
#define V_PM_C2E_SYNC_ERR(x) ((x) << S_PM_C2E_SYNC_ERR)
#define F_PM_C2E_SYNC_ERR    V_PM_C2E_SYNC_ERR(1U)

#define S_PM_E2C_EMPTY_ERR    6
#define V_PM_E2C_EMPTY_ERR(x) ((x) << S_PM_E2C_EMPTY_ERR)
#define F_PM_E2C_EMPTY_ERR    V_PM_E2C_EMPTY_ERR(1U)

#define S_PM_C2E_EMPTY_ERR    7
#define V_PM_C2E_EMPTY_ERR(x) ((x) << S_PM_C2E_EMPTY_ERR)
#define F_PM_C2E_EMPTY_ERR    V_PM_C2E_EMPTY_ERR(1U)

#define S_PM_PAR_ERR    8
#define M_PM_PAR_ERR    0xffff
#define V_PM_PAR_ERR(x) ((x) << S_PM_PAR_ERR)
#define G_PM_PAR_ERR(x) (((x) >> S_PM_PAR_ERR) & M_PM_PAR_ERR)

#define S_PM_E2C_WRT_FULL    24
#define V_PM_E2C_WRT_FULL(x) ((x) << S_PM_E2C_WRT_FULL)
#define F_PM_E2C_WRT_FULL    V_PM_E2C_WRT_FULL(1U)

#define S_PM_C2E_WRT_FULL    25
#define V_PM_C2E_WRT_FULL(x) ((x) << S_PM_C2E_WRT_FULL)
#define F_PM_C2E_WRT_FULL    V_PM_C2E_WRT_FULL(1U)

#define A_ULP_PIO_CTRL 0x998

/* PL registers */
#define A_PL_ENABLE 0xa00

#define S_PL_INTR_SGE_ERR    0
#define V_PL_INTR_SGE_ERR(x) ((x) << S_PL_INTR_SGE_ERR)
#define F_PL_INTR_SGE_ERR    V_PL_INTR_SGE_ERR(1U)

#define S_PL_INTR_SGE_DATA    1
#define V_PL_INTR_SGE_DATA(x) ((x) << S_PL_INTR_SGE_DATA)
#define F_PL_INTR_SGE_DATA    V_PL_INTR_SGE_DATA(1U)

#define S_PL_INTR_MC3    2
#define V_PL_INTR_MC3(x) ((x) << S_PL_INTR_MC3)
#define F_PL_INTR_MC3    V_PL_INTR_MC3(1U)

#define S_PL_INTR_MC4    3
#define V_PL_INTR_MC4(x) ((x) << S_PL_INTR_MC4)
#define F_PL_INTR_MC4    V_PL_INTR_MC4(1U)

#define S_PL_INTR_MC5    4
#define V_PL_INTR_MC5(x) ((x) << S_PL_INTR_MC5)
#define F_PL_INTR_MC5    V_PL_INTR_MC5(1U)

#define S_PL_INTR_RAT    5
#define V_PL_INTR_RAT(x) ((x) << S_PL_INTR_RAT)
#define F_PL_INTR_RAT    V_PL_INTR_RAT(1U)

#define S_PL_INTR_TP    6
#define V_PL_INTR_TP(x) ((x) << S_PL_INTR_TP)
#define F_PL_INTR_TP    V_PL_INTR_TP(1U)

#define S_PL_INTR_ULP    7
#define V_PL_INTR_ULP(x) ((x) << S_PL_INTR_ULP)
#define F_PL_INTR_ULP    V_PL_INTR_ULP(1U)

#define S_PL_INTR_ESPI    8
#define V_PL_INTR_ESPI(x) ((x) << S_PL_INTR_ESPI)
#define F_PL_INTR_ESPI    V_PL_INTR_ESPI(1U)

#define S_PL_INTR_CSPI    9
#define V_PL_INTR_CSPI(x) ((x) << S_PL_INTR_CSPI)
#define F_PL_INTR_CSPI    V_PL_INTR_CSPI(1U)

#define S_PL_INTR_PCIX    10
#define V_PL_INTR_PCIX(x) ((x) << S_PL_INTR_PCIX)
#define F_PL_INTR_PCIX    V_PL_INTR_PCIX(1U)

#define S_PL_INTR_EXT    11
#define V_PL_INTR_EXT(x) ((x) << S_PL_INTR_EXT)
#define F_PL_INTR_EXT    V_PL_INTR_EXT(1U)

#define A_PL_CAUSE 0xa04

/* MC5 registers */
#define A_MC5_CONFIG 0xc04

#define S_MODE    0
#define V_MODE(x) ((x) << S_MODE)
#define F_MODE    V_MODE(1U)

#define S_TCAM_RESET    1
#define V_TCAM_RESET(x) ((x) << S_TCAM_RESET)
#define F_TCAM_RESET    V_TCAM_RESET(1U)

#define S_TCAM_READY    2
#define V_TCAM_READY(x) ((x) << S_TCAM_READY)
#define F_TCAM_READY    V_TCAM_READY(1U)

#define S_DBGI_ENABLE    4
#define V_DBGI_ENABLE(x) ((x) << S_DBGI_ENABLE)
#define F_DBGI_ENABLE    V_DBGI_ENABLE(1U)

#define S_M_BUS_ENABLE    5
#define V_M_BUS_ENABLE(x) ((x) << S_M_BUS_ENABLE)
#define F_M_BUS_ENABLE    V_M_BUS_ENABLE(1U)

#define S_PARITY_ENABLE    6
#define V_PARITY_ENABLE(x) ((x) << S_PARITY_ENABLE)
#define F_PARITY_ENABLE    V_PARITY_ENABLE(1U)

#define S_SYN_ISSUE_MODE    7
#define M_SYN_ISSUE_MODE    0x3
#define V_SYN_ISSUE_MODE(x) ((x) << S_SYN_ISSUE_MODE)
#define G_SYN_ISSUE_MODE(x) (((x) >> S_SYN_ISSUE_MODE) & M_SYN_ISSUE_MODE)

#define S_BUILD    16
#define V_BUILD(x) ((x) << S_BUILD)
#define F_BUILD    V_BUILD(1U)

#define S_COMPRESSION_ENABLE    17
#define V_COMPRESSION_ENABLE(x) ((x) << S_COMPRESSION_ENABLE)
#define F_COMPRESSION_ENABLE    V_COMPRESSION_ENABLE(1U)

#define S_NUM_LIP    18
#define M_NUM_LIP    0x3f
#define V_NUM_LIP(x) ((x) << S_NUM_LIP)
#define G_NUM_LIP(x) (((x) >> S_NUM_LIP) & M_NUM_LIP)

#define S_TCAM_PART_CNT    24
#define M_TCAM_PART_CNT    0x3
#define V_TCAM_PART_CNT(x) ((x) << S_TCAM_PART_CNT)
#define G_TCAM_PART_CNT(x) (((x) >> S_TCAM_PART_CNT) & M_TCAM_PART_CNT)

#define S_TCAM_PART_TYPE    26
#define M_TCAM_PART_TYPE    0x3
#define V_TCAM_PART_TYPE(x) ((x) << S_TCAM_PART_TYPE)
#define G_TCAM_PART_TYPE(x) (((x) >> S_TCAM_PART_TYPE) & M_TCAM_PART_TYPE)

#define S_TCAM_PART_SIZE    28
#define M_TCAM_PART_SIZE    0x3
#define V_TCAM_PART_SIZE(x) ((x) << S_TCAM_PART_SIZE)
#define G_TCAM_PART_SIZE(x) (((x) >> S_TCAM_PART_SIZE) & M_TCAM_PART_SIZE)

#define S_TCAM_PART_TYPE_HI    30
#define V_TCAM_PART_TYPE_HI(x) ((x) << S_TCAM_PART_TYPE_HI)
#define F_TCAM_PART_TYPE_HI    V_TCAM_PART_TYPE_HI(1U)

#define A_MC5_SIZE 0xc08

#define S_SIZE    0
#define M_SIZE    0x3fffff
#define V_SIZE(x) ((x) << S_SIZE)
#define G_SIZE(x) (((x) >> S_SIZE) & M_SIZE)

#define A_MC5_ROUTING_TABLE_INDEX 0xc0c

#define S_START_OF_ROUTING_TABLE    0
#define M_START_OF_ROUTING_TABLE    0x3fffff
#define V_START_OF_ROUTING_TABLE(x) ((x) << S_START_OF_ROUTING_TABLE)
#define G_START_OF_ROUTING_TABLE(x) (((x) >> S_START_OF_ROUTING_TABLE) & M_START_OF_ROUTING_TABLE)

#define A_MC5_SERVER_INDEX 0xc14

#define S_START_OF_SERVER_INDEX    0
#define M_START_OF_SERVER_INDEX    0x3fffff
#define V_START_OF_SERVER_INDEX(x) ((x) << S_START_OF_SERVER_INDEX)
#define G_START_OF_SERVER_INDEX(x) (((x) >> S_START_OF_SERVER_INDEX) & M_START_OF_SERVER_INDEX)

#define A_MC5_LIP_RAM_ADDR 0xc18

#define S_LOCAL_IP_RAM_ADDR    0
#define M_LOCAL_IP_RAM_ADDR    0x3f
#define V_LOCAL_IP_RAM_ADDR(x) ((x) << S_LOCAL_IP_RAM_ADDR)
#define G_LOCAL_IP_RAM_ADDR(x) (((x) >> S_LOCAL_IP_RAM_ADDR) & M_LOCAL_IP_RAM_ADDR)

#define S_RAM_WRITE_ENABLE    8
#define V_RAM_WRITE_ENABLE(x) ((x) << S_RAM_WRITE_ENABLE)
#define F_RAM_WRITE_ENABLE    V_RAM_WRITE_ENABLE(1U)

#define A_MC5_LIP_RAM_DATA 0xc1c
#define A_MC5_RSP_LATENCY 0xc20

#define S_SEARCH_RESPONSE_LATENCY    0
#define M_SEARCH_RESPONSE_LATENCY    0x1f
#define V_SEARCH_RESPONSE_LATENCY(x) ((x) << S_SEARCH_RESPONSE_LATENCY)
#define G_SEARCH_RESPONSE_LATENCY(x) (((x) >> S_SEARCH_RESPONSE_LATENCY) & M_SEARCH_RESPONSE_LATENCY)

#define S_LEARN_RESPONSE_LATENCY    8
#define M_LEARN_RESPONSE_LATENCY    0x1f
#define V_LEARN_RESPONSE_LATENCY(x) ((x) << S_LEARN_RESPONSE_LATENCY)
#define G_LEARN_RESPONSE_LATENCY(x) (((x) >> S_LEARN_RESPONSE_LATENCY) & M_LEARN_RESPONSE_LATENCY)

#define A_MC5_PARITY_LATENCY 0xc24

#define S_SRCHLAT    0
#define M_SRCHLAT    0x1f
#define V_SRCHLAT(x) ((x) << S_SRCHLAT)
#define G_SRCHLAT(x) (((x) >> S_SRCHLAT) & M_SRCHLAT)

#define S_PARLAT    8
#define M_PARLAT    0x1f
#define V_PARLAT(x) ((x) << S_PARLAT)
#define G_PARLAT(x) (((x) >> S_PARLAT) & M_PARLAT)

#define A_MC5_WR_LRN_VERIFY 0xc28

#define S_POVEREN    0
#define V_POVEREN(x) ((x) << S_POVEREN)
#define F_POVEREN    V_POVEREN(1U)

#define S_LRNVEREN    1
#define V_LRNVEREN(x) ((x) << S_LRNVEREN)
#define F_LRNVEREN    V_LRNVEREN(1U)

#define S_VWVEREN    2
#define V_VWVEREN(x) ((x) << S_VWVEREN)
#define F_VWVEREN    V_VWVEREN(1U)

#define A_MC5_PART_ID_INDEX 0xc2c

#define S_IDINDEX    0
#define M_IDINDEX    0xf
#define V_IDINDEX(x) ((x) << S_IDINDEX)
#define G_IDINDEX(x) (((x) >> S_IDINDEX) & M_IDINDEX)

#define A_MC5_RESET_MAX 0xc30

#define S_RSTMAX    0
#define M_RSTMAX    0x1ff
#define V_RSTMAX(x) ((x) << S_RSTMAX)
#define G_RSTMAX(x) (((x) >> S_RSTMAX) & M_RSTMAX)

#define A_MC5_INT_ENABLE 0xc40

#define S_MC5_INT_HIT_OUT_ACTIVE_REGION_ERR    0
#define V_MC5_INT_HIT_OUT_ACTIVE_REGION_ERR(x) ((x) << S_MC5_INT_HIT_OUT_ACTIVE_REGION_ERR)
#define F_MC5_INT_HIT_OUT_ACTIVE_REGION_ERR    V_MC5_INT_HIT_OUT_ACTIVE_REGION_ERR(1U)

#define S_MC5_INT_HIT_IN_ACTIVE_REGION_ERR    1
#define V_MC5_INT_HIT_IN_ACTIVE_REGION_ERR(x) ((x) << S_MC5_INT_HIT_IN_ACTIVE_REGION_ERR)
#define F_MC5_INT_HIT_IN_ACTIVE_REGION_ERR    V_MC5_INT_HIT_IN_ACTIVE_REGION_ERR(1U)

#define S_MC5_INT_HIT_IN_RT_REGION_ERR    2
#define V_MC5_INT_HIT_IN_RT_REGION_ERR(x) ((x) << S_MC5_INT_HIT_IN_RT_REGION_ERR)
#define F_MC5_INT_HIT_IN_RT_REGION_ERR    V_MC5_INT_HIT_IN_RT_REGION_ERR(1U)

#define S_MC5_INT_MISS_ERR    3
#define V_MC5_INT_MISS_ERR(x) ((x) << S_MC5_INT_MISS_ERR)
#define F_MC5_INT_MISS_ERR    V_MC5_INT_MISS_ERR(1U)

#define S_MC5_INT_LIP0_ERR    4
#define V_MC5_INT_LIP0_ERR(x) ((x) << S_MC5_INT_LIP0_ERR)
#define F_MC5_INT_LIP0_ERR    V_MC5_INT_LIP0_ERR(1U)

#define S_MC5_INT_LIP_MISS_ERR    5
#define V_MC5_INT_LIP_MISS_ERR(x) ((x) << S_MC5_INT_LIP_MISS_ERR)
#define F_MC5_INT_LIP_MISS_ERR    V_MC5_INT_LIP_MISS_ERR(1U)

#define S_MC5_INT_PARITY_ERR    6
#define V_MC5_INT_PARITY_ERR(x) ((x) << S_MC5_INT_PARITY_ERR)
#define F_MC5_INT_PARITY_ERR    V_MC5_INT_PARITY_ERR(1U)

#define S_MC5_INT_ACTIVE_REGION_FULL    7
#define V_MC5_INT_ACTIVE_REGION_FULL(x) ((x) << S_MC5_INT_ACTIVE_REGION_FULL)
#define F_MC5_INT_ACTIVE_REGION_FULL    V_MC5_INT_ACTIVE_REGION_FULL(1U)

#define S_MC5_INT_NFA_SRCH_ERR    8
#define V_MC5_INT_NFA_SRCH_ERR(x) ((x) << S_MC5_INT_NFA_SRCH_ERR)
#define F_MC5_INT_NFA_SRCH_ERR    V_MC5_INT_NFA_SRCH_ERR(1U)

#define S_MC5_INT_SYN_COOKIE    9
#define V_MC5_INT_SYN_COOKIE(x) ((x) << S_MC5_INT_SYN_COOKIE)
#define F_MC5_INT_SYN_COOKIE    V_MC5_INT_SYN_COOKIE(1U)

#define S_MC5_INT_SYN_COOKIE_BAD    10
#define V_MC5_INT_SYN_COOKIE_BAD(x) ((x) << S_MC5_INT_SYN_COOKIE_BAD)
#define F_MC5_INT_SYN_COOKIE_BAD    V_MC5_INT_SYN_COOKIE_BAD(1U)

#define S_MC5_INT_SYN_COOKIE_OFF    11
#define V_MC5_INT_SYN_COOKIE_OFF(x) ((x) << S_MC5_INT_SYN_COOKIE_OFF)
#define F_MC5_INT_SYN_COOKIE_OFF    V_MC5_INT_SYN_COOKIE_OFF(1U)

#define S_MC5_INT_UNKNOWN_CMD    15
#define V_MC5_INT_UNKNOWN_CMD(x) ((x) << S_MC5_INT_UNKNOWN_CMD)
#define F_MC5_INT_UNKNOWN_CMD    V_MC5_INT_UNKNOWN_CMD(1U)

#define S_MC5_INT_REQUESTQ_PARITY_ERR    16
#define V_MC5_INT_REQUESTQ_PARITY_ERR(x) ((x) << S_MC5_INT_REQUESTQ_PARITY_ERR)
#define F_MC5_INT_REQUESTQ_PARITY_ERR    V_MC5_INT_REQUESTQ_PARITY_ERR(1U)

#define S_MC5_INT_DISPATCHQ_PARITY_ERR    17
#define V_MC5_INT_DISPATCHQ_PARITY_ERR(x) ((x) << S_MC5_INT_DISPATCHQ_PARITY_ERR)
#define F_MC5_INT_DISPATCHQ_PARITY_ERR    V_MC5_INT_DISPATCHQ_PARITY_ERR(1U)

#define S_MC5_INT_DEL_ACT_EMPTY    18
#define V_MC5_INT_DEL_ACT_EMPTY(x) ((x) << S_MC5_INT_DEL_ACT_EMPTY)
#define F_MC5_INT_DEL_ACT_EMPTY    V_MC5_INT_DEL_ACT_EMPTY(1U)

#define A_MC5_INT_CAUSE 0xc44
#define A_MC5_INT_TID 0xc48
#define A_MC5_INT_PTID 0xc4c
#define A_MC5_DBGI_CONFIG 0xc74
#define A_MC5_DBGI_REQ_CMD 0xc78

#define S_CMDMODE    0
#define M_CMDMODE    0x7
#define V_CMDMODE(x) ((x) << S_CMDMODE)
#define G_CMDMODE(x) (((x) >> S_CMDMODE) & M_CMDMODE)

#define S_SADRSEL    4
#define V_SADRSEL(x) ((x) << S_SADRSEL)
#define F_SADRSEL    V_SADRSEL(1U)

#define S_WRITE_BURST_SIZE    22
#define M_WRITE_BURST_SIZE    0x3ff
#define V_WRITE_BURST_SIZE(x) ((x) << S_WRITE_BURST_SIZE)
#define G_WRITE_BURST_SIZE(x) (((x) >> S_WRITE_BURST_SIZE) & M_WRITE_BURST_SIZE)

#define A_MC5_DBGI_REQ_ADDR0 0xc7c
#define A_MC5_DBGI_REQ_ADDR1 0xc80
#define A_MC5_DBGI_REQ_ADDR2 0xc84
#define A_MC5_DBGI_REQ_DATA0 0xc88
#define A_MC5_DBGI_REQ_DATA1 0xc8c
#define A_MC5_DBGI_REQ_DATA2 0xc90
#define A_MC5_DBGI_REQ_DATA3 0xc94
#define A_MC5_DBGI_REQ_DATA4 0xc98
#define A_MC5_DBGI_REQ_MASK0 0xc9c
#define A_MC5_DBGI_REQ_MASK1 0xca0
#define A_MC5_DBGI_REQ_MASK2 0xca4
#define A_MC5_DBGI_REQ_MASK3 0xca8
#define A_MC5_DBGI_REQ_MASK4 0xcac
#define A_MC5_DBGI_RSP_STATUS 0xcb0

#define S_DBGI_RSP_VALID    0
#define V_DBGI_RSP_VALID(x) ((x) << S_DBGI_RSP_VALID)
#define F_DBGI_RSP_VALID    V_DBGI_RSP_VALID(1U)

#define S_DBGI_RSP_HIT    1
#define V_DBGI_RSP_HIT(x) ((x) << S_DBGI_RSP_HIT)
#define F_DBGI_RSP_HIT    V_DBGI_RSP_HIT(1U)

#define S_DBGI_RSP_ERR    2
#define V_DBGI_RSP_ERR(x) ((x) << S_DBGI_RSP_ERR)
#define F_DBGI_RSP_ERR    V_DBGI_RSP_ERR(1U)

#define S_DBGI_RSP_ERR_REASON    8
#define M_DBGI_RSP_ERR_REASON    0x7
#define V_DBGI_RSP_ERR_REASON(x) ((x) << S_DBGI_RSP_ERR_REASON)
#define G_DBGI_RSP_ERR_REASON(x) (((x) >> S_DBGI_RSP_ERR_REASON) & M_DBGI_RSP_ERR_REASON)

#define A_MC5_DBGI_RSP_DATA0 0xcb4
#define A_MC5_DBGI_RSP_DATA1 0xcb8
#define A_MC5_DBGI_RSP_DATA2 0xcbc
#define A_MC5_DBGI_RSP_DATA3 0xcc0
#define A_MC5_DBGI_RSP_DATA4 0xcc4
#define A_MC5_DBGI_RSP_LAST_CMD 0xcc8
#define A_MC5_POPEN_DATA_WR_CMD 0xccc
#define A_MC5_POPEN_MASK_WR_CMD 0xcd0
#define A_MC5_AOPEN_SRCH_CMD 0xcd4
#define A_MC5_AOPEN_LRN_CMD 0xcd8
#define A_MC5_SYN_SRCH_CMD 0xcdc
#define A_MC5_SYN_LRN_CMD 0xce0
#define A_MC5_ACK_SRCH_CMD 0xce4
#define A_MC5_ACK_LRN_CMD 0xce8
#define A_MC5_ILOOKUP_CMD 0xcec
#define A_MC5_ELOOKUP_CMD 0xcf0
#define A_MC5_DATA_WRITE_CMD 0xcf4
#define A_MC5_DATA_READ_CMD 0xcf8
#define A_MC5_MASK_WRITE_CMD 0xcfc

/* PCICFG registers */
#define A_PCICFG_PM_CSR 0x44
#define A_PCICFG_VPD_ADDR 0x4a

#define S_VPD_ADDR    0
#define M_VPD_ADDR    0x7fff
#define V_VPD_ADDR(x) ((x) << S_VPD_ADDR)
#define G_VPD_ADDR(x) (((x) >> S_VPD_ADDR) & M_VPD_ADDR)

#define S_VPD_OP_FLAG    15
#define V_VPD_OP_FLAG(x) ((x) << S_VPD_OP_FLAG)
#define F_VPD_OP_FLAG    V_VPD_OP_FLAG(1U)

#define A_PCICFG_VPD_DATA 0x4c
#define A_PCICFG_PCIX_CMD 0x60
#define A_PCICFG_INTR_ENABLE 0xf4

#define S_MASTER_PARITY_ERR    0
#define V_MASTER_PARITY_ERR(x) ((x) << S_MASTER_PARITY_ERR)
#define F_MASTER_PARITY_ERR    V_MASTER_PARITY_ERR(1U)

#define S_SIG_TARGET_ABORT    1
#define V_SIG_TARGET_ABORT(x) ((x) << S_SIG_TARGET_ABORT)
#define F_SIG_TARGET_ABORT    V_SIG_TARGET_ABORT(1U)

#define S_RCV_TARGET_ABORT    2
#define V_RCV_TARGET_ABORT(x) ((x) << S_RCV_TARGET_ABORT)
#define F_RCV_TARGET_ABORT    V_RCV_TARGET_ABORT(1U)

#define S_RCV_MASTER_ABORT    3
#define V_RCV_MASTER_ABORT(x) ((x) << S_RCV_MASTER_ABORT)
#define F_RCV_MASTER_ABORT    V_RCV_MASTER_ABORT(1U)

#define S_SIG_SYS_ERR    4
#define V_SIG_SYS_ERR(x) ((x) << S_SIG_SYS_ERR)
#define F_SIG_SYS_ERR    V_SIG_SYS_ERR(1U)

#define S_DET_PARITY_ERR    5
#define V_DET_PARITY_ERR(x) ((x) << S_DET_PARITY_ERR)
#define F_DET_PARITY_ERR    V_DET_PARITY_ERR(1U)

#define S_PIO_PARITY_ERR    6
#define V_PIO_PARITY_ERR(x) ((x) << S_PIO_PARITY_ERR)
#define F_PIO_PARITY_ERR    V_PIO_PARITY_ERR(1U)

#define S_WF_PARITY_ERR    7
#define V_WF_PARITY_ERR(x) ((x) << S_WF_PARITY_ERR)
#define F_WF_PARITY_ERR    V_WF_PARITY_ERR(1U)

#define S_RF_PARITY_ERR    8
#define M_RF_PARITY_ERR    0x3
#define V_RF_PARITY_ERR(x) ((x) << S_RF_PARITY_ERR)
#define G_RF_PARITY_ERR(x) (((x) >> S_RF_PARITY_ERR) & M_RF_PARITY_ERR)

#define S_CF_PARITY_ERR    10
#define M_CF_PARITY_ERR    0x3
#define V_CF_PARITY_ERR(x) ((x) << S_CF_PARITY_ERR)
#define G_CF_PARITY_ERR(x) (((x) >> S_CF_PARITY_ERR) & M_CF_PARITY_ERR)

#define A_PCICFG_INTR_CAUSE 0xf8
#define A_PCICFG_MODE 0xfc

#define S_PCI_MODE_64BIT    0
#define V_PCI_MODE_64BIT(x) ((x) << S_PCI_MODE_64BIT)
#define F_PCI_MODE_64BIT    V_PCI_MODE_64BIT(1U)

#define S_PCI_MODE_66MHZ    1
#define V_PCI_MODE_66MHZ(x) ((x) << S_PCI_MODE_66MHZ)
#define F_PCI_MODE_66MHZ    V_PCI_MODE_66MHZ(1U)

#define S_PCI_MODE_PCIX_INITPAT    2
#define M_PCI_MODE_PCIX_INITPAT    0x7
#define V_PCI_MODE_PCIX_INITPAT(x) ((x) << S_PCI_MODE_PCIX_INITPAT)
#define G_PCI_MODE_PCIX_INITPAT(x) (((x) >> S_PCI_MODE_PCIX_INITPAT) & M_PCI_MODE_PCIX_INITPAT)

#define S_PCI_MODE_PCIX    5
#define V_PCI_MODE_PCIX(x) ((x) << S_PCI_MODE_PCIX)
#define F_PCI_MODE_PCIX    V_PCI_MODE_PCIX(1U)

#define S_PCI_MODE_CLK    6
#define M_PCI_MODE_CLK    0x3
#define V_PCI_MODE_CLK(x) ((x) << S_PCI_MODE_CLK)
#define G_PCI_MODE_CLK(x) (((x) >> S_PCI_MODE_CLK) & M_PCI_MODE_CLK)

#endif /* _CXGB_REGS_H_ */
