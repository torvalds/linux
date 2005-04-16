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

/* FIXME 
 * The PHY defines should be in a separate file.
 */

/* MII register offsets */
#define	MII_CONTROL 0x0000
#define MII_STATUS  0x0001
#define MII_PHY_ID0 0x0002
#define	MII_PHY_ID1 0x0003
#define MII_ANADV   0x0004
#define MII_ANLPAR  0x0005
#define MII_AEXP    0x0006
#define MII_ANEXT   0x0007
#define MII_LSI_PHY_CONFIG 0x0011
/* Status register */
#define MII_LSI_PHY_STAT   0x0012
#define MII_AMD_PHY_STAT   MII_LSI_PHY_STAT
#define MII_INTEL_PHY_STAT 0x0011

#define MII_AUX_CNTRL  0x0018
/* mii registers specific to AMD 79C901 */
#define	MII_STATUS_SUMMARY = 0x0018

/* MII Control register bit definitions. */
#define	MII_CNTL_FDX      0x0100
#define MII_CNTL_RST_AUTO 0x0200
#define	MII_CNTL_ISOLATE  0x0400
#define MII_CNTL_PWRDWN   0x0800
#define	MII_CNTL_AUTO     0x1000
#define MII_CNTL_F100     0x2000
#define	MII_CNTL_LPBK     0x4000
#define MII_CNTL_RESET    0x8000

/* MII Status register bit  */
#define	MII_STAT_EXT        0x0001 
#define MII_STAT_JAB        0x0002
#define	MII_STAT_LINK       0x0004
#define MII_STAT_CAN_AUTO   0x0008
#define	MII_STAT_FAULT      0x0010 
#define MII_STAT_AUTO_DONE  0x0020
#define	MII_STAT_CAN_T      0x0800
#define MII_STAT_CAN_T_FDX  0x1000
#define	MII_STAT_CAN_TX     0x2000 
#define MII_STAT_CAN_TX_FDX 0x4000
#define	MII_STAT_CAN_T4     0x8000


#define		MII_ID1_OUI_LO		0xFC00	/* low bits of OUI mask */
#define		MII_ID1_MODEL		0x03F0	/* model number */
#define		MII_ID1_REV		0x000F	/* model number */

/* MII NWAY Register Bits ...
   valid for the ANAR (Auto-Negotiation Advertisement) and
   ANLPAR (Auto-Negotiation Link Partner) registers */
#define	MII_NWAY_NODE_SEL 0x001f
#define MII_NWAY_CSMA_CD  0x0001
#define	MII_NWAY_T	  0x0020
#define MII_NWAY_T_FDX    0x0040
#define	MII_NWAY_TX       0x0080
#define MII_NWAY_TX_FDX   0x0100
#define	MII_NWAY_T4       0x0200 
#define MII_NWAY_PAUSE    0x0400 
#define	MII_NWAY_RF       0x2000 /* Remote Fault */
#define MII_NWAY_ACK      0x4000 /* Remote Acknowledge */
#define	MII_NWAY_NP       0x8000 /* Next Page (Enable) */

/* mii stsout register bits */
#define	MII_STSOUT_LINK_FAIL 0x4000
#define	MII_STSOUT_SPD       0x0080
#define MII_STSOUT_DPLX      0x0040

/* mii stsics register bits */
#define	MII_STSICS_SPD       0x8000
#define MII_STSICS_DPLX      0x4000
#define	MII_STSICS_LINKSTS   0x0001

/* mii stssum register bits */
#define	MII_STSSUM_LINK  0x0008
#define MII_STSSUM_DPLX  0x0004
#define	MII_STSSUM_AUTO  0x0002
#define MII_STSSUM_SPD   0x0001

/* lsi phy status register */
#define MII_LSI_PHY_STAT_FDX	0x0040
#define MII_LSI_PHY_STAT_SPD	0x0080

/* amd phy status register */
#define MII_AMD_PHY_STAT_FDX	0x0800
#define MII_AMD_PHY_STAT_SPD	0x0400

/* intel phy status register */
#define MII_INTEL_PHY_STAT_FDX	0x0200
#define MII_INTEL_PHY_STAT_SPD	0x4000

/* Auxilliary Control/Status Register */
#define MII_AUX_FDX      0x0001
#define MII_AUX_100      0x0002
#define MII_AUX_F100     0x0004
#define MII_AUX_ANEG     0x0008

typedef struct mii_phy {
	struct mii_phy * next;
	struct mii_chip_info * chip_info;
	u16 status;
	u32 *mii_control_reg;
	u32 *mii_data_reg;
} mii_phy_t;

struct phy_ops {
	int (*phy_init) (struct net_device *, int);
	int (*phy_reset) (struct net_device *, int);
	int (*phy_status) (struct net_device *, int, u16 *, u16 *);
};

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
	mii_phy_t *mii;
	struct phy_ops *phy_ops;
	
	/* These variables are just for quick access to certain regs addresses. */
	volatile mac_reg_t *mac;  /* mac registers                      */   
	volatile u32 *enable;     /* address of MAC Enable Register     */

	u32 vaddr;                /* virtual address of rx/tx buffers   */
	dma_addr_t dma_addr;      /* dma address of rx/tx buffers       */

	u8 *hash_table;
	u32 hash_mode;
	u32 intr_work_done; /* number of Rx and Tx pkts processed in the isr */
	int phy_addr;          /* phy address */
	u32 options;           /* User-settable misc. driver options. */
	u32 drv_flags;
	int want_autoneg;
	struct net_device_stats stats;
	struct timer_list timer;
	spinlock_t lock;       /* Serialise access to device */
};
