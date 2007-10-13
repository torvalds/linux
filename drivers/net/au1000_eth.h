/*
 *
 * Alchemy Au1x00 ethernet driver include file
 *
 * Author: Pete Popov <ppopov@mvista.com>
 *
 * Copyright 2001 MontaVista Software Inc.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 *
 */


#define MAC_IOSIZE 0x10000
#define NUM_RX_DMA 4       /* Au1x00 has 4 rx hardware descriptors */
#define NUM_TX_DMA 4       /* Au1x00 has 4 tx hardware descriptors */

#define NUM_RX_BUFFS 4
#define NUM_TX_BUFFS 4
#define MAX_BUF_SIZE 2048

#define ETH_TX_TIMEOUT HZ/4
#define MAC_MIN_PKT_SIZE 64

#define MULTICAST_FILTER_LIMIT 64

/*
 * Data Buffer Descriptor. Data buffers must be aligned on 32 byte
 * boundary for both, receive and transmit.
 */
typedef struct db_dest {
	struct db_dest *pnext;
	volatile u32 *vaddr;
	dma_addr_t dma_addr;
} db_dest_t;

/*
 * The transmit and receive descriptors are memory
 * mapped registers.
 */
typedef struct tx_dma {
	u32 status;
	u32 buff_stat;
	u32 len;
	u32 pad;
} tx_dma_t;

typedef struct rx_dma {
	u32 status;
	u32 buff_stat;
	u32 pad[2];
} rx_dma_t;


/*
 * MAC control registers, memory mapped.
 */
typedef struct mac_reg {
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
} mac_reg_t;


struct au1000_private {
	db_dest_t *pDBfree;
	db_dest_t db[NUM_RX_BUFFS+NUM_TX_BUFFS];
	volatile rx_dma_t *rx_dma_ring[NUM_RX_DMA];
	volatile tx_dma_t *tx_dma_ring[NUM_TX_DMA];
	db_dest_t *rx_db_inuse[NUM_RX_DMA];
	db_dest_t *tx_db_inuse[NUM_TX_DMA];
	u32 rx_head;
	u32 tx_head;
	u32 tx_tail;
	u32 tx_full;

	int mac_id;

	int mac_enabled;       /* whether MAC is currently enabled and running (req. for mdio) */

	int old_link;          /* used by au1000_adjust_link */
	int old_speed;
	int old_duplex;

	struct phy_device *phy_dev;
	struct mii_bus mii_bus;

	/* These variables are just for quick access to certain regs addresses. */
	volatile mac_reg_t *mac;  /* mac registers                      */
	volatile u32 *enable;     /* address of MAC Enable Register     */

	u32 vaddr;                /* virtual address of rx/tx buffers   */
	dma_addr_t dma_addr;      /* dma address of rx/tx buffers       */

	spinlock_t lock;       /* Serialise access to device */
};
