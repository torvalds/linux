/*
 * ni6510 (am7990 'lance' chip) driver for Linux-net-3
 * BETAcode v0.71 (96/09/29) for 2.0.0 (or later)
 * copyrights (c) 1994,1995,1996 by M.Hipp
 *
 * This driver can handle the old ni6510 board and the newer ni6510
 * EtherBlaster. (probably it also works with every full NE2100
 * compatible card)
 *
 * driver probes: io: 0x360,0x300,0x320,0x340 / dma: 3,5,6,7
 *
 * This is an extension to the Linux operating system, and is covered by the
 * same GNU General Public License that covers the Linux-kernel.
 *
 * comments/bugs/suggestions can be sent to:
 *   Michael Hipp
 *   email: hippm@informatik.uni-tuebingen.de
 *
 * sources:
 *   some things are from the 'ni6510-packet-driver for dos by Russ Nelson'
 *   and from the original drivers by D.Becker
 *
 * known problems:
 *   - on some PCI boards (including my own) the card/board/ISA-bridge has
 *     problems with bus master DMA. This results in lotsa overruns.
 *     It may help to '#define RCV_PARANOIA_CHECK' or try to #undef
 *     the XMT and RCV_VIA_SKB option .. this reduces driver performance.
 *     Or just play with your BIOS options to optimize ISA-DMA access.
 *     Maybe you also wanna play with the LOW_PERFORAMCE and MID_PERFORMANCE
 *     defines -> please report me your experience then
 *   - Harald reported for ASUS SP3G mainboards, that you should use
 *     the 'optimal settings' from the user's manual on page 3-12!
 *
 * credits:
 *   thanx to Jason Sullivan for sending me a ni6510 card!
 *   lot of debug runs with ASUS SP3G Boards (Intel Saturn) by Harald Koenig
 *
 * simple performance test: (486DX-33/Ni6510-EB receives from 486DX4-100/Ni6510-EB)
 *    average: FTP -> 8384421 bytes received in 8.5 seconds
 *           (no RCV_VIA_SKB,no XMT_VIA_SKB,PARANOIA_CHECK,4 XMIT BUFS, 8 RCV_BUFFS)
 *    peak: FTP -> 8384421 bytes received in 7.5 seconds
 *           (RCV_VIA_SKB,XMT_VIA_SKB,no PARANOIA_CHECK,1(!) XMIT BUF, 16 RCV BUFFS)
 */

/*
 * 99.Jun.8: added support for /proc/net/dev byte count for xosview (HK)
 * 96.Sept.29: virt_to_bus stuff added for new memory modell
 * 96.April.29: Added Harald Koenig's Patches (MH)
 * 96.April.13: enhanced error handling .. more tests (MH)
 * 96.April.5/6: a lot of performance tests. Got it stable now (hopefully) (MH)
 * 96.April.1: (no joke ;) .. added EtherBlaster and Module support (MH)
 * 96.Feb.19: fixed a few bugs .. cleanups .. tested for 1.3.66 (MH)
 *            hopefully no more 16MB limit
 *
 * 95.Nov.18: multicast tweaked (AC).
 *
 * 94.Aug.22: changes in xmit_intr (ack more than one xmitted-packet), ni65_send_packet (p->lock) (MH)
 *
 * 94.July.16: fixed bugs in recv_skb and skb-alloc stuff  (MH)
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/dma.h>

#include "ni65.h"

/*
 * the current setting allows an acceptable performance
 * for 'RCV_PARANOIA_CHECK' read the 'known problems' part in
 * the header of this file
 * 'invert' the defines for max. performance. This may cause DMA problems
 * on some boards (e.g on my ASUS SP3G)
 */
#undef XMT_VIA_SKB
#undef RCV_VIA_SKB
#define RCV_PARANOIA_CHECK

#define MID_PERFORMANCE

#if   defined( LOW_PERFORMANCE )
 static int isa0=7,isa1=7,csr80=0x0c10;
#elif defined( MID_PERFORMANCE )
 static int isa0=5,isa1=5,csr80=0x2810;
#else	/* high performance */
 static int isa0=4,isa1=4,csr80=0x0017;
#endif

/*
 * a few card/vendor specific defines
 */
#define NI65_ID0    0x00
#define NI65_ID1    0x55
#define NI65_EB_ID0 0x52
#define NI65_EB_ID1 0x44
#define NE2100_ID0  0x57
#define NE2100_ID1  0x57

#define PORT p->cmdr_addr

/*
 * buffer configuration
 */
#if 1
#define RMDNUM 16
#define RMDNUMMASK 0x80000000
#else
#define RMDNUM 8
#define RMDNUMMASK 0x60000000 /* log2(RMDNUM)<<29 */
#endif

#if 0
#define TMDNUM 1
#define TMDNUMMASK 0x00000000
#else
#define TMDNUM 4
#define TMDNUMMASK 0x40000000 /* log2(TMDNUM)<<29 */
#endif

/* slightly oversized */
#define R_BUF_SIZE 1544
#define T_BUF_SIZE 1544

/*
 * lance register defines
 */
#define L_DATAREG 0x00
#define L_ADDRREG 0x02
#define L_RESET   0x04
#define L_CONFIG  0x05
#define L_BUSIF   0x06

/*
 * to access the lance/am7990-regs, you have to write
 * reg-number into L_ADDRREG, then you can access it using L_DATAREG
 */
#define CSR0  0x00
#define CSR1  0x01
#define CSR2  0x02
#define CSR3  0x03

#define INIT_RING_BEFORE_START	0x1
#define FULL_RESET_ON_ERROR	0x2

#if 0
#define writereg(val,reg) {outw(reg,PORT+L_ADDRREG);inw(PORT+L_ADDRREG); \
                           outw(val,PORT+L_DATAREG);inw(PORT+L_DATAREG);}
#define readreg(reg) (outw(reg,PORT+L_ADDRREG),inw(PORT+L_ADDRREG),\
                       inw(PORT+L_DATAREG))
#if 0
#define writedatareg(val) {outw(val,PORT+L_DATAREG);inw(PORT+L_DATAREG);}
#else
#define writedatareg(val) {  writereg(val,CSR0); }
#endif
#else
#define writereg(val,reg) {outw(reg,PORT+L_ADDRREG);outw(val,PORT+L_DATAREG);}
#define readreg(reg) (outw(reg,PORT+L_ADDRREG),inw(PORT+L_DATAREG))
#define writedatareg(val) { writereg(val,CSR0); }
#endif

static unsigned char ni_vendor[] = { 0x02,0x07,0x01 };

static struct card {
	unsigned char id0,id1;
	short id_offset;
	short total_size;
	short cmd_offset;
	short addr_offset;
	unsigned char *vendor_id;
	char *cardname;
	unsigned long config;
} cards[] = {
	{
		.id0	     = NI65_ID0,
		.id1	     = NI65_ID1,
		.id_offset   = 0x0e,
		.total_size  = 0x10,
		.cmd_offset  = 0x0,
		.addr_offset = 0x8,
		.vendor_id   = ni_vendor,
		.cardname    = "ni6510",
		.config	     = 0x1,
       	},
	{
		.id0	     = NI65_EB_ID0,
		.id1	     = NI65_EB_ID1,
		.id_offset   = 0x0e,
		.total_size  = 0x18,
		.cmd_offset  = 0x10,
		.addr_offset = 0x0,
		.vendor_id   = ni_vendor,
		.cardname    = "ni6510 EtherBlaster",
		.config	     = 0x2,
       	},
	{
		.id0	     = NE2100_ID0,
		.id1	     = NE2100_ID1,
		.id_offset   = 0x0e,
		.total_size  = 0x18,
		.cmd_offset  = 0x10,
		.addr_offset = 0x0,
		.vendor_id   = NULL,
		.cardname    = "generic NE2100",
		.config	     = 0x0,
	},
};
#define NUM_CARDS 3

struct priv
{
	struct rmd rmdhead[RMDNUM];
	struct tmd tmdhead[TMDNUM];
	struct init_block ib;
	int rmdnum;
	int tmdnum,tmdlast;
#ifdef RCV_VIA_SKB
	struct sk_buff *recv_skb[RMDNUM];
#else
	void *recvbounce[RMDNUM];
#endif
#ifdef XMT_VIA_SKB
	struct sk_buff *tmd_skb[TMDNUM];
#endif
	void *tmdbounce[TMDNUM];
	int tmdbouncenum;
	int lock,xmit_queued;

	void *self;
	int cmdr_addr;
	int cardno;
	int features;
	spinlock_t ring_lock;
};

static int  ni65_probe1(struct net_device *dev,int);
static irqreturn_t ni65_interrupt(int irq, void * dev_id);
static void ni65_recv_intr(struct net_device *dev,int);
static void ni65_xmit_intr(struct net_device *dev,int);
static int  ni65_open(struct net_device *dev);
static int  ni65_lance_reinit(struct net_device *dev);
static void ni65_init_lance(struct priv *p,unsigned char*,int,int);
static netdev_tx_t ni65_send_packet(struct sk_buff *skb,
				    struct net_device *dev);
static void  ni65_timeout(struct net_device *dev);
static int  ni65_close(struct net_device *dev);
static int  ni65_alloc_buffer(struct net_device *dev);
static void ni65_free_buffer(struct priv *p);
static void set_multicast_list(struct net_device *dev);

static int irqtab[] __initdata = { 9,12,15,5 }; /* irq config-translate */
static int dmatab[] __initdata = { 0,3,5,6,7 }; /* dma config-translate and autodetect */

static int debuglevel = 1;

/*
 * set 'performance' registers .. we must STOP lance for that
 */
static void ni65_set_performance(struct priv *p)
{
	writereg(CSR0_STOP | CSR0_CLRALL,CSR0); /* STOP */

	if( !(cards[p->cardno].config & 0x02) )
		return;

	outw(80,PORT+L_ADDRREG);
	if(inw(PORT+L_ADDRREG) != 80)
		return;

	writereg( (csr80 & 0x3fff) ,80); /* FIFO watermarks */
	outw(0,PORT+L_ADDRREG);
	outw((short)isa0,PORT+L_BUSIF); /* write ISA 0: DMA_R : isa0 * 50ns */
	outw(1,PORT+L_ADDRREG);
	outw((short)isa1,PORT+L_BUSIF); /* write ISA 1: DMA_W : isa1 * 50ns	*/

	outw(CSR0,PORT+L_ADDRREG);	/* switch back to CSR0 */
}

/*
 * open interface (up)
 */
static int ni65_open(struct net_device *dev)
{
	struct priv *p = dev->ml_priv;
	int irqval = request_irq(dev->irq, ni65_interrupt,0,
                        cards[p->cardno].cardname,dev);
	if (irqval) {
		printk(KERN_ERR "%s: unable to get IRQ %d (irqval=%d).\n",
		          dev->name,dev->irq, irqval);
		return -EAGAIN;
	}

	if(ni65_lance_reinit(dev))
	{
		netif_start_queue(dev);
		return 0;
	}
	else
	{
		free_irq(dev->irq,dev);
		return -EAGAIN;
	}
}

/*
 * close interface (down)
 */
static int ni65_close(struct net_device *dev)
{
	struct priv *p = dev->ml_priv;

	netif_stop_queue(dev);

	outw(inw(PORT+L_RESET),PORT+L_RESET); /* that's the hard way */

#ifdef XMT_VIA_SKB
	{
		int i;
		for(i=0;i<TMDNUM;i++)
		{
			if(p->tmd_skb[i]) {
				dev_kfree_skb(p->tmd_skb[i]);
				p->tmd_skb[i] = NULL;
			}
		}
	}
#endif
	free_irq(dev->irq,dev);
	return 0;
}

static void cleanup_card(struct net_device *dev)
{
	struct priv *p = dev->ml_priv;
	disable_dma(dev->dma);
	free_dma(dev->dma);
	release_region(dev->base_addr, cards[p->cardno].total_size);
	ni65_free_buffer(p);
}

/* set: io,irq,dma or set it when calling insmod */
static int irq;
static int io;
static int dma;

/*
 * Probe The Card (not the lance-chip)
 */
struct net_device * __init ni65_probe(int unit)
{
	struct net_device *dev = alloc_etherdev(0);
	static const int ports[] = { 0x360, 0x300, 0x320, 0x340, 0 };
	const int *port;
	int err = 0;

	if (!dev)
		return ERR_PTR(-ENOMEM);

	if (unit >= 0) {
		sprintf(dev->name, "eth%d", unit);
		netdev_boot_setup_check(dev);
		irq = dev->irq;
		dma = dev->dma;
	} else {
		dev->base_addr = io;
	}

	if (dev->base_addr > 0x1ff) { /* Check a single specified location. */
		err = ni65_probe1(dev, dev->base_addr);
	} else if (dev->base_addr > 0) { /* Don't probe at all. */
		err = -ENXIO;
	} else {
		for (port = ports; *port && ni65_probe1(dev, *port); port++)
			;
		if (!*port)
			err = -ENODEV;
	}
	if (err)
		goto out;

	err = register_netdev(dev);
	if (err)
		goto out1;
	return dev;
out1:
	cleanup_card(dev);
out:
	free_netdev(dev);
	return ERR_PTR(err);
}

static const struct net_device_ops ni65_netdev_ops = {
	.ndo_open		= ni65_open,
	.ndo_stop		= ni65_close,
	.ndo_start_xmit		= ni65_send_packet,
	.ndo_tx_timeout		= ni65_timeout,
	.ndo_set_rx_mode	= set_multicast_list,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

/*
 * this is the real card probe ..
 */
static int __init ni65_probe1(struct net_device *dev,int ioaddr)
{
	int i,j;
	struct priv *p;
	unsigned long flags;

	dev->irq = irq;
	dev->dma = dma;

	for(i=0;i<NUM_CARDS;i++) {
		if(!request_region(ioaddr, cards[i].total_size, cards[i].cardname))
			continue;
		if(cards[i].id_offset >= 0) {
			if(inb(ioaddr+cards[i].id_offset+0) != cards[i].id0 ||
				 inb(ioaddr+cards[i].id_offset+1) != cards[i].id1) {
				 release_region(ioaddr, cards[i].total_size);
				 continue;
			}
		}
		if(cards[i].vendor_id) {
			for(j=0;j<3;j++)
				if(inb(ioaddr+cards[i].addr_offset+j) != cards[i].vendor_id[j]) {
					release_region(ioaddr, cards[i].total_size);
					continue;
			  }
		}
		break;
	}
	if(i == NUM_CARDS)
		return -ENODEV;

	for(j=0;j<6;j++)
		dev->dev_addr[j] = inb(ioaddr+cards[i].addr_offset+j);

	if( (j=ni65_alloc_buffer(dev)) < 0) {
		release_region(ioaddr, cards[i].total_size);
		return j;
	}
	p = dev->ml_priv;
	p->cmdr_addr = ioaddr + cards[i].cmd_offset;
	p->cardno = i;
	spin_lock_init(&p->ring_lock);

	printk(KERN_INFO "%s: %s found at %#3x, ", dev->name, cards[p->cardno].cardname , ioaddr);

	outw(inw(PORT+L_RESET),PORT+L_RESET); /* first: reset the card */
	if( (j=readreg(CSR0)) != 0x4) {
		 printk("failed.\n");
		 printk(KERN_ERR "%s: Can't RESET card: %04x\n", dev->name, j);
		 ni65_free_buffer(p);
		 release_region(ioaddr, cards[p->cardno].total_size);
		 return -EAGAIN;
	}

	outw(88,PORT+L_ADDRREG);
	if(inw(PORT+L_ADDRREG) == 88) {
		unsigned long v;
		v = inw(PORT+L_DATAREG);
		v <<= 16;
		outw(89,PORT+L_ADDRREG);
		v |= inw(PORT+L_DATAREG);
		printk("Version %#08lx, ",v);
		p->features = INIT_RING_BEFORE_START;
	}
	else {
		printk("ancient LANCE, ");
		p->features = 0x0;
	}

	if(test_bit(0,&cards[i].config)) {
		dev->irq = irqtab[(inw(ioaddr+L_CONFIG)>>2)&3];
		dev->dma = dmatab[inw(ioaddr+L_CONFIG)&3];
		printk("IRQ %d (from card), DMA %d (from card).\n",dev->irq,dev->dma);
	}
	else {
		if(dev->dma == 0) {
		/* 'stuck test' from lance.c */
			unsigned long dma_channels =
				((inb(DMA1_STAT_REG) >> 4) & 0x0f)
				| (inb(DMA2_STAT_REG) & 0xf0);
			for(i=1;i<5;i++) {
				int dma = dmatab[i];
				if(test_bit(dma,&dma_channels) || request_dma(dma,"ni6510"))
					continue;

				flags=claim_dma_lock();
				disable_dma(dma);
				set_dma_mode(dma,DMA_MODE_CASCADE);
				enable_dma(dma);
				release_dma_lock(flags);

				ni65_init_lance(p,dev->dev_addr,0,0); /* trigger memory access */

				flags=claim_dma_lock();
				disable_dma(dma);
				free_dma(dma);
				release_dma_lock(flags);

				if(readreg(CSR0) & CSR0_IDON)
					break;
			}
			if(i == 5) {
				printk("failed.\n");
				printk(KERN_ERR "%s: Can't detect DMA channel!\n", dev->name);
				ni65_free_buffer(p);
				release_region(ioaddr, cards[p->cardno].total_size);
				return -EAGAIN;
			}
			dev->dma = dmatab[i];
			printk("DMA %d (autodetected), ",dev->dma);
		}
		else
			printk("DMA %d (assigned), ",dev->dma);

		if(dev->irq < 2)
		{
			unsigned long irq_mask;

			ni65_init_lance(p,dev->dev_addr,0,0);
			irq_mask = probe_irq_on();
			writereg(CSR0_INIT|CSR0_INEA,CSR0); /* trigger interrupt */
			msleep(20);
			dev->irq = probe_irq_off(irq_mask);
			if(!dev->irq)
			{
				printk("Failed to detect IRQ line!\n");
				ni65_free_buffer(p);
				release_region(ioaddr, cards[p->cardno].total_size);
				return -EAGAIN;
			}
			printk("IRQ %d (autodetected).\n",dev->irq);
		}
		else
			printk("IRQ %d (assigned).\n",dev->irq);
	}

	if(request_dma(dev->dma, cards[p->cardno].cardname ) != 0)
	{
		printk(KERN_ERR "%s: Can't request dma-channel %d\n",dev->name,(int) dev->dma);
		ni65_free_buffer(p);
		release_region(ioaddr, cards[p->cardno].total_size);
		return -EAGAIN;
	}

	dev->base_addr = ioaddr;
	dev->netdev_ops = &ni65_netdev_ops;
	dev->watchdog_timeo	= HZ/2;

	return 0; /* everything is OK */
}

/*
 * set lance register and trigger init
 */
static void ni65_init_lance(struct priv *p,unsigned char *daddr,int filter,int mode)
{
	int i;
	u32 pib;

	writereg(CSR0_CLRALL|CSR0_STOP,CSR0);

	for(i=0;i<6;i++)
		p->ib.eaddr[i] = daddr[i];

	for(i=0;i<8;i++)
		p->ib.filter[i] = filter;
	p->ib.mode = mode;

	p->ib.trp = (u32) isa_virt_to_bus(p->tmdhead) | TMDNUMMASK;
	p->ib.rrp = (u32) isa_virt_to_bus(p->rmdhead) | RMDNUMMASK;
	writereg(0,CSR3);	/* busmaster/no word-swap */
	pib = (u32) isa_virt_to_bus(&p->ib);
	writereg(pib & 0xffff,CSR1);
	writereg(pib >> 16,CSR2);

	writereg(CSR0_INIT,CSR0); /* this changes L_ADDRREG to CSR0 */

	for(i=0;i<32;i++)
	{
		mdelay(4);
		if(inw(PORT+L_DATAREG) & (CSR0_IDON | CSR0_MERR) )
			break; /* init ok ? */
	}
}

/*
 * allocate memory area and check the 16MB border
 */
static void *ni65_alloc_mem(struct net_device *dev,char *what,int size,int type)
{
	struct sk_buff *skb=NULL;
	unsigned char *ptr;
	void *ret;

	if(type) {
		ret = skb = alloc_skb(2+16+size,GFP_KERNEL|GFP_DMA);
		if(!skb) {
			printk(KERN_WARNING "%s: unable to allocate %s memory.\n",dev->name,what);
			return NULL;
		}
		skb_reserve(skb,2+16);
		skb_put(skb,R_BUF_SIZE);	 /* grab the whole space .. (not necessary) */
		ptr = skb->data;
	}
	else {
		ret = ptr = kmalloc(T_BUF_SIZE,GFP_KERNEL | GFP_DMA);
		if(!ret)
			return NULL;
	}
	if( (u32) virt_to_phys(ptr+size) > 0x1000000) {
		printk(KERN_WARNING "%s: unable to allocate %s memory in lower 16MB!\n",dev->name,what);
		if(type)
			kfree_skb(skb);
		else
			kfree(ptr);
		return NULL;
	}
	return ret;
}

/*
 * allocate all memory structures .. send/recv buffers etc ...
 */
static int ni65_alloc_buffer(struct net_device *dev)
{
	unsigned char *ptr;
	struct priv *p;
	int i;

	/*
	 * we need 8-aligned memory ..
	 */
	ptr = ni65_alloc_mem(dev,"BUFFER",sizeof(struct priv)+8,0);
	if(!ptr)
		return -ENOMEM;

	p = dev->ml_priv = (struct priv *) (((unsigned long) ptr + 7) & ~0x7);
	memset((char *)p, 0, sizeof(struct priv));
	p->self = ptr;

	for(i=0;i<TMDNUM;i++)
	{
#ifdef XMT_VIA_SKB
		p->tmd_skb[i] = NULL;
#endif
		p->tmdbounce[i] = ni65_alloc_mem(dev,"XMIT",T_BUF_SIZE,0);
		if(!p->tmdbounce[i]) {
			ni65_free_buffer(p);
			return -ENOMEM;
		}
	}

	for(i=0;i<RMDNUM;i++)
	{
#ifdef RCV_VIA_SKB
		p->recv_skb[i] = ni65_alloc_mem(dev,"RECV",R_BUF_SIZE,1);
		if(!p->recv_skb[i]) {
			ni65_free_buffer(p);
			return -ENOMEM;
		}
#else
		p->recvbounce[i] = ni65_alloc_mem(dev,"RECV",R_BUF_SIZE,0);
		if(!p->recvbounce[i]) {
			ni65_free_buffer(p);
			return -ENOMEM;
		}
#endif
	}

	return 0; /* everything is OK */
}

/*
 * free buffers and private struct
 */
static void ni65_free_buffer(struct priv *p)
{
	int i;

	if(!p)
		return;

	for(i=0;i<TMDNUM;i++) {
		kfree(p->tmdbounce[i]);
#ifdef XMT_VIA_SKB
		if(p->tmd_skb[i])
			dev_kfree_skb(p->tmd_skb[i]);
#endif
	}

	for(i=0;i<RMDNUM;i++)
	{
#ifdef RCV_VIA_SKB
		if(p->recv_skb[i])
			dev_kfree_skb(p->recv_skb[i]);
#else
		kfree(p->recvbounce[i]);
#endif
	}
	kfree(p->self);
}


/*
 * stop and (re)start lance .. e.g after an error
 */
static void ni65_stop_start(struct net_device *dev,struct priv *p)
{
	int csr0 = CSR0_INEA;

	writedatareg(CSR0_STOP);

	if(debuglevel > 1)
		printk(KERN_DEBUG "ni65_stop_start\n");

	if(p->features & INIT_RING_BEFORE_START) {
		int i;
#ifdef XMT_VIA_SKB
		struct sk_buff *skb_save[TMDNUM];
#endif
		unsigned long buffer[TMDNUM];
		short blen[TMDNUM];

		if(p->xmit_queued) {
			while(1) {
				if((p->tmdhead[p->tmdlast].u.s.status & XMIT_OWN))
					break;
				p->tmdlast = (p->tmdlast + 1) & (TMDNUM-1);
				if(p->tmdlast == p->tmdnum)
					break;
			}
		}

		for(i=0;i<TMDNUM;i++) {
			struct tmd *tmdp = p->tmdhead + i;
#ifdef XMT_VIA_SKB
			skb_save[i] = p->tmd_skb[i];
#endif
			buffer[i] = (u32) isa_bus_to_virt(tmdp->u.buffer);
			blen[i] = tmdp->blen;
			tmdp->u.s.status = 0x0;
		}

		for(i=0;i<RMDNUM;i++) {
			struct rmd *rmdp = p->rmdhead + i;
			rmdp->u.s.status = RCV_OWN;
		}
		p->tmdnum = p->xmit_queued = 0;
		writedatareg(CSR0_STRT | csr0);

		for(i=0;i<TMDNUM;i++) {
			int num = (i + p->tmdlast) & (TMDNUM-1);
			p->tmdhead[i].u.buffer = (u32) isa_virt_to_bus((char *)buffer[num]); /* status is part of buffer field */
			p->tmdhead[i].blen = blen[num];
			if(p->tmdhead[i].u.s.status & XMIT_OWN) {
				 p->tmdnum = (p->tmdnum + 1) & (TMDNUM-1);
				 p->xmit_queued = 1;
	 writedatareg(CSR0_TDMD | CSR0_INEA | csr0);
			}
#ifdef XMT_VIA_SKB
			p->tmd_skb[i] = skb_save[num];
#endif
		}
		p->rmdnum = p->tmdlast = 0;
		if(!p->lock)
			if (p->tmdnum || !p->xmit_queued)
				netif_wake_queue(dev);
		dev->trans_start = jiffies; /* prevent tx timeout */
	}
	else
		writedatareg(CSR0_STRT | csr0);
}

/*
 * init lance (write init-values .. init-buffers) (open-helper)
 */
static int ni65_lance_reinit(struct net_device *dev)
{
	 int i;
	 struct priv *p = dev->ml_priv;
	 unsigned long flags;

	 p->lock = 0;
	 p->xmit_queued = 0;

	 flags=claim_dma_lock();
	 disable_dma(dev->dma); /* I've never worked with dma, but we do it like the packetdriver */
	 set_dma_mode(dev->dma,DMA_MODE_CASCADE);
	 enable_dma(dev->dma);
	 release_dma_lock(flags);

	 outw(inw(PORT+L_RESET),PORT+L_RESET); /* first: reset the card */
	 if( (i=readreg(CSR0) ) != 0x4)
	 {
		 printk(KERN_ERR "%s: can't RESET %s card: %04x\n",dev->name,
							cards[p->cardno].cardname,(int) i);
		 flags=claim_dma_lock();
		 disable_dma(dev->dma);
		 release_dma_lock(flags);
		 return 0;
	 }

	 p->rmdnum = p->tmdnum = p->tmdlast = p->tmdbouncenum = 0;
	 for(i=0;i<TMDNUM;i++)
	 {
		 struct tmd *tmdp = p->tmdhead + i;
#ifdef XMT_VIA_SKB
		 if(p->tmd_skb[i]) {
			 dev_kfree_skb(p->tmd_skb[i]);
			 p->tmd_skb[i] = NULL;
		 }
#endif
		 tmdp->u.buffer = 0x0;
		 tmdp->u.s.status = XMIT_START | XMIT_END;
		 tmdp->blen = tmdp->status2 = 0;
	 }

	 for(i=0;i<RMDNUM;i++)
	 {
		 struct rmd *rmdp = p->rmdhead + i;
#ifdef RCV_VIA_SKB
		 rmdp->u.buffer = (u32) isa_virt_to_bus(p->recv_skb[i]->data);
#else
		 rmdp->u.buffer = (u32) isa_virt_to_bus(p->recvbounce[i]);
#endif
		 rmdp->blen = -(R_BUF_SIZE-8);
		 rmdp->mlen = 0;
		 rmdp->u.s.status = RCV_OWN;
	 }

	 if(dev->flags & IFF_PROMISC)
		 ni65_init_lance(p,dev->dev_addr,0x00,M_PROM);
	 else if (netdev_mc_count(dev) || dev->flags & IFF_ALLMULTI)
		 ni65_init_lance(p,dev->dev_addr,0xff,0x0);
	 else
		 ni65_init_lance(p,dev->dev_addr,0x00,0x00);

	/*
	 * ni65_set_lance_mem() sets L_ADDRREG to CSR0
	 * NOW, WE WILL NEVER CHANGE THE L_ADDRREG, CSR0 IS ALWAYS SELECTED
	 */

	 if(inw(PORT+L_DATAREG) & CSR0_IDON)	{
		 ni65_set_performance(p);
					 /* init OK: start lance , enable interrupts */
		 writedatareg(CSR0_CLRALL | CSR0_INEA | CSR0_STRT);
		 return 1; /* ->OK */
	 }
	 printk(KERN_ERR "%s: can't init lance, status: %04x\n",dev->name,(int) inw(PORT+L_DATAREG));
	 flags=claim_dma_lock();
	 disable_dma(dev->dma);
	 release_dma_lock(flags);
	 return 0; /* ->Error */
}

/*
 * interrupt handler
 */
static irqreturn_t ni65_interrupt(int irq, void * dev_id)
{
	int csr0 = 0;
	struct net_device *dev = dev_id;
	struct priv *p;
	int bcnt = 32;

	p = dev->ml_priv;

	spin_lock(&p->ring_lock);

	while(--bcnt) {
		csr0 = inw(PORT+L_DATAREG);

#if 0
		writedatareg( (csr0 & CSR0_CLRALL) ); /* ack interrupts, disable int. */
#else
		writedatareg( (csr0 & CSR0_CLRALL) | CSR0_INEA ); /* ack interrupts, interrupts enabled */
#endif

		if(!(csr0 & (CSR0_ERR | CSR0_RINT | CSR0_TINT)))
			break;

		if(csr0 & CSR0_RINT) /* RECV-int? */
			ni65_recv_intr(dev,csr0);
		if(csr0 & CSR0_TINT) /* XMIT-int? */
			ni65_xmit_intr(dev,csr0);

		if(csr0 & CSR0_ERR)
		{
			if(debuglevel > 1)
				printk(KERN_ERR "%s: general error: %04x.\n",dev->name,csr0);
			if(csr0 & CSR0_BABL)
				dev->stats.tx_errors++;
			if(csr0 & CSR0_MISS) {
				int i;
				for(i=0;i<RMDNUM;i++)
					printk("%02x ",p->rmdhead[i].u.s.status);
				printk("\n");
				dev->stats.rx_errors++;
			}
			if(csr0 & CSR0_MERR) {
				if(debuglevel > 1)
					printk(KERN_ERR "%s: Ooops .. memory error: %04x.\n",dev->name,csr0);
				ni65_stop_start(dev,p);
			}
		}
	}

#ifdef RCV_PARANOIA_CHECK
{
 int j;
 for(j=0;j<RMDNUM;j++)
 {
	int i, num2;
	for(i=RMDNUM-1;i>0;i--) {
		 num2 = (p->rmdnum + i) & (RMDNUM-1);
		 if(!(p->rmdhead[num2].u.s.status & RCV_OWN))
				break;
	}

	if(i) {
		int k, num1;
		for(k=0;k<RMDNUM;k++) {
			num1 = (p->rmdnum + k) & (RMDNUM-1);
			if(!(p->rmdhead[num1].u.s.status & RCV_OWN))
				break;
		}
		if(!k)
			break;

		if(debuglevel > 0)
		{
			char buf[256],*buf1;
			buf1 = buf;
			for(k=0;k<RMDNUM;k++) {
				sprintf(buf1,"%02x ",(p->rmdhead[k].u.s.status)); /* & RCV_OWN) ); */
				buf1 += 3;
			}
			*buf1 = 0;
			printk(KERN_ERR "%s: Ooops, receive ring corrupted %2d %2d | %s\n",dev->name,p->rmdnum,i,buf);
		}

		p->rmdnum = num1;
		ni65_recv_intr(dev,csr0);
		if((p->rmdhead[num2].u.s.status & RCV_OWN))
			break;	/* ok, we are 'in sync' again */
	}
	else
		break;
 }
}
#endif

	if( (csr0 & (CSR0_RXON | CSR0_TXON)) != (CSR0_RXON | CSR0_TXON) ) {
		printk(KERN_DEBUG "%s: RX or TX was offline -> restart\n",dev->name);
		ni65_stop_start(dev,p);
	}
	else
		writedatareg(CSR0_INEA);

	spin_unlock(&p->ring_lock);
	return IRQ_HANDLED;
}

/*
 * We have received an Xmit-Interrupt ..
 * send a new packet if necessary
 */
static void ni65_xmit_intr(struct net_device *dev,int csr0)
{
	struct priv *p = dev->ml_priv;

	while(p->xmit_queued)
	{
		struct tmd *tmdp = p->tmdhead + p->tmdlast;
		int tmdstat = tmdp->u.s.status;

		if(tmdstat & XMIT_OWN)
			break;

		if(tmdstat & XMIT_ERR)
		{
#if 0
			if(tmdp->status2 & XMIT_TDRMASK && debuglevel > 3)
				printk(KERN_ERR "%s: tdr-problems (e.g. no resistor)\n",dev->name);
#endif
		 /* checking some errors */
			if(tmdp->status2 & XMIT_RTRY)
				dev->stats.tx_aborted_errors++;
			if(tmdp->status2 & XMIT_LCAR)
				dev->stats.tx_carrier_errors++;
			if(tmdp->status2 & (XMIT_BUFF | XMIT_UFLO )) {
		/* this stops the xmitter */
				dev->stats.tx_fifo_errors++;
				if(debuglevel > 0)
					printk(KERN_ERR "%s: Xmit FIFO/BUFF error\n",dev->name);
				if(p->features & INIT_RING_BEFORE_START) {
					tmdp->u.s.status = XMIT_OWN | XMIT_START | XMIT_END;	/* test: resend this frame */
					ni65_stop_start(dev,p);
					break;	/* no more Xmit processing .. */
				}
				else
				 ni65_stop_start(dev,p);
			}
			if(debuglevel > 2)
				printk(KERN_ERR "%s: xmit-error: %04x %02x-%04x\n",dev->name,csr0,(int) tmdstat,(int) tmdp->status2);
			if(!(csr0 & CSR0_BABL)) /* don't count errors twice */
				dev->stats.tx_errors++;
			tmdp->status2 = 0;
		}
		else {
			dev->stats.tx_bytes -= (short)(tmdp->blen);
			dev->stats.tx_packets++;
		}

#ifdef XMT_VIA_SKB
		if(p->tmd_skb[p->tmdlast]) {
			 dev_kfree_skb_irq(p->tmd_skb[p->tmdlast]);
			 p->tmd_skb[p->tmdlast] = NULL;
		}
#endif

		p->tmdlast = (p->tmdlast + 1) & (TMDNUM-1);
		if(p->tmdlast == p->tmdnum)
			p->xmit_queued = 0;
	}
	netif_wake_queue(dev);
}

/*
 * We have received a packet
 */
static void ni65_recv_intr(struct net_device *dev,int csr0)
{
	struct rmd *rmdp;
	int rmdstat,len;
	int cnt=0;
	struct priv *p = dev->ml_priv;

	rmdp = p->rmdhead + p->rmdnum;
	while(!( (rmdstat = rmdp->u.s.status) & RCV_OWN))
	{
		cnt++;
		if( (rmdstat & (RCV_START | RCV_END | RCV_ERR)) != (RCV_START | RCV_END) ) /* error or oversized? */
		{
			if(!(rmdstat & RCV_ERR)) {
				if(rmdstat & RCV_START)
				{
					dev->stats.rx_length_errors++;
					printk(KERN_ERR "%s: recv, packet too long: %d\n",dev->name,rmdp->mlen & 0x0fff);
				}
			}
			else {
				if(debuglevel > 2)
					printk(KERN_ERR "%s: receive-error: %04x, lance-status: %04x/%04x\n",
									dev->name,(int) rmdstat,csr0,(int) inw(PORT+L_DATAREG) );
				if(rmdstat & RCV_FRAM)
					dev->stats.rx_frame_errors++;
				if(rmdstat & RCV_OFLO)
					dev->stats.rx_over_errors++;
				if(rmdstat & RCV_CRC)
					dev->stats.rx_crc_errors++;
				if(rmdstat & RCV_BUF_ERR)
					dev->stats.rx_fifo_errors++;
			}
			if(!(csr0 & CSR0_MISS)) /* don't count errors twice */
				dev->stats.rx_errors++;
		}
		else if( (len = (rmdp->mlen & 0x0fff) - 4) >= 60)
		{
#ifdef RCV_VIA_SKB
			struct sk_buff *skb = alloc_skb(R_BUF_SIZE+2+16,GFP_ATOMIC);
			if (skb)
				skb_reserve(skb,16);
#else
			struct sk_buff *skb = netdev_alloc_skb(dev, len + 2);
#endif
			if(skb)
			{
				skb_reserve(skb,2);
#ifdef RCV_VIA_SKB
				if( (unsigned long) (skb->data + R_BUF_SIZE) > 0x1000000) {
					skb_put(skb,len);
					skb_copy_to_linear_data(skb, (unsigned char *)(p->recv_skb[p->rmdnum]->data),len);
				}
				else {
					struct sk_buff *skb1 = p->recv_skb[p->rmdnum];
					skb_put(skb,R_BUF_SIZE);
					p->recv_skb[p->rmdnum] = skb;
					rmdp->u.buffer = (u32) isa_virt_to_bus(skb->data);
					skb = skb1;
					skb_trim(skb,len);
				}
#else
				skb_put(skb,len);
				skb_copy_to_linear_data(skb, (unsigned char *) p->recvbounce[p->rmdnum],len);
#endif
				dev->stats.rx_packets++;
				dev->stats.rx_bytes += len;
				skb->protocol=eth_type_trans(skb,dev);
				netif_rx(skb);
			}
			else
			{
				printk(KERN_ERR "%s: can't alloc new sk_buff\n",dev->name);
				dev->stats.rx_dropped++;
			}
		}
		else {
			printk(KERN_INFO "%s: received runt packet\n",dev->name);
			dev->stats.rx_errors++;
		}
		rmdp->blen = -(R_BUF_SIZE-8);
		rmdp->mlen = 0;
		rmdp->u.s.status = RCV_OWN; /* change owner */
		p->rmdnum = (p->rmdnum + 1) & (RMDNUM-1);
		rmdp = p->rmdhead + p->rmdnum;
	}
}

/*
 * kick xmitter ..
 */

static void ni65_timeout(struct net_device *dev)
{
	int i;
	struct priv *p = dev->ml_priv;

	printk(KERN_ERR "%s: xmitter timed out, try to restart!\n",dev->name);
	for(i=0;i<TMDNUM;i++)
		printk("%02x ",p->tmdhead[i].u.s.status);
	printk("\n");
	ni65_lance_reinit(dev);
	dev->trans_start = jiffies; /* prevent tx timeout */
	netif_wake_queue(dev);
}

/*
 *	Send a packet
 */

static netdev_tx_t ni65_send_packet(struct sk_buff *skb,
				    struct net_device *dev)
{
	struct priv *p = dev->ml_priv;

	netif_stop_queue(dev);

	if (test_and_set_bit(0, (void*)&p->lock)) {
		printk(KERN_ERR "%s: Queue was locked.\n", dev->name);
		return NETDEV_TX_BUSY;
	}

	{
		short len = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
		struct tmd *tmdp;
		unsigned long flags;

#ifdef XMT_VIA_SKB
		if( (unsigned long) (skb->data + skb->len) > 0x1000000) {
#endif

			skb_copy_from_linear_data(skb, p->tmdbounce[p->tmdbouncenum],
				      skb->len > T_BUF_SIZE ? T_BUF_SIZE :
							      skb->len);
			if (len > skb->len)
				memset((char *)p->tmdbounce[p->tmdbouncenum]+skb->len, 0, len-skb->len);
			dev_kfree_skb (skb);

			spin_lock_irqsave(&p->ring_lock, flags);
			tmdp = p->tmdhead + p->tmdnum;
			tmdp->u.buffer = (u32) isa_virt_to_bus(p->tmdbounce[p->tmdbouncenum]);
			p->tmdbouncenum = (p->tmdbouncenum + 1) & (TMDNUM - 1);

#ifdef XMT_VIA_SKB
		}
		else {
			spin_lock_irqsave(&p->ring_lock, flags);

			tmdp = p->tmdhead + p->tmdnum;
			tmdp->u.buffer = (u32) isa_virt_to_bus(skb->data);
			p->tmd_skb[p->tmdnum] = skb;
		}
#endif
		tmdp->blen = -len;

		tmdp->u.s.status = XMIT_OWN | XMIT_START | XMIT_END;
		writedatareg(CSR0_TDMD | CSR0_INEA); /* enable xmit & interrupt */

		p->xmit_queued = 1;
		p->tmdnum = (p->tmdnum + 1) & (TMDNUM-1);

		if(p->tmdnum != p->tmdlast)
			netif_wake_queue(dev);

		p->lock = 0;

		spin_unlock_irqrestore(&p->ring_lock, flags);
	}

	return NETDEV_TX_OK;
}

static void set_multicast_list(struct net_device *dev)
{
	if(!ni65_lance_reinit(dev))
		printk(KERN_ERR "%s: Can't switch card into MC mode!\n",dev->name);
	netif_wake_queue(dev);
}

#ifdef MODULE
static struct net_device *dev_ni65;

module_param(irq, int, 0);
module_param(io, int, 0);
module_param(dma, int, 0);
MODULE_PARM_DESC(irq, "ni6510 IRQ number (ignored for some cards)");
MODULE_PARM_DESC(io, "ni6510 I/O base address");
MODULE_PARM_DESC(dma, "ni6510 ISA DMA channel (ignored for some cards)");

int __init init_module(void)
{
 	dev_ni65 = ni65_probe(-1);
	return PTR_ERR_OR_ZERO(dev_ni65);
}

void __exit cleanup_module(void)
{
 	unregister_netdev(dev_ni65);
 	cleanup_card(dev_ni65);
 	free_netdev(dev_ni65);
}
#endif /* MODULE */

MODULE_LICENSE("GPL");
