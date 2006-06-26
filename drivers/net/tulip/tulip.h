/*
	drivers/net/tulip/tulip.h

	Copyright 2000,2001  The Linux Kernel Team
	Written/copyright 1994-2001 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	Please refer to Documentation/DocBook/tulip-user.{pdf,ps,html}
	for more information on this driver, or visit the project
	Web page at http://sourceforge.net/projects/tulip/

*/

#ifndef __NET_TULIP_H__
#define __NET_TULIP_H__

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/irq.h>



/* undefine, or define to various debugging levels (>4 == obscene levels) */
#define TULIP_DEBUG 1

/* undefine USE_IO_OPS for MMIO, define for PIO */
#ifdef CONFIG_TULIP_MMIO
# undef USE_IO_OPS
#else
# define USE_IO_OPS 1
#endif



struct tulip_chip_table {
	char *chip_name;
	int io_size;
	int valid_intrs;	/* CSR7 interrupt enable settings */
	int flags;
	void (*media_timer) (unsigned long data);
};


enum tbl_flag {
	HAS_MII			= 0x0001,
	HAS_MEDIA_TABLE		= 0x0002,
	CSR12_IN_SROM		= 0x0004,
	ALWAYS_CHECK_MII	= 0x0008,
	HAS_ACPI		= 0x0010,
	MC_HASH_ONLY		= 0x0020, /* Hash-only multicast filter. */
	HAS_PNICNWAY		= 0x0080,
	HAS_NWAY		= 0x0040, /* Uses internal NWay xcvr. */
	HAS_INTR_MITIGATION	= 0x0100,
	IS_ASIX			= 0x0200,
	HAS_8023X		= 0x0400,
	COMET_MAC_ADDR		= 0x0800,
	HAS_PCI_MWI		= 0x1000,
	HAS_PHY_IRQ		= 0x2000,
	HAS_SWAPPED_SEEPROM	= 0x4000,
	NEEDS_FAKE_MEDIA_TABLE	= 0x8000,
};


/* chip types.  careful!  order is VERY IMPORTANT here, as these
 * are used throughout the driver as indices into arrays */
/* Note 21142 == 21143. */
enum chips {
	DC21040 = 0,
	DC21041 = 1,
	DC21140 = 2,
	DC21142 = 3, DC21143 = 3,
	LC82C168,
	MX98713,
	MX98715,
	MX98725,
	AX88140,
	PNIC2,
	COMET,
	COMPEX9881,
	I21145,
	DM910X,
	CONEXANT,
};


enum MediaIs {
	MediaIsFD = 1,
	MediaAlwaysFD = 2,
	MediaIsMII = 4,
	MediaIsFx = 8,
	MediaIs100 = 16
};


/* Offsets to the Command and Status Registers, "CSRs".  All accesses
   must be longword instructions and quadword aligned. */
enum tulip_offsets {
	CSR0 = 0,
	CSR1 = 0x08,
	CSR2 = 0x10,
	CSR3 = 0x18,
	CSR4 = 0x20,
	CSR5 = 0x28,
	CSR6 = 0x30,
	CSR7 = 0x38,
	CSR8 = 0x40,
	CSR9 = 0x48,
	CSR10 = 0x50,
	CSR11 = 0x58,
	CSR12 = 0x60,
	CSR13 = 0x68,
	CSR14 = 0x70,
	CSR15 = 0x78,
};

/* register offset and bits for CFDD PCI config reg */
enum pci_cfg_driver_reg {
	CFDD = 0x40,
	CFDD_Sleep = (1 << 31),
	CFDD_Snooze = (1 << 30),
};

#define RxPollInt (RxIntr|RxNoBuf|RxDied|RxJabber)

/* The bits in the CSR5 status registers, mostly interrupt sources. */
enum status_bits {
	TimerInt = 0x800,
	SytemError = 0x2000,
	TPLnkFail = 0x1000,
	TPLnkPass = 0x10,
	NormalIntr = 0x10000,
	AbnormalIntr = 0x8000,
	RxJabber = 0x200,
	RxDied = 0x100,
	RxNoBuf = 0x80,
	RxIntr = 0x40,
	TxFIFOUnderflow = 0x20,
	TxJabber = 0x08,
	TxNoBuf = 0x04,
	TxDied = 0x02,
	TxIntr = 0x01,
};

/* bit mask for CSR5 TX/RX process state */
#define CSR5_TS	0x00700000
#define CSR5_RS	0x000e0000

enum tulip_mode_bits {
	TxThreshold		= (1 << 22),
	FullDuplex		= (1 << 9),
	TxOn			= 0x2000,
	AcceptBroadcast		= 0x0100,
	AcceptAllMulticast	= 0x0080,
	AcceptAllPhys		= 0x0040,
	AcceptRunt		= 0x0008,
	RxOn			= 0x0002,
	RxTx			= (TxOn | RxOn),
};


enum tulip_busconfig_bits {
	MWI			= (1 << 24),
	MRL			= (1 << 23),
	MRM			= (1 << 21),
	CALShift		= 14,
	BurstLenShift		= 8,
};


/* The Tulip Rx and Tx buffer descriptors. */
struct tulip_rx_desc {
	s32 status;
	s32 length;
	u32 buffer1;
	u32 buffer2;
};


struct tulip_tx_desc {
	s32 status;
	s32 length;
	u32 buffer1;
	u32 buffer2;		/* We use only buffer 1.  */
};


enum desc_status_bits {
	DescOwned = 0x80000000,
	RxDescFatalErr = 0x8000,
	RxWholePkt = 0x0300,
};


enum t21143_csr6_bits {
	csr6_sc = (1<<31),
	csr6_ra = (1<<30),
	csr6_ign_dest_msb = (1<<26),
	csr6_mbo = (1<<25),
	csr6_scr = (1<<24),  /* scramble mode flag: can't be set */
	csr6_pcs = (1<<23),  /* Enables PCS functions (symbol mode requires csr6_ps be set) default is set */
	csr6_ttm = (1<<22),  /* Transmit Threshold Mode, set for 10baseT, 0 for 100BaseTX */
	csr6_sf = (1<<21),   /* Store and forward. If set ignores TR bits */
	csr6_hbd = (1<<19),  /* Heart beat disable. Disables SQE function in 10baseT */
	csr6_ps = (1<<18),   /* Port Select. 0 (defualt) = 10baseT, 1 = 100baseTX: can't be set */
	csr6_ca = (1<<17),   /* Collision Offset Enable. If set uses special algorithm in low collision situations */
	csr6_trh = (1<<15),  /* Transmit Threshold high bit */
	csr6_trl = (1<<14),  /* Transmit Threshold low bit */

	/***************************************************************
	 * This table shows transmit threshold values based on media   *
	 * and these two registers (from PNIC1 & 2 docs) Note: this is *
	 * all meaningless if sf is set.                               *
	 ***************************************************************/

	/***********************************
	 * (trh,trl) * 100BaseTX * 10BaseT *
	 ***********************************
	 *   (0,0)   *     128   *    72   *
	 *   (0,1)   *     256   *    96   *
	 *   (1,0)   *     512   *   128   *
	 *   (1,1)   *    1024   *   160   *
	 ***********************************/

	csr6_fc = (1<<12),   /* Forces a collision in next transmission (for testing in loopback mode) */
	csr6_om_int_loop = (1<<10), /* internal (FIFO) loopback flag */
	csr6_om_ext_loop = (1<<11), /* external (PMD) loopback flag */
	/* set both and you get (PHY) loopback */
	csr6_fd = (1<<9),    /* Full duplex mode, disables hearbeat, no loopback */
	csr6_pm = (1<<7),    /* Pass All Multicast */
	csr6_pr = (1<<6),    /* Promiscuous mode */
	csr6_sb = (1<<5),    /* Start(1)/Stop(0) backoff counter */
	csr6_if = (1<<4),    /* Inverse Filtering, rejects only addresses in address table: can't be set */
	csr6_pb = (1<<3),    /* Pass Bad Frames, (1) causes even bad frames to be passed on */
	csr6_ho = (1<<2),    /* Hash-only filtering mode: can't be set */
	csr6_hp = (1<<0),    /* Hash/Perfect Receive Filtering Mode: can't be set */

	csr6_mask_capture = (csr6_sc | csr6_ca),
	csr6_mask_defstate = (csr6_mask_capture | csr6_mbo),
	csr6_mask_hdcap = (csr6_mask_defstate | csr6_hbd | csr6_ps),
	csr6_mask_hdcaptt = (csr6_mask_hdcap  | csr6_trh | csr6_trl),
	csr6_mask_fullcap = (csr6_mask_hdcaptt | csr6_fd),
	csr6_mask_fullpromisc = (csr6_pr | csr6_pm),
	csr6_mask_filters = (csr6_hp | csr6_ho | csr6_if),
	csr6_mask_100bt = (csr6_scr | csr6_pcs | csr6_hbd),
};


/* Keep the ring sizes a power of two for efficiency.
   Making the Tx ring too large decreases the effectiveness of channel
   bonding and packet priority.
   There are no ill effects from too-large receive rings. */

#define TX_RING_SIZE	32
#define RX_RING_SIZE	128
#define MEDIA_MASK     31

#define PKT_BUF_SZ		1536	/* Size of each temporary Rx buffer. */

#define TULIP_MIN_CACHE_LINE	8	/* in units of 32-bit words */

#if defined(__sparc__) || defined(__hppa__)
/* The UltraSparc PCI controllers will disconnect at every 64-byte
 * crossing anyways so it makes no sense to tell Tulip to burst
 * any more than that.
 */
#define TULIP_MAX_CACHE_LINE	16	/* in units of 32-bit words */
#else
#define TULIP_MAX_CACHE_LINE	32	/* in units of 32-bit words */
#endif


/* Ring-wrap flag in length field, use for last ring entry.
	0x01000000 means chain on buffer2 address,
	0x02000000 means use the ring start address in CSR2/3.
   Note: Some work-alike chips do not function correctly in chained mode.
   The ASIX chip works only in chained mode.
   Thus we indicates ring mode, but always write the 'next' field for
   chained mode as well.
*/
#define DESC_RING_WRAP 0x02000000


#define EEPROM_SIZE 512 	/* 2 << EEPROM_ADDRLEN */


#define RUN_AT(x) (jiffies + (x))

#if defined(__i386__)			/* AKA get_unaligned() */
#define get_u16(ptr) (*(u16 *)(ptr))
#else
#define get_u16(ptr) (((u8*)(ptr))[0] + (((u8*)(ptr))[1]<<8))
#endif

struct medialeaf {
	u8 type;
	u8 media;
	unsigned char *leafdata;
};


struct mediatable {
	u16 defaultmedia;
	u8 leafcount;
	u8 csr12dir;		/* General purpose pin directions. */
	unsigned has_mii:1;
	unsigned has_nonmii:1;
	unsigned has_reset:6;
	u32 csr15dir;
	u32 csr15val;		/* 21143 NWay setting. */
	struct medialeaf mleaf[0];
};


struct mediainfo {
	struct mediainfo *next;
	int info_type;
	int index;
	unsigned char *info;
};

struct ring_info {
	struct sk_buff	*skb;
	dma_addr_t	mapping;
};


struct tulip_private {
	const char *product_name;
	struct net_device *next_module;
	struct tulip_rx_desc *rx_ring;
	struct tulip_tx_desc *tx_ring;
	dma_addr_t rx_ring_dma;
	dma_addr_t tx_ring_dma;
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct ring_info tx_buffers[TX_RING_SIZE];
	/* The addresses of receive-in-place skbuffs. */
	struct ring_info rx_buffers[RX_RING_SIZE];
	u16 setup_frame[96];	/* Pseudo-Tx frame to init address table. */
	int chip_id;
	int revision;
	int flags;
	struct net_device_stats stats;
	struct timer_list timer;	/* Media selection timer. */
	struct timer_list oom_timer;    /* Out of memory timer. */
	u32 mc_filter[2];
	spinlock_t lock;
	spinlock_t mii_lock;
	unsigned int cur_rx, cur_tx;	/* The next free ring entry */
	unsigned int dirty_rx, dirty_tx;	/* The ring entries to be free()ed. */

#ifdef 	CONFIG_TULIP_NAPI_HW_MITIGATION
        int mit_on;
#endif
	unsigned int full_duplex:1;	/* Full-duplex operation requested. */
	unsigned int full_duplex_lock:1;
	unsigned int fake_addr:1;	/* Multiport board faked address. */
	unsigned int default_port:4;	/* Last dev->if_port value. */
	unsigned int media2:4;	/* Secondary monitored media port. */
	unsigned int medialock:1;	/* Don't sense media type. */
	unsigned int mediasense:1;	/* Media sensing in progress. */
	unsigned int nway:1, nwayset:1;		/* 21143 internal NWay. */
	unsigned int csr0;	/* CSR0 setting. */
	unsigned int csr6;	/* Current CSR6 control settings. */
	unsigned char eeprom[EEPROM_SIZE];	/* Serial EEPROM contents. */
	void (*link_change) (struct net_device * dev, int csr5);
	u16 sym_advertise, mii_advertise; /* NWay capabilities advertised.  */
	u16 lpar;		/* 21143 Link partner ability. */
	u16 advertising[4];
	signed char phys[4], mii_cnt;	/* MII device addresses. */
	struct mediatable *mtable;
	int cur_index;		/* Current media index. */
	int saved_if_port;
	struct pci_dev *pdev;
	int ttimer;
	int susp_rx;
	unsigned long nir;
	void __iomem *base_addr;
	int csr12_shadow;
	int pad0;		/* Used for 8-byte alignment */
};


struct eeprom_fixup {
	char *name;
	unsigned char addr0;
	unsigned char addr1;
	unsigned char addr2;
	u16 newtable[32];	/* Max length below. */
};


/* 21142.c */
extern u16 t21142_csr14[];
void t21142_timer(unsigned long data);
void t21142_start_nway(struct net_device *dev);
void t21142_lnk_change(struct net_device *dev, int csr5);


/* PNIC2.c */
void pnic2_lnk_change(struct net_device *dev, int csr5);
void pnic2_timer(unsigned long data);
void pnic2_start_nway(struct net_device *dev);
void pnic2_lnk_change(struct net_device *dev, int csr5);

/* eeprom.c */
void tulip_parse_eeprom(struct net_device *dev);
int tulip_read_eeprom(struct net_device *dev, int location, int addr_len);

/* interrupt.c */
extern unsigned int tulip_max_interrupt_work;
extern int tulip_rx_copybreak;
irqreturn_t tulip_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
int tulip_refill_rx(struct net_device *dev);
#ifdef CONFIG_TULIP_NAPI
int tulip_poll(struct net_device *dev, int *budget);
#endif


/* media.c */
int tulip_mdio_read(struct net_device *dev, int phy_id, int location);
void tulip_mdio_write(struct net_device *dev, int phy_id, int location, int value);
void tulip_select_media(struct net_device *dev, int startup);
int tulip_check_duplex(struct net_device *dev);
void tulip_find_mii (struct net_device *dev, int board_idx);

/* pnic.c */
void pnic_do_nway(struct net_device *dev);
void pnic_lnk_change(struct net_device *dev, int csr5);
void pnic_timer(unsigned long data);

/* timer.c */
void tulip_timer(unsigned long data);
void mxic_timer(unsigned long data);
void comet_timer(unsigned long data);

/* tulip_core.c */
extern int tulip_debug;
extern const char * const medianame[];
extern const char tulip_media_cap[];
extern struct tulip_chip_table tulip_tbl[];
void oom_timer(unsigned long data);
extern u8 t21040_csr13[];

static inline void tulip_start_rxtx(struct tulip_private *tp)
{
	void __iomem *ioaddr = tp->base_addr;
	iowrite32(tp->csr6 | RxTx, ioaddr + CSR6);
	barrier();
	(void) ioread32(ioaddr + CSR6); /* mmio sync */
}

static inline void tulip_stop_rxtx(struct tulip_private *tp)
{
	void __iomem *ioaddr = tp->base_addr;
	u32 csr6 = ioread32(ioaddr + CSR6);

	if (csr6 & RxTx) {
		unsigned i=1300/10;
		iowrite32(csr6 & ~RxTx, ioaddr + CSR6);
		barrier();
		/* wait until in-flight frame completes.
		 * Max time @ 10BT: 1500*8b/10Mbps == 1200us (+ 100us margin)
		 * Typically expect this loop to end in < 50 us on 100BT.
		 */
		while (--i && (ioread32(ioaddr + CSR5) & (CSR5_TS|CSR5_RS)))
			udelay(10);

		if (!i)
			printk(KERN_DEBUG "%s: tulip_stop_rxtx() failed\n",
					pci_name(tp->pdev));
	}
}

static inline void tulip_restart_rxtx(struct tulip_private *tp)
{
	tulip_stop_rxtx(tp);
	udelay(5);
	tulip_start_rxtx(tp);
}

#endif /* __NET_TULIP_H__ */
