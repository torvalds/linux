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
 * This file contains all of the code that is specific to the InfiniPath
 * HT chip.
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/htirq.h>

#include "ipath_kernel.h"
#include "ipath_registers.h"

static void ipath_setup_ht_setextled(struct ipath_devdata *, u64, u64);


/*
 * This lists the InfiniPath registers, in the actual chip layout.
 * This structure should never be directly accessed.
 *
 * The names are in InterCap form because they're taken straight from
 * the chip specification.  Since they're only used in this file, they
 * don't pollute the rest of the source.
*/

struct _infinipath_do_not_use_kernel_regs {
	unsigned long long Revision;
	unsigned long long Control;
	unsigned long long PageAlign;
	unsigned long long PortCnt;
	unsigned long long DebugPortSelect;
	unsigned long long DebugPort;
	unsigned long long SendRegBase;
	unsigned long long UserRegBase;
	unsigned long long CounterRegBase;
	unsigned long long Scratch;
	unsigned long long ReservedMisc1;
	unsigned long long InterruptConfig;
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
	unsigned long long ReservedRcv[10];
	unsigned long long SendCtrl;
	unsigned long long SendPIOBufBase;
	unsigned long long SendPIOSize;
	unsigned long long SendPIOBufCnt;
	unsigned long long SendPIOAvailAddr;
	unsigned long long TxIntMemBase;
	unsigned long long TxIntMemSize;
	unsigned long long ReservedSend[9];
	unsigned long long SendBufferError;
	unsigned long long SendBufferErrorCONT1;
	unsigned long long SendBufferErrorCONT2;
	unsigned long long SendBufferErrorCONT3;
	unsigned long long ReservedSBE[4];
	unsigned long long RcvHdrAddr0;
	unsigned long long RcvHdrAddr1;
	unsigned long long RcvHdrAddr2;
	unsigned long long RcvHdrAddr3;
	unsigned long long RcvHdrAddr4;
	unsigned long long RcvHdrAddr5;
	unsigned long long RcvHdrAddr6;
	unsigned long long RcvHdrAddr7;
	unsigned long long RcvHdrAddr8;
	unsigned long long ReservedRHA[7];
	unsigned long long RcvHdrTailAddr0;
	unsigned long long RcvHdrTailAddr1;
	unsigned long long RcvHdrTailAddr2;
	unsigned long long RcvHdrTailAddr3;
	unsigned long long RcvHdrTailAddr4;
	unsigned long long RcvHdrTailAddr5;
	unsigned long long RcvHdrTailAddr6;
	unsigned long long RcvHdrTailAddr7;
	unsigned long long RcvHdrTailAddr8;
	unsigned long long ReservedRHTA[7];
	unsigned long long Sync;	/* Software only */
	unsigned long long Dump;	/* Software only */
	unsigned long long SimVer;	/* Software only */
	unsigned long long ReservedSW[5];
	unsigned long long SerdesConfig0;
	unsigned long long SerdesConfig1;
	unsigned long long SerdesStatus;
	unsigned long long XGXSConfig;
	unsigned long long ReservedSW2[4];
};

#define IPATH_KREG_OFFSET(field) (offsetof(struct \
    _infinipath_do_not_use_kernel_regs, field) / sizeof(u64))
#define IPATH_CREG_OFFSET(field) (offsetof( \
    struct infinipath_counters, field) / sizeof(u64))

static const struct ipath_kregs ipath_ht_kregs = {
	.kr_control = IPATH_KREG_OFFSET(Control),
	.kr_counterregbase = IPATH_KREG_OFFSET(CounterRegBase),
	.kr_debugport = IPATH_KREG_OFFSET(DebugPort),
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
	.kr_interruptconfig = IPATH_KREG_OFFSET(InterruptConfig),
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
	/*
	 * These should not be used directly via ipath_write_kreg64(),
	 * use them with ipath_write_kreg64_port(),
	 */
	.kr_rcvhdraddr = IPATH_KREG_OFFSET(RcvHdrAddr0),
	.kr_rcvhdrtailaddr = IPATH_KREG_OFFSET(RcvHdrTailAddr0)
};

static const struct ipath_cregs ipath_ht_cregs = {
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
	/* calc from Reg_CounterRegBase + offset */
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
#define INFINIPATH_I_RCVURG_MASK ((1U<<9)-1)
#define INFINIPATH_I_RCVAVAIL_MASK ((1U<<9)-1)

/* kr_hwerrclear, kr_hwerrmask, kr_hwerrstatus, bits */
#define INFINIPATH_HWE_HTCMEMPARITYERR_SHIFT 0
#define INFINIPATH_HWE_HTCMEMPARITYERR_MASK 0x3FFFFFULL
#define INFINIPATH_HWE_HTCLNKABYTE0CRCERR   0x0000000000800000ULL
#define INFINIPATH_HWE_HTCLNKABYTE1CRCERR   0x0000000001000000ULL
#define INFINIPATH_HWE_HTCLNKBBYTE0CRCERR   0x0000000002000000ULL
#define INFINIPATH_HWE_HTCLNKBBYTE1CRCERR   0x0000000004000000ULL
#define INFINIPATH_HWE_HTCMISCERR4          0x0000000008000000ULL
#define INFINIPATH_HWE_HTCMISCERR5          0x0000000010000000ULL
#define INFINIPATH_HWE_HTCMISCERR6          0x0000000020000000ULL
#define INFINIPATH_HWE_HTCMISCERR7          0x0000000040000000ULL
#define INFINIPATH_HWE_HTCBUSTREQPARITYERR  0x0000000080000000ULL
#define INFINIPATH_HWE_HTCBUSTRESPPARITYERR 0x0000000100000000ULL
#define INFINIPATH_HWE_HTCBUSIREQPARITYERR  0x0000000200000000ULL
#define INFINIPATH_HWE_COREPLL_FBSLIP       0x0080000000000000ULL
#define INFINIPATH_HWE_COREPLL_RFSLIP       0x0100000000000000ULL
#define INFINIPATH_HWE_HTBPLL_FBSLIP        0x0200000000000000ULL
#define INFINIPATH_HWE_HTBPLL_RFSLIP        0x0400000000000000ULL
#define INFINIPATH_HWE_HTAPLL_FBSLIP        0x0800000000000000ULL
#define INFINIPATH_HWE_HTAPLL_RFSLIP        0x1000000000000000ULL
#define INFINIPATH_HWE_SERDESPLLFAILED      0x2000000000000000ULL

/* kr_extstatus bits */
#define INFINIPATH_EXTS_FREQSEL 0x2
#define INFINIPATH_EXTS_SERDESSEL 0x4
#define INFINIPATH_EXTS_MEMBIST_ENDTEST     0x0000000000004000
#define INFINIPATH_EXTS_MEMBIST_CORRECT     0x0000000000008000


/* TID entries (memory), HT-only */
#define INFINIPATH_RT_ADDR_MASK 0xFFFFFFFFFFULL	/* 40 bits valid */
#define INFINIPATH_RT_VALID 0x8000000000000000ULL
#define INFINIPATH_RT_ADDR_SHIFT 0
#define INFINIPATH_RT_BUFSIZE_MASK 0x3FFFULL
#define INFINIPATH_RT_BUFSIZE_SHIFT 48

/*
 * masks and bits that are different in different chips, or present only
 * in one
 */
static const ipath_err_t infinipath_hwe_htcmemparityerr_mask =
    INFINIPATH_HWE_HTCMEMPARITYERR_MASK;
static const ipath_err_t infinipath_hwe_htcmemparityerr_shift =
    INFINIPATH_HWE_HTCMEMPARITYERR_SHIFT;

static const ipath_err_t infinipath_hwe_htclnkabyte0crcerr =
    INFINIPATH_HWE_HTCLNKABYTE0CRCERR;
static const ipath_err_t infinipath_hwe_htclnkabyte1crcerr =
    INFINIPATH_HWE_HTCLNKABYTE1CRCERR;
static const ipath_err_t infinipath_hwe_htclnkbbyte0crcerr =
    INFINIPATH_HWE_HTCLNKBBYTE0CRCERR;
static const ipath_err_t infinipath_hwe_htclnkbbyte1crcerr =
    INFINIPATH_HWE_HTCLNKBBYTE1CRCERR;

#define _IPATH_GPIO_SDA_NUM 1
#define _IPATH_GPIO_SCL_NUM 0

#define IPATH_GPIO_SDA \
	(1ULL << (_IPATH_GPIO_SDA_NUM+INFINIPATH_EXTC_GPIOOE_SHIFT))
#define IPATH_GPIO_SCL \
	(1ULL << (_IPATH_GPIO_SCL_NUM+INFINIPATH_EXTC_GPIOOE_SHIFT))

/* keep the code below somewhat more readonable; not used elsewhere */
#define _IPATH_HTLINK0_CRCBITS (infinipath_hwe_htclnkabyte0crcerr |	\
				infinipath_hwe_htclnkabyte1crcerr)
#define _IPATH_HTLINK1_CRCBITS (infinipath_hwe_htclnkbbyte0crcerr |	\
				infinipath_hwe_htclnkbbyte1crcerr)
#define _IPATH_HTLANE0_CRCBITS (infinipath_hwe_htclnkabyte0crcerr |	\
				infinipath_hwe_htclnkbbyte0crcerr)
#define _IPATH_HTLANE1_CRCBITS (infinipath_hwe_htclnkabyte1crcerr |	\
				infinipath_hwe_htclnkbbyte1crcerr)

static void hwerr_crcbits(struct ipath_devdata *dd, ipath_err_t hwerrs,
			  char *msg, size_t msgl)
{
	char bitsmsg[64];
	ipath_err_t crcbits = hwerrs &
		(_IPATH_HTLINK0_CRCBITS | _IPATH_HTLINK1_CRCBITS);
	/* don't check if 8bit HT */
	if (dd->ipath_flags & IPATH_8BIT_IN_HT0)
		crcbits &= ~infinipath_hwe_htclnkabyte1crcerr;
	/* don't check if 8bit HT */
	if (dd->ipath_flags & IPATH_8BIT_IN_HT1)
		crcbits &= ~infinipath_hwe_htclnkbbyte1crcerr;
	/*
	 * we'll want to ignore link errors on link that is
	 * not in use, if any.  For now, complain about both
	 */
	if (crcbits) {
		u16 ctrl0, ctrl1;
		snprintf(bitsmsg, sizeof bitsmsg,
			 "[HT%s lane %s CRC (%llx); powercycle to completely clear]",
			 !(crcbits & _IPATH_HTLINK1_CRCBITS) ?
			 "0 (A)" : (!(crcbits & _IPATH_HTLINK0_CRCBITS)
				    ? "1 (B)" : "0+1 (A+B)"),
			 !(crcbits & _IPATH_HTLANE1_CRCBITS) ? "0"
			 : (!(crcbits & _IPATH_HTLANE0_CRCBITS) ? "1" :
			    "0+1"), (unsigned long long) crcbits);
		strlcat(msg, bitsmsg, msgl);

		/*
		 * print extra info for debugging.  slave/primary
		 * config word 4, 8 (link control 0, 1)
		 */

		if (pci_read_config_word(dd->pcidev,
					 dd->ipath_ht_slave_off + 0x4,
					 &ctrl0))
			dev_info(&dd->pcidev->dev, "Couldn't read "
				 "linkctrl0 of slave/primary "
				 "config block\n");
		else if (!(ctrl0 & 1 << 6))
			/* not if EOC bit set */
			ipath_dbg("HT linkctrl0 0x%x%s%s\n", ctrl0,
				  ((ctrl0 >> 8) & 7) ? " CRC" : "",
				  ((ctrl0 >> 4) & 1) ? "linkfail" :
				  "");
		if (pci_read_config_word(dd->pcidev,
					 dd->ipath_ht_slave_off + 0x8,
					 &ctrl1))
			dev_info(&dd->pcidev->dev, "Couldn't read "
				 "linkctrl1 of slave/primary "
				 "config block\n");
		else if (!(ctrl1 & 1 << 6))
			/* not if EOC bit set */
			ipath_dbg("HT linkctrl1 0x%x%s%s\n", ctrl1,
				  ((ctrl1 >> 8) & 7) ? " CRC" : "",
				  ((ctrl1 >> 4) & 1) ? "linkfail" :
				  "");

		/* disable until driver reloaded */
		dd->ipath_hwerrmask &= ~crcbits;
		ipath_write_kreg(dd, dd->ipath_kregs->kr_hwerrmask,
				 dd->ipath_hwerrmask);
		ipath_dbg("HT crc errs: %s\n", msg);
	} else
		ipath_dbg("ignoring HT crc errors 0x%llx, "
			  "not in use\n", (unsigned long long)
			  (hwerrs & (_IPATH_HTLINK0_CRCBITS |
				     _IPATH_HTLINK1_CRCBITS)));
}

/* 6110 specific hardware errors... */
static const struct ipath_hwerror_msgs ipath_6110_hwerror_msgs[] = {
	INFINIPATH_HWE_MSG(HTCBUSIREQPARITYERR, "HTC Ireq Parity"),
	INFINIPATH_HWE_MSG(HTCBUSTREQPARITYERR, "HTC Treq Parity"),
	INFINIPATH_HWE_MSG(HTCBUSTRESPPARITYERR, "HTC Tresp Parity"),
	INFINIPATH_HWE_MSG(HTCMISCERR5, "HT core Misc5"),
	INFINIPATH_HWE_MSG(HTCMISCERR6, "HT core Misc6"),
	INFINIPATH_HWE_MSG(HTCMISCERR7, "HT core Misc7"),
	INFINIPATH_HWE_MSG(RXDSYNCMEMPARITYERR, "Rx Dsync"),
	INFINIPATH_HWE_MSG(SERDESPLLFAILED, "SerDes PLL"),
};

#define TXE_PIO_PARITY ((INFINIPATH_HWE_TXEMEMPARITYERR_PIOBUF | \
		        INFINIPATH_HWE_TXEMEMPARITYERR_PIOPBC) \
		        << INFINIPATH_HWE_TXEMEMPARITYERR_SHIFT)
#define RXE_EAGER_PARITY (INFINIPATH_HWE_RXEMEMPARITYERR_EAGERTID \
			  << INFINIPATH_HWE_RXEMEMPARITYERR_SHIFT)

static int ipath_ht_txe_recover(struct ipath_devdata *);

/**
 * ipath_ht_handle_hwerrors - display hardware errors.
 * @dd: the infinipath device
 * @msg: the output buffer
 * @msgl: the size of the output buffer
 *
 * Use same msg buffer as regular errors to avoid excessive stack
 * use.  Most hardware errors are catastrophic, but for right now,
 * we'll print them and continue.  We reuse the same message buffer as
 * ipath_handle_errors() to avoid excessive stack usage.
 */
static void ipath_ht_handle_hwerrors(struct ipath_devdata *dd, char *msg,
				     size_t msgl)
{
	ipath_err_t hwerrs;
	u32 bits, ctrl;
	int isfatal = 0;
	char bitsmsg[64];

	hwerrs = ipath_read_kreg64(dd, dd->ipath_kregs->kr_hwerrstatus);

	if (!hwerrs) {
		ipath_cdbg(VERBOSE, "Called but no hardware errors set\n");
		/*
		 * better than printing cofusing messages
		 * This seems to be related to clearing the crc error, or
		 * the pll error during init.
		 */
		goto bail;
	} else if (hwerrs == -1LL) {
		ipath_dev_err(dd, "Read of hardware error status failed "
			      "(all bits set); ignoring\n");
		goto bail;
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
	 * it's a parity error we may recover from,
	 * or it's occurred within the last 5 seconds
	 */
	if ((hwerrs & ~(dd->ipath_lasthwerror | TXE_PIO_PARITY |
		RXE_EAGER_PARITY)) ||
		(ipath_debug & __IPATH_VERBDBG))
		dev_info(&dd->pcidev->dev, "Hardware error: hwerr=0x%llx "
			 "(cleared)\n", (unsigned long long) hwerrs);
	dd->ipath_lasthwerror |= hwerrs;

	if (hwerrs & ~dd->ipath_hwe_bitsextant)
		ipath_dev_err(dd, "hwerror interrupt with unknown errors "
			      "%llx set\n", (unsigned long long)
			      (hwerrs & ~dd->ipath_hwe_bitsextant));

	ctrl = ipath_read_kreg32(dd, dd->ipath_kregs->kr_control);
	if ((ctrl & INFINIPATH_C_FREEZEMODE) && !ipath_diag_inuse) {
		/*
		 * parity errors in send memory are recoverable,
		 * just cancel the send (if indicated in * sendbuffererror),
		 * count the occurrence, unfreeze (if no other handled
		 * hardware error bits are set), and continue. They can
		 * occur if a processor speculative read is done to the PIO
		 * buffer while we are sending a packet, for example.
		 */
		if ((hwerrs & TXE_PIO_PARITY) && ipath_ht_txe_recover(dd))
			hwerrs &= ~TXE_PIO_PARITY;
		if (hwerrs & RXE_EAGER_PARITY)
			ipath_dev_err(dd, "RXE parity, Eager TID error is not "
				"recoverable\n");
		if (!hwerrs) {
			ipath_dbg("Clearing freezemode on ignored or "
				  "recovered hardware error\n");
			ctrl &= ~INFINIPATH_C_FREEZEMODE;
			ipath_write_kreg(dd, dd->ipath_kregs->kr_control,
					 ctrl);
		}
	}

	*msg = '\0';

	/*
	 * may someday want to decode into which bits are which
	 * functional area for parity errors, etc.
	 */
	if (hwerrs & (infinipath_hwe_htcmemparityerr_mask
		      << INFINIPATH_HWE_HTCMEMPARITYERR_SHIFT)) {
		bits = (u32) ((hwerrs >>
			       INFINIPATH_HWE_HTCMEMPARITYERR_SHIFT) &
			      INFINIPATH_HWE_HTCMEMPARITYERR_MASK);
		snprintf(bitsmsg, sizeof bitsmsg, "[HTC Parity Errs %x] ",
			 bits);
		strlcat(msg, bitsmsg, msgl);
	}

	ipath_format_hwerrors(hwerrs,
			      ipath_6110_hwerror_msgs,
			      sizeof(ipath_6110_hwerror_msgs) /
			      sizeof(ipath_6110_hwerror_msgs[0]),
			      msg, msgl);

	if (hwerrs & (_IPATH_HTLINK0_CRCBITS | _IPATH_HTLINK1_CRCBITS))
		hwerr_crcbits(dd, hwerrs, msg, msgl);

	if (hwerrs & INFINIPATH_HWE_MEMBISTFAILED) {
		strlcat(msg, "[Memory BIST test failed, InfiniPath hardware unusable]",
			msgl);
		/* ignore from now on, so disable until driver reloaded */
		dd->ipath_hwerrmask &= ~INFINIPATH_HWE_MEMBISTFAILED;
		ipath_write_kreg(dd, dd->ipath_kregs->kr_hwerrmask,
				 dd->ipath_hwerrmask);
	}
#define _IPATH_PLL_FAIL (INFINIPATH_HWE_COREPLL_FBSLIP |	\
			 INFINIPATH_HWE_COREPLL_RFSLIP |	\
			 INFINIPATH_HWE_HTBPLL_FBSLIP |		\
			 INFINIPATH_HWE_HTBPLL_RFSLIP |		\
			 INFINIPATH_HWE_HTAPLL_FBSLIP |		\
			 INFINIPATH_HWE_HTAPLL_RFSLIP)

	if (hwerrs & _IPATH_PLL_FAIL) {
		snprintf(bitsmsg, sizeof bitsmsg,
			 "[PLL failed (%llx), InfiniPath hardware unusable]",
			 (unsigned long long) (hwerrs & _IPATH_PLL_FAIL));
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

	if (hwerrs) {
		/*
		 * if any set that we aren't ignoring; only
		 * make the complaint once, in case it's stuck
		 * or recurring, and we get here multiple
		 * times.
		 * force link down, so switch knows, and
		 * LEDs are turned off
		 */
		if (dd->ipath_flags & IPATH_INITTED) {
			ipath_set_linkstate(dd, IPATH_IB_LINKDOWN);
			ipath_setup_ht_setextled(dd,
				INFINIPATH_IBCS_L_STATE_DOWN,
				INFINIPATH_IBCS_LT_STATE_DISABLED);
			ipath_dev_err(dd, "Fatal Hardware Error (freeze "
					  "mode), no longer usable, SN %.16s\n",
					  dd->ipath_serial);
			isfatal = 1;
		}
		*dd->ipath_statusp &= ~IPATH_STATUS_IB_READY;
		/* mark as having had error */
		*dd->ipath_statusp |= IPATH_STATUS_HWERROR;
		/*
		 * mark as not usable, at a minimum until driver
		 * is reloaded, probably until reboot, since no
		 * other reset is possible.
		 */
		dd->ipath_flags &= ~IPATH_INITTED;
	}
	else
		*msg = 0; /* recovered from all of them */
	if (*msg)
		ipath_dev_err(dd, "%s hardware error\n", msg);
	if (isfatal && !ipath_diag_inuse && dd->ipath_freezemsg)
		/*
		 * for status file; if no trailing brace is copied,
		 * we'll know it was truncated.
		 */
		snprintf(dd->ipath_freezemsg,
			 dd->ipath_freezelen, "{%s}", msg);

bail:;
}

/**
 * ipath_ht_boardname - fill in the board name
 * @dd: the infinipath device
 * @name: the output buffer
 * @namelen: the size of the output buffer
 *
 * fill in the board name, based on the board revision register
 */
static int ipath_ht_boardname(struct ipath_devdata *dd, char *name,
			      size_t namelen)
{
	char *n = NULL;
	u8 boardrev = dd->ipath_boardrev;
	int ret;

	switch (boardrev) {
	case 4:		/* Ponderosa is one of the bringup boards */
		n = "Ponderosa";
		break;
	case 5:
		/*
		 * original production board; two production levels, with
		 * different serial number ranges.   See ipath_ht_early_init() for
		 * case where we enable IPATH_GPIO_INTR for later serial # range.
		 */
		n = "InfiniPath_QHT7040";
		break;
	case 6:
		n = "OEM_Board_3";
		break;
	case 7:
		/* small form factor production board */
		n = "InfiniPath_QHT7140";
		break;
	case 8:
		n = "LS/X-1";
		break;
	case 9:		/* Comstock bringup test board */
		n = "Comstock";
		break;
	case 10:
		n = "OEM_Board_2";
		break;
	case 11:
		n = "InfiniPath_HT-470"; /* obsoleted */
		break;
	case 12:
		n = "OEM_Board_4";
		break;
	default:		/* don't know, just print the number */
		ipath_dev_err(dd, "Don't yet know about board "
			      "with ID %u\n", boardrev);
		snprintf(name, namelen, "Unknown_InfiniPath_QHT7xxx_%u",
			 boardrev);
		break;
	}
	if (n)
		snprintf(name, namelen, "%s", n);

	if (dd->ipath_majrev != 3 || (dd->ipath_minrev < 2 ||
		dd->ipath_minrev > 3)) {
		/*
		 * This version of the driver only supports Rev 3.2 and 3.3
		 */
		ipath_dev_err(dd,
			      "Unsupported InfiniPath hardware revision %u.%u!\n",
			      dd->ipath_majrev, dd->ipath_minrev);
		ret = 1;
		goto bail;
	}
	/*
	 * pkt/word counters are 32 bit, and therefore wrap fast enough
	 * that we snapshot them from a timer, and maintain 64 bit shadow
	 * copies
	 */
	dd->ipath_flags |= IPATH_32BITCOUNTERS;
	if (dd->ipath_htspeed != 800)
		ipath_dev_err(dd,
			      "Incorrectly configured for HT @ %uMHz\n",
			      dd->ipath_htspeed);
	if (dd->ipath_boardrev == 7 || dd->ipath_boardrev == 11 ||
	    dd->ipath_boardrev == 6)
		dd->ipath_flags |= IPATH_GPIO_INTR;
	else
		dd->ipath_flags |= IPATH_POLL_RX_INTR;
	if (dd->ipath_boardrev == 8) {	/* LS/X-1 */
		u64 val;
		val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_extstatus);
		if (val & INFINIPATH_EXTS_SERDESSEL) {
			/*
			 * hardware disabled
			 *
			 * This means that the chip is hardware disabled,
			 * and will not be able to bring up the link,
			 * in any case.  We special case this and abort
			 * early, to avoid later messages.  We also set
			 * the DISABLED status bit
			 */
			ipath_dbg("Unit %u is hardware-disabled\n",
				  dd->ipath_unit);
			*dd->ipath_statusp |= IPATH_STATUS_DISABLED;
			/* this value is handled differently */
			ret = 2;
			goto bail;
		}
	}
	ret = 0;

bail:
	return ret;
}

static void ipath_check_htlink(struct ipath_devdata *dd)
{
	u8 linkerr, link_off, i;

	for (i = 0; i < 2; i++) {
		link_off = dd->ipath_ht_slave_off + i * 4 + 0xd;
		if (pci_read_config_byte(dd->pcidev, link_off, &linkerr))
			dev_info(&dd->pcidev->dev, "Couldn't read "
				 "linkerror%d of HT slave/primary block\n",
				 i);
		else if (linkerr & 0xf0) {
			ipath_cdbg(VERBOSE, "HT linkerr%d bits 0x%x set, "
				   "clearing\n", linkerr >> 4, i);
			/*
			 * writing the linkerr bits that are set should
			 * clear them
			 */
			if (pci_write_config_byte(dd->pcidev, link_off,
						  linkerr))
				ipath_dbg("Failed write to clear HT "
					  "linkerror%d\n", i);
			if (pci_read_config_byte(dd->pcidev, link_off,
						 &linkerr))
				dev_info(&dd->pcidev->dev,
					 "Couldn't reread linkerror%d of "
					 "HT slave/primary block\n", i);
			else if (linkerr & 0xf0)
				dev_info(&dd->pcidev->dev,
					 "HT linkerror%d bits 0x%x "
					 "couldn't be cleared\n",
					 i, linkerr >> 4);
		}
	}
}

static int ipath_setup_ht_reset(struct ipath_devdata *dd)
{
	ipath_dbg("No reset possible for this InfiniPath hardware\n");
	return 0;
}

#define HT_INTR_DISC_CONFIG  0x80	/* HT interrupt and discovery cap */
#define HT_INTR_REG_INDEX    2	/* intconfig requires indirect accesses */

/*
 * Bits 13-15 of command==0 is slave/primary block.  Clear any HT CRC
 * errors.  We only bother to do this at load time, because it's OK if
 * it happened before we were loaded (first time after boot/reset),
 * but any time after that, it's fatal anyway.  Also need to not check
 * for for upper byte errors if we are in 8 bit mode, so figure out
 * our width.  For now, at least, also complain if it's 8 bit.
 */
static void slave_or_pri_blk(struct ipath_devdata *dd, struct pci_dev *pdev,
			     int pos, u8 cap_type)
{
	u8 linkwidth = 0, linkerr, link_a_b_off, link_off;
	u16 linkctrl = 0;
	int i;

	dd->ipath_ht_slave_off = pos;
	/* command word, master_host bit */
	/* master host || slave */
	if ((cap_type >> 2) & 1)
		link_a_b_off = 4;
	else
		link_a_b_off = 0;
	ipath_cdbg(VERBOSE, "HT%u (Link %c) connected to processor\n",
		   link_a_b_off ? 1 : 0,
		   link_a_b_off ? 'B' : 'A');

	link_a_b_off += pos;

	/*
	 * check both link control registers; clear both HT CRC sets if
	 * necessary.
	 */
	for (i = 0; i < 2; i++) {
		link_off = pos + i * 4 + 0x4;
		if (pci_read_config_word(pdev, link_off, &linkctrl))
			ipath_dev_err(dd, "Couldn't read HT link control%d "
				      "register\n", i);
		else if (linkctrl & (0xf << 8)) {
			ipath_cdbg(VERBOSE, "Clear linkctrl%d CRC Error "
				   "bits %x\n", i, linkctrl & (0xf << 8));
			/*
			 * now write them back to clear the error.
			 */
			pci_write_config_byte(pdev, link_off,
					      linkctrl & (0xf << 8));
		}
	}

	/*
	 * As with HT CRC bits, same for protocol errors that might occur
	 * during boot.
	 */
	for (i = 0; i < 2; i++) {
		link_off = pos + i * 4 + 0xd;
		if (pci_read_config_byte(pdev, link_off, &linkerr))
			dev_info(&pdev->dev, "Couldn't read linkerror%d "
				 "of HT slave/primary block\n", i);
		else if (linkerr & 0xf0) {
			ipath_cdbg(VERBOSE, "HT linkerr%d bits 0x%x set, "
				   "clearing\n", linkerr >> 4, i);
			/*
			 * writing the linkerr bits that are set will clear
			 * them
			 */
			if (pci_write_config_byte
			    (pdev, link_off, linkerr))
				ipath_dbg("Failed write to clear HT "
					  "linkerror%d\n", i);
			if (pci_read_config_byte(pdev, link_off, &linkerr))
				dev_info(&pdev->dev, "Couldn't reread "
					 "linkerror%d of HT slave/primary "
					 "block\n", i);
			else if (linkerr & 0xf0)
				dev_info(&pdev->dev, "HT linkerror%d bits "
					 "0x%x couldn't be cleared\n",
					 i, linkerr >> 4);
		}
	}

	/*
	 * this is just for our link to the host, not devices connected
	 * through tunnel.
	 */

	if (pci_read_config_byte(pdev, link_a_b_off + 7, &linkwidth))
		ipath_dev_err(dd, "Couldn't read HT link width "
			      "config register\n");
	else {
		u32 width;
		switch (linkwidth & 7) {
		case 5:
			width = 4;
			break;
		case 4:
			width = 2;
			break;
		case 3:
			width = 32;
			break;
		case 1:
			width = 16;
			break;
		case 0:
		default:	/* if wrong, assume 8 bit */
			width = 8;
			break;
		}

		dd->ipath_htwidth = width;

		if (linkwidth != 0x11) {
			ipath_dev_err(dd, "Not configured for 16 bit HT "
				      "(%x)\n", linkwidth);
			if (!(linkwidth & 0xf)) {
				ipath_dbg("Will ignore HT lane1 errors\n");
				dd->ipath_flags |= IPATH_8BIT_IN_HT0;
			}
		}
	}

	/*
	 * this is just for our link to the host, not devices connected
	 * through tunnel.
	 */
	if (pci_read_config_byte(pdev, link_a_b_off + 0xd, &linkwidth))
		ipath_dev_err(dd, "Couldn't read HT link frequency "
			      "config register\n");
	else {
		u32 speed;
		switch (linkwidth & 0xf) {
		case 6:
			speed = 1000;
			break;
		case 5:
			speed = 800;
			break;
		case 4:
			speed = 600;
			break;
		case 3:
			speed = 500;
			break;
		case 2:
			speed = 400;
			break;
		case 1:
			speed = 300;
			break;
		default:
			/*
			 * assume reserved and vendor-specific are 200...
			 */
		case 0:
			speed = 200;
			break;
		}
		dd->ipath_htspeed = speed;
	}
}

static int ipath_ht_intconfig(struct ipath_devdata *dd)
{
	int ret;

	if (dd->ipath_intconfig) {
		ipath_write_kreg(dd, dd->ipath_kregs->kr_interruptconfig,
				 dd->ipath_intconfig);	/* interrupt address */
		ret = 0;
	} else {
		ipath_dev_err(dd, "No interrupts enabled, couldn't setup "
			      "interrupt address\n");
		ret = -EINVAL;
	}

	return ret;
}

static void ipath_ht_irq_update(struct pci_dev *dev, int irq,
				struct ht_irq_msg *msg)
{
	struct ipath_devdata *dd = pci_get_drvdata(dev);
	u64 prev_intconfig = dd->ipath_intconfig;

	dd->ipath_intconfig = msg->address_lo;
	dd->ipath_intconfig |= ((u64) msg->address_hi) << 32;

	/*
	 * If the previous value of dd->ipath_intconfig is zero, we're
	 * getting configured for the first time, and must not program the
	 * intconfig register here (it will be programmed later, when the
	 * hardware is ready).  Otherwise, we should.
	 */
	if (prev_intconfig)
		ipath_ht_intconfig(dd);
}

/**
 * ipath_setup_ht_config - setup the interruptconfig register
 * @dd: the infinipath device
 * @pdev: the PCI device
 *
 * setup the interruptconfig register from the HT config info.
 * Also clear CRC errors in HT linkcontrol, if necessary.
 * This is done only for the real hardware.  It is done before
 * chip address space is initted, so can't touch infinipath registers
 */
static int ipath_setup_ht_config(struct ipath_devdata *dd,
				 struct pci_dev *pdev)
{
	int pos, ret;

	ret = __ht_create_irq(pdev, 0, ipath_ht_irq_update);
	if (ret < 0) {
		ipath_dev_err(dd, "Couldn't create interrupt handler: "
			      "err %d\n", ret);
		goto bail;
	}
	dd->ipath_irq = ret;
	ret = 0;

	/*
	 * Handle clearing CRC errors in linkctrl register if necessary.  We
	 * do this early, before we ever enable errors or hardware errors,
	 * mostly to avoid causing the chip to enter freeze mode.
	 */
	pos = pci_find_capability(pdev, PCI_CAP_ID_HT);
	if (!pos) {
		ipath_dev_err(dd, "Couldn't find HyperTransport "
			      "capability; no interrupts\n");
		ret = -ENODEV;
		goto bail;
	}
	do {
		u8 cap_type;

		/* the HT capability type byte is 3 bytes after the
		 * capability byte.
		 */
		if (pci_read_config_byte(pdev, pos + 3, &cap_type)) {
			dev_info(&pdev->dev, "Couldn't read config "
				 "command @ %d\n", pos);
			continue;
		}
		if (!(cap_type & 0xE0))
			slave_or_pri_blk(dd, pdev, pos, cap_type);
	} while ((pos = pci_find_next_capability(pdev, pos,
						 PCI_CAP_ID_HT)));

bail:
	return ret;
}

/**
 * ipath_setup_ht_cleanup - clean up any per-chip chip-specific stuff
 * @dd: the infinipath device
 *
 * Called during driver unload.
 * This is currently a nop for the HT chip, not for all chips
 */
static void ipath_setup_ht_cleanup(struct ipath_devdata *dd)
{
}

/**
 * ipath_setup_ht_setextled - set the state of the two external LEDs
 * @dd: the infinipath device
 * @lst: the L state
 * @ltst: the LT state
 *
 * Set the state of the two external LEDs, to indicate physical and
 * logical state of IB link.   For this chip (at least with recommended
 * board pinouts), LED1 is Green (physical state), and LED2 is Yellow
 * (logical state)
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
static void ipath_setup_ht_setextled(struct ipath_devdata *dd,
				     u64 lst, u64 ltst)
{
	u64 extctl;

	/* the diags use the LED to indicate diag info, so we leave
	 * the external LED alone when the diags are running */
	if (ipath_diag_inuse)
		return;

	/*
	 * start by setting both LED control bits to off, then turn
	 * on the appropriate bit(s).
	 */
	if (dd->ipath_boardrev == 8) { /* LS/X-1 uses different pins */
		/*
		 * major difference is that INFINIPATH_EXTC_LEDGBLERR_OFF
		 * is inverted,  because it is normally used to indicate
		 * a hardware fault at reset, if there were errors
		 */
		extctl = (dd->ipath_extctrl & ~INFINIPATH_EXTC_LEDGBLOK_ON)
			| INFINIPATH_EXTC_LEDGBLERR_OFF;
		if (ltst == INFINIPATH_IBCS_LT_STATE_LINKUP)
			extctl &= ~INFINIPATH_EXTC_LEDGBLERR_OFF;
		if (lst == INFINIPATH_IBCS_L_STATE_ACTIVE)
			extctl |= INFINIPATH_EXTC_LEDGBLOK_ON;
	}
	else {
		extctl = dd->ipath_extctrl &
			~(INFINIPATH_EXTC_LED1PRIPORT_ON |
			  INFINIPATH_EXTC_LED2PRIPORT_ON);
		if (ltst == INFINIPATH_IBCS_LT_STATE_LINKUP)
			extctl |= INFINIPATH_EXTC_LED1PRIPORT_ON;
		if (lst == INFINIPATH_IBCS_L_STATE_ACTIVE)
			extctl |= INFINIPATH_EXTC_LED2PRIPORT_ON;
	}
	dd->ipath_extctrl = extctl;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_extctrl, extctl);
}

static void ipath_init_ht_variables(struct ipath_devdata *dd)
{
	dd->ipath_gpio_sda_num = _IPATH_GPIO_SDA_NUM;
	dd->ipath_gpio_scl_num = _IPATH_GPIO_SCL_NUM;
	dd->ipath_gpio_sda = IPATH_GPIO_SDA;
	dd->ipath_gpio_scl = IPATH_GPIO_SCL;

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

	dd->ipath_hwe_bitsextant =
		(INFINIPATH_HWE_HTCMEMPARITYERR_MASK <<
		 INFINIPATH_HWE_HTCMEMPARITYERR_SHIFT) |
		(INFINIPATH_HWE_TXEMEMPARITYERR_MASK <<
		 INFINIPATH_HWE_TXEMEMPARITYERR_SHIFT) |
		(INFINIPATH_HWE_RXEMEMPARITYERR_MASK <<
		 INFINIPATH_HWE_RXEMEMPARITYERR_SHIFT) |
		INFINIPATH_HWE_HTCLNKABYTE0CRCERR |
		INFINIPATH_HWE_HTCLNKABYTE1CRCERR |
		INFINIPATH_HWE_HTCLNKBBYTE0CRCERR |
		INFINIPATH_HWE_HTCLNKBBYTE1CRCERR |
		INFINIPATH_HWE_HTCMISCERR4 |
		INFINIPATH_HWE_HTCMISCERR5 | INFINIPATH_HWE_HTCMISCERR6 |
		INFINIPATH_HWE_HTCMISCERR7 |
		INFINIPATH_HWE_HTCBUSTREQPARITYERR |
		INFINIPATH_HWE_HTCBUSTRESPPARITYERR |
		INFINIPATH_HWE_HTCBUSIREQPARITYERR |
		INFINIPATH_HWE_RXDSYNCMEMPARITYERR |
		INFINIPATH_HWE_MEMBISTFAILED |
		INFINIPATH_HWE_COREPLL_FBSLIP |
		INFINIPATH_HWE_COREPLL_RFSLIP |
		INFINIPATH_HWE_HTBPLL_FBSLIP |
		INFINIPATH_HWE_HTBPLL_RFSLIP |
		INFINIPATH_HWE_HTAPLL_FBSLIP |
		INFINIPATH_HWE_HTAPLL_RFSLIP |
		INFINIPATH_HWE_SERDESPLLFAILED |
		INFINIPATH_HWE_IBCBUSTOSPCPARITYERR |
		INFINIPATH_HWE_IBCBUSFRSPCPARITYERR;

	dd->ipath_i_rcvavail_mask = INFINIPATH_I_RCVAVAIL_MASK;
	dd->ipath_i_rcvurg_mask = INFINIPATH_I_RCVURG_MASK;
}

/**
 * ipath_ht_init_hwerrors - enable hardware errors
 * @dd: the infinipath device
 *
 * now that we have finished initializing everything that might reasonably
 * cause a hardware error, and cleared those errors bits as they occur,
 * we can enable hardware errors in the mask (potentially enabling
 * freeze mode), and enable hardware errors as errors (along with
 * everything else) in errormask
 */
static void ipath_ht_init_hwerrors(struct ipath_devdata *dd)
{
	ipath_err_t val;
	u64 extsval;

	extsval = ipath_read_kreg64(dd, dd->ipath_kregs->kr_extstatus);

	if (!(extsval & INFINIPATH_EXTS_MEMBIST_ENDTEST))
		ipath_dev_err(dd, "MemBIST did not complete!\n");
	if (extsval & INFINIPATH_EXTS_MEMBIST_CORRECT)
		ipath_dbg("MemBIST corrected\n");

	ipath_check_htlink(dd);

	/* barring bugs, all hwerrors become interrupts, which can */
	val = -1LL;
	/* don't look at crc lane1 if 8 bit */
	if (dd->ipath_flags & IPATH_8BIT_IN_HT0)
		val &= ~infinipath_hwe_htclnkabyte1crcerr;
	/* don't look at crc lane1 if 8 bit */
	if (dd->ipath_flags & IPATH_8BIT_IN_HT1)
		val &= ~infinipath_hwe_htclnkbbyte1crcerr;

	/*
	 * disable RXDSYNCMEMPARITY because external serdes is unused,
	 * and therefore the logic will never be used or initialized,
	 * and uninitialized state will normally result in this error
	 * being asserted.  Similarly for the external serdess pll
	 * lock signal.
	 */
	val &= ~(INFINIPATH_HWE_SERDESPLLFAILED |
		 INFINIPATH_HWE_RXDSYNCMEMPARITYERR);

	/*
	 * Disable MISCERR4 because of an inversion in the HT core
	 * logic checking for errors that cause this bit to be set.
	 * The errata can also cause the protocol error bit to be set
	 * in the HT config space linkerror register(s).
	 */
	val &= ~INFINIPATH_HWE_HTCMISCERR4;

	/*
	 * PLL ignored because MDIO interface has a logic problem
	 * for reads, on Comstock and Ponderosa.  BRINGUP
	 */
	if (dd->ipath_boardrev == 4 || dd->ipath_boardrev == 9)
		val &= ~INFINIPATH_HWE_SERDESPLLFAILED;
	dd->ipath_hwerrmask = val;
}

/**
 * ipath_ht_bringup_serdes - bring up the serdes
 * @dd: the infinipath device
 */
static int ipath_ht_bringup_serdes(struct ipath_devdata *dd)
{
	u64 val, config1;
	int ret = 0, change = 0;

	ipath_dbg("Trying to bringup serdes\n");

	if (ipath_read_kreg64(dd, dd->ipath_kregs->kr_hwerrstatus) &
	    INFINIPATH_HWE_SERDESPLLFAILED)
	{
		ipath_dbg("At start, serdes PLL failed bit set in "
			  "hwerrstatus, clearing and continuing\n");
		ipath_write_kreg(dd, dd->ipath_kregs->kr_hwerrclear,
				 INFINIPATH_HWE_SERDESPLLFAILED);
	}

	val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_serdesconfig0);
	config1 = ipath_read_kreg64(dd, dd->ipath_kregs->kr_serdesconfig1);

	ipath_cdbg(VERBOSE, "Initial serdes status is config0=%llx "
		   "config1=%llx, sstatus=%llx xgxs %llx\n",
		   (unsigned long long) val, (unsigned long long) config1,
		   (unsigned long long)
		   ipath_read_kreg64(dd, dd->ipath_kregs->kr_serdesstatus),
		   (unsigned long long)
		   ipath_read_kreg64(dd, dd->ipath_kregs->kr_xgxsconfig));

	/* force reset on */
	val |= INFINIPATH_SERDC0_RESET_PLL
		/* | INFINIPATH_SERDC0_RESET_MASK */
		;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_serdesconfig0, val);
	udelay(15);		/* need pll reset set at least for a bit */

	if (val & INFINIPATH_SERDC0_RESET_PLL) {
		u64 val2 = val &= ~INFINIPATH_SERDC0_RESET_PLL;
		/* set lane resets, and tx idle, during pll reset */
		val2 |= INFINIPATH_SERDC0_RESET_MASK |
			INFINIPATH_SERDC0_TXIDLE;
		ipath_cdbg(VERBOSE, "Clearing serdes PLL reset (writing "
			   "%llx)\n", (unsigned long long) val2);
		ipath_write_kreg(dd, dd->ipath_kregs->kr_serdesconfig0,
				 val2);
		/*
		 * be sure chip saw it
		 */
		val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);
		/*
		 * need pll reset clear at least 11 usec before lane
		 * resets cleared; give it a few more
		 */
		udelay(15);
		val = val2;	/* for check below */
	}

	if (val & (INFINIPATH_SERDC0_RESET_PLL |
		   INFINIPATH_SERDC0_RESET_MASK |
		   INFINIPATH_SERDC0_TXIDLE)) {
		val &= ~(INFINIPATH_SERDC0_RESET_PLL |
			 INFINIPATH_SERDC0_RESET_MASK |
			 INFINIPATH_SERDC0_TXIDLE);
		/* clear them */
		ipath_write_kreg(dd, dd->ipath_kregs->kr_serdesconfig0,
				 val);
	}

	val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_xgxsconfig);
	if (((val >> INFINIPATH_XGXS_MDIOADDR_SHIFT) &
	     INFINIPATH_XGXS_MDIOADDR_MASK) != 3) {
		val &= ~(INFINIPATH_XGXS_MDIOADDR_MASK <<
			 INFINIPATH_XGXS_MDIOADDR_SHIFT);
		/*
		 * we use address 3
		 */
		val |= 3ULL << INFINIPATH_XGXS_MDIOADDR_SHIFT;
		change = 1;
	}
	if (val & INFINIPATH_XGXS_RESET) {
		/* normally true after boot */
		val &= ~INFINIPATH_XGXS_RESET;
		change = 1;
	}
	if (((val >> INFINIPATH_XGXS_RX_POL_SHIFT) &
	     INFINIPATH_XGXS_RX_POL_MASK) != dd->ipath_rx_pol_inv ) {
		/* need to compensate for Tx inversion in partner */
		val &= ~(INFINIPATH_XGXS_RX_POL_MASK <<
		         INFINIPATH_XGXS_RX_POL_SHIFT);
		val |= dd->ipath_rx_pol_inv <<
			INFINIPATH_XGXS_RX_POL_SHIFT;
		change = 1;
	}
	if (change)
		ipath_write_kreg(dd, dd->ipath_kregs->kr_xgxsconfig, val);

	val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_serdesconfig0);

	/* clear current and de-emphasis bits */
	config1 &= ~0x0ffffffff00ULL;
	/* set current to 20ma */
	config1 |= 0x00000000000ULL;
	/* set de-emphasis to -5.68dB */
	config1 |= 0x0cccc000000ULL;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_serdesconfig1, config1);

	ipath_cdbg(VERBOSE, "After setup: serdes status is config0=%llx "
		   "config1=%llx, sstatus=%llx xgxs %llx\n",
		   (unsigned long long) val, (unsigned long long) config1,
		   (unsigned long long)
		   ipath_read_kreg64(dd, dd->ipath_kregs->kr_serdesstatus),
		   (unsigned long long)
		   ipath_read_kreg64(dd, dd->ipath_kregs->kr_xgxsconfig));

	if (!ipath_waitfor_mdio_cmdready(dd)) {
		ipath_write_kreg(dd, dd->ipath_kregs->kr_mdio,
				 ipath_mdio_req(IPATH_MDIO_CMD_READ, 31,
						IPATH_MDIO_CTRL_XGXS_REG_8,
						0));
		if (ipath_waitfor_complete(dd, dd->ipath_kregs->kr_mdio,
					   IPATH_MDIO_DATAVALID, &val))
			ipath_dbg("Never got MDIO data for XGXS status "
				  "read\n");
		else
			ipath_cdbg(VERBOSE, "MDIO Read reg8, "
				   "'bank' 31 %x\n", (u32) val);
	} else
		ipath_dbg("Never got MDIO cmdready for XGXS status read\n");

	return ret;		/* for now, say we always succeeded */
}

/**
 * ipath_ht_quiet_serdes - set serdes to txidle
 * @dd: the infinipath device
 * driver is being unloaded
 */
static void ipath_ht_quiet_serdes(struct ipath_devdata *dd)
{
	u64 val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_serdesconfig0);

	val |= INFINIPATH_SERDC0_TXIDLE;
	ipath_dbg("Setting TxIdleEn on serdes (config0 = %llx)\n",
		  (unsigned long long) val);
	ipath_write_kreg(dd, dd->ipath_kregs->kr_serdesconfig0, val);
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
static void ipath_ht_put_tid(struct ipath_devdata *dd,
			     u64 __iomem *tidptr, u32 type,
			     unsigned long pa)
{
	if (!dd->ipath_kregbase)
		return;

	if (pa != dd->ipath_tidinvalid) {
		if (unlikely((pa & ~INFINIPATH_RT_ADDR_MASK))) {
			dev_info(&dd->pcidev->dev,
				 "physaddr %lx has more than "
				 "40 bits, using only 40!!!\n", pa);
			pa &= INFINIPATH_RT_ADDR_MASK;
		}
		if (type == 0)
			pa |= dd->ipath_tidtemplate;
		else {
			/* in words (fixed, full page).  */
			u64 lenvalid = PAGE_SIZE >> 2;
			lenvalid <<= INFINIPATH_RT_BUFSIZE_SHIFT;
			pa |= lenvalid | INFINIPATH_RT_VALID;
		}
	}
	writeq(pa, tidptr);
}


/**
 * ipath_ht_clear_tid - clear all TID entries for a port, expected and eager
 * @dd: the infinipath device
 * @port: the port
 *
 * Used from ipath_close(), and at chip initialization.
 */
static void ipath_ht_clear_tids(struct ipath_devdata *dd, unsigned port)
{
	u64 __iomem *tidbase;
	int i;

	if (!dd->ipath_kregbase)
		return;

	ipath_cdbg(VERBOSE, "Invalidate TIDs for port %u\n", port);

	/*
	 * need to invalidate all of the expected TID entries for this
	 * port, so we don't have valid entries that might somehow get
	 * used (early in next use of this port, or through some bug)
	 */
	tidbase = (u64 __iomem *) ((char __iomem *)(dd->ipath_kregbase) +
				   dd->ipath_rcvtidbase +
				   port * dd->ipath_rcvtidcnt *
				   sizeof(*tidbase));
	for (i = 0; i < dd->ipath_rcvtidcnt; i++)
		ipath_ht_put_tid(dd, &tidbase[i], 1, dd->ipath_tidinvalid);

	tidbase = (u64 __iomem *) ((char __iomem *)(dd->ipath_kregbase) +
				   dd->ipath_rcvegrbase +
				   port * dd->ipath_rcvegrcnt *
				   sizeof(*tidbase));

	for (i = 0; i < dd->ipath_rcvegrcnt; i++)
		ipath_ht_put_tid(dd, &tidbase[i], 0, dd->ipath_tidinvalid);
}

/**
 * ipath_ht_tidtemplate - setup constants for TID updates
 * @dd: the infinipath device
 *
 * We setup stuff that we use a lot, to avoid calculating each time
 */
static void ipath_ht_tidtemplate(struct ipath_devdata *dd)
{
	dd->ipath_tidtemplate = dd->ipath_ibmaxlen >> 2;
	dd->ipath_tidtemplate <<= INFINIPATH_RT_BUFSIZE_SHIFT;
	dd->ipath_tidtemplate |= INFINIPATH_RT_VALID;

	/*
	 * work around chip errata bug 7358, by marking invalid tids
	 * as having max length
	 */
	dd->ipath_tidinvalid = (-1LL & INFINIPATH_RT_BUFSIZE_MASK) <<
		INFINIPATH_RT_BUFSIZE_SHIFT;
}

static int ipath_ht_early_init(struct ipath_devdata *dd)
{
	u32 __iomem *piobuf;
	u32 pioincr, val32;
	int i;

	/*
	 * one cache line; long IB headers will spill over into received
	 * buffer
	 */
	dd->ipath_rcvhdrentsize = 16;
	dd->ipath_rcvhdrsize = IPATH_DFLT_RCVHDRSIZE;

	/*
	 * For HT, we allocate a somewhat overly large eager buffer,
	 * such that we can guarantee that we can receive the largest
	 * packet that we can send out.  To truly support a 4KB MTU,
	 * we need to bump this to a large value.  To date, other than
	 * testing, we have never encountered an HCA that can really
	 * send 4KB MTU packets, so we do not handle that (we'll get
	 * errors interrupts if we ever see one).
	 */
	dd->ipath_rcvegrbufsize = dd->ipath_piosize2k;

	/*
	 * the min() check here is currently a nop, but it may not
	 * always be, depending on just how we do ipath_rcvegrbufsize
	 */
	dd->ipath_ibmaxlen = min(dd->ipath_piosize2k,
				 dd->ipath_rcvegrbufsize);
	dd->ipath_init_ibmaxlen = dd->ipath_ibmaxlen;
	ipath_ht_tidtemplate(dd);

	/*
	 * zero all the TID entries at startup.  We do this for sanity,
	 * in case of a previous driver crash of some kind, and also
	 * because the chip powers up with these memories in an unknown
	 * state.  Use portcnt, not cfgports, since this is for the
	 * full chip, not for current (possibly different) configuration
	 * value.
	 * Chip Errata bug 6447
	 */
	for (val32 = 0; val32 < dd->ipath_portcnt; val32++)
		ipath_ht_clear_tids(dd, val32);

	/*
	 * write the pbc of each buffer, to be sure it's initialized, then
	 * cancel all the buffers, and also abort any packets that might
	 * have been in flight for some reason (the latter is for driver
	 * unload/reload, but isn't a bad idea at first init).	PIO send
	 * isn't enabled at this point, so there is no danger of sending
	 * these out on the wire.
	 * Chip Errata bug 6610
	 */
	piobuf = (u32 __iomem *) (((char __iomem *)(dd->ipath_kregbase)) +
				  dd->ipath_piobufbase);
	pioincr = dd->ipath_palign / sizeof(*piobuf);
	for (i = 0; i < dd->ipath_piobcnt2k; i++) {
		/*
		 * reasonable word count, just to init pbc
		 */
		writel(16, piobuf);
		piobuf += pioincr;
	}
	/*
	 * self-clearing
	 */
	ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl,
			 INFINIPATH_S_ABORT);

	ipath_get_eeprom_info(dd);
	if (dd->ipath_boardrev == 5 && dd->ipath_serial[0] == '1' &&
		dd->ipath_serial[1] == '2' && dd->ipath_serial[2] == '8') {
		/*
		 * Later production QHT7040 has same changes as QHT7140, so
		 * can use GPIO interrupts.  They have serial #'s starting
		 * with 128, rather than 112.
		 */
		dd->ipath_flags |= IPATH_GPIO_INTR;
		dd->ipath_flags &= ~IPATH_POLL_RX_INTR;
	}
	return 0;
}


static int ipath_ht_txe_recover(struct ipath_devdata *dd)
{
	int cnt = ++ipath_stats.sps_txeparity;
	if (cnt >= IPATH_MAX_PARITY_ATTEMPTS)  {
		if (cnt == IPATH_MAX_PARITY_ATTEMPTS)
			ipath_dev_err(dd,
				"Too many attempts to recover from "
				"TXE parity, giving up\n");
		return 0;
	}
	dev_info(&dd->pcidev->dev,
		"Recovering from TXE PIO parity error\n");
	ipath_disarm_senderrbufs(dd, 1);
	return 1;
}


/**
 * ipath_init_ht_get_base_info - set chip-specific flags for user code
 * @dd: the infinipath device
 * @kbase: ipath_base_info pointer
 *
 * We set the PCIE flag because the lower bandwidth on PCIe vs
 * HyperTransport can affect some user packet algorithms.
 */
static int ipath_ht_get_base_info(struct ipath_portdata *pd, void *kbase)
{
	struct ipath_base_info *kinfo = kbase;

	kinfo->spi_runtime_flags |= IPATH_RUNTIME_HT |
		IPATH_RUNTIME_RCVHDR_COPY;

	return 0;
}

static void ipath_ht_free_irq(struct ipath_devdata *dd)
{
	free_irq(dd->ipath_irq, dd);
	ht_destroy_irq(dd->ipath_irq);
	dd->ipath_irq = 0;
	dd->ipath_intconfig = 0;
}

/**
 * ipath_init_iba6110_funcs - set up the chip-specific function pointers
 * @dd: the infinipath device
 *
 * This is global, and is called directly at init to set up the
 * chip-specific function pointers for later use.
 */
void ipath_init_iba6110_funcs(struct ipath_devdata *dd)
{
	dd->ipath_f_intrsetup = ipath_ht_intconfig;
	dd->ipath_f_bus = ipath_setup_ht_config;
	dd->ipath_f_reset = ipath_setup_ht_reset;
	dd->ipath_f_get_boardname = ipath_ht_boardname;
	dd->ipath_f_init_hwerrors = ipath_ht_init_hwerrors;
	dd->ipath_f_early_init = ipath_ht_early_init;
	dd->ipath_f_handle_hwerrors = ipath_ht_handle_hwerrors;
	dd->ipath_f_quiet_serdes = ipath_ht_quiet_serdes;
	dd->ipath_f_bringup_serdes = ipath_ht_bringup_serdes;
	dd->ipath_f_clear_tids = ipath_ht_clear_tids;
	dd->ipath_f_put_tid = ipath_ht_put_tid;
	dd->ipath_f_cleanup = ipath_setup_ht_cleanup;
	dd->ipath_f_setextled = ipath_setup_ht_setextled;
	dd->ipath_f_get_base_info = ipath_ht_get_base_info;
	dd->ipath_f_free_irq = ipath_ht_free_irq;

	/*
	 * initialize chip-specific variables
	 */
	dd->ipath_f_tidtemplate = ipath_ht_tidtemplate;

	/*
	 * setup the register offsets, since they are different for each
	 * chip
	 */
	dd->ipath_kregs = &ipath_ht_kregs;
	dd->ipath_cregs = &ipath_ht_cregs;

	/*
	 * do very early init that is needed before ipath_f_bus is
	 * called
	 */
	ipath_init_ht_variables(dd);
}
