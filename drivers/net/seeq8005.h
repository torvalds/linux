/*
 * defines, etc for the seeq8005
 */

/*
 * This file is distributed under GPL.
 *
 * This style and layout of this file is also copied
 * from many of the other linux network device drivers.
 */

/* The number of low I/O ports used by the ethercard. */
#define SEEQ8005_IO_EXTENT	16

#define SEEQ_B		(ioaddr)

#define	SEEQ_CMD	(SEEQ_B)		/* Write only */
#define	SEEQ_STATUS	(SEEQ_B)		/* Read only */
#define SEEQ_CFG1	(SEEQ_B + 2)
#define SEEQ_CFG2	(SEEQ_B + 4)
#define	SEEQ_REA	(SEEQ_B + 6)		/* Receive End Area Register */
#define SEEQ_RPR	(SEEQ_B + 10)		/* Receive Pointer Register */
#define	SEEQ_TPR	(SEEQ_B + 12)		/* Transmit Pointer Register */
#define	SEEQ_DMAAR	(SEEQ_B + 14)		/* DMA Address Register */
#define SEEQ_BUFFER	(SEEQ_B + 8)		/* Buffer Window Register */

#define	DEFAULT_TEA	(0x3f)

#define SEEQCMD_DMA_INT_EN	(0x0001)	/* DMA Interrupt Enable */
#define SEEQCMD_RX_INT_EN	(0x0002)	/* Receive Interrupt Enable */
#define SEEQCMD_TX_INT_EN	(0x0004)	/* Transmit Interrupt Enable */
#define SEEQCMD_WINDOW_INT_EN	(0x0008)	/* What the hell is this for?? */
#define SEEQCMD_INT_MASK	(0x000f)

#define SEEQCMD_DMA_INT_ACK	(0x0010)	/* DMA ack */
#define SEEQCMD_RX_INT_ACK	(0x0020)
#define SEEQCMD_TX_INT_ACK	(0x0040)
#define	SEEQCMD_WINDOW_INT_ACK	(0x0080)
#define SEEQCMD_ACK_ALL		(0x00f0)

#define SEEQCMD_SET_DMA_ON	(0x0100)	/* Enables DMA Request logic */
#define SEEQCMD_SET_RX_ON	(0x0200)	/* Enables Packet RX */
#define SEEQCMD_SET_TX_ON	(0x0400)	/* Starts TX run */
#define SEEQCMD_SET_DMA_OFF	(0x0800)
#define SEEQCMD_SET_RX_OFF	(0x1000)
#define SEEQCMD_SET_TX_OFF	(0x2000)
#define SEEQCMD_SET_ALL_OFF	(0x3800)	/* set all logic off */

#define SEEQCMD_FIFO_READ	(0x4000)	/* Set FIFO to read mode (read from Buffer) */
#define SEEQCMD_FIFO_WRITE	(0x8000)	/* Set FIFO to write mode */

#define SEEQSTAT_DMA_INT_EN	(0x0001)	/* Status of interrupt enable */
#define SEEQSTAT_RX_INT_EN	(0x0002)
#define SEEQSTAT_TX_INT_EN	(0x0004)
#define SEEQSTAT_WINDOW_INT_EN	(0x0008)

#define	SEEQSTAT_DMA_INT	(0x0010)	/* Interrupt flagged */
#define SEEQSTAT_RX_INT		(0x0020)
#define SEEQSTAT_TX_INT		(0x0040)
#define	SEEQSTAT_WINDOW_INT	(0x0080)
#define SEEQSTAT_ANY_INT	(0x00f0)

#define SEEQSTAT_DMA_ON		(0x0100)	/* DMA logic on */
#define SEEQSTAT_RX_ON		(0x0200)	/* Packet RX on */
#define SEEQSTAT_TX_ON		(0x0400)	/* TX running */

#define SEEQSTAT_FIFO_FULL	(0x2000)
#define SEEQSTAT_FIFO_EMPTY	(0x4000)
#define SEEQSTAT_FIFO_DIR	(0x8000)	/* 1=read, 0=write */

#define SEEQCFG1_BUFFER_MASK	(0x000f)	/* define what maps into the BUFFER register */
#define SEEQCFG1_BUFFER_MAC0	(0x0000)	/* MAC station addresses 0-5 */
#define SEEQCFG1_BUFFER_MAC1	(0x0001)
#define SEEQCFG1_BUFFER_MAC2	(0x0002)
#define SEEQCFG1_BUFFER_MAC3	(0x0003)
#define SEEQCFG1_BUFFER_MAC4	(0x0004)
#define SEEQCFG1_BUFFER_MAC5	(0x0005)
#define SEEQCFG1_BUFFER_PROM	(0x0006)	/* The Address/CFG PROM */
#define SEEQCFG1_BUFFER_TEA	(0x0007)	/* Transmit end area */
#define SEEQCFG1_BUFFER_BUFFER	(0x0008)	/* Packet buffer memory */
#define SEEQCFG1_BUFFER_INT_VEC	(0x0009)	/* Interrupt Vector */

#define SEEQCFG1_DMA_INTVL_MASK	(0x0030)
#define SEEQCFG1_DMA_CONT	(0x0000)
#define SEEQCFG1_DMA_800ns	(0x0010)
#define SEEQCFG1_DMA_1600ns	(0x0020)
#define SEEQCFG1_DMA_3200ns	(0x0030)

#define SEEQCFG1_DMA_LEN_MASK	(0x00c0)
#define SEEQCFG1_DMA_LEN1	(0x0000)
#define SEEQCFG1_DMA_LEN2	(0x0040)
#define SEEQCFG1_DMA_LEN4	(0x0080)
#define SEEQCFG1_DMA_LEN8	(0x00c0)

#define SEEQCFG1_MAC_MASK	(0x3f00)	/* Dis/enable bits for MAC addresses */
#define SEEQCFG1_MAC0_EN	(0x0100)
#define SEEQCFG1_MAC1_EN	(0x0200)
#define SEEQCFG1_MAC2_EN	(0x0400)
#define SEEQCFG1_MAC3_EN	(0x0800)
#define	SEEQCFG1_MAC4_EN	(0x1000)
#define SEEQCFG1_MAC5_EN	(0x2000)

#define	SEEQCFG1_MATCH_MASK	(0xc000)	/* Packet matching logic cfg bits */
#define SEEQCFG1_MATCH_SPECIFIC	(0x0000)	/* only matching MAC addresses */
#define SEEQCFG1_MATCH_BROAD	(0x4000)	/* matching and broadcast addresses */
#define SEEQCFG1_MATCH_MULTI	(0x8000)	/* matching, broadcast and multicast */
#define SEEQCFG1_MATCH_ALL	(0xc000)	/* Promiscuous mode */

#define SEEQCFG1_DEFAULT	(SEEQCFG1_BUFFER_BUFFER | SEEQCFG1_MAC0_EN | SEEQCFG1_MATCH_BROAD)

#define SEEQCFG2_BYTE_SWAP	(0x0001)	/* 0=Intel byte-order */
#define SEEQCFG2_AUTO_REA	(0x0002)	/* if set, Receive End Area will be updated when reading from Buffer */

#define SEEQCFG2_CRC_ERR_EN	(0x0008)	/* enables receiving of packets with CRC errors */
#define SEEQCFG2_DRIBBLE_EN	(0x0010)	/* enables receiving of non-aligned packets */
#define SEEQCFG2_SHORT_EN	(0x0020)	/* enables receiving of short packets */

#define	SEEQCFG2_SLOTSEL	(0x0040)	/* 0= standard IEEE802.3, 1= smaller,faster, non-standard */
#define SEEQCFG2_NO_PREAM	(0x0080)	/* 1= user supplies Xmit preamble bytes */
#define SEEQCFG2_ADDR_LEN	(0x0100)	/* 1= 2byte addresses */
#define SEEQCFG2_REC_CRC	(0x0200)	/* 0= received packets will have CRC stripped from them */
#define SEEQCFG2_XMIT_NO_CRC	(0x0400)	/* don't xmit CRC with each packet (user supplies it) */
#define SEEQCFG2_LOOPBACK	(0x0800)
#define SEEQCFG2_CTRLO		(0x1000)
#define SEEQCFG2_RESET		(0x8000)	/* software Hard-reset bit */

struct seeq_pkt_hdr {
	unsigned short	next;			/* address of next packet header */
	unsigned char	babble_int:1,		/* enable int on >1514 byte packet */
			coll_int:1,		/* enable int on collision */
			coll_16_int:1,		/* enable int on >15 collision */
			xmit_int:1,		/* enable int on success (or xmit with <15 collision) */
			unused:1,
			data_follows:1,		/* if not set, process this as a header and pointer only */
			chain_cont:1,		/* if set, more headers in chain 		only cmd bit valid in recv header */
			xmit_recv:1;		/* if set, a xmit packet, else a receive packet.*/
	unsigned char	status;
};

#define SEEQPKTH_BAB_INT_EN	(0x01)		/* xmit only */
#define SEEQPKTH_COL_INT_EN	(0x02)		/* xmit only */
#define SEEQPKTH_COL16_INT_EN	(0x04)		/* xmit only */
#define SEEQPKTH_XMIT_INT_EN	(0x08)		/* xmit only */
#define SEEQPKTH_DATA_FOLLOWS	(0x20)		/* supposedly in xmit only */
#define SEEQPKTH_CHAIN		(0x40)		/* more headers follow */
#define SEEQPKTH_XMIT		(0x80)

#define SEEQPKTS_BABBLE		(0x0100)	/* xmit only */
#define SEEQPKTS_OVERSIZE	(0x0100)	/* recv only */
#define SEEQPKTS_COLLISION	(0x0200)	/* xmit only */
#define SEEQPKTS_CRC_ERR	(0x0200)	/* recv only */
#define SEEQPKTS_COLL16		(0x0400)	/* xmit only */
#define SEEQPKTS_DRIB		(0x0400)	/* recv only */
#define SEEQPKTS_SHORT		(0x0800)	/* recv only */
#define SEEQPKTS_DONE		(0x8000)
#define SEEQPKTS_ANY_ERROR	(0x0f00)
