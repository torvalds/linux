/*
 * Linux ARCnet driver - "raw mode" packet encapsulation (no soft headers)
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

#define VERSION "arcnet: raw mode (`r') encapsulation support loaded.\n"


static void rx(struct net_device *dev, int bufnum,
	       struct archdr *pkthdr, int length);
static int build_header(struct sk_buff *skb, struct net_device *dev,
			unsigned short type, uint8_t daddr);
static int prepare_tx(struct net_device *dev, struct archdr *pkt, int length,
		      int bufnum);

static struct ArcProto rawmode_proto =
{
	.suffix		= 'r',
	.mtu		= XMTU,
	.rx		= rx,
	.build_header	= build_header,
	.prepare_tx	= prepare_tx,
	.continue_tx    = NULL,
	.ack_tx         = NULL
};


static int __init arcnet_raw_init(void)
{
	int count;

	printk(VERSION);

	for (count = 0; count < 256; count++)
		if (arc_proto_map[count] == arc_proto_default)
			arc_proto_map[count] = &rawmode_proto;

	/* for raw mode, we only set the bcast proto if there's no better one */
	if (arc_bcast_proto == arc_proto_default)
		arc_bcast_proto = &rawmode_proto;

	arc_proto_default = &rawmode_proto;
	return 0;
}

static void __exit arcnet_raw_exit(void)
{
	arcnet_unregister_proto(&rawmode_proto);
}

module_init(arcnet_raw_init);
module_exit(arcnet_raw_exit);

MODULE_LICENSE("GPL");


/* packet receiver */
static void rx(struct net_device *dev, int bufnum,
	       struct archdr *pkthdr, int length)
{
	struct arcnet_local *lp = netdev_priv(dev);
	struct sk_buff *skb;
	struct archdr *pkt = pkthdr;
	int ofs;

	BUGMSG(D_DURING, "it's a raw packet (length=%d)\n", length);

	if (length > MTU)
		ofs = 512 - length;
	else
		ofs = 256 - length;

	skb = alloc_skb(length + ARC_HDR_SIZE, GFP_ATOMIC);
	if (skb == NULL) {
		BUGMSG(D_NORMAL, "Memory squeeze, dropping packet.\n");
		dev->stats.rx_dropped++;
		return;
	}
	skb_put(skb, length + ARC_HDR_SIZE);
	skb->dev = dev;

	pkt = (struct archdr *) skb->data;

	skb_reset_mac_header(skb);
	skb_pull(skb, ARC_HDR_SIZE);

	/* up to sizeof(pkt->soft) has already been copied from the card */
	memcpy(pkt, pkthdr, sizeof(struct archdr));
	if (length > sizeof(pkt->soft))
		lp->hw.copy_from_card(dev, bufnum, ofs + sizeof(pkt->soft),
				      pkt->soft.raw + sizeof(pkt->soft),
				      length - sizeof(pkt->soft));

	BUGLVL(D_SKB) arcnet_dump_skb(dev, skb, "rx");

	skb->protocol = cpu_to_be16(ETH_P_ARCNET);
;
	netif_rx(skb);
}


/*
 * Create the ARCnet hard/soft headers for raw mode.
 * There aren't any soft headers in raw mode - not even the protocol id.
 */
static int build_header(struct sk_buff *skb, struct net_device *dev,
			unsigned short type, uint8_t daddr)
{
	int hdr_size = ARC_HDR_SIZE;
	struct archdr *pkt = (struct archdr *) skb_push(skb, hdr_size);

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
	struct arcnet_local *lp = netdev_priv(dev);
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
	if (length >= MinTU) {
		hard->offset[0] = 0;
		hard->offset[1] = ofs = 512 - length;
	} else if (length > MTU) {
		hard->offset[0] = 0;
		hard->offset[1] = ofs = 512 - length - 3;
	} else
		hard->offset[0] = ofs = 256 - length;

	BUGMSG(D_DURING, "prepare_tx: length=%d ofs=%d\n",
	       length,ofs);

	lp->hw.copy_to_card(dev, bufnum, 0, hard, ARC_HDR_SIZE);
	lp->hw.copy_to_card(dev, bufnum, ofs, &pkt->soft, length);

	lp->lastload_dest = hard->dest;

	return 1;		/* done */
}
