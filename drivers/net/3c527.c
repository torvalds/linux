/* 3c527.c: 3Com Etherlink/MC32 driver for Linux 2.4 and 2.6.
 *
 *	(c) Copyright 1998 Red Hat Software Inc
 *	Written by Alan Cox. 
 *	Further debugging by Carl Drougge.
 *      Initial SMP support by Felipe W Damasio <felipewd@terra.com.br>
 *      Heavily modified by Richard Procter <rnp@paradise.net.nz>
 *
 *	Based on skeleton.c written 1993-94 by Donald Becker and ne2.c
 *	(for the MCA stuff) written by Wim Dumon.
 *
 *	Thanks to 3Com for making this possible by providing me with the
 *	documentation.
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU General Public License, incorporated herein by reference.
 *
 */

#define DRV_NAME		"3c527"
#define DRV_VERSION		"0.7-SMP"
#define DRV_RELDATE		"2003/09/21"

static const char *version =
DRV_NAME ".c:v" DRV_VERSION " " DRV_RELDATE " Richard Procter <rnp@paradise.net.nz>\n";

/**
 * DOC: Traps for the unwary
 *
 *	The diagram (Figure 1-1) and the POS summary disagree with the
 *	"Interrupt Level" section in the manual.
 *
 *	The manual contradicts itself when describing the minimum number 
 *	buffers in the 'configure lists' command. 
 *	My card accepts a buffer config of 4/4. 
 *
 *	Setting the SAV BP bit does not save bad packets, but
 *	only enables RX on-card stats collection. 
 *
 *	The documentation in places seems to miss things. In actual fact
 *	I've always eventually found everything is documented, it just
 *	requires careful study.
 *
 * DOC: Theory Of Operation
 *
 *	The 3com 3c527 is a 32bit MCA bus mastering adapter with a large
 *	amount of on board intelligence that housekeeps a somewhat dumber
 *	Intel NIC. For performance we want to keep the transmit queue deep
 *	as the card can transmit packets while fetching others from main
 *	memory by bus master DMA. Transmission and reception are driven by
 *	circular buffer queues.
 *
 *	The mailboxes can be used for controlling how the card traverses
 *	its buffer rings, but are used only for inital setup in this
 *	implementation.  The exec mailbox allows a variety of commands to
 *	be executed. Each command must complete before the next is
 *	executed. Primarily we use the exec mailbox for controlling the
 *	multicast lists.  We have to do a certain amount of interesting
 *	hoop jumping as the multicast list changes can occur in interrupt
 *	state when the card has an exec command pending. We defer such
 *	events until the command completion interrupt.
 *
 *	A copy break scheme (taken from 3c59x.c) is employed whereby
 *	received frames exceeding a configurable length are passed
 *	directly to the higher networking layers without incuring a copy,
 *	in what amounts to a time/space trade-off.
 *	 
 *	The card also keeps a large amount of statistical information
 *	on-board. In a perfect world, these could be used safely at no
 *	cost. However, lacking information to the contrary, processing
 *	them without races would involve so much extra complexity as to
 *	make it unworthwhile to do so. In the end, a hybrid SW/HW
 *	implementation was made necessary --- see mc32_update_stats().  
 *
 * DOC: Notes
 *	
 *	It should be possible to use two or more cards, but at this stage
 *	only by loading two copies of the same module.
 *
 *	The on-board 82586 NIC has trouble receiving multiple
 *	back-to-back frames and so is likely to drop packets from fast
 *	senders.
**/

#include <linux/module.h>

#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/mca-legacy.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/ethtool.h>
#include <linux/completion.h>
#include <linux/bitops.h>

#include <asm/semaphore.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>

#include "3c527.h"

MODULE_LICENSE("GPL");

/*
 * The name of the card. Is used for messages and in the requests for
 * io regions, irqs and dma channels
 */
static const char* cardname = DRV_NAME;

/* use 0 for production, 1 for verification, >2 for debug */
#ifndef NET_DEBUG
#define NET_DEBUG 2
#endif

#undef DEBUG_IRQ

static unsigned int mc32_debug = NET_DEBUG;

/* The number of low I/O ports used by the ethercard. */
#define MC32_IO_EXTENT	8

/* As implemented, values must be a power-of-2 -- 4/8/16/32 */ 
#define TX_RING_LEN     32       /* Typically the card supports 37  */
#define RX_RING_LEN     8        /*     "       "        "          */

/* Copy break point, see above for details. 
 * Setting to > 1512 effectively disables this feature.	*/	    
#define RX_COPYBREAK    200      /* Value from 3c59x.c */

/* Issue the 82586 workaround command - this is for "busy lans", but
 * basically means for all lans now days - has a performance (latency) 
 * cost, but best set. */ 
static const int WORKAROUND_82586=1;

/* Pointers to buffers and their on-card records */
struct mc32_ring_desc 
{
	volatile struct skb_header *p;                    
	struct sk_buff *skb;          
};

/* Information that needs to be kept for each board. */
struct mc32_local 
{
	int slot;

	u32 base;
	struct net_device_stats net_stats;
	volatile struct mc32_mailbox *rx_box;
	volatile struct mc32_mailbox *tx_box;
	volatile struct mc32_mailbox *exec_box;
        volatile struct mc32_stats *stats;    /* Start of on-card statistics */
        u16 tx_chain;           /* Transmit list start offset */
	u16 rx_chain;           /* Receive list start offset */
        u16 tx_len;             /* Transmit list count */ 
        u16 rx_len;             /* Receive list count */

	u16 xceiver_desired_state; /* HALTED or RUNNING */
	u16 cmd_nonblocking;    /* Thread is uninterested in command result */
	u16 mc_reload_wait;	/* A multicast load request is pending */
	u32 mc_list_valid;	/* True when the mclist is set */

	struct mc32_ring_desc tx_ring[TX_RING_LEN];	/* Host Transmit ring */
	struct mc32_ring_desc rx_ring[RX_RING_LEN];	/* Host Receive ring */

	atomic_t tx_count;	/* buffers left */
	atomic_t tx_ring_head;  /* index to tx en-queue end */
	u16 tx_ring_tail;       /* index to tx de-queue end */

	u16 rx_ring_tail;       /* index to rx de-queue end */ 

	struct semaphore cmd_mutex;    /* Serialises issuing of execute commands */
        struct completion execution_cmd; /* Card has completed an execute command */
	struct completion xceiver_cmd;   /* Card has completed a tx or rx command */
};

/* The station (ethernet) address prefix, used for a sanity check. */
#define SA_ADDR0 0x02
#define SA_ADDR1 0x60
#define SA_ADDR2 0xAC

struct mca_adapters_t {
	unsigned int	id;
	char		*name;
};

static const struct mca_adapters_t mc32_adapters[] = {
	{ 0x0041, "3COM EtherLink MC/32" },
	{ 0x8EF5, "IBM High Performance Lan Adapter" },
	{ 0x0000, NULL }
};


/* Macros for ring index manipulations */ 
static inline u16 next_rx(u16 rx) { return (rx+1)&(RX_RING_LEN-1); };
static inline u16 prev_rx(u16 rx) { return (rx-1)&(RX_RING_LEN-1); };

static inline u16 next_tx(u16 tx) { return (tx+1)&(TX_RING_LEN-1); };


/* Index to functions, as function prototypes. */
static int	mc32_probe1(struct net_device *dev, int ioaddr);
static int      mc32_command(struct net_device *dev, u16 cmd, void *data, int len);
static int	mc32_open(struct net_device *dev);
static void	mc32_timeout(struct net_device *dev);
static int	mc32_send_packet(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t mc32_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int	mc32_close(struct net_device *dev);
static struct	net_device_stats *mc32_get_stats(struct net_device *dev);
static void	mc32_set_multicast_list(struct net_device *dev);
static void	mc32_reset_multicast_list(struct net_device *dev);
static struct ethtool_ops netdev_ethtool_ops;

static void cleanup_card(struct net_device *dev)
{
	struct mc32_local *lp = netdev_priv(dev);
	unsigned slot = lp->slot;
	mca_mark_as_unused(slot);
	mca_set_adapter_name(slot, NULL);
	free_irq(dev->irq, dev);
	release_region(dev->base_addr, MC32_IO_EXTENT);
}

/**
 * mc32_probe 	-	Search for supported boards
 * @unit: interface number to use
 *
 * Because MCA bus is a real bus and we can scan for cards we could do a
 * single scan for all boards here. Right now we use the passed in device
 * structure and scan for only one board. This needs fixing for modules
 * in particular.
 */

struct net_device *__init mc32_probe(int unit)
{
	struct net_device *dev = alloc_etherdev(sizeof(struct mc32_local));
	static int current_mca_slot = -1;
	int i;
	int err;

	if (!dev)
		return ERR_PTR(-ENOMEM);

	if (unit >= 0)
		sprintf(dev->name, "eth%d", unit);

	SET_MODULE_OWNER(dev);

	/* Do not check any supplied i/o locations. 
	   POS registers usually don't fail :) */

	/* MCA cards have POS registers.  
	   Autodetecting MCA cards is extremely simple. 
	   Just search for the card. */

	for(i = 0; (mc32_adapters[i].name != NULL); i++) {
		current_mca_slot = 
			mca_find_unused_adapter(mc32_adapters[i].id, 0);

		if(current_mca_slot != MCA_NOTFOUND) {
			if(!mc32_probe1(dev, current_mca_slot))
			{
				mca_set_adapter_name(current_mca_slot, 
						mc32_adapters[i].name);
				mca_mark_as_used(current_mca_slot);
				err = register_netdev(dev);
				if (err) {
					cleanup_card(dev);
					free_netdev(dev);
					dev = ERR_PTR(err);
				}
				return dev;
			}
			
		}
	}
	free_netdev(dev);
	return ERR_PTR(-ENODEV);
}

/**
 * mc32_probe1	-	Check a given slot for a board and test the card
 * @dev:  Device structure to fill in
 * @slot: The MCA bus slot being used by this card
 *
 * Decode the slot data and configure the card structures. Having done this we
 * can reset the card and configure it. The card does a full self test cycle
 * in firmware so we have to wait for it to return and post us either a 
 * failure case or some addresses we use to find the board internals.
 */

static int __init mc32_probe1(struct net_device *dev, int slot)
{
	static unsigned version_printed;
	int i, err;
	u8 POS;
	u32 base;
	struct mc32_local *lp = netdev_priv(dev);
	static u16 mca_io_bases[]={
		0x7280,0x7290,
		0x7680,0x7690,
		0x7A80,0x7A90,
		0x7E80,0x7E90
	};
	static u32 mca_mem_bases[]={
		0x00C0000,
		0x00C4000,
		0x00C8000,
		0x00CC000,
		0x00D0000,
		0x00D4000,
		0x00D8000,
		0x00DC000
	};
	static char *failures[]={
		"Processor instruction",
		"Processor data bus",
		"Processor data bus",
		"Processor data bus",
		"Adapter bus",
		"ROM checksum",
		"Base RAM",
		"Extended RAM",
		"82586 internal loopback",
		"82586 initialisation failure",
		"Adapter list configuration error"
	};

	/* Time to play MCA games */

	if (mc32_debug  &&  version_printed++ == 0)
		printk(KERN_DEBUG "%s", version);

	printk(KERN_INFO "%s: %s found in slot %d:", dev->name, cardname, slot);

	POS = mca_read_stored_pos(slot, 2);
	
	if(!(POS&1))
	{
		printk(" disabled.\n");
		return -ENODEV;
	}

	/* Fill in the 'dev' fields. */
	dev->base_addr = mca_io_bases[(POS>>1)&7];
	dev->mem_start = mca_mem_bases[(POS>>4)&7];
	
	POS = mca_read_stored_pos(slot, 4);
	if(!(POS&1))
	{
		printk("memory window disabled.\n");
		return -ENODEV;
	}

	POS = mca_read_stored_pos(slot, 5);
	
	i=(POS>>4)&3;
	if(i==3)
	{
		printk("invalid memory window.\n");
		return -ENODEV;
	}
	
	i*=16384;
	i+=16384;
	
	dev->mem_end=dev->mem_start + i;
	
	dev->irq = ((POS>>2)&3)+9;
	
	if(!request_region(dev->base_addr, MC32_IO_EXTENT, cardname))
	{
		printk("io 0x%3lX, which is busy.\n", dev->base_addr);
		return -EBUSY;
	}

	printk("io 0x%3lX irq %d mem 0x%lX (%dK)\n",
		dev->base_addr, dev->irq, dev->mem_start, i/1024);
	
	
	/* We ought to set the cache line size here.. */
	
	
	/*
	 *	Go PROM browsing
	 */
	 
	printk("%s: Address ", dev->name);
	 
	/* Retrieve and print the ethernet address. */
	for (i = 0; i < 6; i++)
	{
		mca_write_pos(slot, 6, i+12);
		mca_write_pos(slot, 7, 0);
	
		printk(" %2.2x", dev->dev_addr[i] = mca_read_pos(slot,3));
	}

	mca_write_pos(slot, 6, 0);
	mca_write_pos(slot, 7, 0);

	POS = mca_read_stored_pos(slot, 4);
	
	if(POS&2)
		printk(" : BNC port selected.\n");
	else 
		printk(" : AUI port selected.\n");
		
	POS=inb(dev->base_addr+HOST_CTRL);
	POS|=HOST_CTRL_ATTN|HOST_CTRL_RESET;
	POS&=~HOST_CTRL_INTE;
	outb(POS, dev->base_addr+HOST_CTRL);
	/* Reset adapter */
	udelay(100);
	/* Reset off */
	POS&=~(HOST_CTRL_ATTN|HOST_CTRL_RESET);
	outb(POS, dev->base_addr+HOST_CTRL);
	
	udelay(300);
	
	/*
	 *	Grab the IRQ
	 */

	err = request_irq(dev->irq, &mc32_interrupt, SA_SHIRQ | SA_SAMPLE_RANDOM, DRV_NAME, dev);
	if (err) {
		release_region(dev->base_addr, MC32_IO_EXTENT);
		printk(KERN_ERR "%s: unable to get IRQ %d.\n", DRV_NAME, dev->irq);
		goto err_exit_ports;
	}

	memset(lp, 0, sizeof(struct mc32_local));
	lp->slot = slot;

	i=0;

	base = inb(dev->base_addr);
	
	while(base == 0xFF)
	{
		i++;
		if(i == 1000)
		{
			printk(KERN_ERR "%s: failed to boot adapter.\n", dev->name);
			err = -ENODEV; 
			goto err_exit_irq;
		}
		udelay(1000);
		if(inb(dev->base_addr+2)&(1<<5))
			base = inb(dev->base_addr);
	}

	if(base>0)
	{
		if(base < 0x0C)
			printk(KERN_ERR "%s: %s%s.\n", dev->name, failures[base-1],
				base<0x0A?" test failure":"");
		else
			printk(KERN_ERR "%s: unknown failure %d.\n", dev->name, base);
		err = -ENODEV; 
		goto err_exit_irq;
	}
	
	base=0;
	for(i=0;i<4;i++)
	{
		int n=0;
	
		while(!(inb(dev->base_addr+2)&(1<<5)))
		{
			n++;
			udelay(50);
			if(n>100)
			{
				printk(KERN_ERR "%s: mailbox read fail (%d).\n", dev->name, i);
				err = -ENODEV;
				goto err_exit_irq;
			}
		}

		base|=(inb(dev->base_addr)<<(8*i));
	}
	
	lp->exec_box=isa_bus_to_virt(dev->mem_start+base);
	
	base=lp->exec_box->data[1]<<16|lp->exec_box->data[0];  
	
	lp->base = dev->mem_start+base;
	
	lp->rx_box=isa_bus_to_virt(lp->base + lp->exec_box->data[2]); 
	lp->tx_box=isa_bus_to_virt(lp->base + lp->exec_box->data[3]);
	
	lp->stats = isa_bus_to_virt(lp->base + lp->exec_box->data[5]);

	/*
	 *	Descriptor chains (card relative)
	 */
	 
	lp->tx_chain 		= lp->exec_box->data[8];   /* Transmit list start offset */
	lp->rx_chain 		= lp->exec_box->data[10];  /* Receive list start offset */
	lp->tx_len 		= lp->exec_box->data[9];   /* Transmit list count */ 
	lp->rx_len 		= lp->exec_box->data[11];  /* Receive list count */

	init_MUTEX_LOCKED(&lp->cmd_mutex);
	init_completion(&lp->execution_cmd);
	init_completion(&lp->xceiver_cmd);
	
	printk("%s: Firmware Rev %d. %d RX buffers, %d TX buffers. Base of 0x%08X.\n",
		dev->name, lp->exec_box->data[12], lp->rx_len, lp->tx_len, lp->base);

	dev->open		= mc32_open;
	dev->stop		= mc32_close;
	dev->hard_start_xmit	= mc32_send_packet;
	dev->get_stats		= mc32_get_stats;
	dev->set_multicast_list = mc32_set_multicast_list;
	dev->tx_timeout		= mc32_timeout;
	dev->watchdog_timeo	= HZ*5;	/* Board does all the work */
	dev->ethtool_ops	= &netdev_ethtool_ops;

	return 0;

err_exit_irq:
	free_irq(dev->irq, dev);
err_exit_ports:
	release_region(dev->base_addr, MC32_IO_EXTENT);
	return err;
}


/**
 *	mc32_ready_poll		-	wait until we can feed it a command
 *	@dev:	The device to wait for
 *	
 *	Wait until the card becomes ready to accept a command via the
 *	command register. This tells us nothing about the completion
 *	status of any pending commands and takes very little time at all.
 */
 
static inline void mc32_ready_poll(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	while(!(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR));
}


/**
 *	mc32_command_nowait	-	send a command non blocking
 *	@dev: The 3c527 to issue the command to
 *	@cmd: The command word to write to the mailbox
 *	@data: A data block if the command expects one
 *	@len: Length of the data block
 *
 *	Send a command from interrupt state. If there is a command
 *	currently being executed then we return an error of -1. It
 *	simply isn't viable to wait around as commands may be
 *	slow. This can theoretically be starved on SMP, but it's hard
 *	to see a realistic situation.  We do not wait for the command
 *	to complete --- we rely on the interrupt handler to tidy up
 *	after us.
 */

static int mc32_command_nowait(struct net_device *dev, u16 cmd, void *data, int len)
{
	struct mc32_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	int ret = -1;

	if (down_trylock(&lp->cmd_mutex) == 0)
	{
		lp->cmd_nonblocking=1;
		lp->exec_box->mbox=0;
		lp->exec_box->mbox=cmd;
		memcpy((void *)lp->exec_box->data, data, len);
		barrier();	/* the memcpy forgot the volatile so be sure */

		/* Send the command */
		mc32_ready_poll(dev);
		outb(1<<6, ioaddr+HOST_CMD);

		ret = 0;

		/* Interrupt handler will signal mutex on completion */
	}

	return ret;
}


/**
 *	mc32_command	-	send a command and sleep until completion
 *	@dev: The 3c527 card to issue the command to
 *	@cmd: The command word to write to the mailbox
 *	@data: A data block if the command expects one
 *	@len: Length of the data block
 *
 *	Sends exec commands in a user context. This permits us to wait around
 *	for the replies and also to wait for the command buffer to complete
 *	from a previous command before we execute our command. After our 
 *	command completes we will attempt any pending multicast reload
 *	we blocked off by hogging the exec buffer.
 *
 *	You feed the card a command, you wait, it interrupts you get a 
 *	reply. All well and good. The complication arises because you use
 *	commands for filter list changes which come in at bh level from things
 *	like IPV6 group stuff.
 */
  
static int mc32_command(struct net_device *dev, u16 cmd, void *data, int len)
{
	struct mc32_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	int ret = 0;
	
	down(&lp->cmd_mutex);

	/*
	 *     My Turn
	 */

	lp->cmd_nonblocking=0;
	lp->exec_box->mbox=0;
	lp->exec_box->mbox=cmd;
	memcpy((void *)lp->exec_box->data, data, len);
	barrier();	/* the memcpy forgot the volatile so be sure */

	mc32_ready_poll(dev);
	outb(1<<6, ioaddr+HOST_CMD);

	wait_for_completion(&lp->execution_cmd);
	
	if(lp->exec_box->mbox&(1<<13))
		ret = -1;

	up(&lp->cmd_mutex);

	/*
	 *	A multicast set got blocked - try it now
         */

	if(lp->mc_reload_wait)
	{
		mc32_reset_multicast_list(dev);
	}

	return ret;
}


/**
 *	mc32_start_transceiver	-	tell board to restart tx/rx
 *	@dev: The 3c527 card to issue the command to
 *
 *	This may be called from the interrupt state, where it is used
 *	to restart the rx ring if the card runs out of rx buffers. 
 *	
 * 	We must first check if it's ok to (re)start the transceiver. See
 *      mc32_close for details.
 */

static void mc32_start_transceiver(struct net_device *dev) {

	struct mc32_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;

	/* Ignore RX overflow on device closure */ 
	if (lp->xceiver_desired_state==HALTED)
		return; 

	/* Give the card the offset to the post-EOL-bit RX descriptor */
	mc32_ready_poll(dev); 
	lp->rx_box->mbox=0;
	lp->rx_box->data[0]=lp->rx_ring[prev_rx(lp->rx_ring_tail)].p->next; 
	outb(HOST_CMD_START_RX, ioaddr+HOST_CMD);      

	mc32_ready_poll(dev); 
	lp->tx_box->mbox=0;
	outb(HOST_CMD_RESTRT_TX, ioaddr+HOST_CMD);   /* card ignores this on RX restart */ 
	
	/* We are not interrupted on start completion */ 
}


/**
 *	mc32_halt_transceiver	-	tell board to stop tx/rx
 *	@dev: The 3c527 card to issue the command to
 *
 *	We issue the commands to halt the card's transceiver. In fact,
 *	after some experimenting we now simply tell the card to
 *	suspend. When issuing aborts occasionally odd things happened.
 *
 *	We then sleep until the card has notified us that both rx and
 *	tx have been suspended.
 */ 

static void mc32_halt_transceiver(struct net_device *dev) 
{
	struct mc32_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;

	mc32_ready_poll(dev);	
	lp->rx_box->mbox=0;
	outb(HOST_CMD_SUSPND_RX, ioaddr+HOST_CMD);			
	wait_for_completion(&lp->xceiver_cmd);

	mc32_ready_poll(dev); 
	lp->tx_box->mbox=0;
	outb(HOST_CMD_SUSPND_TX, ioaddr+HOST_CMD);	
	wait_for_completion(&lp->xceiver_cmd);
}


/**
 *	mc32_load_rx_ring	-	load the ring of receive buffers
 *	@dev: 3c527 to build the ring for
 *
 *	This initalises the on-card and driver datastructures to
 *	the point where mc32_start_transceiver() can be called.
 *
 *	The card sets up the receive ring for us. We are required to use the
 *	ring it provides, although the size of the ring is configurable.
 *
 * 	We allocate an sk_buff for each ring entry in turn and
 * 	initalise its house-keeping info. At the same time, we read
 * 	each 'next' pointer in our rx_ring array. This reduces slow
 * 	shared-memory reads and makes it easy to access predecessor
 * 	descriptors.
 *
 *	We then set the end-of-list bit for the last entry so that the
 * 	card will know when it has run out of buffers.
 */
	 
static int mc32_load_rx_ring(struct net_device *dev)
{
	struct mc32_local *lp = netdev_priv(dev);
	int i;
	u16 rx_base;
	volatile struct skb_header *p;
	
	rx_base=lp->rx_chain;

	for(i=0; i<RX_RING_LEN; i++) {
		lp->rx_ring[i].skb=alloc_skb(1532, GFP_KERNEL);
		if (lp->rx_ring[i].skb==NULL) {
			for (;i>=0;i--)
				kfree_skb(lp->rx_ring[i].skb);
			return -ENOBUFS;
		}
		skb_reserve(lp->rx_ring[i].skb, 18);

		p=isa_bus_to_virt(lp->base+rx_base);
				
		p->control=0;
		p->data=isa_virt_to_bus(lp->rx_ring[i].skb->data);
		p->status=0;
		p->length=1532;
	
		lp->rx_ring[i].p=p; 
		rx_base=p->next; 
	}

	lp->rx_ring[i-1].p->control |= CONTROL_EOL;

	lp->rx_ring_tail=0;

	return 0;
}	


/**
 *	mc32_flush_rx_ring	-	free the ring of receive buffers
 *	@lp: Local data of 3c527 to flush the rx ring of
 *
 *	Free the buffer for each ring slot. This may be called 
 *      before mc32_load_rx_ring(), eg. on error in mc32_open().
 *      Requires rx skb pointers to point to a valid skb, or NULL.
 */

static void mc32_flush_rx_ring(struct net_device *dev)
{
	struct mc32_local *lp = netdev_priv(dev);
	int i; 

	for(i=0; i < RX_RING_LEN; i++) 
	{ 
		if (lp->rx_ring[i].skb) {
			dev_kfree_skb(lp->rx_ring[i].skb);
			lp->rx_ring[i].skb = NULL;
		}
		lp->rx_ring[i].p=NULL; 
	} 
}


/**
 *	mc32_load_tx_ring	-	load transmit ring
 *	@dev: The 3c527 card to issue the command to
 *
 *	This sets up the host transmit data-structures. 
 *
 *	First, we obtain from the card it's current postion in the tx
 *	ring, so that we will know where to begin transmitting
 *	packets.
 * 	
 * 	Then, we read the 'next' pointers from the on-card tx ring into
 *  	our tx_ring array to reduce slow shared-mem reads. Finally, we
 * 	intitalise the tx house keeping variables.
 * 
 */ 

static void mc32_load_tx_ring(struct net_device *dev)
{ 
	struct mc32_local *lp = netdev_priv(dev);
	volatile struct skb_header *p;
	int i; 
	u16 tx_base;

	tx_base=lp->tx_box->data[0]; 

	for(i=0 ; i<TX_RING_LEN ; i++)
	{
		p=isa_bus_to_virt(lp->base+tx_base);
		lp->tx_ring[i].p=p; 
		lp->tx_ring[i].skb=NULL;

		tx_base=p->next;
	}

	/* -1 so that tx_ring_head cannot "lap" tx_ring_tail */
	/* see mc32_tx_ring */

	atomic_set(&lp->tx_count, TX_RING_LEN-1); 
	atomic_set(&lp->tx_ring_head, 0); 
	lp->tx_ring_tail=0; 
} 


/**
 *	mc32_flush_tx_ring 	-	free transmit ring
 *	@lp: Local data of 3c527 to flush the tx ring of
 *
 *      If the ring is non-empty, zip over the it, freeing any
 *      allocated skb_buffs.  The tx ring house-keeping variables are
 *      then reset. Requires rx skb pointers to point to a valid skb,
 *      or NULL.
 */

static void mc32_flush_tx_ring(struct net_device *dev)
{
	struct mc32_local *lp = netdev_priv(dev);
	int i;

	for (i=0; i < TX_RING_LEN; i++)
	{
		if (lp->tx_ring[i].skb)
		{
			dev_kfree_skb(lp->tx_ring[i].skb);
			lp->tx_ring[i].skb = NULL;
		}
	}

	atomic_set(&lp->tx_count, 0); 
	atomic_set(&lp->tx_ring_head, 0); 
	lp->tx_ring_tail=0;
}
 	

/**
 *	mc32_open	-	handle 'up' of card
 *	@dev: device to open
 *
 *	The user is trying to bring the card into ready state. This requires
 *	a brief dialogue with the card. Firstly we enable interrupts and then
 *	'indications'. Without these enabled the card doesn't bother telling
 *	us what it has done. This had me puzzled for a week.
 *
 *	We configure the number of card descriptors, then load the network
 *	address and multicast filters. Turn on the workaround mode. This
 *	works around a bug in the 82586 - it asks the firmware to do
 *	so. It has a performance (latency) hit but is needed on busy
 *	[read most] lans. We load the ring with buffers then we kick it
 *	all off.
 */

static int mc32_open(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	struct mc32_local *lp = netdev_priv(dev);
	u8 one=1;
	u8 regs;
	u16 descnumbuffs[2] = {TX_RING_LEN, RX_RING_LEN};

	/*
	 *	Interrupts enabled
	 */

	regs=inb(ioaddr+HOST_CTRL);
	regs|=HOST_CTRL_INTE;
	outb(regs, ioaddr+HOST_CTRL);
	
	/*
	 *      Allow ourselves to issue commands
	 */

	up(&lp->cmd_mutex);


	/*
	 *	Send the indications on command
	 */

	mc32_command(dev, 4, &one, 2);

	/*
	 *	Poke it to make sure it's really dead. 
	 */

	mc32_halt_transceiver(dev); 
	mc32_flush_tx_ring(dev); 

	/* 
	 *	Ask card to set up on-card descriptors to our spec 
	 */ 

	if(mc32_command(dev, 8, descnumbuffs, 4)) { 
		printk("%s: %s rejected our buffer configuration!\n",
	 	       dev->name, cardname);
		mc32_close(dev); 
		return -ENOBUFS; 
	}
	
	/* Report new configuration */ 
	mc32_command(dev, 6, NULL, 0); 

	lp->tx_chain 		= lp->exec_box->data[8];   /* Transmit list start offset */
	lp->rx_chain 		= lp->exec_box->data[10];  /* Receive list start offset */
	lp->tx_len 		= lp->exec_box->data[9];   /* Transmit list count */ 
	lp->rx_len 		= lp->exec_box->data[11];  /* Receive list count */
 
	/* Set Network Address */
	mc32_command(dev, 1, dev->dev_addr, 6);
	
	/* Set the filters */
	mc32_set_multicast_list(dev);
		   
	if (WORKAROUND_82586) { 
		u16 zero_word=0;
		mc32_command(dev, 0x0D, &zero_word, 2);   /* 82586 bug workaround on  */
	}

	mc32_load_tx_ring(dev);
	
	if(mc32_load_rx_ring(dev)) 
	{
		mc32_close(dev);
		return -ENOBUFS;
	}

	lp->xceiver_desired_state = RUNNING;
	
	/* And finally, set the ball rolling... */
	mc32_start_transceiver(dev);

	netif_start_queue(dev);

	return 0;
}


/**
 *	mc32_timeout	-	handle a timeout from the network layer
 *	@dev: 3c527 that timed out
 *
 *	Handle a timeout on transmit from the 3c527. This normally means
 *	bad things as the hardware handles cable timeouts and mess for
 *	us.
 *
 */

static void mc32_timeout(struct net_device *dev)
{
	printk(KERN_WARNING "%s: transmit timed out?\n", dev->name);
	/* Try to restart the adaptor. */
	netif_wake_queue(dev);
}


/**
 *	mc32_send_packet	-	queue a frame for transmit
 *	@skb: buffer to transmit
 *	@dev: 3c527 to send it out of
 *
 *	Transmit a buffer. This normally means throwing the buffer onto
 *	the transmit queue as the queue is quite large. If the queue is
 *	full then we set tx_busy and return. Once the interrupt handler
 *	gets messages telling it to reclaim transmit queue entries, we will
 *	clear tx_busy and the kernel will start calling this again.
 *
 *      We do not disable interrupts or acquire any locks; this can
 *      run concurrently with mc32_tx_ring(), and the function itself
 *      is serialised at a higher layer. However, similarly for the
 *      card itself, we must ensure that we update tx_ring_head only
 *      after we've established a valid packet on the tx ring (and
 *      before we let the card "see" it, to prevent it racing with the
 *      irq handler).
 * 
 */

static int mc32_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct mc32_local *lp = netdev_priv(dev);
	u32 head = atomic_read(&lp->tx_ring_head);
	
	volatile struct skb_header *p, *np;

	netif_stop_queue(dev);

	if(atomic_read(&lp->tx_count)==0) {
		return 1;
	}

	if (skb_padto(skb, ETH_ZLEN)) {
		netif_wake_queue(dev);
		return 0;
	}

	atomic_dec(&lp->tx_count); 

	/* P is the last sending/sent buffer as a pointer */
	p=lp->tx_ring[head].p;
		
	head = next_tx(head);

	/* NP is the buffer we will be loading */
	np=lp->tx_ring[head].p; 
	
	/* We will need this to flush the buffer out */
	lp->tx_ring[head].skb=skb;

	np->length      = unlikely(skb->len < ETH_ZLEN) ? ETH_ZLEN : skb->len;			
	np->data	= isa_virt_to_bus(skb->data);
	np->status	= 0;
	np->control     = CONTROL_EOP | CONTROL_EOL;     
	wmb();
		
	/*
	 * The new frame has been setup; we can now
	 * let the interrupt handler and card "see" it
	 */

	atomic_set(&lp->tx_ring_head, head); 
	p->control     &= ~CONTROL_EOL;

	netif_wake_queue(dev);
	return 0;
}


/**
 *	mc32_update_stats	-	pull off the on board statistics
 *	@dev: 3c527 to service
 *
 * 
 *	Query and reset the on-card stats. There's the small possibility
 *	of a race here, which would result in an underestimation of
 *	actual errors. As such, we'd prefer to keep all our stats
 *	collection in software. As a rule, we do. However it can't be
 *	used for rx errors and collisions as, by default, the card discards
 *	bad rx packets. 
 *
 *	Setting the SAV BP in the rx filter command supposedly
 *	stops this behaviour. However, testing shows that it only seems to
 *	enable the collation of on-card rx statistics --- the driver
 *	never sees an RX descriptor with an error status set.
 *
 */

static void mc32_update_stats(struct net_device *dev)
{
	struct mc32_local *lp = netdev_priv(dev);
	volatile struct mc32_stats *st = lp->stats; 

	u32 rx_errors=0; 
      
	rx_errors+=lp->net_stats.rx_crc_errors   +=st->rx_crc_errors;         
	                                           st->rx_crc_errors=0;
	rx_errors+=lp->net_stats.rx_fifo_errors  +=st->rx_overrun_errors;   
	                                           st->rx_overrun_errors=0; 
	rx_errors+=lp->net_stats.rx_frame_errors +=st->rx_alignment_errors; 
 	                                           st->rx_alignment_errors=0;
	rx_errors+=lp->net_stats.rx_length_errors+=st->rx_tooshort_errors; 
	                                           st->rx_tooshort_errors=0;
	rx_errors+=lp->net_stats.rx_missed_errors+=st->rx_outofresource_errors;
	                                           st->rx_outofresource_errors=0; 
        lp->net_stats.rx_errors=rx_errors; 
						   
	/* Number of packets which saw one collision */
	lp->net_stats.collisions+=st->dataC[10];
	st->dataC[10]=0; 

	/* Number of packets which saw 2--15 collisions */ 
	lp->net_stats.collisions+=st->dataC[11]; 
	st->dataC[11]=0; 
}	


/**
 *	mc32_rx_ring	-	process the receive ring
 *	@dev: 3c527 that needs its receive ring processing
 *
 *
 *	We have received one or more indications from the card that a
 *	receive has completed. The buffer ring thus contains dirty
 *	entries. We walk the ring by iterating over the circular rx_ring
 *	array, starting at the next dirty buffer (which happens to be the
 *	one we finished up at last time around).
 *
 *	For each completed packet, we will either copy it and pass it up
 * 	the stack or, if the packet is near MTU sized, we allocate
 *	another buffer and flip the old one up the stack.
 * 
 *	We must succeed in keeping a buffer on the ring. If necessary we
 *	will toss a received packet rather than lose a ring entry. Once
 *	the first uncompleted descriptor is found, we move the
 *	End-Of-List bit to include the buffers just processed.
 *
 */

static void mc32_rx_ring(struct net_device *dev)
{
	struct mc32_local *lp = netdev_priv(dev);
	volatile struct skb_header *p;
	u16 rx_ring_tail;
	u16 rx_old_tail;
	int x=0;

	rx_old_tail = rx_ring_tail = lp->rx_ring_tail;
	
	do
	{ 
		p=lp->rx_ring[rx_ring_tail].p; 

		if(!(p->status & (1<<7))) { /* Not COMPLETED */ 
			break;
		} 
		if(p->status & (1<<6)) /* COMPLETED_OK */
		{		        

			u16 length=p->length;
			struct sk_buff *skb; 
			struct sk_buff *newskb; 

			/* Try to save time by avoiding a copy on big frames */

			if ((length > RX_COPYBREAK) 
			    && ((newskb=dev_alloc_skb(1532)) != NULL)) 
			{ 
				skb=lp->rx_ring[rx_ring_tail].skb;
				skb_put(skb, length);
				
				skb_reserve(newskb,18); 
				lp->rx_ring[rx_ring_tail].skb=newskb;  
				p->data=isa_virt_to_bus(newskb->data);  
			} 
			else 
			{
				skb=dev_alloc_skb(length+2);  

				if(skb==NULL) {
					lp->net_stats.rx_dropped++; 
					goto dropped; 
				}

				skb_reserve(skb,2);
				memcpy(skb_put(skb, length),
				       lp->rx_ring[rx_ring_tail].skb->data, length);
			}
			
			skb->protocol=eth_type_trans(skb,dev); 
			skb->dev=dev; 
			dev->last_rx = jiffies;
 			lp->net_stats.rx_packets++; 
 			lp->net_stats.rx_bytes += length; 
			netif_rx(skb);
		}

	dropped:
		p->length = 1532; 
		p->status = 0;
		
		rx_ring_tail=next_rx(rx_ring_tail); 
	}
        while(x++<48);  

	/* If there was actually a frame to be processed, place the EOL bit */ 
	/* at the descriptor prior to the one to be filled next */ 

	if (rx_ring_tail != rx_old_tail) 
	{ 
		lp->rx_ring[prev_rx(rx_ring_tail)].p->control |=  CONTROL_EOL; 
		lp->rx_ring[prev_rx(rx_old_tail)].p->control  &= ~CONTROL_EOL; 

		lp->rx_ring_tail=rx_ring_tail; 
	}
}


/**
 *	mc32_tx_ring	-	process completed transmits
 *	@dev: 3c527 that needs its transmit ring processing
 *
 *
 *	This operates in a similar fashion to mc32_rx_ring. We iterate
 *	over the transmit ring. For each descriptor which has been
 *	processed by the card, we free its associated buffer and note
 *	any errors. This continues until the transmit ring is emptied
 *	or we reach a descriptor that hasn't yet been processed by the
 *	card.
 * 
 */

static void mc32_tx_ring(struct net_device *dev) 
{
	struct mc32_local *lp = netdev_priv(dev);
	volatile struct skb_header *np;

	/*
	 * We rely on head==tail to mean 'queue empty'.
	 * This is why lp->tx_count=TX_RING_LEN-1: in order to prevent
	 * tx_ring_head wrapping to tail and confusing a 'queue empty'
	 * condition with 'queue full'
	 */

	while (lp->tx_ring_tail != atomic_read(&lp->tx_ring_head))  
	{   
		u16 t; 

		t=next_tx(lp->tx_ring_tail); 
		np=lp->tx_ring[t].p; 

		if(!(np->status & (1<<7))) 
		{
			/* Not COMPLETED */ 
			break; 
		} 
		lp->net_stats.tx_packets++;
		if(!(np->status & (1<<6))) /* Not COMPLETED_OK */
		{
			lp->net_stats.tx_errors++;   

			switch(np->status&0x0F)
			{
				case 1:
					lp->net_stats.tx_aborted_errors++;
					break; /* Max collisions */ 
				case 2:
					lp->net_stats.tx_fifo_errors++;
					break;
				case 3:
					lp->net_stats.tx_carrier_errors++;
					break;
				case 4:
					lp->net_stats.tx_window_errors++;
					break;  /* CTS Lost */ 
				case 5:
					lp->net_stats.tx_aborted_errors++;
					break; /* Transmit timeout */ 
			}
		}
		/* Packets are sent in order - this is
		    basically a FIFO queue of buffers matching
		    the card ring */
		lp->net_stats.tx_bytes+=lp->tx_ring[t].skb->len;
		dev_kfree_skb_irq(lp->tx_ring[t].skb);
		lp->tx_ring[t].skb=NULL;
		atomic_inc(&lp->tx_count);
		netif_wake_queue(dev);

		lp->tx_ring_tail=t; 
	}

} 


/**
 *	mc32_interrupt		-	handle an interrupt from a 3c527
 *	@irq: Interrupt number
 *	@dev_id: 3c527 that requires servicing
 *	@regs: Registers (unused)
 *
 *
 *	An interrupt is raised whenever the 3c527 writes to the command
 *	register. This register contains the message it wishes to send us
 *	packed into a single byte field. We keep reading status entries
 *	until we have processed all the control items, but simply count
 *	transmit and receive reports. When all reports are in we empty the
 *	transceiver rings as appropriate. This saves the overhead of
 *	multiple command requests.
 *
 *	Because MCA is level-triggered, we shouldn't miss indications.
 *	Therefore, we needn't ask the card to suspend interrupts within
 *	this handler. The card receives an implicit acknowledgment of the
 *	current interrupt when we read the command register.
 *
 */

static irqreturn_t mc32_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct net_device *dev = dev_id;
	struct mc32_local *lp;
	int ioaddr, status, boguscount = 0;
	int rx_event = 0;
	int tx_event = 0; 
	
	if (dev == NULL) {
		printk(KERN_WARNING "%s: irq %d for unknown device.\n", cardname, irq);
		return IRQ_NONE;
	}
 
	ioaddr = dev->base_addr;
	lp = netdev_priv(dev);

	/* See whats cooking */

	while((inb(ioaddr+HOST_STATUS)&HOST_STATUS_CWR) && boguscount++<2000)
	{
		status=inb(ioaddr+HOST_CMD);

#ifdef DEBUG_IRQ		
		printk("Status TX%d RX%d EX%d OV%d BC%d\n",
			(status&7), (status>>3)&7, (status>>6)&1,
			(status>>7)&1, boguscount);
#endif
			
		switch(status&7)
		{
			case 0:
				break;
			case 6: /* TX fail */
			case 2:	/* TX ok */
				tx_event = 1; 
				break;
			case 3: /* Halt */
			case 4: /* Abort */
				complete(&lp->xceiver_cmd);
				break;
			default:
				printk("%s: strange tx ack %d\n", dev->name, status&7);
		}
		status>>=3;
		switch(status&7)
		{
			case 0:
				break;
			case 2:	/* RX */
				rx_event=1; 
				break;
			case 3: /* Halt */
			case 4: /* Abort */
				complete(&lp->xceiver_cmd);
				break;
			case 6:
				/* Out of RX buffers stat */
				/* Must restart rx */
				lp->net_stats.rx_dropped++;
				mc32_rx_ring(dev); 
				mc32_start_transceiver(dev); 
				break;
			default:
				printk("%s: strange rx ack %d\n", 
					dev->name, status&7);			
		}
		status>>=3;
		if(status&1)
		{
			/*
			 * No thread is waiting: we need to tidy
			 * up ourself.
			 */
				   
			if (lp->cmd_nonblocking) {
				up(&lp->cmd_mutex);
				if (lp->mc_reload_wait) 
					mc32_reset_multicast_list(dev);
			}
			else complete(&lp->execution_cmd);
		}
		if(status&2)
		{
			/*
			 *	We get interrupted once per
			 *	counter that is about to overflow. 
			 */

			mc32_update_stats(dev);			
		}
	}


	/*
	 *	Process the transmit and receive rings 
         */

	if(tx_event) 
		mc32_tx_ring(dev);
	 
	if(rx_event) 
		mc32_rx_ring(dev);

	return IRQ_HANDLED;
}


/**
 *	mc32_close	-	user configuring the 3c527 down
 *	@dev: 3c527 card to shut down
 *
 *	The 3c527 is a bus mastering device. We must be careful how we
 *	shut it down. It may also be running shared interrupt so we have
 *	to be sure to silence it properly
 *
 *	We indicate that the card is closing to the rest of the
 *	driver.  Otherwise, it is possible that the card may run out
 *	of receive buffers and restart the transceiver while we're
 *	trying to close it.
 * 
 *	We abort any receive and transmits going on and then wait until
 *	any pending exec commands have completed in other code threads.
 *	In theory we can't get here while that is true, in practice I am
 *	paranoid
 *
 *	We turn off the interrupt enable for the board to be sure it can't
 *	intefere with other devices.
 */

static int mc32_close(struct net_device *dev)
{
	struct mc32_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;

	u8 regs;
	u16 one=1;
	
	lp->xceiver_desired_state = HALTED;
	netif_stop_queue(dev);

	/*
	 *	Send the indications on command (handy debug check)
	 */

	mc32_command(dev, 4, &one, 2);

	/* Shut down the transceiver */

	mc32_halt_transceiver(dev); 
	
	/* Ensure we issue no more commands beyond this point */

	down(&lp->cmd_mutex);
	
	/* Ok the card is now stopping */	
	
	regs=inb(ioaddr+HOST_CTRL);
	regs&=~HOST_CTRL_INTE;
	outb(regs, ioaddr+HOST_CTRL);

	mc32_flush_rx_ring(dev);
	mc32_flush_tx_ring(dev);
		
	mc32_update_stats(dev); 

	return 0;
}


/**
 *	mc32_get_stats		-	hand back stats to network layer
 *	@dev: The 3c527 card to handle
 *
 *	We've collected all the stats we can in software already. Now
 *	it's time to update those kept on-card and return the lot. 
 * 
 */

static struct net_device_stats *mc32_get_stats(struct net_device *dev)
{
	struct mc32_local *lp = netdev_priv(dev);
	
	mc32_update_stats(dev); 
	return &lp->net_stats;
}


/**
 *	do_mc32_set_multicast_list	-	attempt to update multicasts
 *	@dev: 3c527 device to load the list on
 *	@retry: indicates this is not the first call. 
 *
 *
 * 	Actually set or clear the multicast filter for this adaptor. The
 *	locking issues are handled by this routine. We have to track
 *	state as it may take multiple calls to get the command sequence
 *	completed. We just keep trying to schedule the loads until we
 *	manage to process them all.
 * 
 *	num_addrs == -1	Promiscuous mode, receive all packets
 * 
 *	num_addrs == 0	Normal mode, clear multicast list
 * 
 *	num_addrs > 0	Multicast mode, receive normal and MC packets, 
 *			and do best-effort filtering. 
 *
 *	See mc32_update_stats() regards setting the SAV BP bit. 
 *
 */

static void do_mc32_set_multicast_list(struct net_device *dev, int retry)
{
	struct mc32_local *lp = netdev_priv(dev);
	u16 filt = (1<<2); /* Save Bad Packets, for stats purposes */ 

	if (dev->flags&IFF_PROMISC)
		/* Enable promiscuous mode */
		filt |= 1;
	else if((dev->flags&IFF_ALLMULTI) || dev->mc_count > 10)
	{
		dev->flags|=IFF_PROMISC;
		filt |= 1;
	}
	else if(dev->mc_count)
	{
		unsigned char block[62];
		unsigned char *bp;
		struct dev_mc_list *dmc=dev->mc_list;
		
		int i;
	       
		if(retry==0)
			lp->mc_list_valid = 0;
		if(!lp->mc_list_valid)
		{
			block[1]=0;
			block[0]=dev->mc_count;
			bp=block+2;
		
			for(i=0;i<dev->mc_count;i++)
			{
				memcpy(bp, dmc->dmi_addr, 6);
				bp+=6;
				dmc=dmc->next;
			}
			if(mc32_command_nowait(dev, 2, block, 2+6*dev->mc_count)==-1)
			{
				lp->mc_reload_wait = 1;
				return;
			}
			lp->mc_list_valid=1;
		}
	}
	
	if(mc32_command_nowait(dev, 0, &filt, 2)==-1) 
	{
		lp->mc_reload_wait = 1;
	} 
	else { 
		lp->mc_reload_wait = 0;
	}
}


/**
 *	mc32_set_multicast_list	-	queue multicast list update
 *	@dev: The 3c527 to use
 *
 *	Commence loading the multicast list. This is called when the kernel
 *	changes the lists. It will override any pending list we are trying to
 *	load.
 */

static void mc32_set_multicast_list(struct net_device *dev)
{
	do_mc32_set_multicast_list(dev,0);
}


/**
 *	mc32_reset_multicast_list	-	reset multicast list
 *	@dev: The 3c527 to use
 *
 *	Attempt the next step in loading the multicast lists. If this attempt
 *	fails to complete then it will be scheduled and this function called
 *	again later from elsewhere.
 */

static void mc32_reset_multicast_list(struct net_device *dev)
{
	do_mc32_set_multicast_list(dev,1);
}

static void netdev_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	sprintf(info->bus_info, "MCA 0x%lx", dev->base_addr);
}

static u32 netdev_get_msglevel(struct net_device *dev)
{
	return mc32_debug;
}

static void netdev_set_msglevel(struct net_device *dev, u32 level)
{
	mc32_debug = level;
}

static struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
	.get_msglevel		= netdev_get_msglevel,
	.set_msglevel		= netdev_set_msglevel,
};

#ifdef MODULE

static struct net_device *this_device;

/**
 *	init_module		-	entry point
 *
 *	Probe and locate a 3c527 card. This really should probe and locate
 *	all the 3c527 cards in the machine not just one of them. Yes you can
 *	insmod multiple modules for now but it's a hack.
 */

int __init init_module(void)
{
	this_device = mc32_probe(-1);
	if (IS_ERR(this_device))
		return PTR_ERR(this_device);
	return 0;
}

/**
 *	cleanup_module	-	free resources for an unload
 *
 *	Unloading time. We release the MCA bus resources and the interrupt
 *	at which point everything is ready to unload. The card must be stopped
 *	at this point or we would not have been called. When we unload we
 *	leave the card stopped but not totally shut down. When the card is
 *	initialized it must be rebooted or the rings reloaded before any
 *	transmit operations are allowed to start scribbling into memory.
 */

void cleanup_module(void)
{
	unregister_netdev(this_device);
	cleanup_card(this_device);
	free_netdev(this_device);
}

#endif /* MODULE */
