/*======================================================================

    A PCMCIA ethernet driver for NS8390-based cards

    This driver supports the D-Link DE-650 and Linksys EthernetCard
    cards, the newer D-Link and Linksys combo cards, Accton EN2212
    cards, the RPTI EP400, and the PreMax PE-200 in non-shared-memory
    mode, and the IBM Credit Card Adapter, the NE4100, the Thomas
    Conrad ethernet card, and the Kingston KNE-PCM/x in shared-memory
    mode.  It will also handle the Socket EA card in either mode.

    Copyright (C) 1999 David A. Hinds -- dahinds@users.sourceforge.net

    pcnet_cs.c 1.153 2003/11/09 18:53:09
    
    The network driver code is based on Donald Becker's NE2000 code:

    Written 1992,1993 by Donald Becker.
    Copyright 1993 United States Government as represented by the
    Director, National Security Agency.  This software may be used and
    distributed according to the terms of the GNU General Public License,
    incorporated herein by reference.
    Donald Becker may be reached at becker@scyld.com

    Based also on Keith Moore's changes to Don Becker's code, for IBM
    CCAE support.  Drivers merged back together, and shared-memory
    Socket EA support added, by Ken Raeburn, September 1995.

======================================================================*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <../drivers/net/8390.h>

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>

#define PCNET_CMD	0x00
#define PCNET_DATAPORT	0x10	/* NatSemi-defined port window offset. */
#define PCNET_RESET	0x1f	/* Issue a read to reset, a write to clear. */
#define PCNET_MISC	0x18	/* For IBM CCAE and Socket EA cards */

#define PCNET_START_PG	0x40	/* First page of TX buffer */
#define PCNET_STOP_PG	0x80	/* Last page +1 of RX ring */

/* Socket EA cards have a larger packet buffer */
#define SOCKET_START_PG	0x01
#define SOCKET_STOP_PG	0xff

#define PCNET_RDC_TIMEOUT (2*HZ/100)	/* Max wait in jiffies for Tx RDC */

static char *if_names[] = { "auto", "10baseT", "10base2"};

#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
module_param(pc_debug, int, 0);
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
static char *version =
"pcnet_cs.c 1.153 2003/11/09 18:53:09 (David Hinds)";
#else
#define DEBUG(n, args...)
#endif

/*====================================================================*/

/* Module parameters */

MODULE_AUTHOR("David Hinds <dahinds@users.sourceforge.net>");
MODULE_DESCRIPTION("NE2000 compatible PCMCIA ethernet driver");
MODULE_LICENSE("GPL");

#define INT_MODULE_PARM(n, v) static int n = v; module_param(n, int, 0)

INT_MODULE_PARM(if_port,	1);	/* Transceiver type */
INT_MODULE_PARM(use_big_buf,	1);	/* use 64K packet buffer? */
INT_MODULE_PARM(mem_speed,	0);	/* shared mem speed, in ns */
INT_MODULE_PARM(delay_output,	0);	/* pause after xmit? */
INT_MODULE_PARM(delay_time,	4);	/* in usec */
INT_MODULE_PARM(use_shmem,	-1);	/* use shared memory? */
INT_MODULE_PARM(full_duplex,	0);	/* full duplex? */

/* Ugh!  Let the user hardwire the hardware address for queer cards */
static int hw_addr[6] = { 0, /* ... */ };
module_param_array(hw_addr, int, NULL, 0);

/*====================================================================*/

static void mii_phy_probe(struct net_device *dev);
static void pcnet_config(dev_link_t *link);
static void pcnet_release(dev_link_t *link);
static int pcnet_event(event_t event, int priority,
		       event_callback_args_t *args);
static int pcnet_open(struct net_device *dev);
static int pcnet_close(struct net_device *dev);
static int ei_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static struct ethtool_ops netdev_ethtool_ops;
static irqreturn_t ei_irq_wrapper(int irq, void *dev_id, struct pt_regs *regs);
static void ei_watchdog(u_long arg);
static void pcnet_reset_8390(struct net_device *dev);
static int set_config(struct net_device *dev, struct ifmap *map);
static int setup_shmem_window(dev_link_t *link, int start_pg,
			      int stop_pg, int cm_offset);
static int setup_dma_config(dev_link_t *link, int start_pg,
			    int stop_pg);

static dev_link_t *pcnet_attach(void);
static void pcnet_detach(dev_link_t *);

static dev_info_t dev_info = "pcnet_cs";
static dev_link_t *dev_list;

/*====================================================================*/

typedef struct hw_info_t {
    u_int	offset;
    u_char	a0, a1, a2;
    u_int	flags;
} hw_info_t;

#define DELAY_OUTPUT	0x01
#define HAS_MISC_REG	0x02
#define USE_BIG_BUF	0x04
#define HAS_IBM_MISC	0x08
#define IS_DL10019	0x10
#define IS_DL10022	0x20
#define HAS_MII		0x40
#define USE_SHMEM	0x80	/* autodetected */

#define AM79C9XX_HOME_PHY	0x00006B90  /* HomePNA PHY */
#define AM79C9XX_ETH_PHY	0x00006B70  /* 10baseT PHY */
#define MII_PHYID_REV_MASK	0xfffffff0
#define MII_PHYID_REG1		0x02
#define MII_PHYID_REG2		0x03

static hw_info_t hw_info[] = {
    { /* Accton EN2212 */ 0x0ff0, 0x00, 0x00, 0xe8, DELAY_OUTPUT }, 
    { /* Allied Telesis LA-PCM */ 0x0ff0, 0x00, 0x00, 0xf4, 0 },
    { /* APEX MultiCard */ 0x03f4, 0x00, 0x20, 0xe5, 0 },
    { /* ASANTE FriendlyNet */ 0x4910, 0x00, 0x00, 0x94,
      DELAY_OUTPUT | HAS_IBM_MISC },
    { /* Danpex EN-6200P2 */ 0x0110, 0x00, 0x40, 0xc7, 0 },
    { /* DataTrek NetCard */ 0x0ff0, 0x00, 0x20, 0xe8, 0 },
    { /* Dayna CommuniCard E */ 0x0110, 0x00, 0x80, 0x19, 0 },
    { /* D-Link DE-650 */ 0x0040, 0x00, 0x80, 0xc8, 0 },
    { /* EP-210 Ethernet */ 0x0110, 0x00, 0x40, 0x33, 0 },
    { /* EP4000 Ethernet */ 0x01c0, 0x00, 0x00, 0xb4, 0 },
    { /* Epson EEN10B */ 0x0ff0, 0x00, 0x00, 0x48,
      HAS_MISC_REG | HAS_IBM_MISC },
    { /* ELECOM Laneed LD-CDWA */ 0xb8, 0x08, 0x00, 0x42, 0 },
    { /* Hypertec Ethernet */ 0x01c0, 0x00, 0x40, 0x4c, 0 },
    { /* IBM CCAE */ 0x0ff0, 0x08, 0x00, 0x5a,
      HAS_MISC_REG | HAS_IBM_MISC },
    { /* IBM CCAE */ 0x0ff0, 0x00, 0x04, 0xac,
      HAS_MISC_REG | HAS_IBM_MISC },
    { /* IBM CCAE */ 0x0ff0, 0x00, 0x06, 0x29,
      HAS_MISC_REG | HAS_IBM_MISC },
    { /* IBM FME */ 0x0374, 0x08, 0x00, 0x5a,
      HAS_MISC_REG | HAS_IBM_MISC },
    { /* IBM FME */ 0x0374, 0x00, 0x04, 0xac,
      HAS_MISC_REG | HAS_IBM_MISC },
    { /* Kansai KLA-PCM/T */ 0x0ff0, 0x00, 0x60, 0x87,
      HAS_MISC_REG | HAS_IBM_MISC },
    { /* NSC DP83903 */ 0x0374, 0x08, 0x00, 0x17,
      HAS_MISC_REG | HAS_IBM_MISC },
    { /* NSC DP83903 */ 0x0374, 0x00, 0xc0, 0xa8,
      HAS_MISC_REG | HAS_IBM_MISC },
    { /* NSC DP83903 */ 0x0374, 0x00, 0xa0, 0xb0,
      HAS_MISC_REG | HAS_IBM_MISC },
    { /* NSC DP83903 */ 0x0198, 0x00, 0x20, 0xe0,
      HAS_MISC_REG | HAS_IBM_MISC },
    { /* I-O DATA PCLA/T */ 0x0ff0, 0x00, 0xa0, 0xb0, 0 },
    { /* Katron PE-520 */ 0x0110, 0x00, 0x40, 0xf6, 0 },
    { /* Kingston KNE-PCM/x */ 0x0ff0, 0x00, 0xc0, 0xf0,
      HAS_MISC_REG | HAS_IBM_MISC },
    { /* Kingston KNE-PCM/x */ 0x0ff0, 0xe2, 0x0c, 0x0f,
      HAS_MISC_REG | HAS_IBM_MISC },
    { /* Kingston KNE-PC2 */ 0x0180, 0x00, 0xc0, 0xf0, 0 },
    { /* Maxtech PCN2000 */ 0x5000, 0x00, 0x00, 0xe8, 0 },
    { /* NDC Instant-Link */ 0x003a, 0x00, 0x80, 0xc6, 0 },
    { /* NE2000 Compatible */ 0x0ff0, 0x00, 0xa0, 0x0c, 0 },
    { /* Network General Sniffer */ 0x0ff0, 0x00, 0x00, 0x65,
      HAS_MISC_REG | HAS_IBM_MISC },
    { /* Panasonic VEL211 */ 0x0ff0, 0x00, 0x80, 0x45, 
      HAS_MISC_REG | HAS_IBM_MISC },
    { /* PreMax PE-200 */ 0x07f0, 0x00, 0x20, 0xe0, 0 },
    { /* RPTI EP400 */ 0x0110, 0x00, 0x40, 0x95, 0 },
    { /* SCM Ethernet */ 0x0ff0, 0x00, 0x20, 0xcb, 0 },
    { /* Socket EA */ 0x4000, 0x00, 0xc0, 0x1b,
      DELAY_OUTPUT | HAS_MISC_REG | USE_BIG_BUF },
    { /* Socket LP-E CF+ */ 0x01c0, 0x00, 0xc0, 0x1b, 0 },
    { /* SuperSocket RE450T */ 0x0110, 0x00, 0xe0, 0x98, 0 },
    { /* Volktek NPL-402CT */ 0x0060, 0x00, 0x40, 0x05, 0 },
    { /* NEC PC-9801N-J12 */ 0x0ff0, 0x00, 0x00, 0x4c, 0 },
    { /* PCMCIA Technology OEM */ 0x01c8, 0x00, 0xa0, 0x0c, 0 }
};

#define NR_INFO		(sizeof(hw_info)/sizeof(hw_info_t))

static hw_info_t default_info = { 0, 0, 0, 0, 0 };
static hw_info_t dl10019_info = { 0, 0, 0, 0, IS_DL10019|HAS_MII };
static hw_info_t dl10022_info = { 0, 0, 0, 0, IS_DL10022|HAS_MII };

typedef struct pcnet_dev_t {
    dev_link_t		link;
    dev_node_t		node;
    u_int		flags;
    void		__iomem *base;
    struct timer_list	watchdog;
    int			stale, fast_poll;
    u_char		phy_id;
    u_char		eth_phy, pna_phy;
    u_short		link_status;
    u_long		mii_reset;
} pcnet_dev_t;

static inline pcnet_dev_t *PRIV(struct net_device *dev)
{
	char *p = netdev_priv(dev);
	return (pcnet_dev_t *)(p + sizeof(struct ei_device));
}

/*======================================================================

    pcnet_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

======================================================================*/

static dev_link_t *pcnet_attach(void)
{
    pcnet_dev_t *info;
    dev_link_t *link;
    struct net_device *dev;
    client_reg_t client_reg;
    int ret;

    DEBUG(0, "pcnet_attach()\n");

    /* Create new ethernet device */
    dev = __alloc_ei_netdev(sizeof(pcnet_dev_t));
    if (!dev) return NULL;
    info = PRIV(dev);
    link = &info->link;
    link->priv = dev;

    link->irq.Attributes = IRQ_TYPE_EXCLUSIVE;
    link->irq.IRQInfo1 = IRQ_LEVEL_ID;
    link->conf.Attributes = CONF_ENABLE_IRQ;
    link->conf.IntType = INT_MEMORY_AND_IO;

    SET_MODULE_OWNER(dev);
    dev->open = &pcnet_open;
    dev->stop = &pcnet_close;
    dev->set_config = &set_config;

    /* Register with Card Services */
    link->next = dev_list;
    dev_list = link;
    client_reg.dev_info = &dev_info;
    client_reg.EventMask =
	CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL |
	CS_EVENT_RESET_PHYSICAL | CS_EVENT_CARD_RESET |
	CS_EVENT_PM_SUSPEND | CS_EVENT_PM_RESUME;
    client_reg.event_handler = &pcnet_event;
    client_reg.Version = 0x0210;
    client_reg.event_callback_args.client_data = link;
    ret = pcmcia_register_client(&link->handle, &client_reg);
    if (ret != CS_SUCCESS) {
	cs_error(link->handle, RegisterClient, ret);
	pcnet_detach(link);
	return NULL;
    }

    return link;
} /* pcnet_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void pcnet_detach(dev_link_t *link)
{
    struct net_device *dev = link->priv;
    dev_link_t **linkp;

    DEBUG(0, "pcnet_detach(0x%p)\n", link);

    /* Locate device structure */
    for (linkp = &dev_list; *linkp; linkp = &(*linkp)->next)
	if (*linkp == link) break;
    if (*linkp == NULL)
	return;

    if (link->dev)
	unregister_netdev(dev);

    if (link->state & DEV_CONFIG)
	pcnet_release(link);

    if (link->handle)
	pcmcia_deregister_client(link->handle);

    /* Unlink device structure, free bits */
    *linkp = link->next;
    free_netdev(dev);
} /* pcnet_detach */

/*======================================================================

    This probes for a card's hardware address, for card types that
    encode this information in their CIS.

======================================================================*/

static hw_info_t *get_hwinfo(dev_link_t *link)
{
    struct net_device *dev = link->priv;
    win_req_t req;
    memreq_t mem;
    u_char __iomem *base, *virt;
    int i, j;

    /* Allocate a small memory window */
    req.Attributes = WIN_DATA_WIDTH_8|WIN_MEMORY_TYPE_AM|WIN_ENABLE;
    req.Base = 0; req.Size = 0;
    req.AccessSpeed = 0;
    i = pcmcia_request_window(&link->handle, &req, &link->win);
    if (i != CS_SUCCESS) {
	cs_error(link->handle, RequestWindow, i);
	return NULL;
    }

    virt = ioremap(req.Base, req.Size);
    mem.Page = 0;
    for (i = 0; i < NR_INFO; i++) {
	mem.CardOffset = hw_info[i].offset & ~(req.Size-1);
	pcmcia_map_mem_page(link->win, &mem);
	base = &virt[hw_info[i].offset & (req.Size-1)];
	if ((readb(base+0) == hw_info[i].a0) &&
	    (readb(base+2) == hw_info[i].a1) &&
	    (readb(base+4) == hw_info[i].a2))
	    break;
    }
    if (i < NR_INFO) {
	for (j = 0; j < 6; j++)
	    dev->dev_addr[j] = readb(base + (j<<1));
    }
    
    iounmap(virt);
    j = pcmcia_release_window(link->win);
    if (j != CS_SUCCESS)
	cs_error(link->handle, ReleaseWindow, j);
    return (i < NR_INFO) ? hw_info+i : NULL;
} /* get_hwinfo */

/*======================================================================

    This probes for a card's hardware address by reading the PROM.
    It checks the address against a list of known types, then falls
    back to a simple NE2000 clone signature check.

======================================================================*/

static hw_info_t *get_prom(dev_link_t *link)
{
    struct net_device *dev = link->priv;
    kio_addr_t ioaddr = dev->base_addr;
    u_char prom[32];
    int i, j;

    /* This is lifted straight from drivers/net/ne.c */
    struct {
	u_char value, offset;
    } program_seq[] = {
	{E8390_NODMA+E8390_PAGE0+E8390_STOP, E8390_CMD}, /* Select page 0*/
	{0x48,	EN0_DCFG},	/* Set byte-wide (0x48) access. */
	{0x00,	EN0_RCNTLO},	/* Clear the count regs. */
	{0x00,	EN0_RCNTHI},
	{0x00,	EN0_IMR},	/* Mask completion irq. */
	{0xFF,	EN0_ISR},
	{E8390_RXOFF, EN0_RXCR},	/* 0x20  Set to monitor */
	{E8390_TXOFF, EN0_TXCR},	/* 0x02  and loopback mode. */
	{32,	EN0_RCNTLO},
	{0x00,	EN0_RCNTHI},
	{0x00,	EN0_RSARLO},	/* DMA starting at 0x0000. */
	{0x00,	EN0_RSARHI},
	{E8390_RREAD+E8390_START, E8390_CMD},
    };

    pcnet_reset_8390(dev);
    mdelay(10);

    for (i = 0; i < sizeof(program_seq)/sizeof(program_seq[0]); i++)
	outb_p(program_seq[i].value, ioaddr + program_seq[i].offset);

    for (i = 0; i < 32; i++)
	prom[i] = inb(ioaddr + PCNET_DATAPORT);
    for (i = 0; i < NR_INFO; i++) {
	if ((prom[0] == hw_info[i].a0) &&
	    (prom[2] == hw_info[i].a1) &&
	    (prom[4] == hw_info[i].a2))
	    break;
    }
    if ((i < NR_INFO) || ((prom[28] == 0x57) && (prom[30] == 0x57))) {
	for (j = 0; j < 6; j++)
	    dev->dev_addr[j] = prom[j<<1];
	return (i < NR_INFO) ? hw_info+i : &default_info;
    }
    return NULL;
} /* get_prom */

/*======================================================================

    For DL10019 based cards, like the Linksys EtherFast

======================================================================*/

static hw_info_t *get_dl10019(dev_link_t *link)
{
    struct net_device *dev = link->priv;
    int i;
    u_char sum;

    for (sum = 0, i = 0x14; i < 0x1c; i++)
	sum += inb_p(dev->base_addr + i);
    if (sum != 0xff)
	return NULL;
    for (i = 0; i < 6; i++)
	dev->dev_addr[i] = inb_p(dev->base_addr + 0x14 + i);
    i = inb(dev->base_addr + 0x1f);
    return ((i == 0x91)||(i == 0x99)) ? &dl10022_info : &dl10019_info;
}

/*======================================================================

    For Asix AX88190 based cards

======================================================================*/

static hw_info_t *get_ax88190(dev_link_t *link)
{
    struct net_device *dev = link->priv;
    kio_addr_t ioaddr = dev->base_addr;
    int i, j;

    /* Not much of a test, but the alternatives are messy */
    if (link->conf.ConfigBase != 0x03c0)
	return NULL;

    outb_p(0x01, ioaddr + EN0_DCFG);	/* Set word-wide access. */
    outb_p(0x00, ioaddr + EN0_RSARLO);	/* DMA starting at 0x0400. */
    outb_p(0x04, ioaddr + EN0_RSARHI);
    outb_p(E8390_RREAD+E8390_START, ioaddr + E8390_CMD);

    for (i = 0; i < 6; i += 2) {
	j = inw(ioaddr + PCNET_DATAPORT);
	dev->dev_addr[i] = j & 0xff;
	dev->dev_addr[i+1] = j >> 8;
    }
    printk(KERN_NOTICE "pcnet_cs: this is an AX88190 card!\n");
    printk(KERN_NOTICE "pcnet_cs: use axnet_cs instead.\n");
    return NULL;
}

/*======================================================================

    This should be totally unnecessary... but when we can't figure
    out the hardware address any other way, we'll let the user hard
    wire it when the module is initialized.

======================================================================*/

static hw_info_t *get_hwired(dev_link_t *link)
{
    struct net_device *dev = link->priv;
    int i;

    for (i = 0; i < 6; i++)
	if (hw_addr[i] != 0) break;
    if (i == 6)
	return NULL;

    for (i = 0; i < 6; i++)
	dev->dev_addr[i] = hw_addr[i];

    return &default_info;
} /* get_hwired */

/*======================================================================

    pcnet_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    ethernet device available to the system.

======================================================================*/

#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

static int try_io_port(dev_link_t *link)
{
    int j, ret;
    if (link->io.NumPorts1 == 32) {
	link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
	if (link->io.NumPorts2 > 0) {
	    /* for master/slave multifunction cards */
	    link->io.Attributes2 = IO_DATA_PATH_WIDTH_8;
	    link->irq.Attributes = 
		IRQ_TYPE_DYNAMIC_SHARING|IRQ_FIRST_SHARED;
	}
    } else {
	/* This should be two 16-port windows */
	link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
	link->io.Attributes2 = IO_DATA_PATH_WIDTH_16;
    }
    if (link->io.BasePort1 == 0) {
	link->io.IOAddrLines = 16;
	for (j = 0; j < 0x400; j += 0x20) {
	    link->io.BasePort1 = j ^ 0x300;
	    link->io.BasePort2 = (j ^ 0x300) + 0x10;
	    ret = pcmcia_request_io(link->handle, &link->io);
	    if (ret == CS_SUCCESS) return ret;
	}
	return ret;
    } else {
	return pcmcia_request_io(link->handle, &link->io);
    }
}

static void pcnet_config(dev_link_t *link)
{
    client_handle_t handle = link->handle;
    struct net_device *dev = link->priv;
    pcnet_dev_t *info = PRIV(dev);
    tuple_t tuple;
    cisparse_t parse;
    int i, last_ret, last_fn, start_pg, stop_pg, cm_offset;
    int manfid = 0, prodid = 0, has_shmem = 0;
    u_short buf[64];
    config_info_t conf;
    hw_info_t *hw_info;

    DEBUG(0, "pcnet_config(0x%p)\n", link);

    tuple.Attributes = 0;
    tuple.TupleData = (cisdata_t *)buf;
    tuple.TupleDataMax = sizeof(buf);
    tuple.TupleOffset = 0;
    tuple.DesiredTuple = CISTPL_CONFIG;
    CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
    CS_CHECK(GetTupleData, pcmcia_get_tuple_data(handle, &tuple));
    CS_CHECK(ParseTuple, pcmcia_parse_tuple(handle, &tuple, &parse));
    link->conf.ConfigBase = parse.config.base;
    link->conf.Present = parse.config.rmask[0];

    /* Configure card */
    link->state |= DEV_CONFIG;

    /* Look up current Vcc */
    CS_CHECK(GetConfigurationInfo, pcmcia_get_configuration_info(handle, &conf));
    link->conf.Vcc = conf.Vcc;

    tuple.DesiredTuple = CISTPL_MANFID;
    tuple.Attributes = TUPLE_RETURN_COMMON;
    if ((pcmcia_get_first_tuple(handle, &tuple) == CS_SUCCESS) &&
 	(pcmcia_get_tuple_data(handle, &tuple) == CS_SUCCESS)) {
	manfid = le16_to_cpu(buf[0]);
	prodid = le16_to_cpu(buf[1]);
    }
    
    tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
    tuple.Attributes = 0;
    CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
    while (last_ret == CS_SUCCESS) {
	cistpl_cftable_entry_t *cfg = &(parse.cftable_entry);
	cistpl_io_t *io = &(parse.cftable_entry.io);
	
	if (pcmcia_get_tuple_data(handle, &tuple) != 0 ||
			pcmcia_parse_tuple(handle, &tuple, &parse) != 0 ||
			cfg->index == 0 || cfg->io.nwin == 0)
		goto next_entry;
	
	link->conf.ConfigIndex = cfg->index;
	/* For multifunction cards, by convention, we configure the
	   network function with window 0, and serial with window 1 */
	if (io->nwin > 1) {
	    i = (io->win[1].len > io->win[0].len);
	    link->io.BasePort2 = io->win[1-i].base;
	    link->io.NumPorts2 = io->win[1-i].len;
	} else {
	    i = link->io.NumPorts2 = 0;
	}
	has_shmem = ((cfg->mem.nwin == 1) &&
		     (cfg->mem.win[0].len >= 0x4000));
	link->io.BasePort1 = io->win[i].base;
	link->io.NumPorts1 = io->win[i].len;
	link->io.IOAddrLines = io->flags & CISTPL_IO_LINES_MASK;
	if (link->io.NumPorts1 + link->io.NumPorts2 >= 32) {
	    last_ret = try_io_port(link);
	    if (last_ret == CS_SUCCESS) break;
	}
    next_entry:
	last_ret = pcmcia_get_next_tuple(handle, &tuple);
    }
    if (last_ret != CS_SUCCESS) {
	cs_error(handle, RequestIO, last_ret);
	goto failed;
    }

    CS_CHECK(RequestIRQ, pcmcia_request_irq(handle, &link->irq));
    
    if (link->io.NumPorts2 == 8) {
	link->conf.Attributes |= CONF_ENABLE_SPKR;
	link->conf.Status = CCSR_AUDIO_ENA;
    }
    if ((manfid == MANFID_IBM) &&
	(prodid == PRODID_IBM_HOME_AND_AWAY))
	link->conf.ConfigIndex |= 0x10;
    
    CS_CHECK(RequestConfiguration, pcmcia_request_configuration(handle, &link->conf));
    dev->irq = link->irq.AssignedIRQ;
    dev->base_addr = link->io.BasePort1;
    if (info->flags & HAS_MISC_REG) {
	if ((if_port == 1) || (if_port == 2))
	    dev->if_port = if_port;
	else
	    printk(KERN_NOTICE "pcnet_cs: invalid if_port requested\n");
    } else {
	dev->if_port = 0;
    }

    hw_info = get_hwinfo(link);
    if (hw_info == NULL)
	hw_info = get_prom(link);
    if (hw_info == NULL)
	hw_info = get_dl10019(link);
    if (hw_info == NULL)
	hw_info = get_ax88190(link);
    if (hw_info == NULL)
	hw_info = get_hwired(link);
    
    if (hw_info == NULL) {
	printk(KERN_NOTICE "pcnet_cs: unable to read hardware net"
	       " address for io base %#3lx\n", dev->base_addr);
	goto failed;
    }

    info->flags = hw_info->flags;
    /* Check for user overrides */
    info->flags |= (delay_output) ? DELAY_OUTPUT : 0;
    if ((manfid == MANFID_SOCKET) &&
	((prodid == PRODID_SOCKET_LPE) ||
	 (prodid == PRODID_SOCKET_LPE_CF) ||
	 (prodid == PRODID_SOCKET_EIO)))
	info->flags &= ~USE_BIG_BUF;
    if (!use_big_buf)
	info->flags &= ~USE_BIG_BUF;
    
    if (info->flags & USE_BIG_BUF) {
	start_pg = SOCKET_START_PG;
	stop_pg = SOCKET_STOP_PG;
	cm_offset = 0x10000;
    } else {
	start_pg = PCNET_START_PG;
	stop_pg = PCNET_STOP_PG;
	cm_offset = 0;
    }

    /* has_shmem is ignored if use_shmem != -1 */
    if ((use_shmem == 0) || (!has_shmem && (use_shmem == -1)) ||
	(setup_shmem_window(link, start_pg, stop_pg, cm_offset) != 0))
	setup_dma_config(link, start_pg, stop_pg);

    ei_status.name = "NE2000";
    ei_status.word16 = 1;
    ei_status.reset_8390 = &pcnet_reset_8390;

    SET_ETHTOOL_OPS(dev, &netdev_ethtool_ops);

    if (info->flags & (IS_DL10019|IS_DL10022)) {
	u_char id = inb(dev->base_addr + 0x1a);
	dev->do_ioctl = &ei_ioctl;
	mii_phy_probe(dev);
	if ((id == 0x30) && !info->pna_phy && (info->eth_phy == 4))
	    info->eth_phy = 0;
    }

    link->dev = &info->node;
    link->state &= ~DEV_CONFIG_PENDING;
    SET_NETDEV_DEV(dev, &handle_to_dev(handle));

#ifdef CONFIG_NET_POLL_CONTROLLER
    dev->poll_controller = ei_poll;
#endif

    if (register_netdev(dev) != 0) {
	printk(KERN_NOTICE "pcnet_cs: register_netdev() failed\n");
	link->dev = NULL;
	goto failed;
    }

    strcpy(info->node.dev_name, dev->name);

    if (info->flags & (IS_DL10019|IS_DL10022)) {
	u_char id = inb(dev->base_addr + 0x1a);
	printk(KERN_INFO "%s: NE2000 (DL100%d rev %02x): ",
	       dev->name, ((info->flags & IS_DL10022) ? 22 : 19), id);
	if (info->pna_phy)
	    printk("PNA, ");
    } else {
	printk(KERN_INFO "%s: NE2000 Compatible: ", dev->name);
    }
    printk("io %#3lx, irq %d,", dev->base_addr, dev->irq);
    if (info->flags & USE_SHMEM)
	printk (" mem %#5lx,", dev->mem_start);
    if (info->flags & HAS_MISC_REG)
	printk(" %s xcvr,", if_names[dev->if_port]);
    printk(" hw_addr ");
    for (i = 0; i < 6; i++)
	printk("%02X%s", dev->dev_addr[i], ((i<5) ? ":" : "\n"));
    return;

cs_failed:
    cs_error(link->handle, last_fn, last_ret);
failed:
    pcnet_release(link);
    link->state &= ~DEV_CONFIG_PENDING;
    return;
} /* pcnet_config */

/*======================================================================

    After a card is removed, pcnet_release() will unregister the net
    device, and release the PCMCIA configuration.  If the device is
    still open, this will be postponed until it is closed.

======================================================================*/

static void pcnet_release(dev_link_t *link)
{
    pcnet_dev_t *info = PRIV(link->priv);

    DEBUG(0, "pcnet_release(0x%p)\n", link);

    if (info->flags & USE_SHMEM) {
	iounmap(info->base);
	pcmcia_release_window(link->win);
    }
    pcmcia_release_configuration(link->handle);
    pcmcia_release_io(link->handle, &link->io);
    pcmcia_release_irq(link->handle, &link->irq);

    link->state &= ~DEV_CONFIG;
}

/*======================================================================

    The card status event handler.  Mostly, this schedules other
    stuff to run after an event is received.  A CARD_REMOVAL event
    also sets some flags to discourage the net drivers from trying
    to talk to the card any more.

======================================================================*/

static int pcnet_event(event_t event, int priority,
		       event_callback_args_t *args)
{
    dev_link_t *link = args->client_data;
    struct net_device *dev = link->priv;

    DEBUG(2, "pcnet_event(0x%06x)\n", event);

    switch (event) {
    case CS_EVENT_CARD_REMOVAL:
	link->state &= ~DEV_PRESENT;
	if (link->state & DEV_CONFIG)
	    netif_device_detach(dev);
	break;
    case CS_EVENT_CARD_INSERTION:
	link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
	pcnet_config(link);
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
		pcnet_reset_8390(dev);
		NS8390_init(dev, 1);
		netif_device_attach(dev);
	    }
	}
	break;
    }
    return 0;
} /* pcnet_event */

/*======================================================================

    MII interface support for DL10019 and DL10022 based cards

    On the DL10019, the MII IO direction bit is 0x10; on the DL10022
    it is 0x20.  Setting both bits seems to work on both card types.

======================================================================*/

#define DLINK_GPIO		0x1c
#define DLINK_DIAG		0x1d
#define DLINK_EEPROM		0x1e

#define MDIO_SHIFT_CLK		0x80
#define MDIO_DATA_OUT		0x40
#define MDIO_DIR_WRITE		0x30
#define MDIO_DATA_WRITE0	(MDIO_DIR_WRITE)
#define MDIO_DATA_WRITE1	(MDIO_DIR_WRITE | MDIO_DATA_OUT)
#define MDIO_DATA_READ		0x10
#define MDIO_MASK		0x0f

static void mdio_sync(kio_addr_t addr)
{
    int bits, mask = inb(addr) & MDIO_MASK;
    for (bits = 0; bits < 32; bits++) {
	outb(mask | MDIO_DATA_WRITE1, addr);
	outb(mask | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, addr);
    }
}

static int mdio_read(kio_addr_t addr, int phy_id, int loc)
{
    u_int cmd = (0x06<<10)|(phy_id<<5)|loc;
    int i, retval = 0, mask = inb(addr) & MDIO_MASK;

    mdio_sync(addr);
    for (i = 13; i >= 0; i--) {
	int dat = (cmd&(1<<i)) ? MDIO_DATA_WRITE1 : MDIO_DATA_WRITE0;
	outb(mask | dat, addr);
	outb(mask | dat | MDIO_SHIFT_CLK, addr);
    }
    for (i = 19; i > 0; i--) {
	outb(mask, addr);
	retval = (retval << 1) | ((inb(addr) & MDIO_DATA_READ) != 0);
	outb(mask | MDIO_SHIFT_CLK, addr);
    }
    return (retval>>1) & 0xffff;
}

static void mdio_write(kio_addr_t addr, int phy_id, int loc, int value)
{
    u_int cmd = (0x05<<28)|(phy_id<<23)|(loc<<18)|(1<<17)|value;
    int i, mask = inb(addr) & MDIO_MASK;

    mdio_sync(addr);
    for (i = 31; i >= 0; i--) {
	int dat = (cmd&(1<<i)) ? MDIO_DATA_WRITE1 : MDIO_DATA_WRITE0;
	outb(mask | dat, addr);
	outb(mask | dat | MDIO_SHIFT_CLK, addr);
    }
    for (i = 1; i >= 0; i--) {
	outb(mask, addr);
	outb(mask | MDIO_SHIFT_CLK, addr);
    }
}

static void mdio_reset(kio_addr_t addr, int phy_id)
{
    outb_p(0x08, addr);
    outb_p(0x0c, addr);
    outb_p(0x08, addr);
    outb_p(0x0c, addr);
    outb_p(0x00, addr);
}

/*======================================================================

    EEPROM access routines for DL10019 and DL10022 based cards

======================================================================*/

#define EE_EEP		0x40
#define EE_ASIC		0x10
#define EE_CS		0x08
#define EE_CK		0x04
#define EE_DO		0x02
#define EE_DI		0x01
#define EE_ADOT		0x01	/* DataOut for ASIC */
#define EE_READ_CMD	0x06

#define DL19FDUPLX	0x0400	/* DL10019 Full duplex mode */

static int read_eeprom(kio_addr_t ioaddr, int location)
{
    int i, retval = 0;
    kio_addr_t ee_addr = ioaddr + DLINK_EEPROM;
    int read_cmd = location | (EE_READ_CMD << 8);

    outb(0, ee_addr);
    outb(EE_EEP|EE_CS, ee_addr);

    /* Shift the read command bits out. */
    for (i = 10; i >= 0; i--) {
	short dataval = (read_cmd & (1 << i)) ? EE_DO : 0;
	outb_p(EE_EEP|EE_CS|dataval, ee_addr);
	outb_p(EE_EEP|EE_CS|dataval|EE_CK, ee_addr);
    }
    outb(EE_EEP|EE_CS, ee_addr);

    for (i = 16; i > 0; i--) {
	outb_p(EE_EEP|EE_CS | EE_CK, ee_addr);
	retval = (retval << 1) | ((inb(ee_addr) & EE_DI) ? 1 : 0);
	outb_p(EE_EEP|EE_CS, ee_addr);
    }

    /* Terminate the EEPROM access. */
    outb(0, ee_addr);
    return retval;
}

/*
    The internal ASIC registers can be changed by EEPROM READ access
    with EE_ASIC bit set.
    In ASIC mode, EE_ADOT is used to output the data to the ASIC.
*/

static void write_asic(kio_addr_t ioaddr, int location, short asic_data)
{
	int i;
	kio_addr_t ee_addr = ioaddr + DLINK_EEPROM;
	short dataval;
	int read_cmd = location | (EE_READ_CMD << 8);

	asic_data |= read_eeprom(ioaddr, location);

	outb(0, ee_addr);
	outb(EE_ASIC|EE_CS|EE_DI, ee_addr);

	read_cmd = read_cmd >> 1;

	/* Shift the read command bits out. */
	for (i = 9; i >= 0; i--) {
		dataval = (read_cmd & (1 << i)) ? EE_DO : 0;
		outb_p(EE_ASIC|EE_CS|EE_DI|dataval, ee_addr);
		outb_p(EE_ASIC|EE_CS|EE_DI|dataval|EE_CK, ee_addr);
		outb_p(EE_ASIC|EE_CS|EE_DI|dataval, ee_addr);
	}
	// sync
	outb(EE_ASIC|EE_CS, ee_addr);
	outb(EE_ASIC|EE_CS|EE_CK, ee_addr);
	outb(EE_ASIC|EE_CS, ee_addr);

	for (i = 15; i >= 0; i--) {
		dataval = (asic_data & (1 << i)) ? EE_ADOT : 0;
		outb_p(EE_ASIC|EE_CS|dataval, ee_addr);
		outb_p(EE_ASIC|EE_CS|dataval|EE_CK, ee_addr);
		outb_p(EE_ASIC|EE_CS|dataval, ee_addr);
	}

	/* Terminate the ASIC access. */
	outb(EE_ASIC|EE_DI, ee_addr);
	outb(EE_ASIC|EE_DI| EE_CK, ee_addr);
	outb(EE_ASIC|EE_DI, ee_addr);

	outb(0, ee_addr);
}

/*====================================================================*/

static void set_misc_reg(struct net_device *dev)
{
    kio_addr_t nic_base = dev->base_addr;
    pcnet_dev_t *info = PRIV(dev);
    u_char tmp;
    
    if (info->flags & HAS_MISC_REG) {
	tmp = inb_p(nic_base + PCNET_MISC) & ~3;
	if (dev->if_port == 2)
	    tmp |= 1;
	if (info->flags & USE_BIG_BUF)
	    tmp |= 2;
	if (info->flags & HAS_IBM_MISC)
	    tmp |= 8;
	outb_p(tmp, nic_base + PCNET_MISC);
    }
    if (info->flags & IS_DL10022) {
	if (info->flags & HAS_MII) {
	    mdio_reset(nic_base + DLINK_GPIO, info->eth_phy);
	    /* Restart MII autonegotiation */
	    mdio_write(nic_base + DLINK_GPIO, info->eth_phy, 0, 0x0000);
	    mdio_write(nic_base + DLINK_GPIO, info->eth_phy, 0, 0x1200);
	    info->mii_reset = jiffies;
	} else {
	    outb(full_duplex ? 4 : 0, nic_base + DLINK_DIAG);
	}
    }
}

/*====================================================================*/

static void mii_phy_probe(struct net_device *dev)
{
    pcnet_dev_t *info = PRIV(dev);
    kio_addr_t mii_addr = dev->base_addr + DLINK_GPIO;
    int i;
    u_int tmp, phyid;

    for (i = 31; i >= 0; i--) {
	tmp = mdio_read(mii_addr, i, 1);
	if ((tmp == 0) || (tmp == 0xffff))
	    continue;
	tmp = mdio_read(mii_addr, i, MII_PHYID_REG1);
	phyid = tmp << 16;
	phyid |= mdio_read(mii_addr, i, MII_PHYID_REG2);
	phyid &= MII_PHYID_REV_MASK;
	DEBUG(0, "%s: MII at %d is 0x%08x\n", dev->name, i, phyid);
	if (phyid == AM79C9XX_HOME_PHY) {
	    info->pna_phy = i;
	} else if (phyid != AM79C9XX_ETH_PHY) {
	    info->eth_phy = i;
	}
    }
}

static int pcnet_open(struct net_device *dev)
{
    pcnet_dev_t *info = PRIV(dev);
    dev_link_t *link = &info->link;
    
    DEBUG(2, "pcnet_open('%s')\n", dev->name);

    if (!DEV_OK(link))
	return -ENODEV;

    link->open++;

    set_misc_reg(dev);
    request_irq(dev->irq, ei_irq_wrapper, SA_SHIRQ, dev_info, dev);

    info->phy_id = info->eth_phy;
    info->link_status = 0x00;
    init_timer(&info->watchdog);
    info->watchdog.function = &ei_watchdog;
    info->watchdog.data = (u_long)dev;
    info->watchdog.expires = jiffies + HZ;
    add_timer(&info->watchdog);

    return ei_open(dev);
} /* pcnet_open */

/*====================================================================*/

static int pcnet_close(struct net_device *dev)
{
    pcnet_dev_t *info = PRIV(dev);
    dev_link_t *link = &info->link;

    DEBUG(2, "pcnet_close('%s')\n", dev->name);

    ei_close(dev);
    free_irq(dev->irq, dev);
    
    link->open--;
    netif_stop_queue(dev);
    del_timer_sync(&info->watchdog);

    return 0;
} /* pcnet_close */

/*======================================================================

    Hard reset the card.  This used to pause for the same period that
    a 8390 reset command required, but that shouldn't be necessary.

======================================================================*/

static void pcnet_reset_8390(struct net_device *dev)
{
    kio_addr_t nic_base = dev->base_addr;
    int i;

    ei_status.txing = ei_status.dmaing = 0;

    outb_p(E8390_NODMA+E8390_PAGE0+E8390_STOP, nic_base + E8390_CMD);

    outb(inb(nic_base + PCNET_RESET), nic_base + PCNET_RESET);

    for (i = 0; i < 100; i++) {
	if ((inb_p(nic_base+EN0_ISR) & ENISR_RESET) != 0)
	    break;
	udelay(100);
    }
    outb_p(ENISR_RESET, nic_base + EN0_ISR); /* Ack intr. */
    
    if (i == 100)
	printk(KERN_ERR "%s: pcnet_reset_8390() did not complete.\n",
	       dev->name);
    set_misc_reg(dev);
    
} /* pcnet_reset_8390 */

/*====================================================================*/

static int set_config(struct net_device *dev, struct ifmap *map)
{
    pcnet_dev_t *info = PRIV(dev);
    if ((map->port != (u_char)(-1)) && (map->port != dev->if_port)) {
	if (!(info->flags & HAS_MISC_REG))
	    return -EOPNOTSUPP;
	else if ((map->port < 1) || (map->port > 2))
	    return -EINVAL;
	dev->if_port = map->port;
	printk(KERN_INFO "%s: switched to %s port\n",
	       dev->name, if_names[dev->if_port]);
	NS8390_init(dev, 1);
    }
    return 0;
}

/*====================================================================*/

static irqreturn_t ei_irq_wrapper(int irq, void *dev_id, struct pt_regs *regs)
{
    struct net_device *dev = dev_id;
    pcnet_dev_t *info = PRIV(dev);
    irqreturn_t ret = ei_interrupt(irq, dev_id, regs);

    if (ret == IRQ_HANDLED)
	    info->stale = 0;
    return ret;
}

static void ei_watchdog(u_long arg)
{
    struct net_device *dev = (struct net_device *)arg;
    pcnet_dev_t *info = PRIV(dev);
    kio_addr_t nic_base = dev->base_addr;
    kio_addr_t mii_addr = nic_base + DLINK_GPIO;
    u_short link;

    if (!netif_device_present(dev)) goto reschedule;

    /* Check for pending interrupt with expired latency timer: with
       this, we can limp along even if the interrupt is blocked */
    outb_p(E8390_NODMA+E8390_PAGE0, nic_base + E8390_CMD);
    if (info->stale++ && (inb_p(nic_base + EN0_ISR) & ENISR_ALL)) {
	if (!info->fast_poll)
	    printk(KERN_INFO "%s: interrupt(s) dropped!\n", dev->name);
	ei_irq_wrapper(dev->irq, dev, NULL);
	info->fast_poll = HZ;
    }
    if (info->fast_poll) {
	info->fast_poll--;
	info->watchdog.expires = jiffies + 1;
	add_timer(&info->watchdog);
	return;
    }

    if (!(info->flags & HAS_MII))
	goto reschedule;

    mdio_read(mii_addr, info->phy_id, 1);
    link = mdio_read(mii_addr, info->phy_id, 1);
    if (!link || (link == 0xffff)) {
	if (info->eth_phy) {
	    info->phy_id = info->eth_phy = 0;
	} else {
	    printk(KERN_INFO "%s: MII is missing!\n", dev->name);
	    info->flags &= ~HAS_MII;
	}
	goto reschedule;
    }

    link &= 0x0004;
    if (link != info->link_status) {
	u_short p = mdio_read(mii_addr, info->phy_id, 5);
	printk(KERN_INFO "%s: %s link beat\n", dev->name,
	       (link) ? "found" : "lost");
	if (link && (info->flags & IS_DL10022)) {
	    /* Disable collision detection on full duplex links */
	    outb((p & 0x0140) ? 4 : 0, nic_base + DLINK_DIAG);
	} else if (link && (info->flags & IS_DL10019)) {
	    /* Disable collision detection on full duplex links */
	    write_asic(dev->base_addr, 4, (p & 0x140) ? DL19FDUPLX : 0);
	}
	if (link) {
	    if (info->phy_id == info->eth_phy) {
		if (p)
		    printk(KERN_INFO "%s: autonegotiation complete: "
			   "%sbaseT-%cD selected\n", dev->name,
			   ((p & 0x0180) ? "100" : "10"),
			   ((p & 0x0140) ? 'F' : 'H'));
		else
		    printk(KERN_INFO "%s: link partner did not "
			   "autonegotiate\n", dev->name);
	    }
	    NS8390_init(dev, 1);
	}
	info->link_status = link;
    }
    if (info->pna_phy && time_after(jiffies, info->mii_reset + 6*HZ)) {
	link = mdio_read(mii_addr, info->eth_phy, 1) & 0x0004;
	if (((info->phy_id == info->pna_phy) && link) ||
	    ((info->phy_id != info->pna_phy) && !link)) {
	    /* isolate this MII and try flipping to the other one */
	    mdio_write(mii_addr, info->phy_id, 0, 0x0400);
	    info->phy_id ^= info->pna_phy ^ info->eth_phy;
	    printk(KERN_INFO "%s: switched to %s transceiver\n", dev->name,
		   (info->phy_id == info->eth_phy) ? "ethernet" : "PNA");
	    mdio_write(mii_addr, info->phy_id, 0,
		       (info->phy_id == info->eth_phy) ? 0x1000 : 0);
	    info->link_status = 0;
	    info->mii_reset = jiffies;
	}
    }

reschedule:
    info->watchdog.expires = jiffies + HZ;
    add_timer(&info->watchdog);
}

/*====================================================================*/

static void netdev_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strcpy(info->driver, "pcnet_cs");
}

static struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
};

/*====================================================================*/


static int ei_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
    pcnet_dev_t *info = PRIV(dev);
    u16 *data = (u16 *)&rq->ifr_ifru;
    kio_addr_t mii_addr = dev->base_addr + DLINK_GPIO;
    switch (cmd) {
    case SIOCGMIIPHY:
	data[0] = info->phy_id;
    case SIOCGMIIREG:		/* Read MII PHY register. */
	data[3] = mdio_read(mii_addr, data[0], data[1] & 0x1f);
	return 0;
    case SIOCSMIIREG:		/* Write MII PHY register. */
	if (!capable(CAP_NET_ADMIN))
	    return -EPERM;
	mdio_write(mii_addr, data[0], data[1] & 0x1f, data[2]);
	return 0;
    }
    return -EOPNOTSUPP;
}

/*====================================================================*/

static void dma_get_8390_hdr(struct net_device *dev,
			     struct e8390_pkt_hdr *hdr,
			     int ring_page)
{
    kio_addr_t nic_base = dev->base_addr;

    if (ei_status.dmaing) {
	printk(KERN_NOTICE "%s: DMAing conflict in dma_block_input."
	       "[DMAstat:%1x][irqlock:%1x]\n",
	       dev->name, ei_status.dmaing, ei_status.irqlock);
	return;
    }
    
    ei_status.dmaing |= 0x01;
    outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, nic_base + PCNET_CMD);
    outb_p(sizeof(struct e8390_pkt_hdr), nic_base + EN0_RCNTLO);
    outb_p(0, nic_base + EN0_RCNTHI);
    outb_p(0, nic_base + EN0_RSARLO);		/* On page boundary */
    outb_p(ring_page, nic_base + EN0_RSARHI);
    outb_p(E8390_RREAD+E8390_START, nic_base + PCNET_CMD);

    insw(nic_base + PCNET_DATAPORT, hdr,
	    sizeof(struct e8390_pkt_hdr)>>1);
    /* Fix for big endian systems */
    hdr->count = le16_to_cpu(hdr->count);

    outb_p(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
    ei_status.dmaing &= ~0x01;
}

/*====================================================================*/

static void dma_block_input(struct net_device *dev, int count,
			    struct sk_buff *skb, int ring_offset)
{
    kio_addr_t nic_base = dev->base_addr;
    int xfer_count = count;
    char *buf = skb->data;

#ifdef PCMCIA_DEBUG
    if ((ei_debug > 4) && (count != 4))
	printk(KERN_DEBUG "%s: [bi=%d]\n", dev->name, count+4);
#endif
    if (ei_status.dmaing) {
	printk(KERN_NOTICE "%s: DMAing conflict in dma_block_input."
	       "[DMAstat:%1x][irqlock:%1x]\n",
	       dev->name, ei_status.dmaing, ei_status.irqlock);
	return;
    }
    ei_status.dmaing |= 0x01;
    outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, nic_base + PCNET_CMD);
    outb_p(count & 0xff, nic_base + EN0_RCNTLO);
    outb_p(count >> 8, nic_base + EN0_RCNTHI);
    outb_p(ring_offset & 0xff, nic_base + EN0_RSARLO);
    outb_p(ring_offset >> 8, nic_base + EN0_RSARHI);
    outb_p(E8390_RREAD+E8390_START, nic_base + PCNET_CMD);

    insw(nic_base + PCNET_DATAPORT,buf,count>>1);
    if (count & 0x01)
	buf[count-1] = inb(nic_base + PCNET_DATAPORT), xfer_count++;

    /* This was for the ALPHA version only, but enough people have
       encountering problems that it is still here. */
#ifdef PCMCIA_DEBUG
    if (ei_debug > 4) {		/* DMA termination address check... */
	int addr, tries = 20;
	do {
	    /* DON'T check for 'inb_p(EN0_ISR) & ENISR_RDC' here
	       -- it's broken for Rx on some cards! */
	    int high = inb_p(nic_base + EN0_RSARHI);
	    int low = inb_p(nic_base + EN0_RSARLO);
	    addr = (high << 8) + low;
	    if (((ring_offset + xfer_count) & 0xff) == (addr & 0xff))
		break;
	} while (--tries > 0);
	if (tries <= 0)
	    printk(KERN_NOTICE "%s: RX transfer address mismatch,"
		   "%#4.4x (expected) vs. %#4.4x (actual).\n",
		   dev->name, ring_offset + xfer_count, addr);
    }
#endif
    outb_p(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
    ei_status.dmaing &= ~0x01;
} /* dma_block_input */

/*====================================================================*/

static void dma_block_output(struct net_device *dev, int count,
			     const u_char *buf, const int start_page)
{
    kio_addr_t nic_base = dev->base_addr;
    pcnet_dev_t *info = PRIV(dev);
#ifdef PCMCIA_DEBUG
    int retries = 0;
#endif
    u_long dma_start;

#ifdef PCMCIA_DEBUG
    if (ei_debug > 4)
	printk(KERN_DEBUG "%s: [bo=%d]\n", dev->name, count);
#endif

    /* Round the count up for word writes.  Do we need to do this?
       What effect will an odd byte count have on the 8390?
       I should check someday. */
    if (count & 0x01)
	count++;
    if (ei_status.dmaing) {
	printk(KERN_NOTICE "%s: DMAing conflict in dma_block_output."
	       "[DMAstat:%1x][irqlock:%1x]\n",
	       dev->name, ei_status.dmaing, ei_status.irqlock);
	return;
    }
    ei_status.dmaing |= 0x01;
    /* We should already be in page 0, but to be safe... */
    outb_p(E8390_PAGE0+E8390_START+E8390_NODMA, nic_base+PCNET_CMD);

#ifdef PCMCIA_DEBUG
  retry:
#endif

    outb_p(ENISR_RDC, nic_base + EN0_ISR);

    /* Now the normal output. */
    outb_p(count & 0xff, nic_base + EN0_RCNTLO);
    outb_p(count >> 8,   nic_base + EN0_RCNTHI);
    outb_p(0x00, nic_base + EN0_RSARLO);
    outb_p(start_page, nic_base + EN0_RSARHI);

    outb_p(E8390_RWRITE+E8390_START, nic_base + PCNET_CMD);
    outsw(nic_base + PCNET_DATAPORT, buf, count>>1);

    dma_start = jiffies;

#ifdef PCMCIA_DEBUG
    /* This was for the ALPHA version only, but enough people have
       encountering problems that it is still here. */
    if (ei_debug > 4) {	/* DMA termination address check... */
	int addr, tries = 20;
	do {
	    int high = inb_p(nic_base + EN0_RSARHI);
	    int low = inb_p(nic_base + EN0_RSARLO);
	    addr = (high << 8) + low;
	    if ((start_page << 8) + count == addr)
		break;
	} while (--tries > 0);
	if (tries <= 0) {
	    printk(KERN_NOTICE "%s: Tx packet transfer address mismatch,"
		   "%#4.4x (expected) vs. %#4.4x (actual).\n",
		   dev->name, (start_page << 8) + count, addr);
	    if (retries++ == 0)
		goto retry;
	}
    }
#endif

    while ((inb_p(nic_base + EN0_ISR) & ENISR_RDC) == 0)
	if (time_after(jiffies, dma_start + PCNET_RDC_TIMEOUT)) {
	    printk(KERN_NOTICE "%s: timeout waiting for Tx RDC.\n",
		   dev->name);
	    pcnet_reset_8390(dev);
	    NS8390_init(dev, 1);
	    break;
	}

    outb_p(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
    if (info->flags & DELAY_OUTPUT)
	udelay((long)delay_time);
    ei_status.dmaing &= ~0x01;
}

/*====================================================================*/

static int setup_dma_config(dev_link_t *link, int start_pg,
			    int stop_pg)
{
    struct net_device *dev = link->priv;

    ei_status.tx_start_page = start_pg;
    ei_status.rx_start_page = start_pg + TX_PAGES;
    ei_status.stop_page = stop_pg;

    /* set up block i/o functions */
    ei_status.get_8390_hdr = &dma_get_8390_hdr;
    ei_status.block_input = &dma_block_input;
    ei_status.block_output = &dma_block_output;

    return 0;
}

/*====================================================================*/

static void copyin(void *dest, void __iomem *src, int c)
{
    u_short *d = dest;
    u_short __iomem *s = src;
    int odd;

    if (c <= 0)
	return;
    odd = (c & 1); c >>= 1;

    if (c) {
	do { *d++ = __raw_readw(s++); } while (--c);
    }
    /* get last byte by fetching a word and masking */
    if (odd)
	*((u_char *)d) = readw(s) & 0xff;
}

static void copyout(void __iomem *dest, const void *src, int c)
{
    u_short __iomem *d = dest;
    const u_short *s = src;
    int odd;

    if (c <= 0)
	return;
    odd = (c & 1); c >>= 1;

    if (c) {
	do { __raw_writew(*s++, d++); } while (--c);
    }
    /* copy last byte doing a read-modify-write */
    if (odd)
	writew((readw(d) & 0xff00) | *(u_char *)s, d);
}

/*====================================================================*/

static void shmem_get_8390_hdr(struct net_device *dev,
			       struct e8390_pkt_hdr *hdr,
			       int ring_page)
{
    void __iomem *xfer_start = ei_status.mem + (TX_PAGES<<8)
				+ (ring_page << 8)
				- (ei_status.rx_start_page << 8);
    
    copyin(hdr, xfer_start, sizeof(struct e8390_pkt_hdr));
    /* Fix for big endian systems */
    hdr->count = le16_to_cpu(hdr->count);
}

/*====================================================================*/

static void shmem_block_input(struct net_device *dev, int count,
			      struct sk_buff *skb, int ring_offset)
{
    void __iomem *xfer_start = ei_status.mem + (TX_PAGES<<8)
				+ ring_offset
				- (ei_status.rx_start_page << 8);
    char *buf = skb->data;
    
    if (xfer_start + count > (void __iomem *)ei_status.rmem_end) {
	/* We must wrap the input move. */
	int semi_count = (void __iomem *)ei_status.rmem_end - xfer_start;
	copyin(buf, xfer_start, semi_count);
	buf += semi_count;
	xfer_start = ei_status.mem + (TX_PAGES<<8);
	count -= semi_count;
    }
    copyin(buf, xfer_start, count);
}

/*====================================================================*/

static void shmem_block_output(struct net_device *dev, int count,
			       const u_char *buf, const int start_page)
{
    void __iomem *shmem = ei_status.mem + (start_page << 8);
    shmem -= ei_status.tx_start_page << 8;
    copyout(shmem, buf, count);
}

/*====================================================================*/

static int setup_shmem_window(dev_link_t *link, int start_pg,
			      int stop_pg, int cm_offset)
{
    struct net_device *dev = link->priv;
    pcnet_dev_t *info = PRIV(dev);
    win_req_t req;
    memreq_t mem;
    int i, window_size, offset, last_ret, last_fn;

    window_size = (stop_pg - start_pg) << 8;
    if (window_size > 32 * 1024)
	window_size = 32 * 1024;

    /* Make sure it's a power of two.  */
    while ((window_size & (window_size - 1)) != 0)
	window_size += window_size & ~(window_size - 1);

    /* Allocate a memory window */
    req.Attributes = WIN_DATA_WIDTH_16|WIN_MEMORY_TYPE_CM|WIN_ENABLE;
    req.Attributes |= WIN_USE_WAIT;
    req.Base = 0; req.Size = window_size;
    req.AccessSpeed = mem_speed;
    CS_CHECK(RequestWindow, pcmcia_request_window(&link->handle, &req, &link->win));

    mem.CardOffset = (start_pg << 8) + cm_offset;
    offset = mem.CardOffset % window_size;
    mem.CardOffset -= offset;
    mem.Page = 0;
    CS_CHECK(MapMemPage, pcmcia_map_mem_page(link->win, &mem));

    /* Try scribbling on the buffer */
    info->base = ioremap(req.Base, window_size);
    for (i = 0; i < (TX_PAGES<<8); i += 2)
	__raw_writew((i>>1), info->base+offset+i);
    udelay(100);
    for (i = 0; i < (TX_PAGES<<8); i += 2)
	if (__raw_readw(info->base+offset+i) != (i>>1)) break;
    pcnet_reset_8390(dev);
    if (i != (TX_PAGES<<8)) {
	iounmap(info->base);
	pcmcia_release_window(link->win);
	info->base = NULL; link->win = NULL;
	goto failed;
    }
    
    ei_status.mem = info->base + offset;
    dev->mem_start = (u_long)ei_status.mem;
    dev->mem_end = ei_status.rmem_end = (u_long)info->base + req.Size;

    ei_status.tx_start_page = start_pg;
    ei_status.rx_start_page = start_pg + TX_PAGES;
    ei_status.stop_page = start_pg + ((req.Size - offset) >> 8);

    /* set up block i/o functions */
    ei_status.get_8390_hdr = &shmem_get_8390_hdr;
    ei_status.block_input = &shmem_block_input;
    ei_status.block_output = &shmem_block_output;

    info->flags |= USE_SHMEM;
    return 0;

cs_failed:
    cs_error(link->handle, last_fn, last_ret);
failed:
    return 1;
}

/*====================================================================*/

static struct pcmcia_driver pcnet_driver = {
	.drv		= {
		.name	= "pcnet_cs",
	},
	.attach		= pcnet_attach,
	.detach		= pcnet_detach,
	.owner		= THIS_MODULE,
};

static int __init init_pcnet_cs(void)
{
    return pcmcia_register_driver(&pcnet_driver);
}

static void __exit exit_pcnet_cs(void)
{
    DEBUG(0, "pcnet_cs: unloading\n");
    pcmcia_unregister_driver(&pcnet_driver);
    BUG_ON(dev_list != NULL);
}

module_init(init_pcnet_cs);
module_exit(exit_pcnet_cs);
