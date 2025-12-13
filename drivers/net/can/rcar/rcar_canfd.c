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

#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/can/dev.h>
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/types.h>

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
#define RCANFD_GERFL_EEF		GENMASK(23, 16)
#define RCANFD_GERFL_CMPOF		BIT(3)	/* CAN FD only */
#define RCANFD_GERFL_THLES		BIT(2)
#define RCANFD_GERFL_MES		BIT(1)
#define RCANFD_GERFL_DEF		BIT(0)

#define RCANFD_GERFL_ERR(gpriv, x) \
({\
	typeof(gpriv) (_gpriv) = (gpriv); \
	((x) & ((FIELD_PREP(RCANFD_GERFL_EEF, (_gpriv)->channels_mask)) | \
		RCANFD_GERFL_MES | ((_gpriv)->fdmode ? RCANFD_GERFL_CMPOF : 0))); \
})

/* AFL Rx rules registers */

/* RSCFDnCFDGAFLECTR / RSCFDnGAFLECTR */
#define RCANFD_GAFLECTR_AFLDAE		BIT(8)
#define RCANFD_GAFLECTR_AFLPN(gpriv, page_num)	((page_num) & (gpriv)->info->max_aflpn)

/* RSCFDnCFDGAFLIDj / RSCFDnGAFLIDj */
#define RCANFD_GAFLID_GAFLLB		BIT(29)

/* RSCFDnCFDGAFLP1_j / RSCFDnGAFLP1_j */
#define RCANFD_GAFLP1_GAFLFDP(x)	(1 << (x))

/* Channel register bits */

/* RSCFDnCmCFG - Classical CAN only */
#define RCANFD_CFG_SJW		GENMASK(25, 24)
#define RCANFD_CFG_TSEG2	GENMASK(22, 20)
#define RCANFD_CFG_TSEG1	GENMASK(19, 16)
#define RCANFD_CFG_BRP		GENMASK(9, 0)

/* RSCFDnCFDCmNCFG - CAN FD only */
#define RCANFD_NCFG_NBRP	GENMASK(9, 0)

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
#define RCANFD_DCFG_DBRP		GENMASK(7, 0)

/* RSCFDnCFDCmFDCFG */
#define RCANFD_GEN4_FDCFG_CLOE		BIT(30)
#define RCANFD_GEN4_FDCFG_FDOE		BIT(28)
#define RCANFD_FDCFG_TDCO		GENMASK(23, 16)
#define RCANFD_FDCFG_TDCE		BIT(9)
#define RCANFD_FDCFG_TDCOC		BIT(8)

/* RSCFDnCFDCmFDSTS */
#define RCANFD_FDSTS_SOC		GENMASK(31, 24)
#define RCANFD_FDSTS_EOC		GENMASK(23, 16)
#define RCANFD_GEN4_FDSTS_TDCVF		BIT(15)
#define RCANFD_GEN4_FDSTS_PNSTS		GENMASK(13, 12)
#define RCANFD_FDSTS_SOCO		BIT(9)
#define RCANFD_FDSTS_EOCO		BIT(8)
#define RCANFD_FDSTS_TDCVF		BIT(7)
#define RCANFD_FDSTS_TDCR		GENMASK(7, 0)

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

/* RSCFDnCFDRFFDSTSx */
#define RCANFD_RFFDSTS_RFFDF		BIT(2)
#define RCANFD_RFFDSTS_RFBRS		BIT(1)
#define RCANFD_RFFDSTS_RFESI		BIT(0)

/* Common FIFO bits */

/* RSCFDnCFDCFCCk */
#define RCANFD_CFCC_CFTML(gpriv, cftml) \
({\
	typeof(gpriv) (_gpriv) = (gpriv); \
	(((cftml) & (_gpriv)->info->max_cftml) << (_gpriv)->info->sh->cftml); \
})
#define RCANFD_CFCC_CFM(gpriv, x)	(((x) & 0x3) << (gpriv)->info->sh->cfm)
#define RCANFD_CFCC_CFIM		BIT(12)
#define RCANFD_CFCC_CFDC(gpriv, x)	(((x) & 0x7) << (gpriv)->info->sh->cfdc)
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

/* RSCFDnCFDCFPTRk */
#define RCANFD_CFPTR_CFDLC(x)		(((x) & 0xf) << 28)

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
/* RSCFDnCFDGAFLCFG / RSCFDnGAFLCFG */
#define RCANFD_GAFLCFG(w)		(0x009c + (0x04 * (w)))
/* RSCFDnCFDRMNB / RSCFDnRMNB */
#define RCANFD_RMNB			(0x00a4)
/* RSCFDnCFDRMND / RSCFDnRMND */
#define RCANFD_RMND(y)			(0x00a8 + (0x04 * (y)))

/* RSCFDnCFDRFCCx / RSCFDnRFCCx */
#define RCANFD_RFCC(gpriv, x)		((gpriv)->info->regs->rfcc + (0x04 * (x)))
/* RSCFDnCFDRFSTSx / RSCFDnRFSTSx */
#define RCANFD_RFSTS(gpriv, x)		(RCANFD_RFCC(gpriv, x) + 0x20)
/* RSCFDnCFDRFPCTRx / RSCFDnRFPCTRx */
#define RCANFD_RFPCTR(gpriv, x)		(RCANFD_RFCC(gpriv, x) + 0x40)

/* Common FIFO Control registers */

/* RSCFDnCFDCFCCx / RSCFDnCFCCx */
#define RCANFD_CFCC(gpriv, ch, idx) \
	((gpriv)->info->regs->cfcc + (0x0c * (ch)) + (0x04 * (idx)))
/* RSCFDnCFDCFSTSx / RSCFDnCFSTSx */
#define RCANFD_CFSTS(gpriv, ch, idx) \
	((gpriv)->info->regs->cfsts + (0x0c * (ch)) + (0x04 * (idx)))
/* RSCFDnCFDCFPCTRx / RSCFDnCFPCTRx */
#define RCANFD_CFPCTR(gpriv, ch, idx) \
	((gpriv)->info->regs->cfpctr + (0x0c * (ch)) + (0x04 * (idx)))

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

/* RSCFDnRFXXx -> RCANFD_C_RFXX(x) */
#define RCANFD_C_RFOFFSET	(0x0e00)
#define RCANFD_C_RFID(x)	(RCANFD_C_RFOFFSET + (0x10 * (x)))
#define RCANFD_C_RFPTR(x)	(RCANFD_C_RFOFFSET + 0x04 + (0x10 * (x)))
#define RCANFD_C_RFDF(x, df) \
		(RCANFD_C_RFOFFSET + 0x08 + (0x10 * (x)) + (0x04 * (df)))

/* RSCFDnCFXXk -> RCANFD_C_CFXX(ch, k) */
#define RCANFD_C_CFOFFSET		(0x0e80)

#define RCANFD_C_CFID(ch, idx) \
	(RCANFD_C_CFOFFSET + (0x30 * (ch)) + (0x10 * (idx)))

#define RCANFD_C_CFPTR(ch, idx)	\
	(RCANFD_C_CFOFFSET + 0x04 + (0x30 * (ch)) + (0x10 * (idx)))

#define RCANFD_C_CFDF(ch, idx, df) \
	(RCANFD_C_CFOFFSET + 0x08 + (0x30 * (ch)) + (0x10 * (idx)) + (0x04 * (df)))

/* R-Car Gen4 Classical and CAN FD mode specific register map */
#define RCANFD_GEN4_GAFL_OFFSET		(0x1800)

/* CAN FD mode specific register map */

/* RSCFDnCFDCmXXX -> gpriv->fcbase[m].xxx */
struct rcar_canfd_f_c {
	u32 dcfg;
	u32 cfdcfg;
	u32 cfdctr;
	u32 cfdsts;
	u32 cfdcrc;
	u32 pad[3];
};

/* RSCFDnCFDGAFLXXXj offset */
#define RCANFD_F_GAFL_OFFSET		(0x1000)

/* RSCFDnCFDRFXXx -> RCANFD_F_RFXX(x) */
#define RCANFD_F_RFOFFSET(gpriv)	((gpriv)->info->regs->rfoffset)
#define RCANFD_F_RFID(gpriv, x)		(RCANFD_F_RFOFFSET(gpriv) + (0x80 * (x)))
#define RCANFD_F_RFPTR(gpriv, x)	(RCANFD_F_RFOFFSET(gpriv) + 0x04 + (0x80 * (x)))
#define RCANFD_F_RFFDSTS(gpriv, x)	(RCANFD_F_RFOFFSET(gpriv) + 0x08 + (0x80 * (x)))
#define RCANFD_F_RFDF(gpriv, x, df) \
	(RCANFD_F_RFOFFSET(gpriv) + 0x0c + (0x80 * (x)) + (0x04 * (df)))

/* RSCFDnCFDCFXXk -> RCANFD_F_CFXX(ch, k) */
#define RCANFD_F_CFOFFSET(gpriv)	((gpriv)->info->regs->cfoffset)

#define RCANFD_F_CFID(gpriv, ch, idx) \
	(RCANFD_F_CFOFFSET(gpriv) + (0x180 * (ch)) + (0x80 * (idx)))

#define RCANFD_F_CFPTR(gpriv, ch, idx) \
	(RCANFD_F_CFOFFSET(gpriv) + 0x04 + (0x180 * (ch)) + (0x80 * (idx)))

#define RCANFD_F_CFFDCSTS(gpriv, ch, idx) \
	(RCANFD_F_CFOFFSET(gpriv) + 0x08 + (0x180 * (ch)) + (0x80 * (idx)))

#define RCANFD_F_CFDF(gpriv, ch, idx, df) \
	(RCANFD_F_CFOFFSET(gpriv) + 0x0c + (0x180 * (ch)) + (0x80 * (idx)) + \
	 (0x04 * (df)))

/* Constants */
#define RCANFD_FIFO_DEPTH		8	/* Tx FIFO depth */
#define RCANFD_NAPI_WEIGHT		8	/* Rx poll quota */

#define RCANFD_NUM_CHANNELS		8	/* Eight channels max */

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

struct rcar_canfd_global;

struct rcar_canfd_regs {
	u16 rfcc;	/* RX FIFO Configuration/Control Register */
	u16 cfcc;	/* Common FIFO Configuration/Control Register */
	u16 cfsts;	/* Common FIFO Status Register */
	u16 cfpctr;	/* Common FIFO Pointer Control Register */
	u16 coffset;	/* Channel Data Bitrate Configuration Register */
	u16 rfoffset;	/* Receive FIFO buffer access ID register */
	u16 cfoffset;	/* Transmit/receive FIFO buffer access ID register */
};

struct rcar_canfd_shift_data {
	u8 ntseg2;	/* Nominal Bit Rate Time Segment 2 Control */
	u8 ntseg1;	/* Nominal Bit Rate Time Segment 1 Control */
	u8 nsjw;	/* Nominal Bit Rate Resynchronization Jump Width Control */
	u8 dtseg2;	/* Data Bit Rate Time Segment 2 Control */
	u8 dtseg1;	/* Data Bit Rate Time Segment 1 Control */
	u8 cftml;	/* Common FIFO TX Message Buffer Link */
	u8 cfm;		/* Common FIFO Mode */
	u8 cfdc;	/* Common FIFO Depth Configuration */
};

struct rcar_canfd_hw_info {
	const struct can_bittiming_const *nom_bittiming;
	const struct can_bittiming_const *data_bittiming;
	const struct can_tdc_const *tdc_const;
	const struct rcar_canfd_regs *regs;
	const struct rcar_canfd_shift_data *sh;
	u8 rnc_field_width;
	u8 max_aflpn;
	u8 max_cftml;
	u8 max_channels;
	u8 postdiv;
	/* hardware features */
	unsigned shared_global_irqs:1;	/* Has shared global irqs */
	unsigned multi_channel_irqs:1;	/* Has multiple channel irqs */
	unsigned ch_interface_mode:1;	/* Has channel interface mode */
	unsigned shared_can_regs:1;	/* Has shared classical can registers */
	unsigned external_clk:1;	/* Has external clock */
};

/* Channel priv data */
struct rcar_canfd_channel {
	struct can_priv can;			/* Must be the first member */
	struct net_device *ndev;
	struct rcar_canfd_global *gpriv;	/* Controller reference */
	void __iomem *base;			/* Register base address */
	struct phy *transceiver;		/* Optional transceiver */
	struct napi_struct napi;
	u32 tx_head;				/* Incremented on xmit */
	u32 tx_tail;				/* Incremented on xmit done */
	u32 channel;				/* Channel number */
	spinlock_t tx_lock;			/* To protect tx path */
};

/* Global priv data */
struct rcar_canfd_global {
	struct rcar_canfd_channel *ch[RCANFD_NUM_CHANNELS];
	void __iomem *base;		/* Register base address */
	struct rcar_canfd_f_c __iomem *fcbase;
	struct platform_device *pdev;	/* Respective platform device */
	struct clk *clkp;		/* Peripheral clock */
	struct clk *can_clk;		/* fCAN clock */
	unsigned long channels_mask;	/* Enabled channels mask */
	bool extclk;			/* CANFD or Ext clock */
	bool fdmode;			/* CAN FD or Classical CAN only mode */
	struct reset_control *rstc1;
	struct reset_control *rstc2;
	const struct rcar_canfd_hw_info *info;
};

/* CAN FD mode nominal rate constants */
static const struct can_bittiming_const rcar_canfd_gen3_nom_bittiming_const = {
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

static const struct can_bittiming_const rcar_canfd_gen4_nom_bittiming_const = {
	.name = RCANFD_DRV_NAME,
	.tseg1_min = 2,
	.tseg1_max = 256,
	.tseg2_min = 2,
	.tseg2_max = 128,
	.sjw_max = 128,
	.brp_min = 1,
	.brp_max = 1024,
	.brp_inc = 1,
};

/* CAN FD mode data rate constants */
static const struct can_bittiming_const rcar_canfd_gen3_data_bittiming_const = {
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

static const struct can_bittiming_const rcar_canfd_gen4_data_bittiming_const = {
	.name = RCANFD_DRV_NAME,
	.tseg1_min = 2,
	.tseg1_max = 32,
	.tseg2_min = 2,
	.tseg2_max = 16,
	.sjw_max = 16,
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

/* CAN FD Transmission Delay Compensation constants */
static const struct can_tdc_const rcar_canfd_gen3_tdc_const = {
	.tdcv_min = 1,
	.tdcv_max = 128,
	.tdco_min = 1,
	.tdco_max = 128,
	.tdcf_min = 0,	/* Filter window not supported */
	.tdcf_max = 0,
};

static const struct can_tdc_const rcar_canfd_gen4_tdc_const = {
	.tdcv_min = 1,
	.tdcv_max = 256,
	.tdco_min = 1,
	.tdco_max = 256,
	.tdcf_min = 0,	/* Filter window not supported */
	.tdcf_max = 0,
};

static const struct rcar_canfd_regs rcar_gen3_regs = {
	.rfcc = 0x00b8,
	.cfcc = 0x0118,
	.cfsts = 0x0178,
	.cfpctr = 0x01d8,
	.coffset = 0x0500,
	.rfoffset = 0x3000,
	.cfoffset = 0x3400,
};

static const struct rcar_canfd_regs rcar_gen4_regs = {
	.rfcc = 0x00c0,
	.cfcc = 0x0120,
	.cfsts = 0x01e0,
	.cfpctr = 0x0240,
	.coffset = 0x1400,
	.rfoffset = 0x6000,
	.cfoffset = 0x6400,
};

static const struct rcar_canfd_shift_data rcar_gen3_shift_data = {
	.ntseg2 = 24,
	.ntseg1 = 16,
	.nsjw = 11,
	.dtseg2 = 20,
	.dtseg1 = 16,
	.cftml = 20,
	.cfm = 16,
	.cfdc = 8,
};

static const struct rcar_canfd_shift_data rcar_gen4_shift_data = {
	.ntseg2 = 25,
	.ntseg1 = 17,
	.nsjw = 10,
	.dtseg2 = 16,
	.dtseg1 = 8,
	.cftml = 16,
	.cfm = 8,
	.cfdc = 21,
};

static const struct rcar_canfd_hw_info rcar_gen3_hw_info = {
	.nom_bittiming = &rcar_canfd_gen3_nom_bittiming_const,
	.data_bittiming = &rcar_canfd_gen3_data_bittiming_const,
	.tdc_const = &rcar_canfd_gen3_tdc_const,
	.regs = &rcar_gen3_regs,
	.sh = &rcar_gen3_shift_data,
	.rnc_field_width = 8,
	.max_aflpn = 31,
	.max_cftml = 15,
	.max_channels = 2,
	.postdiv = 2,
	.shared_global_irqs = 1,
	.ch_interface_mode = 0,
	.shared_can_regs = 0,
	.external_clk = 1,
};

static const struct rcar_canfd_hw_info rcar_gen4_hw_info = {
	.nom_bittiming = &rcar_canfd_gen4_nom_bittiming_const,
	.data_bittiming = &rcar_canfd_gen4_data_bittiming_const,
	.tdc_const = &rcar_canfd_gen4_tdc_const,
	.regs = &rcar_gen4_regs,
	.sh = &rcar_gen4_shift_data,
	.rnc_field_width = 16,
	.max_aflpn = 127,
	.max_cftml = 31,
	.max_channels = 8,
	.postdiv = 2,
	.shared_global_irqs = 1,
	.ch_interface_mode = 1,
	.shared_can_regs = 1,
	.external_clk = 1,
};

static const struct rcar_canfd_hw_info rzg2l_hw_info = {
	.nom_bittiming = &rcar_canfd_gen3_nom_bittiming_const,
	.data_bittiming = &rcar_canfd_gen3_data_bittiming_const,
	.tdc_const = &rcar_canfd_gen3_tdc_const,
	.regs = &rcar_gen3_regs,
	.sh = &rcar_gen3_shift_data,
	.rnc_field_width = 8,
	.max_aflpn = 31,
	.max_cftml = 15,
	.max_channels = 2,
	.postdiv = 1,
	.multi_channel_irqs = 1,
	.ch_interface_mode = 0,
	.shared_can_regs = 0,
	.external_clk = 1,
};

static const struct rcar_canfd_hw_info r9a09g047_hw_info = {
	.nom_bittiming = &rcar_canfd_gen4_nom_bittiming_const,
	.data_bittiming = &rcar_canfd_gen4_data_bittiming_const,
	.tdc_const = &rcar_canfd_gen4_tdc_const,
	.regs = &rcar_gen4_regs,
	.sh = &rcar_gen4_shift_data,
	.rnc_field_width = 16,
	.max_aflpn = 63,
	.max_cftml = 31,
	.max_channels = 6,
	.postdiv = 1,
	.multi_channel_irqs = 1,
	.ch_interface_mode = 1,
	.shared_can_regs = 1,
	.external_clk = 0,
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
	return readl(base + offset);
}

static inline void rcar_canfd_write(void __iomem *base, u32 offset, u32 val)
{
	writel(val, base + offset);
}

static void rcar_canfd_set_bit(void __iomem *base, u32 reg, u32 val)
{
	rcar_canfd_update(val, val, base + reg);
}

static void rcar_canfd_clear_bit(void __iomem *base, u32 reg, u32 val)
{
	rcar_canfd_update(val, 0, base + reg);
}

static void rcar_canfd_update_bit(void __iomem *base, u32 reg,
				  u32 mask, u32 val)
{
	rcar_canfd_update(mask, val, base + reg);
}

static void rcar_canfd_set_bit_reg(void __iomem *addr, u32 val)
{
	rcar_canfd_update(val, val, addr);
}

static void rcar_canfd_clear_bit_reg(void __iomem *addr, u32 val)
{
	rcar_canfd_update(val, 0, addr);
}

static void rcar_canfd_update_bit_reg(void __iomem *addr, u32 mask, u32 val)
{
	rcar_canfd_update(mask, val, addr);
}

static void rcar_canfd_get_data(struct rcar_canfd_channel *priv,
				struct canfd_frame *cf, u32 off)
{
	u32 *data = (u32 *)cf->data;
	u32 i, lwords;

	lwords = DIV_ROUND_UP(cf->len, sizeof(u32));
	for (i = 0; i < lwords; i++)
		data[i] = rcar_canfd_read(priv->base, off + i * sizeof(u32));
}

static void rcar_canfd_put_data(struct rcar_canfd_channel *priv,
				struct canfd_frame *cf, u32 off)
{
	const u32 *data = (u32 *)cf->data;
	u32 i, lwords;

	lwords = DIV_ROUND_UP(cf->len, sizeof(u32));
	for (i = 0; i < lwords; i++)
		rcar_canfd_write(priv->base, off + i * sizeof(u32), data[i]);
}

static void rcar_canfd_tx_failure_cleanup(struct net_device *ndev)
{
	u32 i;

	for (i = 0; i < RCANFD_FIFO_DEPTH; i++)
		can_free_echo_skb(ndev, i, NULL);
}

static void rcar_canfd_set_rnc(struct rcar_canfd_global *gpriv, unsigned int ch,
			       unsigned int num_rules)
{
	unsigned int rnc_stride = 32 / gpriv->info->rnc_field_width;
	unsigned int shift = 32 - (ch % rnc_stride + 1) * gpriv->info->rnc_field_width;
	unsigned int w = ch / rnc_stride;
	u32 rnc = num_rules << shift;

	rcar_canfd_set_bit(gpriv->base, RCANFD_GAFLCFG(w), rnc);
}

static int rcar_canfd_reset_controller(struct rcar_canfd_global *gpriv)
{
	struct device *dev = &gpriv->pdev->dev;
	u32 sts, ch;
	int err;

	/* Check RAMINIT flag as CAN RAM initialization takes place
	 * after the MCU reset
	 */
	err = readl_poll_timeout((gpriv->base + RCANFD_GSTS), sts,
				 !(sts & RCANFD_GSTS_GRAMINIT), 2, 500000);
	if (err) {
		dev_dbg(dev, "global raminit failed\n");
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
		dev_dbg(dev, "global reset failed\n");
		return err;
	}

	/* Reset Global error flags */
	rcar_canfd_write(gpriv->base, RCANFD_GERFL, 0x0);

	/* Set the controller into appropriate mode */
	if (!gpriv->info->ch_interface_mode) {
		if (gpriv->fdmode)
			rcar_canfd_set_bit(gpriv->base, RCANFD_GRMCFG,
					   RCANFD_GRMCFG_RCMC);
		else
			rcar_canfd_clear_bit(gpriv->base, RCANFD_GRMCFG,
					     RCANFD_GRMCFG_RCMC);
	}

	/* Transition all Channels to reset mode */
	for_each_set_bit(ch, &gpriv->channels_mask, gpriv->info->max_channels) {
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
			dev_dbg(dev, "channel %u reset failed\n", ch);
			return err;
		}

		/* Set the controller into appropriate mode */
		if (gpriv->info->ch_interface_mode) {
			/* Do not set CLOE and FDOE simultaneously */
			if (!gpriv->fdmode) {
				rcar_canfd_clear_bit_reg(&gpriv->fcbase[ch].cfdcfg,
							 RCANFD_GEN4_FDCFG_FDOE);
				rcar_canfd_set_bit_reg(&gpriv->fcbase[ch].cfdcfg,
						       RCANFD_GEN4_FDCFG_CLOE);
			} else {
				rcar_canfd_clear_bit_reg(&gpriv->fcbase[ch].cfdcfg,
							 RCANFD_GEN4_FDCFG_FDOE);
				rcar_canfd_clear_bit_reg(&gpriv->fcbase[ch].cfdcfg,
							 RCANFD_GEN4_FDCFG_CLOE);
			}
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
	if (gpriv->extclk)
		cfg |= RCANFD_GCFG_DCS;

	rcar_canfd_set_bit(gpriv->base, RCANFD_GCFG, cfg);

	/* Channel configuration settings */
	for_each_set_bit(ch, &gpriv->channels_mask, gpriv->info->max_channels) {
		rcar_canfd_set_bit(gpriv->base, RCANFD_CCTR(ch),
				   RCANFD_CCTR_ERRD);
		rcar_canfd_update_bit(gpriv->base, RCANFD_CCTR(ch),
				      RCANFD_CCTR_BOM_MASK,
				      RCANFD_CCTR_BOM_BENTRY);
	}
}

static void rcar_canfd_configure_afl_rules(struct rcar_canfd_global *gpriv,
					   u32 ch, u32 rule_entry)
{
	unsigned int offset, page, num_rules = RCANFD_CHANNEL_NUMRULES;
	u32 rule_entry_index = rule_entry % 16;
	u32 ridx = ch + RCANFD_RFFIFO_IDX;

	/* Enable write access to entry */
	page = RCANFD_GAFL_PAGENUM(rule_entry);
	rcar_canfd_set_bit(gpriv->base, RCANFD_GAFLECTR,
			   (RCANFD_GAFLECTR_AFLPN(gpriv, page) |
			    RCANFD_GAFLECTR_AFLDAE));

	/* Write number of rules for channel */
	rcar_canfd_set_rnc(gpriv, ch, num_rules);
	if (gpriv->info->shared_can_regs)
		offset = RCANFD_GEN4_GAFL_OFFSET;
	else if (gpriv->fdmode)
		offset = RCANFD_F_GAFL_OFFSET;
	else
		offset = RCANFD_C_GAFL_OFFSET;

	/* Accept all IDs */
	rcar_canfd_write(gpriv->base, RCANFD_GAFLID(offset, rule_entry_index), 0);
	/* IDE or RTR is not considered for matching */
	rcar_canfd_write(gpriv->base, RCANFD_GAFLM(offset, rule_entry_index), 0);
	/* Any data length accepted */
	rcar_canfd_write(gpriv->base, RCANFD_GAFLP0(offset, rule_entry_index), 0);
	/* Place the msg in corresponding Rx FIFO entry */
	rcar_canfd_set_bit(gpriv->base, RCANFD_GAFLP1(offset, rule_entry_index),
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
	rcar_canfd_write(gpriv->base, RCANFD_RFCC(gpriv, ridx), cfg);
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

	cfg = (RCANFD_CFCC_CFTML(gpriv, cftml) | RCANFD_CFCC_CFM(gpriv, cfm) |
		RCANFD_CFCC_CFIM | RCANFD_CFCC_CFDC(gpriv, cfdc) |
		RCANFD_CFCC_CFPLS(cfpls) | RCANFD_CFCC_CFTXIE);
	rcar_canfd_write(gpriv->base, RCANFD_CFCC(gpriv, ch, RCANFD_CFFIFO_IDX), cfg);

	if (gpriv->fdmode)
		/* Clear FD mode specific control/status register */
		rcar_canfd_write(gpriv->base,
				 RCANFD_F_CFFDCSTS(gpriv, ch, RCANFD_CFFIFO_IDX), 0);
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
	if (gerfl & FIELD_PREP(RCANFD_GERFL_EEF, BIT(ch))) {
		netdev_dbg(ndev, "Ch%u: ECC Error flag\n", ch);
		stats->tx_dropped++;
	}
	if (gerfl & RCANFD_GERFL_MES) {
		sts = rcar_canfd_read(priv->base,
				      RCANFD_CFSTS(gpriv, ch, RCANFD_CFFIFO_IDX));
		if (sts & RCANFD_CFSTS_CFMLT) {
			netdev_dbg(ndev, "Tx Message Lost flag\n");
			stats->tx_dropped++;
			rcar_canfd_write(priv->base,
					 RCANFD_CFSTS(gpriv, ch, RCANFD_CFFIFO_IDX),
					 sts & ~RCANFD_CFSTS_CFMLT);
		}

		sts = rcar_canfd_read(priv->base, RCANFD_RFSTS(gpriv, ridx));
		if (sts & RCANFD_RFSTS_RFMLT) {
			netdev_dbg(ndev, "Rx Message Lost flag\n");
			stats->rx_dropped++;
			rcar_canfd_write(priv->base, RCANFD_RFSTS(gpriv, ridx),
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
		cf->can_id |= CAN_ERR_CRTL | CAN_ERR_CNT;
		cf->data[1] = txerr > rxerr ? CAN_ERR_CRTL_TX_WARNING :
			CAN_ERR_CRTL_RX_WARNING;
		cf->data[6] = txerr;
		cf->data[7] = rxerr;
	}
	if (cerfl & RCANFD_CERFL_EPF) {
		netdev_dbg(ndev, "Error passive interrupt\n");
		priv->can.state = CAN_STATE_ERROR_PASSIVE;
		priv->can.can_stats.error_passive++;
		cf->can_id |= CAN_ERR_CRTL | CAN_ERR_CNT;
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
	netif_rx(skb);
}

static void rcar_canfd_tx_done(struct net_device *ndev)
{
	struct rcar_canfd_channel *priv = netdev_priv(ndev);
	struct rcar_canfd_global *gpriv = priv->gpriv;
	struct net_device_stats *stats = &ndev->stats;
	u32 sts;
	unsigned long flags;
	u32 ch = priv->channel;

	do {
		u8 unsent, sent;

		sent = priv->tx_tail % RCANFD_FIFO_DEPTH;
		stats->tx_packets++;
		stats->tx_bytes += can_get_echo_skb(ndev, sent, NULL);

		spin_lock_irqsave(&priv->tx_lock, flags);
		priv->tx_tail++;
		sts = rcar_canfd_read(priv->base,
				      RCANFD_CFSTS(gpriv, ch, RCANFD_CFFIFO_IDX));
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
	rcar_canfd_write(priv->base, RCANFD_CFSTS(gpriv, ch, RCANFD_CFFIFO_IDX),
			 sts & ~RCANFD_CFSTS_CFTXIF);
}

static void rcar_canfd_handle_global_err(struct rcar_canfd_global *gpriv, u32 ch)
{
	struct rcar_canfd_channel *priv = gpriv->ch[ch];
	struct net_device *ndev = priv->ndev;
	u32 gerfl;

	/* Handle global error interrupts */
	gerfl = rcar_canfd_read(priv->base, RCANFD_GERFL);
	if (unlikely(RCANFD_GERFL_ERR(gpriv, gerfl)))
		rcar_canfd_global_error(ndev);
}

static irqreturn_t rcar_canfd_global_err_interrupt(int irq, void *dev_id)
{
	struct rcar_canfd_global *gpriv = dev_id;
	u32 ch;

	for_each_set_bit(ch, &gpriv->channels_mask, gpriv->info->max_channels)
		rcar_canfd_handle_global_err(gpriv, ch);

	return IRQ_HANDLED;
}

static void rcar_canfd_handle_global_receive(struct rcar_canfd_global *gpriv, u32 ch)
{
	struct rcar_canfd_channel *priv = gpriv->ch[ch];
	u32 ridx = ch + RCANFD_RFFIFO_IDX;
	u32 sts, cc;

	/* Handle Rx interrupts */
	sts = rcar_canfd_read(priv->base, RCANFD_RFSTS(gpriv, ridx));
	cc = rcar_canfd_read(priv->base, RCANFD_RFCC(gpriv, ridx));
	if (likely(sts & RCANFD_RFSTS_RFIF &&
		   cc & RCANFD_RFCC_RFIE)) {
		if (napi_schedule_prep(&priv->napi)) {
			/* Disable Rx FIFO interrupts */
			rcar_canfd_clear_bit(priv->base,
					     RCANFD_RFCC(gpriv, ridx),
					     RCANFD_RFCC_RFIE);
			__napi_schedule(&priv->napi);
		}
	}
}

static irqreturn_t rcar_canfd_global_receive_fifo_interrupt(int irq, void *dev_id)
{
	struct rcar_canfd_global *gpriv = dev_id;
	u32 ch;

	for_each_set_bit(ch, &gpriv->channels_mask, gpriv->info->max_channels)
		rcar_canfd_handle_global_receive(gpriv, ch);

	return IRQ_HANDLED;
}

static irqreturn_t rcar_canfd_global_interrupt(int irq, void *dev_id)
{
	struct rcar_canfd_global *gpriv = dev_id;
	u32 ch;

	/* Global error interrupts still indicate a condition specific
	 * to a channel. RxFIFO interrupt is a global interrupt.
	 */
	for_each_set_bit(ch, &gpriv->channels_mask, gpriv->info->max_channels) {
		rcar_canfd_handle_global_err(gpriv, ch);
		rcar_canfd_handle_global_receive(gpriv, ch);
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
		netif_rx(skb);
	}
}

static void rcar_canfd_handle_channel_tx(struct rcar_canfd_global *gpriv, u32 ch)
{
	struct rcar_canfd_channel *priv = gpriv->ch[ch];
	struct net_device *ndev = priv->ndev;
	u32 sts;

	/* Handle Tx interrupts */
	sts = rcar_canfd_read(priv->base,
			      RCANFD_CFSTS(gpriv, ch, RCANFD_CFFIFO_IDX));
	if (likely(sts & RCANFD_CFSTS_CFTXIF))
		rcar_canfd_tx_done(ndev);
}

static irqreturn_t rcar_canfd_channel_tx_interrupt(int irq, void *dev_id)
{
	struct rcar_canfd_channel *priv = dev_id;

	rcar_canfd_handle_channel_tx(priv->gpriv, priv->channel);

	return IRQ_HANDLED;
}

static void rcar_canfd_handle_channel_err(struct rcar_canfd_global *gpriv, u32 ch)
{
	struct rcar_canfd_channel *priv = gpriv->ch[ch];
	struct net_device *ndev = priv->ndev;
	u16 txerr, rxerr;
	u32 sts, cerfl;

	/* Handle channel error interrupts */
	cerfl = rcar_canfd_read(priv->base, RCANFD_CERFL(ch));
	sts = rcar_canfd_read(priv->base, RCANFD_CSTS(ch));
	txerr = RCANFD_CSTS_TECCNT(sts);
	rxerr = RCANFD_CSTS_RECCNT(sts);
	if (unlikely(RCANFD_CERFL_ERR(cerfl)))
		rcar_canfd_error(ndev, cerfl, txerr, rxerr);

	/* Handle state change to lower states */
	if (unlikely(priv->can.state != CAN_STATE_ERROR_ACTIVE &&
		     priv->can.state != CAN_STATE_BUS_OFF))
		rcar_canfd_state_change(ndev, txerr, rxerr);
}

static irqreturn_t rcar_canfd_channel_err_interrupt(int irq, void *dev_id)
{
	struct rcar_canfd_channel *priv = dev_id;

	rcar_canfd_handle_channel_err(priv->gpriv, priv->channel);

	return IRQ_HANDLED;
}

static irqreturn_t rcar_canfd_channel_interrupt(int irq, void *dev_id)
{
	struct rcar_canfd_global *gpriv = dev_id;
	u32 ch;

	/* Common FIFO is a per channel resource */
	for_each_set_bit(ch, &gpriv->channels_mask, gpriv->info->max_channels) {
		rcar_canfd_handle_channel_err(gpriv, ch);
		rcar_canfd_handle_channel_tx(gpriv, ch);
	}

	return IRQ_HANDLED;
}

static inline u32 rcar_canfd_compute_nominal_bit_rate_cfg(struct rcar_canfd_channel *priv,
							  u16 tseg1, u16 tseg2, u16 sjw, u16 brp)
{
	struct rcar_canfd_global *gpriv = priv->gpriv;
	const struct rcar_canfd_hw_info *info = gpriv->info;
	u32 ntseg1, ntseg2, nsjw, nbrp;

	if ((priv->can.ctrlmode & CAN_CTRLMODE_FD) || gpriv->info->shared_can_regs) {
		ntseg1 = (tseg1 & (info->nom_bittiming->tseg1_max - 1)) << info->sh->ntseg1;
		ntseg2 = (tseg2 & (info->nom_bittiming->tseg2_max - 1)) << info->sh->ntseg2;
		nsjw = (sjw & (info->nom_bittiming->sjw_max - 1)) << info->sh->nsjw;
		nbrp = FIELD_PREP(RCANFD_NCFG_NBRP, brp);
	} else {
		ntseg1 = FIELD_PREP(RCANFD_CFG_TSEG1, tseg1);
		ntseg2 = FIELD_PREP(RCANFD_CFG_TSEG2, tseg2);
		nsjw = FIELD_PREP(RCANFD_CFG_SJW, sjw);
		nbrp = FIELD_PREP(RCANFD_CFG_BRP, brp);
	}

	return (ntseg1 | ntseg2 | nsjw | nbrp);
}

static inline u32 rcar_canfd_compute_data_bit_rate_cfg(const struct rcar_canfd_hw_info *info,
						       u16 tseg1, u16 tseg2, u16 sjw, u16 brp)
{
	u32 dtseg1, dtseg2, dsjw, dbrp;

	dtseg1 = (tseg1 & (info->data_bittiming->tseg1_max - 1)) << info->sh->dtseg1;
	dtseg2 = (tseg2 & (info->data_bittiming->tseg2_max - 1)) << info->sh->dtseg2;
	dsjw = (sjw & (info->data_bittiming->sjw_max - 1)) << 24;
	dbrp = FIELD_PREP(RCANFD_DCFG_DBRP, brp);

	return (dtseg1 | dtseg2 | dsjw | dbrp);
}

static void rcar_canfd_set_bittiming(struct net_device *ndev)
{
	u32 mask = RCANFD_FDCFG_TDCO | RCANFD_FDCFG_TDCE | RCANFD_FDCFG_TDCOC;
	struct rcar_canfd_channel *priv = netdev_priv(ndev);
	struct rcar_canfd_global *gpriv = priv->gpriv;
	const struct can_bittiming *bt = &priv->can.bittiming;
	const struct can_bittiming *dbt = &priv->can.fd.data_bittiming;
	const struct can_tdc_const *tdc_const = priv->can.fd.tdc_const;
	const struct can_tdc *tdc = &priv->can.fd.tdc;
	u32 cfg, tdcmode = 0, tdco = 0;
	u16 brp, sjw, tseg1, tseg2;
	u32 ch = priv->channel;

	/* Nominal bit timing settings */
	brp = bt->brp - 1;
	sjw = bt->sjw - 1;
	tseg1 = bt->prop_seg + bt->phase_seg1 - 1;
	tseg2 = bt->phase_seg2 - 1;
	cfg = rcar_canfd_compute_nominal_bit_rate_cfg(priv, tseg1, tseg2, sjw, brp);
	rcar_canfd_write(priv->base, RCANFD_CCFG(ch), cfg);

	if (!(priv->can.ctrlmode & CAN_CTRLMODE_FD))
		return;

	/* Data bit timing settings */
	brp = dbt->brp - 1;
	sjw = dbt->sjw - 1;
	tseg1 = dbt->prop_seg + dbt->phase_seg1 - 1;
	tseg2 = dbt->phase_seg2 - 1;
	cfg = rcar_canfd_compute_data_bit_rate_cfg(gpriv->info, tseg1, tseg2, sjw, brp);
	writel(cfg, &gpriv->fcbase[ch].dcfg);

	/* Transceiver Delay Compensation */
	if (priv->can.ctrlmode & CAN_CTRLMODE_TDC_AUTO) {
		/* TDC enabled, measured + offset */
		tdcmode = RCANFD_FDCFG_TDCE;
		tdco = tdc->tdco - 1;
	} else if (priv->can.ctrlmode & CAN_CTRLMODE_TDC_MANUAL) {
		/* TDC enabled, offset only */
		tdcmode = RCANFD_FDCFG_TDCE | RCANFD_FDCFG_TDCOC;
		tdco = min(tdc->tdcv + tdc->tdco, tdc_const->tdco_max) - 1;
	}

	rcar_canfd_update_bit_reg(&gpriv->fcbase[ch].cfdcfg, mask,
				  tdcmode | FIELD_PREP(RCANFD_FDCFG_TDCO, tdco));
}

static int rcar_canfd_start(struct net_device *ndev)
{
	struct rcar_canfd_channel *priv = netdev_priv(ndev);
	struct rcar_canfd_global *gpriv = priv->gpriv;
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
	rcar_canfd_set_bit(priv->base, RCANFD_CFCC(gpriv, ch, RCANFD_CFFIFO_IDX),
			   RCANFD_CFCC_CFE);
	rcar_canfd_set_bit(priv->base, RCANFD_RFCC(gpriv, ridx), RCANFD_RFCC_RFE);

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

	err = phy_power_on(priv->transceiver);
	if (err) {
		netdev_err(ndev, "failed to power on PHY: %pe\n", ERR_PTR(err));
		return err;
	}

	/* Peripheral clock is already enabled in probe */
	err = clk_prepare_enable(gpriv->can_clk);
	if (err) {
		netdev_err(ndev, "failed to enable CAN clock: %pe\n", ERR_PTR(err));
		goto out_phy;
	}

	err = open_candev(ndev);
	if (err) {
		netdev_err(ndev, "open_candev() failed: %pe\n", ERR_PTR(err));
		goto out_can_clock;
	}

	napi_enable(&priv->napi);
	err = rcar_canfd_start(ndev);
	if (err)
		goto out_close;
	netif_start_queue(ndev);
	return 0;
out_close:
	napi_disable(&priv->napi);
	close_candev(ndev);
out_can_clock:
	clk_disable_unprepare(gpriv->can_clk);
out_phy:
	phy_power_off(priv->transceiver);
	return err;
}

static void rcar_canfd_stop(struct net_device *ndev)
{
	struct rcar_canfd_channel *priv = netdev_priv(ndev);
	struct rcar_canfd_global *gpriv = priv->gpriv;
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
	rcar_canfd_clear_bit(priv->base, RCANFD_CFCC(gpriv, ch, RCANFD_CFFIFO_IDX),
			     RCANFD_CFCC_CFE);
	rcar_canfd_clear_bit(priv->base, RCANFD_RFCC(gpriv, ridx), RCANFD_RFCC_RFE);

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
	phy_power_off(priv->transceiver);
	return 0;
}

static netdev_tx_t rcar_canfd_start_xmit(struct sk_buff *skb,
					 struct net_device *ndev)
{
	struct rcar_canfd_channel *priv = netdev_priv(ndev);
	struct rcar_canfd_global *gpriv = priv->gpriv;
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	u32 sts = 0, id, dlc;
	unsigned long flags;
	u32 ch = priv->channel;

	if (can_dev_dropped_skb(ndev, skb))
		return NETDEV_TX_OK;

	if (cf->can_id & CAN_EFF_FLAG) {
		id = cf->can_id & CAN_EFF_MASK;
		id |= RCANFD_CFID_CFIDE;
	} else {
		id = cf->can_id & CAN_SFF_MASK;
	}

	if (cf->can_id & CAN_RTR_FLAG)
		id |= RCANFD_CFID_CFRTR;

	dlc = RCANFD_CFPTR_CFDLC(can_fd_len2dlc(cf->len));

	if ((priv->can.ctrlmode & CAN_CTRLMODE_FD) || gpriv->info->shared_can_regs) {
		rcar_canfd_write(priv->base,
				 RCANFD_F_CFID(gpriv, ch, RCANFD_CFFIFO_IDX), id);
		rcar_canfd_write(priv->base,
				 RCANFD_F_CFPTR(gpriv, ch, RCANFD_CFFIFO_IDX), dlc);

		if (can_is_canfd_skb(skb)) {
			/* CAN FD frame format */
			sts |= RCANFD_CFFDCSTS_CFFDF;
			if (cf->flags & CANFD_BRS)
				sts |= RCANFD_CFFDCSTS_CFBRS;

			if (priv->can.state == CAN_STATE_ERROR_PASSIVE)
				sts |= RCANFD_CFFDCSTS_CFESI;
		}

		rcar_canfd_write(priv->base,
				 RCANFD_F_CFFDCSTS(gpriv, ch, RCANFD_CFFIFO_IDX), sts);

		rcar_canfd_put_data(priv, cf,
				    RCANFD_F_CFDF(gpriv, ch, RCANFD_CFFIFO_IDX, 0));
	} else {
		rcar_canfd_write(priv->base,
				 RCANFD_C_CFID(ch, RCANFD_CFFIFO_IDX), id);
		rcar_canfd_write(priv->base,
				 RCANFD_C_CFPTR(ch, RCANFD_CFFIFO_IDX), dlc);
		rcar_canfd_put_data(priv, cf,
				    RCANFD_C_CFDF(ch, RCANFD_CFFIFO_IDX, 0));
	}

	can_put_echo_skb(skb, ndev, priv->tx_head % RCANFD_FIFO_DEPTH, 0);

	spin_lock_irqsave(&priv->tx_lock, flags);
	priv->tx_head++;

	/* Stop the queue if we've filled all FIFO entries */
	if (priv->tx_head - priv->tx_tail >= RCANFD_FIFO_DEPTH)
		netif_stop_queue(ndev);

	/* Start Tx: Write 0xff to CFPC to increment the CPU-side
	 * pointer for the Common FIFO
	 */
	rcar_canfd_write(priv->base,
			 RCANFD_CFPCTR(gpriv, ch, RCANFD_CFFIFO_IDX), 0xff);

	spin_unlock_irqrestore(&priv->tx_lock, flags);
	return NETDEV_TX_OK;
}

static void rcar_canfd_rx_pkt(struct rcar_canfd_channel *priv)
{
	struct net_device *ndev = priv->ndev;
	struct net_device_stats *stats = &ndev->stats;
	struct rcar_canfd_global *gpriv = priv->gpriv;
	struct canfd_frame *cf;
	struct sk_buff *skb;
	u32 sts = 0, id, dlc;
	u32 ch = priv->channel;
	u32 ridx = ch + RCANFD_RFFIFO_IDX;

	if ((priv->can.ctrlmode & CAN_CTRLMODE_FD) || gpriv->info->shared_can_regs) {
		id = rcar_canfd_read(priv->base, RCANFD_F_RFID(gpriv, ridx));
		dlc = rcar_canfd_read(priv->base, RCANFD_F_RFPTR(gpriv, ridx));

		sts = rcar_canfd_read(priv->base, RCANFD_F_RFFDSTS(gpriv, ridx));

		if ((priv->can.ctrlmode & CAN_CTRLMODE_FD) &&
		    sts & RCANFD_RFFDSTS_RFFDF)
			skb = alloc_canfd_skb(ndev, &cf);
		else
			skb = alloc_can_skb(ndev, (struct can_frame **)&cf);
	} else {
		id = rcar_canfd_read(priv->base, RCANFD_C_RFID(ridx));
		dlc = rcar_canfd_read(priv->base, RCANFD_C_RFPTR(ridx));
		skb = alloc_can_skb(ndev, (struct can_frame **)&cf);
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
			cf->len = can_fd_dlc2len(RCANFD_RFPTR_RFDLC(dlc));
		else
			cf->len = can_cc_dlc2len(RCANFD_RFPTR_RFDLC(dlc));

		if (sts & RCANFD_RFFDSTS_RFESI) {
			cf->flags |= CANFD_ESI;
			netdev_dbg(ndev, "ESI Error\n");
		}

		if (!(sts & RCANFD_RFFDSTS_RFFDF) && (id & RCANFD_RFID_RFRTR)) {
			cf->can_id |= CAN_RTR_FLAG;
		} else {
			if (sts & RCANFD_RFFDSTS_RFBRS)
				cf->flags |= CANFD_BRS;

			rcar_canfd_get_data(priv, cf, RCANFD_F_RFDF(gpriv, ridx, 0));
		}
	} else {
		cf->len = can_cc_dlc2len(RCANFD_RFPTR_RFDLC(dlc));
		if (id & RCANFD_RFID_RFRTR)
			cf->can_id |= CAN_RTR_FLAG;
		else if (gpriv->info->shared_can_regs)
			rcar_canfd_get_data(priv, cf, RCANFD_F_RFDF(gpriv, ridx, 0));
		else
			rcar_canfd_get_data(priv, cf, RCANFD_C_RFDF(ridx, 0));
	}

	/* Write 0xff to RFPC to increment the CPU-side
	 * pointer of the Rx FIFO
	 */
	rcar_canfd_write(priv->base, RCANFD_RFPCTR(gpriv, ridx), 0xff);

	if (!(cf->can_id & CAN_RTR_FLAG))
		stats->rx_bytes += cf->len;
	stats->rx_packets++;
	netif_receive_skb(skb);
}

static int rcar_canfd_rx_poll(struct napi_struct *napi, int quota)
{
	struct rcar_canfd_channel *priv =
		container_of(napi, struct rcar_canfd_channel, napi);
	struct rcar_canfd_global *gpriv = priv->gpriv;
	int num_pkts;
	u32 sts;
	u32 ch = priv->channel;
	u32 ridx = ch + RCANFD_RFFIFO_IDX;

	for (num_pkts = 0; num_pkts < quota; num_pkts++) {
		sts = rcar_canfd_read(priv->base, RCANFD_RFSTS(gpriv, ridx));
		/* Check FIFO empty condition */
		if (sts & RCANFD_RFSTS_RFEMP)
			break;

		rcar_canfd_rx_pkt(priv);

		/* Clear interrupt bit */
		if (sts & RCANFD_RFSTS_RFIF)
			rcar_canfd_write(priv->base, RCANFD_RFSTS(gpriv, ridx),
					 sts & ~RCANFD_RFSTS_RFIF);
	}

	/* All packets processed */
	if (num_pkts < quota) {
		if (napi_complete_done(napi, num_pkts)) {
			/* Enable Rx FIFO interrupts */
			rcar_canfd_set_bit(priv->base, RCANFD_RFCC(gpriv, ridx),
					   RCANFD_RFCC_RFIE);
		}
	}
	return num_pkts;
}

static unsigned int rcar_canfd_get_tdcr(struct rcar_canfd_global *gpriv,
					unsigned int ch)
{
	u32 sts = readl(&gpriv->fcbase[ch].cfdsts);
	u32 tdcr = FIELD_GET(RCANFD_FDSTS_TDCR, sts);

	return tdcr & (gpriv->info->tdc_const->tdcv_max - 1);
}

static int rcar_canfd_get_auto_tdcv(const struct net_device *ndev, u32 *tdcv)
{
	struct rcar_canfd_channel *priv = netdev_priv(ndev);
	u32 tdco = priv->can.fd.tdc.tdco;
	u32 tdcr;

	/* Transceiver Delay Compensation Result */
	tdcr = rcar_canfd_get_tdcr(priv->gpriv, priv->channel) + 1;

	*tdcv = tdcr < tdco ? 0 : tdcr - tdco;

	return 0;
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

static int rcar_canfd_get_berr_counter(const struct net_device *ndev,
				       struct can_berr_counter *bec)
{
	struct rcar_canfd_channel *priv = netdev_priv(ndev);
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

static const struct ethtool_ops rcar_canfd_ethtool_ops = {
	.get_ts_info = ethtool_op_get_ts_info,
};

static int rcar_canfd_channel_probe(struct rcar_canfd_global *gpriv, u32 ch,
				    u32 fcan_freq, struct phy *transceiver)
{
	const struct rcar_canfd_hw_info *info = gpriv->info;
	struct platform_device *pdev = gpriv->pdev;
	struct device *dev = &pdev->dev;
	struct rcar_canfd_channel *priv;
	struct net_device *ndev;
	int err = -ENODEV;

	ndev = alloc_candev(sizeof(*priv), RCANFD_FIFO_DEPTH);
	if (!ndev)
		return -ENOMEM;

	priv = netdev_priv(ndev);

	ndev->netdev_ops = &rcar_canfd_netdev_ops;
	ndev->ethtool_ops = &rcar_canfd_ethtool_ops;
	ndev->flags |= IFF_ECHO;
	priv->ndev = ndev;
	priv->base = gpriv->base;
	priv->transceiver = transceiver;
	priv->channel = ch;
	priv->gpriv = gpriv;
	if (transceiver)
		priv->can.bitrate_max = transceiver->attrs.max_link_rate;
	priv->can.clock.freq = fcan_freq;
	dev_info(dev, "can_clk rate is %u\n", priv->can.clock.freq);

	if (info->multi_channel_irqs) {
		char *irq_name;
		char name[10];
		int err_irq;
		int tx_irq;

		scnprintf(name, sizeof(name), "ch%u_err", ch);
		err_irq = platform_get_irq_byname(pdev, name);
		if (err_irq < 0) {
			err = err_irq;
			goto fail;
		}

		scnprintf(name, sizeof(name), "ch%u_trx", ch);
		tx_irq = platform_get_irq_byname(pdev, name);
		if (tx_irq < 0) {
			err = tx_irq;
			goto fail;
		}

		irq_name = devm_kasprintf(dev, GFP_KERNEL, "canfd.ch%d_err",
					  ch);
		if (!irq_name) {
			err = -ENOMEM;
			goto fail;
		}
		err = devm_request_irq(dev, err_irq,
				       rcar_canfd_channel_err_interrupt, 0,
				       irq_name, priv);
		if (err) {
			dev_err(dev, "devm_request_irq CH Err %d failed: %pe\n",
				err_irq, ERR_PTR(err));
			goto fail;
		}
		irq_name = devm_kasprintf(dev, GFP_KERNEL, "canfd.ch%d_trx",
					  ch);
		if (!irq_name) {
			err = -ENOMEM;
			goto fail;
		}
		err = devm_request_irq(dev, tx_irq,
				       rcar_canfd_channel_tx_interrupt, 0,
				       irq_name, priv);
		if (err) {
			dev_err(dev, "devm_request_irq Tx %d failed: %pe\n",
				tx_irq, ERR_PTR(err));
			goto fail;
		}
	}

	if (gpriv->fdmode) {
		priv->can.bittiming_const = gpriv->info->nom_bittiming;
		priv->can.fd.data_bittiming_const = gpriv->info->data_bittiming;
		priv->can.fd.tdc_const = gpriv->info->tdc_const;

		/* Controller starts in CAN FD only mode */
		err = can_set_static_ctrlmode(ndev, CAN_CTRLMODE_FD);
		if (err)
			goto fail;

		priv->can.ctrlmode_supported = CAN_CTRLMODE_BERR_REPORTING |
					       CAN_CTRLMODE_TDC_AUTO |
					       CAN_CTRLMODE_TDC_MANUAL;
		priv->can.fd.do_get_auto_tdcv = rcar_canfd_get_auto_tdcv;
	} else {
		/* Controller starts in Classical CAN only mode */
		if (gpriv->info->shared_can_regs)
			priv->can.bittiming_const = gpriv->info->nom_bittiming;
		else
			priv->can.bittiming_const = &rcar_canfd_bittiming_const;
		priv->can.ctrlmode_supported = CAN_CTRLMODE_BERR_REPORTING;
	}

	priv->can.do_set_mode = rcar_canfd_do_set_mode;
	priv->can.do_get_berr_counter = rcar_canfd_get_berr_counter;
	SET_NETDEV_DEV(ndev, dev);

	netif_napi_add_weight(ndev, &priv->napi, rcar_canfd_rx_poll,
			      RCANFD_NAPI_WEIGHT);
	spin_lock_init(&priv->tx_lock);
	gpriv->ch[priv->channel] = priv;
	err = register_candev(ndev);
	if (err) {
		dev_err(dev, "register_candev() failed: %pe\n", ERR_PTR(err));
		goto fail_candev;
	}
	dev_info(dev, "device registered (channel %u)\n", priv->channel);
	return 0;

fail_candev:
	netif_napi_del(&priv->napi);
fail:
	free_candev(ndev);
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
	struct phy *transceivers[RCANFD_NUM_CHANNELS] = { NULL, };
	const struct rcar_canfd_hw_info *info;
	struct device *dev = &pdev->dev;
	void __iomem *addr;
	u32 sts, ch, fcan_freq;
	struct rcar_canfd_global *gpriv;
	struct device_node *of_child;
	unsigned long channels_mask = 0;
	int err, ch_irq, g_irq;
	int g_err_irq, g_recc_irq;
	u32 rule_entry = 0;
	bool fdmode = true;			/* CAN FD only mode - default */
	char name[9] = "channelX";
	struct clk *clk_ram;
	int i;

	info = of_device_get_match_data(dev);

	if (of_property_read_bool(dev->of_node, "renesas,no-can-fd"))
		fdmode = false;			/* Classical CAN only mode */

	for (i = 0; i < info->max_channels; ++i) {
		name[7] = '0' + i;
		of_child = of_get_available_child_by_name(dev->of_node, name);
		if (of_child) {
			channels_mask |= BIT(i);
			transceivers[i] = devm_of_phy_optional_get(dev,
							of_child, NULL);
			of_node_put(of_child);
		}
		if (IS_ERR(transceivers[i]))
			return PTR_ERR(transceivers[i]);
	}

	if (info->shared_global_irqs) {
		ch_irq = platform_get_irq_byname_optional(pdev, "ch_int");
		if (ch_irq < 0) {
			/* For backward compatibility get irq by index */
			ch_irq = platform_get_irq(pdev, 0);
			if (ch_irq < 0)
				return ch_irq;
		}

		g_irq = platform_get_irq_byname_optional(pdev, "g_int");
		if (g_irq < 0) {
			/* For backward compatibility get irq by index */
			g_irq = platform_get_irq(pdev, 1);
			if (g_irq < 0)
				return g_irq;
		}
	} else {
		g_err_irq = platform_get_irq_byname(pdev, "g_err");
		if (g_err_irq < 0)
			return g_err_irq;

		g_recc_irq = platform_get_irq_byname(pdev, "g_recc");
		if (g_recc_irq < 0)
			return g_recc_irq;
	}

	/* Global controller context */
	gpriv = devm_kzalloc(dev, sizeof(*gpriv), GFP_KERNEL);
	if (!gpriv)
		return -ENOMEM;

	gpriv->pdev = pdev;
	gpriv->channels_mask = channels_mask;
	gpriv->fdmode = fdmode;
	gpriv->info = info;

	gpriv->rstc1 = devm_reset_control_get_optional_exclusive(dev, "rstp_n");
	if (IS_ERR(gpriv->rstc1))
		return dev_err_probe(dev, PTR_ERR(gpriv->rstc1),
				     "failed to get rstp_n\n");

	gpriv->rstc2 = devm_reset_control_get_optional_exclusive(dev, "rstc_n");
	if (IS_ERR(gpriv->rstc2))
		return dev_err_probe(dev, PTR_ERR(gpriv->rstc2),
				     "failed to get rstc_n\n");

	/* Peripheral clock */
	gpriv->clkp = devm_clk_get(dev, "fck");
	if (IS_ERR(gpriv->clkp))
		return dev_err_probe(dev, PTR_ERR(gpriv->clkp),
				     "cannot get peripheral clock\n");

	/* fCAN clock: Pick External clock. If not available fallback to
	 * CANFD clock
	 */
	gpriv->can_clk = devm_clk_get(dev, "can_clk");
	if (IS_ERR(gpriv->can_clk) || (clk_get_rate(gpriv->can_clk) == 0)) {
		gpriv->can_clk = devm_clk_get(dev, "canfd");
		if (IS_ERR(gpriv->can_clk))
			return dev_err_probe(dev, PTR_ERR(gpriv->can_clk),
					     "cannot get canfd clock\n");

		/* CANFD clock may be further divided within the IP */
		fcan_freq = clk_get_rate(gpriv->can_clk) / info->postdiv;
	} else {
		fcan_freq = clk_get_rate(gpriv->can_clk);
		gpriv->extclk = gpriv->info->external_clk;
	}

	clk_ram = devm_clk_get_optional_enabled(dev, "ram_clk");
	if (IS_ERR(clk_ram))
		return dev_err_probe(dev, PTR_ERR(clk_ram),
				     "cannot get enabled ram clock\n");

	addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(addr)) {
		err = PTR_ERR(addr);
		goto fail_dev;
	}
	gpriv->base = addr;
	gpriv->fcbase = addr + gpriv->info->regs->coffset;

	/* Request IRQ that's common for both channels */
	if (info->shared_global_irqs) {
		err = devm_request_irq(dev, ch_irq,
				       rcar_canfd_channel_interrupt, 0,
				       "canfd.ch_int", gpriv);
		if (err) {
			dev_err(dev, "devm_request_irq %d failed: %pe\n",
				ch_irq, ERR_PTR(err));
			goto fail_dev;
		}

		err = devm_request_irq(dev, g_irq, rcar_canfd_global_interrupt,
				       0, "canfd.g_int", gpriv);
		if (err) {
			dev_err(dev, "devm_request_irq %d failed: %pe\n",
				g_irq, ERR_PTR(err));
			goto fail_dev;
		}
	} else {
		err = devm_request_irq(dev, g_recc_irq,
				       rcar_canfd_global_receive_fifo_interrupt, 0,
				       "canfd.g_recc", gpriv);

		if (err) {
			dev_err(dev, "devm_request_irq %d failed: %pe\n",
				g_recc_irq, ERR_PTR(err));
			goto fail_dev;
		}

		err = devm_request_irq(dev, g_err_irq,
				       rcar_canfd_global_err_interrupt, 0,
				       "canfd.g_err", gpriv);
		if (err) {
			dev_err(dev, "devm_request_irq %d failed: %pe\n",
				g_err_irq, ERR_PTR(err));
			goto fail_dev;
		}
	}

	err = reset_control_reset(gpriv->rstc1);
	if (err)
		goto fail_dev;
	err = reset_control_reset(gpriv->rstc2);
	if (err) {
		reset_control_assert(gpriv->rstc1);
		goto fail_dev;
	}

	/* Enable peripheral clock for register access */
	err = clk_prepare_enable(gpriv->clkp);
	if (err) {
		dev_err(dev, "failed to enable peripheral clock: %pe\n",
			ERR_PTR(err));
		goto fail_reset;
	}

	err = rcar_canfd_reset_controller(gpriv);
	if (err) {
		dev_err(dev, "reset controller failed: %pe\n", ERR_PTR(err));
		goto fail_clk;
	}

	/* Controller in Global reset & Channel reset mode */
	rcar_canfd_configure_controller(gpriv);

	/* Configure per channel attributes */
	for_each_set_bit(ch, &gpriv->channels_mask, info->max_channels) {
		/* Configure Channel's Rx fifo */
		rcar_canfd_configure_rx(gpriv, ch);

		/* Configure Channel's Tx (Common) fifo */
		rcar_canfd_configure_tx(gpriv, ch);

		/* Configure receive rules */
		rcar_canfd_configure_afl_rules(gpriv, ch, rule_entry);
		rule_entry += RCANFD_CHANNEL_NUMRULES;
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
		dev_err(dev, "global operational mode failed\n");
		goto fail_mode;
	}

	for_each_set_bit(ch, &gpriv->channels_mask, info->max_channels) {
		err = rcar_canfd_channel_probe(gpriv, ch, fcan_freq,
					       transceivers[ch]);
		if (err)
			goto fail_channel;
	}

	platform_set_drvdata(pdev, gpriv);
	dev_info(dev, "global operational state (%s clk, %s mode)\n",
		 gpriv->extclk ? "ext" : "canfd",
		 gpriv->fdmode ? "fd" : "classical");
	return 0;

fail_channel:
	for_each_set_bit(ch, &gpriv->channels_mask, info->max_channels)
		rcar_canfd_channel_remove(gpriv, ch);
fail_mode:
	rcar_canfd_disable_global_interrupts(gpriv);
fail_clk:
	clk_disable_unprepare(gpriv->clkp);
fail_reset:
	reset_control_assert(gpriv->rstc1);
	reset_control_assert(gpriv->rstc2);
fail_dev:
	return err;
}

static void rcar_canfd_remove(struct platform_device *pdev)
{
	struct rcar_canfd_global *gpriv = platform_get_drvdata(pdev);
	u32 ch;

	rcar_canfd_reset_controller(gpriv);
	rcar_canfd_disable_global_interrupts(gpriv);

	for_each_set_bit(ch, &gpriv->channels_mask, gpriv->info->max_channels) {
		rcar_canfd_disable_channel_interrupts(gpriv->ch[ch]);
		rcar_canfd_channel_remove(gpriv, ch);
	}

	/* Enter global sleep mode */
	rcar_canfd_set_bit(gpriv->base, RCANFD_GCTR, RCANFD_GCTR_GSLPR);
	clk_disable_unprepare(gpriv->clkp);
	reset_control_assert(gpriv->rstc1);
	reset_control_assert(gpriv->rstc2);
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

static const __maybe_unused struct of_device_id rcar_canfd_of_table[] = {
	{ .compatible = "renesas,r8a779a0-canfd", .data = &rcar_gen4_hw_info },
	{ .compatible = "renesas,r9a09g047-canfd", .data = &r9a09g047_hw_info },
	{ .compatible = "renesas,rcar-gen3-canfd", .data = &rcar_gen3_hw_info },
	{ .compatible = "renesas,rcar-gen4-canfd", .data = &rcar_gen4_hw_info },
	{ .compatible = "renesas,rzg2l-canfd", .data = &rzg2l_hw_info },
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
