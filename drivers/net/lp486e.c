/* Intel Professional Workstation/panther ethernet driver */
/* lp486e.c: A panther 82596 ethernet driver for linux. */
/*
    History and copyrights:

    Driver skeleton
        Written 1993 by Donald Becker.
        Copyright 1993 United States Government as represented by the Director,
        National Security Agency.  This software may only be used and
	distributed according to the terms of the GNU General Public License
	as modified by SRC, incorporated herein by reference.

        The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

    Apricot
        Written 1994 by Mark Evans.
        This driver is for the Apricot 82596 bus-master interface

        Modularised 12/94 Mark Evans

    Professional Workstation
	Derived from apricot.c by Ard van Breemen
	<ard@murphy.nl>|<ard@cstmel.hobby.nl>|<ard@cstmel.nl.eu.org>

	Credits:
	Thanks to Murphy Software BV for letting me write this in their time.
	Well, actually, I get payed doing this...
	(Also: see http://www.murphy.nl for murphy, and my homepage ~ard for
	more information on the Professional Workstation)

    Present version
	aeb@cwi.nl
*/
/*
    There are currently two motherboards that I know of in the
    professional workstation. The only one that I know is the
    intel panther motherboard. -- ard
*/
/*
The pws is equipped with an intel 82596. This is a very intelligent controller
which runs its own micro-code. Communication with the hostprocessor is done
through linked lists of commands and buffers in the hostprocessors memory.
A complete description of the 82596 is available from intel. Search for
a file called "29021806.pdf". It is a complete description of the chip itself.
To use it for the pws some additions are needed regarding generation of
the PORT and CA signal, and the interrupt glue needed for a pc.
I/O map:
PORT  SIZE ACTION MEANING
0xCB0    2 WRITE  Lower 16 bits for PORT command
0xCB2    2 WRITE  Upper 16 bits for PORT command, and issue of PORT command
0xCB4    1 WRITE  Generation of CA signal
0xCB8    1 WRITE  Clear interrupt glue
All other communication is through memory!
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/dma.h>

#define DRV_NAME "lp486e"

/* debug print flags */
#define LOG_SRCDST    0x80000000
#define LOG_STATINT   0x40000000
#define LOG_STARTINT  0x20000000

#define i596_debug debug

static int i596_debug = 0;

static const char * const medianame[] = {
	"10baseT", "AUI",
	"10baseT-FD", "AUI-FD",
};

#define LP486E_TOTAL_SIZE 16

#define I596_NULL (0xffffffff)

#define CMD_EOL		0x8000	/* The last command of the list, stop. */
#define CMD_SUSP	0x4000	/* Suspend after doing cmd. */
#define CMD_INTR	0x2000	/* Interrupt after doing cmd. */

#define CMD_FLEX	0x0008	/* Enable flexible memory model */

enum commands {
	CmdNOP = 0,
	CmdIASetup = 1,
	CmdConfigure = 2,
	CmdMulticastList = 3,
	CmdTx = 4,
	CmdTDR = 5,
	CmdDump = 6,
	CmdDiagnose = 7
};

#if 0
static const char *CUcmdnames[8] = { "NOP", "IASetup", "Configure", "MulticastList",
				     "Tx", "TDR", "Dump", "Diagnose" };
#endif

/* Status word bits */
#define	STAT_CX		0x8000	/* The CU finished executing a command
				   with the Interrupt bit set */
#define	STAT_FR		0x4000	/* The RU finished receiving a frame */
#define	STAT_CNA	0x2000	/* The CU left the active state */
#define	STAT_RNR	0x1000	/* The RU left the active state */
#define STAT_ACK	(STAT_CX | STAT_FR | STAT_CNA | STAT_RNR)
#define	STAT_CUS	0x0700	/* Status of CU: 0: idle, 1: suspended,
				   2: active, 3-7: unused */
#define STAT_RUS	0x00f0	/* Status of RU: 0: idle, 1: suspended,
				   2: no resources, 4: ready,
				   10: no resources due to no more RBDs,
				   12: no more RBDs, other: unused */
#define	STAT_T		0x0008	/* Bus throttle timers loaded */
#define	STAT_ZERO	0x0807	/* Always zero */

#if 0
static char *CUstates[8] = {
	"idle", "suspended", "active", 0, 0, 0, 0, 0
};
static char *RUstates[16] = {
	"idle", "suspended", "no resources", 0, "ready", 0, 0, 0,
	0, 0, "no RBDs", 0, "out of RBDs", 0, 0, 0
};

static void
i596_out_status(int status) {
	int bad = 0;
	char *s;

	printk("status %4.4x:", status);
	if (status == 0xffff)
		printk(" strange..\n");
	else {
		if (status & STAT_CX)
			printk("  CU done");
		if (status & STAT_CNA)
			printk("  CU stopped");
		if (status & STAT_FR)
			printk("  got a frame");
		if (status & STAT_RNR)
			printk("  RU stopped");
		if (status & STAT_T)
			printk("  throttled");
		if (status & STAT_ZERO)
			bad = 1;
		s = CUstates[(status & STAT_CUS) >> 8];
		if (!s)
			bad = 1;
		else
			printk("  CU(%s)", s);
		s = RUstates[(status & STAT_RUS) >> 4];
		if (!s)
			bad = 1;
		else
			printk("  RU(%s)", s);
		if (bad)
			printk("  bad status");
		printk("\n");
	}
}
#endif

/* Command word bits */
#define ACK_CX		0x8000
#define ACK_FR		0x4000
#define ACK_CNA		0x2000
#define ACK_RNR		0x1000

#define CUC_START	0x0100
#define CUC_RESUME	0x0200
#define CUC_SUSPEND	0x0300
#define CUC_ABORT	0x0400

#define RX_START	0x0010
#define RX_RESUME	0x0020
#define RX_SUSPEND	0x0030
#define RX_ABORT	0x0040

typedef u32 phys_addr;

static inline phys_addr
va_to_pa(void *x) {
	return x ? virt_to_bus(x) : I596_NULL;
}

static inline void *
pa_to_va(phys_addr x) {
	return (x == I596_NULL) ? NULL : bus_to_virt(x);
}

/* status bits for cmd */
#define CMD_STAT_C	0x8000	/* CU command complete */
#define CMD_STAT_B	0x4000	/* CU command in progress */
#define CMD_STAT_OK	0x2000	/* CU command completed without errors */
#define CMD_STAT_A	0x1000	/* CU command abnormally terminated */

struct i596_cmd {		/* 8 bytes */
	unsigned short status;
	unsigned short command;
	phys_addr pa_next;	/* va_to_pa(struct i596_cmd *next) */
};

#define EOF		0x8000
#define SIZE_MASK	0x3fff

struct i596_tbd {
	unsigned short size;
	unsigned short pad;
	phys_addr pa_next;	/* va_to_pa(struct i596_tbd *next) */
	phys_addr pa_data;	/* va_to_pa(char *data) */
	struct sk_buff *skb;
};

struct tx_cmd {
	struct i596_cmd cmd;
	phys_addr pa_tbd;	/* va_to_pa(struct i596_tbd *tbd) */
	unsigned short size;
	unsigned short pad;
};

/* status bits for rfd */
#define RFD_STAT_C	0x8000	/* Frame reception complete */
#define RFD_STAT_B	0x4000	/* Frame reception in progress */
#define RFD_STAT_OK	0x2000	/* Frame received without errors */
#define RFD_STATUS	0x1fff
#define RFD_LENGTH_ERR	0x1000
#define RFD_CRC_ERR	0x0800
#define RFD_ALIGN_ERR	0x0400
#define RFD_NOBUFS_ERR	0x0200
#define RFD_DMA_ERR	0x0100	/* DMA overrun failure to acquire system bus */
#define RFD_SHORT_FRAME_ERR	0x0080
#define RFD_NOEOP_ERR	0x0040
#define RFD_TRUNC_ERR	0x0020
#define RFD_MULTICAST  0x0002	/* 0: destination had our address
				   1: destination was broadcast/multicast */
#define RFD_COLLISION  0x0001

/* receive frame descriptor */
struct i596_rfd {
	unsigned short stat;
	unsigned short cmd;
	phys_addr pa_next;	/* va_to_pa(struct i596_rfd *next) */
	phys_addr pa_rbd;	/* va_to_pa(struct i596_rbd *rbd) */
	unsigned short count;
	unsigned short size;
	char data[1532];
};

#define RBD_EL		0x8000
#define RBD_P		0x4000
#define RBD_SIZEMASK	0x3fff
#define RBD_EOF		0x8000
#define RBD_F		0x4000

/* receive buffer descriptor */
struct i596_rbd {
	unsigned short size;
	unsigned short pad;
	phys_addr pa_next;	/* va_to_pa(struct i596_tbd *next) */
	phys_addr pa_data;	/* va_to_pa(char *data) */
	phys_addr pa_prev;	/* va_to_pa(struct i596_tbd *prev) */

	/* Driver private part */
	struct sk_buff *skb;
};

#define RX_RING_SIZE 64
#define RX_SKBSIZE (ETH_FRAME_LEN+10)
#define RX_RBD_SIZE 32

/* System Control Block - 40 bytes */
struct i596_scb {
	u16 status;		/* 0 */
	u16 command;		/* 2 */
	phys_addr pa_cmd;	/* 4 - va_to_pa(struct i596_cmd *cmd) */
	phys_addr pa_rfd;	/* 8 - va_to_pa(struct i596_rfd *rfd) */
	u32 crc_err;		/* 12 */
	u32 align_err;		/* 16 */
	u32 resource_err;	/* 20 */
	u32 over_err;		/* 24 */
	u32 rcvdt_err;		/* 28 */
	u32 short_err;		/* 32 */
	u16 t_on;		/* 36 */
	u16 t_off;		/* 38 */
};

/* Intermediate System Configuration Pointer - 8 bytes */
struct i596_iscp {
	u32 busy;		/* 0 */
	phys_addr pa_scb;	/* 4 - va_to_pa(struct i596_scb *scb) */
};

/* System Configuration Pointer - 12 bytes */
struct i596_scp {
	u32 sysbus;		/* 0 */
	u32 pad;		/* 4 */
	phys_addr pa_iscp;	/* 8 - va_to_pa(struct i596_iscp *iscp) */
};

/* Selftest and dump results - needs 16-byte alignment */
/*
 * The size of the dump area is 304 bytes. When the dump is executed
 * by the Port command an extra word will be appended to the dump area.
 * The extra word is a copy of the Dump status word (containing the
 * C, B, OK bits). [I find 0xa006, with a0 for C+OK and 6 for dump]
 */
struct i596_dump {
	u16 dump[153];		/* (304 = 130h) + 2 bytes */
};

struct i596_private {		/* aligned to a 16-byte boundary */
	struct i596_scp scp;	/* 0 - needs 16-byte alignment */
	struct i596_iscp iscp;	/* 12 */
	struct i596_scb scb;	/* 20 */
	u32 dummy;		/* 60 */
	struct i596_dump dump;	/* 64 - needs 16-byte alignment */

	struct i596_cmd set_add;
	char eth_addr[8];	/* directly follows set_add */

	struct i596_cmd set_conf;
	char i596_config[16];	/* directly follows set_conf */

	struct i596_cmd tdr;
	unsigned long tdr_stat;	/* directly follows tdr */

	int last_restart;
	struct i596_rbd *rbd_list;
	struct i596_rbd *rbd_tail;
	struct i596_rfd *rx_tail;
	struct i596_cmd *cmd_tail;
	struct i596_cmd *cmd_head;
	int cmd_backlog;
	unsigned long last_cmd;
	spinlock_t cmd_lock;
};

static char init_setup[14] = {
	0x8E,	/* length 14 bytes, prefetch on */
	0xC8,	/* default: fifo to 8, monitor off */
	0x40,	/* default: don't save bad frames (apricot.c had 0x80) */
	0x2E,	/* (default is 0x26)
		   No source address insertion, 8 byte preamble */
	0x00,	/* default priority and backoff */
	0x60,	/* default interframe spacing */
	0x00,	/* default slot time LSB */
	0xf2,	/* default slot time and nr of retries */
	0x00,	/* default various bits
		   (0: promiscuous mode, 1: broadcast disable,
		    2: encoding mode, 3: transmit on no CRS,
		    4: no CRC insertion, 5: CRC type,
		    6: bit stuffing, 7: padding) */
	0x00,	/* default carrier sense and collision detect */
	0x40,	/* default minimum frame length */
	0xff,	/* (default is 0xff, and that is what apricot.c has;
		   elp486.c has 0xfb: Enable crc append in memory.) */
	0x00,	/* default: not full duplex */
	0x7f	/* (default is 0x3f) multi IA */
};

static int i596_open(struct net_device *dev);
static int i596_start_xmit(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t i596_interrupt(int irq, void *dev_id);
static int i596_close(struct net_device *dev);
static void i596_add_cmd(struct net_device *dev, struct i596_cmd *cmd);
static void print_eth(char *);
static void set_multicast_list(struct net_device *dev);
static void i596_tx_timeout(struct net_device *dev);

static int
i596_timeout(struct net_device *dev, char *msg, int ct) {
	struct i596_private *lp;
	int boguscnt = ct;

	lp = netdev_priv(dev);
	while (lp->scb.command) {
		if (--boguscnt == 0) {
			printk("%s: %s timed out - stat %4.4x, cmd %4.4x\n",
			       dev->name, msg,
			       lp->scb.status, lp->scb.command);
			return 1;
		}
		udelay(5);
		barrier();
	}
	return 0;
}

static inline int
init_rx_bufs(struct net_device *dev, int num) {
	struct i596_private *lp;
	struct i596_rfd *rfd;
	int i;
	// struct i596_rbd *rbd;

	lp = netdev_priv(dev);
	lp->scb.pa_rfd = I596_NULL;

	for (i = 0; i < num; i++) {
		rfd = kmalloc(sizeof(struct i596_rfd), GFP_KERNEL);
		if (rfd == NULL)
			break;

		rfd->stat = 0;
		rfd->pa_rbd = I596_NULL;
		rfd->count = 0;
		rfd->size = 1532;
		if (i == 0) {
			rfd->cmd = CMD_EOL;
			lp->rx_tail = rfd;
		} else {
			rfd->cmd = 0;
		}
		rfd->pa_next = lp->scb.pa_rfd;
		lp->scb.pa_rfd = va_to_pa(rfd);
		lp->rx_tail->pa_next = lp->scb.pa_rfd;
	}

#if 0
	for (i = 0; i<RX_RBD_SIZE; i++) {
		rbd = kmalloc(sizeof(struct i596_rbd), GFP_KERNEL);
		if (rbd) {
			rbd->pad = 0;
			rbd->count = 0;
			rbd->skb = dev_alloc_skb(RX_SKBSIZE);
			if (!rbd->skb) {
				printk("dev_alloc_skb failed");
			}
			rbd->next = rfd->rbd;
			if (i) {
				rfd->rbd->prev = rbd;
				rbd->size = RX_SKBSIZE;
			} else {
				rbd->size = (RX_SKBSIZE | RBD_EL);
				lp->rbd_tail = rbd;
			}

			rfd->rbd = rbd;
		} else {
			printk("Could not kmalloc rbd\n");
		}
	}
	lp->rbd_tail->next = rfd->rbd;
#endif
	return (i);
}

static inline void
remove_rx_bufs(struct net_device *dev) {
	struct i596_private *lp;
	struct i596_rfd *rfd;

	lp = netdev_priv(dev);
	lp->rx_tail->pa_next = I596_NULL;

	do {
		rfd = pa_to_va(lp->scb.pa_rfd);
		lp->scb.pa_rfd = rfd->pa_next;
		kfree(rfd);
	} while (rfd != lp->rx_tail);

	lp->rx_tail = NULL;

#if 0
	for (lp->rbd_list) {
	}
#endif
}

#define PORT_RESET              0x00    /* reset 82596 */
#define PORT_SELFTEST           0x01    /* selftest */
#define PORT_ALTSCP             0x02    /* alternate SCB address */
#define PORT_DUMP               0x03    /* dump */

#define IOADDR	0xcb0		/* real constant */
#define IRQ	10		/* default IRQ - can be changed by ECU */

/* The 82596 requires two 16-bit write cycles for a port command */
static inline void
PORT(phys_addr a, unsigned int cmd) {
	if (a & 0xf)
		printk("lp486e.c: PORT: address not aligned\n");
	outw(((a & 0xffff) | cmd), IOADDR);
	outw(((a>>16) & 0xffff), IOADDR+2);
}

static inline void
CA(void) {
	outb(0, IOADDR+4);
	udelay(8);
}

static inline void
CLEAR_INT(void) {
	outb(0, IOADDR+8);
}

#if 0
/* selftest or dump */
static void
i596_port_do(struct net_device *dev, int portcmd, char *cmdname) {
	struct i596_private *lp = netdev_priv(dev);
	u16 *outp;
	int i, m;

	memset((void *)&(lp->dump), 0, sizeof(struct i596_dump));
	outp = &(lp->dump.dump[0]);

	PORT(va_to_pa(outp), portcmd);
	mdelay(30);             /* random, unmotivated */

	printk("lp486e i82596 %s result:\n", cmdname);
	for (m = ARRAY_SIZE(lp->dump.dump); m && lp->dump.dump[m-1] == 0; m--)
		;
	for (i = 0; i < m; i++) {
		printk(" %04x", lp->dump.dump[i]);
		if (i%8 == 7)
			printk("\n");
	}
	printk("\n");
}
#endif

static int
i596_scp_setup(struct net_device *dev) {
	struct i596_private *lp = netdev_priv(dev);
	int boguscnt;

	/* Setup SCP, ISCP, SCB */
	/*
	 * sysbus bits:
	 *  only a single byte is significant - here 0x44
	 *  0x80: big endian mode (details depend on stepping)
	 *  0x40: 1
	 *  0x20: interrupt pin is active low
	 *  0x10: lock function disabled
	 *  0x08: external triggering of bus throttle timers
	 *  0x06: 00: 82586 compat mode, 01: segmented mode, 10: linear mode
	 *  0x01: unused
	 */
	lp->scp.sysbus = 0x00440000; 		/* linear mode */
	lp->scp.pad = 0;			/* must be zero */
	lp->scp.pa_iscp = va_to_pa(&(lp->iscp));

	/*
	 * The CPU sets the ISCP to 1 before it gives the first CA()
	 */
	lp->iscp.busy = 0x0001;
	lp->iscp.pa_scb = va_to_pa(&(lp->scb));

	lp->scb.command = 0;
	lp->scb.status = 0;
	lp->scb.pa_cmd = I596_NULL;
	/* lp->scb.pa_rfd has been initialised already */

	lp->last_cmd = jiffies;
	lp->cmd_backlog = 0;
	lp->cmd_head = NULL;

	/*
	 * Reset the 82596.
	 * We need to wait 10 systemclock cycles, and
	 * 5 serial clock cycles.
	 */
	PORT(0, PORT_RESET);	/* address part ignored */
	udelay(100);

	/*
	 * Before the CA signal is asserted, the default SCP address
	 * (0x00fffff4) can be changed to a 16-byte aligned value
	 */
	PORT(va_to_pa(&lp->scp), PORT_ALTSCP);	/* change the scp address */

	/*
	 * The initialization procedure begins when a
	 * Channel Attention signal is asserted after a reset.
	 */

	CA();

	/*
	 * The ISCP busy is cleared by the 82596 after the SCB address is read.
	 */
	boguscnt = 100;
	while (lp->iscp.busy) {
		if (--boguscnt == 0) {
			/* No i82596 present? */
			printk("%s: i82596 initialization timed out\n",
			       dev->name);
			return 1;
		}
		udelay(5);
		barrier();
	}
	/* I find here boguscnt==100, so no delay was required. */

	return 0;
}

static int
init_i596(struct net_device *dev) {
	struct i596_private *lp;

	if (i596_scp_setup(dev))
		return 1;

	lp = netdev_priv(dev);
	lp->scb.command = 0;

	memcpy ((void *)lp->i596_config, init_setup, 14);
	lp->set_conf.command = CmdConfigure;
	i596_add_cmd(dev, (void *)&lp->set_conf);

	memcpy ((void *)lp->eth_addr, dev->dev_addr, 6);
	lp->set_add.command = CmdIASetup;
	i596_add_cmd(dev, (struct i596_cmd *)&lp->set_add);

	lp->tdr.command = CmdTDR;
	i596_add_cmd(dev, (struct i596_cmd *)&lp->tdr);

	if (lp->scb.command && i596_timeout(dev, "i82596 init", 200))
		return 1;

	lp->scb.command = RX_START;
	CA();

	barrier();

	if (lp->scb.command && i596_timeout(dev, "Receive Unit start", 100))
		return 1;

	return 0;
}

/* Receive a single frame */
static inline int
i596_rx_one(struct net_device *dev, struct i596_private *lp,
	    struct i596_rfd *rfd, int *frames) {

	if (rfd->stat & RFD_STAT_OK) {
		/* a good frame */
		int pkt_len = (rfd->count & 0x3fff);
		struct sk_buff *skb = dev_alloc_skb(pkt_len);

		(*frames)++;

		if (rfd->cmd & CMD_EOL)
			printk("Received on EOL\n");

		if (skb == NULL) {
			printk ("%s: i596_rx Memory squeeze, "
				"dropping packet.\n", dev->name);
			dev->stats.rx_dropped++;
			return 1;
		}

		memcpy(skb_put(skb,pkt_len), rfd->data, pkt_len);

		skb->protocol = eth_type_trans(skb,dev);
		netif_rx(skb);
		dev->stats.rx_packets++;
	} else {
#if 0
		printk("Frame reception error status %04x\n",
		       rfd->stat);
#endif
		dev->stats.rx_errors++;
		if (rfd->stat & RFD_COLLISION)
			dev->stats.collisions++;
		if (rfd->stat & RFD_SHORT_FRAME_ERR)
			dev->stats.rx_length_errors++;
		if (rfd->stat & RFD_DMA_ERR)
			dev->stats.rx_over_errors++;
		if (rfd->stat & RFD_NOBUFS_ERR)
			dev->stats.rx_fifo_errors++;
		if (rfd->stat & RFD_ALIGN_ERR)
			dev->stats.rx_frame_errors++;
		if (rfd->stat & RFD_CRC_ERR)
			dev->stats.rx_crc_errors++;
		if (rfd->stat & RFD_LENGTH_ERR)
			dev->stats.rx_length_errors++;
	}
	rfd->stat = rfd->count = 0;
	return 0;
}

static int
i596_rx(struct net_device *dev) {
	struct i596_private *lp = netdev_priv(dev);
	struct i596_rfd *rfd;
	int frames = 0;

	while (1) {
		rfd = pa_to_va(lp->scb.pa_rfd);
		if (!rfd) {
			printk(KERN_ERR "i596_rx: NULL rfd?\n");
			return 0;
		}
#if 1
		if (rfd->stat && !(rfd->stat & (RFD_STAT_C | RFD_STAT_B)))
			printk("SF:%p-%04x\n", rfd, rfd->stat);
#endif
		if (!(rfd->stat & RFD_STAT_C))
			break;		/* next one not ready */
		if (i596_rx_one(dev, lp, rfd, &frames))
			break;		/* out of memory */
		rfd->cmd = CMD_EOL;
		lp->rx_tail->cmd = 0;
		lp->rx_tail = rfd;
		lp->scb.pa_rfd = rfd->pa_next;
		barrier();
	}

	return frames;
}

static void
i596_cleanup_cmd(struct net_device *dev) {
	struct i596_private *lp;
	struct i596_cmd *cmd;

	lp = netdev_priv(dev);
	while (lp->cmd_head) {
		cmd = (struct i596_cmd *)lp->cmd_head;

		lp->cmd_head = pa_to_va(lp->cmd_head->pa_next);
		lp->cmd_backlog--;

		switch ((cmd->command) & 0x7) {
			case CmdTx: {
				struct tx_cmd *tx_cmd = (struct tx_cmd *) cmd;
				struct i596_tbd * tx_cmd_tbd;
				tx_cmd_tbd = pa_to_va(tx_cmd->pa_tbd);

				dev_kfree_skb_any(tx_cmd_tbd->skb);

				dev->stats.tx_errors++;
				dev->stats.tx_aborted_errors++;

				cmd->pa_next = I596_NULL;
				kfree((unsigned char *)tx_cmd);
				netif_wake_queue(dev);
				break;
			}
			case CmdMulticastList: {
				// unsigned short count = *((unsigned short *) (ptr + 1));

				cmd->pa_next = I596_NULL;
				kfree((unsigned char *)cmd);
				break;
			}
			default: {
				cmd->pa_next = I596_NULL;
				break;
			}
		}
		barrier();
	}

	if (lp->scb.command && i596_timeout(dev, "i596_cleanup_cmd", 100))
		;

	lp->scb.pa_cmd = va_to_pa(lp->cmd_head);
}

static void i596_reset(struct net_device *dev, struct i596_private *lp, int ioaddr) {

	if (lp->scb.command && i596_timeout(dev, "i596_reset", 100))
		;

	netif_stop_queue(dev);

	lp->scb.command = CUC_ABORT | RX_ABORT;
	CA();
	barrier();

	/* wait for shutdown */
	if (lp->scb.command && i596_timeout(dev, "i596_reset(2)", 400))
		;

	i596_cleanup_cmd(dev);
	i596_rx(dev);

	netif_start_queue(dev);
	/*dev_kfree_skb(skb, FREE_WRITE);*/
	init_i596(dev);
}

static void i596_add_cmd(struct net_device *dev, struct i596_cmd *cmd) {
	struct i596_private *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	unsigned long flags;

	cmd->status = 0;
	cmd->command |= (CMD_EOL | CMD_INTR);
	cmd->pa_next = I596_NULL;

	spin_lock_irqsave(&lp->cmd_lock, flags);

	if (lp->cmd_head) {
		lp->cmd_tail->pa_next = va_to_pa(cmd);
	} else {
		lp->cmd_head = cmd;
		if (lp->scb.command && i596_timeout(dev, "i596_add_cmd", 100))
			;
		lp->scb.pa_cmd = va_to_pa(cmd);
		lp->scb.command = CUC_START;
		CA();
	}
	lp->cmd_tail = cmd;
	lp->cmd_backlog++;

	lp->cmd_head = pa_to_va(lp->scb.pa_cmd);
	spin_unlock_irqrestore(&lp->cmd_lock, flags);

	if (lp->cmd_backlog > 16) {
		int tickssofar = jiffies - lp->last_cmd;
		if (tickssofar < HZ/4)
			return;

		printk(KERN_WARNING "%s: command unit timed out, status resetting.\n", dev->name);
		i596_reset(dev, lp, ioaddr);
	}
}

static int i596_open(struct net_device *dev)
{
	int i;

	i = request_irq(dev->irq, &i596_interrupt, IRQF_SHARED, dev->name, dev);
	if (i) {
		printk(KERN_ERR "%s: IRQ %d not free\n", dev->name, dev->irq);
		return i;
	}

	if ((i = init_rx_bufs(dev, RX_RING_SIZE)) < RX_RING_SIZE)
		printk(KERN_ERR "%s: only able to allocate %d receive buffers\n", dev->name, i);

	if (i < 4) {
		free_irq(dev->irq, dev);
		return -EAGAIN;
	}
	netif_start_queue(dev);
	init_i596(dev);
	return 0;			/* Always succeed */
}

static int i596_start_xmit (struct sk_buff *skb, struct net_device *dev) {
	struct tx_cmd *tx_cmd;
	short length;

	length = skb->len;

	if (length < ETH_ZLEN) {
		if (skb_padto(skb, ETH_ZLEN))
			return 0;
		length = ETH_ZLEN;
	}

	dev->trans_start = jiffies;

	tx_cmd = kmalloc((sizeof (struct tx_cmd) + sizeof (struct i596_tbd)), GFP_ATOMIC);
	if (tx_cmd == NULL) {
		printk(KERN_WARNING "%s: i596_xmit Memory squeeze, dropping packet.\n", dev->name);
		dev->stats.tx_dropped++;
		dev_kfree_skb (skb);
	} else {
		struct i596_tbd *tx_cmd_tbd;
		tx_cmd_tbd = (struct i596_tbd *) (tx_cmd + 1);
		tx_cmd->pa_tbd = va_to_pa (tx_cmd_tbd);
		tx_cmd_tbd->pa_next = I596_NULL;

		tx_cmd->cmd.command = (CMD_FLEX | CmdTx);

		tx_cmd->pad = 0;
		tx_cmd->size = 0;
		tx_cmd_tbd->pad = 0;
		tx_cmd_tbd->size = (EOF | length);

		tx_cmd_tbd->pa_data = va_to_pa (skb->data);
		tx_cmd_tbd->skb = skb;

		if (i596_debug & LOG_SRCDST)
			print_eth (skb->data);

		i596_add_cmd (dev, (struct i596_cmd *) tx_cmd);

		dev->stats.tx_packets++;
	}

	return 0;
}

static void
i596_tx_timeout (struct net_device *dev) {
	struct i596_private *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;

	/* Transmitter timeout, serious problems. */
	printk(KERN_WARNING "%s: transmit timed out, status resetting.\n", dev->name);
	dev->stats.tx_errors++;

	/* Try to restart the adaptor */
	if (lp->last_restart == dev->stats.tx_packets) {
		printk ("Resetting board.\n");

		/* Shutdown and restart */
		i596_reset (dev, lp, ioaddr);
	} else {
		/* Issue a channel attention signal */
		printk ("Kicking board.\n");
		lp->scb.command = (CUC_START | RX_START);
		CA();
		lp->last_restart = dev->stats.tx_packets;
	}
	netif_wake_queue(dev);
}

static void print_eth(char *add)
{
	int i;

	printk ("Dest  ");
	for (i = 0; i < 6; i++)
		printk(" %2.2X", (unsigned char) add[i]);
	printk ("\n");

	printk ("Source");
	for (i = 0; i < 6; i++)
		printk(" %2.2X", (unsigned char) add[i+6]);
	printk ("\n");

	printk ("type %2.2X%2.2X\n",
		(unsigned char) add[12], (unsigned char) add[13]);
}

static const struct net_device_ops i596_netdev_ops = {
	.ndo_open		= i596_open,
	.ndo_stop		= i596_close,
	.ndo_start_xmit		= i596_start_xmit,
	.ndo_set_multicast_list = set_multicast_list,
	.ndo_tx_timeout		= i596_tx_timeout,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static int __init lp486e_probe(struct net_device *dev) {
	struct i596_private *lp;
	unsigned char eth_addr[6] = { 0, 0xaa, 0, 0, 0, 0 };
	unsigned char *bios;
	int i, j;
	int ret = -ENOMEM;
	static int probed;

	if (probed)
		return -ENODEV;
	probed++;

	if (!request_region(IOADDR, LP486E_TOTAL_SIZE, DRV_NAME)) {
		printk(KERN_ERR "lp486e: IO address 0x%x in use\n", IOADDR);
		return -EBUSY;
	}

	lp = netdev_priv(dev);
	spin_lock_init(&lp->cmd_lock);

	/*
	 * Do we really have this thing?
	 */
	if (i596_scp_setup(dev)) {
		ret = -ENODEV;
		goto err_out_kfree;
	}

	dev->base_addr = IOADDR;
	dev->irq = IRQ;


	/*
	 * How do we find the ethernet address? I don't know.
	 * One possibility is to look at the EISA configuration area
	 * [0xe8000-0xe9fff]. This contains the ethernet address
	 * but not at a fixed address - things depend on setup options.
	 *
	 * If we find no address, or the wrong address, use
	 *   ifconfig eth0 hw ether a1:a2:a3:a4:a5:a6
	 * with the value found in the BIOS setup.
	 */
	bios = bus_to_virt(0xe8000);
	for (j = 0; j < 0x2000; j++) {
		if (bios[j] == 0 && bios[j+1] == 0xaa && bios[j+2] == 0) {
			printk("%s: maybe address at BIOS 0x%x:",
			       dev->name, 0xe8000+j);
			for (i = 0; i < 6; i++) {
				eth_addr[i] = bios[i+j];
				printk(" %2.2X", eth_addr[i]);
			}
			printk("\n");
		}
	}

	printk("%s: lp486e 82596 at %#3lx, IRQ %d,",
	       dev->name, dev->base_addr, dev->irq);
	for (i = 0; i < 6; i++)
		printk(" %2.2X", dev->dev_addr[i] = eth_addr[i]);
	printk("\n");

	/* The LP486E-specific entries in the device structure. */
	dev->netdev_ops = &i596_netdev_ops;
	dev->watchdog_timeo = 5*HZ;

#if 0
	/* selftest reports 0x320925ae - don't know what that means */
	i596_port_do(dev, PORT_SELFTEST, "selftest");
	i596_port_do(dev, PORT_DUMP, "dump");
#endif
	return 0;

err_out_kfree:
	release_region(IOADDR, LP486E_TOTAL_SIZE);
	return ret;
}

static inline void
i596_handle_CU_completion(struct net_device *dev,
			  struct i596_private *lp,
			  unsigned short status,
			  unsigned short *ack_cmdp) {
	struct i596_cmd *cmd;
	int frames_out = 0;
	int commands_done = 0;
	int cmd_val;
	unsigned long flags;

	spin_lock_irqsave(&lp->cmd_lock, flags);
	cmd = lp->cmd_head;

	while (lp->cmd_head && (lp->cmd_head->status & CMD_STAT_C)) {
		cmd = lp->cmd_head;

		lp->cmd_head = pa_to_va(lp->cmd_head->pa_next);
		lp->cmd_backlog--;

		commands_done++;
		cmd_val = cmd->command & 0x7;
#if 0
		printk("finished CU %s command (%d)\n",
		       CUcmdnames[cmd_val], cmd_val);
#endif
		switch (cmd_val) {
		case CmdTx:
		{
			struct tx_cmd *tx_cmd;
			struct i596_tbd *tx_cmd_tbd;

			tx_cmd = (struct tx_cmd *) cmd;
			tx_cmd_tbd = pa_to_va(tx_cmd->pa_tbd);

			frames_out++;
			if (cmd->status & CMD_STAT_OK) {
				if (i596_debug)
					print_eth(pa_to_va(tx_cmd_tbd->pa_data));
			} else {
				dev->stats.tx_errors++;
				if (i596_debug)
					printk("transmission failure:%04x\n",
					       cmd->status);
				if (cmd->status & 0x0020)
					dev->stats.collisions++;
				if (!(cmd->status & 0x0040))
					dev->stats.tx_heartbeat_errors++;
				if (cmd->status & 0x0400)
					dev->stats.tx_carrier_errors++;
				if (cmd->status & 0x0800)
					dev->stats.collisions++;
				if (cmd->status & 0x1000)
					dev->stats.tx_aborted_errors++;
			}
			dev_kfree_skb_irq(tx_cmd_tbd->skb);

			cmd->pa_next = I596_NULL;
			kfree((unsigned char *)tx_cmd);
			netif_wake_queue(dev);
			break;
		}

		case CmdMulticastList:
			cmd->pa_next = I596_NULL;
			kfree((unsigned char *)cmd);
			break;

		case CmdTDR:
		{
			unsigned long status = *((unsigned long *) (cmd + 1));
			if (status & 0x8000) {
				if (i596_debug)
					printk("%s: link ok.\n", dev->name);
			} else {
				if (status & 0x4000)
					printk("%s: Transceiver problem.\n",
					       dev->name);
				if (status & 0x2000)
					printk("%s: Termination problem.\n",
					       dev->name);
				if (status & 0x1000)
					printk("%s: Short circuit.\n",
					       dev->name);
				printk("%s: Time %ld.\n",
				       dev->name, status & 0x07ff);
			}
		}
		default:
			cmd->pa_next = I596_NULL;
			lp->last_cmd = jiffies;

		}
		barrier();
	}

	cmd = lp->cmd_head;
	while (cmd && (cmd != lp->cmd_tail)) {
		cmd->command &= 0x1fff;
		cmd = pa_to_va(cmd->pa_next);
		barrier();
	}

	if (lp->cmd_head)
		*ack_cmdp |= CUC_START;
	lp->scb.pa_cmd = va_to_pa(lp->cmd_head);
	spin_unlock_irqrestore(&lp->cmd_lock, flags);
}

static irqreturn_t
i596_interrupt(int irq, void *dev_instance)
{
	struct net_device *dev = dev_instance;
	struct i596_private *lp = netdev_priv(dev);
	unsigned short status, ack_cmd = 0;
	int frames_in = 0;

	/*
	 * The 82596 examines the command, performs the required action,
	 * and then clears the SCB command word.
	 */
	if (lp->scb.command && i596_timeout(dev, "interrupt", 40))
		;

	/*
	 * The status word indicates the status of the 82596.
	 * It is modified only by the 82596.
	 *
	 * [So, we must not clear it. I find often status 0xffff,
	 *  which is not one of the values allowed by the docs.]
	 */
	status = lp->scb.status;
#if 0
	if (i596_debug) {
		printk("%s: i596 interrupt, ", dev->name);
		i596_out_status(status);
	}
#endif
	/* Impossible, but it happens - perhaps when we get
	   a receive interrupt but scb.pa_rfd is I596_NULL. */
	if (status == 0xffff) {
		printk("%s: i596_interrupt: got status 0xffff\n", dev->name);
		goto out;
	}

	ack_cmd = (status & STAT_ACK);

	if (status & (STAT_CX | STAT_CNA))
		i596_handle_CU_completion(dev, lp, status, &ack_cmd);

	if (status & (STAT_FR | STAT_RNR)) {
		/* Restart the receive unit when it got inactive somehow */
		if ((status & STAT_RNR) && netif_running(dev))
			ack_cmd |= RX_START;

		if (status & STAT_FR) {
			frames_in = i596_rx(dev);
			if (!frames_in)
				printk("receive frame reported, but no frames\n");
		}
	}

	/* acknowledge the interrupt */
	/*
	if ((lp->scb.pa_cmd != I596_NULL) && netif_running(dev))
		ack_cmd |= CUC_START;
	*/

	if (lp->scb.command && i596_timeout(dev, "i596 interrupt", 100))
		;

	lp->scb.command = ack_cmd;

	CLEAR_INT();
	CA();

 out:
	return IRQ_HANDLED;
}

static int i596_close(struct net_device *dev) {
	struct i596_private *lp = netdev_priv(dev);

	netif_stop_queue(dev);

	if (i596_debug)
		printk("%s: Shutting down ethercard, status was %4.4x.\n",
		       dev->name, lp->scb.status);

	lp->scb.command = (CUC_ABORT | RX_ABORT);
	CA();

	i596_cleanup_cmd(dev);

	if (lp->scb.command && i596_timeout(dev, "i596_close", 200))
		;

	free_irq(dev->irq, dev);
	remove_rx_bufs(dev);

	return 0;
}

/*
*	Set or clear the multicast filter for this adaptor.
*/

static void set_multicast_list(struct net_device *dev) {
	struct i596_private *lp = netdev_priv(dev);
	struct i596_cmd *cmd;

	if (i596_debug > 1)
		printk ("%s: set multicast list %d\n",
			dev->name, dev->mc_count);

	if (dev->mc_count > 0) {
		struct dev_mc_list *dmi;
		char *cp;
		cmd = kmalloc(sizeof(struct i596_cmd)+2+dev->mc_count*6, GFP_ATOMIC);
		if (cmd == NULL) {
			printk (KERN_ERR "%s: set_multicast Memory squeeze.\n", dev->name);
			return;
		}
		cmd->command = CmdMulticastList;
		*((unsigned short *) (cmd + 1)) = dev->mc_count * 6;
		cp = ((char *)(cmd + 1))+2;
		for (dmi = dev->mc_list; dmi != NULL; dmi = dmi->next) {
			memcpy(cp, dmi,6);
			cp += 6;
		}
		if (i596_debug & LOG_SRCDST)
			print_eth (((char *)(cmd + 1)) + 2);
		i596_add_cmd(dev, cmd);
	} else {
		if (lp->set_conf.pa_next != I596_NULL) {
			return;
		}
		if (dev->mc_count == 0 && !(dev->flags & (IFF_PROMISC | IFF_ALLMULTI))) {
			lp->i596_config[8] &= ~0x01;
		} else {
			lp->i596_config[8] |= 0x01;
		}

		i596_add_cmd(dev, (struct i596_cmd *) &lp->set_conf);
	}
}

MODULE_AUTHOR("Ard van Breemen <ard@cstmel.nl.eu.org>");
MODULE_DESCRIPTION("Intel Panther onboard i82596 driver");
MODULE_LICENSE("GPL");

static struct net_device *dev_lp486e;
static int full_duplex;
static int options;
static int io = IOADDR;
static int irq = IRQ;

module_param(debug, int, 0);
//module_param(max_interrupt_work, int, 0);
//module_param(reverse_probe, int, 0);
//module_param(rx_copybreak, int, 0);
module_param(options, int, 0);
module_param(full_duplex, int, 0);

static int __init lp486e_init_module(void) {
	int err;
	struct net_device *dev = alloc_etherdev(sizeof(struct i596_private));
	if (!dev)
		return -ENOMEM;

	dev->irq = irq;
	dev->base_addr = io;
	err = lp486e_probe(dev);
	if (err) {
		free_netdev(dev);
		return err;
	}
	err = register_netdev(dev);
	if (err) {
		release_region(dev->base_addr, LP486E_TOTAL_SIZE);
		free_netdev(dev);
		return err;
	}
	dev_lp486e = dev;
	full_duplex = 0;
	options = 0;
	return 0;
}

static void __exit lp486e_cleanup_module(void) {
	unregister_netdev(dev_lp486e);
	release_region(dev_lp486e->base_addr, LP486E_TOTAL_SIZE);
	free_netdev(dev_lp486e);
}

module_init(lp486e_init_module);
module_exit(lp486e_cleanup_module);
