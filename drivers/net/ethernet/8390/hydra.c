/* New Hydra driver using generic 8390 core */
/* Based on old hydra driver by Topi Kanerva (topi@susanna.oulu.fi) */

/* This file is subject to the terms and conditions of the GNU General      */
/* Public License.  See the file COPYING in the main directory of the       */
/* Linux distribution for more details.                                     */

/* Peter De Schrijver (p2@mind.be) */
/* Oldenburg 2000 */

/* The Amiganet is a Zorro-II board made by Hydra Systems. It contains a    */
/* NS8390 NIC (network interface controller) clone, 16 or 64K on-board RAM  */
/* and 10BASE-2 (thin coax) and AUI connectors.                             */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>
#include <linux/zorro.h>

#define EI_SHIFT(x)	(ei_local->reg_offset[x])
#define ei_inb(port)   in_8(port)
#define ei_outb(val,port)  out_8(port,val)
#define ei_inb_p(port)   in_8(port)
#define ei_outb_p(val,port)  out_8(port,val)

static const char version[] =
    "8390.c:v1.10cvs 9/23/94 Donald Becker (becker@cesdis.gsfc.nasa.gov)\n";

#include "lib8390.c"

#define NE_EN0_DCFG     (0x0e*2)

#define NESM_START_PG   0x0    /* First page of TX buffer */
#define NESM_STOP_PG    0x40    /* Last page +1 of RX ring */

#define HYDRA_NIC_BASE 0xffe1
#define HYDRA_ADDRPROM 0xffc0
#define HYDRA_VERSION "v3.0alpha"

#define WORDSWAP(a)     ((((a)>>8)&0xff) | ((a)<<8))


static int hydra_init_one(struct zorro_dev *z,
				    const struct zorro_device_id *ent);
static int hydra_init(struct zorro_dev *z);
static int hydra_open(struct net_device *dev);
static int hydra_close(struct net_device *dev);
static void hydra_reset_8390(struct net_device *dev);
static void hydra_get_8390_hdr(struct net_device *dev,
			       struct e8390_pkt_hdr *hdr, int ring_page);
static void hydra_block_input(struct net_device *dev, int count,
			      struct sk_buff *skb, int ring_offset);
static void hydra_block_output(struct net_device *dev, int count,
			       const unsigned char *buf, int start_page);
static void hydra_remove_one(struct zorro_dev *z);

static struct zorro_device_id hydra_zorro_tbl[] = {
    { ZORRO_PROD_HYDRA_SYSTEMS_AMIGANET },
    { 0 }
};
MODULE_DEVICE_TABLE(zorro, hydra_zorro_tbl);

static struct zorro_driver hydra_driver = {
    .name	= "hydra",
    .id_table	= hydra_zorro_tbl,
    .probe	= hydra_init_one,
    .remove	= hydra_remove_one,
};

static int hydra_init_one(struct zorro_dev *z,
			  const struct zorro_device_id *ent)
{
    int err;

    if (!request_mem_region(z->resource.start, 0x10000, "Hydra"))
	return -EBUSY;
    if ((err = hydra_init(z))) {
	release_mem_region(z->resource.start, 0x10000);
	return -EBUSY;
    }
    return 0;
}

static const struct net_device_ops hydra_netdev_ops = {
	.ndo_open		= hydra_open,
	.ndo_stop		= hydra_close,

	.ndo_start_xmit		= __ei_start_xmit,
	.ndo_tx_timeout		= __ei_tx_timeout,
	.ndo_get_stats		= __ei_get_stats,
	.ndo_set_rx_mode	= __ei_set_multicast_list,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_change_mtu		= eth_change_mtu,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= __ei_poll,
#endif
};

static int hydra_init(struct zorro_dev *z)
{
    struct net_device *dev;
    unsigned long board = (unsigned long)ZTWO_VADDR(z->resource.start);
    unsigned long ioaddr = board+HYDRA_NIC_BASE;
    const char name[] = "NE2000";
    int start_page, stop_page;
    int j;
    int err;

    static u32 hydra_offsets[16] = {
	0x00, 0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e,
	0x10, 0x12, 0x14, 0x16, 0x18, 0x1a, 0x1c, 0x1e,
    };

    dev = ____alloc_ei_netdev(0);
    if (!dev)
	return -ENOMEM;

    for (j = 0; j < ETH_ALEN; j++)
	dev->dev_addr[j] = *((u8 *)(board + HYDRA_ADDRPROM + 2*j));

    /* We must set the 8390 for word mode. */
    z_writeb(0x4b, ioaddr + NE_EN0_DCFG);
    start_page = NESM_START_PG;
    stop_page = NESM_STOP_PG;

    dev->base_addr = ioaddr;
    dev->irq = IRQ_AMIGA_PORTS;

    /* Install the Interrupt handler */
    if (request_irq(IRQ_AMIGA_PORTS, __ei_interrupt, IRQF_SHARED, "Hydra Ethernet",
		    dev)) {
	free_netdev(dev);
	return -EAGAIN;
    }

    ei_status.name = name;
    ei_status.tx_start_page = start_page;
    ei_status.stop_page = stop_page;
    ei_status.word16 = 1;
    ei_status.bigendian = 1;

    ei_status.rx_start_page = start_page + TX_PAGES;

    ei_status.reset_8390 = hydra_reset_8390;
    ei_status.block_input = hydra_block_input;
    ei_status.block_output = hydra_block_output;
    ei_status.get_8390_hdr = hydra_get_8390_hdr;
    ei_status.reg_offset = hydra_offsets;

    dev->netdev_ops = &hydra_netdev_ops;
    __NS8390_init(dev, 0);

    err = register_netdev(dev);
    if (err) {
	free_irq(IRQ_AMIGA_PORTS, dev);
	free_netdev(dev);
	return err;
    }

    zorro_set_drvdata(z, dev);

    pr_info("%s: Hydra at %pR, address %pM (hydra.c " HYDRA_VERSION ")\n",
	    dev->name, &z->resource, dev->dev_addr);

    return 0;
}

static int hydra_open(struct net_device *dev)
{
    __ei_open(dev);
    return 0;
}

static int hydra_close(struct net_device *dev)
{
    if (ei_debug > 1)
	printk(KERN_DEBUG "%s: Shutting down ethercard.\n", dev->name);
    __ei_close(dev);
    return 0;
}

static void hydra_reset_8390(struct net_device *dev)
{
    printk(KERN_INFO "Hydra hw reset not there\n");
}

static void hydra_get_8390_hdr(struct net_device *dev,
			       struct e8390_pkt_hdr *hdr, int ring_page)
{
    int nic_base = dev->base_addr;
    short *ptrs;
    unsigned long hdr_start= (nic_base-HYDRA_NIC_BASE) +
			     ((ring_page - NESM_START_PG)<<8);
    ptrs = (short *)hdr;

    *(ptrs++) = z_readw(hdr_start);
    *((short *)hdr) = WORDSWAP(*((short *)hdr));
    hdr_start += 2;
    *(ptrs++) = z_readw(hdr_start);
    *((short *)hdr+1) = WORDSWAP(*((short *)hdr+1));
}

static void hydra_block_input(struct net_device *dev, int count,
			      struct sk_buff *skb, int ring_offset)
{
    unsigned long nic_base = dev->base_addr;
    unsigned long mem_base = nic_base - HYDRA_NIC_BASE;
    unsigned long xfer_start = mem_base + ring_offset - (NESM_START_PG<<8);

    if (count&1)
	count++;

    if (xfer_start+count >  mem_base + (NESM_STOP_PG<<8)) {
	int semi_count = (mem_base + (NESM_STOP_PG<<8)) - xfer_start;

	z_memcpy_fromio(skb->data,xfer_start,semi_count);
	count -= semi_count;
	z_memcpy_fromio(skb->data+semi_count, mem_base, count);
    } else
	z_memcpy_fromio(skb->data, xfer_start,count);

}

static void hydra_block_output(struct net_device *dev, int count,
			       const unsigned char *buf, int start_page)
{
    unsigned long nic_base = dev->base_addr;
    unsigned long mem_base = nic_base - HYDRA_NIC_BASE;

    if (count&1)
	count++;

    z_memcpy_toio(mem_base+((start_page - NESM_START_PG)<<8), buf, count);
}

static void hydra_remove_one(struct zorro_dev *z)
{
    struct net_device *dev = zorro_get_drvdata(z);

    unregister_netdev(dev);
    free_irq(IRQ_AMIGA_PORTS, dev);
    release_mem_region(ZTWO_PADDR(dev->base_addr)-HYDRA_NIC_BASE, 0x10000);
    free_netdev(dev);
}

static int __init hydra_init_module(void)
{
    return zorro_register_driver(&hydra_driver);
}

static void __exit hydra_cleanup_module(void)
{
    zorro_unregister_driver(&hydra_driver);
}

module_init(hydra_init_module);
module_exit(hydra_cleanup_module);

MODULE_LICENSE("GPL");
