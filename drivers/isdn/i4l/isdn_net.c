/* $Id: isdn_net.c,v 1.1.2.2 2004/01/12 22:37:19 keil Exp $
 *
 * Linux ISDN subsystem, network interfaces and related functions (linklevel).
 *
 * Copyright 1994-1998  by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Data Over Voice (DOV) support added - Guy Ellis 23-Mar-02
 *                                       guy@traverse.com.au
 * Outgoing calls - looks for a 'V' in first char of dialed number
 * Incoming calls - checks first character of eaz as follows:
 *   Numeric - accept DATA only - original functionality
 *   'V'     - accept VOICE (DOV) only
 *   'B'     - accept BOTH DATA and DOV types
 *
 * Jan 2001: fix CISCO HDLC      Bjoern A. Zeeb <i4l@zabbadoz.net>
 *           for info on the protocol, see
 *           http://i4l.zabbadoz.net/i4l/cisco-hdlc.txt
 */

#include <linux/isdn.h>
#include <linux/slab.h>
#include <net/arp.h>
#include <net/dst.h>
#include <net/pkt_sched.h>
#include <linux/inetdevice.h>
#include "isdn_common.h"
#include "isdn_net.h"
#ifdef CONFIG_ISDN_PPP
#include "isdn_ppp.h"
#endif
#ifdef CONFIG_ISDN_X25
#include <linux/concap.h>
#include "isdn_concap.h"
#endif


/*
 * Outline of new tbusy handling:
 *
 * Old method, roughly spoken, consisted of setting tbusy when entering
 * isdn_net_start_xmit() and at several other locations and clearing
 * it from isdn_net_start_xmit() thread when sending was successful.
 *
 * With 2.3.x multithreaded network core, to prevent problems, tbusy should
 * only be set by the isdn_net_start_xmit() thread and only when a tx-busy
 * condition is detected. Other threads (in particular isdn_net_stat_callb())
 * are only allowed to clear tbusy.
 *
 * -HE
 */

/*
 * About SOFTNET:
 * Most of the changes were pretty obvious and basically done by HE already.
 *
 * One problem of the isdn net device code is that it uses struct net_device
 * for masters and slaves. However, only master interface are registered to
 * the network layer, and therefore, it only makes sense to call netif_*
 * functions on them.
 *
 * --KG
 */

/*
 * Find out if the netdevice has been ifup-ed yet.
 * For slaves, look at the corresponding master.
 */
static __inline__ int isdn_net_device_started(isdn_net_dev *n)
{
	isdn_net_local *lp = n->local;
	struct net_device *dev;

	if (lp->master)
		dev = lp->master;
	else
		dev = n->dev;
	return netif_running(dev);
}

/*
 * wake up the network -> net_device queue.
 * For slaves, wake the corresponding master interface.
 */
static __inline__ void isdn_net_device_wake_queue(isdn_net_local *lp)
{
	if (lp->master)
		netif_wake_queue(lp->master);
	else
		netif_wake_queue(lp->netdev->dev);
}

/*
 * stop the network -> net_device queue.
 * For slaves, stop the corresponding master interface.
 */
static __inline__ void isdn_net_device_stop_queue(isdn_net_local *lp)
{
	if (lp->master)
		netif_stop_queue(lp->master);
	else
		netif_stop_queue(lp->netdev->dev);
}

/*
 * find out if the net_device which this lp belongs to (lp can be
 * master or slave) is busy. It's busy iff all (master and slave)
 * queues are busy
 */
static __inline__ int isdn_net_device_busy(isdn_net_local *lp)
{
	isdn_net_local *nlp;
	isdn_net_dev *nd;
	unsigned long flags;

	if (!isdn_net_lp_busy(lp))
		return 0;

	if (lp->master)
		nd = ISDN_MASTER_PRIV(lp)->netdev;
	else
		nd = lp->netdev;

	spin_lock_irqsave(&nd->queue_lock, flags);
	nlp = lp->next;
	while (nlp != lp) {
		if (!isdn_net_lp_busy(nlp)) {
			spin_unlock_irqrestore(&nd->queue_lock, flags);
			return 0;
		}
		nlp = nlp->next;
	}
	spin_unlock_irqrestore(&nd->queue_lock, flags);
	return 1;
}

static __inline__ void isdn_net_inc_frame_cnt(isdn_net_local *lp)
{
	atomic_inc(&lp->frame_cnt);
	if (isdn_net_device_busy(lp))
		isdn_net_device_stop_queue(lp);
}

static __inline__ void isdn_net_dec_frame_cnt(isdn_net_local *lp)
{
	atomic_dec(&lp->frame_cnt);

	if (!(isdn_net_device_busy(lp))) {
		if (!skb_queue_empty(&lp->super_tx_queue)) {
			schedule_work(&lp->tqueue);
		} else {
			isdn_net_device_wake_queue(lp);
		}
	}
}

static __inline__ void isdn_net_zero_frame_cnt(isdn_net_local *lp)
{
	atomic_set(&lp->frame_cnt, 0);
}

/* For 2.2.x we leave the transmitter busy timeout at 2 secs, just
 * to be safe.
 * For 2.3.x we push it up to 20 secs, because call establishment
 * (in particular callback) may take such a long time, and we
 * don't want confusing messages in the log. However, there is a slight
 * possibility that this large timeout will break other things like MPPP,
 * which might rely on the tx timeout. If so, we'll find out this way...
 */

#define ISDN_NET_TX_TIMEOUT (20 * HZ)

/* Prototypes */

static int isdn_net_force_dial_lp(isdn_net_local *);
static netdev_tx_t isdn_net_start_xmit(struct sk_buff *,
				       struct net_device *);

static void isdn_net_ciscohdlck_connected(isdn_net_local *lp);
static void isdn_net_ciscohdlck_disconnected(isdn_net_local *lp);

char *isdn_net_revision = "$Revision: 1.1.2.2 $";

/*
 * Code for raw-networking over ISDN
 */

static void
isdn_net_unreachable(struct net_device *dev, struct sk_buff *skb, char *reason)
{
	if (skb) {

		u_short proto = ntohs(skb->protocol);

		printk(KERN_DEBUG "isdn_net: %s: %s, signalling dst_link_failure %s\n",
		       dev->name,
		       (reason != NULL) ? reason : "unknown",
		       (proto != ETH_P_IP) ? "Protocol != ETH_P_IP" : "");

		dst_link_failure(skb);
	}
	else {  /* dial not triggered by rawIP packet */
		printk(KERN_DEBUG "isdn_net: %s: %s\n",
		       dev->name,
		       (reason != NULL) ? reason : "reason unknown");
	}
}

static void
isdn_net_reset(struct net_device *dev)
{
#ifdef CONFIG_ISDN_X25
	struct concap_device_ops *dops =
		((isdn_net_local *)netdev_priv(dev))->dops;
	struct concap_proto *cprot =
		((isdn_net_local *)netdev_priv(dev))->netdev->cprot;
#endif
#ifdef CONFIG_ISDN_X25
	if (cprot && cprot->pops && dops)
		cprot->pops->restart(cprot, dev, dops);
#endif
}

/* Open/initialize the board. */
static int
isdn_net_open(struct net_device *dev)
{
	int i;
	struct net_device *p;
	struct in_device *in_dev;

	/* moved here from isdn_net_reset, because only the master has an
	   interface associated which is supposed to be started. BTW:
	   we need to call netif_start_queue, not netif_wake_queue here */
	netif_start_queue(dev);

	isdn_net_reset(dev);
	/* Fill in the MAC-level header (not needed, but for compatibility... */
	for (i = 0; i < ETH_ALEN - sizeof(u32); i++)
		dev->dev_addr[i] = 0xfc;
	if ((in_dev = dev->ip_ptr) != NULL) {
		/*
		 *      Any address will do - we take the first
		 */
		struct in_ifaddr *ifa = in_dev->ifa_list;
		if (ifa != NULL)
			memcpy(dev->dev_addr + 2, &ifa->ifa_local, 4);
	}

	/* If this interface has slaves, start them also */
	p = MASTER_TO_SLAVE(dev);
	if (p) {
		while (p) {
			isdn_net_reset(p);
			p = MASTER_TO_SLAVE(p);
		}
	}
	isdn_lock_drivers();
	return 0;
}

/*
 * Assign an ISDN-channel to a net-interface
 */
static void
isdn_net_bind_channel(isdn_net_local *lp, int idx)
{
	lp->flags |= ISDN_NET_CONNECTED;
	lp->isdn_device = dev->drvmap[idx];
	lp->isdn_channel = dev->chanmap[idx];
	dev->rx_netdev[idx] = lp->netdev;
	dev->st_netdev[idx] = lp->netdev;
}

/*
 * unbind a net-interface (resets interface after an error)
 */
static void
isdn_net_unbind_channel(isdn_net_local *lp)
{
	skb_queue_purge(&lp->super_tx_queue);

	if (!lp->master) {	/* reset only master device */
		/* Moral equivalent of dev_purge_queues():
		   BEWARE! This chunk of code cannot be called from hardware
		   interrupt handler. I hope it is true. --ANK
		*/
		qdisc_reset_all_tx(lp->netdev->dev);
	}
	lp->dialstate = 0;
	dev->rx_netdev[isdn_dc2minor(lp->isdn_device, lp->isdn_channel)] = NULL;
	dev->st_netdev[isdn_dc2minor(lp->isdn_device, lp->isdn_channel)] = NULL;
	if (lp->isdn_device != -1 && lp->isdn_channel != -1)
		isdn_free_channel(lp->isdn_device, lp->isdn_channel,
				  ISDN_USAGE_NET);
	lp->flags &= ~ISDN_NET_CONNECTED;
	lp->isdn_device = -1;
	lp->isdn_channel = -1;
}

/*
 * Perform auto-hangup and cps-calculation for net-interfaces.
 *
 * auto-hangup:
 * Increment idle-counter (this counter is reset on any incoming or
 * outgoing packet), if counter exceeds configured limit either do a
 * hangup immediately or - if configured - wait until just before the next
 * charge-info.
 *
 * cps-calculation (needed for dynamic channel-bundling):
 * Since this function is called every second, simply reset the
 * byte-counter of the interface after copying it to the cps-variable.
 */
static unsigned long last_jiffies = -HZ;

void
isdn_net_autohup(void)
{
	isdn_net_dev *p = dev->netdev;
	int anymore;

	anymore = 0;
	while (p) {
		isdn_net_local *l = p->local;
		if (jiffies == last_jiffies)
			l->cps = l->transcount;
		else
			l->cps = (l->transcount * HZ) / (jiffies - last_jiffies);
		l->transcount = 0;
		if (dev->net_verbose > 3)
			printk(KERN_DEBUG "%s: %d bogocps\n", p->dev->name, l->cps);
		if ((l->flags & ISDN_NET_CONNECTED) && (!l->dialstate)) {
			anymore = 1;
			l->huptimer++;
			/*
			 * if there is some dialmode where timeout-hangup
			 * should _not_ be done, check for that here
			 */
			if ((l->onhtime) &&
			    (l->huptimer > l->onhtime))
			{
				if (l->hupflags & ISDN_MANCHARGE &&
				    l->hupflags & ISDN_CHARGEHUP) {
					while (time_after(jiffies, l->chargetime + l->chargeint))
						l->chargetime += l->chargeint;
					if (time_after(jiffies, l->chargetime + l->chargeint - 2 * HZ))
						if (l->outgoing || l->hupflags & ISDN_INHUP)
							isdn_net_hangup(p->dev);
				} else if (l->outgoing) {
					if (l->hupflags & ISDN_CHARGEHUP) {
						if (l->hupflags & ISDN_WAITCHARGE) {
							printk(KERN_DEBUG "isdn_net: Hupflags of %s are %X\n",
							       p->dev->name, l->hupflags);
							isdn_net_hangup(p->dev);
						} else if (time_after(jiffies, l->chargetime + l->chargeint)) {
							printk(KERN_DEBUG
							       "isdn_net: %s: chtime = %lu, chint = %d\n",
							       p->dev->name, l->chargetime, l->chargeint);
							isdn_net_hangup(p->dev);
						}
					} else
						isdn_net_hangup(p->dev);
				} else if (l->hupflags & ISDN_INHUP)
					isdn_net_hangup(p->dev);
			}

			if (dev->global_flags & ISDN_GLOBAL_STOPPED || (ISDN_NET_DIALMODE(*l) == ISDN_NET_DM_OFF)) {
				isdn_net_hangup(p->dev);
				break;
			}
		}
		p = (isdn_net_dev *) p->next;
	}
	last_jiffies = jiffies;
	isdn_timer_ctrl(ISDN_TIMER_NETHANGUP, anymore);
}

static void isdn_net_lp_disconnected(isdn_net_local *lp)
{
	isdn_net_rm_from_bundle(lp);
}

/*
 * Handle status-messages from ISDN-interfacecard.
 * This function is called from within the main-status-dispatcher
 * isdn_status_callback, which itself is called from the low-level driver.
 * Return: 1 = Event handled, 0 = not for us or unknown Event.
 */
int
isdn_net_stat_callback(int idx, isdn_ctrl *c)
{
	isdn_net_dev *p = dev->st_netdev[idx];
	int cmd = c->command;

	if (p) {
		isdn_net_local *lp = p->local;
#ifdef CONFIG_ISDN_X25
		struct concap_proto *cprot = lp->netdev->cprot;
		struct concap_proto_ops *pops = cprot ? cprot->pops : NULL;
#endif
		switch (cmd) {
		case ISDN_STAT_BSENT:
			/* A packet has successfully been sent out */
			if ((lp->flags & ISDN_NET_CONNECTED) &&
			    (!lp->dialstate)) {
				isdn_net_dec_frame_cnt(lp);
				lp->stats.tx_packets++;
				lp->stats.tx_bytes += c->parm.length;
			}
			return 1;
		case ISDN_STAT_DCONN:
			/* D-Channel is up */
			switch (lp->dialstate) {
			case 4:
			case 7:
			case 8:
				lp->dialstate++;
				return 1;
			case 12:
				lp->dialstate = 5;
				return 1;
			}
			break;
		case ISDN_STAT_DHUP:
			/* Either D-Channel-hangup or error during dialout */
#ifdef CONFIG_ISDN_X25
			/* If we are not connencted then dialing had
			   failed. If there are generic encap protocol
			   receiver routines signal the closure of
			   the link*/

			if (!(lp->flags & ISDN_NET_CONNECTED)
			    && pops && pops->disconn_ind)
				pops->disconn_ind(cprot);
#endif /* CONFIG_ISDN_X25 */
			if ((!lp->dialstate) && (lp->flags & ISDN_NET_CONNECTED)) {
				if (lp->p_encap == ISDN_NET_ENCAP_CISCOHDLCK)
					isdn_net_ciscohdlck_disconnected(lp);
#ifdef CONFIG_ISDN_PPP
				if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
					isdn_ppp_free(lp);
#endif
				isdn_net_lp_disconnected(lp);
				isdn_all_eaz(lp->isdn_device, lp->isdn_channel);
				printk(KERN_INFO "%s: remote hangup\n", p->dev->name);
				printk(KERN_INFO "%s: Chargesum is %d\n", p->dev->name,
				       lp->charge);
				isdn_net_unbind_channel(lp);
				return 1;
			}
			break;
#ifdef CONFIG_ISDN_X25
		case ISDN_STAT_BHUP:
			/* B-Channel-hangup */
			/* try if there are generic encap protocol
			   receiver routines and signal the closure of
			   the link */
			if (pops && pops->disconn_ind) {
				pops->disconn_ind(cprot);
				return 1;
			}
			break;
#endif /* CONFIG_ISDN_X25 */
		case ISDN_STAT_BCONN:
			/* B-Channel is up */
			isdn_net_zero_frame_cnt(lp);
			switch (lp->dialstate) {
			case 5:
			case 6:
			case 7:
			case 8:
			case 9:
			case 10:
			case 12:
				if (lp->dialstate <= 6) {
					dev->usage[idx] |= ISDN_USAGE_OUTGOING;
					isdn_info_update();
				} else
					dev->rx_netdev[idx] = p;
				lp->dialstate = 0;
				isdn_timer_ctrl(ISDN_TIMER_NETHANGUP, 1);
				if (lp->p_encap == ISDN_NET_ENCAP_CISCOHDLCK)
					isdn_net_ciscohdlck_connected(lp);
				if (lp->p_encap != ISDN_NET_ENCAP_SYNCPPP) {
					if (lp->master) { /* is lp a slave? */
						isdn_net_dev *nd = ISDN_MASTER_PRIV(lp)->netdev;
						isdn_net_add_to_bundle(nd, lp);
					}
				}
				printk(KERN_INFO "isdn_net: %s connected\n", p->dev->name);
				/* If first Chargeinfo comes before B-Channel connect,
				 * we correct the timestamp here.
				 */
				lp->chargetime = jiffies;

				/* reset dial-timeout */
				lp->dialstarted = 0;
				lp->dialwait_timer = 0;

#ifdef CONFIG_ISDN_PPP
				if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
					isdn_ppp_wakeup_daemon(lp);
#endif
#ifdef CONFIG_ISDN_X25
				/* try if there are generic concap receiver routines */
				if (pops)
					if (pops->connect_ind)
						pops->connect_ind(cprot);
#endif /* CONFIG_ISDN_X25 */
				/* ppp needs to do negotiations first */
				if (lp->p_encap != ISDN_NET_ENCAP_SYNCPPP)
					isdn_net_device_wake_queue(lp);
				return 1;
			}
			break;
		case ISDN_STAT_NODCH:
			/* No D-Channel avail. */
			if (lp->dialstate == 4) {
				lp->dialstate--;
				return 1;
			}
			break;
		case ISDN_STAT_CINF:
			/* Charge-info from TelCo. Calculate interval between
			 * charge-infos and set timestamp for last info for
			 * usage by isdn_net_autohup()
			 */
			lp->charge++;
			if (lp->hupflags & ISDN_HAVECHARGE) {
				lp->hupflags &= ~ISDN_WAITCHARGE;
				lp->chargeint = jiffies - lp->chargetime - (2 * HZ);
			}
			if (lp->hupflags & ISDN_WAITCHARGE)
				lp->hupflags |= ISDN_HAVECHARGE;
			lp->chargetime = jiffies;
			printk(KERN_DEBUG "isdn_net: Got CINF chargetime of %s now %lu\n",
			       p->dev->name, lp->chargetime);
			return 1;
		}
	}
	return 0;
}

/*
 * Perform dialout for net-interfaces and timeout-handling for
 * D-Channel-up and B-Channel-up Messages.
 * This function is initially called from within isdn_net_start_xmit() or
 * or isdn_net_find_icall() after initializing the dialstate for an
 * interface. If further calls are needed, the function schedules itself
 * for a timer-callback via isdn_timer_function().
 * The dialstate is also affected by incoming status-messages from
 * the ISDN-Channel which are handled in isdn_net_stat_callback() above.
 */
void
isdn_net_dial(void)
{
	isdn_net_dev *p = dev->netdev;
	int anymore = 0;
	int i;
	isdn_ctrl cmd;
	u_char *phone_number;

	while (p) {
		isdn_net_local *lp = p->local;

#ifdef ISDN_DEBUG_NET_DIAL
		if (lp->dialstate)
			printk(KERN_DEBUG "%s: dialstate=%d\n", p->dev->name, lp->dialstate);
#endif
		switch (lp->dialstate) {
		case 0:
			/* Nothing to do for this interface */
			break;
		case 1:
			/* Initiate dialout. Set phone-number-pointer to first number
			 * of interface.
			 */
			lp->dial = lp->phone[1];
			if (!lp->dial) {
				printk(KERN_WARNING "%s: phone number deleted?\n",
				       p->dev->name);
				isdn_net_hangup(p->dev);
				break;
			}
			anymore = 1;

			if (lp->dialtimeout > 0)
				if (lp->dialstarted == 0 || time_after(jiffies, lp->dialstarted + lp->dialtimeout + lp->dialwait)) {
					lp->dialstarted = jiffies;
					lp->dialwait_timer = 0;
				}

			lp->dialstate++;
			/* Fall through */
		case 2:
			/* Prepare dialing. Clear EAZ, then set EAZ. */
			cmd.driver = lp->isdn_device;
			cmd.arg = lp->isdn_channel;
			cmd.command = ISDN_CMD_CLREAZ;
			isdn_command(&cmd);
			sprintf(cmd.parm.num, "%s", isdn_map_eaz2msn(lp->msn, cmd.driver));
			cmd.command = ISDN_CMD_SETEAZ;
			isdn_command(&cmd);
			lp->dialretry = 0;
			anymore = 1;
			lp->dialstate++;
			/* Fall through */
		case 3:
			/* Setup interface, dial current phone-number, switch to next number.
			 * If list of phone-numbers is exhausted, increment
			 * retry-counter.
			 */
			if (dev->global_flags & ISDN_GLOBAL_STOPPED || (ISDN_NET_DIALMODE(*lp) == ISDN_NET_DM_OFF)) {
				char *s;
				if (dev->global_flags & ISDN_GLOBAL_STOPPED)
					s = "dial suppressed: isdn system stopped";
				else
					s = "dial suppressed: dialmode `off'";
				isdn_net_unreachable(p->dev, NULL, s);
				isdn_net_hangup(p->dev);
				break;
			}
			cmd.driver = lp->isdn_device;
			cmd.command = ISDN_CMD_SETL2;
			cmd.arg = lp->isdn_channel + (lp->l2_proto << 8);
			isdn_command(&cmd);
			cmd.driver = lp->isdn_device;
			cmd.command = ISDN_CMD_SETL3;
			cmd.arg = lp->isdn_channel + (lp->l3_proto << 8);
			isdn_command(&cmd);
			cmd.driver = lp->isdn_device;
			cmd.arg = lp->isdn_channel;
			if (!lp->dial) {
				printk(KERN_WARNING "%s: phone number deleted?\n",
				       p->dev->name);
				isdn_net_hangup(p->dev);
				break;
			}
			if (!strncmp(lp->dial->num, "LEASED", strlen("LEASED"))) {
				lp->dialstate = 4;
				printk(KERN_INFO "%s: Open leased line ...\n", p->dev->name);
			} else {
				if (lp->dialtimeout > 0)
					if (time_after(jiffies, lp->dialstarted + lp->dialtimeout)) {
						lp->dialwait_timer = jiffies + lp->dialwait;
						lp->dialstarted = 0;
						isdn_net_unreachable(p->dev, NULL, "dial: timed out");
						isdn_net_hangup(p->dev);
						break;
					}

				cmd.driver = lp->isdn_device;
				cmd.command = ISDN_CMD_DIAL;
				cmd.parm.setup.si2 = 0;

				/* check for DOV */
				phone_number = lp->dial->num;
				if ((*phone_number == 'v') ||
				    (*phone_number == 'V')) { /* DOV call */
					cmd.parm.setup.si1 = 1;
				} else { /* DATA call */
					cmd.parm.setup.si1 = 7;
				}

				strcpy(cmd.parm.setup.phone, phone_number);
				/*
				 * Switch to next number or back to start if at end of list.
				 */
				if (!(lp->dial = (isdn_net_phone *) lp->dial->next)) {
					lp->dial = lp->phone[1];
					lp->dialretry++;

					if (lp->dialretry > lp->dialmax) {
						if (lp->dialtimeout == 0) {
							lp->dialwait_timer = jiffies + lp->dialwait;
							lp->dialstarted = 0;
							isdn_net_unreachable(p->dev, NULL, "dial: tried all numbers dialmax times");
						}
						isdn_net_hangup(p->dev);
						break;
					}
				}
				sprintf(cmd.parm.setup.eazmsn, "%s",
					isdn_map_eaz2msn(lp->msn, cmd.driver));
				i = isdn_dc2minor(lp->isdn_device, lp->isdn_channel);
				if (i >= 0) {
					strcpy(dev->num[i], cmd.parm.setup.phone);
					dev->usage[i] |= ISDN_USAGE_OUTGOING;
					isdn_info_update();
				}
				printk(KERN_INFO "%s: dialing %d %s... %s\n", p->dev->name,
				       lp->dialretry, cmd.parm.setup.phone,
				       (cmd.parm.setup.si1 == 1) ? "DOV" : "");
				lp->dtimer = 0;
#ifdef ISDN_DEBUG_NET_DIAL
				printk(KERN_DEBUG "dial: d=%d c=%d\n", lp->isdn_device,
				       lp->isdn_channel);
#endif
				isdn_command(&cmd);
			}
			lp->huptimer = 0;
			lp->outgoing = 1;
			if (lp->chargeint) {
				lp->hupflags |= ISDN_HAVECHARGE;
				lp->hupflags &= ~ISDN_WAITCHARGE;
			} else {
				lp->hupflags |= ISDN_WAITCHARGE;
				lp->hupflags &= ~ISDN_HAVECHARGE;
			}
			anymore = 1;
			lp->dialstate =
				(lp->cbdelay &&
				 (lp->flags & ISDN_NET_CBOUT)) ? 12 : 4;
			break;
		case 4:
			/* Wait for D-Channel-connect.
			 * If timeout, switch back to state 3.
			 * Dialmax-handling moved to state 3.
			 */
			if (lp->dtimer++ > ISDN_TIMER_DTIMEOUT10)
				lp->dialstate = 3;
			anymore = 1;
			break;
		case 5:
			/* Got D-Channel-Connect, send B-Channel-request */
			cmd.driver = lp->isdn_device;
			cmd.arg = lp->isdn_channel;
			cmd.command = ISDN_CMD_ACCEPTB;
			anymore = 1;
			lp->dtimer = 0;
			lp->dialstate++;
			isdn_command(&cmd);
			break;
		case 6:
			/* Wait for B- or D-Channel-connect. If timeout,
			 * switch back to state 3.
			 */
#ifdef ISDN_DEBUG_NET_DIAL
			printk(KERN_DEBUG "dialtimer2: %d\n", lp->dtimer);
#endif
			if (lp->dtimer++ > ISDN_TIMER_DTIMEOUT10)
				lp->dialstate = 3;
			anymore = 1;
			break;
		case 7:
			/* Got incoming Call, setup L2 and L3 protocols,
			 * then wait for D-Channel-connect
			 */
#ifdef ISDN_DEBUG_NET_DIAL
			printk(KERN_DEBUG "dialtimer4: %d\n", lp->dtimer);
#endif
			cmd.driver = lp->isdn_device;
			cmd.command = ISDN_CMD_SETL2;
			cmd.arg = lp->isdn_channel + (lp->l2_proto << 8);
			isdn_command(&cmd);
			cmd.driver = lp->isdn_device;
			cmd.command = ISDN_CMD_SETL3;
			cmd.arg = lp->isdn_channel + (lp->l3_proto << 8);
			isdn_command(&cmd);
			if (lp->dtimer++ > ISDN_TIMER_DTIMEOUT15)
				isdn_net_hangup(p->dev);
			else {
				anymore = 1;
				lp->dialstate++;
			}
			break;
		case 9:
			/* Got incoming D-Channel-Connect, send B-Channel-request */
			cmd.driver = lp->isdn_device;
			cmd.arg = lp->isdn_channel;
			cmd.command = ISDN_CMD_ACCEPTB;
			isdn_command(&cmd);
			anymore = 1;
			lp->dtimer = 0;
			lp->dialstate++;
			break;
		case 8:
		case 10:
			/*  Wait for B- or D-channel-connect */
#ifdef ISDN_DEBUG_NET_DIAL
			printk(KERN_DEBUG "dialtimer4: %d\n", lp->dtimer);
#endif
			if (lp->dtimer++ > ISDN_TIMER_DTIMEOUT10)
				isdn_net_hangup(p->dev);
			else
				anymore = 1;
			break;
		case 11:
			/* Callback Delay */
			if (lp->dtimer++ > lp->cbdelay)
				lp->dialstate = 1;
			anymore = 1;
			break;
		case 12:
			/* Remote does callback. Hangup after cbdelay, then wait for incoming
			 * call (in state 4).
			 */
			if (lp->dtimer++ > lp->cbdelay)
			{
				printk(KERN_INFO "%s: hangup waiting for callback ...\n", p->dev->name);
				lp->dtimer = 0;
				lp->dialstate = 4;
				cmd.driver = lp->isdn_device;
				cmd.command = ISDN_CMD_HANGUP;
				cmd.arg = lp->isdn_channel;
				isdn_command(&cmd);
				isdn_all_eaz(lp->isdn_device, lp->isdn_channel);
			}
			anymore = 1;
			break;
		default:
			printk(KERN_WARNING "isdn_net: Illegal dialstate %d for device %s\n",
			       lp->dialstate, p->dev->name);
		}
		p = (isdn_net_dev *) p->next;
	}
	isdn_timer_ctrl(ISDN_TIMER_NETDIAL, anymore);
}

/*
 * Perform hangup for a net-interface.
 */
void
isdn_net_hangup(struct net_device *d)
{
	isdn_net_local *lp = netdev_priv(d);
	isdn_ctrl cmd;
#ifdef CONFIG_ISDN_X25
	struct concap_proto *cprot = lp->netdev->cprot;
	struct concap_proto_ops *pops = cprot ? cprot->pops : NULL;
#endif

	if (lp->flags & ISDN_NET_CONNECTED) {
		if (lp->slave != NULL) {
			isdn_net_local *slp = ISDN_SLAVE_PRIV(lp);
			if (slp->flags & ISDN_NET_CONNECTED) {
				printk(KERN_INFO
				       "isdn_net: hang up slave %s before %s\n",
				       lp->slave->name, d->name);
				isdn_net_hangup(lp->slave);
			}
		}
		printk(KERN_INFO "isdn_net: local hangup %s\n", d->name);
#ifdef CONFIG_ISDN_PPP
		if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
			isdn_ppp_free(lp);
#endif
		isdn_net_lp_disconnected(lp);
#ifdef CONFIG_ISDN_X25
		/* try if there are generic encap protocol
		   receiver routines and signal the closure of
		   the link */
		if (pops && pops->disconn_ind)
			pops->disconn_ind(cprot);
#endif /* CONFIG_ISDN_X25 */

		cmd.driver = lp->isdn_device;
		cmd.command = ISDN_CMD_HANGUP;
		cmd.arg = lp->isdn_channel;
		isdn_command(&cmd);
		printk(KERN_INFO "%s: Chargesum is %d\n", d->name, lp->charge);
		isdn_all_eaz(lp->isdn_device, lp->isdn_channel);
	}
	isdn_net_unbind_channel(lp);
}

typedef struct {
	__be16 source;
	__be16 dest;
} ip_ports;

static void
isdn_net_log_skb(struct sk_buff *skb, isdn_net_local *lp)
{
	/* hopefully, this was set correctly */
	const u_char *p = skb_network_header(skb);
	unsigned short proto = ntohs(skb->protocol);
	int data_ofs;
	ip_ports *ipp;
	char addinfo[100];

	addinfo[0] = '\0';
	/* This check stolen from 2.1.72 dev_queue_xmit_nit() */
	if (p < skb->data || skb_network_header(skb) >= skb_tail_pointer(skb)) {
		/* fall back to old isdn_net_log_packet method() */
		char *buf = skb->data;

		printk(KERN_DEBUG "isdn_net: protocol %04x is buggy, dev %s\n", skb->protocol, lp->netdev->dev->name);
		p = buf;
		proto = ETH_P_IP;
		switch (lp->p_encap) {
		case ISDN_NET_ENCAP_IPTYP:
			proto = ntohs(*(__be16 *)&buf[0]);
			p = &buf[2];
			break;
		case ISDN_NET_ENCAP_ETHER:
			proto = ntohs(*(__be16 *)&buf[12]);
			p = &buf[14];
			break;
		case ISDN_NET_ENCAP_CISCOHDLC:
			proto = ntohs(*(__be16 *)&buf[2]);
			p = &buf[4];
			break;
#ifdef CONFIG_ISDN_PPP
		case ISDN_NET_ENCAP_SYNCPPP:
			proto = ntohs(skb->protocol);
			p = &buf[IPPP_MAX_HEADER];
			break;
#endif
		}
	}
	data_ofs = ((p[0] & 15) * 4);
	switch (proto) {
	case ETH_P_IP:
		switch (p[9]) {
		case 1:
			strcpy(addinfo, " ICMP");
			break;
		case 2:
			strcpy(addinfo, " IGMP");
			break;
		case 4:
			strcpy(addinfo, " IPIP");
			break;
		case 6:
			ipp = (ip_ports *) (&p[data_ofs]);
			sprintf(addinfo, " TCP, port: %d -> %d", ntohs(ipp->source),
				ntohs(ipp->dest));
			break;
		case 8:
			strcpy(addinfo, " EGP");
			break;
		case 12:
			strcpy(addinfo, " PUP");
			break;
		case 17:
			ipp = (ip_ports *) (&p[data_ofs]);
			sprintf(addinfo, " UDP, port: %d -> %d", ntohs(ipp->source),
				ntohs(ipp->dest));
			break;
		case 22:
			strcpy(addinfo, " IDP");
			break;
		}
		printk(KERN_INFO "OPEN: %pI4 -> %pI4%s\n",
		       p + 12, p + 16, addinfo);
		break;
	case ETH_P_ARP:
		printk(KERN_INFO "OPEN: ARP %pI4 -> *.*.*.* ?%pI4\n",
		       p + 14, p + 24);
		break;
	}
}

/*
 * this function is used to send supervisory data, i.e. data which was
 * not received from the network layer, but e.g. frames from ipppd, CCP
 * reset frames etc.
 */
void isdn_net_write_super(isdn_net_local *lp, struct sk_buff *skb)
{
	if (in_irq()) {
		// we can't grab the lock from irq context,
		// so we just queue the packet
		skb_queue_tail(&lp->super_tx_queue, skb);
		schedule_work(&lp->tqueue);
		return;
	}

	spin_lock_bh(&lp->xmit_lock);
	if (!isdn_net_lp_busy(lp)) {
		isdn_net_writebuf_skb(lp, skb);
	} else {
		skb_queue_tail(&lp->super_tx_queue, skb);
	}
	spin_unlock_bh(&lp->xmit_lock);
}

/*
 * called from tq_immediate
 */
static void isdn_net_softint(struct work_struct *work)
{
	isdn_net_local *lp = container_of(work, isdn_net_local, tqueue);
	struct sk_buff *skb;

	spin_lock_bh(&lp->xmit_lock);
	while (!isdn_net_lp_busy(lp)) {
		skb = skb_dequeue(&lp->super_tx_queue);
		if (!skb)
			break;
		isdn_net_writebuf_skb(lp, skb);
	}
	spin_unlock_bh(&lp->xmit_lock);
}

/*
 * all frames sent from the (net) LL to a HL driver should go via this function
 * it's serialized by the caller holding the lp->xmit_lock spinlock
 */
void isdn_net_writebuf_skb(isdn_net_local *lp, struct sk_buff *skb)
{
	int ret;
	int len = skb->len;     /* save len */

	/* before obtaining the lock the caller should have checked that
	   the lp isn't busy */
	if (isdn_net_lp_busy(lp)) {
		printk("isdn BUG at %s:%d!\n", __FILE__, __LINE__);
		goto error;
	}

	if (!(lp->flags & ISDN_NET_CONNECTED)) {
		printk("isdn BUG at %s:%d!\n", __FILE__, __LINE__);
		goto error;
	}
	ret = isdn_writebuf_skb_stub(lp->isdn_device, lp->isdn_channel, 1, skb);
	if (ret != len) {
		/* we should never get here */
		printk(KERN_WARNING "%s: HL driver queue full\n", lp->netdev->dev->name);
		goto error;
	}

	lp->transcount += len;
	isdn_net_inc_frame_cnt(lp);
	return;

error:
	dev_kfree_skb(skb);
	lp->stats.tx_errors++;

}


/*
 *  Helper function for isdn_net_start_xmit.
 *  When called, the connection is already established.
 *  Based on cps-calculation, check if device is overloaded.
 *  If so, and if a slave exists, trigger dialing for it.
 *  If any slave is online, deliver packets using a simple round robin
 *  scheme.
 *
 *  Return: 0 on success, !0 on failure.
 */

static int
isdn_net_xmit(struct net_device *ndev, struct sk_buff *skb)
{
	isdn_net_dev *nd;
	isdn_net_local *slp;
	isdn_net_local *lp = netdev_priv(ndev);
	int retv = NETDEV_TX_OK;

	if (((isdn_net_local *) netdev_priv(ndev))->master) {
		printk("isdn BUG at %s:%d!\n", __FILE__, __LINE__);
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	/* For the other encaps the header has already been built */
#ifdef CONFIG_ISDN_PPP
	if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP) {
		return isdn_ppp_xmit(skb, ndev);
	}
#endif
	nd = ((isdn_net_local *) netdev_priv(ndev))->netdev;
	lp = isdn_net_get_locked_lp(nd);
	if (!lp) {
		printk(KERN_WARNING "%s: all channels busy - requeuing!\n", ndev->name);
		return NETDEV_TX_BUSY;
	}
	/* we have our lp locked from now on */

	/* Reset hangup-timeout */
	lp->huptimer = 0; // FIXME?
	isdn_net_writebuf_skb(lp, skb);
	spin_unlock_bh(&lp->xmit_lock);

	/* the following stuff is here for backwards compatibility.
	 * in future, start-up and hangup of slaves (based on current load)
	 * should move to userspace and get based on an overall cps
	 * calculation
	 */
	if (lp->cps > lp->triggercps) {
		if (lp->slave) {
			if (!lp->sqfull) {
				/* First time overload: set timestamp only */
				lp->sqfull = 1;
				lp->sqfull_stamp = jiffies;
			} else {
				/* subsequent overload: if slavedelay exceeded, start dialing */
				if (time_after(jiffies, lp->sqfull_stamp + lp->slavedelay)) {
					slp = ISDN_SLAVE_PRIV(lp);
					if (!(slp->flags & ISDN_NET_CONNECTED)) {
						isdn_net_force_dial_lp(ISDN_SLAVE_PRIV(lp));
					}
				}
			}
		}
	} else {
		if (lp->sqfull && time_after(jiffies, lp->sqfull_stamp + lp->slavedelay + (10 * HZ))) {
			lp->sqfull = 0;
		}
		/* this is a hack to allow auto-hangup for slaves on moderate loads */
		nd->queue = nd->local;
	}

	return retv;

}

static void
isdn_net_adjust_hdr(struct sk_buff *skb, struct net_device *dev)
{
	isdn_net_local *lp = netdev_priv(dev);
	if (!skb)
		return;
	if (lp->p_encap == ISDN_NET_ENCAP_ETHER) {
		const int pullsize = skb_network_offset(skb) - ETH_HLEN;
		if (pullsize > 0) {
			printk(KERN_DEBUG "isdn_net: Pull junk %d\n", pullsize);
			skb_pull(skb, pullsize);
		}
	}
}


static void isdn_net_tx_timeout(struct net_device *ndev)
{
	isdn_net_local *lp = netdev_priv(ndev);

	printk(KERN_WARNING "isdn_tx_timeout dev %s dialstate %d\n", ndev->name, lp->dialstate);
	if (!lp->dialstate) {
		lp->stats.tx_errors++;
		/*
		 * There is a certain probability that this currently
		 * works at all because if we always wake up the interface,
		 * then upper layer will try to send the next packet
		 * immediately. And then, the old clean_up logic in the
		 * driver will hopefully continue to work as it used to do.
		 *
		 * This is rather primitive right know, we better should
		 * clean internal queues here, in particular for multilink and
		 * ppp, and reset HL driver's channel, too.   --HE
		 *
		 * actually, this may not matter at all, because ISDN hardware
		 * should not see transmitter hangs at all IMO
		 * changed KERN_DEBUG to KERN_WARNING to find out if this is
		 * ever called   --KG
		 */
	}
	netif_trans_update(ndev);
	netif_wake_queue(ndev);
}

/*
 * Try sending a packet.
 * If this interface isn't connected to a ISDN-Channel, find a free channel,
 * and start dialing.
 */
static netdev_tx_t
isdn_net_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	isdn_net_local *lp = netdev_priv(ndev);
#ifdef CONFIG_ISDN_X25
	struct concap_proto *cprot = lp->netdev->cprot;
/* At this point hard_start_xmit() passes control to the encapsulation
   protocol (if present).
   For X.25 auto-dialing is completly bypassed because:
   - It does not conform with the semantics of a reliable datalink
   service as needed by X.25 PLP.
   - I don't want that the interface starts dialing when the network layer
   sends a message which requests to disconnect the lapb link (or if it
   sends any other message not resulting in data transmission).
   Instead, dialing will be initiated by the encapsulation protocol entity
   when a dl_establish request is received from the upper layer.
*/
	if (cprot && cprot->pops) {
		int ret = cprot->pops->encap_and_xmit(cprot, skb);

		if (ret)
			netif_stop_queue(ndev);
		return ret;
	} else
#endif
		/* auto-dialing xmit function */
	{
#ifdef ISDN_DEBUG_NET_DUMP
		u_char *buf;
#endif
		isdn_net_adjust_hdr(skb, ndev);
#ifdef ISDN_DEBUG_NET_DUMP
		buf = skb->data;
		isdn_dumppkt("S:", buf, skb->len, 40);
#endif

		if (!(lp->flags & ISDN_NET_CONNECTED)) {
			int chi;
			/* only do autodial if allowed by config */
			if (!(ISDN_NET_DIALMODE(*lp) == ISDN_NET_DM_AUTO)) {
				isdn_net_unreachable(ndev, skb, "dial rejected: interface not in dialmode `auto'");
				dev_kfree_skb(skb);
				return NETDEV_TX_OK;
			}
			if (lp->phone[1]) {
				ulong flags;

				if (lp->dialwait_timer <= 0)
					if (lp->dialstarted > 0 && lp->dialtimeout > 0 && time_before(jiffies, lp->dialstarted + lp->dialtimeout + lp->dialwait))
						lp->dialwait_timer = lp->dialstarted + lp->dialtimeout + lp->dialwait;

				if (lp->dialwait_timer > 0) {
					if (time_before(jiffies, lp->dialwait_timer)) {
						isdn_net_unreachable(ndev, skb, "dial rejected: retry-time not reached");
						dev_kfree_skb(skb);
						return NETDEV_TX_OK;
					} else
						lp->dialwait_timer = 0;
				}
				/* Grab a free ISDN-Channel */
				spin_lock_irqsave(&dev->lock, flags);
				if (((chi =
				      isdn_get_free_channel(
					      ISDN_USAGE_NET,
					      lp->l2_proto,
					      lp->l3_proto,
					      lp->pre_device,
					      lp->pre_channel,
					      lp->msn)
					     ) < 0) &&
				    ((chi =
				      isdn_get_free_channel(
					      ISDN_USAGE_NET,
					      lp->l2_proto,
					      lp->l3_proto,
					      lp->pre_device,
					      lp->pre_channel^1,
					      lp->msn)
					    ) < 0)) {
					spin_unlock_irqrestore(&dev->lock, flags);
					isdn_net_unreachable(ndev, skb,
							     "No channel");
					dev_kfree_skb(skb);
					return NETDEV_TX_OK;
				}
				/* Log packet, which triggered dialing */
				if (dev->net_verbose)
					isdn_net_log_skb(skb, lp);
				lp->dialstate = 1;
				/* Connect interface with channel */
				isdn_net_bind_channel(lp, chi);
#ifdef CONFIG_ISDN_PPP
				if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP) {
					/* no 'first_skb' handling for syncPPP */
					if (isdn_ppp_bind(lp) < 0) {
						dev_kfree_skb(skb);
						isdn_net_unbind_channel(lp);
						spin_unlock_irqrestore(&dev->lock, flags);
						return NETDEV_TX_OK;	/* STN (skb to nirvana) ;) */
					}
#ifdef CONFIG_IPPP_FILTER
					if (isdn_ppp_autodial_filter(skb, lp)) {
						isdn_ppp_free(lp);
						isdn_net_unbind_channel(lp);
						spin_unlock_irqrestore(&dev->lock, flags);
						isdn_net_unreachable(ndev, skb, "dial rejected: packet filtered");
						dev_kfree_skb(skb);
						return NETDEV_TX_OK;
					}
#endif
					spin_unlock_irqrestore(&dev->lock, flags);
					isdn_net_dial();	/* Initiate dialing */
					netif_stop_queue(ndev);
					return NETDEV_TX_BUSY;	/* let upper layer requeue skb packet */
				}
#endif
				/* Initiate dialing */
				spin_unlock_irqrestore(&dev->lock, flags);
				isdn_net_dial();
				isdn_net_device_stop_queue(lp);
				return NETDEV_TX_BUSY;
			} else {
				isdn_net_unreachable(ndev, skb,
						     "No phone number");
				dev_kfree_skb(skb);
				return NETDEV_TX_OK;
			}
		} else {
			/* Device is connected to an ISDN channel */
			netif_trans_update(ndev);
			if (!lp->dialstate) {
				/* ISDN connection is established, try sending */
				int ret;
				ret = (isdn_net_xmit(ndev, skb));
				if (ret) netif_stop_queue(ndev);
				return ret;
			} else
				netif_stop_queue(ndev);
		}
	}
	return NETDEV_TX_BUSY;
}

/*
 * Shutdown a net-interface.
 */
static int
isdn_net_close(struct net_device *dev)
{
	struct net_device *p;
#ifdef CONFIG_ISDN_X25
	struct concap_proto *cprot =
		((isdn_net_local *)netdev_priv(dev))->netdev->cprot;
	/* printk(KERN_DEBUG "isdn_net_close %s\n" , dev-> name); */
#endif

#ifdef CONFIG_ISDN_X25
	if (cprot && cprot->pops) cprot->pops->close(cprot);
#endif
	netif_stop_queue(dev);
	p = MASTER_TO_SLAVE(dev);
	if (p) {
		/* If this interface has slaves, stop them also */
		while (p) {
#ifdef CONFIG_ISDN_X25
			cprot = ((isdn_net_local *)netdev_priv(p))
				->netdev->cprot;
			if (cprot && cprot->pops)
				cprot->pops->close(cprot);
#endif
			isdn_net_hangup(p);
			p = MASTER_TO_SLAVE(p);
		}
	}
	isdn_net_hangup(dev);
	isdn_unlock_drivers();
	return 0;
}

/*
 * Get statistics
 */
static struct net_device_stats *
isdn_net_get_stats(struct net_device *dev)
{
	isdn_net_local *lp = netdev_priv(dev);
	return &lp->stats;
}

/*      This is simply a copy from std. eth.c EXCEPT we pull ETH_HLEN
 *      instead of dev->hard_header_len off. This is done because the
 *      lowlevel-driver has already pulled off its stuff when we get
 *      here and this routine only gets called with p_encap == ETHER.
 *      Determine the packet's protocol ID. The rule here is that we
 *      assume 802.3 if the type field is short enough to be a length.
 *      This is normal practice and works for any 'now in use' protocol.
 */

static __be16
isdn_net_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	struct ethhdr *eth;
	unsigned char *rawp;

	skb_reset_mac_header(skb);
	skb_pull(skb, ETH_HLEN);
	eth = eth_hdr(skb);

	if (*eth->h_dest & 1) {
		if (ether_addr_equal(eth->h_dest, dev->broadcast))
			skb->pkt_type = PACKET_BROADCAST;
		else
			skb->pkt_type = PACKET_MULTICAST;
	}
	/*
	 *      This ALLMULTI check should be redundant by 1.4
	 *      so don't forget to remove it.
	 */

	else if (dev->flags & (IFF_PROMISC /*| IFF_ALLMULTI*/)) {
		if (!ether_addr_equal(eth->h_dest, dev->dev_addr))
			skb->pkt_type = PACKET_OTHERHOST;
	}
	if (ntohs(eth->h_proto) >= ETH_P_802_3_MIN)
		return eth->h_proto;

	rawp = skb->data;

	/*
	 *      This is a magic hack to spot IPX packets. Older Novell breaks
	 *      the protocol design and runs IPX over 802.3 without an 802.2 LLC
	 *      layer. We look for FFFF which isn't a used 802.2 SSAP/DSAP. This
	 *      won't work for fault tolerant netware but does for the rest.
	 */
	if (*(unsigned short *) rawp == 0xFFFF)
		return htons(ETH_P_802_3);
	/*
	 *      Real 802.2 LLC
	 */
	return htons(ETH_P_802_2);
}


/*
 * CISCO HDLC keepalive specific stuff
 */
static struct sk_buff*
isdn_net_ciscohdlck_alloc_skb(isdn_net_local *lp, int len)
{
	unsigned short hl = dev->drv[lp->isdn_device]->interface->hl_hdrlen;
	struct sk_buff *skb;

	skb = alloc_skb(hl + len, GFP_ATOMIC);
	if (skb)
		skb_reserve(skb, hl);
	else
		printk("isdn out of mem at %s:%d!\n", __FILE__, __LINE__);
	return skb;
}

/* cisco hdlck device private ioctls */
static int
isdn_ciscohdlck_dev_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	isdn_net_local *lp = netdev_priv(dev);
	unsigned long len = 0;
	unsigned long expires = 0;
	int tmp = 0;
	int period = lp->cisco_keepalive_period;
	s8 debserint = lp->cisco_debserint;
	int rc = 0;

	if (lp->p_encap != ISDN_NET_ENCAP_CISCOHDLCK)
		return -EINVAL;

	switch (cmd) {
		/* get/set keepalive period */
	case SIOCGKEEPPERIOD:
		len = (unsigned long)sizeof(lp->cisco_keepalive_period);
		if (copy_to_user(ifr->ifr_data,
				 &lp->cisco_keepalive_period, len))
			rc = -EFAULT;
		break;
	case SIOCSKEEPPERIOD:
		tmp = lp->cisco_keepalive_period;
		len = (unsigned long)sizeof(lp->cisco_keepalive_period);
		if (copy_from_user(&period, ifr->ifr_data, len))
			rc = -EFAULT;
		if ((period > 0) && (period <= 32767))
			lp->cisco_keepalive_period = period;
		else
			rc = -EINVAL;
		if (!rc && (tmp != lp->cisco_keepalive_period)) {
			expires = (unsigned long)(jiffies +
						  lp->cisco_keepalive_period * HZ);
			mod_timer(&lp->cisco_timer, expires);
			printk(KERN_INFO "%s: Keepalive period set "
			       "to %d seconds.\n",
			       dev->name, lp->cisco_keepalive_period);
		}
		break;

		/* get/set debugging */
	case SIOCGDEBSERINT:
		len = (unsigned long)sizeof(lp->cisco_debserint);
		if (copy_to_user(ifr->ifr_data,
				 &lp->cisco_debserint, len))
			rc = -EFAULT;
		break;
	case SIOCSDEBSERINT:
		len = (unsigned long)sizeof(lp->cisco_debserint);
		if (copy_from_user(&debserint,
				   ifr->ifr_data, len))
			rc = -EFAULT;
		if ((debserint >= 0) && (debserint <= 64))
			lp->cisco_debserint = debserint;
		else
			rc = -EINVAL;
		break;

	default:
		rc = -EINVAL;
		break;
	}
	return (rc);
}


static int isdn_net_ioctl(struct net_device *dev,
			  struct ifreq *ifr, int cmd)
{
	isdn_net_local *lp = netdev_priv(dev);

	switch (lp->p_encap) {
#ifdef CONFIG_ISDN_PPP
	case ISDN_NET_ENCAP_SYNCPPP:
		return isdn_ppp_dev_ioctl(dev, ifr, cmd);
#endif
	case ISDN_NET_ENCAP_CISCOHDLCK:
		return isdn_ciscohdlck_dev_ioctl(dev, ifr, cmd);
	default:
		return -EINVAL;
	}
}

/* called via cisco_timer.function */
static void
isdn_net_ciscohdlck_slarp_send_keepalive(unsigned long data)
{
	isdn_net_local *lp = (isdn_net_local *) data;
	struct sk_buff *skb;
	unsigned char *p;
	unsigned long last_cisco_myseq = lp->cisco_myseq;
	int myseq_diff = 0;

	if (!(lp->flags & ISDN_NET_CONNECTED) || lp->dialstate) {
		printk("isdn BUG at %s:%d!\n", __FILE__, __LINE__);
		return;
	}
	lp->cisco_myseq++;

	myseq_diff = (lp->cisco_myseq - lp->cisco_mineseen);
	if ((lp->cisco_line_state) && ((myseq_diff >= 3) || (myseq_diff <= -3))) {
		/* line up -> down */
		lp->cisco_line_state = 0;
		printk(KERN_WARNING
		       "UPDOWN: Line protocol on Interface %s,"
		       " changed state to down\n", lp->netdev->dev->name);
		/* should stop routing higher-level data across */
	} else if ((!lp->cisco_line_state) &&
		   (myseq_diff >= 0) && (myseq_diff <= 2)) {
		/* line down -> up */
		lp->cisco_line_state = 1;
		printk(KERN_WARNING
		       "UPDOWN: Line protocol on Interface %s,"
		       " changed state to up\n", lp->netdev->dev->name);
		/* restart routing higher-level data across */
	}

	if (lp->cisco_debserint)
		printk(KERN_DEBUG "%s: HDLC "
		       "myseq %lu, mineseen %lu%c, yourseen %lu, %s\n",
		       lp->netdev->dev->name, last_cisco_myseq, lp->cisco_mineseen,
		       ((last_cisco_myseq == lp->cisco_mineseen) ? '*' : 040),
		       lp->cisco_yourseq,
		       ((lp->cisco_line_state) ? "line up" : "line down"));

	skb = isdn_net_ciscohdlck_alloc_skb(lp, 4 + 14);
	if (!skb)
		return;

	p = skb_put(skb, 4 + 14);

	/* cisco header */
	*(u8 *)(p + 0) = CISCO_ADDR_UNICAST;
	*(u8 *)(p + 1) = CISCO_CTRL;
	*(__be16 *)(p + 2) = cpu_to_be16(CISCO_TYPE_SLARP);

	/* slarp keepalive */
	*(__be32 *)(p +  4) = cpu_to_be32(CISCO_SLARP_KEEPALIVE);
	*(__be32 *)(p +  8) = cpu_to_be32(lp->cisco_myseq);
	*(__be32 *)(p + 12) = cpu_to_be32(lp->cisco_yourseq);
	*(__be16 *)(p + 16) = cpu_to_be16(0xffff); // reliability, always 0xffff
	p += 18;

	isdn_net_write_super(lp, skb);

	lp->cisco_timer.expires = jiffies + lp->cisco_keepalive_period * HZ;

	add_timer(&lp->cisco_timer);
}

static void
isdn_net_ciscohdlck_slarp_send_request(isdn_net_local *lp)
{
	struct sk_buff *skb;
	unsigned char *p;

	skb = isdn_net_ciscohdlck_alloc_skb(lp, 4 + 14);
	if (!skb)
		return;

	p = skb_put(skb, 4 + 14);

	/* cisco header */
	*(u8 *)(p + 0) = CISCO_ADDR_UNICAST;
	*(u8 *)(p + 1) = CISCO_CTRL;
	*(__be16 *)(p + 2) = cpu_to_be16(CISCO_TYPE_SLARP);

	/* slarp request */
	*(__be32 *)(p +  4) = cpu_to_be32(CISCO_SLARP_REQUEST);
	*(__be32 *)(p +  8) = cpu_to_be32(0); // address
	*(__be32 *)(p + 12) = cpu_to_be32(0); // netmask
	*(__be16 *)(p + 16) = cpu_to_be16(0); // unused
	p += 18;

	isdn_net_write_super(lp, skb);
}

static void
isdn_net_ciscohdlck_connected(isdn_net_local *lp)
{
	lp->cisco_myseq = 0;
	lp->cisco_mineseen = 0;
	lp->cisco_yourseq = 0;
	lp->cisco_keepalive_period = ISDN_TIMER_KEEPINT;
	lp->cisco_last_slarp_in = 0;
	lp->cisco_line_state = 0;
	lp->cisco_debserint = 0;

	/* send slarp request because interface/seq.no.s reset */
	isdn_net_ciscohdlck_slarp_send_request(lp);

	init_timer(&lp->cisco_timer);
	lp->cisco_timer.data = (unsigned long) lp;
	lp->cisco_timer.function = isdn_net_ciscohdlck_slarp_send_keepalive;
	lp->cisco_timer.expires = jiffies + lp->cisco_keepalive_period * HZ;
	add_timer(&lp->cisco_timer);
}

static void
isdn_net_ciscohdlck_disconnected(isdn_net_local *lp)
{
	del_timer(&lp->cisco_timer);
}

static void
isdn_net_ciscohdlck_slarp_send_reply(isdn_net_local *lp)
{
	struct sk_buff *skb;
	unsigned char *p;
	struct in_device *in_dev = NULL;
	__be32 addr = 0;		/* local ipv4 address */
	__be32 mask = 0;		/* local netmask */

	if ((in_dev = lp->netdev->dev->ip_ptr) != NULL) {
		/* take primary(first) address of interface */
		struct in_ifaddr *ifa = in_dev->ifa_list;
		if (ifa != NULL) {
			addr = ifa->ifa_local;
			mask = ifa->ifa_mask;
		}
	}

	skb = isdn_net_ciscohdlck_alloc_skb(lp, 4 + 14);
	if (!skb)
		return;

	p = skb_put(skb, 4 + 14);

	/* cisco header */
	*(u8 *)(p + 0) = CISCO_ADDR_UNICAST;
	*(u8 *)(p + 1) = CISCO_CTRL;
	*(__be16 *)(p + 2) = cpu_to_be16(CISCO_TYPE_SLARP);

	/* slarp reply, send own ip/netmask; if values are nonsense remote
	 * should think we are unable to provide it with an address via SLARP */
	*(__be32 *)(p +  4) = cpu_to_be32(CISCO_SLARP_REPLY);
	*(__be32 *)(p +  8) = addr; // address
	*(__be32 *)(p + 12) = mask; // netmask
	*(__be16 *)(p + 16) = cpu_to_be16(0); // unused
	p += 18;

	isdn_net_write_super(lp, skb);
}

static void
isdn_net_ciscohdlck_slarp_in(isdn_net_local *lp, struct sk_buff *skb)
{
	unsigned char *p;
	int period;
	u32 code;
	u32 my_seq;
	u32 your_seq;
	__be32 local;
	__be32 *addr, *mask;

	if (skb->len < 14)
		return;

	p = skb->data;
	code = be32_to_cpup((__be32 *)p);
	p += 4;

	switch (code) {
	case CISCO_SLARP_REQUEST:
		lp->cisco_yourseq = 0;
		isdn_net_ciscohdlck_slarp_send_reply(lp);
		break;
	case CISCO_SLARP_REPLY:
		addr = (__be32 *)p;
		mask = (__be32 *)(p + 4);
		if (*mask != cpu_to_be32(0xfffffffc))
			goto slarp_reply_out;
		if ((*addr & cpu_to_be32(3)) == cpu_to_be32(0) ||
		    (*addr & cpu_to_be32(3)) == cpu_to_be32(3))
			goto slarp_reply_out;
		local = *addr ^ cpu_to_be32(3);
		printk(KERN_INFO "%s: got slarp reply: remote ip: %pI4, local ip: %pI4 mask: %pI4\n",
		       lp->netdev->dev->name, addr, &local, mask);
		break;
	slarp_reply_out:
		printk(KERN_INFO "%s: got invalid slarp reply (%pI4/%pI4) - ignored\n",
		       lp->netdev->dev->name, addr, mask);
		break;
	case CISCO_SLARP_KEEPALIVE:
		period = (int)((jiffies - lp->cisco_last_slarp_in
				+ HZ / 2 - 1) / HZ);
		if (lp->cisco_debserint &&
		    (period != lp->cisco_keepalive_period) &&
		    lp->cisco_last_slarp_in) {
			printk(KERN_DEBUG "%s: Keepalive period mismatch - "
			       "is %d but should be %d.\n",
			       lp->netdev->dev->name, period,
			       lp->cisco_keepalive_period);
		}
		lp->cisco_last_slarp_in = jiffies;
		my_seq = be32_to_cpup((__be32 *)(p + 0));
		your_seq = be32_to_cpup((__be32 *)(p + 4));
		p += 10;
		lp->cisco_yourseq = my_seq;
		lp->cisco_mineseen = your_seq;
		break;
	}
}

static void
isdn_net_ciscohdlck_receive(isdn_net_local *lp, struct sk_buff *skb)
{
	unsigned char *p;
	u8 addr;
	u8 ctrl;
	u16 type;

	if (skb->len < 4)
		goto out_free;

	p = skb->data;
	addr = *(u8 *)(p + 0);
	ctrl = *(u8 *)(p + 1);
	type = be16_to_cpup((__be16 *)(p + 2));
	p += 4;
	skb_pull(skb, 4);

	if (addr != CISCO_ADDR_UNICAST && addr != CISCO_ADDR_BROADCAST) {
		printk(KERN_WARNING "%s: Unknown Cisco addr 0x%02x\n",
		       lp->netdev->dev->name, addr);
		goto out_free;
	}
	if (ctrl != CISCO_CTRL) {
		printk(KERN_WARNING "%s: Unknown Cisco ctrl 0x%02x\n",
		       lp->netdev->dev->name, ctrl);
		goto out_free;
	}

	switch (type) {
	case CISCO_TYPE_SLARP:
		isdn_net_ciscohdlck_slarp_in(lp, skb);
		goto out_free;
	case CISCO_TYPE_CDP:
		if (lp->cisco_debserint)
			printk(KERN_DEBUG "%s: Received CDP packet. use "
			       "\"no cdp enable\" on cisco.\n",
			       lp->netdev->dev->name);
		goto out_free;
	default:
		/* no special cisco protocol */
		skb->protocol = htons(type);
		netif_rx(skb);
		return;
	}

out_free:
	kfree_skb(skb);
}

/*
 * Got a packet from ISDN-Channel.
 */
static void
isdn_net_receive(struct net_device *ndev, struct sk_buff *skb)
{
	isdn_net_local *lp = netdev_priv(ndev);
	isdn_net_local *olp = lp;	/* original 'lp' */
#ifdef CONFIG_ISDN_X25
	struct concap_proto *cprot = lp->netdev->cprot;
#endif
	lp->transcount += skb->len;

	lp->stats.rx_packets++;
	lp->stats.rx_bytes += skb->len;
	if (lp->master) {
		/* Bundling: If device is a slave-device, deliver to master, also
		 * handle master's statistics and hangup-timeout
		 */
		ndev = lp->master;
		lp = netdev_priv(ndev);
		lp->stats.rx_packets++;
		lp->stats.rx_bytes += skb->len;
	}
	skb->dev = ndev;
	skb->pkt_type = PACKET_HOST;
	skb_reset_mac_header(skb);
#ifdef ISDN_DEBUG_NET_DUMP
	isdn_dumppkt("R:", skb->data, skb->len, 40);
#endif
	switch (lp->p_encap) {
	case ISDN_NET_ENCAP_ETHER:
		/* Ethernet over ISDN */
		olp->huptimer = 0;
		lp->huptimer = 0;
		skb->protocol = isdn_net_type_trans(skb, ndev);
		break;
	case ISDN_NET_ENCAP_UIHDLC:
		/* HDLC with UI-frame (for ispa with -h1 option) */
		olp->huptimer = 0;
		lp->huptimer = 0;
		skb_pull(skb, 2);
		/* Fall through */
	case ISDN_NET_ENCAP_RAWIP:
		/* RAW-IP without MAC-Header */
		olp->huptimer = 0;
		lp->huptimer = 0;
		skb->protocol = htons(ETH_P_IP);
		break;
	case ISDN_NET_ENCAP_CISCOHDLCK:
		isdn_net_ciscohdlck_receive(lp, skb);
		return;
	case ISDN_NET_ENCAP_CISCOHDLC:
		/* CISCO-HDLC IP with type field and  fake I-frame-header */
		skb_pull(skb, 2);
		/* Fall through */
	case ISDN_NET_ENCAP_IPTYP:
		/* IP with type field */
		olp->huptimer = 0;
		lp->huptimer = 0;
		skb->protocol = *(__be16 *)&(skb->data[0]);
		skb_pull(skb, 2);
		if (*(unsigned short *) skb->data == 0xFFFF)
			skb->protocol = htons(ETH_P_802_3);
		break;
#ifdef CONFIG_ISDN_PPP
	case ISDN_NET_ENCAP_SYNCPPP:
		/* huptimer is done in isdn_ppp_push_higher */
		isdn_ppp_receive(lp->netdev, olp, skb);
		return;
#endif

	default:
#ifdef CONFIG_ISDN_X25
		/* try if there are generic sync_device receiver routines */
		if (cprot) if (cprot->pops)
				   if (cprot->pops->data_ind) {
					   cprot->pops->data_ind(cprot, skb);
					   return;
				   };
#endif /* CONFIG_ISDN_X25 */
		printk(KERN_WARNING "%s: unknown encapsulation, dropping\n",
		       lp->netdev->dev->name);
		kfree_skb(skb);
		return;
	}

	netif_rx(skb);
	return;
}

/*
 * A packet arrived via ISDN. Search interface-chain for a corresponding
 * interface. If found, deliver packet to receiver-function and return 1,
 * else return 0.
 */
int
isdn_net_rcv_skb(int idx, struct sk_buff *skb)
{
	isdn_net_dev *p = dev->rx_netdev[idx];

	if (p) {
		isdn_net_local *lp = p->local;
		if ((lp->flags & ISDN_NET_CONNECTED) &&
		    (!lp->dialstate)) {
			isdn_net_receive(p->dev, skb);
			return 1;
		}
	}
	return 0;
}

/*
 *  build an header
 *  depends on encaps that is being used.
 */

static int isdn_net_header(struct sk_buff *skb, struct net_device *dev,
			   unsigned short type,
			   const void *daddr, const void *saddr, unsigned plen)
{
	isdn_net_local *lp = netdev_priv(dev);
	unsigned char *p;
	int len = 0;

	switch (lp->p_encap) {
	case ISDN_NET_ENCAP_ETHER:
		len = eth_header(skb, dev, type, daddr, saddr, plen);
		break;
#ifdef CONFIG_ISDN_PPP
	case ISDN_NET_ENCAP_SYNCPPP:
		/* stick on a fake header to keep fragmentation code happy. */
		len = IPPP_MAX_HEADER;
		skb_push(skb, len);
		break;
#endif
	case ISDN_NET_ENCAP_RAWIP:
		printk(KERN_WARNING "isdn_net_header called with RAW_IP!\n");
		len = 0;
		break;
	case ISDN_NET_ENCAP_IPTYP:
		/* ethernet type field */
		*((__be16 *)skb_push(skb, 2)) = htons(type);
		len = 2;
		break;
	case ISDN_NET_ENCAP_UIHDLC:
		/* HDLC with UI-Frames (for ispa with -h1 option) */
		*((__be16 *)skb_push(skb, 2)) = htons(0x0103);
		len = 2;
		break;
	case ISDN_NET_ENCAP_CISCOHDLC:
	case ISDN_NET_ENCAP_CISCOHDLCK:
		p = skb_push(skb, 4);
		*(u8 *)(p + 0) = CISCO_ADDR_UNICAST;
		*(u8 *)(p + 1) = CISCO_CTRL;
		*(__be16 *)(p + 2) = cpu_to_be16(type);
		p += 4;
		len = 4;
		break;
#ifdef CONFIG_ISDN_X25
	default:
		/* try if there are generic concap protocol routines */
		if (lp->netdev->cprot) {
			printk(KERN_WARNING "isdn_net_header called with concap_proto!\n");
			len = 0;
			break;
		}
		break;
#endif /* CONFIG_ISDN_X25 */
	}
	return len;
}

static int isdn_header_cache(const struct neighbour *neigh, struct hh_cache *hh,
			     __be16 type)
{
	const struct net_device *dev = neigh->dev;
	isdn_net_local *lp = netdev_priv(dev);

	if (lp->p_encap == ISDN_NET_ENCAP_ETHER)
		return eth_header_cache(neigh, hh, type);
	return -1;
}

static void isdn_header_cache_update(struct hh_cache *hh,
				     const struct net_device *dev,
				     const unsigned char *haddr)
{
	isdn_net_local *lp = netdev_priv(dev);
	if (lp->p_encap == ISDN_NET_ENCAP_ETHER)
		eth_header_cache_update(hh, dev, haddr);
}

static const struct header_ops isdn_header_ops = {
	.create = isdn_net_header,
	.cache = isdn_header_cache,
	.cache_update = isdn_header_cache_update,
};

/*
 * Interface-setup. (just after registering a new interface)
 */
static int
isdn_net_init(struct net_device *ndev)
{
	ushort max_hlhdr_len = 0;
	int drvidx;

	/*
	 *  up till binding we ask the protocol layer to reserve as much
	 *  as we might need for HL layer
	 */

	for (drvidx = 0; drvidx < ISDN_MAX_DRIVERS; drvidx++)
		if (dev->drv[drvidx])
			if (max_hlhdr_len < dev->drv[drvidx]->interface->hl_hdrlen)
				max_hlhdr_len = dev->drv[drvidx]->interface->hl_hdrlen;

	ndev->hard_header_len = ETH_HLEN + max_hlhdr_len;
	return 0;
}

static void
isdn_net_swapbind(int drvidx)
{
	isdn_net_dev *p;

#ifdef ISDN_DEBUG_NET_ICALL
	printk(KERN_DEBUG "n_fi: swapping ch of %d\n", drvidx);
#endif
	p = dev->netdev;
	while (p) {
		if (p->local->pre_device == drvidx)
			switch (p->local->pre_channel) {
			case 0:
				p->local->pre_channel = 1;
				break;
			case 1:
				p->local->pre_channel = 0;
				break;
			}
		p = (isdn_net_dev *) p->next;
	}
}

static void
isdn_net_swap_usage(int i1, int i2)
{
	int u1 = dev->usage[i1] & ISDN_USAGE_EXCLUSIVE;
	int u2 = dev->usage[i2] & ISDN_USAGE_EXCLUSIVE;

#ifdef ISDN_DEBUG_NET_ICALL
	printk(KERN_DEBUG "n_fi: usage of %d and %d\n", i1, i2);
#endif
	dev->usage[i1] &= ~ISDN_USAGE_EXCLUSIVE;
	dev->usage[i1] |= u2;
	dev->usage[i2] &= ~ISDN_USAGE_EXCLUSIVE;
	dev->usage[i2] |= u1;
	isdn_info_update();
}

/*
 * An incoming call-request has arrived.
 * Search the interface-chain for an appropriate interface.
 * If found, connect the interface to the ISDN-channel and initiate
 * D- and B-Channel-setup. If secure-flag is set, accept only
 * configured phone-numbers. If callback-flag is set, initiate
 * callback-dialing.
 *
 * Return-Value: 0 = No appropriate interface for this call.
 *               1 = Call accepted
 *               2 = Reject call, wait cbdelay, then call back
 *               3 = Reject call
 *               4 = Wait cbdelay, then call back
 *               5 = No appropriate interface for this call,
 *                   would eventually match if CID was longer.
 */

int
isdn_net_find_icall(int di, int ch, int idx, setup_parm *setup)
{
	char *eaz;
	int si1;
	int si2;
	int ematch;
	int wret;
	int swapped;
	int sidx = 0;
	u_long flags;
	isdn_net_dev *p;
	isdn_net_phone *n;
	char nr[ISDN_MSNLEN];
	char *my_eaz;

	/* Search name in netdev-chain */
	if (!setup->phone[0]) {
		nr[0] = '0';
		nr[1] = '\0';
		printk(KERN_INFO "isdn_net: Incoming call without OAD, assuming '0'\n");
	} else
		strlcpy(nr, setup->phone, ISDN_MSNLEN);
	si1 = (int) setup->si1;
	si2 = (int) setup->si2;
	if (!setup->eazmsn[0]) {
		printk(KERN_WARNING "isdn_net: Incoming call without CPN, assuming '0'\n");
		eaz = "0";
	} else
		eaz = setup->eazmsn;
	if (dev->net_verbose > 1)
		printk(KERN_INFO "isdn_net: call from %s,%d,%d -> %s\n", nr, si1, si2, eaz);
	/* Accept DATA and VOICE calls at this stage
	 * local eaz is checked later for allowed call types
	 */
	if ((si1 != 7) && (si1 != 1)) {
		if (dev->net_verbose > 1)
			printk(KERN_INFO "isdn_net: Service-Indicator not 1 or 7, ignored\n");
		return 0;
	}
	n = (isdn_net_phone *) 0;
	p = dev->netdev;
	ematch = wret = swapped = 0;
#ifdef ISDN_DEBUG_NET_ICALL
	printk(KERN_DEBUG "n_fi: di=%d ch=%d idx=%d usg=%d\n", di, ch, idx,
	       dev->usage[idx]);
#endif
	while (p) {
		int matchret;
		isdn_net_local *lp = p->local;

		/* If last check has triggered as binding-swap, revert it */
		switch (swapped) {
		case 2:
			isdn_net_swap_usage(idx, sidx);
			/* fall through */
		case 1:
			isdn_net_swapbind(di);
			break;
		}
		swapped = 0;
		/* check acceptable call types for DOV */
		my_eaz = isdn_map_eaz2msn(lp->msn, di);
		if (si1 == 1) { /* it's a DOV call, check if we allow it */
			if (*my_eaz == 'v' || *my_eaz == 'V' ||
			    *my_eaz == 'b' || *my_eaz == 'B')
				my_eaz++; /* skip to allow a match */
			else
				my_eaz = NULL; /* force non match */
		} else { /* it's a DATA call, check if we allow it */
			if (*my_eaz == 'b' || *my_eaz == 'B')
				my_eaz++; /* skip to allow a match */
		}
		if (my_eaz)
			matchret = isdn_msncmp(eaz, my_eaz);
		else
			matchret = 1;
		if (!matchret)
			ematch = 1;

		/* Remember if more numbers eventually can match */
		if (matchret > wret)
			wret = matchret;
#ifdef ISDN_DEBUG_NET_ICALL
		printk(KERN_DEBUG "n_fi: if='%s', l.msn=%s, l.flags=%d, l.dstate=%d\n",
		       p->dev->name, lp->msn, lp->flags, lp->dialstate);
#endif
		if ((!matchret) &&                                        /* EAZ is matching   */
		    (((!(lp->flags & ISDN_NET_CONNECTED)) &&              /* but not connected */
		      (USG_NONE(dev->usage[idx]))) ||                     /* and ch. unused or */
		     ((((lp->dialstate == 4) || (lp->dialstate == 12)) && /* if dialing        */
		       (!(lp->flags & ISDN_NET_CALLBACK)))                /* but no callback   */
			     )))
		{
#ifdef ISDN_DEBUG_NET_ICALL
			printk(KERN_DEBUG "n_fi: match1, pdev=%d pch=%d\n",
			       lp->pre_device, lp->pre_channel);
#endif
			if (dev->usage[idx] & ISDN_USAGE_EXCLUSIVE) {
				if ((lp->pre_channel != ch) ||
				    (lp->pre_device != di)) {
					/* Here we got a problem:
					 * If using an ICN-Card, an incoming call is always signaled on
					 * on the first channel of the card, if both channels are
					 * down. However this channel may be bound exclusive. If the
					 * second channel is free, this call should be accepted.
					 * The solution is horribly but it runs, so what:
					 * We exchange the exclusive bindings of the two channels, the
					 * corresponding variables in the interface-structs.
					 */
					if (ch == 0) {
						sidx = isdn_dc2minor(di, 1);
#ifdef ISDN_DEBUG_NET_ICALL
						printk(KERN_DEBUG "n_fi: ch is 0\n");
#endif
						if (USG_NONE(dev->usage[sidx])) {
							/* Second Channel is free, now see if it is bound
							 * exclusive too. */
							if (dev->usage[sidx] & ISDN_USAGE_EXCLUSIVE) {
#ifdef ISDN_DEBUG_NET_ICALL
								printk(KERN_DEBUG "n_fi: 2nd channel is down and bound\n");
#endif
								/* Yes, swap bindings only, if the original
								 * binding is bound to channel 1 of this driver */
								if ((lp->pre_device == di) &&
								    (lp->pre_channel == 1)) {
									isdn_net_swapbind(di);
									swapped = 1;
								} else {
									/* ... else iterate next device */
									p = (isdn_net_dev *) p->next;
									continue;
								}
							} else {
#ifdef ISDN_DEBUG_NET_ICALL
								printk(KERN_DEBUG "n_fi: 2nd channel is down and unbound\n");
#endif
								/* No, swap always and swap excl-usage also */
								isdn_net_swap_usage(idx, sidx);
								isdn_net_swapbind(di);
								swapped = 2;
							}
							/* Now check for exclusive binding again */
#ifdef ISDN_DEBUG_NET_ICALL
							printk(KERN_DEBUG "n_fi: final check\n");
#endif
							if ((dev->usage[idx] & ISDN_USAGE_EXCLUSIVE) &&
							    ((lp->pre_channel != ch) ||
							     (lp->pre_device != di))) {
#ifdef ISDN_DEBUG_NET_ICALL
								printk(KERN_DEBUG "n_fi: final check failed\n");
#endif
								p = (isdn_net_dev *) p->next;
								continue;
							}
						}
					} else {
						/* We are already on the second channel, so nothing to do */
#ifdef ISDN_DEBUG_NET_ICALL
						printk(KERN_DEBUG "n_fi: already on 2nd channel\n");
#endif
					}
				}
			}
#ifdef ISDN_DEBUG_NET_ICALL
			printk(KERN_DEBUG "n_fi: match2\n");
#endif
			n = lp->phone[0];
			if (lp->flags & ISDN_NET_SECURE) {
				while (n) {
					if (!isdn_msncmp(nr, n->num))
						break;
					n = (isdn_net_phone *) n->next;
				}
			}
			if (n || (!(lp->flags & ISDN_NET_SECURE))) {
#ifdef ISDN_DEBUG_NET_ICALL
				printk(KERN_DEBUG "n_fi: match3\n");
#endif
				/* matching interface found */

				/*
				 * Is the state STOPPED?
				 * If so, no dialin is allowed,
				 * so reject actively.
				 * */
				if (ISDN_NET_DIALMODE(*lp) == ISDN_NET_DM_OFF) {
					printk(KERN_INFO "incoming call, interface %s `stopped' -> rejected\n",
					       p->dev->name);
					return 3;
				}
				/*
				 * Is the interface up?
				 * If not, reject the call actively.
				 */
				if (!isdn_net_device_started(p)) {
					printk(KERN_INFO "%s: incoming call, interface down -> rejected\n",
					       p->dev->name);
					return 3;
				}
				/* Interface is up, now see if it's a slave. If so, see if
				 * it's master and parent slave is online. If not, reject the call.
				 */
				if (lp->master) {
					isdn_net_local *mlp = ISDN_MASTER_PRIV(lp);
					printk(KERN_DEBUG "ICALLslv: %s\n", p->dev->name);
					printk(KERN_DEBUG "master=%s\n", lp->master->name);
					if (mlp->flags & ISDN_NET_CONNECTED) {
						printk(KERN_DEBUG "master online\n");
						/* Master is online, find parent-slave (master if first slave) */
						while (mlp->slave) {
							if (ISDN_SLAVE_PRIV(mlp) == lp)
								break;
							mlp = ISDN_SLAVE_PRIV(mlp);
						}
					} else
						printk(KERN_DEBUG "master offline\n");
					/* Found parent, if it's offline iterate next device */
					printk(KERN_DEBUG "mlpf: %d\n", mlp->flags & ISDN_NET_CONNECTED);
					if (!(mlp->flags & ISDN_NET_CONNECTED)) {
						p = (isdn_net_dev *) p->next;
						continue;
					}
				}
				if (lp->flags & ISDN_NET_CALLBACK) {
					int chi;
					/*
					 * Is the state MANUAL?
					 * If so, no callback can be made,
					 * so reject actively.
					 * */
					if (ISDN_NET_DIALMODE(*lp) == ISDN_NET_DM_OFF) {
						printk(KERN_INFO "incoming call for callback, interface %s `off' -> rejected\n",
						       p->dev->name);
						return 3;
					}
					printk(KERN_DEBUG "%s: call from %s -> %s, start callback\n",
					       p->dev->name, nr, eaz);
					if (lp->phone[1]) {
						/* Grab a free ISDN-Channel */
						spin_lock_irqsave(&dev->lock, flags);
						if ((chi =
						     isdn_get_free_channel(
							     ISDN_USAGE_NET,
							     lp->l2_proto,
							     lp->l3_proto,
							     lp->pre_device,
							     lp->pre_channel,
							     lp->msn)
							    ) < 0) {

							printk(KERN_WARNING "isdn_net_find_icall: No channel for %s\n",
							       p->dev->name);
							spin_unlock_irqrestore(&dev->lock, flags);
							return 0;
						}
						/* Setup dialstate. */
						lp->dtimer = 0;
						lp->dialstate = 11;
						/* Connect interface with channel */
						isdn_net_bind_channel(lp, chi);
#ifdef CONFIG_ISDN_PPP
						if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
							if (isdn_ppp_bind(lp) < 0) {
								spin_unlock_irqrestore(&dev->lock, flags);
								isdn_net_unbind_channel(lp);
								return 0;
							}
#endif
						spin_unlock_irqrestore(&dev->lock, flags);
						/* Initiate dialing by returning 2 or 4 */
						return (lp->flags & ISDN_NET_CBHUP) ? 2 : 4;
					} else
						printk(KERN_WARNING "isdn_net: %s: No phone number\n",
						       p->dev->name);
					return 0;
				} else {
					printk(KERN_DEBUG "%s: call from %s -> %s accepted\n",
					       p->dev->name, nr, eaz);
					/* if this interface is dialing, it does it probably on a different
					   device, so free this device */
					if ((lp->dialstate == 4) || (lp->dialstate == 12)) {
#ifdef CONFIG_ISDN_PPP
						if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
							isdn_ppp_free(lp);
#endif
						isdn_net_lp_disconnected(lp);
						isdn_free_channel(lp->isdn_device, lp->isdn_channel,
								  ISDN_USAGE_NET);
					}
					spin_lock_irqsave(&dev->lock, flags);
					dev->usage[idx] &= ISDN_USAGE_EXCLUSIVE;
					dev->usage[idx] |= ISDN_USAGE_NET;
					strcpy(dev->num[idx], nr);
					isdn_info_update();
					dev->st_netdev[idx] = lp->netdev;
					lp->isdn_device = di;
					lp->isdn_channel = ch;
					lp->ppp_slot = -1;
					lp->flags |= ISDN_NET_CONNECTED;
					lp->dialstate = 7;
					lp->dtimer = 0;
					lp->outgoing = 0;
					lp->huptimer = 0;
					lp->hupflags |= ISDN_WAITCHARGE;
					lp->hupflags &= ~ISDN_HAVECHARGE;
#ifdef CONFIG_ISDN_PPP
					if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP) {
						if (isdn_ppp_bind(lp) < 0) {
							isdn_net_unbind_channel(lp);
							spin_unlock_irqrestore(&dev->lock, flags);
							return 0;
						}
					}
#endif
					spin_unlock_irqrestore(&dev->lock, flags);
					return 1;
				}
			}
		}
		p = (isdn_net_dev *) p->next;
	}
	/* If none of configured EAZ/MSN matched and not verbose, be silent */
	if (!ematch || dev->net_verbose)
		printk(KERN_INFO "isdn_net: call from %s -> %d %s ignored\n", nr, di, eaz);
	return (wret == 2) ? 5 : 0;
}

/*
 * Search list of net-interfaces for an interface with given name.
 */
isdn_net_dev *
isdn_net_findif(char *name)
{
	isdn_net_dev *p = dev->netdev;

	while (p) {
		if (!strcmp(p->dev->name, name))
			return p;
		p = (isdn_net_dev *) p->next;
	}
	return (isdn_net_dev *) NULL;
}

/*
 * Force a net-interface to dial out.
 * This is called from the userlevel-routine below or
 * from isdn_net_start_xmit().
 */
static int
isdn_net_force_dial_lp(isdn_net_local *lp)
{
	if ((!(lp->flags & ISDN_NET_CONNECTED)) && !lp->dialstate) {
		int chi;
		if (lp->phone[1]) {
			ulong flags;

			/* Grab a free ISDN-Channel */
			spin_lock_irqsave(&dev->lock, flags);
			if ((chi = isdn_get_free_channel(
				     ISDN_USAGE_NET,
				     lp->l2_proto,
				     lp->l3_proto,
				     lp->pre_device,
				     lp->pre_channel,
				     lp->msn)) < 0) {
				printk(KERN_WARNING "isdn_net_force_dial: No channel for %s\n",
				       lp->netdev->dev->name);
				spin_unlock_irqrestore(&dev->lock, flags);
				return -EAGAIN;
			}
			lp->dialstate = 1;
			/* Connect interface with channel */
			isdn_net_bind_channel(lp, chi);
#ifdef CONFIG_ISDN_PPP
			if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
				if (isdn_ppp_bind(lp) < 0) {
					isdn_net_unbind_channel(lp);
					spin_unlock_irqrestore(&dev->lock, flags);
					return -EAGAIN;
				}
#endif
			/* Initiate dialing */
			spin_unlock_irqrestore(&dev->lock, flags);
			isdn_net_dial();
			return 0;
		} else
			return -EINVAL;
	} else
		return -EBUSY;
}

/*
 * This is called from certain upper protocol layers (multilink ppp
 * and x25iface encapsulation module) that want to initiate dialing
 * themselves.
 */
int
isdn_net_dial_req(isdn_net_local *lp)
{
	/* is there a better error code? */
	if (!(ISDN_NET_DIALMODE(*lp) == ISDN_NET_DM_AUTO)) return -EBUSY;

	return isdn_net_force_dial_lp(lp);
}

/*
 * Force a net-interface to dial out.
 * This is always called from within userspace (ISDN_IOCTL_NET_DIAL).
 */
int
isdn_net_force_dial(char *name)
{
	isdn_net_dev *p = isdn_net_findif(name);

	if (!p)
		return -ENODEV;
	return (isdn_net_force_dial_lp(p->local));
}

/* The ISDN-specific entries in the device structure. */
static const struct net_device_ops isdn_netdev_ops = {
	.ndo_init	      = isdn_net_init,
	.ndo_open	      = isdn_net_open,
	.ndo_stop	      = isdn_net_close,
	.ndo_do_ioctl	      = isdn_net_ioctl,

	.ndo_start_xmit	      = isdn_net_start_xmit,
	.ndo_get_stats	      = isdn_net_get_stats,
	.ndo_tx_timeout	      = isdn_net_tx_timeout,
};

/*
 * Helper for alloc_netdev()
 */
static void _isdn_setup(struct net_device *dev)
{
	isdn_net_local *lp = netdev_priv(dev);

	ether_setup(dev);

	/* Setup the generic properties */
	dev->flags = IFF_NOARP | IFF_POINTOPOINT;

	/* isdn prepends a header in the tx path, can't share skbs */
	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	dev->header_ops = NULL;
	dev->netdev_ops = &isdn_netdev_ops;

	/* for clients with MPPP maybe higher values better */
	dev->tx_queue_len = 30;

	lp->p_encap = ISDN_NET_ENCAP_RAWIP;
	lp->magic = ISDN_NET_MAGIC;
	lp->last = lp;
	lp->next = lp;
	lp->isdn_device = -1;
	lp->isdn_channel = -1;
	lp->pre_device = -1;
	lp->pre_channel = -1;
	lp->exclusive = -1;
	lp->ppp_slot = -1;
	lp->pppbind = -1;
	skb_queue_head_init(&lp->super_tx_queue);
	lp->l2_proto = ISDN_PROTO_L2_X75I;
	lp->l3_proto = ISDN_PROTO_L3_TRANS;
	lp->triggercps = 6000;
	lp->slavedelay = 10 * HZ;
	lp->hupflags = ISDN_INHUP;	/* Do hangup even on incoming calls */
	lp->onhtime = 10;	/* Default hangup-time for saving costs */
	lp->dialmax = 1;
	/* Hangup before Callback, manual dial */
	lp->flags = ISDN_NET_CBHUP | ISDN_NET_DM_MANUAL;
	lp->cbdelay = 25;	/* Wait 5 secs before Callback */
	lp->dialtimeout = -1;  /* Infinite Dial-Timeout */
	lp->dialwait = 5 * HZ; /* Wait 5 sec. after failed dial */
	lp->dialstarted = 0;   /* Jiffies of last dial-start */
	lp->dialwait_timer = 0;  /* Jiffies of earliest next dial-start */
}

/*
 * Allocate a new network-interface and initialize its data structures.
 */
char *
isdn_net_new(char *name, struct net_device *master)
{
	isdn_net_dev *netdev;

	/* Avoid creating an existing interface */
	if (isdn_net_findif(name)) {
		printk(KERN_WARNING "isdn_net: interface %s already exists\n", name);
		return NULL;
	}
	if (name == NULL)
		return NULL;
	if (!(netdev = kzalloc(sizeof(isdn_net_dev), GFP_KERNEL))) {
		printk(KERN_WARNING "isdn_net: Could not allocate net-device\n");
		return NULL;
	}
	netdev->dev = alloc_netdev(sizeof(isdn_net_local), name,
				   NET_NAME_UNKNOWN, _isdn_setup);
	if (!netdev->dev) {
		printk(KERN_WARNING "isdn_net: Could not allocate network device\n");
		kfree(netdev);
		return NULL;
	}
	netdev->local = netdev_priv(netdev->dev);

	if (master) {
		/* Device shall be a slave */
		struct net_device *p = MASTER_TO_SLAVE(master);
		struct net_device *q = master;

		netdev->local->master = master;
		/* Put device at end of slave-chain */
		while (p) {
			q = p;
			p = MASTER_TO_SLAVE(p);
		}
		MASTER_TO_SLAVE(q) = netdev->dev;
	} else {
		/* Device shall be a master */
		/*
		 * Watchdog timer (currently) for master only.
		 */
		netdev->dev->watchdog_timeo = ISDN_NET_TX_TIMEOUT;
		if (register_netdev(netdev->dev) != 0) {
			printk(KERN_WARNING "isdn_net: Could not register net-device\n");
			free_netdev(netdev->dev);
			kfree(netdev);
			return NULL;
		}
	}
	netdev->queue = netdev->local;
	spin_lock_init(&netdev->queue_lock);

	netdev->local->netdev = netdev;

	INIT_WORK(&netdev->local->tqueue, isdn_net_softint);
	spin_lock_init(&netdev->local->xmit_lock);

	/* Put into to netdev-chain */
	netdev->next = (void *) dev->netdev;
	dev->netdev = netdev;
	return netdev->dev->name;
}

char *
isdn_net_newslave(char *parm)
{
	char *p = strchr(parm, ',');
	isdn_net_dev *n;
	char newname[10];

	if (p) {
		/* Slave-Name MUST not be empty or overflow 'newname' */
		if (strscpy(newname, p + 1, sizeof(newname)) <= 0)
			return NULL;
		*p = 0;
		/* Master must already exist */
		if (!(n = isdn_net_findif(parm)))
			return NULL;
		/* Master must be a real interface, not a slave */
		if (n->local->master)
			return NULL;
		/* Master must not be started yet */
		if (isdn_net_device_started(n))
			return NULL;
		return (isdn_net_new(newname, n->dev));
	}
	return NULL;
}

/*
 * Set interface-parameters.
 * Always set all parameters, so the user-level application is responsible
 * for not overwriting existing setups. It has to get the current
 * setup first, if only selected parameters are to be changed.
 */
int
isdn_net_setcfg(isdn_net_ioctl_cfg *cfg)
{
	isdn_net_dev *p = isdn_net_findif(cfg->name);
	ulong features;
	int i;
	int drvidx;
	int chidx;
	char drvid[25];

	if (p) {
		isdn_net_local *lp = p->local;

		/* See if any registered driver supports the features we want */
		features = ((1 << cfg->l2_proto) << ISDN_FEATURE_L2_SHIFT) |
			((1 << cfg->l3_proto) << ISDN_FEATURE_L3_SHIFT);
		for (i = 0; i < ISDN_MAX_DRIVERS; i++)
			if (dev->drv[i])
				if ((dev->drv[i]->interface->features & features) == features)
					break;
		if (i == ISDN_MAX_DRIVERS) {
			printk(KERN_WARNING "isdn_net: No driver with selected features\n");
			return -ENODEV;
		}
		if (lp->p_encap != cfg->p_encap) {
#ifdef CONFIG_ISDN_X25
			struct concap_proto *cprot = p->cprot;
#endif
			if (isdn_net_device_started(p)) {
				printk(KERN_WARNING "%s: cannot change encap when if is up\n",
				       p->dev->name);
				return -EBUSY;
			}
#ifdef CONFIG_ISDN_X25
			if (cprot && cprot->pops)
				cprot->pops->proto_del(cprot);
			p->cprot = NULL;
			lp->dops = NULL;
			/* ... ,  prepare for configuration of new one ... */
			switch (cfg->p_encap) {
			case ISDN_NET_ENCAP_X25IFACE:
				lp->dops = &isdn_concap_reliable_dl_dops;
			}
			/* ... and allocate new one ... */
			p->cprot = isdn_concap_new(cfg->p_encap);
			/* p -> cprot == NULL now if p_encap is not supported
			   by means of the concap_proto mechanism */
			/* the protocol is not configured yet; this will
			   happen later when isdn_net_reset() is called */
#endif
		}
		switch (cfg->p_encap) {
		case ISDN_NET_ENCAP_SYNCPPP:
#ifndef CONFIG_ISDN_PPP
			printk(KERN_WARNING "%s: SyncPPP support not configured\n",
			       p->dev->name);
			return -EINVAL;
#else
			p->dev->type = ARPHRD_PPP;	/* change ARP type */
			p->dev->addr_len = 0;
#endif
			break;
		case ISDN_NET_ENCAP_X25IFACE:
#ifndef CONFIG_ISDN_X25
			printk(KERN_WARNING "%s: isdn-x25 support not configured\n",
			       p->dev->name);
			return -EINVAL;
#else
			p->dev->type = ARPHRD_X25;	/* change ARP type */
			p->dev->addr_len = 0;
#endif
			break;
		case ISDN_NET_ENCAP_CISCOHDLCK:
			break;
		default:
			if (cfg->p_encap >= 0 &&
			    cfg->p_encap <= ISDN_NET_ENCAP_MAX_ENCAP)
				break;
			printk(KERN_WARNING
			       "%s: encapsulation protocol %d not supported\n",
			       p->dev->name, cfg->p_encap);
			return -EINVAL;
		}
		if (strlen(cfg->drvid)) {
			/* A bind has been requested ... */
			char *c,
				*e;

			if (strnlen(cfg->drvid, sizeof(cfg->drvid)) ==
			    sizeof(cfg->drvid))
				return -EINVAL;
			drvidx = -1;
			chidx = -1;
			strcpy(drvid, cfg->drvid);
			if ((c = strchr(drvid, ','))) {
				/* The channel-number is appended to the driver-Id with a comma */
				chidx = (int) simple_strtoul(c + 1, &e, 10);
				if (e == c)
					chidx = -1;
				*c = '\0';
			}
			for (i = 0; i < ISDN_MAX_DRIVERS; i++)
				/* Lookup driver-Id in array */
				if (!(strcmp(dev->drvid[i], drvid))) {
					drvidx = i;
					break;
				}
			if ((drvidx == -1) || (chidx == -1))
				/* Either driver-Id or channel-number invalid */
				return -ENODEV;
		} else {
			/* Parameters are valid, so get them */
			drvidx = lp->pre_device;
			chidx = lp->pre_channel;
		}
		if (cfg->exclusive > 0) {
			unsigned long flags;

			/* If binding is exclusive, try to grab the channel */
			spin_lock_irqsave(&dev->lock, flags);
			if ((i = isdn_get_free_channel(ISDN_USAGE_NET,
						       lp->l2_proto, lp->l3_proto, drvidx,
						       chidx, lp->msn)) < 0) {
				/* Grab failed, because desired channel is in use */
				lp->exclusive = -1;
				spin_unlock_irqrestore(&dev->lock, flags);
				return -EBUSY;
			}
			/* All went ok, so update isdninfo */
			dev->usage[i] = ISDN_USAGE_EXCLUSIVE;
			isdn_info_update();
			spin_unlock_irqrestore(&dev->lock, flags);
			lp->exclusive = i;
		} else {
			/* Non-exclusive binding or unbind. */
			lp->exclusive = -1;
			if ((lp->pre_device != -1) && (cfg->exclusive == -1)) {
				isdn_unexclusive_channel(lp->pre_device, lp->pre_channel);
				isdn_free_channel(lp->pre_device, lp->pre_channel, ISDN_USAGE_NET);
				drvidx = -1;
				chidx = -1;
			}
		}
		strlcpy(lp->msn, cfg->eaz, sizeof(lp->msn));
		lp->pre_device = drvidx;
		lp->pre_channel = chidx;
		lp->onhtime = cfg->onhtime;
		lp->charge = cfg->charge;
		lp->l2_proto = cfg->l2_proto;
		lp->l3_proto = cfg->l3_proto;
		lp->cbdelay = cfg->cbdelay;
		lp->dialmax = cfg->dialmax;
		lp->triggercps = cfg->triggercps;
		lp->slavedelay = cfg->slavedelay * HZ;
		lp->pppbind = cfg->pppbind;
		lp->dialtimeout = cfg->dialtimeout >= 0 ? cfg->dialtimeout * HZ : -1;
		lp->dialwait = cfg->dialwait * HZ;
		if (cfg->secure)
			lp->flags |= ISDN_NET_SECURE;
		else
			lp->flags &= ~ISDN_NET_SECURE;
		if (cfg->cbhup)
			lp->flags |= ISDN_NET_CBHUP;
		else
			lp->flags &= ~ISDN_NET_CBHUP;
		switch (cfg->callback) {
		case 0:
			lp->flags &= ~(ISDN_NET_CALLBACK | ISDN_NET_CBOUT);
			break;
		case 1:
			lp->flags |= ISDN_NET_CALLBACK;
			lp->flags &= ~ISDN_NET_CBOUT;
			break;
		case 2:
			lp->flags |= ISDN_NET_CBOUT;
			lp->flags &= ~ISDN_NET_CALLBACK;
			break;
		}
		lp->flags &= ~ISDN_NET_DIALMODE_MASK;	/* first all bits off */
		if (cfg->dialmode && !(cfg->dialmode & ISDN_NET_DIALMODE_MASK)) {
			/* old isdnctrl version, where only 0 or 1 is given */
			printk(KERN_WARNING
			       "Old isdnctrl version detected! Please update.\n");
			lp->flags |= ISDN_NET_DM_OFF; /* turn on `off' bit */
		}
		else {
			lp->flags |= cfg->dialmode;  /* turn on selected bits */
		}
		if (cfg->chargehup)
			lp->hupflags |= ISDN_CHARGEHUP;
		else
			lp->hupflags &= ~ISDN_CHARGEHUP;
		if (cfg->ihup)
			lp->hupflags |= ISDN_INHUP;
		else
			lp->hupflags &= ~ISDN_INHUP;
		if (cfg->chargeint > 10) {
			lp->hupflags |= ISDN_CHARGEHUP | ISDN_HAVECHARGE | ISDN_MANCHARGE;
			lp->chargeint = cfg->chargeint * HZ;
		}
		if (cfg->p_encap != lp->p_encap) {
			if (cfg->p_encap == ISDN_NET_ENCAP_RAWIP) {
				p->dev->header_ops = NULL;
				p->dev->flags = IFF_NOARP | IFF_POINTOPOINT;
			} else {
				p->dev->header_ops = &isdn_header_ops;
				if (cfg->p_encap == ISDN_NET_ENCAP_ETHER)
					p->dev->flags = IFF_BROADCAST | IFF_MULTICAST;
				else
					p->dev->flags = IFF_NOARP | IFF_POINTOPOINT;
			}
		}
		lp->p_encap = cfg->p_encap;
		return 0;
	}
	return -ENODEV;
}

/*
 * Perform get-interface-parameters.ioctl
 */
int
isdn_net_getcfg(isdn_net_ioctl_cfg *cfg)
{
	isdn_net_dev *p = isdn_net_findif(cfg->name);

	if (p) {
		isdn_net_local *lp = p->local;

		strcpy(cfg->eaz, lp->msn);
		cfg->exclusive = lp->exclusive;
		if (lp->pre_device >= 0) {
			sprintf(cfg->drvid, "%s,%d", dev->drvid[lp->pre_device],
				lp->pre_channel);
		} else
			cfg->drvid[0] = '\0';
		cfg->onhtime = lp->onhtime;
		cfg->charge = lp->charge;
		cfg->l2_proto = lp->l2_proto;
		cfg->l3_proto = lp->l3_proto;
		cfg->p_encap = lp->p_encap;
		cfg->secure = (lp->flags & ISDN_NET_SECURE) ? 1 : 0;
		cfg->callback = 0;
		if (lp->flags & ISDN_NET_CALLBACK)
			cfg->callback = 1;
		if (lp->flags & ISDN_NET_CBOUT)
			cfg->callback = 2;
		cfg->cbhup = (lp->flags & ISDN_NET_CBHUP) ? 1 : 0;
		cfg->dialmode = lp->flags & ISDN_NET_DIALMODE_MASK;
		cfg->chargehup = (lp->hupflags & ISDN_CHARGEHUP) ? 1 : 0;
		cfg->ihup = (lp->hupflags & ISDN_INHUP) ? 1 : 0;
		cfg->cbdelay = lp->cbdelay;
		cfg->dialmax = lp->dialmax;
		cfg->triggercps = lp->triggercps;
		cfg->slavedelay = lp->slavedelay / HZ;
		cfg->chargeint = (lp->hupflags & ISDN_CHARGEHUP) ?
			(lp->chargeint / HZ) : 0;
		cfg->pppbind = lp->pppbind;
		cfg->dialtimeout = lp->dialtimeout >= 0 ? lp->dialtimeout / HZ : -1;
		cfg->dialwait = lp->dialwait / HZ;
		if (lp->slave) {
			if (strlen(lp->slave->name) >= 10)
				strcpy(cfg->slave, "too-long");
			else
				strcpy(cfg->slave, lp->slave->name);
		} else
			cfg->slave[0] = '\0';
		if (lp->master) {
			if (strlen(lp->master->name) >= 10)
				strcpy(cfg->master, "too-long");
			else
				strcpy(cfg->master, lp->master->name);
		} else
			cfg->master[0] = '\0';
		return 0;
	}
	return -ENODEV;
}

/*
 * Add a phone-number to an interface.
 */
int
isdn_net_addphone(isdn_net_ioctl_phone *phone)
{
	isdn_net_dev *p = isdn_net_findif(phone->name);
	isdn_net_phone *n;

	if (p) {
		if (!(n = kmalloc(sizeof(isdn_net_phone), GFP_KERNEL)))
			return -ENOMEM;
		strlcpy(n->num, phone->phone, sizeof(n->num));
		n->next = p->local->phone[phone->outgoing & 1];
		p->local->phone[phone->outgoing & 1] = n;
		return 0;
	}
	return -ENODEV;
}

/*
 * Copy a string of all phone-numbers of an interface to user space.
 * This might sleep and must be called with the isdn semaphore down.
 */
int
isdn_net_getphones(isdn_net_ioctl_phone *phone, char __user *phones)
{
	isdn_net_dev *p = isdn_net_findif(phone->name);
	int inout = phone->outgoing & 1;
	int more = 0;
	int count = 0;
	isdn_net_phone *n;

	if (!p)
		return -ENODEV;
	inout &= 1;
	for (n = p->local->phone[inout]; n; n = n->next) {
		if (more) {
			put_user(' ', phones++);
			count++;
		}
		if (copy_to_user(phones, n->num, strlen(n->num) + 1)) {
			return -EFAULT;
		}
		phones += strlen(n->num);
		count += strlen(n->num);
		more = 1;
	}
	put_user(0, phones);
	count++;
	return count;
}

/*
 * Copy a string containing the peer's phone number of a connected interface
 * to user space.
 */
int
isdn_net_getpeer(isdn_net_ioctl_phone *phone, isdn_net_ioctl_phone __user *peer)
{
	isdn_net_dev *p = isdn_net_findif(phone->name);
	int ch, dv, idx;

	if (!p)
		return -ENODEV;
	/*
	 * Theoretical race: while this executes, the remote number might
	 * become invalid (hang up) or change (new connection), resulting
	 * in (partially) wrong number copied to user. This race
	 * currently ignored.
	 */
	ch = p->local->isdn_channel;
	dv = p->local->isdn_device;
	if (ch < 0 && dv < 0)
		return -ENOTCONN;
	idx = isdn_dc2minor(dv, ch);
	if (idx < 0)
		return -ENODEV;
	/* for pre-bound channels, we need this extra check */
	if (strncmp(dev->num[idx], "???", 3) == 0)
		return -ENOTCONN;
	strncpy(phone->phone, dev->num[idx], ISDN_MSNLEN);
	phone->outgoing = USG_OUTGOING(dev->usage[idx]);
	if (copy_to_user(peer, phone, sizeof(*peer)))
		return -EFAULT;
	return 0;
}
/*
 * Delete a phone-number from an interface.
 */
int
isdn_net_delphone(isdn_net_ioctl_phone *phone)
{
	isdn_net_dev *p = isdn_net_findif(phone->name);
	int inout = phone->outgoing & 1;
	isdn_net_phone *n;
	isdn_net_phone *m;

	if (p) {
		n = p->local->phone[inout];
		m = NULL;
		while (n) {
			if (!strcmp(n->num, phone->phone)) {
				if (p->local->dial == n)
					p->local->dial = n->next;
				if (m)
					m->next = n->next;
				else
					p->local->phone[inout] = n->next;
				kfree(n);
				return 0;
			}
			m = n;
			n = (isdn_net_phone *) n->next;
		}
		return -EINVAL;
	}
	return -ENODEV;
}

/*
 * Delete all phone-numbers of an interface.
 */
static int
isdn_net_rmallphone(isdn_net_dev *p)
{
	isdn_net_phone *n;
	isdn_net_phone *m;
	int i;

	for (i = 0; i < 2; i++) {
		n = p->local->phone[i];
		while (n) {
			m = n->next;
			kfree(n);
			n = m;
		}
		p->local->phone[i] = NULL;
	}
	p->local->dial = NULL;
	return 0;
}

/*
 * Force a hangup of a network-interface.
 */
int
isdn_net_force_hangup(char *name)
{
	isdn_net_dev *p = isdn_net_findif(name);
	struct net_device *q;

	if (p) {
		if (p->local->isdn_device < 0)
			return 1;
		q = p->local->slave;
		/* If this interface has slaves, do a hangup for them also. */
		while (q) {
			isdn_net_hangup(q);
			q = MASTER_TO_SLAVE(q);
		}
		isdn_net_hangup(p->dev);
		return 0;
	}
	return -ENODEV;
}

/*
 * Helper-function for isdn_net_rm: Do the real work.
 */
static int
isdn_net_realrm(isdn_net_dev *p, isdn_net_dev *q)
{
	u_long flags;

	if (isdn_net_device_started(p)) {
		return -EBUSY;
	}
#ifdef CONFIG_ISDN_X25
	if (p->cprot && p->cprot->pops)
		p->cprot->pops->proto_del(p->cprot);
#endif
	/* Free all phone-entries */
	isdn_net_rmallphone(p);
	/* If interface is bound exclusive, free channel-usage */
	if (p->local->exclusive != -1)
		isdn_unexclusive_channel(p->local->pre_device, p->local->pre_channel);
	if (p->local->master) {
		/* It's a slave-device, so update master's slave-pointer if necessary */
		if (((isdn_net_local *) ISDN_MASTER_PRIV(p->local))->slave ==
		    p->dev)
			((isdn_net_local *)ISDN_MASTER_PRIV(p->local))->slave =
				p->local->slave;
	} else {
		/* Unregister only if it's a master-device */
		unregister_netdev(p->dev);
	}
	/* Unlink device from chain */
	spin_lock_irqsave(&dev->lock, flags);
	if (q)
		q->next = p->next;
	else
		dev->netdev = p->next;
	if (p->local->slave) {
		/* If this interface has a slave, remove it also */
		char *slavename = p->local->slave->name;
		isdn_net_dev *n = dev->netdev;
		q = NULL;
		while (n) {
			if (!strcmp(n->dev->name, slavename)) {
				spin_unlock_irqrestore(&dev->lock, flags);
				isdn_net_realrm(n, q);
				spin_lock_irqsave(&dev->lock, flags);
				break;
			}
			q = n;
			n = (isdn_net_dev *)n->next;
		}
	}
	spin_unlock_irqrestore(&dev->lock, flags);
	/* If no more net-devices remain, disable auto-hangup timer */
	if (dev->netdev == NULL)
		isdn_timer_ctrl(ISDN_TIMER_NETHANGUP, 0);
	free_netdev(p->dev);
	kfree(p);

	return 0;
}

/*
 * Remove a single network-interface.
 */
int
isdn_net_rm(char *name)
{
	u_long flags;
	isdn_net_dev *p;
	isdn_net_dev *q;

	/* Search name in netdev-chain */
	spin_lock_irqsave(&dev->lock, flags);
	p = dev->netdev;
	q = NULL;
	while (p) {
		if (!strcmp(p->dev->name, name)) {
			spin_unlock_irqrestore(&dev->lock, flags);
			return (isdn_net_realrm(p, q));
		}
		q = p;
		p = (isdn_net_dev *) p->next;
	}
	spin_unlock_irqrestore(&dev->lock, flags);
	/* If no more net-devices remain, disable auto-hangup timer */
	if (dev->netdev == NULL)
		isdn_timer_ctrl(ISDN_TIMER_NETHANGUP, 0);
	return -ENODEV;
}

/*
 * Remove all network-interfaces
 */
int
isdn_net_rmall(void)
{
	u_long flags;
	int ret;

	/* Walk through netdev-chain */
	spin_lock_irqsave(&dev->lock, flags);
	while (dev->netdev) {
		if (!dev->netdev->local->master) {
			/* Remove master-devices only, slaves get removed with their master */
			spin_unlock_irqrestore(&dev->lock, flags);
			if ((ret = isdn_net_realrm(dev->netdev, NULL))) {
				return ret;
			}
			spin_lock_irqsave(&dev->lock, flags);
		}
	}
	dev->netdev = NULL;
	spin_unlock_irqrestore(&dev->lock, flags);
	return 0;
}
