/*D:500
 * The Guest network driver.
 *
 * This is very simple a virtual network driver, and our last Guest driver.
 * The only trick is that it can talk directly to multiple other recipients
 * (ie. other Guests on the same network).  It can also be used with only the
 * Host on the network.
 :*/

/* Copyright 2006 Rusty Russell <rusty@rustcorp.com.au> IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
//#define DEBUG
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/mm_types.h>
#include <linux/io.h>
#include <linux/lguest_bus.h>

#define SHARED_SIZE		PAGE_SIZE
#define MAX_LANS		4
#define NUM_SKBS		8

/*M:011 Network code master Jeff Garzik points out numerous shortcomings in
 * this driver if it aspires to greatness.
 *
 * Firstly, it doesn't use "NAPI": the networking's New API, and is poorer for
 * it.  As he says "NAPI means system-wide load leveling, across multiple
 * network interfaces.  Lack of NAPI can mean competition at higher loads."
 *
 * He also points out that we don't implement set_mac_address, so users cannot
 * change the devices hardware address.  When I asked why one would want to:
 * "Bonding, and situations where you /do/ want the MAC address to "leak" out
 * of the host onto the wider net."
 *
 * Finally, he would like module unloading: "It is not unrealistic to think of
 * [un|re|]loading the net support module in an lguest guest.  And, adding
 * module support makes the programmer more responsible, because they now have
 * to learn to clean up after themselves.  Any driver that cannot clean up
 * after itself is an incomplete driver in my book."
 :*/

/*D:530 The "struct lguestnet_info" contains all the information we need to
 * know about the network device. */
struct lguestnet_info
{
	/* The mapped device page(s) (an array of "struct lguest_net"). */
	struct lguest_net *peer;
	/* The physical address of the device page(s) */
	unsigned long peer_phys;
	/* The size of the device page(s). */
	unsigned long mapsize;

	/* The lguest_device I come from */
	struct lguest_device *lgdev;

	/* My peerid (ie. my slot in the array). */
	unsigned int me;

	/* Receive queue: the network packets waiting to be filled. */
	struct sk_buff *skb[NUM_SKBS];
	struct lguest_dma dma[NUM_SKBS];
};
/*:*/

/* How many bytes left in this page. */
static unsigned int rest_of_page(void *data)
{
	return PAGE_SIZE - ((unsigned long)data % PAGE_SIZE);
}

/*D:570 Each peer (ie. Guest or Host) on the network binds their receive
 * buffers to a different key: we simply use the physical address of the
 * device's memory page plus the peer number.  The Host insists that all keys
 * be a multiple of 4, so we multiply the peer number by 4. */
static unsigned long peer_key(struct lguestnet_info *info, unsigned peernum)
{
	return info->peer_phys + 4 * peernum;
}

/* This is the routine which sets up a "struct lguest_dma" to point to a
 * network packet, similar to req_to_dma() in lguest_blk.c.  The structure of a
 * "struct sk_buff" has grown complex over the years: it consists of a "head"
 * linear section pointed to by "skb->data", and possibly an array of
 * "fragments" in the case of a non-linear packet.
 *
 * Our receive buffers don't use fragments at all but outgoing skbs might, so
 * we handle it. */
static void skb_to_dma(const struct sk_buff *skb, unsigned int headlen,
		       struct lguest_dma *dma)
{
	unsigned int i, seg;

	/* First, we put the linear region into the "struct lguest_dma".  Each
	 * entry can't go over a page boundary, so even though all our packets
	 * are 1514 bytes or less, we might need to use two entries here: */
	for (i = seg = 0; i < headlen; seg++, i += rest_of_page(skb->data+i)) {
		dma->addr[seg] = virt_to_phys(skb->data + i);
		dma->len[seg] = min((unsigned)(headlen - i),
				    rest_of_page(skb->data + i));
	}

	/* Now we handle the fragments: at least they're guaranteed not to go
	 * over a page.  skb_shinfo(skb) returns a pointer to the structure
	 * which tells us about the number of fragments and the fragment
	 * array. */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++, seg++) {
		const skb_frag_t *f = &skb_shinfo(skb)->frags[i];
		/* Should not happen with MTU less than 64k - 2 * PAGE_SIZE. */
		if (seg == LGUEST_MAX_DMA_SECTIONS) {
			/* We will end up sending a truncated packet should
			 * this ever happen.  Plus, a cool log message! */
			printk("Woah dude!  Megapacket!\n");
			break;
		}
		dma->addr[seg] = page_to_phys(f->page) + f->page_offset;
		dma->len[seg] = f->size;
	}

	/* If after all that we didn't use the entire "struct lguest_dma"
	 * array, we terminate it with a 0 length. */
	if (seg < LGUEST_MAX_DMA_SECTIONS)
		dma->len[seg] = 0;
}

/*
 * Packet transmission.
 *
 * Our packet transmission is a little unusual.  A real network card would just
 * send out the packet and leave the receivers to decide if they're interested.
 * Instead, we look through the network device memory page and see if any of
 * the ethernet addresses match the packet destination, and if so we send it to
 * that Guest.
 *
 * This is made a little more complicated in two cases.  The first case is
 * broadcast packets: for that we send the packet to all Guests on the network,
 * one at a time.  The second case is "promiscuous" mode, where a Guest wants
 * to see all the packets on the network.  We need a way for the Guest to tell
 * us it wants to see all packets, so it sets the "multicast" bit on its
 * published MAC address, which is never valid in a real ethernet address.
 */
#define PROMISC_BIT		0x01

/* This is the callback which is summoned whenever the network device's
 * multicast or promiscuous state changes.  If the card is in promiscuous mode,
 * we advertise that in our ethernet address in the device's memory.  We do the
 * same if Linux wants any or all multicast traffic.  */
static void lguestnet_set_multicast(struct net_device *dev)
{
	struct lguestnet_info *info = netdev_priv(dev);

	if ((dev->flags & (IFF_PROMISC|IFF_ALLMULTI)) || dev->mc_count)
		info->peer[info->me].mac[0] |= PROMISC_BIT;
	else
		info->peer[info->me].mac[0] &= ~PROMISC_BIT;
}

/* A simple test function to see if a peer wants to see all packets.*/
static int promisc(struct lguestnet_info *info, unsigned int peer)
{
	return info->peer[peer].mac[0] & PROMISC_BIT;
}

/* Another simple function to see if a peer's advertised ethernet address
 * matches a packet's destination ethernet address. */
static int mac_eq(const unsigned char mac[ETH_ALEN],
		  struct lguestnet_info *info, unsigned int peer)
{
	/* Ignore multicast bit, which peer turns on to mean promisc. */
	if ((info->peer[peer].mac[0] & (~PROMISC_BIT)) != mac[0])
		return 0;
	return memcmp(mac+1, info->peer[peer].mac+1, ETH_ALEN-1) == 0;
}

/* This is the function which actually sends a packet once we've decided a
 * peer wants it: */
static void transfer_packet(struct net_device *dev,
			    struct sk_buff *skb,
			    unsigned int peernum)
{
	struct lguestnet_info *info = netdev_priv(dev);
	struct lguest_dma dma;

	/* We use our handy "struct lguest_dma" packing function to prepare
	 * the skb for sending. */
	skb_to_dma(skb, skb_headlen(skb), &dma);
	pr_debug("xfer length %04x (%u)\n", htons(skb->len), skb->len);

	/* This is the actual send call which copies the packet. */
	lguest_send_dma(peer_key(info, peernum), &dma);

	/* Check that the entire packet was transmitted.  If not, it could mean
	 * that the other Guest registered a short receive buffer, but this
	 * driver should never do that.  More likely, the peer is dead. */
	if (dma.used_len != skb->len) {
		dev->stats.tx_carrier_errors++;
		pr_debug("Bad xfer to peer %i: %i of %i (dma %p/%i)\n",
			 peernum, dma.used_len, skb->len,
			 (void *)dma.addr[0], dma.len[0]);
	} else {
		/* On success we update the stats. */
		dev->stats.tx_bytes += skb->len;
		dev->stats.tx_packets++;
	}
}

/* Another helper function to tell is if a slot in the device memory is unused.
 * Since we always set the Local Assignment bit in the ethernet address, the
 * first byte can never be 0. */
static int unused_peer(const struct lguest_net peer[], unsigned int num)
{
	return peer[num].mac[0] == 0;
}

/* Finally, here is the routine which handles an outgoing packet.  It's called
 * "start_xmit" for traditional reasons. */
static int lguestnet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned int i;
	int broadcast;
	struct lguestnet_info *info = netdev_priv(dev);
	/* Extract the destination ethernet address from the packet. */
	const unsigned char *dest = ((struct ethhdr *)skb->data)->h_dest;
	DECLARE_MAC_BUF(mac);

	pr_debug("%s: xmit %s\n", dev->name, print_mac(mac, dest));

	/* If it's a multicast packet, we broadcast to everyone.  That's not
	 * very efficient, but there are very few applications which actually
	 * use multicast, which is a shame really.
	 *
	 * As etherdevice.h points out: "By definition the broadcast address is
	 * also a multicast address."  So we don't have to test for broadcast
	 * packets separately. */
	broadcast = is_multicast_ether_addr(dest);

	/* Look through all the published ethernet addresses to see if we
	 * should send this packet. */
	for (i = 0; i < info->mapsize/sizeof(struct lguest_net); i++) {
		/* We don't send to ourselves (we actually can't SEND_DMA to
		 * ourselves anyway), and don't send to unused slots.*/
		if (i == info->me || unused_peer(info->peer, i))
			continue;

		/* If it's broadcast we send it.  If they want every packet we
		 * send it.  If the destination matches their address we send
		 * it.  Otherwise we go to the next peer. */
		if (!broadcast && !promisc(info, i) && !mac_eq(dest, info, i))
			continue;

		pr_debug("lguestnet %s: sending from %i to %i\n",
			 dev->name, info->me, i);
		/* Our routine which actually does the transfer. */
		transfer_packet(dev, skb, i);
	}

	/* An xmit routine is expected to dispose of the packet, so we do. */
	dev_kfree_skb(skb);

	/* As per kernel convention, 0 means success.  This is why I love
	 * networking: even if we never sent to anyone, that's still
	 * success! */
	return 0;
}

/*D:560
 * Packet receiving.
 *
 * First, here's a helper routine which fills one of our array of receive
 * buffers: */
static int fill_slot(struct net_device *dev, unsigned int slot)
{
	struct lguestnet_info *info = netdev_priv(dev);

	/* We can receive ETH_DATA_LEN (1500) byte packets, plus a standard
	 * ethernet header of ETH_HLEN (14) bytes. */
	info->skb[slot] = netdev_alloc_skb(dev, ETH_HLEN + ETH_DATA_LEN);
	if (!info->skb[slot]) {
		printk("%s: could not fill slot %i\n", dev->name, slot);
		return -ENOMEM;
	}

	/* skb_to_dma() is a helper which sets up the "struct lguest_dma" to
	 * point to the data in the skb: we also use it for sending out a
	 * packet. */
	skb_to_dma(info->skb[slot], ETH_HLEN + ETH_DATA_LEN, &info->dma[slot]);

	/* This is a Write Memory Barrier: it ensures that the entry in the
	 * receive buffer array is written *before* we set the "used_len" entry
	 * to 0.  If the Host were looking at the receive buffer array from a
	 * different CPU, it could potentially see "used_len = 0" and not see
	 * the updated receive buffer information.  This would be a horribly
	 * nasty bug, so make sure the compiler and CPU know this has to happen
	 * first. */
	wmb();
	/* Writing 0 to "used_len" tells the Host it can use this receive
	 * buffer now. */
	info->dma[slot].used_len = 0;
	return 0;
}

/* This is the actual receive routine.  When we receive an interrupt from the
 * Host to tell us a packet has been delivered, we arrive here: */
static irqreturn_t lguestnet_rcv(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct lguestnet_info *info = netdev_priv(dev);
	unsigned int i, done = 0;

	/* Look through our entire receive array for an entry which has data
	 * in it. */
	for (i = 0; i < ARRAY_SIZE(info->dma); i++) {
		unsigned int length;
		struct sk_buff *skb;

		length = info->dma[i].used_len;
		if (length == 0)
			continue;

		/* We've found one!  Remember the skb (we grabbed the length
		 * above), and immediately refill the slot we've taken it
		 * from. */
		done++;
		skb = info->skb[i];
		fill_slot(dev, i);

		/* This shouldn't happen: micropackets could be sent by a
		 * badly-behaved Guest on the network, but the Host will never
		 * stuff more data in the buffer than the buffer length. */
		if (length < ETH_HLEN || length > ETH_HLEN + ETH_DATA_LEN) {
			pr_debug(KERN_WARNING "%s: unbelievable skb len: %i\n",
				 dev->name, length);
			dev_kfree_skb(skb);
			continue;
		}

		/* skb_put(), what a great function!  I've ranted about this
		 * function before (http://lkml.org/lkml/1999/9/26/24).  You
		 * call it after you've added data to the end of an skb (in
		 * this case, it was the Host which wrote the data). */
		skb_put(skb, length);

		/* The ethernet header contains a protocol field: we use the
		 * standard helper to extract it, and place the result in
		 * skb->protocol.  The helper also sets up skb->pkt_type and
		 * eats up the ethernet header from the front of the packet. */
		skb->protocol = eth_type_trans(skb, dev);

		/* If this device doesn't need checksums for sending, we also
		 * don't need to check the packets when they come in. */
		if (dev->features & NETIF_F_NO_CSUM)
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		/* As a last resort for debugging the driver or the lguest I/O
		 * subsystem, you can uncomment the "#define DEBUG" at the top
		 * of this file, which turns all the pr_debug() into printk()
		 * and floods the logs. */
		pr_debug("Receiving skb proto 0x%04x len %i type %i\n",
			 ntohs(skb->protocol), skb->len, skb->pkt_type);

		/* Update the packet and byte counts (visible from ifconfig,
		 * and good for debugging). */
		dev->stats.rx_bytes += skb->len;
		dev->stats.rx_packets++;

		/* Hand our fresh network packet into the stack's "network
		 * interface receive" routine.  That will free the packet
		 * itself when it's finished. */
		netif_rx(skb);
	}

	/* If we found any packets, we assume the interrupt was for us. */
	return done ? IRQ_HANDLED : IRQ_NONE;
}

/*D:550 This is where we start: when the device is brought up by dhcpd or
 * ifconfig.  At this point we advertise our MAC address to the rest of the
 * network, and register receive buffers ready for incoming packets. */
static int lguestnet_open(struct net_device *dev)
{
	int i;
	struct lguestnet_info *info = netdev_priv(dev);

	/* Copy our MAC address into the device page, so others on the network
	 * can find us. */
	memcpy(info->peer[info->me].mac, dev->dev_addr, ETH_ALEN);

	/* We might already be in promisc mode (dev->flags & IFF_PROMISC).  Our
	 * set_multicast callback handles this already, so we call it now. */
	lguestnet_set_multicast(dev);

	/* Allocate packets and put them into our "struct lguest_dma" array.
	 * If we fail to allocate all the packets we could still limp along,
	 * but it's a sign of real stress so we should probably give up now. */
	for (i = 0; i < ARRAY_SIZE(info->dma); i++) {
		if (fill_slot(dev, i) != 0)
			goto cleanup;
	}

	/* Finally we tell the Host where our array of "struct lguest_dma"
	 * receive buffers is, binding it to the key corresponding to the
	 * device's physical memory plus our peerid. */
	if (lguest_bind_dma(peer_key(info,info->me), info->dma,
			    NUM_SKBS, lgdev_irq(info->lgdev)) != 0)
		goto cleanup;
	return 0;

cleanup:
	while (--i >= 0)
		dev_kfree_skb(info->skb[i]);
	return -ENOMEM;
}
/*:*/

/* The close routine is called when the device is no longer in use: we clean up
 * elegantly. */
static int lguestnet_close(struct net_device *dev)
{
	unsigned int i;
	struct lguestnet_info *info = netdev_priv(dev);

	/* Clear all trace of our existence out of the device memory by setting
	 * the slot which held our MAC address to 0 (unused). */
	memset(&info->peer[info->me], 0, sizeof(info->peer[info->me]));

	/* Unregister our array of receive buffers */
	lguest_unbind_dma(peer_key(info, info->me), info->dma);
	for (i = 0; i < ARRAY_SIZE(info->dma); i++)
		dev_kfree_skb(info->skb[i]);
	return 0;
}

/*D:510 The network device probe function is basically a standard ethernet
 * device setup.  It reads the "struct lguest_device_desc" and sets the "struct
 * net_device".  Oh, the line-by-line excitement!  Let's skip over it. :*/
static int lguestnet_probe(struct lguest_device *lgdev)
{
	int err, irqf = IRQF_SHARED;
	struct net_device *dev;
	struct lguestnet_info *info;
	struct lguest_device_desc *desc = &lguest_devices[lgdev->index];

	pr_debug("lguest_net: probing for device %i\n", lgdev->index);

	dev = alloc_etherdev(sizeof(struct lguestnet_info));
	if (!dev)
		return -ENOMEM;

	/* Ethernet defaults with some changes */
	ether_setup(dev);
	dev->set_mac_address = NULL;

	dev->dev_addr[0] = 0x02; /* set local assignment bit (IEEE802) */
	dev->dev_addr[1] = 0x00;
	memcpy(&dev->dev_addr[2], &lguest_data.guestid, 2);
	dev->dev_addr[4] = 0x00;
	dev->dev_addr[5] = 0x00;

	dev->open = lguestnet_open;
	dev->stop = lguestnet_close;
	dev->hard_start_xmit = lguestnet_start_xmit;

	/* We don't actually support multicast yet, but turning on/off
	 * promisc also calls dev->set_multicast_list. */
	dev->set_multicast_list = lguestnet_set_multicast;
	SET_NETDEV_DEV(dev, &lgdev->dev);

	/* The network code complains if you have "scatter-gather" capability
	 * if you don't also handle checksums (it seem that would be
	 * "illogical").  So we use a lie of omission and don't tell it that we
	 * can handle scattered packets unless we also don't want checksums,
	 * even though to us they're completely independent. */
	if (desc->features & LGUEST_NET_F_NOCSUM)
		dev->features = NETIF_F_SG|NETIF_F_NO_CSUM;

	info = netdev_priv(dev);
	info->mapsize = PAGE_SIZE * desc->num_pages;
	info->peer_phys = ((unsigned long)desc->pfn << PAGE_SHIFT);
	info->lgdev = lgdev;
	info->peer = lguest_map(info->peer_phys, desc->num_pages);
	if (!info->peer) {
		err = -ENOMEM;
		goto free;
	}

	/* This stores our peerid (upper bits reserved for future). */
	info->me = (desc->features & (info->mapsize-1));

	err = register_netdev(dev);
	if (err) {
		pr_debug("lguestnet: registering device failed\n");
		goto unmap;
	}

	if (lguest_devices[lgdev->index].features & LGUEST_DEVICE_F_RANDOMNESS)
		irqf |= IRQF_SAMPLE_RANDOM;
	if (request_irq(lgdev_irq(lgdev), lguestnet_rcv, irqf, "lguestnet",
			dev) != 0) {
		pr_debug("lguestnet: cannot get irq %i\n", lgdev_irq(lgdev));
		goto unregister;
	}

	pr_debug("lguestnet: registered device %s\n", dev->name);
	/* Finally, we put the "struct net_device" in the generic "struct
	 * lguest_device"s private pointer.  Again, it's not necessary, but
	 * makes sure the cool kernel kids don't tease us. */
	lgdev->private = dev;
	return 0;

unregister:
	unregister_netdev(dev);
unmap:
	lguest_unmap(info->peer);
free:
	free_netdev(dev);
	return err;
}

static struct lguest_driver lguestnet_drv = {
	.name = "lguestnet",
	.owner = THIS_MODULE,
	.device_type = LGUEST_DEVICE_T_NET,
	.probe = lguestnet_probe,
};

static __init int lguestnet_init(void)
{
	return register_lguest_driver(&lguestnet_drv);
}
module_init(lguestnet_init);

MODULE_DESCRIPTION("Lguest network driver");
MODULE_LICENSE("GPL");

/*D:580
 * This is the last of the Drivers, and with this we have covered the many and
 * wonderous and fine (and boring) details of the Guest.
 *
 * "make Launcher" beckons, where we answer questions like "Where do Guests
 * come from?", and "What do you do when someone asks for optimization?"
 */
