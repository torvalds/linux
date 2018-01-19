/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARCH_PXA27X_UDC_H
#define _ASM_ARCH_PXA27X_UDC_H

#ifdef _ASM_ARCH_PXA25X_UDC_H
#error You cannot include both PXA25x and PXA27x UDC support
#endif

#define UDCCR           __REG(0x40600000) /* UDC Control Register */
#define UDCCR_OEN	(1 << 31)	/* On-the-Go Enable */
#define UDCCR_AALTHNP	(1 << 30)	/* A-device Alternate Host Negotiation
					   Protocol Port Support */
#define UDCCR_AHNP	(1 << 29)	/* A-device Host Negotiation Protocol
					   Support */
#define UDCCR_BHNP	(1 << 28)	/* B-device Host Negotiation Protocol
					   Enable */
#define UDCCR_DWRE	(1 << 16)	/* Device Remote Wake-up Enable */
#define UDCCR_ACN	(0x03 << 11)	/* Active UDC configuration Number */
#define UDCCR_ACN_S	11
#define UDCCR_AIN	(0x07 << 8)	/* Active UDC interface Number */
#define UDCCR_AIN_S	8
#define UDCCR_AAISN	(0x07 << 5)	/* Active UDC Alternate Interface
					   Setting Number */
#define UDCCR_AAISN_S	5
#define UDCCR_SMAC	(1 << 4)	/* Switch Endpoint Memory to Active
					   Configuration */
#define UDCCR_EMCE	(1 << 3)	/* Endpoint Memory Configuration
					   Error */
#define UDCCR_UDR	(1 << 2)	/* UDC Resume */
#define UDCCR_UDA	(1 << 1)	/* UDC Active */
#define UDCCR_UDE	(1 << 0)	/* UDC Enable */

#define UDCICR0         __REG(0x40600004) /* UDC Interrupt Control Register0 */
#define UDCICR1         __REG(0x40600008) /* UDC Interrupt Control Register1 */
#define UDCICR_FIFOERR	(1 << 1)	/* FIFO Error interrupt for EP */
#define UDCICR_PKTCOMPL (1 << 0)	/* Packet Complete interrupt for EP */

#define UDC_INT_FIFOERROR  (0x2)
#define UDC_INT_PACKETCMP  (0x1)

#define UDCICR_INT(n,intr) (((intr) & 0x03) << (((n) & 0x0F) * 2))
#define UDCICR1_IECC	(1 << 31)	/* IntEn - Configuration Change */
#define UDCICR1_IESOF	(1 << 30)	/* IntEn - Start of Frame */
#define UDCICR1_IERU	(1 << 29)	/* IntEn - Resume */
#define UDCICR1_IESU	(1 << 28)	/* IntEn - Suspend */
#define UDCICR1_IERS	(1 << 27)	/* IntEn - Reset */

#define UDCISR0         __REG(0x4060000C) /* UDC Interrupt Status Register 0 */
#define UDCISR1         __REG(0x40600010) /* UDC Interrupt Status Register 1 */
#define UDCISR_INT(n,intr) (((intr) & 0x03) << (((n) & 0x0F) * 2))
#define UDCISR1_IRCC	(1 << 31)	/* IntReq - Configuration Change */
#define UDCISR1_IRSOF	(1 << 30)	/* IntReq - Start of Frame */
#define UDCISR1_IRRU	(1 << 29)	/* IntReq - Resume */
#define UDCISR1_IRSU	(1 << 28)	/* IntReq - Suspend */
#define UDCISR1_IRRS	(1 << 27)	/* IntReq - Reset */

#define UDCFNR          __REG(0x40600014) /* UDC Frame Number Register */
#define UDCOTGICR	__REG(0x40600018) /* UDC On-The-Go interrupt control */
#define UDCOTGICR_IESF	(1 << 24)	/* OTG SET_FEATURE command recvd */
#define UDCOTGICR_IEXR	(1 << 17)	/* Extra Transceiver Interrupt
					   Rising Edge Interrupt Enable */
#define UDCOTGICR_IEXF	(1 << 16)	/* Extra Transceiver Interrupt
					   Falling Edge Interrupt Enable */
#define UDCOTGICR_IEVV40R (1 << 9)	/* OTG Vbus Valid 4.0V Rising Edge
					   Interrupt Enable */
#define UDCOTGICR_IEVV40F (1 << 8)	/* OTG Vbus Valid 4.0V Falling Edge
					   Interrupt Enable */
#define UDCOTGICR_IEVV44R (1 << 7)	/* OTG Vbus Valid 4.4V Rising Edge
					   Interrupt Enable */
#define UDCOTGICR_IEVV44F (1 << 6)	/* OTG Vbus Valid 4.4V Falling Edge
					   Interrupt Enable */
#define UDCOTGICR_IESVR	(1 << 5)	/* OTG Session Valid Rising Edge
					   Interrupt Enable */
#define UDCOTGICR_IESVF	(1 << 4)	/* OTG Session Valid Falling Edge
					   Interrupt Enable */
#define UDCOTGICR_IESDR	(1 << 3)	/* OTG A-Device SRP Detect Rising
					   Edge Interrupt Enable */
#define UDCOTGICR_IESDF	(1 << 2)	/* OTG A-Device SRP Detect Falling
					   Edge Interrupt Enable */
#define UDCOTGICR_IEIDR	(1 << 1)	/* OTG ID Change Rising Edge
					   Interrupt Enable */
#define UDCOTGICR_IEIDF	(1 << 0)	/* OTG ID Change Falling Edge
					   Interrupt Enable */

#define UP2OCR		  __REG(0x40600020)  /* USB Port 2 Output Control register */
#define UP3OCR		  __REG(0x40600024)  /* USB Port 2 Output Control register */

#define UP2OCR_CPVEN	(1 << 0)	/* Charge Pump Vbus Enable */
#define UP2OCR_CPVPE	(1 << 1)	/* Charge Pump Vbus Pulse Enable */
#define UP2OCR_DPPDE	(1 << 2)	/* Host Port 2 Transceiver D+ Pull Down Enable */
#define UP2OCR_DMPDE	(1 << 3)	/* Host Port 2 Transceiver D- Pull Down Enable */
#define UP2OCR_DPPUE	(1 << 4)	/* Host Port 2 Transceiver D+ Pull Up Enable */
#define UP2OCR_DMPUE	(1 << 5)	/* Host Port 2 Transceiver D- Pull Up Enable */
#define UP2OCR_DPPUBE	(1 << 6)	/* Host Port 2 Transceiver D+ Pull Up Bypass Enable */
#define UP2OCR_DMPUBE	(1 << 7)	/* Host Port 2 Transceiver D- Pull Up Bypass Enable */
#define UP2OCR_EXSP		(1 << 8)	/* External Transceiver Speed Control */
#define UP2OCR_EXSUS	(1 << 9)	/* External Transceiver Speed Enable */
#define UP2OCR_IDON		(1 << 10)	/* OTG ID Read Enable */
#define UP2OCR_HXS		(1 << 16)	/* Host Port 2 Transceiver Output Select */
#define UP2OCR_HXOE		(1 << 17)	/* Host Port 2 Transceiver Output Enable */
#define UP2OCR_SEOS(x)		((x & 7) << 24)	/* Single-Ended Output Select */

#define UDCCSN(x)	__REG2(0x40600100, (x) << 2)
#define UDCCSR0         __REG(0x40600100) /* UDC Control/Status register - Endpoint 0 */
#define UDCCSR0_SA	(1 << 7)	/* Setup Active */
#define UDCCSR0_RNE	(1 << 6)	/* Receive FIFO Not Empty */
#define UDCCSR0_FST	(1 << 5)	/* Force Stall */
#define UDCCSR0_SST	(1 << 4)	/* Sent Stall */
#define UDCCSR0_DME	(1 << 3)	/* DMA Enable */
#define UDCCSR0_FTF	(1 << 2)	/* Flush Transmit FIFO */
#define UDCCSR0_IPR	(1 << 1)	/* IN Packet Ready */
#define UDCCSR0_OPC	(1 << 0)	/* OUT Packet Complete */

#define UDCCSRA         __REG(0x40600104) /* UDC Control/Status register - Endpoint A */
#define UDCCSRB         __REG(0x40600108) /* UDC Control/Status register - Endpoint B */
#define UDCCSRC         __REG(0x4060010C) /* UDC Control/Status register - Endpoint C */
#define UDCCSRD         __REG(0x40600110) /* UDC Control/Status register - Endpoint D */
#define UDCCSRE         __REG(0x40600114) /* UDC Control/Status register - Endpoint E */
#define UDCCSRF         __REG(0x40600118) /* UDC Control/Status register - Endpoint F */
#define UDCCSRG         __REG(0x4060011C) /* UDC Control/Status register - Endpoint G */
#define UDCCSRH         __REG(0x40600120) /* UDC Control/Status register - Endpoint H */
#define UDCCSRI         __REG(0x40600124) /* UDC Control/Status register - Endpoint I */
#define UDCCSRJ         __REG(0x40600128) /* UDC Control/Status register - Endpoint J */
#define UDCCSRK         __REG(0x4060012C) /* UDC Control/Status register - Endpoint K */
#define UDCCSRL         __REG(0x40600130) /* UDC Control/Status register - Endpoint L */
#define UDCCSRM         __REG(0x40600134) /* UDC Control/Status register - Endpoint M */
#define UDCCSRN         __REG(0x40600138) /* UDC Control/Status register - Endpoint N */
#define UDCCSRP         __REG(0x4060013C) /* UDC Control/Status register - Endpoint P */
#define UDCCSRQ         __REG(0x40600140) /* UDC Control/Status register - Endpoint Q */
#define UDCCSRR         __REG(0x40600144) /* UDC Control/Status register - Endpoint R */
#define UDCCSRS         __REG(0x40600148) /* UDC Control/Status register - Endpoint S */
#define UDCCSRT         __REG(0x4060014C) /* UDC Control/Status register - Endpoint T */
#define UDCCSRU         __REG(0x40600150) /* UDC Control/Status register - Endpoint U */
#define UDCCSRV         __REG(0x40600154) /* UDC Control/Status register - Endpoint V */
#define UDCCSRW         __REG(0x40600158) /* UDC Control/Status register - Endpoint W */
#define UDCCSRX         __REG(0x4060015C) /* UDC Control/Status register - Endpoint X */

#define UDCCSR_DPE	(1 << 9)	/* Data Packet Error */
#define UDCCSR_FEF	(1 << 8)	/* Flush Endpoint FIFO */
#define UDCCSR_SP	(1 << 7)	/* Short Packet Control/Status */
#define UDCCSR_BNE	(1 << 6)	/* Buffer Not Empty (IN endpoints) */
#define UDCCSR_BNF	(1 << 6)	/* Buffer Not Full (OUT endpoints) */
#define UDCCSR_FST	(1 << 5)	/* Force STALL */
#define UDCCSR_SST	(1 << 4)	/* Sent STALL */
#define UDCCSR_DME	(1 << 3)	/* DMA Enable */
#define UDCCSR_TRN	(1 << 2)	/* Tx/Rx NAK */
#define UDCCSR_PC	(1 << 1)	/* Packet Complete */
#define UDCCSR_FS	(1 << 0)	/* FIFO needs service */

#define UDCBCN(x)	__REG2(0x40600200, (x)<<2)
#define UDCBCR0         __REG(0x40600200) /* Byte Count Register - EP0 */
#define UDCBCRA         __REG(0x40600204) /* Byte Count Register - EPA */
#define UDCBCRB         __REG(0x40600208) /* Byte Count Register - EPB */
#define UDCBCRC         __REG(0x4060020C) /* Byte Count Register - EPC */
#define UDCBCRD         __REG(0x40600210) /* Byte Count Register - EPD */
#define UDCBCRE         __REG(0x40600214) /* Byte Count Register - EPE */
#define UDCBCRF         __REG(0x40600218) /* Byte Count Register - EPF */
#define UDCBCRG         __REG(0x4060021C) /* Byte Count Register - EPG */
#define UDCBCRH         __REG(0x40600220) /* Byte Count Register - EPH */
#define UDCBCRI         __REG(0x40600224) /* Byte Count Register - EPI */
#define UDCBCRJ         __REG(0x40600228) /* Byte Count Register - EPJ */
#define UDCBCRK         __REG(0x4060022C) /* Byte Count Register - EPK */
#define UDCBCRL         __REG(0x40600230) /* Byte Count Register - EPL */
#define UDCBCRM         __REG(0x40600234) /* Byte Count Register - EPM */
#define UDCBCRN         __REG(0x40600238) /* Byte Count Register - EPN */
#define UDCBCRP         __REG(0x4060023C) /* Byte Count Register - EPP */
#define UDCBCRQ         __REG(0x40600240) /* Byte Count Register - EPQ */
#define UDCBCRR         __REG(0x40600244) /* Byte Count Register - EPR */
#define UDCBCRS         __REG(0x40600248) /* Byte Count Register - EPS */
#define UDCBCRT         __REG(0x4060024C) /* Byte Count Register - EPT */
#define UDCBCRU         __REG(0x40600250) /* Byte Count Register - EPU */
#define UDCBCRV         __REG(0x40600254) /* Byte Count Register - EPV */
#define UDCBCRW         __REG(0x40600258) /* Byte Count Register - EPW */
#define UDCBCRX         __REG(0x4060025C) /* Byte Count Register - EPX */

#define UDCDN(x)	__REG2(0x40600300, (x)<<2)
#define PHYS_UDCDN(x)	(0x40600300 + ((x)<<2))
#define PUDCDN(x)	(volatile u32 *)(io_p2v(PHYS_UDCDN((x))))
#define UDCDR0          __REG(0x40600300) /* Data Register - EP0 */
#define UDCDRA          __REG(0x40600304) /* Data Register - EPA */
#define UDCDRB          __REG(0x40600308) /* Data Register - EPB */
#define UDCDRC          __REG(0x4060030C) /* Data Register - EPC */
#define UDCDRD          __REG(0x40600310) /* Data Register - EPD */
#define UDCDRE          __REG(0x40600314) /* Data Register - EPE */
#define UDCDRF          __REG(0x40600318) /* Data Register - EPF */
#define UDCDRG          __REG(0x4060031C) /* Data Register - EPG */
#define UDCDRH          __REG(0x40600320) /* Data Register - EPH */
#define UDCDRI          __REG(0x40600324) /* Data Register - EPI */
#define UDCDRJ          __REG(0x40600328) /* Data Register - EPJ */
#define UDCDRK          __REG(0x4060032C) /* Data Register - EPK */
#define UDCDRL          __REG(0x40600330) /* Data Register - EPL */
#define UDCDRM          __REG(0x40600334) /* Data Register - EPM */
#define UDCDRN          __REG(0x40600338) /* Data Register - EPN */
#define UDCDRP          __REG(0x4060033C) /* Data Register - EPP */
#define UDCDRQ          __REG(0x40600340) /* Data Register - EPQ */
#define UDCDRR          __REG(0x40600344) /* Data Register - EPR */
#define UDCDRS          __REG(0x40600348) /* Data Register - EPS */
#define UDCDRT          __REG(0x4060034C) /* Data Register - EPT */
#define UDCDRU          __REG(0x40600350) /* Data Register - EPU */
#define UDCDRV          __REG(0x40600354) /* Data Register - EPV */
#define UDCDRW          __REG(0x40600358) /* Data Register - EPW */
#define UDCDRX          __REG(0x4060035C) /* Data Register - EPX */

#define UDCCN(x)       __REG2(0x40600400, (x)<<2)
#define UDCCRA          __REG(0x40600404) /* Configuration register EPA */
#define UDCCRB          __REG(0x40600408) /* Configuration register EPB */
#define UDCCRC          __REG(0x4060040C) /* Configuration register EPC */
#define UDCCRD          __REG(0x40600410) /* Configuration register EPD */
#define UDCCRE          __REG(0x40600414) /* Configuration register EPE */
#define UDCCRF          __REG(0x40600418) /* Configuration register EPF */
#define UDCCRG          __REG(0x4060041C) /* Configuration register EPG */
#define UDCCRH          __REG(0x40600420) /* Configuration register EPH */
#define UDCCRI          __REG(0x40600424) /* Configuration register EPI */
#define UDCCRJ          __REG(0x40600428) /* Configuration register EPJ */
#define UDCCRK          __REG(0x4060042C) /* Configuration register EPK */
#define UDCCRL          __REG(0x40600430) /* Configuration register EPL */
#define UDCCRM          __REG(0x40600434) /* Configuration register EPM */
#define UDCCRN          __REG(0x40600438) /* Configuration register EPN */
#define UDCCRP          __REG(0x4060043C) /* Configuration register EPP */
#define UDCCRQ          __REG(0x40600440) /* Configuration register EPQ */
#define UDCCRR          __REG(0x40600444) /* Configuration register EPR */
#define UDCCRS          __REG(0x40600448) /* Configuration register EPS */
#define UDCCRT          __REG(0x4060044C) /* Configuration register EPT */
#define UDCCRU          __REG(0x40600450) /* Configuration register EPU */
#define UDCCRV          __REG(0x40600454) /* Configuration register EPV */
#define UDCCRW          __REG(0x40600458) /* Configuration register EPW */
#define UDCCRX          __REG(0x4060045C) /* Configuration register EPX */

#define UDCCONR_CN	(0x03 << 25)	/* Configuration Number */
#define UDCCONR_CN_S	(25)
#define UDCCONR_IN	(0x07 << 22)	/* Interface Number */
#define UDCCONR_IN_S	(22)
#define UDCCONR_AISN	(0x07 << 19)	/* Alternate Interface Number */
#define UDCCONR_AISN_S	(19)
#define UDCCONR_EN	(0x0f << 15)	/* Endpoint Number */
#define UDCCONR_EN_S	(15)
#define UDCCONR_ET	(0x03 << 13)	/* Endpoint Type: */
#define UDCCONR_ET_S	(13)
#define UDCCONR_ET_INT	(0x03 << 13)	/*   Interrupt */
#define UDCCONR_ET_BULK	(0x02 << 13)	/*   Bulk */
#define UDCCONR_ET_ISO	(0x01 << 13)	/*   Isochronous */
#define UDCCONR_ET_NU	(0x00 << 13)	/*   Not used */
#define UDCCONR_ED	(1 << 12)	/* Endpoint Direction */
#define UDCCONR_MPS	(0x3ff << 2)	/* Maximum Packet Size */
#define UDCCONR_MPS_S	(2)
#define UDCCONR_DE	(1 << 1)	/* Double Buffering Enable */
#define UDCCONR_EE	(1 << 0)	/* Endpoint Enable */


#define UDC_INT_FIFOERROR  (0x2)
#define UDC_INT_PACKETCMP  (0x1)

#define UDC_FNR_MASK     (0x7ff)

#define UDCCSR_WR_MASK   (UDCCSR_DME|UDCCSR_FST)
#define UDC_BCR_MASK    (0x3ff)

#endif
