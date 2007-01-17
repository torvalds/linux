/*
 * Copyright (c) 2006 QLogic, Inc. All rights reserved.
 * Copyright (c) 2003, 2004, 2005, 2006 PathScale, Inc. All rights reserved.
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
/*
 * This file contains all of the code that is specific to the
 * InfiniPath PCIe chip.
 */

#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>


#include "ipath_kernel.h"
#include "ipath_registers.h"

/*
 * This file contains all the chip-specific register information and
 * access functions for the QLogic InfiniPath PCI-Express chip.
 *
 * This lists the InfiniPath registers, in the actual chip layout.
 * This structure should never be directly accessed.
 */
struct _infinipath_do_not_use_kernel_regs {
	unsigned long long Revision;
	unsigned long long Control;
	unsigned long long PageAlign;
	unsigned long long PortCnt;
	unsigned long long DebugPortSelect;
	unsigned long long Reserved0;
	unsigned long long SendRegBase;
	unsigned long long UserRegBase;
	unsigned long long CounterRegBase;
	unsigned long long Scratch;
	unsigned long long Reserved1;
	unsigned long long Reserved2;
	unsigned long long IntBlocked;
	unsigned long long IntMask;
	unsigned long long IntStatus;
	unsigned long long IntClear;
	unsigned long long ErrorMask;
	unsigned long long ErrorStatus;
	unsigned long long ErrorClear;
	unsigned long long HwErrMask;
	unsigned long long HwErrStatus;
	unsigned long long HwErrClear;
	unsigned long long HwDiagCtrl;
	unsigned long long MDIO;
	unsigned long long IBCStatus;
	unsigned long long IBCCtrl;
	unsigned long long ExtStatus;
	unsigned long long ExtCtrl;
	unsigned long long GPIOOut;
	unsigned long long GPIOMask;
	unsigned long long GPIOStatus;
	unsigned long long GPIOClear;
	unsigned long long RcvCtrl;
	unsigned long long RcvBTHQP;
	unsigned long long RcvHdrSize;
	unsigned long long RcvHdrCnt;
	unsigned long long RcvHdrEntSize;
	unsigned long long RcvTIDBase;
	unsigned long long RcvTIDCnt;
	unsigned long long RcvEgrBase;
	unsigned long long RcvEgrCnt;
	unsigned long long RcvBufBase;
	unsigned long long RcvBufSize;
	unsigned long long RxIntMemBase;
	unsigned long long RxIntMemSize;
	unsigned long long RcvPartitionKey;
	unsigned long long Reserved3;
	unsigned long long RcvPktLEDCnt;
	unsigned long long Reserved4[8];
	unsigned long long SendCtrl;
	unsigned long long SendPIOBufBase;
	unsigned long long SendPIOSize;
	unsigned long long SendPIOBufCnt;
	unsigned long long SendPIOAvailAddr;
	unsigned long long TxIntMemBase;
	unsigned long long TxIntMemSize;
	unsigned long long Reserved5;
	unsigned long long PCIeRBufTestReg0;
	unsigned long long PCIeRBufTestReg1;
	unsigned long long Reserved51[6];
	unsigned long long SendBufferError;
	unsigned long long SendBufferErrorCONT1;
	unsigned long long Reserved6SBE[6];
	unsigned long long RcvHdrAddr0;
	unsigned long long RcvHdrAddr1;
	unsigned long long RcvHdrAddr2;
	unsigned long long RcvHdrAddr3;
	unsigned long long RcvHdrAddr4;
	unsigned long long Reserved7RHA[11];
	unsigned long long RcvHdrTailAddr0;
	unsigned long long RcvHdrTailAddr1;
	unsigned long long RcvHdrTailAddr2;
	unsigned long long RcvHdrTailAddr3;
	unsigned long long RcvHdrTailAddr4;
	unsigned long long Reserved8RHTA[11];
	unsigned long long Reserved9SW[8];
	unsigned long long SerdesConfig0;
	unsigned long long SerdesConfig1;
	unsigned long long SerdesStatus;
	unsigned long long XGXSConfig;
	unsigned long long IBPLLCfg;
	unsigned long long Reserved10SW2[3];
	unsigned long long PCIEQ0SerdesConfig0;
	unsigned long long PCIEQ0SerdesConfig1;
	unsigned long long PCIEQ0SerdesStatus;
	unsigned long long Reserved11;
	unsigned long long PCIEQ1SerdesConfig0;
	unsigned long long PCIEQ1SerdesConfig1;
	unsigned long long PCIEQ1SerdesStatus;
	unsigned long long Reserved12;
};

#define IPATH_KREG_OFFSET(field) (offsetof(struct \
    _infinipath_do_not_use_kernel_regs, field) / sizeof(u64))
#define IPATH_CREG_OFFSET(field) (offsetof( \
    struct infinipath_counters, field) / sizeof(u64))

static const struct ipath_kregs ipath_pe_kregs = {
	.kr_control = IPATH_KREG_OFFSET(Control),
	.kr_counterregbase = IPATH_KREG_OFFSET(CounterRegBase),
	.kr_debugportselect = IPATH_KREG_OFFSET(DebugPortSelect),
	.kr_errorclear = IPATH_KREG_OFFSET(ErrorClear),
	.kr_errormask = IPATH_KREG_OFFSET(ErrorMask),
	.kr_errorstatus = IPATH_KREG_OFFSET(ErrorStatus),
	.kr_extctrl = IPATH_KREG_OFFSET(ExtCtrl),
	.kr_extstatus = IPATH_KREG_OFFSET(ExtStatus),
	.kr_gpio_clear = IPATH_KREG_OFFSET(GPIOClear),
	.kr_gpio_mask = IPATH_KREG_OFFSET(GPIOMask),
	.kr_gpio_out = IPATH_KREG_OFFSET(GPIOOut),
	.kr_gpio_status = IPATH_KREG_OFFSET(GPIOStatus),
	.kr_hwdiagctrl = IPATH_KREG_OFFSET(HwDiagCtrl),
	.kr_hwerrclear = IPATH_KREG_OFFSET(HwErrClear),
	.kr_hwerrmask = IPATH_KREG_OFFSET(HwErrMask),
	.kr_hwerrstatus = IPATH_KREG_OFFSET(HwErrStatus),
	.kr_ibcctrl = IPATH_KREG_OFFSET(IBCCtrl),
	.kr_ibcstatus = IPATH_KREG_OFFSET(IBCStatus),
	.kr_intblocked = IPATH_KREG_OFFSET(IntBlocked),
	.kr_intclear = IPATH_KREG_OFFSET(IntClear),
	.kr_intmask = IPATH_KREG_OFFSET(IntMask),
	.kr_intstatus = IPATH_KREG_OFFSET(IntStatus),
	.kr_mdio = IPATH_KREG_OFFSET(MDIO),
	.kr_pagealign = IPATH_KREG_OFFSET(PageAlign),
	.kr_partitionkey = IPATH_KREG_OFFSET(RcvPartitionKey),
	.kr_portcnt = IPATH_KREG_OFFSET(PortCnt),
	.kr_rcvbthqp = IPATH_KREG_OFFSET(RcvBTHQP),
	.kr_rcvbufbase = IPATH_KREG_OFFSET(RcvBufBase),
	.kr_rcvbufsize = IPATH_KREG_OFFSET(RcvBufSize),
	.kr_rcvctrl = IPATH_KREG_OFFSET(RcvCtrl),
	.kr_rcvegrbase = IPATH_KREG_OFFSET(RcvEgrBase),
	.kr_rcvegrcnt = IPATH_KREG_OFFSET(RcvEgrCnt),
	.kr_rcvhdrcnt = IPATH_KREG_OFFSET(RcvHdrCnt),
	.kr_rcvhdrentsize = IPATH_KREG_OFFSET(RcvHdrEntSize),
	.kr_rcvhdrsize = IPATH_KREG_OFFSET(RcvHdrSize),
	.kr_rcvintmembase = IPATH_KREG_OFFSET(RxIntMemBase),
	.kr_rcvintmemsize = IPATH_KREG_OFFSET(RxIntMemSize),
	.kr_rcvtidbase = IPATH_KREG_OFFSET(RcvTIDBase),
	.kr_rcvtidcnt = IPATH_KREG_OFFSET(RcvTIDCnt),
	.kr_revision = IPATH_KREG_OFFSET(Revision),
	.kr_scratch = IPATH_KREG_OFFSET(Scratch),
	.kr_sendbuffererror = IPATH_KREG_OFFSET(SendBufferError),
	.kr_sendctrl = IPATH_KREG_OFFSET(SendCtrl),
	.kr_sendpioavailaddr = IPATH_KREG_OFFSET(SendPIOAvailAddr),
	.kr_sendpiobufbase = IPATH_KREG_OFFSET(SendPIOBufBase),
	.kr_sendpiobufcnt = IPATH_KREG_OFFSET(SendPIOBufCnt),
	.kr_sendpiosize = IPATH_KREG_OFFSET(SendPIOSize),
	.kr_sendregbase = IPATH_KREG_OFFSET(SendRegBase),
	.kr_txintmembase = IPATH_KREG_OFFSET(TxIntMemBase),
	.kr_txintmemsize = IPATH_KREG_OFFSET(TxIntMemSize),
	.kr_userregbase = IPATH_KREG_OFFSET(UserRegBase),
	.kr_serdesconfig0 = IPATH_KREG_OFFSET(SerdesConfig0),
	.kr_serdesconfig1 = IPATH_KREG_OFFSET(SerdesConfig1),
	.kr_serdesstatus = IPATH_KREG_OFFSET(SerdesStatus),
	.kr_xgxsconfig = IPATH_KREG_OFFSET(XGXSConfig),
	.kr_ibpllcfg = IPATH_KREG_OFFSET(IBPLLCfg),

	/*
	 * These should not be used directly via ipath_read_kreg64(),
	 * use them with ipath_read_kreg64_port()
	 */
	.kr_rcvhdraddr = IPATH_KREG_OFFSET(RcvHdrAddr0),
	.kr_rcvhdrtailaddr = IPATH_KREG_OFFSET(RcvHdrTailAddr0),

	/* The rcvpktled register controls one of the debug port signals, so
	 * a packet activity LED can be connected to it. */
	.kr_rcvpktledcnt = IPATH_KREG_OFFSET(RcvPktLEDCnt),
	.kr_pcierbuftestreg0 = IPATH_KREG_OFFSET(PCIeRBufTestReg0),
	.kr_pcierbuftestreg1 = IPATH_KREG_OFFSET(PCIeRBufTestReg1),
	.kr_pcieq0serdesconfig0 = IPATH_KREG_OFFSET(PCIEQ0SerdesConfig0),
	.kr_pcieq0serdesconfig1 = IPATH_KREG_OFFSET(PCIEQ0SerdesConfig1),
	.kr_pcieq0serdesstatus = IPATH_KREG_OFFSET(PCIEQ0SerdesStatus),
	.kr_pcieq1serdesconfig0 = IPATH_KREG_OFFSET(PCIEQ1SerdesConfig0),
	.kr_pcieq1serdesconfig1 = IPATH_KREG_OFFSET(PCIEQ1SerdesConfig1),
	.kr_pcieq1serdesstatus = IPATH_KREG_OFFSET(PCIEQ1SerdesStatus)
};

static const struct ipath_cregs ipath_pe_cregs = {
	.cr_badformatcnt = IPATH_CREG_OFFSET(RxBadFormatCnt),
	.cr_erricrccnt = IPATH_CREG_OFFSET(RxICRCErrCnt),
	.cr_errlinkcnt = IPATH_CREG_OFFSET(RxLinkProblemCnt),
	.cr_errlpcrccnt = IPATH_CREG_OFFSET(RxLPCRCErrCnt),
	.cr_errpkey = IPATH_CREG_OFFSET(RxPKeyMismatchCnt),
	.cr_errrcvflowctrlcnt = IPATH_CREG_OFFSET(RxFlowCtrlErrCnt),
	.cr_err_rlencnt = IPATH_CREG_OFFSET(RxLenErrCnt),
	.cr_errslencnt = IPATH_CREG_OFFSET(TxLenErrCnt),
	.cr_errtidfull = IPATH_CREG_OFFSET(RxTIDFullErrCnt),
	.cr_errtidvalid = IPATH_CREG_OFFSET(RxTIDValidErrCnt),
	.cr_errvcrccnt = IPATH_CREG_OFFSET(RxVCRCErrCnt),
	.cr_ibstatuschange = IPATH_CREG_OFFSET(IBStatusChangeCnt),
	.cr_intcnt = IPATH_CREG_OFFSET(LBIntCnt),
	.cr_invalidrlencnt = IPATH_CREG_OFFSET(RxMaxMinLenErrCnt),
	.cr_invalidslencnt = IPATH_CREG_OFFSET(TxMaxMinLenErrCnt),
	.cr_lbflowstallcnt = IPATH_CREG_OFFSET(LBFlowStallCnt),
	.cr_pktrcvcnt = IPATH_CREG_OFFSET(RxDataPktCnt),
	.cr_pktrcvflowctrlcnt = IPATH_CREG_OFFSET(RxFlowPktCnt),
	.cr_pktsendcnt = IPATH_CREG_OFFSET(TxDataPktCnt),
	.cr_pktsendflowcnt = IPATH_CREG_OFFSET(TxFlowPktCnt),
	.cr_portovflcnt = IPATH_CREG_OFFSET(RxP0HdrEgrOvflCnt),
	.cr_rcvebpcnt = IPATH_CREG_OFFSET(RxEBPCnt),
	.cr_rcvovflcnt = IPATH_CREG_OFFSET(RxBufOvflCnt),
	.cr_senddropped = IPATH_CREG_OFFSET(TxDroppedPktCnt),
	.cr_sendstallcnt = IPATH_CREG_OFFSET(TxFlowStallCnt),
	.cr_sendunderruncnt = IPATH_CREG_OFFSET(TxUnderrunCnt),
	.cr_wordrcvcnt = IPATH_CREG_OFFSET(RxDwordCnt),
	.cr_wordsendcnt = IPATH_CREG_OFFSET(TxDwordCnt),
	.cr_unsupvlcnt = IPATH_CREG_OFFSET(TxUnsupVLErrCnt),
	.cr_rxdroppktcnt = IPATH_CREG_OFFSET(RxDroppedPktCnt),
	.cr_iblinkerrrecovcnt = IPATH_CREG_OFFSET(IBLinkErrRecoveryCnt),
	.cr_iblinkdowncnt = IPATH_CREG_OFFSET(IBLinkDownedCnt),
	.cr_ibsymbolerrcnt = IPATH_CREG_OFFSET(IBSymbolErrCnt)
};

/* kr_intstatus, kr_intclear, kr_intmask bits */
#define INFINIPATH_I_RCVURG_MASK ((1U<<5)-1)
#define INFINIPATH_I_RCVAVAIL_MASK ((1U<<5)-1)

/* kr_hwerrclear, kr_hwerrmask, kr_hwerrstatus, bits */
#define INFINIPATH_HWE_PCIEMEMPARITYERR_MASK  0x000000000000003fULL
#define INFINIPATH_HWE_PCIEMEMPARITYERR_SHIFT 0
#define INFINIPATH_HWE_PCIEPOISONEDTLP      0x0000000010000000ULL
#define INFINIPATH_HWE_PCIECPLTIMEOUT       0x0000000020000000ULL
#define INFINIPATH_HWE_PCIEBUSPARITYXTLH    0x0000000040000000ULL
#define INFINIPATH_HWE_PCIEBUSPARITYXADM    0x0000000080000000ULL
#define INFINIPATH_HWE_PCIEBUSPARITYRADM    0x0000000100000000ULL
#define INFINIPATH_HWE_COREPLL_FBSLIP       0x0080000000000000ULL
#define INFINIPATH_HWE_COREPLL_RFSLIP       0x0100000000000000ULL
#define INFINIPATH_HWE_PCIE1PLLFAILED       0x0400000000000000ULL
#define INFINIPATH_HWE_PCIE0PLLFAILED       0x0800000000000000ULL
#define INFINIPATH_HWE_SERDESPLLFAILED      0x1000000000000000ULL

/* kr_extstatus bits */
#define INFINIPATH_EXTS_FREQSEL 0x2
#define INFINIPATH_EXTS_SERDESSEL 0x4
#define INFINIPATH_EXTS_MEMBIST_ENDTEST     0x0000000000004000
#define INFINIPATH_EXTS_MEMBIST_FOUND       0x0000000000008000

#define _IPATH_GPIO_SDA_NUM 1
#define _IPATH_GPIO_SCL_NUM 0

#define IPATH_GPIO_SDA (1ULL << \
	(_IPATH_GPIO_SDA_NUM+INFINIPATH_EXTC_GPIOOE_SHIFT))
#define IPATH_GPIO_SCL (1ULL << \
	(_IPATH_GPIO_SCL_NUM+INFINIPATH_EXTC_GPIOOE_SHIFT))

/*
 * Rev2 silicon allows suppressing check for ArmLaunch errors.
 * this can speed up short packet sends on systems that do
 * not guaranteee write-order.
 */
#define INFINIPATH_XGXS_SUPPRESS_ARMLAUNCH_ERR (1ULL<<63)

/* 6120 specific hardware errors... */
static const struct ipath_hwerror_msgs ipath_6120_hwerror_msgs[] = {
	INFINIPATH_HWE_MSG(PCIEPOISONEDTLP, "PCIe Poisoned TLP"),
	INFINIPATH_HWE_MSG(PCIECPLTIMEOUT, "PCIe completion timeout"),
	/*
	 * In practice, it's unlikely wthat we'll see PCIe PLL, or bus
	 * parity or memory parity error failures, because most likely we
	 * won't be able to talk to the core of the chip.  Nonetheless, we
	 * might see them, if they are in parts of the PCIe core that aren't
	 * essential.
	 */
	INFINIPATH_HWE_MSG(PCIE1PLLFAILED, "PCIePLL1"),
	INFINIPATH_HWE_MSG(PCIE0PLLFAILED, "PCIePLL0"),
	INFINIPATH_HWE_MSG(PCIEBUSPARITYXTLH, "PCIe XTLH core parity"),
	INFINIPATH_HWE_MSG(PCIEBUSPARITYXADM, "PCIe ADM TX core parity"),
	INFINIPATH_HWE_MSG(PCIEBUSPARITYRADM, "PCIe ADM RX core parity"),
	INFINIPATH_HWE_MSG(RXDSYNCMEMPARITYERR, "Rx Dsync"),
	INFINIPATH_HWE_MSG(SERDESPLLFAILED, "SerDes PLL"),
};

/**
 * ipath_pe_handle_hwerrors - display hardware errors.
 * @dd: the infinipath device
 * @msg: the output buffer
 * @msgl: the size of the output buffer
 *
 * Use same msg buffer as regular errors to avoid excessive stack
 * use.  Most hardware errors are catastrophic, but for right now,
 * we'll print them and continue.  We reuse the same message buffer as
 * ipath_handle_errors() to avoid excessive stack usage.
 */
static void ipath_pe_handle_hwerrors(struct ipath_devdata *dd, char *msg,
				     size_t msgl)
{
	ipath_err_t hwerrs;
	u32 bits, ctrl;
	int isfatal = 0;
	char bitsmsg[64];

	hwerrs = ipath_read_kreg64(dd, dd->ipath_kregs->kr_hwerrstatus);
	if (!hwerrs) {
		/*
		 * better than printing cofusing messages
		 * This seems to be related to clearing the crc error, or
		 * the pll error during init.
		 */
		ipath_cdbg(VERBOSE, "Called but no hardware errors set\n");
		return;
	} else if (hwerrs == ~0ULL) {
		ipath_dev_err(dd, "Read of hardware error status failed "
			      "(all bits set); ignoring\n");
		return;
	}
	ipath_stats.sps_hwerrs++;

	/* Always clear the error status register, except MEMBISTFAIL,
	 * regardless of whether we continue or stop using the chip.
	 * We want that set so we know it failed, even across driver reload.
	 * We'll still ignore it in the hwerrmask.  We do this partly for
	 * diagnostics, but also for support */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_hwerrclear,
			 hwerrs&~INFINIPATH_HWE_MEMBISTFAILED);

	hwerrs &= dd->ipath_hwerrmask;

	/*
	 * make sure we get this much out, unless told to be quiet,
	 * or it's occurred within the last 5 seconds
	 */
	if ((hwerrs & ~(dd->ipath_lasthwerror |
			((INFINIPATH_HWE_TXEMEMPARITYERR_PIOBUF |
			  INFINIPATH_HWE_TXEMEMPARITYERR_PIOPBC)
			 << INFINIPATH_HWE_TXEMEMPARITYERR_SHIFT))) ||
	    (ipath_debug & __IPATH_VERBDBG))
		dev_info(&dd->pcidev->dev, "Hardware error: hwerr=0x%llx "
			 "(cleared)\n", (unsigned long long) hwerrs);
	dd->ipath_lasthwerror |= hwerrs;

	if (hwerrs & ~dd->ipath_hwe_bitsextant)
		ipath_dev_err(dd, "hwerror interrupt with unknown errors "
			      "%llx set\n", (unsigned long long)
			      (hwerrs & ~dd->ipath_hwe_bitsextant));

	ctrl = ipath_read_kreg32(dd, dd->ipath_kregs->kr_control);
	if (ctrl & INFINIPATH_C_FREEZEMODE) {
		/*
		 * parity errors in send memory are recoverable,
		 * just cancel the send (if indicated in * sendbuffererror),
		 * count the occurrence, unfreeze (if no other handled
		 * hardware error bits are set), and continue. They can
		 * occur if a processor speculative read is done to the PIO
		 * buffer while we are sending a packet, for example.
		 */
		if (hwerrs & ((INFINIPATH_HWE_TXEMEMPARITYERR_PIOBUF |
			       INFINIPATH_HWE_TXEMEMPARITYERR_PIOPBC)
			      << INFINIPATH_HWE_TXEMEMPARITYERR_SHIFT)) {
			ipath_stats.sps_txeparity++;
			ipath_dbg("Recovering from TXE parity error (%llu), "
			    	  "hwerrstatus=%llx\n",
				  (unsigned long long) ipath_stats.sps_txeparity,
				  (unsigned long long) hwerrs);
			ipath_disarm_senderrbufs(dd);
			hwerrs &= ~((INFINIPATH_HWE_TXEMEMPARITYERR_PIOBUF |
				     INFINIPATH_HWE_TXEMEMPARITYERR_PIOPBC)
				    << INFINIPATH_HWE_TXEMEMPARITYERR_SHIFT);
			if (!hwerrs) { /* else leave in freeze mode */
				ipath_write_kreg(dd,
						 dd->ipath_kregs->kr_control,
						 dd->ipath_control);
			    return;
			}
		}
		if (hwerrs) {
			/*
			 * if any set that we aren't ignoring only make the
			 * complaint once, in case it's stuck or recurring,
			 * and we get here multiple times
			 */
			if (dd->ipath_flags & IPATH_INITTED) {
				ipath_dev_err(dd, "Fatal Hardware Error (freeze "
					      "mode), no longer usable, SN %.16s\n",
						  dd->ipath_serial);
				isfatal = 1;
			}
			/*
			 * Mark as having had an error for driver, and also
			 * for /sys and status word mapped to user programs.
			 * This marks unit as not usable, until reset
			 */
			*dd->ipath_statusp &= ~IPATH_STATUS_IB_READY;
			*dd->ipath_statusp |= IPATH_STATUS_HWERROR;
			dd->ipath_flags &= ~IPATH_INITTED;
		} else {
			ipath_dbg("Clearing freezemode on ignored hardware "
				  "error\n");
			ipath_write_kreg(dd, dd->ipath_kregs->kr_control,
			   		 dd->ipath_control);
		}
	}

	*msg = '\0';

	if (hwerrs & INFINIPATH_HWE_MEMBISTFAILED) {
		strlcat(msg, "[Memory BIST test failed, InfiniPath hardware unusable]",
			msgl);
		/* ignore from now on, so disable until driver reloaded */
		*dd->ipath_statusp |= IPATH_STATUS_HWERROR;
		dd->ipath_hwerrmask &= ~INFINIPATH_HWE_MEMBISTFAILED;
		ipath_write_kreg(dd, dd->ipath_kregs->kr_hwerrmask,
				 dd->ipath_hwerrmask);
	}

	ipath_format_hwerrors(hwerrs,
			      ipath_6120_hwerror_msgs,
			      sizeof(ipath_6120_hwerror_msgs)/
			      sizeof(ipath_6120_hwerror_msgs[0]),
			      msg, msgl);

	if (hwerrs & (INFINIPATH_HWE_PCIEMEMPARITYERR_MASK
		      << INFINIPATH_HWE_PCIEMEMPARITYERR_SHIFT)) {
		bits = (u32) ((hwerrs >>
			       INFINIPATH_HWE_PCIEMEMPARITYERR_SHIFT) &
			      INFINIPATH_HWE_PCIEMEMPARITYERR_MASK);
		snprintf(bitsmsg, sizeof bitsmsg,
			 "[PCIe Mem Parity Errs %x] ", bits);
		strlcat(msg, bitsmsg, msgl);
	}

#define _IPATH_PLL_FAIL (INFINIPATH_HWE_COREPLL_FBSLIP |	\
			 INFINIPATH_HWE_COREPLL_RFSLIP )

	if (hwerrs & _IPATH_PLL_FAIL) {
		snprintf(bitsmsg, sizeof bitsmsg,
			 "[PLL failed (%llx), InfiniPath hardware unusable]",
			 (unsigned long long) hwerrs & _IPATH_PLL_FAIL);
		strlcat(msg, bitsmsg, msgl);
		/* ignore from now on, so disable until driver reloaded */
		dd->ipath_hwerrmask &= ~(hwerrs & _IPATH_PLL_FAIL);
		ipath_write_kreg(dd, dd->ipath_kregs->kr_hwerrmask,
				 dd->ipath_hwerrmask);
	}

	if (hwerrs & INFINIPATH_HWE_SERDESPLLFAILED) {
		/*
		 * If it occurs, it is left masked since the eternal
		 * interface is unused
		 */
		dd->ipath_hwerrmask &= ~INFINIPATH_HWE_SERDESPLLFAILED;
		ipath_write_kreg(dd, dd->ipath_kregs->kr_hwerrmask,
				 dd->ipath_hwerrmask);
	}

	ipath_dev_err(dd, "%s hardware error\n", msg);
	if (isfatal && !ipath_diag_inuse && dd->ipath_freezemsg) {
		/*
		 * for /sys status file ; if no trailing } is copied, we'll
		 * know it was truncated.
		 */
		snprintf(dd->ipath_freezemsg, dd->ipath_freezelen,
			 "{%s}", msg);
	}
}

/**
 * ipath_pe_boardname - fill in the board name
 * @dd: the infinipath device
 * @name: the output buffer
 * @namelen: the size of the output buffer
 *
 * info is based on the board revision register
 */
static int ipath_pe_boardname(struct ipath_devdata *dd, char *name,
			      size_t namelen)
{
	char *n = NULL;
	u8 boardrev = dd->ipath_boardrev;
	int ret;

	switch (boardrev) {
	case 0:
		n = "InfiniPath_Emulation";
		break;
	case 1:
		n = "InfiniPath_QLE7140-Bringup";
		break;
	case 2:
		n = "InfiniPath_QLE7140";
		break;
	case 3:
		n = "InfiniPath_QMI7140";
		break;
	case 4:
		n = "InfiniPath_QEM7140";
		break;
	case 5:
		n = "InfiniPath_QMH7140";
		break;
	case 6:
		n = "InfiniPath_QLE7142";
		break;
	default:
		ipath_dev_err(dd,
			      "Don't yet know about board with ID %u\n",
			      boardrev);
		snprintf(name, namelen, "Unknown_InfiniPath_PCIe_%u",
			 boardrev);
		break;
	}
	if (n)
		snprintf(name, namelen, "%s", n);

	if (dd->ipath_majrev != 4 || !dd->ipath_minrev || dd->ipath_minrev>2) {
		ipath_dev_err(dd, "Unsupported InfiniPath hardware revision %u.%u!\n",
			      dd->ipath_majrev, dd->ipath_minrev);
		ret = 1;
	} else
		ret = 0;

	return ret;
}

/**
 * ipath_pe_init_hwerrors - enable hardware errors
 * @dd: the infinipath device
 *
 * now that we have finished initializing everything that might reasonably
 * cause a hardware error, and cleared those errors bits as they occur,
 * we can enable hardware errors in the mask (potentially enabling
 * freeze mode), and enable hardware errors as errors (along with
 * everything else) in errormask
 */
static void ipath_pe_init_hwerrors(struct ipath_devdata *dd)
{
	ipath_err_t val;
	u64 extsval;

	extsval = ipath_read_kreg64(dd, dd->ipath_kregs->kr_extstatus);

	if (!(extsval & INFINIPATH_EXTS_MEMBIST_ENDTEST))
		ipath_dev_err(dd, "MemBIST did not complete!\n");

	val = ~0ULL;	/* barring bugs, all hwerrors become interrupts, */

	if (!dd->ipath_boardrev)	// no PLL for Emulator
		val &= ~INFINIPATH_HWE_SERDESPLLFAILED;

	if (dd->ipath_minrev < 2) {
		/* workaround bug 9460 in internal interface bus parity
		 * checking. Fixed (HW bug 9490) in Rev2.
		 */
		val &= ~INFINIPATH_HWE_PCIEBUSPARITYRADM;
	}
	dd->ipath_hwerrmask = val;
}

/**
 * ipath_pe_bringup_serdes - bring up the serdes
 * @dd: the infinipath device
 */
static int ipath_pe_bringup_serdes(struct ipath_devdata *dd)
{
	u64 val, config1, prev_val;
	int ret = 0;

	ipath_dbg("Trying to bringup serdes\n");

	if (ipath_read_kreg64(dd, dd->ipath_kregs->kr_hwerrstatus) &
	    INFINIPATH_HWE_SERDESPLLFAILED) {
		ipath_dbg("At start, serdes PLL failed bit set "
			  "in hwerrstatus, clearing and continuing\n");
		ipath_write_kreg(dd, dd->ipath_kregs->kr_hwerrclear,
				 INFINIPATH_HWE_SERDESPLLFAILED);
	}

	val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_serdesconfig0);
	config1 = ipath_read_kreg64(dd, dd->ipath_kregs->kr_serdesconfig1);

	ipath_cdbg(VERBOSE, "SerDes status config0=%llx config1=%llx, "
		   "xgxsconfig %llx\n", (unsigned long long) val,
		   (unsigned long long) config1, (unsigned long long)
		   ipath_read_kreg64(dd, dd->ipath_kregs->kr_xgxsconfig));

	/*
	 * Force reset on, also set rxdetect enable.  Must do before reading
	 * serdesstatus at least for simulation, or some of the bits in
	 * serdes status will come back as undefined and cause simulation
	 * failures
	 */
	val |= INFINIPATH_SERDC0_RESET_PLL | INFINIPATH_SERDC0_RXDETECT_EN
		| INFINIPATH_SERDC0_L1PWR_DN;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_serdesconfig0, val);
	/* be sure chip saw it */
	ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);
	udelay(5);		/* need pll reset set at least for a bit */
	/*
	 * after PLL is reset, set the per-lane Resets and TxIdle and
	 * clear the PLL reset and rxdetect (to get falling edge).
	 * Leave L1PWR bits set (permanently)
	 */
	val &= ~(INFINIPATH_SERDC0_RXDETECT_EN | INFINIPATH_SERDC0_RESET_PLL
		 | INFINIPATH_SERDC0_L1PWR_DN);
	val |= INFINIPATH_SERDC0_RESET_MASK | INFINIPATH_SERDC0_TXIDLE;
	ipath_cdbg(VERBOSE, "Clearing pll reset and setting lane resets "
		   "and txidle (%llx)\n", (unsigned long long) val);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_serdesconfig0, val);
	/* be sure chip saw it */
	ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);
	/* need PLL reset clear for at least 11 usec before lane
	 * resets cleared; give it a few more to be sure */
	udelay(15);
	val &= ~(INFINIPATH_SERDC0_RESET_MASK | INFINIPATH_SERDC0_TXIDLE);

	ipath_cdbg(VERBOSE, "Clearing lane resets and txidle "
		   "(writing %llx)\n", (unsigned long long) val);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_serdesconfig0, val);
	/* be sure chip saw it */
	val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);

	val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_xgxsconfig);
	prev_val = val;
	if (((val >> INFINIPATH_XGXS_MDIOADDR_SHIFT) &
	     INFINIPATH_XGXS_MDIOADDR_MASK) != 3) {
		val &=
			~(INFINIPATH_XGXS_MDIOADDR_MASK <<
			  INFINIPATH_XGXS_MDIOADDR_SHIFT);
		/* MDIO address 3 */
		val |= 3ULL << INFINIPATH_XGXS_MDIOADDR_SHIFT;
	}
	if (val & INFINIPATH_XGXS_RESET) {
		val &= ~INFINIPATH_XGXS_RESET;
	}
	if (((val >> INFINIPATH_XGXS_RX_POL_SHIFT) &
	     INFINIPATH_XGXS_RX_POL_MASK) != dd->ipath_rx_pol_inv ) {
		/* need to compensate for Tx inversion in partner */
		val &= ~(INFINIPATH_XGXS_RX_POL_MASK <<
		         INFINIPATH_XGXS_RX_POL_SHIFT);
		val |= dd->ipath_rx_pol_inv <<
			INFINIPATH_XGXS_RX_POL_SHIFT;
	}
	if (dd->ipath_minrev >= 2) {
		/* Rev 2. can tolerate multiple writes to PBC, and
		 * allowing them can provide lower latency on some
		 * CPUs, but this feature is off by default, only
		 * turned on by setting D63 of XGXSconfig reg.
		 * May want to make this conditional more
		 * fine-grained in future. This is not exactly
		 * related to XGXS, but where the bit ended up.
		 */
		val |= INFINIPATH_XGXS_SUPPRESS_ARMLAUNCH_ERR;
	}
	if (val != prev_val)
		ipath_write_kreg(dd, dd->ipath_kregs->kr_xgxsconfig, val);

	val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_serdesconfig0);

	/* clear current and de-emphasis bits */
	config1 &= ~0x0ffffffff00ULL;
	/* set current to 20ma */
	config1 |= 0x00000000000ULL;
	/* set de-emphasis to -5.68dB */
	config1 |= 0x0cccc000000ULL;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_serdesconfig1, config1);

	ipath_cdbg(VERBOSE, "done: SerDes status config0=%llx "
		   "config1=%llx, sstatus=%llx xgxs=%llx\n",
		   (unsigned long long) val, (unsigned long long) config1,
		   (unsigned long long)
		   ipath_read_kreg64(dd, dd->ipath_kregs->kr_serdesstatus),
		   (unsigned long long)
		   ipath_read_kreg64(dd, dd->ipath_kregs->kr_xgxsconfig));

	if (!ipath_waitfor_mdio_cmdready(dd)) {
		ipath_write_kreg(
			dd, dd->ipath_kregs->kr_mdio,
			ipath_mdio_req(IPATH_MDIO_CMD_READ, 31,
				       IPATH_MDIO_CTRL_XGXS_REG_8, 0));
		if (ipath_waitfor_complete(dd, dd->ipath_kregs->kr_mdio,
					   IPATH_MDIO_DATAVALID, &val))
			ipath_dbg("Never got MDIO data for XGXS "
				  "status read\n");
		else
			ipath_cdbg(VERBOSE, "MDIO Read reg8, "
				   "'bank' 31 %x\n", (u32) val);
	} else
		ipath_dbg("Never got MDIO cmdready for XGXS status read\n");

	return ret;
}

/**
 * ipath_pe_quiet_serdes - set serdes to txidle
 * @dd: the infinipath device
 * Called when driver is being unloaded
 */
static void ipath_pe_quiet_serdes(struct ipath_devdata *dd)
{
	u64 val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_serdesconfig0);

	val |= INFINIPATH_SERDC0_TXIDLE;
	ipath_dbg("Setting TxIdleEn on serdes (config0 = %llx)\n",
		  (unsigned long long) val);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_serdesconfig0, val);
}

static int ipath_pe_intconfig(struct ipath_devdata *dd)
{
	u64 val;
	u32 chiprev;

	/*
	 * If the chip supports added error indication via GPIO pins,
	 * enable interrupts on those bits so the interrupt routine
	 * can count the events. Also set flag so interrupt routine
	 * can know they are expected.
	 */
	chiprev = dd->ipath_revision >> INFINIPATH_R_CHIPREVMINOR_SHIFT;
	if ((chiprev & INFINIPATH_R_CHIPREVMINOR_MASK) > 1) {
		/* Rev2+ reports extra errors via internal GPIO pins */
		dd->ipath_flags |= IPATH_GPIO_ERRINTRS;
		val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_gpio_mask);
		val |= IPATH_GPIO_ERRINTR_MASK;
		ipath_write_kreg( dd, dd->ipath_kregs->kr_gpio_mask, val);
	}
	return 0;
}

/**
 * ipath_setup_pe_setextled - set the state of the two external LEDs
 * @dd: the infinipath device
 * @lst: the L state
 * @ltst: the LT state

 * These LEDs indicate the physical and logical state of IB link.
 * For this chip (at least with recommended board pinouts), LED1
 * is Yellow (logical state) and LED2 is Green (physical state),
 *
 * Note:  We try to match the Mellanox HCA LED behavior as best
 * we can.  Green indicates physical link state is OK (something is
 * plugged in, and we can train).
 * Amber indicates the link is logically up (ACTIVE).
 * Mellanox further blinks the amber LED to indicate data packet
 * activity, but we have no hardware support for that, so it would
 * require waking up every 10-20 msecs and checking the counters
 * on the chip, and then turning the LED off if appropriate.  That's
 * visible overhead, so not something we will do.
 *
 */
static void ipath_setup_pe_setextled(struct ipath_devdata *dd, u64 lst,
				     u64 ltst)
{
	u64 extctl;

	/* the diags use the LED to indicate diag info, so we leave
	 * the external LED alone when the diags are running */
	if (ipath_diag_inuse)
		return;

	extctl = dd->ipath_extctrl & ~(INFINIPATH_EXTC_LED1PRIPORT_ON |
				       INFINIPATH_EXTC_LED2PRIPORT_ON);

	if (ltst & INFINIPATH_IBCS_LT_STATE_LINKUP)
		extctl |= INFINIPATH_EXTC_LED2PRIPORT_ON;
	if (lst == INFINIPATH_IBCS_L_STATE_ACTIVE)
		extctl |= INFINIPATH_EXTC_LED1PRIPORT_ON;
	dd->ipath_extctrl = extctl;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_extctrl, extctl);
}

/**
 * ipath_setup_pe_cleanup - clean up any per-chip chip-specific stuff
 * @dd: the infinipath device
 *
 * This is called during driver unload.
 * We do the pci_disable_msi here, not in generic code, because it
 * isn't used for the HT chips. If we do end up needing pci_enable_msi
 * at some point in the future for HT, we'll move the call back
 * into the main init_one code.
 */
static void ipath_setup_pe_cleanup(struct ipath_devdata *dd)
{
	dd->ipath_msi_lo = 0;	/* just in case unload fails */
	pci_disable_msi(dd->pcidev);
}

/**
 * ipath_setup_pe_config - setup PCIe config related stuff
 * @dd: the infinipath device
 * @pdev: the PCI device
 *
 * The pci_enable_msi() call will fail on systems with MSI quirks
 * such as those with AMD8131, even if the device of interest is not
 * attached to that device, (in the 2.6.13 - 2.6.15 kernels, at least, fixed
 * late in 2.6.16).
 * All that can be done is to edit the kernel source to remove the quirk
 * check until that is fixed.
 * We do not need to call enable_msi() for our HyperTransport chip,
 * even though it uses MSI, and we want to avoid the quirk warning, so
 * So we call enable_msi only for PCIe.  If we do end up needing
 * pci_enable_msi at some point in the future for HT, we'll move the
 * call back into the main init_one code.
 * We save the msi lo and hi values, so we can restore them after
 * chip reset (the kernel PCI infrastructure doesn't yet handle that
 * correctly).
 */
static int ipath_setup_pe_config(struct ipath_devdata *dd,
				 struct pci_dev *pdev)
{
	int pos, ret;

	dd->ipath_msi_lo = 0;	/* used as a flag during reset processing */
	ret = pci_enable_msi(dd->pcidev);
	if (ret)
		ipath_dev_err(dd, "pci_enable_msi failed: %d, "
			      "interrupts may not work\n", ret);
	/* continue even if it fails, we may still be OK... */
	dd->ipath_irq = pdev->irq;

	if ((pos = pci_find_capability(dd->pcidev, PCI_CAP_ID_MSI))) {
		u16 control;
		pci_read_config_dword(dd->pcidev, pos + PCI_MSI_ADDRESS_LO,
				      &dd->ipath_msi_lo);
		pci_read_config_dword(dd->pcidev, pos + PCI_MSI_ADDRESS_HI,
				      &dd->ipath_msi_hi);
		pci_read_config_word(dd->pcidev, pos + PCI_MSI_FLAGS,
				     &control);
		/* now save the data (vector) info */
		pci_read_config_word(dd->pcidev,
				     pos + ((control & PCI_MSI_FLAGS_64BIT)
					    ? 12 : 8),
				     &dd->ipath_msi_data);
		ipath_cdbg(VERBOSE, "Read msi data 0x%x from config offset "
			   "0x%x, control=0x%x\n", dd->ipath_msi_data,
			   pos + ((control & PCI_MSI_FLAGS_64BIT) ? 12 : 8),
			   control);
		/* we save the cachelinesize also, although it doesn't
		 * really matter */
		pci_read_config_byte(dd->pcidev, PCI_CACHE_LINE_SIZE,
				     &dd->ipath_pci_cacheline);
	} else
		ipath_dev_err(dd, "Can't find MSI capability, "
			      "can't save MSI settings for reset\n");
	if ((pos = pci_find_capability(dd->pcidev, PCI_CAP_ID_EXP))) {
		u16 linkstat;
		pci_read_config_word(dd->pcidev, pos + PCI_EXP_LNKSTA,
				     &linkstat);
		linkstat >>= 4;
		linkstat &= 0x1f;
		if (linkstat != 8)
			ipath_dev_err(dd, "PCIe width %u, "
				      "performance reduced\n", linkstat);
	}
	else
		ipath_dev_err(dd, "Can't find PCI Express "
			      "capability!\n");
	return 0;
}

static void ipath_init_pe_variables(struct ipath_devdata *dd)
{
	/*
	 * bits for selecting i2c direction and values,
	 * used for I2C serial flash
	 */
	dd->ipath_gpio_sda_num = _IPATH_GPIO_SDA_NUM;
	dd->ipath_gpio_scl_num = _IPATH_GPIO_SCL_NUM;
	dd->ipath_gpio_sda = IPATH_GPIO_SDA;
	dd->ipath_gpio_scl = IPATH_GPIO_SCL;

	/* variables for sanity checking interrupt and errors */
	dd->ipath_hwe_bitsextant =
		(INFINIPATH_HWE_RXEMEMPARITYERR_MASK <<
		 INFINIPATH_HWE_RXEMEMPARITYERR_SHIFT) |
		(INFINIPATH_HWE_TXEMEMPARITYERR_MASK <<
		 INFINIPATH_HWE_TXEMEMPARITYERR_SHIFT) |
		(INFINIPATH_HWE_PCIEMEMPARITYERR_MASK <<
		 INFINIPATH_HWE_PCIEMEMPARITYERR_SHIFT) |
		INFINIPATH_HWE_PCIE1PLLFAILED |
		INFINIPATH_HWE_PCIE0PLLFAILED |
		INFINIPATH_HWE_PCIEPOISONEDTLP |
		INFINIPATH_HWE_PCIECPLTIMEOUT |
		INFINIPATH_HWE_PCIEBUSPARITYXTLH |
		INFINIPATH_HWE_PCIEBUSPARITYXADM |
		INFINIPATH_HWE_PCIEBUSPARITYRADM |
		INFINIPATH_HWE_MEMBISTFAILED |
		INFINIPATH_HWE_COREPLL_FBSLIP |
		INFINIPATH_HWE_COREPLL_RFSLIP |
		INFINIPATH_HWE_SERDESPLLFAILED |
		INFINIPATH_HWE_IBCBUSTOSPCPARITYERR |
		INFINIPATH_HWE_IBCBUSFRSPCPARITYERR;
	dd->ipath_i_bitsextant =
		(INFINIPATH_I_RCVURG_MASK << INFINIPATH_I_RCVURG_SHIFT) |
		(INFINIPATH_I_RCVAVAIL_MASK <<
		 INFINIPATH_I_RCVAVAIL_SHIFT) |
		INFINIPATH_I_ERROR | INFINIPATH_I_SPIOSENT |
		INFINIPATH_I_SPIOBUFAVAIL | INFINIPATH_I_GPIO;
	dd->ipath_e_bitsextant =
		INFINIPATH_E_RFORMATERR | INFINIPATH_E_RVCRC |
		INFINIPATH_E_RICRC | INFINIPATH_E_RMINPKTLEN |
		INFINIPATH_E_RMAXPKTLEN | INFINIPATH_E_RLONGPKTLEN |
		INFINIPATH_E_RSHORTPKTLEN | INFINIPATH_E_RUNEXPCHAR |
		INFINIPATH_E_RUNSUPVL | INFINIPATH_E_REBP |
		INFINIPATH_E_RIBFLOW | INFINIPATH_E_RBADVERSION |
		INFINIPATH_E_RRCVEGRFULL | INFINIPATH_E_RRCVHDRFULL |
		INFINIPATH_E_RBADTID | INFINIPATH_E_RHDRLEN |
		INFINIPATH_E_RHDR | INFINIPATH_E_RIBLOSTLINK |
		INFINIPATH_E_SMINPKTLEN | INFINIPATH_E_SMAXPKTLEN |
		INFINIPATH_E_SUNDERRUN | INFINIPATH_E_SPKTLEN |
		INFINIPATH_E_SDROPPEDSMPPKT | INFINIPATH_E_SDROPPEDDATAPKT |
		INFINIPATH_E_SPIOARMLAUNCH | INFINIPATH_E_SUNEXPERRPKTNUM |
		INFINIPATH_E_SUNSUPVL | INFINIPATH_E_IBSTATUSCHANGED |
		INFINIPATH_E_INVALIDADDR | INFINIPATH_E_RESET |
		INFINIPATH_E_HARDWARE;

	dd->ipath_i_rcvavail_mask = INFINIPATH_I_RCVAVAIL_MASK;
	dd->ipath_i_rcvurg_mask = INFINIPATH_I_RCVURG_MASK;
}

/* setup the MSI stuff again after a reset.  I'd like to just call
 * pci_enable_msi() and request_irq() again, but when I do that,
 * the MSI enable bit doesn't get set in the command word, and
 * we switch to to a different interrupt vector, which is confusing,
 * so I instead just do it all inline.  Perhaps somehow can tie this
 * into the PCIe hotplug support at some point
 * Note, because I'm doing it all here, I don't call pci_disable_msi()
 * or free_irq() at the start of ipath_setup_pe_reset().
 */
static int ipath_reinit_msi(struct ipath_devdata *dd)
{
	int pos;
	u16 control;
	int ret;

	if (!dd->ipath_msi_lo) {
		dev_info(&dd->pcidev->dev, "Can't restore MSI config, "
			 "initial setup failed?\n");
		ret = 0;
		goto bail;
	}

	if (!(pos = pci_find_capability(dd->pcidev, PCI_CAP_ID_MSI))) {
		ipath_dev_err(dd, "Can't find MSI capability, "
			      "can't restore MSI settings\n");
		ret = 0;
		goto bail;
	}
	ipath_cdbg(VERBOSE, "Writing msi_lo 0x%x to config offset 0x%x\n",
		   dd->ipath_msi_lo, pos + PCI_MSI_ADDRESS_LO);
	pci_write_config_dword(dd->pcidev, pos + PCI_MSI_ADDRESS_LO,
			       dd->ipath_msi_lo);
	ipath_cdbg(VERBOSE, "Writing msi_lo 0x%x to config offset 0x%x\n",
		   dd->ipath_msi_hi, pos + PCI_MSI_ADDRESS_HI);
	pci_write_config_dword(dd->pcidev, pos + PCI_MSI_ADDRESS_HI,
			       dd->ipath_msi_hi);
	pci_read_config_word(dd->pcidev, pos + PCI_MSI_FLAGS, &control);
	if (!(control & PCI_MSI_FLAGS_ENABLE)) {
		ipath_cdbg(VERBOSE, "MSI control at off %x was %x, "
			   "setting MSI enable (%x)\n", pos + PCI_MSI_FLAGS,
			   control, control | PCI_MSI_FLAGS_ENABLE);
		control |= PCI_MSI_FLAGS_ENABLE;
		pci_write_config_word(dd->pcidev, pos + PCI_MSI_FLAGS,
				      control);
	}
	/* now rewrite the data (vector) info */
	pci_write_config_word(dd->pcidev, pos +
			      ((control & PCI_MSI_FLAGS_64BIT) ? 12 : 8),
			      dd->ipath_msi_data);
	/* we restore the cachelinesize also, although it doesn't really
	 * matter */
	pci_write_config_byte(dd->pcidev, PCI_CACHE_LINE_SIZE,
			      dd->ipath_pci_cacheline);
	/* and now set the pci master bit again */
	pci_set_master(dd->pcidev);
	ret = 1;

bail:
	return ret;
}

/* This routine sleeps, so it can only be called from user context, not
 * from interrupt context.  If we need interrupt context, we can split
 * it into two routines.
*/
static int ipath_setup_pe_reset(struct ipath_devdata *dd)
{
	u64 val;
	int i;
	int ret;

	/* Use ERROR so it shows up in logs, etc. */
	ipath_dev_err(dd, "Resetting InfiniPath unit %u\n", dd->ipath_unit);
	/* keep chip from being accessed in a few places */
	dd->ipath_flags &= ~(IPATH_INITTED|IPATH_PRESENT);
	val = dd->ipath_control | INFINIPATH_C_RESET;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_control, val);
	mb();

	for (i = 1; i <= 5; i++) {
		int r;
		/* allow MBIST, etc. to complete; longer on each retry.
		 * We sometimes get machine checks from bus timeout if no
		 * response, so for now, make it *really* long.
		 */
		msleep(1000 + (1 + i) * 2000);
		if ((r =
		     pci_write_config_dword(dd->pcidev, PCI_BASE_ADDRESS_0,
					    dd->ipath_pcibar0)))
			ipath_dev_err(dd, "rewrite of BAR0 failed: %d\n",
				      r);
		if ((r =
		     pci_write_config_dword(dd->pcidev, PCI_BASE_ADDRESS_1,
					    dd->ipath_pcibar1)))
			ipath_dev_err(dd, "rewrite of BAR1 failed: %d\n",
				      r);
		/* now re-enable memory access */
		if ((r = pci_enable_device(dd->pcidev)))
			ipath_dev_err(dd, "pci_enable_device failed after "
				      "reset: %d\n", r);
		/* whether it worked or not, mark as present, again */
		dd->ipath_flags |= IPATH_PRESENT;
		val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_revision);
		if (val == dd->ipath_revision) {
			ipath_cdbg(VERBOSE, "Got matching revision "
				   "register %llx on try %d\n",
				   (unsigned long long) val, i);
			ret = ipath_reinit_msi(dd);
			goto bail;
		}
		/* Probably getting -1 back */
		ipath_dbg("Didn't get expected revision register, "
			  "got %llx, try %d\n", (unsigned long long) val,
			  i + 1);
	}
	ret = 0; /* failed */

bail:
	return ret;
}

/**
 * ipath_pe_put_tid - write a TID in chip
 * @dd: the infinipath device
 * @tidptr: pointer to the expected TID (in chip) to udpate
 * @tidtype: 0 for eager, 1 for expected
 * @pa: physical address of in memory buffer; ipath_tidinvalid if freeing
 *
 * This exists as a separate routine to allow for special locking etc.
 * It's used for both the full cleanup on exit, as well as the normal
 * setup and teardown.
 */
static void ipath_pe_put_tid(struct ipath_devdata *dd, u64 __iomem *tidptr,
			     u32 type, unsigned long pa)
{
	u32 __iomem *tidp32 = (u32 __iomem *)tidptr;
	unsigned long flags = 0; /* keep gcc quiet */

	if (pa != dd->ipath_tidinvalid) {
		if (pa & ((1U << 11) - 1)) {
			dev_info(&dd->pcidev->dev, "BUG: physaddr %lx "
				 "not 4KB aligned!\n", pa);
			return;
		}
		pa >>= 11;
		/* paranoia check */
		if (pa & (7<<29))
			ipath_dev_err(dd,
				      "BUG: Physical page address 0x%lx "
				      "has bits set in 31-29\n", pa);

		if (type == 0)
			pa |= dd->ipath_tidtemplate;
		else /* for now, always full 4KB page */
			pa |= 2 << 29;
	}

	/* workaround chip bug 9437 by writing each TID twice
	 * and holding a spinlock around the writes, so they don't
	 * intermix with other TID (eager or expected) writes
	 * Unfortunately, this call can be done from interrupt level
	 * for the port 0 eager TIDs, so we have to use irqsave
	 */
	spin_lock_irqsave(&dd->ipath_tid_lock, flags);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_scratch, 0xfeeddeaf);
	if (dd->ipath_kregbase)
		writel(pa, tidp32);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_scratch, 0xdeadbeef);
	mmiowb();
	spin_unlock_irqrestore(&dd->ipath_tid_lock, flags);
}
/**
 * ipath_pe_put_tid_2 - write a TID in chip, Revision 2 or higher
 * @dd: the infinipath device
 * @tidptr: pointer to the expected TID (in chip) to udpate
 * @tidtype: 0 for eager, 1 for expected
 * @pa: physical address of in memory buffer; ipath_tidinvalid if freeing
 *
 * This exists as a separate routine to allow for selection of the
 * appropriate "flavor". The static calls in cleanup just use the
 * revision-agnostic form, as they are not performance critical.
 */
static void ipath_pe_put_tid_2(struct ipath_devdata *dd, u64 __iomem *tidptr,
			     u32 type, unsigned long pa)
{
	u32 __iomem *tidp32 = (u32 __iomem *)tidptr;

	if (pa != dd->ipath_tidinvalid) {
		if (pa & ((1U << 11) - 1)) {
			dev_info(&dd->pcidev->dev, "BUG: physaddr %lx "
				 "not 2KB aligned!\n", pa);
			return;
		}
		pa >>= 11;
		/* paranoia check */
		if (pa & (7<<29))
			ipath_dev_err(dd,
				      "BUG: Physical page address 0x%lx "
				      "has bits set in 31-29\n", pa);

		if (type == 0)
			pa |= dd->ipath_tidtemplate;
		else /* for now, always full 4KB page */
			pa |= 2 << 29;
	}
	if (dd->ipath_kregbase)
		writel(pa, tidp32);
	mmiowb();
}


/**
 * ipath_pe_clear_tid - clear all TID entries for a port, expected and eager
 * @dd: the infinipath device
 * @port: the port
 *
 * clear all TID entries for a port, expected and eager.
 * Used from ipath_close().  On this chip, TIDs are only 32 bits,
 * not 64, but they are still on 64 bit boundaries, so tidbase
 * is declared as u64 * for the pointer math, even though we write 32 bits
 */
static void ipath_pe_clear_tids(struct ipath_devdata *dd, unsigned port)
{
	u64 __iomem *tidbase;
	unsigned long tidinv;
	int i;

	if (!dd->ipath_kregbase)
		return;

	ipath_cdbg(VERBOSE, "Invalidate TIDs for port %u\n", port);

	tidinv = dd->ipath_tidinvalid;
	tidbase = (u64 __iomem *)
		((char __iomem *)(dd->ipath_kregbase) +
		 dd->ipath_rcvtidbase +
		 port * dd->ipath_rcvtidcnt * sizeof(*tidbase));

	for (i = 0; i < dd->ipath_rcvtidcnt; i++)
		ipath_pe_put_tid(dd, &tidbase[i], 0, tidinv);

	tidbase = (u64 __iomem *)
		((char __iomem *)(dd->ipath_kregbase) +
		 dd->ipath_rcvegrbase +
		 port * dd->ipath_rcvegrcnt * sizeof(*tidbase));

	for (i = 0; i < dd->ipath_rcvegrcnt; i++)
		ipath_pe_put_tid(dd, &tidbase[i], 1, tidinv);
}

/**
 * ipath_pe_tidtemplate - setup constants for TID updates
 * @dd: the infinipath device
 *
 * We setup stuff that we use a lot, to avoid calculating each time
 */
static void ipath_pe_tidtemplate(struct ipath_devdata *dd)
{
	u32 egrsize = dd->ipath_rcvegrbufsize;

	/* For now, we always allocate 4KB buffers (at init) so we can
	 * receive max size packets.  We may want a module parameter to
	 * specify 2KB or 4KB and/or make be per port instead of per device
	 * for those who want to reduce memory footprint.  Note that the
	 * ipath_rcvhdrentsize size must be large enough to hold the largest
	 * IB header (currently 96 bytes) that we expect to handle (plus of
	 * course the 2 dwords of RHF).
	 */
	if (egrsize == 2048)
		dd->ipath_tidtemplate = 1U << 29;
	else if (egrsize == 4096)
		dd->ipath_tidtemplate = 2U << 29;
	else {
		egrsize = 4096;
		dev_info(&dd->pcidev->dev, "BUG: unsupported egrbufsize "
			 "%u, using %u\n", dd->ipath_rcvegrbufsize,
			 egrsize);
		dd->ipath_tidtemplate = 2U << 29;
	}
	dd->ipath_tidinvalid = 0;
}

static int ipath_pe_early_init(struct ipath_devdata *dd)
{
	dd->ipath_flags |= IPATH_4BYTE_TID;

	/*
	 * For openfabrics, we need to be able to handle an IB header of
	 * 24 dwords.  HT chip has arbitrary sized receive buffers, so we
	 * made them the same size as the PIO buffers.  This chip does not
	 * handle arbitrary size buffers, so we need the header large enough
	 * to handle largest IB header, but still have room for a 2KB MTU
	 * standard IB packet.
	 */
	dd->ipath_rcvhdrentsize = 24;
	dd->ipath_rcvhdrsize = IPATH_DFLT_RCVHDRSIZE;

	/*
	 * To truly support a 4KB MTU (for usermode), we need to
	 * bump this to a larger value.  For now, we use them for
	 * the kernel only.
	 */
	dd->ipath_rcvegrbufsize = 2048;
	/*
	 * the min() check here is currently a nop, but it may not always
	 * be, depending on just how we do ipath_rcvegrbufsize
	 */
	dd->ipath_ibmaxlen = min(dd->ipath_piosize2k,
				 dd->ipath_rcvegrbufsize +
				 (dd->ipath_rcvhdrentsize << 2));
	dd->ipath_init_ibmaxlen = dd->ipath_ibmaxlen;

	/*
	 * We can request a receive interrupt for 1 or
	 * more packets from current offset.  For now, we set this
	 * up for a single packet.
	 */
	dd->ipath_rhdrhead_intr_off = 1ULL<<32;

	ipath_get_eeprom_info(dd);

	return 0;
}

int __attribute__((weak)) ipath_unordered_wc(void)
{
	return 0;
}

/**
 * ipath_init_pe_get_base_info - set chip-specific flags for user code
 * @pd: the infinipath port
 * @kbase: ipath_base_info pointer
 *
 * We set the PCIE flag because the lower bandwidth on PCIe vs
 * HyperTransport can affect some user packet algorithims.
 */
static int ipath_pe_get_base_info(struct ipath_portdata *pd, void *kbase)
{
	struct ipath_base_info *kinfo = kbase;
	struct ipath_devdata *dd;

	if (ipath_unordered_wc()) {
		kinfo->spi_runtime_flags |= IPATH_RUNTIME_FORCE_WC_ORDER;
		ipath_cdbg(PROC, "Intel processor, forcing WC order\n");
	}
	else
		ipath_cdbg(PROC, "Not Intel processor, WC ordered\n");

	if (pd == NULL)
		goto done;

	dd = pd->port_dd;

	if (dd != NULL && dd->ipath_minrev >= 2) {
		ipath_cdbg(PROC, "IBA6120 Rev2, allow multiple PBC write\n");
		kinfo->spi_runtime_flags |= IPATH_RUNTIME_PBC_REWRITE;
		ipath_cdbg(PROC, "IBA6120 Rev2, allow loose DMA alignment\n");
		kinfo->spi_runtime_flags |= IPATH_RUNTIME_LOOSE_DMA_ALIGN;
	}

done:
	kinfo->spi_runtime_flags |= IPATH_RUNTIME_PCIE;
	return 0;
}

static void ipath_pe_free_irq(struct ipath_devdata *dd)
{
	free_irq(dd->ipath_irq, dd);
	dd->ipath_irq = 0;
}

/**
 * ipath_init_iba6120_funcs - set up the chip-specific function pointers
 * @dd: the infinipath device
 *
 * This is global, and is called directly at init to set up the
 * chip-specific function pointers for later use.
 */
void ipath_init_iba6120_funcs(struct ipath_devdata *dd)
{
	dd->ipath_f_intrsetup = ipath_pe_intconfig;
	dd->ipath_f_bus = ipath_setup_pe_config;
	dd->ipath_f_reset = ipath_setup_pe_reset;
	dd->ipath_f_get_boardname = ipath_pe_boardname;
	dd->ipath_f_init_hwerrors = ipath_pe_init_hwerrors;
	dd->ipath_f_early_init = ipath_pe_early_init;
	dd->ipath_f_handle_hwerrors = ipath_pe_handle_hwerrors;
	dd->ipath_f_quiet_serdes = ipath_pe_quiet_serdes;
	dd->ipath_f_bringup_serdes = ipath_pe_bringup_serdes;
	dd->ipath_f_clear_tids = ipath_pe_clear_tids;
	if (dd->ipath_minrev >= 2)
		dd->ipath_f_put_tid = ipath_pe_put_tid_2;
	else
		dd->ipath_f_put_tid = ipath_pe_put_tid;
	dd->ipath_f_cleanup = ipath_setup_pe_cleanup;
	dd->ipath_f_setextled = ipath_setup_pe_setextled;
	dd->ipath_f_get_base_info = ipath_pe_get_base_info;
	dd->ipath_f_free_irq = ipath_pe_free_irq;

	/* initialize chip-specific variables */
	dd->ipath_f_tidtemplate = ipath_pe_tidtemplate;

	/*
	 * setup the register offsets, since they are different for each
	 * chip
	 */
	dd->ipath_kregs = &ipath_pe_kregs;
	dd->ipath_cregs = &ipath_pe_cregs;

	ipath_init_pe_variables(dd);
}

