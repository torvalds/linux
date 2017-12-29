/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2014 Chelsio Communications, Inc. All rights reserved.
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

#ifndef __T4_REGS_H
#define __T4_REGS_H

#define MYPF_BASE 0x1b000
#define MYPF_REG(reg_addr) (MYPF_BASE + (reg_addr))

#define PF0_BASE 0x1e000
#define PF0_REG(reg_addr) (PF0_BASE + (reg_addr))

#define PF_STRIDE 0x400
#define PF_BASE(idx) (PF0_BASE + (idx) * PF_STRIDE)
#define PF_REG(idx, reg) (PF_BASE(idx) + (reg))

#define MYPORT_BASE 0x1c000
#define MYPORT_REG(reg_addr) (MYPORT_BASE + (reg_addr))

#define PORT0_BASE 0x20000
#define PORT0_REG(reg_addr) (PORT0_BASE + (reg_addr))

#define PORT_STRIDE 0x2000
#define PORT_BASE(idx) (PORT0_BASE + (idx) * PORT_STRIDE)
#define PORT_REG(idx, reg) (PORT_BASE(idx) + (reg))

#define EDC_STRIDE (EDC_1_BASE_ADDR - EDC_0_BASE_ADDR)
#define EDC_REG(reg, idx) (reg + EDC_STRIDE * idx)

#define PCIE_MEM_ACCESS_REG(reg_addr, idx) ((reg_addr) + (idx) * 8)
#define PCIE_MAILBOX_REG(reg_addr, idx) ((reg_addr) + (idx) * 8)
#define MC_BIST_STATUS_REG(reg_addr, idx) ((reg_addr) + (idx) * 4)
#define EDC_BIST_STATUS_REG(reg_addr, idx) ((reg_addr) + (idx) * 4)

#define PCIE_FW_REG(reg_addr, idx) ((reg_addr) + (idx) * 4)

#define NUM_LE_DB_DBGI_REQ_DATA_INSTANCES 17
#define NUM_LE_DB_DBGI_RSP_DATA_INSTANCES 17

#define SGE_PF_KDOORBELL_A 0x0

#define QID_S    15
#define QID_V(x) ((x) << QID_S)

#define DBPRIO_S    14
#define DBPRIO_V(x) ((x) << DBPRIO_S)
#define DBPRIO_F    DBPRIO_V(1U)

#define PIDX_S    0
#define PIDX_V(x) ((x) << PIDX_S)

#define SGE_VF_KDOORBELL_A 0x0

#define DBTYPE_S    13
#define DBTYPE_V(x) ((x) << DBTYPE_S)
#define DBTYPE_F    DBTYPE_V(1U)

#define PIDX_T5_S    0
#define PIDX_T5_M    0x1fffU
#define PIDX_T5_V(x) ((x) << PIDX_T5_S)
#define PIDX_T5_G(x) (((x) >> PIDX_T5_S) & PIDX_T5_M)

#define SGE_PF_GTS_A 0x4

#define INGRESSQID_S    16
#define INGRESSQID_V(x) ((x) << INGRESSQID_S)

#define TIMERREG_S    13
#define TIMERREG_V(x) ((x) << TIMERREG_S)

#define SEINTARM_S    12
#define SEINTARM_V(x) ((x) << SEINTARM_S)

#define CIDXINC_S    0
#define CIDXINC_M    0xfffU
#define CIDXINC_V(x) ((x) << CIDXINC_S)

#define SGE_CONTROL_A	0x1008
#define SGE_CONTROL2_A	0x1124

#define RXPKTCPLMODE_S    18
#define RXPKTCPLMODE_V(x) ((x) << RXPKTCPLMODE_S)
#define RXPKTCPLMODE_F    RXPKTCPLMODE_V(1U)

#define EGRSTATUSPAGESIZE_S    17
#define EGRSTATUSPAGESIZE_V(x) ((x) << EGRSTATUSPAGESIZE_S)
#define EGRSTATUSPAGESIZE_F    EGRSTATUSPAGESIZE_V(1U)

#define PKTSHIFT_S    10
#define PKTSHIFT_M    0x7U
#define PKTSHIFT_V(x) ((x) << PKTSHIFT_S)
#define PKTSHIFT_G(x) (((x) >> PKTSHIFT_S) & PKTSHIFT_M)

#define INGPCIEBOUNDARY_S    7
#define INGPCIEBOUNDARY_V(x) ((x) << INGPCIEBOUNDARY_S)

#define INGPADBOUNDARY_S    4
#define INGPADBOUNDARY_M    0x7U
#define INGPADBOUNDARY_V(x) ((x) << INGPADBOUNDARY_S)
#define INGPADBOUNDARY_G(x) (((x) >> INGPADBOUNDARY_S) & INGPADBOUNDARY_M)

#define EGRPCIEBOUNDARY_S    1
#define EGRPCIEBOUNDARY_V(x) ((x) << EGRPCIEBOUNDARY_S)

#define  INGPACKBOUNDARY_S	16
#define  INGPACKBOUNDARY_M	0x7U
#define  INGPACKBOUNDARY_V(x)	((x) << INGPACKBOUNDARY_S)
#define  INGPACKBOUNDARY_G(x)	(((x) >> INGPACKBOUNDARY_S) \
				 & INGPACKBOUNDARY_M)

#define VFIFO_ENABLE_S    10
#define VFIFO_ENABLE_V(x) ((x) << VFIFO_ENABLE_S)
#define VFIFO_ENABLE_F    VFIFO_ENABLE_V(1U)

#define SGE_DBVFIFO_BADDR_A 0x1138

#define DBVFIFO_SIZE_S    6
#define DBVFIFO_SIZE_M    0xfffU
#define DBVFIFO_SIZE_G(x) (((x) >> DBVFIFO_SIZE_S) & DBVFIFO_SIZE_M)

#define T6_DBVFIFO_SIZE_S    0
#define T6_DBVFIFO_SIZE_M    0x1fffU
#define T6_DBVFIFO_SIZE_G(x) (((x) >> T6_DBVFIFO_SIZE_S) & T6_DBVFIFO_SIZE_M)

#define SGE_CTXT_CMD_A 0x11fc

#define BUSY_S    31
#define BUSY_V(x) ((x) << BUSY_S)
#define BUSY_F    BUSY_V(1U)

#define CTXTTYPE_S    24
#define CTXTTYPE_M    0x3U
#define CTXTTYPE_V(x) ((x) << CTXTTYPE_S)

#define CTXTQID_S    0
#define CTXTQID_M    0x1ffffU
#define CTXTQID_V(x) ((x) << CTXTQID_S)

#define SGE_CTXT_DATA0_A 0x1200
#define SGE_CTXT_DATA5_A 0x1214

#define GLOBALENABLE_S    0
#define GLOBALENABLE_V(x) ((x) << GLOBALENABLE_S)
#define GLOBALENABLE_F    GLOBALENABLE_V(1U)

#define SGE_HOST_PAGE_SIZE_A 0x100c

#define HOSTPAGESIZEPF7_S    28
#define HOSTPAGESIZEPF7_M    0xfU
#define HOSTPAGESIZEPF7_V(x) ((x) << HOSTPAGESIZEPF7_S)
#define HOSTPAGESIZEPF7_G(x) (((x) >> HOSTPAGESIZEPF7_S) & HOSTPAGESIZEPF7_M)

#define HOSTPAGESIZEPF6_S    24
#define HOSTPAGESIZEPF6_M    0xfU
#define HOSTPAGESIZEPF6_V(x) ((x) << HOSTPAGESIZEPF6_S)
#define HOSTPAGESIZEPF6_G(x) (((x) >> HOSTPAGESIZEPF6_S) & HOSTPAGESIZEPF6_M)

#define HOSTPAGESIZEPF5_S    20
#define HOSTPAGESIZEPF5_M    0xfU
#define HOSTPAGESIZEPF5_V(x) ((x) << HOSTPAGESIZEPF5_S)
#define HOSTPAGESIZEPF5_G(x) (((x) >> HOSTPAGESIZEPF5_S) & HOSTPAGESIZEPF5_M)

#define HOSTPAGESIZEPF4_S    16
#define HOSTPAGESIZEPF4_M    0xfU
#define HOSTPAGESIZEPF4_V(x) ((x) << HOSTPAGESIZEPF4_S)
#define HOSTPAGESIZEPF4_G(x) (((x) >> HOSTPAGESIZEPF4_S) & HOSTPAGESIZEPF4_M)

#define HOSTPAGESIZEPF3_S    12
#define HOSTPAGESIZEPF3_M    0xfU
#define HOSTPAGESIZEPF3_V(x) ((x) << HOSTPAGESIZEPF3_S)
#define HOSTPAGESIZEPF3_G(x) (((x) >> HOSTPAGESIZEPF3_S) & HOSTPAGESIZEPF3_M)

#define HOSTPAGESIZEPF2_S    8
#define HOSTPAGESIZEPF2_M    0xfU
#define HOSTPAGESIZEPF2_V(x) ((x) << HOSTPAGESIZEPF2_S)
#define HOSTPAGESIZEPF2_G(x) (((x) >> HOSTPAGESIZEPF2_S) & HOSTPAGESIZEPF2_M)

#define HOSTPAGESIZEPF1_S    4
#define HOSTPAGESIZEPF1_M    0xfU
#define HOSTPAGESIZEPF1_V(x) ((x) << HOSTPAGESIZEPF1_S)
#define HOSTPAGESIZEPF1_G(x) (((x) >> HOSTPAGESIZEPF1_S) & HOSTPAGESIZEPF1_M)

#define HOSTPAGESIZEPF0_S    0
#define HOSTPAGESIZEPF0_M    0xfU
#define HOSTPAGESIZEPF0_V(x) ((x) << HOSTPAGESIZEPF0_S)
#define HOSTPAGESIZEPF0_G(x) (((x) >> HOSTPAGESIZEPF0_S) & HOSTPAGESIZEPF0_M)

#define SGE_EGRESS_QUEUES_PER_PAGE_PF_A 0x1010
#define SGE_EGRESS_QUEUES_PER_PAGE_VF_A 0x1014

#define QUEUESPERPAGEPF1_S    4

#define QUEUESPERPAGEPF0_S    0
#define QUEUESPERPAGEPF0_M    0xfU
#define QUEUESPERPAGEPF0_V(x) ((x) << QUEUESPERPAGEPF0_S)
#define QUEUESPERPAGEPF0_G(x) (((x) >> QUEUESPERPAGEPF0_S) & QUEUESPERPAGEPF0_M)

#define SGE_INT_CAUSE1_A	0x1024
#define SGE_INT_CAUSE2_A	0x1030
#define SGE_INT_CAUSE3_A	0x103c

#define ERR_FLM_DBP_S    31
#define ERR_FLM_DBP_V(x) ((x) << ERR_FLM_DBP_S)
#define ERR_FLM_DBP_F    ERR_FLM_DBP_V(1U)

#define ERR_FLM_IDMA1_S    30
#define ERR_FLM_IDMA1_V(x) ((x) << ERR_FLM_IDMA1_S)
#define ERR_FLM_IDMA1_F    ERR_FLM_IDMA1_V(1U)

#define ERR_FLM_IDMA0_S    29
#define ERR_FLM_IDMA0_V(x) ((x) << ERR_FLM_IDMA0_S)
#define ERR_FLM_IDMA0_F    ERR_FLM_IDMA0_V(1U)

#define ERR_FLM_HINT_S    28
#define ERR_FLM_HINT_V(x) ((x) << ERR_FLM_HINT_S)
#define ERR_FLM_HINT_F    ERR_FLM_HINT_V(1U)

#define ERR_PCIE_ERROR3_S    27
#define ERR_PCIE_ERROR3_V(x) ((x) << ERR_PCIE_ERROR3_S)
#define ERR_PCIE_ERROR3_F    ERR_PCIE_ERROR3_V(1U)

#define ERR_PCIE_ERROR2_S    26
#define ERR_PCIE_ERROR2_V(x) ((x) << ERR_PCIE_ERROR2_S)
#define ERR_PCIE_ERROR2_F    ERR_PCIE_ERROR2_V(1U)

#define ERR_PCIE_ERROR1_S    25
#define ERR_PCIE_ERROR1_V(x) ((x) << ERR_PCIE_ERROR1_S)
#define ERR_PCIE_ERROR1_F    ERR_PCIE_ERROR1_V(1U)

#define ERR_PCIE_ERROR0_S    24
#define ERR_PCIE_ERROR0_V(x) ((x) << ERR_PCIE_ERROR0_S)
#define ERR_PCIE_ERROR0_F    ERR_PCIE_ERROR0_V(1U)

#define ERR_CPL_EXCEED_IQE_SIZE_S    22
#define ERR_CPL_EXCEED_IQE_SIZE_V(x) ((x) << ERR_CPL_EXCEED_IQE_SIZE_S)
#define ERR_CPL_EXCEED_IQE_SIZE_F    ERR_CPL_EXCEED_IQE_SIZE_V(1U)

#define ERR_INVALID_CIDX_INC_S    21
#define ERR_INVALID_CIDX_INC_V(x) ((x) << ERR_INVALID_CIDX_INC_S)
#define ERR_INVALID_CIDX_INC_F    ERR_INVALID_CIDX_INC_V(1U)

#define ERR_CPL_OPCODE_0_S    19
#define ERR_CPL_OPCODE_0_V(x) ((x) << ERR_CPL_OPCODE_0_S)
#define ERR_CPL_OPCODE_0_F    ERR_CPL_OPCODE_0_V(1U)

#define ERR_DROPPED_DB_S    18
#define ERR_DROPPED_DB_V(x) ((x) << ERR_DROPPED_DB_S)
#define ERR_DROPPED_DB_F    ERR_DROPPED_DB_V(1U)

#define ERR_DATA_CPL_ON_HIGH_QID1_S    17
#define ERR_DATA_CPL_ON_HIGH_QID1_V(x) ((x) << ERR_DATA_CPL_ON_HIGH_QID1_S)
#define ERR_DATA_CPL_ON_HIGH_QID1_F    ERR_DATA_CPL_ON_HIGH_QID1_V(1U)

#define ERR_DATA_CPL_ON_HIGH_QID0_S    16
#define ERR_DATA_CPL_ON_HIGH_QID0_V(x) ((x) << ERR_DATA_CPL_ON_HIGH_QID0_S)
#define ERR_DATA_CPL_ON_HIGH_QID0_F    ERR_DATA_CPL_ON_HIGH_QID0_V(1U)

#define ERR_BAD_DB_PIDX3_S    15
#define ERR_BAD_DB_PIDX3_V(x) ((x) << ERR_BAD_DB_PIDX3_S)
#define ERR_BAD_DB_PIDX3_F    ERR_BAD_DB_PIDX3_V(1U)

#define ERR_BAD_DB_PIDX2_S    14
#define ERR_BAD_DB_PIDX2_V(x) ((x) << ERR_BAD_DB_PIDX2_S)
#define ERR_BAD_DB_PIDX2_F    ERR_BAD_DB_PIDX2_V(1U)

#define ERR_BAD_DB_PIDX1_S    13
#define ERR_BAD_DB_PIDX1_V(x) ((x) << ERR_BAD_DB_PIDX1_S)
#define ERR_BAD_DB_PIDX1_F    ERR_BAD_DB_PIDX1_V(1U)

#define ERR_BAD_DB_PIDX0_S    12
#define ERR_BAD_DB_PIDX0_V(x) ((x) << ERR_BAD_DB_PIDX0_S)
#define ERR_BAD_DB_PIDX0_F    ERR_BAD_DB_PIDX0_V(1U)

#define ERR_ING_CTXT_PRIO_S    10
#define ERR_ING_CTXT_PRIO_V(x) ((x) << ERR_ING_CTXT_PRIO_S)
#define ERR_ING_CTXT_PRIO_F    ERR_ING_CTXT_PRIO_V(1U)

#define ERR_EGR_CTXT_PRIO_S    9
#define ERR_EGR_CTXT_PRIO_V(x) ((x) << ERR_EGR_CTXT_PRIO_S)
#define ERR_EGR_CTXT_PRIO_F    ERR_EGR_CTXT_PRIO_V(1U)

#define DBFIFO_HP_INT_S    8
#define DBFIFO_HP_INT_V(x) ((x) << DBFIFO_HP_INT_S)
#define DBFIFO_HP_INT_F    DBFIFO_HP_INT_V(1U)

#define DBFIFO_LP_INT_S    7
#define DBFIFO_LP_INT_V(x) ((x) << DBFIFO_LP_INT_S)
#define DBFIFO_LP_INT_F    DBFIFO_LP_INT_V(1U)

#define INGRESS_SIZE_ERR_S    5
#define INGRESS_SIZE_ERR_V(x) ((x) << INGRESS_SIZE_ERR_S)
#define INGRESS_SIZE_ERR_F    INGRESS_SIZE_ERR_V(1U)

#define EGRESS_SIZE_ERR_S    4
#define EGRESS_SIZE_ERR_V(x) ((x) << EGRESS_SIZE_ERR_S)
#define EGRESS_SIZE_ERR_F    EGRESS_SIZE_ERR_V(1U)

#define SGE_INT_ENABLE3_A 0x1040
#define SGE_FL_BUFFER_SIZE0_A 0x1044
#define SGE_FL_BUFFER_SIZE1_A 0x1048
#define SGE_FL_BUFFER_SIZE2_A 0x104c
#define SGE_FL_BUFFER_SIZE3_A 0x1050
#define SGE_FL_BUFFER_SIZE4_A 0x1054
#define SGE_FL_BUFFER_SIZE5_A 0x1058
#define SGE_FL_BUFFER_SIZE6_A 0x105c
#define SGE_FL_BUFFER_SIZE7_A 0x1060
#define SGE_FL_BUFFER_SIZE8_A 0x1064

#define SGE_IMSG_CTXT_BADDR_A 0x1088
#define SGE_FLM_CACHE_BADDR_A 0x108c
#define SGE_FLM_CFG_A 0x1090

#define NOHDR_S    18
#define NOHDR_V(x) ((x) << NOHDR_S)
#define NOHDR_F    NOHDR_V(1U)

#define HDRSTARTFLQ_S    11
#define HDRSTARTFLQ_M    0x7U
#define HDRSTARTFLQ_G(x) (((x) >> HDRSTARTFLQ_S) & HDRSTARTFLQ_M)

#define SGE_INGRESS_RX_THRESHOLD_A 0x10a0

#define THRESHOLD_0_S    24
#define THRESHOLD_0_M    0x3fU
#define THRESHOLD_0_V(x) ((x) << THRESHOLD_0_S)
#define THRESHOLD_0_G(x) (((x) >> THRESHOLD_0_S) & THRESHOLD_0_M)

#define THRESHOLD_1_S    16
#define THRESHOLD_1_M    0x3fU
#define THRESHOLD_1_V(x) ((x) << THRESHOLD_1_S)
#define THRESHOLD_1_G(x) (((x) >> THRESHOLD_1_S) & THRESHOLD_1_M)

#define THRESHOLD_2_S    8
#define THRESHOLD_2_M    0x3fU
#define THRESHOLD_2_V(x) ((x) << THRESHOLD_2_S)
#define THRESHOLD_2_G(x) (((x) >> THRESHOLD_2_S) & THRESHOLD_2_M)

#define THRESHOLD_3_S    0
#define THRESHOLD_3_M    0x3fU
#define THRESHOLD_3_V(x) ((x) << THRESHOLD_3_S)
#define THRESHOLD_3_G(x) (((x) >> THRESHOLD_3_S) & THRESHOLD_3_M)

#define SGE_CONM_CTRL_A 0x1094

#define EGRTHRESHOLD_S    8
#define EGRTHRESHOLD_M    0x3fU
#define EGRTHRESHOLD_V(x) ((x) << EGRTHRESHOLD_S)
#define EGRTHRESHOLD_G(x) (((x) >> EGRTHRESHOLD_S) & EGRTHRESHOLD_M)

#define EGRTHRESHOLDPACKING_S    14
#define EGRTHRESHOLDPACKING_M    0x3fU
#define EGRTHRESHOLDPACKING_V(x) ((x) << EGRTHRESHOLDPACKING_S)
#define EGRTHRESHOLDPACKING_G(x) \
	(((x) >> EGRTHRESHOLDPACKING_S) & EGRTHRESHOLDPACKING_M)

#define T6_EGRTHRESHOLDPACKING_S    16
#define T6_EGRTHRESHOLDPACKING_M    0xffU
#define T6_EGRTHRESHOLDPACKING_G(x) \
	(((x) >> T6_EGRTHRESHOLDPACKING_S) & T6_EGRTHRESHOLDPACKING_M)

#define SGE_TIMESTAMP_LO_A 0x1098
#define SGE_TIMESTAMP_HI_A 0x109c

#define TSOP_S    28
#define TSOP_M    0x3U
#define TSOP_V(x) ((x) << TSOP_S)
#define TSOP_G(x) (((x) >> TSOP_S) & TSOP_M)

#define TSVAL_S    0
#define TSVAL_M    0xfffffffU
#define TSVAL_V(x) ((x) << TSVAL_S)
#define TSVAL_G(x) (((x) >> TSVAL_S) & TSVAL_M)

#define SGE_DBFIFO_STATUS_A 0x10a4
#define SGE_DBVFIFO_SIZE_A 0x113c

#define HP_INT_THRESH_S    28
#define HP_INT_THRESH_M    0xfU
#define HP_INT_THRESH_V(x) ((x) << HP_INT_THRESH_S)

#define LP_INT_THRESH_S    12
#define LP_INT_THRESH_M    0xfU
#define LP_INT_THRESH_V(x) ((x) << LP_INT_THRESH_S)

#define SGE_DOORBELL_CONTROL_A 0x10a8

#define NOCOALESCE_S    26
#define NOCOALESCE_V(x) ((x) << NOCOALESCE_S)
#define NOCOALESCE_F    NOCOALESCE_V(1U)

#define ENABLE_DROP_S    13
#define ENABLE_DROP_V(x) ((x) << ENABLE_DROP_S)
#define ENABLE_DROP_F    ENABLE_DROP_V(1U)

#define SGE_TIMER_VALUE_0_AND_1_A 0x10b8

#define TIMERVALUE0_S    16
#define TIMERVALUE0_M    0xffffU
#define TIMERVALUE0_V(x) ((x) << TIMERVALUE0_S)
#define TIMERVALUE0_G(x) (((x) >> TIMERVALUE0_S) & TIMERVALUE0_M)

#define TIMERVALUE1_S    0
#define TIMERVALUE1_M    0xffffU
#define TIMERVALUE1_V(x) ((x) << TIMERVALUE1_S)
#define TIMERVALUE1_G(x) (((x) >> TIMERVALUE1_S) & TIMERVALUE1_M)

#define SGE_TIMER_VALUE_2_AND_3_A 0x10bc

#define TIMERVALUE2_S    16
#define TIMERVALUE2_M    0xffffU
#define TIMERVALUE2_V(x) ((x) << TIMERVALUE2_S)
#define TIMERVALUE2_G(x) (((x) >> TIMERVALUE2_S) & TIMERVALUE2_M)

#define TIMERVALUE3_S    0
#define TIMERVALUE3_M    0xffffU
#define TIMERVALUE3_V(x) ((x) << TIMERVALUE3_S)
#define TIMERVALUE3_G(x) (((x) >> TIMERVALUE3_S) & TIMERVALUE3_M)

#define SGE_TIMER_VALUE_4_AND_5_A 0x10c0

#define TIMERVALUE4_S    16
#define TIMERVALUE4_M    0xffffU
#define TIMERVALUE4_V(x) ((x) << TIMERVALUE4_S)
#define TIMERVALUE4_G(x) (((x) >> TIMERVALUE4_S) & TIMERVALUE4_M)

#define TIMERVALUE5_S    0
#define TIMERVALUE5_M    0xffffU
#define TIMERVALUE5_V(x) ((x) << TIMERVALUE5_S)
#define TIMERVALUE5_G(x) (((x) >> TIMERVALUE5_S) & TIMERVALUE5_M)

#define SGE_DEBUG_INDEX_A 0x10cc
#define SGE_DEBUG_DATA_HIGH_A 0x10d0
#define SGE_DEBUG_DATA_LOW_A 0x10d4

#define SGE_DEBUG_DATA_LOW_INDEX_2_A	0x12c8
#define SGE_DEBUG_DATA_LOW_INDEX_3_A	0x12cc
#define SGE_DEBUG_DATA_HIGH_INDEX_10_A	0x12a8

#define SGE_INGRESS_QUEUES_PER_PAGE_PF_A 0x10f4
#define SGE_INGRESS_QUEUES_PER_PAGE_VF_A 0x10f8

#define SGE_ERROR_STATS_A 0x1100

#define UNCAPTURED_ERROR_S    18
#define UNCAPTURED_ERROR_V(x) ((x) << UNCAPTURED_ERROR_S)
#define UNCAPTURED_ERROR_F    UNCAPTURED_ERROR_V(1U)

#define ERROR_QID_VALID_S    17
#define ERROR_QID_VALID_V(x) ((x) << ERROR_QID_VALID_S)
#define ERROR_QID_VALID_F    ERROR_QID_VALID_V(1U)

#define ERROR_QID_S    0
#define ERROR_QID_M    0x1ffffU
#define ERROR_QID_G(x) (((x) >> ERROR_QID_S) & ERROR_QID_M)

#define HP_INT_THRESH_S    28
#define HP_INT_THRESH_M    0xfU
#define HP_INT_THRESH_V(x) ((x) << HP_INT_THRESH_S)

#define HP_COUNT_S    16
#define HP_COUNT_M    0x7ffU
#define HP_COUNT_G(x) (((x) >> HP_COUNT_S) & HP_COUNT_M)

#define LP_INT_THRESH_S    12
#define LP_INT_THRESH_M    0xfU
#define LP_INT_THRESH_V(x) ((x) << LP_INT_THRESH_S)

#define LP_COUNT_S    0
#define LP_COUNT_M    0x7ffU
#define LP_COUNT_G(x) (((x) >> LP_COUNT_S) & LP_COUNT_M)

#define LP_INT_THRESH_T5_S    18
#define LP_INT_THRESH_T5_M    0xfffU
#define LP_INT_THRESH_T5_V(x) ((x) << LP_INT_THRESH_T5_S)

#define LP_COUNT_T5_S    0
#define LP_COUNT_T5_M    0x3ffffU
#define LP_COUNT_T5_G(x) (((x) >> LP_COUNT_T5_S) & LP_COUNT_T5_M)

#define SGE_DOORBELL_CONTROL_A 0x10a8

#define SGE_STAT_TOTAL_A	0x10e4
#define SGE_STAT_MATCH_A	0x10e8
#define SGE_STAT_CFG_A		0x10ec

#define STATMODE_S    2
#define STATMODE_V(x) ((x) << STATMODE_S)

#define STATSOURCE_T5_S    9
#define STATSOURCE_T5_M    0xfU
#define STATSOURCE_T5_V(x) ((x) << STATSOURCE_T5_S)
#define STATSOURCE_T5_G(x) (((x) >> STATSOURCE_T5_S) & STATSOURCE_T5_M)

#define T6_STATMODE_S    0
#define T6_STATMODE_V(x) ((x) << T6_STATMODE_S)

#define SGE_DBFIFO_STATUS2_A 0x1118

#define HP_INT_THRESH_T5_S    10
#define HP_INT_THRESH_T5_M    0xfU
#define HP_INT_THRESH_T5_V(x) ((x) << HP_INT_THRESH_T5_S)

#define HP_COUNT_T5_S    0
#define HP_COUNT_T5_M    0x3ffU
#define HP_COUNT_T5_G(x) (((x) >> HP_COUNT_T5_S) & HP_COUNT_T5_M)

#define ENABLE_DROP_S    13
#define ENABLE_DROP_V(x) ((x) << ENABLE_DROP_S)
#define ENABLE_DROP_F    ENABLE_DROP_V(1U)

#define DROPPED_DB_S    0
#define DROPPED_DB_V(x) ((x) << DROPPED_DB_S)
#define DROPPED_DB_F    DROPPED_DB_V(1U)

#define SGE_CTXT_CMD_A 0x11fc
#define SGE_DBQ_CTXT_BADDR_A 0x1084

/* registers for module PCIE */
#define PCIE_PF_CFG_A	0x40

#define AIVEC_S    4
#define AIVEC_M    0x3ffU
#define AIVEC_V(x) ((x) << AIVEC_S)

#define PCIE_PF_CLI_A	0x44
#define PCIE_INT_CAUSE_A	0x3004

#define UNXSPLCPLERR_S    29
#define UNXSPLCPLERR_V(x) ((x) << UNXSPLCPLERR_S)
#define UNXSPLCPLERR_F    UNXSPLCPLERR_V(1U)

#define PCIEPINT_S    28
#define PCIEPINT_V(x) ((x) << PCIEPINT_S)
#define PCIEPINT_F    PCIEPINT_V(1U)

#define PCIESINT_S    27
#define PCIESINT_V(x) ((x) << PCIESINT_S)
#define PCIESINT_F    PCIESINT_V(1U)

#define RPLPERR_S    26
#define RPLPERR_V(x) ((x) << RPLPERR_S)
#define RPLPERR_F    RPLPERR_V(1U)

#define RXWRPERR_S    25
#define RXWRPERR_V(x) ((x) << RXWRPERR_S)
#define RXWRPERR_F    RXWRPERR_V(1U)

#define RXCPLPERR_S    24
#define RXCPLPERR_V(x) ((x) << RXCPLPERR_S)
#define RXCPLPERR_F    RXCPLPERR_V(1U)

#define PIOTAGPERR_S    23
#define PIOTAGPERR_V(x) ((x) << PIOTAGPERR_S)
#define PIOTAGPERR_F    PIOTAGPERR_V(1U)

#define MATAGPERR_S    22
#define MATAGPERR_V(x) ((x) << MATAGPERR_S)
#define MATAGPERR_F    MATAGPERR_V(1U)

#define INTXCLRPERR_S    21
#define INTXCLRPERR_V(x) ((x) << INTXCLRPERR_S)
#define INTXCLRPERR_F    INTXCLRPERR_V(1U)

#define FIDPERR_S    20
#define FIDPERR_V(x) ((x) << FIDPERR_S)
#define FIDPERR_F    FIDPERR_V(1U)

#define CFGSNPPERR_S    19
#define CFGSNPPERR_V(x) ((x) << CFGSNPPERR_S)
#define CFGSNPPERR_F    CFGSNPPERR_V(1U)

#define HRSPPERR_S    18
#define HRSPPERR_V(x) ((x) << HRSPPERR_S)
#define HRSPPERR_F    HRSPPERR_V(1U)

#define HREQPERR_S    17
#define HREQPERR_V(x) ((x) << HREQPERR_S)
#define HREQPERR_F    HREQPERR_V(1U)

#define HCNTPERR_S    16
#define HCNTPERR_V(x) ((x) << HCNTPERR_S)
#define HCNTPERR_F    HCNTPERR_V(1U)

#define DRSPPERR_S    15
#define DRSPPERR_V(x) ((x) << DRSPPERR_S)
#define DRSPPERR_F    DRSPPERR_V(1U)

#define DREQPERR_S    14
#define DREQPERR_V(x) ((x) << DREQPERR_S)
#define DREQPERR_F    DREQPERR_V(1U)

#define DCNTPERR_S    13
#define DCNTPERR_V(x) ((x) << DCNTPERR_S)
#define DCNTPERR_F    DCNTPERR_V(1U)

#define CRSPPERR_S    12
#define CRSPPERR_V(x) ((x) << CRSPPERR_S)
#define CRSPPERR_F    CRSPPERR_V(1U)

#define CREQPERR_S    11
#define CREQPERR_V(x) ((x) << CREQPERR_S)
#define CREQPERR_F    CREQPERR_V(1U)

#define CCNTPERR_S    10
#define CCNTPERR_V(x) ((x) << CCNTPERR_S)
#define CCNTPERR_F    CCNTPERR_V(1U)

#define TARTAGPERR_S    9
#define TARTAGPERR_V(x) ((x) << TARTAGPERR_S)
#define TARTAGPERR_F    TARTAGPERR_V(1U)

#define PIOREQPERR_S    8
#define PIOREQPERR_V(x) ((x) << PIOREQPERR_S)
#define PIOREQPERR_F    PIOREQPERR_V(1U)

#define PIOCPLPERR_S    7
#define PIOCPLPERR_V(x) ((x) << PIOCPLPERR_S)
#define PIOCPLPERR_F    PIOCPLPERR_V(1U)

#define MSIXDIPERR_S    6
#define MSIXDIPERR_V(x) ((x) << MSIXDIPERR_S)
#define MSIXDIPERR_F    MSIXDIPERR_V(1U)

#define MSIXDATAPERR_S    5
#define MSIXDATAPERR_V(x) ((x) << MSIXDATAPERR_S)
#define MSIXDATAPERR_F    MSIXDATAPERR_V(1U)

#define MSIXADDRHPERR_S    4
#define MSIXADDRHPERR_V(x) ((x) << MSIXADDRHPERR_S)
#define MSIXADDRHPERR_F    MSIXADDRHPERR_V(1U)

#define MSIXADDRLPERR_S    3
#define MSIXADDRLPERR_V(x) ((x) << MSIXADDRLPERR_S)
#define MSIXADDRLPERR_F    MSIXADDRLPERR_V(1U)

#define MSIDATAPERR_S    2
#define MSIDATAPERR_V(x) ((x) << MSIDATAPERR_S)
#define MSIDATAPERR_F    MSIDATAPERR_V(1U)

#define MSIADDRHPERR_S    1
#define MSIADDRHPERR_V(x) ((x) << MSIADDRHPERR_S)
#define MSIADDRHPERR_F    MSIADDRHPERR_V(1U)

#define MSIADDRLPERR_S    0
#define MSIADDRLPERR_V(x) ((x) << MSIADDRLPERR_S)
#define MSIADDRLPERR_F    MSIADDRLPERR_V(1U)

#define READRSPERR_S    29
#define READRSPERR_V(x) ((x) << READRSPERR_S)
#define READRSPERR_F    READRSPERR_V(1U)

#define TRGT1GRPPERR_S    28
#define TRGT1GRPPERR_V(x) ((x) << TRGT1GRPPERR_S)
#define TRGT1GRPPERR_F    TRGT1GRPPERR_V(1U)

#define IPSOTPERR_S    27
#define IPSOTPERR_V(x) ((x) << IPSOTPERR_S)
#define IPSOTPERR_F    IPSOTPERR_V(1U)

#define IPRETRYPERR_S    26
#define IPRETRYPERR_V(x) ((x) << IPRETRYPERR_S)
#define IPRETRYPERR_F    IPRETRYPERR_V(1U)

#define IPRXDATAGRPPERR_S    25
#define IPRXDATAGRPPERR_V(x) ((x) << IPRXDATAGRPPERR_S)
#define IPRXDATAGRPPERR_F    IPRXDATAGRPPERR_V(1U)

#define IPRXHDRGRPPERR_S    24
#define IPRXHDRGRPPERR_V(x) ((x) << IPRXHDRGRPPERR_S)
#define IPRXHDRGRPPERR_F    IPRXHDRGRPPERR_V(1U)

#define MAGRPPERR_S    22
#define MAGRPPERR_V(x) ((x) << MAGRPPERR_S)
#define MAGRPPERR_F    MAGRPPERR_V(1U)

#define VFIDPERR_S    21
#define VFIDPERR_V(x) ((x) << VFIDPERR_S)
#define VFIDPERR_F    VFIDPERR_V(1U)

#define HREQWRPERR_S    16
#define HREQWRPERR_V(x) ((x) << HREQWRPERR_S)
#define HREQWRPERR_F    HREQWRPERR_V(1U)

#define DREQWRPERR_S    13
#define DREQWRPERR_V(x) ((x) << DREQWRPERR_S)
#define DREQWRPERR_F    DREQWRPERR_V(1U)

#define CREQRDPERR_S    11
#define CREQRDPERR_V(x) ((x) << CREQRDPERR_S)
#define CREQRDPERR_F    CREQRDPERR_V(1U)

#define MSTTAGQPERR_S    10
#define MSTTAGQPERR_V(x) ((x) << MSTTAGQPERR_S)
#define MSTTAGQPERR_F    MSTTAGQPERR_V(1U)

#define PIOREQGRPPERR_S    8
#define PIOREQGRPPERR_V(x) ((x) << PIOREQGRPPERR_S)
#define PIOREQGRPPERR_F    PIOREQGRPPERR_V(1U)

#define PIOCPLGRPPERR_S    7
#define PIOCPLGRPPERR_V(x) ((x) << PIOCPLGRPPERR_S)
#define PIOCPLGRPPERR_F    PIOCPLGRPPERR_V(1U)

#define MSIXSTIPERR_S    2
#define MSIXSTIPERR_V(x) ((x) << MSIXSTIPERR_S)
#define MSIXSTIPERR_F    MSIXSTIPERR_V(1U)

#define MSTTIMEOUTPERR_S    1
#define MSTTIMEOUTPERR_V(x) ((x) << MSTTIMEOUTPERR_S)
#define MSTTIMEOUTPERR_F    MSTTIMEOUTPERR_V(1U)

#define MSTGRPPERR_S    0
#define MSTGRPPERR_V(x) ((x) << MSTGRPPERR_S)
#define MSTGRPPERR_F    MSTGRPPERR_V(1U)

#define PCIE_NONFAT_ERR_A	0x3010
#define PCIE_CFG_SPACE_REQ_A	0x3060
#define PCIE_CFG_SPACE_DATA_A	0x3064
#define PCIE_MEM_ACCESS_BASE_WIN_A 0x3068

#define PCIEOFST_S    10
#define PCIEOFST_M    0x3fffffU
#define PCIEOFST_G(x) (((x) >> PCIEOFST_S) & PCIEOFST_M)

#define BIR_S    8
#define BIR_M    0x3U
#define BIR_V(x) ((x) << BIR_S)
#define BIR_G(x) (((x) >> BIR_S) & BIR_M)

#define WINDOW_S    0
#define WINDOW_M    0xffU
#define WINDOW_V(x) ((x) << WINDOW_S)
#define WINDOW_G(x) (((x) >> WINDOW_S) & WINDOW_M)

#define PCIE_MEM_ACCESS_OFFSET_A 0x306c

#define ENABLE_S    30
#define ENABLE_V(x) ((x) << ENABLE_S)
#define ENABLE_F    ENABLE_V(1U)

#define LOCALCFG_S    28
#define LOCALCFG_V(x) ((x) << LOCALCFG_S)
#define LOCALCFG_F    LOCALCFG_V(1U)

#define FUNCTION_S    12
#define FUNCTION_V(x) ((x) << FUNCTION_S)

#define REGISTER_S    0
#define REGISTER_V(x) ((x) << REGISTER_S)

#define T6_ENABLE_S    31
#define T6_ENABLE_V(x) ((x) << T6_ENABLE_S)
#define T6_ENABLE_F    T6_ENABLE_V(1U)

#define PFNUM_S    0
#define PFNUM_V(x) ((x) << PFNUM_S)

#define PCIE_FW_A 0x30b8
#define PCIE_FW_PF_A 0x30bc

#define PCIE_CORE_UTL_SYSTEM_BUS_AGENT_STATUS_A 0x5908

#define RNPP_S    31
#define RNPP_V(x) ((x) << RNPP_S)
#define RNPP_F    RNPP_V(1U)

#define RPCP_S    29
#define RPCP_V(x) ((x) << RPCP_S)
#define RPCP_F    RPCP_V(1U)

#define RCIP_S    27
#define RCIP_V(x) ((x) << RCIP_S)
#define RCIP_F    RCIP_V(1U)

#define RCCP_S    26
#define RCCP_V(x) ((x) << RCCP_S)
#define RCCP_F    RCCP_V(1U)

#define RFTP_S    23
#define RFTP_V(x) ((x) << RFTP_S)
#define RFTP_F    RFTP_V(1U)

#define PTRP_S    20
#define PTRP_V(x) ((x) << PTRP_S)
#define PTRP_F    PTRP_V(1U)

#define PCIE_CORE_UTL_PCI_EXPRESS_PORT_STATUS_A 0x59a4

#define TPCP_S    30
#define TPCP_V(x) ((x) << TPCP_S)
#define TPCP_F    TPCP_V(1U)

#define TNPP_S    29
#define TNPP_V(x) ((x) << TNPP_S)
#define TNPP_F    TNPP_V(1U)

#define TFTP_S    28
#define TFTP_V(x) ((x) << TFTP_S)
#define TFTP_F    TFTP_V(1U)

#define TCAP_S    27
#define TCAP_V(x) ((x) << TCAP_S)
#define TCAP_F    TCAP_V(1U)

#define TCIP_S    26
#define TCIP_V(x) ((x) << TCIP_S)
#define TCIP_F    TCIP_V(1U)

#define RCAP_S    25
#define RCAP_V(x) ((x) << RCAP_S)
#define RCAP_F    RCAP_V(1U)

#define PLUP_S    23
#define PLUP_V(x) ((x) << PLUP_S)
#define PLUP_F    PLUP_V(1U)

#define PLDN_S    22
#define PLDN_V(x) ((x) << PLDN_S)
#define PLDN_F    PLDN_V(1U)

#define OTDD_S    21
#define OTDD_V(x) ((x) << OTDD_S)
#define OTDD_F    OTDD_V(1U)

#define GTRP_S    20
#define GTRP_V(x) ((x) << GTRP_S)
#define GTRP_F    GTRP_V(1U)

#define RDPE_S    18
#define RDPE_V(x) ((x) << RDPE_S)
#define RDPE_F    RDPE_V(1U)

#define TDCE_S    17
#define TDCE_V(x) ((x) << TDCE_S)
#define TDCE_F    TDCE_V(1U)

#define TDUE_S    16
#define TDUE_V(x) ((x) << TDUE_S)
#define TDUE_F    TDUE_V(1U)

/* registers for module MC */
#define MC_INT_CAUSE_A		0x7518
#define MC_P_INT_CAUSE_A	0x41318

#define ECC_UE_INT_CAUSE_S    2
#define ECC_UE_INT_CAUSE_V(x) ((x) << ECC_UE_INT_CAUSE_S)
#define ECC_UE_INT_CAUSE_F    ECC_UE_INT_CAUSE_V(1U)

#define ECC_CE_INT_CAUSE_S    1
#define ECC_CE_INT_CAUSE_V(x) ((x) << ECC_CE_INT_CAUSE_S)
#define ECC_CE_INT_CAUSE_F    ECC_CE_INT_CAUSE_V(1U)

#define PERR_INT_CAUSE_S    0
#define PERR_INT_CAUSE_V(x) ((x) << PERR_INT_CAUSE_S)
#define PERR_INT_CAUSE_F    PERR_INT_CAUSE_V(1U)

#define DBG_GPIO_EN_A		0x6010
#define XGMAC_PORT_CFG_A	0x1000
#define MAC_PORT_CFG_A		0x800

#define SIGNAL_DET_S    14
#define SIGNAL_DET_V(x) ((x) << SIGNAL_DET_S)
#define SIGNAL_DET_F    SIGNAL_DET_V(1U)

#define MC_ECC_STATUS_A		0x751c
#define MC_P_ECC_STATUS_A	0x4131c

#define ECC_CECNT_S    16
#define ECC_CECNT_M    0xffffU
#define ECC_CECNT_V(x) ((x) << ECC_CECNT_S)
#define ECC_CECNT_G(x) (((x) >> ECC_CECNT_S) & ECC_CECNT_M)

#define ECC_UECNT_S    0
#define ECC_UECNT_M    0xffffU
#define ECC_UECNT_V(x) ((x) << ECC_UECNT_S)
#define ECC_UECNT_G(x) (((x) >> ECC_UECNT_S) & ECC_UECNT_M)

#define MC_BIST_CMD_A 0x7600

#define START_BIST_S    31
#define START_BIST_V(x) ((x) << START_BIST_S)
#define START_BIST_F    START_BIST_V(1U)

#define BIST_CMD_GAP_S    8
#define BIST_CMD_GAP_V(x) ((x) << BIST_CMD_GAP_S)

#define BIST_OPCODE_S    0
#define BIST_OPCODE_V(x) ((x) << BIST_OPCODE_S)

#define MC_BIST_CMD_ADDR_A 0x7604
#define MC_BIST_CMD_LEN_A 0x7608
#define MC_BIST_DATA_PATTERN_A 0x760c

#define MC_BIST_STATUS_RDATA_A 0x7688

/* registers for module MA */
#define MA_EDRAM0_BAR_A 0x77c0

#define EDRAM0_BASE_S    16
#define EDRAM0_BASE_M    0xfffU
#define EDRAM0_BASE_G(x) (((x) >> EDRAM0_BASE_S) & EDRAM0_BASE_M)

#define EDRAM0_SIZE_S    0
#define EDRAM0_SIZE_M    0xfffU
#define EDRAM0_SIZE_V(x) ((x) << EDRAM0_SIZE_S)
#define EDRAM0_SIZE_G(x) (((x) >> EDRAM0_SIZE_S) & EDRAM0_SIZE_M)

#define MA_EDRAM1_BAR_A 0x77c4

#define EDRAM1_BASE_S    16
#define EDRAM1_BASE_M    0xfffU
#define EDRAM1_BASE_G(x) (((x) >> EDRAM1_BASE_S) & EDRAM1_BASE_M)

#define EDRAM1_SIZE_S    0
#define EDRAM1_SIZE_M    0xfffU
#define EDRAM1_SIZE_V(x) ((x) << EDRAM1_SIZE_S)
#define EDRAM1_SIZE_G(x) (((x) >> EDRAM1_SIZE_S) & EDRAM1_SIZE_M)

#define MA_EXT_MEMORY_BAR_A 0x77c8

#define EXT_MEM_BASE_S    16
#define EXT_MEM_BASE_M    0xfffU
#define EXT_MEM_BASE_V(x) ((x) << EXT_MEM_BASE_S)
#define EXT_MEM_BASE_G(x) (((x) >> EXT_MEM_BASE_S) & EXT_MEM_BASE_M)

#define EXT_MEM_SIZE_S    0
#define EXT_MEM_SIZE_M    0xfffU
#define EXT_MEM_SIZE_V(x) ((x) << EXT_MEM_SIZE_S)
#define EXT_MEM_SIZE_G(x) (((x) >> EXT_MEM_SIZE_S) & EXT_MEM_SIZE_M)

#define MA_EXT_MEMORY1_BAR_A 0x7808

#define EXT_MEM1_BASE_S    16
#define EXT_MEM1_BASE_M    0xfffU
#define EXT_MEM1_BASE_G(x) (((x) >> EXT_MEM1_BASE_S) & EXT_MEM1_BASE_M)

#define EXT_MEM1_SIZE_S    0
#define EXT_MEM1_SIZE_M    0xfffU
#define EXT_MEM1_SIZE_V(x) ((x) << EXT_MEM1_SIZE_S)
#define EXT_MEM1_SIZE_G(x) (((x) >> EXT_MEM1_SIZE_S) & EXT_MEM1_SIZE_M)

#define MA_EXT_MEMORY0_BAR_A 0x77c8

#define EXT_MEM0_BASE_S    16
#define EXT_MEM0_BASE_M    0xfffU
#define EXT_MEM0_BASE_G(x) (((x) >> EXT_MEM0_BASE_S) & EXT_MEM0_BASE_M)

#define EXT_MEM0_SIZE_S    0
#define EXT_MEM0_SIZE_M    0xfffU
#define EXT_MEM0_SIZE_V(x) ((x) << EXT_MEM0_SIZE_S)
#define EXT_MEM0_SIZE_G(x) (((x) >> EXT_MEM0_SIZE_S) & EXT_MEM0_SIZE_M)

#define MA_TARGET_MEM_ENABLE_A 0x77d8

#define EXT_MEM_ENABLE_S    2
#define EXT_MEM_ENABLE_V(x) ((x) << EXT_MEM_ENABLE_S)
#define EXT_MEM_ENABLE_F    EXT_MEM_ENABLE_V(1U)

#define EDRAM1_ENABLE_S    1
#define EDRAM1_ENABLE_V(x) ((x) << EDRAM1_ENABLE_S)
#define EDRAM1_ENABLE_F    EDRAM1_ENABLE_V(1U)

#define EDRAM0_ENABLE_S    0
#define EDRAM0_ENABLE_V(x) ((x) << EDRAM0_ENABLE_S)
#define EDRAM0_ENABLE_F    EDRAM0_ENABLE_V(1U)

#define EXT_MEM1_ENABLE_S    4
#define EXT_MEM1_ENABLE_V(x) ((x) << EXT_MEM1_ENABLE_S)
#define EXT_MEM1_ENABLE_F    EXT_MEM1_ENABLE_V(1U)

#define EXT_MEM0_ENABLE_S    2
#define EXT_MEM0_ENABLE_V(x) ((x) << EXT_MEM0_ENABLE_S)
#define EXT_MEM0_ENABLE_F    EXT_MEM0_ENABLE_V(1U)

#define MA_INT_CAUSE_A	0x77e0

#define MEM_PERR_INT_CAUSE_S    1
#define MEM_PERR_INT_CAUSE_V(x) ((x) << MEM_PERR_INT_CAUSE_S)
#define MEM_PERR_INT_CAUSE_F    MEM_PERR_INT_CAUSE_V(1U)

#define MEM_WRAP_INT_CAUSE_S    0
#define MEM_WRAP_INT_CAUSE_V(x) ((x) << MEM_WRAP_INT_CAUSE_S)
#define MEM_WRAP_INT_CAUSE_F    MEM_WRAP_INT_CAUSE_V(1U)

#define MA_INT_WRAP_STATUS_A	0x77e4

#define MEM_WRAP_ADDRESS_S    4
#define MEM_WRAP_ADDRESS_M    0xfffffffU
#define MEM_WRAP_ADDRESS_G(x) (((x) >> MEM_WRAP_ADDRESS_S) & MEM_WRAP_ADDRESS_M)

#define MEM_WRAP_CLIENT_NUM_S    0
#define MEM_WRAP_CLIENT_NUM_M    0xfU
#define MEM_WRAP_CLIENT_NUM_G(x) \
	(((x) >> MEM_WRAP_CLIENT_NUM_S) & MEM_WRAP_CLIENT_NUM_M)

#define MA_PARITY_ERROR_STATUS_A	0x77f4
#define MA_PARITY_ERROR_STATUS1_A	0x77f4
#define MA_PARITY_ERROR_STATUS2_A	0x7804

/* registers for module EDC_0 */
#define EDC_0_BASE_ADDR		0x7900

#define EDC_BIST_CMD_A		0x7904
#define EDC_BIST_CMD_ADDR_A	0x7908
#define EDC_BIST_CMD_LEN_A	0x790c
#define EDC_BIST_DATA_PATTERN_A 0x7910
#define EDC_BIST_STATUS_RDATA_A	0x7928
#define EDC_INT_CAUSE_A		0x7978

#define ECC_UE_PAR_S    5
#define ECC_UE_PAR_V(x) ((x) << ECC_UE_PAR_S)
#define ECC_UE_PAR_F    ECC_UE_PAR_V(1U)

#define ECC_CE_PAR_S    4
#define ECC_CE_PAR_V(x) ((x) << ECC_CE_PAR_S)
#define ECC_CE_PAR_F    ECC_CE_PAR_V(1U)

#define PERR_PAR_CAUSE_S    3
#define PERR_PAR_CAUSE_V(x) ((x) << PERR_PAR_CAUSE_S)
#define PERR_PAR_CAUSE_F    PERR_PAR_CAUSE_V(1U)

#define EDC_ECC_STATUS_A	0x797c

/* registers for module EDC_1 */
#define EDC_1_BASE_ADDR	0x7980

/* registers for module CIM */
#define CIM_BOOT_CFG_A 0x7b00
#define CIM_SDRAM_BASE_ADDR_A 0x7b14
#define CIM_SDRAM_ADDR_SIZE_A 0x7b18
#define CIM_EXTMEM2_BASE_ADDR_A 0x7b1c
#define CIM_EXTMEM2_ADDR_SIZE_A 0x7b20
#define CIM_PF_MAILBOX_CTRL_SHADOW_COPY_A 0x290

#define  BOOTADDR_M	0xffffff00U

#define UPCRST_S    0
#define UPCRST_V(x) ((x) << UPCRST_S)
#define UPCRST_F    UPCRST_V(1U)

#define CIM_PF_MAILBOX_DATA_A 0x240
#define CIM_PF_MAILBOX_CTRL_A 0x280

#define MBMSGVALID_S    3
#define MBMSGVALID_V(x) ((x) << MBMSGVALID_S)
#define MBMSGVALID_F    MBMSGVALID_V(1U)

#define MBINTREQ_S    2
#define MBINTREQ_V(x) ((x) << MBINTREQ_S)
#define MBINTREQ_F    MBINTREQ_V(1U)

#define MBOWNER_S    0
#define MBOWNER_M    0x3U
#define MBOWNER_V(x) ((x) << MBOWNER_S)
#define MBOWNER_G(x) (((x) >> MBOWNER_S) & MBOWNER_M)

#define CIM_PF_HOST_INT_ENABLE_A 0x288

#define MBMSGRDYINTEN_S    19
#define MBMSGRDYINTEN_V(x) ((x) << MBMSGRDYINTEN_S)
#define MBMSGRDYINTEN_F    MBMSGRDYINTEN_V(1U)

#define CIM_PF_HOST_INT_CAUSE_A 0x28c

#define MBMSGRDYINT_S    19
#define MBMSGRDYINT_V(x) ((x) << MBMSGRDYINT_S)
#define MBMSGRDYINT_F    MBMSGRDYINT_V(1U)

#define CIM_HOST_INT_CAUSE_A 0x7b2c

#define TIEQOUTPARERRINT_S    20
#define TIEQOUTPARERRINT_V(x) ((x) << TIEQOUTPARERRINT_S)
#define TIEQOUTPARERRINT_F    TIEQOUTPARERRINT_V(1U)

#define TIEQINPARERRINT_S    19
#define TIEQINPARERRINT_V(x) ((x) << TIEQINPARERRINT_S)
#define TIEQINPARERRINT_F    TIEQINPARERRINT_V(1U)

#define TIMER0INT_S    2
#define TIMER0INT_V(x) ((x) << TIMER0INT_S)
#define TIMER0INT_F    TIMER0INT_V(1U)

#define PREFDROPINT_S    1
#define PREFDROPINT_V(x) ((x) << PREFDROPINT_S)
#define PREFDROPINT_F    PREFDROPINT_V(1U)

#define UPACCNONZERO_S    0
#define UPACCNONZERO_V(x) ((x) << UPACCNONZERO_S)
#define UPACCNONZERO_F    UPACCNONZERO_V(1U)

#define MBHOSTPARERR_S    18
#define MBHOSTPARERR_V(x) ((x) << MBHOSTPARERR_S)
#define MBHOSTPARERR_F    MBHOSTPARERR_V(1U)

#define MBUPPARERR_S    17
#define MBUPPARERR_V(x) ((x) << MBUPPARERR_S)
#define MBUPPARERR_F    MBUPPARERR_V(1U)

#define IBQTP0PARERR_S    16
#define IBQTP0PARERR_V(x) ((x) << IBQTP0PARERR_S)
#define IBQTP0PARERR_F    IBQTP0PARERR_V(1U)

#define IBQTP1PARERR_S    15
#define IBQTP1PARERR_V(x) ((x) << IBQTP1PARERR_S)
#define IBQTP1PARERR_F    IBQTP1PARERR_V(1U)

#define IBQULPPARERR_S    14
#define IBQULPPARERR_V(x) ((x) << IBQULPPARERR_S)
#define IBQULPPARERR_F    IBQULPPARERR_V(1U)

#define IBQSGELOPARERR_S    13
#define IBQSGELOPARERR_V(x) ((x) << IBQSGELOPARERR_S)
#define IBQSGELOPARERR_F    IBQSGELOPARERR_V(1U)

#define IBQSGEHIPARERR_S    12
#define IBQSGEHIPARERR_V(x) ((x) << IBQSGEHIPARERR_S)
#define IBQSGEHIPARERR_F    IBQSGEHIPARERR_V(1U)

#define IBQNCSIPARERR_S    11
#define IBQNCSIPARERR_V(x) ((x) << IBQNCSIPARERR_S)
#define IBQNCSIPARERR_F    IBQNCSIPARERR_V(1U)

#define OBQULP0PARERR_S    10
#define OBQULP0PARERR_V(x) ((x) << OBQULP0PARERR_S)
#define OBQULP0PARERR_F    OBQULP0PARERR_V(1U)

#define OBQULP1PARERR_S    9
#define OBQULP1PARERR_V(x) ((x) << OBQULP1PARERR_S)
#define OBQULP1PARERR_F    OBQULP1PARERR_V(1U)

#define OBQULP2PARERR_S    8
#define OBQULP2PARERR_V(x) ((x) << OBQULP2PARERR_S)
#define OBQULP2PARERR_F    OBQULP2PARERR_V(1U)

#define OBQULP3PARERR_S    7
#define OBQULP3PARERR_V(x) ((x) << OBQULP3PARERR_S)
#define OBQULP3PARERR_F    OBQULP3PARERR_V(1U)

#define OBQSGEPARERR_S    6
#define OBQSGEPARERR_V(x) ((x) << OBQSGEPARERR_S)
#define OBQSGEPARERR_F    OBQSGEPARERR_V(1U)

#define OBQNCSIPARERR_S    5
#define OBQNCSIPARERR_V(x) ((x) << OBQNCSIPARERR_S)
#define OBQNCSIPARERR_F    OBQNCSIPARERR_V(1U)

#define CIM_HOST_UPACC_INT_CAUSE_A 0x7b34

#define EEPROMWRINT_S    30
#define EEPROMWRINT_V(x) ((x) << EEPROMWRINT_S)
#define EEPROMWRINT_F    EEPROMWRINT_V(1U)

#define TIMEOUTMAINT_S    29
#define TIMEOUTMAINT_V(x) ((x) << TIMEOUTMAINT_S)
#define TIMEOUTMAINT_F    TIMEOUTMAINT_V(1U)

#define TIMEOUTINT_S    28
#define TIMEOUTINT_V(x) ((x) << TIMEOUTINT_S)
#define TIMEOUTINT_F    TIMEOUTINT_V(1U)

#define RSPOVRLOOKUPINT_S    27
#define RSPOVRLOOKUPINT_V(x) ((x) << RSPOVRLOOKUPINT_S)
#define RSPOVRLOOKUPINT_F    RSPOVRLOOKUPINT_V(1U)

#define REQOVRLOOKUPINT_S    26
#define REQOVRLOOKUPINT_V(x) ((x) << REQOVRLOOKUPINT_S)
#define REQOVRLOOKUPINT_F    REQOVRLOOKUPINT_V(1U)

#define BLKWRPLINT_S    25
#define BLKWRPLINT_V(x) ((x) << BLKWRPLINT_S)
#define BLKWRPLINT_F    BLKWRPLINT_V(1U)

#define BLKRDPLINT_S    24
#define BLKRDPLINT_V(x) ((x) << BLKRDPLINT_S)
#define BLKRDPLINT_F    BLKRDPLINT_V(1U)

#define SGLWRPLINT_S    23
#define SGLWRPLINT_V(x) ((x) << SGLWRPLINT_S)
#define SGLWRPLINT_F    SGLWRPLINT_V(1U)

#define SGLRDPLINT_S    22
#define SGLRDPLINT_V(x) ((x) << SGLRDPLINT_S)
#define SGLRDPLINT_F    SGLRDPLINT_V(1U)

#define BLKWRCTLINT_S    21
#define BLKWRCTLINT_V(x) ((x) << BLKWRCTLINT_S)
#define BLKWRCTLINT_F    BLKWRCTLINT_V(1U)

#define BLKRDCTLINT_S    20
#define BLKRDCTLINT_V(x) ((x) << BLKRDCTLINT_S)
#define BLKRDCTLINT_F    BLKRDCTLINT_V(1U)

#define SGLWRCTLINT_S    19
#define SGLWRCTLINT_V(x) ((x) << SGLWRCTLINT_S)
#define SGLWRCTLINT_F    SGLWRCTLINT_V(1U)

#define SGLRDCTLINT_S    18
#define SGLRDCTLINT_V(x) ((x) << SGLRDCTLINT_S)
#define SGLRDCTLINT_F    SGLRDCTLINT_V(1U)

#define BLKWREEPROMINT_S    17
#define BLKWREEPROMINT_V(x) ((x) << BLKWREEPROMINT_S)
#define BLKWREEPROMINT_F    BLKWREEPROMINT_V(1U)

#define BLKRDEEPROMINT_S    16
#define BLKRDEEPROMINT_V(x) ((x) << BLKRDEEPROMINT_S)
#define BLKRDEEPROMINT_F    BLKRDEEPROMINT_V(1U)

#define SGLWREEPROMINT_S    15
#define SGLWREEPROMINT_V(x) ((x) << SGLWREEPROMINT_S)
#define SGLWREEPROMINT_F    SGLWREEPROMINT_V(1U)

#define SGLRDEEPROMINT_S    14
#define SGLRDEEPROMINT_V(x) ((x) << SGLRDEEPROMINT_S)
#define SGLRDEEPROMINT_F    SGLRDEEPROMINT_V(1U)

#define BLKWRFLASHINT_S    13
#define BLKWRFLASHINT_V(x) ((x) << BLKWRFLASHINT_S)
#define BLKWRFLASHINT_F    BLKWRFLASHINT_V(1U)

#define BLKRDFLASHINT_S    12
#define BLKRDFLASHINT_V(x) ((x) << BLKRDFLASHINT_S)
#define BLKRDFLASHINT_F    BLKRDFLASHINT_V(1U)

#define SGLWRFLASHINT_S    11
#define SGLWRFLASHINT_V(x) ((x) << SGLWRFLASHINT_S)
#define SGLWRFLASHINT_F    SGLWRFLASHINT_V(1U)

#define SGLRDFLASHINT_S    10
#define SGLRDFLASHINT_V(x) ((x) << SGLRDFLASHINT_S)
#define SGLRDFLASHINT_F    SGLRDFLASHINT_V(1U)

#define BLKWRBOOTINT_S    9
#define BLKWRBOOTINT_V(x) ((x) << BLKWRBOOTINT_S)
#define BLKWRBOOTINT_F    BLKWRBOOTINT_V(1U)

#define BLKRDBOOTINT_S    8
#define BLKRDBOOTINT_V(x) ((x) << BLKRDBOOTINT_S)
#define BLKRDBOOTINT_F    BLKRDBOOTINT_V(1U)

#define SGLWRBOOTINT_S    7
#define SGLWRBOOTINT_V(x) ((x) << SGLWRBOOTINT_S)
#define SGLWRBOOTINT_F    SGLWRBOOTINT_V(1U)

#define SGLRDBOOTINT_S    6
#define SGLRDBOOTINT_V(x) ((x) << SGLRDBOOTINT_S)
#define SGLRDBOOTINT_F    SGLRDBOOTINT_V(1U)

#define ILLWRBEINT_S    5
#define ILLWRBEINT_V(x) ((x) << ILLWRBEINT_S)
#define ILLWRBEINT_F    ILLWRBEINT_V(1U)

#define ILLRDBEINT_S    4
#define ILLRDBEINT_V(x) ((x) << ILLRDBEINT_S)
#define ILLRDBEINT_F    ILLRDBEINT_V(1U)

#define ILLRDINT_S    3
#define ILLRDINT_V(x) ((x) << ILLRDINT_S)
#define ILLRDINT_F    ILLRDINT_V(1U)

#define ILLWRINT_S    2
#define ILLWRINT_V(x) ((x) << ILLWRINT_S)
#define ILLWRINT_F    ILLWRINT_V(1U)

#define ILLTRANSINT_S    1
#define ILLTRANSINT_V(x) ((x) << ILLTRANSINT_S)
#define ILLTRANSINT_F    ILLTRANSINT_V(1U)

#define RSVDSPACEINT_S    0
#define RSVDSPACEINT_V(x) ((x) << RSVDSPACEINT_S)
#define RSVDSPACEINT_F    RSVDSPACEINT_V(1U)

/* registers for module TP */
#define DBGLAWHLF_S    23
#define DBGLAWHLF_V(x) ((x) << DBGLAWHLF_S)
#define DBGLAWHLF_F    DBGLAWHLF_V(1U)

#define DBGLAWPTR_S    16
#define DBGLAWPTR_M    0x7fU
#define DBGLAWPTR_G(x) (((x) >> DBGLAWPTR_S) & DBGLAWPTR_M)

#define DBGLAENABLE_S    12
#define DBGLAENABLE_V(x) ((x) << DBGLAENABLE_S)
#define DBGLAENABLE_F    DBGLAENABLE_V(1U)

#define DBGLARPTR_S    0
#define DBGLARPTR_M    0x7fU
#define DBGLARPTR_V(x) ((x) << DBGLARPTR_S)

#define CRXPKTENC_S    3
#define CRXPKTENC_V(x) ((x) << CRXPKTENC_S)
#define CRXPKTENC_F    CRXPKTENC_V(1U)

#define TP_DBG_LA_DATAL_A	0x7ed8
#define TP_DBG_LA_CONFIG_A	0x7ed4
#define TP_OUT_CONFIG_A		0x7d04
#define TP_GLOBAL_CONFIG_A	0x7d08

#define TP_CMM_TCB_BASE_A 0x7d10
#define TP_CMM_MM_BASE_A 0x7d14
#define TP_CMM_TIMER_BASE_A 0x7d18
#define TP_PMM_TX_BASE_A 0x7d20
#define TP_PMM_RX_BASE_A 0x7d28
#define TP_PMM_RX_PAGE_SIZE_A 0x7d2c
#define TP_PMM_RX_MAX_PAGE_A 0x7d30
#define TP_PMM_TX_PAGE_SIZE_A 0x7d34
#define TP_PMM_TX_MAX_PAGE_A 0x7d38
#define TP_CMM_MM_MAX_PSTRUCT_A 0x7e6c

#define PMRXNUMCHN_S    31
#define PMRXNUMCHN_V(x) ((x) << PMRXNUMCHN_S)
#define PMRXNUMCHN_F    PMRXNUMCHN_V(1U)

#define PMTXNUMCHN_S    30
#define PMTXNUMCHN_M    0x3U
#define PMTXNUMCHN_G(x) (((x) >> PMTXNUMCHN_S) & PMTXNUMCHN_M)

#define PMTXMAXPAGE_S    0
#define PMTXMAXPAGE_M    0x1fffffU
#define PMTXMAXPAGE_G(x) (((x) >> PMTXMAXPAGE_S) & PMTXMAXPAGE_M)

#define PMRXMAXPAGE_S    0
#define PMRXMAXPAGE_M    0x1fffffU
#define PMRXMAXPAGE_G(x) (((x) >> PMRXMAXPAGE_S) & PMRXMAXPAGE_M)

#define DBGLAMODE_S	14
#define DBGLAMODE_M	0x3U
#define DBGLAMODE_G(x)	(((x) >> DBGLAMODE_S) & DBGLAMODE_M)

#define FIVETUPLELOOKUP_S    17
#define FIVETUPLELOOKUP_M    0x3U
#define FIVETUPLELOOKUP_V(x) ((x) << FIVETUPLELOOKUP_S)
#define FIVETUPLELOOKUP_G(x) (((x) >> FIVETUPLELOOKUP_S) & FIVETUPLELOOKUP_M)

#define TP_PARA_REG2_A 0x7d68

#define MAXRXDATA_S    16
#define MAXRXDATA_M    0xffffU
#define MAXRXDATA_G(x) (((x) >> MAXRXDATA_S) & MAXRXDATA_M)

#define TP_TIMER_RESOLUTION_A 0x7d90

#define TIMERRESOLUTION_S    16
#define TIMERRESOLUTION_M    0xffU
#define TIMERRESOLUTION_G(x) (((x) >> TIMERRESOLUTION_S) & TIMERRESOLUTION_M)

#define TIMESTAMPRESOLUTION_S    8
#define TIMESTAMPRESOLUTION_M    0xffU
#define TIMESTAMPRESOLUTION_G(x) \
	(((x) >> TIMESTAMPRESOLUTION_S) & TIMESTAMPRESOLUTION_M)

#define DELAYEDACKRESOLUTION_S    0
#define DELAYEDACKRESOLUTION_M    0xffU
#define DELAYEDACKRESOLUTION_G(x) \
	(((x) >> DELAYEDACKRESOLUTION_S) & DELAYEDACKRESOLUTION_M)

#define TP_SHIFT_CNT_A 0x7dc0
#define TP_RXT_MIN_A 0x7d98
#define TP_RXT_MAX_A 0x7d9c
#define TP_PERS_MIN_A 0x7da0
#define TP_PERS_MAX_A 0x7da4
#define TP_KEEP_IDLE_A 0x7da8
#define TP_KEEP_INTVL_A 0x7dac
#define TP_INIT_SRTT_A 0x7db0
#define TP_DACK_TIMER_A 0x7db4
#define TP_FINWAIT2_TIMER_A 0x7db8

#define INITSRTT_S    0
#define INITSRTT_M    0xffffU
#define INITSRTT_G(x) (((x) >> INITSRTT_S) & INITSRTT_M)

#define PERSMAX_S    0
#define PERSMAX_M    0x3fffffffU
#define PERSMAX_V(x) ((x) << PERSMAX_S)
#define PERSMAX_G(x) (((x) >> PERSMAX_S) & PERSMAX_M)

#define SYNSHIFTMAX_S    24
#define SYNSHIFTMAX_M    0xffU
#define SYNSHIFTMAX_V(x) ((x) << SYNSHIFTMAX_S)
#define SYNSHIFTMAX_G(x) (((x) >> SYNSHIFTMAX_S) & SYNSHIFTMAX_M)

#define RXTSHIFTMAXR1_S    20
#define RXTSHIFTMAXR1_M    0xfU
#define RXTSHIFTMAXR1_V(x) ((x) << RXTSHIFTMAXR1_S)
#define RXTSHIFTMAXR1_G(x) (((x) >> RXTSHIFTMAXR1_S) & RXTSHIFTMAXR1_M)

#define RXTSHIFTMAXR2_S    16
#define RXTSHIFTMAXR2_M    0xfU
#define RXTSHIFTMAXR2_V(x) ((x) << RXTSHIFTMAXR2_S)
#define RXTSHIFTMAXR2_G(x) (((x) >> RXTSHIFTMAXR2_S) & RXTSHIFTMAXR2_M)

#define PERSHIFTBACKOFFMAX_S    12
#define PERSHIFTBACKOFFMAX_M    0xfU
#define PERSHIFTBACKOFFMAX_V(x) ((x) << PERSHIFTBACKOFFMAX_S)
#define PERSHIFTBACKOFFMAX_G(x) \
	(((x) >> PERSHIFTBACKOFFMAX_S) & PERSHIFTBACKOFFMAX_M)

#define PERSHIFTMAX_S    8
#define PERSHIFTMAX_M    0xfU
#define PERSHIFTMAX_V(x) ((x) << PERSHIFTMAX_S)
#define PERSHIFTMAX_G(x) (((x) >> PERSHIFTMAX_S) & PERSHIFTMAX_M)

#define KEEPALIVEMAXR1_S    4
#define KEEPALIVEMAXR1_M    0xfU
#define KEEPALIVEMAXR1_V(x) ((x) << KEEPALIVEMAXR1_S)
#define KEEPALIVEMAXR1_G(x) (((x) >> KEEPALIVEMAXR1_S) & KEEPALIVEMAXR1_M)

#define KEEPALIVEMAXR2_S    0
#define KEEPALIVEMAXR2_M    0xfU
#define KEEPALIVEMAXR2_V(x) ((x) << KEEPALIVEMAXR2_S)
#define KEEPALIVEMAXR2_G(x) (((x) >> KEEPALIVEMAXR2_S) & KEEPALIVEMAXR2_M)

#define ROWINDEX_S    16
#define ROWINDEX_V(x) ((x) << ROWINDEX_S)

#define TP_CCTRL_TABLE_A	0x7ddc
#define TP_PACE_TABLE_A 0x7dd8
#define TP_MTU_TABLE_A		0x7de4

#define MTUINDEX_S    24
#define MTUINDEX_V(x) ((x) << MTUINDEX_S)

#define MTUWIDTH_S    16
#define MTUWIDTH_M    0xfU
#define MTUWIDTH_V(x) ((x) << MTUWIDTH_S)
#define MTUWIDTH_G(x) (((x) >> MTUWIDTH_S) & MTUWIDTH_M)

#define MTUVALUE_S    0
#define MTUVALUE_M    0x3fffU
#define MTUVALUE_V(x) ((x) << MTUVALUE_S)
#define MTUVALUE_G(x) (((x) >> MTUVALUE_S) & MTUVALUE_M)

#define TP_RSS_LKP_TABLE_A	0x7dec
#define TP_CMM_MM_RX_FLST_BASE_A 0x7e60
#define TP_CMM_MM_TX_FLST_BASE_A 0x7e64
#define TP_CMM_MM_PS_FLST_BASE_A 0x7e68

#define LKPTBLROWVLD_S    31
#define LKPTBLROWVLD_V(x) ((x) << LKPTBLROWVLD_S)
#define LKPTBLROWVLD_F    LKPTBLROWVLD_V(1U)

#define LKPTBLQUEUE1_S    10
#define LKPTBLQUEUE1_M    0x3ffU
#define LKPTBLQUEUE1_G(x) (((x) >> LKPTBLQUEUE1_S) & LKPTBLQUEUE1_M)

#define LKPTBLQUEUE0_S    0
#define LKPTBLQUEUE0_M    0x3ffU
#define LKPTBLQUEUE0_G(x) (((x) >> LKPTBLQUEUE0_S) & LKPTBLQUEUE0_M)

#define TP_TM_PIO_ADDR_A 0x7e18
#define TP_TM_PIO_DATA_A 0x7e1c
#define TP_MOD_CONFIG_A 0x7e24

#define TIMERMODE_S    8
#define TIMERMODE_M    0xffU
#define TIMERMODE_G(x) (((x) >> TIMERMODE_S) & TIMERMODE_M)

#define TP_TX_MOD_Q1_Q0_TIMER_SEPARATOR_A 0x3
#define TP_TX_MOD_Q1_Q0_RATE_LIMIT_A 0x8

#define TP_PIO_ADDR_A	0x7e40
#define TP_PIO_DATA_A	0x7e44
#define TP_MIB_INDEX_A	0x7e50
#define TP_MIB_DATA_A	0x7e54
#define TP_INT_CAUSE_A	0x7e74

#define FLMTXFLSTEMPTY_S    30
#define FLMTXFLSTEMPTY_V(x) ((x) << FLMTXFLSTEMPTY_S)
#define FLMTXFLSTEMPTY_F    FLMTXFLSTEMPTY_V(1U)

#define TP_TX_ORATE_A 0x7ebc

#define OFDRATE3_S    24
#define OFDRATE3_M    0xffU
#define OFDRATE3_G(x) (((x) >> OFDRATE3_S) & OFDRATE3_M)

#define OFDRATE2_S    16
#define OFDRATE2_M    0xffU
#define OFDRATE2_G(x) (((x) >> OFDRATE2_S) & OFDRATE2_M)

#define OFDRATE1_S    8
#define OFDRATE1_M    0xffU
#define OFDRATE1_G(x) (((x) >> OFDRATE1_S) & OFDRATE1_M)

#define OFDRATE0_S    0
#define OFDRATE0_M    0xffU
#define OFDRATE0_G(x) (((x) >> OFDRATE0_S) & OFDRATE0_M)

#define TP_TX_TRATE_A 0x7ed0

#define TNLRATE3_S    24
#define TNLRATE3_M    0xffU
#define TNLRATE3_G(x) (((x) >> TNLRATE3_S) & TNLRATE3_M)

#define TNLRATE2_S    16
#define TNLRATE2_M    0xffU
#define TNLRATE2_G(x) (((x) >> TNLRATE2_S) & TNLRATE2_M)

#define TNLRATE1_S    8
#define TNLRATE1_M    0xffU
#define TNLRATE1_G(x) (((x) >> TNLRATE1_S) & TNLRATE1_M)

#define TNLRATE0_S    0
#define TNLRATE0_M    0xffU
#define TNLRATE0_G(x) (((x) >> TNLRATE0_S) & TNLRATE0_M)

#define TP_VLAN_PRI_MAP_A 0x140

#define FRAGMENTATION_S    9
#define FRAGMENTATION_V(x) ((x) << FRAGMENTATION_S)
#define FRAGMENTATION_F    FRAGMENTATION_V(1U)

#define MPSHITTYPE_S    8
#define MPSHITTYPE_V(x) ((x) << MPSHITTYPE_S)
#define MPSHITTYPE_F    MPSHITTYPE_V(1U)

#define MACMATCH_S    7
#define MACMATCH_V(x) ((x) << MACMATCH_S)
#define MACMATCH_F    MACMATCH_V(1U)

#define ETHERTYPE_S    6
#define ETHERTYPE_V(x) ((x) << ETHERTYPE_S)
#define ETHERTYPE_F    ETHERTYPE_V(1U)

#define PROTOCOL_S    5
#define PROTOCOL_V(x) ((x) << PROTOCOL_S)
#define PROTOCOL_F    PROTOCOL_V(1U)

#define TOS_S    4
#define TOS_V(x) ((x) << TOS_S)
#define TOS_F    TOS_V(1U)

#define VLAN_S    3
#define VLAN_V(x) ((x) << VLAN_S)
#define VLAN_F    VLAN_V(1U)

#define VNIC_ID_S    2
#define VNIC_ID_V(x) ((x) << VNIC_ID_S)
#define VNIC_ID_F    VNIC_ID_V(1U)

#define PORT_S    1
#define PORT_V(x) ((x) << PORT_S)
#define PORT_F    PORT_V(1U)

#define FCOE_S    0
#define FCOE_V(x) ((x) << FCOE_S)
#define FCOE_F    FCOE_V(1U)

#define FILTERMODE_S    15
#define FILTERMODE_V(x) ((x) << FILTERMODE_S)
#define FILTERMODE_F    FILTERMODE_V(1U)

#define FCOEMASK_S    14
#define FCOEMASK_V(x) ((x) << FCOEMASK_S)
#define FCOEMASK_F    FCOEMASK_V(1U)

#define TP_INGRESS_CONFIG_A	0x141

#define VNIC_S    11
#define VNIC_V(x) ((x) << VNIC_S)
#define VNIC_F    VNIC_V(1U)

#define CSUM_HAS_PSEUDO_HDR_S    10
#define CSUM_HAS_PSEUDO_HDR_V(x) ((x) << CSUM_HAS_PSEUDO_HDR_S)
#define CSUM_HAS_PSEUDO_HDR_F    CSUM_HAS_PSEUDO_HDR_V(1U)

#define TP_MIB_MAC_IN_ERR_0_A	0x0
#define TP_MIB_HDR_IN_ERR_0_A	0x4
#define TP_MIB_TCP_IN_ERR_0_A	0x8
#define TP_MIB_TCP_OUT_RST_A	0xc
#define TP_MIB_TCP_IN_SEG_HI_A	0x10
#define TP_MIB_TCP_IN_SEG_LO_A	0x11
#define TP_MIB_TCP_OUT_SEG_HI_A	0x12
#define TP_MIB_TCP_OUT_SEG_LO_A 0x13
#define TP_MIB_TCP_RXT_SEG_HI_A	0x14
#define TP_MIB_TCP_RXT_SEG_LO_A	0x15
#define TP_MIB_TNL_CNG_DROP_0_A 0x18
#define TP_MIB_OFD_CHN_DROP_0_A 0x1c
#define TP_MIB_TCP_V6IN_ERR_0_A 0x28
#define TP_MIB_TCP_V6OUT_RST_A	0x2c
#define TP_MIB_OFD_ARP_DROP_A	0x36
#define TP_MIB_CPL_IN_REQ_0_A	0x38
#define TP_MIB_CPL_OUT_RSP_0_A	0x3c
#define TP_MIB_TNL_DROP_0_A	0x44
#define TP_MIB_FCOE_DDP_0_A	0x48
#define TP_MIB_FCOE_DROP_0_A	0x4c
#define TP_MIB_FCOE_BYTE_0_HI_A	0x50
#define TP_MIB_OFD_VLN_DROP_0_A	0x58
#define TP_MIB_USM_PKTS_A	0x5c
#define TP_MIB_RQE_DFR_PKT_A	0x64

#define ULP_TX_INT_CAUSE_A	0x8dcc
#define ULP_TX_TPT_LLIMIT_A	0x8dd4
#define ULP_TX_TPT_ULIMIT_A	0x8dd8
#define ULP_TX_PBL_LLIMIT_A	0x8ddc
#define ULP_TX_PBL_ULIMIT_A	0x8de0
#define ULP_TX_ERR_TABLE_BASE_A 0x8e04

#define PBL_BOUND_ERR_CH3_S    31
#define PBL_BOUND_ERR_CH3_V(x) ((x) << PBL_BOUND_ERR_CH3_S)
#define PBL_BOUND_ERR_CH3_F    PBL_BOUND_ERR_CH3_V(1U)

#define PBL_BOUND_ERR_CH2_S    30
#define PBL_BOUND_ERR_CH2_V(x) ((x) << PBL_BOUND_ERR_CH2_S)
#define PBL_BOUND_ERR_CH2_F    PBL_BOUND_ERR_CH2_V(1U)

#define PBL_BOUND_ERR_CH1_S    29
#define PBL_BOUND_ERR_CH1_V(x) ((x) << PBL_BOUND_ERR_CH1_S)
#define PBL_BOUND_ERR_CH1_F    PBL_BOUND_ERR_CH1_V(1U)

#define PBL_BOUND_ERR_CH0_S    28
#define PBL_BOUND_ERR_CH0_V(x) ((x) << PBL_BOUND_ERR_CH0_S)
#define PBL_BOUND_ERR_CH0_F    PBL_BOUND_ERR_CH0_V(1U)

#define PM_RX_INT_CAUSE_A	0x8fdc
#define PM_RX_STAT_CONFIG_A 0x8fc8
#define PM_RX_STAT_COUNT_A 0x8fcc
#define PM_RX_STAT_LSB_A 0x8fd0
#define PM_RX_DBG_CTRL_A 0x8fd0
#define PM_RX_DBG_DATA_A 0x8fd4
#define PM_RX_DBG_STAT_MSB_A 0x10013

#define PMRX_FRAMING_ERROR_F	0x003ffff0U

#define ZERO_E_CMD_ERROR_S    22
#define ZERO_E_CMD_ERROR_V(x) ((x) << ZERO_E_CMD_ERROR_S)
#define ZERO_E_CMD_ERROR_F    ZERO_E_CMD_ERROR_V(1U)

#define OCSPI_PAR_ERROR_S    3
#define OCSPI_PAR_ERROR_V(x) ((x) << OCSPI_PAR_ERROR_S)
#define OCSPI_PAR_ERROR_F    OCSPI_PAR_ERROR_V(1U)

#define DB_OPTIONS_PAR_ERROR_S    2
#define DB_OPTIONS_PAR_ERROR_V(x) ((x) << DB_OPTIONS_PAR_ERROR_S)
#define DB_OPTIONS_PAR_ERROR_F    DB_OPTIONS_PAR_ERROR_V(1U)

#define IESPI_PAR_ERROR_S    1
#define IESPI_PAR_ERROR_V(x) ((x) << IESPI_PAR_ERROR_S)
#define IESPI_PAR_ERROR_F    IESPI_PAR_ERROR_V(1U)

#define ULP_TX_LA_RDPTR_0_A 0x8ec0
#define ULP_TX_LA_RDDATA_0_A 0x8ec4
#define ULP_TX_LA_WRPTR_0_A 0x8ec8

#define PMRX_E_PCMD_PAR_ERROR_S    0
#define PMRX_E_PCMD_PAR_ERROR_V(x) ((x) << PMRX_E_PCMD_PAR_ERROR_S)
#define PMRX_E_PCMD_PAR_ERROR_F    PMRX_E_PCMD_PAR_ERROR_V(1U)

#define PM_TX_INT_CAUSE_A	0x8ffc
#define PM_TX_STAT_CONFIG_A 0x8fe8
#define PM_TX_STAT_COUNT_A 0x8fec
#define PM_TX_STAT_LSB_A 0x8ff0
#define PM_TX_DBG_CTRL_A 0x8ff0
#define PM_TX_DBG_DATA_A 0x8ff4
#define PM_TX_DBG_STAT_MSB_A 0x1001a

#define PCMD_LEN_OVFL0_S    31
#define PCMD_LEN_OVFL0_V(x) ((x) << PCMD_LEN_OVFL0_S)
#define PCMD_LEN_OVFL0_F    PCMD_LEN_OVFL0_V(1U)

#define PCMD_LEN_OVFL1_S    30
#define PCMD_LEN_OVFL1_V(x) ((x) << PCMD_LEN_OVFL1_S)
#define PCMD_LEN_OVFL1_F    PCMD_LEN_OVFL1_V(1U)

#define PCMD_LEN_OVFL2_S    29
#define PCMD_LEN_OVFL2_V(x) ((x) << PCMD_LEN_OVFL2_S)
#define PCMD_LEN_OVFL2_F    PCMD_LEN_OVFL2_V(1U)

#define ZERO_C_CMD_ERROR_S    28
#define ZERO_C_CMD_ERROR_V(x) ((x) << ZERO_C_CMD_ERROR_S)
#define ZERO_C_CMD_ERROR_F    ZERO_C_CMD_ERROR_V(1U)

#define  PMTX_FRAMING_ERROR_F 0x0ffffff0U

#define OESPI_PAR_ERROR_S    3
#define OESPI_PAR_ERROR_V(x) ((x) << OESPI_PAR_ERROR_S)
#define OESPI_PAR_ERROR_F    OESPI_PAR_ERROR_V(1U)

#define ICSPI_PAR_ERROR_S    1
#define ICSPI_PAR_ERROR_V(x) ((x) << ICSPI_PAR_ERROR_S)
#define ICSPI_PAR_ERROR_F    ICSPI_PAR_ERROR_V(1U)

#define PMTX_C_PCMD_PAR_ERROR_S    0
#define PMTX_C_PCMD_PAR_ERROR_V(x) ((x) << PMTX_C_PCMD_PAR_ERROR_S)
#define PMTX_C_PCMD_PAR_ERROR_F    PMTX_C_PCMD_PAR_ERROR_V(1U)

#define MPS_PORT_STAT_TX_PORT_BYTES_L 0x400
#define MPS_PORT_STAT_TX_PORT_BYTES_H 0x404
#define MPS_PORT_STAT_TX_PORT_FRAMES_L 0x408
#define MPS_PORT_STAT_TX_PORT_FRAMES_H 0x40c
#define MPS_PORT_STAT_TX_PORT_BCAST_L 0x410
#define MPS_PORT_STAT_TX_PORT_BCAST_H 0x414
#define MPS_PORT_STAT_TX_PORT_MCAST_L 0x418
#define MPS_PORT_STAT_TX_PORT_MCAST_H 0x41c
#define MPS_PORT_STAT_TX_PORT_UCAST_L 0x420
#define MPS_PORT_STAT_TX_PORT_UCAST_H 0x424
#define MPS_PORT_STAT_TX_PORT_ERROR_L 0x428
#define MPS_PORT_STAT_TX_PORT_ERROR_H 0x42c
#define MPS_PORT_STAT_TX_PORT_64B_L 0x430
#define MPS_PORT_STAT_TX_PORT_64B_H 0x434
#define MPS_PORT_STAT_TX_PORT_65B_127B_L 0x438
#define MPS_PORT_STAT_TX_PORT_65B_127B_H 0x43c
#define MPS_PORT_STAT_TX_PORT_128B_255B_L 0x440
#define MPS_PORT_STAT_TX_PORT_128B_255B_H 0x444
#define MPS_PORT_STAT_TX_PORT_256B_511B_L 0x448
#define MPS_PORT_STAT_TX_PORT_256B_511B_H 0x44c
#define MPS_PORT_STAT_TX_PORT_512B_1023B_L 0x450
#define MPS_PORT_STAT_TX_PORT_512B_1023B_H 0x454
#define MPS_PORT_STAT_TX_PORT_1024B_1518B_L 0x458
#define MPS_PORT_STAT_TX_PORT_1024B_1518B_H 0x45c
#define MPS_PORT_STAT_TX_PORT_1519B_MAX_L 0x460
#define MPS_PORT_STAT_TX_PORT_1519B_MAX_H 0x464
#define MPS_PORT_STAT_TX_PORT_DROP_L 0x468
#define MPS_PORT_STAT_TX_PORT_DROP_H 0x46c
#define MPS_PORT_STAT_TX_PORT_PAUSE_L 0x470
#define MPS_PORT_STAT_TX_PORT_PAUSE_H 0x474
#define MPS_PORT_STAT_TX_PORT_PPP0_L 0x478
#define MPS_PORT_STAT_TX_PORT_PPP0_H 0x47c
#define MPS_PORT_STAT_TX_PORT_PPP1_L 0x480
#define MPS_PORT_STAT_TX_PORT_PPP1_H 0x484
#define MPS_PORT_STAT_TX_PORT_PPP2_L 0x488
#define MPS_PORT_STAT_TX_PORT_PPP2_H 0x48c
#define MPS_PORT_STAT_TX_PORT_PPP3_L 0x490
#define MPS_PORT_STAT_TX_PORT_PPP3_H 0x494
#define MPS_PORT_STAT_TX_PORT_PPP4_L 0x498
#define MPS_PORT_STAT_TX_PORT_PPP4_H 0x49c
#define MPS_PORT_STAT_TX_PORT_PPP5_L 0x4a0
#define MPS_PORT_STAT_TX_PORT_PPP5_H 0x4a4
#define MPS_PORT_STAT_TX_PORT_PPP6_L 0x4a8
#define MPS_PORT_STAT_TX_PORT_PPP6_H 0x4ac
#define MPS_PORT_STAT_TX_PORT_PPP7_L 0x4b0
#define MPS_PORT_STAT_TX_PORT_PPP7_H 0x4b4
#define MPS_PORT_STAT_LB_PORT_BYTES_L 0x4c0
#define MPS_PORT_STAT_LB_PORT_BYTES_H 0x4c4
#define MPS_PORT_STAT_LB_PORT_FRAMES_L 0x4c8
#define MPS_PORT_STAT_LB_PORT_FRAMES_H 0x4cc
#define MPS_PORT_STAT_LB_PORT_BCAST_L 0x4d0
#define MPS_PORT_STAT_LB_PORT_BCAST_H 0x4d4
#define MPS_PORT_STAT_LB_PORT_MCAST_L 0x4d8
#define MPS_PORT_STAT_LB_PORT_MCAST_H 0x4dc
#define MPS_PORT_STAT_LB_PORT_UCAST_L 0x4e0
#define MPS_PORT_STAT_LB_PORT_UCAST_H 0x4e4
#define MPS_PORT_STAT_LB_PORT_ERROR_L 0x4e8
#define MPS_PORT_STAT_LB_PORT_ERROR_H 0x4ec
#define MPS_PORT_STAT_LB_PORT_64B_L 0x4f0
#define MPS_PORT_STAT_LB_PORT_64B_H 0x4f4
#define MPS_PORT_STAT_LB_PORT_65B_127B_L 0x4f8
#define MPS_PORT_STAT_LB_PORT_65B_127B_H 0x4fc
#define MPS_PORT_STAT_LB_PORT_128B_255B_L 0x500
#define MPS_PORT_STAT_LB_PORT_128B_255B_H 0x504
#define MPS_PORT_STAT_LB_PORT_256B_511B_L 0x508
#define MPS_PORT_STAT_LB_PORT_256B_511B_H 0x50c
#define MPS_PORT_STAT_LB_PORT_512B_1023B_L 0x510
#define MPS_PORT_STAT_LB_PORT_512B_1023B_H 0x514
#define MPS_PORT_STAT_LB_PORT_1024B_1518B_L 0x518
#define MPS_PORT_STAT_LB_PORT_1024B_1518B_H 0x51c
#define MPS_PORT_STAT_LB_PORT_1519B_MAX_L 0x520
#define MPS_PORT_STAT_LB_PORT_1519B_MAX_H 0x524
#define MPS_PORT_STAT_LB_PORT_DROP_FRAMES 0x528
#define MPS_PORT_STAT_LB_PORT_DROP_FRAMES_L 0x528
#define MPS_PORT_STAT_RX_PORT_BYTES_L 0x540
#define MPS_PORT_STAT_RX_PORT_BYTES_H 0x544
#define MPS_PORT_STAT_RX_PORT_FRAMES_L 0x548
#define MPS_PORT_STAT_RX_PORT_FRAMES_H 0x54c
#define MPS_PORT_STAT_RX_PORT_BCAST_L 0x550
#define MPS_PORT_STAT_RX_PORT_BCAST_H 0x554
#define MPS_PORT_STAT_RX_PORT_MCAST_L 0x558
#define MPS_PORT_STAT_RX_PORT_MCAST_H 0x55c
#define MPS_PORT_STAT_RX_PORT_UCAST_L 0x560
#define MPS_PORT_STAT_RX_PORT_UCAST_H 0x564
#define MPS_PORT_STAT_RX_PORT_MTU_ERROR_L 0x568
#define MPS_PORT_STAT_RX_PORT_MTU_ERROR_H 0x56c
#define MPS_PORT_STAT_RX_PORT_MTU_CRC_ERROR_L 0x570
#define MPS_PORT_STAT_RX_PORT_MTU_CRC_ERROR_H 0x574
#define MPS_PORT_STAT_RX_PORT_CRC_ERROR_L 0x578
#define MPS_PORT_STAT_RX_PORT_CRC_ERROR_H 0x57c
#define MPS_PORT_STAT_RX_PORT_LEN_ERROR_L 0x580
#define MPS_PORT_STAT_RX_PORT_LEN_ERROR_H 0x584
#define MPS_PORT_STAT_RX_PORT_SYM_ERROR_L 0x588
#define MPS_PORT_STAT_RX_PORT_SYM_ERROR_H 0x58c
#define MPS_PORT_STAT_RX_PORT_64B_L 0x590
#define MPS_PORT_STAT_RX_PORT_64B_H 0x594
#define MPS_PORT_STAT_RX_PORT_65B_127B_L 0x598
#define MPS_PORT_STAT_RX_PORT_65B_127B_H 0x59c
#define MPS_PORT_STAT_RX_PORT_128B_255B_L 0x5a0
#define MPS_PORT_STAT_RX_PORT_128B_255B_H 0x5a4
#define MPS_PORT_STAT_RX_PORT_256B_511B_L 0x5a8
#define MPS_PORT_STAT_RX_PORT_256B_511B_H 0x5ac
#define MPS_PORT_STAT_RX_PORT_512B_1023B_L 0x5b0
#define MPS_PORT_STAT_RX_PORT_512B_1023B_H 0x5b4
#define MPS_PORT_STAT_RX_PORT_1024B_1518B_L 0x5b8
#define MPS_PORT_STAT_RX_PORT_1024B_1518B_H 0x5bc
#define MPS_PORT_STAT_RX_PORT_1519B_MAX_L 0x5c0
#define MPS_PORT_STAT_RX_PORT_1519B_MAX_H 0x5c4
#define MPS_PORT_STAT_RX_PORT_PAUSE_L 0x5c8
#define MPS_PORT_STAT_RX_PORT_PAUSE_H 0x5cc
#define MPS_PORT_STAT_RX_PORT_PPP0_L 0x5d0
#define MPS_PORT_STAT_RX_PORT_PPP0_H 0x5d4
#define MPS_PORT_STAT_RX_PORT_PPP1_L 0x5d8
#define MPS_PORT_STAT_RX_PORT_PPP1_H 0x5dc
#define MPS_PORT_STAT_RX_PORT_PPP2_L 0x5e0
#define MPS_PORT_STAT_RX_PORT_PPP2_H 0x5e4
#define MPS_PORT_STAT_RX_PORT_PPP3_L 0x5e8
#define MPS_PORT_STAT_RX_PORT_PPP3_H 0x5ec
#define MPS_PORT_STAT_RX_PORT_PPP4_L 0x5f0
#define MPS_PORT_STAT_RX_PORT_PPP4_H 0x5f4
#define MPS_PORT_STAT_RX_PORT_PPP5_L 0x5f8
#define MPS_PORT_STAT_RX_PORT_PPP5_H 0x5fc
#define MPS_PORT_STAT_RX_PORT_PPP6_L 0x600
#define MPS_PORT_STAT_RX_PORT_PPP6_H 0x604
#define MPS_PORT_STAT_RX_PORT_PPP7_L 0x608
#define MPS_PORT_STAT_RX_PORT_PPP7_H 0x60c
#define MPS_PORT_STAT_RX_PORT_LESS_64B_L 0x610
#define MPS_PORT_STAT_RX_PORT_LESS_64B_H 0x614
#define MAC_PORT_MAGIC_MACID_LO 0x824
#define MAC_PORT_MAGIC_MACID_HI 0x828
#define MAC_PORT_TX_TS_VAL_LO   0x928
#define MAC_PORT_TX_TS_VAL_HI   0x92c

#define MAC_PORT_EPIO_DATA0_A 0x8c0
#define MAC_PORT_EPIO_DATA1_A 0x8c4
#define MAC_PORT_EPIO_DATA2_A 0x8c8
#define MAC_PORT_EPIO_DATA3_A 0x8cc
#define MAC_PORT_EPIO_OP_A 0x8d0

#define MAC_PORT_CFG2_A 0x818

#define MPS_CMN_CTL_A	0x9000

#define COUNTPAUSEMCRX_S    5
#define COUNTPAUSEMCRX_V(x) ((x) << COUNTPAUSEMCRX_S)
#define COUNTPAUSEMCRX_F    COUNTPAUSEMCRX_V(1U)

#define COUNTPAUSESTATRX_S    4
#define COUNTPAUSESTATRX_V(x) ((x) << COUNTPAUSESTATRX_S)
#define COUNTPAUSESTATRX_F    COUNTPAUSESTATRX_V(1U)

#define COUNTPAUSEMCTX_S    3
#define COUNTPAUSEMCTX_V(x) ((x) << COUNTPAUSEMCTX_S)
#define COUNTPAUSEMCTX_F    COUNTPAUSEMCTX_V(1U)

#define COUNTPAUSESTATTX_S    2
#define COUNTPAUSESTATTX_V(x) ((x) << COUNTPAUSESTATTX_S)
#define COUNTPAUSESTATTX_F    COUNTPAUSESTATTX_V(1U)

#define NUMPORTS_S    0
#define NUMPORTS_M    0x3U
#define NUMPORTS_G(x) (((x) >> NUMPORTS_S) & NUMPORTS_M)

#define MPS_INT_CAUSE_A 0x9008
#define MPS_TX_INT_CAUSE_A 0x9408
#define MPS_STAT_CTL_A 0x9600

#define FRMERR_S    15
#define FRMERR_V(x) ((x) << FRMERR_S)
#define FRMERR_F    FRMERR_V(1U)

#define SECNTERR_S    14
#define SECNTERR_V(x) ((x) << SECNTERR_S)
#define SECNTERR_F    SECNTERR_V(1U)

#define BUBBLE_S    13
#define BUBBLE_V(x) ((x) << BUBBLE_S)
#define BUBBLE_F    BUBBLE_V(1U)

#define TXDESCFIFO_S    9
#define TXDESCFIFO_M    0xfU
#define TXDESCFIFO_V(x) ((x) << TXDESCFIFO_S)

#define TXDATAFIFO_S    5
#define TXDATAFIFO_M    0xfU
#define TXDATAFIFO_V(x) ((x) << TXDATAFIFO_S)

#define NCSIFIFO_S    4
#define NCSIFIFO_V(x) ((x) << NCSIFIFO_S)
#define NCSIFIFO_F    NCSIFIFO_V(1U)

#define TPFIFO_S    0
#define TPFIFO_M    0xfU
#define TPFIFO_V(x) ((x) << TPFIFO_S)

#define MPS_STAT_PERR_INT_CAUSE_SRAM_A		0x9614
#define MPS_STAT_PERR_INT_CAUSE_TX_FIFO_A	0x9620
#define MPS_STAT_PERR_INT_CAUSE_RX_FIFO_A	0x962c

#define MPS_STAT_RX_BG_0_MAC_DROP_FRAME_L 0x9640
#define MPS_STAT_RX_BG_0_MAC_DROP_FRAME_H 0x9644
#define MPS_STAT_RX_BG_1_MAC_DROP_FRAME_L 0x9648
#define MPS_STAT_RX_BG_1_MAC_DROP_FRAME_H 0x964c
#define MPS_STAT_RX_BG_2_MAC_DROP_FRAME_L 0x9650
#define MPS_STAT_RX_BG_2_MAC_DROP_FRAME_H 0x9654
#define MPS_STAT_RX_BG_3_MAC_DROP_FRAME_L 0x9658
#define MPS_STAT_RX_BG_3_MAC_DROP_FRAME_H 0x965c
#define MPS_STAT_RX_BG_0_LB_DROP_FRAME_L 0x9660
#define MPS_STAT_RX_BG_0_LB_DROP_FRAME_H 0x9664
#define MPS_STAT_RX_BG_1_LB_DROP_FRAME_L 0x9668
#define MPS_STAT_RX_BG_1_LB_DROP_FRAME_H 0x966c
#define MPS_STAT_RX_BG_2_LB_DROP_FRAME_L 0x9670
#define MPS_STAT_RX_BG_2_LB_DROP_FRAME_H 0x9674
#define MPS_STAT_RX_BG_3_LB_DROP_FRAME_L 0x9678
#define MPS_STAT_RX_BG_3_LB_DROP_FRAME_H 0x967c
#define MPS_STAT_RX_BG_0_MAC_TRUNC_FRAME_L 0x9680
#define MPS_STAT_RX_BG_0_MAC_TRUNC_FRAME_H 0x9684
#define MPS_STAT_RX_BG_1_MAC_TRUNC_FRAME_L 0x9688
#define MPS_STAT_RX_BG_1_MAC_TRUNC_FRAME_H 0x968c
#define MPS_STAT_RX_BG_2_MAC_TRUNC_FRAME_L 0x9690
#define MPS_STAT_RX_BG_2_MAC_TRUNC_FRAME_H 0x9694
#define MPS_STAT_RX_BG_3_MAC_TRUNC_FRAME_L 0x9698
#define MPS_STAT_RX_BG_3_MAC_TRUNC_FRAME_H 0x969c
#define MPS_STAT_RX_BG_0_LB_TRUNC_FRAME_L 0x96a0
#define MPS_STAT_RX_BG_0_LB_TRUNC_FRAME_H 0x96a4
#define MPS_STAT_RX_BG_1_LB_TRUNC_FRAME_L 0x96a8
#define MPS_STAT_RX_BG_1_LB_TRUNC_FRAME_H 0x96ac
#define MPS_STAT_RX_BG_2_LB_TRUNC_FRAME_L 0x96b0
#define MPS_STAT_RX_BG_2_LB_TRUNC_FRAME_H 0x96b4
#define MPS_STAT_RX_BG_3_LB_TRUNC_FRAME_L 0x96b8
#define MPS_STAT_RX_BG_3_LB_TRUNC_FRAME_H 0x96bc

#define MPS_TRC_CFG_A 0x9800

#define TRCFIFOEMPTY_S    4
#define TRCFIFOEMPTY_V(x) ((x) << TRCFIFOEMPTY_S)
#define TRCFIFOEMPTY_F    TRCFIFOEMPTY_V(1U)

#define TRCIGNOREDROPINPUT_S    3
#define TRCIGNOREDROPINPUT_V(x) ((x) << TRCIGNOREDROPINPUT_S)
#define TRCIGNOREDROPINPUT_F    TRCIGNOREDROPINPUT_V(1U)

#define TRCKEEPDUPLICATES_S    2
#define TRCKEEPDUPLICATES_V(x) ((x) << TRCKEEPDUPLICATES_S)
#define TRCKEEPDUPLICATES_F    TRCKEEPDUPLICATES_V(1U)

#define TRCEN_S    1
#define TRCEN_V(x) ((x) << TRCEN_S)
#define TRCEN_F    TRCEN_V(1U)

#define TRCMULTIFILTER_S    0
#define TRCMULTIFILTER_V(x) ((x) << TRCMULTIFILTER_S)
#define TRCMULTIFILTER_F    TRCMULTIFILTER_V(1U)

#define MPS_TRC_RSS_CONTROL_A		0x9808
#define MPS_TRC_FILTER1_RSS_CONTROL_A	0x9ff4
#define MPS_TRC_FILTER2_RSS_CONTROL_A	0x9ffc
#define MPS_TRC_FILTER3_RSS_CONTROL_A	0xa004
#define MPS_T5_TRC_RSS_CONTROL_A	0xa00c

#define RSSCONTROL_S    16
#define RSSCONTROL_V(x) ((x) << RSSCONTROL_S)

#define QUEUENUMBER_S    0
#define QUEUENUMBER_V(x) ((x) << QUEUENUMBER_S)

#define TFINVERTMATCH_S    24
#define TFINVERTMATCH_V(x) ((x) << TFINVERTMATCH_S)
#define TFINVERTMATCH_F    TFINVERTMATCH_V(1U)

#define TFEN_S    22
#define TFEN_V(x) ((x) << TFEN_S)
#define TFEN_F    TFEN_V(1U)

#define TFPORT_S    18
#define TFPORT_M    0xfU
#define TFPORT_V(x) ((x) << TFPORT_S)
#define TFPORT_G(x) (((x) >> TFPORT_S) & TFPORT_M)

#define TFLENGTH_S    8
#define TFLENGTH_M    0x1fU
#define TFLENGTH_V(x) ((x) << TFLENGTH_S)
#define TFLENGTH_G(x) (((x) >> TFLENGTH_S) & TFLENGTH_M)

#define TFOFFSET_S    0
#define TFOFFSET_M    0x1fU
#define TFOFFSET_V(x) ((x) << TFOFFSET_S)
#define TFOFFSET_G(x) (((x) >> TFOFFSET_S) & TFOFFSET_M)

#define T5_TFINVERTMATCH_S    25
#define T5_TFINVERTMATCH_V(x) ((x) << T5_TFINVERTMATCH_S)
#define T5_TFINVERTMATCH_F    T5_TFINVERTMATCH_V(1U)

#define T5_TFEN_S    23
#define T5_TFEN_V(x) ((x) << T5_TFEN_S)
#define T5_TFEN_F    T5_TFEN_V(1U)

#define T5_TFPORT_S    18
#define T5_TFPORT_M    0x1fU
#define T5_TFPORT_V(x) ((x) << T5_TFPORT_S)
#define T5_TFPORT_G(x) (((x) >> T5_TFPORT_S) & T5_TFPORT_M)

#define MPS_TRC_FILTER_MATCH_CTL_A_A 0x9810
#define MPS_TRC_FILTER_MATCH_CTL_B_A 0x9820

#define TFMINPKTSIZE_S    16
#define TFMINPKTSIZE_M    0x1ffU
#define TFMINPKTSIZE_V(x) ((x) << TFMINPKTSIZE_S)
#define TFMINPKTSIZE_G(x) (((x) >> TFMINPKTSIZE_S) & TFMINPKTSIZE_M)

#define TFCAPTUREMAX_S    0
#define TFCAPTUREMAX_M    0x3fffU
#define TFCAPTUREMAX_V(x) ((x) << TFCAPTUREMAX_S)
#define TFCAPTUREMAX_G(x) (((x) >> TFCAPTUREMAX_S) & TFCAPTUREMAX_M)

#define MPS_TRC_FILTER0_MATCH_A 0x9c00
#define MPS_TRC_FILTER0_DONT_CARE_A 0x9c80
#define MPS_TRC_FILTER1_MATCH_A 0x9d00

#define TP_RSS_CONFIG_A 0x7df0

#define TNL4TUPENIPV6_S    31
#define TNL4TUPENIPV6_V(x) ((x) << TNL4TUPENIPV6_S)
#define TNL4TUPENIPV6_F    TNL4TUPENIPV6_V(1U)

#define TNL2TUPENIPV6_S    30
#define TNL2TUPENIPV6_V(x) ((x) << TNL2TUPENIPV6_S)
#define TNL2TUPENIPV6_F    TNL2TUPENIPV6_V(1U)

#define TNL4TUPENIPV4_S    29
#define TNL4TUPENIPV4_V(x) ((x) << TNL4TUPENIPV4_S)
#define TNL4TUPENIPV4_F    TNL4TUPENIPV4_V(1U)

#define TNL2TUPENIPV4_S    28
#define TNL2TUPENIPV4_V(x) ((x) << TNL2TUPENIPV4_S)
#define TNL2TUPENIPV4_F    TNL2TUPENIPV4_V(1U)

#define TNLTCPSEL_S    27
#define TNLTCPSEL_V(x) ((x) << TNLTCPSEL_S)
#define TNLTCPSEL_F    TNLTCPSEL_V(1U)

#define TNLIP6SEL_S    26
#define TNLIP6SEL_V(x) ((x) << TNLIP6SEL_S)
#define TNLIP6SEL_F    TNLIP6SEL_V(1U)

#define TNLVRTSEL_S    25
#define TNLVRTSEL_V(x) ((x) << TNLVRTSEL_S)
#define TNLVRTSEL_F    TNLVRTSEL_V(1U)

#define TNLMAPEN_S    24
#define TNLMAPEN_V(x) ((x) << TNLMAPEN_S)
#define TNLMAPEN_F    TNLMAPEN_V(1U)

#define OFDHASHSAVE_S    19
#define OFDHASHSAVE_V(x) ((x) << OFDHASHSAVE_S)
#define OFDHASHSAVE_F    OFDHASHSAVE_V(1U)

#define OFDVRTSEL_S    18
#define OFDVRTSEL_V(x) ((x) << OFDVRTSEL_S)
#define OFDVRTSEL_F    OFDVRTSEL_V(1U)

#define OFDMAPEN_S    17
#define OFDMAPEN_V(x) ((x) << OFDMAPEN_S)
#define OFDMAPEN_F    OFDMAPEN_V(1U)

#define OFDLKPEN_S    16
#define OFDLKPEN_V(x) ((x) << OFDLKPEN_S)
#define OFDLKPEN_F    OFDLKPEN_V(1U)

#define SYN4TUPENIPV6_S    15
#define SYN4TUPENIPV6_V(x) ((x) << SYN4TUPENIPV6_S)
#define SYN4TUPENIPV6_F    SYN4TUPENIPV6_V(1U)

#define SYN2TUPENIPV6_S    14
#define SYN2TUPENIPV6_V(x) ((x) << SYN2TUPENIPV6_S)
#define SYN2TUPENIPV6_F    SYN2TUPENIPV6_V(1U)

#define SYN4TUPENIPV4_S    13
#define SYN4TUPENIPV4_V(x) ((x) << SYN4TUPENIPV4_S)
#define SYN4TUPENIPV4_F    SYN4TUPENIPV4_V(1U)

#define SYN2TUPENIPV4_S    12
#define SYN2TUPENIPV4_V(x) ((x) << SYN2TUPENIPV4_S)
#define SYN2TUPENIPV4_F    SYN2TUPENIPV4_V(1U)

#define SYNIP6SEL_S    11
#define SYNIP6SEL_V(x) ((x) << SYNIP6SEL_S)
#define SYNIP6SEL_F    SYNIP6SEL_V(1U)

#define SYNVRTSEL_S    10
#define SYNVRTSEL_V(x) ((x) << SYNVRTSEL_S)
#define SYNVRTSEL_F    SYNVRTSEL_V(1U)

#define SYNMAPEN_S    9
#define SYNMAPEN_V(x) ((x) << SYNMAPEN_S)
#define SYNMAPEN_F    SYNMAPEN_V(1U)

#define SYNLKPEN_S    8
#define SYNLKPEN_V(x) ((x) << SYNLKPEN_S)
#define SYNLKPEN_F    SYNLKPEN_V(1U)

#define CHANNELENABLE_S    7
#define CHANNELENABLE_V(x) ((x) << CHANNELENABLE_S)
#define CHANNELENABLE_F    CHANNELENABLE_V(1U)

#define PORTENABLE_S    6
#define PORTENABLE_V(x) ((x) << PORTENABLE_S)
#define PORTENABLE_F    PORTENABLE_V(1U)

#define TNLALLLOOKUP_S    5
#define TNLALLLOOKUP_V(x) ((x) << TNLALLLOOKUP_S)
#define TNLALLLOOKUP_F    TNLALLLOOKUP_V(1U)

#define VIRTENABLE_S    4
#define VIRTENABLE_V(x) ((x) << VIRTENABLE_S)
#define VIRTENABLE_F    VIRTENABLE_V(1U)

#define CONGESTIONENABLE_S    3
#define CONGESTIONENABLE_V(x) ((x) << CONGESTIONENABLE_S)
#define CONGESTIONENABLE_F    CONGESTIONENABLE_V(1U)

#define HASHTOEPLITZ_S    2
#define HASHTOEPLITZ_V(x) ((x) << HASHTOEPLITZ_S)
#define HASHTOEPLITZ_F    HASHTOEPLITZ_V(1U)

#define UDPENABLE_S    1
#define UDPENABLE_V(x) ((x) << UDPENABLE_S)
#define UDPENABLE_F    UDPENABLE_V(1U)

#define DISABLE_S    0
#define DISABLE_V(x) ((x) << DISABLE_S)
#define DISABLE_F    DISABLE_V(1U)

#define TP_RSS_CONFIG_TNL_A 0x7df4

#define MASKSIZE_S    28
#define MASKSIZE_M    0xfU
#define MASKSIZE_V(x) ((x) << MASKSIZE_S)
#define MASKSIZE_G(x) (((x) >> MASKSIZE_S) & MASKSIZE_M)

#define MASKFILTER_S    16
#define MASKFILTER_M    0x7ffU
#define MASKFILTER_V(x) ((x) << MASKFILTER_S)
#define MASKFILTER_G(x) (((x) >> MASKFILTER_S) & MASKFILTER_M)

#define USEWIRECH_S    0
#define USEWIRECH_V(x) ((x) << USEWIRECH_S)
#define USEWIRECH_F    USEWIRECH_V(1U)

#define HASHALL_S    2
#define HASHALL_V(x) ((x) << HASHALL_S)
#define HASHALL_F    HASHALL_V(1U)

#define HASHETH_S    1
#define HASHETH_V(x) ((x) << HASHETH_S)
#define HASHETH_F    HASHETH_V(1U)

#define TP_RSS_CONFIG_OFD_A 0x7df8

#define RRCPLMAPEN_S    20
#define RRCPLMAPEN_V(x) ((x) << RRCPLMAPEN_S)
#define RRCPLMAPEN_F    RRCPLMAPEN_V(1U)

#define RRCPLQUEWIDTH_S    16
#define RRCPLQUEWIDTH_M    0xfU
#define RRCPLQUEWIDTH_V(x) ((x) << RRCPLQUEWIDTH_S)
#define RRCPLQUEWIDTH_G(x) (((x) >> RRCPLQUEWIDTH_S) & RRCPLQUEWIDTH_M)

#define TP_RSS_CONFIG_SYN_A 0x7dfc
#define TP_RSS_CONFIG_VRT_A 0x7e00

#define VFRDRG_S    25
#define VFRDRG_V(x) ((x) << VFRDRG_S)
#define VFRDRG_F    VFRDRG_V(1U)

#define VFRDEN_S    24
#define VFRDEN_V(x) ((x) << VFRDEN_S)
#define VFRDEN_F    VFRDEN_V(1U)

#define VFPERREN_S    23
#define VFPERREN_V(x) ((x) << VFPERREN_S)
#define VFPERREN_F    VFPERREN_V(1U)

#define KEYPERREN_S    22
#define KEYPERREN_V(x) ((x) << KEYPERREN_S)
#define KEYPERREN_F    KEYPERREN_V(1U)

#define DISABLEVLAN_S    21
#define DISABLEVLAN_V(x) ((x) << DISABLEVLAN_S)
#define DISABLEVLAN_F    DISABLEVLAN_V(1U)

#define ENABLEUP0_S    20
#define ENABLEUP0_V(x) ((x) << ENABLEUP0_S)
#define ENABLEUP0_F    ENABLEUP0_V(1U)

#define HASHDELAY_S    16
#define HASHDELAY_M    0xfU
#define HASHDELAY_V(x) ((x) << HASHDELAY_S)
#define HASHDELAY_G(x) (((x) >> HASHDELAY_S) & HASHDELAY_M)

#define VFWRADDR_S    8
#define VFWRADDR_M    0x7fU
#define VFWRADDR_V(x) ((x) << VFWRADDR_S)
#define VFWRADDR_G(x) (((x) >> VFWRADDR_S) & VFWRADDR_M)

#define KEYMODE_S    6
#define KEYMODE_M    0x3U
#define KEYMODE_V(x) ((x) << KEYMODE_S)
#define KEYMODE_G(x) (((x) >> KEYMODE_S) & KEYMODE_M)

#define VFWREN_S    5
#define VFWREN_V(x) ((x) << VFWREN_S)
#define VFWREN_F    VFWREN_V(1U)

#define KEYWREN_S    4
#define KEYWREN_V(x) ((x) << KEYWREN_S)
#define KEYWREN_F    KEYWREN_V(1U)

#define KEYWRADDR_S    0
#define KEYWRADDR_M    0xfU
#define KEYWRADDR_V(x) ((x) << KEYWRADDR_S)
#define KEYWRADDR_G(x) (((x) >> KEYWRADDR_S) & KEYWRADDR_M)

#define KEYWRADDRX_S    30
#define KEYWRADDRX_M    0x3U
#define KEYWRADDRX_V(x) ((x) << KEYWRADDRX_S)
#define KEYWRADDRX_G(x) (((x) >> KEYWRADDRX_S) & KEYWRADDRX_M)

#define KEYEXTEND_S    26
#define KEYEXTEND_V(x) ((x) << KEYEXTEND_S)
#define KEYEXTEND_F    KEYEXTEND_V(1U)

#define LKPIDXSIZE_S    24
#define LKPIDXSIZE_M    0x3U
#define LKPIDXSIZE_V(x) ((x) << LKPIDXSIZE_S)
#define LKPIDXSIZE_G(x) (((x) >> LKPIDXSIZE_S) & LKPIDXSIZE_M)

#define TP_RSS_VFL_CONFIG_A 0x3a
#define TP_RSS_VFH_CONFIG_A 0x3b

#define ENABLEUDPHASH_S    31
#define ENABLEUDPHASH_V(x) ((x) << ENABLEUDPHASH_S)
#define ENABLEUDPHASH_F    ENABLEUDPHASH_V(1U)

#define VFUPEN_S    30
#define VFUPEN_V(x) ((x) << VFUPEN_S)
#define VFUPEN_F    VFUPEN_V(1U)

#define VFVLNEX_S    28
#define VFVLNEX_V(x) ((x) << VFVLNEX_S)
#define VFVLNEX_F    VFVLNEX_V(1U)

#define VFPRTEN_S    27
#define VFPRTEN_V(x) ((x) << VFPRTEN_S)
#define VFPRTEN_F    VFPRTEN_V(1U)

#define VFCHNEN_S    26
#define VFCHNEN_V(x) ((x) << VFCHNEN_S)
#define VFCHNEN_F    VFCHNEN_V(1U)

#define DEFAULTQUEUE_S    16
#define DEFAULTQUEUE_M    0x3ffU
#define DEFAULTQUEUE_G(x) (((x) >> DEFAULTQUEUE_S) & DEFAULTQUEUE_M)

#define VFIP6TWOTUPEN_S    6
#define VFIP6TWOTUPEN_V(x) ((x) << VFIP6TWOTUPEN_S)
#define VFIP6TWOTUPEN_F    VFIP6TWOTUPEN_V(1U)

#define VFIP4FOURTUPEN_S    5
#define VFIP4FOURTUPEN_V(x) ((x) << VFIP4FOURTUPEN_S)
#define VFIP4FOURTUPEN_F    VFIP4FOURTUPEN_V(1U)

#define VFIP4TWOTUPEN_S    4
#define VFIP4TWOTUPEN_V(x) ((x) << VFIP4TWOTUPEN_S)
#define VFIP4TWOTUPEN_F    VFIP4TWOTUPEN_V(1U)

#define KEYINDEX_S    0
#define KEYINDEX_M    0xfU
#define KEYINDEX_G(x) (((x) >> KEYINDEX_S) & KEYINDEX_M)

#define MAPENABLE_S    31
#define MAPENABLE_V(x) ((x) << MAPENABLE_S)
#define MAPENABLE_F    MAPENABLE_V(1U)

#define CHNENABLE_S    30
#define CHNENABLE_V(x) ((x) << CHNENABLE_S)
#define CHNENABLE_F    CHNENABLE_V(1U)

#define LE_DB_DBGI_CONFIG_A 0x19cf0

#define DBGICMDBUSY_S    3
#define DBGICMDBUSY_V(x) ((x) << DBGICMDBUSY_S)
#define DBGICMDBUSY_F    DBGICMDBUSY_V(1U)

#define DBGICMDSTRT_S    2
#define DBGICMDSTRT_V(x) ((x) << DBGICMDSTRT_S)
#define DBGICMDSTRT_F    DBGICMDSTRT_V(1U)

#define DBGICMDMODE_S    0
#define DBGICMDMODE_M    0x3U
#define DBGICMDMODE_V(x) ((x) << DBGICMDMODE_S)

#define LE_DB_DBGI_REQ_TCAM_CMD_A 0x19cf4

#define DBGICMD_S    20
#define DBGICMD_M    0xfU
#define DBGICMD_V(x) ((x) << DBGICMD_S)

#define DBGITID_S    0
#define DBGITID_M    0xfffffU
#define DBGITID_V(x) ((x) << DBGITID_S)

#define LE_DB_DBGI_REQ_DATA_A 0x19d00
#define LE_DB_DBGI_RSP_STATUS_A 0x19d94

#define LE_DB_DBGI_RSP_DATA_A 0x19da0

#define PRTENABLE_S    29
#define PRTENABLE_V(x) ((x) << PRTENABLE_S)
#define PRTENABLE_F    PRTENABLE_V(1U)

#define UDPFOURTUPEN_S    28
#define UDPFOURTUPEN_V(x) ((x) << UDPFOURTUPEN_S)
#define UDPFOURTUPEN_F    UDPFOURTUPEN_V(1U)

#define IP6FOURTUPEN_S    27
#define IP6FOURTUPEN_V(x) ((x) << IP6FOURTUPEN_S)
#define IP6FOURTUPEN_F    IP6FOURTUPEN_V(1U)

#define IP6TWOTUPEN_S    26
#define IP6TWOTUPEN_V(x) ((x) << IP6TWOTUPEN_S)
#define IP6TWOTUPEN_F    IP6TWOTUPEN_V(1U)

#define IP4FOURTUPEN_S    25
#define IP4FOURTUPEN_V(x) ((x) << IP4FOURTUPEN_S)
#define IP4FOURTUPEN_F    IP4FOURTUPEN_V(1U)

#define IP4TWOTUPEN_S    24
#define IP4TWOTUPEN_V(x) ((x) << IP4TWOTUPEN_S)
#define IP4TWOTUPEN_F    IP4TWOTUPEN_V(1U)

#define IVFWIDTH_S    20
#define IVFWIDTH_M    0xfU
#define IVFWIDTH_V(x) ((x) << IVFWIDTH_S)
#define IVFWIDTH_G(x) (((x) >> IVFWIDTH_S) & IVFWIDTH_M)

#define CH1DEFAULTQUEUE_S    10
#define CH1DEFAULTQUEUE_M    0x3ffU
#define CH1DEFAULTQUEUE_V(x) ((x) << CH1DEFAULTQUEUE_S)
#define CH1DEFAULTQUEUE_G(x) (((x) >> CH1DEFAULTQUEUE_S) & CH1DEFAULTQUEUE_M)

#define CH0DEFAULTQUEUE_S    0
#define CH0DEFAULTQUEUE_M    0x3ffU
#define CH0DEFAULTQUEUE_V(x) ((x) << CH0DEFAULTQUEUE_S)
#define CH0DEFAULTQUEUE_G(x) (((x) >> CH0DEFAULTQUEUE_S) & CH0DEFAULTQUEUE_M)

#define VFLKPIDX_S    8
#define VFLKPIDX_M    0xffU
#define VFLKPIDX_G(x) (((x) >> VFLKPIDX_S) & VFLKPIDX_M)

#define T6_VFWRADDR_S    8
#define T6_VFWRADDR_M    0xffU
#define T6_VFWRADDR_V(x) ((x) << T6_VFWRADDR_S)
#define T6_VFWRADDR_G(x) (((x) >> T6_VFWRADDR_S) & T6_VFWRADDR_M)

#define TP_RSS_CONFIG_CNG_A 0x7e04
#define TP_RSS_SECRET_KEY0_A 0x40
#define TP_RSS_PF0_CONFIG_A 0x30
#define TP_RSS_PF_MAP_A 0x38
#define TP_RSS_PF_MSK_A 0x39

#define PF1LKPIDX_S    3

#define PF0LKPIDX_M    0x7U

#define PF1MSKSIZE_S    4
#define PF1MSKSIZE_M    0xfU

#define CHNCOUNT3_S    31
#define CHNCOUNT3_V(x) ((x) << CHNCOUNT3_S)
#define CHNCOUNT3_F    CHNCOUNT3_V(1U)

#define CHNCOUNT2_S    30
#define CHNCOUNT2_V(x) ((x) << CHNCOUNT2_S)
#define CHNCOUNT2_F    CHNCOUNT2_V(1U)

#define CHNCOUNT1_S    29
#define CHNCOUNT1_V(x) ((x) << CHNCOUNT1_S)
#define CHNCOUNT1_F    CHNCOUNT1_V(1U)

#define CHNCOUNT0_S    28
#define CHNCOUNT0_V(x) ((x) << CHNCOUNT0_S)
#define CHNCOUNT0_F    CHNCOUNT0_V(1U)

#define CHNUNDFLOW3_S    27
#define CHNUNDFLOW3_V(x) ((x) << CHNUNDFLOW3_S)
#define CHNUNDFLOW3_F    CHNUNDFLOW3_V(1U)

#define CHNUNDFLOW2_S    26
#define CHNUNDFLOW2_V(x) ((x) << CHNUNDFLOW2_S)
#define CHNUNDFLOW2_F    CHNUNDFLOW2_V(1U)

#define CHNUNDFLOW1_S    25
#define CHNUNDFLOW1_V(x) ((x) << CHNUNDFLOW1_S)
#define CHNUNDFLOW1_F    CHNUNDFLOW1_V(1U)

#define CHNUNDFLOW0_S    24
#define CHNUNDFLOW0_V(x) ((x) << CHNUNDFLOW0_S)
#define CHNUNDFLOW0_F    CHNUNDFLOW0_V(1U)

#define RSTCHN3_S    19
#define RSTCHN3_V(x) ((x) << RSTCHN3_S)
#define RSTCHN3_F    RSTCHN3_V(1U)

#define RSTCHN2_S    18
#define RSTCHN2_V(x) ((x) << RSTCHN2_S)
#define RSTCHN2_F    RSTCHN2_V(1U)

#define RSTCHN1_S    17
#define RSTCHN1_V(x) ((x) << RSTCHN1_S)
#define RSTCHN1_F    RSTCHN1_V(1U)

#define RSTCHN0_S    16
#define RSTCHN0_V(x) ((x) << RSTCHN0_S)
#define RSTCHN0_F    RSTCHN0_V(1U)

#define UPDVLD_S    15
#define UPDVLD_V(x) ((x) << UPDVLD_S)
#define UPDVLD_F    UPDVLD_V(1U)

#define XOFF_S    14
#define XOFF_V(x) ((x) << XOFF_S)
#define XOFF_F    XOFF_V(1U)

#define UPDCHN3_S    13
#define UPDCHN3_V(x) ((x) << UPDCHN3_S)
#define UPDCHN3_F    UPDCHN3_V(1U)

#define UPDCHN2_S    12
#define UPDCHN2_V(x) ((x) << UPDCHN2_S)
#define UPDCHN2_F    UPDCHN2_V(1U)

#define UPDCHN1_S    11
#define UPDCHN1_V(x) ((x) << UPDCHN1_S)
#define UPDCHN1_F    UPDCHN1_V(1U)

#define UPDCHN0_S    10
#define UPDCHN0_V(x) ((x) << UPDCHN0_S)
#define UPDCHN0_F    UPDCHN0_V(1U)

#define QUEUE_S    0
#define QUEUE_M    0x3ffU
#define QUEUE_V(x) ((x) << QUEUE_S)
#define QUEUE_G(x) (((x) >> QUEUE_S) & QUEUE_M)

#define MPS_TRC_INT_CAUSE_A	0x985c

#define MISCPERR_S    8
#define MISCPERR_V(x) ((x) << MISCPERR_S)
#define MISCPERR_F    MISCPERR_V(1U)

#define PKTFIFO_S    4
#define PKTFIFO_M    0xfU
#define PKTFIFO_V(x) ((x) << PKTFIFO_S)

#define FILTMEM_S    0
#define FILTMEM_M    0xfU
#define FILTMEM_V(x) ((x) << FILTMEM_S)

#define MPS_CLS_INT_CAUSE_A 0xd028

#define HASHSRAM_S    2
#define HASHSRAM_V(x) ((x) << HASHSRAM_S)
#define HASHSRAM_F    HASHSRAM_V(1U)

#define MATCHTCAM_S    1
#define MATCHTCAM_V(x) ((x) << MATCHTCAM_S)
#define MATCHTCAM_F    MATCHTCAM_V(1U)

#define MATCHSRAM_S    0
#define MATCHSRAM_V(x) ((x) << MATCHSRAM_S)
#define MATCHSRAM_F    MATCHSRAM_V(1U)

#define MPS_RX_PG_RSV0_A 0x11010
#define MPS_RX_PG_RSV4_A 0x11020
#define MPS_RX_PERR_INT_CAUSE_A 0x11074
#define MPS_RX_MAC_BG_PG_CNT0_A 0x11208
#define MPS_RX_LPBK_BG_PG_CNT0_A 0x11218

#define MPS_CLS_TCAM_Y_L_A 0xf000
#define MPS_CLS_TCAM_DATA0_A 0xf000
#define MPS_CLS_TCAM_DATA1_A 0xf004

#define CTLREQID_S    30
#define CTLREQID_V(x) ((x) << CTLREQID_S)

#define MPS_VF_RPLCT_MAP0_A 0x1111c
#define MPS_VF_RPLCT_MAP1_A 0x11120
#define MPS_VF_RPLCT_MAP2_A 0x11124
#define MPS_VF_RPLCT_MAP3_A 0x11128
#define MPS_VF_RPLCT_MAP4_A 0x11300
#define MPS_VF_RPLCT_MAP5_A 0x11304
#define MPS_VF_RPLCT_MAP6_A 0x11308
#define MPS_VF_RPLCT_MAP7_A 0x1130c

#define VIDL_S    16
#define VIDL_M    0xffffU
#define VIDL_G(x) (((x) >> VIDL_S) & VIDL_M)

#define DATALKPTYPE_S    10
#define DATALKPTYPE_M    0x3U
#define DATALKPTYPE_G(x) (((x) >> DATALKPTYPE_S) & DATALKPTYPE_M)

#define DATAPORTNUM_S    12
#define DATAPORTNUM_M    0xfU
#define DATAPORTNUM_G(x) (((x) >> DATAPORTNUM_S) & DATAPORTNUM_M)

#define DATADIPHIT_S    8
#define DATADIPHIT_V(x) ((x) << DATADIPHIT_S)
#define DATADIPHIT_F    DATADIPHIT_V(1U)

#define DATAVIDH2_S    7
#define DATAVIDH2_V(x) ((x) << DATAVIDH2_S)
#define DATAVIDH2_F    DATAVIDH2_V(1U)

#define DATAVIDH1_S    0
#define DATAVIDH1_M    0x7fU
#define DATAVIDH1_G(x) (((x) >> DATAVIDH1_S) & DATAVIDH1_M)

#define MPS_CLS_TCAM_RDATA0_REQ_ID1_A 0xf020
#define MPS_CLS_TCAM_RDATA1_REQ_ID1_A 0xf024
#define MPS_CLS_TCAM_RDATA2_REQ_ID1_A 0xf028

#define USED_S    16
#define USED_M    0x7ffU
#define USED_G(x) (((x) >> USED_S) & USED_M)

#define ALLOC_S    0
#define ALLOC_M    0x7ffU
#define ALLOC_G(x) (((x) >> ALLOC_S) & ALLOC_M)

#define T5_USED_S    16
#define T5_USED_M    0xfffU
#define T5_USED_G(x) (((x) >> T5_USED_S) & T5_USED_M)

#define T5_ALLOC_S    0
#define T5_ALLOC_M    0xfffU
#define T5_ALLOC_G(x) (((x) >> T5_ALLOC_S) & T5_ALLOC_M)

#define DMACH_S    0
#define DMACH_M    0xffffU
#define DMACH_G(x) (((x) >> DMACH_S) & DMACH_M)

#define MPS_CLS_TCAM_X_L_A 0xf008
#define MPS_CLS_TCAM_DATA2_CTL_A 0xf008

#define CTLCMDTYPE_S    31
#define CTLCMDTYPE_V(x) ((x) << CTLCMDTYPE_S)
#define CTLCMDTYPE_F    CTLCMDTYPE_V(1U)

#define CTLTCAMSEL_S    25
#define CTLTCAMSEL_V(x) ((x) << CTLTCAMSEL_S)

#define CTLTCAMINDEX_S    17
#define CTLTCAMINDEX_V(x) ((x) << CTLTCAMINDEX_S)

#define CTLXYBITSEL_S    16
#define CTLXYBITSEL_V(x) ((x) << CTLXYBITSEL_S)

#define MPS_CLS_TCAM_Y_L(idx) (MPS_CLS_TCAM_Y_L_A + (idx) * 16)
#define NUM_MPS_CLS_TCAM_Y_L_INSTANCES 512

#define MPS_CLS_TCAM_X_L(idx) (MPS_CLS_TCAM_X_L_A + (idx) * 16)
#define NUM_MPS_CLS_TCAM_X_L_INSTANCES 512

#define MPS_CLS_SRAM_L_A 0xe000

#define T6_MULTILISTEN0_S    26

#define T6_SRAM_PRIO3_S    23
#define T6_SRAM_PRIO3_M    0x7U
#define T6_SRAM_PRIO3_G(x) (((x) >> T6_SRAM_PRIO3_S) & T6_SRAM_PRIO3_M)

#define T6_SRAM_PRIO2_S    20
#define T6_SRAM_PRIO2_M    0x7U
#define T6_SRAM_PRIO2_G(x) (((x) >> T6_SRAM_PRIO2_S) & T6_SRAM_PRIO2_M)

#define T6_SRAM_PRIO1_S    17
#define T6_SRAM_PRIO1_M    0x7U
#define T6_SRAM_PRIO1_G(x) (((x) >> T6_SRAM_PRIO1_S) & T6_SRAM_PRIO1_M)

#define T6_SRAM_PRIO0_S    14
#define T6_SRAM_PRIO0_M    0x7U
#define T6_SRAM_PRIO0_G(x) (((x) >> T6_SRAM_PRIO0_S) & T6_SRAM_PRIO0_M)

#define T6_SRAM_VLD_S    13
#define T6_SRAM_VLD_V(x) ((x) << T6_SRAM_VLD_S)
#define T6_SRAM_VLD_F    T6_SRAM_VLD_V(1U)

#define T6_REPLICATE_S    12
#define T6_REPLICATE_V(x) ((x) << T6_REPLICATE_S)
#define T6_REPLICATE_F    T6_REPLICATE_V(1U)

#define T6_PF_S    9
#define T6_PF_M    0x7U
#define T6_PF_G(x) (((x) >> T6_PF_S) & T6_PF_M)

#define T6_VF_VALID_S    8
#define T6_VF_VALID_V(x) ((x) << T6_VF_VALID_S)
#define T6_VF_VALID_F    T6_VF_VALID_V(1U)

#define T6_VF_S    0
#define T6_VF_M    0xffU
#define T6_VF_G(x) (((x) >> T6_VF_S) & T6_VF_M)

#define MPS_CLS_SRAM_H_A 0xe004

#define MPS_CLS_SRAM_L(idx) (MPS_CLS_SRAM_L_A + (idx) * 8)
#define NUM_MPS_CLS_SRAM_L_INSTANCES 336

#define MPS_CLS_SRAM_H(idx) (MPS_CLS_SRAM_H_A + (idx) * 8)
#define NUM_MPS_CLS_SRAM_H_INSTANCES 336

#define MULTILISTEN0_S    25

#define REPLICATE_S    11
#define REPLICATE_V(x) ((x) << REPLICATE_S)
#define REPLICATE_F    REPLICATE_V(1U)

#define PF_S    8
#define PF_M    0x7U
#define PF_G(x) (((x) >> PF_S) & PF_M)

#define VF_VALID_S    7
#define VF_VALID_V(x) ((x) << VF_VALID_S)
#define VF_VALID_F    VF_VALID_V(1U)

#define VF_S    0
#define VF_M    0x7fU
#define VF_G(x) (((x) >> VF_S) & VF_M)

#define SRAM_PRIO3_S    22
#define SRAM_PRIO3_M    0x7U
#define SRAM_PRIO3_G(x) (((x) >> SRAM_PRIO3_S) & SRAM_PRIO3_M)

#define SRAM_PRIO2_S    19
#define SRAM_PRIO2_M    0x7U
#define SRAM_PRIO2_G(x) (((x) >> SRAM_PRIO2_S) & SRAM_PRIO2_M)

#define SRAM_PRIO1_S    16
#define SRAM_PRIO1_M    0x7U
#define SRAM_PRIO1_G(x) (((x) >> SRAM_PRIO1_S) & SRAM_PRIO1_M)

#define SRAM_PRIO0_S    13
#define SRAM_PRIO0_M    0x7U
#define SRAM_PRIO0_G(x) (((x) >> SRAM_PRIO0_S) & SRAM_PRIO0_M)

#define SRAM_VLD_S    12
#define SRAM_VLD_V(x) ((x) << SRAM_VLD_S)
#define SRAM_VLD_F    SRAM_VLD_V(1U)

#define PORTMAP_S    0
#define PORTMAP_M    0xfU
#define PORTMAP_G(x) (((x) >> PORTMAP_S) & PORTMAP_M)

#define CPL_INTR_CAUSE_A 0x19054

#define CIM_OP_MAP_PERR_S    5
#define CIM_OP_MAP_PERR_V(x) ((x) << CIM_OP_MAP_PERR_S)
#define CIM_OP_MAP_PERR_F    CIM_OP_MAP_PERR_V(1U)

#define CIM_OVFL_ERROR_S    4
#define CIM_OVFL_ERROR_V(x) ((x) << CIM_OVFL_ERROR_S)
#define CIM_OVFL_ERROR_F    CIM_OVFL_ERROR_V(1U)

#define TP_FRAMING_ERROR_S    3
#define TP_FRAMING_ERROR_V(x) ((x) << TP_FRAMING_ERROR_S)
#define TP_FRAMING_ERROR_F    TP_FRAMING_ERROR_V(1U)

#define SGE_FRAMING_ERROR_S    2
#define SGE_FRAMING_ERROR_V(x) ((x) << SGE_FRAMING_ERROR_S)
#define SGE_FRAMING_ERROR_F    SGE_FRAMING_ERROR_V(1U)

#define CIM_FRAMING_ERROR_S    1
#define CIM_FRAMING_ERROR_V(x) ((x) << CIM_FRAMING_ERROR_S)
#define CIM_FRAMING_ERROR_F    CIM_FRAMING_ERROR_V(1U)

#define ZERO_SWITCH_ERROR_S    0
#define ZERO_SWITCH_ERROR_V(x) ((x) << ZERO_SWITCH_ERROR_S)
#define ZERO_SWITCH_ERROR_F    ZERO_SWITCH_ERROR_V(1U)

#define SMB_INT_CAUSE_A 0x19090

#define MSTTXFIFOPARINT_S    21
#define MSTTXFIFOPARINT_V(x) ((x) << MSTTXFIFOPARINT_S)
#define MSTTXFIFOPARINT_F    MSTTXFIFOPARINT_V(1U)

#define MSTRXFIFOPARINT_S    20
#define MSTRXFIFOPARINT_V(x) ((x) << MSTRXFIFOPARINT_S)
#define MSTRXFIFOPARINT_F    MSTRXFIFOPARINT_V(1U)

#define SLVFIFOPARINT_S    19
#define SLVFIFOPARINT_V(x) ((x) << SLVFIFOPARINT_S)
#define SLVFIFOPARINT_F    SLVFIFOPARINT_V(1U)

#define ULP_RX_INT_CAUSE_A 0x19158
#define ULP_RX_ISCSI_LLIMIT_A 0x1915c
#define ULP_RX_ISCSI_ULIMIT_A 0x19160
#define ULP_RX_ISCSI_TAGMASK_A 0x19164
#define ULP_RX_ISCSI_PSZ_A 0x19168
#define ULP_RX_TDDP_LLIMIT_A 0x1916c
#define ULP_RX_TDDP_ULIMIT_A 0x19170
#define ULP_RX_STAG_LLIMIT_A 0x1917c
#define ULP_RX_STAG_ULIMIT_A 0x19180
#define ULP_RX_RQ_LLIMIT_A 0x19184
#define ULP_RX_RQ_ULIMIT_A 0x19188
#define ULP_RX_PBL_LLIMIT_A 0x1918c
#define ULP_RX_PBL_ULIMIT_A 0x19190
#define ULP_RX_CTX_BASE_A 0x19194
#define ULP_RX_RQUDP_LLIMIT_A 0x191a4
#define ULP_RX_RQUDP_ULIMIT_A 0x191a8
#define ULP_RX_LA_CTL_A 0x1923c
#define ULP_RX_LA_RDPTR_A 0x19240
#define ULP_RX_LA_RDDATA_A 0x19244
#define ULP_RX_LA_WRPTR_A 0x19248

#define HPZ3_S    24
#define HPZ3_V(x) ((x) << HPZ3_S)

#define HPZ2_S    16
#define HPZ2_V(x) ((x) << HPZ2_S)

#define HPZ1_S    8
#define HPZ1_V(x) ((x) << HPZ1_S)

#define HPZ0_S    0
#define HPZ0_V(x) ((x) << HPZ0_S)

#define ULP_RX_TDDP_PSZ_A 0x19178

/* registers for module SF */
#define SF_DATA_A 0x193f8
#define SF_OP_A 0x193fc

#define SF_BUSY_S    31
#define SF_BUSY_V(x) ((x) << SF_BUSY_S)
#define SF_BUSY_F    SF_BUSY_V(1U)

#define SF_LOCK_S    4
#define SF_LOCK_V(x) ((x) << SF_LOCK_S)
#define SF_LOCK_F    SF_LOCK_V(1U)

#define SF_CONT_S    3
#define SF_CONT_V(x) ((x) << SF_CONT_S)
#define SF_CONT_F    SF_CONT_V(1U)

#define BYTECNT_S    1
#define BYTECNT_V(x) ((x) << BYTECNT_S)

#define OP_S    0
#define OP_V(x) ((x) << OP_S)
#define OP_F    OP_V(1U)

#define PL_PF_INT_CAUSE_A 0x3c0

#define PFSW_S    3
#define PFSW_V(x) ((x) << PFSW_S)
#define PFSW_F    PFSW_V(1U)

#define PFCIM_S    1
#define PFCIM_V(x) ((x) << PFCIM_S)
#define PFCIM_F    PFCIM_V(1U)

#define PL_PF_INT_ENABLE_A 0x3c4
#define PL_PF_CTL_A 0x3c8

#define PL_WHOAMI_A 0x19400

#define SOURCEPF_S    8
#define SOURCEPF_M    0x7U
#define SOURCEPF_G(x) (((x) >> SOURCEPF_S) & SOURCEPF_M)

#define T6_SOURCEPF_S    9
#define T6_SOURCEPF_M    0x7U
#define T6_SOURCEPF_G(x) (((x) >> T6_SOURCEPF_S) & T6_SOURCEPF_M)

#define PL_INT_CAUSE_A 0x1940c

#define ULP_TX_S    27
#define ULP_TX_V(x) ((x) << ULP_TX_S)
#define ULP_TX_F    ULP_TX_V(1U)

#define SGE_S    26
#define SGE_V(x) ((x) << SGE_S)
#define SGE_F    SGE_V(1U)

#define CPL_SWITCH_S    24
#define CPL_SWITCH_V(x) ((x) << CPL_SWITCH_S)
#define CPL_SWITCH_F    CPL_SWITCH_V(1U)

#define ULP_RX_S    23
#define ULP_RX_V(x) ((x) << ULP_RX_S)
#define ULP_RX_F    ULP_RX_V(1U)

#define PM_RX_S    22
#define PM_RX_V(x) ((x) << PM_RX_S)
#define PM_RX_F    PM_RX_V(1U)

#define PM_TX_S    21
#define PM_TX_V(x) ((x) << PM_TX_S)
#define PM_TX_F    PM_TX_V(1U)

#define MA_S    20
#define MA_V(x) ((x) << MA_S)
#define MA_F    MA_V(1U)

#define TP_S    19
#define TP_V(x) ((x) << TP_S)
#define TP_F    TP_V(1U)

#define LE_S    18
#define LE_V(x) ((x) << LE_S)
#define LE_F    LE_V(1U)

#define EDC1_S    17
#define EDC1_V(x) ((x) << EDC1_S)
#define EDC1_F    EDC1_V(1U)

#define EDC0_S    16
#define EDC0_V(x) ((x) << EDC0_S)
#define EDC0_F    EDC0_V(1U)

#define MC_S    15
#define MC_V(x) ((x) << MC_S)
#define MC_F    MC_V(1U)

#define PCIE_S    14
#define PCIE_V(x) ((x) << PCIE_S)
#define PCIE_F    PCIE_V(1U)

#define XGMAC_KR1_S    12
#define XGMAC_KR1_V(x) ((x) << XGMAC_KR1_S)
#define XGMAC_KR1_F    XGMAC_KR1_V(1U)

#define XGMAC_KR0_S    11
#define XGMAC_KR0_V(x) ((x) << XGMAC_KR0_S)
#define XGMAC_KR0_F    XGMAC_KR0_V(1U)

#define XGMAC1_S    10
#define XGMAC1_V(x) ((x) << XGMAC1_S)
#define XGMAC1_F    XGMAC1_V(1U)

#define XGMAC0_S    9
#define XGMAC0_V(x) ((x) << XGMAC0_S)
#define XGMAC0_F    XGMAC0_V(1U)

#define SMB_S    8
#define SMB_V(x) ((x) << SMB_S)
#define SMB_F    SMB_V(1U)

#define SF_S    7
#define SF_V(x) ((x) << SF_S)
#define SF_F    SF_V(1U)

#define PL_S    6
#define PL_V(x) ((x) << PL_S)
#define PL_F    PL_V(1U)

#define NCSI_S    5
#define NCSI_V(x) ((x) << NCSI_S)
#define NCSI_F    NCSI_V(1U)

#define MPS_S    4
#define MPS_V(x) ((x) << MPS_S)
#define MPS_F    MPS_V(1U)

#define CIM_S    0
#define CIM_V(x) ((x) << CIM_S)
#define CIM_F    CIM_V(1U)

#define MC1_S    31
#define MC1_V(x) ((x) << MC1_S)
#define MC1_F    MC1_V(1U)

#define PL_INT_ENABLE_A 0x19410
#define PL_INT_MAP0_A 0x19414
#define PL_RST_A 0x19428

#define PIORST_S    1
#define PIORST_V(x) ((x) << PIORST_S)
#define PIORST_F    PIORST_V(1U)

#define PIORSTMODE_S    0
#define PIORSTMODE_V(x) ((x) << PIORSTMODE_S)
#define PIORSTMODE_F    PIORSTMODE_V(1U)

#define PL_PL_INT_CAUSE_A 0x19430

#define FATALPERR_S    4
#define FATALPERR_V(x) ((x) << FATALPERR_S)
#define FATALPERR_F    FATALPERR_V(1U)

#define PERRVFID_S    0
#define PERRVFID_V(x) ((x) << PERRVFID_S)
#define PERRVFID_F    PERRVFID_V(1U)

#define PL_REV_A 0x1943c

#define REV_S    0
#define REV_M    0xfU
#define REV_V(x) ((x) << REV_S)
#define REV_G(x) (((x) >> REV_S) & REV_M)

#define T6_UNKNOWNCMD_S    3
#define T6_UNKNOWNCMD_V(x) ((x) << T6_UNKNOWNCMD_S)
#define T6_UNKNOWNCMD_F    T6_UNKNOWNCMD_V(1U)

#define T6_LIP0_S    2
#define T6_LIP0_V(x) ((x) << T6_LIP0_S)
#define T6_LIP0_F    T6_LIP0_V(1U)

#define T6_LIPMISS_S    1
#define T6_LIPMISS_V(x) ((x) << T6_LIPMISS_S)
#define T6_LIPMISS_F    T6_LIPMISS_V(1U)

#define LE_DB_CONFIG_A 0x19c04
#define LE_DB_ROUTING_TABLE_INDEX_A 0x19c10
#define LE_DB_ACTIVE_TABLE_START_INDEX_A 0x19c10
#define LE_DB_FILTER_TABLE_INDEX_A 0x19c14
#define LE_DB_SERVER_INDEX_A 0x19c18
#define LE_DB_SRVR_START_INDEX_A 0x19c18
#define LE_DB_CLIP_TABLE_INDEX_A 0x19c1c
#define LE_DB_ACT_CNT_IPV4_A 0x19c20
#define LE_DB_ACT_CNT_IPV6_A 0x19c24
#define LE_DB_HASH_CONFIG_A 0x19c28

#define HASHTIDSIZE_S    16
#define HASHTIDSIZE_M    0x3fU
#define HASHTIDSIZE_G(x) (((x) >> HASHTIDSIZE_S) & HASHTIDSIZE_M)

#define LE_DB_HASH_TID_BASE_A 0x19c30
#define LE_DB_HASH_TBL_BASE_ADDR_A 0x19c30
#define LE_DB_INT_CAUSE_A 0x19c3c
#define LE_DB_TID_HASHBASE_A 0x19df8
#define T6_LE_DB_HASH_TID_BASE_A 0x19df8

#define HASHEN_S    20
#define HASHEN_V(x) ((x) << HASHEN_S)
#define HASHEN_F    HASHEN_V(1U)

#define ASLIPCOMPEN_S    17
#define ASLIPCOMPEN_V(x) ((x) << ASLIPCOMPEN_S)
#define ASLIPCOMPEN_F    ASLIPCOMPEN_V(1U)

#define REQQPARERR_S    16
#define REQQPARERR_V(x) ((x) << REQQPARERR_S)
#define REQQPARERR_F    REQQPARERR_V(1U)

#define UNKNOWNCMD_S    15
#define UNKNOWNCMD_V(x) ((x) << UNKNOWNCMD_S)
#define UNKNOWNCMD_F    UNKNOWNCMD_V(1U)

#define PARITYERR_S    6
#define PARITYERR_V(x) ((x) << PARITYERR_S)
#define PARITYERR_F    PARITYERR_V(1U)

#define LIPMISS_S    5
#define LIPMISS_V(x) ((x) << LIPMISS_S)
#define LIPMISS_F    LIPMISS_V(1U)

#define LIP0_S    4
#define LIP0_V(x) ((x) << LIP0_S)
#define LIP0_F    LIP0_V(1U)

#define BASEADDR_S    3
#define BASEADDR_M    0x1fffffffU
#define BASEADDR_G(x) (((x) >> BASEADDR_S) & BASEADDR_M)

#define TCAMINTPERR_S    13
#define TCAMINTPERR_V(x) ((x) << TCAMINTPERR_S)
#define TCAMINTPERR_F    TCAMINTPERR_V(1U)

#define SSRAMINTPERR_S    10
#define SSRAMINTPERR_V(x) ((x) << SSRAMINTPERR_S)
#define SSRAMINTPERR_F    SSRAMINTPERR_V(1U)

#define LE_DB_RSP_CODE_0_A	0x19c74

#define TCAM_ACTV_HIT_S		0
#define TCAM_ACTV_HIT_M		0x1fU
#define TCAM_ACTV_HIT_V(x)	((x) << TCAM_ACTV_HIT_S)
#define TCAM_ACTV_HIT_G(x)	(((x) >> TCAM_ACTV_HIT_S) & TCAM_ACTV_HIT_M)

#define LE_DB_RSP_CODE_1_A     0x19c78

#define HASH_ACTV_HIT_S		25
#define HASH_ACTV_HIT_M		0x1fU
#define HASH_ACTV_HIT_V(x)	((x) << HASH_ACTV_HIT_S)
#define HASH_ACTV_HIT_G(x)	(((x) >> HASH_ACTV_HIT_S) & HASH_ACTV_HIT_M)

#define LE_3_DB_HASH_MASK_GEN_IPV4_T6_A	0x19eac
#define LE_4_DB_HASH_MASK_GEN_IPV4_T6_A	0x19eb0

#define NCSI_INT_CAUSE_A 0x1a0d8

#define CIM_DM_PRTY_ERR_S    8
#define CIM_DM_PRTY_ERR_V(x) ((x) << CIM_DM_PRTY_ERR_S)
#define CIM_DM_PRTY_ERR_F    CIM_DM_PRTY_ERR_V(1U)

#define MPS_DM_PRTY_ERR_S    7
#define MPS_DM_PRTY_ERR_V(x) ((x) << MPS_DM_PRTY_ERR_S)
#define MPS_DM_PRTY_ERR_F    MPS_DM_PRTY_ERR_V(1U)

#define TXFIFO_PRTY_ERR_S    1
#define TXFIFO_PRTY_ERR_V(x) ((x) << TXFIFO_PRTY_ERR_S)
#define TXFIFO_PRTY_ERR_F    TXFIFO_PRTY_ERR_V(1U)

#define RXFIFO_PRTY_ERR_S    0
#define RXFIFO_PRTY_ERR_V(x) ((x) << RXFIFO_PRTY_ERR_S)
#define RXFIFO_PRTY_ERR_F    RXFIFO_PRTY_ERR_V(1U)

#define XGMAC_PORT_CFG2_A 0x1018

#define PATEN_S    18
#define PATEN_V(x) ((x) << PATEN_S)
#define PATEN_F    PATEN_V(1U)

#define MAGICEN_S    17
#define MAGICEN_V(x) ((x) << MAGICEN_S)
#define MAGICEN_F    MAGICEN_V(1U)

#define XGMAC_PORT_MAGIC_MACID_LO 0x1024
#define XGMAC_PORT_MAGIC_MACID_HI 0x1028

#define XGMAC_PORT_EPIO_DATA0_A 0x10c0
#define XGMAC_PORT_EPIO_DATA1_A 0x10c4
#define XGMAC_PORT_EPIO_DATA2_A 0x10c8
#define XGMAC_PORT_EPIO_DATA3_A 0x10cc
#define XGMAC_PORT_EPIO_OP_A 0x10d0

#define EPIOWR_S    8
#define EPIOWR_V(x) ((x) << EPIOWR_S)
#define EPIOWR_F    EPIOWR_V(1U)

#define ADDRESS_S    0
#define ADDRESS_V(x) ((x) << ADDRESS_S)

#define MAC_PORT_INT_CAUSE_A 0x8dc
#define XGMAC_PORT_INT_CAUSE_A 0x10dc

#define TP_TX_MOD_QUEUE_REQ_MAP_A 0x7e28

#define TP_TX_MOD_QUEUE_WEIGHT0_A 0x7e30
#define TP_TX_MOD_CHANNEL_WEIGHT_A 0x7e34

#define TX_MOD_QUEUE_REQ_MAP_S    0
#define TX_MOD_QUEUE_REQ_MAP_V(x) ((x) << TX_MOD_QUEUE_REQ_MAP_S)

#define TX_MODQ_WEIGHT3_S    24
#define TX_MODQ_WEIGHT3_V(x) ((x) << TX_MODQ_WEIGHT3_S)

#define TX_MODQ_WEIGHT2_S    16
#define TX_MODQ_WEIGHT2_V(x) ((x) << TX_MODQ_WEIGHT2_S)

#define TX_MODQ_WEIGHT1_S    8
#define TX_MODQ_WEIGHT1_V(x) ((x) << TX_MODQ_WEIGHT1_S)

#define TX_MODQ_WEIGHT0_S    0
#define TX_MODQ_WEIGHT0_V(x) ((x) << TX_MODQ_WEIGHT0_S)

#define TP_TX_SCHED_HDR_A 0x23
#define TP_TX_SCHED_FIFO_A 0x24
#define TP_TX_SCHED_PCMD_A 0x25

#define NUM_MPS_CLS_SRAM_L_INSTANCES 336
#define NUM_MPS_T5_CLS_SRAM_L_INSTANCES 512

#define T5_PORT0_BASE 0x30000
#define T5_PORT_STRIDE 0x4000
#define T5_PORT_BASE(idx) (T5_PORT0_BASE + (idx) * T5_PORT_STRIDE)
#define T5_PORT_REG(idx, reg) (T5_PORT_BASE(idx) + (reg))

#define MC_0_BASE_ADDR 0x40000
#define MC_1_BASE_ADDR 0x48000
#define MC_STRIDE (MC_1_BASE_ADDR - MC_0_BASE_ADDR)
#define MC_REG(reg, idx) (reg + MC_STRIDE * idx)

#define MC_P_BIST_CMD_A			0x41400
#define MC_P_BIST_CMD_ADDR_A		0x41404
#define MC_P_BIST_CMD_LEN_A		0x41408
#define MC_P_BIST_DATA_PATTERN_A	0x4140c
#define MC_P_BIST_STATUS_RDATA_A	0x41488

#define EDC_T50_BASE_ADDR		0x50000

#define EDC_H_BIST_CMD_A		0x50004
#define EDC_H_BIST_CMD_ADDR_A		0x50008
#define EDC_H_BIST_CMD_LEN_A		0x5000c
#define EDC_H_BIST_DATA_PATTERN_A	0x50010
#define EDC_H_BIST_STATUS_RDATA_A	0x50028

#define EDC_H_ECC_ERR_ADDR_A		0x50084
#define EDC_T51_BASE_ADDR		0x50800

#define EDC_T5_STRIDE (EDC_T51_BASE_ADDR - EDC_T50_BASE_ADDR)
#define EDC_T5_REG(reg, idx) (reg + EDC_T5_STRIDE * idx)

#define PL_VF_REV_A 0x4
#define PL_VF_WHOAMI_A 0x0
#define PL_VF_REVISION_A 0x8

/* registers for module CIM */
#define CIM_HOST_ACC_CTRL_A	0x7b50
#define CIM_HOST_ACC_DATA_A	0x7b54
#define UP_UP_DBG_LA_CFG_A	0x140
#define UP_UP_DBG_LA_DATA_A	0x144

#define HOSTBUSY_S	17
#define HOSTBUSY_V(x)	((x) << HOSTBUSY_S)
#define HOSTBUSY_F	HOSTBUSY_V(1U)

#define HOSTWRITE_S	16
#define HOSTWRITE_V(x)	((x) << HOSTWRITE_S)
#define HOSTWRITE_F	HOSTWRITE_V(1U)

#define CIM_IBQ_DBG_CFG_A 0x7b60

#define IBQDBGADDR_S    16
#define IBQDBGADDR_M    0xfffU
#define IBQDBGADDR_V(x) ((x) << IBQDBGADDR_S)
#define IBQDBGADDR_G(x) (((x) >> IBQDBGADDR_S) & IBQDBGADDR_M)

#define IBQDBGBUSY_S    1
#define IBQDBGBUSY_V(x) ((x) << IBQDBGBUSY_S)
#define IBQDBGBUSY_F    IBQDBGBUSY_V(1U)

#define IBQDBGEN_S    0
#define IBQDBGEN_V(x) ((x) << IBQDBGEN_S)
#define IBQDBGEN_F    IBQDBGEN_V(1U)

#define CIM_OBQ_DBG_CFG_A 0x7b64

#define OBQDBGADDR_S    16
#define OBQDBGADDR_M    0xfffU
#define OBQDBGADDR_V(x) ((x) << OBQDBGADDR_S)
#define OBQDBGADDR_G(x) (((x) >> OBQDBGADDR_S) & OBQDBGADDR_M)

#define OBQDBGBUSY_S    1
#define OBQDBGBUSY_V(x) ((x) << OBQDBGBUSY_S)
#define OBQDBGBUSY_F    OBQDBGBUSY_V(1U)

#define OBQDBGEN_S    0
#define OBQDBGEN_V(x) ((x) << OBQDBGEN_S)
#define OBQDBGEN_F    OBQDBGEN_V(1U)

#define CIM_IBQ_DBG_DATA_A 0x7b68
#define CIM_OBQ_DBG_DATA_A 0x7b6c
#define CIM_DEBUGCFG_A 0x7b70
#define CIM_DEBUGSTS_A 0x7b74

#define POLADBGRDPTR_S		23
#define POLADBGRDPTR_M		0x1ffU
#define POLADBGRDPTR_V(x)	((x) << POLADBGRDPTR_S)

#define POLADBGWRPTR_S		16
#define POLADBGWRPTR_M		0x1ffU
#define POLADBGWRPTR_G(x)	(((x) >> POLADBGWRPTR_S) & POLADBGWRPTR_M)

#define PILADBGRDPTR_S		14
#define PILADBGRDPTR_M		0x1ffU
#define PILADBGRDPTR_V(x)	((x) << PILADBGRDPTR_S)

#define PILADBGWRPTR_S		0
#define PILADBGWRPTR_M		0x1ffU
#define PILADBGWRPTR_G(x)	(((x) >> PILADBGWRPTR_S) & PILADBGWRPTR_M)

#define LADBGEN_S	12
#define LADBGEN_V(x)	((x) << LADBGEN_S)
#define LADBGEN_F	LADBGEN_V(1U)

#define CIM_PO_LA_DEBUGDATA_A 0x7b78
#define CIM_PI_LA_DEBUGDATA_A 0x7b7c
#define CIM_PO_LA_MADEBUGDATA_A	0x7b80
#define CIM_PI_LA_MADEBUGDATA_A	0x7b84

#define UPDBGLARDEN_S		1
#define UPDBGLARDEN_V(x)	((x) << UPDBGLARDEN_S)
#define UPDBGLARDEN_F		UPDBGLARDEN_V(1U)

#define UPDBGLAEN_S	0
#define UPDBGLAEN_V(x)	((x) << UPDBGLAEN_S)
#define UPDBGLAEN_F	UPDBGLAEN_V(1U)

#define UPDBGLARDPTR_S		2
#define UPDBGLARDPTR_M		0xfffU
#define UPDBGLARDPTR_V(x)	((x) << UPDBGLARDPTR_S)

#define UPDBGLAWRPTR_S    16
#define UPDBGLAWRPTR_M    0xfffU
#define UPDBGLAWRPTR_G(x) (((x) >> UPDBGLAWRPTR_S) & UPDBGLAWRPTR_M)

#define UPDBGLACAPTPCONLY_S	30
#define UPDBGLACAPTPCONLY_V(x)	((x) << UPDBGLACAPTPCONLY_S)
#define UPDBGLACAPTPCONLY_F	UPDBGLACAPTPCONLY_V(1U)

#define CIM_QUEUE_CONFIG_REF_A 0x7b48
#define CIM_QUEUE_CONFIG_CTRL_A 0x7b4c

#define CIMQSIZE_S    24
#define CIMQSIZE_M    0x3fU
#define CIMQSIZE_G(x) (((x) >> CIMQSIZE_S) & CIMQSIZE_M)

#define CIMQBASE_S    16
#define CIMQBASE_M    0x3fU
#define CIMQBASE_G(x) (((x) >> CIMQBASE_S) & CIMQBASE_M)

#define QUEFULLTHRSH_S    0
#define QUEFULLTHRSH_M    0x1ffU
#define QUEFULLTHRSH_G(x) (((x) >> QUEFULLTHRSH_S) & QUEFULLTHRSH_M)

#define UP_IBQ_0_RDADDR_A 0x10
#define UP_IBQ_0_SHADOW_RDADDR_A 0x280
#define UP_OBQ_0_REALADDR_A 0x104
#define UP_OBQ_0_SHADOW_REALADDR_A 0x394

#define IBQRDADDR_S    0
#define IBQRDADDR_M    0x1fffU
#define IBQRDADDR_G(x) (((x) >> IBQRDADDR_S) & IBQRDADDR_M)

#define IBQWRADDR_S    0
#define IBQWRADDR_M    0x1fffU
#define IBQWRADDR_G(x) (((x) >> IBQWRADDR_S) & IBQWRADDR_M)

#define QUERDADDR_S    0
#define QUERDADDR_M    0x7fffU
#define QUERDADDR_G(x) (((x) >> QUERDADDR_S) & QUERDADDR_M)

#define QUEREMFLITS_S    0
#define QUEREMFLITS_M    0x7ffU
#define QUEREMFLITS_G(x) (((x) >> QUEREMFLITS_S) & QUEREMFLITS_M)

#define QUEEOPCNT_S    16
#define QUEEOPCNT_M    0xfffU
#define QUEEOPCNT_G(x) (((x) >> QUEEOPCNT_S) & QUEEOPCNT_M)

#define QUESOPCNT_S    0
#define QUESOPCNT_M    0xfffU
#define QUESOPCNT_G(x) (((x) >> QUESOPCNT_S) & QUESOPCNT_M)

#define OBQSELECT_S    4
#define OBQSELECT_V(x) ((x) << OBQSELECT_S)
#define OBQSELECT_F    OBQSELECT_V(1U)

#define IBQSELECT_S    3
#define IBQSELECT_V(x) ((x) << IBQSELECT_S)
#define IBQSELECT_F    IBQSELECT_V(1U)

#define QUENUMSELECT_S    0
#define QUENUMSELECT_V(x) ((x) << QUENUMSELECT_S)

#endif /* __T4_REGS_H */
