/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ARCH_PXA3xx_U2D_H
#define __ASM_ARCH_PXA3xx_U2D_H

#include <mach/bitfield.h>

/*
 * USB2 device controller registers and bits definitions
 */
#define U2DCR		(0x0000)	/* U2D Control Register */
#define U2DCR_NDC	(1   << 31)	/* NAK During Config */
#define U2DCR_HSTC	(0x7 << 28)	/* High Speed Timeout Calibration */
#define U2DCR_SPEOREN	(1   << 27)	/* Short Packet EOR INTR generation Enable */
#define U2DCR_FSTC	(0x7 << 24)	/* Full Speed Timeout Calibration */
#define U2DCR_UCLKOVR	(1   << 22)	/* UTM Clock Override */
#define U2DCR_ABP	(1   << 21)	/* Application Bus Power */
#define U2DCR_ADD	(1   << 20)	/* Application Device Disconnect */
#define U2DCR_CC	(1   << 19)	/* Configuration Change */
#define U2DCR_HS	(1   << 18)	/* High Speed USB Detection */
#define U2DCR_SMAC	(1   << 17)	/* Switch Endpoint Memory to Active Configuration */
#define U2DCR_DWRE	(1   << 16)	/* Device Remote Wake-up Feature */
#define U2DCR_ACN	(0xf << 12)	/* Active U2D Configuration Number */
#define U2DCR_AIN	(0xf << 8)	/* Active U2D Interface Number */
#define U2DCR_AAISN	(0xf << 4)	/* Active U2D Alternate Interface Setting Number */
#define U2DCR_EMCE	(1   << 3)	/* Endpoint Memory Configuration Error */
#define U2DCR_UDR	(1   << 2)	/* U2D Resume */
#define U2DCR_UDA	(1   << 1)	/* U2D Active */
#define U2DCR_UDE	(1   << 0)	/* U2D Enable */

#define U2DICR			(0x0004)	/* U2D Interrupt Control Register */
#define U2DISR			(0x000C)	/* U2D Interrupt Status Register */
#define U2DINT_CC		(1 << 31)	/* Interrupt - Configuration Change */
#define U2DINT_SOF		(1 << 30)	/* Interrupt - SOF */
#define U2DINT_USOF		(1 << 29)	/* Interrupt - micro SOF */
#define U2DINT_RU		(1 << 28)	/* Interrupt - Resume */
#define U2DINT_SU		(1 << 27)	/* Interrupt - Suspend */
#define U2DINT_RS		(1 << 26)	/* Interrupt - Reset */
#define U2DINT_DPE		(1 << 25)	/* Interrupt - Data Packet Error */
#define U2DINT_FIFOERR		(0x4)		/* Interrupt - endpoint FIFO error */
#define U2DINT_PACKETCMP	(0x2)		/* Interrupt - endpoint packet complete */
#define U2DINT_SPACKETCMP	(0x1)		/* Interrupt - endpoint short packet complete */

#define U2DFNR			(0x0014)	/* U2D Frame Number Register */

#define U2DINT(n, intr)		(((intr) & 0x07) << (((n) & 0x07) * 3))
#define U2DICR2			(0x0008)	/* U2D Interrupt Control Register 2 */
#define U2DISR2			(0x0010)	/* U2D Interrupt Status Register 2 */

#define U2DOTGCR		(0x0020)	/* U2D OTG Control Register */
#define U2DOTGCR_OTGEN		(1 << 31)	/* On-The-Go Enable */
#define U2DOTGCR_AALTHNP	(1 << 30)	/* A-device Alternate Host Negotiation Protocal Port Support */
#define U2DOTGCR_AHNP		(1 << 29)	/* A-device Host Negotiation Protocal Support */
#define U2DOTGCR_BHNP		(1 << 28)	/* B-device Host Negotiation Protocal Enable */

#ifdef CONFIG_CPU_PXA930
#define U2DOTGCR_LPA		(1 << 15)	/* ULPI low power mode active */
#define U2DOTGCR_IESI		(1 << 13)	/* OTG interrupt Enable */
#define U2DOTGCR_ISSI		(1 << 12)	/* OTG interrupt status */
#endif

#define U2DOTGCR_CKAF	(1 << 5)	/* Carkit Mode Alternate Function Select */
#define U2DOTGCR_UTMID	(1 << 4)	/* UTMI Interface Disable */
#define U2DOTGCR_ULAF	(1 << 3)	/* ULPI Mode Alternate Function Select */
#define U2DOTGCR_SMAF	(1 << 2)	/* Serial Mode Alternate Function Select */
#define U2DOTGCR_RTSM	(1 << 1)	/* Return to Synchronous Mode (ULPI Mode) */
#define U2DOTGCR_ULE	(1 << 0)	/* ULPI Wrapper Enable */

#define U2DOTGICR	(0x0024)	/* U2D OTG Interrupt Control Register */
#define U2DOTGISR	(0x0028)	/* U2D OTG Interrupt Status Register */

#define U2DOTGINT_SF	(1 << 17)	/* OTG Set Feature Command Received */
#define U2DOTGINT_SI	(1 << 16)	/* OTG Interrupt */
#define U2DOTGINT_RLS1	(1 << 14)	/* RXCMD Linestate[1] Change Interrupt Rise */
#define U2DOTGINT_RLS0	(1 << 13)	/* RXCMD Linestate[0] Change Interrupt Rise */
#define U2DOTGINT_RID	(1 << 12)	/* RXCMD OTG ID Change Interrupt Rise */
#define U2DOTGINT_RSE	(1 << 11)	/* RXCMD OTG Session End Interrupt Rise */
#define U2DOTGINT_RSV	(1 << 10)	/* RXCMD OTG Session Valid Interrupt Rise */
#define U2DOTGINT_RVV	(1 << 9)	/* RXCMD OTG Vbus Valid Interrupt Rise */
#define U2DOTGINT_RCK	(1 << 8)	/* RXCMD Carkit Interrupt Rise */
#define U2DOTGINT_FLS1	(1 << 6)	/* RXCMD Linestate[1] Change Interrupt Fall */
#define U2DOTGINT_FLS0	(1 << 5)	/* RXCMD Linestate[0] Change Interrupt Fall */
#define U2DOTGINT_FID	(1 << 4)	/* RXCMD OTG ID Change Interrupt Fall */
#define U2DOTGINT_FSE	(1 << 3)	/* RXCMD OTG Session End Interrupt Fall */
#define U2DOTGINT_FSV	(1 << 2)	/* RXCMD OTG Session Valid Interrupt Fall */
#define U2DOTGINT_FVV	(1 << 1)	/* RXCMD OTG Vbus Valid Interrupt Fall */
#define U2DOTGINT_FCK	(1 << 0)	/* RXCMD Carkit Interrupt Fall */

#define U2DOTGUSR	(0x002C)	/* U2D OTG ULPI Status Register */
#define U2DOTGUSR_LPA	(1 << 31)	/* ULPI Low Power Mode Active */
#define U2DOTGUSR_S6A	(1 << 30)	/* ULPI Serial Mode (6-pin) Active */
#define U2DOTGUSR_S3A	(1 << 29)	/* ULPI Serial Mode (3-pin) Active */
#define U2DOTGUSR_CKA	(1 << 28)	/* ULPI Car Kit Mode Active */
#define U2DOTGUSR_LS1	(1 << 6)	/* RXCMD Linestate 1 Status */
#define U2DOTGUSR_LS0	(1 << 5)	/* RXCMD Linestate 0 Status */
#define U2DOTGUSR_ID	(1 << 4)	/* OTG IDGnd Status */
#define U2DOTGUSR_SE	(1 << 3)	/* OTG Session End Status */
#define U2DOTGUSR_SV	(1 << 2)	/* OTG Session Valid Status */
#define U2DOTGUSR_VV	(1 << 1)	/* OTG Vbus Valid Status */
#define U2DOTGUSR_CK	(1 << 0)	/* Carkit Interrupt Status */

#define U2DOTGUCR	(0x0030)	/* U2D OTG ULPI Control Register */
#define U2DOTGUCR_RUN	(1    << 25)	/* RUN */
#define U2DOTGUCR_RNW	(1    << 24)	/* Read or Write operation */
#define U2DOTGUCR_ADDR	(0x3f << 16)	/* Address of the ULPI PHY register */
#define U2DOTGUCR_WDATA	(0xff << 8)	/* The data for a WRITE command */
#define U2DOTGUCR_RDATA	(0xff << 0)	/* The data for a READ command */

#define U2DP3CR		(0x0034)	/* U2D Port 3 Control Register */
#define U2DP3CR_P2SS	(0x3 << 8)	/* Host Port 2 Serial Mode Select */
#define U2DP3CR_P3SS	(0x7 << 4)	/* Host Port 3 Serial Mode Select */
#define U2DP3CR_VPVMBEN	(0x1 << 2)	/* Host Port 3 Vp/Vm Block Enable */
#define U2DP3CR_CFG	(0x3 << 0)	/* Host Port 3 Configuration */

#define U2DCSR0		(0x0100)	/* U2D Control/Status Register - Endpoint 0 */
#define U2DCSR0_IPA	(1 << 8)	/* IN Packet Adjusted */
#define U2DCSR0_SA	(1 << 7)	/* SETUP Active */
#define U2DCSR0_RNE	(1 << 6)	/* Receive FIFO Not Empty */
#define U2DCSR0_FST	(1 << 5)	/* Force Stall */
#define U2DCSR0_SST	(1 << 4)	/* Send Stall */
#define U2DCSR0_DME	(1 << 3)	/* DMA Enable */
#define U2DCSR0_FTF	(1 << 2)	/* Flush Transmit FIFO */
#define U2DCSR0_IPR	(1 << 1)	/* IN Packet Ready */
#define U2DCSR0_OPC	(1 << 0)	/* OUT Packet Complete */

#define U2DCSR(x)	(0x0100 + ((x) << 2))	/* U2D Control/Status Register - Endpoint x */
#define U2DCSR_BF	(1 << 10)	/* Buffer Full, for OUT eps */
#define U2DCSR_BE	(1 << 10)	/* Buffer Empty, for IN eps */
#define U2DCSR_DPE	(1 << 9)	/* Data Packet Error, for ISO eps only */
#define U2DCSR_FEF	(1 << 8)	/* Flush Endpoint FIFO */
#define U2DCSR_SP	(1 << 7)	/* Short Packet Control/Status, for OUT eps only, readonly */
#define U2DCSR_BNE	(1 << 6)	/* Buffer Not Empty, for OUT eps */
#define U2DCSR_BNF	(1 << 6)	/* Buffer Not Full, for IN eps */
#define U2DCSR_FST	(1 << 5)	/* Force STALL, write 1 set */
#define U2DCSR_SST	(1 << 4)	/* Sent STALL, write 1 clear */
#define U2DCSR_DME	(1 << 3)	/* DMA Enable */
#define U2DCSR_TRN	(1 << 2)	/* Tx/Rx NAK, write 1 clear */
#define U2DCSR_PC	(1 << 1)	/* Packet Complete, write 1 clear */
#define U2DCSR_FS	(1 << 0)	/* FIFO needs Service */

#define U2DBCR0		(0x0200)		/* U2D Byte Count Register - Endpoint 0 */
#define U2DBCR(x)	(0x0200 + ((x) << 2))	/* U2D Byte Count Register - Endpoint x */

#define U2DDR0		(0x0300)		/* U2D Data Register - Endpoint 0 */

#define U2DEPCR(x)	(0x0400 + ((x) << 2))	/* U2D Configuration Register - Endpoint x */
#define U2DEPCR_EE	(1 << 0)		/* Endpoint Enable */
#define U2DEPCR_BS_MASK	(0x3FE)			/* Buffer Size, BS*8=FIFO size, max 8184B = 8KB */

#define U2DSCA		(0x0500)		/* U2D Setup Command Address */
#define U2DSCA_VALUE	(0x0120)

#define U2DEN0		(0x0504)		/* U2D Endpoint Information Register - Endpoint 0 */
#define U2DEN(x)	(0x0504 + ((x) << 2))	/* U2D Endpoint Information Register - Endpoint x */

/* U2DMA registers */
#define U2DMACSR0		(0x1000)	/* U2DMA Control/Status Register - Channel 0 */
#define U2DMACSR(x)		(0x1000 + ((x) << 2))	/* U2DMA Control/Status Register - Channel x */
#define U2DMACSR_RUN		(1 << 31)	/* Run Bit (read / write) */
#define U2DMACSR_STOPIRQEN	(1 << 29)	/* Stop Interrupt Enable (read / write) */
#define U2DMACSR_EORIRQEN	(1 << 28)	/* End of Receive Interrupt Enable (R/W) */
#define U2DMACSR_EORJMPEN	(1 << 27)	/* Jump to next descriptor on EOR */
#define U2DMACSR_EORSTOPEN	(1 << 26)	/* STOP on an EOR */
#define U2DMACSR_RASIRQEN	(1 << 23)	/* Request After Cnannel Stopped Interrupt Enable */
#define U2DMACSR_MASKRUN	(1 << 22)	/* Mask Run */
#define U2DMACSR_SCEMC		(3 << 18)	/* System Bus Split Completion Error Message Class */
#define U2DMACSR_SCEMI		(0x1f << 13)	/* System Bus Split Completion Error Message Index */
#define U2DMACSR_BUSERRTYPE	(7 << 10)	/* PX Bus Error Type */
#define U2DMACSR_EORINTR	(1 << 9)	/* End Of Receive */
#define U2DMACSR_REQPEND	(1 << 8)	/* Request Pending */
#define U2DMACSR_RASINTR	(1 << 4)	/* Request After Channel Stopped (read / write 1 clear) */
#define U2DMACSR_STOPINTR	(1 << 3)	/* Stop Interrupt (read only) */
#define U2DMACSR_ENDINTR	(1 << 2)	/* End Interrupt (read / write 1 clear) */
#define U2DMACSR_STARTINTR	(1 << 1)	/* Start Interrupt (read / write 1 clear) */
#define U2DMACSR_BUSERRINTR	(1 << 0)	/* Bus Error Interrupt (read / write 1 clear) */

#define U2DMACR		(0x1080)		/* U2DMA Control Register */
#define U2DMAINT	(0x10F0)		/* U2DMA Interrupt Register */

#define U2DMABR0	(0x1100)		/* U2DMA Branch Register - Channel 0 */
#define U2DMABR(x)      (0x1100 + (x) << 2)	/* U2DMA Branch Register - Channel x */

#define U2DMADADR0      (0x1200)		/* U2DMA Descriptor Address Register - Channel 0 */
#define U2DMADADR(x)    (0x1200 + (x) * 0x10)	/* U2DMA Descriptor Address Register - Channel x */

#define U2DMADADR_STOP	(1U << 0)

#define U2DMASADR0	(0x1204)		/* U2DMA Source Address Register - Channel 0 */
#define U2DMASADR(x)	(0x1204 + (x) * 0x10)	/* U2DMA Source Address Register - Channel x */
#define U2DMATADR0	(0x1208)		/* U2DMA Target Address Register - Channel 0 */
#define U2DMATADR(x)	(0x1208 + (x) * 0x10)	/* U2DMA Target Address Register - Channel x */

#define U2DMACMDR0	(0x120C)		/* U2DMA Command Address Register - Channel 0 */
#define U2DMACMDR(x)	(0x120C + (x) * 0x10)	/* U2DMA Command Address Register - Channel x */

#define U2DMACMDR_XFRDIS	(1 << 31)	/* Transfer Direction */
#define U2DMACMDR_STARTIRQEN	(1 << 22)	/* Start Interrupt Enable */
#define U2DMACMDR_ENDIRQEN	(1 << 21)	/* End Interrupt Enable */
#define U2DMACMDR_PACKCOMP	(1 << 13)	/* Packet Complete */
#define U2DMACMDR_LEN		(0x07ff)	/* length mask (max = 2K - 1) */

#endif /* __ASM_ARCH_PXA3xx_U2D_H */
