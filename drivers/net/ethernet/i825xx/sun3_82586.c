/*
 * Sun3 i82586 Ethernet driver
 *
 * Cloned from ni52.c for the Sun3 by Sam Creasey (sammy@sammy.net)
 *
 * Original copyright follows:
 * --------------------------
 *
 * net-3-driver for the NI5210 card (i82586 Ethernet chip)
 *
 * This is an extension to the Linux operating system, and is covered by the
 * same Gnu Public License that covers that work.
 *
 * Alphacode 0.82 (96/09/29) for Linux 2.0.0 (or later)
 * Copyrights (c) 1994,1995,1996 by M.Hipp (hippm@informatik.uni-tuebingen.de)
 * --------------------------
 *
 * Consult ni52.c for further notes from the original driver.
 *
 * This incarnation currently supports the OBIO version of the i82586 chip
 * used in certain sun3 models.  It should be fairly doable to expand this
 * to support VME if I should every acquire such a board.
 *
 */

static int debuglevel = 0; /* debug-printk 0: off 1: a few 2: more */
static int automatic_resume = 0; /* experimental .. better should be zero */
static int rfdadd = 0; /* rfdadd=1 may be better for 8K MEM cards */
static int fifo=0x8;	/* don't change */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <asm/io.h>
#include <asm/idprom.h>
#include <asm/machines.h>
#include <asm/sun3mmu.h>
#include <asm/dvma.h>
#include <asm/byteorder.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include "sun3_82586.h"

#define DRV_NAME "sun3_82586"

#define DEBUG       /* debug on */
#define SYSBUSVAL 0 /* 16 Bit */
#define SUN3_82586_TOTAL_SIZE	PAGE_SIZE

#define sun3_attn586()  {*(volatile unsigned char *)(dev->base_addr) |= IEOB_ATTEN; *(volatile unsigned char *)(dev->base_addr) &= ~IEOB_ATTEN;}
#define sun3_reset586() {*(volatile unsigned char *)(dev->base_addr) = 0; udelay(100); *(volatile unsigned char *)(dev->base_addr) = IEOB_NORSET;}
#define sun3_disint()   {*(volatile unsigned char *)(dev->base_addr) &= ~IEOB_IENAB;}
#define sun3_enaint()   {*(volatile unsigned char *)(dev->base_addr) |= IEOB_IENAB;}
#define sun3_active()   {*(volatile unsigned char *)(dev->base_addr) |= (IEOB_IENAB|IEOB_ONAIR|IEOB_NORSET);}

#define make32(ptr16) (p->memtop + (swab16((unsigned short) (ptr16))) )
#define make24(ptr32) (char *)swab32(( ((unsigned long) (ptr32)) - p->base))
#define make16(ptr32) (swab16((unsigned short) ((unsigned long)(ptr32) - (unsigned long) p->memtop )))

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

#define RECV_BUFF_SIZE 1536 /* slightly oversized */
#define XMIT_BUFF_SIZE 1536 /* slightly oversized */
#define NUM_XMIT_BUFFS 1    /* config for 32K shmem */
#define NUM_RECV_BUFFS_8 4 /* config for 32K shared mem */
#define NUM_RECV_BUFFS_16 9 /* config for 32K shared mem */
#define NUM_RECV_BUFFS_32 16 /* config for 32K shared mem */
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
       if(!p->reseted) { p->reseted = 1; sun3_reset586(); } } } }

#define WAIT_4_SCB_CMD_RUC() { int i; \
  for(i=0;i<16384;i++) { \
    if(!p->scb->cmd_ruc) break; \
    DELAY_18(); \
    if(i == 16383) { \
      printk("%s: scb_cmd (ruc) timed out: %04x,%04x .. disabling i82586!!\n",dev->name,p->scb->cmd_ruc,p->scb->rus); \
       if(!p->reseted) { p->reseted = 1; sun3_reset586(); } } } }

#define WAIT_4_STAT_COMPL(addr) { int i; \
   for(i=0;i<32767;i++) { \
     if(swab16((addr)->cmd_status) & STAT_COMPL) break; \
     DELAY_16(); DELAY_16(); } }

static int     sun3_82586_probe1(struct net_device *dev,int ioaddr);
static irqreturn_t sun3_82586_interrupt(int irq,void *dev_id);
static int     sun3_82586_open(struct net_device *dev);
static int     sun3_82586_close(struct net_device *dev);
static int     sun3_82586_send_packet(struct sk_buff *,struct net_device *);
static struct  net_device_stats *sun3_82586_get_stats(struct net_device *dev);
static void    set_multicast_list(struct net_device *dev);
static void    sun3_82586_timeout(struct net_device *dev);
#if 0
static void    sun3_82586_dump(struct net_device *,void *);
#endif

/* helper-functions */
static int     init586(struct net_device *dev);
static int     check586(struct net_device *dev,char *where,unsigned size);
static void    alloc586(struct net_device *dev);
static void    startrecv586(struct net_device *dev);
static void   *alloc_rfa(struct net_device *dev,void *ptr);
static void    sun3_82586_rcv_int(struct net_device *dev);
static void    sun3_82586_xmt_int(struct net_device *dev);
static void    sun3_82586_rnr_int(struct net_device *dev);

struct priv
{
	unsigned long base;
	char *memtop;
	long int lock;
	int reseted;
	volatile struct rfd_struct	*rfd_last,*rfd_top,*rfd_first;
	volatile struct scp_struct	*scp;	/* volatile is important */
	volatile struct iscp_struct	*iscp;	/* volatile is important */
	volatile struct scb_struct	*scb;	/* volatile is important */
	volatile struct tbd_struct	*xmit_buffs[NUM_XMIT_BUFFS];
	volatile struct transmit_cmd_struct *xmit_cmds[NUM_XMIT_BUFFS];
#if (NUM_XMIT_BUFFS == 1)
	volatile struct nop_cmd_struct *nop_cmds[2];
#else
	volatile struct nop_cmd_struct *nop_cmds[NUM_XMIT_BUFFS];
#endif
	volatile int		nop_point,num_recv_buffs;
	volatile char		*xmit_cbuffs[NUM_XMIT_BUFFS];
	volatile int		xmit_count,xmit_last;
};

/**********************************************
 * close device
 */
static int sun3_82586_close(struct net_device *dev)
{
	free_irq(dev->irq, dev);

	sun3_reset586(); /* the hard way to stop the receiver */

	netif_stop_queue(dev);

	return 0;
}

/**********************************************
 * open device
 */
static int sun3_82586_open(struct net_device *dev)
{
	int ret;

	sun3_disint();
	alloc586(dev);
	init586(dev);
	startrecv586(dev);
	sun3_enaint();

	ret = request_irq(dev->irq, sun3_82586_interrupt,0,dev->name,dev);
	if (ret)
	{
		sun3_reset586();
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
	struct priv *p = &pb;
	char *iscp_addr;
	int i;

	p->base = (unsigned long) dvma_btov(0);
	p->memtop = (char *)dvma_btov((unsigned long)where);
	p->scp = (struct scp_struct *)(p->base + SCP_DEFAULT_ADDRESS);
	memset((char *)p->scp,0, sizeof(struct scp_struct));
	for(i=0;i<sizeof(struct scp_struct);i++) /* memory was writeable? */
		if(((char *)p->scp)[i])
			return 0;
	p->scp->sysbus = SYSBUSVAL;				/* 1 = 8Bit-Bus, 0 = 16 Bit */
	if(p->scp->sysbus != SYSBUSVAL)
		return 0;

	iscp_addr = (char *)dvma_btov((unsigned long)where);

	p->iscp = (struct iscp_struct *) iscp_addr;
	memset((char *)p->iscp,0, sizeof(struct iscp_struct));

	p->scp->iscp = make24(p->iscp);
	p->iscp->busy = 1;

	sun3_reset586();
	sun3_attn586();
	DELAY(1);	/* wait a while... */

	if(p->iscp->busy) /* i82586 clears 'busy' after successful init */
		return 0;

	return 1;
}

/******************************************************************
 * set iscp at the right place, called by sun3_82586_probe1 and open586.
 */
static void alloc586(struct net_device *dev)
{
	struct priv *p = netdev_priv(dev);

	sun3_reset586();
	DELAY(1);

	p->scp	= (struct scp_struct *)	(p->base + SCP_DEFAULT_ADDRESS);
	p->iscp	= (struct iscp_struct *) dvma_btov(dev->mem_start);
	p->scb  = (struct scb_struct *)  ((char *)p->iscp + sizeof(struct iscp_struct));

	memset((char *) p->iscp,0,sizeof(struct iscp_struct));
	memset((char *) p->scp ,0,sizeof(struct scp_struct));

	p->scp->iscp = make24(p->iscp);
	p->scp->sysbus = SYSBUSVAL;
	p->iscp->scb_offset = make16(p->scb);
	p->iscp->scb_base = make24(dvma_btov(dev->mem_start));

	p->iscp->busy = 1;
	sun3_reset586();
	sun3_attn586();

	DELAY(1);

	if(p->iscp->busy)
		printk("%s: Init-Problems (alloc).\n",dev->name);

	p->reseted = 0;

	memset((char *)p->scb,0,sizeof(struct scb_struct));
}

struct net_device * __init sun3_82586_probe(int unit)
{
	struct net_device *dev;
	unsigned long ioaddr;
	static int found = 0;
	int err = -ENOMEM;

	/* check that this machine has an onboard 82586 */
	switch(idprom->id_machtype) {
	case SM_SUN3|SM_3_160:
	case SM_SUN3|SM_3_260:
		/* these machines have 82586 */
		break;

	default:
		return ERR_PTR(-ENODEV);
	}

	if (found)
		return ERR_PTR(-ENODEV);

	ioaddr = (unsigned long)ioremap(IE_OBIO, SUN3_82586_TOTAL_SIZE);
	if (!ioaddr)
		return ERR_PTR(-ENOMEM);
	found = 1;

	dev = alloc_etherdev(sizeof(struct priv));
	if (!dev)
		goto out;
	if (unit >= 0) {
		sprintf(dev->name, "eth%d", unit);
		netdev_boot_setup_check(dev);
	}

	dev->irq = IE_IRQ;
	dev->base_addr = ioaddr;
	err = sun3_82586_probe1(dev, ioaddr);
	if (err)
		goto out1;
	err = register_netdev(dev);
	if (err)
		goto out2;
	return dev;

out2:
	release_region(ioaddr, SUN3_82586_TOTAL_SIZE);
out1:
	free_netdev(dev);
out:
	iounmap((void __iomem *)ioaddr);
	return ERR_PTR(err);
}

static const struct net_device_ops sun3_82586_netdev_ops = {
	.ndo_open		= sun3_82586_open,
	.ndo_stop		= sun3_82586_close,
	.ndo_start_xmit		= sun3_82586_send_packet,
	.ndo_set_rx_mode	= set_multicast_list,
	.ndo_tx_timeout		= sun3_82586_timeout,
	.ndo_get_stats		= sun3_82586_get_stats,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_change_mtu		= eth_change_mtu,
};

static int __init sun3_82586_probe1(struct net_device *dev,int ioaddr)
{
	int i, size, retval;

	if (!request_region(ioaddr, SUN3_82586_TOTAL_SIZE, DRV_NAME))
		return -EBUSY;

	/* copy in the ethernet address from the prom */
	for(i = 0; i < 6 ; i++)
	     dev->dev_addr[i] = idprom->id_ethaddr[i];

	printk("%s: SUN3 Intel 82586 found at %lx, ",dev->name,dev->base_addr);

	/*
	 * check (or search) IO-Memory, 32K
	 */
	size = 0x8000;

	dev->mem_start = (unsigned long)dvma_malloc_align(0x8000, 0x1000);
	dev->mem_end = dev->mem_start + size;

	if(size != 0x2000 && size != 0x4000 && size != 0x8000) {
		printk("\n%s: Illegal memory size %d. Allowed is 0x2000 or 0x4000 or 0x8000 bytes.\n",dev->name,size);
		retval = -ENODEV;
		goto out;
	}
	if(!check586(dev,(char *) dev->mem_start,size)) {
		printk("?memcheck, Can't find memory at 0x%lx with size %d!\n",dev->mem_start,size);
		retval = -ENODEV;
		goto out;
	}

	((struct priv *)netdev_priv(dev))->memtop =
					(char *)dvma_btov(dev->mem_start);
	((struct priv *)netdev_priv(dev))->base = (unsigned long) dvma_btov(0);
	alloc586(dev);

	/* set number of receive-buffs according to memsize */
	if(size == 0x2000)
		((struct priv *)netdev_priv(dev))->num_recv_buffs =
							NUM_RECV_BUFFS_8;
	else if(size == 0x4000)
		((struct priv *)netdev_priv(dev))->num_recv_buffs =
							NUM_RECV_BUFFS_16;
	else
		((struct priv *)netdev_priv(dev))->num_recv_buffs =
							NUM_RECV_BUFFS_32;

	printk("Memaddr: 0x%lx, Memsize: %d, IRQ %d\n",dev->mem_start,size, dev->irq);

	dev->netdev_ops		= &sun3_82586_netdev_ops;
	dev->watchdog_timeo	= HZ/20;

	dev->if_port 		= 0;
	return 0;
out:
	release_region(ioaddr, SUN3_82586_TOTAL_SIZE);
	return retval;
}


static int init586(struct net_device *dev)
{
	void *ptr;
	int i,result=0;
	struct priv *p = netdev_priv(dev);
	volatile struct configure_cmd_struct	*cfg_cmd;
	volatile struct iasetup_cmd_struct *ias_cmd;
	volatile struct tdr_cmd_struct *tdr_cmd;
	volatile struct mcsetup_cmd_struct *mc_cmd;
	struct netdev_hw_addr *ha;
	int num_addrs=netdev_mc_count(dev);

	ptr = (void *) ((char *)p->scb + sizeof(struct scb_struct));

	cfg_cmd = (struct configure_cmd_struct *)ptr; /* configure-command */
	cfg_cmd->cmd_status	= 0;
	cfg_cmd->cmd_cmd	= swab16(CMD_CONFIGURE | CMD_LAST);
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
			cfg_cmd->promisc = 1;
		}
	}
	if(dev->flags&IFF_PROMISC)
		cfg_cmd->promisc = 1;
	cfg_cmd->carr_coll	= 0x00;

	p->scb->cbl_offset	= make16(cfg_cmd);
	p->scb->cmd_ruc		= 0;

	p->scb->cmd_cuc		= CUC_START; /* cmd.-unit start */
	sun3_attn586();

	WAIT_4_STAT_COMPL(cfg_cmd);

	if((swab16(cfg_cmd->cmd_status) & (STAT_OK|STAT_COMPL)) != (STAT_COMPL|STAT_OK))
	{
		printk("%s: configure command failed: %x\n",dev->name,swab16(cfg_cmd->cmd_status));
		return 1;
	}

	/*
	 * individual address setup
	 */

	ias_cmd = (struct iasetup_cmd_struct *)ptr;

	ias_cmd->cmd_status	= 0;
	ias_cmd->cmd_cmd	= swab16(CMD_IASETUP | CMD_LAST);
	ias_cmd->cmd_link	= 0xffff;

	memcpy((char *)&ias_cmd->iaddr,(char *) dev->dev_addr,ETH_ALEN);

	p->scb->cbl_offset = make16(ias_cmd);

	p->scb->cmd_cuc = CUC_START; /* cmd.-unit start */
	sun3_attn586();

	WAIT_4_STAT_COMPL(ias_cmd);

	if((swab16(ias_cmd->cmd_status) & (STAT_OK|STAT_COMPL)) != (STAT_OK|STAT_COMPL)) {
		printk("%s (82586): individual address setup command failed: %04x\n",dev->name,swab16(ias_cmd->cmd_status));
		return 1;
	}

	/*
	 * TDR, wire check .. e.g. no resistor e.t.c
	 */

	tdr_cmd = (struct tdr_cmd_struct *)ptr;

	tdr_cmd->cmd_status	= 0;
	tdr_cmd->cmd_cmd	= swab16(CMD_TDR | CMD_LAST);
	tdr_cmd->cmd_link	= 0xffff;
	tdr_cmd->status		= 0;

	p->scb->cbl_offset = make16(tdr_cmd);
	p->scb->cmd_cuc = CUC_START; /* cmd.-unit start */
	sun3_attn586();

	WAIT_4_STAT_COMPL(tdr_cmd);

	if(!(swab16(tdr_cmd->cmd_status) & STAT_COMPL))
	{
		printk("%s: Problems while running the TDR.\n",dev->name);
	}
	else
	{
		DELAY_16(); /* wait for result */
		result = swab16(tdr_cmd->status);

		p->scb->cmd_cuc = p->scb->cus & STAT_MASK;
		sun3_attn586(); /* ack the interrupts */

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
		mc_cmd->cmd_cmd = swab16(CMD_MCSETUP | CMD_LAST);
		mc_cmd->cmd_link = 0xffff;
		mc_cmd->mc_cnt = swab16(num_addrs * 6);

		i = 0;
		netdev_for_each_mc_addr(ha, dev)
			memcpy((char *) mc_cmd->mc_list[i++],
			       ha->addr, ETH_ALEN);

		p->scb->cbl_offset = make16(mc_cmd);
		p->scb->cmd_cuc = CUC_START;
		sun3_attn586();

		WAIT_4_STAT_COMPL(mc_cmd);

		if( (swab16(mc_cmd->cmd_status) & (STAT_COMPL|STAT_OK)) != (STAT_COMPL|STAT_OK) )
			printk("%s: Can't apply multicast-address-list.\n",dev->name);
	}

	/*
	 * alloc nop/xmit-cmds
	 */
#if (NUM_XMIT_BUFFS == 1)
	for(i=0;i<2;i++)
	{
		p->nop_cmds[i] 			= (struct nop_cmd_struct *)ptr;
		p->nop_cmds[i]->cmd_cmd		= swab16(CMD_NOP);
		p->nop_cmds[i]->cmd_status 	= 0;
		p->nop_cmds[i]->cmd_link	= make16((p->nop_cmds[i]));
		ptr = (char *) ptr + sizeof(struct nop_cmd_struct);
	}
#else
	for(i=0;i<NUM_XMIT_BUFFS;i++)
	{
		p->nop_cmds[i]			= (struct nop_cmd_struct *)ptr;
		p->nop_cmds[i]->cmd_cmd		= swab16(CMD_NOP);
		p->nop_cmds[i]->cmd_status	= 0;
		p->nop_cmds[i]->cmd_link	= make16((p->nop_cmds[i]));
		ptr = (char *) ptr + sizeof(struct nop_cmd_struct);
	}
#endif

	ptr = alloc_rfa(dev,ptr); /* init receive-frame-area */

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
		if(ptr > (void *)dev->mem_end)
		{
			printk("%s: not enough shared-mem for your configuration!\n",dev->name);
			return 1;
		}
		memset((char *)(p->xmit_cmds[i]) ,0, sizeof(struct transmit_cmd_struct));
		memset((char *)(p->xmit_buffs[i]),0, sizeof(struct tbd_struct));
		p->xmit_cmds[i]->cmd_link = make16(p->nop_cmds[(i+1)%NUM_XMIT_BUFFS]);
		p->xmit_cmds[i]->cmd_status = swab16(STAT_COMPL);
		p->xmit_cmds[i]->cmd_cmd = swab16(CMD_XMIT | CMD_INT);
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
	sun3_attn586();
	WAIT_4_SCB_CMD();
#else
	p->xmit_cmds[0]->cmd_link = make16(p->xmit_cmds[0]);
	p->xmit_cmds[0]->cmd_cmd	= swab16(CMD_XMIT | CMD_SUSPEND | CMD_INT);
#endif

	/*
	 * ack. interrupts
	 */
	p->scb->cmd_cuc = p->scb->cus & STAT_MASK;
	sun3_attn586();
	DELAY_16();

	sun3_enaint();
	sun3_active();

	return 0;
}

/******************************************************
 * This is a helper routine for sun3_82586_rnr_int() and init586().
 * It sets up the Receive Frame Area (RFA).
 */

static void *alloc_rfa(struct net_device *dev,void *ptr)
{
	volatile struct rfd_struct *rfd = (struct rfd_struct *)ptr;
	volatile struct rbd_struct *rbd;
	int i;
	struct priv *p = netdev_priv(dev);

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
		rbd[i].size = swab16(RECV_BUFF_SIZE);
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

static irqreturn_t sun3_82586_interrupt(int irq,void *dev_id)
{
	struct net_device *dev = dev_id;
	unsigned short stat;
	int cnt=0;
	struct priv *p;

	if (!dev) {
		printk ("sun3_82586-interrupt: irq %d for unknown device.\n",irq);
		return IRQ_NONE;
	}
	p = netdev_priv(dev);

	if(debuglevel > 1)
		printk("I");

	WAIT_4_SCB_CMD(); /* wait for last command	*/

	while((stat=p->scb->cus & STAT_MASK))
	{
		p->scb->cmd_cuc = stat;
		sun3_attn586();

		if(stat & STAT_FR)	 /* received a frame */
			sun3_82586_rcv_int(dev);

		if(stat & STAT_RNR) /* RU went 'not ready' */
		{
			printk("(R)");
			if(p->scb->rus & RU_SUSPEND) /* special case: RU_SUSPEND */
			{
				WAIT_4_SCB_CMD();
				p->scb->cmd_ruc = RUC_RESUME;
				sun3_attn586();
				WAIT_4_SCB_CMD_RUC();
			}
			else
			{
				printk("%s: Receiver-Unit went 'NOT READY': %04x/%02x.\n",dev->name,(int) stat,(int) p->scb->rus);
				sun3_82586_rnr_int(dev);
			}
		}

		if(stat & STAT_CX)		/* command with I-bit set complete */
			 sun3_82586_xmt_int(dev);

#ifndef NO_NOPCOMMANDS
		if(stat & STAT_CNA)	/* CU went 'not ready' */
		{
			if(netif_running(dev))
				printk("%s: oops! CU has left active state. stat: %04x/%02x.\n",dev->name,(int) stat,(int) p->scb->cus);
		}
#endif

		if(debuglevel > 1)
			printk("%d",cnt++);

		WAIT_4_SCB_CMD(); /* wait for ack. (sun3_82586_xmt_int can be faster than ack!!) */
		if(p->scb->cmd_cuc)	 /* timed out? */
		{
			printk("%s: Acknowledge timed out.\n",dev->name);
			sun3_disint();
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

static void sun3_82586_rcv_int(struct net_device *dev)
{
	int status,cnt=0;
	unsigned short totlen;
	struct sk_buff *skb;
	struct rbd_struct *rbd;
	struct priv *p = netdev_priv(dev);

	if(debuglevel > 0)
		printk("R");

	for(;(status = p->rfd_top->stat_high) & RFD_COMPL;)
	{
			rbd = (struct rbd_struct *) make32(p->rfd_top->rbd_offset);

			if(status & RFD_OK) /* frame received without error? */
			{
				if( (totlen = swab16(rbd->status)) & RBD_LAST) /* the first and the last buffer? */
				{
					totlen &= RBD_MASK; /* length of this frame */
					rbd->status = 0;
					skb = netdev_alloc_skb(dev, totlen + 2);
					if(skb != NULL)
					{
						skb_reserve(skb,2);
						skb_put(skb,totlen);
						skb_copy_to_linear_data(skb,(char *) p->base+swab32((unsigned long) rbd->buffer),totlen);
						skb->protocol=eth_type_trans(skb,dev);
						netif_rx(skb);
						dev->stats.rx_packets++;
					}
					else
						dev->stats.rx_dropped++;
				}
				else
				{
					int rstat;
						 /* free all RBD's until RBD_LAST is set */
					totlen = 0;
					while(!((rstat=swab16(rbd->status)) & RBD_LAST))
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
					dev->stats.rx_dropped++;
			 }
		}
		else /* frame !(ok), only with 'save-bad-frames' */
		{
			printk("%s: oops! rfd-error-status: %04x\n",dev->name,status);
			dev->stats.rx_errors++;
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
		sun3_attn586();
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

static void sun3_82586_rnr_int(struct net_device *dev)
{
	struct priv *p = netdev_priv(dev);

	dev->stats.rx_errors++;

	WAIT_4_SCB_CMD();		/* wait for the last cmd, WAIT_4_FULLSTAT?? */
	p->scb->cmd_ruc = RUC_ABORT; /* usually the RU is in the 'no resource'-state .. abort it now. */
	sun3_attn586();
	WAIT_4_SCB_CMD_RUC();		/* wait for accept cmd. */

	alloc_rfa(dev,(char *)p->rfd_first);
/* maybe add a check here, before restarting the RU */
	startrecv586(dev); /* restart RU */

	printk("%s: Receive-Unit restarted. Status: %04x\n",dev->name,p->scb->rus);

}

/**********************************************************
 * handle xmit - interrupt
 */

static void sun3_82586_xmt_int(struct net_device *dev)
{
	int status;
	struct priv *p = netdev_priv(dev);

	if(debuglevel > 0)
		printk("X");

	status = swab16(p->xmit_cmds[p->xmit_last]->cmd_status);
	if(!(status & STAT_COMPL))
		printk("%s: strange .. xmit-int without a 'COMPLETE'\n",dev->name);

	if(status & STAT_OK)
	{
		dev->stats.tx_packets++;
		dev->stats.collisions += (status & TCMD_MAXCOLLMASK);
	}
	else
	{
		dev->stats.tx_errors++;
		if(status & TCMD_LATECOLL) {
			printk("%s: late collision detected.\n",dev->name);
			dev->stats.collisions++;
		}
		else if(status & TCMD_NOCARRIER) {
			dev->stats.tx_carrier_errors++;
			printk("%s: no carrier detected.\n",dev->name);
		}
		else if(status & TCMD_LOSTCTS)
			printk("%s: loss of CTS detected.\n",dev->name);
		else if(status & TCMD_UNDERRUN) {
			dev->stats.tx_fifo_errors++;
			printk("%s: DMA underrun detected.\n",dev->name);
		}
		else if(status & TCMD_MAXCOLL) {
			printk("%s: Max. collisions exceeded.\n",dev->name);
			dev->stats.collisions += 16;
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
	struct priv *p = netdev_priv(dev);

	WAIT_4_SCB_CMD();
	WAIT_4_SCB_CMD_RUC();
	p->scb->rfa_offset = make16(p->rfd_first);
	p->scb->cmd_ruc = RUC_START;
	sun3_attn586();		/* start cmd. */
	WAIT_4_SCB_CMD_RUC();	/* wait for accept cmd. (no timeout!!) */
}

static void sun3_82586_timeout(struct net_device *dev)
{
	struct priv *p = netdev_priv(dev);
#ifndef NO_NOPCOMMANDS
	if(p->scb->cus & CU_ACTIVE) /* COMMAND-UNIT active? */
	{
		netif_wake_queue(dev);
#ifdef DEBUG
		printk("%s: strange ... timeout with CU active?!?\n",dev->name);
		printk("%s: X0: %04x N0: %04x N1: %04x %d\n",dev->name,(int)swab16(p->xmit_cmds[0]->cmd_status),(int)swab16(p->nop_cmds[0]->cmd_status),(int)swab16(p->nop_cmds[1]->cmd_status),(int)p->nop_point);
#endif
		p->scb->cmd_cuc = CUC_ABORT;
		sun3_attn586();
		WAIT_4_SCB_CMD();
		p->scb->cbl_offset = make16(p->nop_cmds[p->nop_point]);
		p->scb->cmd_cuc = CUC_START;
		sun3_attn586();
		WAIT_4_SCB_CMD();
		dev->trans_start = jiffies; /* prevent tx timeout */
		return 0;
	}
#endif
	{
#ifdef DEBUG
		printk("%s: xmitter timed out, try to restart! stat: %02x\n",dev->name,p->scb->cus);
		printk("%s: command-stats: %04x %04x\n",dev->name,swab16(p->xmit_cmds[0]->cmd_status),swab16(p->xmit_cmds[1]->cmd_status));
		printk("%s: check, whether you set the right interrupt number!\n",dev->name);
#endif
		sun3_82586_close(dev);
		sun3_82586_open(dev);
	}
	dev->trans_start = jiffies; /* prevent tx timeout */
}

/******************************************************
 * send frame
 */

static int sun3_82586_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	int len,i;
#ifndef NO_NOPCOMMANDS
	int next_nop;
#endif
	struct priv *p = netdev_priv(dev);

	if(skb->len > XMIT_BUFF_SIZE)
	{
		printk("%s: Sorry, max. framelength is %d bytes. The length of your frame is %d bytes.\n",dev->name,XMIT_BUFF_SIZE,skb->len);
		return NETDEV_TX_OK;
	}

	netif_stop_queue(dev);

#if(NUM_XMIT_BUFFS > 1)
	if(test_and_set_bit(0,(void *) &p->lock)) {
		printk("%s: Queue was locked\n",dev->name);
		return NETDEV_TX_BUSY;
	}
	else
#endif
	{
		len = skb->len;
		if (len < ETH_ZLEN) {
			memset((void *)p->xmit_cbuffs[p->xmit_count], 0,
			       ETH_ZLEN);
			len = ETH_ZLEN;
		}
		skb_copy_from_linear_data(skb, (void *)p->xmit_cbuffs[p->xmit_count], skb->len);

#if (NUM_XMIT_BUFFS == 1)
#	ifdef NO_NOPCOMMANDS

#ifdef DEBUG
		if(p->scb->cus & CU_ACTIVE)
		{
			printk("%s: Hmmm .. CU is still running and we wanna send a new packet.\n",dev->name);
			printk("%s: stat: %04x %04x\n",dev->name,p->scb->cus,swab16(p->xmit_cmds[0]->cmd_status));
		}
#endif

		p->xmit_buffs[0]->size = swab16(TBD_LAST | len);
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

			sun3_attn586();
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
		p->xmit_buffs[0]->size = swab16(TBD_LAST | len);

		p->xmit_cmds[0]->cmd_link	 = p->nop_cmds[next_nop]->cmd_link
			= make16((p->nop_cmds[next_nop]));
		p->xmit_cmds[0]->cmd_status = p->nop_cmds[next_nop]->cmd_status = 0;

		p->nop_cmds[p->nop_point]->cmd_link = make16((p->xmit_cmds[0]));
		p->nop_point = next_nop;
		dev_kfree_skb(skb);
#	endif
#else
		p->xmit_buffs[p->xmit_count]->size = swab16(TBD_LAST | len);
		if( (next_nop = p->xmit_count + 1) == NUM_XMIT_BUFFS )
			next_nop = 0;

		p->xmit_cmds[p->xmit_count]->cmd_status	= 0;
		/* linkpointer of xmit-command already points to next nop cmd */
		p->nop_cmds[next_nop]->cmd_link = make16((p->nop_cmds[next_nop]));
		p->nop_cmds[next_nop]->cmd_status = 0;

		p->nop_cmds[p->xmit_count]->cmd_link = make16((p->xmit_cmds[p->xmit_count]));
		p->xmit_count = next_nop;

		{
			unsigned long flags;
			local_irq_save(flags);
			if(p->xmit_count != p->xmit_last)
				netif_wake_queue(dev);
			p->lock = 0;
			local_irq_restore(flags);
		}
		dev_kfree_skb(skb);
#endif
	}
	return NETDEV_TX_OK;
}

/*******************************************
 * Someone wanna have the statistics
 */

static struct net_device_stats *sun3_82586_get_stats(struct net_device *dev)
{
	struct priv *p = netdev_priv(dev);
	unsigned short crc,aln,rsc,ovrn;

	crc = swab16(p->scb->crc_errs); /* get error-statistic from the ni82586 */
	p->scb->crc_errs = 0;
	aln = swab16(p->scb->aln_errs);
	p->scb->aln_errs = 0;
	rsc = swab16(p->scb->rsc_errs);
	p->scb->rsc_errs = 0;
	ovrn = swab16(p->scb->ovrn_errs);
	p->scb->ovrn_errs = 0;

	dev->stats.rx_crc_errors += crc;
	dev->stats.rx_fifo_errors += ovrn;
	dev->stats.rx_frame_errors += aln;
	dev->stats.rx_dropped += rsc;

	return &dev->stats;
}

/********************************************************
 * Set MC list ..
 */

static void set_multicast_list(struct net_device *dev)
{
	netif_stop_queue(dev);
	sun3_disint();
	alloc586(dev);
	init586(dev);
	startrecv586(dev);
	sun3_enaint();
	netif_wake_queue(dev);
}

#if 0
/*
 * DUMP .. we expect a not running CMD unit and enough space
 */
void sun3_82586_dump(struct net_device *dev,void *ptr)
{
	struct priv *p = netdev_priv(dev);
	struct dump_cmd_struct *dump_cmd = (struct dump_cmd_struct *) ptr;
	int i;

	p->scb->cmd_cuc = CUC_ABORT;
	sun3_attn586();
	WAIT_4_SCB_CMD();
	WAIT_4_SCB_CMD_RUC();

	dump_cmd->cmd_status = 0;
	dump_cmd->cmd_cmd = CMD_DUMP | CMD_LAST;
	dump_cmd->dump_offset = make16((dump_cmd + 1));
	dump_cmd->cmd_link = 0xffff;

	p->scb->cbl_offset = make16(dump_cmd);
	p->scb->cmd_cuc = CUC_START;
	sun3_attn586();
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
