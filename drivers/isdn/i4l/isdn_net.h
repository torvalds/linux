/* $Id: isdn_net.h,v 1.1.2.2 2004/01/12 22:37:19 keil Exp $
 *
 * header for Linux ISDN subsystem, network related functions (linklevel).
 *
 * Copyright 1994-1999  by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

			      /* Definitions for hupflags:                */
#define ISDN_WAITCHARGE  1      /* did not get a charge info yet            */
#define ISDN_HAVECHARGE  2      /* We know a charge info                    */
#define ISDN_CHARGEHUP   4      /* We want to use the charge mechanism      */
#define ISDN_INHUP       8      /* Even if incoming, close after huptimeout */
#define ISDN_MANCHARGE  16      /* Charge Interval manually set             */

/*
 * Definitions for Cisco-HDLC header.
 */

#define CISCO_ADDR_UNICAST    0x0f
#define CISCO_ADDR_BROADCAST  0x8f
#define CISCO_CTRL            0x00
#define CISCO_TYPE_CDP        0x2000
#define CISCO_TYPE_SLARP      0x8035
#define CISCO_SLARP_REQUEST   0
#define CISCO_SLARP_REPLY     1
#define CISCO_SLARP_KEEPALIVE 2

extern char *isdn_net_new(char *, struct net_device *);
extern char *isdn_net_newslave(char *);
extern int isdn_net_rm(char *);
extern int isdn_net_rmall(void);
extern int isdn_net_stat_callback(int, isdn_ctrl *);
extern int isdn_net_setcfg(isdn_net_ioctl_cfg *);
extern int isdn_net_getcfg(isdn_net_ioctl_cfg *);
extern int isdn_net_addphone(isdn_net_ioctl_phone *);
extern int isdn_net_getphones(isdn_net_ioctl_phone *, char __user *);
extern int isdn_net_getpeer(isdn_net_ioctl_phone *, isdn_net_ioctl_phone __user *);
extern int isdn_net_delphone(isdn_net_ioctl_phone *);
extern int isdn_net_find_icall(int, int, int, setup_parm *);
extern void isdn_net_hangup(struct net_device *);
extern void isdn_net_dial(void);
extern void isdn_net_autohup(void);
extern int isdn_net_force_hangup(char *);
extern int isdn_net_force_dial(char *);
extern isdn_net_dev *isdn_net_findif(char *);
extern int isdn_net_rcv_skb(int, struct sk_buff *);
extern int isdn_net_dial_req(isdn_net_local *);
extern void isdn_net_writebuf_skb(isdn_net_local *lp, struct sk_buff *skb);
extern void isdn_net_write_super(isdn_net_local *lp, struct sk_buff *skb);

#define ISDN_NET_MAX_QUEUE_LENGTH 2

#define ISDN_MASTER_PRIV(lp) ((isdn_net_local *) netdev_priv(lp->master))
#define ISDN_SLAVE_PRIV(lp) ((isdn_net_local *) netdev_priv(lp->slave))
#define MASTER_TO_SLAVE(master)	\
			(((isdn_net_local *) netdev_priv(master))->slave)

/*
 * is this particular channel busy?
 */
static __inline__ int isdn_net_lp_busy(isdn_net_local *lp)
{
	if (atomic_read(&lp->frame_cnt) < ISDN_NET_MAX_QUEUE_LENGTH)
		return 0;
	else 
		return 1;
}

/*
 * For the given net device, this will get a non-busy channel out of the
 * corresponding bundle. The returned channel is locked.
 */
static __inline__ isdn_net_local * isdn_net_get_locked_lp(isdn_net_dev *nd)
{
	unsigned long flags;
	isdn_net_local *lp;

	spin_lock_irqsave(&nd->queue_lock, flags);
	lp = nd->queue;         /* get lp on top of queue */
	while (isdn_net_lp_busy(nd->queue)) {
		nd->queue = nd->queue->next;
		if (nd->queue == lp) { /* not found -- should never happen */
			lp = NULL;
			goto errout;
		}
	}
	lp = nd->queue;
	nd->queue = nd->queue->next;
	spin_unlock_irqrestore(&nd->queue_lock, flags);
	spin_lock(&lp->xmit_lock);
	local_bh_disable();
	return lp;
errout:
	spin_unlock_irqrestore(&nd->queue_lock, flags);
	return lp;
}

/*
 * add a channel to a bundle
 */
static __inline__ void isdn_net_add_to_bundle(isdn_net_dev *nd, isdn_net_local *nlp)
{
	isdn_net_local *lp;
	unsigned long flags;

	spin_lock_irqsave(&nd->queue_lock, flags);

	lp = nd->queue;
//	printk(KERN_DEBUG "%s: lp:%s(%p) nlp:%s(%p) last(%p)\n",
//		__func__, lp->name, lp, nlp->name, nlp, lp->last);
	nlp->last = lp->last;
	lp->last->next = nlp;
	lp->last = nlp;
	nlp->next = lp;
	nd->queue = nlp;

	spin_unlock_irqrestore(&nd->queue_lock, flags);
}
/*
 * remove a channel from the bundle it belongs to
 */
static __inline__ void isdn_net_rm_from_bundle(isdn_net_local *lp)
{
	isdn_net_local *master_lp = lp;
	unsigned long flags;

	if (lp->master)
		master_lp = ISDN_MASTER_PRIV(lp);

//	printk(KERN_DEBUG "%s: lp:%s(%p) mlp:%s(%p) last(%p) next(%p) mndq(%p)\n",
//		__func__, lp->name, lp, master_lp->name, master_lp, lp->last, lp->next, master_lp->netdev->queue);
	spin_lock_irqsave(&master_lp->netdev->queue_lock, flags);
	lp->last->next = lp->next;
	lp->next->last = lp->last;
	if (master_lp->netdev->queue == lp) {
		master_lp->netdev->queue = lp->next;
		if (lp->next == lp) { /* last in queue */
			master_lp->netdev->queue = master_lp->netdev->local;
		}
	}
	lp->next = lp->last = lp;	/* (re)set own pointers */
//	printk(KERN_DEBUG "%s: mndq(%p)\n",
//		__func__, master_lp->netdev->queue);
	spin_unlock_irqrestore(&master_lp->netdev->queue_lock, flags);
}

