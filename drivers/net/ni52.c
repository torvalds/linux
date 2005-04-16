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
 *   compile with:
 *       gcc -O2 -fomit-frame-pointer -m486 -D__KERNEL__ -DMODULE -c ni52.c
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
 *   (before getting an i82596 (yes 596 not 586) manual, the existing drivers helped
 *    me a lot to understand this tricky chip.)
 *
 * Known Problems:
 *   The internal sysbus seems to be slow. So we often lose packets because of
 *   overruns while receiving from a fast remote host.
 *   This can slow down TCP connections. Maybe the newer ni5210 cards are better.
 *   my experience is, that if a machine sends with more than about 500-600K/s
 *   the fifo/sysbus overflows.
 *
 * IMPORTANT NOTE:
 *   On fast networks, it's a (very) good idea to have 16K shared memory. With
 *   8K, we can store only 4 receive frames, so it can (easily) happen that a remote
 *   machine 'overruns' our system.
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
 *   sending in NOP-mode: peak performance up to 530K/s (but better don't run this mode)
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
 * 30.Sep.93: Added nop-chain .. driver now runs with only one Xmit-Buff, too (MH)
 *
 * < 30.Sep.93: first versions
 */

static int debuglevel;	/* debug-printk 0: off 1: a few 2: more */
static int automatic_resume; /* experimental .. better should be zero */
static int rfdadd;	/* rfdadd=1 may be better for 8K MEM cards */
static int fifo=0x8;	/* don't change */

/* #define REALLY_SLOW_IO */

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

#define ni_attn586()  {outb(0,dev->base_addr+NI52_ATTENTION);}
#define ni_reset586() {outb(0,dev->base_addr+NI52_RESET);}
#define ni_disint()   {outb(0,dev->base_addr+NI52_INTDIS);}
#define ni_enaint()   {outb(0,dev->base_addr+NI52_INTENA);}

#define make32(ptr16) (p->memtop + (short) (ptr16) )
#define make24(ptr32) ( ((char *) (ptr32)) - p->base)
#define make16(ptr32) ((unsigned short) ((unsigned long)(ptr32) - (unsigned long) p->memtop ))

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

/* different DELAYs */
#define DELAY(x) mdelay(32 * x);
#define DELAY_16(); { udelay(16); }
#define DELAY_18(); { udelay(4); }

/* wait for command with timeout: */
#define WAIT_4_SCB_CMD() \
{ int i; \
  for(i=0;i<16384;i++) { \
    if(!p->scb->cmd_cuc) break; \
    DELAY_18(); \
    if(i == 16383) { \
      printk("%s: scb_cmd timed out: %04x,%04x .. disabling i82586!!\n",dev->name,p->scb->cmd_cuc,p->scb->cus); \
       if(!p->reseted) { p->reseted = 1; ni_reset586(); } } } }

#define WAIT_4_SCB_CMD_RUC() { int i; \
  for(i=0;i<16384;i++) { \
    if(!p->scb->cmd_ruc) break; \
    DELAY_18(); \
    if(i == 16383) { \
      printk("%s: scb_cmd (ruc) timed out: %04x,%04x .. disabling i82586!!\n",dev->name,p->scb->cmd_ruc,p->scb->rus); \
       if(!p->reseted) { p->reseted = 1; ni_reset586(); } } } }

#define WAIT_4_STAT_COMPL(addr) { int i; \
   for(i=0;i<32767;i++) { \
     if((addr)->cmd_status & STAT_COMPL) break; \
     DELAY_16(); DELAY_16(); } }

#define NI52_TOTAL_SIZE 16
#define NI52_ADDR0 0x02
#define NI52_ADDR1 0x07
#define NI52_ADDR2 0x01

static int     ni52_probe1(struct net_device *dev,int ioaddr);
static irqreturn_t ni52_interrupt(int irq,void *dev_id,struct pt_regs *reg_ptr);
static int     ni52_open(struct net_device *dev);
static int     ni52_close(struct net_device *dev);
static int     ni52_send_packet(struct sk_buff *,struct net_device *);
static struct  net_device_stats *ni52_get_stats(struct net_device *dev);
static void    set_multicast_list(struct net_device *dev);
static void    ni52_timeout(struct net_device *dev);
#if 0
static void    ni52_dump(struct net_device *,void *);
#endif

/* helper-functions */
static int     init586(struct net_device *dev);
static int     check586(struct net_device *dev,char *where,unsigned size);
static void    alloc586(struct net_device *dev);
static void    startrecv586(struct net_device *dev);
static void   *alloc_rfa(struct net_device *dev,void *ptr);
static void    ni52_rcv_int(struct net_device *dev);
static void    ni52_xmt_int(struct net_device *dev);
static void    ni52_rnr_int(struct net_device *dev);

struct priv
{
	struct net_device_stats stats;
	unsigned long base;
	char *memtop;
	long int lock;
	int reseted;
	volatile struct rfd_struct	*rfd_last,*rfd_top,*rfd_first;
	volatile struct scp_struct	*scp;	/* volatile is important */
	volatile struct iscp_struct	*iscp;	/* volatile is important */
	volatile struct scb_struct	*scb;	/* volatile is important */
	volatile struct tbd_struct	*xmit_buffs[NUM_XMIT_BUFFS];
#if (NUM_XMIT_BUFFS == 1)
	volatile struct transmit_cmd_struct *xmit_cmds[2];
	volatile struct nop_cmd_struct *nop_cmds[2];
#else
	volatile struct transmit_cmd_struct *xmit_cmds[NUM_XMIT_BUFFS];
	volatile struct nop_cmd_struct *nop_cmds[NUM_XMIT_BUFFS];
#endif
	volatile int		nop_point,num_recv_buffs;
	volatile char		*xmit_cbuffs[NUM_XMIT_BUFFS];
	volatile int		xmit_count,xmit_last;
};

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

	ret = request_irq(dev->irq, &ni52_interrupt,0,dev->name,dev);
	if (ret)
	{
		ni_reset586();
		return ret;
	}

	netif_start_queue(dev);

	return 0; /* most done by init */
}

/**********************************************
 * Check to see if there's an 82586 out there.
 */
static int check586(struct net_device *dev,char *where,unsigned size)
{
	struct priv pb;
	struct priv *p = /* (struct priv *) dev->priv*/ &pb;
	char *iscp_addrs[2];
	int i;

	p->base = (unsigned long) isa_bus_to_virt((unsigned long)where) + size - 0x01000000;
	p->memtop = isa_bus_to_virt((unsigned long)where) + size;
	p->scp = (struct scp_struct *)(p->base + SCP_DEFAULT_ADDRESS);
	memset((char *)p->scp,0, sizeof(struct scp_struct));
	for(i=0;i<sizeof(struct scp_struct);i++) /* memory was writeable? */
		if(((char *)p->scp)[i])
			return 0;
	p->scp->sysbus = SYSBUSVAL;				/* 1 = 8Bit-Bus, 0 = 16 Bit */
	if(p->scp->sysbus != SYSBUSVAL)
		return 0;

	iscp_addrs[0] = isa_bus_to_virt((unsigned long)where);
	iscp_addrs[1]= (char *) p->scp - sizeof(struct iscp_struct);

	for(i=0;i<2;i++)
	{
		p->iscp = (struct iscp_struct *) iscp_addrs[i];
		memset((char *)p->iscp,0, sizeof(struct iscp_struct));

		p->scp->iscp = make24(p->iscp);
		p->iscp->busy = 1;

		ni_reset586();
		ni_attn586();
		DELAY(1);	/* wait a while... */

		if(p->iscp->busy) /* i82586 clears 'busy' after successful init */
			return 0;
	}
	return 1;
}

/******************************************************************
 * set iscp at the right place, called by ni52_probe1 and open586.
 */
static void alloc586(struct net_device *dev)
{
	struct priv *p =	(struct priv *) dev->priv;

	ni_reset586();
	DELAY(1);

	p->scp	= (struct scp_struct *)	(p->base + SCP_DEFAULT_ADDRESS);
	p->scb	= (struct scb_struct *)	isa_bus_to_virt(dev->mem_start);
	p->iscp = (struct iscp_struct *) ((char *)p->scp - sizeof(struct iscp_struct));

	memset((char *) p->iscp,0,sizeof(struct iscp_struct));
	memset((char *) p->scp ,0,sizeof(struct scp_struct));

	p->scp->iscp = make24(p->iscp);
	p->scp->sysbus = SYSBUSVAL;
	p->iscp->scb_offset = make16(p->scb);

	p->iscp->busy = 1;
	ni_reset586();
	ni_attn586();

	DELAY(1);

	if(p->iscp->busy)
		printk("%s: Init-Problems (alloc).\n",dev->name);

	p->reseted = 0;

	memset((char *)p->scb,0,sizeof(struct scb_struct));
}

/* set: io,irq,memstart,memend or set it when calling insmod */
static int irq=9;
static int io=0x300;
static long memstart;	/* e.g 0xd0000 */
static long memend;	/* e.g 0xd4000 */

/**********************************************
 * probe the ni5210-card
 */
struct net_device * __init ni52_probe(int unit)
{
	struct net_device *dev = alloc_etherdev(sizeof(struct priv));
	static int ports[] = {0x300, 0x280, 0x360 , 0x320 , 0x340, 0};
	int *port;
	int err = 0;

	if (!dev)
		return ERR_PTR(-ENOMEM);

	if (unit >= 0) {
		sprintf(dev->name, "eth%d", unit);
		netdev_boot_setup_check(dev);
		io = dev->base_addr;
		irq = dev->irq;
		memstart = dev->mem_start;
		memend = dev->mem_end;
	}

	SET_MODULE_OWNER(dev);

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
	release_region(dev->base_addr, NI52_TOTAL_SIZE);
out:
	free_netdev(dev);
	return ERR_PTR(err);
}

static int __init ni52_probe1(struct net_device *dev,int ioaddr)
{
	int i, size, retval;

	dev->base_addr = ioaddr;
	dev->irq = irq;
	dev->mem_start = memstart;
	dev->mem_end = memend;

	if (!request_region(ioaddr, NI52_TOTAL_SIZE, DRV_NAME))
		return -EBUSY;

	if( !(inb(ioaddr+NI52_MAGIC1) == NI52_MAGICVAL1) ||
	    !(inb(ioaddr+NI52_MAGIC2) == NI52_MAGICVAL2)) {
		retval = -ENODEV;
		goto out;
	}

	for(i=0;i<ETH_ALEN;i++)
		dev->dev_addr[i] = inb(dev->base_addr+i);

	if(dev->dev_addr[0] != NI52_ADDR0 || dev->dev_addr[1] != NI52_ADDR1
		 || dev->dev_addr[2] != NI52_ADDR2) {
		retval = -ENODEV;
		goto out;
	}

	printk(KERN_INFO "%s: NI5210 found at %#3lx, ",dev->name,dev->base_addr);

	/*
	 * check (or search) IO-Memory, 8K and 16K
	 */
#ifdef MODULE
	size = dev->mem_end - dev->mem_start;
	if(size != 0x2000 && size != 0x4000) {
		printk("\n%s: Illegal memory size %d. Allowed is 0x2000 or 0x4000 bytes.\n",dev->name,size);
		retval = -ENODEV;
		goto out;
	}
	if(!check586(dev,(char *) dev->mem_start,size)) {
		printk("?memcheck, Can't find memory at 0x%lx with size %d!\n",dev->mem_start,size);
		retval = -ENODEV;
		goto out;
	}
#else
	if(dev->mem_start != 0) /* no auto-mem-probe */
	{
		size = 0x4000; /* check for 16K mem */
		if(!check586(dev,(char *) dev->mem_start,size)) {
			size = 0x2000; /* check for 8K mem */
			if(!check586(dev,(char *) dev->mem_start,size)) {
				printk("?memprobe, Can't find memory at 0x%lx!\n",dev->mem_start);
				retval = -ENODEV;
				goto out;
			}
		}
	}
	else
	{
		static long memaddrs[] = { 0xc8000,0xca000,0xcc000,0xce000,0xd0000,0xd2000,
					0xd4000,0xd6000,0xd8000,0xda000,0xdc000, 0 };
		for(i=0;;i++)
		{
			if(!memaddrs[i]) {
				printk("?memprobe, Can't find io-memory!\n");
				retval = -ENODEV;
				goto out;
			}
			dev->mem_start = memaddrs[i];
			size = 0x2000; /* check for 8K mem */
			if(check586(dev,(char *)dev->mem_start,size)) /* 8K-check */
				break;
			size = 0x4000; /* check for 16K mem */
			if(check586(dev,(char *)dev->mem_start,size)) /* 16K-check */
				break;
		}
	}
	dev->mem_end = dev->mem_start + size; /* set mem_end showed by 'ifconfig' */
#endif

	memset((char *) dev->priv,0,sizeof(struct priv));

	((struct priv *) (dev->priv))->memtop = isa_bus_to_virt(dev->mem_start) + size;
	((struct priv *) (dev->priv))->base =	(unsigned long) isa_bus_to_virt(dev->mem_start) + size - 0x01000000;
	alloc586(dev);

	/* set number of receive-buffs according to memsize */
	if(size == 0x2000)
		((struct priv *) dev->priv)->num_recv_buffs = NUM_RECV_BUFFS_8;
	else
		((struct priv *) dev->priv)->num_recv_buffs = NUM_RECV_BUFFS_16;

	printk("Memaddr: 0x%lx, Memsize: %d, ",dev->mem_start,size);

	if(dev->irq < 2)
	{
		unsigned long irq_mask;

		irq_mask = probe_irq_on();
		ni_reset586();
		ni_attn586();

		mdelay(20);
		dev->irq = probe_irq_off(irq_mask);
		if(!dev->irq)
		{
			printk("?autoirq, Failed to detect IRQ line!\n");
			retval = -EAGAIN;
			goto out;
		}
		printk("IRQ %d (autodetected).\n",dev->irq);
	}
	else	{
		if(dev->irq == 2)
			dev->irq = 9;
		printk("IRQ %d (assigned and not checked!).\n",dev->irq);
	}

	dev->open		= ni52_open;
	dev->stop		= ni52_close;
	dev->get_stats		= ni52_get_stats;
	dev->tx_timeout 	= ni52_timeout;
	dev->watchdog_timeo	= HZ/20;
	dev->hard_start_xmit 	= ni52_send_packet;
	dev->set_multicast_list = set_multicast_list;

	dev->if_port 		= 0;

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
	void *ptr;
	int i,result=0;
	struct priv *p = (struct priv *) dev->priv;
	volatile struct configure_cmd_struct	*cfg_cmd;
	volatile struct iasetup_cmd_struct *ias_cmd;
	volatile struct tdr_cmd_struct *tdr_cmd;
	volatile struct mcsetup_cmd_struct *mc_cmd;
	struct dev_mc_list *dmi=dev->mc_list;
	int num_addrs=dev->mc_count;

	ptr = (void *) ((char *)p->scb + sizeof(struct scb_struct));

	cfg_cmd = (struct configure_cmd_struct *)ptr; /* configure-command */
	cfg_cmd->cmd_status	= 0;
	cfg_cmd->cmd_cmd	= CMD_CONFIGURE | CMD_LAST;
	cfg_cmd->cmd_link	= 0xffff;

	cfg_cmd->byte_cnt	= 0x0a; /* number of cfg bytes */
	cfg_cmd->fifo		= fifo; /* fifo-limit (8=tx:32/rx:64) */
	cfg_cmd->sav_bf		= 0x40; /* hold or discard bad recv frames (bit 7) */
	cfg_cmd->adr_len	= 0x2e; /* addr_len |!src_insert |pre-len |loopback */
	cfg_cmd->priority	= 0x00;
	cfg_cmd->ifs		= 0x60;
	cfg_cmd->time_low	= 0x00;
	cfg_cmd->time_high	= 0xf2;
	cfg_cmd->promisc	= 0;
	if(dev->flags & IFF_ALLMULTI) {
		int len = ((char *) p->iscp - (char *) ptr - 8) / 6;
		if(num_addrs > len)	{
			printk("%s: switching to promisc. mode\n",dev->name);
			dev->flags|=IFF_PROMISC;
		}
	}
	if(dev->flags&IFF_PROMISC)
	{
			 cfg_cmd->promisc=1;
			 dev->flags|=IFF_PROMISC;
	}
	cfg_cmd->carr_coll	= 0x00;

	p->scb->cbl_offset	= make16(cfg_cmd);
	p->scb->cmd_ruc		= 0;

	p->scb->cmd_cuc		= CUC_START; /* cmd.-unit start */
	ni_attn586();

	WAIT_4_STAT_COMPL(cfg_cmd);

	if((cfg_cmd->cmd_status & (STAT_OK|STAT_COMPL)) != (STAT_COMPL|STAT_OK))
	{
		printk("%s: configure command failed: %x\n",dev->name,cfg_cmd->cmd_status);
		return 1;
	}

	/*
	 * individual address setup
	 */

	ias_cmd = (struct iasetup_cmd_struct *)ptr;

	ias_cmd->cmd_status	= 0;
	ias_cmd->cmd_cmd	= CMD_IASETUP | CMD_LAST;
	ias_cmd->cmd_link	= 0xffff;

	memcpy((char *)&ias_cmd->iaddr,(char *) dev->dev_addr,ETH_ALEN);

	p->scb->cbl_offset = make16(ias_cmd);

	p->scb->cmd_cuc = CUC_START; /* cmd.-unit start */
	ni_attn586();

	WAIT_4_STAT_COMPL(ias_cmd);

	if((ias_cmd->cmd_status & (STAT_OK|STAT_COMPL)) != (STAT_OK|STAT_COMPL)) {
		printk("%s (ni52): individual address setup command failed: %04x\n",dev->name,ias_cmd->cmd_status);
		return 1;
	}

	/*
	 * TDR, wire check .. e.g. no resistor e.t.c
	 */
	 
	tdr_cmd = (struct tdr_cmd_struct *)ptr;

	tdr_cmd->cmd_status	= 0;
	tdr_cmd->cmd_cmd	= CMD_TDR | CMD_LAST;
	tdr_cmd->cmd_link	= 0xffff;
	tdr_cmd->status		= 0;

	p->scb->cbl_offset = make16(tdr_cmd);
	p->scb->cmd_cuc = CUC_START; /* cmd.-unit start */
	ni_attn586();

	WAIT_4_STAT_COMPL(tdr_cmd);

	if(!(tdr_cmd->cmd_status & STAT_COMPL))
	{
		printk("%s: Problems while running the TDR.\n",dev->name);
	}
	else
	{
		DELAY_16(); /* wait for result */
		result = tdr_cmd->status;

		p->scb->cmd_cuc = p->scb->cus & STAT_MASK;
		ni_attn586(); /* ack the interrupts */

		if(result & TDR_LNK_OK)
			;
		else if(result & TDR_XCVR_PRB)
			printk("%s: TDR: Transceiver problem. Check the cable(s)!\n",dev->name);
		else if(result & TDR_ET_OPN)
			printk("%s: TDR: No correct termination %d clocks away.\n",dev->name,result & TDR_TIMEMASK);
		else if(result & TDR_ET_SRT)
		{
			if (result & TDR_TIMEMASK) /* time == 0 -> strange :-) */
				printk("%s: TDR: Detected a short circuit %d clocks away.\n",dev->name,result & TDR_TIMEMASK);
		}
		else
			printk("%s: TDR: Unknown status %04x\n",dev->name,result);
	}

	/*
	 * Multicast setup
	 */
	if(num_addrs && !(dev->flags & IFF_PROMISC) )
	{
		mc_cmd = (struct mcsetup_cmd_struct *) ptr;
		mc_cmd->cmd_status = 0;
		mc_cmd->cmd_cmd = CMD_MCSETUP | CMD_LAST;
		mc_cmd->cmd_link = 0xffff;
		mc_cmd->mc_cnt = num_addrs * 6;

		for(i=0;i<num_addrs;i++,dmi=dmi->next)
			memcpy((char *) mc_cmd->mc_list[i], dmi->dmi_addr,6);

		p->scb->cbl_offset = make16(mc_cmd);
		p->scb->cmd_cuc = CUC_START;
		ni_attn586();

		WAIT_4_STAT_COMPL(mc_cmd);

		if( (mc_cmd->cmd_status & (STAT_COMPL|STAT_OK)) != (STAT_COMPL|STAT_OK) )
			printk("%s: Can't apply multicast-address-list.\n",dev->name);
	}

	/*
	 * alloc nop/xmit-cmds
	 */
#if (NUM_XMIT_BUFFS == 1)
	for(i=0;i<2;i++)
	{
		p->nop_cmds[i] 			= (struct nop_cmd_struct *)ptr;
		p->nop_cmds[i]->cmd_cmd		= CMD_NOP;
		p->nop_cmds[i]->cmd_status 	= 0;
		p->nop_cmds[i]->cmd_link	= make16((p->nop_cmds[i]));
		ptr = (char *) ptr + sizeof(struct nop_cmd_struct);
	}
#else
	for(i=0;i<NUM_XMIT_BUFFS;i++)
	{
		p->nop_cmds[i]			= (struct nop_cmd_struct *)ptr;
		p->nop_cmds[i]->cmd_cmd		= CMD_NOP;
		p->nop_cmds[i]->cmd_status	= 0;
		p->nop_cmds[i]->cmd_link	= make16((p->nop_cmds[i]));
		ptr = (char *) ptr + sizeof(struct nop_cmd_struct);
	}
#endif

	ptr = alloc_rfa(dev,(void *)ptr); /* init receive-frame-area */

	/*
	 * alloc xmit-buffs / init xmit_cmds
	 */
	for(i=0;i<NUM_XMIT_BUFFS;i++)
	{
		p->xmit_cmds[i] = (struct transmit_cmd_struct *)ptr; /*transmit cmd/buff 0*/
		ptr = (char *) ptr + sizeof(struct transmit_cmd_struct);
		p->xmit_cbuffs[i] = (char *)ptr; /* char-buffs */
		ptr = (char *) ptr + XMIT_BUFF_SIZE;
		p->xmit_buffs[i] = (struct tbd_struct *)ptr; /* TBD */
		ptr = (char *) ptr + sizeof(struct tbd_struct);
		if((void *)ptr > (void *)p->iscp)
		{
			printk("%s: not enough shared-mem for your configuration!\n",dev->name);
			return 1;
		}
		memset((char *)(p->xmit_cmds[i]) ,0, sizeof(struct transmit_cmd_struct));
		memset((char *)(p->xmit_buffs[i]),0, sizeof(struct tbd_struct));
		p->xmit_cmds[i]->cmd_link = make16(p->nop_cmds[(i+1)%NUM_XMIT_BUFFS]);
		p->xmit_cmds[i]->cmd_status = STAT_COMPL;
		p->xmit_cmds[i]->cmd_cmd = CMD_XMIT | CMD_INT;
		p->xmit_cmds[i]->tbd_offset = make16((p->xmit_buffs[i]));
		p->xmit_buffs[i]->next = 0xffff;
		p->xmit_buffs[i]->buffer = make24((p->xmit_cbuffs[i]));
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
	p->scb->cbl_offset = make16(p->nop_cmds[0]);
	p->scb->cmd_cuc = CUC_START;
	ni_attn586();
	WAIT_4_SCB_CMD();
#else
	p->xmit_cmds[0]->cmd_link = make16(p->xmit_cmds[0]);
	p->xmit_cmds[0]->cmd_cmd	= CMD_XMIT | CMD_SUSPEND | CMD_INT;
#endif

	/*
	 * ack. interrupts
	 */
	p->scb->cmd_cuc = p->scb->cus & STAT_MASK;
	ni_attn586();
	DELAY_16();

	ni_enaint();

	return 0;
}

/******************************************************
 * This is a helper routine for ni52_rnr_int() and init586().
 * It sets up the Receive Frame Area (RFA).
 */

static void *alloc_rfa(struct net_device *dev,void *ptr)
{
	volatile struct rfd_struct *rfd = (struct rfd_struct *)ptr;
	volatile struct rbd_struct *rbd;
	int i;
	struct priv *p = (struct priv *) dev->priv;

	memset((char *) rfd,0,sizeof(struct rfd_struct)*(p->num_recv_buffs+rfdadd));
	p->rfd_first = rfd;

	for(i = 0; i < (p->num_recv_buffs+rfdadd); i++) {
		rfd[i].next = make16(rfd + (i+1) % (p->num_recv_buffs+rfdadd) );
		rfd[i].rbd_offset = 0xffff;
	}
	rfd[p->num_recv_buffs-1+rfdadd].last = RFD_SUSP;	 /* RU suspend */

	ptr = (void *) (rfd + (p->num_recv_buffs + rfdadd) );

	rbd = (struct rbd_struct *) ptr;
	ptr = (void *) (rbd + p->num_recv_buffs);

	 /* clr descriptors */
	memset((char *) rbd,0,sizeof(struct rbd_struct)*(p->num_recv_buffs));

	for(i=0;i<p->num_recv_buffs;i++)
	{
		rbd[i].next = make16((rbd + (i+1) % p->num_recv_buffs));
		rbd[i].size = RECV_BUFF_SIZE;
		rbd[i].buffer = make24(ptr);
		ptr = (char *) ptr + RECV_BUFF_SIZE;
	}

	p->rfd_top	= p->rfd_first;
	p->rfd_last = p->rfd_first + (p->num_recv_buffs - 1 + rfdadd);

	p->scb->rfa_offset		= make16(p->rfd_first);
	p->rfd_first->rbd_offset	= make16(rbd);

	return ptr;
}


/**************************************************
 * Interrupt Handler ...
 */

static irqreturn_t ni52_interrupt(int irq,void *dev_id,struct pt_regs *reg_ptr)
{
	struct net_device *dev = dev_id;
	unsigned short stat;
	int cnt=0;
	struct priv *p;

	if (!dev) {
		printk ("ni5210-interrupt: irq %d for unknown device.\n",irq);
		return IRQ_NONE;
	}
	p = (struct priv *) dev->priv;

	if(debuglevel > 1)
		printk("I");

	WAIT_4_SCB_CMD(); /* wait for last command	*/

	while((stat=p->scb->cus & STAT_MASK))
	{
		p->scb->cmd_cuc = stat;
		ni_attn586();

		if(stat & STAT_FR)	 /* received a frame */
			ni52_rcv_int(dev);

		if(stat & STAT_RNR) /* RU went 'not ready' */
		{
			printk("(R)");
			if(p->scb->rus & RU_SUSPEND) /* special case: RU_SUSPEND */
			{
				WAIT_4_SCB_CMD();
				p->scb->cmd_ruc = RUC_RESUME;
				ni_attn586();
				WAIT_4_SCB_CMD_RUC();
			}
			else
			{
				printk("%s: Receiver-Unit went 'NOT READY': %04x/%02x.\n",dev->name,(int) stat,(int) p->scb->rus);
				ni52_rnr_int(dev);
			}
		}

		if(stat & STAT_CX)		/* command with I-bit set complete */
			 ni52_xmt_int(dev);

#ifndef NO_NOPCOMMANDS
		if(stat & STAT_CNA)	/* CU went 'not ready' */
		{
			if(netif_running(dev))
				printk("%s: oops! CU has left active state. stat: %04x/%02x.\n",dev->name,(int) stat,(int) p->scb->cus);
		}
#endif

		if(debuglevel > 1)
			printk("%d",cnt++);

		WAIT_4_SCB_CMD(); /* wait for ack. (ni52_xmt_int can be faster than ack!!) */
		if(p->scb->cmd_cuc)	 /* timed out? */
		{
			printk("%s: Acknowledge timed out.\n",dev->name);
			ni_disint();
			break;
		}
	}

	if(debuglevel > 1)
		printk("i");
	return IRQ_HANDLED;
}

/*******************************************************
 * receive-interrupt
 */

static void ni52_rcv_int(struct net_device *dev)
{
	int status,cnt=0;
	unsigned short totlen;
	struct sk_buff *skb;
	struct rbd_struct *rbd;
	struct priv *p = (struct priv *) dev->priv;

	if(debuglevel > 0)
		printk("R");

	for(;(status = p->rfd_top->stat_high) & RFD_COMPL;)
	{
			rbd = (struct rbd_struct *) make32(p->rfd_top->rbd_offset);

			if(status & RFD_OK) /* frame received without error? */
			{
				if( (totlen = rbd->status) & RBD_LAST) /* the first and the last buffer? */
				{
					totlen &= RBD_MASK; /* length of this frame */
					rbd->status = 0;
					skb = (struct sk_buff *) dev_alloc_skb(totlen+2);
					if(skb != NULL)
					{
						skb->dev = dev;
						skb_reserve(skb,2);
						skb_put(skb,totlen);
						eth_copy_and_sum(skb,(char *) p->base+(unsigned long) rbd->buffer,totlen,0);
						skb->protocol=eth_type_trans(skb,dev);
						netif_rx(skb);
						dev->last_rx = jiffies;
						p->stats.rx_packets++;
						p->stats.rx_bytes += totlen;
					}
					else
						p->stats.rx_dropped++;
				}
				else
				{
					int rstat;
						 /* free all RBD's until RBD_LAST is set */
					totlen = 0;
					while(!((rstat=rbd->status) & RBD_LAST))
					{
						totlen += rstat & RBD_MASK;
						if(!rstat)
						{
							printk("%s: Whoops .. no end mark in RBD list\n",dev->name);
							break;
						}
						rbd->status = 0;
						rbd = (struct rbd_struct *) make32(rbd->next);
					}
					totlen += rstat & RBD_MASK;
					rbd->status = 0;
					printk("%s: received oversized frame! length: %d\n",dev->name,totlen);
					p->stats.rx_dropped++;
			 }
		}
		else /* frame !(ok), only with 'save-bad-frames' */
		{
			printk("%s: oops! rfd-error-status: %04x\n",dev->name,status);
			p->stats.rx_errors++;
		}
		p->rfd_top->stat_high = 0;
		p->rfd_top->last = RFD_SUSP; /* maybe exchange by RFD_LAST */
		p->rfd_top->rbd_offset = 0xffff;
		p->rfd_last->last = 0;				/* delete RFD_SUSP	*/
		p->rfd_last = p->rfd_top;
		p->rfd_top = (struct rfd_struct *) make32(p->rfd_top->next); /* step to next RFD */
		p->scb->rfa_offset = make16(p->rfd_top);

		if(debuglevel > 0)
			printk("%d",cnt++);
	}

	if(automatic_resume)
	{
		WAIT_4_SCB_CMD();
		p->scb->cmd_ruc = RUC_RESUME;
		ni_attn586();
		WAIT_4_SCB_CMD_RUC();
	}

#ifdef WAIT_4_BUSY
	{
		int i;
		for(i=0;i<1024;i++)
		{
			if(p->rfd_top->status)
				break;
			DELAY_16();
			if(i == 1023)
				printk("%s: RU hasn't fetched next RFD (not busy/complete)\n",dev->name);
		}
	}
#endif

#if 0
	if(!at_least_one)
	{
		int i;
		volatile struct rfd_struct *rfds=p->rfd_top;
		volatile struct rbd_struct *rbds;
		printk("%s: received a FC intr. without having a frame: %04x %d\n",dev->name,status,old_at_least);
		for(i=0;i< (p->num_recv_buffs+4);i++)
		{
			rbds = (struct rbd_struct *) make32(rfds->rbd_offset);
			printk("%04x:%04x ",rfds->status,rbds->status);
			rfds = (struct rfd_struct *) make32(rfds->next);
		}
		printk("\nerrs: %04x %04x stat: %04x\n",(int)p->scb->rsc_errs,(int)p->scb->ovrn_errs,(int)p->scb->status);
		printk("\nerrs: %04x %04x rus: %02x, cus: %02x\n",(int)p->scb->rsc_errs,(int)p->scb->ovrn_errs,(int)p->scb->rus,(int)p->scb->cus);
	}
	old_at_least = at_least_one;
#endif

	if(debuglevel > 0)
		printk("r");
}

/**********************************************************
 * handle 'Receiver went not ready'.
 */

static void ni52_rnr_int(struct net_device *dev)
{
	struct priv *p = (struct priv *) dev->priv;

	p->stats.rx_errors++;

	WAIT_4_SCB_CMD();		/* wait for the last cmd, WAIT_4_FULLSTAT?? */
	p->scb->cmd_ruc = RUC_ABORT; /* usually the RU is in the 'no resource'-state .. abort it now. */
	ni_attn586();
	WAIT_4_SCB_CMD_RUC();		/* wait for accept cmd. */

	alloc_rfa(dev,(char *)p->rfd_first);
/* maybe add a check here, before restarting the RU */
	startrecv586(dev); /* restart RU */

	printk("%s: Receive-Unit restarted. Status: %04x\n",dev->name,p->scb->rus);

}

/**********************************************************
 * handle xmit - interrupt
 */

static void ni52_xmt_int(struct net_device *dev)
{
	int status;
	struct priv *p = (struct priv *) dev->priv;

	if(debuglevel > 0)
		printk("X");

	status = p->xmit_cmds[p->xmit_last]->cmd_status;
	if(!(status & STAT_COMPL))
		printk("%s: strange .. xmit-int without a 'COMPLETE'\n",dev->name);

	if(status & STAT_OK)
	{
		p->stats.tx_packets++;
		p->stats.collisions += (status & TCMD_MAXCOLLMASK);
	}
	else
	{
		p->stats.tx_errors++;
		if(status & TCMD_LATECOLL) {
			printk("%s: late collision detected.\n",dev->name);
			p->stats.collisions++;
		}
		else if(status & TCMD_NOCARRIER) {
			p->stats.tx_carrier_errors++;
			printk("%s: no carrier detected.\n",dev->name);
		}
		else if(status & TCMD_LOSTCTS)
			printk("%s: loss of CTS detected.\n",dev->name);
		else if(status & TCMD_UNDERRUN) {
			p->stats.tx_fifo_errors++;
			printk("%s: DMA underrun detected.\n",dev->name);
		}
		else if(status & TCMD_MAXCOLL) {
			printk("%s: Max. collisions exceeded.\n",dev->name);
			p->stats.collisions += 16;
		}
	}

#if (NUM_XMIT_BUFFS > 1)
	if( (++p->xmit_last) == NUM_XMIT_BUFFS)
		p->xmit_last = 0;
#endif
	netif_wake_queue(dev);
}

/***********************************************************
 * (re)start the receiver
 */

static void startrecv586(struct net_device *dev)
{
	struct priv *p = (struct priv *) dev->priv;

	WAIT_4_SCB_CMD();
	WAIT_4_SCB_CMD_RUC();
	p->scb->rfa_offset = make16(p->rfd_first);
	p->scb->cmd_ruc = RUC_START;
	ni_attn586();		/* start cmd. */
	WAIT_4_SCB_CMD_RUC();	/* wait for accept cmd. (no timeout!!) */
}

static void ni52_timeout(struct net_device *dev)
{
	struct priv *p = (struct priv *) dev->priv;
#ifndef NO_NOPCOMMANDS
	if(p->scb->cus & CU_ACTIVE) /* COMMAND-UNIT active? */
	{
		netif_wake_queue(dev);
#ifdef DEBUG
		printk("%s: strange ... timeout with CU active?!?\n",dev->name);
		printk("%s: X0: %04x N0: %04x N1: %04x %d\n",dev->name,(int)p->xmit_cmds[0]->cmd_status,(int)p->nop_cmds[0]->cmd_status,(int)p->nop_cmds[1]->cmd_status,(int)p->nop_point);
#endif
		p->scb->cmd_cuc = CUC_ABORT;
		ni_attn586();
		WAIT_4_SCB_CMD();
		p->scb->cbl_offset = make16(p->nop_cmds[p->nop_point]);
		p->scb->cmd_cuc = CUC_START;
		ni_attn586();
		WAIT_4_SCB_CMD();
		dev->trans_start = jiffies;
		return 0;
	}
#endif
	{
#ifdef DEBUG
		printk("%s: xmitter timed out, try to restart! stat: %02x\n",dev->name,p->scb->cus);
		printk("%s: command-stats: %04x %04x\n",dev->name,p->xmit_cmds[0]->cmd_status,p->xmit_cmds[1]->cmd_status);
		printk("%s: check, whether you set the right interrupt number!\n",dev->name);
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
	int len,i;
#ifndef NO_NOPCOMMANDS
	int next_nop;
#endif
	struct priv *p = (struct priv *) dev->priv;

	if(skb->len > XMIT_BUFF_SIZE)
	{
		printk("%s: Sorry, max. framelength is %d bytes. The length of your frame is %d bytes.\n",dev->name,XMIT_BUFF_SIZE,skb->len);
		return 0;
	}

	netif_stop_queue(dev);

#if(NUM_XMIT_BUFFS > 1)
	if(test_and_set_bit(0,(void *) &p->lock)) {
		printk("%s: Queue was locked\n",dev->name);
		return 1;
	}
	else
#endif
	{
		memcpy((char *)p->xmit_cbuffs[p->xmit_count],(char *)(skb->data),skb->len);
		len = skb->len;
		if (len < ETH_ZLEN) {
			len = ETH_ZLEN;
			memset((char *)p->xmit_cbuffs[p->xmit_count]+skb->len, 0, len - skb->len);
		}

#if (NUM_XMIT_BUFFS == 1)
#	ifdef NO_NOPCOMMANDS

#ifdef DEBUG
		if(p->scb->cus & CU_ACTIVE)
		{
			printk("%s: Hmmm .. CU is still running and we wanna send a new packet.\n",dev->name);
			printk("%s: stat: %04x %04x\n",dev->name,p->scb->cus,p->xmit_cmds[0]->cmd_status);
		}
#endif

		p->xmit_buffs[0]->size = TBD_LAST | len;
		for(i=0;i<16;i++)
		{
			p->xmit_cmds[0]->cmd_status = 0;
			WAIT_4_SCB_CMD();
			if( (p->scb->cus & CU_STATUS) == CU_SUSPEND)
				p->scb->cmd_cuc = CUC_RESUME;
			else
			{
				p->scb->cbl_offset = make16(p->xmit_cmds[0]);
				p->scb->cmd_cuc = CUC_START;
			}

			ni_attn586();
			dev->trans_start = jiffies;
			if(!i)
				dev_kfree_skb(skb);
			WAIT_4_SCB_CMD();
			if( (p->scb->cus & CU_ACTIVE)) /* test it, because CU sometimes doesn't start immediately */
				break;
			if(p->xmit_cmds[0]->cmd_status)
				break;
			if(i==15)
				printk("%s: Can't start transmit-command.\n",dev->name);
		}
#	else
		next_nop = (p->nop_point + 1) & 0x1;
		p->xmit_buffs[0]->size = TBD_LAST | len;

		p->xmit_cmds[0]->cmd_link	 = p->nop_cmds[next_nop]->cmd_link
																= make16((p->nop_cmds[next_nop]));
		p->xmit_cmds[0]->cmd_status = p->nop_cmds[next_nop]->cmd_status = 0;

		p->nop_cmds[p->nop_point]->cmd_link = make16((p->xmit_cmds[0]));
		dev->trans_start = jiffies;
		p->nop_point = next_nop;
		dev_kfree_skb(skb);
#	endif
#else
		p->xmit_buffs[p->xmit_count]->size = TBD_LAST | len;
		if( (next_nop = p->xmit_count + 1) == NUM_XMIT_BUFFS )
			next_nop = 0;

		p->xmit_cmds[p->xmit_count]->cmd_status	= 0;
		/* linkpointer of xmit-command already points to next nop cmd */
		p->nop_cmds[next_nop]->cmd_link = make16((p->nop_cmds[next_nop]));
		p->nop_cmds[next_nop]->cmd_status = 0;

		p->nop_cmds[p->xmit_count]->cmd_link = make16((p->xmit_cmds[p->xmit_count]));
		dev->trans_start = jiffies;
		p->xmit_count = next_nop;

		{
			unsigned long flags;
			save_flags(flags);
			cli();
			if(p->xmit_count != p->xmit_last)
				netif_wake_queue(dev);
			p->lock = 0;
			restore_flags(flags);
		}
		dev_kfree_skb(skb);
#endif
	}
	return 0;
}

/*******************************************
 * Someone wanna have the statistics
 */

static struct net_device_stats *ni52_get_stats(struct net_device *dev)
{
	struct priv *p = (struct priv *) dev->priv;
	unsigned short crc,aln,rsc,ovrn;

	crc = p->scb->crc_errs; /* get error-statistic from the ni82586 */
	p->scb->crc_errs = 0;
	aln = p->scb->aln_errs;
	p->scb->aln_errs = 0;
	rsc = p->scb->rsc_errs;
	p->scb->rsc_errs = 0;
	ovrn = p->scb->ovrn_errs;
	p->scb->ovrn_errs = 0;

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

int init_module(void)
{
	if(io <= 0x0 || !memend || !memstart || irq < 2) {
		printk("ni52: Autoprobing not allowed for modules.\nni52: Set symbols 'io' 'irq' 'memstart' and 'memend'\n");
		return -ENODEV;
	}
	dev_ni52 = ni52_probe(-1);
	if (IS_ERR(dev_ni52))
		return PTR_ERR(dev_ni52);
	return 0;
}

void cleanup_module(void)
{
	unregister_netdev(dev_ni52);
	release_region(dev_ni52->base_addr, NI52_TOTAL_SIZE);
	free_netdev(dev_ni52);
}
#endif /* MODULE */

#if 0
/*
 * DUMP .. we expect a not running CMD unit and enough space
 */
void ni52_dump(struct net_device *dev,void *ptr)
{
	struct priv *p = (struct priv *) dev->priv;
	struct dump_cmd_struct *dump_cmd = (struct dump_cmd_struct *) ptr;
	int i;

	p->scb->cmd_cuc = CUC_ABORT;
	ni_attn586();
	WAIT_4_SCB_CMD();
	WAIT_4_SCB_CMD_RUC();

	dump_cmd->cmd_status = 0;
	dump_cmd->cmd_cmd = CMD_DUMP | CMD_LAST;
	dump_cmd->dump_offset = make16((dump_cmd + 1));
	dump_cmd->cmd_link = 0xffff;

	p->scb->cbl_offset = make16(dump_cmd);
	p->scb->cmd_cuc = CUC_START;
	ni_attn586();
	WAIT_4_STAT_COMPL(dump_cmd);

	if( (dump_cmd->cmd_status & (STAT_COMPL|STAT_OK)) != (STAT_COMPL|STAT_OK) )
				printk("%s: Can't get dump information.\n",dev->name);

	for(i=0;i<170;i++) {
		printk("%02x ",(int) ((unsigned char *) (dump_cmd + 1))[i]);
		if(i % 24 == 23)
			printk("\n");
	}
	printk("\n");
}
#endif
MODULE_LICENSE("GPL");

/*
 * END: linux/drivers/net/ni52.c
 */
