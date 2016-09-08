/*
 * hw.h - DesignWare HS OTG Controller hardware definitions
 *
 * Copyright 2004-2013 Synopsys, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __DWC2_HW_H__
#define __DWC2_HW_H__

#define HSOTG_REG(x)	(x)

#define GOTGCTL				HSOTG_REG(0x000)
#define GOTGCTL_CHIRPEN			(1 << 27)
#define GOTGCTL_MULT_VALID_BC_MASK	(0x1f << 22)
#define GOTGCTL_MULT_VALID_BC_SHIFT	22
#define GOTGCTL_OTGVER			(1 << 20)
#define GOTGCTL_BSESVLD			(1 << 19)
#define GOTGCTL_ASESVLD			(1 << 18)
#define GOTGCTL_DBNC_SHORT		(1 << 17)
#define GOTGCTL_CONID_B			(1 << 16)
#define GOTGCTL_DBNCE_FLTR_BYPASS	(1 << 15)
#define GOTGCTL_DEVHNPEN		(1 << 11)
#define GOTGCTL_HSTSETHNPEN		(1 << 10)
#define GOTGCTL_HNPREQ			(1 << 9)
#define GOTGCTL_HSTNEGSCS		(1 << 8)
#define GOTGCTL_SESREQ			(1 << 1)
#define GOTGCTL_SESREQSCS		(1 << 0)

#define GOTGINT				HSOTG_REG(0x004)
#define GOTGINT_DBNCE_DONE		(1 << 19)
#define GOTGINT_A_DEV_TOUT_CHG		(1 << 18)
#define GOTGINT_HST_NEG_DET		(1 << 17)
#define GOTGINT_HST_NEG_SUC_STS_CHNG	(1 << 9)
#define GOTGINT_SES_REQ_SUC_STS_CHNG	(1 << 8)
#define GOTGINT_SES_END_DET		(1 << 2)

#define GAHBCFG				HSOTG_REG(0x008)
#define GAHBCFG_AHB_SINGLE		(1 << 23)
#define GAHBCFG_NOTI_ALL_DMA_WRIT	(1 << 22)
#define GAHBCFG_REM_MEM_SUPP		(1 << 21)
#define GAHBCFG_P_TXF_EMP_LVL		(1 << 8)
#define GAHBCFG_NP_TXF_EMP_LVL		(1 << 7)
#define GAHBCFG_DMA_EN			(1 << 5)
#define GAHBCFG_HBSTLEN_MASK		(0xf << 1)
#define GAHBCFG_HBSTLEN_SHIFT		1
#define GAHBCFG_HBSTLEN_SINGLE		0
#define GAHBCFG_HBSTLEN_INCR		1
#define GAHBCFG_HBSTLEN_INCR4		3
#define GAHBCFG_HBSTLEN_INCR8		5
#define GAHBCFG_HBSTLEN_INCR16		7
#define GAHBCFG_GLBL_INTR_EN		(1 << 0)
#define GAHBCFG_CTRL_MASK		(GAHBCFG_P_TXF_EMP_LVL | \
					 GAHBCFG_NP_TXF_EMP_LVL | \
					 GAHBCFG_DMA_EN | \
					 GAHBCFG_GLBL_INTR_EN)

#define GUSBCFG				HSOTG_REG(0x00C)
#define GUSBCFG_FORCEDEVMODE		(1 << 30)
#define GUSBCFG_FORCEHOSTMODE		(1 << 29)
#define GUSBCFG_TXENDDELAY		(1 << 28)
#define GUSBCFG_ICTRAFFICPULLREMOVE	(1 << 27)
#define GUSBCFG_ICUSBCAP		(1 << 26)
#define GUSBCFG_ULPI_INT_PROT_DIS	(1 << 25)
#define GUSBCFG_INDICATORPASSTHROUGH	(1 << 24)
#define GUSBCFG_INDICATORCOMPLEMENT	(1 << 23)
#define GUSBCFG_TERMSELDLPULSE		(1 << 22)
#define GUSBCFG_ULPI_INT_VBUS_IND	(1 << 21)
#define GUSBCFG_ULPI_EXT_VBUS_DRV	(1 << 20)
#define GUSBCFG_ULPI_CLK_SUSP_M		(1 << 19)
#define GUSBCFG_ULPI_AUTO_RES		(1 << 18)
#define GUSBCFG_ULPI_FS_LS		(1 << 17)
#define GUSBCFG_OTG_UTMI_FS_SEL		(1 << 16)
#define GUSBCFG_PHY_LP_CLK_SEL		(1 << 15)
#define GUSBCFG_USBTRDTIM_MASK		(0xf << 10)
#define GUSBCFG_USBTRDTIM_SHIFT		10
#define GUSBCFG_HNPCAP			(1 << 9)
#define GUSBCFG_SRPCAP			(1 << 8)
#define GUSBCFG_DDRSEL			(1 << 7)
#define GUSBCFG_PHYSEL			(1 << 6)
#define GUSBCFG_FSINTF			(1 << 5)
#define GUSBCFG_ULPI_UTMI_SEL		(1 << 4)
#define GUSBCFG_PHYIF16			(1 << 3)
#define GUSBCFG_PHYIF8			(0 << 3)
#define GUSBCFG_TOUTCAL_MASK		(0x7 << 0)
#define GUSBCFG_TOUTCAL_SHIFT		0
#define GUSBCFG_TOUTCAL_LIMIT		0x7
#define GUSBCFG_TOUTCAL(_x)		((_x) << 0)

#define GRSTCTL				HSOTG_REG(0x010)
#define GRSTCTL_AHBIDLE			(1 << 31)
#define GRSTCTL_DMAREQ			(1 << 30)
#define GRSTCTL_TXFNUM_MASK		(0x1f << 6)
#define GRSTCTL_TXFNUM_SHIFT		6
#define GRSTCTL_TXFNUM_LIMIT		0x1f
#define GRSTCTL_TXFNUM(_x)		((_x) << 6)
#define GRSTCTL_TXFFLSH			(1 << 5)
#define GRSTCTL_RXFFLSH			(1 << 4)
#define GRSTCTL_IN_TKNQ_FLSH		(1 << 3)
#define GRSTCTL_FRMCNTRRST		(1 << 2)
#define GRSTCTL_HSFTRST			(1 << 1)
#define GRSTCTL_CSFTRST			(1 << 0)

#define GINTSTS				HSOTG_REG(0x014)
#define GINTMSK				HSOTG_REG(0x018)
#define GINTSTS_WKUPINT			(1 << 31)
#define GINTSTS_SESSREQINT		(1 << 30)
#define GINTSTS_DISCONNINT		(1 << 29)
#define GINTSTS_CONIDSTSCHNG		(1 << 28)
#define GINTSTS_LPMTRANRCVD		(1 << 27)
#define GINTSTS_PTXFEMP			(1 << 26)
#define GINTSTS_HCHINT			(1 << 25)
#define GINTSTS_PRTINT			(1 << 24)
#define GINTSTS_RESETDET		(1 << 23)
#define GINTSTS_FET_SUSP		(1 << 22)
#define GINTSTS_INCOMPL_IP		(1 << 21)
#define GINTSTS_INCOMPL_SOOUT		(1 << 21)
#define GINTSTS_INCOMPL_SOIN		(1 << 20)
#define GINTSTS_OEPINT			(1 << 19)
#define GINTSTS_IEPINT			(1 << 18)
#define GINTSTS_EPMIS			(1 << 17)
#define GINTSTS_RESTOREDONE		(1 << 16)
#define GINTSTS_EOPF			(1 << 15)
#define GINTSTS_ISOUTDROP		(1 << 14)
#define GINTSTS_ENUMDONE		(1 << 13)
#define GINTSTS_USBRST			(1 << 12)
#define GINTSTS_USBSUSP			(1 << 11)
#define GINTSTS_ERLYSUSP		(1 << 10)
#define GINTSTS_I2CINT			(1 << 9)
#define GINTSTS_ULPI_CK_INT		(1 << 8)
#define GINTSTS_GOUTNAKEFF		(1 << 7)
#define GINTSTS_GINNAKEFF		(1 << 6)
#define GINTSTS_NPTXFEMP		(1 << 5)
#define GINTSTS_RXFLVL			(1 << 4)
#define GINTSTS_SOF			(1 << 3)
#define GINTSTS_OTGINT			(1 << 2)
#define GINTSTS_MODEMIS			(1 << 1)
#define GINTSTS_CURMODE_HOST		(1 << 0)

#define GRXSTSR				HSOTG_REG(0x01C)
#define GRXSTSP				HSOTG_REG(0x020)
#define GRXSTS_FN_MASK			(0x7f << 25)
#define GRXSTS_FN_SHIFT			25
#define GRXSTS_PKTSTS_MASK		(0xf << 17)
#define GRXSTS_PKTSTS_SHIFT		17
#define GRXSTS_PKTSTS_GLOBALOUTNAK	1
#define GRXSTS_PKTSTS_OUTRX		2
#define GRXSTS_PKTSTS_HCHIN		2
#define GRXSTS_PKTSTS_OUTDONE		3
#define GRXSTS_PKTSTS_HCHIN_XFER_COMP	3
#define GRXSTS_PKTSTS_SETUPDONE		4
#define GRXSTS_PKTSTS_DATATOGGLEERR	5
#define GRXSTS_PKTSTS_SETUPRX		6
#define GRXSTS_PKTSTS_HCHHALTED		7
#define GRXSTS_HCHNUM_MASK		(0xf << 0)
#define GRXSTS_HCHNUM_SHIFT		0
#define GRXSTS_DPID_MASK		(0x3 << 15)
#define GRXSTS_DPID_SHIFT		15
#define GRXSTS_BYTECNT_MASK		(0x7ff << 4)
#define GRXSTS_BYTECNT_SHIFT		4
#define GRXSTS_EPNUM_MASK		(0xf << 0)
#define GRXSTS_EPNUM_SHIFT		0

#define GRXFSIZ				HSOTG_REG(0x024)
#define GRXFSIZ_DEPTH_MASK		(0xffff << 0)
#define GRXFSIZ_DEPTH_SHIFT		0

#define GNPTXFSIZ			HSOTG_REG(0x028)
/* Use FIFOSIZE_* constants to access this register */

#define GNPTXSTS			HSOTG_REG(0x02C)
#define GNPTXSTS_NP_TXQ_TOP_MASK		(0x7f << 24)
#define GNPTXSTS_NP_TXQ_TOP_SHIFT		24
#define GNPTXSTS_NP_TXQ_SPC_AVAIL_MASK		(0xff << 16)
#define GNPTXSTS_NP_TXQ_SPC_AVAIL_SHIFT		16
#define GNPTXSTS_NP_TXQ_SPC_AVAIL_GET(_v)	(((_v) >> 16) & 0xff)
#define GNPTXSTS_NP_TXF_SPC_AVAIL_MASK		(0xffff << 0)
#define GNPTXSTS_NP_TXF_SPC_AVAIL_SHIFT		0
#define GNPTXSTS_NP_TXF_SPC_AVAIL_GET(_v)	(((_v) >> 0) & 0xffff)

#define GI2CCTL				HSOTG_REG(0x0030)
#define GI2CCTL_BSYDNE			(1 << 31)
#define GI2CCTL_RW			(1 << 30)
#define GI2CCTL_I2CDATSE0		(1 << 28)
#define GI2CCTL_I2CDEVADDR_MASK		(0x3 << 26)
#define GI2CCTL_I2CDEVADDR_SHIFT	26
#define GI2CCTL_I2CSUSPCTL		(1 << 25)
#define GI2CCTL_ACK			(1 << 24)
#define GI2CCTL_I2CEN			(1 << 23)
#define GI2CCTL_ADDR_MASK		(0x7f << 16)
#define GI2CCTL_ADDR_SHIFT		16
#define GI2CCTL_REGADDR_MASK		(0xff << 8)
#define GI2CCTL_REGADDR_SHIFT		8
#define GI2CCTL_RWDATA_MASK		(0xff << 0)
#define GI2CCTL_RWDATA_SHIFT		0

#define GPVNDCTL			HSOTG_REG(0x0034)
#define GGPIO				HSOTG_REG(0x0038)
#define GUID				HSOTG_REG(0x003c)
#define GSNPSID				HSOTG_REG(0x0040)
#define GHWCFG1				HSOTG_REG(0x0044)

#define GHWCFG2				HSOTG_REG(0x0048)
#define GHWCFG2_OTG_ENABLE_IC_USB		(1 << 31)
#define GHWCFG2_DEV_TOKEN_Q_DEPTH_MASK		(0x1f << 26)
#define GHWCFG2_DEV_TOKEN_Q_DEPTH_SHIFT		26
#define GHWCFG2_HOST_PERIO_TX_Q_DEPTH_MASK	(0x3 << 24)
#define GHWCFG2_HOST_PERIO_TX_Q_DEPTH_SHIFT	24
#define GHWCFG2_NONPERIO_TX_Q_DEPTH_MASK	(0x3 << 22)
#define GHWCFG2_NONPERIO_TX_Q_DEPTH_SHIFT	22
#define GHWCFG2_MULTI_PROC_INT			(1 << 20)
#define GHWCFG2_DYNAMIC_FIFO			(1 << 19)
#define GHWCFG2_PERIO_EP_SUPPORTED		(1 << 18)
#define GHWCFG2_NUM_HOST_CHAN_MASK		(0xf << 14)
#define GHWCFG2_NUM_HOST_CHAN_SHIFT		14
#define GHWCFG2_NUM_DEV_EP_MASK			(0xf << 10)
#define GHWCFG2_NUM_DEV_EP_SHIFT		10
#define GHWCFG2_FS_PHY_TYPE_MASK		(0x3 << 8)
#define GHWCFG2_FS_PHY_TYPE_SHIFT		8
#define GHWCFG2_FS_PHY_TYPE_NOT_SUPPORTED	0
#define GHWCFG2_FS_PHY_TYPE_DEDICATED		1
#define GHWCFG2_FS_PHY_TYPE_SHARED_UTMI		2
#define GHWCFG2_FS_PHY_TYPE_SHARED_ULPI		3
#define GHWCFG2_HS_PHY_TYPE_MASK		(0x3 << 6)
#define GHWCFG2_HS_PHY_TYPE_SHIFT		6
#define GHWCFG2_HS_PHY_TYPE_NOT_SUPPORTED	0
#define GHWCFG2_HS_PHY_TYPE_UTMI		1
#define GHWCFG2_HS_PHY_TYPE_ULPI		2
#define GHWCFG2_HS_PHY_TYPE_UTMI_ULPI		3
#define GHWCFG2_POINT2POINT			(1 << 5)
#define GHWCFG2_ARCHITECTURE_MASK		(0x3 << 3)
#define GHWCFG2_ARCHITECTURE_SHIFT		3
#define GHWCFG2_SLAVE_ONLY_ARCH			0
#define GHWCFG2_EXT_DMA_ARCH			1
#define GHWCFG2_INT_DMA_ARCH			2
#define GHWCFG2_OP_MODE_MASK			(0x7 << 0)
#define GHWCFG2_OP_MODE_SHIFT			0
#define GHWCFG2_OP_MODE_HNP_SRP_CAPABLE		0
#define GHWCFG2_OP_MODE_SRP_ONLY_CAPABLE	1
#define GHWCFG2_OP_MODE_NO_HNP_SRP_CAPABLE	2
#define GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE	3
#define GHWCFG2_OP_MODE_NO_SRP_CAPABLE_DEVICE	4
#define GHWCFG2_OP_MODE_SRP_CAPABLE_HOST	5
#define GHWCFG2_OP_MODE_NO_SRP_CAPABLE_HOST	6
#define GHWCFG2_OP_MODE_UNDEFINED		7

#define GHWCFG3				HSOTG_REG(0x004c)
#define GHWCFG3_DFIFO_DEPTH_MASK		(0xffff << 16)
#define GHWCFG3_DFIFO_DEPTH_SHIFT		16
#define GHWCFG3_OTG_LPM_EN			(1 << 15)
#define GHWCFG3_BC_SUPPORT			(1 << 14)
#define GHWCFG3_OTG_ENABLE_HSIC			(1 << 13)
#define GHWCFG3_ADP_SUPP			(1 << 12)
#define GHWCFG3_SYNCH_RESET_TYPE		(1 << 11)
#define GHWCFG3_OPTIONAL_FEATURES		(1 << 10)
#define GHWCFG3_VENDOR_CTRL_IF			(1 << 9)
#define GHWCFG3_I2C				(1 << 8)
#define GHWCFG3_OTG_FUNC			(1 << 7)
#define GHWCFG3_PACKET_SIZE_CNTR_WIDTH_MASK	(0x7 << 4)
#define GHWCFG3_PACKET_SIZE_CNTR_WIDTH_SHIFT	4
#define GHWCFG3_XFER_SIZE_CNTR_WIDTH_MASK	(0xf << 0)
#define GHWCFG3_XFER_SIZE_CNTR_WIDTH_SHIFT	0

#define GHWCFG4				HSOTG_REG(0x0050)
#define GHWCFG4_DESC_DMA_DYN			(1 << 31)
#define GHWCFG4_DESC_DMA			(1 << 30)
#define GHWCFG4_NUM_IN_EPS_MASK			(0xf << 26)
#define GHWCFG4_NUM_IN_EPS_SHIFT		26
#define GHWCFG4_DED_FIFO_EN			(1 << 25)
#define GHWCFG4_DED_FIFO_SHIFT		25
#define GHWCFG4_SESSION_END_FILT_EN		(1 << 24)
#define GHWCFG4_B_VALID_FILT_EN			(1 << 23)
#define GHWCFG4_A_VALID_FILT_EN			(1 << 22)
#define GHWCFG4_VBUS_VALID_FILT_EN		(1 << 21)
#define GHWCFG4_IDDIG_FILT_EN			(1 << 20)
#define GHWCFG4_NUM_DEV_MODE_CTRL_EP_MASK	(0xf << 16)
#define GHWCFG4_NUM_DEV_MODE_CTRL_EP_SHIFT	16
#define GHWCFG4_UTMI_PHY_DATA_WIDTH_MASK	(0x3 << 14)
#define GHWCFG4_UTMI_PHY_DATA_WIDTH_SHIFT	14
#define GHWCFG4_UTMI_PHY_DATA_WIDTH_8		0
#define GHWCFG4_UTMI_PHY_DATA_WIDTH_16		1
#define GHWCFG4_UTMI_PHY_DATA_WIDTH_8_OR_16	2
#define GHWCFG4_XHIBER				(1 << 7)
#define GHWCFG4_HIBER				(1 << 6)
#define GHWCFG4_MIN_AHB_FREQ			(1 << 5)
#define GHWCFG4_POWER_OPTIMIZ			(1 << 4)
#define GHWCFG4_NUM_DEV_PERIO_IN_EP_MASK	(0xf << 0)
#define GHWCFG4_NUM_DEV_PERIO_IN_EP_SHIFT	0

#define GLPMCFG				HSOTG_REG(0x0054)
#define GLPMCFG_INV_SEL_HSIC		(1 << 31)
#define GLPMCFG_HSIC_CONNECT		(1 << 30)
#define GLPMCFG_RETRY_COUNT_STS_MASK	(0x7 << 25)
#define GLPMCFG_RETRY_COUNT_STS_SHIFT	25
#define GLPMCFG_SEND_LPM		(1 << 24)
#define GLPMCFG_RETRY_COUNT_MASK	(0x7 << 21)
#define GLPMCFG_RETRY_COUNT_SHIFT	21
#define GLPMCFG_LPM_CHAN_INDEX_MASK	(0xf << 17)
#define GLPMCFG_LPM_CHAN_INDEX_SHIFT	17
#define GLPMCFG_SLEEP_STATE_RESUMEOK	(1 << 16)
#define GLPMCFG_PRT_SLEEP_STS		(1 << 15)
#define GLPMCFG_LPM_RESP_MASK		(0x3 << 13)
#define GLPMCFG_LPM_RESP_SHIFT		13
#define GLPMCFG_HIRD_THRES_MASK		(0x1f << 8)
#define GLPMCFG_HIRD_THRES_SHIFT	8
#define GLPMCFG_HIRD_THRES_EN			(0x10 << 8)
#define GLPMCFG_EN_UTMI_SLEEP		(1 << 7)
#define GLPMCFG_REM_WKUP_EN		(1 << 6)
#define GLPMCFG_HIRD_MASK		(0xf << 2)
#define GLPMCFG_HIRD_SHIFT		2
#define GLPMCFG_APPL_RESP		(1 << 1)
#define GLPMCFG_LPM_CAP_EN		(1 << 0)

#define GPWRDN				HSOTG_REG(0x0058)
#define GPWRDN_MULT_VAL_ID_BC_MASK	(0x1f << 24)
#define GPWRDN_MULT_VAL_ID_BC_SHIFT	24
#define GPWRDN_ADP_INT			(1 << 23)
#define GPWRDN_BSESSVLD			(1 << 22)
#define GPWRDN_IDSTS			(1 << 21)
#define GPWRDN_LINESTATE_MASK		(0x3 << 19)
#define GPWRDN_LINESTATE_SHIFT		19
#define GPWRDN_STS_CHGINT_MSK		(1 << 18)
#define GPWRDN_STS_CHGINT		(1 << 17)
#define GPWRDN_SRP_DET_MSK		(1 << 16)
#define GPWRDN_SRP_DET			(1 << 15)
#define GPWRDN_CONNECT_DET_MSK		(1 << 14)
#define GPWRDN_CONNECT_DET		(1 << 13)
#define GPWRDN_DISCONN_DET_MSK		(1 << 12)
#define GPWRDN_DISCONN_DET		(1 << 11)
#define GPWRDN_RST_DET_MSK		(1 << 10)
#define GPWRDN_RST_DET			(1 << 9)
#define GPWRDN_LNSTSCHG_MSK		(1 << 8)
#define GPWRDN_LNSTSCHG			(1 << 7)
#define GPWRDN_DIS_VBUS			(1 << 6)
#define GPWRDN_PWRDNSWTCH		(1 << 5)
#define GPWRDN_PWRDNRSTN		(1 << 4)
#define GPWRDN_PWRDNCLMP		(1 << 3)
#define GPWRDN_RESTORE			(1 << 2)
#define GPWRDN_PMUACTV			(1 << 1)
#define GPWRDN_PMUINTSEL		(1 << 0)

#define GDFIFOCFG			HSOTG_REG(0x005c)
#define GDFIFOCFG_EPINFOBASE_MASK	(0xffff << 16)
#define GDFIFOCFG_EPINFOBASE_SHIFT	16
#define GDFIFOCFG_GDFIFOCFG_MASK	(0xffff << 0)
#define GDFIFOCFG_GDFIFOCFG_SHIFT	0

#define ADPCTL				HSOTG_REG(0x0060)
#define ADPCTL_AR_MASK			(0x3 << 27)
#define ADPCTL_AR_SHIFT			27
#define ADPCTL_ADP_TMOUT_INT_MSK	(1 << 26)
#define ADPCTL_ADP_SNS_INT_MSK		(1 << 25)
#define ADPCTL_ADP_PRB_INT_MSK		(1 << 24)
#define ADPCTL_ADP_TMOUT_INT		(1 << 23)
#define ADPCTL_ADP_SNS_INT		(1 << 22)
#define ADPCTL_ADP_PRB_INT		(1 << 21)
#define ADPCTL_ADPENA			(1 << 20)
#define ADPCTL_ADPRES			(1 << 19)
#define ADPCTL_ENASNS			(1 << 18)
#define ADPCTL_ENAPRB			(1 << 17)
#define ADPCTL_RTIM_MASK		(0x7ff << 6)
#define ADPCTL_RTIM_SHIFT		6
#define ADPCTL_PRB_PER_MASK		(0x3 << 4)
#define ADPCTL_PRB_PER_SHIFT		4
#define ADPCTL_PRB_DELTA_MASK		(0x3 << 2)
#define ADPCTL_PRB_DELTA_SHIFT		2
#define ADPCTL_PRB_DSCHRG_MASK		(0x3 << 0)
#define ADPCTL_PRB_DSCHRG_SHIFT		0

#define HPTXFSIZ			HSOTG_REG(0x100)
/* Use FIFOSIZE_* constants to access this register */

#define DPTXFSIZN(_a)			HSOTG_REG(0x104 + (((_a) - 1) * 4))
/* Use FIFOSIZE_* constants to access this register */

/* These apply to the GNPTXFSIZ, HPTXFSIZ and DPTXFSIZN registers */
#define FIFOSIZE_DEPTH_MASK		(0xffff << 16)
#define FIFOSIZE_DEPTH_SHIFT		16
#define FIFOSIZE_STARTADDR_MASK		(0xffff << 0)
#define FIFOSIZE_STARTADDR_SHIFT	0
#define FIFOSIZE_DEPTH_GET(_x)		(((_x) >> 16) & 0xffff)

/* Device mode registers */

#define DCFG				HSOTG_REG(0x800)
#define DCFG_EPMISCNT_MASK		(0x1f << 18)
#define DCFG_EPMISCNT_SHIFT		18
#define DCFG_EPMISCNT_LIMIT		0x1f
#define DCFG_EPMISCNT(_x)		((_x) << 18)
#define DCFG_PERFRINT_MASK		(0x3 << 11)
#define DCFG_PERFRINT_SHIFT		11
#define DCFG_PERFRINT_LIMIT		0x3
#define DCFG_PERFRINT(_x)		((_x) << 11)
#define DCFG_DEVADDR_MASK		(0x7f << 4)
#define DCFG_DEVADDR_SHIFT		4
#define DCFG_DEVADDR_LIMIT		0x7f
#define DCFG_DEVADDR(_x)		((_x) << 4)
#define DCFG_NZ_STS_OUT_HSHK		(1 << 2)
#define DCFG_DEVSPD_MASK		(0x3 << 0)
#define DCFG_DEVSPD_SHIFT		0
#define DCFG_DEVSPD_HS			0
#define DCFG_DEVSPD_FS			1
#define DCFG_DEVSPD_LS			2
#define DCFG_DEVSPD_FS48		3

#define DCTL				HSOTG_REG(0x804)
#define DCTL_PWRONPRGDONE		(1 << 11)
#define DCTL_CGOUTNAK			(1 << 10)
#define DCTL_SGOUTNAK			(1 << 9)
#define DCTL_CGNPINNAK			(1 << 8)
#define DCTL_SGNPINNAK			(1 << 7)
#define DCTL_TSTCTL_MASK		(0x7 << 4)
#define DCTL_TSTCTL_SHIFT		4
#define DCTL_GOUTNAKSTS			(1 << 3)
#define DCTL_GNPINNAKSTS		(1 << 2)
#define DCTL_SFTDISCON			(1 << 1)
#define DCTL_RMTWKUPSIG			(1 << 0)

#define DSTS				HSOTG_REG(0x808)
#define DSTS_SOFFN_MASK			(0x3fff << 8)
#define DSTS_SOFFN_SHIFT		8
#define DSTS_SOFFN_LIMIT		0x3fff
#define DSTS_SOFFN(_x)			((_x) << 8)
#define DSTS_ERRATICERR			(1 << 3)
#define DSTS_ENUMSPD_MASK		(0x3 << 1)
#define DSTS_ENUMSPD_SHIFT		1
#define DSTS_ENUMSPD_HS			0
#define DSTS_ENUMSPD_FS			1
#define DSTS_ENUMSPD_LS			2
#define DSTS_ENUMSPD_FS48		3
#define DSTS_SUSPSTS			(1 << 0)

#define DIEPMSK				HSOTG_REG(0x810)
#define DIEPMSK_NAKMSK			(1 << 13)
#define DIEPMSK_BNAININTRMSK		(1 << 9)
#define DIEPMSK_TXFIFOUNDRNMSK		(1 << 8)
#define DIEPMSK_TXFIFOEMPTY		(1 << 7)
#define DIEPMSK_INEPNAKEFFMSK		(1 << 6)
#define DIEPMSK_INTKNEPMISMSK		(1 << 5)
#define DIEPMSK_INTKNTXFEMPMSK		(1 << 4)
#define DIEPMSK_TIMEOUTMSK		(1 << 3)
#define DIEPMSK_AHBERRMSK		(1 << 2)
#define DIEPMSK_EPDISBLDMSK		(1 << 1)
#define DIEPMSK_XFERCOMPLMSK		(1 << 0)

#define DOEPMSK				HSOTG_REG(0x814)
#define DOEPMSK_BACK2BACKSETUP		(1 << 6)
#define DOEPMSK_STSPHSERCVDMSK		(1 << 5)
#define DOEPMSK_OUTTKNEPDISMSK		(1 << 4)
#define DOEPMSK_SETUPMSK		(1 << 3)
#define DOEPMSK_AHBERRMSK		(1 << 2)
#define DOEPMSK_EPDISBLDMSK		(1 << 1)
#define DOEPMSK_XFERCOMPLMSK		(1 << 0)

#define DAINT				HSOTG_REG(0x818)
#define DAINTMSK			HSOTG_REG(0x81C)
#define DAINT_OUTEP_SHIFT		16
#define DAINT_OUTEP(_x)			(1 << ((_x) + 16))
#define DAINT_INEP(_x)			(1 << (_x))

#define DTKNQR1				HSOTG_REG(0x820)
#define DTKNQR2				HSOTG_REG(0x824)
#define DTKNQR3				HSOTG_REG(0x830)
#define DTKNQR4				HSOTG_REG(0x834)
#define DIEPEMPMSK			HSOTG_REG(0x834)

#define DVBUSDIS			HSOTG_REG(0x828)
#define DVBUSPULSE			HSOTG_REG(0x82C)

#define DIEPCTL0			HSOTG_REG(0x900)
#define DIEPCTL(_a)			HSOTG_REG(0x900 + ((_a) * 0x20))

#define DOEPCTL0			HSOTG_REG(0xB00)
#define DOEPCTL(_a)			HSOTG_REG(0xB00 + ((_a) * 0x20))

/* EP0 specialness:
 * bits[29..28] - reserved (no SetD0PID, SetD1PID)
 * bits[25..22] - should always be zero, this isn't a periodic endpoint
 * bits[10..0]  - MPS setting different for EP0
 */
#define D0EPCTL_MPS_MASK		(0x3 << 0)
#define D0EPCTL_MPS_SHIFT		0
#define D0EPCTL_MPS_64			0
#define D0EPCTL_MPS_32			1
#define D0EPCTL_MPS_16			2
#define D0EPCTL_MPS_8			3

#define DXEPCTL_EPENA			(1 << 31)
#define DXEPCTL_EPDIS			(1 << 30)
#define DXEPCTL_SETD1PID		(1 << 29)
#define DXEPCTL_SETODDFR		(1 << 29)
#define DXEPCTL_SETD0PID		(1 << 28)
#define DXEPCTL_SETEVENFR		(1 << 28)
#define DXEPCTL_SNAK			(1 << 27)
#define DXEPCTL_CNAK			(1 << 26)
#define DXEPCTL_TXFNUM_MASK		(0xf << 22)
#define DXEPCTL_TXFNUM_SHIFT		22
#define DXEPCTL_TXFNUM_LIMIT		0xf
#define DXEPCTL_TXFNUM(_x)		((_x) << 22)
#define DXEPCTL_STALL			(1 << 21)
#define DXEPCTL_SNP			(1 << 20)
#define DXEPCTL_EPTYPE_MASK		(0x3 << 18)
#define DXEPCTL_EPTYPE_CONTROL		(0x0 << 18)
#define DXEPCTL_EPTYPE_ISO		(0x1 << 18)
#define DXEPCTL_EPTYPE_BULK		(0x2 << 18)
#define DXEPCTL_EPTYPE_INTERRUPT	(0x3 << 18)

#define DXEPCTL_NAKSTS			(1 << 17)
#define DXEPCTL_DPID			(1 << 16)
#define DXEPCTL_EOFRNUM			(1 << 16)
#define DXEPCTL_USBACTEP		(1 << 15)
#define DXEPCTL_NEXTEP_MASK		(0xf << 11)
#define DXEPCTL_NEXTEP_SHIFT		11
#define DXEPCTL_NEXTEP_LIMIT		0xf
#define DXEPCTL_NEXTEP(_x)		((_x) << 11)
#define DXEPCTL_MPS_MASK		(0x7ff << 0)
#define DXEPCTL_MPS_SHIFT		0
#define DXEPCTL_MPS_LIMIT		0x7ff
#define DXEPCTL_MPS(_x)			((_x) << 0)

#define DIEPINT(_a)			HSOTG_REG(0x908 + ((_a) * 0x20))
#define DOEPINT(_a)			HSOTG_REG(0xB08 + ((_a) * 0x20))
#define DXEPINT_SETUP_RCVD		(1 << 15)
#define DXEPINT_NYETINTRPT		(1 << 14)
#define DXEPINT_NAKINTRPT		(1 << 13)
#define DXEPINT_BBLEERRINTRPT		(1 << 12)
#define DXEPINT_PKTDRPSTS		(1 << 11)
#define DXEPINT_BNAINTR			(1 << 9)
#define DXEPINT_TXFIFOUNDRN		(1 << 8)
#define DXEPINT_OUTPKTERR		(1 << 8)
#define DXEPINT_TXFEMP			(1 << 7)
#define DXEPINT_INEPNAKEFF		(1 << 6)
#define DXEPINT_BACK2BACKSETUP		(1 << 6)
#define DXEPINT_INTKNEPMIS		(1 << 5)
#define DXEPINT_STSPHSERCVD		(1 << 5)
#define DXEPINT_INTKNTXFEMP		(1 << 4)
#define DXEPINT_OUTTKNEPDIS		(1 << 4)
#define DXEPINT_TIMEOUT			(1 << 3)
#define DXEPINT_SETUP			(1 << 3)
#define DXEPINT_AHBERR			(1 << 2)
#define DXEPINT_EPDISBLD		(1 << 1)
#define DXEPINT_XFERCOMPL		(1 << 0)

#define DIEPTSIZ0			HSOTG_REG(0x910)
#define DIEPTSIZ0_PKTCNT_MASK		(0x3 << 19)
#define DIEPTSIZ0_PKTCNT_SHIFT		19
#define DIEPTSIZ0_PKTCNT_LIMIT		0x3
#define DIEPTSIZ0_PKTCNT(_x)		((_x) << 19)
#define DIEPTSIZ0_XFERSIZE_MASK		(0x7f << 0)
#define DIEPTSIZ0_XFERSIZE_SHIFT	0
#define DIEPTSIZ0_XFERSIZE_LIMIT	0x7f
#define DIEPTSIZ0_XFERSIZE(_x)		((_x) << 0)

#define DOEPTSIZ0			HSOTG_REG(0xB10)
#define DOEPTSIZ0_SUPCNT_MASK		(0x3 << 29)
#define DOEPTSIZ0_SUPCNT_SHIFT		29
#define DOEPTSIZ0_SUPCNT_LIMIT		0x3
#define DOEPTSIZ0_SUPCNT(_x)		((_x) << 29)
#define DOEPTSIZ0_PKTCNT		(1 << 19)
#define DOEPTSIZ0_XFERSIZE_MASK		(0x7f << 0)
#define DOEPTSIZ0_XFERSIZE_SHIFT	0

#define DIEPTSIZ(_a)			HSOTG_REG(0x910 + ((_a) * 0x20))
#define DOEPTSIZ(_a)			HSOTG_REG(0xB10 + ((_a) * 0x20))
#define DXEPTSIZ_MC_MASK		(0x3 << 29)
#define DXEPTSIZ_MC_SHIFT		29
#define DXEPTSIZ_MC_LIMIT		0x3
#define DXEPTSIZ_MC(_x)			((_x) << 29)
#define DXEPTSIZ_PKTCNT_MASK		(0x3ff << 19)
#define DXEPTSIZ_PKTCNT_SHIFT		19
#define DXEPTSIZ_PKTCNT_LIMIT		0x3ff
#define DXEPTSIZ_PKTCNT_GET(_v)		(((_v) >> 19) & 0x3ff)
#define DXEPTSIZ_PKTCNT(_x)		((_x) << 19)
#define DXEPTSIZ_XFERSIZE_MASK		(0x7ffff << 0)
#define DXEPTSIZ_XFERSIZE_SHIFT		0
#define DXEPTSIZ_XFERSIZE_LIMIT		0x7ffff
#define DXEPTSIZ_XFERSIZE_GET(_v)	(((_v) >> 0) & 0x7ffff)
#define DXEPTSIZ_XFERSIZE(_x)		((_x) << 0)

#define DIEPDMA(_a)			HSOTG_REG(0x914 + ((_a) * 0x20))
#define DOEPDMA(_a)			HSOTG_REG(0xB14 + ((_a) * 0x20))

#define DTXFSTS(_a)			HSOTG_REG(0x918 + ((_a) * 0x20))

#define PCGCTL				HSOTG_REG(0x0e00)
#define PCGCTL_IF_DEV_MODE		(1 << 31)
#define PCGCTL_P2HD_PRT_SPD_MASK	(0x3 << 29)
#define PCGCTL_P2HD_PRT_SPD_SHIFT	29
#define PCGCTL_P2HD_DEV_ENUM_SPD_MASK	(0x3 << 27)
#define PCGCTL_P2HD_DEV_ENUM_SPD_SHIFT	27
#define PCGCTL_MAC_DEV_ADDR_MASK	(0x7f << 20)
#define PCGCTL_MAC_DEV_ADDR_SHIFT	20
#define PCGCTL_MAX_TERMSEL		(1 << 19)
#define PCGCTL_MAX_XCVRSELECT_MASK	(0x3 << 17)
#define PCGCTL_MAX_XCVRSELECT_SHIFT	17
#define PCGCTL_PORT_POWER		(1 << 16)
#define PCGCTL_PRT_CLK_SEL_MASK		(0x3 << 14)
#define PCGCTL_PRT_CLK_SEL_SHIFT	14
#define PCGCTL_ESS_REG_RESTORED		(1 << 13)
#define PCGCTL_EXTND_HIBER_SWITCH	(1 << 12)
#define PCGCTL_EXTND_HIBER_PWRCLMP	(1 << 11)
#define PCGCTL_ENBL_EXTND_HIBER		(1 << 10)
#define PCGCTL_RESTOREMODE		(1 << 9)
#define PCGCTL_RESETAFTSUSP		(1 << 8)
#define PCGCTL_DEEP_SLEEP		(1 << 7)
#define PCGCTL_PHY_IN_SLEEP		(1 << 6)
#define PCGCTL_ENBL_SLEEP_GATING	(1 << 5)
#define PCGCTL_RSTPDWNMODULE		(1 << 3)
#define PCGCTL_PWRCLMP			(1 << 2)
#define PCGCTL_GATEHCLK			(1 << 1)
#define PCGCTL_STOPPCLK			(1 << 0)

#define EPFIFO(_a)			HSOTG_REG(0x1000 + ((_a) * 0x1000))

/* Host Mode Registers */

#define HCFG				HSOTG_REG(0x0400)
#define HCFG_MODECHTIMEN		(1 << 31)
#define HCFG_PERSCHEDENA		(1 << 26)
#define HCFG_FRLISTEN_MASK		(0x3 << 24)
#define HCFG_FRLISTEN_SHIFT		24
#define HCFG_FRLISTEN_8				(0 << 24)
#define FRLISTEN_8_SIZE				8
#define HCFG_FRLISTEN_16			(1 << 24)
#define FRLISTEN_16_SIZE			16
#define HCFG_FRLISTEN_32			(2 << 24)
#define FRLISTEN_32_SIZE			32
#define HCFG_FRLISTEN_64			(3 << 24)
#define FRLISTEN_64_SIZE			64
#define HCFG_DESCDMA			(1 << 23)
#define HCFG_RESVALID_MASK		(0xff << 8)
#define HCFG_RESVALID_SHIFT		8
#define HCFG_ENA32KHZ			(1 << 7)
#define HCFG_FSLSSUPP			(1 << 2)
#define HCFG_FSLSPCLKSEL_MASK		(0x3 << 0)
#define HCFG_FSLSPCLKSEL_SHIFT		0
#define HCFG_FSLSPCLKSEL_30_60_MHZ	0
#define HCFG_FSLSPCLKSEL_48_MHZ		1
#define HCFG_FSLSPCLKSEL_6_MHZ		2

#define HFIR				HSOTG_REG(0x0404)
#define HFIR_FRINT_MASK			(0xffff << 0)
#define HFIR_FRINT_SHIFT		0
#define HFIR_RLDCTRL			(1 << 16)

#define HFNUM				HSOTG_REG(0x0408)
#define HFNUM_FRREM_MASK		(0xffff << 16)
#define HFNUM_FRREM_SHIFT		16
#define HFNUM_FRNUM_MASK		(0xffff << 0)
#define HFNUM_FRNUM_SHIFT		0
#define HFNUM_MAX_FRNUM			0x3fff

#define HPTXSTS				HSOTG_REG(0x0410)
#define TXSTS_QTOP_ODD			(1 << 31)
#define TXSTS_QTOP_CHNEP_MASK		(0xf << 27)
#define TXSTS_QTOP_CHNEP_SHIFT		27
#define TXSTS_QTOP_TOKEN_MASK		(0x3 << 25)
#define TXSTS_QTOP_TOKEN_SHIFT		25
#define TXSTS_QTOP_TERMINATE		(1 << 24)
#define TXSTS_QSPCAVAIL_MASK		(0xff << 16)
#define TXSTS_QSPCAVAIL_SHIFT		16
#define TXSTS_FSPCAVAIL_MASK		(0xffff << 0)
#define TXSTS_FSPCAVAIL_SHIFT		0

#define HAINT				HSOTG_REG(0x0414)
#define HAINTMSK			HSOTG_REG(0x0418)
#define HFLBADDR			HSOTG_REG(0x041c)

#define HPRT0				HSOTG_REG(0x0440)
#define HPRT0_SPD_MASK			(0x3 << 17)
#define HPRT0_SPD_SHIFT			17
#define HPRT0_SPD_HIGH_SPEED		0
#define HPRT0_SPD_FULL_SPEED		1
#define HPRT0_SPD_LOW_SPEED		2
#define HPRT0_TSTCTL_MASK		(0xf << 13)
#define HPRT0_TSTCTL_SHIFT		13
#define HPRT0_PWR			(1 << 12)
#define HPRT0_LNSTS_MASK		(0x3 << 10)
#define HPRT0_LNSTS_SHIFT		10
#define HPRT0_RST			(1 << 8)
#define HPRT0_SUSP			(1 << 7)
#define HPRT0_RES			(1 << 6)
#define HPRT0_OVRCURRCHG		(1 << 5)
#define HPRT0_OVRCURRACT		(1 << 4)
#define HPRT0_ENACHG			(1 << 3)
#define HPRT0_ENA			(1 << 2)
#define HPRT0_CONNDET			(1 << 1)
#define HPRT0_CONNSTS			(1 << 0)

#define HCCHAR(_ch)			HSOTG_REG(0x0500 + 0x20 * (_ch))
#define HCCHAR_CHENA			(1 << 31)
#define HCCHAR_CHDIS			(1 << 30)
#define HCCHAR_ODDFRM			(1 << 29)
#define HCCHAR_DEVADDR_MASK		(0x7f << 22)
#define HCCHAR_DEVADDR_SHIFT		22
#define HCCHAR_MULTICNT_MASK		(0x3 << 20)
#define HCCHAR_MULTICNT_SHIFT		20
#define HCCHAR_EPTYPE_MASK		(0x3 << 18)
#define HCCHAR_EPTYPE_SHIFT		18
#define HCCHAR_LSPDDEV			(1 << 17)
#define HCCHAR_EPDIR			(1 << 15)
#define HCCHAR_EPNUM_MASK		(0xf << 11)
#define HCCHAR_EPNUM_SHIFT		11
#define HCCHAR_MPS_MASK			(0x7ff << 0)
#define HCCHAR_MPS_SHIFT		0

#define HCSPLT(_ch)			HSOTG_REG(0x0504 + 0x20 * (_ch))
#define HCSPLT_SPLTENA			(1 << 31)
#define HCSPLT_COMPSPLT			(1 << 16)
#define HCSPLT_XACTPOS_MASK		(0x3 << 14)
#define HCSPLT_XACTPOS_SHIFT		14
#define HCSPLT_XACTPOS_MID		0
#define HCSPLT_XACTPOS_END		1
#define HCSPLT_XACTPOS_BEGIN		2
#define HCSPLT_XACTPOS_ALL		3
#define HCSPLT_HUBADDR_MASK		(0x7f << 7)
#define HCSPLT_HUBADDR_SHIFT		7
#define HCSPLT_PRTADDR_MASK		(0x7f << 0)
#define HCSPLT_PRTADDR_SHIFT		0

#define HCINT(_ch)			HSOTG_REG(0x0508 + 0x20 * (_ch))
#define HCINTMSK(_ch)			HSOTG_REG(0x050c + 0x20 * (_ch))
#define HCINTMSK_RESERVED14_31		(0x3ffff << 14)
#define HCINTMSK_FRM_LIST_ROLL		(1 << 13)
#define HCINTMSK_XCS_XACT		(1 << 12)
#define HCINTMSK_BNA			(1 << 11)
#define HCINTMSK_DATATGLERR		(1 << 10)
#define HCINTMSK_FRMOVRUN		(1 << 9)
#define HCINTMSK_BBLERR			(1 << 8)
#define HCINTMSK_XACTERR		(1 << 7)
#define HCINTMSK_NYET			(1 << 6)
#define HCINTMSK_ACK			(1 << 5)
#define HCINTMSK_NAK			(1 << 4)
#define HCINTMSK_STALL			(1 << 3)
#define HCINTMSK_AHBERR			(1 << 2)
#define HCINTMSK_CHHLTD			(1 << 1)
#define HCINTMSK_XFERCOMPL		(1 << 0)

#define HCTSIZ(_ch)			HSOTG_REG(0x0510 + 0x20 * (_ch))
#define TSIZ_DOPNG			(1 << 31)
#define TSIZ_SC_MC_PID_MASK		(0x3 << 29)
#define TSIZ_SC_MC_PID_SHIFT		29
#define TSIZ_SC_MC_PID_DATA0		0
#define TSIZ_SC_MC_PID_DATA2		1
#define TSIZ_SC_MC_PID_DATA1		2
#define TSIZ_SC_MC_PID_MDATA		3
#define TSIZ_SC_MC_PID_SETUP		3
#define TSIZ_PKTCNT_MASK		(0x3ff << 19)
#define TSIZ_PKTCNT_SHIFT		19
#define TSIZ_NTD_MASK			(0xff << 8)
#define TSIZ_NTD_SHIFT			8
#define TSIZ_SCHINFO_MASK		(0xff << 0)
#define TSIZ_SCHINFO_SHIFT		0
#define TSIZ_XFERSIZE_MASK		(0x7ffff << 0)
#define TSIZ_XFERSIZE_SHIFT		0

#define HCDMA(_ch)			HSOTG_REG(0x0514 + 0x20 * (_ch))

#define HCDMAB(_ch)			HSOTG_REG(0x051c + 0x20 * (_ch))

#define HCFIFO(_ch)			HSOTG_REG(0x1000 + 0x1000 * (_ch))

/**
 * struct dwc2_hcd_dma_desc - Host-mode DMA descriptor structure
 *
 * @status: DMA descriptor status quadlet
 * @buf:    DMA descriptor data buffer pointer
 *
 * DMA Descriptor structure contains two quadlets:
 * Status quadlet and Data buffer pointer.
 */
struct dwc2_hcd_dma_desc {
	u32 status;
	u32 buf;
};

#define HOST_DMA_A			(1 << 31)
#define HOST_DMA_STS_MASK		(0x3 << 28)
#define HOST_DMA_STS_SHIFT		28
#define HOST_DMA_STS_PKTERR		(1 << 28)
#define HOST_DMA_EOL			(1 << 26)
#define HOST_DMA_IOC			(1 << 25)
#define HOST_DMA_SUP			(1 << 24)
#define HOST_DMA_ALT_QTD		(1 << 23)
#define HOST_DMA_QTD_OFFSET_MASK	(0x3f << 17)
#define HOST_DMA_QTD_OFFSET_SHIFT	17
#define HOST_DMA_ISOC_NBYTES_MASK	(0xfff << 0)
#define HOST_DMA_ISOC_NBYTES_SHIFT	0
#define HOST_DMA_NBYTES_MASK		(0x1ffff << 0)
#define HOST_DMA_NBYTES_SHIFT		0

#define MAX_DMA_DESC_SIZE		131071
#define MAX_DMA_DESC_NUM_GENERIC	64
#define MAX_DMA_DESC_NUM_HS_ISOC	256

#endif /* __DWC2_HW_H__ */
