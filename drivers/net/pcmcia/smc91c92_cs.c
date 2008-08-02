/*======================================================================

    A PCMCIA ethernet driver for SMC91c92-based cards.

    This driver supports Megahertz PCMCIA ethernet cards; and
    Megahertz, Motorola, Ositech, and Psion Dacom ethernet/modem
    multifunction cards.

    Copyright (C) 1999 David A. Hinds -- dahinds@users.sourceforge.net

    smc91c92_cs.c 1.122 2002/10/25 06:26:39

    This driver contains code written by Donald Becker
    (becker@scyld.com), Rowan Hughes (x-csrdh@jcu.edu.au),
    David Hinds (dahinds@users.sourceforge.net), and Erik Stahlman
    (erik@vt.edu).  Donald wrote the SMC 91c92 code using parts of
    Erik's SMC 91c94 driver.  Rowan wrote a similar driver, and I've
    incorporated some parts of his driver here.  I (Dave) wrote most
    of the PCMCIA glue code, and the Ositech support code.  Kelly
    Stephens (kstephen@holli.com) added support for the Motorola
    Mariner, with help from Allen Brost.

    This software may be used and distributed according to the terms of
    the GNU General Public License, incorporated herein by reference.

======================================================================*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/jiffies.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>
#include <pcmcia/ss.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/uaccess.h>

/* Ositech Seven of Diamonds firmware */
#include "ositech.h"

/*====================================================================*/

static const char *if_names[] = { "auto", "10baseT", "10base2"};

/* Module parameters */

MODULE_DESCRIPTION("SMC 91c92 series PCMCIA ethernet driver");
MODULE_LICENSE("GPL");

#define INT_MODULE_PARM(n, v) static int n = v; module_param(n, int, 0)

/*
  Transceiver/media type.
   0 = auto
   1 = 10baseT (and autoselect if #define AUTOSELECT),
   2 = AUI/10base2,
*/
INT_MODULE_PARM(if_port, 0);

#ifdef PCMCIA_DEBUG
INT_MODULE_PARM(pc_debug, PCMCIA_DEBUG);
static const char *version =
"smc91c92_cs.c 1.123 2006/11/09 Donald Becker, becker@scyld.com.\n";
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
#else
#define DEBUG(n, args...)
#endif

#define DRV_NAME	"smc91c92_cs"
#define DRV_VERSION	"1.123"

/*====================================================================*/

/* Operational parameter that usually are not changed. */

/* Time in jiffies before concluding Tx hung */
#define TX_TIMEOUT		((400*HZ)/1000)

/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
#define INTR_WORK		4

/* Times to check the check the chip before concluding that it doesn't
   currently have room for another Tx packet. */
#define MEMORY_WAIT_TIME       	8

struct smc_private {
	struct pcmcia_device	*p_dev;
    spinlock_t			lock;
    u_short			manfid;
    u_short			cardid;
    struct net_device_stats	stats;
    dev_node_t			node;
    struct sk_buff		*saved_skb;
    int				packets_waiting;
    void			__iomem *base;
    u_short			cfg;
    struct timer_list		media;
    int				watchdog, tx_err;
    u_short			media_status;
    u_short			fast_poll;
    u_short			link_status;
    struct mii_if_info		mii_if;
    int				duplex;
    int				rx_ovrn;
};

struct smc_cfg_mem {
    tuple_t tuple;
    cisparse_t parse;
    u_char buf[255];
};

/* Special definitions for Megahertz multifunction cards */
#define MEGAHERTZ_ISR		0x0380

/* Special function registers for Motorola Mariner */
#define MOT_LAN			0x0000
#define MOT_UART		0x0020
#define MOT_EEPROM		0x20

#define MOT_NORMAL \
(COR_LEVEL_REQ | COR_FUNC_ENA | COR_ADDR_DECODE | COR_IREQ_ENA)

/* Special function registers for Ositech cards */
#define OSITECH_AUI_CTL		0x0c
#define OSITECH_PWRDOWN		0x0d
#define OSITECH_RESET		0x0e
#define OSITECH_ISR		0x0f
#define OSITECH_AUI_PWR		0x0c
#define OSITECH_RESET_ISR	0x0e

#define OSI_AUI_PWR		0x40
#define OSI_LAN_PWRDOWN		0x02
#define OSI_MODEM_PWRDOWN	0x01
#define OSI_LAN_RESET		0x02
#define OSI_MODEM_RESET		0x01

/* Symbolic constants for the SMC91c9* series chips, from Erik Stahlman. */
#define	BANK_SELECT		14		/* Window select register. */
#define SMC_SELECT_BANK(x)  { outw(x, ioaddr + BANK_SELECT); }

/* Bank 0 registers. */
#define	TCR 		0	/* transmit control register */
#define	 TCR_CLEAR	0	/* do NOTHING */
#define  TCR_ENABLE	0x0001	/* if this is 1, we can transmit */
#define	 TCR_PAD_EN	0x0080	/* pads short packets to 64 bytes */
#define  TCR_MONCSN	0x0400  /* Monitor Carrier. */
#define  TCR_FDUPLX	0x0800  /* Full duplex mode. */
#define	 TCR_NORMAL TCR_ENABLE | TCR_PAD_EN

#define EPH		2	/* Ethernet Protocol Handler report. */
#define  EPH_TX_SUC	0x0001
#define  EPH_SNGLCOL	0x0002
#define  EPH_MULCOL	0x0004
#define  EPH_LTX_MULT	0x0008
#define  EPH_16COL	0x0010
#define  EPH_SQET	0x0020
#define  EPH_LTX_BRD	0x0040
#define  EPH_TX_DEFR	0x0080
#define  EPH_LAT_COL	0x0200
#define  EPH_LOST_CAR	0x0400
#define  EPH_EXC_DEF	0x0800
#define  EPH_CTR_ROL	0x1000
#define  EPH_RX_OVRN	0x2000
#define  EPH_LINK_OK	0x4000
#define  EPH_TX_UNRN	0x8000
#define MEMINFO		8	/* Memory Information Register */
#define MEMCFG		10	/* Memory Configuration Register */

/* Bank 1 registers. */
#define CONFIG			0
#define  CFG_MII_SELECT		0x8000	/* 91C100 only */
#define  CFG_NO_WAIT		0x1000
#define  CFG_FULL_STEP		0x0400
#define  CFG_SET_SQLCH		0x0200
#define  CFG_AUI_SELECT	 	0x0100
#define  CFG_16BIT		0x0080
#define  CFG_DIS_LINK		0x0040
#define  CFG_STATIC		0x0030
#define  CFG_IRQ_SEL_1		0x0004
#define  CFG_IRQ_SEL_0		0x0002
#define BASE_ADDR		2
#define	ADDR0			4
#define	GENERAL			10
#define	CONTROL			12
#define  CTL_STORE		0x0001
#define  CTL_RELOAD		0x0002
#define  CTL_EE_SELECT		0x0004
#define  CTL_TE_ENABLE		0x0020
#define  CTL_CR_ENABLE		0x0040
#define  CTL_LE_ENABLE		0x0080
#define  CTL_AUTO_RELEASE	0x0800
#define	 CTL_POWERDOWN		0x2000

/* Bank 2 registers. */
#define MMU_CMD		0
#define	 MC_ALLOC	0x20  	/* or with number of 256 byte packets */
#define	 MC_RESET	0x40
#define  MC_RELEASE  	0x80  	/* remove and release the current rx packet */
#define  MC_FREEPKT  	0xA0  	/* Release packet in PNR register */
#define  MC_ENQUEUE	0xC0 	/* Enqueue the packet for transmit */
#define	PNR_ARR		2
#define FIFO_PORTS	4
#define  FP_RXEMPTY	0x8000
#define	POINTER		6
#define  PTR_AUTO_INC	0x0040
#define  PTR_READ	0x2000
#define	 PTR_AUTOINC 	0x4000
#define	 PTR_RCV	0x8000
#define	DATA_1		8
#define	INTERRUPT	12
#define  IM_RCV_INT		0x1
#define	 IM_TX_INT		0x2
#define	 IM_TX_EMPTY_INT	0x4
#define	 IM_ALLOC_INT		0x8
#define	 IM_RX_OVRN_INT		0x10
#define	 IM_EPH_INT		0x20

#define	RCR		4
enum RxCfg { RxAllMulti = 0x0004, RxPromisc = 0x0002,
	     RxEnable = 0x0100, RxStripCRC = 0x0200};
#define  RCR_SOFTRESET	0x8000 	/* resets the chip */
#define	 RCR_STRIP_CRC	0x200	/* strips CRC */
#define  RCR_ENABLE	0x100	/* IFF this is set, we can receive packets */
#define  RCR_ALMUL	0x4 	/* receive all multicast packets */
#define	 RCR_PROMISC	0x2	/* enable promiscuous mode */

/* the normal settings for the RCR register : */
#define	 RCR_NORMAL	(RCR_STRIP_CRC | RCR_ENABLE)
#define  RCR_CLEAR	0x0		/* set it to a base state */
#define	COUNTER		6

/* BANK 3 -- not the same values as in smc9194! */
#define	MULTICAST0	0
#define	MULTICAST2	2
#define	MULTICAST4	4
#define	MULTICAST6	6
#define MGMT    	8
#define REVISION	0x0a

/* Transmit status bits. */
#define TS_SUCCESS 0x0001
#define TS_16COL   0x0010
#define TS_LATCOL  0x0200
#define TS_LOSTCAR 0x0400

/* Receive status bits. */
#define RS_ALGNERR	0x8000
#define RS_BADCRC	0x2000
#define RS_ODDFRAME	0x1000
#define RS_TOOLONG	0x0800
#define RS_TOOSHORT	0x0400
#define RS_MULTICAST	0x0001
#define RS_ERRORS	(RS_ALGNERR | RS_BADCRC | RS_TOOLONG | RS_TOOSHORT)

#define set_bits(v, p) outw(inw(p)|(v), (p))
#define mask_bits(v, p) outw(inw(p)&(v), (p))

/*====================================================================*/

static void smc91c92_detach(struct pcmcia_device *p_dev);
static int smc91c92_config(struct pcmcia_device *link);
static void smc91c92_release(struct pcmcia_device *link);

static int smc_open(struct net_device *dev);
static int smc_close(struct net_device *dev);
static int smc_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static void smc_tx_timeout(struct net_device *dev);
static int smc_start_xmit(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t smc_interrupt(int irq, void *dev_id);
static void smc_rx(struct net_device *dev);
static struct net_device_stats *smc_get_stats(struct net_device *dev);
static void set_rx_mode(struct net_device *dev);
static int s9k_config(struct net_device *dev, struct ifmap *map);
static void smc_set_xcvr(struct net_device *dev, int if_port);
static void smc_reset(struct net_device *dev);
static void media_check(u_long arg);
static void mdio_sync(unsigned int addr);
static int mdio_read(struct net_device *dev, int phy_id, int loc);
static void mdio_write(struct net_device *dev, int phy_id, int loc, int value);
static int smc_link_ok(struct net_device *dev);
static const struct ethtool_ops ethtool_ops;

/*======================================================================

  smc91c92_attach() creates an "instance" of the driver, allocating
  local data structures for one device.  The device is registered
  with Card Services.

======================================================================*/

static int smc91c92_probe(struct pcmcia_device *link)
{
    struct smc_private *smc;
    struct net_device *dev;

    DEBUG(0, "smc91c92_attach()\n");

    /* Create new ethernet device */
    dev = alloc_etherdev(sizeof(struct smc_private));
    if (!dev)
	return -ENOMEM;
    smc = netdev_priv(dev);
    smc->p_dev = link;
    link->priv = dev;

    spin_lock_init(&smc->lock);
    link->io.NumPorts1 = 16;
    link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
    link->io.IOAddrLines = 4;
    link->irq.Attributes = IRQ_TYPE_DYNAMIC_SHARING|IRQ_HANDLE_PRESENT;
    link->irq.IRQInfo1 = IRQ_LEVEL_ID;
    link->irq.Handler = &smc_interrupt;
    link->irq.Instance = dev;
    link->conf.Attributes = CONF_ENABLE_IRQ;
    link->conf.IntType = INT_MEMORY_AND_IO;

    /* The SMC91c92-specific entries in the device structure. */
    dev->hard_start_xmit = &smc_start_xmit;
    dev->get_stats = &smc_get_stats;
    dev->set_config = &s9k_config;
    dev->set_multicast_list = &set_rx_mode;
    dev->open = &smc_open;
    dev->stop = &smc_close;
    dev->do_ioctl = &smc_ioctl;
    SET_ETHTOOL_OPS(dev, &ethtool_ops);
#ifdef HAVE_TX_TIMEOUT
    dev->tx_timeout = smc_tx_timeout;
    dev->watchdog_timeo = TX_TIMEOUT;
#endif

    smc->mii_if.dev = dev;
    smc->mii_if.mdio_read = mdio_read;
    smc->mii_if.mdio_write = mdio_write;
    smc->mii_if.phy_id_mask = 0x1f;
    smc->mii_if.reg_num_mask = 0x1f;

    return smc91c92_config(link);
} /* smc91c92_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void smc91c92_detach(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;

    DEBUG(0, "smc91c92_detach(0x%p)\n", link);

    if (link->dev_node)
	unregister_netdev(dev);

    smc91c92_release(link);

    free_netdev(dev);
} /* smc91c92_detach */

/*====================================================================*/

static int cvt_ascii_address(struct net_device *dev, char *s)
{
    int i, j, da, c;

    if (strlen(s) != 12)
	return -1;
    for (i = 0; i < 6; i++) {
	da = 0;
	for (j = 0; j < 2; j++) {
	    c = *s++;
	    da <<= 4;
	    da += ((c >= '0') && (c <= '9')) ?
		(c - '0') : ((c & 0x0f) + 9);
	}
	dev->dev_addr[i] = da;
    }
    return 0;
}

/*====================================================================*/

static int first_tuple(struct pcmcia_device *handle, tuple_t *tuple,
		cisparse_t *parse)
{
	int i;

	if ((i = pcmcia_get_first_tuple(handle, tuple)) != CS_SUCCESS ||
			(i = pcmcia_get_tuple_data(handle, tuple)) != CS_SUCCESS)
		return i;
	return pcmcia_parse_tuple(handle, tuple, parse);
}

static int next_tuple(struct pcmcia_device *handle, tuple_t *tuple,
		cisparse_t *parse)
{
	int i;

	if ((i = pcmcia_get_next_tuple(handle, tuple)) != CS_SUCCESS ||
			(i = pcmcia_get_tuple_data(handle, tuple)) != CS_SUCCESS)
		return i;
	return pcmcia_parse_tuple(handle, tuple, parse);
}

/*======================================================================

    Configuration stuff for Megahertz cards

    mhz_3288_power() is used to power up a 3288's ethernet chip.
    mhz_mfc_config() handles socket setup for multifunction (1144
    and 3288) cards.  mhz_setup() gets a card's hardware ethernet
    address.

======================================================================*/

static int mhz_3288_power(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;
    struct smc_private *smc = netdev_priv(dev);
    u_char tmp;

    /* Read the ISR twice... */
    readb(smc->base+MEGAHERTZ_ISR);
    udelay(5);
    readb(smc->base+MEGAHERTZ_ISR);

    /* Pause 200ms... */
    mdelay(200);

    /* Now read and write the COR... */
    tmp = readb(smc->base + link->conf.ConfigBase + CISREG_COR);
    udelay(5);
    writeb(tmp, smc->base + link->conf.ConfigBase + CISREG_COR);

    return 0;
}

static int mhz_mfc_config_check(struct pcmcia_device *p_dev,
				cistpl_cftable_entry_t *cf,
				cistpl_cftable_entry_t *dflt,
				void *priv_data)
{
	int k;
	p_dev->io.BasePort2 = cf->io.win[0].base;
	for (k = 0; k < 0x400; k += 0x10) {
		if (k & 0x80)
			continue;
		p_dev->io.BasePort1 = k ^ 0x300;
		if (!pcmcia_request_io(p_dev, &p_dev->io))
			return 0;
	}
	return -ENODEV;
}

static int mhz_mfc_config(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;
    struct smc_private *smc = netdev_priv(dev);
    struct smc_cfg_mem *cfg_mem;
    win_req_t req;
    memreq_t mem;
    int i;

    cfg_mem = kmalloc(sizeof(struct smc_cfg_mem), GFP_KERNEL);
    if (!cfg_mem)
        return CS_OUT_OF_RESOURCE;

    link->conf.Attributes |= CONF_ENABLE_SPKR;
    link->conf.Status = CCSR_AUDIO_ENA;
    link->irq.Attributes =
	IRQ_TYPE_DYNAMIC_SHARING|IRQ_FIRST_SHARED|IRQ_HANDLE_PRESENT;
    link->io.IOAddrLines = 16;
    link->io.Attributes2 = IO_DATA_PATH_WIDTH_8;
    link->io.NumPorts2 = 8;

    /* The Megahertz combo cards have modem-like CIS entries, so
       we have to explicitly try a bunch of port combinations. */
    if (pcmcia_loop_config(link, mhz_mfc_config_check, NULL))
	goto free_cfg_mem;
    dev->base_addr = link->io.BasePort1;

    /* Allocate a memory window, for accessing the ISR */
    req.Attributes = WIN_DATA_WIDTH_8|WIN_MEMORY_TYPE_AM|WIN_ENABLE;
    req.Base = req.Size = 0;
    req.AccessSpeed = 0;
    i = pcmcia_request_window(&link, &req, &link->win);
    if (i != CS_SUCCESS)
	goto free_cfg_mem;
    smc->base = ioremap(req.Base, req.Size);
    mem.CardOffset = mem.Page = 0;
    if (smc->manfid == MANFID_MOTOROLA)
	mem.CardOffset = link->conf.ConfigBase;
    i = pcmcia_map_mem_page(link->win, &mem);

    if ((i == CS_SUCCESS)
	&& (smc->manfid == MANFID_MEGAHERTZ)
	&& (smc->cardid == PRODID_MEGAHERTZ_EM3288))
	mhz_3288_power(link);

free_cfg_mem:
    kfree(cfg_mem);
    return -ENODEV;
}

static int mhz_setup(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;
    struct smc_cfg_mem *cfg_mem;
    tuple_t *tuple;
    cisparse_t *parse;
    u_char *buf, *station_addr;
    int rc;

    cfg_mem = kmalloc(sizeof(struct smc_cfg_mem), GFP_KERNEL);
    if (!cfg_mem)
	return -1;

    tuple = &cfg_mem->tuple;
    parse = &cfg_mem->parse;
    buf = cfg_mem->buf;

    tuple->Attributes = tuple->TupleOffset = 0;
    tuple->TupleData = (cisdata_t *)buf;
    tuple->TupleDataMax = 255;

    /* Read the station address from the CIS.  It is stored as the last
       (fourth) string in the Version 1 Version/ID tuple. */
    tuple->DesiredTuple = CISTPL_VERS_1;
    if (first_tuple(link, tuple, parse) != CS_SUCCESS) {
	rc = -1;
	goto free_cfg_mem;
    }
    /* Ugh -- the EM1144 card has two VERS_1 tuples!?! */
    if (next_tuple(link, tuple, parse) != CS_SUCCESS)
	first_tuple(link, tuple, parse);
    if (parse->version_1.ns > 3) {
	station_addr = parse->version_1.str + parse->version_1.ofs[3];
	if (cvt_ascii_address(dev, station_addr) == 0) {
		rc = 0;
		goto free_cfg_mem;
	}
    }

    /* Another possibility: for the EM3288, in a special tuple */
    tuple->DesiredTuple = 0x81;
    if (pcmcia_get_first_tuple(link, tuple) != CS_SUCCESS) {
	rc = -1;
	goto free_cfg_mem;
    }
    if (pcmcia_get_tuple_data(link, tuple) != CS_SUCCESS) {
	rc = -1;
	goto free_cfg_mem;
    }
    buf[12] = '\0';
    if (cvt_ascii_address(dev, buf) == 0) {
	rc = 0;
	goto free_cfg_mem;
   }
    rc = -1;
free_cfg_mem:
   kfree(cfg_mem);
   return rc;
}

/*======================================================================

    Configuration stuff for the Motorola Mariner

    mot_config() writes directly to the Mariner configuration
    registers because the CIS is just bogus.

======================================================================*/

static void mot_config(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;
    struct smc_private *smc = netdev_priv(dev);
    unsigned int ioaddr = dev->base_addr;
    unsigned int iouart = link->io.BasePort2;

    /* Set UART base address and force map with COR bit 1 */
    writeb(iouart & 0xff,        smc->base + MOT_UART + CISREG_IOBASE_0);
    writeb((iouart >> 8) & 0xff, smc->base + MOT_UART + CISREG_IOBASE_1);
    writeb(MOT_NORMAL,           smc->base + MOT_UART + CISREG_COR);

    /* Set SMC base address and force map with COR bit 1 */
    writeb(ioaddr & 0xff,        smc->base + MOT_LAN + CISREG_IOBASE_0);
    writeb((ioaddr >> 8) & 0xff, smc->base + MOT_LAN + CISREG_IOBASE_1);
    writeb(MOT_NORMAL,           smc->base + MOT_LAN + CISREG_COR);

    /* Wait for things to settle down */
    mdelay(100);
}

static int mot_setup(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;
    unsigned int ioaddr = dev->base_addr;
    int i, wait, loop;
    u_int addr;

    /* Read Ethernet address from Serial EEPROM */

    for (i = 0; i < 3; i++) {
	SMC_SELECT_BANK(2);
	outw(MOT_EEPROM + i, ioaddr + POINTER);
	SMC_SELECT_BANK(1);
	outw((CTL_RELOAD | CTL_EE_SELECT), ioaddr + CONTROL);

	for (loop = wait = 0; loop < 200; loop++) {
	    udelay(10);
	    wait = ((CTL_RELOAD | CTL_STORE) & inw(ioaddr + CONTROL));
	    if (wait == 0) break;
	}
	
	if (wait)
	    return -1;
	
	addr = inw(ioaddr + GENERAL);
	dev->dev_addr[2*i]   = addr & 0xff;
	dev->dev_addr[2*i+1] = (addr >> 8) & 0xff;
    }

    return 0;
}

/*====================================================================*/

static int smc_configcheck(struct pcmcia_device *p_dev,
			   cistpl_cftable_entry_t *cf,
			   cistpl_cftable_entry_t *dflt,
			   void *priv_data)
{
	p_dev->io.BasePort1 = cf->io.win[0].base;
	p_dev->io.IOAddrLines = cf->io.flags & CISTPL_IO_LINES_MASK;
	return pcmcia_request_io(p_dev, &p_dev->io);
}

static int smc_config(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;
    int i;

    link->io.NumPorts1 = 16;
    i = pcmcia_loop_config(link, smc_configcheck, NULL);
    if (!i)
	    dev->base_addr = link->io.BasePort1;

    return i;
}

static int smc_setup(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;
    struct smc_cfg_mem *cfg_mem;
    tuple_t *tuple;
    cisparse_t *parse;
    cistpl_lan_node_id_t *node_id;
    u_char *buf, *station_addr;
    int i, rc;

    cfg_mem = kmalloc(sizeof(struct smc_cfg_mem), GFP_KERNEL);
    if (!cfg_mem)
	return CS_OUT_OF_RESOURCE;

    tuple = &cfg_mem->tuple;
    parse = &cfg_mem->parse;
    buf = cfg_mem->buf;

    tuple->Attributes = tuple->TupleOffset = 0;
    tuple->TupleData = (cisdata_t *)buf;
    tuple->TupleDataMax = 255;

    /* Check for a LAN function extension tuple */
    tuple->DesiredTuple = CISTPL_FUNCE;
    i = first_tuple(link, tuple, parse);
    while (i == CS_SUCCESS) {
	if (parse->funce.type == CISTPL_FUNCE_LAN_NODE_ID)
	    break;
	i = next_tuple(link, tuple, parse);
    }
    if (i == CS_SUCCESS) {
	node_id = (cistpl_lan_node_id_t *)parse->funce.data;
	if (node_id->nb == 6) {
	    for (i = 0; i < 6; i++)
		dev->dev_addr[i] = node_id->id[i];
	    rc = 0;
	    goto free_cfg_mem;
	}
    }
    /* Try the third string in the Version 1 Version/ID tuple. */
    if (link->prod_id[2]) {
	station_addr = link->prod_id[2];
	if (cvt_ascii_address(dev, station_addr) == 0) {
		rc = 0;
		goto free_cfg_mem;
	}
    }

    rc = -1;
free_cfg_mem:
    kfree(cfg_mem);
    return rc;
}

/*====================================================================*/

static int osi_config(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;
    static const unsigned int com[4] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };
    int i, j;

    link->conf.Attributes |= CONF_ENABLE_SPKR;
    link->conf.Status = CCSR_AUDIO_ENA;
    link->irq.Attributes =
	IRQ_TYPE_DYNAMIC_SHARING|IRQ_FIRST_SHARED|IRQ_HANDLE_PRESENT;
    link->io.NumPorts1 = 64;
    link->io.Attributes2 = IO_DATA_PATH_WIDTH_8;
    link->io.NumPorts2 = 8;
    link->io.IOAddrLines = 16;

    /* Enable Hard Decode, LAN, Modem */
    link->conf.ConfigIndex = 0x23;

    for (i = j = 0; j < 4; j++) {
	link->io.BasePort2 = com[j];
	i = pcmcia_request_io(link, &link->io);
	if (i == CS_SUCCESS) break;
    }
    if (i != CS_SUCCESS) {
	/* Fallback: turn off hard decode */
	link->conf.ConfigIndex = 0x03;
	link->io.NumPorts2 = 0;
	i = pcmcia_request_io(link, &link->io);
    }
    dev->base_addr = link->io.BasePort1 + 0x10;
    return i;
}

static int osi_setup(struct pcmcia_device *link, u_short manfid, u_short cardid)
{
    struct net_device *dev = link->priv;
    struct smc_cfg_mem *cfg_mem;
    tuple_t *tuple;
    u_char *buf;
    int i, rc;

    cfg_mem = kmalloc(sizeof(struct smc_cfg_mem), GFP_KERNEL);
    if (!cfg_mem)
        return -1;

    tuple = &cfg_mem->tuple;
    buf = cfg_mem->buf;

    tuple->Attributes = TUPLE_RETURN_COMMON;
    tuple->TupleData = (cisdata_t *)buf;
    tuple->TupleDataMax = 255;
    tuple->TupleOffset = 0;

    /* Read the station address from tuple 0x90, subtuple 0x04 */
    tuple->DesiredTuple = 0x90;
    i = pcmcia_get_first_tuple(link, tuple);
    while (i == CS_SUCCESS) {
	i = pcmcia_get_tuple_data(link, tuple);
	if ((i != CS_SUCCESS) || (buf[0] == 0x04))
	    break;
	i = pcmcia_get_next_tuple(link, tuple);
    }
    if (i != CS_SUCCESS) {
	rc = -1;
	goto free_cfg_mem;
    }
    for (i = 0; i < 6; i++)
	dev->dev_addr[i] = buf[i+2];

    if (((manfid == MANFID_OSITECH) &&
	 (cardid == PRODID_OSITECH_SEVEN)) ||
	((manfid == MANFID_PSION) &&
	 (cardid == PRODID_PSION_NET100))) {
	/* Download the Seven of Diamonds firmware */
	for (i = 0; i < sizeof(__Xilinx7OD); i++) {
	    outb(__Xilinx7OD[i], link->io.BasePort1+2);
	    udelay(50);
	}
    } else if (manfid == MANFID_OSITECH) {
	/* Make sure both functions are powered up */
	set_bits(0x300, link->io.BasePort1 + OSITECH_AUI_PWR);
	/* Now, turn on the interrupt for both card functions */
	set_bits(0x300, link->io.BasePort1 + OSITECH_RESET_ISR);
	DEBUG(2, "AUI/PWR: %4.4x RESET/ISR: %4.4x\n",
	      inw(link->io.BasePort1 + OSITECH_AUI_PWR),
	      inw(link->io.BasePort1 + OSITECH_RESET_ISR));
    }
    rc = 0;
free_cfg_mem:
   kfree(cfg_mem);
   return rc;
}

static int smc91c92_suspend(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	if (link->open)
		netif_device_detach(dev);

	return 0;
}

static int smc91c92_resume(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;
	struct smc_private *smc = netdev_priv(dev);
	int i;

	if ((smc->manfid == MANFID_MEGAHERTZ) &&
	    (smc->cardid == PRODID_MEGAHERTZ_EM3288))
		mhz_3288_power(link);
	if (smc->manfid == MANFID_MOTOROLA)
		mot_config(link);
	if ((smc->manfid == MANFID_OSITECH) &&
	    (smc->cardid != PRODID_OSITECH_SEVEN)) {
		/* Power up the card and enable interrupts */
		set_bits(0x0300, dev->base_addr-0x10+OSITECH_AUI_PWR);
		set_bits(0x0300, dev->base_addr-0x10+OSITECH_RESET_ISR);
	}
	if (((smc->manfid == MANFID_OSITECH) &&
	     (smc->cardid == PRODID_OSITECH_SEVEN)) ||
	    ((smc->manfid == MANFID_PSION) &&
	     (smc->cardid == PRODID_PSION_NET100))) {
		/* Download the Seven of Diamonds firmware */
		for (i = 0; i < sizeof(__Xilinx7OD); i++) {
			outb(__Xilinx7OD[i], link->io.BasePort1+2);
			udelay(50);
		}
	}
	if (link->open) {
		smc_reset(dev);
		netif_device_attach(dev);
	}

	return 0;
}


/*======================================================================

    This verifies that the chip is some SMC91cXX variant, and returns
    the revision code if successful.  Otherwise, it returns -ENODEV.

======================================================================*/

static int check_sig(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;
    unsigned int ioaddr = dev->base_addr;
    int width;
    u_short s;

    SMC_SELECT_BANK(1);
    if (inw(ioaddr + BANK_SELECT) >> 8 != 0x33) {
	/* Try powering up the chip */
	outw(0, ioaddr + CONTROL);
	mdelay(55);
    }

    /* Try setting bus width */
    width = (link->io.Attributes1 == IO_DATA_PATH_WIDTH_AUTO);
    s = inb(ioaddr + CONFIG);
    if (width)
	s |= CFG_16BIT;
    else
	s &= ~CFG_16BIT;
    outb(s, ioaddr + CONFIG);

    /* Check Base Address Register to make sure bus width is OK */
    s = inw(ioaddr + BASE_ADDR);
    if ((inw(ioaddr + BANK_SELECT) >> 8 == 0x33) &&
	((s >> 8) != (s & 0xff))) {
	SMC_SELECT_BANK(3);
	s = inw(ioaddr + REVISION);
	return (s & 0xff);
    }

    if (width) {
	    modconf_t mod = {
		    .Attributes = CONF_IO_CHANGE_WIDTH,
	    };
	    printk(KERN_INFO "smc91c92_cs: using 8-bit IO window.\n");

	    smc91c92_suspend(link);
	    pcmcia_modify_configuration(link, &mod);
	    smc91c92_resume(link);
	    return check_sig(link);
    }
    return -ENODEV;
}

/*======================================================================

    smc91c92_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    ethernet device available to the system.

======================================================================*/

#define CS_EXIT_TEST(ret, svc, label) \
if (ret != CS_SUCCESS) { cs_error(link, svc, ret); goto label; }

static int smc91c92_config(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;
    struct smc_private *smc = netdev_priv(dev);
    char *name;
    int i, j, rev;
    unsigned int ioaddr;
    u_long mir;
    DECLARE_MAC_BUF(mac);

    DEBUG(0, "smc91c92_config(0x%p)\n", link);

    smc->manfid = link->manf_id;
    smc->cardid = link->card_id;

    if ((smc->manfid == MANFID_OSITECH) &&
	(smc->cardid != PRODID_OSITECH_SEVEN)) {
	i = osi_config(link);
    } else if ((smc->manfid == MANFID_MOTOROLA) ||
	       ((smc->manfid == MANFID_MEGAHERTZ) &&
		((smc->cardid == PRODID_MEGAHERTZ_VARIOUS) ||
		 (smc->cardid == PRODID_MEGAHERTZ_EM3288)))) {
	i = mhz_mfc_config(link);
    } else {
	i = smc_config(link);
    }
    CS_EXIT_TEST(i, RequestIO, config_failed);

    i = pcmcia_request_irq(link, &link->irq);
    CS_EXIT_TEST(i, RequestIRQ, config_failed);
    i = pcmcia_request_configuration(link, &link->conf);
    CS_EXIT_TEST(i, RequestConfiguration, config_failed);

    if (smc->manfid == MANFID_MOTOROLA)
	mot_config(link);

    dev->irq = link->irq.AssignedIRQ;

    if ((if_port >= 0) && (if_port <= 2))
	dev->if_port = if_port;
    else
	printk(KERN_NOTICE "smc91c92_cs: invalid if_port requested\n");

    switch (smc->manfid) {
    case MANFID_OSITECH:
    case MANFID_PSION:
	i = osi_setup(link, smc->manfid, smc->cardid); break;
    case MANFID_SMC:
    case MANFID_NEW_MEDIA:
	i = smc_setup(link); break;
    case 0x128: /* For broken Megahertz cards */
    case MANFID_MEGAHERTZ:
	i = mhz_setup(link); break;
    case MANFID_MOTOROLA:
    default: /* get the hw address from EEPROM */
	i = mot_setup(link); break;
    }

    if (i != 0) {
	printk(KERN_NOTICE "smc91c92_cs: Unable to find hardware address.\n");
	goto config_undo;
    }

    smc->duplex = 0;
    smc->rx_ovrn = 0;

    rev = check_sig(link);
    name = "???";
    if (rev > 0)
	switch (rev >> 4) {
	case 3: name = "92"; break;
	case 4: name = ((rev & 15) >= 6) ? "96" : "94"; break;
	case 5: name = "95"; break;
	case 7: name = "100"; break;
	case 8: name = "100-FD"; break;
	case 9: name = "110"; break;
	}

    ioaddr = dev->base_addr;
    if (rev > 0) {
	u_long mcr;
	SMC_SELECT_BANK(0);
	mir = inw(ioaddr + MEMINFO) & 0xff;
	if (mir == 0xff) mir++;
	/* Get scale factor for memory size */
	mcr = ((rev >> 4) > 3) ? inw(ioaddr + MEMCFG) : 0x0200;
	mir *= 128 * (1<<((mcr >> 9) & 7));
	SMC_SELECT_BANK(1);
	smc->cfg = inw(ioaddr + CONFIG) & ~CFG_AUI_SELECT;
	smc->cfg |= CFG_NO_WAIT | CFG_16BIT | CFG_STATIC;
	if (smc->manfid == MANFID_OSITECH)
	    smc->cfg |= CFG_IRQ_SEL_1 | CFG_IRQ_SEL_0;
	if ((rev >> 4) >= 7)
	    smc->cfg |= CFG_MII_SELECT;
    } else
	mir = 0;

    if (smc->cfg & CFG_MII_SELECT) {
	SMC_SELECT_BANK(3);

	for (i = 0; i < 32; i++) {
	    j = mdio_read(dev, i, 1);
	    if ((j != 0) && (j != 0xffff)) break;
	}
	smc->mii_if.phy_id = (i < 32) ? i : -1;

	SMC_SELECT_BANK(0);
    }

    link->dev_node = &smc->node;
    SET_NETDEV_DEV(dev, &handle_to_dev(link));

    if (register_netdev(dev) != 0) {
	printk(KERN_ERR "smc91c92_cs: register_netdev() failed\n");
	link->dev_node = NULL;
	goto config_undo;
    }

    strcpy(smc->node.dev_name, dev->name);

    printk(KERN_INFO "%s: smc91c%s rev %d: io %#3lx, irq %d, "
	   "hw_addr %s\n",
	   dev->name, name, (rev & 0x0f), dev->base_addr, dev->irq,
	   print_mac(mac, dev->dev_addr));

    if (rev > 0) {
	if (mir & 0x3ff)
	    printk(KERN_INFO "  %lu byte", mir);
	else
	    printk(KERN_INFO "  %lu kb", mir>>10);
	printk(" buffer, %s xcvr\n", (smc->cfg & CFG_MII_SELECT) ?
	       "MII" : if_names[dev->if_port]);
    }

    if (smc->cfg & CFG_MII_SELECT) {
	if (smc->mii_if.phy_id != -1) {
	    DEBUG(0, "  MII transceiver at index %d, status %x.\n",
		  smc->mii_if.phy_id, j);
	} else {
    	    printk(KERN_NOTICE "  No MII transceivers found!\n");
	}
    }
    return 0;

config_undo:
    unregister_netdev(dev);
config_failed:			/* CS_EXIT_TEST() calls jump to here... */
    smc91c92_release(link);
    return -ENODEV;
} /* smc91c92_config */

/*======================================================================

    After a card is removed, smc91c92_release() will unregister the net
    device, and release the PCMCIA configuration.  If the device is
    still open, this will be postponed until it is closed.

======================================================================*/

static void smc91c92_release(struct pcmcia_device *link)
{
	DEBUG(0, "smc91c92_release(0x%p)\n", link);
	if (link->win) {
		struct net_device *dev = link->priv;
		struct smc_private *smc = netdev_priv(dev);
		iounmap(smc->base);
	}
	pcmcia_disable_device(link);
}

/*======================================================================

    MII interface support for SMC91cXX based cards
======================================================================*/

#define MDIO_SHIFT_CLK		0x04
#define MDIO_DATA_OUT		0x01
#define MDIO_DIR_WRITE		0x08
#define MDIO_DATA_WRITE0	(MDIO_DIR_WRITE)
#define MDIO_DATA_WRITE1	(MDIO_DIR_WRITE | MDIO_DATA_OUT)
#define MDIO_DATA_READ		0x02

static void mdio_sync(unsigned int addr)
{
    int bits;
    for (bits = 0; bits < 32; bits++) {
	outb(MDIO_DATA_WRITE1, addr);
	outb(MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, addr);
    }
}

static int mdio_read(struct net_device *dev, int phy_id, int loc)
{
    unsigned int addr = dev->base_addr + MGMT;
    u_int cmd = (0x06<<10)|(phy_id<<5)|loc;
    int i, retval = 0;

    mdio_sync(addr);
    for (i = 13; i >= 0; i--) {
	int dat = (cmd&(1<<i)) ? MDIO_DATA_WRITE1 : MDIO_DATA_WRITE0;
	outb(dat, addr);
	outb(dat | MDIO_SHIFT_CLK, addr);
    }
    for (i = 19; i > 0; i--) {
	outb(0, addr);
	retval = (retval << 1) | ((inb(addr) & MDIO_DATA_READ) != 0);
	outb(MDIO_SHIFT_CLK, addr);
    }
    return (retval>>1) & 0xffff;
}

static void mdio_write(struct net_device *dev, int phy_id, int loc, int value)
{
    unsigned int addr = dev->base_addr + MGMT;
    u_int cmd = (0x05<<28)|(phy_id<<23)|(loc<<18)|(1<<17)|value;
    int i;

    mdio_sync(addr);
    for (i = 31; i >= 0; i--) {
	int dat = (cmd&(1<<i)) ? MDIO_DATA_WRITE1 : MDIO_DATA_WRITE0;
	outb(dat, addr);
	outb(dat | MDIO_SHIFT_CLK, addr);
    }
    for (i = 1; i >= 0; i--) {
	outb(0, addr);
	outb(MDIO_SHIFT_CLK, addr);
    }
}

/*======================================================================

    The driver core code, most of which should be common with a
    non-PCMCIA implementation.

======================================================================*/

#ifdef PCMCIA_DEBUG
static void smc_dump(struct net_device *dev)
{
    unsigned int ioaddr = dev->base_addr;
    u_short i, w, save;
    save = inw(ioaddr + BANK_SELECT);
    for (w = 0; w < 4; w++) {
	SMC_SELECT_BANK(w);
	printk(KERN_DEBUG "bank %d: ", w);
	for (i = 0; i < 14; i += 2)
	    printk(" %04x", inw(ioaddr + i));
	printk("\n");
    }
    outw(save, ioaddr + BANK_SELECT);
}
#endif

static int smc_open(struct net_device *dev)
{
    struct smc_private *smc = netdev_priv(dev);
    struct pcmcia_device *link = smc->p_dev;

#ifdef PCMCIA_DEBUG
    DEBUG(0, "%s: smc_open(%p), ID/Window %4.4x.\n",
	  dev->name, dev, inw(dev->base_addr + BANK_SELECT));
    if (pc_debug > 1) smc_dump(dev);
#endif

    /* Check that the PCMCIA card is still here. */
    if (!pcmcia_dev_present(link))
	return -ENODEV;
    /* Physical device present signature. */
    if (check_sig(link) < 0) {
	printk("smc91c92_cs: Yikes!  Bad chip signature!\n");
	return -ENODEV;
    }
    link->open++;

    netif_start_queue(dev);
    smc->saved_skb = NULL;
    smc->packets_waiting = 0;

    smc_reset(dev);
    init_timer(&smc->media);
    smc->media.function = &media_check;
    smc->media.data = (u_long) dev;
    smc->media.expires = jiffies + HZ;
    add_timer(&smc->media);

    return 0;
} /* smc_open */

/*====================================================================*/

static int smc_close(struct net_device *dev)
{
    struct smc_private *smc = netdev_priv(dev);
    struct pcmcia_device *link = smc->p_dev;
    unsigned int ioaddr = dev->base_addr;

    DEBUG(0, "%s: smc_close(), status %4.4x.\n",
	  dev->name, inw(ioaddr + BANK_SELECT));

    netif_stop_queue(dev);

    /* Shut off all interrupts, and turn off the Tx and Rx sections.
       Don't bother to check for chip present. */
    SMC_SELECT_BANK(2);	/* Nominally paranoia, but do no assume... */
    outw(0, ioaddr + INTERRUPT);
    SMC_SELECT_BANK(0);
    mask_bits(0xff00, ioaddr + RCR);
    mask_bits(0xff00, ioaddr + TCR);

    /* Put the chip into power-down mode. */
    SMC_SELECT_BANK(1);
    outw(CTL_POWERDOWN, ioaddr + CONTROL );

    link->open--;
    del_timer_sync(&smc->media);

    return 0;
} /* smc_close */

/*======================================================================

   Transfer a packet to the hardware and trigger the packet send.
   This may be called at either from either the Tx queue code
   or the interrupt handler.

======================================================================*/

static void smc_hardware_send_packet(struct net_device * dev)
{
    struct smc_private *smc = netdev_priv(dev);
    struct sk_buff *skb = smc->saved_skb;
    unsigned int ioaddr = dev->base_addr;
    u_char packet_no;

    if (!skb) {
	printk(KERN_ERR "%s: In XMIT with no packet to send.\n", dev->name);
	return;
    }

    /* There should be a packet slot waiting. */
    packet_no = inw(ioaddr + PNR_ARR) >> 8;
    if (packet_no & 0x80) {
	/* If not, there is a hardware problem!  Likely an ejected card. */
	printk(KERN_WARNING "%s: 91c92 hardware Tx buffer allocation"
	       " failed, status %#2.2x.\n", dev->name, packet_no);
	dev_kfree_skb_irq(skb);
	smc->saved_skb = NULL;
	netif_start_queue(dev);
	return;
    }

    smc->stats.tx_bytes += skb->len;
    /* The card should use the just-allocated buffer. */
    outw(packet_no, ioaddr + PNR_ARR);
    /* point to the beginning of the packet */
    outw(PTR_AUTOINC , ioaddr + POINTER);

    /* Send the packet length (+6 for status, length and ctl byte)
       and the status word (set to zeros). */
    {
	u_char *buf = skb->data;
	u_int length = skb->len; /* The chip will pad to ethernet min. */

	DEBUG(2, "%s: Trying to xmit packet of length %d.\n",
	      dev->name, length);
	
	/* send the packet length: +6 for status word, length, and ctl */
	outw(0, ioaddr + DATA_1);
	outw(length + 6, ioaddr + DATA_1);
	outsw(ioaddr + DATA_1, buf, length >> 1);
	
	/* The odd last byte, if there is one, goes in the control word. */
	outw((length & 1) ? 0x2000 | buf[length-1] : 0, ioaddr + DATA_1);
    }

    /* Enable the Tx interrupts, both Tx (TxErr) and TxEmpty. */
    outw(((IM_TX_INT|IM_TX_EMPTY_INT)<<8) |
	 (inw(ioaddr + INTERRUPT) & 0xff00),
	 ioaddr + INTERRUPT);

    /* The chip does the rest of the work. */
    outw(MC_ENQUEUE , ioaddr + MMU_CMD);

    smc->saved_skb = NULL;
    dev_kfree_skb_irq(skb);
    dev->trans_start = jiffies;
    netif_start_queue(dev);
    return;
}

/*====================================================================*/

static void smc_tx_timeout(struct net_device *dev)
{
    struct smc_private *smc = netdev_priv(dev);
    unsigned int ioaddr = dev->base_addr;

    printk(KERN_NOTICE "%s: SMC91c92 transmit timed out, "
	   "Tx_status %2.2x status %4.4x.\n",
	   dev->name, inw(ioaddr)&0xff, inw(ioaddr + 2));
    smc->stats.tx_errors++;
    smc_reset(dev);
    dev->trans_start = jiffies;
    smc->saved_skb = NULL;
    netif_wake_queue(dev);
}

static int smc_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct smc_private *smc = netdev_priv(dev);
    unsigned int ioaddr = dev->base_addr;
    u_short num_pages;
    short time_out, ir;
    unsigned long flags;

    netif_stop_queue(dev);

    DEBUG(2, "%s: smc_start_xmit(length = %d) called,"
	  " status %4.4x.\n", dev->name, skb->len, inw(ioaddr + 2));

    if (smc->saved_skb) {
	/* THIS SHOULD NEVER HAPPEN. */
	smc->stats.tx_aborted_errors++;
	printk(KERN_DEBUG "%s: Internal error -- sent packet while busy.\n",
	       dev->name);
	return 1;
    }
    smc->saved_skb = skb;

    num_pages = skb->len >> 8;

    if (num_pages > 7) {
	printk(KERN_ERR "%s: Far too big packet error.\n", dev->name);
	dev_kfree_skb (skb);
	smc->saved_skb = NULL;
	smc->stats.tx_dropped++;
	return 0;		/* Do not re-queue this packet. */
    }
    /* A packet is now waiting. */
    smc->packets_waiting++;

    spin_lock_irqsave(&smc->lock, flags);
    SMC_SELECT_BANK(2);	/* Paranoia, we should always be in window 2 */

    /* need MC_RESET to keep the memory consistent. errata? */
    if (smc->rx_ovrn) {
	outw(MC_RESET, ioaddr + MMU_CMD);
	smc->rx_ovrn = 0;
    }

    /* Allocate the memory; send the packet now if we win. */
    outw(MC_ALLOC | num_pages, ioaddr + MMU_CMD);
    for (time_out = MEMORY_WAIT_TIME; time_out >= 0; time_out--) {
	ir = inw(ioaddr+INTERRUPT);
	if (ir & IM_ALLOC_INT) {
	    /* Acknowledge the interrupt, send the packet. */
	    outw((ir&0xff00) | IM_ALLOC_INT, ioaddr + INTERRUPT);
	    smc_hardware_send_packet(dev);	/* Send the packet now.. */
	    spin_unlock_irqrestore(&smc->lock, flags);
	    return 0;
	}
    }

    /* Otherwise defer until the Tx-space-allocated interrupt. */
    DEBUG(2, "%s: memory allocation deferred.\n", dev->name);
    outw((IM_ALLOC_INT << 8) | (ir & 0xff00), ioaddr + INTERRUPT);
    spin_unlock_irqrestore(&smc->lock, flags);

    return 0;
}

/*======================================================================

    Handle a Tx anomolous event.  Entered while in Window 2.

======================================================================*/

static void smc_tx_err(struct net_device * dev)
{
    struct smc_private *smc = netdev_priv(dev);
    unsigned int ioaddr = dev->base_addr;
    int saved_packet = inw(ioaddr + PNR_ARR) & 0xff;
    int packet_no = inw(ioaddr + FIFO_PORTS) & 0x7f;
    int tx_status;

    /* select this as the packet to read from */
    outw(packet_no, ioaddr + PNR_ARR);

    /* read the first word from this packet */
    outw(PTR_AUTOINC | PTR_READ | 0, ioaddr + POINTER);

    tx_status = inw(ioaddr + DATA_1);

    smc->stats.tx_errors++;
    if (tx_status & TS_LOSTCAR) smc->stats.tx_carrier_errors++;
    if (tx_status & TS_LATCOL)  smc->stats.tx_window_errors++;
    if (tx_status & TS_16COL) {
	smc->stats.tx_aborted_errors++;
	smc->tx_err++;
    }

    if (tx_status & TS_SUCCESS) {
	printk(KERN_NOTICE "%s: Successful packet caused error "
	       "interrupt?\n", dev->name);
    }
    /* re-enable transmit */
    SMC_SELECT_BANK(0);
    outw(inw(ioaddr + TCR) | TCR_ENABLE | smc->duplex, ioaddr + TCR);
    SMC_SELECT_BANK(2);

    outw(MC_FREEPKT, ioaddr + MMU_CMD); 	/* Free the packet memory. */

    /* one less packet waiting for me */
    smc->packets_waiting--;

    outw(saved_packet, ioaddr + PNR_ARR);
    return;
}

/*====================================================================*/

static void smc_eph_irq(struct net_device *dev)
{
    struct smc_private *smc = netdev_priv(dev);
    unsigned int ioaddr = dev->base_addr;
    u_short card_stats, ephs;

    SMC_SELECT_BANK(0);
    ephs = inw(ioaddr + EPH);
    DEBUG(2, "%s: Ethernet protocol handler interrupt, status"
	  " %4.4x.\n", dev->name, ephs);
    /* Could be a counter roll-over warning: update stats. */
    card_stats = inw(ioaddr + COUNTER);
    /* single collisions */
    smc->stats.collisions += card_stats & 0xF;
    card_stats >>= 4;
    /* multiple collisions */
    smc->stats.collisions += card_stats & 0xF;
#if 0 		/* These are for when linux supports these statistics */
    card_stats >>= 4;			/* deferred */
    card_stats >>= 4;			/* excess deferred */
#endif
    /* If we had a transmit error we must re-enable the transmitter. */
    outw(inw(ioaddr + TCR) | TCR_ENABLE | smc->duplex, ioaddr + TCR);

    /* Clear a link error interrupt. */
    SMC_SELECT_BANK(1);
    outw(CTL_AUTO_RELEASE | 0x0000, ioaddr + CONTROL);
    outw(CTL_AUTO_RELEASE | CTL_TE_ENABLE | CTL_CR_ENABLE,
	 ioaddr + CONTROL);
    SMC_SELECT_BANK(2);
}

/*====================================================================*/

static irqreturn_t smc_interrupt(int irq, void *dev_id)
{
    struct net_device *dev = dev_id;
    struct smc_private *smc = netdev_priv(dev);
    unsigned int ioaddr;
    u_short saved_bank, saved_pointer, mask, status;
    unsigned int handled = 1;
    char bogus_cnt = INTR_WORK;		/* Work we are willing to do. */

    if (!netif_device_present(dev))
	return IRQ_NONE;

    ioaddr = dev->base_addr;

    DEBUG(3, "%s: SMC91c92 interrupt %d at %#x.\n", dev->name,
	  irq, ioaddr);

    spin_lock(&smc->lock);
    smc->watchdog = 0;
    saved_bank = inw(ioaddr + BANK_SELECT);
    if ((saved_bank & 0xff00) != 0x3300) {
	/* The device does not exist -- the card could be off-line, or
	   maybe it has been ejected. */
	DEBUG(1, "%s: SMC91c92 interrupt %d for non-existent"
	      "/ejected device.\n", dev->name, irq);
	handled = 0;
	goto irq_done;
    }

    SMC_SELECT_BANK(2);
    saved_pointer = inw(ioaddr + POINTER);
    mask = inw(ioaddr + INTERRUPT) >> 8;
    /* clear all interrupts */
    outw(0, ioaddr + INTERRUPT);

    do { /* read the status flag, and mask it */
	status = inw(ioaddr + INTERRUPT) & 0xff;
	DEBUG(3, "%s: Status is %#2.2x (mask %#2.2x).\n", dev->name,
	      status, mask);
	if ((status & mask) == 0) {
	    if (bogus_cnt == INTR_WORK)
		handled = 0;
	    break;
	}
	if (status & IM_RCV_INT) {
	    /* Got a packet(s). */
	    smc_rx(dev);
	}
	if (status & IM_TX_INT) {
	    smc_tx_err(dev);
	    outw(IM_TX_INT, ioaddr + INTERRUPT);
	}
	status &= mask;
	if (status & IM_TX_EMPTY_INT) {
	    outw(IM_TX_EMPTY_INT, ioaddr + INTERRUPT);
	    mask &= ~IM_TX_EMPTY_INT;
	    smc->stats.tx_packets += smc->packets_waiting;
	    smc->packets_waiting = 0;
	}
	if (status & IM_ALLOC_INT) {
	    /* Clear this interrupt so it doesn't happen again */
	    mask &= ~IM_ALLOC_INT;
	
	    smc_hardware_send_packet(dev);
	
	    /* enable xmit interrupts based on this */
	    mask |= (IM_TX_EMPTY_INT | IM_TX_INT);
	
	    /* and let the card send more packets to me */
	    netif_wake_queue(dev);
	}
	if (status & IM_RX_OVRN_INT) {
	    smc->stats.rx_errors++;
	    smc->stats.rx_fifo_errors++;
	    if (smc->duplex)
		smc->rx_ovrn = 1; /* need MC_RESET outside smc_interrupt */
	    outw(IM_RX_OVRN_INT, ioaddr + INTERRUPT);
	}
	if (status & IM_EPH_INT)
	    smc_eph_irq(dev);
    } while (--bogus_cnt);

    DEBUG(3, "  Restoring saved registers mask %2.2x bank %4.4x"
	  " pointer %4.4x.\n", mask, saved_bank, saved_pointer);

    /* restore state register */
    outw((mask<<8), ioaddr + INTERRUPT);
    outw(saved_pointer, ioaddr + POINTER);
    SMC_SELECT_BANK(saved_bank);

    DEBUG(3, "%s: Exiting interrupt IRQ%d.\n", dev->name, irq);

irq_done:

    if ((smc->manfid == MANFID_OSITECH) &&
	(smc->cardid != PRODID_OSITECH_SEVEN)) {
	/* Retrigger interrupt if needed */
	mask_bits(0x00ff, ioaddr-0x10+OSITECH_RESET_ISR);
	set_bits(0x0300, ioaddr-0x10+OSITECH_RESET_ISR);
    }
    if (smc->manfid == MANFID_MOTOROLA) {
	u_char cor;
	cor = readb(smc->base + MOT_UART + CISREG_COR);
	writeb(cor & ~COR_IREQ_ENA, smc->base + MOT_UART + CISREG_COR);
	writeb(cor, smc->base + MOT_UART + CISREG_COR);
	cor = readb(smc->base + MOT_LAN + CISREG_COR);
	writeb(cor & ~COR_IREQ_ENA, smc->base + MOT_LAN + CISREG_COR);
	writeb(cor, smc->base + MOT_LAN + CISREG_COR);
    }
#ifdef DOES_NOT_WORK
    if (smc->base != NULL) { /* Megahertz MFC's */
	readb(smc->base+MEGAHERTZ_ISR);
	readb(smc->base+MEGAHERTZ_ISR);
    }
#endif
    spin_unlock(&smc->lock);
    return IRQ_RETVAL(handled);
}

/*====================================================================*/

static void smc_rx(struct net_device *dev)
{
    struct smc_private *smc = netdev_priv(dev);
    unsigned int ioaddr = dev->base_addr;
    int rx_status;
    int packet_length;	/* Caution: not frame length, rather words
			   to transfer from the chip. */

    /* Assertion: we are in Window 2. */

    if (inw(ioaddr + FIFO_PORTS) & FP_RXEMPTY) {
	printk(KERN_ERR "%s: smc_rx() with nothing on Rx FIFO.\n",
	       dev->name);
	return;
    }

    /*  Reset the read pointer, and read the status and packet length. */
    outw(PTR_READ | PTR_RCV | PTR_AUTOINC, ioaddr + POINTER);
    rx_status = inw(ioaddr + DATA_1);
    packet_length = inw(ioaddr + DATA_1) & 0x07ff;

    DEBUG(2, "%s: Receive status %4.4x length %d.\n",
	  dev->name, rx_status, packet_length);

    if (!(rx_status & RS_ERRORS)) {		
	/* do stuff to make a new packet */
	struct sk_buff *skb;
	
	/* Note: packet_length adds 5 or 6 extra bytes here! */
	skb = dev_alloc_skb(packet_length+2);
	
	if (skb == NULL) {
	    DEBUG(1, "%s: Low memory, packet dropped.\n", dev->name);
	    smc->stats.rx_dropped++;
	    outw(MC_RELEASE, ioaddr + MMU_CMD);
	    return;
	}
	
	packet_length -= (rx_status & RS_ODDFRAME ? 5 : 6);
	skb_reserve(skb, 2);
	insw(ioaddr+DATA_1, skb_put(skb, packet_length),
	     (packet_length+1)>>1);
	skb->protocol = eth_type_trans(skb, dev);
	
	netif_rx(skb);
	dev->last_rx = jiffies;
	smc->stats.rx_packets++;
	smc->stats.rx_bytes += packet_length;
	if (rx_status & RS_MULTICAST)
	    smc->stats.multicast++;
    } else {
	/* error ... */
	smc->stats.rx_errors++;
	
	if (rx_status & RS_ALGNERR)  smc->stats.rx_frame_errors++;
	if (rx_status & (RS_TOOSHORT | RS_TOOLONG))
	    smc->stats.rx_length_errors++;
	if (rx_status & RS_BADCRC)	smc->stats.rx_crc_errors++;
    }
    /* Let the MMU free the memory of this packet. */
    outw(MC_RELEASE, ioaddr + MMU_CMD);

    return;
}

/*====================================================================*/

static struct net_device_stats *smc_get_stats(struct net_device *dev)
{
    struct smc_private *smc = netdev_priv(dev);
    /* Nothing to update - the 91c92 is a pretty primative chip. */
    return &smc->stats;
}

/*======================================================================

    Calculate values for the hardware multicast filter hash table.

======================================================================*/

static void fill_multicast_tbl(int count, struct dev_mc_list *addrs,
			       u_char *multicast_table)
{
    struct dev_mc_list	*mc_addr;

    for (mc_addr = addrs;  mc_addr && count-- > 0;  mc_addr = mc_addr->next) {
	u_int position = ether_crc(6, mc_addr->dmi_addr);
#ifndef final_version		/* Verify multicast address. */
	if ((mc_addr->dmi_addr[0] & 1) == 0)
	    continue;
#endif
	multicast_table[position >> 29] |= 1 << ((position >> 26) & 7);
    }
}

/*======================================================================

    Set the receive mode.

    This routine is used by both the protocol level to notify us of
    promiscuous/multicast mode changes, and by the open/reset code to
    initialize the Rx registers.  We always set the multicast list and
    leave the receiver running.

======================================================================*/

static void set_rx_mode(struct net_device *dev)
{
    unsigned int ioaddr = dev->base_addr;
    struct smc_private *smc = netdev_priv(dev);
    u_int multicast_table[ 2 ] = { 0, };
    unsigned long flags;
    u_short rx_cfg_setting;

    if (dev->flags & IFF_PROMISC) {
	rx_cfg_setting = RxStripCRC | RxEnable | RxPromisc | RxAllMulti;
    } else if (dev->flags & IFF_ALLMULTI)
	rx_cfg_setting = RxStripCRC | RxEnable | RxAllMulti;
    else {
	if (dev->mc_count)  {
	    fill_multicast_tbl(dev->mc_count, dev->mc_list,
			       (u_char *)multicast_table);
	}
	rx_cfg_setting = RxStripCRC | RxEnable;
    }

    /* Load MC table and Rx setting into the chip without interrupts. */
    spin_lock_irqsave(&smc->lock, flags);
    SMC_SELECT_BANK(3);
    outl(multicast_table[0], ioaddr + MULTICAST0);
    outl(multicast_table[1], ioaddr + MULTICAST4);
    SMC_SELECT_BANK(0);
    outw(rx_cfg_setting, ioaddr + RCR);
    SMC_SELECT_BANK(2);
    spin_unlock_irqrestore(&smc->lock, flags);

    return;
}

/*======================================================================

    Senses when a card's config changes. Here, it's coax or TP.

======================================================================*/

static int s9k_config(struct net_device *dev, struct ifmap *map)
{
    struct smc_private *smc = netdev_priv(dev);
    if ((map->port != (u_char)(-1)) && (map->port != dev->if_port)) {
	if (smc->cfg & CFG_MII_SELECT)
	    return -EOPNOTSUPP;
	else if (map->port > 2)
	    return -EINVAL;
	dev->if_port = map->port;
	printk(KERN_INFO "%s: switched to %s port\n",
	       dev->name, if_names[dev->if_port]);
	smc_reset(dev);
    }
    return 0;
}

/*======================================================================

    Reset the chip, reloading every register that might be corrupted.

======================================================================*/

/*
  Set transceiver type, perhaps to something other than what the user
  specified in dev->if_port.
*/
static void smc_set_xcvr(struct net_device *dev, int if_port)
{
    struct smc_private *smc = netdev_priv(dev);
    unsigned int ioaddr = dev->base_addr;
    u_short saved_bank;

    saved_bank = inw(ioaddr + BANK_SELECT);
    SMC_SELECT_BANK(1);
    if (if_port == 2) {
	outw(smc->cfg | CFG_AUI_SELECT, ioaddr + CONFIG);
	if ((smc->manfid == MANFID_OSITECH) &&
	    (smc->cardid != PRODID_OSITECH_SEVEN))
	    set_bits(OSI_AUI_PWR, ioaddr - 0x10 + OSITECH_AUI_PWR);
	smc->media_status = ((dev->if_port == 0) ? 0x0001 : 0x0002);
    } else {
	outw(smc->cfg, ioaddr + CONFIG);
	if ((smc->manfid == MANFID_OSITECH) &&
	    (smc->cardid != PRODID_OSITECH_SEVEN))
	    mask_bits(~OSI_AUI_PWR, ioaddr - 0x10 + OSITECH_AUI_PWR);
	smc->media_status = ((dev->if_port == 0) ? 0x0012 : 0x4001);
    }
    SMC_SELECT_BANK(saved_bank);
}

static void smc_reset(struct net_device *dev)
{
    unsigned int ioaddr = dev->base_addr;
    struct smc_private *smc = netdev_priv(dev);
    int i;

    DEBUG(0, "%s: smc91c92 reset called.\n", dev->name);

    /* The first interaction must be a write to bring the chip out
       of sleep mode. */
    SMC_SELECT_BANK(0);
    /* Reset the chip. */
    outw(RCR_SOFTRESET, ioaddr + RCR);
    udelay(10);

    /* Clear the transmit and receive configuration registers. */
    outw(RCR_CLEAR, ioaddr + RCR);
    outw(TCR_CLEAR, ioaddr + TCR);

    /* Set the Window 1 control, configuration and station addr registers.
       No point in writing the I/O base register ;-> */
    SMC_SELECT_BANK(1);
    /* Automatically release successfully transmitted packets,
       Accept link errors, counter and Tx error interrupts. */
    outw(CTL_AUTO_RELEASE | CTL_TE_ENABLE | CTL_CR_ENABLE,
	 ioaddr + CONTROL);
    smc_set_xcvr(dev, dev->if_port);
    if ((smc->manfid == MANFID_OSITECH) &&
	(smc->cardid != PRODID_OSITECH_SEVEN))
	outw((dev->if_port == 2 ? OSI_AUI_PWR : 0) |
	     (inw(ioaddr-0x10+OSITECH_AUI_PWR) & 0xff00),
	     ioaddr - 0x10 + OSITECH_AUI_PWR);

    /* Fill in the physical address.  The databook is wrong about the order! */
    for (i = 0; i < 6; i += 2)
	outw((dev->dev_addr[i+1]<<8)+dev->dev_addr[i],
	     ioaddr + ADDR0 + i);

    /* Reset the MMU */
    SMC_SELECT_BANK(2);
    outw(MC_RESET, ioaddr + MMU_CMD);
    outw(0, ioaddr + INTERRUPT);

    /* Re-enable the chip. */
    SMC_SELECT_BANK(0);
    outw(((smc->cfg & CFG_MII_SELECT) ? 0 : TCR_MONCSN) |
	 TCR_ENABLE | TCR_PAD_EN | smc->duplex, ioaddr + TCR);
    set_rx_mode(dev);

    if (smc->cfg & CFG_MII_SELECT) {
	SMC_SELECT_BANK(3);

	/* Reset MII */
	mdio_write(dev, smc->mii_if.phy_id, 0, 0x8000);

	/* Advertise 100F, 100H, 10F, 10H */
	mdio_write(dev, smc->mii_if.phy_id, 4, 0x01e1);

	/* Restart MII autonegotiation */
	mdio_write(dev, smc->mii_if.phy_id, 0, 0x0000);
	mdio_write(dev, smc->mii_if.phy_id, 0, 0x1200);
    }

    /* Enable interrupts. */
    SMC_SELECT_BANK(2);
    outw((IM_EPH_INT | IM_RX_OVRN_INT | IM_RCV_INT) << 8,
	 ioaddr + INTERRUPT);
}

/*======================================================================

    Media selection timer routine

======================================================================*/

static void media_check(u_long arg)
{
    struct net_device *dev = (struct net_device *) arg;
    struct smc_private *smc = netdev_priv(dev);
    unsigned int ioaddr = dev->base_addr;
    u_short i, media, saved_bank;
    u_short link;
    unsigned long flags;

    spin_lock_irqsave(&smc->lock, flags);

    saved_bank = inw(ioaddr + BANK_SELECT);

    if (!netif_device_present(dev))
	goto reschedule;

    SMC_SELECT_BANK(2);

    /* need MC_RESET to keep the memory consistent. errata? */
    if (smc->rx_ovrn) {
	outw(MC_RESET, ioaddr + MMU_CMD);
	smc->rx_ovrn = 0;
    }
    i = inw(ioaddr + INTERRUPT);
    SMC_SELECT_BANK(0);
    media = inw(ioaddr + EPH) & EPH_LINK_OK;
    SMC_SELECT_BANK(1);
    media |= (inw(ioaddr + CONFIG) & CFG_AUI_SELECT) ? 2 : 1;

    /* Check for pending interrupt with watchdog flag set: with
       this, we can limp along even if the interrupt is blocked */
    if (smc->watchdog++ && ((i>>8) & i)) {
	if (!smc->fast_poll)
	    printk(KERN_INFO "%s: interrupt(s) dropped!\n", dev->name);
	smc_interrupt(dev->irq, dev);
	smc->fast_poll = HZ;
    }
    if (smc->fast_poll) {
	smc->fast_poll--;
	smc->media.expires = jiffies + HZ/100;
	add_timer(&smc->media);
	SMC_SELECT_BANK(saved_bank);
	spin_unlock_irqrestore(&smc->lock, flags);
	return;
    }

    if (smc->cfg & CFG_MII_SELECT) {
	if (smc->mii_if.phy_id < 0)
	    goto reschedule;

	SMC_SELECT_BANK(3);
	link = mdio_read(dev, smc->mii_if.phy_id, 1);
	if (!link || (link == 0xffff)) {
  	    printk(KERN_INFO "%s: MII is missing!\n", dev->name);
	    smc->mii_if.phy_id = -1;
	    goto reschedule;
	}

	link &= 0x0004;
	if (link != smc->link_status) {
	    u_short p = mdio_read(dev, smc->mii_if.phy_id, 5);
	    printk(KERN_INFO "%s: %s link beat\n", dev->name,
		(link) ? "found" : "lost");
	    smc->duplex = (((p & 0x0100) || ((p & 0x1c0) == 0x40))
			   ? TCR_FDUPLX : 0);
	    if (link) {
	        printk(KERN_INFO "%s: autonegotiation complete: "
		       "%sbaseT-%cD selected\n", dev->name,
		       ((p & 0x0180) ? "100" : "10"),
		       (smc->duplex ? 'F' : 'H'));
	    }
	    SMC_SELECT_BANK(0);
	    outw(inw(ioaddr + TCR) | smc->duplex, ioaddr + TCR);
	    smc->link_status = link;
	}
	goto reschedule;
    }

    /* Ignore collisions unless we've had no rx's recently */
    if (time_after(jiffies, dev->last_rx + HZ)) {
	if (smc->tx_err || (smc->media_status & EPH_16COL))
	    media |= EPH_16COL;
    }
    smc->tx_err = 0;

    if (media != smc->media_status) {
	if ((media & smc->media_status & 1) &&
	    ((smc->media_status ^ media) & EPH_LINK_OK))
	    printk(KERN_INFO "%s: %s link beat\n", dev->name,
		   (smc->media_status & EPH_LINK_OK ? "lost" : "found"));
	else if ((media & smc->media_status & 2) &&
		 ((smc->media_status ^ media) & EPH_16COL))
	    printk(KERN_INFO "%s: coax cable %s\n", dev->name,
		   (media & EPH_16COL ? "problem" : "ok"));
	if (dev->if_port == 0) {
	    if (media & 1) {
		if (media & EPH_LINK_OK)
		    printk(KERN_INFO "%s: flipped to 10baseT\n",
			   dev->name);
		else
		    smc_set_xcvr(dev, 2);
	    } else {
		if (media & EPH_16COL)
		    smc_set_xcvr(dev, 1);
		else
		    printk(KERN_INFO "%s: flipped to 10base2\n",
			   dev->name);
	    }
	}
	smc->media_status = media;
    }

reschedule:
    smc->media.expires = jiffies + HZ;
    add_timer(&smc->media);
    SMC_SELECT_BANK(saved_bank);
    spin_unlock_irqrestore(&smc->lock, flags);
}

static int smc_link_ok(struct net_device *dev)
{
    unsigned int ioaddr = dev->base_addr;
    struct smc_private *smc = netdev_priv(dev);

    if (smc->cfg & CFG_MII_SELECT) {
	return mii_link_ok(&smc->mii_if);
    } else {
        SMC_SELECT_BANK(0);
	return inw(ioaddr + EPH) & EPH_LINK_OK;
    }
}

static int smc_netdev_get_ecmd(struct net_device *dev, struct ethtool_cmd *ecmd)
{
    u16 tmp;
    unsigned int ioaddr = dev->base_addr;

    ecmd->supported = (SUPPORTED_TP | SUPPORTED_AUI |
	SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full);
		
    SMC_SELECT_BANK(1);
    tmp = inw(ioaddr + CONFIG);
    ecmd->port = (tmp & CFG_AUI_SELECT) ? PORT_AUI : PORT_TP;
    ecmd->transceiver = XCVR_INTERNAL;
    ecmd->speed = SPEED_10;
    ecmd->phy_address = ioaddr + MGMT;

    SMC_SELECT_BANK(0);
    tmp = inw(ioaddr + TCR);
    ecmd->duplex = (tmp & TCR_FDUPLX) ? DUPLEX_FULL : DUPLEX_HALF;

    return 0;
}

static int smc_netdev_set_ecmd(struct net_device *dev, struct ethtool_cmd *ecmd)
{
    u16 tmp;
    unsigned int ioaddr = dev->base_addr;

    if (ecmd->speed != SPEED_10)
    	return -EINVAL;
    if (ecmd->duplex != DUPLEX_HALF && ecmd->duplex != DUPLEX_FULL)
    	return -EINVAL;
    if (ecmd->port != PORT_TP && ecmd->port != PORT_AUI)
	return -EINVAL;
    if (ecmd->transceiver != XCVR_INTERNAL)
    	return -EINVAL;

    if (ecmd->port == PORT_AUI)
	smc_set_xcvr(dev, 1);
    else
	smc_set_xcvr(dev, 0);

    SMC_SELECT_BANK(0);
    tmp = inw(ioaddr + TCR);
    if (ecmd->duplex == DUPLEX_FULL)
	tmp |= TCR_FDUPLX;
    else
	tmp &= ~TCR_FDUPLX;
    outw(tmp, ioaddr + TCR);
	
    return 0;
}

static int check_if_running(struct net_device *dev)
{
	if (!netif_running(dev))
		return -EINVAL;
	return 0;
}

static void smc_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
}

static int smc_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct smc_private *smc = netdev_priv(dev);
	unsigned int ioaddr = dev->base_addr;
	u16 saved_bank = inw(ioaddr + BANK_SELECT);
	int ret;

	spin_lock_irq(&smc->lock);
	SMC_SELECT_BANK(3);
	if (smc->cfg & CFG_MII_SELECT)
		ret = mii_ethtool_gset(&smc->mii_if, ecmd);
	else
		ret = smc_netdev_get_ecmd(dev, ecmd);
	SMC_SELECT_BANK(saved_bank);
	spin_unlock_irq(&smc->lock);
	return ret;
}

static int smc_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct smc_private *smc = netdev_priv(dev);
	unsigned int ioaddr = dev->base_addr;
	u16 saved_bank = inw(ioaddr + BANK_SELECT);
	int ret;

	spin_lock_irq(&smc->lock);
	SMC_SELECT_BANK(3);
	if (smc->cfg & CFG_MII_SELECT)
		ret = mii_ethtool_sset(&smc->mii_if, ecmd);
	else
		ret = smc_netdev_set_ecmd(dev, ecmd);
	SMC_SELECT_BANK(saved_bank);
	spin_unlock_irq(&smc->lock);
	return ret;
}

static u32 smc_get_link(struct net_device *dev)
{
	struct smc_private *smc = netdev_priv(dev);
	unsigned int ioaddr = dev->base_addr;
	u16 saved_bank = inw(ioaddr + BANK_SELECT);
	u32 ret;

	spin_lock_irq(&smc->lock);
	SMC_SELECT_BANK(3);
	ret = smc_link_ok(dev);
	SMC_SELECT_BANK(saved_bank);
	spin_unlock_irq(&smc->lock);
	return ret;
}

#ifdef PCMCIA_DEBUG
static u32 smc_get_msglevel(struct net_device *dev)
{
	return pc_debug;
}

static void smc_set_msglevel(struct net_device *dev, u32 val)
{
	pc_debug = val;
}
#endif

static int smc_nway_reset(struct net_device *dev)
{
	struct smc_private *smc = netdev_priv(dev);
	if (smc->cfg & CFG_MII_SELECT) {
		unsigned int ioaddr = dev->base_addr;
		u16 saved_bank = inw(ioaddr + BANK_SELECT);
		int res;

		SMC_SELECT_BANK(3);
		res = mii_nway_restart(&smc->mii_if);
		SMC_SELECT_BANK(saved_bank);

		return res;
	} else
		return -EOPNOTSUPP;
}

static const struct ethtool_ops ethtool_ops = {
	.begin = check_if_running,
	.get_drvinfo = smc_get_drvinfo,
	.get_settings = smc_get_settings,
	.set_settings = smc_set_settings,
	.get_link = smc_get_link,
#ifdef PCMCIA_DEBUG
	.get_msglevel = smc_get_msglevel,
	.set_msglevel = smc_set_msglevel,
#endif
	.nway_reset = smc_nway_reset,
};

static int smc_ioctl (struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct smc_private *smc = netdev_priv(dev);
	struct mii_ioctl_data *mii = if_mii(rq);
	int rc = 0;
	u16 saved_bank;
	unsigned int ioaddr = dev->base_addr;

	if (!netif_running(dev))
		return -EINVAL;

	spin_lock_irq(&smc->lock);
	saved_bank = inw(ioaddr + BANK_SELECT);
	SMC_SELECT_BANK(3);
	rc = generic_mii_ioctl(&smc->mii_if, mii, cmd, NULL);
	SMC_SELECT_BANK(saved_bank);
	spin_unlock_irq(&smc->lock);
	return rc;
}

static struct pcmcia_device_id smc91c92_ids[] = {
	PCMCIA_PFC_DEVICE_MANF_CARD(0, 0x0109, 0x0501),
	PCMCIA_PFC_DEVICE_MANF_CARD(0, 0x0140, 0x000a),
	PCMCIA_PFC_DEVICE_PROD_ID123(0, "MEGAHERTZ", "CC/XJEM3288", "DATA/FAX/CELL ETHERNET MODEM", 0xf510db04, 0x04cd2988, 0x46a52d63),
	PCMCIA_PFC_DEVICE_PROD_ID123(0, "MEGAHERTZ", "CC/XJEM3336", "DATA/FAX/CELL ETHERNET MODEM", 0xf510db04, 0x0143b773, 0x46a52d63),
	PCMCIA_PFC_DEVICE_PROD_ID123(0, "MEGAHERTZ", "EM1144T", "PCMCIA MODEM", 0xf510db04, 0x856d66c8, 0xbd6c43ef),
	PCMCIA_PFC_DEVICE_PROD_ID123(0, "MEGAHERTZ", "XJEM1144/CCEM1144", "PCMCIA MODEM", 0xf510db04, 0x52d21e1e, 0xbd6c43ef),
	PCMCIA_PFC_DEVICE_PROD_ID12(0, "Gateway 2000", "XJEM3336", 0xdd9989be, 0x662c394c),
	PCMCIA_PFC_DEVICE_PROD_ID12(0, "MEGAHERTZ", "XJEM1144/CCEM1144", 0xf510db04, 0x52d21e1e),
	PCMCIA_PFC_DEVICE_PROD_ID12(0, "Ositech", "Trumpcard:Jack of Diamonds Modem+Ethernet", 0xc2f80cd, 0x656947b9),
	PCMCIA_PFC_DEVICE_PROD_ID12(0, "Ositech", "Trumpcard:Jack of Hearts Modem+Ethernet", 0xc2f80cd, 0xdc9ba5ed),
	PCMCIA_MFC_DEVICE_MANF_CARD(0, 0x016c, 0x0020),
	PCMCIA_DEVICE_MANF_CARD(0x016c, 0x0023),
	PCMCIA_DEVICE_PROD_ID123("BASICS by New Media Corporation", "Ethernet", "SMC91C94", 0x23c78a9d, 0x00b2e941, 0xcef397fb),
	PCMCIA_DEVICE_PROD_ID12("ARGOSY", "Fast Ethernet PCCard", 0x78f308dc, 0xdcea68bc),
	PCMCIA_DEVICE_PROD_ID12("dit Co., Ltd.", "PC Card-10/100BTX", 0xe59365c8, 0x6a2161d1),
	PCMCIA_DEVICE_PROD_ID12("DYNALINK", "L100C", 0x6a26d1cf, 0xc16ce9c5),
	PCMCIA_DEVICE_PROD_ID12("Farallon", "Farallon Enet", 0x58d93fc4, 0x244734e9),
	PCMCIA_DEVICE_PROD_ID12("Megahertz", "CC10BT/2", 0x33234748, 0x3c95b953),
	PCMCIA_DEVICE_PROD_ID12("MELCO/SMC", "LPC-TX", 0xa2cd8e6d, 0x42da662a),
	PCMCIA_DEVICE_PROD_ID12("Ositech", "Trumpcard:Four of Diamonds Ethernet", 0xc2f80cd, 0xb3466314),
	PCMCIA_DEVICE_PROD_ID12("Ositech", "Trumpcard:Seven of Diamonds Ethernet", 0xc2f80cd, 0x194b650a),
	PCMCIA_DEVICE_PROD_ID12("PCMCIA", "Fast Ethernet PCCard", 0x281f1c5d, 0xdcea68bc),
	PCMCIA_DEVICE_PROD_ID12("Psion", "10Mb Ethernet", 0x4ef00b21, 0x844be9e9),
	PCMCIA_DEVICE_PROD_ID12("SMC", "EtherEZ Ethernet 8020", 0xc4f8b18b, 0x4a0eeb2d),
	/* These conflict with other cards! */
	/* PCMCIA_DEVICE_MANF_CARD(0x0186, 0x0100), */
	/* PCMCIA_DEVICE_MANF_CARD(0x8a01, 0xc1ab), */
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, smc91c92_ids);

static struct pcmcia_driver smc91c92_cs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "smc91c92_cs",
	},
	.probe		= smc91c92_probe,
	.remove		= smc91c92_detach,
	.id_table       = smc91c92_ids,
	.suspend	= smc91c92_suspend,
	.resume		= smc91c92_resume,
};

static int __init init_smc91c92_cs(void)
{
	return pcmcia_register_driver(&smc91c92_cs_driver);
}

static void __exit exit_smc91c92_cs(void)
{
	pcmcia_unregister_driver(&smc91c92_cs_driver);
}

module_init(init_smc91c92_cs);
module_exit(exit_smc91c92_cs);
