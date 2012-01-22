/*======================================================================
    fmvj18x_cs.c 2.8 2002/03/23

    A fmvj18x (and its compatibles) PCMCIA client driver

    Contributed by Shingo Fujimoto, shingo@flab.fujitsu.co.jp

    TDK LAK-CD021 and CONTEC C-NET(PC)C support added by 
    Nobuhiro Katayama, kata-n@po.iijnet.or.jp

    The PCMCIA client code is based on code written by David Hinds.
    Network code is based on the "FMV-18x driver" by Yutaka TAMIYA
    but is actually largely Donald Becker's AT1700 driver, which
    carries the following attribution:

    Written 1993-94 by Donald Becker.

    Copyright 1993 United States Government as represented by the
    Director, National Security Agency.
    
    This software may be used and distributed according to the terms
    of the GNU General Public License, incorporated herein by reference.
    
    The author may be reached as becker@scyld.com, or C/O
    Scyld Computing Corporation
    410 Severn Ave., Suite 210
    Annapolis MD 21403
   
======================================================================*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define DRV_NAME	"fmvj18x_cs"
#define DRV_VERSION	"2.9"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/crc32.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/system.h>

/*====================================================================*/

/* Module parameters */

MODULE_DESCRIPTION("fmvj18x and compatible PCMCIA ethernet driver");
MODULE_LICENSE("GPL");

#define INT_MODULE_PARM(n, v) static int n = v; module_param(n, int, 0)

/* SRAM configuration */
/* 0:4KB*2 TX buffer   else:8KB*2 TX buffer */
INT_MODULE_PARM(sram_config, 0);


/*====================================================================*/
/*
    PCMCIA event handlers
 */
static int fmvj18x_config(struct pcmcia_device *link);
static int fmvj18x_get_hwinfo(struct pcmcia_device *link, u_char *node_id);
static int fmvj18x_setup_mfc(struct pcmcia_device *link);
static void fmvj18x_release(struct pcmcia_device *link);
static void fmvj18x_detach(struct pcmcia_device *p_dev);

/*
    LAN controller(MBH86960A) specific routines
 */
static int fjn_config(struct net_device *dev, struct ifmap *map);
static int fjn_open(struct net_device *dev);
static int fjn_close(struct net_device *dev);
static netdev_tx_t fjn_start_xmit(struct sk_buff *skb,
					struct net_device *dev);
static irqreturn_t fjn_interrupt(int irq, void *dev_id);
static void fjn_rx(struct net_device *dev);
static void fjn_reset(struct net_device *dev);
static void set_rx_mode(struct net_device *dev);
static void fjn_tx_timeout(struct net_device *dev);
static const struct ethtool_ops netdev_ethtool_ops;

/*
    card type
 */
typedef enum { MBH10302, MBH10304, TDK, CONTEC, LA501, UNGERMANN, 
	       XXX10304, NEC, KME
} cardtype_t;

/*
    driver specific data structure
*/
typedef struct local_info_t {
	struct pcmcia_device	*p_dev;
    long open_time;
    uint tx_started:1;
    uint tx_queue;
    u_short tx_queue_len;
    cardtype_t cardtype;
    u_short sent;
    u_char __iomem *base;
} local_info_t;

#define MC_FILTERBREAK 64

/*====================================================================*/
/* 
    ioport offset from the base address 
 */
#define TX_STATUS               0 /* transmit status register */
#define RX_STATUS               1 /* receive status register */
#define TX_INTR                 2 /* transmit interrupt mask register */
#define RX_INTR                 3 /* receive interrupt mask register */
#define TX_MODE                 4 /* transmit mode register */
#define RX_MODE                 5 /* receive mode register */
#define CONFIG_0                6 /* configuration register 0 */
#define CONFIG_1                7 /* configuration register 1 */

#define NODE_ID                 8 /* node ID register            (bank 0) */
#define MAR_ADR                 8 /* multicast address registers (bank 1) */

#define DATAPORT                8 /* buffer mem port registers   (bank 2) */
#define TX_START               10 /* transmit start register */
#define COL_CTRL               11 /* 16 collision control register */
#define BMPR12                 12 /* reserved */
#define BMPR13                 13 /* reserved */
#define RX_SKIP                14 /* skip received packet register */

#define LAN_CTRL               16 /* LAN card control register */

#define MAC_ID               0x1a /* hardware address */
#define UNGERMANN_MAC_ID     0x18 /* UNGERMANN-BASS hardware address */

/* 
    control bits 
 */
#define ENA_TMT_OK           0x80
#define ENA_TMT_REC          0x20
#define ENA_COL              0x04
#define ENA_16_COL           0x02
#define ENA_TBUS_ERR         0x01

#define ENA_PKT_RDY          0x80
#define ENA_BUS_ERR          0x40
#define ENA_LEN_ERR          0x08
#define ENA_ALG_ERR          0x04
#define ENA_CRC_ERR          0x02
#define ENA_OVR_FLO          0x01

/* flags */
#define F_TMT_RDY            0x80 /* can accept new packet */
#define F_NET_BSY            0x40 /* carrier is detected */
#define F_TMT_OK             0x20 /* send packet successfully */
#define F_SRT_PKT            0x10 /* short packet error */
#define F_COL_ERR            0x04 /* collision error */
#define F_16_COL             0x02 /* 16 collision error */
#define F_TBUS_ERR           0x01 /* bus read error */

#define F_PKT_RDY            0x80 /* packet(s) in buffer */
#define F_BUS_ERR            0x40 /* bus read error */
#define F_LEN_ERR            0x08 /* short packet */
#define F_ALG_ERR            0x04 /* frame error */
#define F_CRC_ERR            0x02 /* CRC error */
#define F_OVR_FLO            0x01 /* overflow error */

#define F_BUF_EMP            0x40 /* receive buffer is empty */

#define F_SKP_PKT            0x05 /* drop packet in buffer */

/* default bitmaps */
#define D_TX_INTR  ( ENA_TMT_OK )
#define D_RX_INTR  ( ENA_PKT_RDY | ENA_LEN_ERR \
		   | ENA_ALG_ERR | ENA_CRC_ERR | ENA_OVR_FLO )
#define TX_STAT_M  ( F_TMT_RDY )
#define RX_STAT_M  ( F_PKT_RDY | F_LEN_ERR \
                   | F_ALG_ERR | F_CRC_ERR | F_OVR_FLO )

/* commands */
#define D_TX_MODE            0x06 /* no tests, detect carrier */
#define ID_MATCHED           0x02 /* (RX_MODE) */
#define RECV_ALL             0x03 /* (RX_MODE) */
#define CONFIG0_DFL          0x5a /* 16bit bus, 4K x 2 Tx queues */
#define CONFIG0_DFL_1        0x5e /* 16bit bus, 8K x 2 Tx queues */
#define CONFIG0_RST          0xda /* Data Link Controller off (CONFIG_0) */
#define CONFIG0_RST_1        0xde /* Data Link Controller off (CONFIG_0) */
#define BANK_0               0xa0 /* bank 0 (CONFIG_1) */
#define BANK_1               0xa4 /* bank 1 (CONFIG_1) */
#define BANK_2               0xa8 /* bank 2 (CONFIG_1) */
#define CHIP_OFF             0x80 /* contrl chip power off (CONFIG_1) */
#define DO_TX                0x80 /* do transmit packet */
#define SEND_PKT             0x81 /* send a packet */
#define AUTO_MODE            0x07 /* Auto skip packet on 16 col detected */
#define MANU_MODE            0x03 /* Stop and skip packet on 16 col */
#define TDK_AUTO_MODE        0x47 /* Auto skip packet on 16 col detected */
#define TDK_MANU_MODE        0x43 /* Stop and skip packet on 16 col */
#define INTR_OFF             0x0d /* LAN controller ignores interrupts */
#define INTR_ON              0x1d /* LAN controller will catch interrupts */

#define TX_TIMEOUT		((400*HZ)/1000)

#define BANK_0U              0x20 /* bank 0 (CONFIG_1) */
#define BANK_1U              0x24 /* bank 1 (CONFIG_1) */
#define BANK_2U              0x28 /* bank 2 (CONFIG_1) */

static const struct net_device_ops fjn_netdev_ops = {
	.ndo_open 		= fjn_open,
	.ndo_stop		= fjn_close,
	.ndo_start_xmit 	= fjn_start_xmit,
	.ndo_tx_timeout 	= fjn_tx_timeout,
	.ndo_set_config 	= fjn_config,
	.ndo_set_rx_mode	= set_rx_mode,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static int fmvj18x_probe(struct pcmcia_device *link)
{
    local_info_t *lp;
    struct net_device *dev;

    dev_dbg(&link->dev, "fmvj18x_attach()\n");

    /* Make up a FMVJ18x specific data structure */
    dev = alloc_etherdev(sizeof(local_info_t));
    if (!dev)
	return -ENOMEM;
    lp = netdev_priv(dev);
    link->priv = dev;
    lp->p_dev = link;
    lp->base = NULL;

    /* The io structure describes IO port mapping */
    link->resource[0]->end = 32;
    link->resource[0]->flags |= IO_DATA_PATH_WIDTH_AUTO;

    /* General socket configuration */
    link->config_flags |= CONF_ENABLE_IRQ;

    dev->netdev_ops = &fjn_netdev_ops;
    dev->watchdog_timeo = TX_TIMEOUT;

    SET_ETHTOOL_OPS(dev, &netdev_ethtool_ops);

    return fmvj18x_config(link);
} /* fmvj18x_attach */

/*====================================================================*/

static void fmvj18x_detach(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;

    dev_dbg(&link->dev, "fmvj18x_detach\n");

    unregister_netdev(dev);

    fmvj18x_release(link);

    free_netdev(dev);
} /* fmvj18x_detach */

/*====================================================================*/

static int mfc_try_io_port(struct pcmcia_device *link)
{
    int i, ret;
    static const unsigned int serial_base[5] =
	{ 0x3f8, 0x2f8, 0x3e8, 0x2e8, 0x0 };

    for (i = 0; i < 5; i++) {
	link->resource[1]->start = serial_base[i];
	link->resource[1]->flags |= IO_DATA_PATH_WIDTH_8;
	if (link->resource[1]->start == 0) {
	    link->resource[1]->end = 0;
	    pr_notice("out of resource for serial\n");
	}
	ret = pcmcia_request_io(link);
	if (ret == 0)
		return ret;
    }
    return ret;
}

static int ungermann_try_io_port(struct pcmcia_device *link)
{
    int ret;
    unsigned int ioaddr;
    /*
	Ungermann-Bass Access/CARD accepts 0x300,0x320,0x340,0x360
	0x380,0x3c0 only for ioport.
    */
    for (ioaddr = 0x300; ioaddr < 0x3e0; ioaddr += 0x20) {
	link->resource[0]->start = ioaddr;
	ret = pcmcia_request_io(link);
	if (ret == 0) {
	    /* calculate ConfigIndex value */
	    link->config_index =
		((link->resource[0]->start & 0x0f0) >> 3) | 0x22;
	    return ret;
	}
    }
    return ret;	/* RequestIO failed */
}

static int fmvj18x_ioprobe(struct pcmcia_device *p_dev, void *priv_data)
{
	return 0; /* strange, but that's what the code did already before... */
}

static int fmvj18x_config(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;
    local_info_t *lp = netdev_priv(dev);
    int i, ret;
    unsigned int ioaddr;
    cardtype_t cardtype;
    char *card_name = "unknown";
    u8 *buf;
    size_t len;
    u_char buggybuf[32];

    dev_dbg(&link->dev, "fmvj18x_config\n");

    link->io_lines = 5;

    len = pcmcia_get_tuple(link, CISTPL_FUNCE, &buf);
    kfree(buf);

    if (len) {
	/* Yes, I have CISTPL_FUNCE. Let's check CISTPL_MANFID */
	ret = pcmcia_loop_config(link, fmvj18x_ioprobe, NULL);
	if (ret != 0)
		goto failed;

	switch (link->manf_id) {
	case MANFID_TDK:
	    cardtype = TDK;
	    if (link->card_id == PRODID_TDK_GN3410 ||
		link->card_id == PRODID_TDK_NP9610 ||
		link->card_id == PRODID_TDK_MN3200) {
		/* MultiFunction Card */
		link->config_base = 0x800;
		link->config_index = 0x47;
		link->resource[1]->end = 8;
	    }
	    break;
	case MANFID_NEC:
	    cardtype = NEC; /* MultiFunction Card */
	    link->config_base = 0x800;
	    link->config_index = 0x47;
	    link->resource[1]->end = 8;
	    break;
	case MANFID_KME:
	    cardtype = KME; /* MultiFunction Card */
	    link->config_base = 0x800;
	    link->config_index = 0x47;
	    link->resource[1]->end = 8;
	    break;
	case MANFID_CONTEC:
	    cardtype = CONTEC;
	    break;
	case MANFID_FUJITSU:
	    if (link->config_base == 0x0fe0)
		cardtype = MBH10302;
	    else if (link->card_id == PRODID_FUJITSU_MBH10302) 
                /* RATOC REX-5588/9822/4886's PRODID are 0004(=MBH10302),
                   but these are MBH10304 based card. */ 
		cardtype = MBH10304;
	    else if (link->card_id == PRODID_FUJITSU_MBH10304)
		cardtype = MBH10304;
	    else
		cardtype = LA501;
	    break;
	default:
	    cardtype = MBH10304;
	}
    } else {
	/* old type card */
	switch (link->manf_id) {
	case MANFID_FUJITSU:
	    if (link->card_id == PRODID_FUJITSU_MBH10304) {
		cardtype = XXX10304;    /* MBH10304 with buggy CIS */
		link->config_index = 0x20;
	    } else {
		cardtype = MBH10302;    /* NextCom NC5310, etc. */
		link->config_index = 1;
	    }
	    break;
	case MANFID_UNGERMANN:
	    cardtype = UNGERMANN;
	    break;
	default:
	    cardtype = MBH10302;
	    link->config_index = 1;
	}
    }

    if (link->resource[1]->end != 0) {
	ret = mfc_try_io_port(link);
	if (ret != 0) goto failed;
    } else if (cardtype == UNGERMANN) {
	ret = ungermann_try_io_port(link);
	if (ret != 0) goto failed;
    } else { 
	    ret = pcmcia_request_io(link);
	    if (ret)
		    goto failed;
    }
    ret = pcmcia_request_irq(link, fjn_interrupt);
    if (ret)
	    goto failed;
    ret = pcmcia_enable_device(link);
    if (ret)
	    goto failed;

    dev->irq = link->irq;
    dev->base_addr = link->resource[0]->start;

    if (resource_size(link->resource[1]) != 0) {
	ret = fmvj18x_setup_mfc(link);
	if (ret != 0) goto failed;
    }

    ioaddr = dev->base_addr;

    /* Reset controller */
    if (sram_config == 0) 
	outb(CONFIG0_RST, ioaddr + CONFIG_0);
    else
	outb(CONFIG0_RST_1, ioaddr + CONFIG_0);

    /* Power On chip and select bank 0 */
    if (cardtype == MBH10302)
	outb(BANK_0, ioaddr + CONFIG_1);
    else
	outb(BANK_0U, ioaddr + CONFIG_1);
    
    /* Set hardware address */
    switch (cardtype) {
    case MBH10304:
    case TDK:
    case LA501:
    case CONTEC:
    case NEC:
    case KME:
	if (cardtype == MBH10304) {
	    card_name = "FMV-J182";

	    len = pcmcia_get_tuple(link, CISTPL_FUNCE, &buf);
	    if (len < 11) {
		    kfree(buf);
		    goto failed;
	    }
	    /* Read MACID from CIS */
	    for (i = 5; i < 11; i++)
		    dev->dev_addr[i] = buf[i];
	    kfree(buf);
	} else {
	    if (pcmcia_get_mac_from_cis(link, dev))
		goto failed;
	    if( cardtype == TDK ) {
		card_name = "TDK LAK-CD021";
	    } else if( cardtype == LA501 ) {
		card_name = "LA501";
	    } else if( cardtype == NEC ) {
		card_name = "PK-UG-J001";
	    } else if( cardtype == KME ) {
		card_name = "Panasonic";
	    } else {
		card_name = "C-NET(PC)C";
	    }
	}
	break;
    case UNGERMANN:
	/* Read MACID from register */
	for (i = 0; i < 6; i++) 
	    dev->dev_addr[i] = inb(ioaddr + UNGERMANN_MAC_ID + i);
	card_name = "Access/CARD";
	break;
    case XXX10304:
	/* Read MACID from Buggy CIS */
	if (fmvj18x_get_hwinfo(link, buggybuf) == -1) {
	    pr_notice("unable to read hardware net address\n");
	    goto failed;
	}
	for (i = 0 ; i < 6; i++) {
	    dev->dev_addr[i] = buggybuf[i];
	}
	card_name = "FMV-J182";
	break;
    case MBH10302:
    default:
	/* Read MACID from register */
	for (i = 0; i < 6; i++) 
	    dev->dev_addr[i] = inb(ioaddr + MAC_ID + i);
	card_name = "FMV-J181";
	break;
    }

    lp->cardtype = cardtype;
    SET_NETDEV_DEV(dev, &link->dev);

    if (register_netdev(dev) != 0) {
	pr_notice("register_netdev() failed\n");
	goto failed;
    }

    /* print current configuration */
    netdev_info(dev, "%s, sram %s, port %#3lx, irq %d, hw_addr %pM\n",
		card_name, sram_config == 0 ? "4K TX*2" : "8K TX*2",
		dev->base_addr, dev->irq, dev->dev_addr);

    return 0;
    
failed:
    fmvj18x_release(link);
    return -ENODEV;
} /* fmvj18x_config */
/*====================================================================*/

static int fmvj18x_get_hwinfo(struct pcmcia_device *link, u_char *node_id)
{
    u_char __iomem *base;
    int i, j;

    /* Allocate a small memory window */
    link->resource[2]->flags |= WIN_DATA_WIDTH_8|WIN_MEMORY_TYPE_AM|WIN_ENABLE;
    link->resource[2]->start = 0; link->resource[2]->end = 0;
    i = pcmcia_request_window(link, link->resource[2], 0);
    if (i != 0)
	return -1;

    base = ioremap(link->resource[2]->start, resource_size(link->resource[2]));
    pcmcia_map_mem_page(link, link->resource[2], 0);

    /*
     *  MBH10304 CISTPL_FUNCE_LAN_NODE_ID format
     *  22 0d xx xx xx 04 06 yy yy yy yy yy yy ff
     *  'xx' is garbage.
     *  'yy' is MAC address.
    */ 
    for (i = 0; i < 0x200; i++) {
	if (readb(base+i*2) == 0x22) {	
		if (readb(base+(i-1)*2) == 0xff &&
		    readb(base+(i+5)*2) == 0x04 &&
		    readb(base+(i+6)*2) == 0x06 &&
		    readb(base+(i+13)*2) == 0xff)
			break;
	}
    }

    if (i != 0x200) {
	for (j = 0 ; j < 6; j++,i++) {
	    node_id[j] = readb(base+(i+7)*2);
	}
    }

    iounmap(base);
    j = pcmcia_release_window(link, link->resource[2]);
    return (i != 0x200) ? 0 : -1;

} /* fmvj18x_get_hwinfo */
/*====================================================================*/

static int fmvj18x_setup_mfc(struct pcmcia_device *link)
{
    int i;
    struct net_device *dev = link->priv;
    unsigned int ioaddr;
    local_info_t *lp = netdev_priv(dev);

    /* Allocate a small memory window */
    link->resource[3]->flags = WIN_DATA_WIDTH_8|WIN_MEMORY_TYPE_AM|WIN_ENABLE;
    link->resource[3]->start = link->resource[3]->end = 0;
    i = pcmcia_request_window(link, link->resource[3], 0);
    if (i != 0)
	return -1;

    lp->base = ioremap(link->resource[3]->start,
		       resource_size(link->resource[3]));
    if (lp->base == NULL) {
	netdev_notice(dev, "ioremap failed\n");
	return -1;
    }

    i = pcmcia_map_mem_page(link, link->resource[3], 0);
    if (i != 0) {
	iounmap(lp->base);
	lp->base = NULL;
	return -1;
    }
    
    ioaddr = dev->base_addr;
    writeb(0x47, lp->base+0x800);	/* Config Option Register of LAN */
    writeb(0x0,  lp->base+0x802);	/* Config and Status Register */

    writeb(ioaddr & 0xff, lp->base+0x80a);	  /* I/O Base(Low) of LAN */
    writeb((ioaddr >> 8) & 0xff, lp->base+0x80c); /* I/O Base(High) of LAN */
   
    writeb(0x45, lp->base+0x820);	/* Config Option Register of Modem */
    writeb(0x8,  lp->base+0x822);	/* Config and Status Register */

    return 0;

}
/*====================================================================*/

static void fmvj18x_release(struct pcmcia_device *link)
{

    struct net_device *dev = link->priv;
    local_info_t *lp = netdev_priv(dev);
    u_char __iomem *tmp;

    dev_dbg(&link->dev, "fmvj18x_release\n");

    if (lp->base != NULL) {
	tmp = lp->base;
	lp->base = NULL;    /* set NULL before iounmap */
	iounmap(tmp);
    }

    pcmcia_disable_device(link);

}

static int fmvj18x_suspend(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	if (link->open)
		netif_device_detach(dev);

	return 0;
}

static int fmvj18x_resume(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	if (link->open) {
		fjn_reset(dev);
		netif_device_attach(dev);
	}

	return 0;
}

/*====================================================================*/

static const struct pcmcia_device_id fmvj18x_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x0004, 0x0004),
	PCMCIA_DEVICE_PROD_ID12("EAGLE Technology", "NE200 ETHERNET LAN MBH10302 04", 0x528c88c4, 0x74f91e59),
	PCMCIA_DEVICE_PROD_ID12("Eiger Labs,Inc", "EPX-10BT PC Card Ethernet 10BT", 0x53af556e, 0x877f9922),
	PCMCIA_DEVICE_PROD_ID12("Eiger labs,Inc.", "EPX-10BT PC Card Ethernet 10BT", 0xf47e6c66, 0x877f9922),
	PCMCIA_DEVICE_PROD_ID12("FUJITSU", "LAN Card(FMV-J182)", 0x6ee5a3d8, 0x5baf31db),
	PCMCIA_DEVICE_PROD_ID12("FUJITSU", "MBH10308", 0x6ee5a3d8, 0x3f04875e),
	PCMCIA_DEVICE_PROD_ID12("FUJITSU TOWA", "LA501", 0xb8451188, 0x12939ba2),
	PCMCIA_DEVICE_PROD_ID12("HITACHI", "HT-4840-11", 0xf4f43949, 0x773910f4),
	PCMCIA_DEVICE_PROD_ID12("NextComK.K.", "NC5310B Ver1.0       ", 0x8cef4d3a, 0x075fc7b6),
	PCMCIA_DEVICE_PROD_ID12("NextComK.K.", "NC5310 Ver1.0        ", 0x8cef4d3a, 0xbccf43e6),
	PCMCIA_DEVICE_PROD_ID12("RATOC System Inc.", "10BASE_T CARD R280", 0x85c10e17, 0xd9413666),
	PCMCIA_DEVICE_PROD_ID12("TDK", "LAC-CD02x", 0x1eae9475, 0x8fa0ee70),
	PCMCIA_DEVICE_PROD_ID12("TDK", "LAC-CF010", 0x1eae9475, 0x7683bc9a),
	PCMCIA_DEVICE_PROD_ID1("CONTEC Co.,Ltd.", 0x58d8fee2),
	PCMCIA_DEVICE_PROD_ID1("PCMCIA LAN MBH10304  ES", 0x2599f454),
	PCMCIA_DEVICE_PROD_ID1("PCMCIA MBH10302", 0x8f4005da),
	PCMCIA_DEVICE_PROD_ID1("UBKK,V2.0", 0x90888080),
	PCMCIA_PFC_DEVICE_PROD_ID12(0, "TDK", "GlobalNetworker 3410/3412", 0x1eae9475, 0xd9a93bed),
	PCMCIA_PFC_DEVICE_PROD_ID12(0, "NEC", "PK-UG-J001" ,0x18df0ba0 ,0x831b1064),
	PCMCIA_PFC_DEVICE_MANF_CARD(0, 0x0105, 0x0d0a),
	PCMCIA_PFC_DEVICE_MANF_CARD(0, 0x0105, 0x0e0a),
	PCMCIA_PFC_DEVICE_MANF_CARD(0, 0x0032, 0x0e01),
	PCMCIA_PFC_DEVICE_MANF_CARD(0, 0x0032, 0x0a05),
	PCMCIA_PFC_DEVICE_MANF_CARD(0, 0x0032, 0x0b05),
	PCMCIA_PFC_DEVICE_MANF_CARD(0, 0x0032, 0x1101),
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, fmvj18x_ids);

static struct pcmcia_driver fmvj18x_cs_driver = {
	.owner		= THIS_MODULE,
	.name		= "fmvj18x_cs",
	.probe		= fmvj18x_probe,
	.remove		= fmvj18x_detach,
	.id_table       = fmvj18x_ids,
	.suspend	= fmvj18x_suspend,
	.resume		= fmvj18x_resume,
};

static int __init init_fmvj18x_cs(void)
{
	return pcmcia_register_driver(&fmvj18x_cs_driver);
}

static void __exit exit_fmvj18x_cs(void)
{
	pcmcia_unregister_driver(&fmvj18x_cs_driver);
}

module_init(init_fmvj18x_cs);
module_exit(exit_fmvj18x_cs);

/*====================================================================*/

static irqreturn_t fjn_interrupt(int dummy, void *dev_id)
{
    struct net_device *dev = dev_id;
    local_info_t *lp = netdev_priv(dev);
    unsigned int ioaddr;
    unsigned short tx_stat, rx_stat;

    ioaddr = dev->base_addr;

    /* avoid multiple interrupts */
    outw(0x0000, ioaddr + TX_INTR);

    /* wait for a while */
    udelay(1);

    /* get status */
    tx_stat = inb(ioaddr + TX_STATUS);
    rx_stat = inb(ioaddr + RX_STATUS);

    /* clear status */
    outb(tx_stat, ioaddr + TX_STATUS);
    outb(rx_stat, ioaddr + RX_STATUS);
    
    pr_debug("%s: interrupt, rx_status %02x.\n", dev->name, rx_stat);
    pr_debug("               tx_status %02x.\n", tx_stat);
    
    if (rx_stat || (inb(ioaddr + RX_MODE) & F_BUF_EMP) == 0) {
	/* there is packet(s) in rx buffer */
	fjn_rx(dev);
    }
    if (tx_stat & F_TMT_RDY) {
	dev->stats.tx_packets += lp->sent ;
        lp->sent = 0 ;
	if (lp->tx_queue) {
	    outb(DO_TX | lp->tx_queue, ioaddr + TX_START);
	    lp->sent = lp->tx_queue ;
	    lp->tx_queue = 0;
	    lp->tx_queue_len = 0;
	    dev->trans_start = jiffies;
	} else {
	    lp->tx_started = 0;
	}
	netif_wake_queue(dev);
    }
    pr_debug("%s: exiting interrupt,\n", dev->name);
    pr_debug("    tx_status %02x, rx_status %02x.\n", tx_stat, rx_stat);

    outb(D_TX_INTR, ioaddr + TX_INTR);
    outb(D_RX_INTR, ioaddr + RX_INTR);

    if (lp->base != NULL) {
	/* Ack interrupt for multifunction card */
	writeb(0x01, lp->base+0x802);
	writeb(0x09, lp->base+0x822);
    }

    return IRQ_HANDLED;

} /* fjn_interrupt */

/*====================================================================*/

static void fjn_tx_timeout(struct net_device *dev)
{
    struct local_info_t *lp = netdev_priv(dev);
    unsigned int ioaddr = dev->base_addr;

    netdev_notice(dev, "transmit timed out with status %04x, %s?\n",
		  htons(inw(ioaddr + TX_STATUS)),
		  inb(ioaddr + TX_STATUS) & F_TMT_RDY
		  ? "IRQ conflict" : "network cable problem");
    netdev_notice(dev, "timeout registers: %04x %04x %04x "
		  "%04x %04x %04x %04x %04x.\n",
		  htons(inw(ioaddr + 0)), htons(inw(ioaddr + 2)),
		  htons(inw(ioaddr + 4)), htons(inw(ioaddr + 6)),
		  htons(inw(ioaddr + 8)), htons(inw(ioaddr + 10)),
		  htons(inw(ioaddr + 12)), htons(inw(ioaddr + 14)));
    dev->stats.tx_errors++;
    /* ToDo: We should try to restart the adaptor... */
    local_irq_disable();
    fjn_reset(dev);

    lp->tx_started = 0;
    lp->tx_queue = 0;
    lp->tx_queue_len = 0;
    lp->sent = 0;
    lp->open_time = jiffies;
    local_irq_enable();
    netif_wake_queue(dev);
}

static netdev_tx_t fjn_start_xmit(struct sk_buff *skb,
					struct net_device *dev)
{
    struct local_info_t *lp = netdev_priv(dev);
    unsigned int ioaddr = dev->base_addr;
    short length = skb->len;
    
    if (length < ETH_ZLEN)
    {
    	if (skb_padto(skb, ETH_ZLEN))
    		return NETDEV_TX_OK;
    	length = ETH_ZLEN;
    }

    netif_stop_queue(dev);

    {
	unsigned char *buf = skb->data;

	if (length > ETH_FRAME_LEN) {
	    netdev_notice(dev, "Attempting to send a large packet (%d bytes)\n",
			  length);
	    return NETDEV_TX_BUSY;
	}

	netdev_dbg(dev, "Transmitting a packet of length %lu\n",
		   (unsigned long)skb->len);
	dev->stats.tx_bytes += skb->len;

	/* Disable both interrupts. */
	outw(0x0000, ioaddr + TX_INTR);

	/* wait for a while */
	udelay(1);

	outw(length, ioaddr + DATAPORT);
	outsw(ioaddr + DATAPORT, buf, (length + 1) >> 1);

	lp->tx_queue++;
	lp->tx_queue_len += ((length+3) & ~1);

	if (lp->tx_started == 0) {
	    /* If the Tx is idle, always trigger a transmit. */
	    outb(DO_TX | lp->tx_queue, ioaddr + TX_START);
	    lp->sent = lp->tx_queue ;
	    lp->tx_queue = 0;
	    lp->tx_queue_len = 0;
	    lp->tx_started = 1;
	    netif_start_queue(dev);
	} else {
	    if( sram_config == 0 ) {
		if (lp->tx_queue_len < (4096 - (ETH_FRAME_LEN +2)) )
		    /* Yes, there is room for one more packet. */
		    netif_start_queue(dev);
	    } else {
		if (lp->tx_queue_len < (8192 - (ETH_FRAME_LEN +2)) && 
						lp->tx_queue < 127 )
		    /* Yes, there is room for one more packet. */
		    netif_start_queue(dev);
	    }
	}

	/* Re-enable interrupts */
	outb(D_TX_INTR, ioaddr + TX_INTR);
	outb(D_RX_INTR, ioaddr + RX_INTR);
    }
    dev_kfree_skb (skb);

    return NETDEV_TX_OK;
} /* fjn_start_xmit */

/*====================================================================*/

static void fjn_reset(struct net_device *dev)
{
    struct local_info_t *lp = netdev_priv(dev);
    unsigned int ioaddr = dev->base_addr;
    int i;

    netdev_dbg(dev, "fjn_reset() called\n");

    /* Reset controller */
    if( sram_config == 0 ) 
	outb(CONFIG0_RST, ioaddr + CONFIG_0);
    else
	outb(CONFIG0_RST_1, ioaddr + CONFIG_0);

    /* Power On chip and select bank 0 */
    if (lp->cardtype == MBH10302)
	outb(BANK_0, ioaddr + CONFIG_1);
    else
	outb(BANK_0U, ioaddr + CONFIG_1);

    /* Set Tx modes */
    outb(D_TX_MODE, ioaddr + TX_MODE);
    /* set Rx modes */
    outb(ID_MATCHED, ioaddr + RX_MODE);

    /* Set hardware address */
    for (i = 0; i < 6; i++) 
        outb(dev->dev_addr[i], ioaddr + NODE_ID + i);

    /* (re)initialize the multicast table */
    set_rx_mode(dev);

    /* Switch to bank 2 (runtime mode) */
    if (lp->cardtype == MBH10302)
	outb(BANK_2, ioaddr + CONFIG_1);
    else
	outb(BANK_2U, ioaddr + CONFIG_1);

    /* set 16col ctrl bits */
    if( lp->cardtype == TDK || lp->cardtype == CONTEC) 
        outb(TDK_AUTO_MODE, ioaddr + COL_CTRL);
    else
        outb(AUTO_MODE, ioaddr + COL_CTRL);

    /* clear Reserved Regs */
    outb(0x00, ioaddr + BMPR12);
    outb(0x00, ioaddr + BMPR13);

    /* reset Skip packet reg. */
    outb(0x01, ioaddr + RX_SKIP);

    /* Enable Tx and Rx */
    if( sram_config == 0 )
	outb(CONFIG0_DFL, ioaddr + CONFIG_0);
    else
	outb(CONFIG0_DFL_1, ioaddr + CONFIG_0);

    /* Init receive pointer ? */
    inw(ioaddr + DATAPORT);
    inw(ioaddr + DATAPORT);

    /* Clear all status */
    outb(0xff, ioaddr + TX_STATUS);
    outb(0xff, ioaddr + RX_STATUS);

    if (lp->cardtype == MBH10302)
	outb(INTR_OFF, ioaddr + LAN_CTRL);

    /* Turn on Rx interrupts */
    outb(D_TX_INTR, ioaddr + TX_INTR);
    outb(D_RX_INTR, ioaddr + RX_INTR);

    /* Turn on interrupts from LAN card controller */
    if (lp->cardtype == MBH10302)
	outb(INTR_ON, ioaddr + LAN_CTRL);
} /* fjn_reset */

/*====================================================================*/

static void fjn_rx(struct net_device *dev)
{
    unsigned int ioaddr = dev->base_addr;
    int boguscount = 10;	/* 5 -> 10: by agy 19940922 */

    pr_debug("%s: in rx_packet(), rx_status %02x.\n",
	  dev->name, inb(ioaddr + RX_STATUS));

    while ((inb(ioaddr + RX_MODE) & F_BUF_EMP) == 0) {
	u_short status = inw(ioaddr + DATAPORT);

	netdev_dbg(dev, "Rxing packet mode %02x status %04x.\n",
		   inb(ioaddr + RX_MODE), status);
#ifndef final_version
	if (status == 0) {
	    outb(F_SKP_PKT, ioaddr + RX_SKIP);
	    break;
	}
#endif
	if ((status & 0xF0) != 0x20) {	/* There was an error. */
	    dev->stats.rx_errors++;
	    if (status & F_LEN_ERR) dev->stats.rx_length_errors++;
	    if (status & F_ALG_ERR) dev->stats.rx_frame_errors++;
	    if (status & F_CRC_ERR) dev->stats.rx_crc_errors++;
	    if (status & F_OVR_FLO) dev->stats.rx_over_errors++;
	} else {
	    u_short pkt_len = inw(ioaddr + DATAPORT);
	    /* Malloc up new buffer. */
	    struct sk_buff *skb;

	    if (pkt_len > 1550) {
		netdev_notice(dev, "The FMV-18x claimed a very large packet, size %d\n",
			      pkt_len);
		outb(F_SKP_PKT, ioaddr + RX_SKIP);
		dev->stats.rx_errors++;
		break;
	    }
	    skb = dev_alloc_skb(pkt_len+2);
	    if (skb == NULL) {
		netdev_notice(dev, "Memory squeeze, dropping packet (len %d)\n",
			      pkt_len);
		outb(F_SKP_PKT, ioaddr + RX_SKIP);
		dev->stats.rx_dropped++;
		break;
	    }

	    skb_reserve(skb, 2);
	    insw(ioaddr + DATAPORT, skb_put(skb, pkt_len),
		 (pkt_len + 1) >> 1);
	    skb->protocol = eth_type_trans(skb, dev);

	    {
		int i;
		pr_debug("%s: Rxed packet of length %d: ",
			dev->name, pkt_len);
		for (i = 0; i < 14; i++)
			pr_debug(" %02x", skb->data[i]);
		pr_debug(".\n");
	    }

	    netif_rx(skb);
	    dev->stats.rx_packets++;
	    dev->stats.rx_bytes += pkt_len;
	}
	if (--boguscount <= 0)
	    break;
    }

    /* If any worth-while packets have been received, dev_rint()
	   has done a netif_wake_queue() for us and will work on them
	   when we get to the bottom-half routine. */
/*
    if (lp->cardtype != TDK) {
	int i;
	for (i = 0; i < 20; i++) {
	    if ((inb(ioaddr + RX_MODE) & F_BUF_EMP) == F_BUF_EMP)
		break;
	    (void)inw(ioaddr + DATAPORT);  /+ dummy status read +/
	    outb(F_SKP_PKT, ioaddr + RX_SKIP);
	}

	if (i > 0)
	    pr_debug("%s: Exint Rx packet with mode %02x after "
		  "%d ticks.\n", dev->name, inb(ioaddr + RX_MODE), i);
    }
*/
} /* fjn_rx */

/*====================================================================*/

static void netdev_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	snprintf(info->bus_info, sizeof(info->bus_info),
		"PCMCIA 0x%lx", dev->base_addr);
}

static const struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
};

static int fjn_config(struct net_device *dev, struct ifmap *map){
    return 0;
}

static int fjn_open(struct net_device *dev)
{
    struct local_info_t *lp = netdev_priv(dev);
    struct pcmcia_device *link = lp->p_dev;

    pr_debug("fjn_open('%s').\n", dev->name);

    if (!pcmcia_dev_present(link))
	return -ENODEV;
    
    link->open++;
    
    fjn_reset(dev);
    
    lp->tx_started = 0;
    lp->tx_queue = 0;
    lp->tx_queue_len = 0;
    lp->open_time = jiffies;
    netif_start_queue(dev);
    
    return 0;
} /* fjn_open */

/*====================================================================*/

static int fjn_close(struct net_device *dev)
{
    struct local_info_t *lp = netdev_priv(dev);
    struct pcmcia_device *link = lp->p_dev;
    unsigned int ioaddr = dev->base_addr;

    pr_debug("fjn_close('%s').\n", dev->name);

    lp->open_time = 0;
    netif_stop_queue(dev);

    /* Set configuration register 0 to disable Tx and Rx. */
    if( sram_config == 0 ) 
	outb(CONFIG0_RST ,ioaddr + CONFIG_0);
    else
	outb(CONFIG0_RST_1 ,ioaddr + CONFIG_0);

    /* Update the statistics -- ToDo. */

    /* Power-down the chip.  Green, green, green! */
    outb(CHIP_OFF ,ioaddr + CONFIG_1);

    /* Set the ethernet adaptor disable IRQ */
    if (lp->cardtype == MBH10302)
	outb(INTR_OFF, ioaddr + LAN_CTRL);

    link->open--;

    return 0;
} /* fjn_close */

/*====================================================================*/

/*
  Set the multicast/promiscuous mode for this adaptor.
*/

static void set_rx_mode(struct net_device *dev)
{
    unsigned int ioaddr = dev->base_addr;
    u_char mc_filter[8];		 /* Multicast hash filter */
    u_long flags;
    int i;
    
    int saved_bank;
    int saved_config_0 = inb(ioaddr + CONFIG_0);
     
    local_irq_save(flags); 

    /* Disable Tx and Rx */
    if (sram_config == 0) 
	outb(CONFIG0_RST, ioaddr + CONFIG_0);
    else
	outb(CONFIG0_RST_1, ioaddr + CONFIG_0);

    if (dev->flags & IFF_PROMISC) {
	memset(mc_filter, 0xff, sizeof(mc_filter));
	outb(3, ioaddr + RX_MODE);	/* Enable promiscuous mode */
    } else if (netdev_mc_count(dev) > MC_FILTERBREAK ||
	       (dev->flags & IFF_ALLMULTI)) {
	/* Too many to filter perfectly -- accept all multicasts. */
	memset(mc_filter, 0xff, sizeof(mc_filter));
	outb(2, ioaddr + RX_MODE);	/* Use normal mode. */
    } else if (netdev_mc_empty(dev)) {
	memset(mc_filter, 0x00, sizeof(mc_filter));
	outb(1, ioaddr + RX_MODE);	/* Ignore almost all multicasts. */
    } else {
	struct netdev_hw_addr *ha;

	memset(mc_filter, 0, sizeof(mc_filter));
	netdev_for_each_mc_addr(ha, dev) {
	    unsigned int bit = ether_crc_le(ETH_ALEN, ha->addr) >> 26;
	    mc_filter[bit >> 3] |= (1 << (bit & 7));
	}
	outb(2, ioaddr + RX_MODE);	/* Use normal mode. */
    }

    /* Switch to bank 1 and set the multicast table. */
    saved_bank = inb(ioaddr + CONFIG_1);
    outb(0xe4, ioaddr + CONFIG_1);

    for (i = 0; i < 8; i++)
	outb(mc_filter[i], ioaddr + MAR_ADR + i);
    outb(saved_bank, ioaddr + CONFIG_1);

    outb(saved_config_0, ioaddr + CONFIG_0);

    local_irq_restore(flags);
}
