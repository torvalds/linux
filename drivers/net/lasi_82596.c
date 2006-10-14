/* lasi_82596.c -- driver for the intel 82596 ethernet controller, as
   munged into HPPA boxen .

   This driver is based upon 82596.c, original credits are below...
   but there were too many hoops which HP wants jumped through to
   keep this code in there in a sane manner.

   3 primary sources of the mess --
   1) hppa needs *lots* of cacheline flushing to keep this kind of
   MMIO running.

   2) The 82596 needs to see all of its pointers as their physical
   address.  Thus virt_to_bus/bus_to_virt are *everywhere*.

   3) The implementation HP is using seems to be significantly pickier
   about when and how the command and RX units are started.  some
   command ordering was changed.

   Examination of the mach driver leads one to believe that there
   might be a saner way to pull this off...  anyone who feels like a
   full rewrite can be my guest.

   Split 02/13/2000 Sam Creasey (sammy@oh.verio.com)

   02/01/2000  Initial modifications for parisc by Helge Deller (deller@gmx.de)
   03/02/2000  changes for better/correct(?) cache-flushing (deller)
*/

/* 82596.c: A generic 82596 ethernet driver for linux. */
/*
   Based on Apricot.c
   Written 1994 by Mark Evans.
   This driver is for the Apricot 82596 bus-master interface

   Modularised 12/94 Mark Evans


   Modified to support the 82596 ethernet chips on 680x0 VME boards.
   by Richard Hirst <richard@sleepie.demon.co.uk>
   Renamed to be 82596.c

   980825:  Changed to receive directly in to sk_buffs which are
   allocated at open() time.  Eliminates copy on incoming frames
   (small ones are still copied).  Shared data now held in a
   non-cached page, so we can run on 68060 in copyback mode.

   TBD:
   * look at deferring rx frames rather than discarding (as per tulip)
   * handle tx ring full as per tulip
   * performace test to tune rx_copybreak

   Most of my modifications relate to the braindead big-endian
   implementation by Intel.  When the i596 is operating in
   'big-endian' mode, it thinks a 32 bit value of 0x12345678
   should be stored as 0x56781234.  This is a real pain, when
   you have linked lists which are shared by the 680x0 and the
   i596.

   Driver skeleton
   Written 1993 by Donald Becker.
   Copyright 1993 United States Government as represented by the Director,
   National Security Agency. This software may only be used and distributed
   according to the terms of the GNU General Public License as modified by SRC,
   incorporated herein by reference.

   The author may be reached as becker@scyld.com, or C/O
   Scyld Computing Corporation, 410 Severn Ave., Suite 210, Annapolis MD 21403

 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/pdc.h>
#include <asm/cache.h>
#include <asm/parisc-device.h>

#define LASI_82596_DRIVER_VERSION "LASI 82596 driver - Revision: 1.30"

/* DEBUG flags
 */

#define DEB_INIT	0x0001
#define DEB_PROBE	0x0002
#define DEB_SERIOUS	0x0004
#define DEB_ERRORS	0x0008
#define DEB_MULTI	0x0010
#define DEB_TDR		0x0020
#define DEB_OPEN	0x0040
#define DEB_RESET	0x0080
#define DEB_ADDCMD	0x0100
#define DEB_STATUS	0x0200
#define DEB_STARTTX	0x0400
#define DEB_RXADDR	0x0800
#define DEB_TXADDR	0x1000
#define DEB_RXFRAME	0x2000
#define DEB_INTS	0x4000
#define DEB_STRUCT	0x8000
#define DEB_ANY		0xffff


#define DEB(x,y)	if (i596_debug & (x)) { y; }


#define  CHECK_WBACK(priv, addr,len) \
	do { dma_cache_sync((priv)->dev, (void *)addr, len, DMA_TO_DEVICE); } while (0)

#define  CHECK_INV(priv, addr,len) \
	do { dma_cache_sync((priv)->dev, (void *)addr, len, DMA_FROM_DEVICE); } while(0)

#define  CHECK_WBACK_INV(priv, addr,len) \
	do { dma_cache_sync((priv)->dev, (void *)addr, len, DMA_BIDIRECTIONAL); } while (0)


#define PA_I82596_RESET		0	/* Offsets relative to LASI-LAN-Addr.*/
#define PA_CPU_PORT_L_ACCESS	4
#define PA_CHANNEL_ATTENTION	8


/*
 * Define various macros for Channel Attention, word swapping etc., dependent
 * on architecture.  MVME and BVME are 680x0 based, otherwise it is Intel.
 */

#ifdef __BIG_ENDIAN
#define WSWAPrfd(x)  (((u32)(x)<<16) | ((((u32)(x)))>>16))
#define WSWAPrbd(x)  (((u32)(x)<<16) | ((((u32)(x)))>>16))
#define WSWAPiscp(x) (((u32)(x)<<16) | ((((u32)(x)))>>16))
#define WSWAPscb(x)  (((u32)(x)<<16) | ((((u32)(x)))>>16))
#define WSWAPcmd(x)  (((u32)(x)<<16) | ((((u32)(x)))>>16))
#define WSWAPtbd(x)  (((u32)(x)<<16) | ((((u32)(x)))>>16))
#define WSWAPchar(x) (((u32)(x)<<16) | ((((u32)(x)))>>16))
#define ISCP_BUSY	0x00010000
#define MACH_IS_APRICOT	0
#else
#define WSWAPrfd(x)     ((struct i596_rfd *)(x))
#define WSWAPrbd(x)     ((struct i596_rbd *)(x))
#define WSWAPiscp(x)    ((struct i596_iscp *)(x))
#define WSWAPscb(x)     ((struct i596_scb *)(x))
#define WSWAPcmd(x)     ((struct i596_cmd *)(x))
#define WSWAPtbd(x)     ((struct i596_tbd *)(x))
#define WSWAPchar(x)    ((char *)(x))
#define ISCP_BUSY	0x0001
#define MACH_IS_APRICOT	1
#endif

/*
 * The MPU_PORT command allows direct access to the 82596. With PORT access
 * the following commands are available (p5-18). The 32-bit port command
 * must be word-swapped with the most significant word written first.
 * This only applies to VME boards.
 */
#define PORT_RESET		0x00	/* reset 82596 */
#define PORT_SELFTEST		0x01	/* selftest */
#define PORT_ALTSCP		0x02	/* alternate SCB address */
#define PORT_ALTDUMP		0x03	/* Alternate DUMP address */

static int i596_debug = (DEB_SERIOUS|DEB_PROBE);

MODULE_AUTHOR("Richard Hirst");
MODULE_DESCRIPTION("i82596 driver");
MODULE_LICENSE("GPL");
module_param(i596_debug, int, 0);
MODULE_PARM_DESC(i596_debug, "lasi_82596 debug mask");

/* Copy frames shorter than rx_copybreak, otherwise pass on up in
 * a full sized sk_buff.  Value of 100 stolen from tulip.c (!alpha).
 */
static int rx_copybreak = 100;

#define MAX_DRIVERS	4	/* max count of drivers */

#define PKT_BUF_SZ	1536
#define MAX_MC_CNT	64

#define I596_NULL ((u32)0xffffffff)

#define CMD_EOL		0x8000	/* The last command of the list, stop. */
#define CMD_SUSP	0x4000	/* Suspend after doing cmd. */
#define CMD_INTR	0x2000	/* Interrupt after doing cmd. */

#define CMD_FLEX	0x0008	/* Enable flexible memory model */

enum commands {
	CmdNOp = 0, CmdSASetup = 1, CmdConfigure = 2, CmdMulticastList = 3,
	CmdTx = 4, CmdTDR = 5, CmdDump = 6, CmdDiagnose = 7
};

#define STAT_C		0x8000	/* Set to 0 after execution */
#define STAT_B		0x4000	/* Command being executed */
#define STAT_OK		0x2000	/* Command executed ok */
#define STAT_A		0x1000	/* Command aborted */

#define	 CUC_START	0x0100
#define	 CUC_RESUME	0x0200
#define	 CUC_SUSPEND    0x0300
#define	 CUC_ABORT	0x0400
#define	 RX_START	0x0010
#define	 RX_RESUME	0x0020
#define	 RX_SUSPEND	0x0030
#define	 RX_ABORT	0x0040

#define TX_TIMEOUT	5

#define OPT_SWAP_PORT	0x0001	/* Need to wordswp on the MPU port */


struct i596_reg {
	unsigned short porthi;
	unsigned short portlo;
	u32            ca;
};

#define EOF		0x8000
#define SIZE_MASK	0x3fff

struct i596_tbd {
	unsigned short size;
	unsigned short pad;
	dma_addr_t     next;
	dma_addr_t     data;
	u32 cache_pad[5];		/* Total 32 bytes... */
};

/* The command structure has two 'next' pointers; v_next is the address of
 * the next command as seen by the CPU, b_next is the address of the next
 * command as seen by the 82596.  The b_next pointer, as used by the 82596
 * always references the status field of the next command, rather than the
 * v_next field, because the 82596 is unaware of v_next.  It may seem more
 * logical to put v_next at the end of the structure, but we cannot do that
 * because the 82596 expects other fields to be there, depending on command
 * type.
 */

struct i596_cmd {
	struct i596_cmd *v_next;	/* Address from CPUs viewpoint */
	unsigned short status;
	unsigned short command;
	dma_addr_t     b_next;	/* Address from i596 viewpoint */
};

struct tx_cmd {
	struct i596_cmd cmd;
	dma_addr_t     tbd;
	unsigned short size;
	unsigned short pad;
	struct sk_buff *skb;		/* So we can free it after tx */
	dma_addr_t dma_addr;
#ifdef __LP64__
	u32 cache_pad[6];		/* Total 64 bytes... */
#else
	u32 cache_pad[1];		/* Total 32 bytes... */
#endif
};

struct tdr_cmd {
	struct i596_cmd cmd;
	unsigned short status;
	unsigned short pad;
};

struct mc_cmd {
	struct i596_cmd cmd;
	short mc_cnt;
	char mc_addrs[MAX_MC_CNT*6];
};

struct sa_cmd {
	struct i596_cmd cmd;
	char eth_addr[8];
};

struct cf_cmd {
	struct i596_cmd cmd;
	char i596_config[16];
};

struct i596_rfd {
	unsigned short stat;
	unsigned short cmd;
	dma_addr_t     b_next;	/* Address from i596 viewpoint */
	dma_addr_t     rbd;
	unsigned short count;
	unsigned short size;
	struct i596_rfd *v_next;	/* Address from CPUs viewpoint */
	struct i596_rfd *v_prev;
#ifndef __LP64__
	u32 cache_pad[2];		/* Total 32 bytes... */
#endif
};

struct i596_rbd {
    /* hardware data */
    unsigned short count;
    unsigned short zero1;
    dma_addr_t     b_next;
    dma_addr_t     b_data;		/* Address from i596 viewpoint */
    unsigned short size;
    unsigned short zero2;
    /* driver data */
    struct sk_buff *skb;
    struct i596_rbd *v_next;
    dma_addr_t     b_addr;		/* This rbd addr from i596 view */
    unsigned char *v_data;		/* Address from CPUs viewpoint */
					/* Total 32 bytes... */
#ifdef __LP64__
    u32 cache_pad[4];
#endif
};

/* These values as chosen so struct i596_private fits in one page... */

#define TX_RING_SIZE 32
#define RX_RING_SIZE 16

struct i596_scb {
	unsigned short status;
	unsigned short command;
	dma_addr_t    cmd;
	dma_addr_t    rfd;
	u32           crc_err;
	u32           align_err;
	u32           resource_err;
	u32           over_err;
	u32           rcvdt_err;
	u32           short_err;
	unsigned short t_on;
	unsigned short t_off;
};

struct i596_iscp {
	u32           stat;
	dma_addr_t    scb;
};

struct i596_scp {
	u32           sysbus;
	u32            pad;
	dma_addr_t    iscp;
};

struct i596_private {
	volatile struct i596_scp scp		__attribute__((aligned(32)));
	volatile struct i596_iscp iscp		__attribute__((aligned(32)));
	volatile struct i596_scb scb		__attribute__((aligned(32)));
	struct sa_cmd sa_cmd			__attribute__((aligned(32)));
	struct cf_cmd cf_cmd			__attribute__((aligned(32)));
	struct tdr_cmd tdr_cmd			__attribute__((aligned(32)));
	struct mc_cmd mc_cmd			__attribute__((aligned(32)));
	struct i596_rfd rfds[RX_RING_SIZE]	__attribute__((aligned(32)));
	struct i596_rbd rbds[RX_RING_SIZE]	__attribute__((aligned(32)));
	struct tx_cmd tx_cmds[TX_RING_SIZE]	__attribute__((aligned(32)));
	struct i596_tbd tbds[TX_RING_SIZE]	__attribute__((aligned(32)));
	u32    stat;
	int last_restart;
	struct i596_rfd *rfd_head;
	struct i596_rbd *rbd_head;
	struct i596_cmd *cmd_tail;
	struct i596_cmd *cmd_head;
	int cmd_backlog;
	u32    last_cmd;
	struct net_device_stats stats;
	int next_tx_cmd;
	int options;
	spinlock_t lock;
	dma_addr_t dma_addr;
	struct device *dev;
};

static const char init_setup[] =
{
	0x8E,			/* length, prefetch on */
	0xC8,			/* fifo to 8, monitor off */
	0x80,			/* don't save bad frames */
	0x2E,			/* No source address insertion, 8 byte preamble */
	0x00,			/* priority and backoff defaults */
	0x60,			/* interframe spacing */
	0x00,			/* slot time LSB */
	0xf2,			/* slot time and retries */
	0x00,			/* promiscuous mode */
	0x00,			/* collision detect */
	0x40,			/* minimum frame length */
	0xff,
	0x00,
	0x7f /*  *multi IA */ };

static int i596_open(struct net_device *dev);
static int i596_start_xmit(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t i596_interrupt(int irq, void *dev_id);
static int i596_close(struct net_device *dev);
static struct net_device_stats *i596_get_stats(struct net_device *dev);
static void i596_add_cmd(struct net_device *dev, struct i596_cmd *cmd);
static void i596_tx_timeout (struct net_device *dev);
static void print_eth(unsigned char *buf, char *str);
static void set_multicast_list(struct net_device *dev);

static int rx_ring_size = RX_RING_SIZE;
static int ticks_limit = 100;
static int max_cmd_backlog = TX_RING_SIZE-1;

#ifdef CONFIG_NET_POLL_CONTROLLER
static void i596_poll_controller(struct net_device *dev);
#endif


static inline void CA(struct net_device *dev)
{
	gsc_writel(0, dev->base_addr + PA_CHANNEL_ATTENTION);
}


static inline void MPU_PORT(struct net_device *dev, int c, dma_addr_t x)
{
	struct i596_private *lp = dev->priv;

	u32 v = (u32) (c) | (u32) (x);
	u16 a, b;

	if (lp->options & OPT_SWAP_PORT) {
		a = v >> 16;
		b = v & 0xffff;
	} else {
		a = v & 0xffff;
		b = v >> 16;
	}

	gsc_writel(a, dev->base_addr + PA_CPU_PORT_L_ACCESS);
	udelay(1);
	gsc_writel(b, dev->base_addr + PA_CPU_PORT_L_ACCESS);
}


static inline int wait_istat(struct net_device *dev, struct i596_private *lp, int delcnt, char *str)
{
	CHECK_INV(lp, &(lp->iscp), sizeof(struct i596_iscp));
	while (--delcnt && lp->iscp.stat) {
		udelay(10);
		CHECK_INV(lp, &(lp->iscp), sizeof(struct i596_iscp));
	}
	if (!delcnt) {
		printk("%s: %s, iscp.stat %04x, didn't clear\n",
		     dev->name, str, lp->iscp.stat);
		return -1;
	}
	else
		return 0;
}


static inline int wait_cmd(struct net_device *dev, struct i596_private *lp, int delcnt, char *str)
{
	CHECK_INV(lp, &(lp->scb), sizeof(struct i596_scb));
	while (--delcnt && lp->scb.command) {
		udelay(10);
		CHECK_INV(lp, &(lp->scb), sizeof(struct i596_scb));
	}
	if (!delcnt) {
		printk("%s: %s, status %4.4x, cmd %4.4x.\n",
		     dev->name, str, lp->scb.status, lp->scb.command);
		return -1;
	}
	else
		return 0;
}


static void i596_display_data(struct net_device *dev)
{
	struct i596_private *lp = dev->priv;
	struct i596_cmd *cmd;
	struct i596_rfd *rfd;
	struct i596_rbd *rbd;

	printk("lp and scp at %p, .sysbus = %08x, .iscp = %08x\n",
	       &lp->scp, lp->scp.sysbus, lp->scp.iscp);
	printk("iscp at %p, iscp.stat = %08x, .scb = %08x\n",
	       &lp->iscp, lp->iscp.stat, lp->iscp.scb);
	printk("scb at %p, scb.status = %04x, .command = %04x,"
		" .cmd = %08x, .rfd = %08x\n",
	       &lp->scb, lp->scb.status, lp->scb.command,
		lp->scb.cmd, lp->scb.rfd);
	printk("   errors: crc %x, align %x, resource %x,"
               " over %x, rcvdt %x, short %x\n",
		lp->scb.crc_err, lp->scb.align_err, lp->scb.resource_err,
		lp->scb.over_err, lp->scb.rcvdt_err, lp->scb.short_err);
	cmd = lp->cmd_head;
	while (cmd != NULL) {
		printk("cmd at %p, .status = %04x, .command = %04x, .b_next = %08x\n",
		  cmd, cmd->status, cmd->command, cmd->b_next);
		cmd = cmd->v_next;
	}
	rfd = lp->rfd_head;
	printk("rfd_head = %p\n", rfd);
	do {
		printk("   %p .stat %04x, .cmd %04x, b_next %08x, rbd %08x,"
                        " count %04x\n",
			rfd, rfd->stat, rfd->cmd, rfd->b_next, rfd->rbd,
			rfd->count);
		rfd = rfd->v_next;
	} while (rfd != lp->rfd_head);
	rbd = lp->rbd_head;
	printk("rbd_head = %p\n", rbd);
	do {
		printk("   %p .count %04x, b_next %08x, b_data %08x, size %04x\n",
			rbd, rbd->count, rbd->b_next, rbd->b_data, rbd->size);
		rbd = rbd->v_next;
	} while (rbd != lp->rbd_head);
	CHECK_INV(lp, lp, sizeof(struct i596_private));
}


#if defined(ENABLE_MVME16x_NET) || defined(ENABLE_BVME6000_NET)
static void i596_error(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	volatile unsigned char *pcc2 = (unsigned char *) 0xfff42000;

	pcc2[0x28] = 1;
	pcc2[0x2b] = 0x1d;
	printk("%s: Error interrupt\n", dev->name);
	i596_display_data(dev);
}
#endif

#define virt_to_dma(lp,v) ((lp)->dma_addr + (dma_addr_t)((unsigned long)(v)-(unsigned long)(lp)))

static inline void init_rx_bufs(struct net_device *dev)
{
	struct i596_private *lp = dev->priv;
	int i;
	struct i596_rfd *rfd;
	struct i596_rbd *rbd;

	/* First build the Receive Buffer Descriptor List */

	for (i = 0, rbd = lp->rbds; i < rx_ring_size; i++, rbd++) {
		dma_addr_t dma_addr;
		struct sk_buff *skb = dev_alloc_skb(PKT_BUF_SZ + 4);

		if (skb == NULL)
			panic("%s: alloc_skb() failed", __FILE__);
		skb_reserve(skb, 2);
		dma_addr = dma_map_single(lp->dev, skb->data,PKT_BUF_SZ,
					  DMA_FROM_DEVICE);
		skb->dev = dev;
		rbd->v_next = rbd+1;
		rbd->b_next = WSWAPrbd(virt_to_dma(lp,rbd+1));
		rbd->b_addr = WSWAPrbd(virt_to_dma(lp,rbd));
		rbd->skb = skb;
		rbd->v_data = skb->data;
		rbd->b_data = WSWAPchar(dma_addr);
		rbd->size = PKT_BUF_SZ;
	}
	lp->rbd_head = lp->rbds;
	rbd = lp->rbds + rx_ring_size - 1;
	rbd->v_next = lp->rbds;
	rbd->b_next = WSWAPrbd(virt_to_dma(lp,lp->rbds));

	/* Now build the Receive Frame Descriptor List */

	for (i = 0, rfd = lp->rfds; i < rx_ring_size; i++, rfd++) {
		rfd->rbd = I596_NULL;
		rfd->v_next = rfd+1;
		rfd->v_prev = rfd-1;
		rfd->b_next = WSWAPrfd(virt_to_dma(lp,rfd+1));
		rfd->cmd = CMD_FLEX;
	}
	lp->rfd_head = lp->rfds;
	lp->scb.rfd = WSWAPrfd(virt_to_dma(lp,lp->rfds));
	rfd = lp->rfds;
	rfd->rbd = WSWAPrbd(virt_to_dma(lp,lp->rbd_head));
	rfd->v_prev = lp->rfds + rx_ring_size - 1;
	rfd = lp->rfds + rx_ring_size - 1;
	rfd->v_next = lp->rfds;
	rfd->b_next = WSWAPrfd(virt_to_dma(lp,lp->rfds));
	rfd->cmd = CMD_EOL|CMD_FLEX;

	CHECK_WBACK_INV(lp, lp, sizeof(struct i596_private));
}

static inline void remove_rx_bufs(struct net_device *dev)
{
	struct i596_private *lp = dev->priv;
	struct i596_rbd *rbd;
	int i;

	for (i = 0, rbd = lp->rbds; i < rx_ring_size; i++, rbd++) {
		if (rbd->skb == NULL)
			break;
		dma_unmap_single(lp->dev,
				 (dma_addr_t)WSWAPchar(rbd->b_data),
				 PKT_BUF_SZ, DMA_FROM_DEVICE);
		dev_kfree_skb(rbd->skb);
	}
}


static void rebuild_rx_bufs(struct net_device *dev)
{
	struct i596_private *lp = dev->priv;
	int i;

	/* Ensure rx frame/buffer descriptors are tidy */

	for (i = 0; i < rx_ring_size; i++) {
		lp->rfds[i].rbd = I596_NULL;
		lp->rfds[i].cmd = CMD_FLEX;
	}
	lp->rfds[rx_ring_size-1].cmd = CMD_EOL|CMD_FLEX;
	lp->rfd_head = lp->rfds;
	lp->scb.rfd = WSWAPrfd(virt_to_dma(lp,lp->rfds));
	lp->rbd_head = lp->rbds;
	lp->rfds[0].rbd = WSWAPrbd(virt_to_dma(lp,lp->rbds));

	CHECK_WBACK_INV(lp, lp, sizeof(struct i596_private));
}


static int init_i596_mem(struct net_device *dev)
{
	struct i596_private *lp = dev->priv;
	unsigned long flags;

	disable_irq(dev->irq);	/* disable IRQs from LAN */
	DEB(DEB_INIT,
		printk("RESET 82596 port: %lx (with IRQ %d disabled)\n",
		       (dev->base_addr + PA_I82596_RESET),
		       dev->irq));

	gsc_writel(0, (dev->base_addr + PA_I82596_RESET)); /* Hard Reset */
	udelay(100);			/* Wait 100us - seems to help */

	/* change the scp address */

	lp->last_cmd = jiffies;


	lp->scp.sysbus = 0x0000006c;
	lp->scp.iscp = WSWAPiscp(virt_to_dma(lp,&(lp->iscp)));
	lp->iscp.scb = WSWAPscb(virt_to_dma(lp,&(lp->scb)));
	lp->iscp.stat = ISCP_BUSY;
	lp->cmd_backlog = 0;

	lp->cmd_head = NULL;
        lp->scb.cmd = I596_NULL;

	DEB(DEB_INIT, printk("%s: starting i82596.\n", dev->name));

	CHECK_WBACK(lp, &(lp->scp), sizeof(struct i596_scp));
	CHECK_WBACK(lp, &(lp->iscp), sizeof(struct i596_iscp));

	MPU_PORT(dev, PORT_ALTSCP, virt_to_dma(lp,&lp->scp));

	CA(dev);

	if (wait_istat(dev, lp, 1000, "initialization timed out"))
		goto failed;
	DEB(DEB_INIT, printk("%s: i82596 initialization successful\n", dev->name));

	/* Ensure rx frame/buffer descriptors are tidy */
	rebuild_rx_bufs(dev);

	lp->scb.command = 0;
	CHECK_WBACK(lp, &(lp->scb), sizeof(struct i596_scb));

	enable_irq(dev->irq);	/* enable IRQs from LAN */

	DEB(DEB_INIT, printk("%s: queuing CmdConfigure\n", dev->name));
	memcpy(lp->cf_cmd.i596_config, init_setup, sizeof(init_setup));
	lp->cf_cmd.cmd.command = CmdConfigure;
	CHECK_WBACK(lp, &(lp->cf_cmd), sizeof(struct cf_cmd));
	i596_add_cmd(dev, &lp->cf_cmd.cmd);

	DEB(DEB_INIT, printk("%s: queuing CmdSASetup\n", dev->name));
	memcpy(lp->sa_cmd.eth_addr, dev->dev_addr, 6);
	lp->sa_cmd.cmd.command = CmdSASetup;
	CHECK_WBACK(lp, &(lp->sa_cmd), sizeof(struct sa_cmd));
	i596_add_cmd(dev, &lp->sa_cmd.cmd);

	DEB(DEB_INIT, printk("%s: queuing CmdTDR\n", dev->name));
	lp->tdr_cmd.cmd.command = CmdTDR;
	CHECK_WBACK(lp, &(lp->tdr_cmd), sizeof(struct tdr_cmd));
	i596_add_cmd(dev, &lp->tdr_cmd.cmd);

	spin_lock_irqsave (&lp->lock, flags);

	if (wait_cmd(dev, lp, 1000, "timed out waiting to issue RX_START")) {
		spin_unlock_irqrestore (&lp->lock, flags);
		goto failed;
	}
	DEB(DEB_INIT, printk("%s: Issuing RX_START\n", dev->name));
	lp->scb.command = RX_START;
	lp->scb.rfd = WSWAPrfd(virt_to_dma(lp,lp->rfds));
	CHECK_WBACK(lp, &(lp->scb), sizeof(struct i596_scb));

	CA(dev);

	spin_unlock_irqrestore (&lp->lock, flags);

	if (wait_cmd(dev, lp, 1000, "RX_START not processed"))
		goto failed;
	DEB(DEB_INIT, printk("%s: Receive unit started OK\n", dev->name));

	return 0;

failed:
	printk("%s: Failed to initialise 82596\n", dev->name);
	MPU_PORT(dev, PORT_RESET, 0);
	return -1;
}


static inline int i596_rx(struct net_device *dev)
{
	struct i596_private *lp = dev->priv;
	struct i596_rfd *rfd;
	struct i596_rbd *rbd;
	int frames = 0;

	DEB(DEB_RXFRAME, printk("i596_rx(), rfd_head %p, rbd_head %p\n",
			lp->rfd_head, lp->rbd_head));


	rfd = lp->rfd_head;		/* Ref next frame to check */

	CHECK_INV(lp, rfd, sizeof(struct i596_rfd));
	while ((rfd->stat) & STAT_C) {	/* Loop while complete frames */
		if (rfd->rbd == I596_NULL)
			rbd = NULL;
		else if (rfd->rbd == lp->rbd_head->b_addr) {
			rbd = lp->rbd_head;
			CHECK_INV(lp, rbd, sizeof(struct i596_rbd));
		}
		else {
			printk("%s: rbd chain broken!\n", dev->name);
			/* XXX Now what? */
			rbd = NULL;
		}
		DEB(DEB_RXFRAME, printk("  rfd %p, rfd.rbd %08x, rfd.stat %04x\n",
			rfd, rfd->rbd, rfd->stat));

		if (rbd != NULL && ((rfd->stat) & STAT_OK)) {
			/* a good frame */
			int pkt_len = rbd->count & 0x3fff;
			struct sk_buff *skb = rbd->skb;
			int rx_in_place = 0;

			DEB(DEB_RXADDR,print_eth(rbd->v_data, "received"));
			frames++;

			/* Check if the packet is long enough to just accept
			 * without copying to a properly sized skbuff.
			 */

			if (pkt_len > rx_copybreak) {
				struct sk_buff *newskb;
				dma_addr_t dma_addr;

				dma_unmap_single(lp->dev,(dma_addr_t)WSWAPchar(rbd->b_data), PKT_BUF_SZ, DMA_FROM_DEVICE);
				/* Get fresh skbuff to replace filled one. */
				newskb = dev_alloc_skb(PKT_BUF_SZ + 4);
				if (newskb == NULL) {
					skb = NULL;	/* drop pkt */
					goto memory_squeeze;
				}
				skb_reserve(newskb, 2);

				/* Pass up the skb already on the Rx ring. */
				skb_put(skb, pkt_len);
				rx_in_place = 1;
				rbd->skb = newskb;
				newskb->dev = dev;
				dma_addr = dma_map_single(lp->dev, newskb->data, PKT_BUF_SZ, DMA_FROM_DEVICE);
				rbd->v_data = newskb->data;
				rbd->b_data = WSWAPchar(dma_addr);
				CHECK_WBACK_INV(lp, rbd, sizeof(struct i596_rbd));
			}
			else
				skb = dev_alloc_skb(pkt_len + 2);
memory_squeeze:
			if (skb == NULL) {
				/* XXX tulip.c can defer packets here!! */
				printk("%s: i596_rx Memory squeeze, dropping packet.\n", dev->name);
				lp->stats.rx_dropped++;
			}
			else {
				skb->dev = dev;
				if (!rx_in_place) {
					/* 16 byte align the data fields */
					dma_sync_single_for_cpu(lp->dev, (dma_addr_t)WSWAPchar(rbd->b_data), PKT_BUF_SZ, DMA_FROM_DEVICE);
					skb_reserve(skb, 2);
					memcpy(skb_put(skb,pkt_len), rbd->v_data, pkt_len);
					dma_sync_single_for_device(lp->dev, (dma_addr_t)WSWAPchar(rbd->b_data), PKT_BUF_SZ, DMA_FROM_DEVICE);
				}
				skb->len = pkt_len;
				skb->protocol=eth_type_trans(skb,dev);
				netif_rx(skb);
				dev->last_rx = jiffies;
				lp->stats.rx_packets++;
				lp->stats.rx_bytes+=pkt_len;
			}
		}
		else {
			DEB(DEB_ERRORS, printk("%s: Error, rfd.stat = 0x%04x\n",
					dev->name, rfd->stat));
			lp->stats.rx_errors++;
			if ((rfd->stat) & 0x0001)
				lp->stats.collisions++;
			if ((rfd->stat) & 0x0080)
				lp->stats.rx_length_errors++;
			if ((rfd->stat) & 0x0100)
				lp->stats.rx_over_errors++;
			if ((rfd->stat) & 0x0200)
				lp->stats.rx_fifo_errors++;
			if ((rfd->stat) & 0x0400)
				lp->stats.rx_frame_errors++;
			if ((rfd->stat) & 0x0800)
				lp->stats.rx_crc_errors++;
			if ((rfd->stat) & 0x1000)
				lp->stats.rx_length_errors++;
		}

		/* Clear the buffer descriptor count and EOF + F flags */

		if (rbd != NULL && (rbd->count & 0x4000)) {
			rbd->count = 0;
			lp->rbd_head = rbd->v_next;
			CHECK_WBACK_INV(lp, rbd, sizeof(struct i596_rbd));
		}

		/* Tidy the frame descriptor, marking it as end of list */

		rfd->rbd = I596_NULL;
		rfd->stat = 0;
		rfd->cmd = CMD_EOL|CMD_FLEX;
		rfd->count = 0;

		/* Remove end-of-list from old end descriptor */

		rfd->v_prev->cmd = CMD_FLEX;

		/* Update record of next frame descriptor to process */

		lp->scb.rfd = rfd->b_next;
		lp->rfd_head = rfd->v_next;
		CHECK_WBACK_INV(lp, rfd->v_prev, sizeof(struct i596_rfd));
		CHECK_WBACK_INV(lp, rfd, sizeof(struct i596_rfd));
		rfd = lp->rfd_head;
		CHECK_INV(lp, rfd, sizeof(struct i596_rfd));
	}

	DEB(DEB_RXFRAME, printk("frames %d\n", frames));

	return 0;
}


static inline void i596_cleanup_cmd(struct net_device *dev, struct i596_private *lp)
{
	struct i596_cmd *ptr;

	while (lp->cmd_head != NULL) {
		ptr = lp->cmd_head;
		lp->cmd_head = ptr->v_next;
		lp->cmd_backlog--;

		switch ((ptr->command) & 0x7) {
		case CmdTx:
			{
				struct tx_cmd *tx_cmd = (struct tx_cmd *) ptr;
				struct sk_buff *skb = tx_cmd->skb;
				dma_unmap_single(lp->dev, tx_cmd->dma_addr, skb->len, DMA_TO_DEVICE);

				dev_kfree_skb(skb);

				lp->stats.tx_errors++;
				lp->stats.tx_aborted_errors++;

				ptr->v_next = NULL;
				ptr->b_next = I596_NULL;
				tx_cmd->cmd.command = 0;  /* Mark as free */
				break;
			}
		default:
			ptr->v_next = NULL;
			ptr->b_next = I596_NULL;
		}
		CHECK_WBACK_INV(lp, ptr, sizeof(struct i596_cmd));
	}

	wait_cmd(dev, lp, 100, "i596_cleanup_cmd timed out");
	lp->scb.cmd = I596_NULL;
	CHECK_WBACK(lp, &(lp->scb), sizeof(struct i596_scb));
}


static inline void i596_reset(struct net_device *dev, struct i596_private *lp)
{
	unsigned long flags;

	DEB(DEB_RESET, printk("i596_reset\n"));

	spin_lock_irqsave (&lp->lock, flags);

	wait_cmd(dev, lp, 100, "i596_reset timed out");

	netif_stop_queue(dev);

	/* FIXME: this command might cause an lpmc */
	lp->scb.command = CUC_ABORT | RX_ABORT;
	CHECK_WBACK(lp, &(lp->scb), sizeof(struct i596_scb));
	CA(dev);

	/* wait for shutdown */
	wait_cmd(dev, lp, 1000, "i596_reset 2 timed out");
	spin_unlock_irqrestore (&lp->lock, flags);

	i596_cleanup_cmd(dev,lp);
	i596_rx(dev);

	netif_start_queue(dev);
	init_i596_mem(dev);
}


static void i596_add_cmd(struct net_device *dev, struct i596_cmd *cmd)
{
	struct i596_private *lp = dev->priv;
	unsigned long flags;

	DEB(DEB_ADDCMD, printk("i596_add_cmd cmd_head %p\n", lp->cmd_head));

	cmd->status = 0;
	cmd->command |= (CMD_EOL | CMD_INTR);
	cmd->v_next = NULL;
	cmd->b_next = I596_NULL;
	CHECK_WBACK(lp, cmd, sizeof(struct i596_cmd));

	spin_lock_irqsave (&lp->lock, flags);

	if (lp->cmd_head != NULL) {
		lp->cmd_tail->v_next = cmd;
		lp->cmd_tail->b_next = WSWAPcmd(virt_to_dma(lp,&cmd->status));
		CHECK_WBACK(lp, lp->cmd_tail, sizeof(struct i596_cmd));
	} else {
		lp->cmd_head = cmd;
		wait_cmd(dev, lp, 100, "i596_add_cmd timed out");
		lp->scb.cmd = WSWAPcmd(virt_to_dma(lp,&cmd->status));
		lp->scb.command = CUC_START;
		CHECK_WBACK(lp, &(lp->scb), sizeof(struct i596_scb));
		CA(dev);
	}
	lp->cmd_tail = cmd;
	lp->cmd_backlog++;

	spin_unlock_irqrestore (&lp->lock, flags);

	if (lp->cmd_backlog > max_cmd_backlog) {
		unsigned long tickssofar = jiffies - lp->last_cmd;

		if (tickssofar < ticks_limit)
			return;

		printk("%s: command unit timed out, status resetting.\n", dev->name);
#if 1
		i596_reset(dev, lp);
#endif
	}
}

#if 0
/* this function makes a perfectly adequate probe...  but we have a
   device list */
static int i596_test(struct net_device *dev)
{
	struct i596_private *lp = dev->priv;
	volatile int *tint;
	u32 data;

	tint = (volatile int *)(&(lp->scp));
	data = virt_to_dma(lp,tint);

	tint[1] = -1;
	CHECK_WBACK(lp, tint, PAGE_SIZE);

	MPU_PORT(dev, 1, data);

	for(data = 1000000; data; data--) {
		CHECK_INV(lp, tint, PAGE_SIZE);
		if(tint[1] != -1)
			break;

	}

	printk("i596_test result %d\n", tint[1]);

}
#endif


static int i596_open(struct net_device *dev)
{
	DEB(DEB_OPEN, printk("%s: i596_open() irq %d.\n", dev->name, dev->irq));

	if (request_irq(dev->irq, &i596_interrupt, 0, "i82596", dev)) {
		printk("%s: IRQ %d not free\n", dev->name, dev->irq);
		goto out;
	}

	init_rx_bufs(dev);

	if (init_i596_mem(dev)) {
		printk("%s: Failed to init memory\n", dev->name);
		goto out_remove_rx_bufs;
	}

	netif_start_queue(dev);

	return 0;

out_remove_rx_bufs:
	remove_rx_bufs(dev);
	free_irq(dev->irq, dev);
out:
	return -EAGAIN;
}

static void i596_tx_timeout (struct net_device *dev)
{
	struct i596_private *lp = dev->priv;

	/* Transmitter timeout, serious problems. */
	DEB(DEB_ERRORS, printk("%s: transmit timed out, status resetting.\n",
			dev->name));

	lp->stats.tx_errors++;

	/* Try to restart the adaptor */
	if (lp->last_restart == lp->stats.tx_packets) {
		DEB(DEB_ERRORS, printk("Resetting board.\n"));
		/* Shutdown and restart */
		i596_reset (dev, lp);
	} else {
		/* Issue a channel attention signal */
		DEB(DEB_ERRORS, printk("Kicking board.\n"));
		lp->scb.command = CUC_START | RX_START;
		CHECK_WBACK_INV(lp, &(lp->scb), sizeof(struct i596_scb));
		CA (dev);
		lp->last_restart = lp->stats.tx_packets;
	}

	dev->trans_start = jiffies;
	netif_wake_queue (dev);
}


static int i596_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct i596_private *lp = dev->priv;
	struct tx_cmd *tx_cmd;
	struct i596_tbd *tbd;
	short length = skb->len;
	dev->trans_start = jiffies;

	DEB(DEB_STARTTX, printk("%s: i596_start_xmit(%x,%p) called\n", dev->name,
				skb->len, skb->data));

	if (length < ETH_ZLEN) {
		if (skb_padto(skb, ETH_ZLEN))
			return 0;
		length = ETH_ZLEN;
	}

	netif_stop_queue(dev);

	tx_cmd = lp->tx_cmds + lp->next_tx_cmd;
	tbd = lp->tbds + lp->next_tx_cmd;

	if (tx_cmd->cmd.command) {
		DEB(DEB_ERRORS, printk("%s: xmit ring full, dropping packet.\n",
				dev->name));
		lp->stats.tx_dropped++;

		dev_kfree_skb(skb);
	} else {
		if (++lp->next_tx_cmd == TX_RING_SIZE)
			lp->next_tx_cmd = 0;
		tx_cmd->tbd = WSWAPtbd(virt_to_dma(lp,tbd));
		tbd->next = I596_NULL;

		tx_cmd->cmd.command = CMD_FLEX | CmdTx;
		tx_cmd->skb = skb;

		tx_cmd->pad = 0;
		tx_cmd->size = 0;
		tbd->pad = 0;
		tbd->size = EOF | length;

		tx_cmd->dma_addr = dma_map_single(lp->dev, skb->data, skb->len,
				DMA_TO_DEVICE);
		tbd->data = WSWAPchar(tx_cmd->dma_addr);

		DEB(DEB_TXADDR,print_eth(skb->data, "tx-queued"));
		CHECK_WBACK_INV(lp, tx_cmd, sizeof(struct tx_cmd));
		CHECK_WBACK_INV(lp, tbd, sizeof(struct i596_tbd));
		i596_add_cmd(dev, &tx_cmd->cmd);

		lp->stats.tx_packets++;
		lp->stats.tx_bytes += length;
	}

	netif_start_queue(dev);

	return 0;
}

static void print_eth(unsigned char *add, char *str)
{
	int i;

	printk("i596 0x%p, ", add);
	for (i = 0; i < 6; i++)
		printk(" %02X", add[i + 6]);
	printk(" -->");
	for (i = 0; i < 6; i++)
		printk(" %02X", add[i]);
	printk(" %02X%02X, %s\n", add[12], add[13], str);
}


#define LAN_PROM_ADDR	0xF0810000

static int __devinit i82596_probe(struct net_device *dev,
				  struct device *gen_dev)
{
	int i;
	struct i596_private *lp;
	char eth_addr[6];
	dma_addr_t dma_addr;

	/* This lot is ensure things have been cache line aligned. */
	BUILD_BUG_ON(sizeof(struct i596_rfd) != 32);
	BUILD_BUG_ON(sizeof(struct i596_rbd) &  31);
	BUILD_BUG_ON(sizeof(struct tx_cmd)   &  31);
	BUILD_BUG_ON(sizeof(struct i596_tbd) != 32);
#ifndef __LP64__
	BUILD_BUG_ON(sizeof(struct i596_private) > 4096);
#endif

	if (!dev->base_addr || !dev->irq)
		return -ENODEV;

	if (pdc_lan_station_id(eth_addr, dev->base_addr)) {
		for (i=0; i < 6; i++) {
			eth_addr[i] = gsc_readb(LAN_PROM_ADDR + i);
		}
		printk(KERN_INFO "%s: MAC of HP700 LAN read from EEPROM\n", __FILE__);
	}

	dev->mem_start = (unsigned long) dma_alloc_noncoherent(gen_dev,
		sizeof(struct i596_private), &dma_addr, GFP_KERNEL);
	if (!dev->mem_start) {
		printk(KERN_ERR "%s: Couldn't get shared memory\n", __FILE__);
		return -ENOMEM;
	}

	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = eth_addr[i];

	/* The 82596-specific entries in the device structure. */
	dev->open = i596_open;
	dev->stop = i596_close;
	dev->hard_start_xmit = i596_start_xmit;
	dev->get_stats = i596_get_stats;
	dev->set_multicast_list = set_multicast_list;
	dev->tx_timeout = i596_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = i596_poll_controller;
#endif

	dev->priv = (void *)(dev->mem_start);

	lp = dev->priv;
	memset(lp, 0, sizeof(struct i596_private));

	lp->scb.command = 0;
	lp->scb.cmd = I596_NULL;
	lp->scb.rfd = I596_NULL;
	spin_lock_init(&lp->lock);
	lp->dma_addr = dma_addr;
	lp->dev = gen_dev;

	CHECK_WBACK_INV(lp, dev->mem_start, sizeof(struct i596_private));

	i = register_netdev(dev);
	if (i) {
		lp = dev->priv;
		dma_free_noncoherent(lp->dev, sizeof(struct i596_private),
				    (void *)dev->mem_start, lp->dma_addr);
		return i;
	};

	DEB(DEB_PROBE, printk(KERN_INFO "%s: 82596 at %#3lx,", dev->name, dev->base_addr));
	for (i = 0; i < 6; i++)
		DEB(DEB_PROBE, printk(" %2.2X", dev->dev_addr[i]));
	DEB(DEB_PROBE, printk(" IRQ %d.\n", dev->irq));
	DEB(DEB_INIT, printk(KERN_INFO "%s: lp at 0x%p (%d bytes), lp->scb at 0x%p\n",
		dev->name, lp, (int)sizeof(struct i596_private), &lp->scb));

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void i596_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	i596_interrupt(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

static irqreturn_t i596_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct i596_private *lp;
	unsigned short status, ack_cmd = 0;

	if (dev == NULL) {
		printk("%s: irq %d for unknown device.\n", __FUNCTION__, irq);
		return IRQ_NONE;
	}

	lp = dev->priv;

	spin_lock (&lp->lock);

	wait_cmd(dev, lp, 100, "i596 interrupt, timeout");
	status = lp->scb.status;

	DEB(DEB_INTS, printk("%s: i596 interrupt, IRQ %d, status %4.4x.\n",
			dev->name, irq, status));

	ack_cmd = status & 0xf000;

	if (!ack_cmd) {
		DEB(DEB_ERRORS, printk("%s: interrupt with no events\n", dev->name));
		spin_unlock (&lp->lock);
		return IRQ_NONE;
	}

	if ((status & 0x8000) || (status & 0x2000)) {
		struct i596_cmd *ptr;

		if ((status & 0x8000))
			DEB(DEB_INTS, printk("%s: i596 interrupt completed command.\n", dev->name));
		if ((status & 0x2000))
			DEB(DEB_INTS, printk("%s: i596 interrupt command unit inactive %x.\n", dev->name, status & 0x0700));

		while (lp->cmd_head != NULL) {
			CHECK_INV(lp, lp->cmd_head, sizeof(struct i596_cmd));
			if (!(lp->cmd_head->status & STAT_C))
				break;

			ptr = lp->cmd_head;

			DEB(DEB_STATUS, printk("cmd_head->status = %04x, ->command = %04x\n",
				       lp->cmd_head->status, lp->cmd_head->command));
			lp->cmd_head = ptr->v_next;
			lp->cmd_backlog--;

			switch ((ptr->command) & 0x7) {
			case CmdTx:
			    {
				struct tx_cmd *tx_cmd = (struct tx_cmd *) ptr;
				struct sk_buff *skb = tx_cmd->skb;

				if ((ptr->status) & STAT_OK) {
					DEB(DEB_TXADDR, print_eth(skb->data, "tx-done"));
				} else {
					lp->stats.tx_errors++;
					if ((ptr->status) & 0x0020)
						lp->stats.collisions++;
					if (!((ptr->status) & 0x0040))
						lp->stats.tx_heartbeat_errors++;
					if ((ptr->status) & 0x0400)
						lp->stats.tx_carrier_errors++;
					if ((ptr->status) & 0x0800)
						lp->stats.collisions++;
					if ((ptr->status) & 0x1000)
						lp->stats.tx_aborted_errors++;
				}
				dma_unmap_single(lp->dev, tx_cmd->dma_addr, skb->len, DMA_TO_DEVICE);
				dev_kfree_skb_irq(skb);

				tx_cmd->cmd.command = 0; /* Mark free */
				break;
			    }
			case CmdTDR:
			    {
				unsigned short status = ((struct tdr_cmd *)ptr)->status;

				if (status & 0x8000) {
					DEB(DEB_ANY, printk("%s: link ok.\n", dev->name));
				} else {
					if (status & 0x4000)
						printk("%s: Transceiver problem.\n", dev->name);
					if (status & 0x2000)
						printk("%s: Termination problem.\n", dev->name);
					if (status & 0x1000)
						printk("%s: Short circuit.\n", dev->name);

					DEB(DEB_TDR, printk("%s: Time %d.\n", dev->name, status & 0x07ff));
				}
				break;
			    }
			case CmdConfigure:
				/* Zap command so set_multicast_list() knows it is free */
				ptr->command = 0;
				break;
			}
			ptr->v_next = NULL;
		        ptr->b_next = I596_NULL;
			CHECK_WBACK(lp, ptr, sizeof(struct i596_cmd));
			lp->last_cmd = jiffies;
		}

		/* This mess is arranging that only the last of any outstanding
		 * commands has the interrupt bit set.  Should probably really
		 * only add to the cmd queue when the CU is stopped.
		 */
		ptr = lp->cmd_head;
		while ((ptr != NULL) && (ptr != lp->cmd_tail)) {
			struct i596_cmd *prev = ptr;

			ptr->command &= 0x1fff;
			ptr = ptr->v_next;
			CHECK_WBACK_INV(lp, prev, sizeof(struct i596_cmd));
		}

		if ((lp->cmd_head != NULL))
			ack_cmd |= CUC_START;
		lp->scb.cmd = WSWAPcmd(virt_to_dma(lp,&lp->cmd_head->status));
		CHECK_WBACK_INV(lp, &lp->scb, sizeof(struct i596_scb));
	}
	if ((status & 0x1000) || (status & 0x4000)) {
		if ((status & 0x4000))
			DEB(DEB_INTS, printk("%s: i596 interrupt received a frame.\n", dev->name));
		i596_rx(dev);
		/* Only RX_START if stopped - RGH 07-07-96 */
		if (status & 0x1000) {
			if (netif_running(dev)) {
				DEB(DEB_ERRORS, printk("%s: i596 interrupt receive unit inactive, status 0x%x\n", dev->name, status));
				ack_cmd |= RX_START;
				lp->stats.rx_errors++;
				lp->stats.rx_fifo_errors++;
				rebuild_rx_bufs(dev);
			}
		}
	}
	wait_cmd(dev, lp, 100, "i596 interrupt, timeout");
	lp->scb.command = ack_cmd;
	CHECK_WBACK(lp, &lp->scb, sizeof(struct i596_scb));

	/* DANGER: I suspect that some kind of interrupt
	 acknowledgement aside from acking the 82596 might be needed
	 here...  but it's running acceptably without */

	CA(dev);

	wait_cmd(dev, lp, 100, "i596 interrupt, exit timeout");
	DEB(DEB_INTS, printk("%s: exiting interrupt.\n", dev->name));

	spin_unlock (&lp->lock);
	return IRQ_HANDLED;
}

static int i596_close(struct net_device *dev)
{
	struct i596_private *lp = dev->priv;
	unsigned long flags;

	netif_stop_queue(dev);

	DEB(DEB_INIT, printk("%s: Shutting down ethercard, status was %4.4x.\n",
		       dev->name, lp->scb.status));

	spin_lock_irqsave(&lp->lock, flags);

	wait_cmd(dev, lp, 100, "close1 timed out");
	lp->scb.command = CUC_ABORT | RX_ABORT;
	CHECK_WBACK(lp, &lp->scb, sizeof(struct i596_scb));

	CA(dev);

	wait_cmd(dev, lp, 100, "close2 timed out");
	spin_unlock_irqrestore(&lp->lock, flags);
	DEB(DEB_STRUCT,i596_display_data(dev));
	i596_cleanup_cmd(dev,lp);

	disable_irq(dev->irq);

	free_irq(dev->irq, dev);
	remove_rx_bufs(dev);

	return 0;
}

static struct net_device_stats *
 i596_get_stats(struct net_device *dev)
{
	struct i596_private *lp = dev->priv;

	return &lp->stats;
}

/*
 *    Set or clear the multicast filter for this adaptor.
 */

static void set_multicast_list(struct net_device *dev)
{
	struct i596_private *lp = dev->priv;
	int config = 0, cnt;

	DEB(DEB_MULTI, printk("%s: set multicast list, %d entries, promisc %s, allmulti %s\n",
		dev->name, dev->mc_count, dev->flags & IFF_PROMISC ? "ON" : "OFF",
		dev->flags & IFF_ALLMULTI ? "ON" : "OFF"));

	if ((dev->flags & IFF_PROMISC) && !(lp->cf_cmd.i596_config[8] & 0x01)) {
		lp->cf_cmd.i596_config[8] |= 0x01;
		config = 1;
	}
	if (!(dev->flags & IFF_PROMISC) && (lp->cf_cmd.i596_config[8] & 0x01)) {
		lp->cf_cmd.i596_config[8] &= ~0x01;
		config = 1;
	}
	if ((dev->flags & IFF_ALLMULTI) && (lp->cf_cmd.i596_config[11] & 0x20)) {
		lp->cf_cmd.i596_config[11] &= ~0x20;
		config = 1;
	}
	if (!(dev->flags & IFF_ALLMULTI) && !(lp->cf_cmd.i596_config[11] & 0x20)) {
		lp->cf_cmd.i596_config[11] |= 0x20;
		config = 1;
	}
	if (config) {
		if (lp->cf_cmd.cmd.command)
			printk("%s: config change request already queued\n",
			       dev->name);
		else {
			lp->cf_cmd.cmd.command = CmdConfigure;
			CHECK_WBACK_INV(lp, &lp->cf_cmd, sizeof(struct cf_cmd));
			i596_add_cmd(dev, &lp->cf_cmd.cmd);
		}
	}

	cnt = dev->mc_count;
	if (cnt > MAX_MC_CNT)
	{
		cnt = MAX_MC_CNT;
		printk("%s: Only %d multicast addresses supported",
			dev->name, cnt);
	}

	if (dev->mc_count > 0) {
		struct dev_mc_list *dmi;
		unsigned char *cp;
		struct mc_cmd *cmd;

		cmd = &lp->mc_cmd;
		cmd->cmd.command = CmdMulticastList;
		cmd->mc_cnt = dev->mc_count * 6;
		cp = cmd->mc_addrs;
		for (dmi = dev->mc_list; cnt && dmi != NULL; dmi = dmi->next, cnt--, cp += 6) {
			memcpy(cp, dmi->dmi_addr, 6);
			if (i596_debug > 1)
				DEB(DEB_MULTI, printk("%s: Adding address %02x:%02x:%02x:%02x:%02x:%02x\n",
						dev->name, cp[0],cp[1],cp[2],cp[3],cp[4],cp[5]));
		}
		CHECK_WBACK_INV(lp, &lp->mc_cmd, sizeof(struct mc_cmd));
		i596_add_cmd(dev, &cmd->cmd);
	}
}

static int debug = -1;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "lasi_82596 debug mask");

static int num_drivers;
static struct net_device *netdevs[MAX_DRIVERS];

static int __devinit
lan_init_chip(struct parisc_device *dev)
{
	struct	net_device *netdevice;
	int	retval;

	if (num_drivers >= MAX_DRIVERS) {
		/* max count of possible i82596 drivers reached */
		return -ENOMEM;
	}

	if (num_drivers == 0)
		printk(KERN_INFO LASI_82596_DRIVER_VERSION "\n");

	if (!dev->irq) {
		printk(KERN_ERR "%s: IRQ not found for i82596 at 0x%lx\n",
			__FILE__, dev->hpa.start);
		return -ENODEV;
	}

	printk(KERN_INFO "Found i82596 at 0x%lx, IRQ %d\n", dev->hpa.start,
			dev->irq);

	netdevice = alloc_etherdev(0);
	if (!netdevice)
		return -ENOMEM;

	netdevice->base_addr = dev->hpa.start;
	netdevice->irq = dev->irq;

	retval = i82596_probe(netdevice, &dev->dev);
	if (retval) {
		free_netdev(netdevice);
		return -ENODEV;
	}

	if (dev->id.sversion == 0x72) {
		((struct i596_private *)netdevice->priv)->options = OPT_SWAP_PORT;
	}

	netdevs[num_drivers++] = netdevice;

	return retval;
}


static struct parisc_device_id lan_tbl[] = {
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x0008a },
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x00072 },
	{ 0, }
};

MODULE_DEVICE_TABLE(parisc, lan_tbl);

static struct parisc_driver lan_driver = {
	.name		= "lasi_82596",
	.id_table	= lan_tbl,
	.probe		= lan_init_chip,
};

static int __devinit lasi_82596_init(void)
{
	if (debug >= 0)
		i596_debug = debug;
	return register_parisc_driver(&lan_driver);
}

module_init(lasi_82596_init);

static void __exit lasi_82596_exit(void)
{
	int i;

	for (i=0; i<MAX_DRIVERS; i++) {
		struct i596_private *lp;
		struct net_device *netdevice;

		netdevice = netdevs[i];
		if (!netdevice)
			continue;

		unregister_netdev(netdevice);

		lp = netdevice->priv;
		dma_free_noncoherent(lp->dev, sizeof(struct i596_private),
				       (void *)netdevice->mem_start, lp->dma_addr);
		free_netdev(netdevice);
	}
	num_drivers = 0;

	unregister_parisc_driver(&lan_driver);
}

module_exit(lasi_82596_exit);
