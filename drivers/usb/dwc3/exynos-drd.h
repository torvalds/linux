/* drivers/usb/dwc3/exynos-drd.h
 *
 * Copyright (c) 2012 Samsung Electronics Co. Ltd
 * Author: Anton Tikhomirov <av.tikhomirov@samsung.com>
 *
 * Exynos SuperSpeed USB 3.0 DRD Controller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __DRIVERS_USB_DWC3_EXYNOS_DRD_H
#define __DRIVERS_USB_DWC3_EXYNOS_DRD_H

/* Global registers */
#define EXYNOS_USB3_GSBUSCFG0		0xC100
#define EXYNOS_USB3_GSBUSCFG0_SBusStoreAndForward	(1 << 12)
#define EXYNOS_USB3_GSBUSCFG0_DatBigEnd			(1 << 11)
#define EXYNOS_USB3_GSBUSCFG0_INCR256BrstEna		(1 << 7)
#define EXYNOS_USB3_GSBUSCFG0_INCR128BrstEna		(1 << 6)
#define EXYNOS_USB3_GSBUSCFG0_INCR64BrstEna		(1 << 5)
#define EXYNOS_USB3_GSBUSCFG0_INCR32BrstEna		(1 << 4)
#define EXYNOS_USB3_GSBUSCFG0_INCR16BrstEna		(1 << 3)
#define EXYNOS_USB3_GSBUSCFG0_INCR8BrstEna		(1 << 2)
#define EXYNOS_USB3_GSBUSCFG0_INCR4BrstEna		(1 << 1)
#define EXYNOS_USB3_GSBUSCFG0_INCRBrstEna		(1 << 0)

#define EXYNOS_USB3_GSBUSCFG1		0xC104
#define EXYNOS_USB3_GSBUSCFG1_EN1KPAGE			(1 << 12)
#define EXYNOS_USB3_GSBUSCFG1_BREQLIMIT_MASK		(0xf << 8)
#define EXYNOS_USB3_GSBUSCFG1_BREQLIMIT_SHIFT		8
#define EXYNOS_USB3_GSBUSCFG1_BREQLIMIT(_x)		((_x) << 8)

#define EXYNOS_USB3_GTXTHRCFG		0xC108
#define EXYNOS_USB3_GTXTHRCFG_USBTxPktCntSel		(1 << 29)
#define EXYNOS_USB3_GTXTHRCFG_USBTxPktCnt_MASK		(0xf << 24)
#define EXYNOS_USB3_GTXTHRCFG_USBTxPktCnt_SHIFT		24
#define EXYNOS_USB3_GTXTHRCFG_USBTxPktCnt(_x)		((_x) << 24)
#define EXYNOS_USB3_GTXTHRCFG_USBMaxTxBurstSize_MASK	(0xff << 16)
#define EXYNOS_USB3_GTXTHRCFG_USBMaxTxBurstSize_SHIFT	16
#define EXYNOS_USB3_GTXTHRCFG_USBMaxTxBurstSize(_x)	((_x) << 16)

#define EXYNOS_USB3_GRXTHRCFG		0xC10C
#define EXYNOS_USB3_GRXTHRCFG_USBRxPktCntSel		(1 << 29)
#define EXYNOS_USB3_GRXTHRCFG_USBRxPktCnt_MASK		(0xf << 24)
#define EXYNOS_USB3_GRXTHRCFG_USBRxPktCnt_SHIFT		24
#define EXYNOS_USB3_GRXTHRCFG_USBRxPktCnt(_x)		((_x) << 24)
#define EXYNOS_USB3_GRXTHRCFG_USBMaxRxBurstSize_MASK	(0x1f << 19)
#define EXYNOS_USB3_GRXTHRCFG_USBMaxRxBurstSize_SHIFT	19
#define EXYNOS_USB3_GRXTHRCFG_USBMaxRxBurstSize(_x)	((_x) << 19)

#define EXYNOS_USB3_GCTL		0xC110
#define EXYNOS_USB3_GCTL_PwrDnScale_MASK		(0x1fff << 19)
#define EXYNOS_USB3_GCTL_PwrDnScale_SHIFT		19
#define EXYNOS_USB3_GCTL_PwrDnScale(_x)			((_x) << 19)
#define EXYNOS_USB3_GCTL_U2RSTECN			(1 << 16)
#define EXYNOS_USB3_GCTL_FRMSCLDWN_MASK			(0x3 << 14)
#define EXYNOS_USB3_GCTL_FRMSCLDWN_SHIFT		14
#define EXYNOS_USB3_GCTL_FRMSCLDWN(_x)			((_x) << 14)
#define EXYNOS_USB3_GCTL_PrtCapDir_MASK			(0x3 << 12)
#define EXYNOS_USB3_GCTL_PrtCapDir_SHIFT		12
#define EXYNOS_USB3_GCTL_PrtCapDir(_x)			((_x) << 12)
#define EXYNOS_USB3_GCTL_CoreSoftReset			(1 << 11)
#define EXYNOS_USB3_GCTL_LocalLpBkEn			(1 << 10)
#define EXYNOS_USB3_GCTL_LpbkEn				(1 << 9)
#define EXYNOS_USB3_GCTL_DebugAttach			(1 << 8)
#define EXYNOS_USB3_GCTL_RAMClkSel_MASK			(0x3 << 6)
#define EXYNOS_USB3_GCTL_RAMClkSel_SHIFT		6
#define EXYNOS_USB3_GCTL_RAMClkSel(_x)			((_x) << 6)
#define EXYNOS_USB3_GCTL_ScaleDown_MASK			(0x3 << 4)
#define EXYNOS_USB3_GCTL_ScaleDown_SHIFT		4
#define EXYNOS_USB3_GCTL_ScaleDown(_x)			((_x) << 4)
#define EXYNOS_USB3_GCTL_DisScramble			(1 << 3)
#define EXYNOS_USB3_GCTL_SsPwrClmp			(1 << 2)
#define EXYNOS_USB3_GCTL_HsFsLsPwrClmp			(1 << 1)
#define EXYNOS_USB3_GCTL_DsblClkGtng			(1 << 0)

#define EXYNOS_USB3_GEVTEN		0xC114
#define EXYNOS_USB3_GEVTEN_I2CEvtEn			(1 << 1)
#define EXYNOS_USB3_GEVTEN_ULPICKEvtEn			(1 << 0)
#define EXYNOS_USB3_GEVTEN_I2CCKEvtEn			(1 << 0)

#define EXYNOS_USB3_GSTS		0xC118
#define EXYNOS_USB3_GSTS_CBELT_MASK			(0xfff << 20)
#define EXYNOS_USB3_GSTS_CBELT_SHIFT			20
#define EXYNOS_USB3_GSTS_CBELT(_x)			((_x) << 20)
#define EXYNOS_USB3_GSTS_OTG_IP				(1 << 10)
#define EXYNOS_USB3_GSTS_BC_IP				(1 << 9)
#define EXYNOS_USB3_GSTS_ADP_IP				(1 << 8)
#define EXYNOS_USB3_GSTS_Host_IP			(1 << 7)
#define EXYNOS_USB3_GSTS_Device_IP			(1 << 6)
#define EXYNOS_USB3_GSTS_CSRTimeout			(1 << 5)
#define EXYNOS_USB3_GSTS_BusErrAddrVld			(1 << 4)
#define EXYNOS_USB3_GSTS_CurMod_MASK			(0x3 << 0)
#define EXYNOS_USB3_GSTS_CurMod_SHIFT			0
#define EXYNOS_USB3_GSTS_CurMod(_x)			((_x) << 0)

#define EXYNOS_USB3_GSNPSID		0xC120

#define EXYNOS_USB3_GGPIO		0xC124
#define EXYNOS_USB3_GGPIO_GPO_MASK			(0xffff << 16)
#define EXYNOS_USB3_GGPIO_GPO_SHIFT			16
#define EXYNOS_USB3_GGPIO_GPO(_x)			((_x) << 16)
#define EXYNOS_USB3_GGPIO_GPI_MASK			(0xffff << 0)
#define EXYNOS_USB3_GGPIO_GPI_SHIFT			0
#define EXYNOS_USB3_GGPIO_GPI(_x)			((x) << 0)

#define EXYNOS_USB3_GUID		0xC128

#define EXYNOS_USB3_GUCTL		0xC12C
#define EXYNOS_USB3_GUCTL_SprsCtrlTransEn		(1 << 17)
#define EXYNOS_USB3_GUCTL_ResBwHSEPS			(1 << 16)
#define EXYNOS_USB3_GUCTL_CMdevAddr			(1 << 15)
#define EXYNOS_USB3_GUCTL_USBHstInAutoRetryEn		(1 << 14)
#define EXYNOS_USB3_GUCTL_USBHstInMaxBurst_MASK		(0x7 << 11)
#define EXYNOS_USB3_GUCTL_USBHstInMaxBurst_SHIFT	11
#define EXYNOS_USB3_GUCTL_USBHstInMaxBurst(_x)		((_x) << 11)
#define EXYNOS_USB3_GUCTL_DTCT_MASK			(0x3 << 9)
#define EXYNOS_USB3_GUCTL_DTCT_SHIFT			9
#define EXYNOS_USB3_GUCTL_DTCT(_x)			((_x) << 9)
#define EXYNOS_USB3_GUCTL_DTFT_MASK			(0x1ff << 0)
#define EXYNOS_USB3_GUCTL_DTFT_SHIFT			0
#define EXYNOS_USB3_GUCTL_DTFT(_x)			((_x) << 0)

#define EXYNOS_USB3_GBUSERRADDR_31_0	0xC130
#define EXYNOS_USB3_GBUSERRADDR_63_32	0xC134
#define EXYNOS_USB3_GPRTBIMAP_31_0	0xC138
#define EXYNOS_USB3_GPRTBIMAP_63_32	0xC13C

#define EXYNOS_USB3_GHWPARAMS0		0xC140
#define EXYNOS_USB3_GHWPARAMS1		0xC144
#define EXYNOS_USB3_GHWPARAMS2		0xC148
#define EXYNOS_USB3_GHWPARAMS3		0xC14C
#define EXYNOS_USB3_GHWPARAMS4		0xC150
#define EXYNOS_USB3_GHWPARAMS5		0xC154
#define EXYNOS_USB3_GHWPARAMS6		0xC158
#define EXYNOS_USB3_GHWPARAMS6_SRPSupport		(1 << 10)
#define EXYNOS_USB3_GHWPARAMS7		0xC15C

#define EXYNOS_USB3_GDBGFIFOSPACE	0xC160
#define EXYNOS_USB3_GDBGLTSSM		0xC164

#define EXYNOS_USB3_GDBGLSPMUX		0xC170
#define EXYNOS_USB3_GDBGLSP		0xC174
#define EXYNOS_USB3_GDBGEPINFO0		0xC178
#define EXYNOS_USB3_GDBGEPINFO1		0xC17C

#define EXYNOS_USB3_GPRTBIMAP_HS_31_0	0xC180
#define EXYNOS_USB3_GPRTBIMAP_HS_63_32	0xC184
#define EXYNOS_USB3_GPRTBIMAP_FS_31_0	0xC188
#define EXYNOS_USB3_GPRTBIMAP_FS_63_32	0xC18C

#define EXYNOS_USB3_GUSB2PHYCFG(_a)	(0xC200 + ((_a) * 0x04))
#define EXYNOS_USB3_GUSB2PHYCFGx_PHYSoftRst		(1 << 31)
#define EXYNOS_USB3_GUSB2PHYCFGx_PhyIntrNum_MASK	(0x3f << 19)
#define EXYNOS_USB3_GUSB2PHYCFGx_PhyIntrNum_SHIFT	19
#define EXYNOS_USB3_GUSB2PHYCFGx_PhyIntrNum(_x)		((_x) << 19)
#define EXYNOS_USB3_GUSB2PHYCFGx_ULPIExtVbusIndicator	(1 << 18)
#define EXYNOS_USB3_GUSB2PHYCFGx_ULPIExtVbusDrv		(1 << 17)
#define EXYNOS_USB3_GUSB2PHYCFGx_ULPIClkSusM		(1 << 16)
#define EXYNOS_USB3_GUSB2PHYCFGx_ULPIAutoRes		(1 << 15)
#define EXYNOS_USB3_GUSB2PHYCFGx_PhyLPwrClkSel		(1 << 14)
#define EXYNOS_USB3_GUSB2PHYCFGx_USBTrdTim_MASK		(0xf << 10)
#define EXYNOS_USB3_GUSB2PHYCFGx_USBTrdTim_SHIFT	10
#define EXYNOS_USB3_GUSB2PHYCFGx_USBTrdTim(_x)		((_x) << 10)
#define EXYNOS_USB3_GUSB2PHYCFGx_EnblSlpM		(1 << 8)
#define EXYNOS_USB3_GUSB2PHYCFGx_PHYSel			(1 << 7)
#define EXYNOS_USB3_GUSB2PHYCFGx_SusPHY			(1 << 6)
#define EXYNOS_USB3_GUSB2PHYCFGx_FSIntf			(1 << 5)
#define EXYNOS_USB3_GUSB2PHYCFGx_ULPI_UTMI_Sel		(1 << 4)
#define EXYNOS_USB3_GUSB2PHYCFGx_PHYIf			(1 << 3)
#define EXYNOS_USB3_GUSB2PHYCFGx_TOutCal_MASK		(0x7 << 0)
#define EXYNOS_USB3_GUSB2PHYCFGx_TOutCal_SHIFT		0
#define EXYNOS_USB3_GUSB2PHYCFGx_TOutCal(_x)		((_x) << 0)

#define EXYNOS_USB3_GUSB2I2CCTL(_a)	(0xC240 + ((_a) * 0x04))

#define EXYNOS_USB3_GUSB2PHYACC(_a)	(0xC280 + ((_a) * 0x04))
#define EXYNOS_USB3_GUSB2PHYACCx_DisUlpiDrvr		(1 << 26)
#define EXYNOS_USB3_GUSB2PHYACCx_NewRegReq		(1 << 25)
#define EXYNOS_USB3_GUSB2PHYACCx_VStsDone		(1 << 24)
#define EXYNOS_USB3_GUSB2PHYACCx_VStsBsy		(1 << 23)
#define EXYNOS_USB3_GUSB2PHYACCx_RegWr			(1 << 22)
#define EXYNOS_USB3_GUSB2PHYACCx_RegAddr_MASK		(0x3f << 16)
#define EXYNOS_USB3_GUSB2PHYACCx_RegAddr_SHIFT		16
#define EXYNOS_USB3_GUSB2PHYACCx_RegAddr(_x)		((_x) << 16)
/* Next 2 fields are overlaping. Is it error in user manual? */
#define EXYNOS_USB3_GUSB2PHYACCx_VCtrl_MASK		(0xff << 8)
#define EXYNOS_USB3_GUSB2PHYACCx_VCtrl_SHIFT		8
#define EXYNOS_USB3_GUSB2PHYACCx_VCtrl(_x)		((_x) << 8)
/*--------*/
#define EXYNOS_USB3_GUSB2PHYACCx_ExtRegAddr_MASK	(0x3f << 8)
#define EXYNOS_USB3_GUSB2PHYACCx_ExtRegAddr_SHIFT	8
#define EXYNOS_USB3_GUSB2PHYACCx_ExtRegAddr(_x)		((_x) << 8)
/*--------*/
#define EXYNOS_USB3_GUSB2PHYACCx_RegData_MASK		(0xff << 0)
#define EXYNOS_USB3_GUSB2PHYACCx_RegData_SHIFT		0
#define EXYNOS_USB3_GUSB2PHYACCx_RegData(_x)		((_x) << 0)

#define EXYNOS_USB3_GUSB3PIPECTL(_a)	(0xC2C0 + ((_a) * 0x04))
#define EXYNOS_USB3_GUSB3PIPECTLx_PHYSoftRst		(1 << 31)
#define EXYNOS_USB3_GUSB3PIPECTLx_request_p1p2p3	(1 << 24)
#define EXYNOS_USB3_GUSB3PIPECTLx_StartRxdetU3RxDet	(1 << 23)
#define EXYNOS_USB3_GUSB3PIPECTLx_DisRxDetU3RxDet	(1 << 22)
#define EXYNOS_USB3_GUSB3PIPECTLx_delay_p1p2p3_MASK	(0x7 << 19)
#define EXYNOS_USB3_GUSB3PIPECTLx_delay_p1p2p3_SHIFT	19
#define EXYNOS_USB3_GUSB3PIPECTLx_delay_p1p2p3(_x)	((_x) << 19)
#define EXYNOS_USB3_GUSB3PIPECTLx_delay_phy_pwr_p1p2p3	(1 << 18)
#define EXYNOS_USB3_GUSB3PIPECTLx_SuspSSPhy		(1 << 17)
#define EXYNOS_USB3_GUSB3PIPECTLx_DatWidth_MASK		(0x3 << 15)
#define EXYNOS_USB3_GUSB3PIPECTLx_DatWidth_SHIFT	15
#define EXYNOS_USB3_GUSB3PIPECTLx_DatWidth(_x)		((_x) << 15)
#define EXYNOS_USB3_GUSB3PIPECTLx_AbortRxDetInU2	(1 << 14)
#define EXYNOS_USB3_GUSB3PIPECTLx_SkipRxDet		(1 << 13)
#define EXYNOS_USB3_GUSB3PIPECTLx_LFPSP0Algn		(1 << 12)
#define EXYNOS_USB3_GUSB3PIPECTLx_P3P2TranOK		(1 << 11)
#define EXYNOS_USB3_GUSB3PIPECTLx_LFPSFilt		(1 << 9)
#define EXYNOS_USB3_GUSB3PIPECTLx_TxSwing		(1 << 6)
#define EXYNOS_USB3_GUSB3PIPECTLx_TxMargin_MASK		(0x7 << 3)
#define EXYNOS_USB3_GUSB3PIPECTLx_TxMargin_SHIFT	3
#define EXYNOS_USB3_GUSB3PIPECTLx_TxMargin(_x)		((_x) << 3)
#define EXYNOS_USB3_GUSB3PIPECTLx_TxDeemphasis_MASK	(0x3 << 1)
#define EXYNOS_USB3_GUSB3PIPECTLx_TxDeemphasis_SHIFT	1
#define EXYNOS_USB3_GUSB3PIPECTLx_TxDeemphasis(_x)	((_x) << 1)
#define EXYNOS_USB3_GUSB3PIPECTLx_ElasticBufferMode	(1 << 0)

#define EXYNOS_USB3_GTXFIFOSIZ(_a)	(0xC300 + ((_a) * 0x04))
#define EXYNOS_USB3_GTXFIFOSIZx_TxFStAddr_n_MASK	(0xffff << 16)
#define EXYNOS_USB3_GTXFIFOSIZx_TxFStAddr_n_SHIFT	16
#define EXYNOS_USB3_GTXFIFOSIZx_TxFStAddr_n(_x)		((_x) << 16)
#define EXYNOS_USB3_GTXFIFOSIZx_TxFDep_n_MASK		(0xffff << 0)
#define EXYNOS_USB3_GTXFIFOSIZx_TxFDep_n_SHIFT		0
#define EXYNOS_USB3_GTXFIFOSIZx_TxFDep_n(_x)		((_x) << 0)

#define EXYNOS_USB3_GRXFIFOSIZ(_a)	(0xC380 + ((_a) * 0x04))
#define EXYNOS_USB3_GRXFIFOSIZx_RxFStAddr_n_MASK	(0xffff << 16)
#define EXYNOS_USB3_GRXFIFOSIZx_RxFStAddr_n_SHIFT	16
#define EXYNOS_USB3_GRXFIFOSIZx_RxFStAddr_n(_x)		((_x) << 16)
#define EXYNOS_USB3_GRXFIFOSIZx_RxFDep_n_MASK		(0xffff << 0)
#define EXYNOS_USB3_GRXFIFOSIZx_RxFDep_n_SHIFT		0
#define EXYNOS_USB3_GRXFIFOSIZx_RxFDep_n(_x)		((_x) << 0)

#define EXYNOS_USB3_GEVNTADR_31_0(_a)	(0xC400 + ((_a) * 0x10))
#define EXYNOS_USB3_GEVNTADR_63_32(_a)	(0xC404 + ((_a) * 0x10))

#define EXYNOS_USB3_GEVNTSIZ(_a)	(0xC408 + ((_a) * 0x10))
#define EXYNOS_USB3_GEVNTSIZx_EvntIntMask		(1 << 31)
#define EXYNOS_USB3_GEVNTSIZx_EVNTSiz_MASK		(0xffff << 0)
#define EXYNOS_USB3_GEVNTSIZx_EVNTSiz_SHIFT		0
#define EXYNOS_USB3_GEVNTSIZx_EVNTSiz(x)		((_x) << 0)

#define EXYNOS_USB3_GEVNTCOUNT(_a)	(0xC40C + ((_a) * 0x10))
#define EXYNOS_USB3_GEVNTCOUNTx_EVNTCount_MASK		(0xffff << 0)
#define EXYNOS_USB3_GEVNTCOUNTx_EVNTCount_SHIFT		0
#define EXYNOS_USB3_GEVNTCOUNTx_EVNTCount(_x)		((_x) << 0)

#define EXYNOS_USB3_GHWPARAMS8		0xC600

/* USB 2.0 OTG and Battery Charger registers */
#define EXYNOS_USB3_OCFG		0xCC00
#define EXYNOS_USB3_OCFG_OTG_Version			(1 << 2)
#define EXYNOS_USB3_OCFG_HNPCap				(1 << 1)
#define EXYNOS_USB3_OCFG_SRPCap				(1 << 0)

#define EXYNOS_USB3_OCTL		0xCC04
#define EXYNOS_USB3_OCTL_PeriMode			(1 << 6)
#define EXYNOS_USB3_OCTL_PrtPwrCtl			(1 << 5)
#define EXYNOS_USB3_OCTL_HNPReq				(1 << 4)
#define EXYNOS_USB3_OCTL_SesReq				(1 << 3)
#define EXYNOS_USB3_OCTL_TermSelDLPulse			(1 << 2)
#define EXYNOS_USB3_OCTL_DevSetHNPEn			(1 << 1)
#define EXYNOS_USB3_OCTL_HstSetHNPEn			(1 << 0)

#define EXYNOS_USB3_OEVT		0xCC08
#define EXYNOS_USB3_OEVT_DeviceMode			(1 << 31)
#define EXYNOS_USB3_OEVT_OTGConIDStsChngEvnt		(1 << 24)
#define EXYNOS_USB3_OEVT_OTGADevBHostEndEvnt		(1 << 20)
#define EXYNOS_USB3_OEVT_OTGADevHostEvnt		(1 << 19)
#define EXYNOS_USB3_OEVT_OTGADevHNPChngEvnt		(1 << 18)
#define EXYNOS_USB3_OEVT_OTGADevSRPDetEvnt		(1 << 17)
#define EXYNOS_USB3_OEVT_OTGADevSessEndDetEvnt		(1 << 16)
#define EXYNOS_USB3_OEVT_OTGBDevBHostEndEvnt		(1 << 11)
#define EXYNOS_USB3_OEVT_OTGBDevHNPChngEvnt		(1 << 10)
#define EXYNOS_USB3_OEVT_OTGBDevSessVldDetEvnt		(1 << 9)
#define EXYNOS_USB3_OEVT_OTGBDevVBUSChngEvnt		(1 << 8)
#define EXYNOS_USB3_OEVT_BSesVld			(1 << 3)
#define EXYNOS_USB3_OEVT_HstNegSts			(1 << 2)
#define EXYNOS_USB3_OEVT_SesReqSts			(1 << 1)
#define EXYNOS_USB3_OEVT_OEVTError			(1 << 0)
#define EXYNOS_USB3_OEVT_CLEAR_ALL			0x1FFFFFF

#define EXYNOS_USB3_OEVTEN		0xCC0C
#define EXYNOS_USB3_OEVTEN_OTGConIDStsChngEvntEn	(1 << 24)
#define EXYNOS_USB3_OEVTEN_OTGADevBHostEndEvntEn	(1 << 20)
#define EXYNOS_USB3_OEVTEN_OTGADevHostEvntEn		(1 << 19)
#define EXYNOS_USB3_OEVTEN_OTGADevHNPChngEvntEn		(1 << 18)
#define EXYNOS_USB3_OEVTEN_OTGADevSRPDetEvntEn		(1 << 17)
#define EXYNOS_USB3_OEVTEN_OTGADevSessEndDetEvntEn	(1 << 16)
#define EXYNOS_USB3_OEVTEN_OTGBDevBHostEndEvntEn	(1 << 11)
#define EXYNOS_USB3_OEVTEN_OTGBDevHNPChngEvntEn		(1 << 10)
#define EXYNOS_USB3_OEVTEN_OTGBDevSessVldDetEvntEn	(1 << 9)
#define EXYNOS_USB3_OEVTEN_OTGBDevVBUSChngEvntEn	(1 << 8)

#define EXYNOS_USB3_OSTS		0xCC10
#define EXYNOS_USB3_OSTS_OTG_state_MASK			(0xf << 8)
#define EXYNOS_USB3_OSTS_OTG_state_SHIFT		8
#define EXYNOS_USB3_OSTS_OTG_state(_x)			((_x) << 8)
#define EXYNOS_USB3_OSTS_PeripheralState		(1 << 4)
#define EXYNOS_USB3_OSTS_xHCIPrtPower			(1 << 3)
#define EXYNOS_USB3_OSTS_BSesVld			(1 << 2)
#define EXYNOS_USB3_OSTS_VbusVld			(1 << 1)
#define EXYNOS_USB3_OSTS_ConIDSts			(1 << 0)

#define EXYNOS_USB3_ADPCFG		0xCC20
#define EXYNOS_USB3_ADPCFG_PrbPer_MASK			(0x3 << 30)
#define EXYNOS_USB3_ADPCFG_PrbPer_SHIFT			30
#define EXYNOS_USB3_ADPCFG_PrbPer(_x)			((_x) << 30)
#define EXYNOS_USB3_ADPCFG_PrbDelta_MASK		(0x3 << 28)
#define EXYNOS_USB3_ADPCFG_PrbDelta_SHIFT		28
#define EXYNOS_USB3_ADPCFG_PrbDelta(_x)			((_x) << 28)
#define EXYNOS_USB3_ADPCFG_PrbDschg_MASK		(0x3 << 26)
#define EXYNOS_USB3_ADPCFG_PrbDschg_SHIFT		26
#define EXYNOS_USB3_ADPCFG_PrbDschg(_x)			((_x) << 26)

#define EXYNOS_USB3_ADPCTL		0xCC24
#define EXYNOS_USB3_ADPCTL_EnaPrb			(1 << 28)
#define EXYNOS_USB3_ADPCTL_EnaSns			(1 << 27)
#define EXYNOS_USB3_ADPCTL_ADPEn			(1 << 26)
#define EXYNOS_USB3_ADPCTL_ADPRes			(1 << 25)
#define EXYNOS_USB3_ADPCTL_WB				(1 << 24)

#define EXYNOS_USB3_ADPEVT		0xCC28
#define EXYNOS_USB3_ADPEVT_AdpPrbEvnt			(1 << 28)
#define EXYNOS_USB3_ADPEVT_AdpSnsEvnt			(1 << 27)
#define EXYNOS_USB3_ADPEVT_AdpTmoutEvnt			(1 << 26)
#define EXYNOS_USB3_ADPEVT_ADPRstCmpltEvnt		(1 << 25)
#define EXYNOS_USB3_ADPEVT_RTIM_MASK			(0x7ff << 0)
#define EXYNOS_USB3_ADPEVT_RTIM_SHIFT			0
#define EXYNOS_USB3_ADPEVT_RTIM(_x)			((_x) << 0)

#define EXYNOS_USB3_ADPEVTEN		0xCC2C
#define EXYNOS_USB3_ADPEVTEN_AdpPrbEvntEn		(1 << 28)
#define EXYNOS_USB3_ADPEVTEN_AdpSnsEvntEn		(1 << 27)
#define EXYNOS_USB3_ADPEVTEN_AdpTmoutEvntEn		(1 << 26)
#define EXYNOS_USB3_ADPEVTEN_ADPRstCmpltEvntEn		(1 << 25)

#define EXYNOS_USB3_BCFG		0xCC30
#define EXYNOS_USB3_BCFG_IDDIG_SEL			(1 << 1)
#define EXYNOS_USB3_BCFG_CHIRP_EN			(1 << 0)

#define EXYNOS_USB3_BCEVT		0xCC38
#define EXYNOS_USB3_BCEVT_MV_ChngEvnt			(1 << 24)
#define EXYNOS_USB3_BCEVT_MultValIdBc_MASK		(0x1f << 0)
#define EXYNOS_USB3_BCEVT_MultValIdBc_SHIFT		0
#define EXYNOS_USB3_BCEVT_MultValIdBc(_x)		((_x) << 0)

#define EXYNOS_USB3_BCEVTEN		0xCC3C
#define EXYNOS_USB3_BCEVTEN_MV_ChngEvntEn		(1 << 24)

struct exynos_drd {
	struct exynos_drd_core	core;
	struct platform_device	*udc;
	struct platform_device	*xhci;
	struct platform_device	*active_child;
	struct device		*dev;
	struct dwc3_exynos_data	*pdata;

	spinlock_t		lock;

	struct clk		*clk;
	struct resource		*res;
	int			irq;

	struct resource		glob_res;
	void __iomem            *regs;
};

#endif /* __DRIVERS_USB_DWC3_EXYNOS_DRD_H */
