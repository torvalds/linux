// SPDX-License-Identifier: GPL-2.0+
/* Renesas R-Car CAN FD device driver
 *
 * Copyright (C) 2015 Renesas Electronics Corp.
 */

/* The R-Car CAN FD controller can operate in either one of the below two modes
 *  - CAN FD only mode
 *  - Classical CAN (CAN 2.0) only mode
 *
 * This driver puts the controller in CAN FD only mode by default. In this
 * mode, the controller acts as a CAN FD node that can also interoperate with
 * CAN 2.0 nodes.
 *
 * To switch the controller to Classical CAN (CAN 2.0) only mode, add
 * "renesas,no-can-fd" optional property to the device tree node. A h/w reset is
 * also required to switch modes.
 *
 * Note: The h/w manual register naming convention is clumsy and not acceptable
 * to use as it is in the driver. However, those names are added as comments
 * wherever it is modified to a readable name.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/can/led.h>
#include <linux/can/dev.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/iopoll.h>

#define RCANFD_DRV_NAME			"rcar_canfd"

/* Global register bits */

/* RSCFDnCFDGRMCFG */
#define RCANFD_GRMCFG_RCMC		BIT(0)

/* RSCFDnCFDGCFG / RSCFDnGCFG */
#define RCANFD_GCFG_EEFE		BIT(6)
#define RCANFD_GCFG_CMPOC		BIT(5)	/* CAN FD only */
#define RCANFD_GCFG_DCS			BIT(4)
#define RCANFD_GCFG_DCE			BIT(1)
#define RCANFD_GCFG_TPRI		BIT(0)

/* RSCFDnCFDGCTR / RSCFDnGCTR */
#define RCANFD_GCTR_TSRST		BIT(16)
#define RCANFD_GCTR_CFMPOFIE		BIT(11)	/* CAN FD only */
#define RCANFD_GCTR_THLEIE		BIT(10)
#define RCANFD_GCTR_MEIE		BIT(9)
#define RCANFD_GCTR_DEIE		BIT(8)
#define RCANFD_GCTR_GSLPR		BIT(2)
#define RCANFD_GCTR_GMDC_MASK		(0x3)
#define RCANFD_GCTR_GMDC_GOPM		(0x0)
#define RCANFD_GCTR_GMDC_GRESET		(0x1)
#define RCANFD_GCTR_GMDC_GTEST		(0x2)

/* RSCFDnCFDGSTS / RSCFDnGSTS */
#define RCANFD_GSTS_GRAMINIT		BIT(3)
#define RCANFD_GSTS_GSLPSTS		BIT(2)
#define RCANFD_GSTS_GHLTSTS		BIT(1)
#define RCANFD_GSTS_GRSTSTS		BIT(0)
/* Non-operational status */
#define RCANFD_GSTS_GNOPM		(BIT(0) | BIT(1) | BIT(2) | BIT(3))

/* RSCFDnCFDGERFL / RSCFDnGERFL */
#define RCANFD_GERFL_EEF1		BIT(17)
#define RCANFD_GERFL_EEF0		BIT(16)
#define RCANFD_GERFL_CMPOF		BIT(3)	/* CAN FD only */
#define RCANFD_GERFL_THLES		BIT(2)
#define RCANFD_GERFL_MES		BIT(1)
#define RCANFD_GERFL_DEF		BIT(0)

#define RCANFD_GERFL_ERR(gpriv, x)	((x) & (RCANFD_GERFL_EEF1 |\
					RCANFD_GERFL_EEF0 | RCANFD_GERFL_MES |\
					(gpriv->fdmode ?\
					 RCANFD_GERFL_CMPOF : 0)))

/* AFL Rx rules registers */

/* RSCFDnCFDGAFLCFG0 / RSCFDnGAFLCFG0 */
#define RCANFD_GAFLCFG_SETRNC(n, x)	(((x) & 0xff) << (24 - n * 8))
#define RCANFD_GAFLCFG_GETRNC(n, x)	(((x) >> (24 - n * 8)) & 0xff)

/* RSCFDnCFDGAFLECTR / RSCFDnGAFLECTR */
#define RCANFD_GAFLECTR_AFLDAE		BIT(8)
#define RCANFD_GAFLECTR_AFLPN(x)	((x) & 0x1f)

/* RSCFDnCFDGAFLIDj / RSCFDnGAFLIDj */
#define RCANFD_GAFLID_GAFLLB		BIT(29)

/* RSCFDnCFDGAFLP1_j / RSCFDnGAFLP1_j */
#define RCANFD_GAFLP1_GAFLFDP(x)	(1 << (x))

/* Channel register bits */

/* RSCFDnCmCFG - Classical CAN only */
#define RCANFD_CFG_SJW(x)		(((x) & 0x3) << 24)
#define RCANFD_CFG_TSEG2(x)		(((x) & 0x7) << 20)
#define RCANFD_CFG_TSEG1(x)		(((x) & 0xf) << 16)
#define RCANFD_CFG_BRP(x)		(((x) & 0x3ff) << 0)

/* RSCFDnCFDCmNCFG - CAN FD only */
#define RCANFD_NCFG_NTSEG2(x)		(((x) & 0x1f) << 24)
#define RCANFD_NCFG_NTSEG1(x)		(((x) & 0x7f) << 16)
#define RCANFD_NCFG_NSJW(x)		(((x) & 0x1f) << 11)
#define RCANFD_NCFG_NBRP(x)		(((x) & 0x3ff) << 0)

/* RSCFDnCFDCmCTR / RSCFDnCmCTR */
#define RCANFD_CCTR_CTME		BIT(24)
#define RCANFD_CCTR_ERRD		BIT(23)
#define RCANFD_CCTR_BOM_MASK		(0x3 << 21)
#define RCANFD_CCTR_BOM_ISO		(0x0 << 21)
#define RCANFD_CCTR_BOM_BENTRY		(0x1 << 21)
#define RCANFD_CCTR_BOM_BEND		(0x2 << 21)
#define RCANFD_CCTR_TDCVFIE		BIT(19)
#define RCANFD_CCTR_SOCOIE		BIT(18)
#define RCANFD_CCTR_EOCOIE		BIT(17)
#define RCANFD_CCTR_TAIE		BIT(16)
#define RCANFD_CCTR_ALIE		BIT(15)
#define RCANFD_CCTR_BLIE		BIT(14)
#define RCANFD_CCTR_OLIE		BIT(13)
#define RCANFD_CCTR_BORIE		BIT(12)
#define RCANFD_CCTR_BOEIE		BIT(11)
#define RCANFD_CCTR_EPIE		BIT(10)
#define RCANFD_CCTR_EWIE		BIT(9)
#define RCANFD_CCTR_BEIE		BIT(8)
#define RCANFD_CCTR_CSLPR		BIT(2)
#define RCANFD_CCTR_CHMDC_MASK		(0x3)
#define RCANFD_CCTR_CHDMC_COPM		(0x0)
#define RCANFD_CCTR_CHDMC_CRESET	(0x1)
#define RCANFD_CCTR_CHDMC_CHLT		(0x2)

/* RSCFDnCFDCmSTS / RSCFDnCmSTS */
#define RCANFD_CSTS_COMSTS		BIT(7)
#define RCANFD_CSTS_RECSTS		BIT(6)
#define RCANFD_CSTS_TRMSTS		BIT(5)
#define RCANFD_CSTS_BOSTS		BIT(4)
#define RCANFD_CSTS_EPSTS		BIT(3)
#define RCANFD_CSTS_SLPSTS		BIT(2)
#define RCANFD_CSTS_HLTSTS		BIT(1)
#define RCANFD_CSTS_CRSTSTS		BIT(0)

#define RCANFD_CSTS_TECCNT(x)		(((x) >> 24) & 0xff)
#define RCANFD_CSTS_RECCNT(x)		(((x) >> 16) & 0xff)

/* RSCFDnCFDCmERFL / RSCFDnCmERFL */
#define RCANFD_CERFL_ADERR		BIT(14)
#define RCANFD_CERFL_B0ERR		BIT(13)
#define RCANFD_CERFL_B1ERR		BIT(12)
#define RCANFD_CERFL_CERR		BIT(11)
#define RCANFD_CERFL_AERR		BIT(10)
#define RCANFD_CERFL_FERR		BIT(9)
#define RCANFD_CERFL_SERR		BIT(8)
#define RCANFD_CERFL_ALF		BIT(7)
#define RCANFD_CERFL_BLF		BIT(6)
#define RCANFD_CERFL_OVLF		BIT(5)
#define RCANFD_CERFL_BORF		BIT(4)
#define RCANFD_CERFL_BOEF		BIT(3)
#define RCANFD_CERFL_EPF		BIT(2)
#define RCANFD_CERFL_EWF		BIT(1)
#define RCANFD_CERFL_BEF		BIT(0)

#define RCANFD_CERFL_ERR(x)		((x) & (0x7fff)) /* above bits 14:0 */

/* RSCFDnCFDCmDCFG */
#define RCANFD_DCFG_DSJW(x)		(((x) & 0x7) << 24)
#define RCANFD_DCFG_DTSEG2(x)		(((x) & 0x7) << 20)
#define RCANFD_DCFG_DTSEG1(x)		(((x) & 0xf) << 16)
#define RCANFD_DCFG_DBRP(x)		(((x) & 0xff) << 0)

/* RSCFDnCFDCmFDCFG */
#define RCANFD_FDCFG_TDCE		BIT(9)
#define RCANFD_FDCFG_TDCOC		BIT(8)
#define RCANFD_FDCFG_TDCO(x)		(((x) & 0x7f) >> 16)

/* RSCFDnCFDRFCCx */
#define RCANFD_RFCC_RFIM		BIT(12)
#define RCANFD_RFCC_RFDC(x)		(((x) & 0x7) << 8)
#define RCANFD_RFCC_RFPLS(x)		(((x) & 0x7) << 4)
#define RCANFD_RFCC_RFIE		BIT(1)
#define RCANFD_RFCC_RFE			BIT(0)

/* RSCFDnCFDRFSTSx */
#define RCANFD_RFSTS_RFIF		BIT(3)
#define RCANFD_RFSTS_RFMLT		BIT(2)
#define RCANFD_RFSTS_RFFLL		BIT(1)
#define RCANFD_RFSTS_RFEMP		BIT(0)

/* RSCFDnCFDRFIDx */
#define RCANFD_RFID_RFIDE		BIT(31)
#define RCANFD_RFID_RFRTR		BIT(30)

/* RSCFDnCFDRFPTRx */
#define RCANFD_RFPTR_RFDLC(x)		(((x) >> 28) & 0xf)
#define RCANFD_RFPTR_RFPTR(x)		(((x) >> 16) & 0xfff)
#define RCANFD_RFPTR_RFTS(x)		(((x) >> 0) & 0xffff)

/* RSCFDnCFDRFFDSTSx */
#define RCANFD_RFFDSTS_RFFDF		BIT(2)
#define RCANFD_RFFDSTS_RFBRS		BIT(1)
#define RCANFD_RFFDSTS_RFESI		BIT(0)

/* Common FIFO bits */

/* RSCFDnCFDCFCCk */
#define RCANFD_CFCC_CFTML(x)		(((x) & 0xf) << 20)
#define RCANFD_CFCC_CFM(x)		(((x) & 0x3) << 16)
#define RCANFD_CFCC_CFIM		BIT(12)
#define RCANFD_CFCC_CFDC(x)		(((x) & 0x7) << 8)
#define RCANFD_CFCC_CFPLS(x)		(((x) & 0x7) << 4)
#define RCANFD_CFCC_CFTXIE		BIT(2)
#define RCANFD_CFCC_CFE			BIT(0)

/* RSCFDnCFDCFSTSk */
#define RCANFD_CFSTS_CFMC(x)		(((x) >> 8) & 0xff)
#define RCANFD_CFSTS_CFTXIF		BIT(4)
#define RCANFD_CFSTS_CFMLT		BIT(2)
#define RCANFD_CFSTS_CFFLL		BIT(1)
#define RCANFD_CFSTS_CFEMP		BIT(0)

/* RSCFDnCFDCFIDk */
#define RCANFD_CFID_CFIDE		BIT(31)
#define RCANFD_CFID_CFRTR		BIT(30)
#define RCANFD_CFID_CFID_MASK(x)	((x) & 0x1fffffff)

/* RSCFDnCFDCFPTRk */
#define RCANFD_CFPTR_CFDLC(x)		(((x) & 0xf) << 28)
#define RCANFD_CFPTR_CFPTR(x)		(((x) & 0xfff) << 16)
#define RCANFD_CFPTR_CFTS(x)		(((x) & 0xff) << 0)

/* RSCFDnCFDCFFDCSTSk */
#define RCANFD_CFFDCSTS_CFFDF		BIT(2)
#define RCANFD_CFFDCSTS_CFBRS		BIT(1)
#define RCANFD_CFFDCSTS_CFESI		BIT(0)

/* This controller supports either Classical CAN only mode or CAN FD only mode.
 * These modes are supported in two separate set of register maps & names.
 * However, some of the register offsets are common for both modes. Those
 * offsets are listed below as Common registers.
 *
 * The CAN FD only mode specific registers & Classical CAN only mode specific
 * registers are listed separately. Their register names starts with
 * RCANFD_F_xxx & RCANFD_C_xxx respectively.
 */

/* Common registers */

/* RSCFDnCFDCmNCFG / RSCFDnCmCFG */
#define RCANFD_CCFG(m)			(0x0000 + (0x10 * (m)))
/* RSCFDnCFDCmCTR / RSCFDnCmCTR */
#define RCANFD_CCTR(m)			(0x0004 + (0x10 * (m)))
/* RSCFDnCFDCmSTS / RSCFDnCmSTS */
#define RCANFD_CSTS(m)			(0x0008 + (0x10 * (m)))
/* RSCFDnCFDCmERFL / RSCFDnCmERFL */
#define RCANFD_CERFL(m)			(0x000C + (0x10 * (m)))

/* RSCFDnCFDGCFG / RSCFDnGCFG */
#define RCANFD_GCFG			(0x0084)
/* RSCFDnCFDGCTR / RSCFDnGCTR */
#define RCANFD_GCTR			(0x0088)
/* RSCFDnCFDGCTS / RSCFDnGCTS */
#define RCANFD_GSTS			(0x008c)
/* RSCFDnCFDGERFL / RSCFDnGERFL */
#define RCANFD_GERFL			(0x0090)
/* RSCFDnCFDGTSC / RSCFDnGTSC */
#define RCANFD_GTSC			(0x0094)
/* RSCFDnCFDGAFLECTR / RSCFDnGAFLECTR */
#define RCANFD_GAFLECTR			(0x0098)
/* RSCFDnCFDGAFLCFG0 / RSCFDnGAFLCFG0 */
#define RCANFD_GAFLCFG0			(0x009c)
/* RSCFDnCFDGAFLCFG1 / RSCFDnGAFLCFG1 */
#define RCANFD_GAFLCFG1			(0x00a0)
/* RSCFDnCFDRMNB / RSCFDnRMNB */
#define RCANFD_RMNB			(0x00a4)
/* RSCFDnCFDRMND / RSCFDnRMND */
#define RCANFD_RMND(y)			(0x00a8 + (0x04 * (y)))

/* RSCFDnCFDRFCCx / RSCFDnRFCCx */
#define RCANFD_RFCC(x)			(0x00b8 + (0x04 * (x)))
/* RSCFDnCFDRFSTSx / RSCFDnRFSTSx */
#define RCANFD_RFSTS(x)			(0x00d8 + (0x04 * (x)))
/* RSCFDnCFDRFPCTRx / RSCFDnRFPCTRx */
#define RCANFD_RFPCTR(x)		(0x00f8 + (0x04 * (x)))

/* Common FIFO Control registers */

/* RSCFDnCFDCFCCx / RSCFDnCFCCx */
#define RCANFD_CFCC(ch, idx)		(0x0118 + (0x0c * (ch)) + \
					 (0x04 * (idx)))
/* RSCFDnCFDCFSTSx / RSCFDnCFSTSx */
#define RCANFD_CFSTS(ch, idx)		(0x0178 + (0x0c * (ch)) + \
					 (0x04 * (idx)))
/* RSCFDnCFDCFPCTRx / RSCFDnCFPCTRx */
#define RCANFD_CFPCTR(ch, idx)		(0x01d8 + (0x0c * (ch)) + \
					 (0x04 * (idx)))

/* RSCFDnCFDFESTS / RSCFDnFESTS */
#define RCANFD_FESTS			(0x0238)
/* RSCFDnCFDFFSTS / RSCFDnFFSTS */
#define RCANFD_FFSTS			(0x023c)
/* RSCFDnCFDFMSTS / RSCFDnFMSTS */
#define RCANFD_FMSTS			(0x0240)
/* RSCFDnCFDRFISTS / RSCFDnRFISTS */
#define RCANFD_RFISTS			(0x0244)
/* RSCFDnCFDCFRISTS / RSCFDnCFRISTS */
#define RCANFD_CFRISTS			(0x0248)
/* RSCFDnCFDCFTISTS / RSCFDnCFTISTS */
#define RCANFD_CFTISTS			(0x024c)

/* RSCFDnCFDTMCp / RSCFDnTMCp */
#define RCANFD_TMC(p)			(0x0250 + (0x01 * (p)))
/* RSCFDnCFDTMSTSp / RSCFDnTMSTSp */
#define RCANFD_TMSTS(p)			(0x02d0 + (0x01 * (p)))

/* RSCFDnCFDTMTRSTSp / RSCFDnTMTRSTSp */
#define RCANFD_TMTRSTS(y)		(0x0350 + (0x04 * (y)))
/* RSCFDnCFDTMTARSTSp / RSCFDnTMTARSTSp */
#define RCANFD_TMTARSTS(y)		(0x0360 + (0x04 * (y)))
/* RSCFDnCFDTMTCSTSp / RSCFDnTMTCSTSp */
#define RCANFD_TMTCSTS(y)		(0x0370 + (0x04 * (y)))
/* RSCFDnCFDTMTASTSp / RSCFDnTMTASTSp */
#define RCANFD_TMTASTS(y)		(0x0380 + (0x04 * (y)))
/* RSCFDnCFDTMIECy / RSCFDnTMIECy */
#define RCANFD_TMIEC(y)			(0x0390 + (0x04 * (y)))

/* RSCFDnCFDTXQCCm / RSCFDnTXQCCm */
#define RCANFD_TXQCC(m)			(0x03a0 + (0x04 * (m)))
/* RSCFDnCFDTXQSTSm / RSCFDnTXQSTSm */
#define RCANFD_TXQSTS(m)		(0x03c0 + (0x04 * (m)))
/* RSCFDnCFDTXQPCTRm / RSCFDnTXQPCTRm */
#define RCANFD_TXQPCTR(m)		(0x03e0 + (0x04 * (m)))

/* RSCFDnCFDTHLCCm / RSCFDnTHLCCm */
#define RCANFD_THLCC(m)			(0x0400 + (0x04 * (m)))
/* RSCFDnCFDTHLSTSm / RSCFDnTHLSTSm */
#define RCANFD_THLSTS(m)		(0x0420 + (0x04 * (m)))
/* RSCFDnCFDTHLPCTRm / RSCFDnTHLPCTRm */
#define RCANFD_THLPCTR(m)		(0x0440 + (0x04 * (m)))

/* RSCFDnCFDGTINTSTS0 / RSCFDnGTINTSTS0 */
#define RCANFD_GTINTSTS0		(0x0460)
/* RSCFDnCFDGTINTSTS1 / RSCFDnGTINTSTS1 */
#define RCANFD_GTINTSTS1		(0x0464)
/* RSCFDnCFDGTSTCFG / RSCFDnGTSTCFG */
#define RCANFD_GTSTCFG			(0x0468)
/* RSCFDnCFDGTSTCTR / RSCFDnGTSTCTR */
#define RCANFD_GTSTCTR			(0x046c)
/* RSCFDnCFDGLOCKK / RSCFDnGLOCKK */
#define RCANFD_GLOCKK			(0x047c)
/* RSCFDnCFDGRMCFG */
#define RCANFD_GRMCFG			(0x04fc)

/* RSCFDnCFDGAFLIDj / RSCFDnGAFLIDj */
#define RCANFD_GAFLID(offset, j)	((offset) + (0x10 * (j)))
/* RSCFDnCFDGAFLMj / RSCFDnGAFLMj */
#define RCANFD_GAFLM(offset, j)		((offset) + 0x04 + (0x10 * (j)))
/* RSCFDnCFDGAFLP0j / RSCFDnGAFLP0j */
#define RCANFD_GAFLP0(offset, j)	((offset) + 0x08 + (0x10 * (j)))
/* RSCFDnCFDGAFLP1j / RSCFDnGAFLP1j */
#define RCANFD_GAFLP1(offset, j)	((offset) + 0x0c + (0x10 * (j)))

/* Classical CAN only mode register map */

/* RSCFDnGAFLXXXj offset */
#define RCANFD_C_GAFL_OFFSET		(0x0500)

/* RSCFDnRMXXXq -> RCANFD_C_RMXXX(q) */
#define RCANFD_C_RMID(q)		(0x0600 + (0x10 * (q)))
#define RCANFD_C_RMPTR(q)		(0x0604 + (0x10 * (q)))
#define RCANFD_C_RMDF0(q)		(0x0608 + (0x10 * (q)))
#define RCANFD_C_RMDF1(q)		(0x060c + (0x10 * (q)))

/* RSCFDnRFXXx -> RCANFD_C_RFXX(x) */
#define RCANFD_C_RFOFFSET		(0x0e00)
#define RCANFD_C_RFID(x)		(RCANFD_C_RFOFFSET + (0x10 * (x)))
#define RCANFD_C_RFPTR(x)		(RCANFD_C_RFOFFSET + 0x04 + \
					 (0x10 * (x)))
#define RCANFD_C_RFDF(x, df)		(RCANFD_C_RFOFFSET + 0x08 + \
					 (0x10 * (x)) + (0x04 * (df)))

/* RSCFDnCFXXk -> RCANFD_C_CFXX(ch, k) */
#define RCANFD_C_CFOFFSET		(0x0e80)
#define RCANFD_C_CFID(ch, idx)		(RCANFD_C_CFOFFSET + (0x30 * (ch)) + \
					 (0x10 * (idx)))
#define RCANFD_C_CFPTR(ch, idx)		(RCANFD_C_CFOFFSET + 0x04 + \
					 (0x30 * (ch)) + (0x10 * (idx)))
#define RCANFD_C_CFDF(ch, idx, df)	(RCANFD_C_CFOFFSET + 0x08 + \
					 (0x30 * (ch)) + (0x10 * (idx)) + \
					 (0x04 * (df)))

/* RSCFDnTMXXp -> RCANFD_C_TMXX(p) */
#define RCANFD_C_TMID(p)		(0x1000 + (0x10 * (p)))
#define RCANFD_C_TMPTR(p)		(0x1004 + (0x10 * (p)))
#define RCANFD_C_TMDF0(p)		(0x1008 + (0x10 * (p)))
#define RCANFD_C_TMDF1(p)		(0x100c + (0x10 * (p)))

/* RSCFDnTHLACCm */
#define RCANFD_C_THLACC(m)		(0x1800 + (0x04 * (m)))
/* RSCFDnRPGACCr */
#define RCANFD_C_RPGACC(r)		(0x1900 + (0x04 * (r)))

/* CAN FD mode specific register map */

/* RSCFDnCFDCmXXX -> RCANFD_F_XXX(m) */
#define RCANFD_F_DCFG(m)		(0x0500 + (0x20 * (m)))
#define RCANFD_F_CFDCFG(m)		(0x0504 + (0x20 * (m)))
#define RCANFD_F_CFDCTR(m)		(0x0508 + (0x20 * (m)))
#define RCANFD_F_CFDSTS(m)		(0x050c + (0x20 * (m)))
#define RCANFD_F_CFDCRC(m)		(0x0510 + (0x20 * (m)))

/* RSCFDnCFDGAFLXXXj offset */
#define RCANFD_F_GAFL_OFFSET		(0x1000)

/* RSCFDnCFDRMXXXq -> RCANFD_F_RMXXX(q) */
#define RCANFD_F_RMID(q)		(0x2000 + (0x20 * (q)))
#define RCANFD_F_RMPTR(q)		(0x2004 + (0x20 * (q)))
#define RCANFD_F_RMFDSTS(q)		(0x2008 + (0x20 * (q)))
#define RCANFD_F_RMDF(q, b)		(0x200c + (0x04 * (b)) + (0x20 * (q)))

/* RSCFDnCFDRFXXx -> RCANFD_F_RFXX(x) */
#define RCANFD_F_RFOFFSET		(0x3000)
#define RCANFD_F_RFID(x)		(RCANFD_F_RFOFFSET + (0x80 * (x)))
#define RCANFD_F_RFPTR(x)		(RCANFD_F_RFOFFSET + 0x04 + \
					 (0x80 * (x)))
#define RCANFD_F_RFFDSTS(x)		(RCANFD_F_RFOFFSET + 0x08 + \
					 (0x80 * (x)))
#define RCANFD_F_RFDF(x, df)		(RCANFD_F_RFOFFSET + 0x0c + \
					 (0x80 * (x)) + (0x04 * (df)))

/* RSCFDnCFDCFXXk -> RCANFD_F_CFXX(ch, k) */
#define RCANFD_F_CFOFFSET		(0x3400)
#define RCANFD_F_CFID(ch, idx)		(RCANFD_F_CFOFFSET + (0x180 * (ch)) + \
					 (0x80 * (idx)))
#define RCANFD_F_CFPTR(ch, idx)		(RCANFD_F_CFOFFSET + 0x04 + \
					 (0x180 * (ch)) + (0x80 * (idx)))
#define RCANFD_F_CFFDCSTS(ch, idx)	(RCANFD_F_CFOFFSET + 0x08 + \
					 (0x180 * (ch)) + (0x80 * (idx)))
#define RCANFD_F_CFDF(ch, idx, df)	(RCANFD_F_CFOFFSET + 0x0c + \
					 (0x180 * (ch)) + (0x80 * (idx)) + \
					 (0x04 * (df)))

/* RSCFDnCFDTMXXp -> RCANFD_F_TMXX(p) */
#define RCANFD_F_TMID(p)		(0x4000 + (0x20 * (p)))
#define RCANFD_F_TMPTR(p)		(0x4004 + (0x20 * (p)))
#define RCANFD_F_TMFDCTR(p)		(0x4008 + (0x20 * (p)))
#define RCANFD_F_TMDF(p, b)		(0x400c + (0x20 * (p)) + (0x04 * (b)))

/* RSCFDnCFDTHLACCm */
#define RCANFD_F_THLACC(m)		(0x6000 + (0x04 * (m)))
/* RSCFDnCFDRPGACCr */
#define RCANFD_F_RPGACC(r)		(0x6400 + (0x04 * (r)))

/* Constants */
#define RCANFD_FIFO_DEPTH		8	/* Tx FIFO depth */
#define RCANFD_NAPI_WEIGHT		8	/* Rx poll quota */

#define RCANFD_NUM_CHANNELS		2	/* Two channels max */
#define RCANFD_CHANNELS_MASK		BIT((RCANFD_NUM_CHANNELS) - 1)

#define RCANFD_GAFL_PAGENUM(entry)	((entry) / 16)
#define RCANFD_CHANNEL_NUMRULES		1	/* only one rule per channel */

/* Rx FIFO is a global resource of the controller. There are 8 such FIFOs
 * available. Each channel gets a dedicated Rx FIFO (i.e.) the channel
 * number is added to RFFIFO index.
 */
#define RCANFD_RFFIFO_IDX		0

/* Tx/Rx or Common FIFO is a per channel resource. Each channel has 3 Common
 * FIFOs dedicated to them. Use the first (index 0) FIFO out of the 3 for Tx.
 */
#define RCANFD_CFFIFO_IDX		0

/* fCAN clock select register settings */
enum rcar_canfd_fcanclk {
	RCANFD_CANFDCLK = 0,		/* CANFD clock */
	RCANFD_EXTCLK,			/* Externally input clock */
};

struct rcar_canfd_global;

/* Channel priv data */
struct rcar_canfd_channel {
	struct can_priv can;			/* Must be the first member */
	struct net_device *ndev;
	struct rcar_canfd_global *gpriv;	/* Controller reference */
	void __iomem *base;			/* Register base address */
	struct napi_struct napi;
	u8  tx_len[RCANFD_FIFO_DEPTH];		/* For net stats */
	u32 tx_head;				/* Incremented on xmit */
	u32 tx_tail;				/* Incremented on xmit done */
	u32 channel;				/* Channel number */
	spinlock_t tx_lock;			/* To protect tx path */
};

/* Global priv data */
struct rcar_canfd_global {
	struct rcar_canfd_channel *ch[RCANFD_NUM_CHANNELS];
	void __iomem *base;		/* Register base address */
	struct platform_device *pdev;	/* Respective platform device */
	struct clk *clkp;		/* Peripheral clock */
	struct clk *can_clk;		/* fCAN clock */
	enum rcar_canfd_fcanclk fcan;	/* CANFD or Ext clock */
	unsigned long channels_mask;	/* Enabled channels mask */
	bool fdmode;			/* CAN FD or Classical CAN only mode */
};

/* CAN FD mode nominal rate constants */
static const struct can_bittiming_const rcar_canfd_nom_bittiming_const = {
	.name = RCANFD_DRV_NAME,
	.tseg1_min = 2,
	.tseg1_max = 128,
	.tseg2_min = 2,
	.tseg2_max = 32,
	.sjw_max = 32,
	.brp_min = 1,
	.brp_max = 1024,
	.brp_inc = 1,
};

/* CAN FD mode data rate constants */
static const struct can_bittiming_const rcar_canfd_data_bittiming_const = {
	.name = RCANFD_DRV_NAME,
	.tseg1_min = 2,
	.tseg1_max = 16,
	.tseg2_min = 2,
	.tseg2_max = 8,
	.sjw_max = 8,
	.brp_min = 1,
	.brp_max = 256,
	.brp_inc = 1,
};

/* Classical CAN mode bitrate constants */
static const struct can_bittiming_const rcar_canfd_bittiming_const = {
	.name = RCANFD_DRV_NAME,
	.tseg1_min = 4,
	.tseg1_max = 16,
	.tseg2_min = 2,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 1024,
	.brp_inc = 1,
};

/* Helper functions */
static inline void rcar_canfd_update(u32 mask, u32 val, u32 __iomem *reg)
{
	u32 data = readl(reg);

	data &= ~mask;
	data |= (val & mask);
	writel(data, reg);
}

static inline u32 rcar_canfd_read(void __iomem *base, u32 offset)
{
	return readl(base + (offset));
}

static inline void rcar_canfd_write(void __iomem *base, u32 offset, u32 val)
{
	writel(val, base + (offset));
}

static void rcar_canfd_set_bit(void __iomem *base, u32 reg, u32 val)
{
	rcar_canfd_update(val, val, base + (reg));
}

static void rcar_canfd_clear_bit(void __iomem *base, u32 reg, u32 val)
{
	rcar_canfd_update(val, 0, base + (reg));
}

static void rcar_canfd_update_bit(void __iomem *base, u32 reg,
				  u32 mask, u32 val)
{
	rcar_canfd_update(mask, val, base + (reg));
}

static void rcar_canfd_get_data(struct rcar_canfd_channel *priv,
				struct canfd_frame *cf, u32 off)
{
	u32 i, lwords;

	lwords = DIV_ROUND_UP(cf->len, sizeof(u32));
	for (i = 0; i < lwords; i++)
		*((u32 *)cf->data + i) =
			rcar_canfd_read(priv->base, off + (i * sizeof(u32)));
}

static void rcar_canfd_put_data(struct rcar_canfd_channel *priv,
				struct canfd_frame *cf, u32 off)
{
	u32 i, lwords;

	lwords = DIV_ROUND_UP(cf->len, sizeof(u32));
	for (i = 0; i < lwords; i++)
		rcar_canfd_write(priv->base, off + (i * sizeof(u32)),
				 *((u32 *)cf->data + i));
}

static void rcar_canfd_tx_failure_cleanup(struct net_device *ndev)
{
	u32 i;

	for (i = 0; i < RCANFD_FIFO_DEPTH; i++)
		can_free_echo_skb(ndev, i);
}

static int rcar_canfd_reset_controller(struct rcar_canfd_global *gpriv)
{
	u32 sts, ch;
	int err;

	/* Check RAMINIT flag as CAN RAM initialization takes place
	 * after the MCU reset
	 */
	err = readl_poll_timeout((gpriv->base + RCANFD_GSTS), sts,
				 !(sts & RCANFD_GSTS_GRAMINIT), 2, 500000);
	if (err) {
		dev_dbg(&gpriv->pdev->dev, "global raminit failed\n");
		return err;
	}

	/* Transition to Global Reset mode */
	rcar_canfd_clear_bit(gpriv->base, RCANFD_GCTR, RCANFD_GCTR_GSLPR);
	rcar_canfd_update_bit(gpriv->base, RCANFD_GCTR,
			      RCANFD_GCTR_GMDC_MASK, RCANFD_GCTR_GMDC_GRESET);

	/* Ensure Global reset mode */
	err = readl_poll_timeout((gpriv->base + RCANFD_GSTS), sts,
				 (sts & RCANFD_GSTS_GRSTSTS), 2, 500000);
	if (err) {
		dev_dbg(&gpriv->pdev->dev, "global reset failed\n");
		return err;
	}

	/* Reset Global error flags */
	rcar_canfd_write(gpriv->base, RCANFD_GERFL, 0x0);

	/* Set the controller into appropriate mode */
	if (gpriv->fdmode)
		rcar_canfd_set_bit(gpriv->base, RCANFD_GRMCFG,
				   RCANFD_GRMCFG_RCMC);
	else
		rcar_canfd_clear_bit(gpriv->base, RCANFD_GRMCFG,
				     RCANFD_GRMCFG_RCMC);

	/* Transition all Channels to reset mode */
	for_each_set_bit(ch, &gpriv->channels_mask, RCANFD_NUM_CHANNELS) {
		rcar_canfd_clear_bit(gpriv->base,
				     RCANFD_CCTR(ch), RCANFD_CCTR_CSLPR);

		rcar_canfd_update_bit(gpriv->base, RCANFD_CCTR(ch),
				      RCANFD_CCTR_CHMDC_MASK,
				      RCANFD_CCTR_CHDMC_CRESET);

		/* Ensure Channel reset mode */
		err = readl_poll_timeout((gpriv->base + RCANFD_CSTS(ch)), sts,
					 (sts & RCANFD_CSTS_CRSTSTS),
					 2, 500000);
		if (err) {
			dev_dbg(&gpriv->pdev->dev,
				"channel %u reset failed\n", ch);
			return err;
		}
	}
	return 0;
}

static void rcar_canfd_configure_controller(struct rcar_canfd_global *gpriv)
{
	u32 cfg, ch;

	/* Global configuration settings */

	/* ECC Error flag Enable */
	cfg = RCANFD_GCFG_EEFE;

	if (gpriv->fdmode)
		/* Truncate payload to configured message size RFPLS */
		cfg |= RCANFD_GCFG_CMPOC;

	/* Set External Clock if selected */
	if (gpriv->fcan != RCANFD_CANFDCLK)
		cfg |= RCANFD_GCFG_DCS;

	rcar_canfd_set_bit(gpriv->base, RCANFD_GCFG, cfg);

	/* Channel configuration settings */
	for_each_set_bit(ch, &gpriv->channels_mask, RCANFD_NUM_CHANNELS) {
		rcar_canfd_set_bit(gpriv->base, RCANFD_CCTR(ch),
				   RCANFD_CCTR_ERRD);
		rcar_canfd_update_bit(gpriv->base, RCANFD_CCTR(ch),
				      RCANFD_CCTR_BOM_MASK,
				      RCANFD_CCTR_BOM_BENTRY);
	}
}

static void rcar_canfd_configure_afl_rules(struct rcar_canfd_global *gpriv,
					   u32 ch)
{
	u32 cfg;
	int offset, start, page, num_rules = RCANFD_CHANNEL_NUMRULES;
	u32 ridx = ch + RCANFD_RFFIFO_IDX;

	if (ch == 0) {
		start = 0; /* Channel 0 always starts from 0th rule */
	} else {
		/* Get number of Channel 0 rules and adjust */
		cfg = rcar_canfd_read(gpriv->base, RCANFD_GAFLCFG0);
		start = RCANFD_GAFLCFG_GETRNC(0, cfg);
	}

	/* Enable write access to entry */
	page = RCANFD_GAFL_PAGENUM(start);
	rcar_canfd_set_bit(gpriv->base, RCANFD_GAFLECTR,
			   (RCANFD_GAFLECTR_AFLPN(page) |
			    RCANFD_GAFLECTR_AFLDAE));

	/* Write number of rules for channel */
	rcar_canfd_set_bit(gpriv->base, RCANFD_GAFLCFG0,
			   RCANFD_GAFLCFG_SETRNC(ch, num_rules));
	if (gpriv->fdmode)
		offset = RCANFD_F_GAFL_OFFSET;
	else
		offset = RCANFD_C_GAFL_OFFSET;

	/* Accept all IDs */
	rcar_canfd_write(gpriv->base, RCANFD_GAFLID(offset, start), 0);
	/* IDE or RTR is not considered for matching */
	rcar_canfd_write(gpriv->base, RCANFD_GAFLM(offset, start), 0);
	/* Any data length accepted */
	rcar_canfd_write(gpriv->base, RCANFD_GAFLP0(offset, start), 0);
	/* Place the msg in corresponding Rx FIFO entry */
	rcar_canfd_write(gpriv->base, RCANFD_GAFLP1(offset, start),
			 RCANFD_GAFLP1_GAFLFDP(ridx));

	/* Disable write access to page */
	rcar_canfd_clear_bit(gpriv->base,
			     RCANFD_GAFLECTR, RCANFD_GAFLECTR_AFLDAE);
}

static void rcar_canfd_configure_rx(struct rcar_canfd_global *gpriv, u32 ch)
{
	/* Rx FIFO is used for reception */
	u32 cfg;
	u16 rfdc, rfpls;

	/* Select Rx FIFO based on channel */
	u32 ridx = ch + RCANFD_RFFIFO_IDX;

	rfdc = 2;		/* b010 - 8 messages Rx FIFO depth */
	if (gpriv->fdmode)
		rfpls = 7;	/* b111 - Max 64 bytes payload */
	else
		rfpls = 0;	/* b000 - Max 8 bytes payload */

	cfg = (RCANFD_RFCC_RFIM | RCANFD_RFCC_RFDC(rfdc) |
		RCANFD_RFCC_RFPLS(rfpls) | RCANFD_RFCC_RFIE);
	rcar_canfd_write(gpriv->base, RCANFD_RFCC(ridx), cfg);
}

static void rcar_canfd_configure_tx(struct rcar_canfd_global *gpriv, u32 ch)
{
	/* Tx/Rx(Common) FIFO configured in Tx mode is
	 * used for transmission
	 *
	 * Each channel has 3 Common FIFO dedicated to them.
	 * Use the 1st (index 0) out of 3
	 */
	u32 cfg;
	u16 cftml, cfm, cfdc, cfpls;

	cftml = 0;		/* 0th buffer */
	cfm = 1;		/* b01 - Transmit mode */
	cfdc = 2;		/* b010 - 8 messages Tx FIFO depth */
	if (gpriv->fdmode)
		cfpls = 7;	/* b111 - Max 64 bytes payload */
	else
		cfpls = 0;	/* b000 - Max 8 bytes payload */

	cfg = (RCANFD_CFCC_CFTML(cftml) | RCANFD_CFCC_CFM(cfm) |
		RCANFD_CFCC_CFIM | RCANFD_CFCC_CFDC(cfdc) |
		RCANFD_CFCC_CFPLS(cfpls) | RCANFD_CFCC_CFTXIE);
	rcar_canfd_write(gpriv->base, RCANFD_CFCC(ch, RCANFD_CFFIFO_IDX), cfg);

	if (gpriv->fdmode)
		/* Clear FD mode specific control/status register */
		rcar_canfd_write(gpriv->base,
				 RCANFD_F_CFFDCSTS(ch, RCANFD_CFFIFO_IDX), 0);
}

static void rcar_canfd_enable_global_interrupts(struct rcar_canfd_global *gpriv)
{
	u32 ctr;

	/* Clear any stray error interrupt flags */
	rcar_canfd_write(gpriv->base, RCANFD_GERFL, 0);

	/* Global interrupts setup */
	ctr = RCANFD_GCTR_MEIE;
	if (gpriv->fdmode)
		ctr |= RCANFD_GCTR_CFMPOFIE;

	rcar_canfd_set_bit(gpriv->base, RCANFD_GCTR, ctr);
}

static void rcar_canfd_disable_global_interrupts(struct rcar_canfd_global
						 *gpriv)
{
	/* Disable all interrupts */
	rcar_canfd_write(gpriv->base, RCANFD_GCTR, 0);

	/* Clear any stray error interrupt flags */
	rcar_canfd_write(gpriv->base, RCANFD_GERFL, 0);
}

static void rcar_canfd_enable_channel_interrupts(struct rcar_canfd_channel
						 *priv)
{
	u32 ctr, ch = priv->channel;

	/* Clear any stray error flags */
	rcar_canfd_write(priv->base, RCANFD_CERFL(ch), 0);

	/* Channel interrupts setup */
	ctr = (RCANFD_CCTR_TAIE |
	       RCANFD_CCTR_ALIE | RCANFD_CCTR_BLIE |
	       RCANFD_CCTR_OLIE | RCANFD_CCTR_BORIE |
	       RCANFD_CCTR_BOEIE | RCANFD_CCTR_EPIE |
	       RCANFD_CCTR_EWIE | RCANFD_CCTR_BEIE);
	rcar_canfd_set_bit(priv->base, RCANFD_CCTR(ch), ctr);
}

static void rcar_canfd_disable_channel_interrupts(struct rcar_canfd_channel
						  *priv)
{
	u32 ctr, ch = priv->channel;

	ctr = (RCANFD_CCTR_TAIE |
	       RCANFD_CCTR_ALIE | RCANFD_CCTR_BLIE |
	       RCANFD_CCTR_OLIE | RCANFD_CCTR_BORIE |
	       RCANFD_CCTR_BOEIE | RCANFD_CCTR_EPIE |
	       RCANFD_CCTR_EWIE | RCANFD_CCTR_BEIE);
	rcar_canfd_clear_bit(priv->base, RCANFD_CCTR(ch), ctr);

	/* Clear any stray error flags */
	rcar_canfd_write(priv->base, RCANFD_CERFL(ch), 0);
}

static void rcar_canfd_global_error(struct net_device *ndev)
{
	struct rcar_canfd_channel *priv = netdev_priv(ndev);
	struct rcar_canfd_global *gpriv = priv->gpriv;
	struct net_device_stats *stats = &ndev->stats;
	u32 ch = priv->channel;
	u32 gerfl, sts;
	u32 ridx = ch + RCANFD_RFFIFO_IDX;

	gerfl = rcar_canfd_read(priv->base, RCANFD_GERFL);
	if ((gerfl & RCANFD_GERFL_EEF0) && (ch == 0)) {
		netdev_dbg(ndev, "Ch0: ECC Error flag\n");
		stats->tx_dropped++;
	}
	if ((gerfl & RCANFD_GERFL_EEF1) && (ch == 1)) {
		netdev_dbg(ndev, "Ch1: ECC Error flag\n");
		stats->tx_dropped++;
	}
	if (gerfl & RCANFD_GERFL_MES) {
		sts = rcar_canfd_read(priv->base,
				      RCANFD_CFSTS(ch, RCANFD_CFFIFO_IDX));
		if (sts & RCANFD_CFSTS_CFMLT) {
			netdev_dbg(ndev, "Tx Message Lost flag\n");
			stats->tx_dropped++;
			rcar_canfd_write(priv->base,
					 RCANFD_CFSTS(ch, RCANFD_CFFIFO_IDX),
					 sts & ~RCANFD_CFSTS_CFMLT);
		}

		sts = rcar_canfd_read(priv->base, RCANFD_RFSTS(ridx));
		if (sts & RCANFD_RFSTS_RFMLT) {
			netdev_dbg(ndev, "Rx Message Lost flag\n");
			stats->rx_dropped++;
			rcar_canfd_write(priv->base, RCANFD_RFSTS(ridx),
					 sts & ~RCANFD_RFSTS_RFMLT);
		}
	}
	if (gpriv->fdmode && gerfl & RCANFD_GERFL_CMPOF) {
		/* Message Lost flag will be set for respective channel
		 * when this condition happens with counters and flags
		 * already updated.
		 */
		netdev_dbg(ndev, "global payload overflow interrupt\n");
	}

	/* Clear all global error interrupts. Only affected channels bits
	 * get cleared
	 */
	rcar_canfd_write(priv->base, RCANFD_GERFL, 0);
}

static void rcar_canfd_error(struct net_device *ndev, u32 cerfl,
			     u16 txerr, u16 rxerr)
{
	struct rcar_canfd_channel *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u32 ch = priv->channel;

	netdev_dbg(ndev, "ch erfl %x txerr %u rxerr %u\n", cerfl, txerr, rxerr);

	/* Propagate the error condition to the CAN stack */
	skb = alloc_can_err_skb(ndev, &cf);
	if (!skb) {
		stats->rx_dropped++;
		return;
	}

	/* Channel error interrupts */
	if (cerfl & RCANFD_CERFL_BEF) {
		netdev_dbg(ndev, "Bus error\n");
		cf->can_id |= CAN_ERR_BUSERROR | CAN_ERR_PROT;
		cf->data[2] = CAN_ERR_PROT_UNSPEC;
		priv->can.can_stats.bus_error++;
	}
	if (cerfl & RCANFD_CERFL_ADERR) {
		netdev_dbg(ndev, "ACK Delimiter Error\n");
		stats->tx_errors++;
		cf->data[3] |= CAN_ERR_PROT_LOC_ACK_DEL;
	}
	if (cerfl & RCANFD_CERFL_B0ERR) {
		netdev_dbg(ndev, "Bit Error (dominant)\n");
		stats->tx_errors++;
		cf->data[2] |= CAN_ERR_PROT_BIT0;
	}
	if (cerfl & RCANFD_CERFL_B1ERR) {
		netdev_dbg(ndev, "Bit Error (recessive)\n");
		stats->tx_errors++;
		cf->data[2] |= CAN_ERR_PROT_BIT1;
	}
	if (cerfl & RCANFD_CERFL_CERR) {
		netdev_dbg(ndev, "CRC Error\n");
		stats->rx_errors++;
		cf->data[3] |= CAN_ERR_PROT_LOC_CRC_SEQ;
	}
	if (cerfl & RCANFD_CERFL_AERR) {
		netdev_dbg(ndev, "ACK Error\n");
		stats->tx_errors++;
		cf->can_id |= CAN_ERR_ACK;
		cf->data[3] |= CAN_ERR_PROT_LOC_ACK;
	}
	if (cerfl & RCANFD_CERFL_FERR) {
		netdev_dbg(ndev, "Form Error\n");
		stats->rx_errors++;
		cf->data[2] |= CAN_ERR_PROT_FORM;
	}
	if (cerfl & RCANFD_CERFL_SERR) {
		netdev_dbg(ndev, "Stuff Error\n");
		stats->rx_errors++;
		cf->data[2] |= CAN_ERR_PROT_STUFF;
	}
	if (cerfl & RCANFD_CERFL_ALF) {
		netdev_dbg(ndev, "Arbitration lost Error\n");
		priv->can.can_stats.arbitration_lost++;
		cf->can_id |= CAN_ERR_LOSTARB;
		cf->data[0] |= CAN_ERR_LOSTARB_UNSPEC;
	}
	if (cerfl & RCANFD_CERFL_BLF) {
		netdev_dbg(ndev, "Bus Lock Error\n");
		stats->rx_errors++;
		cf->can_id |= CAN_ERR_BUSERROR;
	}
	if (cerfl & RCANFD_CERFL_EWF) {
		netdev_dbg(ndev, "Error warning interrupt\n");
		priv->can.state = CAN_STATE_ERROR_WARNING;
		priv->can.can_stats.error_warning++;
		cf->can_id |= CAN_ERR_CRTL;
		cf->data[1] = txerr > rxerr ? CAN_ERR_CRTL_TX_WARNING :
			CAN_ERR_CRTL_RX_WARNING;
		cf->data[6] = txerr;
		cf->data[7] = rxerr;
	}
	if (cerfl & RCANFD_CERFL_EPF) {
		netdev_dbg(ndev, "Error passive interrupt\n");
		priv->can.state = CAN_STATE_ERROR_PASSIVE;
		priv->can.can_stats.error_passive++;
		cf->can_id |= CAN_ERR_CRTL;
		cf->data[1] = txerr > rxerr ? CAN_ERR_CRTL_TX_PASSIVE :
			CAN_ERR_CRTL_RX_PASSIVE;
		cf->data[6] = txerr;
		cf->data[7] = rxerr;
	}
	if (cerfl & RCANFD_CERFL_BOEF) {
		netdev_dbg(ndev, "Bus-off entry interrupt\n");
		rcar_canfd_tx_failure_cleanup(ndev);
		priv->can.state = CAN_STATE_BUS_OFF;
		priv->can.can_stats.bus_off++;
		can_bus_off(ndev);
		cf->can_id |= CAN_ERR_BUSOFF;
	}
	if (cerfl & RCANFD_CERFL_OVLF) {
		netdev_dbg(ndev,
			   "Overload Frame Transmission error interrupt\n");
		stats->tx_errors++;
		cf->can_id |= CAN_ERR_PROT;
		cf->data[2] |= CAN_ERR_PROT_OVERLOAD;
	}

	/* Clear channel error interrupts that are handled */
	rcar_canfd_write(priv->base, RCANFD_CERFL(ch),
			 RCANFD_CERFL_ERR(~cerfl));
	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;
	netif_rx(skb);
}

static void rcar_canfd_tx_done(struct net_device *ndev)
{
	struct rcar_canfd_channel *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	u32 sts;
	unsigned long flags;
	u32 ch = priv->channel;

	do {
		u8 unsent, sent;

		sent = priv->tx_tail % RCANFD_FIFO_DEPTH;
		stats->tx_packets++;
		stats->tx_bytes += priv->tx_len[sent];
		priv->tx_len[sent] = 0;
		can_get_echo_skb(ndev, sent);

		spin_lock_irqsave(&priv->tx_lock, flags);
		priv->tx_tail++;
		sts = rcar_canfd_read(priv->base,
				      RCANFD_CFSTS(ch, RCANFD_CFFIFO_IDX));
		unsent = RCANFD_CFSTS_CFMC(sts);

		/* Wake producer only when there is room */
		if (unsent != RCANFD_FIFO_DEPTH)
			netif_wake_queue(ndev);

		if (priv->tx_head - priv->tx_tail <= unsent) {
			spin_unlock_irqrestore(&priv->tx_lock, flags);
			break;
		}
		spin_unlock_irqrestore(&priv->tx_lock, flags);

	} while (1);

	/* Clear interrupt */
	rcar_canfd_write(priv->base, RCANFD_CFSTS(ch, RCANFD_CFFIFO_IDX),
			 sts & ~RCANFD_CFSTS_CFTXIF);
	can_led_event(ndev, CAN_LED_EVENT_TX);
}

static irqreturn_t rcar_canfd_global_interrupt(int irq, void *dev_id)
{
	struct rcar_canfd_global *gpriv = dev_id;
	struct net_device *ndev;
	struct rcar_canfd_channel *priv;
	u32 sts, gerfl;
	u32 ch, ridx;

	/* Global error interrupts still indicate a condition specific
	 * to a channel. RxFIFO interrupt is a global interrupt.
	 */
	for_each_set_bit(ch, &gpriv->channels_mask, RCANFD_NUM_CHANNELS) {
		priv = gpriv->ch[ch];
		ndev = priv->ndev;
		ridx = ch + RCANFD_RFFIFO_IDX;

		/* Global error interrupts */
		gerfl = rcar_canfd_read(priv->base, RCANFD_GERFL);
		if (unlikely(RCANFD_GERFL_ERR(gpriv, gerfl)))
			rcar_canfd_global_error(ndev);

		/* Handle Rx interrupts */
		sts = rcar_canfd_read(priv->base, RCANFD_RFSTS(ridx));
		if (likely(sts & RCANFD_RFSTS_RFIF)) {
			if (napi_schedule_prep(&priv->napi)) {
				/* Disable Rx FIFO interrupts */
				rcar_canfd_clear_bit(priv->base,
						     RCANFD_RFCC(ridx),
						     RCANFD_RFCC_RFIE);
				__napi_schedule(&priv->napi);
			}
		}
	}
	return IRQ_HANDLED;
}

static void rcar_canfd_state_change(struct net_device *ndev,
				    u16 txerr, u16 rxerr)
{
	struct rcar_canfd_channel *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	enum can_state rx_state, tx_state, state = priv->can.state;
	struct can_frame *cf;
	struct sk_buff *skb;

	/* Handle transition from error to normal states */
	if (txerr < 96 && rxerr < 96)
		state = CAN_STATE_ERROR_ACTIVE;
	else if (txerr < 128 && rxerr < 128)
		state = CAN_STATE_ERROR_WARNING;

	if (state != priv->can.state) {
		netdev_dbg(ndev, "state: new %d, old %d: txerr %u, rxerr %u\n",
			   state, priv->can.state, txerr, rxerr);
		skb = alloc_can_err_skb(ndev, &cf);
		if (!skb) {
			stats->rx_dropped++;
			return;
		}
		tx_state = txerr >= rxerr ? state : 0;
		rx_state = txerr <= rxerr ? state : 0;

		can_change_state(ndev, cf, tx_state, rx_state);
		stats->rx_packets++;
		stats->rx_bytes += cf->can_dlc;
		netif_rx(skb);
	}
}

static irqreturn_t rcar_canfd_channel_interrupt(int irq, void *dev_id)
{
	struct rcar_canfd_global *gpriv = dev_id;
	struct net_device *ndev;
	struct rcar_canfd_channel *priv;
	u32 sts, ch, cerfl;
	u16 txerr, rxerr;

	/* Common FIFO is a per channel resource */
	for_each_set_bit(ch, &gpriv->channels_mask, RCANFD_NUM_CHANNELS) {
		priv = gpriv->ch[ch];
		ndev = priv->ndev;

		/* Channel error interrupts */
		cerfl = rcar_canfd_read(priv->base, RCANFD_CERFL(ch));
		sts = rcar_canfd_read(priv->base, RCANFD_CSTS(ch));
		txerr = RCANFD_CSTS_TECCNT(sts);
		rxerr = RCANFD_CSTS_RECCNT(sts);
		if (unlikely(RCANFD_CERFL_ERR(cerfl)))
			rcar_canfd_error(ndev, cerfl, txerr, rxerr);

		/* Handle state change to lower states */
		if (unlikely((priv->can.state != CAN_STATE_ERROR_ACTIVE) &&
			     (priv->can.state != CAN_STATE_BUS_OFF)))
			rcar_canfd_state_change(ndev, txerr, rxerr);

		/* Handle Tx interrupts */
		sts = rcar_canfd_read(priv->base,
				      RCANFD_CFSTS(ch, RCANFD_CFFIFO_IDX));
		if (likely(sts & RCANFD_CFSTS_CFTXIF))
			rcar_canfd_tx_done(ndev);
	}
	return IRQ_HANDLED;
}

static void rcar_canfd_set_bittiming(struct net_device *dev)
{
	struct rcar_canfd_channel *priv = netdev_priv(dev);
	const struct can_bittiming *bt = &priv->can.bittiming;
	const struct can_bittiming *dbt = &priv->can.data_bittiming;
	u16 brp, sjw, tseg1, tseg2;
	u32 cfg;
	u32 ch = priv->channel;

	/* Nominal bit timing settings */
	brp = bt->brp - 1;
	sjw = bt->sjw - 1;
	tseg1 = bt->prop_seg + bt->phase_seg1 - 1;
	tseg2 = bt->phase_seg2 - 1;

	if (priv->can.ctrlmode & CAN_CTRLMODE_FD) {
		/* CAN FD only mode */
		cfg = (RCANFD_NCFG_NTSEG1(tseg1) | RCANFD_NCFG_NBRP(brp) |
		       RCANFD_NCFG_NSJW(sjw) | RCANFD_NCFG_NTSEG2(tseg2));

		rcar_canfd_write(priv->base, RCANFD_CCFG(ch), cfg);
		netdev_dbg(priv->ndev, "nrate: brp %u, sjw %u, tseg1 %u, tseg2 %u\n",
			   brp, sjw, tseg1, tseg2);

		/* Data bit timing settings */
		brp = dbt->brp - 1;
		sjw = dbt->sjw - 1;
		tseg1 = dbt->prop_seg + dbt->phase_seg1 - 1;
		tseg2 = dbt->phase_seg2 - 1;

		cfg = (RCANFD_DCFG_DTSEG1(tseg1) | RCANFD_DCFG_DBRP(brp) |
		       RCANFD_DCFG_DSJW(sjw) | RCANFD_DCFG_DTSEG2(tseg2));

		rcar_canfd_write(priv->base, RCANFD_F_DCFG(ch), cfg);
		netdev_dbg(priv->ndev, "drate: brp %u, sjw %u, tseg1 %u, tseg2 %u\n",
			   brp, sjw, tseg1, tseg2);
	} else {
		/* Classical CAN only mode */
		cfg = (RCANFD_CFG_TSEG1(tseg1) | RCANFD_CFG_BRP(brp) |
			RCANFD_CFG_SJW(sjw) | RCANFD_CFG_TSEG2(tseg2));

		rcar_canfd_write(priv->base, RCANFD_CCFG(ch), cfg);
		netdev_dbg(priv->ndev,
			   "rate: brp %u, sjw %u, tseg1 %u, tseg2 %u\n",
			   brp, sjw, tseg1, tseg2);
	}
}

static int rcar_canfd_start(struct net_device *ndev)
{
	struct rcar_canfd_channel *priv = netdev_priv(ndev);
	int err = -EOPNOTSUPP;
	u32 sts, ch = priv->channel;
	u32 ridx = ch + RCANFD_RFFIFO_IDX;

	rcar_canfd_set_bittiming(ndev);

	rcar_canfd_enable_channel_interrupts(priv);

	/* Set channel to Operational mode */
	rcar_canfd_update_bit(priv->base, RCANFD_CCTR(ch),
			      RCANFD_CCTR_CHMDC_MASK, RCANFD_CCTR_CHDMC_COPM);

	/* Verify channel mode change */
	err = readl_poll_timeout((priv->base + RCANFD_CSTS(ch)), sts,
				 (sts & RCANFD_CSTS_COMSTS), 2, 500000);
	if (err) {
		netdev_err(ndev, "channel %u communication state failed\n", ch);
		goto fail_mode_change;
	}

	/* Enable Common & Rx FIFO */
	rcar_canfd_set_bit(priv->base, RCANFD_CFCC(ch, RCANFD_CFFIFO_IDX),
			   RCANFD_CFCC_CFE);
	rcar_canfd_set_bit(priv->base, RCANFD_RFCC(ridx), RCANFD_RFCC_RFE);

	priv->can.state = CAN_STATE_ERROR_ACTIVE;
	return 0;

fail_mode_change:
	rcar_canfd_disable_channel_interrupts(priv);
	return err;
}

static int rcar_canfd_open(struct net_device *ndev)
{
	struct rcar_canfd_channel *priv = netdev_priv(ndev);
	struct rcar_canfd_global *gpriv = priv->gpriv;
	int err;

	/* Peripheral clock is already enabled in probe */
	err = clk_prepare_enable(gpriv->can_clk);
	if (err) {
		netdev_err(ndev, "failed to enable CAN clock, error %d\n", err);
		goto out_clock;
	}

	err = open_candev(ndev);
	if (err) {
		netdev_err(ndev, "open_candev() failed, error %d\n", err);
		goto out_can_clock;
	}

	napi_enable(&priv->napi);
	err = rcar_canfd_start(ndev);
	if (err)
		goto out_close;
	netif_start_queue(ndev);
	can_led_event(ndev, CAN_LED_EVENT_OPEN);
	return 0;
out_close:
	napi_disable(&priv->napi);
	close_candev(ndev);
out_can_clock:
	clk_disable_unprepare(gpriv->can_clk);
out_clock:
	return err;
}

static void rcar_canfd_stop(struct net_device *ndev)
{
	struct rcar_canfd_channel *priv = netdev_priv(ndev);
	int err;
	u32 sts, ch = priv->channel;
	u32 ridx = ch + RCANFD_RFFIFO_IDX;

	/* Transition to channel reset mode  */
	rcar_canfd_update_bit(priv->base, RCANFD_CCTR(ch),
			      RCANFD_CCTR_CHMDC_MASK, RCANFD_CCTR_CHDMC_CRESET);

	/* Check Channel reset mode */
	err = readl_poll_timeout((priv->base + RCANFD_CSTS(ch)), sts,
				 (sts & RCANFD_CSTS_CRSTSTS), 2, 500000);
	if (err)
		netdev_err(ndev, "channel %u reset failed\n", ch);

	rcar_canfd_disable_channel_interrupts(priv);

	/* Disable Common & Rx FIFO */
	rcar_canfd_clear_bit(priv->base, RCANFD_CFCC(ch, RCANFD_CFFIFO_IDX),
			     RCANFD_CFCC_CFE);
	rcar_canfd_clear_bit(priv->base, RCANFD_RFCC(ridx), RCANFD_RFCC_RFE);

	/* Set the state as STOPPED */
	priv->can.state = CAN_STATE_STOPPED;
}

static int rcar_canfd_close(struct net_device *ndev)
{
	struct rcar_canfd_channel *priv = netdev_priv(ndev);
	struct rcar_canfd_global *gpriv = priv->gpriv;

	netif_stop_queue(ndev);
	rcar_canfd_stop(ndev);
	napi_disable(&priv->napi);
	clk_disable_unprepare(gpriv->can_clk);
	close_candev(ndev);
	can_led_event(ndev, CAN_LED_EVENT_STOP);
	return 0;
}

static netdev_tx_t rcar_canfd_start_xmit(struct sk_buff *skb,
					 struct net_device *ndev)
{
	struct rcar_canfd_channel *priv = netdev_priv(ndev);
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	u32 sts = 0, id, dlc;
	unsigned long flags;
	u32 ch = priv->channel;

	if (can_dropped_invalid_skb(ndev, skb))
		return NETDEV_TX_OK;

	if (cf->can_id & CAN_EFF_FLAG) {
		id = cf->can_id & CAN_EFF_MASK;
		id |= RCANFD_CFID_CFIDE;
	} else {
		id = cf->can_id & CAN_SFF_MASK;
	}

	if (cf->can_id & CAN_RTR_FLAG)
		id |= RCANFD_CFID_CFRTR;

	dlc = RCANFD_CFPTR_CFDLC(can_len2dlc(cf->len));

	if (priv->can.ctrlmode & CAN_CTRLMODE_FD) {
		rcar_canfd_write(priv->base,
				 RCANFD_F_CFID(ch, RCANFD_CFFIFO_IDX), id);
		rcar_canfd_write(priv->base,
				 RCANFD_F_CFPTR(ch, RCANFD_CFFIFO_IDX), dlc);

		if (can_is_canfd_skb(skb)) {
			/* CAN FD frame format */
			sts |= RCANFD_CFFDCSTS_CFFDF;
			if (cf->flags & CANFD_BRS)
				sts |= RCANFD_CFFDCSTS_CFBRS;

			if (priv->can.state == CAN_STATE_ERROR_PASSIVE)
				sts |= RCANFD_CFFDCSTS_CFESI;
		}

		rcar_canfd_write(priv->base,
				 RCANFD_F_CFFDCSTS(ch, RCANFD_CFFIFO_IDX), sts);

		rcar_canfd_put_data(priv, cf,
				    RCANFD_F_CFDF(ch, RCANFD_CFFIFO_IDX, 0));
	} else {
		rcar_canfd_write(priv->base,
				 RCANFD_C_CFID(ch, RCANFD_CFFIFO_IDX), id);
		rcar_canfd_write(priv->base,
				 RCANFD_C_CFPTR(ch, RCANFD_CFFIFO_IDX), dlc);
		rcar_canfd_put_data(priv, cf,
				    RCANFD_C_CFDF(ch, RCANFD_CFFIFO_IDX, 0));
	}

	priv->tx_len[priv->tx_head % RCANFD_FIFO_DEPTH] = cf->len;
	can_put_echo_skb(skb, ndev, priv->tx_head % RCANFD_FIFO_DEPTH);

	spin_lock_irqsave(&priv->tx_lock, flags);
	priv->tx_head++;

	/* Stop the queue if we've filled all FIFO entries */
	if (priv->tx_head - priv->tx_tail >= RCANFD_FIFO_DEPTH)
		netif_stop_queue(ndev);

	/* Start Tx: Write 0xff to CFPC to increment the CPU-side
	 * pointer for the Common FIFO
	 */
	rcar_canfd_write(priv->base,
			 RCANFD_CFPCTR(ch, RCANFD_CFFIFO_IDX), 0xff);

	spin_unlock_irqrestore(&priv->tx_lock, flags);
	return NETDEV_TX_OK;
}

static void rcar_canfd_rx_pkt(struct rcar_canfd_channel *priv)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct canfd_frame *cf;
	struct sk_buff *skb;
	u32 sts = 0, id, dlc;
	u32 ch = priv->channel;
	u32 ridx = ch + RCANFD_RFFIFO_IDX;

	if (priv->can.ctrlmode & CAN_CTRLMODE_FD) {
		id = rcar_canfd_read(priv->base, RCANFD_F_RFID(ridx));
		dlc = rcar_canfd_read(priv->base, RCANFD_F_RFPTR(ridx));

		sts = rcar_canfd_read(priv->base, RCANFD_F_RFFDSTS(ridx));
		if (sts & RCANFD_RFFDSTS_RFFDF)
			skb = alloc_canfd_skb(priv->ndev, &cf);
		else
			skb = alloc_can_skb(priv->ndev,
					    (struct can_frame **)&cf);
	} else {
		id = rcar_canfd_read(priv->base, RCANFD_C_RFID(ridx));
		dlc = rcar_canfd_read(priv->base, RCANFD_C_RFPTR(ridx));
		skb = alloc_can_skb(priv->ndev, (struct can_frame **)&cf);
	}

	if (!skb) {
		stats->rx_dropped++;
		return;
	}

	if (id & RCANFD_RFID_RFIDE)
		cf->can_id = (id & CAN_EFF_MASK) | CAN_EFF_FLAG;
	else
		cf->can_id = id & CAN_SFF_MASK;

	if (priv->can.ctrlmode & CAN_CTRLMODE_FD) {
		if (sts & RCANFD_RFFDSTS_RFFDF)
			cf->len = can_dlc2len(RCANFD_RFPTR_RFDLC(dlc));
		else
			cf->len = get_can_dlc(RCANFD_RFPTR_RFDLC(dlc));

		if (sts & RCANFD_RFFDSTS_RFESI) {
			cf->flags |= CANFD_ESI;
			netdev_dbg(priv->ndev, "ESI Error\n");
		}

		if (!(sts & RCANFD_RFFDSTS_RFFDF) && (id & RCANFD_RFID_RFRTR)) {
			cf->can_id |= CAN_RTR_FLAG;
		} else {
			if (sts & RCANFD_RFFDSTS_RFBRS)
				cf->flags |= CANFD_BRS;

			rcar_canfd_get_data(priv, cf, RCANFD_F_RFDF(ridx, 0));
		}
	} else {
		cf->len = get_can_dlc(RCANFD_RFPTR_RFDLC(dlc));
		if (id & RCANFD_RFID_RFRTR)
			cf->can_id |= CAN_RTR_FLAG;
		else
			rcar_canfd_get_data(priv, cf, RCANFD_C_RFDF(ridx, 0));
	}

	/* Write 0xff to RFPC to increment the CPU-side
	 * pointer of the Rx FIFO
	 */
	rcar_canfd_write(priv->base, RCANFD_RFPCTR(ridx), 0xff);

	can_led_event(priv->ndev, CAN_LED_EVENT_RX);

	stats->rx_bytes += cf->len;
	stats->rx_packets++;
	netif_receive_skb(skb);
}

static int rcar_canfd_rx_poll(struct napi_struct *napi, int quota)
{
	struct rcar_canfd_channel *priv =
		container_of(napi, struct rcar_canfd_channel, napi);
	int num_pkts;
	u32 sts;
	u32 ch = priv->channel;
	u32 ridx = ch + RCANFD_RFFIFO_IDX;

	for (num_pkts = 0; num_pkts < quota; num_pkts++) {
		sts = rcar_canfd_read(priv->base, RCANFD_RFSTS(ridx));
		/* Check FIFO empty condition */
		if (sts & RCANFD_RFSTS_RFEMP)
			break;

		rcar_canfd_rx_pkt(priv);

		/* Clear interrupt bit */
		if (sts & RCANFD_RFSTS_RFIF)
			rcar_canfd_write(priv->base, RCANFD_RFSTS(ridx),
					 sts & ~RCANFD_RFSTS_RFIF);
	}

	/* All packets processed */
	if (num_pkts < quota) {
		if (napi_complete_done(napi, num_pkts)) {
			/* Enable Rx FIFO interrupts */
			rcar_canfd_set_bit(priv->base, RCANFD_RFCC(ridx),
					   RCANFD_RFCC_RFIE);
		}
	}
	return num_pkts;
}

static int rcar_canfd_do_set_mode(struct net_device *ndev, enum can_mode mode)
{
	int err;

	switch (mode) {
	case CAN_MODE_START:
		err = rcar_canfd_start(ndev);
		if (err)
			return err;
		netif_wake_queue(ndev);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int rcar_canfd_get_berr_counter(const struct net_device *dev,
				       struct can_berr_counter *bec)
{
	struct rcar_canfd_channel *priv = netdev_priv(dev);
	u32 val, ch = priv->channel;

	/* Peripheral clock is already enabled in probe */
	val = rcar_canfd_read(priv->base, RCANFD_CSTS(ch));
	bec->txerr = RCANFD_CSTS_TECCNT(val);
	bec->rxerr = RCANFD_CSTS_RECCNT(val);
	return 0;
}

static const struct net_device_ops rcar_canfd_netdev_ops = {
	.ndo_open = rcar_canfd_open,
	.ndo_stop = rcar_canfd_close,
	.ndo_start_xmit = rcar_canfd_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static int rcar_canfd_channel_probe(struct rcar_canfd_global *gpriv, u32 ch,
				    u32 fcan_freq)
{
	struct platform_device *pdev = gpriv->pdev;
	struct rcar_canfd_channel *priv;
	struct net_device *ndev;
	int err = -ENODEV;

	ndev = alloc_candev(sizeof(*priv), RCANFD_FIFO_DEPTH);
	if (!ndev) {
		dev_err(&pdev->dev, "alloc_candev() failed\n");
		err = -ENOMEM;
		goto fail;
	}
	priv = netdev_priv(ndev);

	ndev->netdev_ops = &rcar_canfd_netdev_ops;
	ndev->flags |= IFF_ECHO;
	priv->ndev = ndev;
	priv->base = gpriv->base;
	priv->channel = ch;
	priv->can.clock.freq = fcan_freq;
	dev_info(&pdev->dev, "can_clk rate is %u\n", priv->can.clock.freq);

	if (gpriv->fdmode) {
		priv->can.bittiming_const = &rcar_canfd_nom_bittiming_const;
		priv->can.data_bittiming_const =
			&rcar_canfd_data_bittiming_const;

		/* Controller starts in CAN FD only mode */
		can_set_static_ctrlmode(ndev, CAN_CTRLMODE_FD);
		priv->can.ctrlmode_supported = CAN_CTRLMODE_BERR_REPORTING;
	} else {
		/* Controller starts in Classical CAN only mode */
		priv->can.bittiming_const = &rcar_canfd_bittiming_const;
		priv->can.ctrlmode_supported = CAN_CTRLMODE_BERR_REPORTING;
	}

	priv->can.do_set_mode = rcar_canfd_do_set_mode;
	priv->can.do_get_berr_counter = rcar_canfd_get_berr_counter;
	priv->gpriv = gpriv;
	SET_NETDEV_DEV(ndev, &pdev->dev);

	netif_napi_add(ndev, &priv->napi, rcar_canfd_rx_poll,
		       RCANFD_NAPI_WEIGHT);
	err = register_candev(ndev);
	if (err) {
		dev_err(&pdev->dev,
			"register_candev() failed, error %d\n", err);
		goto fail_candev;
	}
	spin_lock_init(&priv->tx_lock);
	devm_can_led_init(ndev);
	gpriv->ch[priv->channel] = priv;
	dev_info(&pdev->dev, "device registered (channel %u)\n", priv->channel);
	return 0;

fail_candev:
	netif_napi_del(&priv->napi);
	free_candev(ndev);
fail:
	return err;
}

static void rcar_canfd_channel_remove(struct rcar_canfd_global *gpriv, u32 ch)
{
	struct rcar_canfd_channel *priv = gpriv->ch[ch];

	if (priv) {
		unregister_candev(priv->ndev);
		netif_napi_del(&priv->napi);
		free_candev(priv->ndev);
	}
}

static int rcar_canfd_probe(struct platform_device *pdev)
{
	void __iomem *addr;
	u32 sts, ch, fcan_freq;
	struct rcar_canfd_global *gpriv;
	struct device_node *of_child;
	unsigned long channels_mask = 0;
	int err, ch_irq, g_irq;
	bool fdmode = true;			/* CAN FD only mode - default */

	if (of_property_read_bool(pdev->dev.of_node, "renesas,no-can-fd"))
		fdmode = false;			/* Classical CAN only mode */

	of_child = of_get_child_by_name(pdev->dev.of_node, "channel0");
	if (of_child && of_device_is_available(of_child))
		channels_mask |= BIT(0);	/* Channel 0 */

	of_child = of_get_child_by_name(pdev->dev.of_node, "channel1");
	if (of_child && of_device_is_available(of_child))
		channels_mask |= BIT(1);	/* Channel 1 */

	ch_irq = platform_get_irq(pdev, 0);
	if (ch_irq < 0) {
		err = ch_irq;
		goto fail_dev;
	}

	g_irq = platform_get_irq(pdev, 1);
	if (g_irq < 0) {
		err = g_irq;
		goto fail_dev;
	}

	/* Global controller context */
	gpriv = devm_kzalloc(&pdev->dev, sizeof(*gpriv), GFP_KERNEL);
	if (!gpriv) {
		err = -ENOMEM;
		goto fail_dev;
	}
	gpriv->pdev = pdev;
	gpriv->channels_mask = channels_mask;
	gpriv->fdmode = fdmode;

	/* Peripheral clock */
	gpriv->clkp = devm_clk_get(&pdev->dev, "fck");
	if (IS_ERR(gpriv->clkp)) {
		err = PTR_ERR(gpriv->clkp);
		dev_err(&pdev->dev, "cannot get peripheral clock, error %d\n",
			err);
		goto fail_dev;
	}

	/* fCAN clock: Pick External clock. If not available fallback to
	 * CANFD clock
	 */
	gpriv->can_clk = devm_clk_get(&pdev->dev, "can_clk");
	if (IS_ERR(gpriv->can_clk) || (clk_get_rate(gpriv->can_clk) == 0)) {
		gpriv->can_clk = devm_clk_get(&pdev->dev, "canfd");
		if (IS_ERR(gpriv->can_clk)) {
			err = PTR_ERR(gpriv->can_clk);
			dev_err(&pdev->dev,
				"cannot get canfd clock, error %d\n", err);
			goto fail_dev;
		}
		gpriv->fcan = RCANFD_CANFDCLK;

	} else {
		gpriv->fcan = RCANFD_EXTCLK;
	}
	fcan_freq = clk_get_rate(gpriv->can_clk);

	if (gpriv->fcan == RCANFD_CANFDCLK)
		/* CANFD clock is further divided by (1/2) within the IP */
		fcan_freq /= 2;

	addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(addr)) {
		err = PTR_ERR(addr);
		goto fail_dev;
	}
	gpriv->base = addr;

	/* Request IRQ that's common for both channels */
	err = devm_request_irq(&pdev->dev, ch_irq,
			       rcar_canfd_channel_interrupt, 0,
			       "canfd.chn", gpriv);
	if (err) {
		dev_err(&pdev->dev, "devm_request_irq(%d) failed, error %d\n",
			ch_irq, err);
		goto fail_dev;
	}
	err = devm_request_irq(&pdev->dev, g_irq,
			       rcar_canfd_global_interrupt, 0,
			       "canfd.gbl", gpriv);
	if (err) {
		dev_err(&pdev->dev, "devm_request_irq(%d) failed, error %d\n",
			g_irq, err);
		goto fail_dev;
	}

	/* Enable peripheral clock for register access */
	err = clk_prepare_enable(gpriv->clkp);
	if (err) {
		dev_err(&pdev->dev,
			"failed to enable peripheral clock, error %d\n", err);
		goto fail_dev;
	}

	err = rcar_canfd_reset_controller(gpriv);
	if (err) {
		dev_err(&pdev->dev, "reset controller failed\n");
		goto fail_clk;
	}

	/* Controller in Global reset & Channel reset mode */
	rcar_canfd_configure_controller(gpriv);

	/* Configure per channel attributes */
	for_each_set_bit(ch, &gpriv->channels_mask, RCANFD_NUM_CHANNELS) {
		/* Configure Channel's Rx fifo */
		rcar_canfd_configure_rx(gpriv, ch);

		/* Configure Channel's Tx (Common) fifo */
		rcar_canfd_configure_tx(gpriv, ch);

		/* Configure receive rules */
		rcar_canfd_configure_afl_rules(gpriv, ch);
	}

	/* Configure common interrupts */
	rcar_canfd_enable_global_interrupts(gpriv);

	/* Start Global operation mode */
	rcar_canfd_update_bit(gpriv->base, RCANFD_GCTR, RCANFD_GCTR_GMDC_MASK,
			      RCANFD_GCTR_GMDC_GOPM);

	/* Verify mode change */
	err = readl_poll_timeout((gpriv->base + RCANFD_GSTS), sts,
				 !(sts & RCANFD_GSTS_GNOPM), 2, 500000);
	if (err) {
		dev_err(&pdev->dev, "global operational mode failed\n");
		goto fail_mode;
	}

	for_each_set_bit(ch, &gpriv->channels_mask, RCANFD_NUM_CHANNELS) {
		err = rcar_canfd_channel_probe(gpriv, ch, fcan_freq);
		if (err)
			goto fail_channel;
	}

	platform_set_drvdata(pdev, gpriv);
	dev_info(&pdev->dev, "global operational state (clk %d, fdmode %d)\n",
		 gpriv->fcan, gpriv->fdmode);
	return 0;

fail_channel:
	for_each_set_bit(ch, &gpriv->channels_mask, RCANFD_NUM_CHANNELS)
		rcar_canfd_channel_remove(gpriv, ch);
fail_mode:
	rcar_canfd_disable_global_interrupts(gpriv);
fail_clk:
	clk_disable_unprepare(gpriv->clkp);
fail_dev:
	return err;
}

static int rcar_canfd_remove(struct platform_device *pdev)
{
	struct rcar_canfd_global *gpriv = platform_get_drvdata(pdev);
	u32 ch;

	rcar_canfd_reset_controller(gpriv);
	rcar_canfd_disable_global_interrupts(gpriv);

	for_each_set_bit(ch, &gpriv->channels_mask, RCANFD_NUM_CHANNELS) {
		rcar_canfd_disable_channel_interrupts(gpriv->ch[ch]);
		rcar_canfd_channel_remove(gpriv, ch);
	}

	/* Enter global sleep mode */
	rcar_canfd_set_bit(gpriv->base, RCANFD_GCTR, RCANFD_GCTR_GSLPR);
	clk_disable_unprepare(gpriv->clkp);
	return 0;
}

static int __maybe_unused rcar_canfd_suspend(struct device *dev)
{
	return 0;
}

static int __maybe_unused rcar_canfd_resume(struct device *dev)
{
	return 0;
}

static SIMPLE_DEV_PM_OPS(rcar_canfd_pm_ops, rcar_canfd_suspend,
			 rcar_canfd_resume);

static const struct of_device_id rcar_canfd_of_table[] = {
	{ .compatible = "renesas,rcar-gen3-canfd" },
	{ }
};

MODULE_DEVICE_TABLE(of, rcar_canfd_of_table);

static struct platform_driver rcar_canfd_driver = {
	.driver = {
		.name = RCANFD_DRV_NAME,
		.of_match_table = of_match_ptr(rcar_canfd_of_table),
		.pm = &rcar_canfd_pm_ops,
	},
	.probe = rcar_canfd_probe,
	.remove = rcar_canfd_remove,
};

module_platform_driver(rcar_canfd_driver);

MODULE_AUTHOR("Ramesh Shanmugasundaram <ramesh.shanmugasundaram@bp.renesas.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CAN FD driver for Renesas R-Car SoC");
MODULE_ALIAS("platform:" RCANFD_DRV_NAME);
