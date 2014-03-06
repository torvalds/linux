/* drivers/usb/gadget/s3c-hsotg.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      http://armlinux.simtec.co.uk/
 *      Ben Dooks <ben@simtec.co.uk>
 *
 * USB2.0 Highspeed/OtG Synopsis DWC2 device block registers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __REGS_USB_HSOTG_H
#define __REGS_USB_HSOTG_H __FILE__

#define HSOTG_REG(x) (x)

#define GOTGCTL				HSOTG_REG(0x000)
#define GOTGCTL_BSESVLD			(1 << 19)
#define GOTGCTL_ASESVLD			(1 << 18)
#define GOTGCTL_DBNC_SHORT			(1 << 17)
#define GOTGCTL_CONID_B			(1 << 16)
#define GOTGCTL_DEVHNPEN			(1 << 11)
#define GOTGCTL_HSSETHNPEN			(1 << 10)
#define GOTGCTL_HNPREQ				(1 << 9)
#define GOTGCTL_HSTNEGSCS			(1 << 8)
#define GOTGCTL_SESREQ				(1 << 1)
#define GOTGCTL_SESREQSCS			(1 << 0)

#define GOTGINT				HSOTG_REG(0x004)
#define GOTGINT_DbnceDone			(1 << 19)
#define GOTGINT_ADevTOUTChg			(1 << 18)
#define GOTGINT_HstNegDet			(1 << 17)
#define GOTGINT_HstnegSucStsChng		(1 << 9)
#define GOTGINT_SesReqSucStsChng		(1 << 8)
#define GOTGINT_SesEndDet			(1 << 2)

#define GAHBCFG				HSOTG_REG(0x008)
#define GAHBCFG_PTxFEmpLvl			(1 << 8)
#define GAHBCFG_NPTxFEmpLvl			(1 << 7)
#define GAHBCFG_DMAEn				(1 << 5)
#define GAHBCFG_HBstLen_MASK			(0xf << 1)
#define GAHBCFG_HBstLen_SHIFT			(1)
#define GAHBCFG_HBstLen_Single			(0x0 << 1)
#define GAHBCFG_HBstLen_Incr			(0x1 << 1)
#define GAHBCFG_HBstLen_Incr4			(0x3 << 1)
#define GAHBCFG_HBstLen_Incr8			(0x5 << 1)
#define GAHBCFG_HBstLen_Incr16			(0x7 << 1)
#define GAHBCFG_GlblIntrEn			(1 << 0)

#define GUSBCFG				HSOTG_REG(0x00C)
#define GUSBCFG_PHYLPClkSel			(1 << 15)
#define GUSBCFG_HNPCap				(1 << 9)
#define GUSBCFG_SRPCap				(1 << 8)
#define GUSBCFG_PHYIf16			(1 << 3)
#define GUSBCFG_PHYIf8				(0 << 3)
#define GUSBCFG_TOutCal_MASK			(0x7 << 0)
#define GUSBCFG_TOutCal_SHIFT			(0)
#define GUSBCFG_TOutCal_LIMIT			(0x7)
#define GUSBCFG_TOutCal(_x)			((_x) << 0)

#define GRSTCTL				HSOTG_REG(0x010)

#define GRSTCTL_AHBIdle			(1 << 31)
#define GRSTCTL_DMAReq				(1 << 30)
#define GRSTCTL_TxFNum_MASK			(0x1f << 6)
#define GRSTCTL_TxFNum_SHIFT			(6)
#define GRSTCTL_TxFNum_LIMIT			(0x1f)
#define GRSTCTL_TxFNum(_x)			((_x) << 6)
#define GRSTCTL_TxFFlsh			(1 << 5)
#define GRSTCTL_RxFFlsh			(1 << 4)
#define GRSTCTL_INTknQFlsh			(1 << 3)
#define GRSTCTL_FrmCntrRst			(1 << 2)
#define GRSTCTL_HSftRst			(1 << 1)
#define GRSTCTL_CSftRst			(1 << 0)

#define GINTSTS				HSOTG_REG(0x014)
#define GINTMSK				HSOTG_REG(0x018)

#define GINTSTS_WkUpInt			(1 << 31)
#define GINTSTS_SessReqInt			(1 << 30)
#define GINTSTS_DisconnInt			(1 << 29)
#define GINTSTS_ConIDStsChng			(1 << 28)
#define GINTSTS_PTxFEmp			(1 << 26)
#define GINTSTS_HChInt				(1 << 25)
#define GINTSTS_PrtInt				(1 << 24)
#define GINTSTS_FetSusp			(1 << 22)
#define GINTSTS_incompIP			(1 << 21)
#define GINTSTS_IncomplSOIN			(1 << 20)
#define GINTSTS_OEPInt				(1 << 19)
#define GINTSTS_IEPInt				(1 << 18)
#define GINTSTS_EPMis				(1 << 17)
#define GINTSTS_EOPF				(1 << 15)
#define GINTSTS_ISOutDrop			(1 << 14)
#define GINTSTS_EnumDone			(1 << 13)
#define GINTSTS_USBRst				(1 << 12)
#define GINTSTS_USBSusp			(1 << 11)
#define GINTSTS_ErlySusp			(1 << 10)
#define GINTSTS_GOUTNakEff			(1 << 7)
#define GINTSTS_GINNakEff			(1 << 6)
#define GINTSTS_NPTxFEmp			(1 << 5)
#define GINTSTS_RxFLvl				(1 << 4)
#define GINTSTS_SOF				(1 << 3)
#define GINTSTS_OTGInt				(1 << 2)
#define GINTSTS_ModeMis			(1 << 1)
#define GINTSTS_CurMod_Host			(1 << 0)

#define GRXSTSR				HSOTG_REG(0x01C)
#define GRXSTSP				HSOTG_REG(0x020)

#define GRXSTS_FN_MASK				(0x7f << 25)
#define GRXSTS_FN_SHIFT			(25)

#define GRXSTS_PktSts_MASK			(0xf << 17)
#define GRXSTS_PktSts_SHIFT			(17)
#define GRXSTS_PktSts_GlobalOutNAK		(0x1 << 17)
#define GRXSTS_PktSts_OutRX			(0x2 << 17)
#define GRXSTS_PktSts_OutDone			(0x3 << 17)
#define GRXSTS_PktSts_SetupDone		(0x4 << 17)
#define GRXSTS_PktSts_SetupRX			(0x6 << 17)

#define GRXSTS_DPID_MASK			(0x3 << 15)
#define GRXSTS_DPID_SHIFT			(15)
#define GRXSTS_ByteCnt_MASK			(0x7ff << 4)
#define GRXSTS_ByteCnt_SHIFT			(4)
#define GRXSTS_EPNum_MASK			(0xf << 0)
#define GRXSTS_EPNum_SHIFT			(0)

#define GRXFSIZ				HSOTG_REG(0x024)

#define GNPTXFSIZ				HSOTG_REG(0x028)

#define GNPTXFSIZ_NPTxFDep_MASK		(0xffff << 16)
#define GNPTXFSIZ_NPTxFDep_SHIFT		(16)
#define GNPTXFSIZ_NPTxFDep_LIMIT		(0xffff)
#define GNPTXFSIZ_NPTxFDep(_x)			((_x) << 16)
#define GNPTXFSIZ_NPTxFStAddr_MASK		(0xffff << 0)
#define GNPTXFSIZ_NPTxFStAddr_SHIFT		(0)
#define GNPTXFSIZ_NPTxFStAddr_LIMIT		(0xffff)
#define GNPTXFSIZ_NPTxFStAddr(_x)		((_x) << 0)

#define GNPTXSTS				HSOTG_REG(0x02C)

#define GNPTXSTS_NPtxQTop_MASK			(0x7f << 24)
#define GNPTXSTS_NPtxQTop_SHIFT		(24)

#define GNPTXSTS_NPTxQSpcAvail_MASK		(0xff << 16)
#define GNPTXSTS_NPTxQSpcAvail_SHIFT		(16)
#define GNPTXSTS_NPTxQSpcAvail_GET(_v)		(((_v) >> 16) & 0xff)

#define GNPTXSTS_NPTxFSpcAvail_MASK		(0xffff << 0)
#define GNPTXSTS_NPTxFSpcAvail_SHIFT		(0)
#define GNPTXSTS_NPTxFSpcAvail_GET(_v)		(((_v) >> 0) & 0xffff)


#define HPTXFSIZ				HSOTG_REG(0x100)

#define DPTXFSIZn(_a)		HSOTG_REG(0x104 + (((_a) - 1) * 4))

#define DPTXFSIZn_DPTxFSize_MASK		(0xffff << 16)
#define DPTXFSIZn_DPTxFSize_SHIFT		(16)
#define DPTXFSIZn_DPTxFSize_GET(_v)		(((_v) >> 16) & 0xffff)
#define DPTXFSIZn_DPTxFSize_LIMIT		(0xffff)
#define DPTXFSIZn_DPTxFSize(_x)		((_x) << 16)

#define DPTXFSIZn_DPTxFStAddr_MASK		(0xffff << 0)
#define DPTXFSIZn_DPTxFStAddr_SHIFT		(0)

/* Device mode registers */
#define DCFG					HSOTG_REG(0x800)

#define DCFG_EPMisCnt_MASK			(0x1f << 18)
#define DCFG_EPMisCnt_SHIFT			(18)
#define DCFG_EPMisCnt_LIMIT			(0x1f)
#define DCFG_EPMisCnt(_x)			((_x) << 18)

#define DCFG_PerFrInt_MASK			(0x3 << 11)
#define DCFG_PerFrInt_SHIFT			(11)
#define DCFG_PerFrInt_LIMIT			(0x3)
#define DCFG_PerFrInt(_x)			((_x) << 11)

#define DCFG_DevAddr_MASK			(0x7f << 4)
#define DCFG_DevAddr_SHIFT			(4)
#define DCFG_DevAddr_LIMIT			(0x7f)
#define DCFG_DevAddr(_x)			((_x) << 4)

#define DCFG_NZStsOUTHShk			(1 << 2)

#define DCFG_DevSpd_MASK			(0x3 << 0)
#define DCFG_DevSpd_SHIFT			(0)
#define DCFG_DevSpd_HS				(0x0 << 0)
#define DCFG_DevSpd_FS				(0x1 << 0)
#define DCFG_DevSpd_LS				(0x2 << 0)
#define DCFG_DevSpd_FS48			(0x3 << 0)

#define DCTL					HSOTG_REG(0x804)

#define DCTL_PWROnPrgDone			(1 << 11)
#define DCTL_CGOUTNak				(1 << 10)
#define DCTL_SGOUTNak				(1 << 9)
#define DCTL_CGNPInNAK				(1 << 8)
#define DCTL_SGNPInNAK				(1 << 7)
#define DCTL_TstCtl_MASK			(0x7 << 4)
#define DCTL_TstCtl_SHIFT			(4)
#define DCTL_GOUTNakSts			(1 << 3)
#define DCTL_GNPINNakSts			(1 << 2)
#define DCTL_SftDiscon				(1 << 1)
#define DCTL_RmtWkUpSig			(1 << 0)

#define DSTS					HSOTG_REG(0x808)

#define DSTS_SOFFN_MASK			(0x3fff << 8)
#define DSTS_SOFFN_SHIFT			(8)
#define DSTS_SOFFN_LIMIT			(0x3fff)
#define DSTS_SOFFN(_x)				((_x) << 8)
#define DSTS_ErraticErr			(1 << 3)
#define DSTS_EnumSpd_MASK			(0x3 << 1)
#define DSTS_EnumSpd_SHIFT			(1)
#define DSTS_EnumSpd_HS			(0x0 << 1)
#define DSTS_EnumSpd_FS			(0x1 << 1)
#define DSTS_EnumSpd_LS			(0x2 << 1)
#define DSTS_EnumSpd_FS48			(0x3 << 1)

#define DSTS_SuspSts				(1 << 0)

#define DIEPMSK				HSOTG_REG(0x810)

#define DIEPMSK_TxFIFOEmpty			(1 << 7)
#define DIEPMSK_INEPNakEffMsk			(1 << 6)
#define DIEPMSK_INTknEPMisMsk			(1 << 5)
#define DIEPMSK_INTknTXFEmpMsk			(1 << 4)
#define DIEPMSK_TimeOUTMsk			(1 << 3)
#define DIEPMSK_AHBErrMsk			(1 << 2)
#define DIEPMSK_EPDisbldMsk			(1 << 1)
#define DIEPMSK_XferComplMsk			(1 << 0)

#define DOEPMSK				HSOTG_REG(0x814)

#define DOEPMSK_Back2BackSetup			(1 << 6)
#define DOEPMSK_OUTTknEPdisMsk			(1 << 4)
#define DOEPMSK_SetupMsk			(1 << 3)
#define DOEPMSK_AHBErrMsk			(1 << 2)
#define DOEPMSK_EPDisbldMsk			(1 << 1)
#define DOEPMSK_XferComplMsk			(1 << 0)

#define DAINT					HSOTG_REG(0x818)
#define DAINTMSK				HSOTG_REG(0x81C)

#define DAINT_OutEP_SHIFT			(16)
#define DAINT_OutEP(x)				(1 << ((x) + 16))
#define DAINT_InEP(x)				(1 << (x))

#define DTKNQR1				HSOTG_REG(0x820)
#define DTKNQR2				HSOTG_REG(0x824)
#define DTKNQR3				HSOTG_REG(0x830)
#define DTKNQR4				HSOTG_REG(0x834)

#define DVBUSDIS				HSOTG_REG(0x828)
#define DVBUSPULSE				HSOTG_REG(0x82C)

#define DIEPCTL0				HSOTG_REG(0x900)
#define DOEPCTL0				HSOTG_REG(0xB00)
#define DIEPCTL(_a)			HSOTG_REG(0x900 + ((_a) * 0x20))
#define DOEPCTL(_a)			HSOTG_REG(0xB00 + ((_a) * 0x20))

/* EP0 specialness:
 * bits[29..28] - reserved (no SetD0PID, SetD1PID)
 * bits[25..22] - should always be zero, this isn't a periodic endpoint
 * bits[10..0] - MPS setting differenct for EP0
 */
#define D0EPCTL_MPS_MASK			(0x3 << 0)
#define D0EPCTL_MPS_SHIFT			(0)
#define D0EPCTL_MPS_64				(0x0 << 0)
#define D0EPCTL_MPS_32				(0x1 << 0)
#define D0EPCTL_MPS_16				(0x2 << 0)
#define D0EPCTL_MPS_8				(0x3 << 0)

#define DxEPCTL_EPEna				(1 << 31)
#define DxEPCTL_EPDis				(1 << 30)
#define DxEPCTL_SetD1PID			(1 << 29)
#define DxEPCTL_SetOddFr			(1 << 29)
#define DxEPCTL_SetD0PID			(1 << 28)
#define DxEPCTL_SetEvenFr			(1 << 28)
#define DxEPCTL_SNAK				(1 << 27)
#define DxEPCTL_CNAK				(1 << 26)
#define DxEPCTL_TxFNum_MASK			(0xf << 22)
#define DxEPCTL_TxFNum_SHIFT			(22)
#define DxEPCTL_TxFNum_LIMIT			(0xf)
#define DxEPCTL_TxFNum(_x)			((_x) << 22)

#define DxEPCTL_Stall				(1 << 21)
#define DxEPCTL_Snp				(1 << 20)
#define DxEPCTL_EPType_MASK			(0x3 << 18)
#define DxEPCTL_EPType_SHIFT			(18)
#define DxEPCTL_EPType_Control			(0x0 << 18)
#define DxEPCTL_EPType_Iso			(0x1 << 18)
#define DxEPCTL_EPType_Bulk			(0x2 << 18)
#define DxEPCTL_EPType_Intterupt		(0x3 << 18)

#define DxEPCTL_NAKsts				(1 << 17)
#define DxEPCTL_DPID				(1 << 16)
#define DxEPCTL_EOFrNum			(1 << 16)
#define DxEPCTL_USBActEp			(1 << 15)
#define DxEPCTL_NextEp_MASK			(0xf << 11)
#define DxEPCTL_NextEp_SHIFT			(11)
#define DxEPCTL_NextEp_LIMIT			(0xf)
#define DxEPCTL_NextEp(_x)			((_x) << 11)

#define DxEPCTL_MPS_MASK			(0x7ff << 0)
#define DxEPCTL_MPS_SHIFT			(0)
#define DxEPCTL_MPS_LIMIT			(0x7ff)
#define DxEPCTL_MPS(_x)			((_x) << 0)

#define DIEPINT(_a)			HSOTG_REG(0x908 + ((_a) * 0x20))
#define DOEPINT(_a)			HSOTG_REG(0xB08 + ((_a) * 0x20))

#define DxEPINT_INEPNakEff			(1 << 6)
#define DxEPINT_Back2BackSetup			(1 << 6)
#define DxEPINT_INTknEPMis			(1 << 5)
#define DxEPINT_INTknTXFEmp			(1 << 4)
#define DxEPINT_OUTTknEPdis			(1 << 4)
#define DxEPINT_Timeout			(1 << 3)
#define DxEPINT_Setup				(1 << 3)
#define DxEPINT_AHBErr				(1 << 2)
#define DxEPINT_EPDisbld			(1 << 1)
#define DxEPINT_XferCompl			(1 << 0)

#define DIEPTSIZ0				HSOTG_REG(0x910)

#define DIEPTSIZ0_PktCnt_MASK			(0x3 << 19)
#define DIEPTSIZ0_PktCnt_SHIFT			(19)
#define DIEPTSIZ0_PktCnt_LIMIT			(0x3)
#define DIEPTSIZ0_PktCnt(_x)			((_x) << 19)

#define DIEPTSIZ0_XferSize_MASK		(0x7f << 0)
#define DIEPTSIZ0_XferSize_SHIFT		(0)
#define DIEPTSIZ0_XferSize_LIMIT		(0x7f)
#define DIEPTSIZ0_XferSize(_x)			((_x) << 0)

#define DOEPTSIZ0				HSOTG_REG(0xB10)
#define DOEPTSIZ0_SUPCnt_MASK			(0x3 << 29)
#define DOEPTSIZ0_SUPCnt_SHIFT			(29)
#define DOEPTSIZ0_SUPCnt_LIMIT			(0x3)
#define DOEPTSIZ0_SUPCnt(_x)			((_x) << 29)

#define DOEPTSIZ0_PktCnt			(1 << 19)
#define DOEPTSIZ0_XferSize_MASK		(0x7f << 0)
#define DOEPTSIZ0_XferSize_SHIFT		(0)

#define DIEPTSIZ(_a)			HSOTG_REG(0x910 + ((_a) * 0x20))
#define DOEPTSIZ(_a)			HSOTG_REG(0xB10 + ((_a) * 0x20))

#define DxEPTSIZ_MC_MASK			(0x3 << 29)
#define DxEPTSIZ_MC_SHIFT			(29)
#define DxEPTSIZ_MC_LIMIT			(0x3)
#define DxEPTSIZ_MC(_x)			((_x) << 29)

#define DxEPTSIZ_PktCnt_MASK			(0x3ff << 19)
#define DxEPTSIZ_PktCnt_SHIFT			(19)
#define DxEPTSIZ_PktCnt_GET(_v)		(((_v) >> 19) & 0x3ff)
#define DxEPTSIZ_PktCnt_LIMIT			(0x3ff)
#define DxEPTSIZ_PktCnt(_x)			((_x) << 19)

#define DxEPTSIZ_XferSize_MASK			(0x7ffff << 0)
#define DxEPTSIZ_XferSize_SHIFT		(0)
#define DxEPTSIZ_XferSize_GET(_v)		(((_v) >> 0) & 0x7ffff)
#define DxEPTSIZ_XferSize_LIMIT		(0x7ffff)
#define DxEPTSIZ_XferSize(_x)			((_x) << 0)

#define DIEPDMA(_a)			HSOTG_REG(0x914 + ((_a) * 0x20))
#define DOEPDMA(_a)			HSOTG_REG(0xB14 + ((_a) * 0x20))
#define DTXFSTS(_a)			HSOTG_REG(0x918 + ((_a) * 0x20))

#define EPFIFO(_a)			HSOTG_REG(0x1000 + ((_a) * 0x1000))

#endif /* __REGS_USB_HSOTG_H */
