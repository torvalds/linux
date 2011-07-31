/* arch/arm/plat-s3c/include/plat/regs-usb-hsotg.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      http://armlinux.simtec.co.uk/
 *      Ben Dooks <ben@simtec.co.uk>
 *
 * S3C - USB2.0 Highspeed/OtG device block registers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __PLAT_S3C64XX_REGS_USB_HSOTG_H
#define __PLAT_S3C64XX_REGS_USB_HSOTG_H __FILE__

#define S3C_HSOTG_REG(x) (x)

#define S3C_GOTGCTL				S3C_HSOTG_REG(0x000)
#define S3C_GOTGCTL_BSESVLD			(1 << 19)
#define S3C_GOTGCTL_ASESVLD			(1 << 18)
#define S3C_GOTGCTL_DBNC_SHORT			(1 << 17)
#define S3C_GOTGCTL_CONID_B			(1 << 16)
#define S3C_GOTGCTL_DEVHNPEN			(1 << 11)
#define S3C_GOTGCTL_HSSETHNPEN			(1 << 10)
#define S3C_GOTGCTL_HNPREQ			(1 << 9)
#define S3C_GOTGCTL_HSTNEGSCS			(1 << 8)
#define S3C_GOTGCTL_SESREQ			(1 << 1)
#define S3C_GOTGCTL_SESREQSCS			(1 << 0)

#define S3C_GOTGINT				S3C_HSOTG_REG(0x004)
#define S3C_GOTGINT_DbnceDone			(1 << 19)
#define S3C_GOTGINT_ADevTOUTChg			(1 << 18)
#define S3C_GOTGINT_HstNegDet			(1 << 17)
#define S3C_GOTGINT_HstnegSucStsChng		(1 << 9)
#define S3C_GOTGINT_SesReqSucStsChng		(1 << 8)
#define S3C_GOTGINT_SesEndDet			(1 << 2)

#define S3C_GAHBCFG				S3C_HSOTG_REG(0x008)
#define S3C_GAHBCFG_PTxFEmpLvl			(1 << 8)
#define S3C_GAHBCFG_NPTxFEmpLvl			(1 << 7)
#define S3C_GAHBCFG_DMAEn			(1 << 5)
#define S3C_GAHBCFG_HBstLen_MASK		(0xf << 1)
#define S3C_GAHBCFG_HBstLen_SHIFT		(1)
#define S3C_GAHBCFG_HBstLen_Single		(0x0 << 1)
#define S3C_GAHBCFG_HBstLen_Incr		(0x1 << 1)
#define S3C_GAHBCFG_HBstLen_Incr4		(0x3 << 1)
#define S3C_GAHBCFG_HBstLen_Incr8		(0x5 << 1)
#define S3C_GAHBCFG_HBstLen_Incr16		(0x7 << 1)
#define S3C_GAHBCFG_GlblIntrEn			(1 << 0)

#define S3C_GUSBCFG				S3C_HSOTG_REG(0x00C)
#define S3C_GUSBCFG_PHYLPClkSel			(1 << 15)
#define S3C_GUSBCFG_HNPCap			(1 << 9)
#define S3C_GUSBCFG_SRPCap			(1 << 8)
#define S3C_GUSBCFG_PHYIf16			(1 << 3)
#define S3C_GUSBCFG_TOutCal_MASK		(0x7 << 0)
#define S3C_GUSBCFG_TOutCal_SHIFT		(0)
#define S3C_GUSBCFG_TOutCal_LIMIT		(0x7)
#define S3C_GUSBCFG_TOutCal(_x)			((_x) << 0)

#define S3C_GRSTCTL				S3C_HSOTG_REG(0x010)

#define S3C_GRSTCTL_AHBIdle			(1 << 31)
#define S3C_GRSTCTL_DMAReq			(1 << 30)
#define S3C_GRSTCTL_TxFNum_MASK			(0x1f << 6)
#define S3C_GRSTCTL_TxFNum_SHIFT		(6)
#define S3C_GRSTCTL_TxFNum_LIMIT		(0x1f)
#define S3C_GRSTCTL_TxFNum(_x)			((_x) << 6)
#define S3C_GRSTCTL_TxFFlsh			(1 << 5)
#define S3C_GRSTCTL_RxFFlsh			(1 << 4)
#define S3C_GRSTCTL_INTknQFlsh			(1 << 3)
#define S3C_GRSTCTL_FrmCntrRst			(1 << 2)
#define S3C_GRSTCTL_HSftRst			(1 << 1)
#define S3C_GRSTCTL_CSftRst			(1 << 0)

#define S3C_GINTSTS				S3C_HSOTG_REG(0x014)
#define S3C_GINTMSK				S3C_HSOTG_REG(0x018)

#define S3C_GINTSTS_WkUpInt			(1 << 31)
#define S3C_GINTSTS_SessReqInt			(1 << 30)
#define S3C_GINTSTS_DisconnInt			(1 << 29)
#define S3C_GINTSTS_ConIDStsChng		(1 << 28)
#define S3C_GINTSTS_PTxFEmp			(1 << 26)
#define S3C_GINTSTS_HChInt			(1 << 25)
#define S3C_GINTSTS_PrtInt			(1 << 24)
#define S3C_GINTSTS_FetSusp			(1 << 22)
#define S3C_GINTSTS_incompIP			(1 << 21)
#define S3C_GINTSTS_IncomplSOIN			(1 << 20)
#define S3C_GINTSTS_OEPInt			(1 << 19)
#define S3C_GINTSTS_IEPInt			(1 << 18)
#define S3C_GINTSTS_EPMis			(1 << 17)
#define S3C_GINTSTS_EOPF			(1 << 15)
#define S3C_GINTSTS_ISOutDrop			(1 << 14)
#define S3C_GINTSTS_EnumDone			(1 << 13)
#define S3C_GINTSTS_USBRst			(1 << 12)
#define S3C_GINTSTS_USBSusp			(1 << 11)
#define S3C_GINTSTS_ErlySusp			(1 << 10)
#define S3C_GINTSTS_GOUTNakEff			(1 << 7)
#define S3C_GINTSTS_GINNakEff			(1 << 6)
#define S3C_GINTSTS_NPTxFEmp			(1 << 5)
#define S3C_GINTSTS_RxFLvl			(1 << 4)
#define S3C_GINTSTS_SOF				(1 << 3)
#define S3C_GINTSTS_OTGInt			(1 << 2)
#define S3C_GINTSTS_ModeMis			(1 << 1)
#define S3C_GINTSTS_CurMod_Host			(1 << 0)

#define S3C_GRXSTSR				S3C_HSOTG_REG(0x01C)
#define S3C_GRXSTSP				S3C_HSOTG_REG(0x020)

#define S3C_GRXSTS_FN_MASK			(0x7f << 25)
#define S3C_GRXSTS_FN_SHIFT			(25)

#define S3C_GRXSTS_PktSts_MASK			(0xf << 17)
#define S3C_GRXSTS_PktSts_SHIFT			(17)
#define S3C_GRXSTS_PktSts_GlobalOutNAK		(0x1 << 17)
#define S3C_GRXSTS_PktSts_OutRX			(0x2 << 17)
#define S3C_GRXSTS_PktSts_OutDone		(0x3 << 17)
#define S3C_GRXSTS_PktSts_SetupDone		(0x4 << 17)
#define S3C_GRXSTS_PktSts_SetupRX		(0x6 << 17)

#define S3C_GRXSTS_DPID_MASK			(0x3 << 15)
#define S3C_GRXSTS_DPID_SHIFT			(15)
#define S3C_GRXSTS_ByteCnt_MASK			(0x7ff << 4)
#define S3C_GRXSTS_ByteCnt_SHIFT		(4)
#define S3C_GRXSTS_EPNum_MASK			(0xf << 0)
#define S3C_GRXSTS_EPNum_SHIFT			(0)

#define S3C_GRXFSIZ				S3C_HSOTG_REG(0x024)

#define S3C_GNPTXFSIZ				S3C_HSOTG_REG(0x028)

#define S3C_GNPTXFSIZ_NPTxFDep_MASK		(0xffff << 16)
#define S3C_GNPTXFSIZ_NPTxFDep_SHIFT		(16)
#define S3C_GNPTXFSIZ_NPTxFDep_LIMIT		(0xffff)
#define S3C_GNPTXFSIZ_NPTxFDep(_x)		((_x) << 16)
#define S3C_GNPTXFSIZ_NPTxFStAddr_MASK		(0xffff << 0)
#define S3C_GNPTXFSIZ_NPTxFStAddr_SHIFT		(0)
#define S3C_GNPTXFSIZ_NPTxFStAddr_LIMIT		(0xffff)
#define S3C_GNPTXFSIZ_NPTxFStAddr(_x)		((_x) << 0)

#define S3C_GNPTXSTS				S3C_HSOTG_REG(0x02C)

#define S3C_GNPTXSTS_NPtxQTop_MASK		(0x7f << 24)
#define S3C_GNPTXSTS_NPtxQTop_SHIFT		(24)

#define S3C_GNPTXSTS_NPTxQSpcAvail_MASK		(0xff << 16)
#define S3C_GNPTXSTS_NPTxQSpcAvail_SHIFT	(16)
#define S3C_GNPTXSTS_NPTxQSpcAvail_GET(_v)	(((_v) >> 16) & 0xff)

#define S3C_GNPTXSTS_NPTxFSpcAvail_MASK		(0xffff << 0)
#define S3C_GNPTXSTS_NPTxFSpcAvail_SHIFT	(0)
#define S3C_GNPTXSTS_NPTxFSpcAvail_GET(_v)	(((_v) >> 0) & 0xffff)


#define S3C_HPTXFSIZ				S3C_HSOTG_REG(0x100)

#define S3C_DPTXFSIZn(_a)			S3C_HSOTG_REG(0x104 + (((_a) - 1) * 4))

#define S3C_DPTXFSIZn_DPTxFSize_MASK		(0xffff << 16)
#define S3C_DPTXFSIZn_DPTxFSize_SHIFT		(16)
#define S3C_DPTXFSIZn_DPTxFSize_GET(_v)		(((_v) >> 16) & 0xffff)
#define S3C_DPTXFSIZn_DPTxFSize_LIMIT		(0xffff)
#define S3C_DPTXFSIZn_DPTxFSize(_x)		((_x) << 16)

#define S3C_DPTXFSIZn_DPTxFStAddr_MASK		(0xffff << 0)
#define S3C_DPTXFSIZn_DPTxFStAddr_SHIFT		(0)

/* Device mode registers */
#define S3C_DCFG				S3C_HSOTG_REG(0x800)

#define S3C_DCFG_EPMisCnt_MASK			(0x1f << 18)
#define S3C_DCFG_EPMisCnt_SHIFT			(18)
#define S3C_DCFG_EPMisCnt_LIMIT			(0x1f)
#define S3C_DCFG_EPMisCnt(_x)			((_x) << 18)

#define S3C_DCFG_PerFrInt_MASK			(0x3 << 11)
#define S3C_DCFG_PerFrInt_SHIFT			(11)
#define S3C_DCFG_PerFrInt_LIMIT			(0x3)
#define S3C_DCFG_PerFrInt(_x)			((_x) << 11)

#define S3C_DCFG_DevAddr_MASK			(0x7f << 4)
#define S3C_DCFG_DevAddr_SHIFT			(4)
#define S3C_DCFG_DevAddr_LIMIT			(0x7f)
#define S3C_DCFG_DevAddr(_x)			((_x) << 4)

#define S3C_DCFG_NZStsOUTHShk			(1 << 2)

#define S3C_DCFG_DevSpd_MASK			(0x3 << 0)
#define S3C_DCFG_DevSpd_SHIFT			(0)
#define S3C_DCFG_DevSpd_HS			(0x0 << 0)
#define S3C_DCFG_DevSpd_FS			(0x1 << 0)
#define S3C_DCFG_DevSpd_LS			(0x2 << 0)
#define S3C_DCFG_DevSpd_FS48			(0x3 << 0)

#define S3C_DCTL				S3C_HSOTG_REG(0x804)

#define S3C_DCTL_PWROnPrgDone			(1 << 11)
#define S3C_DCTL_CGOUTNak			(1 << 10)
#define S3C_DCTL_SGOUTNak			(1 << 9)
#define S3C_DCTL_CGNPInNAK			(1 << 8)
#define S3C_DCTL_SGNPInNAK			(1 << 7)
#define S3C_DCTL_TstCtl_MASK			(0x7 << 4)
#define S3C_DCTL_TstCtl_SHIFT			(4)
#define S3C_DCTL_GOUTNakSts			(1 << 3)
#define S3C_DCTL_GNPINNakSts			(1 << 2)
#define S3C_DCTL_SftDiscon			(1 << 1)
#define S3C_DCTL_RmtWkUpSig			(1 << 0)

#define S3C_DSTS				S3C_HSOTG_REG(0x808)

#define S3C_DSTS_SOFFN_MASK			(0x3fff << 8)
#define S3C_DSTS_SOFFN_SHIFT			(8)
#define S3C_DSTS_SOFFN_LIMIT			(0x3fff)
#define S3C_DSTS_SOFFN(_x)			((_x) << 8)
#define S3C_DSTS_ErraticErr			(1 << 3)
#define S3C_DSTS_EnumSpd_MASK			(0x3 << 1)
#define S3C_DSTS_EnumSpd_SHIFT			(1)
#define S3C_DSTS_EnumSpd_HS			(0x0 << 1)
#define S3C_DSTS_EnumSpd_FS			(0x1 << 1)
#define S3C_DSTS_EnumSpd_LS			(0x2 << 1)
#define S3C_DSTS_EnumSpd_FS48			(0x3 << 1)

#define S3C_DSTS_SuspSts			(1 << 0)

#define S3C_DIEPMSK				S3C_HSOTG_REG(0x810)

#define S3C_DIEPMSK_TxFIFOEmpty			(1 << 7)
#define S3C_DIEPMSK_INEPNakEffMsk		(1 << 6)
#define S3C_DIEPMSK_INTknEPMisMsk		(1 << 5)
#define S3C_DIEPMSK_INTknTXFEmpMsk		(1 << 4)
#define S3C_DIEPMSK_TimeOUTMsk			(1 << 3)
#define S3C_DIEPMSK_AHBErrMsk			(1 << 2)
#define S3C_DIEPMSK_EPDisbldMsk			(1 << 1)
#define S3C_DIEPMSK_XferComplMsk		(1 << 0)

#define S3C_DOEPMSK				S3C_HSOTG_REG(0x814)

#define S3C_DOEPMSK_Back2BackSetup		(1 << 6)
#define S3C_DOEPMSK_OUTTknEPdisMsk		(1 << 4)
#define S3C_DOEPMSK_SetupMsk			(1 << 3)
#define S3C_DOEPMSK_AHBErrMsk			(1 << 2)
#define S3C_DOEPMSK_EPDisbldMsk			(1 << 1)
#define S3C_DOEPMSK_XferComplMsk		(1 << 0)

#define S3C_DAINT				S3C_HSOTG_REG(0x818)
#define S3C_DAINTMSK				S3C_HSOTG_REG(0x81C)

#define S3C_DAINT_OutEP_SHIFT			(16)
#define S3C_DAINT_OutEP(x)			(1 << ((x) + 16))
#define S3C_DAINT_InEP(x)			(1 << (x))

#define S3C_DTKNQR1				S3C_HSOTG_REG(0x820)
#define S3C_DTKNQR2				S3C_HSOTG_REG(0x824)
#define S3C_DTKNQR3				S3C_HSOTG_REG(0x830)
#define S3C_DTKNQR4				S3C_HSOTG_REG(0x834)

#define S3C_DVBUSDIS				S3C_HSOTG_REG(0x828)
#define S3C_DVBUSPULSE				S3C_HSOTG_REG(0x82C)

#define S3C_DIEPCTL0				S3C_HSOTG_REG(0x900)
#define S3C_DOEPCTL0				S3C_HSOTG_REG(0xB00)
#define S3C_DIEPCTL(_a)				S3C_HSOTG_REG(0x900 + ((_a) * 0x20))
#define S3C_DOEPCTL(_a)				S3C_HSOTG_REG(0xB00 + ((_a) * 0x20))

/* EP0 specialness:
 * bits[29..28] - reserved (no SetD0PID, SetD1PID)
 * bits[25..22] - should always be zero, this isn't a periodic endpoint
 * bits[10..0] - MPS setting differenct for EP0
*/
#define S3C_D0EPCTL_MPS_MASK			(0x3 << 0)
#define S3C_D0EPCTL_MPS_SHIFT			(0)
#define S3C_D0EPCTL_MPS_64			(0x0 << 0)
#define S3C_D0EPCTL_MPS_32			(0x1 << 0)
#define S3C_D0EPCTL_MPS_16			(0x2 << 0)
#define S3C_D0EPCTL_MPS_8			(0x3 << 0)

#define S3C_DxEPCTL_EPEna			(1 << 31)
#define S3C_DxEPCTL_EPDis			(1 << 30)
#define S3C_DxEPCTL_SetD1PID			(1 << 29)
#define S3C_DxEPCTL_SetOddFr			(1 << 29)
#define S3C_DxEPCTL_SetD0PID			(1 << 28)
#define S3C_DxEPCTL_SetEvenFr			(1 << 28)
#define S3C_DxEPCTL_SNAK			(1 << 27)
#define S3C_DxEPCTL_CNAK			(1 << 26)
#define S3C_DxEPCTL_TxFNum_MASK			(0xf << 22)
#define S3C_DxEPCTL_TxFNum_SHIFT		(22)
#define S3C_DxEPCTL_TxFNum_LIMIT		(0xf)
#define S3C_DxEPCTL_TxFNum(_x)			((_x) << 22)

#define S3C_DxEPCTL_Stall			(1 << 21)
#define S3C_DxEPCTL_Snp				(1 << 20)
#define S3C_DxEPCTL_EPType_MASK			(0x3 << 18)
#define S3C_DxEPCTL_EPType_SHIFT		(18)
#define S3C_DxEPCTL_EPType_Control		(0x0 << 18)
#define S3C_DxEPCTL_EPType_Iso			(0x1 << 18)
#define S3C_DxEPCTL_EPType_Bulk			(0x2 << 18)
#define S3C_DxEPCTL_EPType_Intterupt		(0x3 << 18)

#define S3C_DxEPCTL_NAKsts			(1 << 17)
#define S3C_DxEPCTL_DPID			(1 << 16)
#define S3C_DxEPCTL_EOFrNum			(1 << 16)
#define S3C_DxEPCTL_USBActEp			(1 << 15)
#define S3C_DxEPCTL_NextEp_MASK			(0xf << 11)
#define S3C_DxEPCTL_NextEp_SHIFT		(11)
#define S3C_DxEPCTL_NextEp_LIMIT		(0xf)
#define S3C_DxEPCTL_NextEp(_x)			((_x) << 11)

#define S3C_DxEPCTL_MPS_MASK			(0x7ff << 0)
#define S3C_DxEPCTL_MPS_SHIFT			(0)
#define S3C_DxEPCTL_MPS_LIMIT			(0x7ff)
#define S3C_DxEPCTL_MPS(_x)			((_x) << 0)

#define S3C_DIEPINT(_a)				S3C_HSOTG_REG(0x908 + ((_a) * 0x20))
#define S3C_DOEPINT(_a)				S3C_HSOTG_REG(0xB08 + ((_a) * 0x20))

#define S3C_DxEPINT_INEPNakEff			(1 << 6)
#define S3C_DxEPINT_Back2BackSetup		(1 << 6)
#define S3C_DxEPINT_INTknEPMis			(1 << 5)
#define S3C_DxEPINT_INTknTXFEmp			(1 << 4)
#define S3C_DxEPINT_OUTTknEPdis			(1 << 4)
#define S3C_DxEPINT_Timeout			(1 << 3)
#define S3C_DxEPINT_Setup			(1 << 3)
#define S3C_DxEPINT_AHBErr			(1 << 2)
#define S3C_DxEPINT_EPDisbld			(1 << 1)
#define S3C_DxEPINT_XferCompl			(1 << 0)

#define S3C_DIEPTSIZ0				S3C_HSOTG_REG(0x910)

#define S3C_DIEPTSIZ0_PktCnt_MASK		(0x3 << 19)
#define S3C_DIEPTSIZ0_PktCnt_SHIFT		(19)
#define S3C_DIEPTSIZ0_PktCnt_LIMIT		(0x3)
#define S3C_DIEPTSIZ0_PktCnt(_x)		((_x) << 19)

#define S3C_DIEPTSIZ0_XferSize_MASK		(0x7f << 0)
#define S3C_DIEPTSIZ0_XferSize_SHIFT		(0)
#define S3C_DIEPTSIZ0_XferSize_LIMIT		(0x7f)
#define S3C_DIEPTSIZ0_XferSize(_x)		((_x) << 0)


#define DOEPTSIZ0				S3C_HSOTG_REG(0xB10)
#define S3C_DOEPTSIZ0_SUPCnt_MASK		(0x3 << 29)
#define S3C_DOEPTSIZ0_SUPCnt_SHIFT		(29)
#define S3C_DOEPTSIZ0_SUPCnt_LIMIT		(0x3)
#define S3C_DOEPTSIZ0_SUPCnt(_x)		((_x) << 29)

#define S3C_DOEPTSIZ0_PktCnt			(1 << 19)
#define S3C_DOEPTSIZ0_XferSize_MASK		(0x7f << 0)
#define S3C_DOEPTSIZ0_XferSize_SHIFT		(0)

#define S3C_DIEPTSIZ(_a)			S3C_HSOTG_REG(0x910 + ((_a) * 0x20))
#define S3C_DOEPTSIZ(_a)			S3C_HSOTG_REG(0xB10 + ((_a) * 0x20))

#define S3C_DxEPTSIZ_MC_MASK			(0x3 << 29)
#define S3C_DxEPTSIZ_MC_SHIFT			(29)
#define S3C_DxEPTSIZ_MC_LIMIT			(0x3)
#define S3C_DxEPTSIZ_MC(_x)			((_x) << 29)

#define S3C_DxEPTSIZ_PktCnt_MASK		(0x3ff << 19)
#define S3C_DxEPTSIZ_PktCnt_SHIFT		(19)
#define S3C_DxEPTSIZ_PktCnt_GET(_v)		(((_v) >> 19) & 0x3ff)
#define S3C_DxEPTSIZ_PktCnt_LIMIT		(0x3ff)
#define S3C_DxEPTSIZ_PktCnt(_x)			((_x) << 19)

#define S3C_DxEPTSIZ_XferSize_MASK		(0x7ffff << 0)
#define S3C_DxEPTSIZ_XferSize_SHIFT		(0)
#define S3C_DxEPTSIZ_XferSize_GET(_v)		(((_v) >> 0) & 0x7ffff)
#define S3C_DxEPTSIZ_XferSize_LIMIT		(0x7ffff)
#define S3C_DxEPTSIZ_XferSize(_x)		((_x) << 0)


#define S3C_DIEPDMA(_a)				S3C_HSOTG_REG(0x914 + ((_a) * 0x20))
#define S3C_DOEPDMA(_a)				S3C_HSOTG_REG(0xB14 + ((_a) * 0x20))
#define S3C_DTXFSTS(_a)				S3C_HSOTG_REG(0x918 + ((_a) * 0x20))

#define S3C_EPFIFO(_a)				S3C_HSOTG_REG(0x1000 + ((_a) * 0x1000))

#endif /* __PLAT_S3C64XX_REGS_USB_HSOTG_H */
