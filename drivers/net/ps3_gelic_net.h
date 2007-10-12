/*
 *  PS3 Platfom gelic network driver.
 *
 * Copyright (C) 2007 Sony Computer Entertainment Inc.
 * Copyright 2006, 2007 Sony Corporation.
 *
 * This file is based on: spider_net.h
 *
 * (C) Copyright IBM Corp. 2005
 *
 * Authors : Utz Bacher <utz.bacher@de.ibm.com>
 *           Jens Osterkamp <Jens.Osterkamp@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef _GELIC_NET_H
#define _GELIC_NET_H

/* descriptors */
#define GELIC_NET_RX_DESCRIPTORS        128 /* num of descriptors */
#define GELIC_NET_TX_DESCRIPTORS        128 /* num of descriptors */

#define GELIC_NET_MAX_MTU               VLAN_ETH_FRAME_LEN
#define GELIC_NET_MIN_MTU               VLAN_ETH_ZLEN
#define GELIC_NET_RXBUF_ALIGN           128
#define GELIC_NET_RX_CSUM_DEFAULT       1 /* hw chksum */
#define GELIC_NET_WATCHDOG_TIMEOUT      5*HZ
#define GELIC_NET_NAPI_WEIGHT           (GELIC_NET_RX_DESCRIPTORS)
#define GELIC_NET_BROADCAST_ADDR        0xffffffffffffL
#define GELIC_NET_VLAN_POS              (VLAN_ETH_ALEN * 2)
#define GELIC_NET_VLAN_MAX              4
#define GELIC_NET_MC_COUNT_MAX          32 /* multicast address list */

enum gelic_net_int0_status {
	GELIC_NET_GDTDCEINT  = 24,
	GELIC_NET_GRFANMINT  = 28,
};

/* GHIINT1STS bits */
enum gelic_net_int1_status {
	GELIC_NET_GDADCEINT = 14,
};

/* interrupt mask */
#define GELIC_NET_TXINT                   (1L << (GELIC_NET_GDTDCEINT + 32))

#define GELIC_NET_RXINT0                  (1L << (GELIC_NET_GRFANMINT + 32))
#define GELIC_NET_RXINT1                  (1L << GELIC_NET_GDADCEINT)
#define GELIC_NET_RXINT                   (GELIC_NET_RXINT0 | GELIC_NET_RXINT1)

 /* RX descriptor data_status bits */
#define GELIC_NET_RXDMADU	0x80000000 /* destination MAC addr unknown */
#define GELIC_NET_RXLSTFBF	0x40000000 /* last frame buffer            */
#define GELIC_NET_RXIPCHK	0x20000000 /* IP checksum performed        */
#define GELIC_NET_RXTCPCHK	0x10000000 /* TCP/UDP checksup performed   */
#define GELIC_NET_RXIPSPKT	0x08000000 /* IPsec packet   */
#define GELIC_NET_RXIPSAHPRT	0x04000000 /* IPsec AH protocol performed */
#define GELIC_NET_RXIPSESPPRT	0x02000000 /* IPsec ESP protocol performed */
#define GELIC_NET_RXSESPAH	0x01000000 /*
					    * IPsec ESP protocol auth
					    * performed
					    */

#define GELIC_NET_RXWTPKT	0x00C00000 /*
					    * wakeup trigger packet
					    * 01: Magic Packet (TM)
					    * 10: ARP packet
					    * 11: Multicast MAC addr
					    */
#define GELIC_NET_RXVLNPKT	0x00200000 /* VLAN packet */
/* bit 20..16 reserved */
#define GELIC_NET_RXRRECNUM	0x0000ff00 /* reception receipt number */
#define GELIC_NET_RXRRECNUM_SHIFT	8
/* bit 7..0 reserved */

#define GELIC_NET_TXDESC_TAIL		0
#define GELIC_NET_DATA_STATUS_CHK_MASK	(GELIC_NET_RXIPCHK | GELIC_NET_RXTCPCHK)

/* RX descriptor data_error bits */
/* bit 31 reserved */
#define GELIC_NET_RXALNERR	0x40000000 /* alignement error 10/100M */
#define GELIC_NET_RXOVERERR	0x20000000 /* oversize error */
#define GELIC_NET_RXRNTERR	0x10000000 /* Runt error */
#define GELIC_NET_RXIPCHKERR	0x08000000 /* IP checksum  error */
#define GELIC_NET_RXTCPCHKERR	0x04000000 /* TCP/UDP checksum  error */
#define GELIC_NET_RXUMCHSP	0x02000000 /* unmatched sp on sp */
#define GELIC_NET_RXUMCHSPI	0x01000000 /* unmatched SPI on SAD */
#define GELIC_NET_RXUMCHSAD	0x00800000 /* unmatched SAD */
#define GELIC_NET_RXIPSAHERR	0x00400000 /* auth error on AH protocol
					    * processing */
#define GELIC_NET_RXIPSESPAHERR	0x00200000 /* auth error on ESP protocol
					    * processing */
#define GELIC_NET_RXDRPPKT	0x00100000 /* drop packet */
#define GELIC_NET_RXIPFMTERR	0x00080000 /* IP packet format error */
/* bit 18 reserved */
#define GELIC_NET_RXDATAERR	0x00020000 /* IP packet format error */
#define GELIC_NET_RXCALERR	0x00010000 /* cariier extension length
					    * error */
#define GELIC_NET_RXCREXERR	0x00008000 /* carrier extention error */
#define GELIC_NET_RXMLTCST	0x00004000 /* multicast address frame */
/* bit 13..0 reserved */
#define GELIC_NET_DATA_ERROR_CHK_MASK		\
	(GELIC_NET_RXIPCHKERR | GELIC_NET_RXTCPCHKERR)


/* tx descriptor command and status */
#define GELIC_NET_DMAC_CMDSTAT_NOCS       0xa0080000 /* middle of frame */
#define GELIC_NET_DMAC_CMDSTAT_TCPCS      0xa00a0000
#define GELIC_NET_DMAC_CMDSTAT_UDPCS      0xa00b0000
#define GELIC_NET_DMAC_CMDSTAT_END_FRAME  0x00040000 /* end of frame */

#define GELIC_NET_DMAC_CMDSTAT_RXDCEIS	  0x00000002 /* descriptor chain end
						      * interrupt status */

#define GELIC_NET_DMAC_CMDSTAT_CHAIN_END  0x00000002 /* RXDCEIS:DMA stopped */
#define GELIC_NET_DESCR_IND_PROC_SHIFT    28
#define GELIC_NET_DESCR_IND_PROC_MASKO    0x0fffffff


enum gelic_net_descr_status {
	GELIC_NET_DESCR_COMPLETE            = 0x00, /* used in tx */
	GELIC_NET_DESCR_BUFFER_FULL         = 0x00, /* used in rx */
	GELIC_NET_DESCR_RESPONSE_ERROR      = 0x01, /* used in rx and tx */
	GELIC_NET_DESCR_PROTECTION_ERROR    = 0x02, /* used in rx and tx */
	GELIC_NET_DESCR_FRAME_END           = 0x04, /* used in rx */
	GELIC_NET_DESCR_FORCE_END           = 0x05, /* used in rx and tx */
	GELIC_NET_DESCR_CARDOWNED           = 0x0a, /* used in rx and tx */
	GELIC_NET_DESCR_NOT_IN_USE          = 0x0b  /* any other value */
};
/* for lv1_net_control */
#define GELIC_NET_GET_MAC_ADDRESS               0x0000000000000001
#define GELIC_NET_GET_ETH_PORT_STATUS           0x0000000000000002
#define GELIC_NET_SET_NEGOTIATION_MODE          0x0000000000000003
#define GELIC_NET_GET_VLAN_ID                   0x0000000000000004

#define GELIC_NET_LINK_UP                       0x0000000000000001
#define GELIC_NET_FULL_DUPLEX                   0x0000000000000002
#define GELIC_NET_AUTO_NEG                      0x0000000000000004
#define GELIC_NET_SPEED_10                      0x0000000000000010
#define GELIC_NET_SPEED_100                     0x0000000000000020
#define GELIC_NET_SPEED_1000                    0x0000000000000040

#define GELIC_NET_VLAN_ALL                      0x0000000000000001
#define GELIC_NET_VLAN_WIRED                    0x0000000000000002
#define GELIC_NET_VLAN_WIRELESS                 0x0000000000000003
#define GELIC_NET_VLAN_PSP                      0x0000000000000004
#define GELIC_NET_VLAN_PORT0                    0x0000000000000010
#define GELIC_NET_VLAN_PORT1                    0x0000000000000011
#define GELIC_NET_VLAN_PORT2                    0x0000000000000012
#define GELIC_NET_VLAN_DAEMON_CLIENT_BSS        0x0000000000000013
#define GELIC_NET_VLAN_LIBERO_CLIENT_BSS        0x0000000000000014
#define GELIC_NET_VLAN_NO_ENTRY                 -6

#define GELIC_NET_PORT                          2 /* for port status */

/* size of hardware part of gelic descriptor */
#define GELIC_NET_DESCR_SIZE	(32)
struct gelic_net_descr {
	/* as defined by the hardware */
	u32 buf_addr;
	u32 buf_size;
	u32 next_descr_addr;
	u32 dmac_cmd_status;
	u32 result_size;
	u32 valid_size;	/* all zeroes for tx */
	u32 data_status;
	u32 data_error;	/* all zeroes for tx */

	/* used in the driver */
	struct sk_buff *skb;
	dma_addr_t bus_addr;
	struct gelic_net_descr *next;
	struct gelic_net_descr *prev;
	struct vlan_ethhdr vlan;
} __attribute__((aligned(32)));

struct gelic_net_descr_chain {
	/* we walk from tail to head */
	struct gelic_net_descr *head;
	struct gelic_net_descr *tail;
};

struct gelic_net_card {
	struct net_device *netdev;
	struct napi_struct napi;
	/*
	 * hypervisor requires irq_status should be
	 * 8 bytes aligned, but u64 member is
	 * always disposed in that manner
	 */
	u64 irq_status;
	u64 ghiintmask;

	struct ps3_system_bus_device *dev;
	u32 vlan_id[GELIC_NET_VLAN_MAX];
	int vlan_index;

	struct gelic_net_descr_chain tx_chain;
	struct gelic_net_descr_chain rx_chain;
	int rx_dma_restart_required;
	/* gurad dmac descriptor chain*/
	spinlock_t chain_lock;

	int rx_csum;
	/* guard tx_dma_progress */
	spinlock_t tx_dma_lock;
	int tx_dma_progress;

	struct work_struct tx_timeout_task;
	atomic_t tx_timeout_task_counter;
	wait_queue_head_t waitq;

	struct gelic_net_descr *tx_top, *rx_top;
	struct gelic_net_descr descr[0];
};


extern unsigned long p_to_lp(long pa);

#endif /* _GELIC_NET_H */
