/* $Id: isdn_x25iface.c,v 1.1.2.2 2004/01/12 22:37:19 keil Exp $
 *
 * Linux ISDN subsystem, X.25 related functions
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * stuff needed to support the Linux X.25 PLP code on top of devices that
 * can provide a lab_b service using the concap_proto mechanism.
 * This module supports a network interface which provides lapb_sematics
 * -- as defined in Documentation/networking/x25-iface.txt -- to
 * the upper layer and assumes that the lower layer provides a reliable
 * data link service by means of the concap_device_ops callbacks.
 *
 * Only protocol specific stuff goes here. Device specific stuff
 * goes to another -- device related -- concap_proto support source file.
 *
 */

/* #include <linux/isdn.h> */
#include <linux/netdevice.h>
#include <linux/concap.h>
#include <linux/slab.h>
#include <linux/wanrouter.h>
#include <net/x25device.h>
#include "isdn_x25iface.h"

/* for debugging messages not to cause an oops when device pointer is NULL*/
#define MY_DEVNAME(dev)  ((dev) ? (dev)->name : "DEVICE UNSPECIFIED")


typedef struct isdn_x25iface_proto_data {
	int magic;
	enum wan_states state;
	/* Private stuff, not to be accessed via proto_data. We provide the
	   other storage for the concap_proto instance here as well,
	   enabling us to allocate both with just one kmalloc(): */
	struct concap_proto priv;
} ix25_pdata_t;



/* is now in header file (extern): struct concap_proto * isdn_x25iface_proto_new(void); */
static void isdn_x25iface_proto_del(struct concap_proto *);
static int isdn_x25iface_proto_close(struct concap_proto *);
static int isdn_x25iface_proto_restart(struct concap_proto *,
				       struct net_device *,
				       struct concap_device_ops *);
static int isdn_x25iface_xmit(struct concap_proto *, struct sk_buff *);
static int isdn_x25iface_receive(struct concap_proto *, struct sk_buff *);
static int isdn_x25iface_connect_ind(struct concap_proto *);
static int isdn_x25iface_disconn_ind(struct concap_proto *);


static struct concap_proto_ops ix25_pops = {
	&isdn_x25iface_proto_new,
	&isdn_x25iface_proto_del,
	&isdn_x25iface_proto_restart,
	&isdn_x25iface_proto_close,
	&isdn_x25iface_xmit,
	&isdn_x25iface_receive,
	&isdn_x25iface_connect_ind,
	&isdn_x25iface_disconn_ind
};

/* error message helper function */
static void illegal_state_warn(unsigned state, unsigned char firstbyte)
{
	printk(KERN_WARNING "isdn_x25iface: firstbyte %x illegal in"
	       "current state %d\n", firstbyte, state);
}

/* check protocol data field for consistency */
static int pdata_is_bad(ix25_pdata_t *pda) {

	if (pda  &&  pda->magic == ISDN_X25IFACE_MAGIC) return 0;
	printk(KERN_WARNING
	       "isdn_x25iface_xxx: illegal pointer to proto data\n");
	return 1;
}

/* create a new x25 interface protocol instance
 */
struct concap_proto *isdn_x25iface_proto_new(void)
{
	ix25_pdata_t *tmp = kmalloc(sizeof(ix25_pdata_t), GFP_KERNEL);
	IX25DEBUG("isdn_x25iface_proto_new\n");
	if (tmp) {
		tmp->magic = ISDN_X25IFACE_MAGIC;
		tmp->state = WAN_UNCONFIGURED;
		/* private data space used to hold the concap_proto data.
		   Only to be accessed via the returned pointer */
		spin_lock_init(&tmp->priv.lock);
		tmp->priv.dops       = NULL;
		tmp->priv.net_dev    = NULL;
		tmp->priv.pops       = &ix25_pops;
		tmp->priv.flags      = 0;
		tmp->priv.proto_data = tmp;
		return (&(tmp->priv));
	}
	return NULL;
};

/* close the x25iface encapsulation protocol
 */
static int isdn_x25iface_proto_close(struct concap_proto *cprot) {

	ix25_pdata_t *tmp;
	int ret = 0;
	ulong flags;

	if (!cprot) {
		printk(KERN_ERR "isdn_x25iface_proto_close: "
		       "invalid concap_proto pointer\n");
		return -1;
	}
	IX25DEBUG("isdn_x25iface_proto_close %s \n", MY_DEVNAME(cprot->net_dev));
	spin_lock_irqsave(&cprot->lock, flags);
	cprot->dops    = NULL;
	cprot->net_dev = NULL;
	tmp = cprot->proto_data;
	if (pdata_is_bad(tmp)) {
		ret = -1;
	} else {
		tmp->state = WAN_UNCONFIGURED;
	}
	spin_unlock_irqrestore(&cprot->lock, flags);
	return ret;
}

/* Delete the x25iface encapsulation protocol instance
 */
static void isdn_x25iface_proto_del(struct concap_proto *cprot) {

	ix25_pdata_t *tmp;

	IX25DEBUG("isdn_x25iface_proto_del \n");
	if (!cprot) {
		printk(KERN_ERR "isdn_x25iface_proto_del: "
		       "concap_proto pointer is NULL\n");
		return;
	}
	tmp = cprot->proto_data;
	if (tmp == NULL) {
		printk(KERN_ERR "isdn_x25iface_proto_del: inconsistent "
		       "proto_data pointer (maybe already deleted?)\n");
		return;
	}
	/* close if the protocol is still open */
	if (cprot->dops) isdn_x25iface_proto_close(cprot);
	/* freeing the storage should be sufficient now. But some additional
	   settings might help to catch wild pointer bugs */
	tmp->magic = 0;
	cprot->proto_data = NULL;

	kfree(tmp);
	return;
}

/* (re-)initialize the data structures for x25iface encapsulation
 */
static int isdn_x25iface_proto_restart(struct concap_proto *cprot,
				       struct net_device *ndev,
				       struct concap_device_ops *dops)
{
	ix25_pdata_t *pda = cprot->proto_data;
	ulong flags;

	IX25DEBUG("isdn_x25iface_proto_restart %s \n", MY_DEVNAME(ndev));

	if (pdata_is_bad(pda)) return -1;

	if (!(dops && dops->data_req && dops->connect_req
	      && dops->disconn_req)) {
		printk(KERN_WARNING "isdn_x25iface_restart: required dops"
		       " missing\n");
		isdn_x25iface_proto_close(cprot);
		return -1;
	}
	spin_lock_irqsave(&cprot->lock, flags);
	cprot->net_dev = ndev;
	cprot->pops = &ix25_pops;
	cprot->dops = dops;
	pda->state = WAN_DISCONNECTED;
	spin_unlock_irqrestore(&cprot->lock, flags);
	return 0;
}

/* deliver a dl_data frame received from i4l HL driver to the network layer
 */
static int isdn_x25iface_receive(struct concap_proto *cprot, struct sk_buff *skb)
{
	IX25DEBUG("isdn_x25iface_receive %s \n", MY_DEVNAME(cprot->net_dev));
	if (((ix25_pdata_t *)(cprot->proto_data))
	    ->state == WAN_CONNECTED) {
		if (skb_push(skb, 1)) {
			skb->data[0] = X25_IFACE_DATA;
			skb->protocol = x25_type_trans(skb, cprot->net_dev);
			netif_rx(skb);
			return 0;
		}
	}
	printk(KERN_WARNING "isdn_x25iface_receive %s: not connected, skb dropped\n", MY_DEVNAME(cprot->net_dev));
	dev_kfree_skb(skb);
	return -1;
}

/* a connection set up is indicated by lower layer
 */
static int isdn_x25iface_connect_ind(struct concap_proto *cprot)
{
	struct sk_buff *skb;
	enum wan_states *state_p
		= &(((ix25_pdata_t *)(cprot->proto_data))->state);
	IX25DEBUG("isdn_x25iface_connect_ind %s \n"
		  , MY_DEVNAME(cprot->net_dev));
	if (*state_p == WAN_UNCONFIGURED) {
		printk(KERN_WARNING
		       "isdn_x25iface_connect_ind while unconfigured %s\n"
		       , MY_DEVNAME(cprot->net_dev));
		return -1;
	}
	*state_p = WAN_CONNECTED;

	skb = dev_alloc_skb(1);
	if (skb) {
		*(skb_put(skb, 1)) = X25_IFACE_CONNECT;
		skb->protocol = x25_type_trans(skb, cprot->net_dev);
		netif_rx(skb);
		return 0;
	} else {
		printk(KERN_WARNING "isdn_x25iface_connect_ind: "
		       " out of memory -- disconnecting\n");
		cprot->dops->disconn_req(cprot);
		return -1;
	}
}

/* a disconnect is indicated by lower layer
 */
static int isdn_x25iface_disconn_ind(struct concap_proto *cprot)
{
	struct sk_buff *skb;
	enum wan_states *state_p
		= &(((ix25_pdata_t *)(cprot->proto_data))->state);
	IX25DEBUG("isdn_x25iface_disconn_ind %s \n", MY_DEVNAME(cprot->net_dev));
	if (*state_p == WAN_UNCONFIGURED) {
		printk(KERN_WARNING
		       "isdn_x25iface_disconn_ind while unconfigured\n");
		return -1;
	}
	if (!cprot->net_dev) return -1;
	*state_p = WAN_DISCONNECTED;
	skb = dev_alloc_skb(1);
	if (skb) {
		*(skb_put(skb, 1)) = X25_IFACE_DISCONNECT;
		skb->protocol = x25_type_trans(skb, cprot->net_dev);
		netif_rx(skb);
		return 0;
	} else {
		printk(KERN_WARNING "isdn_x25iface_disconn_ind:"
		       " out of memory\n");
		return -1;
	}
}

/* process a frame handed over to us from linux network layer. First byte
   semantics as defined in Documentation/networking/x25-iface.txt
*/
static int isdn_x25iface_xmit(struct concap_proto *cprot, struct sk_buff *skb)
{
	unsigned char firstbyte = skb->data[0];
	enum wan_states *state = &((ix25_pdata_t *)cprot->proto_data)->state;
	int ret = 0;
	IX25DEBUG("isdn_x25iface_xmit: %s first=%x state=%d\n",
		  MY_DEVNAME(cprot->net_dev), firstbyte, *state);
	switch (firstbyte) {
	case X25_IFACE_DATA:
		if (*state == WAN_CONNECTED) {
			skb_pull(skb, 1);
			netif_trans_update(cprot->net_dev);
			ret = (cprot->dops->data_req(cprot, skb));
			/* prepare for future retransmissions */
			if (ret) skb_push(skb, 1);
			return ret;
		}
		illegal_state_warn(*state, firstbyte);
		break;
	case X25_IFACE_CONNECT:
		if (*state == WAN_DISCONNECTED) {
			*state = WAN_CONNECTING;
			ret = cprot->dops->connect_req(cprot);
			if (ret) {
				/* reset state and notify upper layer about
				 * immidiatly failed attempts */
				isdn_x25iface_disconn_ind(cprot);
			}
		} else {
			illegal_state_warn(*state, firstbyte);
		}
		break;
	case X25_IFACE_DISCONNECT:
		switch (*state) {
		case WAN_DISCONNECTED:
			/* Should not happen. However, give upper layer a
			   chance to recover from inconstistency  but don't
			   trust the lower layer sending the disconn_confirm
			   when already disconnected */
			printk(KERN_WARNING "isdn_x25iface_xmit: disconnect "
			       " requested while disconnected\n");
			isdn_x25iface_disconn_ind(cprot);
			break; /* prevent infinite loops */
		case WAN_CONNECTING:
		case WAN_CONNECTED:
			*state = WAN_DISCONNECTED;
			cprot->dops->disconn_req(cprot);
			break;
		default:
			illegal_state_warn(*state, firstbyte);
		}
		break;
	case X25_IFACE_PARAMS:
		printk(KERN_WARNING "isdn_x25iface_xmit: setting of lapb"
		       " options not yet supported\n");
		break;
	default:
		printk(KERN_WARNING "isdn_x25iface_xmit: frame with illegal"
		       " first byte %x ignored:\n", firstbyte);
	}
	dev_kfree_skb(skb);
	return 0;
}
