/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Alchemy Au1x00 ethernet driver include file
 *
 * Author: Pete Popov <ppopov@mvista.com>
 *
 * Copyright 2001 MontaVista Software Inc.
 */


#define MAC_IOSIZE 0x10000
#define NUM_RX_DMA 4       /* Au1x00 has 4 rx hardware descriptors */
#define NUM_TX_DMA 4       /* Au1x00 has 4 tx hardware descriptors */

#define NUM_RX_BUFFS 4
#define NUM_TX_BUFFS 4
#define MAX_BUF_SIZE 2048

#define ETH_TX_TIMEOUT (HZ/4)
#define MAC_MIN_PKT_SIZE 64

#define MULTICAST_FILTER_LIMIT 64

/*
 * Data Buffer Descriptor. Data buffers must be aligned on 32 byte
 * boundary for both, receive and transmit.
 */
struct db_dest {
	struct db_dest *pnext;
	u32 *vaddr;
	dma_addr_t dma_addr;
};

/*
 * The transmit and receive descriptors are memory
 * mapped registers.
 */
struct tx_dma {
	u32 status;
	u32 buff_stat;
	u32 len;
	u32 pad;
};

struct rx_dma {
	u32 status;
	u32 buff_stat;
	u32 pad[2];
};


/*
 * MAC control registers, memory mapped.
 */
struct mac_reg {
	u32 control;
	u32 mac_addr_high;
	u32 mac_addr_low;
	u32 multi_hash_high;
	u32 multi_hash_low;
	u32 mii_control;
	u32 mii_data;
	u32 flow_control;
	u32 vlan1_tag;
	u32 vlan2_tag;
};


struct au1000_private {
	struct db_dest *pDBfree;
	struct db_dest db[NUM_RX_BUFFS+NUM_TX_BUFFS];
	struct rx_dma *rx_dma_ring[NUM_RX_DMA];
	struct tx_dma *tx_dma_ring[NUM_TX_DMA];
	struct db_dest *rx_db_inuse[NUM_RX_DMA];
	struct db_dest *tx_db_inuse[NUM_TX_DMA];
	u32 rx_head;
	u32 tx_head;
	u32 tx_tail;
	u32 tx_full;

	int mac_id;

	int mac_enabled;       /* whether MAC is currently enabled and running
				* (req. for mdio)
				*/

	int old_link;          /* used by au1000_adjust_link */
	int old_speed;
	int old_duplex;

	struct mii_bus *mii_bus;

	/* PHY configuration */
	int phy_static_config;
	int phy_search_highest_addr;
	int phy1_search_mac0;

	int phy_addr;
	int phy_busid;
	int phy_irq;

	/* These variables are just for quick access
	 * to certain regs addresses.
	 */
	struct mac_reg *mac;  /* mac registers                      */
	u32 *enable;     /* address of MAC Enable Register     */
	void __iomem *macdma;	/* base of MAC DMA port */
	void *vaddr;		/* virtual address of rx/tx buffers   */
	dma_addr_t dma_addr;	/* dma address of rx/tx buffers       */

	spinlock_t lock;       /* Serialise access to device */

	u32 msg_enable;
};
