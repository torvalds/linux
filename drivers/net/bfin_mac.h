/*
 * File:	drivers/net/bfin_mac.c
 * Based on:
 * Maintainer:
 * 		Bryan Wu <bryan.wu@analog.com>
 *
 * Original author:
 * 		Luke Yang <luke.yang@analog.com>
 *
 * Created:
 * Description:
 *
 * Modified:
 *		Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:	Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software ;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ;  either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY ;  without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program ;  see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
 * PHY REGISTER NAMES
 */
#define PHYREG_MODECTL		0x0000
#define PHYREG_MODESTAT		0x0001
#define PHYREG_PHYID1		0x0002
#define PHYREG_PHYID2		0x0003
#define PHYREG_ANAR		0x0004
#define PHYREG_ANLPAR		0x0005
#define PHYREG_ANER		0x0006
#define PHYREG_NSR		0x0010
#define PHYREG_LBREMR		0x0011
#define PHYREG_REC		0x0012
#define PHYREG_10CFG		0x0013
#define PHYREG_PHY1_1		0x0014
#define PHYREG_PHY1_2		0x0015
#define PHYREG_PHY2		0x0016
#define PHYREG_TW_1		0x0017
#define PHYREG_TW_2		0x0018
#define PHYREG_TEST		0x0019

#define PHY_RESET		0x8000
#define PHY_ANEG_EN		0x1000
#define PHY_DUPLEX		0x0100
#define PHY_SPD_SET		0x2000

#define BFIN_MAC_CSUM_OFFLOAD

struct dma_descriptor {
	struct dma_descriptor *next_dma_desc;
	unsigned long start_addr;
	unsigned short config;
	unsigned short x_count;
};

struct status_area_rx {
#if defined(BFIN_MAC_CSUM_OFFLOAD)
	unsigned short ip_hdr_csum;	/* ip header checksum */
	/* ip payload(udp or tcp or others) checksum */
	unsigned short ip_payload_csum;
#endif
	unsigned long status_word;	/* the frame status word */
};

struct status_area_tx {
	unsigned long status_word;	/* the frame status word */
};

/* use two descriptors for a packet */
struct net_dma_desc_rx {
	struct net_dma_desc_rx *next;
	struct sk_buff *skb;
	struct dma_descriptor desc_a;
	struct dma_descriptor desc_b;
	struct status_area_rx status;
};

/* use two descriptors for a packet */
struct net_dma_desc_tx {
	struct net_dma_desc_tx *next;
	struct sk_buff *skb;
	struct dma_descriptor desc_a;
	struct dma_descriptor desc_b;
	unsigned char packet[1560];
	struct status_area_tx status;
};

struct bf537mac_local {
	/*
	 * these are things that the kernel wants me to keep, so users
	 * can find out semi-useless statistics of how well the card is
	 * performing
	 */
	struct net_device_stats stats;

	int version;

	int FlowEnabled;	/* record if data flow is active */
	int EtherIntIVG;	/* IVG for the ethernet interrupt */
	int RXIVG;		/* IVG for the RX completion */
	int TXIVG;		/* IVG for the TX completion */
	int PhyAddr;		/* PHY address */
	int OpMode;		/* set these bits n the OPMODE regs */
	int Port10;		/* set port speed to 10 Mbit/s */
	int GenChksums;		/* IP checksums to be calculated */
	int NoRcveLnth;		/* dont insert recv length at start of buffer */
	int StripPads;		/* remove trailing pad bytes */
	int FullDuplex;		/* set full duplex mode */
	int Negotiate;		/* enable auto negotiation */
	int Loopback;		/* loopback at the PHY */
	int Cache;		/* Buffers may be cached */
	int FlowControl;	/* flow control active */
	int CLKIN;		/* clock in value in MHZ */
	unsigned short IntMask;	/* interrupt mask */
	unsigned char Mac[6];	/* MAC address of the board */
	spinlock_t lock;
};

extern void get_bf537_ether_addr(char *addr);
