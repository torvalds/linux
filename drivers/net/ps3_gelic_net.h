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

/* virtual interrupt status register bits */
	/* INT1 */
#define GELIC_CARD_TX_RAM_FULL_ERR           0x0000000000000001L
#define GELIC_CARD_RX_RAM_FULL_ERR           0x0000000000000002L
#define GELIC_CARD_TX_SHORT_FRAME_ERR        0x0000000000000004L
#define GELIC_CARD_TX_INVALID_DESCR_ERR      0x0000000000000008L
#define GELIC_CARD_RX_FIFO_FULL_ERR          0x0000000000002000L
#define GELIC_CARD_RX_DESCR_CHAIN_END        0x0000000000004000L
#define GELIC_CARD_RX_INVALID_DESCR_ERR      0x0000000000008000L
#define GELIC_CARD_TX_RESPONCE_ERR           0x0000000000010000L
#define GELIC_CARD_RX_RESPONCE_ERR           0x0000000000100000L
#define GELIC_CARD_TX_PROTECTION_ERR         0x0000000000400000L
#define GELIC_CARD_RX_PROTECTION_ERR         0x0000000004000000L
#define GELIC_CARD_TX_TCP_UDP_CHECKSUM_ERR   0x0000000008000000L
#define GELIC_CARD_PORT_STATUS_CHANGED       0x0000000020000000L
	/* INT 0 */
#define GELIC_CARD_TX_FLAGGED_DESCR          0x0004000000000000L
#define GELIC_CARD_RX_FLAGGED_DESCR          0x0040000000000000L
#define GELIC_CARD_TX_TRANSFER_END           0x0080000000000000L
#define GELIC_CARD_TX_DESCR_CHAIN_END        0x0100000000000000L
#define GELIC_CARD_NUMBER_OF_RX_FRAME        0x1000000000000000L
#define GELIC_CARD_ONE_TIME_COUNT_TIMER      0x4000000000000000L
#define GELIC_CARD_FREE_RUN_COUNT_TIMER      0x8000000000000000L

/* initial interrupt mask */
#define GELIC_CARD_TXINT	GELIC_CARD_TX_DESCR_CHAIN_END

#define GELIC_CARD_RXINT	(GELIC_CARD_RX_DESCR_CHAIN_END | \
				 GELIC_CARD_NUMBER_OF_RX_FRAME)

 /* RX descriptor data_status bits */
enum gelic_descr_rx_status {
	GELIC_DESCR_RXDMADU	= 0x80000000, /* destination MAC addr unknown */
	GELIC_DESCR_RXLSTFBF	= 0x40000000, /* last frame buffer            */
	GELIC_DESCR_RXIPCHK	= 0x20000000, /* IP checksum performed        */
	GELIC_DESCR_RXTCPCHK	= 0x10000000, /* TCP/UDP checksup performed   */
	GELIC_DESCR_RXWTPKT	= 0x00C00000, /*
					       * wakeup trigger packet
					       * 01: Magic Packet (TM)
					       * 10: ARP packet
					       * 11: Multicast MAC addr
					       */
	GELIC_DESCR_RXVLNPKT	= 0x00200000, /* VLAN packet */
	/* bit 20..16 reserved */
	GELIC_DESCR_RXRRECNUM	= 0x0000ff00, /* reception receipt number */
	/* bit 7..0 reserved */
};

#define GELIC_DESCR_DATA_STATUS_CHK_MASK	\
	(GELIC_DESCR_RXIPCHK | GELIC_DESCR_RXTCPCHK)

 /* TX descriptor data_status bits */
enum gelic_descr_tx_status {
	GELIC_DESCR_TX_TAIL	= 0x00000001, /* gelic treated this
					       * descriptor was end of
					       * a tx frame
					       */
};

/* RX descriptor data error bits */
enum gelic_descr_rx_error {
	/* bit 31 reserved */
	GELIC_DESCR_RXALNERR	= 0x40000000, /* alignement error 10/100M */
	GELIC_DESCR_RXOVERERR	= 0x20000000, /* oversize error */
	GELIC_DESCR_RXRNTERR	= 0x10000000, /* Runt error */
	GELIC_DESCR_RXIPCHKERR	= 0x08000000, /* IP checksum  error */
	GELIC_DESCR_RXTCPCHKERR	= 0x04000000, /* TCP/UDP checksum  error */
	GELIC_DESCR_RXDRPPKT	= 0x00100000, /* drop packet */
	GELIC_DESCR_RXIPFMTERR	= 0x00080000, /* IP packet format error */
	/* bit 18 reserved */
	GELIC_DESCR_RXDATAERR	= 0x00020000, /* IP packet format error */
	GELIC_DESCR_RXCALERR	= 0x00010000, /* cariier extension length
					      * error */
	GELIC_DESCR_RXCREXERR	= 0x00008000, /* carrier extention error */
	GELIC_DESCR_RXMLTCST	= 0x00004000, /* multicast address frame */
	/* bit 13..0 reserved */
};
#define GELIC_DESCR_DATA_ERROR_CHK_MASK		\
	(GELIC_DESCR_RXIPCHKERR | GELIC_DESCR_RXTCPCHKERR)

/* DMA command and status (RX and TX)*/
enum gelic_descr_dma_status {
	GELIC_DESCR_DMA_COMPLETE            = 0x00000000, /* used in tx */
	GELIC_DESCR_DMA_BUFFER_FULL         = 0x00000000, /* used in rx */
	GELIC_DESCR_DMA_RESPONSE_ERROR      = 0x10000000, /* used in rx, tx */
	GELIC_DESCR_DMA_PROTECTION_ERROR    = 0x20000000, /* used in rx, tx */
	GELIC_DESCR_DMA_FRAME_END           = 0x40000000, /* used in rx */
	GELIC_DESCR_DMA_FORCE_END           = 0x50000000, /* used in rx, tx */
	GELIC_DESCR_DMA_CARDOWNED           = 0xa0000000, /* used in rx, tx */
	GELIC_DESCR_DMA_NOT_IN_USE          = 0xb0000000, /* any other value */
};

#define GELIC_DESCR_DMA_STAT_MASK	(0xf0000000)

/* tx descriptor command and status */
enum gelic_descr_tx_dma_status {
	/* [19] */
	GELIC_DESCR_TX_DMA_IKE		= 0x00080000, /* IPSEC off */
	/* [18] */
	GELIC_DESCR_TX_DMA_FRAME_TAIL	= 0x00040000, /* last descriptor of
						       * the packet
						       */
	/* [17..16] */
	GELIC_DESCR_TX_DMA_TCP_CHKSUM	= 0x00020000, /* TCP packet */
	GELIC_DESCR_TX_DMA_UDP_CHKSUM	= 0x00030000, /* UDP packet */
	GELIC_DESCR_TX_DMA_NO_CHKSUM	= 0x00000000, /* no checksum */

	/* [1] */
	GELIC_DESCR_TX_DMA_CHAIN_END	= 0x00000002, /* DMA terminated
						       * due to chain end
						       */
};

#define GELIC_DESCR_DMA_CMD_NO_CHKSUM	\
	(GELIC_DESCR_DMA_CARDOWNED | GELIC_DESCR_TX_DMA_IKE | \
	GELIC_DESCR_TX_DMA_NO_CHKSUM)

#define GELIC_DESCR_DMA_CMD_TCP_CHKSUM	\
	(GELIC_DESCR_DMA_CARDOWNED | GELIC_DESCR_TX_DMA_IKE | \
	GELIC_DESCR_TX_DMA_TCP_CHKSUM)

#define GELIC_DESCR_DMA_CMD_UDP_CHKSUM	\
	(GELIC_DESCR_DMA_CARDOWNED | GELIC_DESCR_TX_DMA_IKE | \
	GELIC_DESCR_TX_DMA_UDP_CHKSUM)

enum gelic_descr_rx_dma_status {
	/* [ 1 ] */
	GELIC_DESCR_RX_DMA_CHAIN_END	= 0x00000002, /* DMA terminated
						       * due to chain end
						       */
};

/* for lv1_net_control */
enum gelic_lv1_net_control_code {
	GELIC_LV1_GET_MAC_ADDRESS	= 1,
	GELIC_LV1_GET_ETH_PORT_STATUS	= 2,
	GELIC_LV1_SET_NEGOTIATION_MODE	= 3,
	GELIC_LV1_GET_VLAN_ID		= 4,
};

/* status returened from GET_ETH_PORT_STATUS */
enum gelic_lv1_ether_port_status {
	GELIC_LV1_ETHER_LINK_UP		= 0x0000000000000001L,
	GELIC_LV1_ETHER_FULL_DUPLEX	= 0x0000000000000002L,
	GELIC_LV1_ETHER_AUTO_NEG	= 0x0000000000000004L,

	GELIC_LV1_ETHER_SPEED_10	= 0x0000000000000010L,
	GELIC_LV1_ETHER_SPEED_100	= 0x0000000000000020L,
	GELIC_LV1_ETHER_SPEED_1000	= 0x0000000000000040L,
	GELIC_LV1_ETHER_SPEED_MASK	= 0x0000000000000070L
};

enum gelic_lv1_vlan_index {
	/* for outgoing packets */
	GELIC_LV1_VLAN_TX_ETHERNET	= 0x0000000000000002L,
	GELIC_LV1_VLAN_TX_WIRELESS	= 0x0000000000000003L,
	/* for incoming packets */
	GELIC_LV1_VLAN_RX_ETHERNET	= 0x0000000000000012L,
	GELIC_LV1_VLAN_RX_WIRELESS	= 0x0000000000000013L
};

/* size of hardware part of gelic descriptor */
#define GELIC_DESCR_SIZE	(32)
struct gelic_descr {
	/* as defined by the hardware */
	__be32 buf_addr;
	__be32 buf_size;
	__be32 next_descr_addr;
	__be32 dmac_cmd_status;
	__be32 result_size;
	__be32 valid_size;	/* all zeroes for tx */
	__be32 data_status;
	__be32 data_error;	/* all zeroes for tx */

	/* used in the driver */
	struct sk_buff *skb;
	dma_addr_t bus_addr;
	struct gelic_descr *next;
	struct gelic_descr *prev;
	struct vlan_ethhdr vlan;
} __attribute__((aligned(32)));

struct gelic_descr_chain {
	/* we walk from tail to head */
	struct gelic_descr *head;
	struct gelic_descr *tail;
};

struct gelic_card {
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

	struct gelic_descr_chain tx_chain;
	struct gelic_descr_chain rx_chain;
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

	struct gelic_descr *tx_top, *rx_top;
	struct gelic_descr descr[0];
};


extern unsigned long p_to_lp(long pa);

#endif /* _GELIC_NET_H */
