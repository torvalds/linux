/*
 * Linux ARCnet driver - RFC1051 ("simple" standard) packet encapsulation
 * 
 * Written 1994-1999 by Avery Pennarun.
 * Derived from skeleton.c by Donald Becker.
 *
 * Special thanks to Contemporary Controls, Inc. (www.ccontrols.com)
 *  for sponsoring the further development of this driver.
 *
 * **********************
 *
 * The original copyright of skeleton.c was as follows:
 *
 * skeleton.c Written 1993 by Donald Becker.
 * Copyright 1993 United States Government as represented by the
 * Director, National Security Agency.  This software may only be used
 * and distributed according to the terms of the GNU General Public License as
 * modified by SRC, incorporated herein by reference.
 *
 * **********************
 *
 * For more details, see drivers/net/arcnet.c
 *
 * **********************
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/if_arp.h>
#include <net/arp.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/arcdevice.h>

#define VERSION "arcnet: RFC1051 \"simple standard\" (`s') encapsulation support loaded.\n"


static unsigned short type_trans(struct sk_buff *skb, struct net_device *dev);
static void rx(struct net_device *dev, int bufnum,
	       struct archdr *pkthdr, int length);
static int build_header(struct sk_buff *skb, struct net_device *dev,
			unsigned short type, uint8_t daddr);
static int prepare_tx(struct net_device *dev, struct archdr *pkt, int length,
		      int bufnum);


static struct ArcProto rfc1051_proto =
{
	.suffix		= 's',
	.mtu		= XMTU - RFC1051_HDR_SIZE,
	.is_ip          = 1,
	.rx		= rx,
	.build_header	= build_header,
	.prepare_tx	= prepare_tx,
	.continue_tx    = NULL,
	.ack_tx         = NULL
};


static int __init arcnet_rfc1051_init(void)
{
	printk(VERSION);

	arc_proto_map[ARC_P_IP_RFC1051]
	    = arc_proto_map[ARC_P_ARP_RFC1051]
	    = &rfc1051_proto;

	/* if someone else already owns the broadcast, we won't take it */
	if (arc_bcast_proto == arc_proto_default)
		arc_bcast_proto = &rfc1051_proto;

	return 0;
}

static void __exit arcnet_rfc1051_exit(void)
{
	arcnet_unregister_proto(&rfc1051_proto);
}

module_init(arcnet_rfc1051_init);
module_exit(arcnet_rfc1051_exit);

MODULE_LICENSE("GPL");

/*
 * Determine a packet's protocol ID.
 * 
 * With ARCnet we have to convert everything to Ethernet-style stuff.
 */
static unsigned short type_trans(struct sk_buff *skb, struct net_device *dev)
{
	struct arcnet_local *lp = dev->priv;
	struct archdr *pkt = (struct archdr *) skb->data;
	struct arc_rfc1051 *soft = &pkt->soft.rfc1051;
	int hdr_size = ARC_HDR_SIZE + RFC1051_HDR_SIZE;

	/* Pull off the arcnet header. */
	skb_reset_mac_header(skb);
	skb_pull(skb, hdr_size);

	if (pkt->hard.dest == 0)
		skb->pkt_type = PACKET_BROADCAST;
	else if (dev->flags & IFF_PROMISC) {
		/* if we're not sending to ourselves :) */
		if (pkt->hard.dest != dev->dev_addr[0])
			skb->pkt_type = PACKET_OTHERHOST;
	}
	/* now return the protocol number */
	switch (soft->proto) {
	case ARC_P_IP_RFC1051:
		return htons(ETH_P_IP);
	case ARC_P_ARP_RFC1051:
		return htons(ETH_P_ARP);

	default:
		lp->stats.rx_errors++;
		lp->stats.rx_crc_errors++;
		return 0;
	}

	return htons(ETH_P_IP);
}


/* packet receiver */
static void rx(struct net_device *dev, int bufnum,
	       struct archdr *pkthdr, int length)
{
	struct arcnet_local *lp = dev->priv;
	struct sk_buff *skb;
	struct archdr *pkt = pkthdr;
	int ofs;

	BUGMSG(D_DURING, "it's a raw packet (length=%d)\n", length);

	if (length >= MinTU)
		ofs = 512 - length;
	else
		ofs = 256 - length;

	skb = alloc_skb(length + ARC_HDR_SIZE, GFP_ATOMIC);
	if (skb == NULL) {
		BUGMSG(D_NORMAL, "Memory squeeze, dropping packet.\n");
		lp->stats.rx_dropped++;
		return;
	}
	skb_put(skb, length + ARC_HDR_SIZE);
	skb->dev = dev;

	pkt = (struct archdr *) skb->data;

	/* up to sizeof(pkt->soft) has already been copied from the card */
	memcpy(pkt, pkthdr, sizeof(struct archdr));
	if (length > sizeof(pkt->soft))
		lp->hw.copy_from_card(dev, bufnum, ofs + sizeof(pkt->soft),
				      pkt->soft.raw + sizeof(pkt->soft),
				      length - sizeof(pkt->soft));

	BUGLVL(D_SKB) arcnet_dump_skb(dev, skb, "rx");

	skb->protocol = type_trans(skb, dev);
	netif_rx(skb);
	dev->last_rx = jiffies;
}


/*
 * Create the ARCnet hard/soft headers for RFC1051.
 */
static int build_header(struct sk_buff *skb, struct net_device *dev,
			unsigned short type, uint8_t daddr)
{
	struct arcnet_local *lp = dev->priv;
	int hdr_size = ARC_HDR_SIZE + RFC1051_HDR_SIZE;
	struct archdr *pkt = (struct archdr *) skb_push(skb, hdr_size);
	struct arc_rfc1051 *soft = &pkt->soft.rfc1051;

	/* set the protocol ID according to RFC1051 */
	switch (type) {
	case ETH_P_IP:
		soft->proto = ARC_P_IP_RFC1051;
		break;
	case ETH_P_ARP:
		soft->proto = ARC_P_ARP_RFC1051;
		break;
	default:
		BUGMSG(D_NORMAL, "RFC1051: I don't understand protocol %d (%Xh)\n",
		       type, type);
		lp->stats.tx_errors++;
		lp->stats.tx_aborted_errors++;
		return 0;
	}


	/*
	 * Set the source hardware address.
	 *
	 * This is pretty pointless for most purposes, but it can help in
	 * debugging.  ARCnet does not allow us to change the source address in
	 * the actual packet sent)
	 */
	pkt->hard.source = *dev->dev_addr;

	/* see linux/net/ethernet/eth.c to see where I got the following */

	if (dev->flags & (IFF_LOOPBACK | IFF_NOARP)) {
		/* 
		 * FIXME: fill in the last byte of the dest ipaddr here to better
		 * comply with RFC1051 in "noarp" mode.
		 */
		pkt->hard.dest = 0;
		return hdr_size;
	}
	/* otherwise, just fill it in and go! */
	pkt->hard.dest = daddr;

	return hdr_size;	/* success */
}


static int prepare_tx(struct net_device *dev, struct archdr *pkt, int length,
		      int bufnum)
{
	struct arcnet_local *lp = dev->priv;
	struct arc_hardware *hard = &pkt->hard;
	int ofs;

	BUGMSG(D_DURING, "prepare_tx: txbufs=%d/%d/%d\n",
	       lp->next_tx, lp->cur_tx, bufnum);

	length -= ARC_HDR_SIZE;	/* hard header is not included in packet length */

	if (length > XMTU) {
		/* should never happen! other people already check for this. */
		BUGMSG(D_NORMAL, "Bug!  prepare_tx with size %d (> %d)\n",
		       length, XMTU);
		length = XMTU;
	}
	if (length > MinTU) {
		hard->offset[0] = 0;
		hard->offset[1] = ofs = 512 - length;
	} else if (length > MTU) {
		hard->offset[0] = 0;
		hard->offset[1] = ofs = 512 - length - 3;
	} else
		hard->offset[0] = ofs = 256 - length;

	lp->hw.copy_to_card(dev, bufnum, 0, hard, ARC_HDR_SIZE);
	lp->hw.copy_to_card(dev, bufnum, ofs, &pkt->soft, length);

	lp->lastload_dest = hard->dest;

	return 1;		/* done */
}
