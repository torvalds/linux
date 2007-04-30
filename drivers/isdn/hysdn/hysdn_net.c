/* $Id: hysdn_net.c,v 1.8.6.4 2001/09/23 22:24:54 kai Exp $
 *
 * Linux driver for HYSDN cards, net (ethernet type) handling routines.
 *
 * Author    Werner Cornelius (werner@titro.de) for Hypercope GmbH
 * Copyright 1999 by Werner Cornelius (werner@titro.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * This net module has been inspired by the skeleton driver from
 * Donald Becker (becker@CESDIS.gsfc.nasa.gov)
 *
 */

#include <linux/module.h>
#include <linux/signal.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/inetdevice.h>

#include "hysdn_defs.h"

unsigned int hynet_enable = 0xffffffff; 
module_param(hynet_enable, uint, 0);

/* store the actual version for log reporting */
char *hysdn_net_revision = "$Revision: 1.8.6.4 $";

#define MAX_SKB_BUFFERS 20	/* number of buffers for keeping TX-data */

/****************************************************************************/
/* structure containing the complete network data. The structure is aligned */
/* in a way that both, the device and statistics are kept inside it.        */
/* for proper access, the device structure MUST be the first var/struct     */
/* inside the definition.                                                   */
/****************************************************************************/
struct net_local {
	struct net_device netdev;	/* the network device */
	struct net_device_stats stats;
	/* additional vars may be added here */
	char dev_name[9];	/* our own device name */

	/* Tx control lock.  This protects the transmit buffer ring
	 * state along with the "tx full" state of the driver.  This
	 * means all netif_queue flow control actions are protected
	 * by this lock as well.
	 */
	spinlock_t lock;
	struct sk_buff *skbs[MAX_SKB_BUFFERS];	/* pointers to tx-skbs */
	int in_idx, out_idx;	/* indexes to buffer ring */
	int sk_count;		/* number of buffers currently in ring */
};				/* net_local */


/*****************************************************/
/* Get the current statistics for this card.         */
/* This may be called with the card open or closed ! */
/*****************************************************/
static struct net_device_stats *
net_get_stats(struct net_device *dev)
{
	return (&((struct net_local *) dev)->stats);
}				/* net_device_stats */

/*********************************************************************/
/* Open/initialize the board. This is called (in the current kernel) */
/* sometime after booting when the 'ifconfig' program is run.        */
/* This routine should set everything up anew at each open, even     */
/* registers that "should" only need to be set once at boot, so that */
/* there is non-reboot way to recover if something goes wrong.       */
/*********************************************************************/
static int
net_open(struct net_device *dev)
{
	struct in_device *in_dev;
	hysdn_card *card = dev->priv;
	int i;

	netif_start_queue(dev);	/* start tx-queueing */

	/* Fill in the MAC-level header (if not already set) */
	if (!card->mac_addr[0]) {
		for (i = 0; i < ETH_ALEN - sizeof(unsigned long); i++)
			dev->dev_addr[i] = 0xfc;
		if ((in_dev = dev->ip_ptr) != NULL) {
			struct in_ifaddr *ifa = in_dev->ifa_list;
			if (ifa != NULL)
				memcpy(dev->dev_addr + (ETH_ALEN - sizeof(unsigned long)), &ifa->ifa_local, sizeof(unsigned long));
		}
	} else
		memcpy(dev->dev_addr, card->mac_addr, ETH_ALEN);

	return (0);
}				/* net_open */

/*******************************************/
/* flush the currently occupied tx-buffers */
/* must only be called when device closed  */
/*******************************************/
static void
flush_tx_buffers(struct net_local *nl)
{

	while (nl->sk_count) {
		dev_kfree_skb(nl->skbs[nl->out_idx++]);		/* free skb */
		if (nl->out_idx >= MAX_SKB_BUFFERS)
			nl->out_idx = 0;	/* wrap around */
		nl->sk_count--;
	}
}				/* flush_tx_buffers */


/*********************************************************************/
/* close/decativate the device. The device is not removed, but only  */
/* deactivated.                                                      */
/*********************************************************************/
static int
net_close(struct net_device *dev)
{

	netif_stop_queue(dev);	/* disable queueing */

	flush_tx_buffers((struct net_local *) dev);

	return (0);		/* success */
}				/* net_close */

/************************************/
/* send a packet on this interface. */
/* new style for kernel >= 2.3.33   */
/************************************/
static int
net_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev;

	spin_lock_irq(&lp->lock);

	lp->skbs[lp->in_idx++] = skb;	/* add to buffer list */
	if (lp->in_idx >= MAX_SKB_BUFFERS)
		lp->in_idx = 0;	/* wrap around */
	lp->sk_count++;		/* adjust counter */
	dev->trans_start = jiffies;

	/* If we just used up the very last entry in the
	 * TX ring on this device, tell the queueing
	 * layer to send no more.
	 */
	if (lp->sk_count >= MAX_SKB_BUFFERS)
		netif_stop_queue(dev);

	/* When the TX completion hw interrupt arrives, this
	 * is when the transmit statistics are updated.
	 */

	spin_unlock_irq(&lp->lock);

	if (lp->sk_count <= 3) {
		schedule_work(&((hysdn_card *) dev->priv)->irq_queue);
	}
	return (0);		/* success */
}				/* net_send_packet */



/***********************************************************************/
/* acknowlegde a packet send. The network layer will be informed about */
/* completion                                                          */
/***********************************************************************/
void
hysdn_tx_netack(hysdn_card * card)
{
	struct net_local *lp = card->netif;

	if (!lp)
		return;		/* non existing device */


	if (!lp->sk_count)
		return;		/* error condition */

	lp->stats.tx_packets++;
	lp->stats.tx_bytes += lp->skbs[lp->out_idx]->len;

	dev_kfree_skb(lp->skbs[lp->out_idx++]);		/* free skb */
	if (lp->out_idx >= MAX_SKB_BUFFERS)
		lp->out_idx = 0;	/* wrap around */

	if (lp->sk_count-- == MAX_SKB_BUFFERS)	/* dec usage count */
		netif_start_queue((struct net_device *) lp);
}				/* hysdn_tx_netack */

/*****************************************************/
/* we got a packet from the network, go and queue it */
/*****************************************************/
void
hysdn_rx_netpkt(hysdn_card * card, unsigned char *buf, unsigned short len)
{
	struct net_local *lp = card->netif;
	struct sk_buff *skb;

	if (!lp)
		return;		/* non existing device */

	lp->stats.rx_bytes += len;

	skb = dev_alloc_skb(len);
	if (skb == NULL) {
		printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n",
		       lp->netdev.name);
		lp->stats.rx_dropped++;
		return;
	}
	/* copy the data */
	memcpy(skb_put(skb, len), buf, len);

	/* determine the used protocol */
	skb->protocol = eth_type_trans(skb, &lp->netdev);

	netif_rx(skb);
	lp->stats.rx_packets++;	/* adjust packet count */

}				/* hysdn_rx_netpkt */

/*****************************************************/
/* return the pointer to a network packet to be send */
/*****************************************************/
struct sk_buff *
hysdn_tx_netget(hysdn_card * card)
{
	struct net_local *lp = card->netif;

	if (!lp)
		return (NULL);	/* non existing device */

	if (!lp->sk_count)
		return (NULL);	/* nothing available */

	return (lp->skbs[lp->out_idx]);		/* next packet to send */
}				/* hysdn_tx_netget */


/*******************************************/
/* init function called by register device */
/*******************************************/
static int
net_init(struct net_device *dev)
{
	/* setup the function table */
	dev->open = net_open;
	dev->stop = net_close;
	dev->hard_start_xmit = net_send_packet;
	dev->get_stats = net_get_stats;

	/* Fill in the fields of the device structure with ethernet values. */
	ether_setup(dev);

	return (0);		/* success */
}				/* net_init */

/*****************************************************************************/
/* hysdn_net_create creates a new net device for the given card. If a device */
/* already exists, it will be deleted and created a new one. The return value */
/* 0 announces success, else a negative error code will be returned.         */
/*****************************************************************************/
int
hysdn_net_create(hysdn_card * card)
{
	struct net_device *dev;
	int i;
	if(!card) {
		printk(KERN_WARNING "No card-pt in hysdn_net_create!\n");
		return (-ENOMEM);
	}
	hysdn_net_release(card);	/* release an existing net device */
	if ((dev = kzalloc(sizeof(struct net_local), GFP_KERNEL)) == NULL) {
		printk(KERN_WARNING "HYSDN: unable to allocate mem\n");
		return (-ENOMEM);
	}

	spin_lock_init(&((struct net_local *) dev)->lock);

	/* initialise necessary or informing fields */
	dev->base_addr = card->iobase;	/* IO address */
	dev->irq = card->irq;	/* irq */
	dev->init = net_init;	/* the init function of the device */
	if(dev->name) {
		strcpy(dev->name, ((struct net_local *) dev)->dev_name);
	} 
	if ((i = register_netdev(dev))) {
		printk(KERN_WARNING "HYSDN: unable to create network device\n");
		kfree(dev);
		return (i);
	}
	dev->priv = card;	/* remember pointer to own data structure */
	card->netif = dev;	/* setup the local pointer */

	if (card->debug_flags & LOG_NET_INIT)
		hysdn_addlog(card, "network device created");
	return (0);		/* and return success */
}				/* hysdn_net_create */

/***************************************************************************/
/* hysdn_net_release deletes the net device for the given card. The return */
/* value 0 announces success, else a negative error code will be returned. */
/***************************************************************************/
int
hysdn_net_release(hysdn_card * card)
{
	struct net_device *dev = card->netif;

	if (!dev)
		return (0);	/* non existing */

	card->netif = NULL;	/* clear out pointer */
	dev->stop(dev);		/* close the device */

	flush_tx_buffers((struct net_local *) dev);	/* empty buffers */

	unregister_netdev(dev);	/* release the device */
	free_netdev(dev);	/* release the memory allocated */
	if (card->debug_flags & LOG_NET_INIT)
		hysdn_addlog(card, "network device deleted");

	return (0);		/* always successful */
}				/* hysdn_net_release */

/*****************************************************************************/
/* hysdn_net_getname returns a pointer to the name of the network interface. */
/* if the interface is not existing, a "-" is returned.                      */
/*****************************************************************************/
char *
hysdn_net_getname(hysdn_card * card)
{
	struct net_device *dev = card->netif;

	if (!dev)
		return ("-");	/* non existing */

	return (dev->name);
}				/* hysdn_net_getname */
