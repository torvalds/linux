/*
 * Linux ARCnet driver - "cap mode" packet encapsulation.
 * It adds sequence numbers to packets for communicating between a user space
 * application and the driver. After a transmit it sends a packet with protocol
 * byte 0 back up to the userspace containing the sequence number of the packet
 * plus the transmit-status on the ArcNet.
 *
 * Written 2002-4 by Esben Nielsen, Vestas Wind Systems A/S
 * Derived from arc-rawmode.c by Avery Pennarun.
 * arc-rawmode was in turned based on skeleton.c, see below.
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

#define VERSION "arcnet: cap mode (`c') encapsulation support loaded.\n"


static void rx(struct net_device *dev, int bufnum,
	       struct archdr *pkthdr, int length);
static int build_header(struct sk_buff *skb,
			struct net_device *dev,
			unsigned short type,
			uint8_t daddr);
static int prepare_tx(struct net_device *dev, struct archdr *pkt, int length,
		      int bufnum);
static int ack_tx(struct net_device *dev, int acked);


static struct ArcProto capmode_proto =
{
	'r',
	XMTU,
	0,
       	rx,
	build_header,
	prepare_tx,
	NULL,
	ack_tx
};


void arcnet_cap_init(void)
{
	int count;

	for (count = 1; count <= 8; count++)
		if (arc_proto_map[count] == arc_proto_default)
			arc_proto_map[count] = &capmode_proto;

	/* for cap mode, we only set the bcast proto if there's no better one */
	if (arc_bcast_proto == arc_proto_default)
		arc_bcast_proto = &capmode_proto;

	arc_proto_default = &capmode_proto;
	arc_raw_proto = &capmode_proto;
}


#ifdef MODULE

static int __init capmode_module_init(void)
{
	printk(VERSION);
	arcnet_cap_init();
	return 0;
}

static void __exit capmode_module_exit(void)
{
	arcnet_unregister_proto(&capmode_proto);
}
module_init(capmode_module_init);
module_exit(capmode_module_exit);

MODULE_LICENSE("GPL");
#endif				/* MODULE */



/* packet receiver */
static void rx(struct net_device *dev, int bufnum,
	       struct archdr *pkthdr, int length)
{
	struct arcnet_local *lp = (struct arcnet_local *) dev->priv;
	struct sk_buff *skb;
	struct archdr *pkt = pkthdr;
	char *pktbuf, *pkthdrbuf;
	int ofs;

	BUGMSG(D_DURING, "it's a raw(cap) packet (length=%d)\n", length);

	if (length >= MinTU)
		ofs = 512 - length;
	else
		ofs = 256 - length;

	skb = alloc_skb(length + ARC_HDR_SIZE + sizeof(int), GFP_ATOMIC);
	if (skb == NULL) {
		BUGMSG(D_NORMAL, "Memory squeeze, dropping packet.\n");
		lp->stats.rx_dropped++;
		return;
	}
	skb_put(skb, length + ARC_HDR_SIZE + sizeof(int));
	skb->dev = dev;
	skb_reset_mac_header(skb);
	pkt = (struct archdr *)skb_mac_header(skb);
	skb_pull(skb, ARC_HDR_SIZE);

	/* up to sizeof(pkt->soft) has already been copied from the card */
	/* squeeze in an int for the cap encapsulation */

	/* use these variables to be sure we count in bytes, not in
	   sizeof(struct archdr) */
	pktbuf=(char*)pkt;
	pkthdrbuf=(char*)pkthdr;
	memcpy(pktbuf, pkthdrbuf, ARC_HDR_SIZE+sizeof(pkt->soft.cap.proto));
	memcpy(pktbuf+ARC_HDR_SIZE+sizeof(pkt->soft.cap.proto)+sizeof(int),
	       pkthdrbuf+ARC_HDR_SIZE+sizeof(pkt->soft.cap.proto),
	       sizeof(struct archdr)-ARC_HDR_SIZE-sizeof(pkt->soft.cap.proto));

	if (length > sizeof(pkt->soft))
		lp->hw.copy_from_card(dev, bufnum, ofs + sizeof(pkt->soft),
				      pkt->soft.raw + sizeof(pkt->soft)
				      + sizeof(int),
				      length - sizeof(pkt->soft));

	BUGLVL(D_SKB) arcnet_dump_skb(dev, skb, "rx");

	skb->protocol = __constant_htons(ETH_P_ARCNET);
;
	netif_rx(skb);
	dev->last_rx = jiffies;
}


/*
 * Create the ARCnet hard/soft headers for cap mode.
 * There aren't any soft headers in cap mode - not even the protocol id.
 */
static int build_header(struct sk_buff *skb,
			struct net_device *dev,
			unsigned short type,
			uint8_t daddr)
{
	int hdr_size = ARC_HDR_SIZE;
	struct archdr *pkt = (struct archdr *) skb_push(skb, hdr_size);

	BUGMSG(D_PROTO, "Preparing header for cap packet %x.\n",
	       *((int*)&pkt->soft.cap.cookie[0]));
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
	struct arcnet_local *lp = (struct arcnet_local *) dev->priv;
	struct arc_hardware *hard = &pkt->hard;
	int ofs;


	/* hard header is not included in packet length */
	length -= ARC_HDR_SIZE;
	/* And neither is the cookie field */
	length -= sizeof(int);

	BUGMSG(D_DURING, "prepare_tx: txbufs=%d/%d/%d\n",
	       lp->next_tx, lp->cur_tx, bufnum);

	BUGMSG(D_PROTO, "Sending for cap packet %x.\n",
	       *((int*)&pkt->soft.cap.cookie[0]));

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

	BUGMSG(D_DURING, "prepare_tx: length=%d ofs=%d\n",
	       length,ofs);

	// Copy the arcnet-header + the protocol byte down:
	lp->hw.copy_to_card(dev, bufnum, 0, hard, ARC_HDR_SIZE);
	lp->hw.copy_to_card(dev, bufnum, ofs, &pkt->soft.cap.proto,
			    sizeof(pkt->soft.cap.proto));

	// Skip the extra integer we have written into it as a cookie
	// but write the rest of the message:
	lp->hw.copy_to_card(dev, bufnum, ofs+1,
			    ((unsigned char*)&pkt->soft.cap.mes),length-1);

	lp->lastload_dest = hard->dest;

	return 1;		/* done */
}


static int ack_tx(struct net_device *dev, int acked)
{
  struct arcnet_local *lp = (struct arcnet_local *) dev->priv;
  struct sk_buff *ackskb;
  struct archdr *ackpkt;
  int length=sizeof(struct arc_cap);

  BUGMSG(D_DURING, "capmode: ack_tx: protocol: %x: result: %d\n",
	 lp->outgoing.skb->protocol, acked);

  BUGLVL(D_SKB) arcnet_dump_skb(dev, lp->outgoing.skb, "ack_tx");

  /* Now alloc a skb to send back up through the layers: */
  ackskb = alloc_skb(length + ARC_HDR_SIZE , GFP_ATOMIC);
  if (ackskb == NULL) {
	  BUGMSG(D_NORMAL, "Memory squeeze, can't acknowledge.\n");
	  goto free_outskb;
  }

  skb_put(ackskb, length + ARC_HDR_SIZE );
  ackskb->dev = dev;

  skb_reset_mac_header(ackskb);
  ackpkt = (struct archdr *)skb_mac_header(ackskb);
  /* skb_pull(ackskb, ARC_HDR_SIZE); */


  skb_copy_from_linear_data(lp->outgoing.skb, ackpkt,
		ARC_HDR_SIZE + sizeof(struct arc_cap));
  ackpkt->soft.cap.proto=0; /* using protocol 0 for acknowledge */
  ackpkt->soft.cap.mes.ack=acked;

  BUGMSG(D_PROTO, "Ackknowledge for cap packet %x.\n",
	 *((int*)&ackpkt->soft.cap.cookie[0]));

  ackskb->protocol = __constant_htons(ETH_P_ARCNET);

  BUGLVL(D_SKB) arcnet_dump_skb(dev, ackskb, "ack_tx_recv");
  netif_rx(ackskb);

 free_outskb:
  dev_kfree_skb_irq(lp->outgoing.skb);
  lp->outgoing.proto = NULL; /* We are always finished when in this protocol */

  return 0;
}
