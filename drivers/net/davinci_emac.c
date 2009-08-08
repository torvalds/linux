/*
 * DaVinci Ethernet Medium Access Controller
 *
 * DaVinci EMAC is based upon CPPI 3.0 TI DMA engine
 *
 * Copyright (C) 2009 Texas Instruments.
 *
 * ---------------------------------------------------------------------------
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * ---------------------------------------------------------------------------
 * History:
 * 0-5 A number of folks worked on this driver in bits and pieces but the major
 *     contribution came from Suraj Iyer and Anant Gole
 * 6.0 Anant Gole - rewrote the driver as per Linux conventions
 * 6.1 Chaithrika U S - added support for Gigabit and RMII features,
 *     PHY layer usage
 */

/** Pending Items in this driver:
 * 1. Use Linux cache infrastcture for DMA'ed memory (dma_xxx functions)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/highmem.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/semaphore.h>
#include <linux/phy.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#include <asm/irq.h>
#include <asm/page.h>

#include <mach/emac.h>

static int debug_level;
module_param(debug_level, int, 0);
MODULE_PARM_DESC(debug_level, "DaVinci EMAC debug level (NETIF_MSG bits)");

/* Netif debug messages possible */
#define DAVINCI_EMAC_DEBUG	(NETIF_MSG_DRV | \
				NETIF_MSG_PROBE | \
				NETIF_MSG_LINK | \
				NETIF_MSG_TIMER | \
				NETIF_MSG_IFDOWN | \
				NETIF_MSG_IFUP | \
				NETIF_MSG_RX_ERR | \
				NETIF_MSG_TX_ERR | \
				NETIF_MSG_TX_QUEUED | \
				NETIF_MSG_INTR | \
				NETIF_MSG_TX_DONE | \
				NETIF_MSG_RX_STATUS | \
				NETIF_MSG_PKTDATA | \
				NETIF_MSG_HW | \
				NETIF_MSG_WOL)

/* version info */
#define EMAC_MAJOR_VERSION	6
#define EMAC_MINOR_VERSION	1
#define EMAC_MODULE_VERSION	"6.1"
MODULE_VERSION(EMAC_MODULE_VERSION);
static const char emac_version_string[] = "TI DaVinci EMAC Linux v6.1";

/* Configuration items */
#define EMAC_DEF_PASS_CRC		(0) /* Do not pass CRC upto frames */
#define EMAC_DEF_QOS_EN			(0) /* EMAC proprietary QoS disabled */
#define EMAC_DEF_NO_BUFF_CHAIN		(0) /* No buffer chain */
#define EMAC_DEF_MACCTRL_FRAME_EN	(0) /* Discard Maccontrol frames */
#define EMAC_DEF_SHORT_FRAME_EN		(0) /* Discard short frames */
#define EMAC_DEF_ERROR_FRAME_EN		(0) /* Discard error frames */
#define EMAC_DEF_PROM_EN		(0) /* Promiscous disabled */
#define EMAC_DEF_PROM_CH		(0) /* Promiscous channel is 0 */
#define EMAC_DEF_BCAST_EN		(1) /* Broadcast enabled */
#define EMAC_DEF_BCAST_CH		(0) /* Broadcast channel is 0 */
#define EMAC_DEF_MCAST_EN		(1) /* Multicast enabled */
#define EMAC_DEF_MCAST_CH		(0) /* Multicast channel is 0 */

#define EMAC_DEF_TXPRIO_FIXED		(1) /* TX Priority is fixed */
#define EMAC_DEF_TXPACING_EN		(0) /* TX pacing NOT supported*/

#define EMAC_DEF_BUFFER_OFFSET		(0) /* Buffer offset to DMA (future) */
#define EMAC_DEF_MIN_ETHPKTSIZE		(60) /* Minimum ethernet pkt size */
#define EMAC_DEF_MAX_FRAME_SIZE		(1500 + 14 + 4 + 4)
#define EMAC_DEF_TX_CH			(0) /* Default 0th channel */
#define EMAC_DEF_RX_CH			(0) /* Default 0th channel */
#define EMAC_DEF_MDIO_TICK_MS		(10) /* typically 1 tick=1 ms) */
#define EMAC_DEF_MAX_TX_CH		(1) /* Max TX channels configured */
#define EMAC_DEF_MAX_RX_CH		(1) /* Max RX channels configured */
#define EMAC_POLL_WEIGHT		(64) /* Default NAPI poll weight */

/* Buffer descriptor parameters */
#define EMAC_DEF_TX_MAX_SERVICE		(32) /* TX max service BD's */
#define EMAC_DEF_RX_MAX_SERVICE		(64) /* should = netdev->weight */

/* EMAC register related defines */
#define EMAC_ALL_MULTI_REG_VALUE	(0xFFFFFFFF)
#define EMAC_NUM_MULTICAST_BITS		(64)
#define EMAC_TEARDOWN_VALUE		(0xFFFFFFFC)
#define EMAC_TX_CONTROL_TX_ENABLE_VAL	(0x1)
#define EMAC_RX_CONTROL_RX_ENABLE_VAL	(0x1)
#define EMAC_MAC_HOST_ERR_INTMASK_VAL	(0x2)
#define EMAC_RX_UNICAST_CLEAR_ALL	(0xFF)
#define EMAC_INT_MASK_CLEAR		(0xFF)

/* RX MBP register bit positions */
#define EMAC_RXMBP_PASSCRC_MASK		BIT(30)
#define EMAC_RXMBP_QOSEN_MASK		BIT(29)
#define EMAC_RXMBP_NOCHAIN_MASK		BIT(28)
#define EMAC_RXMBP_CMFEN_MASK		BIT(24)
#define EMAC_RXMBP_CSFEN_MASK		BIT(23)
#define EMAC_RXMBP_CEFEN_MASK		BIT(22)
#define EMAC_RXMBP_CAFEN_MASK		BIT(21)
#define EMAC_RXMBP_PROMCH_SHIFT		(16)
#define EMAC_RXMBP_PROMCH_MASK		(0x7 << 16)
#define EMAC_RXMBP_BROADEN_MASK		BIT(13)
#define EMAC_RXMBP_BROADCH_SHIFT	(8)
#define EMAC_RXMBP_BROADCH_MASK		(0x7 << 8)
#define EMAC_RXMBP_MULTIEN_MASK		BIT(5)
#define EMAC_RXMBP_MULTICH_SHIFT	(0)
#define EMAC_RXMBP_MULTICH_MASK		(0x7)
#define EMAC_RXMBP_CHMASK		(0x7)

/* EMAC register definitions/bit maps used */
# define EMAC_MBP_RXPROMISC		(0x00200000)
# define EMAC_MBP_PROMISCCH(ch)		(((ch) & 0x7) << 16)
# define EMAC_MBP_RXBCAST		(0x00002000)
# define EMAC_MBP_BCASTCHAN(ch)		(((ch) & 0x7) << 8)
# define EMAC_MBP_RXMCAST		(0x00000020)
# define EMAC_MBP_MCASTCHAN(ch)		((ch) & 0x7)

/* EMAC mac_control register */
#define EMAC_MACCONTROL_TXPTYPE		(0x200)
#define EMAC_MACCONTROL_TXPACEEN	(0x40)
#define EMAC_MACCONTROL_MIIEN		(0x20)
#define EMAC_MACCONTROL_GIGABITEN	(0x80)
#define EMAC_MACCONTROL_GIGABITEN_SHIFT (7)
#define EMAC_MACCONTROL_FULLDUPLEXEN	(0x1)
#define EMAC_MACCONTROL_RMIISPEED_MASK	BIT(15)

/* GIGABIT MODE related bits */
#define EMAC_DM646X_MACCONTORL_GMIIEN	BIT(5)
#define EMAC_DM646X_MACCONTORL_GIG	BIT(7)
#define EMAC_DM646X_MACCONTORL_GIGFORCE	BIT(17)

/* EMAC mac_status register */
#define EMAC_MACSTATUS_TXERRCODE_MASK	(0xF00000)
#define EMAC_MACSTATUS_TXERRCODE_SHIFT	(20)
#define EMAC_MACSTATUS_TXERRCH_MASK	(0x7)
#define EMAC_MACSTATUS_TXERRCH_SHIFT	(16)
#define EMAC_MACSTATUS_RXERRCODE_MASK	(0xF000)
#define EMAC_MACSTATUS_RXERRCODE_SHIFT	(12)
#define EMAC_MACSTATUS_RXERRCH_MASK	(0x7)
#define EMAC_MACSTATUS_RXERRCH_SHIFT	(8)

/* EMAC RX register masks */
#define EMAC_RX_MAX_LEN_MASK		(0xFFFF)
#define EMAC_RX_BUFFER_OFFSET_MASK	(0xFFFF)

/* MAC_IN_VECTOR (0x180) register bit fields */
#define EMAC_DM644X_MAC_IN_VECTOR_HOST_INT	      (0x20000)
#define EMAC_DM644X_MAC_IN_VECTOR_STATPEND_INT	      (0x10000)
#define EMAC_DM644X_MAC_IN_VECTOR_RX_INT_VEC	      (0x0100)
#define EMAC_DM644X_MAC_IN_VECTOR_TX_INT_VEC	      (0x01)

/** NOTE:: For DM646x the IN_VECTOR has changed */
#define EMAC_DM646X_MAC_IN_VECTOR_RX_INT_VEC	BIT(EMAC_DEF_RX_CH)
#define EMAC_DM646X_MAC_IN_VECTOR_TX_INT_VEC	BIT(16 + EMAC_DEF_TX_CH)

/* CPPI bit positions */
#define EMAC_CPPI_SOP_BIT		BIT(31)
#define EMAC_CPPI_EOP_BIT		BIT(30)
#define EMAC_CPPI_OWNERSHIP_BIT		BIT(29)
#define EMAC_CPPI_EOQ_BIT		BIT(28)
#define EMAC_CPPI_TEARDOWN_COMPLETE_BIT BIT(27)
#define EMAC_CPPI_PASS_CRC_BIT		BIT(26)
#define EMAC_RX_BD_BUF_SIZE		(0xFFFF)
#define EMAC_BD_LENGTH_FOR_CACHE	(16) /* only CPPI bytes */
#define EMAC_RX_BD_PKT_LENGTH_MASK	(0xFFFF)

/* Max hardware defines */
#define EMAC_MAX_TXRX_CHANNELS		 (8)  /* Max hardware channels */
#define EMAC_DEF_MAX_MULTICAST_ADDRESSES (64) /* Max mcast addr's */

/* EMAC Peripheral Device Register Memory Layout structure */
#define EMAC_TXIDVER		0x0
#define EMAC_TXCONTROL		0x4
#define EMAC_TXTEARDOWN		0x8
#define EMAC_RXIDVER		0x10
#define EMAC_RXCONTROL		0x14
#define EMAC_RXTEARDOWN		0x18
#define EMAC_TXINTSTATRAW	0x80
#define EMAC_TXINTSTATMASKED	0x84
#define EMAC_TXINTMASKSET	0x88
#define EMAC_TXINTMASKCLEAR	0x8C
#define EMAC_MACINVECTOR	0x90

#define EMAC_DM646X_MACEOIVECTOR	0x94

#define EMAC_RXINTSTATRAW	0xA0
#define EMAC_RXINTSTATMASKED	0xA4
#define EMAC_RXINTMASKSET	0xA8
#define EMAC_RXINTMASKCLEAR	0xAC
#define EMAC_MACINTSTATRAW	0xB0
#define EMAC_MACINTSTATMASKED	0xB4
#define EMAC_MACINTMASKSET	0xB8
#define EMAC_MACINTMASKCLEAR	0xBC

#define EMAC_RXMBPENABLE	0x100
#define EMAC_RXUNICASTSET	0x104
#define EMAC_RXUNICASTCLEAR	0x108
#define EMAC_RXMAXLEN		0x10C
#define EMAC_RXBUFFEROFFSET	0x110
#define EMAC_RXFILTERLOWTHRESH	0x114

#define EMAC_MACCONTROL		0x160
#define EMAC_MACSTATUS		0x164
#define EMAC_EMCONTROL		0x168
#define EMAC_FIFOCONTROL	0x16C
#define EMAC_MACCONFIG		0x170
#define EMAC_SOFTRESET		0x174
#define EMAC_MACSRCADDRLO	0x1D0
#define EMAC_MACSRCADDRHI	0x1D4
#define EMAC_MACHASH1		0x1D8
#define EMAC_MACHASH2		0x1DC
#define EMAC_MACADDRLO		0x500
#define EMAC_MACADDRHI		0x504
#define EMAC_MACINDEX		0x508

/* EMAC HDP and Completion registors */
#define EMAC_TXHDP(ch)		(0x600 + (ch * 4))
#define EMAC_RXHDP(ch)		(0x620 + (ch * 4))
#define EMAC_TXCP(ch)		(0x640 + (ch * 4))
#define EMAC_RXCP(ch)		(0x660 + (ch * 4))

/* EMAC statistics registers */
#define EMAC_RXGOODFRAMES	0x200
#define EMAC_RXBCASTFRAMES	0x204
#define EMAC_RXMCASTFRAMES	0x208
#define EMAC_RXPAUSEFRAMES	0x20C
#define EMAC_RXCRCERRORS	0x210
#define EMAC_RXALIGNCODEERRORS	0x214
#define EMAC_RXOVERSIZED	0x218
#define EMAC_RXJABBER		0x21C
#define EMAC_RXUNDERSIZED	0x220
#define EMAC_RXFRAGMENTS	0x224
#define EMAC_RXFILTERED		0x228
#define EMAC_RXQOSFILTERED	0x22C
#define EMAC_RXOCTETS		0x230
#define EMAC_TXGOODFRAMES	0x234
#define EMAC_TXBCASTFRAMES	0x238
#define EMAC_TXMCASTFRAMES	0x23C
#define EMAC_TXPAUSEFRAMES	0x240
#define EMAC_TXDEFERRED		0x244
#define EMAC_TXCOLLISION	0x248
#define EMAC_TXSINGLECOLL	0x24C
#define EMAC_TXMULTICOLL	0x250
#define EMAC_TXEXCESSIVECOLL	0x254
#define EMAC_TXLATECOLL		0x258
#define EMAC_TXUNDERRUN		0x25C
#define EMAC_TXCARRIERSENSE	0x260
#define EMAC_TXOCTETS		0x264
#define EMAC_NETOCTETS		0x280
#define EMAC_RXSOFOVERRUNS	0x284
#define EMAC_RXMOFOVERRUNS	0x288
#define EMAC_RXDMAOVERRUNS	0x28C

/* EMAC DM644x control registers */
#define EMAC_CTRL_EWCTL		(0x4)
#define EMAC_CTRL_EWINTTCNT	(0x8)

/* EMAC MDIO related */
/* Mask & Control defines */
#define MDIO_CONTROL_CLKDIV	(0xFF)
#define MDIO_CONTROL_ENABLE	BIT(30)
#define MDIO_USERACCESS_GO	BIT(31)
#define MDIO_USERACCESS_WRITE	BIT(30)
#define MDIO_USERACCESS_READ	(0)
#define MDIO_USERACCESS_REGADR	(0x1F << 21)
#define MDIO_USERACCESS_PHYADR	(0x1F << 16)
#define MDIO_USERACCESS_DATA	(0xFFFF)
#define MDIO_USERPHYSEL_LINKSEL	BIT(7)
#define MDIO_VER_MODID		(0xFFFF << 16)
#define MDIO_VER_REVMAJ		(0xFF   << 8)
#define MDIO_VER_REVMIN		(0xFF)

#define MDIO_USERACCESS(inst)	(0x80 + (inst * 8))
#define MDIO_USERPHYSEL(inst)	(0x84 + (inst * 8))
#define MDIO_CONTROL		(0x04)

/* EMAC DM646X control module registers */
#define EMAC_DM646X_CMRXINTEN	(0x14)
#define EMAC_DM646X_CMTXINTEN	(0x18)

/* EMAC EOI codes for C0 */
#define EMAC_DM646X_MAC_EOI_C0_RXEN	(0x01)
#define EMAC_DM646X_MAC_EOI_C0_TXEN	(0x02)

/** net_buf_obj: EMAC network bufferdata structure
 *
 * EMAC network buffer data structure
 */
struct emac_netbufobj {
	void *buf_token;
	char *data_ptr;
	int length;
};

/** net_pkt_obj: EMAC network packet data structure
 *
 * EMAC network packet data structure - supports buffer list (for future)
 */
struct emac_netpktobj {
	void *pkt_token; /* data token may hold tx/rx chan id */
	struct emac_netbufobj *buf_list; /* array of network buffer objects */
	int num_bufs;
	int pkt_length;
};

/** emac_tx_bd: EMAC TX Buffer descriptor data structure
 *
 * EMAC TX Buffer descriptor data structure
 */
struct emac_tx_bd {
	int h_next;
	int buff_ptr;
	int off_b_len;
	int mode; /* SOP, EOP, ownership, EOQ, teardown,Qstarv, length */
	struct emac_tx_bd __iomem *next;
	void *buf_token;
};

/** emac_txch: EMAC TX Channel data structure
 *
 * EMAC TX Channel data structure
 */
struct emac_txch {
	/* Config related */
	u32 num_bd;
	u32 service_max;

	/* CPPI specific */
	u32 alloc_size;
	void __iomem *bd_mem;
	struct emac_tx_bd __iomem *bd_pool_head;
	struct emac_tx_bd __iomem *active_queue_head;
	struct emac_tx_bd __iomem *active_queue_tail;
	struct emac_tx_bd __iomem *last_hw_bdprocessed;
	u32 queue_active;
	u32 teardown_pending;
	u32 *tx_complete;

	/** statistics */
	u32 proc_count;     /* TX: # of times emac_tx_bdproc is called */
	u32 mis_queued_packets;
	u32 queue_reinit;
	u32 end_of_queue_add;
	u32 out_of_tx_bd;
	u32 no_active_pkts; /* IRQ when there were no packets to process */
	u32 active_queue_count;
};

/** emac_rx_bd: EMAC RX Buffer descriptor data structure
 *
 * EMAC RX Buffer descriptor data structure
 */
struct emac_rx_bd {
	int h_next;
	int buff_ptr;
	int off_b_len;
	int mode;
	struct emac_rx_bd __iomem *next;
	void *data_ptr;
	void *buf_token;
};

/** emac_rxch: EMAC RX Channel data structure
 *
 * EMAC RX Channel data structure
 */
struct emac_rxch {
	/* configuration info */
	u32 num_bd;
	u32 service_max;
	u32 buf_size;
	char mac_addr[6];

	/** CPPI specific */
	u32 alloc_size;
	void __iomem *bd_mem;
	struct emac_rx_bd __iomem *bd_pool_head;
	struct emac_rx_bd __iomem *active_queue_head;
	struct emac_rx_bd __iomem *active_queue_tail;
	u32 queue_active;
	u32 teardown_pending;

	/* packet and buffer objects */
	struct emac_netpktobj pkt_queue;
	struct emac_netbufobj buf_queue;

	/** statistics */
	u32 proc_count; /* number of times emac_rx_bdproc is called */
	u32 processed_bd;
	u32 recycled_bd;
	u32 out_of_rx_bd;
	u32 out_of_rx_buffers;
	u32 queue_reinit;
	u32 end_of_queue_add;
	u32 end_of_queue;
	u32 mis_queued_packets;
};

/* emac_priv: EMAC private data structure
 *
 * EMAC adapter private data structure
 */
struct emac_priv {
	u32 msg_enable;
	struct net_device *ndev;
	struct platform_device *pdev;
	struct napi_struct napi;
	char mac_addr[6];
	spinlock_t tx_lock;
	spinlock_t rx_lock;
	void __iomem *remap_addr;
	u32 emac_base_phys;
	void __iomem *emac_base;
	void __iomem *ctrl_base;
	void __iomem *emac_ctrl_ram;
	u32 ctrl_ram_size;
	struct emac_txch *txch[EMAC_DEF_MAX_TX_CH];
	struct emac_rxch *rxch[EMAC_DEF_MAX_RX_CH];
	u32 link; /* 1=link on, 0=link off */
	u32 speed; /* 0=Auto Neg, 1=No PHY, 10,100, 1000 - mbps */
	u32 duplex; /* Link duplex: 0=Half, 1=Full */
	u32 rx_buf_size;
	u32 isr_count;
	u8 rmii_en;
	u8 version;
	struct net_device_stats net_dev_stats;
	u32 mac_hash1;
	u32 mac_hash2;
	u32 multicast_hash_cnt[EMAC_NUM_MULTICAST_BITS];
	u32 rx_addr_type;
	/* periodic timer required for MDIO polling */
	struct timer_list periodic_timer;
	u32 periodic_ticks;
	u32 timer_active;
	u32 phy_mask;
	/* mii_bus,phy members */
	struct mii_bus *mii_bus;
	struct phy_device *phydev;
	spinlock_t lock;
};

/* clock frequency for EMAC */
static struct clk *emac_clk;
static unsigned long emac_bus_frequency;
static unsigned long mdio_max_freq;

/* EMAC internal utility function */
static inline u32 emac_virt_to_phys(void __iomem *addr)
{
	return (u32 __force) io_v2p(addr);
}

/* Cache macros - Packet buffers would be from skb pool which is cached */
#define EMAC_VIRT_NOCACHE(addr) (addr)
#define EMAC_CACHE_INVALIDATE(addr, size) \
	dma_cache_maint((void *)addr, size, DMA_FROM_DEVICE)
#define EMAC_CACHE_WRITEBACK(addr, size) \
	dma_cache_maint((void *)addr, size, DMA_TO_DEVICE)
#define EMAC_CACHE_WRITEBACK_INVALIDATE(addr, size) \
	dma_cache_maint((void *)addr, size, DMA_BIDIRECTIONAL)

/* DM644x does not have BD's in cached memory - so no cache functions */
#define BD_CACHE_INVALIDATE(addr, size)
#define BD_CACHE_WRITEBACK(addr, size)
#define BD_CACHE_WRITEBACK_INVALIDATE(addr, size)

/* EMAC TX Host Error description strings */
static char *emac_txhost_errcodes[16] = {
	"No error", "SOP error", "Ownership bit not set in SOP buffer",
	"Zero Next Buffer Descriptor Pointer Without EOP",
	"Zero Buffer Pointer", "Zero Buffer Length", "Packet Length Error",
	"Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
	"Reserved", "Reserved", "Reserved", "Reserved"
};

/* EMAC RX Host Error description strings */
static char *emac_rxhost_errcodes[16] = {
	"No error", "Reserved", "Ownership bit not set in input buffer",
	"Reserved", "Zero Buffer Pointer", "Reserved", "Reserved",
	"Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
	"Reserved", "Reserved", "Reserved", "Reserved"
};

/* Helper macros */
#define emac_read(reg)		  ioread32(priv->emac_base + (reg))
#define emac_write(reg, val)      iowrite32(val, priv->emac_base + (reg))

#define emac_ctrl_read(reg)	  ioread32((priv->ctrl_base + (reg)))
#define emac_ctrl_write(reg, val) iowrite32(val, (priv->ctrl_base + (reg)))

#define emac_mdio_read(reg)	  ioread32(bus->priv + (reg))
#define emac_mdio_write(reg, val) iowrite32(val, (bus->priv + (reg)))

/**
 * emac_dump_regs: Dump important EMAC registers to debug terminal
 * @priv: The DaVinci EMAC private adapter structure
 *
 * Executes ethtool set cmd & sets phy mode
 *
 */
static void emac_dump_regs(struct emac_priv *priv)
{
	struct device *emac_dev = &priv->ndev->dev;

	/* Print important registers in EMAC */
	dev_info(emac_dev, "EMAC Basic registers\n");
	dev_info(emac_dev, "EMAC: EWCTL: %08X, EWINTTCNT: %08X\n",
		emac_ctrl_read(EMAC_CTRL_EWCTL),
		emac_ctrl_read(EMAC_CTRL_EWINTTCNT));
	dev_info(emac_dev, "EMAC: TXID: %08X %s, RXID: %08X %s\n",
		emac_read(EMAC_TXIDVER),
		((emac_read(EMAC_TXCONTROL)) ? "enabled" : "disabled"),
		emac_read(EMAC_RXIDVER),
		((emac_read(EMAC_RXCONTROL)) ? "enabled" : "disabled"));
	dev_info(emac_dev, "EMAC: TXIntRaw:%08X, TxIntMasked: %08X, "\
		"TxIntMasSet: %08X\n", emac_read(EMAC_TXINTSTATRAW),
		emac_read(EMAC_TXINTSTATMASKED), emac_read(EMAC_TXINTMASKSET));
	dev_info(emac_dev, "EMAC: RXIntRaw:%08X, RxIntMasked: %08X, "\
		"RxIntMasSet: %08X\n", emac_read(EMAC_RXINTSTATRAW),
		emac_read(EMAC_RXINTSTATMASKED), emac_read(EMAC_RXINTMASKSET));
	dev_info(emac_dev, "EMAC: MacIntRaw:%08X, MacIntMasked: %08X, "\
		"MacInVector=%08X\n", emac_read(EMAC_MACINTSTATRAW),
		emac_read(EMAC_MACINTSTATMASKED), emac_read(EMAC_MACINVECTOR));
	dev_info(emac_dev, "EMAC: EmuControl:%08X, FifoControl: %08X\n",
		emac_read(EMAC_EMCONTROL), emac_read(EMAC_FIFOCONTROL));
	dev_info(emac_dev, "EMAC: MBPEnable:%08X, RXUnicastSet: %08X, "\
		"RXMaxLen=%08X\n", emac_read(EMAC_RXMBPENABLE),
		emac_read(EMAC_RXUNICASTSET), emac_read(EMAC_RXMAXLEN));
	dev_info(emac_dev, "EMAC: MacControl:%08X, MacStatus: %08X, "\
		"MacConfig=%08X\n", emac_read(EMAC_MACCONTROL),
		emac_read(EMAC_MACSTATUS), emac_read(EMAC_MACCONFIG));
	dev_info(emac_dev, "EMAC: TXHDP[0]:%08X, RXHDP[0]: %08X\n",
		emac_read(EMAC_TXHDP(0)), emac_read(EMAC_RXHDP(0)));
	dev_info(emac_dev, "EMAC Statistics\n");
	dev_info(emac_dev, "EMAC: rx_good_frames:%d\n",
		emac_read(EMAC_RXGOODFRAMES));
	dev_info(emac_dev, "EMAC: rx_broadcast_frames:%d\n",
		emac_read(EMAC_RXBCASTFRAMES));
	dev_info(emac_dev, "EMAC: rx_multicast_frames:%d\n",
		emac_read(EMAC_RXMCASTFRAMES));
	dev_info(emac_dev, "EMAC: rx_pause_frames:%d\n",
		emac_read(EMAC_RXPAUSEFRAMES));
	dev_info(emac_dev, "EMAC: rx_crcerrors:%d\n",
		emac_read(EMAC_RXCRCERRORS));
	dev_info(emac_dev, "EMAC: rx_align_code_errors:%d\n",
		emac_read(EMAC_RXALIGNCODEERRORS));
	dev_info(emac_dev, "EMAC: rx_oversized_frames:%d\n",
		emac_read(EMAC_RXOVERSIZED));
	dev_info(emac_dev, "EMAC: rx_jabber_frames:%d\n",
		emac_read(EMAC_RXJABBER));
	dev_info(emac_dev, "EMAC: rx_undersized_frames:%d\n",
		emac_read(EMAC_RXUNDERSIZED));
	dev_info(emac_dev, "EMAC: rx_fragments:%d\n",
		emac_read(EMAC_RXFRAGMENTS));
	dev_info(emac_dev, "EMAC: rx_filtered_frames:%d\n",
		emac_read(EMAC_RXFILTERED));
	dev_info(emac_dev, "EMAC: rx_qos_filtered_frames:%d\n",
		emac_read(EMAC_RXQOSFILTERED));
	dev_info(emac_dev, "EMAC: rx_octets:%d\n",
		emac_read(EMAC_RXOCTETS));
	dev_info(emac_dev, "EMAC: tx_goodframes:%d\n",
		emac_read(EMAC_TXGOODFRAMES));
	dev_info(emac_dev, "EMAC: tx_bcastframes:%d\n",
		emac_read(EMAC_TXBCASTFRAMES));
	dev_info(emac_dev, "EMAC: tx_mcastframes:%d\n",
		emac_read(EMAC_TXMCASTFRAMES));
	dev_info(emac_dev, "EMAC: tx_pause_frames:%d\n",
		emac_read(EMAC_TXPAUSEFRAMES));
	dev_info(emac_dev, "EMAC: tx_deferred_frames:%d\n",
		emac_read(EMAC_TXDEFERRED));
	dev_info(emac_dev, "EMAC: tx_collision_frames:%d\n",
		emac_read(EMAC_TXCOLLISION));
	dev_info(emac_dev, "EMAC: tx_single_coll_frames:%d\n",
		emac_read(EMAC_TXSINGLECOLL));
	dev_info(emac_dev, "EMAC: tx_mult_coll_frames:%d\n",
		emac_read(EMAC_TXMULTICOLL));
	dev_info(emac_dev, "EMAC: tx_excessive_collisions:%d\n",
		emac_read(EMAC_TXEXCESSIVECOLL));
	dev_info(emac_dev, "EMAC: tx_late_collisions:%d\n",
		emac_read(EMAC_TXLATECOLL));
	dev_info(emac_dev, "EMAC: tx_underrun:%d\n",
		emac_read(EMAC_TXUNDERRUN));
	dev_info(emac_dev, "EMAC: tx_carrier_sense_errors:%d\n",
		emac_read(EMAC_TXCARRIERSENSE));
	dev_info(emac_dev, "EMAC: tx_octets:%d\n",
		emac_read(EMAC_TXOCTETS));
	dev_info(emac_dev, "EMAC: net_octets:%d\n",
		emac_read(EMAC_NETOCTETS));
	dev_info(emac_dev, "EMAC: rx_sof_overruns:%d\n",
		emac_read(EMAC_RXSOFOVERRUNS));
	dev_info(emac_dev, "EMAC: rx_mof_overruns:%d\n",
		emac_read(EMAC_RXMOFOVERRUNS));
	dev_info(emac_dev, "EMAC: rx_dma_overruns:%d\n",
		emac_read(EMAC_RXDMAOVERRUNS));
}

/*************************************************************************
 *  EMAC MDIO/Phy Functionality
 *************************************************************************/
/**
 * emac_get_drvinfo: Get EMAC driver information
 * @ndev: The DaVinci EMAC network adapter
 * @info: ethtool info structure containing name and version
 *
 * Returns EMAC driver information (name and version)
 *
 */
static void emac_get_drvinfo(struct net_device *ndev,
			     struct ethtool_drvinfo *info)
{
	strcpy(info->driver, emac_version_string);
	strcpy(info->version, EMAC_MODULE_VERSION);
}

/**
 * emac_get_settings: Get EMAC settings
 * @ndev: The DaVinci EMAC network adapter
 * @ecmd: ethtool command
 *
 * Executes ethool get command
 *
 */
static int emac_get_settings(struct net_device *ndev,
			     struct ethtool_cmd *ecmd)
{
	struct emac_priv *priv = netdev_priv(ndev);
	if (priv->phy_mask)
		return phy_ethtool_gset(priv->phydev, ecmd);
	else
		return -EOPNOTSUPP;

}

/**
 * emac_set_settings: Set EMAC settings
 * @ndev: The DaVinci EMAC network adapter
 * @ecmd: ethtool command
 *
 * Executes ethool set command
 *
 */
static int emac_set_settings(struct net_device *ndev, struct ethtool_cmd *ecmd)
{
	struct emac_priv *priv = netdev_priv(ndev);
	if (priv->phy_mask)
		return phy_ethtool_sset(priv->phydev, ecmd);
	else
		return -EOPNOTSUPP;

}

/**
 * ethtool_ops: DaVinci EMAC Ethtool structure
 *
 * Ethtool support for EMAC adapter
 *
 */
static const struct ethtool_ops ethtool_ops = {
	.get_drvinfo = emac_get_drvinfo,
	.get_settings = emac_get_settings,
	.set_settings = emac_set_settings,
	.get_link = ethtool_op_get_link,
};

/**
 * emac_update_phystatus: Update Phy status
 * @priv: The DaVinci EMAC private adapter structure
 *
 * Updates phy status and takes action for network queue if required
 * based upon link status
 *
 */
static void emac_update_phystatus(struct emac_priv *priv)
{
	u32 mac_control;
	u32 new_duplex;
	u32 cur_duplex;
	struct net_device *ndev = priv->ndev;

	mac_control = emac_read(EMAC_MACCONTROL);
	cur_duplex = (mac_control & EMAC_MACCONTROL_FULLDUPLEXEN) ?
			DUPLEX_FULL : DUPLEX_HALF;
	if (priv->phy_mask)
		new_duplex = priv->phydev->duplex;
	else
		new_duplex = DUPLEX_FULL;

	/* We get called only if link has changed (speed/duplex/status) */
	if ((priv->link) && (new_duplex != cur_duplex)) {
		priv->duplex = new_duplex;
		if (DUPLEX_FULL == priv->duplex)
			mac_control |= (EMAC_MACCONTROL_FULLDUPLEXEN);
		else
			mac_control &= ~(EMAC_MACCONTROL_FULLDUPLEXEN);
	}

	if (priv->speed == SPEED_1000 && (priv->version == EMAC_VERSION_2)) {
		mac_control = emac_read(EMAC_MACCONTROL);
		mac_control |= (EMAC_DM646X_MACCONTORL_GMIIEN |
				EMAC_DM646X_MACCONTORL_GIG |
				EMAC_DM646X_MACCONTORL_GIGFORCE);
	} else {
		/* Clear the GIG bit and GIGFORCE bit */
		mac_control &= ~(EMAC_DM646X_MACCONTORL_GIGFORCE |
					EMAC_DM646X_MACCONTORL_GIG);

		if (priv->rmii_en && (priv->speed == SPEED_100))
			mac_control |= EMAC_MACCONTROL_RMIISPEED_MASK;
		else
			mac_control &= ~EMAC_MACCONTROL_RMIISPEED_MASK;
	}

	/* Update mac_control if changed */
	emac_write(EMAC_MACCONTROL, mac_control);

	if (priv->link) {
		/* link ON */
		if (!netif_carrier_ok(ndev))
			netif_carrier_on(ndev);
	/* reactivate the transmit queue if it is stopped */
		if (netif_running(ndev) && netif_queue_stopped(ndev))
			netif_wake_queue(ndev);
	} else {
		/* link OFF */
		if (netif_carrier_ok(ndev))
			netif_carrier_off(ndev);
		if (!netif_queue_stopped(ndev))
			netif_stop_queue(ndev);
	}
}

/**
 * hash_get: Calculate hash value from mac address
 * @addr: mac address to delete from hash table
 *
 * Calculates hash value from mac address
 *
 */
static u32 hash_get(u8 *addr)
{
	u32 hash;
	u8 tmpval;
	int cnt;
	hash = 0;

	for (cnt = 0; cnt < 2; cnt++) {
		tmpval = *addr++;
		hash ^= (tmpval >> 2) ^ (tmpval << 4);
		tmpval = *addr++;
		hash ^= (tmpval >> 4) ^ (tmpval << 2);
		tmpval = *addr++;
		hash ^= (tmpval >> 6) ^ (tmpval);
	}

	return hash & 0x3F;
}

/**
 * hash_add: Hash function to add mac addr from hash table
 * @priv: The DaVinci EMAC private adapter structure
 * mac_addr: mac address to delete from hash table
 *
 * Adds mac address to the internal hash table
 *
 */
static int hash_add(struct emac_priv *priv, u8 *mac_addr)
{
	struct device *emac_dev = &priv->ndev->dev;
	u32 rc = 0;
	u32 hash_bit;
	u32 hash_value = hash_get(mac_addr);

	if (hash_value >= EMAC_NUM_MULTICAST_BITS) {
		if (netif_msg_drv(priv)) {
			dev_err(emac_dev, "DaVinci EMAC: hash_add(): Invalid "\
				"Hash %08x, should not be greater than %08x",
				hash_value, (EMAC_NUM_MULTICAST_BITS - 1));
		}
		return -1;
	}

	/* set the hash bit only if not previously set */
	if (priv->multicast_hash_cnt[hash_value] == 0) {
		rc = 1; /* hash value changed */
		if (hash_value < 32) {
			hash_bit = BIT(hash_value);
			priv->mac_hash1 |= hash_bit;
		} else {
			hash_bit = BIT((hash_value - 32));
			priv->mac_hash2 |= hash_bit;
		}
	}

	/* incr counter for num of mcast addr's mapped to "this" hash bit */
	++priv->multicast_hash_cnt[hash_value];

	return rc;
}

/**
 * hash_del: Hash function to delete mac addr from hash table
 * @priv: The DaVinci EMAC private adapter structure
 * mac_addr: mac address to delete from hash table
 *
 * Removes mac address from the internal hash table
 *
 */
static int hash_del(struct emac_priv *priv, u8 *mac_addr)
{
	u32 hash_value;
	u32 hash_bit;

	hash_value = hash_get(mac_addr);
	if (priv->multicast_hash_cnt[hash_value] > 0) {
		/* dec cntr for num of mcast addr's mapped to this hash bit */
		--priv->multicast_hash_cnt[hash_value];
	}

	/* if counter still > 0, at least one multicast address refers
	 * to this hash bit. so return 0 */
	if (priv->multicast_hash_cnt[hash_value] > 0)
		return 0;

	if (hash_value < 32) {
		hash_bit = BIT(hash_value);
		priv->mac_hash1 &= ~hash_bit;
	} else {
		hash_bit = BIT((hash_value - 32));
		priv->mac_hash2 &= ~hash_bit;
	}

	/* return 1 to indicate change in mac_hash registers reqd */
	return 1;
}

/* EMAC multicast operation */
#define EMAC_MULTICAST_ADD	0
#define EMAC_MULTICAST_DEL	1
#define EMAC_ALL_MULTI_SET	2
#define EMAC_ALL_MULTI_CLR	3

/**
 * emac_add_mcast: Set multicast address in the EMAC adapter (Internal)
 * @priv: The DaVinci EMAC private adapter structure
 * @action: multicast operation to perform
 * mac_addr: mac address to set
 *
 * Set multicast addresses in EMAC adapter - internal function
 *
 */
static void emac_add_mcast(struct emac_priv *priv, u32 action, u8 *mac_addr)
{
	struct device *emac_dev = &priv->ndev->dev;
	int update = -1;

	switch (action) {
	case EMAC_MULTICAST_ADD:
		update = hash_add(priv, mac_addr);
		break;
	case EMAC_MULTICAST_DEL:
		update = hash_del(priv, mac_addr);
		break;
	case EMAC_ALL_MULTI_SET:
		update = 1;
		priv->mac_hash1 = EMAC_ALL_MULTI_REG_VALUE;
		priv->mac_hash2 = EMAC_ALL_MULTI_REG_VALUE;
		break;
	case EMAC_ALL_MULTI_CLR:
		update = 1;
		priv->mac_hash1 = 0;
		priv->mac_hash2 = 0;
		memset(&(priv->multicast_hash_cnt[0]), 0,
		sizeof(priv->multicast_hash_cnt[0]) *
		       EMAC_NUM_MULTICAST_BITS);
		break;
	default:
		if (netif_msg_drv(priv))
			dev_err(emac_dev, "DaVinci EMAC: add_mcast"\
				": bad operation %d", action);
		break;
	}

	/* write to the hardware only if the register status chances */
	if (update > 0) {
		emac_write(EMAC_MACHASH1, priv->mac_hash1);
		emac_write(EMAC_MACHASH2, priv->mac_hash2);
	}
}

/**
 * emac_dev_mcast_set: Set multicast address in the EMAC adapter
 * @ndev: The DaVinci EMAC network adapter
 *
 * Set multicast addresses in EMAC adapter
 *
 */
static void emac_dev_mcast_set(struct net_device *ndev)
{
	u32 mbp_enable;
	struct emac_priv *priv = netdev_priv(ndev);

	mbp_enable = emac_read(EMAC_RXMBPENABLE);
	if (ndev->flags & IFF_PROMISC) {
		mbp_enable &= (~EMAC_MBP_PROMISCCH(EMAC_DEF_PROM_CH));
		mbp_enable |= (EMAC_MBP_RXPROMISC);
	} else {
		mbp_enable = (mbp_enable & ~EMAC_MBP_RXPROMISC);
		if ((ndev->flags & IFF_ALLMULTI) ||
		    (ndev->mc_count > EMAC_DEF_MAX_MULTICAST_ADDRESSES)) {
			mbp_enable = (mbp_enable | EMAC_MBP_RXMCAST);
			emac_add_mcast(priv, EMAC_ALL_MULTI_SET, NULL);
		}
		if (ndev->mc_count > 0) {
			struct dev_mc_list *mc_ptr;
			mbp_enable = (mbp_enable | EMAC_MBP_RXMCAST);
			emac_add_mcast(priv, EMAC_ALL_MULTI_CLR, NULL);
			/* program multicast address list into EMAC hardware */
			for (mc_ptr = ndev->mc_list; mc_ptr;
			     mc_ptr = mc_ptr->next) {
				emac_add_mcast(priv, EMAC_MULTICAST_ADD,
					       (u8 *)mc_ptr->dmi_addr);
			}
		} else {
			mbp_enable = (mbp_enable & ~EMAC_MBP_RXMCAST);
			emac_add_mcast(priv, EMAC_ALL_MULTI_CLR, NULL);
		}
	}
	/* Set mbp config register */
	emac_write(EMAC_RXMBPENABLE, mbp_enable);
}

/*************************************************************************
 *  EMAC Hardware manipulation
 *************************************************************************/

/**
 * emac_int_disable: Disable EMAC module interrupt (from adapter)
 * @priv: The DaVinci EMAC private adapter structure
 *
 * Disable EMAC interrupt on the adapter
 *
 */
static void emac_int_disable(struct emac_priv *priv)
{
	if (priv->version == EMAC_VERSION_2) {
		unsigned long flags;

		local_irq_save(flags);

		/* Program C0_Int_En to zero to turn off
		* interrupts to the CPU */
		emac_ctrl_write(EMAC_DM646X_CMRXINTEN, 0x0);
		emac_ctrl_write(EMAC_DM646X_CMTXINTEN, 0x0);
		/* NOTE: Rx Threshold and Misc interrupts are not disabled */

		local_irq_restore(flags);

	} else {
		/* Set DM644x control registers for interrupt control */
		emac_ctrl_write(EMAC_CTRL_EWCTL, 0x0);
	}
}

/**
 * emac_int_enable: Enable EMAC module interrupt (from adapter)
 * @priv: The DaVinci EMAC private adapter structure
 *
 * Enable EMAC interrupt on the adapter
 *
 */
static void emac_int_enable(struct emac_priv *priv)
{
	if (priv->version == EMAC_VERSION_2) {
		emac_ctrl_write(EMAC_DM646X_CMRXINTEN, 0xff);
		emac_ctrl_write(EMAC_DM646X_CMTXINTEN, 0xff);

		/* In addition to turning on interrupt Enable, we need
		 * ack by writing appropriate values to the EOI
		 * register */

		/* NOTE: Rx Threshold and Misc interrupts are not enabled */

		/* ack rxen only then a new pulse will be generated */
		emac_write(EMAC_DM646X_MACEOIVECTOR,
			EMAC_DM646X_MAC_EOI_C0_RXEN);

		/* ack txen- only then a new pulse will be generated */
		emac_write(EMAC_DM646X_MACEOIVECTOR,
			EMAC_DM646X_MAC_EOI_C0_TXEN);

	} else {
		/* Set DM644x control registers for interrupt control */
		emac_ctrl_write(EMAC_CTRL_EWCTL, 0x1);
	}
}

/**
 * emac_irq: EMAC interrupt handler
 * @irq: interrupt number
 * @dev_id: EMAC network adapter data structure ptr
 *
 * EMAC Interrupt handler - we only schedule NAPI and not process any packets
 * here. EVen the interrupt status is checked (TX/RX/Err) in NAPI poll function
 *
 * Returns interrupt handled condition
 */
static irqreturn_t emac_irq(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct emac_priv *priv = netdev_priv(ndev);

	++priv->isr_count;
	if (likely(netif_running(priv->ndev))) {
		emac_int_disable(priv);
		napi_schedule(&priv->napi);
	} else {
		/* we are closing down, so dont process anything */
	}
	return IRQ_HANDLED;
}

/** EMAC on-chip buffer descriptor memory
 *
 * WARNING: Please note that the on chip memory is used for both TX and RX
 * buffer descriptor queues and is equally divided between TX and RX desc's
 * If the number of TX or RX descriptors change this memory pointers need
 * to be adjusted. If external memory is allocated then these pointers can
 * pointer to the memory
 *
 */
#define EMAC_TX_BD_MEM(priv)	((priv)->emac_ctrl_ram)
#define EMAC_RX_BD_MEM(priv)	((priv)->emac_ctrl_ram + \
				(((priv)->ctrl_ram_size) >> 1))

/**
 * emac_init_txch: TX channel initialization
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 *
 * Called during device init to setup a TX channel (allocate buffer desc
 * create free pool and keep ready for transmission
 *
 * Returns success(0) or mem alloc failures error code
 */
static int emac_init_txch(struct emac_priv *priv, u32 ch)
{
	struct device *emac_dev = &priv->ndev->dev;
	u32 cnt, bd_size;
	void __iomem *mem;
	struct emac_tx_bd __iomem *curr_bd;
	struct emac_txch *txch = NULL;

	txch = kzalloc(sizeof(struct emac_txch), GFP_KERNEL);
	if (NULL == txch) {
		dev_err(emac_dev, "DaVinci EMAC: TX Ch mem alloc failed");
		return -ENOMEM;
	}
	priv->txch[ch] = txch;
	txch->service_max = EMAC_DEF_TX_MAX_SERVICE;
	txch->active_queue_head = NULL;
	txch->active_queue_tail = NULL;
	txch->queue_active = 0;
	txch->teardown_pending = 0;

	/* allocate memory for TX CPPI channel on a 4 byte boundry */
	txch->tx_complete = kzalloc(txch->service_max * sizeof(u32),
				    GFP_KERNEL);
	if (NULL == txch->tx_complete) {
		dev_err(emac_dev, "DaVinci EMAC: Tx service mem alloc failed");
		kfree(txch);
		return -ENOMEM;
	}

	/* allocate buffer descriptor pool align every BD on four word
	 * boundry for future requirements */
	bd_size = (sizeof(struct emac_tx_bd) + 0xF) & ~0xF;
	txch->num_bd = (priv->ctrl_ram_size >> 1) / bd_size;
	txch->alloc_size = (((bd_size * txch->num_bd) + 0xF) & ~0xF);

	/* alloc TX BD memory */
	txch->bd_mem = EMAC_TX_BD_MEM(priv);
	__memzero((void __force *)txch->bd_mem, txch->alloc_size);

	/* initialize the BD linked list */
	mem = (void __force __iomem *)
			(((u32 __force) txch->bd_mem + 0xF) & ~0xF);
	txch->bd_pool_head = NULL;
	for (cnt = 0; cnt < txch->num_bd; cnt++) {
		curr_bd = mem + (cnt * bd_size);
		curr_bd->next = txch->bd_pool_head;
		txch->bd_pool_head = curr_bd;
	}

	/* reset statistics counters */
	txch->out_of_tx_bd = 0;
	txch->no_active_pkts = 0;
	txch->active_queue_count = 0;

	return 0;
}

/**
 * emac_cleanup_txch: Book-keep function to clean TX channel resources
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: TX channel number
 *
 * Called to clean up TX channel resources
 *
 */
static void emac_cleanup_txch(struct emac_priv *priv, u32 ch)
{
	struct emac_txch *txch = priv->txch[ch];

	if (txch) {
		if (txch->bd_mem)
			txch->bd_mem = NULL;
		kfree(txch->tx_complete);
		kfree(txch);
		priv->txch[ch] = NULL;
	}
}

/**
 * emac_net_tx_complete: TX packet completion function
 * @priv: The DaVinci EMAC private adapter structure
 * @net_data_tokens: packet token - skb pointer
 * @num_tokens: number of skb's to free
 * @ch: TX channel number
 *
 * Frees the skb once packet is transmitted
 *
 */
static int emac_net_tx_complete(struct emac_priv *priv,
				void **net_data_tokens,
				int num_tokens, u32 ch)
{
	u32 cnt;

	if (unlikely(num_tokens && netif_queue_stopped(priv->ndev)))
		netif_start_queue(priv->ndev);
	for (cnt = 0; cnt < num_tokens; cnt++) {
		struct sk_buff *skb = (struct sk_buff *)net_data_tokens[cnt];
		if (skb == NULL)
			continue;
		priv->net_dev_stats.tx_packets++;
		priv->net_dev_stats.tx_bytes += skb->len;
		dev_kfree_skb_any(skb);
	}
	return 0;
}

/**
 * emac_txch_teardown: TX channel teardown
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: TX channel number
 *
 * Called to teardown TX channel
 *
 */
static void emac_txch_teardown(struct emac_priv *priv, u32 ch)
{
	struct device *emac_dev = &priv->ndev->dev;
	u32 teardown_cnt = 0xFFFFFFF0; /* Some high value */
	struct emac_txch *txch = priv->txch[ch];
	struct emac_tx_bd __iomem *curr_bd;

	while ((emac_read(EMAC_TXCP(ch)) & EMAC_TEARDOWN_VALUE) !=
	       EMAC_TEARDOWN_VALUE) {
		/* wait till tx teardown complete */
		cpu_relax(); /* TODO: check if this helps ... */
		--teardown_cnt;
		if (0 == teardown_cnt) {
			dev_err(emac_dev, "EMAC: TX teardown aborted\n");
			break;
		}
	}
	emac_write(EMAC_TXCP(ch), EMAC_TEARDOWN_VALUE);

	/* process sent packets and return skb's to upper layer */
	if (1 == txch->queue_active) {
		curr_bd = txch->active_queue_head;
		while (curr_bd != NULL) {
			emac_net_tx_complete(priv, (void __force *)
					&curr_bd->buf_token, 1, ch);
			if (curr_bd != txch->active_queue_tail)
				curr_bd = curr_bd->next;
			else
				break;
		}
		txch->bd_pool_head = txch->active_queue_head;
		txch->active_queue_head =
		txch->active_queue_tail = NULL;
	}
}

/**
 * emac_stop_txch: Stop TX channel operation
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: TX channel number
 *
 * Called to stop TX channel operation
 *
 */
static void emac_stop_txch(struct emac_priv *priv, u32 ch)
{
	struct emac_txch *txch = priv->txch[ch];

	if (txch) {
		txch->teardown_pending = 1;
		emac_write(EMAC_TXTEARDOWN, 0);
		emac_txch_teardown(priv, ch);
		txch->teardown_pending = 0;
		emac_write(EMAC_TXINTMASKCLEAR, BIT(ch));
	}
}

/**
 * emac_tx_bdproc: TX buffer descriptor (packet) processing
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: TX channel number to process buffer descriptors for
 * @budget: number of packets allowed to process
 * @pending: indication to caller that packets are pending to process
 *
 * Processes TX buffer descriptors after packets are transmitted - checks
 * ownership bit on the TX * descriptor and requeues it to free pool & frees
 * the SKB buffer. Only "budget" number of packets are processed and
 * indication of pending packets provided to the caller
 *
 * Returns number of packets processed
 */
static int emac_tx_bdproc(struct emac_priv *priv, u32 ch, u32 budget)
{
	struct device *emac_dev = &priv->ndev->dev;
	unsigned long flags;
	u32 frame_status;
	u32 pkts_processed = 0;
	u32 tx_complete_cnt = 0;
	struct emac_tx_bd __iomem *curr_bd;
	struct emac_txch *txch = priv->txch[ch];
	u32 *tx_complete_ptr = txch->tx_complete;

	if (unlikely(1 == txch->teardown_pending)) {
		if (netif_msg_tx_err(priv) && net_ratelimit()) {
			dev_err(emac_dev, "DaVinci EMAC:emac_tx_bdproc: "\
				"teardown pending\n");
		}
		return 0;  /* dont handle any pkt completions */
	}

	++txch->proc_count;
	spin_lock_irqsave(&priv->tx_lock, flags);
	curr_bd = txch->active_queue_head;
	if (NULL == curr_bd) {
		emac_write(EMAC_TXCP(ch),
			   emac_virt_to_phys(txch->last_hw_bdprocessed));
		txch->no_active_pkts++;
		spin_unlock_irqrestore(&priv->tx_lock, flags);
		return 0;
	}
	BD_CACHE_INVALIDATE(curr_bd, EMAC_BD_LENGTH_FOR_CACHE);
	frame_status = curr_bd->mode;
	while ((curr_bd) &&
	      ((frame_status & EMAC_CPPI_OWNERSHIP_BIT) == 0) &&
	      (pkts_processed < budget)) {
		emac_write(EMAC_TXCP(ch), emac_virt_to_phys(curr_bd));
		txch->active_queue_head = curr_bd->next;
		if (frame_status & EMAC_CPPI_EOQ_BIT) {
			if (curr_bd->next) {	/* misqueued packet */
				emac_write(EMAC_TXHDP(ch), curr_bd->h_next);
				++txch->mis_queued_packets;
			} else {
				txch->queue_active = 0; /* end of queue */
			}
		}
		*tx_complete_ptr = (u32) curr_bd->buf_token;
		++tx_complete_ptr;
		++tx_complete_cnt;
		curr_bd->next = txch->bd_pool_head;
		txch->bd_pool_head = curr_bd;
		--txch->active_queue_count;
		pkts_processed++;
		txch->last_hw_bdprocessed = curr_bd;
		curr_bd = txch->active_queue_head;
		if (curr_bd) {
			BD_CACHE_INVALIDATE(curr_bd, EMAC_BD_LENGTH_FOR_CACHE);
			frame_status = curr_bd->mode;
		}
	} /* end of pkt processing loop */

	emac_net_tx_complete(priv,
			     (void *)&txch->tx_complete[0],
			     tx_complete_cnt, ch);
	spin_unlock_irqrestore(&priv->tx_lock, flags);
	return pkts_processed;
}

#define EMAC_ERR_TX_OUT_OF_BD -1

/**
 * emac_send: EMAC Transmit function (internal)
 * @priv: The DaVinci EMAC private adapter structure
 * @pkt: packet pointer (contains skb ptr)
 * @ch: TX channel number
 *
 * Called by the transmit function to queue the packet in EMAC hardware queue
 *
 * Returns success(0) or error code (typically out of desc's)
 */
static int emac_send(struct emac_priv *priv, struct emac_netpktobj *pkt, u32 ch)
{
	unsigned long flags;
	struct emac_tx_bd __iomem *curr_bd;
	struct emac_txch *txch;
	struct emac_netbufobj *buf_list;

	txch = priv->txch[ch];
	buf_list = pkt->buf_list;   /* get handle to the buffer array */

	/* check packet size and pad if short */
	if (pkt->pkt_length < EMAC_DEF_MIN_ETHPKTSIZE) {
		buf_list->length += (EMAC_DEF_MIN_ETHPKTSIZE - pkt->pkt_length);
		pkt->pkt_length = EMAC_DEF_MIN_ETHPKTSIZE;
	}

	spin_lock_irqsave(&priv->tx_lock, flags);
	curr_bd = txch->bd_pool_head;
	if (curr_bd == NULL) {
		txch->out_of_tx_bd++;
		spin_unlock_irqrestore(&priv->tx_lock, flags);
		return EMAC_ERR_TX_OUT_OF_BD;
	}

	txch->bd_pool_head = curr_bd->next;
	curr_bd->buf_token = buf_list->buf_token;
	/* FIXME buff_ptr = dma_map_single(... data_ptr ...) */
	curr_bd->buff_ptr = virt_to_phys(buf_list->data_ptr);
	curr_bd->off_b_len = buf_list->length;
	curr_bd->h_next = 0;
	curr_bd->next = NULL;
	curr_bd->mode = (EMAC_CPPI_SOP_BIT | EMAC_CPPI_OWNERSHIP_BIT |
			 EMAC_CPPI_EOP_BIT | pkt->pkt_length);

	/* flush the packet from cache if write back cache is present */
	BD_CACHE_WRITEBACK_INVALIDATE(curr_bd, EMAC_BD_LENGTH_FOR_CACHE);

	/* send the packet */
	if (txch->active_queue_head == NULL) {
		txch->active_queue_head = curr_bd;
		txch->active_queue_tail = curr_bd;
		if (1 != txch->queue_active) {
			emac_write(EMAC_TXHDP(ch),
					emac_virt_to_phys(curr_bd));
			txch->queue_active = 1;
		}
		++txch->queue_reinit;
	} else {
		register struct emac_tx_bd __iomem *tail_bd;
		register u32 frame_status;

		tail_bd = txch->active_queue_tail;
		tail_bd->next = curr_bd;
		txch->active_queue_tail = curr_bd;
		tail_bd = EMAC_VIRT_NOCACHE(tail_bd);
		tail_bd->h_next = (int)emac_virt_to_phys(curr_bd);
		frame_status = tail_bd->mode;
		if (frame_status & EMAC_CPPI_EOQ_BIT) {
			emac_write(EMAC_TXHDP(ch), emac_virt_to_phys(curr_bd));
			frame_status &= ~(EMAC_CPPI_EOQ_BIT);
			tail_bd->mode = frame_status;
			++txch->end_of_queue_add;
		}
	}
	txch->active_queue_count++;
	spin_unlock_irqrestore(&priv->tx_lock, flags);
	return 0;
}

/**
 * emac_dev_xmit: EMAC Transmit function
 * @skb: SKB pointer
 * @ndev: The DaVinci EMAC network adapter
 *
 * Called by the system to transmit a packet  - we queue the packet in
 * EMAC hardware transmit queue
 *
 * Returns success(NETDEV_TX_OK) or error code (typically out of desc's)
 */
static int emac_dev_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct device *emac_dev = &ndev->dev;
	int ret_code;
	struct emac_netbufobj tx_buf; /* buffer obj-only single frame support */
	struct emac_netpktobj tx_packet;  /* packet object */
	struct emac_priv *priv = netdev_priv(ndev);

	/* If no link, return */
	if (unlikely(!priv->link)) {
		if (netif_msg_tx_err(priv) && net_ratelimit())
			dev_err(emac_dev, "DaVinci EMAC: No link to transmit");
		return NETDEV_TX_BUSY;
	}

	/* Build the buffer and packet objects - Since only single fragment is
	 * supported, need not set length and token in both packet & object.
	 * Doing so for completeness sake & to show that this needs to be done
	 * in multifragment case
	 */
	tx_packet.buf_list = &tx_buf;
	tx_packet.num_bufs = 1; /* only single fragment supported */
	tx_packet.pkt_length = skb->len;
	tx_packet.pkt_token = (void *)skb;
	tx_buf.length = skb->len;
	tx_buf.buf_token = (void *)skb;
	tx_buf.data_ptr = skb->data;
	EMAC_CACHE_WRITEBACK((unsigned long)skb->data, skb->len);
	ndev->trans_start = jiffies;
	ret_code = emac_send(priv, &tx_packet, EMAC_DEF_TX_CH);
	if (unlikely(ret_code != 0)) {
		if (ret_code == EMAC_ERR_TX_OUT_OF_BD) {
			if (netif_msg_tx_err(priv) && net_ratelimit())
				dev_err(emac_dev, "DaVinci EMAC: xmit() fatal"\
					" err. Out of TX BD's");
			netif_stop_queue(priv->ndev);
		}
		priv->net_dev_stats.tx_dropped++;
		return NETDEV_TX_BUSY;
	}

	return NETDEV_TX_OK;
}

/**
 * emac_dev_tx_timeout: EMAC Transmit timeout function
 * @ndev: The DaVinci EMAC network adapter
 *
 * Called when system detects that a skb timeout period has expired
 * potentially due to a fault in the adapter in not being able to send
 * it out on the wire. We teardown the TX channel assuming a hardware
 * error and re-initialize the TX channel for hardware operation
 *
 */
static void emac_dev_tx_timeout(struct net_device *ndev)
{
	struct emac_priv *priv = netdev_priv(ndev);
	struct device *emac_dev = &ndev->dev;

	if (netif_msg_tx_err(priv))
		dev_err(emac_dev, "DaVinci EMAC: xmit timeout, restarting TX");

	priv->net_dev_stats.tx_errors++;
	emac_int_disable(priv);
	emac_stop_txch(priv, EMAC_DEF_TX_CH);
	emac_cleanup_txch(priv, EMAC_DEF_TX_CH);
	emac_init_txch(priv, EMAC_DEF_TX_CH);
	emac_write(EMAC_TXHDP(0), 0);
	emac_write(EMAC_TXINTMASKSET, BIT(EMAC_DEF_TX_CH));
	emac_int_enable(priv);
}

/**
 * emac_net_alloc_rx_buf: Allocate a skb for RX
 * @priv: The DaVinci EMAC private adapter structure
 * @buf_size: size of SKB data buffer to allocate
 * @data_token: data token returned (skb handle for storing in buffer desc)
 * @ch: RX channel number
 *
 * Called during RX channel setup - allocates skb buffer of required size
 * and provides the skb handle and allocated buffer data pointer to caller
 *
 * Returns skb data pointer or 0 on failure to alloc skb
 */
static void *emac_net_alloc_rx_buf(struct emac_priv *priv, int buf_size,
		void **data_token, u32 ch)
{
	struct net_device *ndev = priv->ndev;
	struct device *emac_dev = &ndev->dev;
	struct sk_buff *p_skb;

	p_skb = dev_alloc_skb(buf_size);
	if (unlikely(NULL == p_skb)) {
		if (netif_msg_rx_err(priv) && net_ratelimit())
			dev_err(emac_dev, "DaVinci EMAC: failed to alloc skb");
		return NULL;
	}

	/* set device pointer in skb and reserve space for extra bytes */
	p_skb->dev = ndev;
	skb_reserve(p_skb, NET_IP_ALIGN);
	*data_token = (void *) p_skb;
	EMAC_CACHE_WRITEBACK_INVALIDATE((unsigned long)p_skb->data, buf_size);
	return p_skb->data;
}

/**
 * emac_init_rxch: RX channel initialization
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 * @param: mac address for RX channel
 *
 * Called during device init to setup a RX channel (allocate buffers and
 * buffer descriptors, create queue and keep ready for reception
 *
 * Returns success(0) or mem alloc failures error code
 */
static int emac_init_rxch(struct emac_priv *priv, u32 ch, char *param)
{
	struct device *emac_dev = &priv->ndev->dev;
	u32 cnt, bd_size;
	void __iomem *mem;
	struct emac_rx_bd __iomem *curr_bd;
	struct emac_rxch *rxch = NULL;

	rxch = kzalloc(sizeof(struct emac_rxch), GFP_KERNEL);
	if (NULL == rxch) {
		dev_err(emac_dev, "DaVinci EMAC: RX Ch mem alloc failed");
		return -ENOMEM;
	}
	priv->rxch[ch] = rxch;
	rxch->buf_size = priv->rx_buf_size;
	rxch->service_max = EMAC_DEF_RX_MAX_SERVICE;
	rxch->queue_active = 0;
	rxch->teardown_pending = 0;

	/* save mac address */
	for (cnt = 0; cnt < 6; cnt++)
		rxch->mac_addr[cnt] = param[cnt];

	/* allocate buffer descriptor pool align every BD on four word
	 * boundry for future requirements */
	bd_size = (sizeof(struct emac_rx_bd) + 0xF) & ~0xF;
	rxch->num_bd = (priv->ctrl_ram_size >> 1) / bd_size;
	rxch->alloc_size = (((bd_size * rxch->num_bd) + 0xF) & ~0xF);
	rxch->bd_mem = EMAC_RX_BD_MEM(priv);
	__memzero((void __force *)rxch->bd_mem, rxch->alloc_size);
	rxch->pkt_queue.buf_list = &rxch->buf_queue;

	/* allocate RX buffer and initialize the BD linked list */
	mem = (void __force __iomem *)
			(((u32 __force) rxch->bd_mem + 0xF) & ~0xF);
	rxch->active_queue_head = NULL;
	rxch->active_queue_tail = mem;
	for (cnt = 0; cnt < rxch->num_bd; cnt++) {
		curr_bd = mem + (cnt * bd_size);
		/* for future use the last parameter contains the BD ptr */
		curr_bd->data_ptr = emac_net_alloc_rx_buf(priv,
				    rxch->buf_size,
				    (void __force **)&curr_bd->buf_token,
				    EMAC_DEF_RX_CH);
		if (curr_bd->data_ptr == NULL) {
			dev_err(emac_dev, "DaVinci EMAC: RX buf mem alloc " \
				"failed for ch %d\n", ch);
			kfree(rxch);
			return -ENOMEM;
		}

		/* populate the hardware descriptor */
		curr_bd->h_next = emac_virt_to_phys(rxch->active_queue_head);
		/* FIXME buff_ptr = dma_map_single(... data_ptr ...) */
		curr_bd->buff_ptr = virt_to_phys(curr_bd->data_ptr);
		curr_bd->off_b_len = rxch->buf_size;
		curr_bd->mode = EMAC_CPPI_OWNERSHIP_BIT;

		/* write back to hardware memory */
		BD_CACHE_WRITEBACK_INVALIDATE((u32) curr_bd,
					      EMAC_BD_LENGTH_FOR_CACHE);
		curr_bd->next = rxch->active_queue_head;
		rxch->active_queue_head = curr_bd;
	}

	/* At this point rxCppi->activeQueueHead points to the first
	   RX BD ready to be given to RX HDP and rxch->active_queue_tail
	   points to the last RX BD
	 */
	return 0;
}

/**
 * emac_rxch_teardown: RX channel teardown
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 *
 * Called during device stop to teardown RX channel
 *
 */
static void emac_rxch_teardown(struct emac_priv *priv, u32 ch)
{
	struct device *emac_dev = &priv->ndev->dev;
	u32 teardown_cnt = 0xFFFFFFF0; /* Some high value */

	while ((emac_read(EMAC_RXCP(ch)) & EMAC_TEARDOWN_VALUE) !=
	       EMAC_TEARDOWN_VALUE) {
		/* wait till tx teardown complete */
		cpu_relax(); /* TODO: check if this helps ... */
		--teardown_cnt;
		if (0 == teardown_cnt) {
			dev_err(emac_dev, "EMAC: RX teardown aborted\n");
			break;
		}
	}
	emac_write(EMAC_RXCP(ch), EMAC_TEARDOWN_VALUE);
}

/**
 * emac_stop_rxch: Stop RX channel operation
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 *
 * Called during device stop to stop RX channel operation
 *
 */
static void emac_stop_rxch(struct emac_priv *priv, u32 ch)
{
	struct emac_rxch *rxch = priv->rxch[ch];

	if (rxch) {
		rxch->teardown_pending = 1;
		emac_write(EMAC_RXTEARDOWN, ch);
		/* wait for teardown complete */
		emac_rxch_teardown(priv, ch);
		rxch->teardown_pending = 0;
		emac_write(EMAC_RXINTMASKCLEAR, BIT(ch));
	}
}

/**
 * emac_cleanup_rxch: Book-keep function to clean RX channel resources
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 *
 * Called during device stop to clean up RX channel resources
 *
 */
static void emac_cleanup_rxch(struct emac_priv *priv, u32 ch)
{
	struct emac_rxch *rxch = priv->rxch[ch];
	struct emac_rx_bd __iomem *curr_bd;

	if (rxch) {
		/* free the receive buffers previously allocated */
		curr_bd = rxch->active_queue_head;
		while (curr_bd) {
			if (curr_bd->buf_token) {
				dev_kfree_skb_any((struct sk_buff *)\
						  curr_bd->buf_token);
			}
			curr_bd = curr_bd->next;
		}
		if (rxch->bd_mem)
			rxch->bd_mem = NULL;
		kfree(rxch);
		priv->rxch[ch] = NULL;
	}
}

/**
 * emac_set_type0addr: Set EMAC Type0 mac address
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 * @mac_addr: MAC address to set in device
 *
 * Called internally to set Type0 mac address of the adapter (Device)
 *
 * Returns success (0) or appropriate error code (none as of now)
 */
static void emac_set_type0addr(struct emac_priv *priv, u32 ch, char *mac_addr)
{
	u32 val;
	val = ((mac_addr[5] << 8) | (mac_addr[4]));
	emac_write(EMAC_MACSRCADDRLO, val);

	val = ((mac_addr[3] << 24) | (mac_addr[2] << 16) | \
	       (mac_addr[1] << 8) | (mac_addr[0]));
	emac_write(EMAC_MACSRCADDRHI, val);
	val = emac_read(EMAC_RXUNICASTSET);
	val |= BIT(ch);
	emac_write(EMAC_RXUNICASTSET, val);
	val = emac_read(EMAC_RXUNICASTCLEAR);
	val &= ~BIT(ch);
	emac_write(EMAC_RXUNICASTCLEAR, val);
}

/**
 * emac_set_type1addr: Set EMAC Type1 mac address
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 * @mac_addr: MAC address to set in device
 *
 * Called internally to set Type1 mac address of the adapter (Device)
 *
 * Returns success (0) or appropriate error code (none as of now)
 */
static void emac_set_type1addr(struct emac_priv *priv, u32 ch, char *mac_addr)
{
	u32 val;
	emac_write(EMAC_MACINDEX, ch);
	val = ((mac_addr[5] << 8) | mac_addr[4]);
	emac_write(EMAC_MACADDRLO, val);
	val = ((mac_addr[3] << 24) | (mac_addr[2] << 16) | \
	       (mac_addr[1] << 8) | (mac_addr[0]));
	emac_write(EMAC_MACADDRHI, val);
	emac_set_type0addr(priv, ch, mac_addr);
}

/**
 * emac_set_type2addr: Set EMAC Type2 mac address
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 * @mac_addr: MAC address to set in device
 * @index: index into RX address entries
 * @match: match parameter for RX address matching logic
 *
 * Called internally to set Type2 mac address of the adapter (Device)
 *
 * Returns success (0) or appropriate error code (none as of now)
 */
static void emac_set_type2addr(struct emac_priv *priv, u32 ch,
			       char *mac_addr, int index, int match)
{
	u32 val;
	emac_write(EMAC_MACINDEX, index);
	val = ((mac_addr[3] << 24) | (mac_addr[2] << 16) | \
	       (mac_addr[1] << 8) | (mac_addr[0]));
	emac_write(EMAC_MACADDRHI, val);
	val = ((mac_addr[5] << 8) | mac_addr[4] | ((ch & 0x7) << 16) | \
	       (match << 19) | BIT(20));
	emac_write(EMAC_MACADDRLO, val);
	emac_set_type0addr(priv, ch, mac_addr);
}

/**
 * emac_setmac: Set mac address in the adapter (internal function)
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number
 * @mac_addr: MAC address to set in device
 *
 * Called internally to set the mac address of the adapter (Device)
 *
 * Returns success (0) or appropriate error code (none as of now)
 */
static void emac_setmac(struct emac_priv *priv, u32 ch, char *mac_addr)
{
	struct device *emac_dev = &priv->ndev->dev;

	if (priv->rx_addr_type == 0) {
		emac_set_type0addr(priv, ch, mac_addr);
	} else if (priv->rx_addr_type == 1) {
		u32 cnt;
		for (cnt = 0; cnt < EMAC_MAX_TXRX_CHANNELS; cnt++)
			emac_set_type1addr(priv, ch, mac_addr);
	} else if (priv->rx_addr_type == 2) {
		emac_set_type2addr(priv, ch, mac_addr, ch, 1);
		emac_set_type0addr(priv, ch, mac_addr);
	} else {
		if (netif_msg_drv(priv))
			dev_err(emac_dev, "DaVinci EMAC: Wrong addressing\n");
	}
}

/**
 * emac_dev_setmac_addr: Set mac address in the adapter
 * @ndev: The DaVinci EMAC network adapter
 * @addr: MAC address to set in device
 *
 * Called by the system to set the mac address of the adapter (Device)
 *
 * Returns success (0) or appropriate error code (none as of now)
 */
static int emac_dev_setmac_addr(struct net_device *ndev, void *addr)
{
	struct emac_priv *priv = netdev_priv(ndev);
	struct emac_rxch *rxch = priv->rxch[EMAC_DEF_RX_CH];
	struct device *emac_dev = &priv->ndev->dev;
	struct sockaddr *sa = addr;

	if (!is_valid_ether_addr(sa->sa_data))
		return -EINVAL;

	/* Store mac addr in priv and rx channel and set it in EMAC hw */
	memcpy(priv->mac_addr, sa->sa_data, ndev->addr_len);
	memcpy(ndev->dev_addr, sa->sa_data, ndev->addr_len);

	/* If the interface is down - rxch is NULL. */
	/* MAC address is configured only after the interface is enabled. */
	if (netif_running(ndev)) {
		memcpy(rxch->mac_addr, sa->sa_data, ndev->addr_len);
		emac_setmac(priv, EMAC_DEF_RX_CH, rxch->mac_addr);
	}

	if (netif_msg_drv(priv))
		dev_notice(emac_dev, "DaVinci EMAC: emac_dev_setmac_addr %pM\n",
					priv->mac_addr);

	return 0;
}

/**
 * emac_addbd_to_rx_queue: Recycle RX buffer descriptor
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number to process buffer descriptors for
 * @curr_bd: current buffer descriptor
 * @buffer: buffer pointer for descriptor
 * @buf_token: buffer token (stores skb information)
 *
 * Prepares the recycled buffer descriptor and addes it to hardware
 * receive queue - if queue empty this descriptor becomes the head
 * else addes the descriptor to end of queue
 *
 */
static void emac_addbd_to_rx_queue(struct emac_priv *priv, u32 ch,
		struct emac_rx_bd __iomem *curr_bd,
		char *buffer, void *buf_token)
{
	struct emac_rxch *rxch = priv->rxch[ch];

	/* populate the hardware descriptor */
	curr_bd->h_next = 0;
	/* FIXME buff_ptr = dma_map_single(... buffer ...) */
	curr_bd->buff_ptr = virt_to_phys(buffer);
	curr_bd->off_b_len = rxch->buf_size;
	curr_bd->mode = EMAC_CPPI_OWNERSHIP_BIT;
	curr_bd->next = NULL;
	curr_bd->data_ptr = buffer;
	curr_bd->buf_token = buf_token;

	/* write back  */
	BD_CACHE_WRITEBACK_INVALIDATE(curr_bd, EMAC_BD_LENGTH_FOR_CACHE);
	if (rxch->active_queue_head == NULL) {
		rxch->active_queue_head = curr_bd;
		rxch->active_queue_tail = curr_bd;
		if (0 != rxch->queue_active) {
			emac_write(EMAC_RXHDP(ch),
				   emac_virt_to_phys(rxch->active_queue_head));
			rxch->queue_active = 1;
		}
	} else {
		struct emac_rx_bd __iomem *tail_bd;
		u32 frame_status;

		tail_bd = rxch->active_queue_tail;
		rxch->active_queue_tail = curr_bd;
		tail_bd->next = curr_bd;
		tail_bd = EMAC_VIRT_NOCACHE(tail_bd);
		tail_bd->h_next = emac_virt_to_phys(curr_bd);
		frame_status = tail_bd->mode;
		if (frame_status & EMAC_CPPI_EOQ_BIT) {
			emac_write(EMAC_RXHDP(ch),
					emac_virt_to_phys(curr_bd));
			frame_status &= ~(EMAC_CPPI_EOQ_BIT);
			tail_bd->mode = frame_status;
			++rxch->end_of_queue_add;
		}
	}
	++rxch->recycled_bd;
}

/**
 * emac_net_rx_cb: Prepares packet and sends to upper layer
 * @priv: The DaVinci EMAC private adapter structure
 * @net_pkt_list: Network packet list (received packets)
 *
 * Invalidates packet buffer memory and sends the received packet to upper
 * layer
 *
 * Returns success or appropriate error code (none as of now)
 */
static int emac_net_rx_cb(struct emac_priv *priv,
			  struct emac_netpktobj *net_pkt_list)
{
	struct sk_buff *p_skb;
	p_skb = (struct sk_buff *)net_pkt_list->pkt_token;
	/* set length of packet */
	skb_put(p_skb, net_pkt_list->pkt_length);
	EMAC_CACHE_INVALIDATE((unsigned long)p_skb->data, p_skb->len);
	p_skb->protocol = eth_type_trans(p_skb, priv->ndev);
	p_skb->dev->last_rx = jiffies;
	netif_receive_skb(p_skb);
	priv->net_dev_stats.rx_bytes += net_pkt_list->pkt_length;
	priv->net_dev_stats.rx_packets++;
	return 0;
}

/**
 * emac_rx_bdproc: RX buffer descriptor (packet) processing
 * @priv: The DaVinci EMAC private adapter structure
 * @ch: RX channel number to process buffer descriptors for
 * @budget: number of packets allowed to process
 * @pending: indication to caller that packets are pending to process
 *
 * Processes RX buffer descriptors - checks ownership bit on the RX buffer
 * descriptor, sends the receive packet to upper layer, allocates a new SKB
 * and recycles the buffer descriptor (requeues it in hardware RX queue).
 * Only "budget" number of packets are processed and indication of pending
 * packets provided to the caller.
 *
 * Returns number of packets processed (and indication of pending packets)
 */
static int emac_rx_bdproc(struct emac_priv *priv, u32 ch, u32 budget)
{
	unsigned long flags;
	u32 frame_status;
	u32 pkts_processed = 0;
	char *new_buffer;
	struct emac_rx_bd __iomem *curr_bd;
	struct emac_rx_bd __iomem *last_bd;
	struct emac_netpktobj *curr_pkt, pkt_obj;
	struct emac_netbufobj buf_obj;
	struct emac_netbufobj *rx_buf_obj;
	void *new_buf_token;
	struct emac_rxch *rxch = priv->rxch[ch];

	if (unlikely(1 == rxch->teardown_pending))
		return 0;
	++rxch->proc_count;
	spin_lock_irqsave(&priv->rx_lock, flags);
	pkt_obj.buf_list = &buf_obj;
	curr_pkt = &pkt_obj;
	curr_bd = rxch->active_queue_head;
	BD_CACHE_INVALIDATE(curr_bd, EMAC_BD_LENGTH_FOR_CACHE);
	frame_status = curr_bd->mode;

	while ((curr_bd) &&
	       ((frame_status & EMAC_CPPI_OWNERSHIP_BIT) == 0) &&
	       (pkts_processed < budget)) {

		new_buffer = emac_net_alloc_rx_buf(priv, rxch->buf_size,
					&new_buf_token, EMAC_DEF_RX_CH);
		if (unlikely(NULL == new_buffer)) {
			++rxch->out_of_rx_buffers;
			goto end_emac_rx_bdproc;
		}

		/* populate received packet data structure */
		rx_buf_obj = &curr_pkt->buf_list[0];
		rx_buf_obj->data_ptr = (char *)curr_bd->data_ptr;
		rx_buf_obj->length = curr_bd->off_b_len & EMAC_RX_BD_BUF_SIZE;
		rx_buf_obj->buf_token = curr_bd->buf_token;
		curr_pkt->pkt_token = curr_pkt->buf_list->buf_token;
		curr_pkt->num_bufs = 1;
		curr_pkt->pkt_length =
			(frame_status & EMAC_RX_BD_PKT_LENGTH_MASK);
		emac_write(EMAC_RXCP(ch), emac_virt_to_phys(curr_bd));
		++rxch->processed_bd;
		last_bd = curr_bd;
		curr_bd = last_bd->next;
		rxch->active_queue_head = curr_bd;

		/* check if end of RX queue ? */
		if (frame_status & EMAC_CPPI_EOQ_BIT) {
			if (curr_bd) {
				++rxch->mis_queued_packets;
				emac_write(EMAC_RXHDP(ch),
					   emac_virt_to_phys(curr_bd));
			} else {
				++rxch->end_of_queue;
				rxch->queue_active = 0;
			}
		}

		/* recycle BD */
		emac_addbd_to_rx_queue(priv, ch, last_bd, new_buffer,
				       new_buf_token);

		/* return the packet to the user - BD ptr passed in
		 * last parameter for potential *future* use */
		spin_unlock_irqrestore(&priv->rx_lock, flags);
		emac_net_rx_cb(priv, curr_pkt);
		spin_lock_irqsave(&priv->rx_lock, flags);
		curr_bd = rxch->active_queue_head;
		if (curr_bd) {
			BD_CACHE_INVALIDATE(curr_bd, EMAC_BD_LENGTH_FOR_CACHE);
			frame_status = curr_bd->mode;
		}
		++pkts_processed;
	}

end_emac_rx_bdproc:
	spin_unlock_irqrestore(&priv->rx_lock, flags);
	return pkts_processed;
}

/**
 * emac_hw_enable: Enable EMAC hardware for packet transmission/reception
 * @priv: The DaVinci EMAC private adapter structure
 *
 * Enables EMAC hardware for packet processing - enables PHY, enables RX
 * for packet reception and enables device interrupts and then NAPI
 *
 * Returns success (0) or appropriate error code (none right now)
 */
static int emac_hw_enable(struct emac_priv *priv)
{
	u32 ch, val, mbp_enable, mac_control;

	/* Soft reset */
	emac_write(EMAC_SOFTRESET, 1);
	while (emac_read(EMAC_SOFTRESET))
		cpu_relax();

	/* Disable interrupt & Set pacing for more interrupts initially */
	emac_int_disable(priv);

	/* Full duplex enable bit set when auto negotiation happens */
	mac_control =
		(((EMAC_DEF_TXPRIO_FIXED) ? (EMAC_MACCONTROL_TXPTYPE) : 0x0) |
		((priv->speed == 1000) ? EMAC_MACCONTROL_GIGABITEN : 0x0) |
		((EMAC_DEF_TXPACING_EN) ? (EMAC_MACCONTROL_TXPACEEN) : 0x0) |
		((priv->duplex == DUPLEX_FULL) ? 0x1 : 0));
	emac_write(EMAC_MACCONTROL, mac_control);

	mbp_enable =
		(((EMAC_DEF_PASS_CRC) ? (EMAC_RXMBP_PASSCRC_MASK) : 0x0) |
		((EMAC_DEF_QOS_EN) ? (EMAC_RXMBP_QOSEN_MASK) : 0x0) |
		 ((EMAC_DEF_NO_BUFF_CHAIN) ? (EMAC_RXMBP_NOCHAIN_MASK) : 0x0) |
		 ((EMAC_DEF_MACCTRL_FRAME_EN) ? (EMAC_RXMBP_CMFEN_MASK) : 0x0) |
		 ((EMAC_DEF_SHORT_FRAME_EN) ? (EMAC_RXMBP_CSFEN_MASK) : 0x0) |
		 ((EMAC_DEF_ERROR_FRAME_EN) ? (EMAC_RXMBP_CEFEN_MASK) : 0x0) |
		 ((EMAC_DEF_PROM_EN) ? (EMAC_RXMBP_CAFEN_MASK) : 0x0) |
		 ((EMAC_DEF_PROM_CH & EMAC_RXMBP_CHMASK) << \
			EMAC_RXMBP_PROMCH_SHIFT) |
		 ((EMAC_DEF_BCAST_EN) ? (EMAC_RXMBP_BROADEN_MASK) : 0x0) |
		 ((EMAC_DEF_BCAST_CH & EMAC_RXMBP_CHMASK) << \
			EMAC_RXMBP_BROADCH_SHIFT) |
		 ((EMAC_DEF_MCAST_EN) ? (EMAC_RXMBP_MULTIEN_MASK) : 0x0) |
		 ((EMAC_DEF_MCAST_CH & EMAC_RXMBP_CHMASK) << \
			EMAC_RXMBP_MULTICH_SHIFT));
	emac_write(EMAC_RXMBPENABLE, mbp_enable);
	emac_write(EMAC_RXMAXLEN, (EMAC_DEF_MAX_FRAME_SIZE &
				   EMAC_RX_MAX_LEN_MASK));
	emac_write(EMAC_RXBUFFEROFFSET, (EMAC_DEF_BUFFER_OFFSET &
					 EMAC_RX_BUFFER_OFFSET_MASK));
	emac_write(EMAC_RXFILTERLOWTHRESH, 0);
	emac_write(EMAC_RXUNICASTCLEAR, EMAC_RX_UNICAST_CLEAR_ALL);
	priv->rx_addr_type = (emac_read(EMAC_MACCONFIG) >> 8) & 0xFF;

	val = emac_read(EMAC_TXCONTROL);
	val |= EMAC_TX_CONTROL_TX_ENABLE_VAL;
	emac_write(EMAC_TXCONTROL, val);
	val = emac_read(EMAC_RXCONTROL);
	val |= EMAC_RX_CONTROL_RX_ENABLE_VAL;
	emac_write(EMAC_RXCONTROL, val);
	emac_write(EMAC_MACINTMASKSET, EMAC_MAC_HOST_ERR_INTMASK_VAL);

	for (ch = 0; ch < EMAC_DEF_MAX_TX_CH; ch++) {
		emac_write(EMAC_TXHDP(ch), 0);
		emac_write(EMAC_TXINTMASKSET, BIT(ch));
	}
	for (ch = 0; ch < EMAC_DEF_MAX_RX_CH; ch++) {
		struct emac_rxch *rxch = priv->rxch[ch];
		emac_setmac(priv, ch, rxch->mac_addr);
		emac_write(EMAC_RXINTMASKSET, BIT(ch));
		rxch->queue_active = 1;
		emac_write(EMAC_RXHDP(ch),
			   emac_virt_to_phys(rxch->active_queue_head));
	}

	/* Enable MII */
	val = emac_read(EMAC_MACCONTROL);
	val |= (EMAC_MACCONTROL_MIIEN);
	emac_write(EMAC_MACCONTROL, val);

	/* Enable NAPI and interrupts */
	napi_enable(&priv->napi);
	emac_int_enable(priv);
	return 0;

}

/**
 * emac_poll: EMAC NAPI Poll function
 * @ndev: The DaVinci EMAC network adapter
 * @budget: Number of receive packets to process (as told by NAPI layer)
 *
 * NAPI Poll function implemented to process packets as per budget. We check
 * the type of interrupt on the device and accordingly call the TX or RX
 * packet processing functions. We follow the budget for RX processing and
 * also put a cap on number of TX pkts processed through config param. The
 * NAPI schedule function is called if more packets pending.
 *
 * Returns number of packets received (in most cases; else TX pkts - rarely)
 */
static int emac_poll(struct napi_struct *napi, int budget)
{
	unsigned int mask;
	struct emac_priv *priv = container_of(napi, struct emac_priv, napi);
	struct net_device *ndev = priv->ndev;
	struct device *emac_dev = &ndev->dev;
	u32 status = 0;
	u32 num_pkts = 0;

	if (!netif_running(ndev))
		return 0;

	/* Check interrupt vectors and call packet processing */
	status = emac_read(EMAC_MACINVECTOR);

	mask = EMAC_DM644X_MAC_IN_VECTOR_TX_INT_VEC;

	if (priv->version == EMAC_VERSION_2)
		mask = EMAC_DM646X_MAC_IN_VECTOR_TX_INT_VEC;

	if (status & mask) {
		num_pkts = emac_tx_bdproc(priv, EMAC_DEF_TX_CH,
					  EMAC_DEF_TX_MAX_SERVICE);
	} /* TX processing */

	if (num_pkts)
		return budget;

	mask = EMAC_DM644X_MAC_IN_VECTOR_RX_INT_VEC;

	if (priv->version == EMAC_VERSION_2)
		mask = EMAC_DM646X_MAC_IN_VECTOR_RX_INT_VEC;

	if (status & mask) {
		num_pkts = emac_rx_bdproc(priv, EMAC_DEF_RX_CH, budget);
	} /* RX processing */

	if (num_pkts < budget) {
		napi_complete(napi);
		emac_int_enable(priv);
	}

	if (unlikely(status & EMAC_DM644X_MAC_IN_VECTOR_HOST_INT)) {
		u32 ch, cause;
		dev_err(emac_dev, "DaVinci EMAC: Fatal Hardware Error\n");
		netif_stop_queue(ndev);
		napi_disable(&priv->napi);

		status = emac_read(EMAC_MACSTATUS);
		cause = ((status & EMAC_MACSTATUS_TXERRCODE_MASK) >>
			 EMAC_MACSTATUS_TXERRCODE_SHIFT);
		if (cause) {
			ch = ((status & EMAC_MACSTATUS_TXERRCH_MASK) >>
			      EMAC_MACSTATUS_TXERRCH_SHIFT);
			if (net_ratelimit()) {
				dev_err(emac_dev, "TX Host error %s on ch=%d\n",
					&emac_txhost_errcodes[cause][0], ch);
			}
		}
		cause = ((status & EMAC_MACSTATUS_RXERRCODE_MASK) >>
			 EMAC_MACSTATUS_RXERRCODE_SHIFT);
		if (cause) {
			ch = ((status & EMAC_MACSTATUS_RXERRCH_MASK) >>
			      EMAC_MACSTATUS_RXERRCH_SHIFT);
			if (netif_msg_hw(priv) && net_ratelimit())
				dev_err(emac_dev, "RX Host error %s on ch=%d\n",
					&emac_rxhost_errcodes[cause][0], ch);
		}
	} /* Host error processing */

	return num_pkts;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 * emac_poll_controller: EMAC Poll controller function
 * @ndev: The DaVinci EMAC network adapter
 *
 * Polled functionality used by netconsole and others in non interrupt mode
 *
 */
void emac_poll_controller(struct net_device *ndev)
{
	struct emac_priv *priv = netdev_priv(ndev);

	emac_int_disable(priv);
	emac_irq(ndev->irq, priv);
	emac_int_enable(priv);
}
#endif

/* PHY/MII bus related */

/* Wait until mdio is ready for next command */
#define MDIO_WAIT_FOR_USER_ACCESS\
		while ((emac_mdio_read((MDIO_USERACCESS(0))) &\
			MDIO_USERACCESS_GO) != 0)

static int emac_mii_read(struct mii_bus *bus, int phy_id, int phy_reg)
{
	unsigned int phy_data = 0;
	unsigned int phy_control;

	/* Wait until mdio is ready for next command */
	MDIO_WAIT_FOR_USER_ACCESS;

	phy_control = (MDIO_USERACCESS_GO |
		       MDIO_USERACCESS_READ |
		       ((phy_reg << 21) & MDIO_USERACCESS_REGADR) |
		       ((phy_id << 16) & MDIO_USERACCESS_PHYADR) |
		       (phy_data & MDIO_USERACCESS_DATA));
	emac_mdio_write(MDIO_USERACCESS(0), phy_control);

	/* Wait until mdio is ready for next command */
	MDIO_WAIT_FOR_USER_ACCESS;

	return emac_mdio_read(MDIO_USERACCESS(0)) & MDIO_USERACCESS_DATA;

}

static int emac_mii_write(struct mii_bus *bus, int phy_id,
			  int phy_reg, u16 phy_data)
{

	unsigned int control;

	/*  until mdio is ready for next command */
	MDIO_WAIT_FOR_USER_ACCESS;

	control = (MDIO_USERACCESS_GO |
		   MDIO_USERACCESS_WRITE |
		   ((phy_reg << 21) & MDIO_USERACCESS_REGADR) |
		   ((phy_id << 16) & MDIO_USERACCESS_PHYADR) |
		   (phy_data & MDIO_USERACCESS_DATA));
	emac_mdio_write(MDIO_USERACCESS(0), control);

	return 0;
}

static int emac_mii_reset(struct mii_bus *bus)
{
	unsigned int clk_div;
	int mdio_bus_freq = emac_bus_frequency;

	if (mdio_max_freq & mdio_bus_freq)
		clk_div = ((mdio_bus_freq / mdio_max_freq) - 1);
	else
		clk_div = 0xFF;

	clk_div &= MDIO_CONTROL_CLKDIV;

	/* Set enable and clock divider in MDIOControl */
	emac_mdio_write(MDIO_CONTROL, (clk_div | MDIO_CONTROL_ENABLE));

	return 0;

}

static int mii_irqs[PHY_MAX_ADDR] = { PHY_POLL, PHY_POLL };

/* emac_driver: EMAC MII bus structure */

static struct mii_bus *emac_mii;

static void emac_adjust_link(struct net_device *ndev)
{
	struct emac_priv *priv = netdev_priv(ndev);
	struct phy_device *phydev = priv->phydev;
	unsigned long flags;
	int new_state = 0;

	spin_lock_irqsave(&priv->lock, flags);

	if (phydev->link) {
		/* check the mode of operation - full/half duplex */
		if (phydev->duplex != priv->duplex) {
			new_state = 1;
			priv->duplex = phydev->duplex;
		}
		if (phydev->speed != priv->speed) {
			new_state = 1;
			priv->speed = phydev->speed;
		}
		if (!priv->link) {
			new_state = 1;
			priv->link = 1;
		}

	} else if (priv->link) {
		new_state = 1;
		priv->link = 0;
		priv->speed = 0;
		priv->duplex = ~0;
	}
	if (new_state) {
		emac_update_phystatus(priv);
		phy_print_status(priv->phydev);
	}

	spin_unlock_irqrestore(&priv->lock, flags);
}

/*************************************************************************
 *  Linux Driver Model
 *************************************************************************/

/**
 * emac_devioctl: EMAC adapter ioctl
 * @ndev: The DaVinci EMAC network adapter
 * @ifrq: request parameter
 * @cmd: command parameter
 *
 * EMAC driver ioctl function
 *
 * Returns success(0) or appropriate error code
 */
static int emac_devioctl(struct net_device *ndev, struct ifreq *ifrq, int cmd)
{
	dev_warn(&ndev->dev, "DaVinci EMAC: ioctl not supported\n");

	if (!(netif_running(ndev)))
		return -EINVAL;

	/* TODO: Add phy read and write and private statistics get feature */

	return -EOPNOTSUPP;
}

/**
 * emac_dev_open: EMAC device open
 * @ndev: The DaVinci EMAC network adapter
 *
 * Called when system wants to start the interface. We init TX/RX channels
 * and enable the hardware for packet reception/transmission and start the
 * network queue.
 *
 * Returns 0 for a successful open, or appropriate error code
 */
static int emac_dev_open(struct net_device *ndev)
{
	struct device *emac_dev = &ndev->dev;
	u32 rc, cnt, ch;
	int phy_addr;
	struct resource *res;
	int q, m;
	int i = 0;
	int k = 0;
	struct emac_priv *priv = netdev_priv(ndev);

	netif_carrier_off(ndev);
	for (cnt = 0; cnt <= ETH_ALEN; cnt++)
		ndev->dev_addr[cnt] = priv->mac_addr[cnt];

	/* Configuration items */
	priv->rx_buf_size = EMAC_DEF_MAX_FRAME_SIZE + NET_IP_ALIGN;

	/* Clear basic hardware */
	for (ch = 0; ch < EMAC_MAX_TXRX_CHANNELS; ch++) {
		emac_write(EMAC_TXHDP(ch), 0);
		emac_write(EMAC_RXHDP(ch), 0);
		emac_write(EMAC_RXHDP(ch), 0);
		emac_write(EMAC_RXINTMASKCLEAR, EMAC_INT_MASK_CLEAR);
		emac_write(EMAC_TXINTMASKCLEAR, EMAC_INT_MASK_CLEAR);
	}
	priv->mac_hash1 = 0;
	priv->mac_hash2 = 0;
	emac_write(EMAC_MACHASH1, 0);
	emac_write(EMAC_MACHASH2, 0);

	/* multi ch not supported - open 1 TX, 1RX ch by default */
	rc = emac_init_txch(priv, EMAC_DEF_TX_CH);
	if (0 != rc) {
		dev_err(emac_dev, "DaVinci EMAC: emac_init_txch() failed");
		return rc;
	}
	rc = emac_init_rxch(priv, EMAC_DEF_RX_CH, priv->mac_addr);
	if (0 != rc) {
		dev_err(emac_dev, "DaVinci EMAC: emac_init_rxch() failed");
		return rc;
	}

	/* Request IRQ */

	while ((res = platform_get_resource(priv->pdev, IORESOURCE_IRQ, k))) {
		for (i = res->start; i <= res->end; i++) {
			if (request_irq(i, emac_irq, IRQF_DISABLED,
					ndev->name, ndev))
				goto rollback;
		}
		k++;
	}

	/* Start/Enable EMAC hardware */
	emac_hw_enable(priv);

	/* find the first phy */
	priv->phydev = NULL;
	if (priv->phy_mask) {
		emac_mii_reset(priv->mii_bus);
		for (phy_addr = 0; phy_addr < PHY_MAX_ADDR; phy_addr++) {
			if (priv->mii_bus->phy_map[phy_addr]) {
				priv->phydev = priv->mii_bus->phy_map[phy_addr];
				break;
			}
		}

		if (!priv->phydev) {
			printk(KERN_ERR "%s: no PHY found\n", ndev->name);
			return -1;
		}

		priv->phydev = phy_connect(ndev, dev_name(&priv->phydev->dev),
				&emac_adjust_link, 0, PHY_INTERFACE_MODE_MII);

		if (IS_ERR(priv->phydev)) {
			printk(KERN_ERR "%s: Could not attach to PHY\n",
								ndev->name);
			return PTR_ERR(priv->phydev);
		}

		priv->link = 0;
		priv->speed = 0;
		priv->duplex = ~0;

		printk(KERN_INFO "%s: attached PHY driver [%s] "
			"(mii_bus:phy_addr=%s, id=%x)\n", ndev->name,
			priv->phydev->drv->name, dev_name(&priv->phydev->dev),
			priv->phydev->phy_id);
	} else{
		/* No PHY , fix the link, speed and duplex settings */
		priv->link = 1;
		priv->speed = SPEED_100;
		priv->duplex = DUPLEX_FULL;
		emac_update_phystatus(priv);
	}

	if (!netif_running(ndev)) /* debug only - to avoid compiler warning */
		emac_dump_regs(priv);

	if (netif_msg_drv(priv))
		dev_notice(emac_dev, "DaVinci EMAC: Opened %s\n", ndev->name);

	if (priv->phy_mask)
		phy_start(priv->phydev);

	return 0;

rollback:

	dev_err(emac_dev, "DaVinci EMAC: request_irq() failed");

	for (q = k; k >= 0; k--) {
		for (m = i; m >= res->start; m--)
			free_irq(m, ndev);
		res = platform_get_resource(priv->pdev, IORESOURCE_IRQ, k-1);
		m = res->end;
	}
	return -EBUSY;
}

/**
 * emac_dev_stop: EMAC device stop
 * @ndev: The DaVinci EMAC network adapter
 *
 * Called when system wants to stop or down the interface. We stop the network
 * queue, disable interrupts and cleanup TX/RX channels.
 *
 * We return the statistics in net_device_stats structure pulled from emac
 */
static int emac_dev_stop(struct net_device *ndev)
{
	struct resource *res;
	int i = 0;
	int irq_num;
	struct emac_priv *priv = netdev_priv(ndev);
	struct device *emac_dev = &ndev->dev;

	/* inform the upper layers. */
	netif_stop_queue(ndev);
	napi_disable(&priv->napi);

	netif_carrier_off(ndev);
	emac_int_disable(priv);
	emac_stop_txch(priv, EMAC_DEF_TX_CH);
	emac_stop_rxch(priv, EMAC_DEF_RX_CH);
	emac_cleanup_txch(priv, EMAC_DEF_TX_CH);
	emac_cleanup_rxch(priv, EMAC_DEF_RX_CH);
	emac_write(EMAC_SOFTRESET, 1);

	if (priv->phydev)
		phy_disconnect(priv->phydev);

	/* Free IRQ */
	while ((res = platform_get_resource(priv->pdev, IORESOURCE_IRQ, i))) {
		for (irq_num = res->start; irq_num <= res->end; irq_num++)
			free_irq(irq_num, priv->ndev);
		i++;
	}

	if (netif_msg_drv(priv))
		dev_notice(emac_dev, "DaVinci EMAC: %s stopped\n", ndev->name);

	return 0;
}

/**
 * emac_dev_getnetstats: EMAC get statistics function
 * @ndev: The DaVinci EMAC network adapter
 *
 * Called when system wants to get statistics from the device.
 *
 * We return the statistics in net_device_stats structure pulled from emac
 */
static struct net_device_stats *emac_dev_getnetstats(struct net_device *ndev)
{
	struct emac_priv *priv = netdev_priv(ndev);

	/* update emac hardware stats and reset the registers*/

	priv->net_dev_stats.multicast += emac_read(EMAC_RXMCASTFRAMES);
	emac_write(EMAC_RXMCASTFRAMES, EMAC_ALL_MULTI_REG_VALUE);

	priv->net_dev_stats.collisions += (emac_read(EMAC_TXCOLLISION) +
					   emac_read(EMAC_TXSINGLECOLL) +
					   emac_read(EMAC_TXMULTICOLL));
	emac_write(EMAC_TXCOLLISION, EMAC_ALL_MULTI_REG_VALUE);
	emac_write(EMAC_TXSINGLECOLL, EMAC_ALL_MULTI_REG_VALUE);
	emac_write(EMAC_TXMULTICOLL, EMAC_ALL_MULTI_REG_VALUE);

	priv->net_dev_stats.rx_length_errors += (emac_read(EMAC_RXOVERSIZED) +
						emac_read(EMAC_RXJABBER) +
						emac_read(EMAC_RXUNDERSIZED));
	emac_write(EMAC_RXOVERSIZED, EMAC_ALL_MULTI_REG_VALUE);
	emac_write(EMAC_RXJABBER, EMAC_ALL_MULTI_REG_VALUE);
	emac_write(EMAC_RXUNDERSIZED, EMAC_ALL_MULTI_REG_VALUE);

	priv->net_dev_stats.rx_over_errors += (emac_read(EMAC_RXSOFOVERRUNS) +
					       emac_read(EMAC_RXMOFOVERRUNS));
	emac_write(EMAC_RXSOFOVERRUNS, EMAC_ALL_MULTI_REG_VALUE);
	emac_write(EMAC_RXMOFOVERRUNS, EMAC_ALL_MULTI_REG_VALUE);

	priv->net_dev_stats.rx_fifo_errors += emac_read(EMAC_RXDMAOVERRUNS);
	emac_write(EMAC_RXDMAOVERRUNS, EMAC_ALL_MULTI_REG_VALUE);

	priv->net_dev_stats.tx_carrier_errors +=
		emac_read(EMAC_TXCARRIERSENSE);
	emac_write(EMAC_TXCARRIERSENSE, EMAC_ALL_MULTI_REG_VALUE);

	priv->net_dev_stats.tx_fifo_errors = emac_read(EMAC_TXUNDERRUN);
	emac_write(EMAC_TXUNDERRUN, EMAC_ALL_MULTI_REG_VALUE);

	return &priv->net_dev_stats;
}

static const struct net_device_ops emac_netdev_ops = {
	.ndo_open		= emac_dev_open,
	.ndo_stop		= emac_dev_stop,
	.ndo_start_xmit		= emac_dev_xmit,
	.ndo_set_multicast_list	= emac_dev_mcast_set,
	.ndo_set_mac_address	= emac_dev_setmac_addr,
	.ndo_do_ioctl		= emac_devioctl,
	.ndo_tx_timeout		= emac_dev_tx_timeout,
	.ndo_get_stats		= emac_dev_getnetstats,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= emac_poll_controller,
#endif
};

/**
 * davinci_emac_probe: EMAC device probe
 * @pdev: The DaVinci EMAC device that we are removing
 *
 * Called when probing for emac devicesr. We get details of instances and
 * resource information from platform init and register a network device
 * and allocate resources necessary for driver to perform
 */
static int __devinit davinci_emac_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct resource *res;
	struct net_device *ndev;
	struct emac_priv *priv;
	unsigned long size;
	struct emac_platform_data *pdata;
	struct device *emac_dev;

	/* obtain emac clock from kernel */
	emac_clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(emac_clk)) {
		printk(KERN_ERR "DaVinci EMAC: Failed to get EMAC clock\n");
		return -EBUSY;
	}
	emac_bus_frequency = clk_get_rate(emac_clk);
	/* TODO: Probe PHY here if possible */

	ndev = alloc_etherdev(sizeof(struct emac_priv));
	if (!ndev) {
		printk(KERN_ERR "DaVinci EMAC: Error allocating net_device\n");
		clk_put(emac_clk);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, ndev);
	priv = netdev_priv(ndev);
	priv->pdev = pdev;
	priv->ndev = ndev;
	priv->msg_enable = netif_msg_init(debug_level, DAVINCI_EMAC_DEBUG);

	spin_lock_init(&priv->tx_lock);
	spin_lock_init(&priv->rx_lock);
	spin_lock_init(&priv->lock);

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		printk(KERN_ERR "DaVinci EMAC: No platfrom data\n");
		return -ENODEV;
	}

	/* MAC addr and PHY mask , RMII enable info from platform_data */
	memcpy(priv->mac_addr, pdata->mac_addr, 6);
	priv->phy_mask = pdata->phy_mask;
	priv->rmii_en = pdata->rmii_en;
	priv->version = pdata->version;
	emac_dev = &ndev->dev;
	/* Get EMAC platform data */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(emac_dev, "DaVinci EMAC: Error getting res\n");
		rc = -ENOENT;
		goto probe_quit;
	}

	priv->emac_base_phys = res->start + pdata->ctrl_reg_offset;
	size = res->end - res->start + 1;
	if (!request_mem_region(res->start, size, ndev->name)) {
		dev_err(emac_dev, "DaVinci EMAC: failed request_mem_region() \
					 for regs\n");
		rc = -ENXIO;
		goto probe_quit;
	}

	priv->remap_addr = ioremap(res->start, size);
	if (!priv->remap_addr) {
		dev_err(emac_dev, "Unable to map IO\n");
		rc = -ENOMEM;
		release_mem_region(res->start, size);
		goto probe_quit;
	}
	priv->emac_base = priv->remap_addr + pdata->ctrl_reg_offset;
	ndev->base_addr = (unsigned long)priv->remap_addr;

	priv->ctrl_base = priv->remap_addr + pdata->ctrl_mod_reg_offset;
	priv->ctrl_ram_size = pdata->ctrl_ram_size;
	priv->emac_ctrl_ram = priv->remap_addr + pdata->ctrl_ram_offset;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(emac_dev, "DaVinci EMAC: Error getting irq res\n");
		rc = -ENOENT;
		goto no_irq_res;
	}
	ndev->irq = res->start;

	if (!is_valid_ether_addr(priv->mac_addr)) {
		/* Use random MAC if none passed */
		random_ether_addr(priv->mac_addr);
		printk(KERN_WARNING "%s: using random MAC addr: %pM\n",
				__func__, priv->mac_addr);
	}

	ndev->netdev_ops = &emac_netdev_ops;
	SET_ETHTOOL_OPS(ndev, &ethtool_ops);
	netif_napi_add(ndev, &priv->napi, emac_poll, EMAC_POLL_WEIGHT);

	/* register the network device */
	SET_NETDEV_DEV(ndev, &pdev->dev);
	rc = register_netdev(ndev);
	if (rc) {
		dev_err(emac_dev, "DaVinci EMAC: Error in register_netdev\n");
		rc = -ENODEV;
		goto netdev_reg_err;
	}

	clk_enable(emac_clk);

	/* MII/Phy intialisation, mdio bus registration */
	emac_mii = mdiobus_alloc();
	if (emac_mii == NULL) {
		dev_err(emac_dev, "DaVinci EMAC: Error allocating mii_bus\n");
		rc = -ENOMEM;
		goto mdio_alloc_err;
	}

	priv->mii_bus = emac_mii;
	emac_mii->name  = "emac-mii",
	emac_mii->read  = emac_mii_read,
	emac_mii->write = emac_mii_write,
	emac_mii->reset = emac_mii_reset,
	emac_mii->irq   = mii_irqs,
	emac_mii->phy_mask = ~(priv->phy_mask);
	emac_mii->parent = &pdev->dev;
	emac_mii->priv = priv->remap_addr + pdata->mdio_reg_offset;
	snprintf(priv->mii_bus->id, MII_BUS_ID_SIZE, "%x", priv->pdev->id);
	mdio_max_freq = pdata->mdio_max_freq;
	emac_mii->reset(emac_mii);

	/* Register the MII bus */
	rc = mdiobus_register(emac_mii);
	if (rc)
		goto mdiobus_quit;

	if (netif_msg_probe(priv)) {
		dev_notice(emac_dev, "DaVinci EMAC Probe found device "\
			   "(regs: %p, irq: %d)\n",
			   (void *)priv->emac_base_phys, ndev->irq);
	}
	return 0;

mdiobus_quit:
	mdiobus_free(emac_mii);

netdev_reg_err:
mdio_alloc_err:
no_irq_res:
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, res->end - res->start + 1);
	iounmap(priv->remap_addr);

probe_quit:
	clk_put(emac_clk);
	free_netdev(ndev);
	return rc;
}

/**
 * davinci_emac_remove: EMAC device remove
 * @pdev: The DaVinci EMAC device that we are removing
 *
 * Called when removing the device driver. We disable clock usage and release
 * the resources taken up by the driver and unregister network device
 */
static int __devexit davinci_emac_remove(struct platform_device *pdev)
{
	struct resource *res;
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct emac_priv *priv = netdev_priv(ndev);

	dev_notice(&ndev->dev, "DaVinci EMAC: davinci_emac_remove()\n");

	platform_set_drvdata(pdev, NULL);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mdiobus_unregister(priv->mii_bus);
	mdiobus_free(priv->mii_bus);

	release_mem_region(res->start, res->end - res->start + 1);

	unregister_netdev(ndev);
	free_netdev(ndev);
	iounmap(priv->remap_addr);

	clk_disable(emac_clk);
	clk_put(emac_clk);

	return 0;
}

/**
 * davinci_emac_driver: EMAC platform driver structure
 *
 * We implement only probe and remove functions - suspend/resume and
 * others not supported by this module
 */
static struct platform_driver davinci_emac_driver = {
	.driver = {
		.name	 = "davinci_emac",
		.owner	 = THIS_MODULE,
	},
	.probe = davinci_emac_probe,
	.remove = __devexit_p(davinci_emac_remove),
};

/**
 * davinci_emac_init: EMAC driver module init
 *
 * Called when initializing the driver. We register the driver with
 * the platform.
 */
static int __init davinci_emac_init(void)
{
	return platform_driver_register(&davinci_emac_driver);
}
module_init(davinci_emac_init);

/**
 * davinci_emac_exit: EMAC driver module exit
 *
 * Called when exiting the driver completely. We unregister the driver with
 * the platform and exit
 */
static void __exit davinci_emac_exit(void)
{
	platform_driver_unregister(&davinci_emac_driver);
}
module_exit(davinci_emac_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("DaVinci EMAC Maintainer: Anant Gole <anantgole@ti.com>");
MODULE_AUTHOR("DaVinci EMAC Maintainer: Chaithrika U S <chaithrika@ti.com>");
MODULE_DESCRIPTION("DaVinci EMAC Ethernet driver");
