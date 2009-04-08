/*
 * net-3-driver for the NI5210 card (i82586 Ethernet chip)
 *
 * This is an extension to the Linux operating system, and is covered by the
 * same GNU General Public License that covers that work.
 *
 * Alphacode 0.82 (96/09/29) for Linux 2.0.0 (or later)
 * Copyrights (c) 1994,1995,1996 by M.Hipp (hippm@informatik.uni-tuebingen.de)
 *    [feel free to mail ....]
 *
 * when using as module: (no autoprobing!)
 *   run with e.g:
 *       insmod ni52.o io=0x360 irq=9 memstart=0xd0000 memend=0xd4000
 *
 * CAN YOU PLEASE REPORT ME YOUR PERFORMANCE EXPERIENCES !!.
 *
 * If you find a bug, please report me:
 *   The kernel panic output and any kmsg from the ni52 driver
 *   the ni5210-driver-version and the linux-kernel version
 *   how many shared memory (memsize) on the netcard,
 *   bootprom: yes/no, base_addr, mem_start
 *   maybe the ni5210-card revision and the i82586 version
 *
 * autoprobe for: base_addr: 0x300,0x280,0x360,0x320,0x340
 *                mem_start: 0xd0000,0xd2000,0xc8000,0xca000,0xd4000,0xd6000,
 *                           0xd8000,0xcc000,0xce000,0xda000,0xdc000
 *
 * sources:
 *   skeleton.c from Donald Becker
 *
 * I have also done a look in the following sources: (mail me if you need them)
 *   crynwr-packet-driver by Russ Nelson
 *   Garret A. Wollman's (fourth) i82586-driver for BSD
 *   (before getting an i82596 (yes 596 not 586) manual, the existing drivers
 *    helped me a lot to understand this tricky chip.)
 *
 * Known Problems:
 *   The internal sysbus seems to be slow. So we often lose packets because of
 *   overruns while receiving from a fast remote host.
 *   This can slow down TCP connections. Maybe the newer ni5210 cards are
 *   better. My experience is, that if a machine sends with more than about
 *   500-600K/s the fifo/sysbus overflows.
 *
 * IMPORTANT NOTE:
 *   On fast networks, it's a (very) good idea to have 16K shared memory. With
 *   8K, we can store only 4 receive frames, so it can (easily) happen that a
 *   remote machine 'overruns' our system.
 *
 * Known i82586/card problems (I'm sure, there are many more!):
 *   Running the NOP-mode, the i82586 sometimes seems to forget to report
 *   every xmit-interrupt until we restart the CU.
 *   Another MAJOR bug is, that the RU sometimes seems to ignore the EL-Bit
 *   in the RBD-Struct which indicates an end of the RBD queue.
 *   Instead, the RU fetches another (randomly selected and
 *   usually used) RBD and begins to fill it. (Maybe, this happens only if
 *   the last buffer from the previous RFD fits exact into the queue and
 *   the next RFD can't fetch an initial RBD. Anyone knows more? )
 *
 * results from ftp performance tests with Linux 1.2.5
 *   send and receive about 350-400 KByte/s (peak up to 460 kbytes/s)
 *   sending in NOP-mode: peak performance up to 530K/s (but better don't
 *   run this mode)
 */

/*
 * 29.Sept.96: virt_to_bus changes for new memory scheme
 * 19.Feb.96: more Mcast changes, module support (MH)
 *
 * 18.Nov.95: Mcast changes (AC).
 *
 * 23.April.95: fixed(?) receiving problems by configuring a RFD more
 *              than the number of RBD's. Can maybe cause other problems.
 * 18.April.95: Added MODULE support (MH)
 * 17.April.95: MC related changes in init586() and set_multicast_list().
 *              removed use of 'jiffies' in init586() (MH)
 *
 * 19.Sep.94: Added Multicast support (not tested yet) (MH)
 *
 * 18.Sep.94: Workaround for 'EL-Bug'. Removed flexible RBD-handling.
 *            Now, every RFD has exact one RBD. (MH)
 *
 * 14.Sep.94: added promiscuous mode, a few cleanups (MH)
 *
 * 19.Aug.94: changed request_irq() parameter (MH)
 *
 * 20.July.94: removed cleanup bugs, removed a 16K-mem-probe-bug (MH)
 *
 * 19.July.94: lotsa cleanups .. (MH)
 *
 * 17.July.94: some patches ... verified to run with 1.1.29 (MH)
 *
 * 4.July.94: patches for Linux 1.1.24  (MH)
 *
 * 26.March.94: patches for Linux 1.0 and iomem-auto-probe (MH)
 *
 * 30.Sep.93: Added nop-chain .. driver now runs with only one Xmit-Buff,
 *				too (MH)
 *
 * < 30.Sep.93: first versions
 */

static int debuglevel;	/* debug-printk 0: off 1: a few 2: more */
static int automatic_resume; /* experimental .. better should be zero */
static int rfdadd;	/* rfdadd=1 may be better for 8K MEM cards */
static int fifo = 0x8;	/* don't change */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <asm/io.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include "ni52.h"

#define DRV_NAME "ni52"

#define DEBUG       /* debug on */
#define SYSBUSVAL 1 /* 8 Bit */

#define ni_attn586()  { outb(0, dev->base_addr + NI52_ATTENTION); }
#define ni_reset586() { outb(0, dev->base_addr + NI52_RESET); }
#define ni_disint()   { outb(0, dev->base_addr + NI52_INTDIS); }
#define ni_enaint()   { outb(0, dev->base_addr + NI52_INTENA); }

#define make32(ptr16) ((void __iomem *)(p->memtop + (short) (ptr16)))
#define make24(ptr32) ((char __iomem *)(ptr32)) - p->base
#define make16(ptr32) ((unsigned short) ((char __iomem *)(ptr32)\
					- p->memtop))

/******************* how to calculate the buffers *****************************

  * IMPORTANT NOTE: if you configure only one NUM_XMIT_BUFFS, the driver works
  * --------------- in a different (more stable?) mode. Only in this mode it's
  *                 possible to configure the driver with 'NO_NOPCOMMANDS'

sizeof(scp)=12; sizeof(scb)=16; sizeof(iscp)=8;
sizeof(scp)+sizeof(iscp)+sizeof(scb) = 36 = INIT
sizeof(rfd) = 24; sizeof(rbd) = 12;
sizeof(tbd) = 8; sizeof(transmit_cmd) = 16;
sizeof(nop_cmd) = 8;

  * if you don't know the driver, better do not change these values: */

#define RECV_BUFF_SIZE 1524 /* slightly oversized */
#define XMIT_BUFF_SIZE 1524 /* slightly oversized */
#define NUM_XMIT_BUFFS 1    /* config for both, 8K and 16K shmem */
#define NUM_RECV_BUFFS_8  4 /* config for 8K shared mem */
#define NUM_RECV_BUFFS_16 9 /* config for 16K shared mem */
#define NO_NOPCOMMANDS      /* only possible with NUM_XMIT_BUFFS=1 */

/**************************************************************************/


#define NI52_TOTAL_SIZE 16
#define NI52_ADDR0 0x02
#define NI52_ADDR1 0x07
#define NI52_ADDR2 0x01

static int     ni52_probe1(struct net_device *dev, int ioaddr);
static irqreturn_t ni52_interrupt(int irq, void *dev_id);
static int     ni52_open(struct net_device *dev);
static int     ni52_close(struct net_device *dev);
static int     ni52_send_packet(struct sk_buff *, struct net_device *);
static struct  net_device_stats *ni52_get_stats(struct net_device *dev);
static void    set_multicast_list(struct net_device *dev);
static void    ni52_timeout(struct net_device *dev);

/* helper-functions */
static int     init586(struct net_device *dev);
static int     check586(struct net_device *dev, unsigned size);
static void    alloc586(struct net_device *dev);
static void    startrecv586(struct net_device *dev);
static void   __iomem *alloc_rfa(struct net_device *dev, void __iomem *ptr);
static void    ni52_rcv_int(struct net_device *dev);
static void    ni52_xmt_int(struct net_device *dev);
static void    ni52_rnr_int(struct net_device *dev);

struct priv {
	struct net_device_stats stats;
	char __iomem *base;
	char __iomem *mapped;
	char __iomem *memtop;
	spinlock_t spinlock;
	int reset;
	struct rfd_struct __iomem *rfd_last, *rfd_top, *rfd_first;
	struct scp_struct __iomem *scp;
	struct iscp_struct __iomem *iscp;
	struct scb_struct __iomem *scb;
	struct tbd_struct __iomem *xmit_buffs[NUM_XMIT_BUFFS];
#if (NUM_XMIT_BUFFS == 1)
	struct transmit_cmd_struct __iomem *xmit_cmds[2];
	struct nop_cmd_struct __iomem *nop_cmds[2];
#else
	struct transmit_cmd_struct __iomem *xmit_cmds[NUM_XMIT_BUFFS];
	struct nop_cmd_struct __iomem *nop_cmds[NUM_XMIT_BUFFS];
#endif
	int nop_point, num_recv_buffs;
	char __iomem *xmit_cbuffs[NUM_XMIT_BUFFS];
	int xmit_count, xmit_last;
};

/* wait for command with timeout: */
static void wait_for_scb_cmd(struct net_device *dev)
{
	struct priv *p = netdev_priv(dev);
	int i;
	for (i = 0; i < 16384; i++) {
		if (readb(&p->scb->cmd_cuc) == 0)
		      break;
		udelay(4);
		if (i == 16383) {
			printk(KERN_ERR "%s: scb_cmd timed out: %04x,%04x .. disabling i82586!!\n",
				dev->name, readb(&p->scb->cmd_cuc), readb(&p->scb->cus));
			if (!p->reset) {
				p->reset = 1;
				ni_reset586();
			}
		}
	}
}

static void wait_for_scb_cmd_ruc(struct net_device *dev)
{
	struct priv *p = netdev_priv(dev);
	int i;
	for (i = 0; i < 16384; i++) {
		if (readb(&p->scb->cmd_ruc) == 0)
			break;
		udelay(4);
		if (i == 16383) {
			printk(KERN_ERR "%s: scb_cmd (ruc) timed out: %04x,%04x .. disabling i82586!!\n",
				dev->name, readb(&p->scb->cmd_ruc),
				readb(&p->scb->rus));
			if (!p->reset) {
				p->reset = 1;
				ni_reset586();
			}
		}
	}
}

static void wait_for_stat_compl(void __iomem *p)
{
	struct nop_cmd_struct __iomem *addr = p;
	int i;
	for (i = 0; i < 32767; i++) {
		if (readw(&((addr)->cmd_status)) & STAT_COMPL)
			break;
		udelay(32);
	}
}

/**********************************************
 * close device
 */
static int ni52_close(struct net_device *dev)
{
	free_irq(dev->irq, dev);
	ni_reset586(); /* the hard way to stop the receiver */
	netif_stop_queue(dev);
	return 0;
}

/**********************************************
 * open device
 */
static int ni52_open(struct net_device *dev)
{
	int ret;

	ni_disint();
	alloc586(dev);
	init586(dev);
	startrecv586(dev);
	ni_enaint();

	ret = request_irq(dev->irq, &ni52_interrupt, 0, dev->name, dev);
	if (ret) {
		ni_reset586();
		return ret;
	}
	netif_start_queue(dev);
	return 0; /* most done by init */
}

static int check_iscp(struct net_device *dev, void __iomem *addr)
{
	struct iscp_struct __iomem *iscp = addr;
	struct priv *p = netdev_priv(dev);
	memset_io(iscp, 0, sizeof(struct iscp_struct));

	writel(make24(iscp), &p->scp->iscp);
	writeb(1, &iscp->busy);

	ni_reset586();
	ni_attn586();
	mdelay(32);	/* wait a while... */
	/* i82586 clears 'busy' after successful init */
	if (readb(&iscp->busy))
		return 0;
	return 1;
}

/**********************************************
 * Check to see if there's an 82586 out there.
 */
static int check586(struct net_device *dev, unsigned size)
{
	struct priv *p = netdev_priv(dev);
	int i;

	p->mapped = ioremap(dev->mem_start, size);
	if (!p->mapped)
		return 0;

	p->base = p->mapped + size - 0x01000000;
	p->memtop = p->mapped + size;
	p->scp = (struct scp_struct __iomem *)(p->base + SCP_DEFAULT_ADDRESS);
	p->scb	= (struct scb_struct __iomem *)	p->mapped;
	p->iscp = (struct iscp_struct __iomem *)p->scp - 1;
	memset_io(p->scp, 0, sizeof(struct scp_struct));
	for (i = 0; i < sizeof(struct scp_struct); i++)
		/* memory was writeable? */
		if (readb((char __iomem *)p->scp + i))
			goto Enodev;
	writeb(SYSBUSVAL, &p->scp->sysbus);	/* 1 = 8Bit-Bus, 0 = 16 Bit */
	if (readb(&p->scp->sysbus) != SYSBUSVAL)
		goto Enodev;

	if (!check_iscp(dev, p->mapped))
		goto Enodev;
	if (!check_iscp(dev, p->iscp))
		goto Enodev;
	return 1;
Enodev:
	iounmap(p->mapped);
	return 0;
}

/******************************************************************
 * set iscp at the right place, called by ni52_probe1 and open586.
 */
static void alloc586(struct net_device *dev)
{
	struct priv *p = netdev_priv(dev);

	ni_reset586();
	mdelay(32);

	memset_io(p->iscp, 0, sizeof(struct iscp_struct));
	memset_io(p->scp , 0, sizeof(struct scp_struct));

	writel(make24(p->iscp), &p->scp->iscp);
	writeb(SYSBUSVAL, &p->scp->sysbus);
	writew(make16(p->scb), &p->iscp->scb_offset);

	writeb(1, &p->iscp->busy);
	ni_reset586();
	ni_attn586();

	mdelay(32);

	if (readb(&p->iscp->busy))
		printk(KERN_ERR "%s: Init-Problems (alloc).\n", dev->name);

	p->reset = 0;

	memset_io(p->scb, 0, sizeof(struct scb_struct));
}

/* set: io,irq,memstart,memend or set it when calling insmod */
static int irq = 9;
static int io = 0x300;
static long memstart;	/* e.g 0xd0000 */
static long memend;	/* e.g 0xd4000 */

/**********************************************
 * probe the ni5210-card
 */
struct net_device * __init ni52_probe(int unit)
{
	struct net_device *dev = alloc_etherdev(sizeof(struct priv));
	static int ports[] = {0x300, 0x280, 0x360 , 0x320 , 0x340, 0};
	struct priv *p;
	int *port;
	int err = 0;

	if (!dev)
		return ERR_PTR(-ENOMEM);

	p = netdev_priv(dev);

	if (unit >= 0) {
		sprintf(dev->name, "eth%d", unit);
		netdev_boot_setup_check(dev);
		io = dev->base_addr;
		irq = dev->irq;
		memstart = dev->mem_start;
		memend = dev->mem_end;
	}

	if (io > 0x1ff)	{	/* Check a single specified location. */
		err = ni52_probe1(dev, io);
	} else if (io > 0) {		/* Don't probe at all. */
		err = -ENXIO;
	} else {
		for (port = ports; *port && ni52_probe1(dev, *port) ; port++)
			;
		if (*port)
			goto got_it;
#ifdef FULL_IO_PROBE
		for (io = 0x200; io < 0x400 && ni52_probe1(dev, io); io += 8)
			;
		if (io < 0x400)
			goto got_it;
#endif
		err = -ENODEV;
	}
	if (err)
		goto out;
got_it:
	err = register_netdev(dev);
	if (err)
		goto out1;
	return dev;
out1:
	iounmap(p->mapped);
	release_region(dev->base_addr, NI52_TOTAL_SIZE);
out:
	free_netdev(dev);
	return ERR_PTR(err);
}

static const struct net_device_ops ni52_netdev_ops = {
	.ndo_open		= ni52_open,
	.ndo_stop		= ni52_close,
	.ndo_get_stats		= ni52_get_stats,
	.ndo_tx_timeout 	= ni52_timeout,
	.ndo_start_xmit 	= ni52_send_packet,
	.ndo_set_multicast_list = set_multicast_list,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static int __init ni52_probe1(struct net_device *dev, int ioaddr)
{
	int i, size, retval;
	struct priv *priv = netdev_priv(dev);

	dev->base_addr = ioaddr;
	dev->irq = irq;
	dev->mem_start = memstart;
	dev->mem_end = memend;

	spin_lock_init(&priv->spinlock);

	if (!request_region(ioaddr, NI52_TOTAL_SIZE, DRV_NAME))
		return -EBUSY;

	if (!(inb(ioaddr+NI52_MAGIC1) == NI52_MAGICVAL1) ||
	    !(inb(ioaddr+NI52_MAGIC2) == NI52_MAGICVAL2)) {
		retval = -ENODEV;
		goto out;
	}

	for (i = 0; i < ETH_ALEN; i++)
		dev->dev_addr[i] = inb(dev->base_addr+i);

	if (dev->dev_addr[0] != NI52_ADDR0 || dev->dev_addr[1] != NI52_ADDR1
		 || dev->dev_addr[2] != NI52_ADDR2) {
		retval = -ENODEV;
		goto out;
	}

	printk(KERN_INFO "%s: NI5210 found at %#3lx, ",
				dev->name, dev->base_addr);

	/*
	 * check (or search) IO-Memory, 8K and 16K
	 */
#ifdef MODULE
	size = dev->mem_end - dev->mem_start;
	if (size != 0x2000 && size != 0x4000) {
		printk("\n");
		printk(KERN_ERR "%s: Invalid memory size %d. Allowed is 0x2000 or 0x4000 bytes.\n", dev->name, size);
		retval = -ENODEV;
		goto out;
	}
	if (!check586(dev, size)) {
		printk(KERN_ERR "?memcheck, Can't find memory at 0x%lx with size %d!\n", dev->mem_start, size);
		retval = -ENODEV;
		goto out;
	}
#else
	if (dev->mem_start != 0) {
		/* no auto-mem-probe */
		size = 0x4000; /* check for 16K mem */
		if (!check586(dev, size)) {
			size = 0x2000; /* check for 8K mem */
			if (!check586(dev, size)) {
				printk(KERN_ERR "?memprobe, Can't find memory at 0x%lx!\n", dev->mem_start);
				retval = -ENODEV;
				goto out;
			}
		}
	} else {
		static const unsigned long memaddrs[] = {
			0xc8000, 0xca000, 0xcc000, 0xce000, 0xd0000, 0xd2000,
			0xd4000, 0xd6000, 0xd8000, 0xda000, 0xdc000, 0
		};
		for (i = 0;; i++) {
			if (!memaddrs[i]) {
				printk(KERN_ERR "?memprobe, Can't find io-memory!\n");
				retval = -ENODEV;
				goto out;
			}
			dev->mem_start = memaddrs[i];
			size = 0x2000; /* check for 8K mem */
			if (check586(dev, size))
				/* 8K-check */
				break;
			size = 0x4000; /* check for 16K mem */
			if (check586(dev, size))
				/* 16K-check */
				break;
		}
	}
	/* set mem_end showed by 'ifconfig' */
	dev->mem_end = dev->mem_start + size;
#endif

	alloc586(dev);

	/* set number of receive-buffs according to memsize */
	if (size == 0x2000)
		priv->num_recv_buffs = NUM_RECV_BUFFS_8;
	else
		priv->num_recv_buffs = NUM_RECV_BUFFS_16;

	printk(KERN_DEBUG "Memaddr: 0x%lx, Memsize: %d, ",
				dev->mem_start, size);

	if (dev->irq < 2) {
		unsigned long irq_mask;

		irq_mask = probe_irq_on();
		ni_reset586();
		ni_attn586();

		mdelay(20);
		dev->irq = probe_irq_off(irq_mask);
		if (!dev->irq) {
			printk("?autoirq, Failed to detect IRQ line!\n");
			retval = -EAGAIN;
			iounmap(priv->mapped);
			goto out;
		}
		printk("IRQ %d (autodetected).\n", dev->irq);
	} else {
		if (dev->irq == 2)
			dev->irq = 9;
		printk("IRQ %d (assigned and not checked!).\n", dev->irq);
	}

	dev->netdev_ops		= &ni52_netdev_ops;
	dev->watchdog_timeo	= HZ/20;

	return 0;
out:
	release_region(ioaddr, NI52_TOTAL_SIZE);
	return retval;
}

/**********************************************
 * init the chip (ni52-interrupt should be disabled?!)
 * needs a correct 'allocated' memory
 */

static int init586(struct net_device *dev)
{
	void __iomem *ptr;
	int i, result = 0;
	struct priv *p = netdev_priv(dev);
	struct configure_cmd_struct __iomem *cfg_cmd;
	struct iasetup_cmd_struct __iomem *ias_cmd;
	struct tdr_cmd_struct __iomem *tdr_cmd;
	struct mcsetup_cmd_struct __iomem *mc_cmd;
	struct dev_mc_list *dmi = dev->mc_list;
	int num_addrs = dev->mc_count;

	ptr = p->scb + 1;

	cfg_cmd = ptr; /* configure-command */
	writew(0, &cfg_cmd->cmd_status);
	writew(CMD_CONFIGURE | CMD_LAST, &cfg_cmd->cmd_cmd);
	writew(0xFFFF, &cfg_cmd->cmd_link);

	/* number of cfg bytes */
	writeb(0x0a, &cfg_cmd->byte_cnt);
	/* fifo-limit (8=tx:32/rx:64) */
	writeb(fifo, &cfg_cmd->fifo);
	/* hold or discard bad recv frames (bit 7) */
	writeb(0x40, &cfg_cmd->sav_bf);
	/* addr_len |!src_insert |pre-len |loopback */
	writeb(0x2e, &cfg_cmd->adr_len);
	writeb(0x00, &cfg_cmd->priority);
	writeb(0x60, &cfg_cmd->ifs);;
	writeb(0x00, &cfg_cmd->time_low);
	writeb(0xf2, &cfg_cmd->time_high);
	writeb(0x00, &cfg_cmd->promisc);;
	if (dev->flags & IFF_ALLMULTI) {
		int len = ((char __iomem *)p->iscp - (char __iomem *)ptr - 8) / 6;
		if (num_addrs > len) {
			printk(KERN_ERR "%s: switching to promisc. mode\n",
				dev->name);
			writeb(0x01, &cfg_cmd->promisc);
		}
	}
	if (dev->flags & IFF_PROMISC)
		writeb(0x01, &cfg_cmd->promisc);
	writeb(0x00, &cfg_cmd->carr_coll);
	writew(make16(cfg_cmd), &p->scb->cbl_offset);
	writeb(0, &p->scb->cmd_ruc);

	writeb(CUC_START, &p->scb->cmd_cuc); /* cmd.-unit start */
	ni_attn586();

	wait_for_stat_compl(cfg_cmd);

	if ((readw(&cfg_cmd->cmd_status) & (STAT_OK|STAT_COMPL)) !=
							(STAT_COMPL|STAT_OK)) {
		printk(KERN_ERR "%s: configure command failed: %x\n",
				dev->name, readw(&cfg_cmd->cmd_status));
		return 1;
	}

	/*
	 * individual address setup
	 */

	ias_cmd = ptr;

	writew(0, &ias_cmd->cmd_status);
	writew(CMD_IASETUP | CMD_LAST, &ias_cmd->cmd_cmd);
	writew(0xffff, &ias_cmd->cmd_link);

	memcpy_toio(&ias_cmd->iaddr, (char *)dev->dev_addr, ETH_ALEN);

	writew(make16(ias_cmd), &p->scb->cbl_offset);

	writeb(CUC_START, &p->scb->cmd_cuc); /* cmd.-unit start */
	ni_attn586();

	wait_for_stat_compl(ias_cmd);

	if ((readw(&ias_cmd->cmd_status) & (STAT_OK|STAT_COMPL)) !=
							(STAT_OK|STAT_COMPL)) {
		printk(KERN_ERR "%s (ni52): individual address setup command failed: %04x\n", dev->name, readw(&ias_cmd->cmd_status));
		return 1;
	}

	/*
	 * TDR, wire check .. e.g. no resistor e.t.c
	 */

	tdr_cmd = ptr;

	writew(0, &tdr_cmd->cmd_status);
	writew(CMD_TDR | CMD_LAST, &tdr_cmd->cmd_cmd);
	writew(0xffff, &tdr_cmd->cmd_link);
	writew(0, &tdr_cmd->status);

	writew(make16(tdr_cmd), &p->scb->cbl_offset);
	writeb(CUC_START, &p->scb->cmd_cuc); /* cmd.-unit start */
	ni_attn586();

	wait_for_stat_compl(tdr_cmd);

	if (!(readw(&tdr_cmd->cmd_status) & STAT_COMPL))
		printk(KERN_ERR "%s: Problems while running the TDR.\n",
				dev->name);
	else {
		udelay(16);
		result = readw(&tdr_cmd->status);
		writeb(readb(&p->scb->cus) & STAT_MASK, &p->scb->cmd_cuc);
		ni_attn586(); /* ack the interrupts */

		if (result & TDR_LNK_OK)
			;
		else if (result & TDR_XCVR_PRB)
			printk(KERN_ERR "%s: TDR: Transceiver problem. Check the cable(s)!\n",
				dev->name);
		else if (result & TDR_ET_OPN)
			printk(KERN_ERR "%s: TDR: No correct termination %d clocks away.\n",
				dev->name, result & TDR_TIMEMASK);
		else if (result & TDR_ET_SRT) {
			/* time == 0 -> strange :-) */
			if (result & TDR_TIMEMASK)
				printk(KERN_ERR "%s: TDR: Detected a short circuit %d clocks away.\n",
					dev->name, result & TDR_TIMEMASK);
		} else
			printk(KERN_ERR "%s: TDR: Unknown status %04x\n",
						dev->name, result);
	}

	/*
	 * Multicast setup
	 */
	if (num_addrs && !(dev->flags & IFF_PROMISC)) {
		mc_cmd = ptr;
		writew(0, &mc_cmd->cmd_status);
		writew(CMD_MCSETUP | CMD_LAST, &mc_cmd->cmd_cmd);
		writew(0xffff, &mc_cmd->cmd_link);
		writew(num_addrs * 6, &mc_cmd->mc_cnt);

		for (i = 0; i < num_addrs; i++, dmi = dmi->next)
			memcpy_toio(mc_cmd->mc_list[i],
							dmi->dmi_addr, 6);

		writew(make16(mc_cmd), &p->scb->cbl_offset);
		writeb(CUC_START, &p->scb->cmd_cuc);
		ni_attn586();

		wait_for_stat_compl(mc_cmd);

		if ((readw(&mc_cmd->cmd_status) & (STAT_COMPL|STAT_OK))
						 != (STAT_COMPL|STAT_OK))
			printk(KERN_ERR "%s: Can't apply multicast-address-list.\n", dev->name);
	}

	/*
	 * alloc nop/xmit-cmds
	 */
#if (NUM_XMIT_BUFFS == 1)
	for (i = 0; i < 2; i++) {
		p->nop_cmds[i] = ptr;
		writew(CMD_NOP, &p->nop_cmds[i]->cmd_cmd);
		writew(0, &p->nop_cmds[i]->cmd_status);
		writew(make16(p->nop_cmds[i]), &p->nop_cmds[i]->cmd_link);
		ptr = ptr + sizeof(struct nop_cmd_struct);
	}
#else
	for (i = 0; i < NUM_XMIT_BUFFS; i++) {
		p->nop_cmds[i] = ptr;
		writew(CMD_NOP, &p->nop_cmds[i]->cmd_cmd);
		writew(0, &p->nop_cmds[i]->cmd_status);
		writew(make16(p->nop_cmds[i]), &p->nop_cmds[i]->cmd_link);
		ptr = ptr + sizeof(struct nop_cmd_struct);
	}
#endif

	ptr = alloc_rfa(dev, ptr); /* init receive-frame-area */

	/*
	 * alloc xmit-buffs / init xmit_cmds
	 */
	for (i = 0; i < NUM_XMIT_BUFFS; i++) {
		/* Transmit cmd/buff 0 */
		p->xmit_cmds[i] = ptr;
		ptr = ptr + sizeof(struct transmit_cmd_struct);
		p->xmit_cbuffs[i] = ptr; /* char-buffs */
		ptr = ptr + XMIT_BUFF_SIZE;
		p->xmit_buffs[i] = ptr; /* TBD */
		ptr = ptr + sizeof(struct tbd_struct);
		if ((void __iomem *)ptr > (void __iomem *)p->iscp) {
			printk(KERN_ERR "%s: not enough shared-mem for your configuration!\n",
				dev->name);
			return 1;
		}
		memset_io(p->xmit_cmds[i], 0,
					sizeof(struct transmit_cmd_struct));
		memset_io(p->xmit_buffs[i], 0,
					sizeof(struct tbd_struct));
		writew(make16(p->nop_cmds[(i+1)%NUM_XMIT_BUFFS]),
					&p->xmit_cmds[i]->cmd_link);
		writew(STAT_COMPL, &p->xmit_cmds[i]->cmd_status);
		writew(CMD_XMIT|CMD_INT, &p->xmit_cmds[i]->cmd_cmd);
		writew(make16(p->xmit_buffs[i]), &p->xmit_cmds[i]->tbd_offset);
		writew(0xffff, &p->xmit_buffs[i]->next);
		writel(make24(p->xmit_cbuffs[i]), &p->xmit_buffs[i]->buffer);
	}

	p->xmit_count = 0;
	p->xmit_last	= 0;
#ifndef NO_NOPCOMMANDS
	p->nop_point	= 0;
#endif

	 /*
		* 'start transmitter'
		*/
#ifndef NO_NOPCOMMANDS
	writew(make16(p->nop_cmds[0]), &p->scb->cbl_offset);
	writeb(CUC_START, &p->scb->cmd_cuc);
	ni_attn586();
	wait_for_scb_cmd(dev);
#else
	writew(make16(p->xmit_cmds[0]), &p->xmit_cmds[0]->cmd_link);
	writew(CMD_XMIT | CMD_SUSPEND | CMD_INT, &p->xmit_cmds[0]->cmd_cmd);
#endif

	/*
	 * ack. interrupts
	 */
	writeb(readb(&p->scb->cus) & STAT_MASK, &p->scb->cmd_cuc);
	ni_attn586();
	udelay(16);

	ni_enaint();

	return 0;
}

/******************************************************
 * This is a helper routine for ni52_rnr_int() and init586().
 * It sets up the Receive Frame Area (RFA).
 */

static void __iomem *alloc_rfa(struct net_device *dev, void __iomem *ptr)
{
	struct rfd_struct __iomem *rfd = ptr;
	struct rbd_struct __iomem *rbd;
	int i;
	struct priv *p = netdev_priv(dev);

	memset_io(rfd, 0,
		sizeof(struct rfd_struct) * (p->num_recv_buffs + rfdadd));
	p->rfd_first = rfd;

	for (i = 0; i < (p->num_recv_buffs + rfdadd); i++) {
		writew(make16(rfd + (i+1) % (p->num_recv_buffs+rfdadd)),
			&rfd[i].next);
		writew(0xffff, &rfd[i].rbd_offset);
	}
	/* RU suspend */
	writeb(RFD_SUSP, &rfd[p->num_recv_buffs-1+rfdadd].last);

	ptr = rfd + (p->num_recv_buffs + rfdadd);

	rbd = ptr;
	ptr = rbd + p->num_recv_buffs;

	 /* clr descriptors */
	memset_io(rbd, 0, sizeof(struct rbd_struct) * (p->num_recv_buffs));

	for (i = 0; i < p->num_recv_buffs; i++) {
		writew(make16(rbd + (i+1) % p->num_recv_buffs), &rbd[i].next);
		writew(RECV_BUFF_SIZE, &rbd[i].size);
		writel(make24(ptr), &rbd[i].buffer);
		ptr = ptr + RECV_BUFF_SIZE;
	}
	p->rfd_top	= p->rfd_first;
	p->rfd_last = p->rfd_first + (p->num_recv_buffs - 1 + rfdadd);

	writew(make16(p->rfd_first), &p->scb->rfa_offset);
	writew(make16(rbd), &p->rfd_first->rbd_offset);

	return ptr;
}


/**************************************************
 * Interrupt Handler ...
 */

static irqreturn_t ni52_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	unsigned int stat;
	int cnt = 0;
	struct priv *p;

	p = netdev_priv(dev);

	if (debuglevel > 1)
		printk("I");

	spin_lock(&p->spinlock);

	wait_for_scb_cmd(dev); /* wait for last command	*/

	while ((stat = readb(&p->scb->cus) & STAT_MASK)) {
		writeb(stat, &p->scb->cmd_cuc);
		ni_attn586();

		if (stat & STAT_FR)	 /* received a frame */
			ni52_rcv_int(dev);

		if (stat & STAT_RNR) { /* RU went 'not ready' */
			printk("(R)");
			if (readb(&p->scb->rus) & RU_SUSPEND) {
				/* special case: RU_SUSPEND */
				wait_for_scb_cmd(dev);
				writeb(RUC_RESUME, &p->scb->cmd_ruc);
				ni_attn586();
				wait_for_scb_cmd_ruc(dev);
			} else {
				printk(KERN_ERR "%s: Receiver-Unit went 'NOT READY': %04x/%02x.\n",
					dev->name, stat, readb(&p->scb->rus));
				ni52_rnr_int(dev);
			}
		}

		/* Command with I-bit set complete */
		if (stat & STAT_CX)
			 ni52_xmt_int(dev);

#ifndef NO_NOPCOMMANDS
		if (stat & STAT_CNA) {	/* CU went 'not ready' */
			if (netif_running(dev))
				printk(KERN_ERR "%s: oops! CU has left active state. stat: %04x/%02x.\n",
					dev->name, stat, readb(&p->scb->cus));
		}
#endif

		if (debuglevel > 1)
			printk("%d", cnt++);

		/* Wait for ack. (ni52_xmt_int can be faster than ack!!) */
		wait_for_scb_cmd(dev);
		if (readb(&p->scb->cmd_cuc)) {	 /* timed out? */
			printk(KERN_ERR "%s: Acknowledge timed out.\n",
				dev->name);
			ni_disint();
			break;
		}
	}
	spin_unlock(&p->spinlock);

	if (debuglevel > 1)
		printk("i");
	return IRQ_HANDLED;
}

/*******************************************************
 * receive-interrupt
 */

static void ni52_rcv_int(struct net_device *dev)
{
	int status, cnt = 0;
	unsigned short totlen;
	struct sk_buff *skb;
	struct rbd_struct __iomem *rbd;
	struct priv *p = netdev_priv(dev);

	if (debuglevel > 0)
		printk("R");

	for (; (status = readb(&p->rfd_top->stat_high)) & RFD_COMPL;) {
		rbd = make32(readw(&p->rfd_top->rbd_offset));
		if (status & RFD_OK) { /* frame received without error? */
			totlen = readw(&rbd->status);
			if (totlen & RBD_LAST) {
				/* the first and the last buffer? */
				totlen &= RBD_MASK; /* length of this frame */
				writew(0x00, &rbd->status);
				skb = (struct sk_buff *)dev_alloc_skb(totlen+2);
				if (skb != NULL) {
					skb_reserve(skb, 2);
					skb_put(skb, totlen);
					memcpy_fromio(skb->data, p->base + readl(&rbd->buffer), totlen);
					skb->protocol = eth_type_trans(skb, dev);
					netif_rx(skb);
					p->stats.rx_packets++;
					p->stats.rx_bytes += totlen;
				} else
					p->stats.rx_dropped++;
			} else {
				int rstat;
				 /* free all RBD's until RBD_LAST is set */
				totlen = 0;
				while (!((rstat = readw(&rbd->status)) & RBD_LAST)) {
					totlen += rstat & RBD_MASK;
					if (!rstat) {
						printk(KERN_ERR "%s: Whoops .. no end mark in RBD list\n", dev->name);
						break;
					}
					writew(0, &rbd->status);
					rbd = make32(readw(&rbd->next));
				}
				totlen += rstat & RBD_MASK;
				writew(0, &rbd->status);
				printk(KERN_ERR "%s: received oversized frame! length: %d\n",
					dev->name, totlen);
				p->stats.rx_dropped++;
			 }
		} else {/* frame !(ok), only with 'save-bad-frames' */
			printk(KERN_ERR "%s: oops! rfd-error-status: %04x\n",
				dev->name, status);
			p->stats.rx_errors++;
		}
		writeb(0, &p->rfd_top->stat_high);
		writeb(RFD_SUSP, &p->rfd_top->last); /* maybe exchange by RFD_LAST */
		writew(0xffff, &p->rfd_top->rbd_offset);
		writeb(0, &p->rfd_last->last);	/* delete RFD_SUSP	*/
		p->rfd_last = p->rfd_top;
		p->rfd_top = make32(readw(&p->rfd_top->next)); /* step to next RFD */
		writew(make16(p->rfd_top), &p->scb->rfa_offset);

		if (debuglevel > 0)
			printk("%d", cnt++);
	}

	if (automatic_resume) {
		wait_for_scb_cmd(dev);
		writeb(RUC_RESUME, &p->scb->cmd_ruc);
		ni_attn586();
		wait_for_scb_cmd_ruc(dev);
	}

#ifdef WAIT_4_BUSY
	{
		int i;
		for (i = 0; i < 1024; i++) {
			if (p->rfd_top->status)
				break;
			udelay(16);
			if (i == 1023)
				printk(KERN_ERR "%s: RU hasn't fetched next RFD (not busy/complete)\n", dev->name);
		}
	}
#endif
	if (debuglevel > 0)
		printk("r");
}

/**********************************************************
 * handle 'Receiver went not ready'.
 */

static void ni52_rnr_int(struct net_device *dev)
{
	struct priv *p = netdev_priv(dev);

	p->stats.rx_errors++;

	wait_for_scb_cmd(dev);		/* wait for the last cmd, WAIT_4_FULLSTAT?? */
	writeb(RUC_ABORT, &p->scb->cmd_ruc); /* usually the RU is in the 'no resource'-state .. abort it now. */
	ni_attn586();
	wait_for_scb_cmd_ruc(dev);		/* wait for accept cmd. */

	alloc_rfa(dev, p->rfd_first);
	/* maybe add a check here, before restarting the RU */
	startrecv586(dev); /* restart RU */

	printk(KERN_ERR "%s: Receive-Unit restarted. Status: %04x\n",
		dev->name, readb(&p->scb->rus));

}

/**********************************************************
 * handle xmit - interrupt
 */

static void ni52_xmt_int(struct net_device *dev)
{
	int status;
	struct priv *p = netdev_priv(dev);

	if (debuglevel > 0)
		printk("X");

	status = readw(&p->xmit_cmds[p->xmit_last]->cmd_status);
	if (!(status & STAT_COMPL))
		printk(KERN_ERR "%s: strange .. xmit-int without a 'COMPLETE'\n", dev->name);

	if (status & STAT_OK) {
		p->stats.tx_packets++;
		p->stats.collisions += (status & TCMD_MAXCOLLMASK);
	} else {
		p->stats.tx_errors++;
		if (status & TCMD_LATECOLL) {
			printk(KERN_ERR "%s: late collision detected.\n",
				dev->name);
			p->stats.collisions++;
		} else if (status & TCMD_NOCARRIER) {
			p->stats.tx_carrier_errors++;
			printk(KERN_ERR "%s: no carrier detected.\n",
				dev->name);
		} else if (status & TCMD_LOSTCTS)
			printk(KERN_ERR "%s: loss of CTS detected.\n",
				dev->name);
		else if (status & TCMD_UNDERRUN) {
			p->stats.tx_fifo_errors++;
			printk(KERN_ERR "%s: DMA underrun detected.\n",
				dev->name);
		} else if (status & TCMD_MAXCOLL) {
			printk(KERN_ERR "%s: Max. collisions exceeded.\n",
				dev->name);
			p->stats.collisions += 16;
		}
	}
#if (NUM_XMIT_BUFFS > 1)
	if ((++p->xmit_last) == NUM_XMIT_BUFFS)
		p->xmit_last = 0;
#endif
	netif_wake_queue(dev);
}

/***********************************************************
 * (re)start the receiver
 */

static void startrecv586(struct net_device *dev)
{
	struct priv *p = netdev_priv(dev);

	wait_for_scb_cmd(dev);
	wait_for_scb_cmd_ruc(dev);
	writew(make16(p->rfd_first), &p->scb->rfa_offset);
	writeb(RUC_START, &p->scb->cmd_ruc);
	ni_attn586();		/* start cmd. */
	wait_for_scb_cmd_ruc(dev);
	/* wait for accept cmd. (no timeout!!) */
}

static void ni52_timeout(struct net_device *dev)
{
	struct priv *p = netdev_priv(dev);
#ifndef NO_NOPCOMMANDS
	if (readb(&p->scb->cus) & CU_ACTIVE) { /* COMMAND-UNIT active? */
		netif_wake_queue(dev);
#ifdef DEBUG
		printk(KERN_ERR "%s: strange ... timeout with CU active?!?\n",
			dev->name);
		printk(KERN_ERR "%s: X0: %04x N0: %04x N1: %04x %d\n",
			dev->name, (int)p->xmit_cmds[0]->cmd_status,
			readw(&p->nop_cmds[0]->cmd_status),
			readw(&p->nop_cmds[1]->cmd_status),
			p->nop_point);
#endif
		writeb(CUC_ABORT, &p->scb->cmd_cuc);
		ni_attn586();
		wait_for_scb_cmd(dev);
		writew(make16(p->nop_cmds[p->nop_point]), &p->scb->cbl_offset);
		writeb(CUC_START, &p->scb->cmd_cuc);
		ni_attn586();
		wait_for_scb_cmd(dev);
		dev->trans_start = jiffies;
		return 0;
	}
#endif
	{
#ifdef DEBUG
		printk(KERN_ERR "%s: xmitter timed out, try to restart! stat: %02x\n",
				dev->name, readb(&p->scb->cus));
		printk(KERN_ERR "%s: command-stats: %04x %04x\n",
				dev->name,
				readw(&p->xmit_cmds[0]->cmd_status),
				readw(&p->xmit_cmds[1]->cmd_status));
		printk(KERN_ERR "%s: check, whether you set the right interrupt number!\n",
				dev->name);
#endif
		ni52_close(dev);
		ni52_open(dev);
	}
	dev->trans_start = jiffies;
}

/******************************************************
 * send frame
 */

static int ni52_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	int len, i;
#ifndef NO_NOPCOMMANDS
	int next_nop;
#endif
	struct priv *p = netdev_priv(dev);

	if (skb->len > XMIT_BUFF_SIZE) {
		printk(KERN_ERR "%s: Sorry, max. framelength is %d bytes. The length of your frame is %d bytes.\n", dev->name, XMIT_BUFF_SIZE, skb->len);
		return 0;
	}

	netif_stop_queue(dev);

	memcpy_toio(p->xmit_cbuffs[p->xmit_count], skb->data, skb->len);
	len = skb->len;
	if (len < ETH_ZLEN) {
		len = ETH_ZLEN;
		memset_io(p->xmit_cbuffs[p->xmit_count]+skb->len, 0,
							len - skb->len);
	}

#if (NUM_XMIT_BUFFS == 1)
#	ifdef NO_NOPCOMMANDS

#ifdef DEBUG
	if (readb(&p->scb->cus) & CU_ACTIVE) {
		printk(KERN_ERR "%s: Hmmm .. CU is still running and we wanna send a new packet.\n", dev->name);
		printk(KERN_ERR "%s: stat: %04x %04x\n",
				dev->name, readb(&p->scb->cus),
				readw(&p->xmit_cmds[0]->cmd_status));
	}
#endif
	writew(TBD_LAST | len, &p->xmit_buffs[0]->size);
	for (i = 0; i < 16; i++) {
		writew(0, &p->xmit_cmds[0]->cmd_status);
		wait_for_scb_cmd(dev);
		if ((readb(&p->scb->cus) & CU_STATUS) == CU_SUSPEND)
			writeb(CUC_RESUME, &p->scb->cmd_cuc);
		else {
			writew(make16(p->xmit_cmds[0]), &p->scb->cbl_offset);
			writeb(CUC_START, &p->scb->cmd_cuc);
		}
		ni_attn586();
		dev->trans_start = jiffies;
		if (!i)
			dev_kfree_skb(skb);
		wait_for_scb_cmd(dev);
		/* test it, because CU sometimes doesn't start immediately */
		if (readb(&p->scb->cus) & CU_ACTIVE)
			break;
		if (readw(&p->xmit_cmds[0]->cmd_status))
			break;
		if (i == 15)
			printk(KERN_WARNING "%s: Can't start transmit-command.\n", dev->name);
	}
#	else
	next_nop = (p->nop_point + 1) & 0x1;
	writew(TBD_LAST | len, &p->xmit_buffs[0]->size);
	writew(make16(p->nop_cmds[next_nop]), &p->xmit_cmds[0]->cmd_link);
	writew(make16(p->nop_cmds[next_nop]),
				&p->nop_cmds[next_nop]->cmd_link);
	writew(0, &p->xmit_cmds[0]->cmd_status);
	writew(0, &p->nop_cmds[next_nop]->cmd_status);

	writew(make16(p->xmit_cmds[0]), &p->nop_cmds[p->nop_point]->cmd_link);
	dev->trans_start = jiffies;
	p->nop_point = next_nop;
	dev_kfree_skb(skb);
#	endif
#else
	writew(TBD_LAST | len, &p->xmit_buffs[p->xmit_count]->size);
	next_nop = p->xmit_count + 1
	if (next_nop == NUM_XMIT_BUFFS)
		next_nop = 0;
	writew(0, &p->xmit_cmds[p->xmit_count]->cmd_status);
	/* linkpointer of xmit-command already points to next nop cmd */
	writew(make16(p->nop_cmds[next_nop]),
				&p->nop_cmds[next_nop]->cmd_link);
	writew(0, &p->nop_cmds[next_nop]->cmd_status);
	writew(make16(p->xmit_cmds[p->xmit_count]),
				&p->nop_cmds[p->xmit_count]->cmd_link);
	dev->trans_start = jiffies;
	p->xmit_count = next_nop;
	{
		unsigned long flags;
		spin_lock_irqsave(&p->spinlock);
		if (p->xmit_count != p->xmit_last)
			netif_wake_queue(dev);
		spin_unlock_irqrestore(&p->spinlock);
	}
	dev_kfree_skb(skb);
#endif
	return 0;
}

/*******************************************
 * Someone wanna have the statistics
 */

static struct net_device_stats *ni52_get_stats(struct net_device *dev)
{
	struct priv *p = netdev_priv(dev);
	unsigned short crc, aln, rsc, ovrn;

	/* Get error-statistics from the ni82586 */
	crc = readw(&p->scb->crc_errs);
	writew(0, &p->scb->crc_errs);
	aln = readw(&p->scb->aln_errs);
	writew(0, &p->scb->aln_errs);
	rsc = readw(&p->scb->rsc_errs);
	writew(0, &p->scb->rsc_errs);
	ovrn = readw(&p->scb->ovrn_errs);
	writew(0, &p->scb->ovrn_errs);

	p->stats.rx_crc_errors += crc;
	p->stats.rx_fifo_errors += ovrn;
	p->stats.rx_frame_errors += aln;
	p->stats.rx_dropped += rsc;

	return &p->stats;
}

/********************************************************
 * Set MC list ..
 */

static void set_multicast_list(struct net_device *dev)
{
	netif_stop_queue(dev);
	ni_disint();
	alloc586(dev);
	init586(dev);
	startrecv586(dev);
	ni_enaint();
	netif_wake_queue(dev);
}

#ifdef MODULE
static struct net_device *dev_ni52;

module_param(io, int, 0);
module_param(irq, int, 0);
module_param(memstart, long, 0);
module_param(memend, long, 0);
MODULE_PARM_DESC(io, "NI5210 I/O base address,required");
MODULE_PARM_DESC(irq, "NI5210 IRQ number,required");
MODULE_PARM_DESC(memstart, "NI5210 memory base address,required");
MODULE_PARM_DESC(memend, "NI5210 memory end address,required");

int __init init_module(void)
{
	if (io <= 0x0 || !memend || !memstart || irq < 2) {
		printk(KERN_ERR "ni52: Autoprobing not allowed for modules.\n");
		printk(KERN_ERR "ni52: Set symbols 'io' 'irq' 'memstart' and 'memend'\n");
		return -ENODEV;
	}
	dev_ni52 = ni52_probe(-1);
	if (IS_ERR(dev_ni52))
		return PTR_ERR(dev_ni52);
	return 0;
}

void __exit cleanup_module(void)
{
	struct priv *p = netdev_priv(dev_ni52);
	unregister_netdev(dev_ni52);
	iounmap(p->mapped);
	release_region(dev_ni52->base_addr, NI52_TOTAL_SIZE);
	free_netdev(dev_ni52);
}
#endif /* MODULE */

MODULE_LICENSE("GPL");
