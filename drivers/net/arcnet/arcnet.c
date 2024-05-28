/*
 * Linux ARCnet driver - device-independent routines
 *
 * Written 1997 by David Woodhouse.
 * Written 1994-1999 by Avery Pennarun.
 * Written 1999-2000 by Martin Mares <mj@ucw.cz>.
 * Derived from skeleton.c by Donald Becker.
 *
 * Special thanks to Contemporary Controls, Inc. (www.ccontrols.com)
 *  for sponsoring the further development of this driver.
 *
 * **********************
 *
 * The original copyright was as follows:
 *
 * skeleton.c Written 1993 by Donald Becker.
 * Copyright 1993 United States Government as represented by the
 * Director, National Security Agency.  This software may only be used
 * and distributed according to the terms of the GNU General Public License as
 * modified by SRC, incorporated herein by reference.
 *
 * **********************
 *
 * The change log is now in a file called ChangeLog in this directory.
 *
 * Sources:
 *  - Crynwr arcnet.com/arcether.com packet drivers.
 *  - arcnet.c v0.00 dated 1/1/94 and apparently by
 *     Donald Becker - it didn't work :)
 *  - skeleton.c v0.05 dated 11/16/93 by Donald Becker
 *     (from Linux Kernel 1.1.45)
 *  - RFC's 1201 and 1051 - re: TCP/IP over ARCnet
 *  - The official ARCnet COM9026 data sheets (!) thanks to
 *     Ken Cornetet <kcornete@nyx10.cs.du.edu>
 *  - The official ARCnet COM20020 data sheets.
 *  - Information on some more obscure ARCnet controller chips, thanks
 *     to the nice people at SMSC.
 *  - net/inet/eth.c (from kernel 1.1.50) for header-building info.
 *  - Alternate Linux ARCnet source by V.Shergin <vsher@sao.stavropol.su>
 *  - Textual information and more alternate source from Joachim Koenig
 *     <jojo@repas.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <net/arp.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/errqueue.h>

#include <linux/leds.h>

#include "arcdevice.h"
#include "com9026.h"

/* "do nothing" functions for protocol drivers */
static void null_rx(struct net_device *dev, int bufnum,
		    struct archdr *pkthdr, int length);
static int null_build_header(struct sk_buff *skb, struct net_device *dev,
			     unsigned short type, uint8_t daddr);
static int null_prepare_tx(struct net_device *dev, struct archdr *pkt,
			   int length, int bufnum);

static void arcnet_rx(struct net_device *dev, int bufnum);

/* one ArcProto per possible proto ID.  None of the elements of
 * arc_proto_map are allowed to be NULL; they will get set to
 * arc_proto_default instead.  It also must not be NULL; if you would like
 * to set it to NULL, set it to &arc_proto_null instead.
 */
struct ArcProto *arc_proto_map[256];
EXPORT_SYMBOL(arc_proto_map);

struct ArcProto *arc_proto_default;
EXPORT_SYMBOL(arc_proto_default);

struct ArcProto *arc_bcast_proto;
EXPORT_SYMBOL(arc_bcast_proto);

struct ArcProto *arc_raw_proto;
EXPORT_SYMBOL(arc_raw_proto);

static struct ArcProto arc_proto_null = {
	.suffix		= '?',
	.mtu		= XMTU,
	.is_ip          = 0,
	.rx		= null_rx,
	.build_header	= null_build_header,
	.prepare_tx	= null_prepare_tx,
	.continue_tx    = NULL,
	.ack_tx         = NULL
};

/* Exported function prototypes */
int arcnet_debug = ARCNET_DEBUG;
EXPORT_SYMBOL(arcnet_debug);

/* Internal function prototypes */
static int arcnet_header(struct sk_buff *skb, struct net_device *dev,
			 unsigned short type, const void *daddr,
			 const void *saddr, unsigned len);
static int go_tx(struct net_device *dev);

static int debug = ARCNET_DEBUG;
module_param(debug, int, 0);
MODULE_DESCRIPTION("ARCnet core driver");
MODULE_LICENSE("GPL");

static int __init arcnet_init(void)
{
	int count;

	arcnet_debug = debug;

	pr_info("arcnet loaded\n");

	/* initialize the protocol map */
	arc_raw_proto = arc_proto_default = arc_bcast_proto = &arc_proto_null;
	for (count = 0; count < 256; count++)
		arc_proto_map[count] = arc_proto_default;

	if (BUGLVL(D_DURING))
		pr_info("struct sizes: %zd %zd %zd %zd %zd\n",
			sizeof(struct arc_hardware),
			sizeof(struct arc_rfc1201),
			sizeof(struct arc_rfc1051),
			sizeof(struct arc_eth_encap),
			sizeof(struct archdr));

	return 0;
}

static void __exit arcnet_exit(void)
{
}

module_init(arcnet_init);
module_exit(arcnet_exit);

/* Dump the contents of an sk_buff */
#if ARCNET_DEBUG_MAX & D_SKB
void arcnet_dump_skb(struct net_device *dev,
		     struct sk_buff *skb, char *desc)
{
	char hdr[32];

	/* dump the packet */
	snprintf(hdr, sizeof(hdr), "%6s:%s skb->data:", dev->name, desc);
	print_hex_dump(KERN_DEBUG, hdr, DUMP_PREFIX_OFFSET,
		       16, 1, skb->data, skb->len, true);
}
EXPORT_SYMBOL(arcnet_dump_skb);
#endif

/* Dump the contents of an ARCnet buffer */
#if (ARCNET_DEBUG_MAX & (D_RX | D_TX))
static void arcnet_dump_packet(struct net_device *dev, int bufnum,
			       char *desc, int take_arcnet_lock)
{
	struct arcnet_local *lp = netdev_priv(dev);
	int i, length;
	unsigned long flags = 0;
	static uint8_t buf[512];
	char hdr[32];

	/* hw.copy_from_card expects IRQ context so take the IRQ lock
	 * to keep it single threaded
	 */
	if (take_arcnet_lock)
		spin_lock_irqsave(&lp->lock, flags);

	lp->hw.copy_from_card(dev, bufnum, 0, buf, 512);
	if (take_arcnet_lock)
		spin_unlock_irqrestore(&lp->lock, flags);

	/* if the offset[0] byte is nonzero, this is a 256-byte packet */
	length = (buf[2] ? 256 : 512);

	/* dump the packet */
	snprintf(hdr, sizeof(hdr), "%6s:%s packet dump:", dev->name, desc);
	print_hex_dump(KERN_DEBUG, hdr, DUMP_PREFIX_OFFSET,
		       16, 1, buf, length, true);
}

#else

#define arcnet_dump_packet(dev, bufnum, desc, take_arcnet_lock) do { } while (0)

#endif

/* Trigger a LED event in response to a ARCNET device event */
void arcnet_led_event(struct net_device *dev, enum arcnet_led_event event)
{
	struct arcnet_local *lp = netdev_priv(dev);

	switch (event) {
	case ARCNET_LED_EVENT_RECON:
		led_trigger_blink_oneshot(lp->recon_led_trig, 350, 350, 0);
		break;
	case ARCNET_LED_EVENT_OPEN:
		led_trigger_event(lp->tx_led_trig, LED_OFF);
		led_trigger_event(lp->recon_led_trig, LED_OFF);
		break;
	case ARCNET_LED_EVENT_STOP:
		led_trigger_event(lp->tx_led_trig, LED_OFF);
		led_trigger_event(lp->recon_led_trig, LED_OFF);
		break;
	case ARCNET_LED_EVENT_TX:
		led_trigger_blink_oneshot(lp->tx_led_trig, 50, 50, 0);
		break;
	}
}
EXPORT_SYMBOL_GPL(arcnet_led_event);

static void arcnet_led_release(struct device *gendev, void *res)
{
	struct arcnet_local *lp = netdev_priv(to_net_dev(gendev));

	led_trigger_unregister_simple(lp->tx_led_trig);
	led_trigger_unregister_simple(lp->recon_led_trig);
}

/* Register ARCNET LED triggers for a arcnet device
 *
 * This is normally called from a driver's probe function
 */
void devm_arcnet_led_init(struct net_device *netdev, int index, int subid)
{
	struct arcnet_local *lp = netdev_priv(netdev);
	void *res;

	res = devres_alloc(arcnet_led_release, 0, GFP_KERNEL);
	if (!res) {
		netdev_err(netdev, "cannot register LED triggers\n");
		return;
	}

	snprintf(lp->tx_led_trig_name, sizeof(lp->tx_led_trig_name),
		 "arc%d-%d-tx", index, subid);
	snprintf(lp->recon_led_trig_name, sizeof(lp->recon_led_trig_name),
		 "arc%d-%d-recon", index, subid);

	led_trigger_register_simple(lp->tx_led_trig_name,
				    &lp->tx_led_trig);
	led_trigger_register_simple(lp->recon_led_trig_name,
				    &lp->recon_led_trig);

	devres_add(&netdev->dev, res);
}
EXPORT_SYMBOL_GPL(devm_arcnet_led_init);

/* Unregister a protocol driver from the arc_proto_map.  Protocol drivers
 * are responsible for registering themselves, but the unregister routine
 * is pretty generic so we'll do it here.
 */
void arcnet_unregister_proto(struct ArcProto *proto)
{
	int count;

	if (arc_proto_default == proto)
		arc_proto_default = &arc_proto_null;
	if (arc_bcast_proto == proto)
		arc_bcast_proto = arc_proto_default;
	if (arc_raw_proto == proto)
		arc_raw_proto = arc_proto_default;

	for (count = 0; count < 256; count++) {
		if (arc_proto_map[count] == proto)
			arc_proto_map[count] = arc_proto_default;
	}
}
EXPORT_SYMBOL(arcnet_unregister_proto);

/* Add a buffer to the queue.  Only the interrupt handler is allowed to do
 * this, unless interrupts are disabled.
 *
 * Note: we don't check for a full queue, since there aren't enough buffers
 * to more than fill it.
 */
static void release_arcbuf(struct net_device *dev, int bufnum)
{
	struct arcnet_local *lp = netdev_priv(dev);
	int i;

	lp->buf_queue[lp->first_free_buf++] = bufnum;
	lp->first_free_buf %= 5;

	if (BUGLVL(D_DURING)) {
		arc_printk(D_DURING, dev, "release_arcbuf: freed #%d; buffer queue is now: ",
			   bufnum);
		for (i = lp->next_buf; i != lp->first_free_buf; i = (i + 1) % 5)
			arc_cont(D_DURING, "#%d ", lp->buf_queue[i]);
		arc_cont(D_DURING, "\n");
	}
}

/* Get a buffer from the queue.
 * If this returns -1, there are no buffers available.
 */
static int get_arcbuf(struct net_device *dev)
{
	struct arcnet_local *lp = netdev_priv(dev);
	int buf = -1, i;

	if (!atomic_dec_and_test(&lp->buf_lock)) {
		/* already in this function */
		arc_printk(D_NORMAL, dev, "get_arcbuf: overlap (%d)!\n",
			   lp->buf_lock.counter);
	} else {			/* we can continue */
		if (lp->next_buf >= 5)
			lp->next_buf -= 5;

		if (lp->next_buf == lp->first_free_buf) {
			arc_printk(D_NORMAL, dev, "get_arcbuf: BUG: no buffers are available??\n");
		} else {
			buf = lp->buf_queue[lp->next_buf++];
			lp->next_buf %= 5;
		}
	}

	if (BUGLVL(D_DURING)) {
		arc_printk(D_DURING, dev, "get_arcbuf: got #%d; buffer queue is now: ",
			   buf);
		for (i = lp->next_buf; i != lp->first_free_buf; i = (i + 1) % 5)
			arc_cont(D_DURING, "#%d ", lp->buf_queue[i]);
		arc_cont(D_DURING, "\n");
	}

	atomic_inc(&lp->buf_lock);
	return buf;
}

static int choose_mtu(void)
{
	int count, mtu = 65535;

	/* choose the smallest MTU of all available encaps */
	for (count = 0; count < 256; count++) {
		if (arc_proto_map[count] != &arc_proto_null &&
		    arc_proto_map[count]->mtu < mtu) {
			mtu = arc_proto_map[count]->mtu;
		}
	}

	return mtu == 65535 ? XMTU : mtu;
}

static const struct header_ops arcnet_header_ops = {
	.create = arcnet_header,
};

static const struct net_device_ops arcnet_netdev_ops = {
	.ndo_open	= arcnet_open,
	.ndo_stop	= arcnet_close,
	.ndo_start_xmit = arcnet_send_packet,
	.ndo_tx_timeout = arcnet_timeout,
};

/* Setup a struct device for ARCnet. */
static void arcdev_setup(struct net_device *dev)
{
	dev->type = ARPHRD_ARCNET;
	dev->netdev_ops = &arcnet_netdev_ops;
	dev->header_ops = &arcnet_header_ops;
	dev->hard_header_len = sizeof(struct arc_hardware);
	dev->mtu = choose_mtu();

	dev->addr_len = ARCNET_ALEN;
	dev->tx_queue_len = 100;
	dev->broadcast[0] = 0x00;	/* for us, broadcasts are address 0 */
	dev->watchdog_timeo = TX_TIMEOUT;

	/* New-style flags. */
	dev->flags = IFF_BROADCAST;
}

static void arcnet_timer(struct timer_list *t)
{
	struct arcnet_local *lp = from_timer(lp, t, timer);
	struct net_device *dev = lp->dev;

	spin_lock_irq(&lp->lock);

	if (!lp->reset_in_progress && !netif_carrier_ok(dev)) {
		netif_carrier_on(dev);
		netdev_info(dev, "link up\n");
	}

	spin_unlock_irq(&lp->lock);
}

static void reset_device_work(struct work_struct *work)
{
	struct arcnet_local *lp;
	struct net_device *dev;

	lp = container_of(work, struct arcnet_local, reset_work);
	dev = lp->dev;

	/* Do not bring the network interface back up if an ifdown
	 * was already done.
	 */
	if (!netif_running(dev) || !lp->reset_in_progress)
		return;

	rtnl_lock();

	/* Do another check, in case of an ifdown that was triggered in
	 * the small race window between the exit condition above and
	 * acquiring RTNL.
	 */
	if (!netif_running(dev) || !lp->reset_in_progress)
		goto out;

	dev_close(dev);
	dev_open(dev, NULL);

out:
	rtnl_unlock();
}

static void arcnet_reply_tasklet(struct tasklet_struct *t)
{
	struct arcnet_local *lp = from_tasklet(lp, t, reply_tasklet);

	struct sk_buff *ackskb, *skb;
	struct sock_exterr_skb *serr;
	struct sock *sk;
	int ret;

	local_irq_disable();
	skb = lp->outgoing.skb;
	if (!skb || !skb->sk) {
		local_irq_enable();
		return;
	}

	sock_hold(skb->sk);
	sk = skb->sk;
	ackskb = skb_clone_sk(skb);
	sock_put(skb->sk);

	if (!ackskb) {
		local_irq_enable();
		return;
	}

	serr = SKB_EXT_ERR(ackskb);
	memset(serr, 0, sizeof(*serr));
	serr->ee.ee_errno = ENOMSG;
	serr->ee.ee_origin = SO_EE_ORIGIN_TXSTATUS;
	serr->ee.ee_data = skb_shinfo(skb)->tskey;
	serr->ee.ee_info = lp->reply_status;

	/* finally erasing outgoing skb */
	dev_kfree_skb(lp->outgoing.skb);
	lp->outgoing.skb = NULL;

	ackskb->dev = lp->dev;

	ret = sock_queue_err_skb(sk, ackskb);
	if (ret)
		dev_kfree_skb_irq(ackskb);

	local_irq_enable();
};

struct net_device *alloc_arcdev(const char *name)
{
	struct net_device *dev;

	dev = alloc_netdev(sizeof(struct arcnet_local),
			   name && *name ? name : "arc%d", NET_NAME_UNKNOWN,
			   arcdev_setup);
	if (dev) {
		struct arcnet_local *lp = netdev_priv(dev);

		lp->dev = dev;
		spin_lock_init(&lp->lock);
		timer_setup(&lp->timer, arcnet_timer, 0);
		INIT_WORK(&lp->reset_work, reset_device_work);
	}

	return dev;
}
EXPORT_SYMBOL(alloc_arcdev);

void free_arcdev(struct net_device *dev)
{
	struct arcnet_local *lp = netdev_priv(dev);

	/* Do not cancel this at ->ndo_close(), as the workqueue itself
	 * indirectly calls the ifdown path through dev_close().
	 */
	cancel_work_sync(&lp->reset_work);
	free_netdev(dev);
}
EXPORT_SYMBOL(free_arcdev);

/* Open/initialize the board.  This is called sometime after booting when
 * the 'ifconfig' program is run.
 *
 * This routine should set everything up anew at each open, even registers
 * that "should" only need to be set once at boot, so that there is
 * non-reboot way to recover if something goes wrong.
 */
int arcnet_open(struct net_device *dev)
{
	struct arcnet_local *lp = netdev_priv(dev);
	int count, newmtu, error;

	arc_printk(D_INIT, dev, "opened.");

	if (!try_module_get(lp->hw.owner))
		return -ENODEV;

	if (BUGLVL(D_PROTO)) {
		arc_printk(D_PROTO, dev, "protocol map (default is '%c'): ",
			   arc_proto_default->suffix);
		for (count = 0; count < 256; count++)
			arc_cont(D_PROTO, "%c", arc_proto_map[count]->suffix);
		arc_cont(D_PROTO, "\n");
	}

	tasklet_setup(&lp->reply_tasklet, arcnet_reply_tasklet);

	arc_printk(D_INIT, dev, "arcnet_open: resetting card.\n");

	/* try to put the card in a defined state - if it fails the first
	 * time, actually reset it.
	 */
	error = -ENODEV;
	if (lp->hw.reset(dev, 0) && lp->hw.reset(dev, 1))
		goto out_module_put;

	newmtu = choose_mtu();
	if (newmtu < dev->mtu)
		dev->mtu = newmtu;

	arc_printk(D_INIT, dev, "arcnet_open: mtu: %d.\n", dev->mtu);

	/* autodetect the encapsulation for each host. */
	memset(lp->default_proto, 0, sizeof(lp->default_proto));

	/* the broadcast address is special - use the 'bcast' protocol */
	for (count = 0; count < 256; count++) {
		if (arc_proto_map[count] == arc_bcast_proto) {
			lp->default_proto[0] = count;
			break;
		}
	}

	/* initialize buffers */
	atomic_set(&lp->buf_lock, 1);

	lp->next_buf = lp->first_free_buf = 0;
	release_arcbuf(dev, 0);
	release_arcbuf(dev, 1);
	release_arcbuf(dev, 2);
	release_arcbuf(dev, 3);
	lp->cur_tx = lp->next_tx = -1;
	lp->cur_rx = -1;

	lp->rfc1201.sequence = 1;

	/* bring up the hardware driver */
	if (lp->hw.open)
		lp->hw.open(dev);

	if (dev->dev_addr[0] == 0)
		arc_printk(D_NORMAL, dev, "WARNING!  Station address 00 is reserved for broadcasts!\n");
	else if (dev->dev_addr[0] == 255)
		arc_printk(D_NORMAL, dev, "WARNING!  Station address FF may confuse DOS networking programs!\n");

	arc_printk(D_DEBUG, dev, "%s: %d: %s\n", __FILE__, __LINE__, __func__);
	if (lp->hw.status(dev) & RESETflag) {
		arc_printk(D_DEBUG, dev, "%s: %d: %s\n",
			   __FILE__, __LINE__, __func__);
		lp->hw.command(dev, CFLAGScmd | RESETclear);
	}

	arc_printk(D_DEBUG, dev, "%s: %d: %s\n", __FILE__, __LINE__, __func__);
	/* make sure we're ready to receive IRQ's. */
	lp->hw.intmask(dev, 0);
	udelay(1);		/* give it time to set the mask before
				 * we reset it again. (may not even be
				 * necessary)
				 */
	arc_printk(D_DEBUG, dev, "%s: %d: %s\n", __FILE__, __LINE__, __func__);
	lp->intmask = NORXflag | RECONflag;
	lp->hw.intmask(dev, lp->intmask);
	arc_printk(D_DEBUG, dev, "%s: %d: %s\n", __FILE__, __LINE__, __func__);

	netif_carrier_off(dev);
	netif_start_queue(dev);
	mod_timer(&lp->timer, jiffies + msecs_to_jiffies(1000));

	arcnet_led_event(dev, ARCNET_LED_EVENT_OPEN);
	return 0;

 out_module_put:
	module_put(lp->hw.owner);
	return error;
}
EXPORT_SYMBOL(arcnet_open);

/* The inverse routine to arcnet_open - shuts down the card. */
int arcnet_close(struct net_device *dev)
{
	struct arcnet_local *lp = netdev_priv(dev);

	arcnet_led_event(dev, ARCNET_LED_EVENT_STOP);
	del_timer_sync(&lp->timer);

	netif_stop_queue(dev);
	netif_carrier_off(dev);

	tasklet_kill(&lp->reply_tasklet);

	/* flush TX and disable RX */
	lp->hw.intmask(dev, 0);
	lp->hw.command(dev, NOTXcmd);	/* stop transmit */
	lp->hw.command(dev, NORXcmd);	/* disable receive */
	mdelay(1);

	/* shut down the card */
	lp->hw.close(dev);

	/* reset counters */
	lp->reset_in_progress = 0;

	module_put(lp->hw.owner);
	return 0;
}
EXPORT_SYMBOL(arcnet_close);

static int arcnet_header(struct sk_buff *skb, struct net_device *dev,
			 unsigned short type, const void *daddr,
			 const void *saddr, unsigned len)
{
	const struct arcnet_local *lp = netdev_priv(dev);
	uint8_t _daddr, proto_num;
	struct ArcProto *proto;

	arc_printk(D_DURING, dev,
		   "create header from %d to %d; protocol %d (%Xh); size %u.\n",
		   saddr ? *(uint8_t *)saddr : -1,
		   daddr ? *(uint8_t *)daddr : -1,
		   type, type, len);

	if (skb->len != 0 && len != skb->len)
		arc_printk(D_NORMAL, dev, "arcnet_header: Yikes!  skb->len(%d) != len(%d)!\n",
			   skb->len, len);

	/* Type is host order - ? */
	if (type == ETH_P_ARCNET) {
		proto = arc_raw_proto;
		arc_printk(D_DEBUG, dev, "arc_raw_proto used. proto='%c'\n",
			   proto->suffix);
		_daddr = daddr ? *(uint8_t *)daddr : 0;
	} else if (!daddr) {
		/* if the dest addr isn't provided, we can't choose an
		 * encapsulation!  Store the packet type (eg. ETH_P_IP)
		 * for now, and we'll push on a real header when we do
		 * rebuild_header.
		 */
		*(uint16_t *)skb_push(skb, 2) = type;
		/* XXX: Why not use skb->mac_len? */
		if (skb->network_header - skb->mac_header != 2)
			arc_printk(D_NORMAL, dev, "arcnet_header: Yikes!  diff (%u) is not 2!\n",
				   skb->network_header - skb->mac_header);
		return -2;	/* return error -- can't transmit yet! */
	} else {
		/* otherwise, we can just add the header as usual. */
		_daddr = *(uint8_t *)daddr;
		proto_num = lp->default_proto[_daddr];
		proto = arc_proto_map[proto_num];
		arc_printk(D_DURING, dev, "building header for %02Xh using protocol '%c'\n",
			   proto_num, proto->suffix);
		if (proto == &arc_proto_null && arc_bcast_proto != proto) {
			arc_printk(D_DURING, dev, "actually, let's use '%c' instead.\n",
				   arc_bcast_proto->suffix);
			proto = arc_bcast_proto;
		}
	}
	return proto->build_header(skb, dev, type, _daddr);
}

/* Called by the kernel in order to transmit a packet. */
netdev_tx_t arcnet_send_packet(struct sk_buff *skb,
			       struct net_device *dev)
{
	struct arcnet_local *lp = netdev_priv(dev);
	struct archdr *pkt;
	struct arc_rfc1201 *soft;
	struct ArcProto *proto;
	int txbuf;
	unsigned long flags;
	int retval;

	arc_printk(D_DURING, dev,
		   "transmit requested (status=%Xh, txbufs=%d/%d, len=%d, protocol %x)\n",
		   lp->hw.status(dev), lp->cur_tx, lp->next_tx, skb->len, skb->protocol);

	pkt = (struct archdr *)skb->data;
	soft = &pkt->soft.rfc1201;
	proto = arc_proto_map[soft->proto];

	arc_printk(D_SKB_SIZE, dev, "skb: transmitting %d bytes to %02X\n",
		   skb->len, pkt->hard.dest);
	if (BUGLVL(D_SKB))
		arcnet_dump_skb(dev, skb, "tx");

	/* fits in one packet? */
	if (skb->len - ARC_HDR_SIZE > XMTU && !proto->continue_tx) {
		arc_printk(D_NORMAL, dev, "fixme: packet too large: compensating badly!\n");
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;	/* don't try again */
	}

	/* We're busy transmitting a packet... */
	netif_stop_queue(dev);

	spin_lock_irqsave(&lp->lock, flags);
	lp->hw.intmask(dev, 0);
	if (lp->next_tx == -1)
		txbuf = get_arcbuf(dev);
	else
		txbuf = -1;

	if (txbuf != -1) {
		lp->outgoing.skb = skb;
		if (proto->prepare_tx(dev, pkt, skb->len, txbuf) &&
		    !proto->ack_tx) {
			/* done right away and we don't want to acknowledge
			 *  the package later - forget about it now
			 */
			dev->stats.tx_bytes += skb->len;
		} else {
			/* do it the 'split' way */
			lp->outgoing.proto = proto;
			lp->outgoing.skb = skb;
			lp->outgoing.pkt = pkt;

			if (proto->continue_tx &&
			    proto->continue_tx(dev, txbuf)) {
				arc_printk(D_NORMAL, dev,
					   "bug! continue_tx finished the first time! (proto='%c')\n",
					   proto->suffix);
			}
		}
		retval = NETDEV_TX_OK;
		lp->next_tx = txbuf;
	} else {
		retval = NETDEV_TX_BUSY;
	}

	arc_printk(D_DEBUG, dev, "%s: %d: %s, status: %x\n",
		   __FILE__, __LINE__, __func__, lp->hw.status(dev));
	/* make sure we didn't ignore a TX IRQ while we were in here */
	lp->hw.intmask(dev, 0);

	arc_printk(D_DEBUG, dev, "%s: %d: %s\n", __FILE__, __LINE__, __func__);
	lp->intmask |= TXFREEflag | EXCNAKflag;
	lp->hw.intmask(dev, lp->intmask);
	arc_printk(D_DEBUG, dev, "%s: %d: %s, status: %x\n",
		   __FILE__, __LINE__, __func__, lp->hw.status(dev));

	arcnet_led_event(dev, ARCNET_LED_EVENT_TX);

	spin_unlock_irqrestore(&lp->lock, flags);
	return retval;		/* no need to try again */
}
EXPORT_SYMBOL(arcnet_send_packet);

/* Actually start transmitting a packet that was loaded into a buffer
 * by prepare_tx.  This should _only_ be called by the interrupt handler.
 */
static int go_tx(struct net_device *dev)
{
	struct arcnet_local *lp = netdev_priv(dev);

	arc_printk(D_DURING, dev, "go_tx: status=%Xh, intmask=%Xh, next_tx=%d, cur_tx=%d\n",
		   lp->hw.status(dev), lp->intmask, lp->next_tx, lp->cur_tx);

	if (lp->cur_tx != -1 || lp->next_tx == -1)
		return 0;

	if (BUGLVL(D_TX))
		arcnet_dump_packet(dev, lp->next_tx, "go_tx", 0);

	lp->cur_tx = lp->next_tx;
	lp->next_tx = -1;

	/* start sending */
	lp->hw.command(dev, TXcmd | (lp->cur_tx << 3));

	dev->stats.tx_packets++;
	lp->lasttrans_dest = lp->lastload_dest;
	lp->lastload_dest = 0;
	lp->excnak_pending = 0;
	lp->intmask |= TXFREEflag | EXCNAKflag;

	return 1;
}

/* Called by the kernel when transmit times out */
void arcnet_timeout(struct net_device *dev, unsigned int txqueue)
{
	unsigned long flags;
	struct arcnet_local *lp = netdev_priv(dev);
	int status = lp->hw.status(dev);
	char *msg;

	spin_lock_irqsave(&lp->lock, flags);
	if (status & TXFREEflag) {	/* transmit _DID_ finish */
		msg = " - missed IRQ?";
	} else {
		msg = "";
		dev->stats.tx_aborted_errors++;
		lp->timed_out = 1;
		lp->hw.command(dev, NOTXcmd | (lp->cur_tx << 3));
	}
	dev->stats.tx_errors++;

	/* make sure we didn't miss a TX or a EXC NAK IRQ */
	lp->hw.intmask(dev, 0);
	lp->intmask |= TXFREEflag | EXCNAKflag;
	lp->hw.intmask(dev, lp->intmask);

	spin_unlock_irqrestore(&lp->lock, flags);

	if (time_after(jiffies, lp->last_timeout + 10 * HZ)) {
		arc_printk(D_EXTRA, dev, "tx timed out%s (status=%Xh, intmask=%Xh, dest=%02Xh)\n",
			   msg, status, lp->intmask, lp->lasttrans_dest);
		lp->last_timeout = jiffies;
	}

	if (lp->cur_tx == -1)
		netif_wake_queue(dev);
}
EXPORT_SYMBOL(arcnet_timeout);

/* The typical workload of the driver: Handle the network interface
 * interrupts. Establish which device needs attention, and call the correct
 * chipset interrupt handler.
 */
irqreturn_t arcnet_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct arcnet_local *lp;
	int recbuf, status, diagstatus, didsomething, boguscount;
	unsigned long flags;
	int retval = IRQ_NONE;

	arc_printk(D_DURING, dev, "\n");

	arc_printk(D_DURING, dev, "in arcnet_interrupt\n");

	lp = netdev_priv(dev);
	BUG_ON(!lp);

	spin_lock_irqsave(&lp->lock, flags);

	if (lp->reset_in_progress)
		goto out;

	/* RESET flag was enabled - if device is not running, we must
	 * clear it right away (but nothing else).
	 */
	if (!netif_running(dev)) {
		if (lp->hw.status(dev) & RESETflag)
			lp->hw.command(dev, CFLAGScmd | RESETclear);
		lp->hw.intmask(dev, 0);
		spin_unlock_irqrestore(&lp->lock, flags);
		return retval;
	}

	arc_printk(D_DURING, dev, "in arcnet_inthandler (status=%Xh, intmask=%Xh)\n",
		   lp->hw.status(dev), lp->intmask);

	boguscount = 5;
	do {
		status = lp->hw.status(dev);
		diagstatus = (status >> 8) & 0xFF;

		arc_printk(D_DEBUG, dev, "%s: %d: %s: status=%x\n",
			   __FILE__, __LINE__, __func__, status);
		didsomething = 0;

		/* RESET flag was enabled - card is resetting and if RX is
		 * disabled, it's NOT because we just got a packet.
		 *
		 * The card is in an undefined state.
		 * Clear it out and start over.
		 */
		if (status & RESETflag) {
			arc_printk(D_NORMAL, dev, "spurious reset (status=%Xh)\n",
				   status);

			lp->reset_in_progress = 1;
			netif_stop_queue(dev);
			netif_carrier_off(dev);
			schedule_work(&lp->reset_work);

			/* get out of the interrupt handler! */
			goto out;
		}
		/* RX is inhibited - we must have received something.
		 * Prepare to receive into the next buffer.
		 *
		 * We don't actually copy the received packet from the card
		 * until after the transmit handler runs (and possibly
		 * launches the next tx); this should improve latency slightly
		 * if we get both types of interrupts at once.
		 */
		recbuf = -1;
		if (status & lp->intmask & NORXflag) {
			recbuf = lp->cur_rx;
			arc_printk(D_DURING, dev, "Buffer #%d: receive irq (status=%Xh)\n",
				   recbuf, status);

			lp->cur_rx = get_arcbuf(dev);
			if (lp->cur_rx != -1) {
				arc_printk(D_DURING, dev, "enabling receive to buffer #%d\n",
					   lp->cur_rx);
				lp->hw.command(dev, RXcmd | (lp->cur_rx << 3) | RXbcasts);
			}
			didsomething++;
		}

		if ((diagstatus & EXCNAKflag)) {
			arc_printk(D_DURING, dev, "EXCNAK IRQ (diagstat=%Xh)\n",
				   diagstatus);

			lp->hw.command(dev, NOTXcmd);      /* disable transmit */
			lp->excnak_pending = 1;

			lp->hw.command(dev, EXCNAKclear);
			lp->intmask &= ~(EXCNAKflag);
			didsomething++;
		}

		/* a transmit finished, and we're interested in it. */
		if ((status & lp->intmask & TXFREEflag) || lp->timed_out) {
			int ackstatus;
			lp->intmask &= ~(TXFREEflag | EXCNAKflag);

			if (status & TXACKflag)
				ackstatus = 2;
			else if (lp->excnak_pending)
				ackstatus = 1;
			else
				ackstatus = 0;

			arc_printk(D_DURING, dev, "TX IRQ (stat=%Xh)\n",
				   status);

			if (lp->cur_tx != -1 && !lp->timed_out) {
				if (!(status & TXACKflag)) {
					if (lp->lasttrans_dest != 0) {
						arc_printk(D_EXTRA, dev,
							   "transmit was not acknowledged! (status=%Xh, dest=%02Xh)\n",
							   status,
							   lp->lasttrans_dest);
						dev->stats.tx_errors++;
						dev->stats.tx_carrier_errors++;
					} else {
						arc_printk(D_DURING, dev,
							   "broadcast was not acknowledged; that's normal (status=%Xh, dest=%02Xh)\n",
							   status,
							   lp->lasttrans_dest);
					}
				}

				if (lp->outgoing.proto &&
				    lp->outgoing.proto->ack_tx) {
					lp->outgoing.proto
						->ack_tx(dev, ackstatus);
				}
				lp->reply_status = ackstatus;
				tasklet_hi_schedule(&lp->reply_tasklet);
			}
			if (lp->cur_tx != -1)
				release_arcbuf(dev, lp->cur_tx);

			lp->cur_tx = -1;
			lp->timed_out = 0;
			didsomething++;

			/* send another packet if there is one */
			go_tx(dev);

			/* continue a split packet, if any */
			if (lp->outgoing.proto &&
			    lp->outgoing.proto->continue_tx) {
				int txbuf = get_arcbuf(dev);

				if (txbuf != -1) {
					if (lp->outgoing.proto->continue_tx(dev, txbuf)) {
						/* that was the last segment */
						dev->stats.tx_bytes += lp->outgoing.skb->len;
						if (!lp->outgoing.proto->ack_tx) {
							dev_kfree_skb_irq(lp->outgoing.skb);
							lp->outgoing.proto = NULL;
						}
					}
					lp->next_tx = txbuf;
				}
			}
			/* inform upper layers of idleness, if necessary */
			if (lp->cur_tx == -1)
				netif_wake_queue(dev);
		}
		/* now process the received packet, if any */
		if (recbuf != -1) {
			if (BUGLVL(D_RX))
				arcnet_dump_packet(dev, recbuf, "rx irq", 0);

			arcnet_rx(dev, recbuf);
			release_arcbuf(dev, recbuf);

			didsomething++;
		}
		if (status & lp->intmask & RECONflag) {
			lp->hw.command(dev, CFLAGScmd | CONFIGclear);
			dev->stats.tx_carrier_errors++;

			arc_printk(D_RECON, dev, "Network reconfiguration detected (status=%Xh)\n",
				   status);
			if (netif_carrier_ok(dev)) {
				netif_carrier_off(dev);
				netdev_info(dev, "link down\n");
			}
			mod_timer(&lp->timer, jiffies + msecs_to_jiffies(1000));

			arcnet_led_event(dev, ARCNET_LED_EVENT_RECON);
			/* MYRECON bit is at bit 7 of diagstatus */
			if (diagstatus & 0x80)
				arc_printk(D_RECON, dev, "Put out that recon myself\n");

			/* is the RECON info empty or old? */
			if (!lp->first_recon || !lp->last_recon ||
			    time_after(jiffies, lp->last_recon + HZ * 10)) {
				if (lp->network_down)
					arc_printk(D_NORMAL, dev, "reconfiguration detected: cabling restored?\n");
				lp->first_recon = lp->last_recon = jiffies;
				lp->num_recons = lp->network_down = 0;

				arc_printk(D_DURING, dev, "recon: clearing counters.\n");
			} else {	/* add to current RECON counter */
				lp->last_recon = jiffies;
				lp->num_recons++;

				arc_printk(D_DURING, dev, "recon: counter=%d, time=%lds, net=%d\n",
					   lp->num_recons,
					   (lp->last_recon - lp->first_recon) / HZ,
					   lp->network_down);

				/* if network is marked up;
				 * and first_recon and last_recon are 60+ apart;
				 * and the average no. of recons counted is
				 *    > RECON_THRESHOLD/min;
				 * then print a warning message.
				 */
				if (!lp->network_down &&
				    (lp->last_recon - lp->first_recon) <= HZ * 60 &&
				    lp->num_recons >= RECON_THRESHOLD) {
					lp->network_down = 1;
					arc_printk(D_NORMAL, dev, "many reconfigurations detected: cabling problem?\n");
				} else if (!lp->network_down &&
					   lp->last_recon - lp->first_recon > HZ * 60) {
					/* reset counters if we've gone for
					 *  over a minute.
					 */
					lp->first_recon = lp->last_recon;
					lp->num_recons = 1;
				}
			}
		} else if (lp->network_down &&
			   time_after(jiffies, lp->last_recon + HZ * 10)) {
			if (lp->network_down)
				arc_printk(D_NORMAL, dev, "cabling restored?\n");
			lp->first_recon = lp->last_recon = 0;
			lp->num_recons = lp->network_down = 0;

			arc_printk(D_DURING, dev, "not recon: clearing counters anyway.\n");
			netif_carrier_on(dev);
		}

		if (didsomething)
			retval |= IRQ_HANDLED;
	} while (--boguscount && didsomething);

	arc_printk(D_DURING, dev, "arcnet_interrupt complete (status=%Xh, count=%d)\n",
		   lp->hw.status(dev), boguscount);
	arc_printk(D_DURING, dev, "\n");

	lp->hw.intmask(dev, 0);
	udelay(1);
	lp->hw.intmask(dev, lp->intmask);

out:
	spin_unlock_irqrestore(&lp->lock, flags);
	return retval;
}
EXPORT_SYMBOL(arcnet_interrupt);

/* This is a generic packet receiver that calls arcnet??_rx depending on the
 * protocol ID found.
 */
static void arcnet_rx(struct net_device *dev, int bufnum)
{
	struct arcnet_local *lp = netdev_priv(dev);
	union {
		struct archdr pkt;
		char buf[512];
	} rxdata;
	struct arc_rfc1201 *soft;
	int length, ofs;

	soft = &rxdata.pkt.soft.rfc1201;

	lp->hw.copy_from_card(dev, bufnum, 0, &rxdata.pkt, ARC_HDR_SIZE);
	if (rxdata.pkt.hard.offset[0]) {
		ofs = rxdata.pkt.hard.offset[0];
		length = 256 - ofs;
	} else {
		ofs = rxdata.pkt.hard.offset[1];
		length = 512 - ofs;
	}

	/* get the full header, if possible */
	if (sizeof(rxdata.pkt.soft) <= length) {
		lp->hw.copy_from_card(dev, bufnum, ofs, soft, sizeof(rxdata.pkt.soft));
	} else {
		memset(&rxdata.pkt.soft, 0, sizeof(rxdata.pkt.soft));
		lp->hw.copy_from_card(dev, bufnum, ofs, soft, length);
	}

	arc_printk(D_DURING, dev, "Buffer #%d: received packet from %02Xh to %02Xh (%d+4 bytes)\n",
		   bufnum, rxdata.pkt.hard.source, rxdata.pkt.hard.dest, length);

	dev->stats.rx_packets++;
	dev->stats.rx_bytes += length + ARC_HDR_SIZE;

	/* call the right receiver for the protocol */
	if (arc_proto_map[soft->proto]->is_ip) {
		if (BUGLVL(D_PROTO)) {
			struct ArcProto
			*oldp = arc_proto_map[lp->default_proto[rxdata.pkt.hard.source]],
			*newp = arc_proto_map[soft->proto];

			if (oldp != newp) {
				arc_printk(D_PROTO, dev,
					   "got protocol %02Xh; encap for host %02Xh is now '%c' (was '%c')\n",
					   soft->proto, rxdata.pkt.hard.source,
					   newp->suffix, oldp->suffix);
			}
		}

		/* broadcasts will always be done with the last-used encap. */
		lp->default_proto[0] = soft->proto;

		/* in striking contrast, the following isn't a hack. */
		lp->default_proto[rxdata.pkt.hard.source] = soft->proto;
	}
	/* call the protocol-specific receiver. */
	arc_proto_map[soft->proto]->rx(dev, bufnum, &rxdata.pkt, length);
}

static void null_rx(struct net_device *dev, int bufnum,
		    struct archdr *pkthdr, int length)
{
	arc_printk(D_PROTO, dev,
		   "rx: don't know how to deal with proto %02Xh from host %02Xh.\n",
		   pkthdr->soft.rfc1201.proto, pkthdr->hard.source);
}

static int null_build_header(struct sk_buff *skb, struct net_device *dev,
			     unsigned short type, uint8_t daddr)
{
	struct arcnet_local *lp = netdev_priv(dev);

	arc_printk(D_PROTO, dev,
		   "tx: can't build header for encap %02Xh; load a protocol driver.\n",
		   lp->default_proto[daddr]);

	/* always fails */
	return 0;
}

/* the "do nothing" prepare_tx function warns that there's nothing to do. */
static int null_prepare_tx(struct net_device *dev, struct archdr *pkt,
			   int length, int bufnum)
{
	struct arcnet_local *lp = netdev_priv(dev);
	struct arc_hardware newpkt;

	arc_printk(D_PROTO, dev, "tx: no encap for this host; load a protocol driver.\n");

	/* send a packet to myself -- will never get received, of course */
	newpkt.source = newpkt.dest = dev->dev_addr[0];

	/* only one byte of actual data (and it's random) */
	newpkt.offset[0] = 0xFF;

	lp->hw.copy_to_card(dev, bufnum, 0, &newpkt, ARC_HDR_SIZE);

	return 1;		/* done */
}
