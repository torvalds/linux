/*
 *  olympic.h (c) 1999 Peter De Schrijver All Rights Reserved
 *                1999,2000 Mike Phillips (mikep@linuxtr.net)
 *
 *  Linux driver for IBM PCI tokenring cards based on the olympic and the PIT/PHY chipset.
 *
 *  Base Driver Skeleton:
 *      Written 1993-94 by Donald Becker.
 *
 *      Copyright 1993 United States Government as represented by the
 *      Director, National Security Agency.
 *
 *  This software may be used and distributed according to the terms
 *  of the GNU General Public License, incorporated herein by reference.
 */

#define CID 0x4e

#define BCTL 0x70
#define BCTL_SOFTRESET (1<<15)
#define BCTL_MIMREB (1<<6)
#define BCTL_MODE_INDICATOR (1<<5)

#define GPR 0x4a
#define GPR_OPTI_BF (1<<6)
#define GPR_NEPTUNE_BF (1<<4) 
#define GPR_AUTOSENSE (1<<2)
#define GPR_16MBPS (1<<3) 

#define PAG 0x85
#define LBC 0x8e

#define LISR 0x10
#define LISR_SUM 0x14
#define LISR_RWM 0x18

#define LISR_LIE (1<<15)
#define LISR_SLIM (1<<13)
#define LISR_SLI (1<<12)
#define LISR_PCMSRMASK (1<<11)
#define LISR_PCMSRINT (1<<10)
#define LISR_WOLMASK (1<<9)
#define LISR_WOL (1<<8)
#define LISR_SRB_CMD (1<<5)
#define LISR_ASB_REPLY (1<<4)
#define LISR_ASB_FREE_REQ (1<<2)
#define LISR_ARB_FREE (1<<1)
#define LISR_TRB_FRAME (1<<0)

#define SISR 0x20
#define SISR_SUM 0x24
#define SISR_RWM 0x28
#define SISR_RR 0x2C
#define SISR_RESMASK 0x30
#define SISR_MASK 0x54
#define SISR_MASK_SUM 0x58
#define SISR_MASK_RWM 0x5C

#define SISR_TX2_IDLE (1<<31)
#define SISR_TX2_HALT (1<<29)
#define SISR_TX2_EOF (1<<28)
#define SISR_TX1_IDLE (1<<27)
#define SISR_TX1_HALT (1<<25)
#define SISR_TX1_EOF (1<<24)
#define SISR_TIMEOUT (1<<23)
#define SISR_RX_NOBUF (1<<22)
#define SISR_RX_STATUS (1<<21)
#define SISR_RX_HALT (1<<18)
#define SISR_RX_EOF_EARLY (1<<16)
#define SISR_MI (1<<15)
#define SISR_PI (1<<13)
#define SISR_ERR (1<<9)
#define SISR_ADAPTER_CHECK (1<<6)
#define SISR_SRB_REPLY (1<<5)
#define SISR_ASB_FREE (1<<4)
#define SISR_ARB_CMD (1<<3)
#define SISR_TRB_REPLY (1<<2)

#define EISR 0x34
#define EISR_RWM 0x38
#define EISR_MASK 0x3c
#define EISR_MASK_OPTIONS 0x001FFF7F

#define LAPA 0x60
#define LAPWWO 0x64
#define LAPWWC 0x68
#define LAPCTL 0x6C
#define LAIPD 0x78
#define LAIPDDINC 0x7C

#define TIMER 0x50

#define CLKCTL 0x74
#define CLKCTL_PAUSE (1<<15) 

#define PM_CON 0x4

#define BMCTL_SUM 0x40
#define BMCTL_RWM 0x44
#define BMCTL_TX2_DIS (1<<30) 
#define BMCTL_TX1_DIS (1<<26) 
#define BMCTL_RX_DIS (1<<22) 

#define BMASR 0xcc

#define RXDESCQ 0x90
#define RXDESCQCNT 0x94
#define RXCDA 0x98
#define RXENQ 0x9C
#define RXSTATQ 0xA0
#define RXSTATQCNT 0xA4
#define RXCSA 0xA8
#define RXCLEN 0xAC
#define RXHLEN 0xAE

#define TXDESCQ_1 0xb0
#define TXDESCQ_2 0xd0
#define TXDESCQCNT_1 0xb4
#define TXDESCQCNT_2 0xd4
#define TXCDA_1 0xb8
#define TXCDA_2 0xd8
#define TXENQ_1 0xbc
#define TXENQ_2 0xdc
#define TXSTATQ_1 0xc0
#define TXSTATQ_2 0xe0
#define TXSTATQCNT_1 0xc4
#define TXSTATQCNT_2 0xe4
#define TXCSA_1 0xc8
#define TXCSA_2 0xe8
/* Cardbus */
#define FERMASK 0xf4
#define FERMASK_INT_BIT (1<<15)

#define OLYMPIC_IO_SPACE 256

#define SRB_COMMAND_SIZE 50

#define OLYMPIC_MAX_ADAPTERS 8 /* 0x08 __MODULE_STRING can't hand 0xnn */

/* Defines for LAN STATUS CHANGE reports */
#define LSC_SIG_LOSS 0x8000
#define LSC_HARD_ERR 0x4000
#define LSC_SOFT_ERR 0x2000
#define LSC_TRAN_BCN 0x1000
#define LSC_LWF      0x0800
#define LSC_ARW      0x0400
#define LSC_FPE      0x0200
#define LSC_RR       0x0100
#define LSC_CO       0x0080
#define LSC_SS       0x0040
#define LSC_RING_REC 0x0020
#define LSC_SR_CO    0x0010
#define LSC_FDX_MODE 0x0004

/* Defines for OPEN ADAPTER command */

#define OPEN_ADAPTER_EXT_WRAP (1<<15)
#define OPEN_ADAPTER_DIS_HARDEE (1<<14)
#define OPEN_ADAPTER_DIS_SOFTERR (1<<13)
#define OPEN_ADAPTER_PASS_ADC_MAC (1<<12)
#define OPEN_ADAPTER_PASS_ATT_MAC (1<<11)
#define OPEN_ADAPTER_ENABLE_EC (1<<10)
#define OPEN_ADAPTER_CONTENDER (1<<8)
#define OPEN_ADAPTER_PASS_BEACON (1<<7)
#define OPEN_ADAPTER_ENABLE_FDX (1<<6)
#define OPEN_ADAPTER_ENABLE_RPL (1<<5)
#define OPEN_ADAPTER_INHIBIT_ETR (1<<4)
#define OPEN_ADAPTER_INTERNAL_WRAP (1<<3)
#define OPEN_ADAPTER_USE_OPTS2 (1<<0)

#define OPEN_ADAPTER_2_ENABLE_ONNOW (1<<15)

/* Defines for SRB Commands */

#define SRB_ACCESS_REGISTER 0x1f
#define SRB_CLOSE_ADAPTER 0x04
#define SRB_CONFIGURE_BRIDGE 0x0c
#define SRB_CONFIGURE_WAKEUP_EVENT 0x1a
#define SRB_MODIFY_BRIDGE_PARMS 0x15
#define SRB_MODIFY_OPEN_OPTIONS 0x01
#define SRB_MODIFY_RECEIVE_OPTIONS 0x17
#define SRB_NO_OPERATION 0x00
#define SRB_OPEN_ADAPTER 0x03
#define SRB_READ_LOG 0x08
#define SRB_READ_SR_COUNTERS 0x16
#define SRB_RESET_GROUP_ADDRESS 0x02
#define SRB_SAVE_CONFIGURATION 0x1b
#define SRB_SET_BRIDGE_PARMS 0x09
#define SRB_SET_BRIDGE_TARGETS 0x10
#define SRB_SET_FUNC_ADDRESS 0x07
#define SRB_SET_GROUP_ADDRESS 0x06
#define SRB_SET_GROUP_ADDR_OPTIONS 0x11
#define SRB_UPDATE_WAKEUP_PATTERN 0x19

/* Clear return code */

#define OLYMPIC_CLEAR_RET_CODE 0xfe 

/* ARB Commands */
#define ARB_RECEIVE_DATA 0x81
#define ARB_LAN_CHANGE_STATUS 0x84
/* ASB Response commands */

#define ASB_RECEIVE_DATA 0x81


/* Olympic defaults for buffers */
 
#define OLYMPIC_RX_RING_SIZE 16 /* should be a power of 2 */
#define OLYMPIC_TX_RING_SIZE 8 /* should be a power of 2 */

#define PKT_BUF_SZ 4096 /* Default packet size */

/* Olympic data structures */

/* xxxx These structures are all little endian in hardware. */

struct olympic_tx_desc {
	__le32 buffer;
	__le32 status_length;
};

struct olympic_tx_status {
	__le32 status;
};

struct olympic_rx_desc {
	__le32 buffer;
	__le32 res_length; 
};

struct olympic_rx_status {
	__le32 fragmentcnt_framelen;
	__le32 status_buffercnt;
};
/* xxxx END These structures are all little endian in hardware. */
/* xxxx There may be more, but I'm pretty sure about these */

struct mac_receive_buffer {
	__le16 next ; 
	u8 padding ; 
	u8 frame_status ;
	__le16 buffer_length ; 
	u8 frame_data ; 
};

struct olympic_private {
	
	u16 srb;      /* be16 */
	u16 trb;      /* be16 */
	u16 arb;      /* be16 */
	u16 asb;      /* be16 */

	u8 __iomem *olympic_mmio;
	u8 __iomem *olympic_lap;
	struct pci_dev *pdev ; 
	const char *olympic_card_name;

	spinlock_t olympic_lock ; 

	volatile int srb_queued;    /* True if an SRB is still posted */	
	wait_queue_head_t srb_wait;

	volatile int asb_queued;    /* True if an ASB is posted */

	volatile int trb_queued;   /* True if a TRB is posted */
	wait_queue_head_t trb_wait ; 

	/* These must be on a 4 byte boundary. */
	struct olympic_rx_desc olympic_rx_ring[OLYMPIC_RX_RING_SIZE];
	struct olympic_tx_desc olympic_tx_ring[OLYMPIC_TX_RING_SIZE];
	struct olympic_rx_status olympic_rx_status_ring[OLYMPIC_RX_RING_SIZE];	
	struct olympic_tx_status olympic_tx_status_ring[OLYMPIC_TX_RING_SIZE];	

	struct sk_buff *tx_ring_skb[OLYMPIC_TX_RING_SIZE], *rx_ring_skb[OLYMPIC_RX_RING_SIZE];	
	int tx_ring_free, tx_ring_last_status, rx_ring_last_received,rx_status_last_received, free_tx_ring_entries;

	struct net_device_stats olympic_stats ;
	u16 olympic_lan_status ;
	u8 olympic_ring_speed ;
	u16 pkt_buf_sz ; 
	u8 olympic_receive_options, olympic_copy_all_options,olympic_message_level, olympic_network_monitor;  
	u16 olympic_addr_table_addr, olympic_parms_addr ; 
	u8 olympic_laa[6] ; 
	u32 rx_ring_dma_addr;
	u32 rx_status_ring_dma_addr;
	u32 tx_ring_dma_addr;
	u32 tx_status_ring_dma_addr;
};

struct olympic_adapter_addr_table {

	u8 node_addr[6] ; 
	u8 reserved[4] ; 
	u8 func_addr[4] ; 
} ; 

struct olympic_parameters_table { 
	
	u8  phys_addr[4] ; 
	u8  up_node_addr[6] ; 
	u8  up_phys_addr[4] ; 
	u8  poll_addr[6] ; 
	u16 reserved ; 
	u16 acc_priority ; 
	u16 auth_source_class ; 
	u16 att_code ; 
	u8  source_addr[6] ; 
	u16 beacon_type ; 
	u16 major_vector ; 
	u16 lan_status ; 
	u16 soft_error_time ; 
 	u16 reserved1 ; 
	u16 local_ring ; 
	u16 mon_error ; 
	u16 beacon_transmit ; 
	u16 beacon_receive ; 
	u16 frame_correl ; 
	u8  beacon_naun[6] ; 
	u32 reserved2 ; 
	u8  beacon_phys[4] ; 	
}; 
