/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RRUNNER_H_
#define _RRUNNER_H_

#include <linux/interrupt.h>

#if ((BITS_PER_LONG != 32) && (BITS_PER_LONG != 64))
#error "BITS_PER_LONG not defined or not valid"
#endif


struct rr_regs {

	u32	pad0[16];

	u32	HostCtrl;
	u32	LocalCtrl;
	u32	Pc;
	u32	BrkPt;

/* Timer increments every 0.97 micro-seconds (unsigned int) */
	u32	Timer_Hi;
	u32	Timer;
	u32	TimerRef;
	u32	PciState;

	u32	Event;
	u32	MbEvent;

	u32	WinBase;
	u32	WinData;
	u32	RX_state;
	u32	TX_state;

	u32	Overhead;
	u32	ExtIo;

	u32	DmaWriteHostHi;
	u32	DmaWriteHostLo;

	u32	pad1[2];

	u32	DmaReadHostHi;
	u32	DmaReadHostLo;

	u32	pad2;

	u32	DmaReadLen;
	u32	DmaWriteState;

	u32	DmaWriteLcl;
	u32	DmaWriteIPchecksum;
	u32	DmaWriteLen;
	u32	DmaReadState;
	u32	DmaReadLcl;
	u32	DmaReadIPchecksum;
	u32	pad3;

	u32	RxBase;
	u32	RxPrd;
	u32	RxCon;

	u32	pad4;

	u32	TxBase;
	u32	TxPrd;
	u32	TxCon;

	u32	pad5;

	u32	RxIndPro;
	u32	RxIndCon;
	u32	RxIndRef;

	u32	pad6;

	u32	TxIndPro;
	u32	TxIndCon;
	u32	TxIndRef;

	u32	pad7[17];

	u32	DrCmndPro;
	u32	DrCmndCon;
	u32	DrCmndRef;

	u32	pad8;

	u32	DwCmndPro;
	u32	DwCmndCon;
	u32	DwCmndRef;

	u32	AssistState;

	u32	DrDataPro;
	u32	DrDataCon;
	u32	DrDataRef;

	u32	pad9;

	u32	DwDataPro;
	u32	DwDataCon;
	u32	DwDataRef;

	u32	pad10[33];

	u32	EvtCon;

	u32	pad11[5];

	u32	TxPi;
	u32	IpRxPi;

	u32	pad11a[8];

	u32	CmdRing[16];

/* The ULA is in two registers the high order two bytes of the first
 * word contain the RunCode features.
 * ula0		res	res	byte0	byte1
 * ula1		byte2	byte3	byte4	byte5
 */
	u32	Ula0;
	u32	Ula1;

	u32	RxRingHi;
	u32	RxRingLo;

	u32	InfoPtrHi;
	u32	InfoPtrLo;

	u32	Mode;

	u32	ConRetry;
	u32	ConRetryTmr;

	u32	ConTmout;
	u32	CtatTmr;

	u32	MaxRxRng;

	u32	IntrTmr;
	u32	TxDataMvTimeout;
	u32	RxDataMvTimeout;

	u32	EvtPrd;
	u32	TraceIdx;

	u32	Fail1;
	u32	Fail2;

	u32	DrvPrm;

	u32	FilterLA;

	u32	FwRev;
	u32	FwRes1;
	u32	FwRes2;
	u32	FwRes3;

	u32	WriteDmaThresh;
	u32	ReadDmaThresh;

	u32	pad12[325];
	u32	Window[512];
};

/*
 * Host control register bits.
 */

#define RR_INT		0x01
#define RR_CLEAR_INT	0x02
#define NO_SWAP		0x04000004
#define NO_SWAP1	0x00000004
#define PCI_RESET_NIC	0x08
#define HALT_NIC	0x10
#define SSTEP_NIC	0x20
#define MEM_READ_MULTI	0x40
#define NIC_HALTED	0x100
#define HALT_INST	0x200
#define PARITY_ERR	0x400
#define INVALID_INST_B	0x800
#define RR_REV_2	0x20000000
#define RR_REV_MASK	0xf0000000

/*
 * Local control register bits.
 */

#define INTA_STATE		0x01
#define CLEAR_INTA		0x02
#define FAST_EEPROM_ACCESS	0x08
#define ENABLE_EXTRA_SRAM	0x100
#define ENABLE_EXTRA_DESC	0x200
#define ENABLE_PARITY		0x400
#define FORCE_DMA_PARITY_ERROR	0x800
#define ENABLE_EEPROM_WRITE	0x1000
#define ENABLE_DATA_CACHE	0x2000
#define SRAM_LO_PARITY_ERR	0x4000
#define SRAM_HI_PARITY_ERR	0x8000

/*
 * PCI state bits.
 */

#define FORCE_PCI_RESET		0x01
#define PROVIDE_LENGTH		0x02
#define MASK_DMA_READ_MAX	0x1C
#define RBURST_DISABLE		0x00
#define RBURST_4		0x04
#define RBURST_16		0x08
#define RBURST_32		0x0C
#define RBURST_64		0x10
#define RBURST_128		0x14
#define RBURST_256		0x18
#define RBURST_1024		0x1C
#define MASK_DMA_WRITE_MAX	0xE0
#define WBURST_DISABLE		0x00
#define WBURST_4		0x20
#define WBURST_16		0x40
#define WBURST_32		0x60
#define WBURST_64		0x80
#define WBURST_128		0xa0
#define WBURST_256		0xc0
#define WBURST_1024		0xe0
#define MASK_MIN_DMA		0xFF00
#define FIFO_RETRY_ENABLE	0x10000

/*
 * Event register
 */

#define DMA_WRITE_DONE		0x10000
#define DMA_READ_DONE		0x20000
#define DMA_WRITE_ERR		0x40000
#define DMA_READ_ERR		0x80000

/*
 * Receive state
 *
 * RoadRunner HIPPI Receive State Register controls and monitors the
 * HIPPI receive interface in the NIC. Look at err bits when a HIPPI
 * receive Error Event occurs.
 */

#define ENABLE_NEW_CON		0x01
#define RESET_RECV		0x02
#define RECV_ALL		0x00
#define RECV_1K			0x20
#define RECV_2K			0x40
#define RECV_4K			0x60
#define RECV_8K			0x80
#define RECV_16K		0xa0
#define RECV_32K		0xc0
#define RECV_64K		0xe0

/*
 * Transmit status.
 */

#define ENA_XMIT		0x01
#define PERM_CON		0x02

/*
 * DMA write state
 */

#define RESET_DMA		0x01
#define NO_SWAP_DMA		0x02
#define DMA_ACTIVE		0x04
#define THRESH_MASK		0x1F
#define DMA_ERROR_MASK		0xff000000

/*
 * Gooddies stored in the ULA registers.
 */

#define TRACE_ON_WHAT_BIT	0x00020000    /* Traces on */
#define ONEM_BUF_WHAT_BIT	0x00040000    /* 1Meg vs 256K */
#define CHAR_API_WHAT_BIT	0x00080000    /* Char API vs network only */
#define CMD_EVT_WHAT_BIT	0x00200000    /* Command event */
#define LONG_TX_WHAT_BIT	0x00400000
#define LONG_RX_WHAT_BIT	0x00800000
#define WHAT_BIT_MASK		0xFFFD0000    /* Feature bit mask */

/*
 * Mode status
 */

#define EVENT_OVFL		0x80000000
#define FATAL_ERR		0x40000000
#define LOOP_BACK		0x01
#define MODE_PH			0x02
#define MODE_FP			0x00
#define PTR64BIT		0x04
#define PTR32BIT		0x00
#define PTR_WD_SWAP		0x08
#define PTR_WD_NOSWAP		0x00
#define POST_WARN_EVENT		0x10
#define ERR_TERM		0x20
#define DIRECT_CONN		0x40
#define NO_NIC_WATCHDOG		0x80
#define SWAP_DATA		0x100
#define SWAP_CONTROL		0x200
#define NIC_HALT_ON_ERR		0x400
#define NIC_NO_RESTART		0x800
#define HALF_DUP_TX		0x1000
#define HALF_DUP_RX		0x2000


/*
 * Error codes
 */

/* Host Error Codes - values of fail1 */
#define ERR_UNKNOWN_MBOX	0x1001
#define ERR_UNKNOWN_CMD		0x1002
#define ERR_MAX_RING		0x1003
#define ERR_RING_CLOSED		0x1004
#define ERR_RING_OPEN		0x1005
/* Firmware internal errors */
#define ERR_EVENT_RING_FULL	0x01
#define ERR_DW_PEND_CMND_FULL	0x02
#define ERR_DR_PEND_CMND_FULL	0x03
#define ERR_DW_PEND_DATA_FULL	0x04
#define ERR_DR_PEND_DATA_FULL	0x05
#define ERR_ILLEGAL_JUMP	0x06
#define ERR_UNIMPLEMENTED	0x07
#define ERR_TX_INFO_FULL	0x08
#define ERR_RX_INFO_FULL	0x09
#define ERR_ILLEGAL_MODE	0x0A
#define ERR_MAIN_TIMEOUT	0x0B
#define ERR_EVENT_BITS		0x0C
#define ERR_UNPEND_FULL		0x0D
#define ERR_TIMER_QUEUE_FULL	0x0E
#define ERR_TIMER_QUEUE_EMPTY	0x0F
#define ERR_TIMER_NO_FREE	0x10
#define ERR_INTR_START		0x11
#define ERR_BAD_STARTUP		0x12
#define ERR_NO_PKT_END		0x13
#define ERR_HALTED_ON_ERR	0x14
/* Hardware NIC Errors */
#define ERR_WRITE_DMA		0x0101
#define ERR_READ_DMA		0x0102
#define ERR_EXT_SERIAL		0x0103
#define ERR_TX_INT_PARITY	0x0104


/*
 * Event definitions
 */

#define EVT_RING_ENTRIES	64
#define EVT_RING_SIZE		(EVT_RING_ENTRIES * sizeof(struct event))

struct event {
#ifdef __LITTLE_ENDIAN
	u16     index;
	u8      ring;
	u8      code;
#else
	u8      code;
	u8      ring;
	u16     index;
#endif
	u32     timestamp;
};

/*
 * General Events
 */

#define E_NIC_UP	0x01
#define E_WATCHDOG	0x02

#define E_STAT_UPD	0x04
#define E_INVAL_CMD	0x05
#define E_SET_CMD_CONS	0x06
#define E_LINK_ON	0x07
#define E_LINK_OFF	0x08
#define E_INTERN_ERR	0x09
#define E_HOST_ERR	0x0A
#define E_STATS_UPDATE	0x0B
#define E_REJECTING	0x0C

/*
 * Send  Events
 */
#define E_CON_REJ	0x13
#define E_CON_TMOUT	0x14
#define E_CON_NC_TMOUT	0x15	/* I  , Connection No Campon Timeout */
#define E_DISC_ERR	0x16
#define E_INT_PRTY	0x17
#define E_TX_IDLE	0x18
#define E_TX_LINK_DROP	0x19
#define E_TX_INV_RNG	0x1A
#define E_TX_INV_BUF	0x1B
#define E_TX_INV_DSC	0x1C

/*
 * Destination Events
 */
/*
 * General Receive events
 */
#define E_VAL_RNG	0x20
#define E_RX_RNG_ENER	0x21
#define E_INV_RNG	0x22
#define E_RX_RNG_SPC	0x23
#define E_RX_RNG_OUT	0x24
#define E_PKT_DISCARD	0x25
#define E_INFO_EVT	0x27

/*
 * Data corrupted events
 */
#define E_RX_PAR_ERR	0x2B
#define E_RX_LLRC_ERR	0x2C
#define E_IP_CKSM_ERR	0x2D
#define E_DTA_CKSM_ERR	0x2E
#define E_SHT_BST	0x2F

/*
 * Data lost events
 */
#define E_LST_LNK_ERR	0x30
#define E_FLG_SYN_ERR	0x31
#define E_FRM_ERR	0x32
#define E_RX_IDLE	0x33
#define E_PKT_LN_ERR	0x34
#define E_STATE_ERR	0x35
#define E_UNEXP_DATA	0x3C

/*
 * Fatal events
 */
#define E_RX_INV_BUF	0x36
#define E_RX_INV_DSC	0x37
#define E_RNG_BLK	0x38

/*
 * Warning events
 */
#define E_RX_TO		0x39
#define E_BFR_SPC	0x3A
#define E_INV_ULP	0x3B

#define E_NOT_IMPLEMENTED 0x40


/*
 * Commands
 */

#define CMD_RING_ENTRIES	16

struct cmd {
#ifdef __LITTLE_ENDIAN
	u16     index;
	u8      ring;
	u8      code;
#else
	u8      code;
	u8      ring;
	u16     index;
#endif
};

#define C_START_FW	0x01
#define C_UPD_STAT	0x02
#define C_WATCHDOG	0x05
#define C_DEL_RNG	0x09
#define C_NEW_RNG	0x0A
#define C_CONN		0x0D


/*
 * Mode bits
 */

#define  PACKET_BAD		0x01 /* Packet had link-layer error */
#define  INTERRUPT		0x02
#define  TX_IP_CKSUM		0x04
#define  PACKET_END		0x08
#define  PACKET_START		0x10
#define  SAME_IFIELD		0x80


typedef struct {
#if (BITS_PER_LONG == 64)
	u64 addrlo;
#else
	u32 addrhi;
	u32 addrlo;
#endif
} rraddr;


static inline void set_rraddr(rraddr *ra, dma_addr_t addr)
{
	unsigned long baddr = addr;
#if (BITS_PER_LONG == 64)
	ra->addrlo = baddr;
#else
    /* Don't bother setting zero every time */
	ra->addrlo = baddr;
#endif
	mb();
}


static inline void set_rxaddr(struct rr_regs __iomem *regs, volatile dma_addr_t addr)
{
	unsigned long baddr = addr;
#if (BITS_PER_LONG == 64) && defined(__LITTLE_ENDIAN)
	writel(baddr & 0xffffffff, &regs->RxRingHi);
	writel(baddr >> 32, &regs->RxRingLo);
#elif (BITS_PER_LONG == 64)
	writel(baddr >> 32, &regs->RxRingHi);
	writel(baddr & 0xffffffff, &regs->RxRingLo);
#else
	writel(0, &regs->RxRingHi);
	writel(baddr, &regs->RxRingLo);
#endif
	mb();
}


static inline void set_infoaddr(struct rr_regs __iomem *regs, volatile dma_addr_t addr)
{
	unsigned long baddr = addr;
#if (BITS_PER_LONG == 64) && defined(__LITTLE_ENDIAN)
	writel(baddr & 0xffffffff, &regs->InfoPtrHi);
	writel(baddr >> 32, &regs->InfoPtrLo);
#elif (BITS_PER_LONG == 64)
	writel(baddr >> 32, &regs->InfoPtrHi);
	writel(baddr & 0xffffffff, &regs->InfoPtrLo);
#else
	writel(0, &regs->InfoPtrHi);
	writel(baddr, &regs->InfoPtrLo);
#endif
	mb();
}


/*
 * TX ring
 */

#ifdef CONFIG_ROADRUNNER_LARGE_RINGS
#define TX_RING_ENTRIES	32
#else
#define TX_RING_ENTRIES	16
#endif
#define TX_TOTAL_SIZE	(TX_RING_ENTRIES * sizeof(struct tx_desc))

struct tx_desc{
	rraddr	addr;
	u32	res;
#ifdef __LITTLE_ENDIAN
	u16	size;
	u8	pad;
	u8	mode;
#else
	u8	mode;
	u8	pad;
	u16	size;
#endif
};


#ifdef CONFIG_ROADRUNNER_LARGE_RINGS
#define RX_RING_ENTRIES	32
#else
#define RX_RING_ENTRIES 16
#endif
#define RX_TOTAL_SIZE	(RX_RING_ENTRIES * sizeof(struct rx_desc))

struct rx_desc{
	rraddr	addr;
	u32	res;
#ifdef __LITTLE_ENDIAN
	u16	size;
	u8	pad;
	u8	mode;
#else
	u8	mode;
	u8	pad;
	u16	size;
#endif
};


/*
 * ioctl's
 */

#define SIOCRRPFW	SIOCDEVPRIVATE		/* put firmware */
#define SIOCRRGFW	SIOCDEVPRIVATE+1	/* get firmware */
#define SIOCRRID	SIOCDEVPRIVATE+2	/* identify */


struct seg_hdr {
	u32	seg_start;
	u32	seg_len;
	u32	seg_eestart;
};


#define EEPROM_BASE 0x80000000
#define EEPROM_WORDS 8192
#define EEPROM_BYTES (EEPROM_WORDS * sizeof(u32))

struct eeprom_boot {
	u32	key1;
	u32	key2;
	u32	sram_size;
	struct	seg_hdr loader;
	u32	init_chksum;
	u32	reserved1;
};

struct eeprom_manf {
	u32	HeaderFmt;
	u32	Firmware;
	u32	BoardRevision;
	u32	RoadrunnerRev;
	char	OpticsPart[8];
	u32	OpticsRev;
	u32	pad1;
	char	SramPart[8];
	u32	SramRev;
	u32	pad2;
	char	EepromPart[8];
	u32	EepromRev;
	u32	EepromSize;
	char	PalPart[8];
	u32	PalRev;
	u32	pad3;
	char	PalCodeFile[12];
	u32	PalCodeRev;
	char	BoardULA[8];
	char	SerialNo[8];
	char	MfgDate[8];
	char	MfgTime[8];
	char	ModifyDate[8];
	u32	ModCount;
	u32	pad4[13];
};


struct eeprom_phase_info {
	char	phase1File[12];
	u32	phase1Rev;
	char	phase1Date[8];
	char	phase2File[12];
	u32	phase2Rev;
	char	phase2Date[8];
	u32	reserved7[4];
};

struct eeprom_rncd_info {
	u32	FwStart;
	u32	FwRev;
	char	FwDate[8];
	u32	AddrRunCodeSegs;
	u32	FileNames;
	char	File[13][8];
};


/* Phase 1 region (starts are word offset 0x80) */
struct phase1_hdr{
	u32	jump;
	u32	noop;
	struct seg_hdr phase2Seg;
};

struct eeprom {
	struct eeprom_boot	boot;
	u32			pad1[8];
	struct eeprom_manf	manf;
	struct eeprom_phase_info phase_info;
	struct eeprom_rncd_info	rncd_info;
	u32			pad2[15];
	u32			hdr_checksum;
	struct phase1_hdr	phase1;
};


struct rr_stats {
	u32	NicTimeStamp;
	u32	RngCreated;
	u32	RngDeleted;
	u32	IntrGen;
	u32	NEvtOvfl;
	u32	InvCmd;
	u32	DmaReadErrs;
	u32	DmaWriteErrs;
	u32	StatUpdtT;
	u32	StatUpdtC;
	u32	WatchDog;
	u32	Trace;

	/* Serial HIPPI */
	u32	LnkRdyEst;
	u32	GLinkErr;
	u32	AltFlgErr;
	u32	OvhdBit8Sync;
	u32	RmtSerPrtyErr;
	u32	RmtParPrtyErr;
	u32	RmtLoopBk;
	u32	pad1;

	/* HIPPI tx */
	u32	ConEst;
	u32	ConRejS;
	u32	ConRetry;
	u32	ConTmOut;
	u32	SndConDiscon;
	u32	SndParErr;
	u32	PktSnt;
	u32	pad2[2];
	u32	ShFBstSnt;
	u64	BytSent;
	u32	TxTimeout;
	u32	pad3[3];

	/* HIPPI rx */
	u32	ConAcc;
	u32	ConRejdiPrty;
	u32	ConRejd64b;
	u32	ConRejdBuf;
	u32	RxConDiscon;
	u32	RxConNoData;
	u32	PktRx;
	u32	pad4[2];
	u32	ShFBstRx;
	u64	BytRx;
	u32	RxParErr;
	u32	RxLLRCerr;
	u32	RxBstSZerr;
	u32	RxStateErr;
	u32	RxRdyErr;
	u32	RxInvULP;
	u32	RxSpcBuf;
	u32	RxSpcDesc;
	u32	RxRngSpc;
	u32	RxRngFull;
	u32	RxPktLenErr;
	u32	RxCksmErr;
	u32	RxPktDrp;
	u32	RngLowSpc;
	u32	RngDataClose;
	u32	RxTimeout;
	u32	RxIdle;
};


/*
 * This struct is shared with the NIC firmware.
 */
struct ring_ctrl {
	rraddr	rngptr;
#ifdef __LITTLE_ENDIAN
	u16	entries;
	u8	pad;
	u8	entry_size;
	u16	pi;
	u16	mode;
#else
	u8	entry_size;
	u8	pad;
	u16	entries;
	u16	mode;
	u16	pi;
#endif
};

struct rr_info {
	union {
		struct rr_stats stats;
		u32 stati[128];
	} s;
	struct ring_ctrl	evt_ctrl;
	struct ring_ctrl	cmd_ctrl;
	struct ring_ctrl	tx_ctrl;
	u8			pad[464];
	u8			trace[3072];
};

/*
 * The linux structure for the RoadRunner.
 *
 * RX/TX descriptors are put first to make sure they are properly
 * aligned and do not cross cache-line boundaries.
 */

struct rr_private
{
	struct rx_desc		*rx_ring;
	struct tx_desc		*tx_ring;
	struct event		*evt_ring;
	dma_addr_t 		tx_ring_dma;
	dma_addr_t 		rx_ring_dma;
	dma_addr_t 		evt_ring_dma;
	/* Alignment ok ? */
	struct sk_buff		*rx_skbuff[RX_RING_ENTRIES];
	struct sk_buff		*tx_skbuff[TX_RING_ENTRIES];
	struct rr_regs		__iomem *regs;		/* Register base */
	struct ring_ctrl	*rx_ctrl;	/* Receive ring control */
	struct rr_info		*info;		/* Shared info page */
	dma_addr_t 		rx_ctrl_dma;
	dma_addr_t 		info_dma;
	spinlock_t		lock;
	struct timer_list	timer;
	u32			cur_rx, cur_cmd, cur_evt;
	u32			dirty_rx, dirty_tx;
	u32			tx_full;
	u32			fw_rev;
	volatile short		fw_running;
	struct pci_dev		*pci_dev;
};


/*
 * Prototypes
 */
static int rr_init(struct net_device *dev);
static int rr_init1(struct net_device *dev);
static irqreturn_t rr_interrupt(int irq, void *dev_id);

static int rr_open(struct net_device *dev);
static netdev_tx_t rr_start_xmit(struct sk_buff *skb,
				 struct net_device *dev);
static int rr_close(struct net_device *dev);
static int rr_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static unsigned int rr_read_eeprom(struct rr_private *rrpriv,
				   unsigned long offset,
				   unsigned char *buf,
				   unsigned long length);
static u32 rr_read_eeprom_word(struct rr_private *rrpriv, size_t offset);
static int rr_load_firmware(struct net_device *dev);
static inline void rr_raz_tx(struct rr_private *, struct net_device *);
static inline void rr_raz_rx(struct rr_private *, struct net_device *);
#endif /* _RRUNNER_H_ */
