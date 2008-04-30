/*
 * Fast Ethernet Controller (FCC) driver for Motorola MPC8260.
 * Copyright (c) 2000 MontaVista Software, Inc.   Dan Malek (dmalek@jlc.net)
 *
 * This version of the driver is a combination of the 8xx fec and
 * 8260 SCC Ethernet drivers.  This version has some additional
 * configuration options, which should probably be moved out of
 * here.  This driver currently works for the EST SBC8260,
 * SBS Diablo/BCM, Embedded Planet RPX6, TQM8260, and others.
 *
 * Right now, I am very watseful with the buffers.  I allocate memory
 * pages and then divide them into 2K frame buffers.  This way I know I
 * have buffers large enough to hold one frame within one buffer descriptor.
 * Once I get this working, I will use 64 or 128 byte CPM buffers, which
 * will be much more memory efficient and will easily handle lots of
 * small packets.  Since this is a cache coherent processor and CPM,
 * I could also preallocate SKB's and use them directly on the interface.
 *
 * 2004-12	Leo Li (leoli@freescale.com)
 * - Rework the FCC clock configuration part, make it easier to configure.
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mii.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>

#include <asm/immap_cpm2.h>
#include <asm/pgtable.h>
#include <asm/mpc8260.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/signal.h>

/* We can't use the PHY interrupt if we aren't using MDIO. */
#if !defined(CONFIG_USE_MDIO)
#undef PHY_INTERRUPT
#endif

/* If we have a PHY interrupt, we will advertise both full-duplex and half-
 * duplex capabilities.  If we don't have a PHY interrupt, then we will only
 * advertise half-duplex capabilities.
 */
#define MII_ADVERTISE_HALF	(ADVERTISE_100HALF | ADVERTISE_10HALF | \
				 ADVERTISE_CSMA)
#define MII_ADVERTISE_ALL	(ADVERTISE_100FULL | ADVERTISE_10FULL | \
				 MII_ADVERTISE_HALF)
#ifdef PHY_INTERRUPT
#define MII_ADVERTISE_DEFAULT	MII_ADVERTISE_ALL
#else
#define MII_ADVERTISE_DEFAULT	MII_ADVERTISE_HALF
#endif
#include <asm/cpm2.h>

/* The transmitter timeout
 */
#define TX_TIMEOUT	(2*HZ)

#ifdef	CONFIG_USE_MDIO
/* Forward declarations of some structures to support different PHYs */

typedef struct {
	uint mii_data;
	void (*funct)(uint mii_reg, struct net_device *dev);
} phy_cmd_t;

typedef struct {
	uint id;
	char *name;

	const phy_cmd_t *config;
	const phy_cmd_t *startup;
	const phy_cmd_t *ack_int;
	const phy_cmd_t *shutdown;
} phy_info_t;

/* values for phy_status */

#define PHY_CONF_ANE	0x0001  /* 1 auto-negotiation enabled */
#define PHY_CONF_LOOP	0x0002  /* 1 loopback mode enabled */
#define PHY_CONF_SPMASK	0x00f0  /* mask for speed */
#define PHY_CONF_10HDX	0x0010  /* 10 Mbit half duplex supported */
#define PHY_CONF_10FDX	0x0020  /* 10 Mbit full duplex supported */
#define PHY_CONF_100HDX	0x0040  /* 100 Mbit half duplex supported */
#define PHY_CONF_100FDX	0x0080  /* 100 Mbit full duplex supported */

#define PHY_STAT_LINK	0x0100  /* 1 up - 0 down */
#define PHY_STAT_FAULT	0x0200  /* 1 remote fault */
#define PHY_STAT_ANC	0x0400  /* 1 auto-negotiation complete	*/
#define PHY_STAT_SPMASK	0xf000  /* mask for speed */
#define PHY_STAT_10HDX	0x1000  /* 10 Mbit half duplex selected	*/
#define PHY_STAT_10FDX	0x2000  /* 10 Mbit full duplex selected	*/
#define PHY_STAT_100HDX	0x4000  /* 100 Mbit half duplex selected */
#define PHY_STAT_100FDX	0x8000  /* 100 Mbit full duplex selected */
#endif	/* CONFIG_USE_MDIO */

/* The number of Tx and Rx buffers.  These are allocated from the page
 * pool.  The code may assume these are power of two, so it is best
 * to keep them that size.
 * We don't need to allocate pages for the transmitter.  We just use
 * the skbuffer directly.
 */
#define FCC_ENET_RX_PAGES	16
#define FCC_ENET_RX_FRSIZE	2048
#define FCC_ENET_RX_FRPPG	(PAGE_SIZE / FCC_ENET_RX_FRSIZE)
#define RX_RING_SIZE		(FCC_ENET_RX_FRPPG * FCC_ENET_RX_PAGES)
#define TX_RING_SIZE		16	/* Must be power of two */
#define TX_RING_MOD_MASK	15	/*   for this to work */

/* The FCC stores dest/src/type, data, and checksum for receive packets.
 * size includes support for VLAN
 */
#define PKT_MAXBUF_SIZE		1522
#define PKT_MINBUF_SIZE		64

/* Maximum input DMA size.  Must be a should(?) be a multiple of 4.
 * size includes support for VLAN
 */
#define PKT_MAXDMA_SIZE		1524

/* Maximum input buffer size.  Must be a multiple of 32.
*/
#define PKT_MAXBLR_SIZE		1536

static int fcc_enet_open(struct net_device *dev);
static int fcc_enet_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int fcc_enet_rx(struct net_device *dev);
static irqreturn_t fcc_enet_interrupt(int irq, void *dev_id);
static int fcc_enet_close(struct net_device *dev);
static struct net_device_stats *fcc_enet_get_stats(struct net_device *dev);
/* static void set_multicast_list(struct net_device *dev); */
static void fcc_restart(struct net_device *dev, int duplex);
static void fcc_stop(struct net_device *dev);
static int fcc_enet_set_mac_address(struct net_device *dev, void *addr);

/* These will be configurable for the FCC choice.
 * Multiple ports can be configured.  There is little choice among the
 * I/O pins to the PHY, except the clocks.  We will need some board
 * dependent clock selection.
 * Why in the hell did I put these inside #ifdef's?  I dunno, maybe to
 * help show what pins are used for each device.
 */

/* Since the CLK setting changes greatly from board to board, I changed
 * it to a easy way.  You just need to specify which CLK number to use.
 * Note that only limited choices can be make on each port.
 */

/* FCC1 Clock Source Configuration.  There are board specific.
   Can only choose from CLK9-12 */
#ifdef CONFIG_SBC82xx
#define F1_RXCLK	9
#define F1_TXCLK	10
#else
#define F1_RXCLK	12
#define F1_TXCLK	11
#endif

/* FCC2 Clock Source Configuration.  There are board specific.
   Can only choose from CLK13-16 */
#define F2_RXCLK	13
#define F2_TXCLK	14

/* FCC3 Clock Source Configuration.  There are board specific.
   Can only choose from CLK13-16 */
#define F3_RXCLK	15
#define F3_TXCLK	16

/* Automatically generates register configurations */
#define PC_CLK(x)	((uint)(1<<(x-1)))	/* FCC CLK I/O ports */

#define CMXFCR_RF1CS(x)	((uint)((x-5)<<27))	/* FCC1 Receive Clock Source */
#define CMXFCR_TF1CS(x)	((uint)((x-5)<<24))	/* FCC1 Transmit Clock Source */
#define CMXFCR_RF2CS(x)	((uint)((x-9)<<19))	/* FCC2 Receive Clock Source */
#define CMXFCR_TF2CS(x) ((uint)((x-9)<<16))	/* FCC2 Transmit Clock Source */
#define CMXFCR_RF3CS(x)	((uint)((x-9)<<11))	/* FCC3 Receive Clock Source */
#define CMXFCR_TF3CS(x) ((uint)((x-9)<<8))	/* FCC3 Transmit Clock Source */

#define PC_F1RXCLK	PC_CLK(F1_RXCLK)
#define PC_F1TXCLK	PC_CLK(F1_TXCLK)
#define CMX1_CLK_ROUTE	(CMXFCR_RF1CS(F1_RXCLK) | CMXFCR_TF1CS(F1_TXCLK))
#define CMX1_CLK_MASK	((uint)0xff000000)

#define PC_F2RXCLK	PC_CLK(F2_RXCLK)
#define PC_F2TXCLK	PC_CLK(F2_TXCLK)
#define CMX2_CLK_ROUTE	(CMXFCR_RF2CS(F2_RXCLK) | CMXFCR_TF2CS(F2_TXCLK))
#define CMX2_CLK_MASK	((uint)0x00ff0000)

#define PC_F3RXCLK	PC_CLK(F3_RXCLK)
#define PC_F3TXCLK	PC_CLK(F3_TXCLK)
#define CMX3_CLK_ROUTE	(CMXFCR_RF3CS(F3_RXCLK) | CMXFCR_TF3CS(F3_TXCLK))
#define CMX3_CLK_MASK	((uint)0x0000ff00)


/* I/O Pin assignment for FCC1.  I don't yet know the best way to do this,
 * but there is little variation among the choices.
 */
#define PA1_COL		((uint)0x00000001)
#define PA1_CRS		((uint)0x00000002)
#define PA1_TXER	((uint)0x00000004)
#define PA1_TXEN	((uint)0x00000008)
#define PA1_RXDV	((uint)0x00000010)
#define PA1_RXER	((uint)0x00000020)
#define PA1_TXDAT	((uint)0x00003c00)
#define PA1_RXDAT	((uint)0x0003c000)
#define PA1_PSORA_BOUT	(PA1_RXDAT | PA1_TXDAT)
#define PA1_PSORA_BIN	(PA1_COL | PA1_CRS | PA1_TXER | PA1_TXEN | \
				PA1_RXDV | PA1_RXER)
#define PA1_DIRA_BOUT	(PA1_RXDAT | PA1_CRS | PA1_COL | PA1_RXER | PA1_RXDV)
#define PA1_DIRA_BIN	(PA1_TXDAT | PA1_TXEN | PA1_TXER)


/* I/O Pin assignment for FCC2.  I don't yet know the best way to do this,
 * but there is little variation among the choices.
 */
#define PB2_TXER	((uint)0x00000001)
#define PB2_RXDV	((uint)0x00000002)
#define PB2_TXEN	((uint)0x00000004)
#define PB2_RXER	((uint)0x00000008)
#define PB2_COL		((uint)0x00000010)
#define PB2_CRS		((uint)0x00000020)
#define PB2_TXDAT	((uint)0x000003c0)
#define PB2_RXDAT	((uint)0x00003c00)
#define PB2_PSORB_BOUT	(PB2_RXDAT | PB2_TXDAT | PB2_CRS | PB2_COL | \
				PB2_RXER | PB2_RXDV | PB2_TXER)
#define PB2_PSORB_BIN	(PB2_TXEN)
#define PB2_DIRB_BOUT	(PB2_RXDAT | PB2_CRS | PB2_COL | PB2_RXER | PB2_RXDV)
#define PB2_DIRB_BIN	(PB2_TXDAT | PB2_TXEN | PB2_TXER)


/* I/O Pin assignment for FCC3.  I don't yet know the best way to do this,
 * but there is little variation among the choices.
 */
#define PB3_RXDV	((uint)0x00004000)
#define PB3_RXER	((uint)0x00008000)
#define PB3_TXER	((uint)0x00010000)
#define PB3_TXEN	((uint)0x00020000)
#define PB3_COL		((uint)0x00040000)
#define PB3_CRS		((uint)0x00080000)
#ifndef CONFIG_RPX8260
#define PB3_TXDAT	((uint)0x0f000000)
#define PC3_TXDAT	((uint)0x00000000)
#else
#define PB3_TXDAT	((uint)0x0f000000)
#define PC3_TXDAT	0
#endif
#define PB3_RXDAT	((uint)0x00f00000)
#define PB3_PSORB_BOUT	(PB3_RXDAT | PB3_TXDAT | PB3_CRS | PB3_COL | \
				PB3_RXER | PB3_RXDV | PB3_TXER | PB3_TXEN)
#define PB3_PSORB_BIN	(0)
#define PB3_DIRB_BOUT	(PB3_RXDAT | PB3_CRS | PB3_COL | PB3_RXER | PB3_RXDV)
#define PB3_DIRB_BIN	(PB3_TXDAT | PB3_TXEN | PB3_TXER)

#define PC3_PSORC_BOUT	(PC3_TXDAT)
#define PC3_PSORC_BIN	(0)
#define PC3_DIRC_BOUT	(0)
#define PC3_DIRC_BIN	(PC3_TXDAT)


/* MII status/control serial interface.
*/
#if defined(CONFIG_RPX8260)
/* The EP8260 doesn't use Port C for MDIO */
#define PC_MDIO		((uint)0x00000000)
#define PC_MDCK		((uint)0x00000000)
#elif defined(CONFIG_TQM8260)
/* TQM8260 has MDIO and MDCK on PC30 and PC31 respectively */
#define PC_MDIO		((uint)0x00000002)
#define PC_MDCK		((uint)0x00000001)
#elif defined(CONFIG_EST8260) || defined(CONFIG_ADS8260)
#define PC_MDIO		((uint)0x00400000)
#define PC_MDCK		((uint)0x00200000)
#else
#define PC_MDIO		((uint)0x00000004)
#define PC_MDCK		((uint)0x00000020)
#endif

#if defined(CONFIG_USE_MDIO) && (!defined(PC_MDIO) || !defined(PC_MDCK))
#error "Must define PC_MDIO and PC_MDCK if using MDIO"
#endif

/* PHY addresses */
/* default to dynamic config of phy addresses */
#define FCC1_PHY_ADDR 0
#ifdef CONFIG_PQ2FADS
#define FCC2_PHY_ADDR 0
#else
#define FCC2_PHY_ADDR 2
#endif
#define FCC3_PHY_ADDR 3

/* A table of information for supporting FCCs.  This does two things.
 * First, we know how many FCCs we have and they are always externally
 * numbered from zero.  Second, it holds control register and I/O
 * information that could be different among board designs.
 */
typedef struct fcc_info {
	uint	fc_fccnum;
	uint	fc_phyaddr;
	uint	fc_cpmblock;
	uint	fc_cpmpage;
	uint	fc_proff;
	uint	fc_interrupt;
	uint	fc_trxclocks;
	uint	fc_clockroute;
	uint	fc_clockmask;
	uint	fc_mdio;
	uint	fc_mdck;
} fcc_info_t;

static fcc_info_t fcc_ports[] = {
#ifdef CONFIG_FCC1_ENET
	{ 0, FCC1_PHY_ADDR, CPM_CR_FCC1_SBLOCK, CPM_CR_FCC1_PAGE, PROFF_FCC1, SIU_INT_FCC1,
		(PC_F1RXCLK | PC_F1TXCLK), CMX1_CLK_ROUTE, CMX1_CLK_MASK,
		PC_MDIO, PC_MDCK },
#endif
#ifdef CONFIG_FCC2_ENET
	{ 1, FCC2_PHY_ADDR, CPM_CR_FCC2_SBLOCK, CPM_CR_FCC2_PAGE, PROFF_FCC2, SIU_INT_FCC2,
		(PC_F2RXCLK | PC_F2TXCLK), CMX2_CLK_ROUTE, CMX2_CLK_MASK,
		PC_MDIO, PC_MDCK },
#endif
#ifdef CONFIG_FCC3_ENET
	{ 2, FCC3_PHY_ADDR, CPM_CR_FCC3_SBLOCK, CPM_CR_FCC3_PAGE, PROFF_FCC3, SIU_INT_FCC3,
		(PC_F3RXCLK | PC_F3TXCLK), CMX3_CLK_ROUTE, CMX3_CLK_MASK,
		PC_MDIO, PC_MDCK },
#endif
};

/* The FCC buffer descriptors track the ring buffers.  The rx_bd_base and
 * tx_bd_base always point to the base of the buffer descriptors.  The
 * cur_rx and cur_tx point to the currently available buffer.
 * The dirty_tx tracks the current buffer that is being sent by the
 * controller.  The cur_tx and dirty_tx are equal under both completely
 * empty and completely full conditions.  The empty/ready indicator in
 * the buffer descriptor determines the actual condition.
 */
struct fcc_enet_private {
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct	sk_buff* tx_skbuff[TX_RING_SIZE];
	ushort	skb_cur;
	ushort	skb_dirty;

	/* CPM dual port RAM relative addresses.
	*/
	cbd_t	*rx_bd_base;		/* Address of Rx and Tx buffers. */
	cbd_t	*tx_bd_base;
	cbd_t	*cur_rx, *cur_tx;		/* The next free ring entry */
	cbd_t	*dirty_tx;	/* The ring entries to be free()ed. */
	volatile fcc_t	*fccp;
	volatile fcc_enet_t	*ep;
	struct	net_device_stats stats;
	uint	tx_free;
	spinlock_t lock;

#ifdef	CONFIG_USE_MDIO
	uint	phy_id;
	uint	phy_id_done;
	uint	phy_status;
	phy_info_t	*phy;
	struct work_struct phy_relink;
	struct work_struct phy_display_config;
	struct net_device *dev;

	uint	sequence_done;

	uint	phy_addr;
#endif	/* CONFIG_USE_MDIO */

	int	link;
	int	old_link;
	int	full_duplex;

	fcc_info_t	*fip;
};

static void init_fcc_shutdown(fcc_info_t *fip, struct fcc_enet_private *cep,
	volatile cpm2_map_t *immap);
static void init_fcc_startup(fcc_info_t *fip, struct net_device *dev);
static void init_fcc_ioports(fcc_info_t *fip, volatile iop_cpm2_t *io,
	volatile cpm2_map_t *immap);
static void init_fcc_param(fcc_info_t *fip, struct net_device *dev,
	volatile cpm2_map_t *immap);

#ifdef	CONFIG_USE_MDIO
static int	mii_queue(struct net_device *dev, int request, void (*func)(uint, struct net_device *));
static uint	mii_send_receive(fcc_info_t *fip, uint cmd);
static void	mii_do_cmd(struct net_device *dev, const phy_cmd_t *c);

/* Make MII read/write commands for the FCC.
*/
#define mk_mii_read(REG)	(0x60020000 | (((REG) & 0x1f) << 18))
#define mk_mii_write(REG, VAL)	(0x50020000 | (((REG) & 0x1f) << 18) | \
						((VAL) & 0xffff))
#define mk_mii_end	0
#endif	/* CONFIG_USE_MDIO */


static int
fcc_enet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct fcc_enet_private *cep = (struct fcc_enet_private *)dev->priv;
	volatile cbd_t	*bdp;

	/* Fill in a Tx ring entry */
	bdp = cep->cur_tx;

#ifndef final_version
	if (!cep->tx_free || (bdp->cbd_sc & BD_ENET_TX_READY)) {
		/* Ooops.  All transmit buffers are full.  Bail out.
		 * This should not happen, since the tx queue should be stopped.
		 */
		printk("%s: tx queue full!.\n", dev->name);
		return 1;
	}
#endif

	/* Clear all of the status flags. */
	bdp->cbd_sc &= ~BD_ENET_TX_STATS;

	/* If the frame is short, tell CPM to pad it. */
	if (skb->len <= ETH_ZLEN)
		bdp->cbd_sc |= BD_ENET_TX_PAD;
	else
		bdp->cbd_sc &= ~BD_ENET_TX_PAD;

	/* Set buffer length and buffer pointer. */
	bdp->cbd_datlen = skb->len;
	bdp->cbd_bufaddr = __pa(skb->data);

	spin_lock_irq(&cep->lock);

	/* Save skb pointer. */
	cep->tx_skbuff[cep->skb_cur] = skb;

	cep->stats.tx_bytes += skb->len;
	cep->skb_cur = (cep->skb_cur+1) & TX_RING_MOD_MASK;

	/* Send it on its way.  Tell CPM its ready, interrupt when done,
	 * its the last BD of the frame, and to put the CRC on the end.
	 */
	bdp->cbd_sc |= (BD_ENET_TX_READY | BD_ENET_TX_INTR | BD_ENET_TX_LAST | BD_ENET_TX_TC);

#if 0
	/* Errata says don't do this. */
	cep->fccp->fcc_ftodr = 0x8000;
#endif
	dev->trans_start = jiffies;

	/* If this was the last BD in the ring, start at the beginning again. */
	if (bdp->cbd_sc & BD_ENET_TX_WRAP)
		bdp = cep->tx_bd_base;
	else
		bdp++;

	if (!--cep->tx_free)
		netif_stop_queue(dev);

	cep->cur_tx = (cbd_t *)bdp;

	spin_unlock_irq(&cep->lock);

	return 0;
}


static void
fcc_enet_timeout(struct net_device *dev)
{
	struct fcc_enet_private *cep = (struct fcc_enet_private *)dev->priv;

	printk("%s: transmit timed out.\n", dev->name);
	cep->stats.tx_errors++;
#ifndef final_version
	{
		int	i;
		cbd_t	*bdp;
		printk(" Ring data dump: cur_tx %p tx_free %d cur_rx %p.\n",
		       cep->cur_tx, cep->tx_free,
		       cep->cur_rx);
		bdp = cep->tx_bd_base;
		printk(" Tx @base %p :\n", bdp);
		for (i = 0 ; i < TX_RING_SIZE; i++, bdp++)
			printk("%04x %04x %08x\n",
			       bdp->cbd_sc,
			       bdp->cbd_datlen,
			       bdp->cbd_bufaddr);
		bdp = cep->rx_bd_base;
		printk(" Rx @base %p :\n", bdp);
		for (i = 0 ; i < RX_RING_SIZE; i++, bdp++)
			printk("%04x %04x %08x\n",
			       bdp->cbd_sc,
			       bdp->cbd_datlen,
			       bdp->cbd_bufaddr);
	}
#endif
	if (cep->tx_free)
		netif_wake_queue(dev);
}

/* The interrupt handler. */
static irqreturn_t
fcc_enet_interrupt(int irq, void *dev_id)
{
	struct	net_device *dev = dev_id;
	volatile struct	fcc_enet_private *cep;
	volatile cbd_t	*bdp;
	ushort	int_events;
	int	must_restart;

	cep = dev->priv;

	/* Get the interrupt events that caused us to be here.
	*/
	int_events = cep->fccp->fcc_fcce;
	cep->fccp->fcc_fcce = (int_events & cep->fccp->fcc_fccm);
	must_restart = 0;

#ifdef PHY_INTERRUPT
	/* We have to be careful here to make sure that we aren't
	 * interrupted by a PHY interrupt.
	 */
	disable_irq_nosync(PHY_INTERRUPT);
#endif

	/* Handle receive event in its own function.
	*/
	if (int_events & FCC_ENET_RXF)
		fcc_enet_rx(dev_id);

	/* Check for a transmit error.  The manual is a little unclear
	 * about this, so the debug code until I get it figured out.  It
	 * appears that if TXE is set, then TXB is not set.  However,
	 * if carrier sense is lost during frame transmission, the TXE
	 * bit is set, "and continues the buffer transmission normally."
	 * I don't know if "normally" implies TXB is set when the buffer
	 * descriptor is closed.....trial and error :-).
	 */

	/* Transmit OK, or non-fatal error.  Update the buffer descriptors.
	*/
	if (int_events & (FCC_ENET_TXE | FCC_ENET_TXB)) {
	    spin_lock(&cep->lock);
	    bdp = cep->dirty_tx;
	    while ((bdp->cbd_sc&BD_ENET_TX_READY)==0) {
		if (cep->tx_free == TX_RING_SIZE)
		    break;

		if (bdp->cbd_sc & BD_ENET_TX_HB)	/* No heartbeat */
			cep->stats.tx_heartbeat_errors++;
		if (bdp->cbd_sc & BD_ENET_TX_LC)	/* Late collision */
			cep->stats.tx_window_errors++;
		if (bdp->cbd_sc & BD_ENET_TX_RL)	/* Retrans limit */
			cep->stats.tx_aborted_errors++;
		if (bdp->cbd_sc & BD_ENET_TX_UN)	/* Underrun */
			cep->stats.tx_fifo_errors++;
		if (bdp->cbd_sc & BD_ENET_TX_CSL)	/* Carrier lost */
			cep->stats.tx_carrier_errors++;


		/* No heartbeat or Lost carrier are not really bad errors.
		 * The others require a restart transmit command.
		 */
		if (bdp->cbd_sc &
		    (BD_ENET_TX_LC | BD_ENET_TX_RL | BD_ENET_TX_UN)) {
			must_restart = 1;
			cep->stats.tx_errors++;
		}

		cep->stats.tx_packets++;

		/* Deferred means some collisions occurred during transmit,
		 * but we eventually sent the packet OK.
		 */
		if (bdp->cbd_sc & BD_ENET_TX_DEF)
			cep->stats.collisions++;

		/* Free the sk buffer associated with this last transmit. */
		dev_kfree_skb_irq(cep->tx_skbuff[cep->skb_dirty]);
		cep->tx_skbuff[cep->skb_dirty] = NULL;
		cep->skb_dirty = (cep->skb_dirty + 1) & TX_RING_MOD_MASK;

		/* Update pointer to next buffer descriptor to be transmitted. */
		if (bdp->cbd_sc & BD_ENET_TX_WRAP)
			bdp = cep->tx_bd_base;
		else
			bdp++;

		/* I don't know if we can be held off from processing these
		 * interrupts for more than one frame time.  I really hope
		 * not.  In such a case, we would now want to check the
		 * currently available BD (cur_tx) and determine if any
		 * buffers between the dirty_tx and cur_tx have also been
		 * sent.  We would want to process anything in between that
		 * does not have BD_ENET_TX_READY set.
		 */

		/* Since we have freed up a buffer, the ring is no longer
		 * full.
		 */
		if (!cep->tx_free++) {
			if (netif_queue_stopped(dev)) {
				netif_wake_queue(dev);
			}
		}

		cep->dirty_tx = (cbd_t *)bdp;
	    }

	    if (must_restart) {
		volatile cpm_cpm2_t *cp;

		/* Some transmit errors cause the transmitter to shut
		 * down.  We now issue a restart transmit.  Since the
		 * errors close the BD and update the pointers, the restart
		 * _should_ pick up without having to reset any of our
		 * pointers either.  Also, To workaround 8260 device erratum
		 * CPM37, we must disable and then re-enable the transmitter
		 * following a Late Collision, Underrun, or Retry Limit error.
		 */
		cep->fccp->fcc_gfmr &= ~FCC_GFMR_ENT;
		udelay(10); /* wait a few microseconds just on principle */
		cep->fccp->fcc_gfmr |=  FCC_GFMR_ENT;

		cp = cpmp;
		cp->cp_cpcr =
		    mk_cr_cmd(cep->fip->fc_cpmpage, cep->fip->fc_cpmblock,
		    		0x0c, CPM_CR_RESTART_TX) | CPM_CR_FLG;
		while (cp->cp_cpcr & CPM_CR_FLG);
	    }
	    spin_unlock(&cep->lock);
	}

	/* Check for receive busy, i.e. packets coming but no place to
	 * put them.
	 */
	if (int_events & FCC_ENET_BSY) {
		cep->fccp->fcc_fcce = FCC_ENET_BSY;
		cep->stats.rx_dropped++;
	}

#ifdef PHY_INTERRUPT
	enable_irq(PHY_INTERRUPT);
#endif
	return IRQ_HANDLED;
}

/* During a receive, the cur_rx points to the current incoming buffer.
 * When we update through the ring, if the next incoming buffer has
 * not been given to the system, we just set the empty indicator,
 * effectively tossing the packet.
 */
static int
fcc_enet_rx(struct net_device *dev)
{
	struct	fcc_enet_private *cep;
	volatile cbd_t	*bdp;
	struct	sk_buff *skb;
	ushort	pkt_len;

	cep = dev->priv;

	/* First, grab all of the stats for the incoming packet.
	 * These get messed up if we get called due to a busy condition.
	 */
	bdp = cep->cur_rx;

for (;;) {
	if (bdp->cbd_sc & BD_ENET_RX_EMPTY)
		break;

#ifndef final_version
	/* Since we have allocated space to hold a complete frame, both
	 * the first and last indicators should be set.
	 */
	if ((bdp->cbd_sc & (BD_ENET_RX_FIRST | BD_ENET_RX_LAST)) !=
		(BD_ENET_RX_FIRST | BD_ENET_RX_LAST))
			printk("CPM ENET: rcv is not first+last\n");
#endif

	/* Frame too long or too short. */
	if (bdp->cbd_sc & (BD_ENET_RX_LG | BD_ENET_RX_SH))
		cep->stats.rx_length_errors++;
	if (bdp->cbd_sc & BD_ENET_RX_NO)	/* Frame alignment */
		cep->stats.rx_frame_errors++;
	if (bdp->cbd_sc & BD_ENET_RX_CR)	/* CRC Error */
		cep->stats.rx_crc_errors++;
	if (bdp->cbd_sc & BD_ENET_RX_OV)	/* FIFO overrun */
		cep->stats.rx_crc_errors++;
	if (bdp->cbd_sc & BD_ENET_RX_CL)	/* Late Collision */
		cep->stats.rx_frame_errors++;

	if (!(bdp->cbd_sc &
	      (BD_ENET_RX_LG | BD_ENET_RX_SH | BD_ENET_RX_NO | BD_ENET_RX_CR
	       | BD_ENET_RX_OV | BD_ENET_RX_CL)))
	{
		/* Process the incoming frame. */
		cep->stats.rx_packets++;

		/* Remove the FCS from the packet length. */
		pkt_len = bdp->cbd_datlen - 4;
		cep->stats.rx_bytes += pkt_len;

		/* This does 16 byte alignment, much more than we need. */
		skb = dev_alloc_skb(pkt_len);

		if (skb == NULL) {
			printk("%s: Memory squeeze, dropping packet.\n", dev->name);
			cep->stats.rx_dropped++;
		}
		else {
			skb_put(skb,pkt_len);	/* Make room */
			skb_copy_to_linear_data(skb,
				(unsigned char *)__va(bdp->cbd_bufaddr),
				pkt_len);
			skb->protocol=eth_type_trans(skb,dev);
			netif_rx(skb);
		}
	}

	/* Clear the status flags for this buffer. */
	bdp->cbd_sc &= ~BD_ENET_RX_STATS;

	/* Mark the buffer empty. */
	bdp->cbd_sc |= BD_ENET_RX_EMPTY;

	/* Update BD pointer to next entry. */
	if (bdp->cbd_sc & BD_ENET_RX_WRAP)
		bdp = cep->rx_bd_base;
	else
		bdp++;

   }
	cep->cur_rx = (cbd_t *)bdp;

	return 0;
}

static int
fcc_enet_close(struct net_device *dev)
{
#ifdef	CONFIG_USE_MDIO
	struct fcc_enet_private *fep = dev->priv;
#endif

	netif_stop_queue(dev);
	fcc_stop(dev);
#ifdef	CONFIG_USE_MDIO
	if (fep->phy)
		mii_do_cmd(dev, fep->phy->shutdown);
#endif

	return 0;
}

static struct net_device_stats *fcc_enet_get_stats(struct net_device *dev)
{
	struct fcc_enet_private *cep = (struct fcc_enet_private *)dev->priv;

	return &cep->stats;
}

#ifdef	CONFIG_USE_MDIO

/* NOTE: Most of the following comes from the FEC driver for 860. The
 * overall structure of MII code has been retained (as it's proved stable
 * and well-tested), but actual transfer requests are processed "at once"
 * instead of being queued (there's no interrupt-driven MII transfer
 * mechanism, one has to toggle the data/clock bits manually).
 */
static int
mii_queue(struct net_device *dev, int regval, void (*func)(uint, struct net_device *))
{
	struct fcc_enet_private *fep;
	int		retval, tmp;

	/* Add PHY address to register command. */
	fep = dev->priv;
	regval |= fep->phy_addr << 23;

	retval = 0;

	tmp = mii_send_receive(fep->fip, regval);
	if (func)
		func(tmp, dev);

	return retval;
}

static void mii_do_cmd(struct net_device *dev, const phy_cmd_t *c)
{
	int k;

	if(!c)
		return;

	for(k = 0; (c+k)->mii_data != mk_mii_end; k++)
		mii_queue(dev, (c+k)->mii_data, (c+k)->funct);
}

static void mii_parse_sr(uint mii_reg, struct net_device *dev)
{
	volatile struct fcc_enet_private *fep = dev->priv;
	uint s = fep->phy_status;

	s &= ~(PHY_STAT_LINK | PHY_STAT_FAULT | PHY_STAT_ANC);

	if (mii_reg & BMSR_LSTATUS)
		s |= PHY_STAT_LINK;
	if (mii_reg & BMSR_RFAULT)
		s |= PHY_STAT_FAULT;
	if (mii_reg & BMSR_ANEGCOMPLETE)
		s |= PHY_STAT_ANC;

	fep->phy_status = s;
}

static void mii_parse_cr(uint mii_reg, struct net_device *dev)
{
	volatile struct fcc_enet_private *fep = dev->priv;
	uint s = fep->phy_status;

	s &= ~(PHY_CONF_ANE | PHY_CONF_LOOP);

	if (mii_reg & BMCR_ANENABLE)
		s |= PHY_CONF_ANE;
	if (mii_reg & BMCR_LOOPBACK)
		s |= PHY_CONF_LOOP;

	fep->phy_status = s;
}

static void mii_parse_anar(uint mii_reg, struct net_device *dev)
{
	volatile struct fcc_enet_private *fep = dev->priv;
	uint s = fep->phy_status;

	s &= ~(PHY_CONF_SPMASK);

	if (mii_reg & ADVERTISE_10HALF)
		s |= PHY_CONF_10HDX;
	if (mii_reg & ADVERTISE_10FULL)
		s |= PHY_CONF_10FDX;
	if (mii_reg & ADVERTISE_100HALF)
		s |= PHY_CONF_100HDX;
	if (mii_reg & ADVERTISE_100FULL)
		s |= PHY_CONF_100FDX;

	fep->phy_status = s;
}

/* ------------------------------------------------------------------------- */
/* Generic PHY support.  Should work for all PHYs, but does not support link
 * change interrupts.
 */
#ifdef CONFIG_FCC_GENERIC_PHY

static phy_info_t phy_info_generic = {
	0x00000000, /* 0-->match any PHY */
	"GENERIC",

	(const phy_cmd_t []) {  /* config */
		/* advertise only half-duplex capabilities */
		{ mk_mii_write(MII_ADVERTISE, MII_ADVERTISE_HALF),
			mii_parse_anar },

		/* enable auto-negotiation */
		{ mk_mii_write(MII_BMCR, BMCR_ANENABLE), mii_parse_cr },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) {  /* startup */
		/* restart auto-negotiation */
		{ mk_mii_write(MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART),
			NULL },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) { /* ack_int */
		/* We don't actually use the ack_int table with a generic
		 * PHY, but putting a reference to mii_parse_sr here keeps
		 * us from getting a compiler warning about unused static
		 * functions in the case where we only compile in generic
		 * PHY support.
		 */
		{ mk_mii_read(MII_BMSR), mii_parse_sr },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) {  /* shutdown */
		{ mk_mii_end, }
	},
};
#endif	/* ifdef CONFIG_FCC_GENERIC_PHY */

/* ------------------------------------------------------------------------- */
/* The Level one LXT970 is used by many boards				     */

#ifdef CONFIG_FCC_LXT970

#define MII_LXT970_MIRROR    16  /* Mirror register           */
#define MII_LXT970_IER       17  /* Interrupt Enable Register */
#define MII_LXT970_ISR       18  /* Interrupt Status Register */
#define MII_LXT970_CONFIG    19  /* Configuration Register    */
#define MII_LXT970_CSR       20  /* Chip Status Register      */

static void mii_parse_lxt970_csr(uint mii_reg, struct net_device *dev)
{
	volatile struct fcc_enet_private *fep = dev->priv;
	uint s = fep->phy_status;

	s &= ~(PHY_STAT_SPMASK);

	if (mii_reg & 0x0800) {
		if (mii_reg & 0x1000)
			s |= PHY_STAT_100FDX;
		else
			s |= PHY_STAT_100HDX;
	} else {
		if (mii_reg & 0x1000)
			s |= PHY_STAT_10FDX;
		else
			s |= PHY_STAT_10HDX;
	}

	fep->phy_status = s;
}

static phy_info_t phy_info_lxt970 = {
	0x07810000,
	"LXT970",

	(const phy_cmd_t []) {  /* config */
#if 0
//		{ mk_mii_write(MII_ADVERTISE, 0x0021), NULL },

		/* Set default operation of 100-TX....for some reason
		 * some of these bits are set on power up, which is wrong.
		 */
		{ mk_mii_write(MII_LXT970_CONFIG, 0), NULL },
#endif
		{ mk_mii_read(MII_BMCR), mii_parse_cr },
		{ mk_mii_read(MII_ADVERTISE), mii_parse_anar },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) {  /* startup - enable interrupts */
		{ mk_mii_write(MII_LXT970_IER, 0x0002), NULL },
		{ mk_mii_write(MII_BMCR, 0x1200), NULL }, /* autonegotiate */
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) { /* ack_int */
		/* read SR and ISR to acknowledge */

		{ mk_mii_read(MII_BMSR), mii_parse_sr },
		{ mk_mii_read(MII_LXT970_ISR), NULL },

		/* find out the current status */

		{ mk_mii_read(MII_LXT970_CSR), mii_parse_lxt970_csr },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) {  /* shutdown - disable interrupts */
		{ mk_mii_write(MII_LXT970_IER, 0x0000), NULL },
		{ mk_mii_end, }
	},
};

#endif /* CONFIG_FEC_LXT970 */

/* ------------------------------------------------------------------------- */
/* The Level one LXT971 is used on some of my custom boards                  */

#ifdef CONFIG_FCC_LXT971

/* register definitions for the 971 */

#define MII_LXT971_PCR       16  /* Port Control Register     */
#define MII_LXT971_SR2       17  /* Status Register 2         */
#define MII_LXT971_IER       18  /* Interrupt Enable Register */
#define MII_LXT971_ISR       19  /* Interrupt Status Register */
#define MII_LXT971_LCR       20  /* LED Control Register      */
#define MII_LXT971_TCR       30  /* Transmit Control Register */

/*
 * I had some nice ideas of running the MDIO faster...
 * The 971 should support 8MHz and I tried it, but things acted really
 * weird, so 2.5 MHz ought to be enough for anyone...
 */

static void mii_parse_lxt971_sr2(uint mii_reg, struct net_device *dev)
{
	volatile struct fcc_enet_private *fep = dev->priv;
	uint s = fep->phy_status;

	s &= ~(PHY_STAT_SPMASK);

	if (mii_reg & 0x4000) {
		if (mii_reg & 0x0200)
			s |= PHY_STAT_100FDX;
		else
			s |= PHY_STAT_100HDX;
	} else {
		if (mii_reg & 0x0200)
			s |= PHY_STAT_10FDX;
		else
			s |= PHY_STAT_10HDX;
	}
	if (mii_reg & 0x0008)
		s |= PHY_STAT_FAULT;

	fep->phy_status = s;
}

static phy_info_t phy_info_lxt971 = {
	0x0001378e,
	"LXT971",

	(const phy_cmd_t []) {  /* config */
		/* configure link capabilities to advertise */
		{ mk_mii_write(MII_ADVERTISE, MII_ADVERTISE_DEFAULT),
			mii_parse_anar },

		/* enable auto-negotiation */
		{ mk_mii_write(MII_BMCR, BMCR_ANENABLE), mii_parse_cr },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) {  /* startup - enable interrupts */
		{ mk_mii_write(MII_LXT971_IER, 0x00f2), NULL },

		/* restart auto-negotiation */
		{ mk_mii_write(MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART),
			NULL },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) { /* ack_int */
		/* find out the current status */
		{ mk_mii_read(MII_BMSR), NULL },
		{ mk_mii_read(MII_BMSR), mii_parse_sr },
		{ mk_mii_read(MII_LXT971_SR2), mii_parse_lxt971_sr2 },

		/* we only need to read ISR to acknowledge */
		{ mk_mii_read(MII_LXT971_ISR), NULL },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) {  /* shutdown - disable interrupts */
		{ mk_mii_write(MII_LXT971_IER, 0x0000), NULL },
		{ mk_mii_end, }
	},
};

#endif /* CONFIG_FCC_LXT971 */

/* ------------------------------------------------------------------------- */
/* The Quality Semiconductor QS6612 is used on the RPX CLLF                  */

#ifdef CONFIG_FCC_QS6612

/* register definitions */

#define MII_QS6612_MCR       17  /* Mode Control Register      */
#define MII_QS6612_FTR       27  /* Factory Test Register      */
#define MII_QS6612_MCO       28  /* Misc. Control Register     */
#define MII_QS6612_ISR       29  /* Interrupt Source Register  */
#define MII_QS6612_IMR       30  /* Interrupt Mask Register    */
#define MII_QS6612_PCR       31  /* 100BaseTx PHY Control Reg. */

static void mii_parse_qs6612_pcr(uint mii_reg, struct net_device *dev)
{
	volatile struct fcc_enet_private *fep = dev->priv;
	uint s = fep->phy_status;

	s &= ~(PHY_STAT_SPMASK);

	switch((mii_reg >> 2) & 7) {
	case 1: s |= PHY_STAT_10HDX;  break;
	case 2: s |= PHY_STAT_100HDX; break;
	case 5: s |= PHY_STAT_10FDX;  break;
	case 6: s |= PHY_STAT_100FDX; break;
	}

	fep->phy_status = s;
}

static phy_info_t phy_info_qs6612 = {
	0x00181440,
	"QS6612",

	(const phy_cmd_t []) {  /* config */
//	{ mk_mii_write(MII_ADVERTISE, 0x061), NULL }, /* 10  Mbps */

		/* The PHY powers up isolated on the RPX,
		 * so send a command to allow operation.
		 */

		{ mk_mii_write(MII_QS6612_PCR, 0x0dc0), NULL },

		/* parse cr and anar to get some info */

		{ mk_mii_read(MII_BMCR), mii_parse_cr },
		{ mk_mii_read(MII_ADVERTISE), mii_parse_anar },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) {  /* startup - enable interrupts */
		{ mk_mii_write(MII_QS6612_IMR, 0x003a), NULL },
		{ mk_mii_write(MII_BMCR, 0x1200), NULL }, /* autonegotiate */
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) { /* ack_int */

		/* we need to read ISR, SR and ANER to acknowledge */

		{ mk_mii_read(MII_QS6612_ISR), NULL },
		{ mk_mii_read(MII_BMSR), mii_parse_sr },
		{ mk_mii_read(MII_EXPANSION), NULL },

		/* read pcr to get info */

		{ mk_mii_read(MII_QS6612_PCR), mii_parse_qs6612_pcr },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) {  /* shutdown - disable interrupts */
		{ mk_mii_write(MII_QS6612_IMR, 0x0000), NULL },
		{ mk_mii_end, }
	},
};


#endif /* CONFIG_FEC_QS6612 */


/* ------------------------------------------------------------------------- */
/* The Davicom DM9131 is used on the HYMOD board			     */

#ifdef CONFIG_FCC_DM9131

/* register definitions */

#define MII_DM9131_ACR		16	/* Aux. Config Register		*/
#define MII_DM9131_ACSR		17	/* Aux. Config/Status Register	*/
#define MII_DM9131_10TCSR	18	/* 10BaseT Config/Status Reg.	*/
#define MII_DM9131_INTR		21	/* Interrupt Register		*/
#define MII_DM9131_RECR		22	/* Receive Error Counter Reg.	*/
#define MII_DM9131_DISCR	23	/* Disconnect Counter Register	*/

static void mii_parse_dm9131_acsr(uint mii_reg, struct net_device *dev)
{
	volatile struct fcc_enet_private *fep = dev->priv;
	uint s = fep->phy_status;

	s &= ~(PHY_STAT_SPMASK);

	switch ((mii_reg >> 12) & 0xf) {
	case 1: s |= PHY_STAT_10HDX;  break;
	case 2: s |= PHY_STAT_10FDX;  break;
	case 4: s |= PHY_STAT_100HDX; break;
	case 8: s |= PHY_STAT_100FDX; break;
	}

	fep->phy_status = s;
}

static phy_info_t phy_info_dm9131 = {
	0x00181b80,
	"DM9131",

	(const phy_cmd_t []) {  /* config */
		/* parse cr and anar to get some info */
		{ mk_mii_read(MII_BMCR), mii_parse_cr },
		{ mk_mii_read(MII_ADVERTISE), mii_parse_anar },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) {  /* startup - enable interrupts */
		{ mk_mii_write(MII_DM9131_INTR, 0x0002), NULL },
		{ mk_mii_write(MII_BMCR, 0x1200), NULL }, /* autonegotiate */
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) { /* ack_int */

		/* we need to read INTR, SR and ANER to acknowledge */

		{ mk_mii_read(MII_DM9131_INTR), NULL },
		{ mk_mii_read(MII_BMSR), mii_parse_sr },
		{ mk_mii_read(MII_EXPANSION), NULL },

		/* read acsr to get info */

		{ mk_mii_read(MII_DM9131_ACSR), mii_parse_dm9131_acsr },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) {  /* shutdown - disable interrupts */
		{ mk_mii_write(MII_DM9131_INTR, 0x0f00), NULL },
		{ mk_mii_end, }
	},
};


#endif /* CONFIG_FEC_DM9131 */
#ifdef CONFIG_FCC_DM9161
/* ------------------------------------------------------------------------- */
/* DM9161 Control register values */
#define MIIM_DM9161_CR_STOP     0x0400
#define MIIM_DM9161_CR_RSTAN    0x1200

#define MIIM_DM9161_SCR         0x10
#define MIIM_DM9161_SCR_INIT    0x0610

/* DM9161 Specified Configuration and Status Register */
#define MIIM_DM9161_SCSR        0x11
#define MIIM_DM9161_SCSR_100F   0x8000
#define MIIM_DM9161_SCSR_100H   0x4000
#define MIIM_DM9161_SCSR_10F    0x2000
#define MIIM_DM9161_SCSR_10H    0x1000
/* DM9161 10BT register */
#define MIIM_DM9161_10BTCSR 	0x12
#define MIIM_DM9161_10BTCSR_INIT 0x7800
/* DM9161 Interrupt Register */
#define MIIM_DM9161_INTR        0x15
#define MIIM_DM9161_INTR_PEND           0x8000
#define MIIM_DM9161_INTR_DPLX_MASK      0x0800
#define MIIM_DM9161_INTR_SPD_MASK       0x0400
#define MIIM_DM9161_INTR_LINK_MASK      0x0200
#define MIIM_DM9161_INTR_MASK           0x0100
#define MIIM_DM9161_INTR_DPLX_CHANGE    0x0010
#define MIIM_DM9161_INTR_SPD_CHANGE     0x0008
#define MIIM_DM9161_INTR_LINK_CHANGE    0x0004
#define MIIM_DM9161_INTR_INIT           0x0000
#define MIIM_DM9161_INTR_STOP   \
(MIIM_DM9161_INTR_DPLX_MASK | MIIM_DM9161_INTR_SPD_MASK \
  | MIIM_DM9161_INTR_LINK_MASK | MIIM_DM9161_INTR_MASK)

static void mii_parse_dm9161_sr(uint mii_reg, struct net_device * dev)
{
	volatile struct fcc_enet_private *fep = dev->priv;
	uint regstat,  timeout=0xffff;

	while(!(mii_reg & 0x0020) && timeout--)
	{
		regstat=mk_mii_read(MII_BMSR);
	        regstat |= fep->phy_addr <<23;
	        mii_reg = mii_send_receive(fep->fip,regstat);
	}

	mii_parse_sr(mii_reg, dev);
}

static void mii_parse_dm9161_scsr(uint mii_reg, struct net_device * dev)
{
	volatile struct fcc_enet_private *fep = dev->priv;
	uint s = fep->phy_status;

	s &= ~(PHY_STAT_SPMASK);
	switch((mii_reg >>12) & 0xf) {
		case 1:
		{
			s |= PHY_STAT_10HDX;
			printk("10BaseT Half Duplex\n");
			break;
		}
		case 2:
		{
			s |= PHY_STAT_10FDX;
		        printk("10BaseT Full Duplex\n");
			break;
		}
		case 4:
	        {
			s |= PHY_STAT_100HDX;
		        printk("100BaseT Half Duplex\n");
			break;
		}
		case 8:
		{
			s |= PHY_STAT_100FDX;
			printk("100BaseT Full Duplex\n");
			break;
		}
	}

	fep->phy_status = s;

}

static void mii_dm9161_wait(uint mii_reg, struct net_device *dev)
{
	int timeout = HZ;

	/* Davicom takes a bit to come up after a reset,
	 * so wait here for a bit */
	schedule_timeout_uninterruptible(timeout);
}

static phy_info_t phy_info_dm9161 = {
        0x00181b88,
        "Davicom DM9161E",
        (const phy_cmd_t[]) { /* config */
                { mk_mii_write(MII_BMCR, MIIM_DM9161_CR_STOP), NULL},
                /* Do not bypass the scrambler/descrambler */
                { mk_mii_write(MIIM_DM9161_SCR, MIIM_DM9161_SCR_INIT), NULL},
		/* Configure 10BTCSR register */
		{ mk_mii_write(MIIM_DM9161_10BTCSR, MIIM_DM9161_10BTCSR_INIT),NULL},
                /* Configure some basic stuff */
                { mk_mii_write(MII_BMCR, 0x1000), NULL},
		{ mk_mii_read(MII_BMCR), mii_parse_cr },
		{ mk_mii_read(MII_ADVERTISE), mii_parse_anar },
		{ mk_mii_end,}
        },
       (const phy_cmd_t[]) { /* startup */
                /* Restart Auto Negotiation */
                { mk_mii_write(MII_BMCR, MIIM_DM9161_CR_RSTAN), NULL},
                /* Status is read once to clear old link state */
                { mk_mii_read(MII_BMSR), mii_dm9161_wait},
                /* Auto-negotiate */
                { mk_mii_read(MII_BMSR), mii_parse_dm9161_sr},
                /* Read the status */
                { mk_mii_read(MIIM_DM9161_SCSR), mii_parse_dm9161_scsr},
                /* Clear any pending interrupts */
                { mk_mii_read(MIIM_DM9161_INTR), NULL},
                /* Enable Interrupts */
                { mk_mii_write(MIIM_DM9161_INTR, MIIM_DM9161_INTR_INIT), NULL},
                { mk_mii_end,}
        },
       (const phy_cmd_t[]) { /* ack_int */
                { mk_mii_read(MIIM_DM9161_INTR), NULL},
#if 0
		{ mk_mii_read(MII_BMSR), NULL},
		{ mk_mii_read(MII_BMSR), mii_parse_dm9161_sr},
		{ mk_mii_read(MIIM_DM9161_SCSR), mii_parse_dm9161_scsr},
#endif
                { mk_mii_end,}
        },
        (const phy_cmd_t[]) { /* shutdown */
	        { mk_mii_read(MIIM_DM9161_INTR),NULL},
                { mk_mii_write(MIIM_DM9161_INTR, MIIM_DM9161_INTR_STOP), NULL},
	        { mk_mii_end,}
	},
};
#endif /* CONFIG_FCC_DM9161 */

static phy_info_t *phy_info[] = {

#ifdef CONFIG_FCC_LXT970
	&phy_info_lxt970,
#endif /* CONFIG_FEC_LXT970 */

#ifdef CONFIG_FCC_LXT971
	&phy_info_lxt971,
#endif /* CONFIG_FEC_LXT971 */

#ifdef CONFIG_FCC_QS6612
	&phy_info_qs6612,
#endif /* CONFIG_FEC_QS6612 */

#ifdef CONFIG_FCC_DM9131
	&phy_info_dm9131,
#endif /* CONFIG_FEC_DM9131 */

#ifdef CONFIG_FCC_DM9161
	&phy_info_dm9161,
#endif /* CONFIG_FCC_DM9161 */

#ifdef CONFIG_FCC_GENERIC_PHY
	/* Generic PHY support.  This must be the last PHY in the table.
	 * It will be used to support any PHY that doesn't match a previous
	 * entry in the table.
	 */
	&phy_info_generic,
#endif /* CONFIG_FCC_GENERIC_PHY */

	NULL
};

static void mii_display_status(struct work_struct *work)
{
	volatile struct fcc_enet_private *fep =
		container_of(work, struct fcc_enet_private, phy_relink);
	struct net_device *dev = fep->dev;
	uint s = fep->phy_status;

	if (!fep->link && !fep->old_link) {
		/* Link is still down - don't print anything */
		return;
	}

	printk("%s: status: ", dev->name);

	if (!fep->link) {
		printk("link down");
	} else {
		printk("link up");

		switch(s & PHY_STAT_SPMASK) {
		case PHY_STAT_100FDX: printk(", 100 Mbps Full Duplex"); break;
		case PHY_STAT_100HDX: printk(", 100 Mbps Half Duplex"); break;
		case PHY_STAT_10FDX:  printk(", 10 Mbps Full Duplex");  break;
		case PHY_STAT_10HDX:  printk(", 10 Mbps Half Duplex");  break;
		default:
			printk(", Unknown speed/duplex");
		}

		if (s & PHY_STAT_ANC)
			printk(", auto-negotiation complete");
	}

	if (s & PHY_STAT_FAULT)
		printk(", remote fault");

	printk(".\n");
}

static void mii_display_config(struct work_struct *work)
{
	volatile struct fcc_enet_private *fep =
		container_of(work, struct fcc_enet_private,
			     phy_display_config);
	struct net_device *dev = fep->dev;
	uint s = fep->phy_status;

	printk("%s: config: auto-negotiation ", dev->name);

	if (s & PHY_CONF_ANE)
		printk("on");
	else
		printk("off");

	if (s & PHY_CONF_100FDX)
		printk(", 100FDX");
	if (s & PHY_CONF_100HDX)
		printk(", 100HDX");
	if (s & PHY_CONF_10FDX)
		printk(", 10FDX");
	if (s & PHY_CONF_10HDX)
		printk(", 10HDX");
	if (!(s & PHY_CONF_SPMASK))
		printk(", No speed/duplex selected?");

	if (s & PHY_CONF_LOOP)
		printk(", loopback enabled");

	printk(".\n");

	fep->sequence_done = 1;
}

static void mii_relink(struct net_device *dev)
{
	struct fcc_enet_private *fep = dev->priv;
	int duplex = 0;

	fep->old_link = fep->link;
	fep->link = (fep->phy_status & PHY_STAT_LINK) ? 1 : 0;

#ifdef MDIO_DEBUG
	printk("  mii_relink:  link=%d\n", fep->link);
#endif

	if (fep->link) {
		if (fep->phy_status
		    & (PHY_STAT_100FDX | PHY_STAT_10FDX))
			duplex = 1;
		fcc_restart(dev, duplex);
#ifdef MDIO_DEBUG
		printk("  mii_relink:  duplex=%d\n", duplex);
#endif
	}
}

static void mii_queue_relink(uint mii_reg, struct net_device *dev)
{
	struct fcc_enet_private *fep = dev->priv;

	mii_relink(dev);

	schedule_work(&fep->phy_relink);
}

static void mii_queue_config(uint mii_reg, struct net_device *dev)
{
	struct fcc_enet_private *fep = dev->priv;

	schedule_work(&fep->phy_display_config);
}

phy_cmd_t phy_cmd_relink[] = { { mk_mii_read(MII_BMCR), mii_queue_relink },
			       { mk_mii_end, } };
phy_cmd_t phy_cmd_config[] = { { mk_mii_read(MII_BMCR), mii_queue_config },
			       { mk_mii_end, } };


/* Read remainder of PHY ID.
*/
static void
mii_discover_phy3(uint mii_reg, struct net_device *dev)
{
	struct fcc_enet_private *fep;
	int	i;

	fep = dev->priv;
	printk("mii_reg: %08x\n", mii_reg);
	fep->phy_id |= (mii_reg & 0xffff);

	for(i = 0; phy_info[i]; i++)
		if((phy_info[i]->id == (fep->phy_id >> 4)) || !phy_info[i]->id)
			break;

	if(!phy_info[i])
		panic("%s: PHY id 0x%08x is not supported!\n",
		      dev->name, fep->phy_id);

	fep->phy = phy_info[i];
	fep->phy_id_done = 1;

	printk("%s: Phy @ 0x%x, type %s (0x%08x)\n",
		dev->name, fep->phy_addr, fep->phy->name, fep->phy_id);
}

/* Scan all of the MII PHY addresses looking for someone to respond
 * with a valid ID.  This usually happens quickly.
 */
static void
mii_discover_phy(uint mii_reg, struct net_device *dev)
{
	struct fcc_enet_private *fep;
	uint	phytype;

	fep = dev->priv;

	if ((phytype = (mii_reg & 0xffff)) != 0xffff) {

		/* Got first part of ID, now get remainder. */
		fep->phy_id = phytype << 16;
		mii_queue(dev, mk_mii_read(MII_PHYSID2), mii_discover_phy3);
	} else {
		fep->phy_addr++;
		if (fep->phy_addr < 32) {
			mii_queue(dev, mk_mii_read(MII_PHYSID1),
							mii_discover_phy);
		} else {
			printk("fec: No PHY device found.\n");
		}
	}
}
#endif	/* CONFIG_USE_MDIO */

#ifdef PHY_INTERRUPT
/* This interrupt occurs when the PHY detects a link change. */
static irqreturn_t
mii_link_interrupt(int irq, void * dev_id)
{
	struct	net_device *dev = dev_id;
	struct fcc_enet_private *fep = dev->priv;
	fcc_info_t *fip = fep->fip;

	if (fep->phy) {
		/* We don't want to be interrupted by an FCC
		 * interrupt here.
		 */
		disable_irq_nosync(fip->fc_interrupt);

		mii_do_cmd(dev, fep->phy->ack_int);
		/* restart and display status */
		mii_do_cmd(dev, phy_cmd_relink);

		enable_irq(fip->fc_interrupt);
	}
	return IRQ_HANDLED;
}
#endif	/* ifdef PHY_INTERRUPT */

#if 0 /* This should be fixed someday */
/* Set or clear the multicast filter for this adaptor.
 * Skeleton taken from sunlance driver.
 * The CPM Ethernet implementation allows Multicast as well as individual
 * MAC address filtering.  Some of the drivers check to make sure it is
 * a group multicast address, and discard those that are not.  I guess I
 * will do the same for now, but just remove the test if you want
 * individual filtering as well (do the upper net layers want or support
 * this kind of feature?).
 */
static void
set_multicast_list(struct net_device *dev)
{
	struct	fcc_enet_private *cep;
	struct	dev_mc_list *dmi;
	u_char	*mcptr, *tdptr;
	volatile fcc_enet_t *ep;
	int	i, j;

	cep = (struct fcc_enet_private *)dev->priv;

return;
	/* Get pointer to FCC area in parameter RAM.
	*/
	ep = (fcc_enet_t *)dev->base_addr;

	if (dev->flags&IFF_PROMISC) {
	
		/* Log any net taps. */
		printk("%s: Promiscuous mode enabled.\n", dev->name);
		cep->fccp->fcc_fpsmr |= FCC_PSMR_PRO;
	} else {

		cep->fccp->fcc_fpsmr &= ~FCC_PSMR_PRO;

		if (dev->flags & IFF_ALLMULTI) {
			/* Catch all multicast addresses, so set the
			 * filter to all 1's.
			 */
			ep->fen_gaddrh = 0xffffffff;
			ep->fen_gaddrl = 0xffffffff;
		}
		else {
			/* Clear filter and add the addresses in the list.
			*/
			ep->fen_gaddrh = 0;
			ep->fen_gaddrl = 0;

			dmi = dev->mc_list;

			for (i=0; i<dev->mc_count; i++, dmi = dmi->next) {

				/* Only support group multicast for now.
				*/
				if (!(dmi->dmi_addr[0] & 1))
					continue;

				/* The address in dmi_addr is LSB first,
				 * and taddr is MSB first.  We have to
				 * copy bytes MSB first from dmi_addr.
				 */
				mcptr = (u_char *)dmi->dmi_addr + 5;
				tdptr = (u_char *)&ep->fen_taddrh;
				for (j=0; j<6; j++)
					*tdptr++ = *mcptr--;

				/* Ask CPM to run CRC and set bit in
				 * filter mask.
				 */
				cpmp->cp_cpcr = mk_cr_cmd(cep->fip->fc_cpmpage,
						cep->fip->fc_cpmblock, 0x0c,
						CPM_CR_SET_GADDR) | CPM_CR_FLG;
				udelay(10);
				while (cpmp->cp_cpcr & CPM_CR_FLG);
			}
		}
	}
}
#endif /* if 0 */


/* Set the individual MAC address.
 */
int fcc_enet_set_mac_address(struct net_device *dev, void *p)
{
	struct sockaddr *addr= (struct sockaddr *) p;
	struct fcc_enet_private *cep;
	volatile fcc_enet_t *ep;
	unsigned char *eap;
	int i;

	cep = (struct fcc_enet_private *)(dev->priv);
	ep = cep->ep;

        if (netif_running(dev))
                return -EBUSY;

        memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	eap = (unsigned char *) &(ep->fen_paddrh);
	for (i=5; i>=0; i--)
		*eap++ = addr->sa_data[i];

        return 0;
}


/* Initialize the CPM Ethernet on FCC.
 */
static int __init fec_enet_init(void)
{
	struct net_device *dev;
	struct fcc_enet_private *cep;
	fcc_info_t	*fip;
	int	i, np, err;
	volatile	cpm2_map_t		*immap;
	volatile	iop_cpm2_t	*io;

	immap = (cpm2_map_t *)CPM_MAP_ADDR;	/* and to internal registers */
	io = &immap->im_ioport;

	np = sizeof(fcc_ports) / sizeof(fcc_info_t);
	fip = fcc_ports;

	while (np-- > 0) {
		/* Create an Ethernet device instance.
		*/
		dev = alloc_etherdev(sizeof(*cep));
		if (!dev)
			return -ENOMEM;

		cep = dev->priv;
		spin_lock_init(&cep->lock);
		cep->fip = fip;

		init_fcc_shutdown(fip, cep, immap);
		init_fcc_ioports(fip, io, immap);
		init_fcc_param(fip, dev, immap);

		dev->base_addr = (unsigned long)(cep->ep);

		/* The CPM Ethernet specific entries in the device
		 * structure.
		 */
		dev->open = fcc_enet_open;
		dev->hard_start_xmit = fcc_enet_start_xmit;
		dev->tx_timeout = fcc_enet_timeout;
		dev->watchdog_timeo = TX_TIMEOUT;
		dev->stop = fcc_enet_close;
		dev->get_stats = fcc_enet_get_stats;
		/* dev->set_multicast_list = set_multicast_list; */
		dev->set_mac_address = fcc_enet_set_mac_address;

		init_fcc_startup(fip, dev);

		err = register_netdev(dev);
		if (err) {
			free_netdev(dev);
			return err;
		}

		printk("%s: FCC ENET Version 0.3, ", dev->name);
		for (i=0; i<5; i++)
			printk("%02x:", dev->dev_addr[i]);
		printk("%02x\n", dev->dev_addr[5]);

#ifdef	CONFIG_USE_MDIO
		/* Queue up command to detect the PHY and initialize the
	 	* remainder of the interface.
	 	*/
		cep->phy_id_done = 0;
		cep->phy_addr = fip->fc_phyaddr;
		mii_queue(dev, mk_mii_read(MII_PHYSID1), mii_discover_phy);
		INIT_WORK(&cep->phy_relink, mii_display_status);
		INIT_WORK(&cep->phy_display_config, mii_display_config);
		cep->dev = dev;
#endif	/* CONFIG_USE_MDIO */

		fip++;
	}

	return 0;
}
module_init(fec_enet_init);

/* Make sure the device is shut down during initialization.
*/
static void __init
init_fcc_shutdown(fcc_info_t *fip, struct fcc_enet_private *cep,
						volatile cpm2_map_t *immap)
{
	volatile	fcc_enet_t	*ep;
	volatile	fcc_t		*fccp;

	/* Get pointer to FCC area in parameter RAM.
	*/
	ep = (fcc_enet_t *)(&immap->im_dprambase[fip->fc_proff]);

	/* And another to the FCC register area.
	*/
	fccp = (volatile fcc_t *)(&immap->im_fcc[fip->fc_fccnum]);
	cep->fccp = fccp;		/* Keep the pointers handy */
	cep->ep = ep;

	/* Disable receive and transmit in case someone left it running.
	*/
	fccp->fcc_gfmr &= ~(FCC_GFMR_ENR | FCC_GFMR_ENT);
}

/* Initialize the I/O pins for the FCC Ethernet.
*/
static void __init
init_fcc_ioports(fcc_info_t *fip, volatile iop_cpm2_t *io,
						volatile cpm2_map_t *immap)
{

	/* FCC1 pins are on port A/C.  FCC2/3 are port B/C.
	*/
	if (fip->fc_proff == PROFF_FCC1) {
		/* Configure port A and C pins for FCC1 Ethernet.
		 */
		io->iop_pdira &= ~PA1_DIRA_BOUT;
		io->iop_pdira |= PA1_DIRA_BIN;
		io->iop_psora &= ~PA1_PSORA_BOUT;
		io->iop_psora |= PA1_PSORA_BIN;
		io->iop_ppara |= (PA1_DIRA_BOUT | PA1_DIRA_BIN);
	}
	if (fip->fc_proff == PROFF_FCC2) {
		/* Configure port B and C pins for FCC Ethernet.
		 */
		io->iop_pdirb &= ~PB2_DIRB_BOUT;
		io->iop_pdirb |= PB2_DIRB_BIN;
		io->iop_psorb &= ~PB2_PSORB_BOUT;
		io->iop_psorb |= PB2_PSORB_BIN;
		io->iop_pparb |= (PB2_DIRB_BOUT | PB2_DIRB_BIN);
	}
	if (fip->fc_proff == PROFF_FCC3) {
		/* Configure port B and C pins for FCC Ethernet.
		 */
		io->iop_pdirb &= ~PB3_DIRB_BOUT;
		io->iop_pdirb |= PB3_DIRB_BIN;
		io->iop_psorb &= ~PB3_PSORB_BOUT;
		io->iop_psorb |= PB3_PSORB_BIN;
		io->iop_pparb |= (PB3_DIRB_BOUT | PB3_DIRB_BIN);

		io->iop_pdirc &= ~PC3_DIRC_BOUT;
		io->iop_pdirc |= PC3_DIRC_BIN;
		io->iop_psorc &= ~PC3_PSORC_BOUT;
		io->iop_psorc |= PC3_PSORC_BIN;
		io->iop_pparc |= (PC3_DIRC_BOUT | PC3_DIRC_BIN);

	}

	/* Port C has clocks......
	*/
	io->iop_psorc &= ~(fip->fc_trxclocks);
	io->iop_pdirc &= ~(fip->fc_trxclocks);
	io->iop_pparc |= fip->fc_trxclocks;

#ifdef	CONFIG_USE_MDIO
	/* ....and the MII serial clock/data.
	*/
	io->iop_pdatc |= (fip->fc_mdio | fip->fc_mdck);
	io->iop_podrc &= ~(fip->fc_mdio | fip->fc_mdck);
	io->iop_pdirc |= (fip->fc_mdio | fip->fc_mdck);
	io->iop_pparc &= ~(fip->fc_mdio | fip->fc_mdck);
#endif	/* CONFIG_USE_MDIO */

	/* Configure Serial Interface clock routing.
	 * First, clear all FCC bits to zero,
	 * then set the ones we want.
	 */
	immap->im_cpmux.cmx_fcr &= ~(fip->fc_clockmask);
	immap->im_cpmux.cmx_fcr |= fip->fc_clockroute;
}

static void __init
init_fcc_param(fcc_info_t *fip, struct net_device *dev,
						volatile cpm2_map_t *immap)
{
	unsigned char	*eap;
	unsigned long	mem_addr;
	bd_t		*bd;
	int		i, j;
	struct		fcc_enet_private *cep;
	volatile	fcc_enet_t	*ep;
	volatile	cbd_t		*bdp;
	volatile	cpm_cpm2_t	*cp;

	cep = (struct fcc_enet_private *)(dev->priv);
	ep = cep->ep;
	cp = cpmp;

	bd = (bd_t *)__res;

	/* Zero the whole thing.....I must have missed some individually.
	 * It works when I do this.
	 */
	memset((char *)ep, 0, sizeof(fcc_enet_t));

	/* Allocate space for the buffer descriptors from regular memory.
	 * Initialize base addresses for the buffer descriptors.
	 */
	cep->rx_bd_base = kmalloc(sizeof(cbd_t) * RX_RING_SIZE,
			GFP_KERNEL | GFP_DMA);
	ep->fen_genfcc.fcc_rbase = __pa(cep->rx_bd_base);
	cep->tx_bd_base = kmalloc(sizeof(cbd_t) * TX_RING_SIZE,
			GFP_KERNEL | GFP_DMA);
	ep->fen_genfcc.fcc_tbase = __pa(cep->tx_bd_base);

	cep->dirty_tx = cep->cur_tx = cep->tx_bd_base;
	cep->cur_rx = cep->rx_bd_base;

	ep->fen_genfcc.fcc_rstate = (CPMFCR_GBL | CPMFCR_EB) << 24;
	ep->fen_genfcc.fcc_tstate = (CPMFCR_GBL | CPMFCR_EB) << 24;

	/* Set maximum bytes per receive buffer.
	 * It must be a multiple of 32.
	 */
	ep->fen_genfcc.fcc_mrblr = PKT_MAXBLR_SIZE;

	/* Allocate space in the reserved FCC area of DPRAM for the
	 * internal buffers.  No one uses this space (yet), so we
	 * can do this.  Later, we will add resource management for
	 * this area.
	 */
	mem_addr = CPM_FCC_SPECIAL_BASE + (fip->fc_fccnum * 128);
	ep->fen_genfcc.fcc_riptr = mem_addr;
	ep->fen_genfcc.fcc_tiptr = mem_addr+32;
	ep->fen_padptr = mem_addr+64;
	memset((char *)(&(immap->im_dprambase[(mem_addr+64)])), 0x88, 32);

	ep->fen_genfcc.fcc_rbptr = 0;
	ep->fen_genfcc.fcc_tbptr = 0;
	ep->fen_genfcc.fcc_rcrc = 0;
	ep->fen_genfcc.fcc_tcrc = 0;
	ep->fen_genfcc.fcc_res1 = 0;
	ep->fen_genfcc.fcc_res2 = 0;

	ep->fen_camptr = 0;	/* CAM isn't used in this driver */

	/* Set CRC preset and mask.
	*/
	ep->fen_cmask = 0xdebb20e3;
	ep->fen_cpres = 0xffffffff;

	ep->fen_crcec = 0;	/* CRC Error counter */
	ep->fen_alec = 0;	/* alignment error counter */
	ep->fen_disfc = 0;	/* discard frame counter */
	ep->fen_retlim = 15;	/* Retry limit threshold */
	ep->fen_pper = 0;	/* Normal persistence */

	/* Clear hash filter tables.
	*/
	ep->fen_gaddrh = 0;
	ep->fen_gaddrl = 0;
	ep->fen_iaddrh = 0;
	ep->fen_iaddrl = 0;

	/* Clear the Out-of-sequence TxBD.
	*/
	ep->fen_tfcstat = 0;
	ep->fen_tfclen = 0;
	ep->fen_tfcptr = 0;

	ep->fen_mflr = PKT_MAXBUF_SIZE;   /* maximum frame length register */
	ep->fen_minflr = PKT_MINBUF_SIZE;  /* minimum frame length register */

	/* Set Ethernet station address.
	 *
	 * This is supplied in the board information structure, so we
	 * copy that into the controller.
	 * So, far we have only been given one Ethernet address. We make
	 * it unique by setting a few bits in the upper byte of the
	 * non-static part of the address.
	 */
	eap = (unsigned char *)&(ep->fen_paddrh);
	for (i=5; i>=0; i--) {

/*
 * The EP8260 only uses FCC3, so we can safely give it the real
 * MAC address.
 */
#ifdef CONFIG_SBC82xx
		if (i == 5) {
			/* bd->bi_enetaddr holds the SCC0 address; the FCC
			   devices count up from there */
			dev->dev_addr[i] = bd->bi_enetaddr[i] & ~3;
			dev->dev_addr[i] += 1 + fip->fc_fccnum;
			*eap++ = dev->dev_addr[i];
		}
#else
#ifndef CONFIG_RPX8260
		if (i == 3) {
			dev->dev_addr[i] = bd->bi_enetaddr[i];
			dev->dev_addr[i] |= (1 << (7 - fip->fc_fccnum));
			*eap++ = dev->dev_addr[i];
		} else
#endif
		{
			*eap++ = dev->dev_addr[i] = bd->bi_enetaddr[i];
		}
#endif
	}

	ep->fen_taddrh = 0;
	ep->fen_taddrm = 0;
	ep->fen_taddrl = 0;

	ep->fen_maxd1 = PKT_MAXDMA_SIZE;	/* maximum DMA1 length */
	ep->fen_maxd2 = PKT_MAXDMA_SIZE;	/* maximum DMA2 length */

	/* Clear stat counters, in case we ever enable RMON.
	*/
	ep->fen_octc = 0;
	ep->fen_colc = 0;
	ep->fen_broc = 0;
	ep->fen_mulc = 0;
	ep->fen_uspc = 0;
	ep->fen_frgc = 0;
	ep->fen_ospc = 0;
	ep->fen_jbrc = 0;
	ep->fen_p64c = 0;
	ep->fen_p65c = 0;
	ep->fen_p128c = 0;
	ep->fen_p256c = 0;
	ep->fen_p512c = 0;
	ep->fen_p1024c = 0;

	ep->fen_rfthr = 0;	/* Suggested by manual */
	ep->fen_rfcnt = 0;
	ep->fen_cftype = 0;

	/* Now allocate the host memory pages and initialize the
	 * buffer descriptors.
	 */
	bdp = cep->tx_bd_base;
	for (i=0; i<TX_RING_SIZE; i++) {

		/* Initialize the BD for every fragment in the page.
		*/
		bdp->cbd_sc = 0;
		bdp->cbd_datlen = 0;
		bdp->cbd_bufaddr = 0;
		bdp++;
	}

	/* Set the last buffer to wrap.
	*/
	bdp--;
	bdp->cbd_sc |= BD_SC_WRAP;

	bdp = cep->rx_bd_base;
	for (i=0; i<FCC_ENET_RX_PAGES; i++) {

		/* Allocate a page.
		*/
		mem_addr = __get_free_page(GFP_KERNEL);

		/* Initialize the BD for every fragment in the page.
		*/
		for (j=0; j<FCC_ENET_RX_FRPPG; j++) {
			bdp->cbd_sc = BD_ENET_RX_EMPTY | BD_ENET_RX_INTR;
			bdp->cbd_datlen = 0;
			bdp->cbd_bufaddr = __pa(mem_addr);
			mem_addr += FCC_ENET_RX_FRSIZE;
			bdp++;
		}
	}

	/* Set the last buffer to wrap.
	*/
	bdp--;
	bdp->cbd_sc |= BD_SC_WRAP;

	/* Let's re-initialize the channel now.  We have to do it later
	 * than the manual describes because we have just now finished
	 * the BD initialization.
	 */
	cp->cp_cpcr = mk_cr_cmd(fip->fc_cpmpage, fip->fc_cpmblock, 0x0c,
			CPM_CR_INIT_TRX) | CPM_CR_FLG;
	while (cp->cp_cpcr & CPM_CR_FLG);

	cep->skb_cur = cep->skb_dirty = 0;
}

/* Let 'er rip.
*/
static void __init
init_fcc_startup(fcc_info_t *fip, struct net_device *dev)
{
	volatile fcc_t	*fccp;
	struct fcc_enet_private *cep;

	cep = (struct fcc_enet_private *)(dev->priv);
	fccp = cep->fccp;

#ifdef CONFIG_RPX8260
#ifdef PHY_INTERRUPT
	/* Route PHY interrupt to IRQ.  The following code only works for
	 * IRQ1 - IRQ7.  It does not work for Port C interrupts.
	 */
	*((volatile u_char *) (RPX_CSR_ADDR + 13)) &= ~BCSR13_FETH_IRQMASK;
	*((volatile u_char *) (RPX_CSR_ADDR + 13)) |=
		((PHY_INTERRUPT - SIU_INT_IRQ1 + 1) << 4);
#endif
	/* Initialize MDIO pins. */
	*((volatile u_char *) (RPX_CSR_ADDR + 4)) &= ~BCSR4_MII_MDC;
	*((volatile u_char *) (RPX_CSR_ADDR + 4)) |=
		BCSR4_MII_READ | BCSR4_MII_MDIO;
	/* Enable external LXT971 PHY. */
	*((volatile u_char *) (RPX_CSR_ADDR + 4)) |= BCSR4_EN_PHY;
	udelay(1000);
	*((volatile u_char *) (RPX_CSR_ADDR+ 4)) |= BCSR4_EN_MII;
	udelay(1000);
#endif	/* ifdef CONFIG_RPX8260 */

	fccp->fcc_fcce = 0xffff;	/* Clear any pending events */

	/* Leave FCC interrupts masked for now.  Will be unmasked by
	 * fcc_restart().
	 */
	fccp->fcc_fccm = 0;

	/* Install our interrupt handler.
	*/
	if (request_irq(fip->fc_interrupt, fcc_enet_interrupt, 0, "fenet",
				dev) < 0)
		printk("Can't get FCC IRQ %d\n", fip->fc_interrupt);

#ifdef	PHY_INTERRUPT
	/* Make IRQn edge triggered.  This does not work if PHY_INTERRUPT is
	 * on Port C.
	 */
	((volatile cpm2_map_t *) CPM_MAP_ADDR)->im_intctl.ic_siexr |=
		(1 << (14 - (PHY_INTERRUPT - SIU_INT_IRQ1)));

	if (request_irq(PHY_INTERRUPT, mii_link_interrupt, 0,
							"mii", dev) < 0)
		printk(KERN_CRIT "Can't get MII IRQ %d\n", PHY_INTERRUPT);
#endif	/* PHY_INTERRUPT */

	/* Set GFMR to enable Ethernet operating mode.
	 */
	fccp->fcc_gfmr = (FCC_GFMR_TCI | FCC_GFMR_MODE_ENET);

	/* Set sync/delimiters.
	*/
	fccp->fcc_fdsr = 0xd555;

	/* Set protocol specific processing mode for Ethernet.
	 * This has to be adjusted for Full Duplex operation after we can
	 * determine how to detect that.
	 */
	fccp->fcc_fpsmr = FCC_PSMR_ENCRC;

#ifdef CONFIG_PQ2ADS
	/* Enable the PHY. */
	*(volatile uint *)(BCSR_ADDR + 4) &= ~BCSR1_FETHIEN;
	*(volatile uint *)(BCSR_ADDR + 4) |=  BCSR1_FETH_RST;
#endif
#if defined(CONFIG_PQ2ADS) || defined(CONFIG_PQ2FADS)
	/* Enable the 2nd PHY. */
	*(volatile uint *)(BCSR_ADDR + 12) &= ~BCSR3_FETHIEN2;
	*(volatile uint *)(BCSR_ADDR + 12) |=  BCSR3_FETH2_RST;
#endif

#if defined(CONFIG_USE_MDIO) || defined(CONFIG_TQM8260)
	/* start in full duplex mode, and negotiate speed
	 */
	fcc_restart (dev, 1);
#else
	/* start in half duplex mode
	 */
	fcc_restart (dev, 0);
#endif
}

#ifdef	CONFIG_USE_MDIO
/* MII command/status interface.
 * I'm not going to describe all of the details.  You can find the
 * protocol definition in many other places, including the data sheet
 * of most PHY parts.
 * I wonder what "they" were thinking (maybe weren't) when they leave
 * the I2C in the CPM but I have to toggle these bits......
 */
#ifdef CONFIG_RPX8260
	/* The EP8260 has the MDIO pins in a BCSR instead of on Port C
	 * like most other boards.
	 */
#define MDIO_ADDR ((volatile u_char *)(RPX_CSR_ADDR + 4))
#define MAKE_MDIO_OUTPUT *MDIO_ADDR &= ~BCSR4_MII_READ
#define MAKE_MDIO_INPUT  *MDIO_ADDR |=  BCSR4_MII_READ | BCSR4_MII_MDIO
#define OUT_MDIO(bit)				\
	if (bit)				\
		*MDIO_ADDR |=  BCSR4_MII_MDIO;	\
	else					\
		*MDIO_ADDR &= ~BCSR4_MII_MDIO;
#define IN_MDIO (*MDIO_ADDR & BCSR4_MII_MDIO)
#define OUT_MDC(bit)				\
	if (bit)				\
		*MDIO_ADDR |=  BCSR4_MII_MDC;	\
	else					\
		*MDIO_ADDR &= ~BCSR4_MII_MDC;
#else	/* ifdef CONFIG_RPX8260 */
	/* This is for the usual case where the MDIO pins are on Port C.
	 */
#define MDIO_ADDR (((volatile cpm2_map_t *)CPM_MAP_ADDR)->im_ioport)
#define MAKE_MDIO_OUTPUT MDIO_ADDR.iop_pdirc |= fip->fc_mdio
#define MAKE_MDIO_INPUT MDIO_ADDR.iop_pdirc &= ~fip->fc_mdio
#define OUT_MDIO(bit)				\
	if (bit)				\
		MDIO_ADDR.iop_pdatc |= fip->fc_mdio;	\
	else					\
		MDIO_ADDR.iop_pdatc &= ~fip->fc_mdio;
#define IN_MDIO ((MDIO_ADDR.iop_pdatc) & fip->fc_mdio)
#define OUT_MDC(bit)				\
	if (bit)				\
		MDIO_ADDR.iop_pdatc |= fip->fc_mdck;	\
	else					\
		MDIO_ADDR.iop_pdatc &= ~fip->fc_mdck;
#endif	/* ifdef CONFIG_RPX8260 */

static uint
mii_send_receive(fcc_info_t *fip, uint cmd)
{
	uint		retval;
	int		read_op, i, off;
	const int	us = 1;

	read_op = ((cmd & 0xf0000000) == 0x60000000);

	/* Write preamble
	 */
	OUT_MDIO(1);
	MAKE_MDIO_OUTPUT;
	OUT_MDIO(1);
	for (i = 0; i < 32; i++)
	{
		udelay(us);
		OUT_MDC(1);
		udelay(us);
		OUT_MDC(0);
	}

	/* Write data
	 */
	for (i = 0, off = 31; i < (read_op ? 14 : 32); i++, --off)
	{
		OUT_MDIO((cmd >> off) & 0x00000001);
		udelay(us);
		OUT_MDC(1);
		udelay(us);
		OUT_MDC(0);
	}

	retval = cmd;

	if (read_op)
	{
		retval >>= 16;

		MAKE_MDIO_INPUT;
		udelay(us);
		OUT_MDC(1);
		udelay(us);
		OUT_MDC(0);

		for (i = 0; i < 16; i++)
		{
			udelay(us);
			OUT_MDC(1);
			udelay(us);
			retval <<= 1;
			if (IN_MDIO)
				retval++;
			OUT_MDC(0);
		}
	}

	MAKE_MDIO_INPUT;
	udelay(us);
	OUT_MDC(1);
	udelay(us);
	OUT_MDC(0);

	return retval;
}
#endif	/* CONFIG_USE_MDIO */

static void
fcc_stop(struct net_device *dev)
{
	struct fcc_enet_private	*fep= (struct fcc_enet_private *)(dev->priv);
	volatile fcc_t	*fccp = fep->fccp;
	fcc_info_t *fip = fep->fip;
	volatile fcc_enet_t *ep = fep->ep;
	volatile cpm_cpm2_t *cp = cpmp;
	volatile cbd_t *bdp;
	int i;

	if ((fccp->fcc_gfmr & (FCC_GFMR_ENR | FCC_GFMR_ENT)) == 0)
		return;	/* already down */

	fccp->fcc_fccm = 0;

	/* issue the graceful stop tx command */
	while (cp->cp_cpcr & CPM_CR_FLG);
	cp->cp_cpcr = mk_cr_cmd(fip->fc_cpmpage, fip->fc_cpmblock,
				0x0c, CPM_CR_GRA_STOP_TX) | CPM_CR_FLG;
	while (cp->cp_cpcr & CPM_CR_FLG);

	/* Disable transmit/receive */
	fccp->fcc_gfmr &= ~(FCC_GFMR_ENR | FCC_GFMR_ENT);

	/* issue the restart tx command */
	fccp->fcc_fcce = FCC_ENET_GRA;
	while (cp->cp_cpcr & CPM_CR_FLG);
	cp->cp_cpcr = mk_cr_cmd(fip->fc_cpmpage, fip->fc_cpmblock,
				0x0c, CPM_CR_RESTART_TX) | CPM_CR_FLG;
	while (cp->cp_cpcr & CPM_CR_FLG);

	/* free tx buffers */
	fep->skb_cur = fep->skb_dirty = 0;
	for (i=0; i<=TX_RING_MOD_MASK; i++) {
		if (fep->tx_skbuff[i] != NULL) {
			dev_kfree_skb(fep->tx_skbuff[i]);
			fep->tx_skbuff[i] = NULL;
		}
	}
	fep->dirty_tx = fep->cur_tx = fep->tx_bd_base;
	fep->tx_free = TX_RING_SIZE;
	ep->fen_genfcc.fcc_tbptr = ep->fen_genfcc.fcc_tbase;

	/* Initialize the tx buffer descriptors. */
	bdp = fep->tx_bd_base;
	for (i=0; i<TX_RING_SIZE; i++) {
		bdp->cbd_sc = 0;
		bdp->cbd_datlen = 0;
		bdp->cbd_bufaddr = 0;
		bdp++;
	}
	/* Set the last buffer to wrap. */
	bdp--;
	bdp->cbd_sc |= BD_SC_WRAP;
}

static void
fcc_restart(struct net_device *dev, int duplex)
{
	struct fcc_enet_private	*fep = (struct fcc_enet_private *)(dev->priv);
	volatile fcc_t	*fccp = fep->fccp;

	/* stop any transmissions in progress */
	fcc_stop(dev);

	if (duplex)
		fccp->fcc_fpsmr |= FCC_PSMR_FDE | FCC_PSMR_LPB;
	else
		fccp->fcc_fpsmr &= ~(FCC_PSMR_FDE | FCC_PSMR_LPB);

	/* Enable interrupts for transmit error, complete frame
	 * received, and any transmit buffer we have also set the
	 * interrupt flag.
	 */
	fccp->fcc_fccm = (FCC_ENET_TXE | FCC_ENET_RXF | FCC_ENET_TXB);

	/* Enable transmit/receive */
	fccp->fcc_gfmr |= FCC_GFMR_ENR | FCC_GFMR_ENT;
}

static int
fcc_enet_open(struct net_device *dev)
{
	struct fcc_enet_private *fep = dev->priv;

#ifdef	CONFIG_USE_MDIO
	fep->sequence_done = 0;
	fep->link = 0;

	if (fep->phy) {
		fcc_restart(dev, 0);	/* always start in half-duplex */
		mii_do_cmd(dev, fep->phy->ack_int);
		mii_do_cmd(dev, fep->phy->config);
		mii_do_cmd(dev, phy_cmd_config);  /* display configuration */
		while(!fep->sequence_done)
			schedule();

		mii_do_cmd(dev, fep->phy->startup);
		netif_start_queue(dev);
		return 0;		/* Success */
	}
	return -ENODEV;		/* No PHY we understand */
#else
	fep->link = 1;
	fcc_restart(dev, 0);	/* always start in half-duplex */
	netif_start_queue(dev);
	return 0;					/* Always succeed */
#endif	/* CONFIG_USE_MDIO */
}

