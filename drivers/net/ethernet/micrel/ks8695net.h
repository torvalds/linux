/*
 * Micrel KS8695 (Centaur) Ethernet.
 *
 * Copyright 2008 Simtec Electronics
 *		  Daniel Silverstone <dsilvers@simtec.co.uk>
 *		  Vincent Sanders <vince@simtec.co.uk>
 */

#ifndef KS8695NET_H
#define KS8695NET_H

/* Receive descriptor flags */
#define RDES_OWN	(1 << 31)	/* Ownership */
#define RDES_FS		(1 << 30)	/* First Descriptor */
#define RDES_LS		(1 << 29)	/* Last Descriptor */
#define RDES_IPE	(1 << 28)	/* IP Checksum error */
#define RDES_TCPE	(1 << 27)	/* TCP Checksum error */
#define RDES_UDPE	(1 << 26)	/* UDP Checksum error */
#define RDES_ES		(1 << 25)	/* Error summary */
#define RDES_MF		(1 << 24)	/* Multicast Frame */
#define RDES_RE		(1 << 19)	/* MII Error reported */
#define RDES_TL		(1 << 18)	/* Frame too Long */
#define RDES_RF		(1 << 17)	/* Runt Frame */
#define RDES_CE		(1 << 16)	/* CRC error */
#define RDES_FT		(1 << 15)	/* Frame Type */
#define RDES_FLEN	(0x7ff)		/* Frame Length */

#define RDES_RER	(1 << 25)	/* Receive End of Ring */
#define RDES_RBS	(0x7ff)		/* Receive Buffer Size */

/* Transmit descriptor flags */

#define TDES_OWN	(1 << 31)	/* Ownership */

#define TDES_IC		(1 << 31)	/* Interrupt on Completion */
#define TDES_FS		(1 << 30)	/* First Segment */
#define TDES_LS		(1 << 29)	/* Last Segment */
#define TDES_IPCKG	(1 << 28)	/* IP Checksum generate */
#define TDES_TCPCKG	(1 << 27)	/* TCP Checksum generate */
#define TDES_UDPCKG	(1 << 26)	/* UDP Checksum generate */
#define TDES_TER	(1 << 25)	/* Transmit End of Ring */
#define TDES_TBS	(0x7ff)		/* Transmit Buffer Size */

/*
 * Network controller register offsets
 */
#define KS8695_DTXC		(0x00)		/* DMA Transmit Control */
#define KS8695_DRXC		(0x04)		/* DMA Receive Control */
#define KS8695_DTSC		(0x08)		/* DMA Transmit Start Command */
#define KS8695_DRSC		(0x0c)		/* DMA Receive Start Command */
#define KS8695_TDLB		(0x10)		/* Transmit Descriptor List
						 * Base Address
						 */
#define KS8695_RDLB		(0x14)		/* Receive Descriptor List
						 * Base Address
						 */
#define KS8695_MAL		(0x18)		/* MAC Station Address Low */
#define KS8695_MAH		(0x1c)		/* MAC Station Address High */
#define KS8695_AAL_(n)		(0x80 + ((n)*8))	/* MAC Additional
							 * Station Address
							 * (0..15) Low
							 */
#define KS8695_AAH_(n)		(0x84 + ((n)*8))	/* MAC Additional
							 * Station Address
							 * (0..15) High
							 */


/* DMA Transmit Control Register */
#define DTXC_TRST		(1    << 31)	/* Soft Reset */
#define DTXC_TBS		(0x3f << 24)	/* Transmit Burst Size */
#define DTXC_TUCG		(1    << 18)	/* Transmit UDP
						 * Checksum Generate
						 */
#define DTXC_TTCG		(1    << 17)	/* Transmit TCP
						 * Checksum Generate
						 */
#define DTXC_TICG		(1    << 16)	/* Transmit IP
						 * Checksum Generate
						 */
#define DTXC_TFCE		(1    <<  9)	/* Transmit Flow
						 * Control Enable
						 */
#define DTXC_TLB		(1    <<  8)	/* Loopback mode */
#define DTXC_TEP		(1    <<  2)	/* Transmit Enable Padding */
#define DTXC_TAC		(1    <<  1)	/* Transmit Add CRC */
#define DTXC_TE			(1    <<  0)	/* TX Enable */

/* DMA Receive Control Register */
#define DRXC_RBS		(0x3f << 24)	/* Receive Burst Size */
#define DRXC_RUCC		(1    << 18)	/* Receive UDP Checksum check */
#define DRXC_RTCG		(1    << 17)	/* Receive TCP Checksum check */
#define DRXC_RICG		(1    << 16)	/* Receive IP Checksum check */
#define DRXC_RFCE		(1    <<  9)	/* Receive Flow Control
						 * Enable
						 */
#define DRXC_RB			(1    <<  6)	/* Receive Broadcast */
#define DRXC_RM			(1    <<  5)	/* Receive Multicast */
#define DRXC_RU			(1    <<  4)	/* Receive Unicast */
#define DRXC_RERR		(1    <<  3)	/* Receive Error Frame */
#define DRXC_RA			(1    <<  2)	/* Receive All */
#define DRXC_RE			(1    <<  0)	/* RX Enable */

/* Additional Station Address High */
#define AAH_E			(1    << 31)	/* Address Enabled */

#endif /* KS8695NET_H */
