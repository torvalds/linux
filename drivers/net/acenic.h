#ifndef _ACENIC_H_
#define _ACENIC_H_


/*
 * Generate TX index update each time, when TX ring is closed.
 * Normally, this is not useful, because results in more dma (and irqs
 * without TX_COAL_INTS_ONLY).
 */
#define USE_TX_COAL_NOW	 0

/*
 * Addressing:
 *
 * The Tigon uses 64-bit host addresses, regardless of their actual
 * length, and it expects a big-endian format. For 32 bit systems the
 * upper 32 bits of the address are simply ignored (zero), however for
 * little endian 64 bit systems (Alpha) this looks strange with the
 * two parts of the address word being swapped.
 *
 * The addresses are split in two 32 bit words for all architectures
 * as some of them are in PCI shared memory and it is necessary to use
 * readl/writel to access them.
 *
 * The addressing code is derived from Pete Wyckoff's work, but
 * modified to deal properly with readl/writel usage.
 */

struct ace_regs {
	u32	pad0[16];	/* PCI control registers */

	u32	HostCtrl;	/* 0x40 */
	u32	LocalCtrl;

	u32	pad1[2];

	u32	MiscCfg;	/* 0x50 */

	u32	pad2[2];

	u32	PciState;

	u32	pad3[2];	/* 0x60 */

	u32	WinBase;
	u32	WinData;

	u32	pad4[12];	/* 0x70 */

	u32	DmaWriteState;	/* 0xa0 */
	u32	pad5[3];
	u32	DmaReadState;	/* 0xb0 */

	u32	pad6[26];

	u32	AssistState;

	u32	pad7[8];	/* 0x120 */

	u32	CpuCtrl;	/* 0x140 */
	u32	Pc;

	u32	pad8[3];

	u32	SramAddr;	/* 0x154 */
	u32	SramData;

	u32	pad9[49];

	u32	MacRxState;	/* 0x220 */

	u32	pad10[7];

	u32	CpuBCtrl;	/* 0x240 */
	u32	PcB;

	u32	pad11[3];

	u32	SramBAddr;	/* 0x254 */
	u32	SramBData;

	u32	pad12[105];

	u32	pad13[32];	/* 0x400 */
	u32	Stats[32];

	u32	Mb0Hi;		/* 0x500 */
	u32	Mb0Lo;
	u32	Mb1Hi;
	u32	CmdPrd;
	u32	Mb2Hi;
	u32	TxPrd;
	u32	Mb3Hi;
	u32	RxStdPrd;
	u32	Mb4Hi;
	u32	RxJumboPrd;
	u32	Mb5Hi;
	u32	RxMiniPrd;
	u32	Mb6Hi;
	u32	Mb6Lo;
	u32	Mb7Hi;
	u32	Mb7Lo;
	u32	Mb8Hi;
	u32	Mb8Lo;
	u32	Mb9Hi;
	u32	Mb9Lo;
	u32	MbAHi;
	u32	MbALo;
	u32	MbBHi;
	u32	MbBLo;
	u32	MbCHi;
	u32	MbCLo;
	u32	MbDHi;
	u32	MbDLo;
	u32	MbEHi;
	u32	MbELo;
	u32	MbFHi;
	u32	MbFLo;

	u32	pad14[32];

	u32	MacAddrHi;	/* 0x600 */
	u32	MacAddrLo;
	u32	InfoPtrHi;
	u32	InfoPtrLo;
	u32	MultiCastHi;	/* 0x610 */
	u32	MultiCastLo;
	u32	ModeStat;
	u32	DmaReadCfg;
	u32	DmaWriteCfg;	/* 0x620 */
	u32	TxBufRat;
	u32	EvtCsm;
	u32	CmdCsm;
	u32	TuneRxCoalTicks;/* 0x630 */
	u32	TuneTxCoalTicks;
	u32	TuneStatTicks;
	u32	TuneMaxTxDesc;
	u32	TuneMaxRxDesc;	/* 0x640 */
	u32	TuneTrace;
	u32	TuneLink;
	u32	TuneFastLink;
	u32	TracePtr;	/* 0x650 */
	u32	TraceStrt;
	u32	TraceLen;
	u32	IfIdx;
	u32	IfMtu;		/* 0x660 */
	u32	MaskInt;
	u32	GigLnkState;
	u32	FastLnkState;
	u32	pad16[4];	/* 0x670 */
	u32	RxRetCsm;	/* 0x680 */

	u32	pad17[31];

	u32	CmdRng[64];	/* 0x700 */
	u32	Window[0x200];
};


typedef struct {
	u32 addrhi;
	u32 addrlo;
} aceaddr;


#define ACE_WINDOW_SIZE	0x800

#define ACE_JUMBO_MTU 9000
#define ACE_STD_MTU 1500

#define ACE_TRACE_SIZE 0x8000

/*
 * Host control register bits.
 */

#define IN_INT		0x01
#define CLR_INT		0x02
#define HW_RESET	0x08
#define BYTE_SWAP	0x10
#define WORD_SWAP	0x20
#define MASK_INTS	0x40

/*
 * Local control register bits.
 */

#define EEPROM_DATA_IN		0x800000
#define EEPROM_DATA_OUT		0x400000
#define EEPROM_WRITE_ENABLE	0x200000
#define EEPROM_CLK_OUT		0x100000

#define EEPROM_BASE		0xa0000000

#define EEPROM_WRITE_SELECT	0xa0
#define EEPROM_READ_SELECT	0xa1

#define SRAM_BANK_512K		0x200


/*
 * udelay() values for when clocking the eeprom
 */
#define ACE_SHORT_DELAY		2
#define ACE_LONG_DELAY		4


/*
 * Misc Config bits
 */

#define SYNC_SRAM_TIMING	0x100000


/*
 * CPU state bits.
 */

#define CPU_RESET		0x01
#define CPU_TRACE		0x02
#define CPU_PROM_FAILED		0x10
#define CPU_HALT		0x00010000
#define CPU_HALTED		0xffff0000


/*
 * PCI State bits.
 */

#define DMA_READ_MAX_4		0x04
#define DMA_READ_MAX_16		0x08
#define DMA_READ_MAX_32		0x0c
#define DMA_READ_MAX_64		0x10
#define DMA_READ_MAX_128	0x14
#define DMA_READ_MAX_256	0x18
#define DMA_READ_MAX_1K		0x1c
#define DMA_WRITE_MAX_4		0x20
#define DMA_WRITE_MAX_16	0x40
#define DMA_WRITE_MAX_32	0x60
#define DMA_WRITE_MAX_64	0x80
#define DMA_WRITE_MAX_128	0xa0
#define DMA_WRITE_MAX_256	0xc0
#define DMA_WRITE_MAX_1K	0xe0
#define DMA_READ_WRITE_MASK	0xfc
#define MEM_READ_MULTIPLE	0x00020000
#define PCI_66MHZ		0x00080000
#define PCI_32BIT		0x00100000
#define DMA_WRITE_ALL_ALIGN	0x00800000
#define READ_CMD_MEM		0x06000000
#define WRITE_CMD_MEM		0x70000000


/*
 * Mode status
 */

#define ACE_BYTE_SWAP_BD	0x02
#define ACE_WORD_SWAP_BD	0x04		/* not actually used */
#define ACE_WARN		0x08
#define ACE_BYTE_SWAP_DMA	0x10
#define ACE_NO_JUMBO_FRAG	0x200
#define ACE_FATAL		0x40000000


/*
 * DMA config
 */

#define DMA_THRESH_1W		0x10
#define DMA_THRESH_2W		0x20
#define DMA_THRESH_4W		0x40
#define DMA_THRESH_8W		0x80
#define DMA_THRESH_16W		0x100
#define DMA_THRESH_32W		0x0	/* not described in doc, but exists. */


/*
 * Tuning parameters
 */

#define TICKS_PER_SEC		1000000


/*
 * Link bits
 */

#define LNK_PREF		0x00008000
#define LNK_10MB		0x00010000
#define LNK_100MB		0x00020000
#define LNK_1000MB		0x00040000
#define LNK_FULL_DUPLEX		0x00080000
#define LNK_HALF_DUPLEX		0x00100000
#define LNK_TX_FLOW_CTL_Y	0x00200000
#define LNK_NEG_ADVANCED	0x00400000
#define LNK_RX_FLOW_CTL_Y	0x00800000
#define LNK_NIC			0x01000000
#define LNK_JAM			0x02000000
#define LNK_JUMBO		0x04000000
#define LNK_ALTEON		0x08000000
#define LNK_NEG_FCTL		0x10000000
#define LNK_NEGOTIATE		0x20000000
#define LNK_ENABLE		0x40000000
#define LNK_UP			0x80000000


/*
 * Event definitions
 */

#define EVT_RING_ENTRIES	256
#define EVT_RING_SIZE	(EVT_RING_ENTRIES * sizeof(struct event))

struct event {
#ifdef __LITTLE_ENDIAN_BITFIELD
	u32	idx:12;
	u32	code:12;
	u32	evt:8;
#else
	u32	evt:8;
	u32	code:12;
	u32	idx:12;
#endif
	u32     pad;
};


/*
 * Events
 */

#define E_FW_RUNNING		0x01
#define E_STATS_UPDATED		0x04

#define E_STATS_UPDATE		0x04

#define E_LNK_STATE		0x06
#define E_C_LINK_UP		0x01
#define E_C_LINK_DOWN		0x02
#define E_C_LINK_10_100		0x03

#define E_ERROR			0x07
#define E_C_ERR_INVAL_CMD	0x01
#define E_C_ERR_UNIMP_CMD	0x02
#define E_C_ERR_BAD_CFG		0x03

#define E_MCAST_LIST		0x08
#define E_C_MCAST_ADDR_ADD	0x01
#define E_C_MCAST_ADDR_DEL	0x02

#define E_RESET_JUMBO_RNG	0x09


/*
 * Commands
 */

#define CMD_RING_ENTRIES	64

struct cmd {
#ifdef __LITTLE_ENDIAN_BITFIELD
	u32	idx:12;
	u32	code:12;
	u32	evt:8;
#else
	u32	evt:8;
	u32	code:12;
	u32	idx:12;
#endif
};


#define C_HOST_STATE		0x01
#define C_C_STACK_UP		0x01
#define C_C_STACK_DOWN		0x02

#define C_FDR_FILTERING		0x02
#define C_C_FDR_FILT_ENABLE	0x01
#define C_C_FDR_FILT_DISABLE	0x02

#define C_SET_RX_PRD_IDX	0x03
#define C_UPDATE_STATS		0x04
#define C_RESET_JUMBO_RNG	0x05
#define C_ADD_MULTICAST_ADDR	0x08
#define C_DEL_MULTICAST_ADDR	0x09

#define C_SET_PROMISC_MODE	0x0a
#define C_C_PROMISC_ENABLE	0x01
#define C_C_PROMISC_DISABLE	0x02

#define C_LNK_NEGOTIATION	0x0b
#define C_C_NEGOTIATE_BOTH	0x00
#define C_C_NEGOTIATE_GIG	0x01
#define C_C_NEGOTIATE_10_100	0x02

#define C_SET_MAC_ADDR		0x0c
#define C_CLEAR_PROFILE		0x0d

#define C_SET_MULTICAST_MODE	0x0e
#define C_C_MCAST_ENABLE	0x01
#define C_C_MCAST_DISABLE	0x02

#define C_CLEAR_STATS		0x0f
#define C_SET_RX_JUMBO_PRD_IDX	0x10
#define C_REFRESH_STATS		0x11


/*
 * Descriptor flags
 */
#define BD_FLG_TCP_UDP_SUM	0x01
#define BD_FLG_IP_SUM		0x02
#define BD_FLG_END		0x04
#define BD_FLG_MORE		0x08
#define BD_FLG_JUMBO		0x10
#define BD_FLG_UCAST		0x20
#define BD_FLG_MCAST		0x40
#define BD_FLG_BCAST		0x60
#define BD_FLG_TYP_MASK		0x60
#define BD_FLG_IP_FRAG		0x80
#define BD_FLG_IP_FRAG_END	0x100
#define BD_FLG_VLAN_TAG		0x200
#define BD_FLG_FRAME_ERROR	0x400
#define BD_FLG_COAL_NOW		0x800
#define BD_FLG_MINI		0x1000


/*
 * Ring Control block flags
 */
#define RCB_FLG_TCP_UDP_SUM	0x01
#define RCB_FLG_IP_SUM		0x02
#define RCB_FLG_NO_PSEUDO_HDR	0x08
#define RCB_FLG_VLAN_ASSIST	0x10
#define RCB_FLG_COAL_INT_ONLY	0x20
#define RCB_FLG_TX_HOST_RING	0x40
#define RCB_FLG_IEEE_SNAP_SUM	0x80
#define RCB_FLG_EXT_RX_BD	0x100
#define RCB_FLG_RNG_DISABLE	0x200


/*
 * TX ring - maximum TX ring entries for Tigon I's is 128
 */
#define MAX_TX_RING_ENTRIES	256
#define TIGON_I_TX_RING_ENTRIES	128
#define TX_RING_SIZE		(MAX_TX_RING_ENTRIES * sizeof(struct tx_desc))
#define TX_RING_BASE		0x3800

struct tx_desc{
        aceaddr	addr;
	u32	flagsize;
#if 0
/*
 * This is in PCI shared mem and must be accessed with readl/writel
 * real layout is:
 */
#if __LITTLE_ENDIAN
	u16	flags;
	u16	size;
	u16	vlan;
	u16	reserved;
#else
	u16	size;
	u16	flags;
	u16	reserved;
	u16	vlan;
#endif
#endif
	u32	vlanres;
};


#define RX_STD_RING_ENTRIES	512
#define RX_STD_RING_SIZE	(RX_STD_RING_ENTRIES * sizeof(struct rx_desc))

#define RX_JUMBO_RING_ENTRIES	256
#define RX_JUMBO_RING_SIZE	(RX_JUMBO_RING_ENTRIES *sizeof(struct rx_desc))

#define RX_MINI_RING_ENTRIES	1024
#define RX_MINI_RING_SIZE	(RX_MINI_RING_ENTRIES *sizeof(struct rx_desc))

#define RX_RETURN_RING_ENTRIES	2048
#define RX_RETURN_RING_SIZE	(RX_MAX_RETURN_RING_ENTRIES * \
				 sizeof(struct rx_desc))

struct rx_desc{
	aceaddr	addr;
#ifdef __LITTLE_ENDIAN
	u16	size;
	u16	idx;
#else
	u16	idx;
	u16	size;
#endif
#ifdef __LITTLE_ENDIAN
	u16	flags;
	u16	type;
#else
	u16	type;
	u16	flags;
#endif
#ifdef __LITTLE_ENDIAN
	u16	tcp_udp_csum;
	u16	ip_csum;
#else
	u16	ip_csum;
	u16	tcp_udp_csum;
#endif
#ifdef __LITTLE_ENDIAN
	u16	vlan;
	u16	err_flags;
#else
	u16	err_flags;
	u16	vlan;
#endif
	u32	reserved;
	u32	opague;
};


/*
 * This struct is shared with the NIC firmware.
 */
struct ring_ctrl {
	aceaddr	rngptr;
#ifdef __LITTLE_ENDIAN
	u16	flags;
	u16	max_len;
#else
	u16	max_len;
	u16	flags;
#endif
	u32	pad;
};


struct ace_mac_stats {
	u32 excess_colls;
	u32 coll_1;
	u32 coll_2;
	u32 coll_3;
	u32 coll_4;
	u32 coll_5;
	u32 coll_6;
	u32 coll_7;
	u32 coll_8;
	u32 coll_9;
	u32 coll_10;
	u32 coll_11;
	u32 coll_12;
	u32 coll_13;
	u32 coll_14;
	u32 coll_15;
	u32 late_coll;
	u32 defers;
	u32 crc_err;
	u32 underrun;
	u32 crs_err;
	u32 pad[3];
	u32 drop_ula;
	u32 drop_mc;
	u32 drop_fc;
	u32 drop_space;
	u32 coll;
	u32 kept_bc;
	u32 kept_mc;
	u32 kept_uc;
};


struct ace_info {
	union {
		u32 stats[256];
	} s;
	struct ring_ctrl	evt_ctrl;
	struct ring_ctrl	cmd_ctrl;
	struct ring_ctrl	tx_ctrl;
	struct ring_ctrl	rx_std_ctrl;
	struct ring_ctrl	rx_jumbo_ctrl;
	struct ring_ctrl	rx_mini_ctrl;
	struct ring_ctrl	rx_return_ctrl;
	aceaddr	evt_prd_ptr;
	aceaddr	rx_ret_prd_ptr;
	aceaddr	tx_csm_ptr;
	aceaddr	stats2_ptr;
};


struct ring_info {
	struct sk_buff		*skb;
	DEFINE_DMA_UNMAP_ADDR(mapping);
};


/*
 * Funny... As soon as we add maplen on alpha, it starts to work
 * much slower. Hmm... is it because struct does not fit to one cacheline?
 * So, split tx_ring_info.
 */
struct tx_ring_info {
	struct sk_buff		*skb;
	DEFINE_DMA_UNMAP_ADDR(mapping);
	DEFINE_DMA_UNMAP_LEN(maplen);
};


/*
 * struct ace_skb holding the rings of skb's. This is an awful lot of
 * pointers, but I don't see any other smart mode to do this in an
 * efficient manner ;-(
 */
struct ace_skb
{
	struct tx_ring_info	tx_skbuff[MAX_TX_RING_ENTRIES];
	struct ring_info	rx_std_skbuff[RX_STD_RING_ENTRIES];
	struct ring_info	rx_mini_skbuff[RX_MINI_RING_ENTRIES];
	struct ring_info	rx_jumbo_skbuff[RX_JUMBO_RING_ENTRIES];
};


/*
 * Struct private for the AceNIC.
 *
 * Elements are grouped so variables used by the tx handling goes
 * together, and will go into the same cache lines etc. in order to
 * avoid cache line contention between the rx and tx handling on SMP.
 *
 * Frequently accessed variables are put at the beginning of the
 * struct to help the compiler generate better/shorter code.
 */
struct ace_private
{
	struct ace_info		*info;
	struct ace_regs	__iomem	*regs;		/* register base */
	struct ace_skb		*skb;
	dma_addr_t		info_dma;	/* 32/64 bit */

	int			version, link;
	int			promisc, mcast_all;

	/*
	 * TX elements
	 */
	struct tx_desc		*tx_ring;
	u32			tx_prd;
	volatile u32		tx_ret_csm;
	int			tx_ring_entries;

	/*
	 * RX elements
	 */
	unsigned long		std_refill_busy
				__attribute__ ((aligned (SMP_CACHE_BYTES)));
	unsigned long		mini_refill_busy, jumbo_refill_busy;
	atomic_t		cur_rx_bufs;
	atomic_t		cur_mini_bufs;
	atomic_t		cur_jumbo_bufs;
	u32			rx_std_skbprd, rx_mini_skbprd, rx_jumbo_skbprd;
	u32			cur_rx;

	struct rx_desc		*rx_std_ring;
	struct rx_desc		*rx_jumbo_ring;
	struct rx_desc		*rx_mini_ring;
	struct rx_desc		*rx_return_ring;

#if ACENIC_DO_VLAN
	struct vlan_group	*vlgrp;
#endif

	int			tasklet_pending, jumbo;
	struct tasklet_struct	ace_tasklet;

	struct event		*evt_ring;

	volatile u32		*evt_prd, *rx_ret_prd, *tx_csm;

	dma_addr_t		tx_ring_dma;	/* 32/64 bit */
	dma_addr_t		rx_ring_base_dma;
	dma_addr_t		evt_ring_dma;
	dma_addr_t		evt_prd_dma, rx_ret_prd_dma, tx_csm_dma;

	unsigned char		*trace_buf;
	struct pci_dev		*pdev;
	struct net_device	*next;
	volatile int		fw_running;
	int			board_idx;
	u16			pci_command;
	u8			pci_latency;
	const char		*name;
#ifdef INDEX_DEBUG
	spinlock_t		debug_lock
				__attribute__ ((aligned (SMP_CACHE_BYTES)));
	u32			last_tx, last_std_rx, last_mini_rx;
#endif
	int			pci_using_dac;
	u8			firmware_major;
	u8			firmware_minor;
	u8			firmware_fix;
	u32			firmware_start;
};


#define TX_RESERVED	MAX_SKB_FRAGS

static inline int tx_space (struct ace_private *ap, u32 csm, u32 prd)
{
	return (csm - prd - 1) & (ACE_TX_RING_ENTRIES(ap) - 1);
}

#define tx_free(ap) 		tx_space((ap)->tx_ret_csm, (ap)->tx_prd, ap)
#define tx_ring_full(ap, csm, prd)	(tx_space(ap, csm, prd) <= TX_RESERVED)

static inline void set_aceaddr(aceaddr *aa, dma_addr_t addr)
{
	u64 baddr = (u64) addr;
	aa->addrlo = baddr & 0xffffffff;
	aa->addrhi = baddr >> 32;
	wmb();
}


static inline void ace_set_txprd(struct ace_regs __iomem *regs,
				 struct ace_private *ap, u32 value)
{
#ifdef INDEX_DEBUG
	unsigned long flags;
	spin_lock_irqsave(&ap->debug_lock, flags);
	writel(value, &regs->TxPrd);
	if (value == ap->last_tx)
		printk(KERN_ERR "AceNIC RACE ALERT! writing identical value "
		       "to tx producer (%i)\n", value);
	ap->last_tx = value;
	spin_unlock_irqrestore(&ap->debug_lock, flags);
#else
	writel(value, &regs->TxPrd);
#endif
	wmb();
}


static inline void ace_mask_irq(struct net_device *dev)
{
	struct ace_private *ap = netdev_priv(dev);
	struct ace_regs __iomem *regs = ap->regs;

	if (ACE_IS_TIGON_I(ap))
		writel(1, &regs->MaskInt);
	else
		writel(readl(&regs->HostCtrl) | MASK_INTS, &regs->HostCtrl);

	ace_sync_irq(dev->irq);
}


static inline void ace_unmask_irq(struct net_device *dev)
{
	struct ace_private *ap = netdev_priv(dev);
	struct ace_regs __iomem *regs = ap->regs;

	if (ACE_IS_TIGON_I(ap))
		writel(0, &regs->MaskInt);
	else
		writel(readl(&regs->HostCtrl) & ~MASK_INTS, &regs->HostCtrl);
}


/*
 * Prototypes
 */
static int ace_init(struct net_device *dev);
static void ace_load_std_rx_ring(struct ace_private *ap, int nr_bufs);
static void ace_load_mini_rx_ring(struct ace_private *ap, int nr_bufs);
static void ace_load_jumbo_rx_ring(struct ace_private *ap, int nr_bufs);
static irqreturn_t ace_interrupt(int irq, void *dev_id);
static int ace_load_firmware(struct net_device *dev);
static int ace_open(struct net_device *dev);
static netdev_tx_t ace_start_xmit(struct sk_buff *skb,
				  struct net_device *dev);
static int ace_close(struct net_device *dev);
static void ace_tasklet(unsigned long dev);
static void ace_dump_trace(struct ace_private *ap);
static void ace_set_multicast_list(struct net_device *dev);
static int ace_change_mtu(struct net_device *dev, int new_mtu);
static int ace_set_mac_addr(struct net_device *dev, void *p);
static void ace_set_rxtx_parms(struct net_device *dev, int jumbo);
static int ace_allocate_descriptors(struct net_device *dev);
static void ace_free_descriptors(struct net_device *dev);
static void ace_init_cleanup(struct net_device *dev);
static struct net_device_stats *ace_get_stats(struct net_device *dev);
static int read_eeprom_byte(struct net_device *dev, unsigned long offset);
#if ACENIC_DO_VLAN
static void ace_vlan_rx_register(struct net_device *dev, struct vlan_group *grp);
#endif

#endif /* _ACENIC_H_ */
