/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * mace.h - definitions for the registers in the Am79C940 MACE
 * (Medium Access Control for Ethernet) controller.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */

#define REG(x)	volatile unsigned char x; char x ## _pad[15]

struct mace {
	REG(rcvfifo);		/* receive FIFO */
	REG(xmtfifo);		/* transmit FIFO */
	REG(xmtfc);		/* transmit frame control */
	REG(xmtfs);		/* transmit frame status */
	REG(xmtrc);		/* transmit retry count */
	REG(rcvfc);		/* receive frame control */
	REG(rcvfs);		/* receive frame status (4 bytes) */
	REG(fifofc);		/* FIFO frame count */
	REG(ir);		/* interrupt register */
	REG(imr);		/* interrupt mask register */
	REG(pr);		/* poll register */
	REG(biucc);		/* bus interface unit config control */
	REG(fifocc);		/* FIFO configuration control */
	REG(maccc);		/* medium access control config control */
	REG(plscc);		/* phys layer signalling config control */
	REG(phycc);		/* physical configuration control */
	REG(chipid_lo);		/* chip ID, lsb */
	REG(chipid_hi);		/* chip ID, msb */
	REG(iac);		/* internal address config */
	REG(reg19);
	REG(ladrf);		/* logical address filter (8 bytes) */
	REG(padr);		/* physical address (6 bytes) */
	REG(reg22);
	REG(reg23);
	REG(mpc);		/* missed packet count (clears when read) */
	REG(reg25);
	REG(rntpc);		/* runt packet count (clears when read) */
	REG(rcvcc);		/* recv collision count (clears when read) */
	REG(reg28);
	REG(utr);		/* user test reg */
	REG(reg30);
	REG(reg31);
};

/* Bits in XMTFC */
#define DRTRY		0x80	/* don't retry transmission after collision */
#define DXMTFCS		0x08	/* don't append FCS to transmitted frame */
#define AUTO_PAD_XMIT	0x01	/* auto-pad short packets on transmission */

/* Bits in XMTFS: only valid when XMTSV is set in PR and XMTFS */
#define XMTSV		0x80	/* transmit status (i.e. XMTFS) valid */
#define UFLO		0x40	/* underflow - xmit fifo ran dry */
#define LCOL		0x20	/* late collision (transmission aborted) */
#define MORE		0x10	/* 2 or more retries needed to xmit frame */
#define ONE		0x08	/* 1 retry needed to xmit frame */
#define DEFER		0x04	/* MACE had to defer xmission (enet busy) */
#define LCAR		0x02	/* loss of carrier (transmission aborted) */
#define RTRY		0x01	/* too many retries (transmission aborted) */

/* Bits in XMTRC: only valid when XMTSV is set in PR (and XMTFS) */
#define EXDEF		0x80	/* had to defer for excessive time */
#define RETRY_MASK	0x0f	/* number of retries (0 - 15) */

/* Bits in RCVFC */
#define LLRCV		0x08	/* low latency receive: early DMA request */
#define M_RBAR		0x04	/* sets function of EAM/R pin */
#define AUTO_STRIP_RCV	0x01	/* auto-strip short LLC frames on recv */

/*
 * Bits in RCVFS.  After a frame is received, four bytes of status
 * are automatically read from this register and appended to the frame
 * data in memory.  These are:
 * Byte 0 and 1: message byte count and frame status
 * Byte 2: runt packet count
 * Byte 3: receive collision count
 */
#define RS_OFLO		0x8000	/* receive FIFO overflowed */
#define RS_CLSN		0x4000	/* received frame suffered (late) collision */
#define RS_FRAMERR	0x2000	/* framing error flag */
#define RS_FCSERR	0x1000	/* frame had FCS error */
#define RS_COUNT	0x0fff	/* mask for byte count field */

/* Bits (fields) in FIFOFC */
#define RCVFC_SH	4	/* receive frame count in FIFO */
#define RCVFC_MASK	0x0f
#define XMTFC_SH	0	/* transmit frame count in FIFO */
#define XMTFC_MASK	0x0f

/*
 * Bits in IR and IMR.  The IR clears itself when read.
 * Setting a bit in the IMR will disable the corresponding interrupt.
 */
#define JABBER		0x80	/* jabber error - 10baseT xmission too long */
#define BABBLE		0x40	/* babble - xmitter xmitting for too long */
#define CERR		0x20	/* collision err - no SQE test (heartbeat) */
#define RCVCCO		0x10	/* RCVCC overflow */
#define RNTPCO		0x08	/* RNTPC overflow */
#define MPCO		0x04	/* MPC overflow */
#define RCVINT		0x02	/* receive interrupt */
#define XMTINT		0x01	/* transmitter interrupt */

/* Bits in PR */
#define XMTSV		0x80	/* XMTFS valid (same as in XMTFS) */
#define TDTREQ		0x40	/* set when xmit fifo is requesting data */
#define RDTREQ		0x20	/* set when recv fifo requests data xfer */

/* Bits in BIUCC */
#define BSWP		0x40	/* byte swap, i.e. big-endian bus */
#define XMTSP_4		0x00	/* start xmitting when 4 bytes in FIFO */
#define XMTSP_16	0x10	/* start xmitting when 16 bytes in FIFO */
#define XMTSP_64	0x20	/* start xmitting when 64 bytes in FIFO */
#define XMTSP_112	0x30	/* start xmitting when 112 bytes in FIFO */
#define SWRST		0x01	/* software reset */

/* Bits in FIFOCC */
#define XMTFW_8		0x00	/* xmit fifo watermark = 8 words free */
#define XMTFW_16	0x40	/*  16 words free */
#define XMTFW_32	0x80	/*  32 words free */
#define RCVFW_16	0x00	/* recv fifo watermark = 16 bytes avail */
#define RCVFW_32	0x10	/*  32 bytes avail */
#define RCVFW_64	0x20	/*  64 bytes avail */
#define XMTFWU		0x08	/* xmit fifo watermark update enable */
#define RCVFWU		0x04	/* recv fifo watermark update enable */
#define XMTBRST		0x02	/* enable transmit burst mode */
#define RCVBRST		0x01	/* enable receive burst mode */

/* Bits in MACCC */
#define PROM		0x80	/* promiscuous mode */
#define DXMT2PD		0x40	/* disable xmit two-part deferral algorithm */
#define EMBA		0x20	/* enable modified backoff algorithm */
#define DRCVPA		0x08	/* disable receiving physical address */
#define DRCVBC		0x04	/* disable receiving broadcasts */
#define ENXMT		0x02	/* enable transmitter */
#define ENRCV		0x01	/* enable receiver */

/* Bits in PLSCC */
#define XMTSEL		0x08	/* select DO+/DO- state when idle */
#define PORTSEL_AUI	0x00	/* select AUI port */
#define PORTSEL_10T	0x02	/* select 10Base-T port */
#define PORTSEL_DAI	0x04	/* select DAI port */
#define PORTSEL_GPSI	0x06	/* select GPSI port */
#define ENPLSIO		0x01	/* enable optional PLS I/O pins */

/* Bits in PHYCC */
#define LNKFL		0x80	/* reports 10Base-T link failure */
#define DLNKTST		0x40	/* disable 10Base-T link test */
#define REVPOL		0x20	/* 10Base-T receiver polarity reversed */
#define DAPC		0x10	/* disable auto receiver polarity correction */
#define LRT		0x08	/* low receive threshold for long links */
#define ASEL		0x04	/* auto-select AUI or 10Base-T port */
#define RWAKE		0x02	/* remote wake function */
#define AWAKE		0x01	/* auto wake function */

/* Bits in IAC */
#define ADDRCHG		0x80	/* request address change */
#define PHYADDR		0x04	/* access physical address */
#define LOGADDR		0x02	/* access multicast filter */

/* Bits in UTR */
#define RTRE		0x80	/* reserved test register enable. DON'T SET. */
#define RTRD		0x40	/* reserved test register disable.  Sticky */
#define RPAC		0x20	/* accept runt packets */
#define FCOLL		0x10	/* force collision */
#define RCVFCSE		0x08	/* receive FCS enable */
#define LOOP_NONE	0x00	/* no loopback */
#define LOOP_EXT	0x02	/* external loopback */
#define LOOP_INT	0x04	/* internal loopback, excludes MENDEC */
#define LOOP_MENDEC	0x06	/* internal loopback, includes MENDEC */
