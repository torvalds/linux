/****************************************************************************/

/*
 *	fec.h  --  Fast Ethernet Controller for Motorola ColdFire SoC
 *		   processors.
 *
 *	(C) Copyright 2000-2005, Greg Ungerer (gerg@snapgear.com)
 *	(C) Copyright 2000-2001, Lineo (www.lineo.com)
 */

/****************************************************************************/
#ifndef FEC_H
#define	FEC_H
/****************************************************************************/

#include <linux/clocksource.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_clock_kernel.h>

#if defined(CONFIG_M523x) || defined(CONFIG_M527x) || defined(CONFIG_M528x) || \
    defined(CONFIG_M520x) || defined(CONFIG_M532x) || \
    defined(CONFIG_ARCH_MXC) || defined(CONFIG_SOC_IMX28)
/*
 *	Just figures, Motorola would have to change the offsets for
 *	registers in the same peripheral device on different models
 *	of the ColdFire!
 */
#define FEC_IEVENT		0x004 /* Interrupt event reg */
#define FEC_IMASK		0x008 /* Interrupt mask reg */
#define FEC_R_DES_ACTIVE	0x010 /* Receive descriptor reg */
#define FEC_X_DES_ACTIVE	0x014 /* Transmit descriptor reg */
#define FEC_ECNTRL		0x024 /* Ethernet control reg */
#define FEC_MII_DATA		0x040 /* MII manage frame reg */
#define FEC_MII_SPEED		0x044 /* MII speed control reg */
#define FEC_MIB_CTRLSTAT	0x064 /* MIB control/status reg */
#define FEC_R_CNTRL		0x084 /* Receive control reg */
#define FEC_X_CNTRL		0x0c4 /* Transmit Control reg */
#define FEC_ADDR_LOW		0x0e4 /* Low 32bits MAC address */
#define FEC_ADDR_HIGH		0x0e8 /* High 16bits MAC address */
#define FEC_OPD			0x0ec /* Opcode + Pause duration */
#define FEC_HASH_TABLE_HIGH	0x118 /* High 32bits hash table */
#define FEC_HASH_TABLE_LOW	0x11c /* Low 32bits hash table */
#define FEC_GRP_HASH_TABLE_HIGH	0x120 /* High 32bits hash table */
#define FEC_GRP_HASH_TABLE_LOW	0x124 /* Low 32bits hash table */
#define FEC_X_WMRK		0x144 /* FIFO transmit water mark */
#define FEC_R_BOUND		0x14c /* FIFO receive bound reg */
#define FEC_R_FSTART		0x150 /* FIFO receive start reg */
#define FEC_R_DES_START		0x180 /* Receive descriptor ring */
#define FEC_X_DES_START		0x184 /* Transmit descriptor ring */
#define FEC_R_BUFF_SIZE		0x188 /* Maximum receive buff size */
#define FEC_R_FIFO_RSFL		0x190 /* Receive FIFO section full threshold */
#define FEC_R_FIFO_RSEM		0x194 /* Receive FIFO section empty threshold */
#define FEC_R_FIFO_RAEM		0x198 /* Receive FIFO almost empty threshold */
#define FEC_R_FIFO_RAFL		0x19c /* Receive FIFO almost full threshold */
#define FEC_MIIGSK_CFGR		0x300 /* MIIGSK Configuration reg */
#define FEC_MIIGSK_ENR		0x308 /* MIIGSK Enable reg */

#define BM_MIIGSK_CFGR_MII		0x00
#define BM_MIIGSK_CFGR_RMII		0x01
#define BM_MIIGSK_CFGR_FRCONT_10M	0x40

#else

#define FEC_ECNTRL		0x000 /* Ethernet control reg */
#define FEC_IEVENT		0x004 /* Interrupt even reg */
#define FEC_IMASK		0x008 /* Interrupt mask reg */
#define FEC_IVEC		0x00c /* Interrupt vec status reg */
#define FEC_R_DES_ACTIVE	0x010 /* Receive descriptor reg */
#define FEC_X_DES_ACTIVE	0x014 /* Transmit descriptor reg */
#define FEC_MII_DATA		0x040 /* MII manage frame reg */
#define FEC_MII_SPEED		0x044 /* MII speed control reg */
#define FEC_R_BOUND		0x08c /* FIFO receive bound reg */
#define FEC_R_FSTART		0x090 /* FIFO receive start reg */
#define FEC_X_WMRK		0x0a4 /* FIFO transmit water mark */
#define FEC_X_FSTART		0x0ac /* FIFO transmit start reg */
#define FEC_R_CNTRL		0x104 /* Receive control reg */
#define FEC_MAX_FRM_LEN		0x108 /* Maximum frame length reg */
#define FEC_X_CNTRL		0x144 /* Transmit Control reg */
#define FEC_ADDR_LOW		0x3c0 /* Low 32bits MAC address */
#define FEC_ADDR_HIGH		0x3c4 /* High 16bits MAC address */
#define FEC_GRP_HASH_TABLE_HIGH	0x3c8 /* High 32bits hash table */
#define FEC_GRP_HASH_TABLE_LOW	0x3cc /* Low 32bits hash table */
#define FEC_R_DES_START		0x3d0 /* Receive descriptor ring */
#define FEC_X_DES_START		0x3d4 /* Transmit descriptor ring */
#define FEC_R_BUFF_SIZE		0x3d8 /* Maximum receive buff size */
#define FEC_FIFO_RAM		0x400 /* FIFO RAM buffer */

#endif /* CONFIG_M5272 */


/*
 *	Define the buffer descriptor structure.
 */
#if defined(CONFIG_ARCH_MXC) || defined(CONFIG_SOC_IMX28)
struct bufdesc {
	unsigned short cbd_datlen;	/* Data length */
	unsigned short cbd_sc;	/* Control and status info */
	unsigned long cbd_bufaddr;	/* Buffer address */
};

struct bufdesc_ex {
	struct bufdesc desc;
	unsigned long cbd_esc;
	unsigned long cbd_prot;
	unsigned long cbd_bdu;
	unsigned long ts;
	unsigned short res0[4];
};

#else
struct bufdesc {
	unsigned short	cbd_sc;			/* Control and status info */
	unsigned short	cbd_datlen;		/* Data length */
	unsigned long	cbd_bufaddr;		/* Buffer address */
};
#endif

/*
 *	The following definitions courtesy of commproc.h, which where
 *	Copyright (c) 1997 Dan Malek (dmalek@jlc.net).
 */
#define BD_SC_EMPTY     ((ushort)0x8000)        /* Receive is empty */
#define BD_SC_READY     ((ushort)0x8000)        /* Transmit is ready */
#define BD_SC_WRAP      ((ushort)0x2000)        /* Last buffer descriptor */
#define BD_SC_INTRPT    ((ushort)0x1000)        /* Interrupt on change */
#define BD_SC_CM        ((ushort)0x0200)        /* Continuous mode */
#define BD_SC_ID        ((ushort)0x0100)        /* Rec'd too many idles */
#define BD_SC_P         ((ushort)0x0100)        /* xmt preamble */
#define BD_SC_BR        ((ushort)0x0020)        /* Break received */
#define BD_SC_FR        ((ushort)0x0010)        /* Framing error */
#define BD_SC_PR        ((ushort)0x0008)        /* Parity error */
#define BD_SC_OV        ((ushort)0x0002)        /* Overrun */
#define BD_SC_CD        ((ushort)0x0001)        /* ?? */

/* Buffer descriptor control/status used by Ethernet receive.
*/
#define BD_ENET_RX_EMPTY        ((ushort)0x8000)
#define BD_ENET_RX_WRAP         ((ushort)0x2000)
#define BD_ENET_RX_INTR         ((ushort)0x1000)
#define BD_ENET_RX_LAST         ((ushort)0x0800)
#define BD_ENET_RX_FIRST        ((ushort)0x0400)
#define BD_ENET_RX_MISS         ((ushort)0x0100)
#define BD_ENET_RX_LG           ((ushort)0x0020)
#define BD_ENET_RX_NO           ((ushort)0x0010)
#define BD_ENET_RX_SH           ((ushort)0x0008)
#define BD_ENET_RX_CR           ((ushort)0x0004)
#define BD_ENET_RX_OV           ((ushort)0x0002)
#define BD_ENET_RX_CL           ((ushort)0x0001)
#define BD_ENET_RX_STATS        ((ushort)0x013f)        /* All status bits */

/* Buffer descriptor control/status used by Ethernet transmit.
*/
#define BD_ENET_TX_READY        ((ushort)0x8000)
#define BD_ENET_TX_PAD          ((ushort)0x4000)
#define BD_ENET_TX_WRAP         ((ushort)0x2000)
#define BD_ENET_TX_INTR         ((ushort)0x1000)
#define BD_ENET_TX_LAST         ((ushort)0x0800)
#define BD_ENET_TX_TC           ((ushort)0x0400)
#define BD_ENET_TX_DEF          ((ushort)0x0200)
#define BD_ENET_TX_HB           ((ushort)0x0100)
#define BD_ENET_TX_LC           ((ushort)0x0080)
#define BD_ENET_TX_RL           ((ushort)0x0040)
#define BD_ENET_TX_RCMASK       ((ushort)0x003c)
#define BD_ENET_TX_UN           ((ushort)0x0002)
#define BD_ENET_TX_CSL          ((ushort)0x0001)
#define BD_ENET_TX_STATS        ((ushort)0x03ff)        /* All status bits */

/*enhanced buffer desciptor control/status used by Ethernet transmit*/
#define BD_ENET_TX_INT          0x40000000
#define BD_ENET_TX_TS           0x20000000


/* This device has up to three irqs on some platforms */
#define FEC_IRQ_NUM		3

/* The number of Tx and Rx buffers.  These are allocated from the page
 * pool.  The code may assume these are power of two, so it it best
 * to keep them that size.
 * We don't need to allocate pages for the transmitter.  We just use
 * the skbuffer directly.
 */

#define FEC_ENET_RX_PAGES	8
#define FEC_ENET_RX_FRSIZE	2048
#define FEC_ENET_RX_FRPPG	(PAGE_SIZE / FEC_ENET_RX_FRSIZE)
#define RX_RING_SIZE		(FEC_ENET_RX_FRPPG * FEC_ENET_RX_PAGES)
#define FEC_ENET_TX_FRSIZE	2048
#define FEC_ENET_TX_FRPPG	(PAGE_SIZE / FEC_ENET_TX_FRSIZE)
#define TX_RING_SIZE		16	/* Must be power of two */
#define TX_RING_MOD_MASK	15	/*   for this to work */

#define BD_ENET_RX_INT          0x00800000
#define BD_ENET_RX_PTP          ((ushort)0x0400)

/* The FEC buffer descriptors track the ring buffers.  The rx_bd_base and
 * tx_bd_base always point to the base of the buffer descriptors.  The
 * cur_rx and cur_tx point to the currently available buffer.
 * The dirty_tx tracks the current buffer that is being sent by the
 * controller.  The cur_tx and dirty_tx are equal under both completely
 * empty and completely full conditions.  The empty/ready indicator in
 * the buffer descriptor determines the actual condition.
 */
struct fec_enet_private {
	/* Hardware registers of the FEC device */
	void __iomem *hwp;

	struct net_device *netdev;

	struct clk *clk_ipg;
	struct clk *clk_ahb;
	struct clk *clk_ptp;

	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	unsigned char *tx_bounce[TX_RING_SIZE];
	struct	sk_buff *tx_skbuff[TX_RING_SIZE];
	struct	sk_buff *rx_skbuff[RX_RING_SIZE];
	ushort	skb_cur;
	ushort	skb_dirty;

	/* CPM dual port RAM relative addresses */
	dma_addr_t	bd_dma;
	/* Address of Rx and Tx buffers */
	struct bufdesc	*rx_bd_base;
	struct bufdesc	*tx_bd_base;
	/* The next free ring entry */
	struct bufdesc	*cur_rx, *cur_tx;
	/* The ring entries to be free()ed */
	struct bufdesc	*dirty_tx;

	uint	tx_full;
	/* hold while accessing the HW like ringbuffer for tx/rx but not MAC */
	spinlock_t hw_lock;

	struct	platform_device *pdev;

	int	opened;
	int	dev_id;

	/* Phylib and MDIO interface */
	struct	mii_bus *mii_bus;
	struct	phy_device *phy_dev;
	int	mii_timeout;
	uint	phy_speed;
	phy_interface_t	phy_interface;
	int	link;
	int	full_duplex;
	struct	completion mdio_done;
	int	irq[FEC_IRQ_NUM];
	int	bufdesc_ex;
	int	pause_flag;

	struct	napi_struct napi;

	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_caps;
	unsigned long last_overflow_check;
	spinlock_t tmreg_lock;
	struct cyclecounter cc;
	struct timecounter tc;
	int rx_hwtstamp_filter;
	u32 base_incval;
	u32 cycle_speed;
	int hwts_rx_en;
	int hwts_tx_en;
	struct timer_list time_keep;

};

void fec_ptp_init(struct net_device *ndev, struct platform_device *pdev);
void fec_ptp_start_cyclecounter(struct net_device *ndev);
int fec_ptp_ioctl(struct net_device *ndev, struct ifreq *ifr, int cmd);

/****************************************************************************/
#endif /* FEC_H */
