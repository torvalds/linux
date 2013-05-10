/*
 * Racal-Interlan ni5010 Ethernet definitions
 *
 * This is an extension to the Linux operating system, and is covered by the
 * same GNU General Public License that covers that work.
 *
 * copyrights (c) 1996 by Jan-Pascal van Best (jvbest@wi.leidenuniv.nl)
 *
 * I have done a look in the following sources:
 *   crynwr-packet-driver by Russ Nelson
 */

#define NI5010_BUFSIZE	2048	/* number of bytes in a buffer */

#define NI5010_MAGICVAL0 0x00  /* magic-values for ni5010 card */
#define NI5010_MAGICVAL1 0x55
#define NI5010_MAGICVAL2 0xAA

#define SA_ADDR0 0x02
#define SA_ADDR1 0x07
#define SA_ADDR2 0x01

/* The number of low I/O ports used by the ni5010 ethercard. */
#define NI5010_IO_EXTENT       32

#define PRINTK(x) if (NI5010_DEBUG) printk x
#define PRINTK2(x) if (NI5010_DEBUG>=2) printk x
#define PRINTK3(x) if (NI5010_DEBUG>=3) printk x

/* The various IE command registers */
#define EDLC_XSTAT	(ioaddr + 0x00)	/* EDLC transmit csr */
#define EDLC_XCLR	(ioaddr + 0x00)	/* EDLC transmit "Clear IRQ" */
#define EDLC_XMASK	(ioaddr + 0x01)	/* EDLC transmit "IRQ Masks" */
#define EDLC_RSTAT	(ioaddr + 0x02)	/* EDLC receive csr */
#define EDLC_RCLR	(ioaddr + 0x02)	/* EDLC receive "Clear IRQ" */
#define EDLC_RMASK	(ioaddr + 0x03)	/* EDLC receive "IRQ Masks" */
#define EDLC_XMODE	(ioaddr + 0x04)	/* EDLC transmit Mode */
#define EDLC_RMODE	(ioaddr + 0x05)	/* EDLC receive Mode */
#define EDLC_RESET	(ioaddr + 0x06)	/* EDLC RESET register */
#define EDLC_TDR1	(ioaddr + 0x07)	/* "Time Domain Reflectometry" reg1 */
#define EDLC_ADDR	(ioaddr + 0x08)	/* EDLC station address, 6 bytes */
	 			/* 0x0E doesn't exist for r/w */
#define EDLC_TDR2	(ioaddr + 0x0f)	/* "Time Domain Reflectometry" reg2 */
#define IE_GP		(ioaddr + 0x10)	/* GP pointer (word register) */
				/* 0x11 is 2nd byte of GP Pointer */
#define IE_RCNT		(ioaddr + 0x10)	/* Count of bytes in rcv'd packet */
 				/* 0x11 is 2nd byte of "Byte Count" */
#define IE_MMODE	(ioaddr + 0x12)	/* Memory Mode register */
#define IE_DMA_RST	(ioaddr + 0x13)	/* IE DMA Reset.  write only */
#define IE_ISTAT	(ioaddr + 0x13)	/* IE Interrupt Status.  read only */
#define IE_RBUF		(ioaddr + 0x14)	/* IE Receive Buffer port */
#define IE_XBUF		(ioaddr + 0x15)	/* IE Transmit Buffer port */
#define IE_SAPROM	(ioaddr + 0x16)	/* window on station addr prom */
#define IE_RESET	(ioaddr + 0x17)	/* any write causes Board Reset */

/* bits in EDLC_XSTAT, interrupt clear on write, status when read */
#define XS_TPOK		0x80	/* transmit packet successful */
#define XS_CS		0x40	/* carrier sense */
#define XS_RCVD		0x20	/* transmitted packet received */
#define XS_SHORT	0x10	/* transmission media is shorted */
#define XS_UFLW		0x08	/* underflow.  iff failed board */
#define XS_COLL		0x04	/* collision occurred */
#define XS_16COLL	0x02	/* 16th collision occurred */
#define XS_PERR		0x01	/* parity error */

#define XS_CLR_UFLW	0x08	/* clear underflow */
#define XS_CLR_COLL	0x04	/* clear collision */
#define XS_CLR_16COLL	0x02	/* clear 16th collision */
#define XS_CLR_PERR	0x01	/* clear parity error */

/* bits in EDLC_XMASK, mask/enable transmit interrupts.  register is r/w */
#define XM_TPOK		0x80	/* =1 to enable Xmt Pkt OK interrupts */
#define XM_RCVD		0x20	/* =1 to enable Xmt Pkt Rcvd ints */
#define XM_UFLW		0x08	/* =1 to enable Xmt Underflow ints */
#define XM_COLL		0x04	/* =1 to enable Xmt Collision ints */
#define XM_COLL16	0x02	/* =1 to enable Xmt 16th Coll ints */
#define XM_PERR		0x01	/* =1 to enable Xmt Parity Error ints */
 				/* note: always clear this bit */
#define XM_ALL		(XM_TPOK | XM_RCVD | XM_UFLW | XM_COLL | XM_COLL16)

/* bits in EDLC_RSTAT, interrupt clear on write, status when read */
#define RS_PKT_OK	0x80	/* received good packet */
#define RS_RST_PKT	0x10	/* RESET packet received */
#define RS_RUNT		0x08	/* Runt Pkt rcvd.  Len < 64 Bytes */
#define RS_ALIGN	0x04	/* Alignment error. not 8 bit aligned */
#define RS_CRC_ERR	0x02	/* Bad CRC on rcvd pkt */
#define RS_OFLW		0x01	/* overflow for rcv FIFO */
#define RS_VALID_BITS	( RS_PKT_OK | RS_RST_PKT | RS_RUNT | RS_ALIGN | RS_CRC_ERR | RS_OFLW )
 				/* all valid RSTAT bits */

#define RS_CLR_PKT_OK	0x80	/* clear rcvd packet interrupt */
#define RS_CLR_RST_PKT	0x10	/* clear RESET packet received */
#define RS_CLR_RUNT	0x08	/* clear Runt Pckt received */
#define RS_CLR_ALIGN	0x04	/* clear Alignment error */
#define RS_CLR_CRC_ERR	0x02	/* clear CRC error */
#define RS_CLR_OFLW	0x01	/* clear rcv FIFO Overflow */

/* bits in EDLC_RMASK, mask/enable receive interrupts.  register is r/w */
#define RM_PKT_OK	0x80	/* =1 to enable rcvd good packet ints */
#define RM_RST_PKT	0x10	/* =1 to enable RESET packet ints */
#define RM_RUNT		0x08	/* =1 to enable Runt Pkt rcvd ints */
#define RM_ALIGN	0x04	/* =1 to enable Alignment error ints */
#define RM_CRC_ERR	0x02	/* =1 to enable Bad CRC error ints */
#define RM_OFLW		0x01	/* =1 to enable overflow error ints */

/* bits in EDLC_RMODE, set Receive Packet mode.  register is r/w */
#define RMD_TEST	0x80	/* =1 for Chip testing.  normally 0 */
#define RMD_ADD_SIZ	0x10	/* =1 5-byte addr match.  normally 0 */
#define RMD_EN_RUNT	0x08	/* =1 enable runt rcv.  normally 0 */
#define RMD_EN_RST	0x04	/* =1 to rcv RESET pkt.  normally 0 */

#define RMD_PROMISC	0x03	/* receive *all* packets.  unusual */
#define RMD_MULTICAST	0x02	/* receive multicasts too.  unusual */
#define RMD_BROADCAST	0x01	/* receive broadcasts & normal. usual */
#define RMD_NO_PACKETS	0x00	/* don't receive any packets. unusual */

/* bits in EDLC_XMODE, set Transmit Packet mode.  register is r/w */
#define XMD_COLL_CNT	0xf0	/* coll's since success.  read-only */
#define XMD_IG_PAR	0x08	/* =1 to ignore parity.  ALWAYS set */
#define XMD_T_MODE	0x04	/* =1 to power xcvr. ALWAYS set this */
#define XMD_LBC		0x02	/* =1 for loopbakc.  normally set */
#define XMD_DIS_C	0x01	/* =1 disables contention. normally 0 */

/* bits in EDLC_RESET, write only */
#define RS_RESET	0x80	/* =1 to hold EDLC in reset state */

/* bits in IE_MMODE, write only */
#define MM_EN_DMA	0x80	/* =1 begin DMA xfer, Cplt clrs it */
#define MM_EN_RCV	0x40	/* =1 allows Pkt rcv.  clr'd by rcv */
#define MM_EN_XMT	0x20	/* =1 begin Xmt pkt.  Cplt clrs it */
#define MM_BUS_PAGE	0x18	/* =00 ALWAYS.  Used when MUX=1 */
#define MM_NET_PAGE	0x06	/* =00 ALWAYS.  Used when MUX=0 */
#define MM_MUX		0x01	/* =1 means Rcv Buff on system bus */
				/* =0 means Xmt Buff on system bus */

/* bits in IE_ISTAT, read only */
#define IS_TDIAG	0x80	/* =1 if Diagnostic problem */
#define IS_EN_RCV	0x20	/* =1 until frame is rcv'd cplt */
#define IS_EN_XMT	0x10	/* =1 until frame is xmt'd cplt */
#define IS_EN_DMA	0x08	/* =1 until DMA is cplt or aborted */
#define IS_DMA_INT	0x04	/* =0 iff DMA done interrupt. */
#define IS_R_INT	0x02	/* =0 iff unmasked Rcv interrupt */
#define IS_X_INT	0x01	/* =0 iff unmasked Xmt interrupt */

