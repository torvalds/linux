// SPDX-License-Identifier: GPL-2.0-or-later
/* $Id: plip.c,v 1.3.6.2 1997/04/16 15:07:56 phil Exp $ */
/* PLIP: A parallel port "network" driver for Linux. */
/* This driver is for parallel port with 5-bit cable (LapLink (R) cable). */
/*
 * Authors:	Donald Becker <becker@scyld.com>
 *		Tommy Thorn <thorn@daimi.aau.dk>
 *		Tanabe Hiroyasu <hiro@sanpo.t.u-tokyo.ac.jp>
 *		Alan Cox <gw4pts@gw4pts.ampr.org>
 *		Peter Bauer <100136.3530@compuserve.com>
 *		Niibe Yutaka <gniibe@mri.co.jp>
 *		Nimrod Zimerman <zimerman@mailandnews.com>
 *
 * Enhancements:
 *		Modularization and ifreq/ifmap support by Alan Cox.
 *		Rewritten by Niibe Yutaka.
 *		parport-sharing awareness code by Philip Blundell.
 *		SMP locking by Niibe Yutaka.
 *		Support for parallel ports with no IRQ (poll mode),
 *		Modifications to use the parallel port API
 *		by Nimrod Zimerman.
 *
 * Fixes:
 *		Niibe Yutaka
 *		  - Module initialization.
 *		  - MTU fix.
 *		  - Make sure other end is OK, before sending a packet.
 *		  - Fix immediate timer problem.
 *
 *		Al Viro
 *		  - Changed {enable,disable}_irq handling to make it work
 *		    with new ("stack") semantics.
 */

/*
 * Original version and the name 'PLIP' from Donald Becker <becker@scyld.com>
 * inspired by Russ Nelson's parallel port packet driver.
 *
 * NOTE:
 *     Tanabe Hiroyasu had changed the protocol, and it was in Linux v1.0.
 *     Because of the necessity to communicate to DOS machines with the
 *     Crynwr packet driver, Peter Bauer changed the protocol again
 *     back to original protocol.
 *
 *     This version follows original PLIP protocol.
 *     So, this PLIP can't communicate the PLIP of Linux v1.0.
 */

/*
 *     To use with DOS box, please do (Turn on ARP switch):
 *	# ifconfig plip[0-2] arp
 */
static const char version[] = "NET3 PLIP version 2.4-parport gniibe@mri.co.jp\n";

/*
  Sources:
	Ideas and protocols came from Russ Nelson's <nelson@crynwr.com>
	"parallel.asm" parallel port packet driver.

  The "Crynwr" parallel port standard specifies the following protocol:
    Trigger by sending nibble '0x8' (this causes interrupt on other end)
    count-low octet
    count-high octet
    ... data octets
    checksum octet
  Each octet is sent as <wait for rx. '0x1?'> <send 0x10+(octet&0x0F)>
			<wait for rx. '0x0?'> <send 0x00+((octet>>4)&0x0F)>

  The packet is encapsulated as if it were ethernet.

  The cable used is a de facto standard parallel null cable -- sold as
  a "LapLink" cable by various places.  You'll need a 12-conductor cable to
  make one yourself.  The wiring is:
    SLCTIN	17 - 17
    GROUND	25 - 25
    D0->ERROR	2 - 15		15 - 2
    D1->SLCT	3 - 13		13 - 3
    D2->PAPOUT	4 - 12		12 - 4
    D3->ACK	5 - 10		10 - 5
    D4->BUSY	6 - 11		11 - 6
  Do not connect the other pins.  They are
    D5,D6,D7 are 7,8,9
    STROBE is 1, FEED is 14, INIT is 16
    extra grounds are 18,19,20,21,22,23,24
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/inetdevice.h>
#include <linux/skbuff.h>
#include <linux/if_plip.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/parport.h>
#include <linux/bitops.h>

#include <net/neighbour.h>

#include <asm/irq.h>
#include <asm/byteorder.h>

/* Maximum number of devices to support. */
#define PLIP_MAX  8

/* Use 0 for production, 1 for verification, >2 for debug */
#ifndef NET_DEBUG
#define NET_DEBUG 1
#endif
static const unsigned int net_debug = NET_DEBUG;

#define ENABLE(irq)  if (irq != -1) enable_irq(irq)
#define DISABLE(irq) if (irq != -1) disable_irq(irq)

/* In micro second */
#define PLIP_DELAY_UNIT		   1

/* Connection time out = PLIP_TRIGGER_WAIT * PLIP_DELAY_UNIT usec */
#define PLIP_TRIGGER_WAIT	 500

/* Nibble time out = PLIP_NIBBLE_WAIT * PLIP_DELAY_UNIT usec */
#define PLIP_NIBBLE_WAIT        3000

/* Bottom halves */
static void plip_kick_bh(struct work_struct *work);
static void plip_bh(struct work_struct *work);
static void plip_timer_bh(struct work_struct *work);

/* Interrupt handler */
static void plip_interrupt(void *dev_id);

/* Functions for DEV methods */
static netdev_tx_t plip_tx_packet(struct sk_buff *skb, struct net_device *dev);
static int plip_hard_header(struct sk_buff *skb, struct net_device *dev,
                            unsigned short type, const void *daddr,
			    const void *saddr, unsigned len);
static int plip_hard_header_cache(const struct neighbour *neigh,
                                  struct hh_cache *hh, __be16 type);
static int plip_open(struct net_device *dev);
static int plip_close(struct net_device *dev);
static int plip_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
static int plip_preempt(void *handle);
static void plip_wakeup(void *handle);

enum plip_connection_state {
	PLIP_CN_NONE=0,
	PLIP_CN_RECEIVE,
	PLIP_CN_SEND,
	PLIP_CN_CLOSING,
	PLIP_CN_ERROR
};

enum plip_packet_state {
	PLIP_PK_DONE=0,
	PLIP_PK_TRIGGER,
	PLIP_PK_LENGTH_LSB,
	PLIP_PK_LENGTH_MSB,
	PLIP_PK_DATA,
	PLIP_PK_CHECKSUM
};

enum plip_nibble_state {
	PLIP_NB_BEGIN,
	PLIP_NB_1,
	PLIP_NB_2,
};

struct plip_local {
	enum plip_packet_state state;
	enum plip_nibble_state nibble;
	union {
		struct {
#if defined(__LITTLE_ENDIAN)
			unsigned char lsb;
			unsigned char msb;
#elif defined(__BIG_ENDIAN)
			unsigned char msb;
			unsigned char lsb;
#else
#error	"Please fix the endianness defines in <asm/byteorder.h>"
#endif
		} b;
		unsigned short h;
	} length;
	unsigned short byte;
	unsigned char  checksum;
	unsigned char  data;
	struct sk_buff *skb;
};

struct net_local {
	struct net_device *dev;
	struct work_struct immediate;
	struct delayed_work deferred;
	struct delayed_work timer;
	struct plip_local snd_data;
	struct plip_local rcv_data;
	struct pardevice *pardev;
	unsigned long  trigger;
	unsigned long  nibble;
	enum plip_connection_state connection;
	unsigned short timeout_count;
	int is_deferred;
	int port_owner;
	int should_relinquish;
	spinlock_t lock;
	atomic_t kill_timer;
	struct completion killed_timer_cmp;
};

static inline void enable_parport_interrupts (struct net_device *dev)
{
	if (dev->irq != -1)
	{
		struct parport *port =
		   ((struct net_local *)netdev_priv(dev))->pardev->port;
		port->ops->enable_irq (port);
	}
}

static inline void disable_parport_interrupts (struct net_device *dev)
{
	if (dev->irq != -1)
	{
		struct parport *port =
		   ((struct net_local *)netdev_priv(dev))->pardev->port;
		port->ops->disable_irq (port);
	}
}

static inline void write_data (struct net_device *dev, unsigned char data)
{
	struct parport *port =
	   ((struct net_local *)netdev_priv(dev))->pardev->port;

	port->ops->write_data (port, data);
}

static inline unsigned char read_status (struct net_device *dev)
{
	struct parport *port =
	   ((struct net_local *)netdev_priv(dev))->pardev->port;

	return port->ops->read_status (port);
}

static const struct header_ops plip_header_ops = {
	.create	= plip_hard_header,
	.cache  = plip_hard_header_cache,
};

static const struct net_device_ops plip_netdev_ops = {
	.ndo_open		 = plip_open,
	.ndo_stop		 = plip_close,
	.ndo_start_xmit		 = plip_tx_packet,
	.ndo_do_ioctl		 = plip_ioctl,
	.ndo_set_mac_address	 = eth_mac_addr,
	.ndo_validate_addr	 = eth_validate_addr,
};

/* Entry point of PLIP driver.
   Probe the hardware, and register/initialize the driver.

   PLIP is rather weird, because of the way it interacts with the parport
   system.  It is _not_ initialised from Space.c.  Instead, plip_init()
   is called, and that function makes up a "struct net_device" for each port, and
   then calls us here.

   */
static void
plip_init_netdev(struct net_device *dev)
{
	struct net_local *nl = netdev_priv(dev);

	/* Then, override parts of it */
	dev->tx_queue_len 	 = 10;
	dev->flags	         = IFF_POINTOPOINT|IFF_NOARP;
	memset(dev->dev_addr, 0xfc, ETH_ALEN);

	dev->netdev_ops		 = &plip_netdev_ops;
	dev->header_ops          = &plip_header_ops;


	nl->port_owner = 0;

	/* Initialize constants */
	nl->trigger	= PLIP_TRIGGER_WAIT;
	nl->nibble	= PLIP_NIBBLE_WAIT;

	/* Initialize task queue structures */
	INIT_WORK(&nl->immediate, plip_bh);
	INIT_DELAYED_WORK(&nl->deferred, plip_kick_bh);

	if (dev->irq == -1)
		INIT_DELAYED_WORK(&nl->timer, plip_timer_bh);

	spin_lock_init(&nl->lock);
}

/* Bottom half handler for the delayed request.
   This routine is kicked by do_timer().
   Request `plip_bh' to be invoked. */
static void
plip_kick_bh(struct work_struct *work)
{
	struct net_local *nl =
		container_of(work, struct net_local, deferred.work);

	if (nl->is_deferred)
		schedule_work(&nl->immediate);
}

/* Forward declarations of internal routines */
static int plip_none(struct net_device *, struct net_local *,
		     struct plip_local *, struct plip_local *);
static int plip_receive_packet(struct net_device *, struct net_local *,
			       struct plip_local *, struct plip_local *);
static int plip_send_packet(struct net_device *, struct net_local *,
			    struct plip_local *, struct plip_local *);
static int plip_connection_close(struct net_device *, struct net_local *,
				 struct plip_local *, struct plip_local *);
static int plip_error(struct net_device *, struct net_local *,
		      struct plip_local *, struct plip_local *);
static int plip_bh_timeout_error(struct net_device *dev, struct net_local *nl,
				 struct plip_local *snd,
				 struct plip_local *rcv,
				 int error);

#define OK        0
#define TIMEOUT   1
#define ERROR     2
#define HS_TIMEOUT	3

typedef int (*plip_func)(struct net_device *dev, struct net_local *nl,
			 struct plip_local *snd, struct plip_local *rcv);

static const plip_func connection_state_table[] =
{
	plip_none,
	plip_receive_packet,
	plip_send_packet,
	plip_connection_close,
	plip_error
};

/* Bottom half handler of PLIP. */
static void
plip_bh(struct work_struct *work)
{
	struct net_local *nl = container_of(work, struct net_local, immediate);
	struct plip_local *snd = &nl->snd_data;
	struct plip_local *rcv = &nl->rcv_data;
	plip_func f;
	int r;

	nl->is_deferred = 0;
	f = connection_state_table[nl->connection];
	if ((r = (*f)(nl->dev, nl, snd, rcv)) != OK &&
	    (r = plip_bh_timeout_error(nl->dev, nl, snd, rcv, r)) != OK) {
		nl->is_deferred = 1;
		schedule_delayed_work(&nl->deferred, 1);
	}
}

static void
plip_timer_bh(struct work_struct *work)
{
	struct net_local *nl =
		container_of(work, struct net_local, timer.work);

	if (!(atomic_read (&nl->kill_timer))) {
		plip_interrupt (nl->dev);

		schedule_delayed_work(&nl->timer, 1);
	}
	else {
		complete(&nl->killed_timer_cmp);
	}
}

static int
plip_bh_timeout_error(struct net_device *dev, struct net_local *nl,
		      struct plip_local *snd, struct plip_local *rcv,
		      int error)
{
	unsigned char c0;
	/*
	 * This is tricky. If we got here from the beginning of send (either
	 * with ERROR or HS_TIMEOUT) we have IRQ enabled. Otherwise it's
	 * already disabled. With the old variant of {enable,disable}_irq()
	 * extra disable_irq() was a no-op. Now it became mortal - it's
	 * unbalanced and thus we'll never re-enable IRQ (until rmmod plip,
	 * that is). So we have to treat HS_TIMEOUT and ERROR from send
	 * in a special way.
	 */

	spin_lock_irq(&nl->lock);
	if (nl->connection == PLIP_CN_SEND) {

		if (error != ERROR) { /* Timeout */
			nl->timeout_count++;
			if ((error == HS_TIMEOUT && nl->timeout_count <= 10) ||
			    nl->timeout_count <= 3) {
				spin_unlock_irq(&nl->lock);
				/* Try again later */
				return TIMEOUT;
			}
			c0 = read_status(dev);
			printk(KERN_WARNING "%s: transmit timeout(%d,%02x)\n",
			       dev->name, snd->state, c0);
		} else
			error = HS_TIMEOUT;
		dev->stats.tx_errors++;
		dev->stats.tx_aborted_errors++;
	} else if (nl->connection == PLIP_CN_RECEIVE) {
		if (rcv->state == PLIP_PK_TRIGGER) {
			/* Transmission was interrupted. */
			spin_unlock_irq(&nl->lock);
			return OK;
		}
		if (error != ERROR) { /* Timeout */
			if (++nl->timeout_count <= 3) {
				spin_unlock_irq(&nl->lock);
				/* Try again later */
				return TIMEOUT;
			}
			c0 = read_status(dev);
			printk(KERN_WARNING "%s: receive timeout(%d,%02x)\n",
			       dev->name, rcv->state, c0);
		}
		dev->stats.rx_dropped++;
	}
	rcv->state = PLIP_PK_DONE;
	if (rcv->skb) {
		kfree_skb(rcv->skb);
		rcv->skb = NULL;
	}
	snd->state = PLIP_PK_DONE;
	if (snd->skb) {
		dev_kfree_skb(snd->skb);
		snd->skb = NULL;
	}
	spin_unlock_irq(&nl->lock);
	if (error == HS_TIMEOUT) {
		DISABLE(dev->irq);
		synchronize_irq(dev->irq);
	}
	disable_parport_interrupts (dev);
	netif_stop_queue (dev);
	nl->connection = PLIP_CN_ERROR;
	write_data (dev, 0x00);

	return TIMEOUT;
}

static int
plip_none(struct net_device *dev, struct net_local *nl,
	  struct plip_local *snd, struct plip_local *rcv)
{
	return OK;
}

/* PLIP_RECEIVE --- receive a byte(two nibbles)
   Returns OK on success, TIMEOUT on timeout */
static inline int
plip_receive(unsigned short nibble_timeout, struct net_device *dev,
	     enum plip_nibble_state *ns_p, unsigned char *data_p)
{
	unsigned char c0, c1;
	unsigned int cx;

	switch (*ns_p) {
	case PLIP_NB_BEGIN:
		cx = nibble_timeout;
		while (1) {
			c0 = read_status(dev);
			udelay(PLIP_DELAY_UNIT);
			if ((c0 & 0x80) == 0) {
				c1 = read_status(dev);
				if (c0 == c1)
					break;
			}
			if (--cx == 0)
				return TIMEOUT;
		}
		*data_p = (c0 >> 3) & 0x0f;
		write_data (dev, 0x10); /* send ACK */
		*ns_p = PLIP_NB_1;
		fallthrough;

	case PLIP_NB_1:
		cx = nibble_timeout;
		while (1) {
			c0 = read_status(dev);
			udelay(PLIP_DELAY_UNIT);
			if (c0 & 0x80) {
				c1 = read_status(dev);
				if (c0 == c1)
					break;
			}
			if (--cx == 0)
				return TIMEOUT;
		}
		*data_p |= (c0 << 1) & 0xf0;
		write_data (dev, 0x00); /* send ACK */
		*ns_p = PLIP_NB_BEGIN;
		break;
	case PLIP_NB_2:
		break;
	}
	return OK;
}

/*
 *	Determine the packet's protocol ID. The rule here is that we
 *	assume 802.3 if the type field is short enough to be a length.
 *	This is normal practice and works for any 'now in use' protocol.
 *
 *	PLIP is ethernet ish but the daddr might not be valid if unicast.
 *	PLIP fortunately has no bus architecture (its Point-to-point).
 *
 *	We can't fix the daddr thing as that quirk (more bug) is embedded
 *	in far too many old systems not all even running Linux.
 */

static __be16 plip_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	struct ethhdr *eth;
	unsigned char *rawp;

	skb_reset_mac_header(skb);
	skb_pull(skb,dev->hard_header_len);
	eth = eth_hdr(skb);

	if(is_multicast_ether_addr(eth->h_dest))
	{
		if(ether_addr_equal_64bits(eth->h_dest, dev->broadcast))
			skb->pkt_type=PACKET_BROADCAST;
		else
			skb->pkt_type=PACKET_MULTICAST;
	}

	/*
	 *	This ALLMULTI check should be redundant by 1.4
	 *	so don't forget to remove it.
	 */

	if (ntohs(eth->h_proto) >= ETH_P_802_3_MIN)
		return eth->h_proto;

	rawp = skb->data;

	/*
	 *	This is a magic hack to spot IPX packets. Older Novell breaks
	 *	the protocol design and runs IPX over 802.3 without an 802.2 LLC
	 *	layer. We look for FFFF which isn't a used 802.2 SSAP/DSAP. This
	 *	won't work for fault tolerant netware but does for the rest.
	 */
	if (*(unsigned short *)rawp == 0xFFFF)
		return htons(ETH_P_802_3);

	/*
	 *	Real 802.2 LLC
	 */
	return htons(ETH_P_802_2);
}

/* PLIP_RECEIVE_PACKET --- receive a packet */
static int
plip_receive_packet(struct net_device *dev, struct net_local *nl,
		    struct plip_local *snd, struct plip_local *rcv)
{
	unsigned short nibble_timeout = nl->nibble;
	unsigned char *lbuf;

	switch (rcv->state) {
	case PLIP_PK_TRIGGER:
		DISABLE(dev->irq);
		/* Don't need to synchronize irq, as we can safely ignore it */
		disable_parport_interrupts (dev);
		write_data (dev, 0x01); /* send ACK */
		if (net_debug > 2)
			printk(KERN_DEBUG "%s: receive start\n", dev->name);
		rcv->state = PLIP_PK_LENGTH_LSB;
		rcv->nibble = PLIP_NB_BEGIN;
		fallthrough;

	case PLIP_PK_LENGTH_LSB:
		if (snd->state != PLIP_PK_DONE) {
			if (plip_receive(nl->trigger, dev,
					 &rcv->nibble, &rcv->length.b.lsb)) {
				/* collision, here dev->tbusy == 1 */
				rcv->state = PLIP_PK_DONE;
				nl->is_deferred = 1;
				nl->connection = PLIP_CN_SEND;
				schedule_delayed_work(&nl->deferred, 1);
				enable_parport_interrupts (dev);
				ENABLE(dev->irq);
				return OK;
			}
		} else {
			if (plip_receive(nibble_timeout, dev,
					 &rcv->nibble, &rcv->length.b.lsb))
				return TIMEOUT;
		}
		rcv->state = PLIP_PK_LENGTH_MSB;
		fallthrough;

	case PLIP_PK_LENGTH_MSB:
		if (plip_receive(nibble_timeout, dev,
				 &rcv->nibble, &rcv->length.b.msb))
			return TIMEOUT;
		if (rcv->length.h > dev->mtu + dev->hard_header_len ||
		    rcv->length.h < 8) {
			printk(KERN_WARNING "%s: bogus packet size %d.\n", dev->name, rcv->length.h);
			return ERROR;
		}
		/* Malloc up new buffer. */
		rcv->skb = dev_alloc_skb(rcv->length.h + 2);
		if (rcv->skb == NULL) {
			printk(KERN_ERR "%s: Memory squeeze.\n", dev->name);
			return ERROR;
		}
		skb_reserve(rcv->skb, 2);	/* Align IP on 16 byte boundaries */
		skb_put(rcv->skb,rcv->length.h);
		rcv->skb->dev = dev;
		rcv->state = PLIP_PK_DATA;
		rcv->byte = 0;
		rcv->checksum = 0;
		fallthrough;

	case PLIP_PK_DATA:
		lbuf = rcv->skb->data;
		do {
			if (plip_receive(nibble_timeout, dev,
					 &rcv->nibble, &lbuf[rcv->byte]))
				return TIMEOUT;
		} while (++rcv->byte < rcv->length.h);
		do {
			rcv->checksum += lbuf[--rcv->byte];
		} while (rcv->byte);
		rcv->state = PLIP_PK_CHECKSUM;
		fallthrough;

	case PLIP_PK_CHECKSUM:
		if (plip_receive(nibble_timeout, dev,
				 &rcv->nibble, &rcv->data))
			return TIMEOUT;
		if (rcv->data != rcv->checksum) {
			dev->stats.rx_crc_errors++;
			if (net_debug)
				printk(KERN_DEBUG "%s: checksum error\n", dev->name);
			return ERROR;
		}
		rcv->state = PLIP_PK_DONE;
		fallthrough;

	case PLIP_PK_DONE:
		/* Inform the upper layer for the arrival of a packet. */
		rcv->skb->protocol=plip_type_trans(rcv->skb, dev);
		netif_rx_ni(rcv->skb);
		dev->stats.rx_bytes += rcv->length.h;
		dev->stats.rx_packets++;
		rcv->skb = NULL;
		if (net_debug > 2)
			printk(KERN_DEBUG "%s: receive end\n", dev->name);

		/* Close the connection. */
		write_data (dev, 0x00);
		spin_lock_irq(&nl->lock);
		if (snd->state != PLIP_PK_DONE) {
			nl->connection = PLIP_CN_SEND;
			spin_unlock_irq(&nl->lock);
			schedule_work(&nl->immediate);
			enable_parport_interrupts (dev);
			ENABLE(dev->irq);
			return OK;
		} else {
			nl->connection = PLIP_CN_NONE;
			spin_unlock_irq(&nl->lock);
			enable_parport_interrupts (dev);
			ENABLE(dev->irq);
			return OK;
		}
	}
	return OK;
}

/* PLIP_SEND --- send a byte (two nibbles)
   Returns OK on success, TIMEOUT when timeout    */
static inline int
plip_send(unsigned short nibble_timeout, struct net_device *dev,
	  enum plip_nibble_state *ns_p, unsigned char data)
{
	unsigned char c0;
	unsigned int cx;

	switch (*ns_p) {
	case PLIP_NB_BEGIN:
		write_data (dev, data & 0x0f);
		*ns_p = PLIP_NB_1;
		fallthrough;

	case PLIP_NB_1:
		write_data (dev, 0x10 | (data & 0x0f));
		cx = nibble_timeout;
		while (1) {
			c0 = read_status(dev);
			if ((c0 & 0x80) == 0)
				break;
			if (--cx == 0)
				return TIMEOUT;
			udelay(PLIP_DELAY_UNIT);
		}
		write_data (dev, 0x10 | (data >> 4));
		*ns_p = PLIP_NB_2;
		fallthrough;

	case PLIP_NB_2:
		write_data (dev, (data >> 4));
		cx = nibble_timeout;
		while (1) {
			c0 = read_status(dev);
			if (c0 & 0x80)
				break;
			if (--cx == 0)
				return TIMEOUT;
			udelay(PLIP_DELAY_UNIT);
		}
		*ns_p = PLIP_NB_BEGIN;
		return OK;
	}
	return OK;
}

/* PLIP_SEND_PACKET --- send a packet */
static int
plip_send_packet(struct net_device *dev, struct net_local *nl,
		 struct plip_local *snd, struct plip_local *rcv)
{
	unsigned short nibble_timeout = nl->nibble;
	unsigned char *lbuf;
	unsigned char c0;
	unsigned int cx;

	if (snd->skb == NULL || (lbuf = snd->skb->data) == NULL) {
		printk(KERN_DEBUG "%s: send skb lost\n", dev->name);
		snd->state = PLIP_PK_DONE;
		snd->skb = NULL;
		return ERROR;
	}

	switch (snd->state) {
	case PLIP_PK_TRIGGER:
		if ((read_status(dev) & 0xf8) != 0x80)
			return HS_TIMEOUT;

		/* Trigger remote rx interrupt. */
		write_data (dev, 0x08);
		cx = nl->trigger;
		while (1) {
			udelay(PLIP_DELAY_UNIT);
			spin_lock_irq(&nl->lock);
			if (nl->connection == PLIP_CN_RECEIVE) {
				spin_unlock_irq(&nl->lock);
				/* Interrupted. */
				dev->stats.collisions++;
				return OK;
			}
			c0 = read_status(dev);
			if (c0 & 0x08) {
				spin_unlock_irq(&nl->lock);
				DISABLE(dev->irq);
				synchronize_irq(dev->irq);
				if (nl->connection == PLIP_CN_RECEIVE) {
					/* Interrupted.
					   We don't need to enable irq,
					   as it is soon disabled.    */
					/* Yes, we do. New variant of
					   {enable,disable}_irq *counts*
					   them.  -- AV  */
					ENABLE(dev->irq);
					dev->stats.collisions++;
					return OK;
				}
				disable_parport_interrupts (dev);
				if (net_debug > 2)
					printk(KERN_DEBUG "%s: send start\n", dev->name);
				snd->state = PLIP_PK_LENGTH_LSB;
				snd->nibble = PLIP_NB_BEGIN;
				nl->timeout_count = 0;
				break;
			}
			spin_unlock_irq(&nl->lock);
			if (--cx == 0) {
				write_data (dev, 0x00);
				return HS_TIMEOUT;
			}
		}
		break;

	case PLIP_PK_LENGTH_LSB:
		if (plip_send(nibble_timeout, dev,
			      &snd->nibble, snd->length.b.lsb))
			return TIMEOUT;
		snd->state = PLIP_PK_LENGTH_MSB;
		fallthrough;

	case PLIP_PK_LENGTH_MSB:
		if (plip_send(nibble_timeout, dev,
			      &snd->nibble, snd->length.b.msb))
			return TIMEOUT;
		snd->state = PLIP_PK_DATA;
		snd->byte = 0;
		snd->checksum = 0;
		fallthrough;

	case PLIP_PK_DATA:
		do {
			if (plip_send(nibble_timeout, dev,
				      &snd->nibble, lbuf[snd->byte]))
				return TIMEOUT;
		} while (++snd->byte < snd->length.h);
		do {
			snd->checksum += lbuf[--snd->byte];
		} while (snd->byte);
		snd->state = PLIP_PK_CHECKSUM;
		fallthrough;

	case PLIP_PK_CHECKSUM:
		if (plip_send(nibble_timeout, dev,
			      &snd->nibble, snd->checksum))
			return TIMEOUT;

		dev->stats.tx_bytes += snd->skb->len;
		dev_kfree_skb(snd->skb);
		dev->stats.tx_packets++;
		snd->state = PLIP_PK_DONE;
		fallthrough;

	case PLIP_PK_DONE:
		/* Close the connection */
		write_data (dev, 0x00);
		snd->skb = NULL;
		if (net_debug > 2)
			printk(KERN_DEBUG "%s: send end\n", dev->name);
		nl->connection = PLIP_CN_CLOSING;
		nl->is_deferred = 1;
		schedule_delayed_work(&nl->deferred, 1);
		enable_parport_interrupts (dev);
		ENABLE(dev->irq);
		return OK;
	}
	return OK;
}

static int
plip_connection_close(struct net_device *dev, struct net_local *nl,
		      struct plip_local *snd, struct plip_local *rcv)
{
	spin_lock_irq(&nl->lock);
	if (nl->connection == PLIP_CN_CLOSING) {
		nl->connection = PLIP_CN_NONE;
		netif_wake_queue (dev);
	}
	spin_unlock_irq(&nl->lock);
	if (nl->should_relinquish) {
		nl->should_relinquish = nl->port_owner = 0;
		parport_release(nl->pardev);
	}
	return OK;
}

/* PLIP_ERROR --- wait till other end settled */
static int
plip_error(struct net_device *dev, struct net_local *nl,
	   struct plip_local *snd, struct plip_local *rcv)
{
	unsigned char status;

	status = read_status(dev);
	if ((status & 0xf8) == 0x80) {
		if (net_debug > 2)
			printk(KERN_DEBUG "%s: reset interface.\n", dev->name);
		nl->connection = PLIP_CN_NONE;
		nl->should_relinquish = 0;
		netif_start_queue (dev);
		enable_parport_interrupts (dev);
		ENABLE(dev->irq);
		netif_wake_queue (dev);
	} else {
		nl->is_deferred = 1;
		schedule_delayed_work(&nl->deferred, 1);
	}

	return OK;
}

/* Handle the parallel port interrupts. */
static void
plip_interrupt(void *dev_id)
{
	struct net_device *dev = dev_id;
	struct net_local *nl;
	struct plip_local *rcv;
	unsigned char c0;
	unsigned long flags;

	nl = netdev_priv(dev);
	rcv = &nl->rcv_data;

	spin_lock_irqsave (&nl->lock, flags);

	c0 = read_status(dev);
	if ((c0 & 0xf8) != 0xc0) {
		if ((dev->irq != -1) && (net_debug > 1))
			printk(KERN_DEBUG "%s: spurious interrupt\n", dev->name);
		spin_unlock_irqrestore (&nl->lock, flags);
		return;
	}

	if (net_debug > 3)
		printk(KERN_DEBUG "%s: interrupt.\n", dev->name);

	switch (nl->connection) {
	case PLIP_CN_CLOSING:
		netif_wake_queue (dev);
		fallthrough;
	case PLIP_CN_NONE:
	case PLIP_CN_SEND:
		rcv->state = PLIP_PK_TRIGGER;
		nl->connection = PLIP_CN_RECEIVE;
		nl->timeout_count = 0;
		schedule_work(&nl->immediate);
		break;

	case PLIP_CN_RECEIVE:
		/* May occur because there is race condition
		   around test and set of dev->interrupt.
		   Ignore this interrupt. */
		break;

	case PLIP_CN_ERROR:
		printk(KERN_ERR "%s: receive interrupt in error state\n", dev->name);
		break;
	}

	spin_unlock_irqrestore(&nl->lock, flags);
}

static netdev_tx_t
plip_tx_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct net_local *nl = netdev_priv(dev);
	struct plip_local *snd = &nl->snd_data;

	if (netif_queue_stopped(dev))
		return NETDEV_TX_BUSY;

	/* We may need to grab the bus */
	if (!nl->port_owner) {
		if (parport_claim(nl->pardev))
			return NETDEV_TX_BUSY;
		nl->port_owner = 1;
	}

	netif_stop_queue (dev);

	if (skb->len > dev->mtu + dev->hard_header_len) {
		printk(KERN_WARNING "%s: packet too big, %d.\n", dev->name, (int)skb->len);
		netif_start_queue (dev);
		return NETDEV_TX_BUSY;
	}

	if (net_debug > 2)
		printk(KERN_DEBUG "%s: send request\n", dev->name);

	spin_lock_irq(&nl->lock);
	snd->skb = skb;
	snd->length.h = skb->len;
	snd->state = PLIP_PK_TRIGGER;
	if (nl->connection == PLIP_CN_NONE) {
		nl->connection = PLIP_CN_SEND;
		nl->timeout_count = 0;
	}
	schedule_work(&nl->immediate);
	spin_unlock_irq(&nl->lock);

	return NETDEV_TX_OK;
}

static void
plip_rewrite_address(const struct net_device *dev, struct ethhdr *eth)
{
	const struct in_device *in_dev;

	rcu_read_lock();
	in_dev = __in_dev_get_rcu(dev);
	if (in_dev) {
		/* Any address will do - we take the first */
		const struct in_ifaddr *ifa = rcu_dereference(in_dev->ifa_list);
		if (ifa) {
			memcpy(eth->h_source, dev->dev_addr, ETH_ALEN);
			memset(eth->h_dest, 0xfc, 2);
			memcpy(eth->h_dest+2, &ifa->ifa_address, 4);
		}
	}
	rcu_read_unlock();
}

static int
plip_hard_header(struct sk_buff *skb, struct net_device *dev,
		 unsigned short type, const void *daddr,
		 const void *saddr, unsigned len)
{
	int ret;

	ret = eth_header(skb, dev, type, daddr, saddr, len);
	if (ret >= 0)
		plip_rewrite_address (dev, (struct ethhdr *)skb->data);

	return ret;
}

static int plip_hard_header_cache(const struct neighbour *neigh,
				  struct hh_cache *hh, __be16 type)
{
	int ret;

	ret = eth_header_cache(neigh, hh, type);
	if (ret == 0) {
		struct ethhdr *eth;

		eth = (struct ethhdr*)(((u8*)hh->hh_data) +
				       HH_DATA_OFF(sizeof(*eth)));
		plip_rewrite_address (neigh->dev, eth);
	}

	return ret;
}

/* Open/initialize the board.  This is called (in the current kernel)
   sometime after booting when the 'ifconfig' program is run.

   This routine gets exclusive access to the parallel port by allocating
   its IRQ line.
 */
static int
plip_open(struct net_device *dev)
{
	struct net_local *nl = netdev_priv(dev);
	struct in_device *in_dev;

	/* Grab the port */
	if (!nl->port_owner) {
		if (parport_claim(nl->pardev)) return -EAGAIN;
		nl->port_owner = 1;
	}

	nl->should_relinquish = 0;

	/* Clear the data port. */
	write_data (dev, 0x00);

	/* Enable rx interrupt. */
	enable_parport_interrupts (dev);
	if (dev->irq == -1)
	{
		atomic_set (&nl->kill_timer, 0);
		schedule_delayed_work(&nl->timer, 1);
	}

	/* Initialize the state machine. */
	nl->rcv_data.state = nl->snd_data.state = PLIP_PK_DONE;
	nl->rcv_data.skb = nl->snd_data.skb = NULL;
	nl->connection = PLIP_CN_NONE;
	nl->is_deferred = 0;

	/* Fill in the MAC-level header.
	   We used to abuse dev->broadcast to store the point-to-point
	   MAC address, but we no longer do it. Instead, we fetch the
	   interface address whenever it is needed, which is cheap enough
	   because we use the hh_cache. Actually, abusing dev->broadcast
	   didn't work, because when using plip_open the point-to-point
	   address isn't yet known.
	   PLIP doesn't have a real MAC address, but we need it to be
	   DOS compatible, and to properly support taps (otherwise,
	   when the device address isn't identical to the address of a
	   received frame, the kernel incorrectly drops it).             */

	in_dev=__in_dev_get_rtnl(dev);
	if (in_dev) {
		/* Any address will do - we take the first. We already
		   have the first two bytes filled with 0xfc, from
		   plip_init_dev(). */
		const struct in_ifaddr *ifa = rcu_dereference(in_dev->ifa_list);
		if (ifa != NULL) {
			memcpy(dev->dev_addr+2, &ifa->ifa_local, 4);
		}
	}

	netif_start_queue (dev);

	return 0;
}

/* The inverse routine to plip_open (). */
static int
plip_close(struct net_device *dev)
{
	struct net_local *nl = netdev_priv(dev);
	struct plip_local *snd = &nl->snd_data;
	struct plip_local *rcv = &nl->rcv_data;

	netif_stop_queue (dev);
	DISABLE(dev->irq);
	synchronize_irq(dev->irq);

	if (dev->irq == -1)
	{
		init_completion(&nl->killed_timer_cmp);
		atomic_set (&nl->kill_timer, 1);
		wait_for_completion(&nl->killed_timer_cmp);
	}

#ifdef NOTDEF
	outb(0x00, PAR_DATA(dev));
#endif
	nl->is_deferred = 0;
	nl->connection = PLIP_CN_NONE;
	if (nl->port_owner) {
		parport_release(nl->pardev);
		nl->port_owner = 0;
	}

	snd->state = PLIP_PK_DONE;
	if (snd->skb) {
		dev_kfree_skb(snd->skb);
		snd->skb = NULL;
	}
	rcv->state = PLIP_PK_DONE;
	if (rcv->skb) {
		kfree_skb(rcv->skb);
		rcv->skb = NULL;
	}

#ifdef NOTDEF
	/* Reset. */
	outb(0x00, PAR_CONTROL(dev));
#endif
	return 0;
}

static int
plip_preempt(void *handle)
{
	struct net_device *dev = (struct net_device *)handle;
	struct net_local *nl = netdev_priv(dev);

	/* Stand our ground if a datagram is on the wire */
	if (nl->connection != PLIP_CN_NONE) {
		nl->should_relinquish = 1;
		return 1;
	}

	nl->port_owner = 0;	/* Remember that we released the bus */
	return 0;
}

static void
plip_wakeup(void *handle)
{
	struct net_device *dev = (struct net_device *)handle;
	struct net_local *nl = netdev_priv(dev);

	if (nl->port_owner) {
		/* Why are we being woken up? */
		printk(KERN_DEBUG "%s: why am I being woken up?\n", dev->name);
		if (!parport_claim(nl->pardev))
			/* bus_owner is already set (but why?) */
			printk(KERN_DEBUG "%s: I'm broken.\n", dev->name);
		else
			return;
	}

	if (!(dev->flags & IFF_UP))
		/* Don't need the port when the interface is down */
		return;

	if (!parport_claim(nl->pardev)) {
		nl->port_owner = 1;
		/* Clear the data port. */
		write_data (dev, 0x00);
	}
}

static int
plip_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct net_local *nl = netdev_priv(dev);
	struct plipconf *pc = (struct plipconf *) &rq->ifr_ifru;

	if (cmd != SIOCDEVPLIP)
		return -EOPNOTSUPP;

	switch(pc->pcmd) {
	case PLIP_GET_TIMEOUT:
		pc->trigger = nl->trigger;
		pc->nibble  = nl->nibble;
		break;
	case PLIP_SET_TIMEOUT:
		if(!capable(CAP_NET_ADMIN))
			return -EPERM;
		nl->trigger = pc->trigger;
		nl->nibble  = pc->nibble;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int parport[PLIP_MAX] = { [0 ... PLIP_MAX-1] = -1 };
static int timid;

module_param_array(parport, int, NULL, 0);
module_param(timid, int, 0);
MODULE_PARM_DESC(parport, "List of parport device numbers to use by plip");

static struct net_device *dev_plip[PLIP_MAX] = { NULL, };

static inline int
plip_searchfor(int list[], int a)
{
	int i;
	for (i = 0; i < PLIP_MAX && list[i] != -1; i++) {
		if (list[i] == a) return 1;
	}
	return 0;
}

/* plip_attach() is called (by the parport code) when a port is
 * available to use. */
static void plip_attach (struct parport *port)
{
	static int unit;
	struct net_device *dev;
	struct net_local *nl;
	char name[IFNAMSIZ];
	struct pardev_cb plip_cb;

	if ((parport[0] == -1 && (!timid || !port->devices)) ||
	    plip_searchfor(parport, port->number)) {
		if (unit == PLIP_MAX) {
			printk(KERN_ERR "plip: too many devices\n");
			return;
		}

		sprintf(name, "plip%d", unit);
		dev = alloc_etherdev(sizeof(struct net_local));
		if (!dev)
			return;

		strcpy(dev->name, name);

		dev->irq = port->irq;
		dev->base_addr = port->base;
		if (port->irq == -1) {
			printk(KERN_INFO "plip: %s has no IRQ. Using IRQ-less mode,"
		                 "which is fairly inefficient!\n", port->name);
		}

		nl = netdev_priv(dev);
		nl->dev = dev;

		memset(&plip_cb, 0, sizeof(plip_cb));
		plip_cb.private = dev;
		plip_cb.preempt = plip_preempt;
		plip_cb.wakeup = plip_wakeup;
		plip_cb.irq_func = plip_interrupt;

		nl->pardev = parport_register_dev_model(port, dev->name,
							&plip_cb, unit);

		if (!nl->pardev) {
			printk(KERN_ERR "%s: parport_register failed\n", name);
			goto err_free_dev;
		}

		plip_init_netdev(dev);

		if (register_netdev(dev)) {
			printk(KERN_ERR "%s: network register failed\n", name);
			goto err_parport_unregister;
		}

		printk(KERN_INFO "%s", version);
		if (dev->irq != -1)
			printk(KERN_INFO "%s: Parallel port at %#3lx, "
					 "using IRQ %d.\n",
				         dev->name, dev->base_addr, dev->irq);
		else
			printk(KERN_INFO "%s: Parallel port at %#3lx, "
					 "not using IRQ.\n",
					 dev->name, dev->base_addr);
		dev_plip[unit++] = dev;
	}
	return;

err_parport_unregister:
	parport_unregister_device(nl->pardev);
err_free_dev:
	free_netdev(dev);
}

/* plip_detach() is called (by the parport code) when a port is
 * no longer available to use. */
static void plip_detach (struct parport *port)
{
	/* Nothing to do */
}

static int plip_probe(struct pardevice *par_dev)
{
	struct device_driver *drv = par_dev->dev.driver;
	int len = strlen(drv->name);

	if (strncmp(par_dev->name, drv->name, len))
		return -ENODEV;

	return 0;
}

static struct parport_driver plip_driver = {
	.name		= "plip",
	.probe		= plip_probe,
	.match_port	= plip_attach,
	.detach		= plip_detach,
	.devmodel	= true,
};

static void __exit plip_cleanup_module (void)
{
	struct net_device *dev;
	int i;

	for (i=0; i < PLIP_MAX; i++) {
		if ((dev = dev_plip[i])) {
			struct net_local *nl = netdev_priv(dev);
			unregister_netdev(dev);
			if (nl->port_owner)
				parport_release(nl->pardev);
			parport_unregister_device(nl->pardev);
			free_netdev(dev);
			dev_plip[i] = NULL;
		}
	}

	parport_unregister_driver(&plip_driver);
}

#ifndef MODULE

static int parport_ptr;

static int __init plip_setup(char *str)
{
	int ints[4];

	str = get_options(str, ARRAY_SIZE(ints), ints);

	/* Ugh. */
	if (!strncmp(str, "parport", 7)) {
		int n = simple_strtoul(str+7, NULL, 10);
		if (parport_ptr < PLIP_MAX)
			parport[parport_ptr++] = n;
		else
			printk(KERN_INFO "plip: too many ports, %s ignored.\n",
			       str);
	} else if (!strcmp(str, "timid")) {
		timid = 1;
	} else {
		if (ints[0] == 0 || ints[1] == 0) {
			/* disable driver on "plip=" or "plip=0" */
			parport[0] = -2;
		} else {
			printk(KERN_WARNING "warning: 'plip=0x%x' ignored\n",
			       ints[1]);
		}
	}
	return 1;
}

__setup("plip=", plip_setup);

#endif /* !MODULE */

static int __init plip_init (void)
{
	if (parport[0] == -2)
		return 0;

	if (parport[0] != -1 && timid) {
		printk(KERN_WARNING "plip: warning, ignoring `timid' since specific ports given.\n");
		timid = 0;
	}

	if (parport_register_driver (&plip_driver)) {
		printk (KERN_WARNING "plip: couldn't register driver\n");
		return 1;
	}

	return 0;
}

module_init(plip_init);
module_exit(plip_cleanup_module);
MODULE_LICENSE("GPL");
