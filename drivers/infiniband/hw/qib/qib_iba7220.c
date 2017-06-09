/*
 * Copyright (c) 2006, 2007, 2008, 2009, 2010 QLogic Corporation.
 * All rights reserved.
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
 * QLogic_IB 7220 chip (except that specific to the SerDes)
 */

#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/io.h>
#include <rdma/ib_verbs.h>

#include "qib.h"
#include "qib_7220.h"

static void qib_setup_7220_setextled(struct qib_pportdata *, u32);
static void qib_7220_handle_hwerrors(struct qib_devdata *, char *, size_t);
static void sendctrl_7220_mod(struct qib_pportdata *ppd, u32 op);
static u32 qib_7220_iblink_state(u64);
static u8 qib_7220_phys_portstate(u64);
static void qib_sdma_update_7220_tail(struct qib_pportdata *, u16);
static void qib_set_ib_7220_lstate(struct qib_pportdata *, u16, u16);

/*
 * This file contains almost all the chip-specific register information and
 * access functions for the QLogic QLogic_IB 7220 PCI-Express chip, with the
 * exception of SerDes support, which in in qib_sd7220.c.
 */

/* Below uses machine-generated qib_chipnum_regs.h file */
#define KREG_IDX(regname) (QIB_7220_##regname##_OFFS / sizeof(u64))

/* Use defines to tie machine-generated names to lower-case names */
#define kr_control KREG_IDX(Control)
#define kr_counterregbase KREG_IDX(CntrRegBase)
#define kr_errclear KREG_IDX(ErrClear)
#define kr_errmask KREG_IDX(ErrMask)
#define kr_errstatus KREG_IDX(ErrStatus)
#define kr_extctrl KREG_IDX(EXTCtrl)
#define kr_extstatus KREG_IDX(EXTStatus)
#define kr_gpio_clear KREG_IDX(GPIOClear)
#define kr_gpio_mask KREG_IDX(GPIOMask)
#define kr_gpio_out KREG_IDX(GPIOOut)
#define kr_gpio_status KREG_IDX(GPIOStatus)
#define kr_hrtbt_guid KREG_IDX(HRTBT_GUID)
#define kr_hwdiagctrl KREG_IDX(HwDiagCtrl)
#define kr_hwerrclear KREG_IDX(HwErrClear)
#define kr_hwerrmask KREG_IDX(HwErrMask)
#define kr_hwerrstatus KREG_IDX(HwErrStatus)
#define kr_ibcctrl KREG_IDX(IBCCtrl)
#define kr_ibcddrctrl KREG_IDX(IBCDDRCtrl)
#define kr_ibcddrstatus KREG_IDX(IBCDDRStatus)
#define kr_ibcstatus KREG_IDX(IBCStatus)
#define kr_ibserdesctrl KREG_IDX(IBSerDesCtrl)
#define kr_intclear KREG_IDX(IntClear)
#define kr_intmask KREG_IDX(IntMask)
#define kr_intstatus KREG_IDX(IntStatus)
#define kr_ncmodectrl KREG_IDX(IBNCModeCtrl)
#define kr_palign KREG_IDX(PageAlign)
#define kr_partitionkey KREG_IDX(RcvPartitionKey)
#define kr_portcnt KREG_IDX(PortCnt)
#define kr_rcvbthqp KREG_IDX(RcvBTHQP)
#define kr_rcvctrl KREG_IDX(RcvCtrl)
#define kr_rcvegrbase KREG_IDX(RcvEgrBase)
#define kr_rcvegrcnt KREG_IDX(RcvEgrCnt)
#define kr_rcvhdrcnt KREG_IDX(RcvHdrCnt)
#define kr_rcvhdrentsize KREG_IDX(RcvHdrEntSize)
#define kr_rcvhdrsize KREG_IDX(RcvHdrSize)
#define kr_rcvpktledcnt KREG_IDX(RcvPktLEDCnt)
#define kr_rcvtidbase KREG_IDX(RcvTIDBase)
#define kr_rcvtidcnt KREG_IDX(RcvTIDCnt)
#define kr_revision KREG_IDX(Revision)
#define kr_scratch KREG_IDX(Scratch)
#define kr_sendbuffererror KREG_IDX(SendBufErr0)
#define kr_sendctrl KREG_IDX(SendCtrl)
#define kr_senddmabase KREG_IDX(SendDmaBase)
#define kr_senddmabufmask0 KREG_IDX(SendDmaBufMask0)
#define kr_senddmabufmask1 (KREG_IDX(SendDmaBufMask0) + 1)
#define kr_senddmabufmask2 (KREG_IDX(SendDmaBufMask0) + 2)
#define kr_senddmahead KREG_IDX(SendDmaHead)
#define kr_senddmaheadaddr KREG_IDX(SendDmaHeadAddr)
#define kr_senddmalengen KREG_IDX(SendDmaLenGen)
#define kr_senddmastatus KREG_IDX(SendDmaStatus)
#define kr_senddmatail KREG_IDX(SendDmaTail)
#define kr_sendpioavailaddr KREG_IDX(SendBufAvailAddr)
#define kr_sendpiobufbase KREG_IDX(SendBufBase)
#define kr_sendpiobufcnt KREG_IDX(SendBufCnt)
#define kr_sendpiosize KREG_IDX(SendBufSize)
#define kr_sendregbase KREG_IDX(SendRegBase)
#define kr_userregbase KREG_IDX(UserRegBase)
#define kr_xgxs_cfg KREG_IDX(XGXSCfg)

/* These must only be written via qib_write_kreg_ctxt() */
#define kr_rcvhdraddr KREG_IDX(RcvHdrAddr0)
#define kr_rcvhdrtailaddr KREG_IDX(RcvHdrTailAddr0)


#define CREG_IDX(regname) ((QIB_7220_##regname##_OFFS - \
			QIB_7220_LBIntCnt_OFFS) / sizeof(u64))

#define cr_badformat CREG_IDX(RxVersionErrCnt)
#define cr_erricrc CREG_IDX(RxICRCErrCnt)
#define cr_errlink CREG_IDX(RxLinkMalformCnt)
#define cr_errlpcrc CREG_IDX(RxLPCRCErrCnt)
#define cr_errpkey CREG_IDX(RxPKeyMismatchCnt)
#define cr_rcvflowctrl_err CREG_IDX(RxFlowCtrlViolCnt)
#define cr_err_rlen CREG_IDX(RxLenErrCnt)
#define cr_errslen CREG_IDX(TxLenErrCnt)
#define cr_errtidfull CREG_IDX(RxTIDFullErrCnt)
#define cr_errtidvalid CREG_IDX(RxTIDValidErrCnt)
#define cr_errvcrc CREG_IDX(RxVCRCErrCnt)
#define cr_ibstatuschange CREG_IDX(IBStatusChangeCnt)
#define cr_lbint CREG_IDX(LBIntCnt)
#define cr_invalidrlen CREG_IDX(RxMaxMinLenErrCnt)
#define cr_invalidslen CREG_IDX(TxMaxMinLenErrCnt)
#define cr_lbflowstall CREG_IDX(LBFlowStallCnt)
#define cr_pktrcv CREG_IDX(RxDataPktCnt)
#define cr_pktrcvflowctrl CREG_IDX(RxFlowPktCnt)
#define cr_pktsend CREG_IDX(TxDataPktCnt)
#define cr_pktsendflow CREG_IDX(TxFlowPktCnt)
#define cr_portovfl CREG_IDX(RxP0HdrEgrOvflCnt)
#define cr_rcvebp CREG_IDX(RxEBPCnt)
#define cr_rcvovfl CREG_IDX(RxBufOvflCnt)
#define cr_senddropped CREG_IDX(TxDroppedPktCnt)
#define cr_sendstall CREG_IDX(TxFlowStallCnt)
#define cr_sendunderrun CREG_IDX(TxUnderrunCnt)
#define cr_wordrcv CREG_IDX(RxDwordCnt)
#define cr_wordsend CREG_IDX(TxDwordCnt)
#define cr_txunsupvl CREG_IDX(TxUnsupVLErrCnt)
#define cr_rxdroppkt CREG_IDX(RxDroppedPktCnt)
#define cr_iblinkerrrecov CREG_IDX(IBLinkErrRecoveryCnt)
#define cr_iblinkdown CREG_IDX(IBLinkDownedCnt)
#define cr_ibsymbolerr CREG_IDX(IBSymbolErrCnt)
#define cr_vl15droppedpkt CREG_IDX(RxVL15DroppedPktCnt)
#define cr_rxotherlocalphyerr CREG_IDX(RxOtherLocalPhyErrCnt)
#define cr_excessbufferovfl CREG_IDX(ExcessBufferOvflCnt)
#define cr_locallinkintegrityerr CREG_IDX(LocalLinkIntegrityErrCnt)
#define cr_rxvlerr CREG_IDX(RxVlErrCnt)
#define cr_rxdlidfltr CREG_IDX(RxDlidFltrCnt)
#define cr_psstat CREG_IDX(PSStat)
#define cr_psstart CREG_IDX(PSStart)
#define cr_psinterval CREG_IDX(PSInterval)
#define cr_psrcvdatacount CREG_IDX(PSRcvDataCount)
#define cr_psrcvpktscount CREG_IDX(PSRcvPktsCount)
#define cr_psxmitdatacount CREG_IDX(PSXmitDataCount)
#define cr_psxmitpktscount CREG_IDX(PSXmitPktsCount)
#define cr_psxmitwaitcount CREG_IDX(PSXmitWaitCount)
#define cr_txsdmadesc CREG_IDX(TxSDmaDescCnt)
#define cr_pcieretrydiag CREG_IDX(PcieRetryBufDiagQwordCnt)

#define SYM_RMASK(regname, fldname) ((u64)              \
	QIB_7220_##regname##_##fldname##_RMASK)
#define SYM_MASK(regname, fldname) ((u64)               \
	QIB_7220_##regname##_##fldname##_RMASK <<       \
	 QIB_7220_##regname##_##fldname##_LSB)
#define SYM_LSB(regname, fldname) (QIB_7220_##regname##_##fldname##_LSB)
#define SYM_FIELD(value, regname, fldname) ((u64) \
	(((value) >> SYM_LSB(regname, fldname)) & \
	 SYM_RMASK(regname, fldname)))
#define ERR_MASK(fldname) SYM_MASK(ErrMask, fldname##Mask)
#define HWE_MASK(fldname) SYM_MASK(HwErrMask, fldname##Mask)

/* ibcctrl bits */
#define QLOGIC_IB_IBCC_LINKINITCMD_DISABLE 1
/* cycle through TS1/TS2 till OK */
#define QLOGIC_IB_IBCC_LINKINITCMD_POLL 2
/* wait for TS1, then go on */
#define QLOGIC_IB_IBCC_LINKINITCMD_SLEEP 3
#define QLOGIC_IB_IBCC_LINKINITCMD_SHIFT 16

#define QLOGIC_IB_IBCC_LINKCMD_DOWN 1           /* move to 0x11 */
#define QLOGIC_IB_IBCC_LINKCMD_ARMED 2          /* move to 0x21 */
#define QLOGIC_IB_IBCC_LINKCMD_ACTIVE 3 /* move to 0x31 */

#define BLOB_7220_IBCHG 0x81

/*
 * We could have a single register get/put routine, that takes a group type,
 * but this is somewhat clearer and cleaner.  It also gives us some error
 * checking.  64 bit register reads should always work, but are inefficient
 * on opteron (the northbridge always generates 2 separate HT 32 bit reads),
 * so we use kreg32 wherever possible.  User register and counter register
 * reads are always 32 bit reads, so only one form of those routines.
 */

/**
 * qib_read_ureg32 - read 32-bit virtualized per-context register
 * @dd: device
 * @regno: register number
 * @ctxt: context number
 *
 * Return the contents of a register that is virtualized to be per context.
 * Returns -1 on errors (not distinguishable from valid contents at
 * runtime; we may add a separate error variable at some point).
 */
static inline u32 qib_read_ureg32(const struct qib_devdata *dd,
				  enum qib_ureg regno, int ctxt)
{
	if (!dd->kregbase || !(dd->flags & QIB_PRESENT))
		return 0;

	if (dd->userbase)
		return readl(regno + (u64 __iomem *)
			     ((char __iomem *)dd->userbase +
			      dd->ureg_align * ctxt));
	else
		return readl(regno + (u64 __iomem *)
			     (dd->uregbase +
			      (char __iomem *)dd->kregbase +
			      dd->ureg_align * ctxt));
}

/**
 * qib_write_ureg - write 32-bit virtualized per-context register
 * @dd: device
 * @regno: register number
 * @value: value
 * @ctxt: context
 *
 * Write the contents of a register that is virtualized to be per context.
 */
static inline void qib_write_ureg(const struct qib_devdata *dd,
				  enum qib_ureg regno, u64 value, int ctxt)
{
	u64 __iomem *ubase;

	if (dd->userbase)
		ubase = (u64 __iomem *)
			((char __iomem *) dd->userbase +
			 dd->ureg_align * ctxt);
	else
		ubase = (u64 __iomem *)
			(dd->uregbase +
			 (char __iomem *) dd->kregbase +
			 dd->ureg_align * ctxt);

	if (dd->kregbase && (dd->flags & QIB_PRESENT))
		writeq(value, &ubase[regno]);
}

/**
 * qib_write_kreg_ctxt - write a device's per-ctxt 64-bit kernel register
 * @dd: the qlogic_ib device
 * @regno: the register number to write
 * @ctxt: the context containing the register
 * @value: the value to write
 */
static inline void qib_write_kreg_ctxt(const struct qib_devdata *dd,
				       const u16 regno, unsigned ctxt,
				       u64 value)
{
	qib_write_kreg(dd, regno + ctxt, value);
}

static inline void write_7220_creg(const struct qib_devdata *dd,
				   u16 regno, u64 value)
{
	if (dd->cspec->cregbase && (dd->flags & QIB_PRESENT))
		writeq(value, &dd->cspec->cregbase[regno]);
}

static inline u64 read_7220_creg(const struct qib_devdata *dd, u16 regno)
{
	if (!dd->cspec->cregbase || !(dd->flags & QIB_PRESENT))
		return 0;
	return readq(&dd->cspec->cregbase[regno]);
}

static inline u32 read_7220_creg32(const struct qib_devdata *dd, u16 regno)
{
	if (!dd->cspec->cregbase || !(dd->flags & QIB_PRESENT))
		return 0;
	return readl(&dd->cspec->cregbase[regno]);
}

/* kr_revision bits */
#define QLOGIC_IB_R_EMULATORREV_MASK ((1ULL << 22) - 1)
#define QLOGIC_IB_R_EMULATORREV_SHIFT 40

/* kr_control bits */
#define QLOGIC_IB_C_RESET (1U << 7)

/* kr_intstatus, kr_intclear, kr_intmask bits */
#define QLOGIC_IB_I_RCVURG_MASK ((1ULL << 17) - 1)
#define QLOGIC_IB_I_RCVURG_SHIFT 32
#define QLOGIC_IB_I_RCVAVAIL_MASK ((1ULL << 17) - 1)
#define QLOGIC_IB_I_RCVAVAIL_SHIFT 0
#define QLOGIC_IB_I_SERDESTRIMDONE (1ULL << 27)

#define QLOGIC_IB_C_FREEZEMODE 0x00000002
#define QLOGIC_IB_C_LINKENABLE 0x00000004

#define QLOGIC_IB_I_SDMAINT             0x8000000000000000ULL
#define QLOGIC_IB_I_SDMADISABLED        0x4000000000000000ULL
#define QLOGIC_IB_I_ERROR               0x0000000080000000ULL
#define QLOGIC_IB_I_SPIOSENT            0x0000000040000000ULL
#define QLOGIC_IB_I_SPIOBUFAVAIL        0x0000000020000000ULL
#define QLOGIC_IB_I_GPIO                0x0000000010000000ULL

/* variables for sanity checking interrupt and errors */
#define QLOGIC_IB_I_BITSEXTANT \
		(QLOGIC_IB_I_SDMAINT | QLOGIC_IB_I_SDMADISABLED | \
		(QLOGIC_IB_I_RCVURG_MASK << QLOGIC_IB_I_RCVURG_SHIFT) | \
		(QLOGIC_IB_I_RCVAVAIL_MASK << \
		 QLOGIC_IB_I_RCVAVAIL_SHIFT) | \
		QLOGIC_IB_I_ERROR | QLOGIC_IB_I_SPIOSENT | \
		QLOGIC_IB_I_SPIOBUFAVAIL | QLOGIC_IB_I_GPIO | \
		QLOGIC_IB_I_SERDESTRIMDONE)

#define IB_HWE_BITSEXTANT \
	       (HWE_MASK(RXEMemParityErr) | \
		HWE_MASK(TXEMemParityErr) | \
		(QLOGIC_IB_HWE_PCIEMEMPARITYERR_MASK <<	 \
		 QLOGIC_IB_HWE_PCIEMEMPARITYERR_SHIFT) | \
		QLOGIC_IB_HWE_PCIE1PLLFAILED | \
		QLOGIC_IB_HWE_PCIE0PLLFAILED | \
		QLOGIC_IB_HWE_PCIEPOISONEDTLP | \
		QLOGIC_IB_HWE_PCIECPLTIMEOUT | \
		QLOGIC_IB_HWE_PCIEBUSPARITYXTLH | \
		QLOGIC_IB_HWE_PCIEBUSPARITYXADM | \
		QLOGIC_IB_HWE_PCIEBUSPARITYRADM | \
		HWE_MASK(PowerOnBISTFailed) |	  \
		QLOGIC_IB_HWE_COREPLL_FBSLIP | \
		QLOGIC_IB_HWE_COREPLL_RFSLIP | \
		QLOGIC_IB_HWE_SERDESPLLFAILED | \
		HWE_MASK(IBCBusToSPCParityErr) | \
		HWE_MASK(IBCBusFromSPCParityErr) | \
		QLOGIC_IB_HWE_PCIECPLDATAQUEUEERR | \
		QLOGIC_IB_HWE_PCIECPLHDRQUEUEERR | \
		QLOGIC_IB_HWE_SDMAMEMREADERR | \
		QLOGIC_IB_HWE_CLK_UC_PLLNOTLOCKED | \
		QLOGIC_IB_HWE_PCIESERDESQ0PCLKNOTDETECT | \
		QLOGIC_IB_HWE_PCIESERDESQ1PCLKNOTDETECT | \
		QLOGIC_IB_HWE_PCIESERDESQ2PCLKNOTDETECT | \
		QLOGIC_IB_HWE_PCIESERDESQ3PCLKNOTDETECT | \
		QLOGIC_IB_HWE_DDSRXEQMEMORYPARITYERR | \
		QLOGIC_IB_HWE_IB_UC_MEMORYPARITYERR | \
		QLOGIC_IB_HWE_PCIE_UC_OCT0MEMORYPARITYERR | \
		QLOGIC_IB_HWE_PCIE_UC_OCT1MEMORYPARITYERR)

#define IB_E_BITSEXTANT							\
	(ERR_MASK(RcvFormatErr) | ERR_MASK(RcvVCRCErr) |		\
	 ERR_MASK(RcvICRCErr) | ERR_MASK(RcvMinPktLenErr) |		\
	 ERR_MASK(RcvMaxPktLenErr) | ERR_MASK(RcvLongPktLenErr) |	\
	 ERR_MASK(RcvShortPktLenErr) | ERR_MASK(RcvUnexpectedCharErr) | \
	 ERR_MASK(RcvUnsupportedVLErr) | ERR_MASK(RcvEBPErr) |		\
	 ERR_MASK(RcvIBFlowErr) | ERR_MASK(RcvBadVersionErr) |		\
	 ERR_MASK(RcvEgrFullErr) | ERR_MASK(RcvHdrFullErr) |		\
	 ERR_MASK(RcvBadTidErr) | ERR_MASK(RcvHdrLenErr) |		\
	 ERR_MASK(RcvHdrErr) | ERR_MASK(RcvIBLostLinkErr) |		\
	 ERR_MASK(SendSpecialTriggerErr) |				\
	 ERR_MASK(SDmaDisabledErr) | ERR_MASK(SendMinPktLenErr) |	\
	 ERR_MASK(SendMaxPktLenErr) | ERR_MASK(SendUnderRunErr) |	\
	 ERR_MASK(SendPktLenErr) | ERR_MASK(SendDroppedSmpPktErr) |	\
	 ERR_MASK(SendDroppedDataPktErr) |				\
	 ERR_MASK(SendPioArmLaunchErr) |				\
	 ERR_MASK(SendUnexpectedPktNumErr) |				\
	 ERR_MASK(SendUnsupportedVLErr) | ERR_MASK(SendBufMisuseErr) |	\
	 ERR_MASK(SDmaGenMismatchErr) | ERR_MASK(SDmaOutOfBoundErr) |	\
	 ERR_MASK(SDmaTailOutOfBoundErr) | ERR_MASK(SDmaBaseErr) |	\
	 ERR_MASK(SDma1stDescErr) | ERR_MASK(SDmaRpyTagErr) |		\
	 ERR_MASK(SDmaDwEnErr) | ERR_MASK(SDmaMissingDwErr) |		\
	 ERR_MASK(SDmaUnexpDataErr) |					\
	 ERR_MASK(IBStatusChanged) | ERR_MASK(InvalidAddrErr) |		\
	 ERR_MASK(ResetNegated) | ERR_MASK(HardwareErr) |		\
	 ERR_MASK(SDmaDescAddrMisalignErr) |				\
	 ERR_MASK(InvalidEEPCmd))

/* kr_hwerrclear, kr_hwerrmask, kr_hwerrstatus, bits */
#define QLOGIC_IB_HWE_PCIEMEMPARITYERR_MASK  0x00000000000000ffULL
#define QLOGIC_IB_HWE_PCIEMEMPARITYERR_SHIFT 0
#define QLOGIC_IB_HWE_PCIEPOISONEDTLP      0x0000000010000000ULL
#define QLOGIC_IB_HWE_PCIECPLTIMEOUT       0x0000000020000000ULL
#define QLOGIC_IB_HWE_PCIEBUSPARITYXTLH    0x0000000040000000ULL
#define QLOGIC_IB_HWE_PCIEBUSPARITYXADM    0x0000000080000000ULL
#define QLOGIC_IB_HWE_PCIEBUSPARITYRADM    0x0000000100000000ULL
#define QLOGIC_IB_HWE_COREPLL_FBSLIP       0x0080000000000000ULL
#define QLOGIC_IB_HWE_COREPLL_RFSLIP       0x0100000000000000ULL
#define QLOGIC_IB_HWE_PCIE1PLLFAILED       0x0400000000000000ULL
#define QLOGIC_IB_HWE_PCIE0PLLFAILED       0x0800000000000000ULL
#define QLOGIC_IB_HWE_SERDESPLLFAILED      0x1000000000000000ULL
/* specific to this chip */
#define QLOGIC_IB_HWE_PCIECPLDATAQUEUEERR         0x0000000000000040ULL
#define QLOGIC_IB_HWE_PCIECPLHDRQUEUEERR          0x0000000000000080ULL
#define QLOGIC_IB_HWE_SDMAMEMREADERR              0x0000000010000000ULL
#define QLOGIC_IB_HWE_CLK_UC_PLLNOTLOCKED          0x2000000000000000ULL
#define QLOGIC_IB_HWE_PCIESERDESQ0PCLKNOTDETECT   0x0100000000000000ULL
#define QLOGIC_IB_HWE_PCIESERDESQ1PCLKNOTDETECT   0x0200000000000000ULL
#define QLOGIC_IB_HWE_PCIESERDESQ2PCLKNOTDETECT   0x0400000000000000ULL
#define QLOGIC_IB_HWE_PCIESERDESQ3PCLKNOTDETECT   0x0800000000000000ULL
#define QLOGIC_IB_HWE_DDSRXEQMEMORYPARITYERR       0x0000008000000000ULL
#define QLOGIC_IB_HWE_IB_UC_MEMORYPARITYERR        0x0000004000000000ULL
#define QLOGIC_IB_HWE_PCIE_UC_OCT0MEMORYPARITYERR 0x0000001000000000ULL
#define QLOGIC_IB_HWE_PCIE_UC_OCT1MEMORYPARITYERR 0x0000002000000000ULL

#define IBA7220_IBCC_LINKCMD_SHIFT 19

/* kr_ibcddrctrl bits */
#define IBA7220_IBC_DLIDLMC_MASK        0xFFFFFFFFUL
#define IBA7220_IBC_DLIDLMC_SHIFT       32

#define IBA7220_IBC_HRTBT_MASK  (SYM_RMASK(IBCDDRCtrl, HRTBT_AUTO) | \
				 SYM_RMASK(IBCDDRCtrl, HRTBT_ENB))
#define IBA7220_IBC_HRTBT_SHIFT SYM_LSB(IBCDDRCtrl, HRTBT_ENB)

#define IBA7220_IBC_LANE_REV_SUPPORTED (1<<8)
#define IBA7220_IBC_LREV_MASK   1
#define IBA7220_IBC_LREV_SHIFT  8
#define IBA7220_IBC_RXPOL_MASK  1
#define IBA7220_IBC_RXPOL_SHIFT 7
#define IBA7220_IBC_WIDTH_SHIFT 5
#define IBA7220_IBC_WIDTH_MASK  0x3
#define IBA7220_IBC_WIDTH_1X_ONLY       (0 << IBA7220_IBC_WIDTH_SHIFT)
#define IBA7220_IBC_WIDTH_4X_ONLY       (1 << IBA7220_IBC_WIDTH_SHIFT)
#define IBA7220_IBC_WIDTH_AUTONEG       (2 << IBA7220_IBC_WIDTH_SHIFT)
#define IBA7220_IBC_SPEED_AUTONEG       (1 << 1)
#define IBA7220_IBC_SPEED_SDR           (1 << 2)
#define IBA7220_IBC_SPEED_DDR           (1 << 3)
#define IBA7220_IBC_SPEED_AUTONEG_MASK  (0x7 << 1)
#define IBA7220_IBC_IBTA_1_2_MASK       (1)

/* kr_ibcddrstatus */
/* link latency shift is 0, don't bother defining */
#define IBA7220_DDRSTAT_LINKLAT_MASK    0x3ffffff

/* kr_extstatus bits */
#define QLOGIC_IB_EXTS_FREQSEL 0x2
#define QLOGIC_IB_EXTS_SERDESSEL 0x4
#define QLOGIC_IB_EXTS_MEMBIST_ENDTEST     0x0000000000004000
#define QLOGIC_IB_EXTS_MEMBIST_DISABLED    0x0000000000008000

/* kr_xgxsconfig bits */
#define QLOGIC_IB_XGXS_RESET          0x5ULL
#define QLOGIC_IB_XGXS_FC_SAFE        (1ULL << 63)

/* kr_rcvpktledcnt */
#define IBA7220_LEDBLINK_ON_SHIFT 32 /* 4ns period on after packet */
#define IBA7220_LEDBLINK_OFF_SHIFT 0 /* 4ns period off before next on */

#define _QIB_GPIO_SDA_NUM 1
#define _QIB_GPIO_SCL_NUM 0
#define QIB_TWSI_EEPROM_DEV 0xA2 /* All Production 7220 cards. */
#define QIB_TWSI_TEMP_DEV 0x98

/* HW counter clock is at 4nsec */
#define QIB_7220_PSXMITWAIT_CHECK_RATE 4000

#define IBA7220_R_INTRAVAIL_SHIFT 17
#define IBA7220_R_PKEY_DIS_SHIFT 34
#define IBA7220_R_TAILUPD_SHIFT 35
#define IBA7220_R_CTXTCFG_SHIFT 36

#define IBA7220_HDRHEAD_PKTINT_SHIFT 32 /* interrupt cnt in upper 32 bits */

/*
 * the size bits give us 2^N, in KB units.  0 marks as invalid,
 * and 7 is reserved.  We currently use only 2KB and 4KB
 */
#define IBA7220_TID_SZ_SHIFT 37 /* shift to 3bit size selector */
#define IBA7220_TID_SZ_2K (1UL << IBA7220_TID_SZ_SHIFT) /* 2KB */
#define IBA7220_TID_SZ_4K (2UL << IBA7220_TID_SZ_SHIFT) /* 4KB */
#define IBA7220_TID_PA_SHIFT 11U /* TID addr in chip stored w/o low bits */
#define PBC_7220_VL15_SEND (1ULL << 63) /* pbc; VL15, no credit check */
#define PBC_7220_VL15_SEND_CTRL (1ULL << 31) /* control version of same */

#define AUTONEG_TRIES 5 /* sequential retries to negotiate DDR */

/* packet rate matching delay multiplier */
static u8 rate_to_delay[2][2] = {
	/* 1x, 4x */
	{   8, 2 }, /* SDR */
	{   4, 1 }  /* DDR */
};

static u8 ib_rate_to_delay[IB_RATE_120_GBPS + 1] = {
	[IB_RATE_2_5_GBPS] = 8,
	[IB_RATE_5_GBPS] = 4,
	[IB_RATE_10_GBPS] = 2,
	[IB_RATE_20_GBPS] = 1
};

#define IBA7220_LINKSPEED_SHIFT SYM_LSB(IBCStatus, LinkSpeedActive)
#define IBA7220_LINKWIDTH_SHIFT SYM_LSB(IBCStatus, LinkWidthActive)

/* link training states, from IBC */
#define IB_7220_LT_STATE_DISABLED        0x00
#define IB_7220_LT_STATE_LINKUP          0x01
#define IB_7220_LT_STATE_POLLACTIVE      0x02
#define IB_7220_LT_STATE_POLLQUIET       0x03
#define IB_7220_LT_STATE_SLEEPDELAY      0x04
#define IB_7220_LT_STATE_SLEEPQUIET      0x05
#define IB_7220_LT_STATE_CFGDEBOUNCE     0x08
#define IB_7220_LT_STATE_CFGRCVFCFG      0x09
#define IB_7220_LT_STATE_CFGWAITRMT      0x0a
#define IB_7220_LT_STATE_CFGIDLE 0x0b
#define IB_7220_LT_STATE_RECOVERRETRAIN  0x0c
#define IB_7220_LT_STATE_RECOVERWAITRMT  0x0e
#define IB_7220_LT_STATE_RECOVERIDLE     0x0f

/* link state machine states from IBC */
#define IB_7220_L_STATE_DOWN             0x0
#define IB_7220_L_STATE_INIT             0x1
#define IB_7220_L_STATE_ARM              0x2
#define IB_7220_L_STATE_ACTIVE           0x3
#define IB_7220_L_STATE_ACT_DEFER        0x4

static const u8 qib_7220_physportstate[0x20] = {
	[IB_7220_LT_STATE_DISABLED] = IB_PHYSPORTSTATE_DISABLED,
	[IB_7220_LT_STATE_LINKUP] = IB_PHYSPORTSTATE_LINKUP,
	[IB_7220_LT_STATE_POLLACTIVE] = IB_PHYSPORTSTATE_POLL,
	[IB_7220_LT_STATE_POLLQUIET] = IB_PHYSPORTSTATE_POLL,
	[IB_7220_LT_STATE_SLEEPDELAY] = IB_PHYSPORTSTATE_SLEEP,
	[IB_7220_LT_STATE_SLEEPQUIET] = IB_PHYSPORTSTATE_SLEEP,
	[IB_7220_LT_STATE_CFGDEBOUNCE] =
		IB_PHYSPORTSTATE_CFG_TRAIN,
	[IB_7220_LT_STATE_CFGRCVFCFG] =
		IB_PHYSPORTSTATE_CFG_TRAIN,
	[IB_7220_LT_STATE_CFGWAITRMT] =
		IB_PHYSPORTSTATE_CFG_TRAIN,
	[IB_7220_LT_STATE_CFGIDLE] = IB_PHYSPORTSTATE_CFG_TRAIN,
	[IB_7220_LT_STATE_RECOVERRETRAIN] =
		IB_PHYSPORTSTATE_LINK_ERR_RECOVER,
	[IB_7220_LT_STATE_RECOVERWAITRMT] =
		IB_PHYSPORTSTATE_LINK_ERR_RECOVER,
	[IB_7220_LT_STATE_RECOVERIDLE] =
		IB_PHYSPORTSTATE_LINK_ERR_RECOVER,
	[0x10] = IB_PHYSPORTSTATE_CFG_TRAIN,
	[0x11] = IB_PHYSPORTSTATE_CFG_TRAIN,
	[0x12] = IB_PHYSPORTSTATE_CFG_TRAIN,
	[0x13] = IB_PHYSPORTSTATE_CFG_TRAIN,
	[0x14] = IB_PHYSPORTSTATE_CFG_TRAIN,
	[0x15] = IB_PHYSPORTSTATE_CFG_TRAIN,
	[0x16] = IB_PHYSPORTSTATE_CFG_TRAIN,
	[0x17] = IB_PHYSPORTSTATE_CFG_TRAIN
};

int qib_special_trigger;
module_param_named(special_trigger, qib_special_trigger, int, S_IRUGO);
MODULE_PARM_DESC(special_trigger, "Enable SpecialTrigger arm/launch");

#define IBCBUSFRSPCPARITYERR HWE_MASK(IBCBusFromSPCParityErr)
#define IBCBUSTOSPCPARITYERR HWE_MASK(IBCBusToSPCParityErr)

#define SYM_MASK_BIT(regname, fldname, bit) ((u64) \
	(1ULL << (SYM_LSB(regname, fldname) + (bit))))

#define TXEMEMPARITYERR_PIOBUF \
	SYM_MASK_BIT(HwErrMask, TXEMemParityErrMask, 0)
#define TXEMEMPARITYERR_PIOPBC \
	SYM_MASK_BIT(HwErrMask, TXEMemParityErrMask, 1)
#define TXEMEMPARITYERR_PIOLAUNCHFIFO \
	SYM_MASK_BIT(HwErrMask, TXEMemParityErrMask, 2)

#define RXEMEMPARITYERR_RCVBUF \
	SYM_MASK_BIT(HwErrMask, RXEMemParityErrMask, 0)
#define RXEMEMPARITYERR_LOOKUPQ \
	SYM_MASK_BIT(HwErrMask, RXEMemParityErrMask, 1)
#define RXEMEMPARITYERR_EXPTID \
	SYM_MASK_BIT(HwErrMask, RXEMemParityErrMask, 2)
#define RXEMEMPARITYERR_EAGERTID \
	SYM_MASK_BIT(HwErrMask, RXEMemParityErrMask, 3)
#define RXEMEMPARITYERR_FLAGBUF \
	SYM_MASK_BIT(HwErrMask, RXEMemParityErrMask, 4)
#define RXEMEMPARITYERR_DATAINFO \
	SYM_MASK_BIT(HwErrMask, RXEMemParityErrMask, 5)
#define RXEMEMPARITYERR_HDRINFO \
	SYM_MASK_BIT(HwErrMask, RXEMemParityErrMask, 6)

/* 7220 specific hardware errors... */
static const struct qib_hwerror_msgs qib_7220_hwerror_msgs[] = {
	/* generic hardware errors */
	QLOGIC_IB_HWE_MSG(IBCBUSFRSPCPARITYERR, "QIB2IB Parity"),
	QLOGIC_IB_HWE_MSG(IBCBUSTOSPCPARITYERR, "IB2QIB Parity"),

	QLOGIC_IB_HWE_MSG(TXEMEMPARITYERR_PIOBUF,
			  "TXE PIOBUF Memory Parity"),
	QLOGIC_IB_HWE_MSG(TXEMEMPARITYERR_PIOPBC,
			  "TXE PIOPBC Memory Parity"),
	QLOGIC_IB_HWE_MSG(TXEMEMPARITYERR_PIOLAUNCHFIFO,
			  "TXE PIOLAUNCHFIFO Memory Parity"),

	QLOGIC_IB_HWE_MSG(RXEMEMPARITYERR_RCVBUF,
			  "RXE RCVBUF Memory Parity"),
	QLOGIC_IB_HWE_MSG(RXEMEMPARITYERR_LOOKUPQ,
			  "RXE LOOKUPQ Memory Parity"),
	QLOGIC_IB_HWE_MSG(RXEMEMPARITYERR_EAGERTID,
			  "RXE EAGERTID Memory Parity"),
	QLOGIC_IB_HWE_MSG(RXEMEMPARITYERR_EXPTID,
			  "RXE EXPTID Memory Parity"),
	QLOGIC_IB_HWE_MSG(RXEMEMPARITYERR_FLAGBUF,
			  "RXE FLAGBUF Memory Parity"),
	QLOGIC_IB_HWE_MSG(RXEMEMPARITYERR_DATAINFO,
			  "RXE DATAINFO Memory Parity"),
	QLOGIC_IB_HWE_MSG(RXEMEMPARITYERR_HDRINFO,
			  "RXE HDRINFO Memory Parity"),

	/* chip-specific hardware errors */
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_PCIEPOISONEDTLP,
			  "PCIe Poisoned TLP"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_PCIECPLTIMEOUT,
			  "PCIe completion timeout"),
	/*
	 * In practice, it's unlikely wthat we'll see PCIe PLL, or bus
	 * parity or memory parity error failures, because most likely we
	 * won't be able to talk to the core of the chip.  Nonetheless, we
	 * might see them, if they are in parts of the PCIe core that aren't
	 * essential.
	 */
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_PCIE1PLLFAILED,
			  "PCIePLL1"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_PCIE0PLLFAILED,
			  "PCIePLL0"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_PCIEBUSPARITYXTLH,
			  "PCIe XTLH core parity"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_PCIEBUSPARITYXADM,
			  "PCIe ADM TX core parity"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_PCIEBUSPARITYRADM,
			  "PCIe ADM RX core parity"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_SERDESPLLFAILED,
			  "SerDes PLL"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_PCIECPLDATAQUEUEERR,
			  "PCIe cpl header queue"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_PCIECPLHDRQUEUEERR,
			  "PCIe cpl data queue"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_SDMAMEMREADERR,
			  "Send DMA memory read"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_CLK_UC_PLLNOTLOCKED,
			  "uC PLL clock not locked"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_PCIESERDESQ0PCLKNOTDETECT,
			  "PCIe serdes Q0 no clock"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_PCIESERDESQ1PCLKNOTDETECT,
			  "PCIe serdes Q1 no clock"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_PCIESERDESQ2PCLKNOTDETECT,
			  "PCIe serdes Q2 no clock"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_PCIESERDESQ3PCLKNOTDETECT,
			  "PCIe serdes Q3 no clock"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_DDSRXEQMEMORYPARITYERR,
			  "DDS RXEQ memory parity"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_IB_UC_MEMORYPARITYERR,
			  "IB uC memory parity"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_PCIE_UC_OCT0MEMORYPARITYERR,
			  "PCIe uC oct0 memory parity"),
	QLOGIC_IB_HWE_MSG(QLOGIC_IB_HWE_PCIE_UC_OCT1MEMORYPARITYERR,
			  "PCIe uC oct1 memory parity"),
};

#define RXE_PARITY (RXEMEMPARITYERR_EAGERTID|RXEMEMPARITYERR_EXPTID)

#define QLOGIC_IB_E_PKTERRS (\
		ERR_MASK(SendPktLenErr) |				\
		ERR_MASK(SendDroppedDataPktErr) |			\
		ERR_MASK(RcvVCRCErr) |					\
		ERR_MASK(RcvICRCErr) |					\
		ERR_MASK(RcvShortPktLenErr) |				\
		ERR_MASK(RcvEBPErr))

/* Convenience for decoding Send DMA errors */
#define QLOGIC_IB_E_SDMAERRS ( \
		ERR_MASK(SDmaGenMismatchErr) |				\
		ERR_MASK(SDmaOutOfBoundErr) |				\
		ERR_MASK(SDmaTailOutOfBoundErr) | ERR_MASK(SDmaBaseErr) | \
		ERR_MASK(SDma1stDescErr) | ERR_MASK(SDmaRpyTagErr) |	\
		ERR_MASK(SDmaDwEnErr) | ERR_MASK(SDmaMissingDwErr) |	\
		ERR_MASK(SDmaUnexpDataErr) |				\
		ERR_MASK(SDmaDescAddrMisalignErr) |			\
		ERR_MASK(SDmaDisabledErr) |				\
		ERR_MASK(SendBufMisuseErr))

/* These are all rcv-related errors which we want to count for stats */
#define E_SUM_PKTERRS \
	(ERR_MASK(RcvHdrLenErr) | ERR_MASK(RcvBadTidErr) |		\
	 ERR_MASK(RcvBadVersionErr) | ERR_MASK(RcvHdrErr) |		\
	 ERR_MASK(RcvLongPktLenErr) | ERR_MASK(RcvShortPktLenErr) |	\
	 ERR_MASK(RcvMaxPktLenErr) | ERR_MASK(RcvMinPktLenErr) |	\
	 ERR_MASK(RcvFormatErr) | ERR_MASK(RcvUnsupportedVLErr) |	\
	 ERR_MASK(RcvUnexpectedCharErr) | ERR_MASK(RcvEBPErr))

/* These are all send-related errors which we want to count for stats */
#define E_SUM_ERRS \
	(ERR_MASK(SendPioArmLaunchErr) | ERR_MASK(SendUnexpectedPktNumErr) | \
	 ERR_MASK(SendDroppedDataPktErr) | ERR_MASK(SendDroppedSmpPktErr) | \
	 ERR_MASK(SendMaxPktLenErr) | ERR_MASK(SendUnsupportedVLErr) |	\
	 ERR_MASK(SendMinPktLenErr) | ERR_MASK(SendPktLenErr) |		\
	 ERR_MASK(InvalidAddrErr))

/*
 * this is similar to E_SUM_ERRS, but can't ignore armlaunch, don't ignore
 * errors not related to freeze and cancelling buffers.  Can't ignore
 * armlaunch because could get more while still cleaning up, and need
 * to cancel those as they happen.
 */
#define E_SPKT_ERRS_IGNORE \
	(ERR_MASK(SendDroppedDataPktErr) | ERR_MASK(SendDroppedSmpPktErr) | \
	 ERR_MASK(SendMaxPktLenErr) | ERR_MASK(SendMinPktLenErr) |	\
	 ERR_MASK(SendPktLenErr))

/*
 * these are errors that can occur when the link changes state while
 * a packet is being sent or received.  This doesn't cover things
 * like EBP or VCRC that can be the result of a sending having the
 * link change state, so we receive a "known bad" packet.
 */
#define E_SUM_LINK_PKTERRS \
	(ERR_MASK(SendDroppedDataPktErr) | ERR_MASK(SendDroppedSmpPktErr) | \
	 ERR_MASK(SendMinPktLenErr) | ERR_MASK(SendPktLenErr) |		\
	 ERR_MASK(RcvShortPktLenErr) | ERR_MASK(RcvMinPktLenErr) |	\
	 ERR_MASK(RcvUnexpectedCharErr))

static void autoneg_7220_work(struct work_struct *);
static u32 __iomem *qib_7220_getsendbuf(struct qib_pportdata *, u64, u32 *);

/*
 * Called when we might have an error that is specific to a particular
 * PIO buffer, and may need to cancel that buffer, so it can be re-used.
 * because we don't need to force the update of pioavail.
 */
static void qib_disarm_7220_senderrbufs(struct qib_pportdata *ppd)
{
	unsigned long sbuf[3];
	struct qib_devdata *dd = ppd->dd;

	/*
	 * It's possible that sendbuffererror could have bits set; might
	 * have already done this as a result of hardware error handling.
	 */
	/* read these before writing errorclear */
	sbuf[0] = qib_read_kreg64(dd, kr_sendbuffererror);
	sbuf[1] = qib_read_kreg64(dd, kr_sendbuffererror + 1);
	sbuf[2] = qib_read_kreg64(dd, kr_sendbuffererror + 2);

	if (sbuf[0] || sbuf[1] || sbuf[2])
		qib_disarm_piobufs_set(dd, sbuf,
				       dd->piobcnt2k + dd->piobcnt4k);
}

static void qib_7220_txe_recover(struct qib_devdata *dd)
{
	qib_devinfo(dd->pcidev, "Recovering from TXE PIO parity error\n");
	qib_disarm_7220_senderrbufs(dd->pport);
}

/*
 * This is called with interrupts disabled and sdma_lock held.
 */
static void qib_7220_sdma_sendctrl(struct qib_pportdata *ppd, unsigned op)
{
	struct qib_devdata *dd = ppd->dd;
	u64 set_sendctrl = 0;
	u64 clr_sendctrl = 0;

	if (op & QIB_SDMA_SENDCTRL_OP_ENABLE)
		set_sendctrl |= SYM_MASK(SendCtrl, SDmaEnable);
	else
		clr_sendctrl |= SYM_MASK(SendCtrl, SDmaEnable);

	if (op & QIB_SDMA_SENDCTRL_OP_INTENABLE)
		set_sendctrl |= SYM_MASK(SendCtrl, SDmaIntEnable);
	else
		clr_sendctrl |= SYM_MASK(SendCtrl, SDmaIntEnable);

	if (op & QIB_SDMA_SENDCTRL_OP_HALT)
		set_sendctrl |= SYM_MASK(SendCtrl, SDmaHalt);
	else
		clr_sendctrl |= SYM_MASK(SendCtrl, SDmaHalt);

	spin_lock(&dd->sendctrl_lock);

	dd->sendctrl |= set_sendctrl;
	dd->sendctrl &= ~clr_sendctrl;

	qib_write_kreg(dd, kr_sendctrl, dd->sendctrl);
	qib_write_kreg(dd, kr_scratch, 0);

	spin_unlock(&dd->sendctrl_lock);
}

static void qib_decode_7220_sdma_errs(struct qib_pportdata *ppd,
				      u64 err, char *buf, size_t blen)
{
	static const struct {
		u64 err;
		const char *msg;
	} errs[] = {
		{ ERR_MASK(SDmaGenMismatchErr),
		  "SDmaGenMismatch" },
		{ ERR_MASK(SDmaOutOfBoundErr),
		  "SDmaOutOfBound" },
		{ ERR_MASK(SDmaTailOutOfBoundErr),
		  "SDmaTailOutOfBound" },
		{ ERR_MASK(SDmaBaseErr),
		  "SDmaBase" },
		{ ERR_MASK(SDma1stDescErr),
		  "SDma1stDesc" },
		{ ERR_MASK(SDmaRpyTagErr),
		  "SDmaRpyTag" },
		{ ERR_MASK(SDmaDwEnErr),
		  "SDmaDwEn" },
		{ ERR_MASK(SDmaMissingDwErr),
		  "SDmaMissingDw" },
		{ ERR_MASK(SDmaUnexpDataErr),
		  "SDmaUnexpData" },
		{ ERR_MASK(SDmaDescAddrMisalignErr),
		  "SDmaDescAddrMisalign" },
		{ ERR_MASK(SendBufMisuseErr),
		  "SendBufMisuse" },
		{ ERR_MASK(SDmaDisabledErr),
		  "SDmaDisabled" },
	};
	int i;
	size_t bidx = 0;

	for (i = 0; i < ARRAY_SIZE(errs); i++) {
		if (err & errs[i].err)
			bidx += scnprintf(buf + bidx, blen - bidx,
					 "%s ", errs[i].msg);
	}
}

/*
 * This is called as part of link down clean up so disarm and flush
 * all send buffers so that SMP packets can be sent.
 */
static void qib_7220_sdma_hw_clean_up(struct qib_pportdata *ppd)
{
	/* This will trigger the Abort interrupt */
	sendctrl_7220_mod(ppd, QIB_SENDCTRL_DISARM_ALL | QIB_SENDCTRL_FLUSH |
			  QIB_SENDCTRL_AVAIL_BLIP);
	ppd->dd->upd_pio_shadow  = 1; /* update our idea of what's busy */
}

static void qib_sdma_7220_setlengen(struct qib_pportdata *ppd)
{
	/*
	 * Set SendDmaLenGen and clear and set
	 * the MSB of the generation count to enable generation checking
	 * and load the internal generation counter.
	 */
	qib_write_kreg(ppd->dd, kr_senddmalengen, ppd->sdma_descq_cnt);
	qib_write_kreg(ppd->dd, kr_senddmalengen,
		       ppd->sdma_descq_cnt |
		       (1ULL << QIB_7220_SendDmaLenGen_Generation_MSB));
}

static void qib_7220_sdma_hw_start_up(struct qib_pportdata *ppd)
{
	qib_sdma_7220_setlengen(ppd);
	qib_sdma_update_7220_tail(ppd, 0); /* Set SendDmaTail */
	ppd->sdma_head_dma[0] = 0;
}

#define DISABLES_SDMA (							\
		ERR_MASK(SDmaDisabledErr) |				\
		ERR_MASK(SDmaBaseErr) |					\
		ERR_MASK(SDmaTailOutOfBoundErr) |			\
		ERR_MASK(SDmaOutOfBoundErr) |				\
		ERR_MASK(SDma1stDescErr) |				\
		ERR_MASK(SDmaRpyTagErr) |				\
		ERR_MASK(SDmaGenMismatchErr) |				\
		ERR_MASK(SDmaDescAddrMisalignErr) |			\
		ERR_MASK(SDmaMissingDwErr) |				\
		ERR_MASK(SDmaDwEnErr))

static void sdma_7220_errors(struct qib_pportdata *ppd, u64 errs)
{
	unsigned long flags;
	struct qib_devdata *dd = ppd->dd;
	char *msg;

	errs &= QLOGIC_IB_E_SDMAERRS;

	msg = dd->cspec->sdmamsgbuf;
	qib_decode_7220_sdma_errs(ppd, errs, msg,
		sizeof(dd->cspec->sdmamsgbuf));
	spin_lock_irqsave(&ppd->sdma_lock, flags);

	if (errs & ERR_MASK(SendBufMisuseErr)) {
		unsigned long sbuf[3];

		sbuf[0] = qib_read_kreg64(dd, kr_sendbuffererror);
		sbuf[1] = qib_read_kreg64(dd, kr_sendbuffererror + 1);
		sbuf[2] = qib_read_kreg64(dd, kr_sendbuffererror + 2);

		qib_dev_err(ppd->dd,
			    "IB%u:%u SendBufMisuse: %04lx %016lx %016lx\n",
			    ppd->dd->unit, ppd->port, sbuf[2], sbuf[1],
			    sbuf[0]);
	}

	if (errs & ERR_MASK(SDmaUnexpDataErr))
		qib_dev_err(dd, "IB%u:%u SDmaUnexpData\n", ppd->dd->unit,
			    ppd->port);

	switch (ppd->sdma_state.current_state) {
	case qib_sdma_state_s00_hw_down:
		/* not expecting any interrupts */
		break;

	case qib_sdma_state_s10_hw_start_up_wait:
		/* handled in intr path */
		break;

	case qib_sdma_state_s20_idle:
		/* not expecting any interrupts */
		break;

	case qib_sdma_state_s30_sw_clean_up_wait:
		/* not expecting any interrupts */
		break;

	case qib_sdma_state_s40_hw_clean_up_wait:
		if (errs & ERR_MASK(SDmaDisabledErr))
			__qib_sdma_process_event(ppd,
				qib_sdma_event_e50_hw_cleaned);
		break;

	case qib_sdma_state_s50_hw_halt_wait:
		/* handled in intr path */
		break;

	case qib_sdma_state_s99_running:
		if (errs & DISABLES_SDMA)
			__qib_sdma_process_event(ppd,
				qib_sdma_event_e7220_err_halted);
		break;
	}

	spin_unlock_irqrestore(&ppd->sdma_lock, flags);
}

/*
 * Decode the error status into strings, deciding whether to always
 * print * it or not depending on "normal packet errors" vs everything
 * else.   Return 1 if "real" errors, otherwise 0 if only packet
 * errors, so caller can decide what to print with the string.
 */
static int qib_decode_7220_err(struct qib_devdata *dd, char *buf, size_t blen,
			       u64 err)
{
	int iserr = 1;

	*buf = '\0';
	if (err & QLOGIC_IB_E_PKTERRS) {
		if (!(err & ~QLOGIC_IB_E_PKTERRS))
			iserr = 0;
		if ((err & ERR_MASK(RcvICRCErr)) &&
		    !(err & (ERR_MASK(RcvVCRCErr) | ERR_MASK(RcvEBPErr))))
			strlcat(buf, "CRC ", blen);
		if (!iserr)
			goto done;
	}
	if (err & ERR_MASK(RcvHdrLenErr))
		strlcat(buf, "rhdrlen ", blen);
	if (err & ERR_MASK(RcvBadTidErr))
		strlcat(buf, "rbadtid ", blen);
	if (err & ERR_MASK(RcvBadVersionErr))
		strlcat(buf, "rbadversion ", blen);
	if (err & ERR_MASK(RcvHdrErr))
		strlcat(buf, "rhdr ", blen);
	if (err & ERR_MASK(SendSpecialTriggerErr))
		strlcat(buf, "sendspecialtrigger ", blen);
	if (err & ERR_MASK(RcvLongPktLenErr))
		strlcat(buf, "rlongpktlen ", blen);
	if (err & ERR_MASK(RcvMaxPktLenErr))
		strlcat(buf, "rmaxpktlen ", blen);
	if (err & ERR_MASK(RcvMinPktLenErr))
		strlcat(buf, "rminpktlen ", blen);
	if (err & ERR_MASK(SendMinPktLenErr))
		strlcat(buf, "sminpktlen ", blen);
	if (err & ERR_MASK(RcvFormatErr))
		strlcat(buf, "rformaterr ", blen);
	if (err & ERR_MASK(RcvUnsupportedVLErr))
		strlcat(buf, "runsupvl ", blen);
	if (err & ERR_MASK(RcvUnexpectedCharErr))
		strlcat(buf, "runexpchar ", blen);
	if (err & ERR_MASK(RcvIBFlowErr))
		strlcat(buf, "ribflow ", blen);
	if (err & ERR_MASK(SendUnderRunErr))
		strlcat(buf, "sunderrun ", blen);
	if (err & ERR_MASK(SendPioArmLaunchErr))
		strlcat(buf, "spioarmlaunch ", blen);
	if (err & ERR_MASK(SendUnexpectedPktNumErr))
		strlcat(buf, "sunexperrpktnum ", blen);
	if (err & ERR_MASK(SendDroppedSmpPktErr))
		strlcat(buf, "sdroppedsmppkt ", blen);
	if (err & ERR_MASK(SendMaxPktLenErr))
		strlcat(buf, "smaxpktlen ", blen);
	if (err & ERR_MASK(SendUnsupportedVLErr))
		strlcat(buf, "sunsupVL ", blen);
	if (err & ERR_MASK(InvalidAddrErr))
		strlcat(buf, "invalidaddr ", blen);
	if (err & ERR_MASK(RcvEgrFullErr))
		strlcat(buf, "rcvegrfull ", blen);
	if (err & ERR_MASK(RcvHdrFullErr))
		strlcat(buf, "rcvhdrfull ", blen);
	if (err & ERR_MASK(IBStatusChanged))
		strlcat(buf, "ibcstatuschg ", blen);
	if (err & ERR_MASK(RcvIBLostLinkErr))
		strlcat(buf, "riblostlink ", blen);
	if (err & ERR_MASK(HardwareErr))
		strlcat(buf, "hardware ", blen);
	if (err & ERR_MASK(ResetNegated))
		strlcat(buf, "reset ", blen);
	if (err & QLOGIC_IB_E_SDMAERRS)
		qib_decode_7220_sdma_errs(dd->pport, err, buf, blen);
	if (err & ERR_MASK(InvalidEEPCmd))
		strlcat(buf, "invalideepromcmd ", blen);
done:
	return iserr;
}

static void reenable_7220_chase(unsigned long opaque)
{
	struct qib_pportdata *ppd = (struct qib_pportdata *)opaque;

	ppd->cpspec->chase_timer.expires = 0;
	qib_set_ib_7220_lstate(ppd, QLOGIC_IB_IBCC_LINKCMD_DOWN,
		QLOGIC_IB_IBCC_LINKINITCMD_POLL);
}

static void handle_7220_chase(struct qib_pportdata *ppd, u64 ibcst)
{
	u8 ibclt;
	unsigned long tnow;

	ibclt = (u8)SYM_FIELD(ibcst, IBCStatus, LinkTrainingState);

	/*
	 * Detect and handle the state chase issue, where we can
	 * get stuck if we are unlucky on timing on both sides of
	 * the link.   If we are, we disable, set a timer, and
	 * then re-enable.
	 */
	switch (ibclt) {
	case IB_7220_LT_STATE_CFGRCVFCFG:
	case IB_7220_LT_STATE_CFGWAITRMT:
	case IB_7220_LT_STATE_TXREVLANES:
	case IB_7220_LT_STATE_CFGENH:
		tnow = jiffies;
		if (ppd->cpspec->chase_end &&
		    time_after(tnow, ppd->cpspec->chase_end)) {
			ppd->cpspec->chase_end = 0;
			qib_set_ib_7220_lstate(ppd,
				QLOGIC_IB_IBCC_LINKCMD_DOWN,
				QLOGIC_IB_IBCC_LINKINITCMD_DISABLE);
			ppd->cpspec->chase_timer.expires = jiffies +
				QIB_CHASE_DIS_TIME;
			add_timer(&ppd->cpspec->chase_timer);
		} else if (!ppd->cpspec->chase_end)
			ppd->cpspec->chase_end = tnow + QIB_CHASE_TIME;
		break;

	default:
		ppd->cpspec->chase_end = 0;
		break;
	}
}

static void handle_7220_errors(struct qib_devdata *dd, u64 errs)
{
	char *msg;
	u64 ignore_this_time = 0;
	u64 iserr = 0;
	int log_idx;
	struct qib_pportdata *ppd = dd->pport;
	u64 mask;

	/* don't report errors that are masked */
	errs &= dd->cspec->errormask;
	msg = dd->cspec->emsgbuf;

	/* do these first, they are most important */
	if (errs & ERR_MASK(HardwareErr))
		qib_7220_handle_hwerrors(dd, msg, sizeof(dd->cspec->emsgbuf));
	else
		for (log_idx = 0; log_idx < QIB_EEP_LOG_CNT; ++log_idx)
			if (errs & dd->eep_st_masks[log_idx].errs_to_log)
				qib_inc_eeprom_err(dd, log_idx, 1);

	if (errs & QLOGIC_IB_E_SDMAERRS)
		sdma_7220_errors(ppd, errs);

	if (errs & ~IB_E_BITSEXTANT)
		qib_dev_err(dd,
			"error interrupt with unknown errors %llx set\n",
			(unsigned long long) (errs & ~IB_E_BITSEXTANT));

	if (errs & E_SUM_ERRS) {
		qib_disarm_7220_senderrbufs(ppd);
		if ((errs & E_SUM_LINK_PKTERRS) &&
		    !(ppd->lflags & QIBL_LINKACTIVE)) {
			/*
			 * This can happen when trying to bring the link
			 * up, but the IB link changes state at the "wrong"
			 * time. The IB logic then complains that the packet
			 * isn't valid.  We don't want to confuse people, so
			 * we just don't print them, except at debug
			 */
			ignore_this_time = errs & E_SUM_LINK_PKTERRS;
		}
	} else if ((errs & E_SUM_LINK_PKTERRS) &&
		   !(ppd->lflags & QIBL_LINKACTIVE)) {
		/*
		 * This can happen when SMA is trying to bring the link
		 * up, but the IB link changes state at the "wrong" time.
		 * The IB logic then complains that the packet isn't
		 * valid.  We don't want to confuse people, so we just
		 * don't print them, except at debug
		 */
		ignore_this_time = errs & E_SUM_LINK_PKTERRS;
	}

	qib_write_kreg(dd, kr_errclear, errs);

	errs &= ~ignore_this_time;
	if (!errs)
		goto done;

	/*
	 * The ones we mask off are handled specially below
	 * or above.  Also mask SDMADISABLED by default as it
	 * is too chatty.
	 */
	mask = ERR_MASK(IBStatusChanged) |
		ERR_MASK(RcvEgrFullErr) | ERR_MASK(RcvHdrFullErr) |
		ERR_MASK(HardwareErr) | ERR_MASK(SDmaDisabledErr);

	qib_decode_7220_err(dd, msg, sizeof(dd->cspec->emsgbuf), errs & ~mask);

	if (errs & E_SUM_PKTERRS)
		qib_stats.sps_rcverrs++;
	if (errs & E_SUM_ERRS)
		qib_stats.sps_txerrs++;
	iserr = errs & ~(E_SUM_PKTERRS | QLOGIC_IB_E_PKTERRS |
			 ERR_MASK(SDmaDisabledErr));

	if (errs & ERR_MASK(IBStatusChanged)) {
		u64 ibcs;

		ibcs = qib_read_kreg64(dd, kr_ibcstatus);
		if (!(ppd->lflags & QIBL_IB_AUTONEG_INPROG))
			handle_7220_chase(ppd, ibcs);

		/* Update our picture of width and speed from chip */
		ppd->link_width_active =
			((ibcs >> IBA7220_LINKWIDTH_SHIFT) & 1) ?
			    IB_WIDTH_4X : IB_WIDTH_1X;
		ppd->link_speed_active =
			((ibcs >> IBA7220_LINKSPEED_SHIFT) & 1) ?
			    QIB_IB_DDR : QIB_IB_SDR;

		/*
		 * Since going into a recovery state causes the link state
		 * to go down and since recovery is transitory, it is better
		 * if we "miss" ever seeing the link training state go into
		 * recovery (i.e., ignore this transition for link state
		 * special handling purposes) without updating lastibcstat.
		 */
		if (qib_7220_phys_portstate(ibcs) !=
					    IB_PHYSPORTSTATE_LINK_ERR_RECOVER)
			qib_handle_e_ibstatuschanged(ppd, ibcs);
	}

	if (errs & ERR_MASK(ResetNegated)) {
		qib_dev_err(dd,
			"Got reset, requires re-init (unload and reload driver)\n");
		dd->flags &= ~QIB_INITTED;  /* needs re-init */
		/* mark as having had error */
		*dd->devstatusp |= QIB_STATUS_HWERROR;
		*dd->pport->statusp &= ~QIB_STATUS_IB_CONF;
	}

	if (*msg && iserr)
		qib_dev_porterr(dd, ppd->port, "%s error\n", msg);

	if (ppd->state_wanted & ppd->lflags)
		wake_up_interruptible(&ppd->state_wait);

	/*
	 * If there were hdrq or egrfull errors, wake up any processes
	 * waiting in poll.  We used to try to check which contexts had
	 * the overflow, but given the cost of that and the chip reads
	 * to support it, it's better to just wake everybody up if we
	 * get an overflow; waiters can poll again if it's not them.
	 */
	if (errs & (ERR_MASK(RcvEgrFullErr) | ERR_MASK(RcvHdrFullErr))) {
		qib_handle_urcv(dd, ~0U);
		if (errs & ERR_MASK(RcvEgrFullErr))
			qib_stats.sps_buffull++;
		else
			qib_stats.sps_hdrfull++;
	}
done:
	return;
}

/* enable/disable chip from delivering interrupts */
static void qib_7220_set_intr_state(struct qib_devdata *dd, u32 enable)
{
	if (enable) {
		if (dd->flags & QIB_BADINTR)
			return;
		qib_write_kreg(dd, kr_intmask, ~0ULL);
		/* force re-interrupt of any pending interrupts. */
		qib_write_kreg(dd, kr_intclear, 0ULL);
	} else
		qib_write_kreg(dd, kr_intmask, 0ULL);
}

/*
 * Try to cleanup as much as possible for anything that might have gone
 * wrong while in freeze mode, such as pio buffers being written by user
 * processes (causing armlaunch), send errors due to going into freeze mode,
 * etc., and try to avoid causing extra interrupts while doing so.
 * Forcibly update the in-memory pioavail register copies after cleanup
 * because the chip won't do it while in freeze mode (the register values
 * themselves are kept correct).
 * Make sure that we don't lose any important interrupts by using the chip
 * feature that says that writing 0 to a bit in *clear that is set in
 * *status will cause an interrupt to be generated again (if allowed by
 * the *mask value).
 * This is in chip-specific code because of all of the register accesses,
 * even though the details are similar on most chips.
 */
static void qib_7220_clear_freeze(struct qib_devdata *dd)
{
	/* disable error interrupts, to avoid confusion */
	qib_write_kreg(dd, kr_errmask, 0ULL);

	/* also disable interrupts; errormask is sometimes overwritten */
	qib_7220_set_intr_state(dd, 0);

	qib_cancel_sends(dd->pport);

	/* clear the freeze, and be sure chip saw it */
	qib_write_kreg(dd, kr_control, dd->control);
	qib_read_kreg32(dd, kr_scratch);

	/* force in-memory update now we are out of freeze */
	qib_force_pio_avail_update(dd);

	/*
	 * force new interrupt if any hwerr, error or interrupt bits are
	 * still set, and clear "safe" send packet errors related to freeze
	 * and cancelling sends.  Re-enable error interrupts before possible
	 * force of re-interrupt on pending interrupts.
	 */
	qib_write_kreg(dd, kr_hwerrclear, 0ULL);
	qib_write_kreg(dd, kr_errclear, E_SPKT_ERRS_IGNORE);
	qib_write_kreg(dd, kr_errmask, dd->cspec->errormask);
	qib_7220_set_intr_state(dd, 1);
}

/**
 * qib_7220_handle_hwerrors - display hardware errors.
 * @dd: the qlogic_ib device
 * @msg: the output buffer
 * @msgl: the size of the output buffer
 *
 * Use same msg buffer as regular errors to avoid excessive stack
 * use.  Most hardware errors are catastrophic, but for right now,
 * we'll print them and continue.  We reuse the same message buffer as
 * handle_7220_errors() to avoid excessive stack usage.
 */
static void qib_7220_handle_hwerrors(struct qib_devdata *dd, char *msg,
				     size_t msgl)
{
	u64 hwerrs;
	u32 bits, ctrl;
	int isfatal = 0;
	char *bitsmsg;
	int log_idx;

	hwerrs = qib_read_kreg64(dd, kr_hwerrstatus);
	if (!hwerrs)
		goto bail;
	if (hwerrs == ~0ULL) {
		qib_dev_err(dd,
			"Read of hardware error status failed (all bits set); ignoring\n");
		goto bail;
	}
	qib_stats.sps_hwerrs++;

	/*
	 * Always clear the error status register, except MEMBISTFAIL,
	 * regardless of whether we continue or stop using the chip.
	 * We want that set so we know it failed, even across driver reload.
	 * We'll still ignore it in the hwerrmask.  We do this partly for
	 * diagnostics, but also for support.
	 */
	qib_write_kreg(dd, kr_hwerrclear,
		       hwerrs & ~HWE_MASK(PowerOnBISTFailed));

	hwerrs &= dd->cspec->hwerrmask;

	/* We log some errors to EEPROM, check if we have any of those. */
	for (log_idx = 0; log_idx < QIB_EEP_LOG_CNT; ++log_idx)
		if (hwerrs & dd->eep_st_masks[log_idx].hwerrs_to_log)
			qib_inc_eeprom_err(dd, log_idx, 1);
	if (hwerrs & ~(TXEMEMPARITYERR_PIOBUF | TXEMEMPARITYERR_PIOPBC |
		       RXE_PARITY))
		qib_devinfo(dd->pcidev,
			"Hardware error: hwerr=0x%llx (cleared)\n",
			(unsigned long long) hwerrs);

	if (hwerrs & ~IB_HWE_BITSEXTANT)
		qib_dev_err(dd,
			"hwerror interrupt with unknown errors %llx set\n",
			(unsigned long long) (hwerrs & ~IB_HWE_BITSEXTANT));

	if (hwerrs & QLOGIC_IB_HWE_IB_UC_MEMORYPARITYERR)
		qib_sd7220_clr_ibpar(dd);

	ctrl = qib_read_kreg32(dd, kr_control);
	if ((ctrl & QLOGIC_IB_C_FREEZEMODE) && !dd->diag_client) {
		/*
		 * Parity errors in send memory are recoverable by h/w
		 * just do housekeeping, exit freeze mode and continue.
		 */
		if (hwerrs & (TXEMEMPARITYERR_PIOBUF |
			      TXEMEMPARITYERR_PIOPBC)) {
			qib_7220_txe_recover(dd);
			hwerrs &= ~(TXEMEMPARITYERR_PIOBUF |
				    TXEMEMPARITYERR_PIOPBC);
		}
		if (hwerrs)
			isfatal = 1;
		else
			qib_7220_clear_freeze(dd);
	}

	*msg = '\0';

	if (hwerrs & HWE_MASK(PowerOnBISTFailed)) {
		isfatal = 1;
		strlcat(msg,
			"[Memory BIST test failed, InfiniPath hardware unusable]",
			msgl);
		/* ignore from now on, so disable until driver reloaded */
		dd->cspec->hwerrmask &= ~HWE_MASK(PowerOnBISTFailed);
		qib_write_kreg(dd, kr_hwerrmask, dd->cspec->hwerrmask);
	}

	qib_format_hwerrors(hwerrs, qib_7220_hwerror_msgs,
			    ARRAY_SIZE(qib_7220_hwerror_msgs), msg, msgl);

	bitsmsg = dd->cspec->bitsmsgbuf;
	if (hwerrs & (QLOGIC_IB_HWE_PCIEMEMPARITYERR_MASK <<
		      QLOGIC_IB_HWE_PCIEMEMPARITYERR_SHIFT)) {
		bits = (u32) ((hwerrs >>
			       QLOGIC_IB_HWE_PCIEMEMPARITYERR_SHIFT) &
			      QLOGIC_IB_HWE_PCIEMEMPARITYERR_MASK);
		snprintf(bitsmsg, sizeof(dd->cspec->bitsmsgbuf),
			 "[PCIe Mem Parity Errs %x] ", bits);
		strlcat(msg, bitsmsg, msgl);
	}

#define _QIB_PLL_FAIL (QLOGIC_IB_HWE_COREPLL_FBSLIP |   \
			 QLOGIC_IB_HWE_COREPLL_RFSLIP)

	if (hwerrs & _QIB_PLL_FAIL) {
		isfatal = 1;
		snprintf(bitsmsg, sizeof(dd->cspec->bitsmsgbuf),
			 "[PLL failed (%llx), InfiniPath hardware unusable]",
			 (unsigned long long) hwerrs & _QIB_PLL_FAIL);
		strlcat(msg, bitsmsg, msgl);
		/* ignore from now on, so disable until driver reloaded */
		dd->cspec->hwerrmask &= ~(hwerrs & _QIB_PLL_FAIL);
		qib_write_kreg(dd, kr_hwerrmask, dd->cspec->hwerrmask);
	}

	if (hwerrs & QLOGIC_IB_HWE_SERDESPLLFAILED) {
		/*
		 * If it occurs, it is left masked since the eternal
		 * interface is unused.
		 */
		dd->cspec->hwerrmask &= ~QLOGIC_IB_HWE_SERDESPLLFAILED;
		qib_write_kreg(dd, kr_hwerrmask, dd->cspec->hwerrmask);
	}

	qib_dev_err(dd, "%s hardware error\n", msg);

	if (isfatal && !dd->diag_client) {
		qib_dev_err(dd,
			"Fatal Hardware Error, no longer usable, SN %.16s\n",
			dd->serial);
		/*
		 * For /sys status file and user programs to print; if no
		 * trailing brace is copied, we'll know it was truncated.
		 */
		if (dd->freezemsg)
			snprintf(dd->freezemsg, dd->freezelen,
				 "{%s}", msg);
		qib_disable_after_error(dd);
	}
bail:;
}

/**
 * qib_7220_init_hwerrors - enable hardware errors
 * @dd: the qlogic_ib device
 *
 * now that we have finished initializing everything that might reasonably
 * cause a hardware error, and cleared those errors bits as they occur,
 * we can enable hardware errors in the mask (potentially enabling
 * freeze mode), and enable hardware errors as errors (along with
 * everything else) in errormask
 */
static void qib_7220_init_hwerrors(struct qib_devdata *dd)
{
	u64 val;
	u64 extsval;

	extsval = qib_read_kreg64(dd, kr_extstatus);

	if (!(extsval & (QLOGIC_IB_EXTS_MEMBIST_ENDTEST |
			 QLOGIC_IB_EXTS_MEMBIST_DISABLED)))
		qib_dev_err(dd, "MemBIST did not complete!\n");
	if (extsval & QLOGIC_IB_EXTS_MEMBIST_DISABLED)
		qib_devinfo(dd->pcidev, "MemBIST is disabled.\n");

	val = ~0ULL;    /* default to all hwerrors become interrupts, */

	val &= ~QLOGIC_IB_HWE_IB_UC_MEMORYPARITYERR;
	dd->cspec->hwerrmask = val;

	qib_write_kreg(dd, kr_hwerrclear, ~HWE_MASK(PowerOnBISTFailed));
	qib_write_kreg(dd, kr_hwerrmask, dd->cspec->hwerrmask);

	/* clear all */
	qib_write_kreg(dd, kr_errclear, ~0ULL);
	/* enable errors that are masked, at least this first time. */
	qib_write_kreg(dd, kr_errmask, ~0ULL);
	dd->cspec->errormask = qib_read_kreg64(dd, kr_errmask);
	/* clear any interrupts up to this point (ints still not enabled) */
	qib_write_kreg(dd, kr_intclear, ~0ULL);
}

/*
 * Disable and enable the armlaunch error.  Used for PIO bandwidth testing
 * on chips that are count-based, rather than trigger-based.  There is no
 * reference counting, but that's also fine, given the intended use.
 * Only chip-specific because it's all register accesses
 */
static void qib_set_7220_armlaunch(struct qib_devdata *dd, u32 enable)
{
	if (enable) {
		qib_write_kreg(dd, kr_errclear, ERR_MASK(SendPioArmLaunchErr));
		dd->cspec->errormask |= ERR_MASK(SendPioArmLaunchErr);
	} else
		dd->cspec->errormask &= ~ERR_MASK(SendPioArmLaunchErr);
	qib_write_kreg(dd, kr_errmask, dd->cspec->errormask);
}

/*
 * Formerly took parameter <which> in pre-shifted,
 * pre-merged form with LinkCmd and LinkInitCmd
 * together, and assuming the zero was NOP.
 */
static void qib_set_ib_7220_lstate(struct qib_pportdata *ppd, u16 linkcmd,
				   u16 linitcmd)
{
	u64 mod_wd;
	struct qib_devdata *dd = ppd->dd;
	unsigned long flags;

	if (linitcmd == QLOGIC_IB_IBCC_LINKINITCMD_DISABLE) {
		/*
		 * If we are told to disable, note that so link-recovery
		 * code does not attempt to bring us back up.
		 */
		spin_lock_irqsave(&ppd->lflags_lock, flags);
		ppd->lflags |= QIBL_IB_LINK_DISABLED;
		spin_unlock_irqrestore(&ppd->lflags_lock, flags);
	} else if (linitcmd || linkcmd == QLOGIC_IB_IBCC_LINKCMD_DOWN) {
		/*
		 * Any other linkinitcmd will lead to LINKDOWN and then
		 * to INIT (if all is well), so clear flag to let
		 * link-recovery code attempt to bring us back up.
		 */
		spin_lock_irqsave(&ppd->lflags_lock, flags);
		ppd->lflags &= ~QIBL_IB_LINK_DISABLED;
		spin_unlock_irqrestore(&ppd->lflags_lock, flags);
	}

	mod_wd = (linkcmd << IBA7220_IBCC_LINKCMD_SHIFT) |
		(linitcmd << QLOGIC_IB_IBCC_LINKINITCMD_SHIFT);

	qib_write_kreg(dd, kr_ibcctrl, ppd->cpspec->ibcctrl | mod_wd);
	/* write to chip to prevent back-to-back writes of ibc reg */
	qib_write_kreg(dd, kr_scratch, 0);
}

/*
 * All detailed interaction with the SerDes has been moved to qib_sd7220.c
 *
 * The portion of IBA7220-specific bringup_serdes() that actually deals with
 * registers and memory within the SerDes itself is qib_sd7220_init().
 */

/**
 * qib_7220_bringup_serdes - bring up the serdes
 * @ppd: physical port on the qlogic_ib device
 */
static int qib_7220_bringup_serdes(struct qib_pportdata *ppd)
{
	struct qib_devdata *dd = ppd->dd;
	u64 val, prev_val, guid, ibc;
	int ret = 0;

	/* Put IBC in reset, sends disabled */
	dd->control &= ~QLOGIC_IB_C_LINKENABLE;
	qib_write_kreg(dd, kr_control, 0ULL);

	if (qib_compat_ddr_negotiate) {
		ppd->cpspec->ibdeltainprog = 1;
		ppd->cpspec->ibsymsnap = read_7220_creg32(dd, cr_ibsymbolerr);
		ppd->cpspec->iblnkerrsnap =
			read_7220_creg32(dd, cr_iblinkerrrecov);
	}

	/* flowcontrolwatermark is in units of KBytes */
	ibc = 0x5ULL << SYM_LSB(IBCCtrl, FlowCtrlWaterMark);
	/*
	 * How often flowctrl sent.  More or less in usecs; balance against
	 * watermark value, so that in theory senders always get a flow
	 * control update in time to not let the IB link go idle.
	 */
	ibc |= 0x3ULL << SYM_LSB(IBCCtrl, FlowCtrlPeriod);
	/* max error tolerance */
	ibc |= 0xfULL << SYM_LSB(IBCCtrl, PhyerrThreshold);
	/* use "real" buffer space for */
	ibc |= 4ULL << SYM_LSB(IBCCtrl, CreditScale);
	/* IB credit flow control. */
	ibc |= 0xfULL << SYM_LSB(IBCCtrl, OverrunThreshold);
	/*
	 * set initial max size pkt IBC will send, including ICRC; it's the
	 * PIO buffer size in dwords, less 1; also see qib_set_mtu()
	 */
	ibc |= ((u64)(ppd->ibmaxlen >> 2) + 1) << SYM_LSB(IBCCtrl, MaxPktLen);
	ppd->cpspec->ibcctrl = ibc; /* without linkcmd or linkinitcmd! */

	/* initially come up waiting for TS1, without sending anything. */
	val = ppd->cpspec->ibcctrl | (QLOGIC_IB_IBCC_LINKINITCMD_DISABLE <<
		QLOGIC_IB_IBCC_LINKINITCMD_SHIFT);
	qib_write_kreg(dd, kr_ibcctrl, val);

	if (!ppd->cpspec->ibcddrctrl) {
		/* not on re-init after reset */
		ppd->cpspec->ibcddrctrl = qib_read_kreg64(dd, kr_ibcddrctrl);

		if (ppd->link_speed_enabled == (QIB_IB_SDR | QIB_IB_DDR))
			ppd->cpspec->ibcddrctrl |=
				IBA7220_IBC_SPEED_AUTONEG_MASK |
				IBA7220_IBC_IBTA_1_2_MASK;
		else
			ppd->cpspec->ibcddrctrl |=
				ppd->link_speed_enabled == QIB_IB_DDR ?
				IBA7220_IBC_SPEED_DDR : IBA7220_IBC_SPEED_SDR;
		if ((ppd->link_width_enabled & (IB_WIDTH_1X | IB_WIDTH_4X)) ==
		    (IB_WIDTH_1X | IB_WIDTH_4X))
			ppd->cpspec->ibcddrctrl |= IBA7220_IBC_WIDTH_AUTONEG;
		else
			ppd->cpspec->ibcddrctrl |=
				ppd->link_width_enabled == IB_WIDTH_4X ?
				IBA7220_IBC_WIDTH_4X_ONLY :
				IBA7220_IBC_WIDTH_1X_ONLY;

		/* always enable these on driver reload, not sticky */
		ppd->cpspec->ibcddrctrl |=
			IBA7220_IBC_RXPOL_MASK << IBA7220_IBC_RXPOL_SHIFT;
		ppd->cpspec->ibcddrctrl |=
			IBA7220_IBC_HRTBT_MASK << IBA7220_IBC_HRTBT_SHIFT;

		/* enable automatic lane reversal detection for receive */
		ppd->cpspec->ibcddrctrl |= IBA7220_IBC_LANE_REV_SUPPORTED;
	} else
		/* write to chip to prevent back-to-back writes of ibc reg */
		qib_write_kreg(dd, kr_scratch, 0);

	qib_write_kreg(dd, kr_ibcddrctrl, ppd->cpspec->ibcddrctrl);
	qib_write_kreg(dd, kr_scratch, 0);

	qib_write_kreg(dd, kr_ncmodectrl, 0Ull);
	qib_write_kreg(dd, kr_scratch, 0);

	ret = qib_sd7220_init(dd);

	val = qib_read_kreg64(dd, kr_xgxs_cfg);
	prev_val = val;
	val |= QLOGIC_IB_XGXS_FC_SAFE;
	if (val != prev_val) {
		qib_write_kreg(dd, kr_xgxs_cfg, val);
		qib_read_kreg32(dd, kr_scratch);
	}
	if (val & QLOGIC_IB_XGXS_RESET)
		val &= ~QLOGIC_IB_XGXS_RESET;
	if (val != prev_val)
		qib_write_kreg(dd, kr_xgxs_cfg, val);

	/* first time through, set port guid */
	if (!ppd->guid)
		ppd->guid = dd->base_guid;
	guid = be64_to_cpu(ppd->guid);

	qib_write_kreg(dd, kr_hrtbt_guid, guid);
	if (!ret) {
		dd->control |= QLOGIC_IB_C_LINKENABLE;
		qib_write_kreg(dd, kr_control, dd->control);
	} else
		/* write to chip to prevent back-to-back writes of ibc reg */
		qib_write_kreg(dd, kr_scratch, 0);
	return ret;
}

/**
 * qib_7220_quiet_serdes - set serdes to txidle
 * @ppd: physical port of the qlogic_ib device
 * Called when driver is being unloaded
 */
static void qib_7220_quiet_serdes(struct qib_pportdata *ppd)
{
	u64 val;
	struct qib_devdata *dd = ppd->dd;
	unsigned long flags;

	/* disable IBC */
	dd->control &= ~QLOGIC_IB_C_LINKENABLE;
	qib_write_kreg(dd, kr_control,
		       dd->control | QLOGIC_IB_C_FREEZEMODE);

	ppd->cpspec->chase_end = 0;
	if (ppd->cpspec->chase_timer.data) /* if initted */
		del_timer_sync(&ppd->cpspec->chase_timer);

	if (ppd->cpspec->ibsymdelta || ppd->cpspec->iblnkerrdelta ||
	    ppd->cpspec->ibdeltainprog) {
		u64 diagc;

		/* enable counter writes */
		diagc = qib_read_kreg64(dd, kr_hwdiagctrl);
		qib_write_kreg(dd, kr_hwdiagctrl,
			       diagc | SYM_MASK(HwDiagCtrl, CounterWrEnable));

		if (ppd->cpspec->ibsymdelta || ppd->cpspec->ibdeltainprog) {
			val = read_7220_creg32(dd, cr_ibsymbolerr);
			if (ppd->cpspec->ibdeltainprog)
				val -= val - ppd->cpspec->ibsymsnap;
			val -= ppd->cpspec->ibsymdelta;
			write_7220_creg(dd, cr_ibsymbolerr, val);
		}
		if (ppd->cpspec->iblnkerrdelta || ppd->cpspec->ibdeltainprog) {
			val = read_7220_creg32(dd, cr_iblinkerrrecov);
			if (ppd->cpspec->ibdeltainprog)
				val -= val - ppd->cpspec->iblnkerrsnap;
			val -= ppd->cpspec->iblnkerrdelta;
			write_7220_creg(dd, cr_iblinkerrrecov, val);
		}

		/* and disable counter writes */
		qib_write_kreg(dd, kr_hwdiagctrl, diagc);
	}
	qib_set_ib_7220_lstate(ppd, 0, QLOGIC_IB_IBCC_LINKINITCMD_DISABLE);

	spin_lock_irqsave(&ppd->lflags_lock, flags);
	ppd->lflags &= ~QIBL_IB_AUTONEG_INPROG;
	spin_unlock_irqrestore(&ppd->lflags_lock, flags);
	wake_up(&ppd->cpspec->autoneg_wait);
	cancel_delayed_work_sync(&ppd->cpspec->autoneg_work);

	shutdown_7220_relock_poll(ppd->dd);
	val = qib_read_kreg64(ppd->dd, kr_xgxs_cfg);
	val |= QLOGIC_IB_XGXS_RESET;
	qib_write_kreg(ppd->dd, kr_xgxs_cfg, val);
}

/**
 * qib_setup_7220_setextled - set the state of the two external LEDs
 * @dd: the qlogic_ib device
 * @on: whether the link is up or not
 *
 * The exact combo of LEDs if on is true is determined by looking
 * at the ibcstatus.
 *
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
static void qib_setup_7220_setextled(struct qib_pportdata *ppd, u32 on)
{
	struct qib_devdata *dd = ppd->dd;
	u64 extctl, ledblink = 0, val, lst, ltst;
	unsigned long flags;

	/*
	 * The diags use the LED to indicate diag info, so we leave
	 * the external LED alone when the diags are running.
	 */
	if (dd->diag_client)
		return;

	if (ppd->led_override) {
		ltst = (ppd->led_override & QIB_LED_PHYS) ?
			IB_PHYSPORTSTATE_LINKUP : IB_PHYSPORTSTATE_DISABLED,
		lst = (ppd->led_override & QIB_LED_LOG) ?
			IB_PORT_ACTIVE : IB_PORT_DOWN;
	} else if (on) {
		val = qib_read_kreg64(dd, kr_ibcstatus);
		ltst = qib_7220_phys_portstate(val);
		lst = qib_7220_iblink_state(val);
	} else {
		ltst = 0;
		lst = 0;
	}

	spin_lock_irqsave(&dd->cspec->gpio_lock, flags);
	extctl = dd->cspec->extctrl & ~(SYM_MASK(EXTCtrl, LEDPriPortGreenOn) |
				 SYM_MASK(EXTCtrl, LEDPriPortYellowOn));
	if (ltst == IB_PHYSPORTSTATE_LINKUP) {
		extctl |= SYM_MASK(EXTCtrl, LEDPriPortGreenOn);
		/*
		 * counts are in chip clock (4ns) periods.
		 * This is 1/16 sec (66.6ms) on,
		 * 3/16 sec (187.5 ms) off, with packets rcvd
		 */
		ledblink = ((66600 * 1000UL / 4) << IBA7220_LEDBLINK_ON_SHIFT)
			| ((187500 * 1000UL / 4) << IBA7220_LEDBLINK_OFF_SHIFT);
	}
	if (lst == IB_PORT_ACTIVE)
		extctl |= SYM_MASK(EXTCtrl, LEDPriPortYellowOn);
	dd->cspec->extctrl = extctl;
	qib_write_kreg(dd, kr_extctrl, extctl);
	spin_unlock_irqrestore(&dd->cspec->gpio_lock, flags);

	if (ledblink) /* blink the LED on packet receive */
		qib_write_kreg(dd, kr_rcvpktledcnt, ledblink);
}

static void qib_7220_free_irq(struct qib_devdata *dd)
{
	if (dd->cspec->irq) {
		free_irq(dd->cspec->irq, dd);
		dd->cspec->irq = 0;
	}
	qib_nomsi(dd);
}

/*
 * qib_setup_7220_cleanup - clean up any per-chip chip-specific stuff
 * @dd: the qlogic_ib device
 *
 * This is called during driver unload.
 *
 */
static void qib_setup_7220_cleanup(struct qib_devdata *dd)
{
	qib_7220_free_irq(dd);
	kfree(dd->cspec->cntrs);
	kfree(dd->cspec->portcntrs);
}

/*
 * This is only called for SDmaInt.
 * SDmaDisabled is handled on the error path.
 */
static void sdma_7220_intr(struct qib_pportdata *ppd, u64 istat)
{
	unsigned long flags;

	spin_lock_irqsave(&ppd->sdma_lock, flags);

	switch (ppd->sdma_state.current_state) {
	case qib_sdma_state_s00_hw_down:
		break;

	case qib_sdma_state_s10_hw_start_up_wait:
		__qib_sdma_process_event(ppd, qib_sdma_event_e20_hw_started);
		break;

	case qib_sdma_state_s20_idle:
		break;

	case qib_sdma_state_s30_sw_clean_up_wait:
		break;

	case qib_sdma_state_s40_hw_clean_up_wait:
		break;

	case qib_sdma_state_s50_hw_halt_wait:
		__qib_sdma_process_event(ppd, qib_sdma_event_e60_hw_halted);
		break;

	case qib_sdma_state_s99_running:
		/* too chatty to print here */
		__qib_sdma_intr(ppd);
		break;
	}
	spin_unlock_irqrestore(&ppd->sdma_lock, flags);
}

static void qib_wantpiobuf_7220_intr(struct qib_devdata *dd, u32 needint)
{
	unsigned long flags;

	spin_lock_irqsave(&dd->sendctrl_lock, flags);
	if (needint) {
		if (!(dd->sendctrl & SYM_MASK(SendCtrl, SendBufAvailUpd)))
			goto done;
		/*
		 * blip the availupd off, next write will be on, so
		 * we ensure an avail update, regardless of threshold or
		 * buffers becoming free, whenever we want an interrupt
		 */
		qib_write_kreg(dd, kr_sendctrl, dd->sendctrl &
			~SYM_MASK(SendCtrl, SendBufAvailUpd));
		qib_write_kreg(dd, kr_scratch, 0ULL);
		dd->sendctrl |= SYM_MASK(SendCtrl, SendIntBufAvail);
	} else
		dd->sendctrl &= ~SYM_MASK(SendCtrl, SendIntBufAvail);
	qib_write_kreg(dd, kr_sendctrl, dd->sendctrl);
	qib_write_kreg(dd, kr_scratch, 0ULL);
done:
	spin_unlock_irqrestore(&dd->sendctrl_lock, flags);
}

/*
 * Handle errors and unusual events first, separate function
 * to improve cache hits for fast path interrupt handling.
 */
static noinline void unlikely_7220_intr(struct qib_devdata *dd, u64 istat)
{
	if (unlikely(istat & ~QLOGIC_IB_I_BITSEXTANT))
		qib_dev_err(dd,
			    "interrupt with unknown interrupts %Lx set\n",
			    istat & ~QLOGIC_IB_I_BITSEXTANT);

	if (istat & QLOGIC_IB_I_GPIO) {
		u32 gpiostatus;

		/*
		 * Boards for this chip currently don't use GPIO interrupts,
		 * so clear by writing GPIOstatus to GPIOclear, and complain
		 * to alert developer. To avoid endless repeats, clear
		 * the bits in the mask, since there is some kind of
		 * programming error or chip problem.
		 */
		gpiostatus = qib_read_kreg32(dd, kr_gpio_status);
		/*
		 * In theory, writing GPIOstatus to GPIOclear could
		 * have a bad side-effect on some diagnostic that wanted
		 * to poll for a status-change, but the various shadows
		 * make that problematic at best. Diags will just suppress
		 * all GPIO interrupts during such tests.
		 */
		qib_write_kreg(dd, kr_gpio_clear, gpiostatus);

		if (gpiostatus) {
			const u32 mask = qib_read_kreg32(dd, kr_gpio_mask);
			u32 gpio_irq = mask & gpiostatus;

			/*
			 * A bit set in status and (chip) Mask register
			 * would cause an interrupt. Since we are not
			 * expecting any, report it. Also check that the
			 * chip reflects our shadow, report issues,
			 * and refresh from the shadow.
			 */
			/*
			 * Clear any troublemakers, and update chip
			 * from shadow
			 */
			dd->cspec->gpio_mask &= ~gpio_irq;
			qib_write_kreg(dd, kr_gpio_mask, dd->cspec->gpio_mask);
		}
	}

	if (istat & QLOGIC_IB_I_ERROR) {
		u64 estat;

		qib_stats.sps_errints++;
		estat = qib_read_kreg64(dd, kr_errstatus);
		if (!estat)
			qib_devinfo(dd->pcidev,
				"error interrupt (%Lx), but no error bits set!\n",
				istat);
		else
			handle_7220_errors(dd, estat);
	}
}

static irqreturn_t qib_7220intr(int irq, void *data)
{
	struct qib_devdata *dd = data;
	irqreturn_t ret;
	u64 istat;
	u64 ctxtrbits;
	u64 rmask;
	unsigned i;

	if ((dd->flags & (QIB_PRESENT | QIB_BADINTR)) != QIB_PRESENT) {
		/*
		 * This return value is not great, but we do not want the
		 * interrupt core code to remove our interrupt handler
		 * because we don't appear to be handling an interrupt
		 * during a chip reset.
		 */
		ret = IRQ_HANDLED;
		goto bail;
	}

	istat = qib_read_kreg64(dd, kr_intstatus);

	if (unlikely(!istat)) {
		ret = IRQ_NONE; /* not our interrupt, or already handled */
		goto bail;
	}
	if (unlikely(istat == -1)) {
		qib_bad_intrstatus(dd);
		/* don't know if it was our interrupt or not */
		ret = IRQ_NONE;
		goto bail;
	}

	this_cpu_inc(*dd->int_counter);
	if (unlikely(istat & (~QLOGIC_IB_I_BITSEXTANT |
			      QLOGIC_IB_I_GPIO | QLOGIC_IB_I_ERROR)))
		unlikely_7220_intr(dd, istat);

	/*
	 * Clear the interrupt bits we found set, relatively early, so we
	 * "know" know the chip will have seen this by the time we process
	 * the queue, and will re-interrupt if necessary.  The processor
	 * itself won't take the interrupt again until we return.
	 */
	qib_write_kreg(dd, kr_intclear, istat);

	/*
	 * Handle kernel receive queues before checking for pio buffers
	 * available since receives can overflow; piobuf waiters can afford
	 * a few extra cycles, since they were waiting anyway.
	 */
	ctxtrbits = istat &
		((QLOGIC_IB_I_RCVAVAIL_MASK << QLOGIC_IB_I_RCVAVAIL_SHIFT) |
		 (QLOGIC_IB_I_RCVURG_MASK << QLOGIC_IB_I_RCVURG_SHIFT));
	if (ctxtrbits) {
		rmask = (1ULL << QLOGIC_IB_I_RCVAVAIL_SHIFT) |
			(1ULL << QLOGIC_IB_I_RCVURG_SHIFT);
		for (i = 0; i < dd->first_user_ctxt; i++) {
			if (ctxtrbits & rmask) {
				ctxtrbits &= ~rmask;
				qib_kreceive(dd->rcd[i], NULL, NULL);
			}
			rmask <<= 1;
		}
		if (ctxtrbits) {
			ctxtrbits =
				(ctxtrbits >> QLOGIC_IB_I_RCVAVAIL_SHIFT) |
				(ctxtrbits >> QLOGIC_IB_I_RCVURG_SHIFT);
			qib_handle_urcv(dd, ctxtrbits);
		}
	}

	/* only call for SDmaInt */
	if (istat & QLOGIC_IB_I_SDMAINT)
		sdma_7220_intr(dd->pport, istat);

	if ((istat & QLOGIC_IB_I_SPIOBUFAVAIL) && (dd->flags & QIB_INITTED))
		qib_ib_piobufavail(dd);

	ret = IRQ_HANDLED;
bail:
	return ret;
}

/*
 * Set up our chip-specific interrupt handler.
 * The interrupt type has already been setup, so
 * we just need to do the registration and error checking.
 * If we are using MSI interrupts, we may fall back to
 * INTx later, if the interrupt handler doesn't get called
 * within 1/2 second (see verify_interrupt()).
 */
static void qib_setup_7220_interrupt(struct qib_devdata *dd)
{
	if (!dd->cspec->irq)
		qib_dev_err(dd,
			"irq is 0, BIOS error?  Interrupts won't work\n");
	else {
		int ret = request_irq(dd->cspec->irq, qib_7220intr,
			dd->msi_lo ? 0 : IRQF_SHARED,
			QIB_DRV_NAME, dd);

		if (ret)
			qib_dev_err(dd,
				"Couldn't setup %s interrupt (irq=%d): %d\n",
				dd->msi_lo ?  "MSI" : "INTx",
				dd->cspec->irq, ret);
	}
}

/**
 * qib_7220_boardname - fill in the board name
 * @dd: the qlogic_ib device
 *
 * info is based on the board revision register
 */
static void qib_7220_boardname(struct qib_devdata *dd)
{
	char *n;
	u32 boardid, namelen;

	boardid = SYM_FIELD(dd->revision, Revision,
			    BoardID);

	switch (boardid) {
	case 1:
		n = "InfiniPath_QLE7240";
		break;
	case 2:
		n = "InfiniPath_QLE7280";
		break;
	default:
		qib_dev_err(dd, "Unknown 7220 board with ID %u\n", boardid);
		n = "Unknown_InfiniPath_7220";
		break;
	}

	namelen = strlen(n) + 1;
	dd->boardname = kmalloc(namelen, GFP_KERNEL);
	if (dd->boardname)
		snprintf(dd->boardname, namelen, "%s", n);

	if (dd->majrev != 5 || !dd->minrev || dd->minrev > 2)
		qib_dev_err(dd,
			"Unsupported InfiniPath hardware revision %u.%u!\n",
			dd->majrev, dd->minrev);

	snprintf(dd->boardversion, sizeof(dd->boardversion),
		 "ChipABI %u.%u, %s, InfiniPath%u %u.%u, SW Compat %u\n",
		 QIB_CHIP_VERS_MAJ, QIB_CHIP_VERS_MIN, dd->boardname,
		 (unsigned)SYM_FIELD(dd->revision, Revision_R, Arch),
		 dd->majrev, dd->minrev,
		 (unsigned)SYM_FIELD(dd->revision, Revision_R, SW));
}

/*
 * This routine sleeps, so it can only be called from user context, not
 * from interrupt context.
 */
static int qib_setup_7220_reset(struct qib_devdata *dd)
{
	u64 val;
	int i;
	int ret;
	u16 cmdval;
	u8 int_line, clinesz;
	unsigned long flags;

	qib_pcie_getcmd(dd, &cmdval, &int_line, &clinesz);

	/* Use dev_err so it shows up in logs, etc. */
	qib_dev_err(dd, "Resetting InfiniPath unit %u\n", dd->unit);

	/* no interrupts till re-initted */
	qib_7220_set_intr_state(dd, 0);

	dd->pport->cpspec->ibdeltainprog = 0;
	dd->pport->cpspec->ibsymdelta = 0;
	dd->pport->cpspec->iblnkerrdelta = 0;

	/*
	 * Keep chip from being accessed until we are ready.  Use
	 * writeq() directly, to allow the write even though QIB_PRESENT
	 * isn't set.
	 */
	dd->flags &= ~(QIB_INITTED | QIB_PRESENT);
	/* so we check interrupts work again */
	dd->z_int_counter = qib_int_counter(dd);
	val = dd->control | QLOGIC_IB_C_RESET;
	writeq(val, &dd->kregbase[kr_control]);
	mb(); /* prevent compiler reordering around actual reset */

	for (i = 1; i <= 5; i++) {
		/*
		 * Allow MBIST, etc. to complete; longer on each retry.
		 * We sometimes get machine checks from bus timeout if no
		 * response, so for now, make it *really* long.
		 */
		msleep(1000 + (1 + i) * 2000);

		qib_pcie_reenable(dd, cmdval, int_line, clinesz);

		/*
		 * Use readq directly, so we don't need to mark it as PRESENT
		 * until we get a successful indication that all is well.
		 */
		val = readq(&dd->kregbase[kr_revision]);
		if (val == dd->revision) {
			dd->flags |= QIB_PRESENT; /* it's back */
			ret = qib_reinit_intr(dd);
			goto bail;
		}
	}
	ret = 0; /* failed */

bail:
	if (ret) {
		if (qib_pcie_params(dd, dd->lbus_width, NULL, NULL))
			qib_dev_err(dd,
				"Reset failed to setup PCIe or interrupts; continuing anyway\n");

		/* hold IBC in reset, no sends, etc till later */
		qib_write_kreg(dd, kr_control, 0ULL);

		/* clear the reset error, init error/hwerror mask */
		qib_7220_init_hwerrors(dd);

		/* do setup similar to speed or link-width changes */
		if (dd->pport->cpspec->ibcddrctrl & IBA7220_IBC_IBTA_1_2_MASK)
			dd->cspec->presets_needed = 1;
		spin_lock_irqsave(&dd->pport->lflags_lock, flags);
		dd->pport->lflags |= QIBL_IB_FORCE_NOTIFY;
		dd->pport->lflags &= ~QIBL_IB_AUTONEG_FAILED;
		spin_unlock_irqrestore(&dd->pport->lflags_lock, flags);
	}

	return ret;
}

/**
 * qib_7220_put_tid - write a TID to the chip
 * @dd: the qlogic_ib device
 * @tidptr: pointer to the expected TID (in chip) to update
 * @tidtype: 0 for eager, 1 for expected
 * @pa: physical address of in memory buffer; tidinvalid if freeing
 */
static void qib_7220_put_tid(struct qib_devdata *dd, u64 __iomem *tidptr,
			     u32 type, unsigned long pa)
{
	if (pa != dd->tidinvalid) {
		u64 chippa = pa >> IBA7220_TID_PA_SHIFT;

		/* paranoia checks */
		if (pa != (chippa << IBA7220_TID_PA_SHIFT)) {
			qib_dev_err(dd, "Physaddr %lx not 2KB aligned!\n",
				    pa);
			return;
		}
		if (chippa >= (1UL << IBA7220_TID_SZ_SHIFT)) {
			qib_dev_err(dd,
				"Physical page address 0x%lx larger than supported\n",
				pa);
			return;
		}

		if (type == RCVHQ_RCV_TYPE_EAGER)
			chippa |= dd->tidtemplate;
		else /* for now, always full 4KB page */
			chippa |= IBA7220_TID_SZ_4K;
		pa = chippa;
	}
	writeq(pa, tidptr);
	mmiowb();
}

/**
 * qib_7220_clear_tids - clear all TID entries for a ctxt, expected and eager
 * @dd: the qlogic_ib device
 * @ctxt: the ctxt
 *
 * clear all TID entries for a ctxt, expected and eager.
 * Used from qib_close().  On this chip, TIDs are only 32 bits,
 * not 64, but they are still on 64 bit boundaries, so tidbase
 * is declared as u64 * for the pointer math, even though we write 32 bits
 */
static void qib_7220_clear_tids(struct qib_devdata *dd,
				struct qib_ctxtdata *rcd)
{
	u64 __iomem *tidbase;
	unsigned long tidinv;
	u32 ctxt;
	int i;

	if (!dd->kregbase || !rcd)
		return;

	ctxt = rcd->ctxt;

	tidinv = dd->tidinvalid;
	tidbase = (u64 __iomem *)
		((char __iomem *)(dd->kregbase) +
		 dd->rcvtidbase +
		 ctxt * dd->rcvtidcnt * sizeof(*tidbase));

	for (i = 0; i < dd->rcvtidcnt; i++)
		qib_7220_put_tid(dd, &tidbase[i], RCVHQ_RCV_TYPE_EXPECTED,
				 tidinv);

	tidbase = (u64 __iomem *)
		((char __iomem *)(dd->kregbase) +
		 dd->rcvegrbase +
		 rcd->rcvegr_tid_base * sizeof(*tidbase));

	for (i = 0; i < rcd->rcvegrcnt; i++)
		qib_7220_put_tid(dd, &tidbase[i], RCVHQ_RCV_TYPE_EAGER,
				 tidinv);
}

/**
 * qib_7220_tidtemplate - setup constants for TID updates
 * @dd: the qlogic_ib device
 *
 * We setup stuff that we use a lot, to avoid calculating each time
 */
static void qib_7220_tidtemplate(struct qib_devdata *dd)
{
	if (dd->rcvegrbufsize == 2048)
		dd->tidtemplate = IBA7220_TID_SZ_2K;
	else if (dd->rcvegrbufsize == 4096)
		dd->tidtemplate = IBA7220_TID_SZ_4K;
	dd->tidinvalid = 0;
}

/**
 * qib_init_7220_get_base_info - set chip-specific flags for user code
 * @rcd: the qlogic_ib ctxt
 * @kbase: qib_base_info pointer
 *
 * We set the PCIE flag because the lower bandwidth on PCIe vs
 * HyperTransport can affect some user packet algorithims.
 */
static int qib_7220_get_base_info(struct qib_ctxtdata *rcd,
				  struct qib_base_info *kinfo)
{
	kinfo->spi_runtime_flags |= QIB_RUNTIME_PCIE |
		QIB_RUNTIME_NODMA_RTAIL | QIB_RUNTIME_SDMA;

	if (rcd->dd->flags & QIB_USE_SPCL_TRIG)
		kinfo->spi_runtime_flags |= QIB_RUNTIME_SPECIAL_TRIGGER;

	return 0;
}

static struct qib_message_header *
qib_7220_get_msgheader(struct qib_devdata *dd, __le32 *rhf_addr)
{
	u32 offset = qib_hdrget_offset(rhf_addr);

	return (struct qib_message_header *)
		(rhf_addr - dd->rhf_offset + offset);
}

static void qib_7220_config_ctxts(struct qib_devdata *dd)
{
	unsigned long flags;
	u32 nchipctxts;

	nchipctxts = qib_read_kreg32(dd, kr_portcnt);
	dd->cspec->numctxts = nchipctxts;
	if (qib_n_krcv_queues > 1) {
		dd->qpn_mask = 0x3e;
		dd->first_user_ctxt = qib_n_krcv_queues * dd->num_pports;
		if (dd->first_user_ctxt > nchipctxts)
			dd->first_user_ctxt = nchipctxts;
	} else
		dd->first_user_ctxt = dd->num_pports;
	dd->n_krcv_queues = dd->first_user_ctxt;

	if (!qib_cfgctxts) {
		int nctxts = dd->first_user_ctxt + num_online_cpus();

		if (nctxts <= 5)
			dd->ctxtcnt = 5;
		else if (nctxts <= 9)
			dd->ctxtcnt = 9;
		else if (nctxts <= nchipctxts)
			dd->ctxtcnt = nchipctxts;
	} else if (qib_cfgctxts <= nchipctxts)
		dd->ctxtcnt = qib_cfgctxts;
	if (!dd->ctxtcnt) /* none of the above, set to max */
		dd->ctxtcnt = nchipctxts;

	/*
	 * Chip can be configured for 5, 9, or 17 ctxts, and choice
	 * affects number of eager TIDs per ctxt (1K, 2K, 4K).
	 * Lock to be paranoid about later motion, etc.
	 */
	spin_lock_irqsave(&dd->cspec->rcvmod_lock, flags);
	if (dd->ctxtcnt > 9)
		dd->rcvctrl |= 2ULL << IBA7220_R_CTXTCFG_SHIFT;
	else if (dd->ctxtcnt > 5)
		dd->rcvctrl |= 1ULL << IBA7220_R_CTXTCFG_SHIFT;
	/* else configure for default 5 receive ctxts */
	if (dd->qpn_mask)
		dd->rcvctrl |= 1ULL << QIB_7220_RcvCtrl_RcvQPMapEnable_LSB;
	qib_write_kreg(dd, kr_rcvctrl, dd->rcvctrl);
	spin_unlock_irqrestore(&dd->cspec->rcvmod_lock, flags);

	/* kr_rcvegrcnt changes based on the number of contexts enabled */
	dd->cspec->rcvegrcnt = qib_read_kreg32(dd, kr_rcvegrcnt);
	dd->rcvhdrcnt = max(dd->cspec->rcvegrcnt, IBA7220_KRCVEGRCNT);
}

static int qib_7220_get_ib_cfg(struct qib_pportdata *ppd, int which)
{
	int lsb, ret = 0;
	u64 maskr; /* right-justified mask */

	switch (which) {
	case QIB_IB_CFG_LWID_ENB: /* Get allowed Link-width */
		ret = ppd->link_width_enabled;
		goto done;

	case QIB_IB_CFG_LWID: /* Get currently active Link-width */
		ret = ppd->link_width_active;
		goto done;

	case QIB_IB_CFG_SPD_ENB: /* Get allowed Link speeds */
		ret = ppd->link_speed_enabled;
		goto done;

	case QIB_IB_CFG_SPD: /* Get current Link spd */
		ret = ppd->link_speed_active;
		goto done;

	case QIB_IB_CFG_RXPOL_ENB: /* Get Auto-RX-polarity enable */
		lsb = IBA7220_IBC_RXPOL_SHIFT;
		maskr = IBA7220_IBC_RXPOL_MASK;
		break;

	case QIB_IB_CFG_LREV_ENB: /* Get Auto-Lane-reversal enable */
		lsb = IBA7220_IBC_LREV_SHIFT;
		maskr = IBA7220_IBC_LREV_MASK;
		break;

	case QIB_IB_CFG_LINKLATENCY:
		ret = qib_read_kreg64(ppd->dd, kr_ibcddrstatus)
			& IBA7220_DDRSTAT_LINKLAT_MASK;
		goto done;

	case QIB_IB_CFG_OP_VLS:
		ret = ppd->vls_operational;
		goto done;

	case QIB_IB_CFG_VL_HIGH_CAP:
		ret = 0;
		goto done;

	case QIB_IB_CFG_VL_LOW_CAP:
		ret = 0;
		goto done;

	case QIB_IB_CFG_OVERRUN_THRESH: /* IB overrun threshold */
		ret = SYM_FIELD(ppd->cpspec->ibcctrl, IBCCtrl,
				OverrunThreshold);
		goto done;

	case QIB_IB_CFG_PHYERR_THRESH: /* IB PHY error threshold */
		ret = SYM_FIELD(ppd->cpspec->ibcctrl, IBCCtrl,
				PhyerrThreshold);
		goto done;

	case QIB_IB_CFG_LINKDEFAULT: /* IB link default (sleep/poll) */
		/* will only take effect when the link state changes */
		ret = (ppd->cpspec->ibcctrl &
		       SYM_MASK(IBCCtrl, LinkDownDefaultState)) ?
			IB_LINKINITCMD_SLEEP : IB_LINKINITCMD_POLL;
		goto done;

	case QIB_IB_CFG_HRTBT: /* Get Heartbeat off/enable/auto */
		lsb = IBA7220_IBC_HRTBT_SHIFT;
		maskr = IBA7220_IBC_HRTBT_MASK;
		break;

	case QIB_IB_CFG_PMA_TICKS:
		/*
		 * 0x00 = 10x link transfer rate or 4 nsec. for 2.5Gbs
		 * Since the clock is always 250MHz, the value is 1 or 0.
		 */
		ret = (ppd->link_speed_active == QIB_IB_DDR);
		goto done;

	default:
		ret = -EINVAL;
		goto done;
	}
	ret = (int)((ppd->cpspec->ibcddrctrl >> lsb) & maskr);
done:
	return ret;
}

static int qib_7220_set_ib_cfg(struct qib_pportdata *ppd, int which, u32 val)
{
	struct qib_devdata *dd = ppd->dd;
	u64 maskr; /* right-justified mask */
	int lsb, ret = 0, setforce = 0;
	u16 lcmd, licmd;
	unsigned long flags;
	u32 tmp = 0;

	switch (which) {
	case QIB_IB_CFG_LIDLMC:
		/*
		 * Set LID and LMC. Combined to avoid possible hazard
		 * caller puts LMC in 16MSbits, DLID in 16LSbits of val
		 */
		lsb = IBA7220_IBC_DLIDLMC_SHIFT;
		maskr = IBA7220_IBC_DLIDLMC_MASK;
		break;

	case QIB_IB_CFG_LWID_ENB: /* set allowed Link-width */
		/*
		 * As with speed, only write the actual register if
		 * the link is currently down, otherwise takes effect
		 * on next link change.
		 */
		ppd->link_width_enabled = val;
		if (!(ppd->lflags & QIBL_LINKDOWN))
			goto bail;
		/*
		 * We set the QIBL_IB_FORCE_NOTIFY bit so updown
		 * will get called because we want update
		 * link_width_active, and the change may not take
		 * effect for some time (if we are in POLL), so this
		 * flag will force the updown routine to be called
		 * on the next ibstatuschange down interrupt, even
		 * if it's not an down->up transition.
		 */
		val--; /* convert from IB to chip */
		maskr = IBA7220_IBC_WIDTH_MASK;
		lsb = IBA7220_IBC_WIDTH_SHIFT;
		setforce = 1;
		break;

	case QIB_IB_CFG_SPD_ENB: /* set allowed Link speeds */
		/*
		 * If we turn off IB1.2, need to preset SerDes defaults,
		 * but not right now. Set a flag for the next time
		 * we command the link down.  As with width, only write the
		 * actual register if the link is currently down, otherwise
		 * takes effect on next link change.  Since setting is being
		 * explicitly requested (via MAD or sysfs), clear autoneg
		 * failure status if speed autoneg is enabled.
		 */
		ppd->link_speed_enabled = val;
		if ((ppd->cpspec->ibcddrctrl & IBA7220_IBC_IBTA_1_2_MASK) &&
		    !(val & (val - 1)))
			dd->cspec->presets_needed = 1;
		if (!(ppd->lflags & QIBL_LINKDOWN))
			goto bail;
		/*
		 * We set the QIBL_IB_FORCE_NOTIFY bit so updown
		 * will get called because we want update
		 * link_speed_active, and the change may not take
		 * effect for some time (if we are in POLL), so this
		 * flag will force the updown routine to be called
		 * on the next ibstatuschange down interrupt, even
		 * if it's not an down->up transition.
		 */
		if (val == (QIB_IB_SDR | QIB_IB_DDR)) {
			val = IBA7220_IBC_SPEED_AUTONEG_MASK |
				IBA7220_IBC_IBTA_1_2_MASK;
			spin_lock_irqsave(&ppd->lflags_lock, flags);
			ppd->lflags &= ~QIBL_IB_AUTONEG_FAILED;
			spin_unlock_irqrestore(&ppd->lflags_lock, flags);
		} else
			val = val == QIB_IB_DDR ?
				IBA7220_IBC_SPEED_DDR : IBA7220_IBC_SPEED_SDR;
		maskr = IBA7220_IBC_SPEED_AUTONEG_MASK |
			IBA7220_IBC_IBTA_1_2_MASK;
		/* IBTA 1.2 mode + speed bits are contiguous */
		lsb = SYM_LSB(IBCDDRCtrl, IB_ENHANCED_MODE);
		setforce = 1;
		break;

	case QIB_IB_CFG_RXPOL_ENB: /* set Auto-RX-polarity enable */
		lsb = IBA7220_IBC_RXPOL_SHIFT;
		maskr = IBA7220_IBC_RXPOL_MASK;
		break;

	case QIB_IB_CFG_LREV_ENB: /* set Auto-Lane-reversal enable */
		lsb = IBA7220_IBC_LREV_SHIFT;
		maskr = IBA7220_IBC_LREV_MASK;
		break;

	case QIB_IB_CFG_OVERRUN_THRESH: /* IB overrun threshold */
		maskr = SYM_FIELD(ppd->cpspec->ibcctrl, IBCCtrl,
				  OverrunThreshold);
		if (maskr != val) {
			ppd->cpspec->ibcctrl &=
				~SYM_MASK(IBCCtrl, OverrunThreshold);
			ppd->cpspec->ibcctrl |= (u64) val <<
				SYM_LSB(IBCCtrl, OverrunThreshold);
			qib_write_kreg(dd, kr_ibcctrl, ppd->cpspec->ibcctrl);
			qib_write_kreg(dd, kr_scratch, 0);
		}
		goto bail;

	case QIB_IB_CFG_PHYERR_THRESH: /* IB PHY error threshold */
		maskr = SYM_FIELD(ppd->cpspec->ibcctrl, IBCCtrl,
				  PhyerrThreshold);
		if (maskr != val) {
			ppd->cpspec->ibcctrl &=
				~SYM_MASK(IBCCtrl, PhyerrThreshold);
			ppd->cpspec->ibcctrl |= (u64) val <<
				SYM_LSB(IBCCtrl, PhyerrThreshold);
			qib_write_kreg(dd, kr_ibcctrl, ppd->cpspec->ibcctrl);
			qib_write_kreg(dd, kr_scratch, 0);
		}
		goto bail;

	case QIB_IB_CFG_PKEYS: /* update pkeys */
		maskr = (u64) ppd->pkeys[0] | ((u64) ppd->pkeys[1] << 16) |
			((u64) ppd->pkeys[2] << 32) |
			((u64) ppd->pkeys[3] << 48);
		qib_write_kreg(dd, kr_partitionkey, maskr);
		goto bail;

	case QIB_IB_CFG_LINKDEFAULT: /* IB link default (sleep/poll) */
		/* will only take effect when the link state changes */
		if (val == IB_LINKINITCMD_POLL)
			ppd->cpspec->ibcctrl &=
				~SYM_MASK(IBCCtrl, LinkDownDefaultState);
		else /* SLEEP */
			ppd->cpspec->ibcctrl |=
				SYM_MASK(IBCCtrl, LinkDownDefaultState);
		qib_write_kreg(dd, kr_ibcctrl, ppd->cpspec->ibcctrl);
		qib_write_kreg(dd, kr_scratch, 0);
		goto bail;

	case QIB_IB_CFG_MTU: /* update the MTU in IBC */
		/*
		 * Update our housekeeping variables, and set IBC max
		 * size, same as init code; max IBC is max we allow in
		 * buffer, less the qword pbc, plus 1 for ICRC, in dwords
		 * Set even if it's unchanged, print debug message only
		 * on changes.
		 */
		val = (ppd->ibmaxlen >> 2) + 1;
		ppd->cpspec->ibcctrl &= ~SYM_MASK(IBCCtrl, MaxPktLen);
		ppd->cpspec->ibcctrl |= (u64)val << SYM_LSB(IBCCtrl, MaxPktLen);
		qib_write_kreg(dd, kr_ibcctrl, ppd->cpspec->ibcctrl);
		qib_write_kreg(dd, kr_scratch, 0);
		goto bail;

	case QIB_IB_CFG_LSTATE: /* set the IB link state */
		switch (val & 0xffff0000) {
		case IB_LINKCMD_DOWN:
			lcmd = QLOGIC_IB_IBCC_LINKCMD_DOWN;
			if (!ppd->cpspec->ibdeltainprog &&
			    qib_compat_ddr_negotiate) {
				ppd->cpspec->ibdeltainprog = 1;
				ppd->cpspec->ibsymsnap =
					read_7220_creg32(dd, cr_ibsymbolerr);
				ppd->cpspec->iblnkerrsnap =
					read_7220_creg32(dd, cr_iblinkerrrecov);
			}
			break;

		case IB_LINKCMD_ARMED:
			lcmd = QLOGIC_IB_IBCC_LINKCMD_ARMED;
			break;

		case IB_LINKCMD_ACTIVE:
			lcmd = QLOGIC_IB_IBCC_LINKCMD_ACTIVE;
			break;

		default:
			ret = -EINVAL;
			qib_dev_err(dd, "bad linkcmd req 0x%x\n", val >> 16);
			goto bail;
		}
		switch (val & 0xffff) {
		case IB_LINKINITCMD_NOP:
			licmd = 0;
			break;

		case IB_LINKINITCMD_POLL:
			licmd = QLOGIC_IB_IBCC_LINKINITCMD_POLL;
			break;

		case IB_LINKINITCMD_SLEEP:
			licmd = QLOGIC_IB_IBCC_LINKINITCMD_SLEEP;
			break;

		case IB_LINKINITCMD_DISABLE:
			licmd = QLOGIC_IB_IBCC_LINKINITCMD_DISABLE;
			ppd->cpspec->chase_end = 0;
			/*
			 * stop state chase counter and timer, if running.
			 * wait forpending timer, but don't clear .data (ppd)!
			 */
			if (ppd->cpspec->chase_timer.expires) {
				del_timer_sync(&ppd->cpspec->chase_timer);
				ppd->cpspec->chase_timer.expires = 0;
			}
			break;

		default:
			ret = -EINVAL;
			qib_dev_err(dd, "bad linkinitcmd req 0x%x\n",
				    val & 0xffff);
			goto bail;
		}
		qib_set_ib_7220_lstate(ppd, lcmd, licmd);

		maskr = IBA7220_IBC_WIDTH_MASK;
		lsb = IBA7220_IBC_WIDTH_SHIFT;
		tmp = (ppd->cpspec->ibcddrctrl >> lsb) & maskr;
		/* If the width active on the chip does not match the
		 * width in the shadow register, write the new active
		 * width to the chip.
		 * We don't have to worry about speed as the speed is taken
		 * care of by set_7220_ibspeed_fast called by ib_updown.
		 */
		if (ppd->link_width_enabled-1 != tmp) {
			ppd->cpspec->ibcddrctrl &= ~(maskr << lsb);
			ppd->cpspec->ibcddrctrl |=
				(((u64)(ppd->link_width_enabled-1) & maskr) <<
				 lsb);
			qib_write_kreg(dd, kr_ibcddrctrl,
				       ppd->cpspec->ibcddrctrl);
			qib_write_kreg(dd, kr_scratch, 0);
			spin_lock_irqsave(&ppd->lflags_lock, flags);
			ppd->lflags |= QIBL_IB_FORCE_NOTIFY;
			spin_unlock_irqrestore(&ppd->lflags_lock, flags);
		}
		goto bail;

	case QIB_IB_CFG_HRTBT: /* set Heartbeat off/enable/auto */
		if (val > IBA7220_IBC_HRTBT_MASK) {
			ret = -EINVAL;
			goto bail;
		}
		lsb = IBA7220_IBC_HRTBT_SHIFT;
		maskr = IBA7220_IBC_HRTBT_MASK;
		break;

	default:
		ret = -EINVAL;
		goto bail;
	}
	ppd->cpspec->ibcddrctrl &= ~(maskr << lsb);
	ppd->cpspec->ibcddrctrl |= (((u64) val & maskr) << lsb);
	qib_write_kreg(dd, kr_ibcddrctrl, ppd->cpspec->ibcddrctrl);
	qib_write_kreg(dd, kr_scratch, 0);
	if (setforce) {
		spin_lock_irqsave(&ppd->lflags_lock, flags);
		ppd->lflags |= QIBL_IB_FORCE_NOTIFY;
		spin_unlock_irqrestore(&ppd->lflags_lock, flags);
	}
bail:
	return ret;
}

static int qib_7220_set_loopback(struct qib_pportdata *ppd, const char *what)
{
	int ret = 0;
	u64 val, ddr;

	if (!strncmp(what, "ibc", 3)) {
		ppd->cpspec->ibcctrl |= SYM_MASK(IBCCtrl, Loopback);
		val = 0; /* disable heart beat, so link will come up */
		qib_devinfo(ppd->dd->pcidev, "Enabling IB%u:%u IBC loopback\n",
			 ppd->dd->unit, ppd->port);
	} else if (!strncmp(what, "off", 3)) {
		ppd->cpspec->ibcctrl &= ~SYM_MASK(IBCCtrl, Loopback);
		/* enable heart beat again */
		val = IBA7220_IBC_HRTBT_MASK << IBA7220_IBC_HRTBT_SHIFT;
		qib_devinfo(ppd->dd->pcidev,
			"Disabling IB%u:%u IBC loopback (normal)\n",
			ppd->dd->unit, ppd->port);
	} else
		ret = -EINVAL;
	if (!ret) {
		qib_write_kreg(ppd->dd, kr_ibcctrl, ppd->cpspec->ibcctrl);
		ddr = ppd->cpspec->ibcddrctrl & ~(IBA7220_IBC_HRTBT_MASK
					     << IBA7220_IBC_HRTBT_SHIFT);
		ppd->cpspec->ibcddrctrl = ddr | val;
		qib_write_kreg(ppd->dd, kr_ibcddrctrl,
			       ppd->cpspec->ibcddrctrl);
		qib_write_kreg(ppd->dd, kr_scratch, 0);
	}
	return ret;
}

static void qib_update_7220_usrhead(struct qib_ctxtdata *rcd, u64 hd,
				    u32 updegr, u32 egrhd, u32 npkts)
{
	if (updegr)
		qib_write_ureg(rcd->dd, ur_rcvegrindexhead, egrhd, rcd->ctxt);
	mmiowb();
	qib_write_ureg(rcd->dd, ur_rcvhdrhead, hd, rcd->ctxt);
	mmiowb();
}

static u32 qib_7220_hdrqempty(struct qib_ctxtdata *rcd)
{
	u32 head, tail;

	head = qib_read_ureg32(rcd->dd, ur_rcvhdrhead, rcd->ctxt);
	if (rcd->rcvhdrtail_kvaddr)
		tail = qib_get_rcvhdrtail(rcd);
	else
		tail = qib_read_ureg32(rcd->dd, ur_rcvhdrtail, rcd->ctxt);
	return head == tail;
}

/*
 * Modify the RCVCTRL register in chip-specific way. This
 * is a function because bit positions and (future) register
 * location is chip-specifc, but the needed operations are
 * generic. <op> is a bit-mask because we often want to
 * do multiple modifications.
 */
static void rcvctrl_7220_mod(struct qib_pportdata *ppd, unsigned int op,
			     int ctxt)
{
	struct qib_devdata *dd = ppd->dd;
	u64 mask, val;
	unsigned long flags;

	spin_lock_irqsave(&dd->cspec->rcvmod_lock, flags);
	if (op & QIB_RCVCTRL_TAILUPD_ENB)
		dd->rcvctrl |= (1ULL << IBA7220_R_TAILUPD_SHIFT);
	if (op & QIB_RCVCTRL_TAILUPD_DIS)
		dd->rcvctrl &= ~(1ULL << IBA7220_R_TAILUPD_SHIFT);
	if (op & QIB_RCVCTRL_PKEY_ENB)
		dd->rcvctrl &= ~(1ULL << IBA7220_R_PKEY_DIS_SHIFT);
	if (op & QIB_RCVCTRL_PKEY_DIS)
		dd->rcvctrl |= (1ULL << IBA7220_R_PKEY_DIS_SHIFT);
	if (ctxt < 0)
		mask = (1ULL << dd->ctxtcnt) - 1;
	else
		mask = (1ULL << ctxt);
	if (op & QIB_RCVCTRL_CTXT_ENB) {
		/* always done for specific ctxt */
		dd->rcvctrl |= (mask << SYM_LSB(RcvCtrl, PortEnable));
		if (!(dd->flags & QIB_NODMA_RTAIL))
			dd->rcvctrl |= 1ULL << IBA7220_R_TAILUPD_SHIFT;
		/* Write these registers before the context is enabled. */
		qib_write_kreg_ctxt(dd, kr_rcvhdrtailaddr, ctxt,
			dd->rcd[ctxt]->rcvhdrqtailaddr_phys);
		qib_write_kreg_ctxt(dd, kr_rcvhdraddr, ctxt,
			dd->rcd[ctxt]->rcvhdrq_phys);
		dd->rcd[ctxt]->seq_cnt = 1;
	}
	if (op & QIB_RCVCTRL_CTXT_DIS)
		dd->rcvctrl &= ~(mask << SYM_LSB(RcvCtrl, PortEnable));
	if (op & QIB_RCVCTRL_INTRAVAIL_ENB)
		dd->rcvctrl |= (mask << IBA7220_R_INTRAVAIL_SHIFT);
	if (op & QIB_RCVCTRL_INTRAVAIL_DIS)
		dd->rcvctrl &= ~(mask << IBA7220_R_INTRAVAIL_SHIFT);
	qib_write_kreg(dd, kr_rcvctrl, dd->rcvctrl);
	if ((op & QIB_RCVCTRL_INTRAVAIL_ENB) && dd->rhdrhead_intr_off) {
		/* arm rcv interrupt */
		val = qib_read_ureg32(dd, ur_rcvhdrhead, ctxt) |
			dd->rhdrhead_intr_off;
		qib_write_ureg(dd, ur_rcvhdrhead, val, ctxt);
	}
	if (op & QIB_RCVCTRL_CTXT_ENB) {
		/*
		 * Init the context registers also; if we were
		 * disabled, tail and head should both be zero
		 * already from the enable, but since we don't
		 * know, we have to do it explicitly.
		 */
		val = qib_read_ureg32(dd, ur_rcvegrindextail, ctxt);
		qib_write_ureg(dd, ur_rcvegrindexhead, val, ctxt);

		val = qib_read_ureg32(dd, ur_rcvhdrtail, ctxt);
		dd->rcd[ctxt]->head = val;
		/* If kctxt, interrupt on next receive. */
		if (ctxt < dd->first_user_ctxt)
			val |= dd->rhdrhead_intr_off;
		qib_write_ureg(dd, ur_rcvhdrhead, val, ctxt);
	}
	if (op & QIB_RCVCTRL_CTXT_DIS) {
		if (ctxt >= 0) {
			qib_write_kreg_ctxt(dd, kr_rcvhdrtailaddr, ctxt, 0);
			qib_write_kreg_ctxt(dd, kr_rcvhdraddr, ctxt, 0);
		} else {
			unsigned i;

			for (i = 0; i < dd->cfgctxts; i++) {
				qib_write_kreg_ctxt(dd, kr_rcvhdrtailaddr,
						    i, 0);
				qib_write_kreg_ctxt(dd, kr_rcvhdraddr, i, 0);
			}
		}
	}
	spin_unlock_irqrestore(&dd->cspec->rcvmod_lock, flags);
}

/*
 * Modify the SENDCTRL register in chip-specific way. This
 * is a function there may be multiple such registers with
 * slightly different layouts. To start, we assume the
 * "canonical" register layout of the first chips.
 * Chip requires no back-back sendctrl writes, so write
 * scratch register after writing sendctrl
 */
static void sendctrl_7220_mod(struct qib_pportdata *ppd, u32 op)
{
	struct qib_devdata *dd = ppd->dd;
	u64 tmp_dd_sendctrl;
	unsigned long flags;

	spin_lock_irqsave(&dd->sendctrl_lock, flags);

	/* First the ones that are "sticky", saved in shadow */
	if (op & QIB_SENDCTRL_CLEAR)
		dd->sendctrl = 0;
	if (op & QIB_SENDCTRL_SEND_DIS)
		dd->sendctrl &= ~SYM_MASK(SendCtrl, SPioEnable);
	else if (op & QIB_SENDCTRL_SEND_ENB) {
		dd->sendctrl |= SYM_MASK(SendCtrl, SPioEnable);
		if (dd->flags & QIB_USE_SPCL_TRIG)
			dd->sendctrl |= SYM_MASK(SendCtrl,
						 SSpecialTriggerEn);
	}
	if (op & QIB_SENDCTRL_AVAIL_DIS)
		dd->sendctrl &= ~SYM_MASK(SendCtrl, SendBufAvailUpd);
	else if (op & QIB_SENDCTRL_AVAIL_ENB)
		dd->sendctrl |= SYM_MASK(SendCtrl, SendBufAvailUpd);

	if (op & QIB_SENDCTRL_DISARM_ALL) {
		u32 i, last;

		tmp_dd_sendctrl = dd->sendctrl;
		/*
		 * disarm any that are not yet launched, disabling sends
		 * and updates until done.
		 */
		last = dd->piobcnt2k + dd->piobcnt4k;
		tmp_dd_sendctrl &=
			~(SYM_MASK(SendCtrl, SPioEnable) |
			  SYM_MASK(SendCtrl, SendBufAvailUpd));
		for (i = 0; i < last; i++) {
			qib_write_kreg(dd, kr_sendctrl,
				       tmp_dd_sendctrl |
				       SYM_MASK(SendCtrl, Disarm) | i);
			qib_write_kreg(dd, kr_scratch, 0);
		}
	}

	tmp_dd_sendctrl = dd->sendctrl;

	if (op & QIB_SENDCTRL_FLUSH)
		tmp_dd_sendctrl |= SYM_MASK(SendCtrl, Abort);
	if (op & QIB_SENDCTRL_DISARM)
		tmp_dd_sendctrl |= SYM_MASK(SendCtrl, Disarm) |
			((op & QIB_7220_SendCtrl_DisarmPIOBuf_RMASK) <<
			 SYM_LSB(SendCtrl, DisarmPIOBuf));
	if ((op & QIB_SENDCTRL_AVAIL_BLIP) &&
	    (dd->sendctrl & SYM_MASK(SendCtrl, SendBufAvailUpd)))
		tmp_dd_sendctrl &= ~SYM_MASK(SendCtrl, SendBufAvailUpd);

	qib_write_kreg(dd, kr_sendctrl, tmp_dd_sendctrl);
	qib_write_kreg(dd, kr_scratch, 0);

	if (op & QIB_SENDCTRL_AVAIL_BLIP) {
		qib_write_kreg(dd, kr_sendctrl, dd->sendctrl);
		qib_write_kreg(dd, kr_scratch, 0);
	}

	spin_unlock_irqrestore(&dd->sendctrl_lock, flags);

	if (op & QIB_SENDCTRL_FLUSH) {
		u32 v;
		/*
		 * ensure writes have hit chip, then do a few
		 * more reads, to allow DMA of pioavail registers
		 * to occur, so in-memory copy is in sync with
		 * the chip.  Not always safe to sleep.
		 */
		v = qib_read_kreg32(dd, kr_scratch);
		qib_write_kreg(dd, kr_scratch, v);
		v = qib_read_kreg32(dd, kr_scratch);
		qib_write_kreg(dd, kr_scratch, v);
		qib_read_kreg32(dd, kr_scratch);
	}
}

/**
 * qib_portcntr_7220 - read a per-port counter
 * @dd: the qlogic_ib device
 * @creg: the counter to snapshot
 */
static u64 qib_portcntr_7220(struct qib_pportdata *ppd, u32 reg)
{
	u64 ret = 0ULL;
	struct qib_devdata *dd = ppd->dd;
	u16 creg;
	/* 0xffff for unimplemented or synthesized counters */
	static const u16 xlator[] = {
		[QIBPORTCNTR_PKTSEND] = cr_pktsend,
		[QIBPORTCNTR_WORDSEND] = cr_wordsend,
		[QIBPORTCNTR_PSXMITDATA] = cr_psxmitdatacount,
		[QIBPORTCNTR_PSXMITPKTS] = cr_psxmitpktscount,
		[QIBPORTCNTR_PSXMITWAIT] = cr_psxmitwaitcount,
		[QIBPORTCNTR_SENDSTALL] = cr_sendstall,
		[QIBPORTCNTR_PKTRCV] = cr_pktrcv,
		[QIBPORTCNTR_PSRCVDATA] = cr_psrcvdatacount,
		[QIBPORTCNTR_PSRCVPKTS] = cr_psrcvpktscount,
		[QIBPORTCNTR_RCVEBP] = cr_rcvebp,
		[QIBPORTCNTR_RCVOVFL] = cr_rcvovfl,
		[QIBPORTCNTR_WORDRCV] = cr_wordrcv,
		[QIBPORTCNTR_RXDROPPKT] = cr_rxdroppkt,
		[QIBPORTCNTR_RXLOCALPHYERR] = cr_rxotherlocalphyerr,
		[QIBPORTCNTR_RXVLERR] = cr_rxvlerr,
		[QIBPORTCNTR_ERRICRC] = cr_erricrc,
		[QIBPORTCNTR_ERRVCRC] = cr_errvcrc,
		[QIBPORTCNTR_ERRLPCRC] = cr_errlpcrc,
		[QIBPORTCNTR_BADFORMAT] = cr_badformat,
		[QIBPORTCNTR_ERR_RLEN] = cr_err_rlen,
		[QIBPORTCNTR_IBSYMBOLERR] = cr_ibsymbolerr,
		[QIBPORTCNTR_INVALIDRLEN] = cr_invalidrlen,
		[QIBPORTCNTR_UNSUPVL] = cr_txunsupvl,
		[QIBPORTCNTR_EXCESSBUFOVFL] = cr_excessbufferovfl,
		[QIBPORTCNTR_ERRLINK] = cr_errlink,
		[QIBPORTCNTR_IBLINKDOWN] = cr_iblinkdown,
		[QIBPORTCNTR_IBLINKERRRECOV] = cr_iblinkerrrecov,
		[QIBPORTCNTR_LLI] = cr_locallinkintegrityerr,
		[QIBPORTCNTR_PSINTERVAL] = cr_psinterval,
		[QIBPORTCNTR_PSSTART] = cr_psstart,
		[QIBPORTCNTR_PSSTAT] = cr_psstat,
		[QIBPORTCNTR_VL15PKTDROP] = cr_vl15droppedpkt,
		[QIBPORTCNTR_ERRPKEY] = cr_errpkey,
		[QIBPORTCNTR_KHDROVFL] = 0xffff,
	};

	if (reg >= ARRAY_SIZE(xlator)) {
		qib_devinfo(ppd->dd->pcidev,
			 "Unimplemented portcounter %u\n", reg);
		goto done;
	}
	creg = xlator[reg];

	if (reg == QIBPORTCNTR_KHDROVFL) {
		int i;

		/* sum over all kernel contexts */
		for (i = 0; i < dd->first_user_ctxt; i++)
			ret += read_7220_creg32(dd, cr_portovfl + i);
	}
	if (creg == 0xffff)
		goto done;

	/*
	 * only fast incrementing counters are 64bit; use 32 bit reads to
	 * avoid two independent reads when on opteron
	 */
	if ((creg == cr_wordsend || creg == cr_wordrcv ||
	     creg == cr_pktsend || creg == cr_pktrcv))
		ret = read_7220_creg(dd, creg);
	else
		ret = read_7220_creg32(dd, creg);
	if (creg == cr_ibsymbolerr) {
		if (dd->pport->cpspec->ibdeltainprog)
			ret -= ret - ppd->cpspec->ibsymsnap;
		ret -= dd->pport->cpspec->ibsymdelta;
	} else if (creg == cr_iblinkerrrecov) {
		if (dd->pport->cpspec->ibdeltainprog)
			ret -= ret - ppd->cpspec->iblnkerrsnap;
		ret -= dd->pport->cpspec->iblnkerrdelta;
	}
done:
	return ret;
}

/*
 * Device counter names (not port-specific), one line per stat,
 * single string.  Used by utilities like ipathstats to print the stats
 * in a way which works for different versions of drivers, without changing
 * the utility.  Names need to be 12 chars or less (w/o newline), for proper
 * display by utility.
 * Non-error counters are first.
 * Start of "error" conters is indicated by a leading "E " on the first
 * "error" counter, and doesn't count in label length.
 * The EgrOvfl list needs to be last so we truncate them at the configured
 * context count for the device.
 * cntr7220indices contains the corresponding register indices.
 */
static const char cntr7220names[] =
	"Interrupts\n"
	"HostBusStall\n"
	"E RxTIDFull\n"
	"RxTIDInvalid\n"
	"Ctxt0EgrOvfl\n"
	"Ctxt1EgrOvfl\n"
	"Ctxt2EgrOvfl\n"
	"Ctxt3EgrOvfl\n"
	"Ctxt4EgrOvfl\n"
	"Ctxt5EgrOvfl\n"
	"Ctxt6EgrOvfl\n"
	"Ctxt7EgrOvfl\n"
	"Ctxt8EgrOvfl\n"
	"Ctxt9EgrOvfl\n"
	"Ctx10EgrOvfl\n"
	"Ctx11EgrOvfl\n"
	"Ctx12EgrOvfl\n"
	"Ctx13EgrOvfl\n"
	"Ctx14EgrOvfl\n"
	"Ctx15EgrOvfl\n"
	"Ctx16EgrOvfl\n";

static const size_t cntr7220indices[] = {
	cr_lbint,
	cr_lbflowstall,
	cr_errtidfull,
	cr_errtidvalid,
	cr_portovfl + 0,
	cr_portovfl + 1,
	cr_portovfl + 2,
	cr_portovfl + 3,
	cr_portovfl + 4,
	cr_portovfl + 5,
	cr_portovfl + 6,
	cr_portovfl + 7,
	cr_portovfl + 8,
	cr_portovfl + 9,
	cr_portovfl + 10,
	cr_portovfl + 11,
	cr_portovfl + 12,
	cr_portovfl + 13,
	cr_portovfl + 14,
	cr_portovfl + 15,
	cr_portovfl + 16,
};

/*
 * same as cntr7220names and cntr7220indices, but for port-specific counters.
 * portcntr7220indices is somewhat complicated by some registers needing
 * adjustments of various kinds, and those are ORed with _PORT_VIRT_FLAG
 */
static const char portcntr7220names[] =
	"TxPkt\n"
	"TxFlowPkt\n"
	"TxWords\n"
	"RxPkt\n"
	"RxFlowPkt\n"
	"RxWords\n"
	"TxFlowStall\n"
	"TxDmaDesc\n"  /* 7220 and 7322-only */
	"E RxDlidFltr\n"  /* 7220 and 7322-only */
	"IBStatusChng\n"
	"IBLinkDown\n"
	"IBLnkRecov\n"
	"IBRxLinkErr\n"
	"IBSymbolErr\n"
	"RxLLIErr\n"
	"RxBadFormat\n"
	"RxBadLen\n"
	"RxBufOvrfl\n"
	"RxEBP\n"
	"RxFlowCtlErr\n"
	"RxICRCerr\n"
	"RxLPCRCerr\n"
	"RxVCRCerr\n"
	"RxInvalLen\n"
	"RxInvalPKey\n"
	"RxPktDropped\n"
	"TxBadLength\n"
	"TxDropped\n"
	"TxInvalLen\n"
	"TxUnderrun\n"
	"TxUnsupVL\n"
	"RxLclPhyErr\n" /* 7220 and 7322-only */
	"RxVL15Drop\n" /* 7220 and 7322-only */
	"RxVlErr\n" /* 7220 and 7322-only */
	"XcessBufOvfl\n" /* 7220 and 7322-only */
	;

#define _PORT_VIRT_FLAG 0x8000 /* "virtual", need adjustments */
static const size_t portcntr7220indices[] = {
	QIBPORTCNTR_PKTSEND | _PORT_VIRT_FLAG,
	cr_pktsendflow,
	QIBPORTCNTR_WORDSEND | _PORT_VIRT_FLAG,
	QIBPORTCNTR_PKTRCV | _PORT_VIRT_FLAG,
	cr_pktrcvflowctrl,
	QIBPORTCNTR_WORDRCV | _PORT_VIRT_FLAG,
	QIBPORTCNTR_SENDSTALL | _PORT_VIRT_FLAG,
	cr_txsdmadesc,
	cr_rxdlidfltr,
	cr_ibstatuschange,
	QIBPORTCNTR_IBLINKDOWN | _PORT_VIRT_FLAG,
	QIBPORTCNTR_IBLINKERRRECOV | _PORT_VIRT_FLAG,
	QIBPORTCNTR_ERRLINK | _PORT_VIRT_FLAG,
	QIBPORTCNTR_IBSYMBOLERR | _PORT_VIRT_FLAG,
	QIBPORTCNTR_LLI | _PORT_VIRT_FLAG,
	QIBPORTCNTR_BADFORMAT | _PORT_VIRT_FLAG,
	QIBPORTCNTR_ERR_RLEN | _PORT_VIRT_FLAG,
	QIBPORTCNTR_RCVOVFL | _PORT_VIRT_FLAG,
	QIBPORTCNTR_RCVEBP | _PORT_VIRT_FLAG,
	cr_rcvflowctrl_err,
	QIBPORTCNTR_ERRICRC | _PORT_VIRT_FLAG,
	QIBPORTCNTR_ERRLPCRC | _PORT_VIRT_FLAG,
	QIBPORTCNTR_ERRVCRC | _PORT_VIRT_FLAG,
	QIBPORTCNTR_INVALIDRLEN | _PORT_VIRT_FLAG,
	QIBPORTCNTR_ERRPKEY | _PORT_VIRT_FLAG,
	QIBPORTCNTR_RXDROPPKT | _PORT_VIRT_FLAG,
	cr_invalidslen,
	cr_senddropped,
	cr_errslen,
	cr_sendunderrun,
	cr_txunsupvl,
	QIBPORTCNTR_RXLOCALPHYERR | _PORT_VIRT_FLAG,
	QIBPORTCNTR_VL15PKTDROP | _PORT_VIRT_FLAG,
	QIBPORTCNTR_RXVLERR | _PORT_VIRT_FLAG,
	QIBPORTCNTR_EXCESSBUFOVFL | _PORT_VIRT_FLAG,
};

/* do all the setup to make the counter reads efficient later */
static void init_7220_cntrnames(struct qib_devdata *dd)
{
	int i, j = 0;
	char *s;

	for (i = 0, s = (char *)cntr7220names; s && j <= dd->cfgctxts;
	     i++) {
		/* we always have at least one counter before the egrovfl */
		if (!j && !strncmp("Ctxt0EgrOvfl", s + 1, 12))
			j = 1;
		s = strchr(s + 1, '\n');
		if (s && j)
			j++;
	}
	dd->cspec->ncntrs = i;
	if (!s)
		/* full list; size is without terminating null */
		dd->cspec->cntrnamelen = sizeof(cntr7220names) - 1;
	else
		dd->cspec->cntrnamelen = 1 + s - cntr7220names;
	dd->cspec->cntrs = kmalloc(dd->cspec->ncntrs
		* sizeof(u64), GFP_KERNEL);

	for (i = 0, s = (char *)portcntr7220names; s; i++)
		s = strchr(s + 1, '\n');
	dd->cspec->nportcntrs = i - 1;
	dd->cspec->portcntrnamelen = sizeof(portcntr7220names) - 1;
	dd->cspec->portcntrs = kmalloc(dd->cspec->nportcntrs
		* sizeof(u64), GFP_KERNEL);
}

static u32 qib_read_7220cntrs(struct qib_devdata *dd, loff_t pos, char **namep,
			      u64 **cntrp)
{
	u32 ret;

	if (!dd->cspec->cntrs) {
		ret = 0;
		goto done;
	}

	if (namep) {
		*namep = (char *)cntr7220names;
		ret = dd->cspec->cntrnamelen;
		if (pos >= ret)
			ret = 0; /* final read after getting everything */
	} else {
		u64 *cntr = dd->cspec->cntrs;
		int i;

		ret = dd->cspec->ncntrs * sizeof(u64);
		if (!cntr || pos >= ret) {
			/* everything read, or couldn't get memory */
			ret = 0;
			goto done;
		}

		*cntrp = cntr;
		for (i = 0; i < dd->cspec->ncntrs; i++)
			*cntr++ = read_7220_creg32(dd, cntr7220indices[i]);
	}
done:
	return ret;
}

static u32 qib_read_7220portcntrs(struct qib_devdata *dd, loff_t pos, u32 port,
				  char **namep, u64 **cntrp)
{
	u32 ret;

	if (!dd->cspec->portcntrs) {
		ret = 0;
		goto done;
	}
	if (namep) {
		*namep = (char *)portcntr7220names;
		ret = dd->cspec->portcntrnamelen;
		if (pos >= ret)
			ret = 0; /* final read after getting everything */
	} else {
		u64 *cntr = dd->cspec->portcntrs;
		struct qib_pportdata *ppd = &dd->pport[port];
		int i;

		ret = dd->cspec->nportcntrs * sizeof(u64);
		if (!cntr || pos >= ret) {
			/* everything read, or couldn't get memory */
			ret = 0;
			goto done;
		}
		*cntrp = cntr;
		for (i = 0; i < dd->cspec->nportcntrs; i++) {
			if (portcntr7220indices[i] & _PORT_VIRT_FLAG)
				*cntr++ = qib_portcntr_7220(ppd,
					portcntr7220indices[i] &
					~_PORT_VIRT_FLAG);
			else
				*cntr++ = read_7220_creg32(dd,
					   portcntr7220indices[i]);
		}
	}
done:
	return ret;
}

/**
 * qib_get_7220_faststats - get word counters from chip before they overflow
 * @opaque - contains a pointer to the qlogic_ib device qib_devdata
 *
 * This needs more work; in particular, decision on whether we really
 * need traffic_wds done the way it is
 * called from add_timer
 */
static void qib_get_7220_faststats(unsigned long opaque)
{
	struct qib_devdata *dd = (struct qib_devdata *) opaque;
	struct qib_pportdata *ppd = dd->pport;
	unsigned long flags;
	u64 traffic_wds;

	/*
	 * don't access the chip while running diags, or memory diags can
	 * fail
	 */
	if (!(dd->flags & QIB_INITTED) || dd->diag_client)
		/* but re-arm the timer, for diags case; won't hurt other */
		goto done;

	/*
	 * We now try to maintain an activity timer, based on traffic
	 * exceeding a threshold, so we need to check the word-counts
	 * even if they are 64-bit.
	 */
	traffic_wds = qib_portcntr_7220(ppd, cr_wordsend) +
		qib_portcntr_7220(ppd, cr_wordrcv);
	spin_lock_irqsave(&dd->eep_st_lock, flags);
	traffic_wds -= dd->traffic_wds;
	dd->traffic_wds += traffic_wds;
	spin_unlock_irqrestore(&dd->eep_st_lock, flags);
done:
	mod_timer(&dd->stats_timer, jiffies + HZ * ACTIVITY_TIMER);
}

/*
 * If we are using MSI, try to fallback to INTx.
 */
static int qib_7220_intr_fallback(struct qib_devdata *dd)
{
	if (!dd->msi_lo)
		return 0;

	qib_devinfo(dd->pcidev,
		"MSI interrupt not detected, trying INTx interrupts\n");
	qib_7220_free_irq(dd);
	qib_enable_intx(dd->pcidev);
	/*
	 * Some newer kernels require free_irq before disable_msi,
	 * and irq can be changed during disable and INTx enable
	 * and we need to therefore use the pcidev->irq value,
	 * not our saved MSI value.
	 */
	dd->cspec->irq = dd->pcidev->irq;
	qib_setup_7220_interrupt(dd);
	return 1;
}

/*
 * Reset the XGXS (between serdes and IBC).  Slightly less intrusive
 * than resetting the IBC or external link state, and useful in some
 * cases to cause some retraining.  To do this right, we reset IBC
 * as well.
 */
static void qib_7220_xgxs_reset(struct qib_pportdata *ppd)
{
	u64 val, prev_val;
	struct qib_devdata *dd = ppd->dd;

	prev_val = qib_read_kreg64(dd, kr_xgxs_cfg);
	val = prev_val | QLOGIC_IB_XGXS_RESET;
	prev_val &= ~QLOGIC_IB_XGXS_RESET; /* be sure */
	qib_write_kreg(dd, kr_control,
		       dd->control & ~QLOGIC_IB_C_LINKENABLE);
	qib_write_kreg(dd, kr_xgxs_cfg, val);
	qib_read_kreg32(dd, kr_scratch);
	qib_write_kreg(dd, kr_xgxs_cfg, prev_val);
	qib_write_kreg(dd, kr_control, dd->control);
}

/*
 * For this chip, we want to use the same buffer every time
 * when we are trying to bring the link up (they are always VL15
 * packets).  At that link state the packet should always go out immediately
 * (or at least be discarded at the tx interface if the link is down).
 * If it doesn't, and the buffer isn't available, that means some other
 * sender has gotten ahead of us, and is preventing our packet from going
 * out.  In that case, we flush all packets, and try again.  If that still
 * fails, we fail the request, and hope things work the next time around.
 *
 * We don't need very complicated heuristics on whether the packet had
 * time to go out or not, since even at SDR 1X, it goes out in very short
 * time periods, covered by the chip reads done here and as part of the
 * flush.
 */
static u32 __iomem *get_7220_link_buf(struct qib_pportdata *ppd, u32 *bnum)
{
	u32 __iomem *buf;
	u32 lbuf = ppd->dd->cspec->lastbuf_for_pio;
	int do_cleanup;
	unsigned long flags;

	/*
	 * always blip to get avail list updated, since it's almost
	 * always needed, and is fairly cheap.
	 */
	sendctrl_7220_mod(ppd->dd->pport, QIB_SENDCTRL_AVAIL_BLIP);
	qib_read_kreg64(ppd->dd, kr_scratch); /* extra chip flush */
	buf = qib_getsendbuf_range(ppd->dd, bnum, lbuf, lbuf);
	if (buf)
		goto done;

	spin_lock_irqsave(&ppd->sdma_lock, flags);
	if (ppd->sdma_state.current_state == qib_sdma_state_s20_idle &&
	    ppd->sdma_state.current_state != qib_sdma_state_s00_hw_down) {
		__qib_sdma_process_event(ppd, qib_sdma_event_e00_go_hw_down);
		do_cleanup = 0;
	} else {
		do_cleanup = 1;
		qib_7220_sdma_hw_clean_up(ppd);
	}
	spin_unlock_irqrestore(&ppd->sdma_lock, flags);

	if (do_cleanup) {
		qib_read_kreg64(ppd->dd, kr_scratch); /* extra chip flush */
		buf = qib_getsendbuf_range(ppd->dd, bnum, lbuf, lbuf);
	}
done:
	return buf;
}

/*
 * This code for non-IBTA-compliant IB speed negotiation is only known to
 * work for the SDR to DDR transition, and only between an HCA and a switch
 * with recent firmware.  It is based on observed heuristics, rather than
 * actual knowledge of the non-compliant speed negotiation.
 * It has a number of hard-coded fields, since the hope is to rewrite this
 * when a spec is available on how the negoation is intended to work.
 */
static void autoneg_7220_sendpkt(struct qib_pportdata *ppd, u32 *hdr,
				 u32 dcnt, u32 *data)
{
	int i;
	u64 pbc;
	u32 __iomem *piobuf;
	u32 pnum;
	struct qib_devdata *dd = ppd->dd;

	i = 0;
	pbc = 7 + dcnt + 1; /* 7 dword header, dword data, icrc */
	pbc |= PBC_7220_VL15_SEND;
	while (!(piobuf = get_7220_link_buf(ppd, &pnum))) {
		if (i++ > 5)
			return;
		udelay(2);
	}
	sendctrl_7220_mod(dd->pport, QIB_SENDCTRL_DISARM_BUF(pnum));
	writeq(pbc, piobuf);
	qib_flush_wc();
	qib_pio_copy(piobuf + 2, hdr, 7);
	qib_pio_copy(piobuf + 9, data, dcnt);
	if (dd->flags & QIB_USE_SPCL_TRIG) {
		u32 spcl_off = (pnum >= dd->piobcnt2k) ? 2047 : 1023;

		qib_flush_wc();
		__raw_writel(0xaebecede, piobuf + spcl_off);
	}
	qib_flush_wc();
	qib_sendbuf_done(dd, pnum);
}

/*
 * _start packet gets sent twice at start, _done gets sent twice at end
 */
static void autoneg_7220_send(struct qib_pportdata *ppd, int which)
{
	struct qib_devdata *dd = ppd->dd;
	static u32 swapped;
	u32 dw, i, hcnt, dcnt, *data;
	static u32 hdr[7] = { 0xf002ffff, 0x48ffff, 0x6400abba };
	static u32 madpayload_start[0x40] = {
		0x1810103, 0x1, 0x0, 0x0, 0x2c90000, 0x2c9, 0x0, 0x0,
		0xffffffff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
		0x1, 0x1388, 0x15e, 0x1, /* rest 0's */
		};
	static u32 madpayload_done[0x40] = {
		0x1810103, 0x1, 0x0, 0x0, 0x2c90000, 0x2c9, 0x0, 0x0,
		0xffffffff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
		0x40000001, 0x1388, 0x15e, /* rest 0's */
		};

	dcnt = ARRAY_SIZE(madpayload_start);
	hcnt = ARRAY_SIZE(hdr);
	if (!swapped) {
		/* for maintainability, do it at runtime */
		for (i = 0; i < hcnt; i++) {
			dw = (__force u32) cpu_to_be32(hdr[i]);
			hdr[i] = dw;
		}
		for (i = 0; i < dcnt; i++) {
			dw = (__force u32) cpu_to_be32(madpayload_start[i]);
			madpayload_start[i] = dw;
			dw = (__force u32) cpu_to_be32(madpayload_done[i]);
			madpayload_done[i] = dw;
		}
		swapped = 1;
	}

	data = which ? madpayload_done : madpayload_start;

	autoneg_7220_sendpkt(ppd, hdr, dcnt, data);
	qib_read_kreg64(dd, kr_scratch);
	udelay(2);
	autoneg_7220_sendpkt(ppd, hdr, dcnt, data);
	qib_read_kreg64(dd, kr_scratch);
	udelay(2);
}

/*
 * Do the absolute minimum to cause an IB speed change, and make it
 * ready, but don't actually trigger the change.   The caller will
 * do that when ready (if link is in Polling training state, it will
 * happen immediately, otherwise when link next goes down)
 *
 * This routine should only be used as part of the DDR autonegotation
 * code for devices that are not compliant with IB 1.2 (or code that
 * fixes things up for same).
 *
 * When link has gone down, and autoneg enabled, or autoneg has
 * failed and we give up until next time we set both speeds, and
 * then we want IBTA enabled as well as "use max enabled speed.
 */
static void set_7220_ibspeed_fast(struct qib_pportdata *ppd, u32 speed)
{
	ppd->cpspec->ibcddrctrl &= ~(IBA7220_IBC_SPEED_AUTONEG_MASK |
		IBA7220_IBC_IBTA_1_2_MASK);

	if (speed == (QIB_IB_SDR | QIB_IB_DDR))
		ppd->cpspec->ibcddrctrl |= IBA7220_IBC_SPEED_AUTONEG_MASK |
			IBA7220_IBC_IBTA_1_2_MASK;
	else
		ppd->cpspec->ibcddrctrl |= speed == QIB_IB_DDR ?
			IBA7220_IBC_SPEED_DDR : IBA7220_IBC_SPEED_SDR;

	qib_write_kreg(ppd->dd, kr_ibcddrctrl, ppd->cpspec->ibcddrctrl);
	qib_write_kreg(ppd->dd, kr_scratch, 0);
}

/*
 * This routine is only used when we are not talking to another
 * IB 1.2-compliant device that we think can do DDR.
 * (This includes all existing switch chips as of Oct 2007.)
 * 1.2-compliant devices go directly to DDR prior to reaching INIT
 */
static void try_7220_autoneg(struct qib_pportdata *ppd)
{
	unsigned long flags;

	/*
	 * Required for older non-IB1.2 DDR switches.  Newer
	 * non-IB-compliant switches don't need it, but so far,
	 * aren't bothered by it either.  "Magic constant"
	 */
	qib_write_kreg(ppd->dd, kr_ncmodectrl, 0x3b9dc07);

	spin_lock_irqsave(&ppd->lflags_lock, flags);
	ppd->lflags |= QIBL_IB_AUTONEG_INPROG;
	spin_unlock_irqrestore(&ppd->lflags_lock, flags);
	autoneg_7220_send(ppd, 0);
	set_7220_ibspeed_fast(ppd, QIB_IB_DDR);

	toggle_7220_rclkrls(ppd->dd);
	/* 2 msec is minimum length of a poll cycle */
	queue_delayed_work(ib_wq, &ppd->cpspec->autoneg_work,
			   msecs_to_jiffies(2));
}

/*
 * Handle the empirically determined mechanism for auto-negotiation
 * of DDR speed with switches.
 */
static void autoneg_7220_work(struct work_struct *work)
{
	struct qib_pportdata *ppd;
	struct qib_devdata *dd;
	u64 startms;
	u32 i;
	unsigned long flags;

	ppd = &container_of(work, struct qib_chippport_specific,
			    autoneg_work.work)->pportdata;
	dd = ppd->dd;

	startms = jiffies_to_msecs(jiffies);

	/*
	 * Busy wait for this first part, it should be at most a
	 * few hundred usec, since we scheduled ourselves for 2msec.
	 */
	for (i = 0; i < 25; i++) {
		if (SYM_FIELD(ppd->lastibcstat, IBCStatus, LinkTrainingState)
		     == IB_7220_LT_STATE_POLLQUIET) {
			qib_set_linkstate(ppd, QIB_IB_LINKDOWN_DISABLE);
			break;
		}
		udelay(100);
	}

	if (!(ppd->lflags & QIBL_IB_AUTONEG_INPROG))
		goto done; /* we got there early or told to stop */

	/* we expect this to timeout */
	if (wait_event_timeout(ppd->cpspec->autoneg_wait,
			       !(ppd->lflags & QIBL_IB_AUTONEG_INPROG),
			       msecs_to_jiffies(90)))
		goto done;

	toggle_7220_rclkrls(dd);

	/* we expect this to timeout */
	if (wait_event_timeout(ppd->cpspec->autoneg_wait,
			       !(ppd->lflags & QIBL_IB_AUTONEG_INPROG),
			       msecs_to_jiffies(1700)))
		goto done;

	set_7220_ibspeed_fast(ppd, QIB_IB_SDR);
	toggle_7220_rclkrls(dd);

	/*
	 * Wait up to 250 msec for link to train and get to INIT at DDR;
	 * this should terminate early.
	 */
	wait_event_timeout(ppd->cpspec->autoneg_wait,
		!(ppd->lflags & QIBL_IB_AUTONEG_INPROG),
		msecs_to_jiffies(250));
done:
	if (ppd->lflags & QIBL_IB_AUTONEG_INPROG) {
		spin_lock_irqsave(&ppd->lflags_lock, flags);
		ppd->lflags &= ~QIBL_IB_AUTONEG_INPROG;
		if (dd->cspec->autoneg_tries == AUTONEG_TRIES) {
			ppd->lflags |= QIBL_IB_AUTONEG_FAILED;
			dd->cspec->autoneg_tries = 0;
		}
		spin_unlock_irqrestore(&ppd->lflags_lock, flags);
		set_7220_ibspeed_fast(ppd, ppd->link_speed_enabled);
	}
}

static u32 qib_7220_iblink_state(u64 ibcs)
{
	u32 state = (u32)SYM_FIELD(ibcs, IBCStatus, LinkState);

	switch (state) {
	case IB_7220_L_STATE_INIT:
		state = IB_PORT_INIT;
		break;
	case IB_7220_L_STATE_ARM:
		state = IB_PORT_ARMED;
		break;
	case IB_7220_L_STATE_ACTIVE:
		/* fall through */
	case IB_7220_L_STATE_ACT_DEFER:
		state = IB_PORT_ACTIVE;
		break;
	default: /* fall through */
	case IB_7220_L_STATE_DOWN:
		state = IB_PORT_DOWN;
		break;
	}
	return state;
}

/* returns the IBTA port state, rather than the IBC link training state */
static u8 qib_7220_phys_portstate(u64 ibcs)
{
	u8 state = (u8)SYM_FIELD(ibcs, IBCStatus, LinkTrainingState);
	return qib_7220_physportstate[state];
}

static int qib_7220_ib_updown(struct qib_pportdata *ppd, int ibup, u64 ibcs)
{
	int ret = 0, symadj = 0;
	struct qib_devdata *dd = ppd->dd;
	unsigned long flags;

	spin_lock_irqsave(&ppd->lflags_lock, flags);
	ppd->lflags &= ~QIBL_IB_FORCE_NOTIFY;
	spin_unlock_irqrestore(&ppd->lflags_lock, flags);

	if (!ibup) {
		/*
		 * When the link goes down we don't want AEQ running, so it
		 * won't interfere with IBC training, etc., and we need
		 * to go back to the static SerDes preset values.
		 */
		if (!(ppd->lflags & (QIBL_IB_AUTONEG_FAILED |
				     QIBL_IB_AUTONEG_INPROG)))
			set_7220_ibspeed_fast(ppd, ppd->link_speed_enabled);
		if (!(ppd->lflags & QIBL_IB_AUTONEG_INPROG)) {
			qib_sd7220_presets(dd);
			qib_cancel_sends(ppd); /* initial disarm, etc. */
			spin_lock_irqsave(&ppd->sdma_lock, flags);
			if (__qib_sdma_running(ppd))
				__qib_sdma_process_event(ppd,
					qib_sdma_event_e70_go_idle);
			spin_unlock_irqrestore(&ppd->sdma_lock, flags);
		}
		/* this might better in qib_sd7220_presets() */
		set_7220_relock_poll(dd, ibup);
	} else {
		if (qib_compat_ddr_negotiate &&
		    !(ppd->lflags & (QIBL_IB_AUTONEG_FAILED |
				     QIBL_IB_AUTONEG_INPROG)) &&
		    ppd->link_speed_active == QIB_IB_SDR &&
		    (ppd->link_speed_enabled & (QIB_IB_DDR | QIB_IB_SDR)) ==
		    (QIB_IB_DDR | QIB_IB_SDR) &&
		    dd->cspec->autoneg_tries < AUTONEG_TRIES) {
			/* we are SDR, and DDR auto-negotiation enabled */
			++dd->cspec->autoneg_tries;
			if (!ppd->cpspec->ibdeltainprog) {
				ppd->cpspec->ibdeltainprog = 1;
				ppd->cpspec->ibsymsnap = read_7220_creg32(dd,
					cr_ibsymbolerr);
				ppd->cpspec->iblnkerrsnap = read_7220_creg32(dd,
					cr_iblinkerrrecov);
			}
			try_7220_autoneg(ppd);
			ret = 1; /* no other IB status change processing */
		} else if ((ppd->lflags & QIBL_IB_AUTONEG_INPROG) &&
			   ppd->link_speed_active == QIB_IB_SDR) {
			autoneg_7220_send(ppd, 1);
			set_7220_ibspeed_fast(ppd, QIB_IB_DDR);
			udelay(2);
			toggle_7220_rclkrls(dd);
			ret = 1; /* no other IB status change processing */
		} else {
			if ((ppd->lflags & QIBL_IB_AUTONEG_INPROG) &&
			    (ppd->link_speed_active & QIB_IB_DDR)) {
				spin_lock_irqsave(&ppd->lflags_lock, flags);
				ppd->lflags &= ~(QIBL_IB_AUTONEG_INPROG |
						 QIBL_IB_AUTONEG_FAILED);
				spin_unlock_irqrestore(&ppd->lflags_lock,
						       flags);
				dd->cspec->autoneg_tries = 0;
				/* re-enable SDR, for next link down */
				set_7220_ibspeed_fast(ppd,
						      ppd->link_speed_enabled);
				wake_up(&ppd->cpspec->autoneg_wait);
				symadj = 1;
			} else if (ppd->lflags & QIBL_IB_AUTONEG_FAILED) {
				/*
				 * Clear autoneg failure flag, and do setup
				 * so we'll try next time link goes down and
				 * back to INIT (possibly connected to a
				 * different device).
				 */
				spin_lock_irqsave(&ppd->lflags_lock, flags);
				ppd->lflags &= ~QIBL_IB_AUTONEG_FAILED;
				spin_unlock_irqrestore(&ppd->lflags_lock,
						       flags);
				ppd->cpspec->ibcddrctrl |=
					IBA7220_IBC_IBTA_1_2_MASK;
				qib_write_kreg(dd, kr_ncmodectrl, 0);
				symadj = 1;
			}
		}

		if (!(ppd->lflags & QIBL_IB_AUTONEG_INPROG))
			symadj = 1;

		if (!ret) {
			ppd->delay_mult = rate_to_delay
			    [(ibcs >> IBA7220_LINKSPEED_SHIFT) & 1]
			    [(ibcs >> IBA7220_LINKWIDTH_SHIFT) & 1];

			set_7220_relock_poll(dd, ibup);
			spin_lock_irqsave(&ppd->sdma_lock, flags);
			/*
			 * Unlike 7322, the 7220 needs this, due to lack of
			 * interrupt in some cases when we have sdma active
			 * when the link goes down.
			 */
			if (ppd->sdma_state.current_state !=
			    qib_sdma_state_s20_idle)
				__qib_sdma_process_event(ppd,
					qib_sdma_event_e00_go_hw_down);
			spin_unlock_irqrestore(&ppd->sdma_lock, flags);
		}
	}

	if (symadj) {
		if (ppd->cpspec->ibdeltainprog) {
			ppd->cpspec->ibdeltainprog = 0;
			ppd->cpspec->ibsymdelta += read_7220_creg32(ppd->dd,
				cr_ibsymbolerr) - ppd->cpspec->ibsymsnap;
			ppd->cpspec->iblnkerrdelta += read_7220_creg32(ppd->dd,
				cr_iblinkerrrecov) - ppd->cpspec->iblnkerrsnap;
		}
	} else if (!ibup && qib_compat_ddr_negotiate &&
		   !ppd->cpspec->ibdeltainprog &&
			!(ppd->lflags & QIBL_IB_AUTONEG_INPROG)) {
		ppd->cpspec->ibdeltainprog = 1;
		ppd->cpspec->ibsymsnap = read_7220_creg32(ppd->dd,
							  cr_ibsymbolerr);
		ppd->cpspec->iblnkerrsnap = read_7220_creg32(ppd->dd,
						     cr_iblinkerrrecov);
	}

	if (!ret)
		qib_setup_7220_setextled(ppd, ibup);
	return ret;
}

/*
 * Does read/modify/write to appropriate registers to
 * set output and direction bits selected by mask.
 * these are in their canonical postions (e.g. lsb of
 * dir will end up in D48 of extctrl on existing chips).
 * returns contents of GP Inputs.
 */
static int gpio_7220_mod(struct qib_devdata *dd, u32 out, u32 dir, u32 mask)
{
	u64 read_val, new_out;
	unsigned long flags;

	if (mask) {
		/* some bits being written, lock access to GPIO */
		dir &= mask;
		out &= mask;
		spin_lock_irqsave(&dd->cspec->gpio_lock, flags);
		dd->cspec->extctrl &= ~((u64)mask << SYM_LSB(EXTCtrl, GPIOOe));
		dd->cspec->extctrl |= ((u64) dir << SYM_LSB(EXTCtrl, GPIOOe));
		new_out = (dd->cspec->gpio_out & ~mask) | out;

		qib_write_kreg(dd, kr_extctrl, dd->cspec->extctrl);
		qib_write_kreg(dd, kr_gpio_out, new_out);
		dd->cspec->gpio_out = new_out;
		spin_unlock_irqrestore(&dd->cspec->gpio_lock, flags);
	}
	/*
	 * It is unlikely that a read at this time would get valid
	 * data on a pin whose direction line was set in the same
	 * call to this function. We include the read here because
	 * that allows us to potentially combine a change on one pin with
	 * a read on another, and because the old code did something like
	 * this.
	 */
	read_val = qib_read_kreg64(dd, kr_extstatus);
	return SYM_FIELD(read_val, EXTStatus, GPIOIn);
}

/*
 * Read fundamental info we need to use the chip.  These are
 * the registers that describe chip capabilities, and are
 * saved in shadow registers.
 */
static void get_7220_chip_params(struct qib_devdata *dd)
{
	u64 val;
	u32 piobufs;
	int mtu;

	dd->uregbase = qib_read_kreg32(dd, kr_userregbase);

	dd->rcvtidcnt = qib_read_kreg32(dd, kr_rcvtidcnt);
	dd->rcvtidbase = qib_read_kreg32(dd, kr_rcvtidbase);
	dd->rcvegrbase = qib_read_kreg32(dd, kr_rcvegrbase);
	dd->palign = qib_read_kreg32(dd, kr_palign);
	dd->piobufbase = qib_read_kreg64(dd, kr_sendpiobufbase);
	dd->pio2k_bufbase = dd->piobufbase & 0xffffffff;

	val = qib_read_kreg64(dd, kr_sendpiosize);
	dd->piosize2k = val & ~0U;
	dd->piosize4k = val >> 32;

	mtu = ib_mtu_enum_to_int(qib_ibmtu);
	if (mtu == -1)
		mtu = QIB_DEFAULT_MTU;
	dd->pport->ibmtu = (u32)mtu;

	val = qib_read_kreg64(dd, kr_sendpiobufcnt);
	dd->piobcnt2k = val & ~0U;
	dd->piobcnt4k = val >> 32;
	/* these may be adjusted in init_chip_wc_pat() */
	dd->pio2kbase = (u32 __iomem *)
		((char __iomem *) dd->kregbase + dd->pio2k_bufbase);
	if (dd->piobcnt4k) {
		dd->pio4kbase = (u32 __iomem *)
			((char __iomem *) dd->kregbase +
			 (dd->piobufbase >> 32));
		/*
		 * 4K buffers take 2 pages; we use roundup just to be
		 * paranoid; we calculate it once here, rather than on
		 * ever buf allocate
		 */
		dd->align4k = ALIGN(dd->piosize4k, dd->palign);
	}

	piobufs = dd->piobcnt4k + dd->piobcnt2k;

	dd->pioavregs = ALIGN(piobufs, sizeof(u64) * BITS_PER_BYTE / 2) /
		(sizeof(u64) * BITS_PER_BYTE / 2);
}

/*
 * The chip base addresses in cspec and cpspec have to be set
 * after possible init_chip_wc_pat(), rather than in
 * qib_get_7220_chip_params(), so split out as separate function
 */
static void set_7220_baseaddrs(struct qib_devdata *dd)
{
	u32 cregbase;
	/* init after possible re-map in init_chip_wc_pat() */
	cregbase = qib_read_kreg32(dd, kr_counterregbase);
	dd->cspec->cregbase = (u64 __iomem *)
		((char __iomem *) dd->kregbase + cregbase);

	dd->egrtidbase = (u64 __iomem *)
		((char __iomem *) dd->kregbase + dd->rcvegrbase);
}


#define SENDCTRL_SHADOWED (SYM_MASK(SendCtrl, SendIntBufAvail) |	\
			   SYM_MASK(SendCtrl, SPioEnable) |		\
			   SYM_MASK(SendCtrl, SSpecialTriggerEn) |	\
			   SYM_MASK(SendCtrl, SendBufAvailUpd) |	\
			   SYM_MASK(SendCtrl, AvailUpdThld) |		\
			   SYM_MASK(SendCtrl, SDmaEnable) |		\
			   SYM_MASK(SendCtrl, SDmaIntEnable) |		\
			   SYM_MASK(SendCtrl, SDmaHalt) |		\
			   SYM_MASK(SendCtrl, SDmaSingleDescriptor))

static int sendctrl_hook(struct qib_devdata *dd,
			 const struct diag_observer *op,
			 u32 offs, u64 *data, u64 mask, int only_32)
{
	unsigned long flags;
	unsigned idx = offs / sizeof(u64);
	u64 local_data, all_bits;

	if (idx != kr_sendctrl) {
		qib_dev_err(dd, "SendCtrl Hook called with offs %X, %s-bit\n",
			    offs, only_32 ? "32" : "64");
		return 0;
	}

	all_bits = ~0ULL;
	if (only_32)
		all_bits >>= 32;
	spin_lock_irqsave(&dd->sendctrl_lock, flags);
	if ((mask & all_bits) != all_bits) {
		/*
		 * At least some mask bits are zero, so we need
		 * to read. The judgement call is whether from
		 * reg or shadow. First-cut: read reg, and complain
		 * if any bits which should be shadowed are different
		 * from their shadowed value.
		 */
		if (only_32)
			local_data = (u64)qib_read_kreg32(dd, idx);
		else
			local_data = qib_read_kreg64(dd, idx);
		qib_dev_err(dd, "Sendctrl -> %X, Shad -> %X\n",
			    (u32)local_data, (u32)dd->sendctrl);
		if ((local_data & SENDCTRL_SHADOWED) !=
		    (dd->sendctrl & SENDCTRL_SHADOWED))
			qib_dev_err(dd, "Sendctrl read: %X shadow is %X\n",
				(u32)local_data, (u32) dd->sendctrl);
		*data = (local_data & ~mask) | (*data & mask);
	}
	if (mask) {
		/*
		 * At least some mask bits are one, so we need
		 * to write, but only shadow some bits.
		 */
		u64 sval, tval; /* Shadowed, transient */

		/*
		 * New shadow val is bits we don't want to touch,
		 * ORed with bits we do, that are intended for shadow.
		 */
		sval = (dd->sendctrl & ~mask);
		sval |= *data & SENDCTRL_SHADOWED & mask;
		dd->sendctrl = sval;
		tval = sval | (*data & ~SENDCTRL_SHADOWED & mask);
		qib_dev_err(dd, "Sendctrl <- %X, Shad <- %X\n",
			    (u32)tval, (u32)sval);
		qib_write_kreg(dd, kr_sendctrl, tval);
		qib_write_kreg(dd, kr_scratch, 0Ull);
	}
	spin_unlock_irqrestore(&dd->sendctrl_lock, flags);

	return only_32 ? 4 : 8;
}

static const struct diag_observer sendctrl_observer = {
	sendctrl_hook, kr_sendctrl * sizeof(u64),
	kr_sendctrl * sizeof(u64)
};

/*
 * write the final few registers that depend on some of the
 * init setup.  Done late in init, just before bringing up
 * the serdes.
 */
static int qib_late_7220_initreg(struct qib_devdata *dd)
{
	int ret = 0;
	u64 val;

	qib_write_kreg(dd, kr_rcvhdrentsize, dd->rcvhdrentsize);
	qib_write_kreg(dd, kr_rcvhdrsize, dd->rcvhdrsize);
	qib_write_kreg(dd, kr_rcvhdrcnt, dd->rcvhdrcnt);
	qib_write_kreg(dd, kr_sendpioavailaddr, dd->pioavailregs_phys);
	val = qib_read_kreg64(dd, kr_sendpioavailaddr);
	if (val != dd->pioavailregs_phys) {
		qib_dev_err(dd,
			"Catastrophic software error, SendPIOAvailAddr written as %lx, read back as %llx\n",
			(unsigned long) dd->pioavailregs_phys,
			(unsigned long long) val);
		ret = -EINVAL;
	}
	qib_register_observer(dd, &sendctrl_observer);
	return ret;
}

static int qib_init_7220_variables(struct qib_devdata *dd)
{
	struct qib_chippport_specific *cpspec;
	struct qib_pportdata *ppd;
	int ret = 0;
	u32 sbufs, updthresh;

	cpspec = (struct qib_chippport_specific *)(dd + 1);
	ppd = &cpspec->pportdata;
	dd->pport = ppd;
	dd->num_pports = 1;

	dd->cspec = (struct qib_chip_specific *)(cpspec + dd->num_pports);
	ppd->cpspec = cpspec;

	spin_lock_init(&dd->cspec->sdepb_lock);
	spin_lock_init(&dd->cspec->rcvmod_lock);
	spin_lock_init(&dd->cspec->gpio_lock);

	/* we haven't yet set QIB_PRESENT, so use read directly */
	dd->revision = readq(&dd->kregbase[kr_revision]);

	if ((dd->revision & 0xffffffffU) == 0xffffffffU) {
		qib_dev_err(dd,
			"Revision register read failure, giving up initialization\n");
		ret = -ENODEV;
		goto bail;
	}
	dd->flags |= QIB_PRESENT;  /* now register routines work */

	dd->majrev = (u8) SYM_FIELD(dd->revision, Revision_R,
				    ChipRevMajor);
	dd->minrev = (u8) SYM_FIELD(dd->revision, Revision_R,
				    ChipRevMinor);

	get_7220_chip_params(dd);
	qib_7220_boardname(dd);

	/*
	 * GPIO bits for TWSI data and clock,
	 * used for serial EEPROM.
	 */
	dd->gpio_sda_num = _QIB_GPIO_SDA_NUM;
	dd->gpio_scl_num = _QIB_GPIO_SCL_NUM;
	dd->twsi_eeprom_dev = QIB_TWSI_EEPROM_DEV;

	dd->flags |= QIB_HAS_INTX | QIB_HAS_LINK_LATENCY |
		QIB_NODMA_RTAIL | QIB_HAS_THRESH_UPDATE;
	dd->flags |= qib_special_trigger ?
		QIB_USE_SPCL_TRIG : QIB_HAS_SEND_DMA;

	/*
	 * EEPROM error log 0 is TXE Parity errors. 1 is RXE Parity.
	 * 2 is Some Misc, 3 is reserved for future.
	 */
	dd->eep_st_masks[0].hwerrs_to_log = HWE_MASK(TXEMemParityErr);

	dd->eep_st_masks[1].hwerrs_to_log = HWE_MASK(RXEMemParityErr);

	dd->eep_st_masks[2].errs_to_log = ERR_MASK(ResetNegated);

	init_waitqueue_head(&cpspec->autoneg_wait);
	INIT_DELAYED_WORK(&cpspec->autoneg_work, autoneg_7220_work);

	ret = qib_init_pportdata(ppd, dd, 0, 1);
	if (ret)
		goto bail;
	ppd->link_width_supported = IB_WIDTH_1X | IB_WIDTH_4X;
	ppd->link_speed_supported = QIB_IB_SDR | QIB_IB_DDR;

	ppd->link_width_enabled = ppd->link_width_supported;
	ppd->link_speed_enabled = ppd->link_speed_supported;
	/*
	 * Set the initial values to reasonable default, will be set
	 * for real when link is up.
	 */
	ppd->link_width_active = IB_WIDTH_4X;
	ppd->link_speed_active = QIB_IB_SDR;
	ppd->delay_mult = rate_to_delay[0][1];
	ppd->vls_supported = IB_VL_VL0;
	ppd->vls_operational = ppd->vls_supported;

	if (!qib_mini_init)
		qib_write_kreg(dd, kr_rcvbthqp, QIB_KD_QP);

	setup_timer(&ppd->cpspec->chase_timer, reenable_7220_chase,
		    (unsigned long)ppd);

	qib_num_cfg_vls = 1; /* if any 7220's, only one VL */

	dd->rcvhdrentsize = QIB_RCVHDR_ENTSIZE;
	dd->rcvhdrsize = QIB_DFLT_RCVHDRSIZE;
	dd->rhf_offset =
		dd->rcvhdrentsize - sizeof(u64) / sizeof(u32);

	/* we always allocate at least 2048 bytes for eager buffers */
	ret = ib_mtu_enum_to_int(qib_ibmtu);
	dd->rcvegrbufsize = ret != -1 ? max(ret, 2048) : QIB_DEFAULT_MTU;
	BUG_ON(!is_power_of_2(dd->rcvegrbufsize));
	dd->rcvegrbufsize_shift = ilog2(dd->rcvegrbufsize);

	qib_7220_tidtemplate(dd);

	/*
	 * We can request a receive interrupt for 1 or
	 * more packets from current offset.  For now, we set this
	 * up for a single packet.
	 */
	dd->rhdrhead_intr_off = 1ULL << 32;

	/* setup the stats timer; the add_timer is done at end of init */
	init_timer(&dd->stats_timer);
	dd->stats_timer.function = qib_get_7220_faststats;
	dd->stats_timer.data = (unsigned long) dd;
	dd->stats_timer.expires = jiffies + ACTIVITY_TIMER * HZ;

	/*
	 * Control[4] has been added to change the arbitration within
	 * the SDMA engine between favoring data fetches over descriptor
	 * fetches.  qib_sdma_fetch_arb==0 gives data fetches priority.
	 */
	if (qib_sdma_fetch_arb)
		dd->control |= 1 << 4;

	dd->ureg_align = 0x10000;  /* 64KB alignment */

	dd->piosize2kmax_dwords = (dd->piosize2k >> 2)-1;
	qib_7220_config_ctxts(dd);
	qib_set_ctxtcnt(dd);  /* needed for PAT setup */

	ret = init_chip_wc_pat(dd, 0);
	if (ret)
		goto bail;
	set_7220_baseaddrs(dd); /* set chip access pointers now */

	ret = 0;
	if (qib_mini_init)
		goto bail;

	ret = qib_create_ctxts(dd);
	init_7220_cntrnames(dd);

	/* use all of 4KB buffers for the kernel SDMA, zero if !SDMA.
	 * reserve the update threshold amount for other kernel use, such
	 * as sending SMI, MAD, and ACKs, or 3, whichever is greater,
	 * unless we aren't enabling SDMA, in which case we want to use
	 * all the 4k bufs for the kernel.
	 * if this was less than the update threshold, we could wait
	 * a long time for an update.  Coded this way because we
	 * sometimes change the update threshold for various reasons,
	 * and we want this to remain robust.
	 */
	updthresh = 8U; /* update threshold */
	if (dd->flags & QIB_HAS_SEND_DMA) {
		dd->cspec->sdmabufcnt =  dd->piobcnt4k;
		sbufs = updthresh > 3 ? updthresh : 3;
	} else {
		dd->cspec->sdmabufcnt = 0;
		sbufs = dd->piobcnt4k;
	}

	dd->cspec->lastbuf_for_pio = dd->piobcnt2k + dd->piobcnt4k -
		dd->cspec->sdmabufcnt;
	dd->lastctxt_piobuf = dd->cspec->lastbuf_for_pio - sbufs;
	dd->cspec->lastbuf_for_pio--; /* range is <= , not < */
	dd->last_pio = dd->cspec->lastbuf_for_pio;
	dd->pbufsctxt = dd->lastctxt_piobuf /
		(dd->cfgctxts - dd->first_user_ctxt);

	/*
	 * if we are at 16 user contexts, we will have one 7 sbufs
	 * per context, so drop the update threshold to match.  We
	 * want to update before we actually run out, at low pbufs/ctxt
	 * so give ourselves some margin
	 */
	if ((dd->pbufsctxt - 2) < updthresh)
		updthresh = dd->pbufsctxt - 2;

	dd->cspec->updthresh_dflt = updthresh;
	dd->cspec->updthresh = updthresh;

	/* before full enable, no interrupts, no locking needed */
	dd->sendctrl |= (updthresh & SYM_RMASK(SendCtrl, AvailUpdThld))
			     << SYM_LSB(SendCtrl, AvailUpdThld);

	dd->psxmitwait_supported = 1;
	dd->psxmitwait_check_rate = QIB_7220_PSXMITWAIT_CHECK_RATE;
bail:
	return ret;
}

static u32 __iomem *qib_7220_getsendbuf(struct qib_pportdata *ppd, u64 pbc,
					u32 *pbufnum)
{
	u32 first, last, plen = pbc & QIB_PBC_LENGTH_MASK;
	struct qib_devdata *dd = ppd->dd;
	u32 __iomem *buf;

	if (((pbc >> 32) & PBC_7220_VL15_SEND_CTRL) &&
		!(ppd->lflags & (QIBL_IB_AUTONEG_INPROG | QIBL_LINKACTIVE)))
		buf = get_7220_link_buf(ppd, pbufnum);
	else {
		if ((plen + 1) > dd->piosize2kmax_dwords)
			first = dd->piobcnt2k;
		else
			first = 0;
		/* try 4k if all 2k busy, so same last for both sizes */
		last = dd->cspec->lastbuf_for_pio;
		buf = qib_getsendbuf_range(dd, pbufnum, first, last);
	}
	return buf;
}

/* these 2 "counters" are really control registers, and are always RW */
static void qib_set_cntr_7220_sample(struct qib_pportdata *ppd, u32 intv,
				     u32 start)
{
	write_7220_creg(ppd->dd, cr_psinterval, intv);
	write_7220_creg(ppd->dd, cr_psstart, start);
}

/*
 * NOTE: no real attempt is made to generalize the SDMA stuff.
 * At some point "soon" we will have a new more generalized
 * set of sdma interface, and then we'll clean this up.
 */

/* Must be called with sdma_lock held, or before init finished */
static void qib_sdma_update_7220_tail(struct qib_pportdata *ppd, u16 tail)
{
	/* Commit writes to memory and advance the tail on the chip */
	wmb();
	ppd->sdma_descq_tail = tail;
	qib_write_kreg(ppd->dd, kr_senddmatail, tail);
}

static void qib_sdma_set_7220_desc_cnt(struct qib_pportdata *ppd, unsigned cnt)
{
}

static struct sdma_set_state_action sdma_7220_action_table[] = {
	[qib_sdma_state_s00_hw_down] = {
		.op_enable = 0,
		.op_intenable = 0,
		.op_halt = 0,
		.go_s99_running_tofalse = 1,
	},
	[qib_sdma_state_s10_hw_start_up_wait] = {
		.op_enable = 1,
		.op_intenable = 1,
		.op_halt = 1,
	},
	[qib_sdma_state_s20_idle] = {
		.op_enable = 1,
		.op_intenable = 1,
		.op_halt = 1,
	},
	[qib_sdma_state_s30_sw_clean_up_wait] = {
		.op_enable = 0,
		.op_intenable = 1,
		.op_halt = 0,
	},
	[qib_sdma_state_s40_hw_clean_up_wait] = {
		.op_enable = 1,
		.op_intenable = 1,
		.op_halt = 1,
	},
	[qib_sdma_state_s50_hw_halt_wait] = {
		.op_enable = 1,
		.op_intenable = 1,
		.op_halt = 1,
	},
	[qib_sdma_state_s99_running] = {
		.op_enable = 1,
		.op_intenable = 1,
		.op_halt = 0,
		.go_s99_running_totrue = 1,
	},
};

static void qib_7220_sdma_init_early(struct qib_pportdata *ppd)
{
	ppd->sdma_state.set_state_action = sdma_7220_action_table;
}

static int init_sdma_7220_regs(struct qib_pportdata *ppd)
{
	struct qib_devdata *dd = ppd->dd;
	unsigned i, n;
	u64 senddmabufmask[3] = { 0 };

	/* Set SendDmaBase */
	qib_write_kreg(dd, kr_senddmabase, ppd->sdma_descq_phys);
	qib_sdma_7220_setlengen(ppd);
	qib_sdma_update_7220_tail(ppd, 0); /* Set SendDmaTail */
	/* Set SendDmaHeadAddr */
	qib_write_kreg(dd, kr_senddmaheadaddr, ppd->sdma_head_phys);

	/*
	 * Reserve all the former "kernel" piobufs, using high number range
	 * so we get as many 4K buffers as possible
	 */
	n = dd->piobcnt2k + dd->piobcnt4k;
	i = n - dd->cspec->sdmabufcnt;

	for (; i < n; ++i) {
		unsigned word = i / 64;
		unsigned bit = i & 63;

		BUG_ON(word >= 3);
		senddmabufmask[word] |= 1ULL << bit;
	}
	qib_write_kreg(dd, kr_senddmabufmask0, senddmabufmask[0]);
	qib_write_kreg(dd, kr_senddmabufmask1, senddmabufmask[1]);
	qib_write_kreg(dd, kr_senddmabufmask2, senddmabufmask[2]);

	ppd->sdma_state.first_sendbuf = i;
	ppd->sdma_state.last_sendbuf = n;

	return 0;
}

/* sdma_lock must be held */
static u16 qib_sdma_7220_gethead(struct qib_pportdata *ppd)
{
	struct qib_devdata *dd = ppd->dd;
	int sane;
	int use_dmahead;
	u16 swhead;
	u16 swtail;
	u16 cnt;
	u16 hwhead;

	use_dmahead = __qib_sdma_running(ppd) &&
		(dd->flags & QIB_HAS_SDMA_TIMEOUT);
retry:
	hwhead = use_dmahead ?
		(u16)le64_to_cpu(*ppd->sdma_head_dma) :
		(u16)qib_read_kreg32(dd, kr_senddmahead);

	swhead = ppd->sdma_descq_head;
	swtail = ppd->sdma_descq_tail;
	cnt = ppd->sdma_descq_cnt;

	if (swhead < swtail) {
		/* not wrapped */
		sane = (hwhead >= swhead) & (hwhead <= swtail);
	} else if (swhead > swtail) {
		/* wrapped around */
		sane = ((hwhead >= swhead) && (hwhead < cnt)) ||
			(hwhead <= swtail);
	} else {
		/* empty */
		sane = (hwhead == swhead);
	}

	if (unlikely(!sane)) {
		if (use_dmahead) {
			/* try one more time, directly from the register */
			use_dmahead = 0;
			goto retry;
		}
		/* assume no progress */
		hwhead = swhead;
	}

	return hwhead;
}

static int qib_sdma_7220_busy(struct qib_pportdata *ppd)
{
	u64 hwstatus = qib_read_kreg64(ppd->dd, kr_senddmastatus);

	return (hwstatus & SYM_MASK(SendDmaStatus, ScoreBoardDrainInProg)) ||
	       (hwstatus & SYM_MASK(SendDmaStatus, AbortInProg)) ||
	       (hwstatus & SYM_MASK(SendDmaStatus, InternalSDmaEnable)) ||
	       !(hwstatus & SYM_MASK(SendDmaStatus, ScbEmpty));
}

/*
 * Compute the amount of delay before sending the next packet if the
 * port's send rate differs from the static rate set for the QP.
 * Since the delay affects this packet but the amount of the delay is
 * based on the length of the previous packet, use the last delay computed
 * and save the delay count for this packet to be used next time
 * we get here.
 */
static u32 qib_7220_setpbc_control(struct qib_pportdata *ppd, u32 plen,
				   u8 srate, u8 vl)
{
	u8 snd_mult = ppd->delay_mult;
	u8 rcv_mult = ib_rate_to_delay[srate];
	u32 ret = ppd->cpspec->last_delay_mult;

	ppd->cpspec->last_delay_mult = (rcv_mult > snd_mult) ?
		(plen * (rcv_mult - snd_mult) + 1) >> 1 : 0;

	/* Indicate VL15, if necessary */
	if (vl == 15)
		ret |= PBC_7220_VL15_SEND_CTRL;
	return ret;
}

static void qib_7220_initvl15_bufs(struct qib_devdata *dd)
{
}

static void qib_7220_init_ctxt(struct qib_ctxtdata *rcd)
{
	if (!rcd->ctxt) {
		rcd->rcvegrcnt = IBA7220_KRCVEGRCNT;
		rcd->rcvegr_tid_base = 0;
	} else {
		rcd->rcvegrcnt = rcd->dd->cspec->rcvegrcnt;
		rcd->rcvegr_tid_base = IBA7220_KRCVEGRCNT +
			(rcd->ctxt - 1) * rcd->rcvegrcnt;
	}
}

static void qib_7220_txchk_change(struct qib_devdata *dd, u32 start,
				  u32 len, u32 which, struct qib_ctxtdata *rcd)
{
	int i;
	unsigned long flags;

	switch (which) {
	case TXCHK_CHG_TYPE_KERN:
		/* see if we need to raise avail update threshold */
		spin_lock_irqsave(&dd->uctxt_lock, flags);
		for (i = dd->first_user_ctxt;
		     dd->cspec->updthresh != dd->cspec->updthresh_dflt
		     && i < dd->cfgctxts; i++)
			if (dd->rcd[i] && dd->rcd[i]->subctxt_cnt &&
			   ((dd->rcd[i]->piocnt / dd->rcd[i]->subctxt_cnt) - 1)
			   < dd->cspec->updthresh_dflt)
				break;
		spin_unlock_irqrestore(&dd->uctxt_lock, flags);
		if (i == dd->cfgctxts) {
			spin_lock_irqsave(&dd->sendctrl_lock, flags);
			dd->cspec->updthresh = dd->cspec->updthresh_dflt;
			dd->sendctrl &= ~SYM_MASK(SendCtrl, AvailUpdThld);
			dd->sendctrl |= (dd->cspec->updthresh &
					 SYM_RMASK(SendCtrl, AvailUpdThld)) <<
					   SYM_LSB(SendCtrl, AvailUpdThld);
			spin_unlock_irqrestore(&dd->sendctrl_lock, flags);
			sendctrl_7220_mod(dd->pport, QIB_SENDCTRL_AVAIL_BLIP);
		}
		break;
	case TXCHK_CHG_TYPE_USER:
		spin_lock_irqsave(&dd->sendctrl_lock, flags);
		if (rcd && rcd->subctxt_cnt && ((rcd->piocnt
			/ rcd->subctxt_cnt) - 1) < dd->cspec->updthresh) {
			dd->cspec->updthresh = (rcd->piocnt /
						rcd->subctxt_cnt) - 1;
			dd->sendctrl &= ~SYM_MASK(SendCtrl, AvailUpdThld);
			dd->sendctrl |= (dd->cspec->updthresh &
					SYM_RMASK(SendCtrl, AvailUpdThld))
					<< SYM_LSB(SendCtrl, AvailUpdThld);
			spin_unlock_irqrestore(&dd->sendctrl_lock, flags);
			sendctrl_7220_mod(dd->pport, QIB_SENDCTRL_AVAIL_BLIP);
		} else
			spin_unlock_irqrestore(&dd->sendctrl_lock, flags);
		break;
	}
}

static void writescratch(struct qib_devdata *dd, u32 val)
{
	qib_write_kreg(dd, kr_scratch, val);
}

#define VALID_TS_RD_REG_MASK 0xBF
/**
 * qib_7220_tempsense_read - read register of temp sensor via TWSI
 * @dd: the qlogic_ib device
 * @regnum: register to read from
 *
 * returns reg contents (0..255) or < 0 for error
 */
static int qib_7220_tempsense_rd(struct qib_devdata *dd, int regnum)
{
	int ret;
	u8 rdata;

	if (regnum > 7) {
		ret = -EINVAL;
		goto bail;
	}

	/* return a bogus value for (the one) register we do not have */
	if (!((1 << regnum) & VALID_TS_RD_REG_MASK)) {
		ret = 0;
		goto bail;
	}

	ret = mutex_lock_interruptible(&dd->eep_lock);
	if (ret)
		goto bail;

	ret = qib_twsi_blk_rd(dd, QIB_TWSI_TEMP_DEV, regnum, &rdata, 1);
	if (!ret)
		ret = rdata;

	mutex_unlock(&dd->eep_lock);

	/*
	 * There are three possibilities here:
	 * ret is actual value (0..255)
	 * ret is -ENXIO or -EINVAL from twsi code or this file
	 * ret is -EINTR from mutex_lock_interruptible.
	 */
bail:
	return ret;
}

#ifdef CONFIG_INFINIBAND_QIB_DCA
static int qib_7220_notify_dca(struct qib_devdata *dd, unsigned long event)
{
	return 0;
}
#endif

/* Dummy function, as 7220 boards never disable EEPROM Write */
static int qib_7220_eeprom_wen(struct qib_devdata *dd, int wen)
{
	return 1;
}

/**
 * qib_init_iba7220_funcs - set up the chip-specific function pointers
 * @dev: the pci_dev for qlogic_ib device
 * @ent: pci_device_id struct for this dev
 *
 * This is global, and is called directly at init to set up the
 * chip-specific function pointers for later use.
 */
struct qib_devdata *qib_init_iba7220_funcs(struct pci_dev *pdev,
					   const struct pci_device_id *ent)
{
	struct qib_devdata *dd;
	int ret;
	u32 boardid, minwidth;

	dd = qib_alloc_devdata(pdev, sizeof(struct qib_chip_specific) +
		sizeof(struct qib_chippport_specific));
	if (IS_ERR(dd))
		goto bail;

	dd->f_bringup_serdes    = qib_7220_bringup_serdes;
	dd->f_cleanup           = qib_setup_7220_cleanup;
	dd->f_clear_tids        = qib_7220_clear_tids;
	dd->f_free_irq          = qib_7220_free_irq;
	dd->f_get_base_info     = qib_7220_get_base_info;
	dd->f_get_msgheader     = qib_7220_get_msgheader;
	dd->f_getsendbuf        = qib_7220_getsendbuf;
	dd->f_gpio_mod          = gpio_7220_mod;
	dd->f_eeprom_wen        = qib_7220_eeprom_wen;
	dd->f_hdrqempty         = qib_7220_hdrqempty;
	dd->f_ib_updown         = qib_7220_ib_updown;
	dd->f_init_ctxt         = qib_7220_init_ctxt;
	dd->f_initvl15_bufs     = qib_7220_initvl15_bufs;
	dd->f_intr_fallback     = qib_7220_intr_fallback;
	dd->f_late_initreg      = qib_late_7220_initreg;
	dd->f_setpbc_control    = qib_7220_setpbc_control;
	dd->f_portcntr          = qib_portcntr_7220;
	dd->f_put_tid           = qib_7220_put_tid;
	dd->f_quiet_serdes      = qib_7220_quiet_serdes;
	dd->f_rcvctrl           = rcvctrl_7220_mod;
	dd->f_read_cntrs        = qib_read_7220cntrs;
	dd->f_read_portcntrs    = qib_read_7220portcntrs;
	dd->f_reset             = qib_setup_7220_reset;
	dd->f_init_sdma_regs    = init_sdma_7220_regs;
	dd->f_sdma_busy         = qib_sdma_7220_busy;
	dd->f_sdma_gethead      = qib_sdma_7220_gethead;
	dd->f_sdma_sendctrl     = qib_7220_sdma_sendctrl;
	dd->f_sdma_set_desc_cnt = qib_sdma_set_7220_desc_cnt;
	dd->f_sdma_update_tail  = qib_sdma_update_7220_tail;
	dd->f_sdma_hw_clean_up  = qib_7220_sdma_hw_clean_up;
	dd->f_sdma_hw_start_up  = qib_7220_sdma_hw_start_up;
	dd->f_sdma_init_early   = qib_7220_sdma_init_early;
	dd->f_sendctrl          = sendctrl_7220_mod;
	dd->f_set_armlaunch     = qib_set_7220_armlaunch;
	dd->f_set_cntr_sample   = qib_set_cntr_7220_sample;
	dd->f_iblink_state      = qib_7220_iblink_state;
	dd->f_ibphys_portstate  = qib_7220_phys_portstate;
	dd->f_get_ib_cfg        = qib_7220_get_ib_cfg;
	dd->f_set_ib_cfg        = qib_7220_set_ib_cfg;
	dd->f_set_ib_loopback   = qib_7220_set_loopback;
	dd->f_set_intr_state    = qib_7220_set_intr_state;
	dd->f_setextled         = qib_setup_7220_setextled;
	dd->f_txchk_change      = qib_7220_txchk_change;
	dd->f_update_usrhead    = qib_update_7220_usrhead;
	dd->f_wantpiobuf_intr   = qib_wantpiobuf_7220_intr;
	dd->f_xgxs_reset        = qib_7220_xgxs_reset;
	dd->f_writescratch      = writescratch;
	dd->f_tempsense_rd	= qib_7220_tempsense_rd;
#ifdef CONFIG_INFINIBAND_QIB_DCA
	dd->f_notify_dca = qib_7220_notify_dca;
#endif
	/*
	 * Do remaining pcie setup and save pcie values in dd.
	 * Any error printing is already done by the init code.
	 * On return, we have the chip mapped, but chip registers
	 * are not set up until start of qib_init_7220_variables.
	 */
	ret = qib_pcie_ddinit(dd, pdev, ent);
	if (ret < 0)
		goto bail_free;

	/* initialize chip-specific variables */
	ret = qib_init_7220_variables(dd);
	if (ret)
		goto bail_cleanup;

	if (qib_mini_init)
		goto bail;

	boardid = SYM_FIELD(dd->revision, Revision,
			    BoardID);
	switch (boardid) {
	case 0:
	case 2:
	case 10:
	case 12:
		minwidth = 16; /* x16 capable boards */
		break;
	default:
		minwidth = 8; /* x8 capable boards */
		break;
	}
	if (qib_pcie_params(dd, minwidth, NULL, NULL))
		qib_dev_err(dd,
			"Failed to setup PCIe or interrupts; continuing anyway\n");

	/* save IRQ for possible later use */
	dd->cspec->irq = pdev->irq;

	if (qib_read_kreg64(dd, kr_hwerrstatus) &
	    QLOGIC_IB_HWE_SERDESPLLFAILED)
		qib_write_kreg(dd, kr_hwerrclear,
			       QLOGIC_IB_HWE_SERDESPLLFAILED);

	/* setup interrupt handler (interrupt type handled above) */
	qib_setup_7220_interrupt(dd);
	qib_7220_init_hwerrors(dd);

	/* clear diagctrl register, in case diags were running and crashed */
	qib_write_kreg(dd, kr_hwdiagctrl, 0);

	goto bail;

bail_cleanup:
	qib_pcie_ddcleanup(dd);
bail_free:
	qib_free_devdata(dd);
	dd = ERR_PTR(ret);
bail:
	return dd;
}
