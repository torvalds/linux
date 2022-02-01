/*
 * Copyright (c) 2012 - 2017 Intel Corporation.  All rights reserved.
 * Copyright (c) 2008 - 2012 QLogic Corporation. All rights reserved.
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
 * InfiniPath 7322 chip
 */

#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_smi.h>
#ifdef CONFIG_INFINIBAND_QIB_DCA
#include <linux/dca.h>
#endif

#include "qib.h"
#include "qib_7322_regs.h"
#include "qib_qsfp.h"

#include "qib_mad.h"
#include "qib_verbs.h"

#undef pr_fmt
#define pr_fmt(fmt) QIB_DRV_NAME " " fmt

static void qib_setup_7322_setextled(struct qib_pportdata *, u32);
static void qib_7322_handle_hwerrors(struct qib_devdata *, char *, size_t);
static void sendctrl_7322_mod(struct qib_pportdata *ppd, u32 op);
static irqreturn_t qib_7322intr(int irq, void *data);
static irqreturn_t qib_7322bufavail(int irq, void *data);
static irqreturn_t sdma_intr(int irq, void *data);
static irqreturn_t sdma_idle_intr(int irq, void *data);
static irqreturn_t sdma_progress_intr(int irq, void *data);
static irqreturn_t sdma_cleanup_intr(int irq, void *data);
static void qib_7322_txchk_change(struct qib_devdata *, u32, u32, u32,
				  struct qib_ctxtdata *rcd);
static u8 qib_7322_phys_portstate(u64);
static u32 qib_7322_iblink_state(u64);
static void qib_set_ib_7322_lstate(struct qib_pportdata *ppd, u16 linkcmd,
				   u16 linitcmd);
static void force_h1(struct qib_pportdata *);
static void adj_tx_serdes(struct qib_pportdata *);
static u32 qib_7322_setpbc_control(struct qib_pportdata *, u32, u8, u8);
static void qib_7322_mini_pcs_reset(struct qib_pportdata *);

static u32 ahb_mod(struct qib_devdata *, int, int, int, u32, u32);
static void ibsd_wr_allchans(struct qib_pportdata *, int, unsigned, unsigned);
static void serdes_7322_los_enable(struct qib_pportdata *, int);
static int serdes_7322_init_old(struct qib_pportdata *);
static int serdes_7322_init_new(struct qib_pportdata *);
static void dump_sdma_7322_state(struct qib_pportdata *);

#define BMASK(msb, lsb) (((1 << ((msb) + 1 - (lsb))) - 1) << (lsb))

/* LE2 serdes values for different cases */
#define LE2_DEFAULT 5
#define LE2_5m 4
#define LE2_QME 0

/* Below is special-purpose, so only really works for the IB SerDes blocks. */
#define IBSD(hw_pidx) (hw_pidx + 2)

/* these are variables for documentation and experimentation purposes */
static const unsigned rcv_int_timeout = 375;
static const unsigned rcv_int_count = 16;
static const unsigned sdma_idle_cnt = 64;

/* Time to stop altering Rx Equalization parameters, after link up. */
#define RXEQ_DISABLE_MSECS 2500

/*
 * Number of VLs we are configured to use (to allow for more
 * credits per vl, etc.)
 */
ushort qib_num_cfg_vls = 2;
module_param_named(num_vls, qib_num_cfg_vls, ushort, S_IRUGO);
MODULE_PARM_DESC(num_vls, "Set number of Virtual Lanes to use (1-8)");

static ushort qib_chase = 1;
module_param_named(chase, qib_chase, ushort, S_IRUGO);
MODULE_PARM_DESC(chase, "Enable state chase handling");

static ushort qib_long_atten = 10; /* 10 dB ~= 5m length */
module_param_named(long_attenuation, qib_long_atten, ushort, S_IRUGO);
MODULE_PARM_DESC(long_attenuation,
		 "attenuation cutoff (dB) for long copper cable setup");

static ushort qib_singleport;
module_param_named(singleport, qib_singleport, ushort, S_IRUGO);
MODULE_PARM_DESC(singleport, "Use only IB port 1; more per-port buffer space");

static ushort qib_krcvq01_no_msi;
module_param_named(krcvq01_no_msi, qib_krcvq01_no_msi, ushort, S_IRUGO);
MODULE_PARM_DESC(krcvq01_no_msi, "No MSI for kctx < 2");

/*
 * Receive header queue sizes
 */
static unsigned qib_rcvhdrcnt;
module_param_named(rcvhdrcnt, qib_rcvhdrcnt, uint, S_IRUGO);
MODULE_PARM_DESC(rcvhdrcnt, "receive header count");

static unsigned qib_rcvhdrsize;
module_param_named(rcvhdrsize, qib_rcvhdrsize, uint, S_IRUGO);
MODULE_PARM_DESC(rcvhdrsize, "receive header size in 32-bit words");

static unsigned qib_rcvhdrentsize;
module_param_named(rcvhdrentsize, qib_rcvhdrentsize, uint, S_IRUGO);
MODULE_PARM_DESC(rcvhdrentsize, "receive header entry size in 32-bit words");

#define MAX_ATTEN_LEN 64 /* plenty for any real system */
/* for read back, default index is ~5m copper cable */
static char txselect_list[MAX_ATTEN_LEN] = "10";
static struct kparam_string kp_txselect = {
	.string = txselect_list,
	.maxlen = MAX_ATTEN_LEN
};
static int  setup_txselect(const char *, const struct kernel_param *);
module_param_call(txselect, setup_txselect, param_get_string,
		  &kp_txselect, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(txselect,
		 "Tx serdes indices (for no QSFP or invalid QSFP data)");

#define BOARD_QME7342 5
#define BOARD_QMH7342 6
#define BOARD_QMH7360 9
#define IS_QMH(dd) (SYM_FIELD((dd)->revision, Revision, BoardID) == \
		    BOARD_QMH7342)
#define IS_QME(dd) (SYM_FIELD((dd)->revision, Revision, BoardID) == \
		    BOARD_QME7342)

#define KREG_IDX(regname)     (QIB_7322_##regname##_OFFS / sizeof(u64))

#define KREG_IBPORT_IDX(regname) ((QIB_7322_##regname##_0_OFFS / sizeof(u64)))

#define MASK_ACROSS(lsb, msb) \
	(((1ULL << ((msb) + 1 - (lsb))) - 1) << (lsb))

#define SYM_RMASK(regname, fldname) ((u64)              \
	QIB_7322_##regname##_##fldname##_RMASK)

#define SYM_MASK(regname, fldname) ((u64)               \
	QIB_7322_##regname##_##fldname##_RMASK <<       \
	 QIB_7322_##regname##_##fldname##_LSB)

#define SYM_FIELD(value, regname, fldname) ((u64)	\
	(((value) >> SYM_LSB(regname, fldname)) &	\
	 SYM_RMASK(regname, fldname)))

/* useful for things like LaFifoEmpty_0...7, TxCreditOK_0...7, etc. */
#define SYM_FIELD_ACROSS(value, regname, fldname, nbits) \
	(((value) >> SYM_LSB(regname, fldname)) & MASK_ACROSS(0, nbits))

#define HWE_MASK(fldname) SYM_MASK(HwErrMask, fldname##Mask)
#define ERR_MASK(fldname) SYM_MASK(ErrMask, fldname##Mask)
#define ERR_MASK_N(fldname) SYM_MASK(ErrMask_0, fldname##Mask)
#define INT_MASK(fldname) SYM_MASK(IntMask, fldname##IntMask)
#define INT_MASK_P(fldname, port) SYM_MASK(IntMask, fldname##IntMask##_##port)
/* Below because most, but not all, fields of IntMask have that full suffix */
#define INT_MASK_PM(fldname, port) SYM_MASK(IntMask, fldname##Mask##_##port)


#define SYM_LSB(regname, fldname) (QIB_7322_##regname##_##fldname##_LSB)

/*
 * the size bits give us 2^N, in KB units.  0 marks as invalid,
 * and 7 is reserved.  We currently use only 2KB and 4KB
 */
#define IBA7322_TID_SZ_SHIFT QIB_7322_RcvTIDArray0_RT_BufSize_LSB
#define IBA7322_TID_SZ_2K (1UL<<IBA7322_TID_SZ_SHIFT) /* 2KB */
#define IBA7322_TID_SZ_4K (2UL<<IBA7322_TID_SZ_SHIFT) /* 4KB */
#define IBA7322_TID_PA_SHIFT 11U /* TID addr in chip stored w/o low bits */

#define SendIBSLIDAssignMask \
	QIB_7322_SendIBSLIDAssign_0_SendIBSLIDAssign_15_0_RMASK
#define SendIBSLMCMask \
	QIB_7322_SendIBSLIDMask_0_SendIBSLIDMask_15_0_RMASK

#define ExtLED_IB1_YEL SYM_MASK(EXTCtrl, LEDPort0YellowOn)
#define ExtLED_IB1_GRN SYM_MASK(EXTCtrl, LEDPort0GreenOn)
#define ExtLED_IB2_YEL SYM_MASK(EXTCtrl, LEDPort1YellowOn)
#define ExtLED_IB2_GRN SYM_MASK(EXTCtrl, LEDPort1GreenOn)
#define ExtLED_IB1_MASK (ExtLED_IB1_YEL | ExtLED_IB1_GRN)
#define ExtLED_IB2_MASK (ExtLED_IB2_YEL | ExtLED_IB2_GRN)

#define _QIB_GPIO_SDA_NUM 1
#define _QIB_GPIO_SCL_NUM 0
#define QIB_EEPROM_WEN_NUM 14
#define QIB_TWSI_EEPROM_DEV 0xA2 /* All Production 7322 cards. */

/* HW counter clock is at 4nsec */
#define QIB_7322_PSXMITWAIT_CHECK_RATE 4000

/* full speed IB port 1 only */
#define PORT_SPD_CAP (QIB_IB_SDR | QIB_IB_DDR | QIB_IB_QDR)
#define PORT_SPD_CAP_SHIFT 3

/* full speed featuremask, both ports */
#define DUAL_PORT_CAP (PORT_SPD_CAP | (PORT_SPD_CAP << PORT_SPD_CAP_SHIFT))

/*
 * This file contains almost all the chip-specific register information and
 * access functions for the FAKED QLogic InfiniPath 7322 PCI-Express chip.
 */

/* Use defines to tie machine-generated names to lower-case names */
#define kr_contextcnt KREG_IDX(ContextCnt)
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
#define kr_hwdiagctrl KREG_IDX(HwDiagCtrl)
#define kr_debugportval KREG_IDX(DebugPortValueReg)
#define kr_fmask KREG_IDX(feature_mask)
#define kr_act_fmask KREG_IDX(active_feature_mask)
#define kr_hwerrclear KREG_IDX(HwErrClear)
#define kr_hwerrmask KREG_IDX(HwErrMask)
#define kr_hwerrstatus KREG_IDX(HwErrStatus)
#define kr_intclear KREG_IDX(IntClear)
#define kr_intmask KREG_IDX(IntMask)
#define kr_intredirect KREG_IDX(IntRedirect0)
#define kr_intstatus KREG_IDX(IntStatus)
#define kr_pagealign KREG_IDX(PageAlign)
#define kr_rcvavailtimeout KREG_IDX(RcvAvailTimeOut0)
#define kr_rcvctrl KREG_IDX(RcvCtrl) /* Common, but chip also has per-port */
#define kr_rcvegrbase KREG_IDX(RcvEgrBase)
#define kr_rcvegrcnt KREG_IDX(RcvEgrCnt)
#define kr_rcvhdrcnt KREG_IDX(RcvHdrCnt)
#define kr_rcvhdrentsize KREG_IDX(RcvHdrEntSize)
#define kr_rcvhdrsize KREG_IDX(RcvHdrSize)
#define kr_rcvtidbase KREG_IDX(RcvTIDBase)
#define kr_rcvtidcnt KREG_IDX(RcvTIDCnt)
#define kr_revision KREG_IDX(Revision)
#define kr_scratch KREG_IDX(Scratch)
#define kr_sendbuffererror KREG_IDX(SendBufErr0) /* and base for 1 and 2 */
#define kr_sendcheckmask KREG_IDX(SendCheckMask0) /* and 1, 2 */
#define kr_sendctrl KREG_IDX(SendCtrl)
#define kr_sendgrhcheckmask KREG_IDX(SendGRHCheckMask0) /* and 1, 2 */
#define kr_sendibpktmask KREG_IDX(SendIBPacketMask0) /* and 1, 2 */
#define kr_sendpioavailaddr KREG_IDX(SendBufAvailAddr)
#define kr_sendpiobufbase KREG_IDX(SendBufBase)
#define kr_sendpiobufcnt KREG_IDX(SendBufCnt)
#define kr_sendpiosize KREG_IDX(SendBufSize)
#define kr_sendregbase KREG_IDX(SendRegBase)
#define kr_sendbufavail0 KREG_IDX(SendBufAvail0)
#define kr_userregbase KREG_IDX(UserRegBase)
#define kr_intgranted KREG_IDX(Int_Granted)
#define kr_vecclr_wo_int KREG_IDX(vec_clr_without_int)
#define kr_intblocked KREG_IDX(IntBlocked)
#define kr_r_access KREG_IDX(SPC_JTAG_ACCESS_REG)

/*
 * per-port kernel registers.  Access only with qib_read_kreg_port()
 * or qib_write_kreg_port()
 */
#define krp_errclear KREG_IBPORT_IDX(ErrClear)
#define krp_errmask KREG_IBPORT_IDX(ErrMask)
#define krp_errstatus KREG_IBPORT_IDX(ErrStatus)
#define krp_highprio_0 KREG_IBPORT_IDX(HighPriority0)
#define krp_highprio_limit KREG_IBPORT_IDX(HighPriorityLimit)
#define krp_hrtbt_guid KREG_IBPORT_IDX(HRTBT_GUID)
#define krp_ib_pcsconfig KREG_IBPORT_IDX(IBPCSConfig)
#define krp_ibcctrl_a KREG_IBPORT_IDX(IBCCtrlA)
#define krp_ibcctrl_b KREG_IBPORT_IDX(IBCCtrlB)
#define krp_ibcctrl_c KREG_IBPORT_IDX(IBCCtrlC)
#define krp_ibcstatus_a KREG_IBPORT_IDX(IBCStatusA)
#define krp_ibcstatus_b KREG_IBPORT_IDX(IBCStatusB)
#define krp_txestatus KREG_IBPORT_IDX(TXEStatus)
#define krp_lowprio_0 KREG_IBPORT_IDX(LowPriority0)
#define krp_ncmodectrl KREG_IBPORT_IDX(IBNCModeCtrl)
#define krp_partitionkey KREG_IBPORT_IDX(RcvPartitionKey)
#define krp_psinterval KREG_IBPORT_IDX(PSInterval)
#define krp_psstart KREG_IBPORT_IDX(PSStart)
#define krp_psstat KREG_IBPORT_IDX(PSStat)
#define krp_rcvbthqp KREG_IBPORT_IDX(RcvBTHQP)
#define krp_rcvctrl KREG_IBPORT_IDX(RcvCtrl)
#define krp_rcvpktledcnt KREG_IBPORT_IDX(RcvPktLEDCnt)
#define krp_rcvqpmaptable KREG_IBPORT_IDX(RcvQPMapTableA)
#define krp_rxcreditvl0 KREG_IBPORT_IDX(RxCreditVL0)
#define krp_rxcreditvl15 (KREG_IBPORT_IDX(RxCreditVL0)+15)
#define krp_sendcheckcontrol KREG_IBPORT_IDX(SendCheckControl)
#define krp_sendctrl KREG_IBPORT_IDX(SendCtrl)
#define krp_senddmabase KREG_IBPORT_IDX(SendDmaBase)
#define krp_senddmabufmask0 KREG_IBPORT_IDX(SendDmaBufMask0)
#define krp_senddmabufmask1 (KREG_IBPORT_IDX(SendDmaBufMask0) + 1)
#define krp_senddmabufmask2 (KREG_IBPORT_IDX(SendDmaBufMask0) + 2)
#define krp_senddmabuf_use0 KREG_IBPORT_IDX(SendDmaBufUsed0)
#define krp_senddmabuf_use1 (KREG_IBPORT_IDX(SendDmaBufUsed0) + 1)
#define krp_senddmabuf_use2 (KREG_IBPORT_IDX(SendDmaBufUsed0) + 2)
#define krp_senddmadesccnt KREG_IBPORT_IDX(SendDmaDescCnt)
#define krp_senddmahead KREG_IBPORT_IDX(SendDmaHead)
#define krp_senddmaheadaddr KREG_IBPORT_IDX(SendDmaHeadAddr)
#define krp_senddmaidlecnt KREG_IBPORT_IDX(SendDmaIdleCnt)
#define krp_senddmalengen KREG_IBPORT_IDX(SendDmaLenGen)
#define krp_senddmaprioritythld KREG_IBPORT_IDX(SendDmaPriorityThld)
#define krp_senddmareloadcnt KREG_IBPORT_IDX(SendDmaReloadCnt)
#define krp_senddmastatus KREG_IBPORT_IDX(SendDmaStatus)
#define krp_senddmatail KREG_IBPORT_IDX(SendDmaTail)
#define krp_sendhdrsymptom KREG_IBPORT_IDX(SendHdrErrSymptom)
#define krp_sendslid KREG_IBPORT_IDX(SendIBSLIDAssign)
#define krp_sendslidmask KREG_IBPORT_IDX(SendIBSLIDMask)
#define krp_ibsdtestiftx KREG_IBPORT_IDX(IB_SDTEST_IF_TX)
#define krp_adapt_dis_timer KREG_IBPORT_IDX(ADAPT_DISABLE_TIMER_THRESHOLD)
#define krp_tx_deemph_override KREG_IBPORT_IDX(IBSD_TX_DEEMPHASIS_OVERRIDE)
#define krp_serdesctrl KREG_IBPORT_IDX(IBSerdesCtrl)

/*
 * Per-context kernel registers.  Access only with qib_read_kreg_ctxt()
 * or qib_write_kreg_ctxt()
 */
#define krc_rcvhdraddr KREG_IDX(RcvHdrAddr0)
#define krc_rcvhdrtailaddr KREG_IDX(RcvHdrTailAddr0)

/*
 * TID Flow table, per context.  Reduces
 * number of hdrq updates to one per flow (or on errors).
 * context 0 and 1 share same memory, but have distinct
 * addresses.  Since for now, we never use expected sends
 * on kernel contexts, we don't worry about that (we initialize
 * those entries for ctxt 0/1 on driver load twice, for example).
 */
#define NUM_TIDFLOWS_CTXT 0x20 /* 0x20 per context; have to hardcode */
#define ur_rcvflowtable (KREG_IDX(RcvTIDFlowTable0) - KREG_IDX(RcvHdrTail0))

/* these are the error bits in the tid flows, and are W1C */
#define TIDFLOW_ERRBITS  ( \
	(SYM_MASK(RcvTIDFlowTable0, GenMismatch) << \
	SYM_LSB(RcvTIDFlowTable0, GenMismatch)) | \
	(SYM_MASK(RcvTIDFlowTable0, SeqMismatch) << \
	SYM_LSB(RcvTIDFlowTable0, SeqMismatch)))

/* Most (not all) Counters are per-IBport.
 * Requires LBIntCnt is at offset 0 in the group
 */
#define CREG_IDX(regname) \
((QIB_7322_##regname##_0_OFFS - QIB_7322_LBIntCnt_OFFS) / sizeof(u64))

#define crp_badformat CREG_IDX(RxVersionErrCnt)
#define crp_err_rlen CREG_IDX(RxLenErrCnt)
#define crp_erricrc CREG_IDX(RxICRCErrCnt)
#define crp_errlink CREG_IDX(RxLinkMalformCnt)
#define crp_errlpcrc CREG_IDX(RxLPCRCErrCnt)
#define crp_errpkey CREG_IDX(RxPKeyMismatchCnt)
#define crp_errvcrc CREG_IDX(RxVCRCErrCnt)
#define crp_excessbufferovfl CREG_IDX(ExcessBufferOvflCnt)
#define crp_iblinkdown CREG_IDX(IBLinkDownedCnt)
#define crp_iblinkerrrecov CREG_IDX(IBLinkErrRecoveryCnt)
#define crp_ibstatuschange CREG_IDX(IBStatusChangeCnt)
#define crp_ibsymbolerr CREG_IDX(IBSymbolErrCnt)
#define crp_invalidrlen CREG_IDX(RxMaxMinLenErrCnt)
#define crp_locallinkintegrityerr CREG_IDX(LocalLinkIntegrityErrCnt)
#define crp_pktrcv CREG_IDX(RxDataPktCnt)
#define crp_pktrcvflowctrl CREG_IDX(RxFlowPktCnt)
#define crp_pktsend CREG_IDX(TxDataPktCnt)
#define crp_pktsendflow CREG_IDX(TxFlowPktCnt)
#define crp_psrcvdatacount CREG_IDX(PSRcvDataCount)
#define crp_psrcvpktscount CREG_IDX(PSRcvPktsCount)
#define crp_psxmitdatacount CREG_IDX(PSXmitDataCount)
#define crp_psxmitpktscount CREG_IDX(PSXmitPktsCount)
#define crp_psxmitwaitcount CREG_IDX(PSXmitWaitCount)
#define crp_rcvebp CREG_IDX(RxEBPCnt)
#define crp_rcvflowctrlviol CREG_IDX(RxFlowCtrlViolCnt)
#define crp_rcvovfl CREG_IDX(RxBufOvflCnt)
#define crp_rxdlidfltr CREG_IDX(RxDlidFltrCnt)
#define crp_rxdroppkt CREG_IDX(RxDroppedPktCnt)
#define crp_rxotherlocalphyerr CREG_IDX(RxOtherLocalPhyErrCnt)
#define crp_rxqpinvalidctxt CREG_IDX(RxQPInvalidContextCnt)
#define crp_rxvlerr CREG_IDX(RxVlErrCnt)
#define crp_sendstall CREG_IDX(TxFlowStallCnt)
#define crp_txdroppedpkt CREG_IDX(TxDroppedPktCnt)
#define crp_txhdrerr CREG_IDX(TxHeadersErrCnt)
#define crp_txlenerr CREG_IDX(TxLenErrCnt)
#define crp_txminmaxlenerr CREG_IDX(TxMaxMinLenErrCnt)
#define crp_txsdmadesc CREG_IDX(TxSDmaDescCnt)
#define crp_txunderrun CREG_IDX(TxUnderrunCnt)
#define crp_txunsupvl CREG_IDX(TxUnsupVLErrCnt)
#define crp_vl15droppedpkt CREG_IDX(RxVL15DroppedPktCnt)
#define crp_wordrcv CREG_IDX(RxDwordCnt)
#define crp_wordsend CREG_IDX(TxDwordCnt)
#define crp_tx_creditstalls CREG_IDX(TxCreditUpToDateTimeOut)

/* these are the (few) counters that are not port-specific */
#define CREG_DEVIDX(regname) ((QIB_7322_##regname##_OFFS - \
			QIB_7322_LBIntCnt_OFFS) / sizeof(u64))
#define cr_base_egrovfl CREG_DEVIDX(RxP0HdrEgrOvflCnt)
#define cr_lbint CREG_DEVIDX(LBIntCnt)
#define cr_lbstall CREG_DEVIDX(LBFlowStallCnt)
#define cr_pcieretrydiag CREG_DEVIDX(PcieRetryBufDiagQwordCnt)
#define cr_rxtidflowdrop CREG_DEVIDX(RxTidFlowDropCnt)
#define cr_tidfull CREG_DEVIDX(RxTIDFullErrCnt)
#define cr_tidinvalid CREG_DEVIDX(RxTIDValidErrCnt)

/* no chip register for # of IB ports supported, so define */
#define NUM_IB_PORTS 2

/* 1 VL15 buffer per hardware IB port, no register for this, so define */
#define NUM_VL15_BUFS NUM_IB_PORTS

/*
 * context 0 and 1 are special, and there is no chip register that
 * defines this value, so we have to define it here.
 * These are all allocated to either 0 or 1 for single port
 * hardware configuration, otherwise each gets half
 */
#define KCTXT0_EGRCNT 2048

/* values for vl and port fields in PBC, 7322-specific */
#define PBC_PORT_SEL_LSB 26
#define PBC_PORT_SEL_RMASK 1
#define PBC_VL_NUM_LSB 27
#define PBC_VL_NUM_RMASK 7
#define PBC_7322_VL15_SEND (1ULL << 63) /* pbc; VL15, no credit check */
#define PBC_7322_VL15_SEND_CTRL (1ULL << 31) /* control version of same */

static u8 ib_rate_to_delay[IB_RATE_120_GBPS + 1] = {
	[IB_RATE_2_5_GBPS] = 16,
	[IB_RATE_5_GBPS] = 8,
	[IB_RATE_10_GBPS] = 4,
	[IB_RATE_20_GBPS] = 2,
	[IB_RATE_30_GBPS] = 2,
	[IB_RATE_40_GBPS] = 1
};

static const char * const qib_sdma_state_names[] = {
	[qib_sdma_state_s00_hw_down]          = "s00_HwDown",
	[qib_sdma_state_s10_hw_start_up_wait] = "s10_HwStartUpWait",
	[qib_sdma_state_s20_idle]             = "s20_Idle",
	[qib_sdma_state_s30_sw_clean_up_wait] = "s30_SwCleanUpWait",
	[qib_sdma_state_s40_hw_clean_up_wait] = "s40_HwCleanUpWait",
	[qib_sdma_state_s50_hw_halt_wait]     = "s50_HwHaltWait",
	[qib_sdma_state_s99_running]          = "s99_Running",
};

#define IBA7322_LINKSPEED_SHIFT SYM_LSB(IBCStatusA_0, LinkSpeedActive)
#define IBA7322_LINKWIDTH_SHIFT SYM_LSB(IBCStatusA_0, LinkWidthActive)

/* link training states, from IBC */
#define IB_7322_LT_STATE_DISABLED        0x00
#define IB_7322_LT_STATE_LINKUP          0x01
#define IB_7322_LT_STATE_POLLACTIVE      0x02
#define IB_7322_LT_STATE_POLLQUIET       0x03
#define IB_7322_LT_STATE_SLEEPDELAY      0x04
#define IB_7322_LT_STATE_SLEEPQUIET      0x05
#define IB_7322_LT_STATE_CFGDEBOUNCE     0x08
#define IB_7322_LT_STATE_CFGRCVFCFG      0x09
#define IB_7322_LT_STATE_CFGWAITRMT      0x0a
#define IB_7322_LT_STATE_CFGIDLE         0x0b
#define IB_7322_LT_STATE_RECOVERRETRAIN  0x0c
#define IB_7322_LT_STATE_TXREVLANES      0x0d
#define IB_7322_LT_STATE_RECOVERWAITRMT  0x0e
#define IB_7322_LT_STATE_RECOVERIDLE     0x0f
#define IB_7322_LT_STATE_CFGENH          0x10
#define IB_7322_LT_STATE_CFGTEST         0x11
#define IB_7322_LT_STATE_CFGWAITRMTTEST  0x12
#define IB_7322_LT_STATE_CFGWAITENH      0x13

/* link state machine states from IBC */
#define IB_7322_L_STATE_DOWN             0x0
#define IB_7322_L_STATE_INIT             0x1
#define IB_7322_L_STATE_ARM              0x2
#define IB_7322_L_STATE_ACTIVE           0x3
#define IB_7322_L_STATE_ACT_DEFER        0x4

static const u8 qib_7322_physportstate[0x20] = {
	[IB_7322_LT_STATE_DISABLED] = IB_PHYSPORTSTATE_DISABLED,
	[IB_7322_LT_STATE_LINKUP] = IB_PHYSPORTSTATE_LINKUP,
	[IB_7322_LT_STATE_POLLACTIVE] = IB_PHYSPORTSTATE_POLL,
	[IB_7322_LT_STATE_POLLQUIET] = IB_PHYSPORTSTATE_POLL,
	[IB_7322_LT_STATE_SLEEPDELAY] = IB_PHYSPORTSTATE_SLEEP,
	[IB_7322_LT_STATE_SLEEPQUIET] = IB_PHYSPORTSTATE_SLEEP,
	[IB_7322_LT_STATE_CFGDEBOUNCE] = IB_PHYSPORTSTATE_CFG_TRAIN,
	[IB_7322_LT_STATE_CFGRCVFCFG] =
		IB_PHYSPORTSTATE_CFG_TRAIN,
	[IB_7322_LT_STATE_CFGWAITRMT] =
		IB_PHYSPORTSTATE_CFG_TRAIN,
	[IB_7322_LT_STATE_CFGIDLE] = IB_PHYSPORTSTATE_CFG_IDLE,
	[IB_7322_LT_STATE_RECOVERRETRAIN] =
		IB_PHYSPORTSTATE_LINK_ERR_RECOVER,
	[IB_7322_LT_STATE_RECOVERWAITRMT] =
		IB_PHYSPORTSTATE_LINK_ERR_RECOVER,
	[IB_7322_LT_STATE_RECOVERIDLE] =
		IB_PHYSPORTSTATE_LINK_ERR_RECOVER,
	[IB_7322_LT_STATE_CFGENH] = IB_PHYSPORTSTATE_CFG_ENH,
	[IB_7322_LT_STATE_CFGTEST] = IB_PHYSPORTSTATE_CFG_TRAIN,
	[IB_7322_LT_STATE_CFGWAITRMTTEST] =
		IB_PHYSPORTSTATE_CFG_TRAIN,
	[IB_7322_LT_STATE_CFGWAITENH] =
		IB_PHYSPORTSTATE_CFG_WAIT_ENH,
	[0x14] = IB_PHYSPORTSTATE_CFG_TRAIN,
	[0x15] = IB_PHYSPORTSTATE_CFG_TRAIN,
	[0x16] = IB_PHYSPORTSTATE_CFG_TRAIN,
	[0x17] = IB_PHYSPORTSTATE_CFG_TRAIN
};

#ifdef CONFIG_INFINIBAND_QIB_DCA
struct qib_irq_notify {
	int rcv;
	void *arg;
	struct irq_affinity_notify notify;
};
#endif

struct qib_chip_specific {
	u64 __iomem *cregbase;
	u64 *cntrs;
	spinlock_t rcvmod_lock; /* protect rcvctrl shadow changes */
	spinlock_t gpio_lock; /* RMW of shadows/regs for ExtCtrl and GPIO */
	u64 main_int_mask;      /* clear bits which have dedicated handlers */
	u64 int_enable_mask;  /* for per port interrupts in single port mode */
	u64 errormask;
	u64 hwerrmask;
	u64 gpio_out; /* shadow of kr_gpio_out, for rmw ops */
	u64 gpio_mask; /* shadow the gpio mask register */
	u64 extctrl; /* shadow the gpio output enable, etc... */
	u32 ncntrs;
	u32 nportcntrs;
	u32 cntrnamelen;
	u32 portcntrnamelen;
	u32 numctxts;
	u32 rcvegrcnt;
	u32 updthresh; /* current AvailUpdThld */
	u32 updthresh_dflt; /* default AvailUpdThld */
	u32 r1;
	u32 num_msix_entries;
	u32 sdmabufcnt;
	u32 lastbuf_for_pio;
	u32 stay_in_freeze;
	u32 recovery_ports_initted;
#ifdef CONFIG_INFINIBAND_QIB_DCA
	u32 dca_ctrl;
	int rhdr_cpu[18];
	int sdma_cpu[2];
	u64 dca_rcvhdr_ctrl[5]; /* B, C, D, E, F */
#endif
	struct qib_msix_entry *msix_entries;
	unsigned long *sendchkenable;
	unsigned long *sendgrhchk;
	unsigned long *sendibchk;
	u32 rcvavail_timeout[18];
	char emsgbuf[128]; /* for device error interrupt msg buffer */
};

/* Table of entries in "human readable" form Tx Emphasis. */
struct txdds_ent {
	u8 amp;
	u8 pre;
	u8 main;
	u8 post;
};

struct vendor_txdds_ent {
	u8 oui[QSFP_VOUI_LEN];
	u8 *partnum;
	struct txdds_ent sdr;
	struct txdds_ent ddr;
	struct txdds_ent qdr;
};

static void write_tx_serdes_param(struct qib_pportdata *, struct txdds_ent *);

#define TXDDS_TABLE_SZ 16 /* number of entries per speed in onchip table */
#define TXDDS_EXTRA_SZ 18 /* number of extra tx settings entries */
#define TXDDS_MFG_SZ 2    /* number of mfg tx settings entries */
#define SERDES_CHANS 4 /* yes, it's obvious, but one less magic number */

#define H1_FORCE_VAL 8
#define H1_FORCE_QME 1 /*  may be overridden via setup_txselect() */
#define H1_FORCE_QMH 7 /*  may be overridden via setup_txselect() */

/* The static and dynamic registers are paired, and the pairs indexed by spd */
#define krp_static_adapt_dis(spd) (KREG_IBPORT_IDX(ADAPT_DISABLE_STATIC_SDR) \
	+ ((spd) * 2))

#define QDR_DFE_DISABLE_DELAY 4000 /* msec after LINKUP */
#define QDR_STATIC_ADAPT_DOWN 0xf0f0f0f0ULL /* link down, H1-H4 QDR adapts */
#define QDR_STATIC_ADAPT_DOWN_R1 0ULL /* r1 link down, H1-H4 QDR adapts */
#define QDR_STATIC_ADAPT_INIT 0xffffffffffULL /* up, disable H0,H1-8, LE */
#define QDR_STATIC_ADAPT_INIT_R1 0xf0ffffffffULL /* r1 up, disable H0,H1-8 */

struct qib_chippport_specific {
	u64 __iomem *kpregbase;
	u64 __iomem *cpregbase;
	u64 *portcntrs;
	struct qib_pportdata *ppd;
	wait_queue_head_t autoneg_wait;
	struct delayed_work autoneg_work;
	struct delayed_work ipg_work;
	struct timer_list chase_timer;
	/*
	 * these 5 fields are used to establish deltas for IB symbol
	 * errors and linkrecovery errors.  They can be reported on
	 * some chips during link negotiation prior to INIT, and with
	 * DDR when faking DDR negotiations with non-IBTA switches.
	 * The chip counters are adjusted at driver unload if there is
	 * a non-zero delta.
	 */
	u64 ibdeltainprog;
	u64 ibsymdelta;
	u64 ibsymsnap;
	u64 iblnkerrdelta;
	u64 iblnkerrsnap;
	u64 iblnkdownsnap;
	u64 iblnkdowndelta;
	u64 ibmalfdelta;
	u64 ibmalfsnap;
	u64 ibcctrl_a; /* krp_ibcctrl_a shadow */
	u64 ibcctrl_b; /* krp_ibcctrl_b shadow */
	unsigned long qdr_dfe_time;
	unsigned long chase_end;
	u32 autoneg_tries;
	u32 recovery_init;
	u32 qdr_dfe_on;
	u32 qdr_reforce;
	/*
	 * Per-bay per-channel rcv QMH H1 values and Tx values for QDR.
	 * entry zero is unused, to simplify indexing
	 */
	u8 h1_val;
	u8 no_eep;  /* txselect table index to use if no qsfp info */
	u8 ipg_tries;
	u8 ibmalfusesnap;
	struct qib_qsfp_data qsfp_data;
	char epmsgbuf[192]; /* for port error interrupt msg buffer */
	char sdmamsgbuf[192]; /* for per-port sdma error messages */
};

static struct {
	const char *name;
	irq_handler_t handler;
	int lsb;
	int port; /* 0 if not port-specific, else port # */
	int dca;
} irq_table[] = {
	{ "", qib_7322intr, -1, 0, 0 },
	{ " (buf avail)", qib_7322bufavail,
		SYM_LSB(IntStatus, SendBufAvail), 0, 0},
	{ " (sdma 0)", sdma_intr,
		SYM_LSB(IntStatus, SDmaInt_0), 1, 1 },
	{ " (sdma 1)", sdma_intr,
		SYM_LSB(IntStatus, SDmaInt_1), 2, 1 },
	{ " (sdmaI 0)", sdma_idle_intr,
		SYM_LSB(IntStatus, SDmaIdleInt_0), 1, 1},
	{ " (sdmaI 1)", sdma_idle_intr,
		SYM_LSB(IntStatus, SDmaIdleInt_1), 2, 1},
	{ " (sdmaP 0)", sdma_progress_intr,
		SYM_LSB(IntStatus, SDmaProgressInt_0), 1, 1 },
	{ " (sdmaP 1)", sdma_progress_intr,
		SYM_LSB(IntStatus, SDmaProgressInt_1), 2, 1 },
	{ " (sdmaC 0)", sdma_cleanup_intr,
		SYM_LSB(IntStatus, SDmaCleanupDone_0), 1, 0 },
	{ " (sdmaC 1)", sdma_cleanup_intr,
		SYM_LSB(IntStatus, SDmaCleanupDone_1), 2 , 0},
};

#ifdef CONFIG_INFINIBAND_QIB_DCA

static const struct dca_reg_map {
	int     shadow_inx;
	int     lsb;
	u64     mask;
	u16     regno;
} dca_rcvhdr_reg_map[] = {
	{ 0, SYM_LSB(DCACtrlB, RcvHdrq0DCAOPH),
	   ~SYM_MASK(DCACtrlB, RcvHdrq0DCAOPH) , KREG_IDX(DCACtrlB) },
	{ 0, SYM_LSB(DCACtrlB, RcvHdrq1DCAOPH),
	   ~SYM_MASK(DCACtrlB, RcvHdrq1DCAOPH) , KREG_IDX(DCACtrlB) },
	{ 0, SYM_LSB(DCACtrlB, RcvHdrq2DCAOPH),
	   ~SYM_MASK(DCACtrlB, RcvHdrq2DCAOPH) , KREG_IDX(DCACtrlB) },
	{ 0, SYM_LSB(DCACtrlB, RcvHdrq3DCAOPH),
	   ~SYM_MASK(DCACtrlB, RcvHdrq3DCAOPH) , KREG_IDX(DCACtrlB) },
	{ 1, SYM_LSB(DCACtrlC, RcvHdrq4DCAOPH),
	   ~SYM_MASK(DCACtrlC, RcvHdrq4DCAOPH) , KREG_IDX(DCACtrlC) },
	{ 1, SYM_LSB(DCACtrlC, RcvHdrq5DCAOPH),
	   ~SYM_MASK(DCACtrlC, RcvHdrq5DCAOPH) , KREG_IDX(DCACtrlC) },
	{ 1, SYM_LSB(DCACtrlC, RcvHdrq6DCAOPH),
	   ~SYM_MASK(DCACtrlC, RcvHdrq6DCAOPH) , KREG_IDX(DCACtrlC) },
	{ 1, SYM_LSB(DCACtrlC, RcvHdrq7DCAOPH),
	   ~SYM_MASK(DCACtrlC, RcvHdrq7DCAOPH) , KREG_IDX(DCACtrlC) },
	{ 2, SYM_LSB(DCACtrlD, RcvHdrq8DCAOPH),
	   ~SYM_MASK(DCACtrlD, RcvHdrq8DCAOPH) , KREG_IDX(DCACtrlD) },
	{ 2, SYM_LSB(DCACtrlD, RcvHdrq9DCAOPH),
	   ~SYM_MASK(DCACtrlD, RcvHdrq9DCAOPH) , KREG_IDX(DCACtrlD) },
	{ 2, SYM_LSB(DCACtrlD, RcvHdrq10DCAOPH),
	   ~SYM_MASK(DCACtrlD, RcvHdrq10DCAOPH) , KREG_IDX(DCACtrlD) },
	{ 2, SYM_LSB(DCACtrlD, RcvHdrq11DCAOPH),
	   ~SYM_MASK(DCACtrlD, RcvHdrq11DCAOPH) , KREG_IDX(DCACtrlD) },
	{ 3, SYM_LSB(DCACtrlE, RcvHdrq12DCAOPH),
	   ~SYM_MASK(DCACtrlE, RcvHdrq12DCAOPH) , KREG_IDX(DCACtrlE) },
	{ 3, SYM_LSB(DCACtrlE, RcvHdrq13DCAOPH),
	   ~SYM_MASK(DCACtrlE, RcvHdrq13DCAOPH) , KREG_IDX(DCACtrlE) },
	{ 3, SYM_LSB(DCACtrlE, RcvHdrq14DCAOPH),
	   ~SYM_MASK(DCACtrlE, RcvHdrq14DCAOPH) , KREG_IDX(DCACtrlE) },
	{ 3, SYM_LSB(DCACtrlE, RcvHdrq15DCAOPH),
	   ~SYM_MASK(DCACtrlE, RcvHdrq15DCAOPH) , KREG_IDX(DCACtrlE) },
	{ 4, SYM_LSB(DCACtrlF, RcvHdrq16DCAOPH),
	   ~SYM_MASK(DCACtrlF, RcvHdrq16DCAOPH) , KREG_IDX(DCACtrlF) },
	{ 4, SYM_LSB(DCACtrlF, RcvHdrq17DCAOPH),
	   ~SYM_MASK(DCACtrlF, RcvHdrq17DCAOPH) , KREG_IDX(DCACtrlF) },
};
#endif

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

#define BLOB_7322_IBCHG 0x101

static inline void qib_write_kreg(const struct qib_devdata *dd,
				  const u32 regno, u64 value);
static inline u32 qib_read_kreg32(const struct qib_devdata *, const u32);
static void write_7322_initregs(struct qib_devdata *);
static void write_7322_init_portregs(struct qib_pportdata *);
static void setup_7322_link_recovery(struct qib_pportdata *, u32);
static void check_7322_rxe_status(struct qib_pportdata *);
static u32 __iomem *qib_7322_getsendbuf(struct qib_pportdata *, u64, u32 *);
#ifdef CONFIG_INFINIBAND_QIB_DCA
static void qib_setup_dca(struct qib_devdata *dd);
static void setup_dca_notifier(struct qib_devdata *dd, int msixnum);
static void reset_dca_notifier(struct qib_devdata *dd, int msixnum);
#endif

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
	return readl(regno + (u64 __iomem *)(
		(dd->ureg_align * ctxt) + (dd->userbase ?
		 (char __iomem *)dd->userbase :
		 (char __iomem *)dd->kregbase + dd->uregbase)));
}

/**
 * qib_write_ureg - write virtualized per-context register
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

static inline u32 qib_read_kreg32(const struct qib_devdata *dd,
				  const u32 regno)
{
	if (!dd->kregbase || !(dd->flags & QIB_PRESENT))
		return -1;
	return readl((u32 __iomem *) &dd->kregbase[regno]);
}

static inline u64 qib_read_kreg64(const struct qib_devdata *dd,
				  const u32 regno)
{
	if (!dd->kregbase || !(dd->flags & QIB_PRESENT))
		return -1;
	return readq(&dd->kregbase[regno]);
}

static inline void qib_write_kreg(const struct qib_devdata *dd,
				  const u32 regno, u64 value)
{
	if (dd->kregbase && (dd->flags & QIB_PRESENT))
		writeq(value, &dd->kregbase[regno]);
}

/*
 * not many sanity checks for the port-specific kernel register routines,
 * since they are only used when it's known to be safe.
*/
static inline u64 qib_read_kreg_port(const struct qib_pportdata *ppd,
				     const u16 regno)
{
	if (!ppd->cpspec->kpregbase || !(ppd->dd->flags & QIB_PRESENT))
		return 0ULL;
	return readq(&ppd->cpspec->kpregbase[regno]);
}

static inline void qib_write_kreg_port(const struct qib_pportdata *ppd,
				       const u16 regno, u64 value)
{
	if (ppd->cpspec && ppd->dd && ppd->cpspec->kpregbase &&
	    (ppd->dd->flags & QIB_PRESENT))
		writeq(value, &ppd->cpspec->kpregbase[regno]);
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

static inline u64 read_7322_creg(const struct qib_devdata *dd, u16 regno)
{
	if (!dd->cspec->cregbase || !(dd->flags & QIB_PRESENT))
		return 0;
	return readq(&dd->cspec->cregbase[regno]);


}

static inline u32 read_7322_creg32(const struct qib_devdata *dd, u16 regno)
{
	if (!dd->cspec->cregbase || !(dd->flags & QIB_PRESENT))
		return 0;
	return readl(&dd->cspec->cregbase[regno]);


}

static inline void write_7322_creg_port(const struct qib_pportdata *ppd,
					u16 regno, u64 value)
{
	if (ppd->cpspec && ppd->cpspec->cpregbase &&
	    (ppd->dd->flags & QIB_PRESENT))
		writeq(value, &ppd->cpspec->cpregbase[regno]);
}

static inline u64 read_7322_creg_port(const struct qib_pportdata *ppd,
				      u16 regno)
{
	if (!ppd->cpspec || !ppd->cpspec->cpregbase ||
	    !(ppd->dd->flags & QIB_PRESENT))
		return 0;
	return readq(&ppd->cpspec->cpregbase[regno]);
}

static inline u32 read_7322_creg32_port(const struct qib_pportdata *ppd,
					u16 regno)
{
	if (!ppd->cpspec || !ppd->cpspec->cpregbase ||
	    !(ppd->dd->flags & QIB_PRESENT))
		return 0;
	return readl(&ppd->cpspec->cpregbase[regno]);
}

/* bits in Control register */
#define QLOGIC_IB_C_RESET SYM_MASK(Control, SyncReset)
#define QLOGIC_IB_C_SDMAFETCHPRIOEN SYM_MASK(Control, SDmaDescFetchPriorityEn)

/* bits in general interrupt regs */
#define QIB_I_RCVURG_LSB SYM_LSB(IntMask, RcvUrg0IntMask)
#define QIB_I_RCVURG_RMASK MASK_ACROSS(0, 17)
#define QIB_I_RCVURG_MASK (QIB_I_RCVURG_RMASK << QIB_I_RCVURG_LSB)
#define QIB_I_RCVAVAIL_LSB SYM_LSB(IntMask, RcvAvail0IntMask)
#define QIB_I_RCVAVAIL_RMASK MASK_ACROSS(0, 17)
#define QIB_I_RCVAVAIL_MASK (QIB_I_RCVAVAIL_RMASK << QIB_I_RCVAVAIL_LSB)
#define QIB_I_C_ERROR INT_MASK(Err)

#define QIB_I_SPIOSENT (INT_MASK_P(SendDone, 0) | INT_MASK_P(SendDone, 1))
#define QIB_I_SPIOBUFAVAIL INT_MASK(SendBufAvail)
#define QIB_I_GPIO INT_MASK(AssertGPIO)
#define QIB_I_P_SDMAINT(pidx) \
	(INT_MASK_P(SDma, pidx) | INT_MASK_P(SDmaIdle, pidx) | \
	 INT_MASK_P(SDmaProgress, pidx) | \
	 INT_MASK_PM(SDmaCleanupDone, pidx))

/* Interrupt bits that are "per port" */
#define QIB_I_P_BITSEXTANT(pidx) \
	(INT_MASK_P(Err, pidx) | INT_MASK_P(SendDone, pidx) | \
	INT_MASK_P(SDma, pidx) | INT_MASK_P(SDmaIdle, pidx) | \
	INT_MASK_P(SDmaProgress, pidx) | \
	INT_MASK_PM(SDmaCleanupDone, pidx))

/* Interrupt bits that are common to a device */
/* currently unused: QIB_I_SPIOSENT */
#define QIB_I_C_BITSEXTANT \
	(QIB_I_RCVURG_MASK | QIB_I_RCVAVAIL_MASK | \
	QIB_I_SPIOSENT | \
	QIB_I_C_ERROR | QIB_I_SPIOBUFAVAIL | QIB_I_GPIO)

#define QIB_I_BITSEXTANT (QIB_I_C_BITSEXTANT | \
	QIB_I_P_BITSEXTANT(0) | QIB_I_P_BITSEXTANT(1))

/*
 * Error bits that are "per port".
 */
#define QIB_E_P_IBSTATUSCHANGED ERR_MASK_N(IBStatusChanged)
#define QIB_E_P_SHDR ERR_MASK_N(SHeadersErr)
#define QIB_E_P_VL15_BUF_MISUSE ERR_MASK_N(VL15BufMisuseErr)
#define QIB_E_P_SND_BUF_MISUSE ERR_MASK_N(SendBufMisuseErr)
#define QIB_E_P_SUNSUPVL ERR_MASK_N(SendUnsupportedVLErr)
#define QIB_E_P_SUNEXP_PKTNUM ERR_MASK_N(SendUnexpectedPktNumErr)
#define QIB_E_P_SDROP_DATA ERR_MASK_N(SendDroppedDataPktErr)
#define QIB_E_P_SDROP_SMP ERR_MASK_N(SendDroppedSmpPktErr)
#define QIB_E_P_SPKTLEN ERR_MASK_N(SendPktLenErr)
#define QIB_E_P_SUNDERRUN ERR_MASK_N(SendUnderRunErr)
#define QIB_E_P_SMAXPKTLEN ERR_MASK_N(SendMaxPktLenErr)
#define QIB_E_P_SMINPKTLEN ERR_MASK_N(SendMinPktLenErr)
#define QIB_E_P_RIBLOSTLINK ERR_MASK_N(RcvIBLostLinkErr)
#define QIB_E_P_RHDR ERR_MASK_N(RcvHdrErr)
#define QIB_E_P_RHDRLEN ERR_MASK_N(RcvHdrLenErr)
#define QIB_E_P_RBADTID ERR_MASK_N(RcvBadTidErr)
#define QIB_E_P_RBADVERSION ERR_MASK_N(RcvBadVersionErr)
#define QIB_E_P_RIBFLOW ERR_MASK_N(RcvIBFlowErr)
#define QIB_E_P_REBP ERR_MASK_N(RcvEBPErr)
#define QIB_E_P_RUNSUPVL ERR_MASK_N(RcvUnsupportedVLErr)
#define QIB_E_P_RUNEXPCHAR ERR_MASK_N(RcvUnexpectedCharErr)
#define QIB_E_P_RSHORTPKTLEN ERR_MASK_N(RcvShortPktLenErr)
#define QIB_E_P_RLONGPKTLEN ERR_MASK_N(RcvLongPktLenErr)
#define QIB_E_P_RMAXPKTLEN ERR_MASK_N(RcvMaxPktLenErr)
#define QIB_E_P_RMINPKTLEN ERR_MASK_N(RcvMinPktLenErr)
#define QIB_E_P_RICRC ERR_MASK_N(RcvICRCErr)
#define QIB_E_P_RVCRC ERR_MASK_N(RcvVCRCErr)
#define QIB_E_P_RFORMATERR ERR_MASK_N(RcvFormatErr)

#define QIB_E_P_SDMA1STDESC ERR_MASK_N(SDma1stDescErr)
#define QIB_E_P_SDMABASE ERR_MASK_N(SDmaBaseErr)
#define QIB_E_P_SDMADESCADDRMISALIGN ERR_MASK_N(SDmaDescAddrMisalignErr)
#define QIB_E_P_SDMADWEN ERR_MASK_N(SDmaDwEnErr)
#define QIB_E_P_SDMAGENMISMATCH ERR_MASK_N(SDmaGenMismatchErr)
#define QIB_E_P_SDMAHALT ERR_MASK_N(SDmaHaltErr)
#define QIB_E_P_SDMAMISSINGDW ERR_MASK_N(SDmaMissingDwErr)
#define QIB_E_P_SDMAOUTOFBOUND ERR_MASK_N(SDmaOutOfBoundErr)
#define QIB_E_P_SDMARPYTAG ERR_MASK_N(SDmaRpyTagErr)
#define QIB_E_P_SDMATAILOUTOFBOUND ERR_MASK_N(SDmaTailOutOfBoundErr)
#define QIB_E_P_SDMAUNEXPDATA ERR_MASK_N(SDmaUnexpDataErr)

/* Error bits that are common to a device */
#define QIB_E_RESET ERR_MASK(ResetNegated)
#define QIB_E_HARDWARE ERR_MASK(HardwareErr)
#define QIB_E_INVALIDADDR ERR_MASK(InvalidAddrErr)


/*
 * Per chip (rather than per-port) errors.  Most either do
 * nothing but trigger a print (because they self-recover, or
 * always occur in tandem with other errors that handle the
 * issue), or because they indicate errors with no recovery,
 * but we want to know that they happened.
 */
#define QIB_E_SBUF_VL15_MISUSE ERR_MASK(SBufVL15MisUseErr)
#define QIB_E_BADEEP ERR_MASK(InvalidEEPCmd)
#define QIB_E_VLMISMATCH ERR_MASK(SendVLMismatchErr)
#define QIB_E_ARMLAUNCH ERR_MASK(SendArmLaunchErr)
#define QIB_E_SPCLTRIG ERR_MASK(SendSpecialTriggerErr)
#define QIB_E_RRCVHDRFULL ERR_MASK(RcvHdrFullErr)
#define QIB_E_RRCVEGRFULL ERR_MASK(RcvEgrFullErr)
#define QIB_E_RCVCTXTSHARE ERR_MASK(RcvContextShareErr)

/* SDMA chip errors (not per port)
 * QIB_E_SDMA_BUF_DUP needs no special handling, because we will also get
 * the SDMAHALT error immediately, so we just print the dup error via the
 * E_AUTO mechanism.  This is true of most of the per-port fatal errors
 * as well, but since this is port-independent, by definition, it's
 * handled a bit differently.  SDMA_VL15 and SDMA_WRONG_PORT are per
 * packet send errors, and so are handled in the same manner as other
 * per-packet errors.
 */
#define QIB_E_SDMA_VL15 ERR_MASK(SDmaVL15Err)
#define QIB_E_SDMA_WRONG_PORT ERR_MASK(SDmaWrongPortErr)
#define QIB_E_SDMA_BUF_DUP ERR_MASK(SDmaBufMaskDuplicateErr)

/*
 * Below functionally equivalent to legacy QLOGIC_IB_E_PKTERRS
 * it is used to print "common" packet errors.
 */
#define QIB_E_P_PKTERRS (QIB_E_P_SPKTLEN |\
	QIB_E_P_SDROP_DATA | QIB_E_P_RVCRC |\
	QIB_E_P_RICRC | QIB_E_P_RSHORTPKTLEN |\
	QIB_E_P_VL15_BUF_MISUSE | QIB_E_P_SHDR | \
	QIB_E_P_REBP)

/* Error Bits that Packet-related (Receive, per-port) */
#define QIB_E_P_RPKTERRS (\
	QIB_E_P_RHDRLEN | QIB_E_P_RBADTID | \
	QIB_E_P_RBADVERSION | QIB_E_P_RHDR | \
	QIB_E_P_RLONGPKTLEN | QIB_E_P_RSHORTPKTLEN |\
	QIB_E_P_RMAXPKTLEN | QIB_E_P_RMINPKTLEN | \
	QIB_E_P_RFORMATERR | QIB_E_P_RUNSUPVL | \
	QIB_E_P_RUNEXPCHAR | QIB_E_P_RIBFLOW | QIB_E_P_REBP)

/*
 * Error bits that are Send-related (per port)
 * (ARMLAUNCH excluded from E_SPKTERRS because it gets special handling).
 * All of these potentially need to have a buffer disarmed
 */
#define QIB_E_P_SPKTERRS (\
	QIB_E_P_SUNEXP_PKTNUM |\
	QIB_E_P_SDROP_DATA | QIB_E_P_SDROP_SMP |\
	QIB_E_P_SMAXPKTLEN |\
	QIB_E_P_VL15_BUF_MISUSE | QIB_E_P_SHDR | \
	QIB_E_P_SMINPKTLEN | QIB_E_P_SPKTLEN | \
	QIB_E_P_SND_BUF_MISUSE | QIB_E_P_SUNSUPVL)

#define QIB_E_SPKTERRS ( \
		QIB_E_SBUF_VL15_MISUSE | QIB_E_VLMISMATCH | \
		ERR_MASK_N(SendUnsupportedVLErr) |			\
		QIB_E_SPCLTRIG | QIB_E_SDMA_VL15 | QIB_E_SDMA_WRONG_PORT)

#define QIB_E_P_SDMAERRS ( \
	QIB_E_P_SDMAHALT | \
	QIB_E_P_SDMADESCADDRMISALIGN | \
	QIB_E_P_SDMAUNEXPDATA | \
	QIB_E_P_SDMAMISSINGDW | \
	QIB_E_P_SDMADWEN | \
	QIB_E_P_SDMARPYTAG | \
	QIB_E_P_SDMA1STDESC | \
	QIB_E_P_SDMABASE | \
	QIB_E_P_SDMATAILOUTOFBOUND | \
	QIB_E_P_SDMAOUTOFBOUND | \
	QIB_E_P_SDMAGENMISMATCH)

/*
 * This sets some bits more than once, but makes it more obvious which
 * bits are not handled under other categories, and the repeat definition
 * is not a problem.
 */
#define QIB_E_P_BITSEXTANT ( \
	QIB_E_P_SPKTERRS | QIB_E_P_PKTERRS | QIB_E_P_RPKTERRS | \
	QIB_E_P_RIBLOSTLINK | QIB_E_P_IBSTATUSCHANGED | \
	QIB_E_P_SND_BUF_MISUSE | QIB_E_P_SUNDERRUN | \
	QIB_E_P_SHDR | QIB_E_P_VL15_BUF_MISUSE | QIB_E_P_SDMAERRS \
	)

/*
 * These are errors that can occur when the link
 * changes state while a packet is being sent or received.  This doesn't
 * cover things like EBP or VCRC that can be the result of a sending
 * having the link change state, so we receive a "known bad" packet.
 * All of these are "per port", so renamed:
 */
#define QIB_E_P_LINK_PKTERRS (\
	QIB_E_P_SDROP_DATA | QIB_E_P_SDROP_SMP |\
	QIB_E_P_SMINPKTLEN | QIB_E_P_SPKTLEN |\
	QIB_E_P_RSHORTPKTLEN | QIB_E_P_RMINPKTLEN |\
	QIB_E_P_RUNEXPCHAR)

/*
 * This sets some bits more than once, but makes it more obvious which
 * bits are not handled under other categories (such as QIB_E_SPKTERRS),
 * and the repeat definition is not a problem.
 */
#define QIB_E_C_BITSEXTANT (\
	QIB_E_HARDWARE | QIB_E_INVALIDADDR | QIB_E_BADEEP |\
	QIB_E_ARMLAUNCH | QIB_E_VLMISMATCH | QIB_E_RRCVHDRFULL |\
	QIB_E_RRCVEGRFULL | QIB_E_RESET | QIB_E_SBUF_VL15_MISUSE)

/* Likewise Neuter E_SPKT_ERRS_IGNORE */
#define E_SPKT_ERRS_IGNORE 0

#define QIB_EXTS_MEMBIST_DISABLED \
	SYM_MASK(EXTStatus, MemBISTDisabled)
#define QIB_EXTS_MEMBIST_ENDTEST \
	SYM_MASK(EXTStatus, MemBISTEndTest)

#define QIB_E_SPIOARMLAUNCH \
	ERR_MASK(SendArmLaunchErr)

#define IBA7322_IBCC_LINKINITCMD_MASK SYM_RMASK(IBCCtrlA_0, LinkInitCmd)
#define IBA7322_IBCC_LINKCMD_SHIFT SYM_LSB(IBCCtrlA_0, LinkCmd)

/*
 * IBTA_1_2 is set when multiple speeds are enabled (normal),
 * and also if forced QDR (only QDR enabled).  It's enabled for the
 * forced QDR case so that scrambling will be enabled by the TS3
 * exchange, when supported by both sides of the link.
 */
#define IBA7322_IBC_IBTA_1_2_MASK SYM_MASK(IBCCtrlB_0, IB_ENHANCED_MODE)
#define IBA7322_IBC_MAX_SPEED_MASK SYM_MASK(IBCCtrlB_0, SD_SPEED)
#define IBA7322_IBC_SPEED_QDR SYM_MASK(IBCCtrlB_0, SD_SPEED_QDR)
#define IBA7322_IBC_SPEED_DDR SYM_MASK(IBCCtrlB_0, SD_SPEED_DDR)
#define IBA7322_IBC_SPEED_SDR SYM_MASK(IBCCtrlB_0, SD_SPEED_SDR)
#define IBA7322_IBC_SPEED_MASK (SYM_MASK(IBCCtrlB_0, SD_SPEED_SDR) | \
	SYM_MASK(IBCCtrlB_0, SD_SPEED_DDR) | SYM_MASK(IBCCtrlB_0, SD_SPEED_QDR))
#define IBA7322_IBC_SPEED_LSB SYM_LSB(IBCCtrlB_0, SD_SPEED_SDR)

#define IBA7322_LEDBLINK_OFF_SHIFT SYM_LSB(RcvPktLEDCnt_0, OFFperiod)
#define IBA7322_LEDBLINK_ON_SHIFT SYM_LSB(RcvPktLEDCnt_0, ONperiod)

#define IBA7322_IBC_WIDTH_AUTONEG SYM_MASK(IBCCtrlB_0, IB_NUM_CHANNELS)
#define IBA7322_IBC_WIDTH_4X_ONLY (1<<SYM_LSB(IBCCtrlB_0, IB_NUM_CHANNELS))
#define IBA7322_IBC_WIDTH_1X_ONLY (0<<SYM_LSB(IBCCtrlB_0, IB_NUM_CHANNELS))

#define IBA7322_IBC_RXPOL_MASK SYM_MASK(IBCCtrlB_0, IB_POLARITY_REV_SUPP)
#define IBA7322_IBC_RXPOL_LSB SYM_LSB(IBCCtrlB_0, IB_POLARITY_REV_SUPP)
#define IBA7322_IBC_HRTBT_MASK (SYM_MASK(IBCCtrlB_0, HRTBT_AUTO) | \
	SYM_MASK(IBCCtrlB_0, HRTBT_ENB))
#define IBA7322_IBC_HRTBT_RMASK (IBA7322_IBC_HRTBT_MASK >> \
	SYM_LSB(IBCCtrlB_0, HRTBT_ENB))
#define IBA7322_IBC_HRTBT_LSB SYM_LSB(IBCCtrlB_0, HRTBT_ENB)

#define IBA7322_REDIRECT_VEC_PER_REG 12

#define IBA7322_SENDCHK_PKEY SYM_MASK(SendCheckControl_0, PKey_En)
#define IBA7322_SENDCHK_BTHQP SYM_MASK(SendCheckControl_0, BTHQP_En)
#define IBA7322_SENDCHK_SLID SYM_MASK(SendCheckControl_0, SLID_En)
#define IBA7322_SENDCHK_RAW_IPV6 SYM_MASK(SendCheckControl_0, RawIPV6_En)
#define IBA7322_SENDCHK_MINSZ SYM_MASK(SendCheckControl_0, PacketTooSmall_En)

#define AUTONEG_TRIES 3 /* sequential retries to negotiate DDR */

#define HWE_AUTO(fldname) { .mask = SYM_MASK(HwErrMask, fldname##Mask), \
	.msg = #fldname , .sz = sizeof(#fldname) }
#define HWE_AUTO_P(fldname, port) { .mask = SYM_MASK(HwErrMask, \
	fldname##Mask##_##port), .msg = #fldname , .sz = sizeof(#fldname) }
static const struct qib_hwerror_msgs qib_7322_hwerror_msgs[] = {
	HWE_AUTO_P(IBSerdesPClkNotDetect, 1),
	HWE_AUTO_P(IBSerdesPClkNotDetect, 0),
	HWE_AUTO(PCIESerdesPClkNotDetect),
	HWE_AUTO(PowerOnBISTFailed),
	HWE_AUTO(TempsenseTholdReached),
	HWE_AUTO(MemoryErr),
	HWE_AUTO(PCIeBusParityErr),
	HWE_AUTO(PcieCplTimeout),
	HWE_AUTO(PciePoisonedTLP),
	HWE_AUTO_P(SDmaMemReadErr, 1),
	HWE_AUTO_P(SDmaMemReadErr, 0),
	HWE_AUTO_P(IBCBusFromSPCParityErr, 1),
	HWE_AUTO_P(IBCBusToSPCParityErr, 1),
	HWE_AUTO_P(IBCBusFromSPCParityErr, 0),
	HWE_AUTO(statusValidNoEop),
	HWE_AUTO(LATriggered),
	{ .mask = 0, .sz = 0 }
};

#define E_AUTO(fldname) { .mask = SYM_MASK(ErrMask, fldname##Mask), \
	.msg = #fldname, .sz = sizeof(#fldname) }
#define E_P_AUTO(fldname) { .mask = SYM_MASK(ErrMask_0, fldname##Mask), \
	.msg = #fldname, .sz = sizeof(#fldname) }
static const struct qib_hwerror_msgs qib_7322error_msgs[] = {
	E_AUTO(RcvEgrFullErr),
	E_AUTO(RcvHdrFullErr),
	E_AUTO(ResetNegated),
	E_AUTO(HardwareErr),
	E_AUTO(InvalidAddrErr),
	E_AUTO(SDmaVL15Err),
	E_AUTO(SBufVL15MisUseErr),
	E_AUTO(InvalidEEPCmd),
	E_AUTO(RcvContextShareErr),
	E_AUTO(SendVLMismatchErr),
	E_AUTO(SendArmLaunchErr),
	E_AUTO(SendSpecialTriggerErr),
	E_AUTO(SDmaWrongPortErr),
	E_AUTO(SDmaBufMaskDuplicateErr),
	{ .mask = 0, .sz = 0 }
};

static const struct  qib_hwerror_msgs qib_7322p_error_msgs[] = {
	E_P_AUTO(IBStatusChanged),
	E_P_AUTO(SHeadersErr),
	E_P_AUTO(VL15BufMisuseErr),
	/*
	 * SDmaHaltErr is not really an error, make it clearer;
	 */
	{.mask = SYM_MASK(ErrMask_0, SDmaHaltErrMask), .msg = "SDmaHalted",
		.sz = 11},
	E_P_AUTO(SDmaDescAddrMisalignErr),
	E_P_AUTO(SDmaUnexpDataErr),
	E_P_AUTO(SDmaMissingDwErr),
	E_P_AUTO(SDmaDwEnErr),
	E_P_AUTO(SDmaRpyTagErr),
	E_P_AUTO(SDma1stDescErr),
	E_P_AUTO(SDmaBaseErr),
	E_P_AUTO(SDmaTailOutOfBoundErr),
	E_P_AUTO(SDmaOutOfBoundErr),
	E_P_AUTO(SDmaGenMismatchErr),
	E_P_AUTO(SendBufMisuseErr),
	E_P_AUTO(SendUnsupportedVLErr),
	E_P_AUTO(SendUnexpectedPktNumErr),
	E_P_AUTO(SendDroppedDataPktErr),
	E_P_AUTO(SendDroppedSmpPktErr),
	E_P_AUTO(SendPktLenErr),
	E_P_AUTO(SendUnderRunErr),
	E_P_AUTO(SendMaxPktLenErr),
	E_P_AUTO(SendMinPktLenErr),
	E_P_AUTO(RcvIBLostLinkErr),
	E_P_AUTO(RcvHdrErr),
	E_P_AUTO(RcvHdrLenErr),
	E_P_AUTO(RcvBadTidErr),
	E_P_AUTO(RcvBadVersionErr),
	E_P_AUTO(RcvIBFlowErr),
	E_P_AUTO(RcvEBPErr),
	E_P_AUTO(RcvUnsupportedVLErr),
	E_P_AUTO(RcvUnexpectedCharErr),
	E_P_AUTO(RcvShortPktLenErr),
	E_P_AUTO(RcvLongPktLenErr),
	E_P_AUTO(RcvMaxPktLenErr),
	E_P_AUTO(RcvMinPktLenErr),
	E_P_AUTO(RcvICRCErr),
	E_P_AUTO(RcvVCRCErr),
	E_P_AUTO(RcvFormatErr),
	{ .mask = 0, .sz = 0 }
};

/*
 * Below generates "auto-message" for interrupts not specific to any port or
 * context
 */
#define INTR_AUTO(fldname) { .mask = SYM_MASK(IntMask, fldname##Mask), \
	.msg = #fldname, .sz = sizeof(#fldname) }
/* Below generates "auto-message" for interrupts specific to a port */
#define INTR_AUTO_P(fldname) { .mask = MASK_ACROSS(\
	SYM_LSB(IntMask, fldname##Mask##_0), \
	SYM_LSB(IntMask, fldname##Mask##_1)), \
	.msg = #fldname "_P", .sz = sizeof(#fldname "_P") }
/* For some reason, the SerDesTrimDone bits are reversed */
#define INTR_AUTO_PI(fldname) { .mask = MASK_ACROSS(\
	SYM_LSB(IntMask, fldname##Mask##_1), \
	SYM_LSB(IntMask, fldname##Mask##_0)), \
	.msg = #fldname "_P", .sz = sizeof(#fldname "_P") }
/*
 * Below generates "auto-message" for interrupts specific to a context,
 * with ctxt-number appended
 */
#define INTR_AUTO_C(fldname) { .mask = MASK_ACROSS(\
	SYM_LSB(IntMask, fldname##0IntMask), \
	SYM_LSB(IntMask, fldname##17IntMask)), \
	.msg = #fldname "_C", .sz = sizeof(#fldname "_C") }

#define TXSYMPTOM_AUTO_P(fldname) \
	{ .mask = SYM_MASK(SendHdrErrSymptom_0, fldname), \
	.msg = #fldname, .sz = sizeof(#fldname) }
static const struct  qib_hwerror_msgs hdrchk_msgs[] = {
	TXSYMPTOM_AUTO_P(NonKeyPacket),
	TXSYMPTOM_AUTO_P(GRHFail),
	TXSYMPTOM_AUTO_P(PkeyFail),
	TXSYMPTOM_AUTO_P(QPFail),
	TXSYMPTOM_AUTO_P(SLIDFail),
	TXSYMPTOM_AUTO_P(RawIPV6),
	TXSYMPTOM_AUTO_P(PacketTooSmall),
	{ .mask = 0, .sz = 0 }
};

#define IBA7322_HDRHEAD_PKTINT_SHIFT 32 /* interrupt cnt in upper 32 bits */

/*
 * Called when we might have an error that is specific to a particular
 * PIO buffer, and may need to cancel that buffer, so it can be re-used,
 * because we don't need to force the update of pioavail
 */
static void qib_disarm_7322_senderrbufs(struct qib_pportdata *ppd)
{
	struct qib_devdata *dd = ppd->dd;
	u32 i;
	int any;
	u32 piobcnt = dd->piobcnt2k + dd->piobcnt4k + NUM_VL15_BUFS;
	u32 regcnt = (piobcnt + BITS_PER_LONG - 1) / BITS_PER_LONG;
	unsigned long sbuf[4];

	/*
	 * It's possible that sendbuffererror could have bits set; might
	 * have already done this as a result of hardware error handling.
	 */
	any = 0;
	for (i = 0; i < regcnt; ++i) {
		sbuf[i] = qib_read_kreg64(dd, kr_sendbuffererror + i);
		if (sbuf[i]) {
			any = 1;
			qib_write_kreg(dd, kr_sendbuffererror + i, sbuf[i]);
		}
	}

	if (any)
		qib_disarm_piobufs_set(dd, sbuf, piobcnt);
}

/* No txe_recover yet, if ever */

/* No decode__errors yet */
static void err_decode(char *msg, size_t len, u64 errs,
		       const struct qib_hwerror_msgs *msp)
{
	u64 these, lmask;
	int took, multi, n = 0;

	while (errs && msp && msp->mask) {
		multi = (msp->mask & (msp->mask - 1));
		while (errs & msp->mask) {
			these = (errs & msp->mask);
			lmask = (these & (these - 1)) ^ these;
			if (len) {
				if (n++) {
					/* separate the strings */
					*msg++ = ',';
					len--;
				}
				/* msp->sz counts the nul */
				took = min_t(size_t, msp->sz - (size_t)1, len);
				memcpy(msg,  msp->msg, took);
				len -= took;
				msg += took;
				if (len)
					*msg = '\0';
			}
			errs &= ~lmask;
			if (len && multi) {
				/* More than one bit this mask */
				int idx = -1;

				while (lmask & msp->mask) {
					++idx;
					lmask >>= 1;
				}
				took = scnprintf(msg, len, "_%d", idx);
				len -= took;
				msg += took;
			}
		}
		++msp;
	}
	/* If some bits are left, show in hex. */
	if (len && errs)
		snprintf(msg, len, "%sMORE:%llX", n ? "," : "",
			(unsigned long long) errs);
}

/* only called if r1 set */
static void flush_fifo(struct qib_pportdata *ppd)
{
	struct qib_devdata *dd = ppd->dd;
	u32 __iomem *piobuf;
	u32 bufn;
	u32 *hdr;
	u64 pbc;
	const unsigned hdrwords = 7;
	static struct ib_header ibhdr = {
		.lrh[0] = cpu_to_be16(0xF000 | QIB_LRH_BTH),
		.lrh[1] = IB_LID_PERMISSIVE,
		.lrh[2] = cpu_to_be16(hdrwords + SIZE_OF_CRC),
		.lrh[3] = IB_LID_PERMISSIVE,
		.u.oth.bth[0] = cpu_to_be32(
			(IB_OPCODE_UD_SEND_ONLY << 24) | QIB_DEFAULT_P_KEY),
		.u.oth.bth[1] = cpu_to_be32(0),
		.u.oth.bth[2] = cpu_to_be32(0),
		.u.oth.u.ud.deth[0] = cpu_to_be32(0),
		.u.oth.u.ud.deth[1] = cpu_to_be32(0),
	};

	/*
	 * Send a dummy VL15 packet to flush the launch FIFO.
	 * This will not actually be sent since the TxeBypassIbc bit is set.
	 */
	pbc = PBC_7322_VL15_SEND |
		(((u64)ppd->hw_pidx) << (PBC_PORT_SEL_LSB + 32)) |
		(hdrwords + SIZE_OF_CRC);
	piobuf = qib_7322_getsendbuf(ppd, pbc, &bufn);
	if (!piobuf)
		return;
	writeq(pbc, piobuf);
	hdr = (u32 *) &ibhdr;
	if (dd->flags & QIB_PIO_FLUSH_WC) {
		qib_flush_wc();
		qib_pio_copy(piobuf + 2, hdr, hdrwords - 1);
		qib_flush_wc();
		__raw_writel(hdr[hdrwords - 1], piobuf + hdrwords + 1);
		qib_flush_wc();
	} else
		qib_pio_copy(piobuf + 2, hdr, hdrwords);
	qib_sendbuf_done(dd, bufn);
}

/*
 * This is called with interrupts disabled and sdma_lock held.
 */
static void qib_7322_sdma_sendctrl(struct qib_pportdata *ppd, unsigned op)
{
	struct qib_devdata *dd = ppd->dd;
	u64 set_sendctrl = 0;
	u64 clr_sendctrl = 0;

	if (op & QIB_SDMA_SENDCTRL_OP_ENABLE)
		set_sendctrl |= SYM_MASK(SendCtrl_0, SDmaEnable);
	else
		clr_sendctrl |= SYM_MASK(SendCtrl_0, SDmaEnable);

	if (op & QIB_SDMA_SENDCTRL_OP_INTENABLE)
		set_sendctrl |= SYM_MASK(SendCtrl_0, SDmaIntEnable);
	else
		clr_sendctrl |= SYM_MASK(SendCtrl_0, SDmaIntEnable);

	if (op & QIB_SDMA_SENDCTRL_OP_HALT)
		set_sendctrl |= SYM_MASK(SendCtrl_0, SDmaHalt);
	else
		clr_sendctrl |= SYM_MASK(SendCtrl_0, SDmaHalt);

	if (op & QIB_SDMA_SENDCTRL_OP_DRAIN)
		set_sendctrl |= SYM_MASK(SendCtrl_0, TxeBypassIbc) |
				SYM_MASK(SendCtrl_0, TxeAbortIbc) |
				SYM_MASK(SendCtrl_0, TxeDrainRmFifo);
	else
		clr_sendctrl |= SYM_MASK(SendCtrl_0, TxeBypassIbc) |
				SYM_MASK(SendCtrl_0, TxeAbortIbc) |
				SYM_MASK(SendCtrl_0, TxeDrainRmFifo);

	spin_lock(&dd->sendctrl_lock);

	/* If we are draining everything, block sends first */
	if (op & QIB_SDMA_SENDCTRL_OP_DRAIN) {
		ppd->p_sendctrl &= ~SYM_MASK(SendCtrl_0, SendEnable);
		qib_write_kreg_port(ppd, krp_sendctrl, ppd->p_sendctrl);
		qib_write_kreg(dd, kr_scratch, 0);
	}

	ppd->p_sendctrl |= set_sendctrl;
	ppd->p_sendctrl &= ~clr_sendctrl;

	if (op & QIB_SDMA_SENDCTRL_OP_CLEANUP)
		qib_write_kreg_port(ppd, krp_sendctrl,
				    ppd->p_sendctrl |
				    SYM_MASK(SendCtrl_0, SDmaCleanup));
	else
		qib_write_kreg_port(ppd, krp_sendctrl, ppd->p_sendctrl);
	qib_write_kreg(dd, kr_scratch, 0);

	if (op & QIB_SDMA_SENDCTRL_OP_DRAIN) {
		ppd->p_sendctrl |= SYM_MASK(SendCtrl_0, SendEnable);
		qib_write_kreg_port(ppd, krp_sendctrl, ppd->p_sendctrl);
		qib_write_kreg(dd, kr_scratch, 0);
	}

	spin_unlock(&dd->sendctrl_lock);

	if ((op & QIB_SDMA_SENDCTRL_OP_DRAIN) && ppd->dd->cspec->r1)
		flush_fifo(ppd);
}

static void qib_7322_sdma_hw_clean_up(struct qib_pportdata *ppd)
{
	__qib_sdma_process_event(ppd, qib_sdma_event_e50_hw_cleaned);
}

static void qib_sdma_7322_setlengen(struct qib_pportdata *ppd)
{
	/*
	 * Set SendDmaLenGen and clear and set
	 * the MSB of the generation count to enable generation checking
	 * and load the internal generation counter.
	 */
	qib_write_kreg_port(ppd, krp_senddmalengen, ppd->sdma_descq_cnt);
	qib_write_kreg_port(ppd, krp_senddmalengen,
			    ppd->sdma_descq_cnt |
			    (1ULL << QIB_7322_SendDmaLenGen_0_Generation_MSB));
}

/*
 * Must be called with sdma_lock held, or before init finished.
 */
static void qib_sdma_update_7322_tail(struct qib_pportdata *ppd, u16 tail)
{
	/* Commit writes to memory and advance the tail on the chip */
	wmb();
	ppd->sdma_descq_tail = tail;
	qib_write_kreg_port(ppd, krp_senddmatail, tail);
}

/*
 * This is called with interrupts disabled and sdma_lock held.
 */
static void qib_7322_sdma_hw_start_up(struct qib_pportdata *ppd)
{
	/*
	 * Drain all FIFOs.
	 * The hardware doesn't require this but we do it so that verbs
	 * and user applications don't wait for link active to send stale
	 * data.
	 */
	sendctrl_7322_mod(ppd, QIB_SENDCTRL_FLUSH);

	qib_sdma_7322_setlengen(ppd);
	qib_sdma_update_7322_tail(ppd, 0); /* Set SendDmaTail */
	ppd->sdma_head_dma[0] = 0;
	qib_7322_sdma_sendctrl(ppd,
		ppd->sdma_state.current_op | QIB_SDMA_SENDCTRL_OP_CLEANUP);
}

#define DISABLES_SDMA ( \
	QIB_E_P_SDMAHALT | \
	QIB_E_P_SDMADESCADDRMISALIGN | \
	QIB_E_P_SDMAMISSINGDW | \
	QIB_E_P_SDMADWEN | \
	QIB_E_P_SDMARPYTAG | \
	QIB_E_P_SDMA1STDESC | \
	QIB_E_P_SDMABASE | \
	QIB_E_P_SDMATAILOUTOFBOUND | \
	QIB_E_P_SDMAOUTOFBOUND | \
	QIB_E_P_SDMAGENMISMATCH)

static void sdma_7322_p_errors(struct qib_pportdata *ppd, u64 errs)
{
	unsigned long flags;
	struct qib_devdata *dd = ppd->dd;

	errs &= QIB_E_P_SDMAERRS;
	err_decode(ppd->cpspec->sdmamsgbuf, sizeof(ppd->cpspec->sdmamsgbuf),
		   errs, qib_7322p_error_msgs);

	if (errs & QIB_E_P_SDMAUNEXPDATA)
		qib_dev_err(dd, "IB%u:%u SDmaUnexpData\n", dd->unit,
			    ppd->port);

	spin_lock_irqsave(&ppd->sdma_lock, flags);

	if (errs != QIB_E_P_SDMAHALT) {
		/* SDMA errors have QIB_E_P_SDMAHALT and another bit set */
		qib_dev_porterr(dd, ppd->port,
			"SDMA %s 0x%016llx %s\n",
			qib_sdma_state_names[ppd->sdma_state.current_state],
			errs, ppd->cpspec->sdmamsgbuf);
		dump_sdma_7322_state(ppd);
	}

	switch (ppd->sdma_state.current_state) {
	case qib_sdma_state_s00_hw_down:
		break;

	case qib_sdma_state_s10_hw_start_up_wait:
		if (errs & QIB_E_P_SDMAHALT)
			__qib_sdma_process_event(ppd,
				qib_sdma_event_e20_hw_started);
		break;

	case qib_sdma_state_s20_idle:
		break;

	case qib_sdma_state_s30_sw_clean_up_wait:
		break;

	case qib_sdma_state_s40_hw_clean_up_wait:
		if (errs & QIB_E_P_SDMAHALT)
			__qib_sdma_process_event(ppd,
				qib_sdma_event_e50_hw_cleaned);
		break;

	case qib_sdma_state_s50_hw_halt_wait:
		if (errs & QIB_E_P_SDMAHALT)
			__qib_sdma_process_event(ppd,
				qib_sdma_event_e60_hw_halted);
		break;

	case qib_sdma_state_s99_running:
		__qib_sdma_process_event(ppd, qib_sdma_event_e7322_err_halted);
		__qib_sdma_process_event(ppd, qib_sdma_event_e60_hw_halted);
		break;
	}

	spin_unlock_irqrestore(&ppd->sdma_lock, flags);
}

/*
 * handle per-device errors (not per-port errors)
 */
static noinline void handle_7322_errors(struct qib_devdata *dd)
{
	char *msg;
	u64 iserr = 0;
	u64 errs;
	u64 mask;

	qib_stats.sps_errints++;
	errs = qib_read_kreg64(dd, kr_errstatus);
	if (!errs) {
		qib_devinfo(dd->pcidev,
			"device error interrupt, but no error bits set!\n");
		goto done;
	}

	/* don't report errors that are masked */
	errs &= dd->cspec->errormask;
	msg = dd->cspec->emsgbuf;

	/* do these first, they are most important */
	if (errs & QIB_E_HARDWARE) {
		*msg = '\0';
		qib_7322_handle_hwerrors(dd, msg, sizeof(dd->cspec->emsgbuf));
	}

	if (errs & QIB_E_SPKTERRS) {
		qib_disarm_7322_senderrbufs(dd->pport);
		qib_stats.sps_txerrs++;
	} else if (errs & QIB_E_INVALIDADDR)
		qib_stats.sps_txerrs++;
	else if (errs & QIB_E_ARMLAUNCH) {
		qib_stats.sps_txerrs++;
		qib_disarm_7322_senderrbufs(dd->pport);
	}
	qib_write_kreg(dd, kr_errclear, errs);

	/*
	 * The ones we mask off are handled specially below
	 * or above.  Also mask SDMADISABLED by default as it
	 * is too chatty.
	 */
	mask = QIB_E_HARDWARE;
	*msg = '\0';

	err_decode(msg, sizeof(dd->cspec->emsgbuf), errs & ~mask,
		   qib_7322error_msgs);

	/*
	 * Getting reset is a tragedy for all ports. Mark the device
	 * _and_ the ports as "offline" in way meaningful to each.
	 */
	if (errs & QIB_E_RESET) {
		int pidx;

		qib_dev_err(dd,
			"Got reset, requires re-init (unload and reload driver)\n");
		dd->flags &= ~QIB_INITTED;  /* needs re-init */
		/* mark as having had error */
		*dd->devstatusp |= QIB_STATUS_HWERROR;
		for (pidx = 0; pidx < dd->num_pports; ++pidx)
			if (dd->pport[pidx].link_speed_supported)
				*dd->pport[pidx].statusp &= ~QIB_STATUS_IB_CONF;
	}

	if (*msg && iserr)
		qib_dev_err(dd, "%s error\n", msg);

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

static void qib_error_tasklet(struct tasklet_struct *t)
{
	struct qib_devdata *dd = from_tasklet(dd, t, error_tasklet);

	handle_7322_errors(dd);
	qib_write_kreg(dd, kr_errmask, dd->cspec->errormask);
}

static void reenable_chase(struct timer_list *t)
{
	struct qib_chippport_specific *cp = from_timer(cp, t, chase_timer);
	struct qib_pportdata *ppd = cp->ppd;

	ppd->cpspec->chase_timer.expires = 0;
	qib_set_ib_7322_lstate(ppd, QLOGIC_IB_IBCC_LINKCMD_DOWN,
		QLOGIC_IB_IBCC_LINKINITCMD_POLL);
}

static void disable_chase(struct qib_pportdata *ppd, unsigned long tnow,
		u8 ibclt)
{
	ppd->cpspec->chase_end = 0;

	if (!qib_chase)
		return;

	qib_set_ib_7322_lstate(ppd, QLOGIC_IB_IBCC_LINKCMD_DOWN,
		QLOGIC_IB_IBCC_LINKINITCMD_DISABLE);
	ppd->cpspec->chase_timer.expires = jiffies + QIB_CHASE_DIS_TIME;
	add_timer(&ppd->cpspec->chase_timer);
}

static void handle_serdes_issues(struct qib_pportdata *ppd, u64 ibcst)
{
	u8 ibclt;
	unsigned long tnow;

	ibclt = (u8)SYM_FIELD(ibcst, IBCStatusA_0, LinkTrainingState);

	/*
	 * Detect and handle the state chase issue, where we can
	 * get stuck if we are unlucky on timing on both sides of
	 * the link.   If we are, we disable, set a timer, and
	 * then re-enable.
	 */
	switch (ibclt) {
	case IB_7322_LT_STATE_CFGRCVFCFG:
	case IB_7322_LT_STATE_CFGWAITRMT:
	case IB_7322_LT_STATE_TXREVLANES:
	case IB_7322_LT_STATE_CFGENH:
		tnow = jiffies;
		if (ppd->cpspec->chase_end &&
		     time_after(tnow, ppd->cpspec->chase_end))
			disable_chase(ppd, tnow, ibclt);
		else if (!ppd->cpspec->chase_end)
			ppd->cpspec->chase_end = tnow + QIB_CHASE_TIME;
		break;
	default:
		ppd->cpspec->chase_end = 0;
		break;
	}

	if (((ibclt >= IB_7322_LT_STATE_CFGTEST &&
	      ibclt <= IB_7322_LT_STATE_CFGWAITENH) ||
	     ibclt == IB_7322_LT_STATE_LINKUP) &&
	    (ibcst & SYM_MASK(IBCStatusA_0, LinkSpeedQDR))) {
		force_h1(ppd);
		ppd->cpspec->qdr_reforce = 1;
		if (!ppd->dd->cspec->r1)
			serdes_7322_los_enable(ppd, 0);
	} else if (ppd->cpspec->qdr_reforce &&
		(ibcst & SYM_MASK(IBCStatusA_0, LinkSpeedQDR)) &&
		 (ibclt == IB_7322_LT_STATE_CFGENH ||
		ibclt == IB_7322_LT_STATE_CFGIDLE ||
		ibclt == IB_7322_LT_STATE_LINKUP))
		force_h1(ppd);

	if ((IS_QMH(ppd->dd) || IS_QME(ppd->dd)) &&
	    ppd->link_speed_enabled == QIB_IB_QDR &&
	    (ibclt == IB_7322_LT_STATE_CFGTEST ||
	     ibclt == IB_7322_LT_STATE_CFGENH ||
	     (ibclt >= IB_7322_LT_STATE_POLLACTIVE &&
	      ibclt <= IB_7322_LT_STATE_SLEEPQUIET)))
		adj_tx_serdes(ppd);

	if (ibclt != IB_7322_LT_STATE_LINKUP) {
		u8 ltstate = qib_7322_phys_portstate(ibcst);
		u8 pibclt = (u8)SYM_FIELD(ppd->lastibcstat, IBCStatusA_0,
					  LinkTrainingState);
		if (!ppd->dd->cspec->r1 &&
		    pibclt == IB_7322_LT_STATE_LINKUP &&
		    ltstate != IB_PHYSPORTSTATE_LINK_ERR_RECOVER &&
		    ltstate != IB_PHYSPORTSTATE_RECOVERY_RETRAIN &&
		    ltstate != IB_PHYSPORTSTATE_RECOVERY_WAITRMT &&
		    ltstate != IB_PHYSPORTSTATE_RECOVERY_IDLE)
			/* If the link went down (but no into recovery,
			 * turn LOS back on */
			serdes_7322_los_enable(ppd, 1);
		if (!ppd->cpspec->qdr_dfe_on &&
		    ibclt <= IB_7322_LT_STATE_SLEEPQUIET) {
			ppd->cpspec->qdr_dfe_on = 1;
			ppd->cpspec->qdr_dfe_time = 0;
			/* On link down, reenable QDR adaptation */
			qib_write_kreg_port(ppd, krp_static_adapt_dis(2),
					    ppd->dd->cspec->r1 ?
					    QDR_STATIC_ADAPT_DOWN_R1 :
					    QDR_STATIC_ADAPT_DOWN);
			pr_info(
				"IB%u:%u re-enabled QDR adaptation ibclt %x\n",
				ppd->dd->unit, ppd->port, ibclt);
		}
	}
}

static int qib_7322_set_ib_cfg(struct qib_pportdata *, int, u32);

/*
 * This is per-pport error handling.
 * will likely get it's own MSIx interrupt (one for each port,
 * although just a single handler).
 */
static noinline void handle_7322_p_errors(struct qib_pportdata *ppd)
{
	char *msg;
	u64 ignore_this_time = 0, iserr = 0, errs, fmask;
	struct qib_devdata *dd = ppd->dd;

	/* do this as soon as possible */
	fmask = qib_read_kreg64(dd, kr_act_fmask);
	if (!fmask)
		check_7322_rxe_status(ppd);

	errs = qib_read_kreg_port(ppd, krp_errstatus);
	if (!errs)
		qib_devinfo(dd->pcidev,
			 "Port%d error interrupt, but no error bits set!\n",
			 ppd->port);
	if (!fmask)
		errs &= ~QIB_E_P_IBSTATUSCHANGED;
	if (!errs)
		goto done;

	msg = ppd->cpspec->epmsgbuf;
	*msg = '\0';

	if (errs & ~QIB_E_P_BITSEXTANT) {
		err_decode(msg, sizeof(ppd->cpspec->epmsgbuf),
			   errs & ~QIB_E_P_BITSEXTANT, qib_7322p_error_msgs);
		if (!*msg)
			snprintf(msg, sizeof(ppd->cpspec->epmsgbuf),
				 "no others");
		qib_dev_porterr(dd, ppd->port,
			"error interrupt with unknown errors 0x%016Lx set (and %s)\n",
			(errs & ~QIB_E_P_BITSEXTANT), msg);
		*msg = '\0';
	}

	if (errs & QIB_E_P_SHDR) {
		u64 symptom;

		/* determine cause, then write to clear */
		symptom = qib_read_kreg_port(ppd, krp_sendhdrsymptom);
		qib_write_kreg_port(ppd, krp_sendhdrsymptom, 0);
		err_decode(msg, sizeof(ppd->cpspec->epmsgbuf), symptom,
			   hdrchk_msgs);
		*msg = '\0';
		/* senderrbuf cleared in SPKTERRS below */
	}

	if (errs & QIB_E_P_SPKTERRS) {
		if ((errs & QIB_E_P_LINK_PKTERRS) &&
		    !(ppd->lflags & QIBL_LINKACTIVE)) {
			/*
			 * This can happen when trying to bring the link
			 * up, but the IB link changes state at the "wrong"
			 * time. The IB logic then complains that the packet
			 * isn't valid.  We don't want to confuse people, so
			 * we just don't print them, except at debug
			 */
			err_decode(msg, sizeof(ppd->cpspec->epmsgbuf),
				   (errs & QIB_E_P_LINK_PKTERRS),
				   qib_7322p_error_msgs);
			*msg = '\0';
			ignore_this_time = errs & QIB_E_P_LINK_PKTERRS;
		}
		qib_disarm_7322_senderrbufs(ppd);
	} else if ((errs & QIB_E_P_LINK_PKTERRS) &&
		   !(ppd->lflags & QIBL_LINKACTIVE)) {
		/*
		 * This can happen when SMA is trying to bring the link
		 * up, but the IB link changes state at the "wrong" time.
		 * The IB logic then complains that the packet isn't
		 * valid.  We don't want to confuse people, so we just
		 * don't print them, except at debug
		 */
		err_decode(msg, sizeof(ppd->cpspec->epmsgbuf), errs,
			   qib_7322p_error_msgs);
		ignore_this_time = errs & QIB_E_P_LINK_PKTERRS;
		*msg = '\0';
	}

	qib_write_kreg_port(ppd, krp_errclear, errs);

	errs &= ~ignore_this_time;
	if (!errs)
		goto done;

	if (errs & QIB_E_P_RPKTERRS)
		qib_stats.sps_rcverrs++;
	if (errs & QIB_E_P_SPKTERRS)
		qib_stats.sps_txerrs++;

	iserr = errs & ~(QIB_E_P_RPKTERRS | QIB_E_P_PKTERRS);

	if (errs & QIB_E_P_SDMAERRS)
		sdma_7322_p_errors(ppd, errs);

	if (errs & QIB_E_P_IBSTATUSCHANGED) {
		u64 ibcs;
		u8 ltstate;

		ibcs = qib_read_kreg_port(ppd, krp_ibcstatus_a);
		ltstate = qib_7322_phys_portstate(ibcs);

		if (!(ppd->lflags & QIBL_IB_AUTONEG_INPROG))
			handle_serdes_issues(ppd, ibcs);
		if (!(ppd->cpspec->ibcctrl_a &
		      SYM_MASK(IBCCtrlA_0, IBStatIntReductionEn))) {
			/*
			 * We got our interrupt, so init code should be
			 * happy and not try alternatives. Now squelch
			 * other "chatter" from link-negotiation (pre Init)
			 */
			ppd->cpspec->ibcctrl_a |=
				SYM_MASK(IBCCtrlA_0, IBStatIntReductionEn);
			qib_write_kreg_port(ppd, krp_ibcctrl_a,
					    ppd->cpspec->ibcctrl_a);
		}

		/* Update our picture of width and speed from chip */
		ppd->link_width_active =
			(ibcs & SYM_MASK(IBCStatusA_0, LinkWidthActive)) ?
			    IB_WIDTH_4X : IB_WIDTH_1X;
		ppd->link_speed_active = (ibcs & SYM_MASK(IBCStatusA_0,
			LinkSpeedQDR)) ? QIB_IB_QDR : (ibcs &
			  SYM_MASK(IBCStatusA_0, LinkSpeedActive)) ?
				   QIB_IB_DDR : QIB_IB_SDR;

		if ((ppd->lflags & QIBL_IB_LINK_DISABLED) && ltstate !=
		    IB_PHYSPORTSTATE_DISABLED)
			qib_set_ib_7322_lstate(ppd, 0,
			       QLOGIC_IB_IBCC_LINKINITCMD_DISABLE);
		else
			/*
			 * Since going into a recovery state causes the link
			 * state to go down and since recovery is transitory,
			 * it is better if we "miss" ever seeing the link
			 * training state go into recovery (i.e., ignore this
			 * transition for link state special handling purposes)
			 * without updating lastibcstat.
			 */
			if (ltstate != IB_PHYSPORTSTATE_LINK_ERR_RECOVER &&
			    ltstate != IB_PHYSPORTSTATE_RECOVERY_RETRAIN &&
			    ltstate != IB_PHYSPORTSTATE_RECOVERY_WAITRMT &&
			    ltstate != IB_PHYSPORTSTATE_RECOVERY_IDLE)
				qib_handle_e_ibstatuschanged(ppd, ibcs);
	}
	if (*msg && iserr)
		qib_dev_porterr(dd, ppd->port, "%s error\n", msg);

	if (ppd->state_wanted & ppd->lflags)
		wake_up_interruptible(&ppd->state_wait);
done:
	return;
}

/* enable/disable chip from delivering interrupts */
static void qib_7322_set_intr_state(struct qib_devdata *dd, u32 enable)
{
	if (enable) {
		if (dd->flags & QIB_BADINTR)
			return;
		qib_write_kreg(dd, kr_intmask, dd->cspec->int_enable_mask);
		/* cause any pending enabled interrupts to be re-delivered */
		qib_write_kreg(dd, kr_intclear, 0ULL);
		if (dd->cspec->num_msix_entries) {
			/* and same for MSIx */
			u64 val = qib_read_kreg64(dd, kr_intgranted);

			if (val)
				qib_write_kreg(dd, kr_intgranted, val);
		}
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
static void qib_7322_clear_freeze(struct qib_devdata *dd)
{
	int pidx;

	/* disable error interrupts, to avoid confusion */
	qib_write_kreg(dd, kr_errmask, 0ULL);

	for (pidx = 0; pidx < dd->num_pports; ++pidx)
		if (dd->pport[pidx].link_speed_supported)
			qib_write_kreg_port(dd->pport + pidx, krp_errmask,
					    0ULL);

	/* also disable interrupts; errormask is sometimes overwritten */
	qib_7322_set_intr_state(dd, 0);

	/* clear the freeze, and be sure chip saw it */
	qib_write_kreg(dd, kr_control, dd->control);
	qib_read_kreg32(dd, kr_scratch);

	/*
	 * Force new interrupt if any hwerr, error or interrupt bits are
	 * still set, and clear "safe" send packet errors related to freeze
	 * and cancelling sends.  Re-enable error interrupts before possible
	 * force of re-interrupt on pending interrupts.
	 */
	qib_write_kreg(dd, kr_hwerrclear, 0ULL);
	qib_write_kreg(dd, kr_errclear, E_SPKT_ERRS_IGNORE);
	qib_write_kreg(dd, kr_errmask, dd->cspec->errormask);
	/* We need to purge per-port errs and reset mask, too */
	for (pidx = 0; pidx < dd->num_pports; ++pidx) {
		if (!dd->pport[pidx].link_speed_supported)
			continue;
		qib_write_kreg_port(dd->pport + pidx, krp_errclear, ~0Ull);
		qib_write_kreg_port(dd->pport + pidx, krp_errmask, ~0Ull);
	}
	qib_7322_set_intr_state(dd, 1);
}

/* no error handling to speak of */
/**
 * qib_7322_handle_hwerrors - display hardware errors.
 * @dd: the qlogic_ib device
 * @msg: the output buffer
 * @msgl: the size of the output buffer
 *
 * Use same msg buffer as regular errors to avoid excessive stack
 * use.  Most hardware errors are catastrophic, but for right now,
 * we'll print them and continue.  We reuse the same message buffer as
 * qib_handle_errors() to avoid excessive stack usage.
 */
static void qib_7322_handle_hwerrors(struct qib_devdata *dd, char *msg,
				     size_t msgl)
{
	u64 hwerrs;
	u32 ctrl;
	int isfatal = 0;

	hwerrs = qib_read_kreg64(dd, kr_hwerrstatus);
	if (!hwerrs)
		goto bail;
	if (hwerrs == ~0ULL) {
		qib_dev_err(dd,
			"Read of hardware error status failed (all bits set); ignoring\n");
		goto bail;
	}
	qib_stats.sps_hwerrs++;

	/* Always clear the error status register, except BIST fail */
	qib_write_kreg(dd, kr_hwerrclear, hwerrs &
		       ~HWE_MASK(PowerOnBISTFailed));

	hwerrs &= dd->cspec->hwerrmask;

	/* no EEPROM logging, yet */

	if (hwerrs)
		qib_devinfo(dd->pcidev,
			"Hardware error: hwerr=0x%llx (cleared)\n",
			(unsigned long long) hwerrs);

	ctrl = qib_read_kreg32(dd, kr_control);
	if ((ctrl & SYM_MASK(Control, FreezeMode)) && !dd->diag_client) {
		/*
		 * No recovery yet...
		 */
		if ((hwerrs & ~HWE_MASK(LATriggered)) ||
		    dd->cspec->stay_in_freeze) {
			/*
			 * If any set that we aren't ignoring only make the
			 * complaint once, in case it's stuck or recurring,
			 * and we get here multiple times
			 * Force link down, so switch knows, and
			 * LEDs are turned off.
			 */
			if (dd->flags & QIB_INITTED)
				isfatal = 1;
		} else
			qib_7322_clear_freeze(dd);
	}

	if (hwerrs & HWE_MASK(PowerOnBISTFailed)) {
		isfatal = 1;
		strlcpy(msg,
			"[Memory BIST test failed, InfiniPath hardware unusable]",
			msgl);
		/* ignore from now on, so disable until driver reloaded */
		dd->cspec->hwerrmask &= ~HWE_MASK(PowerOnBISTFailed);
		qib_write_kreg(dd, kr_hwerrmask, dd->cspec->hwerrmask);
	}

	err_decode(msg, msgl, hwerrs, qib_7322_hwerror_msgs);

	/* Ignore esoteric PLL failures et al. */

	qib_dev_err(dd, "%s hardware error\n", msg);

	if (hwerrs &
		   (SYM_MASK(HwErrMask, SDmaMemReadErrMask_0) |
		    SYM_MASK(HwErrMask, SDmaMemReadErrMask_1))) {
		int pidx = 0;
		int err;
		unsigned long flags;
		struct qib_pportdata *ppd = dd->pport;

		for (; pidx < dd->num_pports; ++pidx, ppd++) {
			err = 0;
			if (pidx == 0 && (hwerrs &
				SYM_MASK(HwErrMask, SDmaMemReadErrMask_0)))
				err++;
			if (pidx == 1 && (hwerrs &
				SYM_MASK(HwErrMask, SDmaMemReadErrMask_1)))
				err++;
			if (err) {
				spin_lock_irqsave(&ppd->sdma_lock, flags);
				dump_sdma_7322_state(ppd);
				spin_unlock_irqrestore(&ppd->sdma_lock, flags);
			}
		}
	}

	if (isfatal && !dd->diag_client) {
		qib_dev_err(dd,
			"Fatal Hardware Error, no longer usable, SN %.16s\n",
			dd->serial);
		/*
		 * for /sys status file and user programs to print; if no
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
 * qib_7322_init_hwerrors - enable hardware errors
 * @dd: the qlogic_ib device
 *
 * now that we have finished initializing everything that might reasonably
 * cause a hardware error, and cleared those errors bits as they occur,
 * we can enable hardware errors in the mask (potentially enabling
 * freeze mode), and enable hardware errors as errors (along with
 * everything else) in errormask
 */
static void qib_7322_init_hwerrors(struct qib_devdata *dd)
{
	int pidx;
	u64 extsval;

	extsval = qib_read_kreg64(dd, kr_extstatus);
	if (!(extsval & (QIB_EXTS_MEMBIST_DISABLED |
			 QIB_EXTS_MEMBIST_ENDTEST)))
		qib_dev_err(dd, "MemBIST did not complete!\n");

	/* never clear BIST failure, so reported on each driver load */
	qib_write_kreg(dd, kr_hwerrclear, ~HWE_MASK(PowerOnBISTFailed));
	qib_write_kreg(dd, kr_hwerrmask, dd->cspec->hwerrmask);

	/* clear all */
	qib_write_kreg(dd, kr_errclear, ~0ULL);
	/* enable errors that are masked, at least this first time. */
	qib_write_kreg(dd, kr_errmask, ~0ULL);
	dd->cspec->errormask = qib_read_kreg64(dd, kr_errmask);
	for (pidx = 0; pidx < dd->num_pports; ++pidx)
		if (dd->pport[pidx].link_speed_supported)
			qib_write_kreg_port(dd->pport + pidx, krp_errmask,
					    ~0ULL);
}

/*
 * Disable and enable the armlaunch error.  Used for PIO bandwidth testing
 * on chips that are count-based, rather than trigger-based.  There is no
 * reference counting, but that's also fine, given the intended use.
 * Only chip-specific because it's all register accesses
 */
static void qib_set_7322_armlaunch(struct qib_devdata *dd, u32 enable)
{
	if (enable) {
		qib_write_kreg(dd, kr_errclear, QIB_E_SPIOARMLAUNCH);
		dd->cspec->errormask |= QIB_E_SPIOARMLAUNCH;
	} else
		dd->cspec->errormask &= ~QIB_E_SPIOARMLAUNCH;
	qib_write_kreg(dd, kr_errmask, dd->cspec->errormask);
}

/*
 * Formerly took parameter <which> in pre-shifted,
 * pre-merged form with LinkCmd and LinkInitCmd
 * together, and assuming the zero was NOP.
 */
static void qib_set_ib_7322_lstate(struct qib_pportdata *ppd, u16 linkcmd,
				   u16 linitcmd)
{
	u64 mod_wd;
	struct qib_devdata *dd = ppd->dd;
	unsigned long flags;

	if (linitcmd == QLOGIC_IB_IBCC_LINKINITCMD_DISABLE) {
		/*
		 * If we are told to disable, note that so link-recovery
		 * code does not attempt to bring us back up.
		 * Also reset everything that we can, so we start
		 * completely clean when re-enabled (before we
		 * actually issue the disable to the IBC)
		 */
		qib_7322_mini_pcs_reset(ppd);
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
		/*
		 * Clear status change interrupt reduction so the
		 * new state is seen.
		 */
		ppd->cpspec->ibcctrl_a &=
			~SYM_MASK(IBCCtrlA_0, IBStatIntReductionEn);
	}

	mod_wd = (linkcmd << IBA7322_IBCC_LINKCMD_SHIFT) |
		(linitcmd << QLOGIC_IB_IBCC_LINKINITCMD_SHIFT);

	qib_write_kreg_port(ppd, krp_ibcctrl_a, ppd->cpspec->ibcctrl_a |
			    mod_wd);
	/* write to chip to prevent back-to-back writes of ibc reg */
	qib_write_kreg(dd, kr_scratch, 0);

}

/*
 * The total RCV buffer memory is 64KB, used for both ports, and is
 * in units of 64 bytes (same as IB flow control credit unit).
 * The consumedVL unit in the same registers are in 32 byte units!
 * So, a VL15 packet needs 4.50 IB credits, and 9 rx buffer chunks,
 * and we can therefore allocate just 9 IB credits for 2 VL15 packets
 * in krp_rxcreditvl15, rather than 10.
 */
#define RCV_BUF_UNITSZ 64
#define NUM_RCV_BUF_UNITS(dd) ((64 * 1024) / (RCV_BUF_UNITSZ * dd->num_pports))

static void set_vls(struct qib_pportdata *ppd)
{
	int i, numvls, totcred, cred_vl, vl0extra;
	struct qib_devdata *dd = ppd->dd;
	u64 val;

	numvls = qib_num_vls(ppd->vls_operational);

	/*
	 * Set up per-VL credits. Below is kluge based on these assumptions:
	 * 1) port is disabled at the time early_init is called.
	 * 2) give VL15 17 credits, for two max-plausible packets.
	 * 3) Give VL0-N the rest, with any rounding excess used for VL0
	 */
	/* 2 VL15 packets @ 288 bytes each (including IB headers) */
	totcred = NUM_RCV_BUF_UNITS(dd);
	cred_vl = (2 * 288 + RCV_BUF_UNITSZ - 1) / RCV_BUF_UNITSZ;
	totcred -= cred_vl;
	qib_write_kreg_port(ppd, krp_rxcreditvl15, (u64) cred_vl);
	cred_vl = totcred / numvls;
	vl0extra = totcred - cred_vl * numvls;
	qib_write_kreg_port(ppd, krp_rxcreditvl0, cred_vl + vl0extra);
	for (i = 1; i < numvls; i++)
		qib_write_kreg_port(ppd, krp_rxcreditvl0 + i, cred_vl);
	for (; i < 8; i++) /* no buffer space for other VLs */
		qib_write_kreg_port(ppd, krp_rxcreditvl0 + i, 0);

	/* Notify IBC that credits need to be recalculated */
	val = qib_read_kreg_port(ppd, krp_ibsdtestiftx);
	val |= SYM_MASK(IB_SDTEST_IF_TX_0, CREDIT_CHANGE);
	qib_write_kreg_port(ppd, krp_ibsdtestiftx, val);
	qib_write_kreg(dd, kr_scratch, 0ULL);
	val &= ~SYM_MASK(IB_SDTEST_IF_TX_0, CREDIT_CHANGE);
	qib_write_kreg_port(ppd, krp_ibsdtestiftx, val);

	for (i = 0; i < numvls; i++)
		val = qib_read_kreg_port(ppd, krp_rxcreditvl0 + i);
	val = qib_read_kreg_port(ppd, krp_rxcreditvl15);

	/* Change the number of operational VLs */
	ppd->cpspec->ibcctrl_a = (ppd->cpspec->ibcctrl_a &
				~SYM_MASK(IBCCtrlA_0, NumVLane)) |
		((u64)(numvls - 1) << SYM_LSB(IBCCtrlA_0, NumVLane));
	qib_write_kreg_port(ppd, krp_ibcctrl_a, ppd->cpspec->ibcctrl_a);
	qib_write_kreg(dd, kr_scratch, 0ULL);
}

/*
 * The code that deals with actual SerDes is in serdes_7322_init().
 * Compared to the code for iba7220, it is minimal.
 */
static int serdes_7322_init(struct qib_pportdata *ppd);

/**
 * qib_7322_bringup_serdes - bring up the serdes
 * @ppd: physical port on the qlogic_ib device
 */
static int qib_7322_bringup_serdes(struct qib_pportdata *ppd)
{
	struct qib_devdata *dd = ppd->dd;
	u64 val, guid, ibc;
	unsigned long flags;

	/*
	 * SerDes model not in Pd, but still need to
	 * set up much of IBCCtrl and IBCDDRCtrl; move elsewhere
	 * eventually.
	 */
	/* Put IBC in reset, sends disabled (should be in reset already) */
	ppd->cpspec->ibcctrl_a &= ~SYM_MASK(IBCCtrlA_0, IBLinkEn);
	qib_write_kreg_port(ppd, krp_ibcctrl_a, ppd->cpspec->ibcctrl_a);
	qib_write_kreg(dd, kr_scratch, 0ULL);

	/* ensure previous Tx parameters are not still forced */
	qib_write_kreg_port(ppd, krp_tx_deemph_override,
		SYM_MASK(IBSD_TX_DEEMPHASIS_OVERRIDE_0,
		reset_tx_deemphasis_override));

	if (qib_compat_ddr_negotiate) {
		ppd->cpspec->ibdeltainprog = 1;
		ppd->cpspec->ibsymsnap = read_7322_creg32_port(ppd,
						crp_ibsymbolerr);
		ppd->cpspec->iblnkerrsnap = read_7322_creg32_port(ppd,
						crp_iblinkerrrecov);
	}

	/* flowcontrolwatermark is in units of KBytes */
	ibc = 0x5ULL << SYM_LSB(IBCCtrlA_0, FlowCtrlWaterMark);
	/*
	 * Flow control is sent this often, even if no changes in
	 * buffer space occur.  Units are 128ns for this chip.
	 * Set to 3usec.
	 */
	ibc |= 24ULL << SYM_LSB(IBCCtrlA_0, FlowCtrlPeriod);
	/* max error tolerance */
	ibc |= 0xfULL << SYM_LSB(IBCCtrlA_0, PhyerrThreshold);
	/* IB credit flow control. */
	ibc |= 0xfULL << SYM_LSB(IBCCtrlA_0, OverrunThreshold);
	/*
	 * set initial max size pkt IBC will send, including ICRC; it's the
	 * PIO buffer size in dwords, less 1; also see qib_set_mtu()
	 */
	ibc |= ((u64)(ppd->ibmaxlen >> 2) + 1) <<
		SYM_LSB(IBCCtrlA_0, MaxPktLen);
	ppd->cpspec->ibcctrl_a = ibc; /* without linkcmd or linkinitcmd! */

	/*
	 * Reset the PCS interface to the serdes (and also ibc, which is still
	 * in reset from above).  Writes new value of ibcctrl_a as last step.
	 */
	qib_7322_mini_pcs_reset(ppd);

	if (!ppd->cpspec->ibcctrl_b) {
		unsigned lse = ppd->link_speed_enabled;

		/*
		 * Not on re-init after reset, establish shadow
		 * and force initial config.
		 */
		ppd->cpspec->ibcctrl_b = qib_read_kreg_port(ppd,
							     krp_ibcctrl_b);
		ppd->cpspec->ibcctrl_b &= ~(IBA7322_IBC_SPEED_QDR |
				IBA7322_IBC_SPEED_DDR |
				IBA7322_IBC_SPEED_SDR |
				IBA7322_IBC_WIDTH_AUTONEG |
				SYM_MASK(IBCCtrlB_0, IB_LANE_REV_SUPPORTED));
		if (lse & (lse - 1)) /* Muliple speeds enabled */
			ppd->cpspec->ibcctrl_b |=
				(lse << IBA7322_IBC_SPEED_LSB) |
				IBA7322_IBC_IBTA_1_2_MASK |
				IBA7322_IBC_MAX_SPEED_MASK;
		else
			ppd->cpspec->ibcctrl_b |= (lse == QIB_IB_QDR) ?
				IBA7322_IBC_SPEED_QDR |
				 IBA7322_IBC_IBTA_1_2_MASK :
				(lse == QIB_IB_DDR) ?
					IBA7322_IBC_SPEED_DDR :
					IBA7322_IBC_SPEED_SDR;
		if ((ppd->link_width_enabled & (IB_WIDTH_1X | IB_WIDTH_4X)) ==
		    (IB_WIDTH_1X | IB_WIDTH_4X))
			ppd->cpspec->ibcctrl_b |= IBA7322_IBC_WIDTH_AUTONEG;
		else
			ppd->cpspec->ibcctrl_b |=
				ppd->link_width_enabled == IB_WIDTH_4X ?
				IBA7322_IBC_WIDTH_4X_ONLY :
				IBA7322_IBC_WIDTH_1X_ONLY;

		/* always enable these on driver reload, not sticky */
		ppd->cpspec->ibcctrl_b |= (IBA7322_IBC_RXPOL_MASK |
			IBA7322_IBC_HRTBT_MASK);
	}
	qib_write_kreg_port(ppd, krp_ibcctrl_b, ppd->cpspec->ibcctrl_b);

	/* setup so we have more time at CFGTEST to change H1 */
	val = qib_read_kreg_port(ppd, krp_ibcctrl_c);
	val &= ~SYM_MASK(IBCCtrlC_0, IB_FRONT_PORCH);
	val |= 0xfULL << SYM_LSB(IBCCtrlC_0, IB_FRONT_PORCH);
	qib_write_kreg_port(ppd, krp_ibcctrl_c, val);

	serdes_7322_init(ppd);

	guid = be64_to_cpu(ppd->guid);
	if (!guid) {
		if (dd->base_guid)
			guid = be64_to_cpu(dd->base_guid) + ppd->port - 1;
		ppd->guid = cpu_to_be64(guid);
	}

	qib_write_kreg_port(ppd, krp_hrtbt_guid, guid);
	/* write to chip to prevent back-to-back writes of ibc reg */
	qib_write_kreg(dd, kr_scratch, 0);

	/* Enable port */
	ppd->cpspec->ibcctrl_a |= SYM_MASK(IBCCtrlA_0, IBLinkEn);
	set_vls(ppd);

	/* initially come up DISABLED, without sending anything. */
	val = ppd->cpspec->ibcctrl_a | (QLOGIC_IB_IBCC_LINKINITCMD_DISABLE <<
					QLOGIC_IB_IBCC_LINKINITCMD_SHIFT);
	qib_write_kreg_port(ppd, krp_ibcctrl_a, val);
	qib_write_kreg(dd, kr_scratch, 0ULL);
	/* clear the linkinit cmds */
	ppd->cpspec->ibcctrl_a = val & ~SYM_MASK(IBCCtrlA_0, LinkInitCmd);

	/* be paranoid against later code motion, etc. */
	spin_lock_irqsave(&dd->cspec->rcvmod_lock, flags);
	ppd->p_rcvctrl |= SYM_MASK(RcvCtrl_0, RcvIBPortEnable);
	qib_write_kreg_port(ppd, krp_rcvctrl, ppd->p_rcvctrl);
	spin_unlock_irqrestore(&dd->cspec->rcvmod_lock, flags);

	/* Also enable IBSTATUSCHG interrupt.  */
	val = qib_read_kreg_port(ppd, krp_errmask);
	qib_write_kreg_port(ppd, krp_errmask,
		val | ERR_MASK_N(IBStatusChanged));

	/* Always zero until we start messing with SerDes for real */
	return 0;
}

/**
 * qib_7322_mini_quiet_serdes - set serdes to txidle
 * @ppd: the qlogic_ib device
 * Called when driver is being unloaded
 */
static void qib_7322_mini_quiet_serdes(struct qib_pportdata *ppd)
{
	u64 val;
	unsigned long flags;

	qib_set_ib_7322_lstate(ppd, 0, QLOGIC_IB_IBCC_LINKINITCMD_DISABLE);

	spin_lock_irqsave(&ppd->lflags_lock, flags);
	ppd->lflags &= ~QIBL_IB_AUTONEG_INPROG;
	spin_unlock_irqrestore(&ppd->lflags_lock, flags);
	wake_up(&ppd->cpspec->autoneg_wait);
	cancel_delayed_work_sync(&ppd->cpspec->autoneg_work);
	if (ppd->dd->cspec->r1)
		cancel_delayed_work_sync(&ppd->cpspec->ipg_work);

	ppd->cpspec->chase_end = 0;
	if (ppd->cpspec->chase_timer.function) /* if initted */
		del_timer_sync(&ppd->cpspec->chase_timer);

	/*
	 * Despite the name, actually disables IBC as well. Do it when
	 * we are as sure as possible that no more packets can be
	 * received, following the down and the PCS reset.
	 * The actual disabling happens in qib_7322_mini_pci_reset(),
	 * along with the PCS being reset.
	 */
	ppd->cpspec->ibcctrl_a &= ~SYM_MASK(IBCCtrlA_0, IBLinkEn);
	qib_7322_mini_pcs_reset(ppd);

	/*
	 * Update the adjusted counters so the adjustment persists
	 * across driver reload.
	 */
	if (ppd->cpspec->ibsymdelta || ppd->cpspec->iblnkerrdelta ||
	    ppd->cpspec->ibdeltainprog || ppd->cpspec->iblnkdowndelta) {
		struct qib_devdata *dd = ppd->dd;
		u64 diagc;

		/* enable counter writes */
		diagc = qib_read_kreg64(dd, kr_hwdiagctrl);
		qib_write_kreg(dd, kr_hwdiagctrl,
			       diagc | SYM_MASK(HwDiagCtrl, CounterWrEnable));

		if (ppd->cpspec->ibsymdelta || ppd->cpspec->ibdeltainprog) {
			val = read_7322_creg32_port(ppd, crp_ibsymbolerr);
			if (ppd->cpspec->ibdeltainprog)
				val -= val - ppd->cpspec->ibsymsnap;
			val -= ppd->cpspec->ibsymdelta;
			write_7322_creg_port(ppd, crp_ibsymbolerr, val);
		}
		if (ppd->cpspec->iblnkerrdelta || ppd->cpspec->ibdeltainprog) {
			val = read_7322_creg32_port(ppd, crp_iblinkerrrecov);
			if (ppd->cpspec->ibdeltainprog)
				val -= val - ppd->cpspec->iblnkerrsnap;
			val -= ppd->cpspec->iblnkerrdelta;
			write_7322_creg_port(ppd, crp_iblinkerrrecov, val);
		}
		if (ppd->cpspec->iblnkdowndelta) {
			val = read_7322_creg32_port(ppd, crp_iblinkdown);
			val += ppd->cpspec->iblnkdowndelta;
			write_7322_creg_port(ppd, crp_iblinkdown, val);
		}
		/*
		 * No need to save ibmalfdelta since IB perfcounters
		 * are cleared on driver reload.
		 */

		/* and disable counter writes */
		qib_write_kreg(dd, kr_hwdiagctrl, diagc);
	}
}

/**
 * qib_setup_7322_setextled - set the state of the two external LEDs
 * @ppd: physical port on the qlogic_ib device
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
 */
static void qib_setup_7322_setextled(struct qib_pportdata *ppd, u32 on)
{
	struct qib_devdata *dd = ppd->dd;
	u64 extctl, ledblink = 0, val;
	unsigned long flags;
	int yel, grn;

	/*
	 * The diags use the LED to indicate diag info, so we leave
	 * the external LED alone when the diags are running.
	 */
	if (dd->diag_client)
		return;

	/* Allow override of LED display for, e.g. Locating system in rack */
	if (ppd->led_override) {
		grn = (ppd->led_override & QIB_LED_PHYS);
		yel = (ppd->led_override & QIB_LED_LOG);
	} else if (on) {
		val = qib_read_kreg_port(ppd, krp_ibcstatus_a);
		grn = qib_7322_phys_portstate(val) ==
			IB_PHYSPORTSTATE_LINKUP;
		yel = qib_7322_iblink_state(val) == IB_PORT_ACTIVE;
	} else {
		grn = 0;
		yel = 0;
	}

	spin_lock_irqsave(&dd->cspec->gpio_lock, flags);
	extctl = dd->cspec->extctrl & (ppd->port == 1 ?
		~ExtLED_IB1_MASK : ~ExtLED_IB2_MASK);
	if (grn) {
		extctl |= ppd->port == 1 ? ExtLED_IB1_GRN : ExtLED_IB2_GRN;
		/*
		 * Counts are in chip clock (4ns) periods.
		 * This is 1/16 sec (66.6ms) on,
		 * 3/16 sec (187.5 ms) off, with packets rcvd.
		 */
		ledblink = ((66600 * 1000UL / 4) << IBA7322_LEDBLINK_ON_SHIFT) |
			((187500 * 1000UL / 4) << IBA7322_LEDBLINK_OFF_SHIFT);
	}
	if (yel)
		extctl |= ppd->port == 1 ? ExtLED_IB1_YEL : ExtLED_IB2_YEL;
	dd->cspec->extctrl = extctl;
	qib_write_kreg(dd, kr_extctrl, dd->cspec->extctrl);
	spin_unlock_irqrestore(&dd->cspec->gpio_lock, flags);

	if (ledblink) /* blink the LED on packet receive */
		qib_write_kreg_port(ppd, krp_rcvpktledcnt, ledblink);
}

#ifdef CONFIG_INFINIBAND_QIB_DCA

static int qib_7322_notify_dca(struct qib_devdata *dd, unsigned long event)
{
	switch (event) {
	case DCA_PROVIDER_ADD:
		if (dd->flags & QIB_DCA_ENABLED)
			break;
		if (!dca_add_requester(&dd->pcidev->dev)) {
			qib_devinfo(dd->pcidev, "DCA enabled\n");
			dd->flags |= QIB_DCA_ENABLED;
			qib_setup_dca(dd);
		}
		break;
	case DCA_PROVIDER_REMOVE:
		if (dd->flags & QIB_DCA_ENABLED) {
			dca_remove_requester(&dd->pcidev->dev);
			dd->flags &= ~QIB_DCA_ENABLED;
			dd->cspec->dca_ctrl = 0;
			qib_write_kreg(dd, KREG_IDX(DCACtrlA),
				dd->cspec->dca_ctrl);
		}
		break;
	}
	return 0;
}

static void qib_update_rhdrq_dca(struct qib_ctxtdata *rcd, int cpu)
{
	struct qib_devdata *dd = rcd->dd;
	struct qib_chip_specific *cspec = dd->cspec;

	if (!(dd->flags & QIB_DCA_ENABLED))
		return;
	if (cspec->rhdr_cpu[rcd->ctxt] != cpu) {
		const struct dca_reg_map *rmp;

		cspec->rhdr_cpu[rcd->ctxt] = cpu;
		rmp = &dca_rcvhdr_reg_map[rcd->ctxt];
		cspec->dca_rcvhdr_ctrl[rmp->shadow_inx] &= rmp->mask;
		cspec->dca_rcvhdr_ctrl[rmp->shadow_inx] |=
			(u64) dca3_get_tag(&dd->pcidev->dev, cpu) << rmp->lsb;
		qib_devinfo(dd->pcidev,
			"Ctxt %d cpu %d dca %llx\n", rcd->ctxt, cpu,
			(long long) cspec->dca_rcvhdr_ctrl[rmp->shadow_inx]);
		qib_write_kreg(dd, rmp->regno,
			       cspec->dca_rcvhdr_ctrl[rmp->shadow_inx]);
		cspec->dca_ctrl |= SYM_MASK(DCACtrlA, RcvHdrqDCAEnable);
		qib_write_kreg(dd, KREG_IDX(DCACtrlA), cspec->dca_ctrl);
	}
}

static void qib_update_sdma_dca(struct qib_pportdata *ppd, int cpu)
{
	struct qib_devdata *dd = ppd->dd;
	struct qib_chip_specific *cspec = dd->cspec;
	unsigned pidx = ppd->port - 1;

	if (!(dd->flags & QIB_DCA_ENABLED))
		return;
	if (cspec->sdma_cpu[pidx] != cpu) {
		cspec->sdma_cpu[pidx] = cpu;
		cspec->dca_rcvhdr_ctrl[4] &= ~(ppd->hw_pidx ?
			SYM_MASK(DCACtrlF, SendDma1DCAOPH) :
			SYM_MASK(DCACtrlF, SendDma0DCAOPH));
		cspec->dca_rcvhdr_ctrl[4] |=
			(u64) dca3_get_tag(&dd->pcidev->dev, cpu) <<
				(ppd->hw_pidx ?
					SYM_LSB(DCACtrlF, SendDma1DCAOPH) :
					SYM_LSB(DCACtrlF, SendDma0DCAOPH));
		qib_devinfo(dd->pcidev,
			"sdma %d cpu %d dca %llx\n", ppd->hw_pidx, cpu,
			(long long) cspec->dca_rcvhdr_ctrl[4]);
		qib_write_kreg(dd, KREG_IDX(DCACtrlF),
			       cspec->dca_rcvhdr_ctrl[4]);
		cspec->dca_ctrl |= ppd->hw_pidx ?
			SYM_MASK(DCACtrlA, SendDMAHead1DCAEnable) :
			SYM_MASK(DCACtrlA, SendDMAHead0DCAEnable);
		qib_write_kreg(dd, KREG_IDX(DCACtrlA), cspec->dca_ctrl);
	}
}

static void qib_setup_dca(struct qib_devdata *dd)
{
	struct qib_chip_specific *cspec = dd->cspec;
	int i;

	for (i = 0; i < ARRAY_SIZE(cspec->rhdr_cpu); i++)
		cspec->rhdr_cpu[i] = -1;
	for (i = 0; i < ARRAY_SIZE(cspec->sdma_cpu); i++)
		cspec->sdma_cpu[i] = -1;
	cspec->dca_rcvhdr_ctrl[0] =
		(1ULL << SYM_LSB(DCACtrlB, RcvHdrq0DCAXfrCnt)) |
		(1ULL << SYM_LSB(DCACtrlB, RcvHdrq1DCAXfrCnt)) |
		(1ULL << SYM_LSB(DCACtrlB, RcvHdrq2DCAXfrCnt)) |
		(1ULL << SYM_LSB(DCACtrlB, RcvHdrq3DCAXfrCnt));
	cspec->dca_rcvhdr_ctrl[1] =
		(1ULL << SYM_LSB(DCACtrlC, RcvHdrq4DCAXfrCnt)) |
		(1ULL << SYM_LSB(DCACtrlC, RcvHdrq5DCAXfrCnt)) |
		(1ULL << SYM_LSB(DCACtrlC, RcvHdrq6DCAXfrCnt)) |
		(1ULL << SYM_LSB(DCACtrlC, RcvHdrq7DCAXfrCnt));
	cspec->dca_rcvhdr_ctrl[2] =
		(1ULL << SYM_LSB(DCACtrlD, RcvHdrq8DCAXfrCnt)) |
		(1ULL << SYM_LSB(DCACtrlD, RcvHdrq9DCAXfrCnt)) |
		(1ULL << SYM_LSB(DCACtrlD, RcvHdrq10DCAXfrCnt)) |
		(1ULL << SYM_LSB(DCACtrlD, RcvHdrq11DCAXfrCnt));
	cspec->dca_rcvhdr_ctrl[3] =
		(1ULL << SYM_LSB(DCACtrlE, RcvHdrq12DCAXfrCnt)) |
		(1ULL << SYM_LSB(DCACtrlE, RcvHdrq13DCAXfrCnt)) |
		(1ULL << SYM_LSB(DCACtrlE, RcvHdrq14DCAXfrCnt)) |
		(1ULL << SYM_LSB(DCACtrlE, RcvHdrq15DCAXfrCnt));
	cspec->dca_rcvhdr_ctrl[4] =
		(1ULL << SYM_LSB(DCACtrlF, RcvHdrq16DCAXfrCnt)) |
		(1ULL << SYM_LSB(DCACtrlF, RcvHdrq17DCAXfrCnt));
	for (i = 0; i < ARRAY_SIZE(cspec->sdma_cpu); i++)
		qib_write_kreg(dd, KREG_IDX(DCACtrlB) + i,
			       cspec->dca_rcvhdr_ctrl[i]);
	for (i = 0; i < cspec->num_msix_entries; i++)
		setup_dca_notifier(dd, i);
}

static void qib_irq_notifier_notify(struct irq_affinity_notify *notify,
			     const cpumask_t *mask)
{
	struct qib_irq_notify *n =
		container_of(notify, struct qib_irq_notify, notify);
	int cpu = cpumask_first(mask);

	if (n->rcv) {
		struct qib_ctxtdata *rcd = (struct qib_ctxtdata *)n->arg;

		qib_update_rhdrq_dca(rcd, cpu);
	} else {
		struct qib_pportdata *ppd = (struct qib_pportdata *)n->arg;

		qib_update_sdma_dca(ppd, cpu);
	}
}

static void qib_irq_notifier_release(struct kref *ref)
{
	struct qib_irq_notify *n =
		container_of(ref, struct qib_irq_notify, notify.kref);
	struct qib_devdata *dd;

	if (n->rcv) {
		struct qib_ctxtdata *rcd = (struct qib_ctxtdata *)n->arg;

		dd = rcd->dd;
	} else {
		struct qib_pportdata *ppd = (struct qib_pportdata *)n->arg;

		dd = ppd->dd;
	}
	qib_devinfo(dd->pcidev,
		"release on HCA notify 0x%p n 0x%p\n", ref, n);
	kfree(n);
}
#endif

static void qib_7322_free_irq(struct qib_devdata *dd)
{
	u64 intgranted;
	int i;

	dd->cspec->main_int_mask = ~0ULL;

	for (i = 0; i < dd->cspec->num_msix_entries; i++) {
		/* only free IRQs that were allocated */
		if (dd->cspec->msix_entries[i].arg) {
#ifdef CONFIG_INFINIBAND_QIB_DCA
			reset_dca_notifier(dd, i);
#endif
			irq_set_affinity_hint(pci_irq_vector(dd->pcidev, i),
					      NULL);
			free_cpumask_var(dd->cspec->msix_entries[i].mask);
			pci_free_irq(dd->pcidev, i,
				     dd->cspec->msix_entries[i].arg);
		}
	}

	/* If num_msix_entries was 0, disable the INTx IRQ */
	if (!dd->cspec->num_msix_entries)
		pci_free_irq(dd->pcidev, 0, dd);
	else
		dd->cspec->num_msix_entries = 0;

	pci_free_irq_vectors(dd->pcidev);

	/* make sure no MSIx interrupts are left pending */
	intgranted = qib_read_kreg64(dd, kr_intgranted);
	if (intgranted)
		qib_write_kreg(dd, kr_intgranted, intgranted);
}

static void qib_setup_7322_cleanup(struct qib_devdata *dd)
{
	int i;

#ifdef CONFIG_INFINIBAND_QIB_DCA
	if (dd->flags & QIB_DCA_ENABLED) {
		dca_remove_requester(&dd->pcidev->dev);
		dd->flags &= ~QIB_DCA_ENABLED;
		dd->cspec->dca_ctrl = 0;
		qib_write_kreg(dd, KREG_IDX(DCACtrlA), dd->cspec->dca_ctrl);
	}
#endif

	qib_7322_free_irq(dd);
	kfree(dd->cspec->cntrs);
	kfree(dd->cspec->sendchkenable);
	kfree(dd->cspec->sendgrhchk);
	kfree(dd->cspec->sendibchk);
	kfree(dd->cspec->msix_entries);
	for (i = 0; i < dd->num_pports; i++) {
		unsigned long flags;
		u32 mask = QSFP_GPIO_MOD_PRS_N |
			(QSFP_GPIO_MOD_PRS_N << QSFP_GPIO_PORT2_SHIFT);

		kfree(dd->pport[i].cpspec->portcntrs);
		if (dd->flags & QIB_HAS_QSFP) {
			spin_lock_irqsave(&dd->cspec->gpio_lock, flags);
			dd->cspec->gpio_mask &= ~mask;
			qib_write_kreg(dd, kr_gpio_mask, dd->cspec->gpio_mask);
			spin_unlock_irqrestore(&dd->cspec->gpio_lock, flags);
		}
	}
}

/* handle SDMA interrupts */
static void sdma_7322_intr(struct qib_devdata *dd, u64 istat)
{
	struct qib_pportdata *ppd0 = &dd->pport[0];
	struct qib_pportdata *ppd1 = &dd->pport[1];
	u64 intr0 = istat & (INT_MASK_P(SDma, 0) |
		INT_MASK_P(SDmaIdle, 0) | INT_MASK_P(SDmaProgress, 0));
	u64 intr1 = istat & (INT_MASK_P(SDma, 1) |
		INT_MASK_P(SDmaIdle, 1) | INT_MASK_P(SDmaProgress, 1));

	if (intr0)
		qib_sdma_intr(ppd0);
	if (intr1)
		qib_sdma_intr(ppd1);

	if (istat & INT_MASK_PM(SDmaCleanupDone, 0))
		qib_sdma_process_event(ppd0, qib_sdma_event_e20_hw_started);
	if (istat & INT_MASK_PM(SDmaCleanupDone, 1))
		qib_sdma_process_event(ppd1, qib_sdma_event_e20_hw_started);
}

/*
 * Set or clear the Send buffer available interrupt enable bit.
 */
static void qib_wantpiobuf_7322_intr(struct qib_devdata *dd, u32 needint)
{
	unsigned long flags;

	spin_lock_irqsave(&dd->sendctrl_lock, flags);
	if (needint)
		dd->sendctrl |= SYM_MASK(SendCtrl, SendIntBufAvail);
	else
		dd->sendctrl &= ~SYM_MASK(SendCtrl, SendIntBufAvail);
	qib_write_kreg(dd, kr_sendctrl, dd->sendctrl);
	qib_write_kreg(dd, kr_scratch, 0ULL);
	spin_unlock_irqrestore(&dd->sendctrl_lock, flags);
}

/*
 * Somehow got an interrupt with reserved bits set in interrupt status.
 * Print a message so we know it happened, then clear them.
 * keep mainline interrupt handler cache-friendly
 */
static noinline void unknown_7322_ibits(struct qib_devdata *dd, u64 istat)
{
	u64 kills;
	char msg[128];

	kills = istat & ~QIB_I_BITSEXTANT;
	qib_dev_err(dd,
		"Clearing reserved interrupt(s) 0x%016llx: %s\n",
		(unsigned long long) kills, msg);
	qib_write_kreg(dd, kr_intmask, (dd->cspec->int_enable_mask & ~kills));
}

/* keep mainline interrupt handler cache-friendly */
static noinline void unknown_7322_gpio_intr(struct qib_devdata *dd)
{
	u32 gpiostatus;
	int handled = 0;
	int pidx;

	/*
	 * Boards for this chip currently don't use GPIO interrupts,
	 * so clear by writing GPIOstatus to GPIOclear, and complain
	 * to developer.  To avoid endless repeats, clear
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
	/*
	 * Check for QSFP MOD_PRS changes
	 * only works for single port if IB1 != pidx1
	 */
	for (pidx = 0; pidx < dd->num_pports && (dd->flags & QIB_HAS_QSFP);
	     ++pidx) {
		struct qib_pportdata *ppd;
		struct qib_qsfp_data *qd;
		u32 mask;

		if (!dd->pport[pidx].link_speed_supported)
			continue;
		mask = QSFP_GPIO_MOD_PRS_N;
		ppd = dd->pport + pidx;
		mask <<= (QSFP_GPIO_PORT2_SHIFT * ppd->hw_pidx);
		if (gpiostatus & dd->cspec->gpio_mask & mask) {
			u64 pins;

			qd = &ppd->cpspec->qsfp_data;
			gpiostatus &= ~mask;
			pins = qib_read_kreg64(dd, kr_extstatus);
			pins >>= SYM_LSB(EXTStatus, GPIOIn);
			if (!(pins & mask)) {
				++handled;
				qd->t_insert = jiffies;
				queue_work(ib_wq, &qd->work);
			}
		}
	}

	if (gpiostatus && !handled) {
		const u32 mask = qib_read_kreg32(dd, kr_gpio_mask);
		u32 gpio_irq = mask & gpiostatus;

		/*
		 * Clear any troublemakers, and update chip from shadow
		 */
		dd->cspec->gpio_mask &= ~gpio_irq;
		qib_write_kreg(dd, kr_gpio_mask, dd->cspec->gpio_mask);
	}
}

/*
 * Handle errors and unusual events first, separate function
 * to improve cache hits for fast path interrupt handling.
 */
static noinline void unlikely_7322_intr(struct qib_devdata *dd, u64 istat)
{
	if (istat & ~QIB_I_BITSEXTANT)
		unknown_7322_ibits(dd, istat);
	if (istat & QIB_I_GPIO)
		unknown_7322_gpio_intr(dd);
	if (istat & QIB_I_C_ERROR) {
		qib_write_kreg(dd, kr_errmask, 0ULL);
		tasklet_schedule(&dd->error_tasklet);
	}
	if (istat & INT_MASK_P(Err, 0) && dd->rcd[0])
		handle_7322_p_errors(dd->rcd[0]->ppd);
	if (istat & INT_MASK_P(Err, 1) && dd->rcd[1])
		handle_7322_p_errors(dd->rcd[1]->ppd);
}

/*
 * Dynamically adjust the rcv int timeout for a context based on incoming
 * packet rate.
 */
static void adjust_rcv_timeout(struct qib_ctxtdata *rcd, int npkts)
{
	struct qib_devdata *dd = rcd->dd;
	u32 timeout = dd->cspec->rcvavail_timeout[rcd->ctxt];

	/*
	 * Dynamically adjust idle timeout on chip
	 * based on number of packets processed.
	 */
	if (npkts < rcv_int_count && timeout > 2)
		timeout >>= 1;
	else if (npkts >= rcv_int_count && timeout < rcv_int_timeout)
		timeout = min(timeout << 1, rcv_int_timeout);
	else
		return;

	dd->cspec->rcvavail_timeout[rcd->ctxt] = timeout;
	qib_write_kreg(dd, kr_rcvavailtimeout + rcd->ctxt, timeout);
}

/*
 * This is the main interrupt handler.
 * It will normally only be used for low frequency interrupts but may
 * have to handle all interrupts if INTx is enabled or fewer than normal
 * MSIx interrupts were allocated.
 * This routine should ignore the interrupt bits for any of the
 * dedicated MSIx handlers.
 */
static irqreturn_t qib_7322intr(int irq, void *data)
{
	struct qib_devdata *dd = data;
	irqreturn_t ret;
	u64 istat;
	u64 ctxtrbits;
	u64 rmask;
	unsigned i;
	u32 npkts;

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

	if (unlikely(istat == ~0ULL)) {
		qib_bad_intrstatus(dd);
		qib_dev_err(dd, "Interrupt status all f's, skipping\n");
		/* don't know if it was our interrupt or not */
		ret = IRQ_NONE;
		goto bail;
	}

	istat &= dd->cspec->main_int_mask;
	if (unlikely(!istat)) {
		/* already handled, or shared and not us */
		ret = IRQ_NONE;
		goto bail;
	}

	this_cpu_inc(*dd->int_counter);

	/* handle "errors" of various kinds first, device ahead of port */
	if (unlikely(istat & (~QIB_I_BITSEXTANT | QIB_I_GPIO |
			      QIB_I_C_ERROR | INT_MASK_P(Err, 0) |
			      INT_MASK_P(Err, 1))))
		unlikely_7322_intr(dd, istat);

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
	ctxtrbits = istat & (QIB_I_RCVAVAIL_MASK | QIB_I_RCVURG_MASK);
	if (ctxtrbits) {
		rmask = (1ULL << QIB_I_RCVAVAIL_LSB) |
			(1ULL << QIB_I_RCVURG_LSB);
		for (i = 0; i < dd->first_user_ctxt; i++) {
			if (ctxtrbits & rmask) {
				ctxtrbits &= ~rmask;
				if (dd->rcd[i])
					qib_kreceive(dd->rcd[i], NULL, &npkts);
			}
			rmask <<= 1;
		}
		if (ctxtrbits) {
			ctxtrbits = (ctxtrbits >> QIB_I_RCVAVAIL_LSB) |
				(ctxtrbits >> QIB_I_RCVURG_LSB);
			qib_handle_urcv(dd, ctxtrbits);
		}
	}

	if (istat & (QIB_I_P_SDMAINT(0) | QIB_I_P_SDMAINT(1)))
		sdma_7322_intr(dd, istat);

	if ((istat & QIB_I_SPIOBUFAVAIL) && (dd->flags & QIB_INITTED))
		qib_ib_piobufavail(dd);

	ret = IRQ_HANDLED;
bail:
	return ret;
}

/*
 * Dedicated receive packet available interrupt handler.
 */
static irqreturn_t qib_7322pintr(int irq, void *data)
{
	struct qib_ctxtdata *rcd = data;
	struct qib_devdata *dd = rcd->dd;
	u32 npkts;

	if ((dd->flags & (QIB_PRESENT | QIB_BADINTR)) != QIB_PRESENT)
		/*
		 * This return value is not great, but we do not want the
		 * interrupt core code to remove our interrupt handler
		 * because we don't appear to be handling an interrupt
		 * during a chip reset.
		 */
		return IRQ_HANDLED;

	this_cpu_inc(*dd->int_counter);

	/* Clear the interrupt bit we expect to be set. */
	qib_write_kreg(dd, kr_intclear, ((1ULL << QIB_I_RCVAVAIL_LSB) |
		       (1ULL << QIB_I_RCVURG_LSB)) << rcd->ctxt);

	qib_kreceive(rcd, NULL, &npkts);

	return IRQ_HANDLED;
}

/*
 * Dedicated Send buffer available interrupt handler.
 */
static irqreturn_t qib_7322bufavail(int irq, void *data)
{
	struct qib_devdata *dd = data;

	if ((dd->flags & (QIB_PRESENT | QIB_BADINTR)) != QIB_PRESENT)
		/*
		 * This return value is not great, but we do not want the
		 * interrupt core code to remove our interrupt handler
		 * because we don't appear to be handling an interrupt
		 * during a chip reset.
		 */
		return IRQ_HANDLED;

	this_cpu_inc(*dd->int_counter);

	/* Clear the interrupt bit we expect to be set. */
	qib_write_kreg(dd, kr_intclear, QIB_I_SPIOBUFAVAIL);

	/* qib_ib_piobufavail() will clear the want PIO interrupt if needed */
	if (dd->flags & QIB_INITTED)
		qib_ib_piobufavail(dd);
	else
		qib_wantpiobuf_7322_intr(dd, 0);

	return IRQ_HANDLED;
}

/*
 * Dedicated Send DMA interrupt handler.
 */
static irqreturn_t sdma_intr(int irq, void *data)
{
	struct qib_pportdata *ppd = data;
	struct qib_devdata *dd = ppd->dd;

	if ((dd->flags & (QIB_PRESENT | QIB_BADINTR)) != QIB_PRESENT)
		/*
		 * This return value is not great, but we do not want the
		 * interrupt core code to remove our interrupt handler
		 * because we don't appear to be handling an interrupt
		 * during a chip reset.
		 */
		return IRQ_HANDLED;

	this_cpu_inc(*dd->int_counter);

	/* Clear the interrupt bit we expect to be set. */
	qib_write_kreg(dd, kr_intclear, ppd->hw_pidx ?
		       INT_MASK_P(SDma, 1) : INT_MASK_P(SDma, 0));
	qib_sdma_intr(ppd);

	return IRQ_HANDLED;
}

/*
 * Dedicated Send DMA idle interrupt handler.
 */
static irqreturn_t sdma_idle_intr(int irq, void *data)
{
	struct qib_pportdata *ppd = data;
	struct qib_devdata *dd = ppd->dd;

	if ((dd->flags & (QIB_PRESENT | QIB_BADINTR)) != QIB_PRESENT)
		/*
		 * This return value is not great, but we do not want the
		 * interrupt core code to remove our interrupt handler
		 * because we don't appear to be handling an interrupt
		 * during a chip reset.
		 */
		return IRQ_HANDLED;

	this_cpu_inc(*dd->int_counter);

	/* Clear the interrupt bit we expect to be set. */
	qib_write_kreg(dd, kr_intclear, ppd->hw_pidx ?
		       INT_MASK_P(SDmaIdle, 1) : INT_MASK_P(SDmaIdle, 0));
	qib_sdma_intr(ppd);

	return IRQ_HANDLED;
}

/*
 * Dedicated Send DMA progress interrupt handler.
 */
static irqreturn_t sdma_progress_intr(int irq, void *data)
{
	struct qib_pportdata *ppd = data;
	struct qib_devdata *dd = ppd->dd;

	if ((dd->flags & (QIB_PRESENT | QIB_BADINTR)) != QIB_PRESENT)
		/*
		 * This return value is not great, but we do not want the
		 * interrupt core code to remove our interrupt handler
		 * because we don't appear to be handling an interrupt
		 * during a chip reset.
		 */
		return IRQ_HANDLED;

	this_cpu_inc(*dd->int_counter);

	/* Clear the interrupt bit we expect to be set. */
	qib_write_kreg(dd, kr_intclear, ppd->hw_pidx ?
		       INT_MASK_P(SDmaProgress, 1) :
		       INT_MASK_P(SDmaProgress, 0));
	qib_sdma_intr(ppd);

	return IRQ_HANDLED;
}

/*
 * Dedicated Send DMA cleanup interrupt handler.
 */
static irqreturn_t sdma_cleanup_intr(int irq, void *data)
{
	struct qib_pportdata *ppd = data;
	struct qib_devdata *dd = ppd->dd;

	if ((dd->flags & (QIB_PRESENT | QIB_BADINTR)) != QIB_PRESENT)
		/*
		 * This return value is not great, but we do not want the
		 * interrupt core code to remove our interrupt handler
		 * because we don't appear to be handling an interrupt
		 * during a chip reset.
		 */
		return IRQ_HANDLED;

	this_cpu_inc(*dd->int_counter);

	/* Clear the interrupt bit we expect to be set. */
	qib_write_kreg(dd, kr_intclear, ppd->hw_pidx ?
		       INT_MASK_PM(SDmaCleanupDone, 1) :
		       INT_MASK_PM(SDmaCleanupDone, 0));
	qib_sdma_process_event(ppd, qib_sdma_event_e20_hw_started);

	return IRQ_HANDLED;
}

#ifdef CONFIG_INFINIBAND_QIB_DCA

static void reset_dca_notifier(struct qib_devdata *dd, int msixnum)
{
	if (!dd->cspec->msix_entries[msixnum].dca)
		return;

	qib_devinfo(dd->pcidev, "Disabling notifier on HCA %d irq %d\n",
		    dd->unit, pci_irq_vector(dd->pcidev, msixnum));
	irq_set_affinity_notifier(pci_irq_vector(dd->pcidev, msixnum), NULL);
	dd->cspec->msix_entries[msixnum].notifier = NULL;
}

static void setup_dca_notifier(struct qib_devdata *dd, int msixnum)
{
	struct qib_msix_entry *m = &dd->cspec->msix_entries[msixnum];
	struct qib_irq_notify *n;

	if (!m->dca)
		return;
	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (n) {
		int ret;

		m->notifier = n;
		n->notify.irq = pci_irq_vector(dd->pcidev, msixnum);
		n->notify.notify = qib_irq_notifier_notify;
		n->notify.release = qib_irq_notifier_release;
		n->arg = m->arg;
		n->rcv = m->rcv;
		qib_devinfo(dd->pcidev,
			"set notifier irq %d rcv %d notify %p\n",
			n->notify.irq, n->rcv, &n->notify);
		ret = irq_set_affinity_notifier(
				n->notify.irq,
				&n->notify);
		if (ret) {
			m->notifier = NULL;
			kfree(n);
		}
	}
}

#endif

/*
 * Set up our chip-specific interrupt handler.
 * The interrupt type has already been setup, so
 * we just need to do the registration and error checking.
 * If we are using MSIx interrupts, we may fall back to
 * INTx later, if the interrupt handler doesn't get called
 * within 1/2 second (see verify_interrupt()).
 */
static void qib_setup_7322_interrupt(struct qib_devdata *dd, int clearpend)
{
	int ret, i, msixnum;
	u64 redirect[6];
	u64 mask;
	const struct cpumask *local_mask;
	int firstcpu, secondcpu = 0, currrcvcpu = 0;

	if (!dd->num_pports)
		return;

	if (clearpend) {
		/*
		 * if not switching interrupt types, be sure interrupts are
		 * disabled, and then clear anything pending at this point,
		 * because we are starting clean.
		 */
		qib_7322_set_intr_state(dd, 0);

		/* clear the reset error, init error/hwerror mask */
		qib_7322_init_hwerrors(dd);

		/* clear any interrupt bits that might be set */
		qib_write_kreg(dd, kr_intclear, ~0ULL);

		/* make sure no pending MSIx intr, and clear diag reg */
		qib_write_kreg(dd, kr_intgranted, ~0ULL);
		qib_write_kreg(dd, kr_vecclr_wo_int, ~0ULL);
	}

	if (!dd->cspec->num_msix_entries) {
		/* Try to get INTx interrupt */
try_intx:
		ret = pci_request_irq(dd->pcidev, 0, qib_7322intr, NULL, dd,
				      QIB_DRV_NAME);
		if (ret) {
			qib_dev_err(
				dd,
				"Couldn't setup INTx interrupt (irq=%d): %d\n",
				pci_irq_vector(dd->pcidev, 0), ret);
			return;
		}
		dd->cspec->main_int_mask = ~0ULL;
		return;
	}

	/* Try to get MSIx interrupts */
	memset(redirect, 0, sizeof(redirect));
	mask = ~0ULL;
	msixnum = 0;
	local_mask = cpumask_of_pcibus(dd->pcidev->bus);
	firstcpu = cpumask_first(local_mask);
	if (firstcpu >= nr_cpu_ids ||
			cpumask_weight(local_mask) == num_online_cpus()) {
		local_mask = topology_core_cpumask(0);
		firstcpu = cpumask_first(local_mask);
	}
	if (firstcpu < nr_cpu_ids) {
		secondcpu = cpumask_next(firstcpu, local_mask);
		if (secondcpu >= nr_cpu_ids)
			secondcpu = firstcpu;
		currrcvcpu = secondcpu;
	}
	for (i = 0; msixnum < dd->cspec->num_msix_entries; i++) {
		irq_handler_t handler;
		void *arg;
		int lsb, reg, sh;
#ifdef CONFIG_INFINIBAND_QIB_DCA
		int dca = 0;
#endif
		if (i < ARRAY_SIZE(irq_table)) {
			if (irq_table[i].port) {
				/* skip if for a non-configured port */
				if (irq_table[i].port > dd->num_pports)
					continue;
				arg = dd->pport + irq_table[i].port - 1;
			} else
				arg = dd;
#ifdef CONFIG_INFINIBAND_QIB_DCA
			dca = irq_table[i].dca;
#endif
			lsb = irq_table[i].lsb;
			handler = irq_table[i].handler;
			ret = pci_request_irq(dd->pcidev, msixnum, handler,
					      NULL, arg, QIB_DRV_NAME "%d%s",
					      dd->unit,
					      irq_table[i].name);
		} else {
			unsigned ctxt;

			ctxt = i - ARRAY_SIZE(irq_table);
			/* per krcvq context receive interrupt */
			arg = dd->rcd[ctxt];
			if (!arg)
				continue;
			if (qib_krcvq01_no_msi && ctxt < 2)
				continue;
#ifdef CONFIG_INFINIBAND_QIB_DCA
			dca = 1;
#endif
			lsb = QIB_I_RCVAVAIL_LSB + ctxt;
			handler = qib_7322pintr;
			ret = pci_request_irq(dd->pcidev, msixnum, handler,
					      NULL, arg,
					      QIB_DRV_NAME "%d (kctx)",
					      dd->unit);
		}

		if (ret) {
			/*
			 * Shouldn't happen since the enable said we could
			 * have as many as we are trying to setup here.
			 */
			qib_dev_err(dd,
				    "Couldn't setup MSIx interrupt (vec=%d, irq=%d): %d\n",
				    msixnum,
				    pci_irq_vector(dd->pcidev, msixnum),
				    ret);
			qib_7322_free_irq(dd);
			pci_alloc_irq_vectors(dd->pcidev, 1, 1,
					      PCI_IRQ_LEGACY);
			goto try_intx;
		}
		dd->cspec->msix_entries[msixnum].arg = arg;
#ifdef CONFIG_INFINIBAND_QIB_DCA
		dd->cspec->msix_entries[msixnum].dca = dca;
		dd->cspec->msix_entries[msixnum].rcv =
			handler == qib_7322pintr;
#endif
		if (lsb >= 0) {
			reg = lsb / IBA7322_REDIRECT_VEC_PER_REG;
			sh = (lsb % IBA7322_REDIRECT_VEC_PER_REG) *
				SYM_LSB(IntRedirect0, vec1);
			mask &= ~(1ULL << lsb);
			redirect[reg] |= ((u64) msixnum) << sh;
		}
		qib_read_kreg64(dd, 2 * msixnum + 1 +
				(QIB_7322_MsixTable_OFFS / sizeof(u64)));
		if (firstcpu < nr_cpu_ids &&
			zalloc_cpumask_var(
				&dd->cspec->msix_entries[msixnum].mask,
				GFP_KERNEL)) {
			if (handler == qib_7322pintr) {
				cpumask_set_cpu(currrcvcpu,
					dd->cspec->msix_entries[msixnum].mask);
				currrcvcpu = cpumask_next(currrcvcpu,
					local_mask);
				if (currrcvcpu >= nr_cpu_ids)
					currrcvcpu = secondcpu;
			} else {
				cpumask_set_cpu(firstcpu,
					dd->cspec->msix_entries[msixnum].mask);
			}
			irq_set_affinity_hint(
				pci_irq_vector(dd->pcidev, msixnum),
				dd->cspec->msix_entries[msixnum].mask);
		}
		msixnum++;
	}
	/* Initialize the vector mapping */
	for (i = 0; i < ARRAY_SIZE(redirect); i++)
		qib_write_kreg(dd, kr_intredirect + i, redirect[i]);
	dd->cspec->main_int_mask = mask;
	tasklet_setup(&dd->error_tasklet, qib_error_tasklet);
}

/**
 * qib_7322_boardname - fill in the board name and note features
 * @dd: the qlogic_ib device
 *
 * info will be based on the board revision register
 */
static unsigned qib_7322_boardname(struct qib_devdata *dd)
{
	/* Will need enumeration of board-types here */
	u32 boardid;
	unsigned int features = DUAL_PORT_CAP;

	boardid = SYM_FIELD(dd->revision, Revision, BoardID);

	switch (boardid) {
	case 0:
		dd->boardname = "InfiniPath_QLE7342_Emulation";
		break;
	case 1:
		dd->boardname = "InfiniPath_QLE7340";
		dd->flags |= QIB_HAS_QSFP;
		features = PORT_SPD_CAP;
		break;
	case 2:
		dd->boardname = "InfiniPath_QLE7342";
		dd->flags |= QIB_HAS_QSFP;
		break;
	case 3:
		dd->boardname = "InfiniPath_QMI7342";
		break;
	case 4:
		dd->boardname = "InfiniPath_Unsupported7342";
		qib_dev_err(dd, "Unsupported version of QMH7342\n");
		features = 0;
		break;
	case BOARD_QMH7342:
		dd->boardname = "InfiniPath_QMH7342";
		features = 0x24;
		break;
	case BOARD_QME7342:
		dd->boardname = "InfiniPath_QME7342";
		break;
	case 8:
		dd->boardname = "InfiniPath_QME7362";
		dd->flags |= QIB_HAS_QSFP;
		break;
	case BOARD_QMH7360:
		dd->boardname = "Intel IB QDR 1P FLR-QSFP Adptr";
		dd->flags |= QIB_HAS_QSFP;
		break;
	case 15:
		dd->boardname = "InfiniPath_QLE7342_TEST";
		dd->flags |= QIB_HAS_QSFP;
		break;
	default:
		dd->boardname = "InfiniPath_QLE73xy_UNKNOWN";
		qib_dev_err(dd, "Unknown 7322 board type %u\n", boardid);
		break;
	}
	dd->board_atten = 1; /* index into txdds_Xdr */

	snprintf(dd->boardversion, sizeof(dd->boardversion),
		 "ChipABI %u.%u, %s, InfiniPath%u %u.%u, SW Compat %u\n",
		 QIB_CHIP_VERS_MAJ, QIB_CHIP_VERS_MIN, dd->boardname,
		 (unsigned int)SYM_FIELD(dd->revision, Revision_R, Arch),
		 dd->majrev, dd->minrev,
		 (unsigned int)SYM_FIELD(dd->revision, Revision_R, SW));

	if (qib_singleport && (features >> PORT_SPD_CAP_SHIFT) & PORT_SPD_CAP) {
		qib_devinfo(dd->pcidev,
			    "IB%u: Forced to single port mode by module parameter\n",
			    dd->unit);
		features &= PORT_SPD_CAP;
	}

	return features;
}

/*
 * This routine sleeps, so it can only be called from user context, not
 * from interrupt context.
 */
static int qib_do_7322_reset(struct qib_devdata *dd)
{
	u64 val;
	u64 *msix_vecsave = NULL;
	int i, msix_entries, ret = 1;
	u16 cmdval;
	u8 int_line, clinesz;
	unsigned long flags;

	/* Use dev_err so it shows up in logs, etc. */
	qib_dev_err(dd, "Resetting InfiniPath unit %u\n", dd->unit);

	qib_pcie_getcmd(dd, &cmdval, &int_line, &clinesz);

	msix_entries = dd->cspec->num_msix_entries;

	/* no interrupts till re-initted */
	qib_7322_set_intr_state(dd, 0);

	qib_7322_free_irq(dd);

	if (msix_entries) {
		/* can be up to 512 bytes, too big for stack */
		msix_vecsave = kmalloc_array(2 * dd->cspec->num_msix_entries,
					     sizeof(u64),
					     GFP_KERNEL);
	}

	/*
	 * Core PCI (as of 2.6.18) doesn't save or rewrite the full vector
	 * info that is set up by the BIOS, so we have to save and restore
	 * it ourselves.   There is some risk something could change it,
	 * after we save it, but since we have disabled the MSIx, it
	 * shouldn't be touched...
	 */
	for (i = 0; i < msix_entries; i++) {
		u64 vecaddr, vecdata;

		vecaddr = qib_read_kreg64(dd, 2 * i +
				  (QIB_7322_MsixTable_OFFS / sizeof(u64)));
		vecdata = qib_read_kreg64(dd, 1 + 2 * i +
				  (QIB_7322_MsixTable_OFFS / sizeof(u64)));
		if (msix_vecsave) {
			msix_vecsave[2 * i] = vecaddr;
			/* save it without the masked bit set */
			msix_vecsave[1 + 2 * i] = vecdata & ~0x100000000ULL;
		}
	}

	dd->pport->cpspec->ibdeltainprog = 0;
	dd->pport->cpspec->ibsymdelta = 0;
	dd->pport->cpspec->iblnkerrdelta = 0;
	dd->pport->cpspec->ibmalfdelta = 0;
	/* so we check interrupts work again */
	dd->z_int_counter = qib_int_counter(dd);

	/*
	 * Keep chip from being accessed until we are ready.  Use
	 * writeq() directly, to allow the write even though QIB_PRESENT
	 * isn't set.
	 */
	dd->flags &= ~(QIB_INITTED | QIB_PRESENT | QIB_BADINTR);
	dd->flags |= QIB_DOING_RESET;
	val = dd->control | QLOGIC_IB_C_RESET;
	writeq(val, &dd->kregbase[kr_control]);

	for (i = 1; i <= 5; i++) {
		/*
		 * Allow MBIST, etc. to complete; longer on each retry.
		 * We sometimes get machine checks from bus timeout if no
		 * response, so for now, make it *really* long.
		 */
		msleep(1000 + (1 + i) * 3000);

		qib_pcie_reenable(dd, cmdval, int_line, clinesz);

		/*
		 * Use readq directly, so we don't need to mark it as PRESENT
		 * until we get a successful indication that all is well.
		 */
		val = readq(&dd->kregbase[kr_revision]);
		if (val == dd->revision)
			break;
		if (i == 5) {
			qib_dev_err(dd,
				"Failed to initialize after reset, unusable\n");
			ret = 0;
			goto  bail;
		}
	}

	dd->flags |= QIB_PRESENT; /* it's back */

	if (msix_entries) {
		/* restore the MSIx vector address and data if saved above */
		for (i = 0; i < msix_entries; i++) {
			if (!msix_vecsave || !msix_vecsave[2 * i])
				continue;
			qib_write_kreg(dd, 2 * i +
				(QIB_7322_MsixTable_OFFS / sizeof(u64)),
				msix_vecsave[2 * i]);
			qib_write_kreg(dd, 1 + 2 * i +
				(QIB_7322_MsixTable_OFFS / sizeof(u64)),
				msix_vecsave[1 + 2 * i]);
		}
	}

	/* initialize the remaining registers.  */
	for (i = 0; i < dd->num_pports; ++i)
		write_7322_init_portregs(&dd->pport[i]);
	write_7322_initregs(dd);

	if (qib_pcie_params(dd, dd->lbus_width, &msix_entries))
		qib_dev_err(dd,
			"Reset failed to setup PCIe or interrupts; continuing anyway\n");

	dd->cspec->num_msix_entries = msix_entries;
	qib_setup_7322_interrupt(dd, 1);

	for (i = 0; i < dd->num_pports; ++i) {
		struct qib_pportdata *ppd = &dd->pport[i];

		spin_lock_irqsave(&ppd->lflags_lock, flags);
		ppd->lflags |= QIBL_IB_FORCE_NOTIFY;
		ppd->lflags &= ~QIBL_IB_AUTONEG_FAILED;
		spin_unlock_irqrestore(&ppd->lflags_lock, flags);
	}

bail:
	dd->flags &= ~QIB_DOING_RESET; /* OK or not, no longer resetting */
	kfree(msix_vecsave);
	return ret;
}

/**
 * qib_7322_put_tid - write a TID to the chip
 * @dd: the qlogic_ib device
 * @tidptr: pointer to the expected TID (in chip) to update
 * @type: 0 for eager, 1 for expected
 * @pa: physical address of in memory buffer; tidinvalid if freeing
 */
static void qib_7322_put_tid(struct qib_devdata *dd, u64 __iomem *tidptr,
			     u32 type, unsigned long pa)
{
	if (!(dd->flags & QIB_PRESENT))
		return;
	if (pa != dd->tidinvalid) {
		u64 chippa = pa >> IBA7322_TID_PA_SHIFT;

		/* paranoia checks */
		if (pa != (chippa << IBA7322_TID_PA_SHIFT)) {
			qib_dev_err(dd, "Physaddr %lx not 2KB aligned!\n",
				    pa);
			return;
		}
		if (chippa >= (1UL << IBA7322_TID_SZ_SHIFT)) {
			qib_dev_err(dd,
				"Physical page address 0x%lx larger than supported\n",
				pa);
			return;
		}

		if (type == RCVHQ_RCV_TYPE_EAGER)
			chippa |= dd->tidtemplate;
		else /* for now, always full 4KB page */
			chippa |= IBA7322_TID_SZ_4K;
		pa = chippa;
	}
	writeq(pa, tidptr);
}

/**
 * qib_7322_clear_tids - clear all TID entries for a ctxt, expected and eager
 * @dd: the qlogic_ib device
 * @rcd: the ctxt
 *
 * clear all TID entries for a ctxt, expected and eager.
 * Used from qib_close().
 */
static void qib_7322_clear_tids(struct qib_devdata *dd,
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
		((char __iomem *) dd->kregbase +
		 dd->rcvtidbase +
		 ctxt * dd->rcvtidcnt * sizeof(*tidbase));

	for (i = 0; i < dd->rcvtidcnt; i++)
		qib_7322_put_tid(dd, &tidbase[i], RCVHQ_RCV_TYPE_EXPECTED,
				 tidinv);

	tidbase = (u64 __iomem *)
		((char __iomem *) dd->kregbase +
		 dd->rcvegrbase +
		 rcd->rcvegr_tid_base * sizeof(*tidbase));

	for (i = 0; i < rcd->rcvegrcnt; i++)
		qib_7322_put_tid(dd, &tidbase[i], RCVHQ_RCV_TYPE_EAGER,
				 tidinv);
}

/**
 * qib_7322_tidtemplate - setup constants for TID updates
 * @dd: the qlogic_ib device
 *
 * We setup stuff that we use a lot, to avoid calculating each time
 */
static void qib_7322_tidtemplate(struct qib_devdata *dd)
{
	/*
	 * For now, we always allocate 4KB buffers (at init) so we can
	 * receive max size packets.  We may want a module parameter to
	 * specify 2KB or 4KB and/or make it per port instead of per device
	 * for those who want to reduce memory footprint.  Note that the
	 * rcvhdrentsize size must be large enough to hold the largest
	 * IB header (currently 96 bytes) that we expect to handle (plus of
	 * course the 2 dwords of RHF).
	 */
	if (dd->rcvegrbufsize == 2048)
		dd->tidtemplate = IBA7322_TID_SZ_2K;
	else if (dd->rcvegrbufsize == 4096)
		dd->tidtemplate = IBA7322_TID_SZ_4K;
	dd->tidinvalid = 0;
}

/**
 * qib_7322_get_base_info - set chip-specific flags for user code
 * @rcd: the qlogic_ib ctxt
 * @kinfo: qib_base_info pointer
 *
 * We set the PCIE flag because the lower bandwidth on PCIe vs
 * HyperTransport can affect some user packet algorithims.
 */

static int qib_7322_get_base_info(struct qib_ctxtdata *rcd,
				  struct qib_base_info *kinfo)
{
	kinfo->spi_runtime_flags |= QIB_RUNTIME_CTXT_MSB_IN_QP |
		QIB_RUNTIME_PCIE | QIB_RUNTIME_NODMA_RTAIL |
		QIB_RUNTIME_HDRSUPP | QIB_RUNTIME_SDMA;
	if (rcd->dd->cspec->r1)
		kinfo->spi_runtime_flags |= QIB_RUNTIME_RCHK;
	if (rcd->dd->flags & QIB_USE_SPCL_TRIG)
		kinfo->spi_runtime_flags |= QIB_RUNTIME_SPECIAL_TRIGGER;

	return 0;
}

static struct qib_message_header *
qib_7322_get_msgheader(struct qib_devdata *dd, __le32 *rhf_addr)
{
	u32 offset = qib_hdrget_offset(rhf_addr);

	return (struct qib_message_header *)
		(rhf_addr - dd->rhf_offset + offset);
}

/*
 * Configure number of contexts.
 */
static void qib_7322_config_ctxts(struct qib_devdata *dd)
{
	unsigned long flags;
	u32 nchipctxts;

	nchipctxts = qib_read_kreg32(dd, kr_contextcnt);
	dd->cspec->numctxts = nchipctxts;
	if (qib_n_krcv_queues > 1 && dd->num_pports) {
		dd->first_user_ctxt = NUM_IB_PORTS +
			(qib_n_krcv_queues - 1) * dd->num_pports;
		if (dd->first_user_ctxt > nchipctxts)
			dd->first_user_ctxt = nchipctxts;
		dd->n_krcv_queues = dd->first_user_ctxt / dd->num_pports;
	} else {
		dd->first_user_ctxt = NUM_IB_PORTS;
		dd->n_krcv_queues = 1;
	}

	if (!qib_cfgctxts) {
		int nctxts = dd->first_user_ctxt + num_online_cpus();

		if (nctxts <= 6)
			dd->ctxtcnt = 6;
		else if (nctxts <= 10)
			dd->ctxtcnt = 10;
		else if (nctxts <= nchipctxts)
			dd->ctxtcnt = nchipctxts;
	} else if (qib_cfgctxts < dd->num_pports)
		dd->ctxtcnt = dd->num_pports;
	else if (qib_cfgctxts <= nchipctxts)
		dd->ctxtcnt = qib_cfgctxts;
	if (!dd->ctxtcnt) /* none of the above, set to max */
		dd->ctxtcnt = nchipctxts;

	/*
	 * Chip can be configured for 6, 10, or 18 ctxts, and choice
	 * affects number of eager TIDs per ctxt (1K, 2K, 4K).
	 * Lock to be paranoid about later motion, etc.
	 */
	spin_lock_irqsave(&dd->cspec->rcvmod_lock, flags);
	if (dd->ctxtcnt > 10)
		dd->rcvctrl |= 2ULL << SYM_LSB(RcvCtrl, ContextCfg);
	else if (dd->ctxtcnt > 6)
		dd->rcvctrl |= 1ULL << SYM_LSB(RcvCtrl, ContextCfg);
	/* else configure for default 6 receive ctxts */

	/* The XRC opcode is 5. */
	dd->rcvctrl |= 5ULL << SYM_LSB(RcvCtrl, XrcTypeCode);

	/*
	 * RcvCtrl *must* be written here so that the
	 * chip understands how to change rcvegrcnt below.
	 */
	qib_write_kreg(dd, kr_rcvctrl, dd->rcvctrl);
	spin_unlock_irqrestore(&dd->cspec->rcvmod_lock, flags);

	/* kr_rcvegrcnt changes based on the number of contexts enabled */
	dd->cspec->rcvegrcnt = qib_read_kreg32(dd, kr_rcvegrcnt);
	if (qib_rcvhdrcnt)
		dd->rcvhdrcnt = max(dd->cspec->rcvegrcnt, qib_rcvhdrcnt);
	else
		dd->rcvhdrcnt = 2 * max(dd->cspec->rcvegrcnt,
				    dd->num_pports > 1 ? 1024U : 2048U);
}

static int qib_7322_get_ib_cfg(struct qib_pportdata *ppd, int which)
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
		lsb = SYM_LSB(IBCCtrlB_0, IB_POLARITY_REV_SUPP);
		maskr = SYM_RMASK(IBCCtrlB_0, IB_POLARITY_REV_SUPP);
		break;

	case QIB_IB_CFG_LREV_ENB: /* Get Auto-Lane-reversal enable */
		lsb = SYM_LSB(IBCCtrlB_0, IB_LANE_REV_SUPPORTED);
		maskr = SYM_RMASK(IBCCtrlB_0, IB_LANE_REV_SUPPORTED);
		break;

	case QIB_IB_CFG_LINKLATENCY:
		ret = qib_read_kreg_port(ppd, krp_ibcstatus_b) &
			SYM_MASK(IBCStatusB_0, LinkRoundTripLatency);
		goto done;

	case QIB_IB_CFG_OP_VLS:
		ret = ppd->vls_operational;
		goto done;

	case QIB_IB_CFG_VL_HIGH_CAP:
		ret = 16;
		goto done;

	case QIB_IB_CFG_VL_LOW_CAP:
		ret = 16;
		goto done;

	case QIB_IB_CFG_OVERRUN_THRESH: /* IB overrun threshold */
		ret = SYM_FIELD(ppd->cpspec->ibcctrl_a, IBCCtrlA_0,
				OverrunThreshold);
		goto done;

	case QIB_IB_CFG_PHYERR_THRESH: /* IB PHY error threshold */
		ret = SYM_FIELD(ppd->cpspec->ibcctrl_a, IBCCtrlA_0,
				PhyerrThreshold);
		goto done;

	case QIB_IB_CFG_LINKDEFAULT: /* IB link default (sleep/poll) */
		/* will only take effect when the link state changes */
		ret = (ppd->cpspec->ibcctrl_a &
		       SYM_MASK(IBCCtrlA_0, LinkDownDefaultState)) ?
			IB_LINKINITCMD_SLEEP : IB_LINKINITCMD_POLL;
		goto done;

	case QIB_IB_CFG_HRTBT: /* Get Heartbeat off/enable/auto */
		lsb = IBA7322_IBC_HRTBT_LSB;
		maskr = IBA7322_IBC_HRTBT_RMASK; /* OR of AUTO and ENB */
		break;

	case QIB_IB_CFG_PMA_TICKS:
		/*
		 * 0x00 = 10x link transfer rate or 4 nsec. for 2.5Gbs
		 * Since the clock is always 250MHz, the value is 3, 1 or 0.
		 */
		if (ppd->link_speed_active == QIB_IB_QDR)
			ret = 3;
		else if (ppd->link_speed_active == QIB_IB_DDR)
			ret = 1;
		else
			ret = 0;
		goto done;

	default:
		ret = -EINVAL;
		goto done;
	}
	ret = (int)((ppd->cpspec->ibcctrl_b >> lsb) & maskr);
done:
	return ret;
}

/*
 * Below again cribbed liberally from older version. Do not lean
 * heavily on it.
 */
#define IBA7322_IBC_DLIDLMC_SHIFT QIB_7322_IBCCtrlB_0_IB_DLID_LSB
#define IBA7322_IBC_DLIDLMC_MASK (QIB_7322_IBCCtrlB_0_IB_DLID_RMASK \
	| (QIB_7322_IBCCtrlB_0_IB_DLID_MASK_RMASK << 16))

static int qib_7322_set_ib_cfg(struct qib_pportdata *ppd, int which, u32 val)
{
	struct qib_devdata *dd = ppd->dd;
	u64 maskr; /* right-justified mask */
	int lsb, ret = 0;
	u16 lcmd, licmd;
	unsigned long flags;

	switch (which) {
	case QIB_IB_CFG_LIDLMC:
		/*
		 * Set LID and LMC. Combined to avoid possible hazard
		 * caller puts LMC in 16MSbits, DLID in 16LSbits of val
		 */
		lsb = IBA7322_IBC_DLIDLMC_SHIFT;
		maskr = IBA7322_IBC_DLIDLMC_MASK;
		/*
		 * For header-checking, the SLID in the packet will
		 * be masked with SendIBSLMCMask, and compared
		 * with SendIBSLIDAssignMask. Make sure we do not
		 * set any bits not covered by the mask, or we get
		 * false-positives.
		 */
		qib_write_kreg_port(ppd, krp_sendslid,
				    val & (val >> 16) & SendIBSLIDAssignMask);
		qib_write_kreg_port(ppd, krp_sendslidmask,
				    (val >> 16) & SendIBSLMCMask);
		break;

	case QIB_IB_CFG_LWID_ENB: /* set allowed Link-width */
		ppd->link_width_enabled = val;
		/* convert IB value to chip register value */
		if (val == IB_WIDTH_1X)
			val = 0;
		else if (val == IB_WIDTH_4X)
			val = 1;
		else
			val = 3;
		maskr = SYM_RMASK(IBCCtrlB_0, IB_NUM_CHANNELS);
		lsb = SYM_LSB(IBCCtrlB_0, IB_NUM_CHANNELS);
		break;

	case QIB_IB_CFG_SPD_ENB: /* set allowed Link speeds */
		/*
		 * As with width, only write the actual register if the
		 * link is currently down, otherwise takes effect on next
		 * link change.  Since setting is being explicitly requested
		 * (via MAD or sysfs), clear autoneg failure status if speed
		 * autoneg is enabled.
		 */
		ppd->link_speed_enabled = val;
		val <<= IBA7322_IBC_SPEED_LSB;
		maskr = IBA7322_IBC_SPEED_MASK | IBA7322_IBC_IBTA_1_2_MASK |
			IBA7322_IBC_MAX_SPEED_MASK;
		if (val & (val - 1)) {
			/* Muliple speeds enabled */
			val |= IBA7322_IBC_IBTA_1_2_MASK |
				IBA7322_IBC_MAX_SPEED_MASK;
			spin_lock_irqsave(&ppd->lflags_lock, flags);
			ppd->lflags &= ~QIBL_IB_AUTONEG_FAILED;
			spin_unlock_irqrestore(&ppd->lflags_lock, flags);
		} else if (val & IBA7322_IBC_SPEED_QDR)
			val |= IBA7322_IBC_IBTA_1_2_MASK;
		/* IBTA 1.2 mode + min/max + speed bits are contiguous */
		lsb = SYM_LSB(IBCCtrlB_0, IB_ENHANCED_MODE);
		break;

	case QIB_IB_CFG_RXPOL_ENB: /* set Auto-RX-polarity enable */
		lsb = SYM_LSB(IBCCtrlB_0, IB_POLARITY_REV_SUPP);
		maskr = SYM_RMASK(IBCCtrlB_0, IB_POLARITY_REV_SUPP);
		break;

	case QIB_IB_CFG_LREV_ENB: /* set Auto-Lane-reversal enable */
		lsb = SYM_LSB(IBCCtrlB_0, IB_LANE_REV_SUPPORTED);
		maskr = SYM_RMASK(IBCCtrlB_0, IB_LANE_REV_SUPPORTED);
		break;

	case QIB_IB_CFG_OVERRUN_THRESH: /* IB overrun threshold */
		maskr = SYM_FIELD(ppd->cpspec->ibcctrl_a, IBCCtrlA_0,
				  OverrunThreshold);
		if (maskr != val) {
			ppd->cpspec->ibcctrl_a &=
				~SYM_MASK(IBCCtrlA_0, OverrunThreshold);
			ppd->cpspec->ibcctrl_a |= (u64) val <<
				SYM_LSB(IBCCtrlA_0, OverrunThreshold);
			qib_write_kreg_port(ppd, krp_ibcctrl_a,
					    ppd->cpspec->ibcctrl_a);
			qib_write_kreg(dd, kr_scratch, 0ULL);
		}
		goto bail;

	case QIB_IB_CFG_PHYERR_THRESH: /* IB PHY error threshold */
		maskr = SYM_FIELD(ppd->cpspec->ibcctrl_a, IBCCtrlA_0,
				  PhyerrThreshold);
		if (maskr != val) {
			ppd->cpspec->ibcctrl_a &=
				~SYM_MASK(IBCCtrlA_0, PhyerrThreshold);
			ppd->cpspec->ibcctrl_a |= (u64) val <<
				SYM_LSB(IBCCtrlA_0, PhyerrThreshold);
			qib_write_kreg_port(ppd, krp_ibcctrl_a,
					    ppd->cpspec->ibcctrl_a);
			qib_write_kreg(dd, kr_scratch, 0ULL);
		}
		goto bail;

	case QIB_IB_CFG_PKEYS: /* update pkeys */
		maskr = (u64) ppd->pkeys[0] | ((u64) ppd->pkeys[1] << 16) |
			((u64) ppd->pkeys[2] << 32) |
			((u64) ppd->pkeys[3] << 48);
		qib_write_kreg_port(ppd, krp_partitionkey, maskr);
		goto bail;

	case QIB_IB_CFG_LINKDEFAULT: /* IB link default (sleep/poll) */
		/* will only take effect when the link state changes */
		if (val == IB_LINKINITCMD_POLL)
			ppd->cpspec->ibcctrl_a &=
				~SYM_MASK(IBCCtrlA_0, LinkDownDefaultState);
		else /* SLEEP */
			ppd->cpspec->ibcctrl_a |=
				SYM_MASK(IBCCtrlA_0, LinkDownDefaultState);
		qib_write_kreg_port(ppd, krp_ibcctrl_a, ppd->cpspec->ibcctrl_a);
		qib_write_kreg(dd, kr_scratch, 0ULL);
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
		ppd->cpspec->ibcctrl_a &= ~SYM_MASK(IBCCtrlA_0, MaxPktLen);
		ppd->cpspec->ibcctrl_a |= (u64)val <<
			SYM_LSB(IBCCtrlA_0, MaxPktLen);
		qib_write_kreg_port(ppd, krp_ibcctrl_a,
				    ppd->cpspec->ibcctrl_a);
		qib_write_kreg(dd, kr_scratch, 0ULL);
		goto bail;

	case QIB_IB_CFG_LSTATE: /* set the IB link state */
		switch (val & 0xffff0000) {
		case IB_LINKCMD_DOWN:
			lcmd = QLOGIC_IB_IBCC_LINKCMD_DOWN;
			ppd->cpspec->ibmalfusesnap = 1;
			ppd->cpspec->ibmalfsnap = read_7322_creg32_port(ppd,
				crp_errlink);
			if (!ppd->cpspec->ibdeltainprog &&
			    qib_compat_ddr_negotiate) {
				ppd->cpspec->ibdeltainprog = 1;
				ppd->cpspec->ibsymsnap =
					read_7322_creg32_port(ppd,
							      crp_ibsymbolerr);
				ppd->cpspec->iblnkerrsnap =
					read_7322_creg32_port(ppd,
						      crp_iblinkerrrecov);
			}
			break;

		case IB_LINKCMD_ARMED:
			lcmd = QLOGIC_IB_IBCC_LINKCMD_ARMED;
			if (ppd->cpspec->ibmalfusesnap) {
				ppd->cpspec->ibmalfusesnap = 0;
				ppd->cpspec->ibmalfdelta +=
					read_7322_creg32_port(ppd,
							      crp_errlink) -
					ppd->cpspec->ibmalfsnap;
			}
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
		qib_set_ib_7322_lstate(ppd, lcmd, licmd);
		goto bail;

	case QIB_IB_CFG_OP_VLS:
		if (ppd->vls_operational != val) {
			ppd->vls_operational = val;
			set_vls(ppd);
		}
		goto bail;

	case QIB_IB_CFG_VL_HIGH_LIMIT:
		qib_write_kreg_port(ppd, krp_highprio_limit, val);
		goto bail;

	case QIB_IB_CFG_HRTBT: /* set Heartbeat off/enable/auto */
		if (val > 3) {
			ret = -EINVAL;
			goto bail;
		}
		lsb = IBA7322_IBC_HRTBT_LSB;
		maskr = IBA7322_IBC_HRTBT_RMASK; /* OR of AUTO and ENB */
		break;

	case QIB_IB_CFG_PORT:
		/* val is the port number of the switch we are connected to. */
		if (ppd->dd->cspec->r1) {
			cancel_delayed_work(&ppd->cpspec->ipg_work);
			ppd->cpspec->ipg_tries = 0;
		}
		goto bail;

	default:
		ret = -EINVAL;
		goto bail;
	}
	ppd->cpspec->ibcctrl_b &= ~(maskr << lsb);
	ppd->cpspec->ibcctrl_b |= (((u64) val & maskr) << lsb);
	qib_write_kreg_port(ppd, krp_ibcctrl_b, ppd->cpspec->ibcctrl_b);
	qib_write_kreg(dd, kr_scratch, 0);
bail:
	return ret;
}

static int qib_7322_set_loopback(struct qib_pportdata *ppd, const char *what)
{
	int ret = 0;
	u64 val, ctrlb;

	/* only IBC loopback, may add serdes and xgxs loopbacks later */
	if (!strncmp(what, "ibc", 3)) {
		ppd->cpspec->ibcctrl_a |= SYM_MASK(IBCCtrlA_0,
						       Loopback);
		val = 0; /* disable heart beat, so link will come up */
		qib_devinfo(ppd->dd->pcidev, "Enabling IB%u:%u IBC loopback\n",
			 ppd->dd->unit, ppd->port);
	} else if (!strncmp(what, "off", 3)) {
		ppd->cpspec->ibcctrl_a &= ~SYM_MASK(IBCCtrlA_0,
							Loopback);
		/* enable heart beat again */
		val = IBA7322_IBC_HRTBT_RMASK << IBA7322_IBC_HRTBT_LSB;
		qib_devinfo(ppd->dd->pcidev,
			"Disabling IB%u:%u IBC loopback (normal)\n",
			ppd->dd->unit, ppd->port);
	} else
		ret = -EINVAL;
	if (!ret) {
		qib_write_kreg_port(ppd, krp_ibcctrl_a,
				    ppd->cpspec->ibcctrl_a);
		ctrlb = ppd->cpspec->ibcctrl_b & ~(IBA7322_IBC_HRTBT_MASK
					     << IBA7322_IBC_HRTBT_LSB);
		ppd->cpspec->ibcctrl_b = ctrlb | val;
		qib_write_kreg_port(ppd, krp_ibcctrl_b,
				    ppd->cpspec->ibcctrl_b);
		qib_write_kreg(ppd->dd, kr_scratch, 0);
	}
	return ret;
}

static void get_vl_weights(struct qib_pportdata *ppd, unsigned regno,
			   struct ib_vl_weight_elem *vl)
{
	unsigned i;

	for (i = 0; i < 16; i++, regno++, vl++) {
		u32 val = qib_read_kreg_port(ppd, regno);

		vl->vl = (val >> SYM_LSB(LowPriority0_0, VirtualLane)) &
			SYM_RMASK(LowPriority0_0, VirtualLane);
		vl->weight = (val >> SYM_LSB(LowPriority0_0, Weight)) &
			SYM_RMASK(LowPriority0_0, Weight);
	}
}

static void set_vl_weights(struct qib_pportdata *ppd, unsigned regno,
			   struct ib_vl_weight_elem *vl)
{
	unsigned i;

	for (i = 0; i < 16; i++, regno++, vl++) {
		u64 val;

		val = ((vl->vl & SYM_RMASK(LowPriority0_0, VirtualLane)) <<
			SYM_LSB(LowPriority0_0, VirtualLane)) |
		      ((vl->weight & SYM_RMASK(LowPriority0_0, Weight)) <<
			SYM_LSB(LowPriority0_0, Weight));
		qib_write_kreg_port(ppd, regno, val);
	}
	if (!(ppd->p_sendctrl & SYM_MASK(SendCtrl_0, IBVLArbiterEn))) {
		struct qib_devdata *dd = ppd->dd;
		unsigned long flags;

		spin_lock_irqsave(&dd->sendctrl_lock, flags);
		ppd->p_sendctrl |= SYM_MASK(SendCtrl_0, IBVLArbiterEn);
		qib_write_kreg_port(ppd, krp_sendctrl, ppd->p_sendctrl);
		qib_write_kreg(dd, kr_scratch, 0);
		spin_unlock_irqrestore(&dd->sendctrl_lock, flags);
	}
}

static int qib_7322_get_ib_table(struct qib_pportdata *ppd, int which, void *t)
{
	switch (which) {
	case QIB_IB_TBL_VL_HIGH_ARB:
		get_vl_weights(ppd, krp_highprio_0, t);
		break;

	case QIB_IB_TBL_VL_LOW_ARB:
		get_vl_weights(ppd, krp_lowprio_0, t);
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static int qib_7322_set_ib_table(struct qib_pportdata *ppd, int which, void *t)
{
	switch (which) {
	case QIB_IB_TBL_VL_HIGH_ARB:
		set_vl_weights(ppd, krp_highprio_0, t);
		break;

	case QIB_IB_TBL_VL_LOW_ARB:
		set_vl_weights(ppd, krp_lowprio_0, t);
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static void qib_update_7322_usrhead(struct qib_ctxtdata *rcd, u64 hd,
				    u32 updegr, u32 egrhd, u32 npkts)
{
	/*
	 * Need to write timeout register before updating rcvhdrhead to ensure
	 * that the timer is enabled on reception of a packet.
	 */
	if (hd >> IBA7322_HDRHEAD_PKTINT_SHIFT)
		adjust_rcv_timeout(rcd, npkts);
	if (updegr)
		qib_write_ureg(rcd->dd, ur_rcvegrindexhead, egrhd, rcd->ctxt);
	qib_write_ureg(rcd->dd, ur_rcvhdrhead, hd, rcd->ctxt);
	qib_write_ureg(rcd->dd, ur_rcvhdrhead, hd, rcd->ctxt);
}

static u32 qib_7322_hdrqempty(struct qib_ctxtdata *rcd)
{
	u32 head, tail;

	head = qib_read_ureg32(rcd->dd, ur_rcvhdrhead, rcd->ctxt);
	if (rcd->rcvhdrtail_kvaddr)
		tail = qib_get_rcvhdrtail(rcd);
	else
		tail = qib_read_ureg32(rcd->dd, ur_rcvhdrtail, rcd->ctxt);
	return head == tail;
}

#define RCVCTRL_COMMON_MODS (QIB_RCVCTRL_CTXT_ENB | \
	QIB_RCVCTRL_CTXT_DIS | \
	QIB_RCVCTRL_TIDFLOW_ENB | \
	QIB_RCVCTRL_TIDFLOW_DIS | \
	QIB_RCVCTRL_TAILUPD_ENB | \
	QIB_RCVCTRL_TAILUPD_DIS | \
	QIB_RCVCTRL_INTRAVAIL_ENB | \
	QIB_RCVCTRL_INTRAVAIL_DIS | \
	QIB_RCVCTRL_BP_ENB | \
	QIB_RCVCTRL_BP_DIS)

#define RCVCTRL_PORT_MODS (QIB_RCVCTRL_CTXT_ENB | \
	QIB_RCVCTRL_CTXT_DIS | \
	QIB_RCVCTRL_PKEY_DIS | \
	QIB_RCVCTRL_PKEY_ENB)

/*
 * Modify the RCVCTRL register in chip-specific way. This
 * is a function because bit positions and (future) register
 * location is chip-specifc, but the needed operations are
 * generic. <op> is a bit-mask because we often want to
 * do multiple modifications.
 */
static void rcvctrl_7322_mod(struct qib_pportdata *ppd, unsigned int op,
			     int ctxt)
{
	struct qib_devdata *dd = ppd->dd;
	struct qib_ctxtdata *rcd;
	u64 mask, val;
	unsigned long flags;

	spin_lock_irqsave(&dd->cspec->rcvmod_lock, flags);

	if (op & QIB_RCVCTRL_TIDFLOW_ENB)
		dd->rcvctrl |= SYM_MASK(RcvCtrl, TidFlowEnable);
	if (op & QIB_RCVCTRL_TIDFLOW_DIS)
		dd->rcvctrl &= ~SYM_MASK(RcvCtrl, TidFlowEnable);
	if (op & QIB_RCVCTRL_TAILUPD_ENB)
		dd->rcvctrl |= SYM_MASK(RcvCtrl, TailUpd);
	if (op & QIB_RCVCTRL_TAILUPD_DIS)
		dd->rcvctrl &= ~SYM_MASK(RcvCtrl, TailUpd);
	if (op & QIB_RCVCTRL_PKEY_ENB)
		ppd->p_rcvctrl &= ~SYM_MASK(RcvCtrl_0, RcvPartitionKeyDisable);
	if (op & QIB_RCVCTRL_PKEY_DIS)
		ppd->p_rcvctrl |= SYM_MASK(RcvCtrl_0, RcvPartitionKeyDisable);
	if (ctxt < 0) {
		mask = (1ULL << dd->ctxtcnt) - 1;
		rcd = NULL;
	} else {
		mask = (1ULL << ctxt);
		rcd = dd->rcd[ctxt];
	}
	if ((op & QIB_RCVCTRL_CTXT_ENB) && rcd) {
		ppd->p_rcvctrl |=
			(mask << SYM_LSB(RcvCtrl_0, ContextEnableKernel));
		if (!(dd->flags & QIB_NODMA_RTAIL)) {
			op |= QIB_RCVCTRL_TAILUPD_ENB; /* need reg write */
			dd->rcvctrl |= SYM_MASK(RcvCtrl, TailUpd);
		}
		/* Write these registers before the context is enabled. */
		qib_write_kreg_ctxt(dd, krc_rcvhdrtailaddr, ctxt,
				    rcd->rcvhdrqtailaddr_phys);
		qib_write_kreg_ctxt(dd, krc_rcvhdraddr, ctxt,
				    rcd->rcvhdrq_phys);
		rcd->seq_cnt = 1;
	}
	if (op & QIB_RCVCTRL_CTXT_DIS)
		ppd->p_rcvctrl &=
			~(mask << SYM_LSB(RcvCtrl_0, ContextEnableKernel));
	if (op & QIB_RCVCTRL_BP_ENB)
		dd->rcvctrl |= mask << SYM_LSB(RcvCtrl, dontDropRHQFull);
	if (op & QIB_RCVCTRL_BP_DIS)
		dd->rcvctrl &= ~(mask << SYM_LSB(RcvCtrl, dontDropRHQFull));
	if (op & QIB_RCVCTRL_INTRAVAIL_ENB)
		dd->rcvctrl |= (mask << SYM_LSB(RcvCtrl, IntrAvail));
	if (op & QIB_RCVCTRL_INTRAVAIL_DIS)
		dd->rcvctrl &= ~(mask << SYM_LSB(RcvCtrl, IntrAvail));
	/*
	 * Decide which registers to write depending on the ops enabled.
	 * Special case is "flush" (no bits set at all)
	 * which needs to write both.
	 */
	if (op == 0 || (op & RCVCTRL_COMMON_MODS))
		qib_write_kreg(dd, kr_rcvctrl, dd->rcvctrl);
	if (op == 0 || (op & RCVCTRL_PORT_MODS))
		qib_write_kreg_port(ppd, krp_rcvctrl, ppd->p_rcvctrl);
	if ((op & QIB_RCVCTRL_CTXT_ENB) && dd->rcd[ctxt]) {
		/*
		 * Init the context registers also; if we were
		 * disabled, tail and head should both be zero
		 * already from the enable, but since we don't
		 * know, we have to do it explicitly.
		 */
		val = qib_read_ureg32(dd, ur_rcvegrindextail, ctxt);
		qib_write_ureg(dd, ur_rcvegrindexhead, val, ctxt);

		/* be sure enabling write seen; hd/tl should be 0 */
		(void) qib_read_kreg32(dd, kr_scratch);
		val = qib_read_ureg32(dd, ur_rcvhdrtail, ctxt);
		dd->rcd[ctxt]->head = val;
		/* If kctxt, interrupt on next receive. */
		if (ctxt < dd->first_user_ctxt)
			val |= dd->rhdrhead_intr_off;
		qib_write_ureg(dd, ur_rcvhdrhead, val, ctxt);
	} else if ((op & QIB_RCVCTRL_INTRAVAIL_ENB) &&
		dd->rcd[ctxt] && dd->rhdrhead_intr_off) {
		/* arm rcv interrupt */
		val = dd->rcd[ctxt]->head | dd->rhdrhead_intr_off;
		qib_write_ureg(dd, ur_rcvhdrhead, val, ctxt);
	}
	if (op & QIB_RCVCTRL_CTXT_DIS) {
		unsigned f;

		/* Now that the context is disabled, clear these registers. */
		if (ctxt >= 0) {
			qib_write_kreg_ctxt(dd, krc_rcvhdrtailaddr, ctxt, 0);
			qib_write_kreg_ctxt(dd, krc_rcvhdraddr, ctxt, 0);
			for (f = 0; f < NUM_TIDFLOWS_CTXT; f++)
				qib_write_ureg(dd, ur_rcvflowtable + f,
					       TIDFLOW_ERRBITS, ctxt);
		} else {
			unsigned i;

			for (i = 0; i < dd->cfgctxts; i++) {
				qib_write_kreg_ctxt(dd, krc_rcvhdrtailaddr,
						    i, 0);
				qib_write_kreg_ctxt(dd, krc_rcvhdraddr, i, 0);
				for (f = 0; f < NUM_TIDFLOWS_CTXT; f++)
					qib_write_ureg(dd, ur_rcvflowtable + f,
						       TIDFLOW_ERRBITS, i);
			}
		}
	}
	spin_unlock_irqrestore(&dd->cspec->rcvmod_lock, flags);
}

/*
 * Modify the SENDCTRL register in chip-specific way. This
 * is a function where there are multiple such registers with
 * slightly different layouts.
 * The chip doesn't allow back-to-back sendctrl writes, so write
 * the scratch register after writing sendctrl.
 *
 * Which register is written depends on the operation.
 * Most operate on the common register, while
 * SEND_ENB and SEND_DIS operate on the per-port ones.
 * SEND_ENB is included in common because it can change SPCL_TRIG
 */
#define SENDCTRL_COMMON_MODS (\
	QIB_SENDCTRL_CLEAR | \
	QIB_SENDCTRL_AVAIL_DIS | \
	QIB_SENDCTRL_AVAIL_ENB | \
	QIB_SENDCTRL_AVAIL_BLIP | \
	QIB_SENDCTRL_DISARM | \
	QIB_SENDCTRL_DISARM_ALL | \
	QIB_SENDCTRL_SEND_ENB)

#define SENDCTRL_PORT_MODS (\
	QIB_SENDCTRL_CLEAR | \
	QIB_SENDCTRL_SEND_ENB | \
	QIB_SENDCTRL_SEND_DIS | \
	QIB_SENDCTRL_FLUSH)

static void sendctrl_7322_mod(struct qib_pportdata *ppd, u32 op)
{
	struct qib_devdata *dd = ppd->dd;
	u64 tmp_dd_sendctrl;
	unsigned long flags;

	spin_lock_irqsave(&dd->sendctrl_lock, flags);

	/* First the dd ones that are "sticky", saved in shadow */
	if (op & QIB_SENDCTRL_CLEAR)
		dd->sendctrl = 0;
	if (op & QIB_SENDCTRL_AVAIL_DIS)
		dd->sendctrl &= ~SYM_MASK(SendCtrl, SendBufAvailUpd);
	else if (op & QIB_SENDCTRL_AVAIL_ENB) {
		dd->sendctrl |= SYM_MASK(SendCtrl, SendBufAvailUpd);
		if (dd->flags & QIB_USE_SPCL_TRIG)
			dd->sendctrl |= SYM_MASK(SendCtrl, SpecialTriggerEn);
	}

	/* Then the ppd ones that are "sticky", saved in shadow */
	if (op & QIB_SENDCTRL_SEND_DIS)
		ppd->p_sendctrl &= ~SYM_MASK(SendCtrl_0, SendEnable);
	else if (op & QIB_SENDCTRL_SEND_ENB)
		ppd->p_sendctrl |= SYM_MASK(SendCtrl_0, SendEnable);

	if (op & QIB_SENDCTRL_DISARM_ALL) {
		u32 i, last;

		tmp_dd_sendctrl = dd->sendctrl;
		last = dd->piobcnt2k + dd->piobcnt4k + NUM_VL15_BUFS;
		/*
		 * Disarm any buffers that are not yet launched,
		 * disabling updates until done.
		 */
		tmp_dd_sendctrl &= ~SYM_MASK(SendCtrl, SendBufAvailUpd);
		for (i = 0; i < last; i++) {
			qib_write_kreg(dd, kr_sendctrl,
				       tmp_dd_sendctrl |
				       SYM_MASK(SendCtrl, Disarm) | i);
			qib_write_kreg(dd, kr_scratch, 0);
		}
	}

	if (op & QIB_SENDCTRL_FLUSH) {
		u64 tmp_ppd_sendctrl = ppd->p_sendctrl;

		/*
		 * Now drain all the fifos.  The Abort bit should never be
		 * needed, so for now, at least, we don't use it.
		 */
		tmp_ppd_sendctrl |=
			SYM_MASK(SendCtrl_0, TxeDrainRmFifo) |
			SYM_MASK(SendCtrl_0, TxeDrainLaFifo) |
			SYM_MASK(SendCtrl_0, TxeBypassIbc);
		qib_write_kreg_port(ppd, krp_sendctrl, tmp_ppd_sendctrl);
		qib_write_kreg(dd, kr_scratch, 0);
	}

	tmp_dd_sendctrl = dd->sendctrl;

	if (op & QIB_SENDCTRL_DISARM)
		tmp_dd_sendctrl |= SYM_MASK(SendCtrl, Disarm) |
			((op & QIB_7322_SendCtrl_DisarmSendBuf_RMASK) <<
			 SYM_LSB(SendCtrl, DisarmSendBuf));
	if ((op & QIB_SENDCTRL_AVAIL_BLIP) &&
	    (dd->sendctrl & SYM_MASK(SendCtrl, SendBufAvailUpd)))
		tmp_dd_sendctrl &= ~SYM_MASK(SendCtrl, SendBufAvailUpd);

	if (op == 0 || (op & SENDCTRL_COMMON_MODS)) {
		qib_write_kreg(dd, kr_sendctrl, tmp_dd_sendctrl);
		qib_write_kreg(dd, kr_scratch, 0);
	}

	if (op == 0 || (op & SENDCTRL_PORT_MODS)) {
		qib_write_kreg_port(ppd, krp_sendctrl, ppd->p_sendctrl);
		qib_write_kreg(dd, kr_scratch, 0);
	}

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

#define _PORT_VIRT_FLAG 0x8000U /* "virtual", need adjustments */
#define _PORT_64BIT_FLAG 0x10000U /* not "virtual", but 64bit */
#define _PORT_CNTR_IDXMASK 0x7fffU /* mask off flags above */

/**
 * qib_portcntr_7322 - read a per-port chip counter
 * @ppd: the qlogic_ib pport
 * @reg: the counter to read (not a chip offset)
 */
static u64 qib_portcntr_7322(struct qib_pportdata *ppd, u32 reg)
{
	struct qib_devdata *dd = ppd->dd;
	u64 ret = 0ULL;
	u16 creg;
	/* 0xffff for unimplemented or synthesized counters */
	static const u32 xlator[] = {
		[QIBPORTCNTR_PKTSEND] = crp_pktsend | _PORT_64BIT_FLAG,
		[QIBPORTCNTR_WORDSEND] = crp_wordsend | _PORT_64BIT_FLAG,
		[QIBPORTCNTR_PSXMITDATA] = crp_psxmitdatacount,
		[QIBPORTCNTR_PSXMITPKTS] = crp_psxmitpktscount,
		[QIBPORTCNTR_PSXMITWAIT] = crp_psxmitwaitcount,
		[QIBPORTCNTR_SENDSTALL] = crp_sendstall,
		[QIBPORTCNTR_PKTRCV] = crp_pktrcv | _PORT_64BIT_FLAG,
		[QIBPORTCNTR_PSRCVDATA] = crp_psrcvdatacount,
		[QIBPORTCNTR_PSRCVPKTS] = crp_psrcvpktscount,
		[QIBPORTCNTR_RCVEBP] = crp_rcvebp,
		[QIBPORTCNTR_RCVOVFL] = crp_rcvovfl,
		[QIBPORTCNTR_WORDRCV] = crp_wordrcv | _PORT_64BIT_FLAG,
		[QIBPORTCNTR_RXDROPPKT] = 0xffff, /* not needed  for 7322 */
		[QIBPORTCNTR_RXLOCALPHYERR] = crp_rxotherlocalphyerr,
		[QIBPORTCNTR_RXVLERR] = crp_rxvlerr,
		[QIBPORTCNTR_ERRICRC] = crp_erricrc,
		[QIBPORTCNTR_ERRVCRC] = crp_errvcrc,
		[QIBPORTCNTR_ERRLPCRC] = crp_errlpcrc,
		[QIBPORTCNTR_BADFORMAT] = crp_badformat,
		[QIBPORTCNTR_ERR_RLEN] = crp_err_rlen,
		[QIBPORTCNTR_IBSYMBOLERR] = crp_ibsymbolerr,
		[QIBPORTCNTR_INVALIDRLEN] = crp_invalidrlen,
		[QIBPORTCNTR_UNSUPVL] = crp_txunsupvl,
		[QIBPORTCNTR_EXCESSBUFOVFL] = crp_excessbufferovfl,
		[QIBPORTCNTR_ERRLINK] = crp_errlink,
		[QIBPORTCNTR_IBLINKDOWN] = crp_iblinkdown,
		[QIBPORTCNTR_IBLINKERRRECOV] = crp_iblinkerrrecov,
		[QIBPORTCNTR_LLI] = crp_locallinkintegrityerr,
		[QIBPORTCNTR_VL15PKTDROP] = crp_vl15droppedpkt,
		[QIBPORTCNTR_ERRPKEY] = crp_errpkey,
		/*
		 * the next 3 aren't really counters, but were implemented
		 * as counters in older chips, so still get accessed as
		 * though they were counters from this code.
		 */
		[QIBPORTCNTR_PSINTERVAL] = krp_psinterval,
		[QIBPORTCNTR_PSSTART] = krp_psstart,
		[QIBPORTCNTR_PSSTAT] = krp_psstat,
		/* pseudo-counter, summed for all ports */
		[QIBPORTCNTR_KHDROVFL] = 0xffff,
	};

	if (reg >= ARRAY_SIZE(xlator)) {
		qib_devinfo(ppd->dd->pcidev,
			 "Unimplemented portcounter %u\n", reg);
		goto done;
	}
	creg = xlator[reg] & _PORT_CNTR_IDXMASK;

	/* handle non-counters and special cases first */
	if (reg == QIBPORTCNTR_KHDROVFL) {
		int i;

		/* sum over all kernel contexts (skip if mini_init) */
		for (i = 0; dd->rcd && i < dd->first_user_ctxt; i++) {
			struct qib_ctxtdata *rcd = dd->rcd[i];

			if (!rcd || rcd->ppd != ppd)
				continue;
			ret += read_7322_creg32(dd, cr_base_egrovfl + i);
		}
		goto done;
	} else if (reg == QIBPORTCNTR_RXDROPPKT) {
		/*
		 * Used as part of the synthesis of port_rcv_errors
		 * in the verbs code for IBTA counters.  Not needed for 7322,
		 * because all the errors are already counted by other cntrs.
		 */
		goto done;
	} else if (reg == QIBPORTCNTR_PSINTERVAL ||
		   reg == QIBPORTCNTR_PSSTART || reg == QIBPORTCNTR_PSSTAT) {
		/* were counters in older chips, now per-port kernel regs */
		ret = qib_read_kreg_port(ppd, creg);
		goto done;
	}

	/*
	 * Only fast increment counters are 64 bits; use 32 bit reads to
	 * avoid two independent reads when on Opteron.
	 */
	if (xlator[reg] & _PORT_64BIT_FLAG)
		ret = read_7322_creg_port(ppd, creg);
	else
		ret = read_7322_creg32_port(ppd, creg);
	if (creg == crp_ibsymbolerr) {
		if (ppd->cpspec->ibdeltainprog)
			ret -= ret - ppd->cpspec->ibsymsnap;
		ret -= ppd->cpspec->ibsymdelta;
	} else if (creg == crp_iblinkerrrecov) {
		if (ppd->cpspec->ibdeltainprog)
			ret -= ret - ppd->cpspec->iblnkerrsnap;
		ret -= ppd->cpspec->iblnkerrdelta;
	} else if (creg == crp_errlink)
		ret -= ppd->cpspec->ibmalfdelta;
	else if (creg == crp_iblinkdown)
		ret += ppd->cpspec->iblnkdowndelta;
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
 * cntr7322indices contains the corresponding register indices.
 */
static const char cntr7322names[] =
	"Interrupts\n"
	"HostBusStall\n"
	"E RxTIDFull\n"
	"RxTIDInvalid\n"
	"RxTIDFloDrop\n" /* 7322 only */
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
	"Ctx16EgrOvfl\n"
	"Ctx17EgrOvfl\n"
	;

static const u32 cntr7322indices[] = {
	cr_lbint | _PORT_64BIT_FLAG,
	cr_lbstall | _PORT_64BIT_FLAG,
	cr_tidfull,
	cr_tidinvalid,
	cr_rxtidflowdrop,
	cr_base_egrovfl + 0,
	cr_base_egrovfl + 1,
	cr_base_egrovfl + 2,
	cr_base_egrovfl + 3,
	cr_base_egrovfl + 4,
	cr_base_egrovfl + 5,
	cr_base_egrovfl + 6,
	cr_base_egrovfl + 7,
	cr_base_egrovfl + 8,
	cr_base_egrovfl + 9,
	cr_base_egrovfl + 10,
	cr_base_egrovfl + 11,
	cr_base_egrovfl + 12,
	cr_base_egrovfl + 13,
	cr_base_egrovfl + 14,
	cr_base_egrovfl + 15,
	cr_base_egrovfl + 16,
	cr_base_egrovfl + 17,
};

/*
 * same as cntr7322names and cntr7322indices, but for port-specific counters.
 * portcntr7322indices is somewhat complicated by some registers needing
 * adjustments of various kinds, and those are ORed with _PORT_VIRT_FLAG
 */
static const char portcntr7322names[] =
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
	"RxLclPhyErr\n" /* 7220 and 7322-only from here down */
	"RxVL15Drop\n"
	"RxVlErr\n"
	"XcessBufOvfl\n"
	"RxQPBadCtxt\n" /* 7322-only from here down */
	"TXBadHeader\n"
	;

static const u32 portcntr7322indices[] = {
	QIBPORTCNTR_PKTSEND | _PORT_VIRT_FLAG,
	crp_pktsendflow,
	QIBPORTCNTR_WORDSEND | _PORT_VIRT_FLAG,
	QIBPORTCNTR_PKTRCV | _PORT_VIRT_FLAG,
	crp_pktrcvflowctrl,
	QIBPORTCNTR_WORDRCV | _PORT_VIRT_FLAG,
	QIBPORTCNTR_SENDSTALL | _PORT_VIRT_FLAG,
	crp_txsdmadesc | _PORT_64BIT_FLAG,
	crp_rxdlidfltr,
	crp_ibstatuschange,
	QIBPORTCNTR_IBLINKDOWN | _PORT_VIRT_FLAG,
	QIBPORTCNTR_IBLINKERRRECOV | _PORT_VIRT_FLAG,
	QIBPORTCNTR_ERRLINK | _PORT_VIRT_FLAG,
	QIBPORTCNTR_IBSYMBOLERR | _PORT_VIRT_FLAG,
	QIBPORTCNTR_LLI | _PORT_VIRT_FLAG,
	QIBPORTCNTR_BADFORMAT | _PORT_VIRT_FLAG,
	QIBPORTCNTR_ERR_RLEN | _PORT_VIRT_FLAG,
	QIBPORTCNTR_RCVOVFL | _PORT_VIRT_FLAG,
	QIBPORTCNTR_RCVEBP | _PORT_VIRT_FLAG,
	crp_rcvflowctrlviol,
	QIBPORTCNTR_ERRICRC | _PORT_VIRT_FLAG,
	QIBPORTCNTR_ERRLPCRC | _PORT_VIRT_FLAG,
	QIBPORTCNTR_ERRVCRC | _PORT_VIRT_FLAG,
	QIBPORTCNTR_INVALIDRLEN | _PORT_VIRT_FLAG,
	QIBPORTCNTR_ERRPKEY | _PORT_VIRT_FLAG,
	QIBPORTCNTR_RXDROPPKT | _PORT_VIRT_FLAG,
	crp_txminmaxlenerr,
	crp_txdroppedpkt,
	crp_txlenerr,
	crp_txunderrun,
	crp_txunsupvl,
	QIBPORTCNTR_RXLOCALPHYERR | _PORT_VIRT_FLAG,
	QIBPORTCNTR_VL15PKTDROP | _PORT_VIRT_FLAG,
	QIBPORTCNTR_RXVLERR | _PORT_VIRT_FLAG,
	QIBPORTCNTR_EXCESSBUFOVFL | _PORT_VIRT_FLAG,
	crp_rxqpinvalidctxt,
	crp_txhdrerr,
};

/* do all the setup to make the counter reads efficient later */
static void init_7322_cntrnames(struct qib_devdata *dd)
{
	int i, j = 0;
	char *s;

	for (i = 0, s = (char *)cntr7322names; s && j <= dd->cfgctxts;
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
		dd->cspec->cntrnamelen = sizeof(cntr7322names) - 1;
	else
		dd->cspec->cntrnamelen = 1 + s - cntr7322names;
	dd->cspec->cntrs = kmalloc_array(dd->cspec->ncntrs, sizeof(u64),
					 GFP_KERNEL);

	for (i = 0, s = (char *)portcntr7322names; s; i++)
		s = strchr(s + 1, '\n');
	dd->cspec->nportcntrs = i - 1;
	dd->cspec->portcntrnamelen = sizeof(portcntr7322names) - 1;
	for (i = 0; i < dd->num_pports; ++i) {
		dd->pport[i].cpspec->portcntrs =
			kmalloc_array(dd->cspec->nportcntrs, sizeof(u64),
				      GFP_KERNEL);
	}
}

static u32 qib_read_7322cntrs(struct qib_devdata *dd, loff_t pos, char **namep,
			      u64 **cntrp)
{
	u32 ret;

	if (namep) {
		ret = dd->cspec->cntrnamelen;
		if (pos >= ret)
			ret = 0; /* final read after getting everything */
		else
			*namep = (char *) cntr7322names;
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
			if (cntr7322indices[i] & _PORT_64BIT_FLAG)
				*cntr++ = read_7322_creg(dd,
							 cntr7322indices[i] &
							 _PORT_CNTR_IDXMASK);
			else
				*cntr++ = read_7322_creg32(dd,
							   cntr7322indices[i]);
	}
done:
	return ret;
}

static u32 qib_read_7322portcntrs(struct qib_devdata *dd, loff_t pos, u32 port,
				  char **namep, u64 **cntrp)
{
	u32 ret;

	if (namep) {
		ret = dd->cspec->portcntrnamelen;
		if (pos >= ret)
			ret = 0; /* final read after getting everything */
		else
			*namep = (char *)portcntr7322names;
	} else {
		struct qib_pportdata *ppd = &dd->pport[port];
		u64 *cntr = ppd->cpspec->portcntrs;
		int i;

		ret = dd->cspec->nportcntrs * sizeof(u64);
		if (!cntr || pos >= ret) {
			/* everything read, or couldn't get memory */
			ret = 0;
			goto done;
		}
		*cntrp = cntr;
		for (i = 0; i < dd->cspec->nportcntrs; i++) {
			if (portcntr7322indices[i] & _PORT_VIRT_FLAG)
				*cntr++ = qib_portcntr_7322(ppd,
					portcntr7322indices[i] &
					_PORT_CNTR_IDXMASK);
			else if (portcntr7322indices[i] & _PORT_64BIT_FLAG)
				*cntr++ = read_7322_creg_port(ppd,
					   portcntr7322indices[i] &
					    _PORT_CNTR_IDXMASK);
			else
				*cntr++ = read_7322_creg32_port(ppd,
					   portcntr7322indices[i]);
		}
	}
done:
	return ret;
}

/**
 * qib_get_7322_faststats - get word counters from chip before they overflow
 * @t: contains a pointer to the qlogic_ib device qib_devdata
 *
 * VESTIGIAL IBA7322 has no "small fast counters", so the only
 * real purpose of this function is to maintain the notion of
 * "active time", which in turn is only logged into the eeprom,
 * which we don;t have, yet, for 7322-based boards.
 *
 * called from add_timer
 */
static void qib_get_7322_faststats(struct timer_list *t)
{
	struct qib_devdata *dd = from_timer(dd, t, stats_timer);
	struct qib_pportdata *ppd;
	unsigned long flags;
	u64 traffic_wds;
	int pidx;

	for (pidx = 0; pidx < dd->num_pports; ++pidx) {
		ppd = dd->pport + pidx;

		/*
		 * If port isn't enabled or not operational ports, or
		 * diags is running (can cause memory diags to fail)
		 * skip this port this time.
		 */
		if (!ppd->link_speed_supported || !(dd->flags & QIB_INITTED)
		    || dd->diag_client)
			continue;

		/*
		 * Maintain an activity timer, based on traffic
		 * exceeding a threshold, so we need to check the word-counts
		 * even if they are 64-bit.
		 */
		traffic_wds = qib_portcntr_7322(ppd, QIBPORTCNTR_WORDRCV) +
			qib_portcntr_7322(ppd, QIBPORTCNTR_WORDSEND);
		spin_lock_irqsave(&ppd->dd->eep_st_lock, flags);
		traffic_wds -= ppd->dd->traffic_wds;
		ppd->dd->traffic_wds += traffic_wds;
		spin_unlock_irqrestore(&ppd->dd->eep_st_lock, flags);
		if (ppd->cpspec->qdr_dfe_on && (ppd->link_speed_active &
						QIB_IB_QDR) &&
		    (ppd->lflags & (QIBL_LINKINIT | QIBL_LINKARMED |
				    QIBL_LINKACTIVE)) &&
		    ppd->cpspec->qdr_dfe_time &&
		    time_is_before_jiffies(ppd->cpspec->qdr_dfe_time)) {
			ppd->cpspec->qdr_dfe_on = 0;

			qib_write_kreg_port(ppd, krp_static_adapt_dis(2),
					    ppd->dd->cspec->r1 ?
					    QDR_STATIC_ADAPT_INIT_R1 :
					    QDR_STATIC_ADAPT_INIT);
			force_h1(ppd);
		}
	}
	mod_timer(&dd->stats_timer, jiffies + HZ * ACTIVITY_TIMER);
}

/*
 * If we were using MSIx, try to fallback to INTx.
 */
static int qib_7322_intr_fallback(struct qib_devdata *dd)
{
	if (!dd->cspec->num_msix_entries)
		return 0; /* already using INTx */

	qib_devinfo(dd->pcidev,
		"MSIx interrupt not detected, trying INTx interrupts\n");
	qib_7322_free_irq(dd);
	if (pci_alloc_irq_vectors(dd->pcidev, 1, 1, PCI_IRQ_LEGACY) < 0)
		qib_dev_err(dd, "Failed to enable INTx\n");
	qib_setup_7322_interrupt(dd, 0);
	return 1;
}

/*
 * Reset the XGXS (between serdes and IBC).  Slightly less intrusive
 * than resetting the IBC or external link state, and useful in some
 * cases to cause some retraining.  To do this right, we reset IBC
 * as well, then return to previous state (which may be still in reset)
 * NOTE: some callers of this "know" this writes the current value
 * of cpspec->ibcctrl_a as part of it's operation, so if that changes,
 * check all callers.
 */
static void qib_7322_mini_pcs_reset(struct qib_pportdata *ppd)
{
	u64 val;
	struct qib_devdata *dd = ppd->dd;
	const u64 reset_bits = SYM_MASK(IBPCSConfig_0, xcv_rreset) |
		SYM_MASK(IBPCSConfig_0, xcv_treset) |
		SYM_MASK(IBPCSConfig_0, tx_rx_reset);

	val = qib_read_kreg_port(ppd, krp_ib_pcsconfig);
	qib_write_kreg(dd, kr_hwerrmask,
		       dd->cspec->hwerrmask & ~HWE_MASK(statusValidNoEop));
	qib_write_kreg_port(ppd, krp_ibcctrl_a,
			    ppd->cpspec->ibcctrl_a &
			    ~SYM_MASK(IBCCtrlA_0, IBLinkEn));

	qib_write_kreg_port(ppd, krp_ib_pcsconfig, val | reset_bits);
	qib_read_kreg32(dd, kr_scratch);
	qib_write_kreg_port(ppd, krp_ib_pcsconfig, val & ~reset_bits);
	qib_write_kreg_port(ppd, krp_ibcctrl_a, ppd->cpspec->ibcctrl_a);
	qib_write_kreg(dd, kr_scratch, 0ULL);
	qib_write_kreg(dd, kr_hwerrclear,
		       SYM_MASK(HwErrClear, statusValidNoEopClear));
	qib_write_kreg(dd, kr_hwerrmask, dd->cspec->hwerrmask);
}

/*
 * This code for non-IBTA-compliant IB speed negotiation is only known to
 * work for the SDR to DDR transition, and only between an HCA and a switch
 * with recent firmware.  It is based on observed heuristics, rather than
 * actual knowledge of the non-compliant speed negotiation.
 * It has a number of hard-coded fields, since the hope is to rewrite this
 * when a spec is available on how the negoation is intended to work.
 */
static void autoneg_7322_sendpkt(struct qib_pportdata *ppd, u32 *hdr,
				 u32 dcnt, u32 *data)
{
	int i;
	u64 pbc;
	u32 __iomem *piobuf;
	u32 pnum, control, len;
	struct qib_devdata *dd = ppd->dd;

	i = 0;
	len = 7 + dcnt + 1; /* 7 dword header, dword data, icrc */
	control = qib_7322_setpbc_control(ppd, len, 0, 15);
	pbc = ((u64) control << 32) | len;
	while (!(piobuf = qib_7322_getsendbuf(ppd, pbc, &pnum))) {
		if (i++ > 15)
			return;
		udelay(2);
	}
	/* disable header check on this packet, since it can't be valid */
	dd->f_txchk_change(dd, pnum, 1, TXCHK_CHG_TYPE_DIS1, NULL);
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
	/* and re-enable hdr check */
	dd->f_txchk_change(dd, pnum, 1, TXCHK_CHG_TYPE_ENAB1, NULL);
}

/*
 * _start packet gets sent twice at start, _done gets sent twice at end
 */
static void qib_autoneg_7322_send(struct qib_pportdata *ppd, int which)
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

	autoneg_7322_sendpkt(ppd, hdr, dcnt, data);
	qib_read_kreg64(dd, kr_scratch);
	udelay(2);
	autoneg_7322_sendpkt(ppd, hdr, dcnt, data);
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
static void set_7322_ibspeed_fast(struct qib_pportdata *ppd, u32 speed)
{
	u64 newctrlb;

	newctrlb = ppd->cpspec->ibcctrl_b & ~(IBA7322_IBC_SPEED_MASK |
				    IBA7322_IBC_IBTA_1_2_MASK |
				    IBA7322_IBC_MAX_SPEED_MASK);

	if (speed & (speed - 1)) /* multiple speeds */
		newctrlb |= (speed << IBA7322_IBC_SPEED_LSB) |
				    IBA7322_IBC_IBTA_1_2_MASK |
				    IBA7322_IBC_MAX_SPEED_MASK;
	else
		newctrlb |= speed == QIB_IB_QDR ?
			IBA7322_IBC_SPEED_QDR | IBA7322_IBC_IBTA_1_2_MASK :
			((speed == QIB_IB_DDR ?
			  IBA7322_IBC_SPEED_DDR : IBA7322_IBC_SPEED_SDR));

	if (newctrlb == ppd->cpspec->ibcctrl_b)
		return;

	ppd->cpspec->ibcctrl_b = newctrlb;
	qib_write_kreg_port(ppd, krp_ibcctrl_b, ppd->cpspec->ibcctrl_b);
	qib_write_kreg(ppd->dd, kr_scratch, 0);
}

/*
 * This routine is only used when we are not talking to another
 * IB 1.2-compliant device that we think can do DDR.
 * (This includes all existing switch chips as of Oct 2007.)
 * 1.2-compliant devices go directly to DDR prior to reaching INIT
 */
static void try_7322_autoneg(struct qib_pportdata *ppd)
{
	unsigned long flags;

	spin_lock_irqsave(&ppd->lflags_lock, flags);
	ppd->lflags |= QIBL_IB_AUTONEG_INPROG;
	spin_unlock_irqrestore(&ppd->lflags_lock, flags);
	qib_autoneg_7322_send(ppd, 0);
	set_7322_ibspeed_fast(ppd, QIB_IB_DDR);
	qib_7322_mini_pcs_reset(ppd);
	/* 2 msec is minimum length of a poll cycle */
	queue_delayed_work(ib_wq, &ppd->cpspec->autoneg_work,
			   msecs_to_jiffies(2));
}

/*
 * Handle the empirically determined mechanism for auto-negotiation
 * of DDR speed with switches.
 */
static void autoneg_7322_work(struct work_struct *work)
{
	struct qib_pportdata *ppd;
	u32 i;
	unsigned long flags;

	ppd = container_of(work, struct qib_chippport_specific,
			    autoneg_work.work)->ppd;

	/*
	 * Busy wait for this first part, it should be at most a
	 * few hundred usec, since we scheduled ourselves for 2msec.
	 */
	for (i = 0; i < 25; i++) {
		if (SYM_FIELD(ppd->lastibcstat, IBCStatusA_0, LinkState)
		     == IB_7322_LT_STATE_POLLQUIET) {
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
	qib_7322_mini_pcs_reset(ppd);

	/* we expect this to timeout */
	if (wait_event_timeout(ppd->cpspec->autoneg_wait,
			       !(ppd->lflags & QIBL_IB_AUTONEG_INPROG),
			       msecs_to_jiffies(1700)))
		goto done;
	qib_7322_mini_pcs_reset(ppd);

	set_7322_ibspeed_fast(ppd, QIB_IB_SDR);

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
		if (ppd->cpspec->autoneg_tries == AUTONEG_TRIES) {
			ppd->lflags |= QIBL_IB_AUTONEG_FAILED;
			ppd->cpspec->autoneg_tries = 0;
		}
		spin_unlock_irqrestore(&ppd->lflags_lock, flags);
		set_7322_ibspeed_fast(ppd, ppd->link_speed_enabled);
	}
}

/*
 * This routine is used to request IPG set in the QLogic switch.
 * Only called if r1.
 */
static void try_7322_ipg(struct qib_pportdata *ppd)
{
	struct qib_ibport *ibp = &ppd->ibport_data;
	struct ib_mad_send_buf *send_buf;
	struct ib_mad_agent *agent;
	struct ib_smp *smp;
	unsigned delay;
	int ret;

	agent = ibp->rvp.send_agent;
	if (!agent)
		goto retry;

	send_buf = ib_create_send_mad(agent, 0, 0, 0, IB_MGMT_MAD_HDR,
				      IB_MGMT_MAD_DATA, GFP_ATOMIC,
				      IB_MGMT_BASE_VERSION);
	if (IS_ERR(send_buf))
		goto retry;

	if (!ibp->smi_ah) {
		struct ib_ah *ah;

		ah = qib_create_qp0_ah(ibp, be16_to_cpu(IB_LID_PERMISSIVE));
		if (IS_ERR(ah))
			ret = PTR_ERR(ah);
		else {
			send_buf->ah = ah;
			ibp->smi_ah = ibah_to_rvtah(ah);
			ret = 0;
		}
	} else {
		send_buf->ah = &ibp->smi_ah->ibah;
		ret = 0;
	}

	smp = send_buf->mad;
	smp->base_version = IB_MGMT_BASE_VERSION;
	smp->mgmt_class = IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE;
	smp->class_version = 1;
	smp->method = IB_MGMT_METHOD_SEND;
	smp->hop_cnt = 1;
	smp->attr_id = QIB_VENDOR_IPG;
	smp->attr_mod = 0;

	if (!ret)
		ret = ib_post_send_mad(send_buf, NULL);
	if (ret)
		ib_free_send_mad(send_buf);
retry:
	delay = 2 << ppd->cpspec->ipg_tries;
	queue_delayed_work(ib_wq, &ppd->cpspec->ipg_work,
			   msecs_to_jiffies(delay));
}

/*
 * Timeout handler for setting IPG.
 * Only called if r1.
 */
static void ipg_7322_work(struct work_struct *work)
{
	struct qib_pportdata *ppd;

	ppd = container_of(work, struct qib_chippport_specific,
			   ipg_work.work)->ppd;
	if ((ppd->lflags & (QIBL_LINKINIT | QIBL_LINKARMED | QIBL_LINKACTIVE))
	    && ++ppd->cpspec->ipg_tries <= 10)
		try_7322_ipg(ppd);
}

static u32 qib_7322_iblink_state(u64 ibcs)
{
	u32 state = (u32)SYM_FIELD(ibcs, IBCStatusA_0, LinkState);

	switch (state) {
	case IB_7322_L_STATE_INIT:
		state = IB_PORT_INIT;
		break;
	case IB_7322_L_STATE_ARM:
		state = IB_PORT_ARMED;
		break;
	case IB_7322_L_STATE_ACTIVE:
	case IB_7322_L_STATE_ACT_DEFER:
		state = IB_PORT_ACTIVE;
		break;
	default:
		fallthrough;
	case IB_7322_L_STATE_DOWN:
		state = IB_PORT_DOWN;
		break;
	}
	return state;
}

/* returns the IBTA port state, rather than the IBC link training state */
static u8 qib_7322_phys_portstate(u64 ibcs)
{
	u8 state = (u8)SYM_FIELD(ibcs, IBCStatusA_0, LinkTrainingState);
	return qib_7322_physportstate[state];
}

static int qib_7322_ib_updown(struct qib_pportdata *ppd, int ibup, u64 ibcs)
{
	int ret = 0, symadj = 0;
	unsigned long flags;
	int mult;

	spin_lock_irqsave(&ppd->lflags_lock, flags);
	ppd->lflags &= ~QIBL_IB_FORCE_NOTIFY;
	spin_unlock_irqrestore(&ppd->lflags_lock, flags);

	/* Update our picture of width and speed from chip */
	if (ibcs & SYM_MASK(IBCStatusA_0, LinkSpeedQDR)) {
		ppd->link_speed_active = QIB_IB_QDR;
		mult = 4;
	} else if (ibcs & SYM_MASK(IBCStatusA_0, LinkSpeedActive)) {
		ppd->link_speed_active = QIB_IB_DDR;
		mult = 2;
	} else {
		ppd->link_speed_active = QIB_IB_SDR;
		mult = 1;
	}
	if (ibcs & SYM_MASK(IBCStatusA_0, LinkWidthActive)) {
		ppd->link_width_active = IB_WIDTH_4X;
		mult *= 4;
	} else
		ppd->link_width_active = IB_WIDTH_1X;
	ppd->delay_mult = ib_rate_to_delay[mult_to_ib_rate(mult)];

	if (!ibup) {
		u64 clr;

		/* Link went down. */
		/* do IPG MAD again after linkdown, even if last time failed */
		ppd->cpspec->ipg_tries = 0;
		clr = qib_read_kreg_port(ppd, krp_ibcstatus_b) &
			(SYM_MASK(IBCStatusB_0, heartbeat_timed_out) |
			 SYM_MASK(IBCStatusB_0, heartbeat_crosstalk));
		if (clr)
			qib_write_kreg_port(ppd, krp_ibcstatus_b, clr);
		if (!(ppd->lflags & (QIBL_IB_AUTONEG_FAILED |
				     QIBL_IB_AUTONEG_INPROG)))
			set_7322_ibspeed_fast(ppd, ppd->link_speed_enabled);
		if (!(ppd->lflags & QIBL_IB_AUTONEG_INPROG)) {
			struct qib_qsfp_data *qd =
				&ppd->cpspec->qsfp_data;
			/* unlock the Tx settings, speed may change */
			qib_write_kreg_port(ppd, krp_tx_deemph_override,
				SYM_MASK(IBSD_TX_DEEMPHASIS_OVERRIDE_0,
				reset_tx_deemphasis_override));
			qib_cancel_sends(ppd);
			/* on link down, ensure sane pcs state */
			qib_7322_mini_pcs_reset(ppd);
			/* schedule the qsfp refresh which should turn the link
			   off */
			if (ppd->dd->flags & QIB_HAS_QSFP) {
				qd->t_insert = jiffies;
				queue_work(ib_wq, &qd->work);
			}
			spin_lock_irqsave(&ppd->sdma_lock, flags);
			if (__qib_sdma_running(ppd))
				__qib_sdma_process_event(ppd,
					qib_sdma_event_e70_go_idle);
			spin_unlock_irqrestore(&ppd->sdma_lock, flags);
		}
		clr = read_7322_creg32_port(ppd, crp_iblinkdown);
		if (clr == ppd->cpspec->iblnkdownsnap)
			ppd->cpspec->iblnkdowndelta++;
	} else {
		if (qib_compat_ddr_negotiate &&
		    !(ppd->lflags & (QIBL_IB_AUTONEG_FAILED |
				     QIBL_IB_AUTONEG_INPROG)) &&
		    ppd->link_speed_active == QIB_IB_SDR &&
		    (ppd->link_speed_enabled & QIB_IB_DDR)
		    && ppd->cpspec->autoneg_tries < AUTONEG_TRIES) {
			/* we are SDR, and auto-negotiation enabled */
			++ppd->cpspec->autoneg_tries;
			if (!ppd->cpspec->ibdeltainprog) {
				ppd->cpspec->ibdeltainprog = 1;
				ppd->cpspec->ibsymdelta +=
					read_7322_creg32_port(ppd,
						crp_ibsymbolerr) -
						ppd->cpspec->ibsymsnap;
				ppd->cpspec->iblnkerrdelta +=
					read_7322_creg32_port(ppd,
						crp_iblinkerrrecov) -
						ppd->cpspec->iblnkerrsnap;
			}
			try_7322_autoneg(ppd);
			ret = 1; /* no other IB status change processing */
		} else if ((ppd->lflags & QIBL_IB_AUTONEG_INPROG) &&
			   ppd->link_speed_active == QIB_IB_SDR) {
			qib_autoneg_7322_send(ppd, 1);
			set_7322_ibspeed_fast(ppd, QIB_IB_DDR);
			qib_7322_mini_pcs_reset(ppd);
			udelay(2);
			ret = 1; /* no other IB status change processing */
		} else if ((ppd->lflags & QIBL_IB_AUTONEG_INPROG) &&
			   (ppd->link_speed_active & QIB_IB_DDR)) {
			spin_lock_irqsave(&ppd->lflags_lock, flags);
			ppd->lflags &= ~(QIBL_IB_AUTONEG_INPROG |
					 QIBL_IB_AUTONEG_FAILED);
			spin_unlock_irqrestore(&ppd->lflags_lock, flags);
			ppd->cpspec->autoneg_tries = 0;
			/* re-enable SDR, for next link down */
			set_7322_ibspeed_fast(ppd, ppd->link_speed_enabled);
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
			spin_unlock_irqrestore(&ppd->lflags_lock, flags);
			ppd->cpspec->ibcctrl_b |= IBA7322_IBC_IBTA_1_2_MASK;
			symadj = 1;
		}
		if (!(ppd->lflags & QIBL_IB_AUTONEG_INPROG)) {
			symadj = 1;
			if (ppd->dd->cspec->r1 && ppd->cpspec->ipg_tries <= 10)
				try_7322_ipg(ppd);
			if (!ppd->cpspec->recovery_init)
				setup_7322_link_recovery(ppd, 0);
			ppd->cpspec->qdr_dfe_time = jiffies +
				msecs_to_jiffies(QDR_DFE_DISABLE_DELAY);
		}
		ppd->cpspec->ibmalfusesnap = 0;
		ppd->cpspec->ibmalfsnap = read_7322_creg32_port(ppd,
			crp_errlink);
	}
	if (symadj) {
		ppd->cpspec->iblnkdownsnap =
			read_7322_creg32_port(ppd, crp_iblinkdown);
		if (ppd->cpspec->ibdeltainprog) {
			ppd->cpspec->ibdeltainprog = 0;
			ppd->cpspec->ibsymdelta += read_7322_creg32_port(ppd,
				crp_ibsymbolerr) - ppd->cpspec->ibsymsnap;
			ppd->cpspec->iblnkerrdelta += read_7322_creg32_port(ppd,
				crp_iblinkerrrecov) - ppd->cpspec->iblnkerrsnap;
		}
	} else if (!ibup && qib_compat_ddr_negotiate &&
		   !ppd->cpspec->ibdeltainprog &&
			!(ppd->lflags & QIBL_IB_AUTONEG_INPROG)) {
		ppd->cpspec->ibdeltainprog = 1;
		ppd->cpspec->ibsymsnap = read_7322_creg32_port(ppd,
			crp_ibsymbolerr);
		ppd->cpspec->iblnkerrsnap = read_7322_creg32_port(ppd,
			crp_iblinkerrrecov);
	}

	if (!ret)
		qib_setup_7322_setextled(ppd, ibup);
	return ret;
}

/*
 * Does read/modify/write to appropriate registers to
 * set output and direction bits selected by mask.
 * these are in their canonical positions (e.g. lsb of
 * dir will end up in D48 of extctrl on existing chips).
 * returns contents of GP Inputs.
 */
static int gpio_7322_mod(struct qib_devdata *dd, u32 out, u32 dir, u32 mask)
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

/* Enable writes to config EEPROM, if possible. Returns previous state */
static int qib_7322_eeprom_wen(struct qib_devdata *dd, int wen)
{
	int prev_wen;
	u32 mask;

	mask = 1 << QIB_EEPROM_WEN_NUM;
	prev_wen = ~gpio_7322_mod(dd, 0, 0, 0) >> QIB_EEPROM_WEN_NUM;
	gpio_7322_mod(dd, wen ? 0 : mask, mask, mask);

	return prev_wen & 1;
}

/*
 * Read fundamental info we need to use the chip.  These are
 * the registers that describe chip capabilities, and are
 * saved in shadow registers.
 */
static void get_7322_chip_params(struct qib_devdata *dd)
{
	u64 val;
	u32 piobufs;
	int mtu;

	dd->palign = qib_read_kreg32(dd, kr_pagealign);

	dd->uregbase = qib_read_kreg32(dd, kr_userregbase);

	dd->rcvtidcnt = qib_read_kreg32(dd, kr_rcvtidcnt);
	dd->rcvtidbase = qib_read_kreg32(dd, kr_rcvtidbase);
	dd->rcvegrbase = qib_read_kreg32(dd, kr_rcvegrbase);
	dd->piobufbase = qib_read_kreg64(dd, kr_sendpiobufbase);
	dd->pio2k_bufbase = dd->piobufbase & 0xffffffff;

	val = qib_read_kreg64(dd, kr_sendpiobufcnt);
	dd->piobcnt2k = val & ~0U;
	dd->piobcnt4k = val >> 32;
	val = qib_read_kreg64(dd, kr_sendpiosize);
	dd->piosize2k = val & ~0U;
	dd->piosize4k = val >> 32;

	mtu = ib_mtu_enum_to_int(qib_ibmtu);
	if (mtu == -1)
		mtu = QIB_DEFAULT_MTU;
	dd->pport[0].ibmtu = (u32)mtu;
	dd->pport[1].ibmtu = (u32)mtu;

	/* these may be adjusted in init_chip_wc_pat() */
	dd->pio2kbase = (u32 __iomem *)
		((char __iomem *) dd->kregbase + dd->pio2k_bufbase);
	dd->pio4kbase = (u32 __iomem *)
		((char __iomem *) dd->kregbase +
		 (dd->piobufbase >> 32));
	/*
	 * 4K buffers take 2 pages; we use roundup just to be
	 * paranoid; we calculate it once here, rather than on
	 * ever buf allocate
	 */
	dd->align4k = ALIGN(dd->piosize4k, dd->palign);

	piobufs = dd->piobcnt4k + dd->piobcnt2k + NUM_VL15_BUFS;

	dd->pioavregs = ALIGN(piobufs, sizeof(u64) * BITS_PER_BYTE / 2) /
		(sizeof(u64) * BITS_PER_BYTE / 2);
}

/*
 * The chip base addresses in cspec and cpspec have to be set
 * after possible init_chip_wc_pat(), rather than in
 * get_7322_chip_params(), so split out as separate function
 */
static void qib_7322_set_baseaddrs(struct qib_devdata *dd)
{
	u32 cregbase;

	cregbase = qib_read_kreg32(dd, kr_counterregbase);

	dd->cspec->cregbase = (u64 __iomem *)(cregbase +
		(char __iomem *)dd->kregbase);

	dd->egrtidbase = (u64 __iomem *)
		((char __iomem *) dd->kregbase + dd->rcvegrbase);

	/* port registers are defined as relative to base of chip */
	dd->pport[0].cpspec->kpregbase =
		(u64 __iomem *)((char __iomem *)dd->kregbase);
	dd->pport[1].cpspec->kpregbase =
		(u64 __iomem *)(dd->palign +
		(char __iomem *)dd->kregbase);
	dd->pport[0].cpspec->cpregbase =
		(u64 __iomem *)(qib_read_kreg_port(&dd->pport[0],
		kr_counterregbase) + (char __iomem *)dd->kregbase);
	dd->pport[1].cpspec->cpregbase =
		(u64 __iomem *)(qib_read_kreg_port(&dd->pport[1],
		kr_counterregbase) + (char __iomem *)dd->kregbase);
}

/*
 * This is a fairly special-purpose observer, so we only support
 * the port-specific parts of SendCtrl
 */

#define SENDCTRL_SHADOWED (SYM_MASK(SendCtrl_0, SendEnable) |		\
			   SYM_MASK(SendCtrl_0, SDmaEnable) |		\
			   SYM_MASK(SendCtrl_0, SDmaIntEnable) |	\
			   SYM_MASK(SendCtrl_0, SDmaSingleDescriptor) | \
			   SYM_MASK(SendCtrl_0, SDmaHalt) |		\
			   SYM_MASK(SendCtrl_0, IBVLArbiterEn) |	\
			   SYM_MASK(SendCtrl_0, ForceCreditUpToDate))

static int sendctrl_hook(struct qib_devdata *dd,
			 const struct diag_observer *op, u32 offs,
			 u64 *data, u64 mask, int only_32)
{
	unsigned long flags;
	unsigned idx;
	unsigned pidx;
	struct qib_pportdata *ppd = NULL;
	u64 local_data, all_bits;

	/*
	 * The fixed correspondence between Physical ports and pports is
	 * severed. We need to hunt for the ppd that corresponds
	 * to the offset we got. And we have to do that without admitting
	 * we know the stride, apparently.
	 */
	for (pidx = 0; pidx < dd->num_pports; ++pidx) {
		u64 __iomem *psptr;
		u32 psoffs;

		ppd = dd->pport + pidx;
		if (!ppd->cpspec->kpregbase)
			continue;

		psptr = ppd->cpspec->kpregbase + krp_sendctrl;
		psoffs = (u32) (psptr - dd->kregbase) * sizeof(*psptr);
		if (psoffs == offs)
			break;
	}

	/* If pport is not being managed by driver, just avoid shadows. */
	if (pidx >= dd->num_pports)
		ppd = NULL;

	/* In any case, "idx" is flat index in kreg space */
	idx = offs / sizeof(u64);

	all_bits = ~0ULL;
	if (only_32)
		all_bits >>= 32;

	spin_lock_irqsave(&dd->sendctrl_lock, flags);
	if (!ppd || (mask & all_bits) != all_bits) {
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
		if (ppd) {
			sval = ppd->p_sendctrl & ~mask;
			sval |= *data & SENDCTRL_SHADOWED & mask;
			ppd->p_sendctrl = sval;
		} else
			sval = *data & SENDCTRL_SHADOWED & mask;
		tval = sval | (*data & ~SENDCTRL_SHADOWED & mask);
		qib_write_kreg(dd, idx, tval);
		qib_write_kreg(dd, kr_scratch, 0Ull);
	}
	spin_unlock_irqrestore(&dd->sendctrl_lock, flags);
	return only_32 ? 4 : 8;
}

static const struct diag_observer sendctrl_0_observer = {
	sendctrl_hook, KREG_IDX(SendCtrl_0) * sizeof(u64),
	KREG_IDX(SendCtrl_0) * sizeof(u64)
};

static const struct diag_observer sendctrl_1_observer = {
	sendctrl_hook, KREG_IDX(SendCtrl_1) * sizeof(u64),
	KREG_IDX(SendCtrl_1) * sizeof(u64)
};

static ushort sdma_fetch_prio = 8;
module_param_named(sdma_fetch_prio, sdma_fetch_prio, ushort, S_IRUGO);
MODULE_PARM_DESC(sdma_fetch_prio, "SDMA descriptor fetch priority");

/* Besides logging QSFP events, we set appropriate TxDDS values */
static void init_txdds_table(struct qib_pportdata *ppd, int override);

static void qsfp_7322_event(struct work_struct *work)
{
	struct qib_qsfp_data *qd;
	struct qib_pportdata *ppd;
	unsigned long pwrup;
	unsigned long flags;
	int ret;
	u32 le2;

	qd = container_of(work, struct qib_qsfp_data, work);
	ppd = qd->ppd;
	pwrup = qd->t_insert +
		msecs_to_jiffies(QSFP_PWR_LAG_MSEC - QSFP_MODPRS_LAG_MSEC);

	/* Delay for 20 msecs to allow ModPrs resistor to setup */
	mdelay(QSFP_MODPRS_LAG_MSEC);

	if (!qib_qsfp_mod_present(ppd)) {
		ppd->cpspec->qsfp_data.modpresent = 0;
		/* Set the physical link to disabled */
		qib_set_ib_7322_lstate(ppd, 0,
				       QLOGIC_IB_IBCC_LINKINITCMD_DISABLE);
		spin_lock_irqsave(&ppd->lflags_lock, flags);
		ppd->lflags &= ~QIBL_LINKV;
		spin_unlock_irqrestore(&ppd->lflags_lock, flags);
	} else {
		/*
		 * Some QSFP's not only do not respond until the full power-up
		 * time, but may behave badly if we try. So hold off responding
		 * to insertion.
		 */
		while (1) {
			if (time_is_before_jiffies(pwrup))
				break;
			msleep(20);
		}

		ret = qib_refresh_qsfp_cache(ppd, &qd->cache);

		/*
		 * Need to change LE2 back to defaults if we couldn't
		 * read the cable type (to handle cable swaps), so do this
		 * even on failure to read cable information.  We don't
		 * get here for QME, so IS_QME check not needed here.
		 */
		if (!ret && !ppd->dd->cspec->r1) {
			if (QSFP_IS_ACTIVE_FAR(qd->cache.tech))
				le2 = LE2_QME;
			else if (qd->cache.atten[1] >= qib_long_atten &&
				 QSFP_IS_CU(qd->cache.tech))
				le2 = LE2_5m;
			else
				le2 = LE2_DEFAULT;
		} else
			le2 = LE2_DEFAULT;
		ibsd_wr_allchans(ppd, 13, (le2 << 7), BMASK(9, 7));
		/*
		 * We always change parameteters, since we can choose
		 * values for cables without eeproms, and the cable may have
		 * changed from a cable with full or partial eeprom content
		 * to one with partial or no content.
		 */
		init_txdds_table(ppd, 0);
		/* The physical link is being re-enabled only when the
		 * previous state was DISABLED and the VALID bit is not
		 * set. This should only happen when  the cable has been
		 * physically pulled. */
		if (!ppd->cpspec->qsfp_data.modpresent &&
		    (ppd->lflags & (QIBL_LINKV | QIBL_IB_LINK_DISABLED))) {
			ppd->cpspec->qsfp_data.modpresent = 1;
			qib_set_ib_7322_lstate(ppd, 0,
				QLOGIC_IB_IBCC_LINKINITCMD_SLEEP);
			spin_lock_irqsave(&ppd->lflags_lock, flags);
			ppd->lflags |= QIBL_LINKV;
			spin_unlock_irqrestore(&ppd->lflags_lock, flags);
		}
	}
}

/*
 * There is little we can do but complain to the user if QSFP
 * initialization fails.
 */
static void qib_init_7322_qsfp(struct qib_pportdata *ppd)
{
	unsigned long flags;
	struct qib_qsfp_data *qd = &ppd->cpspec->qsfp_data;
	struct qib_devdata *dd = ppd->dd;
	u64 mod_prs_bit = QSFP_GPIO_MOD_PRS_N;

	mod_prs_bit <<= (QSFP_GPIO_PORT2_SHIFT * ppd->hw_pidx);
	qd->ppd = ppd;
	qib_qsfp_init(qd, qsfp_7322_event);
	spin_lock_irqsave(&dd->cspec->gpio_lock, flags);
	dd->cspec->extctrl |= (mod_prs_bit << SYM_LSB(EXTCtrl, GPIOInvert));
	dd->cspec->gpio_mask |= mod_prs_bit;
	qib_write_kreg(dd, kr_extctrl, dd->cspec->extctrl);
	qib_write_kreg(dd, kr_gpio_mask, dd->cspec->gpio_mask);
	spin_unlock_irqrestore(&dd->cspec->gpio_lock, flags);
}

/*
 * called at device initialization time, and also if the txselect
 * module parameter is changed.  This is used for cables that don't
 * have valid QSFP EEPROMs (not present, or attenuation is zero).
 * We initialize to the default, then if there is a specific
 * unit,port match, we use that (and set it immediately, for the
 * current speed, if the link is at INIT or better).
 * String format is "default# unit#,port#=# ... u,p=#", separators must
 * be a SPACE character.  A newline terminates.  The u,p=# tuples may
 * optionally have "u,p=#,#", where the final # is the H1 value
 * The last specific match is used (actually, all are used, but last
 * one is the one that winds up set); if none at all, fall back on default.
 */
static void set_no_qsfp_atten(struct qib_devdata *dd, int change)
{
	char *nxt, *str;
	u32 pidx, unit, port, deflt, h1;
	unsigned long val;
	int any = 0, seth1;
	int txdds_size;

	str = txselect_list;

	/* default number is validated in setup_txselect() */
	deflt = simple_strtoul(str, &nxt, 0);
	for (pidx = 0; pidx < dd->num_pports; ++pidx)
		dd->pport[pidx].cpspec->no_eep = deflt;

	txdds_size = TXDDS_TABLE_SZ + TXDDS_EXTRA_SZ;
	if (IS_QME(dd) || IS_QMH(dd))
		txdds_size += TXDDS_MFG_SZ;

	while (*nxt && nxt[1]) {
		str = ++nxt;
		unit = simple_strtoul(str, &nxt, 0);
		if (nxt == str || !*nxt || *nxt != ',') {
			while (*nxt && *nxt++ != ' ') /* skip to next, if any */
				;
			continue;
		}
		str = ++nxt;
		port = simple_strtoul(str, &nxt, 0);
		if (nxt == str || *nxt != '=') {
			while (*nxt && *nxt++ != ' ') /* skip to next, if any */
				;
			continue;
		}
		str = ++nxt;
		val = simple_strtoul(str, &nxt, 0);
		if (nxt == str) {
			while (*nxt && *nxt++ != ' ') /* skip to next, if any */
				;
			continue;
		}
		if (val >= txdds_size)
			continue;
		seth1 = 0;
		h1 = 0; /* gcc thinks it might be used uninitted */
		if (*nxt == ',' && nxt[1]) {
			str = ++nxt;
			h1 = (u32)simple_strtoul(str, &nxt, 0);
			if (nxt == str)
				while (*nxt && *nxt++ != ' ') /* skip */
					;
			else
				seth1 = 1;
		}
		for (pidx = 0; dd->unit == unit && pidx < dd->num_pports;
		     ++pidx) {
			struct qib_pportdata *ppd = &dd->pport[pidx];

			if (ppd->port != port || !ppd->link_speed_supported)
				continue;
			ppd->cpspec->no_eep = val;
			if (seth1)
				ppd->cpspec->h1_val = h1;
			/* now change the IBC and serdes, overriding generic */
			init_txdds_table(ppd, 1);
			/* Re-enable the physical state machine on mezz boards
			 * now that the correct settings have been set.
			 * QSFP boards are handles by the QSFP event handler */
			if (IS_QMH(dd) || IS_QME(dd))
				qib_set_ib_7322_lstate(ppd, 0,
					    QLOGIC_IB_IBCC_LINKINITCMD_SLEEP);
			any++;
		}
		if (*nxt == '\n')
			break; /* done */
	}
	if (change && !any) {
		/* no specific setting, use the default.
		 * Change the IBC and serdes, but since it's
		 * general, don't override specific settings.
		 */
		for (pidx = 0; pidx < dd->num_pports; ++pidx)
			if (dd->pport[pidx].link_speed_supported)
				init_txdds_table(&dd->pport[pidx], 0);
	}
}

/* handle the txselect parameter changing */
static int setup_txselect(const char *str, const struct kernel_param *kp)
{
	struct qib_devdata *dd;
	unsigned long index, val;
	char *n;

	if (strlen(str) >= ARRAY_SIZE(txselect_list)) {
		pr_info("txselect_values string too long\n");
		return -ENOSPC;
	}
	val = simple_strtoul(str, &n, 0);
	if (n == str || val >= (TXDDS_TABLE_SZ + TXDDS_EXTRA_SZ +
				TXDDS_MFG_SZ)) {
		pr_info("txselect_values must start with a number < %d\n",
			TXDDS_TABLE_SZ + TXDDS_EXTRA_SZ + TXDDS_MFG_SZ);
		return -EINVAL;
	}
	strncpy(txselect_list, str, ARRAY_SIZE(txselect_list) - 1);

	xa_for_each(&qib_dev_table, index, dd)
		if (dd->deviceid == PCI_DEVICE_ID_QLOGIC_IB_7322)
			set_no_qsfp_atten(dd, 1);
	return 0;
}

/*
 * Write the final few registers that depend on some of the
 * init setup.  Done late in init, just before bringing up
 * the serdes.
 */
static int qib_late_7322_initreg(struct qib_devdata *dd)
{
	int ret = 0, n;
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

	n = dd->piobcnt2k + dd->piobcnt4k + NUM_VL15_BUFS;
	qib_7322_txchk_change(dd, 0, n, TXCHK_CHG_TYPE_KERN, NULL);
	/* driver sends get pkey, lid, etc. checking also, to catch bugs */
	qib_7322_txchk_change(dd, 0, n, TXCHK_CHG_TYPE_ENAB1, NULL);

	qib_register_observer(dd, &sendctrl_0_observer);
	qib_register_observer(dd, &sendctrl_1_observer);

	dd->control &= ~QLOGIC_IB_C_SDMAFETCHPRIOEN;
	qib_write_kreg(dd, kr_control, dd->control);
	/*
	 * Set SendDmaFetchPriority and init Tx params, including
	 * QSFP handler on boards that have QSFP.
	 * First set our default attenuation entry for cables that
	 * don't have valid attenuation.
	 */
	set_no_qsfp_atten(dd, 0);
	for (n = 0; n < dd->num_pports; ++n) {
		struct qib_pportdata *ppd = dd->pport + n;

		qib_write_kreg_port(ppd, krp_senddmaprioritythld,
				    sdma_fetch_prio & 0xf);
		/* Initialize qsfp if present on board. */
		if (dd->flags & QIB_HAS_QSFP)
			qib_init_7322_qsfp(ppd);
	}
	dd->control |= QLOGIC_IB_C_SDMAFETCHPRIOEN;
	qib_write_kreg(dd, kr_control, dd->control);

	return ret;
}

/* per IB port errors.  */
#define SENDCTRL_PIBP (MASK_ACROSS(0, 1) | MASK_ACROSS(3, 3) | \
	MASK_ACROSS(8, 15))
#define RCVCTRL_PIBP (MASK_ACROSS(0, 17) | MASK_ACROSS(39, 41))
#define ERRS_PIBP (MASK_ACROSS(57, 58) | MASK_ACROSS(54, 54) | \
	MASK_ACROSS(36, 49) | MASK_ACROSS(29, 34) | MASK_ACROSS(14, 17) | \
	MASK_ACROSS(0, 11))

/*
 * Write the initialization per-port registers that need to be done at
 * driver load and after reset completes (i.e., that aren't done as part
 * of other init procedures called from qib_init.c).
 * Some of these should be redundant on reset, but play safe.
 */
static void write_7322_init_portregs(struct qib_pportdata *ppd)
{
	u64 val;
	int i;

	if (!ppd->link_speed_supported) {
		/* no buffer credits for this port */
		for (i = 1; i < 8; i++)
			qib_write_kreg_port(ppd, krp_rxcreditvl0 + i, 0);
		qib_write_kreg_port(ppd, krp_ibcctrl_b, 0);
		qib_write_kreg(ppd->dd, kr_scratch, 0);
		return;
	}

	/*
	 * Set the number of supported virtual lanes in IBC,
	 * for flow control packet handling on unsupported VLs
	 */
	val = qib_read_kreg_port(ppd, krp_ibsdtestiftx);
	val &= ~SYM_MASK(IB_SDTEST_IF_TX_0, VL_CAP);
	val |= (u64)(ppd->vls_supported - 1) <<
		SYM_LSB(IB_SDTEST_IF_TX_0, VL_CAP);
	qib_write_kreg_port(ppd, krp_ibsdtestiftx, val);

	qib_write_kreg_port(ppd, krp_rcvbthqp, QIB_KD_QP);

	/* enable tx header checking */
	qib_write_kreg_port(ppd, krp_sendcheckcontrol, IBA7322_SENDCHK_PKEY |
			    IBA7322_SENDCHK_BTHQP | IBA7322_SENDCHK_SLID |
			    IBA7322_SENDCHK_RAW_IPV6 | IBA7322_SENDCHK_MINSZ);

	qib_write_kreg_port(ppd, krp_ncmodectrl,
		SYM_MASK(IBNCModeCtrl_0, ScrambleCapLocal));

	/*
	 * Unconditionally clear the bufmask bits.  If SDMA is
	 * enabled, we'll set them appropriately later.
	 */
	qib_write_kreg_port(ppd, krp_senddmabufmask0, 0);
	qib_write_kreg_port(ppd, krp_senddmabufmask1, 0);
	qib_write_kreg_port(ppd, krp_senddmabufmask2, 0);
	if (ppd->dd->cspec->r1)
		ppd->p_sendctrl |= SYM_MASK(SendCtrl_0, ForceCreditUpToDate);
}

/*
 * Write the initialization per-device registers that need to be done at
 * driver load and after reset completes (i.e., that aren't done as part
 * of other init procedures called from qib_init.c).  Also write per-port
 * registers that are affected by overall device config, such as QP mapping
 * Some of these should be redundant on reset, but play safe.
 */
static void write_7322_initregs(struct qib_devdata *dd)
{
	struct qib_pportdata *ppd;
	int i, pidx;
	u64 val;

	/* Set Multicast QPs received by port 2 to map to context one. */
	qib_write_kreg(dd, KREG_IDX(RcvQPMulticastContext_1), 1);

	for (pidx = 0; pidx < dd->num_pports; ++pidx) {
		unsigned n, regno;
		unsigned long flags;

		if (dd->n_krcv_queues < 2 ||
			!dd->pport[pidx].link_speed_supported)
			continue;

		ppd = &dd->pport[pidx];

		/* be paranoid against later code motion, etc. */
		spin_lock_irqsave(&dd->cspec->rcvmod_lock, flags);
		ppd->p_rcvctrl |= SYM_MASK(RcvCtrl_0, RcvQPMapEnable);
		spin_unlock_irqrestore(&dd->cspec->rcvmod_lock, flags);

		/* Initialize QP to context mapping */
		regno = krp_rcvqpmaptable;
		val = 0;
		if (dd->num_pports > 1)
			n = dd->first_user_ctxt / dd->num_pports;
		else
			n = dd->first_user_ctxt - 1;
		for (i = 0; i < 32; ) {
			unsigned ctxt;

			if (dd->num_pports > 1)
				ctxt = (i % n) * dd->num_pports + pidx;
			else if (i % n)
				ctxt = (i % n) + 1;
			else
				ctxt = ppd->hw_pidx;
			val |= ctxt << (5 * (i % 6));
			i++;
			if (i % 6 == 0) {
				qib_write_kreg_port(ppd, regno, val);
				val = 0;
				regno++;
			}
		}
		qib_write_kreg_port(ppd, regno, val);
	}

	/*
	 * Setup up interrupt mitigation for kernel contexts, but
	 * not user contexts (user contexts use interrupts when
	 * stalled waiting for any packet, so want those interrupts
	 * right away).
	 */
	for (i = 0; i < dd->first_user_ctxt; i++) {
		dd->cspec->rcvavail_timeout[i] = rcv_int_timeout;
		qib_write_kreg(dd, kr_rcvavailtimeout + i, rcv_int_timeout);
	}

	/*
	 * Initialize  as (disabled) rcvflow tables.  Application code
	 * will setup each flow as it uses the flow.
	 * Doesn't clear any of the error bits that might be set.
	 */
	val = TIDFLOW_ERRBITS; /* these are W1C */
	for (i = 0; i < dd->cfgctxts; i++) {
		int flow;

		for (flow = 0; flow < NUM_TIDFLOWS_CTXT; flow++)
			qib_write_ureg(dd, ur_rcvflowtable+flow, val, i);
	}

	/*
	 * dual cards init to dual port recovery, single port cards to
	 * the one port.  Dual port cards may later adjust to 1 port,
	 * and then back to dual port if both ports are connected
	 * */
	if (dd->num_pports)
		setup_7322_link_recovery(dd->pport, dd->num_pports > 1);
}

static int qib_init_7322_variables(struct qib_devdata *dd)
{
	struct qib_pportdata *ppd;
	unsigned features, pidx, sbufcnt;
	int ret, mtu;
	u32 sbufs, updthresh;
	resource_size_t vl15off;

	/* pport structs are contiguous, allocated after devdata */
	ppd = (struct qib_pportdata *)(dd + 1);
	dd->pport = ppd;
	ppd[0].dd = dd;
	ppd[1].dd = dd;

	dd->cspec = (struct qib_chip_specific *)(ppd + 2);

	ppd[0].cpspec = (struct qib_chippport_specific *)(dd->cspec + 1);
	ppd[1].cpspec = &ppd[0].cpspec[1];
	ppd[0].cpspec->ppd = &ppd[0]; /* for autoneg_7322_work() */
	ppd[1].cpspec->ppd = &ppd[1]; /* for autoneg_7322_work() */

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

	dd->majrev = (u8) SYM_FIELD(dd->revision, Revision_R, ChipRevMajor);
	dd->minrev = (u8) SYM_FIELD(dd->revision, Revision_R, ChipRevMinor);
	dd->cspec->r1 = dd->minrev == 1;

	get_7322_chip_params(dd);
	features = qib_7322_boardname(dd);

	/* now that piobcnt2k and 4k set, we can allocate these */
	sbufcnt = dd->piobcnt2k + dd->piobcnt4k +
		NUM_VL15_BUFS + BITS_PER_LONG - 1;
	sbufcnt /= BITS_PER_LONG;
	dd->cspec->sendchkenable =
		kmalloc_array(sbufcnt, sizeof(*dd->cspec->sendchkenable),
			      GFP_KERNEL);
	dd->cspec->sendgrhchk =
		kmalloc_array(sbufcnt, sizeof(*dd->cspec->sendgrhchk),
			      GFP_KERNEL);
	dd->cspec->sendibchk =
		kmalloc_array(sbufcnt, sizeof(*dd->cspec->sendibchk),
			      GFP_KERNEL);
	if (!dd->cspec->sendchkenable || !dd->cspec->sendgrhchk ||
		!dd->cspec->sendibchk) {
		ret = -ENOMEM;
		goto bail;
	}

	ppd = dd->pport;

	/*
	 * GPIO bits for TWSI data and clock,
	 * used for serial EEPROM.
	 */
	dd->gpio_sda_num = _QIB_GPIO_SDA_NUM;
	dd->gpio_scl_num = _QIB_GPIO_SCL_NUM;
	dd->twsi_eeprom_dev = QIB_TWSI_EEPROM_DEV;

	dd->flags |= QIB_HAS_INTX | QIB_HAS_LINK_LATENCY |
		QIB_NODMA_RTAIL | QIB_HAS_VLSUPP | QIB_HAS_HDRSUPP |
		QIB_HAS_THRESH_UPDATE |
		(sdma_idle_cnt ? QIB_HAS_SDMA_TIMEOUT : 0);
	dd->flags |= qib_special_trigger ?
		QIB_USE_SPCL_TRIG : QIB_HAS_SEND_DMA;

	/*
	 * Setup initial values.  These may change when PAT is enabled, but
	 * we need these to do initial chip register accesses.
	 */
	qib_7322_set_baseaddrs(dd);

	mtu = ib_mtu_enum_to_int(qib_ibmtu);
	if (mtu == -1)
		mtu = QIB_DEFAULT_MTU;

	dd->cspec->int_enable_mask = QIB_I_BITSEXTANT;
	/* all hwerrors become interrupts, unless special purposed */
	dd->cspec->hwerrmask = ~0ULL;
	/*  link_recovery setup causes these errors, so ignore them,
	 *  other than clearing them when they occur */
	dd->cspec->hwerrmask &=
		~(SYM_MASK(HwErrMask, IBSerdesPClkNotDetectMask_0) |
		  SYM_MASK(HwErrMask, IBSerdesPClkNotDetectMask_1) |
		  HWE_MASK(LATriggered));

	for (pidx = 0; pidx < NUM_IB_PORTS; ++pidx) {
		struct qib_chippport_specific *cp = ppd->cpspec;

		ppd->link_speed_supported = features & PORT_SPD_CAP;
		features >>=  PORT_SPD_CAP_SHIFT;
		if (!ppd->link_speed_supported) {
			/* single port mode (7340, or configured) */
			dd->skip_kctxt_mask |= 1 << pidx;
			if (pidx == 0) {
				/* Make sure port is disabled. */
				qib_write_kreg_port(ppd, krp_rcvctrl, 0);
				qib_write_kreg_port(ppd, krp_ibcctrl_a, 0);
				ppd[0] = ppd[1];
				dd->cspec->hwerrmask &= ~(SYM_MASK(HwErrMask,
						  IBSerdesPClkNotDetectMask_0)
						  | SYM_MASK(HwErrMask,
						  SDmaMemReadErrMask_0));
				dd->cspec->int_enable_mask &= ~(
				     SYM_MASK(IntMask, SDmaCleanupDoneMask_0) |
				     SYM_MASK(IntMask, SDmaIdleIntMask_0) |
				     SYM_MASK(IntMask, SDmaProgressIntMask_0) |
				     SYM_MASK(IntMask, SDmaIntMask_0) |
				     SYM_MASK(IntMask, ErrIntMask_0) |
				     SYM_MASK(IntMask, SendDoneIntMask_0));
			} else {
				/* Make sure port is disabled. */
				qib_write_kreg_port(ppd, krp_rcvctrl, 0);
				qib_write_kreg_port(ppd, krp_ibcctrl_a, 0);
				dd->cspec->hwerrmask &= ~(SYM_MASK(HwErrMask,
						  IBSerdesPClkNotDetectMask_1)
						  | SYM_MASK(HwErrMask,
						  SDmaMemReadErrMask_1));
				dd->cspec->int_enable_mask &= ~(
				     SYM_MASK(IntMask, SDmaCleanupDoneMask_1) |
				     SYM_MASK(IntMask, SDmaIdleIntMask_1) |
				     SYM_MASK(IntMask, SDmaProgressIntMask_1) |
				     SYM_MASK(IntMask, SDmaIntMask_1) |
				     SYM_MASK(IntMask, ErrIntMask_1) |
				     SYM_MASK(IntMask, SendDoneIntMask_1));
			}
			continue;
		}

		dd->num_pports++;
		ret = qib_init_pportdata(ppd, dd, pidx, dd->num_pports);
		if (ret) {
			dd->num_pports--;
			goto bail;
		}

		ppd->link_width_supported = IB_WIDTH_1X | IB_WIDTH_4X;
		ppd->link_width_enabled = IB_WIDTH_4X;
		ppd->link_speed_enabled = ppd->link_speed_supported;
		/*
		 * Set the initial values to reasonable default, will be set
		 * for real when link is up.
		 */
		ppd->link_width_active = IB_WIDTH_4X;
		ppd->link_speed_active = QIB_IB_SDR;
		ppd->delay_mult = ib_rate_to_delay[IB_RATE_10_GBPS];
		switch (qib_num_cfg_vls) {
		case 1:
			ppd->vls_supported = IB_VL_VL0;
			break;
		case 2:
			ppd->vls_supported = IB_VL_VL0_1;
			break;
		default:
			qib_devinfo(dd->pcidev,
				    "Invalid num_vls %u, using 4 VLs\n",
				    qib_num_cfg_vls);
			qib_num_cfg_vls = 4;
			fallthrough;
		case 4:
			ppd->vls_supported = IB_VL_VL0_3;
			break;
		case 8:
			if (mtu <= 2048)
				ppd->vls_supported = IB_VL_VL0_7;
			else {
				qib_devinfo(dd->pcidev,
					    "Invalid num_vls %u for MTU %d , using 4 VLs\n",
					    qib_num_cfg_vls, mtu);
				ppd->vls_supported = IB_VL_VL0_3;
				qib_num_cfg_vls = 4;
			}
			break;
		}
		ppd->vls_operational = ppd->vls_supported;

		init_waitqueue_head(&cp->autoneg_wait);
		INIT_DELAYED_WORK(&cp->autoneg_work,
				  autoneg_7322_work);
		if (ppd->dd->cspec->r1)
			INIT_DELAYED_WORK(&cp->ipg_work, ipg_7322_work);

		/*
		 * For Mez and similar cards, no qsfp info, so do
		 * the "cable info" setup here.  Can be overridden
		 * in adapter-specific routines.
		 */
		if (!(dd->flags & QIB_HAS_QSFP)) {
			if (!IS_QMH(dd) && !IS_QME(dd))
				qib_devinfo(dd->pcidev,
					"IB%u:%u: Unknown mezzanine card type\n",
					dd->unit, ppd->port);
			cp->h1_val = IS_QMH(dd) ? H1_FORCE_QMH : H1_FORCE_QME;
			/*
			 * Choose center value as default tx serdes setting
			 * until changed through module parameter.
			 */
			ppd->cpspec->no_eep = IS_QMH(dd) ?
				TXDDS_TABLE_SZ + 2 : TXDDS_TABLE_SZ + 4;
		} else
			cp->h1_val = H1_FORCE_VAL;

		/* Avoid writes to chip for mini_init */
		if (!qib_mini_init)
			write_7322_init_portregs(ppd);

		timer_setup(&cp->chase_timer, reenable_chase, 0);

		ppd++;
	}

	dd->rcvhdrentsize = qib_rcvhdrentsize ?
		qib_rcvhdrentsize : QIB_RCVHDR_ENTSIZE;
	dd->rcvhdrsize = qib_rcvhdrsize ?
		qib_rcvhdrsize : QIB_DFLT_RCVHDRSIZE;
	dd->rhf_offset = dd->rcvhdrentsize - sizeof(u64) / sizeof(u32);

	/* we always allocate at least 2048 bytes for eager buffers */
	dd->rcvegrbufsize = max(mtu, 2048);
	dd->rcvegrbufsize_shift = ilog2(dd->rcvegrbufsize);

	qib_7322_tidtemplate(dd);

	/*
	 * We can request a receive interrupt for 1 or
	 * more packets from current offset.
	 */
	dd->rhdrhead_intr_off =
		(u64) rcv_int_count << IBA7322_HDRHEAD_PKTINT_SHIFT;

	/* setup the stats timer; the add_timer is done at end of init */
	timer_setup(&dd->stats_timer, qib_get_7322_faststats, 0);

	dd->ureg_align = 0x10000;  /* 64KB alignment */

	dd->piosize2kmax_dwords = dd->piosize2k >> 2;

	qib_7322_config_ctxts(dd);
	qib_set_ctxtcnt(dd);

	/*
	 * We do not set WC on the VL15 buffers to avoid
	 * a rare problem with unaligned writes from
	 * interrupt-flushed store buffers, so we need
	 * to map those separately here.  We can't solve
	 * this for the rarely used mtrr case.
	 */
	ret = init_chip_wc_pat(dd, 0);
	if (ret)
		goto bail;

	/* vl15 buffers start just after the 4k buffers */
	vl15off = dd->physaddr + (dd->piobufbase >> 32) +
		  dd->piobcnt4k * dd->align4k;
	dd->piovl15base	= ioremap(vl15off,
					  NUM_VL15_BUFS * dd->align4k);
	if (!dd->piovl15base) {
		ret = -ENOMEM;
		goto bail;
	}

	qib_7322_set_baseaddrs(dd); /* set chip access pointers now */

	ret = 0;
	if (qib_mini_init)
		goto bail;
	if (!dd->num_pports) {
		qib_dev_err(dd, "No ports enabled, giving up initialization\n");
		goto bail; /* no error, so can still figure out why err */
	}

	write_7322_initregs(dd);
	ret = qib_create_ctxts(dd);
	init_7322_cntrnames(dd);

	updthresh = 8U; /* update threshold */

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
	if (dd->flags & QIB_HAS_SEND_DMA) {
		dd->cspec->sdmabufcnt = dd->piobcnt4k;
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
	dd->pbufsctxt = (dd->cfgctxts > dd->first_user_ctxt) ?
		dd->lastctxt_piobuf / (dd->cfgctxts - dd->first_user_ctxt) : 0;

	/*
	 * If we have 16 user contexts, we will have 7 sbufs
	 * per context, so reduce the update threshold to match.  We
	 * want to update before we actually run out, at low pbufs/ctxt
	 * so give ourselves some margin.
	 */
	if (dd->pbufsctxt >= 2 && dd->pbufsctxt - 2 < updthresh)
		updthresh = dd->pbufsctxt - 2;
	dd->cspec->updthresh_dflt = updthresh;
	dd->cspec->updthresh = updthresh;

	/* before full enable, no interrupts, no locking needed */
	dd->sendctrl |= ((updthresh & SYM_RMASK(SendCtrl, AvailUpdThld))
			     << SYM_LSB(SendCtrl, AvailUpdThld)) |
			SYM_MASK(SendCtrl, SendBufAvailPad64Byte);

	dd->psxmitwait_supported = 1;
	dd->psxmitwait_check_rate = QIB_7322_PSXMITWAIT_CHECK_RATE;
bail:
	if (!dd->ctxtcnt)
		dd->ctxtcnt = 1; /* for other initialization code */

	return ret;
}

static u32 __iomem *qib_7322_getsendbuf(struct qib_pportdata *ppd, u64 pbc,
					u32 *pbufnum)
{
	u32 first, last, plen = pbc & QIB_PBC_LENGTH_MASK;
	struct qib_devdata *dd = ppd->dd;

	/* last is same for 2k and 4k, because we use 4k if all 2k busy */
	if (pbc & PBC_7322_VL15_SEND) {
		first = dd->piobcnt2k + dd->piobcnt4k + ppd->hw_pidx;
		last = first;
	} else {
		if ((plen + 1) > dd->piosize2kmax_dwords)
			first = dd->piobcnt2k;
		else
			first = 0;
		last = dd->cspec->lastbuf_for_pio;
	}
	return qib_getsendbuf_range(dd, pbufnum, first, last);
}

static void qib_set_cntr_7322_sample(struct qib_pportdata *ppd, u32 intv,
				     u32 start)
{
	qib_write_kreg_port(ppd, krp_psinterval, intv);
	qib_write_kreg_port(ppd, krp_psstart, start);
}

/*
 * Must be called with sdma_lock held, or before init finished.
 */
static void qib_sdma_set_7322_desc_cnt(struct qib_pportdata *ppd, unsigned cnt)
{
	qib_write_kreg_port(ppd, krp_senddmadesccnt, cnt);
}

/*
 * sdma_lock should be acquired before calling this routine
 */
static void dump_sdma_7322_state(struct qib_pportdata *ppd)
{
	u64 reg, reg1, reg2;

	reg = qib_read_kreg_port(ppd, krp_senddmastatus);
	qib_dev_porterr(ppd->dd, ppd->port,
		"SDMA senddmastatus: 0x%016llx\n", reg);

	reg = qib_read_kreg_port(ppd, krp_sendctrl);
	qib_dev_porterr(ppd->dd, ppd->port,
		"SDMA sendctrl: 0x%016llx\n", reg);

	reg = qib_read_kreg_port(ppd, krp_senddmabase);
	qib_dev_porterr(ppd->dd, ppd->port,
		"SDMA senddmabase: 0x%016llx\n", reg);

	reg = qib_read_kreg_port(ppd, krp_senddmabufmask0);
	reg1 = qib_read_kreg_port(ppd, krp_senddmabufmask1);
	reg2 = qib_read_kreg_port(ppd, krp_senddmabufmask2);
	qib_dev_porterr(ppd->dd, ppd->port,
		"SDMA senddmabufmask 0:%llx  1:%llx  2:%llx\n",
		 reg, reg1, reg2);

	/* get bufuse bits, clear them, and print them again if non-zero */
	reg = qib_read_kreg_port(ppd, krp_senddmabuf_use0);
	qib_write_kreg_port(ppd, krp_senddmabuf_use0, reg);
	reg1 = qib_read_kreg_port(ppd, krp_senddmabuf_use1);
	qib_write_kreg_port(ppd, krp_senddmabuf_use0, reg1);
	reg2 = qib_read_kreg_port(ppd, krp_senddmabuf_use2);
	qib_write_kreg_port(ppd, krp_senddmabuf_use0, reg2);
	/* 0 and 1 should always be zero, so print as short form */
	qib_dev_porterr(ppd->dd, ppd->port,
		 "SDMA current senddmabuf_use 0:%llx  1:%llx  2:%llx\n",
		 reg, reg1, reg2);
	reg = qib_read_kreg_port(ppd, krp_senddmabuf_use0);
	reg1 = qib_read_kreg_port(ppd, krp_senddmabuf_use1);
	reg2 = qib_read_kreg_port(ppd, krp_senddmabuf_use2);
	/* 0 and 1 should always be zero, so print as short form */
	qib_dev_porterr(ppd->dd, ppd->port,
		 "SDMA cleared senddmabuf_use 0:%llx  1:%llx  2:%llx\n",
		 reg, reg1, reg2);

	reg = qib_read_kreg_port(ppd, krp_senddmatail);
	qib_dev_porterr(ppd->dd, ppd->port,
		"SDMA senddmatail: 0x%016llx\n", reg);

	reg = qib_read_kreg_port(ppd, krp_senddmahead);
	qib_dev_porterr(ppd->dd, ppd->port,
		"SDMA senddmahead: 0x%016llx\n", reg);

	reg = qib_read_kreg_port(ppd, krp_senddmaheadaddr);
	qib_dev_porterr(ppd->dd, ppd->port,
		"SDMA senddmaheadaddr: 0x%016llx\n", reg);

	reg = qib_read_kreg_port(ppd, krp_senddmalengen);
	qib_dev_porterr(ppd->dd, ppd->port,
		"SDMA senddmalengen: 0x%016llx\n", reg);

	reg = qib_read_kreg_port(ppd, krp_senddmadesccnt);
	qib_dev_porterr(ppd->dd, ppd->port,
		"SDMA senddmadesccnt: 0x%016llx\n", reg);

	reg = qib_read_kreg_port(ppd, krp_senddmaidlecnt);
	qib_dev_porterr(ppd->dd, ppd->port,
		"SDMA senddmaidlecnt: 0x%016llx\n", reg);

	reg = qib_read_kreg_port(ppd, krp_senddmaprioritythld);
	qib_dev_porterr(ppd->dd, ppd->port,
		"SDMA senddmapriorityhld: 0x%016llx\n", reg);

	reg = qib_read_kreg_port(ppd, krp_senddmareloadcnt);
	qib_dev_porterr(ppd->dd, ppd->port,
		"SDMA senddmareloadcnt: 0x%016llx\n", reg);

	dump_sdma_state(ppd);
}

static struct sdma_set_state_action sdma_7322_action_table[] = {
	[qib_sdma_state_s00_hw_down] = {
		.go_s99_running_tofalse = 1,
		.op_enable = 0,
		.op_intenable = 0,
		.op_halt = 0,
		.op_drain = 0,
	},
	[qib_sdma_state_s10_hw_start_up_wait] = {
		.op_enable = 0,
		.op_intenable = 1,
		.op_halt = 1,
		.op_drain = 0,
	},
	[qib_sdma_state_s20_idle] = {
		.op_enable = 1,
		.op_intenable = 1,
		.op_halt = 1,
		.op_drain = 0,
	},
	[qib_sdma_state_s30_sw_clean_up_wait] = {
		.op_enable = 0,
		.op_intenable = 1,
		.op_halt = 1,
		.op_drain = 0,
	},
	[qib_sdma_state_s40_hw_clean_up_wait] = {
		.op_enable = 1,
		.op_intenable = 1,
		.op_halt = 1,
		.op_drain = 0,
	},
	[qib_sdma_state_s50_hw_halt_wait] = {
		.op_enable = 1,
		.op_intenable = 1,
		.op_halt = 1,
		.op_drain = 1,
	},
	[qib_sdma_state_s99_running] = {
		.op_enable = 1,
		.op_intenable = 1,
		.op_halt = 0,
		.op_drain = 0,
		.go_s99_running_totrue = 1,
	},
};

static void qib_7322_sdma_init_early(struct qib_pportdata *ppd)
{
	ppd->sdma_state.set_state_action = sdma_7322_action_table;
}

static int init_sdma_7322_regs(struct qib_pportdata *ppd)
{
	struct qib_devdata *dd = ppd->dd;
	unsigned lastbuf, erstbuf;
	u64 senddmabufmask[3] = { 0 };
	int n;

	qib_write_kreg_port(ppd, krp_senddmabase, ppd->sdma_descq_phys);
	qib_sdma_7322_setlengen(ppd);
	qib_sdma_update_7322_tail(ppd, 0); /* Set SendDmaTail */
	qib_write_kreg_port(ppd, krp_senddmareloadcnt, sdma_idle_cnt);
	qib_write_kreg_port(ppd, krp_senddmadesccnt, 0);
	qib_write_kreg_port(ppd, krp_senddmaheadaddr, ppd->sdma_head_phys);

	if (dd->num_pports)
		n = dd->cspec->sdmabufcnt / dd->num_pports; /* no remainder */
	else
		n = dd->cspec->sdmabufcnt; /* failsafe for init */
	erstbuf = (dd->piobcnt2k + dd->piobcnt4k) -
		((dd->num_pports == 1 || ppd->port == 2) ? n :
		dd->cspec->sdmabufcnt);
	lastbuf = erstbuf + n;

	ppd->sdma_state.first_sendbuf = erstbuf;
	ppd->sdma_state.last_sendbuf = lastbuf;
	for (; erstbuf < lastbuf; ++erstbuf) {
		unsigned word = erstbuf / BITS_PER_LONG;
		unsigned bit = erstbuf & (BITS_PER_LONG - 1);

		senddmabufmask[word] |= 1ULL << bit;
	}
	qib_write_kreg_port(ppd, krp_senddmabufmask0, senddmabufmask[0]);
	qib_write_kreg_port(ppd, krp_senddmabufmask1, senddmabufmask[1]);
	qib_write_kreg_port(ppd, krp_senddmabufmask2, senddmabufmask[2]);
	return 0;
}

/* sdma_lock must be held */
static u16 qib_sdma_7322_gethead(struct qib_pportdata *ppd)
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
		(u16) le64_to_cpu(*ppd->sdma_head_dma) :
		(u16) qib_read_kreg_port(ppd, krp_senddmahead);

	swhead = ppd->sdma_descq_head;
	swtail = ppd->sdma_descq_tail;
	cnt = ppd->sdma_descq_cnt;

	if (swhead < swtail)
		/* not wrapped */
		sane = (hwhead >= swhead) & (hwhead <= swtail);
	else if (swhead > swtail)
		/* wrapped around */
		sane = ((hwhead >= swhead) && (hwhead < cnt)) ||
			(hwhead <= swtail);
	else
		/* empty */
		sane = (hwhead == swhead);

	if (unlikely(!sane)) {
		if (use_dmahead) {
			/* try one more time, directly from the register */
			use_dmahead = 0;
			goto retry;
		}
		/* proceed as if no progress */
		hwhead = swhead;
	}

	return hwhead;
}

static int qib_sdma_7322_busy(struct qib_pportdata *ppd)
{
	u64 hwstatus = qib_read_kreg_port(ppd, krp_senddmastatus);

	return (hwstatus & SYM_MASK(SendDmaStatus_0, ScoreBoardDrainInProg)) ||
	       (hwstatus & SYM_MASK(SendDmaStatus_0, HaltInProg)) ||
	       !(hwstatus & SYM_MASK(SendDmaStatus_0, InternalSDmaHalt)) ||
	       !(hwstatus & SYM_MASK(SendDmaStatus_0, ScbEmpty));
}

/*
 * Compute the amount of delay before sending the next packet if the
 * port's send rate differs from the static rate set for the QP.
 * The delay affects the next packet and the amount of the delay is
 * based on the length of the this packet.
 */
static u32 qib_7322_setpbc_control(struct qib_pportdata *ppd, u32 plen,
				   u8 srate, u8 vl)
{
	u8 snd_mult = ppd->delay_mult;
	u8 rcv_mult = ib_rate_to_delay[srate];
	u32 ret;

	ret = rcv_mult > snd_mult ? ((plen + 1) >> 1) * snd_mult : 0;

	/* Indicate VL15, else set the VL in the control word */
	if (vl == 15)
		ret |= PBC_7322_VL15_SEND_CTRL;
	else
		ret |= vl << PBC_VL_NUM_LSB;
	ret |= ((u32)(ppd->hw_pidx)) << PBC_PORT_SEL_LSB;

	return ret;
}

/*
 * Enable the per-port VL15 send buffers for use.
 * They follow the rest of the buffers, without a config parameter.
 * This was in initregs, but that is done before the shadow
 * is set up, and this has to be done after the shadow is
 * set up.
 */
static void qib_7322_initvl15_bufs(struct qib_devdata *dd)
{
	unsigned vl15bufs;

	vl15bufs = dd->piobcnt2k + dd->piobcnt4k;
	qib_chg_pioavailkernel(dd, vl15bufs, NUM_VL15_BUFS,
			       TXCHK_CHG_TYPE_KERN, NULL);
}

static void qib_7322_init_ctxt(struct qib_ctxtdata *rcd)
{
	if (rcd->ctxt < NUM_IB_PORTS) {
		if (rcd->dd->num_pports > 1) {
			rcd->rcvegrcnt = KCTXT0_EGRCNT / 2;
			rcd->rcvegr_tid_base = rcd->ctxt ? rcd->rcvegrcnt : 0;
		} else {
			rcd->rcvegrcnt = KCTXT0_EGRCNT;
			rcd->rcvegr_tid_base = 0;
		}
	} else {
		rcd->rcvegrcnt = rcd->dd->cspec->rcvegrcnt;
		rcd->rcvegr_tid_base = KCTXT0_EGRCNT +
			(rcd->ctxt - NUM_IB_PORTS) * rcd->rcvegrcnt;
	}
}

#define QTXSLEEPS 5000
static void qib_7322_txchk_change(struct qib_devdata *dd, u32 start,
				  u32 len, u32 which, struct qib_ctxtdata *rcd)
{
	int i;
	const int last = start + len - 1;
	const int lastr = last / BITS_PER_LONG;
	u32 sleeps = 0;
	int wait = rcd != NULL;
	unsigned long flags;

	while (wait) {
		unsigned long shadow = 0;
		int cstart, previ = -1;

		/*
		 * when flipping from kernel to user, we can't change
		 * the checking type if the buffer is allocated to the
		 * driver.   It's OK the other direction, because it's
		 * from close, and we have just disarm'ed all the
		 * buffers.  All the kernel to kernel changes are also
		 * OK.
		 */
		for (cstart = start; cstart <= last; cstart++) {
			i = ((2 * cstart) + QLOGIC_IB_SENDPIOAVAIL_BUSY_SHIFT)
				/ BITS_PER_LONG;
			if (i != previ) {
				shadow = (unsigned long)
					le64_to_cpu(dd->pioavailregs_dma[i]);
				previ = i;
			}
			if (test_bit(((2 * cstart) +
				      QLOGIC_IB_SENDPIOAVAIL_BUSY_SHIFT)
				     % BITS_PER_LONG, &shadow))
				break;
		}

		if (cstart > last)
			break;

		if (sleeps == QTXSLEEPS)
			break;
		/* make sure we see an updated copy next time around */
		sendctrl_7322_mod(dd->pport, QIB_SENDCTRL_AVAIL_BLIP);
		sleeps++;
		msleep(20);
	}

	switch (which) {
	case TXCHK_CHG_TYPE_DIS1:
		/*
		 * disable checking on a range; used by diags; just
		 * one buffer, but still written generically
		 */
		for (i = start; i <= last; i++)
			clear_bit(i, dd->cspec->sendchkenable);
		break;

	case TXCHK_CHG_TYPE_ENAB1:
		/*
		 * (re)enable checking on a range; used by diags; just
		 * one buffer, but still written generically; read
		 * scratch to be sure buffer actually triggered, not
		 * just flushed from processor.
		 */
		qib_read_kreg32(dd, kr_scratch);
		for (i = start; i <= last; i++)
			set_bit(i, dd->cspec->sendchkenable);
		break;

	case TXCHK_CHG_TYPE_KERN:
		/* usable by kernel */
		for (i = start; i <= last; i++) {
			set_bit(i, dd->cspec->sendibchk);
			clear_bit(i, dd->cspec->sendgrhchk);
		}
		spin_lock_irqsave(&dd->uctxt_lock, flags);
		/* see if we need to raise avail update threshold */
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
			sendctrl_7322_mod(dd->pport, QIB_SENDCTRL_AVAIL_BLIP);
		}
		break;

	case TXCHK_CHG_TYPE_USER:
		/* for user process */
		for (i = start; i <= last; i++) {
			clear_bit(i, dd->cspec->sendibchk);
			set_bit(i, dd->cspec->sendgrhchk);
		}
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
			sendctrl_7322_mod(dd->pport, QIB_SENDCTRL_AVAIL_BLIP);
		} else
			spin_unlock_irqrestore(&dd->sendctrl_lock, flags);
		break;

	default:
		break;
	}

	for (i = start / BITS_PER_LONG; which >= 2 && i <= lastr; ++i)
		qib_write_kreg(dd, kr_sendcheckmask + i,
			       dd->cspec->sendchkenable[i]);

	for (i = start / BITS_PER_LONG; which < 2 && i <= lastr; ++i) {
		qib_write_kreg(dd, kr_sendgrhcheckmask + i,
			       dd->cspec->sendgrhchk[i]);
		qib_write_kreg(dd, kr_sendibpktmask + i,
			       dd->cspec->sendibchk[i]);
	}

	/*
	 * Be sure whatever we did was seen by the chip and acted upon,
	 * before we return.  Mostly important for which >= 2.
	 */
	qib_read_kreg32(dd, kr_scratch);
}


/* useful for trigger analyzers, etc. */
static void writescratch(struct qib_devdata *dd, u32 val)
{
	qib_write_kreg(dd, kr_scratch, val);
}

/* Dummy for now, use chip regs soon */
static int qib_7322_tempsense_rd(struct qib_devdata *dd, int regnum)
{
	return -ENXIO;
}

/**
 * qib_init_iba7322_funcs - set up the chip-specific function pointers
 * @pdev: the pci_dev for qlogic_ib device
 * @ent: pci_device_id struct for this dev
 *
 * Also allocates, inits, and returns the devdata struct for this
 * device instance
 *
 * This is global, and is called directly at init to set up the
 * chip-specific function pointers for later use.
 */
struct qib_devdata *qib_init_iba7322_funcs(struct pci_dev *pdev,
					   const struct pci_device_id *ent)
{
	struct qib_devdata *dd;
	int ret, i;
	u32 tabsize, actual_cnt = 0;

	dd = qib_alloc_devdata(pdev,
		NUM_IB_PORTS * sizeof(struct qib_pportdata) +
		sizeof(struct qib_chip_specific) +
		NUM_IB_PORTS * sizeof(struct qib_chippport_specific));
	if (IS_ERR(dd))
		goto bail;

	dd->f_bringup_serdes    = qib_7322_bringup_serdes;
	dd->f_cleanup           = qib_setup_7322_cleanup;
	dd->f_clear_tids        = qib_7322_clear_tids;
	dd->f_free_irq          = qib_7322_free_irq;
	dd->f_get_base_info     = qib_7322_get_base_info;
	dd->f_get_msgheader     = qib_7322_get_msgheader;
	dd->f_getsendbuf        = qib_7322_getsendbuf;
	dd->f_gpio_mod          = gpio_7322_mod;
	dd->f_eeprom_wen        = qib_7322_eeprom_wen;
	dd->f_hdrqempty         = qib_7322_hdrqempty;
	dd->f_ib_updown         = qib_7322_ib_updown;
	dd->f_init_ctxt         = qib_7322_init_ctxt;
	dd->f_initvl15_bufs     = qib_7322_initvl15_bufs;
	dd->f_intr_fallback     = qib_7322_intr_fallback;
	dd->f_late_initreg      = qib_late_7322_initreg;
	dd->f_setpbc_control    = qib_7322_setpbc_control;
	dd->f_portcntr          = qib_portcntr_7322;
	dd->f_put_tid           = qib_7322_put_tid;
	dd->f_quiet_serdes      = qib_7322_mini_quiet_serdes;
	dd->f_rcvctrl           = rcvctrl_7322_mod;
	dd->f_read_cntrs        = qib_read_7322cntrs;
	dd->f_read_portcntrs    = qib_read_7322portcntrs;
	dd->f_reset             = qib_do_7322_reset;
	dd->f_init_sdma_regs    = init_sdma_7322_regs;
	dd->f_sdma_busy         = qib_sdma_7322_busy;
	dd->f_sdma_gethead      = qib_sdma_7322_gethead;
	dd->f_sdma_sendctrl     = qib_7322_sdma_sendctrl;
	dd->f_sdma_set_desc_cnt = qib_sdma_set_7322_desc_cnt;
	dd->f_sdma_update_tail  = qib_sdma_update_7322_tail;
	dd->f_sendctrl          = sendctrl_7322_mod;
	dd->f_set_armlaunch     = qib_set_7322_armlaunch;
	dd->f_set_cntr_sample   = qib_set_cntr_7322_sample;
	dd->f_iblink_state      = qib_7322_iblink_state;
	dd->f_ibphys_portstate  = qib_7322_phys_portstate;
	dd->f_get_ib_cfg        = qib_7322_get_ib_cfg;
	dd->f_set_ib_cfg        = qib_7322_set_ib_cfg;
	dd->f_set_ib_loopback   = qib_7322_set_loopback;
	dd->f_get_ib_table      = qib_7322_get_ib_table;
	dd->f_set_ib_table      = qib_7322_set_ib_table;
	dd->f_set_intr_state    = qib_7322_set_intr_state;
	dd->f_setextled         = qib_setup_7322_setextled;
	dd->f_txchk_change      = qib_7322_txchk_change;
	dd->f_update_usrhead    = qib_update_7322_usrhead;
	dd->f_wantpiobuf_intr   = qib_wantpiobuf_7322_intr;
	dd->f_xgxs_reset        = qib_7322_mini_pcs_reset;
	dd->f_sdma_hw_clean_up  = qib_7322_sdma_hw_clean_up;
	dd->f_sdma_hw_start_up  = qib_7322_sdma_hw_start_up;
	dd->f_sdma_init_early   = qib_7322_sdma_init_early;
	dd->f_writescratch      = writescratch;
	dd->f_tempsense_rd	= qib_7322_tempsense_rd;
#ifdef CONFIG_INFINIBAND_QIB_DCA
	dd->f_notify_dca	= qib_7322_notify_dca;
#endif
	/*
	 * Do remaining PCIe setup and save PCIe values in dd.
	 * Any error printing is already done by the init code.
	 * On return, we have the chip mapped, but chip registers
	 * are not set up until start of qib_init_7322_variables.
	 */
	ret = qib_pcie_ddinit(dd, pdev, ent);
	if (ret < 0)
		goto bail_free;

	/* initialize chip-specific variables */
	ret = qib_init_7322_variables(dd);
	if (ret)
		goto bail_cleanup;

	if (qib_mini_init || !dd->num_pports)
		goto bail;

	/*
	 * Determine number of vectors we want; depends on port count
	 * and number of configured kernel receive queues actually used.
	 * Should also depend on whether sdma is enabled or not, but
	 * that's such a rare testing case it's not worth worrying about.
	 */
	tabsize = dd->first_user_ctxt + ARRAY_SIZE(irq_table);
	for (i = 0; i < tabsize; i++)
		if ((i < ARRAY_SIZE(irq_table) &&
		     irq_table[i].port <= dd->num_pports) ||
		    (i >= ARRAY_SIZE(irq_table) &&
		     dd->rcd[i - ARRAY_SIZE(irq_table)]))
			actual_cnt++;
	/* reduce by ctxt's < 2 */
	if (qib_krcvq01_no_msi)
		actual_cnt -= dd->num_pports;

	tabsize = actual_cnt;
	dd->cspec->msix_entries = kcalloc(tabsize,
					  sizeof(struct qib_msix_entry),
					  GFP_KERNEL);
	if (!dd->cspec->msix_entries)
		tabsize = 0;

	if (qib_pcie_params(dd, 8, &tabsize))
		qib_dev_err(dd,
			"Failed to setup PCIe or interrupts; continuing anyway\n");
	/* may be less than we wanted, if not enough available */
	dd->cspec->num_msix_entries = tabsize;

	/* setup interrupt handler */
	qib_setup_7322_interrupt(dd, 1);

	/* clear diagctrl register, in case diags were running and crashed */
	qib_write_kreg(dd, kr_hwdiagctrl, 0);
#ifdef CONFIG_INFINIBAND_QIB_DCA
	if (!dca_add_requester(&pdev->dev)) {
		qib_devinfo(dd->pcidev, "DCA enabled\n");
		dd->flags |= QIB_DCA_ENABLED;
		qib_setup_dca(dd);
	}
#endif
	goto bail;

bail_cleanup:
	qib_pcie_ddcleanup(dd);
bail_free:
	qib_free_devdata(dd);
	dd = ERR_PTR(ret);
bail:
	return dd;
}

/*
 * Set the table entry at the specified index from the table specifed.
 * There are 3 * TXDDS_TABLE_SZ entries in all per port, with the first
 * TXDDS_TABLE_SZ for SDR, the next for DDR, and the last for QDR.
 * 'idx' below addresses the correct entry, while its 4 LSBs select the
 * corresponding entry (one of TXDDS_TABLE_SZ) from the selected table.
 */
#define DDS_ENT_AMP_LSB 14
#define DDS_ENT_MAIN_LSB 9
#define DDS_ENT_POST_LSB 5
#define DDS_ENT_PRE_XTRA_LSB 3
#define DDS_ENT_PRE_LSB 0

/*
 * Set one entry in the TxDDS table for spec'd port
 * ridx picks one of the entries, while tp points
 * to the appropriate table entry.
 */
static void set_txdds(struct qib_pportdata *ppd, int ridx,
		      const struct txdds_ent *tp)
{
	struct qib_devdata *dd = ppd->dd;
	u32 pack_ent;
	int regidx;

	/* Get correct offset in chip-space, and in source table */
	regidx = KREG_IBPORT_IDX(IBSD_DDS_MAP_TABLE) + ridx;
	/*
	 * We do not use qib_write_kreg_port() because it was intended
	 * only for registers in the lower "port specific" pages.
	 * So do index calculation  by hand.
	 */
	if (ppd->hw_pidx)
		regidx += (dd->palign / sizeof(u64));

	pack_ent = tp->amp << DDS_ENT_AMP_LSB;
	pack_ent |= tp->main << DDS_ENT_MAIN_LSB;
	pack_ent |= tp->pre << DDS_ENT_PRE_LSB;
	pack_ent |= tp->post << DDS_ENT_POST_LSB;
	qib_write_kreg(dd, regidx, pack_ent);
	/* Prevent back-to-back writes by hitting scratch */
	qib_write_kreg(ppd->dd, kr_scratch, 0);
}

static const struct vendor_txdds_ent vendor_txdds[] = {
	{ /* Amphenol 1m 30awg NoEq */
		{ 0x41, 0x50, 0x48 }, "584470002       ",
		{ 10,  0,  0,  5 }, { 10,  0,  0,  9 }, {  7,  1,  0, 13 },
	},
	{ /* Amphenol 3m 28awg NoEq */
		{ 0x41, 0x50, 0x48 }, "584470004       ",
		{  0,  0,  0,  8 }, {  0,  0,  0, 11 }, {  0,  1,  7, 15 },
	},
	{ /* Finisar 3m OM2 Optical */
		{ 0x00, 0x90, 0x65 }, "FCBG410QB1C03-QL",
		{  0,  0,  0,  3 }, {  0,  0,  0,  4 }, {  0,  0,  0, 13 },
	},
	{ /* Finisar 30m OM2 Optical */
		{ 0x00, 0x90, 0x65 }, "FCBG410QB1C30-QL",
		{  0,  0,  0,  1 }, {  0,  0,  0,  5 }, {  0,  0,  0, 11 },
	},
	{ /* Finisar Default OM2 Optical */
		{ 0x00, 0x90, 0x65 }, NULL,
		{  0,  0,  0,  2 }, {  0,  0,  0,  5 }, {  0,  0,  0, 12 },
	},
	{ /* Gore 1m 30awg NoEq */
		{ 0x00, 0x21, 0x77 }, "QSN3300-1       ",
		{  0,  0,  0,  6 }, {  0,  0,  0,  9 }, {  0,  1,  0, 15 },
	},
	{ /* Gore 2m 30awg NoEq */
		{ 0x00, 0x21, 0x77 }, "QSN3300-2       ",
		{  0,  0,  0,  8 }, {  0,  0,  0, 10 }, {  0,  1,  7, 15 },
	},
	{ /* Gore 1m 28awg NoEq */
		{ 0x00, 0x21, 0x77 }, "QSN3800-1       ",
		{  0,  0,  0,  6 }, {  0,  0,  0,  8 }, {  0,  1,  0, 15 },
	},
	{ /* Gore 3m 28awg NoEq */
		{ 0x00, 0x21, 0x77 }, "QSN3800-3       ",
		{  0,  0,  0,  9 }, {  0,  0,  0, 13 }, {  0,  1,  7, 15 },
	},
	{ /* Gore 5m 24awg Eq */
		{ 0x00, 0x21, 0x77 }, "QSN7000-5       ",
		{  0,  0,  0,  7 }, {  0,  0,  0,  9 }, {  0,  1,  3, 15 },
	},
	{ /* Gore 7m 24awg Eq */
		{ 0x00, 0x21, 0x77 }, "QSN7000-7       ",
		{  0,  0,  0,  9 }, {  0,  0,  0, 11 }, {  0,  2,  6, 15 },
	},
	{ /* Gore 5m 26awg Eq */
		{ 0x00, 0x21, 0x77 }, "QSN7600-5       ",
		{  0,  0,  0,  8 }, {  0,  0,  0, 11 }, {  0,  1,  9, 13 },
	},
	{ /* Gore 7m 26awg Eq */
		{ 0x00, 0x21, 0x77 }, "QSN7600-7       ",
		{  0,  0,  0,  8 }, {  0,  0,  0, 11 }, {  10,  1,  8, 15 },
	},
	{ /* Intersil 12m 24awg Active */
		{ 0x00, 0x30, 0xB4 }, "QLX4000CQSFP1224",
		{  0,  0,  0,  2 }, {  0,  0,  0,  5 }, {  0,  3,  0,  9 },
	},
	{ /* Intersil 10m 28awg Active */
		{ 0x00, 0x30, 0xB4 }, "QLX4000CQSFP1028",
		{  0,  0,  0,  6 }, {  0,  0,  0,  4 }, {  0,  2,  0,  2 },
	},
	{ /* Intersil 7m 30awg Active */
		{ 0x00, 0x30, 0xB4 }, "QLX4000CQSFP0730",
		{  0,  0,  0,  6 }, {  0,  0,  0,  4 }, {  0,  1,  0,  3 },
	},
	{ /* Intersil 5m 32awg Active */
		{ 0x00, 0x30, 0xB4 }, "QLX4000CQSFP0532",
		{  0,  0,  0,  6 }, {  0,  0,  0,  6 }, {  0,  2,  0,  8 },
	},
	{ /* Intersil Default Active */
		{ 0x00, 0x30, 0xB4 }, NULL,
		{  0,  0,  0,  6 }, {  0,  0,  0,  5 }, {  0,  2,  0,  5 },
	},
	{ /* Luxtera 20m Active Optical */
		{ 0x00, 0x25, 0x63 }, NULL,
		{  0,  0,  0,  5 }, {  0,  0,  0,  8 }, {  0,  2,  0,  12 },
	},
	{ /* Molex 1M Cu loopback */
		{ 0x00, 0x09, 0x3A }, "74763-0025      ",
		{  2,  2,  6, 15 }, {  2,  2,  6, 15 }, {  2,  2,  6, 15 },
	},
	{ /* Molex 2m 28awg NoEq */
		{ 0x00, 0x09, 0x3A }, "74757-2201      ",
		{  0,  0,  0,  6 }, {  0,  0,  0,  9 }, {  0,  1,  1, 15 },
	},
};

static const struct txdds_ent txdds_sdr[TXDDS_TABLE_SZ] = {
	/* amp, pre, main, post */
	{  2, 2, 15,  6 },	/* Loopback */
	{  0, 0,  0,  1 },	/*  2 dB */
	{  0, 0,  0,  2 },	/*  3 dB */
	{  0, 0,  0,  3 },	/*  4 dB */
	{  0, 0,  0,  4 },	/*  5 dB */
	{  0, 0,  0,  5 },	/*  6 dB */
	{  0, 0,  0,  6 },	/*  7 dB */
	{  0, 0,  0,  7 },	/*  8 dB */
	{  0, 0,  0,  8 },	/*  9 dB */
	{  0, 0,  0,  9 },	/* 10 dB */
	{  0, 0,  0, 10 },	/* 11 dB */
	{  0, 0,  0, 11 },	/* 12 dB */
	{  0, 0,  0, 12 },	/* 13 dB */
	{  0, 0,  0, 13 },	/* 14 dB */
	{  0, 0,  0, 14 },	/* 15 dB */
	{  0, 0,  0, 15 },	/* 16 dB */
};

static const struct txdds_ent txdds_ddr[TXDDS_TABLE_SZ] = {
	/* amp, pre, main, post */
	{  2, 2, 15,  6 },	/* Loopback */
	{  0, 0,  0,  8 },	/*  2 dB */
	{  0, 0,  0,  8 },	/*  3 dB */
	{  0, 0,  0,  9 },	/*  4 dB */
	{  0, 0,  0,  9 },	/*  5 dB */
	{  0, 0,  0, 10 },	/*  6 dB */
	{  0, 0,  0, 10 },	/*  7 dB */
	{  0, 0,  0, 11 },	/*  8 dB */
	{  0, 0,  0, 11 },	/*  9 dB */
	{  0, 0,  0, 12 },	/* 10 dB */
	{  0, 0,  0, 12 },	/* 11 dB */
	{  0, 0,  0, 13 },	/* 12 dB */
	{  0, 0,  0, 13 },	/* 13 dB */
	{  0, 0,  0, 14 },	/* 14 dB */
	{  0, 0,  0, 14 },	/* 15 dB */
	{  0, 0,  0, 15 },	/* 16 dB */
};

static const struct txdds_ent txdds_qdr[TXDDS_TABLE_SZ] = {
	/* amp, pre, main, post */
	{  2, 2, 15,  6 },	/* Loopback */
	{  0, 1,  0,  7 },	/*  2 dB (also QMH7342) */
	{  0, 1,  0,  9 },	/*  3 dB (also QMH7342) */
	{  0, 1,  0, 11 },	/*  4 dB */
	{  0, 1,  0, 13 },	/*  5 dB */
	{  0, 1,  0, 15 },	/*  6 dB */
	{  0, 1,  3, 15 },	/*  7 dB */
	{  0, 1,  7, 15 },	/*  8 dB */
	{  0, 1,  7, 15 },	/*  9 dB */
	{  0, 1,  8, 15 },	/* 10 dB */
	{  0, 1,  9, 15 },	/* 11 dB */
	{  0, 1, 10, 15 },	/* 12 dB */
	{  0, 2,  6, 15 },	/* 13 dB */
	{  0, 2,  7, 15 },	/* 14 dB */
	{  0, 2,  8, 15 },	/* 15 dB */
	{  0, 2,  9, 15 },	/* 16 dB */
};

/*
 * extra entries for use with txselect, for indices >= TXDDS_TABLE_SZ.
 * These are mostly used for mez cards going through connectors
 * and backplane traces, but can be used to add other "unusual"
 * table values as well.
 */
static const struct txdds_ent txdds_extra_sdr[TXDDS_EXTRA_SZ] = {
	/* amp, pre, main, post */
	{  0, 0, 0,  1 },	/* QMH7342 backplane settings */
	{  0, 0, 0,  1 },	/* QMH7342 backplane settings */
	{  0, 0, 0,  2 },	/* QMH7342 backplane settings */
	{  0, 0, 0,  2 },	/* QMH7342 backplane settings */
	{  0, 0, 0,  3 },	/* QMH7342 backplane settings */
	{  0, 0, 0,  4 },	/* QMH7342 backplane settings */
	{  0, 1, 4, 15 },	/* QME7342 backplane settings 1.0 */
	{  0, 1, 3, 15 },	/* QME7342 backplane settings 1.0 */
	{  0, 1, 0, 12 },	/* QME7342 backplane settings 1.0 */
	{  0, 1, 0, 11 },	/* QME7342 backplane settings 1.0 */
	{  0, 1, 0,  9 },	/* QME7342 backplane settings 1.0 */
	{  0, 1, 0, 14 },	/* QME7342 backplane settings 1.0 */
	{  0, 1, 2, 15 },	/* QME7342 backplane settings 1.0 */
	{  0, 1, 0, 11 },       /* QME7342 backplane settings 1.1 */
	{  0, 1, 0,  7 },       /* QME7342 backplane settings 1.1 */
	{  0, 1, 0,  9 },       /* QME7342 backplane settings 1.1 */
	{  0, 1, 0,  6 },       /* QME7342 backplane settings 1.1 */
	{  0, 1, 0,  8 },       /* QME7342 backplane settings 1.1 */
};

static const struct txdds_ent txdds_extra_ddr[TXDDS_EXTRA_SZ] = {
	/* amp, pre, main, post */
	{  0, 0, 0,  7 },	/* QMH7342 backplane settings */
	{  0, 0, 0,  7 },	/* QMH7342 backplane settings */
	{  0, 0, 0,  8 },	/* QMH7342 backplane settings */
	{  0, 0, 0,  8 },	/* QMH7342 backplane settings */
	{  0, 0, 0,  9 },	/* QMH7342 backplane settings */
	{  0, 0, 0, 10 },	/* QMH7342 backplane settings */
	{  0, 1, 4, 15 },	/* QME7342 backplane settings 1.0 */
	{  0, 1, 3, 15 },	/* QME7342 backplane settings 1.0 */
	{  0, 1, 0, 12 },	/* QME7342 backplane settings 1.0 */
	{  0, 1, 0, 11 },	/* QME7342 backplane settings 1.0 */
	{  0, 1, 0,  9 },	/* QME7342 backplane settings 1.0 */
	{  0, 1, 0, 14 },	/* QME7342 backplane settings 1.0 */
	{  0, 1, 2, 15 },	/* QME7342 backplane settings 1.0 */
	{  0, 1, 0, 11 },       /* QME7342 backplane settings 1.1 */
	{  0, 1, 0,  7 },       /* QME7342 backplane settings 1.1 */
	{  0, 1, 0,  9 },       /* QME7342 backplane settings 1.1 */
	{  0, 1, 0,  6 },       /* QME7342 backplane settings 1.1 */
	{  0, 1, 0,  8 },       /* QME7342 backplane settings 1.1 */
};

static const struct txdds_ent txdds_extra_qdr[TXDDS_EXTRA_SZ] = {
	/* amp, pre, main, post */
	{  0, 1,  0,  4 },	/* QMH7342 backplane settings */
	{  0, 1,  0,  5 },	/* QMH7342 backplane settings */
	{  0, 1,  0,  6 },	/* QMH7342 backplane settings */
	{  0, 1,  0,  8 },	/* QMH7342 backplane settings */
	{  0, 1,  0, 10 },	/* QMH7342 backplane settings */
	{  0, 1,  0, 12 },	/* QMH7342 backplane settings */
	{  0, 1,  4, 15 },	/* QME7342 backplane settings 1.0 */
	{  0, 1,  3, 15 },	/* QME7342 backplane settings 1.0 */
	{  0, 1,  0, 12 },	/* QME7342 backplane settings 1.0 */
	{  0, 1,  0, 11 },	/* QME7342 backplane settings 1.0 */
	{  0, 1,  0,  9 },	/* QME7342 backplane settings 1.0 */
	{  0, 1,  0, 14 },	/* QME7342 backplane settings 1.0 */
	{  0, 1,  2, 15 },	/* QME7342 backplane settings 1.0 */
	{  0, 1,  0, 11 },      /* QME7342 backplane settings 1.1 */
	{  0, 1,  0,  7 },      /* QME7342 backplane settings 1.1 */
	{  0, 1,  0,  9 },      /* QME7342 backplane settings 1.1 */
	{  0, 1,  0,  6 },      /* QME7342 backplane settings 1.1 */
	{  0, 1,  0,  8 },      /* QME7342 backplane settings 1.1 */
};

static const struct txdds_ent txdds_extra_mfg[TXDDS_MFG_SZ] = {
	/* amp, pre, main, post */
	{ 0, 0, 0, 0 },         /* QME7342 mfg settings */
	{ 0, 0, 0, 6 },         /* QME7342 P2 mfg settings */
};

static const struct txdds_ent *get_atten_table(const struct txdds_ent *txdds,
					       unsigned atten)
{
	/*
	 * The attenuation table starts at 2dB for entry 1,
	 * with entry 0 being the loopback entry.
	 */
	if (atten <= 2)
		atten = 1;
	else if (atten > TXDDS_TABLE_SZ)
		atten = TXDDS_TABLE_SZ - 1;
	else
		atten--;
	return txdds + atten;
}

/*
 * if override is set, the module parameter txselect has a value
 * for this specific port, so use it, rather than our normal mechanism.
 */
static void find_best_ent(struct qib_pportdata *ppd,
			  const struct txdds_ent **sdr_dds,
			  const struct txdds_ent **ddr_dds,
			  const struct txdds_ent **qdr_dds, int override)
{
	struct qib_qsfp_cache *qd = &ppd->cpspec->qsfp_data.cache;
	int idx;

	/* Search table of known cables */
	for (idx = 0; !override && idx < ARRAY_SIZE(vendor_txdds); ++idx) {
		const struct vendor_txdds_ent *v = vendor_txdds + idx;

		if (!memcmp(v->oui, qd->oui, QSFP_VOUI_LEN) &&
		    (!v->partnum ||
		     !memcmp(v->partnum, qd->partnum, QSFP_PN_LEN))) {
			*sdr_dds = &v->sdr;
			*ddr_dds = &v->ddr;
			*qdr_dds = &v->qdr;
			return;
		}
	}

	/* Active cables don't have attenuation so we only set SERDES
	 * settings to account for the attenuation of the board traces. */
	if (!override && QSFP_IS_ACTIVE(qd->tech)) {
		*sdr_dds = txdds_sdr + ppd->dd->board_atten;
		*ddr_dds = txdds_ddr + ppd->dd->board_atten;
		*qdr_dds = txdds_qdr + ppd->dd->board_atten;
		return;
	}

	if (!override && QSFP_HAS_ATTEN(qd->tech) && (qd->atten[0] ||
						      qd->atten[1])) {
		*sdr_dds = get_atten_table(txdds_sdr, qd->atten[0]);
		*ddr_dds = get_atten_table(txdds_ddr, qd->atten[0]);
		*qdr_dds = get_atten_table(txdds_qdr, qd->atten[1]);
		return;
	} else if (ppd->cpspec->no_eep < TXDDS_TABLE_SZ) {
		/*
		 * If we have no (or incomplete) data from the cable
		 * EEPROM, or no QSFP, or override is set, use the
		 * module parameter value to index into the attentuation
		 * table.
		 */
		idx = ppd->cpspec->no_eep;
		*sdr_dds = &txdds_sdr[idx];
		*ddr_dds = &txdds_ddr[idx];
		*qdr_dds = &txdds_qdr[idx];
	} else if (ppd->cpspec->no_eep < (TXDDS_TABLE_SZ + TXDDS_EXTRA_SZ)) {
		/* similar to above, but index into the "extra" table. */
		idx = ppd->cpspec->no_eep - TXDDS_TABLE_SZ;
		*sdr_dds = &txdds_extra_sdr[idx];
		*ddr_dds = &txdds_extra_ddr[idx];
		*qdr_dds = &txdds_extra_qdr[idx];
	} else if ((IS_QME(ppd->dd) || IS_QMH(ppd->dd)) &&
		   ppd->cpspec->no_eep < (TXDDS_TABLE_SZ + TXDDS_EXTRA_SZ +
					  TXDDS_MFG_SZ)) {
		idx = ppd->cpspec->no_eep - (TXDDS_TABLE_SZ + TXDDS_EXTRA_SZ);
		pr_info("IB%u:%u use idx %u into txdds_mfg\n",
			ppd->dd->unit, ppd->port, idx);
		*sdr_dds = &txdds_extra_mfg[idx];
		*ddr_dds = &txdds_extra_mfg[idx];
		*qdr_dds = &txdds_extra_mfg[idx];
	} else {
		/* this shouldn't happen, it's range checked */
		*sdr_dds = txdds_sdr + qib_long_atten;
		*ddr_dds = txdds_ddr + qib_long_atten;
		*qdr_dds = txdds_qdr + qib_long_atten;
	}
}

static void init_txdds_table(struct qib_pportdata *ppd, int override)
{
	const struct txdds_ent *sdr_dds, *ddr_dds, *qdr_dds;
	struct txdds_ent *dds;
	int idx;
	int single_ent = 0;

	find_best_ent(ppd, &sdr_dds, &ddr_dds, &qdr_dds, override);

	/* for mez cards or override, use the selected value for all entries */
	if (!(ppd->dd->flags & QIB_HAS_QSFP) || override)
		single_ent = 1;

	/* Fill in the first entry with the best entry found. */
	set_txdds(ppd, 0, sdr_dds);
	set_txdds(ppd, TXDDS_TABLE_SZ, ddr_dds);
	set_txdds(ppd, 2 * TXDDS_TABLE_SZ, qdr_dds);
	if (ppd->lflags & (QIBL_LINKINIT | QIBL_LINKARMED |
		QIBL_LINKACTIVE)) {
		dds = (struct txdds_ent *)(ppd->link_speed_active ==
					   QIB_IB_QDR ?  qdr_dds :
					   (ppd->link_speed_active ==
					    QIB_IB_DDR ? ddr_dds : sdr_dds));
		write_tx_serdes_param(ppd, dds);
	}

	/* Fill in the remaining entries with the default table values. */
	for (idx = 1; idx < ARRAY_SIZE(txdds_sdr); ++idx) {
		set_txdds(ppd, idx, single_ent ? sdr_dds : txdds_sdr + idx);
		set_txdds(ppd, idx + TXDDS_TABLE_SZ,
			  single_ent ? ddr_dds : txdds_ddr + idx);
		set_txdds(ppd, idx + 2 * TXDDS_TABLE_SZ,
			  single_ent ? qdr_dds : txdds_qdr + idx);
	}
}

#define KR_AHB_ACC KREG_IDX(ahb_access_ctrl)
#define KR_AHB_TRANS KREG_IDX(ahb_transaction_reg)
#define AHB_TRANS_RDY SYM_MASK(ahb_transaction_reg, ahb_rdy)
#define AHB_ADDR_LSB SYM_LSB(ahb_transaction_reg, ahb_address)
#define AHB_DATA_LSB SYM_LSB(ahb_transaction_reg, ahb_data)
#define AHB_WR SYM_MASK(ahb_transaction_reg, write_not_read)
#define AHB_TRANS_TRIES 10

/*
 * The chan argument is 0=chan0, 1=chan1, 2=pll, 3=chan2, 4=chan4,
 * 5=subsystem which is why most calls have "chan + chan >> 1"
 * for the channel argument.
 */
static u32 ahb_mod(struct qib_devdata *dd, int quad, int chan, int addr,
		    u32 data, u32 mask)
{
	u32 rd_data, wr_data, sz_mask;
	u64 trans, acc, prev_acc;
	u32 ret = 0xBAD0BAD;
	int tries;

	prev_acc = qib_read_kreg64(dd, KR_AHB_ACC);
	/* From this point on, make sure we return access */
	acc = (quad << 1) | 1;
	qib_write_kreg(dd, KR_AHB_ACC, acc);

	for (tries = 1; tries < AHB_TRANS_TRIES; ++tries) {
		trans = qib_read_kreg64(dd, KR_AHB_TRANS);
		if (trans & AHB_TRANS_RDY)
			break;
	}
	if (tries >= AHB_TRANS_TRIES) {
		qib_dev_err(dd, "No ahb_rdy in %d tries\n", AHB_TRANS_TRIES);
		goto bail;
	}

	/* If mask is not all 1s, we need to read, but different SerDes
	 * entities have different sizes
	 */
	sz_mask = (1UL << ((quad == 1) ? 32 : 16)) - 1;
	wr_data = data & mask & sz_mask;
	if ((~mask & sz_mask) != 0) {
		trans = ((chan << 6) | addr) << (AHB_ADDR_LSB + 1);
		qib_write_kreg(dd, KR_AHB_TRANS, trans);

		for (tries = 1; tries < AHB_TRANS_TRIES; ++tries) {
			trans = qib_read_kreg64(dd, KR_AHB_TRANS);
			if (trans & AHB_TRANS_RDY)
				break;
		}
		if (tries >= AHB_TRANS_TRIES) {
			qib_dev_err(dd, "No Rd ahb_rdy in %d tries\n",
				    AHB_TRANS_TRIES);
			goto bail;
		}
		/* Re-read in case host split reads and read data first */
		trans = qib_read_kreg64(dd, KR_AHB_TRANS);
		rd_data = (uint32_t)(trans >> AHB_DATA_LSB);
		wr_data |= (rd_data & ~mask & sz_mask);
	}

	/* If mask is not zero, we need to write. */
	if (mask & sz_mask) {
		trans = ((chan << 6) | addr) << (AHB_ADDR_LSB + 1);
		trans |= ((uint64_t)wr_data << AHB_DATA_LSB);
		trans |= AHB_WR;
		qib_write_kreg(dd, KR_AHB_TRANS, trans);

		for (tries = 1; tries < AHB_TRANS_TRIES; ++tries) {
			trans = qib_read_kreg64(dd, KR_AHB_TRANS);
			if (trans & AHB_TRANS_RDY)
				break;
		}
		if (tries >= AHB_TRANS_TRIES) {
			qib_dev_err(dd, "No Wr ahb_rdy in %d tries\n",
				    AHB_TRANS_TRIES);
			goto bail;
		}
	}
	ret = wr_data;
bail:
	qib_write_kreg(dd, KR_AHB_ACC, prev_acc);
	return ret;
}

static void ibsd_wr_allchans(struct qib_pportdata *ppd, int addr, unsigned data,
			     unsigned mask)
{
	struct qib_devdata *dd = ppd->dd;
	int chan;

	for (chan = 0; chan < SERDES_CHANS; ++chan) {
		ahb_mod(dd, IBSD(ppd->hw_pidx), (chan + (chan >> 1)), addr,
			data, mask);
		ahb_mod(dd, IBSD(ppd->hw_pidx), (chan + (chan >> 1)), addr,
			0, 0);
	}
}

static void serdes_7322_los_enable(struct qib_pportdata *ppd, int enable)
{
	u64 data = qib_read_kreg_port(ppd, krp_serdesctrl);
	u8 state = SYM_FIELD(data, IBSerdesCtrl_0, RXLOSEN);

	if (enable && !state) {
		pr_info("IB%u:%u Turning LOS on\n",
			ppd->dd->unit, ppd->port);
		data |= SYM_MASK(IBSerdesCtrl_0, RXLOSEN);
	} else if (!enable && state) {
		pr_info("IB%u:%u Turning LOS off\n",
			ppd->dd->unit, ppd->port);
		data &= ~SYM_MASK(IBSerdesCtrl_0, RXLOSEN);
	}
	qib_write_kreg_port(ppd, krp_serdesctrl, data);
}

static int serdes_7322_init(struct qib_pportdata *ppd)
{
	int ret = 0;

	if (ppd->dd->cspec->r1)
		ret = serdes_7322_init_old(ppd);
	else
		ret = serdes_7322_init_new(ppd);
	return ret;
}

static int serdes_7322_init_old(struct qib_pportdata *ppd)
{
	u32 le_val;

	/*
	 * Initialize the Tx DDS tables.  Also done every QSFP event,
	 * for adapters with QSFP
	 */
	init_txdds_table(ppd, 0);

	/* ensure no tx overrides from earlier driver loads */
	qib_write_kreg_port(ppd, krp_tx_deemph_override,
		SYM_MASK(IBSD_TX_DEEMPHASIS_OVERRIDE_0,
		reset_tx_deemphasis_override));

	/* Patch some SerDes defaults to "Better for IB" */
	/* Timing Loop Bandwidth: cdr_timing[11:9] = 0 */
	ibsd_wr_allchans(ppd, 2, 0, BMASK(11, 9));

	/* Termination: rxtermctrl_r2d addr 11 bits [12:11] = 1 */
	ibsd_wr_allchans(ppd, 11, (1 << 11), BMASK(12, 11));
	/* Enable LE2: rxle2en_r2a addr 13 bit [6] = 1 */
	ibsd_wr_allchans(ppd, 13, (1 << 6), (1 << 6));

	/* May be overridden in qsfp_7322_event */
	le_val = IS_QME(ppd->dd) ? LE2_QME : LE2_DEFAULT;
	ibsd_wr_allchans(ppd, 13, (le_val << 7), BMASK(9, 7));

	/* enable LE1 adaptation for all but QME, which is disabled */
	le_val = IS_QME(ppd->dd) ? 0 : 1;
	ibsd_wr_allchans(ppd, 13, (le_val << 5), (1 << 5));

	/* Clear cmode-override, may be set from older driver */
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 10, 0 << 14, 1 << 14);

	/* Timing Recovery: rxtapsel addr 5 bits [9:8] = 0 */
	ibsd_wr_allchans(ppd, 5, (0 << 8), BMASK(9, 8));

	/* setup LoS params; these are subsystem, so chan == 5 */
	/* LoS filter threshold_count on, ch 0-3, set to 8 */
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 5, 8 << 11, BMASK(14, 11));
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 7, 8 << 4, BMASK(7, 4));
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 8, 8 << 11, BMASK(14, 11));
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 10, 8 << 4, BMASK(7, 4));

	/* LoS filter threshold_count off, ch 0-3, set to 4 */
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 6, 4 << 0, BMASK(3, 0));
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 7, 4 << 8, BMASK(11, 8));
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 9, 4 << 0, BMASK(3, 0));
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 10, 4 << 8, BMASK(11, 8));

	/* LoS filter select enabled */
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 9, 1 << 15, 1 << 15);

	/* LoS target data:  SDR=4, DDR=2, QDR=1 */
	ibsd_wr_allchans(ppd, 14, (1 << 3), BMASK(5, 3)); /* QDR */
	ibsd_wr_allchans(ppd, 20, (2 << 10), BMASK(12, 10)); /* DDR */
	ibsd_wr_allchans(ppd, 20, (4 << 13), BMASK(15, 13)); /* SDR */

	serdes_7322_los_enable(ppd, 1);

	/* rxbistena; set 0 to avoid effects of it switch later */
	ibsd_wr_allchans(ppd, 9, 0 << 15, 1 << 15);

	/* Configure 4 DFE taps, and only they adapt */
	ibsd_wr_allchans(ppd, 16, 0 << 0, BMASK(1, 0));

	/* gain hi stop 32 (22) (6:1) lo stop 7 (10:7) target 22 (13) (15:11) */
	le_val = (ppd->dd->cspec->r1 || IS_QME(ppd->dd)) ? 0xb6c0 : 0x6bac;
	ibsd_wr_allchans(ppd, 21, le_val, 0xfffe);

	/*
	 * Set receive adaptation mode.  SDR and DDR adaptation are
	 * always on, and QDR is initially enabled; later disabled.
	 */
	qib_write_kreg_port(ppd, krp_static_adapt_dis(0), 0ULL);
	qib_write_kreg_port(ppd, krp_static_adapt_dis(1), 0ULL);
	qib_write_kreg_port(ppd, krp_static_adapt_dis(2),
			    ppd->dd->cspec->r1 ?
			    QDR_STATIC_ADAPT_DOWN_R1 : QDR_STATIC_ADAPT_DOWN);
	ppd->cpspec->qdr_dfe_on = 1;

	/* FLoop LOS gate: PPM filter  enabled */
	ibsd_wr_allchans(ppd, 38, 0 << 10, 1 << 10);

	/* rx offset center enabled */
	ibsd_wr_allchans(ppd, 12, 1 << 4, 1 << 4);

	if (!ppd->dd->cspec->r1) {
		ibsd_wr_allchans(ppd, 12, 1 << 12, 1 << 12);
		ibsd_wr_allchans(ppd, 12, 2 << 8, 0x0f << 8);
	}

	/* Set the frequency loop bandwidth to 15 */
	ibsd_wr_allchans(ppd, 2, 15 << 5, BMASK(8, 5));

	return 0;
}

static int serdes_7322_init_new(struct qib_pportdata *ppd)
{
	unsigned long tend;
	u32 le_val, rxcaldone;
	int chan, chan_done = (1 << SERDES_CHANS) - 1;

	/* Clear cmode-override, may be set from older driver */
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 10, 0 << 14, 1 << 14);

	/* ensure no tx overrides from earlier driver loads */
	qib_write_kreg_port(ppd, krp_tx_deemph_override,
		SYM_MASK(IBSD_TX_DEEMPHASIS_OVERRIDE_0,
		reset_tx_deemphasis_override));

	/* START OF LSI SUGGESTED SERDES BRINGUP */
	/* Reset - Calibration Setup */
	/*       Stop DFE adaptaion */
	ibsd_wr_allchans(ppd, 1, 0, BMASK(9, 1));
	/*       Disable LE1 */
	ibsd_wr_allchans(ppd, 13, 0, BMASK(5, 5));
	/*       Disable autoadapt for LE1 */
	ibsd_wr_allchans(ppd, 1, 0, BMASK(15, 15));
	/*       Disable LE2 */
	ibsd_wr_allchans(ppd, 13, 0, BMASK(6, 6));
	/*       Disable VGA */
	ibsd_wr_allchans(ppd, 5, 0, BMASK(0, 0));
	/*       Disable AFE Offset Cancel */
	ibsd_wr_allchans(ppd, 12, 0, BMASK(12, 12));
	/*       Disable Timing Loop */
	ibsd_wr_allchans(ppd, 2, 0, BMASK(3, 3));
	/*       Disable Frequency Loop */
	ibsd_wr_allchans(ppd, 2, 0, BMASK(4, 4));
	/*       Disable Baseline Wander Correction */
	ibsd_wr_allchans(ppd, 13, 0, BMASK(13, 13));
	/*       Disable RX Calibration */
	ibsd_wr_allchans(ppd, 4, 0, BMASK(10, 10));
	/*       Disable RX Offset Calibration */
	ibsd_wr_allchans(ppd, 12, 0, BMASK(4, 4));
	/*       Select BB CDR */
	ibsd_wr_allchans(ppd, 2, (1 << 15), BMASK(15, 15));
	/*       CDR Step Size */
	ibsd_wr_allchans(ppd, 5, 0, BMASK(9, 8));
	/*       Enable phase Calibration */
	ibsd_wr_allchans(ppd, 12, (1 << 5), BMASK(5, 5));
	/*       DFE Bandwidth [2:14-12] */
	ibsd_wr_allchans(ppd, 2, (4 << 12), BMASK(14, 12));
	/*       DFE Config (4 taps only) */
	ibsd_wr_allchans(ppd, 16, 0, BMASK(1, 0));
	/*       Gain Loop Bandwidth */
	if (!ppd->dd->cspec->r1) {
		ibsd_wr_allchans(ppd, 12, 1 << 12, BMASK(12, 12));
		ibsd_wr_allchans(ppd, 12, 2 << 8, BMASK(11, 8));
	} else {
		ibsd_wr_allchans(ppd, 19, (3 << 11), BMASK(13, 11));
	}
	/*       Baseline Wander Correction Gain [13:4-0] (leave as default) */
	/*       Baseline Wander Correction Gain [3:7-5] (leave as default) */
	/*       Data Rate Select [5:7-6] (leave as default) */
	/*       RX Parallel Word Width [3:10-8] (leave as default) */

	/* RX REST */
	/*       Single- or Multi-channel reset */
	/*       RX Analog reset */
	/*       RX Digital reset */
	ibsd_wr_allchans(ppd, 0, 0, BMASK(15, 13));
	msleep(20);
	/*       RX Analog reset */
	ibsd_wr_allchans(ppd, 0, (1 << 14), BMASK(14, 14));
	msleep(20);
	/*       RX Digital reset */
	ibsd_wr_allchans(ppd, 0, (1 << 13), BMASK(13, 13));
	msleep(20);

	/* setup LoS params; these are subsystem, so chan == 5 */
	/* LoS filter threshold_count on, ch 0-3, set to 8 */
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 5, 8 << 11, BMASK(14, 11));
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 7, 8 << 4, BMASK(7, 4));
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 8, 8 << 11, BMASK(14, 11));
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 10, 8 << 4, BMASK(7, 4));

	/* LoS filter threshold_count off, ch 0-3, set to 4 */
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 6, 4 << 0, BMASK(3, 0));
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 7, 4 << 8, BMASK(11, 8));
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 9, 4 << 0, BMASK(3, 0));
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 10, 4 << 8, BMASK(11, 8));

	/* LoS filter select enabled */
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), 5, 9, 1 << 15, 1 << 15);

	/* LoS target data:  SDR=4, DDR=2, QDR=1 */
	ibsd_wr_allchans(ppd, 14, (1 << 3), BMASK(5, 3)); /* QDR */
	ibsd_wr_allchans(ppd, 20, (2 << 10), BMASK(12, 10)); /* DDR */
	ibsd_wr_allchans(ppd, 20, (4 << 13), BMASK(15, 13)); /* SDR */

	/* Turn on LOS on initial SERDES init */
	serdes_7322_los_enable(ppd, 1);
	/* FLoop LOS gate: PPM filter  enabled */
	ibsd_wr_allchans(ppd, 38, 0 << 10, 1 << 10);

	/* RX LATCH CALIBRATION */
	/*       Enable Eyefinder Phase Calibration latch */
	ibsd_wr_allchans(ppd, 15, 1, BMASK(0, 0));
	/*       Enable RX Offset Calibration latch */
	ibsd_wr_allchans(ppd, 12, (1 << 4), BMASK(4, 4));
	msleep(20);
	/*       Start Calibration */
	ibsd_wr_allchans(ppd, 4, (1 << 10), BMASK(10, 10));
	tend = jiffies + msecs_to_jiffies(500);
	while (chan_done && !time_is_before_jiffies(tend)) {
		msleep(20);
		for (chan = 0; chan < SERDES_CHANS; ++chan) {
			rxcaldone = ahb_mod(ppd->dd, IBSD(ppd->hw_pidx),
					    (chan + (chan >> 1)),
					    25, 0, 0);
			if ((~rxcaldone & (u32)BMASK(9, 9)) == 0 &&
			    (~chan_done & (1 << chan)) == 0)
				chan_done &= ~(1 << chan);
		}
	}
	if (chan_done) {
		pr_info("Serdes %d calibration not done after .5 sec: 0x%x\n",
			 IBSD(ppd->hw_pidx), chan_done);
	} else {
		for (chan = 0; chan < SERDES_CHANS; ++chan) {
			rxcaldone = ahb_mod(ppd->dd, IBSD(ppd->hw_pidx),
					    (chan + (chan >> 1)),
					    25, 0, 0);
			if ((~rxcaldone & (u32)BMASK(10, 10)) == 0)
				pr_info("Serdes %d chan %d calibration failed\n",
					IBSD(ppd->hw_pidx), chan);
		}
	}

	/*       Turn off Calibration */
	ibsd_wr_allchans(ppd, 4, 0, BMASK(10, 10));
	msleep(20);

	/* BRING RX UP */
	/*       Set LE2 value (May be overridden in qsfp_7322_event) */
	le_val = IS_QME(ppd->dd) ? LE2_QME : LE2_DEFAULT;
	ibsd_wr_allchans(ppd, 13, (le_val << 7), BMASK(9, 7));
	/*       Set LE2 Loop bandwidth */
	ibsd_wr_allchans(ppd, 3, (7 << 5), BMASK(7, 5));
	/*       Enable LE2 */
	ibsd_wr_allchans(ppd, 13, (1 << 6), BMASK(6, 6));
	msleep(20);
	/*       Enable H0 only */
	ibsd_wr_allchans(ppd, 1, 1, BMASK(9, 1));
	/* gain hi stop 32 (22) (6:1) lo stop 7 (10:7) target 22 (13) (15:11) */
	le_val = (ppd->dd->cspec->r1 || IS_QME(ppd->dd)) ? 0xb6c0 : 0x6bac;
	ibsd_wr_allchans(ppd, 21, le_val, 0xfffe);
	/*       Enable VGA */
	ibsd_wr_allchans(ppd, 5, 0, BMASK(0, 0));
	msleep(20);
	/*       Set Frequency Loop Bandwidth */
	ibsd_wr_allchans(ppd, 2, (15 << 5), BMASK(8, 5));
	/*       Enable Frequency Loop */
	ibsd_wr_allchans(ppd, 2, (1 << 4), BMASK(4, 4));
	/*       Set Timing Loop Bandwidth */
	ibsd_wr_allchans(ppd, 2, 0, BMASK(11, 9));
	/*       Enable Timing Loop */
	ibsd_wr_allchans(ppd, 2, (1 << 3), BMASK(3, 3));
	msleep(50);
	/*       Enable DFE
	 *       Set receive adaptation mode.  SDR and DDR adaptation are
	 *       always on, and QDR is initially enabled; later disabled.
	 */
	qib_write_kreg_port(ppd, krp_static_adapt_dis(0), 0ULL);
	qib_write_kreg_port(ppd, krp_static_adapt_dis(1), 0ULL);
	qib_write_kreg_port(ppd, krp_static_adapt_dis(2),
			    ppd->dd->cspec->r1 ?
			    QDR_STATIC_ADAPT_DOWN_R1 : QDR_STATIC_ADAPT_DOWN);
	ppd->cpspec->qdr_dfe_on = 1;
	/*       Disable LE1  */
	ibsd_wr_allchans(ppd, 13, (0 << 5), (1 << 5));
	/*       Disable auto adapt for LE1 */
	ibsd_wr_allchans(ppd, 1, (0 << 15), BMASK(15, 15));
	msleep(20);
	/*       Enable AFE Offset Cancel */
	ibsd_wr_allchans(ppd, 12, (1 << 12), BMASK(12, 12));
	/*       Enable Baseline Wander Correction */
	ibsd_wr_allchans(ppd, 12, (1 << 13), BMASK(13, 13));
	/* Termination: rxtermctrl_r2d addr 11 bits [12:11] = 1 */
	ibsd_wr_allchans(ppd, 11, (1 << 11), BMASK(12, 11));
	/* VGA output common mode */
	ibsd_wr_allchans(ppd, 12, (3 << 2), BMASK(3, 2));

	/*
	 * Initialize the Tx DDS tables.  Also done every QSFP event,
	 * for adapters with QSFP
	 */
	init_txdds_table(ppd, 0);

	return 0;
}

/* start adjust QMH serdes parameters */

static void set_man_code(struct qib_pportdata *ppd, int chan, int code)
{
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), (chan + (chan >> 1)),
		9, code << 9, 0x3f << 9);
}

static void set_man_mode_h1(struct qib_pportdata *ppd, int chan,
	int enable, u32 tapenable)
{
	if (enable)
		ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), (chan + (chan >> 1)),
			1, 3 << 10, 0x1f << 10);
	else
		ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), (chan + (chan >> 1)),
			1, 0, 0x1f << 10);
}

/* Set clock to 1, 0, 1, 0 */
static void clock_man(struct qib_pportdata *ppd, int chan)
{
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), (chan + (chan >> 1)),
		4, 0x4000, 0x4000);
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), (chan + (chan >> 1)),
		4, 0, 0x4000);
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), (chan + (chan >> 1)),
		4, 0x4000, 0x4000);
	ahb_mod(ppd->dd, IBSD(ppd->hw_pidx), (chan + (chan >> 1)),
		4, 0, 0x4000);
}

/*
 * write the current Tx serdes pre,post,main,amp settings into the serdes.
 * The caller must pass the settings appropriate for the current speed,
 * or not care if they are correct for the current speed.
 */
static void write_tx_serdes_param(struct qib_pportdata *ppd,
				  struct txdds_ent *txdds)
{
	u64 deemph;

	deemph = qib_read_kreg_port(ppd, krp_tx_deemph_override);
	/* field names for amp, main, post, pre, respectively */
	deemph &= ~(SYM_MASK(IBSD_TX_DEEMPHASIS_OVERRIDE_0, txampcntl_d2a) |
		    SYM_MASK(IBSD_TX_DEEMPHASIS_OVERRIDE_0, txc0_ena) |
		    SYM_MASK(IBSD_TX_DEEMPHASIS_OVERRIDE_0, txcp1_ena) |
		    SYM_MASK(IBSD_TX_DEEMPHASIS_OVERRIDE_0, txcn1_ena));

	deemph |= SYM_MASK(IBSD_TX_DEEMPHASIS_OVERRIDE_0,
			   tx_override_deemphasis_select);
	deemph |= (txdds->amp & SYM_RMASK(IBSD_TX_DEEMPHASIS_OVERRIDE_0,
		    txampcntl_d2a)) << SYM_LSB(IBSD_TX_DEEMPHASIS_OVERRIDE_0,
				       txampcntl_d2a);
	deemph |= (txdds->main & SYM_RMASK(IBSD_TX_DEEMPHASIS_OVERRIDE_0,
		     txc0_ena)) << SYM_LSB(IBSD_TX_DEEMPHASIS_OVERRIDE_0,
				   txc0_ena);
	deemph |= (txdds->post & SYM_RMASK(IBSD_TX_DEEMPHASIS_OVERRIDE_0,
		     txcp1_ena)) << SYM_LSB(IBSD_TX_DEEMPHASIS_OVERRIDE_0,
				    txcp1_ena);
	deemph |= (txdds->pre & SYM_RMASK(IBSD_TX_DEEMPHASIS_OVERRIDE_0,
		     txcn1_ena)) << SYM_LSB(IBSD_TX_DEEMPHASIS_OVERRIDE_0,
				    txcn1_ena);
	qib_write_kreg_port(ppd, krp_tx_deemph_override, deemph);
}

/*
 * Set the parameters for mez cards on link bounce, so they are
 * always exactly what was requested.  Similar logic to init_txdds
 * but does just the serdes.
 */
static void adj_tx_serdes(struct qib_pportdata *ppd)
{
	const struct txdds_ent *sdr_dds, *ddr_dds, *qdr_dds;
	struct txdds_ent *dds;

	find_best_ent(ppd, &sdr_dds, &ddr_dds, &qdr_dds, 1);
	dds = (struct txdds_ent *)(ppd->link_speed_active == QIB_IB_QDR ?
		qdr_dds : (ppd->link_speed_active == QIB_IB_DDR ?
				ddr_dds : sdr_dds));
	write_tx_serdes_param(ppd, dds);
}

/* set QDR forced value for H1, if needed */
static void force_h1(struct qib_pportdata *ppd)
{
	int chan;

	ppd->cpspec->qdr_reforce = 0;
	if (!ppd->dd->cspec->r1)
		return;

	for (chan = 0; chan < SERDES_CHANS; chan++) {
		set_man_mode_h1(ppd, chan, 1, 0);
		set_man_code(ppd, chan, ppd->cpspec->h1_val);
		clock_man(ppd, chan);
		set_man_mode_h1(ppd, chan, 0, 0);
	}
}

#define SJA_EN SYM_MASK(SPC_JTAG_ACCESS_REG, SPC_JTAG_ACCESS_EN)
#define BISTEN_LSB SYM_LSB(SPC_JTAG_ACCESS_REG, bist_en)

#define R_OPCODE_LSB 3
#define R_OP_NOP 0
#define R_OP_SHIFT 2
#define R_OP_UPDATE 3
#define R_TDI_LSB 2
#define R_TDO_LSB 1
#define R_RDY 1

static int qib_r_grab(struct qib_devdata *dd)
{
	u64 val = SJA_EN;

	qib_write_kreg(dd, kr_r_access, val);
	qib_read_kreg32(dd, kr_scratch);
	return 0;
}

/* qib_r_wait_for_rdy() not only waits for the ready bit, it
 * returns the current state of R_TDO
 */
static int qib_r_wait_for_rdy(struct qib_devdata *dd)
{
	u64 val;
	int timeout;

	for (timeout = 0; timeout < 100 ; ++timeout) {
		val = qib_read_kreg32(dd, kr_r_access);
		if (val & R_RDY)
			return (val >> R_TDO_LSB) & 1;
	}
	return -1;
}

static int qib_r_shift(struct qib_devdata *dd, int bisten,
		       int len, u8 *inp, u8 *outp)
{
	u64 valbase, val;
	int ret, pos;

	valbase = SJA_EN | (bisten << BISTEN_LSB) |
		(R_OP_SHIFT << R_OPCODE_LSB);
	ret = qib_r_wait_for_rdy(dd);
	if (ret < 0)
		goto bail;
	for (pos = 0; pos < len; ++pos) {
		val = valbase;
		if (outp) {
			outp[pos >> 3] &= ~(1 << (pos & 7));
			outp[pos >> 3] |= (ret << (pos & 7));
		}
		if (inp) {
			int tdi = inp[pos >> 3] >> (pos & 7);

			val |= ((tdi & 1) << R_TDI_LSB);
		}
		qib_write_kreg(dd, kr_r_access, val);
		qib_read_kreg32(dd, kr_scratch);
		ret = qib_r_wait_for_rdy(dd);
		if (ret < 0)
			break;
	}
	/* Restore to NOP between operations. */
	val =  SJA_EN | (bisten << BISTEN_LSB);
	qib_write_kreg(dd, kr_r_access, val);
	qib_read_kreg32(dd, kr_scratch);
	ret = qib_r_wait_for_rdy(dd);

	if (ret >= 0)
		ret = pos;
bail:
	return ret;
}

static int qib_r_update(struct qib_devdata *dd, int bisten)
{
	u64 val;
	int ret;

	val = SJA_EN | (bisten << BISTEN_LSB) | (R_OP_UPDATE << R_OPCODE_LSB);
	ret = qib_r_wait_for_rdy(dd);
	if (ret >= 0) {
		qib_write_kreg(dd, kr_r_access, val);
		qib_read_kreg32(dd, kr_scratch);
	}
	return ret;
}

#define BISTEN_PORT_SEL 15
#define LEN_PORT_SEL 625
#define BISTEN_AT 17
#define LEN_AT 156
#define BISTEN_ETM 16
#define LEN_ETM 632

#define BIT2BYTE(x) (((x) +  BITS_PER_BYTE - 1) / BITS_PER_BYTE)

/* these are common for all IB port use cases. */
static u8 reset_at[BIT2BYTE(LEN_AT)] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00,
};
static u8 reset_atetm[BIT2BYTE(LEN_ETM)] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x80, 0xe3, 0x81, 0x73, 0x3c, 0x70, 0x8e,
	0x07, 0xce, 0xf1, 0xc0, 0x39, 0x1e, 0x38, 0xc7, 0x03, 0xe7,
	0x78, 0xe0, 0x1c, 0x0f, 0x9c, 0x7f, 0x80, 0x73, 0x0f, 0x70,
	0xde, 0x01, 0xce, 0x39, 0xc0, 0xf9, 0x06, 0x38, 0xd7, 0x00,
	0xe7, 0x19, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
};
static u8 at[BIT2BYTE(LEN_AT)] = {
	0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00,
};

/* used for IB1 or IB2, only one in use */
static u8 atetm_1port[BIT2BYTE(LEN_ETM)] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x10, 0xf2, 0x80, 0x83, 0x1e, 0x38, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x50, 0xf4, 0x41, 0x00, 0x18, 0x78, 0xc8, 0x03,
	0x07, 0x7b, 0xa0, 0x3e, 0x00, 0x02, 0x00, 0x00, 0x18, 0x00,
	0x18, 0x00, 0x00, 0x00, 0x00, 0x4b, 0x00, 0x00, 0x00,
};

/* used when both IB1 and IB2 are in use */
static u8 atetm_2port[BIT2BYTE(LEN_ETM)] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79,
	0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0xf8, 0x80, 0x83, 0x1e, 0x38, 0xe0, 0x03, 0x05,
	0x7b, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
	0xa2, 0x0f, 0x50, 0xf4, 0x41, 0x00, 0x18, 0x78, 0xd1, 0x07,
	0x02, 0x7c, 0x80, 0x3e, 0x00, 0x02, 0x00, 0x00, 0x3e, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00,
};

/* used when only IB1 is in use */
static u8 portsel_port1[BIT2BYTE(LEN_PORT_SEL)] = {
	0x32, 0x65, 0xa4, 0x7b, 0x10, 0x98, 0xdc, 0xfe, 0x13, 0x13,
	0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x73, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13,
	0x13, 0x78, 0x78, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13,
	0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x74, 0x32,
	0x32, 0x32, 0x32, 0x32, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
	0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
	0x14, 0x14, 0x9f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* used when only IB2 is in use */
static u8 portsel_port2[BIT2BYTE(LEN_PORT_SEL)] = {
	0x32, 0x65, 0xa4, 0x7b, 0x10, 0x98, 0xdc, 0xfe, 0x39, 0x39,
	0x39, 0x39, 0x39, 0x39, 0x39, 0x39, 0x73, 0x32, 0x32, 0x32,
	0x32, 0x32, 0x39, 0x39, 0x39, 0x39, 0x39, 0x39, 0x39, 0x39,
	0x39, 0x78, 0x78, 0x39, 0x39, 0x39, 0x39, 0x39, 0x39, 0x39,
	0x3a, 0x3a, 0x3a, 0x3a, 0x3a, 0x3a, 0x3a, 0x3a, 0x74, 0x32,
	0x32, 0x32, 0x32, 0x32, 0x3a, 0x3a, 0x3a, 0x3a, 0x3a, 0x3a,
	0x3a, 0x3a, 0x3a, 0x3a, 0x3a, 0x3a, 0x3a, 0x3a, 0x3a, 0x3a,
	0x3a, 0x3a, 0x9f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01,
};

/* used when both IB1 and IB2 are in use */
static u8 portsel_2port[BIT2BYTE(LEN_PORT_SEL)] = {
	0x32, 0xba, 0x54, 0x76, 0x10, 0x98, 0xdc, 0xfe, 0x13, 0x13,
	0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x73, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13,
	0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13,
	0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x74, 0x32,
	0x32, 0x32, 0x32, 0x32, 0x14, 0x14, 0x14, 0x14, 0x14, 0x3a,
	0x3a, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
	0x14, 0x14, 0x9f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/*
 * Do setup to properly handle IB link recovery; if port is zero, we
 * are initializing to cover both ports; otherwise we are initializing
 * to cover a single port card, or the port has reached INIT and we may
 * need to switch coverage types.
 */
static void setup_7322_link_recovery(struct qib_pportdata *ppd, u32 both)
{
	u8 *portsel, *etm;
	struct qib_devdata *dd = ppd->dd;

	if (!ppd->dd->cspec->r1)
		return;
	if (!both) {
		dd->cspec->recovery_ports_initted++;
		ppd->cpspec->recovery_init = 1;
	}
	if (!both && dd->cspec->recovery_ports_initted == 1) {
		portsel = ppd->port == 1 ? portsel_port1 : portsel_port2;
		etm = atetm_1port;
	} else {
		portsel = portsel_2port;
		etm = atetm_2port;
	}

	if (qib_r_grab(dd) < 0 ||
		qib_r_shift(dd, BISTEN_ETM, LEN_ETM, reset_atetm, NULL) < 0 ||
		qib_r_update(dd, BISTEN_ETM) < 0 ||
		qib_r_shift(dd, BISTEN_AT, LEN_AT, reset_at, NULL) < 0 ||
		qib_r_update(dd, BISTEN_AT) < 0 ||
		qib_r_shift(dd, BISTEN_PORT_SEL, LEN_PORT_SEL,
			    portsel, NULL) < 0 ||
		qib_r_update(dd, BISTEN_PORT_SEL) < 0 ||
		qib_r_shift(dd, BISTEN_AT, LEN_AT, at, NULL) < 0 ||
		qib_r_update(dd, BISTEN_AT) < 0 ||
		qib_r_shift(dd, BISTEN_ETM, LEN_ETM, etm, NULL) < 0 ||
		qib_r_update(dd, BISTEN_ETM) < 0)
		qib_dev_err(dd, "Failed IB link recovery setup\n");
}

static void check_7322_rxe_status(struct qib_pportdata *ppd)
{
	struct qib_devdata *dd = ppd->dd;
	u64 fmask;

	if (dd->cspec->recovery_ports_initted != 1)
		return; /* rest doesn't apply to dualport */
	qib_write_kreg(dd, kr_control, dd->control |
		       SYM_MASK(Control, FreezeMode));
	(void)qib_read_kreg64(dd, kr_scratch);
	udelay(3); /* ibcreset asserted 400ns, be sure that's over */
	fmask = qib_read_kreg64(dd, kr_act_fmask);
	if (!fmask) {
		/*
		 * require a powercycle before we'll work again, and make
		 * sure we get no more interrupts, and don't turn off
		 * freeze.
		 */
		ppd->dd->cspec->stay_in_freeze = 1;
		qib_7322_set_intr_state(ppd->dd, 0);
		qib_write_kreg(dd, kr_fmask, 0ULL);
		qib_dev_err(dd, "HCA unusable until powercycled\n");
		return; /* eventually reset */
	}

	qib_write_kreg(ppd->dd, kr_hwerrclear,
	    SYM_MASK(HwErrClear, IBSerdesPClkNotDetectClear_1));

	/* don't do the full clear_freeze(), not needed for this */
	qib_write_kreg(dd, kr_control, dd->control);
	qib_read_kreg32(dd, kr_scratch);
	/* take IBC out of reset */
	if (ppd->link_speed_supported) {
		ppd->cpspec->ibcctrl_a &=
			~SYM_MASK(IBCCtrlA_0, IBStatIntReductionEn);
		qib_write_kreg_port(ppd, krp_ibcctrl_a,
				    ppd->cpspec->ibcctrl_a);
		qib_read_kreg32(dd, kr_scratch);
		if (ppd->lflags & QIBL_IB_LINK_DISABLED)
			qib_set_ib_7322_lstate(ppd, 0,
				QLOGIC_IB_IBCC_LINKINITCMD_DISABLE);
	}
}
