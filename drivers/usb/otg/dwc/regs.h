/*
 * DesignWare HS OTG controller driver
 * Copyright (C) 2006 Synopsys, Inc.
 * Portions Copyright (C) 2010 Applied Micro Circuits Corporation.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/licenses
 * or write to the Free Software Foundation, Inc., 51 Franklin Street,
 * Suite 500, Boston, MA 02110-1335 USA.
 *
 * Based on Synopsys driver version 2.60a
 * Modified by Mark Miesfeld <mmiesfeld@apm.com>
 *
 * Revamped register difinitions by Tirumala R Marri(tmarri@apm.com)
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING BUT NOT LIMITED TO THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL SYNOPSYS, INC. BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES
 * (INCLUDING BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef __DWC_OTG_REGS_H__
#define __DWC_OTG_REGS_H__

#include <linux/types.h>
/*Bit fields in the Device EP Transfer Size Register is 11 bits */
#undef DWC_LIMITED_XFER_SIZE
/*
 * This file contains the Macro defintions for accessing the DWC_otg core
 * registers.
 *
 * The application interfaces with the HS OTG core by reading from and
 * writing to the Control and Status Register (CSR) space through the
 * AHB Slave interface. These registers are 32 bits wide, and the
 * addresses are 32-bit-block aligned.
 * CSRs are classified as follows:
 * - Core Global Registers
 * - Device Mode Registers
 * - Device Global Registers
 * - Device Endpoint Specific Registers
 * - Host Mode Registers
 * - Host Global Registers
 * - Host Port CSRs
 * - Host Channel Specific Registers
 *
 * Only the Core Global registers can be accessed in both Device and
 * Host modes. When the HS OTG core is operating in one mode, either
 * Device or Host, the application must not access registers from the
 * other mode. When the core switches from one mode to another, the
 * registers in the new mode of operation must be reprogrammed as they
 * would be after a power-on reset.
 */

/*
 * DWC_otg Core registers.  The core_global_regs structure defines the
 * size and relative field offsets for the Core Global registers.
 */
#define	DWC_GOTGCTL		0x000
#define	DWC_GOTGINT		0x004
#define	DWC_GAHBCFG		0x008
#define	DWC_GUSBCFG		0x00C
#define	DWC_GRSTCTL		0x010
#define	DWC_GINTSTS		0x014
#define	DWC_GINTMSK		0x018
#define	DWC_GRXSTSR		0x01C
#define	DWC_GRXSTSP		0x020
#define	DWC_GRXFSIZ		0x024
#define	DWC_GNPTXFSIZ		0x028
#define	DWC_GNPTXSTS		0x02C
#define	DWC_GI2CCTL		0x030
#define	DWC_VDCTL		0x034
#define	DWC_GGPIO		0x038
#define	DWC_GUID		0x03C
#define	DWC_GSNPSID		0x040
#define	DWC_GHWCFG1		0x044
#define	DWC_GHWCFG2		0x048
#define	DWC_GHWCFG3		0x04c
#define	DWC_GHWCFG4		0x050
#define	DWC_HPTXFSIZ		0x100
#define	DWC_DPTX_FSIZ_DIPTXF(x)	(0x104 + (x) * 4)	/* 15 <= x > 1 */

#define DWC_GLBINTRMASK				0x0001
#define DWC_DMAENABLE				0x0020
#define DWC_NPTXEMPTYLVL_EMPTY			0x0080
#define DWC_NPTXEMPTYLVL_HALFEMPTY		0x0000
#define DWC_PTXEMPTYLVL_EMPTY			0x0100
#define DWC_PTXEMPTYLVL_HALFEMPTY		0x0000

#define DWC_SLAVE_ONLY_ARCH			0
#define DWC_EXT_DMA_ARCH			1
#define DWC_INT_DMA_ARCH			2

#define DWC_MODE_HNP_SRP_CAPABLE		0
#define DWC_MODE_SRP_ONLY_CAPABLE		1
#define DWC_MODE_NO_HNP_SRP_CAPABLE		2
#define DWC_MODE_SRP_CAPABLE_DEVICE		3
#define DWC_MODE_NO_SRP_CAPABLE_DEVICE		4
#define DWC_MODE_SRP_CAPABLE_HOST		5
#define DWC_MODE_NO_SRP_CAPABLE_HOST		6

/*
 * These Macros represents the bit fields of the Core OTG Controland Status
 * Register (GOTGCTL).  Set the bits using the bit fields then write the u32
 * value to the register.
 */
#define DWC_GCTL_BSESSION_VALID     (1 << 19)
#define DWC_GCTL_CSESSION_VALID     (1 << 18)
#define DWC_GCTL_DEBOUNCE           (1 << 17)
#define DWC_GCTL_CONN_ID_STATUS     (1 << 16)
#define DWC_GCTL_DEV_HNP_ENA        (1 << 11)
#define DWC_GCTL_HOST_HNP_ENA       (1 << 10)
#define DWC_GCTL_HNP_REQ            (1 << 9)
#define DWC_GCTL_HOST_NEG_SUCCES    (1 << 8)
#define DWC_GCTL_SES_REQ            (1 << 1)
#define DWC_GCTL_SES_REQ_SUCCESS    (1 << 0)

#define DWC_GCTL_BSESSION_VALID_RD(reg)		(((reg) & (0x001 << 19)) >> 19)
#define DWC_GCTL_CSESSION_VALID_RD(reg)		(((reg) & (0x001 << 18)) >> 18)
#define DWC_GCTL_DEBOUNCE_RD(reg)		(((reg) & (0x001 << 17)) >> 17)
#define DWC_GCTL_CONN_ID_STATUS_RD(reg)		(((reg) & (0x001 << 16)) >> 16)
#define DWC_GCTL_DEV_HNP_ENA_RD(reg)		(((reg) & (0x001 << 11)) >> 11)
#define DWC_GCTL_HOST_HNP_ENA_RD(reg)		(((reg) & (0x001 << 10)) >> 10)
#define DWC_GCTL_HNP_REQ_RD(reg)		(((reg) & (0x001 << 9)) >> 9)
#define DWC_GCTL_HOST_NEG_SUCCES_RD(reg)	(((reg) & (0x001 << 8)) >> 8)
#define DWC_GCTL_SES_REQ_RD(reg)		(((reg) & (0x001 << 1)) >> 1)
#define DWC_GCTL_SES_REQ_SUCCESS_RD(reg)	(((reg) & (0x001 << 0)) >> 0)

#define DWC_GCTL_BSESSION_VALID_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 19))) | ((x) << 19))
#define DWC_GCTL_CSESSION_VALID_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 18))) | ((x) << 18))
#define DWC_GCTL_DEBOUNCE_RW(reg, x)		\
	(((reg) & (~((u32)0x01 << 17))) | ((x) << 17))
#define DWC_GCTL_CONN_ID_STATUS_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 16))) | ((x) << 16))
#define DWC_GCTL_DEV_HNP_ENA_RW	(reg, x)	\
	(((reg) & (~((u32)0x01 << 11))) | ((x) << 11))
#define DWC_GCTL_HOST_HNP_ENA_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 10))) | ((x) << 10))
#define DWC_GCTL_HNP_REQ_RW(reg, x)		\
	(((reg) & (~((u32)0x01 << 9))) | ((x) << 9))
#define DWC_GCTL_HOST_NEG_SUCCES_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 8))) | ((x) << 8))
#define DWC_GCTL_SES_REQ_RW(reg, x)		\
	(((reg) & (~((u32)0x01 << 1))) | ((x) << 1))
#define DWC_GCTL_SES_REQ_SUCCESS_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 0))) | ((x) << 0))
/*
 * These Macros represents the bit fields of the Core OTG Interrupt Register
 * (GOTGINT).  Set/clear the bits using the bit fields then write the u32
 * value to the register.
 */
#define DWC_GINT_DEBDONE	(1 << 19)
#define DWC_GINT_DEVTOUT	(1 << 18)
#define DWC_GINT_HST_NEGDET	(1 << 17)
#define DWC_GINT_HST_NEGSUC	(1 << 9)
#define DWC_GINT_SES_REQSUC	(1 << 8)
#define DWC_GINT_SES_ENDDET	(1 << 2)

/*
 * These Macros represents the bit fields of the Core AHB Configuration Register
 * (GAHBCFG).  Set/clear the bits using the bit fields then write the u32 value
 * to the register.
 */
#define DWC_AHBCFG_FIFO_EMPTY		(1 << 8)
#define DWC_AHBCFG_NPFIFO_EMPTY		(1 << 7)
#define DWC_AHBCFG_DMA_ENA		(1 << 5)
#define DWC_AHBCFG_BURST_LEN(x)		(x << 1)
#define DWC_AHBCFG_GLBL_INT_MASK	(1 << 0)

#define DWC_GAHBCFG_TXFEMPTYLVL_EMPTY		1
#define DWC_GAHBCFG_TXFEMPTYLVL_HALFEMPTY	0
#define DWC_GAHBCFG_DMAENABLE			1
#define DWC_GAHBCFG_INT_DMA_BURST_SINGLE	0
#define DWC_GAHBCFG_INT_DMA_BURST_INCR		1
#define DWC_GAHBCFG_INT_DMA_BURST_INCR4		3
#define DWC_GAHBCFG_INT_DMA_BURST_INCR8		5
#define DWC_GAHBCFG_INT_DMA_BURST_INCR16	7

/*

 * (GUSBCFG).  Set the bits using the bit fields then write the u32 value to the
 * register.
 */
#define DWC_USBCFG_CORR_PKT		(1 << 31)
#define DWC_USBCFG_FRC_DEV_MODE		(1 << 30)
#define DWC_USBCFG_FRC_HST_MODE		(1 << 29)
#define DWC_USBCFG_TERM_SEL_DL_PULSE	(1 << 22)
#define DWC_USBCFG_ULPI_INTVBUS_INDICATOR (1 << 21)
#define DWC_USBCFG_ULPI_EXT_VBUS_DRV	(1 << 20)
#define DWC_USBCFG_ULPI_CLK_SUS_M	(1 << 19)
#define DWC_USBCFG_ULPI_AUTO_RES	(1 << 18)
#define DWC_USBCFG_ULPI_FSLS		(1 << 17)
#define DWC_USBCFG_OTGUTMIFSSEL		(1 << 16)
#define DWC_USBCFG_PHYLPWRCLKSEL	(1 << 15)
#define DWC_USBCFG_NPTXFRWNDEN		(1 << 14)
#define DWC_USBCFG_TRN_TIME(x)		(x << 10)
#define DWC_USBCFG_HNP_CAP		(1 << 9)
#define DWC_USBCFG_SRP_CAP		(1 << 8)
#define DWC_USBCFG_DDRSEL		(1 << 7)
#define DWC_USBCFG_USB_2_11		(1 << 6)
#define DWC_USBCFG_FSINTF		(1 << 5)
#define DWC_USBCFG_ULPI_UTMI_SEL	(1 << 4)
#define DWC_USBCFG_PHYIF		(1 << 3)
#define DWC_USBCFG_TOUT_CAL(x)		(x << 0)

/*
 * These Macros represents the bit fields of the Core Reset Register (GRSTCTL).
 * Set/clear the bits using the bit fields then write the u32 value to the
 * register.
 */
#define DWC_RSTCTL_AHB_IDLE	(1 << 31)
#define DWC_RSTCTL_DMA_REQ	(1 << 30)
#define DWC_RSTCTL_TX_FIFO_NUM(reg, x)	\
	(((reg) & (~((u32)0x1f << 6))) | ((x) << 6))
#define DWC_RSTCTL_TX_FIFO_FLUSH	(1 << 5)
#define DWC_RSTCTL_RX_FIFO_FLUSH	(1 << 4)
#define DWC_RSTCTL_TKN_QUE_FLUSH	(1 << 3)
#define DWC_RSTCTL_HSTFRM_CNTR_RST	(1 << 2)
#define DWC_RSTCTL_HCLK_SFT_RST	(1 << 1)
#define DWC_RSTCTL_SFT_RST	(1 << 1)
#define DWC_GRSTCTL_TXFNUM_ALL	0x10

/*
 * These Macros represents the bit fields of the Core Interrupt Mask Register
 * (GINTMSK).  Set/clear the bits using the bit fields then write the u32 value
 * to the register.
 */
#define DWC_INTMSK_WKP			(1 << 31)
#define DWC_INTMSK_NEW_SES_DET		(1 << 30)
#define DWC_INTMSK_SES_DISCON_DET	(1 << 29)
#define DWC_INTMSK_CON_ID_STS_CHG	(1 << 28)
#define DWC_INTMSK_P_TXFIFO_EMPTY	(1 << 26)
#define DWC_INTMSK_HST_CHAN		(1 << 25)
#define DWC_INTMSK_HST_PORT		(1 << 24)
#define DWC_INTMSK_DATA_FETCH_SUS	(1 << 23)
#define DWC_INTMSK_INCMP_PTX		(1 << 22)
#define DWC_INTMSK_INCMP_OUT_PTX	(1 << 21)
#define DWC_INTMSK_INCMP_IN_ATX		(1 << 20)
#define DWC_INTMSK_OUT_ENDP		(1 << 19)
#define DWC_INTMSK_IN_ENDP		(1 << 18)
#define DWC_INTMSK_ENDP_MIS_MTCH	(1 << 17)
#define DWC_INTMSK_END_OF_PFRM		(1 << 15)
#define DWC_INTMSK_ISYNC_OUTPKT_DRP	(1 << 14)
#define DWC_INTMSK_ENUM_DONE		(1 << 13)
#define DWC_INTMSK_USB_RST		(1 << 12)
#define DWC_INTMSK_USB_SUSP		(1 << 11)
#define DWC_INTMSK_EARLY_SUSP		(1 << 10)
#define DWC_INTMSK_I2C_INTR		(1 << 9)
#define DWC_INTMSK_GLBL_OUT_NAK		(1 << 7)
#define DWC_INTMSK_GLBL_IN_NAK		(1 << 6)
#define DWC_INTMSK_NP_TXFIFO_EMPT	(1 << 5)
#define DWC_INTMSK_RXFIFO_NOT_EMPT	(1 << 4)
#define DWC_INTMSK_STRT_OF_FRM		(1 << 3)
#define DWC_INTMSK_OTG			(1 << 2)
#define DWC_INTMSK_MODE_MISMTC		(1 << 1)
/*
 * These Macros represents the bit fields of the Core Interrupt Register
 * (GINTSTS).  Set/clear the bits using the bit fields then write the u32 value
 * to the register.
 */
#define DWC_INTSTS_WKP			(1 << 31)
#define DWC_INTSTS_NEW_SES_DET		(1 << 30)
#define DWC_INTSTS_SES_DISCON_DET	(1 << 29)
#define DWC_INTSTS_CON_ID_STS_CHG	(1 << 28)
#define DWC_INTSTS_P_TXFIFO_EMPTY	(1 << 26)
#define DWC_INTSTS_HST_CHAN		(1 << 25)
#define DWC_INTSTS_HST_PORT		(1 << 24)
#define DWC_INTSTS_DATA_FETCH_SUS	(1 << 23)
#define DWC_INTSTS_INCMP_PTX		(1 << 22)
#define DWC_INTSTS_INCMP_OUT_PTX	(1 << 21)
#define DWC_INTSTS_INCMP_IN_ATX		(1 << 20)
#define DWC_INTSTS_OUT_ENDP		(1 << 19)
#define DWC_INTSTS_IN_ENDP		(1 << 18)
#define DWC_INTSTS_ENDP_MIS_MTCH	(1 << 17)
#define DWC_INTSTS_END_OF_PFRM		(1 << 15)
#define DWC_INTSTS_ISYNC_OUTPKT_DRP	(1 << 14)
#define DWC_INTSTS_ENUM_DONE		(1 << 13)
#define DWC_INTSTS_USB_RST		(1 << 12)
#define DWC_INTSTS_USB_SUSP		(1 << 11)
#define DWC_INTSTS_EARLY_SUSP		(1 << 10)
#define DWC_INTSTS_I2C_INTR		(1 << 9)
#define DWC_INTSTS_GLBL_OUT_NAK		(1 << 7)
#define DWC_INTSTS_GLBL_IN_NAK		(1 << 6)
#define DWC_INTSTS_NP_TXFIFO_EMPT	(1 << 5)
#define DWC_INTSTS_RXFIFO_NOT_EMPT	(1 << 4)
#define DWC_INTSTS_STRT_OF_FRM		(1 << 3)
#define DWC_INTSTS_OTG			(1 << 2)
#define DWC_INTSTS_MODE_MISMTC		(1 << 1)
#define DWC_INTSTS_CURR_MODE		(1 << 0)
#define DWC_SOF_INTR_MASK		0x0008
#define DWC_HOST_MODE			1

/*
 * These Macros represents the bit fields in the Device Receive Status Read and
 * Pop Registers (GRXSTSR, GRXSTSP) Read the register into the u32
 * element then read out the bits using the bit elements.
 */
#define DWC_DM_RXSTS_PKT_STS	(0x01f << 17)
#define DWC_DM_RXSTS_PKT_DPID	(0x003 << 15)
#define DWC_DM_RXSTS_BYTE_CNT	(0x7ff << 4)
#define DWC_DM_RXSTS_CHAN_NUM	(0x00f << 0)

#define DWC_DM_RXSTS_PKT_STS_RD(reg)	(((reg) & (0x00f << 17)) >> 17)
#define DWC_DM_RXSTS_PKT_DPID_RD(reg)	(((reg) & (0x003 << 15)) >> 15)
#define DWC_DM_RXSTS_BYTE_CNT_RD(reg)	(((reg) & (0x7ff << 04)) >> 04)
#define DWC_DM_RXSTS_CHAN_NUM_RD(reg)	((reg) & 0x00f)

#define DWC_STS_DATA_UPDT		0x2	/* OUT Data Packet */
#define DWC_STS_XFER_COMP		0x3	/* OUT Data Transfer Complete */
#define DWC_DSTS_GOUT_NAK		0x1	/* Global OUT NAK */
#define DWC_DSTS_SETUP_COMP		0x4	/* Setup Phase Complete */
#define DWC_DSTS_SETUP_UPDT		0x6	/* SETUP Packet */

/*
 * These Macros represents the bit fields in the Host Receive Status Read and
 * Pop Registers (GRXSTSR, GRXSTSP) Read the register into the u32
 * element then read out the bits using the bit elements.
 */
#define DWC_HM_RXSTS_FRM_NUM	(0x00f << 21)
#define DWC_HM_RXSTS_PKT_STS	(0x01f << 17)
#define DWC_HM_RXSTS_PKT_DPID	(0x003 << 15)
#define DWC_HM_RXSTS_BYTE_CNT	(0x7ff << 4)
#define DWC_HM_RXSTS_CHAN_NUM	(0x00f << 0)

#define DWC_HM_RXSTS_PKT_STS_RD(reg)	(((reg) & (0x00f << 17)) >> 17)
#define DWC_HM_RXSTS_PKT_DPID_RD(reg)	(((reg) & (0x003 << 15)) >> 15)
#define DWC_HM_RXSTS_BYTE_CNT_RD(reg)	(((reg) & (0x7ff << 04)) >> 04)
#define DWC_HM_RXSTS_CHAN_NUM_RD(reg)	((reg) & 0x00f)

#define DWC_GRXSTS_PKTSTS_IN			0x2
#define DWC_GRXSTS_PKTSTS_IN_XFER_COMP		0x3
#define DWC_GRXSTS_PKTSTS_DATA_TOGGLE_ERR	0x5
#define DWC_GRXSTS_PKTSTS_CH_HALTED		0x7

/*
 * These Macros represents the bit fields in the FIFO Size Registers (HPTXFSIZ,
 * GNPTXFSIZ, DPTXFSIZn). Read the register into the u32 element then
 * read out the bits using the bit elements.
 */
#define DWC_RX_FIFO_DEPTH_RD(reg)	(((reg) & ((u32)0xffff << 16)) >> 16)
#define DWC_RX_FIFO_DEPTH_WR(reg, x)	\
	(((reg) & (~((u32)0xffff << 16))) | ((x) << 16))
#define DWC_RX_FIFO_START_ADDR_RD(reg)		((reg) & 0xffff)
#define DWC_RX_FIFO_START_ADDR_WR(reg, x)	\
	(((reg) & (~((u32)0xffff))) | (x))

/*
 * These Macros represents the bit fields in the Non-Periodic Tx FIFO/Queue
 * Status Register (GNPTXSTS). Read the register into the u32 element then read
 * out the bits using the bit elements.
 */
#define DWC_GNPTXSTS_NPTXQTOP_CHNEP_RD(x)	(((x) & (0x3f << 26)) >> 26)
#define DWC_GNPTXSTS_NPTXQTOP_TKN_RD(x)		(((x) & (0x03 << 24)) >> 24)
#define DWC_GNPTXSTS_NPTXQSPCAVAIL_RD(x)	(((x) & (0xff << 16)) >> 16)
#define DWC_GNPTXSTS_NPTXFSPCAVAIL_RD(x)	(0xffff & (x))

/*
 * These Macros represents the bit fields in the Transmit FIFO Status Register
 * (DTXFSTS). Read the register into the u32 element then read out the bits
 * using the bit elements.
 */
#define DWC_DTXFSTS_TXFSSPC_AVAI_RD(x)	((x) & 0xffff)

/*
 * These Macros represents the bit fields in the I2C Control Register (I2CCTL).
 * Read the register into the u32 element then read out the bits using the bit
 * elements.
 */
#define DWC_I2CCTL_BSYDNE	(1 << 31)
#define DWC_I2CCTL_RW		(1 << 30)
#define DWC_I2CCTL_I2CDEVADDR(x)	((x) << 27)
#define DWC_I2CCTL_I2CSUSCTL	(1 << 25)
#define DWC_I2CCTL_ACK		(1 << 24)
#define DWC_I2CCTL_I2CEN	(1 << 23)
#define DWC_I2CCTL_ADDR		(1 << 22)
#define DWC_I2CCTL_REGADDR(x)	((x) << 14)
#define DWC_I2CCTL_RWDATA(x)	((x) << 6)

/*
 * These Macros represents the bit fields in the User HW Config1 Register.  Read
 * the register into the u32 element then read out the bits using the bit
 * elements.
 */
#define DWC_HWCFG1_EPDIR15(x)	((x) << 30)
#define DWC_HWCFG1_EPDIR14(x)	((x) << 28)
#define DWC_HWCFG1_EPDIR13(x)	((x) << 26)
#define DWC_HWCFG1_EPDIR12(x)	((x) << 24)
#define DWC_HWCFG1_EPDIR11(x)	((x) << 22)
#define DWC_HWCFG1_EPDIR10(x)	((x) << 20)
#define DWC_HWCFG1_EPDIR9(x)	((x) << 18)
#define DWC_HWCFG1_EPDIR8(x)	((x) << 16)
#define DWC_HWCFG1_EPDIR7(x)	((x) << 14)
#define DWC_HWCFG1_EPDIR6(x)	((x) << 13)
#define DWC_HWCFG1_EPDIR5(x)	((x) << 10)
#define DWC_HWCFG1_EPDIR4(x)	((x) << 08)
#define DWC_HWCFG1_EPDIR3(x)	((x) << 06)
#define DWC_HWCFG1_EPDIR2(x)	((x) << 04)
#define DWC_HWCFG1_EPDIR1(x)	((x) << 02)
#define DWC_HWCFG1_EPDIR0(x)	((x) << 00)

/*
 * These Macros represents the bit fields in the User HW Config2 Register.  Read
 * the register into the u32 element then read out the bits using the bit
 * elements.
 */
#define DWC_HWCFG2_DEV_TKN_Q_DEPTH_RD(x)	(((x) & (0x1F << 26)) >> 26)
#define DWC_HWCFG2_HOST_PERIO_Q_DEPTH_RD(x)	(((x) & (0x3 << 24)) >> 24)
#define DWC_HWCFG2_NP_TX_Q_DEPTH_RD(x)		(((x) & (0x3 << 22)) >> 22)
#define DWC_HWCFG2_RX_STS_Q_DEPTH_RD(x)		(((x) & (0x3 << 20)) >> 20)
#define DWC_HWCFG2_DYN_FIFO_RD(x)		(((x) & (0x1 << 19)) >> 19)
#define DWC_HWCFG2_PERIO_EP_SUPP_RD(x)		(((x) & (0x1 << 18)) >> 18)
#define DWC_HWCFG2_NO_HST_CHAN_RD(x)		(((x) & (0xf << 14)) >> 14)
#define DWC_HWCFG2_NO_DEV_EP_RD(x)		(((x) & (0xf << 10)) >> 10)
#define DWC_HWCFG2_FS_PHY_TYPE_RD(x)		(((x) & (0x3 <<  8)) >> 8)
#define DWC_HWCFG2_HS_PHY_TYPE_RD(x)		(((x) & (0x3 << 06)) >> 06)
#define DWC_HWCFG2_P_2_P_RD(x)			(((x) & (0x1 << 05)) >> 05)
#define DWC_HWCFG2_ARCH_RD(x)			(((x) & (0x3 << 03)) >> 03)
#define DWC_HWCFG2_OP_MODE_RD(x)		((x) & 0x7)

#define DWC_HWCFG2_HS_PHY_TYPE_NOT_SUPPORTED		0
#define DWC_HWCFG2_HS_PHY_TYPE_UTMI			1
#define DWC_HWCFG2_HS_PHY_TYPE_ULPI			2
#define DWC_HWCFG2_HS_PHY_TYPE_UTMI_ULPI		3
#define DWC_HWCFG2_OP_MODE_HNP_SRP_CAPABLE_OTG		0
#define DWC_HWCFG2_OP_MODE_SRP_ONLY_CAPABLE_OTG		1
#define DWC_HWCFG2_OP_MODE_NO_HNP_SRP_CAPABLE_OTG	2
#define DWC_HWCFG2_OP_MODE_SRP_CAPABLE_DEVICE		3
#define DWC_HWCFG2_OP_MODE_NO_SRP_CAPABLE_DEVICE	4
#define DWC_HWCFG2_OP_MODE_SRP_CAPABLE_HOST		5
#define DWC_HWCFG2_OP_MODE_NO_SRP_CAPABLE_HOST		6

/*
 * These Macros represents the bit fields in the User HW Config3 Register.  ead
 * the register into the u32 element then read out the bits using the bit
 * elements.
 */
#define DWC_HWCFG3_DFIFO_DEPTH_RD(x)		(((x) & (0xffff << 16)) >> 16)
#define	DWC_HWCFG3_AHB_PHY_CLK_SYNC_RD(x)	(((x) & (0x1 << 12)) >> 12)
#define DWC_HWCFG3_SYNC_RST_TYPE_RD(x)		(((x) & (0x1 << 11)) >> 11)
#define DWC_HWCFG3_OPT_FEATURES_RD(x)		(((x) & (0x1 << 10)) >> 10)
#define DWC_HWCFG3_VEND_CTRL_IF_RD(x)		(((x) & (0x1 << 9)) >> 9)
#define DWC_HWCFG3_I2C_RD(x)			(((x) & (0x1 << 8)) >> 8)
#define DWC_HWCFG3_OTG_FUNC_RD(x)		(((x) & (0x1 << 07)) >> 07)
#define DWC_HWCFG3_PKTSIZE_CTR_WIDTH_RD(x)	(((x) & (0x7 << 04)) >> 04)
#define DWC_HWCFG3_XFERSIZE_CTR_WIDTH_RD(x)	((x) & 0xf)

/*
 * These Macros represents the bit fields in the User HW Config4 Register.  Read
 * the register into the u32 element then read out the bits using the bit
 * elements.
 */
#define DWC_HWCFG4_NUM_IN_EPS_RD(x)		(((x) & (0xF << 26)) >> 26)
#define DWC_HWCFG4_DED_FIFO_ENA_RD(x)		(((x) & (0x1 << 25)) >> 25)
#define DWC_HWCFG4_SES_END_FILT_EN_RD(x)	(((x) & (0x1 << 24)) >> 24)
#define DWC_HWCFG4_BVALID_FILT_EN_RD(x)		(((x) & (0x1 << 23)) >> 23)
#define DWC_HWCFG4_AVALID_FILT_EN_RD(x)		(((x) & (0x1 << 22)) >> 22)
#define DWC_HWCFG4_VBUS_VALID_FILT_EN_RD(x)	(((x) & (0x1 << 21)) >> 21)
#define DWC_HWCFG4_IDDIG_FILT_EN_RD(x)		(((x) & (0x1 << 20)) >> 20)
#define DWC_HWCFG4_NUM_DEV_MODE_CTRL_EP_RD(x)	(((x) & (0xF << 16)) >> 16)
#define DWC_HWCFG4_UTMI_PHY_DATA_WIDTH_RD(x)	(((x) & (0x3 << 14)) >> 14)
#define DWC_HWCFG4_MIN_AHB_FREQ_RD(x)		(((x) & (0x1 << 05)) >> 05)
#define DWC_HWCFG4_POWER_OPT_RD(x)		(((x) & (0x1 << 04)) >> 04)
#define	DWC_HWCFG4_NUM_DEV_PERIO_IN_EP_RD(x)	((x) & 0xf)

/*
 * Device Global Registers. Offsets 800h-BFFh
 *
 * The following structures define the size and relative field offsets for the
 * Device Mode Registers.
 *
 * These registers are visible only in Device mode and must not be accessed in
 * Host mode, as the results are unknown.
 */
#define DWC_DCFG	0x000
#define DWC_DCTL	0x004
#define DWC_DSTS	0x008
#define DWC_DIEPMSK	0x010
#define DWC_DOEPMSK	0x014
#define DWC_DAINT	0x018
#define DWC_DAINTMSK	0x01C
#define DWC_DTKNQR1	0x020
#define DWC_DTKNQR2	0x024
#define DWC_DVBUSDIS	0x028
#define DWC_DVBUSPULSE	0x02C
#define DWC_DTKNQR3_DTHRCTL	0x030
#define DWC_DTKNQR4FIFOEMPTYMSK	0x034

/*
 * These Macros represents the bit fields in the Device Configuration
 * Register.  Read the register into the u32 member then
 * set/clear the bits using the bit elements.  Write the
 * u32 member to the dcfg register.
*/
#define DWC_DCFG_IN_EP_MISMATCH_CNT_RD(x)	(((x) & (0x1f << 18)) >> 18)
#define DWC_DCFG_P_FRM_INTRVL_RD(x)		(((x) & (0x03 << 11)) >> 11)
#define DWC_DCFG_DEV_ADDR_RD(x)			(((x) & (0x3f << 04)) >> 04)
#define DWC_DCFG_NGL_STS_OUT_RD(x)		(((x) & (0x1 << 2)) >> 2)
#define DWC_DCFG_DEV_SPEED_RD(x)		((x) & 0x3)

#define DWC_DCFG_IN_EP_MISMATCH_CNT_WR(reg, x)	\
	(((reg) & (~((u32)0x1f << 18))) | ((x) << 18))
#define DWC_DCFG_P_FRM_INTRVL_WR(reg, x)	\
	(((reg) & (~((u32)0x03 << 11))) | ((x) << 11))
#define DWC_DCFG_DEV_ADDR_WR(reg, x)	\
	(((reg) & (~((u32)0x3f << 04))) | ((x) << 04))
#define DWC_DCFG_NGL_STS_OUT_WR(reg, x)	\
	(((reg) & (~((u32)0x1 << 2)))   | ((x) << 2))
#define DWC_DCFG_DEV_SPEED_WR(reg, x)	\
	(((reg) & (~(u32)0x3)) | (x))

#define DWC_DCFG_FRAME_INTERVAL_80		0
#define DWC_DCFG_FRAME_INTERVAL_85		1
#define DWC_DCFG_FRAME_INTERVAL_90		2
#define DWC_DCFG_FRAME_INTERVAL_95		3

/*
 * These Macros represents the bit fields in the Device Control Register.  Read
 * the register into the u32 member then set/clear the bits using the bit
 * elements.
 */
#define DWC_DCTL_PWR_ON_PROG_DONE_RD(x)	(((x) & (1 << 11)) >> 11)

#define DWC_DCTL_PWR_ON_PROG_DONE_WR(reg, x)	\
	(((reg) & (~((u32)0x01 << 11))) | ((x) << 11))
#define DWC_DCTL_CLR_GLBL_OUT_NAK_WR(reg, x)	\
	(((reg) & (~((u32)0x01 << 10))) | ((x) << 10))
#define DWC_DCTL_SET_GLBL_OUT_NAL(reg, x)	\
	(((reg) & (~((u32)0x01 << 9))) | ((x) << 9))
#define DWC_DCTL_CLR_CLBL_NP_IN_NAK(reg, x)	\
	(((reg) & (~((u32)0x01 << 8))) | ((x) << 8))
#define DWC_DCTL_SET_GLBL_NP_IN_NAK(reg, x)	\
	(((reg) & (~((u32)0x01 << 07))) | ((x) << 07))
#define DWC_DCTL_TST_CTL(reg, x)	\
	(((reg) & (~((u32)0x07 << 04))) | ((x) << 04))
#define DWC_DCTL_GLBL_OUT_NAK_STS(reg, x)	\
	(((reg) & (~((u32)0x01 << 03))) | ((x) << 03))
#define DWC_DCTL_GLBL_NP_IN_NAK(reg, x)		\
	(((reg) & (~((u32)0x01 << 02))) | ((x) << 02))
#define DWC_DCTL_SFT_DISCONNECT(reg, x)		\
	(((reg) & (~((u32)0x01 << 01))) | ((x) << 01))
#define DEC_DCTL_REMOTE_WAKEUP_SIG(reg, x)	\
	(((reg) & (~((u32)0x01 << 00))) | ((x) << 00))

/*
 * These Macros represents the bit fields in the Dev Status Register. Read the
 * register into the u32 member then set/clear the bits using the bit elements.
 */
#define DWC_DSTS_SOFFN_RD(x)		(((x) & (0x3fff << 8)) >> 8)
#define DWC_DSTS_ERRTICERR_RD(x)	(((x) & (0x0001 << 3)) >> 3)
#define DWC_DSTS_ENUM_SPEED_RD(x)	(((x) & (0x0003 << 1)) >> 1)
#define DWC_DSTS_SUSP_STS_RD(x)		((x) & 1)

#define DWC_DSTS_ENUMSPD_HS_PHY_30MHZ_OR_60MHZ		0
#define DWC_DSTS_ENUMSPD_FS_PHY_30MHZ_OR_60MHZ		1
#define DWC_DSTS_ENUMSPD_LS_PHY_6MHZ			2
#define DWC_DSTS_ENUMSPD_FS_PHY_48MHZ			3

/*
 * These Macros represents the bit fields in the Device IN EP Interrupt Register
 * and the Device IN EP Common Mask Register.
 *
 * Read the register into the u32 member then set/clear the bits using the bit
 * elements.
 */
#define DWC_DIEPINT_TXFIFO_UNDERN_RD(x)		(((x) & (0x1 << 8)) >> 8)
#define DWC_DIEPINT_TXFIFO_EMPTY_RD(x)		(((x) & (0x1 << 7)) >> 7)
#define DWC_DIEPINT_IN_EP_NAK_RD(x)		(((x) & (0x1 << 6)) >> 6)
#define DWC_DIEPINT_IN_TKN_EP_MISS_RD(x)	(((x) & (0x1 << 5)) >> 5)
#define DWC_DIEPINT_IN_TKN_TX_EMPTY_RD(x)	(((x) & (0x1 << 4)) >> 4)
#define DWC_DIEPINT_TOUT_COND_RD(x)		(((x) & (0x1 << 3)) >> 3)
#define DWC_DIEPINT_AHB_ERROR_RD(x)		(((x) & (0x1 << 2)) >> 2)
#define DWC_DIEPINT_EP_DISA_RD(x)		(((x) & (0x1 << 1)) >> 1)
#define DWC_DIEPINT_TX_CMPL_RD(x)		((x) & 0x1)

#define DWC_DIEPINT_TXFIFO_UNDERN_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 8))) | ((x) << 8))
#define DWC_DIEPINT_TXFIFO_EMPTY_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 7))) | ((x) << 7))
#define DWC_DIEPINT_IN_EP_NAK_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 6))) | ((x) << 6))
#define DWC_DIEPINT_IN_TKN_EP_MISS_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 5))) | ((x) << 5))
#define DWC_DIEPINT_IN_TKN_TX_EMPTY_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 4))) | ((x) << 4))
#define DWC_DIEPINT_TOUT_COND_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 3))) | ((x) << 3))
#define DWC_DIEPINT_AHB_ERROR_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 2))) | ((x) << 2))
#define DWC_DIEPINT_EP_DISA_RW(reg, x)		\
	(((reg) & (~((u32)0x01 << 1))) | ((x) << 1))
#define DWC_DIEPINT_TX_CMPL_RW(reg, x)		\
	(((reg) & (~((u32)0x01 << 0))) | ((x) << 0))

#define DWC_DIEPMSK_TXFIFO_UNDERN_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 8))) | ((x) << 8))
#define DWC_DIEPMSK_TXFIFO_EMPTY_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 7))) | ((x) << 7))
#define DWC_DIEPMSK_IN_EP_NAK_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 6))) | ((x) << 6))
#define DWC_DIEPMSK_IN_TKN_EP_MISS_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 5))) | ((x) << 5))
#define DWC_DIEPMSK_IN_TKN_TX_EMPTY_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 4))) | ((x) << 4))
#define DWC_DIEPMSK_TOUT_COND_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 3))) | ((x) << 3))
#define DWC_DIEPMSK_AHB_ERROR_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 2))) | ((x) << 2))
#define DWC_DIEPMSK_EP_DISA_RW(reg, x)		\
	(((reg) & (~((u32)0x01 << 1))) | ((x) << 1))
#define DWC_DIEPMSK_TX_CMPL_RW(reg, x)		\
	(((reg) & (~((u32)0x01 << 0))) | ((x) << 0))

/*
 * These Macros represents the bit fields in the Device OUT EP Itr Register
 * and Device OUT EP Common Interrupt Mask Register.
 *
 * Read the register into the u32 member then set/clear the bits using the bit
 * elements.
 */
#define DWC_DOEPINT_OUTPKT_ERR_RD(x)	(((x) & (0x1 << 8)) >> 8)
#define DWC_DOEPINT_B2B_PKTS_RD(x)	(((x) & (0x1 << 6)) >> 6)
#define DWC_DOEPINT_OUT_TKN_RD(x)	(((x) & (0x1 << 4)) >> 4)
#define DWC_DOEPINT_SETUP_DONE_RD(x)	(((x) & (0x1 << 3)) >> 3)
#define DWC_DOEPINT_AHB_ERROR_RD(x)	(((x) & (0x1 << 2)) >> 2)
#define DWC_DOEPINT_EP_DISA_RD(x)	(((x) & (0x1 << 1)) >> 1)
#define DWC_DOEPINT_TX_COMPL_RD(x)	(((x) & (0x1 << 0)) >> 0)

#define DWC_DOEPMSK_OUTPKT_ERR_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 8))) | ((x) << 8))
#define DWC_DOEPMSK_B2B_PKTS_RW(reg, x)		\
	(((reg) & (~((u32)0x01 << 6))) | ((x) << 6))
#define DWC_DOEPMSK_OUT_TKN_RW(reg, x)		\
	(((reg) & (~((u32)0x01 << 4))) | ((x) << 4))
#define DWC_DOEPMSK_SETUP_DONE_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 3))) | ((x) << 3))
#define DWC_DOEPMSK_AHB_ERROR_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 2))) | ((x) << 2))
#define DWC_DOEPMSK_EP_DISA_RW(reg, x)		\
	(((reg) & (~((u32)0x01 << 1))) | ((x) << 1))
#define DWC_DOEPMSK_TX_COMPL_RW(reg, x)		\
	(((reg) & (~((u32)0x01 << 0))) | ((x) << 0))

/*
 * These Macros represents the bit fields in the Device All EP Intr and Mask
 * Registers.  Read the register into the u32 member then set/clear the bits
 * using the bit elements.
 */
#define DWC_DAINT_OUT_EP_RD(reg, ep)	\
	(((reg) & (1 << (ep + 16))) >> (ep + 16))
#define DWC_DAINTMSK_OUT_EP_RW(reg, ep)	\
	(((reg) & (~(u32)(1 << (ep + 16)))) | (1 << (ep + 16)))
#define DWC_DAINT_IN_EP_RD(reg, ep)		(((reg) & (1 << ep)) >> ep)
#define DWC_DAINTMSK_IN_EP_RW(reg, ep)	\
	(((reg) & (~(u32)(1 << ep))) | (1 << ep))
#define DWC_DAINT_OUTEP15	(1 << 31)
#define DWC_DAINT_OUTEP14	(1 << 30)
#define DWC_DAINT_OUTEP13	(1 << 29)
#define DWC_DAINT_OUTEP12	(1 << 28)
#define DWC_DAINT_OUTEP11	(1 << 27)
#define DWC_DAINT_OUTEP10	(1 << 26)
#define DWC_DAINT_OUTEP09	(1 << 25)
#define DWC_DAINT_OUTEP08	(1 << 24)
#define DWC_DAINT_OUTEP07	(1 << 23)
#define DWC_DAINT_OUTEP06	(1 << 22)
#define DWC_DAINT_OUTEP05	(1 << 21)
#define DWC_DAINT_OUTEP04	(1 << 20)
#define DWC_DAINT_OUTEP03	(1 << 19)
#define DWC_DAINT_OUTEP02	(1 << 18)
#define DWC_DAINT_OUTEP01	(1 << 17)
#define DWC_DAINT_OUTEP00	(1 << 16)
#define DWC_DAINT_INEP15	(1 << 15)
#define DWC_DAINT_INEP14	(1 << 14)
#define DWC_DAINT_INEP13	(1 << 13)
#define DWC_DAINT_INEP12	(1 << 12)
#define DWC_DAINT_INEP11	(1 << 11)
#define DWC_DAINT_INEP10	(1 << 10)
#define DWC_DAINT_INEP09	(1 << 09)
#define DWC_DAINT_INEP08	(1 << 08)
#define DWC_DAINT_INEP07	(1 << 07)
#define DWC_DAINT_INEP06	(1 << 06)
#define DWC_DAINT_INEP05	(1 << 05)
#define DWC_DAINT_INEP04	(1 << 04)
#define DWC_DAINT_INEP03	(1 << 03)
#define DWC_DAINT_INEP02	(1 << 02)
#define DWC_DAINT_INEP01	(1 << 01)
#define DWC_DAINT_INEP00	(1 << 00)

/*
 * These Macros represents the bit fields in the Device IN Token Queue Read
 * Registers.  Read the register into the u32 member. READ-ONLY Register
 */
#define DWC_DTKNQR1_EP_TKN_NO_RD(x)		(((x) & (0xffffff << 8)) >> 8)
#define DWC_DTKNQR1_WRAP_BIT_RD(x)		(((x) & (1 << 7)) >> 7)
#define DWC_DTKNQR1_INT_TKN_Q_WR_PTR_RD(x)	((x)  & 0x1f)

/*
 * These Macros represents Threshold control Register. Read and wr the register
 * into the u32 member.  READ-WRITABLE Register
 */
#define DWC_DTHCTRL_RX_ARB_PARK_EN_RD(x)	(((x) & (0x001 << 27)) >> 27)
#define DWC_DTHCTRL_RX_THR_LEN_RD(x)		(((x) & (0x1ff << 17)) >> 17)
#define DWC_DTHCTRL_RX_THR_EN_RD(x)		(((x) & (0x001 << 16)) >> 16)
#define DWC_DTHCTRL_TX_THR_LEN_RD(x)		(((x) & (0x1ff << 02)) >> 02)
#define DWC_DTHCTRL_ISO_THR_EN(x)		(((x) & (0x001 << 01)) >> 01)
#define DWC_DTHCTRL_NON_ISO_THR_ENA_RD(x)	(((x) & (0x001 << 00)) >> 00)

#define DWC_DTHCTRL_RX_ARB_PARK_EN_RW(reg, x)	\
	(((reg) & (~((u32)0x001 << 27))) | ((x) << 27))
#define DWC_DTHCTRL_RX_THR_LEN_RW(reg, x)	\
	(((reg) & (~((u32)0x1ff << 17))) | ((x) << 17))
#define DWC_DTHCTRL_RX_THR_EN_RW(reg, x)	\
	(((reg) & (~((u32)0x001 << 16))) | ((x) << 16))
#define DWC_DTHCTRL_TX_THR_LEN_RW(reg, x)	\
	(((reg) & (~((u32)0x1ff << 02))) | ((x) << 02))
#define DWC_DTHCTRL_ISO_THR_EN_RW(reg, x)	\
	(((reg) & (~((u32)0x001 << 01))) | ((x) << 01))
#define DWC_DTHCTRL_NON_ISO_THR_ENA_RW(reg, x)	\
	(((reg) & (~((u32)0x001 << 00))) | ((x) << 00))

/*
 * Device Logical IN Endpoint-Specific Registers. Offsets 900h-AFCh
 *
 * There will be one set of endpoint registers per logical endpoint implemented.
 *
 * These registers are visible only in Device mode and must not be accessed in
 * Host mode, as the results are unknown.
 */
#define DWC_DIEPCTL         0x00
#define DWC_DIEPINT         0x08
#define DWC_DIEPTSIZ        0x10
#define DWC_DIEPDMA         0x14
#define DWC_DTXFSTS         0x18

/*
 * Device Logical OUT Endpoint-Specific Registers. Offsets: B00h-CFCh
 *
 * There will be one set of endpoint registers per logical endpoint implemented.
 *
 * These registers are visible only in Device mode and must not be accessed in
 * Host mode, as the results are unknown.
 */
#define DWC_DOEPCTL		0x00
#define DWC_DOEPFN		0x04
#define DWC_DOEPINT		0x08
#define DWC_DOEPTSIZ		0x10
#define DWC_DOEPDMA		0x14

/*
 * These Macros represents the bit fields in the Device EP Ctrl Register. Read
 * the register into the u32 member then set/clear the bits using the bit
 * elements.
 */
#define DWC_DEP0CTL_MPS_64			0
#define DWC_DEP0CTL_MPS_32			1
#define DWC_DEP0CTL_MPS_16			2
#define DWC_DEP0CTL_MPS_8			3

#define DWC_DEPCTL_EPENA_RD(x)		(((x) & (0x1 << 31)) >> 31)
#define DWC_DEPCTL_EPDIS_RD(x)		(((x) & (0x1 << 30)) >> 30)
#define DWC_DEPCTL_SET_DATA1_PID_RD(x)	(((x) & (0x1 << 29)) >> 29)
#define DWC_DEPCTL_SET_DATA0_PID_RD(x)	(((x) & (0x1 << 28)) >> 28)
#define DWC_DEPCTL_SET_NAK_RD(x)	(((x) & (0x1 << 27)) >> 27)
#define DWC_DEPCTL_CLR_NAK_RD(x)	(((x) & (0x1 << 26)) >> 26)
#define DWC_DEPCTL_TX_FIFO_NUM_RD(x)	(((x) & (0xf << 22)) >> 22)
#define DWC_DEPCTL_STALL_HNDSHK	_RD(x)	(((x) & (0x1 << 21)) >> 21)
#define DWC_DEPCTL_SNP_MODE_RD(x)	(((x) & (0x1 << 20)) >> 20)
#define DWC_DEPCTL_EP_TYPE_RD(x)	(((x) & (0x3 << 18)) >> 18)
#define DWC_DEPCTL_NKASTS_RD(x)		(((x) & (0x1 << 17)) >> 17)
#define DWC_DEPCTL_DPID	_RD(x)		(((x) & (0x1 << 16)) >> 16)
#define DWC_DEPCTL_ACT_EP_RD(x)		(((x) & (0x1 << 15)) >> 15)
#define DWC_DEPCTL_NXT_EP_RD(x)		(((x) & (0xf << 11)) >> 11)
#define DWC_DEPCTL_MPS_RD(x)		(((x) & (0x7ff << 00)) >> 00)

#define DWC_DEPCTL_EPENA_RW(reg, x)		\
	(((reg) & (~((u32)0x001 << 31))) | ((x) << 31))
#define DWC_DEPCTL_EPDIS_RW(reg, x)		\
	(((reg) & (~((u32)0x001 << 30))) | ((x) << 30))
#define DWC_DEPCTL_SET_DATA1_PID_RW(reg, x)	\
	(((reg) & (~((u32)0x001 << 29))) | ((x) << 29))
#define DWC_DEPCTL_SET_DATA0_PID_RW(reg, x)	\
	(((reg) & (~((u32)0x001 << 28))) | ((x) << 28))
#define DWC_DEPCTL_SET_NAK_RW(reg, x)		\
	(((reg) & (~((u32)0x001 << 27))) | ((x) << 27))
#define DWC_DEPCTL_CLR_NAK_RW(reg, x)		\
	(((reg) & (~((u32)0x001 << 26))) | ((x) << 26))
#define DWC_DEPCTL_TX_FIFO_NUM_RW(reg, x)	\
	(((reg) & (~((u32)0x00f << 22))) | ((x) << 22))
#define DWC_DEPCTL_STALL_HNDSHK_RW(reg, x)	\
	(((reg) & (~((u32)0x001 << 21))) | ((x) << 21))
#define DWC_DEPCTL_SNP_MODE_RW(reg, x)		\
	(((reg) & (~((u32)0x001 << 20))) | ((x) << 20))
#define DWC_DEPCTL_EP_TYPE_RW(reg, x)		\
	(((reg) & (~((u32)0x003 << 18))) | ((x) << 18))
#define DWC_DEPCTL_NKASTS_RW(reg, x)		\
	(((reg) & (~((u32)0x001 << 17))) | ((x) << 17))
#define DWC_DEPCTL_DPID_RW(reg, x)		\
	(((reg) & (~((u32)0x001 << 16))) | ((x) << 16))
#define DWC_DEPCTL_ACT_EP_RW(reg, x)		\
	(((reg) & (~((u32)0x001 << 15))) | ((x) << 15))
#define DWC_DEPCTL_NXT_EP_RW(reg, x)		\
	(((reg) & (~((u32)0x00f << 11))) | ((x) << 11))
#define DWC_DEPCTL_MPS_RW(reg, x)		\
	(((reg) & (~((u32)0x7ff << 00))) | ((x) << 00))

/*
 * These Macros represents the bit fields in the Device EP Txfer Size Register.
 * Read the register into the u32 member then set/clear the bits using the bit
 * elements.
 */
#if defined(DWC_LIMITED_XFER_SIZE)
#define DWC_DEPTSIZ_MCOUNT_RD(x)	(((x) & (0x003 << 29)) >> 29)
#define	DWC_DEPTSIZ_PKT_CNT_RD(x)	(((x) & (0x01f << 19)) >> 19)
#define DWC_DEPTSIZ_XFER_SIZ_RD(x)	(((x) & (0x7ff << 00)) >> 00)
#define DWC_DEPTSIZ_MCOUNT_RW(reg, x)	\
	(((reg) & (~((u32)0x003 << 29))) | ((x) << 29))
#define	DWC_DEPTSIZ_PKT_CNT_RW(reg, x)	\
	(((reg) & (~((u32)0x01f << 19))) | ((x) << 19))
#define DWC_DEPTSIZ_XFER_SIZ_RW(reg, x)	\
	(((reg) & (~((u32)0x7ff << 00))) | ((x) << 00))
#else
#define DWC_DEPTSIZ_MCOUNT_RD(x)	\
	(((x) & (0x003 << 29)) >> 29)
#define	DWC_DEPTSIZ_PKT_CNT_RD(x)	\
	(((x) & (0x3ff << 19)) >> 19)
#define DWC_DEPTSIZ_XFER_SIZ_RD(x)	\
	(((x) & (0x7ffff << 00)) >> 00)
#define DWC_DEPTSIZ_MCOUNT_RW(reg, x)	\
	(((reg) & (~((u32)0x003 << 29))) | ((x) << 29))
#define	DWC_DEPTSIZ_PKT_CNT_RW(reg, x)	\
	(((reg) & (~((u32)0x7ff << 19))) | ((x) << 19))
#define DWC_DEPTSIZ_XFER_SIZ_RW(reg, x)	\
	(((reg) & (~((u32)0x7ffff << 00))) | ((x) << 00))
#endif

/*
 * These Macros represents the bit fields in the Device EP 0 Transfer Size
 * Register.  Read the register into the u32 member then set/clear the bits
 * using the bit elements.
 */
#define DWC_DEPTSIZ0_SUPCNT_RD(x)	(((x) & (0x003 << 29)) >> 29)
#define	DWC_DEPTSIZ0_PKT_CNT_RD(x)	(((x) & (0x001 << 19)) >> 19)
#define DWC_DEPTSIZ0_XFER_SIZ_RD(x)	(((x) & (0x07f << 00)) >> 00)
#define DWC_DEPTSIZ0_SUPCNT_RW(reg, x)	\
	(((reg) & (~((u32)0x003 << 29))) | ((x) << 29))
#define	DWC_DEPTSIZ0_PKT_CNT_RW(reg, x)	\
	(((reg) & (~((u32)0x001 << 19))) | ((x) << 19))
#define DWC_DEPTSIZ0_XFER_SIZ_RW(reg, x)	\
	(((reg) & (~((u32)0x07f << 00))) | ((x) << 00))

#define MAX_PERIO_FIFOS			15	/* Max periodic FIFOs */
#define MAX_TX_FIFOS			15	/* Max non-periodic FIFOs */

/* Maximum number of Endpoints/HostChannels */
#define MAX_EPS_CHANNELS 16	/* This come from device tree or defconfig */

/*
 * The device_if structure contains information needed to manage the DWC_otg
 * controller acting in device mode. It represents the programming view of the
 * device-specific aspects of the controller.
 */
struct device_if {
	/* Device Global Registers starting at offset 800h */
	ulong dev_global_regs;
#define DWC_DEV_GLOBAL_REG_OFFSET		0x800

	/* Device Logical IN Endpoint-Specific Registers 900h-AFCh */
	ulong in_ep_regs[MAX_EPS_CHANNELS];
#define DWC_DEV_IN_EP_REG_OFFSET		0x900
#define DWC_EP_REG_OFFSET			0x20

	/* Device Logical OUT Endpoint-Specific Registers B00h-CFCh */
	ulong out_ep_regs[MAX_EPS_CHANNELS];
#define DWC_DEV_OUT_EP_REG_OFFSET		0xB00

	/* Device configuration information */
	/* Device Speed  0: Unknown, 1: LS, 2:FS, 3: HS */
	u8 speed;
	/*  Number # of Tx EP range: 0-15 exept ep0 */
	u8 num_in_eps;
	/*  Number # of Rx EP range: 0-15 exept ep 0 */
	u8 num_out_eps;

	/* Size of periodic FIFOs (Bytes) */
	u16 perio_tx_fifo_size[MAX_PERIO_FIFOS];

	/* Size of Tx FIFOs (Bytes) */
	u16 tx_fifo_size[MAX_TX_FIFOS];

	/* Thresholding enable flags and length varaiables */
	u16 rx_thr_en;
	u16 iso_tx_thr_en;
	u16 non_iso_tx_thr_en;
	u16 rx_thr_length;
	u16 tx_thr_length;
};

/*
 * These Macros represents the bit fields in the Power and Clock Gating Control
 * Register. Read the register into the u32 member then set/clear the
 * bits using the bit elements.
 */
#define DWC_PCGCCTL_PHY_SUS_RD(x)		(((x) & (0x001 << 4)) >> 4)
#define DWC_PCGCCTL_RSTP_DWN_RD(x)		(((x) & (0x001 << 3)) >> 3)
#define DWC_PCGCCTL_PWR_CLAMP_RD(x)		(((x) & (0x001 << 2)) >> 2)
#define DWC_PCGCCTL_GATE_HCLK_RD(x)		(((x) & (0x001 << 1)) >> 1)
#define DWC_PCGCCTL_STOP_CLK_RD(x)		(((x) & (0x001 << 0)) >> 0)

#define DWC_PCGCCTL_RSTP_DWN_RW(reg, x)		\
	(((reg) & (~((u32)0x001 << 3))) | ((x) << 3))
#define DWC_PCGCCTL_PWR_CLAMP_RW(reg, x)	\
	(((reg) & (~((u32)0x001 << 2))) | ((x) << 2))
#define DWC_PCGCCTL_GATE_HCLK_RW(reg, x)	\
	(((reg) & (~((u32)0x001 << 1))) | ((x) << 1))
#define DWC_PCGCCTL_STOP_CLK_SET(reg)		\
	(((reg) | 1))
#define DWC_PCGCCTL_STOP_CLK_CLR(reg)		\
	(((reg) & (~((u32)0x001 << 0))))

/*
 * Host Mode Register Structures
 */

/*
 * The Host Global Registers structure defines the size and relative field
 * offsets for the Host Mode Global Registers.  Host Global Registers offsets
 * 400h-7FFh.
*/
#define DWC_HCFG		0x00
#define DWC_HFIR		0x04
#define DWC_HFNUM		0x08
#define DWC_HPTXSTS		0x10
#define DWC_HAINT		0x14
#define DWC_HAINTMSK		0x18

/*
 * These Macros represents the bit fields in the Host Configuration Register.
 * Read the register into the u32 member then set/clear the bits using the bit
 * elements. Write the u32 member to the hcfg register.
 */
#define DWC_HCFG_FSLSUPP_RD(x)		(((x) & (0x001 << 2)) >> 2)
#define DWC_HCFG_FSLSP_CLK_RD(x)	(((x) & (0x003 << 0)) >> 0)
#define DWC_HCFG_FSLSUPP_RW(reg, x)	\
	(((reg) & (~((u32)0x001 << 2))) | ((x) << 2))
#define DWC_HCFG_FSLSP_CLK_RW(reg, x)	\
	(((reg) & (~((u32)0x003 << 0))) | ((x) << 0))

#define DWC_HCFG_30_60_MHZ			0
#define DWC_HCFG_48_MHZ				1
#define DWC_HCFG_6_MHZ				2

/*
 * These Macros represents the bit fields in the Host Frame Remaing/Number
 * Register.
 */
#define DWC_HFIR_FRINT_RD(x)	(((x) & (0xffff << 0)) >> 0)
#define DWC_HFIR_FRINT_RW(reg, x)	\
	(((reg) & (~((u32)0xffff << 0))) | ((x) << 0))

/*
 * These Macros represents the bit fields in the Host Frame Remaing/Number
 * Register.
 */
#define DWC_HFNUM_FRREM_RD(x)		(((x) & (0xffff << 16)) >> 16)
#define DWC_HFNUM_FRNUM_RD(x)		(((x) & (0xffff << 0)) >> 0)
#define DWC_HFNUM_FRREM_RW(reg, x)	\
	(((reg) & (~((u32)0xffff << 16))) | ((x) << 16))
#define DWC_HFNUM_FRNUM_RW(reg, x)	\
	(((reg) & (~((u32)0xffff << 0))) | ((x) << 0))
#define DWC_HFNUM_MAX_FRNUM			0x3FFF
#define DWC_HFNUM_MAX_FRNUM			0x3FFF

#define DWC_HPTXSTS_PTXQTOP_ODD_RD(x)	(((x) & (0x01 << 31)) >> 31)
#define DWC_HPTXSTS_PTXQTOP_CHNUM_RD(x)	(((x) & (0x0f << 27)) >> 27)
#define DWC_HPTXSTS_PTXQTOP_TKN_RD(x)	(((x) & (0x03 << 25)) >> 25)
#define DWC_HPTXSTS_PTXQTOP_TERM_RD(x)	(((x) & (0x01 << 24)) >> 24)
#define DWC_HPTXSTS_PTXSPC_AVAIL_RD(x)	(((x) & (0xff << 16)) >> 16)
#define DWC_HPTXSTS_PTXFSPC_AVAIL_RD(x)	(((x) & (0xffff << 00)) >> 00)

/*
 * These Macros represents the bit fields in the Host Port Control and Status
 * Register. Read the register into the u32 member then set/clear the bits using
 * the bit elements. Write the u32 member to the hprt0 register.
 */
#define DWC_HPRT0_PRT_SPD_RD(x)		(((x) & (0x3 << 17)) >> 17)
#define DWC_HPRT0_PRT_TST_CTL_RD(x)	(((x) & (0xf << 13)) >> 13)
#define DWC_HPRT0_PRT_PWR_RD(x)		(((x) & (0x1 << 12)) >> 12)
#define DWC_HPRT0_PRT_LSTS_RD(x)	(((x) & (0x3 << 10)) >> 10)
#define DWC_HPRT0_PRT_RST_RD(x)		(((x) & (0x1 << 8)) >> 8)
#define DWC_HPRT0_PRT_SUS_RD(x)		(((x) & (0x1 << 7)) >> 7)
#define DWC_HPRT0_PRT_RES_RD(x)		(((x) & (0x1 << 6)) >> 6)
#define DWC_HPRT0_PRT_OVRCURR_CHG_RD(x)	(((x) & (0x1 << 5)) >> 5)
#define DWC_HPRT0_PRT_OVRCURR_ACT_RD(x)	(((x) & (0x1 << 4)) >> 4)
#define DWC_HPRT0_PRT_ENA_DIS_CHG_RD(x)	(((x) & (0x1 << 3)) >> 3)
#define DWC_HPRT0_PRT_ENA_RD(x)		(((x) & (0x1 << 2)) >> 2)
#define DWC_HPRT0_PRT_CONN_DET_RD(x)	(((x) & (0x1 << 1)) >> 1)
#define DWC_HPRT0_PRT_STS_RD(x)		(((x) & (0x1 << 0)) >> 0)

#define DWC_HPRT0_PRT_SPD_RW(reg, x)		\
	(((reg) & (~((u32)0x3 << 17))) | ((x) << 17))
#define DWC_HPRT0_PRT_TST_CTL_RW(reg, x)	\
	(((reg) & (~((u32)0xf << 13))) | ((x) << 13))
#define DWC_HPRT0_PRT_PWR_RW(reg, x)		\
	(((reg) & (~((u32)0x1 << 12))) | ((x) << 12))
#define DWC_HPRT0_PRT_LSTS_RW(reg, x)		\
	(((reg) & (~((u32)0x3 << 10))) | ((x) << 10))
#define DWC_HPRT0_PRT_RST_RW(reg, x)		\
	(((reg) & (~((u32)0x1 << 8))) | ((x) << 8))
#define DWC_HPRT0_PRT_SUS_RW(reg, x)		\
	(((reg) & (~((u32)0x1 << 7))) | ((x) << 7))
#define DWC_HPRT0_PRT_RES_RW(reg, x)		\
	(((reg) & (~((u32)0x1 << 6))) | ((x) << 6))
#define DWC_HPRT0_PRT_OVRCURR_CHG_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 5))) | ((x) << 5))
#define DWC_HPRT0_PRT_OVRCURR_ACT_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 4))) | ((x) << 4))
#define DWC_HPRT0_PRT_ENA_DIS_CHG_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 3))) | ((x) << 3))
#define DWC_HPRT0_PRT_ENA_RW(reg, x)		\
	(((reg) & (~((u32)0x1 << 2))) | ((x) << 2))
#define DWC_HPRT0_PRT_CONN_DET_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 1))) | ((x) << 1))
#define DWC_HPRT0_PRT_STS_RW(reg, x)		\
	(((reg) & (~((u32)0x1 << 0))) | ((x) << 0))

#define DWC_HPRT0_PRTSPD_HIGH_SPEED		0
#define DWC_HPRT0_PRTSPD_FULL_SPEED		1
#define DWC_HPRT0_PRTSPD_LOW_SPEED		2

/*
 * These Macros represents the bit fields in the Host All Interrupt Register.
 */
#define DWC_HAINT_CH15_RD(x)	(((x) & (0x1 << 15)) >> 15)
#define DWC_HAINT_CH14_RD(x)	(((x) & (0x1 << 14)) >> 14)
#define DWC_HAINT_CH13_RD(x)	(((x) & (0x1 << 13)) >> 13)
#define DWC_HAINT_CH12_RD(x)	(((x) & (0x1 << 12)) >> 12)
#define DWC_HAINT_CH11_RD(x)	(((x) & (0x1 << 11)) >> 11)
#define DWC_HAINT_CH10_RD(x)	(((x) & (0x1 << 10)) >> 10)
#define DWC_HAINT_CH09_RD(x)	(((x) & (0x1 << 9)) >> 9)
#define DWC_HAINT_CH08_RD(x)	(((x) & (0x1 << 8)) >> 8)
#define DWC_HAINT_CH07_RD(x)	(((x) & (0x1 << 7)) >> 7)
#define DWC_HAINT_CH06_RD(x)	(((x) & (0x1 << 6)) >> 6)
#define DWC_HAINT_CH05_RD(x)	(((x) & (0x1 << 5)) >> 5)
#define DWC_HAINT_CH04_RD(x)	(((x) & (0x1 << 4)) >> 4)
#define DWC_HAINT_CH03_RD(x)	(((x) & (0x1 << 3)) >> 3)
#define DWC_HAINT_CH02_RD(x)	(((x) & (0x1 << 2)) >> 2)
#define DWC_HAINT_CH01_RD(x)	(((x) & (0x1 << 1)) >> 1)
#define DWC_HAINT_CH00_RD(x)	(((x) & (0x1 << 0)) >> 0)

#define DWC_HAINT_RD(x)	(((x) & (0xffff << 0)) >> 0)

/*
 * These Macros represents the bit fields in the Host All Interrupt Register.
 */
#define DWC_HAINTMSK_CH15_RD(x)	(((x) & (0x1 << 15)) >> 15)
#define DWC_HAINTMSK_CH14_RD(x)	(((x) & (0x1 << 14)) >> 14)
#define DWC_HAINTMSK_CH13_RD(x)	(((x) & (0x1 << 13)) >> 13)
#define DWC_HAINTMSK_CH12_RD(x)	(((x) & (0x1 << 12)) >> 12)
#define DWC_HAINTMSK_CH11_RD(x)	(((x) & (0x1 << 11)) >> 11)
#define DWC_HAINTMSK_CH10_RD(x)	(((x) & (0x1 << 10)) >> 10)
#define DWC_HAINTMSK_CH09_RD(x)	(((x) & (0x1 << 9)) >> 9)
#define DWC_HAINTMSK_CH08_RD(x)	(((x) & (0x1 << 8)) >> 8)
#define DWC_HAINTMSK_CH07_RD(x)	(((x) & (0x1 << 7)) >> 7)
#define DWC_HAINTMSK_CH06_RD(x)	(((x) & (0x1 << 6)) >> 6)
#define DWC_HAINTMSK_CH05_RD(x)	(((x) & (0x1 << 5)) >> 5)
#define DWC_HAINTMSK_CH04_RD(x)	(((x) & (0x1 << 4)) >> 4)
#define DWC_HAINTMSK_CH03_RD(x)	(((x) & (0x1 << 3)) >> 3)
#define DWC_HAINTMSK_CH02_RD(x)	(((x) & (0x1 << 2)) >> 2)
#define DWC_HAINTMSK_CH01_RD(x)	(((x) & (0x1 << 1)) >> 1)
#define DWC_HAINTMSK_CH00_RD(x)	(((x) & (0x1 << 0)) >> 0)
#define DWC_HAINTMSK_RD(x)	((x) & 0xffff)

#define DWC_HAINTMSK_CH15_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 15))) | ((x) << 15))
#define DWC_HAINTMSK_CH14_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 14))) | ((x) << 14))
#define DWC_HAINTMSK_CH13_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 13))) | ((x) << 13))
#define DWC_HAINTMSK_CH12_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 12))) | ((x) << 12))
#define DWC_HAINTMSK_CH11_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 11))) | ((x) << 11))
#define DWC_HAINTMSK_CH10_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 10))) | ((x) << 10))
#define DWC_HAINTMSK_CH09_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 9))) | ((x) << 9))
#define DWC_HAINTMSK_CH08_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 8))) | ((x) << 8))
#define DWC_HAINTMSK_CH07_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 7))) | ((x) << 7))
#define DWC_HAINTMSK_CH06_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 6))) | ((x) << 6))
#define DWC_HAINTMSK_CH05_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 5))) | ((x) << 5))
#define DWC_HAINTMSK_CH04_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 4))) | ((x) << 4))
#define DWC_HAINTMSK_CH03_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 3))) | ((x) << 3))
#define DWC_HAINTMSK_CH02_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 2))) | ((x) << 2))
#define DWC_HAINTMSK_CH01_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 1))) | ((x) << 1))
#define DWC_HAINTMSK_CH00_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 0))) | ((x) << 0))
#define DWC_HAINTMSK_RW(reg, x)		\
	(((reg) & (~((u32)0xffff))) | x)

/*
 * Host Channel Specific Registers. 500h-5FCh
 */
#define DWC_HCCHAR	0x00
#define DWC_HCSPLT	0x04
#define DWC_HCINT	0x08
#define DWC_HCINTMSK	0x0C
#define DWC_HCTSIZ	0x10
#define DWC_HCDMA	0x14

/*
 * These Macros represents the bit fields in the Host Channel Characteristics
 * Register. Read the register into the u32 member then set/clear the bits using
 * the bit elements. Write the u32 member to the hcchar register.
 */
#define DWC_HCCHAR_ENA_RD(x)		(((x) & (0x001 << 31)) >> 31)
#define DWC_HCCHAR_DIS_RD(x)		(((x) & (0x001 << 30)) >> 30)
#define DWC_HCCHAR_ODD_FRAME_RD(x)	(((x) & (0x001 << 29)) >> 29)
#define DWC_HCCHAR_DEV_ADDR_RD(x)	(((x) & (0x07f << 22)) >> 22)
#define DWC_HCCHAR_MULTI_CNT_RD(x)	(((x) & (0x003 << 20)) >> 20)
#define DWC_HCCHAR_EPTYPE_RD(x)		(((x) & (0x003 << 18)) >> 18)
#define DWC_HCCHAR_LSP_DEV_RD(x)	(((x) & (0x001 << 17)) >> 17)
#define DWC_HCCHAR_EPDIR_RD(x)		(((x) & (0x001 << 15)) >> 15)
#define DWC_HCCHAR_EP_NUM_RD(x)	(((x) & (0x00f << 11)) >> 11)
#define DWC_HCCHAR_MPS_RD(x)		(((x) & (0x7ff << 0)) >> 0)

#define DWC_HCCHAR_ENA_RW(reg, x)		\
	(((reg) & (~((u32)0x001 << 31))) | ((x) << 31))
#define DWC_HCCHAR_DIS_RW(reg, x)		\
	(((reg) & (~((u32)0x001 << 30))) | ((x) << 30))
#define DWC_HCCHAR_ODD_FRAME_RW(reg, x)		\
	(((reg) & (~((u32)0x001 << 29))) | ((x) << 29))
#define DWC_HCCHAR_DEV_ADDR_RW(reg, x)		\
	(((reg) & (~((u32)0x07f << 22))) | ((x) << 22))
#define DWC_HCCHAR_MULTI_CNT_RW(reg, x)		\
	(((reg) & (~((u32)0x003 << 20))) | ((x) << 20))
#define DWC_HCCHAR_EPTYPE_RW(reg, x)		\
	(((reg) & (~((u32)0x003 << 18))) | ((x) << 18))
#define DWC_HCCHAR_LSP_DEV_RW(reg, x)		\
	(((reg) & (~((u32)0x001 << 17))) | ((x) << 17))
#define DWC_HCCHAR_EPDIR_RW(reg, x)		\
	(((reg) & (~((u32)0x001 << 15))) | ((x) << 15))
#define DWC_HCCHAR_EP_NUM_RW(reg, x)		\
	(((reg) & (~((u32)0x00f << 11))) | ((x) << 11))
#define DWC_HCCHAR_MPS_RW(reg, x)		\
	(((reg) & (~((u32)0x7ff << 0))) | ((x) << 0))

#define DWC_HCSPLT_ENA_RD(x)		(((x) & (0x01 << 31)) >> 31)
#define DWC_HCSPLT_COMP_SPLT_RD(x)	(((x) & (0x01 << 16)) >> 16)
#define DWC_HCSPLT_TRANS_POS_RD(x)	(((x) & (0x03 << 14)) >> 14)
#define DWC_HCSPLT_HUB_ADDR_RD(x)	(((x) & (0x7f << 7)) >> 7)
#define DWC_HCSPLT_PRT_ADDR_RD(x)	(((x) & (0x7f << 0)) >> 0)

#define DWC_HCSPLT_ENA_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 31))) | ((x) << 31))
#define DWC_HCSPLT_COMP_SPLT_RW(reg, x)	\
	(((reg) & (~((u32)0x01 << 16))) | ((x) << 16))
#define DWC_HCSPLT_TRANS_POS_RW(reg, x)	\
	(((reg) & (~((u32)0x03 << 14))) | ((x) << 14))
#define DWC_HCSPLT_HUB_ADDR_RW(reg, x)	\
	(((reg) & (~((u32)0x7f << 7))) | ((x) << 7))
#define DWC_HCSPLT_PRT_ADDR_RW(reg, x)	\
	(((reg) & (~((u32)0x7f << 0))) | ((x) << 0))

#define DWC_HCSPLIT_XACTPOS_MID			0
#define DWC_HCSPLIT_XACTPOS_END			1
#define DWC_HCSPLIT_XACTPOS_BEGIN		2
#define DWC_HCSPLIT_XACTPOS_ALL			3

/*
 * These Macros represents the bit fields in the Host All Interrupt
 * Register.
 */
#define DWC_HCINT_DATA_TOG_ERR_RD(x)		(((x) & (0x1 << 10)) >> 10)
#define DWC_HCINT_FRAME_OVERN_ERR_RD(x)		(((x) & (0x1 << 9)) >> 9)
#define DWC_HCINT_BBL_ERR_RD(x)			(((x) & (0x1 << 8)) >> 8)
#define DWC_HCINT_TRANS_ERR_RD(x)		(((x) & (0x1 << 7)) >> 7)
#define DWC_HCINT_NYET_RESP_REC_RD(x)		(((x) & (0x1 << 6)) >> 6)
#define DWC_HCINT_ACK_RESP_REC_RD(x)		(((x) & (0x1 << 5)) >> 5)
#define DWC_HCINT_NAK_RESP_REC_RD(x)		(((x) & (0x1 << 4)) >> 4)
#define DWC_HCINT_STALL_RESP_REC_RD(x)		(((x) & (0x1 << 3)) >> 3)
#define DWC_HCINT_AHB_ERR_RD(x)			(((x) & (0x1 << 2)) >> 2)
#define DWC_HCINT_CHAN_HALTED_RD(x)		(((x) & (0x1 << 1)) >> 1)
#define DWC_HCINT_TXFER_CMPL_RD(x)		(((x) & (0x1 << 0)) >> 0)

#define DWC_HCINT_DATA_TOG_ERR_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 10))) | ((x) << 10))
#define DWC_HCINT_FRAME_OVERN_ERR_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 9))) | ((x) << 9))
#define DWC_HCINT_BBL_ERR_RW(reg, x)		\
	(((reg) & (~((u32)0x1 << 8))) | ((x) << 8))
#define DWC_HCINT_TRANS_ERR_RW(reg, x)		\
	(((reg) & (~((u32)0x1 << 7))) | ((x) << 7))
#define DWC_HCINT_NYET_RESP_REC_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 6))) | ((x) << 6))
#define DWC_HCINT_ACK_RESP_REC_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 5))) | ((x) << 5))
#define DWC_HCINT_NAK_RESP_REC_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 4))) | ((x) << 4))
#define DWC_HCINT_STALL_RESP_REC_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 3))) | ((x) << 3))
#define DWC_HCINT_AHB_ERR_RW(reg, x)		\
	(((reg) & (~((u32)0x1 << 2))) | ((x) << 2))
#define DWC_HCINT_CHAN_HALTED_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 1))) | ((x) << 1))
#define DWC_HCINT_TXFER_CMPL_RW(reg, x)		\
	(((reg) & (~((u32)0x1 << 0))) | ((x) << 0))

/*
 * These Macros represents the bit fields in the Host Channel Transfer Size
 * Register. Read the register into the u32 member then set/clear the  bits
 * using the bit elements. Write the u32 member to the hcchar register.
 */
#define DWC_HCTSIZ_DO_PING_PROTO_RD(x)	(((x) & (0x00001 << 31)) >> 31)
#define DWC_HCTSIZ_PKT_PID_RD(x)	(((x) & (0x00003 << 29)) >> 29)
#define DWC_HCTSIZ_PKT_CNT_RD(x)	(((x) & (0x003ff << 19)) >> 19)
#define DWC_HCTSIZ_XFER_SIZE_RD(x)	(((x) & (0x7ffff << 00)) >> 00)

#define DWC_HCTSIZ_DO_PING_PROTO_RW(reg, x)	\
	(((reg) & (~((u32)0x00001 << 31))) | ((x) << 31))
#define DWC_HCTSIZ_PKT_PID_RW(reg, x)	\
	(((reg) & (~((u32)0x00003 << 29))) | ((x) << 29))
#define DWC_HCTSIZ_PKT_CNT_RW(reg, x)	\
	(((reg) & (~((u32)0x003ff << 19))) | ((x) << 19))
#define DWC_HCTSIZ_XFER_SIZE_RW(reg, x)	\
	(((reg) & (~((u32)0x7ffff << 00))) | ((x) << 00))

#define DWC_HCTSIZ_DATA0			0
#define DWC_HCTSIZ_DATA1			2
#define DWC_HCTSIZ_DATA2			1
#define DWC_HCTSIZ_MDATA			3
#define DWC_HCTSIZ_SETUP			3

/*
 * These Macros represents the bit fields in the Host Channel Interrupt Mask
 * Register. Read the register into the u32 member then set/clear the bits using
 * the bit elements. Write the u32 member to the hcintmsk register.
 */
#define DWC_HCINTMSK_DATA_TOG_ERR_RD(x)		(((x) & (0x1 << 10)) >> 10)
#define DWC_HCINTMSK_FRAME_OVERN_ERR_RD(x)	(((x) & (0x1 << 9)) >> 9)
#define DWC_HCINTMSK_BBL_ERR_RD(x)		(((x) & (0x1 << 8)) >> 8)
#define DWC_HCINTMSK_TRANS_ERR_RD(x)		(((x) & (0x1 << 7)) >> 7)
#define DWC_HCINTMSK_NYET_RESP_REC_RD(x)	(((x) & (0x1 << 6)) >> 6)
#define DWC_HCINTMSK_ACK_RESP_REC_RD(x)		(((x) & (0x1 << 5)) >> 5)
#define DWC_HCINTMSK_NAK_RESP_REC_RD(x)		(((x) & (0x1 << 4)) >> 4)
#define DWC_HCINTMSK_STALL_RESP_REC_RD(x)	(((x) & (0x1 << 3)) >> 3)
#define DWC_HCINTMSK_AHB_ERR_RD(x)		(((x) & (0x1 << 2)) >> 2)
#define DWC_HCINTMSK_CHAN_HALTED_RD(x)		(((x) & (0x1 << 1)) >> 1)
#define DWC_HCINTMSK_TXFER_CMPL_RD(x)		(((x) & (0x1 << 0)) >> 0)

#define DWC_HCINTMSK_DATA_TOG_ERR_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 10))) | ((x) << 10))
#define DWC_HCINTMSK_FRAME_OVERN_ERR_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 9))) | ((x) << 9))
#define DWC_HCINTMSK_BBL_ERR_RW(reg, x)		\
	(((reg) & (~((u32)0x1 << 8))) | ((x) << 8))
#define DWC_HCINTMSK_TRANS_ERR_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 7))) | ((x) << 7))
#define DWC_HCINTMSK_NYET_RESP_REC_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 6))) | ((x) << 6))
#define DWC_HCINTMSK_ACK_RESP_REC_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 5))) | ((x) << 5))
#define DWC_HCINTMSK_NAK_RESP_REC_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 4))) | ((x) << 4))
#define DWC_HCINTMSK_STALL_RESP_REC_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 3))) | ((x) << 3))
#define DWC_HCINTMSK_AHB_ERR_RW(reg, x)		\
	(((reg) & (~((u32)0x1 << 2))) | ((x) << 2))
#define DWC_HCINTMSK_CHAN_HALTED_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 1))) | ((x) << 1))
#define DWC_HCINTMSK_TXFER_CMPL_RW(reg, x)	\
	(((reg) & (~((u32)0x1 << 0))) | ((x) << 0))

/*
 * OTG Host Interface Structure.
 *
 * The OTG Host Interface Structure structure contains information needed to
 * manage the DWC_otg controller acting in host mode. It represents the
 * programming view of the host-specific aspects of the controller.
 */
struct dwc_host_if {		/* CONFIG_DWC_OTG_REG_LE */
	/* Host Global Registers starting at offset 400h. */
	ulong host_global_regs;
#define DWC_OTG_HOST_GLOBAL_REG_OFFSET		0x400

	/* Host Port 0 Control and Status Register */
	ulong hprt0;
#define DWC_OTG_HOST_PORT_REGS_OFFSET		0x440

	/* Host Channel Specific Registers at offsets 500h-5FCh. */
	ulong hc_regs[MAX_EPS_CHANNELS];
#define DWC_OTG_HOST_CHAN_REGS_OFFSET		0x500
#define DWC_OTG_CHAN_REGS_OFFSET		0x20

	/* Host configuration information */
	/* Number of Host Channels (range: 1-16) */
	u8 num_host_channels;
	/* Periodic EPs supported (0: no, 1: yes) */
	u8 perio_eps_supported;
	/* Periodic Tx FIFO Size (Only 1 host periodic Tx FIFO) */
	u16 perio_tx_fifo_size;
};
#endif
