/*********************************************************************
 *                
 * Filename:      netwave_cs.c
 * Version:       0.4.1
 * Description:   Netwave AirSurfer Wireless LAN PC Card driver
 * Status:        Experimental.
 * Authors:       John Markus Bjørndalen <johnm@cs.uit.no>
 *                Dag Brattli <dagb@cs.uit.no>
 *                David Hinds <dahinds@users.sourceforge.net>
 * Created at:    A long time ago!
 * Modified at:   Mon Nov 10 11:54:37 1997
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1997 University of Tromsø, Norway
 *
 * Revision History:
 *
 *   08-Nov-97 15:14:47   John Markus Bjørndalen <johnm@cs.uit.no>
 *    - Fixed some bugs in netwave_rx and cleaned it up a bit. 
 *      (One of the bugs would have destroyed packets when receiving
 *      multiple packets per interrupt). 
 *    - Cleaned up parts of newave_hw_xmit. 
 *    - A few general cleanups. 
 *   24-Oct-97 13:17:36   Dag Brattli <dagb@cs.uit.no>
 *    - Fixed netwave_rx receive function (got updated docs)
 *   Others:
 *    - Changed name from xircnw to netwave, take a look at 
 *      http://www.netwave-wireless.com
 *    - Some reorganizing of the code
 *    - Removed possible race condition between interrupt handler and transmit
 *      function
 *    - Started to add wireless extensions, but still needs some coding
 *    - Added watchdog for better handling of transmission timeouts 
 *      (hopefully this works better)
 ********************************************************************/

/* To have statistics (just packets sent) define this */
#undef NETWAVE_STATS

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>
#ifdef CONFIG_NET_RADIO
#include <linux/wireless.h>
#include <net/iw_handler.h>
#endif

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>
#include <pcmcia/mem_op.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>

#define NETWAVE_REGOFF         0x8000
/* The Netwave IO registers, offsets to iobase */
#define NETWAVE_REG_COR        0x0
#define NETWAVE_REG_CCSR       0x2
#define NETWAVE_REG_ASR        0x4
#define NETWAVE_REG_IMR        0xa
#define NETWAVE_REG_PMR        0xc
#define NETWAVE_REG_IOLOW      0x6
#define NETWAVE_REG_IOHI       0x7
#define NETWAVE_REG_IOCONTROL  0x8
#define NETWAVE_REG_DATA       0xf
/* The Netwave Extended IO registers, offsets to RamBase */
#define NETWAVE_EREG_ASCC      0x114
#define NETWAVE_EREG_RSER      0x120
#define NETWAVE_EREG_RSERW     0x124
#define NETWAVE_EREG_TSER      0x130
#define NETWAVE_EREG_TSERW     0x134
#define NETWAVE_EREG_CB        0x100
#define NETWAVE_EREG_SPCQ      0x154
#define NETWAVE_EREG_SPU       0x155
#define NETWAVE_EREG_LIF       0x14e
#define NETWAVE_EREG_ISPLQ     0x156
#define NETWAVE_EREG_HHC       0x158
#define NETWAVE_EREG_NI        0x16e
#define NETWAVE_EREG_MHS       0x16b
#define NETWAVE_EREG_TDP       0x140
#define NETWAVE_EREG_RDP       0x150
#define NETWAVE_EREG_PA        0x160
#define NETWAVE_EREG_EC        0x180
#define NETWAVE_EREG_CRBP      0x17a
#define NETWAVE_EREG_ARW       0x166

/*
 * Commands used in the extended command buffer
 * NETWAVE_EREG_CB (0x100-0x10F) 
 */
#define NETWAVE_CMD_NOP        0x00
#define NETWAVE_CMD_SRC        0x01
#define NETWAVE_CMD_STC        0x02
#define NETWAVE_CMD_AMA        0x03
#define NETWAVE_CMD_DMA        0x04
#define NETWAVE_CMD_SAMA       0x05
#define NETWAVE_CMD_ER         0x06
#define NETWAVE_CMD_DR         0x07
#define NETWAVE_CMD_TL         0x08
#define NETWAVE_CMD_SRP        0x09
#define NETWAVE_CMD_SSK        0x0a
#define NETWAVE_CMD_SMD        0x0b
#define NETWAVE_CMD_SAPD       0x0c
#define NETWAVE_CMD_SSS        0x11
/* End of Command marker */
#define NETWAVE_CMD_EOC        0x00

/* ASR register bits */
#define NETWAVE_ASR_RXRDY   0x80
#define NETWAVE_ASR_TXBA    0x01

#define TX_TIMEOUT		((32*HZ)/100)

static const unsigned int imrConfRFU1 = 0x10; /* RFU interrupt mask, keep high */
static const unsigned int imrConfIENA = 0x02; /* Interrupt enable */

static const unsigned int corConfIENA   = 0x01; /* Interrupt enable */
static const unsigned int corConfLVLREQ = 0x40; /* Keep high */

static const unsigned int rxConfRxEna  = 0x80; /* Receive Enable */
static const unsigned int rxConfMAC    = 0x20; /* MAC host receive mode*/ 
static const unsigned int rxConfPro    = 0x10; /* Promiscuous */
static const unsigned int rxConfAMP    = 0x08; /* Accept Multicast Packets */
static const unsigned int rxConfBcast  = 0x04; /* Accept Broadcast Packets */

static const unsigned int txConfTxEna  = 0x80; /* Transmit Enable */
static const unsigned int txConfMAC    = 0x20; /* Host sends MAC mode */
static const unsigned int txConfEUD    = 0x10; /* Enable Uni-Data packets */
static const unsigned int txConfKey    = 0x02; /* Scramble data packets */
static const unsigned int txConfLoop   = 0x01; /* Loopback mode */

/*
   All the PCMCIA modules use PCMCIA_DEBUG to control debugging.  If
   you do not define PCMCIA_DEBUG at all, all the debug code will be
   left out.  If you compile with PCMCIA_DEBUG=0, the debug code will
   be present but disabled -- but it can then be enabled for specific
   modules at load time with a 'pc_debug=#' option to insmod.
*/

#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
module_param(pc_debug, int, 0);
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
static char *version =
"netwave_cs.c 0.3.0 Thu Jul 17 14:36:02 1997 (John Markus Bjørndalen)\n";
#else
#define DEBUG(n, args...)
#endif

static dev_info_t dev_info = "netwave_cs";

/*====================================================================*/

/* Parameters that can be set with 'insmod' */

/* Choose the domain, default is 0x100 */
static u_int  domain = 0x100;

/* Scramble key, range from 0x0 to 0xffff.  
 * 0x0 is no scrambling. 
 */
static u_int  scramble_key = 0x0;

/* Shared memory speed, in ns. The documentation states that 
 * the card should not be read faster than every 400ns. 
 * This timing should be provided by the HBA. If it becomes a 
 * problem, try setting mem_speed to 400. 
 */
static int mem_speed;

module_param(domain, int, 0);
module_param(scramble_key, int, 0);
module_param(mem_speed, int, 0);

/*====================================================================*/

/* PCMCIA (Card Services) related functions */
static void netwave_release(dev_link_t *link);     /* Card removal */
static int  netwave_event(event_t event, int priority, 
					      event_callback_args_t *args);
static void netwave_pcmcia_config(dev_link_t *arg); /* Runs after card 
													   insertion */
static dev_link_t *netwave_attach(void);     /* Create instance */
static void netwave_detach(dev_link_t *);    /* Destroy instance */

/* Hardware configuration */
static void netwave_doreset(kio_addr_t iobase, u_char __iomem *ramBase);
static void netwave_reset(struct net_device *dev);

/* Misc device stuff */
static int netwave_open(struct net_device *dev);  /* Open the device */
static int netwave_close(struct net_device *dev); /* Close the device */

/* Packet transmission and Packet reception */
static int netwave_start_xmit( struct sk_buff *skb, struct net_device *dev);
static int netwave_rx( struct net_device *dev);

/* Interrupt routines */
static irqreturn_t netwave_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void netwave_watchdog(struct net_device *);

/* Statistics */
static void update_stats(struct net_device *dev);
static struct net_device_stats *netwave_get_stats(struct net_device *dev);

/* Wireless extensions */
static struct iw_statistics* netwave_get_wireless_stats(struct net_device *dev);

static void set_multicast_list(struct net_device *dev);

/*
   A linked list of "instances" of the skeleton device.  Each actual
   PCMCIA card corresponds to one device instance, and is described
   by one dev_link_t structure (defined in ds.h).

   You may not want to use a linked list for this -- for example, the
   memory card driver uses an array of dev_link_t pointers, where minor
   device numbers are used to derive the corresponding array index.
*/
static dev_link_t *dev_list;

/*
   A dev_link_t structure has fields for most things that are needed
   to keep track of a socket, but there will usually be some device
   specific information that also needs to be kept track of.  The
   'priv' pointer in a dev_link_t structure can be used to point to
   a device-specific private data structure, like this.

   A driver needs to provide a dev_node_t structure for each device
   on a card.  In some cases, there is only one device per card (for
   example, ethernet cards, modems).  In other cases, there may be
   many actual or logical devices (SCSI adapters, memory cards with
   multiple partitions).  The dev_node_t structures need to be kept
   in a linked list starting at the 'dev' field of a dev_link_t
   structure.  We allocate them in the card's private data structure,
   because they generally can't be allocated dynamically.
*/

static const struct iw_handler_def	netwave_handler_def;

#define SIOCGIPSNAP	SIOCIWFIRSTPRIV	+ 1	/* Site Survey Snapshot */

#define MAX_ESA 10

typedef struct net_addr {
    u_char addr48[6];
} net_addr;

struct site_survey {
    u_short length;
    u_char  struct_revision;
    u_char  roaming_state;
	
    u_char  sp_existsFlag;
    u_char  sp_link_quality;
    u_char  sp_max_link_quality;
    u_char  linkQualityGoodFairBoundary;
    u_char  linkQualityFairPoorBoundary;
    u_char  sp_utilization;
    u_char  sp_goodness;
    u_char  sp_hotheadcount;
    u_char  roaming_condition;
	
    net_addr sp;
    u_char   numAPs;
    net_addr nearByAccessPoints[MAX_ESA];
};	
   
typedef struct netwave_private {
    dev_link_t link;
    spinlock_t	spinlock;	/* Serialize access to the hardware (SMP) */
    dev_node_t node;
    u_char     __iomem *ramBase;
    int        timeoutCounter;
    int        lastExec;
    struct timer_list      watchdog;	/* To avoid blocking state */
    struct site_survey     nss;
    struct net_device_stats stats;
    struct iw_statistics   iw_stats;    /* Wireless stats */
} netwave_private;

#ifdef NETWAVE_STATS
static struct net_device_stats *netwave_get_stats(struct net_device *dev);
#endif

/*
 * The Netwave card is little-endian, so won't work for big endian
 * systems.
 */
static inline unsigned short get_uint16(u_char __iomem *staddr) 
{
    return readw(staddr); /* Return only 16 bits */
}

static inline short get_int16(u_char __iomem * staddr)
{
    return readw(staddr);
}

/* 
 * Wait until the WOC (Write Operation Complete) bit in the 
 * ASR (Adapter Status Register) is asserted. 
 * This should have aborted if it takes too long time. 
 */
static inline void wait_WOC(unsigned int iobase)
{
    /* Spin lock */
    while ((inb(iobase + NETWAVE_REG_ASR) & 0x8) != 0x8) ; 
}

static void netwave_snapshot(netwave_private *priv, u_char __iomem *ramBase, 
			     kio_addr_t iobase) {
    u_short resultBuffer;

    /* if time since last snapshot is > 1 sec. (100 jiffies?)  then take 
     * new snapshot, else return cached data. This is the recommended rate.  
     */
    if ( jiffies - priv->lastExec > 100) { 
	/* Take site survey  snapshot */ 
	/*printk( KERN_DEBUG "Taking new snapshot. %ld\n", jiffies -
	  priv->lastExec); */
	wait_WOC(iobase); 
	writeb(NETWAVE_CMD_SSS, ramBase + NETWAVE_EREG_CB + 0); 
	writeb(NETWAVE_CMD_EOC, ramBase + NETWAVE_EREG_CB + 1); 
	wait_WOC(iobase); 

	/* Get result and copy to cach */ 
	resultBuffer = readw(ramBase + NETWAVE_EREG_CRBP); 
	copy_from_pc( &priv->nss, ramBase+resultBuffer, 
		      sizeof(struct site_survey)); 
    } 
}

/*
 * Function netwave_get_wireless_stats (dev)
 *
 *    Wireless extensions statistics
 *
 */
static struct iw_statistics *netwave_get_wireless_stats(struct net_device *dev)
{	
    unsigned long flags;
    kio_addr_t iobase = dev->base_addr;
    netwave_private *priv = netdev_priv(dev);
    u_char __iomem *ramBase = priv->ramBase;
    struct iw_statistics* wstats;
	
    wstats = &priv->iw_stats;

    spin_lock_irqsave(&priv->spinlock, flags);
	
    netwave_snapshot( priv, ramBase, iobase);

    wstats->status = priv->nss.roaming_state;
    wstats->qual.qual = readb( ramBase + NETWAVE_EREG_SPCQ); 
    wstats->qual.level = readb( ramBase + NETWAVE_EREG_ISPLQ);
    wstats->qual.noise = readb( ramBase + NETWAVE_EREG_SPU) & 0x3f;
    wstats->discard.nwid = 0L;
    wstats->discard.code = 0L;
    wstats->discard.misc = 0L;

    spin_unlock_irqrestore(&priv->spinlock, flags);
    
    return &priv->iw_stats;
}

/*
 * Function netwave_attach (void)
 *
 *     Creates an "instance" of the driver, allocating local data 
 *     structures for one device.  The device is registered with Card 
 *     Services.
 *
 *     The dev_link structure is initialized, but we don't actually
 *     configure the card at this point -- we wait until we receive a
 *     card insertion event.
 */
static dev_link_t *netwave_attach(void)
{
    client_reg_t client_reg;
    dev_link_t *link;
    struct net_device *dev;
    netwave_private *priv;
    int ret;
    
    DEBUG(0, "netwave_attach()\n");
    
    /* Initialize the dev_link_t structure */
    dev = alloc_etherdev(sizeof(netwave_private));
    if (!dev)
	return NULL;
    priv = netdev_priv(dev);
    link = &priv->link;
    link->priv = dev;

    /* The io structure describes IO port mapping */
    link->io.NumPorts1 = 16;
    link->io.Attributes1 = IO_DATA_PATH_WIDTH_16;
    /* link->io.NumPorts2 = 16; 
       link->io.Attributes2 = IO_DATA_PATH_WIDTH_16; */
    link->io.IOAddrLines = 5;
    
    /* Interrupt setup */
    link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
    link->irq.IRQInfo1 = IRQ_LEVEL_ID;
    link->irq.Handler = &netwave_interrupt;
    
    /* General socket configuration */
    link->conf.Attributes = CONF_ENABLE_IRQ;
    link->conf.Vcc = 50;
    link->conf.IntType = INT_MEMORY_AND_IO;
    link->conf.ConfigIndex = 1;
    link->conf.Present = PRESENT_OPTION;

    /* Netwave private struct init. link/dev/node already taken care of,
     * other stuff zero'd - Jean II */
    spin_lock_init(&priv->spinlock);

    /* Netwave specific entries in the device structure */
    SET_MODULE_OWNER(dev);
    dev->hard_start_xmit = &netwave_start_xmit;
    dev->get_stats  = &netwave_get_stats;
    dev->set_multicast_list = &set_multicast_list;
    /* wireless extensions */
    dev->wireless_handlers = (struct iw_handler_def *)&netwave_handler_def;

    dev->tx_timeout = &netwave_watchdog;
    dev->watchdog_timeo = TX_TIMEOUT;

    dev->open = &netwave_open;
    dev->stop = &netwave_close;
    link->irq.Instance = dev;
    
    /* Register with Card Services */
    link->next = dev_list;
    dev_list = link;
    client_reg.dev_info = &dev_info;
    client_reg.Version = 0x0210;
    client_reg.event_callback_args.client_data = link;
    ret = pcmcia_register_client(&link->handle, &client_reg);
    if (ret != 0) {
	cs_error(link->handle, RegisterClient, ret);
	netwave_detach(link);
	return NULL;
    }

    return link;
} /* netwave_attach */

/*
 * Function netwave_detach (link)
 *
 *    This deletes a driver "instance".  The device is de-registered
 *    with Card Services.  If it has been released, all local data
 *    structures are freed.  Otherwise, the structures will be freed
 *    when the device is released.
 */
static void netwave_detach(dev_link_t *link)
{
    struct net_device *dev = link->priv;
    dev_link_t **linkp;

    DEBUG(0, "netwave_detach(0x%p)\n", link);
  
    /*
	  If the device is currently configured and active, we won't
	  actually delete it yet.  Instead, it is marked so that when
	  the release() function is called, that will trigger a proper
	  detach().
	*/
    if (link->state & DEV_CONFIG)
	netwave_release(link);
	
    /* Break the link with Card Services */
    if (link->handle)
	pcmcia_deregister_client(link->handle);
    
    /* Locate device structure */
    for (linkp = &dev_list; *linkp; linkp = &(*linkp)->next)
	if (*linkp == link) break;
    if (*linkp == NULL)
      {
	DEBUG(1, "netwave_cs: detach fail, '%s' not in list\n",
	      link->dev->dev_name);
	return;
      }

    /* Unlink device structure, free pieces */
    *linkp = link->next;
    if (link->dev) 
	unregister_netdev(dev);
    free_netdev(dev);
    
} /* netwave_detach */

/*
 * Wireless Handler : get protocol name
 */
static int netwave_get_name(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	strcpy(wrqu->name, "Netwave");
	return 0;
}

/*
 * Wireless Handler : set Network ID
 */
static int netwave_set_nwid(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	unsigned long flags;
	kio_addr_t iobase = dev->base_addr;
	netwave_private *priv = netdev_priv(dev);
	u_char __iomem *ramBase = priv->ramBase;

	/* Disable interrupts & save flags */
	spin_lock_irqsave(&priv->spinlock, flags);

	if(!wrqu->nwid.disabled) {
	    domain = wrqu->nwid.value;
	    printk( KERN_DEBUG "Setting domain to 0x%x%02x\n", 
		    (domain >> 8) & 0x01, domain & 0xff);
	    wait_WOC(iobase);
	    writeb(NETWAVE_CMD_SMD, ramBase + NETWAVE_EREG_CB + 0);
	    writeb( domain & 0xff, ramBase + NETWAVE_EREG_CB + 1);
	    writeb((domain >>8 ) & 0x01,ramBase + NETWAVE_EREG_CB+2);
	    writeb(NETWAVE_CMD_EOC, ramBase + NETWAVE_EREG_CB + 3);
	}

	/* ReEnable interrupts & restore flags */
	spin_unlock_irqrestore(&priv->spinlock, flags);
    
	return 0;
}

/*
 * Wireless Handler : get Network ID
 */
static int netwave_get_nwid(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	wrqu->nwid.value = domain;
	wrqu->nwid.disabled = 0;
	wrqu->nwid.fixed = 1;
	return 0;
}

/*
 * Wireless Handler : set scramble key
 */
static int netwave_set_scramble(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu,
				char *key)
{
	unsigned long flags;
	kio_addr_t iobase = dev->base_addr;
	netwave_private *priv = netdev_priv(dev);
	u_char __iomem *ramBase = priv->ramBase;

	/* Disable interrupts & save flags */
	spin_lock_irqsave(&priv->spinlock, flags);

	scramble_key = (key[0] << 8) | key[1];
	wait_WOC(iobase);
	writeb(NETWAVE_CMD_SSK, ramBase + NETWAVE_EREG_CB + 0);
	writeb(scramble_key & 0xff, ramBase + NETWAVE_EREG_CB + 1);
	writeb((scramble_key>>8) & 0xff, ramBase + NETWAVE_EREG_CB + 2);
	writeb(NETWAVE_CMD_EOC, ramBase + NETWAVE_EREG_CB + 3);

	/* ReEnable interrupts & restore flags */
	spin_unlock_irqrestore(&priv->spinlock, flags);
    
	return 0;
}

/*
 * Wireless Handler : get scramble key
 */
static int netwave_get_scramble(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu,
				char *key)
{
	key[1] = scramble_key & 0xff;
	key[0] = (scramble_key>>8) & 0xff;
	wrqu->encoding.flags = IW_ENCODE_ENABLED;
	wrqu->encoding.length = 2;
	return 0;
}

/*
 * Wireless Handler : get mode
 */
static int netwave_get_mode(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	if(domain & 0x100)
		wrqu->mode = IW_MODE_INFRA;
	else
		wrqu->mode = IW_MODE_ADHOC;

	return 0;
}

/*
 * Wireless Handler : get range info
 */
static int netwave_get_range(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu,
			     char *extra)
{
	struct iw_range *range = (struct iw_range *) extra;
	int ret = 0;

	/* Set the length (very important for backward compatibility) */
	wrqu->data.length = sizeof(struct iw_range);

	/* Set all the info we don't care or don't know about to zero */
	memset(range, 0, sizeof(struct iw_range));

	/* Set the Wireless Extension versions */
	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 9;	/* Nothing for us in v10 and v11 */
		   
	/* Set information in the range struct */
	range->throughput = 450 * 1000;	/* don't argue on this ! */
	range->min_nwid = 0x0000;
	range->max_nwid = 0x01FF;

	range->num_channels = range->num_frequency = 0;
		   
	range->sensitivity = 0x3F;
	range->max_qual.qual = 255;
	range->max_qual.level = 255;
	range->max_qual.noise = 0;
		   
	range->num_bitrates = 1;
	range->bitrate[0] = 1000000;	/* 1 Mb/s */

	range->encoding_size[0] = 2;		/* 16 bits scrambling */
	range->num_encoding_sizes = 1;
	range->max_encoding_tokens = 1;	/* Only one key possible */

	return ret;
}

/*
 * Wireless Private Handler : get snapshot
 */
static int netwave_get_snap(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	unsigned long flags;
	kio_addr_t iobase = dev->base_addr;
	netwave_private *priv = netdev_priv(dev);
	u_char __iomem *ramBase = priv->ramBase;

	/* Disable interrupts & save flags */
	spin_lock_irqsave(&priv->spinlock, flags);

	/* Take snapshot of environment */
	netwave_snapshot( priv, ramBase, iobase);
	wrqu->data.length = priv->nss.length;
	memcpy(extra, (u_char *) &priv->nss, sizeof( struct site_survey));

	priv->lastExec = jiffies;

	/* ReEnable interrupts & restore flags */
	spin_unlock_irqrestore(&priv->spinlock, flags);
    
	return(0);
}

/*
 * Structures to export the Wireless Handlers
 *     This is the stuff that are treated the wireless extensions (iwconfig)
 */

static const struct iw_priv_args netwave_private_args[] = {
/*{ cmd,         set_args,                            get_args, name } */
  { SIOCGIPSNAP, 0, 
    IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | sizeof(struct site_survey), 
    "getsitesurvey" },
};

static const iw_handler		netwave_handler[] =
{
	NULL,				/* SIOCSIWNAME */
	netwave_get_name,		/* SIOCGIWNAME */
	netwave_set_nwid,		/* SIOCSIWNWID */
	netwave_get_nwid,		/* SIOCGIWNWID */
	NULL,				/* SIOCSIWFREQ */
	NULL,				/* SIOCGIWFREQ */
	NULL,				/* SIOCSIWMODE */
	netwave_get_mode,		/* SIOCGIWMODE */
	NULL,				/* SIOCSIWSENS */
	NULL,				/* SIOCGIWSENS */
	NULL,				/* SIOCSIWRANGE */
	netwave_get_range,		/* SIOCGIWRANGE */
	NULL,				/* SIOCSIWPRIV */
	NULL,				/* SIOCGIWPRIV */
	NULL,				/* SIOCSIWSTATS */
	NULL,				/* SIOCGIWSTATS */
	NULL,				/* SIOCSIWSPY */
	NULL,				/* SIOCGIWSPY */
	NULL,				/* -- hole -- */
	NULL,				/* -- hole -- */
	NULL,				/* SIOCSIWAP */
	NULL,				/* SIOCGIWAP */
	NULL,				/* -- hole -- */
	NULL,				/* SIOCGIWAPLIST */
	NULL,				/* -- hole -- */
	NULL,				/* -- hole -- */
	NULL,				/* SIOCSIWESSID */
	NULL,				/* SIOCGIWESSID */
	NULL,				/* SIOCSIWNICKN */
	NULL,				/* SIOCGIWNICKN */
	NULL,				/* -- hole -- */
	NULL,				/* -- hole -- */
	NULL,				/* SIOCSIWRATE */
	NULL,				/* SIOCGIWRATE */
	NULL,				/* SIOCSIWRTS */
	NULL,				/* SIOCGIWRTS */
	NULL,				/* SIOCSIWFRAG */
	NULL,				/* SIOCGIWFRAG */
	NULL,				/* SIOCSIWTXPOW */
	NULL,				/* SIOCGIWTXPOW */
	NULL,				/* SIOCSIWRETRY */
	NULL,				/* SIOCGIWRETRY */
	netwave_set_scramble,		/* SIOCSIWENCODE */
	netwave_get_scramble,		/* SIOCGIWENCODE */
};

static const iw_handler		netwave_private_handler[] =
{
	NULL,				/* SIOCIWFIRSTPRIV */
	netwave_get_snap,		/* SIOCIWFIRSTPRIV + 1 */
};

static const struct iw_handler_def	netwave_handler_def =
{
	.num_standard	= sizeof(netwave_handler)/sizeof(iw_handler),
	.num_private	= sizeof(netwave_private_handler)/sizeof(iw_handler),
	.num_private_args = sizeof(netwave_private_args)/sizeof(struct iw_priv_args),
	.standard	= (iw_handler *) netwave_handler,
	.private	= (iw_handler *) netwave_private_handler,
	.private_args	= (struct iw_priv_args *) netwave_private_args,
	.get_wireless_stats = netwave_get_wireless_stats,
};

/*
 * Function netwave_pcmcia_config (link)
 *
 *     netwave_pcmcia_config() is scheduled to run after a CARD_INSERTION 
 *     event is received, to configure the PCMCIA socket, and to make the
 *     device available to the system. 
 *
 */

#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

static void netwave_pcmcia_config(dev_link_t *link) {
    client_handle_t handle = link->handle;
    struct net_device *dev = link->priv;
    netwave_private *priv = netdev_priv(dev);
    tuple_t tuple;
    cisparse_t parse;
    int i, j, last_ret, last_fn;
    u_char buf[64];
    win_req_t req;
    memreq_t mem;
    u_char __iomem *ramBase = NULL;

    DEBUG(0, "netwave_pcmcia_config(0x%p)\n", link);

    /*
      This reads the card's CONFIG tuple to find its configuration
      registers.
    */
    tuple.Attributes = 0;
    tuple.TupleData = (cisdata_t *) buf;
    tuple.TupleDataMax = 64;
    tuple.TupleOffset = 0;
    tuple.DesiredTuple = CISTPL_CONFIG;
    CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
    CS_CHECK(GetTupleData, pcmcia_get_tuple_data(handle, &tuple));
    CS_CHECK(ParseTuple, pcmcia_parse_tuple(handle, &tuple, &parse));
    link->conf.ConfigBase = parse.config.base;
    link->conf.Present = parse.config.rmask[0];

    /* Configure card */
    link->state |= DEV_CONFIG;

    /*
     *  Try allocating IO ports.  This tries a few fixed addresses.
     *  If you want, you can also read the card's config table to
     *  pick addresses -- see the serial driver for an example.
     */
    for (i = j = 0x0; j < 0x400; j += 0x20) {
	link->io.BasePort1 = j ^ 0x300;
	i = pcmcia_request_io(link->handle, &link->io);
	if (i == CS_SUCCESS) break;
    }
    if (i != CS_SUCCESS) {
	cs_error(link->handle, RequestIO, i);
	goto failed;
    }

    /*
     *  Now allocate an interrupt line.  Note that this does not
     *  actually assign a handler to the interrupt.
     */
    CS_CHECK(RequestIRQ, pcmcia_request_irq(handle, &link->irq));

    /*
     *  This actually configures the PCMCIA socket -- setting up
     *  the I/O windows and the interrupt mapping.
     */
    CS_CHECK(RequestConfiguration, pcmcia_request_configuration(handle, &link->conf));

    /*
     *  Allocate a 32K memory window.  Note that the dev_link_t
     *  structure provides space for one window handle -- if your
     *  device needs several windows, you'll need to keep track of
     *  the handles in your private data structure, dev->priv.
     */
    DEBUG(1, "Setting mem speed of %d\n", mem_speed);

    req.Attributes = WIN_DATA_WIDTH_8|WIN_MEMORY_TYPE_CM|WIN_ENABLE;
    req.Base = 0; req.Size = 0x8000;
    req.AccessSpeed = mem_speed;
    CS_CHECK(RequestWindow, pcmcia_request_window(&link->handle, &req, &link->win));
    mem.CardOffset = 0x20000; mem.Page = 0; 
    CS_CHECK(MapMemPage, pcmcia_map_mem_page(link->win, &mem));

    /* Store base address of the common window frame */
    ramBase = ioremap(req.Base, 0x8000);
    priv->ramBase = ramBase;

    dev->irq = link->irq.AssignedIRQ;
    dev->base_addr = link->io.BasePort1;
    SET_NETDEV_DEV(dev, &handle_to_dev(handle));

    if (register_netdev(dev) != 0) {
	printk(KERN_DEBUG "netwave_cs: register_netdev() failed\n");
	goto failed;
    }

    strcpy(priv->node.dev_name, dev->name);
    link->dev = &priv->node;
    link->state &= ~DEV_CONFIG_PENDING;

    /* Reset card before reading physical address */
    netwave_doreset(dev->base_addr, ramBase);

    /* Read the ethernet address and fill in the Netwave registers. */
    for (i = 0; i < 6; i++) 
	dev->dev_addr[i] = readb(ramBase + NETWAVE_EREG_PA + i);

    printk(KERN_INFO "%s: Netwave: port %#3lx, irq %d, mem %lx id "
	   "%c%c, hw_addr ", dev->name, dev->base_addr, dev->irq,
	   (u_long) ramBase, (int) readb(ramBase+NETWAVE_EREG_NI),
	   (int) readb(ramBase+NETWAVE_EREG_NI+1));
    for (i = 0; i < 6; i++)
	printk("%02X%s", dev->dev_addr[i], ((i<5) ? ":" : "\n"));

    /* get revision words */
    printk(KERN_DEBUG "Netwave_reset: revision %04x %04x\n", 
	   get_uint16(ramBase + NETWAVE_EREG_ARW),
	   get_uint16(ramBase + NETWAVE_EREG_ARW+2));
    return;

cs_failed:
    cs_error(link->handle, last_fn, last_ret);
failed:
    netwave_release(link);
} /* netwave_pcmcia_config */

/*
 * Function netwave_release (arg)
 *
 *    After a card is removed, netwave_release() will unregister the net
 *    device, and release the PCMCIA configuration.  If the device is
 *    still open, this will be postponed until it is closed.
 */
static void netwave_release(dev_link_t *link)
{
    struct net_device *dev = link->priv;
    netwave_private *priv = netdev_priv(dev);

    DEBUG(0, "netwave_release(0x%p)\n", link);

    /* Don't bother checking to see if these succeed or not */
    if (link->win) {
	iounmap(priv->ramBase);
	pcmcia_release_window(link->win);
    }
    pcmcia_release_configuration(link->handle);
    pcmcia_release_io(link->handle, &link->io);
    pcmcia_release_irq(link->handle, &link->irq);

    link->state &= ~DEV_CONFIG;
}

/*
 * Function netwave_event (event, priority, args)
 *
 *    The card status event handler.  Mostly, this schedules other
 *    stuff to run after an event is received.  A CARD_REMOVAL event
 *    also sets some flags to discourage the net drivers from trying
 *    to talk to the card any more.
 *
 *    When a CARD_REMOVAL event is received, we immediately set a flag
 *    to block future accesses to this device.  All the functions that
 *    actually access the device should check this flag to make sure
 *    the card is still present.
 *
 */
static int netwave_event(event_t event, int priority,
			 event_callback_args_t *args)
{
    dev_link_t *link = args->client_data;
    struct net_device *dev = link->priv;
	
    DEBUG(1, "netwave_event(0x%06x)\n", event);
  
    switch (event) {
    case CS_EVENT_REGISTRATION_COMPLETE:
	DEBUG(0, "netwave_cs: registration complete\n");
	break;

    case CS_EVENT_CARD_REMOVAL:
	link->state &= ~DEV_PRESENT;
	if (link->state & DEV_CONFIG) {
	    netif_device_detach(dev);
	    netwave_release(link);
	}
	break;
    case CS_EVENT_CARD_INSERTION:
	link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
	netwave_pcmcia_config( link);
	break;
    case CS_EVENT_PM_SUSPEND:
	link->state |= DEV_SUSPEND;
	/* Fall through... */
    case CS_EVENT_RESET_PHYSICAL:
	if (link->state & DEV_CONFIG) {
	    if (link->open)
		netif_device_detach(dev);
	    pcmcia_release_configuration(link->handle);
	}
	break;
    case CS_EVENT_PM_RESUME:
	link->state &= ~DEV_SUSPEND;
	/* Fall through... */
    case CS_EVENT_CARD_RESET:
	if (link->state & DEV_CONFIG) {
	    pcmcia_request_configuration(link->handle, &link->conf);
	    if (link->open) {
		netwave_reset(dev);
		netif_device_attach(dev);
	    }
	}
	break;
    }
    return 0;
} /* netwave_event */

/*
 * Function netwave_doreset (ioBase, ramBase)
 *
 *    Proper hardware reset of the card.
 */
static void netwave_doreset(kio_addr_t ioBase, u_char __iomem *ramBase)
{
    /* Reset card */
    wait_WOC(ioBase);
    outb(0x80, ioBase + NETWAVE_REG_PMR);
    writeb(0x08, ramBase + NETWAVE_EREG_ASCC); /* Bit 3 is WOC */
    outb(0x0, ioBase + NETWAVE_REG_PMR); /* release reset */
}

/*
 * Function netwave_reset (dev)
 *
 *    Reset and restore all of the netwave registers 
 */
static void netwave_reset(struct net_device *dev) {
    /* u_char state; */
    netwave_private *priv = netdev_priv(dev);
    u_char __iomem *ramBase = priv->ramBase;
    kio_addr_t iobase = dev->base_addr;

    DEBUG(0, "netwave_reset: Done with hardware reset\n");

    priv->timeoutCounter = 0;

    /* Reset card */
    netwave_doreset(iobase, ramBase);
    printk(KERN_DEBUG "netwave_reset: Done with hardware reset\n");
	
    /* Write a NOP to check the card */
    wait_WOC(iobase);
    writeb(NETWAVE_CMD_NOP, ramBase + NETWAVE_EREG_CB + 0);
    writeb(NETWAVE_CMD_EOC, ramBase + NETWAVE_EREG_CB + 1);
	
    /* Set receive conf */
    wait_WOC(iobase);
    writeb(NETWAVE_CMD_SRC, ramBase + NETWAVE_EREG_CB + 0);
    writeb(rxConfRxEna + rxConfBcast, ramBase + NETWAVE_EREG_CB + 1);
    writeb(NETWAVE_CMD_EOC, ramBase + NETWAVE_EREG_CB + 2);
    
    /* Set transmit conf */
    wait_WOC(iobase);
    writeb(NETWAVE_CMD_STC, ramBase + NETWAVE_EREG_CB + 0);
    writeb(txConfTxEna, ramBase + NETWAVE_EREG_CB + 1);
    writeb(NETWAVE_CMD_EOC, ramBase + NETWAVE_EREG_CB + 2);
    
    /* Now set the MU Domain */
    printk(KERN_DEBUG "Setting domain to 0x%x%02x\n", (domain >> 8) & 0x01, domain & 0xff);
    wait_WOC(iobase);
    writeb(NETWAVE_CMD_SMD, ramBase + NETWAVE_EREG_CB + 0);
    writeb(domain & 0xff, ramBase + NETWAVE_EREG_CB + 1);
    writeb((domain>>8) & 0x01, ramBase + NETWAVE_EREG_CB + 2);
    writeb(NETWAVE_CMD_EOC, ramBase + NETWAVE_EREG_CB + 3);
	
    /* Set scramble key */
    printk(KERN_DEBUG "Setting scramble key to 0x%x\n", scramble_key);
    wait_WOC(iobase);
    writeb(NETWAVE_CMD_SSK, ramBase + NETWAVE_EREG_CB + 0);
    writeb(scramble_key & 0xff, ramBase + NETWAVE_EREG_CB + 1);
    writeb((scramble_key>>8) & 0xff, ramBase + NETWAVE_EREG_CB + 2);
    writeb(NETWAVE_CMD_EOC, ramBase + NETWAVE_EREG_CB + 3);

    /* Enable interrupts, bit 4 high to keep unused
     * source from interrupting us, bit 2 high to 
     * set interrupt enable, 567 to enable TxDN, 
     * RxErr and RxRdy
     */
    wait_WOC(iobase);
    outb(imrConfIENA+imrConfRFU1, iobase + NETWAVE_REG_IMR);

    /* Hent 4 bytes fra 0x170. Skal vaere 0a,29,88,36
     * waitWOC
     * skriv 80 til d000:3688
     * sjekk om det ble 80
     */
    
    /* Enable Receiver */
    wait_WOC(iobase);
    writeb(NETWAVE_CMD_ER, ramBase + NETWAVE_EREG_CB + 0);
    writeb(NETWAVE_CMD_EOC, ramBase + NETWAVE_EREG_CB + 1);
	
    /* Set the IENA bit in COR */
    wait_WOC(iobase);
    outb(corConfIENA + corConfLVLREQ, iobase + NETWAVE_REG_COR);
}

/*
 * Function netwave_hw_xmit (data, len, dev)    
 */
static int netwave_hw_xmit(unsigned char* data, int len,
			   struct net_device* dev) {
    unsigned long flags;
    unsigned int TxFreeList,
	         curBuff,
	         MaxData, 
                 DataOffset;
    int tmpcount; 
	
    netwave_private *priv = netdev_priv(dev);
    u_char __iomem * ramBase = priv->ramBase;
    kio_addr_t iobase = dev->base_addr;

    /* Disable interrupts & save flags */
    spin_lock_irqsave(&priv->spinlock, flags);

    /* Check if there are transmit buffers available */
    wait_WOC(iobase);
    if ((inb(iobase+NETWAVE_REG_ASR) & NETWAVE_ASR_TXBA) == 0) {
	/* No buffers available */
	printk(KERN_DEBUG "netwave_hw_xmit: %s - no xmit buffers available.\n",
	       dev->name);
	spin_unlock_irqrestore(&priv->spinlock, flags);
	return 1;
    }

    priv->stats.tx_bytes += len;

    DEBUG(3, "Transmitting with SPCQ %x SPU %x LIF %x ISPLQ %x\n",
	  readb(ramBase + NETWAVE_EREG_SPCQ),
	  readb(ramBase + NETWAVE_EREG_SPU),
	  readb(ramBase + NETWAVE_EREG_LIF),
	  readb(ramBase + NETWAVE_EREG_ISPLQ));

    /* Now try to insert it into the adapters free memory */
    wait_WOC(iobase);
    TxFreeList = get_uint16(ramBase + NETWAVE_EREG_TDP);
    MaxData    = get_uint16(ramBase + NETWAVE_EREG_TDP+2);
    DataOffset = get_uint16(ramBase + NETWAVE_EREG_TDP+4);
	
    DEBUG(3, "TxFreeList %x, MaxData %x, DataOffset %x\n",
	  TxFreeList, MaxData, DataOffset);

    /* Copy packet to the adapter fragment buffers */
    curBuff = TxFreeList; 
    tmpcount = 0; 
    while (tmpcount < len) {
	int tmplen = len - tmpcount; 
	copy_to_pc(ramBase + curBuff + DataOffset, data + tmpcount, 
		   (tmplen < MaxData) ? tmplen : MaxData);
	tmpcount += MaxData;
			
	/* Advance to next buffer */
	curBuff = get_uint16(ramBase + curBuff);
    }
    
    /* Now issue transmit list */
    wait_WOC(iobase);
    writeb(NETWAVE_CMD_TL, ramBase + NETWAVE_EREG_CB + 0);
    writeb(len & 0xff, ramBase + NETWAVE_EREG_CB + 1);
    writeb((len>>8) & 0xff, ramBase + NETWAVE_EREG_CB + 2);
    writeb(NETWAVE_CMD_EOC, ramBase + NETWAVE_EREG_CB + 3);

    spin_unlock_irqrestore(&priv->spinlock, flags);
    return 0;
}

static int netwave_start_xmit(struct sk_buff *skb, struct net_device *dev) {
	/* This flag indicate that the hardware can't perform a transmission.
	 * Theoritically, NET3 check it before sending a packet to the driver,
	 * but in fact it never do that and pool continuously.
	 * As the watchdog will abort too long transmissions, we are quite safe...
	 */

    netif_stop_queue(dev);

    {
	short length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
	unsigned char* buf = skb->data;
	
	if (netwave_hw_xmit( buf, length, dev) == 1) {
	    /* Some error, let's make them call us another time? */
	    netif_start_queue(dev);
	}
	dev->trans_start = jiffies;
    }
    dev_kfree_skb(skb);
    
    return 0;
} /* netwave_start_xmit */

/*
 * Function netwave_interrupt (irq, dev_id, regs)
 *
 *    This function is the interrupt handler for the Netwave card. This
 *    routine will be called whenever: 
 *	  1. A packet is received.
 *	  2. A packet has successfully been transferred and the unit is
 *	     ready to transmit another packet.
 *	  3. A command has completed execution.
 */
static irqreturn_t netwave_interrupt(int irq, void* dev_id, struct pt_regs *regs)
{
    kio_addr_t iobase;
    u_char __iomem *ramBase;
    struct net_device *dev = (struct net_device *)dev_id;
    struct netwave_private *priv = netdev_priv(dev);
    dev_link_t *link = &priv->link;
    int i;
    
    if (!netif_device_present(dev))
	return IRQ_NONE;
    
    iobase = dev->base_addr;
    ramBase = priv->ramBase;
	
    /* Now find what caused the interrupt, check while interrupts ready */
    for (i = 0; i < 10; i++) {
	u_char status;
		
	wait_WOC(iobase);	
	if (!(inb(iobase+NETWAVE_REG_CCSR) & 0x02))
	    break; /* None of the interrupt sources asserted (normal exit) */
	
        status = inb(iobase + NETWAVE_REG_ASR);
		
	if (!DEV_OK(link)) {
	    DEBUG(1, "netwave_interrupt: Interrupt with status 0x%x "
		  "from removed or suspended card!\n", status);
	    break;
	}
		
	/* RxRdy */
	if (status & 0x80) {
	    netwave_rx(dev);
	    /* wait_WOC(iobase); */
	    /* RxRdy cannot be reset directly by the host */
	}
	/* RxErr */
	if (status & 0x40) {
	    u_char rser;
			
	    rser = readb(ramBase + NETWAVE_EREG_RSER);			
	    
	    if (rser & 0x04) {
		++priv->stats.rx_dropped; 
		++priv->stats.rx_crc_errors;
	    }
	    if (rser & 0x02)
		++priv->stats.rx_frame_errors;
			
	    /* Clear the RxErr bit in RSER. RSER+4 is the
	     * write part. Also clear the RxCRC (0x04) and 
	     * RxBig (0x02) bits if present */
	    wait_WOC(iobase);
	    writeb(0x40 | (rser & 0x06), ramBase + NETWAVE_EREG_RSER + 4);

	    /* Write bit 6 high to ASCC to clear RxErr in ASR,
	     * WOC must be set first! 
	     */
	    wait_WOC(iobase);
	    writeb(0x40, ramBase + NETWAVE_EREG_ASCC);

	    /* Remember to count up priv->stats on error packets */
	    ++priv->stats.rx_errors;
	}
	/* TxDN */
	if (status & 0x20) {
	    int txStatus;

	    txStatus = readb(ramBase + NETWAVE_EREG_TSER);
	    DEBUG(3, "Transmit done. TSER = %x id %x\n", 
		  txStatus, readb(ramBase + NETWAVE_EREG_TSER + 1));
	    
	    if (txStatus & 0x20) {
		/* Transmitting was okay, clear bits */
		wait_WOC(iobase);
		writeb(0x2f, ramBase + NETWAVE_EREG_TSER + 4);
		++priv->stats.tx_packets;
	    }
			
	    if (txStatus & 0xd0) {
		if (txStatus & 0x80) {
		    ++priv->stats.collisions; /* Because of /proc/net/dev*/
		    /* ++priv->stats.tx_aborted_errors; */
		    /* printk("Collision. %ld\n", jiffies - dev->trans_start); */
		}
		if (txStatus & 0x40) 
		    ++priv->stats.tx_carrier_errors;
		/* 0x80 TxGU Transmit giveup - nine times and no luck
		 * 0x40 TxNOAP No access point. Discarded packet.
		 * 0x10 TxErr Transmit error. Always set when 
		 *      TxGU and TxNOAP is set. (Those are the only ones
		 *      to set TxErr).
		 */
		DEBUG(3, "netwave_interrupt: TxDN with error status %x\n", 
		      txStatus);
		
		/* Clear out TxGU, TxNOAP, TxErr and TxTrys */
		wait_WOC(iobase);
		writeb(0xdf & txStatus, ramBase+NETWAVE_EREG_TSER+4);
		++priv->stats.tx_errors;
	    }
	    DEBUG(3, "New status is TSER %x ASR %x\n",
		  readb(ramBase + NETWAVE_EREG_TSER),
		  inb(iobase + NETWAVE_REG_ASR));

	    netif_wake_queue(dev);
	}
	/* TxBA, this would trigger on all error packets received */
	/* if (status & 0x01) {
	   DEBUG(4, "Transmit buffers available, %x\n", status);
	   }
	   */
    }
    /* Handled if we looped at least one time - Jean II */
    return IRQ_RETVAL(i);
} /* netwave_interrupt */

/*
 * Function netwave_watchdog (a)
 *
 *    Watchdog : when we start a transmission, we set a timer in the
 *    kernel.  If the transmission complete, this timer is disabled. If
 *    it expire, we reset the card.
 *
 */
static void netwave_watchdog(struct net_device *dev) {

    DEBUG(1, "%s: netwave_watchdog: watchdog timer expired\n", dev->name);
    netwave_reset(dev);
    dev->trans_start = jiffies;
    netif_wake_queue(dev);
} /* netwave_watchdog */

static struct net_device_stats *netwave_get_stats(struct net_device *dev) {
    netwave_private *priv = netdev_priv(dev);

    update_stats(dev);

    DEBUG(2, "netwave: SPCQ %x SPU %x LIF %x ISPLQ %x MHS %x rxtx %x"
	  " %x tx %x %x %x %x\n", 
	  readb(priv->ramBase + NETWAVE_EREG_SPCQ),
	  readb(priv->ramBase + NETWAVE_EREG_SPU),
	  readb(priv->ramBase + NETWAVE_EREG_LIF),
	  readb(priv->ramBase + NETWAVE_EREG_ISPLQ),
	  readb(priv->ramBase + NETWAVE_EREG_MHS),
	  readb(priv->ramBase + NETWAVE_EREG_EC + 0xe),
	  readb(priv->ramBase + NETWAVE_EREG_EC + 0xf),
	  readb(priv->ramBase + NETWAVE_EREG_EC + 0x18),
	  readb(priv->ramBase + NETWAVE_EREG_EC + 0x19),
	  readb(priv->ramBase + NETWAVE_EREG_EC + 0x1a),
	  readb(priv->ramBase + NETWAVE_EREG_EC + 0x1b));

    return &priv->stats;
}

static void update_stats(struct net_device *dev) {
    //unsigned long flags;
/*     netwave_private *priv = netdev_priv(dev); */

    //spin_lock_irqsave(&priv->spinlock, flags);

/*    priv->stats.rx_packets = readb(priv->ramBase + 0x18e); 
    priv->stats.tx_packets = readb(priv->ramBase + 0x18f); */

    //spin_unlock_irqrestore(&priv->spinlock, flags);
}

static int netwave_rx(struct net_device *dev)
{
    netwave_private *priv = netdev_priv(dev);
    u_char __iomem *ramBase = priv->ramBase;
    kio_addr_t iobase = dev->base_addr;
    u_char rxStatus;
    struct sk_buff *skb = NULL;
    unsigned int curBuffer,
		rcvList;
    int rcvLen;
    int tmpcount = 0;
    int dataCount, dataOffset;
    int i;
    u_char *ptr;
	
    DEBUG(3, "xinw_rx: Receiving ... \n");

    /* Receive max 10 packets for now. */
    for (i = 0; i < 10; i++) {
	/* Any packets? */
	wait_WOC(iobase);
	rxStatus = readb(ramBase + NETWAVE_EREG_RSER);		
	if ( !( rxStatus & 0x80)) /* No more packets */
	    break;
		
	/* Check if multicast/broadcast or other */
	/* multicast = (rxStatus & 0x20);  */
		
	/* The receive list pointer and length of the packet */
	wait_WOC(iobase);
	rcvLen  = get_int16( ramBase + NETWAVE_EREG_RDP);
	rcvList = get_uint16( ramBase + NETWAVE_EREG_RDP + 2);
		
	if (rcvLen < 0) {
	    printk(KERN_DEBUG "netwave_rx: Receive packet with len %d\n", 
		   rcvLen);
	    return 0;
	}
		
	skb = dev_alloc_skb(rcvLen+5);
	if (skb == NULL) {
	    DEBUG(1, "netwave_rx: Could not allocate an sk_buff of "
		  "length %d\n", rcvLen);
	    ++priv->stats.rx_dropped; 
	    /* Tell the adapter to skip the packet */
	    wait_WOC(iobase);
	    writeb(NETWAVE_CMD_SRP, ramBase + NETWAVE_EREG_CB + 0);
	    writeb(NETWAVE_CMD_EOC, ramBase + NETWAVE_EREG_CB + 1);
	    return 0;
	}

	skb_reserve( skb, 2);  /* Align IP on 16 byte */
	skb_put( skb, rcvLen);
	skb->dev = dev;

	/* Copy packet fragments to the skb data area */
	ptr = (u_char*) skb->data;
	curBuffer = rcvList;
	tmpcount = 0; 
	while ( tmpcount < rcvLen) {
	    /* Get length and offset of current buffer */
	    dataCount  = get_uint16( ramBase+curBuffer+2);
	    dataOffset = get_uint16( ramBase+curBuffer+4);
		
	    copy_from_pc( ptr + tmpcount,
			  ramBase+curBuffer+dataOffset, dataCount);

	    tmpcount += dataCount;
		
	    /* Point to next buffer */
	    curBuffer = get_uint16(ramBase + curBuffer);
	}
	
	skb->protocol = eth_type_trans(skb,dev);
	/* Queue packet for network layer */
	netif_rx(skb);

	dev->last_rx = jiffies;
	priv->stats.rx_packets++;
	priv->stats.rx_bytes += rcvLen;

	/* Got the packet, tell the adapter to skip it */
	wait_WOC(iobase);
	writeb(NETWAVE_CMD_SRP, ramBase + NETWAVE_EREG_CB + 0);
	writeb(NETWAVE_CMD_EOC, ramBase + NETWAVE_EREG_CB + 1);
	DEBUG(3, "Packet reception ok\n");
    }
    return 0;
}

static int netwave_open(struct net_device *dev) {
    netwave_private *priv = netdev_priv(dev);
    dev_link_t *link = &priv->link;

    DEBUG(1, "netwave_open: starting.\n");
    
    if (!DEV_OK(link))
	return -ENODEV;

    link->open++;

    netif_start_queue(dev);
    netwave_reset(dev);
	
    return 0;
}

static int netwave_close(struct net_device *dev) {
    netwave_private *priv = netdev_priv(dev);
    dev_link_t *link = &priv->link;

    DEBUG(1, "netwave_close: finishing.\n");

    link->open--;
    netif_stop_queue(dev);

    return 0;
}

static struct pcmcia_device_id netwave_ids[] = {
	PCMCIA_DEVICE_PROD_ID12("Xircom", "CreditCard Netwave", 0x2e3ee845, 0x54e28a28),
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, netwave_ids);

static struct pcmcia_driver netwave_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "netwave_cs",
	},
	.attach		= netwave_attach,
	.event		= netwave_event,
	.detach		= netwave_detach,
	.id_table       = netwave_ids,
};

static int __init init_netwave_cs(void)
{
	return pcmcia_register_driver(&netwave_driver);
}

static void __exit exit_netwave_cs(void)
{
	pcmcia_unregister_driver(&netwave_driver);
	BUG_ON(dev_list != NULL);
}

module_init(init_netwave_cs);
module_exit(exit_netwave_cs);

/* Set or clear the multicast filter for this adaptor.
   num_addrs == -1	Promiscuous mode, receive all packets
   num_addrs == 0	Normal mode, clear multicast list
   num_addrs > 0	Multicast mode, receive normal and MC packets, and do
   best-effort filtering.
 */
static void set_multicast_list(struct net_device *dev)
{
    kio_addr_t iobase = dev->base_addr;
    netwave_private *priv = netdev_priv(dev);
    u_char __iomem * ramBase = priv->ramBase;
    u_char  rcvMode = 0;
   
#ifdef PCMCIA_DEBUG
    if (pc_debug > 2) {
	static int old;
	if (old != dev->mc_count) {
	    old = dev->mc_count;
	    DEBUG(0, "%s: setting Rx mode to %d addresses.\n",
		  dev->name, dev->mc_count);
	}
    }
#endif
	
    if (dev->mc_count || (dev->flags & IFF_ALLMULTI)) {
	/* Multicast Mode */
	rcvMode = rxConfRxEna + rxConfAMP + rxConfBcast;
    } else if (dev->flags & IFF_PROMISC) {
	/* Promiscous mode */
	rcvMode = rxConfRxEna + rxConfPro + rxConfAMP + rxConfBcast;
    } else {
	/* Normal mode */
	rcvMode = rxConfRxEna + rxConfBcast;
    }
	
    /* printk("netwave set_multicast_list: rcvMode to %x\n", rcvMode);*/
    /* Now set receive mode */
    wait_WOC(iobase);
    writeb(NETWAVE_CMD_SRC, ramBase + NETWAVE_EREG_CB + 0);
    writeb(rcvMode, ramBase + NETWAVE_EREG_CB + 1);
    writeb(NETWAVE_CMD_EOC, ramBase + NETWAVE_EREG_CB + 2);
}
MODULE_LICENSE("GPL");
