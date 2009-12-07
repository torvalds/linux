/*
 * e100net.c: A network driver for the ETRAX 100LX network controller.
 *
 * Copyright (c) 1998-2002 Axis Communications AB.
 *
 * The outline of this driver comes from skeleton.c.
 *
 */


#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/bitops.h>

#include <linux/if.h>
#include <linux/mii.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>

#include <arch/svinto.h>/* DMA and register descriptions */
#include <asm/io.h>         /* CRIS_LED_* I/O functions */
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/system.h>
#include <asm/ethernet.h>
#include <asm/cache.h>
#include <arch/io_interface_mux.h>

//#define ETHDEBUG
#define D(x)

/*
 * The name of the card. Is used for messages and in the requests for
 * io regions, irqs and dma channels
 */

static const char* cardname = "ETRAX 100LX built-in ethernet controller";

/* A default ethernet address. Highlevel SW will set the real one later */

static struct sockaddr default_mac = {
	0,
	{ 0x00, 0x40, 0x8C, 0xCD, 0x00, 0x00 }
};

/* Information that need to be kept for each board. */
struct net_local {
	struct net_device_stats stats;
	struct mii_if_info mii_if;

	/* Tx control lock.  This protects the transmit buffer ring
	 * state along with the "tx full" state of the driver.  This
	 * means all netif_queue flow control actions are protected
	 * by this lock as well.
	 */
	spinlock_t lock;

	spinlock_t led_lock; /* Protect LED state */
	spinlock_t transceiver_lock; /* Protect transceiver state. */
};

typedef struct etrax_eth_descr
{
	etrax_dma_descr descr;
	struct sk_buff* skb;
} etrax_eth_descr;

/* Some transceivers requires special handling */
struct transceiver_ops
{
	unsigned int oui;
	void (*check_speed)(struct net_device* dev);
	void (*check_duplex)(struct net_device* dev);
};

/* Duplex settings */
enum duplex
{
	half,
	full,
	autoneg
};

/* Dma descriptors etc. */

#define MAX_MEDIA_DATA_SIZE 1522

#define MIN_PACKET_LEN      46
#define ETHER_HEAD_LEN      14

/*
** MDIO constants.
*/
#define MDIO_START                          0x1
#define MDIO_READ                           0x2
#define MDIO_WRITE                          0x1
#define MDIO_PREAMBLE              0xfffffffful

/* Broadcom specific */
#define MDIO_AUX_CTRL_STATUS_REG           0x18
#define MDIO_BC_FULL_DUPLEX_IND             0x1
#define MDIO_BC_SPEED                       0x2

/* TDK specific */
#define MDIO_TDK_DIAGNOSTIC_REG              18
#define MDIO_TDK_DIAGNOSTIC_RATE          0x400
#define MDIO_TDK_DIAGNOSTIC_DPLX          0x800

/*Intel LXT972A specific*/
#define MDIO_INT_STATUS_REG_2			0x0011
#define MDIO_INT_FULL_DUPLEX_IND       (1 << 9)
#define MDIO_INT_SPEED                (1 << 14)

/* Network flash constants */
#define NET_FLASH_TIME                  (HZ/50) /* 20 ms */
#define NET_FLASH_PAUSE                (HZ/100) /* 10 ms */
#define NET_LINK_UP_CHECK_INTERVAL       (2*HZ) /* 2 s   */
#define NET_DUPLEX_CHECK_INTERVAL        (2*HZ) /* 2 s   */

#define NO_NETWORK_ACTIVITY 0
#define NETWORK_ACTIVITY    1

#define NBR_OF_RX_DESC     32
#define NBR_OF_TX_DESC     16

/* Large packets are sent directly to upper layers while small packets are */
/* copied (to reduce memory waste). The following constant decides the breakpoint */
#define RX_COPYBREAK 256

/* Due to a chip bug we need to flush the cache when descriptors are returned */
/* to the DMA. To decrease performance impact we return descriptors in chunks. */
/* The following constant determines the number of descriptors to return. */
#define RX_QUEUE_THRESHOLD  NBR_OF_RX_DESC/2

#define GET_BIT(bit,val)   (((val) >> (bit)) & 0x01)

/* Define some macros to access ETRAX 100 registers */
#define SETF(var, reg, field, val) var = (var & ~IO_MASK_(reg##_, field##_)) | \
					  IO_FIELD_(reg##_, field##_, val)
#define SETS(var, reg, field, val) var = (var & ~IO_MASK_(reg##_, field##_)) | \
					  IO_STATE_(reg##_, field##_, _##val)

static etrax_eth_descr *myNextRxDesc;  /* Points to the next descriptor to
                                          to be processed */
static etrax_eth_descr *myLastRxDesc;  /* The last processed descriptor */

static etrax_eth_descr RxDescList[NBR_OF_RX_DESC] __attribute__ ((aligned(32)));

static etrax_eth_descr* myFirstTxDesc; /* First packet not yet sent */
static etrax_eth_descr* myLastTxDesc;  /* End of send queue */
static etrax_eth_descr* myNextTxDesc;  /* Next descriptor to use */
static etrax_eth_descr TxDescList[NBR_OF_TX_DESC] __attribute__ ((aligned(32)));

static unsigned int network_rec_config_shadow = 0;

static unsigned int network_tr_ctrl_shadow = 0;

/* Network speed indication. */
static DEFINE_TIMER(speed_timer, NULL, 0, 0);
static DEFINE_TIMER(clear_led_timer, NULL, 0, 0);
static int current_speed; /* Speed read from transceiver */
static int current_speed_selection; /* Speed selected by user */
static unsigned long led_next_time;
static int led_active;
static int rx_queue_len;

/* Duplex */
static DEFINE_TIMER(duplex_timer, NULL, 0, 0);
static int full_duplex;
static enum duplex current_duplex;

/* Index to functions, as function prototypes. */

static int etrax_ethernet_init(void);

static int e100_open(struct net_device *dev);
static int e100_set_mac_address(struct net_device *dev, void *addr);
static int e100_send_packet(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t e100rxtx_interrupt(int irq, void *dev_id);
static irqreturn_t e100nw_interrupt(int irq, void *dev_id);
static void e100_rx(struct net_device *dev);
static int e100_close(struct net_device *dev);
static int e100_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
static int e100_set_config(struct net_device* dev, struct ifmap* map);
static void e100_tx_timeout(struct net_device *dev);
static struct net_device_stats *e100_get_stats(struct net_device *dev);
static void set_multicast_list(struct net_device *dev);
static void e100_hardware_send_packet(struct net_local* np, char *buf, int length);
static void update_rx_stats(struct net_device_stats *);
static void update_tx_stats(struct net_device_stats *);
static int e100_probe_transceiver(struct net_device* dev);

static void e100_check_speed(unsigned long priv);
static void e100_set_speed(struct net_device* dev, unsigned long speed);
static void e100_check_duplex(unsigned long priv);
static void e100_set_duplex(struct net_device* dev, enum duplex);
static void e100_negotiate(struct net_device* dev);

static int e100_get_mdio_reg(struct net_device *dev, int phy_id, int location);
static void e100_set_mdio_reg(struct net_device *dev, int phy_id, int location, int value);

static void e100_send_mdio_cmd(unsigned short cmd, int write_cmd);
static void e100_send_mdio_bit(unsigned char bit);
static unsigned char e100_receive_mdio_bit(void);
static void e100_reset_transceiver(struct net_device* net);

static void e100_clear_network_leds(unsigned long dummy);
static void e100_set_network_leds(int active);

static const struct ethtool_ops e100_ethtool_ops;
#if defined(CONFIG_ETRAX_NO_PHY)
static void dummy_check_speed(struct net_device* dev);
static void dummy_check_duplex(struct net_device* dev);
#else
static void broadcom_check_speed(struct net_device* dev);
static void broadcom_check_duplex(struct net_device* dev);
static void tdk_check_speed(struct net_device* dev);
static void tdk_check_duplex(struct net_device* dev);
static void intel_check_speed(struct net_device* dev);
static void intel_check_duplex(struct net_device* dev);
static void generic_check_speed(struct net_device* dev);
static void generic_check_duplex(struct net_device* dev);
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
static void e100_netpoll(struct net_device* dev);
#endif

static int autoneg_normal = 1;

struct transceiver_ops transceivers[] =
{
#if defined(CONFIG_ETRAX_NO_PHY)
	{0x0000, dummy_check_speed, dummy_check_duplex}        /* Dummy */
#else
	{0x1018, broadcom_check_speed, broadcom_check_duplex},  /* Broadcom */
	{0xC039, tdk_check_speed, tdk_check_duplex},            /* TDK 2120 */
	{0x039C, tdk_check_speed, tdk_check_duplex},            /* TDK 2120C */
        {0x04de, intel_check_speed, intel_check_duplex},     	/* Intel LXT972A*/
	{0x0000, generic_check_speed, generic_check_duplex}     /* Generic, must be last */
#endif
};

struct transceiver_ops* transceiver = &transceivers[0];

static const struct net_device_ops e100_netdev_ops = {
	.ndo_open		= e100_open,
	.ndo_stop		= e100_close,
	.ndo_start_xmit		= e100_send_packet,
	.ndo_tx_timeout		= e100_tx_timeout,
	.ndo_get_stats		= e100_get_stats,
	.ndo_set_multicast_list	= set_multicast_list,
	.ndo_do_ioctl		= e100_ioctl,
	.ndo_set_mac_address	= e100_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_config		= e100_set_config,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= e100_netpoll,
#endif
};

#define tx_done(dev) (*R_DMA_CH0_CMD == 0)

/*
 * Check for a network adaptor of this type, and return '0' if one exists.
 * If dev->base_addr == 0, probe all likely locations.
 * If dev->base_addr == 1, always return failure.
 * If dev->base_addr == 2, allocate space for the device and return success
 * (detachable devices only).
 */

static int __init
etrax_ethernet_init(void)
{
	struct net_device *dev;
        struct net_local* np;
	int i, err;

	printk(KERN_INFO
	       "ETRAX 100LX 10/100MBit ethernet v2.0 (c) 1998-2007 Axis Communications AB\n");

	if (cris_request_io_interface(if_eth, cardname)) {
		printk(KERN_CRIT "etrax_ethernet_init failed to get IO interface\n");
		return -EBUSY;
	}

	dev = alloc_etherdev(sizeof(struct net_local));
	if (!dev)
		return -ENOMEM;

	np = netdev_priv(dev);

	/* we do our own locking */
	dev->features |= NETIF_F_LLTX;

	dev->base_addr = (unsigned int)R_NETWORK_SA_0; /* just to have something to show */

	/* now setup our etrax specific stuff */

	dev->irq = NETWORK_DMA_RX_IRQ_NBR; /* we really use DMATX as well... */
	dev->dma = NETWORK_RX_DMA_NBR;

	/* fill in our handlers so the network layer can talk to us in the future */

	dev->ethtool_ops	= &e100_ethtool_ops;
	dev->netdev_ops		= &e100_netdev_ops;

	spin_lock_init(&np->lock);
	spin_lock_init(&np->led_lock);
	spin_lock_init(&np->transceiver_lock);

	/* Initialise the list of Etrax DMA-descriptors */

	/* Initialise receive descriptors */

	for (i = 0; i < NBR_OF_RX_DESC; i++) {
		/* Allocate two extra cachelines to make sure that buffer used
		 * by DMA does not share cacheline with any other data (to
		 * avoid cache bug)
		 */
		RxDescList[i].skb = dev_alloc_skb(MAX_MEDIA_DATA_SIZE + 2 * L1_CACHE_BYTES);
		if (!RxDescList[i].skb)
			return -ENOMEM;
		RxDescList[i].descr.ctrl   = 0;
		RxDescList[i].descr.sw_len = MAX_MEDIA_DATA_SIZE;
		RxDescList[i].descr.next   = virt_to_phys(&RxDescList[i + 1]);
		RxDescList[i].descr.buf    = L1_CACHE_ALIGN(virt_to_phys(RxDescList[i].skb->data));
		RxDescList[i].descr.status = 0;
		RxDescList[i].descr.hw_len = 0;
		prepare_rx_descriptor(&RxDescList[i].descr);
	}

	RxDescList[NBR_OF_RX_DESC - 1].descr.ctrl   = d_eol;
	RxDescList[NBR_OF_RX_DESC - 1].descr.next   = virt_to_phys(&RxDescList[0]);
	rx_queue_len = 0;

	/* Initialize transmit descriptors */
	for (i = 0; i < NBR_OF_TX_DESC; i++) {
		TxDescList[i].descr.ctrl   = 0;
		TxDescList[i].descr.sw_len = 0;
		TxDescList[i].descr.next   = virt_to_phys(&TxDescList[i + 1].descr);
		TxDescList[i].descr.buf    = 0;
		TxDescList[i].descr.status = 0;
		TxDescList[i].descr.hw_len = 0;
		TxDescList[i].skb = 0;
	}

	TxDescList[NBR_OF_TX_DESC - 1].descr.ctrl   = d_eol;
	TxDescList[NBR_OF_TX_DESC - 1].descr.next   = virt_to_phys(&TxDescList[0].descr);

	/* Initialise initial pointers */

	myNextRxDesc  = &RxDescList[0];
	myLastRxDesc  = &RxDescList[NBR_OF_RX_DESC - 1];
	myFirstTxDesc = &TxDescList[0];
	myNextTxDesc  = &TxDescList[0];
	myLastTxDesc  = &TxDescList[NBR_OF_TX_DESC - 1];

	/* Register device */
	err = register_netdev(dev);
	if (err) {
		free_netdev(dev);
		return err;
	}

	/* set the default MAC address */

	e100_set_mac_address(dev, &default_mac);

	/* Initialize speed indicator stuff. */

	current_speed = 10;
	current_speed_selection = 0; /* Auto */
	speed_timer.expires = jiffies + NET_LINK_UP_CHECK_INTERVAL;
	speed_timer.data = (unsigned long)dev;
	speed_timer.function = e100_check_speed;

	clear_led_timer.function = e100_clear_network_leds;
	clear_led_timer.data = (unsigned long)dev;

	full_duplex = 0;
	current_duplex = autoneg;
	duplex_timer.expires = jiffies + NET_DUPLEX_CHECK_INTERVAL;
        duplex_timer.data = (unsigned long)dev;
	duplex_timer.function = e100_check_duplex;

        /* Initialize mii interface */
	np->mii_if.phy_id_mask = 0x1f;
	np->mii_if.reg_num_mask = 0x1f;
	np->mii_if.dev = dev;
	np->mii_if.mdio_read = e100_get_mdio_reg;
	np->mii_if.mdio_write = e100_set_mdio_reg;

	/* Initialize group address registers to make sure that no */
	/* unwanted addresses are matched */
	*R_NETWORK_GA_0 = 0x00000000;
	*R_NETWORK_GA_1 = 0x00000000;

	/* Initialize next time the led can flash */
	led_next_time = jiffies;
	return 0;
}

/* set MAC address of the interface. called from the core after a
 * SIOCSIFADDR ioctl, and from the bootup above.
 */

static int
e100_set_mac_address(struct net_device *dev, void *p)
{
	struct net_local *np = netdev_priv(dev);
	struct sockaddr *addr = p;

	spin_lock(&np->lock); /* preemption protection */

	/* remember it */

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	/* Write it to the hardware.
	 * Note the way the address is wrapped:
	 * *R_NETWORK_SA_0 = a0_0 | (a0_1 << 8) | (a0_2 << 16) | (a0_3 << 24);
	 * *R_NETWORK_SA_1 = a0_4 | (a0_5 << 8);
	 */

	*R_NETWORK_SA_0 = dev->dev_addr[0] | (dev->dev_addr[1] << 8) |
		(dev->dev_addr[2] << 16) | (dev->dev_addr[3] << 24);
	*R_NETWORK_SA_1 = dev->dev_addr[4] | (dev->dev_addr[5] << 8);
	*R_NETWORK_SA_2 = 0;

	/* show it in the log as well */

	printk(KERN_INFO "%s: changed MAC to %pM\n", dev->name, dev->dev_addr);

	spin_unlock(&np->lock);

	return 0;
}

/*
 * Open/initialize the board. This is called (in the current kernel)
 * sometime after booting when the 'ifconfig' program is run.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is non-reboot way to recover if something goes wrong.
 */

static int
e100_open(struct net_device *dev)
{
	unsigned long flags;

	/* enable the MDIO output pin */

	*R_NETWORK_MGM_CTRL = IO_STATE(R_NETWORK_MGM_CTRL, mdoe, enable);

	*R_IRQ_MASK0_CLR =
		IO_STATE(R_IRQ_MASK0_CLR, overrun, clr) |
		IO_STATE(R_IRQ_MASK0_CLR, underrun, clr) |
		IO_STATE(R_IRQ_MASK0_CLR, excessive_col, clr);

	/* clear dma0 and 1 eop and descr irq masks */
	*R_IRQ_MASK2_CLR =
		IO_STATE(R_IRQ_MASK2_CLR, dma0_descr, clr) |
		IO_STATE(R_IRQ_MASK2_CLR, dma0_eop, clr) |
		IO_STATE(R_IRQ_MASK2_CLR, dma1_descr, clr) |
		IO_STATE(R_IRQ_MASK2_CLR, dma1_eop, clr);

	/* Reset and wait for the DMA channels */

	RESET_DMA(NETWORK_TX_DMA_NBR);
	RESET_DMA(NETWORK_RX_DMA_NBR);
	WAIT_DMA(NETWORK_TX_DMA_NBR);
	WAIT_DMA(NETWORK_RX_DMA_NBR);

	/* Initialise the etrax network controller */

	/* allocate the irq corresponding to the receiving DMA */

	if (request_irq(NETWORK_DMA_RX_IRQ_NBR, e100rxtx_interrupt,
			IRQF_SAMPLE_RANDOM, cardname, (void *)dev)) {
		goto grace_exit0;
	}

	/* allocate the irq corresponding to the transmitting DMA */

	if (request_irq(NETWORK_DMA_TX_IRQ_NBR, e100rxtx_interrupt, 0,
			cardname, (void *)dev)) {
		goto grace_exit1;
	}

	/* allocate the irq corresponding to the network errors etc */

	if (request_irq(NETWORK_STATUS_IRQ_NBR, e100nw_interrupt, 0,
			cardname, (void *)dev)) {
		goto grace_exit2;
	}

	/*
	 * Always allocate the DMA channels after the IRQ,
	 * and clean up on failure.
	 */

	if (cris_request_dma(NETWORK_TX_DMA_NBR,
	                     cardname,
	                     DMA_VERBOSE_ON_ERROR,
	                     dma_eth)) {
		goto grace_exit3;
        }

	if (cris_request_dma(NETWORK_RX_DMA_NBR,
	                     cardname,
	                     DMA_VERBOSE_ON_ERROR,
	                     dma_eth)) {
		goto grace_exit4;
        }

	/* give the HW an idea of what MAC address we want */

	*R_NETWORK_SA_0 = dev->dev_addr[0] | (dev->dev_addr[1] << 8) |
		(dev->dev_addr[2] << 16) | (dev->dev_addr[3] << 24);
	*R_NETWORK_SA_1 = dev->dev_addr[4] | (dev->dev_addr[5] << 8);
	*R_NETWORK_SA_2 = 0;

#if 0
	/* use promiscuous mode for testing */
	*R_NETWORK_GA_0 = 0xffffffff;
	*R_NETWORK_GA_1 = 0xffffffff;

	*R_NETWORK_REC_CONFIG = 0xd; /* broadcast rec, individ. rec, ma0 enabled */
#else
	SETS(network_rec_config_shadow, R_NETWORK_REC_CONFIG, max_size, size1522);
	SETS(network_rec_config_shadow, R_NETWORK_REC_CONFIG, broadcast, receive);
	SETS(network_rec_config_shadow, R_NETWORK_REC_CONFIG, ma0, enable);
	SETF(network_rec_config_shadow, R_NETWORK_REC_CONFIG, duplex, full_duplex);
	*R_NETWORK_REC_CONFIG = network_rec_config_shadow;
#endif

	*R_NETWORK_GEN_CONFIG =
		IO_STATE(R_NETWORK_GEN_CONFIG, phy,    mii_clk) |
		IO_STATE(R_NETWORK_GEN_CONFIG, enable, on);

	SETS(network_tr_ctrl_shadow, R_NETWORK_TR_CTRL, clr_error, clr);
	SETS(network_tr_ctrl_shadow, R_NETWORK_TR_CTRL, delay, none);
	SETS(network_tr_ctrl_shadow, R_NETWORK_TR_CTRL, cancel, dont);
	SETS(network_tr_ctrl_shadow, R_NETWORK_TR_CTRL, cd, enable);
	SETS(network_tr_ctrl_shadow, R_NETWORK_TR_CTRL, retry, enable);
	SETS(network_tr_ctrl_shadow, R_NETWORK_TR_CTRL, pad, enable);
	SETS(network_tr_ctrl_shadow, R_NETWORK_TR_CTRL, crc, enable);
	*R_NETWORK_TR_CTRL = network_tr_ctrl_shadow;

	local_irq_save(flags);

	/* enable the irq's for ethernet DMA */

	*R_IRQ_MASK2_SET =
		IO_STATE(R_IRQ_MASK2_SET, dma0_eop, set) |
		IO_STATE(R_IRQ_MASK2_SET, dma1_eop, set);

	*R_IRQ_MASK0_SET =
		IO_STATE(R_IRQ_MASK0_SET, overrun,       set) |
		IO_STATE(R_IRQ_MASK0_SET, underrun,      set) |
		IO_STATE(R_IRQ_MASK0_SET, excessive_col, set);

	/* make sure the irqs are cleared */

	*R_DMA_CH0_CLR_INTR = IO_STATE(R_DMA_CH0_CLR_INTR, clr_eop, do);
	*R_DMA_CH1_CLR_INTR = IO_STATE(R_DMA_CH1_CLR_INTR, clr_eop, do);

	/* make sure the rec and transmit error counters are cleared */

	(void)*R_REC_COUNTERS;  /* dummy read */
	(void)*R_TR_COUNTERS;   /* dummy read */

	/* start the receiving DMA channel so we can receive packets from now on */

	*R_DMA_CH1_FIRST = virt_to_phys(myNextRxDesc);
	*R_DMA_CH1_CMD = IO_STATE(R_DMA_CH1_CMD, cmd, start);

	/* Set up transmit DMA channel so it can be restarted later */

	*R_DMA_CH0_FIRST = 0;
	*R_DMA_CH0_DESCR = virt_to_phys(myLastTxDesc);
	netif_start_queue(dev);

	local_irq_restore(flags);

	/* Probe for transceiver */
	if (e100_probe_transceiver(dev))
		goto grace_exit5;

	/* Start duplex/speed timers */
	add_timer(&speed_timer);
	add_timer(&duplex_timer);

	/* We are now ready to accept transmit requeusts from
	 * the queueing layer of the networking.
	 */
	netif_carrier_on(dev);

	return 0;

grace_exit5:
	cris_free_dma(NETWORK_RX_DMA_NBR, cardname);
grace_exit4:
	cris_free_dma(NETWORK_TX_DMA_NBR, cardname);
grace_exit3:
	free_irq(NETWORK_STATUS_IRQ_NBR, (void *)dev);
grace_exit2:
	free_irq(NETWORK_DMA_TX_IRQ_NBR, (void *)dev);
grace_exit1:
	free_irq(NETWORK_DMA_RX_IRQ_NBR, (void *)dev);
grace_exit0:
	return -EAGAIN;
}

#if defined(CONFIG_ETRAX_NO_PHY)
static void
dummy_check_speed(struct net_device* dev)
{
	current_speed = 100;
}
#else
static void
generic_check_speed(struct net_device* dev)
{
	unsigned long data;
	struct net_local *np = netdev_priv(dev);

	data = e100_get_mdio_reg(dev, np->mii_if.phy_id, MII_ADVERTISE);
	if ((data & ADVERTISE_100FULL) ||
	    (data & ADVERTISE_100HALF))
		current_speed = 100;
	else
		current_speed = 10;
}

static void
tdk_check_speed(struct net_device* dev)
{
	unsigned long data;
	struct net_local *np = netdev_priv(dev);

	data = e100_get_mdio_reg(dev, np->mii_if.phy_id,
				 MDIO_TDK_DIAGNOSTIC_REG);
	current_speed = (data & MDIO_TDK_DIAGNOSTIC_RATE ? 100 : 10);
}

static void
broadcom_check_speed(struct net_device* dev)
{
	unsigned long data;
	struct net_local *np = netdev_priv(dev);

	data = e100_get_mdio_reg(dev, np->mii_if.phy_id,
				 MDIO_AUX_CTRL_STATUS_REG);
	current_speed = (data & MDIO_BC_SPEED ? 100 : 10);
}

static void
intel_check_speed(struct net_device* dev)
{
	unsigned long data;
	struct net_local *np = netdev_priv(dev);

	data = e100_get_mdio_reg(dev, np->mii_if.phy_id,
				 MDIO_INT_STATUS_REG_2);
	current_speed = (data & MDIO_INT_SPEED ? 100 : 10);
}
#endif
static void
e100_check_speed(unsigned long priv)
{
	struct net_device* dev = (struct net_device*)priv;
	struct net_local *np = netdev_priv(dev);
	static int led_initiated = 0;
	unsigned long data;
	int old_speed = current_speed;

	spin_lock(&np->transceiver_lock);

	data = e100_get_mdio_reg(dev, np->mii_if.phy_id, MII_BMSR);
	if (!(data & BMSR_LSTATUS)) {
		current_speed = 0;
	} else {
		transceiver->check_speed(dev);
	}

	spin_lock(&np->led_lock);
	if ((old_speed != current_speed) || !led_initiated) {
		led_initiated = 1;
		e100_set_network_leds(NO_NETWORK_ACTIVITY);
		if (current_speed)
			netif_carrier_on(dev);
		else
			netif_carrier_off(dev);
	}
	spin_unlock(&np->led_lock);

	/* Reinitialize the timer. */
	speed_timer.expires = jiffies + NET_LINK_UP_CHECK_INTERVAL;
	add_timer(&speed_timer);

	spin_unlock(&np->transceiver_lock);
}

static void
e100_negotiate(struct net_device* dev)
{
	struct net_local *np = netdev_priv(dev);
	unsigned short data = e100_get_mdio_reg(dev, np->mii_if.phy_id,
						MII_ADVERTISE);

	/* Discard old speed and duplex settings */
	data &= ~(ADVERTISE_100HALF | ADVERTISE_100FULL |
	          ADVERTISE_10HALF | ADVERTISE_10FULL);

	switch (current_speed_selection) {
		case 10:
			if (current_duplex == full)
				data |= ADVERTISE_10FULL;
			else if (current_duplex == half)
				data |= ADVERTISE_10HALF;
			else
				data |= ADVERTISE_10HALF | ADVERTISE_10FULL;
			break;

		case 100:
			 if (current_duplex == full)
				data |= ADVERTISE_100FULL;
			else if (current_duplex == half)
				data |= ADVERTISE_100HALF;
			else
				data |= ADVERTISE_100HALF | ADVERTISE_100FULL;
			break;

		case 0: /* Auto */
			 if (current_duplex == full)
				data |= ADVERTISE_100FULL | ADVERTISE_10FULL;
			else if (current_duplex == half)
				data |= ADVERTISE_100HALF | ADVERTISE_10HALF;
			else
				data |= ADVERTISE_10HALF | ADVERTISE_10FULL |
				  ADVERTISE_100HALF | ADVERTISE_100FULL;
			break;

		default: /* assume autoneg speed and duplex */
			data |= ADVERTISE_10HALF | ADVERTISE_10FULL |
				  ADVERTISE_100HALF | ADVERTISE_100FULL;
			break;
	}

	e100_set_mdio_reg(dev, np->mii_if.phy_id, MII_ADVERTISE, data);

	data = e100_get_mdio_reg(dev, np->mii_if.phy_id, MII_BMCR);
	if (autoneg_normal) {
		/* Renegotiate with link partner */
		data |= BMCR_ANENABLE | BMCR_ANRESTART;
	} else {
		/* Don't negotiate speed or duplex */
		data &= ~(BMCR_ANENABLE | BMCR_ANRESTART);

		/* Set speed and duplex static */
		if (current_speed_selection == 10)
			data &= ~BMCR_SPEED100;
		else
			data |= BMCR_SPEED100;

		if (current_duplex != full)
			data &= ~BMCR_FULLDPLX;
		else
			data |= BMCR_FULLDPLX;
	}
	e100_set_mdio_reg(dev, np->mii_if.phy_id, MII_BMCR, data);
}

static void
e100_set_speed(struct net_device* dev, unsigned long speed)
{
	struct net_local *np = netdev_priv(dev);

	spin_lock(&np->transceiver_lock);
	if (speed != current_speed_selection) {
		current_speed_selection = speed;
		e100_negotiate(dev);
	}
	spin_unlock(&np->transceiver_lock);
}

static void
e100_check_duplex(unsigned long priv)
{
	struct net_device *dev = (struct net_device *)priv;
	struct net_local *np = netdev_priv(dev);
	int old_duplex;

	spin_lock(&np->transceiver_lock);
	old_duplex = full_duplex;
	transceiver->check_duplex(dev);
	if (old_duplex != full_duplex) {
		/* Duplex changed */
		SETF(network_rec_config_shadow, R_NETWORK_REC_CONFIG, duplex, full_duplex);
		*R_NETWORK_REC_CONFIG = network_rec_config_shadow;
	}

	/* Reinitialize the timer. */
	duplex_timer.expires = jiffies + NET_DUPLEX_CHECK_INTERVAL;
	add_timer(&duplex_timer);
	np->mii_if.full_duplex = full_duplex;
	spin_unlock(&np->transceiver_lock);
}
#if defined(CONFIG_ETRAX_NO_PHY)
static void
dummy_check_duplex(struct net_device* dev)
{
	full_duplex = 1;
}
#else
static void
generic_check_duplex(struct net_device* dev)
{
	unsigned long data;
	struct net_local *np = netdev_priv(dev);

	data = e100_get_mdio_reg(dev, np->mii_if.phy_id, MII_ADVERTISE);
	if ((data & ADVERTISE_10FULL) ||
	    (data & ADVERTISE_100FULL))
		full_duplex = 1;
	else
		full_duplex = 0;
}

static void
tdk_check_duplex(struct net_device* dev)
{
	unsigned long data;
	struct net_local *np = netdev_priv(dev);

	data = e100_get_mdio_reg(dev, np->mii_if.phy_id,
				 MDIO_TDK_DIAGNOSTIC_REG);
	full_duplex = (data & MDIO_TDK_DIAGNOSTIC_DPLX) ? 1 : 0;
}

static void
broadcom_check_duplex(struct net_device* dev)
{
	unsigned long data;
	struct net_local *np = netdev_priv(dev);

	data = e100_get_mdio_reg(dev, np->mii_if.phy_id,
				 MDIO_AUX_CTRL_STATUS_REG);
	full_duplex = (data & MDIO_BC_FULL_DUPLEX_IND) ? 1 : 0;
}

static void
intel_check_duplex(struct net_device* dev)
{
	unsigned long data;
	struct net_local *np = netdev_priv(dev);

	data = e100_get_mdio_reg(dev, np->mii_if.phy_id,
				 MDIO_INT_STATUS_REG_2);
	full_duplex = (data & MDIO_INT_FULL_DUPLEX_IND) ? 1 : 0;
}
#endif
static void
e100_set_duplex(struct net_device* dev, enum duplex new_duplex)
{
	struct net_local *np = netdev_priv(dev);

	spin_lock(&np->transceiver_lock);
	if (new_duplex != current_duplex) {
		current_duplex = new_duplex;
		e100_negotiate(dev);
	}
	spin_unlock(&np->transceiver_lock);
}

static int
e100_probe_transceiver(struct net_device* dev)
{
	int ret = 0;

#if !defined(CONFIG_ETRAX_NO_PHY)
	unsigned int phyid_high;
	unsigned int phyid_low;
	unsigned int oui;
	struct transceiver_ops* ops = NULL;
	struct net_local *np = netdev_priv(dev);

	spin_lock(&np->transceiver_lock);

	/* Probe MDIO physical address */
	for (np->mii_if.phy_id = 0; np->mii_if.phy_id <= 31;
	     np->mii_if.phy_id++) {
		if (e100_get_mdio_reg(dev,
				      np->mii_if.phy_id, MII_BMSR) != 0xffff)
			break;
	}
	if (np->mii_if.phy_id == 32) {
		ret = -ENODEV;
		goto out;
	}

	/* Get manufacturer */
	phyid_high = e100_get_mdio_reg(dev, np->mii_if.phy_id, MII_PHYSID1);
	phyid_low = e100_get_mdio_reg(dev, np->mii_if.phy_id, MII_PHYSID2);
	oui = (phyid_high << 6) | (phyid_low >> 10);

	for (ops = &transceivers[0]; ops->oui; ops++) {
		if (ops->oui == oui)
			break;
	}
	transceiver = ops;
out:
	spin_unlock(&np->transceiver_lock);
#endif
	return ret;
}

static int
e100_get_mdio_reg(struct net_device *dev, int phy_id, int location)
{
	unsigned short cmd;    /* Data to be sent on MDIO port */
	int data;   /* Data read from MDIO */
	int bitCounter;

	/* Start of frame, OP Code, Physical Address, Register Address */
	cmd = (MDIO_START << 14) | (MDIO_READ << 12) | (phy_id << 7) |
		(location << 2);

	e100_send_mdio_cmd(cmd, 0);

	data = 0;

	/* Data... */
	for (bitCounter=15; bitCounter>=0 ; bitCounter--) {
		data |= (e100_receive_mdio_bit() << bitCounter);
	}

	return data;
}

static void
e100_set_mdio_reg(struct net_device *dev, int phy_id, int location, int value)
{
	int bitCounter;
	unsigned short cmd;

	cmd = (MDIO_START << 14) | (MDIO_WRITE << 12) | (phy_id << 7) |
	      (location << 2);

	e100_send_mdio_cmd(cmd, 1);

	/* Data... */
	for (bitCounter=15; bitCounter>=0 ; bitCounter--) {
		e100_send_mdio_bit(GET_BIT(bitCounter, value));
	}

}

static void
e100_send_mdio_cmd(unsigned short cmd, int write_cmd)
{
	int bitCounter;
	unsigned char data = 0x2;

	/* Preamble */
	for (bitCounter = 31; bitCounter>= 0; bitCounter--)
		e100_send_mdio_bit(GET_BIT(bitCounter, MDIO_PREAMBLE));

	for (bitCounter = 15; bitCounter >= 2; bitCounter--)
		e100_send_mdio_bit(GET_BIT(bitCounter, cmd));

	/* Turnaround */
	for (bitCounter = 1; bitCounter >= 0 ; bitCounter--)
		if (write_cmd)
			e100_send_mdio_bit(GET_BIT(bitCounter, data));
		else
			e100_receive_mdio_bit();
}

static void
e100_send_mdio_bit(unsigned char bit)
{
	*R_NETWORK_MGM_CTRL =
		IO_STATE(R_NETWORK_MGM_CTRL, mdoe, enable) |
		IO_FIELD(R_NETWORK_MGM_CTRL, mdio, bit);
	udelay(1);
	*R_NETWORK_MGM_CTRL =
		IO_STATE(R_NETWORK_MGM_CTRL, mdoe, enable) |
		IO_MASK(R_NETWORK_MGM_CTRL, mdck) |
		IO_FIELD(R_NETWORK_MGM_CTRL, mdio, bit);
	udelay(1);
}

static unsigned char
e100_receive_mdio_bit()
{
	unsigned char bit;
	*R_NETWORK_MGM_CTRL = 0;
	bit = IO_EXTRACT(R_NETWORK_STAT, mdio, *R_NETWORK_STAT);
	udelay(1);
	*R_NETWORK_MGM_CTRL = IO_MASK(R_NETWORK_MGM_CTRL, mdck);
	udelay(1);
	return bit;
}

static void
e100_reset_transceiver(struct net_device* dev)
{
	struct net_local *np = netdev_priv(dev);
	unsigned short cmd;
	unsigned short data;
	int bitCounter;

	data = e100_get_mdio_reg(dev, np->mii_if.phy_id, MII_BMCR);

	cmd = (MDIO_START << 14) | (MDIO_WRITE << 12) | (np->mii_if.phy_id << 7) | (MII_BMCR << 2);

	e100_send_mdio_cmd(cmd, 1);

	data |= 0x8000;

	for (bitCounter = 15; bitCounter >= 0 ; bitCounter--) {
		e100_send_mdio_bit(GET_BIT(bitCounter, data));
	}
}

/* Called by upper layers if they decide it took too long to complete
 * sending a packet - we need to reset and stuff.
 */

static void
e100_tx_timeout(struct net_device *dev)
{
	struct net_local *np = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&np->lock, flags);

	printk(KERN_WARNING "%s: transmit timed out, %s?\n", dev->name,
	       tx_done(dev) ? "IRQ problem" : "network cable problem");

	/* remember we got an error */

	np->stats.tx_errors++;

	/* reset the TX DMA in case it has hung on something */

	RESET_DMA(NETWORK_TX_DMA_NBR);
	WAIT_DMA(NETWORK_TX_DMA_NBR);

	/* Reset the transceiver. */

	e100_reset_transceiver(dev);

	/* and get rid of the packets that never got an interrupt */
	while (myFirstTxDesc != myNextTxDesc) {
		dev_kfree_skb(myFirstTxDesc->skb);
		myFirstTxDesc->skb = 0;
		myFirstTxDesc = phys_to_virt(myFirstTxDesc->descr.next);
	}

	/* Set up transmit DMA channel so it can be restarted later */
	*R_DMA_CH0_FIRST = 0;
	*R_DMA_CH0_DESCR = virt_to_phys(myLastTxDesc);

	/* tell the upper layers we're ok again */

	netif_wake_queue(dev);
	spin_unlock_irqrestore(&np->lock, flags);
}


/* This will only be invoked if the driver is _not_ in XOFF state.
 * What this means is that we need not check it, and that this
 * invariant will hold if we make sure that the netif_*_queue()
 * calls are done at the proper times.
 */

static int
e100_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct net_local *np = netdev_priv(dev);
	unsigned char *buf = skb->data;
	unsigned long flags;

#ifdef ETHDEBUG
	printk("send packet len %d\n", length);
#endif
	spin_lock_irqsave(&np->lock, flags);  /* protect from tx_interrupt and ourself */

	myNextTxDesc->skb = skb;

	dev->trans_start = jiffies;

	e100_hardware_send_packet(np, buf, skb->len);

	myNextTxDesc = phys_to_virt(myNextTxDesc->descr.next);

	/* Stop queue if full */
	if (myNextTxDesc == myFirstTxDesc) {
		netif_stop_queue(dev);
	}

	spin_unlock_irqrestore(&np->lock, flags);

	return NETDEV_TX_OK;
}

/*
 * The typical workload of the driver:
 *   Handle the network interface interrupts.
 */

static irqreturn_t
e100rxtx_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct net_local *np = netdev_priv(dev);
	unsigned long irqbits;

	/*
	 * Note that both rx and tx interrupts are blocked at this point,
	 * regardless of which got us here.
	 */

	irqbits = *R_IRQ_MASK2_RD;

	/* Handle received packets */
	if (irqbits & IO_STATE(R_IRQ_MASK2_RD, dma1_eop, active)) {
		/* acknowledge the eop interrupt */

		*R_DMA_CH1_CLR_INTR = IO_STATE(R_DMA_CH1_CLR_INTR, clr_eop, do);

		/* check if one or more complete packets were indeed received */

		while ((*R_DMA_CH1_FIRST != virt_to_phys(myNextRxDesc)) &&
		       (myNextRxDesc != myLastRxDesc)) {
			/* Take out the buffer and give it to the OS, then
			 * allocate a new buffer to put a packet in.
			 */
			e100_rx(dev);
			np->stats.rx_packets++;
			/* restart/continue on the channel, for safety */
			*R_DMA_CH1_CMD = IO_STATE(R_DMA_CH1_CMD, cmd, restart);
			/* clear dma channel 1 eop/descr irq bits */
			*R_DMA_CH1_CLR_INTR =
				IO_STATE(R_DMA_CH1_CLR_INTR, clr_eop, do) |
				IO_STATE(R_DMA_CH1_CLR_INTR, clr_descr, do);

			/* now, we might have gotten another packet
			   so we have to loop back and check if so */
		}
	}

	/* Report any packets that have been sent */
	while (virt_to_phys(myFirstTxDesc) != *R_DMA_CH0_FIRST &&
	       (netif_queue_stopped(dev) || myFirstTxDesc != myNextTxDesc)) {
		np->stats.tx_bytes += myFirstTxDesc->skb->len;
		np->stats.tx_packets++;

		/* dma is ready with the transmission of the data in tx_skb, so now
		   we can release the skb memory */
		dev_kfree_skb_irq(myFirstTxDesc->skb);
		myFirstTxDesc->skb = 0;
		myFirstTxDesc = phys_to_virt(myFirstTxDesc->descr.next);
                /* Wake up queue. */
		netif_wake_queue(dev);
	}

	if (irqbits & IO_STATE(R_IRQ_MASK2_RD, dma0_eop, active)) {
		/* acknowledge the eop interrupt. */
		*R_DMA_CH0_CLR_INTR = IO_STATE(R_DMA_CH0_CLR_INTR, clr_eop, do);
	}

	return IRQ_HANDLED;
}

static irqreturn_t
e100nw_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct net_local *np = netdev_priv(dev);
	unsigned long irqbits = *R_IRQ_MASK0_RD;

	/* check for underrun irq */
	if (irqbits & IO_STATE(R_IRQ_MASK0_RD, underrun, active)) {
		SETS(network_tr_ctrl_shadow, R_NETWORK_TR_CTRL, clr_error, clr);
		*R_NETWORK_TR_CTRL = network_tr_ctrl_shadow;
		SETS(network_tr_ctrl_shadow, R_NETWORK_TR_CTRL, clr_error, nop);
		np->stats.tx_errors++;
		D(printk("ethernet receiver underrun!\n"));
	}

	/* check for overrun irq */
	if (irqbits & IO_STATE(R_IRQ_MASK0_RD, overrun, active)) {
		update_rx_stats(&np->stats); /* this will ack the irq */
		D(printk("ethernet receiver overrun!\n"));
	}
	/* check for excessive collision irq */
	if (irqbits & IO_STATE(R_IRQ_MASK0_RD, excessive_col, active)) {
		SETS(network_tr_ctrl_shadow, R_NETWORK_TR_CTRL, clr_error, clr);
		*R_NETWORK_TR_CTRL = network_tr_ctrl_shadow;
		SETS(network_tr_ctrl_shadow, R_NETWORK_TR_CTRL, clr_error, nop);
		np->stats.tx_errors++;
		D(printk("ethernet excessive collisions!\n"));
	}
	return IRQ_HANDLED;
}

/* We have a good packet(s), get it/them out of the buffers. */
static void
e100_rx(struct net_device *dev)
{
	struct sk_buff *skb;
	int length = 0;
	struct net_local *np = netdev_priv(dev);
	unsigned char *skb_data_ptr;
#ifdef ETHDEBUG
	int i;
#endif
	etrax_eth_descr *prevRxDesc;  /* The descriptor right before myNextRxDesc */
	spin_lock(&np->led_lock);
	if (!led_active && time_after(jiffies, led_next_time)) {
		/* light the network leds depending on the current speed. */
		e100_set_network_leds(NETWORK_ACTIVITY);

		/* Set the earliest time we may clear the LED */
		led_next_time = jiffies + NET_FLASH_TIME;
		led_active = 1;
		mod_timer(&clear_led_timer, jiffies + HZ/10);
	}
	spin_unlock(&np->led_lock);

	length = myNextRxDesc->descr.hw_len - 4;
	np->stats.rx_bytes += length;

#ifdef ETHDEBUG
	printk("Got a packet of length %d:\n", length);
	/* dump the first bytes in the packet */
	skb_data_ptr = (unsigned char *)phys_to_virt(myNextRxDesc->descr.buf);
	for (i = 0; i < 8; i++) {
		printk("%d: %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x\n", i * 8,
		       skb_data_ptr[0],skb_data_ptr[1],skb_data_ptr[2],skb_data_ptr[3],
		       skb_data_ptr[4],skb_data_ptr[5],skb_data_ptr[6],skb_data_ptr[7]);
		skb_data_ptr += 8;
	}
#endif

	if (length < RX_COPYBREAK) {
		/* Small packet, copy data */
		skb = dev_alloc_skb(length - ETHER_HEAD_LEN);
		if (!skb) {
			np->stats.rx_errors++;
			printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n", dev->name);
			goto update_nextrxdesc;
		}

		skb_put(skb, length - ETHER_HEAD_LEN);        /* allocate room for the packet body */
		skb_data_ptr = skb_push(skb, ETHER_HEAD_LEN); /* allocate room for the header */

#ifdef ETHDEBUG
		printk("head = 0x%x, data = 0x%x, tail = 0x%x, end = 0x%x\n",
		       skb->head, skb->data, skb_tail_pointer(skb),
		       skb_end_pointer(skb));
		printk("copying packet to 0x%x.\n", skb_data_ptr);
#endif

		memcpy(skb_data_ptr, phys_to_virt(myNextRxDesc->descr.buf), length);
	}
	else {
		/* Large packet, send directly to upper layers and allocate new
		 * memory (aligned to cache line boundary to avoid bug).
		 * Before sending the skb to upper layers we must make sure
		 * that skb->data points to the aligned start of the packet.
		 */
		int align;
		struct sk_buff *new_skb = dev_alloc_skb(MAX_MEDIA_DATA_SIZE + 2 * L1_CACHE_BYTES);
		if (!new_skb) {
			np->stats.rx_errors++;
			printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n", dev->name);
			goto update_nextrxdesc;
		}
		skb = myNextRxDesc->skb;
		align = (int)phys_to_virt(myNextRxDesc->descr.buf) - (int)skb->data;
		skb_put(skb, length + align);
		skb_pull(skb, align); /* Remove alignment bytes */
		myNextRxDesc->skb = new_skb;
		myNextRxDesc->descr.buf = L1_CACHE_ALIGN(virt_to_phys(myNextRxDesc->skb->data));
	}

	skb->protocol = eth_type_trans(skb, dev);

	/* Send the packet to the upper layers */
	netif_rx(skb);

  update_nextrxdesc:
	/* Prepare for next packet */
	myNextRxDesc->descr.status = 0;
	prevRxDesc = myNextRxDesc;
	myNextRxDesc = phys_to_virt(myNextRxDesc->descr.next);

	rx_queue_len++;

	/* Check if descriptors should be returned */
	if (rx_queue_len == RX_QUEUE_THRESHOLD) {
		flush_etrax_cache();
		prevRxDesc->descr.ctrl |= d_eol;
		myLastRxDesc->descr.ctrl &= ~d_eol;
		myLastRxDesc = prevRxDesc;
		rx_queue_len = 0;
	}
}

/* The inverse routine to net_open(). */
static int
e100_close(struct net_device *dev)
{
	struct net_local *np = netdev_priv(dev);

	printk(KERN_INFO "Closing %s.\n", dev->name);

	netif_stop_queue(dev);

	*R_IRQ_MASK0_CLR =
		IO_STATE(R_IRQ_MASK0_CLR, overrun, clr) |
		IO_STATE(R_IRQ_MASK0_CLR, underrun, clr) |
		IO_STATE(R_IRQ_MASK0_CLR, excessive_col, clr);

	*R_IRQ_MASK2_CLR =
		IO_STATE(R_IRQ_MASK2_CLR, dma0_descr, clr) |
		IO_STATE(R_IRQ_MASK2_CLR, dma0_eop, clr) |
		IO_STATE(R_IRQ_MASK2_CLR, dma1_descr, clr) |
		IO_STATE(R_IRQ_MASK2_CLR, dma1_eop, clr);

	/* Stop the receiver and the transmitter */

	RESET_DMA(NETWORK_TX_DMA_NBR);
	RESET_DMA(NETWORK_RX_DMA_NBR);

	/* Flush the Tx and disable Rx here. */

	free_irq(NETWORK_DMA_RX_IRQ_NBR, (void *)dev);
	free_irq(NETWORK_DMA_TX_IRQ_NBR, (void *)dev);
	free_irq(NETWORK_STATUS_IRQ_NBR, (void *)dev);

	cris_free_dma(NETWORK_TX_DMA_NBR, cardname);
	cris_free_dma(NETWORK_RX_DMA_NBR, cardname);

	/* Update the statistics here. */

	update_rx_stats(&np->stats);
	update_tx_stats(&np->stats);

	/* Stop speed/duplex timers */
	del_timer(&speed_timer);
	del_timer(&duplex_timer);

	return 0;
}

static int
e100_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mii_ioctl_data *data = if_mii(ifr);
	struct net_local *np = netdev_priv(dev);
	int rc = 0;
        int old_autoneg;

	spin_lock(&np->lock); /* Preempt protection */
	switch (cmd) {
		/* The ioctls below should be considered obsolete but are */
		/* still present for compatability with old scripts/apps  */
		case SET_ETH_SPEED_10:                  /* 10 Mbps */
			e100_set_speed(dev, 10);
			break;
		case SET_ETH_SPEED_100:                /* 100 Mbps */
			e100_set_speed(dev, 100);
			break;
		case SET_ETH_SPEED_AUTO:        /* Auto-negotiate speed */
			e100_set_speed(dev, 0);
			break;
		case SET_ETH_DUPLEX_HALF:       /* Half duplex */
			e100_set_duplex(dev, half);
			break;
		case SET_ETH_DUPLEX_FULL:       /* Full duplex */
			e100_set_duplex(dev, full);
			break;
		case SET_ETH_DUPLEX_AUTO:       /* Auto-negotiate duplex */
			e100_set_duplex(dev, autoneg);
			break;
	        case SET_ETH_AUTONEG:
			old_autoneg = autoneg_normal;
		        autoneg_normal = *(int*)data;
			if (autoneg_normal != old_autoneg)
				e100_negotiate(dev);
			break;
		default:
			rc = generic_mii_ioctl(&np->mii_if, if_mii(ifr),
						cmd, NULL);
			break;
	}
	spin_unlock(&np->lock);
	return rc;
}

static int e100_get_settings(struct net_device *dev,
			     struct ethtool_cmd *cmd)
{
	struct net_local *np = netdev_priv(dev);
	int err;

	spin_lock_irq(&np->lock);
	err = mii_ethtool_gset(&np->mii_if, cmd);
	spin_unlock_irq(&np->lock);

	/* The PHY may support 1000baseT, but the Etrax100 does not.  */
	cmd->supported &= ~(SUPPORTED_1000baseT_Half
			    | SUPPORTED_1000baseT_Full);
	return err;
}

static int e100_set_settings(struct net_device *dev,
			     struct ethtool_cmd *ecmd)
{
	if (ecmd->autoneg == AUTONEG_ENABLE) {
		e100_set_duplex(dev, autoneg);
		e100_set_speed(dev, 0);
	} else {
		e100_set_duplex(dev, ecmd->duplex == DUPLEX_HALF ? half : full);
		e100_set_speed(dev, ecmd->speed == SPEED_10 ? 10: 100);
	}

	return 0;
}

static void e100_get_drvinfo(struct net_device *dev,
			     struct ethtool_drvinfo *info)
{
	strncpy(info->driver, "ETRAX 100LX", sizeof(info->driver) - 1);
	strncpy(info->version, "$Revision: 1.31 $", sizeof(info->version) - 1);
	strncpy(info->fw_version, "N/A", sizeof(info->fw_version) - 1);
	strncpy(info->bus_info, "N/A", sizeof(info->bus_info) - 1);
}

static int e100_nway_reset(struct net_device *dev)
{
	if (current_duplex == autoneg && current_speed_selection == 0)
		e100_negotiate(dev);
	return 0;
}

static const struct ethtool_ops e100_ethtool_ops = {
	.get_settings	= e100_get_settings,
	.set_settings	= e100_set_settings,
	.get_drvinfo	= e100_get_drvinfo,
	.nway_reset	= e100_nway_reset,
	.get_link	= ethtool_op_get_link,
};

static int
e100_set_config(struct net_device *dev, struct ifmap *map)
{
	struct net_local *np = netdev_priv(dev);

	spin_lock(&np->lock); /* Preempt protection */

	switch(map->port) {
		case IF_PORT_UNKNOWN:
			/* Use autoneg */
			e100_set_speed(dev, 0);
			e100_set_duplex(dev, autoneg);
			break;
		case IF_PORT_10BASET:
			e100_set_speed(dev, 10);
			e100_set_duplex(dev, autoneg);
			break;
		case IF_PORT_100BASET:
		case IF_PORT_100BASETX:
			e100_set_speed(dev, 100);
			e100_set_duplex(dev, autoneg);
			break;
		case IF_PORT_100BASEFX:
		case IF_PORT_10BASE2:
		case IF_PORT_AUI:
			spin_unlock(&np->lock);
			return -EOPNOTSUPP;
			break;
		default:
			printk(KERN_ERR "%s: Invalid media selected", dev->name);
			spin_unlock(&np->lock);
			return -EINVAL;
	}
	spin_unlock(&np->lock);
	return 0;
}

static void
update_rx_stats(struct net_device_stats *es)
{
	unsigned long r = *R_REC_COUNTERS;
	/* update stats relevant to reception errors */
	es->rx_fifo_errors += IO_EXTRACT(R_REC_COUNTERS, congestion, r);
	es->rx_crc_errors += IO_EXTRACT(R_REC_COUNTERS, crc_error, r);
	es->rx_frame_errors += IO_EXTRACT(R_REC_COUNTERS, alignment_error, r);
	es->rx_length_errors += IO_EXTRACT(R_REC_COUNTERS, oversize, r);
}

static void
update_tx_stats(struct net_device_stats *es)
{
	unsigned long r = *R_TR_COUNTERS;
	/* update stats relevant to transmission errors */
	es->collisions +=
		IO_EXTRACT(R_TR_COUNTERS, single_col, r) +
		IO_EXTRACT(R_TR_COUNTERS, multiple_col, r);
}

/*
 * Get the current statistics.
 * This may be called with the card open or closed.
 */
static struct net_device_stats *
e100_get_stats(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&lp->lock, flags);

	update_rx_stats(&lp->stats);
	update_tx_stats(&lp->stats);

	spin_unlock_irqrestore(&lp->lock, flags);
	return &lp->stats;
}

/*
 * Set or clear the multicast filter for this adaptor.
 * num_addrs == -1	Promiscuous mode, receive all packets
 * num_addrs == 0	Normal mode, clear multicast list
 * num_addrs > 0	Multicast mode, receive normal and MC packets,
 *			and do best-effort filtering.
 */
static void
set_multicast_list(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	int num_addr = dev->mc_count;
	unsigned long int lo_bits;
	unsigned long int hi_bits;

	spin_lock(&lp->lock);
	if (dev->flags & IFF_PROMISC) {
		/* promiscuous mode */
		lo_bits = 0xfffffffful;
		hi_bits = 0xfffffffful;

		/* Enable individual receive */
		SETS(network_rec_config_shadow, R_NETWORK_REC_CONFIG, individual, receive);
		*R_NETWORK_REC_CONFIG = network_rec_config_shadow;
	} else if (dev->flags & IFF_ALLMULTI) {
		/* enable all multicasts */
		lo_bits = 0xfffffffful;
		hi_bits = 0xfffffffful;

		/* Disable individual receive */
		SETS(network_rec_config_shadow, R_NETWORK_REC_CONFIG, individual, discard);
		*R_NETWORK_REC_CONFIG =  network_rec_config_shadow;
	} else if (num_addr == 0) {
		/* Normal, clear the mc list */
		lo_bits = 0x00000000ul;
		hi_bits = 0x00000000ul;

		/* Disable individual receive */
		SETS(network_rec_config_shadow, R_NETWORK_REC_CONFIG, individual, discard);
		*R_NETWORK_REC_CONFIG =  network_rec_config_shadow;
	} else {
		/* MC mode, receive normal and MC packets */
		char hash_ix;
		struct dev_mc_list *dmi = dev->mc_list;
		int i;
		char *baddr;

		lo_bits = 0x00000000ul;
		hi_bits = 0x00000000ul;
		for (i = 0; i < num_addr; i++) {
			/* Calculate the hash index for the GA registers */

			hash_ix = 0;
			baddr = dmi->dmi_addr;
			hash_ix ^= (*baddr) & 0x3f;
			hash_ix ^= ((*baddr) >> 6) & 0x03;
			++baddr;
			hash_ix ^= ((*baddr) << 2) & 0x03c;
			hash_ix ^= ((*baddr) >> 4) & 0xf;
			++baddr;
			hash_ix ^= ((*baddr) << 4) & 0x30;
			hash_ix ^= ((*baddr) >> 2) & 0x3f;
			++baddr;
			hash_ix ^= (*baddr) & 0x3f;
			hash_ix ^= ((*baddr) >> 6) & 0x03;
			++baddr;
			hash_ix ^= ((*baddr) << 2) & 0x03c;
			hash_ix ^= ((*baddr) >> 4) & 0xf;
			++baddr;
			hash_ix ^= ((*baddr) << 4) & 0x30;
			hash_ix ^= ((*baddr) >> 2) & 0x3f;

			hash_ix &= 0x3f;

			if (hash_ix >= 32) {
				hi_bits |= (1 << (hash_ix-32));
			} else {
				lo_bits |= (1 << hash_ix);
			}
			dmi = dmi->next;
		}
		/* Disable individual receive */
		SETS(network_rec_config_shadow, R_NETWORK_REC_CONFIG, individual, discard);
		*R_NETWORK_REC_CONFIG = network_rec_config_shadow;
	}
	*R_NETWORK_GA_0 = lo_bits;
	*R_NETWORK_GA_1 = hi_bits;
	spin_unlock(&lp->lock);
}

void
e100_hardware_send_packet(struct net_local *np, char *buf, int length)
{
	D(printk("e100 send pack, buf 0x%x len %d\n", buf, length));

	spin_lock(&np->led_lock);
	if (!led_active && time_after(jiffies, led_next_time)) {
		/* light the network leds depending on the current speed. */
		e100_set_network_leds(NETWORK_ACTIVITY);

		/* Set the earliest time we may clear the LED */
		led_next_time = jiffies + NET_FLASH_TIME;
		led_active = 1;
		mod_timer(&clear_led_timer, jiffies + HZ/10);
	}
	spin_unlock(&np->led_lock);

	/* configure the tx dma descriptor */
	myNextTxDesc->descr.sw_len = length;
	myNextTxDesc->descr.ctrl = d_eop | d_eol | d_wait;
	myNextTxDesc->descr.buf = virt_to_phys(buf);

        /* Move end of list */
        myLastTxDesc->descr.ctrl &= ~d_eol;
        myLastTxDesc = myNextTxDesc;

	/* Restart DMA channel */
	*R_DMA_CH0_CMD = IO_STATE(R_DMA_CH0_CMD, cmd, restart);
}

static void
e100_clear_network_leds(unsigned long dummy)
{
	struct net_device *dev = (struct net_device *)dummy;
	struct net_local *np = netdev_priv(dev);

	spin_lock(&np->led_lock);

	if (led_active && time_after(jiffies, led_next_time)) {
		e100_set_network_leds(NO_NETWORK_ACTIVITY);

		/* Set the earliest time we may set the LED */
		led_next_time = jiffies + NET_FLASH_PAUSE;
		led_active = 0;
	}

	spin_unlock(&np->led_lock);
}

static void
e100_set_network_leds(int active)
{
#if defined(CONFIG_ETRAX_NETWORK_LED_ON_WHEN_LINK)
	int light_leds = (active == NO_NETWORK_ACTIVITY);
#elif defined(CONFIG_ETRAX_NETWORK_LED_ON_WHEN_ACTIVITY)
	int light_leds = (active == NETWORK_ACTIVITY);
#else
#error "Define either CONFIG_ETRAX_NETWORK_LED_ON_WHEN_LINK or CONFIG_ETRAX_NETWORK_LED_ON_WHEN_ACTIVITY"
#endif

	if (!current_speed) {
		/* Make LED red, link is down */
#if defined(CONFIG_ETRAX_NETWORK_RED_ON_NO_CONNECTION)
		CRIS_LED_NETWORK_SET(CRIS_LED_RED);
#else
		CRIS_LED_NETWORK_SET(CRIS_LED_OFF);
#endif
	} else if (light_leds) {
		if (current_speed == 10) {
			CRIS_LED_NETWORK_SET(CRIS_LED_ORANGE);
		} else {
			CRIS_LED_NETWORK_SET(CRIS_LED_GREEN);
		}
	} else {
		CRIS_LED_NETWORK_SET(CRIS_LED_OFF);
	}
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void
e100_netpoll(struct net_device* netdev)
{
	e100rxtx_interrupt(NETWORK_DMA_TX_IRQ_NBR, netdev, NULL);
}
#endif

static int
etrax_init_module(void)
{
	return etrax_ethernet_init();
}

static int __init
e100_boot_setup(char* str)
{
	struct sockaddr sa = {0};
	int i;

	/* Parse the colon separated Ethernet station address */
	for (i = 0; i <  ETH_ALEN; i++) {
		unsigned int tmp;
		if (sscanf(str + 3*i, "%2x", &tmp) != 1) {
			printk(KERN_WARNING "Malformed station address");
			return 0;
		}
		sa.sa_data[i] = (char)tmp;
	}

	default_mac = sa;
	return 1;
}

__setup("etrax100_eth=", e100_boot_setup);

module_init(etrax_init_module);
