/*
 *  3c359.h (c) 2000 Mike Phillips (mikep@linuxtr.net) All Rights Reserved
 *
 *  Linux driver for 3Com 3C359 Token Link PCI XL cards.
 *
 *  This software may be used and distributed according to the terms
 *  of the GNU General Public License Version 2 or (at your option) 
 *  any later verion, incorporated herein by reference.
 */

/* Memory Access Commands */
#define IO_BYTE_READ 0x28 << 24
#define IO_BYTE_WRITE 0x18 << 24 
#define IO_WORD_READ 0x20 << 24
#define IO_WORD_WRITE 0x10 << 24
#define MMIO_BYTE_READ 0x88 << 24
#define MMIO_BYTE_WRITE 0x48 << 24
#define MMIO_WORD_READ 0x80 << 24
#define MMIO_WORD_WRITE 0x40 << 24
#define MEM_BYTE_READ 0x8C << 24
#define MEM_BYTE_WRITE 0x4C << 24
#define MEM_WORD_READ 0x84 << 24
#define MEM_WORD_WRITE 0x44 << 24

#define PMBAR 0x1C80
#define PMB_CPHOLD (1<<10)

#define CPATTENTION 0x180D
#define CPA_PMBARVIS (1<<7)
#define CPA_MEMWREN (1<<6)

#define SWITCHSETTINGS 0x1C88
#define EECONTROL 0x1C8A
#define EEDATA 0x1C8C
#define EEREAD 0x0080 
#define EEWRITE 0x0040
#define EEERASE 0x0060
#define EE_ENABLE_WRITE 0x0030
#define EEBUSY (1<<15)

#define WRBR 0xCDE02
#define WWOR 0xCDE04
#define WWCR 0xCDE06
#define MACSTATUS 0xCDE08 
#define MISR_RW 0xCDE0B
#define MISR_AND 0xCDE2B
#define MISR_SET 0xCDE4B
#define RXBUFAREA 0xCDE10
#define RXEARLYTHRESH 0xCDE12
#define TXSTARTTHRESH 0x58
#define DNPRIREQTHRESH 0x2C

#define MISR_CSRB (1<<5)
#define MISR_RASB (1<<4)
#define MISR_SRBFR (1<<3)
#define MISR_ASBFR (1<<2)
#define MISR_ARBF (1<<1) 

/* MISR Flags memory locations */
#define MF_SSBF 0xDFFE0 
#define MF_ARBF 0xDFFE1
#define MF_ASBFR 0xDFFE2
#define MF_SRBFR 0xDFFE3
#define MF_RASB 0xDFFE4
#define MF_CSRB 0xDFFE5

#define MMIO_MACDATA 0x10 
#define MMIO_MAC_ACCESS_CMD 0x14
#define MMIO_TIMER 0x1A
#define MMIO_DMA_CTRL 0x20
#define MMIO_DNLISTPTR 0x24
#define MMIO_HASHFILTER 0x28
#define MMIO_CONFIG 0x29
#define MMIO_DNPRIREQTHRESH 0x2C
#define MMIO_DNPOLL 0x2D
#define MMIO_UPPKTSTATUS 0x30
#define MMIO_FREETIMER 0x34
#define MMIO_COUNTDOWN 0x36
#define MMIO_UPLISTPTR 0x38
#define MMIO_UPPOLL 0x3C
#define MMIO_UPBURSTTHRESH 0x40
#define MMIO_DNBURSTTHRESH 0x41
#define MMIO_INTSTATUS_AUTO 0x56
#define MMIO_TXSTARTTHRESH 0x58
#define MMIO_INTERRUPTENABLE 0x5A
#define MMIO_INDICATIONENABLE 0x5C
#define MMIO_COMMAND 0x5E  /* These two are meant to be the same */
#define MMIO_INTSTATUS 0x5E /* Makes the code more readable this way */
#define INTSTAT_CMD_IN_PROGRESS (1<<12) 
#define INTSTAT_SRB (1<<14)
#define INTSTAT_INTLATCH (1<<0)

/* Indication / Interrupt Mask 
 * Annoyingly the bits to be set in the indication and interrupt enable
 * do not match with the actual bits received in the interrupt, although
 * they are in the same order. 
 * The mapping for the indication / interrupt are:
 * Bit	Indication / Interrupt
 *   0	HostError
 *   1	txcomplete
 *   2	updneeded
 *   3	rxcomplete
 *   4	intrequested
 *   5	macerror
 *   6  dncomplete
 *   7	upcomplete
 *   8	txunderrun
 *   9	asbf
 *  10	srbr
 *  11	arbc
 *
 *  The only ones we don't want to receive are txcomplete and rxcomplete
 *  we use dncomplete and upcomplete instead.
 */

#define INT_MASK 0xFF5

/* Note the subtle difference here, IND and INT */

#define SETINDENABLE (8<<12)
#define SETINTENABLE (7<<12)
#define SRBBIT (1<<10)
#define ASBBIT (1<<9)
#define ARBBIT (1<<11)

#define SRB 0xDFE90
#define ASB 0xDFED0
#define ARB 0xD0000
#define SCRATCH 0xDFEF0

#define INT_REQUEST 0x6000 /* (6 << 12) */
#define ACK_INTERRUPT 0x6800 /* (13 <<11) */
#define GLOBAL_RESET 0x00 
#define DNDISABLE 0x5000 
#define DNENABLE 0x4800 
#define DNSTALL 0x3002
#define DNRESET 0x5800
#define DNUNSTALL 0x3003
#define UPRESET 0x2800
#define UPSTALL 0x3000
#define UPUNSTALL 0x3001
#define SETCONFIG 0x4000
#define SETTXSTARTTHRESH 0x9800 

/* Received Interrupts */
#define ASBFINT (1<<13)
#define SRBRINT (1<<14)
#define ARBCINT (1<<15)
#define TXUNDERRUN (1<<11)

#define UPCOMPINT (1<<10)
#define DNCOMPINT (1<<9)
#define HARDERRINT (1<<7)
#define RXCOMPLETE (1<<4)
#define TXCOMPINT (1<<2)
#define HOSTERRINT (1<<1)

/* Receive descriptor bits */
#define RXOVERRUN (1<<19)
#define RXFC (1<<21)
#define RXAR (1<<22)
#define RXUPDCOMPLETE (1<<23)
#define RXUPDFULL (1<<24)
#define RXUPLASTFRAG (1<<31)

/* Transmit descriptor bits */
#define TXDNCOMPLETE (1<<16)
#define TXTXINDICATE (1<<27)
#define TXDPDEMPTY (1<<29)
#define TXDNINDICATE (1<<31)
#define TXDNFRAGLAST (1<<31)

/* Interrupts to Acknowledge */
#define LATCH_ACK 1 
#define TXCOMPACK (1<<1)
#define INTREQACK (1<<2)
#define DNCOMPACK (1<<3)
#define UPCOMPACK (1<<4)
#define ASBFACK (1<<5)
#define SRBRACK (1<<6)
#define ARBCACK (1<<7)

#define XL_IO_SPACE 128
#define SRB_COMMAND_SIZE 50

/* Adapter Commands */
#define REQUEST_INT 0x00
#define MODIFY_OPEN_PARMS 0x01
#define RESTORE_OPEN_PARMS 0x02
#define OPEN_NIC 0x03
#define CLOSE_NIC 0x04
#define SET_SLEEP_MODE 0x05
#define SET_GROUP_ADDRESS 0x06
#define SET_FUNC_ADDRESS 0x07
#define READ_LOG 0x08
#define SET_MULTICAST_MODE 0x0C
#define CHANGE_WAKEUP_PATTERN 0x0D
#define GET_STATISTICS 0x13
#define SET_RECEIVE_MODE 0x1F

/* ARB Commands */
#define RECEIVE_DATA 0x81
#define RING_STATUS_CHANGE 0x84

/* ASB Commands */
#define ASB_RECEIVE_DATE 0x81 

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

#define XL_MAX_ADAPTERS 8 /* 0x08 __MODULE_STRING can't hand 0xnn */

/* 3c359 defaults for buffers */
 
#define XL_RX_RING_SIZE 16 /* must be a power of 2 */
#define XL_TX_RING_SIZE 16 /* must be a power of 2 */

#define PKT_BUF_SZ 4096 /* Default packet size */

/* 3c359 data structures */

struct xl_tx_desc {
	u32 dnnextptr ; 
	u32 framestartheader ; 
	u32 buffer ;
	u32 buffer_length ;
};

struct xl_rx_desc {
	u32 upnextptr ; 
	u32 framestatus ; 
	u32 upfragaddr ; 
	u32 upfraglen ; 
};

struct xl_private {
	

	/* These two structures must be aligned on 8 byte boundaries */

	/* struct xl_rx_desc xl_rx_ring[XL_RX_RING_SIZE]; */
	/* struct xl_tx_desc xl_tx_ring[XL_TX_RING_SIZE]; */
	struct xl_rx_desc *xl_rx_ring ; 
	struct xl_tx_desc *xl_tx_ring ; 
	struct sk_buff *tx_ring_skb[XL_TX_RING_SIZE], *rx_ring_skb[XL_RX_RING_SIZE];	
	int tx_ring_head, tx_ring_tail ;  
	int rx_ring_tail, rx_ring_no ; 
	int free_ring_entries ; 

	u16 srb;
	u16 arb;
	u16 asb;

	u8 __iomem *xl_mmio;
	char *xl_card_name;
	struct pci_dev *pdev ; 
	
	spinlock_t xl_lock ; 

	volatile int srb_queued;    
	struct wait_queue *srb_wait;
	volatile int asb_queued;   

	struct net_device_stats xl_stats ;

	u16 mac_buffer ; 	
	u16 xl_lan_status ;
	u8 xl_ring_speed ;
	u16 pkt_buf_sz ; 
	u8 xl_message_level; 
	u16 xl_copy_all_options ;  
	unsigned char xl_functional_addr[4] ; 
	u16 xl_addr_table_addr, xl_parms_addr ; 
	u8 xl_laa[6] ; 
	u32 rx_ring_dma_addr ; 
	u32 tx_ring_dma_addr ; 
};

