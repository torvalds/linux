/*
 * Copyright(c) 2015 - 2018 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * This file contains all of the code that is specific to the HFI chip
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>

#include "hfi.h"
#include "trace.h"
#include "mad.h"
#include "pio.h"
#include "sdma.h"
#include "eprom.h"
#include "efivar.h"
#include "platform.h"
#include "aspm.h"
#include "affinity.h"
#include "debugfs.h"
#include "fault.h"

uint kdeth_qp;
module_param_named(kdeth_qp, kdeth_qp, uint, S_IRUGO);
MODULE_PARM_DESC(kdeth_qp, "Set the KDETH queue pair prefix");

uint num_vls = HFI1_MAX_VLS_SUPPORTED;
module_param(num_vls, uint, S_IRUGO);
MODULE_PARM_DESC(num_vls, "Set number of Virtual Lanes to use (1-8)");

/*
 * Default time to aggregate two 10K packets from the idle state
 * (timer not running). The timer starts at the end of the first packet,
 * so only the time for one 10K packet and header plus a bit extra is needed.
 * 10 * 1024 + 64 header byte = 10304 byte
 * 10304 byte / 12.5 GB/s = 824.32ns
 */
uint rcv_intr_timeout = (824 + 16); /* 16 is for coalescing interrupt */
module_param(rcv_intr_timeout, uint, S_IRUGO);
MODULE_PARM_DESC(rcv_intr_timeout, "Receive interrupt mitigation timeout in ns");

uint rcv_intr_count = 16; /* same as qib */
module_param(rcv_intr_count, uint, S_IRUGO);
MODULE_PARM_DESC(rcv_intr_count, "Receive interrupt mitigation count");

ushort link_crc_mask = SUPPORTED_CRCS;
module_param(link_crc_mask, ushort, S_IRUGO);
MODULE_PARM_DESC(link_crc_mask, "CRCs to use on the link");

uint loopback;
module_param_named(loopback, loopback, uint, S_IRUGO);
MODULE_PARM_DESC(loopback, "Put into loopback mode (1 = serdes, 3 = external cable");

/* Other driver tunables */
uint rcv_intr_dynamic = 1; /* enable dynamic mode for rcv int mitigation*/
static ushort crc_14b_sideband = 1;
static uint use_flr = 1;
uint quick_linkup; /* skip LNI */

struct flag_table {
	u64 flag;	/* the flag */
	char *str;	/* description string */
	u16 extra;	/* extra information */
	u16 unused0;
	u32 unused1;
};

/* str must be a string constant */
#define FLAG_ENTRY(str, extra, flag) {flag, str, extra}
#define FLAG_ENTRY0(str, flag) {flag, str, 0}

/* Send Error Consequences */
#define SEC_WRITE_DROPPED	0x1
#define SEC_PACKET_DROPPED	0x2
#define SEC_SC_HALTED		0x4	/* per-context only */
#define SEC_SPC_FREEZE		0x8	/* per-HFI only */

#define DEFAULT_KRCVQS		  2
#define MIN_KERNEL_KCTXTS         2
#define FIRST_KERNEL_KCTXT        1

/*
 * RSM instance allocation
 *   0 - Verbs
 *   1 - User Fecn Handling
 *   2 - Vnic
 */
#define RSM_INS_VERBS             0
#define RSM_INS_FECN              1
#define RSM_INS_VNIC              2

/* Bit offset into the GUID which carries HFI id information */
#define GUID_HFI_INDEX_SHIFT     39

/* extract the emulation revision */
#define emulator_rev(dd) ((dd)->irev >> 8)
/* parallel and serial emulation versions are 3 and 4 respectively */
#define is_emulator_p(dd) ((((dd)->irev) & 0xf) == 3)
#define is_emulator_s(dd) ((((dd)->irev) & 0xf) == 4)

/* RSM fields for Verbs */
/* packet type */
#define IB_PACKET_TYPE         2ull
#define QW_SHIFT               6ull
/* QPN[7..1] */
#define QPN_WIDTH              7ull

/* LRH.BTH: QW 0, OFFSET 48 - for match */
#define LRH_BTH_QW             0ull
#define LRH_BTH_BIT_OFFSET     48ull
#define LRH_BTH_OFFSET(off)    ((LRH_BTH_QW << QW_SHIFT) | (off))
#define LRH_BTH_MATCH_OFFSET   LRH_BTH_OFFSET(LRH_BTH_BIT_OFFSET)
#define LRH_BTH_SELECT
#define LRH_BTH_MASK           3ull
#define LRH_BTH_VALUE          2ull

/* LRH.SC[3..0] QW 0, OFFSET 56 - for match */
#define LRH_SC_QW              0ull
#define LRH_SC_BIT_OFFSET      56ull
#define LRH_SC_OFFSET(off)     ((LRH_SC_QW << QW_SHIFT) | (off))
#define LRH_SC_MATCH_OFFSET    LRH_SC_OFFSET(LRH_SC_BIT_OFFSET)
#define LRH_SC_MASK            128ull
#define LRH_SC_VALUE           0ull

/* SC[n..0] QW 0, OFFSET 60 - for select */
#define LRH_SC_SELECT_OFFSET  ((LRH_SC_QW << QW_SHIFT) | (60ull))

/* QPN[m+n:1] QW 1, OFFSET 1 */
#define QPN_SELECT_OFFSET      ((1ull << QW_SHIFT) | (1ull))

/* RSM fields for Vnic */
/* L2_TYPE: QW 0, OFFSET 61 - for match */
#define L2_TYPE_QW             0ull
#define L2_TYPE_BIT_OFFSET     61ull
#define L2_TYPE_OFFSET(off)    ((L2_TYPE_QW << QW_SHIFT) | (off))
#define L2_TYPE_MATCH_OFFSET   L2_TYPE_OFFSET(L2_TYPE_BIT_OFFSET)
#define L2_TYPE_MASK           3ull
#define L2_16B_VALUE           2ull

/* L4_TYPE QW 1, OFFSET 0 - for match */
#define L4_TYPE_QW              1ull
#define L4_TYPE_BIT_OFFSET      0ull
#define L4_TYPE_OFFSET(off)     ((L4_TYPE_QW << QW_SHIFT) | (off))
#define L4_TYPE_MATCH_OFFSET    L4_TYPE_OFFSET(L4_TYPE_BIT_OFFSET)
#define L4_16B_TYPE_MASK        0xFFull
#define L4_16B_ETH_VALUE        0x78ull

/* 16B VESWID - for select */
#define L4_16B_HDR_VESWID_OFFSET  ((2 << QW_SHIFT) | (16ull))
/* 16B ENTROPY - for select */
#define L2_16B_ENTROPY_OFFSET     ((1 << QW_SHIFT) | (32ull))

/* defines to build power on SC2VL table */
#define SC2VL_VAL( \
	num, \
	sc0, sc0val, \
	sc1, sc1val, \
	sc2, sc2val, \
	sc3, sc3val, \
	sc4, sc4val, \
	sc5, sc5val, \
	sc6, sc6val, \
	sc7, sc7val) \
( \
	((u64)(sc0val) << SEND_SC2VLT##num##_SC##sc0##_SHIFT) | \
	((u64)(sc1val) << SEND_SC2VLT##num##_SC##sc1##_SHIFT) | \
	((u64)(sc2val) << SEND_SC2VLT##num##_SC##sc2##_SHIFT) | \
	((u64)(sc3val) << SEND_SC2VLT##num##_SC##sc3##_SHIFT) | \
	((u64)(sc4val) << SEND_SC2VLT##num##_SC##sc4##_SHIFT) | \
	((u64)(sc5val) << SEND_SC2VLT##num##_SC##sc5##_SHIFT) | \
	((u64)(sc6val) << SEND_SC2VLT##num##_SC##sc6##_SHIFT) | \
	((u64)(sc7val) << SEND_SC2VLT##num##_SC##sc7##_SHIFT)   \
)

#define DC_SC_VL_VAL( \
	range, \
	e0, e0val, \
	e1, e1val, \
	e2, e2val, \
	e3, e3val, \
	e4, e4val, \
	e5, e5val, \
	e6, e6val, \
	e7, e7val, \
	e8, e8val, \
	e9, e9val, \
	e10, e10val, \
	e11, e11val, \
	e12, e12val, \
	e13, e13val, \
	e14, e14val, \
	e15, e15val) \
( \
	((u64)(e0val) << DCC_CFG_SC_VL_TABLE_##range##_ENTRY##e0##_SHIFT) | \
	((u64)(e1val) << DCC_CFG_SC_VL_TABLE_##range##_ENTRY##e1##_SHIFT) | \
	((u64)(e2val) << DCC_CFG_SC_VL_TABLE_##range##_ENTRY##e2##_SHIFT) | \
	((u64)(e3val) << DCC_CFG_SC_VL_TABLE_##range##_ENTRY##e3##_SHIFT) | \
	((u64)(e4val) << DCC_CFG_SC_VL_TABLE_##range##_ENTRY##e4##_SHIFT) | \
	((u64)(e5val) << DCC_CFG_SC_VL_TABLE_##range##_ENTRY##e5##_SHIFT) | \
	((u64)(e6val) << DCC_CFG_SC_VL_TABLE_##range##_ENTRY##e6##_SHIFT) | \
	((u64)(e7val) << DCC_CFG_SC_VL_TABLE_##range##_ENTRY##e7##_SHIFT) | \
	((u64)(e8val) << DCC_CFG_SC_VL_TABLE_##range##_ENTRY##e8##_SHIFT) | \
	((u64)(e9val) << DCC_CFG_SC_VL_TABLE_##range##_ENTRY##e9##_SHIFT) | \
	((u64)(e10val) << DCC_CFG_SC_VL_TABLE_##range##_ENTRY##e10##_SHIFT) | \
	((u64)(e11val) << DCC_CFG_SC_VL_TABLE_##range##_ENTRY##e11##_SHIFT) | \
	((u64)(e12val) << DCC_CFG_SC_VL_TABLE_##range##_ENTRY##e12##_SHIFT) | \
	((u64)(e13val) << DCC_CFG_SC_VL_TABLE_##range##_ENTRY##e13##_SHIFT) | \
	((u64)(e14val) << DCC_CFG_SC_VL_TABLE_##range##_ENTRY##e14##_SHIFT) | \
	((u64)(e15val) << DCC_CFG_SC_VL_TABLE_##range##_ENTRY##e15##_SHIFT) \
)

/* all CceStatus sub-block freeze bits */
#define ALL_FROZE (CCE_STATUS_SDMA_FROZE_SMASK \
			| CCE_STATUS_RXE_FROZE_SMASK \
			| CCE_STATUS_TXE_FROZE_SMASK \
			| CCE_STATUS_TXE_PIO_FROZE_SMASK)
/* all CceStatus sub-block TXE pause bits */
#define ALL_TXE_PAUSE (CCE_STATUS_TXE_PIO_PAUSED_SMASK \
			| CCE_STATUS_TXE_PAUSED_SMASK \
			| CCE_STATUS_SDMA_PAUSED_SMASK)
/* all CceStatus sub-block RXE pause bits */
#define ALL_RXE_PAUSE CCE_STATUS_RXE_PAUSED_SMASK

#define CNTR_MAX 0xFFFFFFFFFFFFFFFFULL
#define CNTR_32BIT_MAX 0x00000000FFFFFFFF

/*
 * CCE Error flags.
 */
static struct flag_table cce_err_status_flags[] = {
/* 0*/	FLAG_ENTRY0("CceCsrParityErr",
		CCE_ERR_STATUS_CCE_CSR_PARITY_ERR_SMASK),
/* 1*/	FLAG_ENTRY0("CceCsrReadBadAddrErr",
		CCE_ERR_STATUS_CCE_CSR_READ_BAD_ADDR_ERR_SMASK),
/* 2*/	FLAG_ENTRY0("CceCsrWriteBadAddrErr",
		CCE_ERR_STATUS_CCE_CSR_WRITE_BAD_ADDR_ERR_SMASK),
/* 3*/	FLAG_ENTRY0("CceTrgtAsyncFifoParityErr",
		CCE_ERR_STATUS_CCE_TRGT_ASYNC_FIFO_PARITY_ERR_SMASK),
/* 4*/	FLAG_ENTRY0("CceTrgtAccessErr",
		CCE_ERR_STATUS_CCE_TRGT_ACCESS_ERR_SMASK),
/* 5*/	FLAG_ENTRY0("CceRspdDataParityErr",
		CCE_ERR_STATUS_CCE_RSPD_DATA_PARITY_ERR_SMASK),
/* 6*/	FLAG_ENTRY0("CceCli0AsyncFifoParityErr",
		CCE_ERR_STATUS_CCE_CLI0_ASYNC_FIFO_PARITY_ERR_SMASK),
/* 7*/	FLAG_ENTRY0("CceCsrCfgBusParityErr",
		CCE_ERR_STATUS_CCE_CSR_CFG_BUS_PARITY_ERR_SMASK),
/* 8*/	FLAG_ENTRY0("CceCli2AsyncFifoParityErr",
		CCE_ERR_STATUS_CCE_CLI2_ASYNC_FIFO_PARITY_ERR_SMASK),
/* 9*/	FLAG_ENTRY0("CceCli1AsyncFifoPioCrdtParityErr",
	    CCE_ERR_STATUS_CCE_CLI1_ASYNC_FIFO_PIO_CRDT_PARITY_ERR_SMASK),
/*10*/	FLAG_ENTRY0("CceCli1AsyncFifoPioCrdtParityErr",
	    CCE_ERR_STATUS_CCE_CLI1_ASYNC_FIFO_SDMA_HD_PARITY_ERR_SMASK),
/*11*/	FLAG_ENTRY0("CceCli1AsyncFifoRxdmaParityError",
	    CCE_ERR_STATUS_CCE_CLI1_ASYNC_FIFO_RXDMA_PARITY_ERROR_SMASK),
/*12*/	FLAG_ENTRY0("CceCli1AsyncFifoDbgParityError",
		CCE_ERR_STATUS_CCE_CLI1_ASYNC_FIFO_DBG_PARITY_ERROR_SMASK),
/*13*/	FLAG_ENTRY0("PcicRetryMemCorErr",
		CCE_ERR_STATUS_PCIC_RETRY_MEM_COR_ERR_SMASK),
/*14*/	FLAG_ENTRY0("PcicRetryMemCorErr",
		CCE_ERR_STATUS_PCIC_RETRY_SOT_MEM_COR_ERR_SMASK),
/*15*/	FLAG_ENTRY0("PcicPostHdQCorErr",
		CCE_ERR_STATUS_PCIC_POST_HD_QCOR_ERR_SMASK),
/*16*/	FLAG_ENTRY0("PcicPostHdQCorErr",
		CCE_ERR_STATUS_PCIC_POST_DAT_QCOR_ERR_SMASK),
/*17*/	FLAG_ENTRY0("PcicPostHdQCorErr",
		CCE_ERR_STATUS_PCIC_CPL_HD_QCOR_ERR_SMASK),
/*18*/	FLAG_ENTRY0("PcicCplDatQCorErr",
		CCE_ERR_STATUS_PCIC_CPL_DAT_QCOR_ERR_SMASK),
/*19*/	FLAG_ENTRY0("PcicNPostHQParityErr",
		CCE_ERR_STATUS_PCIC_NPOST_HQ_PARITY_ERR_SMASK),
/*20*/	FLAG_ENTRY0("PcicNPostDatQParityErr",
		CCE_ERR_STATUS_PCIC_NPOST_DAT_QPARITY_ERR_SMASK),
/*21*/	FLAG_ENTRY0("PcicRetryMemUncErr",
		CCE_ERR_STATUS_PCIC_RETRY_MEM_UNC_ERR_SMASK),
/*22*/	FLAG_ENTRY0("PcicRetrySotMemUncErr",
		CCE_ERR_STATUS_PCIC_RETRY_SOT_MEM_UNC_ERR_SMASK),
/*23*/	FLAG_ENTRY0("PcicPostHdQUncErr",
		CCE_ERR_STATUS_PCIC_POST_HD_QUNC_ERR_SMASK),
/*24*/	FLAG_ENTRY0("PcicPostDatQUncErr",
		CCE_ERR_STATUS_PCIC_POST_DAT_QUNC_ERR_SMASK),
/*25*/	FLAG_ENTRY0("PcicCplHdQUncErr",
		CCE_ERR_STATUS_PCIC_CPL_HD_QUNC_ERR_SMASK),
/*26*/	FLAG_ENTRY0("PcicCplDatQUncErr",
		CCE_ERR_STATUS_PCIC_CPL_DAT_QUNC_ERR_SMASK),
/*27*/	FLAG_ENTRY0("PcicTransmitFrontParityErr",
		CCE_ERR_STATUS_PCIC_TRANSMIT_FRONT_PARITY_ERR_SMASK),
/*28*/	FLAG_ENTRY0("PcicTransmitBackParityErr",
		CCE_ERR_STATUS_PCIC_TRANSMIT_BACK_PARITY_ERR_SMASK),
/*29*/	FLAG_ENTRY0("PcicReceiveParityErr",
		CCE_ERR_STATUS_PCIC_RECEIVE_PARITY_ERR_SMASK),
/*30*/	FLAG_ENTRY0("CceTrgtCplTimeoutErr",
		CCE_ERR_STATUS_CCE_TRGT_CPL_TIMEOUT_ERR_SMASK),
/*31*/	FLAG_ENTRY0("LATriggered",
		CCE_ERR_STATUS_LA_TRIGGERED_SMASK),
/*32*/	FLAG_ENTRY0("CceSegReadBadAddrErr",
		CCE_ERR_STATUS_CCE_SEG_READ_BAD_ADDR_ERR_SMASK),
/*33*/	FLAG_ENTRY0("CceSegWriteBadAddrErr",
		CCE_ERR_STATUS_CCE_SEG_WRITE_BAD_ADDR_ERR_SMASK),
/*34*/	FLAG_ENTRY0("CceRcplAsyncFifoParityErr",
		CCE_ERR_STATUS_CCE_RCPL_ASYNC_FIFO_PARITY_ERR_SMASK),
/*35*/	FLAG_ENTRY0("CceRxdmaConvFifoParityErr",
		CCE_ERR_STATUS_CCE_RXDMA_CONV_FIFO_PARITY_ERR_SMASK),
/*36*/	FLAG_ENTRY0("CceMsixTableCorErr",
		CCE_ERR_STATUS_CCE_MSIX_TABLE_COR_ERR_SMASK),
/*37*/	FLAG_ENTRY0("CceMsixTableUncErr",
		CCE_ERR_STATUS_CCE_MSIX_TABLE_UNC_ERR_SMASK),
/*38*/	FLAG_ENTRY0("CceIntMapCorErr",
		CCE_ERR_STATUS_CCE_INT_MAP_COR_ERR_SMASK),
/*39*/	FLAG_ENTRY0("CceIntMapUncErr",
		CCE_ERR_STATUS_CCE_INT_MAP_UNC_ERR_SMASK),
/*40*/	FLAG_ENTRY0("CceMsixCsrParityErr",
		CCE_ERR_STATUS_CCE_MSIX_CSR_PARITY_ERR_SMASK),
/*41-63 reserved*/
};

/*
 * Misc Error flags
 */
#define MES(text) MISC_ERR_STATUS_MISC_##text##_ERR_SMASK
static struct flag_table misc_err_status_flags[] = {
/* 0*/	FLAG_ENTRY0("CSR_PARITY", MES(CSR_PARITY)),
/* 1*/	FLAG_ENTRY0("CSR_READ_BAD_ADDR", MES(CSR_READ_BAD_ADDR)),
/* 2*/	FLAG_ENTRY0("CSR_WRITE_BAD_ADDR", MES(CSR_WRITE_BAD_ADDR)),
/* 3*/	FLAG_ENTRY0("SBUS_WRITE_FAILED", MES(SBUS_WRITE_FAILED)),
/* 4*/	FLAG_ENTRY0("KEY_MISMATCH", MES(KEY_MISMATCH)),
/* 5*/	FLAG_ENTRY0("FW_AUTH_FAILED", MES(FW_AUTH_FAILED)),
/* 6*/	FLAG_ENTRY0("EFUSE_CSR_PARITY", MES(EFUSE_CSR_PARITY)),
/* 7*/	FLAG_ENTRY0("EFUSE_READ_BAD_ADDR", MES(EFUSE_READ_BAD_ADDR)),
/* 8*/	FLAG_ENTRY0("EFUSE_WRITE", MES(EFUSE_WRITE)),
/* 9*/	FLAG_ENTRY0("EFUSE_DONE_PARITY", MES(EFUSE_DONE_PARITY)),
/*10*/	FLAG_ENTRY0("INVALID_EEP_CMD", MES(INVALID_EEP_CMD)),
/*11*/	FLAG_ENTRY0("MBIST_FAIL", MES(MBIST_FAIL)),
/*12*/	FLAG_ENTRY0("PLL_LOCK_FAIL", MES(PLL_LOCK_FAIL))
};

/*
 * TXE PIO Error flags and consequences
 */
static struct flag_table pio_err_status_flags[] = {
/* 0*/	FLAG_ENTRY("PioWriteBadCtxt",
	SEC_WRITE_DROPPED,
	SEND_PIO_ERR_STATUS_PIO_WRITE_BAD_CTXT_ERR_SMASK),
/* 1*/	FLAG_ENTRY("PioWriteAddrParity",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_WRITE_ADDR_PARITY_ERR_SMASK),
/* 2*/	FLAG_ENTRY("PioCsrParity",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_CSR_PARITY_ERR_SMASK),
/* 3*/	FLAG_ENTRY("PioSbMemFifo0",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_SB_MEM_FIFO0_ERR_SMASK),
/* 4*/	FLAG_ENTRY("PioSbMemFifo1",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_SB_MEM_FIFO1_ERR_SMASK),
/* 5*/	FLAG_ENTRY("PioPccFifoParity",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_PCC_FIFO_PARITY_ERR_SMASK),
/* 6*/	FLAG_ENTRY("PioPecFifoParity",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_PEC_FIFO_PARITY_ERR_SMASK),
/* 7*/	FLAG_ENTRY("PioSbrdctlCrrelParity",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_SBRDCTL_CRREL_PARITY_ERR_SMASK),
/* 8*/	FLAG_ENTRY("PioSbrdctrlCrrelFifoParity",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_SBRDCTRL_CRREL_FIFO_PARITY_ERR_SMASK),
/* 9*/	FLAG_ENTRY("PioPktEvictFifoParityErr",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_PKT_EVICT_FIFO_PARITY_ERR_SMASK),
/*10*/	FLAG_ENTRY("PioSmPktResetParity",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_SM_PKT_RESET_PARITY_ERR_SMASK),
/*11*/	FLAG_ENTRY("PioVlLenMemBank0Unc",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_VL_LEN_MEM_BANK0_UNC_ERR_SMASK),
/*12*/	FLAG_ENTRY("PioVlLenMemBank1Unc",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_VL_LEN_MEM_BANK1_UNC_ERR_SMASK),
/*13*/	FLAG_ENTRY("PioVlLenMemBank0Cor",
	0,
	SEND_PIO_ERR_STATUS_PIO_VL_LEN_MEM_BANK0_COR_ERR_SMASK),
/*14*/	FLAG_ENTRY("PioVlLenMemBank1Cor",
	0,
	SEND_PIO_ERR_STATUS_PIO_VL_LEN_MEM_BANK1_COR_ERR_SMASK),
/*15*/	FLAG_ENTRY("PioCreditRetFifoParity",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_CREDIT_RET_FIFO_PARITY_ERR_SMASK),
/*16*/	FLAG_ENTRY("PioPpmcPblFifo",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_PPMC_PBL_FIFO_ERR_SMASK),
/*17*/	FLAG_ENTRY("PioInitSmIn",
	0,
	SEND_PIO_ERR_STATUS_PIO_INIT_SM_IN_ERR_SMASK),
/*18*/	FLAG_ENTRY("PioPktEvictSmOrArbSm",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_PKT_EVICT_SM_OR_ARB_SM_ERR_SMASK),
/*19*/	FLAG_ENTRY("PioHostAddrMemUnc",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_HOST_ADDR_MEM_UNC_ERR_SMASK),
/*20*/	FLAG_ENTRY("PioHostAddrMemCor",
	0,
	SEND_PIO_ERR_STATUS_PIO_HOST_ADDR_MEM_COR_ERR_SMASK),
/*21*/	FLAG_ENTRY("PioWriteDataParity",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_WRITE_DATA_PARITY_ERR_SMASK),
/*22*/	FLAG_ENTRY("PioStateMachine",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_STATE_MACHINE_ERR_SMASK),
/*23*/	FLAG_ENTRY("PioWriteQwValidParity",
	SEC_WRITE_DROPPED | SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_WRITE_QW_VALID_PARITY_ERR_SMASK),
/*24*/	FLAG_ENTRY("PioBlockQwCountParity",
	SEC_WRITE_DROPPED | SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_BLOCK_QW_COUNT_PARITY_ERR_SMASK),
/*25*/	FLAG_ENTRY("PioVlfVlLenParity",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_VLF_VL_LEN_PARITY_ERR_SMASK),
/*26*/	FLAG_ENTRY("PioVlfSopParity",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_VLF_SOP_PARITY_ERR_SMASK),
/*27*/	FLAG_ENTRY("PioVlFifoParity",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_VL_FIFO_PARITY_ERR_SMASK),
/*28*/	FLAG_ENTRY("PioPpmcBqcMemParity",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_PPMC_BQC_MEM_PARITY_ERR_SMASK),
/*29*/	FLAG_ENTRY("PioPpmcSopLen",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_PPMC_SOP_LEN_ERR_SMASK),
/*30-31 reserved*/
/*32*/	FLAG_ENTRY("PioCurrentFreeCntParity",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_CURRENT_FREE_CNT_PARITY_ERR_SMASK),
/*33*/	FLAG_ENTRY("PioLastReturnedCntParity",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_LAST_RETURNED_CNT_PARITY_ERR_SMASK),
/*34*/	FLAG_ENTRY("PioPccSopHeadParity",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_PCC_SOP_HEAD_PARITY_ERR_SMASK),
/*35*/	FLAG_ENTRY("PioPecSopHeadParityErr",
	SEC_SPC_FREEZE,
	SEND_PIO_ERR_STATUS_PIO_PEC_SOP_HEAD_PARITY_ERR_SMASK),
/*36-63 reserved*/
};

/* TXE PIO errors that cause an SPC freeze */
#define ALL_PIO_FREEZE_ERR \
	(SEND_PIO_ERR_STATUS_PIO_WRITE_ADDR_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_CSR_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_SB_MEM_FIFO0_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_SB_MEM_FIFO1_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_PCC_FIFO_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_PEC_FIFO_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_SBRDCTL_CRREL_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_SBRDCTRL_CRREL_FIFO_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_PKT_EVICT_FIFO_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_SM_PKT_RESET_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_VL_LEN_MEM_BANK0_UNC_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_VL_LEN_MEM_BANK1_UNC_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_CREDIT_RET_FIFO_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_PPMC_PBL_FIFO_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_PKT_EVICT_SM_OR_ARB_SM_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_HOST_ADDR_MEM_UNC_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_WRITE_DATA_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_STATE_MACHINE_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_WRITE_QW_VALID_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_BLOCK_QW_COUNT_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_VLF_VL_LEN_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_VLF_SOP_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_VL_FIFO_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_PPMC_BQC_MEM_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_PPMC_SOP_LEN_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_CURRENT_FREE_CNT_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_LAST_RETURNED_CNT_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_PCC_SOP_HEAD_PARITY_ERR_SMASK \
	| SEND_PIO_ERR_STATUS_PIO_PEC_SOP_HEAD_PARITY_ERR_SMASK)

/*
 * TXE SDMA Error flags
 */
static struct flag_table sdma_err_status_flags[] = {
/* 0*/	FLAG_ENTRY0("SDmaRpyTagErr",
		SEND_DMA_ERR_STATUS_SDMA_RPY_TAG_ERR_SMASK),
/* 1*/	FLAG_ENTRY0("SDmaCsrParityErr",
		SEND_DMA_ERR_STATUS_SDMA_CSR_PARITY_ERR_SMASK),
/* 2*/	FLAG_ENTRY0("SDmaPcieReqTrackingUncErr",
		SEND_DMA_ERR_STATUS_SDMA_PCIE_REQ_TRACKING_UNC_ERR_SMASK),
/* 3*/	FLAG_ENTRY0("SDmaPcieReqTrackingCorErr",
		SEND_DMA_ERR_STATUS_SDMA_PCIE_REQ_TRACKING_COR_ERR_SMASK),
/*04-63 reserved*/
};

/* TXE SDMA errors that cause an SPC freeze */
#define ALL_SDMA_FREEZE_ERR  \
		(SEND_DMA_ERR_STATUS_SDMA_RPY_TAG_ERR_SMASK \
		| SEND_DMA_ERR_STATUS_SDMA_CSR_PARITY_ERR_SMASK \
		| SEND_DMA_ERR_STATUS_SDMA_PCIE_REQ_TRACKING_UNC_ERR_SMASK)

/* SendEgressErrInfo bits that correspond to a PortXmitDiscard counter */
#define PORT_DISCARD_EGRESS_ERRS \
	(SEND_EGRESS_ERR_INFO_TOO_LONG_IB_PACKET_ERR_SMASK \
	| SEND_EGRESS_ERR_INFO_VL_MAPPING_ERR_SMASK \
	| SEND_EGRESS_ERR_INFO_VL_ERR_SMASK)

/*
 * TXE Egress Error flags
 */
#define SEES(text) SEND_EGRESS_ERR_STATUS_##text##_ERR_SMASK
static struct flag_table egress_err_status_flags[] = {
/* 0*/	FLAG_ENTRY0("TxPktIntegrityMemCorErr", SEES(TX_PKT_INTEGRITY_MEM_COR)),
/* 1*/	FLAG_ENTRY0("TxPktIntegrityMemUncErr", SEES(TX_PKT_INTEGRITY_MEM_UNC)),
/* 2 reserved */
/* 3*/	FLAG_ENTRY0("TxEgressFifoUnderrunOrParityErr",
		SEES(TX_EGRESS_FIFO_UNDERRUN_OR_PARITY)),
/* 4*/	FLAG_ENTRY0("TxLinkdownErr", SEES(TX_LINKDOWN)),
/* 5*/	FLAG_ENTRY0("TxIncorrectLinkStateErr", SEES(TX_INCORRECT_LINK_STATE)),
/* 6 reserved */
/* 7*/	FLAG_ENTRY0("TxPioLaunchIntfParityErr",
		SEES(TX_PIO_LAUNCH_INTF_PARITY)),
/* 8*/	FLAG_ENTRY0("TxSdmaLaunchIntfParityErr",
		SEES(TX_SDMA_LAUNCH_INTF_PARITY)),
/* 9-10 reserved */
/*11*/	FLAG_ENTRY0("TxSbrdCtlStateMachineParityErr",
		SEES(TX_SBRD_CTL_STATE_MACHINE_PARITY)),
/*12*/	FLAG_ENTRY0("TxIllegalVLErr", SEES(TX_ILLEGAL_VL)),
/*13*/	FLAG_ENTRY0("TxLaunchCsrParityErr", SEES(TX_LAUNCH_CSR_PARITY)),
/*14*/	FLAG_ENTRY0("TxSbrdCtlCsrParityErr", SEES(TX_SBRD_CTL_CSR_PARITY)),
/*15*/	FLAG_ENTRY0("TxConfigParityErr", SEES(TX_CONFIG_PARITY)),
/*16*/	FLAG_ENTRY0("TxSdma0DisallowedPacketErr",
		SEES(TX_SDMA0_DISALLOWED_PACKET)),
/*17*/	FLAG_ENTRY0("TxSdma1DisallowedPacketErr",
		SEES(TX_SDMA1_DISALLOWED_PACKET)),
/*18*/	FLAG_ENTRY0("TxSdma2DisallowedPacketErr",
		SEES(TX_SDMA2_DISALLOWED_PACKET)),
/*19*/	FLAG_ENTRY0("TxSdma3DisallowedPacketErr",
		SEES(TX_SDMA3_DISALLOWED_PACKET)),
/*20*/	FLAG_ENTRY0("TxSdma4DisallowedPacketErr",
		SEES(TX_SDMA4_DISALLOWED_PACKET)),
/*21*/	FLAG_ENTRY0("TxSdma5DisallowedPacketErr",
		SEES(TX_SDMA5_DISALLOWED_PACKET)),
/*22*/	FLAG_ENTRY0("TxSdma6DisallowedPacketErr",
		SEES(TX_SDMA6_DISALLOWED_PACKET)),
/*23*/	FLAG_ENTRY0("TxSdma7DisallowedPacketErr",
		SEES(TX_SDMA7_DISALLOWED_PACKET)),
/*24*/	FLAG_ENTRY0("TxSdma8DisallowedPacketErr",
		SEES(TX_SDMA8_DISALLOWED_PACKET)),
/*25*/	FLAG_ENTRY0("TxSdma9DisallowedPacketErr",
		SEES(TX_SDMA9_DISALLOWED_PACKET)),
/*26*/	FLAG_ENTRY0("TxSdma10DisallowedPacketErr",
		SEES(TX_SDMA10_DISALLOWED_PACKET)),
/*27*/	FLAG_ENTRY0("TxSdma11DisallowedPacketErr",
		SEES(TX_SDMA11_DISALLOWED_PACKET)),
/*28*/	FLAG_ENTRY0("TxSdma12DisallowedPacketErr",
		SEES(TX_SDMA12_DISALLOWED_PACKET)),
/*29*/	FLAG_ENTRY0("TxSdma13DisallowedPacketErr",
		SEES(TX_SDMA13_DISALLOWED_PACKET)),
/*30*/	FLAG_ENTRY0("TxSdma14DisallowedPacketErr",
		SEES(TX_SDMA14_DISALLOWED_PACKET)),
/*31*/	FLAG_ENTRY0("TxSdma15DisallowedPacketErr",
		SEES(TX_SDMA15_DISALLOWED_PACKET)),
/*32*/	FLAG_ENTRY0("TxLaunchFifo0UncOrParityErr",
		SEES(TX_LAUNCH_FIFO0_UNC_OR_PARITY)),
/*33*/	FLAG_ENTRY0("TxLaunchFifo1UncOrParityErr",
		SEES(TX_LAUNCH_FIFO1_UNC_OR_PARITY)),
/*34*/	FLAG_ENTRY0("TxLaunchFifo2UncOrParityErr",
		SEES(TX_LAUNCH_FIFO2_UNC_OR_PARITY)),
/*35*/	FLAG_ENTRY0("TxLaunchFifo3UncOrParityErr",
		SEES(TX_LAUNCH_FIFO3_UNC_OR_PARITY)),
/*36*/	FLAG_ENTRY0("TxLaunchFifo4UncOrParityErr",
		SEES(TX_LAUNCH_FIFO4_UNC_OR_PARITY)),
/*37*/	FLAG_ENTRY0("TxLaunchFifo5UncOrParityErr",
		SEES(TX_LAUNCH_FIFO5_UNC_OR_PARITY)),
/*38*/	FLAG_ENTRY0("TxLaunchFifo6UncOrParityErr",
		SEES(TX_LAUNCH_FIFO6_UNC_OR_PARITY)),
/*39*/	FLAG_ENTRY0("TxLaunchFifo7UncOrParityErr",
		SEES(TX_LAUNCH_FIFO7_UNC_OR_PARITY)),
/*40*/	FLAG_ENTRY0("TxLaunchFifo8UncOrParityErr",
		SEES(TX_LAUNCH_FIFO8_UNC_OR_PARITY)),
/*41*/	FLAG_ENTRY0("TxCreditReturnParityErr", SEES(TX_CREDIT_RETURN_PARITY)),
/*42*/	FLAG_ENTRY0("TxSbHdrUncErr", SEES(TX_SB_HDR_UNC)),
/*43*/	FLAG_ENTRY0("TxReadSdmaMemoryUncErr", SEES(TX_READ_SDMA_MEMORY_UNC)),
/*44*/	FLAG_ENTRY0("TxReadPioMemoryUncErr", SEES(TX_READ_PIO_MEMORY_UNC)),
/*45*/	FLAG_ENTRY0("TxEgressFifoUncErr", SEES(TX_EGRESS_FIFO_UNC)),
/*46*/	FLAG_ENTRY0("TxHcrcInsertionErr", SEES(TX_HCRC_INSERTION)),
/*47*/	FLAG_ENTRY0("TxCreditReturnVLErr", SEES(TX_CREDIT_RETURN_VL)),
/*48*/	FLAG_ENTRY0("TxLaunchFifo0CorErr", SEES(TX_LAUNCH_FIFO0_COR)),
/*49*/	FLAG_ENTRY0("TxLaunchFifo1CorErr", SEES(TX_LAUNCH_FIFO1_COR)),
/*50*/	FLAG_ENTRY0("TxLaunchFifo2CorErr", SEES(TX_LAUNCH_FIFO2_COR)),
/*51*/	FLAG_ENTRY0("TxLaunchFifo3CorErr", SEES(TX_LAUNCH_FIFO3_COR)),
/*52*/	FLAG_ENTRY0("TxLaunchFifo4CorErr", SEES(TX_LAUNCH_FIFO4_COR)),
/*53*/	FLAG_ENTRY0("TxLaunchFifo5CorErr", SEES(TX_LAUNCH_FIFO5_COR)),
/*54*/	FLAG_ENTRY0("TxLaunchFifo6CorErr", SEES(TX_LAUNCH_FIFO6_COR)),
/*55*/	FLAG_ENTRY0("TxLaunchFifo7CorErr", SEES(TX_LAUNCH_FIFO7_COR)),
/*56*/	FLAG_ENTRY0("TxLaunchFifo8CorErr", SEES(TX_LAUNCH_FIFO8_COR)),
/*57*/	FLAG_ENTRY0("TxCreditOverrunErr", SEES(TX_CREDIT_OVERRUN)),
/*58*/	FLAG_ENTRY0("TxSbHdrCorErr", SEES(TX_SB_HDR_COR)),
/*59*/	FLAG_ENTRY0("TxReadSdmaMemoryCorErr", SEES(TX_READ_SDMA_MEMORY_COR)),
/*60*/	FLAG_ENTRY0("TxReadPioMemoryCorErr", SEES(TX_READ_PIO_MEMORY_COR)),
/*61*/	FLAG_ENTRY0("TxEgressFifoCorErr", SEES(TX_EGRESS_FIFO_COR)),
/*62*/	FLAG_ENTRY0("TxReadSdmaMemoryCsrUncErr",
		SEES(TX_READ_SDMA_MEMORY_CSR_UNC)),
/*63*/	FLAG_ENTRY0("TxReadPioMemoryCsrUncErr",
		SEES(TX_READ_PIO_MEMORY_CSR_UNC)),
};

/*
 * TXE Egress Error Info flags
 */
#define SEEI(text) SEND_EGRESS_ERR_INFO_##text##_ERR_SMASK
static struct flag_table egress_err_info_flags[] = {
/* 0*/	FLAG_ENTRY0("Reserved", 0ull),
/* 1*/	FLAG_ENTRY0("VLErr", SEEI(VL)),
/* 2*/	FLAG_ENTRY0("JobKeyErr", SEEI(JOB_KEY)),
/* 3*/	FLAG_ENTRY0("JobKeyErr", SEEI(JOB_KEY)),
/* 4*/	FLAG_ENTRY0("PartitionKeyErr", SEEI(PARTITION_KEY)),
/* 5*/	FLAG_ENTRY0("SLIDErr", SEEI(SLID)),
/* 6*/	FLAG_ENTRY0("OpcodeErr", SEEI(OPCODE)),
/* 7*/	FLAG_ENTRY0("VLMappingErr", SEEI(VL_MAPPING)),
/* 8*/	FLAG_ENTRY0("RawErr", SEEI(RAW)),
/* 9*/	FLAG_ENTRY0("RawIPv6Err", SEEI(RAW_IPV6)),
/*10*/	FLAG_ENTRY0("GRHErr", SEEI(GRH)),
/*11*/	FLAG_ENTRY0("BypassErr", SEEI(BYPASS)),
/*12*/	FLAG_ENTRY0("KDETHPacketsErr", SEEI(KDETH_PACKETS)),
/*13*/	FLAG_ENTRY0("NonKDETHPacketsErr", SEEI(NON_KDETH_PACKETS)),
/*14*/	FLAG_ENTRY0("TooSmallIBPacketsErr", SEEI(TOO_SMALL_IB_PACKETS)),
/*15*/	FLAG_ENTRY0("TooSmallBypassPacketsErr", SEEI(TOO_SMALL_BYPASS_PACKETS)),
/*16*/	FLAG_ENTRY0("PbcTestErr", SEEI(PBC_TEST)),
/*17*/	FLAG_ENTRY0("BadPktLenErr", SEEI(BAD_PKT_LEN)),
/*18*/	FLAG_ENTRY0("TooLongIBPacketErr", SEEI(TOO_LONG_IB_PACKET)),
/*19*/	FLAG_ENTRY0("TooLongBypassPacketsErr", SEEI(TOO_LONG_BYPASS_PACKETS)),
/*20*/	FLAG_ENTRY0("PbcStaticRateControlErr", SEEI(PBC_STATIC_RATE_CONTROL)),
/*21*/	FLAG_ENTRY0("BypassBadPktLenErr", SEEI(BAD_PKT_LEN)),
};

/* TXE Egress errors that cause an SPC freeze */
#define ALL_TXE_EGRESS_FREEZE_ERR \
	(SEES(TX_EGRESS_FIFO_UNDERRUN_OR_PARITY) \
	| SEES(TX_PIO_LAUNCH_INTF_PARITY) \
	| SEES(TX_SDMA_LAUNCH_INTF_PARITY) \
	| SEES(TX_SBRD_CTL_STATE_MACHINE_PARITY) \
	| SEES(TX_LAUNCH_CSR_PARITY) \
	| SEES(TX_SBRD_CTL_CSR_PARITY) \
	| SEES(TX_CONFIG_PARITY) \
	| SEES(TX_LAUNCH_FIFO0_UNC_OR_PARITY) \
	| SEES(TX_LAUNCH_FIFO1_UNC_OR_PARITY) \
	| SEES(TX_LAUNCH_FIFO2_UNC_OR_PARITY) \
	| SEES(TX_LAUNCH_FIFO3_UNC_OR_PARITY) \
	| SEES(TX_LAUNCH_FIFO4_UNC_OR_PARITY) \
	| SEES(TX_LAUNCH_FIFO5_UNC_OR_PARITY) \
	| SEES(TX_LAUNCH_FIFO6_UNC_OR_PARITY) \
	| SEES(TX_LAUNCH_FIFO7_UNC_OR_PARITY) \
	| SEES(TX_LAUNCH_FIFO8_UNC_OR_PARITY) \
	| SEES(TX_CREDIT_RETURN_PARITY))

/*
 * TXE Send error flags
 */
#define SES(name) SEND_ERR_STATUS_SEND_##name##_ERR_SMASK
static struct flag_table send_err_status_flags[] = {
/* 0*/	FLAG_ENTRY0("SendCsrParityErr", SES(CSR_PARITY)),
/* 1*/	FLAG_ENTRY0("SendCsrReadBadAddrErr", SES(CSR_READ_BAD_ADDR)),
/* 2*/	FLAG_ENTRY0("SendCsrWriteBadAddrErr", SES(CSR_WRITE_BAD_ADDR))
};

/*
 * TXE Send Context Error flags and consequences
 */
static struct flag_table sc_err_status_flags[] = {
/* 0*/	FLAG_ENTRY("InconsistentSop",
		SEC_PACKET_DROPPED | SEC_SC_HALTED,
		SEND_CTXT_ERR_STATUS_PIO_INCONSISTENT_SOP_ERR_SMASK),
/* 1*/	FLAG_ENTRY("DisallowedPacket",
		SEC_PACKET_DROPPED | SEC_SC_HALTED,
		SEND_CTXT_ERR_STATUS_PIO_DISALLOWED_PACKET_ERR_SMASK),
/* 2*/	FLAG_ENTRY("WriteCrossesBoundary",
		SEC_WRITE_DROPPED | SEC_SC_HALTED,
		SEND_CTXT_ERR_STATUS_PIO_WRITE_CROSSES_BOUNDARY_ERR_SMASK),
/* 3*/	FLAG_ENTRY("WriteOverflow",
		SEC_WRITE_DROPPED | SEC_SC_HALTED,
		SEND_CTXT_ERR_STATUS_PIO_WRITE_OVERFLOW_ERR_SMASK),
/* 4*/	FLAG_ENTRY("WriteOutOfBounds",
		SEC_WRITE_DROPPED | SEC_SC_HALTED,
		SEND_CTXT_ERR_STATUS_PIO_WRITE_OUT_OF_BOUNDS_ERR_SMASK),
/* 5-63 reserved*/
};

/*
 * RXE Receive Error flags
 */
#define RXES(name) RCV_ERR_STATUS_RX_##name##_ERR_SMASK
static struct flag_table rxe_err_status_flags[] = {
/* 0*/	FLAG_ENTRY0("RxDmaCsrCorErr", RXES(DMA_CSR_COR)),
/* 1*/	FLAG_ENTRY0("RxDcIntfParityErr", RXES(DC_INTF_PARITY)),
/* 2*/	FLAG_ENTRY0("RxRcvHdrUncErr", RXES(RCV_HDR_UNC)),
/* 3*/	FLAG_ENTRY0("RxRcvHdrCorErr", RXES(RCV_HDR_COR)),
/* 4*/	FLAG_ENTRY0("RxRcvDataUncErr", RXES(RCV_DATA_UNC)),
/* 5*/	FLAG_ENTRY0("RxRcvDataCorErr", RXES(RCV_DATA_COR)),
/* 6*/	FLAG_ENTRY0("RxRcvQpMapTableUncErr", RXES(RCV_QP_MAP_TABLE_UNC)),
/* 7*/	FLAG_ENTRY0("RxRcvQpMapTableCorErr", RXES(RCV_QP_MAP_TABLE_COR)),
/* 8*/	FLAG_ENTRY0("RxRcvCsrParityErr", RXES(RCV_CSR_PARITY)),
/* 9*/	FLAG_ENTRY0("RxDcSopEopParityErr", RXES(DC_SOP_EOP_PARITY)),
/*10*/	FLAG_ENTRY0("RxDmaFlagUncErr", RXES(DMA_FLAG_UNC)),
/*11*/	FLAG_ENTRY0("RxDmaFlagCorErr", RXES(DMA_FLAG_COR)),
/*12*/	FLAG_ENTRY0("RxRcvFsmEncodingErr", RXES(RCV_FSM_ENCODING)),
/*13*/	FLAG_ENTRY0("RxRbufFreeListUncErr", RXES(RBUF_FREE_LIST_UNC)),
/*14*/	FLAG_ENTRY0("RxRbufFreeListCorErr", RXES(RBUF_FREE_LIST_COR)),
/*15*/	FLAG_ENTRY0("RxRbufLookupDesRegUncErr", RXES(RBUF_LOOKUP_DES_REG_UNC)),
/*16*/	FLAG_ENTRY0("RxRbufLookupDesRegUncCorErr",
		RXES(RBUF_LOOKUP_DES_REG_UNC_COR)),
/*17*/	FLAG_ENTRY0("RxRbufLookupDesUncErr", RXES(RBUF_LOOKUP_DES_UNC)),
/*18*/	FLAG_ENTRY0("RxRbufLookupDesCorErr", RXES(RBUF_LOOKUP_DES_COR)),
/*19*/	FLAG_ENTRY0("RxRbufBlockListReadUncErr",
		RXES(RBUF_BLOCK_LIST_READ_UNC)),
/*20*/	FLAG_ENTRY0("RxRbufBlockListReadCorErr",
		RXES(RBUF_BLOCK_LIST_READ_COR)),
/*21*/	FLAG_ENTRY0("RxRbufCsrQHeadBufNumParityErr",
		RXES(RBUF_CSR_QHEAD_BUF_NUM_PARITY)),
/*22*/	FLAG_ENTRY0("RxRbufCsrQEntCntParityErr",
		RXES(RBUF_CSR_QENT_CNT_PARITY)),
/*23*/	FLAG_ENTRY0("RxRbufCsrQNextBufParityErr",
		RXES(RBUF_CSR_QNEXT_BUF_PARITY)),
/*24*/	FLAG_ENTRY0("RxRbufCsrQVldBitParityErr",
		RXES(RBUF_CSR_QVLD_BIT_PARITY)),
/*25*/	FLAG_ENTRY0("RxRbufCsrQHdPtrParityErr", RXES(RBUF_CSR_QHD_PTR_PARITY)),
/*26*/	FLAG_ENTRY0("RxRbufCsrQTlPtrParityErr", RXES(RBUF_CSR_QTL_PTR_PARITY)),
/*27*/	FLAG_ENTRY0("RxRbufCsrQNumOfPktParityErr",
		RXES(RBUF_CSR_QNUM_OF_PKT_PARITY)),
/*28*/	FLAG_ENTRY0("RxRbufCsrQEOPDWParityErr", RXES(RBUF_CSR_QEOPDW_PARITY)),
/*29*/	FLAG_ENTRY0("RxRbufCtxIdParityErr", RXES(RBUF_CTX_ID_PARITY)),
/*30*/	FLAG_ENTRY0("RxRBufBadLookupErr", RXES(RBUF_BAD_LOOKUP)),
/*31*/	FLAG_ENTRY0("RxRbufFullErr", RXES(RBUF_FULL)),
/*32*/	FLAG_ENTRY0("RxRbufEmptyErr", RXES(RBUF_EMPTY)),
/*33*/	FLAG_ENTRY0("RxRbufFlRdAddrParityErr", RXES(RBUF_FL_RD_ADDR_PARITY)),
/*34*/	FLAG_ENTRY0("RxRbufFlWrAddrParityErr", RXES(RBUF_FL_WR_ADDR_PARITY)),
/*35*/	FLAG_ENTRY0("RxRbufFlInitdoneParityErr",
		RXES(RBUF_FL_INITDONE_PARITY)),
/*36*/	FLAG_ENTRY0("RxRbufFlInitWrAddrParityErr",
		RXES(RBUF_FL_INIT_WR_ADDR_PARITY)),
/*37*/	FLAG_ENTRY0("RxRbufNextFreeBufUncErr", RXES(RBUF_NEXT_FREE_BUF_UNC)),
/*38*/	FLAG_ENTRY0("RxRbufNextFreeBufCorErr", RXES(RBUF_NEXT_FREE_BUF_COR)),
/*39*/	FLAG_ENTRY0("RxLookupDesPart1UncErr", RXES(LOOKUP_DES_PART1_UNC)),
/*40*/	FLAG_ENTRY0("RxLookupDesPart1UncCorErr",
		RXES(LOOKUP_DES_PART1_UNC_COR)),
/*41*/	FLAG_ENTRY0("RxLookupDesPart2ParityErr",
		RXES(LOOKUP_DES_PART2_PARITY)),
/*42*/	FLAG_ENTRY0("RxLookupRcvArrayUncErr", RXES(LOOKUP_RCV_ARRAY_UNC)),
/*43*/	FLAG_ENTRY0("RxLookupRcvArrayCorErr", RXES(LOOKUP_RCV_ARRAY_COR)),
/*44*/	FLAG_ENTRY0("RxLookupCsrParityErr", RXES(LOOKUP_CSR_PARITY)),
/*45*/	FLAG_ENTRY0("RxHqIntrCsrParityErr", RXES(HQ_INTR_CSR_PARITY)),
/*46*/	FLAG_ENTRY0("RxHqIntrFsmErr", RXES(HQ_INTR_FSM)),
/*47*/	FLAG_ENTRY0("RxRbufDescPart1UncErr", RXES(RBUF_DESC_PART1_UNC)),
/*48*/	FLAG_ENTRY0("RxRbufDescPart1CorErr", RXES(RBUF_DESC_PART1_COR)),
/*49*/	FLAG_ENTRY0("RxRbufDescPart2UncErr", RXES(RBUF_DESC_PART2_UNC)),
/*50*/	FLAG_ENTRY0("RxRbufDescPart2CorErr", RXES(RBUF_DESC_PART2_COR)),
/*51*/	FLAG_ENTRY0("RxDmaHdrFifoRdUncErr", RXES(DMA_HDR_FIFO_RD_UNC)),
/*52*/	FLAG_ENTRY0("RxDmaHdrFifoRdCorErr", RXES(DMA_HDR_FIFO_RD_COR)),
/*53*/	FLAG_ENTRY0("RxDmaDataFifoRdUncErr", RXES(DMA_DATA_FIFO_RD_UNC)),
/*54*/	FLAG_ENTRY0("RxDmaDataFifoRdCorErr", RXES(DMA_DATA_FIFO_RD_COR)),
/*55*/	FLAG_ENTRY0("RxRbufDataUncErr", RXES(RBUF_DATA_UNC)),
/*56*/	FLAG_ENTRY0("RxRbufDataCorErr", RXES(RBUF_DATA_COR)),
/*57*/	FLAG_ENTRY0("RxDmaCsrParityErr", RXES(DMA_CSR_PARITY)),
/*58*/	FLAG_ENTRY0("RxDmaEqFsmEncodingErr", RXES(DMA_EQ_FSM_ENCODING)),
/*59*/	FLAG_ENTRY0("RxDmaDqFsmEncodingErr", RXES(DMA_DQ_FSM_ENCODING)),
/*60*/	FLAG_ENTRY0("RxDmaCsrUncErr", RXES(DMA_CSR_UNC)),
/*61*/	FLAG_ENTRY0("RxCsrReadBadAddrErr", RXES(CSR_READ_BAD_ADDR)),
/*62*/	FLAG_ENTRY0("RxCsrWriteBadAddrErr", RXES(CSR_WRITE_BAD_ADDR)),
/*63*/	FLAG_ENTRY0("RxCsrParityErr", RXES(CSR_PARITY))
};

/* RXE errors that will trigger an SPC freeze */
#define ALL_RXE_FREEZE_ERR  \
	(RCV_ERR_STATUS_RX_RCV_QP_MAP_TABLE_UNC_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RCV_CSR_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_DMA_FLAG_UNC_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RCV_FSM_ENCODING_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_FREE_LIST_UNC_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_LOOKUP_DES_REG_UNC_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_LOOKUP_DES_REG_UNC_COR_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_LOOKUP_DES_UNC_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_BLOCK_LIST_READ_UNC_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_CSR_QHEAD_BUF_NUM_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_CSR_QENT_CNT_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_CSR_QNEXT_BUF_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_CSR_QVLD_BIT_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_CSR_QHD_PTR_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_CSR_QTL_PTR_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_CSR_QNUM_OF_PKT_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_CSR_QEOPDW_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_CTX_ID_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_BAD_LOOKUP_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_FULL_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_EMPTY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_FL_RD_ADDR_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_FL_WR_ADDR_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_FL_INITDONE_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_FL_INIT_WR_ADDR_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_NEXT_FREE_BUF_UNC_ERR_SMASK \
	| RCV_ERR_STATUS_RX_LOOKUP_DES_PART1_UNC_ERR_SMASK \
	| RCV_ERR_STATUS_RX_LOOKUP_DES_PART1_UNC_COR_ERR_SMASK \
	| RCV_ERR_STATUS_RX_LOOKUP_DES_PART2_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_LOOKUP_RCV_ARRAY_UNC_ERR_SMASK \
	| RCV_ERR_STATUS_RX_LOOKUP_CSR_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_HQ_INTR_CSR_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_HQ_INTR_FSM_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_DESC_PART1_UNC_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_DESC_PART1_COR_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_DESC_PART2_UNC_ERR_SMASK \
	| RCV_ERR_STATUS_RX_DMA_HDR_FIFO_RD_UNC_ERR_SMASK \
	| RCV_ERR_STATUS_RX_DMA_DATA_FIFO_RD_UNC_ERR_SMASK \
	| RCV_ERR_STATUS_RX_RBUF_DATA_UNC_ERR_SMASK \
	| RCV_ERR_STATUS_RX_DMA_CSR_PARITY_ERR_SMASK \
	| RCV_ERR_STATUS_RX_DMA_EQ_FSM_ENCODING_ERR_SMASK \
	| RCV_ERR_STATUS_RX_DMA_DQ_FSM_ENCODING_ERR_SMASK \
	| RCV_ERR_STATUS_RX_DMA_CSR_UNC_ERR_SMASK \
	| RCV_ERR_STATUS_RX_CSR_PARITY_ERR_SMASK)

#define RXE_FREEZE_ABORT_MASK \
	(RCV_ERR_STATUS_RX_DMA_CSR_UNC_ERR_SMASK | \
	RCV_ERR_STATUS_RX_DMA_HDR_FIFO_RD_UNC_ERR_SMASK | \
	RCV_ERR_STATUS_RX_DMA_DATA_FIFO_RD_UNC_ERR_SMASK)

/*
 * DCC Error Flags
 */
#define DCCE(name) DCC_ERR_FLG_##name##_SMASK
static struct flag_table dcc_err_flags[] = {
	FLAG_ENTRY0("bad_l2_err", DCCE(BAD_L2_ERR)),
	FLAG_ENTRY0("bad_sc_err", DCCE(BAD_SC_ERR)),
	FLAG_ENTRY0("bad_mid_tail_err", DCCE(BAD_MID_TAIL_ERR)),
	FLAG_ENTRY0("bad_preemption_err", DCCE(BAD_PREEMPTION_ERR)),
	FLAG_ENTRY0("preemption_err", DCCE(PREEMPTION_ERR)),
	FLAG_ENTRY0("preemptionvl15_err", DCCE(PREEMPTIONVL15_ERR)),
	FLAG_ENTRY0("bad_vl_marker_err", DCCE(BAD_VL_MARKER_ERR)),
	FLAG_ENTRY0("bad_dlid_target_err", DCCE(BAD_DLID_TARGET_ERR)),
	FLAG_ENTRY0("bad_lver_err", DCCE(BAD_LVER_ERR)),
	FLAG_ENTRY0("uncorrectable_err", DCCE(UNCORRECTABLE_ERR)),
	FLAG_ENTRY0("bad_crdt_ack_err", DCCE(BAD_CRDT_ACK_ERR)),
	FLAG_ENTRY0("unsup_pkt_type", DCCE(UNSUP_PKT_TYPE)),
	FLAG_ENTRY0("bad_ctrl_flit_err", DCCE(BAD_CTRL_FLIT_ERR)),
	FLAG_ENTRY0("event_cntr_parity_err", DCCE(EVENT_CNTR_PARITY_ERR)),
	FLAG_ENTRY0("event_cntr_rollover_err", DCCE(EVENT_CNTR_ROLLOVER_ERR)),
	FLAG_ENTRY0("link_err", DCCE(LINK_ERR)),
	FLAG_ENTRY0("misc_cntr_rollover_err", DCCE(MISC_CNTR_ROLLOVER_ERR)),
	FLAG_ENTRY0("bad_ctrl_dist_err", DCCE(BAD_CTRL_DIST_ERR)),
	FLAG_ENTRY0("bad_tail_dist_err", DCCE(BAD_TAIL_DIST_ERR)),
	FLAG_ENTRY0("bad_head_dist_err", DCCE(BAD_HEAD_DIST_ERR)),
	FLAG_ENTRY0("nonvl15_state_err", DCCE(NONVL15_STATE_ERR)),
	FLAG_ENTRY0("vl15_multi_err", DCCE(VL15_MULTI_ERR)),
	FLAG_ENTRY0("bad_pkt_length_err", DCCE(BAD_PKT_LENGTH_ERR)),
	FLAG_ENTRY0("unsup_vl_err", DCCE(UNSUP_VL_ERR)),
	FLAG_ENTRY0("perm_nvl15_err", DCCE(PERM_NVL15_ERR)),
	FLAG_ENTRY0("slid_zero_err", DCCE(SLID_ZERO_ERR)),
	FLAG_ENTRY0("dlid_zero_err", DCCE(DLID_ZERO_ERR)),
	FLAG_ENTRY0("length_mtu_err", DCCE(LENGTH_MTU_ERR)),
	FLAG_ENTRY0("rx_early_drop_err", DCCE(RX_EARLY_DROP_ERR)),
	FLAG_ENTRY0("late_short_err", DCCE(LATE_SHORT_ERR)),
	FLAG_ENTRY0("late_long_err", DCCE(LATE_LONG_ERR)),
	FLAG_ENTRY0("late_ebp_err", DCCE(LATE_EBP_ERR)),
	FLAG_ENTRY0("fpe_tx_fifo_ovflw_err", DCCE(FPE_TX_FIFO_OVFLW_ERR)),
	FLAG_ENTRY0("fpe_tx_fifo_unflw_err", DCCE(FPE_TX_FIFO_UNFLW_ERR)),
	FLAG_ENTRY0("csr_access_blocked_host", DCCE(CSR_ACCESS_BLOCKED_HOST)),
	FLAG_ENTRY0("csr_access_blocked_uc", DCCE(CSR_ACCESS_BLOCKED_UC)),
	FLAG_ENTRY0("tx_ctrl_parity_err", DCCE(TX_CTRL_PARITY_ERR)),
	FLAG_ENTRY0("tx_ctrl_parity_mbe_err", DCCE(TX_CTRL_PARITY_MBE_ERR)),
	FLAG_ENTRY0("tx_sc_parity_err", DCCE(TX_SC_PARITY_ERR)),
	FLAG_ENTRY0("rx_ctrl_parity_mbe_err", DCCE(RX_CTRL_PARITY_MBE_ERR)),
	FLAG_ENTRY0("csr_parity_err", DCCE(CSR_PARITY_ERR)),
	FLAG_ENTRY0("csr_inval_addr", DCCE(CSR_INVAL_ADDR)),
	FLAG_ENTRY0("tx_byte_shft_parity_err", DCCE(TX_BYTE_SHFT_PARITY_ERR)),
	FLAG_ENTRY0("rx_byte_shft_parity_err", DCCE(RX_BYTE_SHFT_PARITY_ERR)),
	FLAG_ENTRY0("fmconfig_err", DCCE(FMCONFIG_ERR)),
	FLAG_ENTRY0("rcvport_err", DCCE(RCVPORT_ERR)),
};

/*
 * LCB error flags
 */
#define LCBE(name) DC_LCB_ERR_FLG_##name##_SMASK
static struct flag_table lcb_err_flags[] = {
/* 0*/	FLAG_ENTRY0("CSR_PARITY_ERR", LCBE(CSR_PARITY_ERR)),
/* 1*/	FLAG_ENTRY0("INVALID_CSR_ADDR", LCBE(INVALID_CSR_ADDR)),
/* 2*/	FLAG_ENTRY0("RST_FOR_FAILED_DESKEW", LCBE(RST_FOR_FAILED_DESKEW)),
/* 3*/	FLAG_ENTRY0("ALL_LNS_FAILED_REINIT_TEST",
		LCBE(ALL_LNS_FAILED_REINIT_TEST)),
/* 4*/	FLAG_ENTRY0("LOST_REINIT_STALL_OR_TOS", LCBE(LOST_REINIT_STALL_OR_TOS)),
/* 5*/	FLAG_ENTRY0("TX_LESS_THAN_FOUR_LNS", LCBE(TX_LESS_THAN_FOUR_LNS)),
/* 6*/	FLAG_ENTRY0("RX_LESS_THAN_FOUR_LNS", LCBE(RX_LESS_THAN_FOUR_LNS)),
/* 7*/	FLAG_ENTRY0("SEQ_CRC_ERR", LCBE(SEQ_CRC_ERR)),
/* 8*/	FLAG_ENTRY0("REINIT_FROM_PEER", LCBE(REINIT_FROM_PEER)),
/* 9*/	FLAG_ENTRY0("REINIT_FOR_LN_DEGRADE", LCBE(REINIT_FOR_LN_DEGRADE)),
/*10*/	FLAG_ENTRY0("CRC_ERR_CNT_HIT_LIMIT", LCBE(CRC_ERR_CNT_HIT_LIMIT)),
/*11*/	FLAG_ENTRY0("RCLK_STOPPED", LCBE(RCLK_STOPPED)),
/*12*/	FLAG_ENTRY0("UNEXPECTED_REPLAY_MARKER", LCBE(UNEXPECTED_REPLAY_MARKER)),
/*13*/	FLAG_ENTRY0("UNEXPECTED_ROUND_TRIP_MARKER",
		LCBE(UNEXPECTED_ROUND_TRIP_MARKER)),
/*14*/	FLAG_ENTRY0("ILLEGAL_NULL_LTP", LCBE(ILLEGAL_NULL_LTP)),
/*15*/	FLAG_ENTRY0("ILLEGAL_FLIT_ENCODING", LCBE(ILLEGAL_FLIT_ENCODING)),
/*16*/	FLAG_ENTRY0("FLIT_INPUT_BUF_OFLW", LCBE(FLIT_INPUT_BUF_OFLW)),
/*17*/	FLAG_ENTRY0("VL_ACK_INPUT_BUF_OFLW", LCBE(VL_ACK_INPUT_BUF_OFLW)),
/*18*/	FLAG_ENTRY0("VL_ACK_INPUT_PARITY_ERR", LCBE(VL_ACK_INPUT_PARITY_ERR)),
/*19*/	FLAG_ENTRY0("VL_ACK_INPUT_WRONG_CRC_MODE",
		LCBE(VL_ACK_INPUT_WRONG_CRC_MODE)),
/*20*/	FLAG_ENTRY0("FLIT_INPUT_BUF_MBE", LCBE(FLIT_INPUT_BUF_MBE)),
/*21*/	FLAG_ENTRY0("FLIT_INPUT_BUF_SBE", LCBE(FLIT_INPUT_BUF_SBE)),
/*22*/	FLAG_ENTRY0("REPLAY_BUF_MBE", LCBE(REPLAY_BUF_MBE)),
/*23*/	FLAG_ENTRY0("REPLAY_BUF_SBE", LCBE(REPLAY_BUF_SBE)),
/*24*/	FLAG_ENTRY0("CREDIT_RETURN_FLIT_MBE", LCBE(CREDIT_RETURN_FLIT_MBE)),
/*25*/	FLAG_ENTRY0("RST_FOR_LINK_TIMEOUT", LCBE(RST_FOR_LINK_TIMEOUT)),
/*26*/	FLAG_ENTRY0("RST_FOR_INCOMPLT_RND_TRIP",
		LCBE(RST_FOR_INCOMPLT_RND_TRIP)),
/*27*/	FLAG_ENTRY0("HOLD_REINIT", LCBE(HOLD_REINIT)),
/*28*/	FLAG_ENTRY0("NEG_EDGE_LINK_TRANSFER_ACTIVE",
		LCBE(NEG_EDGE_LINK_TRANSFER_ACTIVE)),
/*29*/	FLAG_ENTRY0("REDUNDANT_FLIT_PARITY_ERR",
		LCBE(REDUNDANT_FLIT_PARITY_ERR))
};

/*
 * DC8051 Error Flags
 */
#define D8E(name) DC_DC8051_ERR_FLG_##name##_SMASK
static struct flag_table dc8051_err_flags[] = {
	FLAG_ENTRY0("SET_BY_8051", D8E(SET_BY_8051)),
	FLAG_ENTRY0("LOST_8051_HEART_BEAT", D8E(LOST_8051_HEART_BEAT)),
	FLAG_ENTRY0("CRAM_MBE", D8E(CRAM_MBE)),
	FLAG_ENTRY0("CRAM_SBE", D8E(CRAM_SBE)),
	FLAG_ENTRY0("DRAM_MBE", D8E(DRAM_MBE)),
	FLAG_ENTRY0("DRAM_SBE", D8E(DRAM_SBE)),
	FLAG_ENTRY0("IRAM_MBE", D8E(IRAM_MBE)),
	FLAG_ENTRY0("IRAM_SBE", D8E(IRAM_SBE)),
	FLAG_ENTRY0("UNMATCHED_SECURE_MSG_ACROSS_BCC_LANES",
		    D8E(UNMATCHED_SECURE_MSG_ACROSS_BCC_LANES)),
	FLAG_ENTRY0("INVALID_CSR_ADDR", D8E(INVALID_CSR_ADDR)),
};

/*
 * DC8051 Information Error flags
 *
 * Flags in DC8051_DBG_ERR_INFO_SET_BY_8051.ERROR field.
 */
static struct flag_table dc8051_info_err_flags[] = {
	FLAG_ENTRY0("Spico ROM check failed",  SPICO_ROM_FAILED),
	FLAG_ENTRY0("Unknown frame received",  UNKNOWN_FRAME),
	FLAG_ENTRY0("Target BER not met",      TARGET_BER_NOT_MET),
	FLAG_ENTRY0("Serdes internal loopback failure",
		    FAILED_SERDES_INTERNAL_LOOPBACK),
	FLAG_ENTRY0("Failed SerDes init",      FAILED_SERDES_INIT),
	FLAG_ENTRY0("Failed LNI(Polling)",     FAILED_LNI_POLLING),
	FLAG_ENTRY0("Failed LNI(Debounce)",    FAILED_LNI_DEBOUNCE),
	FLAG_ENTRY0("Failed LNI(EstbComm)",    FAILED_LNI_ESTBCOMM),
	FLAG_ENTRY0("Failed LNI(OptEq)",       FAILED_LNI_OPTEQ),
	FLAG_ENTRY0("Failed LNI(VerifyCap_1)", FAILED_LNI_VERIFY_CAP1),
	FLAG_ENTRY0("Failed LNI(VerifyCap_2)", FAILED_LNI_VERIFY_CAP2),
	FLAG_ENTRY0("Failed LNI(ConfigLT)",    FAILED_LNI_CONFIGLT),
	FLAG_ENTRY0("Host Handshake Timeout",  HOST_HANDSHAKE_TIMEOUT),
	FLAG_ENTRY0("External Device Request Timeout",
		    EXTERNAL_DEVICE_REQ_TIMEOUT),
};

/*
 * DC8051 Information Host Information flags
 *
 * Flags in DC8051_DBG_ERR_INFO_SET_BY_8051.HOST_MSG field.
 */
static struct flag_table dc8051_info_host_msg_flags[] = {
	FLAG_ENTRY0("Host request done", 0x0001),
	FLAG_ENTRY0("BC PWR_MGM message", 0x0002),
	FLAG_ENTRY0("BC SMA message", 0x0004),
	FLAG_ENTRY0("BC Unknown message (BCC)", 0x0008),
	FLAG_ENTRY0("BC Unknown message (LCB)", 0x0010),
	FLAG_ENTRY0("External device config request", 0x0020),
	FLAG_ENTRY0("VerifyCap all frames received", 0x0040),
	FLAG_ENTRY0("LinkUp achieved", 0x0080),
	FLAG_ENTRY0("Link going down", 0x0100),
	FLAG_ENTRY0("Link width downgraded", 0x0200),
};

static u32 encoded_size(u32 size);
static u32 chip_to_opa_lstate(struct hfi1_devdata *dd, u32 chip_lstate);
static int set_physical_link_state(struct hfi1_devdata *dd, u64 state);
static void read_vc_remote_phy(struct hfi1_devdata *dd, u8 *power_management,
			       u8 *continuous);
static void read_vc_remote_fabric(struct hfi1_devdata *dd, u8 *vau, u8 *z,
				  u8 *vcu, u16 *vl15buf, u8 *crc_sizes);
static void read_vc_remote_link_width(struct hfi1_devdata *dd,
				      u8 *remote_tx_rate, u16 *link_widths);
static void read_vc_local_link_mode(struct hfi1_devdata *dd, u8 *misc_bits,
				    u8 *flag_bits, u16 *link_widths);
static void read_remote_device_id(struct hfi1_devdata *dd, u16 *device_id,
				  u8 *device_rev);
static void read_local_lni(struct hfi1_devdata *dd, u8 *enable_lane_rx);
static int read_tx_settings(struct hfi1_devdata *dd, u8 *enable_lane_tx,
			    u8 *tx_polarity_inversion,
			    u8 *rx_polarity_inversion, u8 *max_rate);
static void handle_sdma_eng_err(struct hfi1_devdata *dd,
				unsigned int context, u64 err_status);
static void handle_qsfp_int(struct hfi1_devdata *dd, u32 source, u64 reg);
static void handle_dcc_err(struct hfi1_devdata *dd,
			   unsigned int context, u64 err_status);
static void handle_lcb_err(struct hfi1_devdata *dd,
			   unsigned int context, u64 err_status);
static void handle_8051_interrupt(struct hfi1_devdata *dd, u32 unused, u64 reg);
static void handle_cce_err(struct hfi1_devdata *dd, u32 unused, u64 reg);
static void handle_rxe_err(struct hfi1_devdata *dd, u32 unused, u64 reg);
static void handle_misc_err(struct hfi1_devdata *dd, u32 unused, u64 reg);
static void handle_pio_err(struct hfi1_devdata *dd, u32 unused, u64 reg);
static void handle_sdma_err(struct hfi1_devdata *dd, u32 unused, u64 reg);
static void handle_egress_err(struct hfi1_devdata *dd, u32 unused, u64 reg);
static void handle_txe_err(struct hfi1_devdata *dd, u32 unused, u64 reg);
static void set_partition_keys(struct hfi1_pportdata *ppd);
static const char *link_state_name(u32 state);
static const char *link_state_reason_name(struct hfi1_pportdata *ppd,
					  u32 state);
static int do_8051_command(struct hfi1_devdata *dd, u32 type, u64 in_data,
			   u64 *out_data);
static int read_idle_sma(struct hfi1_devdata *dd, u64 *data);
static int thermal_init(struct hfi1_devdata *dd);

static void update_statusp(struct hfi1_pportdata *ppd, u32 state);
static int wait_phys_link_offline_substates(struct hfi1_pportdata *ppd,
					    int msecs);
static int wait_logical_linkstate(struct hfi1_pportdata *ppd, u32 state,
				  int msecs);
static void log_state_transition(struct hfi1_pportdata *ppd, u32 state);
static void log_physical_state(struct hfi1_pportdata *ppd, u32 state);
static int wait_physical_linkstate(struct hfi1_pportdata *ppd, u32 state,
				   int msecs);
static int wait_phys_link_out_of_offline(struct hfi1_pportdata *ppd,
					 int msecs);
static void read_planned_down_reason_code(struct hfi1_devdata *dd, u8 *pdrrc);
static void read_link_down_reason(struct hfi1_devdata *dd, u8 *ldr);
static void handle_temp_err(struct hfi1_devdata *dd);
static void dc_shutdown(struct hfi1_devdata *dd);
static void dc_start(struct hfi1_devdata *dd);
static int qos_rmt_entries(struct hfi1_devdata *dd, unsigned int *mp,
			   unsigned int *np);
static void clear_full_mgmt_pkey(struct hfi1_pportdata *ppd);
static int wait_link_transfer_active(struct hfi1_devdata *dd, int wait_ms);
static void clear_rsm_rule(struct hfi1_devdata *dd, u8 rule_index);
static void update_xmit_counters(struct hfi1_pportdata *ppd, u16 link_width);

/*
 * Error interrupt table entry.  This is used as input to the interrupt
 * "clear down" routine used for all second tier error interrupt register.
 * Second tier interrupt registers have a single bit representing them
 * in the top-level CceIntStatus.
 */
struct err_reg_info {
	u32 status;		/* status CSR offset */
	u32 clear;		/* clear CSR offset */
	u32 mask;		/* mask CSR offset */
	void (*handler)(struct hfi1_devdata *dd, u32 source, u64 reg);
	const char *desc;
};

#define NUM_MISC_ERRS (IS_GENERAL_ERR_END + 1 - IS_GENERAL_ERR_START)
#define NUM_DC_ERRS (IS_DC_END + 1 - IS_DC_START)
#define NUM_VARIOUS (IS_VARIOUS_END + 1 - IS_VARIOUS_START)

/*
 * Helpers for building HFI and DC error interrupt table entries.  Different
 * helpers are needed because of inconsistent register names.
 */
#define EE(reg, handler, desc) \
	{ reg##_STATUS, reg##_CLEAR, reg##_MASK, \
		handler, desc }
#define DC_EE1(reg, handler, desc) \
	{ reg##_FLG, reg##_FLG_CLR, reg##_FLG_EN, handler, desc }
#define DC_EE2(reg, handler, desc) \
	{ reg##_FLG, reg##_CLR, reg##_EN, handler, desc }

/*
 * Table of the "misc" grouping of error interrupts.  Each entry refers to
 * another register containing more information.
 */
static const struct err_reg_info misc_errs[NUM_MISC_ERRS] = {
/* 0*/	EE(CCE_ERR,		handle_cce_err,    "CceErr"),
/* 1*/	EE(RCV_ERR,		handle_rxe_err,    "RxeErr"),
/* 2*/	EE(MISC_ERR,	handle_misc_err,   "MiscErr"),
/* 3*/	{ 0, 0, 0, NULL }, /* reserved */
/* 4*/	EE(SEND_PIO_ERR,    handle_pio_err,    "PioErr"),
/* 5*/	EE(SEND_DMA_ERR,    handle_sdma_err,   "SDmaErr"),
/* 6*/	EE(SEND_EGRESS_ERR, handle_egress_err, "EgressErr"),
/* 7*/	EE(SEND_ERR,	handle_txe_err,    "TxeErr")
	/* the rest are reserved */
};

/*
 * Index into the Various section of the interrupt sources
 * corresponding to the Critical Temperature interrupt.
 */
#define TCRIT_INT_SOURCE 4

/*
 * SDMA error interrupt entry - refers to another register containing more
 * information.
 */
static const struct err_reg_info sdma_eng_err =
	EE(SEND_DMA_ENG_ERR, handle_sdma_eng_err, "SDmaEngErr");

static const struct err_reg_info various_err[NUM_VARIOUS] = {
/* 0*/	{ 0, 0, 0, NULL }, /* PbcInt */
/* 1*/	{ 0, 0, 0, NULL }, /* GpioAssertInt */
/* 2*/	EE(ASIC_QSFP1,	handle_qsfp_int,	"QSFP1"),
/* 3*/	EE(ASIC_QSFP2,	handle_qsfp_int,	"QSFP2"),
/* 4*/	{ 0, 0, 0, NULL }, /* TCritInt */
	/* rest are reserved */
};

/*
 * The DC encoding of mtu_cap for 10K MTU in the DCC_CFG_PORT_CONFIG
 * register can not be derived from the MTU value because 10K is not
 * a power of 2. Therefore, we need a constant. Everything else can
 * be calculated.
 */
#define DCC_CFG_PORT_MTU_CAP_10240 7

/*
 * Table of the DC grouping of error interrupts.  Each entry refers to
 * another register containing more information.
 */
static const struct err_reg_info dc_errs[NUM_DC_ERRS] = {
/* 0*/	DC_EE1(DCC_ERR,		handle_dcc_err,	       "DCC Err"),
/* 1*/	DC_EE2(DC_LCB_ERR,	handle_lcb_err,	       "LCB Err"),
/* 2*/	DC_EE2(DC_DC8051_ERR,	handle_8051_interrupt, "DC8051 Interrupt"),
/* 3*/	/* dc_lbm_int - special, see is_dc_int() */
	/* the rest are reserved */
};

struct cntr_entry {
	/*
	 * counter name
	 */
	char *name;

	/*
	 * csr to read for name (if applicable)
	 */
	u64 csr;

	/*
	 * offset into dd or ppd to store the counter's value
	 */
	int offset;

	/*
	 * flags
	 */
	u8 flags;

	/*
	 * accessor for stat element, context either dd or ppd
	 */
	u64 (*rw_cntr)(const struct cntr_entry *, void *context, int vl,
		       int mode, u64 data);
};

#define C_RCV_HDR_OVF_FIRST C_RCV_HDR_OVF_0
#define C_RCV_HDR_OVF_LAST C_RCV_HDR_OVF_159

#define CNTR_ELEM(name, csr, offset, flags, accessor) \
{ \
	name, \
	csr, \
	offset, \
	flags, \
	accessor \
}

/* 32bit RXE */
#define RXE32_PORT_CNTR_ELEM(name, counter, flags) \
CNTR_ELEM(#name, \
	  (counter * 8 + RCV_COUNTER_ARRAY32), \
	  0, flags | CNTR_32BIT, \
	  port_access_u32_csr)

#define RXE32_DEV_CNTR_ELEM(name, counter, flags) \
CNTR_ELEM(#name, \
	  (counter * 8 + RCV_COUNTER_ARRAY32), \
	  0, flags | CNTR_32BIT, \
	  dev_access_u32_csr)

/* 64bit RXE */
#define RXE64_PORT_CNTR_ELEM(name, counter, flags) \
CNTR_ELEM(#name, \
	  (counter * 8 + RCV_COUNTER_ARRAY64), \
	  0, flags, \
	  port_access_u64_csr)

#define RXE64_DEV_CNTR_ELEM(name, counter, flags) \
CNTR_ELEM(#name, \
	  (counter * 8 + RCV_COUNTER_ARRAY64), \
	  0, flags, \
	  dev_access_u64_csr)

#define OVR_LBL(ctx) C_RCV_HDR_OVF_ ## ctx
#define OVR_ELM(ctx) \
CNTR_ELEM("RcvHdrOvr" #ctx, \
	  (RCV_HDR_OVFL_CNT + ctx * 0x100), \
	  0, CNTR_NORMAL, port_access_u64_csr)

/* 32bit TXE */
#define TXE32_PORT_CNTR_ELEM(name, counter, flags) \
CNTR_ELEM(#name, \
	  (counter * 8 + SEND_COUNTER_ARRAY32), \
	  0, flags | CNTR_32BIT, \
	  port_access_u32_csr)

/* 64bit TXE */
#define TXE64_PORT_CNTR_ELEM(name, counter, flags) \
CNTR_ELEM(#name, \
	  (counter * 8 + SEND_COUNTER_ARRAY64), \
	  0, flags, \
	  port_access_u64_csr)

# define TX64_DEV_CNTR_ELEM(name, counter, flags) \
CNTR_ELEM(#name,\
	  counter * 8 + SEND_COUNTER_ARRAY64, \
	  0, \
	  flags, \
	  dev_access_u64_csr)

/* CCE */
#define CCE_PERF_DEV_CNTR_ELEM(name, counter, flags) \
CNTR_ELEM(#name, \
	  (counter * 8 + CCE_COUNTER_ARRAY32), \
	  0, flags | CNTR_32BIT, \
	  dev_access_u32_csr)

#define CCE_INT_DEV_CNTR_ELEM(name, counter, flags) \
CNTR_ELEM(#name, \
	  (counter * 8 + CCE_INT_COUNTER_ARRAY32), \
	  0, flags | CNTR_32BIT, \
	  dev_access_u32_csr)

/* DC */
#define DC_PERF_CNTR(name, counter, flags) \
CNTR_ELEM(#name, \
	  counter, \
	  0, \
	  flags, \
	  dev_access_u64_csr)

#define DC_PERF_CNTR_LCB(name, counter, flags) \
CNTR_ELEM(#name, \
	  counter, \
	  0, \
	  flags, \
	  dc_access_lcb_cntr)

/* ibp counters */
#define SW_IBP_CNTR(name, cntr) \
CNTR_ELEM(#name, \
	  0, \
	  0, \
	  CNTR_SYNTH, \
	  access_ibp_##cntr)

/**
 * hfi_addr_from_offset - return addr for readq/writeq
 * @dd - the dd device
 * @offset - the offset of the CSR within bar0
 *
 * This routine selects the appropriate base address
 * based on the indicated offset.
 */
static inline void __iomem *hfi1_addr_from_offset(
	const struct hfi1_devdata *dd,
	u32 offset)
{
	if (offset >= dd->base2_start)
		return dd->kregbase2 + (offset - dd->base2_start);
	return dd->kregbase1 + offset;
}

/**
 * read_csr - read CSR at the indicated offset
 * @dd - the dd device
 * @offset - the offset of the CSR within bar0
 *
 * Return: the value read or all FF's if there
 * is no mapping
 */
u64 read_csr(const struct hfi1_devdata *dd, u32 offset)
{
	if (dd->flags & HFI1_PRESENT)
		return readq(hfi1_addr_from_offset(dd, offset));
	return -1;
}

/**
 * write_csr - write CSR at the indicated offset
 * @dd - the dd device
 * @offset - the offset of the CSR within bar0
 * @value - value to write
 */
void write_csr(const struct hfi1_devdata *dd, u32 offset, u64 value)
{
	if (dd->flags & HFI1_PRESENT) {
		void __iomem *base = hfi1_addr_from_offset(dd, offset);

		/* avoid write to RcvArray */
		if (WARN_ON(offset >= RCV_ARRAY && offset < dd->base2_start))
			return;
		writeq(value, base);
	}
}

/**
 * get_csr_addr - return te iomem address for offset
 * @dd - the dd device
 * @offset - the offset of the CSR within bar0
 *
 * Return: The iomem address to use in subsequent
 * writeq/readq operations.
 */
void __iomem *get_csr_addr(
	const struct hfi1_devdata *dd,
	u32 offset)
{
	if (dd->flags & HFI1_PRESENT)
		return hfi1_addr_from_offset(dd, offset);
	return NULL;
}

static inline u64 read_write_csr(const struct hfi1_devdata *dd, u32 csr,
				 int mode, u64 value)
{
	u64 ret;

	if (mode == CNTR_MODE_R) {
		ret = read_csr(dd, csr);
	} else if (mode == CNTR_MODE_W) {
		write_csr(dd, csr, value);
		ret = value;
	} else {
		dd_dev_err(dd, "Invalid cntr register access mode");
		return 0;
	}

	hfi1_cdbg(CNTR, "csr 0x%x val 0x%llx mode %d", csr, ret, mode);
	return ret;
}

/* Dev Access */
static u64 dev_access_u32_csr(const struct cntr_entry *entry,
			      void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = context;
	u64 csr = entry->csr;

	if (entry->flags & CNTR_SDMA) {
		if (vl == CNTR_INVALID_VL)
			return 0;
		csr += 0x100 * vl;
	} else {
		if (vl != CNTR_INVALID_VL)
			return 0;
	}
	return read_write_csr(dd, csr, mode, data);
}

static u64 access_sde_err_cnt(const struct cntr_entry *entry,
			      void *context, int idx, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	if (dd->per_sdma && idx < dd->num_sdma)
		return dd->per_sdma[idx].err_cnt;
	return 0;
}

static u64 access_sde_int_cnt(const struct cntr_entry *entry,
			      void *context, int idx, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	if (dd->per_sdma && idx < dd->num_sdma)
		return dd->per_sdma[idx].sdma_int_cnt;
	return 0;
}

static u64 access_sde_idle_int_cnt(const struct cntr_entry *entry,
				   void *context, int idx, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	if (dd->per_sdma && idx < dd->num_sdma)
		return dd->per_sdma[idx].idle_int_cnt;
	return 0;
}

static u64 access_sde_progress_int_cnt(const struct cntr_entry *entry,
				       void *context, int idx, int mode,
				       u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	if (dd->per_sdma && idx < dd->num_sdma)
		return dd->per_sdma[idx].progress_int_cnt;
	return 0;
}

static u64 dev_access_u64_csr(const struct cntr_entry *entry, void *context,
			      int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = context;

	u64 val = 0;
	u64 csr = entry->csr;

	if (entry->flags & CNTR_VL) {
		if (vl == CNTR_INVALID_VL)
			return 0;
		csr += 8 * vl;
	} else {
		if (vl != CNTR_INVALID_VL)
			return 0;
	}

	val = read_write_csr(dd, csr, mode, data);
	return val;
}

static u64 dc_access_lcb_cntr(const struct cntr_entry *entry, void *context,
			      int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = context;
	u32 csr = entry->csr;
	int ret = 0;

	if (vl != CNTR_INVALID_VL)
		return 0;
	if (mode == CNTR_MODE_R)
		ret = read_lcb_csr(dd, csr, &data);
	else if (mode == CNTR_MODE_W)
		ret = write_lcb_csr(dd, csr, data);

	if (ret) {
		dd_dev_err(dd, "Could not acquire LCB for counter 0x%x", csr);
		return 0;
	}

	hfi1_cdbg(CNTR, "csr 0x%x val 0x%llx mode %d", csr, data, mode);
	return data;
}

/* Port Access */
static u64 port_access_u32_csr(const struct cntr_entry *entry, void *context,
			       int vl, int mode, u64 data)
{
	struct hfi1_pportdata *ppd = context;

	if (vl != CNTR_INVALID_VL)
		return 0;
	return read_write_csr(ppd->dd, entry->csr, mode, data);
}

static u64 port_access_u64_csr(const struct cntr_entry *entry,
			       void *context, int vl, int mode, u64 data)
{
	struct hfi1_pportdata *ppd = context;
	u64 val;
	u64 csr = entry->csr;

	if (entry->flags & CNTR_VL) {
		if (vl == CNTR_INVALID_VL)
			return 0;
		csr += 8 * vl;
	} else {
		if (vl != CNTR_INVALID_VL)
			return 0;
	}
	val = read_write_csr(ppd->dd, csr, mode, data);
	return val;
}

/* Software defined */
static inline u64 read_write_sw(struct hfi1_devdata *dd, u64 *cntr, int mode,
				u64 data)
{
	u64 ret;

	if (mode == CNTR_MODE_R) {
		ret = *cntr;
	} else if (mode == CNTR_MODE_W) {
		*cntr = data;
		ret = data;
	} else {
		dd_dev_err(dd, "Invalid cntr sw access mode");
		return 0;
	}

	hfi1_cdbg(CNTR, "val 0x%llx mode %d", ret, mode);

	return ret;
}

static u64 access_sw_link_dn_cnt(const struct cntr_entry *entry, void *context,
				 int vl, int mode, u64 data)
{
	struct hfi1_pportdata *ppd = context;

	if (vl != CNTR_INVALID_VL)
		return 0;
	return read_write_sw(ppd->dd, &ppd->link_downed, mode, data);
}

static u64 access_sw_link_up_cnt(const struct cntr_entry *entry, void *context,
				 int vl, int mode, u64 data)
{
	struct hfi1_pportdata *ppd = context;

	if (vl != CNTR_INVALID_VL)
		return 0;
	return read_write_sw(ppd->dd, &ppd->link_up, mode, data);
}

static u64 access_sw_unknown_frame_cnt(const struct cntr_entry *entry,
				       void *context, int vl, int mode,
				       u64 data)
{
	struct hfi1_pportdata *ppd = (struct hfi1_pportdata *)context;

	if (vl != CNTR_INVALID_VL)
		return 0;
	return read_write_sw(ppd->dd, &ppd->unknown_frame_count, mode, data);
}

static u64 access_sw_xmit_discards(const struct cntr_entry *entry,
				   void *context, int vl, int mode, u64 data)
{
	struct hfi1_pportdata *ppd = (struct hfi1_pportdata *)context;
	u64 zero = 0;
	u64 *counter;

	if (vl == CNTR_INVALID_VL)
		counter = &ppd->port_xmit_discards;
	else if (vl >= 0 && vl < C_VL_COUNT)
		counter = &ppd->port_xmit_discards_vl[vl];
	else
		counter = &zero;

	return read_write_sw(ppd->dd, counter, mode, data);
}

static u64 access_xmit_constraint_errs(const struct cntr_entry *entry,
				       void *context, int vl, int mode,
				       u64 data)
{
	struct hfi1_pportdata *ppd = context;

	if (vl != CNTR_INVALID_VL)
		return 0;

	return read_write_sw(ppd->dd, &ppd->port_xmit_constraint_errors,
			     mode, data);
}

static u64 access_rcv_constraint_errs(const struct cntr_entry *entry,
				      void *context, int vl, int mode, u64 data)
{
	struct hfi1_pportdata *ppd = context;

	if (vl != CNTR_INVALID_VL)
		return 0;

	return read_write_sw(ppd->dd, &ppd->port_rcv_constraint_errors,
			     mode, data);
}

u64 get_all_cpu_total(u64 __percpu *cntr)
{
	int cpu;
	u64 counter = 0;

	for_each_possible_cpu(cpu)
		counter += *per_cpu_ptr(cntr, cpu);
	return counter;
}

static u64 read_write_cpu(struct hfi1_devdata *dd, u64 *z_val,
			  u64 __percpu *cntr,
			  int vl, int mode, u64 data)
{
	u64 ret = 0;

	if (vl != CNTR_INVALID_VL)
		return 0;

	if (mode == CNTR_MODE_R) {
		ret = get_all_cpu_total(cntr) - *z_val;
	} else if (mode == CNTR_MODE_W) {
		/* A write can only zero the counter */
		if (data == 0)
			*z_val = get_all_cpu_total(cntr);
		else
			dd_dev_err(dd, "Per CPU cntrs can only be zeroed");
	} else {
		dd_dev_err(dd, "Invalid cntr sw cpu access mode");
		return 0;
	}

	return ret;
}

static u64 access_sw_cpu_intr(const struct cntr_entry *entry,
			      void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = context;

	return read_write_cpu(dd, &dd->z_int_counter, dd->int_counter, vl,
			      mode, data);
}

static u64 access_sw_cpu_rcv_limit(const struct cntr_entry *entry,
				   void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = context;

	return read_write_cpu(dd, &dd->z_rcv_limit, dd->rcv_limit, vl,
			      mode, data);
}

static u64 access_sw_pio_wait(const struct cntr_entry *entry,
			      void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = context;

	return dd->verbs_dev.n_piowait;
}

static u64 access_sw_pio_drain(const struct cntr_entry *entry,
			       void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->verbs_dev.n_piodrain;
}

static u64 access_sw_ctx0_seq_drop(const struct cntr_entry *entry,
				   void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = context;

	return dd->ctx0_seq_drop;
}

static u64 access_sw_vtx_wait(const struct cntr_entry *entry,
			      void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = context;

	return dd->verbs_dev.n_txwait;
}

static u64 access_sw_kmem_wait(const struct cntr_entry *entry,
			       void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = context;

	return dd->verbs_dev.n_kmem_wait;
}

static u64 access_sw_send_schedule(const struct cntr_entry *entry,
				   void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return read_write_cpu(dd, &dd->z_send_schedule, dd->send_schedule, vl,
			      mode, data);
}

/* Software counters for the error status bits within MISC_ERR_STATUS */
static u64 access_misc_pll_lock_fail_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->misc_err_status_cnt[12];
}

static u64 access_misc_mbist_fail_err_cnt(const struct cntr_entry *entry,
					  void *context, int vl, int mode,
					  u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->misc_err_status_cnt[11];
}

static u64 access_misc_invalid_eep_cmd_err_cnt(const struct cntr_entry *entry,
					       void *context, int vl, int mode,
					       u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->misc_err_status_cnt[10];
}

static u64 access_misc_efuse_done_parity_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->misc_err_status_cnt[9];
}

static u64 access_misc_efuse_write_err_cnt(const struct cntr_entry *entry,
					   void *context, int vl, int mode,
					   u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->misc_err_status_cnt[8];
}

static u64 access_misc_efuse_read_bad_addr_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->misc_err_status_cnt[7];
}

static u64 access_misc_efuse_csr_parity_err_cnt(const struct cntr_entry *entry,
						void *context, int vl,
						int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->misc_err_status_cnt[6];
}

static u64 access_misc_fw_auth_failed_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->misc_err_status_cnt[5];
}

static u64 access_misc_key_mismatch_err_cnt(const struct cntr_entry *entry,
					    void *context, int vl, int mode,
					    u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->misc_err_status_cnt[4];
}

static u64 access_misc_sbus_write_failed_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->misc_err_status_cnt[3];
}

static u64 access_misc_csr_write_bad_addr_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->misc_err_status_cnt[2];
}

static u64 access_misc_csr_read_bad_addr_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->misc_err_status_cnt[1];
}

static u64 access_misc_csr_parity_err_cnt(const struct cntr_entry *entry,
					  void *context, int vl, int mode,
					  u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->misc_err_status_cnt[0];
}

/*
 * Software counter for the aggregate of
 * individual CceErrStatus counters
 */
static u64 access_sw_cce_err_status_aggregated_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_cce_err_status_aggregate;
}

/*
 * Software counters corresponding to each of the
 * error status bits within CceErrStatus
 */
static u64 access_cce_msix_csr_parity_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[40];
}

static u64 access_cce_int_map_unc_err_cnt(const struct cntr_entry *entry,
					  void *context, int vl, int mode,
					  u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[39];
}

static u64 access_cce_int_map_cor_err_cnt(const struct cntr_entry *entry,
					  void *context, int vl, int mode,
					  u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[38];
}

static u64 access_cce_msix_table_unc_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[37];
}

static u64 access_cce_msix_table_cor_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[36];
}

static u64 access_cce_rxdma_conv_fifo_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[35];
}

static u64 access_cce_rcpl_async_fifo_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[34];
}

static u64 access_cce_seg_write_bad_addr_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[33];
}

static u64 access_cce_seg_read_bad_addr_err_cnt(const struct cntr_entry *entry,
						void *context, int vl, int mode,
						u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[32];
}

static u64 access_la_triggered_cnt(const struct cntr_entry *entry,
				   void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[31];
}

static u64 access_cce_trgt_cpl_timeout_err_cnt(const struct cntr_entry *entry,
					       void *context, int vl, int mode,
					       u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[30];
}

static u64 access_pcic_receive_parity_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[29];
}

static u64 access_pcic_transmit_back_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[28];
}

static u64 access_pcic_transmit_front_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[27];
}

static u64 access_pcic_cpl_dat_q_unc_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[26];
}

static u64 access_pcic_cpl_hd_q_unc_err_cnt(const struct cntr_entry *entry,
					    void *context, int vl, int mode,
					    u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[25];
}

static u64 access_pcic_post_dat_q_unc_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[24];
}

static u64 access_pcic_post_hd_q_unc_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[23];
}

static u64 access_pcic_retry_sot_mem_unc_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[22];
}

static u64 access_pcic_retry_mem_unc_err(const struct cntr_entry *entry,
					 void *context, int vl, int mode,
					 u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[21];
}

static u64 access_pcic_n_post_dat_q_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[20];
}

static u64 access_pcic_n_post_h_q_parity_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[19];
}

static u64 access_pcic_cpl_dat_q_cor_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[18];
}

static u64 access_pcic_cpl_hd_q_cor_err_cnt(const struct cntr_entry *entry,
					    void *context, int vl, int mode,
					    u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[17];
}

static u64 access_pcic_post_dat_q_cor_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[16];
}

static u64 access_pcic_post_hd_q_cor_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[15];
}

static u64 access_pcic_retry_sot_mem_cor_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[14];
}

static u64 access_pcic_retry_mem_cor_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[13];
}

static u64 access_cce_cli1_async_fifo_dbg_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[12];
}

static u64 access_cce_cli1_async_fifo_rxdma_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[11];
}

static u64 access_cce_cli1_async_fifo_sdma_hd_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[10];
}

static u64 access_cce_cl1_async_fifo_pio_crdt_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[9];
}

static u64 access_cce_cli2_async_fifo_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[8];
}

static u64 access_cce_csr_cfg_bus_parity_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[7];
}

static u64 access_cce_cli0_async_fifo_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[6];
}

static u64 access_cce_rspd_data_parity_err_cnt(const struct cntr_entry *entry,
					       void *context, int vl, int mode,
					       u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[5];
}

static u64 access_cce_trgt_access_err_cnt(const struct cntr_entry *entry,
					  void *context, int vl, int mode,
					  u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[4];
}

static u64 access_cce_trgt_async_fifo_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[3];
}

static u64 access_cce_csr_write_bad_addr_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[2];
}

static u64 access_cce_csr_read_bad_addr_err_cnt(const struct cntr_entry *entry,
						void *context, int vl,
						int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[1];
}

static u64 access_ccs_csr_parity_err_cnt(const struct cntr_entry *entry,
					 void *context, int vl, int mode,
					 u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->cce_err_status_cnt[0];
}

/*
 * Software counters corresponding to each of the
 * error status bits within RcvErrStatus
 */
static u64 access_rx_csr_parity_err_cnt(const struct cntr_entry *entry,
					void *context, int vl, int mode,
					u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[63];
}

static u64 access_rx_csr_write_bad_addr_err_cnt(const struct cntr_entry *entry,
						void *context, int vl,
						int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[62];
}

static u64 access_rx_csr_read_bad_addr_err_cnt(const struct cntr_entry *entry,
					       void *context, int vl, int mode,
					       u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[61];
}

static u64 access_rx_dma_csr_unc_err_cnt(const struct cntr_entry *entry,
					 void *context, int vl, int mode,
					 u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[60];
}

static u64 access_rx_dma_dq_fsm_encoding_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[59];
}

static u64 access_rx_dma_eq_fsm_encoding_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[58];
}

static u64 access_rx_dma_csr_parity_err_cnt(const struct cntr_entry *entry,
					    void *context, int vl, int mode,
					    u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[57];
}

static u64 access_rx_rbuf_data_cor_err_cnt(const struct cntr_entry *entry,
					   void *context, int vl, int mode,
					   u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[56];
}

static u64 access_rx_rbuf_data_unc_err_cnt(const struct cntr_entry *entry,
					   void *context, int vl, int mode,
					   u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[55];
}

static u64 access_rx_dma_data_fifo_rd_cor_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[54];
}

static u64 access_rx_dma_data_fifo_rd_unc_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[53];
}

static u64 access_rx_dma_hdr_fifo_rd_cor_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[52];
}

static u64 access_rx_dma_hdr_fifo_rd_unc_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[51];
}

static u64 access_rx_rbuf_desc_part2_cor_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[50];
}

static u64 access_rx_rbuf_desc_part2_unc_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[49];
}

static u64 access_rx_rbuf_desc_part1_cor_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[48];
}

static u64 access_rx_rbuf_desc_part1_unc_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[47];
}

static u64 access_rx_hq_intr_fsm_err_cnt(const struct cntr_entry *entry,
					 void *context, int vl, int mode,
					 u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[46];
}

static u64 access_rx_hq_intr_csr_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[45];
}

static u64 access_rx_lookup_csr_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[44];
}

static u64 access_rx_lookup_rcv_array_cor_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[43];
}

static u64 access_rx_lookup_rcv_array_unc_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[42];
}

static u64 access_rx_lookup_des_part2_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[41];
}

static u64 access_rx_lookup_des_part1_unc_cor_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[40];
}

static u64 access_rx_lookup_des_part1_unc_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[39];
}

static u64 access_rx_rbuf_next_free_buf_cor_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[38];
}

static u64 access_rx_rbuf_next_free_buf_unc_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[37];
}

static u64 access_rbuf_fl_init_wr_addr_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[36];
}

static u64 access_rx_rbuf_fl_initdone_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[35];
}

static u64 access_rx_rbuf_fl_write_addr_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[34];
}

static u64 access_rx_rbuf_fl_rd_addr_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[33];
}

static u64 access_rx_rbuf_empty_err_cnt(const struct cntr_entry *entry,
					void *context, int vl, int mode,
					u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[32];
}

static u64 access_rx_rbuf_full_err_cnt(const struct cntr_entry *entry,
				       void *context, int vl, int mode,
				       u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[31];
}

static u64 access_rbuf_bad_lookup_err_cnt(const struct cntr_entry *entry,
					  void *context, int vl, int mode,
					  u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[30];
}

static u64 access_rbuf_ctx_id_parity_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[29];
}

static u64 access_rbuf_csr_qeopdw_parity_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[28];
}

static u64 access_rx_rbuf_csr_q_num_of_pkt_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[27];
}

static u64 access_rx_rbuf_csr_q_t1_ptr_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[26];
}

static u64 access_rx_rbuf_csr_q_hd_ptr_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[25];
}

static u64 access_rx_rbuf_csr_q_vld_bit_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[24];
}

static u64 access_rx_rbuf_csr_q_next_buf_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[23];
}

static u64 access_rx_rbuf_csr_q_ent_cnt_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[22];
}

static u64 access_rx_rbuf_csr_q_head_buf_num_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[21];
}

static u64 access_rx_rbuf_block_list_read_cor_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[20];
}

static u64 access_rx_rbuf_block_list_read_unc_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[19];
}

static u64 access_rx_rbuf_lookup_des_cor_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[18];
}

static u64 access_rx_rbuf_lookup_des_unc_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[17];
}

static u64 access_rx_rbuf_lookup_des_reg_unc_cor_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[16];
}

static u64 access_rx_rbuf_lookup_des_reg_unc_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[15];
}

static u64 access_rx_rbuf_free_list_cor_err_cnt(const struct cntr_entry *entry,
						void *context, int vl,
						int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[14];
}

static u64 access_rx_rbuf_free_list_unc_err_cnt(const struct cntr_entry *entry,
						void *context, int vl,
						int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[13];
}

static u64 access_rx_rcv_fsm_encoding_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[12];
}

static u64 access_rx_dma_flag_cor_err_cnt(const struct cntr_entry *entry,
					  void *context, int vl, int mode,
					  u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[11];
}

static u64 access_rx_dma_flag_unc_err_cnt(const struct cntr_entry *entry,
					  void *context, int vl, int mode,
					  u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[10];
}

static u64 access_rx_dc_sop_eop_parity_err_cnt(const struct cntr_entry *entry,
					       void *context, int vl, int mode,
					       u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[9];
}

static u64 access_rx_rcv_csr_parity_err_cnt(const struct cntr_entry *entry,
					    void *context, int vl, int mode,
					    u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[8];
}

static u64 access_rx_rcv_qp_map_table_cor_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[7];
}

static u64 access_rx_rcv_qp_map_table_unc_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[6];
}

static u64 access_rx_rcv_data_cor_err_cnt(const struct cntr_entry *entry,
					  void *context, int vl, int mode,
					  u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[5];
}

static u64 access_rx_rcv_data_unc_err_cnt(const struct cntr_entry *entry,
					  void *context, int vl, int mode,
					  u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[4];
}

static u64 access_rx_rcv_hdr_cor_err_cnt(const struct cntr_entry *entry,
					 void *context, int vl, int mode,
					 u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[3];
}

static u64 access_rx_rcv_hdr_unc_err_cnt(const struct cntr_entry *entry,
					 void *context, int vl, int mode,
					 u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[2];
}

static u64 access_rx_dc_intf_parity_err_cnt(const struct cntr_entry *entry,
					    void *context, int vl, int mode,
					    u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[1];
}

static u64 access_rx_dma_csr_cor_err_cnt(const struct cntr_entry *entry,
					 void *context, int vl, int mode,
					 u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->rcv_err_status_cnt[0];
}

/*
 * Software counters corresponding to each of the
 * error status bits within SendPioErrStatus
 */
static u64 access_pio_pec_sop_head_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[35];
}

static u64 access_pio_pcc_sop_head_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[34];
}

static u64 access_pio_last_returned_cnt_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[33];
}

static u64 access_pio_current_free_cnt_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[32];
}

static u64 access_pio_reserved_31_err_cnt(const struct cntr_entry *entry,
					  void *context, int vl, int mode,
					  u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[31];
}

static u64 access_pio_reserved_30_err_cnt(const struct cntr_entry *entry,
					  void *context, int vl, int mode,
					  u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[30];
}

static u64 access_pio_ppmc_sop_len_err_cnt(const struct cntr_entry *entry,
					   void *context, int vl, int mode,
					   u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[29];
}

static u64 access_pio_ppmc_bqc_mem_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[28];
}

static u64 access_pio_vl_fifo_parity_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[27];
}

static u64 access_pio_vlf_sop_parity_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[26];
}

static u64 access_pio_vlf_v1_len_parity_err_cnt(const struct cntr_entry *entry,
						void *context, int vl,
						int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[25];
}

static u64 access_pio_block_qw_count_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[24];
}

static u64 access_pio_write_qw_valid_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[23];
}

static u64 access_pio_state_machine_err_cnt(const struct cntr_entry *entry,
					    void *context, int vl, int mode,
					    u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[22];
}

static u64 access_pio_write_data_parity_err_cnt(const struct cntr_entry *entry,
						void *context, int vl,
						int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[21];
}

static u64 access_pio_host_addr_mem_cor_err_cnt(const struct cntr_entry *entry,
						void *context, int vl,
						int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[20];
}

static u64 access_pio_host_addr_mem_unc_err_cnt(const struct cntr_entry *entry,
						void *context, int vl,
						int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[19];
}

static u64 access_pio_pkt_evict_sm_or_arb_sm_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[18];
}

static u64 access_pio_init_sm_in_err_cnt(const struct cntr_entry *entry,
					 void *context, int vl, int mode,
					 u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[17];
}

static u64 access_pio_ppmc_pbl_fifo_err_cnt(const struct cntr_entry *entry,
					    void *context, int vl, int mode,
					    u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[16];
}

static u64 access_pio_credit_ret_fifo_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[15];
}

static u64 access_pio_v1_len_mem_bank1_cor_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[14];
}

static u64 access_pio_v1_len_mem_bank0_cor_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[13];
}

static u64 access_pio_v1_len_mem_bank1_unc_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[12];
}

static u64 access_pio_v1_len_mem_bank0_unc_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[11];
}

static u64 access_pio_sm_pkt_reset_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[10];
}

static u64 access_pio_pkt_evict_fifo_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[9];
}

static u64 access_pio_sbrdctrl_crrel_fifo_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[8];
}

static u64 access_pio_sbrdctl_crrel_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[7];
}

static u64 access_pio_pec_fifo_parity_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[6];
}

static u64 access_pio_pcc_fifo_parity_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[5];
}

static u64 access_pio_sb_mem_fifo1_err_cnt(const struct cntr_entry *entry,
					   void *context, int vl, int mode,
					   u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[4];
}

static u64 access_pio_sb_mem_fifo0_err_cnt(const struct cntr_entry *entry,
					   void *context, int vl, int mode,
					   u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[3];
}

static u64 access_pio_csr_parity_err_cnt(const struct cntr_entry *entry,
					 void *context, int vl, int mode,
					 u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[2];
}

static u64 access_pio_write_addr_parity_err_cnt(const struct cntr_entry *entry,
						void *context, int vl,
						int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[1];
}

static u64 access_pio_write_bad_ctxt_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_pio_err_status_cnt[0];
}

/*
 * Software counters corresponding to each of the
 * error status bits within SendDmaErrStatus
 */
static u64 access_sdma_pcie_req_tracking_cor_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_dma_err_status_cnt[3];
}

static u64 access_sdma_pcie_req_tracking_unc_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_dma_err_status_cnt[2];
}

static u64 access_sdma_csr_parity_err_cnt(const struct cntr_entry *entry,
					  void *context, int vl, int mode,
					  u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_dma_err_status_cnt[1];
}

static u64 access_sdma_rpy_tag_err_cnt(const struct cntr_entry *entry,
				       void *context, int vl, int mode,
				       u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_dma_err_status_cnt[0];
}

/*
 * Software counters corresponding to each of the
 * error status bits within SendEgressErrStatus
 */
static u64 access_tx_read_pio_memory_csr_unc_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[63];
}

static u64 access_tx_read_sdma_memory_csr_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[62];
}

static u64 access_tx_egress_fifo_cor_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[61];
}

static u64 access_tx_read_pio_memory_cor_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[60];
}

static u64 access_tx_read_sdma_memory_cor_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[59];
}

static u64 access_tx_sb_hdr_cor_err_cnt(const struct cntr_entry *entry,
					void *context, int vl, int mode,
					u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[58];
}

static u64 access_tx_credit_overrun_err_cnt(const struct cntr_entry *entry,
					    void *context, int vl, int mode,
					    u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[57];
}

static u64 access_tx_launch_fifo8_cor_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[56];
}

static u64 access_tx_launch_fifo7_cor_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[55];
}

static u64 access_tx_launch_fifo6_cor_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[54];
}

static u64 access_tx_launch_fifo5_cor_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[53];
}

static u64 access_tx_launch_fifo4_cor_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[52];
}

static u64 access_tx_launch_fifo3_cor_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[51];
}

static u64 access_tx_launch_fifo2_cor_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[50];
}

static u64 access_tx_launch_fifo1_cor_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[49];
}

static u64 access_tx_launch_fifo0_cor_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[48];
}

static u64 access_tx_credit_return_vl_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[47];
}

static u64 access_tx_hcrc_insertion_err_cnt(const struct cntr_entry *entry,
					    void *context, int vl, int mode,
					    u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[46];
}

static u64 access_tx_egress_fifo_unc_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[45];
}

static u64 access_tx_read_pio_memory_unc_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[44];
}

static u64 access_tx_read_sdma_memory_unc_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[43];
}

static u64 access_tx_sb_hdr_unc_err_cnt(const struct cntr_entry *entry,
					void *context, int vl, int mode,
					u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[42];
}

static u64 access_tx_credit_return_partiy_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[41];
}

static u64 access_tx_launch_fifo8_unc_or_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[40];
}

static u64 access_tx_launch_fifo7_unc_or_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[39];
}

static u64 access_tx_launch_fifo6_unc_or_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[38];
}

static u64 access_tx_launch_fifo5_unc_or_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[37];
}

static u64 access_tx_launch_fifo4_unc_or_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[36];
}

static u64 access_tx_launch_fifo3_unc_or_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[35];
}

static u64 access_tx_launch_fifo2_unc_or_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[34];
}

static u64 access_tx_launch_fifo1_unc_or_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[33];
}

static u64 access_tx_launch_fifo0_unc_or_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[32];
}

static u64 access_tx_sdma15_disallowed_packet_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[31];
}

static u64 access_tx_sdma14_disallowed_packet_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[30];
}

static u64 access_tx_sdma13_disallowed_packet_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[29];
}

static u64 access_tx_sdma12_disallowed_packet_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[28];
}

static u64 access_tx_sdma11_disallowed_packet_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[27];
}

static u64 access_tx_sdma10_disallowed_packet_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[26];
}

static u64 access_tx_sdma9_disallowed_packet_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[25];
}

static u64 access_tx_sdma8_disallowed_packet_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[24];
}

static u64 access_tx_sdma7_disallowed_packet_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[23];
}

static u64 access_tx_sdma6_disallowed_packet_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[22];
}

static u64 access_tx_sdma5_disallowed_packet_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[21];
}

static u64 access_tx_sdma4_disallowed_packet_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[20];
}

static u64 access_tx_sdma3_disallowed_packet_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[19];
}

static u64 access_tx_sdma2_disallowed_packet_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[18];
}

static u64 access_tx_sdma1_disallowed_packet_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[17];
}

static u64 access_tx_sdma0_disallowed_packet_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[16];
}

static u64 access_tx_config_parity_err_cnt(const struct cntr_entry *entry,
					   void *context, int vl, int mode,
					   u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[15];
}

static u64 access_tx_sbrd_ctl_csr_parity_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[14];
}

static u64 access_tx_launch_csr_parity_err_cnt(const struct cntr_entry *entry,
					       void *context, int vl, int mode,
					       u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[13];
}

static u64 access_tx_illegal_vl_err_cnt(const struct cntr_entry *entry,
					void *context, int vl, int mode,
					u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[12];
}

static u64 access_tx_sbrd_ctl_state_machine_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[11];
}

static u64 access_egress_reserved_10_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[10];
}

static u64 access_egress_reserved_9_err_cnt(const struct cntr_entry *entry,
					    void *context, int vl, int mode,
					    u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[9];
}

static u64 access_tx_sdma_launch_intf_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[8];
}

static u64 access_tx_pio_launch_intf_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[7];
}

static u64 access_egress_reserved_6_err_cnt(const struct cntr_entry *entry,
					    void *context, int vl, int mode,
					    u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[6];
}

static u64 access_tx_incorrect_link_state_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[5];
}

static u64 access_tx_linkdown_err_cnt(const struct cntr_entry *entry,
				      void *context, int vl, int mode,
				      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[4];
}

static u64 access_tx_egress_fifi_underrun_or_parity_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[3];
}

static u64 access_egress_reserved_2_err_cnt(const struct cntr_entry *entry,
					    void *context, int vl, int mode,
					    u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[2];
}

static u64 access_tx_pkt_integrity_mem_unc_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[1];
}

static u64 access_tx_pkt_integrity_mem_cor_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_egress_err_status_cnt[0];
}

/*
 * Software counters corresponding to each of the
 * error status bits within SendErrStatus
 */
static u64 access_send_csr_write_bad_addr_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_err_status_cnt[2];
}

static u64 access_send_csr_read_bad_addr_err_cnt(const struct cntr_entry *entry,
						 void *context, int vl,
						 int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_err_status_cnt[1];
}

static u64 access_send_csr_parity_cnt(const struct cntr_entry *entry,
				      void *context, int vl, int mode,
				      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->send_err_status_cnt[0];
}

/*
 * Software counters corresponding to each of the
 * error status bits within SendCtxtErrStatus
 */
static u64 access_pio_write_out_of_bounds_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_ctxt_err_status_cnt[4];
}

static u64 access_pio_write_overflow_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_ctxt_err_status_cnt[3];
}

static u64 access_pio_write_crosses_boundary_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_ctxt_err_status_cnt[2];
}

static u64 access_pio_disallowed_packet_err_cnt(const struct cntr_entry *entry,
						void *context, int vl,
						int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_ctxt_err_status_cnt[1];
}

static u64 access_pio_inconsistent_sop_err_cnt(const struct cntr_entry *entry,
					       void *context, int vl, int mode,
					       u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_ctxt_err_status_cnt[0];
}

/*
 * Software counters corresponding to each of the
 * error status bits within SendDmaEngErrStatus
 */
static u64 access_sdma_header_request_fifo_cor_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[23];
}

static u64 access_sdma_header_storage_cor_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[22];
}

static u64 access_sdma_packet_tracking_cor_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[21];
}

static u64 access_sdma_assembly_cor_err_cnt(const struct cntr_entry *entry,
					    void *context, int vl, int mode,
					    u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[20];
}

static u64 access_sdma_desc_table_cor_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[19];
}

static u64 access_sdma_header_request_fifo_unc_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[18];
}

static u64 access_sdma_header_storage_unc_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[17];
}

static u64 access_sdma_packet_tracking_unc_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[16];
}

static u64 access_sdma_assembly_unc_err_cnt(const struct cntr_entry *entry,
					    void *context, int vl, int mode,
					    u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[15];
}

static u64 access_sdma_desc_table_unc_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[14];
}

static u64 access_sdma_timeout_err_cnt(const struct cntr_entry *entry,
				       void *context, int vl, int mode,
				       u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[13];
}

static u64 access_sdma_header_length_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[12];
}

static u64 access_sdma_header_address_err_cnt(const struct cntr_entry *entry,
					      void *context, int vl, int mode,
					      u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[11];
}

static u64 access_sdma_header_select_err_cnt(const struct cntr_entry *entry,
					     void *context, int vl, int mode,
					     u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[10];
}

static u64 access_sdma_reserved_9_err_cnt(const struct cntr_entry *entry,
					  void *context, int vl, int mode,
					  u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[9];
}

static u64 access_sdma_packet_desc_overflow_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[8];
}

static u64 access_sdma_length_mismatch_err_cnt(const struct cntr_entry *entry,
					       void *context, int vl,
					       int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[7];
}

static u64 access_sdma_halt_err_cnt(const struct cntr_entry *entry,
				    void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[6];
}

static u64 access_sdma_mem_read_err_cnt(const struct cntr_entry *entry,
					void *context, int vl, int mode,
					u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[5];
}

static u64 access_sdma_first_desc_err_cnt(const struct cntr_entry *entry,
					  void *context, int vl, int mode,
					  u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[4];
}

static u64 access_sdma_tail_out_of_bounds_err_cnt(
				const struct cntr_entry *entry,
				void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[3];
}

static u64 access_sdma_too_long_err_cnt(const struct cntr_entry *entry,
					void *context, int vl, int mode,
					u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[2];
}

static u64 access_sdma_gen_mismatch_err_cnt(const struct cntr_entry *entry,
					    void *context, int vl, int mode,
					    u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[1];
}

static u64 access_sdma_wrong_dw_err_cnt(const struct cntr_entry *entry,
					void *context, int vl, int mode,
					u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	return dd->sw_send_dma_eng_err_status_cnt[0];
}

static u64 access_dc_rcv_err_cnt(const struct cntr_entry *entry,
				 void *context, int vl, int mode,
				 u64 data)
{
	struct hfi1_devdata *dd = (struct hfi1_devdata *)context;

	u64 val = 0;
	u64 csr = entry->csr;

	val = read_write_csr(dd, csr, mode, data);
	if (mode == CNTR_MODE_R) {
		val = val > CNTR_MAX - dd->sw_rcv_bypass_packet_errors ?
			CNTR_MAX : val + dd->sw_rcv_bypass_packet_errors;
	} else if (mode == CNTR_MODE_W) {
		dd->sw_rcv_bypass_packet_errors = 0;
	} else {
		dd_dev_err(dd, "Invalid cntr register access mode");
		return 0;
	}
	return val;
}

#define def_access_sw_cpu(cntr) \
static u64 access_sw_cpu_##cntr(const struct cntr_entry *entry,		      \
			      void *context, int vl, int mode, u64 data)      \
{									      \
	struct hfi1_pportdata *ppd = (struct hfi1_pportdata *)context;	      \
	return read_write_cpu(ppd->dd, &ppd->ibport_data.rvp.z_ ##cntr,	      \
			      ppd->ibport_data.rvp.cntr, vl,		      \
			      mode, data);				      \
}

def_access_sw_cpu(rc_acks);
def_access_sw_cpu(rc_qacks);
def_access_sw_cpu(rc_delayed_comp);

#define def_access_ibp_counter(cntr) \
static u64 access_ibp_##cntr(const struct cntr_entry *entry,		      \
				void *context, int vl, int mode, u64 data)    \
{									      \
	struct hfi1_pportdata *ppd = (struct hfi1_pportdata *)context;	      \
									      \
	if (vl != CNTR_INVALID_VL)					      \
		return 0;						      \
									      \
	return read_write_sw(ppd->dd, &ppd->ibport_data.rvp.n_ ##cntr,	      \
			     mode, data);				      \
}

def_access_ibp_counter(loop_pkts);
def_access_ibp_counter(rc_resends);
def_access_ibp_counter(rnr_naks);
def_access_ibp_counter(other_naks);
def_access_ibp_counter(rc_timeouts);
def_access_ibp_counter(pkt_drops);
def_access_ibp_counter(dmawait);
def_access_ibp_counter(rc_seqnak);
def_access_ibp_counter(rc_dupreq);
def_access_ibp_counter(rdma_seq);
def_access_ibp_counter(unaligned);
def_access_ibp_counter(seq_naks);
def_access_ibp_counter(rc_crwaits);

static struct cntr_entry dev_cntrs[DEV_CNTR_LAST] = {
[C_RCV_OVF] = RXE32_DEV_CNTR_ELEM(RcvOverflow, RCV_BUF_OVFL_CNT, CNTR_SYNTH),
[C_RX_LEN_ERR] = RXE32_DEV_CNTR_ELEM(RxLenErr, RCV_LENGTH_ERR_CNT, CNTR_SYNTH),
[C_RX_SHORT_ERR] = RXE32_DEV_CNTR_ELEM(RxShrErr, RCV_SHORT_ERR_CNT, CNTR_SYNTH),
[C_RX_ICRC_ERR] = RXE32_DEV_CNTR_ELEM(RxICrcErr, RCV_ICRC_ERR_CNT, CNTR_SYNTH),
[C_RX_EBP] = RXE32_DEV_CNTR_ELEM(RxEbpCnt, RCV_EBP_CNT, CNTR_SYNTH),
[C_RX_TID_FULL] = RXE32_DEV_CNTR_ELEM(RxTIDFullEr, RCV_TID_FULL_ERR_CNT,
			CNTR_NORMAL),
[C_RX_TID_INVALID] = RXE32_DEV_CNTR_ELEM(RxTIDInvalid, RCV_TID_VALID_ERR_CNT,
			CNTR_NORMAL),
[C_RX_TID_FLGMS] = RXE32_DEV_CNTR_ELEM(RxTidFLGMs,
			RCV_TID_FLOW_GEN_MISMATCH_CNT,
			CNTR_NORMAL),
[C_RX_CTX_EGRS] = RXE32_DEV_CNTR_ELEM(RxCtxEgrS, RCV_CONTEXT_EGR_STALL,
			CNTR_NORMAL),
[C_RCV_TID_FLSMS] = RXE32_DEV_CNTR_ELEM(RxTidFLSMs,
			RCV_TID_FLOW_SEQ_MISMATCH_CNT, CNTR_NORMAL),
[C_CCE_PCI_CR_ST] = CCE_PERF_DEV_CNTR_ELEM(CcePciCrSt,
			CCE_PCIE_POSTED_CRDT_STALL_CNT, CNTR_NORMAL),
[C_CCE_PCI_TR_ST] = CCE_PERF_DEV_CNTR_ELEM(CcePciTrSt, CCE_PCIE_TRGT_STALL_CNT,
			CNTR_NORMAL),
[C_CCE_PIO_WR_ST] = CCE_PERF_DEV_CNTR_ELEM(CcePioWrSt, CCE_PIO_WR_STALL_CNT,
			CNTR_NORMAL),
[C_CCE_ERR_INT] = CCE_INT_DEV_CNTR_ELEM(CceErrInt, CCE_ERR_INT_CNT,
			CNTR_NORMAL),
[C_CCE_SDMA_INT] = CCE_INT_DEV_CNTR_ELEM(CceSdmaInt, CCE_SDMA_INT_CNT,
			CNTR_NORMAL),
[C_CCE_MISC_INT] = CCE_INT_DEV_CNTR_ELEM(CceMiscInt, CCE_MISC_INT_CNT,
			CNTR_NORMAL),
[C_CCE_RCV_AV_INT] = CCE_INT_DEV_CNTR_ELEM(CceRcvAvInt, CCE_RCV_AVAIL_INT_CNT,
			CNTR_NORMAL),
[C_CCE_RCV_URG_INT] = CCE_INT_DEV_CNTR_ELEM(CceRcvUrgInt,
			CCE_RCV_URGENT_INT_CNT,	CNTR_NORMAL),
[C_CCE_SEND_CR_INT] = CCE_INT_DEV_CNTR_ELEM(CceSndCrInt,
			CCE_SEND_CREDIT_INT_CNT, CNTR_NORMAL),
[C_DC_UNC_ERR] = DC_PERF_CNTR(DcUnctblErr, DCC_ERR_UNCORRECTABLE_CNT,
			      CNTR_SYNTH),
[C_DC_RCV_ERR] = CNTR_ELEM("DcRecvErr", DCC_ERR_PORTRCV_ERR_CNT, 0, CNTR_SYNTH,
			    access_dc_rcv_err_cnt),
[C_DC_FM_CFG_ERR] = DC_PERF_CNTR(DcFmCfgErr, DCC_ERR_FMCONFIG_ERR_CNT,
				 CNTR_SYNTH),
[C_DC_RMT_PHY_ERR] = DC_PERF_CNTR(DcRmtPhyErr, DCC_ERR_RCVREMOTE_PHY_ERR_CNT,
				  CNTR_SYNTH),
[C_DC_DROPPED_PKT] = DC_PERF_CNTR(DcDroppedPkt, DCC_ERR_DROPPED_PKT_CNT,
				  CNTR_SYNTH),
[C_DC_MC_XMIT_PKTS] = DC_PERF_CNTR(DcMcXmitPkts,
				   DCC_PRF_PORT_XMIT_MULTICAST_CNT, CNTR_SYNTH),
[C_DC_MC_RCV_PKTS] = DC_PERF_CNTR(DcMcRcvPkts,
				  DCC_PRF_PORT_RCV_MULTICAST_PKT_CNT,
				  CNTR_SYNTH),
[C_DC_XMIT_CERR] = DC_PERF_CNTR(DcXmitCorr,
				DCC_PRF_PORT_XMIT_CORRECTABLE_CNT, CNTR_SYNTH),
[C_DC_RCV_CERR] = DC_PERF_CNTR(DcRcvCorrCnt, DCC_PRF_PORT_RCV_CORRECTABLE_CNT,
			       CNTR_SYNTH),
[C_DC_RCV_FCC] = DC_PERF_CNTR(DcRxFCntl, DCC_PRF_RX_FLOW_CRTL_CNT,
			      CNTR_SYNTH),
[C_DC_XMIT_FCC] = DC_PERF_CNTR(DcXmitFCntl, DCC_PRF_TX_FLOW_CRTL_CNT,
			       CNTR_SYNTH),
[C_DC_XMIT_FLITS] = DC_PERF_CNTR(DcXmitFlits, DCC_PRF_PORT_XMIT_DATA_CNT,
				 CNTR_SYNTH),
[C_DC_RCV_FLITS] = DC_PERF_CNTR(DcRcvFlits, DCC_PRF_PORT_RCV_DATA_CNT,
				CNTR_SYNTH),
[C_DC_XMIT_PKTS] = DC_PERF_CNTR(DcXmitPkts, DCC_PRF_PORT_XMIT_PKTS_CNT,
				CNTR_SYNTH),
[C_DC_RCV_PKTS] = DC_PERF_CNTR(DcRcvPkts, DCC_PRF_PORT_RCV_PKTS_CNT,
			       CNTR_SYNTH),
[C_DC_RX_FLIT_VL] = DC_PERF_CNTR(DcRxFlitVl, DCC_PRF_PORT_VL_RCV_DATA_CNT,
				 CNTR_SYNTH | CNTR_VL),
[C_DC_RX_PKT_VL] = DC_PERF_CNTR(DcRxPktVl, DCC_PRF_PORT_VL_RCV_PKTS_CNT,
				CNTR_SYNTH | CNTR_VL),
[C_DC_RCV_FCN] = DC_PERF_CNTR(DcRcvFcn, DCC_PRF_PORT_RCV_FECN_CNT, CNTR_SYNTH),
[C_DC_RCV_FCN_VL] = DC_PERF_CNTR(DcRcvFcnVl, DCC_PRF_PORT_VL_RCV_FECN_CNT,
				 CNTR_SYNTH | CNTR_VL),
[C_DC_RCV_BCN] = DC_PERF_CNTR(DcRcvBcn, DCC_PRF_PORT_RCV_BECN_CNT, CNTR_SYNTH),
[C_DC_RCV_BCN_VL] = DC_PERF_CNTR(DcRcvBcnVl, DCC_PRF_PORT_VL_RCV_BECN_CNT,
				 CNTR_SYNTH | CNTR_VL),
[C_DC_RCV_BBL] = DC_PERF_CNTR(DcRcvBbl, DCC_PRF_PORT_RCV_BUBBLE_CNT,
			      CNTR_SYNTH),
[C_DC_RCV_BBL_VL] = DC_PERF_CNTR(DcRcvBblVl, DCC_PRF_PORT_VL_RCV_BUBBLE_CNT,
				 CNTR_SYNTH | CNTR_VL),
[C_DC_MARK_FECN] = DC_PERF_CNTR(DcMarkFcn, DCC_PRF_PORT_MARK_FECN_CNT,
				CNTR_SYNTH),
[C_DC_MARK_FECN_VL] = DC_PERF_CNTR(DcMarkFcnVl, DCC_PRF_PORT_VL_MARK_FECN_CNT,
				   CNTR_SYNTH | CNTR_VL),
[C_DC_TOTAL_CRC] =
	DC_PERF_CNTR_LCB(DcTotCrc, DC_LCB_ERR_INFO_TOTAL_CRC_ERR,
			 CNTR_SYNTH),
[C_DC_CRC_LN0] = DC_PERF_CNTR_LCB(DcCrcLn0, DC_LCB_ERR_INFO_CRC_ERR_LN0,
				  CNTR_SYNTH),
[C_DC_CRC_LN1] = DC_PERF_CNTR_LCB(DcCrcLn1, DC_LCB_ERR_INFO_CRC_ERR_LN1,
				  CNTR_SYNTH),
[C_DC_CRC_LN2] = DC_PERF_CNTR_LCB(DcCrcLn2, DC_LCB_ERR_INFO_CRC_ERR_LN2,
				  CNTR_SYNTH),
[C_DC_CRC_LN3] = DC_PERF_CNTR_LCB(DcCrcLn3, DC_LCB_ERR_INFO_CRC_ERR_LN3,
				  CNTR_SYNTH),
[C_DC_CRC_MULT_LN] =
	DC_PERF_CNTR_LCB(DcMultLn, DC_LCB_ERR_INFO_CRC_ERR_MULTI_LN,
			 CNTR_SYNTH),
[C_DC_TX_REPLAY] = DC_PERF_CNTR_LCB(DcTxReplay, DC_LCB_ERR_INFO_TX_REPLAY_CNT,
				    CNTR_SYNTH),
[C_DC_RX_REPLAY] = DC_PERF_CNTR_LCB(DcRxReplay, DC_LCB_ERR_INFO_RX_REPLAY_CNT,
				    CNTR_SYNTH),
[C_DC_SEQ_CRC_CNT] =
	DC_PERF_CNTR_LCB(DcLinkSeqCrc, DC_LCB_ERR_INFO_SEQ_CRC_CNT,
			 CNTR_SYNTH),
[C_DC_ESC0_ONLY_CNT] =
	DC_PERF_CNTR_LCB(DcEsc0, DC_LCB_ERR_INFO_ESCAPE_0_ONLY_CNT,
			 CNTR_SYNTH),
[C_DC_ESC0_PLUS1_CNT] =
	DC_PERF_CNTR_LCB(DcEsc1, DC_LCB_ERR_INFO_ESCAPE_0_PLUS1_CNT,
			 CNTR_SYNTH),
[C_DC_ESC0_PLUS2_CNT] =
	DC_PERF_CNTR_LCB(DcEsc0Plus2, DC_LCB_ERR_INFO_ESCAPE_0_PLUS2_CNT,
			 CNTR_SYNTH),
[C_DC_REINIT_FROM_PEER_CNT] =
	DC_PERF_CNTR_LCB(DcReinitPeer, DC_LCB_ERR_INFO_REINIT_FROM_PEER_CNT,
			 CNTR_SYNTH),
[C_DC_SBE_CNT] = DC_PERF_CNTR_LCB(DcSbe, DC_LCB_ERR_INFO_SBE_CNT,
				  CNTR_SYNTH),
[C_DC_MISC_FLG_CNT] =
	DC_PERF_CNTR_LCB(DcMiscFlg, DC_LCB_ERR_INFO_MISC_FLG_CNT,
			 CNTR_SYNTH),
[C_DC_PRF_GOOD_LTP_CNT] =
	DC_PERF_CNTR_LCB(DcGoodLTP, DC_LCB_PRF_GOOD_LTP_CNT, CNTR_SYNTH),
[C_DC_PRF_ACCEPTED_LTP_CNT] =
	DC_PERF_CNTR_LCB(DcAccLTP, DC_LCB_PRF_ACCEPTED_LTP_CNT,
			 CNTR_SYNTH),
[C_DC_PRF_RX_FLIT_CNT] =
	DC_PERF_CNTR_LCB(DcPrfRxFlit, DC_LCB_PRF_RX_FLIT_CNT, CNTR_SYNTH),
[C_DC_PRF_TX_FLIT_CNT] =
	DC_PERF_CNTR_LCB(DcPrfTxFlit, DC_LCB_PRF_TX_FLIT_CNT, CNTR_SYNTH),
[C_DC_PRF_CLK_CNTR] =
	DC_PERF_CNTR_LCB(DcPrfClk, DC_LCB_PRF_CLK_CNTR, CNTR_SYNTH),
[C_DC_PG_DBG_FLIT_CRDTS_CNT] =
	DC_PERF_CNTR_LCB(DcFltCrdts, DC_LCB_PG_DBG_FLIT_CRDTS_CNT, CNTR_SYNTH),
[C_DC_PG_STS_PAUSE_COMPLETE_CNT] =
	DC_PERF_CNTR_LCB(DcPauseComp, DC_LCB_PG_STS_PAUSE_COMPLETE_CNT,
			 CNTR_SYNTH),
[C_DC_PG_STS_TX_SBE_CNT] =
	DC_PERF_CNTR_LCB(DcStsTxSbe, DC_LCB_PG_STS_TX_SBE_CNT, CNTR_SYNTH),
[C_DC_PG_STS_TX_MBE_CNT] =
	DC_PERF_CNTR_LCB(DcStsTxMbe, DC_LCB_PG_STS_TX_MBE_CNT,
			 CNTR_SYNTH),
[C_SW_CPU_INTR] = CNTR_ELEM("Intr", 0, 0, CNTR_NORMAL,
			    access_sw_cpu_intr),
[C_SW_CPU_RCV_LIM] = CNTR_ELEM("RcvLimit", 0, 0, CNTR_NORMAL,
			    access_sw_cpu_rcv_limit),
[C_SW_CTX0_SEQ_DROP] = CNTR_ELEM("SeqDrop0", 0, 0, CNTR_NORMAL,
			    access_sw_ctx0_seq_drop),
[C_SW_VTX_WAIT] = CNTR_ELEM("vTxWait", 0, 0, CNTR_NORMAL,
			    access_sw_vtx_wait),
[C_SW_PIO_WAIT] = CNTR_ELEM("PioWait", 0, 0, CNTR_NORMAL,
			    access_sw_pio_wait),
[C_SW_PIO_DRAIN] = CNTR_ELEM("PioDrain", 0, 0, CNTR_NORMAL,
			    access_sw_pio_drain),
[C_SW_KMEM_WAIT] = CNTR_ELEM("KmemWait", 0, 0, CNTR_NORMAL,
			    access_sw_kmem_wait),
[C_SW_TID_WAIT] = CNTR_ELEM("TidWait", 0, 0, CNTR_NORMAL,
			    hfi1_access_sw_tid_wait),
[C_SW_SEND_SCHED] = CNTR_ELEM("SendSched", 0, 0, CNTR_NORMAL,
			    access_sw_send_schedule),
[C_SDMA_DESC_FETCHED_CNT] = CNTR_ELEM("SDEDscFdCn",
				      SEND_DMA_DESC_FETCHED_CNT, 0,
				      CNTR_NORMAL | CNTR_32BIT | CNTR_SDMA,
				      dev_access_u32_csr),
[C_SDMA_INT_CNT] = CNTR_ELEM("SDMAInt", 0, 0,
			     CNTR_NORMAL | CNTR_32BIT | CNTR_SDMA,
			     access_sde_int_cnt),
[C_SDMA_ERR_CNT] = CNTR_ELEM("SDMAErrCt", 0, 0,
			     CNTR_NORMAL | CNTR_32BIT | CNTR_SDMA,
			     access_sde_err_cnt),
[C_SDMA_IDLE_INT_CNT] = CNTR_ELEM("SDMAIdInt", 0, 0,
				  CNTR_NORMAL | CNTR_32BIT | CNTR_SDMA,
				  access_sde_idle_int_cnt),
[C_SDMA_PROGRESS_INT_CNT] = CNTR_ELEM("SDMAPrIntCn", 0, 0,
				      CNTR_NORMAL | CNTR_32BIT | CNTR_SDMA,
				      access_sde_progress_int_cnt),
/* MISC_ERR_STATUS */
[C_MISC_PLL_LOCK_FAIL_ERR] = CNTR_ELEM("MISC_PLL_LOCK_FAIL_ERR", 0, 0,
				CNTR_NORMAL,
				access_misc_pll_lock_fail_err_cnt),
[C_MISC_MBIST_FAIL_ERR] = CNTR_ELEM("MISC_MBIST_FAIL_ERR", 0, 0,
				CNTR_NORMAL,
				access_misc_mbist_fail_err_cnt),
[C_MISC_INVALID_EEP_CMD_ERR] = CNTR_ELEM("MISC_INVALID_EEP_CMD_ERR", 0, 0,
				CNTR_NORMAL,
				access_misc_invalid_eep_cmd_err_cnt),
[C_MISC_EFUSE_DONE_PARITY_ERR] = CNTR_ELEM("MISC_EFUSE_DONE_PARITY_ERR", 0, 0,
				CNTR_NORMAL,
				access_misc_efuse_done_parity_err_cnt),
[C_MISC_EFUSE_WRITE_ERR] = CNTR_ELEM("MISC_EFUSE_WRITE_ERR", 0, 0,
				CNTR_NORMAL,
				access_misc_efuse_write_err_cnt),
[C_MISC_EFUSE_READ_BAD_ADDR_ERR] = CNTR_ELEM("MISC_EFUSE_READ_BAD_ADDR_ERR", 0,
				0, CNTR_NORMAL,
				access_misc_efuse_read_bad_addr_err_cnt),
[C_MISC_EFUSE_CSR_PARITY_ERR] = CNTR_ELEM("MISC_EFUSE_CSR_PARITY_ERR", 0, 0,
				CNTR_NORMAL,
				access_misc_efuse_csr_parity_err_cnt),
[C_MISC_FW_AUTH_FAILED_ERR] = CNTR_ELEM("MISC_FW_AUTH_FAILED_ERR", 0, 0,
				CNTR_NORMAL,
				access_misc_fw_auth_failed_err_cnt),
[C_MISC_KEY_MISMATCH_ERR] = CNTR_ELEM("MISC_KEY_MISMATCH_ERR", 0, 0,
				CNTR_NORMAL,
				access_misc_key_mismatch_err_cnt),
[C_MISC_SBUS_WRITE_FAILED_ERR] = CNTR_ELEM("MISC_SBUS_WRITE_FAILED_ERR", 0, 0,
				CNTR_NORMAL,
				access_misc_sbus_write_failed_err_cnt),
[C_MISC_CSR_WRITE_BAD_ADDR_ERR] = CNTR_ELEM("MISC_CSR_WRITE_BAD_ADDR_ERR", 0, 0,
				CNTR_NORMAL,
				access_misc_csr_write_bad_addr_err_cnt),
[C_MISC_CSR_READ_BAD_ADDR_ERR] = CNTR_ELEM("MISC_CSR_READ_BAD_ADDR_ERR", 0, 0,
				CNTR_NORMAL,
				access_misc_csr_read_bad_addr_err_cnt),
[C_MISC_CSR_PARITY_ERR] = CNTR_ELEM("MISC_CSR_PARITY_ERR", 0, 0,
				CNTR_NORMAL,
				access_misc_csr_parity_err_cnt),
/* CceErrStatus */
[C_CCE_ERR_STATUS_AGGREGATED_CNT] = CNTR_ELEM("CceErrStatusAggregatedCnt", 0, 0,
				CNTR_NORMAL,
				access_sw_cce_err_status_aggregated_cnt),
[C_CCE_MSIX_CSR_PARITY_ERR] = CNTR_ELEM("CceMsixCsrParityErr", 0, 0,
				CNTR_NORMAL,
				access_cce_msix_csr_parity_err_cnt),
[C_CCE_INT_MAP_UNC_ERR] = CNTR_ELEM("CceIntMapUncErr", 0, 0,
				CNTR_NORMAL,
				access_cce_int_map_unc_err_cnt),
[C_CCE_INT_MAP_COR_ERR] = CNTR_ELEM("CceIntMapCorErr", 0, 0,
				CNTR_NORMAL,
				access_cce_int_map_cor_err_cnt),
[C_CCE_MSIX_TABLE_UNC_ERR] = CNTR_ELEM("CceMsixTableUncErr", 0, 0,
				CNTR_NORMAL,
				access_cce_msix_table_unc_err_cnt),
[C_CCE_MSIX_TABLE_COR_ERR] = CNTR_ELEM("CceMsixTableCorErr", 0, 0,
				CNTR_NORMAL,
				access_cce_msix_table_cor_err_cnt),
[C_CCE_RXDMA_CONV_FIFO_PARITY_ERR] = CNTR_ELEM("CceRxdmaConvFifoParityErr", 0,
				0, CNTR_NORMAL,
				access_cce_rxdma_conv_fifo_parity_err_cnt),
[C_CCE_RCPL_ASYNC_FIFO_PARITY_ERR] = CNTR_ELEM("CceRcplAsyncFifoParityErr", 0,
				0, CNTR_NORMAL,
				access_cce_rcpl_async_fifo_parity_err_cnt),
[C_CCE_SEG_WRITE_BAD_ADDR_ERR] = CNTR_ELEM("CceSegWriteBadAddrErr", 0, 0,
				CNTR_NORMAL,
				access_cce_seg_write_bad_addr_err_cnt),
[C_CCE_SEG_READ_BAD_ADDR_ERR] = CNTR_ELEM("CceSegReadBadAddrErr", 0, 0,
				CNTR_NORMAL,
				access_cce_seg_read_bad_addr_err_cnt),
[C_LA_TRIGGERED] = CNTR_ELEM("Cce LATriggered", 0, 0,
				CNTR_NORMAL,
				access_la_triggered_cnt),
[C_CCE_TRGT_CPL_TIMEOUT_ERR] = CNTR_ELEM("CceTrgtCplTimeoutErr", 0, 0,
				CNTR_NORMAL,
				access_cce_trgt_cpl_timeout_err_cnt),
[C_PCIC_RECEIVE_PARITY_ERR] = CNTR_ELEM("PcicReceiveParityErr", 0, 0,
				CNTR_NORMAL,
				access_pcic_receive_parity_err_cnt),
[C_PCIC_TRANSMIT_BACK_PARITY_ERR] = CNTR_ELEM("PcicTransmitBackParityErr", 0, 0,
				CNTR_NORMAL,
				access_pcic_transmit_back_parity_err_cnt),
[C_PCIC_TRANSMIT_FRONT_PARITY_ERR] = CNTR_ELEM("PcicTransmitFrontParityErr", 0,
				0, CNTR_NORMAL,
				access_pcic_transmit_front_parity_err_cnt),
[C_PCIC_CPL_DAT_Q_UNC_ERR] = CNTR_ELEM("PcicCplDatQUncErr", 0, 0,
				CNTR_NORMAL,
				access_pcic_cpl_dat_q_unc_err_cnt),
[C_PCIC_CPL_HD_Q_UNC_ERR] = CNTR_ELEM("PcicCplHdQUncErr", 0, 0,
				CNTR_NORMAL,
				access_pcic_cpl_hd_q_unc_err_cnt),
[C_PCIC_POST_DAT_Q_UNC_ERR] = CNTR_ELEM("PcicPostDatQUncErr", 0, 0,
				CNTR_NORMAL,
				access_pcic_post_dat_q_unc_err_cnt),
[C_PCIC_POST_HD_Q_UNC_ERR] = CNTR_ELEM("PcicPostHdQUncErr", 0, 0,
				CNTR_NORMAL,
				access_pcic_post_hd_q_unc_err_cnt),
[C_PCIC_RETRY_SOT_MEM_UNC_ERR] = CNTR_ELEM("PcicRetrySotMemUncErr", 0, 0,
				CNTR_NORMAL,
				access_pcic_retry_sot_mem_unc_err_cnt),
[C_PCIC_RETRY_MEM_UNC_ERR] = CNTR_ELEM("PcicRetryMemUncErr", 0, 0,
				CNTR_NORMAL,
				access_pcic_retry_mem_unc_err),
[C_PCIC_N_POST_DAT_Q_PARITY_ERR] = CNTR_ELEM("PcicNPostDatQParityErr", 0, 0,
				CNTR_NORMAL,
				access_pcic_n_post_dat_q_parity_err_cnt),
[C_PCIC_N_POST_H_Q_PARITY_ERR] = CNTR_ELEM("PcicNPostHQParityErr", 0, 0,
				CNTR_NORMAL,
				access_pcic_n_post_h_q_parity_err_cnt),
[C_PCIC_CPL_DAT_Q_COR_ERR] = CNTR_ELEM("PcicCplDatQCorErr", 0, 0,
				CNTR_NORMAL,
				access_pcic_cpl_dat_q_cor_err_cnt),
[C_PCIC_CPL_HD_Q_COR_ERR] = CNTR_ELEM("PcicCplHdQCorErr", 0, 0,
				CNTR_NORMAL,
				access_pcic_cpl_hd_q_cor_err_cnt),
[C_PCIC_POST_DAT_Q_COR_ERR] = CNTR_ELEM("PcicPostDatQCorErr", 0, 0,
				CNTR_NORMAL,
				access_pcic_post_dat_q_cor_err_cnt),
[C_PCIC_POST_HD_Q_COR_ERR] = CNTR_ELEM("PcicPostHdQCorErr", 0, 0,
				CNTR_NORMAL,
				access_pcic_post_hd_q_cor_err_cnt),
[C_PCIC_RETRY_SOT_MEM_COR_ERR] = CNTR_ELEM("PcicRetrySotMemCorErr", 0, 0,
				CNTR_NORMAL,
				access_pcic_retry_sot_mem_cor_err_cnt),
[C_PCIC_RETRY_MEM_COR_ERR] = CNTR_ELEM("PcicRetryMemCorErr", 0, 0,
				CNTR_NORMAL,
				access_pcic_retry_mem_cor_err_cnt),
[C_CCE_CLI1_ASYNC_FIFO_DBG_PARITY_ERR] = CNTR_ELEM(
				"CceCli1AsyncFifoDbgParityError", 0, 0,
				CNTR_NORMAL,
				access_cce_cli1_async_fifo_dbg_parity_err_cnt),
[C_CCE_CLI1_ASYNC_FIFO_RXDMA_PARITY_ERR] = CNTR_ELEM(
				"CceCli1AsyncFifoRxdmaParityError", 0, 0,
				CNTR_NORMAL,
				access_cce_cli1_async_fifo_rxdma_parity_err_cnt
				),
[C_CCE_CLI1_ASYNC_FIFO_SDMA_HD_PARITY_ERR] = CNTR_ELEM(
			"CceCli1AsyncFifoSdmaHdParityErr", 0, 0,
			CNTR_NORMAL,
			access_cce_cli1_async_fifo_sdma_hd_parity_err_cnt),
[C_CCE_CLI1_ASYNC_FIFO_PIO_CRDT_PARITY_ERR] = CNTR_ELEM(
			"CceCli1AsyncFifoPioCrdtParityErr", 0, 0,
			CNTR_NORMAL,
			access_cce_cl1_async_fifo_pio_crdt_parity_err_cnt),
[C_CCE_CLI2_ASYNC_FIFO_PARITY_ERR] = CNTR_ELEM("CceCli2AsyncFifoParityErr", 0,
			0, CNTR_NORMAL,
			access_cce_cli2_async_fifo_parity_err_cnt),
[C_CCE_CSR_CFG_BUS_PARITY_ERR] = CNTR_ELEM("CceCsrCfgBusParityErr", 0, 0,
			CNTR_NORMAL,
			access_cce_csr_cfg_bus_parity_err_cnt),
[C_CCE_CLI0_ASYNC_FIFO_PARTIY_ERR] = CNTR_ELEM("CceCli0AsyncFifoParityErr", 0,
			0, CNTR_NORMAL,
			access_cce_cli0_async_fifo_parity_err_cnt),
[C_CCE_RSPD_DATA_PARITY_ERR] = CNTR_ELEM("CceRspdDataParityErr", 0, 0,
			CNTR_NORMAL,
			access_cce_rspd_data_parity_err_cnt),
[C_CCE_TRGT_ACCESS_ERR] = CNTR_ELEM("CceTrgtAccessErr", 0, 0,
			CNTR_NORMAL,
			access_cce_trgt_access_err_cnt),
[C_CCE_TRGT_ASYNC_FIFO_PARITY_ERR] = CNTR_ELEM("CceTrgtAsyncFifoParityErr", 0,
			0, CNTR_NORMAL,
			access_cce_trgt_async_fifo_parity_err_cnt),
[C_CCE_CSR_WRITE_BAD_ADDR_ERR] = CNTR_ELEM("CceCsrWriteBadAddrErr", 0, 0,
			CNTR_NORMAL,
			access_cce_csr_write_bad_addr_err_cnt),
[C_CCE_CSR_READ_BAD_ADDR_ERR] = CNTR_ELEM("CceCsrReadBadAddrErr", 0, 0,
			CNTR_NORMAL,
			access_cce_csr_read_bad_addr_err_cnt),
[C_CCE_CSR_PARITY_ERR] = CNTR_ELEM("CceCsrParityErr", 0, 0,
			CNTR_NORMAL,
			access_ccs_csr_parity_err_cnt),

/* RcvErrStatus */
[C_RX_CSR_PARITY_ERR] = CNTR_ELEM("RxCsrParityErr", 0, 0,
			CNTR_NORMAL,
			access_rx_csr_parity_err_cnt),
[C_RX_CSR_WRITE_BAD_ADDR_ERR] = CNTR_ELEM("RxCsrWriteBadAddrErr", 0, 0,
			CNTR_NORMAL,
			access_rx_csr_write_bad_addr_err_cnt),
[C_RX_CSR_READ_BAD_ADDR_ERR] = CNTR_ELEM("RxCsrReadBadAddrErr", 0, 0,
			CNTR_NORMAL,
			access_rx_csr_read_bad_addr_err_cnt),
[C_RX_DMA_CSR_UNC_ERR] = CNTR_ELEM("RxDmaCsrUncErr", 0, 0,
			CNTR_NORMAL,
			access_rx_dma_csr_unc_err_cnt),
[C_RX_DMA_DQ_FSM_ENCODING_ERR] = CNTR_ELEM("RxDmaDqFsmEncodingErr", 0, 0,
			CNTR_NORMAL,
			access_rx_dma_dq_fsm_encoding_err_cnt),
[C_RX_DMA_EQ_FSM_ENCODING_ERR] = CNTR_ELEM("RxDmaEqFsmEncodingErr", 0, 0,
			CNTR_NORMAL,
			access_rx_dma_eq_fsm_encoding_err_cnt),
[C_RX_DMA_CSR_PARITY_ERR] = CNTR_ELEM("RxDmaCsrParityErr", 0, 0,
			CNTR_NORMAL,
			access_rx_dma_csr_parity_err_cnt),
[C_RX_RBUF_DATA_COR_ERR] = CNTR_ELEM("RxRbufDataCorErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_data_cor_err_cnt),
[C_RX_RBUF_DATA_UNC_ERR] = CNTR_ELEM("RxRbufDataUncErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_data_unc_err_cnt),
[C_RX_DMA_DATA_FIFO_RD_COR_ERR] = CNTR_ELEM("RxDmaDataFifoRdCorErr", 0, 0,
			CNTR_NORMAL,
			access_rx_dma_data_fifo_rd_cor_err_cnt),
[C_RX_DMA_DATA_FIFO_RD_UNC_ERR] = CNTR_ELEM("RxDmaDataFifoRdUncErr", 0, 0,
			CNTR_NORMAL,
			access_rx_dma_data_fifo_rd_unc_err_cnt),
[C_RX_DMA_HDR_FIFO_RD_COR_ERR] = CNTR_ELEM("RxDmaHdrFifoRdCorErr", 0, 0,
			CNTR_NORMAL,
			access_rx_dma_hdr_fifo_rd_cor_err_cnt),
[C_RX_DMA_HDR_FIFO_RD_UNC_ERR] = CNTR_ELEM("RxDmaHdrFifoRdUncErr", 0, 0,
			CNTR_NORMAL,
			access_rx_dma_hdr_fifo_rd_unc_err_cnt),
[C_RX_RBUF_DESC_PART2_COR_ERR] = CNTR_ELEM("RxRbufDescPart2CorErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_desc_part2_cor_err_cnt),
[C_RX_RBUF_DESC_PART2_UNC_ERR] = CNTR_ELEM("RxRbufDescPart2UncErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_desc_part2_unc_err_cnt),
[C_RX_RBUF_DESC_PART1_COR_ERR] = CNTR_ELEM("RxRbufDescPart1CorErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_desc_part1_cor_err_cnt),
[C_RX_RBUF_DESC_PART1_UNC_ERR] = CNTR_ELEM("RxRbufDescPart1UncErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_desc_part1_unc_err_cnt),
[C_RX_HQ_INTR_FSM_ERR] = CNTR_ELEM("RxHqIntrFsmErr", 0, 0,
			CNTR_NORMAL,
			access_rx_hq_intr_fsm_err_cnt),
[C_RX_HQ_INTR_CSR_PARITY_ERR] = CNTR_ELEM("RxHqIntrCsrParityErr", 0, 0,
			CNTR_NORMAL,
			access_rx_hq_intr_csr_parity_err_cnt),
[C_RX_LOOKUP_CSR_PARITY_ERR] = CNTR_ELEM("RxLookupCsrParityErr", 0, 0,
			CNTR_NORMAL,
			access_rx_lookup_csr_parity_err_cnt),
[C_RX_LOOKUP_RCV_ARRAY_COR_ERR] = CNTR_ELEM("RxLookupRcvArrayCorErr", 0, 0,
			CNTR_NORMAL,
			access_rx_lookup_rcv_array_cor_err_cnt),
[C_RX_LOOKUP_RCV_ARRAY_UNC_ERR] = CNTR_ELEM("RxLookupRcvArrayUncErr", 0, 0,
			CNTR_NORMAL,
			access_rx_lookup_rcv_array_unc_err_cnt),
[C_RX_LOOKUP_DES_PART2_PARITY_ERR] = CNTR_ELEM("RxLookupDesPart2ParityErr", 0,
			0, CNTR_NORMAL,
			access_rx_lookup_des_part2_parity_err_cnt),
[C_RX_LOOKUP_DES_PART1_UNC_COR_ERR] = CNTR_ELEM("RxLookupDesPart1UncCorErr", 0,
			0, CNTR_NORMAL,
			access_rx_lookup_des_part1_unc_cor_err_cnt),
[C_RX_LOOKUP_DES_PART1_UNC_ERR] = CNTR_ELEM("RxLookupDesPart1UncErr", 0, 0,
			CNTR_NORMAL,
			access_rx_lookup_des_part1_unc_err_cnt),
[C_RX_RBUF_NEXT_FREE_BUF_COR_ERR] = CNTR_ELEM("RxRbufNextFreeBufCorErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_next_free_buf_cor_err_cnt),
[C_RX_RBUF_NEXT_FREE_BUF_UNC_ERR] = CNTR_ELEM("RxRbufNextFreeBufUncErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_next_free_buf_unc_err_cnt),
[C_RX_RBUF_FL_INIT_WR_ADDR_PARITY_ERR] = CNTR_ELEM(
			"RxRbufFlInitWrAddrParityErr", 0, 0,
			CNTR_NORMAL,
			access_rbuf_fl_init_wr_addr_parity_err_cnt),
[C_RX_RBUF_FL_INITDONE_PARITY_ERR] = CNTR_ELEM("RxRbufFlInitdoneParityErr", 0,
			0, CNTR_NORMAL,
			access_rx_rbuf_fl_initdone_parity_err_cnt),
[C_RX_RBUF_FL_WRITE_ADDR_PARITY_ERR] = CNTR_ELEM("RxRbufFlWrAddrParityErr", 0,
			0, CNTR_NORMAL,
			access_rx_rbuf_fl_write_addr_parity_err_cnt),
[C_RX_RBUF_FL_RD_ADDR_PARITY_ERR] = CNTR_ELEM("RxRbufFlRdAddrParityErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_fl_rd_addr_parity_err_cnt),
[C_RX_RBUF_EMPTY_ERR] = CNTR_ELEM("RxRbufEmptyErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_empty_err_cnt),
[C_RX_RBUF_FULL_ERR] = CNTR_ELEM("RxRbufFullErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_full_err_cnt),
[C_RX_RBUF_BAD_LOOKUP_ERR] = CNTR_ELEM("RxRBufBadLookupErr", 0, 0,
			CNTR_NORMAL,
			access_rbuf_bad_lookup_err_cnt),
[C_RX_RBUF_CTX_ID_PARITY_ERR] = CNTR_ELEM("RxRbufCtxIdParityErr", 0, 0,
			CNTR_NORMAL,
			access_rbuf_ctx_id_parity_err_cnt),
[C_RX_RBUF_CSR_QEOPDW_PARITY_ERR] = CNTR_ELEM("RxRbufCsrQEOPDWParityErr", 0, 0,
			CNTR_NORMAL,
			access_rbuf_csr_qeopdw_parity_err_cnt),
[C_RX_RBUF_CSR_Q_NUM_OF_PKT_PARITY_ERR] = CNTR_ELEM(
			"RxRbufCsrQNumOfPktParityErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_csr_q_num_of_pkt_parity_err_cnt),
[C_RX_RBUF_CSR_Q_T1_PTR_PARITY_ERR] = CNTR_ELEM(
			"RxRbufCsrQTlPtrParityErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_csr_q_t1_ptr_parity_err_cnt),
[C_RX_RBUF_CSR_Q_HD_PTR_PARITY_ERR] = CNTR_ELEM("RxRbufCsrQHdPtrParityErr", 0,
			0, CNTR_NORMAL,
			access_rx_rbuf_csr_q_hd_ptr_parity_err_cnt),
[C_RX_RBUF_CSR_Q_VLD_BIT_PARITY_ERR] = CNTR_ELEM("RxRbufCsrQVldBitParityErr", 0,
			0, CNTR_NORMAL,
			access_rx_rbuf_csr_q_vld_bit_parity_err_cnt),
[C_RX_RBUF_CSR_Q_NEXT_BUF_PARITY_ERR] = CNTR_ELEM("RxRbufCsrQNextBufParityErr",
			0, 0, CNTR_NORMAL,
			access_rx_rbuf_csr_q_next_buf_parity_err_cnt),
[C_RX_RBUF_CSR_Q_ENT_CNT_PARITY_ERR] = CNTR_ELEM("RxRbufCsrQEntCntParityErr", 0,
			0, CNTR_NORMAL,
			access_rx_rbuf_csr_q_ent_cnt_parity_err_cnt),
[C_RX_RBUF_CSR_Q_HEAD_BUF_NUM_PARITY_ERR] = CNTR_ELEM(
			"RxRbufCsrQHeadBufNumParityErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_csr_q_head_buf_num_parity_err_cnt),
[C_RX_RBUF_BLOCK_LIST_READ_COR_ERR] = CNTR_ELEM("RxRbufBlockListReadCorErr", 0,
			0, CNTR_NORMAL,
			access_rx_rbuf_block_list_read_cor_err_cnt),
[C_RX_RBUF_BLOCK_LIST_READ_UNC_ERR] = CNTR_ELEM("RxRbufBlockListReadUncErr", 0,
			0, CNTR_NORMAL,
			access_rx_rbuf_block_list_read_unc_err_cnt),
[C_RX_RBUF_LOOKUP_DES_COR_ERR] = CNTR_ELEM("RxRbufLookupDesCorErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_lookup_des_cor_err_cnt),
[C_RX_RBUF_LOOKUP_DES_UNC_ERR] = CNTR_ELEM("RxRbufLookupDesUncErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_lookup_des_unc_err_cnt),
[C_RX_RBUF_LOOKUP_DES_REG_UNC_COR_ERR] = CNTR_ELEM(
			"RxRbufLookupDesRegUncCorErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_lookup_des_reg_unc_cor_err_cnt),
[C_RX_RBUF_LOOKUP_DES_REG_UNC_ERR] = CNTR_ELEM("RxRbufLookupDesRegUncErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_lookup_des_reg_unc_err_cnt),
[C_RX_RBUF_FREE_LIST_COR_ERR] = CNTR_ELEM("RxRbufFreeListCorErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_free_list_cor_err_cnt),
[C_RX_RBUF_FREE_LIST_UNC_ERR] = CNTR_ELEM("RxRbufFreeListUncErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rbuf_free_list_unc_err_cnt),
[C_RX_RCV_FSM_ENCODING_ERR] = CNTR_ELEM("RxRcvFsmEncodingErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rcv_fsm_encoding_err_cnt),
[C_RX_DMA_FLAG_COR_ERR] = CNTR_ELEM("RxDmaFlagCorErr", 0, 0,
			CNTR_NORMAL,
			access_rx_dma_flag_cor_err_cnt),
[C_RX_DMA_FLAG_UNC_ERR] = CNTR_ELEM("RxDmaFlagUncErr", 0, 0,
			CNTR_NORMAL,
			access_rx_dma_flag_unc_err_cnt),
[C_RX_DC_SOP_EOP_PARITY_ERR] = CNTR_ELEM("RxDcSopEopParityErr", 0, 0,
			CNTR_NORMAL,
			access_rx_dc_sop_eop_parity_err_cnt),
[C_RX_RCV_CSR_PARITY_ERR] = CNTR_ELEM("RxRcvCsrParityErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rcv_csr_parity_err_cnt),
[C_RX_RCV_QP_MAP_TABLE_COR_ERR] = CNTR_ELEM("RxRcvQpMapTableCorErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rcv_qp_map_table_cor_err_cnt),
[C_RX_RCV_QP_MAP_TABLE_UNC_ERR] = CNTR_ELEM("RxRcvQpMapTableUncErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rcv_qp_map_table_unc_err_cnt),
[C_RX_RCV_DATA_COR_ERR] = CNTR_ELEM("RxRcvDataCorErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rcv_data_cor_err_cnt),
[C_RX_RCV_DATA_UNC_ERR] = CNTR_ELEM("RxRcvDataUncErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rcv_data_unc_err_cnt),
[C_RX_RCV_HDR_COR_ERR] = CNTR_ELEM("RxRcvHdrCorErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rcv_hdr_cor_err_cnt),
[C_RX_RCV_HDR_UNC_ERR] = CNTR_ELEM("RxRcvHdrUncErr", 0, 0,
			CNTR_NORMAL,
			access_rx_rcv_hdr_unc_err_cnt),
[C_RX_DC_INTF_PARITY_ERR] = CNTR_ELEM("RxDcIntfParityErr", 0, 0,
			CNTR_NORMAL,
			access_rx_dc_intf_parity_err_cnt),
[C_RX_DMA_CSR_COR_ERR] = CNTR_ELEM("RxDmaCsrCorErr", 0, 0,
			CNTR_NORMAL,
			access_rx_dma_csr_cor_err_cnt),
/* SendPioErrStatus */
[C_PIO_PEC_SOP_HEAD_PARITY_ERR] = CNTR_ELEM("PioPecSopHeadParityErr", 0, 0,
			CNTR_NORMAL,
			access_pio_pec_sop_head_parity_err_cnt),
[C_PIO_PCC_SOP_HEAD_PARITY_ERR] = CNTR_ELEM("PioPccSopHeadParityErr", 0, 0,
			CNTR_NORMAL,
			access_pio_pcc_sop_head_parity_err_cnt),
[C_PIO_LAST_RETURNED_CNT_PARITY_ERR] = CNTR_ELEM("PioLastReturnedCntParityErr",
			0, 0, CNTR_NORMAL,
			access_pio_last_returned_cnt_parity_err_cnt),
[C_PIO_CURRENT_FREE_CNT_PARITY_ERR] = CNTR_ELEM("PioCurrentFreeCntParityErr", 0,
			0, CNTR_NORMAL,
			access_pio_current_free_cnt_parity_err_cnt),
[C_PIO_RSVD_31_ERR] = CNTR_ELEM("Pio Reserved 31", 0, 0,
			CNTR_NORMAL,
			access_pio_reserved_31_err_cnt),
[C_PIO_RSVD_30_ERR] = CNTR_ELEM("Pio Reserved 30", 0, 0,
			CNTR_NORMAL,
			access_pio_reserved_30_err_cnt),
[C_PIO_PPMC_SOP_LEN_ERR] = CNTR_ELEM("PioPpmcSopLenErr", 0, 0,
			CNTR_NORMAL,
			access_pio_ppmc_sop_len_err_cnt),
[C_PIO_PPMC_BQC_MEM_PARITY_ERR] = CNTR_ELEM("PioPpmcBqcMemParityErr", 0, 0,
			CNTR_NORMAL,
			access_pio_ppmc_bqc_mem_parity_err_cnt),
[C_PIO_VL_FIFO_PARITY_ERR] = CNTR_ELEM("PioVlFifoParityErr", 0, 0,
			CNTR_NORMAL,
			access_pio_vl_fifo_parity_err_cnt),
[C_PIO_VLF_SOP_PARITY_ERR] = CNTR_ELEM("PioVlfSopParityErr", 0, 0,
			CNTR_NORMAL,
			access_pio_vlf_sop_parity_err_cnt),
[C_PIO_VLF_V1_LEN_PARITY_ERR] = CNTR_ELEM("PioVlfVlLenParityErr", 0, 0,
			CNTR_NORMAL,
			access_pio_vlf_v1_len_parity_err_cnt),
[C_PIO_BLOCK_QW_COUNT_PARITY_ERR] = CNTR_ELEM("PioBlockQwCountParityErr", 0, 0,
			CNTR_NORMAL,
			access_pio_block_qw_count_parity_err_cnt),
[C_PIO_WRITE_QW_VALID_PARITY_ERR] = CNTR_ELEM("PioWriteQwValidParityErr", 0, 0,
			CNTR_NORMAL,
			access_pio_write_qw_valid_parity_err_cnt),
[C_PIO_STATE_MACHINE_ERR] = CNTR_ELEM("PioStateMachineErr", 0, 0,
			CNTR_NORMAL,
			access_pio_state_machine_err_cnt),
[C_PIO_WRITE_DATA_PARITY_ERR] = CNTR_ELEM("PioWriteDataParityErr", 0, 0,
			CNTR_NORMAL,
			access_pio_write_data_parity_err_cnt),
[C_PIO_HOST_ADDR_MEM_COR_ERR] = CNTR_ELEM("PioHostAddrMemCorErr", 0, 0,
			CNTR_NORMAL,
			access_pio_host_addr_mem_cor_err_cnt),
[C_PIO_HOST_ADDR_MEM_UNC_ERR] = CNTR_ELEM("PioHostAddrMemUncErr", 0, 0,
			CNTR_NORMAL,
			access_pio_host_addr_mem_unc_err_cnt),
[C_PIO_PKT_EVICT_SM_OR_ARM_SM_ERR] = CNTR_ELEM("PioPktEvictSmOrArbSmErr", 0, 0,
			CNTR_NORMAL,
			access_pio_pkt_evict_sm_or_arb_sm_err_cnt),
[C_PIO_INIT_SM_IN_ERR] = CNTR_ELEM("PioInitSmInErr", 0, 0,
			CNTR_NORMAL,
			access_pio_init_sm_in_err_cnt),
[C_PIO_PPMC_PBL_FIFO_ERR] = CNTR_ELEM("PioPpmcPblFifoErr", 0, 0,
			CNTR_NORMAL,
			access_pio_ppmc_pbl_fifo_err_cnt),
[C_PIO_CREDIT_RET_FIFO_PARITY_ERR] = CNTR_ELEM("PioCreditRetFifoParityErr", 0,
			0, CNTR_NORMAL,
			access_pio_credit_ret_fifo_parity_err_cnt),
[C_PIO_V1_LEN_MEM_BANK1_COR_ERR] = CNTR_ELEM("PioVlLenMemBank1CorErr", 0, 0,
			CNTR_NORMAL,
			access_pio_v1_len_mem_bank1_cor_err_cnt),
[C_PIO_V1_LEN_MEM_BANK0_COR_ERR] = CNTR_ELEM("PioVlLenMemBank0CorErr", 0, 0,
			CNTR_NORMAL,
			access_pio_v1_len_mem_bank0_cor_err_cnt),
[C_PIO_V1_LEN_MEM_BANK1_UNC_ERR] = CNTR_ELEM("PioVlLenMemBank1UncErr", 0, 0,
			CNTR_NORMAL,
			access_pio_v1_len_mem_bank1_unc_err_cnt),
[C_PIO_V1_LEN_MEM_BANK0_UNC_ERR] = CNTR_ELEM("PioVlLenMemBank0UncErr", 0, 0,
			CNTR_NORMAL,
			access_pio_v1_len_mem_bank0_unc_err_cnt),
[C_PIO_SM_PKT_RESET_PARITY_ERR] = CNTR_ELEM("PioSmPktResetParityErr", 0, 0,
			CNTR_NORMAL,
			access_pio_sm_pkt_reset_parity_err_cnt),
[C_PIO_PKT_EVICT_FIFO_PARITY_ERR] = CNTR_ELEM("PioPktEvictFifoParityErr", 0, 0,
			CNTR_NORMAL,
			access_pio_pkt_evict_fifo_parity_err_cnt),
[C_PIO_SBRDCTRL_CRREL_FIFO_PARITY_ERR] = CNTR_ELEM(
			"PioSbrdctrlCrrelFifoParityErr", 0, 0,
			CNTR_NORMAL,
			access_pio_sbrdctrl_crrel_fifo_parity_err_cnt),
[C_PIO_SBRDCTL_CRREL_PARITY_ERR] = CNTR_ELEM("PioSbrdctlCrrelParityErr", 0, 0,
			CNTR_NORMAL,
			access_pio_sbrdctl_crrel_parity_err_cnt),
[C_PIO_PEC_FIFO_PARITY_ERR] = CNTR_ELEM("PioPecFifoParityErr", 0, 0,
			CNTR_NORMAL,
			access_pio_pec_fifo_parity_err_cnt),
[C_PIO_PCC_FIFO_PARITY_ERR] = CNTR_ELEM("PioPccFifoParityErr", 0, 0,
			CNTR_NORMAL,
			access_pio_pcc_fifo_parity_err_cnt),
[C_PIO_SB_MEM_FIFO1_ERR] = CNTR_ELEM("PioSbMemFifo1Err", 0, 0,
			CNTR_NORMAL,
			access_pio_sb_mem_fifo1_err_cnt),
[C_PIO_SB_MEM_FIFO0_ERR] = CNTR_ELEM("PioSbMemFifo0Err", 0, 0,
			CNTR_NORMAL,
			access_pio_sb_mem_fifo0_err_cnt),
[C_PIO_CSR_PARITY_ERR] = CNTR_ELEM("PioCsrParityErr", 0, 0,
			CNTR_NORMAL,
			access_pio_csr_parity_err_cnt),
[C_PIO_WRITE_ADDR_PARITY_ERR] = CNTR_ELEM("PioWriteAddrParityErr", 0, 0,
			CNTR_NORMAL,
			access_pio_write_addr_parity_err_cnt),
[C_PIO_WRITE_BAD_CTXT_ERR] = CNTR_ELEM("PioWriteBadCtxtErr", 0, 0,
			CNTR_NORMAL,
			access_pio_write_bad_ctxt_err_cnt),
/* SendDmaErrStatus */
[C_SDMA_PCIE_REQ_TRACKING_COR_ERR] = CNTR_ELEM("SDmaPcieReqTrackingCorErr", 0,
			0, CNTR_NORMAL,
			access_sdma_pcie_req_tracking_cor_err_cnt),
[C_SDMA_PCIE_REQ_TRACKING_UNC_ERR] = CNTR_ELEM("SDmaPcieReqTrackingUncErr", 0,
			0, CNTR_NORMAL,
			access_sdma_pcie_req_tracking_unc_err_cnt),
[C_SDMA_CSR_PARITY_ERR] = CNTR_ELEM("SDmaCsrParityErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_csr_parity_err_cnt),
[C_SDMA_RPY_TAG_ERR] = CNTR_ELEM("SDmaRpyTagErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_rpy_tag_err_cnt),
/* SendEgressErrStatus */
[C_TX_READ_PIO_MEMORY_CSR_UNC_ERR] = CNTR_ELEM("TxReadPioMemoryCsrUncErr", 0, 0,
			CNTR_NORMAL,
			access_tx_read_pio_memory_csr_unc_err_cnt),
[C_TX_READ_SDMA_MEMORY_CSR_UNC_ERR] = CNTR_ELEM("TxReadSdmaMemoryCsrUncErr", 0,
			0, CNTR_NORMAL,
			access_tx_read_sdma_memory_csr_err_cnt),
[C_TX_EGRESS_FIFO_COR_ERR] = CNTR_ELEM("TxEgressFifoCorErr", 0, 0,
			CNTR_NORMAL,
			access_tx_egress_fifo_cor_err_cnt),
[C_TX_READ_PIO_MEMORY_COR_ERR] = CNTR_ELEM("TxReadPioMemoryCorErr", 0, 0,
			CNTR_NORMAL,
			access_tx_read_pio_memory_cor_err_cnt),
[C_TX_READ_SDMA_MEMORY_COR_ERR] = CNTR_ELEM("TxReadSdmaMemoryCorErr", 0, 0,
			CNTR_NORMAL,
			access_tx_read_sdma_memory_cor_err_cnt),
[C_TX_SB_HDR_COR_ERR] = CNTR_ELEM("TxSbHdrCorErr", 0, 0,
			CNTR_NORMAL,
			access_tx_sb_hdr_cor_err_cnt),
[C_TX_CREDIT_OVERRUN_ERR] = CNTR_ELEM("TxCreditOverrunErr", 0, 0,
			CNTR_NORMAL,
			access_tx_credit_overrun_err_cnt),
[C_TX_LAUNCH_FIFO8_COR_ERR] = CNTR_ELEM("TxLaunchFifo8CorErr", 0, 0,
			CNTR_NORMAL,
			access_tx_launch_fifo8_cor_err_cnt),
[C_TX_LAUNCH_FIFO7_COR_ERR] = CNTR_ELEM("TxLaunchFifo7CorErr", 0, 0,
			CNTR_NORMAL,
			access_tx_launch_fifo7_cor_err_cnt),
[C_TX_LAUNCH_FIFO6_COR_ERR] = CNTR_ELEM("TxLaunchFifo6CorErr", 0, 0,
			CNTR_NORMAL,
			access_tx_launch_fifo6_cor_err_cnt),
[C_TX_LAUNCH_FIFO5_COR_ERR] = CNTR_ELEM("TxLaunchFifo5CorErr", 0, 0,
			CNTR_NORMAL,
			access_tx_launch_fifo5_cor_err_cnt),
[C_TX_LAUNCH_FIFO4_COR_ERR] = CNTR_ELEM("TxLaunchFifo4CorErr", 0, 0,
			CNTR_NORMAL,
			access_tx_launch_fifo4_cor_err_cnt),
[C_TX_LAUNCH_FIFO3_COR_ERR] = CNTR_ELEM("TxLaunchFifo3CorErr", 0, 0,
			CNTR_NORMAL,
			access_tx_launch_fifo3_cor_err_cnt),
[C_TX_LAUNCH_FIFO2_COR_ERR] = CNTR_ELEM("TxLaunchFifo2CorErr", 0, 0,
			CNTR_NORMAL,
			access_tx_launch_fifo2_cor_err_cnt),
[C_TX_LAUNCH_FIFO1_COR_ERR] = CNTR_ELEM("TxLaunchFifo1CorErr", 0, 0,
			CNTR_NORMAL,
			access_tx_launch_fifo1_cor_err_cnt),
[C_TX_LAUNCH_FIFO0_COR_ERR] = CNTR_ELEM("TxLaunchFifo0CorErr", 0, 0,
			CNTR_NORMAL,
			access_tx_launch_fifo0_cor_err_cnt),
[C_TX_CREDIT_RETURN_VL_ERR] = CNTR_ELEM("TxCreditReturnVLErr", 0, 0,
			CNTR_NORMAL,
			access_tx_credit_return_vl_err_cnt),
[C_TX_HCRC_INSERTION_ERR] = CNTR_ELEM("TxHcrcInsertionErr", 0, 0,
			CNTR_NORMAL,
			access_tx_hcrc_insertion_err_cnt),
[C_TX_EGRESS_FIFI_UNC_ERR] = CNTR_ELEM("TxEgressFifoUncErr", 0, 0,
			CNTR_NORMAL,
			access_tx_egress_fifo_unc_err_cnt),
[C_TX_READ_PIO_MEMORY_UNC_ERR] = CNTR_ELEM("TxReadPioMemoryUncErr", 0, 0,
			CNTR_NORMAL,
			access_tx_read_pio_memory_unc_err_cnt),
[C_TX_READ_SDMA_MEMORY_UNC_ERR] = CNTR_ELEM("TxReadSdmaMemoryUncErr", 0, 0,
			CNTR_NORMAL,
			access_tx_read_sdma_memory_unc_err_cnt),
[C_TX_SB_HDR_UNC_ERR] = CNTR_ELEM("TxSbHdrUncErr", 0, 0,
			CNTR_NORMAL,
			access_tx_sb_hdr_unc_err_cnt),
[C_TX_CREDIT_RETURN_PARITY_ERR] = CNTR_ELEM("TxCreditReturnParityErr", 0, 0,
			CNTR_NORMAL,
			access_tx_credit_return_partiy_err_cnt),
[C_TX_LAUNCH_FIFO8_UNC_OR_PARITY_ERR] = CNTR_ELEM("TxLaunchFifo8UncOrParityErr",
			0, 0, CNTR_NORMAL,
			access_tx_launch_fifo8_unc_or_parity_err_cnt),
[C_TX_LAUNCH_FIFO7_UNC_OR_PARITY_ERR] = CNTR_ELEM("TxLaunchFifo7UncOrParityErr",
			0, 0, CNTR_NORMAL,
			access_tx_launch_fifo7_unc_or_parity_err_cnt),
[C_TX_LAUNCH_FIFO6_UNC_OR_PARITY_ERR] = CNTR_ELEM("TxLaunchFifo6UncOrParityErr",
			0, 0, CNTR_NORMAL,
			access_tx_launch_fifo6_unc_or_parity_err_cnt),
[C_TX_LAUNCH_FIFO5_UNC_OR_PARITY_ERR] = CNTR_ELEM("TxLaunchFifo5UncOrParityErr",
			0, 0, CNTR_NORMAL,
			access_tx_launch_fifo5_unc_or_parity_err_cnt),
[C_TX_LAUNCH_FIFO4_UNC_OR_PARITY_ERR] = CNTR_ELEM("TxLaunchFifo4UncOrParityErr",
			0, 0, CNTR_NORMAL,
			access_tx_launch_fifo4_unc_or_parity_err_cnt),
[C_TX_LAUNCH_FIFO3_UNC_OR_PARITY_ERR] = CNTR_ELEM("TxLaunchFifo3UncOrParityErr",
			0, 0, CNTR_NORMAL,
			access_tx_launch_fifo3_unc_or_parity_err_cnt),
[C_TX_LAUNCH_FIFO2_UNC_OR_PARITY_ERR] = CNTR_ELEM("TxLaunchFifo2UncOrParityErr",
			0, 0, CNTR_NORMAL,
			access_tx_launch_fifo2_unc_or_parity_err_cnt),
[C_TX_LAUNCH_FIFO1_UNC_OR_PARITY_ERR] = CNTR_ELEM("TxLaunchFifo1UncOrParityErr",
			0, 0, CNTR_NORMAL,
			access_tx_launch_fifo1_unc_or_parity_err_cnt),
[C_TX_LAUNCH_FIFO0_UNC_OR_PARITY_ERR] = CNTR_ELEM("TxLaunchFifo0UncOrParityErr",
			0, 0, CNTR_NORMAL,
			access_tx_launch_fifo0_unc_or_parity_err_cnt),
[C_TX_SDMA15_DISALLOWED_PACKET_ERR] = CNTR_ELEM("TxSdma15DisallowedPacketErr",
			0, 0, CNTR_NORMAL,
			access_tx_sdma15_disallowed_packet_err_cnt),
[C_TX_SDMA14_DISALLOWED_PACKET_ERR] = CNTR_ELEM("TxSdma14DisallowedPacketErr",
			0, 0, CNTR_NORMAL,
			access_tx_sdma14_disallowed_packet_err_cnt),
[C_TX_SDMA13_DISALLOWED_PACKET_ERR] = CNTR_ELEM("TxSdma13DisallowedPacketErr",
			0, 0, CNTR_NORMAL,
			access_tx_sdma13_disallowed_packet_err_cnt),
[C_TX_SDMA12_DISALLOWED_PACKET_ERR] = CNTR_ELEM("TxSdma12DisallowedPacketErr",
			0, 0, CNTR_NORMAL,
			access_tx_sdma12_disallowed_packet_err_cnt),
[C_TX_SDMA11_DISALLOWED_PACKET_ERR] = CNTR_ELEM("TxSdma11DisallowedPacketErr",
			0, 0, CNTR_NORMAL,
			access_tx_sdma11_disallowed_packet_err_cnt),
[C_TX_SDMA10_DISALLOWED_PACKET_ERR] = CNTR_ELEM("TxSdma10DisallowedPacketErr",
			0, 0, CNTR_NORMAL,
			access_tx_sdma10_disallowed_packet_err_cnt),
[C_TX_SDMA9_DISALLOWED_PACKET_ERR] = CNTR_ELEM("TxSdma9DisallowedPacketErr",
			0, 0, CNTR_NORMAL,
			access_tx_sdma9_disallowed_packet_err_cnt),
[C_TX_SDMA8_DISALLOWED_PACKET_ERR] = CNTR_ELEM("TxSdma8DisallowedPacketErr",
			0, 0, CNTR_NORMAL,
			access_tx_sdma8_disallowed_packet_err_cnt),
[C_TX_SDMA7_DISALLOWED_PACKET_ERR] = CNTR_ELEM("TxSdma7DisallowedPacketErr",
			0, 0, CNTR_NORMAL,
			access_tx_sdma7_disallowed_packet_err_cnt),
[C_TX_SDMA6_DISALLOWED_PACKET_ERR] = CNTR_ELEM("TxSdma6DisallowedPacketErr",
			0, 0, CNTR_NORMAL,
			access_tx_sdma6_disallowed_packet_err_cnt),
[C_TX_SDMA5_DISALLOWED_PACKET_ERR] = CNTR_ELEM("TxSdma5DisallowedPacketErr",
			0, 0, CNTR_NORMAL,
			access_tx_sdma5_disallowed_packet_err_cnt),
[C_TX_SDMA4_DISALLOWED_PACKET_ERR] = CNTR_ELEM("TxSdma4DisallowedPacketErr",
			0, 0, CNTR_NORMAL,
			access_tx_sdma4_disallowed_packet_err_cnt),
[C_TX_SDMA3_DISALLOWED_PACKET_ERR] = CNTR_ELEM("TxSdma3DisallowedPacketErr",
			0, 0, CNTR_NORMAL,
			access_tx_sdma3_disallowed_packet_err_cnt),
[C_TX_SDMA2_DISALLOWED_PACKET_ERR] = CNTR_ELEM("TxSdma2DisallowedPacketErr",
			0, 0, CNTR_NORMAL,
			access_tx_sdma2_disallowed_packet_err_cnt),
[C_TX_SDMA1_DISALLOWED_PACKET_ERR] = CNTR_ELEM("TxSdma1DisallowedPacketErr",
			0, 0, CNTR_NORMAL,
			access_tx_sdma1_disallowed_packet_err_cnt),
[C_TX_SDMA0_DISALLOWED_PACKET_ERR] = CNTR_ELEM("TxSdma0DisallowedPacketErr",
			0, 0, CNTR_NORMAL,
			access_tx_sdma0_disallowed_packet_err_cnt),
[C_TX_CONFIG_PARITY_ERR] = CNTR_ELEM("TxConfigParityErr", 0, 0,
			CNTR_NORMAL,
			access_tx_config_parity_err_cnt),
[C_TX_SBRD_CTL_CSR_PARITY_ERR] = CNTR_ELEM("TxSbrdCtlCsrParityErr", 0, 0,
			CNTR_NORMAL,
			access_tx_sbrd_ctl_csr_parity_err_cnt),
[C_TX_LAUNCH_CSR_PARITY_ERR] = CNTR_ELEM("TxLaunchCsrParityErr", 0, 0,
			CNTR_NORMAL,
			access_tx_launch_csr_parity_err_cnt),
[C_TX_ILLEGAL_CL_ERR] = CNTR_ELEM("TxIllegalVLErr", 0, 0,
			CNTR_NORMAL,
			access_tx_illegal_vl_err_cnt),
[C_TX_SBRD_CTL_STATE_MACHINE_PARITY_ERR] = CNTR_ELEM(
			"TxSbrdCtlStateMachineParityErr", 0, 0,
			CNTR_NORMAL,
			access_tx_sbrd_ctl_state_machine_parity_err_cnt),
[C_TX_RESERVED_10] = CNTR_ELEM("Tx Egress Reserved 10", 0, 0,
			CNTR_NORMAL,
			access_egress_reserved_10_err_cnt),
[C_TX_RESERVED_9] = CNTR_ELEM("Tx Egress Reserved 9", 0, 0,
			CNTR_NORMAL,
			access_egress_reserved_9_err_cnt),
[C_TX_SDMA_LAUNCH_INTF_PARITY_ERR] = CNTR_ELEM("TxSdmaLaunchIntfParityErr",
			0, 0, CNTR_NORMAL,
			access_tx_sdma_launch_intf_parity_err_cnt),
[C_TX_PIO_LAUNCH_INTF_PARITY_ERR] = CNTR_ELEM("TxPioLaunchIntfParityErr", 0, 0,
			CNTR_NORMAL,
			access_tx_pio_launch_intf_parity_err_cnt),
[C_TX_RESERVED_6] = CNTR_ELEM("Tx Egress Reserved 6", 0, 0,
			CNTR_NORMAL,
			access_egress_reserved_6_err_cnt),
[C_TX_INCORRECT_LINK_STATE_ERR] = CNTR_ELEM("TxIncorrectLinkStateErr", 0, 0,
			CNTR_NORMAL,
			access_tx_incorrect_link_state_err_cnt),
[C_TX_LINK_DOWN_ERR] = CNTR_ELEM("TxLinkdownErr", 0, 0,
			CNTR_NORMAL,
			access_tx_linkdown_err_cnt),
[C_TX_EGRESS_FIFO_UNDERRUN_OR_PARITY_ERR] = CNTR_ELEM(
			"EgressFifoUnderrunOrParityErr", 0, 0,
			CNTR_NORMAL,
			access_tx_egress_fifi_underrun_or_parity_err_cnt),
[C_TX_RESERVED_2] = CNTR_ELEM("Tx Egress Reserved 2", 0, 0,
			CNTR_NORMAL,
			access_egress_reserved_2_err_cnt),
[C_TX_PKT_INTEGRITY_MEM_UNC_ERR] = CNTR_ELEM("TxPktIntegrityMemUncErr", 0, 0,
			CNTR_NORMAL,
			access_tx_pkt_integrity_mem_unc_err_cnt),
[C_TX_PKT_INTEGRITY_MEM_COR_ERR] = CNTR_ELEM("TxPktIntegrityMemCorErr", 0, 0,
			CNTR_NORMAL,
			access_tx_pkt_integrity_mem_cor_err_cnt),
/* SendErrStatus */
[C_SEND_CSR_WRITE_BAD_ADDR_ERR] = CNTR_ELEM("SendCsrWriteBadAddrErr", 0, 0,
			CNTR_NORMAL,
			access_send_csr_write_bad_addr_err_cnt),
[C_SEND_CSR_READ_BAD_ADD_ERR] = CNTR_ELEM("SendCsrReadBadAddrErr", 0, 0,
			CNTR_NORMAL,
			access_send_csr_read_bad_addr_err_cnt),
[C_SEND_CSR_PARITY_ERR] = CNTR_ELEM("SendCsrParityErr", 0, 0,
			CNTR_NORMAL,
			access_send_csr_parity_cnt),
/* SendCtxtErrStatus */
[C_PIO_WRITE_OUT_OF_BOUNDS_ERR] = CNTR_ELEM("PioWriteOutOfBoundsErr", 0, 0,
			CNTR_NORMAL,
			access_pio_write_out_of_bounds_err_cnt),
[C_PIO_WRITE_OVERFLOW_ERR] = CNTR_ELEM("PioWriteOverflowErr", 0, 0,
			CNTR_NORMAL,
			access_pio_write_overflow_err_cnt),
[C_PIO_WRITE_CROSSES_BOUNDARY_ERR] = CNTR_ELEM("PioWriteCrossesBoundaryErr",
			0, 0, CNTR_NORMAL,
			access_pio_write_crosses_boundary_err_cnt),
[C_PIO_DISALLOWED_PACKET_ERR] = CNTR_ELEM("PioDisallowedPacketErr", 0, 0,
			CNTR_NORMAL,
			access_pio_disallowed_packet_err_cnt),
[C_PIO_INCONSISTENT_SOP_ERR] = CNTR_ELEM("PioInconsistentSopErr", 0, 0,
			CNTR_NORMAL,
			access_pio_inconsistent_sop_err_cnt),
/* SendDmaEngErrStatus */
[C_SDMA_HEADER_REQUEST_FIFO_COR_ERR] = CNTR_ELEM("SDmaHeaderRequestFifoCorErr",
			0, 0, CNTR_NORMAL,
			access_sdma_header_request_fifo_cor_err_cnt),
[C_SDMA_HEADER_STORAGE_COR_ERR] = CNTR_ELEM("SDmaHeaderStorageCorErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_header_storage_cor_err_cnt),
[C_SDMA_PACKET_TRACKING_COR_ERR] = CNTR_ELEM("SDmaPacketTrackingCorErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_packet_tracking_cor_err_cnt),
[C_SDMA_ASSEMBLY_COR_ERR] = CNTR_ELEM("SDmaAssemblyCorErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_assembly_cor_err_cnt),
[C_SDMA_DESC_TABLE_COR_ERR] = CNTR_ELEM("SDmaDescTableCorErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_desc_table_cor_err_cnt),
[C_SDMA_HEADER_REQUEST_FIFO_UNC_ERR] = CNTR_ELEM("SDmaHeaderRequestFifoUncErr",
			0, 0, CNTR_NORMAL,
			access_sdma_header_request_fifo_unc_err_cnt),
[C_SDMA_HEADER_STORAGE_UNC_ERR] = CNTR_ELEM("SDmaHeaderStorageUncErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_header_storage_unc_err_cnt),
[C_SDMA_PACKET_TRACKING_UNC_ERR] = CNTR_ELEM("SDmaPacketTrackingUncErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_packet_tracking_unc_err_cnt),
[C_SDMA_ASSEMBLY_UNC_ERR] = CNTR_ELEM("SDmaAssemblyUncErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_assembly_unc_err_cnt),
[C_SDMA_DESC_TABLE_UNC_ERR] = CNTR_ELEM("SDmaDescTableUncErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_desc_table_unc_err_cnt),
[C_SDMA_TIMEOUT_ERR] = CNTR_ELEM("SDmaTimeoutErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_timeout_err_cnt),
[C_SDMA_HEADER_LENGTH_ERR] = CNTR_ELEM("SDmaHeaderLengthErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_header_length_err_cnt),
[C_SDMA_HEADER_ADDRESS_ERR] = CNTR_ELEM("SDmaHeaderAddressErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_header_address_err_cnt),
[C_SDMA_HEADER_SELECT_ERR] = CNTR_ELEM("SDmaHeaderSelectErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_header_select_err_cnt),
[C_SMDA_RESERVED_9] = CNTR_ELEM("SDma Reserved 9", 0, 0,
			CNTR_NORMAL,
			access_sdma_reserved_9_err_cnt),
[C_SDMA_PACKET_DESC_OVERFLOW_ERR] = CNTR_ELEM("SDmaPacketDescOverflowErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_packet_desc_overflow_err_cnt),
[C_SDMA_LENGTH_MISMATCH_ERR] = CNTR_ELEM("SDmaLengthMismatchErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_length_mismatch_err_cnt),
[C_SDMA_HALT_ERR] = CNTR_ELEM("SDmaHaltErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_halt_err_cnt),
[C_SDMA_MEM_READ_ERR] = CNTR_ELEM("SDmaMemReadErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_mem_read_err_cnt),
[C_SDMA_FIRST_DESC_ERR] = CNTR_ELEM("SDmaFirstDescErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_first_desc_err_cnt),
[C_SDMA_TAIL_OUT_OF_BOUNDS_ERR] = CNTR_ELEM("SDmaTailOutOfBoundsErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_tail_out_of_bounds_err_cnt),
[C_SDMA_TOO_LONG_ERR] = CNTR_ELEM("SDmaTooLongErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_too_long_err_cnt),
[C_SDMA_GEN_MISMATCH_ERR] = CNTR_ELEM("SDmaGenMismatchErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_gen_mismatch_err_cnt),
[C_SDMA_WRONG_DW_ERR] = CNTR_ELEM("SDmaWrongDwErr", 0, 0,
			CNTR_NORMAL,
			access_sdma_wrong_dw_err_cnt),
};

static struct cntr_entry port_cntrs[PORT_CNTR_LAST] = {
[C_TX_UNSUP_VL] = TXE32_PORT_CNTR_ELEM(TxUnVLErr, SEND_UNSUP_VL_ERR_CNT,
			CNTR_NORMAL),
[C_TX_INVAL_LEN] = TXE32_PORT_CNTR_ELEM(TxInvalLen, SEND_LEN_ERR_CNT,
			CNTR_NORMAL),
[C_TX_MM_LEN_ERR] = TXE32_PORT_CNTR_ELEM(TxMMLenErr, SEND_MAX_MIN_LEN_ERR_CNT,
			CNTR_NORMAL),
[C_TX_UNDERRUN] = TXE32_PORT_CNTR_ELEM(TxUnderrun, SEND_UNDERRUN_CNT,
			CNTR_NORMAL),
[C_TX_FLOW_STALL] = TXE32_PORT_CNTR_ELEM(TxFlowStall, SEND_FLOW_STALL_CNT,
			CNTR_NORMAL),
[C_TX_DROPPED] = TXE32_PORT_CNTR_ELEM(TxDropped, SEND_DROPPED_PKT_CNT,
			CNTR_NORMAL),
[C_TX_HDR_ERR] = TXE32_PORT_CNTR_ELEM(TxHdrErr, SEND_HEADERS_ERR_CNT,
			CNTR_NORMAL),
[C_TX_PKT] = TXE64_PORT_CNTR_ELEM(TxPkt, SEND_DATA_PKT_CNT, CNTR_NORMAL),
[C_TX_WORDS] = TXE64_PORT_CNTR_ELEM(TxWords, SEND_DWORD_CNT, CNTR_NORMAL),
[C_TX_WAIT] = TXE64_PORT_CNTR_ELEM(TxWait, SEND_WAIT_CNT, CNTR_SYNTH),
[C_TX_FLIT_VL] = TXE64_PORT_CNTR_ELEM(TxFlitVL, SEND_DATA_VL0_CNT,
				      CNTR_SYNTH | CNTR_VL),
[C_TX_PKT_VL] = TXE64_PORT_CNTR_ELEM(TxPktVL, SEND_DATA_PKT_VL0_CNT,
				     CNTR_SYNTH | CNTR_VL),
[C_TX_WAIT_VL] = TXE64_PORT_CNTR_ELEM(TxWaitVL, SEND_WAIT_VL0_CNT,
				      CNTR_SYNTH | CNTR_VL),
[C_RX_PKT] = RXE64_PORT_CNTR_ELEM(RxPkt, RCV_DATA_PKT_CNT, CNTR_NORMAL),
[C_RX_WORDS] = RXE64_PORT_CNTR_ELEM(RxWords, RCV_DWORD_CNT, CNTR_NORMAL),
[C_SW_LINK_DOWN] = CNTR_ELEM("SwLinkDown", 0, 0, CNTR_SYNTH | CNTR_32BIT,
			     access_sw_link_dn_cnt),
[C_SW_LINK_UP] = CNTR_ELEM("SwLinkUp", 0, 0, CNTR_SYNTH | CNTR_32BIT,
			   access_sw_link_up_cnt),
[C_SW_UNKNOWN_FRAME] = CNTR_ELEM("UnknownFrame", 0, 0, CNTR_NORMAL,
				 access_sw_unknown_frame_cnt),
[C_SW_XMIT_DSCD] = CNTR_ELEM("XmitDscd", 0, 0, CNTR_SYNTH | CNTR_32BIT,
			     access_sw_xmit_discards),
[C_SW_XMIT_DSCD_VL] = CNTR_ELEM("XmitDscdVl", 0, 0,
				CNTR_SYNTH | CNTR_32BIT | CNTR_VL,
				access_sw_xmit_discards),
[C_SW_XMIT_CSTR_ERR] = CNTR_ELEM("XmitCstrErr", 0, 0, CNTR_SYNTH,
				 access_xmit_constraint_errs),
[C_SW_RCV_CSTR_ERR] = CNTR_ELEM("RcvCstrErr", 0, 0, CNTR_SYNTH,
				access_rcv_constraint_errs),
[C_SW_IBP_LOOP_PKTS] = SW_IBP_CNTR(LoopPkts, loop_pkts),
[C_SW_IBP_RC_RESENDS] = SW_IBP_CNTR(RcResend, rc_resends),
[C_SW_IBP_RNR_NAKS] = SW_IBP_CNTR(RnrNak, rnr_naks),
[C_SW_IBP_OTHER_NAKS] = SW_IBP_CNTR(OtherNak, other_naks),
[C_SW_IBP_RC_TIMEOUTS] = SW_IBP_CNTR(RcTimeOut, rc_timeouts),
[C_SW_IBP_PKT_DROPS] = SW_IBP_CNTR(PktDrop, pkt_drops),
[C_SW_IBP_DMA_WAIT] = SW_IBP_CNTR(DmaWait, dmawait),
[C_SW_IBP_RC_SEQNAK] = SW_IBP_CNTR(RcSeqNak, rc_seqnak),
[C_SW_IBP_RC_DUPREQ] = SW_IBP_CNTR(RcDupRew, rc_dupreq),
[C_SW_IBP_RDMA_SEQ] = SW_IBP_CNTR(RdmaSeq, rdma_seq),
[C_SW_IBP_UNALIGNED] = SW_IBP_CNTR(Unaligned, unaligned),
[C_SW_IBP_SEQ_NAK] = SW_IBP_CNTR(SeqNak, seq_naks),
[C_SW_IBP_RC_CRWAITS] = SW_IBP_CNTR(RcCrWait, rc_crwaits),
[C_SW_CPU_RC_ACKS] = CNTR_ELEM("RcAcks", 0, 0, CNTR_NORMAL,
			       access_sw_cpu_rc_acks),
[C_SW_CPU_RC_QACKS] = CNTR_ELEM("RcQacks", 0, 0, CNTR_NORMAL,
				access_sw_cpu_rc_qacks),
[C_SW_CPU_RC_DELAYED_COMP] = CNTR_ELEM("RcDelayComp", 0, 0, CNTR_NORMAL,
				       access_sw_cpu_rc_delayed_comp),
[OVR_LBL(0)] = OVR_ELM(0), [OVR_LBL(1)] = OVR_ELM(1),
[OVR_LBL(2)] = OVR_ELM(2), [OVR_LBL(3)] = OVR_ELM(3),
[OVR_LBL(4)] = OVR_ELM(4), [OVR_LBL(5)] = OVR_ELM(5),
[OVR_LBL(6)] = OVR_ELM(6), [OVR_LBL(7)] = OVR_ELM(7),
[OVR_LBL(8)] = OVR_ELM(8), [OVR_LBL(9)] = OVR_ELM(9),
[OVR_LBL(10)] = OVR_ELM(10), [OVR_LBL(11)] = OVR_ELM(11),
[OVR_LBL(12)] = OVR_ELM(12), [OVR_LBL(13)] = OVR_ELM(13),
[OVR_LBL(14)] = OVR_ELM(14), [OVR_LBL(15)] = OVR_ELM(15),
[OVR_LBL(16)] = OVR_ELM(16), [OVR_LBL(17)] = OVR_ELM(17),
[OVR_LBL(18)] = OVR_ELM(18), [OVR_LBL(19)] = OVR_ELM(19),
[OVR_LBL(20)] = OVR_ELM(20), [OVR_LBL(21)] = OVR_ELM(21),
[OVR_LBL(22)] = OVR_ELM(22), [OVR_LBL(23)] = OVR_ELM(23),
[OVR_LBL(24)] = OVR_ELM(24), [OVR_LBL(25)] = OVR_ELM(25),
[OVR_LBL(26)] = OVR_ELM(26), [OVR_LBL(27)] = OVR_ELM(27),
[OVR_LBL(28)] = OVR_ELM(28), [OVR_LBL(29)] = OVR_ELM(29),
[OVR_LBL(30)] = OVR_ELM(30), [OVR_LBL(31)] = OVR_ELM(31),
[OVR_LBL(32)] = OVR_ELM(32), [OVR_LBL(33)] = OVR_ELM(33),
[OVR_LBL(34)] = OVR_ELM(34), [OVR_LBL(35)] = OVR_ELM(35),
[OVR_LBL(36)] = OVR_ELM(36), [OVR_LBL(37)] = OVR_ELM(37),
[OVR_LBL(38)] = OVR_ELM(38), [OVR_LBL(39)] = OVR_ELM(39),
[OVR_LBL(40)] = OVR_ELM(40), [OVR_LBL(41)] = OVR_ELM(41),
[OVR_LBL(42)] = OVR_ELM(42), [OVR_LBL(43)] = OVR_ELM(43),
[OVR_LBL(44)] = OVR_ELM(44), [OVR_LBL(45)] = OVR_ELM(45),
[OVR_LBL(46)] = OVR_ELM(46), [OVR_LBL(47)] = OVR_ELM(47),
[OVR_LBL(48)] = OVR_ELM(48), [OVR_LBL(49)] = OVR_ELM(49),
[OVR_LBL(50)] = OVR_ELM(50), [OVR_LBL(51)] = OVR_ELM(51),
[OVR_LBL(52)] = OVR_ELM(52), [OVR_LBL(53)] = OVR_ELM(53),
[OVR_LBL(54)] = OVR_ELM(54), [OVR_LBL(55)] = OVR_ELM(55),
[OVR_LBL(56)] = OVR_ELM(56), [OVR_LBL(57)] = OVR_ELM(57),
[OVR_LBL(58)] = OVR_ELM(58), [OVR_LBL(59)] = OVR_ELM(59),
[OVR_LBL(60)] = OVR_ELM(60), [OVR_LBL(61)] = OVR_ELM(61),
[OVR_LBL(62)] = OVR_ELM(62), [OVR_LBL(63)] = OVR_ELM(63),
[OVR_LBL(64)] = OVR_ELM(64), [OVR_LBL(65)] = OVR_ELM(65),
[OVR_LBL(66)] = OVR_ELM(66), [OVR_LBL(67)] = OVR_ELM(67),
[OVR_LBL(68)] = OVR_ELM(68), [OVR_LBL(69)] = OVR_ELM(69),
[OVR_LBL(70)] = OVR_ELM(70), [OVR_LBL(71)] = OVR_ELM(71),
[OVR_LBL(72)] = OVR_ELM(72), [OVR_LBL(73)] = OVR_ELM(73),
[OVR_LBL(74)] = OVR_ELM(74), [OVR_LBL(75)] = OVR_ELM(75),
[OVR_LBL(76)] = OVR_ELM(76), [OVR_LBL(77)] = OVR_ELM(77),
[OVR_LBL(78)] = OVR_ELM(78), [OVR_LBL(79)] = OVR_ELM(79),
[OVR_LBL(80)] = OVR_ELM(80), [OVR_LBL(81)] = OVR_ELM(81),
[OVR_LBL(82)] = OVR_ELM(82), [OVR_LBL(83)] = OVR_ELM(83),
[OVR_LBL(84)] = OVR_ELM(84), [OVR_LBL(85)] = OVR_ELM(85),
[OVR_LBL(86)] = OVR_ELM(86), [OVR_LBL(87)] = OVR_ELM(87),
[OVR_LBL(88)] = OVR_ELM(88), [OVR_LBL(89)] = OVR_ELM(89),
[OVR_LBL(90)] = OVR_ELM(90), [OVR_LBL(91)] = OVR_ELM(91),
[OVR_LBL(92)] = OVR_ELM(92), [OVR_LBL(93)] = OVR_ELM(93),
[OVR_LBL(94)] = OVR_ELM(94), [OVR_LBL(95)] = OVR_ELM(95),
[OVR_LBL(96)] = OVR_ELM(96), [OVR_LBL(97)] = OVR_ELM(97),
[OVR_LBL(98)] = OVR_ELM(98), [OVR_LBL(99)] = OVR_ELM(99),
[OVR_LBL(100)] = OVR_ELM(100), [OVR_LBL(101)] = OVR_ELM(101),
[OVR_LBL(102)] = OVR_ELM(102), [OVR_LBL(103)] = OVR_ELM(103),
[OVR_LBL(104)] = OVR_ELM(104), [OVR_LBL(105)] = OVR_ELM(105),
[OVR_LBL(106)] = OVR_ELM(106), [OVR_LBL(107)] = OVR_ELM(107),
[OVR_LBL(108)] = OVR_ELM(108), [OVR_LBL(109)] = OVR_ELM(109),
[OVR_LBL(110)] = OVR_ELM(110), [OVR_LBL(111)] = OVR_ELM(111),
[OVR_LBL(112)] = OVR_ELM(112), [OVR_LBL(113)] = OVR_ELM(113),
[OVR_LBL(114)] = OVR_ELM(114), [OVR_LBL(115)] = OVR_ELM(115),
[OVR_LBL(116)] = OVR_ELM(116), [OVR_LBL(117)] = OVR_ELM(117),
[OVR_LBL(118)] = OVR_ELM(118), [OVR_LBL(119)] = OVR_ELM(119),
[OVR_LBL(120)] = OVR_ELM(120), [OVR_LBL(121)] = OVR_ELM(121),
[OVR_LBL(122)] = OVR_ELM(122), [OVR_LBL(123)] = OVR_ELM(123),
[OVR_LBL(124)] = OVR_ELM(124), [OVR_LBL(125)] = OVR_ELM(125),
[OVR_LBL(126)] = OVR_ELM(126), [OVR_LBL(127)] = OVR_ELM(127),
[OVR_LBL(128)] = OVR_ELM(128), [OVR_LBL(129)] = OVR_ELM(129),
[OVR_LBL(130)] = OVR_ELM(130), [OVR_LBL(131)] = OVR_ELM(131),
[OVR_LBL(132)] = OVR_ELM(132), [OVR_LBL(133)] = OVR_ELM(133),
[OVR_LBL(134)] = OVR_ELM(134), [OVR_LBL(135)] = OVR_ELM(135),
[OVR_LBL(136)] = OVR_ELM(136), [OVR_LBL(137)] = OVR_ELM(137),
[OVR_LBL(138)] = OVR_ELM(138), [OVR_LBL(139)] = OVR_ELM(139),
[OVR_LBL(140)] = OVR_ELM(140), [OVR_LBL(141)] = OVR_ELM(141),
[OVR_LBL(142)] = OVR_ELM(142), [OVR_LBL(143)] = OVR_ELM(143),
[OVR_LBL(144)] = OVR_ELM(144), [OVR_LBL(145)] = OVR_ELM(145),
[OVR_LBL(146)] = OVR_ELM(146), [OVR_LBL(147)] = OVR_ELM(147),
[OVR_LBL(148)] = OVR_ELM(148), [OVR_LBL(149)] = OVR_ELM(149),
[OVR_LBL(150)] = OVR_ELM(150), [OVR_LBL(151)] = OVR_ELM(151),
[OVR_LBL(152)] = OVR_ELM(152), [OVR_LBL(153)] = OVR_ELM(153),
[OVR_LBL(154)] = OVR_ELM(154), [OVR_LBL(155)] = OVR_ELM(155),
[OVR_LBL(156)] = OVR_ELM(156), [OVR_LBL(157)] = OVR_ELM(157),
[OVR_LBL(158)] = OVR_ELM(158), [OVR_LBL(159)] = OVR_ELM(159),
};

/* ======================================================================== */

/* return true if this is chip revision revision a */
int is_ax(struct hfi1_devdata *dd)
{
	u8 chip_rev_minor =
		dd->revision >> CCE_REVISION_CHIP_REV_MINOR_SHIFT
			& CCE_REVISION_CHIP_REV_MINOR_MASK;
	return (chip_rev_minor & 0xf0) == 0;
}

/* return true if this is chip revision revision b */
int is_bx(struct hfi1_devdata *dd)
{
	u8 chip_rev_minor =
		dd->revision >> CCE_REVISION_CHIP_REV_MINOR_SHIFT
			& CCE_REVISION_CHIP_REV_MINOR_MASK;
	return (chip_rev_minor & 0xF0) == 0x10;
}

/* return true is kernel urg disabled for rcd */
bool is_urg_masked(struct hfi1_ctxtdata *rcd)
{
	u64 mask;
	u32 is = IS_RCVURGENT_START + rcd->ctxt;
	u8 bit = is % 64;

	mask = read_csr(rcd->dd, CCE_INT_MASK + (8 * (is / 64)));
	return !(mask & BIT_ULL(bit));
}

/*
 * Append string s to buffer buf.  Arguments curp and len are the current
 * position and remaining length, respectively.
 *
 * return 0 on success, 1 on out of room
 */
static int append_str(char *buf, char **curp, int *lenp, const char *s)
{
	char *p = *curp;
	int len = *lenp;
	int result = 0; /* success */
	char c;

	/* add a comma, if first in the buffer */
	if (p != buf) {
		if (len == 0) {
			result = 1; /* out of room */
			goto done;
		}
		*p++ = ',';
		len--;
	}

	/* copy the string */
	while ((c = *s++) != 0) {
		if (len == 0) {
			result = 1; /* out of room */
			goto done;
		}
		*p++ = c;
		len--;
	}

done:
	/* write return values */
	*curp = p;
	*lenp = len;

	return result;
}

/*
 * Using the given flag table, print a comma separated string into
 * the buffer.  End in '*' if the buffer is too short.
 */
static char *flag_string(char *buf, int buf_len, u64 flags,
			 struct flag_table *table, int table_size)
{
	char extra[32];
	char *p = buf;
	int len = buf_len;
	int no_room = 0;
	int i;

	/* make sure there is at least 2 so we can form "*" */
	if (len < 2)
		return "";

	len--;	/* leave room for a nul */
	for (i = 0; i < table_size; i++) {
		if (flags & table[i].flag) {
			no_room = append_str(buf, &p, &len, table[i].str);
			if (no_room)
				break;
			flags &= ~table[i].flag;
		}
	}

	/* any undocumented bits left? */
	if (!no_room && flags) {
		snprintf(extra, sizeof(extra), "bits 0x%llx", flags);
		no_room = append_str(buf, &p, &len, extra);
	}

	/* add * if ran out of room */
	if (no_room) {
		/* may need to back up to add space for a '*' */
		if (len == 0)
			--p;
		*p++ = '*';
	}

	/* add final nul - space already allocated above */
	*p = 0;
	return buf;
}

/* first 8 CCE error interrupt source names */
static const char * const cce_misc_names[] = {
	"CceErrInt",		/* 0 */
	"RxeErrInt",		/* 1 */
	"MiscErrInt",		/* 2 */
	"Reserved3",		/* 3 */
	"PioErrInt",		/* 4 */
	"SDmaErrInt",		/* 5 */
	"EgressErrInt",		/* 6 */
	"TxeErrInt"		/* 7 */
};

/*
 * Return the miscellaneous error interrupt name.
 */
static char *is_misc_err_name(char *buf, size_t bsize, unsigned int source)
{
	if (source < ARRAY_SIZE(cce_misc_names))
		strncpy(buf, cce_misc_names[source], bsize);
	else
		snprintf(buf, bsize, "Reserved%u",
			 source + IS_GENERAL_ERR_START);

	return buf;
}

/*
 * Return the SDMA engine error interrupt name.
 */
static char *is_sdma_eng_err_name(char *buf, size_t bsize, unsigned int source)
{
	snprintf(buf, bsize, "SDmaEngErrInt%u", source);
	return buf;
}

/*
 * Return the send context error interrupt name.
 */
static char *is_sendctxt_err_name(char *buf, size_t bsize, unsigned int source)
{
	snprintf(buf, bsize, "SendCtxtErrInt%u", source);
	return buf;
}

static const char * const various_names[] = {
	"PbcInt",
	"GpioAssertInt",
	"Qsfp1Int",
	"Qsfp2Int",
	"TCritInt"
};

/*
 * Return the various interrupt name.
 */
static char *is_various_name(char *buf, size_t bsize, unsigned int source)
{
	if (source < ARRAY_SIZE(various_names))
		strncpy(buf, various_names[source], bsize);
	else
		snprintf(buf, bsize, "Reserved%u", source + IS_VARIOUS_START);
	return buf;
}

/*
 * Return the DC interrupt name.
 */
static char *is_dc_name(char *buf, size_t bsize, unsigned int source)
{
	static const char * const dc_int_names[] = {
		"common",
		"lcb",
		"8051",
		"lbm"	/* local block merge */
	};

	if (source < ARRAY_SIZE(dc_int_names))
		snprintf(buf, bsize, "dc_%s_int", dc_int_names[source]);
	else
		snprintf(buf, bsize, "DCInt%u", source);
	return buf;
}

static const char * const sdma_int_names[] = {
	"SDmaInt",
	"SdmaIdleInt",
	"SdmaProgressInt",
};

/*
 * Return the SDMA engine interrupt name.
 */
static char *is_sdma_eng_name(char *buf, size_t bsize, unsigned int source)
{
	/* what interrupt */
	unsigned int what  = source / TXE_NUM_SDMA_ENGINES;
	/* which engine */
	unsigned int which = source % TXE_NUM_SDMA_ENGINES;

	if (likely(what < 3))
		snprintf(buf, bsize, "%s%u", sdma_int_names[what], which);
	else
		snprintf(buf, bsize, "Invalid SDMA interrupt %u", source);
	return buf;
}

/*
 * Return the receive available interrupt name.
 */
static char *is_rcv_avail_name(char *buf, size_t bsize, unsigned int source)
{
	snprintf(buf, bsize, "RcvAvailInt%u", source);
	return buf;
}

/*
 * Return the receive urgent interrupt name.
 */
static char *is_rcv_urgent_name(char *buf, size_t bsize, unsigned int source)
{
	snprintf(buf, bsize, "RcvUrgentInt%u", source);
	return buf;
}

/*
 * Return the send credit interrupt name.
 */
static char *is_send_credit_name(char *buf, size_t bsize, unsigned int source)
{
	snprintf(buf, bsize, "SendCreditInt%u", source);
	return buf;
}

/*
 * Return the reserved interrupt name.
 */
static char *is_reserved_name(char *buf, size_t bsize, unsigned int source)
{
	snprintf(buf, bsize, "Reserved%u", source + IS_RESERVED_START);
	return buf;
}

static char *cce_err_status_string(char *buf, int buf_len, u64 flags)
{
	return flag_string(buf, buf_len, flags,
			   cce_err_status_flags,
			   ARRAY_SIZE(cce_err_status_flags));
}

static char *rxe_err_status_string(char *buf, int buf_len, u64 flags)
{
	return flag_string(buf, buf_len, flags,
			   rxe_err_status_flags,
			   ARRAY_SIZE(rxe_err_status_flags));
}

static char *misc_err_status_string(char *buf, int buf_len, u64 flags)
{
	return flag_string(buf, buf_len, flags, misc_err_status_flags,
			   ARRAY_SIZE(misc_err_status_flags));
}

static char *pio_err_status_string(char *buf, int buf_len, u64 flags)
{
	return flag_string(buf, buf_len, flags,
			   pio_err_status_flags,
			   ARRAY_SIZE(pio_err_status_flags));
}

static char *sdma_err_status_string(char *buf, int buf_len, u64 flags)
{
	return flag_string(buf, buf_len, flags,
			   sdma_err_status_flags,
			   ARRAY_SIZE(sdma_err_status_flags));
}

static char *egress_err_status_string(char *buf, int buf_len, u64 flags)
{
	return flag_string(buf, buf_len, flags,
			   egress_err_status_flags,
			   ARRAY_SIZE(egress_err_status_flags));
}

static char *egress_err_info_string(char *buf, int buf_len, u64 flags)
{
	return flag_string(buf, buf_len, flags,
			   egress_err_info_flags,
			   ARRAY_SIZE(egress_err_info_flags));
}

static char *send_err_status_string(char *buf, int buf_len, u64 flags)
{
	return flag_string(buf, buf_len, flags,
			   send_err_status_flags,
			   ARRAY_SIZE(send_err_status_flags));
}

static void handle_cce_err(struct hfi1_devdata *dd, u32 unused, u64 reg)
{
	char buf[96];
	int i = 0;

	/*
	 * For most these errors, there is nothing that can be done except
	 * report or record it.
	 */
	dd_dev_info(dd, "CCE Error: %s\n",
		    cce_err_status_string(buf, sizeof(buf), reg));

	if ((reg & CCE_ERR_STATUS_CCE_CLI2_ASYNC_FIFO_PARITY_ERR_SMASK) &&
	    is_ax(dd) && (dd->icode != ICODE_FUNCTIONAL_SIMULATOR)) {
		/* this error requires a manual drop into SPC freeze mode */
		/* then a fix up */
		start_freeze_handling(dd->pport, FREEZE_SELF);
	}

	for (i = 0; i < NUM_CCE_ERR_STATUS_COUNTERS; i++) {
		if (reg & (1ull << i)) {
			incr_cntr64(&dd->cce_err_status_cnt[i]);
			/* maintain a counter over all cce_err_status errors */
			incr_cntr64(&dd->sw_cce_err_status_aggregate);
		}
	}
}

/*
 * Check counters for receive errors that do not have an interrupt
 * associated with them.
 */
#define RCVERR_CHECK_TIME 10
static void update_rcverr_timer(struct timer_list *t)
{
	struct hfi1_devdata *dd = from_timer(dd, t, rcverr_timer);
	struct hfi1_pportdata *ppd = dd->pport;
	u32 cur_ovfl_cnt = read_dev_cntr(dd, C_RCV_OVF, CNTR_INVALID_VL);

	if (dd->rcv_ovfl_cnt < cur_ovfl_cnt &&
	    ppd->port_error_action & OPA_PI_MASK_EX_BUFFER_OVERRUN) {
		dd_dev_info(dd, "%s: PortErrorAction bounce\n", __func__);
		set_link_down_reason(
		ppd, OPA_LINKDOWN_REASON_EXCESSIVE_BUFFER_OVERRUN, 0,
		OPA_LINKDOWN_REASON_EXCESSIVE_BUFFER_OVERRUN);
		queue_work(ppd->link_wq, &ppd->link_bounce_work);
	}
	dd->rcv_ovfl_cnt = (u32)cur_ovfl_cnt;

	mod_timer(&dd->rcverr_timer, jiffies + HZ * RCVERR_CHECK_TIME);
}

static int init_rcverr(struct hfi1_devdata *dd)
{
	timer_setup(&dd->rcverr_timer, update_rcverr_timer, 0);
	/* Assume the hardware counter has been reset */
	dd->rcv_ovfl_cnt = 0;
	return mod_timer(&dd->rcverr_timer, jiffies + HZ * RCVERR_CHECK_TIME);
}

static void free_rcverr(struct hfi1_devdata *dd)
{
	if (dd->rcverr_timer.function)
		del_timer_sync(&dd->rcverr_timer);
}

static void handle_rxe_err(struct hfi1_devdata *dd, u32 unused, u64 reg)
{
	char buf[96];
	int i = 0;

	dd_dev_info(dd, "Receive Error: %s\n",
		    rxe_err_status_string(buf, sizeof(buf), reg));

	if (reg & ALL_RXE_FREEZE_ERR) {
		int flags = 0;

		/*
		 * Freeze mode recovery is disabled for the errors
		 * in RXE_FREEZE_ABORT_MASK
		 */
		if (is_ax(dd) && (reg & RXE_FREEZE_ABORT_MASK))
			flags = FREEZE_ABORT;

		start_freeze_handling(dd->pport, flags);
	}

	for (i = 0; i < NUM_RCV_ERR_STATUS_COUNTERS; i++) {
		if (reg & (1ull << i))
			incr_cntr64(&dd->rcv_err_status_cnt[i]);
	}
}

static void handle_misc_err(struct hfi1_devdata *dd, u32 unused, u64 reg)
{
	char buf[96];
	int i = 0;

	dd_dev_info(dd, "Misc Error: %s",
		    misc_err_status_string(buf, sizeof(buf), reg));
	for (i = 0; i < NUM_MISC_ERR_STATUS_COUNTERS; i++) {
		if (reg & (1ull << i))
			incr_cntr64(&dd->misc_err_status_cnt[i]);
	}
}

static void handle_pio_err(struct hfi1_devdata *dd, u32 unused, u64 reg)
{
	char buf[96];
	int i = 0;

	dd_dev_info(dd, "PIO Error: %s\n",
		    pio_err_status_string(buf, sizeof(buf), reg));

	if (reg & ALL_PIO_FREEZE_ERR)
		start_freeze_handling(dd->pport, 0);

	for (i = 0; i < NUM_SEND_PIO_ERR_STATUS_COUNTERS; i++) {
		if (reg & (1ull << i))
			incr_cntr64(&dd->send_pio_err_status_cnt[i]);
	}
}

static void handle_sdma_err(struct hfi1_devdata *dd, u32 unused, u64 reg)
{
	char buf[96];
	int i = 0;

	dd_dev_info(dd, "SDMA Error: %s\n",
		    sdma_err_status_string(buf, sizeof(buf), reg));

	if (reg & ALL_SDMA_FREEZE_ERR)
		start_freeze_handling(dd->pport, 0);

	for (i = 0; i < NUM_SEND_DMA_ERR_STATUS_COUNTERS; i++) {
		if (reg & (1ull << i))
			incr_cntr64(&dd->send_dma_err_status_cnt[i]);
	}
}

static inline void __count_port_discards(struct hfi1_pportdata *ppd)
{
	incr_cntr64(&ppd->port_xmit_discards);
}

static void count_port_inactive(struct hfi1_devdata *dd)
{
	__count_port_discards(dd->pport);
}

/*
 * We have had a "disallowed packet" error during egress. Determine the
 * integrity check which failed, and update relevant error counter, etc.
 *
 * Note that the SEND_EGRESS_ERR_INFO register has only a single
 * bit of state per integrity check, and so we can miss the reason for an
 * egress error if more than one packet fails the same integrity check
 * since we cleared the corresponding bit in SEND_EGRESS_ERR_INFO.
 */
static void handle_send_egress_err_info(struct hfi1_devdata *dd,
					int vl)
{
	struct hfi1_pportdata *ppd = dd->pport;
	u64 src = read_csr(dd, SEND_EGRESS_ERR_SOURCE); /* read first */
	u64 info = read_csr(dd, SEND_EGRESS_ERR_INFO);
	char buf[96];

	/* clear down all observed info as quickly as possible after read */
	write_csr(dd, SEND_EGRESS_ERR_INFO, info);

	dd_dev_info(dd,
		    "Egress Error Info: 0x%llx, %s Egress Error Src 0x%llx\n",
		    info, egress_err_info_string(buf, sizeof(buf), info), src);

	/* Eventually add other counters for each bit */
	if (info & PORT_DISCARD_EGRESS_ERRS) {
		int weight, i;

		/*
		 * Count all applicable bits as individual errors and
		 * attribute them to the packet that triggered this handler.
		 * This may not be completely accurate due to limitations
		 * on the available hardware error information.  There is
		 * a single information register and any number of error
		 * packets may have occurred and contributed to it before
		 * this routine is called.  This means that:
		 * a) If multiple packets with the same error occur before
		 *    this routine is called, earlier packets are missed.
		 *    There is only a single bit for each error type.
		 * b) Errors may not be attributed to the correct VL.
		 *    The driver is attributing all bits in the info register
		 *    to the packet that triggered this call, but bits
		 *    could be an accumulation of different packets with
		 *    different VLs.
		 * c) A single error packet may have multiple counts attached
		 *    to it.  There is no way for the driver to know if
		 *    multiple bits set in the info register are due to a
		 *    single packet or multiple packets.  The driver assumes
		 *    multiple packets.
		 */
		weight = hweight64(info & PORT_DISCARD_EGRESS_ERRS);
		for (i = 0; i < weight; i++) {
			__count_port_discards(ppd);
			if (vl >= 0 && vl < TXE_NUM_DATA_VL)
				incr_cntr64(&ppd->port_xmit_discards_vl[vl]);
			else if (vl == 15)
				incr_cntr64(&ppd->port_xmit_discards_vl
					    [C_VL_15]);
		}
	}
}

/*
 * Input value is a bit position within the SEND_EGRESS_ERR_STATUS
 * register. Does it represent a 'port inactive' error?
 */
static inline int port_inactive_err(u64 posn)
{
	return (posn >= SEES(TX_LINKDOWN) &&
		posn <= SEES(TX_INCORRECT_LINK_STATE));
}

/*
 * Input value is a bit position within the SEND_EGRESS_ERR_STATUS
 * register. Does it represent a 'disallowed packet' error?
 */
static inline int disallowed_pkt_err(int posn)
{
	return (posn >= SEES(TX_SDMA0_DISALLOWED_PACKET) &&
		posn <= SEES(TX_SDMA15_DISALLOWED_PACKET));
}

/*
 * Input value is a bit position of one of the SDMA engine disallowed
 * packet errors.  Return which engine.  Use of this must be guarded by
 * disallowed_pkt_err().
 */
static inline int disallowed_pkt_engine(int posn)
{
	return posn - SEES(TX_SDMA0_DISALLOWED_PACKET);
}

/*
 * Translate an SDMA engine to a VL.  Return -1 if the tranlation cannot
 * be done.
 */
static int engine_to_vl(struct hfi1_devdata *dd, int engine)
{
	struct sdma_vl_map *m;
	int vl;

	/* range check */
	if (engine < 0 || engine >= TXE_NUM_SDMA_ENGINES)
		return -1;

	rcu_read_lock();
	m = rcu_dereference(dd->sdma_map);
	vl = m->engine_to_vl[engine];
	rcu_read_unlock();

	return vl;
}

/*
 * Translate the send context (sofware index) into a VL.  Return -1 if the
 * translation cannot be done.
 */
static int sc_to_vl(struct hfi1_devdata *dd, int sw_index)
{
	struct send_context_info *sci;
	struct send_context *sc;
	int i;

	sci = &dd->send_contexts[sw_index];

	/* there is no information for user (PSM) and ack contexts */
	if ((sci->type != SC_KERNEL) && (sci->type != SC_VL15))
		return -1;

	sc = sci->sc;
	if (!sc)
		return -1;
	if (dd->vld[15].sc == sc)
		return 15;
	for (i = 0; i < num_vls; i++)
		if (dd->vld[i].sc == sc)
			return i;

	return -1;
}

static void handle_egress_err(struct hfi1_devdata *dd, u32 unused, u64 reg)
{
	u64 reg_copy = reg, handled = 0;
	char buf[96];
	int i = 0;

	if (reg & ALL_TXE_EGRESS_FREEZE_ERR)
		start_freeze_handling(dd->pport, 0);
	else if (is_ax(dd) &&
		 (reg & SEND_EGRESS_ERR_STATUS_TX_CREDIT_RETURN_VL_ERR_SMASK) &&
		 (dd->icode != ICODE_FUNCTIONAL_SIMULATOR))
		start_freeze_handling(dd->pport, 0);

	while (reg_copy) {
		int posn = fls64(reg_copy);
		/* fls64() returns a 1-based offset, we want it zero based */
		int shift = posn - 1;
		u64 mask = 1ULL << shift;

		if (port_inactive_err(shift)) {
			count_port_inactive(dd);
			handled |= mask;
		} else if (disallowed_pkt_err(shift)) {
			int vl = engine_to_vl(dd, disallowed_pkt_engine(shift));

			handle_send_egress_err_info(dd, vl);
			handled |= mask;
		}
		reg_copy &= ~mask;
	}

	reg &= ~handled;

	if (reg)
		dd_dev_info(dd, "Egress Error: %s\n",
			    egress_err_status_string(buf, sizeof(buf), reg));

	for (i = 0; i < NUM_SEND_EGRESS_ERR_STATUS_COUNTERS; i++) {
		if (reg & (1ull << i))
			incr_cntr64(&dd->send_egress_err_status_cnt[i]);
	}
}

static void handle_txe_err(struct hfi1_devdata *dd, u32 unused, u64 reg)
{
	char buf[96];
	int i = 0;

	dd_dev_info(dd, "Send Error: %s\n",
		    send_err_status_string(buf, sizeof(buf), reg));

	for (i = 0; i < NUM_SEND_ERR_STATUS_COUNTERS; i++) {
		if (reg & (1ull << i))
			incr_cntr64(&dd->send_err_status_cnt[i]);
	}
}

/*
 * The maximum number of times the error clear down will loop before
 * blocking a repeating error.  This value is arbitrary.
 */
#define MAX_CLEAR_COUNT 20

/*
 * Clear and handle an error register.  All error interrupts are funneled
 * through here to have a central location to correctly handle single-
 * or multi-shot errors.
 *
 * For non per-context registers, call this routine with a context value
 * of 0 so the per-context offset is zero.
 *
 * If the handler loops too many times, assume that something is wrong
 * and can't be fixed, so mask the error bits.
 */
static void interrupt_clear_down(struct hfi1_devdata *dd,
				 u32 context,
				 const struct err_reg_info *eri)
{
	u64 reg;
	u32 count;

	/* read in a loop until no more errors are seen */
	count = 0;
	while (1) {
		reg = read_kctxt_csr(dd, context, eri->status);
		if (reg == 0)
			break;
		write_kctxt_csr(dd, context, eri->clear, reg);
		if (likely(eri->handler))
			eri->handler(dd, context, reg);
		count++;
		if (count > MAX_CLEAR_COUNT) {
			u64 mask;

			dd_dev_err(dd, "Repeating %s bits 0x%llx - masking\n",
				   eri->desc, reg);
			/*
			 * Read-modify-write so any other masked bits
			 * remain masked.
			 */
			mask = read_kctxt_csr(dd, context, eri->mask);
			mask &= ~reg;
			write_kctxt_csr(dd, context, eri->mask, mask);
			break;
		}
	}
}

/*
 * CCE block "misc" interrupt.  Source is < 16.
 */
static void is_misc_err_int(struct hfi1_devdata *dd, unsigned int source)
{
	const struct err_reg_info *eri = &misc_errs[source];

	if (eri->handler) {
		interrupt_clear_down(dd, 0, eri);
	} else {
		dd_dev_err(dd, "Unexpected misc interrupt (%u) - reserved\n",
			   source);
	}
}

static char *send_context_err_status_string(char *buf, int buf_len, u64 flags)
{
	return flag_string(buf, buf_len, flags,
			   sc_err_status_flags,
			   ARRAY_SIZE(sc_err_status_flags));
}

/*
 * Send context error interrupt.  Source (hw_context) is < 160.
 *
 * All send context errors cause the send context to halt.  The normal
 * clear-down mechanism cannot be used because we cannot clear the
 * error bits until several other long-running items are done first.
 * This is OK because with the context halted, nothing else is going
 * to happen on it anyway.
 */
static void is_sendctxt_err_int(struct hfi1_devdata *dd,
				unsigned int hw_context)
{
	struct send_context_info *sci;
	struct send_context *sc;
	char flags[96];
	u64 status;
	u32 sw_index;
	int i = 0;
	unsigned long irq_flags;

	sw_index = dd->hw_to_sw[hw_context];
	if (sw_index >= dd->num_send_contexts) {
		dd_dev_err(dd,
			   "out of range sw index %u for send context %u\n",
			   sw_index, hw_context);
		return;
	}
	sci = &dd->send_contexts[sw_index];
	spin_lock_irqsave(&dd->sc_lock, irq_flags);
	sc = sci->sc;
	if (!sc) {
		dd_dev_err(dd, "%s: context %u(%u): no sc?\n", __func__,
			   sw_index, hw_context);
		spin_unlock_irqrestore(&dd->sc_lock, irq_flags);
		return;
	}

	/* tell the software that a halt has begun */
	sc_stop(sc, SCF_HALTED);

	status = read_kctxt_csr(dd, hw_context, SEND_CTXT_ERR_STATUS);

	dd_dev_info(dd, "Send Context %u(%u) Error: %s\n", sw_index, hw_context,
		    send_context_err_status_string(flags, sizeof(flags),
						   status));

	if (status & SEND_CTXT_ERR_STATUS_PIO_DISALLOWED_PACKET_ERR_SMASK)
		handle_send_egress_err_info(dd, sc_to_vl(dd, sw_index));

	/*
	 * Automatically restart halted kernel contexts out of interrupt
	 * context.  User contexts must ask the driver to restart the context.
	 */
	if (sc->type != SC_USER)
		queue_work(dd->pport->hfi1_wq, &sc->halt_work);
	spin_unlock_irqrestore(&dd->sc_lock, irq_flags);

	/*
	 * Update the counters for the corresponding status bits.
	 * Note that these particular counters are aggregated over all
	 * 160 contexts.
	 */
	for (i = 0; i < NUM_SEND_CTXT_ERR_STATUS_COUNTERS; i++) {
		if (status & (1ull << i))
			incr_cntr64(&dd->sw_ctxt_err_status_cnt[i]);
	}
}

static void handle_sdma_eng_err(struct hfi1_devdata *dd,
				unsigned int source, u64 status)
{
	struct sdma_engine *sde;
	int i = 0;

	sde = &dd->per_sdma[source];
#ifdef CONFIG_SDMA_VERBOSITY
	dd_dev_err(sde->dd, "CONFIG SDMA(%u) %s:%d %s()\n", sde->this_idx,
		   slashstrip(__FILE__), __LINE__, __func__);
	dd_dev_err(sde->dd, "CONFIG SDMA(%u) source: %u status 0x%llx\n",
		   sde->this_idx, source, (unsigned long long)status);
#endif
	sde->err_cnt++;
	sdma_engine_error(sde, status);

	/*
	* Update the counters for the corresponding status bits.
	* Note that these particular counters are aggregated over
	* all 16 DMA engines.
	*/
	for (i = 0; i < NUM_SEND_DMA_ENG_ERR_STATUS_COUNTERS; i++) {
		if (status & (1ull << i))
			incr_cntr64(&dd->sw_send_dma_eng_err_status_cnt[i]);
	}
}

/*
 * CCE block SDMA error interrupt.  Source is < 16.
 */
static void is_sdma_eng_err_int(struct hfi1_devdata *dd, unsigned int source)
{
#ifdef CONFIG_SDMA_VERBOSITY
	struct sdma_engine *sde = &dd->per_sdma[source];

	dd_dev_err(dd, "CONFIG SDMA(%u) %s:%d %s()\n", sde->this_idx,
		   slashstrip(__FILE__), __LINE__, __func__);
	dd_dev_err(dd, "CONFIG SDMA(%u) source: %u\n", sde->this_idx,
		   source);
	sdma_dumpstate(sde);
#endif
	interrupt_clear_down(dd, source, &sdma_eng_err);
}

/*
 * CCE block "various" interrupt.  Source is < 8.
 */
static void is_various_int(struct hfi1_devdata *dd, unsigned int source)
{
	const struct err_reg_info *eri = &various_err[source];

	/*
	 * TCritInt cannot go through interrupt_clear_down()
	 * because it is not a second tier interrupt. The handler
	 * should be called directly.
	 */
	if (source == TCRIT_INT_SOURCE)
		handle_temp_err(dd);
	else if (eri->handler)
		interrupt_clear_down(dd, 0, eri);
	else
		dd_dev_info(dd,
			    "%s: Unimplemented/reserved interrupt %d\n",
			    __func__, source);
}

static void handle_qsfp_int(struct hfi1_devdata *dd, u32 src_ctx, u64 reg)
{
	/* src_ctx is always zero */
	struct hfi1_pportdata *ppd = dd->pport;
	unsigned long flags;
	u64 qsfp_int_mgmt = (u64)(QSFP_HFI0_INT_N | QSFP_HFI0_MODPRST_N);

	if (reg & QSFP_HFI0_MODPRST_N) {
		if (!qsfp_mod_present(ppd)) {
			dd_dev_info(dd, "%s: QSFP module removed\n",
				    __func__);

			ppd->driver_link_ready = 0;
			/*
			 * Cable removed, reset all our information about the
			 * cache and cable capabilities
			 */

			spin_lock_irqsave(&ppd->qsfp_info.qsfp_lock, flags);
			/*
			 * We don't set cache_refresh_required here as we expect
			 * an interrupt when a cable is inserted
			 */
			ppd->qsfp_info.cache_valid = 0;
			ppd->qsfp_info.reset_needed = 0;
			ppd->qsfp_info.limiting_active = 0;
			spin_unlock_irqrestore(&ppd->qsfp_info.qsfp_lock,
					       flags);
			/* Invert the ModPresent pin now to detect plug-in */
			write_csr(dd, dd->hfi1_id ? ASIC_QSFP2_INVERT :
				  ASIC_QSFP1_INVERT, qsfp_int_mgmt);

			if ((ppd->offline_disabled_reason >
			  HFI1_ODR_MASK(
			  OPA_LINKDOWN_REASON_LOCAL_MEDIA_NOT_INSTALLED)) ||
			  (ppd->offline_disabled_reason ==
			  HFI1_ODR_MASK(OPA_LINKDOWN_REASON_NONE)))
				ppd->offline_disabled_reason =
				HFI1_ODR_MASK(
				OPA_LINKDOWN_REASON_LOCAL_MEDIA_NOT_INSTALLED);

			if (ppd->host_link_state == HLS_DN_POLL) {
				/*
				 * The link is still in POLL. This means
				 * that the normal link down processing
				 * will not happen. We have to do it here
				 * before turning the DC off.
				 */
				queue_work(ppd->link_wq, &ppd->link_down_work);
			}
		} else {
			dd_dev_info(dd, "%s: QSFP module inserted\n",
				    __func__);

			spin_lock_irqsave(&ppd->qsfp_info.qsfp_lock, flags);
			ppd->qsfp_info.cache_valid = 0;
			ppd->qsfp_info.cache_refresh_required = 1;
			spin_unlock_irqrestore(&ppd->qsfp_info.qsfp_lock,
					       flags);

			/*
			 * Stop inversion of ModPresent pin to detect
			 * removal of the cable
			 */
			qsfp_int_mgmt &= ~(u64)QSFP_HFI0_MODPRST_N;
			write_csr(dd, dd->hfi1_id ? ASIC_QSFP2_INVERT :
				  ASIC_QSFP1_INVERT, qsfp_int_mgmt);

			ppd->offline_disabled_reason =
				HFI1_ODR_MASK(OPA_LINKDOWN_REASON_TRANSIENT);
		}
	}

	if (reg & QSFP_HFI0_INT_N) {
		dd_dev_info(dd, "%s: Interrupt received from QSFP module\n",
			    __func__);
		spin_lock_irqsave(&ppd->qsfp_info.qsfp_lock, flags);
		ppd->qsfp_info.check_interrupt_flags = 1;
		spin_unlock_irqrestore(&ppd->qsfp_info.qsfp_lock, flags);
	}

	/* Schedule the QSFP work only if there is a cable attached. */
	if (qsfp_mod_present(ppd))
		queue_work(ppd->link_wq, &ppd->qsfp_info.qsfp_work);
}

static int request_host_lcb_access(struct hfi1_devdata *dd)
{
	int ret;

	ret = do_8051_command(dd, HCMD_MISC,
			      (u64)HCMD_MISC_REQUEST_LCB_ACCESS <<
			      LOAD_DATA_FIELD_ID_SHIFT, NULL);
	if (ret != HCMD_SUCCESS) {
		dd_dev_err(dd, "%s: command failed with error %d\n",
			   __func__, ret);
	}
	return ret == HCMD_SUCCESS ? 0 : -EBUSY;
}

static int request_8051_lcb_access(struct hfi1_devdata *dd)
{
	int ret;

	ret = do_8051_command(dd, HCMD_MISC,
			      (u64)HCMD_MISC_GRANT_LCB_ACCESS <<
			      LOAD_DATA_FIELD_ID_SHIFT, NULL);
	if (ret != HCMD_SUCCESS) {
		dd_dev_err(dd, "%s: command failed with error %d\n",
			   __func__, ret);
	}
	return ret == HCMD_SUCCESS ? 0 : -EBUSY;
}

/*
 * Set the LCB selector - allow host access.  The DCC selector always
 * points to the host.
 */
static inline void set_host_lcb_access(struct hfi1_devdata *dd)
{
	write_csr(dd, DC_DC8051_CFG_CSR_ACCESS_SEL,
		  DC_DC8051_CFG_CSR_ACCESS_SEL_DCC_SMASK |
		  DC_DC8051_CFG_CSR_ACCESS_SEL_LCB_SMASK);
}

/*
 * Clear the LCB selector - allow 8051 access.  The DCC selector always
 * points to the host.
 */
static inline void set_8051_lcb_access(struct hfi1_devdata *dd)
{
	write_csr(dd, DC_DC8051_CFG_CSR_ACCESS_SEL,
		  DC_DC8051_CFG_CSR_ACCESS_SEL_DCC_SMASK);
}

/*
 * Acquire LCB access from the 8051.  If the host already has access,
 * just increment a counter.  Otherwise, inform the 8051 that the
 * host is taking access.
 *
 * Returns:
 *	0 on success
 *	-EBUSY if the 8051 has control and cannot be disturbed
 *	-errno if unable to acquire access from the 8051
 */
int acquire_lcb_access(struct hfi1_devdata *dd, int sleep_ok)
{
	struct hfi1_pportdata *ppd = dd->pport;
	int ret = 0;

	/*
	 * Use the host link state lock so the operation of this routine
	 * { link state check, selector change, count increment } can occur
	 * as a unit against a link state change.  Otherwise there is a
	 * race between the state change and the count increment.
	 */
	if (sleep_ok) {
		mutex_lock(&ppd->hls_lock);
	} else {
		while (!mutex_trylock(&ppd->hls_lock))
			udelay(1);
	}

	/* this access is valid only when the link is up */
	if (ppd->host_link_state & HLS_DOWN) {
		dd_dev_info(dd, "%s: link state %s not up\n",
			    __func__, link_state_name(ppd->host_link_state));
		ret = -EBUSY;
		goto done;
	}

	if (dd->lcb_access_count == 0) {
		ret = request_host_lcb_access(dd);
		if (ret) {
			dd_dev_err(dd,
				   "%s: unable to acquire LCB access, err %d\n",
				   __func__, ret);
			goto done;
		}
		set_host_lcb_access(dd);
	}
	dd->lcb_access_count++;
done:
	mutex_unlock(&ppd->hls_lock);
	return ret;
}

/*
 * Release LCB access by decrementing the use count.  If the count is moving
 * from 1 to 0, inform 8051 that it has control back.
 *
 * Returns:
 *	0 on success
 *	-errno if unable to release access to the 8051
 */
int release_lcb_access(struct hfi1_devdata *dd, int sleep_ok)
{
	int ret = 0;

	/*
	 * Use the host link state lock because the acquire needed it.
	 * Here, we only need to keep { selector change, count decrement }
	 * as a unit.
	 */
	if (sleep_ok) {
		mutex_lock(&dd->pport->hls_lock);
	} else {
		while (!mutex_trylock(&dd->pport->hls_lock))
			udelay(1);
	}

	if (dd->lcb_access_count == 0) {
		dd_dev_err(dd, "%s: LCB access count is zero.  Skipping.\n",
			   __func__);
		goto done;
	}

	if (dd->lcb_access_count == 1) {
		set_8051_lcb_access(dd);
		ret = request_8051_lcb_access(dd);
		if (ret) {
			dd_dev_err(dd,
				   "%s: unable to release LCB access, err %d\n",
				   __func__, ret);
			/* restore host access if the grant didn't work */
			set_host_lcb_access(dd);
			goto done;
		}
	}
	dd->lcb_access_count--;
done:
	mutex_unlock(&dd->pport->hls_lock);
	return ret;
}

/*
 * Initialize LCB access variables and state.  Called during driver load,
 * after most of the initialization is finished.
 *
 * The DC default is LCB access on for the host.  The driver defaults to
 * leaving access to the 8051.  Assign access now - this constrains the call
 * to this routine to be after all LCB set-up is done.  In particular, after
 * hf1_init_dd() -> set_up_interrupts() -> clear_all_interrupts()
 */
static void init_lcb_access(struct hfi1_devdata *dd)
{
	dd->lcb_access_count = 0;
}

/*
 * Write a response back to a 8051 request.
 */
static void hreq_response(struct hfi1_devdata *dd, u8 return_code, u16 rsp_data)
{
	write_csr(dd, DC_DC8051_CFG_EXT_DEV_0,
		  DC_DC8051_CFG_EXT_DEV_0_COMPLETED_SMASK |
		  (u64)return_code <<
		  DC_DC8051_CFG_EXT_DEV_0_RETURN_CODE_SHIFT |
		  (u64)rsp_data << DC_DC8051_CFG_EXT_DEV_0_RSP_DATA_SHIFT);
}

/*
 * Handle host requests from the 8051.
 */
static void handle_8051_request(struct hfi1_pportdata *ppd)
{
	struct hfi1_devdata *dd = ppd->dd;
	u64 reg;
	u16 data = 0;
	u8 type;

	reg = read_csr(dd, DC_DC8051_CFG_EXT_DEV_1);
	if ((reg & DC_DC8051_CFG_EXT_DEV_1_REQ_NEW_SMASK) == 0)
		return;	/* no request */

	/* zero out COMPLETED so the response is seen */
	write_csr(dd, DC_DC8051_CFG_EXT_DEV_0, 0);

	/* extract request details */
	type = (reg >> DC_DC8051_CFG_EXT_DEV_1_REQ_TYPE_SHIFT)
			& DC_DC8051_CFG_EXT_DEV_1_REQ_TYPE_MASK;
	data = (reg >> DC_DC8051_CFG_EXT_DEV_1_REQ_DATA_SHIFT)
			& DC_DC8051_CFG_EXT_DEV_1_REQ_DATA_MASK;

	switch (type) {
	case HREQ_LOAD_CONFIG:
	case HREQ_SAVE_CONFIG:
	case HREQ_READ_CONFIG:
	case HREQ_SET_TX_EQ_ABS:
	case HREQ_SET_TX_EQ_REL:
	case HREQ_ENABLE:
		dd_dev_info(dd, "8051 request: request 0x%x not supported\n",
			    type);
		hreq_response(dd, HREQ_NOT_SUPPORTED, 0);
		break;
	case HREQ_LCB_RESET:
		/* Put the LCB, RX FPE and TX FPE into reset */
		write_csr(dd, DCC_CFG_RESET, LCB_RX_FPE_TX_FPE_INTO_RESET);
		/* Make sure the write completed */
		(void)read_csr(dd, DCC_CFG_RESET);
		/* Hold the reset long enough to take effect */
		udelay(1);
		/* Take the LCB, RX FPE and TX FPE out of reset */
		write_csr(dd, DCC_CFG_RESET, LCB_RX_FPE_TX_FPE_OUT_OF_RESET);
		hreq_response(dd, HREQ_SUCCESS, 0);

		break;
	case HREQ_CONFIG_DONE:
		hreq_response(dd, HREQ_SUCCESS, 0);
		break;

	case HREQ_INTERFACE_TEST:
		hreq_response(dd, HREQ_SUCCESS, data);
		break;
	default:
		dd_dev_err(dd, "8051 request: unknown request 0x%x\n", type);
		hreq_response(dd, HREQ_NOT_SUPPORTED, 0);
		break;
	}
}

/*
 * Set up allocation unit vaulue.
 */
void set_up_vau(struct hfi1_devdata *dd, u8 vau)
{
	u64 reg = read_csr(dd, SEND_CM_GLOBAL_CREDIT);

	/* do not modify other values in the register */
	reg &= ~SEND_CM_GLOBAL_CREDIT_AU_SMASK;
	reg |= (u64)vau << SEND_CM_GLOBAL_CREDIT_AU_SHIFT;
	write_csr(dd, SEND_CM_GLOBAL_CREDIT, reg);
}

/*
 * Set up initial VL15 credits of the remote.  Assumes the rest of
 * the CM credit registers are zero from a previous global or credit reset.
 * Shared limit for VL15 will always be 0.
 */
void set_up_vl15(struct hfi1_devdata *dd, u16 vl15buf)
{
	u64 reg = read_csr(dd, SEND_CM_GLOBAL_CREDIT);

	/* set initial values for total and shared credit limit */
	reg &= ~(SEND_CM_GLOBAL_CREDIT_TOTAL_CREDIT_LIMIT_SMASK |
		 SEND_CM_GLOBAL_CREDIT_SHARED_LIMIT_SMASK);

	/*
	 * Set total limit to be equal to VL15 credits.
	 * Leave shared limit at 0.
	 */
	reg |= (u64)vl15buf << SEND_CM_GLOBAL_CREDIT_TOTAL_CREDIT_LIMIT_SHIFT;
	write_csr(dd, SEND_CM_GLOBAL_CREDIT, reg);

	write_csr(dd, SEND_CM_CREDIT_VL15, (u64)vl15buf
		  << SEND_CM_CREDIT_VL15_DEDICATED_LIMIT_VL_SHIFT);
}

/*
 * Zero all credit details from the previous connection and
 * reset the CM manager's internal counters.
 */
void reset_link_credits(struct hfi1_devdata *dd)
{
	int i;

	/* remove all previous VL credit limits */
	for (i = 0; i < TXE_NUM_DATA_VL; i++)
		write_csr(dd, SEND_CM_CREDIT_VL + (8 * i), 0);
	write_csr(dd, SEND_CM_CREDIT_VL15, 0);
	write_csr(dd, SEND_CM_GLOBAL_CREDIT, 0);
	/* reset the CM block */
	pio_send_control(dd, PSC_CM_RESET);
	/* reset cached value */
	dd->vl15buf_cached = 0;
}

/* convert a vCU to a CU */
static u32 vcu_to_cu(u8 vcu)
{
	return 1 << vcu;
}

/* convert a CU to a vCU */
static u8 cu_to_vcu(u32 cu)
{
	return ilog2(cu);
}

/* convert a vAU to an AU */
static u32 vau_to_au(u8 vau)
{
	return 8 * (1 << vau);
}

static void set_linkup_defaults(struct hfi1_pportdata *ppd)
{
	ppd->sm_trap_qp = 0x0;
	ppd->sa_qp = 0x1;
}

/*
 * Graceful LCB shutdown.  This leaves the LCB FIFOs in reset.
 */
static void lcb_shutdown(struct hfi1_devdata *dd, int abort)
{
	u64 reg;

	/* clear lcb run: LCB_CFG_RUN.EN = 0 */
	write_csr(dd, DC_LCB_CFG_RUN, 0);
	/* set tx fifo reset: LCB_CFG_TX_FIFOS_RESET.VAL = 1 */
	write_csr(dd, DC_LCB_CFG_TX_FIFOS_RESET,
		  1ull << DC_LCB_CFG_TX_FIFOS_RESET_VAL_SHIFT);
	/* set dcc reset csr: DCC_CFG_RESET.{reset_lcb,reset_rx_fpe} = 1 */
	dd->lcb_err_en = read_csr(dd, DC_LCB_ERR_EN);
	reg = read_csr(dd, DCC_CFG_RESET);
	write_csr(dd, DCC_CFG_RESET, reg |
		  DCC_CFG_RESET_RESET_LCB | DCC_CFG_RESET_RESET_RX_FPE);
	(void)read_csr(dd, DCC_CFG_RESET); /* make sure the write completed */
	if (!abort) {
		udelay(1);    /* must hold for the longer of 16cclks or 20ns */
		write_csr(dd, DCC_CFG_RESET, reg);
		write_csr(dd, DC_LCB_ERR_EN, dd->lcb_err_en);
	}
}

/*
 * This routine should be called after the link has been transitioned to
 * OFFLINE (OFFLINE state has the side effect of putting the SerDes into
 * reset).
 *
 * The expectation is that the caller of this routine would have taken
 * care of properly transitioning the link into the correct state.
 * NOTE: the caller needs to acquire the dd->dc8051_lock lock
 *       before calling this function.
 */
static void _dc_shutdown(struct hfi1_devdata *dd)
{
	lockdep_assert_held(&dd->dc8051_lock);

	if (dd->dc_shutdown)
		return;

	dd->dc_shutdown = 1;
	/* Shutdown the LCB */
	lcb_shutdown(dd, 1);
	/*
	 * Going to OFFLINE would have causes the 8051 to put the
	 * SerDes into reset already. Just need to shut down the 8051,
	 * itself.
	 */
	write_csr(dd, DC_DC8051_CFG_RST, 0x1);
}

static void dc_shutdown(struct hfi1_devdata *dd)
{
	mutex_lock(&dd->dc8051_lock);
	_dc_shutdown(dd);
	mutex_unlock(&dd->dc8051_lock);
}

/*
 * Calling this after the DC has been brought out of reset should not
 * do any damage.
 * NOTE: the caller needs to acquire the dd->dc8051_lock lock
 *       before calling this function.
 */
static void _dc_start(struct hfi1_devdata *dd)
{
	lockdep_assert_held(&dd->dc8051_lock);

	if (!dd->dc_shutdown)
		return;

	/* Take the 8051 out of reset */
	write_csr(dd, DC_DC8051_CFG_RST, 0ull);
	/* Wait until 8051 is ready */
	if (wait_fm_ready(dd, TIMEOUT_8051_START))
		dd_dev_err(dd, "%s: timeout starting 8051 firmware\n",
			   __func__);

	/* Take away reset for LCB and RX FPE (set in lcb_shutdown). */
	write_csr(dd, DCC_CFG_RESET, LCB_RX_FPE_TX_FPE_OUT_OF_RESET);
	/* lcb_shutdown() with abort=1 does not restore these */
	write_csr(dd, DC_LCB_ERR_EN, dd->lcb_err_en);
	dd->dc_shutdown = 0;
}

static void dc_start(struct hfi1_devdata *dd)
{
	mutex_lock(&dd->dc8051_lock);
	_dc_start(dd);
	mutex_unlock(&dd->dc8051_lock);
}

/*
 * These LCB adjustments are for the Aurora SerDes core in the FPGA.
 */
static void adjust_lcb_for_fpga_serdes(struct hfi1_devdata *dd)
{
	u64 rx_radr, tx_radr;
	u32 version;

	if (dd->icode != ICODE_FPGA_EMULATION)
		return;

	/*
	 * These LCB defaults on emulator _s are good, nothing to do here:
	 *	LCB_CFG_TX_FIFOS_RADR
	 *	LCB_CFG_RX_FIFOS_RADR
	 *	LCB_CFG_LN_DCLK
	 *	LCB_CFG_IGNORE_LOST_RCLK
	 */
	if (is_emulator_s(dd))
		return;
	/* else this is _p */

	version = emulator_rev(dd);
	if (!is_ax(dd))
		version = 0x2d;	/* all B0 use 0x2d or higher settings */

	if (version <= 0x12) {
		/* release 0x12 and below */

		/*
		 * LCB_CFG_RX_FIFOS_RADR.RST_VAL = 0x9
		 * LCB_CFG_RX_FIFOS_RADR.OK_TO_JUMP_VAL = 0x9
		 * LCB_CFG_RX_FIFOS_RADR.DO_NOT_JUMP_VAL = 0xa
		 */
		rx_radr =
		      0xaull << DC_LCB_CFG_RX_FIFOS_RADR_DO_NOT_JUMP_VAL_SHIFT
		    | 0x9ull << DC_LCB_CFG_RX_FIFOS_RADR_OK_TO_JUMP_VAL_SHIFT
		    | 0x9ull << DC_LCB_CFG_RX_FIFOS_RADR_RST_VAL_SHIFT;
		/*
		 * LCB_CFG_TX_FIFOS_RADR.ON_REINIT = 0 (default)
		 * LCB_CFG_TX_FIFOS_RADR.RST_VAL = 6
		 */
		tx_radr = 6ull << DC_LCB_CFG_TX_FIFOS_RADR_RST_VAL_SHIFT;
	} else if (version <= 0x18) {
		/* release 0x13 up to 0x18 */
		/* LCB_CFG_RX_FIFOS_RADR = 0x988 */
		rx_radr =
		      0x9ull << DC_LCB_CFG_RX_FIFOS_RADR_DO_NOT_JUMP_VAL_SHIFT
		    | 0x8ull << DC_LCB_CFG_RX_FIFOS_RADR_OK_TO_JUMP_VAL_SHIFT
		    | 0x8ull << DC_LCB_CFG_RX_FIFOS_RADR_RST_VAL_SHIFT;
		tx_radr = 7ull << DC_LCB_CFG_TX_FIFOS_RADR_RST_VAL_SHIFT;
	} else if (version == 0x19) {
		/* release 0x19 */
		/* LCB_CFG_RX_FIFOS_RADR = 0xa99 */
		rx_radr =
		      0xAull << DC_LCB_CFG_RX_FIFOS_RADR_DO_NOT_JUMP_VAL_SHIFT
		    | 0x9ull << DC_LCB_CFG_RX_FIFOS_RADR_OK_TO_JUMP_VAL_SHIFT
		    | 0x9ull << DC_LCB_CFG_RX_FIFOS_RADR_RST_VAL_SHIFT;
		tx_radr = 3ull << DC_LCB_CFG_TX_FIFOS_RADR_RST_VAL_SHIFT;
	} else if (version == 0x1a) {
		/* release 0x1a */
		/* LCB_CFG_RX_FIFOS_RADR = 0x988 */
		rx_radr =
		      0x9ull << DC_LCB_CFG_RX_FIFOS_RADR_DO_NOT_JUMP_VAL_SHIFT
		    | 0x8ull << DC_LCB_CFG_RX_FIFOS_RADR_OK_TO_JUMP_VAL_SHIFT
		    | 0x8ull << DC_LCB_CFG_RX_FIFOS_RADR_RST_VAL_SHIFT;
		tx_radr = 7ull << DC_LCB_CFG_TX_FIFOS_RADR_RST_VAL_SHIFT;
		write_csr(dd, DC_LCB_CFG_LN_DCLK, 1ull);
	} else {
		/* release 0x1b and higher */
		/* LCB_CFG_RX_FIFOS_RADR = 0x877 */
		rx_radr =
		      0x8ull << DC_LCB_CFG_RX_FIFOS_RADR_DO_NOT_JUMP_VAL_SHIFT
		    | 0x7ull << DC_LCB_CFG_RX_FIFOS_RADR_OK_TO_JUMP_VAL_SHIFT
		    | 0x7ull << DC_LCB_CFG_RX_FIFOS_RADR_RST_VAL_SHIFT;
		tx_radr = 3ull << DC_LCB_CFG_TX_FIFOS_RADR_RST_VAL_SHIFT;
	}

	write_csr(dd, DC_LCB_CFG_RX_FIFOS_RADR, rx_radr);
	/* LCB_CFG_IGNORE_LOST_RCLK.EN = 1 */
	write_csr(dd, DC_LCB_CFG_IGNORE_LOST_RCLK,
		  DC_LCB_CFG_IGNORE_LOST_RCLK_EN_SMASK);
	write_csr(dd, DC_LCB_CFG_TX_FIFOS_RADR, tx_radr);
}

/*
 * Handle a SMA idle message
 *
 * This is a work-queue function outside of the interrupt.
 */
void handle_sma_message(struct work_struct *work)
{
	struct hfi1_pportdata *ppd = container_of(work, struct hfi1_pportdata,
							sma_message_work);
	struct hfi1_devdata *dd = ppd->dd;
	u64 msg;
	int ret;

	/*
	 * msg is bytes 1-4 of the 40-bit idle message - the command code
	 * is stripped off
	 */
	ret = read_idle_sma(dd, &msg);
	if (ret)
		return;
	dd_dev_info(dd, "%s: SMA message 0x%llx\n", __func__, msg);
	/*
	 * React to the SMA message.  Byte[1] (0 for us) is the command.
	 */
	switch (msg & 0xff) {
	case SMA_IDLE_ARM:
		/*
		 * See OPAv1 table 9-14 - HFI and External Switch Ports Key
		 * State Transitions
		 *
		 * Only expected in INIT or ARMED, discard otherwise.
		 */
		if (ppd->host_link_state & (HLS_UP_INIT | HLS_UP_ARMED))
			ppd->neighbor_normal = 1;
		break;
	case SMA_IDLE_ACTIVE:
		/*
		 * See OPAv1 table 9-14 - HFI and External Switch Ports Key
		 * State Transitions
		 *
		 * Can activate the node.  Discard otherwise.
		 */
		if (ppd->host_link_state == HLS_UP_ARMED &&
		    ppd->is_active_optimize_enabled) {
			ppd->neighbor_normal = 1;
			ret = set_link_state(ppd, HLS_UP_ACTIVE);
			if (ret)
				dd_dev_err(
					dd,
					"%s: received Active SMA idle message, couldn't set link to Active\n",
					__func__);
		}
		break;
	default:
		dd_dev_err(dd,
			   "%s: received unexpected SMA idle message 0x%llx\n",
			   __func__, msg);
		break;
	}
}

static void adjust_rcvctrl(struct hfi1_devdata *dd, u64 add, u64 clear)
{
	u64 rcvctrl;
	unsigned long flags;

	spin_lock_irqsave(&dd->rcvctrl_lock, flags);
	rcvctrl = read_csr(dd, RCV_CTRL);
	rcvctrl |= add;
	rcvctrl &= ~clear;
	write_csr(dd, RCV_CTRL, rcvctrl);
	spin_unlock_irqrestore(&dd->rcvctrl_lock, flags);
}

static inline void add_rcvctrl(struct hfi1_devdata *dd, u64 add)
{
	adjust_rcvctrl(dd, add, 0);
}

static inline void clear_rcvctrl(struct hfi1_devdata *dd, u64 clear)
{
	adjust_rcvctrl(dd, 0, clear);
}

/*
 * Called from all interrupt handlers to start handling an SPC freeze.
 */
void start_freeze_handling(struct hfi1_pportdata *ppd, int flags)
{
	struct hfi1_devdata *dd = ppd->dd;
	struct send_context *sc;
	int i;
	int sc_flags;

	if (flags & FREEZE_SELF)
		write_csr(dd, CCE_CTRL, CCE_CTRL_SPC_FREEZE_SMASK);

	/* enter frozen mode */
	dd->flags |= HFI1_FROZEN;

	/* notify all SDMA engines that they are going into a freeze */
	sdma_freeze_notify(dd, !!(flags & FREEZE_LINK_DOWN));

	sc_flags = SCF_FROZEN | SCF_HALTED | (flags & FREEZE_LINK_DOWN ?
					      SCF_LINK_DOWN : 0);
	/* do halt pre-handling on all enabled send contexts */
	for (i = 0; i < dd->num_send_contexts; i++) {
		sc = dd->send_contexts[i].sc;
		if (sc && (sc->flags & SCF_ENABLED))
			sc_stop(sc, sc_flags);
	}

	/* Send context are frozen. Notify user space */
	hfi1_set_uevent_bits(ppd, _HFI1_EVENT_FROZEN_BIT);

	if (flags & FREEZE_ABORT) {
		dd_dev_err(dd,
			   "Aborted freeze recovery. Please REBOOT system\n");
		return;
	}
	/* queue non-interrupt handler */
	queue_work(ppd->hfi1_wq, &ppd->freeze_work);
}

/*
 * Wait until all 4 sub-blocks indicate that they have frozen or unfrozen,
 * depending on the "freeze" parameter.
 *
 * No need to return an error if it times out, our only option
 * is to proceed anyway.
 */
static void wait_for_freeze_status(struct hfi1_devdata *dd, int freeze)
{
	unsigned long timeout;
	u64 reg;

	timeout = jiffies + msecs_to_jiffies(FREEZE_STATUS_TIMEOUT);
	while (1) {
		reg = read_csr(dd, CCE_STATUS);
		if (freeze) {
			/* waiting until all indicators are set */
			if ((reg & ALL_FROZE) == ALL_FROZE)
				return;	/* all done */
		} else {
			/* waiting until all indicators are clear */
			if ((reg & ALL_FROZE) == 0)
				return; /* all done */
		}

		if (time_after(jiffies, timeout)) {
			dd_dev_err(dd,
				   "Time out waiting for SPC %sfreeze, bits 0x%llx, expecting 0x%llx, continuing",
				   freeze ? "" : "un", reg & ALL_FROZE,
				   freeze ? ALL_FROZE : 0ull);
			return;
		}
		usleep_range(80, 120);
	}
}

/*
 * Do all freeze handling for the RXE block.
 */
static void rxe_freeze(struct hfi1_devdata *dd)
{
	int i;
	struct hfi1_ctxtdata *rcd;

	/* disable port */
	clear_rcvctrl(dd, RCV_CTRL_RCV_PORT_ENABLE_SMASK);

	/* disable all receive contexts */
	for (i = 0; i < dd->num_rcv_contexts; i++) {
		rcd = hfi1_rcd_get_by_index(dd, i);
		hfi1_rcvctrl(dd, HFI1_RCVCTRL_CTXT_DIS, rcd);
		hfi1_rcd_put(rcd);
	}
}

/*
 * Unfreeze handling for the RXE block - kernel contexts only.
 * This will also enable the port.  User contexts will do unfreeze
 * handling on a per-context basis as they call into the driver.
 *
 */
static void rxe_kernel_unfreeze(struct hfi1_devdata *dd)
{
	u32 rcvmask;
	u16 i;
	struct hfi1_ctxtdata *rcd;

	/* enable all kernel contexts */
	for (i = 0; i < dd->num_rcv_contexts; i++) {
		rcd = hfi1_rcd_get_by_index(dd, i);

		/* Ensure all non-user contexts(including vnic) are enabled */
		if (!rcd ||
		    (i >= dd->first_dyn_alloc_ctxt && !rcd->is_vnic)) {
			hfi1_rcd_put(rcd);
			continue;
		}
		rcvmask = HFI1_RCVCTRL_CTXT_ENB;
		/* HFI1_RCVCTRL_TAILUPD_[ENB|DIS] needs to be set explicitly */
		rcvmask |= hfi1_rcvhdrtail_kvaddr(rcd) ?
			HFI1_RCVCTRL_TAILUPD_ENB : HFI1_RCVCTRL_TAILUPD_DIS;
		hfi1_rcvctrl(dd, rcvmask, rcd);
		hfi1_rcd_put(rcd);
	}

	/* enable port */
	add_rcvctrl(dd, RCV_CTRL_RCV_PORT_ENABLE_SMASK);
}

/*
 * Non-interrupt SPC freeze handling.
 *
 * This is a work-queue function outside of the triggering interrupt.
 */
void handle_freeze(struct work_struct *work)
{
	struct hfi1_pportdata *ppd = container_of(work, struct hfi1_pportdata,
								freeze_work);
	struct hfi1_devdata *dd = ppd->dd;

	/* wait for freeze indicators on all affected blocks */
	wait_for_freeze_status(dd, 1);

	/* SPC is now frozen */

	/* do send PIO freeze steps */
	pio_freeze(dd);

	/* do send DMA freeze steps */
	sdma_freeze(dd);

	/* do send egress freeze steps - nothing to do */

	/* do receive freeze steps */
	rxe_freeze(dd);

	/*
	 * Unfreeze the hardware - clear the freeze, wait for each
	 * block's frozen bit to clear, then clear the frozen flag.
	 */
	write_csr(dd, CCE_CTRL, CCE_CTRL_SPC_UNFREEZE_SMASK);
	wait_for_freeze_status(dd, 0);

	if (is_ax(dd)) {
		write_csr(dd, CCE_CTRL, CCE_CTRL_SPC_FREEZE_SMASK);
		wait_for_freeze_status(dd, 1);
		write_csr(dd, CCE_CTRL, CCE_CTRL_SPC_UNFREEZE_SMASK);
		wait_for_freeze_status(dd, 0);
	}

	/* do send PIO unfreeze steps for kernel contexts */
	pio_kernel_unfreeze(dd);

	/* do send DMA unfreeze steps */
	sdma_unfreeze(dd);

	/* do send egress unfreeze steps - nothing to do */

	/* do receive unfreeze steps for kernel contexts */
	rxe_kernel_unfreeze(dd);

	/*
	 * The unfreeze procedure touches global device registers when
	 * it disables and re-enables RXE. Mark the device unfrozen
	 * after all that is done so other parts of the driver waiting
	 * for the device to unfreeze don't do things out of order.
	 *
	 * The above implies that the meaning of HFI1_FROZEN flag is
	 * "Device has gone into freeze mode and freeze mode handling
	 * is still in progress."
	 *
	 * The flag will be removed when freeze mode processing has
	 * completed.
	 */
	dd->flags &= ~HFI1_FROZEN;
	wake_up(&dd->event_queue);

	/* no longer frozen */
}

/**
 * update_xmit_counters - update PortXmitWait/PortVlXmitWait
 * counters.
 * @ppd: info of physical Hfi port
 * @link_width: new link width after link up or downgrade
 *
 * Update the PortXmitWait and PortVlXmitWait counters after
 * a link up or downgrade event to reflect a link width change.
 */
static void update_xmit_counters(struct hfi1_pportdata *ppd, u16 link_width)
{
	int i;
	u16 tx_width;
	u16 link_speed;

	tx_width = tx_link_width(link_width);
	link_speed = get_link_speed(ppd->link_speed_active);

	/*
	 * There are C_VL_COUNT number of PortVLXmitWait counters.
	 * Adding 1 to C_VL_COUNT to include the PortXmitWait counter.
	 */
	for (i = 0; i < C_VL_COUNT + 1; i++)
		get_xmit_wait_counters(ppd, tx_width, link_speed, i);
}

/*
 * Handle a link up interrupt from the 8051.
 *
 * This is a work-queue function outside of the interrupt.
 */
void handle_link_up(struct work_struct *work)
{
	struct hfi1_pportdata *ppd = container_of(work, struct hfi1_pportdata,
						  link_up_work);
	struct hfi1_devdata *dd = ppd->dd;

	set_link_state(ppd, HLS_UP_INIT);

	/* cache the read of DC_LCB_STS_ROUND_TRIP_LTP_CNT */
	read_ltp_rtt(dd);
	/*
	 * OPA specifies that certain counters are cleared on a transition
	 * to link up, so do that.
	 */
	clear_linkup_counters(dd);
	/*
	 * And (re)set link up default values.
	 */
	set_linkup_defaults(ppd);

	/*
	 * Set VL15 credits. Use cached value from verify cap interrupt.
	 * In case of quick linkup or simulator, vl15 value will be set by
	 * handle_linkup_change. VerifyCap interrupt handler will not be
	 * called in those scenarios.
	 */
	if (!(quick_linkup || dd->icode == ICODE_FUNCTIONAL_SIMULATOR))
		set_up_vl15(dd, dd->vl15buf_cached);

	/* enforce link speed enabled */
	if ((ppd->link_speed_active & ppd->link_speed_enabled) == 0) {
		/* oops - current speed is not enabled, bounce */
		dd_dev_err(dd,
			   "Link speed active 0x%x is outside enabled 0x%x, downing link\n",
			   ppd->link_speed_active, ppd->link_speed_enabled);
		set_link_down_reason(ppd, OPA_LINKDOWN_REASON_SPEED_POLICY, 0,
				     OPA_LINKDOWN_REASON_SPEED_POLICY);
		set_link_state(ppd, HLS_DN_OFFLINE);
		start_link(ppd);
	}
}

/*
 * Several pieces of LNI information were cached for SMA in ppd.
 * Reset these on link down
 */
static void reset_neighbor_info(struct hfi1_pportdata *ppd)
{
	ppd->neighbor_guid = 0;
	ppd->neighbor_port_number = 0;
	ppd->neighbor_type = 0;
	ppd->neighbor_fm_security = 0;
}

static const char * const link_down_reason_strs[] = {
	[OPA_LINKDOWN_REASON_NONE] = "None",
	[OPA_LINKDOWN_REASON_RCV_ERROR_0] = "Receive error 0",
	[OPA_LINKDOWN_REASON_BAD_PKT_LEN] = "Bad packet length",
	[OPA_LINKDOWN_REASON_PKT_TOO_LONG] = "Packet too long",
	[OPA_LINKDOWN_REASON_PKT_TOO_SHORT] = "Packet too short",
	[OPA_LINKDOWN_REASON_BAD_SLID] = "Bad SLID",
	[OPA_LINKDOWN_REASON_BAD_DLID] = "Bad DLID",
	[OPA_LINKDOWN_REASON_BAD_L2] = "Bad L2",
	[OPA_LINKDOWN_REASON_BAD_SC] = "Bad SC",
	[OPA_LINKDOWN_REASON_RCV_ERROR_8] = "Receive error 8",
	[OPA_LINKDOWN_REASON_BAD_MID_TAIL] = "Bad mid tail",
	[OPA_LINKDOWN_REASON_RCV_ERROR_10] = "Receive error 10",
	[OPA_LINKDOWN_REASON_PREEMPT_ERROR] = "Preempt error",
	[OPA_LINKDOWN_REASON_PREEMPT_VL15] = "Preempt vl15",
	[OPA_LINKDOWN_REASON_BAD_VL_MARKER] = "Bad VL marker",
	[OPA_LINKDOWN_REASON_RCV_ERROR_14] = "Receive error 14",
	[OPA_LINKDOWN_REASON_RCV_ERROR_15] = "Receive error 15",
	[OPA_LINKDOWN_REASON_BAD_HEAD_DIST] = "Bad head distance",
	[OPA_LINKDOWN_REASON_BAD_TAIL_DIST] = "Bad tail distance",
	[OPA_LINKDOWN_REASON_BAD_CTRL_DIST] = "Bad control distance",
	[OPA_LINKDOWN_REASON_BAD_CREDIT_ACK] = "Bad credit ack",
	[OPA_LINKDOWN_REASON_UNSUPPORTED_VL_MARKER] = "Unsupported VL marker",
	[OPA_LINKDOWN_REASON_BAD_PREEMPT] = "Bad preempt",
	[OPA_LINKDOWN_REASON_BAD_CONTROL_FLIT] = "Bad control flit",
	[OPA_LINKDOWN_REASON_EXCEED_MULTICAST_LIMIT] = "Exceed multicast limit",
	[OPA_LINKDOWN_REASON_RCV_ERROR_24] = "Receive error 24",
	[OPA_LINKDOWN_REASON_RCV_ERROR_25] = "Receive error 25",
	[OPA_LINKDOWN_REASON_RCV_ERROR_26] = "Receive error 26",
	[OPA_LINKDOWN_REASON_RCV_ERROR_27] = "Receive error 27",
	[OPA_LINKDOWN_REASON_RCV_ERROR_28] = "Receive error 28",
	[OPA_LINKDOWN_REASON_RCV_ERROR_29] = "Receive error 29",
	[OPA_LINKDOWN_REASON_RCV_ERROR_30] = "Receive error 30",
	[OPA_LINKDOWN_REASON_EXCESSIVE_BUFFER_OVERRUN] =
					"Excessive buffer overrun",
	[OPA_LINKDOWN_REASON_UNKNOWN] = "Unknown",
	[OPA_LINKDOWN_REASON_REBOOT] = "Reboot",
	[OPA_LINKDOWN_REASON_NEIGHBOR_UNKNOWN] = "Neighbor unknown",
	[OPA_LINKDOWN_REASON_FM_BOUNCE] = "FM bounce",
	[OPA_LINKDOWN_REASON_SPEED_POLICY] = "Speed policy",
	[OPA_LINKDOWN_REASON_WIDTH_POLICY] = "Width policy",
	[OPA_LINKDOWN_REASON_DISCONNECTED] = "Disconnected",
	[OPA_LINKDOWN_REASON_LOCAL_MEDIA_NOT_INSTALLED] =
					"Local media not installed",
	[OPA_LINKDOWN_REASON_NOT_INSTALLED] = "Not installed",
	[OPA_LINKDOWN_REASON_CHASSIS_CONFIG] = "Chassis config",
	[OPA_LINKDOWN_REASON_END_TO_END_NOT_INSTALLED] =
					"End to end not installed",
	[OPA_LINKDOWN_REASON_POWER_POLICY] = "Power policy",
	[OPA_LINKDOWN_REASON_LINKSPEED_POLICY] = "Link speed policy",
	[OPA_LINKDOWN_REASON_LINKWIDTH_POLICY] = "Link width policy",
	[OPA_LINKDOWN_REASON_SWITCH_MGMT] = "Switch management",
	[OPA_LINKDOWN_REASON_SMA_DISABLED] = "SMA disabled",
	[OPA_LINKDOWN_REASON_TRANSIENT] = "Transient"
};

/* return the neighbor link down reason string */
static const char *link_down_reason_str(u8 reason)
{
	const char *str = NULL;

	if (reason < ARRAY_SIZE(link_down_reason_strs))
		str = link_down_reason_strs[reason];
	if (!str)
		str = "(invalid)";

	return str;
}

/*
 * Handle a link down interrupt from the 8051.
 *
 * This is a work-queue function outside of the interrupt.
 */
void handle_link_down(struct work_struct *work)
{
	u8 lcl_reason, neigh_reason = 0;
	u8 link_down_reason;
	struct hfi1_pportdata *ppd = container_of(work, struct hfi1_pportdata,
						  link_down_work);
	int was_up;
	static const char ldr_str[] = "Link down reason: ";

	if ((ppd->host_link_state &
	     (HLS_DN_POLL | HLS_VERIFY_CAP | HLS_GOING_UP)) &&
	     ppd->port_type == PORT_TYPE_FIXED)
		ppd->offline_disabled_reason =
			HFI1_ODR_MASK(OPA_LINKDOWN_REASON_NOT_INSTALLED);

	/* Go offline first, then deal with reading/writing through 8051 */
	was_up = !!(ppd->host_link_state & HLS_UP);
	set_link_state(ppd, HLS_DN_OFFLINE);
	xchg(&ppd->is_link_down_queued, 0);

	if (was_up) {
		lcl_reason = 0;
		/* link down reason is only valid if the link was up */
		read_link_down_reason(ppd->dd, &link_down_reason);
		switch (link_down_reason) {
		case LDR_LINK_TRANSFER_ACTIVE_LOW:
			/* the link went down, no idle message reason */
			dd_dev_info(ppd->dd, "%sUnexpected link down\n",
				    ldr_str);
			break;
		case LDR_RECEIVED_LINKDOWN_IDLE_MSG:
			/*
			 * The neighbor reason is only valid if an idle message
			 * was received for it.
			 */
			read_planned_down_reason_code(ppd->dd, &neigh_reason);
			dd_dev_info(ppd->dd,
				    "%sNeighbor link down message %d, %s\n",
				    ldr_str, neigh_reason,
				    link_down_reason_str(neigh_reason));
			break;
		case LDR_RECEIVED_HOST_OFFLINE_REQ:
			dd_dev_info(ppd->dd,
				    "%sHost requested link to go offline\n",
				    ldr_str);
			break;
		default:
			dd_dev_info(ppd->dd, "%sUnknown reason 0x%x\n",
				    ldr_str, link_down_reason);
			break;
		}

		/*
		 * If no reason, assume peer-initiated but missed
		 * LinkGoingDown idle flits.
		 */
		if (neigh_reason == 0)
			lcl_reason = OPA_LINKDOWN_REASON_NEIGHBOR_UNKNOWN;
	} else {
		/* went down while polling or going up */
		lcl_reason = OPA_LINKDOWN_REASON_TRANSIENT;
	}

	set_link_down_reason(ppd, lcl_reason, neigh_reason, 0);

	/* inform the SMA when the link transitions from up to down */
	if (was_up && ppd->local_link_down_reason.sma == 0 &&
	    ppd->neigh_link_down_reason.sma == 0) {
		ppd->local_link_down_reason.sma =
					ppd->local_link_down_reason.latest;
		ppd->neigh_link_down_reason.sma =
					ppd->neigh_link_down_reason.latest;
	}

	reset_neighbor_info(ppd);

	/* disable the port */
	clear_rcvctrl(ppd->dd, RCV_CTRL_RCV_PORT_ENABLE_SMASK);

	/*
	 * If there is no cable attached, turn the DC off. Otherwise,
	 * start the link bring up.
	 */
	if (ppd->port_type == PORT_TYPE_QSFP && !qsfp_mod_present(ppd))
		dc_shutdown(ppd->dd);
	else
		start_link(ppd);
}

void handle_link_bounce(struct work_struct *work)
{
	struct hfi1_pportdata *ppd = container_of(work, struct hfi1_pportdata,
							link_bounce_work);

	/*
	 * Only do something if the link is currently up.
	 */
	if (ppd->host_link_state & HLS_UP) {
		set_link_state(ppd, HLS_DN_OFFLINE);
		start_link(ppd);
	} else {
		dd_dev_info(ppd->dd, "%s: link not up (%s), nothing to do\n",
			    __func__, link_state_name(ppd->host_link_state));
	}
}

/*
 * Mask conversion: Capability exchange to Port LTP.  The capability
 * exchange has an implicit 16b CRC that is mandatory.
 */
static int cap_to_port_ltp(int cap)
{
	int port_ltp = PORT_LTP_CRC_MODE_16; /* this mode is mandatory */

	if (cap & CAP_CRC_14B)
		port_ltp |= PORT_LTP_CRC_MODE_14;
	if (cap & CAP_CRC_48B)
		port_ltp |= PORT_LTP_CRC_MODE_48;
	if (cap & CAP_CRC_12B_16B_PER_LANE)
		port_ltp |= PORT_LTP_CRC_MODE_PER_LANE;

	return port_ltp;
}

/*
 * Convert an OPA Port LTP mask to capability mask
 */
int port_ltp_to_cap(int port_ltp)
{
	int cap_mask = 0;

	if (port_ltp & PORT_LTP_CRC_MODE_14)
		cap_mask |= CAP_CRC_14B;
	if (port_ltp & PORT_LTP_CRC_MODE_48)
		cap_mask |= CAP_CRC_48B;
	if (port_ltp & PORT_LTP_CRC_MODE_PER_LANE)
		cap_mask |= CAP_CRC_12B_16B_PER_LANE;

	return cap_mask;
}

/*
 * Convert a single DC LCB CRC mode to an OPA Port LTP mask.
 */
static int lcb_to_port_ltp(int lcb_crc)
{
	int port_ltp = 0;

	if (lcb_crc == LCB_CRC_12B_16B_PER_LANE)
		port_ltp = PORT_LTP_CRC_MODE_PER_LANE;
	else if (lcb_crc == LCB_CRC_48B)
		port_ltp = PORT_LTP_CRC_MODE_48;
	else if (lcb_crc == LCB_CRC_14B)
		port_ltp = PORT_LTP_CRC_MODE_14;
	else
		port_ltp = PORT_LTP_CRC_MODE_16;

	return port_ltp;
}

static void clear_full_mgmt_pkey(struct hfi1_pportdata *ppd)
{
	if (ppd->pkeys[2] != 0) {
		ppd->pkeys[2] = 0;
		(void)hfi1_set_ib_cfg(ppd, HFI1_IB_CFG_PKEYS, 0);
		hfi1_event_pkey_change(ppd->dd, ppd->port);
	}
}

/*
 * Convert the given link width to the OPA link width bitmask.
 */
static u16 link_width_to_bits(struct hfi1_devdata *dd, u16 width)
{
	switch (width) {
	case 0:
		/*
		 * Simulator and quick linkup do not set the width.
		 * Just set it to 4x without complaint.
		 */
		if (dd->icode == ICODE_FUNCTIONAL_SIMULATOR || quick_linkup)
			return OPA_LINK_WIDTH_4X;
		return 0; /* no lanes up */
	case 1: return OPA_LINK_WIDTH_1X;
	case 2: return OPA_LINK_WIDTH_2X;
	case 3: return OPA_LINK_WIDTH_3X;
	default:
		dd_dev_info(dd, "%s: invalid width %d, using 4\n",
			    __func__, width);
		/* fall through */
	case 4: return OPA_LINK_WIDTH_4X;
	}
}

/*
 * Do a population count on the bottom nibble.
 */
static const u8 bit_counts[16] = {
	0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4
};

static inline u8 nibble_to_count(u8 nibble)
{
	return bit_counts[nibble & 0xf];
}

/*
 * Read the active lane information from the 8051 registers and return
 * their widths.
 *
 * Active lane information is found in these 8051 registers:
 *	enable_lane_tx
 *	enable_lane_rx
 */
static void get_link_widths(struct hfi1_devdata *dd, u16 *tx_width,
			    u16 *rx_width)
{
	u16 tx, rx;
	u8 enable_lane_rx;
	u8 enable_lane_tx;
	u8 tx_polarity_inversion;
	u8 rx_polarity_inversion;
	u8 max_rate;

	/* read the active lanes */
	read_tx_settings(dd, &enable_lane_tx, &tx_polarity_inversion,
			 &rx_polarity_inversion, &max_rate);
	read_local_lni(dd, &enable_lane_rx);

	/* convert to counts */
	tx = nibble_to_count(enable_lane_tx);
	rx = nibble_to_count(enable_lane_rx);

	/*
	 * Set link_speed_active here, overriding what was set in
	 * handle_verify_cap().  The ASIC 8051 firmware does not correctly
	 * set the max_rate field in handle_verify_cap until v0.19.
	 */
	if ((dd->icode == ICODE_RTL_SILICON) &&
	    (dd->dc8051_ver < dc8051_ver(0, 19, 0))) {
		/* max_rate: 0 = 12.5G, 1 = 25G */
		switch (max_rate) {
		case 0:
			dd->pport[0].link_speed_active = OPA_LINK_SPEED_12_5G;
			break;
		default:
			dd_dev_err(dd,
				   "%s: unexpected max rate %d, using 25Gb\n",
				   __func__, (int)max_rate);
			/* fall through */
		case 1:
			dd->pport[0].link_speed_active = OPA_LINK_SPEED_25G;
			break;
		}
	}

	dd_dev_info(dd,
		    "Fabric active lanes (width): tx 0x%x (%d), rx 0x%x (%d)\n",
		    enable_lane_tx, tx, enable_lane_rx, rx);
	*tx_width = link_width_to_bits(dd, tx);
	*rx_width = link_width_to_bits(dd, rx);
}

/*
 * Read verify_cap_local_fm_link_width[1] to obtain the link widths.
 * Valid after the end of VerifyCap and during LinkUp.  Does not change
 * after link up.  I.e. look elsewhere for downgrade information.
 *
 * Bits are:
 *	+ bits [7:4] contain the number of active transmitters
 *	+ bits [3:0] contain the number of active receivers
 * These are numbers 1 through 4 and can be different values if the
 * link is asymmetric.
 *
 * verify_cap_local_fm_link_width[0] retains its original value.
 */
static void get_linkup_widths(struct hfi1_devdata *dd, u16 *tx_width,
			      u16 *rx_width)
{
	u16 widths, tx, rx;
	u8 misc_bits, local_flags;
	u16 active_tx, active_rx;

	read_vc_local_link_mode(dd, &misc_bits, &local_flags, &widths);
	tx = widths >> 12;
	rx = (widths >> 8) & 0xf;

	*tx_width = link_width_to_bits(dd, tx);
	*rx_width = link_width_to_bits(dd, rx);

	/* print the active widths */
	get_link_widths(dd, &active_tx, &active_rx);
}

/*
 * Set ppd->link_width_active and ppd->link_width_downgrade_active using
 * hardware information when the link first comes up.
 *
 * The link width is not available until after VerifyCap.AllFramesReceived
 * (the trigger for handle_verify_cap), so this is outside that routine
 * and should be called when the 8051 signals linkup.
 */
void get_linkup_link_widths(struct hfi1_pportdata *ppd)
{
	u16 tx_width, rx_width;

	/* get end-of-LNI link widths */
	get_linkup_widths(ppd->dd, &tx_width, &rx_width);

	/* use tx_width as the link is supposed to be symmetric on link up */
	ppd->link_width_active = tx_width;
	/* link width downgrade active (LWD.A) starts out matching LW.A */
	ppd->link_width_downgrade_tx_active = ppd->link_width_active;
	ppd->link_width_downgrade_rx_active = ppd->link_width_active;
	/* per OPA spec, on link up LWD.E resets to LWD.S */
	ppd->link_width_downgrade_enabled = ppd->link_width_downgrade_supported;
	/* cache the active egress rate (units {10^6 bits/sec]) */
	ppd->current_egress_rate = active_egress_rate(ppd);
}

/*
 * Handle a verify capabilities interrupt from the 8051.
 *
 * This is a work-queue function outside of the interrupt.
 */
void handle_verify_cap(struct work_struct *work)
{
	struct hfi1_pportdata *ppd = container_of(work, struct hfi1_pportdata,
								link_vc_work);
	struct hfi1_devdata *dd = ppd->dd;
	u64 reg;
	u8 power_management;
	u8 continuous;
	u8 vcu;
	u8 vau;
	u8 z;
	u16 vl15buf;
	u16 link_widths;
	u16 crc_mask;
	u16 crc_val;
	u16 device_id;
	u16 active_tx, active_rx;
	u8 partner_supported_crc;
	u8 remote_tx_rate;
	u8 device_rev;

	set_link_state(ppd, HLS_VERIFY_CAP);

	lcb_shutdown(dd, 0);
	adjust_lcb_for_fpga_serdes(dd);

	read_vc_remote_phy(dd, &power_management, &continuous);
	read_vc_remote_fabric(dd, &vau, &z, &vcu, &vl15buf,
			      &partner_supported_crc);
	read_vc_remote_link_width(dd, &remote_tx_rate, &link_widths);
	read_remote_device_id(dd, &device_id, &device_rev);

	/* print the active widths */
	get_link_widths(dd, &active_tx, &active_rx);
	dd_dev_info(dd,
		    "Peer PHY: power management 0x%x, continuous updates 0x%x\n",
		    (int)power_management, (int)continuous);
	dd_dev_info(dd,
		    "Peer Fabric: vAU %d, Z %d, vCU %d, vl15 credits 0x%x, CRC sizes 0x%x\n",
		    (int)vau, (int)z, (int)vcu, (int)vl15buf,
		    (int)partner_supported_crc);
	dd_dev_info(dd, "Peer Link Width: tx rate 0x%x, widths 0x%x\n",
		    (u32)remote_tx_rate, (u32)link_widths);
	dd_dev_info(dd, "Peer Device ID: 0x%04x, Revision 0x%02x\n",
		    (u32)device_id, (u32)device_rev);
	/*
	 * The peer vAU value just read is the peer receiver value.  HFI does
	 * not support a transmit vAU of 0 (AU == 8).  We advertised that
	 * with Z=1 in the fabric capabilities sent to the peer.  The peer
	 * will see our Z=1, and, if it advertised a vAU of 0, will move its
	 * receive to vAU of 1 (AU == 16).  Do the same here.  We do not care
	 * about the peer Z value - our sent vAU is 3 (hardwired) and is not
	 * subject to the Z value exception.
	 */
	if (vau == 0)
		vau = 1;
	set_up_vau(dd, vau);

	/*
	 * Set VL15 credits to 0 in global credit register. Cache remote VL15
	 * credits value and wait for link-up interrupt ot set it.
	 */
	set_up_vl15(dd, 0);
	dd->vl15buf_cached = vl15buf;

	/* set up the LCB CRC mode */
	crc_mask = ppd->port_crc_mode_enabled & partner_supported_crc;

	/* order is important: use the lowest bit in common */
	if (crc_mask & CAP_CRC_14B)
		crc_val = LCB_CRC_14B;
	else if (crc_mask & CAP_CRC_48B)
		crc_val = LCB_CRC_48B;
	else if (crc_mask & CAP_CRC_12B_16B_PER_LANE)
		crc_val = LCB_CRC_12B_16B_PER_LANE;
	else
		crc_val = LCB_CRC_16B;

	dd_dev_info(dd, "Final LCB CRC mode: %d\n", (int)crc_val);
	write_csr(dd, DC_LCB_CFG_CRC_MODE,
		  (u64)crc_val << DC_LCB_CFG_CRC_MODE_TX_VAL_SHIFT);

	/* set (14b only) or clear sideband credit */
	reg = read_csr(dd, SEND_CM_CTRL);
	if (crc_val == LCB_CRC_14B && crc_14b_sideband) {
		write_csr(dd, SEND_CM_CTRL,
			  reg | SEND_CM_CTRL_FORCE_CREDIT_MODE_SMASK);
	} else {
		write_csr(dd, SEND_CM_CTRL,
			  reg & ~SEND_CM_CTRL_FORCE_CREDIT_MODE_SMASK);
	}

	ppd->link_speed_active = 0;	/* invalid value */
	if (dd->dc8051_ver < dc8051_ver(0, 20, 0)) {
		/* remote_tx_rate: 0 = 12.5G, 1 = 25G */
		switch (remote_tx_rate) {
		case 0:
			ppd->link_speed_active = OPA_LINK_SPEED_12_5G;
			break;
		case 1:
			ppd->link_speed_active = OPA_LINK_SPEED_25G;
			break;
		}
	} else {
		/* actual rate is highest bit of the ANDed rates */
		u8 rate = remote_tx_rate & ppd->local_tx_rate;

		if (rate & 2)
			ppd->link_speed_active = OPA_LINK_SPEED_25G;
		else if (rate & 1)
			ppd->link_speed_active = OPA_LINK_SPEED_12_5G;
	}
	if (ppd->link_speed_active == 0) {
		dd_dev_err(dd, "%s: unexpected remote tx rate %d, using 25Gb\n",
			   __func__, (int)remote_tx_rate);
		ppd->link_speed_active = OPA_LINK_SPEED_25G;
	}

	/*
	 * Cache the values of the supported, enabled, and active
	 * LTP CRC modes to return in 'portinfo' queries. But the bit
	 * flags that are returned in the portinfo query differ from
	 * what's in the link_crc_mask, crc_sizes, and crc_val
	 * variables. Convert these here.
	 */
	ppd->port_ltp_crc_mode = cap_to_port_ltp(link_crc_mask) << 8;
		/* supported crc modes */
	ppd->port_ltp_crc_mode |=
		cap_to_port_ltp(ppd->port_crc_mode_enabled) << 4;
		/* enabled crc modes */
	ppd->port_ltp_crc_mode |= lcb_to_port_ltp(crc_val);
		/* active crc mode */

	/* set up the remote credit return table */
	assign_remote_cm_au_table(dd, vcu);

	/*
	 * The LCB is reset on entry to handle_verify_cap(), so this must
	 * be applied on every link up.
	 *
	 * Adjust LCB error kill enable to kill the link if
	 * these RBUF errors are seen:
	 *	REPLAY_BUF_MBE_SMASK
	 *	FLIT_INPUT_BUF_MBE_SMASK
	 */
	if (is_ax(dd)) {			/* fixed in B0 */
		reg = read_csr(dd, DC_LCB_CFG_LINK_KILL_EN);
		reg |= DC_LCB_CFG_LINK_KILL_EN_REPLAY_BUF_MBE_SMASK
			| DC_LCB_CFG_LINK_KILL_EN_FLIT_INPUT_BUF_MBE_SMASK;
		write_csr(dd, DC_LCB_CFG_LINK_KILL_EN, reg);
	}

	/* pull LCB fifos out of reset - all fifo clocks must be stable */
	write_csr(dd, DC_LCB_CFG_TX_FIFOS_RESET, 0);

	/* give 8051 access to the LCB CSRs */
	write_csr(dd, DC_LCB_ERR_EN, 0); /* mask LCB errors */
	set_8051_lcb_access(dd);

	/* tell the 8051 to go to LinkUp */
	set_link_state(ppd, HLS_GOING_UP);
}

/**
 * apply_link_downgrade_policy - Apply the link width downgrade enabled
 * policy against the current active link widths.
 * @ppd: info of physical Hfi port
 * @refresh_widths: True indicates link downgrade event
 * @return: True indicates a successful link downgrade. False indicates
 *	    link downgrade event failed and the link will bounce back to
 *	    default link width.
 *
 * Called when the enabled policy changes or the active link widths
 * change.
 * Refresh_widths indicates that a link downgrade occurred. The
 * link_downgraded variable is set by refresh_widths and
 * determines the success/failure of the policy application.
 */
bool apply_link_downgrade_policy(struct hfi1_pportdata *ppd,
				 bool refresh_widths)
{
	int do_bounce = 0;
	int tries;
	u16 lwde;
	u16 tx, rx;
	bool link_downgraded = refresh_widths;

	/* use the hls lock to avoid a race with actual link up */
	tries = 0;
retry:
	mutex_lock(&ppd->hls_lock);
	/* only apply if the link is up */
	if (ppd->host_link_state & HLS_DOWN) {
		/* still going up..wait and retry */
		if (ppd->host_link_state & HLS_GOING_UP) {
			if (++tries < 1000) {
				mutex_unlock(&ppd->hls_lock);
				usleep_range(100, 120); /* arbitrary */
				goto retry;
			}
			dd_dev_err(ppd->dd,
				   "%s: giving up waiting for link state change\n",
				   __func__);
		}
		goto done;
	}

	lwde = ppd->link_width_downgrade_enabled;

	if (refresh_widths) {
		get_link_widths(ppd->dd, &tx, &rx);
		ppd->link_width_downgrade_tx_active = tx;
		ppd->link_width_downgrade_rx_active = rx;
	}

	if (ppd->link_width_downgrade_tx_active == 0 ||
	    ppd->link_width_downgrade_rx_active == 0) {
		/* the 8051 reported a dead link as a downgrade */
		dd_dev_err(ppd->dd, "Link downgrade is really a link down, ignoring\n");
		link_downgraded = false;
	} else if (lwde == 0) {
		/* downgrade is disabled */

		/* bounce if not at starting active width */
		if ((ppd->link_width_active !=
		     ppd->link_width_downgrade_tx_active) ||
		    (ppd->link_width_active !=
		     ppd->link_width_downgrade_rx_active)) {
			dd_dev_err(ppd->dd,
				   "Link downgrade is disabled and link has downgraded, downing link\n");
			dd_dev_err(ppd->dd,
				   "  original 0x%x, tx active 0x%x, rx active 0x%x\n",
				   ppd->link_width_active,
				   ppd->link_width_downgrade_tx_active,
				   ppd->link_width_downgrade_rx_active);
			do_bounce = 1;
			link_downgraded = false;
		}
	} else if ((lwde & ppd->link_width_downgrade_tx_active) == 0 ||
		   (lwde & ppd->link_width_downgrade_rx_active) == 0) {
		/* Tx or Rx is outside the enabled policy */
		dd_dev_err(ppd->dd,
			   "Link is outside of downgrade allowed, downing link\n");
		dd_dev_err(ppd->dd,
			   "  enabled 0x%x, tx active 0x%x, rx active 0x%x\n",
			   lwde, ppd->link_width_downgrade_tx_active,
			   ppd->link_width_downgrade_rx_active);
		do_bounce = 1;
		link_downgraded = false;
	}

done:
	mutex_unlock(&ppd->hls_lock);

	if (do_bounce) {
		set_link_down_reason(ppd, OPA_LINKDOWN_REASON_WIDTH_POLICY, 0,
				     OPA_LINKDOWN_REASON_WIDTH_POLICY);
		set_link_state(ppd, HLS_DN_OFFLINE);
		start_link(ppd);
	}

	return link_downgraded;
}

/*
 * Handle a link downgrade interrupt from the 8051.
 *
 * This is a work-queue function outside of the interrupt.
 */
void handle_link_downgrade(struct work_struct *work)
{
	struct hfi1_pportdata *ppd = container_of(work, struct hfi1_pportdata,
							link_downgrade_work);

	dd_dev_info(ppd->dd, "8051: Link width downgrade\n");
	if (apply_link_downgrade_policy(ppd, true))
		update_xmit_counters(ppd, ppd->link_width_downgrade_tx_active);
}

static char *dcc_err_string(char *buf, int buf_len, u64 flags)
{
	return flag_string(buf, buf_len, flags, dcc_err_flags,
		ARRAY_SIZE(dcc_err_flags));
}

static char *lcb_err_string(char *buf, int buf_len, u64 flags)
{
	return flag_string(buf, buf_len, flags, lcb_err_flags,
		ARRAY_SIZE(lcb_err_flags));
}

static char *dc8051_err_string(char *buf, int buf_len, u64 flags)
{
	return flag_string(buf, buf_len, flags, dc8051_err_flags,
		ARRAY_SIZE(dc8051_err_flags));
}

static char *dc8051_info_err_string(char *buf, int buf_len, u64 flags)
{
	return flag_string(buf, buf_len, flags, dc8051_info_err_flags,
		ARRAY_SIZE(dc8051_info_err_flags));
}

static char *dc8051_info_host_msg_string(char *buf, int buf_len, u64 flags)
{
	return flag_string(buf, buf_len, flags, dc8051_info_host_msg_flags,
		ARRAY_SIZE(dc8051_info_host_msg_flags));
}

static void handle_8051_interrupt(struct hfi1_devdata *dd, u32 unused, u64 reg)
{
	struct hfi1_pportdata *ppd = dd->pport;
	u64 info, err, host_msg;
	int queue_link_down = 0;
	char buf[96];

	/* look at the flags */
	if (reg & DC_DC8051_ERR_FLG_SET_BY_8051_SMASK) {
		/* 8051 information set by firmware */
		/* read DC8051_DBG_ERR_INFO_SET_BY_8051 for details */
		info = read_csr(dd, DC_DC8051_DBG_ERR_INFO_SET_BY_8051);
		err = (info >> DC_DC8051_DBG_ERR_INFO_SET_BY_8051_ERROR_SHIFT)
			& DC_DC8051_DBG_ERR_INFO_SET_BY_8051_ERROR_MASK;
		host_msg = (info >>
			DC_DC8051_DBG_ERR_INFO_SET_BY_8051_HOST_MSG_SHIFT)
			& DC_DC8051_DBG_ERR_INFO_SET_BY_8051_HOST_MSG_MASK;

		/*
		 * Handle error flags.
		 */
		if (err & FAILED_LNI) {
			/*
			 * LNI error indications are cleared by the 8051
			 * only when starting polling.  Only pay attention
			 * to them when in the states that occur during
			 * LNI.
			 */
			if (ppd->host_link_state
			    & (HLS_DN_POLL | HLS_VERIFY_CAP | HLS_GOING_UP)) {
				queue_link_down = 1;
				dd_dev_info(dd, "Link error: %s\n",
					    dc8051_info_err_string(buf,
								   sizeof(buf),
								   err &
								   FAILED_LNI));
			}
			err &= ~(u64)FAILED_LNI;
		}
		/* unknown frames can happen durning LNI, just count */
		if (err & UNKNOWN_FRAME) {
			ppd->unknown_frame_count++;
			err &= ~(u64)UNKNOWN_FRAME;
		}
		if (err) {
			/* report remaining errors, but do not do anything */
			dd_dev_err(dd, "8051 info error: %s\n",
				   dc8051_info_err_string(buf, sizeof(buf),
							  err));
		}

		/*
		 * Handle host message flags.
		 */
		if (host_msg & HOST_REQ_DONE) {
			/*
			 * Presently, the driver does a busy wait for
			 * host requests to complete.  This is only an
			 * informational message.
			 * NOTE: The 8051 clears the host message
			 * information *on the next 8051 command*.
			 * Therefore, when linkup is achieved,
			 * this flag will still be set.
			 */
			host_msg &= ~(u64)HOST_REQ_DONE;
		}
		if (host_msg & BC_SMA_MSG) {
			queue_work(ppd->link_wq, &ppd->sma_message_work);
			host_msg &= ~(u64)BC_SMA_MSG;
		}
		if (host_msg & LINKUP_ACHIEVED) {
			dd_dev_info(dd, "8051: Link up\n");
			queue_work(ppd->link_wq, &ppd->link_up_work);
			host_msg &= ~(u64)LINKUP_ACHIEVED;
		}
		if (host_msg & EXT_DEVICE_CFG_REQ) {
			handle_8051_request(ppd);
			host_msg &= ~(u64)EXT_DEVICE_CFG_REQ;
		}
		if (host_msg & VERIFY_CAP_FRAME) {
			queue_work(ppd->link_wq, &ppd->link_vc_work);
			host_msg &= ~(u64)VERIFY_CAP_FRAME;
		}
		if (host_msg & LINK_GOING_DOWN) {
			const char *extra = "";
			/* no downgrade action needed if going down */
			if (host_msg & LINK_WIDTH_DOWNGRADED) {
				host_msg &= ~(u64)LINK_WIDTH_DOWNGRADED;
				extra = " (ignoring downgrade)";
			}
			dd_dev_info(dd, "8051: Link down%s\n", extra);
			queue_link_down = 1;
			host_msg &= ~(u64)LINK_GOING_DOWN;
		}
		if (host_msg & LINK_WIDTH_DOWNGRADED) {
			queue_work(ppd->link_wq, &ppd->link_downgrade_work);
			host_msg &= ~(u64)LINK_WIDTH_DOWNGRADED;
		}
		if (host_msg) {
			/* report remaining messages, but do not do anything */
			dd_dev_info(dd, "8051 info host message: %s\n",
				    dc8051_info_host_msg_string(buf,
								sizeof(buf),
								host_msg));
		}

		reg &= ~DC_DC8051_ERR_FLG_SET_BY_8051_SMASK;
	}
	if (reg & DC_DC8051_ERR_FLG_LOST_8051_HEART_BEAT_SMASK) {
		/*
		 * Lost the 8051 heartbeat.  If this happens, we
		 * receive constant interrupts about it.  Disable
		 * the interrupt after the first.
		 */
		dd_dev_err(dd, "Lost 8051 heartbeat\n");
		write_csr(dd, DC_DC8051_ERR_EN,
			  read_csr(dd, DC_DC8051_ERR_EN) &
			  ~DC_DC8051_ERR_EN_LOST_8051_HEART_BEAT_SMASK);

		reg &= ~DC_DC8051_ERR_FLG_LOST_8051_HEART_BEAT_SMASK;
	}
	if (reg) {
		/* report the error, but do not do anything */
		dd_dev_err(dd, "8051 error: %s\n",
			   dc8051_err_string(buf, sizeof(buf), reg));
	}

	if (queue_link_down) {
		/*
		 * if the link is already going down or disabled, do not
		 * queue another. If there's a link down entry already
		 * queued, don't queue another one.
		 */
		if ((ppd->host_link_state &
		    (HLS_GOING_OFFLINE | HLS_LINK_COOLDOWN)) ||
		    ppd->link_enabled == 0) {
			dd_dev_info(dd, "%s: not queuing link down. host_link_state %x, link_enabled %x\n",
				    __func__, ppd->host_link_state,
				    ppd->link_enabled);
		} else {
			if (xchg(&ppd->is_link_down_queued, 1) == 1)
				dd_dev_info(dd,
					    "%s: link down request already queued\n",
					    __func__);
			else
				queue_work(ppd->link_wq, &ppd->link_down_work);
		}
	}
}

static const char * const fm_config_txt[] = {
[0] =
	"BadHeadDist: Distance violation between two head flits",
[1] =
	"BadTailDist: Distance violation between two tail flits",
[2] =
	"BadCtrlDist: Distance violation between two credit control flits",
[3] =
	"BadCrdAck: Credits return for unsupported VL",
[4] =
	"UnsupportedVLMarker: Received VL Marker",
[5] =
	"BadPreempt: Exceeded the preemption nesting level",
[6] =
	"BadControlFlit: Received unsupported control flit",
/* no 7 */
[8] =
	"UnsupportedVLMarker: Received VL Marker for unconfigured or disabled VL",
};

static const char * const port_rcv_txt[] = {
[1] =
	"BadPktLen: Illegal PktLen",
[2] =
	"PktLenTooLong: Packet longer than PktLen",
[3] =
	"PktLenTooShort: Packet shorter than PktLen",
[4] =
	"BadSLID: Illegal SLID (0, using multicast as SLID, does not include security validation of SLID)",
[5] =
	"BadDLID: Illegal DLID (0, doesn't match HFI)",
[6] =
	"BadL2: Illegal L2 opcode",
[7] =
	"BadSC: Unsupported SC",
[9] =
	"BadRC: Illegal RC",
[11] =
	"PreemptError: Preempting with same VL",
[12] =
	"PreemptVL15: Preempting a VL15 packet",
};

#define OPA_LDR_FMCONFIG_OFFSET 16
#define OPA_LDR_PORTRCV_OFFSET 0
static void handle_dcc_err(struct hfi1_devdata *dd, u32 unused, u64 reg)
{
	u64 info, hdr0, hdr1;
	const char *extra;
	char buf[96];
	struct hfi1_pportdata *ppd = dd->pport;
	u8 lcl_reason = 0;
	int do_bounce = 0;

	if (reg & DCC_ERR_FLG_UNCORRECTABLE_ERR_SMASK) {
		if (!(dd->err_info_uncorrectable & OPA_EI_STATUS_SMASK)) {
			info = read_csr(dd, DCC_ERR_INFO_UNCORRECTABLE);
			dd->err_info_uncorrectable = info & OPA_EI_CODE_SMASK;
			/* set status bit */
			dd->err_info_uncorrectable |= OPA_EI_STATUS_SMASK;
		}
		reg &= ~DCC_ERR_FLG_UNCORRECTABLE_ERR_SMASK;
	}

	if (reg & DCC_ERR_FLG_LINK_ERR_SMASK) {
		struct hfi1_pportdata *ppd = dd->pport;
		/* this counter saturates at (2^32) - 1 */
		if (ppd->link_downed < (u32)UINT_MAX)
			ppd->link_downed++;
		reg &= ~DCC_ERR_FLG_LINK_ERR_SMASK;
	}

	if (reg & DCC_ERR_FLG_FMCONFIG_ERR_SMASK) {
		u8 reason_valid = 1;

		info = read_csr(dd, DCC_ERR_INFO_FMCONFIG);
		if (!(dd->err_info_fmconfig & OPA_EI_STATUS_SMASK)) {
			dd->err_info_fmconfig = info & OPA_EI_CODE_SMASK;
			/* set status bit */
			dd->err_info_fmconfig |= OPA_EI_STATUS_SMASK;
		}
		switch (info) {
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
			extra = fm_config_txt[info];
			break;
		case 8:
			extra = fm_config_txt[info];
			if (ppd->port_error_action &
			    OPA_PI_MASK_FM_CFG_UNSUPPORTED_VL_MARKER) {
				do_bounce = 1;
				/*
				 * lcl_reason cannot be derived from info
				 * for this error
				 */
				lcl_reason =
				  OPA_LINKDOWN_REASON_UNSUPPORTED_VL_MARKER;
			}
			break;
		default:
			reason_valid = 0;
			snprintf(buf, sizeof(buf), "reserved%lld", info);
			extra = buf;
			break;
		}

		if (reason_valid && !do_bounce) {
			do_bounce = ppd->port_error_action &
					(1 << (OPA_LDR_FMCONFIG_OFFSET + info));
			lcl_reason = info + OPA_LINKDOWN_REASON_BAD_HEAD_DIST;
		}

		/* just report this */
		dd_dev_info_ratelimited(dd, "DCC Error: fmconfig error: %s\n",
					extra);
		reg &= ~DCC_ERR_FLG_FMCONFIG_ERR_SMASK;
	}

	if (reg & DCC_ERR_FLG_RCVPORT_ERR_SMASK) {
		u8 reason_valid = 1;

		info = read_csr(dd, DCC_ERR_INFO_PORTRCV);
		hdr0 = read_csr(dd, DCC_ERR_INFO_PORTRCV_HDR0);
		hdr1 = read_csr(dd, DCC_ERR_INFO_PORTRCV_HDR1);
		if (!(dd->err_info_rcvport.status_and_code &
		      OPA_EI_STATUS_SMASK)) {
			dd->err_info_rcvport.status_and_code =
				info & OPA_EI_CODE_SMASK;
			/* set status bit */
			dd->err_info_rcvport.status_and_code |=
				OPA_EI_STATUS_SMASK;
			/*
			 * save first 2 flits in the packet that caused
			 * the error
			 */
			dd->err_info_rcvport.packet_flit1 = hdr0;
			dd->err_info_rcvport.packet_flit2 = hdr1;
		}
		switch (info) {
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 9:
		case 11:
		case 12:
			extra = port_rcv_txt[info];
			break;
		default:
			reason_valid = 0;
			snprintf(buf, sizeof(buf), "reserved%lld", info);
			extra = buf;
			break;
		}

		if (reason_valid && !do_bounce) {
			do_bounce = ppd->port_error_action &
					(1 << (OPA_LDR_PORTRCV_OFFSET + info));
			lcl_reason = info + OPA_LINKDOWN_REASON_RCV_ERROR_0;
		}

		/* just report this */
		dd_dev_info_ratelimited(dd, "DCC Error: PortRcv error: %s\n"
					"               hdr0 0x%llx, hdr1 0x%llx\n",
					extra, hdr0, hdr1);

		reg &= ~DCC_ERR_FLG_RCVPORT_ERR_SMASK;
	}

	if (reg & DCC_ERR_FLG_EN_CSR_ACCESS_BLOCKED_UC_SMASK) {
		/* informative only */
		dd_dev_info_ratelimited(dd, "8051 access to LCB blocked\n");
		reg &= ~DCC_ERR_FLG_EN_CSR_ACCESS_BLOCKED_UC_SMASK;
	}
	if (reg & DCC_ERR_FLG_EN_CSR_ACCESS_BLOCKED_HOST_SMASK) {
		/* informative only */
		dd_dev_info_ratelimited(dd, "host access to LCB blocked\n");
		reg &= ~DCC_ERR_FLG_EN_CSR_ACCESS_BLOCKED_HOST_SMASK;
	}

	if (unlikely(hfi1_dbg_fault_suppress_err(&dd->verbs_dev)))
		reg &= ~DCC_ERR_FLG_LATE_EBP_ERR_SMASK;

	/* report any remaining errors */
	if (reg)
		dd_dev_info_ratelimited(dd, "DCC Error: %s\n",
					dcc_err_string(buf, sizeof(buf), reg));

	if (lcl_reason == 0)
		lcl_reason = OPA_LINKDOWN_REASON_UNKNOWN;

	if (do_bounce) {
		dd_dev_info_ratelimited(dd, "%s: PortErrorAction bounce\n",
					__func__);
		set_link_down_reason(ppd, lcl_reason, 0, lcl_reason);
		queue_work(ppd->link_wq, &ppd->link_bounce_work);
	}
}

static void handle_lcb_err(struct hfi1_devdata *dd, u32 unused, u64 reg)
{
	char buf[96];

	dd_dev_info(dd, "LCB Error: %s\n",
		    lcb_err_string(buf, sizeof(buf), reg));
}

/*
 * CCE block DC interrupt.  Source is < 8.
 */
static void is_dc_int(struct hfi1_devdata *dd, unsigned int source)
{
	const struct err_reg_info *eri = &dc_errs[source];

	if (eri->handler) {
		interrupt_clear_down(dd, 0, eri);
	} else if (source == 3 /* dc_lbm_int */) {
		/*
		 * This indicates that a parity error has occurred on the
		 * address/control lines presented to the LBM.  The error
		 * is a single pulse, there is no associated error flag,
		 * and it is non-maskable.  This is because if a parity
		 * error occurs on the request the request is dropped.
		 * This should never occur, but it is nice to know if it
		 * ever does.
		 */
		dd_dev_err(dd, "Parity error in DC LBM block\n");
	} else {
		dd_dev_err(dd, "Invalid DC interrupt %u\n", source);
	}
}

/*
 * TX block send credit interrupt.  Source is < 160.
 */
static void is_send_credit_int(struct hfi1_devdata *dd, unsigned int source)
{
	sc_group_release_update(dd, source);
}

/*
 * TX block SDMA interrupt.  Source is < 48.
 *
 * SDMA interrupts are grouped by type:
 *
 *	 0 -  N-1 = SDma
 *	 N - 2N-1 = SDmaProgress
 *	2N - 3N-1 = SDmaIdle
 */
static void is_sdma_eng_int(struct hfi1_devdata *dd, unsigned int source)
{
	/* what interrupt */
	unsigned int what  = source / TXE_NUM_SDMA_ENGINES;
	/* which engine */
	unsigned int which = source % TXE_NUM_SDMA_ENGINES;

#ifdef CONFIG_SDMA_VERBOSITY
	dd_dev_err(dd, "CONFIG SDMA(%u) %s:%d %s()\n", which,
		   slashstrip(__FILE__), __LINE__, __func__);
	sdma_dumpstate(&dd->per_sdma[which]);
#endif

	if (likely(what < 3 && which < dd->num_sdma)) {
		sdma_engine_interrupt(&dd->per_sdma[which], 1ull << source);
	} else {
		/* should not happen */
		dd_dev_err(dd, "Invalid SDMA interrupt 0x%x\n", source);
	}
}

/**
 * is_rcv_avail_int() - User receive context available IRQ handler
 * @dd: valid dd
 * @source: logical IRQ source (offset from IS_RCVAVAIL_START)
 *
 * RX block receive available interrupt.  Source is < 160.
 *
 * This is the general interrupt handler for user (PSM) receive contexts,
 * and can only be used for non-threaded IRQs.
 */
static void is_rcv_avail_int(struct hfi1_devdata *dd, unsigned int source)
{
	struct hfi1_ctxtdata *rcd;
	char *err_detail;

	if (likely(source < dd->num_rcv_contexts)) {
		rcd = hfi1_rcd_get_by_index(dd, source);
		if (rcd) {
			handle_user_interrupt(rcd);
			hfi1_rcd_put(rcd);
			return;	/* OK */
		}
		/* received an interrupt, but no rcd */
		err_detail = "dataless";
	} else {
		/* received an interrupt, but are not using that context */
		err_detail = "out of range";
	}
	dd_dev_err(dd, "unexpected %s receive available context interrupt %u\n",
		   err_detail, source);
}

/**
 * is_rcv_urgent_int() - User receive context urgent IRQ handler
 * @dd: valid dd
 * @source: logical IRQ source (offset from IS_RCVURGENT_START)
 *
 * RX block receive urgent interrupt.  Source is < 160.
 *
 * NOTE: kernel receive contexts specifically do NOT enable this IRQ.
 */
static void is_rcv_urgent_int(struct hfi1_devdata *dd, unsigned int source)
{
	struct hfi1_ctxtdata *rcd;
	char *err_detail;

	if (likely(source < dd->num_rcv_contexts)) {
		rcd = hfi1_rcd_get_by_index(dd, source);
		if (rcd) {
			handle_user_interrupt(rcd);
			hfi1_rcd_put(rcd);
			return;	/* OK */
		}
		/* received an interrupt, but no rcd */
		err_detail = "dataless";
	} else {
		/* received an interrupt, but are not using that context */
		err_detail = "out of range";
	}
	dd_dev_err(dd, "unexpected %s receive urgent context interrupt %u\n",
		   err_detail, source);
}

/*
 * Reserved range interrupt.  Should not be called in normal operation.
 */
static void is_reserved_int(struct hfi1_devdata *dd, unsigned int source)
{
	char name[64];

	dd_dev_err(dd, "unexpected %s interrupt\n",
		   is_reserved_name(name, sizeof(name), source));
}

static const struct is_table is_table[] = {
/*
 * start		 end
 *				name func		interrupt func
 */
{ IS_GENERAL_ERR_START,  IS_GENERAL_ERR_END,
				is_misc_err_name,	is_misc_err_int },
{ IS_SDMAENG_ERR_START,  IS_SDMAENG_ERR_END,
				is_sdma_eng_err_name,	is_sdma_eng_err_int },
{ IS_SENDCTXT_ERR_START, IS_SENDCTXT_ERR_END,
				is_sendctxt_err_name,	is_sendctxt_err_int },
{ IS_SDMA_START,	     IS_SDMA_IDLE_END,
				is_sdma_eng_name,	is_sdma_eng_int },
{ IS_VARIOUS_START,	     IS_VARIOUS_END,
				is_various_name,	is_various_int },
{ IS_DC_START,	     IS_DC_END,
				is_dc_name,		is_dc_int },
{ IS_RCVAVAIL_START,     IS_RCVAVAIL_END,
				is_rcv_avail_name,	is_rcv_avail_int },
{ IS_RCVURGENT_START,    IS_RCVURGENT_END,
				is_rcv_urgent_name,	is_rcv_urgent_int },
{ IS_SENDCREDIT_START,   IS_SENDCREDIT_END,
				is_send_credit_name,	is_send_credit_int},
{ IS_RESERVED_START,     IS_RESERVED_END,
				is_reserved_name,	is_reserved_int},
};

/*
 * Interrupt source interrupt - called when the given source has an interrupt.
 * Source is a bit index into an array of 64-bit integers.
 */
static void is_interrupt(struct hfi1_devdata *dd, unsigned int source)
{
	const struct is_table *entry;

	/* avoids a double compare by walking the table in-order */
	for (entry = &is_table[0]; entry->is_name; entry++) {
		if (source <= entry->end) {
			trace_hfi1_interrupt(dd, entry, source);
			entry->is_int(dd, source - entry->start);
			return;
		}
	}
	/* fell off the end */
	dd_dev_err(dd, "invalid interrupt source %u\n", source);
}

/**
 * gerneral_interrupt() -  General interrupt handler
 * @irq: MSIx IRQ vector
 * @data: hfi1 devdata
 *
 * This is able to correctly handle all non-threaded interrupts.  Receive
 * context DATA IRQs are threaded and are not supported by this handler.
 *
 */
irqreturn_t general_interrupt(int irq, void *data)
{
	struct hfi1_devdata *dd = data;
	u64 regs[CCE_NUM_INT_CSRS];
	u32 bit;
	int i;
	irqreturn_t handled = IRQ_NONE;

	this_cpu_inc(*dd->int_counter);

	/* phase 1: scan and clear all handled interrupts */
	for (i = 0; i < CCE_NUM_INT_CSRS; i++) {
		if (dd->gi_mask[i] == 0) {
			regs[i] = 0;	/* used later */
			continue;
		}
		regs[i] = read_csr(dd, CCE_INT_STATUS + (8 * i)) &
				dd->gi_mask[i];
		/* only clear if anything is set */
		if (regs[i])
			write_csr(dd, CCE_INT_CLEAR + (8 * i), regs[i]);
	}

	/* phase 2: call the appropriate handler */
	for_each_set_bit(bit, (unsigned long *)&regs[0],
			 CCE_NUM_INT_CSRS * 64) {
		is_interrupt(dd, bit);
		handled = IRQ_HANDLED;
	}

	return handled;
}

irqreturn_t sdma_interrupt(int irq, void *data)
{
	struct sdma_engine *sde = data;
	struct hfi1_devdata *dd = sde->dd;
	u64 status;

#ifdef CONFIG_SDMA_VERBOSITY
	dd_dev_err(dd, "CONFIG SDMA(%u) %s:%d %s()\n", sde->this_idx,
		   slashstrip(__FILE__), __LINE__, __func__);
	sdma_dumpstate(sde);
#endif

	this_cpu_inc(*dd->int_counter);

	/* This read_csr is really bad in the hot path */
	status = read_csr(dd,
			  CCE_INT_STATUS + (8 * (IS_SDMA_START / 64)))
			  & sde->imask;
	if (likely(status)) {
		/* clear the interrupt(s) */
		write_csr(dd,
			  CCE_INT_CLEAR + (8 * (IS_SDMA_START / 64)),
			  status);

		/* handle the interrupt(s) */
		sdma_engine_interrupt(sde, status);
	} else {
		dd_dev_info_ratelimited(dd, "SDMA engine %u interrupt, but no status bits set\n",
					sde->this_idx);
	}
	return IRQ_HANDLED;
}

/*
 * Clear the receive interrupt.  Use a read of the interrupt clear CSR
 * to insure that the write completed.  This does NOT guarantee that
 * queued DMA writes to memory from the chip are pushed.
 */
static inline void clear_recv_intr(struct hfi1_ctxtdata *rcd)
{
	struct hfi1_devdata *dd = rcd->dd;
	u32 addr = CCE_INT_CLEAR + (8 * rcd->ireg);

	write_csr(dd, addr, rcd->imask);
	/* force the above write on the chip and get a value back */
	(void)read_csr(dd, addr);
}

/* force the receive interrupt */
void force_recv_intr(struct hfi1_ctxtdata *rcd)
{
	write_csr(rcd->dd, CCE_INT_FORCE + (8 * rcd->ireg), rcd->imask);
}

/*
 * Return non-zero if a packet is present.
 *
 * This routine is called when rechecking for packets after the RcvAvail
 * interrupt has been cleared down.  First, do a quick check of memory for
 * a packet present.  If not found, use an expensive CSR read of the context
 * tail to determine the actual tail.  The CSR read is necessary because there
 * is no method to push pending DMAs to memory other than an interrupt and we
 * are trying to determine if we need to force an interrupt.
 */
static inline int check_packet_present(struct hfi1_ctxtdata *rcd)
{
	u32 tail;

	if (hfi1_packet_present(rcd))
		return 1;

	/* fall back to a CSR read, correct indpendent of DMA_RTAIL */
	tail = (u32)read_uctxt_csr(rcd->dd, rcd->ctxt, RCV_HDR_TAIL);
	return hfi1_rcd_head(rcd) != tail;
}

/**
 * Common code for receive contexts interrupt handlers.
 * Update traces, increment kernel IRQ counter and
 * setup ASPM when needed.
 */
static void receive_interrupt_common(struct hfi1_ctxtdata *rcd)
{
	struct hfi1_devdata *dd = rcd->dd;

	trace_hfi1_receive_interrupt(dd, rcd);
	this_cpu_inc(*dd->int_counter);
	aspm_ctx_disable(rcd);
}

/**
 * __hfi1_rcd_eoi_intr() - Make HW issue receive interrupt
 * when there are packets present in the queue. When calling
 * with interrupts enabled please use hfi1_rcd_eoi_intr.
 *
 * @rcd: valid receive context
 */
static void __hfi1_rcd_eoi_intr(struct hfi1_ctxtdata *rcd)
{
	clear_recv_intr(rcd);
	if (check_packet_present(rcd))
		force_recv_intr(rcd);
}

/**
 * hfi1_rcd_eoi_intr() - End of Interrupt processing action
 *
 * @rcd: Ptr to hfi1_ctxtdata of receive context
 *
 *  Hold IRQs so we can safely clear the interrupt and
 *  recheck for a packet that may have arrived after the previous
 *  check and the interrupt clear.  If a packet arrived, force another
 *  interrupt. This routine can be called at the end of receive packet
 *  processing in interrupt service routines, interrupt service thread
 *  and softirqs
 */
static void hfi1_rcd_eoi_intr(struct hfi1_ctxtdata *rcd)
{
	unsigned long flags;

	local_irq_save(flags);
	__hfi1_rcd_eoi_intr(rcd);
	local_irq_restore(flags);
}

/*
 * Receive packet IRQ handler.  This routine expects to be on its own IRQ.
 * This routine will try to handle packets immediately (latency), but if
 * it finds too many, it will invoke the thread handler (bandwitdh).  The
 * chip receive interrupt is *not* cleared down until this or the thread (if
 * invoked) is finished.  The intent is to avoid extra interrupts while we
 * are processing packets anyway.
 */
irqreturn_t receive_context_interrupt(int irq, void *data)
{
	struct hfi1_ctxtdata *rcd = data;
	int disposition;

	receive_interrupt_common(rcd);

	/* receive interrupt remains blocked while processing packets */
	disposition = rcd->do_interrupt(rcd, 0);

	/*
	 * Too many packets were seen while processing packets in this
	 * IRQ handler.  Invoke the handler thread.  The receive interrupt
	 * remains blocked.
	 */
	if (disposition == RCV_PKT_LIMIT)
		return IRQ_WAKE_THREAD;

	__hfi1_rcd_eoi_intr(rcd);
	return IRQ_HANDLED;
}

/*
 * Receive packet thread handler.  This expects to be invoked with the
 * receive interrupt still blocked.
 */
irqreturn_t receive_context_thread(int irq, void *data)
{
	struct hfi1_ctxtdata *rcd = data;

	/* receive interrupt is still blocked from the IRQ handler */
	(void)rcd->do_interrupt(rcd, 1);

	hfi1_rcd_eoi_intr(rcd);

	return IRQ_HANDLED;
}

/* ========================================================================= */

u32 read_physical_state(struct hfi1_devdata *dd)
{
	u64 reg;

	reg = read_csr(dd, DC_DC8051_STS_CUR_STATE);
	return (reg >> DC_DC8051_STS_CUR_STATE_PORT_SHIFT)
				& DC_DC8051_STS_CUR_STATE_PORT_MASK;
}

u32 read_logical_state(struct hfi1_devdata *dd)
{
	u64 reg;

	reg = read_csr(dd, DCC_CFG_PORT_CONFIG);
	return (reg >> DCC_CFG_PORT_CONFIG_LINK_STATE_SHIFT)
				& DCC_CFG_PORT_CONFIG_LINK_STATE_MASK;
}

static void set_logical_state(struct hfi1_devdata *dd, u32 chip_lstate)
{
	u64 reg;

	reg = read_csr(dd, DCC_CFG_PORT_CONFIG);
	/* clear current state, set new state */
	reg &= ~DCC_CFG_PORT_CONFIG_LINK_STATE_SMASK;
	reg |= (u64)chip_lstate << DCC_CFG_PORT_CONFIG_LINK_STATE_SHIFT;
	write_csr(dd, DCC_CFG_PORT_CONFIG, reg);
}

/*
 * Use the 8051 to read a LCB CSR.
 */
static int read_lcb_via_8051(struct hfi1_devdata *dd, u32 addr, u64 *data)
{
	u32 regno;
	int ret;

	if (dd->icode == ICODE_FUNCTIONAL_SIMULATOR) {
		if (acquire_lcb_access(dd, 0) == 0) {
			*data = read_csr(dd, addr);
			release_lcb_access(dd, 0);
			return 0;
		}
		return -EBUSY;
	}

	/* register is an index of LCB registers: (offset - base) / 8 */
	regno = (addr - DC_LCB_CFG_RUN) >> 3;
	ret = do_8051_command(dd, HCMD_READ_LCB_CSR, regno, data);
	if (ret != HCMD_SUCCESS)
		return -EBUSY;
	return 0;
}

/*
 * Provide a cache for some of the LCB registers in case the LCB is
 * unavailable.
 * (The LCB is unavailable in certain link states, for example.)
 */
struct lcb_datum {
	u32 off;
	u64 val;
};

static struct lcb_datum lcb_cache[] = {
	{ DC_LCB_ERR_INFO_RX_REPLAY_CNT, 0},
	{ DC_LCB_ERR_INFO_SEQ_CRC_CNT, 0 },
	{ DC_LCB_ERR_INFO_REINIT_FROM_PEER_CNT, 0 },
};

static void update_lcb_cache(struct hfi1_devdata *dd)
{
	int i;
	int ret;
	u64 val;

	for (i = 0; i < ARRAY_SIZE(lcb_cache); i++) {
		ret = read_lcb_csr(dd, lcb_cache[i].off, &val);

		/* Update if we get good data */
		if (likely(ret != -EBUSY))
			lcb_cache[i].val = val;
	}
}

static int read_lcb_cache(u32 off, u64 *val)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(lcb_cache); i++) {
		if (lcb_cache[i].off == off) {
			*val = lcb_cache[i].val;
			return 0;
		}
	}

	pr_warn("%s bad offset 0x%x\n", __func__, off);
	return -1;
}

/*
 * Read an LCB CSR.  Access may not be in host control, so check.
 * Return 0 on success, -EBUSY on failure.
 */
int read_lcb_csr(struct hfi1_devdata *dd, u32 addr, u64 *data)
{
	struct hfi1_pportdata *ppd = dd->pport;

	/* if up, go through the 8051 for the value */
	if (ppd->host_link_state & HLS_UP)
		return read_lcb_via_8051(dd, addr, data);
	/* if going up or down, check the cache, otherwise, no access */
	if (ppd->host_link_state & (HLS_GOING_UP | HLS_GOING_OFFLINE)) {
		if (read_lcb_cache(addr, data))
			return -EBUSY;
		return 0;
	}

	/* otherwise, host has access */
	*data = read_csr(dd, addr);
	return 0;
}

/*
 * Use the 8051 to write a LCB CSR.
 */
static int write_lcb_via_8051(struct hfi1_devdata *dd, u32 addr, u64 data)
{
	u32 regno;
	int ret;

	if (dd->icode == ICODE_FUNCTIONAL_SIMULATOR ||
	    (dd->dc8051_ver < dc8051_ver(0, 20, 0))) {
		if (acquire_lcb_access(dd, 0) == 0) {
			write_csr(dd, addr, data);
			release_lcb_access(dd, 0);
			return 0;
		}
		return -EBUSY;
	}

	/* register is an index of LCB registers: (offset - base) / 8 */
	regno = (addr - DC_LCB_CFG_RUN) >> 3;
	ret = do_8051_command(dd, HCMD_WRITE_LCB_CSR, regno, &data);
	if (ret != HCMD_SUCCESS)
		return -EBUSY;
	return 0;
}

/*
 * Write an LCB CSR.  Access may not be in host control, so check.
 * Return 0 on success, -EBUSY on failure.
 */
int write_lcb_csr(struct hfi1_devdata *dd, u32 addr, u64 data)
{
	struct hfi1_pportdata *ppd = dd->pport;

	/* if up, go through the 8051 for the value */
	if (ppd->host_link_state & HLS_UP)
		return write_lcb_via_8051(dd, addr, data);
	/* if going up or down, no access */
	if (ppd->host_link_state & (HLS_GOING_UP | HLS_GOING_OFFLINE))
		return -EBUSY;
	/* otherwise, host has access */
	write_csr(dd, addr, data);
	return 0;
}

/*
 * Returns:
 *	< 0 = Linux error, not able to get access
 *	> 0 = 8051 command RETURN_CODE
 */
static int do_8051_command(struct hfi1_devdata *dd, u32 type, u64 in_data,
			   u64 *out_data)
{
	u64 reg, completed;
	int return_code;
	unsigned long timeout;

	hfi1_cdbg(DC8051, "type %d, data 0x%012llx", type, in_data);

	mutex_lock(&dd->dc8051_lock);

	/* We can't send any commands to the 8051 if it's in reset */
	if (dd->dc_shutdown) {
		return_code = -ENODEV;
		goto fail;
	}

	/*
	 * If an 8051 host command timed out previously, then the 8051 is
	 * stuck.
	 *
	 * On first timeout, attempt to reset and restart the entire DC
	 * block (including 8051). (Is this too big of a hammer?)
	 *
	 * If the 8051 times out a second time, the reset did not bring it
	 * back to healthy life. In that case, fail any subsequent commands.
	 */
	if (dd->dc8051_timed_out) {
		if (dd->dc8051_timed_out > 1) {
			dd_dev_err(dd,
				   "Previous 8051 host command timed out, skipping command %u\n",
				   type);
			return_code = -ENXIO;
			goto fail;
		}
		_dc_shutdown(dd);
		_dc_start(dd);
	}

	/*
	 * If there is no timeout, then the 8051 command interface is
	 * waiting for a command.
	 */

	/*
	 * When writing a LCB CSR, out_data contains the full value to
	 * to be written, while in_data contains the relative LCB
	 * address in 7:0.  Do the work here, rather than the caller,
	 * of distrubting the write data to where it needs to go:
	 *
	 * Write data
	 *   39:00 -> in_data[47:8]
	 *   47:40 -> DC8051_CFG_EXT_DEV_0.RETURN_CODE
	 *   63:48 -> DC8051_CFG_EXT_DEV_0.RSP_DATA
	 */
	if (type == HCMD_WRITE_LCB_CSR) {
		in_data |= ((*out_data) & 0xffffffffffull) << 8;
		/* must preserve COMPLETED - it is tied to hardware */
		reg = read_csr(dd, DC_DC8051_CFG_EXT_DEV_0);
		reg &= DC_DC8051_CFG_EXT_DEV_0_COMPLETED_SMASK;
		reg |= ((((*out_data) >> 40) & 0xff) <<
				DC_DC8051_CFG_EXT_DEV_0_RETURN_CODE_SHIFT)
		      | ((((*out_data) >> 48) & 0xffff) <<
				DC_DC8051_CFG_EXT_DEV_0_RSP_DATA_SHIFT);
		write_csr(dd, DC_DC8051_CFG_EXT_DEV_0, reg);
	}

	/*
	 * Do two writes: the first to stabilize the type and req_data, the
	 * second to activate.
	 */
	reg = ((u64)type & DC_DC8051_CFG_HOST_CMD_0_REQ_TYPE_MASK)
			<< DC_DC8051_CFG_HOST_CMD_0_REQ_TYPE_SHIFT
		| (in_data & DC_DC8051_CFG_HOST_CMD_0_REQ_DATA_MASK)
			<< DC_DC8051_CFG_HOST_CMD_0_REQ_DATA_SHIFT;
	write_csr(dd, DC_DC8051_CFG_HOST_CMD_0, reg);
	reg |= DC_DC8051_CFG_HOST_CMD_0_REQ_NEW_SMASK;
	write_csr(dd, DC_DC8051_CFG_HOST_CMD_0, reg);

	/* wait for completion, alternate: interrupt */
	timeout = jiffies + msecs_to_jiffies(DC8051_COMMAND_TIMEOUT);
	while (1) {
		reg = read_csr(dd, DC_DC8051_CFG_HOST_CMD_1);
		completed = reg & DC_DC8051_CFG_HOST_CMD_1_COMPLETED_SMASK;
		if (completed)
			break;
		if (time_after(jiffies, timeout)) {
			dd->dc8051_timed_out++;
			dd_dev_err(dd, "8051 host command %u timeout\n", type);
			if (out_data)
				*out_data = 0;
			return_code = -ETIMEDOUT;
			goto fail;
		}
		udelay(2);
	}

	if (out_data) {
		*out_data = (reg >> DC_DC8051_CFG_HOST_CMD_1_RSP_DATA_SHIFT)
				& DC_DC8051_CFG_HOST_CMD_1_RSP_DATA_MASK;
		if (type == HCMD_READ_LCB_CSR) {
			/* top 16 bits are in a different register */
			*out_data |= (read_csr(dd, DC_DC8051_CFG_EXT_DEV_1)
				& DC_DC8051_CFG_EXT_DEV_1_REQ_DATA_SMASK)
				<< (48
				    - DC_DC8051_CFG_EXT_DEV_1_REQ_DATA_SHIFT);
		}
	}
	return_code = (reg >> DC_DC8051_CFG_HOST_CMD_1_RETURN_CODE_SHIFT)
				& DC_DC8051_CFG_HOST_CMD_1_RETURN_CODE_MASK;
	dd->dc8051_timed_out = 0;
	/*
	 * Clear command for next user.
	 */
	write_csr(dd, DC_DC8051_CFG_HOST_CMD_0, 0);

fail:
	mutex_unlock(&dd->dc8051_lock);
	return return_code;
}

static int set_physical_link_state(struct hfi1_devdata *dd, u64 state)
{
	return do_8051_command(dd, HCMD_CHANGE_PHY_STATE, state, NULL);
}

int load_8051_config(struct hfi1_devdata *dd, u8 field_id,
		     u8 lane_id, u32 config_data)
{
	u64 data;
	int ret;

	data = (u64)field_id << LOAD_DATA_FIELD_ID_SHIFT
		| (u64)lane_id << LOAD_DATA_LANE_ID_SHIFT
		| (u64)config_data << LOAD_DATA_DATA_SHIFT;
	ret = do_8051_command(dd, HCMD_LOAD_CONFIG_DATA, data, NULL);
	if (ret != HCMD_SUCCESS) {
		dd_dev_err(dd,
			   "load 8051 config: field id %d, lane %d, err %d\n",
			   (int)field_id, (int)lane_id, ret);
	}
	return ret;
}

/*
 * Read the 8051 firmware "registers".  Use the RAM directly.  Always
 * set the result, even on error.
 * Return 0 on success, -errno on failure
 */
int read_8051_config(struct hfi1_devdata *dd, u8 field_id, u8 lane_id,
		     u32 *result)
{
	u64 big_data;
	u32 addr;
	int ret;

	/* address start depends on the lane_id */
	if (lane_id < 4)
		addr = (4 * NUM_GENERAL_FIELDS)
			+ (lane_id * 4 * NUM_LANE_FIELDS);
	else
		addr = 0;
	addr += field_id * 4;

	/* read is in 8-byte chunks, hardware will truncate the address down */
	ret = read_8051_data(dd, addr, 8, &big_data);

	if (ret == 0) {
		/* extract the 4 bytes we want */
		if (addr & 0x4)
			*result = (u32)(big_data >> 32);
		else
			*result = (u32)big_data;
	} else {
		*result = 0;
		dd_dev_err(dd, "%s: direct read failed, lane %d, field %d!\n",
			   __func__, lane_id, field_id);
	}

	return ret;
}

static int write_vc_local_phy(struct hfi1_devdata *dd, u8 power_management,
			      u8 continuous)
{
	u32 frame;

	frame = continuous << CONTINIOUS_REMOTE_UPDATE_SUPPORT_SHIFT
		| power_management << POWER_MANAGEMENT_SHIFT;
	return load_8051_config(dd, VERIFY_CAP_LOCAL_PHY,
				GENERAL_CONFIG, frame);
}

static int write_vc_local_fabric(struct hfi1_devdata *dd, u8 vau, u8 z, u8 vcu,
				 u16 vl15buf, u8 crc_sizes)
{
	u32 frame;

	frame = (u32)vau << VAU_SHIFT
		| (u32)z << Z_SHIFT
		| (u32)vcu << VCU_SHIFT
		| (u32)vl15buf << VL15BUF_SHIFT
		| (u32)crc_sizes << CRC_SIZES_SHIFT;
	return load_8051_config(dd, VERIFY_CAP_LOCAL_FABRIC,
				GENERAL_CONFIG, frame);
}

static void read_vc_local_link_mode(struct hfi1_devdata *dd, u8 *misc_bits,
				    u8 *flag_bits, u16 *link_widths)
{
	u32 frame;

	read_8051_config(dd, VERIFY_CAP_LOCAL_LINK_MODE, GENERAL_CONFIG,
			 &frame);
	*misc_bits = (frame >> MISC_CONFIG_BITS_SHIFT) & MISC_CONFIG_BITS_MASK;
	*flag_bits = (frame >> LOCAL_FLAG_BITS_SHIFT) & LOCAL_FLAG_BITS_MASK;
	*link_widths = (frame >> LINK_WIDTH_SHIFT) & LINK_WIDTH_MASK;
}

static int write_vc_local_link_mode(struct hfi1_devdata *dd,
				    u8 misc_bits,
				    u8 flag_bits,
				    u16 link_widths)
{
	u32 frame;

	frame = (u32)misc_bits << MISC_CONFIG_BITS_SHIFT
		| (u32)flag_bits << LOCAL_FLAG_BITS_SHIFT
		| (u32)link_widths << LINK_WIDTH_SHIFT;
	return load_8051_config(dd, VERIFY_CAP_LOCAL_LINK_MODE, GENERAL_CONFIG,
		     frame);
}

static int write_local_device_id(struct hfi1_devdata *dd, u16 device_id,
				 u8 device_rev)
{
	u32 frame;

	frame = ((u32)device_id << LOCAL_DEVICE_ID_SHIFT)
		| ((u32)device_rev << LOCAL_DEVICE_REV_SHIFT);
	return load_8051_config(dd, LOCAL_DEVICE_ID, GENERAL_CONFIG, frame);
}

static void read_remote_device_id(struct hfi1_devdata *dd, u16 *device_id,
				  u8 *device_rev)
{
	u32 frame;

	read_8051_config(dd, REMOTE_DEVICE_ID, GENERAL_CONFIG, &frame);
	*device_id = (frame >> REMOTE_DEVICE_ID_SHIFT) & REMOTE_DEVICE_ID_MASK;
	*device_rev = (frame >> REMOTE_DEVICE_REV_SHIFT)
			& REMOTE_DEVICE_REV_MASK;
}

int write_host_interface_version(struct hfi1_devdata *dd, u8 version)
{
	u32 frame;
	u32 mask;

	mask = (HOST_INTERFACE_VERSION_MASK << HOST_INTERFACE_VERSION_SHIFT);
	read_8051_config(dd, RESERVED_REGISTERS, GENERAL_CONFIG, &frame);
	/* Clear, then set field */
	frame &= ~mask;
	frame |= ((u32)version << HOST_INTERFACE_VERSION_SHIFT);
	return load_8051_config(dd, RESERVED_REGISTERS, GENERAL_CONFIG,
				frame);
}

void read_misc_status(struct hfi1_devdata *dd, u8 *ver_major, u8 *ver_minor,
		      u8 *ver_patch)
{
	u32 frame;

	read_8051_config(dd, MISC_STATUS, GENERAL_CONFIG, &frame);
	*ver_major = (frame >> STS_FM_VERSION_MAJOR_SHIFT) &
		STS_FM_VERSION_MAJOR_MASK;
	*ver_minor = (frame >> STS_FM_VERSION_MINOR_SHIFT) &
		STS_FM_VERSION_MINOR_MASK;

	read_8051_config(dd, VERSION_PATCH, GENERAL_CONFIG, &frame);
	*ver_patch = (frame >> STS_FM_VERSION_PATCH_SHIFT) &
		STS_FM_VERSION_PATCH_MASK;
}

static void read_vc_remote_phy(struct hfi1_devdata *dd, u8 *power_management,
			       u8 *continuous)
{
	u32 frame;

	read_8051_config(dd, VERIFY_CAP_REMOTE_PHY, GENERAL_CONFIG, &frame);
	*power_management = (frame >> POWER_MANAGEMENT_SHIFT)
					& POWER_MANAGEMENT_MASK;
	*continuous = (frame >> CONTINIOUS_REMOTE_UPDATE_SUPPORT_SHIFT)
					& CONTINIOUS_REMOTE_UPDATE_SUPPORT_MASK;
}

static void read_vc_remote_fabric(struct hfi1_devdata *dd, u8 *vau, u8 *z,
				  u8 *vcu, u16 *vl15buf, u8 *crc_sizes)
{
	u32 frame;

	read_8051_config(dd, VERIFY_CAP_REMOTE_FABRIC, GENERAL_CONFIG, &frame);
	*vau = (frame >> VAU_SHIFT) & VAU_MASK;
	*z = (frame >> Z_SHIFT) & Z_MASK;
	*vcu = (frame >> VCU_SHIFT) & VCU_MASK;
	*vl15buf = (frame >> VL15BUF_SHIFT) & VL15BUF_MASK;
	*crc_sizes = (frame >> CRC_SIZES_SHIFT) & CRC_SIZES_MASK;
}

static void read_vc_remote_link_width(struct hfi1_devdata *dd,
				      u8 *remote_tx_rate,
				      u16 *link_widths)
{
	u32 frame;

	read_8051_config(dd, VERIFY_CAP_REMOTE_LINK_WIDTH, GENERAL_CONFIG,
			 &frame);
	*remote_tx_rate = (frame >> REMOTE_TX_RATE_SHIFT)
				& REMOTE_TX_RATE_MASK;
	*link_widths = (frame >> LINK_WIDTH_SHIFT) & LINK_WIDTH_MASK;
}

static void read_local_lni(struct hfi1_devdata *dd, u8 *enable_lane_rx)
{
	u32 frame;

	read_8051_config(dd, LOCAL_LNI_INFO, GENERAL_CONFIG, &frame);
	*enable_lane_rx = (frame >> ENABLE_LANE_RX_SHIFT) & ENABLE_LANE_RX_MASK;
}

static void read_last_local_state(struct hfi1_devdata *dd, u32 *lls)
{
	read_8051_config(dd, LAST_LOCAL_STATE_COMPLETE, GENERAL_CONFIG, lls);
}

static void read_last_remote_state(struct hfi1_devdata *dd, u32 *lrs)
{
	read_8051_config(dd, LAST_REMOTE_STATE_COMPLETE, GENERAL_CONFIG, lrs);
}

void hfi1_read_link_quality(struct hfi1_devdata *dd, u8 *link_quality)
{
	u32 frame;
	int ret;

	*link_quality = 0;
	if (dd->pport->host_link_state & HLS_UP) {
		ret = read_8051_config(dd, LINK_QUALITY_INFO, GENERAL_CONFIG,
				       &frame);
		if (ret == 0)
			*link_quality = (frame >> LINK_QUALITY_SHIFT)
						& LINK_QUALITY_MASK;
	}
}

static void read_planned_down_reason_code(struct hfi1_devdata *dd, u8 *pdrrc)
{
	u32 frame;

	read_8051_config(dd, LINK_QUALITY_INFO, GENERAL_CONFIG, &frame);
	*pdrrc = (frame >> DOWN_REMOTE_REASON_SHIFT) & DOWN_REMOTE_REASON_MASK;
}

static void read_link_down_reason(struct hfi1_devdata *dd, u8 *ldr)
{
	u32 frame;

	read_8051_config(dd, LINK_DOWN_REASON, GENERAL_CONFIG, &frame);
	*ldr = (frame & 0xff);
}

static int read_tx_settings(struct hfi1_devdata *dd,
			    u8 *enable_lane_tx,
			    u8 *tx_polarity_inversion,
			    u8 *rx_polarity_inversion,
			    u8 *max_rate)
{
	u32 frame;
	int ret;

	ret = read_8051_config(dd, TX_SETTINGS, GENERAL_CONFIG, &frame);
	*enable_lane_tx = (frame >> ENABLE_LANE_TX_SHIFT)
				& ENABLE_LANE_TX_MASK;
	*tx_polarity_inversion = (frame >> TX_POLARITY_INVERSION_SHIFT)
				& TX_POLARITY_INVERSION_MASK;
	*rx_polarity_inversion = (frame >> RX_POLARITY_INVERSION_SHIFT)
				& RX_POLARITY_INVERSION_MASK;
	*max_rate = (frame >> MAX_RATE_SHIFT) & MAX_RATE_MASK;
	return ret;
}

static int write_tx_settings(struct hfi1_devdata *dd,
			     u8 enable_lane_tx,
			     u8 tx_polarity_inversion,
			     u8 rx_polarity_inversion,
			     u8 max_rate)
{
	u32 frame;

	/* no need to mask, all variable sizes match field widths */
	frame = enable_lane_tx << ENABLE_LANE_TX_SHIFT
		| tx_polarity_inversion << TX_POLARITY_INVERSION_SHIFT
		| rx_polarity_inversion << RX_POLARITY_INVERSION_SHIFT
		| max_rate << MAX_RATE_SHIFT;
	return load_8051_config(dd, TX_SETTINGS, GENERAL_CONFIG, frame);
}

/*
 * Read an idle LCB message.
 *
 * Returns 0 on success, -EINVAL on error
 */
static int read_idle_message(struct hfi1_devdata *dd, u64 type, u64 *data_out)
{
	int ret;

	ret = do_8051_command(dd, HCMD_READ_LCB_IDLE_MSG, type, data_out);
	if (ret != HCMD_SUCCESS) {
		dd_dev_err(dd, "read idle message: type %d, err %d\n",
			   (u32)type, ret);
		return -EINVAL;
	}
	dd_dev_info(dd, "%s: read idle message 0x%llx\n", __func__, *data_out);
	/* return only the payload as we already know the type */
	*data_out >>= IDLE_PAYLOAD_SHIFT;
	return 0;
}

/*
 * Read an idle SMA message.  To be done in response to a notification from
 * the 8051.
 *
 * Returns 0 on success, -EINVAL on error
 */
static int read_idle_sma(struct hfi1_devdata *dd, u64 *data)
{
	return read_idle_message(dd, (u64)IDLE_SMA << IDLE_MSG_TYPE_SHIFT,
				 data);
}

/*
 * Send an idle LCB message.
 *
 * Returns 0 on success, -EINVAL on error
 */
static int send_idle_message(struct hfi1_devdata *dd, u64 data)
{
	int ret;

	dd_dev_info(dd, "%s: sending idle message 0x%llx\n", __func__, data);
	ret = do_8051_command(dd, HCMD_SEND_LCB_IDLE_MSG, data, NULL);
	if (ret != HCMD_SUCCESS) {
		dd_dev_err(dd, "send idle message: data 0x%llx, err %d\n",
			   data, ret);
		return -EINVAL;
	}
	return 0;
}

/*
 * Send an idle SMA message.
 *
 * Returns 0 on success, -EINVAL on error
 */
int send_idle_sma(struct hfi1_devdata *dd, u64 message)
{
	u64 data;

	data = ((message & IDLE_PAYLOAD_MASK) << IDLE_PAYLOAD_SHIFT) |
		((u64)IDLE_SMA << IDLE_MSG_TYPE_SHIFT);
	return send_idle_message(dd, data);
}

/*
 * Initialize the LCB then do a quick link up.  This may or may not be
 * in loopback.
 *
 * return 0 on success, -errno on error
 */
static int do_quick_linkup(struct hfi1_devdata *dd)
{
	int ret;

	lcb_shutdown(dd, 0);

	if (loopback) {
		/* LCB_CFG_LOOPBACK.VAL = 2 */
		/* LCB_CFG_LANE_WIDTH.VAL = 0 */
		write_csr(dd, DC_LCB_CFG_LOOPBACK,
			  IB_PACKET_TYPE << DC_LCB_CFG_LOOPBACK_VAL_SHIFT);
		write_csr(dd, DC_LCB_CFG_LANE_WIDTH, 0);
	}

	/* start the LCBs */
	/* LCB_CFG_TX_FIFOS_RESET.VAL = 0 */
	write_csr(dd, DC_LCB_CFG_TX_FIFOS_RESET, 0);

	/* simulator only loopback steps */
	if (loopback && dd->icode == ICODE_FUNCTIONAL_SIMULATOR) {
		/* LCB_CFG_RUN.EN = 1 */
		write_csr(dd, DC_LCB_CFG_RUN,
			  1ull << DC_LCB_CFG_RUN_EN_SHIFT);

		ret = wait_link_transfer_active(dd, 10);
		if (ret)
			return ret;

		write_csr(dd, DC_LCB_CFG_ALLOW_LINK_UP,
			  1ull << DC_LCB_CFG_ALLOW_LINK_UP_VAL_SHIFT);
	}

	if (!loopback) {
		/*
		 * When doing quick linkup and not in loopback, both
		 * sides must be done with LCB set-up before either
		 * starts the quick linkup.  Put a delay here so that
		 * both sides can be started and have a chance to be
		 * done with LCB set up before resuming.
		 */
		dd_dev_err(dd,
			   "Pausing for peer to be finished with LCB set up\n");
		msleep(5000);
		dd_dev_err(dd, "Continuing with quick linkup\n");
	}

	write_csr(dd, DC_LCB_ERR_EN, 0); /* mask LCB errors */
	set_8051_lcb_access(dd);

	/*
	 * State "quick" LinkUp request sets the physical link state to
	 * LinkUp without a verify capability sequence.
	 * This state is in simulator v37 and later.
	 */
	ret = set_physical_link_state(dd, PLS_QUICK_LINKUP);
	if (ret != HCMD_SUCCESS) {
		dd_dev_err(dd,
			   "%s: set physical link state to quick LinkUp failed with return %d\n",
			   __func__, ret);

		set_host_lcb_access(dd);
		write_csr(dd, DC_LCB_ERR_EN, ~0ull); /* watch LCB errors */

		if (ret >= 0)
			ret = -EINVAL;
		return ret;
	}

	return 0; /* success */
}

/*
 * Do all special steps to set up loopback.
 */
static int init_loopback(struct hfi1_devdata *dd)
{
	dd_dev_info(dd, "Entering loopback mode\n");

	/* all loopbacks should disable self GUID check */
	write_csr(dd, DC_DC8051_CFG_MODE,
		  (read_csr(dd, DC_DC8051_CFG_MODE) | DISABLE_SELF_GUID_CHECK));

	/*
	 * The simulator has only one loopback option - LCB.  Switch
	 * to that option, which includes quick link up.
	 *
	 * Accept all valid loopback values.
	 */
	if ((dd->icode == ICODE_FUNCTIONAL_SIMULATOR) &&
	    (loopback == LOOPBACK_SERDES || loopback == LOOPBACK_LCB ||
	     loopback == LOOPBACK_CABLE)) {
		loopback = LOOPBACK_LCB;
		quick_linkup = 1;
		return 0;
	}

	/*
	 * SerDes loopback init sequence is handled in set_local_link_attributes
	 */
	if (loopback == LOOPBACK_SERDES)
		return 0;

	/* LCB loopback - handled at poll time */
	if (loopback == LOOPBACK_LCB) {
		quick_linkup = 1; /* LCB is always quick linkup */

		/* not supported in emulation due to emulation RTL changes */
		if (dd->icode == ICODE_FPGA_EMULATION) {
			dd_dev_err(dd,
				   "LCB loopback not supported in emulation\n");
			return -EINVAL;
		}
		return 0;
	}

	/* external cable loopback requires no extra steps */
	if (loopback == LOOPBACK_CABLE)
		return 0;

	dd_dev_err(dd, "Invalid loopback mode %d\n", loopback);
	return -EINVAL;
}

/*
 * Translate from the OPA_LINK_WIDTH handed to us by the FM to bits
 * used in the Verify Capability link width attribute.
 */
static u16 opa_to_vc_link_widths(u16 opa_widths)
{
	int i;
	u16 result = 0;

	static const struct link_bits {
		u16 from;
		u16 to;
	} opa_link_xlate[] = {
		{ OPA_LINK_WIDTH_1X, 1 << (1 - 1)  },
		{ OPA_LINK_WIDTH_2X, 1 << (2 - 1)  },
		{ OPA_LINK_WIDTH_3X, 1 << (3 - 1)  },
		{ OPA_LINK_WIDTH_4X, 1 << (4 - 1)  },
	};

	for (i = 0; i < ARRAY_SIZE(opa_link_xlate); i++) {
		if (opa_widths & opa_link_xlate[i].from)
			result |= opa_link_xlate[i].to;
	}
	return result;
}

/*
 * Set link attributes before moving to polling.
 */
static int set_local_link_attributes(struct hfi1_pportdata *ppd)
{
	struct hfi1_devdata *dd = ppd->dd;
	u8 enable_lane_tx;
	u8 tx_polarity_inversion;
	u8 rx_polarity_inversion;
	int ret;
	u32 misc_bits = 0;
	/* reset our fabric serdes to clear any lingering problems */
	fabric_serdes_reset(dd);

	/* set the local tx rate - need to read-modify-write */
	ret = read_tx_settings(dd, &enable_lane_tx, &tx_polarity_inversion,
			       &rx_polarity_inversion, &ppd->local_tx_rate);
	if (ret)
		goto set_local_link_attributes_fail;

	if (dd->dc8051_ver < dc8051_ver(0, 20, 0)) {
		/* set the tx rate to the fastest enabled */
		if (ppd->link_speed_enabled & OPA_LINK_SPEED_25G)
			ppd->local_tx_rate = 1;
		else
			ppd->local_tx_rate = 0;
	} else {
		/* set the tx rate to all enabled */
		ppd->local_tx_rate = 0;
		if (ppd->link_speed_enabled & OPA_LINK_SPEED_25G)
			ppd->local_tx_rate |= 2;
		if (ppd->link_speed_enabled & OPA_LINK_SPEED_12_5G)
			ppd->local_tx_rate |= 1;
	}

	enable_lane_tx = 0xF; /* enable all four lanes */
	ret = write_tx_settings(dd, enable_lane_tx, tx_polarity_inversion,
				rx_polarity_inversion, ppd->local_tx_rate);
	if (ret != HCMD_SUCCESS)
		goto set_local_link_attributes_fail;

	ret = write_host_interface_version(dd, HOST_INTERFACE_VERSION);
	if (ret != HCMD_SUCCESS) {
		dd_dev_err(dd,
			   "Failed to set host interface version, return 0x%x\n",
			   ret);
		goto set_local_link_attributes_fail;
	}

	/*
	 * DC supports continuous updates.
	 */
	ret = write_vc_local_phy(dd,
				 0 /* no power management */,
				 1 /* continuous updates */);
	if (ret != HCMD_SUCCESS)
		goto set_local_link_attributes_fail;

	/* z=1 in the next call: AU of 0 is not supported by the hardware */
	ret = write_vc_local_fabric(dd, dd->vau, 1, dd->vcu, dd->vl15_init,
				    ppd->port_crc_mode_enabled);
	if (ret != HCMD_SUCCESS)
		goto set_local_link_attributes_fail;

	/*
	 * SerDes loopback init sequence requires
	 * setting bit 0 of MISC_CONFIG_BITS
	 */
	if (loopback == LOOPBACK_SERDES)
		misc_bits |= 1 << LOOPBACK_SERDES_CONFIG_BIT_MASK_SHIFT;

	/*
	 * An external device configuration request is used to reset the LCB
	 * to retry to obtain operational lanes when the first attempt is
	 * unsuccesful.
	 */
	if (dd->dc8051_ver >= dc8051_ver(1, 25, 0))
		misc_bits |= 1 << EXT_CFG_LCB_RESET_SUPPORTED_SHIFT;

	ret = write_vc_local_link_mode(dd, misc_bits, 0,
				       opa_to_vc_link_widths(
						ppd->link_width_enabled));
	if (ret != HCMD_SUCCESS)
		goto set_local_link_attributes_fail;

	/* let peer know who we are */
	ret = write_local_device_id(dd, dd->pcidev->device, dd->minrev);
	if (ret == HCMD_SUCCESS)
		return 0;

set_local_link_attributes_fail:
	dd_dev_err(dd,
		   "Failed to set local link attributes, return 0x%x\n",
		   ret);
	return ret;
}

/*
 * Call this to start the link.
 * Do not do anything if the link is disabled.
 * Returns 0 if link is disabled, moved to polling, or the driver is not ready.
 */
int start_link(struct hfi1_pportdata *ppd)
{
	/*
	 * Tune the SerDes to a ballpark setting for optimal signal and bit
	 * error rate.  Needs to be done before starting the link.
	 */
	tune_serdes(ppd);

	if (!ppd->driver_link_ready) {
		dd_dev_info(ppd->dd,
			    "%s: stopping link start because driver is not ready\n",
			    __func__);
		return 0;
	}

	/*
	 * FULL_MGMT_P_KEY is cleared from the pkey table, so that the
	 * pkey table can be configured properly if the HFI unit is connected
	 * to switch port with MgmtAllowed=NO
	 */
	clear_full_mgmt_pkey(ppd);

	return set_link_state(ppd, HLS_DN_POLL);
}

static void wait_for_qsfp_init(struct hfi1_pportdata *ppd)
{
	struct hfi1_devdata *dd = ppd->dd;
	u64 mask;
	unsigned long timeout;

	/*
	 * Some QSFP cables have a quirk that asserts the IntN line as a side
	 * effect of power up on plug-in. We ignore this false positive
	 * interrupt until the module has finished powering up by waiting for
	 * a minimum timeout of the module inrush initialization time of
	 * 500 ms (SFF 8679 Table 5-6) to ensure the voltage rails in the
	 * module have stabilized.
	 */
	msleep(500);

	/*
	 * Check for QSFP interrupt for t_init (SFF 8679 Table 8-1)
	 */
	timeout = jiffies + msecs_to_jiffies(2000);
	while (1) {
		mask = read_csr(dd, dd->hfi1_id ?
				ASIC_QSFP2_IN : ASIC_QSFP1_IN);
		if (!(mask & QSFP_HFI0_INT_N))
			break;
		if (time_after(jiffies, timeout)) {
			dd_dev_info(dd, "%s: No IntN detected, reset complete\n",
				    __func__);
			break;
		}
		udelay(2);
	}
}

static void set_qsfp_int_n(struct hfi1_pportdata *ppd, u8 enable)
{
	struct hfi1_devdata *dd = ppd->dd;
	u64 mask;

	mask = read_csr(dd, dd->hfi1_id ? ASIC_QSFP2_MASK : ASIC_QSFP1_MASK);
	if (enable) {
		/*
		 * Clear the status register to avoid an immediate interrupt
		 * when we re-enable the IntN pin
		 */
		write_csr(dd, dd->hfi1_id ? ASIC_QSFP2_CLEAR : ASIC_QSFP1_CLEAR,
			  QSFP_HFI0_INT_N);
		mask |= (u64)QSFP_HFI0_INT_N;
	} else {
		mask &= ~(u64)QSFP_HFI0_INT_N;
	}
	write_csr(dd, dd->hfi1_id ? ASIC_QSFP2_MASK : ASIC_QSFP1_MASK, mask);
}

int reset_qsfp(struct hfi1_pportdata *ppd)
{
	struct hfi1_devdata *dd = ppd->dd;
	u64 mask, qsfp_mask;

	/* Disable INT_N from triggering QSFP interrupts */
	set_qsfp_int_n(ppd, 0);

	/* Reset the QSFP */
	mask = (u64)QSFP_HFI0_RESET_N;

	qsfp_mask = read_csr(dd,
			     dd->hfi1_id ? ASIC_QSFP2_OUT : ASIC_QSFP1_OUT);
	qsfp_mask &= ~mask;
	write_csr(dd,
		  dd->hfi1_id ? ASIC_QSFP2_OUT : ASIC_QSFP1_OUT, qsfp_mask);

	udelay(10);

	qsfp_mask |= mask;
	write_csr(dd,
		  dd->hfi1_id ? ASIC_QSFP2_OUT : ASIC_QSFP1_OUT, qsfp_mask);

	wait_for_qsfp_init(ppd);

	/*
	 * Allow INT_N to trigger the QSFP interrupt to watch
	 * for alarms and warnings
	 */
	set_qsfp_int_n(ppd, 1);

	/*
	 * After the reset, AOC transmitters are enabled by default. They need
	 * to be turned off to complete the QSFP setup before they can be
	 * enabled again.
	 */
	return set_qsfp_tx(ppd, 0);
}

static int handle_qsfp_error_conditions(struct hfi1_pportdata *ppd,
					u8 *qsfp_interrupt_status)
{
	struct hfi1_devdata *dd = ppd->dd;

	if ((qsfp_interrupt_status[0] & QSFP_HIGH_TEMP_ALARM) ||
	    (qsfp_interrupt_status[0] & QSFP_HIGH_TEMP_WARNING))
		dd_dev_err(dd, "%s: QSFP cable temperature too high\n",
			   __func__);

	if ((qsfp_interrupt_status[0] & QSFP_LOW_TEMP_ALARM) ||
	    (qsfp_interrupt_status[0] & QSFP_LOW_TEMP_WARNING))
		dd_dev_err(dd, "%s: QSFP cable temperature too low\n",
			   __func__);

	/*
	 * The remaining alarms/warnings don't matter if the link is down.
	 */
	if (ppd->host_link_state & HLS_DOWN)
		return 0;

	if ((qsfp_interrupt_status[1] & QSFP_HIGH_VCC_ALARM) ||
	    (qsfp_interrupt_status[1] & QSFP_HIGH_VCC_WARNING))
		dd_dev_err(dd, "%s: QSFP supply voltage too high\n",
			   __func__);

	if ((qsfp_interrupt_status[1] & QSFP_LOW_VCC_ALARM) ||
	    (qsfp_interrupt_status[1] & QSFP_LOW_VCC_WARNING))
		dd_dev_err(dd, "%s: QSFP supply voltage too low\n",
			   __func__);

	/* Byte 2 is vendor specific */

	if ((qsfp_interrupt_status[3] & QSFP_HIGH_POWER_ALARM) ||
	    (qsfp_interrupt_status[3] & QSFP_HIGH_POWER_WARNING))
		dd_dev_err(dd, "%s: Cable RX channel 1/2 power too high\n",
			   __func__);

	if ((qsfp_interrupt_status[3] & QSFP_LOW_POWER_ALARM) ||
	    (qsfp_interrupt_status[3] & QSFP_LOW_POWER_WARNING))
		dd_dev_err(dd, "%s: Cable RX channel 1/2 power too low\n",
			   __func__);

	if ((qsfp_interrupt_status[4] & QSFP_HIGH_POWER_ALARM) ||
	    (qsfp_interrupt_status[4] & QSFP_HIGH_POWER_WARNING))
		dd_dev_err(dd, "%s: Cable RX channel 3/4 power too high\n",
			   __func__);

	if ((qsfp_interrupt_status[4] & QSFP_LOW_POWER_ALARM) ||
	    (qsfp_interrupt_status[4] & QSFP_LOW_POWER_WARNING))
		dd_dev_err(dd, "%s: Cable RX channel 3/4 power too low\n",
			   __func__);

	if ((qsfp_interrupt_status[5] & QSFP_HIGH_BIAS_ALARM) ||
	    (qsfp_interrupt_status[5] & QSFP_HIGH_BIAS_WARNING))
		dd_dev_err(dd, "%s: Cable TX channel 1/2 bias too high\n",
			   __func__);

	if ((qsfp_interrupt_status[5] & QSFP_LOW_BIAS_ALARM) ||
	    (qsfp_interrupt_status[5] & QSFP_LOW_BIAS_WARNING))
		dd_dev_err(dd, "%s: Cable TX channel 1/2 bias too low\n",
			   __func__);

	if ((qsfp_interrupt_status[6] & QSFP_HIGH_BIAS_ALARM) ||
	    (qsfp_interrupt_status[6] & QSFP_HIGH_BIAS_WARNING))
		dd_dev_err(dd, "%s: Cable TX channel 3/4 bias too high\n",
			   __func__);

	if ((qsfp_interrupt_status[6] & QSFP_LOW_BIAS_ALARM) ||
	    (qsfp_interrupt_status[6] & QSFP_LOW_BIAS_WARNING))
		dd_dev_err(dd, "%s: Cable TX channel 3/4 bias too low\n",
			   __func__);

	if ((qsfp_interrupt_status[7] & QSFP_HIGH_POWER_ALARM) ||
	    (qsfp_interrupt_status[7] & QSFP_HIGH_POWER_WARNING))
		dd_dev_err(dd, "%s: Cable TX channel 1/2 power too high\n",
			   __func__);

	if ((qsfp_interrupt_status[7] & QSFP_LOW_POWER_ALARM) ||
	    (qsfp_interrupt_status[7] & QSFP_LOW_POWER_WARNING))
		dd_dev_err(dd, "%s: Cable TX channel 1/2 power too low\n",
			   __func__);

	if ((qsfp_interrupt_status[8] & QSFP_HIGH_POWER_ALARM) ||
	    (qsfp_interrupt_status[8] & QSFP_HIGH_POWER_WARNING))
		dd_dev_err(dd, "%s: Cable TX channel 3/4 power too high\n",
			   __func__);

	if ((qsfp_interrupt_status[8] & QSFP_LOW_POWER_ALARM) ||
	    (qsfp_interrupt_status[8] & QSFP_LOW_POWER_WARNING))
		dd_dev_err(dd, "%s: Cable TX channel 3/4 power too low\n",
			   __func__);

	/* Bytes 9-10 and 11-12 are reserved */
	/* Bytes 13-15 are vendor specific */

	return 0;
}

/* This routine will only be scheduled if the QSFP module present is asserted */
void qsfp_event(struct work_struct *work)
{
	struct qsfp_data *qd;
	struct hfi1_pportdata *ppd;
	struct hfi1_devdata *dd;

	qd = container_of(work, struct qsfp_data, qsfp_work);
	ppd = qd->ppd;
	dd = ppd->dd;

	/* Sanity check */
	if (!qsfp_mod_present(ppd))
		return;

	if (ppd->host_link_state == HLS_DN_DISABLE) {
		dd_dev_info(ppd->dd,
			    "%s: stopping link start because link is disabled\n",
			    __func__);
		return;
	}

	/*
	 * Turn DC back on after cable has been re-inserted. Up until
	 * now, the DC has been in reset to save power.
	 */
	dc_start(dd);

	if (qd->cache_refresh_required) {
		set_qsfp_int_n(ppd, 0);

		wait_for_qsfp_init(ppd);

		/*
		 * Allow INT_N to trigger the QSFP interrupt to watch
		 * for alarms and warnings
		 */
		set_qsfp_int_n(ppd, 1);

		start_link(ppd);
	}

	if (qd->check_interrupt_flags) {
		u8 qsfp_interrupt_status[16] = {0,};

		if (one_qsfp_read(ppd, dd->hfi1_id, 6,
				  &qsfp_interrupt_status[0], 16) != 16) {
			dd_dev_info(dd,
				    "%s: Failed to read status of QSFP module\n",
				    __func__);
		} else {
			unsigned long flags;

			handle_qsfp_error_conditions(
					ppd, qsfp_interrupt_status);
			spin_lock_irqsave(&ppd->qsfp_info.qsfp_lock, flags);
			ppd->qsfp_info.check_interrupt_flags = 0;
			spin_unlock_irqrestore(&ppd->qsfp_info.qsfp_lock,
					       flags);
		}
	}
}

void init_qsfp_int(struct hfi1_devdata *dd)
{
	struct hfi1_pportdata *ppd = dd->pport;
	u64 qsfp_mask;

	qsfp_mask = (u64)(QSFP_HFI0_INT_N | QSFP_HFI0_MODPRST_N);
	/* Clear current status to avoid spurious interrupts */
	write_csr(dd, dd->hfi1_id ? ASIC_QSFP2_CLEAR : ASIC_QSFP1_CLEAR,
		  qsfp_mask);
	write_csr(dd, dd->hfi1_id ? ASIC_QSFP2_MASK : ASIC_QSFP1_MASK,
		  qsfp_mask);

	set_qsfp_int_n(ppd, 0);

	/* Handle active low nature of INT_N and MODPRST_N pins */
	if (qsfp_mod_present(ppd))
		qsfp_mask &= ~(u64)QSFP_HFI0_MODPRST_N;
	write_csr(dd,
		  dd->hfi1_id ? ASIC_QSFP2_INVERT : ASIC_QSFP1_INVERT,
		  qsfp_mask);

	/* Enable the appropriate QSFP IRQ source */
	if (!dd->hfi1_id)
		set_intr_bits(dd, QSFP1_INT, QSFP1_INT, true);
	else
		set_intr_bits(dd, QSFP2_INT, QSFP2_INT, true);
}

/*
 * Do a one-time initialize of the LCB block.
 */
static void init_lcb(struct hfi1_devdata *dd)
{
	/* simulator does not correctly handle LCB cclk loopback, skip */
	if (dd->icode == ICODE_FUNCTIONAL_SIMULATOR)
		return;

	/* the DC has been reset earlier in the driver load */

	/* set LCB for cclk loopback on the port */
	write_csr(dd, DC_LCB_CFG_TX_FIFOS_RESET, 0x01);
	write_csr(dd, DC_LCB_CFG_LANE_WIDTH, 0x00);
	write_csr(dd, DC_LCB_CFG_REINIT_AS_SLAVE, 0x00);
	write_csr(dd, DC_LCB_CFG_CNT_FOR_SKIP_STALL, 0x110);
	write_csr(dd, DC_LCB_CFG_CLK_CNTR, 0x08);
	write_csr(dd, DC_LCB_CFG_LOOPBACK, 0x02);
	write_csr(dd, DC_LCB_CFG_TX_FIFOS_RESET, 0x00);
}

/*
 * Perform a test read on the QSFP.  Return 0 on success, -ERRNO
 * on error.
 */
static int test_qsfp_read(struct hfi1_pportdata *ppd)
{
	int ret;
	u8 status;

	/*
	 * Report success if not a QSFP or, if it is a QSFP, but the cable is
	 * not present
	 */
	if (ppd->port_type != PORT_TYPE_QSFP || !qsfp_mod_present(ppd))
		return 0;

	/* read byte 2, the status byte */
	ret = one_qsfp_read(ppd, ppd->dd->hfi1_id, 2, &status, 1);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EIO;

	return 0; /* success */
}

/*
 * Values for QSFP retry.
 *
 * Give up after 10s (20 x 500ms).  The overall timeout was empirically
 * arrived at from experience on a large cluster.
 */
#define MAX_QSFP_RETRIES 20
#define QSFP_RETRY_WAIT 500 /* msec */

/*
 * Try a QSFP read.  If it fails, schedule a retry for later.
 * Called on first link activation after driver load.
 */
static void try_start_link(struct hfi1_pportdata *ppd)
{
	if (test_qsfp_read(ppd)) {
		/* read failed */
		if (ppd->qsfp_retry_count >= MAX_QSFP_RETRIES) {
			dd_dev_err(ppd->dd, "QSFP not responding, giving up\n");
			return;
		}
		dd_dev_info(ppd->dd,
			    "QSFP not responding, waiting and retrying %d\n",
			    (int)ppd->qsfp_retry_count);
		ppd->qsfp_retry_count++;
		queue_delayed_work(ppd->link_wq, &ppd->start_link_work,
				   msecs_to_jiffies(QSFP_RETRY_WAIT));
		return;
	}
	ppd->qsfp_retry_count = 0;

	start_link(ppd);
}

/*
 * Workqueue function to start the link after a delay.
 */
void handle_start_link(struct work_struct *work)
{
	struct hfi1_pportdata *ppd = container_of(work, struct hfi1_pportdata,
						  start_link_work.work);
	try_start_link(ppd);
}

int bringup_serdes(struct hfi1_pportdata *ppd)
{
	struct hfi1_devdata *dd = ppd->dd;
	u64 guid;
	int ret;

	if (HFI1_CAP_IS_KSET(EXTENDED_PSN))
		add_rcvctrl(dd, RCV_CTRL_RCV_EXTENDED_PSN_ENABLE_SMASK);

	guid = ppd->guids[HFI1_PORT_GUID_INDEX];
	if (!guid) {
		if (dd->base_guid)
			guid = dd->base_guid + ppd->port - 1;
		ppd->guids[HFI1_PORT_GUID_INDEX] = guid;
	}

	/* Set linkinit_reason on power up per OPA spec */
	ppd->linkinit_reason = OPA_LINKINIT_REASON_LINKUP;

	/* one-time init of the LCB */
	init_lcb(dd);

	if (loopback) {
		ret = init_loopback(dd);
		if (ret < 0)
			return ret;
	}

	get_port_type(ppd);
	if (ppd->port_type == PORT_TYPE_QSFP) {
		set_qsfp_int_n(ppd, 0);
		wait_for_qsfp_init(ppd);
		set_qsfp_int_n(ppd, 1);
	}

	try_start_link(ppd);
	return 0;
}

void hfi1_quiet_serdes(struct hfi1_pportdata *ppd)
{
	struct hfi1_devdata *dd = ppd->dd;

	/*
	 * Shut down the link and keep it down.   First turn off that the
	 * driver wants to allow the link to be up (driver_link_ready).
	 * Then make sure the link is not automatically restarted
	 * (link_enabled).  Cancel any pending restart.  And finally
	 * go offline.
	 */
	ppd->driver_link_ready = 0;
	ppd->link_enabled = 0;

	ppd->qsfp_retry_count = MAX_QSFP_RETRIES; /* prevent more retries */
	flush_delayed_work(&ppd->start_link_work);
	cancel_delayed_work_sync(&ppd->start_link_work);

	ppd->offline_disabled_reason =
			HFI1_ODR_MASK(OPA_LINKDOWN_REASON_REBOOT);
	set_link_down_reason(ppd, OPA_LINKDOWN_REASON_REBOOT, 0,
			     OPA_LINKDOWN_REASON_REBOOT);
	set_link_state(ppd, HLS_DN_OFFLINE);

	/* disable the port */
	clear_rcvctrl(dd, RCV_CTRL_RCV_PORT_ENABLE_SMASK);
	cancel_work_sync(&ppd->freeze_work);
}

static inline int init_cpu_counters(struct hfi1_devdata *dd)
{
	struct hfi1_pportdata *ppd;
	int i;

	ppd = (struct hfi1_pportdata *)(dd + 1);
	for (i = 0; i < dd->num_pports; i++, ppd++) {
		ppd->ibport_data.rvp.rc_acks = NULL;
		ppd->ibport_data.rvp.rc_qacks = NULL;
		ppd->ibport_data.rvp.rc_acks = alloc_percpu(u64);
		ppd->ibport_data.rvp.rc_qacks = alloc_percpu(u64);
		ppd->ibport_data.rvp.rc_delayed_comp = alloc_percpu(u64);
		if (!ppd->ibport_data.rvp.rc_acks ||
		    !ppd->ibport_data.rvp.rc_delayed_comp ||
		    !ppd->ibport_data.rvp.rc_qacks)
			return -ENOMEM;
	}

	return 0;
}

/*
 * index is the index into the receive array
 */
void hfi1_put_tid(struct hfi1_devdata *dd, u32 index,
		  u32 type, unsigned long pa, u16 order)
{
	u64 reg;

	if (!(dd->flags & HFI1_PRESENT))
		goto done;

	if (type == PT_INVALID || type == PT_INVALID_FLUSH) {
		pa = 0;
		order = 0;
	} else if (type > PT_INVALID) {
		dd_dev_err(dd,
			   "unexpected receive array type %u for index %u, not handled\n",
			   type, index);
		goto done;
	}
	trace_hfi1_put_tid(dd, index, type, pa, order);

#define RT_ADDR_SHIFT 12	/* 4KB kernel address boundary */
	reg = RCV_ARRAY_RT_WRITE_ENABLE_SMASK
		| (u64)order << RCV_ARRAY_RT_BUF_SIZE_SHIFT
		| ((pa >> RT_ADDR_SHIFT) & RCV_ARRAY_RT_ADDR_MASK)
					<< RCV_ARRAY_RT_ADDR_SHIFT;
	trace_hfi1_write_rcvarray(dd->rcvarray_wc + (index * 8), reg);
	writeq(reg, dd->rcvarray_wc + (index * 8));

	if (type == PT_EAGER || type == PT_INVALID_FLUSH || (index & 3) == 3)
		/*
		 * Eager entries are written and flushed
		 *
		 * Expected entries are flushed every 4 writes
		 */
		flush_wc();
done:
	return;
}

void hfi1_clear_tids(struct hfi1_ctxtdata *rcd)
{
	struct hfi1_devdata *dd = rcd->dd;
	u32 i;

	/* this could be optimized */
	for (i = rcd->eager_base; i < rcd->eager_base +
		     rcd->egrbufs.alloced; i++)
		hfi1_put_tid(dd, i, PT_INVALID, 0, 0);

	for (i = rcd->expected_base;
			i < rcd->expected_base + rcd->expected_count; i++)
		hfi1_put_tid(dd, i, PT_INVALID, 0, 0);
}

static const char * const ib_cfg_name_strings[] = {
	"HFI1_IB_CFG_LIDLMC",
	"HFI1_IB_CFG_LWID_DG_ENB",
	"HFI1_IB_CFG_LWID_ENB",
	"HFI1_IB_CFG_LWID",
	"HFI1_IB_CFG_SPD_ENB",
	"HFI1_IB_CFG_SPD",
	"HFI1_IB_CFG_RXPOL_ENB",
	"HFI1_IB_CFG_LREV_ENB",
	"HFI1_IB_CFG_LINKLATENCY",
	"HFI1_IB_CFG_HRTBT",
	"HFI1_IB_CFG_OP_VLS",
	"HFI1_IB_CFG_VL_HIGH_CAP",
	"HFI1_IB_CFG_VL_LOW_CAP",
	"HFI1_IB_CFG_OVERRUN_THRESH",
	"HFI1_IB_CFG_PHYERR_THRESH",
	"HFI1_IB_CFG_LINKDEFAULT",
	"HFI1_IB_CFG_PKEYS",
	"HFI1_IB_CFG_MTU",
	"HFI1_IB_CFG_LSTATE",
	"HFI1_IB_CFG_VL_HIGH_LIMIT",
	"HFI1_IB_CFG_PMA_TICKS",
	"HFI1_IB_CFG_PORT"
};

static const char *ib_cfg_name(int which)
{
	if (which < 0 || which >= ARRAY_SIZE(ib_cfg_name_strings))
		return "invalid";
	return ib_cfg_name_strings[which];
}

int hfi1_get_ib_cfg(struct hfi1_pportdata *ppd, int which)
{
	struct hfi1_devdata *dd = ppd->dd;
	int val = 0;

	switch (which) {
	case HFI1_IB_CFG_LWID_ENB: /* allowed Link-width */
		val = ppd->link_width_enabled;
		break;
	case HFI1_IB_CFG_LWID: /* currently active Link-width */
		val = ppd->link_width_active;
		break;
	case HFI1_IB_CFG_SPD_ENB: /* allowed Link speeds */
		val = ppd->link_speed_enabled;
		break;
	case HFI1_IB_CFG_SPD: /* current Link speed */
		val = ppd->link_speed_active;
		break;

	case HFI1_IB_CFG_RXPOL_ENB: /* Auto-RX-polarity enable */
	case HFI1_IB_CFG_LREV_ENB: /* Auto-Lane-reversal enable */
	case HFI1_IB_CFG_LINKLATENCY:
		goto unimplemented;

	case HFI1_IB_CFG_OP_VLS:
		val = ppd->actual_vls_operational;
		break;
	case HFI1_IB_CFG_VL_HIGH_CAP: /* VL arb high priority table size */
		val = VL_ARB_HIGH_PRIO_TABLE_SIZE;
		break;
	case HFI1_IB_CFG_VL_LOW_CAP: /* VL arb low priority table size */
		val = VL_ARB_LOW_PRIO_TABLE_SIZE;
		break;
	case HFI1_IB_CFG_OVERRUN_THRESH: /* IB overrun threshold */
		val = ppd->overrun_threshold;
		break;
	case HFI1_IB_CFG_PHYERR_THRESH: /* IB PHY error threshold */
		val = ppd->phy_error_threshold;
		break;
	case HFI1_IB_CFG_LINKDEFAULT: /* IB link default (sleep/poll) */
		val = HLS_DEFAULT;
		break;

	case HFI1_IB_CFG_HRTBT: /* Heartbeat off/enable/auto */
	case HFI1_IB_CFG_PMA_TICKS:
	default:
unimplemented:
		if (HFI1_CAP_IS_KSET(PRINT_UNIMPL))
			dd_dev_info(
				dd,
				"%s: which %s: not implemented\n",
				__func__,
				ib_cfg_name(which));
		break;
	}

	return val;
}

/*
 * The largest MAD packet size.
 */
#define MAX_MAD_PACKET 2048

/*
 * Return the maximum header bytes that can go on the _wire_
 * for this device. This count includes the ICRC which is
 * not part of the packet held in memory but it is appended
 * by the HW.
 * This is dependent on the device's receive header entry size.
 * HFI allows this to be set per-receive context, but the
 * driver presently enforces a global value.
 */
u32 lrh_max_header_bytes(struct hfi1_devdata *dd)
{
	/*
	 * The maximum non-payload (MTU) bytes in LRH.PktLen are
	 * the Receive Header Entry Size minus the PBC (or RHF) size
	 * plus one DW for the ICRC appended by HW.
	 *
	 * dd->rcd[0].rcvhdrqentsize is in DW.
	 * We use rcd[0] as all context will have the same value. Also,
	 * the first kernel context would have been allocated by now so
	 * we are guaranteed a valid value.
	 */
	return (get_hdrqentsize(dd->rcd[0]) - 2/*PBC/RHF*/ + 1/*ICRC*/) << 2;
}

/*
 * Set Send Length
 * @ppd - per port data
 *
 * Set the MTU by limiting how many DWs may be sent.  The SendLenCheck*
 * registers compare against LRH.PktLen, so use the max bytes included
 * in the LRH.
 *
 * This routine changes all VL values except VL15, which it maintains at
 * the same value.
 */
static void set_send_length(struct hfi1_pportdata *ppd)
{
	struct hfi1_devdata *dd = ppd->dd;
	u32 max_hb = lrh_max_header_bytes(dd), dcmtu;
	u32 maxvlmtu = dd->vld[15].mtu;
	u64 len1 = 0, len2 = (((dd->vld[15].mtu + max_hb) >> 2)
			      & SEND_LEN_CHECK1_LEN_VL15_MASK) <<
		SEND_LEN_CHECK1_LEN_VL15_SHIFT;
	int i, j;
	u32 thres;

	for (i = 0; i < ppd->vls_supported; i++) {
		if (dd->vld[i].mtu > maxvlmtu)
			maxvlmtu = dd->vld[i].mtu;
		if (i <= 3)
			len1 |= (((dd->vld[i].mtu + max_hb) >> 2)
				 & SEND_LEN_CHECK0_LEN_VL0_MASK) <<
				((i % 4) * SEND_LEN_CHECK0_LEN_VL1_SHIFT);
		else
			len2 |= (((dd->vld[i].mtu + max_hb) >> 2)
				 & SEND_LEN_CHECK1_LEN_VL4_MASK) <<
				((i % 4) * SEND_LEN_CHECK1_LEN_VL5_SHIFT);
	}
	write_csr(dd, SEND_LEN_CHECK0, len1);
	write_csr(dd, SEND_LEN_CHECK1, len2);
	/* adjust kernel credit return thresholds based on new MTUs */
	/* all kernel receive contexts have the same hdrqentsize */
	for (i = 0; i < ppd->vls_supported; i++) {
		thres = min(sc_percent_to_threshold(dd->vld[i].sc, 50),
			    sc_mtu_to_threshold(dd->vld[i].sc,
						dd->vld[i].mtu,
						get_hdrqentsize(dd->rcd[0])));
		for (j = 0; j < INIT_SC_PER_VL; j++)
			sc_set_cr_threshold(
					pio_select_send_context_vl(dd, j, i),
					    thres);
	}
	thres = min(sc_percent_to_threshold(dd->vld[15].sc, 50),
		    sc_mtu_to_threshold(dd->vld[15].sc,
					dd->vld[15].mtu,
					dd->rcd[0]->rcvhdrqentsize));
	sc_set_cr_threshold(dd->vld[15].sc, thres);

	/* Adjust maximum MTU for the port in DC */
	dcmtu = maxvlmtu == 10240 ? DCC_CFG_PORT_MTU_CAP_10240 :
		(ilog2(maxvlmtu >> 8) + 1);
	len1 = read_csr(ppd->dd, DCC_CFG_PORT_CONFIG);
	len1 &= ~DCC_CFG_PORT_CONFIG_MTU_CAP_SMASK;
	len1 |= ((u64)dcmtu & DCC_CFG_PORT_CONFIG_MTU_CAP_MASK) <<
		DCC_CFG_PORT_CONFIG_MTU_CAP_SHIFT;
	write_csr(ppd->dd, DCC_CFG_PORT_CONFIG, len1);
}

static void set_lidlmc(struct hfi1_pportdata *ppd)
{
	int i;
	u64 sreg = 0;
	struct hfi1_devdata *dd = ppd->dd;
	u32 mask = ~((1U << ppd->lmc) - 1);
	u64 c1 = read_csr(ppd->dd, DCC_CFG_PORT_CONFIG1);
	u32 lid;

	/*
	 * Program 0 in CSR if port lid is extended. This prevents
	 * 9B packets being sent out for large lids.
	 */
	lid = (ppd->lid >= be16_to_cpu(IB_MULTICAST_LID_BASE)) ? 0 : ppd->lid;
	c1 &= ~(DCC_CFG_PORT_CONFIG1_TARGET_DLID_SMASK
		| DCC_CFG_PORT_CONFIG1_DLID_MASK_SMASK);
	c1 |= ((lid & DCC_CFG_PORT_CONFIG1_TARGET_DLID_MASK)
			<< DCC_CFG_PORT_CONFIG1_TARGET_DLID_SHIFT) |
	      ((mask & DCC_CFG_PORT_CONFIG1_DLID_MASK_MASK)
			<< DCC_CFG_PORT_CONFIG1_DLID_MASK_SHIFT);
	write_csr(ppd->dd, DCC_CFG_PORT_CONFIG1, c1);

	/*
	 * Iterate over all the send contexts and set their SLID check
	 */
	sreg = ((mask & SEND_CTXT_CHECK_SLID_MASK_MASK) <<
			SEND_CTXT_CHECK_SLID_MASK_SHIFT) |
	       (((lid & mask) & SEND_CTXT_CHECK_SLID_VALUE_MASK) <<
			SEND_CTXT_CHECK_SLID_VALUE_SHIFT);

	for (i = 0; i < chip_send_contexts(dd); i++) {
		hfi1_cdbg(LINKVERB, "SendContext[%d].SLID_CHECK = 0x%x",
			  i, (u32)sreg);
		write_kctxt_csr(dd, i, SEND_CTXT_CHECK_SLID, sreg);
	}

	/* Now we have to do the same thing for the sdma engines */
	sdma_update_lmc(dd, mask, lid);
}

static const char *state_completed_string(u32 completed)
{
	static const char * const state_completed[] = {
		"EstablishComm",
		"OptimizeEQ",
		"VerifyCap"
	};

	if (completed < ARRAY_SIZE(state_completed))
		return state_completed[completed];

	return "unknown";
}

static const char all_lanes_dead_timeout_expired[] =
	"All lanes were inactive – was the interconnect media removed?";
static const char tx_out_of_policy[] =
	"Passing lanes on local port do not meet the local link width policy";
static const char no_state_complete[] =
	"State timeout occurred before link partner completed the state";
static const char * const state_complete_reasons[] = {
	[0x00] = "Reason unknown",
	[0x01] = "Link was halted by driver, refer to LinkDownReason",
	[0x02] = "Link partner reported failure",
	[0x10] = "Unable to achieve frame sync on any lane",
	[0x11] =
	  "Unable to find a common bit rate with the link partner",
	[0x12] =
	  "Unable to achieve frame sync on sufficient lanes to meet the local link width policy",
	[0x13] =
	  "Unable to identify preset equalization on sufficient lanes to meet the local link width policy",
	[0x14] = no_state_complete,
	[0x15] =
	  "State timeout occurred before link partner identified equalization presets",
	[0x16] =
	  "Link partner completed the EstablishComm state, but the passing lanes do not meet the local link width policy",
	[0x17] = tx_out_of_policy,
	[0x20] = all_lanes_dead_timeout_expired,
	[0x21] =
	  "Unable to achieve acceptable BER on sufficient lanes to meet the local link width policy",
	[0x22] = no_state_complete,
	[0x23] =
	  "Link partner completed the OptimizeEq state, but the passing lanes do not meet the local link width policy",
	[0x24] = tx_out_of_policy,
	[0x30] = all_lanes_dead_timeout_expired,
	[0x31] =
	  "State timeout occurred waiting for host to process received frames",
	[0x32] = no_state_complete,
	[0x33] =
	  "Link partner completed the VerifyCap state, but the passing lanes do not meet the local link width policy",
	[0x34] = tx_out_of_policy,
	[0x35] = "Negotiated link width is mutually exclusive",
	[0x36] =
	  "Timed out before receiving verifycap frames in VerifyCap.Exchange",
	[0x37] = "Unable to resolve secure data exchange",
};

static const char *state_complete_reason_code_string(struct hfi1_pportdata *ppd,
						     u32 code)
{
	const char *str = NULL;

	if (code < ARRAY_SIZE(state_complete_reasons))
		str = state_complete_reasons[code];

	if (str)
		return str;
	return "Reserved";
}

/* describe the given last state complete frame */
static void decode_state_complete(struct hfi1_pportdata *ppd, u32 frame,
				  const char *prefix)
{
	struct hfi1_devdata *dd = ppd->dd;
	u32 success;
	u32 state;
	u32 reason;
	u32 lanes;

	/*
	 * Decode frame:
	 *  [ 0: 0] - success
	 *  [ 3: 1] - state
	 *  [ 7: 4] - next state timeout
	 *  [15: 8] - reason code
	 *  [31:16] - lanes
	 */
	success = frame & 0x1;
	state = (frame >> 1) & 0x7;
	reason = (frame >> 8) & 0xff;
	lanes = (frame >> 16) & 0xffff;

	dd_dev_err(dd, "Last %s LNI state complete frame 0x%08x:\n",
		   prefix, frame);
	dd_dev_err(dd, "    last reported state state: %s (0x%x)\n",
		   state_completed_string(state), state);
	dd_dev_err(dd, "    state successfully completed: %s\n",
		   success ? "yes" : "no");
	dd_dev_err(dd, "    fail reason 0x%x: %s\n",
		   reason, state_complete_reason_code_string(ppd, reason));
	dd_dev_err(dd, "    passing lane mask: 0x%x", lanes);
}

/*
 * Read the last state complete frames and explain them.  This routine
 * expects to be called if the link went down during link negotiation
 * and initialization (LNI).  That is, anywhere between polling and link up.
 */
static void check_lni_states(struct hfi1_pportdata *ppd)
{
	u32 last_local_state;
	u32 last_remote_state;

	read_last_local_state(ppd->dd, &last_local_state);
	read_last_remote_state(ppd->dd, &last_remote_state);

	/*
	 * Don't report anything if there is nothing to report.  A value of
	 * 0 means the link was taken down while polling and there was no
	 * training in-process.
	 */
	if (last_local_state == 0 && last_remote_state == 0)
		return;

	decode_state_complete(ppd, last_local_state, "transmitted");
	decode_state_complete(ppd, last_remote_state, "received");
}

/* wait for wait_ms for LINK_TRANSFER_ACTIVE to go to 1 */
static int wait_link_transfer_active(struct hfi1_devdata *dd, int wait_ms)
{
	u64 reg;
	unsigned long timeout;

	/* watch LCB_STS_LINK_TRANSFER_ACTIVE */
	timeout = jiffies + msecs_to_jiffies(wait_ms);
	while (1) {
		reg = read_csr(dd, DC_LCB_STS_LINK_TRANSFER_ACTIVE);
		if (reg)
			break;
		if (time_after(jiffies, timeout)) {
			dd_dev_err(dd,
				   "timeout waiting for LINK_TRANSFER_ACTIVE\n");
			return -ETIMEDOUT;
		}
		udelay(2);
	}
	return 0;
}

/* called when the logical link state is not down as it should be */
static void force_logical_link_state_down(struct hfi1_pportdata *ppd)
{
	struct hfi1_devdata *dd = ppd->dd;

	/*
	 * Bring link up in LCB loopback
	 */
	write_csr(dd, DC_LCB_CFG_TX_FIFOS_RESET, 1);
	write_csr(dd, DC_LCB_CFG_IGNORE_LOST_RCLK,
		  DC_LCB_CFG_IGNORE_LOST_RCLK_EN_SMASK);

	write_csr(dd, DC_LCB_CFG_LANE_WIDTH, 0);
	write_csr(dd, DC_LCB_CFG_REINIT_AS_SLAVE, 0);
	write_csr(dd, DC_LCB_CFG_CNT_FOR_SKIP_STALL, 0x110);
	write_csr(dd, DC_LCB_CFG_LOOPBACK, 0x2);

	write_csr(dd, DC_LCB_CFG_TX_FIFOS_RESET, 0);
	(void)read_csr(dd, DC_LCB_CFG_TX_FIFOS_RESET);
	udelay(3);
	write_csr(dd, DC_LCB_CFG_ALLOW_LINK_UP, 1);
	write_csr(dd, DC_LCB_CFG_RUN, 1ull << DC_LCB_CFG_RUN_EN_SHIFT);

	wait_link_transfer_active(dd, 100);

	/*
	 * Bring the link down again.
	 */
	write_csr(dd, DC_LCB_CFG_TX_FIFOS_RESET, 1);
	write_csr(dd, DC_LCB_CFG_ALLOW_LINK_UP, 0);
	write_csr(dd, DC_LCB_CFG_IGNORE_LOST_RCLK, 0);

	dd_dev_info(ppd->dd, "logical state forced to LINK_DOWN\n");
}

/*
 * Helper for set_link_state().  Do not call except from that routine.
 * Expects ppd->hls_mutex to be held.
 *
 * @rem_reason value to be sent to the neighbor
 *
 * LinkDownReasons only set if transition succeeds.
 */
static int goto_offline(struct hfi1_pportdata *ppd, u8 rem_reason)
{
	struct hfi1_devdata *dd = ppd->dd;
	u32 previous_state;
	int offline_state_ret;
	int ret;

	update_lcb_cache(dd);

	previous_state = ppd->host_link_state;
	ppd->host_link_state = HLS_GOING_OFFLINE;

	/* start offline transition */
	ret = set_physical_link_state(dd, (rem_reason << 8) | PLS_OFFLINE);

	if (ret != HCMD_SUCCESS) {
		dd_dev_err(dd,
			   "Failed to transition to Offline link state, return %d\n",
			   ret);
		return -EINVAL;
	}
	if (ppd->offline_disabled_reason ==
			HFI1_ODR_MASK(OPA_LINKDOWN_REASON_NONE))
		ppd->offline_disabled_reason =
		HFI1_ODR_MASK(OPA_LINKDOWN_REASON_TRANSIENT);

	offline_state_ret = wait_phys_link_offline_substates(ppd, 10000);
	if (offline_state_ret < 0)
		return offline_state_ret;

	/* Disabling AOC transmitters */
	if (ppd->port_type == PORT_TYPE_QSFP &&
	    ppd->qsfp_info.limiting_active &&
	    qsfp_mod_present(ppd)) {
		int ret;

		ret = acquire_chip_resource(dd, qsfp_resource(dd), QSFP_WAIT);
		if (ret == 0) {
			set_qsfp_tx(ppd, 0);
			release_chip_resource(dd, qsfp_resource(dd));
		} else {
			/* not fatal, but should warn */
			dd_dev_err(dd,
				   "Unable to acquire lock to turn off QSFP TX\n");
		}
	}

	/*
	 * Wait for the offline.Quiet transition if it hasn't happened yet. It
	 * can take a while for the link to go down.
	 */
	if (offline_state_ret != PLS_OFFLINE_QUIET) {
		ret = wait_physical_linkstate(ppd, PLS_OFFLINE, 30000);
		if (ret < 0)
			return ret;
	}

	/*
	 * Now in charge of LCB - must be after the physical state is
	 * offline.quiet and before host_link_state is changed.
	 */
	set_host_lcb_access(dd);
	write_csr(dd, DC_LCB_ERR_EN, ~0ull); /* watch LCB errors */

	/* make sure the logical state is also down */
	ret = wait_logical_linkstate(ppd, IB_PORT_DOWN, 1000);
	if (ret)
		force_logical_link_state_down(ppd);

	ppd->host_link_state = HLS_LINK_COOLDOWN; /* LCB access allowed */
	update_statusp(ppd, IB_PORT_DOWN);

	/*
	 * The LNI has a mandatory wait time after the physical state
	 * moves to Offline.Quiet.  The wait time may be different
	 * depending on how the link went down.  The 8051 firmware
	 * will observe the needed wait time and only move to ready
	 * when that is completed.  The largest of the quiet timeouts
	 * is 6s, so wait that long and then at least 0.5s more for
	 * other transitions, and another 0.5s for a buffer.
	 */
	ret = wait_fm_ready(dd, 7000);
	if (ret) {
		dd_dev_err(dd,
			   "After going offline, timed out waiting for the 8051 to become ready to accept host requests\n");
		/* state is really offline, so make it so */
		ppd->host_link_state = HLS_DN_OFFLINE;
		return ret;
	}

	/*
	 * The state is now offline and the 8051 is ready to accept host
	 * requests.
	 *	- change our state
	 *	- notify others if we were previously in a linkup state
	 */
	ppd->host_link_state = HLS_DN_OFFLINE;
	if (previous_state & HLS_UP) {
		/* went down while link was up */
		handle_linkup_change(dd, 0);
	} else if (previous_state
			& (HLS_DN_POLL | HLS_VERIFY_CAP | HLS_GOING_UP)) {
		/* went down while attempting link up */
		check_lni_states(ppd);

		/* The QSFP doesn't need to be reset on LNI failure */
		ppd->qsfp_info.reset_needed = 0;
	}

	/* the active link width (downgrade) is 0 on link down */
	ppd->link_width_active = 0;
	ppd->link_width_downgrade_tx_active = 0;
	ppd->link_width_downgrade_rx_active = 0;
	ppd->current_egress_rate = 0;
	return 0;
}

/* return the link state name */
static const char *link_state_name(u32 state)
{
	const char *name;
	int n = ilog2(state);
	static const char * const names[] = {
		[__HLS_UP_INIT_BP]	 = "INIT",
		[__HLS_UP_ARMED_BP]	 = "ARMED",
		[__HLS_UP_ACTIVE_BP]	 = "ACTIVE",
		[__HLS_DN_DOWNDEF_BP]	 = "DOWNDEF",
		[__HLS_DN_POLL_BP]	 = "POLL",
		[__HLS_DN_DISABLE_BP]	 = "DISABLE",
		[__HLS_DN_OFFLINE_BP]	 = "OFFLINE",
		[__HLS_VERIFY_CAP_BP]	 = "VERIFY_CAP",
		[__HLS_GOING_UP_BP]	 = "GOING_UP",
		[__HLS_GOING_OFFLINE_BP] = "GOING_OFFLINE",
		[__HLS_LINK_COOLDOWN_BP] = "LINK_COOLDOWN"
	};

	name = n < ARRAY_SIZE(names) ? names[n] : NULL;
	return name ? name : "unknown";
}

/* return the link state reason name */
static const char *link_state_reason_name(struct hfi1_pportdata *ppd, u32 state)
{
	if (state == HLS_UP_INIT) {
		switch (ppd->linkinit_reason) {
		case OPA_LINKINIT_REASON_LINKUP:
			return "(LINKUP)";
		case OPA_LINKINIT_REASON_FLAPPING:
			return "(FLAPPING)";
		case OPA_LINKINIT_OUTSIDE_POLICY:
			return "(OUTSIDE_POLICY)";
		case OPA_LINKINIT_QUARANTINED:
			return "(QUARANTINED)";
		case OPA_LINKINIT_INSUFIC_CAPABILITY:
			return "(INSUFIC_CAPABILITY)";
		default:
			break;
		}
	}
	return "";
}

/*
 * driver_pstate - convert the driver's notion of a port's
 * state (an HLS_*) into a physical state (a {IB,OPA}_PORTPHYSSTATE_*).
 * Return -1 (converted to a u32) to indicate error.
 */
u32 driver_pstate(struct hfi1_pportdata *ppd)
{
	switch (ppd->host_link_state) {
	case HLS_UP_INIT:
	case HLS_UP_ARMED:
	case HLS_UP_ACTIVE:
		return IB_PORTPHYSSTATE_LINKUP;
	case HLS_DN_POLL:
		return IB_PORTPHYSSTATE_POLLING;
	case HLS_DN_DISABLE:
		return IB_PORTPHYSSTATE_DISABLED;
	case HLS_DN_OFFLINE:
		return OPA_PORTPHYSSTATE_OFFLINE;
	case HLS_VERIFY_CAP:
		return IB_PORTPHYSSTATE_TRAINING;
	case HLS_GOING_UP:
		return IB_PORTPHYSSTATE_TRAINING;
	case HLS_GOING_OFFLINE:
		return OPA_PORTPHYSSTATE_OFFLINE;
	case HLS_LINK_COOLDOWN:
		return OPA_PORTPHYSSTATE_OFFLINE;
	case HLS_DN_DOWNDEF:
	default:
		dd_dev_err(ppd->dd, "invalid host_link_state 0x%x\n",
			   ppd->host_link_state);
		return  -1;
	}
}

/*
 * driver_lstate - convert the driver's notion of a port's
 * state (an HLS_*) into a logical state (a IB_PORT_*). Return -1
 * (converted to a u32) to indicate error.
 */
u32 driver_lstate(struct hfi1_pportdata *ppd)
{
	if (ppd->host_link_state && (ppd->host_link_state & HLS_DOWN))
		return IB_PORT_DOWN;

	switch (ppd->host_link_state & HLS_UP) {
	case HLS_UP_INIT:
		return IB_PORT_INIT;
	case HLS_UP_ARMED:
		return IB_PORT_ARMED;
	case HLS_UP_ACTIVE:
		return IB_PORT_ACTIVE;
	default:
		dd_dev_err(ppd->dd, "invalid host_link_state 0x%x\n",
			   ppd->host_link_state);
	return -1;
	}
}

void set_link_down_reason(struct hfi1_pportdata *ppd, u8 lcl_reason,
			  u8 neigh_reason, u8 rem_reason)
{
	if (ppd->local_link_down_reason.latest == 0 &&
	    ppd->neigh_link_down_reason.latest == 0) {
		ppd->local_link_down_reason.latest = lcl_reason;
		ppd->neigh_link_down_reason.latest = neigh_reason;
		ppd->remote_link_down_reason = rem_reason;
	}
}

/**
 * data_vls_operational() - Verify if data VL BCT credits and MTU
 *			    are both set.
 * @ppd: pointer to hfi1_pportdata structure
 *
 * Return: true - Ok, false -otherwise.
 */
static inline bool data_vls_operational(struct hfi1_pportdata *ppd)
{
	int i;
	u64 reg;

	if (!ppd->actual_vls_operational)
		return false;

	for (i = 0; i < ppd->vls_supported; i++) {
		reg = read_csr(ppd->dd, SEND_CM_CREDIT_VL + (8 * i));
		if ((reg && !ppd->dd->vld[i].mtu) ||
		    (!reg && ppd->dd->vld[i].mtu))
			return false;
	}

	return true;
}

/*
 * Change the physical and/or logical link state.
 *
 * Do not call this routine while inside an interrupt.  It contains
 * calls to routines that can take multiple seconds to finish.
 *
 * Returns 0 on success, -errno on failure.
 */
int set_link_state(struct hfi1_pportdata *ppd, u32 state)
{
	struct hfi1_devdata *dd = ppd->dd;
	struct ib_event event = {.device = NULL};
	int ret1, ret = 0;
	int orig_new_state, poll_bounce;

	mutex_lock(&ppd->hls_lock);

	orig_new_state = state;
	if (state == HLS_DN_DOWNDEF)
		state = HLS_DEFAULT;

	/* interpret poll -> poll as a link bounce */
	poll_bounce = ppd->host_link_state == HLS_DN_POLL &&
		      state == HLS_DN_POLL;

	dd_dev_info(dd, "%s: current %s, new %s %s%s\n", __func__,
		    link_state_name(ppd->host_link_state),
		    link_state_name(orig_new_state),
		    poll_bounce ? "(bounce) " : "",
		    link_state_reason_name(ppd, state));

	/*
	 * If we're going to a (HLS_*) link state that implies the logical
	 * link state is neither of (IB_PORT_ARMED, IB_PORT_ACTIVE), then
	 * reset is_sm_config_started to 0.
	 */
	if (!(state & (HLS_UP_ARMED | HLS_UP_ACTIVE)))
		ppd->is_sm_config_started = 0;

	/*
	 * Do nothing if the states match.  Let a poll to poll link bounce
	 * go through.
	 */
	if (ppd->host_link_state == state && !poll_bounce)
		goto done;

	switch (state) {
	case HLS_UP_INIT:
		if (ppd->host_link_state == HLS_DN_POLL &&
		    (quick_linkup || dd->icode == ICODE_FUNCTIONAL_SIMULATOR)) {
			/*
			 * Quick link up jumps from polling to here.
			 *
			 * Whether in normal or loopback mode, the
			 * simulator jumps from polling to link up.
			 * Accept that here.
			 */
			/* OK */
		} else if (ppd->host_link_state != HLS_GOING_UP) {
			goto unexpected;
		}

		/*
		 * Wait for Link_Up physical state.
		 * Physical and Logical states should already be
		 * be transitioned to LinkUp and LinkInit respectively.
		 */
		ret = wait_physical_linkstate(ppd, PLS_LINKUP, 1000);
		if (ret) {
			dd_dev_err(dd,
				   "%s: physical state did not change to LINK-UP\n",
				   __func__);
			break;
		}

		ret = wait_logical_linkstate(ppd, IB_PORT_INIT, 1000);
		if (ret) {
			dd_dev_err(dd,
				   "%s: logical state did not change to INIT\n",
				   __func__);
			break;
		}

		/* clear old transient LINKINIT_REASON code */
		if (ppd->linkinit_reason >= OPA_LINKINIT_REASON_CLEAR)
			ppd->linkinit_reason =
				OPA_LINKINIT_REASON_LINKUP;

		/* enable the port */
		add_rcvctrl(dd, RCV_CTRL_RCV_PORT_ENABLE_SMASK);

		handle_linkup_change(dd, 1);
		pio_kernel_linkup(dd);

		/*
		 * After link up, a new link width will have been set.
		 * Update the xmit counters with regards to the new
		 * link width.
		 */
		update_xmit_counters(ppd, ppd->link_width_active);

		ppd->host_link_state = HLS_UP_INIT;
		update_statusp(ppd, IB_PORT_INIT);
		break;
	case HLS_UP_ARMED:
		if (ppd->host_link_state != HLS_UP_INIT)
			goto unexpected;

		if (!data_vls_operational(ppd)) {
			dd_dev_err(dd,
				   "%s: Invalid data VL credits or mtu\n",
				   __func__);
			ret = -EINVAL;
			break;
		}

		set_logical_state(dd, LSTATE_ARMED);
		ret = wait_logical_linkstate(ppd, IB_PORT_ARMED, 1000);
		if (ret) {
			dd_dev_err(dd,
				   "%s: logical state did not change to ARMED\n",
				   __func__);
			break;
		}
		ppd->host_link_state = HLS_UP_ARMED;
		update_statusp(ppd, IB_PORT_ARMED);
		/*
		 * The simulator does not currently implement SMA messages,
		 * so neighbor_normal is not set.  Set it here when we first
		 * move to Armed.
		 */
		if (dd->icode == ICODE_FUNCTIONAL_SIMULATOR)
			ppd->neighbor_normal = 1;
		break;
	case HLS_UP_ACTIVE:
		if (ppd->host_link_state != HLS_UP_ARMED)
			goto unexpected;

		set_logical_state(dd, LSTATE_ACTIVE);
		ret = wait_logical_linkstate(ppd, IB_PORT_ACTIVE, 1000);
		if (ret) {
			dd_dev_err(dd,
				   "%s: logical state did not change to ACTIVE\n",
				   __func__);
		} else {
			/* tell all engines to go running */
			sdma_all_running(dd);
			ppd->host_link_state = HLS_UP_ACTIVE;
			update_statusp(ppd, IB_PORT_ACTIVE);

			/* Signal the IB layer that the port has went active */
			event.device = &dd->verbs_dev.rdi.ibdev;
			event.element.port_num = ppd->port;
			event.event = IB_EVENT_PORT_ACTIVE;
		}
		break;
	case HLS_DN_POLL:
		if ((ppd->host_link_state == HLS_DN_DISABLE ||
		     ppd->host_link_state == HLS_DN_OFFLINE) &&
		    dd->dc_shutdown)
			dc_start(dd);
		/* Hand LED control to the DC */
		write_csr(dd, DCC_CFG_LED_CNTRL, 0);

		if (ppd->host_link_state != HLS_DN_OFFLINE) {
			u8 tmp = ppd->link_enabled;

			ret = goto_offline(ppd, ppd->remote_link_down_reason);
			if (ret) {
				ppd->link_enabled = tmp;
				break;
			}
			ppd->remote_link_down_reason = 0;

			if (ppd->driver_link_ready)
				ppd->link_enabled = 1;
		}

		set_all_slowpath(ppd->dd);
		ret = set_local_link_attributes(ppd);
		if (ret)
			break;

		ppd->port_error_action = 0;

		if (quick_linkup) {
			/* quick linkup does not go into polling */
			ret = do_quick_linkup(dd);
		} else {
			ret1 = set_physical_link_state(dd, PLS_POLLING);
			if (!ret1)
				ret1 = wait_phys_link_out_of_offline(ppd,
								     3000);
			if (ret1 != HCMD_SUCCESS) {
				dd_dev_err(dd,
					   "Failed to transition to Polling link state, return 0x%x\n",
					   ret1);
				ret = -EINVAL;
			}
		}

		/*
		 * Change the host link state after requesting DC8051 to
		 * change its physical state so that we can ignore any
		 * interrupt with stale LNI(XX) error, which will not be
		 * cleared until DC8051 transitions to Polling state.
		 */
		ppd->host_link_state = HLS_DN_POLL;
		ppd->offline_disabled_reason =
			HFI1_ODR_MASK(OPA_LINKDOWN_REASON_NONE);
		/*
		 * If an error occurred above, go back to offline.  The
		 * caller may reschedule another attempt.
		 */
		if (ret)
			goto_offline(ppd, 0);
		else
			log_physical_state(ppd, PLS_POLLING);
		break;
	case HLS_DN_DISABLE:
		/* link is disabled */
		ppd->link_enabled = 0;

		/* allow any state to transition to disabled */

		/* must transition to offline first */
		if (ppd->host_link_state != HLS_DN_OFFLINE) {
			ret = goto_offline(ppd, ppd->remote_link_down_reason);
			if (ret)
				break;
			ppd->remote_link_down_reason = 0;
		}

		if (!dd->dc_shutdown) {
			ret1 = set_physical_link_state(dd, PLS_DISABLED);
			if (ret1 != HCMD_SUCCESS) {
				dd_dev_err(dd,
					   "Failed to transition to Disabled link state, return 0x%x\n",
					   ret1);
				ret = -EINVAL;
				break;
			}
			ret = wait_physical_linkstate(ppd, PLS_DISABLED, 10000);
			if (ret) {
				dd_dev_err(dd,
					   "%s: physical state did not change to DISABLED\n",
					   __func__);
				break;
			}
			dc_shutdown(dd);
		}
		ppd->host_link_state = HLS_DN_DISABLE;
		break;
	case HLS_DN_OFFLINE:
		if (ppd->host_link_state == HLS_DN_DISABLE)
			dc_start(dd);

		/* allow any state to transition to offline */
		ret = goto_offline(ppd, ppd->remote_link_down_reason);
		if (!ret)
			ppd->remote_link_down_reason = 0;
		break;
	case HLS_VERIFY_CAP:
		if (ppd->host_link_state != HLS_DN_POLL)
			goto unexpected;
		ppd->host_link_state = HLS_VERIFY_CAP;
		log_physical_state(ppd, PLS_CONFIGPHY_VERIFYCAP);
		break;
	case HLS_GOING_UP:
		if (ppd->host_link_state != HLS_VERIFY_CAP)
			goto unexpected;

		ret1 = set_physical_link_state(dd, PLS_LINKUP);
		if (ret1 != HCMD_SUCCESS) {
			dd_dev_err(dd,
				   "Failed to transition to link up state, return 0x%x\n",
				   ret1);
			ret = -EINVAL;
			break;
		}
		ppd->host_link_state = HLS_GOING_UP;
		break;

	case HLS_GOING_OFFLINE:		/* transient within goto_offline() */
	case HLS_LINK_COOLDOWN:		/* transient within goto_offline() */
	default:
		dd_dev_info(dd, "%s: state 0x%x: not supported\n",
			    __func__, state);
		ret = -EINVAL;
		break;
	}

	goto done;

unexpected:
	dd_dev_err(dd, "%s: unexpected state transition from %s to %s\n",
		   __func__, link_state_name(ppd->host_link_state),
		   link_state_name(state));
	ret = -EINVAL;

done:
	mutex_unlock(&ppd->hls_lock);

	if (event.device)
		ib_dispatch_event(&event);

	return ret;
}

int hfi1_set_ib_cfg(struct hfi1_pportdata *ppd, int which, u32 val)
{
	u64 reg;
	int ret = 0;

	switch (which) {
	case HFI1_IB_CFG_LIDLMC:
		set_lidlmc(ppd);
		break;
	case HFI1_IB_CFG_VL_HIGH_LIMIT:
		/*
		 * The VL Arbitrator high limit is sent in units of 4k
		 * bytes, while HFI stores it in units of 64 bytes.
		 */
		val *= 4096 / 64;
		reg = ((u64)val & SEND_HIGH_PRIORITY_LIMIT_LIMIT_MASK)
			<< SEND_HIGH_PRIORITY_LIMIT_LIMIT_SHIFT;
		write_csr(ppd->dd, SEND_HIGH_PRIORITY_LIMIT, reg);
		break;
	case HFI1_IB_CFG_LINKDEFAULT: /* IB link default (sleep/poll) */
		/* HFI only supports POLL as the default link down state */
		if (val != HLS_DN_POLL)
			ret = -EINVAL;
		break;
	case HFI1_IB_CFG_OP_VLS:
		if (ppd->vls_operational != val) {
			ppd->vls_operational = val;
			if (!ppd->port)
				ret = -EINVAL;
		}
		break;
	/*
	 * For link width, link width downgrade, and speed enable, always AND
	 * the setting with what is actually supported.  This has two benefits.
	 * First, enabled can't have unsupported values, no matter what the
	 * SM or FM might want.  Second, the ALL_SUPPORTED wildcards that mean
	 * "fill in with your supported value" have all the bits in the
	 * field set, so simply ANDing with supported has the desired result.
	 */
	case HFI1_IB_CFG_LWID_ENB: /* set allowed Link-width */
		ppd->link_width_enabled = val & ppd->link_width_supported;
		break;
	case HFI1_IB_CFG_LWID_DG_ENB: /* set allowed link width downgrade */
		ppd->link_width_downgrade_enabled =
				val & ppd->link_width_downgrade_supported;
		break;
	case HFI1_IB_CFG_SPD_ENB: /* allowed Link speeds */
		ppd->link_speed_enabled = val & ppd->link_speed_supported;
		break;
	case HFI1_IB_CFG_OVERRUN_THRESH: /* IB overrun threshold */
		/*
		 * HFI does not follow IB specs, save this value
		 * so we can report it, if asked.
		 */
		ppd->overrun_threshold = val;
		break;
	case HFI1_IB_CFG_PHYERR_THRESH: /* IB PHY error threshold */
		/*
		 * HFI does not follow IB specs, save this value
		 * so we can report it, if asked.
		 */
		ppd->phy_error_threshold = val;
		break;

	case HFI1_IB_CFG_MTU:
		set_send_length(ppd);
		break;

	case HFI1_IB_CFG_PKEYS:
		if (HFI1_CAP_IS_KSET(PKEY_CHECK))
			set_partition_keys(ppd);
		break;

	default:
		if (HFI1_CAP_IS_KSET(PRINT_UNIMPL))
			dd_dev_info(ppd->dd,
				    "%s: which %s, val 0x%x: not implemented\n",
				    __func__, ib_cfg_name(which), val);
		break;
	}
	return ret;
}

/* begin functions related to vl arbitration table caching */
static void init_vl_arb_caches(struct hfi1_pportdata *ppd)
{
	int i;

	BUILD_BUG_ON(VL_ARB_TABLE_SIZE !=
			VL_ARB_LOW_PRIO_TABLE_SIZE);
	BUILD_BUG_ON(VL_ARB_TABLE_SIZE !=
			VL_ARB_HIGH_PRIO_TABLE_SIZE);

	/*
	 * Note that we always return values directly from the
	 * 'vl_arb_cache' (and do no CSR reads) in response to a
	 * 'Get(VLArbTable)'. This is obviously correct after a
	 * 'Set(VLArbTable)', since the cache will then be up to
	 * date. But it's also correct prior to any 'Set(VLArbTable)'
	 * since then both the cache, and the relevant h/w registers
	 * will be zeroed.
	 */

	for (i = 0; i < MAX_PRIO_TABLE; i++)
		spin_lock_init(&ppd->vl_arb_cache[i].lock);
}

/*
 * vl_arb_lock_cache
 *
 * All other vl_arb_* functions should be called only after locking
 * the cache.
 */
static inline struct vl_arb_cache *
vl_arb_lock_cache(struct hfi1_pportdata *ppd, int idx)
{
	if (idx != LO_PRIO_TABLE && idx != HI_PRIO_TABLE)
		return NULL;
	spin_lock(&ppd->vl_arb_cache[idx].lock);
	return &ppd->vl_arb_cache[idx];
}

static inline void vl_arb_unlock_cache(struct hfi1_pportdata *ppd, int idx)
{
	spin_unlock(&ppd->vl_arb_cache[idx].lock);
}

static void vl_arb_get_cache(struct vl_arb_cache *cache,
			     struct ib_vl_weight_elem *vl)
{
	memcpy(vl, cache->table, VL_ARB_TABLE_SIZE * sizeof(*vl));
}

static void vl_arb_set_cache(struct vl_arb_cache *cache,
			     struct ib_vl_weight_elem *vl)
{
	memcpy(cache->table, vl, VL_ARB_TABLE_SIZE * sizeof(*vl));
}

static int vl_arb_match_cache(struct vl_arb_cache *cache,
			      struct ib_vl_weight_elem *vl)
{
	return !memcmp(cache->table, vl, VL_ARB_TABLE_SIZE * sizeof(*vl));
}

/* end functions related to vl arbitration table caching */

static int set_vl_weights(struct hfi1_pportdata *ppd, u32 target,
			  u32 size, struct ib_vl_weight_elem *vl)
{
	struct hfi1_devdata *dd = ppd->dd;
	u64 reg;
	unsigned int i, is_up = 0;
	int drain, ret = 0;

	mutex_lock(&ppd->hls_lock);

	if (ppd->host_link_state & HLS_UP)
		is_up = 1;

	drain = !is_ax(dd) && is_up;

	if (drain)
		/*
		 * Before adjusting VL arbitration weights, empty per-VL
		 * FIFOs, otherwise a packet whose VL weight is being
		 * set to 0 could get stuck in a FIFO with no chance to
		 * egress.
		 */
		ret = stop_drain_data_vls(dd);

	if (ret) {
		dd_dev_err(
			dd,
			"%s: cannot stop/drain VLs - refusing to change VL arbitration weights\n",
			__func__);
		goto err;
	}

	for (i = 0; i < size; i++, vl++) {
		/*
		 * NOTE: The low priority shift and mask are used here, but
		 * they are the same for both the low and high registers.
		 */
		reg = (((u64)vl->vl & SEND_LOW_PRIORITY_LIST_VL_MASK)
				<< SEND_LOW_PRIORITY_LIST_VL_SHIFT)
		      | (((u64)vl->weight
				& SEND_LOW_PRIORITY_LIST_WEIGHT_MASK)
				<< SEND_LOW_PRIORITY_LIST_WEIGHT_SHIFT);
		write_csr(dd, target + (i * 8), reg);
	}
	pio_send_control(dd, PSC_GLOBAL_VLARB_ENABLE);

	if (drain)
		open_fill_data_vls(dd); /* reopen all VLs */

err:
	mutex_unlock(&ppd->hls_lock);

	return ret;
}

/*
 * Read one credit merge VL register.
 */
static void read_one_cm_vl(struct hfi1_devdata *dd, u32 csr,
			   struct vl_limit *vll)
{
	u64 reg = read_csr(dd, csr);

	vll->dedicated = cpu_to_be16(
		(reg >> SEND_CM_CREDIT_VL_DEDICATED_LIMIT_VL_SHIFT)
		& SEND_CM_CREDIT_VL_DEDICATED_LIMIT_VL_MASK);
	vll->shared = cpu_to_be16(
		(reg >> SEND_CM_CREDIT_VL_SHARED_LIMIT_VL_SHIFT)
		& SEND_CM_CREDIT_VL_SHARED_LIMIT_VL_MASK);
}

/*
 * Read the current credit merge limits.
 */
static int get_buffer_control(struct hfi1_devdata *dd,
			      struct buffer_control *bc, u16 *overall_limit)
{
	u64 reg;
	int i;

	/* not all entries are filled in */
	memset(bc, 0, sizeof(*bc));

	/* OPA and HFI have a 1-1 mapping */
	for (i = 0; i < TXE_NUM_DATA_VL; i++)
		read_one_cm_vl(dd, SEND_CM_CREDIT_VL + (8 * i), &bc->vl[i]);

	/* NOTE: assumes that VL* and VL15 CSRs are bit-wise identical */
	read_one_cm_vl(dd, SEND_CM_CREDIT_VL15, &bc->vl[15]);

	reg = read_csr(dd, SEND_CM_GLOBAL_CREDIT);
	bc->overall_shared_limit = cpu_to_be16(
		(reg >> SEND_CM_GLOBAL_CREDIT_SHARED_LIMIT_SHIFT)
		& SEND_CM_GLOBAL_CREDIT_SHARED_LIMIT_MASK);
	if (overall_limit)
		*overall_limit = (reg
			>> SEND_CM_GLOBAL_CREDIT_TOTAL_CREDIT_LIMIT_SHIFT)
			& SEND_CM_GLOBAL_CREDIT_TOTAL_CREDIT_LIMIT_MASK;
	return sizeof(struct buffer_control);
}

static int get_sc2vlnt(struct hfi1_devdata *dd, struct sc2vlnt *dp)
{
	u64 reg;
	int i;

	/* each register contains 16 SC->VLnt mappings, 4 bits each */
	reg = read_csr(dd, DCC_CFG_SC_VL_TABLE_15_0);
	for (i = 0; i < sizeof(u64); i++) {
		u8 byte = *(((u8 *)&reg) + i);

		dp->vlnt[2 * i] = byte & 0xf;
		dp->vlnt[(2 * i) + 1] = (byte & 0xf0) >> 4;
	}

	reg = read_csr(dd, DCC_CFG_SC_VL_TABLE_31_16);
	for (i = 0; i < sizeof(u64); i++) {
		u8 byte = *(((u8 *)&reg) + i);

		dp->vlnt[16 + (2 * i)] = byte & 0xf;
		dp->vlnt[16 + (2 * i) + 1] = (byte & 0xf0) >> 4;
	}
	return sizeof(struct sc2vlnt);
}

static void get_vlarb_preempt(struct hfi1_devdata *dd, u32 nelems,
			      struct ib_vl_weight_elem *vl)
{
	unsigned int i;

	for (i = 0; i < nelems; i++, vl++) {
		vl->vl = 0xf;
		vl->weight = 0;
	}
}

static void set_sc2vlnt(struct hfi1_devdata *dd, struct sc2vlnt *dp)
{
	write_csr(dd, DCC_CFG_SC_VL_TABLE_15_0,
		  DC_SC_VL_VAL(15_0,
			       0, dp->vlnt[0] & 0xf,
			       1, dp->vlnt[1] & 0xf,
			       2, dp->vlnt[2] & 0xf,
			       3, dp->vlnt[3] & 0xf,
			       4, dp->vlnt[4] & 0xf,
			       5, dp->vlnt[5] & 0xf,
			       6, dp->vlnt[6] & 0xf,
			       7, dp->vlnt[7] & 0xf,
			       8, dp->vlnt[8] & 0xf,
			       9, dp->vlnt[9] & 0xf,
			       10, dp->vlnt[10] & 0xf,
			       11, dp->vlnt[11] & 0xf,
			       12, dp->vlnt[12] & 0xf,
			       13, dp->vlnt[13] & 0xf,
			       14, dp->vlnt[14] & 0xf,
			       15, dp->vlnt[15] & 0xf));
	write_csr(dd, DCC_CFG_SC_VL_TABLE_31_16,
		  DC_SC_VL_VAL(31_16,
			       16, dp->vlnt[16] & 0xf,
			       17, dp->vlnt[17] & 0xf,
			       18, dp->vlnt[18] & 0xf,
			       19, dp->vlnt[19] & 0xf,
			       20, dp->vlnt[20] & 0xf,
			       21, dp->vlnt[21] & 0xf,
			       22, dp->vlnt[22] & 0xf,
			       23, dp->vlnt[23] & 0xf,
			       24, dp->vlnt[24] & 0xf,
			       25, dp->vlnt[25] & 0xf,
			       26, dp->vlnt[26] & 0xf,
			       27, dp->vlnt[27] & 0xf,
			       28, dp->vlnt[28] & 0xf,
			       29, dp->vlnt[29] & 0xf,
			       30, dp->vlnt[30] & 0xf,
			       31, dp->vlnt[31] & 0xf));
}

static void nonzero_msg(struct hfi1_devdata *dd, int idx, const char *what,
			u16 limit)
{
	if (limit != 0)
		dd_dev_info(dd, "Invalid %s limit %d on VL %d, ignoring\n",
			    what, (int)limit, idx);
}

/* change only the shared limit portion of SendCmGLobalCredit */
static void set_global_shared(struct hfi1_devdata *dd, u16 limit)
{
	u64 reg;

	reg = read_csr(dd, SEND_CM_GLOBAL_CREDIT);
	reg &= ~SEND_CM_GLOBAL_CREDIT_SHARED_LIMIT_SMASK;
	reg |= (u64)limit << SEND_CM_GLOBAL_CREDIT_SHARED_LIMIT_SHIFT;
	write_csr(dd, SEND_CM_GLOBAL_CREDIT, reg);
}

/* change only the total credit limit portion of SendCmGLobalCredit */
static void set_global_limit(struct hfi1_devdata *dd, u16 limit)
{
	u64 reg;

	reg = read_csr(dd, SEND_CM_GLOBAL_CREDIT);
	reg &= ~SEND_CM_GLOBAL_CREDIT_TOTAL_CREDIT_LIMIT_SMASK;
	reg |= (u64)limit << SEND_CM_GLOBAL_CREDIT_TOTAL_CREDIT_LIMIT_SHIFT;
	write_csr(dd, SEND_CM_GLOBAL_CREDIT, reg);
}

/* set the given per-VL shared limit */
static void set_vl_shared(struct hfi1_devdata *dd, int vl, u16 limit)
{
	u64 reg;
	u32 addr;

	if (vl < TXE_NUM_DATA_VL)
		addr = SEND_CM_CREDIT_VL + (8 * vl);
	else
		addr = SEND_CM_CREDIT_VL15;

	reg = read_csr(dd, addr);
	reg &= ~SEND_CM_CREDIT_VL_SHARED_LIMIT_VL_SMASK;
	reg |= (u64)limit << SEND_CM_CREDIT_VL_SHARED_LIMIT_VL_SHIFT;
	write_csr(dd, addr, reg);
}

/* set the given per-VL dedicated limit */
static void set_vl_dedicated(struct hfi1_devdata *dd, int vl, u16 limit)
{
	u64 reg;
	u32 addr;

	if (vl < TXE_NUM_DATA_VL)
		addr = SEND_CM_CREDIT_VL + (8 * vl);
	else
		addr = SEND_CM_CREDIT_VL15;

	reg = read_csr(dd, addr);
	reg &= ~SEND_CM_CREDIT_VL_DEDICATED_LIMIT_VL_SMASK;
	reg |= (u64)limit << SEND_CM_CREDIT_VL_DEDICATED_LIMIT_VL_SHIFT;
	write_csr(dd, addr, reg);
}

/* spin until the given per-VL status mask bits clear */
static void wait_for_vl_status_clear(struct hfi1_devdata *dd, u64 mask,
				     const char *which)
{
	unsigned long timeout;
	u64 reg;

	timeout = jiffies + msecs_to_jiffies(VL_STATUS_CLEAR_TIMEOUT);
	while (1) {
		reg = read_csr(dd, SEND_CM_CREDIT_USED_STATUS) & mask;

		if (reg == 0)
			return;	/* success */
		if (time_after(jiffies, timeout))
			break;		/* timed out */
		udelay(1);
	}

	dd_dev_err(dd,
		   "%s credit change status not clearing after %dms, mask 0x%llx, not clear 0x%llx\n",
		   which, VL_STATUS_CLEAR_TIMEOUT, mask, reg);
	/*
	 * If this occurs, it is likely there was a credit loss on the link.
	 * The only recovery from that is a link bounce.
	 */
	dd_dev_err(dd,
		   "Continuing anyway.  A credit loss may occur.  Suggest a link bounce\n");
}

/*
 * The number of credits on the VLs may be changed while everything
 * is "live", but the following algorithm must be followed due to
 * how the hardware is actually implemented.  In particular,
 * Return_Credit_Status[] is the only correct status check.
 *
 * if (reducing Global_Shared_Credit_Limit or any shared limit changing)
 *     set Global_Shared_Credit_Limit = 0
 *     use_all_vl = 1
 * mask0 = all VLs that are changing either dedicated or shared limits
 * set Shared_Limit[mask0] = 0
 * spin until Return_Credit_Status[use_all_vl ? all VL : mask0] == 0
 * if (changing any dedicated limit)
 *     mask1 = all VLs that are lowering dedicated limits
 *     lower Dedicated_Limit[mask1]
 *     spin until Return_Credit_Status[mask1] == 0
 *     raise Dedicated_Limits
 * raise Shared_Limits
 * raise Global_Shared_Credit_Limit
 *
 * lower = if the new limit is lower, set the limit to the new value
 * raise = if the new limit is higher than the current value (may be changed
 *	earlier in the algorithm), set the new limit to the new value
 */
int set_buffer_control(struct hfi1_pportdata *ppd,
		       struct buffer_control *new_bc)
{
	struct hfi1_devdata *dd = ppd->dd;
	u64 changing_mask, ld_mask, stat_mask;
	int change_count;
	int i, use_all_mask;
	int this_shared_changing;
	int vl_count = 0, ret;
	/*
	 * A0: add the variable any_shared_limit_changing below and in the
	 * algorithm above.  If removing A0 support, it can be removed.
	 */
	int any_shared_limit_changing;
	struct buffer_control cur_bc;
	u8 changing[OPA_MAX_VLS];
	u8 lowering_dedicated[OPA_MAX_VLS];
	u16 cur_total;
	u32 new_total = 0;
	const u64 all_mask =
	SEND_CM_CREDIT_USED_STATUS_VL0_RETURN_CREDIT_STATUS_SMASK
	 | SEND_CM_CREDIT_USED_STATUS_VL1_RETURN_CREDIT_STATUS_SMASK
	 | SEND_CM_CREDIT_USED_STATUS_VL2_RETURN_CREDIT_STATUS_SMASK
	 | SEND_CM_CREDIT_USED_STATUS_VL3_RETURN_CREDIT_STATUS_SMASK
	 | SEND_CM_CREDIT_USED_STATUS_VL4_RETURN_CREDIT_STATUS_SMASK
	 | SEND_CM_CREDIT_USED_STATUS_VL5_RETURN_CREDIT_STATUS_SMASK
	 | SEND_CM_CREDIT_USED_STATUS_VL6_RETURN_CREDIT_STATUS_SMASK
	 | SEND_CM_CREDIT_USED_STATUS_VL7_RETURN_CREDIT_STATUS_SMASK
	 | SEND_CM_CREDIT_USED_STATUS_VL15_RETURN_CREDIT_STATUS_SMASK;

#define valid_vl(idx) ((idx) < TXE_NUM_DATA_VL || (idx) == 15)
#define NUM_USABLE_VLS 16	/* look at VL15 and less */

	/* find the new total credits, do sanity check on unused VLs */
	for (i = 0; i < OPA_MAX_VLS; i++) {
		if (valid_vl(i)) {
			new_total += be16_to_cpu(new_bc->vl[i].dedicated);
			continue;
		}
		nonzero_msg(dd, i, "dedicated",
			    be16_to_cpu(new_bc->vl[i].dedicated));
		nonzero_msg(dd, i, "shared",
			    be16_to_cpu(new_bc->vl[i].shared));
		new_bc->vl[i].dedicated = 0;
		new_bc->vl[i].shared = 0;
	}
	new_total += be16_to_cpu(new_bc->overall_shared_limit);

	/* fetch the current values */
	get_buffer_control(dd, &cur_bc, &cur_total);

	/*
	 * Create the masks we will use.
	 */
	memset(changing, 0, sizeof(changing));
	memset(lowering_dedicated, 0, sizeof(lowering_dedicated));
	/*
	 * NOTE: Assumes that the individual VL bits are adjacent and in
	 * increasing order
	 */
	stat_mask =
		SEND_CM_CREDIT_USED_STATUS_VL0_RETURN_CREDIT_STATUS_SMASK;
	changing_mask = 0;
	ld_mask = 0;
	change_count = 0;
	any_shared_limit_changing = 0;
	for (i = 0; i < NUM_USABLE_VLS; i++, stat_mask <<= 1) {
		if (!valid_vl(i))
			continue;
		this_shared_changing = new_bc->vl[i].shared
						!= cur_bc.vl[i].shared;
		if (this_shared_changing)
			any_shared_limit_changing = 1;
		if (new_bc->vl[i].dedicated != cur_bc.vl[i].dedicated ||
		    this_shared_changing) {
			changing[i] = 1;
			changing_mask |= stat_mask;
			change_count++;
		}
		if (be16_to_cpu(new_bc->vl[i].dedicated) <
					be16_to_cpu(cur_bc.vl[i].dedicated)) {
			lowering_dedicated[i] = 1;
			ld_mask |= stat_mask;
		}
	}

	/* bracket the credit change with a total adjustment */
	if (new_total > cur_total)
		set_global_limit(dd, new_total);

	/*
	 * Start the credit change algorithm.
	 */
	use_all_mask = 0;
	if ((be16_to_cpu(new_bc->overall_shared_limit) <
	     be16_to_cpu(cur_bc.overall_shared_limit)) ||
	    (is_ax(dd) && any_shared_limit_changing)) {
		set_global_shared(dd, 0);
		cur_bc.overall_shared_limit = 0;
		use_all_mask = 1;
	}

	for (i = 0; i < NUM_USABLE_VLS; i++) {
		if (!valid_vl(i))
			continue;

		if (changing[i]) {
			set_vl_shared(dd, i, 0);
			cur_bc.vl[i].shared = 0;
		}
	}

	wait_for_vl_status_clear(dd, use_all_mask ? all_mask : changing_mask,
				 "shared");

	if (change_count > 0) {
		for (i = 0; i < NUM_USABLE_VLS; i++) {
			if (!valid_vl(i))
				continue;

			if (lowering_dedicated[i]) {
				set_vl_dedicated(dd, i,
						 be16_to_cpu(new_bc->
							     vl[i].dedicated));
				cur_bc.vl[i].dedicated =
						new_bc->vl[i].dedicated;
			}
		}

		wait_for_vl_status_clear(dd, ld_mask, "dedicated");

		/* now raise all dedicated that are going up */
		for (i = 0; i < NUM_USABLE_VLS; i++) {
			if (!valid_vl(i))
				continue;

			if (be16_to_cpu(new_bc->vl[i].dedicated) >
					be16_to_cpu(cur_bc.vl[i].dedicated))
				set_vl_dedicated(dd, i,
						 be16_to_cpu(new_bc->
							     vl[i].dedicated));
		}
	}

	/* next raise all shared that are going up */
	for (i = 0; i < NUM_USABLE_VLS; i++) {
		if (!valid_vl(i))
			continue;

		if (be16_to_cpu(new_bc->vl[i].shared) >
				be16_to_cpu(cur_bc.vl[i].shared))
			set_vl_shared(dd, i, be16_to_cpu(new_bc->vl[i].shared));
	}

	/* finally raise the global shared */
	if (be16_to_cpu(new_bc->overall_shared_limit) >
	    be16_to_cpu(cur_bc.overall_shared_limit))
		set_global_shared(dd,
				  be16_to_cpu(new_bc->overall_shared_limit));

	/* bracket the credit change with a total adjustment */
	if (new_total < cur_total)
		set_global_limit(dd, new_total);

	/*
	 * Determine the actual number of operational VLS using the number of
	 * dedicated and shared credits for each VL.
	 */
	if (change_count > 0) {
		for (i = 0; i < TXE_NUM_DATA_VL; i++)
			if (be16_to_cpu(new_bc->vl[i].dedicated) > 0 ||
			    be16_to_cpu(new_bc->vl[i].shared) > 0)
				vl_count++;
		ppd->actual_vls_operational = vl_count;
		ret = sdma_map_init(dd, ppd->port - 1, vl_count ?
				    ppd->actual_vls_operational :
				    ppd->vls_operational,
				    NULL);
		if (ret == 0)
			ret = pio_map_init(dd, ppd->port - 1, vl_count ?
					   ppd->actual_vls_operational :
					   ppd->vls_operational, NULL);
		if (ret)
			return ret;
	}
	return 0;
}

/*
 * Read the given fabric manager table. Return the size of the
 * table (in bytes) on success, and a negative error code on
 * failure.
 */
int fm_get_table(struct hfi1_pportdata *ppd, int which, void *t)

{
	int size;
	struct vl_arb_cache *vlc;

	switch (which) {
	case FM_TBL_VL_HIGH_ARB:
		size = 256;
		/*
		 * OPA specifies 128 elements (of 2 bytes each), though
		 * HFI supports only 16 elements in h/w.
		 */
		vlc = vl_arb_lock_cache(ppd, HI_PRIO_TABLE);
		vl_arb_get_cache(vlc, t);
		vl_arb_unlock_cache(ppd, HI_PRIO_TABLE);
		break;
	case FM_TBL_VL_LOW_ARB:
		size = 256;
		/*
		 * OPA specifies 128 elements (of 2 bytes each), though
		 * HFI supports only 16 elements in h/w.
		 */
		vlc = vl_arb_lock_cache(ppd, LO_PRIO_TABLE);
		vl_arb_get_cache(vlc, t);
		vl_arb_unlock_cache(ppd, LO_PRIO_TABLE);
		break;
	case FM_TBL_BUFFER_CONTROL:
		size = get_buffer_control(ppd->dd, t, NULL);
		break;
	case FM_TBL_SC2VLNT:
		size = get_sc2vlnt(ppd->dd, t);
		break;
	case FM_TBL_VL_PREEMPT_ELEMS:
		size = 256;
		/* OPA specifies 128 elements, of 2 bytes each */
		get_vlarb_preempt(ppd->dd, OPA_MAX_VLS, t);
		break;
	case FM_TBL_VL_PREEMPT_MATRIX:
		size = 256;
		/*
		 * OPA specifies that this is the same size as the VL
		 * arbitration tables (i.e., 256 bytes).
		 */
		break;
	default:
		return -EINVAL;
	}
	return size;
}

/*
 * Write the given fabric manager table.
 */
int fm_set_table(struct hfi1_pportdata *ppd, int which, void *t)
{
	int ret = 0;
	struct vl_arb_cache *vlc;

	switch (which) {
	case FM_TBL_VL_HIGH_ARB:
		vlc = vl_arb_lock_cache(ppd, HI_PRIO_TABLE);
		if (vl_arb_match_cache(vlc, t)) {
			vl_arb_unlock_cache(ppd, HI_PRIO_TABLE);
			break;
		}
		vl_arb_set_cache(vlc, t);
		vl_arb_unlock_cache(ppd, HI_PRIO_TABLE);
		ret = set_vl_weights(ppd, SEND_HIGH_PRIORITY_LIST,
				     VL_ARB_HIGH_PRIO_TABLE_SIZE, t);
		break;
	case FM_TBL_VL_LOW_ARB:
		vlc = vl_arb_lock_cache(ppd, LO_PRIO_TABLE);
		if (vl_arb_match_cache(vlc, t)) {
			vl_arb_unlock_cache(ppd, LO_PRIO_TABLE);
			break;
		}
		vl_arb_set_cache(vlc, t);
		vl_arb_unlock_cache(ppd, LO_PRIO_TABLE);
		ret = set_vl_weights(ppd, SEND_LOW_PRIORITY_LIST,
				     VL_ARB_LOW_PRIO_TABLE_SIZE, t);
		break;
	case FM_TBL_BUFFER_CONTROL:
		ret = set_buffer_control(ppd, t);
		break;
	case FM_TBL_SC2VLNT:
		set_sc2vlnt(ppd->dd, t);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

/*
 * Disable all data VLs.
 *
 * Return 0 if disabled, non-zero if the VLs cannot be disabled.
 */
static int disable_data_vls(struct hfi1_devdata *dd)
{
	if (is_ax(dd))
		return 1;

	pio_send_control(dd, PSC_DATA_VL_DISABLE);

	return 0;
}

/*
 * open_fill_data_vls() - the counterpart to stop_drain_data_vls().
 * Just re-enables all data VLs (the "fill" part happens
 * automatically - the name was chosen for symmetry with
 * stop_drain_data_vls()).
 *
 * Return 0 if successful, non-zero if the VLs cannot be enabled.
 */
int open_fill_data_vls(struct hfi1_devdata *dd)
{
	if (is_ax(dd))
		return 1;

	pio_send_control(dd, PSC_DATA_VL_ENABLE);

	return 0;
}

/*
 * drain_data_vls() - assumes that disable_data_vls() has been called,
 * wait for occupancy (of per-VL FIFOs) for all contexts, and SDMA
 * engines to drop to 0.
 */
static void drain_data_vls(struct hfi1_devdata *dd)
{
	sc_wait(dd);
	sdma_wait(dd);
	pause_for_credit_return(dd);
}

/*
 * stop_drain_data_vls() - disable, then drain all per-VL fifos.
 *
 * Use open_fill_data_vls() to resume using data VLs.  This pair is
 * meant to be used like this:
 *
 * stop_drain_data_vls(dd);
 * // do things with per-VL resources
 * open_fill_data_vls(dd);
 */
int stop_drain_data_vls(struct hfi1_devdata *dd)
{
	int ret;

	ret = disable_data_vls(dd);
	if (ret == 0)
		drain_data_vls(dd);

	return ret;
}

/*
 * Convert a nanosecond time to a cclock count.  No matter how slow
 * the cclock, a non-zero ns will always have a non-zero result.
 */
u32 ns_to_cclock(struct hfi1_devdata *dd, u32 ns)
{
	u32 cclocks;

	if (dd->icode == ICODE_FPGA_EMULATION)
		cclocks = (ns * 1000) / FPGA_CCLOCK_PS;
	else  /* simulation pretends to be ASIC */
		cclocks = (ns * 1000) / ASIC_CCLOCK_PS;
	if (ns && !cclocks)	/* if ns nonzero, must be at least 1 */
		cclocks = 1;
	return cclocks;
}

/*
 * Convert a cclock count to nanoseconds. Not matter how slow
 * the cclock, a non-zero cclocks will always have a non-zero result.
 */
u32 cclock_to_ns(struct hfi1_devdata *dd, u32 cclocks)
{
	u32 ns;

	if (dd->icode == ICODE_FPGA_EMULATION)
		ns = (cclocks * FPGA_CCLOCK_PS) / 1000;
	else  /* simulation pretends to be ASIC */
		ns = (cclocks * ASIC_CCLOCK_PS) / 1000;
	if (cclocks && !ns)
		ns = 1;
	return ns;
}

/*
 * Dynamically adjust the receive interrupt timeout for a context based on
 * incoming packet rate.
 *
 * NOTE: Dynamic adjustment does not allow rcv_intr_count to be zero.
 */
static void adjust_rcv_timeout(struct hfi1_ctxtdata *rcd, u32 npkts)
{
	struct hfi1_devdata *dd = rcd->dd;
	u32 timeout = rcd->rcvavail_timeout;

	/*
	 * This algorithm doubles or halves the timeout depending on whether
	 * the number of packets received in this interrupt were less than or
	 * greater equal the interrupt count.
	 *
	 * The calculations below do not allow a steady state to be achieved.
	 * Only at the endpoints it is possible to have an unchanging
	 * timeout.
	 */
	if (npkts < rcv_intr_count) {
		/*
		 * Not enough packets arrived before the timeout, adjust
		 * timeout downward.
		 */
		if (timeout < 2) /* already at minimum? */
			return;
		timeout >>= 1;
	} else {
		/*
		 * More than enough packets arrived before the timeout, adjust
		 * timeout upward.
		 */
		if (timeout >= dd->rcv_intr_timeout_csr) /* already at max? */
			return;
		timeout = min(timeout << 1, dd->rcv_intr_timeout_csr);
	}

	rcd->rcvavail_timeout = timeout;
	/*
	 * timeout cannot be larger than rcv_intr_timeout_csr which has already
	 * been verified to be in range
	 */
	write_kctxt_csr(dd, rcd->ctxt, RCV_AVAIL_TIME_OUT,
			(u64)timeout <<
			RCV_AVAIL_TIME_OUT_TIME_OUT_RELOAD_SHIFT);
}

void update_usrhead(struct hfi1_ctxtdata *rcd, u32 hd, u32 updegr, u32 egrhd,
		    u32 intr_adjust, u32 npkts)
{
	struct hfi1_devdata *dd = rcd->dd;
	u64 reg;
	u32 ctxt = rcd->ctxt;

	/*
	 * Need to write timeout register before updating RcvHdrHead to ensure
	 * that a new value is used when the HW decides to restart counting.
	 */
	if (intr_adjust)
		adjust_rcv_timeout(rcd, npkts);
	if (updegr) {
		reg = (egrhd & RCV_EGR_INDEX_HEAD_HEAD_MASK)
			<< RCV_EGR_INDEX_HEAD_HEAD_SHIFT;
		write_uctxt_csr(dd, ctxt, RCV_EGR_INDEX_HEAD, reg);
	}
	reg = ((u64)rcv_intr_count << RCV_HDR_HEAD_COUNTER_SHIFT) |
		(((u64)hd & RCV_HDR_HEAD_HEAD_MASK)
			<< RCV_HDR_HEAD_HEAD_SHIFT);
	write_uctxt_csr(dd, ctxt, RCV_HDR_HEAD, reg);
}

u32 hdrqempty(struct hfi1_ctxtdata *rcd)
{
	u32 head, tail;

	head = (read_uctxt_csr(rcd->dd, rcd->ctxt, RCV_HDR_HEAD)
		& RCV_HDR_HEAD_HEAD_SMASK) >> RCV_HDR_HEAD_HEAD_SHIFT;

	if (hfi1_rcvhdrtail_kvaddr(rcd))
		tail = get_rcvhdrtail(rcd);
	else
		tail = read_uctxt_csr(rcd->dd, rcd->ctxt, RCV_HDR_TAIL);

	return head == tail;
}

/*
 * Context Control and Receive Array encoding for buffer size:
 *	0x0 invalid
 *	0x1   4 KB
 *	0x2   8 KB
 *	0x3  16 KB
 *	0x4  32 KB
 *	0x5  64 KB
 *	0x6 128 KB
 *	0x7 256 KB
 *	0x8 512 KB (Receive Array only)
 *	0x9   1 MB (Receive Array only)
 *	0xa   2 MB (Receive Array only)
 *
 *	0xB-0xF - reserved (Receive Array only)
 *
 *
 * This routine assumes that the value has already been sanity checked.
 */
static u32 encoded_size(u32 size)
{
	switch (size) {
	case   4 * 1024: return 0x1;
	case   8 * 1024: return 0x2;
	case  16 * 1024: return 0x3;
	case  32 * 1024: return 0x4;
	case  64 * 1024: return 0x5;
	case 128 * 1024: return 0x6;
	case 256 * 1024: return 0x7;
	case 512 * 1024: return 0x8;
	case   1 * 1024 * 1024: return 0x9;
	case   2 * 1024 * 1024: return 0xa;
	}
	return 0x1;	/* if invalid, go with the minimum size */
}

/**
 * encode_rcv_header_entry_size - return chip specific encoding for size
 * @size: size in dwords
 *
 * Convert a receive header entry size that to the encoding used in the CSR.
 *
 * Return a zero if the given size is invalid, otherwise the encoding.
 */
u8 encode_rcv_header_entry_size(u8 size)
{
	/* there are only 3 valid receive header entry sizes */
	if (size == 2)
		return 1;
	if (size == 16)
		return 2;
	if (size == 32)
		return 4;
	return 0; /* invalid */
}

/**
 * hfi1_validate_rcvhdrcnt - validate hdrcnt
 * @dd: the device data
 * @thecnt: the header count
 */
int hfi1_validate_rcvhdrcnt(struct hfi1_devdata *dd, uint thecnt)
{
	if (thecnt <= HFI1_MIN_HDRQ_EGRBUF_CNT) {
		dd_dev_err(dd, "Receive header queue count too small\n");
		return -EINVAL;
	}

	if (thecnt > HFI1_MAX_HDRQ_EGRBUF_CNT) {
		dd_dev_err(dd,
			   "Receive header queue count cannot be greater than %u\n",
			   HFI1_MAX_HDRQ_EGRBUF_CNT);
		return -EINVAL;
	}

	if (thecnt % HDRQ_INCREMENT) {
		dd_dev_err(dd, "Receive header queue count %d must be divisible by %lu\n",
			   thecnt, HDRQ_INCREMENT);
		return -EINVAL;
	}

	return 0;
}

/**
 * set_hdrq_regs - set header queue registers for context
 * @dd: the device data
 * @ctxt: the context
 * @entsize: the dword entry size
 * @hdrcnt: the number of header entries
 */
void set_hdrq_regs(struct hfi1_devdata *dd, u8 ctxt, u8 entsize, u16 hdrcnt)
{
	u64 reg;

	reg = (((u64)hdrcnt >> HDRQ_SIZE_SHIFT) & RCV_HDR_CNT_CNT_MASK) <<
	      RCV_HDR_CNT_CNT_SHIFT;
	write_kctxt_csr(dd, ctxt, RCV_HDR_CNT, reg);
	reg = ((u64)encode_rcv_header_entry_size(entsize) &
	       RCV_HDR_ENT_SIZE_ENT_SIZE_MASK) <<
	      RCV_HDR_ENT_SIZE_ENT_SIZE_SHIFT;
	write_kctxt_csr(dd, ctxt, RCV_HDR_ENT_SIZE, reg);
	reg = ((u64)DEFAULT_RCVHDRSIZE & RCV_HDR_SIZE_HDR_SIZE_MASK) <<
	      RCV_HDR_SIZE_HDR_SIZE_SHIFT;
	write_kctxt_csr(dd, ctxt, RCV_HDR_SIZE, reg);

	/*
	 * Program dummy tail address for every receive context
	 * before enabling any receive context
	 */
	write_kctxt_csr(dd, ctxt, RCV_HDR_TAIL_ADDR,
			dd->rcvhdrtail_dummy_dma);
}

void hfi1_rcvctrl(struct hfi1_devdata *dd, unsigned int op,
		  struct hfi1_ctxtdata *rcd)
{
	u64 rcvctrl, reg;
	int did_enable = 0;
	u16 ctxt;

	if (!rcd)
		return;

	ctxt = rcd->ctxt;

	hfi1_cdbg(RCVCTRL, "ctxt %d op 0x%x", ctxt, op);

	rcvctrl = read_kctxt_csr(dd, ctxt, RCV_CTXT_CTRL);
	/* if the context already enabled, don't do the extra steps */
	if ((op & HFI1_RCVCTRL_CTXT_ENB) &&
	    !(rcvctrl & RCV_CTXT_CTRL_ENABLE_SMASK)) {
		/* reset the tail and hdr addresses, and sequence count */
		write_kctxt_csr(dd, ctxt, RCV_HDR_ADDR,
				rcd->rcvhdrq_dma);
		if (hfi1_rcvhdrtail_kvaddr(rcd))
			write_kctxt_csr(dd, ctxt, RCV_HDR_TAIL_ADDR,
					rcd->rcvhdrqtailaddr_dma);
		hfi1_set_seq_cnt(rcd, 1);

		/* reset the cached receive header queue head value */
		hfi1_set_rcd_head(rcd, 0);

		/*
		 * Zero the receive header queue so we don't get false
		 * positives when checking the sequence number.  The
		 * sequence numbers could land exactly on the same spot.
		 * E.g. a rcd restart before the receive header wrapped.
		 */
		memset(rcd->rcvhdrq, 0, rcvhdrq_size(rcd));

		/* starting timeout */
		rcd->rcvavail_timeout = dd->rcv_intr_timeout_csr;

		/* enable the context */
		rcvctrl |= RCV_CTXT_CTRL_ENABLE_SMASK;

		/* clean the egr buffer size first */
		rcvctrl &= ~RCV_CTXT_CTRL_EGR_BUF_SIZE_SMASK;
		rcvctrl |= ((u64)encoded_size(rcd->egrbufs.rcvtid_size)
				& RCV_CTXT_CTRL_EGR_BUF_SIZE_MASK)
					<< RCV_CTXT_CTRL_EGR_BUF_SIZE_SHIFT;

		/* zero RcvHdrHead - set RcvHdrHead.Counter after enable */
		write_uctxt_csr(dd, ctxt, RCV_HDR_HEAD, 0);
		did_enable = 1;

		/* zero RcvEgrIndexHead */
		write_uctxt_csr(dd, ctxt, RCV_EGR_INDEX_HEAD, 0);

		/* set eager count and base index */
		reg = (((u64)(rcd->egrbufs.alloced >> RCV_SHIFT)
			& RCV_EGR_CTRL_EGR_CNT_MASK)
		       << RCV_EGR_CTRL_EGR_CNT_SHIFT) |
			(((rcd->eager_base >> RCV_SHIFT)
			  & RCV_EGR_CTRL_EGR_BASE_INDEX_MASK)
			 << RCV_EGR_CTRL_EGR_BASE_INDEX_SHIFT);
		write_kctxt_csr(dd, ctxt, RCV_EGR_CTRL, reg);

		/*
		 * Set TID (expected) count and base index.
		 * rcd->expected_count is set to individual RcvArray entries,
		 * not pairs, and the CSR takes a pair-count in groups of
		 * four, so divide by 8.
		 */
		reg = (((rcd->expected_count >> RCV_SHIFT)
					& RCV_TID_CTRL_TID_PAIR_CNT_MASK)
				<< RCV_TID_CTRL_TID_PAIR_CNT_SHIFT) |
		      (((rcd->expected_base >> RCV_SHIFT)
					& RCV_TID_CTRL_TID_BASE_INDEX_MASK)
				<< RCV_TID_CTRL_TID_BASE_INDEX_SHIFT);
		write_kctxt_csr(dd, ctxt, RCV_TID_CTRL, reg);
		if (ctxt == HFI1_CTRL_CTXT)
			write_csr(dd, RCV_VL15, HFI1_CTRL_CTXT);
	}
	if (op & HFI1_RCVCTRL_CTXT_DIS) {
		write_csr(dd, RCV_VL15, 0);
		/*
		 * When receive context is being disabled turn on tail
		 * update with a dummy tail address and then disable
		 * receive context.
		 */
		if (dd->rcvhdrtail_dummy_dma) {
			write_kctxt_csr(dd, ctxt, RCV_HDR_TAIL_ADDR,
					dd->rcvhdrtail_dummy_dma);
			/* Enabling RcvCtxtCtrl.TailUpd is intentional. */
			rcvctrl |= RCV_CTXT_CTRL_TAIL_UPD_SMASK;
		}

		rcvctrl &= ~RCV_CTXT_CTRL_ENABLE_SMASK;
	}
	if (op & HFI1_RCVCTRL_INTRAVAIL_ENB) {
		set_intr_bits(dd, IS_RCVAVAIL_START + rcd->ctxt,
			      IS_RCVAVAIL_START + rcd->ctxt, true);
		rcvctrl |= RCV_CTXT_CTRL_INTR_AVAIL_SMASK;
	}
	if (op & HFI1_RCVCTRL_INTRAVAIL_DIS) {
		set_intr_bits(dd, IS_RCVAVAIL_START + rcd->ctxt,
			      IS_RCVAVAIL_START + rcd->ctxt, false);
		rcvctrl &= ~RCV_CTXT_CTRL_INTR_AVAIL_SMASK;
	}
	if ((op & HFI1_RCVCTRL_TAILUPD_ENB) && hfi1_rcvhdrtail_kvaddr(rcd))
		rcvctrl |= RCV_CTXT_CTRL_TAIL_UPD_SMASK;
	if (op & HFI1_RCVCTRL_TAILUPD_DIS) {
		/* See comment on RcvCtxtCtrl.TailUpd above */
		if (!(op & HFI1_RCVCTRL_CTXT_DIS))
			rcvctrl &= ~RCV_CTXT_CTRL_TAIL_UPD_SMASK;
	}
	if (op & HFI1_RCVCTRL_TIDFLOW_ENB)
		rcvctrl |= RCV_CTXT_CTRL_TID_FLOW_ENABLE_SMASK;
	if (op & HFI1_RCVCTRL_TIDFLOW_DIS)
		rcvctrl &= ~RCV_CTXT_CTRL_TID_FLOW_ENABLE_SMASK;
	if (op & HFI1_RCVCTRL_ONE_PKT_EGR_ENB) {
		/*
		 * In one-packet-per-eager mode, the size comes from
		 * the RcvArray entry.
		 */
		rcvctrl &= ~RCV_CTXT_CTRL_EGR_BUF_SIZE_SMASK;
		rcvctrl |= RCV_CTXT_CTRL_ONE_PACKET_PER_EGR_BUFFER_SMASK;
	}
	if (op & HFI1_RCVCTRL_ONE_PKT_EGR_DIS)
		rcvctrl &= ~RCV_CTXT_CTRL_ONE_PACKET_PER_EGR_BUFFER_SMASK;
	if (op & HFI1_RCVCTRL_NO_RHQ_DROP_ENB)
		rcvctrl |= RCV_CTXT_CTRL_DONT_DROP_RHQ_FULL_SMASK;
	if (op & HFI1_RCVCTRL_NO_RHQ_DROP_DIS)
		rcvctrl &= ~RCV_CTXT_CTRL_DONT_DROP_RHQ_FULL_SMASK;
	if (op & HFI1_RCVCTRL_NO_EGR_DROP_ENB)
		rcvctrl |= RCV_CTXT_CTRL_DONT_DROP_EGR_FULL_SMASK;
	if (op & HFI1_RCVCTRL_NO_EGR_DROP_DIS)
		rcvctrl &= ~RCV_CTXT_CTRL_DONT_DROP_EGR_FULL_SMASK;
	if (op & HFI1_RCVCTRL_URGENT_ENB)
		set_intr_bits(dd, IS_RCVURGENT_START + rcd->ctxt,
			      IS_RCVURGENT_START + rcd->ctxt, true);
	if (op & HFI1_RCVCTRL_URGENT_DIS)
		set_intr_bits(dd, IS_RCVURGENT_START + rcd->ctxt,
			      IS_RCVURGENT_START + rcd->ctxt, false);

	hfi1_cdbg(RCVCTRL, "ctxt %d rcvctrl 0x%llx\n", ctxt, rcvctrl);
	write_kctxt_csr(dd, ctxt, RCV_CTXT_CTRL, rcvctrl);

	/* work around sticky RcvCtxtStatus.BlockedRHQFull */
	if (did_enable &&
	    (rcvctrl & RCV_CTXT_CTRL_DONT_DROP_RHQ_FULL_SMASK)) {
		reg = read_kctxt_csr(dd, ctxt, RCV_CTXT_STATUS);
		if (reg != 0) {
			dd_dev_info(dd, "ctxt %d status %lld (blocked)\n",
				    ctxt, reg);
			read_uctxt_csr(dd, ctxt, RCV_HDR_HEAD);
			write_uctxt_csr(dd, ctxt, RCV_HDR_HEAD, 0x10);
			write_uctxt_csr(dd, ctxt, RCV_HDR_HEAD, 0x00);
			read_uctxt_csr(dd, ctxt, RCV_HDR_HEAD);
			reg = read_kctxt_csr(dd, ctxt, RCV_CTXT_STATUS);
			dd_dev_info(dd, "ctxt %d status %lld (%s blocked)\n",
				    ctxt, reg, reg == 0 ? "not" : "still");
		}
	}

	if (did_enable) {
		/*
		 * The interrupt timeout and count must be set after
		 * the context is enabled to take effect.
		 */
		/* set interrupt timeout */
		write_kctxt_csr(dd, ctxt, RCV_AVAIL_TIME_OUT,
				(u64)rcd->rcvavail_timeout <<
				RCV_AVAIL_TIME_OUT_TIME_OUT_RELOAD_SHIFT);

		/* set RcvHdrHead.Counter, zero RcvHdrHead.Head (again) */
		reg = (u64)rcv_intr_count << RCV_HDR_HEAD_COUNTER_SHIFT;
		write_uctxt_csr(dd, ctxt, RCV_HDR_HEAD, reg);
	}

	if (op & (HFI1_RCVCTRL_TAILUPD_DIS | HFI1_RCVCTRL_CTXT_DIS))
		/*
		 * If the context has been disabled and the Tail Update has
		 * been cleared, set the RCV_HDR_TAIL_ADDR CSR to dummy address
		 * so it doesn't contain an address that is invalid.
		 */
		write_kctxt_csr(dd, ctxt, RCV_HDR_TAIL_ADDR,
				dd->rcvhdrtail_dummy_dma);
}

u32 hfi1_read_cntrs(struct hfi1_devdata *dd, char **namep, u64 **cntrp)
{
	int ret;
	u64 val = 0;

	if (namep) {
		ret = dd->cntrnameslen;
		*namep = dd->cntrnames;
	} else {
		const struct cntr_entry *entry;
		int i, j;

		ret = (dd->ndevcntrs) * sizeof(u64);

		/* Get the start of the block of counters */
		*cntrp = dd->cntrs;

		/*
		 * Now go and fill in each counter in the block.
		 */
		for (i = 0; i < DEV_CNTR_LAST; i++) {
			entry = &dev_cntrs[i];
			hfi1_cdbg(CNTR, "reading %s", entry->name);
			if (entry->flags & CNTR_DISABLED) {
				/* Nothing */
				hfi1_cdbg(CNTR, "\tDisabled\n");
			} else {
				if (entry->flags & CNTR_VL) {
					hfi1_cdbg(CNTR, "\tPer VL\n");
					for (j = 0; j < C_VL_COUNT; j++) {
						val = entry->rw_cntr(entry,
								  dd, j,
								  CNTR_MODE_R,
								  0);
						hfi1_cdbg(
						   CNTR,
						   "\t\tRead 0x%llx for %d\n",
						   val, j);
						dd->cntrs[entry->offset + j] =
									    val;
					}
				} else if (entry->flags & CNTR_SDMA) {
					hfi1_cdbg(CNTR,
						  "\t Per SDMA Engine\n");
					for (j = 0; j < chip_sdma_engines(dd);
					     j++) {
						val =
						entry->rw_cntr(entry, dd, j,
							       CNTR_MODE_R, 0);
						hfi1_cdbg(CNTR,
							  "\t\tRead 0x%llx for %d\n",
							  val, j);
						dd->cntrs[entry->offset + j] =
									val;
					}
				} else {
					val = entry->rw_cntr(entry, dd,
							CNTR_INVALID_VL,
							CNTR_MODE_R, 0);
					dd->cntrs[entry->offset] = val;
					hfi1_cdbg(CNTR, "\tRead 0x%llx", val);
				}
			}
		}
	}
	return ret;
}

/*
 * Used by sysfs to create files for hfi stats to read
 */
u32 hfi1_read_portcntrs(struct hfi1_pportdata *ppd, char **namep, u64 **cntrp)
{
	int ret;
	u64 val = 0;

	if (namep) {
		ret = ppd->dd->portcntrnameslen;
		*namep = ppd->dd->portcntrnames;
	} else {
		const struct cntr_entry *entry;
		int i, j;

		ret = ppd->dd->nportcntrs * sizeof(u64);
		*cntrp = ppd->cntrs;

		for (i = 0; i < PORT_CNTR_LAST; i++) {
			entry = &port_cntrs[i];
			hfi1_cdbg(CNTR, "reading %s", entry->name);
			if (entry->flags & CNTR_DISABLED) {
				/* Nothing */
				hfi1_cdbg(CNTR, "\tDisabled\n");
				continue;
			}

			if (entry->flags & CNTR_VL) {
				hfi1_cdbg(CNTR, "\tPer VL");
				for (j = 0; j < C_VL_COUNT; j++) {
					val = entry->rw_cntr(entry, ppd, j,
							       CNTR_MODE_R,
							       0);
					hfi1_cdbg(
					   CNTR,
					   "\t\tRead 0x%llx for %d",
					   val, j);
					ppd->cntrs[entry->offset + j] = val;
				}
			} else {
				val = entry->rw_cntr(entry, ppd,
						       CNTR_INVALID_VL,
						       CNTR_MODE_R,
						       0);
				ppd->cntrs[entry->offset] = val;
				hfi1_cdbg(CNTR, "\tRead 0x%llx", val);
			}
		}
	}
	return ret;
}

static void free_cntrs(struct hfi1_devdata *dd)
{
	struct hfi1_pportdata *ppd;
	int i;

	if (dd->synth_stats_timer.function)
		del_timer_sync(&dd->synth_stats_timer);
	ppd = (struct hfi1_pportdata *)(dd + 1);
	for (i = 0; i < dd->num_pports; i++, ppd++) {
		kfree(ppd->cntrs);
		kfree(ppd->scntrs);
		free_percpu(ppd->ibport_data.rvp.rc_acks);
		free_percpu(ppd->ibport_data.rvp.rc_qacks);
		free_percpu(ppd->ibport_data.rvp.rc_delayed_comp);
		ppd->cntrs = NULL;
		ppd->scntrs = NULL;
		ppd->ibport_data.rvp.rc_acks = NULL;
		ppd->ibport_data.rvp.rc_qacks = NULL;
		ppd->ibport_data.rvp.rc_delayed_comp = NULL;
	}
	kfree(dd->portcntrnames);
	dd->portcntrnames = NULL;
	kfree(dd->cntrs);
	dd->cntrs = NULL;
	kfree(dd->scntrs);
	dd->scntrs = NULL;
	kfree(dd->cntrnames);
	dd->cntrnames = NULL;
	if (dd->update_cntr_wq) {
		destroy_workqueue(dd->update_cntr_wq);
		dd->update_cntr_wq = NULL;
	}
}

static u64 read_dev_port_cntr(struct hfi1_devdata *dd, struct cntr_entry *entry,
			      u64 *psval, void *context, int vl)
{
	u64 val;
	u64 sval = *psval;

	if (entry->flags & CNTR_DISABLED) {
		dd_dev_err(dd, "Counter %s not enabled", entry->name);
		return 0;
	}

	hfi1_cdbg(CNTR, "cntr: %s vl %d psval 0x%llx", entry->name, vl, *psval);

	val = entry->rw_cntr(entry, context, vl, CNTR_MODE_R, 0);

	/* If its a synthetic counter there is more work we need to do */
	if (entry->flags & CNTR_SYNTH) {
		if (sval == CNTR_MAX) {
			/* No need to read already saturated */
			return CNTR_MAX;
		}

		if (entry->flags & CNTR_32BIT) {
			/* 32bit counters can wrap multiple times */
			u64 upper = sval >> 32;
			u64 lower = (sval << 32) >> 32;

			if (lower > val) { /* hw wrapped */
				if (upper == CNTR_32BIT_MAX)
					val = CNTR_MAX;
				else
					upper++;
			}

			if (val != CNTR_MAX)
				val = (upper << 32) | val;

		} else {
			/* If we rolled we are saturated */
			if ((val < sval) || (val > CNTR_MAX))
				val = CNTR_MAX;
		}
	}

	*psval = val;

	hfi1_cdbg(CNTR, "\tNew val=0x%llx", val);

	return val;
}

static u64 write_dev_port_cntr(struct hfi1_devdata *dd,
			       struct cntr_entry *entry,
			       u64 *psval, void *context, int vl, u64 data)
{
	u64 val;

	if (entry->flags & CNTR_DISABLED) {
		dd_dev_err(dd, "Counter %s not enabled", entry->name);
		return 0;
	}

	hfi1_cdbg(CNTR, "cntr: %s vl %d psval 0x%llx", entry->name, vl, *psval);

	if (entry->flags & CNTR_SYNTH) {
		*psval = data;
		if (entry->flags & CNTR_32BIT) {
			val = entry->rw_cntr(entry, context, vl, CNTR_MODE_W,
					     (data << 32) >> 32);
			val = data; /* return the full 64bit value */
		} else {
			val = entry->rw_cntr(entry, context, vl, CNTR_MODE_W,
					     data);
		}
	} else {
		val = entry->rw_cntr(entry, context, vl, CNTR_MODE_W, data);
	}

	*psval = val;

	hfi1_cdbg(CNTR, "\tNew val=0x%llx", val);

	return val;
}

u64 read_dev_cntr(struct hfi1_devdata *dd, int index, int vl)
{
	struct cntr_entry *entry;
	u64 *sval;

	entry = &dev_cntrs[index];
	sval = dd->scntrs + entry->offset;

	if (vl != CNTR_INVALID_VL)
		sval += vl;

	return read_dev_port_cntr(dd, entry, sval, dd, vl);
}

u64 write_dev_cntr(struct hfi1_devdata *dd, int index, int vl, u64 data)
{
	struct cntr_entry *entry;
	u64 *sval;

	entry = &dev_cntrs[index];
	sval = dd->scntrs + entry->offset;

	if (vl != CNTR_INVALID_VL)
		sval += vl;

	return write_dev_port_cntr(dd, entry, sval, dd, vl, data);
}

u64 read_port_cntr(struct hfi1_pportdata *ppd, int index, int vl)
{
	struct cntr_entry *entry;
	u64 *sval;

	entry = &port_cntrs[index];
	sval = ppd->scntrs + entry->offset;

	if (vl != CNTR_INVALID_VL)
		sval += vl;

	if ((index >= C_RCV_HDR_OVF_FIRST + ppd->dd->num_rcv_contexts) &&
	    (index <= C_RCV_HDR_OVF_LAST)) {
		/* We do not want to bother for disabled contexts */
		return 0;
	}

	return read_dev_port_cntr(ppd->dd, entry, sval, ppd, vl);
}

u64 write_port_cntr(struct hfi1_pportdata *ppd, int index, int vl, u64 data)
{
	struct cntr_entry *entry;
	u64 *sval;

	entry = &port_cntrs[index];
	sval = ppd->scntrs + entry->offset;

	if (vl != CNTR_INVALID_VL)
		sval += vl;

	if ((index >= C_RCV_HDR_OVF_FIRST + ppd->dd->num_rcv_contexts) &&
	    (index <= C_RCV_HDR_OVF_LAST)) {
		/* We do not want to bother for disabled contexts */
		return 0;
	}

	return write_dev_port_cntr(ppd->dd, entry, sval, ppd, vl, data);
}

static void do_update_synth_timer(struct work_struct *work)
{
	u64 cur_tx;
	u64 cur_rx;
	u64 total_flits;
	u8 update = 0;
	int i, j, vl;
	struct hfi1_pportdata *ppd;
	struct cntr_entry *entry;
	struct hfi1_devdata *dd = container_of(work, struct hfi1_devdata,
					       update_cntr_work);

	/*
	 * Rather than keep beating on the CSRs pick a minimal set that we can
	 * check to watch for potential roll over. We can do this by looking at
	 * the number of flits sent/recv. If the total flits exceeds 32bits then
	 * we have to iterate all the counters and update.
	 */
	entry = &dev_cntrs[C_DC_RCV_FLITS];
	cur_rx = entry->rw_cntr(entry, dd, CNTR_INVALID_VL, CNTR_MODE_R, 0);

	entry = &dev_cntrs[C_DC_XMIT_FLITS];
	cur_tx = entry->rw_cntr(entry, dd, CNTR_INVALID_VL, CNTR_MODE_R, 0);

	hfi1_cdbg(
	    CNTR,
	    "[%d] curr tx=0x%llx rx=0x%llx :: last tx=0x%llx rx=0x%llx\n",
	    dd->unit, cur_tx, cur_rx, dd->last_tx, dd->last_rx);

	if ((cur_tx < dd->last_tx) || (cur_rx < dd->last_rx)) {
		/*
		 * May not be strictly necessary to update but it won't hurt and
		 * simplifies the logic here.
		 */
		update = 1;
		hfi1_cdbg(CNTR, "[%d] Tripwire counter rolled, updating",
			  dd->unit);
	} else {
		total_flits = (cur_tx - dd->last_tx) + (cur_rx - dd->last_rx);
		hfi1_cdbg(CNTR,
			  "[%d] total flits 0x%llx limit 0x%llx\n", dd->unit,
			  total_flits, (u64)CNTR_32BIT_MAX);
		if (total_flits >= CNTR_32BIT_MAX) {
			hfi1_cdbg(CNTR, "[%d] 32bit limit hit, updating",
				  dd->unit);
			update = 1;
		}
	}

	if (update) {
		hfi1_cdbg(CNTR, "[%d] Updating dd and ppd counters", dd->unit);
		for (i = 0; i < DEV_CNTR_LAST; i++) {
			entry = &dev_cntrs[i];
			if (entry->flags & CNTR_VL) {
				for (vl = 0; vl < C_VL_COUNT; vl++)
					read_dev_cntr(dd, i, vl);
			} else {
				read_dev_cntr(dd, i, CNTR_INVALID_VL);
			}
		}
		ppd = (struct hfi1_pportdata *)(dd + 1);
		for (i = 0; i < dd->num_pports; i++, ppd++) {
			for (j = 0; j < PORT_CNTR_LAST; j++) {
				entry = &port_cntrs[j];
				if (entry->flags & CNTR_VL) {
					for (vl = 0; vl < C_VL_COUNT; vl++)
						read_port_cntr(ppd, j, vl);
				} else {
					read_port_cntr(ppd, j, CNTR_INVALID_VL);
				}
			}
		}

		/*
		 * We want the value in the register. The goal is to keep track
		 * of the number of "ticks" not the counter value. In other
		 * words if the register rolls we want to notice it and go ahead
		 * and force an update.
		 */
		entry = &dev_cntrs[C_DC_XMIT_FLITS];
		dd->last_tx = entry->rw_cntr(entry, dd, CNTR_INVALID_VL,
						CNTR_MODE_R, 0);

		entry = &dev_cntrs[C_DC_RCV_FLITS];
		dd->last_rx = entry->rw_cntr(entry, dd, CNTR_INVALID_VL,
						CNTR_MODE_R, 0);

		hfi1_cdbg(CNTR, "[%d] setting last tx/rx to 0x%llx 0x%llx",
			  dd->unit, dd->last_tx, dd->last_rx);

	} else {
		hfi1_cdbg(CNTR, "[%d] No update necessary", dd->unit);
	}
}

static void update_synth_timer(struct timer_list *t)
{
	struct hfi1_devdata *dd = from_timer(dd, t, synth_stats_timer);

	queue_work(dd->update_cntr_wq, &dd->update_cntr_work);
	mod_timer(&dd->synth_stats_timer, jiffies + HZ * SYNTH_CNT_TIME);
}

#define C_MAX_NAME 16 /* 15 chars + one for /0 */
static int init_cntrs(struct hfi1_devdata *dd)
{
	int i, rcv_ctxts, j;
	size_t sz;
	char *p;
	char name[C_MAX_NAME];
	struct hfi1_pportdata *ppd;
	const char *bit_type_32 = ",32";
	const int bit_type_32_sz = strlen(bit_type_32);
	u32 sdma_engines = chip_sdma_engines(dd);

	/* set up the stats timer; the add_timer is done at the end */
	timer_setup(&dd->synth_stats_timer, update_synth_timer, 0);

	/***********************/
	/* per device counters */
	/***********************/

	/* size names and determine how many we have*/
	dd->ndevcntrs = 0;
	sz = 0;

	for (i = 0; i < DEV_CNTR_LAST; i++) {
		if (dev_cntrs[i].flags & CNTR_DISABLED) {
			hfi1_dbg_early("\tSkipping %s\n", dev_cntrs[i].name);
			continue;
		}

		if (dev_cntrs[i].flags & CNTR_VL) {
			dev_cntrs[i].offset = dd->ndevcntrs;
			for (j = 0; j < C_VL_COUNT; j++) {
				snprintf(name, C_MAX_NAME, "%s%d",
					 dev_cntrs[i].name, vl_from_idx(j));
				sz += strlen(name);
				/* Add ",32" for 32-bit counters */
				if (dev_cntrs[i].flags & CNTR_32BIT)
					sz += bit_type_32_sz;
				sz++;
				dd->ndevcntrs++;
			}
		} else if (dev_cntrs[i].flags & CNTR_SDMA) {
			dev_cntrs[i].offset = dd->ndevcntrs;
			for (j = 0; j < sdma_engines; j++) {
				snprintf(name, C_MAX_NAME, "%s%d",
					 dev_cntrs[i].name, j);
				sz += strlen(name);
				/* Add ",32" for 32-bit counters */
				if (dev_cntrs[i].flags & CNTR_32BIT)
					sz += bit_type_32_sz;
				sz++;
				dd->ndevcntrs++;
			}
		} else {
			/* +1 for newline. */
			sz += strlen(dev_cntrs[i].name) + 1;
			/* Add ",32" for 32-bit counters */
			if (dev_cntrs[i].flags & CNTR_32BIT)
				sz += bit_type_32_sz;
			dev_cntrs[i].offset = dd->ndevcntrs;
			dd->ndevcntrs++;
		}
	}

	/* allocate space for the counter values */
	dd->cntrs = kcalloc(dd->ndevcntrs + num_driver_cntrs, sizeof(u64),
			    GFP_KERNEL);
	if (!dd->cntrs)
		goto bail;

	dd->scntrs = kcalloc(dd->ndevcntrs, sizeof(u64), GFP_KERNEL);
	if (!dd->scntrs)
		goto bail;

	/* allocate space for the counter names */
	dd->cntrnameslen = sz;
	dd->cntrnames = kmalloc(sz, GFP_KERNEL);
	if (!dd->cntrnames)
		goto bail;

	/* fill in the names */
	for (p = dd->cntrnames, i = 0; i < DEV_CNTR_LAST; i++) {
		if (dev_cntrs[i].flags & CNTR_DISABLED) {
			/* Nothing */
		} else if (dev_cntrs[i].flags & CNTR_VL) {
			for (j = 0; j < C_VL_COUNT; j++) {
				snprintf(name, C_MAX_NAME, "%s%d",
					 dev_cntrs[i].name,
					 vl_from_idx(j));
				memcpy(p, name, strlen(name));
				p += strlen(name);

				/* Counter is 32 bits */
				if (dev_cntrs[i].flags & CNTR_32BIT) {
					memcpy(p, bit_type_32, bit_type_32_sz);
					p += bit_type_32_sz;
				}

				*p++ = '\n';
			}
		} else if (dev_cntrs[i].flags & CNTR_SDMA) {
			for (j = 0; j < sdma_engines; j++) {
				snprintf(name, C_MAX_NAME, "%s%d",
					 dev_cntrs[i].name, j);
				memcpy(p, name, strlen(name));
				p += strlen(name);

				/* Counter is 32 bits */
				if (dev_cntrs[i].flags & CNTR_32BIT) {
					memcpy(p, bit_type_32, bit_type_32_sz);
					p += bit_type_32_sz;
				}

				*p++ = '\n';
			}
		} else {
			memcpy(p, dev_cntrs[i].name, strlen(dev_cntrs[i].name));
			p += strlen(dev_cntrs[i].name);

			/* Counter is 32 bits */
			if (dev_cntrs[i].flags & CNTR_32BIT) {
				memcpy(p, bit_type_32, bit_type_32_sz);
				p += bit_type_32_sz;
			}

			*p++ = '\n';
		}
	}

	/*********************/
	/* per port counters */
	/*********************/

	/*
	 * Go through the counters for the overflows and disable the ones we
	 * don't need. This varies based on platform so we need to do it
	 * dynamically here.
	 */
	rcv_ctxts = dd->num_rcv_contexts;
	for (i = C_RCV_HDR_OVF_FIRST + rcv_ctxts;
	     i <= C_RCV_HDR_OVF_LAST; i++) {
		port_cntrs[i].flags |= CNTR_DISABLED;
	}

	/* size port counter names and determine how many we have*/
	sz = 0;
	dd->nportcntrs = 0;
	for (i = 0; i < PORT_CNTR_LAST; i++) {
		if (port_cntrs[i].flags & CNTR_DISABLED) {
			hfi1_dbg_early("\tSkipping %s\n", port_cntrs[i].name);
			continue;
		}

		if (port_cntrs[i].flags & CNTR_VL) {
			port_cntrs[i].offset = dd->nportcntrs;
			for (j = 0; j < C_VL_COUNT; j++) {
				snprintf(name, C_MAX_NAME, "%s%d",
					 port_cntrs[i].name, vl_from_idx(j));
				sz += strlen(name);
				/* Add ",32" for 32-bit counters */
				if (port_cntrs[i].flags & CNTR_32BIT)
					sz += bit_type_32_sz;
				sz++;
				dd->nportcntrs++;
			}
		} else {
			/* +1 for newline */
			sz += strlen(port_cntrs[i].name) + 1;
			/* Add ",32" for 32-bit counters */
			if (port_cntrs[i].flags & CNTR_32BIT)
				sz += bit_type_32_sz;
			port_cntrs[i].offset = dd->nportcntrs;
			dd->nportcntrs++;
		}
	}

	/* allocate space for the counter names */
	dd->portcntrnameslen = sz;
	dd->portcntrnames = kmalloc(sz, GFP_KERNEL);
	if (!dd->portcntrnames)
		goto bail;

	/* fill in port cntr names */
	for (p = dd->portcntrnames, i = 0; i < PORT_CNTR_LAST; i++) {
		if (port_cntrs[i].flags & CNTR_DISABLED)
			continue;

		if (port_cntrs[i].flags & CNTR_VL) {
			for (j = 0; j < C_VL_COUNT; j++) {
				snprintf(name, C_MAX_NAME, "%s%d",
					 port_cntrs[i].name, vl_from_idx(j));
				memcpy(p, name, strlen(name));
				p += strlen(name);

				/* Counter is 32 bits */
				if (port_cntrs[i].flags & CNTR_32BIT) {
					memcpy(p, bit_type_32, bit_type_32_sz);
					p += bit_type_32_sz;
				}

				*p++ = '\n';
			}
		} else {
			memcpy(p, port_cntrs[i].name,
			       strlen(port_cntrs[i].name));
			p += strlen(port_cntrs[i].name);

			/* Counter is 32 bits */
			if (port_cntrs[i].flags & CNTR_32BIT) {
				memcpy(p, bit_type_32, bit_type_32_sz);
				p += bit_type_32_sz;
			}

			*p++ = '\n';
		}
	}

	/* allocate per port storage for counter values */
	ppd = (struct hfi1_pportdata *)(dd + 1);
	for (i = 0; i < dd->num_pports; i++, ppd++) {
		ppd->cntrs = kcalloc(dd->nportcntrs, sizeof(u64), GFP_KERNEL);
		if (!ppd->cntrs)
			goto bail;

		ppd->scntrs = kcalloc(dd->nportcntrs, sizeof(u64), GFP_KERNEL);
		if (!ppd->scntrs)
			goto bail;
	}

	/* CPU counters need to be allocated and zeroed */
	if (init_cpu_counters(dd))
		goto bail;

	dd->update_cntr_wq = alloc_ordered_workqueue("hfi1_update_cntr_%d",
						     WQ_MEM_RECLAIM, dd->unit);
	if (!dd->update_cntr_wq)
		goto bail;

	INIT_WORK(&dd->update_cntr_work, do_update_synth_timer);

	mod_timer(&dd->synth_stats_timer, jiffies + HZ * SYNTH_CNT_TIME);
	return 0;
bail:
	free_cntrs(dd);
	return -ENOMEM;
}

static u32 chip_to_opa_lstate(struct hfi1_devdata *dd, u32 chip_lstate)
{
	switch (chip_lstate) {
	default:
		dd_dev_err(dd,
			   "Unknown logical state 0x%x, reporting IB_PORT_DOWN\n",
			   chip_lstate);
		/* fall through */
	case LSTATE_DOWN:
		return IB_PORT_DOWN;
	case LSTATE_INIT:
		return IB_PORT_INIT;
	case LSTATE_ARMED:
		return IB_PORT_ARMED;
	case LSTATE_ACTIVE:
		return IB_PORT_ACTIVE;
	}
}

u32 chip_to_opa_pstate(struct hfi1_devdata *dd, u32 chip_pstate)
{
	/* look at the HFI meta-states only */
	switch (chip_pstate & 0xf0) {
	default:
		dd_dev_err(dd, "Unexpected chip physical state of 0x%x\n",
			   chip_pstate);
		/* fall through */
	case PLS_DISABLED:
		return IB_PORTPHYSSTATE_DISABLED;
	case PLS_OFFLINE:
		return OPA_PORTPHYSSTATE_OFFLINE;
	case PLS_POLLING:
		return IB_PORTPHYSSTATE_POLLING;
	case PLS_CONFIGPHY:
		return IB_PORTPHYSSTATE_TRAINING;
	case PLS_LINKUP:
		return IB_PORTPHYSSTATE_LINKUP;
	case PLS_PHYTEST:
		return IB_PORTPHYSSTATE_PHY_TEST;
	}
}

/* return the OPA port logical state name */
const char *opa_lstate_name(u32 lstate)
{
	static const char * const port_logical_names[] = {
		"PORT_NOP",
		"PORT_DOWN",
		"PORT_INIT",
		"PORT_ARMED",
		"PORT_ACTIVE",
		"PORT_ACTIVE_DEFER",
	};
	if (lstate < ARRAY_SIZE(port_logical_names))
		return port_logical_names[lstate];
	return "unknown";
}

/* return the OPA port physical state name */
const char *opa_pstate_name(u32 pstate)
{
	static const char * const port_physical_names[] = {
		"PHYS_NOP",
		"reserved1",
		"PHYS_POLL",
		"PHYS_DISABLED",
		"PHYS_TRAINING",
		"PHYS_LINKUP",
		"PHYS_LINK_ERR_RECOVER",
		"PHYS_PHY_TEST",
		"reserved8",
		"PHYS_OFFLINE",
		"PHYS_GANGED",
		"PHYS_TEST",
	};
	if (pstate < ARRAY_SIZE(port_physical_names))
		return port_physical_names[pstate];
	return "unknown";
}

/**
 * update_statusp - Update userspace status flag
 * @ppd: Port data structure
 * @state: port state information
 *
 * Actual port status is determined by the host_link_state value
 * in the ppd.
 *
 * host_link_state MUST be updated before updating the user space
 * statusp.
 */
static void update_statusp(struct hfi1_pportdata *ppd, u32 state)
{
	/*
	 * Set port status flags in the page mapped into userspace
	 * memory. Do it here to ensure a reliable state - this is
	 * the only function called by all state handling code.
	 * Always set the flags due to the fact that the cache value
	 * might have been changed explicitly outside of this
	 * function.
	 */
	if (ppd->statusp) {
		switch (state) {
		case IB_PORT_DOWN:
		case IB_PORT_INIT:
			*ppd->statusp &= ~(HFI1_STATUS_IB_CONF |
					   HFI1_STATUS_IB_READY);
			break;
		case IB_PORT_ARMED:
			*ppd->statusp |= HFI1_STATUS_IB_CONF;
			break;
		case IB_PORT_ACTIVE:
			*ppd->statusp |= HFI1_STATUS_IB_READY;
			break;
		}
	}
	dd_dev_info(ppd->dd, "logical state changed to %s (0x%x)\n",
		    opa_lstate_name(state), state);
}

/**
 * wait_logical_linkstate - wait for an IB link state change to occur
 * @ppd: port device
 * @state: the state to wait for
 * @msecs: the number of milliseconds to wait
 *
 * Wait up to msecs milliseconds for IB link state change to occur.
 * For now, take the easy polling route.
 * Returns 0 if state reached, otherwise -ETIMEDOUT.
 */
static int wait_logical_linkstate(struct hfi1_pportdata *ppd, u32 state,
				  int msecs)
{
	unsigned long timeout;
	u32 new_state;

	timeout = jiffies + msecs_to_jiffies(msecs);
	while (1) {
		new_state = chip_to_opa_lstate(ppd->dd,
					       read_logical_state(ppd->dd));
		if (new_state == state)
			break;
		if (time_after(jiffies, timeout)) {
			dd_dev_err(ppd->dd,
				   "timeout waiting for link state 0x%x\n",
				   state);
			return -ETIMEDOUT;
		}
		msleep(20);
	}

	return 0;
}

static void log_state_transition(struct hfi1_pportdata *ppd, u32 state)
{
	u32 ib_pstate = chip_to_opa_pstate(ppd->dd, state);

	dd_dev_info(ppd->dd,
		    "physical state changed to %s (0x%x), phy 0x%x\n",
		    opa_pstate_name(ib_pstate), ib_pstate, state);
}

/*
 * Read the physical hardware link state and check if it matches host
 * drivers anticipated state.
 */
static void log_physical_state(struct hfi1_pportdata *ppd, u32 state)
{
	u32 read_state = read_physical_state(ppd->dd);

	if (read_state == state) {
		log_state_transition(ppd, state);
	} else {
		dd_dev_err(ppd->dd,
			   "anticipated phy link state 0x%x, read 0x%x\n",
			   state, read_state);
	}
}

/*
 * wait_physical_linkstate - wait for an physical link state change to occur
 * @ppd: port device
 * @state: the state to wait for
 * @msecs: the number of milliseconds to wait
 *
 * Wait up to msecs milliseconds for physical link state change to occur.
 * Returns 0 if state reached, otherwise -ETIMEDOUT.
 */
static int wait_physical_linkstate(struct hfi1_pportdata *ppd, u32 state,
				   int msecs)
{
	u32 read_state;
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(msecs);
	while (1) {
		read_state = read_physical_state(ppd->dd);
		if (read_state == state)
			break;
		if (time_after(jiffies, timeout)) {
			dd_dev_err(ppd->dd,
				   "timeout waiting for phy link state 0x%x\n",
				   state);
			return -ETIMEDOUT;
		}
		usleep_range(1950, 2050); /* sleep 2ms-ish */
	}

	log_state_transition(ppd, state);
	return 0;
}

/*
 * wait_phys_link_offline_quiet_substates - wait for any offline substate
 * @ppd: port device
 * @msecs: the number of milliseconds to wait
 *
 * Wait up to msecs milliseconds for any offline physical link
 * state change to occur.
 * Returns 0 if at least one state is reached, otherwise -ETIMEDOUT.
 */
static int wait_phys_link_offline_substates(struct hfi1_pportdata *ppd,
					    int msecs)
{
	u32 read_state;
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(msecs);
	while (1) {
		read_state = read_physical_state(ppd->dd);
		if ((read_state & 0xF0) == PLS_OFFLINE)
			break;
		if (time_after(jiffies, timeout)) {
			dd_dev_err(ppd->dd,
				   "timeout waiting for phy link offline.quiet substates. Read state 0x%x, %dms\n",
				   read_state, msecs);
			return -ETIMEDOUT;
		}
		usleep_range(1950, 2050); /* sleep 2ms-ish */
	}

	log_state_transition(ppd, read_state);
	return read_state;
}

/*
 * wait_phys_link_out_of_offline - wait for any out of offline state
 * @ppd: port device
 * @msecs: the number of milliseconds to wait
 *
 * Wait up to msecs milliseconds for any out of offline physical link
 * state change to occur.
 * Returns 0 if at least one state is reached, otherwise -ETIMEDOUT.
 */
static int wait_phys_link_out_of_offline(struct hfi1_pportdata *ppd,
					 int msecs)
{
	u32 read_state;
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(msecs);
	while (1) {
		read_state = read_physical_state(ppd->dd);
		if ((read_state & 0xF0) != PLS_OFFLINE)
			break;
		if (time_after(jiffies, timeout)) {
			dd_dev_err(ppd->dd,
				   "timeout waiting for phy link out of offline. Read state 0x%x, %dms\n",
				   read_state, msecs);
			return -ETIMEDOUT;
		}
		usleep_range(1950, 2050); /* sleep 2ms-ish */
	}

	log_state_transition(ppd, read_state);
	return read_state;
}

#define CLEAR_STATIC_RATE_CONTROL_SMASK(r) \
(r &= ~SEND_CTXT_CHECK_ENABLE_DISALLOW_PBC_STATIC_RATE_CONTROL_SMASK)

#define SET_STATIC_RATE_CONTROL_SMASK(r) \
(r |= SEND_CTXT_CHECK_ENABLE_DISALLOW_PBC_STATIC_RATE_CONTROL_SMASK)

void hfi1_init_ctxt(struct send_context *sc)
{
	if (sc) {
		struct hfi1_devdata *dd = sc->dd;
		u64 reg;
		u8 set = (sc->type == SC_USER ?
			  HFI1_CAP_IS_USET(STATIC_RATE_CTRL) :
			  HFI1_CAP_IS_KSET(STATIC_RATE_CTRL));
		reg = read_kctxt_csr(dd, sc->hw_context,
				     SEND_CTXT_CHECK_ENABLE);
		if (set)
			CLEAR_STATIC_RATE_CONTROL_SMASK(reg);
		else
			SET_STATIC_RATE_CONTROL_SMASK(reg);
		write_kctxt_csr(dd, sc->hw_context,
				SEND_CTXT_CHECK_ENABLE, reg);
	}
}

int hfi1_tempsense_rd(struct hfi1_devdata *dd, struct hfi1_temp *temp)
{
	int ret = 0;
	u64 reg;

	if (dd->icode != ICODE_RTL_SILICON) {
		if (HFI1_CAP_IS_KSET(PRINT_UNIMPL))
			dd_dev_info(dd, "%s: tempsense not supported by HW\n",
				    __func__);
		return -EINVAL;
	}
	reg = read_csr(dd, ASIC_STS_THERM);
	temp->curr = ((reg >> ASIC_STS_THERM_CURR_TEMP_SHIFT) &
		      ASIC_STS_THERM_CURR_TEMP_MASK);
	temp->lo_lim = ((reg >> ASIC_STS_THERM_LO_TEMP_SHIFT) &
			ASIC_STS_THERM_LO_TEMP_MASK);
	temp->hi_lim = ((reg >> ASIC_STS_THERM_HI_TEMP_SHIFT) &
			ASIC_STS_THERM_HI_TEMP_MASK);
	temp->crit_lim = ((reg >> ASIC_STS_THERM_CRIT_TEMP_SHIFT) &
			  ASIC_STS_THERM_CRIT_TEMP_MASK);
	/* triggers is a 3-bit value - 1 bit per trigger. */
	temp->triggers = (u8)((reg >> ASIC_STS_THERM_LOW_SHIFT) & 0x7);

	return ret;
}

/* ========================================================================= */

/**
 * read_mod_write() - Calculate the IRQ register index and set/clear the bits
 * @dd: valid devdata
 * @src: IRQ source to determine register index from
 * @bits: the bits to set or clear
 * @set: true == set the bits, false == clear the bits
 *
 */
static void read_mod_write(struct hfi1_devdata *dd, u16 src, u64 bits,
			   bool set)
{
	u64 reg;
	u16 idx = src / BITS_PER_REGISTER;

	spin_lock(&dd->irq_src_lock);
	reg = read_csr(dd, CCE_INT_MASK + (8 * idx));
	if (set)
		reg |= bits;
	else
		reg &= ~bits;
	write_csr(dd, CCE_INT_MASK + (8 * idx), reg);
	spin_unlock(&dd->irq_src_lock);
}

/**
 * set_intr_bits() - Enable/disable a range (one or more) IRQ sources
 * @dd: valid devdata
 * @first: first IRQ source to set/clear
 * @last: last IRQ source (inclusive) to set/clear
 * @set: true == set the bits, false == clear the bits
 *
 * If first == last, set the exact source.
 */
int set_intr_bits(struct hfi1_devdata *dd, u16 first, u16 last, bool set)
{
	u64 bits = 0;
	u64 bit;
	u16 src;

	if (first > NUM_INTERRUPT_SOURCES || last > NUM_INTERRUPT_SOURCES)
		return -EINVAL;

	if (last < first)
		return -ERANGE;

	for (src = first; src <= last; src++) {
		bit = src % BITS_PER_REGISTER;
		/* wrapped to next register? */
		if (!bit && bits) {
			read_mod_write(dd, src - 1, bits, set);
			bits = 0;
		}
		bits |= BIT_ULL(bit);
	}
	read_mod_write(dd, last, bits, set);

	return 0;
}

/*
 * Clear all interrupt sources on the chip.
 */
void clear_all_interrupts(struct hfi1_devdata *dd)
{
	int i;

	for (i = 0; i < CCE_NUM_INT_CSRS; i++)
		write_csr(dd, CCE_INT_CLEAR + (8 * i), ~(u64)0);

	write_csr(dd, CCE_ERR_CLEAR, ~(u64)0);
	write_csr(dd, MISC_ERR_CLEAR, ~(u64)0);
	write_csr(dd, RCV_ERR_CLEAR, ~(u64)0);
	write_csr(dd, SEND_ERR_CLEAR, ~(u64)0);
	write_csr(dd, SEND_PIO_ERR_CLEAR, ~(u64)0);
	write_csr(dd, SEND_DMA_ERR_CLEAR, ~(u64)0);
	write_csr(dd, SEND_EGRESS_ERR_CLEAR, ~(u64)0);
	for (i = 0; i < chip_send_contexts(dd); i++)
		write_kctxt_csr(dd, i, SEND_CTXT_ERR_CLEAR, ~(u64)0);
	for (i = 0; i < chip_sdma_engines(dd); i++)
		write_kctxt_csr(dd, i, SEND_DMA_ENG_ERR_CLEAR, ~(u64)0);

	write_csr(dd, DCC_ERR_FLG_CLR, ~(u64)0);
	write_csr(dd, DC_LCB_ERR_CLR, ~(u64)0);
	write_csr(dd, DC_DC8051_ERR_CLR, ~(u64)0);
}

/*
 * Remap the interrupt source from the general handler to the given MSI-X
 * interrupt.
 */
void remap_intr(struct hfi1_devdata *dd, int isrc, int msix_intr)
{
	u64 reg;
	int m, n;

	/* clear from the handled mask of the general interrupt */
	m = isrc / 64;
	n = isrc % 64;
	if (likely(m < CCE_NUM_INT_CSRS)) {
		dd->gi_mask[m] &= ~((u64)1 << n);
	} else {
		dd_dev_err(dd, "remap interrupt err\n");
		return;
	}

	/* direct the chip source to the given MSI-X interrupt */
	m = isrc / 8;
	n = isrc % 8;
	reg = read_csr(dd, CCE_INT_MAP + (8 * m));
	reg &= ~((u64)0xff << (8 * n));
	reg |= ((u64)msix_intr & 0xff) << (8 * n);
	write_csr(dd, CCE_INT_MAP + (8 * m), reg);
}

void remap_sdma_interrupts(struct hfi1_devdata *dd, int engine, int msix_intr)
{
	/*
	 * SDMA engine interrupt sources grouped by type, rather than
	 * engine.  Per-engine interrupts are as follows:
	 *	SDMA
	 *	SDMAProgress
	 *	SDMAIdle
	 */
	remap_intr(dd, IS_SDMA_START + engine, msix_intr);
	remap_intr(dd, IS_SDMA_PROGRESS_START + engine, msix_intr);
	remap_intr(dd, IS_SDMA_IDLE_START + engine, msix_intr);
}

/*
 * Set the general handler to accept all interrupts, remap all
 * chip interrupts back to MSI-X 0.
 */
void reset_interrupts(struct hfi1_devdata *dd)
{
	int i;

	/* all interrupts handled by the general handler */
	for (i = 0; i < CCE_NUM_INT_CSRS; i++)
		dd->gi_mask[i] = ~(u64)0;

	/* all chip interrupts map to MSI-X 0 */
	for (i = 0; i < CCE_NUM_INT_MAP_CSRS; i++)
		write_csr(dd, CCE_INT_MAP + (8 * i), 0);
}

/**
 * set_up_interrupts() - Initialize the IRQ resources and state
 * @dd: valid devdata
 *
 */
static int set_up_interrupts(struct hfi1_devdata *dd)
{
	int ret;

	/* mask all interrupts */
	set_intr_bits(dd, IS_FIRST_SOURCE, IS_LAST_SOURCE, false);

	/* clear all pending interrupts */
	clear_all_interrupts(dd);

	/* reset general handler mask, chip MSI-X mappings */
	reset_interrupts(dd);

	/* ask for MSI-X interrupts */
	ret = msix_initialize(dd);
	if (ret)
		return ret;

	ret = msix_request_irqs(dd);
	if (ret)
		msix_clean_up_interrupts(dd);

	return ret;
}

/*
 * Set up context values in dd.  Sets:
 *
 *	num_rcv_contexts - number of contexts being used
 *	n_krcv_queues - number of kernel contexts
 *	first_dyn_alloc_ctxt - first dynamically allocated context
 *                             in array of contexts
 *	freectxts  - number of free user contexts
 *	num_send_contexts - number of PIO send contexts being used
 *	num_vnic_contexts - number of contexts reserved for VNIC
 */
static int set_up_context_variables(struct hfi1_devdata *dd)
{
	unsigned long num_kernel_contexts;
	u16 num_vnic_contexts = HFI1_NUM_VNIC_CTXT;
	int total_contexts;
	int ret;
	unsigned ngroups;
	int rmt_count;
	int user_rmt_reduced;
	u32 n_usr_ctxts;
	u32 send_contexts = chip_send_contexts(dd);
	u32 rcv_contexts = chip_rcv_contexts(dd);

	/*
	 * Kernel receive contexts:
	 * - Context 0 - control context (VL15/multicast/error)
	 * - Context 1 - first kernel context
	 * - Context 2 - second kernel context
	 * ...
	 */
	if (n_krcvqs)
		/*
		 * n_krcvqs is the sum of module parameter kernel receive
		 * contexts, krcvqs[].  It does not include the control
		 * context, so add that.
		 */
		num_kernel_contexts = n_krcvqs + 1;
	else
		num_kernel_contexts = DEFAULT_KRCVQS + 1;
	/*
	 * Every kernel receive context needs an ACK send context.
	 * one send context is allocated for each VL{0-7} and VL15
	 */
	if (num_kernel_contexts > (send_contexts - num_vls - 1)) {
		dd_dev_err(dd,
			   "Reducing # kernel rcv contexts to: %d, from %lu\n",
			   send_contexts - num_vls - 1,
			   num_kernel_contexts);
		num_kernel_contexts = send_contexts - num_vls - 1;
	}

	/* Accommodate VNIC contexts if possible */
	if ((num_kernel_contexts + num_vnic_contexts) > rcv_contexts) {
		dd_dev_err(dd, "No receive contexts available for VNIC\n");
		num_vnic_contexts = 0;
	}
	total_contexts = num_kernel_contexts + num_vnic_contexts;

	/*
	 * User contexts:
	 *	- default to 1 user context per real (non-HT) CPU core if
	 *	  num_user_contexts is negative
	 */
	if (num_user_contexts < 0)
		n_usr_ctxts = cpumask_weight(&node_affinity.real_cpu_mask);
	else
		n_usr_ctxts = num_user_contexts;
	/*
	 * Adjust the counts given a global max.
	 */
	if (total_contexts + n_usr_ctxts > rcv_contexts) {
		dd_dev_err(dd,
			   "Reducing # user receive contexts to: %d, from %u\n",
			   rcv_contexts - total_contexts,
			   n_usr_ctxts);
		/* recalculate */
		n_usr_ctxts = rcv_contexts - total_contexts;
	}

	/*
	 * The RMT entries are currently allocated as shown below:
	 * 1. QOS (0 to 128 entries);
	 * 2. FECN (num_kernel_context - 1 + num_user_contexts +
	 *    num_vnic_contexts);
	 * 3. VNIC (num_vnic_contexts).
	 * It should be noted that FECN oversubscribe num_vnic_contexts
	 * entries of RMT because both VNIC and PSM could allocate any receive
	 * context between dd->first_dyn_alloc_text and dd->num_rcv_contexts,
	 * and PSM FECN must reserve an RMT entry for each possible PSM receive
	 * context.
	 */
	rmt_count = qos_rmt_entries(dd, NULL, NULL) + (num_vnic_contexts * 2);
	if (HFI1_CAP_IS_KSET(TID_RDMA))
		rmt_count += num_kernel_contexts - 1;
	if (rmt_count + n_usr_ctxts > NUM_MAP_ENTRIES) {
		user_rmt_reduced = NUM_MAP_ENTRIES - rmt_count;
		dd_dev_err(dd,
			   "RMT size is reducing the number of user receive contexts from %u to %d\n",
			   n_usr_ctxts,
			   user_rmt_reduced);
		/* recalculate */
		n_usr_ctxts = user_rmt_reduced;
	}

	total_contexts += n_usr_ctxts;

	/* the first N are kernel contexts, the rest are user/vnic contexts */
	dd->num_rcv_contexts = total_contexts;
	dd->n_krcv_queues = num_kernel_contexts;
	dd->first_dyn_alloc_ctxt = num_kernel_contexts;
	dd->num_vnic_contexts = num_vnic_contexts;
	dd->num_user_contexts = n_usr_ctxts;
	dd->freectxts = n_usr_ctxts;
	dd_dev_info(dd,
		    "rcv contexts: chip %d, used %d (kernel %d, vnic %u, user %u)\n",
		    rcv_contexts,
		    (int)dd->num_rcv_contexts,
		    (int)dd->n_krcv_queues,
		    dd->num_vnic_contexts,
		    dd->num_user_contexts);

	/*
	 * Receive array allocation:
	 *   All RcvArray entries are divided into groups of 8. This
	 *   is required by the hardware and will speed up writes to
	 *   consecutive entries by using write-combining of the entire
	 *   cacheline.
	 *
	 *   The number of groups are evenly divided among all contexts.
	 *   any left over groups will be given to the first N user
	 *   contexts.
	 */
	dd->rcv_entries.group_size = RCV_INCREMENT;
	ngroups = chip_rcv_array_count(dd) / dd->rcv_entries.group_size;
	dd->rcv_entries.ngroups = ngroups / dd->num_rcv_contexts;
	dd->rcv_entries.nctxt_extra = ngroups -
		(dd->num_rcv_contexts * dd->rcv_entries.ngroups);
	dd_dev_info(dd, "RcvArray groups %u, ctxts extra %u\n",
		    dd->rcv_entries.ngroups,
		    dd->rcv_entries.nctxt_extra);
	if (dd->rcv_entries.ngroups * dd->rcv_entries.group_size >
	    MAX_EAGER_ENTRIES * 2) {
		dd->rcv_entries.ngroups = (MAX_EAGER_ENTRIES * 2) /
			dd->rcv_entries.group_size;
		dd_dev_info(dd,
			    "RcvArray group count too high, change to %u\n",
			    dd->rcv_entries.ngroups);
		dd->rcv_entries.nctxt_extra = 0;
	}
	/*
	 * PIO send contexts
	 */
	ret = init_sc_pools_and_sizes(dd);
	if (ret >= 0) {	/* success */
		dd->num_send_contexts = ret;
		dd_dev_info(
			dd,
			"send contexts: chip %d, used %d (kernel %d, ack %d, user %d, vl15 %d)\n",
			send_contexts,
			dd->num_send_contexts,
			dd->sc_sizes[SC_KERNEL].count,
			dd->sc_sizes[SC_ACK].count,
			dd->sc_sizes[SC_USER].count,
			dd->sc_sizes[SC_VL15].count);
		ret = 0;	/* success */
	}

	return ret;
}

/*
 * Set the device/port partition key table. The MAD code
 * will ensure that, at least, the partial management
 * partition key is present in the table.
 */
static void set_partition_keys(struct hfi1_pportdata *ppd)
{
	struct hfi1_devdata *dd = ppd->dd;
	u64 reg = 0;
	int i;

	dd_dev_info(dd, "Setting partition keys\n");
	for (i = 0; i < hfi1_get_npkeys(dd); i++) {
		reg |= (ppd->pkeys[i] &
			RCV_PARTITION_KEY_PARTITION_KEY_A_MASK) <<
			((i % 4) *
			 RCV_PARTITION_KEY_PARTITION_KEY_B_SHIFT);
		/* Each register holds 4 PKey values. */
		if ((i % 4) == 3) {
			write_csr(dd, RCV_PARTITION_KEY +
				  ((i - 3) * 2), reg);
			reg = 0;
		}
	}

	/* Always enable HW pkeys check when pkeys table is set */
	add_rcvctrl(dd, RCV_CTRL_RCV_PARTITION_KEY_ENABLE_SMASK);
}

/*
 * These CSRs and memories are uninitialized on reset and must be
 * written before reading to set the ECC/parity bits.
 *
 * NOTE: All user context CSRs that are not mmaped write-only
 * (e.g. the TID flows) must be initialized even if the driver never
 * reads them.
 */
static void write_uninitialized_csrs_and_memories(struct hfi1_devdata *dd)
{
	int i, j;

	/* CceIntMap */
	for (i = 0; i < CCE_NUM_INT_MAP_CSRS; i++)
		write_csr(dd, CCE_INT_MAP + (8 * i), 0);

	/* SendCtxtCreditReturnAddr */
	for (i = 0; i < chip_send_contexts(dd); i++)
		write_kctxt_csr(dd, i, SEND_CTXT_CREDIT_RETURN_ADDR, 0);

	/* PIO Send buffers */
	/* SDMA Send buffers */
	/*
	 * These are not normally read, and (presently) have no method
	 * to be read, so are not pre-initialized
	 */

	/* RcvHdrAddr */
	/* RcvHdrTailAddr */
	/* RcvTidFlowTable */
	for (i = 0; i < chip_rcv_contexts(dd); i++) {
		write_kctxt_csr(dd, i, RCV_HDR_ADDR, 0);
		write_kctxt_csr(dd, i, RCV_HDR_TAIL_ADDR, 0);
		for (j = 0; j < RXE_NUM_TID_FLOWS; j++)
			write_uctxt_csr(dd, i, RCV_TID_FLOW_TABLE + (8 * j), 0);
	}

	/* RcvArray */
	for (i = 0; i < chip_rcv_array_count(dd); i++)
		hfi1_put_tid(dd, i, PT_INVALID_FLUSH, 0, 0);

	/* RcvQPMapTable */
	for (i = 0; i < 32; i++)
		write_csr(dd, RCV_QP_MAP_TABLE + (8 * i), 0);
}

/*
 * Use the ctrl_bits in CceCtrl to clear the status_bits in CceStatus.
 */
static void clear_cce_status(struct hfi1_devdata *dd, u64 status_bits,
			     u64 ctrl_bits)
{
	unsigned long timeout;
	u64 reg;

	/* is the condition present? */
	reg = read_csr(dd, CCE_STATUS);
	if ((reg & status_bits) == 0)
		return;

	/* clear the condition */
	write_csr(dd, CCE_CTRL, ctrl_bits);

	/* wait for the condition to clear */
	timeout = jiffies + msecs_to_jiffies(CCE_STATUS_TIMEOUT);
	while (1) {
		reg = read_csr(dd, CCE_STATUS);
		if ((reg & status_bits) == 0)
			return;
		if (time_after(jiffies, timeout)) {
			dd_dev_err(dd,
				   "Timeout waiting for CceStatus to clear bits 0x%llx, remaining 0x%llx\n",
				   status_bits, reg & status_bits);
			return;
		}
		udelay(1);
	}
}

/* set CCE CSRs to chip reset defaults */
static void reset_cce_csrs(struct hfi1_devdata *dd)
{
	int i;

	/* CCE_REVISION read-only */
	/* CCE_REVISION2 read-only */
	/* CCE_CTRL - bits clear automatically */
	/* CCE_STATUS read-only, use CceCtrl to clear */
	clear_cce_status(dd, ALL_FROZE, CCE_CTRL_SPC_UNFREEZE_SMASK);
	clear_cce_status(dd, ALL_TXE_PAUSE, CCE_CTRL_TXE_RESUME_SMASK);
	clear_cce_status(dd, ALL_RXE_PAUSE, CCE_CTRL_RXE_RESUME_SMASK);
	for (i = 0; i < CCE_NUM_SCRATCH; i++)
		write_csr(dd, CCE_SCRATCH + (8 * i), 0);
	/* CCE_ERR_STATUS read-only */
	write_csr(dd, CCE_ERR_MASK, 0);
	write_csr(dd, CCE_ERR_CLEAR, ~0ull);
	/* CCE_ERR_FORCE leave alone */
	for (i = 0; i < CCE_NUM_32_BIT_COUNTERS; i++)
		write_csr(dd, CCE_COUNTER_ARRAY32 + (8 * i), 0);
	write_csr(dd, CCE_DC_CTRL, CCE_DC_CTRL_RESETCSR);
	/* CCE_PCIE_CTRL leave alone */
	for (i = 0; i < CCE_NUM_MSIX_VECTORS; i++) {
		write_csr(dd, CCE_MSIX_TABLE_LOWER + (8 * i), 0);
		write_csr(dd, CCE_MSIX_TABLE_UPPER + (8 * i),
			  CCE_MSIX_TABLE_UPPER_RESETCSR);
	}
	for (i = 0; i < CCE_NUM_MSIX_PBAS; i++) {
		/* CCE_MSIX_PBA read-only */
		write_csr(dd, CCE_MSIX_INT_GRANTED, ~0ull);
		write_csr(dd, CCE_MSIX_VEC_CLR_WITHOUT_INT, ~0ull);
	}
	for (i = 0; i < CCE_NUM_INT_MAP_CSRS; i++)
		write_csr(dd, CCE_INT_MAP, 0);
	for (i = 0; i < CCE_NUM_INT_CSRS; i++) {
		/* CCE_INT_STATUS read-only */
		write_csr(dd, CCE_INT_MASK + (8 * i), 0);
		write_csr(dd, CCE_INT_CLEAR + (8 * i), ~0ull);
		/* CCE_INT_FORCE leave alone */
		/* CCE_INT_BLOCKED read-only */
	}
	for (i = 0; i < CCE_NUM_32_BIT_INT_COUNTERS; i++)
		write_csr(dd, CCE_INT_COUNTER_ARRAY32 + (8 * i), 0);
}

/* set MISC CSRs to chip reset defaults */
static void reset_misc_csrs(struct hfi1_devdata *dd)
{
	int i;

	for (i = 0; i < 32; i++) {
		write_csr(dd, MISC_CFG_RSA_R2 + (8 * i), 0);
		write_csr(dd, MISC_CFG_RSA_SIGNATURE + (8 * i), 0);
		write_csr(dd, MISC_CFG_RSA_MODULUS + (8 * i), 0);
	}
	/*
	 * MISC_CFG_SHA_PRELOAD leave alone - always reads 0 and can
	 * only be written 128-byte chunks
	 */
	/* init RSA engine to clear lingering errors */
	write_csr(dd, MISC_CFG_RSA_CMD, 1);
	write_csr(dd, MISC_CFG_RSA_MU, 0);
	write_csr(dd, MISC_CFG_FW_CTRL, 0);
	/* MISC_STS_8051_DIGEST read-only */
	/* MISC_STS_SBM_DIGEST read-only */
	/* MISC_STS_PCIE_DIGEST read-only */
	/* MISC_STS_FAB_DIGEST read-only */
	/* MISC_ERR_STATUS read-only */
	write_csr(dd, MISC_ERR_MASK, 0);
	write_csr(dd, MISC_ERR_CLEAR, ~0ull);
	/* MISC_ERR_FORCE leave alone */
}

/* set TXE CSRs to chip reset defaults */
static void reset_txe_csrs(struct hfi1_devdata *dd)
{
	int i;

	/*
	 * TXE Kernel CSRs
	 */
	write_csr(dd, SEND_CTRL, 0);
	__cm_reset(dd, 0);	/* reset CM internal state */
	/* SEND_CONTEXTS read-only */
	/* SEND_DMA_ENGINES read-only */
	/* SEND_PIO_MEM_SIZE read-only */
	/* SEND_DMA_MEM_SIZE read-only */
	write_csr(dd, SEND_HIGH_PRIORITY_LIMIT, 0);
	pio_reset_all(dd);	/* SEND_PIO_INIT_CTXT */
	/* SEND_PIO_ERR_STATUS read-only */
	write_csr(dd, SEND_PIO_ERR_MASK, 0);
	write_csr(dd, SEND_PIO_ERR_CLEAR, ~0ull);
	/* SEND_PIO_ERR_FORCE leave alone */
	/* SEND_DMA_ERR_STATUS read-only */
	write_csr(dd, SEND_DMA_ERR_MASK, 0);
	write_csr(dd, SEND_DMA_ERR_CLEAR, ~0ull);
	/* SEND_DMA_ERR_FORCE leave alone */
	/* SEND_EGRESS_ERR_STATUS read-only */
	write_csr(dd, SEND_EGRESS_ERR_MASK, 0);
	write_csr(dd, SEND_EGRESS_ERR_CLEAR, ~0ull);
	/* SEND_EGRESS_ERR_FORCE leave alone */
	write_csr(dd, SEND_BTH_QP, 0);
	write_csr(dd, SEND_STATIC_RATE_CONTROL, 0);
	write_csr(dd, SEND_SC2VLT0, 0);
	write_csr(dd, SEND_SC2VLT1, 0);
	write_csr(dd, SEND_SC2VLT2, 0);
	write_csr(dd, SEND_SC2VLT3, 0);
	write_csr(dd, SEND_LEN_CHECK0, 0);
	write_csr(dd, SEND_LEN_CHECK1, 0);
	/* SEND_ERR_STATUS read-only */
	write_csr(dd, SEND_ERR_MASK, 0);
	write_csr(dd, SEND_ERR_CLEAR, ~0ull);
	/* SEND_ERR_FORCE read-only */
	for (i = 0; i < VL_ARB_LOW_PRIO_TABLE_SIZE; i++)
		write_csr(dd, SEND_LOW_PRIORITY_LIST + (8 * i), 0);
	for (i = 0; i < VL_ARB_HIGH_PRIO_TABLE_SIZE; i++)
		write_csr(dd, SEND_HIGH_PRIORITY_LIST + (8 * i), 0);
	for (i = 0; i < chip_send_contexts(dd) / NUM_CONTEXTS_PER_SET; i++)
		write_csr(dd, SEND_CONTEXT_SET_CTRL + (8 * i), 0);
	for (i = 0; i < TXE_NUM_32_BIT_COUNTER; i++)
		write_csr(dd, SEND_COUNTER_ARRAY32 + (8 * i), 0);
	for (i = 0; i < TXE_NUM_64_BIT_COUNTER; i++)
		write_csr(dd, SEND_COUNTER_ARRAY64 + (8 * i), 0);
	write_csr(dd, SEND_CM_CTRL, SEND_CM_CTRL_RESETCSR);
	write_csr(dd, SEND_CM_GLOBAL_CREDIT, SEND_CM_GLOBAL_CREDIT_RESETCSR);
	/* SEND_CM_CREDIT_USED_STATUS read-only */
	write_csr(dd, SEND_CM_TIMER_CTRL, 0);
	write_csr(dd, SEND_CM_LOCAL_AU_TABLE0_TO3, 0);
	write_csr(dd, SEND_CM_LOCAL_AU_TABLE4_TO7, 0);
	write_csr(dd, SEND_CM_REMOTE_AU_TABLE0_TO3, 0);
	write_csr(dd, SEND_CM_REMOTE_AU_TABLE4_TO7, 0);
	for (i = 0; i < TXE_NUM_DATA_VL; i++)
		write_csr(dd, SEND_CM_CREDIT_VL + (8 * i), 0);
	write_csr(dd, SEND_CM_CREDIT_VL15, 0);
	/* SEND_CM_CREDIT_USED_VL read-only */
	/* SEND_CM_CREDIT_USED_VL15 read-only */
	/* SEND_EGRESS_CTXT_STATUS read-only */
	/* SEND_EGRESS_SEND_DMA_STATUS read-only */
	write_csr(dd, SEND_EGRESS_ERR_INFO, ~0ull);
	/* SEND_EGRESS_ERR_INFO read-only */
	/* SEND_EGRESS_ERR_SOURCE read-only */

	/*
	 * TXE Per-Context CSRs
	 */
	for (i = 0; i < chip_send_contexts(dd); i++) {
		write_kctxt_csr(dd, i, SEND_CTXT_CTRL, 0);
		write_kctxt_csr(dd, i, SEND_CTXT_CREDIT_CTRL, 0);
		write_kctxt_csr(dd, i, SEND_CTXT_CREDIT_RETURN_ADDR, 0);
		write_kctxt_csr(dd, i, SEND_CTXT_CREDIT_FORCE, 0);
		write_kctxt_csr(dd, i, SEND_CTXT_ERR_MASK, 0);
		write_kctxt_csr(dd, i, SEND_CTXT_ERR_CLEAR, ~0ull);
		write_kctxt_csr(dd, i, SEND_CTXT_CHECK_ENABLE, 0);
		write_kctxt_csr(dd, i, SEND_CTXT_CHECK_VL, 0);
		write_kctxt_csr(dd, i, SEND_CTXT_CHECK_JOB_KEY, 0);
		write_kctxt_csr(dd, i, SEND_CTXT_CHECK_PARTITION_KEY, 0);
		write_kctxt_csr(dd, i, SEND_CTXT_CHECK_SLID, 0);
		write_kctxt_csr(dd, i, SEND_CTXT_CHECK_OPCODE, 0);
	}

	/*
	 * TXE Per-SDMA CSRs
	 */
	for (i = 0; i < chip_sdma_engines(dd); i++) {
		write_kctxt_csr(dd, i, SEND_DMA_CTRL, 0);
		/* SEND_DMA_STATUS read-only */
		write_kctxt_csr(dd, i, SEND_DMA_BASE_ADDR, 0);
		write_kctxt_csr(dd, i, SEND_DMA_LEN_GEN, 0);
		write_kctxt_csr(dd, i, SEND_DMA_TAIL, 0);
		/* SEND_DMA_HEAD read-only */
		write_kctxt_csr(dd, i, SEND_DMA_HEAD_ADDR, 0);
		write_kctxt_csr(dd, i, SEND_DMA_PRIORITY_THLD, 0);
		/* SEND_DMA_IDLE_CNT read-only */
		write_kctxt_csr(dd, i, SEND_DMA_RELOAD_CNT, 0);
		write_kctxt_csr(dd, i, SEND_DMA_DESC_CNT, 0);
		/* SEND_DMA_DESC_FETCHED_CNT read-only */
		/* SEND_DMA_ENG_ERR_STATUS read-only */
		write_kctxt_csr(dd, i, SEND_DMA_ENG_ERR_MASK, 0);
		write_kctxt_csr(dd, i, SEND_DMA_ENG_ERR_CLEAR, ~0ull);
		/* SEND_DMA_ENG_ERR_FORCE leave alone */
		write_kctxt_csr(dd, i, SEND_DMA_CHECK_ENABLE, 0);
		write_kctxt_csr(dd, i, SEND_DMA_CHECK_VL, 0);
		write_kctxt_csr(dd, i, SEND_DMA_CHECK_JOB_KEY, 0);
		write_kctxt_csr(dd, i, SEND_DMA_CHECK_PARTITION_KEY, 0);
		write_kctxt_csr(dd, i, SEND_DMA_CHECK_SLID, 0);
		write_kctxt_csr(dd, i, SEND_DMA_CHECK_OPCODE, 0);
		write_kctxt_csr(dd, i, SEND_DMA_MEMORY, 0);
	}
}

/*
 * Expect on entry:
 * o Packet ingress is disabled, i.e. RcvCtrl.RcvPortEnable == 0
 */
static void init_rbufs(struct hfi1_devdata *dd)
{
	u64 reg;
	int count;

	/*
	 * Wait for DMA to stop: RxRbufPktPending and RxPktInProgress are
	 * clear.
	 */
	count = 0;
	while (1) {
		reg = read_csr(dd, RCV_STATUS);
		if ((reg & (RCV_STATUS_RX_RBUF_PKT_PENDING_SMASK
			    | RCV_STATUS_RX_PKT_IN_PROGRESS_SMASK)) == 0)
			break;
		/*
		 * Give up after 1ms - maximum wait time.
		 *
		 * RBuf size is 136KiB.  Slowest possible is PCIe Gen1 x1 at
		 * 250MB/s bandwidth.  Lower rate to 66% for overhead to get:
		 *	136 KB / (66% * 250MB/s) = 844us
		 */
		if (count++ > 500) {
			dd_dev_err(dd,
				   "%s: in-progress DMA not clearing: RcvStatus 0x%llx, continuing\n",
				   __func__, reg);
			break;
		}
		udelay(2); /* do not busy-wait the CSR */
	}

	/* start the init - expect RcvCtrl to be 0 */
	write_csr(dd, RCV_CTRL, RCV_CTRL_RX_RBUF_INIT_SMASK);

	/*
	 * Read to force the write of Rcvtrl.RxRbufInit.  There is a brief
	 * period after the write before RcvStatus.RxRbufInitDone is valid.
	 * The delay in the first run through the loop below is sufficient and
	 * required before the first read of RcvStatus.RxRbufInintDone.
	 */
	read_csr(dd, RCV_CTRL);

	/* wait for the init to finish */
	count = 0;
	while (1) {
		/* delay is required first time through - see above */
		udelay(2); /* do not busy-wait the CSR */
		reg = read_csr(dd, RCV_STATUS);
		if (reg & (RCV_STATUS_RX_RBUF_INIT_DONE_SMASK))
			break;

		/* give up after 100us - slowest possible at 33MHz is 73us */
		if (count++ > 50) {
			dd_dev_err(dd,
				   "%s: RcvStatus.RxRbufInit not set, continuing\n",
				   __func__);
			break;
		}
	}
}

/* set RXE CSRs to chip reset defaults */
static void reset_rxe_csrs(struct hfi1_devdata *dd)
{
	int i, j;

	/*
	 * RXE Kernel CSRs
	 */
	write_csr(dd, RCV_CTRL, 0);
	init_rbufs(dd);
	/* RCV_STATUS read-only */
	/* RCV_CONTEXTS read-only */
	/* RCV_ARRAY_CNT read-only */
	/* RCV_BUF_SIZE read-only */
	write_csr(dd, RCV_BTH_QP, 0);
	write_csr(dd, RCV_MULTICAST, 0);
	write_csr(dd, RCV_BYPASS, 0);
	write_csr(dd, RCV_VL15, 0);
	/* this is a clear-down */
	write_csr(dd, RCV_ERR_INFO,
		  RCV_ERR_INFO_RCV_EXCESS_BUFFER_OVERRUN_SMASK);
	/* RCV_ERR_STATUS read-only */
	write_csr(dd, RCV_ERR_MASK, 0);
	write_csr(dd, RCV_ERR_CLEAR, ~0ull);
	/* RCV_ERR_FORCE leave alone */
	for (i = 0; i < 32; i++)
		write_csr(dd, RCV_QP_MAP_TABLE + (8 * i), 0);
	for (i = 0; i < 4; i++)
		write_csr(dd, RCV_PARTITION_KEY + (8 * i), 0);
	for (i = 0; i < RXE_NUM_32_BIT_COUNTERS; i++)
		write_csr(dd, RCV_COUNTER_ARRAY32 + (8 * i), 0);
	for (i = 0; i < RXE_NUM_64_BIT_COUNTERS; i++)
		write_csr(dd, RCV_COUNTER_ARRAY64 + (8 * i), 0);
	for (i = 0; i < RXE_NUM_RSM_INSTANCES; i++)
		clear_rsm_rule(dd, i);
	for (i = 0; i < 32; i++)
		write_csr(dd, RCV_RSM_MAP_TABLE + (8 * i), 0);

	/*
	 * RXE Kernel and User Per-Context CSRs
	 */
	for (i = 0; i < chip_rcv_contexts(dd); i++) {
		/* kernel */
		write_kctxt_csr(dd, i, RCV_CTXT_CTRL, 0);
		/* RCV_CTXT_STATUS read-only */
		write_kctxt_csr(dd, i, RCV_EGR_CTRL, 0);
		write_kctxt_csr(dd, i, RCV_TID_CTRL, 0);
		write_kctxt_csr(dd, i, RCV_KEY_CTRL, 0);
		write_kctxt_csr(dd, i, RCV_HDR_ADDR, 0);
		write_kctxt_csr(dd, i, RCV_HDR_CNT, 0);
		write_kctxt_csr(dd, i, RCV_HDR_ENT_SIZE, 0);
		write_kctxt_csr(dd, i, RCV_HDR_SIZE, 0);
		write_kctxt_csr(dd, i, RCV_HDR_TAIL_ADDR, 0);
		write_kctxt_csr(dd, i, RCV_AVAIL_TIME_OUT, 0);
		write_kctxt_csr(dd, i, RCV_HDR_OVFL_CNT, 0);

		/* user */
		/* RCV_HDR_TAIL read-only */
		write_uctxt_csr(dd, i, RCV_HDR_HEAD, 0);
		/* RCV_EGR_INDEX_TAIL read-only */
		write_uctxt_csr(dd, i, RCV_EGR_INDEX_HEAD, 0);
		/* RCV_EGR_OFFSET_TAIL read-only */
		for (j = 0; j < RXE_NUM_TID_FLOWS; j++) {
			write_uctxt_csr(dd, i,
					RCV_TID_FLOW_TABLE + (8 * j), 0);
		}
	}
}

/*
 * Set sc2vl tables.
 *
 * They power on to zeros, so to avoid send context errors
 * they need to be set:
 *
 * SC 0-7 -> VL 0-7 (respectively)
 * SC 15  -> VL 15
 * otherwise
 *        -> VL 0
 */
static void init_sc2vl_tables(struct hfi1_devdata *dd)
{
	int i;
	/* init per architecture spec, constrained by hardware capability */

	/* HFI maps sent packets */
	write_csr(dd, SEND_SC2VLT0, SC2VL_VAL(
		0,
		0, 0, 1, 1,
		2, 2, 3, 3,
		4, 4, 5, 5,
		6, 6, 7, 7));
	write_csr(dd, SEND_SC2VLT1, SC2VL_VAL(
		1,
		8, 0, 9, 0,
		10, 0, 11, 0,
		12, 0, 13, 0,
		14, 0, 15, 15));
	write_csr(dd, SEND_SC2VLT2, SC2VL_VAL(
		2,
		16, 0, 17, 0,
		18, 0, 19, 0,
		20, 0, 21, 0,
		22, 0, 23, 0));
	write_csr(dd, SEND_SC2VLT3, SC2VL_VAL(
		3,
		24, 0, 25, 0,
		26, 0, 27, 0,
		28, 0, 29, 0,
		30, 0, 31, 0));

	/* DC maps received packets */
	write_csr(dd, DCC_CFG_SC_VL_TABLE_15_0, DC_SC_VL_VAL(
		15_0,
		0, 0, 1, 1,  2, 2,  3, 3,  4, 4,  5, 5,  6, 6,  7,  7,
		8, 0, 9, 0, 10, 0, 11, 0, 12, 0, 13, 0, 14, 0, 15, 15));
	write_csr(dd, DCC_CFG_SC_VL_TABLE_31_16, DC_SC_VL_VAL(
		31_16,
		16, 0, 17, 0, 18, 0, 19, 0, 20, 0, 21, 0, 22, 0, 23, 0,
		24, 0, 25, 0, 26, 0, 27, 0, 28, 0, 29, 0, 30, 0, 31, 0));

	/* initialize the cached sc2vl values consistently with h/w */
	for (i = 0; i < 32; i++) {
		if (i < 8 || i == 15)
			*((u8 *)(dd->sc2vl) + i) = (u8)i;
		else
			*((u8 *)(dd->sc2vl) + i) = 0;
	}
}

/*
 * Read chip sizes and then reset parts to sane, disabled, values.  We cannot
 * depend on the chip going through a power-on reset - a driver may be loaded
 * and unloaded many times.
 *
 * Do not write any CSR values to the chip in this routine - there may be
 * a reset following the (possible) FLR in this routine.
 *
 */
static int init_chip(struct hfi1_devdata *dd)
{
	int i;
	int ret = 0;

	/*
	 * Put the HFI CSRs in a known state.
	 * Combine this with a DC reset.
	 *
	 * Stop the device from doing anything while we do a
	 * reset.  We know there are no other active users of
	 * the device since we are now in charge.  Turn off
	 * off all outbound and inbound traffic and make sure
	 * the device does not generate any interrupts.
	 */

	/* disable send contexts and SDMA engines */
	write_csr(dd, SEND_CTRL, 0);
	for (i = 0; i < chip_send_contexts(dd); i++)
		write_kctxt_csr(dd, i, SEND_CTXT_CTRL, 0);
	for (i = 0; i < chip_sdma_engines(dd); i++)
		write_kctxt_csr(dd, i, SEND_DMA_CTRL, 0);
	/* disable port (turn off RXE inbound traffic) and contexts */
	write_csr(dd, RCV_CTRL, 0);
	for (i = 0; i < chip_rcv_contexts(dd); i++)
		write_csr(dd, RCV_CTXT_CTRL, 0);
	/* mask all interrupt sources */
	for (i = 0; i < CCE_NUM_INT_CSRS; i++)
		write_csr(dd, CCE_INT_MASK + (8 * i), 0ull);

	/*
	 * DC Reset: do a full DC reset before the register clear.
	 * A recommended length of time to hold is one CSR read,
	 * so reread the CceDcCtrl.  Then, hold the DC in reset
	 * across the clear.
	 */
	write_csr(dd, CCE_DC_CTRL, CCE_DC_CTRL_DC_RESET_SMASK);
	(void)read_csr(dd, CCE_DC_CTRL);

	if (use_flr) {
		/*
		 * A FLR will reset the SPC core and part of the PCIe.
		 * The parts that need to be restored have already been
		 * saved.
		 */
		dd_dev_info(dd, "Resetting CSRs with FLR\n");

		/* do the FLR, the DC reset will remain */
		pcie_flr(dd->pcidev);

		/* restore command and BARs */
		ret = restore_pci_variables(dd);
		if (ret) {
			dd_dev_err(dd, "%s: Could not restore PCI variables\n",
				   __func__);
			return ret;
		}

		if (is_ax(dd)) {
			dd_dev_info(dd, "Resetting CSRs with FLR\n");
			pcie_flr(dd->pcidev);
			ret = restore_pci_variables(dd);
			if (ret) {
				dd_dev_err(dd, "%s: Could not restore PCI variables\n",
					   __func__);
				return ret;
			}
		}
	} else {
		dd_dev_info(dd, "Resetting CSRs with writes\n");
		reset_cce_csrs(dd);
		reset_txe_csrs(dd);
		reset_rxe_csrs(dd);
		reset_misc_csrs(dd);
	}
	/* clear the DC reset */
	write_csr(dd, CCE_DC_CTRL, 0);

	/* Set the LED off */
	setextled(dd, 0);

	/*
	 * Clear the QSFP reset.
	 * An FLR enforces a 0 on all out pins. The driver does not touch
	 * ASIC_QSFPn_OUT otherwise.  This leaves RESET_N low and
	 * anything plugged constantly in reset, if it pays attention
	 * to RESET_N.
	 * Prime examples of this are optical cables. Set all pins high.
	 * I2CCLK and I2CDAT will change per direction, and INT_N and
	 * MODPRS_N are input only and their value is ignored.
	 */
	write_csr(dd, ASIC_QSFP1_OUT, 0x1f);
	write_csr(dd, ASIC_QSFP2_OUT, 0x1f);
	init_chip_resources(dd);
	return ret;
}

static void init_early_variables(struct hfi1_devdata *dd)
{
	int i;

	/* assign link credit variables */
	dd->vau = CM_VAU;
	dd->link_credits = CM_GLOBAL_CREDITS;
	if (is_ax(dd))
		dd->link_credits--;
	dd->vcu = cu_to_vcu(hfi1_cu);
	/* enough room for 8 MAD packets plus header - 17K */
	dd->vl15_init = (8 * (2048 + 128)) / vau_to_au(dd->vau);
	if (dd->vl15_init > dd->link_credits)
		dd->vl15_init = dd->link_credits;

	write_uninitialized_csrs_and_memories(dd);

	if (HFI1_CAP_IS_KSET(PKEY_CHECK))
		for (i = 0; i < dd->num_pports; i++) {
			struct hfi1_pportdata *ppd = &dd->pport[i];

			set_partition_keys(ppd);
		}
	init_sc2vl_tables(dd);
}

static void init_kdeth_qp(struct hfi1_devdata *dd)
{
	/* user changed the KDETH_QP */
	if (kdeth_qp != 0 && kdeth_qp >= 0xff) {
		/* out of range or illegal value */
		dd_dev_err(dd, "Invalid KDETH queue pair prefix, ignoring");
		kdeth_qp = 0;
	}
	if (kdeth_qp == 0)	/* not set, or failed range check */
		kdeth_qp = DEFAULT_KDETH_QP;

	write_csr(dd, SEND_BTH_QP,
		  (kdeth_qp & SEND_BTH_QP_KDETH_QP_MASK) <<
		  SEND_BTH_QP_KDETH_QP_SHIFT);

	write_csr(dd, RCV_BTH_QP,
		  (kdeth_qp & RCV_BTH_QP_KDETH_QP_MASK) <<
		  RCV_BTH_QP_KDETH_QP_SHIFT);
}

/**
 * hfi1_get_qp_map
 * @dd: device data
 * @idx: index to read
 */
u8 hfi1_get_qp_map(struct hfi1_devdata *dd, u8 idx)
{
	u64 reg = read_csr(dd, RCV_QP_MAP_TABLE + (idx / 8) * 8);

	reg >>= (idx % 8) * 8;
	return reg;
}

/**
 * init_qpmap_table
 * @dd - device data
 * @first_ctxt - first context
 * @last_ctxt - first context
 *
 * This return sets the qpn mapping table that
 * is indexed by qpn[8:1].
 *
 * The routine will round robin the 256 settings
 * from first_ctxt to last_ctxt.
 *
 * The first/last looks ahead to having specialized
 * receive contexts for mgmt and bypass.  Normal
 * verbs traffic will assumed to be on a range
 * of receive contexts.
 */
static void init_qpmap_table(struct hfi1_devdata *dd,
			     u32 first_ctxt,
			     u32 last_ctxt)
{
	u64 reg = 0;
	u64 regno = RCV_QP_MAP_TABLE;
	int i;
	u64 ctxt = first_ctxt;

	for (i = 0; i < 256; i++) {
		reg |= ctxt << (8 * (i % 8));
		ctxt++;
		if (ctxt > last_ctxt)
			ctxt = first_ctxt;
		if (i % 8 == 7) {
			write_csr(dd, regno, reg);
			reg = 0;
			regno += 8;
		}
	}

	add_rcvctrl(dd, RCV_CTRL_RCV_QP_MAP_ENABLE_SMASK
			| RCV_CTRL_RCV_BYPASS_ENABLE_SMASK);
}

struct rsm_map_table {
	u64 map[NUM_MAP_REGS];
	unsigned int used;
};

struct rsm_rule_data {
	u8 offset;
	u8 pkt_type;
	u32 field1_off;
	u32 field2_off;
	u32 index1_off;
	u32 index1_width;
	u32 index2_off;
	u32 index2_width;
	u32 mask1;
	u32 value1;
	u32 mask2;
	u32 value2;
};

/*
 * Return an initialized RMT map table for users to fill in.  OK if it
 * returns NULL, indicating no table.
 */
static struct rsm_map_table *alloc_rsm_map_table(struct hfi1_devdata *dd)
{
	struct rsm_map_table *rmt;
	u8 rxcontext = is_ax(dd) ? 0 : 0xff;  /* 0 is default if a0 ver. */

	rmt = kmalloc(sizeof(*rmt), GFP_KERNEL);
	if (rmt) {
		memset(rmt->map, rxcontext, sizeof(rmt->map));
		rmt->used = 0;
	}

	return rmt;
}

/*
 * Write the final RMT map table to the chip and free the table.  OK if
 * table is NULL.
 */
static void complete_rsm_map_table(struct hfi1_devdata *dd,
				   struct rsm_map_table *rmt)
{
	int i;

	if (rmt) {
		/* write table to chip */
		for (i = 0; i < NUM_MAP_REGS; i++)
			write_csr(dd, RCV_RSM_MAP_TABLE + (8 * i), rmt->map[i]);

		/* enable RSM */
		add_rcvctrl(dd, RCV_CTRL_RCV_RSM_ENABLE_SMASK);
	}
}

/*
 * Add a receive side mapping rule.
 */
static void add_rsm_rule(struct hfi1_devdata *dd, u8 rule_index,
			 struct rsm_rule_data *rrd)
{
	write_csr(dd, RCV_RSM_CFG + (8 * rule_index),
		  (u64)rrd->offset << RCV_RSM_CFG_OFFSET_SHIFT |
		  1ull << rule_index | /* enable bit */
		  (u64)rrd->pkt_type << RCV_RSM_CFG_PACKET_TYPE_SHIFT);
	write_csr(dd, RCV_RSM_SELECT + (8 * rule_index),
		  (u64)rrd->field1_off << RCV_RSM_SELECT_FIELD1_OFFSET_SHIFT |
		  (u64)rrd->field2_off << RCV_RSM_SELECT_FIELD2_OFFSET_SHIFT |
		  (u64)rrd->index1_off << RCV_RSM_SELECT_INDEX1_OFFSET_SHIFT |
		  (u64)rrd->index1_width << RCV_RSM_SELECT_INDEX1_WIDTH_SHIFT |
		  (u64)rrd->index2_off << RCV_RSM_SELECT_INDEX2_OFFSET_SHIFT |
		  (u64)rrd->index2_width << RCV_RSM_SELECT_INDEX2_WIDTH_SHIFT);
	write_csr(dd, RCV_RSM_MATCH + (8 * rule_index),
		  (u64)rrd->mask1 << RCV_RSM_MATCH_MASK1_SHIFT |
		  (u64)rrd->value1 << RCV_RSM_MATCH_VALUE1_SHIFT |
		  (u64)rrd->mask2 << RCV_RSM_MATCH_MASK2_SHIFT |
		  (u64)rrd->value2 << RCV_RSM_MATCH_VALUE2_SHIFT);
}

/*
 * Clear a receive side mapping rule.
 */
static void clear_rsm_rule(struct hfi1_devdata *dd, u8 rule_index)
{
	write_csr(dd, RCV_RSM_CFG + (8 * rule_index), 0);
	write_csr(dd, RCV_RSM_SELECT + (8 * rule_index), 0);
	write_csr(dd, RCV_RSM_MATCH + (8 * rule_index), 0);
}

/* return the number of RSM map table entries that will be used for QOS */
static int qos_rmt_entries(struct hfi1_devdata *dd, unsigned int *mp,
			   unsigned int *np)
{
	int i;
	unsigned int m, n;
	u8 max_by_vl = 0;

	/* is QOS active at all? */
	if (dd->n_krcv_queues <= MIN_KERNEL_KCTXTS ||
	    num_vls == 1 ||
	    krcvqsset <= 1)
		goto no_qos;

	/* determine bits for qpn */
	for (i = 0; i < min_t(unsigned int, num_vls, krcvqsset); i++)
		if (krcvqs[i] > max_by_vl)
			max_by_vl = krcvqs[i];
	if (max_by_vl > 32)
		goto no_qos;
	m = ilog2(__roundup_pow_of_two(max_by_vl));

	/* determine bits for vl */
	n = ilog2(__roundup_pow_of_two(num_vls));

	/* reject if too much is used */
	if ((m + n) > 7)
		goto no_qos;

	if (mp)
		*mp = m;
	if (np)
		*np = n;

	return 1 << (m + n);

no_qos:
	if (mp)
		*mp = 0;
	if (np)
		*np = 0;
	return 0;
}

/**
 * init_qos - init RX qos
 * @dd - device data
 * @rmt - RSM map table
 *
 * This routine initializes Rule 0 and the RSM map table to implement
 * quality of service (qos).
 *
 * If all of the limit tests succeed, qos is applied based on the array
 * interpretation of krcvqs where entry 0 is VL0.
 *
 * The number of vl bits (n) and the number of qpn bits (m) are computed to
 * feed both the RSM map table and the single rule.
 */
static void init_qos(struct hfi1_devdata *dd, struct rsm_map_table *rmt)
{
	struct rsm_rule_data rrd;
	unsigned qpns_per_vl, ctxt, i, qpn, n = 1, m;
	unsigned int rmt_entries;
	u64 reg;

	if (!rmt)
		goto bail;
	rmt_entries = qos_rmt_entries(dd, &m, &n);
	if (rmt_entries == 0)
		goto bail;
	qpns_per_vl = 1 << m;

	/* enough room in the map table? */
	rmt_entries = 1 << (m + n);
	if (rmt->used + rmt_entries >= NUM_MAP_ENTRIES)
		goto bail;

	/* add qos entries to the the RSM map table */
	for (i = 0, ctxt = FIRST_KERNEL_KCTXT; i < num_vls; i++) {
		unsigned tctxt;

		for (qpn = 0, tctxt = ctxt;
		     krcvqs[i] && qpn < qpns_per_vl; qpn++) {
			unsigned idx, regoff, regidx;

			/* generate the index the hardware will produce */
			idx = rmt->used + ((qpn << n) ^ i);
			regoff = (idx % 8) * 8;
			regidx = idx / 8;
			/* replace default with context number */
			reg = rmt->map[regidx];
			reg &= ~(RCV_RSM_MAP_TABLE_RCV_CONTEXT_A_MASK
				<< regoff);
			reg |= (u64)(tctxt++) << regoff;
			rmt->map[regidx] = reg;
			if (tctxt == ctxt + krcvqs[i])
				tctxt = ctxt;
		}
		ctxt += krcvqs[i];
	}

	rrd.offset = rmt->used;
	rrd.pkt_type = 2;
	rrd.field1_off = LRH_BTH_MATCH_OFFSET;
	rrd.field2_off = LRH_SC_MATCH_OFFSET;
	rrd.index1_off = LRH_SC_SELECT_OFFSET;
	rrd.index1_width = n;
	rrd.index2_off = QPN_SELECT_OFFSET;
	rrd.index2_width = m + n;
	rrd.mask1 = LRH_BTH_MASK;
	rrd.value1 = LRH_BTH_VALUE;
	rrd.mask2 = LRH_SC_MASK;
	rrd.value2 = LRH_SC_VALUE;

	/* add rule 0 */
	add_rsm_rule(dd, RSM_INS_VERBS, &rrd);

	/* mark RSM map entries as used */
	rmt->used += rmt_entries;
	/* map everything else to the mcast/err/vl15 context */
	init_qpmap_table(dd, HFI1_CTRL_CTXT, HFI1_CTRL_CTXT);
	dd->qos_shift = n + 1;
	return;
bail:
	dd->qos_shift = 1;
	init_qpmap_table(dd, FIRST_KERNEL_KCTXT, dd->n_krcv_queues - 1);
}

static void init_fecn_handling(struct hfi1_devdata *dd,
			       struct rsm_map_table *rmt)
{
	struct rsm_rule_data rrd;
	u64 reg;
	int i, idx, regoff, regidx, start;
	u8 offset;
	u32 total_cnt;

	if (HFI1_CAP_IS_KSET(TID_RDMA))
		/* Exclude context 0 */
		start = 1;
	else
		start = dd->first_dyn_alloc_ctxt;

	total_cnt = dd->num_rcv_contexts - start;

	/* there needs to be enough room in the map table */
	if (rmt->used + total_cnt >= NUM_MAP_ENTRIES) {
		dd_dev_err(dd, "FECN handling disabled - too many contexts allocated\n");
		return;
	}

	/*
	 * RSM will extract the destination context as an index into the
	 * map table.  The destination contexts are a sequential block
	 * in the range start...num_rcv_contexts-1 (inclusive).
	 * Map entries are accessed as offset + extracted value.  Adjust
	 * the added offset so this sequence can be placed anywhere in
	 * the table - as long as the entries themselves do not wrap.
	 * There are only enough bits in offset for the table size, so
	 * start with that to allow for a "negative" offset.
	 */
	offset = (u8)(NUM_MAP_ENTRIES + rmt->used - start);

	for (i = start, idx = rmt->used; i < dd->num_rcv_contexts;
	     i++, idx++) {
		/* replace with identity mapping */
		regoff = (idx % 8) * 8;
		regidx = idx / 8;
		reg = rmt->map[regidx];
		reg &= ~(RCV_RSM_MAP_TABLE_RCV_CONTEXT_A_MASK << regoff);
		reg |= (u64)i << regoff;
		rmt->map[regidx] = reg;
	}

	/*
	 * For RSM intercept of Expected FECN packets:
	 * o packet type 0 - expected
	 * o match on F (bit 95), using select/match 1, and
	 * o match on SH (bit 133), using select/match 2.
	 *
	 * Use index 1 to extract the 8-bit receive context from DestQP
	 * (start at bit 64).  Use that as the RSM map table index.
	 */
	rrd.offset = offset;
	rrd.pkt_type = 0;
	rrd.field1_off = 95;
	rrd.field2_off = 133;
	rrd.index1_off = 64;
	rrd.index1_width = 8;
	rrd.index2_off = 0;
	rrd.index2_width = 0;
	rrd.mask1 = 1;
	rrd.value1 = 1;
	rrd.mask2 = 1;
	rrd.value2 = 1;

	/* add rule 1 */
	add_rsm_rule(dd, RSM_INS_FECN, &rrd);

	rmt->used += total_cnt;
}

/* Initialize RSM for VNIC */
void hfi1_init_vnic_rsm(struct hfi1_devdata *dd)
{
	u8 i, j;
	u8 ctx_id = 0;
	u64 reg;
	u32 regoff;
	struct rsm_rule_data rrd;

	if (hfi1_vnic_is_rsm_full(dd, NUM_VNIC_MAP_ENTRIES)) {
		dd_dev_err(dd, "Vnic RSM disabled, rmt entries used = %d\n",
			   dd->vnic.rmt_start);
		return;
	}

	dev_dbg(&(dd)->pcidev->dev, "Vnic rsm start = %d, end %d\n",
		dd->vnic.rmt_start,
		dd->vnic.rmt_start + NUM_VNIC_MAP_ENTRIES);

	/* Update RSM mapping table, 32 regs, 256 entries - 1 ctx per byte */
	regoff = RCV_RSM_MAP_TABLE + (dd->vnic.rmt_start / 8) * 8;
	reg = read_csr(dd, regoff);
	for (i = 0; i < NUM_VNIC_MAP_ENTRIES; i++) {
		/* Update map register with vnic context */
		j = (dd->vnic.rmt_start + i) % 8;
		reg &= ~(0xffllu << (j * 8));
		reg |= (u64)dd->vnic.ctxt[ctx_id++]->ctxt << (j * 8);
		/* Wrap up vnic ctx index */
		ctx_id %= dd->vnic.num_ctxt;
		/* Write back map register */
		if (j == 7 || ((i + 1) == NUM_VNIC_MAP_ENTRIES)) {
			dev_dbg(&(dd)->pcidev->dev,
				"Vnic rsm map reg[%d] =0x%llx\n",
				regoff - RCV_RSM_MAP_TABLE, reg);

			write_csr(dd, regoff, reg);
			regoff += 8;
			if (i < (NUM_VNIC_MAP_ENTRIES - 1))
				reg = read_csr(dd, regoff);
		}
	}

	/* Add rule for vnic */
	rrd.offset = dd->vnic.rmt_start;
	rrd.pkt_type = 4;
	/* Match 16B packets */
	rrd.field1_off = L2_TYPE_MATCH_OFFSET;
	rrd.mask1 = L2_TYPE_MASK;
	rrd.value1 = L2_16B_VALUE;
	/* Match ETH L4 packets */
	rrd.field2_off = L4_TYPE_MATCH_OFFSET;
	rrd.mask2 = L4_16B_TYPE_MASK;
	rrd.value2 = L4_16B_ETH_VALUE;
	/* Calc context from veswid and entropy */
	rrd.index1_off = L4_16B_HDR_VESWID_OFFSET;
	rrd.index1_width = ilog2(NUM_VNIC_MAP_ENTRIES);
	rrd.index2_off = L2_16B_ENTROPY_OFFSET;
	rrd.index2_width = ilog2(NUM_VNIC_MAP_ENTRIES);
	add_rsm_rule(dd, RSM_INS_VNIC, &rrd);

	/* Enable RSM if not already enabled */
	add_rcvctrl(dd, RCV_CTRL_RCV_RSM_ENABLE_SMASK);
}

void hfi1_deinit_vnic_rsm(struct hfi1_devdata *dd)
{
	clear_rsm_rule(dd, RSM_INS_VNIC);

	/* Disable RSM if used only by vnic */
	if (dd->vnic.rmt_start == 0)
		clear_rcvctrl(dd, RCV_CTRL_RCV_RSM_ENABLE_SMASK);
}

static int init_rxe(struct hfi1_devdata *dd)
{
	struct rsm_map_table *rmt;
	u64 val;

	/* enable all receive errors */
	write_csr(dd, RCV_ERR_MASK, ~0ull);

	rmt = alloc_rsm_map_table(dd);
	if (!rmt)
		return -ENOMEM;

	/* set up QOS, including the QPN map table */
	init_qos(dd, rmt);
	init_fecn_handling(dd, rmt);
	complete_rsm_map_table(dd, rmt);
	/* record number of used rsm map entries for vnic */
	dd->vnic.rmt_start = rmt->used;
	kfree(rmt);

	/*
	 * make sure RcvCtrl.RcvWcb <= PCIe Device Control
	 * Register Max_Payload_Size (PCI_EXP_DEVCTL in Linux PCIe config
	 * space, PciCfgCap2.MaxPayloadSize in HFI).  There is only one
	 * invalid configuration: RcvCtrl.RcvWcb set to its max of 256 and
	 * Max_PayLoad_Size set to its minimum of 128.
	 *
	 * Presently, RcvCtrl.RcvWcb is not modified from its default of 0
	 * (64 bytes).  Max_Payload_Size is possibly modified upward in
	 * tune_pcie_caps() which is called after this routine.
	 */

	/* Have 16 bytes (4DW) of bypass header available in header queue */
	val = read_csr(dd, RCV_BYPASS);
	val &= ~RCV_BYPASS_HDR_SIZE_SMASK;
	val |= ((4ull & RCV_BYPASS_HDR_SIZE_MASK) <<
		RCV_BYPASS_HDR_SIZE_SHIFT);
	write_csr(dd, RCV_BYPASS, val);
	return 0;
}

static void init_other(struct hfi1_devdata *dd)
{
	/* enable all CCE errors */
	write_csr(dd, CCE_ERR_MASK, ~0ull);
	/* enable *some* Misc errors */
	write_csr(dd, MISC_ERR_MASK, DRIVER_MISC_MASK);
	/* enable all DC errors, except LCB */
	write_csr(dd, DCC_ERR_FLG_EN, ~0ull);
	write_csr(dd, DC_DC8051_ERR_EN, ~0ull);
}

/*
 * Fill out the given AU table using the given CU.  A CU is defined in terms
 * AUs.  The table is a an encoding: given the index, how many AUs does that
 * represent?
 *
 * NOTE: Assumes that the register layout is the same for the
 * local and remote tables.
 */
static void assign_cm_au_table(struct hfi1_devdata *dd, u32 cu,
			       u32 csr0to3, u32 csr4to7)
{
	write_csr(dd, csr0to3,
		  0ull << SEND_CM_LOCAL_AU_TABLE0_TO3_LOCAL_AU_TABLE0_SHIFT |
		  1ull << SEND_CM_LOCAL_AU_TABLE0_TO3_LOCAL_AU_TABLE1_SHIFT |
		  2ull * cu <<
		  SEND_CM_LOCAL_AU_TABLE0_TO3_LOCAL_AU_TABLE2_SHIFT |
		  4ull * cu <<
		  SEND_CM_LOCAL_AU_TABLE0_TO3_LOCAL_AU_TABLE3_SHIFT);
	write_csr(dd, csr4to7,
		  8ull * cu <<
		  SEND_CM_LOCAL_AU_TABLE4_TO7_LOCAL_AU_TABLE4_SHIFT |
		  16ull * cu <<
		  SEND_CM_LOCAL_AU_TABLE4_TO7_LOCAL_AU_TABLE5_SHIFT |
		  32ull * cu <<
		  SEND_CM_LOCAL_AU_TABLE4_TO7_LOCAL_AU_TABLE6_SHIFT |
		  64ull * cu <<
		  SEND_CM_LOCAL_AU_TABLE4_TO7_LOCAL_AU_TABLE7_SHIFT);
}

static void assign_local_cm_au_table(struct hfi1_devdata *dd, u8 vcu)
{
	assign_cm_au_table(dd, vcu_to_cu(vcu), SEND_CM_LOCAL_AU_TABLE0_TO3,
			   SEND_CM_LOCAL_AU_TABLE4_TO7);
}

void assign_remote_cm_au_table(struct hfi1_devdata *dd, u8 vcu)
{
	assign_cm_au_table(dd, vcu_to_cu(vcu), SEND_CM_REMOTE_AU_TABLE0_TO3,
			   SEND_CM_REMOTE_AU_TABLE4_TO7);
}

static void init_txe(struct hfi1_devdata *dd)
{
	int i;

	/* enable all PIO, SDMA, general, and Egress errors */
	write_csr(dd, SEND_PIO_ERR_MASK, ~0ull);
	write_csr(dd, SEND_DMA_ERR_MASK, ~0ull);
	write_csr(dd, SEND_ERR_MASK, ~0ull);
	write_csr(dd, SEND_EGRESS_ERR_MASK, ~0ull);

	/* enable all per-context and per-SDMA engine errors */
	for (i = 0; i < chip_send_contexts(dd); i++)
		write_kctxt_csr(dd, i, SEND_CTXT_ERR_MASK, ~0ull);
	for (i = 0; i < chip_sdma_engines(dd); i++)
		write_kctxt_csr(dd, i, SEND_DMA_ENG_ERR_MASK, ~0ull);

	/* set the local CU to AU mapping */
	assign_local_cm_au_table(dd, dd->vcu);

	/*
	 * Set reasonable default for Credit Return Timer
	 * Don't set on Simulator - causes it to choke.
	 */
	if (dd->icode != ICODE_FUNCTIONAL_SIMULATOR)
		write_csr(dd, SEND_CM_TIMER_CTRL, HFI1_CREDIT_RETURN_RATE);
}

int hfi1_set_ctxt_jkey(struct hfi1_devdata *dd, struct hfi1_ctxtdata *rcd,
		       u16 jkey)
{
	u8 hw_ctxt;
	u64 reg;

	if (!rcd || !rcd->sc)
		return -EINVAL;

	hw_ctxt = rcd->sc->hw_context;
	reg = SEND_CTXT_CHECK_JOB_KEY_MASK_SMASK | /* mask is always 1's */
		((jkey & SEND_CTXT_CHECK_JOB_KEY_VALUE_MASK) <<
		 SEND_CTXT_CHECK_JOB_KEY_VALUE_SHIFT);
	/* JOB_KEY_ALLOW_PERMISSIVE is not allowed by default */
	if (HFI1_CAP_KGET_MASK(rcd->flags, ALLOW_PERM_JKEY))
		reg |= SEND_CTXT_CHECK_JOB_KEY_ALLOW_PERMISSIVE_SMASK;
	write_kctxt_csr(dd, hw_ctxt, SEND_CTXT_CHECK_JOB_KEY, reg);
	/*
	 * Enable send-side J_KEY integrity check, unless this is A0 h/w
	 */
	if (!is_ax(dd)) {
		reg = read_kctxt_csr(dd, hw_ctxt, SEND_CTXT_CHECK_ENABLE);
		reg |= SEND_CTXT_CHECK_ENABLE_CHECK_JOB_KEY_SMASK;
		write_kctxt_csr(dd, hw_ctxt, SEND_CTXT_CHECK_ENABLE, reg);
	}

	/* Enable J_KEY check on receive context. */
	reg = RCV_KEY_CTRL_JOB_KEY_ENABLE_SMASK |
		((jkey & RCV_KEY_CTRL_JOB_KEY_VALUE_MASK) <<
		 RCV_KEY_CTRL_JOB_KEY_VALUE_SHIFT);
	write_kctxt_csr(dd, rcd->ctxt, RCV_KEY_CTRL, reg);

	return 0;
}

int hfi1_clear_ctxt_jkey(struct hfi1_devdata *dd, struct hfi1_ctxtdata *rcd)
{
	u8 hw_ctxt;
	u64 reg;

	if (!rcd || !rcd->sc)
		return -EINVAL;

	hw_ctxt = rcd->sc->hw_context;
	write_kctxt_csr(dd, hw_ctxt, SEND_CTXT_CHECK_JOB_KEY, 0);
	/*
	 * Disable send-side J_KEY integrity check, unless this is A0 h/w.
	 * This check would not have been enabled for A0 h/w, see
	 * set_ctxt_jkey().
	 */
	if (!is_ax(dd)) {
		reg = read_kctxt_csr(dd, hw_ctxt, SEND_CTXT_CHECK_ENABLE);
		reg &= ~SEND_CTXT_CHECK_ENABLE_CHECK_JOB_KEY_SMASK;
		write_kctxt_csr(dd, hw_ctxt, SEND_CTXT_CHECK_ENABLE, reg);
	}
	/* Turn off the J_KEY on the receive side */
	write_kctxt_csr(dd, rcd->ctxt, RCV_KEY_CTRL, 0);

	return 0;
}

int hfi1_set_ctxt_pkey(struct hfi1_devdata *dd, struct hfi1_ctxtdata *rcd,
		       u16 pkey)
{
	u8 hw_ctxt;
	u64 reg;

	if (!rcd || !rcd->sc)
		return -EINVAL;

	hw_ctxt = rcd->sc->hw_context;
	reg = ((u64)pkey & SEND_CTXT_CHECK_PARTITION_KEY_VALUE_MASK) <<
		SEND_CTXT_CHECK_PARTITION_KEY_VALUE_SHIFT;
	write_kctxt_csr(dd, hw_ctxt, SEND_CTXT_CHECK_PARTITION_KEY, reg);
	reg = read_kctxt_csr(dd, hw_ctxt, SEND_CTXT_CHECK_ENABLE);
	reg |= SEND_CTXT_CHECK_ENABLE_CHECK_PARTITION_KEY_SMASK;
	reg &= ~SEND_CTXT_CHECK_ENABLE_DISALLOW_KDETH_PACKETS_SMASK;
	write_kctxt_csr(dd, hw_ctxt, SEND_CTXT_CHECK_ENABLE, reg);

	return 0;
}

int hfi1_clear_ctxt_pkey(struct hfi1_devdata *dd, struct hfi1_ctxtdata *ctxt)
{
	u8 hw_ctxt;
	u64 reg;

	if (!ctxt || !ctxt->sc)
		return -EINVAL;

	hw_ctxt = ctxt->sc->hw_context;
	reg = read_kctxt_csr(dd, hw_ctxt, SEND_CTXT_CHECK_ENABLE);
	reg &= ~SEND_CTXT_CHECK_ENABLE_CHECK_PARTITION_KEY_SMASK;
	write_kctxt_csr(dd, hw_ctxt, SEND_CTXT_CHECK_ENABLE, reg);
	write_kctxt_csr(dd, hw_ctxt, SEND_CTXT_CHECK_PARTITION_KEY, 0);

	return 0;
}

/*
 * Start doing the clean up the the chip. Our clean up happens in multiple
 * stages and this is just the first.
 */
void hfi1_start_cleanup(struct hfi1_devdata *dd)
{
	aspm_exit(dd);
	free_cntrs(dd);
	free_rcverr(dd);
	finish_chip_resources(dd);
}

#define HFI_BASE_GUID(dev) \
	((dev)->base_guid & ~(1ULL << GUID_HFI_INDEX_SHIFT))

/*
 * Information can be shared between the two HFIs on the same ASIC
 * in the same OS.  This function finds the peer device and sets
 * up a shared structure.
 */
static int init_asic_data(struct hfi1_devdata *dd)
{
	unsigned long index;
	struct hfi1_devdata *peer;
	struct hfi1_asic_data *asic_data;
	int ret = 0;

	/* pre-allocate the asic structure in case we are the first device */
	asic_data = kzalloc(sizeof(*dd->asic_data), GFP_KERNEL);
	if (!asic_data)
		return -ENOMEM;

	xa_lock_irq(&hfi1_dev_table);
	/* Find our peer device */
	xa_for_each(&hfi1_dev_table, index, peer) {
		if ((HFI_BASE_GUID(dd) == HFI_BASE_GUID(peer)) &&
		    dd->unit != peer->unit)
			break;
	}

	if (peer) {
		/* use already allocated structure */
		dd->asic_data = peer->asic_data;
		kfree(asic_data);
	} else {
		dd->asic_data = asic_data;
		mutex_init(&dd->asic_data->asic_resource_mutex);
	}
	dd->asic_data->dds[dd->hfi1_id] = dd; /* self back-pointer */
	xa_unlock_irq(&hfi1_dev_table);

	/* first one through - set up i2c devices */
	if (!peer)
		ret = set_up_i2c(dd, dd->asic_data);

	return ret;
}

/*
 * Set dd->boardname.  Use a generic name if a name is not returned from
 * EFI variable space.
 *
 * Return 0 on success, -ENOMEM if space could not be allocated.
 */
static int obtain_boardname(struct hfi1_devdata *dd)
{
	/* generic board description */
	const char generic[] =
		"Intel Omni-Path Host Fabric Interface Adapter 100 Series";
	unsigned long size;
	int ret;

	ret = read_hfi1_efi_var(dd, "description", &size,
				(void **)&dd->boardname);
	if (ret) {
		dd_dev_info(dd, "Board description not found\n");
		/* use generic description */
		dd->boardname = kstrdup(generic, GFP_KERNEL);
		if (!dd->boardname)
			return -ENOMEM;
	}
	return 0;
}

/*
 * Check the interrupt registers to make sure that they are mapped correctly.
 * It is intended to help user identify any mismapping by VMM when the driver
 * is running in a VM. This function should only be called before interrupt
 * is set up properly.
 *
 * Return 0 on success, -EINVAL on failure.
 */
static int check_int_registers(struct hfi1_devdata *dd)
{
	u64 reg;
	u64 all_bits = ~(u64)0;
	u64 mask;

	/* Clear CceIntMask[0] to avoid raising any interrupts */
	mask = read_csr(dd, CCE_INT_MASK);
	write_csr(dd, CCE_INT_MASK, 0ull);
	reg = read_csr(dd, CCE_INT_MASK);
	if (reg)
		goto err_exit;

	/* Clear all interrupt status bits */
	write_csr(dd, CCE_INT_CLEAR, all_bits);
	reg = read_csr(dd, CCE_INT_STATUS);
	if (reg)
		goto err_exit;

	/* Set all interrupt status bits */
	write_csr(dd, CCE_INT_FORCE, all_bits);
	reg = read_csr(dd, CCE_INT_STATUS);
	if (reg != all_bits)
		goto err_exit;

	/* Restore the interrupt mask */
	write_csr(dd, CCE_INT_CLEAR, all_bits);
	write_csr(dd, CCE_INT_MASK, mask);

	return 0;
err_exit:
	write_csr(dd, CCE_INT_MASK, mask);
	dd_dev_err(dd, "Interrupt registers not properly mapped by VMM\n");
	return -EINVAL;
}

/**
 * hfi1_init_dd() - Initialize most of the dd structure.
 * @dev: the pci_dev for hfi1_ib device
 * @ent: pci_device_id struct for this dev
 *
 * This is global, and is called directly at init to set up the
 * chip-specific function pointers for later use.
 */
int hfi1_init_dd(struct hfi1_devdata *dd)
{
	struct pci_dev *pdev = dd->pcidev;
	struct hfi1_pportdata *ppd;
	u64 reg;
	int i, ret;
	static const char * const inames[] = { /* implementation names */
		"RTL silicon",
		"RTL VCS simulation",
		"RTL FPGA emulation",
		"Functional simulator"
	};
	struct pci_dev *parent = pdev->bus->self;
	u32 sdma_engines = chip_sdma_engines(dd);

	ppd = dd->pport;
	for (i = 0; i < dd->num_pports; i++, ppd++) {
		int vl;
		/* init common fields */
		hfi1_init_pportdata(pdev, ppd, dd, 0, 1);
		/* DC supports 4 link widths */
		ppd->link_width_supported =
			OPA_LINK_WIDTH_1X | OPA_LINK_WIDTH_2X |
			OPA_LINK_WIDTH_3X | OPA_LINK_WIDTH_4X;
		ppd->link_width_downgrade_supported =
			ppd->link_width_supported;
		/* start out enabling only 4X */
		ppd->link_width_enabled = OPA_LINK_WIDTH_4X;
		ppd->link_width_downgrade_enabled =
					ppd->link_width_downgrade_supported;
		/* link width active is 0 when link is down */
		/* link width downgrade active is 0 when link is down */

		if (num_vls < HFI1_MIN_VLS_SUPPORTED ||
		    num_vls > HFI1_MAX_VLS_SUPPORTED) {
			dd_dev_err(dd, "Invalid num_vls %u, using %u VLs\n",
				   num_vls, HFI1_MAX_VLS_SUPPORTED);
			num_vls = HFI1_MAX_VLS_SUPPORTED;
		}
		ppd->vls_supported = num_vls;
		ppd->vls_operational = ppd->vls_supported;
		/* Set the default MTU. */
		for (vl = 0; vl < num_vls; vl++)
			dd->vld[vl].mtu = hfi1_max_mtu;
		dd->vld[15].mtu = MAX_MAD_PACKET;
		/*
		 * Set the initial values to reasonable default, will be set
		 * for real when link is up.
		 */
		ppd->overrun_threshold = 0x4;
		ppd->phy_error_threshold = 0xf;
		ppd->port_crc_mode_enabled = link_crc_mask;
		/* initialize supported LTP CRC mode */
		ppd->port_ltp_crc_mode = cap_to_port_ltp(link_crc_mask) << 8;
		/* initialize enabled LTP CRC mode */
		ppd->port_ltp_crc_mode |= cap_to_port_ltp(link_crc_mask) << 4;
		/* start in offline */
		ppd->host_link_state = HLS_DN_OFFLINE;
		init_vl_arb_caches(ppd);
	}

	/*
	 * Do remaining PCIe setup and save PCIe values in dd.
	 * Any error printing is already done by the init code.
	 * On return, we have the chip mapped.
	 */
	ret = hfi1_pcie_ddinit(dd, pdev);
	if (ret < 0)
		goto bail_free;

	/* Save PCI space registers to rewrite after device reset */
	ret = save_pci_variables(dd);
	if (ret < 0)
		goto bail_cleanup;

	dd->majrev = (dd->revision >> CCE_REVISION_CHIP_REV_MAJOR_SHIFT)
			& CCE_REVISION_CHIP_REV_MAJOR_MASK;
	dd->minrev = (dd->revision >> CCE_REVISION_CHIP_REV_MINOR_SHIFT)
			& CCE_REVISION_CHIP_REV_MINOR_MASK;

	/*
	 * Check interrupt registers mapping if the driver has no access to
	 * the upstream component. In this case, it is likely that the driver
	 * is running in a VM.
	 */
	if (!parent) {
		ret = check_int_registers(dd);
		if (ret)
			goto bail_cleanup;
	}

	/*
	 * obtain the hardware ID - NOT related to unit, which is a
	 * software enumeration
	 */
	reg = read_csr(dd, CCE_REVISION2);
	dd->hfi1_id = (reg >> CCE_REVISION2_HFI_ID_SHIFT)
					& CCE_REVISION2_HFI_ID_MASK;
	/* the variable size will remove unwanted bits */
	dd->icode = reg >> CCE_REVISION2_IMPL_CODE_SHIFT;
	dd->irev = reg >> CCE_REVISION2_IMPL_REVISION_SHIFT;
	dd_dev_info(dd, "Implementation: %s, revision 0x%x\n",
		    dd->icode < ARRAY_SIZE(inames) ?
		    inames[dd->icode] : "unknown", (int)dd->irev);

	/* speeds the hardware can support */
	dd->pport->link_speed_supported = OPA_LINK_SPEED_25G;
	/* speeds allowed to run at */
	dd->pport->link_speed_enabled = dd->pport->link_speed_supported;
	/* give a reasonable active value, will be set on link up */
	dd->pport->link_speed_active = OPA_LINK_SPEED_25G;

	/* fix up link widths for emulation _p */
	ppd = dd->pport;
	if (dd->icode == ICODE_FPGA_EMULATION && is_emulator_p(dd)) {
		ppd->link_width_supported =
			ppd->link_width_enabled =
			ppd->link_width_downgrade_supported =
			ppd->link_width_downgrade_enabled =
				OPA_LINK_WIDTH_1X;
	}
	/* insure num_vls isn't larger than number of sdma engines */
	if (HFI1_CAP_IS_KSET(SDMA) && num_vls > sdma_engines) {
		dd_dev_err(dd, "num_vls %u too large, using %u VLs\n",
			   num_vls, sdma_engines);
		num_vls = sdma_engines;
		ppd->vls_supported = sdma_engines;
		ppd->vls_operational = ppd->vls_supported;
	}

	/*
	 * Convert the ns parameter to the 64 * cclocks used in the CSR.
	 * Limit the max if larger than the field holds.  If timeout is
	 * non-zero, then the calculated field will be at least 1.
	 *
	 * Must be after icode is set up - the cclock rate depends
	 * on knowing the hardware being used.
	 */
	dd->rcv_intr_timeout_csr = ns_to_cclock(dd, rcv_intr_timeout) / 64;
	if (dd->rcv_intr_timeout_csr >
			RCV_AVAIL_TIME_OUT_TIME_OUT_RELOAD_MASK)
		dd->rcv_intr_timeout_csr =
			RCV_AVAIL_TIME_OUT_TIME_OUT_RELOAD_MASK;
	else if (dd->rcv_intr_timeout_csr == 0 && rcv_intr_timeout)
		dd->rcv_intr_timeout_csr = 1;

	/* needs to be done before we look for the peer device */
	read_guid(dd);

	/* set up shared ASIC data with peer device */
	ret = init_asic_data(dd);
	if (ret)
		goto bail_cleanup;

	/* obtain chip sizes, reset chip CSRs */
	ret = init_chip(dd);
	if (ret)
		goto bail_cleanup;

	/* read in the PCIe link speed information */
	ret = pcie_speeds(dd);
	if (ret)
		goto bail_cleanup;

	/* call before get_platform_config(), after init_chip_resources() */
	ret = eprom_init(dd);
	if (ret)
		goto bail_free_rcverr;

	/* Needs to be called before hfi1_firmware_init */
	get_platform_config(dd);

	/* read in firmware */
	ret = hfi1_firmware_init(dd);
	if (ret)
		goto bail_cleanup;

	/*
	 * In general, the PCIe Gen3 transition must occur after the
	 * chip has been idled (so it won't initiate any PCIe transactions
	 * e.g. an interrupt) and before the driver changes any registers
	 * (the transition will reset the registers).
	 *
	 * In particular, place this call after:
	 * - init_chip()     - the chip will not initiate any PCIe transactions
	 * - pcie_speeds()   - reads the current link speed
	 * - hfi1_firmware_init() - the needed firmware is ready to be
	 *			    downloaded
	 */
	ret = do_pcie_gen3_transition(dd);
	if (ret)
		goto bail_cleanup;

	/*
	 * This should probably occur in hfi1_pcie_init(), but historically
	 * occurs after the do_pcie_gen3_transition() code.
	 */
	tune_pcie_caps(dd);

	/* start setting dd values and adjusting CSRs */
	init_early_variables(dd);

	parse_platform_config(dd);

	ret = obtain_boardname(dd);
	if (ret)
		goto bail_cleanup;

	snprintf(dd->boardversion, BOARD_VERS_MAX,
		 "ChipABI %u.%u, ChipRev %u.%u, SW Compat %llu\n",
		 HFI1_CHIP_VERS_MAJ, HFI1_CHIP_VERS_MIN,
		 (u32)dd->majrev,
		 (u32)dd->minrev,
		 (dd->revision >> CCE_REVISION_SW_SHIFT)
		    & CCE_REVISION_SW_MASK);

	ret = set_up_context_variables(dd);
	if (ret)
		goto bail_cleanup;

	/* set initial RXE CSRs */
	ret = init_rxe(dd);
	if (ret)
		goto bail_cleanup;

	/* set initial TXE CSRs */
	init_txe(dd);
	/* set initial non-RXE, non-TXE CSRs */
	init_other(dd);
	/* set up KDETH QP prefix in both RX and TX CSRs */
	init_kdeth_qp(dd);

	ret = hfi1_dev_affinity_init(dd);
	if (ret)
		goto bail_cleanup;

	/* send contexts must be set up before receive contexts */
	ret = init_send_contexts(dd);
	if (ret)
		goto bail_cleanup;

	ret = hfi1_create_kctxts(dd);
	if (ret)
		goto bail_cleanup;

	/*
	 * Initialize aspm, to be done after gen3 transition and setting up
	 * contexts and before enabling interrupts
	 */
	aspm_init(dd);

	ret = init_pervl_scs(dd);
	if (ret)
		goto bail_cleanup;

	/* sdma init */
	for (i = 0; i < dd->num_pports; ++i) {
		ret = sdma_init(dd, i);
		if (ret)
			goto bail_cleanup;
	}

	/* use contexts created by hfi1_create_kctxts */
	ret = set_up_interrupts(dd);
	if (ret)
		goto bail_cleanup;

	ret = hfi1_comp_vectors_set_up(dd);
	if (ret)
		goto bail_clear_intr;

	/* set up LCB access - must be after set_up_interrupts() */
	init_lcb_access(dd);

	/*
	 * Serial number is created from the base guid:
	 * [27:24] = base guid [38:35]
	 * [23: 0] = base guid [23: 0]
	 */
	snprintf(dd->serial, SERIAL_MAX, "0x%08llx\n",
		 (dd->base_guid & 0xFFFFFF) |
		     ((dd->base_guid >> 11) & 0xF000000));

	dd->oui1 = dd->base_guid >> 56 & 0xFF;
	dd->oui2 = dd->base_guid >> 48 & 0xFF;
	dd->oui3 = dd->base_guid >> 40 & 0xFF;

	ret = load_firmware(dd); /* asymmetric with dispose_firmware() */
	if (ret)
		goto bail_clear_intr;

	thermal_init(dd);

	ret = init_cntrs(dd);
	if (ret)
		goto bail_clear_intr;

	ret = init_rcverr(dd);
	if (ret)
		goto bail_free_cntrs;

	init_completion(&dd->user_comp);

	/* The user refcount starts with one to inidicate an active device */
	atomic_set(&dd->user_refcount, 1);

	goto bail;

bail_free_rcverr:
	free_rcverr(dd);
bail_free_cntrs:
	free_cntrs(dd);
bail_clear_intr:
	hfi1_comp_vectors_clean_up(dd);
	msix_clean_up_interrupts(dd);
bail_cleanup:
	hfi1_pcie_ddcleanup(dd);
bail_free:
	hfi1_free_devdata(dd);
bail:
	return ret;
}

static u16 delay_cycles(struct hfi1_pportdata *ppd, u32 desired_egress_rate,
			u32 dw_len)
{
	u32 delta_cycles;
	u32 current_egress_rate = ppd->current_egress_rate;
	/* rates here are in units of 10^6 bits/sec */

	if (desired_egress_rate == -1)
		return 0; /* shouldn't happen */

	if (desired_egress_rate >= current_egress_rate)
		return 0; /* we can't help go faster, only slower */

	delta_cycles = egress_cycles(dw_len * 4, desired_egress_rate) -
			egress_cycles(dw_len * 4, current_egress_rate);

	return (u16)delta_cycles;
}

/**
 * create_pbc - build a pbc for transmission
 * @flags: special case flags or-ed in built pbc
 * @srate: static rate
 * @vl: vl
 * @dwlen: dword length (header words + data words + pbc words)
 *
 * Create a PBC with the given flags, rate, VL, and length.
 *
 * NOTE: The PBC created will not insert any HCRC - all callers but one are
 * for verbs, which does not use this PSM feature.  The lone other caller
 * is for the diagnostic interface which calls this if the user does not
 * supply their own PBC.
 */
u64 create_pbc(struct hfi1_pportdata *ppd, u64 flags, int srate_mbs, u32 vl,
	       u32 dw_len)
{
	u64 pbc, delay = 0;

	if (unlikely(srate_mbs))
		delay = delay_cycles(ppd, srate_mbs, dw_len);

	pbc = flags
		| (delay << PBC_STATIC_RATE_CONTROL_COUNT_SHIFT)
		| ((u64)PBC_IHCRC_NONE << PBC_INSERT_HCRC_SHIFT)
		| (vl & PBC_VL_MASK) << PBC_VL_SHIFT
		| (dw_len & PBC_LENGTH_DWS_MASK)
			<< PBC_LENGTH_DWS_SHIFT;

	return pbc;
}

#define SBUS_THERMAL    0x4f
#define SBUS_THERM_MONITOR_MODE 0x1

#define THERM_FAILURE(dev, ret, reason) \
	dd_dev_err((dd),						\
		   "Thermal sensor initialization failed: %s (%d)\n",	\
		   (reason), (ret))

/*
 * Initialize the thermal sensor.
 *
 * After initialization, enable polling of thermal sensor through
 * SBus interface. In order for this to work, the SBus Master
 * firmware has to be loaded due to the fact that the HW polling
 * logic uses SBus interrupts, which are not supported with
 * default firmware. Otherwise, no data will be returned through
 * the ASIC_STS_THERM CSR.
 */
static int thermal_init(struct hfi1_devdata *dd)
{
	int ret = 0;

	if (dd->icode != ICODE_RTL_SILICON ||
	    check_chip_resource(dd, CR_THERM_INIT, NULL))
		return ret;

	ret = acquire_chip_resource(dd, CR_SBUS, SBUS_TIMEOUT);
	if (ret) {
		THERM_FAILURE(dd, ret, "Acquire SBus");
		return ret;
	}

	dd_dev_info(dd, "Initializing thermal sensor\n");
	/* Disable polling of thermal readings */
	write_csr(dd, ASIC_CFG_THERM_POLL_EN, 0x0);
	msleep(100);
	/* Thermal Sensor Initialization */
	/*    Step 1: Reset the Thermal SBus Receiver */
	ret = sbus_request_slow(dd, SBUS_THERMAL, 0x0,
				RESET_SBUS_RECEIVER, 0);
	if (ret) {
		THERM_FAILURE(dd, ret, "Bus Reset");
		goto done;
	}
	/*    Step 2: Set Reset bit in Thermal block */
	ret = sbus_request_slow(dd, SBUS_THERMAL, 0x0,
				WRITE_SBUS_RECEIVER, 0x1);
	if (ret) {
		THERM_FAILURE(dd, ret, "Therm Block Reset");
		goto done;
	}
	/*    Step 3: Write clock divider value (100MHz -> 2MHz) */
	ret = sbus_request_slow(dd, SBUS_THERMAL, 0x1,
				WRITE_SBUS_RECEIVER, 0x32);
	if (ret) {
		THERM_FAILURE(dd, ret, "Write Clock Div");
		goto done;
	}
	/*    Step 4: Select temperature mode */
	ret = sbus_request_slow(dd, SBUS_THERMAL, 0x3,
				WRITE_SBUS_RECEIVER,
				SBUS_THERM_MONITOR_MODE);
	if (ret) {
		THERM_FAILURE(dd, ret, "Write Mode Sel");
		goto done;
	}
	/*    Step 5: De-assert block reset and start conversion */
	ret = sbus_request_slow(dd, SBUS_THERMAL, 0x0,
				WRITE_SBUS_RECEIVER, 0x2);
	if (ret) {
		THERM_FAILURE(dd, ret, "Write Reset Deassert");
		goto done;
	}
	/*    Step 5.1: Wait for first conversion (21.5ms per spec) */
	msleep(22);

	/* Enable polling of thermal readings */
	write_csr(dd, ASIC_CFG_THERM_POLL_EN, 0x1);

	/* Set initialized flag */
	ret = acquire_chip_resource(dd, CR_THERM_INIT, 0);
	if (ret)
		THERM_FAILURE(dd, ret, "Unable to set thermal init flag");

done:
	release_chip_resource(dd, CR_SBUS);
	return ret;
}

static void handle_temp_err(struct hfi1_devdata *dd)
{
	struct hfi1_pportdata *ppd = &dd->pport[0];
	/*
	 * Thermal Critical Interrupt
	 * Put the device into forced freeze mode, take link down to
	 * offline, and put DC into reset.
	 */
	dd_dev_emerg(dd,
		     "Critical temperature reached! Forcing device into freeze mode!\n");
	dd->flags |= HFI1_FORCED_FREEZE;
	start_freeze_handling(ppd, FREEZE_SELF | FREEZE_ABORT);
	/*
	 * Shut DC down as much and as quickly as possible.
	 *
	 * Step 1: Take the link down to OFFLINE. This will cause the
	 *         8051 to put the Serdes in reset. However, we don't want to
	 *         go through the entire link state machine since we want to
	 *         shutdown ASAP. Furthermore, this is not a graceful shutdown
	 *         but rather an attempt to save the chip.
	 *         Code below is almost the same as quiet_serdes() but avoids
	 *         all the extra work and the sleeps.
	 */
	ppd->driver_link_ready = 0;
	ppd->link_enabled = 0;
	set_physical_link_state(dd, (OPA_LINKDOWN_REASON_SMA_DISABLED << 8) |
				PLS_OFFLINE);
	/*
	 * Step 2: Shutdown LCB and 8051
	 *         After shutdown, do not restore DC_CFG_RESET value.
	 */
	dc_shutdown(dd);
}
