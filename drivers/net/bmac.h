/*
 * mace.h - definitions for the registers in the "Big Mac"
 *  Ethernet controller found in PowerMac G3 models.
 *
 * Copyright (C) 1998 Randy Gobbel.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/* The "Big MAC" appears to have some parts in common with the Sun "Happy Meal"
 * (HME) controller.  See sunhme.h
 */

 
/* register offsets */

/* global status and control */
#define	XIFC		0x000   /* low-level interface control */
#	define	TxOutputEnable	0x0001 /* output driver enable */
#	define	XIFLoopback	0x0002 /* Loopback-mode XIF enable */
#	define	MIILoopback	0x0004 /* Loopback-mode MII enable */
#	define	MIILoopbackBits	0x0006
#	define	MIIBuffDisable	0x0008 /* MII receive buffer disable */
#	define	SQETestEnable	0x0010 /* SQE test enable */
#	define	SQETimeWindow	0x03e0 /* SQE time window */
#	define	XIFLanceMode	0x0010 /* Lance mode enable */
#	define	XIFLanceIPG0	0x03e0 /* Lance mode IPG0 */
#define	TXFIFOCSR	0x100   /* transmit FIFO control */
#	define	TxFIFOEnable	0x0001
#define	TXTH		0x110   /* transmit threshold */
#	define	TxThreshold	0x0004
#define RXFIFOCSR	0x120   /* receive FIFO control */
#	define	RxFIFOEnable	0x0001
#define MEMADD		0x130   /* memory address, unknown function */
#define MEMDATAHI	0x140   /* memory data high, presently unused in driver */
#define MEMDATALO	0x150   /* memory data low, presently unused in driver */
#define XCVRIF		0x160   /* transceiver interface control */
#	define	COLActiveLow	0x0002
#	define	SerialMode	0x0004
#	define	ClkBit		0x0008
#	define	LinkStatus	0x0100
#define CHIPID          0x170   /* chip ID */
#define	MIFCSR		0x180   /* ??? */
#define	SROMCSR		0x190   /* SROM control */
#	define	ChipSelect	0x0001
#	define	Clk		0x0002
#define TXPNTR		0x1a0   /* transmit pointer */
#define	RXPNTR		0x1b0   /* receive pointer */
#define	STATUS		0x200   /* status--reading this clears it */
#define	INTDISABLE	0x210   /* interrupt enable/disable control */
/* bits below are the same in both STATUS and INTDISABLE registers */
#	define	FrameReceived	0x00000001 /* Received a frame */
#	define	RxFrameCntExp	0x00000002 /* Receive frame counter expired */
#	define	RxAlignCntExp	0x00000004 /* Align-error counter expired */
#	define	RxCRCCntExp	0x00000008 /* CRC-error counter expired */
#	define	RxLenCntExp	0x00000010 /* Length-error counter expired */
#	define	RxOverFlow	0x00000020 /* Receive FIFO overflow */
#	define	RxCodeViolation	0x00000040 /* Code-violation counter expired */
#	define	SQETestError	0x00000080 /* Test error in XIF for SQE */
#	define	FrameSent	0x00000100 /* Transmitted a frame */
#	define	TxUnderrun	0x00000200 /* Transmit FIFO underrun */
#	define	TxMaxSizeError	0x00000400 /* Max-packet size error */
#	define	TxNormalCollExp	0x00000800 /* Normal-collision counter expired */
#	define	TxExcessCollExp	0x00001000 /* Excess-collision counter expired */
#	define	TxLateCollExp	0x00002000 /* Late-collision counter expired */
#	define	TxNetworkCollExp 0x00004000 /* First-collision counter expired */
#	define	TxDeferTimerExp	0x00008000 /* Defer-timer expired */
#	define	RxFIFOToHost	0x00010000 /* Data moved from FIFO to host */
#	define	RxNoDescriptors	0x00020000 /* No more receive descriptors */
#	define	RxDMAError	0x00040000 /* Error during receive DMA */
#	define	RxDMALateErr	0x00080000 /* Receive DMA, data late */
#	define	RxParityErr	0x00100000 /* Parity error during receive DMA */
#	define	RxTagError	0x00200000 /* Tag error during receive DMA */
#	define	TxEOPError	0x00400000 /* Tx descriptor did not have EOP set */
#	define	MIFIntrEvent	0x00800000 /* MIF is signaling an interrupt */
#	define	TxHostToFIFO	0x01000000 /* Data moved from host to FIFO  */
#	define	TxFIFOAllSent	0x02000000 /* Transmitted all packets in FIFO */
#	define	TxDMAError	0x04000000 /* Error during transmit DMA */
#	define	TxDMALateError	0x08000000 /* Late error during transmit DMA */
#	define	TxParityError	0x10000000 /* Parity error during transmit DMA */
#	define	TxTagError	0x20000000 /* Tag error during transmit DMA */
#	define	PIOError	0x40000000 /* PIO access got an error */
#	define	PIOParityError	0x80000000 /* PIO access got a parity error  */
#	define	DisableAll	0xffffffff
#	define	EnableAll	0x00000000
/* #	define	NormalIntEvents	~(FrameReceived | FrameSent | TxUnderrun) */
#	define	EnableNormal	~(FrameReceived | FrameSent)
#	define	EnableErrors	(FrameReceived | FrameSent)
#	define	RxErrorMask	(RxFrameCntExp | RxAlignCntExp | RxCRCCntExp | \
				 RxLenCntExp | RxOverFlow | RxCodeViolation)
#	define	TxErrorMask	(TxUnderrun | TxMaxSizeError | TxExcessCollExp | \
				 TxLateCollExp | TxNetworkCollExp | TxDeferTimerExp)

/* transmit control */
#define	TXRST		0x420   /* transmit reset */
#	define	TxResetBit	0x0001
#define	TXCFG		0x430   /* transmit configuration control*/
#	define	TxMACEnable	0x0001 /* output driver enable */
#	define	TxSlowMode	0x0020 /* enable slow mode */
#	define	TxIgnoreColl	0x0040 /* ignore transmit collisions */
#	define	TxNoFCS		0x0080 /* do not emit FCS */
#	define	TxNoBackoff	0x0100 /* no backoff in case of collisions */
#	define	TxFullDuplex	0x0200 /* enable full-duplex */
#	define	TxNeverGiveUp	0x0400 /* don't give up on transmits */
#define IPG1		0x440   /* Inter-packet gap 1 */
#define IPG2		0x450   /* Inter-packet gap 2 */
#define ALIMIT		0x460   /* Transmit attempt limit */
#define SLOT		0x470   /* Transmit slot time */
#define PALEN		0x480   /* Size of transmit preamble */
#define PAPAT		0x490   /* Pattern for transmit preamble */
#define TXSFD		0x4a0   /* Transmit frame delimiter */
#define JAM		0x4b0   /* Jam size */
#define TXMAX		0x4c0   /* Transmit max pkt size */
#define TXMIN		0x4d0   /* Transmit min pkt size */
#define PAREG		0x4e0   /* Count of transmit peak attempts */
#define DCNT		0x4f0   /* Transmit defer timer */
#define NCCNT		0x500   /* Transmit normal-collision counter */
#define NTCNT		0x510   /* Transmit first-collision counter */
#define EXCNT		0x520   /* Transmit excess-collision counter */
#define LTCNT		0x530   /* Transmit late-collision counter */
#define RSEED		0x540   /* Transmit random number seed */
#define TXSM		0x550   /* Transmit state machine */

/* receive control */
#define RXRST		0x620   /* receive reset */
#	define	RxResetValue	0x0000
#define RXCFG		0x630   /* receive configuration control */
#	define	RxMACEnable	0x0001 /* receiver overall enable */
#	define	RxCFGReserved	0x0004
#	define	RxPadStripEnab	0x0020 /* enable pad byte stripping */
#	define	RxPromiscEnable	0x0040 /* turn on promiscuous mode */
#	define	RxNoErrCheck	0x0080 /* disable receive error checking */
#	define	RxCRCNoStrip	0x0100 /* disable auto-CRC-stripping */
#	define	RxRejectOwnPackets 0x0200 /* don't receive our own packets */
#	define	RxGrpPromisck	0x0400 /* enable group promiscuous mode */
#	define	RxHashFilterEnable 0x0800 /* enable hash filter */
#	define	RxAddrFilterEnable 0x1000 /* enable address filter */
#define RXMAX		0x640   /* Max receive packet size */
#define RXMIN		0x650   /* Min receive packet size */
#define MADD2		0x660   /* our enet address, high part */
#define MADD1		0x670   /* our enet address, middle part */
#define MADD0		0x680   /* our enet address, low part */
#define FRCNT		0x690   /* receive frame counter */
#define LECNT		0x6a0   /* Receive excess length error counter */
#define AECNT		0x6b0   /* Receive misaligned error counter */
#define FECNT		0x6c0   /* Receive CRC error counter */
#define RXSM		0x6d0   /* Receive state machine */
#define RXCV		0x6e0   /* Receive code violation */

#define BHASH3		0x700   /* multicast hash register */
#define BHASH2		0x710   /* multicast hash register */
#define BHASH1		0x720   /* multicast hash register */
#define BHASH0		0x730   /* multicast hash register */

#define AFR2		0x740   /* address filtering setup? */
#define AFR1		0x750   /* address filtering setup? */
#define AFR0		0x760   /* address filtering setup? */
#define AFCR		0x770   /* address filter compare register? */
#	define	EnableAllCompares 0x0fff

/* bits in XIFC */
