/*
 * Copyright (C) 2001,2002,2003,2004 Broadcom Corporation
 * Copyright (c) 2006, 2007  Maciej W. Rozycki
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *
 * This driver is designed for the Broadcom SiByte SOC built-in
 * Ethernet controllers. Written by Mitch Lichtenberg at Broadcom Corp.
 *
 * Updated to the driver model and the PHY abstraction layer
 * by Maciej W. Rozycki.
 */

#include <linux/bug.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/prefetch.h>

#include <asm/cache.h>
#include <asm/io.h>
#include <asm/processor.h>	/* Processor type for cache alignment. */

/* Operational parameters that usually are not changed. */

#define CONFIG_SBMAC_COALESCE

/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (2*HZ)


MODULE_AUTHOR("Mitch Lichtenberg (Broadcom Corp.)");
MODULE_DESCRIPTION("Broadcom SiByte SOC GB Ethernet driver");

/* A few user-configurable values which may be modified when a driver
   module is loaded. */

/* 1 normal messages, 0 quiet .. 7 verbose. */
static int debug = 1;
module_param(debug, int, S_IRUGO);
MODULE_PARM_DESC(debug, "Debug messages");

#ifdef CONFIG_SBMAC_COALESCE
static int int_pktcnt_tx = 255;
module_param(int_pktcnt_tx, int, S_IRUGO);
MODULE_PARM_DESC(int_pktcnt_tx, "TX packet count");

static int int_timeout_tx = 255;
module_param(int_timeout_tx, int, S_IRUGO);
MODULE_PARM_DESC(int_timeout_tx, "TX timeout value");

static int int_pktcnt_rx = 64;
module_param(int_pktcnt_rx, int, S_IRUGO);
MODULE_PARM_DESC(int_pktcnt_rx, "RX packet count");

static int int_timeout_rx = 64;
module_param(int_timeout_rx, int, S_IRUGO);
MODULE_PARM_DESC(int_timeout_rx, "RX timeout value");
#endif

#include <asm/sibyte/board.h>
#include <asm/sibyte/sb1250.h>
#if defined(CONFIG_SIBYTE_BCM1x55) || defined(CONFIG_SIBYTE_BCM1x80)
#include <asm/sibyte/bcm1480_regs.h>
#include <asm/sibyte/bcm1480_int.h>
#define R_MAC_DMA_OODPKTLOST_RX	R_MAC_DMA_OODPKTLOST
#elif defined(CONFIG_SIBYTE_SB1250) || defined(CONFIG_SIBYTE_BCM112X)
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_int.h>
#else
#error invalid SiByte MAC configuration
#endif
#include <asm/sibyte/sb1250_scd.h>
#include <asm/sibyte/sb1250_mac.h>
#include <asm/sibyte/sb1250_dma.h>

#if defined(CONFIG_SIBYTE_BCM1x55) || defined(CONFIG_SIBYTE_BCM1x80)
#define UNIT_INT(n)		(K_BCM1480_INT_MAC_0 + ((n) * 2))
#elif defined(CONFIG_SIBYTE_SB1250) || defined(CONFIG_SIBYTE_BCM112X)
#define UNIT_INT(n)		(K_INT_MAC_0 + (n))
#else
#error invalid SiByte MAC configuration
#endif

#ifdef K_INT_PHY
#define SBMAC_PHY_INT			K_INT_PHY
#else
#define SBMAC_PHY_INT			PHY_POLL
#endif

/**********************************************************************
 *  Simple types
 ********************************************************************* */

enum sbmac_speed {
	sbmac_speed_none = 0,
	sbmac_speed_10 = SPEED_10,
	sbmac_speed_100 = SPEED_100,
	sbmac_speed_1000 = SPEED_1000,
};

enum sbmac_duplex {
	sbmac_duplex_none = -1,
	sbmac_duplex_half = DUPLEX_HALF,
	sbmac_duplex_full = DUPLEX_FULL,
};

enum sbmac_fc {
	sbmac_fc_none,
	sbmac_fc_disabled,
	sbmac_fc_frame,
	sbmac_fc_collision,
	sbmac_fc_carrier,
};

enum sbmac_state {
	sbmac_state_uninit,
	sbmac_state_off,
	sbmac_state_on,
	sbmac_state_broken,
};


/**********************************************************************
 *  Macros
 ********************************************************************* */


#define SBDMA_NEXTBUF(d,f) ((((d)->f+1) == (d)->sbdma_dscrtable_end) ? \
			  (d)->sbdma_dscrtable : (d)->f+1)


#define NUMCACHEBLKS(x) (((x)+SMP_CACHE_BYTES-1)/SMP_CACHE_BYTES)

#define SBMAC_MAX_TXDESCR	256
#define SBMAC_MAX_RXDESCR	256

#define ETHER_ADDR_LEN		6
#define ENET_PACKET_SIZE	1518
/*#define ENET_PACKET_SIZE	9216 */

/**********************************************************************
 *  DMA Descriptor structure
 ********************************************************************* */

struct sbdmadscr {
	uint64_t  dscr_a;
	uint64_t  dscr_b;
};

/**********************************************************************
 *  DMA Controller structure
 ********************************************************************* */

struct sbmacdma {

	/*
	 * This stuff is used to identify the channel and the registers
	 * associated with it.
	 */
	struct sbmac_softc	*sbdma_eth;	/* back pointer to associated
						   MAC */
	int			sbdma_channel;	/* channel number */
	int			sbdma_txdir;	/* direction (1=transmit) */
	int			sbdma_maxdescr;	/* total # of descriptors
						   in ring */
#ifdef CONFIG_SBMAC_COALESCE
	int			sbdma_int_pktcnt;
						/* # descriptors rx/tx
						   before interrupt */
	int			sbdma_int_timeout;
						/* # usec rx/tx interrupt */
#endif
	void __iomem		*sbdma_config0;	/* DMA config register 0 */
	void __iomem		*sbdma_config1;	/* DMA config register 1 */
	void __iomem		*sbdma_dscrbase;
						/* descriptor base address */
	void __iomem		*sbdma_dscrcnt;	/* descriptor count register */
	void __iomem		*sbdma_curdscr;	/* current descriptor
						   address */
	void __iomem		*sbdma_oodpktlost;
						/* pkt drop (rx only) */

	/*
	 * This stuff is for maintenance of the ring
	 */
	void			*sbdma_dscrtable_unaligned;
	struct sbdmadscr	*sbdma_dscrtable;
						/* base of descriptor table */
	struct sbdmadscr	*sbdma_dscrtable_end;
						/* end of descriptor table */
	struct sk_buff		**sbdma_ctxtable;
						/* context table, one
						   per descr */
	dma_addr_t		sbdma_dscrtable_phys;
						/* and also the phys addr */
	struct sbdmadscr	*sbdma_addptr;	/* next dscr for sw to add */
	struct sbdmadscr	*sbdma_remptr;	/* next dscr for sw
						   to remove */
};


/**********************************************************************
 *  Ethernet softc structure
 ********************************************************************* */

struct sbmac_softc {

	/*
	 * Linux-specific things
	 */
	struct net_device	*sbm_dev;	/* pointer to linux device */
	struct napi_struct	napi;
	struct phy_device	*phy_dev;	/* the associated PHY device */
	struct mii_bus		*mii_bus;	/* the MII bus */
	int			phy_irq[PHY_MAX_ADDR];
	spinlock_t		sbm_lock;	/* spin lock */
	int			sbm_devflags;	/* current device flags */

	/*
	 * Controller-specific things
	 */
	void __iomem		*sbm_base;	/* MAC's base address */
	enum sbmac_state	sbm_state;	/* current state */

	void __iomem		*sbm_macenable;	/* MAC Enable Register */
	void __iomem		*sbm_maccfg;	/* MAC Config Register */
	void __iomem		*sbm_fifocfg;	/* FIFO Config Register */
	void __iomem		*sbm_framecfg;	/* Frame Config Register */
	void __iomem		*sbm_rxfilter;	/* Receive Filter Register */
	void __iomem		*sbm_isr;	/* Interrupt Status Register */
	void __iomem		*sbm_imr;	/* Interrupt Mask Register */
	void __iomem		*sbm_mdio;	/* MDIO Register */

	enum sbmac_speed	sbm_speed;	/* current speed */
	enum sbmac_duplex	sbm_duplex;	/* current duplex */
	enum sbmac_fc		sbm_fc;		/* cur. flow control setting */
	int			sbm_pause;	/* current pause setting */
	int			sbm_link;	/* current link state */

	unsigned char		sbm_hwaddr[ETHER_ADDR_LEN];

	struct sbmacdma		sbm_txdma;	/* only channel 0 for now */
	struct sbmacdma		sbm_rxdma;
	int			rx_hw_checksum;
	int			sbe_idx;
};


/**********************************************************************
 *  Externs
 ********************************************************************* */

/**********************************************************************
 *  Prototypes
 ********************************************************************* */

static void sbdma_initctx(struct sbmacdma *d, struct sbmac_softc *s, int chan,
			  int txrx, int maxdescr);
static void sbdma_channel_start(struct sbmacdma *d, int rxtx);
static int sbdma_add_rcvbuffer(struct sbmac_softc *sc, struct sbmacdma *d,
			       struct sk_buff *m);
static int sbdma_add_txbuffer(struct sbmacdma *d, struct sk_buff *m);
static void sbdma_emptyring(struct sbmacdma *d);
static void sbdma_fillring(struct sbmac_softc *sc, struct sbmacdma *d);
static int sbdma_rx_process(struct sbmac_softc *sc, struct sbmacdma *d,
			    int work_to_do, int poll);
static void sbdma_tx_process(struct sbmac_softc *sc, struct sbmacdma *d,
			     int poll);
static int sbmac_initctx(struct sbmac_softc *s);
static void sbmac_channel_start(struct sbmac_softc *s);
static void sbmac_channel_stop(struct sbmac_softc *s);
static enum sbmac_state sbmac_set_channel_state(struct sbmac_softc *,
						enum sbmac_state);
static void sbmac_promiscuous_mode(struct sbmac_softc *sc, int onoff);
static uint64_t sbmac_addr2reg(unsigned char *ptr);
static irqreturn_t sbmac_intr(int irq, void *dev_instance);
static int sbmac_start_tx(struct sk_buff *skb, struct net_device *dev);
static void sbmac_setmulti(struct sbmac_softc *sc);
static int sbmac_init(struct platform_device *pldev, long long base);
static int sbmac_set_speed(struct sbmac_softc *s, enum sbmac_speed speed);
static int sbmac_set_duplex(struct sbmac_softc *s, enum sbmac_duplex duplex,
			    enum sbmac_fc fc);

static int sbmac_open(struct net_device *dev);
static void sbmac_tx_timeout (struct net_device *dev);
static void sbmac_set_rx_mode(struct net_device *dev);
static int sbmac_mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int sbmac_close(struct net_device *dev);
static int sbmac_poll(struct napi_struct *napi, int budget);

static void sbmac_mii_poll(struct net_device *dev);
static int sbmac_mii_probe(struct net_device *dev);

static void sbmac_mii_sync(void __iomem *sbm_mdio);
static void sbmac_mii_senddata(void __iomem *sbm_mdio, unsigned int data,
			       int bitcnt);
static int sbmac_mii_read(struct mii_bus *bus, int phyaddr, int regidx);
static int sbmac_mii_write(struct mii_bus *bus, int phyaddr, int regidx,
			   u16 val);


/**********************************************************************
 *  Globals
 ********************************************************************* */

static char sbmac_string[] = "sb1250-mac";

static char sbmac_mdio_string[] = "sb1250-mac-mdio";


/**********************************************************************
 *  MDIO constants
 ********************************************************************* */

#define	MII_COMMAND_START	0x01
#define	MII_COMMAND_READ	0x02
#define	MII_COMMAND_WRITE	0x01
#define	MII_COMMAND_ACK		0x02

#define M_MAC_MDIO_DIR_OUTPUT	0		/* for clarity */

#define ENABLE 		1
#define DISABLE		0

/**********************************************************************
 *  SBMAC_MII_SYNC(sbm_mdio)
 *
 *  Synchronize with the MII - send a pattern of bits to the MII
 *  that will guarantee that it is ready to accept a command.
 *
 *  Input parameters:
 *  	   sbm_mdio - address of the MAC's MDIO register
 *
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbmac_mii_sync(void __iomem *sbm_mdio)
{
	int cnt;
	uint64_t bits;
	int mac_mdio_genc;

	mac_mdio_genc = __raw_readq(sbm_mdio) & M_MAC_GENC;

	bits = M_MAC_MDIO_DIR_OUTPUT | M_MAC_MDIO_OUT;

	__raw_writeq(bits | mac_mdio_genc, sbm_mdio);

	for (cnt = 0; cnt < 32; cnt++) {
		__raw_writeq(bits | M_MAC_MDC | mac_mdio_genc, sbm_mdio);
		__raw_writeq(bits | mac_mdio_genc, sbm_mdio);
	}
}

/**********************************************************************
 *  SBMAC_MII_SENDDATA(sbm_mdio, data, bitcnt)
 *
 *  Send some bits to the MII.  The bits to be sent are right-
 *  justified in the 'data' parameter.
 *
 *  Input parameters:
 *  	   sbm_mdio - address of the MAC's MDIO register
 *  	   data     - data to send
 *  	   bitcnt   - number of bits to send
 ********************************************************************* */

static void sbmac_mii_senddata(void __iomem *sbm_mdio, unsigned int data,
			       int bitcnt)
{
	int i;
	uint64_t bits;
	unsigned int curmask;
	int mac_mdio_genc;

	mac_mdio_genc = __raw_readq(sbm_mdio) & M_MAC_GENC;

	bits = M_MAC_MDIO_DIR_OUTPUT;
	__raw_writeq(bits | mac_mdio_genc, sbm_mdio);

	curmask = 1 << (bitcnt - 1);

	for (i = 0; i < bitcnt; i++) {
		if (data & curmask)
			bits |= M_MAC_MDIO_OUT;
		else bits &= ~M_MAC_MDIO_OUT;
		__raw_writeq(bits | mac_mdio_genc, sbm_mdio);
		__raw_writeq(bits | M_MAC_MDC | mac_mdio_genc, sbm_mdio);
		__raw_writeq(bits | mac_mdio_genc, sbm_mdio);
		curmask >>= 1;
	}
}



/**********************************************************************
 *  SBMAC_MII_READ(bus, phyaddr, regidx)
 *  Read a PHY register.
 *
 *  Input parameters:
 *  	   bus     - MDIO bus handle
 *  	   phyaddr - PHY's address
 *  	   regnum  - index of register to read
 *
 *  Return value:
 *  	   value read, or 0xffff if an error occurred.
 ********************************************************************* */

static int sbmac_mii_read(struct mii_bus *bus, int phyaddr, int regidx)
{
	struct sbmac_softc *sc = (struct sbmac_softc *)bus->priv;
	void __iomem *sbm_mdio = sc->sbm_mdio;
	int idx;
	int error;
	int regval;
	int mac_mdio_genc;

	/*
	 * Synchronize ourselves so that the PHY knows the next
	 * thing coming down is a command
	 */
	sbmac_mii_sync(sbm_mdio);

	/*
	 * Send the data to the PHY.  The sequence is
	 * a "start" command (2 bits)
	 * a "read" command (2 bits)
	 * the PHY addr (5 bits)
	 * the register index (5 bits)
	 */
	sbmac_mii_senddata(sbm_mdio, MII_COMMAND_START, 2);
	sbmac_mii_senddata(sbm_mdio, MII_COMMAND_READ, 2);
	sbmac_mii_senddata(sbm_mdio, phyaddr, 5);
	sbmac_mii_senddata(sbm_mdio, regidx, 5);

	mac_mdio_genc = __raw_readq(sbm_mdio) & M_MAC_GENC;

	/*
	 * Switch the port around without a clock transition.
	 */
	__raw_writeq(M_MAC_MDIO_DIR_INPUT | mac_mdio_genc, sbm_mdio);

	/*
	 * Send out a clock pulse to signal we want the status
	 */
	__raw_writeq(M_MAC_MDIO_DIR_INPUT | M_MAC_MDC | mac_mdio_genc,
		     sbm_mdio);
	__raw_writeq(M_MAC_MDIO_DIR_INPUT | mac_mdio_genc, sbm_mdio);

	/*
	 * If an error occurred, the PHY will signal '1' back
	 */
	error = __raw_readq(sbm_mdio) & M_MAC_MDIO_IN;

	/*
	 * Issue an 'idle' clock pulse, but keep the direction
	 * the same.
	 */
	__raw_writeq(M_MAC_MDIO_DIR_INPUT | M_MAC_MDC | mac_mdio_genc,
		     sbm_mdio);
	__raw_writeq(M_MAC_MDIO_DIR_INPUT | mac_mdio_genc, sbm_mdio);

	regval = 0;

	for (idx = 0; idx < 16; idx++) {
		regval <<= 1;

		if (error == 0) {
			if (__raw_readq(sbm_mdio) & M_MAC_MDIO_IN)
				regval |= 1;
		}

		__raw_writeq(M_MAC_MDIO_DIR_INPUT | M_MAC_MDC | mac_mdio_genc,
			     sbm_mdio);
		__raw_writeq(M_MAC_MDIO_DIR_INPUT | mac_mdio_genc, sbm_mdio);
	}

	/* Switch back to output */
	__raw_writeq(M_MAC_MDIO_DIR_OUTPUT | mac_mdio_genc, sbm_mdio);

	if (error == 0)
		return regval;
	return 0xffff;
}


/**********************************************************************
 *  SBMAC_MII_WRITE(bus, phyaddr, regidx, regval)
 *
 *  Write a value to a PHY register.
 *
 *  Input parameters:
 *  	   bus     - MDIO bus handle
 *  	   phyaddr - PHY to use
 *  	   regidx  - register within the PHY
 *  	   regval  - data to write to register
 *
 *  Return value:
 *  	   0 for success
 ********************************************************************* */

static int sbmac_mii_write(struct mii_bus *bus, int phyaddr, int regidx,
			   u16 regval)
{
	struct sbmac_softc *sc = (struct sbmac_softc *)bus->priv;
	void __iomem *sbm_mdio = sc->sbm_mdio;
	int mac_mdio_genc;

	sbmac_mii_sync(sbm_mdio);

	sbmac_mii_senddata(sbm_mdio, MII_COMMAND_START, 2);
	sbmac_mii_senddata(sbm_mdio, MII_COMMAND_WRITE, 2);
	sbmac_mii_senddata(sbm_mdio, phyaddr, 5);
	sbmac_mii_senddata(sbm_mdio, regidx, 5);
	sbmac_mii_senddata(sbm_mdio, MII_COMMAND_ACK, 2);
	sbmac_mii_senddata(sbm_mdio, regval, 16);

	mac_mdio_genc = __raw_readq(sbm_mdio) & M_MAC_GENC;

	__raw_writeq(M_MAC_MDIO_DIR_OUTPUT | mac_mdio_genc, sbm_mdio);

	return 0;
}



/**********************************************************************
 *  SBDMA_INITCTX(d,s,chan,txrx,maxdescr)
 *
 *  Initialize a DMA channel context.  Since there are potentially
 *  eight DMA channels per MAC, it's nice to do this in a standard
 *  way.
 *
 *  Input parameters:
 *  	   d - struct sbmacdma (DMA channel context)
 *  	   s - struct sbmac_softc (pointer to a MAC)
 *  	   chan - channel number (0..1 right now)
 *  	   txrx - Identifies DMA_TX or DMA_RX for channel direction
 *      maxdescr - number of descriptors
 *
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbdma_initctx(struct sbmacdma *d, struct sbmac_softc *s, int chan,
			  int txrx, int maxdescr)
{
#ifdef CONFIG_SBMAC_COALESCE
	int int_pktcnt, int_timeout;
#endif

	/*
	 * Save away interesting stuff in the structure
	 */

	d->sbdma_eth       = s;
	d->sbdma_channel   = chan;
	d->sbdma_txdir     = txrx;

#if 0
	/* RMON clearing */
	s->sbe_idx =(s->sbm_base - A_MAC_BASE_0)/MAC_SPACING;
#endif

	__raw_writeq(0, s->sbm_base + R_MAC_RMON_TX_BYTES);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_COLLISIONS);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_LATE_COL);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_EX_COL);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_FCS_ERROR);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_TX_ABORT);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_TX_BAD);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_TX_GOOD);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_TX_RUNT);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_TX_OVERSIZE);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_RX_BYTES);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_RX_MCAST);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_RX_BCAST);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_RX_BAD);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_RX_GOOD);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_RX_RUNT);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_RX_OVERSIZE);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_RX_FCS_ERROR);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_RX_LENGTH_ERROR);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_RX_CODE_ERROR);
	__raw_writeq(0, s->sbm_base + R_MAC_RMON_RX_ALIGN_ERROR);

	/*
	 * initialize register pointers
	 */

	d->sbdma_config0 =
		s->sbm_base + R_MAC_DMA_REGISTER(txrx,chan,R_MAC_DMA_CONFIG0);
	d->sbdma_config1 =
		s->sbm_base + R_MAC_DMA_REGISTER(txrx,chan,R_MAC_DMA_CONFIG1);
	d->sbdma_dscrbase =
		s->sbm_base + R_MAC_DMA_REGISTER(txrx,chan,R_MAC_DMA_DSCR_BASE);
	d->sbdma_dscrcnt =
		s->sbm_base + R_MAC_DMA_REGISTER(txrx,chan,R_MAC_DMA_DSCR_CNT);
	d->sbdma_curdscr =
		s->sbm_base + R_MAC_DMA_REGISTER(txrx,chan,R_MAC_DMA_CUR_DSCRADDR);
	if (d->sbdma_txdir)
		d->sbdma_oodpktlost = NULL;
	else
		d->sbdma_oodpktlost =
			s->sbm_base + R_MAC_DMA_REGISTER(txrx,chan,R_MAC_DMA_OODPKTLOST_RX);

	/*
	 * Allocate memory for the ring
	 */

	d->sbdma_maxdescr = maxdescr;

	d->sbdma_dscrtable_unaligned = kcalloc(d->sbdma_maxdescr + 1,
					       sizeof(*d->sbdma_dscrtable),
					       GFP_KERNEL);

	/*
	 * The descriptor table must be aligned to at least 16 bytes or the
	 * MAC will corrupt it.
	 */
	d->sbdma_dscrtable = (struct sbdmadscr *)
			     ALIGN((unsigned long)d->sbdma_dscrtable_unaligned,
				   sizeof(*d->sbdma_dscrtable));

	d->sbdma_dscrtable_end = d->sbdma_dscrtable + d->sbdma_maxdescr;

	d->sbdma_dscrtable_phys = virt_to_phys(d->sbdma_dscrtable);

	/*
	 * And context table
	 */

	d->sbdma_ctxtable = kcalloc(d->sbdma_maxdescr,
				    sizeof(*d->sbdma_ctxtable), GFP_KERNEL);

#ifdef CONFIG_SBMAC_COALESCE
	/*
	 * Setup Rx/Tx DMA coalescing defaults
	 */

	int_pktcnt = (txrx == DMA_TX) ? int_pktcnt_tx : int_pktcnt_rx;
	if ( int_pktcnt ) {
		d->sbdma_int_pktcnt = int_pktcnt;
	} else {
		d->sbdma_int_pktcnt = 1;
	}

	int_timeout = (txrx == DMA_TX) ? int_timeout_tx : int_timeout_rx;
	if ( int_timeout ) {
		d->sbdma_int_timeout = int_timeout;
	} else {
		d->sbdma_int_timeout = 0;
	}
#endif

}

/**********************************************************************
 *  SBDMA_CHANNEL_START(d)
 *
 *  Initialize the hardware registers for a DMA channel.
 *
 *  Input parameters:
 *  	   d - DMA channel to init (context must be previously init'd
 *         rxtx - DMA_RX or DMA_TX depending on what type of channel
 *
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbdma_channel_start(struct sbmacdma *d, int rxtx)
{
	/*
	 * Turn on the DMA channel
	 */

#ifdef CONFIG_SBMAC_COALESCE
	__raw_writeq(V_DMA_INT_TIMEOUT(d->sbdma_int_timeout) |
		       0, d->sbdma_config1);
	__raw_writeq(M_DMA_EOP_INT_EN |
		       V_DMA_RINGSZ(d->sbdma_maxdescr) |
		       V_DMA_INT_PKTCNT(d->sbdma_int_pktcnt) |
		       0, d->sbdma_config0);
#else
	__raw_writeq(0, d->sbdma_config1);
	__raw_writeq(V_DMA_RINGSZ(d->sbdma_maxdescr) |
		       0, d->sbdma_config0);
#endif

	__raw_writeq(d->sbdma_dscrtable_phys, d->sbdma_dscrbase);

	/*
	 * Initialize ring pointers
	 */

	d->sbdma_addptr = d->sbdma_dscrtable;
	d->sbdma_remptr = d->sbdma_dscrtable;
}

/**********************************************************************
 *  SBDMA_CHANNEL_STOP(d)
 *
 *  Initialize the hardware registers for a DMA channel.
 *
 *  Input parameters:
 *  	   d - DMA channel to init (context must be previously init'd
 *
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbdma_channel_stop(struct sbmacdma *d)
{
	/*
	 * Turn off the DMA channel
	 */

	__raw_writeq(0, d->sbdma_config1);

	__raw_writeq(0, d->sbdma_dscrbase);

	__raw_writeq(0, d->sbdma_config0);

	/*
	 * Zero ring pointers
	 */

	d->sbdma_addptr = NULL;
	d->sbdma_remptr = NULL;
}

static inline void sbdma_align_skb(struct sk_buff *skb,
				   unsigned int power2, unsigned int offset)
{
	unsigned char *addr = skb->data;
	unsigned char *newaddr = PTR_ALIGN(addr, power2);

	skb_reserve(skb, newaddr - addr + offset);
}


/**********************************************************************
 *  SBDMA_ADD_RCVBUFFER(d,sb)
 *
 *  Add a buffer to the specified DMA channel.   For receive channels,
 *  this queues a buffer for inbound packets.
 *
 *  Input parameters:
 *	   sc - softc structure
 *  	    d - DMA channel descriptor
 * 	   sb - sk_buff to add, or NULL if we should allocate one
 *
 *  Return value:
 *  	   0 if buffer could not be added (ring is full)
 *  	   1 if buffer added successfully
 ********************************************************************* */


static int sbdma_add_rcvbuffer(struct sbmac_softc *sc, struct sbmacdma *d,
			       struct sk_buff *sb)
{
	struct net_device *dev = sc->sbm_dev;
	struct sbdmadscr *dsc;
	struct sbdmadscr *nextdsc;
	struct sk_buff *sb_new = NULL;
	int pktsize = ENET_PACKET_SIZE;

	/* get pointer to our current place in the ring */

	dsc = d->sbdma_addptr;
	nextdsc = SBDMA_NEXTBUF(d,sbdma_addptr);

	/*
	 * figure out if the ring is full - if the next descriptor
	 * is the same as the one that we're going to remove from
	 * the ring, the ring is full
	 */

	if (nextdsc == d->sbdma_remptr) {
		return -ENOSPC;
	}

	/*
	 * Allocate a sk_buff if we don't already have one.
	 * If we do have an sk_buff, reset it so that it's empty.
	 *
	 * Note: sk_buffs don't seem to be guaranteed to have any sort
	 * of alignment when they are allocated.  Therefore, allocate enough
	 * extra space to make sure that:
	 *
	 *    1. the data does not start in the middle of a cache line.
	 *    2. The data does not end in the middle of a cache line
	 *    3. The buffer can be aligned such that the IP addresses are
	 *       naturally aligned.
	 *
	 *  Remember, the SOCs MAC writes whole cache lines at a time,
	 *  without reading the old contents first.  So, if the sk_buff's
	 *  data portion starts in the middle of a cache line, the SOC
	 *  DMA will trash the beginning (and ending) portions.
	 */

	if (sb == NULL) {
		sb_new = netdev_alloc_skb(dev, ENET_PACKET_SIZE +
					       SMP_CACHE_BYTES * 2 +
					       NET_IP_ALIGN);
		if (sb_new == NULL) {
			pr_info("%s: sk_buff allocation failed\n",
			       d->sbdma_eth->sbm_dev->name);
			return -ENOBUFS;
		}

		sbdma_align_skb(sb_new, SMP_CACHE_BYTES, NET_IP_ALIGN);
	}
	else {
		sb_new = sb;
		/*
		 * nothing special to reinit buffer, it's already aligned
		 * and sb->data already points to a good place.
		 */
	}

	/*
	 * fill in the descriptor
	 */

#ifdef CONFIG_SBMAC_COALESCE
	/*
	 * Do not interrupt per DMA transfer.
	 */
	dsc->dscr_a = virt_to_phys(sb_new->data) |
		V_DMA_DSCRA_A_SIZE(NUMCACHEBLKS(pktsize + NET_IP_ALIGN)) | 0;
#else
	dsc->dscr_a = virt_to_phys(sb_new->data) |
		V_DMA_DSCRA_A_SIZE(NUMCACHEBLKS(pktsize + NET_IP_ALIGN)) |
		M_DMA_DSCRA_INTERRUPT;
#endif

	/* receiving: no options */
	dsc->dscr_b = 0;

	/*
	 * fill in the context
	 */

	d->sbdma_ctxtable[dsc-d->sbdma_dscrtable] = sb_new;

	/*
	 * point at next packet
	 */

	d->sbdma_addptr = nextdsc;

	/*
	 * Give the buffer to the DMA engine.
	 */

	__raw_writeq(1, d->sbdma_dscrcnt);

	return 0;					/* we did it */
}

/**********************************************************************
 *  SBDMA_ADD_TXBUFFER(d,sb)
 *
 *  Add a transmit buffer to the specified DMA channel, causing a
 *  transmit to start.
 *
 *  Input parameters:
 *  	   d - DMA channel descriptor
 * 	   sb - sk_buff to add
 *
 *  Return value:
 *  	   0 transmit queued successfully
 *  	   otherwise error code
 ********************************************************************* */


static int sbdma_add_txbuffer(struct sbmacdma *d, struct sk_buff *sb)
{
	struct sbdmadscr *dsc;
	struct sbdmadscr *nextdsc;
	uint64_t phys;
	uint64_t ncb;
	int length;

	/* get pointer to our current place in the ring */

	dsc = d->sbdma_addptr;
	nextdsc = SBDMA_NEXTBUF(d,sbdma_addptr);

	/*
	 * figure out if the ring is full - if the next descriptor
	 * is the same as the one that we're going to remove from
	 * the ring, the ring is full
	 */

	if (nextdsc == d->sbdma_remptr) {
		return -ENOSPC;
	}

	/*
	 * Under Linux, it's not necessary to copy/coalesce buffers
	 * like it is on NetBSD.  We think they're all contiguous,
	 * but that may not be true for GBE.
	 */

	length = sb->len;

	/*
	 * fill in the descriptor.  Note that the number of cache
	 * blocks in the descriptor is the number of blocks
	 * *spanned*, so we need to add in the offset (if any)
	 * while doing the calculation.
	 */

	phys = virt_to_phys(sb->data);
	ncb = NUMCACHEBLKS(length+(phys & (SMP_CACHE_BYTES - 1)));

	dsc->dscr_a = phys |
		V_DMA_DSCRA_A_SIZE(ncb) |
#ifndef CONFIG_SBMAC_COALESCE
		M_DMA_DSCRA_INTERRUPT |
#endif
		M_DMA_ETHTX_SOP;

	/* transmitting: set outbound options and length */

	dsc->dscr_b = V_DMA_DSCRB_OPTIONS(K_DMA_ETHTX_APPENDCRC_APPENDPAD) |
		V_DMA_DSCRB_PKT_SIZE(length);

	/*
	 * fill in the context
	 */

	d->sbdma_ctxtable[dsc-d->sbdma_dscrtable] = sb;

	/*
	 * point at next packet
	 */

	d->sbdma_addptr = nextdsc;

	/*
	 * Give the buffer to the DMA engine.
	 */

	__raw_writeq(1, d->sbdma_dscrcnt);

	return 0;					/* we did it */
}




/**********************************************************************
 *  SBDMA_EMPTYRING(d)
 *
 *  Free all allocated sk_buffs on the specified DMA channel;
 *
 *  Input parameters:
 *  	   d  - DMA channel
 *
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbdma_emptyring(struct sbmacdma *d)
{
	int idx;
	struct sk_buff *sb;

	for (idx = 0; idx < d->sbdma_maxdescr; idx++) {
		sb = d->sbdma_ctxtable[idx];
		if (sb) {
			dev_kfree_skb(sb);
			d->sbdma_ctxtable[idx] = NULL;
		}
	}
}


/**********************************************************************
 *  SBDMA_FILLRING(d)
 *
 *  Fill the specified DMA channel (must be receive channel)
 *  with sk_buffs
 *
 *  Input parameters:
 *	   sc - softc structure
 *  	    d - DMA channel
 *
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbdma_fillring(struct sbmac_softc *sc, struct sbmacdma *d)
{
	int idx;

	for (idx = 0; idx < SBMAC_MAX_RXDESCR - 1; idx++) {
		if (sbdma_add_rcvbuffer(sc, d, NULL) != 0)
			break;
	}
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void sbmac_netpoll(struct net_device *netdev)
{
	struct sbmac_softc *sc = netdev_priv(netdev);
	int irq = sc->sbm_dev->irq;

	__raw_writeq(0, sc->sbm_imr);

	sbmac_intr(irq, netdev);

#ifdef CONFIG_SBMAC_COALESCE
	__raw_writeq(((M_MAC_INT_EOP_COUNT | M_MAC_INT_EOP_TIMER) << S_MAC_TX_CH0) |
	((M_MAC_INT_EOP_COUNT | M_MAC_INT_EOP_TIMER) << S_MAC_RX_CH0),
	sc->sbm_imr);
#else
	__raw_writeq((M_MAC_INT_CHANNEL << S_MAC_TX_CH0) |
	(M_MAC_INT_CHANNEL << S_MAC_RX_CH0), sc->sbm_imr);
#endif
}
#endif

/**********************************************************************
 *  SBDMA_RX_PROCESS(sc,d,work_to_do,poll)
 *
 *  Process "completed" receive buffers on the specified DMA channel.
 *
 *  Input parameters:
 *            sc - softc structure
 *  	       d - DMA channel context
 *    work_to_do - no. of packets to process before enabling interrupt
 *                 again (for NAPI)
 *          poll - 1: using polling (for NAPI)
 *
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static int sbdma_rx_process(struct sbmac_softc *sc, struct sbmacdma *d,
			    int work_to_do, int poll)
{
	struct net_device *dev = sc->sbm_dev;
	int curidx;
	int hwidx;
	struct sbdmadscr *dsc;
	struct sk_buff *sb;
	int len;
	int work_done = 0;
	int dropped = 0;

	prefetch(d);

again:
	/* Check if the HW dropped any frames */
	dev->stats.rx_fifo_errors
	    += __raw_readq(sc->sbm_rxdma.sbdma_oodpktlost) & 0xffff;
	__raw_writeq(0, sc->sbm_rxdma.sbdma_oodpktlost);

	while (work_to_do-- > 0) {
		/*
		 * figure out where we are (as an index) and where
		 * the hardware is (also as an index)
		 *
		 * This could be done faster if (for example) the
		 * descriptor table was page-aligned and contiguous in
		 * both virtual and physical memory -- you could then
		 * just compare the low-order bits of the virtual address
		 * (sbdma_remptr) and the physical address (sbdma_curdscr CSR)
		 */

		dsc = d->sbdma_remptr;
		curidx = dsc - d->sbdma_dscrtable;

		prefetch(dsc);
		prefetch(&d->sbdma_ctxtable[curidx]);

		hwidx = ((__raw_readq(d->sbdma_curdscr) & M_DMA_CURDSCR_ADDR) -
			 d->sbdma_dscrtable_phys) /
			sizeof(*d->sbdma_dscrtable);

		/*
		 * If they're the same, that means we've processed all
		 * of the descriptors up to (but not including) the one that
		 * the hardware is working on right now.
		 */

		if (curidx == hwidx)
			goto done;

		/*
		 * Otherwise, get the packet's sk_buff ptr back
		 */

		sb = d->sbdma_ctxtable[curidx];
		d->sbdma_ctxtable[curidx] = NULL;

		len = (int)G_DMA_DSCRB_PKT_SIZE(dsc->dscr_b) - 4;

		/*
		 * Check packet status.  If good, process it.
		 * If not, silently drop it and put it back on the
		 * receive ring.
		 */

		if (likely (!(dsc->dscr_a & M_DMA_ETHRX_BAD))) {

			/*
			 * Add a new buffer to replace the old one.  If we fail
			 * to allocate a buffer, we're going to drop this
			 * packet and put it right back on the receive ring.
			 */

			if (unlikely(sbdma_add_rcvbuffer(sc, d, NULL) ==
				     -ENOBUFS)) {
				dev->stats.rx_dropped++;
				/* Re-add old buffer */
				sbdma_add_rcvbuffer(sc, d, sb);
				/* No point in continuing at the moment */
				printk(KERN_ERR "dropped packet (1)\n");
				d->sbdma_remptr = SBDMA_NEXTBUF(d,sbdma_remptr);
				goto done;
			} else {
				/*
				 * Set length into the packet
				 */
				skb_put(sb,len);

				/*
				 * Buffer has been replaced on the
				 * receive ring.  Pass the buffer to
				 * the kernel
				 */
				sb->protocol = eth_type_trans(sb,d->sbdma_eth->sbm_dev);
				/* Check hw IPv4/TCP checksum if supported */
				if (sc->rx_hw_checksum == ENABLE) {
					if (!((dsc->dscr_a) & M_DMA_ETHRX_BADIP4CS) &&
					    !((dsc->dscr_a) & M_DMA_ETHRX_BADTCPCS)) {
						sb->ip_summed = CHECKSUM_UNNECESSARY;
						/* don't need to set sb->csum */
					} else {
						skb_checksum_none_assert(sb);
					}
				}
				prefetch(sb->data);
				prefetch((const void *)(((char *)sb->data)+32));
				if (poll)
					dropped = netif_receive_skb(sb);
				else
					dropped = netif_rx(sb);

				if (dropped == NET_RX_DROP) {
					dev->stats.rx_dropped++;
					d->sbdma_remptr = SBDMA_NEXTBUF(d,sbdma_remptr);
					goto done;
				}
				else {
					dev->stats.rx_bytes += len;
					dev->stats.rx_packets++;
				}
			}
		} else {
			/*
			 * Packet was mangled somehow.  Just drop it and
			 * put it back on the receive ring.
			 */
			dev->stats.rx_errors++;
			sbdma_add_rcvbuffer(sc, d, sb);
		}


		/*
		 * .. and advance to the next buffer.
		 */

		d->sbdma_remptr = SBDMA_NEXTBUF(d,sbdma_remptr);
		work_done++;
	}
	if (!poll) {
		work_to_do = 32;
		goto again; /* collect fifo drop statistics again */
	}
done:
	return work_done;
}

/**********************************************************************
 *  SBDMA_TX_PROCESS(sc,d)
 *
 *  Process "completed" transmit buffers on the specified DMA channel.
 *  This is normally called within the interrupt service routine.
 *  Note that this isn't really ideal for priority channels, since
 *  it processes all of the packets on a given channel before
 *  returning.
 *
 *  Input parameters:
 *      sc - softc structure
 *  	 d - DMA channel context
 *    poll - 1: using polling (for NAPI)
 *
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbdma_tx_process(struct sbmac_softc *sc, struct sbmacdma *d,
			     int poll)
{
	struct net_device *dev = sc->sbm_dev;
	int curidx;
	int hwidx;
	struct sbdmadscr *dsc;
	struct sk_buff *sb;
	unsigned long flags;
	int packets_handled = 0;

	spin_lock_irqsave(&(sc->sbm_lock), flags);

	if (d->sbdma_remptr == d->sbdma_addptr)
	  goto end_unlock;

	hwidx = ((__raw_readq(d->sbdma_curdscr) & M_DMA_CURDSCR_ADDR) -
		 d->sbdma_dscrtable_phys) / sizeof(*d->sbdma_dscrtable);

	for (;;) {
		/*
		 * figure out where we are (as an index) and where
		 * the hardware is (also as an index)
		 *
		 * This could be done faster if (for example) the
		 * descriptor table was page-aligned and contiguous in
		 * both virtual and physical memory -- you could then
		 * just compare the low-order bits of the virtual address
		 * (sbdma_remptr) and the physical address (sbdma_curdscr CSR)
		 */

		curidx = d->sbdma_remptr - d->sbdma_dscrtable;

		/*
		 * If they're the same, that means we've processed all
		 * of the descriptors up to (but not including) the one that
		 * the hardware is working on right now.
		 */

		if (curidx == hwidx)
			break;

		/*
		 * Otherwise, get the packet's sk_buff ptr back
		 */

		dsc = &(d->sbdma_dscrtable[curidx]);
		sb = d->sbdma_ctxtable[curidx];
		d->sbdma_ctxtable[curidx] = NULL;

		/*
		 * Stats
		 */

		dev->stats.tx_bytes += sb->len;
		dev->stats.tx_packets++;

		/*
		 * for transmits, we just free buffers.
		 */

		dev_kfree_skb_irq(sb);

		/*
		 * .. and advance to the next buffer.
		 */

		d->sbdma_remptr = SBDMA_NEXTBUF(d,sbdma_remptr);

		packets_handled++;

	}

	/*
	 * Decide if we should wake up the protocol or not.
	 * Other drivers seem to do this when we reach a low
	 * watermark on the transmit queue.
	 */

	if (packets_handled)
		netif_wake_queue(d->sbdma_eth->sbm_dev);

end_unlock:
	spin_unlock_irqrestore(&(sc->sbm_lock), flags);

}



/**********************************************************************
 *  SBMAC_INITCTX(s)
 *
 *  Initialize an Ethernet context structure - this is called
 *  once per MAC on the 1250.  Memory is allocated here, so don't
 *  call it again from inside the ioctl routines that bring the
 *  interface up/down
 *
 *  Input parameters:
 *  	   s - sbmac context structure
 *
 *  Return value:
 *  	   0
 ********************************************************************* */

static int sbmac_initctx(struct sbmac_softc *s)
{

	/*
	 * figure out the addresses of some ports
	 */

	s->sbm_macenable = s->sbm_base + R_MAC_ENABLE;
	s->sbm_maccfg    = s->sbm_base + R_MAC_CFG;
	s->sbm_fifocfg   = s->sbm_base + R_MAC_THRSH_CFG;
	s->sbm_framecfg  = s->sbm_base + R_MAC_FRAMECFG;
	s->sbm_rxfilter  = s->sbm_base + R_MAC_ADFILTER_CFG;
	s->sbm_isr       = s->sbm_base + R_MAC_STATUS;
	s->sbm_imr       = s->sbm_base + R_MAC_INT_MASK;
	s->sbm_mdio      = s->sbm_base + R_MAC_MDIO;

	/*
	 * Initialize the DMA channels.  Right now, only one per MAC is used
	 * Note: Only do this _once_, as it allocates memory from the kernel!
	 */

	sbdma_initctx(&(s->sbm_txdma),s,0,DMA_TX,SBMAC_MAX_TXDESCR);
	sbdma_initctx(&(s->sbm_rxdma),s,0,DMA_RX,SBMAC_MAX_RXDESCR);

	/*
	 * initial state is OFF
	 */

	s->sbm_state = sbmac_state_off;

	return 0;
}


static void sbdma_uninitctx(struct sbmacdma *d)
{
	if (d->sbdma_dscrtable_unaligned) {
		kfree(d->sbdma_dscrtable_unaligned);
		d->sbdma_dscrtable_unaligned = d->sbdma_dscrtable = NULL;
	}

	if (d->sbdma_ctxtable) {
		kfree(d->sbdma_ctxtable);
		d->sbdma_ctxtable = NULL;
	}
}


static void sbmac_uninitctx(struct sbmac_softc *sc)
{
	sbdma_uninitctx(&(sc->sbm_txdma));
	sbdma_uninitctx(&(sc->sbm_rxdma));
}


/**********************************************************************
 *  SBMAC_CHANNEL_START(s)
 *
 *  Start packet processing on this MAC.
 *
 *  Input parameters:
 *  	   s - sbmac structure
 *
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbmac_channel_start(struct sbmac_softc *s)
{
	uint64_t reg;
	void __iomem *port;
	uint64_t cfg,fifo,framecfg;
	int idx, th_value;

	/*
	 * Don't do this if running
	 */

	if (s->sbm_state == sbmac_state_on)
		return;

	/*
	 * Bring the controller out of reset, but leave it off.
	 */

	__raw_writeq(0, s->sbm_macenable);

	/*
	 * Ignore all received packets
	 */

	__raw_writeq(0, s->sbm_rxfilter);

	/*
	 * Calculate values for various control registers.
	 */

	cfg = M_MAC_RETRY_EN |
		M_MAC_TX_HOLD_SOP_EN |
		V_MAC_TX_PAUSE_CNT_16K |
		M_MAC_AP_STAT_EN |
		M_MAC_FAST_SYNC |
		M_MAC_SS_EN |
		0;

	/*
	 * Be sure that RD_THRSH+WR_THRSH <= 32 for pass1 pars
	 * and make sure that RD_THRSH + WR_THRSH <=128 for pass2 and above
	 * Use a larger RD_THRSH for gigabit
	 */
	if (soc_type == K_SYS_SOC_TYPE_BCM1250 && periph_rev < 2)
		th_value = 28;
	else
		th_value = 64;

	fifo = V_MAC_TX_WR_THRSH(4) |	/* Must be '4' or '8' */
		((s->sbm_speed == sbmac_speed_1000)
		 ? V_MAC_TX_RD_THRSH(th_value) : V_MAC_TX_RD_THRSH(4)) |
		V_MAC_TX_RL_THRSH(4) |
		V_MAC_RX_PL_THRSH(4) |
		V_MAC_RX_RD_THRSH(4) |	/* Must be '4' */
		V_MAC_RX_RL_THRSH(8) |
		0;

	framecfg = V_MAC_MIN_FRAMESZ_DEFAULT |
		V_MAC_MAX_FRAMESZ_DEFAULT |
		V_MAC_BACKOFF_SEL(1);

	/*
	 * Clear out the hash address map
	 */

	port = s->sbm_base + R_MAC_HASH_BASE;
	for (idx = 0; idx < MAC_HASH_COUNT; idx++) {
		__raw_writeq(0, port);
		port += sizeof(uint64_t);
	}

	/*
	 * Clear out the exact-match table
	 */

	port = s->sbm_base + R_MAC_ADDR_BASE;
	for (idx = 0; idx < MAC_ADDR_COUNT; idx++) {
		__raw_writeq(0, port);
		port += sizeof(uint64_t);
	}

	/*
	 * Clear out the DMA Channel mapping table registers
	 */

	port = s->sbm_base + R_MAC_CHUP0_BASE;
	for (idx = 0; idx < MAC_CHMAP_COUNT; idx++) {
		__raw_writeq(0, port);
		port += sizeof(uint64_t);
	}


	port = s->sbm_base + R_MAC_CHLO0_BASE;
	for (idx = 0; idx < MAC_CHMAP_COUNT; idx++) {
		__raw_writeq(0, port);
		port += sizeof(uint64_t);
	}

	/*
	 * Program the hardware address.  It goes into the hardware-address
	 * register as well as the first filter register.
	 */

	reg = sbmac_addr2reg(s->sbm_hwaddr);

	port = s->sbm_base + R_MAC_ADDR_BASE;
	__raw_writeq(reg, port);
	port = s->sbm_base + R_MAC_ETHERNET_ADDR;

#ifdef CONFIG_SB1_PASS_1_WORKAROUNDS
	/*
	 * Pass1 SOCs do not receive packets addressed to the
	 * destination address in the R_MAC_ETHERNET_ADDR register.
	 * Set the value to zero.
	 */
	__raw_writeq(0, port);
#else
	__raw_writeq(reg, port);
#endif

	/*
	 * Set the receive filter for no packets, and write values
	 * to the various config registers
	 */

	__raw_writeq(0, s->sbm_rxfilter);
	__raw_writeq(0, s->sbm_imr);
	__raw_writeq(framecfg, s->sbm_framecfg);
	__raw_writeq(fifo, s->sbm_fifocfg);
	__raw_writeq(cfg, s->sbm_maccfg);

	/*
	 * Initialize DMA channels (rings should be ok now)
	 */

	sbdma_channel_start(&(s->sbm_rxdma), DMA_RX);
	sbdma_channel_start(&(s->sbm_txdma), DMA_TX);

	/*
	 * Configure the speed, duplex, and flow control
	 */

	sbmac_set_speed(s,s->sbm_speed);
	sbmac_set_duplex(s,s->sbm_duplex,s->sbm_fc);

	/*
	 * Fill the receive ring
	 */

	sbdma_fillring(s, &(s->sbm_rxdma));

	/*
	 * Turn on the rest of the bits in the enable register
	 */

#if defined(CONFIG_SIBYTE_BCM1x55) || defined(CONFIG_SIBYTE_BCM1x80)
	__raw_writeq(M_MAC_RXDMA_EN0 |
		       M_MAC_TXDMA_EN0, s->sbm_macenable);
#elif defined(CONFIG_SIBYTE_SB1250) || defined(CONFIG_SIBYTE_BCM112X)
	__raw_writeq(M_MAC_RXDMA_EN0 |
		       M_MAC_TXDMA_EN0 |
		       M_MAC_RX_ENABLE |
		       M_MAC_TX_ENABLE, s->sbm_macenable);
#else
#error invalid SiByte MAC configuration
#endif

#ifdef CONFIG_SBMAC_COALESCE
	__raw_writeq(((M_MAC_INT_EOP_COUNT | M_MAC_INT_EOP_TIMER) << S_MAC_TX_CH0) |
		       ((M_MAC_INT_EOP_COUNT | M_MAC_INT_EOP_TIMER) << S_MAC_RX_CH0), s->sbm_imr);
#else
	__raw_writeq((M_MAC_INT_CHANNEL << S_MAC_TX_CH0) |
		       (M_MAC_INT_CHANNEL << S_MAC_RX_CH0), s->sbm_imr);
#endif

	/*
	 * Enable receiving unicasts and broadcasts
	 */

	__raw_writeq(M_MAC_UCAST_EN | M_MAC_BCAST_EN, s->sbm_rxfilter);

	/*
	 * we're running now.
	 */

	s->sbm_state = sbmac_state_on;

	/*
	 * Program multicast addresses
	 */

	sbmac_setmulti(s);

	/*
	 * If channel was in promiscuous mode before, turn that on
	 */

	if (s->sbm_devflags & IFF_PROMISC) {
		sbmac_promiscuous_mode(s,1);
	}

}


/**********************************************************************
 *  SBMAC_CHANNEL_STOP(s)
 *
 *  Stop packet processing on this MAC.
 *
 *  Input parameters:
 *  	   s - sbmac structure
 *
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbmac_channel_stop(struct sbmac_softc *s)
{
	/* don't do this if already stopped */

	if (s->sbm_state == sbmac_state_off)
		return;

	/* don't accept any packets, disable all interrupts */

	__raw_writeq(0, s->sbm_rxfilter);
	__raw_writeq(0, s->sbm_imr);

	/* Turn off ticker */

	/* XXX */

	/* turn off receiver and transmitter */

	__raw_writeq(0, s->sbm_macenable);

	/* We're stopped now. */

	s->sbm_state = sbmac_state_off;

	/*
	 * Stop DMA channels (rings should be ok now)
	 */

	sbdma_channel_stop(&(s->sbm_rxdma));
	sbdma_channel_stop(&(s->sbm_txdma));

	/* Empty the receive and transmit rings */

	sbdma_emptyring(&(s->sbm_rxdma));
	sbdma_emptyring(&(s->sbm_txdma));

}

/**********************************************************************
 *  SBMAC_SET_CHANNEL_STATE(state)
 *
 *  Set the channel's state ON or OFF
 *
 *  Input parameters:
 *  	   state - new state
 *
 *  Return value:
 *  	   old state
 ********************************************************************* */
static enum sbmac_state sbmac_set_channel_state(struct sbmac_softc *sc,
						enum sbmac_state state)
{
	enum sbmac_state oldstate = sc->sbm_state;

	/*
	 * If same as previous state, return
	 */

	if (state == oldstate) {
		return oldstate;
	}

	/*
	 * If new state is ON, turn channel on
	 */

	if (state == sbmac_state_on) {
		sbmac_channel_start(sc);
	}
	else {
		sbmac_channel_stop(sc);
	}

	/*
	 * Return previous state
	 */

	return oldstate;
}


/**********************************************************************
 *  SBMAC_PROMISCUOUS_MODE(sc,onoff)
 *
 *  Turn on or off promiscuous mode
 *
 *  Input parameters:
 *  	   sc - softc
 *      onoff - 1 to turn on, 0 to turn off
 *
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbmac_promiscuous_mode(struct sbmac_softc *sc,int onoff)
{
	uint64_t reg;

	if (sc->sbm_state != sbmac_state_on)
		return;

	if (onoff) {
		reg = __raw_readq(sc->sbm_rxfilter);
		reg |= M_MAC_ALLPKT_EN;
		__raw_writeq(reg, sc->sbm_rxfilter);
	}
	else {
		reg = __raw_readq(sc->sbm_rxfilter);
		reg &= ~M_MAC_ALLPKT_EN;
		__raw_writeq(reg, sc->sbm_rxfilter);
	}
}

/**********************************************************************
 *  SBMAC_SETIPHDR_OFFSET(sc,onoff)
 *
 *  Set the iphdr offset as 15 assuming ethernet encapsulation
 *
 *  Input parameters:
 *  	   sc - softc
 *
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbmac_set_iphdr_offset(struct sbmac_softc *sc)
{
	uint64_t reg;

	/* Hard code the off set to 15 for now */
	reg = __raw_readq(sc->sbm_rxfilter);
	reg &= ~M_MAC_IPHDR_OFFSET | V_MAC_IPHDR_OFFSET(15);
	__raw_writeq(reg, sc->sbm_rxfilter);

	/* BCM1250 pass1 didn't have hardware checksum.  Everything
	   later does.  */
	if (soc_type == K_SYS_SOC_TYPE_BCM1250 && periph_rev < 2) {
		sc->rx_hw_checksum = DISABLE;
	} else {
		sc->rx_hw_checksum = ENABLE;
	}
}


/**********************************************************************
 *  SBMAC_ADDR2REG(ptr)
 *
 *  Convert six bytes into the 64-bit register value that
 *  we typically write into the SBMAC's address/mcast registers
 *
 *  Input parameters:
 *  	   ptr - pointer to 6 bytes
 *
 *  Return value:
 *  	   register value
 ********************************************************************* */

static uint64_t sbmac_addr2reg(unsigned char *ptr)
{
	uint64_t reg = 0;

	ptr += 6;

	reg |= (uint64_t) *(--ptr);
	reg <<= 8;
	reg |= (uint64_t) *(--ptr);
	reg <<= 8;
	reg |= (uint64_t) *(--ptr);
	reg <<= 8;
	reg |= (uint64_t) *(--ptr);
	reg <<= 8;
	reg |= (uint64_t) *(--ptr);
	reg <<= 8;
	reg |= (uint64_t) *(--ptr);

	return reg;
}


/**********************************************************************
 *  SBMAC_SET_SPEED(s,speed)
 *
 *  Configure LAN speed for the specified MAC.
 *  Warning: must be called when MAC is off!
 *
 *  Input parameters:
 *  	   s - sbmac structure
 *  	   speed - speed to set MAC to (see enum sbmac_speed)
 *
 *  Return value:
 *  	   1 if successful
 *      0 indicates invalid parameters
 ********************************************************************* */

static int sbmac_set_speed(struct sbmac_softc *s, enum sbmac_speed speed)
{
	uint64_t cfg;
	uint64_t framecfg;

	/*
	 * Save new current values
	 */

	s->sbm_speed = speed;

	if (s->sbm_state == sbmac_state_on)
		return 0;	/* save for next restart */

	/*
	 * Read current register values
	 */

	cfg = __raw_readq(s->sbm_maccfg);
	framecfg = __raw_readq(s->sbm_framecfg);

	/*
	 * Mask out the stuff we want to change
	 */

	cfg &= ~(M_MAC_BURST_EN | M_MAC_SPEED_SEL);
	framecfg &= ~(M_MAC_IFG_RX | M_MAC_IFG_TX | M_MAC_IFG_THRSH |
		      M_MAC_SLOT_SIZE);

	/*
	 * Now add in the new bits
	 */

	switch (speed) {
	case sbmac_speed_10:
		framecfg |= V_MAC_IFG_RX_10 |
			V_MAC_IFG_TX_10 |
			K_MAC_IFG_THRSH_10 |
			V_MAC_SLOT_SIZE_10;
		cfg |= V_MAC_SPEED_SEL_10MBPS;
		break;

	case sbmac_speed_100:
		framecfg |= V_MAC_IFG_RX_100 |
			V_MAC_IFG_TX_100 |
			V_MAC_IFG_THRSH_100 |
			V_MAC_SLOT_SIZE_100;
		cfg |= V_MAC_SPEED_SEL_100MBPS ;
		break;

	case sbmac_speed_1000:
		framecfg |= V_MAC_IFG_RX_1000 |
			V_MAC_IFG_TX_1000 |
			V_MAC_IFG_THRSH_1000 |
			V_MAC_SLOT_SIZE_1000;
		cfg |= V_MAC_SPEED_SEL_1000MBPS | M_MAC_BURST_EN;
		break;

	default:
		return 0;
	}

	/*
	 * Send the bits back to the hardware
	 */

	__raw_writeq(framecfg, s->sbm_framecfg);
	__raw_writeq(cfg, s->sbm_maccfg);

	return 1;
}

/**********************************************************************
 *  SBMAC_SET_DUPLEX(s,duplex,fc)
 *
 *  Set Ethernet duplex and flow control options for this MAC
 *  Warning: must be called when MAC is off!
 *
 *  Input parameters:
 *  	   s - sbmac structure
 *  	   duplex - duplex setting (see enum sbmac_duplex)
 *  	   fc - flow control setting (see enum sbmac_fc)
 *
 *  Return value:
 *  	   1 if ok
 *  	   0 if an invalid parameter combination was specified
 ********************************************************************* */

static int sbmac_set_duplex(struct sbmac_softc *s, enum sbmac_duplex duplex,
			    enum sbmac_fc fc)
{
	uint64_t cfg;

	/*
	 * Save new current values
	 */

	s->sbm_duplex = duplex;
	s->sbm_fc = fc;

	if (s->sbm_state == sbmac_state_on)
		return 0;	/* save for next restart */

	/*
	 * Read current register values
	 */

	cfg = __raw_readq(s->sbm_maccfg);

	/*
	 * Mask off the stuff we're about to change
	 */

	cfg &= ~(M_MAC_FC_SEL | M_MAC_FC_CMD | M_MAC_HDX_EN);


	switch (duplex) {
	case sbmac_duplex_half:
		switch (fc) {
		case sbmac_fc_disabled:
			cfg |= M_MAC_HDX_EN | V_MAC_FC_CMD_DISABLED;
			break;

		case sbmac_fc_collision:
			cfg |= M_MAC_HDX_EN | V_MAC_FC_CMD_ENABLED;
			break;

		case sbmac_fc_carrier:
			cfg |= M_MAC_HDX_EN | V_MAC_FC_CMD_ENAB_FALSECARR;
			break;

		case sbmac_fc_frame:		/* not valid in half duplex */
		default:			/* invalid selection */
			return 0;
		}
		break;

	case sbmac_duplex_full:
		switch (fc) {
		case sbmac_fc_disabled:
			cfg |= V_MAC_FC_CMD_DISABLED;
			break;

		case sbmac_fc_frame:
			cfg |= V_MAC_FC_CMD_ENABLED;
			break;

		case sbmac_fc_collision:	/* not valid in full duplex */
		case sbmac_fc_carrier:		/* not valid in full duplex */
		default:
			return 0;
		}
		break;
	default:
		return 0;
	}

	/*
	 * Send the bits back to the hardware
	 */

	__raw_writeq(cfg, s->sbm_maccfg);

	return 1;
}




/**********************************************************************
 *  SBMAC_INTR()
 *
 *  Interrupt handler for MAC interrupts
 *
 *  Input parameters:
 *  	   MAC structure
 *
 *  Return value:
 *  	   nothing
 ********************************************************************* */
static irqreturn_t sbmac_intr(int irq,void *dev_instance)
{
	struct net_device *dev = (struct net_device *) dev_instance;
	struct sbmac_softc *sc = netdev_priv(dev);
	uint64_t isr;
	int handled = 0;

	/*
	 * Read the ISR (this clears the bits in the real
	 * register, except for counter addr)
	 */

	isr = __raw_readq(sc->sbm_isr) & ~M_MAC_COUNTER_ADDR;

	if (isr == 0)
		return IRQ_RETVAL(0);
	handled = 1;

	/*
	 * Transmits on channel 0
	 */

	if (isr & (M_MAC_INT_CHANNEL << S_MAC_TX_CH0))
		sbdma_tx_process(sc,&(sc->sbm_txdma), 0);

	if (isr & (M_MAC_INT_CHANNEL << S_MAC_RX_CH0)) {
		if (napi_schedule_prep(&sc->napi)) {
			__raw_writeq(0, sc->sbm_imr);
			__napi_schedule(&sc->napi);
			/* Depend on the exit from poll to reenable intr */
		}
		else {
			/* may leave some packets behind */
			sbdma_rx_process(sc,&(sc->sbm_rxdma),
					 SBMAC_MAX_RXDESCR * 2, 0);
		}
	}
	return IRQ_RETVAL(handled);
}

/**********************************************************************
 *  SBMAC_START_TX(skb,dev)
 *
 *  Start output on the specified interface.  Basically, we
 *  queue as many buffers as we can until the ring fills up, or
 *  we run off the end of the queue, whichever comes first.
 *
 *  Input parameters:
 *
 *
 *  Return value:
 *  	   nothing
 ********************************************************************* */
static int sbmac_start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct sbmac_softc *sc = netdev_priv(dev);
	unsigned long flags;

	/* lock eth irq */
	spin_lock_irqsave(&sc->sbm_lock, flags);

	/*
	 * Put the buffer on the transmit ring.  If we
	 * don't have room, stop the queue.
	 */

	if (sbdma_add_txbuffer(&(sc->sbm_txdma),skb)) {
		/* XXX save skb that we could not send */
		netif_stop_queue(dev);
		spin_unlock_irqrestore(&sc->sbm_lock, flags);

		return NETDEV_TX_BUSY;
	}

	spin_unlock_irqrestore(&sc->sbm_lock, flags);

	return NETDEV_TX_OK;
}

/**********************************************************************
 *  SBMAC_SETMULTI(sc)
 *
 *  Reprogram the multicast table into the hardware, given
 *  the list of multicasts associated with the interface
 *  structure.
 *
 *  Input parameters:
 *  	   sc - softc
 *
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbmac_setmulti(struct sbmac_softc *sc)
{
	uint64_t reg;
	void __iomem *port;
	int idx;
	struct netdev_hw_addr *ha;
	struct net_device *dev = sc->sbm_dev;

	/*
	 * Clear out entire multicast table.  We do this by nuking
	 * the entire hash table and all the direct matches except
	 * the first one, which is used for our station address
	 */

	for (idx = 1; idx < MAC_ADDR_COUNT; idx++) {
		port = sc->sbm_base + R_MAC_ADDR_BASE+(idx*sizeof(uint64_t));
		__raw_writeq(0, port);
	}

	for (idx = 0; idx < MAC_HASH_COUNT; idx++) {
		port = sc->sbm_base + R_MAC_HASH_BASE+(idx*sizeof(uint64_t));
		__raw_writeq(0, port);
	}

	/*
	 * Clear the filter to say we don't want any multicasts.
	 */

	reg = __raw_readq(sc->sbm_rxfilter);
	reg &= ~(M_MAC_MCAST_INV | M_MAC_MCAST_EN);
	__raw_writeq(reg, sc->sbm_rxfilter);

	if (dev->flags & IFF_ALLMULTI) {
		/*
		 * Enable ALL multicasts.  Do this by inverting the
		 * multicast enable bit.
		 */
		reg = __raw_readq(sc->sbm_rxfilter);
		reg |= (M_MAC_MCAST_INV | M_MAC_MCAST_EN);
		__raw_writeq(reg, sc->sbm_rxfilter);
		return;
	}


	/*
	 * Progam new multicast entries.  For now, only use the
	 * perfect filter.  In the future we'll need to use the
	 * hash filter if the perfect filter overflows
	 */

	/* XXX only using perfect filter for now, need to use hash
	 * XXX if the table overflows */

	idx = 1;		/* skip station address */
	netdev_for_each_mc_addr(ha, dev) {
		if (idx == MAC_ADDR_COUNT)
			break;
		reg = sbmac_addr2reg(ha->addr);
		port = sc->sbm_base + R_MAC_ADDR_BASE+(idx * sizeof(uint64_t));
		__raw_writeq(reg, port);
		idx++;
	}

	/*
	 * Enable the "accept multicast bits" if we programmed at least one
	 * multicast.
	 */

	if (idx > 1) {
		reg = __raw_readq(sc->sbm_rxfilter);
		reg |= M_MAC_MCAST_EN;
		__raw_writeq(reg, sc->sbm_rxfilter);
	}
}

static int sb1250_change_mtu(struct net_device *_dev, int new_mtu)
{
	if (new_mtu >  ENET_PACKET_SIZE)
		return -EINVAL;
	_dev->mtu = new_mtu;
	pr_info("changing the mtu to %d\n", new_mtu);
	return 0;
}

static const struct net_device_ops sbmac_netdev_ops = {
	.ndo_open		= sbmac_open,
	.ndo_stop		= sbmac_close,
	.ndo_start_xmit		= sbmac_start_tx,
	.ndo_set_rx_mode	= sbmac_set_rx_mode,
	.ndo_tx_timeout		= sbmac_tx_timeout,
	.ndo_do_ioctl		= sbmac_mii_ioctl,
	.ndo_change_mtu		= sb1250_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= sbmac_netpoll,
#endif
};

/**********************************************************************
 *  SBMAC_INIT(dev)
 *
 *  Attach routine - init hardware and hook ourselves into linux
 *
 *  Input parameters:
 *  	   dev - net_device structure
 *
 *  Return value:
 *  	   status
 ********************************************************************* */

static int sbmac_init(struct platform_device *pldev, long long base)
{
	struct net_device *dev = dev_get_drvdata(&pldev->dev);
	int idx = pldev->id;
	struct sbmac_softc *sc = netdev_priv(dev);
	unsigned char *eaddr;
	uint64_t ea_reg;
	int i;
	int err;

	sc->sbm_dev = dev;
	sc->sbe_idx = idx;

	eaddr = sc->sbm_hwaddr;

	/*
	 * Read the ethernet address.  The firmware left this programmed
	 * for us in the ethernet address register for each mac.
	 */

	ea_reg = __raw_readq(sc->sbm_base + R_MAC_ETHERNET_ADDR);
	__raw_writeq(0, sc->sbm_base + R_MAC_ETHERNET_ADDR);
	for (i = 0; i < 6; i++) {
		eaddr[i] = (uint8_t) (ea_reg & 0xFF);
		ea_reg >>= 8;
	}

	for (i = 0; i < 6; i++) {
		dev->dev_addr[i] = eaddr[i];
	}

	/*
	 * Initialize context (get pointers to registers and stuff), then
	 * allocate the memory for the descriptor tables.
	 */

	sbmac_initctx(sc);

	/*
	 * Set up Linux device callins
	 */

	spin_lock_init(&(sc->sbm_lock));

	dev->netdev_ops = &sbmac_netdev_ops;
	dev->watchdog_timeo = TX_TIMEOUT;

	netif_napi_add(dev, &sc->napi, sbmac_poll, 16);

	dev->irq		= UNIT_INT(idx);

	/* This is needed for PASS2 for Rx H/W checksum feature */
	sbmac_set_iphdr_offset(sc);

	sc->mii_bus = mdiobus_alloc();
	if (sc->mii_bus == NULL) {
		err = -ENOMEM;
		goto uninit_ctx;
	}

	sc->mii_bus->name = sbmac_mdio_string;
	snprintf(sc->mii_bus->id, MII_BUS_ID_SIZE, "%x", idx);
	sc->mii_bus->priv = sc;
	sc->mii_bus->read = sbmac_mii_read;
	sc->mii_bus->write = sbmac_mii_write;
	sc->mii_bus->irq = sc->phy_irq;
	for (i = 0; i < PHY_MAX_ADDR; ++i)
		sc->mii_bus->irq[i] = SBMAC_PHY_INT;

	sc->mii_bus->parent = &pldev->dev;
	/*
	 * Probe PHY address
	 */
	err = mdiobus_register(sc->mii_bus);
	if (err) {
		printk(KERN_ERR "%s: unable to register MDIO bus\n",
		       dev->name);
		goto free_mdio;
	}
	dev_set_drvdata(&pldev->dev, sc->mii_bus);

	err = register_netdev(dev);
	if (err) {
		printk(KERN_ERR "%s.%d: unable to register netdev\n",
		       sbmac_string, idx);
		goto unreg_mdio;
	}

	pr_info("%s.%d: registered as %s\n", sbmac_string, idx, dev->name);

	if (sc->rx_hw_checksum == ENABLE)
		pr_info("%s: enabling TCP rcv checksum\n", dev->name);

	/*
	 * Display Ethernet address (this is called during the config
	 * process so we need to finish off the config message that
	 * was being displayed)
	 */
	pr_info("%s: SiByte Ethernet at 0x%08Lx, address: %pM\n",
	       dev->name, base, eaddr);

	return 0;
unreg_mdio:
	mdiobus_unregister(sc->mii_bus);
	dev_set_drvdata(&pldev->dev, NULL);
free_mdio:
	mdiobus_free(sc->mii_bus);
uninit_ctx:
	sbmac_uninitctx(sc);
	return err;
}


static int sbmac_open(struct net_device *dev)
{
	struct sbmac_softc *sc = netdev_priv(dev);
	int err;

	if (debug > 1)
		pr_debug("%s: sbmac_open() irq %d.\n", dev->name, dev->irq);

	/*
	 * map/route interrupt (clear status first, in case something
	 * weird is pending; we haven't initialized the mac registers
	 * yet)
	 */

	__raw_readq(sc->sbm_isr);
	err = request_irq(dev->irq, sbmac_intr, IRQF_SHARED, dev->name, dev);
	if (err) {
		printk(KERN_ERR "%s: unable to get IRQ %d\n", dev->name,
		       dev->irq);
		goto out_err;
	}

	sc->sbm_speed = sbmac_speed_none;
	sc->sbm_duplex = sbmac_duplex_none;
	sc->sbm_fc = sbmac_fc_none;
	sc->sbm_pause = -1;
	sc->sbm_link = 0;

	/*
	 * Attach to the PHY
	 */
	err = sbmac_mii_probe(dev);
	if (err)
		goto out_unregister;

	/*
	 * Turn on the channel
	 */

	sbmac_set_channel_state(sc,sbmac_state_on);

	netif_start_queue(dev);

	sbmac_set_rx_mode(dev);

	phy_start(sc->phy_dev);

	napi_enable(&sc->napi);

	return 0;

out_unregister:
	free_irq(dev->irq, dev);
out_err:
	return err;
}

static int sbmac_mii_probe(struct net_device *dev)
{
	struct sbmac_softc *sc = netdev_priv(dev);
	struct phy_device *phy_dev;
	int i;

	for (i = 0; i < PHY_MAX_ADDR; i++) {
		phy_dev = sc->mii_bus->phy_map[i];
		if (phy_dev)
			break;
	}
	if (!phy_dev) {
		printk(KERN_ERR "%s: no PHY found\n", dev->name);
		return -ENXIO;
	}

	phy_dev = phy_connect(dev, dev_name(&phy_dev->dev), &sbmac_mii_poll, 0,
			      PHY_INTERFACE_MODE_GMII);
	if (IS_ERR(phy_dev)) {
		printk(KERN_ERR "%s: could not attach to PHY\n", dev->name);
		return PTR_ERR(phy_dev);
	}

	/* Remove any features not supported by the controller */
	phy_dev->supported &= SUPPORTED_10baseT_Half |
			      SUPPORTED_10baseT_Full |
			      SUPPORTED_100baseT_Half |
			      SUPPORTED_100baseT_Full |
			      SUPPORTED_1000baseT_Half |
			      SUPPORTED_1000baseT_Full |
			      SUPPORTED_Autoneg |
			      SUPPORTED_MII |
			      SUPPORTED_Pause |
			      SUPPORTED_Asym_Pause;
	phy_dev->advertising = phy_dev->supported;

	pr_info("%s: attached PHY driver [%s] (mii_bus:phy_addr=%s, irq=%d)\n",
		dev->name, phy_dev->drv->name,
		dev_name(&phy_dev->dev), phy_dev->irq);

	sc->phy_dev = phy_dev;

	return 0;
}


static void sbmac_mii_poll(struct net_device *dev)
{
	struct sbmac_softc *sc = netdev_priv(dev);
	struct phy_device *phy_dev = sc->phy_dev;
	unsigned long flags;
	enum sbmac_fc fc;
	int link_chg, speed_chg, duplex_chg, pause_chg, fc_chg;

	link_chg = (sc->sbm_link != phy_dev->link);
	speed_chg = (sc->sbm_speed != phy_dev->speed);
	duplex_chg = (sc->sbm_duplex != phy_dev->duplex);
	pause_chg = (sc->sbm_pause != phy_dev->pause);

	if (!link_chg && !speed_chg && !duplex_chg && !pause_chg)
		return;					/* Hmmm... */

	if (!phy_dev->link) {
		if (link_chg) {
			sc->sbm_link = phy_dev->link;
			sc->sbm_speed = sbmac_speed_none;
			sc->sbm_duplex = sbmac_duplex_none;
			sc->sbm_fc = sbmac_fc_disabled;
			sc->sbm_pause = -1;
			pr_info("%s: link unavailable\n", dev->name);
		}
		return;
	}

	if (phy_dev->duplex == DUPLEX_FULL) {
		if (phy_dev->pause)
			fc = sbmac_fc_frame;
		else
			fc = sbmac_fc_disabled;
	} else
		fc = sbmac_fc_collision;
	fc_chg = (sc->sbm_fc != fc);

	pr_info("%s: link available: %dbase-%cD\n", dev->name, phy_dev->speed,
		phy_dev->duplex == DUPLEX_FULL ? 'F' : 'H');

	spin_lock_irqsave(&sc->sbm_lock, flags);

	sc->sbm_speed = phy_dev->speed;
	sc->sbm_duplex = phy_dev->duplex;
	sc->sbm_fc = fc;
	sc->sbm_pause = phy_dev->pause;
	sc->sbm_link = phy_dev->link;

	if ((speed_chg || duplex_chg || fc_chg) &&
	    sc->sbm_state != sbmac_state_off) {
		/*
		 * something changed, restart the channel
		 */
		if (debug > 1)
			pr_debug("%s: restarting channel "
				 "because PHY state changed\n", dev->name);
		sbmac_channel_stop(sc);
		sbmac_channel_start(sc);
	}

	spin_unlock_irqrestore(&sc->sbm_lock, flags);
}


static void sbmac_tx_timeout (struct net_device *dev)
{
	struct sbmac_softc *sc = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&sc->sbm_lock, flags);


	dev->trans_start = jiffies; /* prevent tx timeout */
	dev->stats.tx_errors++;

	spin_unlock_irqrestore(&sc->sbm_lock, flags);

	printk (KERN_WARNING "%s: Transmit timed out\n",dev->name);
}




static void sbmac_set_rx_mode(struct net_device *dev)
{
	unsigned long flags;
	struct sbmac_softc *sc = netdev_priv(dev);

	spin_lock_irqsave(&sc->sbm_lock, flags);
	if ((dev->flags ^ sc->sbm_devflags) & IFF_PROMISC) {
		/*
		 * Promiscuous changed.
		 */

		if (dev->flags & IFF_PROMISC) {
			sbmac_promiscuous_mode(sc,1);
		}
		else {
			sbmac_promiscuous_mode(sc,0);
		}
	}
	spin_unlock_irqrestore(&sc->sbm_lock, flags);

	/*
	 * Program the multicasts.  Do this every time.
	 */

	sbmac_setmulti(sc);

}

static int sbmac_mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct sbmac_softc *sc = netdev_priv(dev);

	if (!netif_running(dev) || !sc->phy_dev)
		return -EINVAL;

	return phy_mii_ioctl(sc->phy_dev, rq, cmd);
}

static int sbmac_close(struct net_device *dev)
{
	struct sbmac_softc *sc = netdev_priv(dev);

	napi_disable(&sc->napi);

	phy_stop(sc->phy_dev);

	sbmac_set_channel_state(sc, sbmac_state_off);

	netif_stop_queue(dev);

	if (debug > 1)
		pr_debug("%s: Shutting down ethercard\n", dev->name);

	phy_disconnect(sc->phy_dev);
	sc->phy_dev = NULL;
	free_irq(dev->irq, dev);

	sbdma_emptyring(&(sc->sbm_txdma));
	sbdma_emptyring(&(sc->sbm_rxdma));

	return 0;
}

static int sbmac_poll(struct napi_struct *napi, int budget)
{
	struct sbmac_softc *sc = container_of(napi, struct sbmac_softc, napi);
	int work_done;

	work_done = sbdma_rx_process(sc, &(sc->sbm_rxdma), budget, 1);
	sbdma_tx_process(sc, &(sc->sbm_txdma), 1);

	if (work_done < budget) {
		napi_complete(napi);

#ifdef CONFIG_SBMAC_COALESCE
		__raw_writeq(((M_MAC_INT_EOP_COUNT | M_MAC_INT_EOP_TIMER) << S_MAC_TX_CH0) |
			     ((M_MAC_INT_EOP_COUNT | M_MAC_INT_EOP_TIMER) << S_MAC_RX_CH0),
			     sc->sbm_imr);
#else
		__raw_writeq((M_MAC_INT_CHANNEL << S_MAC_TX_CH0) |
			     (M_MAC_INT_CHANNEL << S_MAC_RX_CH0), sc->sbm_imr);
#endif
	}

	return work_done;
}


static int __devinit sbmac_probe(struct platform_device *pldev)
{
	struct net_device *dev;
	struct sbmac_softc *sc;
	void __iomem *sbm_base;
	struct resource *res;
	u64 sbmac_orig_hwaddr;
	int err;

	res = platform_get_resource(pldev, IORESOURCE_MEM, 0);
	BUG_ON(!res);
	sbm_base = ioremap_nocache(res->start, resource_size(res));
	if (!sbm_base) {
		printk(KERN_ERR "%s: unable to map device registers\n",
		       dev_name(&pldev->dev));
		err = -ENOMEM;
		goto out_out;
	}

	/*
	 * The R_MAC_ETHERNET_ADDR register will be set to some nonzero
	 * value for us by the firmware if we're going to use this MAC.
	 * If we find a zero, skip this MAC.
	 */
	sbmac_orig_hwaddr = __raw_readq(sbm_base + R_MAC_ETHERNET_ADDR);
	pr_debug("%s: %sconfiguring MAC at 0x%08Lx\n", dev_name(&pldev->dev),
		 sbmac_orig_hwaddr ? "" : "not ", (long long)res->start);
	if (sbmac_orig_hwaddr == 0) {
		err = 0;
		goto out_unmap;
	}

	/*
	 * Okay, cool.  Initialize this MAC.
	 */
	dev = alloc_etherdev(sizeof(struct sbmac_softc));
	if (!dev) {
		printk(KERN_ERR "%s: unable to allocate etherdev\n",
		       dev_name(&pldev->dev));
		err = -ENOMEM;
		goto out_unmap;
	}

	dev_set_drvdata(&pldev->dev, dev);
	SET_NETDEV_DEV(dev, &pldev->dev);

	sc = netdev_priv(dev);
	sc->sbm_base = sbm_base;

	err = sbmac_init(pldev, res->start);
	if (err)
		goto out_kfree;

	return 0;

out_kfree:
	free_netdev(dev);
	__raw_writeq(sbmac_orig_hwaddr, sbm_base + R_MAC_ETHERNET_ADDR);

out_unmap:
	iounmap(sbm_base);

out_out:
	return err;
}

static int __exit sbmac_remove(struct platform_device *pldev)
{
	struct net_device *dev = dev_get_drvdata(&pldev->dev);
	struct sbmac_softc *sc = netdev_priv(dev);

	unregister_netdev(dev);
	sbmac_uninitctx(sc);
	mdiobus_unregister(sc->mii_bus);
	mdiobus_free(sc->mii_bus);
	iounmap(sc->sbm_base);
	free_netdev(dev);

	return 0;
}

static struct platform_driver sbmac_driver = {
	.probe = sbmac_probe,
	.remove = __exit_p(sbmac_remove),
	.driver = {
		.name = sbmac_string,
		.owner  = THIS_MODULE,
	},
};

static int __init sbmac_init_module(void)
{
	return platform_driver_register(&sbmac_driver);
}

static void __exit sbmac_cleanup_module(void)
{
	platform_driver_unregister(&sbmac_driver);
}

module_init(sbmac_init_module);
module_exit(sbmac_cleanup_module);
