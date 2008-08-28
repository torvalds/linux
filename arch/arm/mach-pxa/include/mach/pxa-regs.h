/*
 *  arch/arm/mach-pxa/include/mach/pxa-regs.h
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __PXA_REGS_H
#define __PXA_REGS_H


/*
 * PXA Chip selects
 */

#define PXA_CS0_PHYS	0x00000000
#define PXA_CS1_PHYS	0x04000000
#define PXA_CS2_PHYS	0x08000000
#define PXA_CS3_PHYS	0x0C000000
#define PXA_CS4_PHYS	0x10000000
#define PXA_CS5_PHYS	0x14000000


/*
 * Personal Computer Memory Card International Association (PCMCIA) sockets
 */

#define PCMCIAPrtSp	0x04000000	/* PCMCIA Partition Space [byte]   */
#define PCMCIASp	(4*PCMCIAPrtSp)	/* PCMCIA Space [byte]             */
#define PCMCIAIOSp	PCMCIAPrtSp	/* PCMCIA I/O Space [byte]         */
#define PCMCIAAttrSp	PCMCIAPrtSp	/* PCMCIA Attribute Space [byte]   */
#define PCMCIAMemSp	PCMCIAPrtSp	/* PCMCIA Memory Space [byte]      */

#define PCMCIA0Sp	PCMCIASp	/* PCMCIA 0 Space [byte]           */
#define PCMCIA0IOSp	PCMCIAIOSp	/* PCMCIA 0 I/O Space [byte]       */
#define PCMCIA0AttrSp	PCMCIAAttrSp	/* PCMCIA 0 Attribute Space [byte] */
#define PCMCIA0MemSp	PCMCIAMemSp	/* PCMCIA 0 Memory Space [byte]    */

#define PCMCIA1Sp	PCMCIASp	/* PCMCIA 1 Space [byte]           */
#define PCMCIA1IOSp	PCMCIAIOSp	/* PCMCIA 1 I/O Space [byte]       */
#define PCMCIA1AttrSp	PCMCIAAttrSp	/* PCMCIA 1 Attribute Space [byte] */
#define PCMCIA1MemSp	PCMCIAMemSp	/* PCMCIA 1 Memory Space [byte]    */

#define _PCMCIA(Nb)	        	/* PCMCIA [0..1]                   */ \
                	(0x20000000 + (Nb)*PCMCIASp)
#define _PCMCIAIO(Nb)	_PCMCIA (Nb)	/* PCMCIA I/O [0..1]               */
#define _PCMCIAAttr(Nb)	        	/* PCMCIA Attribute [0..1]         */ \
                	(_PCMCIA (Nb) + 2*PCMCIAPrtSp)
#define _PCMCIAMem(Nb)	        	/* PCMCIA Memory [0..1]            */ \
                	(_PCMCIA (Nb) + 3*PCMCIAPrtSp)

#define _PCMCIA0	_PCMCIA (0)	/* PCMCIA 0                        */
#define _PCMCIA0IO	_PCMCIAIO (0)	/* PCMCIA 0 I/O                    */
#define _PCMCIA0Attr	_PCMCIAAttr (0)	/* PCMCIA 0 Attribute              */
#define _PCMCIA0Mem	_PCMCIAMem (0)	/* PCMCIA 0 Memory                 */

#define _PCMCIA1	_PCMCIA (1)	/* PCMCIA 1                        */
#define _PCMCIA1IO	_PCMCIAIO (1)	/* PCMCIA 1 I/O                    */
#define _PCMCIA1Attr	_PCMCIAAttr (1)	/* PCMCIA 1 Attribute              */
#define _PCMCIA1Mem	_PCMCIAMem (1)	/* PCMCIA 1 Memory                 */



/*
 * DMA Controller
 */

#define DCSR0		__REG(0x40000000)  /* DMA Control / Status Register for Channel 0 */
#define DCSR1		__REG(0x40000004)  /* DMA Control / Status Register for Channel 1 */
#define DCSR2		__REG(0x40000008)  /* DMA Control / Status Register for Channel 2 */
#define DCSR3		__REG(0x4000000c)  /* DMA Control / Status Register for Channel 3 */
#define DCSR4		__REG(0x40000010)  /* DMA Control / Status Register for Channel 4 */
#define DCSR5		__REG(0x40000014)  /* DMA Control / Status Register for Channel 5 */
#define DCSR6		__REG(0x40000018)  /* DMA Control / Status Register for Channel 6 */
#define DCSR7		__REG(0x4000001c)  /* DMA Control / Status Register for Channel 7 */
#define DCSR8		__REG(0x40000020)  /* DMA Control / Status Register for Channel 8 */
#define DCSR9		__REG(0x40000024)  /* DMA Control / Status Register for Channel 9 */
#define DCSR10		__REG(0x40000028)  /* DMA Control / Status Register for Channel 10 */
#define DCSR11		__REG(0x4000002c)  /* DMA Control / Status Register for Channel 11 */
#define DCSR12		__REG(0x40000030)  /* DMA Control / Status Register for Channel 12 */
#define DCSR13		__REG(0x40000034)  /* DMA Control / Status Register for Channel 13 */
#define DCSR14		__REG(0x40000038)  /* DMA Control / Status Register for Channel 14 */
#define DCSR15		__REG(0x4000003c)  /* DMA Control / Status Register for Channel 15 */

#define DCSR(x)		__REG2(0x40000000, (x) << 2)

#define DCSR_RUN	(1 << 31)	/* Run Bit (read / write) */
#define DCSR_NODESC	(1 << 30)	/* No-Descriptor Fetch (read / write) */
#define DCSR_STOPIRQEN	(1 << 29)	/* Stop Interrupt Enable (read / write) */
#ifdef CONFIG_PXA27x
#define DCSR_EORIRQEN	(1 << 28)       /* End of Receive Interrupt Enable (R/W) */
#define DCSR_EORJMPEN	(1 << 27)       /* Jump to next descriptor on EOR */
#define DCSR_EORSTOPEN	(1 << 26)       /* STOP on an EOR */
#define DCSR_SETCMPST	(1 << 25)       /* Set Descriptor Compare Status */
#define DCSR_CLRCMPST	(1 << 24)       /* Clear Descriptor Compare Status */
#define DCSR_CMPST	(1 << 10)       /* The Descriptor Compare Status */
#define DCSR_EORINTR	(1 << 9)        /* The end of Receive */
#endif
#define DCSR_REQPEND	(1 << 8)	/* Request Pending (read-only) */
#define DCSR_STOPSTATE	(1 << 3)	/* Stop State (read-only) */
#define DCSR_ENDINTR	(1 << 2)	/* End Interrupt (read / write) */
#define DCSR_STARTINTR	(1 << 1)	/* Start Interrupt (read / write) */
#define DCSR_BUSERR	(1 << 0)	/* Bus Error Interrupt (read / write) */

#define DALGN		__REG(0x400000a0)  /* DMA Alignment Register */
#define DINT		__REG(0x400000f0)  /* DMA Interrupt Register */

#define DRCMR(n)	(*(((n) < 64) ? \
			&__REG2(0x40000100, ((n) & 0x3f) << 2) : \
			&__REG2(0x40001100, ((n) & 0x3f) << 2)))

#define DRCMR0		__REG(0x40000100)  /* Request to Channel Map Register for DREQ 0 */
#define DRCMR1		__REG(0x40000104)  /* Request to Channel Map Register for DREQ 1 */
#define DRCMR2		__REG(0x40000108)  /* Request to Channel Map Register for I2S receive Request */
#define DRCMR3		__REG(0x4000010c)  /* Request to Channel Map Register for I2S transmit Request */
#define DRCMR4		__REG(0x40000110)  /* Request to Channel Map Register for BTUART receive Request */
#define DRCMR5		__REG(0x40000114)  /* Request to Channel Map Register for BTUART transmit Request. */
#define DRCMR6		__REG(0x40000118)  /* Request to Channel Map Register for FFUART receive Request */
#define DRCMR7		__REG(0x4000011c)  /* Request to Channel Map Register for FFUART transmit Request */
#define DRCMR8		__REG(0x40000120)  /* Request to Channel Map Register for AC97 microphone Request */
#define DRCMR9		__REG(0x40000124)  /* Request to Channel Map Register for AC97 modem receive Request */
#define DRCMR10		__REG(0x40000128)  /* Request to Channel Map Register for AC97 modem transmit Request */
#define DRCMR11		__REG(0x4000012c)  /* Request to Channel Map Register for AC97 audio receive Request */
#define DRCMR12		__REG(0x40000130)  /* Request to Channel Map Register for AC97 audio transmit Request */
#define DRCMR13		__REG(0x40000134)  /* Request to Channel Map Register for SSP receive Request */
#define DRCMR14		__REG(0x40000138)  /* Request to Channel Map Register for SSP transmit Request */
#define DRCMR15		__REG(0x4000013c)  /* Request to Channel Map Register for SSP2 receive Request */
#define DRCMR16		__REG(0x40000140)  /* Request to Channel Map Register for SSP2 transmit Request */
#define DRCMR17		__REG(0x40000144)  /* Request to Channel Map Register for ICP receive Request */
#define DRCMR18		__REG(0x40000148)  /* Request to Channel Map Register for ICP transmit Request */
#define DRCMR19		__REG(0x4000014c)  /* Request to Channel Map Register for STUART receive Request */
#define DRCMR20		__REG(0x40000150)  /* Request to Channel Map Register for STUART transmit Request */
#define DRCMR21		__REG(0x40000154)  /* Request to Channel Map Register for MMC receive Request */
#define DRCMR22		__REG(0x40000158)  /* Request to Channel Map Register for MMC transmit Request */
#define DRCMR23		__REG(0x4000015c)  /* Reserved */
#define DRCMR24		__REG(0x40000160)  /* Reserved */
#define DRCMR25		__REG(0x40000164)  /* Request to Channel Map Register for USB endpoint 1 Request */
#define DRCMR26		__REG(0x40000168)  /* Request to Channel Map Register for USB endpoint 2 Request */
#define DRCMR27		__REG(0x4000016C)  /* Request to Channel Map Register for USB endpoint 3 Request */
#define DRCMR28		__REG(0x40000170)  /* Request to Channel Map Register for USB endpoint 4 Request */
#define DRCMR29		__REG(0x40000174)  /* Reserved */
#define DRCMR30		__REG(0x40000178)  /* Request to Channel Map Register for USB endpoint 6 Request */
#define DRCMR31		__REG(0x4000017C)  /* Request to Channel Map Register for USB endpoint 7 Request */
#define DRCMR32		__REG(0x40000180)  /* Request to Channel Map Register for USB endpoint 8 Request */
#define DRCMR33		__REG(0x40000184)  /* Request to Channel Map Register for USB endpoint 9 Request */
#define DRCMR34		__REG(0x40000188)  /* Reserved */
#define DRCMR35		__REG(0x4000018C)  /* Request to Channel Map Register for USB endpoint 11 Request */
#define DRCMR36		__REG(0x40000190)  /* Request to Channel Map Register for USB endpoint 12 Request */
#define DRCMR37		__REG(0x40000194)  /* Request to Channel Map Register for USB endpoint 13 Request */
#define DRCMR38		__REG(0x40000198)  /* Request to Channel Map Register for USB endpoint 14 Request */
#define DRCMR39		__REG(0x4000019C)  /* Reserved */
#define DRCMR66		__REG(0x40001108)  /* Request to Channel Map Register for SSP3 receive Request */
#define DRCMR67		__REG(0x4000110C)  /* Request to Channel Map Register for SSP3 transmit Request */
#define DRCMR68		__REG(0x40001110)  /* Request to Channel Map Register for Camera FIFO 0 Request */
#define DRCMR69		__REG(0x40001114)  /* Request to Channel Map Register for Camera FIFO 1 Request */
#define DRCMR70		__REG(0x40001118)  /* Request to Channel Map Register for Camera FIFO 2 Request */

#define DRCMRRXSADR	DRCMR2
#define DRCMRTXSADR	DRCMR3
#define DRCMRRXBTRBR	DRCMR4
#define DRCMRTXBTTHR	DRCMR5
#define DRCMRRXFFRBR	DRCMR6
#define DRCMRTXFFTHR	DRCMR7
#define DRCMRRXMCDR	DRCMR8
#define DRCMRRXMODR	DRCMR9
#define DRCMRTXMODR	DRCMR10
#define DRCMRRXPCDR	DRCMR11
#define DRCMRTXPCDR	DRCMR12
#define DRCMRRXSSDR	DRCMR13
#define DRCMRTXSSDR	DRCMR14
#define DRCMRRXSS2DR   DRCMR15
#define DRCMRTXSS2DR   DRCMR16
#define DRCMRRXICDR	DRCMR17
#define DRCMRTXICDR	DRCMR18
#define DRCMRRXSTRBR	DRCMR19
#define DRCMRTXSTTHR	DRCMR20
#define DRCMRRXMMC	DRCMR21
#define DRCMRTXMMC	DRCMR22
#define DRCMRRXSS3DR   DRCMR66
#define DRCMRTXSS3DR   DRCMR67
#define DRCMRUDC(x)	DRCMR((x) + 24)

#define DRCMR_MAPVLD	(1 << 7)	/* Map Valid (read / write) */
#define DRCMR_CHLNUM	0x1f		/* mask for Channel Number (read / write) */

#define DDADR0		__REG(0x40000200)  /* DMA Descriptor Address Register Channel 0 */
#define DSADR0		__REG(0x40000204)  /* DMA Source Address Register Channel 0 */
#define DTADR0		__REG(0x40000208)  /* DMA Target Address Register Channel 0 */
#define DCMD0		__REG(0x4000020c)  /* DMA Command Address Register Channel 0 */
#define DDADR1		__REG(0x40000210)  /* DMA Descriptor Address Register Channel 1 */
#define DSADR1		__REG(0x40000214)  /* DMA Source Address Register Channel 1 */
#define DTADR1		__REG(0x40000218)  /* DMA Target Address Register Channel 1 */
#define DCMD1		__REG(0x4000021c)  /* DMA Command Address Register Channel 1 */
#define DDADR2		__REG(0x40000220)  /* DMA Descriptor Address Register Channel 2 */
#define DSADR2		__REG(0x40000224)  /* DMA Source Address Register Channel 2 */
#define DTADR2		__REG(0x40000228)  /* DMA Target Address Register Channel 2 */
#define DCMD2		__REG(0x4000022c)  /* DMA Command Address Register Channel 2 */
#define DDADR3		__REG(0x40000230)  /* DMA Descriptor Address Register Channel 3 */
#define DSADR3		__REG(0x40000234)  /* DMA Source Address Register Channel 3 */
#define DTADR3		__REG(0x40000238)  /* DMA Target Address Register Channel 3 */
#define DCMD3		__REG(0x4000023c)  /* DMA Command Address Register Channel 3 */
#define DDADR4		__REG(0x40000240)  /* DMA Descriptor Address Register Channel 4 */
#define DSADR4		__REG(0x40000244)  /* DMA Source Address Register Channel 4 */
#define DTADR4		__REG(0x40000248)  /* DMA Target Address Register Channel 4 */
#define DCMD4		__REG(0x4000024c)  /* DMA Command Address Register Channel 4 */
#define DDADR5		__REG(0x40000250)  /* DMA Descriptor Address Register Channel 5 */
#define DSADR5		__REG(0x40000254)  /* DMA Source Address Register Channel 5 */
#define DTADR5		__REG(0x40000258)  /* DMA Target Address Register Channel 5 */
#define DCMD5		__REG(0x4000025c)  /* DMA Command Address Register Channel 5 */
#define DDADR6		__REG(0x40000260)  /* DMA Descriptor Address Register Channel 6 */
#define DSADR6		__REG(0x40000264)  /* DMA Source Address Register Channel 6 */
#define DTADR6		__REG(0x40000268)  /* DMA Target Address Register Channel 6 */
#define DCMD6		__REG(0x4000026c)  /* DMA Command Address Register Channel 6 */
#define DDADR7		__REG(0x40000270)  /* DMA Descriptor Address Register Channel 7 */
#define DSADR7		__REG(0x40000274)  /* DMA Source Address Register Channel 7 */
#define DTADR7		__REG(0x40000278)  /* DMA Target Address Register Channel 7 */
#define DCMD7		__REG(0x4000027c)  /* DMA Command Address Register Channel 7 */
#define DDADR8		__REG(0x40000280)  /* DMA Descriptor Address Register Channel 8 */
#define DSADR8		__REG(0x40000284)  /* DMA Source Address Register Channel 8 */
#define DTADR8		__REG(0x40000288)  /* DMA Target Address Register Channel 8 */
#define DCMD8		__REG(0x4000028c)  /* DMA Command Address Register Channel 8 */
#define DDADR9		__REG(0x40000290)  /* DMA Descriptor Address Register Channel 9 */
#define DSADR9		__REG(0x40000294)  /* DMA Source Address Register Channel 9 */
#define DTADR9		__REG(0x40000298)  /* DMA Target Address Register Channel 9 */
#define DCMD9		__REG(0x4000029c)  /* DMA Command Address Register Channel 9 */
#define DDADR10		__REG(0x400002a0)  /* DMA Descriptor Address Register Channel 10 */
#define DSADR10		__REG(0x400002a4)  /* DMA Source Address Register Channel 10 */
#define DTADR10		__REG(0x400002a8)  /* DMA Target Address Register Channel 10 */
#define DCMD10		__REG(0x400002ac)  /* DMA Command Address Register Channel 10 */
#define DDADR11		__REG(0x400002b0)  /* DMA Descriptor Address Register Channel 11 */
#define DSADR11		__REG(0x400002b4)  /* DMA Source Address Register Channel 11 */
#define DTADR11		__REG(0x400002b8)  /* DMA Target Address Register Channel 11 */
#define DCMD11		__REG(0x400002bc)  /* DMA Command Address Register Channel 11 */
#define DDADR12		__REG(0x400002c0)  /* DMA Descriptor Address Register Channel 12 */
#define DSADR12		__REG(0x400002c4)  /* DMA Source Address Register Channel 12 */
#define DTADR12		__REG(0x400002c8)  /* DMA Target Address Register Channel 12 */
#define DCMD12		__REG(0x400002cc)  /* DMA Command Address Register Channel 12 */
#define DDADR13		__REG(0x400002d0)  /* DMA Descriptor Address Register Channel 13 */
#define DSADR13		__REG(0x400002d4)  /* DMA Source Address Register Channel 13 */
#define DTADR13		__REG(0x400002d8)  /* DMA Target Address Register Channel 13 */
#define DCMD13		__REG(0x400002dc)  /* DMA Command Address Register Channel 13 */
#define DDADR14		__REG(0x400002e0)  /* DMA Descriptor Address Register Channel 14 */
#define DSADR14		__REG(0x400002e4)  /* DMA Source Address Register Channel 14 */
#define DTADR14		__REG(0x400002e8)  /* DMA Target Address Register Channel 14 */
#define DCMD14		__REG(0x400002ec)  /* DMA Command Address Register Channel 14 */
#define DDADR15		__REG(0x400002f0)  /* DMA Descriptor Address Register Channel 15 */
#define DSADR15		__REG(0x400002f4)  /* DMA Source Address Register Channel 15 */
#define DTADR15		__REG(0x400002f8)  /* DMA Target Address Register Channel 15 */
#define DCMD15		__REG(0x400002fc)  /* DMA Command Address Register Channel 15 */

#define DDADR(x)	__REG2(0x40000200, (x) << 4)
#define DSADR(x)	__REG2(0x40000204, (x) << 4)
#define DTADR(x)	__REG2(0x40000208, (x) << 4)
#define DCMD(x)		__REG2(0x4000020c, (x) << 4)

#define DDADR_DESCADDR	0xfffffff0	/* Address of next descriptor (mask) */
#define DDADR_STOP	(1 << 0)	/* Stop (read / write) */

#define DCMD_INCSRCADDR	(1 << 31)	/* Source Address Increment Setting. */
#define DCMD_INCTRGADDR	(1 << 30)	/* Target Address Increment Setting. */
#define DCMD_FLOWSRC	(1 << 29)	/* Flow Control by the source. */
#define DCMD_FLOWTRG	(1 << 28)	/* Flow Control by the target. */
#define DCMD_STARTIRQEN	(1 << 22)	/* Start Interrupt Enable */
#define DCMD_ENDIRQEN	(1 << 21)	/* End Interrupt Enable */
#define DCMD_ENDIAN	(1 << 18)	/* Device Endian-ness. */
#define DCMD_BURST8	(1 << 16)	/* 8 byte burst */
#define DCMD_BURST16	(2 << 16)	/* 16 byte burst */
#define DCMD_BURST32	(3 << 16)	/* 32 byte burst */
#define DCMD_WIDTH1	(1 << 14)	/* 1 byte width */
#define DCMD_WIDTH2	(2 << 14)	/* 2 byte width (HalfWord) */
#define DCMD_WIDTH4	(3 << 14)	/* 4 byte width (Word) */
#define DCMD_LENGTH	0x01fff		/* length mask (max = 8K - 1) */


/*
 * UARTs
 */

/* Full Function UART (FFUART) */
#define FFUART		FFRBR
#define FFRBR		__REG(0x40100000)  /* Receive Buffer Register (read only) */
#define FFTHR		__REG(0x40100000)  /* Transmit Holding Register (write only) */
#define FFIER		__REG(0x40100004)  /* Interrupt Enable Register (read/write) */
#define FFIIR		__REG(0x40100008)  /* Interrupt ID Register (read only) */
#define FFFCR		__REG(0x40100008)  /* FIFO Control Register (write only) */
#define FFLCR		__REG(0x4010000C)  /* Line Control Register (read/write) */
#define FFMCR		__REG(0x40100010)  /* Modem Control Register (read/write) */
#define FFLSR		__REG(0x40100014)  /* Line Status Register (read only) */
#define FFMSR		__REG(0x40100018)  /* Modem Status Register (read only) */
#define FFSPR		__REG(0x4010001C)  /* Scratch Pad Register (read/write) */
#define FFISR		__REG(0x40100020)  /* Infrared Selection Register (read/write) */
#define FFDLL		__REG(0x40100000)  /* Divisor Latch Low Register (DLAB = 1) (read/write) */
#define FFDLH		__REG(0x40100004)  /* Divisor Latch High Register (DLAB = 1) (read/write) */

/* Bluetooth UART (BTUART) */
#define BTUART		BTRBR
#define BTRBR		__REG(0x40200000)  /* Receive Buffer Register (read only) */
#define BTTHR		__REG(0x40200000)  /* Transmit Holding Register (write only) */
#define BTIER		__REG(0x40200004)  /* Interrupt Enable Register (read/write) */
#define BTIIR		__REG(0x40200008)  /* Interrupt ID Register (read only) */
#define BTFCR		__REG(0x40200008)  /* FIFO Control Register (write only) */
#define BTLCR		__REG(0x4020000C)  /* Line Control Register (read/write) */
#define BTMCR		__REG(0x40200010)  /* Modem Control Register (read/write) */
#define BTLSR		__REG(0x40200014)  /* Line Status Register (read only) */
#define BTMSR		__REG(0x40200018)  /* Modem Status Register (read only) */
#define BTSPR		__REG(0x4020001C)  /* Scratch Pad Register (read/write) */
#define BTISR		__REG(0x40200020)  /* Infrared Selection Register (read/write) */
#define BTDLL		__REG(0x40200000)  /* Divisor Latch Low Register (DLAB = 1) (read/write) */
#define BTDLH		__REG(0x40200004)  /* Divisor Latch High Register (DLAB = 1) (read/write) */

/* Standard UART (STUART) */
#define STUART		STRBR
#define STRBR		__REG(0x40700000)  /* Receive Buffer Register (read only) */
#define STTHR		__REG(0x40700000)  /* Transmit Holding Register (write only) */
#define STIER		__REG(0x40700004)  /* Interrupt Enable Register (read/write) */
#define STIIR		__REG(0x40700008)  /* Interrupt ID Register (read only) */
#define STFCR		__REG(0x40700008)  /* FIFO Control Register (write only) */
#define STLCR		__REG(0x4070000C)  /* Line Control Register (read/write) */
#define STMCR		__REG(0x40700010)  /* Modem Control Register (read/write) */
#define STLSR		__REG(0x40700014)  /* Line Status Register (read only) */
#define STMSR		__REG(0x40700018)  /* Reserved */
#define STSPR		__REG(0x4070001C)  /* Scratch Pad Register (read/write) */
#define STISR		__REG(0x40700020)  /* Infrared Selection Register (read/write) */
#define STDLL		__REG(0x40700000)  /* Divisor Latch Low Register (DLAB = 1) (read/write) */
#define STDLH		__REG(0x40700004)  /* Divisor Latch High Register (DLAB = 1) (read/write) */

/* Hardware UART (HWUART) */
#define HWUART		HWRBR
#define HWRBR		__REG(0x41600000)  /* Receive Buffer Register (read only) */
#define HWTHR		__REG(0x41600000)  /* Transmit Holding Register (write only) */
#define HWIER		__REG(0x41600004)  /* Interrupt Enable Register (read/write) */
#define HWIIR		__REG(0x41600008)  /* Interrupt ID Register (read only) */
#define HWFCR		__REG(0x41600008)  /* FIFO Control Register (write only) */
#define HWLCR		__REG(0x4160000C)  /* Line Control Register (read/write) */
#define HWMCR		__REG(0x41600010)  /* Modem Control Register (read/write) */
#define HWLSR		__REG(0x41600014)  /* Line Status Register (read only) */
#define HWMSR		__REG(0x41600018)  /* Modem Status Register (read only) */
#define HWSPR		__REG(0x4160001C)  /* Scratch Pad Register (read/write) */
#define HWISR		__REG(0x41600020)  /* Infrared Selection Register (read/write) */
#define HWFOR		__REG(0x41600024)  /* Receive FIFO Occupancy Register (read only) */
#define HWABR		__REG(0x41600028)  /* Auto-Baud Control Register (read/write) */
#define HWACR		__REG(0x4160002C)  /* Auto-Baud Count Register (read only) */
#define HWDLL		__REG(0x41600000)  /* Divisor Latch Low Register (DLAB = 1) (read/write) */
#define HWDLH		__REG(0x41600004)  /* Divisor Latch High Register (DLAB = 1) (read/write) */

#define IER_DMAE	(1 << 7)	/* DMA Requests Enable */
#define IER_UUE		(1 << 6)	/* UART Unit Enable */
#define IER_NRZE	(1 << 5)	/* NRZ coding Enable */
#define IER_RTIOE	(1 << 4)	/* Receiver Time Out Interrupt Enable */
#define IER_MIE		(1 << 3)	/* Modem Interrupt Enable */
#define IER_RLSE	(1 << 2)	/* Receiver Line Status Interrupt Enable */
#define IER_TIE		(1 << 1)	/* Transmit Data request Interrupt Enable */
#define IER_RAVIE	(1 << 0)	/* Receiver Data Available Interrupt Enable */

#define IIR_FIFOES1	(1 << 7)	/* FIFO Mode Enable Status */
#define IIR_FIFOES0	(1 << 6)	/* FIFO Mode Enable Status */
#define IIR_TOD		(1 << 3)	/* Time Out Detected */
#define IIR_IID2	(1 << 2)	/* Interrupt Source Encoded */
#define IIR_IID1	(1 << 1)	/* Interrupt Source Encoded */
#define IIR_IP		(1 << 0)	/* Interrupt Pending (active low) */

#define FCR_ITL2	(1 << 7)	/* Interrupt Trigger Level */
#define FCR_ITL1	(1 << 6)	/* Interrupt Trigger Level */
#define FCR_RESETTF	(1 << 2)	/* Reset Transmitter FIFO */
#define FCR_RESETRF	(1 << 1)	/* Reset Receiver FIFO */
#define FCR_TRFIFOE	(1 << 0)	/* Transmit and Receive FIFO Enable */
#define FCR_ITL_1	(0)
#define FCR_ITL_8	(FCR_ITL1)
#define FCR_ITL_16	(FCR_ITL2)
#define FCR_ITL_32	(FCR_ITL2|FCR_ITL1)

#define LCR_DLAB	(1 << 7)	/* Divisor Latch Access Bit */
#define LCR_SB		(1 << 6)	/* Set Break */
#define LCR_STKYP	(1 << 5)	/* Sticky Parity */
#define LCR_EPS		(1 << 4)	/* Even Parity Select */
#define LCR_PEN		(1 << 3)	/* Parity Enable */
#define LCR_STB		(1 << 2)	/* Stop Bit */
#define LCR_WLS1	(1 << 1)	/* Word Length Select */
#define LCR_WLS0	(1 << 0)	/* Word Length Select */

#define LSR_FIFOE	(1 << 7)	/* FIFO Error Status */
#define LSR_TEMT	(1 << 6)	/* Transmitter Empty */
#define LSR_TDRQ	(1 << 5)	/* Transmit Data Request */
#define LSR_BI		(1 << 4)	/* Break Interrupt */
#define LSR_FE		(1 << 3)	/* Framing Error */
#define LSR_PE		(1 << 2)	/* Parity Error */
#define LSR_OE		(1 << 1)	/* Overrun Error */
#define LSR_DR		(1 << 0)	/* Data Ready */

#define MCR_LOOP	(1 << 4)
#define MCR_OUT2	(1 << 3)	/* force MSR_DCD in loopback mode */
#define MCR_OUT1	(1 << 2)	/* force MSR_RI in loopback mode */
#define MCR_RTS		(1 << 1)	/* Request to Send */
#define MCR_DTR		(1 << 0)	/* Data Terminal Ready */

#define MSR_DCD		(1 << 7)	/* Data Carrier Detect */
#define MSR_RI		(1 << 6)	/* Ring Indicator */
#define MSR_DSR		(1 << 5)	/* Data Set Ready */
#define MSR_CTS		(1 << 4)	/* Clear To Send */
#define MSR_DDCD	(1 << 3)	/* Delta Data Carrier Detect */
#define MSR_TERI	(1 << 2)	/* Trailing Edge Ring Indicator */
#define MSR_DDSR	(1 << 1)	/* Delta Data Set Ready */
#define MSR_DCTS	(1 << 0)	/* Delta Clear To Send */

/*
 * IrSR (Infrared Selection Register)
 */
#define STISR_RXPL      (1 << 4)        /* Receive Data Polarity */
#define STISR_TXPL      (1 << 3)        /* Transmit Data Polarity */
#define STISR_XMODE     (1 << 2)        /* Transmit Pulse Width Select */
#define STISR_RCVEIR    (1 << 1)        /* Receiver SIR Enable */
#define STISR_XMITIR    (1 << 0)        /* Transmitter SIR Enable */


/*
 * I2C registers
 */

#define IBMR		__REG(0x40301680)  /* I2C Bus Monitor Register - IBMR */
#define IDBR		__REG(0x40301688)  /* I2C Data Buffer Register - IDBR */
#define ICR		__REG(0x40301690)  /* I2C Control Register - ICR */
#define ISR		__REG(0x40301698)  /* I2C Status Register - ISR */
#define ISAR		__REG(0x403016A0)  /* I2C Slave Address Register - ISAR */

#define PWRIBMR    __REG(0x40f00180)  /* Power I2C Bus Monitor Register-IBMR */
#define PWRIDBR    __REG(0x40f00188)  /* Power I2C Data Buffer Register-IDBR */
#define PWRICR __REG(0x40f00190)  /* Power I2C Control Register - ICR */
#define PWRISR __REG(0x40f00198)  /* Power I2C Status Register - ISR */
#define PWRISAR    __REG(0x40f001A0)  /*Power I2C Slave Address Register-ISAR */

#define ICR_START	(1 << 0)	   /* start bit */
#define ICR_STOP	(1 << 1)	   /* stop bit */
#define ICR_ACKNAK	(1 << 2)	   /* send ACK(0) or NAK(1) */
#define ICR_TB		(1 << 3)	   /* transfer byte bit */
#define ICR_MA		(1 << 4)	   /* master abort */
#define ICR_SCLE	(1 << 5)	   /* master clock enable */
#define ICR_IUE		(1 << 6)	   /* unit enable */
#define ICR_GCD		(1 << 7)	   /* general call disable */
#define ICR_ITEIE	(1 << 8)	   /* enable tx interrupts */
#define ICR_IRFIE	(1 << 9)	   /* enable rx interrupts */
#define ICR_BEIE	(1 << 10)	   /* enable bus error ints */
#define ICR_SSDIE	(1 << 11)	   /* slave STOP detected int enable */
#define ICR_ALDIE	(1 << 12)	   /* enable arbitration interrupt */
#define ICR_SADIE	(1 << 13)	   /* slave address detected int enable */
#define ICR_UR		(1 << 14)	   /* unit reset */

#define ISR_RWM		(1 << 0)	   /* read/write mode */
#define ISR_ACKNAK	(1 << 1)	   /* ack/nak status */
#define ISR_UB		(1 << 2)	   /* unit busy */
#define ISR_IBB		(1 << 3)	   /* bus busy */
#define ISR_SSD		(1 << 4)	   /* slave stop detected */
#define ISR_ALD		(1 << 5)	   /* arbitration loss detected */
#define ISR_ITE		(1 << 6)	   /* tx buffer empty */
#define ISR_IRF		(1 << 7)	   /* rx buffer full */
#define ISR_GCAD	(1 << 8)	   /* general call address detected */
#define ISR_SAD		(1 << 9)	   /* slave address detected */
#define ISR_BED		(1 << 10)	   /* bus error no ACK/NAK */


/*
 * Serial Audio Controller
 */

#define SACR0		__REG(0x40400000)  /* Global Control Register */
#define SACR1		__REG(0x40400004)  /* Serial Audio I 2 S/MSB-Justified Control Register */
#define SASR0		__REG(0x4040000C)  /* Serial Audio I 2 S/MSB-Justified Interface and FIFO Status Register */
#define SAIMR		__REG(0x40400014)  /* Serial Audio Interrupt Mask Register */
#define SAICR		__REG(0x40400018)  /* Serial Audio Interrupt Clear Register */
#define SADIV		__REG(0x40400060)  /* Audio Clock Divider Register. */
#define SADR		__REG(0x40400080)  /* Serial Audio Data Register (TX and RX FIFO access Register). */

#define SACR0_RFTH(x)	((x) << 12)	/* Rx FIFO Interrupt or DMA Trigger Threshold */
#define SACR0_TFTH(x)	((x) << 8)	/* Tx FIFO Interrupt or DMA Trigger Threshold */
#define SACR0_STRF	(1 << 5)	/* FIFO Select for EFWR Special Function */
#define SACR0_EFWR	(1 << 4)	/* Enable EFWR Function  */
#define SACR0_RST	(1 << 3)	/* FIFO, i2s Register Reset */
#define SACR0_BCKD	(1 << 2) 	/* Bit Clock Direction */
#define SACR0_ENB	(1 << 0)	/* Enable I2S Link */
#define SACR1_ENLBF	(1 << 5)	/* Enable Loopback */
#define SACR1_DRPL	(1 << 4) 	/* Disable Replaying Function */
#define SACR1_DREC	(1 << 3)	/* Disable Recording Function */
#define SACR1_AMSL	(1 << 0)	/* Specify Alternate Mode */

#define SASR0_I2SOFF	(1 << 7)	/* Controller Status */
#define SASR0_ROR	(1 << 6)	/* Rx FIFO Overrun */
#define SASR0_TUR	(1 << 5)	/* Tx FIFO Underrun */
#define SASR0_RFS	(1 << 4)	/* Rx FIFO Service Request */
#define SASR0_TFS	(1 << 3)	/* Tx FIFO Service Request */
#define SASR0_BSY	(1 << 2)	/* I2S Busy */
#define SASR0_RNE	(1 << 1)	/* Rx FIFO Not Empty */
#define SASR0_TNF	(1 << 0) 	/* Tx FIFO Not Empty */

#define SAICR_ROR	(1 << 6)	/* Clear Rx FIFO Overrun Interrupt */
#define SAICR_TUR	(1 << 5)	/* Clear Tx FIFO Underrun Interrupt */

#define SAIMR_ROR	(1 << 6)	/* Enable Rx FIFO Overrun Condition Interrupt */
#define SAIMR_TUR	(1 << 5)	/* Enable Tx FIFO Underrun Condition Interrupt */
#define SAIMR_RFS	(1 << 4)	/* Enable Rx FIFO Service Interrupt */
#define SAIMR_TFS	(1 << 3)	/* Enable Tx FIFO Service Interrupt */

/*
 * AC97 Controller registers
 */

#define POCR		__REG(0x40500000)  /* PCM Out Control Register */
#define POCR_FEIE	(1 << 3)	/* FIFO Error Interrupt Enable */
#define POCR_FSRIE	(1 << 1)	/* FIFO Service Request Interrupt Enable */

#define PICR		__REG(0x40500004)  /* PCM In Control Register */
#define PICR_FEIE	(1 << 3)	/* FIFO Error Interrupt Enable */
#define PICR_FSRIE	(1 << 1)	/* FIFO Service Request Interrupt Enable */

#define MCCR		__REG(0x40500008)  /* Mic In Control Register */
#define MCCR_FEIE	(1 << 3)	/* FIFO Error Interrupt Enable */
#define MCCR_FSRIE	(1 << 1)	/* FIFO Service Request Interrupt Enable */

#define GCR		__REG(0x4050000C)  /* Global Control Register */
#ifdef CONFIG_PXA3xx
#define GCR_CLKBPB	(1 << 31)	/* Internal clock enable */
#endif
#define GCR_nDMAEN	(1 << 24)	/* non DMA Enable */
#define GCR_CDONE_IE	(1 << 19)	/* Command Done Interrupt Enable */
#define GCR_SDONE_IE	(1 << 18)	/* Status Done Interrupt Enable */
#define GCR_SECRDY_IEN	(1 << 9)	/* Secondary Ready Interrupt Enable */
#define GCR_PRIRDY_IEN	(1 << 8)	/* Primary Ready Interrupt Enable */
#define GCR_SECRES_IEN	(1 << 5)	/* Secondary Resume Interrupt Enable */
#define GCR_PRIRES_IEN	(1 << 4)	/* Primary Resume Interrupt Enable */
#define GCR_ACLINK_OFF	(1 << 3)	/* AC-link Shut Off */
#define GCR_WARM_RST	(1 << 2)	/* AC97 Warm Reset */
#define GCR_COLD_RST	(1 << 1)	/* AC'97 Cold Reset (0 = active) */
#define GCR_GIE		(1 << 0)	/* Codec GPI Interrupt Enable */

#define POSR		__REG(0x40500010)  /* PCM Out Status Register */
#define POSR_FIFOE	(1 << 4)	/* FIFO error */
#define POSR_FSR	(1 << 2)	/* FIFO Service Request */

#define PISR		__REG(0x40500014)  /* PCM In Status Register */
#define PISR_FIFOE	(1 << 4)	/* FIFO error */
#define PISR_EOC	(1 << 3)	/* DMA End-of-Chain (exclusive clear) */
#define PISR_FSR	(1 << 2)	/* FIFO Service Request */

#define MCSR		__REG(0x40500018)  /* Mic In Status Register */
#define MCSR_FIFOE	(1 << 4)	/* FIFO error */
#define MCSR_EOC	(1 << 3)	/* DMA End-of-Chain (exclusive clear) */
#define MCSR_FSR	(1 << 2)	/* FIFO Service Request */

#define GSR		__REG(0x4050001C)  /* Global Status Register */
#define GSR_CDONE	(1 << 19)	/* Command Done */
#define GSR_SDONE	(1 << 18)	/* Status Done */
#define GSR_RDCS	(1 << 15)	/* Read Completion Status */
#define GSR_BIT3SLT12	(1 << 14)	/* Bit 3 of slot 12 */
#define GSR_BIT2SLT12	(1 << 13)	/* Bit 2 of slot 12 */
#define GSR_BIT1SLT12	(1 << 12)	/* Bit 1 of slot 12 */
#define GSR_SECRES	(1 << 11)	/* Secondary Resume Interrupt */
#define GSR_PRIRES	(1 << 10)	/* Primary Resume Interrupt */
#define GSR_SCR		(1 << 9)	/* Secondary Codec Ready */
#define GSR_PCR		(1 << 8)	/*  Primary Codec Ready */
#define GSR_MCINT	(1 << 7)	/* Mic In Interrupt */
#define GSR_POINT	(1 << 6)	/* PCM Out Interrupt */
#define GSR_PIINT	(1 << 5)	/* PCM In Interrupt */
#define GSR_ACOFFD	(1 << 3)	/* AC-link Shut Off Done */
#define GSR_MOINT	(1 << 2)	/* Modem Out Interrupt */
#define GSR_MIINT	(1 << 1)	/* Modem In Interrupt */
#define GSR_GSCI	(1 << 0)	/* Codec GPI Status Change Interrupt */

#define CAR		__REG(0x40500020)  /* CODEC Access Register */
#define CAR_CAIP	(1 << 0)	/* Codec Access In Progress */

#define PCDR		__REG(0x40500040)  /* PCM FIFO Data Register */
#define MCDR		__REG(0x40500060)  /* Mic-in FIFO Data Register */

#define MOCR		__REG(0x40500100)  /* Modem Out Control Register */
#define MOCR_FEIE	(1 << 3)	/* FIFO Error */
#define MOCR_FSRIE	(1 << 1)	/* FIFO Service Request Interrupt Enable */

#define MICR		__REG(0x40500108)  /* Modem In Control Register */
#define MICR_FEIE	(1 << 3)	/* FIFO Error */
#define MICR_FSRIE	(1 << 1)	/* FIFO Service Request Interrupt Enable */

#define MOSR		__REG(0x40500110)  /* Modem Out Status Register */
#define MOSR_FIFOE	(1 << 4)	/* FIFO error */
#define MOSR_FSR	(1 << 2)	/* FIFO Service Request */

#define MISR		__REG(0x40500118)  /* Modem In Status Register */
#define MISR_FIFOE	(1 << 4)	/* FIFO error */
#define MISR_EOC	(1 << 3)	/* DMA End-of-Chain (exclusive clear) */
#define MISR_FSR	(1 << 2)	/* FIFO Service Request */

#define MODR		__REG(0x40500140)  /* Modem FIFO Data Register */

#define PAC_REG_BASE	__REG(0x40500200)  /* Primary Audio Codec */
#define SAC_REG_BASE	__REG(0x40500300)  /* Secondary Audio Codec */
#define PMC_REG_BASE	__REG(0x40500400)  /* Primary Modem Codec */
#define SMC_REG_BASE	__REG(0x40500500)  /* Secondary Modem Codec */


/*
 * Fast Infrared Communication Port
 */

#define FICP		__REG(0x40800000)  /* Start of FICP area */
#define ICCR0		__REG(0x40800000)  /* ICP Control Register 0 */
#define ICCR1		__REG(0x40800004)  /* ICP Control Register 1 */
#define ICCR2		__REG(0x40800008)  /* ICP Control Register 2 */
#define ICDR		__REG(0x4080000c)  /* ICP Data Register */
#define ICSR0		__REG(0x40800014)  /* ICP Status Register 0 */
#define ICSR1		__REG(0x40800018)  /* ICP Status Register 1 */

#define ICCR0_AME	(1 << 7)	/* Address match enable */
#define ICCR0_TIE	(1 << 6)	/* Transmit FIFO interrupt enable */
#define ICCR0_RIE	(1 << 5)	/* Recieve FIFO interrupt enable */
#define ICCR0_RXE	(1 << 4)	/* Receive enable */
#define ICCR0_TXE	(1 << 3)	/* Transmit enable */
#define ICCR0_TUS	(1 << 2)	/* Transmit FIFO underrun select */
#define ICCR0_LBM	(1 << 1)	/* Loopback mode */
#define ICCR0_ITR	(1 << 0)	/* IrDA transmission */

#define ICCR2_RXP       (1 << 3)	/* Receive Pin Polarity select */
#define ICCR2_TXP       (1 << 2)	/* Transmit Pin Polarity select */
#define ICCR2_TRIG	(3 << 0)	/* Receive FIFO Trigger threshold */
#define ICCR2_TRIG_8    (0 << 0)	/* 	>= 8 bytes */
#define ICCR2_TRIG_16   (1 << 0)	/*	>= 16 bytes */
#define ICCR2_TRIG_32   (2 << 0)	/*	>= 32 bytes */

#ifdef CONFIG_PXA27x
#define ICSR0_EOC	(1 << 6)	/* DMA End of Descriptor Chain */
#endif
#define ICSR0_FRE	(1 << 5)	/* Framing error */
#define ICSR0_RFS	(1 << 4)	/* Receive FIFO service request */
#define ICSR0_TFS	(1 << 3)	/* Transnit FIFO service request */
#define ICSR0_RAB	(1 << 2)	/* Receiver abort */
#define ICSR0_TUR	(1 << 1)	/* Trunsmit FIFO underun */
#define ICSR0_EIF	(1 << 0)	/* End/Error in FIFO */

#define ICSR1_ROR	(1 << 6)	/* Receiver FIFO underrun  */
#define ICSR1_CRE	(1 << 5)	/* CRC error */
#define ICSR1_EOF	(1 << 4)	/* End of frame */
#define ICSR1_TNF	(1 << 3)	/* Transmit FIFO not full */
#define ICSR1_RNE	(1 << 2)	/* Receive FIFO not empty */
#define ICSR1_TBY	(1 << 1)	/* Tramsmiter busy flag */
#define ICSR1_RSY	(1 << 0)	/* Recevier synchronized flag */


/*
 * Real Time Clock
 */

#define RCNR		__REG(0x40900000)  /* RTC Count Register */
#define RTAR		__REG(0x40900004)  /* RTC Alarm Register */
#define RTSR		__REG(0x40900008)  /* RTC Status Register */
#define RTTR		__REG(0x4090000C)  /* RTC Timer Trim Register */
#define PIAR		__REG(0x40900038)  /* Periodic Interrupt Alarm Register */

#define RTSR_PICE	(1 << 15)	/* Periodic interrupt count enable */
#define RTSR_PIALE	(1 << 14)	/* Periodic interrupt Alarm enable */
#define RTSR_HZE	(1 << 3)	/* HZ interrupt enable */
#define RTSR_ALE	(1 << 2)	/* RTC alarm interrupt enable */
#define RTSR_HZ		(1 << 1)	/* HZ rising-edge detected */
#define RTSR_AL		(1 << 0)	/* RTC alarm detected */


/*
 * OS Timer & Match Registers
 */

#define OSMR0		__REG(0x40A00000)  /* */
#define OSMR1		__REG(0x40A00004)  /* */
#define OSMR2		__REG(0x40A00008)  /* */
#define OSMR3		__REG(0x40A0000C)  /* */
#define OSMR4		__REG(0x40A00080)  /* */
#define OSCR		__REG(0x40A00010)  /* OS Timer Counter Register */
#define OSCR4		__REG(0x40A00040)  /* OS Timer Counter Register */
#define OMCR4		__REG(0x40A000C0)  /* */
#define OSSR		__REG(0x40A00014)  /* OS Timer Status Register */
#define OWER		__REG(0x40A00018)  /* OS Timer Watchdog Enable Register */
#define OIER		__REG(0x40A0001C)  /* OS Timer Interrupt Enable Register */

#define OSSR_M3		(1 << 3)	/* Match status channel 3 */
#define OSSR_M2		(1 << 2)	/* Match status channel 2 */
#define OSSR_M1		(1 << 1)	/* Match status channel 1 */
#define OSSR_M0		(1 << 0)	/* Match status channel 0 */

#define OWER_WME	(1 << 0)	/* Watchdog Match Enable */

#define OIER_E3		(1 << 3)	/* Interrupt enable channel 3 */
#define OIER_E2		(1 << 2)	/* Interrupt enable channel 2 */
#define OIER_E1		(1 << 1)	/* Interrupt enable channel 1 */
#define OIER_E0		(1 << 0)	/* Interrupt enable channel 0 */


/*
 * Pulse Width Modulator
 */

#define PWM_CTRL0	__REG(0x40B00000)  /* PWM 0 Control Register */
#define PWM_PWDUTY0	__REG(0x40B00004)  /* PWM 0 Duty Cycle Register */
#define PWM_PERVAL0	__REG(0x40B00008)  /* PWM 0 Period Control Register */

#define PWM_CTRL1	__REG(0x40C00000)  /* PWM 1Control Register */
#define PWM_PWDUTY1	__REG(0x40C00004)  /* PWM 1 Duty Cycle Register */
#define PWM_PERVAL1	__REG(0x40C00008)  /* PWM 1 Period Control Register */


/*
 * Interrupt Controller
 */

#define ICIP		__REG(0x40D00000)  /* Interrupt Controller IRQ Pending Register */
#define ICMR		__REG(0x40D00004)  /* Interrupt Controller Mask Register */
#define ICLR		__REG(0x40D00008)  /* Interrupt Controller Level Register */
#define ICFP		__REG(0x40D0000C)  /* Interrupt Controller FIQ Pending Register */
#define ICPR		__REG(0x40D00010)  /* Interrupt Controller Pending Register */
#define ICCR		__REG(0x40D00014)  /* Interrupt Controller Control Register */

#define ICIP2		__REG(0x40D0009C)  /* Interrupt Controller IRQ Pending Register 2 */
#define ICMR2		__REG(0x40D000A0)  /* Interrupt Controller Mask Register 2 */
#define ICLR2		__REG(0x40D000A4)  /* Interrupt Controller Level Register 2 */
#define ICFP2		__REG(0x40D000A8)  /* Interrupt Controller FIQ Pending Register 2 */
#define ICPR2		__REG(0x40D000AC)  /* Interrupt Controller Pending Register 2 */

/*
 * General Purpose I/O
 */

#define GPIO0_BASE	((void __iomem *)io_p2v(0x40E00000))
#define GPIO1_BASE	((void __iomem *)io_p2v(0x40E00004))
#define GPIO2_BASE	((void __iomem *)io_p2v(0x40E00008))
#define GPIO3_BASE	((void __iomem *)io_p2v(0x40E00100))

#define GPLR_OFFSET	0x00
#define GPDR_OFFSET	0x0C
#define GPSR_OFFSET	0x18
#define GPCR_OFFSET	0x24
#define GRER_OFFSET	0x30
#define GFER_OFFSET	0x3C
#define GEDR_OFFSET	0x48

#define GPLR0		__REG(0x40E00000)  /* GPIO Pin-Level Register GPIO<31:0> */
#define GPLR1		__REG(0x40E00004)  /* GPIO Pin-Level Register GPIO<63:32> */
#define GPLR2		__REG(0x40E00008)  /* GPIO Pin-Level Register GPIO<80:64> */

#define GPDR0		__REG(0x40E0000C)  /* GPIO Pin Direction Register GPIO<31:0> */
#define GPDR1		__REG(0x40E00010)  /* GPIO Pin Direction Register GPIO<63:32> */
#define GPDR2		__REG(0x40E00014)  /* GPIO Pin Direction Register GPIO<80:64> */

#define GPSR0		__REG(0x40E00018)  /* GPIO Pin Output Set Register GPIO<31:0> */
#define GPSR1		__REG(0x40E0001C)  /* GPIO Pin Output Set Register GPIO<63:32> */
#define GPSR2		__REG(0x40E00020)  /* GPIO Pin Output Set Register GPIO<80:64> */

#define GPCR0		__REG(0x40E00024)  /* GPIO Pin Output Clear Register GPIO<31:0> */
#define GPCR1		__REG(0x40E00028)  /* GPIO Pin Output Clear Register GPIO <63:32> */
#define GPCR2		__REG(0x40E0002C)  /* GPIO Pin Output Clear Register GPIO <80:64> */

#define GRER0		__REG(0x40E00030)  /* GPIO Rising-Edge Detect Register GPIO<31:0> */
#define GRER1		__REG(0x40E00034)  /* GPIO Rising-Edge Detect Register GPIO<63:32> */
#define GRER2		__REG(0x40E00038)  /* GPIO Rising-Edge Detect Register GPIO<80:64> */

#define GFER0		__REG(0x40E0003C)  /* GPIO Falling-Edge Detect Register GPIO<31:0> */
#define GFER1		__REG(0x40E00040)  /* GPIO Falling-Edge Detect Register GPIO<63:32> */
#define GFER2		__REG(0x40E00044)  /* GPIO Falling-Edge Detect Register GPIO<80:64> */

#define GEDR0		__REG(0x40E00048)  /* GPIO Edge Detect Status Register GPIO<31:0> */
#define GEDR1		__REG(0x40E0004C)  /* GPIO Edge Detect Status Register GPIO<63:32> */
#define GEDR2		__REG(0x40E00050)  /* GPIO Edge Detect Status Register GPIO<80:64> */

#define GAFR0_L		__REG(0x40E00054)  /* GPIO Alternate Function Select Register GPIO<15:0> */
#define GAFR0_U		__REG(0x40E00058)  /* GPIO Alternate Function Select Register GPIO<31:16> */
#define GAFR1_L		__REG(0x40E0005C)  /* GPIO Alternate Function Select Register GPIO<47:32> */
#define GAFR1_U		__REG(0x40E00060)  /* GPIO Alternate Function Select Register GPIO<63:48> */
#define GAFR2_L		__REG(0x40E00064)  /* GPIO Alternate Function Select Register GPIO<79:64> */
#define GAFR2_U		__REG(0x40E00068)  /* GPIO Alternate Function Select Register GPIO<95-80> */
#define GAFR3_L		__REG(0x40E0006C)  /* GPIO Alternate Function Select Register GPIO<111:96> */
#define GAFR3_U		__REG(0x40E00070)  /* GPIO Alternate Function Select Register GPIO<127:112> */

#define GPLR3		__REG(0x40E00100)  /* GPIO Pin-Level Register GPIO<127:96> */
#define GPDR3		__REG(0x40E0010C)  /* GPIO Pin Direction Register GPIO<127:96> */
#define GPSR3		__REG(0x40E00118)  /* GPIO Pin Output Set Register GPIO<127:96> */
#define GPCR3		__REG(0x40E00124)  /* GPIO Pin Output Clear Register GPIO<127:96> */
#define GRER3		__REG(0x40E00130)  /* GPIO Rising-Edge Detect Register GPIO<127:96> */
#define GFER3		__REG(0x40E0013C)  /* GPIO Falling-Edge Detect Register GPIO<127:96> */
#define GEDR3		__REG(0x40E00148)  /* GPIO Edge Detect Status Register GPIO<127:96> */

/* More handy macros.  The argument is a literal GPIO number. */

#define GPIO_bit(x)	(1 << ((x) & 0x1f))

#if defined(CONFIG_PXA27x) || defined(CONFIG_PXA3xx)

/* Interrupt Controller */

#define _GPLR(x)	__REG2(0x40E00000, ((x) & 0x60) >> 3)
#define _GPDR(x)	__REG2(0x40E0000C, ((x) & 0x60) >> 3)
#define _GPSR(x)	__REG2(0x40E00018, ((x) & 0x60) >> 3)
#define _GPCR(x)	__REG2(0x40E00024, ((x) & 0x60) >> 3)
#define _GRER(x)	__REG2(0x40E00030, ((x) & 0x60) >> 3)
#define _GFER(x)	__REG2(0x40E0003C, ((x) & 0x60) >> 3)
#define _GEDR(x)	__REG2(0x40E00048, ((x) & 0x60) >> 3)
#define _GAFR(x)	__REG2(0x40E00054, ((x) & 0x70) >> 2)

#define GPLR(x) 	(*((((x) & 0x7f) < 96) ? &_GPLR(x) : &GPLR3))
#define GPDR(x)		(*((((x) & 0x7f) < 96) ? &_GPDR(x) : &GPDR3))
#define GPSR(x)		(*((((x) & 0x7f) < 96) ? &_GPSR(x) : &GPSR3))
#define GPCR(x)		(*((((x) & 0x7f) < 96) ? &_GPCR(x) : &GPCR3))
#define GRER(x)		(*((((x) & 0x7f) < 96) ? &_GRER(x) : &GRER3))
#define GFER(x)		(*((((x) & 0x7f) < 96) ? &_GFER(x) : &GFER3))
#define GEDR(x)		(*((((x) & 0x7f) < 96) ? &_GEDR(x) : &GEDR3))
#define GAFR(x)		(*((((x) & 0x7f) < 96) ? &_GAFR(x) : \
			 ((((x) & 0x7f) < 112) ? &GAFR3_L : &GAFR3_U)))
#else

#define GPLR(x)		__REG2(0x40E00000, ((x) & 0x60) >> 3)
#define GPDR(x)		__REG2(0x40E0000C, ((x) & 0x60) >> 3)
#define GPSR(x)		__REG2(0x40E00018, ((x) & 0x60) >> 3)
#define GPCR(x)		__REG2(0x40E00024, ((x) & 0x60) >> 3)
#define GRER(x)		__REG2(0x40E00030, ((x) & 0x60) >> 3)
#define GFER(x)		__REG2(0x40E0003C, ((x) & 0x60) >> 3)
#define GEDR(x)		__REG2(0x40E00048, ((x) & 0x60) >> 3)
#define GAFR(x)		__REG2(0x40E00054, ((x) & 0x70) >> 2)

#endif

/*
 * Power Manager - see pxa2xx-regs.h
 */

/*
 * SSP Serial Port Registers - see arch/arm/mach-pxa/include/mach/regs-ssp.h
 */

/*
 * MultiMediaCard (MMC) controller - see drivers/mmc/host/pxamci.h
 */

/*
 * Core Clock - see arch/arm/mach-pxa/include/mach/pxa2xx-regs.h
 */

#ifdef CONFIG_PXA27x

/* Camera Interface */
#define CICR0		__REG(0x50000000)
#define CICR1		__REG(0x50000004)
#define CICR2		__REG(0x50000008)
#define CICR3		__REG(0x5000000C)
#define CICR4		__REG(0x50000010)
#define CISR		__REG(0x50000014)
#define CIFR		__REG(0x50000018)
#define CITOR		__REG(0x5000001C)
#define CIBR0		__REG(0x50000028)
#define CIBR1		__REG(0x50000030)
#define CIBR2		__REG(0x50000038)

#define CICR0_DMAEN	(1 << 31)	/* DMA request enable */
#define CICR0_PAR_EN	(1 << 30)	/* Parity enable */
#define CICR0_SL_CAP_EN	(1 << 29)	/* Capture enable for slave mode */
#define CICR0_ENB	(1 << 28)	/* Camera interface enable */
#define CICR0_DIS	(1 << 27)	/* Camera interface disable */
#define CICR0_SIM	(0x7 << 24)	/* Sensor interface mode mask */
#define CICR0_TOM	(1 << 9)	/* Time-out mask */
#define CICR0_RDAVM	(1 << 8)	/* Receive-data-available mask */
#define CICR0_FEM	(1 << 7)	/* FIFO-empty mask */
#define CICR0_EOLM	(1 << 6)	/* End-of-line mask */
#define CICR0_PERRM	(1 << 5)	/* Parity-error mask */
#define CICR0_QDM	(1 << 4)	/* Quick-disable mask */
#define CICR0_CDM	(1 << 3)	/* Disable-done mask */
#define CICR0_SOFM	(1 << 2)	/* Start-of-frame mask */
#define CICR0_EOFM	(1 << 1)	/* End-of-frame mask */
#define CICR0_FOM	(1 << 0)	/* FIFO-overrun mask */

#define CICR1_TBIT	(1 << 31)	/* Transparency bit */
#define CICR1_RGBT_CONV	(0x3 << 29)	/* RGBT conversion mask */
#define CICR1_PPL	(0x7ff << 15)	/* Pixels per line mask */
#define CICR1_RGB_CONV	(0x7 << 12)	/* RGB conversion mask */
#define CICR1_RGB_F	(1 << 11)	/* RGB format */
#define CICR1_YCBCR_F	(1 << 10)	/* YCbCr format */
#define CICR1_RGB_BPP	(0x7 << 7)	/* RGB bis per pixel mask */
#define CICR1_RAW_BPP	(0x3 << 5)	/* Raw bis per pixel mask */
#define CICR1_COLOR_SP	(0x3 << 3)	/* Color space mask */
#define CICR1_DW	(0x7 << 0)	/* Data width mask */

#define CICR2_BLW	(0xff << 24)	/* Beginning-of-line pixel clock
					   wait count mask */
#define CICR2_ELW	(0xff << 16)	/* End-of-line pixel clock
					   wait count mask */
#define CICR2_HSW	(0x3f << 10)	/* Horizontal sync pulse width mask */
#define CICR2_BFPW	(0x3f << 3)	/* Beginning-of-frame pixel clock
					   wait count mask */
#define CICR2_FSW	(0x7 << 0)	/* Frame stabilization
					   wait count mask */

#define CICR3_BFW	(0xff << 24)	/* Beginning-of-frame line clock
					   wait count mask */
#define CICR3_EFW	(0xff << 16)	/* End-of-frame line clock
					   wait count mask */
#define CICR3_VSW	(0x3f << 10)	/* Vertical sync pulse width mask */
#define CICR3_BFPW	(0x3f << 3)	/* Beginning-of-frame pixel clock
					   wait count mask */
#define CICR3_LPF	(0x7ff << 0)	/* Lines per frame mask */

#define CICR4_MCLK_DLY	(0x3 << 24)	/* MCLK Data Capture Delay mask */
#define CICR4_PCLK_EN	(1 << 23)	/* Pixel clock enable */
#define CICR4_PCP	(1 << 22)	/* Pixel clock polarity */
#define CICR4_HSP	(1 << 21)	/* Horizontal sync polarity */
#define CICR4_VSP	(1 << 20)	/* Vertical sync polarity */
#define CICR4_MCLK_EN	(1 << 19)	/* MCLK enable */
#define CICR4_FR_RATE	(0x7 << 8)	/* Frame rate mask */
#define CICR4_DIV	(0xff << 0)	/* Clock divisor mask */

#define CISR_FTO	(1 << 15)	/* FIFO time-out */
#define CISR_RDAV_2	(1 << 14)	/* Channel 2 receive data available */
#define CISR_RDAV_1	(1 << 13)	/* Channel 1 receive data available */
#define CISR_RDAV_0	(1 << 12)	/* Channel 0 receive data available */
#define CISR_FEMPTY_2	(1 << 11)	/* Channel 2 FIFO empty */
#define CISR_FEMPTY_1	(1 << 10)	/* Channel 1 FIFO empty */
#define CISR_FEMPTY_0	(1 << 9)	/* Channel 0 FIFO empty */
#define CISR_EOL	(1 << 8)	/* End of line */
#define CISR_PAR_ERR	(1 << 7)	/* Parity error */
#define CISR_CQD	(1 << 6)	/* Camera interface quick disable */
#define CISR_CDD	(1 << 5)	/* Camera interface disable done */
#define CISR_SOF	(1 << 4)	/* Start of frame */
#define CISR_EOF	(1 << 3)	/* End of frame */
#define CISR_IFO_2	(1 << 2)	/* FIFO overrun for Channel 2 */
#define CISR_IFO_1	(1 << 1)	/* FIFO overrun for Channel 1 */
#define CISR_IFO_0	(1 << 0)	/* FIFO overrun for Channel 0 */

#define CIFR_FLVL2	(0x7f << 23)	/* FIFO 2 level mask */
#define CIFR_FLVL1	(0x7f << 16)	/* FIFO 1 level mask */
#define CIFR_FLVL0	(0xff << 8)	/* FIFO 0 level mask */
#define CIFR_THL_0	(0x3 << 4)	/* Threshold Level for Channel 0 FIFO */
#define CIFR_RESET_F	(1 << 3)	/* Reset input FIFOs */
#define CIFR_FEN2	(1 << 2)	/* FIFO enable for channel 2 */
#define CIFR_FEN1	(1 << 1)	/* FIFO enable for channel 1 */
#define CIFR_FEN0	(1 << 0)	/* FIFO enable for channel 0 */

#define SRAM_SIZE		0x40000 /* 4x64K  */

#define SRAM_MEM_PHYS		0x5C000000

#define IMPMCR		__REG(0x58000000) /* IM Power Management Control Reg */
#define IMPMSR		__REG(0x58000008) /* IM Power Management Status Reg */

#define IMPMCR_PC3		(0x3 << 22) /* Bank 3 Power Control */
#define IMPMCR_PC3_RUN_MODE	(0x0 << 22) /*   Run mode */
#define IMPMCR_PC3_STANDBY_MODE	(0x1 << 22) /*   Standby mode */
#define IMPMCR_PC3_AUTO_MODE	(0x3 << 22) /*   Automatically controlled */

#define IMPMCR_PC2		(0x3 << 20) /* Bank 2 Power Control */
#define IMPMCR_PC2_RUN_MODE	(0x0 << 20) /*   Run mode */
#define IMPMCR_PC2_STANDBY_MODE	(0x1 << 20) /*   Standby mode */
#define IMPMCR_PC2_AUTO_MODE	(0x3 << 20) /*   Automatically controlled */

#define IMPMCR_PC1		(0x3 << 18) /* Bank 1 Power Control */
#define IMPMCR_PC1_RUN_MODE	(0x0 << 18) /*   Run mode */
#define IMPMCR_PC1_STANDBY_MODE	(0x1 << 18) /*   Standby mode */
#define IMPMCR_PC1_AUTO_MODE	(0x3 << 18) /*   Automatically controlled */

#define IMPMCR_PC0		(0x3 << 16) /* Bank 0 Power Control */
#define IMPMCR_PC0_RUN_MODE	(0x0 << 16) /*   Run mode */
#define IMPMCR_PC0_STANDBY_MODE	(0x1 << 16) /*   Standby mode */
#define IMPMCR_PC0_AUTO_MODE	(0x3 << 16) /*   Automatically controlled */

#define IMPMCR_AW3		(1 << 11) /* Bank 3 Automatic Wake-up enable */
#define IMPMCR_AW2		(1 << 10) /* Bank 2 Automatic Wake-up enable */
#define IMPMCR_AW1		(1 << 9)  /* Bank 1 Automatic Wake-up enable */
#define IMPMCR_AW0		(1 << 8)  /* Bank 0 Automatic Wake-up enable */

#define IMPMCR_DST		(0xFF << 0) /* Delay Standby Time, ms */

#define IMPMSR_PS3		(0x3 << 6) /* Bank 3 Power Status: */
#define IMPMSR_PS3_RUN_MODE	(0x0 << 6) /*    Run mode */
#define IMPMSR_PS3_STANDBY_MODE	(0x1 << 6) /*    Standby mode */

#define IMPMSR_PS2		(0x3 << 4) /* Bank 2 Power Status: */
#define IMPMSR_PS2_RUN_MODE	(0x0 << 4) /*    Run mode */
#define IMPMSR_PS2_STANDBY_MODE	(0x1 << 4) /*    Standby mode */

#define IMPMSR_PS1		(0x3 << 2) /* Bank 1 Power Status: */
#define IMPMSR_PS1_RUN_MODE	(0x0 << 2) /*    Run mode */
#define IMPMSR_PS1_STANDBY_MODE	(0x1 << 2) /*    Standby mode */

#define IMPMSR_PS0		(0x3 << 0) /* Bank 0 Power Status: */
#define IMPMSR_PS0_RUN_MODE	(0x0 << 0) /*    Run mode */
#define IMPMSR_PS0_STANDBY_MODE	(0x1 << 0) /*    Standby mode */

#endif

#if defined(CONFIG_PXA27x) || defined(CONFIG_PXA3xx)
/*
 * UHC: USB Host Controller (OHCI-like) register definitions
 */
#define UHC_BASE_PHYS	(0x4C000000)
#define UHCREV		__REG(0x4C000000) /* UHC HCI Spec Revision */
#define UHCHCON		__REG(0x4C000004) /* UHC Host Control Register */
#define UHCCOMS		__REG(0x4C000008) /* UHC Command Status Register */
#define UHCINTS		__REG(0x4C00000C) /* UHC Interrupt Status Register */
#define UHCINTE		__REG(0x4C000010) /* UHC Interrupt Enable */
#define UHCINTD		__REG(0x4C000014) /* UHC Interrupt Disable */
#define UHCHCCA		__REG(0x4C000018) /* UHC Host Controller Comm. Area */
#define UHCPCED		__REG(0x4C00001C) /* UHC Period Current Endpt Descr */
#define UHCCHED		__REG(0x4C000020) /* UHC Control Head Endpt Descr */
#define UHCCCED		__REG(0x4C000024) /* UHC Control Current Endpt Descr */
#define UHCBHED		__REG(0x4C000028) /* UHC Bulk Head Endpt Descr */
#define UHCBCED		__REG(0x4C00002C) /* UHC Bulk Current Endpt Descr */
#define UHCDHEAD	__REG(0x4C000030) /* UHC Done Head */
#define UHCFMI		__REG(0x4C000034) /* UHC Frame Interval */
#define UHCFMR		__REG(0x4C000038) /* UHC Frame Remaining */
#define UHCFMN		__REG(0x4C00003C) /* UHC Frame Number */
#define UHCPERS		__REG(0x4C000040) /* UHC Periodic Start */
#define UHCLS		__REG(0x4C000044) /* UHC Low Speed Threshold */

#define UHCRHDA		__REG(0x4C000048) /* UHC Root Hub Descriptor A */
#define UHCRHDA_NOCP	(1 << 12)	/* No over current protection */

#define UHCRHDB		__REG(0x4C00004C) /* UHC Root Hub Descriptor B */
#define UHCRHS		__REG(0x4C000050) /* UHC Root Hub Status */
#define UHCRHPS1	__REG(0x4C000054) /* UHC Root Hub Port 1 Status */
#define UHCRHPS2	__REG(0x4C000058) /* UHC Root Hub Port 2 Status */
#define UHCRHPS3	__REG(0x4C00005C) /* UHC Root Hub Port 3 Status */

#define UHCSTAT		__REG(0x4C000060) /* UHC Status Register */
#define UHCSTAT_UPS3	(1 << 16)	/* USB Power Sense Port3 */
#define UHCSTAT_SBMAI	(1 << 15)	/* System Bus Master Abort Interrupt*/
#define UHCSTAT_SBTAI	(1 << 14)	/* System Bus Target Abort Interrupt*/
#define UHCSTAT_UPRI	(1 << 13)	/* USB Port Resume Interrupt */
#define UHCSTAT_UPS2	(1 << 12)	/* USB Power Sense Port 2 */
#define UHCSTAT_UPS1	(1 << 11)	/* USB Power Sense Port 1 */
#define UHCSTAT_HTA	(1 << 10)	/* HCI Target Abort */
#define UHCSTAT_HBA	(1 << 8)	/* HCI Buffer Active */
#define UHCSTAT_RWUE	(1 << 7)	/* HCI Remote Wake Up Event */

#define UHCHR           __REG(0x4C000064) /* UHC Reset Register */
#define UHCHR_SSEP3	(1 << 11)	/* Sleep Standby Enable for Port3 */
#define UHCHR_SSEP2	(1 << 10)	/* Sleep Standby Enable for Port2 */
#define UHCHR_SSEP1	(1 << 9)	/* Sleep Standby Enable for Port1 */
#define UHCHR_PCPL	(1 << 7)	/* Power control polarity low */
#define UHCHR_PSPL	(1 << 6)	/* Power sense polarity low */
#define UHCHR_SSE	(1 << 5)	/* Sleep Standby Enable */
#define UHCHR_UIT	(1 << 4)	/* USB Interrupt Test */
#define UHCHR_SSDC	(1 << 3)	/* Simulation Scale Down Clock */
#define UHCHR_CGR	(1 << 2)	/* Clock Generation Reset */
#define UHCHR_FHR	(1 << 1)	/* Force Host Controller Reset */
#define UHCHR_FSBIR	(1 << 0)	/* Force System Bus Iface Reset */

#define UHCHIE          __REG(0x4C000068) /* UHC Interrupt Enable Register*/
#define UHCHIE_UPS3IE	(1 << 14)	/* Power Sense Port3 IntEn */
#define UHCHIE_UPRIE	(1 << 13)	/* Port Resume IntEn */
#define UHCHIE_UPS2IE	(1 << 12)	/* Power Sense Port2 IntEn */
#define UHCHIE_UPS1IE	(1 << 11)	/* Power Sense Port1 IntEn */
#define UHCHIE_TAIE	(1 << 10)	/* HCI Interface Transfer Abort
					   Interrupt Enable*/
#define UHCHIE_HBAIE	(1 << 8)	/* HCI Buffer Active IntEn */
#define UHCHIE_RWIE	(1 << 7)	/* Remote Wake-up IntEn */

#define UHCHIT          __REG(0x4C00006C) /* UHC Interrupt Test register */

#endif /* CONFIG_PXA27x || CONFIG_PXA3xx */

/* PWRMODE register M field values */

#define PWRMODE_IDLE		0x1
#define PWRMODE_STANDBY		0x2
#define PWRMODE_SLEEP		0x3
#define PWRMODE_DEEPSLEEP	0x7

#endif
