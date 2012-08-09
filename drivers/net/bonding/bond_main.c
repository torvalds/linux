/*
 * originally based on the dummy device.
 *
 * Copyright 1999, Thomas Davis, tadavis@lbl.gov.
 * Licensed under the GPL. Based on dummy.c, and eql.c devices.
 *
 * bonding.c: an Ethernet Bonding driver
 *
 * This is useful to talk to a Cisco EtherChannel compatible equipment:
 *	Cisco 5500
 *	Sun Trunking (Solaris)
 *	Alteon AceDirector Trunks
 *	Linux Bonding
 *	and probably many L2 switches ...
 *
 * How it works:
 *    ifconfig bond0 ipaddress netmask up
 *      will setup a network device, with an ip address.  No mac address
 *	will be assigned at this time.  The hw mac address will come from
 *	the first slave bonded to the channel.  All slaves will then use
 *	this hw mac address.
 *
 *    ifconfig bond0 down
 *         will release all slaves, marking them as down.
 *
 *    ifenslave bond0 eth0
 *	will attach eth0 to bond0 as a slave.  eth0 hw mac address will either
 *	a: be used as initial mac address
 *	b: if a hw mac address already is there, eth0's hw mac address
 *	   will then be set from bond0.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <net/ip.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/socket.h>
#include <linux/ctype.h>
#include <linux/inet.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <asm/dma.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/rtnetlink.h>
#include <linux/smp.h>
#include <linux/if_ether.h>
#include <net/arp.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/if_bonding.h>
#include <linux/jiffies.h>
#include <linux/preempt.h>
#include <net/route.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/pkt_sched.h>
#include "bonding.h"
#include "bond_3ad.h"
#include "bond_alb.h"

/*---------------------------- Module parameters ----------------------------*/

/* monitor all links that often (in milliseconds). <=0 disables monitoring */
#define BOND_LINK_MON_INTERV	0
#define BOND_LINK_ARP_INTERV	0

static int max_bonds	= BOND_DEFAULT_MAX_BONDS;
static int tx_queues	= BOND_DEFAULT_TX_QUEUES;
static int num_peer_notif = 1;
static int miimon	= BOND_LINK_MON_INTERV;
static int updelay;
static int downdelay;
static int use_carrier	= 1;
static char *mode;
static char *primary;
static char *primary_reselect;
static char *lacp_rate;
static int min_links;
static char *ad_select;
static char *xmit_hash_policy;
static int arp_interval = BOND_LINK_ARP_INTERV;
static char *arp_ip_target[BOND_MAX_ARP_TARGETS];
static char *arp_validate;
static char *fail_over_mac;
static int all_slaves_active = 0;
static struct bond_params bonding_defaults;
static int resend_igmp = BOND_DEFAULT_RESEND_IGMP;

module_param(max_bonds, int, 0);
MODULE_PARM_DESC(max_bonds, "Max number of bonded devices");
module_param(tx_queues, int, 0);
MODULE_PARM_DESC(tx_queues, "Max number of transmit queues (default = 16)");
module_param_named(num_grat_arp, num_peer_notif, int, 0644);
MODULE_PARM_DESC(num_grat_arp, "Number of peer notifications to send on "
			       "failover event (alias of num_unsol_na)");
module_param_named(num_unsol_na, num_peer_notif, int, 0644);
MODULE_PARM_DESC(num_unsol_na, "Number of peer notifications to send on "
			       "failover event (alias of num_grat_arp)");
module_param(miimon, int, 0);
MODULE_PARM_DESC(miimon, "Link check interval in milliseconds");
module_param(updelay, int, 0);
MODULE_PARM_DESC(updelay, "Delay before considering link up, in milliseconds");
module_param(downdelay, int, 0);
MODULE_PARM_DESC(downdelay, "Delay before considering link down, "
			    "in milliseconds");
module_param(use_carrier, int, 0);
MODULE_PARM_DESC(use_carrier, "Use netif_carrier_ok (vs MII ioctls) in miimon; "
			      "0 for off, 1 for on (default)");
module_param(mode, charp, 0);
MODULE_PARM_DESC(mode, "Mode of operation; 0 for balance-rr, "
		       "1 for active-backup, 2 for balance-xor, "
		       "3 for broadcast, 4 for 802.3ad, 5 for balance-tlb, "
		       "6 for balance-alb");
module_param(primary, charp, 0);
MODULE_PARM_DESC(primary, "Primary network device to use");
module_param(primary_reselect, charp, 0);
MODULE_PARM_DESC(primary_reselect, "Reselect primary slave "
				   "once it comes up; "
				   "0 for always (default), "
				   "1 for only if speed of primary is "
				   "better, "
				   "2 for only on active slave "
				   "failure");
module_param(lacp_rate, charp, 0);
MODULE_PARM_DESC(lacp_rate, "LACPDU tx rate to request from 802.3ad partner; "
			    "0 for slow, 1 for fast");
module_param(ad_select, charp, 0);
MODULE_PARM_DESC(ad_select, "803.ad aggregation selection logic; "
			    "0 for stable (default), 1 for bandwidth, "
			    "2 for count");
module_param(min_links, int, 0);
MODULE_PARM_DESC(min_links, "Minimum number of available links before turning on carrier");

module_param(xmit_hash_policy, charp, 0);
MODULE_PARM_DESC(xmit_hash_policy, "balance-xor and 802.3ad hashing method; "
				   "0 for layer 2 (default), 1 for layer 3+4, "
				   "2 for layer 2+3");
module_param(arp_interval, int, 0);
MODULE_PARM_DESC(arp_interval, "arp interval in milliseconds");
module_param_array(arp_ip_target, charp, NULL, 0);
MODULE_PARM_DESC(arp_ip_target, "arp targets in n.n.n.n form");
module_param(arp_validate, charp, 0);
MODULE_PARM_DESC(arp_validate, "validate src/dst of ARP probes; "
			       "0 for none (default), 1 for active, "
			       "2 for backup, 3 for all");
module_param(fail_over_mac, charp, 0);
MODULE_PARM_DESC(fail_over_mac, "For active-backup, do not set all slaves to "
				"the same MAC; 0 for none (default), "
				"1 for active, 2 for follow");
module_param(all_slaves_active, int, 0);
MODULE_PARM_DESC(all_slaves_active, "Keep all frames received on an interface"
				     "by setting active flag for all slaves; "
				     "0 for never (default), 1 for always.");
module_param(resend_igmp, int, 0);
MODULE_PARM_DESC(resend_igmp, "Number of IGMP membership reports to send on "
			      "link failure");

/*----------------------------- Global variables ----------------------------*/

#ifdef CONFIG_NET_POLL_CONTROLLER
atomic_t netpoll_block_tx = ATOMIC_INIT(0);
#endif

int bond_net_id __read_mostly;

static __be32 arp_target[BOND_MAX_ARP_TARGETS];
static int arp_ip_count;
static int bond_mode	= BOND_MODE_ROUNDROBIN;
static int xmit_hashtype = BOND_XMIT_POLICY_LAYER2;
static int lacp_fast;

const struct bond_parm_tbl bond_lacp_tbl[] = {
{	"slow",		AD_LACP_SLOW},
{	"fast",		AD_LACP_FAST},
{	NULL,		-1},
};

const struct bond_parm_tbl bond_mode_tbl[] = {
{	"balance-rr",		BOND_MODE_ROUNDROBIN},
{	"active-backup",	BOND_MODE_ACTIVEBACKUP},
{	"balance-xor",		BOND_MODE_XOR},
{	"broadcast",		BOND_MODE_BROADCAST},
{	"802.3ad",		BOND_MODE_8023AD},
{	"balance-tlb",		BOND_MODE_TLB},
{	"balance-alb",		BOND_MODE_ALB},
{	NULL,			-1},
};

const struct bond_parm_tbl xmit_hashtype_tbl[] = {
{	"layer2",		BOND_XMIT_POLICY_LAYER2},
{	"layer3+4",		BOND_XMIT_POLICY_LAYER34},
{	"layer2+3",		BOND_XMIT_POLICY_LAYER23},
{	NULL,			-1},
};

const struct bond_parm_tbl arp_validate_tbl[] = {
{	"none",			BOND_ARP_VALIDATE_NONE},
{	"active",		BOND_ARP_VALIDATE_ACTIVE},
{	"backup",		BOND_ARP_VALIDATE_BACKUP},
{	"all",			BOND_ARP_VALIDATE_ALL},
{	NULL,			-1},
};

const struct bond_parm_tbl fail_over_mac_tbl[] = {
{	"none",			BOND_FOM_NONE},
{	"active",		BOND_FOM_ACTIVE},
{	"follow",		BOND_FOM_FOLLOW},
{	NULL,			-1},
};

const struct bond_parm_tbl pri_reselect_tbl[] = {
{	"always",		BOND_PRI_RESELECT_ALWAYS},
{	"better",		BOND_PRI_RESELECT_BETTER},
{	"failure",		BOND_PRI_RESELECT_FAILURE},
{	NULL,			-1},
};

struct bond_parm_tbl ad_select_tbl[] = {
{	"stable",	BOND_AD_STABLE},
{	"bandwidth",	BOND_AD_BANDWIDTH},
{	"count",	BOND_AD_COUNT},
{	NULL,		-1},
};

/*-------------------------- Forward declarations ---------------------------*/

static int bond_init(struct net_device *bond_dev);
static void bond_uninit(struct net_device *bond_dev);

/*---------------------------- General routines -----------------------------*/

const char *bond_mode_name(int mode)
{
	static const char *names[] = {
		[BOND_MODE_ROUNDROBIN] = "load balancing (round-robin)",
		[BOND_MODE_ACTIVEBACKUP] = "fault-tolerance (active-backup)",
		[BOND_MODE_XOR] = "load balancing (xor)",
		[BOND_MODE_BROADCAST] = "fault-tolerance (broadcast)",
		[BOND_MODE_8023AD] = "IEEE 802.3ad Dynamic link aggregation",
		[BOND_MODE_TLB] = "transmit load balancing",
		[BOND_MODE_ALB] = "adaptive load balancing",
	};

	if (mode < 0 || mode > BOND_MODE_ALB)
		return "unknown";

	return names[mode];
}

/*---------------------------------- VLAN -----------------------------------*/

/**
 * bond_add_vlan - add a new vlan id on bond
 * @bond: bond that got the notification
 * @vlan_id: the vlan id to add
 *
 * Returns -ENOMEM if allocation failed.
 */
static int bond_add_vlan(struct bonding *bond, unsigned short vlan_id)
{
	struct vlan_entry *vlan;

	pr_debug("bond: %s, vlan id %d\n",
		 (bond ? bond->dev->name : "None"), vlan_id);

	vlan = kzalloc(sizeof(struct vlan_entry), GFP_KERNEL);
	if (!vlan)
		return -ENOMEM;

	INIT_LIST_HEAD(&vlan->vlan_list);
	vlan->vlan_id = vlan_id;

	write_lock_bh(&bond->lock);

	list_add_tail(&vlan->vlan_list, &bond->vlan_list);

	write_unlock_bh(&bond->lock);

	pr_debug("added VLAN ID %d on bond %s\n", vlan_id, bond->dev->name);

	return 0;
}

/**
 * bond_del_vlan - delete a vlan id from bond
 * @bond: bond that got the notification
 * @vlan_id: the vlan id to delete
 *
 * returns -ENODEV if @vlan_id was not found in @bond.
 */
static int bond_del_vlan(struct bonding *bond, unsigned short vlan_id)
{
	struct vlan_entry *vlan;
	int res = -ENODEV;

	pr_debug("bond: %s, vlan id %d\n", bond->dev->name, vlan_id);

	block_netpoll_tx();
	write_lock_bh(&bond->lock);

	list_for_each_entry(vlan, &bond->vlan_list, vlan_list) {
		if (vlan->vlan_id == vlan_id) {
			list_del(&vlan->vlan_list);

			if (bond_is_lb(bond))
				bond_alb_clear_vlan(bond, vlan_id);

			pr_debug("removed VLAN ID %d from bond %s\n",
				 vlan_id, bond->dev->name);

			kfree(vlan);

			res = 0;
			goto out;
		}
	}

	pr_debug("couldn't find VLAN ID %d in bond %s\n",
		 vlan_id, bond->dev->name);

out:
	write_unlock_bh(&bond->lock);
	unblock_netpoll_tx();
	return res;
}

/**
 * bond_next_vlan - safely skip to the next item in the vlans list.
 * @bond: the bond we're working on
 * @curr: item we're advancing from
 *
 * Returns %NULL if list is empty, bond->next_vlan if @curr is %NULL,
 * or @curr->next otherwise (even if it is @curr itself again).
 *
 * Caller must hold bond->lock
 */
struct vlan_entry *bond_next_vlan(struct bonding *bond, struct vlan_entry *curr)
{
	struct vlan_entry *next, *last;

	if (list_empty(&bond->vlan_list))
		return NULL;

	if (!curr) {
		next = list_entry(bond->vlan_list.next,
				  struct vlan_entry, vlan_list);
	} else {
		last = list_entry(bond->vlan_list.prev,
				  struct vlan_entry, vlan_list);
		if (last == curr) {
			next = list_entry(bond->vlan_list.next,
					  struct vlan_entry, vlan_list);
		} else {
			next = list_entry(curr->vlan_list.next,
					  struct vlan_entry, vlan_list);
		}
	}

	return next;
}

/**
 * bond_dev_queue_xmit - Prepare skb for xmit.
 *
 * @bond: bond device that got this skb for tx.
 * @skb: hw accel VLAN tagged skb to transmit
 * @slave_dev: slave that is supposed to xmit this skbuff
 */
int bond_dev_queue_xmit(struct bonding *bond, struct sk_buff *skb,
			struct net_device *slave_dev)
{
	skb->dev = slave_dev;

	BUILD_BUG_ON(sizeof(skb->queue_mapping) !=
		     sizeof(qdisc_skb_cb(skb)->slave_dev_queue_mapping));
	skb->queue_mapping = qdisc_skb_cb(skb)->slave_dev_queue_mapping;

	if (unlikely(netpoll_tx_running(slave_dev)))
		bond_netpoll_send_skb(bond_get_slave_by_dev(bond, slave_dev), skb);
	else
		dev_queue_xmit(skb);

	return 0;
}

/*
 * In the following 2 functions, bond_vlan_rx_add_vid and bond_vlan_rx_kill_vid,
 * We don't protect the slave list iteration with a lock because:
 * a. This operation is performed in IOCTL context,
 * b. The operation is protected by the RTNL semaphore in the 8021q code,
 * c. Holding a lock with BH disabled while directly calling a base driver
 *    entry point is generally a BAD idea.
 *
 * The design of synchronization/protection for this operation in the 8021q
 * module is good for one or more VLAN devices over a single physical device
 * and cannot be extended for a teaming solution like bonding, so there is a
 * potential race condition here where a net device from the vlan group might
 * be referenced (either by a base driver or the 8021q code) while it is being
 * removed from the system. However, it turns out we're not making matters
 * worse, and if it works for regular VLAN usage it will work here too.
*/

/**
 * bond_vlan_rx_add_vid - Propagates adding an id to slaves
 * @bond_dev: bonding net device that got called
 * @vid: vlan id being added
 */
static int bond_vlan_rx_add_vid(struct net_device *bond_dev, uint16_t vid)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave, *stop_at;
	int i, res;

	bond_for_each_slave(bond, slave, i) {
		res = vlan_vid_add(slave->dev, vid);
		if (res)
			goto unwind;
	}

	res = bond_add_vlan(bond, vid);
	if (res) {
		pr_err("%s: Error: Failed to add vlan id %d\n",
		       bond_dev->name, vid);
		return res;
	}

	return 0;

unwind:
	/* unwind from head to the slave that failed */
	stop_at = slave;
	bond_for_each_slave_from_to(bond, slave, i, bond->first_slave, stop_at)
		vlan_vid_del(slave->dev, vid);

	return res;
}

/**
 * bond_vlan_rx_kill_vid - Propagates deleting an id to slaves
 * @bond_dev: bonding net device that got called
 * @vid: vlan id being removed
 */
static int bond_vlan_rx_kill_vid(struct net_device *bond_dev, uint16_t vid)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave;
	int i, res;

	bond_for_each_slave(bond, slave, i)
		vlan_vid_del(slave->dev, vid);

	res = bond_del_vlan(bond, vid);
	if (res) {
		pr_err("%s: Error: Failed to remove vlan id %d\n",
		       bond_dev->name, vid);
		return res;
	}

	return 0;
}

static void bond_add_vlans_on_slave(struct bonding *bond, struct net_device *slave_dev)
{
	struct vlan_entry *vlan;
	int res;

	list_for_each_entry(vlan, &bond->vlan_list, vlan_list) {
		res = vlan_vid_add(slave_dev, vlan->vlan_id);
		if (res)
			pr_warning("%s: Failed to add vlan id %d to device %s\n",
				   bond->dev->name, vlan->vlan_id,
				   slave_dev->name);
	}
}

static void bond_del_vlans_from_slave(struct bonding *bond,
				      struct net_device *slave_dev)
{
	struct vlan_entry *vlan;

	list_for_each_entry(vlan, &bond->vlan_list, vlan_list) {
		if (!vlan->vlan_id)
			continue;
		vlan_vid_del(slave_dev, vlan->vlan_id);
	}
}

/*------------------------------- Link status -------------------------------*/

/*
 * Set the carrier state for the master according to the state of its
 * slaves.  If any slaves are up, the master is up.  In 802.3ad mode,
 * do special 802.3ad magic.
 *
 * Returns zero if carrier state does not change, nonzero if it does.
 */
static int bond_set_carrier(struct bonding *bond)
{
	struct slave *slave;
	int i;

	if (bond->slave_cnt == 0)
		goto down;

	if (bond->params.mode == BOND_MODE_8023AD)
		return bond_3ad_set_carrier(bond);

	bond_for_each_slave(bond, slave, i) {
		if (slave->link == BOND_LINK_UP) {
			if (!netif_carrier_ok(bond->dev)) {
				netif_carrier_on(bond->dev);
				return 1;
			}
			return 0;
		}
	}

down:
	if (netif_carrier_ok(bond->dev)) {
		netif_carrier_off(bond->dev);
		return 1;
	}
	return 0;
}

/*
 * Get link speed and duplex from the slave's base driver
 * using ethtool. If for some reason the call fails or the
 * values are invalid, set speed and duplex to -1,
 * and return.
 */
static void bond_update_speed_duplex(struct slave *slave)
{
	struct net_device *slave_dev = slave->dev;
	struct ethtool_cmd ecmd;
	u32 slave_speed;
	int res;

	slave->speed = SPEED_UNKNOWN;
	slave->duplex = DUPLEX_UNKNOWN;

	res = __ethtool_get_settings(slave_dev, &ecmd);
	if (res < 0)
		return;

	slave_speed = ethtool_cmd_speed(&ecmd);
	if (slave_speed == 0 || slave_speed == ((__u32) -1))
		return;

	switch (ecmd.duplex) {
	case DUPLEX_FULL:
	case DUPLEX_HALF:
		break;
	default:
		return;
	}

	slave->speed = slave_speed;
	slave->duplex = ecmd.duplex;

	return;
}

/*
 * if <dev> supports MII link status reporting, check its link status.
 *
 * We either do MII/ETHTOOL ioctls, or check netif_carrier_ok(),
 * depending upon the setting of the use_carrier parameter.
 *
 * Return either BMSR_LSTATUS, meaning that the link is up (or we
 * can't tell and just pretend it is), or 0, meaning that the link is
 * down.
 *
 * If reporting is non-zero, instead of faking link up, return -1 if
 * both ETHTOOL and MII ioctls fail (meaning the device does not
 * support them).  If use_carrier is set, return whatever it says.
 * It'd be nice if there was a good way to tell if a driver supports
 * netif_carrier, but there really isn't.
 */
static int bond_check_dev_link(struct bonding *bond,
			       struct net_device *slave_dev, int reporting)
{
	const struct net_device_ops *slave_ops = slave_dev->netdev_ops;
	int (*ioctl)(struct net_device *, struct ifreq *, int);
	struct ifreq ifr;
	struct mii_ioctl_data *mii;

	if (!reporting && !netif_running(slave_dev))
		return 0;

	if (bond->params.use_carrier)
		return netif_carrier_ok(slave_dev) ? BMSR_LSTATUS : 0;

	/* Try to get link status using Ethtool first. */
	if (slave_dev->ethtool_ops) {
		if (slave_dev->ethtool_ops->get_link) {
			u32 link;

			link = slave_dev->ethtool_ops->get_link(slave_dev);

			return link ? BMSR_LSTATUS : 0;
		}
	}

	/* Ethtool can't be used, fallback to MII ioctls. */
	ioctl = slave_ops->ndo_do_ioctl;
	if (ioctl) {
		/* TODO: set pointer to correct ioctl on a per team member */
		/*       bases to make this more efficient. that is, once  */
		/*       we determine the correct ioctl, we will always    */
		/*       call it and not the others for that team          */
		/*       member.                                           */

		/*
		 * We cannot assume that SIOCGMIIPHY will also read a
		 * register; not all network drivers (e.g., e100)
		 * support that.
		 */

		/* Yes, the mii is overlaid on the ifreq.ifr_ifru */
		strncpy(ifr.ifr_name, slave_dev->name, IFNAMSIZ);
		mii = if_mii(&ifr);
		if (IOCTL(slave_dev, &ifr, SIOCGMIIPHY) == 0) {
			mii->reg_num = MII_BMSR;
			if (IOCTL(slave_dev, &ifr, SIOCGMIIREG) == 0)
				return mii->val_out & BMSR_LSTATUS;
		}
	}

	/*
	 * If reporting, report that either there's no dev->do_ioctl,
	 * or both SIOCGMIIREG and get_link failed (meaning that we
	 * cannot report link status).  If not reporting, pretend
	 * we're ok.
	 */
	return reporting ? -1 : BMSR_LSTATUS;
}

/*----------------------------- Multicast list ------------------------------*/

/*
 * Push the promiscuity flag down to appropriate slaves
 */
static int bond_set_promiscuity(struct bonding *bond, int inc)
{
	int err = 0;
	if (USES_PRIMARY(bond->params.mode)) {
		/* write lock already acquired */
		if (bond->curr_active_slave) {
			err = dev_set_promiscuity(bond->curr_active_slave->dev,
						  inc);
		}
	} else {
		struct slave *slave;
		int i;
		bond_for_each_slave(bond, slave, i) {
			err = dev_set_promiscuity(slave->dev, inc);
			if (err)
				return err;
		}
	}
	return err;
}

/*
 * Push the allmulti flag down to all slaves
 */
static int bond_set_allmulti(struct bonding *bond, int inc)
{
	int err = 0;
	if (USES_PRIMARY(bond->params.mode)) {
		/* write lock already acquired */
		if (bond->curr_active_slave) {
			err = dev_set_allmulti(bond->curr_active_slave->dev,
					       inc);
		}
	} else {
		struct slave *slave;
		int i;
		bond_for_each_slave(bond, slave, i) {
			err = dev_set_allmulti(slave->dev, inc);
			if (err)
				return err;
		}
	}
	return err;
}

/*
 * Add a Multicast address to slaves
 * according to mode
 */
static void bond_mc_add(struct bonding *bond, void *addr)
{
	if (USES_PRIMARY(bond->params.mode)) {
		/* write lock already acquired */
		if (bond->curr_active_slave)
			dev_mc_add(bond->curr_active_slave->dev, addr);
	} else {
		struct slave *slave;
		int i;

		bond_for_each_slave(bond, slave, i)
			dev_mc_add(slave->dev, addr);
	}
}

/*
 * Remove a multicast address from slave
 * according to mode
 */
static void bond_mc_del(struct bonding *bond, void *addr)
{
	if (USES_PRIMARY(bond->params.mode)) {
		/* write lock already acquired */
		if (bond->curr_active_slave)
			dev_mc_del(bond->curr_active_slave->dev, addr);
	} else {
		struct slave *slave;
		int i;
		bond_for_each_slave(bond, slave, i) {
			dev_mc_del(slave->dev, addr);
		}
	}
}


static void __bond_resend_igmp_join_requests(struct net_device *dev)
{
	struct in_device *in_dev;

	rcu_read_lock();
	in_dev = __in_dev_get_rcu(dev);
	if (in_dev)
		ip_mc_rejoin_groups(in_dev);
	rcu_read_unlock();
}

/*
 * Retrieve the list of registered multicast addresses for the bonding
 * device and retransmit an IGMP JOIN request to the current active
 * slave.
 */
static void bond_resend_igmp_join_requests(struct bonding *bond)
{
	struct net_device *bond_dev, *vlan_dev, *master_dev;
	struct vlan_entry *vlan;

	read_lock(&bond->lock);

	bond_dev = bond->dev;

	/* rejoin all groups on bond device */
	__bond_resend_igmp_join_requests(bond_dev);

	/*
	 * if bond is enslaved to a bridge,
	 * then rejoin all groups on its master
	 */
	master_dev = bond_dev->master;
	if (master_dev)
		if ((master_dev->priv_flags & IFF_EBRIDGE)
			&& (bond_dev->priv_flags & IFF_BRIDGE_PORT))
			__bond_resend_igmp_join_requests(master_dev);

	/* rejoin all groups on vlan devices */
	list_for_each_entry(vlan, &bond->vlan_list, vlan_list) {
		rcu_read_lock();
		vlan_dev = __vlan_find_dev_deep(bond_dev,
						vlan->vlan_id);
		rcu_read_unlock();
		if (vlan_dev)
			__bond_resend_igmp_join_requests(vlan_dev);
	}

	if (--bond->igmp_retrans > 0)
		queue_delayed_work(bond->wq, &bond->mcast_work, HZ/5);

	read_unlock(&bond->lock);
}

static void bond_resend_igmp_join_requests_delayed(struct work_struct *work)
{
	struct bonding *bond = container_of(work, struct bonding,
					    mcast_work.work);
	bond_resend_igmp_join_requests(bond);
}

/*
 * flush all members of flush->mc_list from device dev->mc_list
 */
static void bond_mc_list_flush(struct net_device *bond_dev,
			       struct net_device *slave_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct netdev_hw_addr *ha;

	netdev_for_each_mc_addr(ha, bond_dev)
		dev_mc_del(slave_dev, ha->addr);

	if (bond->params.mode == BOND_MODE_8023AD) {
		/* del lacpdu mc addr from mc list */
		u8 lacpdu_multicast[ETH_ALEN] = MULTICAST_LACPDU_ADDR;

		dev_mc_del(slave_dev, lacpdu_multicast);
	}
}

/*--------------------------- Active slave change ---------------------------*/

/*
 * Update the mc list and multicast-related flags for the new and
 * old active slaves (if any) according to the multicast mode, and
 * promiscuous flags unconditionally.
 */
static void bond_mc_swap(struct bonding *bond, struct slave *new_active,
			 struct slave *old_active)
{
	struct netdev_hw_addr *ha;

	if (!USES_PRIMARY(bond->params.mode))
		/* nothing to do -  mc list is already up-to-date on
		 * all slaves
		 */
		return;

	if (old_active) {
		if (bond->dev->flags & IFF_PROMISC)
			dev_set_promiscuity(old_active->dev, -1);

		if (bond->dev->flags & IFF_ALLMULTI)
			dev_set_allmulti(old_active->dev, -1);

		netdev_for_each_mc_addr(ha, bond->dev)
			dev_mc_del(old_active->dev, ha->addr);
	}

	if (new_active) {
		/* FIXME: Signal errors upstream. */
		if (bond->dev->flags & IFF_PROMISC)
			dev_set_promiscuity(new_active->dev, 1);

		if (bond->dev->flags & IFF_ALLMULTI)
			dev_set_allmulti(new_active->dev, 1);

		netdev_for_each_mc_addr(ha, bond->dev)
			dev_mc_add(new_active->dev, ha->addr);
	}
}

/*
 * bond_do_fail_over_mac
 *
 * Perform special MAC address swapping for fail_over_mac settings
 *
 * Called with RTNL, bond->lock for read, curr_slave_lock for write_bh.
 */
static void bond_do_fail_over_mac(struct bonding *bond,
				  struct slave *new_active,
				  struct slave *old_active)
	__releases(&bond->curr_slave_lock)
	__releases(&bond->lock)
	__acquires(&bond->lock)
	__acquires(&bond->curr_slave_lock)
{
	u8 tmp_mac[ETH_ALEN];
	struct sockaddr saddr;
	int rv;

	switch (bond->params.fail_over_mac) {
	case BOND_FOM_ACTIVE:
		if (new_active) {
			memcpy(bond->dev->dev_addr,  new_active->dev->dev_addr,
			       new_active->dev->addr_len);
			write_unlock_bh(&bond->curr_slave_lock);
			read_unlock(&bond->lock);
			call_netdevice_notifiers(NETDEV_CHANGEADDR, bond->dev);
			read_lock(&bond->lock);
			write_lock_bh(&bond->curr_slave_lock);
		}
		break;
	case BOND_FOM_FOLLOW:
		/*
		 * if new_active && old_active, swap them
		 * if just old_active, do nothing (going to no active slave)
		 * if just new_active, set new_active to bond's MAC
		 */
		if (!new_active)
			return;

		write_unlock_bh(&bond->curr_slave_lock);
		read_unlock(&bond->lock);

		if (old_active) {
			memcpy(tmp_mac, new_active->dev->dev_addr, ETH_ALEN);
			memcpy(saddr.sa_data, old_active->dev->dev_addr,
			       ETH_ALEN);
			saddr.sa_family = new_active->dev->type;
		} else {
			memcpy(saddr.sa_data, bond->dev->dev_addr, ETH_ALEN);
			saddr.sa_family = bond->dev->type;
		}

		rv = dev_set_mac_address(new_active->dev, &saddr);
		if (rv) {
			pr_err("%s: Error %d setting MAC of slave %s\n",
			       bond->dev->name, -rv, new_active->dev->name);
			goto out;
		}

		if (!old_active)
			goto out;

		memcpy(saddr.sa_data, tmp_mac, ETH_ALEN);
		saddr.sa_family = old_active->dev->type;

		rv = dev_set_mac_address(old_active->dev, &saddr);
		if (rv)
			pr_err("%s: Error %d setting MAC of slave %s\n",
			       bond->dev->name, -rv, new_active->dev->name);
out:
		read_lock(&bond->lock);
		write_lock_bh(&bond->curr_slave_lock);
		break;
	default:
		pr_err("%s: bond_do_fail_over_mac impossible: bad policy %d\n",
		       bond->dev->name, bond->params.fail_over_mac);
		break;
	}

}

static bool bond_should_change_active(struct bonding *bond)
{
	struct slave *prim = bond->primary_slave;
	struct slave *curr = bond->curr_active_slave;

	if (!prim || !curr || curr->link != BOND_LINK_UP)
		return true;
	if (bond->force_primary) {
		bond->force_primary = false;
		return true;
	}
	if (bond->params.primary_reselect == BOND_PRI_RESELECT_BETTER &&
	    (prim->speed < curr->speed ||
	     (prim->speed == curr->speed && prim->duplex <= curr->duplex)))
		return false;
	if (bond->params.primary_reselect == BOND_PRI_RESELECT_FAILURE)
		return false;
	return true;
}

/**
 * find_best_interface - select the best available slave to be the active one
 * @bond: our bonding struct
 *
 * Warning: Caller must hold curr_slave_lock for writing.
 */
static struct slave *bond_find_best_slave(struct bonding *bond)
{
	struct slave *new_active, *old_active;
	struct slave *bestslave = NULL;
	int mintime = bond->params.updelay;
	int i;

	new_active = bond->curr_active_slave;

	if (!new_active) { /* there were no active slaves left */
		if (bond->slave_cnt > 0)   /* found one slave */
			new_active = bond->first_slave;
		else
			return NULL; /* still no slave, return NULL */
	}

	if ((bond->primary_slave) &&
	    bond->primary_slave->link == BOND_LINK_UP &&
	    bond_should_change_active(bond)) {
		new_active = bond->primary_slave;
	}

	/* remember where to stop iterating over the slaves */
	old_active = new_active;

	bond_for_each_slave_from(bond, new_active, i, old_active) {
		if (new_active->link == BOND_LINK_UP) {
			return new_active;
		} else if (new_active->link == BOND_LINK_BACK &&
			   IS_UP(new_active->dev)) {
			/* link up, but waiting for stabilization */
			if (new_active->delay < mintime) {
				mintime = new_active->delay;
				bestslave = new_active;
			}
		}
	}

	return bestslave;
}

static bool bond_should_notify_peers(struct bonding *bond)
{
	struct slave *slave = bond->curr_active_slave;

	pr_debug("bond_should_notify_peers: bond %s slave %s\n",
		 bond->dev->name, slave ? slave->dev->name : "NULL");

	if (!slave || !bond->send_peer_notif ||
	    test_bit(__LINK_STATE_LINKWATCH_PENDING, &slave->dev->state))
		return false;

	bond->send_peer_notif--;
	return true;
}

/**
 * change_active_interface - change the active slave into the specified one
 * @bond: our bonding struct
 * @new: the new slave to make the active one
 *
 * Set the new slave to the bond's settings and unset them on the old
 * curr_active_slave.
 * Setting include flags, mc-list, promiscuity, allmulti, etc.
 *
 * If @new's link state is %BOND_LINK_BACK we'll set it to %BOND_LINK_UP,
 * because it is apparently the best available slave we have, even though its
 * updelay hasn't timed out yet.
 *
 * If new_active is not NULL, caller must hold bond->lock for read and
 * curr_slave_lock for write_bh.
 */
void bond_change_active_slave(struct bonding *bond, struct slave *new_active)
{
	struct slave *old_active = bond->curr_active_slave;

	if (old_active == new_active)
		return;

	if (new_active) {
		new_active->jiffies = jiffies;

		if (new_active->link == BOND_LINK_BACK) {
			if (USES_PRIMARY(bond->params.mode)) {
				pr_info("%s: making interface %s the new active one %d ms earlier.\n",
					bond->dev->name, new_active->dev->name,
					(bond->params.updelay - new_active->delay) * bond->params.miimon);
			}

			new_active->delay = 0;
			new_active->link = BOND_LINK_UP;

			if (bond->params.mode == BOND_MODE_8023AD)
				bond_3ad_handle_link_change(new_active, BOND_LINK_UP);

			if (bond_is_lb(bond))
				bond_alb_handle_link_change(bond, new_active, BOND_LINK_UP);
		} else {
			if (USES_PRIMARY(bond->params.mode)) {
				pr_info("%s: making interface %s the new active one.\n",
					bond->dev->name, new_active->dev->name);
			}
		}
	}

	if (USES_PRIMARY(bond->params.mode))
		bond_mc_swap(bond, new_active, old_active);

	if (bond_is_lb(bond)) {
		bond_alb_handle_active_change(bond, new_active);
		if (old_active)
			bond_set_slave_inactive_flags(old_active);
		if (new_active)
			bond_set_slave_active_flags(new_active);
	} else {
		bond->curr_active_slave = new_active;
	}

	if (bond->params.mode == BOND_MODE_ACTIVEBACKUP) {
		if (old_active)
			bond_set_slave_inactive_flags(old_active);

		if (new_active) {
			bool should_notify_peers = false;

			bond_set_slave_active_flags(new_active);

			if (bond->params.fail_over_mac)
				bond_do_fail_over_mac(bond, new_active,
						      old_active);

			if (netif_running(bond->dev)) {
				bond->send_peer_notif =
					bond->params.num_peer_notif;
				should_notify_peers =
					bond_should_notify_peers(bond);
			}

			write_unlock_bh(&bond->curr_slave_lock);
			read_unlock(&bond->lock);

			call_netdevice_notifiers(NETDEV_BONDING_FAILOVER, bond->dev);
			if (should_notify_peers)
				call_netdevice_notifiers(NETDEV_NOTIFY_PEERS,
							 bond->dev);

			read_lock(&bond->lock);
			write_lock_bh(&bond->curr_slave_lock);
		}
	}

	/* resend IGMP joins since active slave has changed or
	 * all were sent on curr_active_slave.
	 * resend only if bond is brought up with the affected
	 * bonding modes and the retransmission is enabled */
	if (netif_running(bond->dev) && (bond->params.resend_igmp > 0) &&
	    ((USES_PRIMARY(bond->params.mode) && new_active) ||
	     bond->params.mode == BOND_MODE_ROUNDROBIN)) {
		bond->igmp_retrans = bond->params.resend_igmp;
		queue_delayed_work(bond->wq, &bond->mcast_work, 0);
	}
}

/**
 * bond_select_active_slave - select a new active slave, if needed
 * @bond: our bonding struct
 *
 * This functions should be called when one of the following occurs:
 * - The old curr_active_slave has been released or lost its link.
 * - The primary_slave has got its link back.
 * - A slave has got its link back and there's no old curr_active_slave.
 *
 * Caller must hold bond->lock for read and curr_slave_lock for write_bh.
 */
void bond_select_active_slave(struct bonding *bond)
{
	struct slave *best_slave;
	int rv;

	best_slave = bond_find_best_slave(bond);
	if (best_slave != bond->curr_active_slave) {
		bond_change_active_slave(bond, best_slave);
		rv = bond_set_carrier(bond);
		if (!rv)
			return;

		if (netif_carrier_ok(bond->dev)) {
			pr_info("%s: first active interface up!\n",
				bond->dev->name);
		} else {
			pr_info("%s: now running without any active interface !\n",
				bond->dev->name);
		}
	}
}

/*--------------------------- slave list handling ---------------------------*/

/*
 * This function attaches the slave to the end of list.
 *
 * bond->lock held for writing by caller.
 */
static void bond_attach_slave(struct bonding *bond, struct slave *new_slave)
{
	if (bond->first_slave == NULL) { /* attaching the first slave */
		new_slave->next = new_slave;
		new_slave->prev = new_slave;
		bond->first_slave = new_slave;
	} else {
		new_slave->next = bond->first_slave;
		new_slave->prev = bond->first_slave->prev;
		new_slave->next->prev = new_slave;
		new_slave->prev->next = new_slave;
	}

	bond->slave_cnt++;
}

/*
 * This function detaches the slave from the list.
 * WARNING: no check is made to verify if the slave effectively
 * belongs to <bond>.
 * Nothing is freed on return, structures are just unchained.
 * If any slave pointer in bond was pointing to <slave>,
 * it should be changed by the calling function.
 *
 * bond->lock held for writing by caller.
 */
static void bond_detach_slave(struct bonding *bond, struct slave *slave)
{
	if (slave->next)
		slave->next->prev = slave->prev;

	if (slave->prev)
		slave->prev->next = slave->next;

	if (bond->first_slave == slave) { /* slave is the first slave */
		if (bond->slave_cnt > 1) { /* there are more slave */
			bond->first_slave = slave->next;
		} else {
			bond->first_slave = NULL; /* slave was the last one */
		}
	}

	slave->next = NULL;
	slave->prev = NULL;
	bond->slave_cnt--;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static inline int slave_enable_netpoll(struct slave *slave)
{
	struct netpoll *np;
	int err = 0;

	np = kzalloc(sizeof(*np), GFP_KERNEL);
	err = -ENOMEM;
	if (!np)
		goto out;

	err = __netpoll_setup(np, slave->dev);
	if (err) {
		kfree(np);
		goto out;
	}
	slave->np = np;
out:
	return err;
}
static inline void slave_disable_netpoll(struct slave *slave)
{
	struct netpoll *np = slave->np;

	if (!np)
		return;

	slave->np = NULL;
	synchronize_rcu_bh();
	__netpoll_cleanup(np);
	kfree(np);
}
static inline bool slave_dev_support_netpoll(struct net_device *slave_dev)
{
	if (slave_dev->priv_flags & IFF_DISABLE_NETPOLL)
		return false;
	if (!slave_dev->netdev_ops->ndo_poll_controller)
		return false;
	return true;
}

static void bond_poll_controller(struct net_device *bond_dev)
{
}

static void __bond_netpoll_cleanup(struct bonding *bond)
{
	struct slave *slave;
	int i;

	bond_for_each_slave(bond, slave, i)
		if (IS_UP(slave->dev))
			slave_disable_netpoll(slave);
}
static void bond_netpoll_cleanup(struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);

	read_lock(&bond->lock);
	__bond_netpoll_cleanup(bond);
	read_unlock(&bond->lock);
}

static int bond_netpoll_setup(struct net_device *dev, struct netpoll_info *ni)
{
	struct bonding *bond = netdev_priv(dev);
	struct slave *slave;
	int i, err = 0;

	read_lock(&bond->lock);
	bond_for_each_slave(bond, slave, i) {
		err = slave_enable_netpoll(slave);
		if (err) {
			__bond_netpoll_cleanup(bond);
			break;
		}
	}
	read_unlock(&bond->lock);
	return err;
}

static struct netpoll_info *bond_netpoll_info(struct bonding *bond)
{
	return bond->dev->npinfo;
}

#else
static inline int slave_enable_netpoll(struct slave *slave)
{
	return 0;
}
static inline void slave_disable_netpoll(struct slave *slave)
{
}
static void bond_netpoll_cleanup(struct net_device *bond_dev)
{
}
#endif

/*---------------------------------- IOCTL ----------------------------------*/

static int bond_sethwaddr(struct net_device *bond_dev,
			  struct net_device *slave_dev)
{
	pr_debug("bond_dev=%p\n", bond_dev);
	pr_debug("slave_dev=%p\n", slave_dev);
	pr_debug("slave_dev->addr_len=%d\n", slave_dev->addr_len);
	memcpy(bond_dev->dev_addr, slave_dev->dev_addr, slave_dev->addr_len);
	return 0;
}

static netdev_features_t bond_fix_features(struct net_device *dev,
	netdev_features_t features)
{
	struct slave *slave;
	struct bonding *bond = netdev_priv(dev);
	netdev_features_t mask;
	int i;

	read_lock(&bond->lock);

	if (!bond->first_slave) {
		/* Disable adding VLANs to empty bond. But why? --mq */
		features |= NETIF_F_VLAN_CHALLENGED;
		goto out;
	}

	mask = features;
	features &= ~NETIF_F_ONE_FOR_ALL;
	features |= NETIF_F_ALL_FOR_ALL;

	bond_for_each_slave(bond, slave, i) {
		features = netdev_increment_features(features,
						     slave->dev->features,
						     mask);
	}

out:
	read_unlock(&bond->lock);
	return features;
}

#define BOND_VLAN_FEATURES	(NETIF_F_ALL_CSUM | NETIF_F_SG | \
				 NETIF_F_FRAGLIST | NETIF_F_ALL_TSO | \
				 NETIF_F_HIGHDMA | NETIF_F_LRO)

static void bond_compute_features(struct bonding *bond)
{
	struct slave *slave;
	struct net_device *bond_dev = bond->dev;
	netdev_features_t vlan_features = BOND_VLAN_FEATURES;
	unsigned short max_hard_header_len = ETH_HLEN;
	int i;
	unsigned int flags, dst_release_flag = IFF_XMIT_DST_RELEASE;

	read_lock(&bond->lock);

	if (!bond->first_slave)
		goto done;

	bond_for_each_slave(bond, slave, i) {
		vlan_features = netdev_increment_features(vlan_features,
			slave->dev->vlan_features, BOND_VLAN_FEATURES);

		dst_release_flag &= slave->dev->priv_flags;
		if (slave->dev->hard_header_len > max_hard_header_len)
			max_hard_header_len = slave->dev->hard_header_len;
	}

done:
	bond_dev->vlan_features = vlan_features;
	bond_dev->hard_header_len = max_hard_header_len;

	flags = bond_dev->priv_flags & ~IFF_XMIT_DST_RELEASE;
	bond_dev->priv_flags = flags | dst_release_flag;

	read_unlock(&bond->lock);

	netdev_change_features(bond_dev);
}

static void bond_setup_by_slave(struct net_device *bond_dev,
				struct net_device *slave_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);

	bond_dev->header_ops	    = slave_dev->header_ops;

	bond_dev->type		    = slave_dev->type;
	bond_dev->hard_header_len   = slave_dev->hard_header_len;
	bond_dev->addr_len	    = slave_dev->addr_len;

	memcpy(bond_dev->broadcast, slave_dev->broadcast,
		slave_dev->addr_len);
	bond->setup_by_slave = 1;
}

/* On bonding slaves other than the currently active slave, suppress
 * duplicates except for alb non-mcast/bcast.
 */
static bool bond_should_deliver_exact_match(struct sk_buff *skb,
					    struct slave *slave,
					    struct bonding *bond)
{
	if (bond_is_slave_inactive(slave)) {
		if (bond->params.mode == BOND_MODE_ALB &&
		    skb->pkt_type != PACKET_BROADCAST &&
		    skb->pkt_type != PACKET_MULTICAST)
			return false;
		return true;
	}
	return false;
}

static rx_handler_result_t bond_handle_frame(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct slave *slave;
	struct bonding *bond;
	int (*recv_probe)(const struct sk_buff *, struct bonding *,
			  struct slave *);
	int ret = RX_HANDLER_ANOTHER;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(!skb))
		return RX_HANDLER_CONSUMED;

	*pskb = skb;

	slave = bond_slave_get_rcu(skb->dev);
	bond = slave->bond;

	if (bond->params.arp_interval)
		slave->dev->last_rx = jiffies;

	recv_probe = ACCESS_ONCE(bond->recv_probe);
	if (recv_probe) {
		ret = recv_probe(skb, bond, slave);
		if (ret == RX_HANDLER_CONSUMED) {
			consume_skb(skb);
			return ret;
		}
	}

	if (bond_should_deliver_exact_match(skb, slave, bond)) {
		return RX_HANDLER_EXACT;
	}

	skb->dev = bond->dev;

	if (bond->params.mode == BOND_MODE_ALB &&
	    bond->dev->priv_flags & IFF_BRIDGE_PORT &&
	    skb->pkt_type == PACKET_HOST) {

		if (unlikely(skb_cow_head(skb,
					  skb->data - skb_mac_header(skb)))) {
			kfree_skb(skb);
			return RX_HANDLER_CONSUMED;
		}
		memcpy(eth_hdr(skb)->h_dest, bond->dev->dev_addr, ETH_ALEN);
	}

	return ret;
}

/* enslave device <slave> to bond device <master> */
int bond_enslave(struct net_device *bond_dev, struct net_device *slave_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	const struct net_device_ops *slave_ops = slave_dev->netdev_ops;
	struct slave *new_slave = NULL;
	struct netdev_hw_addr *ha;
	struct sockaddr addr;
	int link_reporting;
	int res = 0;

	if (!bond->params.use_carrier && slave_dev->ethtool_ops == NULL &&
		slave_ops->ndo_do_ioctl == NULL) {
		pr_warning("%s: Warning: no link monitoring support for %s\n",
			   bond_dev->name, slave_dev->name);
	}

	/* already enslaved */
	if (slave_dev->flags & IFF_SLAVE) {
		pr_debug("Error, Device was already enslaved\n");
		return -EBUSY;
	}

	/* vlan challenged mutual exclusion */
	/* no need to lock since we're protected by rtnl_lock */
	if (slave_dev->features & NETIF_F_VLAN_CHALLENGED) {
		pr_debug("%s: NETIF_F_VLAN_CHALLENGED\n", slave_dev->name);
		if (bond_vlan_used(bond)) {
			pr_err("%s: Error: cannot enslave VLAN challenged slave %s on VLAN enabled bond %s\n",
			       bond_dev->name, slave_dev->name, bond_dev->name);
			return -EPERM;
		} else {
			pr_warning("%s: Warning: enslaved VLAN challenged slave %s. Adding VLANs will be blocked as long as %s is part of bond %s\n",
				   bond_dev->name, slave_dev->name,
				   slave_dev->name, bond_dev->name);
		}
	} else {
		pr_debug("%s: ! NETIF_F_VLAN_CHALLENGED\n", slave_dev->name);
	}

	/*
	 * Old ifenslave binaries are no longer supported.  These can
	 * be identified with moderate accuracy by the state of the slave:
	 * the current ifenslave will set the interface down prior to
	 * enslaving it; the old ifenslave will not.
	 */
	if ((slave_dev->flags & IFF_UP)) {
		pr_err("%s is up. This may be due to an out of date ifenslave.\n",
		       slave_dev->name);
		res = -EPERM;
		goto err_undo_flags;
	}

	/* set bonding device ether type by slave - bonding netdevices are
	 * created with ether_setup, so when the slave type is not ARPHRD_ETHER
	 * there is a need to override some of the type dependent attribs/funcs.
	 *
	 * bond ether type mutual exclusion - don't allow slaves of dissimilar
	 * ether type (eg ARPHRD_ETHER and ARPHRD_INFINIBAND) share the same bond
	 */
	if (bond->slave_cnt == 0) {
		if (bond_dev->type != slave_dev->type) {
			pr_debug("%s: change device type from %d to %d\n",
				 bond_dev->name,
				 bond_dev->type, slave_dev->type);

			res = call_netdevice_notifiers(NETDEV_PRE_TYPE_CHANGE,
						       bond_dev);
			res = notifier_to_errno(res);
			if (res) {
				pr_err("%s: refused to change device type\n",
				       bond_dev->name);
				res = -EBUSY;
				goto err_undo_flags;
			}

			/* Flush unicast and multicast addresses */
			dev_uc_flush(bond_dev);
			dev_mc_flush(bond_dev);

			if (slave_dev->type != ARPHRD_ETHER)
				bond_setup_by_slave(bond_dev, slave_dev);
			else {
				ether_setup(bond_dev);
				bond_dev->priv_flags &= ~IFF_TX_SKB_SHARING;
			}

			call_netdevice_notifiers(NETDEV_POST_TYPE_CHANGE,
						 bond_dev);
		}
	} else if (bond_dev->type != slave_dev->type) {
		pr_err("%s ether type (%d) is different from other slaves (%d), can not enslave it.\n",
		       slave_dev->name,
		       slave_dev->type, bond_dev->type);
		res = -EINVAL;
		goto err_undo_flags;
	}

	if (slave_ops->ndo_set_mac_address == NULL) {
		if (bond->slave_cnt == 0) {
			pr_warning("%s: Warning: The first slave device specified does not support setting the MAC address. Setting fail_over_mac to active.",
				   bond_dev->name);
			bond->params.fail_over_mac = BOND_FOM_ACTIVE;
		} else if (bond->params.fail_over_mac != BOND_FOM_ACTIVE) {
			pr_err("%s: Error: The slave device specified does not support setting the MAC address, but fail_over_mac is not set to active.\n",
			       bond_dev->name);
			res = -EOPNOTSUPP;
			goto err_undo_flags;
		}
	}

	call_netdevice_notifiers(NETDEV_JOIN, slave_dev);

	/* If this is the first slave, then we need to set the master's hardware
	 * address to be the same as the slave's. */
	if (is_zero_ether_addr(bond->dev->dev_addr))
		memcpy(bond->dev->dev_addr, slave_dev->dev_addr,
		       slave_dev->addr_len);


	new_slave = kzalloc(sizeof(struct slave), GFP_KERNEL);
	if (!new_slave) {
		res = -ENOMEM;
		goto err_undo_flags;
	}

	/*
	 * Set the new_slave's queue_id to be zero.  Queue ID mapping
	 * is set via sysfs or module option if desired.
	 */
	new_slave->queue_id = 0;

	/* Save slave's original mtu and then set it to match the bond */
	new_slave->original_mtu = slave_dev->mtu;
	res = dev_set_mtu(slave_dev, bond->dev->mtu);
	if (res) {
		pr_debug("Error %d calling dev_set_mtu\n", res);
		goto err_free;
	}

	/*
	 * Save slave's original ("permanent") mac address for modes
	 * that need it, and for restoring it upon release, and then
	 * set it to the master's address
	 */
	memcpy(new_slave->perm_hwaddr, slave_dev->dev_addr, ETH_ALEN);

	if (!bond->params.fail_over_mac) {
		/*
		 * Set slave to master's mac address.  The application already
		 * set the master's mac address to that of the first slave
		 */
		memcpy(addr.sa_data, bond_dev->dev_addr, bond_dev->addr_len);
		addr.sa_family = slave_dev->type;
		res = dev_set_mac_address(slave_dev, &addr);
		if (res) {
			pr_debug("Error %d calling set_mac_address\n", res);
			goto err_restore_mtu;
		}
	}

	res = netdev_set_bond_master(slave_dev, bond_dev);
	if (res) {
		pr_debug("Error %d calling netdev_set_bond_master\n", res);
		goto err_restore_mac;
	}

	/* open the slave since the application closed it */
	res = dev_open(slave_dev);
	if (res) {
		pr_debug("Opening slave %s failed\n", slave_dev->name);
		goto err_unset_master;
	}

	new_slave->bond = bond;
	new_slave->dev = slave_dev;
	slave_dev->priv_flags |= IFF_BONDING;

	if (bond_is_lb(bond)) {
		/* bond_alb_init_slave() must be called before all other stages since
		 * it might fail and we do not want to have to undo everything
		 */
		res = bond_alb_init_slave(bond, new_slave);
		if (res)
			goto err_close;
	}

	/* If the mode USES_PRIMARY, then the new slave gets the
	 * master's promisc (and mc) settings only if it becomes the
	 * curr_active_slave, and that is taken care of later when calling
	 * bond_change_active()
	 */
	if (!USES_PRIMARY(bond->params.mode)) {
		/* set promiscuity level to new slave */
		if (bond_dev->flags & IFF_PROMISC) {
			res = dev_set_promiscuity(slave_dev, 1);
			if (res)
				goto err_close;
		}

		/* set allmulti level to new slave */
		if (bond_dev->flags & IFF_ALLMULTI) {
			res = dev_set_allmulti(slave_dev, 1);
			if (res)
				goto err_close;
		}

		netif_addr_lock_bh(bond_dev);
		/* upload master's mc_list to new slave */
		netdev_for_each_mc_addr(ha, bond_dev)
			dev_mc_add(slave_dev, ha->addr);
		netif_addr_unlock_bh(bond_dev);
	}

	if (bond->params.mode == BOND_MODE_8023AD) {
		/* add lacpdu mc addr to mc list */
		u8 lacpdu_multicast[ETH_ALEN] = MULTICAST_LACPDU_ADDR;

		dev_mc_add(slave_dev, lacpdu_multicast);
	}

	bond_add_vlans_on_slave(bond, slave_dev);

	write_lock_bh(&bond->lock);

	bond_attach_slave(bond, new_slave);

	new_slave->delay = 0;
	new_slave->link_failure_count = 0;

	write_unlock_bh(&bond->lock);

	bond_compute_features(bond);

	read_lock(&bond->lock);

	new_slave->last_arp_rx = jiffies -
		(msecs_to_jiffies(bond->params.arp_interval) + 1);

	if (bond->params.miimon && !bond->params.use_carrier) {
		link_reporting = bond_check_dev_link(bond, slave_dev, 1);

		if ((link_reporting == -1) && !bond->params.arp_interval) {
			/*
			 * miimon is set but a bonded network driver
			 * does not support ETHTOOL/MII and
			 * arp_interval is not set.  Note: if
			 * use_carrier is enabled, we will never go
			 * here (because netif_carrier is always
			 * supported); thus, we don't need to change
			 * the messages for netif_carrier.
			 */
			pr_warning("%s: Warning: MII and ETHTOOL support not available for interface %s, and arp_interval/arp_ip_target module parameters not specified, thus bonding will not detect link failures! see bonding.txt for details.\n",
			       bond_dev->name, slave_dev->name);
		} else if (link_reporting == -1) {
			/* unable get link status using mii/ethtool */
			pr_warning("%s: Warning: can't get link status from interface %s; the network driver associated with this interface does not support MII or ETHTOOL link status reporting, thus miimon has no effect on this interface.\n",
				   bond_dev->name, slave_dev->name);
		}
	}

	/* check for initial state */
	if (bond->params.miimon) {
		if (bond_check_dev_link(bond, slave_dev, 0) == BMSR_LSTATUS) {
			if (bond->params.updelay) {
				new_slave->link = BOND_LINK_BACK;
				new_slave->delay = bond->params.updelay;
			} else {
				new_slave->link = BOND_LINK_UP;
			}
		} else {
			new_slave->link = BOND_LINK_DOWN;
		}
	} else if (bond->params.arp_interval) {
		new_slave->link = (netif_carrier_ok(slave_dev) ?
			BOND_LINK_UP : BOND_LINK_DOWN);
	} else {
		new_slave->link = BOND_LINK_UP;
	}

	if (new_slave->link != BOND_LINK_DOWN)
		new_slave->jiffies = jiffies;
	pr_debug("Initial state of slave_dev is BOND_LINK_%s\n",
		new_slave->link == BOND_LINK_DOWN ? "DOWN" :
			(new_slave->link == BOND_LINK_UP ? "UP" : "BACK"));

	bond_update_speed_duplex(new_slave);

	if (USES_PRIMARY(bond->params.mode) && bond->params.primary[0]) {
		/* if there is a primary slave, remember it */
		if (strcmp(bond->params.primary, new_slave->dev->name) == 0) {
			bond->primary_slave = new_slave;
			bond->force_primary = true;
		}
	}

	write_lock_bh(&bond->curr_slave_lock);

	switch (bond->params.mode) {
	case BOND_MODE_ACTIVEBACKUP:
		bond_set_slave_inactive_flags(new_slave);
		bond_select_active_slave(bond);
		break;
	case BOND_MODE_8023AD:
		/* in 802.3ad mode, the internal mechanism
		 * will activate the slaves in the selected
		 * aggregator
		 */
		bond_set_slave_inactive_flags(new_slave);
		/* if this is the first slave */
		if (bond->slave_cnt == 1) {
			SLAVE_AD_INFO(new_slave).id = 1;
			/* Initialize AD with the number of times that the AD timer is called in 1 second
			 * can be called only after the mac address of the bond is set
			 */
			bond_3ad_initialize(bond, 1000/AD_TIMER_INTERVAL);
		} else {
			SLAVE_AD_INFO(new_slave).id =
				SLAVE_AD_INFO(new_slave->prev).id + 1;
		}

		bond_3ad_bind_slave(new_slave);
		break;
	case BOND_MODE_TLB:
	case BOND_MODE_ALB:
		bond_set_active_slave(new_slave);
		bond_set_slave_inactive_flags(new_slave);
		bond_select_active_slave(bond);
		break;
	default:
		pr_debug("This slave is always active in trunk mode\n");

		/* always active in trunk mode */
		bond_set_active_slave(new_slave);

		/* In trunking mode there is little meaning to curr_active_slave
		 * anyway (it holds no special properties of the bond device),
		 * so we can change it without calling change_active_interface()
		 */
		if (!bond->curr_active_slave)
			bond->curr_active_slave = new_slave;

		break;
	} /* switch(bond_mode) */

	write_unlock_bh(&bond->curr_slave_lock);

	bond_set_carrier(bond);

#ifdef CONFIG_NET_POLL_CONTROLLER
	slave_dev->npinfo = bond_netpoll_info(bond);
	if (slave_dev->npinfo) {
		if (slave_enable_netpoll(new_slave)) {
			read_unlock(&bond->lock);
			pr_info("Error, %s: master_dev is using netpoll, "
				 "but new slave device does not support netpoll.\n",
				 bond_dev->name);
			res = -EBUSY;
			goto err_detach;
		}
	}
#endif

	read_unlock(&bond->lock);

	res = bond_create_slave_symlinks(bond_dev, slave_dev);
	if (res)
		goto err_detach;

	res = netdev_rx_handler_register(slave_dev, bond_handle_frame,
					 new_slave);
	if (res) {
		pr_debug("Error %d calling netdev_rx_handler_register\n", res);
		goto err_dest_symlinks;
	}

	pr_info("%s: enslaving %s as a%s interface with a%s link.\n",
		bond_dev->name, slave_dev->name,
		bond_is_active_slave(new_slave) ? "n active" : " backup",
		new_slave->link != BOND_LINK_DOWN ? "n up" : " down");

	/* enslave is successful */
	return 0;

/* Undo stages on error */
err_dest_symlinks:
	bond_destroy_slave_symlinks(bond_dev, slave_dev);

err_detach:
	write_lock_bh(&bond->lock);
	bond_detach_slave(bond, new_slave);
	write_unlock_bh(&bond->lock);

err_close:
	dev_close(slave_dev);

err_unset_master:
	netdev_set_bond_master(slave_dev, NULL);

err_restore_mac:
	if (!bond->params.fail_over_mac) {
		/* XXX TODO - fom follow mode needs to change master's
		 * MAC if this slave's MAC is in use by the bond, or at
		 * least print a warning.
		 */
		memcpy(addr.sa_data, new_slave->perm_hwaddr, ETH_ALEN);
		addr.sa_family = slave_dev->type;
		dev_set_mac_address(slave_dev, &addr);
	}

err_restore_mtu:
	dev_set_mtu(slave_dev, new_slave->original_mtu);

err_free:
	kfree(new_slave);

err_undo_flags:
	bond_compute_features(bond);

	return res;
}

/*
 * Try to release the slave device <slave> from the bond device <master>
 * It is legal to access curr_active_slave without a lock because all the function
 * is write-locked.
 *
 * The rules for slave state should be:
 *   for Active/Backup:
 *     Active stays on all backups go down
 *   for Bonded connections:
 *     The first up interface should be left on and all others downed.
 */
int bond_release(struct net_device *bond_dev, struct net_device *slave_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave, *oldcurrent;
	struct sockaddr addr;
	netdev_features_t old_features = bond_dev->features;

	/* slave is not a slave or master is not master of this slave */
	if (!(slave_dev->flags & IFF_SLAVE) ||
	    (slave_dev->master != bond_dev)) {
		pr_err("%s: Error: cannot release %s.\n",
		       bond_dev->name, slave_dev->name);
		return -EINVAL;
	}

	block_netpoll_tx();
	call_netdevice_notifiers(NETDEV_RELEASE, bond_dev);
	write_lock_bh(&bond->lock);

	slave = bond_get_slave_by_dev(bond, slave_dev);
	if (!slave) {
		/* not a slave of this bond */
		pr_info("%s: %s not enslaved\n",
			bond_dev->name, slave_dev->name);
		write_unlock_bh(&bond->lock);
		unblock_netpoll_tx();
		return -EINVAL;
	}

	/* unregister rx_handler early so bond_handle_frame wouldn't be called
	 * for this slave anymore.
	 */
	netdev_rx_handler_unregister(slave_dev);
	write_unlock_bh(&bond->lock);
	synchronize_net();
	write_lock_bh(&bond->lock);

	if (!bond->params.fail_over_mac) {
		if (ether_addr_equal(bond_dev->dev_addr, slave->perm_hwaddr) &&
		    bond->slave_cnt > 1)
			pr_warning("%s: Warning: the permanent HWaddr of %s - %pM - is still in use by %s. Set the HWaddr of %s to a different address to avoid conflicts.\n",
				   bond_dev->name, slave_dev->name,
				   slave->perm_hwaddr,
				   bond_dev->name, slave_dev->name);
	}

	/* Inform AD package of unbinding of slave. */
	if (bond->params.mode == BOND_MODE_8023AD) {
		/* must be called before the slave is
		 * detached from the list
		 */
		bond_3ad_unbind_slave(slave);
	}

	pr_info("%s: releasing %s interface %s\n",
		bond_dev->name,
		bond_is_active_slave(slave) ? "active" : "backup",
		slave_dev->name);

	oldcurrent = bond->curr_active_slave;

	bond->current_arp_slave = NULL;

	/* release the slave from its bond */
	bond_detach_slave(bond, slave);

	if (bond->primary_slave == slave)
		bond->primary_slave = NULL;

	if (oldcurrent == slave)
		bond_change_active_slave(bond, NULL);

	if (bond_is_lb(bond)) {
		/* Must be called only after the slave has been
		 * detached from the list and the curr_active_slave
		 * has been cleared (if our_slave == old_current),
		 * but before a new active slave is selected.
		 */
		write_unlock_bh(&bond->lock);
		bond_alb_deinit_slave(bond, slave);
		write_lock_bh(&bond->lock);
	}

	if (oldcurrent == slave) {
		/*
		 * Note that we hold RTNL over this sequence, so there
		 * is no concern that another slave add/remove event
		 * will interfere.
		 */
		write_unlock_bh(&bond->lock);
		read_lock(&bond->lock);
		write_lock_bh(&bond->curr_slave_lock);

		bond_select_active_slave(bond);

		write_unlock_bh(&bond->curr_slave_lock);
		read_unlock(&bond->lock);
		write_lock_bh(&bond->lock);
	}

	if (bond->slave_cnt == 0) {
		bond_set_carrier(bond);

		/* if the last slave was removed, zero the mac address
		 * of the master so it will be set by the application
		 * to the mac address of the first slave
		 */
		memset(bond_dev->dev_addr, 0, bond_dev->addr_len);

		if (bond_vlan_used(bond)) {
			pr_warning("%s: Warning: clearing HW address of %s while it still has VLANs.\n",
				   bond_dev->name, bond_dev->name);
			pr_warning("%s: When re-adding slaves, make sure the bond's HW address matches its VLANs'.\n",
				   bond_dev->name);
		}
	}

	write_unlock_bh(&bond->lock);
	unblock_netpoll_tx();

	if (bond->slave_cnt == 0)
		call_netdevice_notifiers(NETDEV_CHANGEADDR, bond->dev);

	bond_compute_features(bond);
	if (!(bond_dev->features & NETIF_F_VLAN_CHALLENGED) &&
	    (old_features & NETIF_F_VLAN_CHALLENGED))
		pr_info("%s: last VLAN challenged slave %s left bond %s. VLAN blocking is removed\n",
			bond_dev->name, slave_dev->name, bond_dev->name);

	/* must do this from outside any spinlocks */
	bond_destroy_slave_symlinks(bond_dev, slave_dev);

	bond_del_vlans_from_slave(bond, slave_dev);

	/* If the mode USES_PRIMARY, then we should only remove its
	 * promisc and mc settings if it was the curr_active_slave, but that was
	 * already taken care of above when we detached the slave
	 */
	if (!USES_PRIMARY(bond->params.mode)) {
		/* unset promiscuity level from slave */
		if (bond_dev->flags & IFF_PROMISC)
			dev_set_promiscuity(slave_dev, -1);

		/* unset allmulti level from slave */
		if (bond_dev->flags & IFF_ALLMULTI)
			dev_set_allmulti(slave_dev, -1);

		/* flush master's mc_list from slave */
		netif_addr_lock_bh(bond_dev);
		bond_mc_list_flush(bond_dev, slave_dev);
		netif_addr_unlock_bh(bond_dev);
	}

	netdev_set_bond_master(slave_dev, NULL);

	slave_disable_netpoll(slave);

	/* close slave before restoring its mac address */
	dev_close(slave_dev);

	if (bond->params.fail_over_mac != BOND_FOM_ACTIVE) {
		/* restore original ("permanent") mac address */
		memcpy(addr.sa_data, slave->perm_hwaddr, ETH_ALEN);
		addr.sa_family = slave_dev->type;
		dev_set_mac_address(slave_dev, &addr);
	}

	dev_set_mtu(slave_dev, slave->original_mtu);

	slave_dev->priv_flags &= ~IFF_BONDING;

	kfree(slave);

	return 0;  /* deletion OK */
}

/*
* First release a slave and then destroy the bond if no more slaves are left.
* Must be under rtnl_lock when this function is called.
*/
static int  bond_release_and_destroy(struct net_device *bond_dev,
				     struct net_device *slave_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	int ret;

	ret = bond_release(bond_dev, slave_dev);
	if ((ret == 0) && (bond->slave_cnt == 0)) {
		bond_dev->priv_flags |= IFF_DISABLE_NETPOLL;
		pr_info("%s: destroying bond %s.\n",
			bond_dev->name, bond_dev->name);
		unregister_netdevice(bond_dev);
	}
	return ret;
}

/*
 * This function releases all slaves.
 */
static int bond_release_all(struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave;
	struct net_device *slave_dev;
	struct sockaddr addr;

	write_lock_bh(&bond->lock);

	netif_carrier_off(bond_dev);

	if (bond->slave_cnt == 0)
		goto out;

	bond->current_arp_slave = NULL;
	bond->primary_slave = NULL;
	bond_change_active_slave(bond, NULL);

	while ((slave = bond->first_slave) != NULL) {
		/* Inform AD package of unbinding of slave
		 * before slave is detached from the list.
		 */
		if (bond->params.mode == BOND_MODE_8023AD)
			bond_3ad_unbind_slave(slave);

		slave_dev = slave->dev;
		bond_detach_slave(bond, slave);

		/* now that the slave is detached, unlock and perform
		 * all the undo steps that should not be called from
		 * within a lock.
		 */
		write_unlock_bh(&bond->lock);

		/* unregister rx_handler early so bond_handle_frame wouldn't
		 * be called for this slave anymore.
		 */
		netdev_rx_handler_unregister(slave_dev);
		synchronize_net();

		if (bond_is_lb(bond)) {
			/* must be called only after the slave
			 * has been detached from the list
			 */
			bond_alb_deinit_slave(bond, slave);
		}

		bond_destroy_slave_symlinks(bond_dev, slave_dev);
		bond_del_vlans_from_slave(bond, slave_dev);

		/* If the mode USES_PRIMARY, then we should only remove its
		 * promisc and mc settings if it was the curr_active_slave, but that was
		 * already taken care of above when we detached the slave
		 */
		if (!USES_PRIMARY(bond->params.mode)) {
			/* unset promiscuity level from slave */
			if (bond_dev->flags & IFF_PROMISC)
				dev_set_promiscuity(slave_dev, -1);

			/* unset allmulti level from slave */
			if (bond_dev->flags & IFF_ALLMULTI)
				dev_set_allmulti(slave_dev, -1);

			/* flush master's mc_list from slave */
			netif_addr_lock_bh(bond_dev);
			bond_mc_list_flush(bond_dev, slave_dev);
			netif_addr_unlock_bh(bond_dev);
		}

		netdev_set_bond_master(slave_dev, NULL);

		slave_disable_netpoll(slave);

		/* close slave before restoring its mac address */
		dev_close(slave_dev);

		if (!bond->params.fail_over_mac) {
			/* restore original ("permanent") mac address*/
			memcpy(addr.sa_data, slave->perm_hwaddr, ETH_ALEN);
			addr.sa_family = slave_dev->type;
			dev_set_mac_address(slave_dev, &addr);
		}

		kfree(slave);

		/* re-acquire the lock before getting the next slave */
		write_lock_bh(&bond->lock);
	}

	/* zero the mac address of the master so it will be
	 * set by the application to the mac address of the
	 * first slave
	 */
	memset(bond_dev->dev_addr, 0, bond_dev->addr_len);

	if (bond_vlan_used(bond)) {
		pr_warning("%s: Warning: clearing HW address of %s while it still has VLANs.\n",
			   bond_dev->name, bond_dev->name);
		pr_warning("%s: When re-adding slaves, make sure the bond's HW address matches its VLANs'.\n",
			   bond_dev->name);
	}

	pr_info("%s: released all slaves\n", bond_dev->name);

out:
	write_unlock_bh(&bond->lock);

	bond_compute_features(bond);

	return 0;
}

/*
 * This function changes the active slave to slave <slave_dev>.
 * It returns -EINVAL in the following cases.
 *  - <slave_dev> is not found in the list.
 *  - There is not active slave now.
 *  - <slave_dev> is already active.
 *  - The link state of <slave_dev> is not BOND_LINK_UP.
 *  - <slave_dev> is not running.
 * In these cases, this function does nothing.
 * In the other cases, current_slave pointer is changed and 0 is returned.
 */
static int bond_ioctl_change_active(struct net_device *bond_dev, struct net_device *slave_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *old_active = NULL;
	struct slave *new_active = NULL;
	int res = 0;

	if (!USES_PRIMARY(bond->params.mode))
		return -EINVAL;

	/* Verify that master_dev is indeed the master of slave_dev */
	if (!(slave_dev->flags & IFF_SLAVE) || (slave_dev->master != bond_dev))
		return -EINVAL;

	read_lock(&bond->lock);

	read_lock(&bond->curr_slave_lock);
	old_active = bond->curr_active_slave;
	read_unlock(&bond->curr_slave_lock);

	new_active = bond_get_slave_by_dev(bond, slave_dev);

	/*
	 * Changing to the current active: do nothing; return success.
	 */
	if (new_active && (new_active == old_active)) {
		read_unlock(&bond->lock);
		return 0;
	}

	if ((new_active) &&
	    (old_active) &&
	    (new_active->link == BOND_LINK_UP) &&
	    IS_UP(new_active->dev)) {
		block_netpoll_tx();
		write_lock_bh(&bond->curr_slave_lock);
		bond_change_active_slave(bond, new_active);
		write_unlock_bh(&bond->curr_slave_lock);
		unblock_netpoll_tx();
	} else
		res = -EINVAL;

	read_unlock(&bond->lock);

	return res;
}

static int bond_info_query(struct net_device *bond_dev, struct ifbond *info)
{
	struct bonding *bond = netdev_priv(bond_dev);

	info->bond_mode = bond->params.mode;
	info->miimon = bond->params.miimon;

	read_lock(&bond->lock);
	info->num_slaves = bond->slave_cnt;
	read_unlock(&bond->lock);

	return 0;
}

static int bond_slave_info_query(struct net_device *bond_dev, struct ifslave *info)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave;
	int i, res = -ENODEV;

	read_lock(&bond->lock);

	bond_for_each_slave(bond, slave, i) {
		if (i == (int)info->slave_id) {
			res = 0;
			strcpy(info->slave_name, slave->dev->name);
			info->link = slave->link;
			info->state = bond_slave_state(slave);
			info->link_failure_count = slave->link_failure_count;
			break;
		}
	}

	read_unlock(&bond->lock);

	return res;
}

/*-------------------------------- Monitoring -------------------------------*/


static int bond_miimon_inspect(struct bonding *bond)
{
	struct slave *slave;
	int i, link_state, commit = 0;
	bool ignore_updelay;

	ignore_updelay = !bond->curr_active_slave ? true : false;

	bond_for_each_slave(bond, slave, i) {
		slave->new_link = BOND_LINK_NOCHANGE;

		link_state = bond_check_dev_link(bond, slave->dev, 0);

		switch (slave->link) {
		case BOND_LINK_UP:
			if (link_state)
				continue;

			slave->link = BOND_LINK_FAIL;
			slave->delay = bond->params.downdelay;
			if (slave->delay) {
				pr_info("%s: link status down for %sinterface %s, disabling it in %d ms.\n",
					bond->dev->name,
					(bond->params.mode ==
					 BOND_MODE_ACTIVEBACKUP) ?
					(bond_is_active_slave(slave) ?
					 "active " : "backup ") : "",
					slave->dev->name,
					bond->params.downdelay * bond->params.miimon);
			}
			/*FALLTHRU*/
		case BOND_LINK_FAIL:
			if (link_state) {
				/*
				 * recovered before downdelay expired
				 */
				slave->link = BOND_LINK_UP;
				slave->jiffies = jiffies;
				pr_info("%s: link status up again after %d ms for interface %s.\n",
					bond->dev->name,
					(bond->params.downdelay - slave->delay) *
					bond->params.miimon,
					slave->dev->name);
				continue;
			}

			if (slave->delay <= 0) {
				slave->new_link = BOND_LINK_DOWN;
				commit++;
				continue;
			}

			slave->delay--;
			break;

		case BOND_LINK_DOWN:
			if (!link_state)
				continue;

			slave->link = BOND_LINK_BACK;
			slave->delay = bond->params.updelay;

			if (slave->delay) {
				pr_info("%s: link status up for interface %s, enabling it in %d ms.\n",
					bond->dev->name, slave->dev->name,
					ignore_updelay ? 0 :
					bond->params.updelay *
					bond->params.miimon);
			}
			/*FALLTHRU*/
		case BOND_LINK_BACK:
			if (!link_state) {
				slave->link = BOND_LINK_DOWN;
				pr_info("%s: link status down again after %d ms for interface %s.\n",
					bond->dev->name,
					(bond->params.updelay - slave->delay) *
					bond->params.miimon,
					slave->dev->name);

				continue;
			}

			if (ignore_updelay)
				slave->delay = 0;

			if (slave->delay <= 0) {
				slave->new_link = BOND_LINK_UP;
				commit++;
				ignore_updelay = false;
				continue;
			}

			slave->delay--;
			break;
		}
	}

	return commit;
}

static void bond_miimon_commit(struct bonding *bond)
{
	struct slave *slave;
	int i;

	bond_for_each_slave(bond, slave, i) {
		switch (slave->new_link) {
		case BOND_LINK_NOCHANGE:
			continue;

		case BOND_LINK_UP:
			slave->link = BOND_LINK_UP;
			slave->jiffies = jiffies;

			if (bond->params.mode == BOND_MODE_8023AD) {
				/* prevent it from being the active one */
				bond_set_backup_slave(slave);
			} else if (bond->params.mode != BOND_MODE_ACTIVEBACKUP) {
				/* make it immediately active */
				bond_set_active_slave(slave);
			} else if (slave != bond->primary_slave) {
				/* prevent it from being the active one */
				bond_set_backup_slave(slave);
			}

			bond_update_speed_duplex(slave);

			pr_info("%s: link status definitely up for interface %s, %u Mbps %s duplex.\n",
				bond->dev->name, slave->dev->name,
				slave->speed, slave->duplex ? "full" : "half");

			/* notify ad that the link status has changed */
			if (bond->params.mode == BOND_MODE_8023AD)
				bond_3ad_handle_link_change(slave, BOND_LINK_UP);

			if (bond_is_lb(bond))
				bond_alb_handle_link_change(bond, slave,
							    BOND_LINK_UP);

			if (!bond->curr_active_slave ||
			    (slave == bond->primary_slave))
				goto do_failover;

			continue;

		case BOND_LINK_DOWN:
			if (slave->link_failure_count < UINT_MAX)
				slave->link_failure_count++;

			slave->link = BOND_LINK_DOWN;

			if (bond->params.mode == BOND_MODE_ACTIVEBACKUP ||
			    bond->params.mode == BOND_MODE_8023AD)
				bond_set_slave_inactive_flags(slave);

			pr_info("%s: link status definitely down for interface %s, disabling it\n",
				bond->dev->name, slave->dev->name);

			if (bond->params.mode == BOND_MODE_8023AD)
				bond_3ad_handle_link_change(slave,
							    BOND_LINK_DOWN);

			if (bond_is_lb(bond))
				bond_alb_handle_link_change(bond, slave,
							    BOND_LINK_DOWN);

			if (slave == bond->curr_active_slave)
				goto do_failover;

			continue;

		default:
			pr_err("%s: invalid new link %d on slave %s\n",
			       bond->dev->name, slave->new_link,
			       slave->dev->name);
			slave->new_link = BOND_LINK_NOCHANGE;

			continue;
		}

do_failover:
		ASSERT_RTNL();
		block_netpoll_tx();
		write_lock_bh(&bond->curr_slave_lock);
		bond_select_active_slave(bond);
		write_unlock_bh(&bond->curr_slave_lock);
		unblock_netpoll_tx();
	}

	bond_set_carrier(bond);
}

/*
 * bond_mii_monitor
 *
 * Really a wrapper that splits the mii monitor into two phases: an
 * inspection, then (if inspection indicates something needs to be done)
 * an acquisition of appropriate locks followed by a commit phase to
 * implement whatever link state changes are indicated.
 */
void bond_mii_monitor(struct work_struct *work)
{
	struct bonding *bond = container_of(work, struct bonding,
					    mii_work.work);
	bool should_notify_peers = false;
	unsigned long delay;

	read_lock(&bond->lock);

	delay = msecs_to_jiffies(bond->params.miimon);

	if (bond->slave_cnt == 0)
		goto re_arm;

	should_notify_peers = bond_should_notify_peers(bond);

	if (bond_miimon_inspect(bond)) {
		read_unlock(&bond->lock);

		/* Race avoidance with bond_close cancel of workqueue */
		if (!rtnl_trylock()) {
			read_lock(&bond->lock);
			delay = 1;
			should_notify_peers = false;
			goto re_arm;
		}

		read_lock(&bond->lock);

		bond_miimon_commit(bond);

		read_unlock(&bond->lock);
		rtnl_unlock();	/* might sleep, hold no other locks */
		read_lock(&bond->lock);
	}

re_arm:
	if (bond->params.miimon)
		queue_delayed_work(bond->wq, &bond->mii_work, delay);

	read_unlock(&bond->lock);

	if (should_notify_peers) {
		if (!rtnl_trylock()) {
			read_lock(&bond->lock);
			bond->send_peer_notif++;
			read_unlock(&bond->lock);
			return;
		}
		call_netdevice_notifiers(NETDEV_NOTIFY_PEERS, bond->dev);
		rtnl_unlock();
	}
}

static int bond_has_this_ip(struct bonding *bond, __be32 ip)
{
	struct vlan_entry *vlan;
	struct net_device *vlan_dev;

	if (ip == bond_confirm_addr(bond->dev, 0, ip))
		return 1;

	list_for_each_entry(vlan, &bond->vlan_list, vlan_list) {
		rcu_read_lock();
		vlan_dev = __vlan_find_dev_deep(bond->dev, vlan->vlan_id);
		rcu_read_unlock();
		if (vlan_dev && ip == bond_confirm_addr(vlan_dev, 0, ip))
			return 1;
	}

	return 0;
}

/*
 * We go to the (large) trouble of VLAN tagging ARP frames because
 * switches in VLAN mode (especially if ports are configured as
 * "native" to a VLAN) might not pass non-tagged frames.
 */
static void bond_arp_send(struct net_device *slave_dev, int arp_op, __be32 dest_ip, __be32 src_ip, unsigned short vlan_id)
{
	struct sk_buff *skb;

	pr_debug("arp %d on slave %s: dst %x src %x vid %d\n", arp_op,
		 slave_dev->name, dest_ip, src_ip, vlan_id);

	skb = arp_create(arp_op, ETH_P_ARP, dest_ip, slave_dev, src_ip,
			 NULL, slave_dev->dev_addr, NULL);

	if (!skb) {
		pr_err("ARP packet allocation failed\n");
		return;
	}
	if (vlan_id) {
		skb = vlan_put_tag(skb, vlan_id);
		if (!skb) {
			pr_err("failed to insert VLAN tag\n");
			return;
		}
	}
	arp_xmit(skb);
}


static void bond_arp_send_all(struct bonding *bond, struct slave *slave)
{
	int i, vlan_id;
	__be32 *targets = bond->params.arp_targets;
	struct vlan_entry *vlan;
	struct net_device *vlan_dev = NULL;
	struct rtable *rt;

	for (i = 0; (i < BOND_MAX_ARP_TARGETS); i++) {
		__be32 addr;
		if (!targets[i])
			break;
		pr_debug("basa: target %x\n", targets[i]);
		if (!bond_vlan_used(bond)) {
			pr_debug("basa: empty vlan: arp_send\n");
			addr = bond_confirm_addr(bond->dev, targets[i], 0);
			bond_arp_send(slave->dev, ARPOP_REQUEST, targets[i],
				      addr, 0);
			continue;
		}

		/*
		 * If VLANs are configured, we do a route lookup to
		 * determine which VLAN interface would be used, so we
		 * can tag the ARP with the proper VLAN tag.
		 */
		rt = ip_route_output(dev_net(bond->dev), targets[i], 0,
				     RTO_ONLINK, 0);
		if (IS_ERR(rt)) {
			if (net_ratelimit()) {
				pr_warning("%s: no route to arp_ip_target %pI4\n",
					   bond->dev->name, &targets[i]);
			}
			continue;
		}

		/*
		 * This target is not on a VLAN
		 */
		if (rt->dst.dev == bond->dev) {
			ip_rt_put(rt);
			pr_debug("basa: rtdev == bond->dev: arp_send\n");
			addr = bond_confirm_addr(bond->dev, targets[i], 0);
			bond_arp_send(slave->dev, ARPOP_REQUEST, targets[i],
				      addr, 0);
			continue;
		}

		vlan_id = 0;
		list_for_each_entry(vlan, &bond->vlan_list, vlan_list) {
			rcu_read_lock();
			vlan_dev = __vlan_find_dev_deep(bond->dev,
							vlan->vlan_id);
			rcu_read_unlock();
			if (vlan_dev == rt->dst.dev) {
				vlan_id = vlan->vlan_id;
				pr_debug("basa: vlan match on %s %d\n",
				       vlan_dev->name, vlan_id);
				break;
			}
		}

		if (vlan_id && vlan_dev) {
			ip_rt_put(rt);
			addr = bond_confirm_addr(vlan_dev, targets[i], 0);
			bond_arp_send(slave->dev, ARPOP_REQUEST, targets[i],
				      addr, vlan_id);
			continue;
		}

		if (net_ratelimit()) {
			pr_warning("%s: no path to arp_ip_target %pI4 via rt.dev %s\n",
				   bond->dev->name, &targets[i],
				   rt->dst.dev ? rt->dst.dev->name : "NULL");
		}
		ip_rt_put(rt);
	}
}

static void bond_validate_arp(struct bonding *bond, struct slave *slave, __be32 sip, __be32 tip)
{
	int i;
	__be32 *targets = bond->params.arp_targets;

	for (i = 0; (i < BOND_MAX_ARP_TARGETS) && targets[i]; i++) {
		pr_debug("bva: sip %pI4 tip %pI4 t[%d] %pI4 bhti(tip) %d\n",
			 &sip, &tip, i, &targets[i],
			 bond_has_this_ip(bond, tip));
		if (sip == targets[i]) {
			if (bond_has_this_ip(bond, tip))
				slave->last_arp_rx = jiffies;
			return;
		}
	}
}

static int bond_arp_rcv(const struct sk_buff *skb, struct bonding *bond,
			struct slave *slave)
{
	struct arphdr *arp = (struct arphdr *)skb->data;
	unsigned char *arp_ptr;
	__be32 sip, tip;
	int alen;

	if (skb->protocol != __cpu_to_be16(ETH_P_ARP))
		return RX_HANDLER_ANOTHER;

	read_lock(&bond->lock);
	alen = arp_hdr_len(bond->dev);

	pr_debug("bond_arp_rcv: bond %s skb->dev %s\n",
		 bond->dev->name, skb->dev->name);

	if (alen > skb_headlen(skb)) {
		arp = kmalloc(alen, GFP_ATOMIC);
		if (!arp)
			goto out_unlock;
		if (skb_copy_bits(skb, 0, arp, alen) < 0)
			goto out_unlock;
	}

	if (arp->ar_hln != bond->dev->addr_len ||
	    skb->pkt_type == PACKET_OTHERHOST ||
	    skb->pkt_type == PACKET_LOOPBACK ||
	    arp->ar_hrd != htons(ARPHRD_ETHER) ||
	    arp->ar_pro != htons(ETH_P_IP) ||
	    arp->ar_pln != 4)
		goto out_unlock;

	arp_ptr = (unsigned char *)(arp + 1);
	arp_ptr += bond->dev->addr_len;
	memcpy(&sip, arp_ptr, 4);
	arp_ptr += 4 + bond->dev->addr_len;
	memcpy(&tip, arp_ptr, 4);

	pr_debug("bond_arp_rcv: %s %s/%d av %d sv %d sip %pI4 tip %pI4\n",
		 bond->dev->name, slave->dev->name, bond_slave_state(slave),
		 bond->params.arp_validate, slave_do_arp_validate(bond, slave),
		 &sip, &tip);

	/*
	 * Backup slaves won't see the ARP reply, but do come through
	 * here for each ARP probe (so we swap the sip/tip to validate
	 * the probe).  In a "redundant switch, common router" type of
	 * configuration, the ARP probe will (hopefully) travel from
	 * the active, through one switch, the router, then the other
	 * switch before reaching the backup.
	 */
	if (bond_is_active_slave(slave))
		bond_validate_arp(bond, slave, sip, tip);
	else
		bond_validate_arp(bond, slave, tip, sip);

out_unlock:
	read_unlock(&bond->lock);
	if (arp != (struct arphdr *)skb->data)
		kfree(arp);
	return RX_HANDLER_ANOTHER;
}

/*
 * this function is called regularly to monitor each slave's link
 * ensuring that traffic is being sent and received when arp monitoring
 * is used in load-balancing mode. if the adapter has been dormant, then an
 * arp is transmitted to generate traffic. see activebackup_arp_monitor for
 * arp monitoring in active backup mode.
 */
void bond_loadbalance_arp_mon(struct work_struct *work)
{
	struct bonding *bond = container_of(work, struct bonding,
					    arp_work.work);
	struct slave *slave, *oldcurrent;
	int do_failover = 0;
	int delta_in_ticks;
	int i;

	read_lock(&bond->lock);

	delta_in_ticks = msecs_to_jiffies(bond->params.arp_interval);

	if (bond->slave_cnt == 0)
		goto re_arm;

	read_lock(&bond->curr_slave_lock);
	oldcurrent = bond->curr_active_slave;
	read_unlock(&bond->curr_slave_lock);

	/* see if any of the previous devices are up now (i.e. they have
	 * xmt and rcv traffic). the curr_active_slave does not come into
	 * the picture unless it is null. also, slave->jiffies is not needed
	 * here because we send an arp on each slave and give a slave as
	 * long as it needs to get the tx/rx within the delta.
	 * TODO: what about up/down delay in arp mode? it wasn't here before
	 *       so it can wait
	 */
	bond_for_each_slave(bond, slave, i) {
		unsigned long trans_start = dev_trans_start(slave->dev);

		if (slave->link != BOND_LINK_UP) {
			if (time_in_range(jiffies,
				trans_start - delta_in_ticks,
				trans_start + delta_in_ticks) &&
			    time_in_range(jiffies,
				slave->dev->last_rx - delta_in_ticks,
				slave->dev->last_rx + delta_in_ticks)) {

				slave->link  = BOND_LINK_UP;
				bond_set_active_slave(slave);

				/* primary_slave has no meaning in round-robin
				 * mode. the window of a slave being up and
				 * curr_active_slave being null after enslaving
				 * is closed.
				 */
				if (!oldcurrent) {
					pr_info("%s: link status definitely up for interface %s, ",
						bond->dev->name,
						slave->dev->name);
					do_failover = 1;
				} else {
					pr_info("%s: interface %s is now up\n",
						bond->dev->name,
						slave->dev->name);
				}
			}
		} else {
			/* slave->link == BOND_LINK_UP */

			/* not all switches will respond to an arp request
			 * when the source ip is 0, so don't take the link down
			 * if we don't know our ip yet
			 */
			if (!time_in_range(jiffies,
				trans_start - delta_in_ticks,
				trans_start + 2 * delta_in_ticks) ||
			    !time_in_range(jiffies,
				slave->dev->last_rx - delta_in_ticks,
				slave->dev->last_rx + 2 * delta_in_ticks)) {

				slave->link  = BOND_LINK_DOWN;
				bond_set_backup_slave(slave);

				if (slave->link_failure_count < UINT_MAX)
					slave->link_failure_count++;

				pr_info("%s: interface %s is now down.\n",
					bond->dev->name,
					slave->dev->name);

				if (slave == oldcurrent)
					do_failover = 1;
			}
		}

		/* note: if switch is in round-robin mode, all links
		 * must tx arp to ensure all links rx an arp - otherwise
		 * links may oscillate or not come up at all; if switch is
		 * in something like xor mode, there is nothing we can
		 * do - all replies will be rx'ed on same link causing slaves
		 * to be unstable during low/no traffic periods
		 */
		if (IS_UP(slave->dev))
			bond_arp_send_all(bond, slave);
	}

	if (do_failover) {
		block_netpoll_tx();
		write_lock_bh(&bond->curr_slave_lock);

		bond_select_active_slave(bond);

		write_unlock_bh(&bond->curr_slave_lock);
		unblock_netpoll_tx();
	}

re_arm:
	if (bond->params.arp_interval)
		queue_delayed_work(bond->wq, &bond->arp_work, delta_in_ticks);

	read_unlock(&bond->lock);
}

/*
 * Called to inspect slaves for active-backup mode ARP monitor link state
 * changes.  Sets new_link in slaves to specify what action should take
 * place for the slave.  Returns 0 if no changes are found, >0 if changes
 * to link states must be committed.
 *
 * Called with bond->lock held for read.
 */
static int bond_ab_arp_inspect(struct bonding *bond, int delta_in_ticks)
{
	struct slave *slave;
	int i, commit = 0;
	unsigned long trans_start;

	bond_for_each_slave(bond, slave, i) {
		slave->new_link = BOND_LINK_NOCHANGE;

		if (slave->link != BOND_LINK_UP) {
			if (time_in_range(jiffies,
				slave_last_rx(bond, slave) - delta_in_ticks,
				slave_last_rx(bond, slave) + delta_in_ticks)) {

				slave->new_link = BOND_LINK_UP;
				commit++;
			}

			continue;
		}

		/*
		 * Give slaves 2*delta after being enslaved or made
		 * active.  This avoids bouncing, as the last receive
		 * times need a full ARP monitor cycle to be updated.
		 */
		if (time_in_range(jiffies,
				  slave->jiffies - delta_in_ticks,
				  slave->jiffies + 2 * delta_in_ticks))
			continue;

		/*
		 * Backup slave is down if:
		 * - No current_arp_slave AND
		 * - more than 3*delta since last receive AND
		 * - the bond has an IP address
		 *
		 * Note: a non-null current_arp_slave indicates
		 * the curr_active_slave went down and we are
		 * searching for a new one; under this condition
		 * we only take the curr_active_slave down - this
		 * gives each slave a chance to tx/rx traffic
		 * before being taken out
		 */
		if (!bond_is_active_slave(slave) &&
		    !bond->current_arp_slave &&
		    !time_in_range(jiffies,
			slave_last_rx(bond, slave) - delta_in_ticks,
			slave_last_rx(bond, slave) + 3 * delta_in_ticks)) {

			slave->new_link = BOND_LINK_DOWN;
			commit++;
		}

		/*
		 * Active slave is down if:
		 * - more than 2*delta since transmitting OR
		 * - (more than 2*delta since receive AND
		 *    the bond has an IP address)
		 */
		trans_start = dev_trans_start(slave->dev);
		if (bond_is_active_slave(slave) &&
		    (!time_in_range(jiffies,
			trans_start - delta_in_ticks,
			trans_start + 2 * delta_in_ticks) ||
		     !time_in_range(jiffies,
			slave_last_rx(bond, slave) - delta_in_ticks,
			slave_last_rx(bond, slave) + 2 * delta_in_ticks))) {

			slave->new_link = BOND_LINK_DOWN;
			commit++;
		}
	}

	return commit;
}

/*
 * Called to commit link state changes noted by inspection step of
 * active-backup mode ARP monitor.
 *
 * Called with RTNL and bond->lock for read.
 */
static void bond_ab_arp_commit(struct bonding *bond, int delta_in_ticks)
{
	struct slave *slave;
	int i;
	unsigned long trans_start;

	bond_for_each_slave(bond, slave, i) {
		switch (slave->new_link) {
		case BOND_LINK_NOCHANGE:
			continue;

		case BOND_LINK_UP:
			trans_start = dev_trans_start(slave->dev);
			if ((!bond->curr_active_slave &&
			     time_in_range(jiffies,
					   trans_start - delta_in_ticks,
					   trans_start + delta_in_ticks)) ||
			    bond->curr_active_slave != slave) {
				slave->link = BOND_LINK_UP;
				if (bond->current_arp_slave) {
					bond_set_slave_inactive_flags(
						bond->current_arp_slave);
					bond->current_arp_slave = NULL;
				}

				pr_info("%s: link status definitely up for interface %s.\n",
					bond->dev->name, slave->dev->name);

				if (!bond->curr_active_slave ||
				    (slave == bond->primary_slave))
					goto do_failover;

			}

			continue;

		case BOND_LINK_DOWN:
			if (slave->link_failure_count < UINT_MAX)
				slave->link_failure_count++;

			slave->link = BOND_LINK_DOWN;
			bond_set_slave_inactive_flags(slave);

			pr_info("%s: link status definitely down for interface %s, disabling it\n",
				bond->dev->name, slave->dev->name);

			if (slave == bond->curr_active_slave) {
				bond->current_arp_slave = NULL;
				goto do_failover;
			}

			continue;

		default:
			pr_err("%s: impossible: new_link %d on slave %s\n",
			       bond->dev->name, slave->new_link,
			       slave->dev->name);
			continue;
		}

do_failover:
		ASSERT_RTNL();
		block_netpoll_tx();
		write_lock_bh(&bond->curr_slave_lock);
		bond_select_active_slave(bond);
		write_unlock_bh(&bond->curr_slave_lock);
		unblock_netpoll_tx();
	}

	bond_set_carrier(bond);
}

/*
 * Send ARP probes for active-backup mode ARP monitor.
 *
 * Called with bond->lock held for read.
 */
static void bond_ab_arp_probe(struct bonding *bond)
{
	struct slave *slave;
	int i;

	read_lock(&bond->curr_slave_lock);

	if (bond->current_arp_slave && bond->curr_active_slave)
		pr_info("PROBE: c_arp %s && cas %s BAD\n",
			bond->current_arp_slave->dev->name,
			bond->curr_active_slave->dev->name);

	if (bond->curr_active_slave) {
		bond_arp_send_all(bond, bond->curr_active_slave);
		read_unlock(&bond->curr_slave_lock);
		return;
	}

	read_unlock(&bond->curr_slave_lock);

	/* if we don't have a curr_active_slave, search for the next available
	 * backup slave from the current_arp_slave and make it the candidate
	 * for becoming the curr_active_slave
	 */

	if (!bond->current_arp_slave) {
		bond->current_arp_slave = bond->first_slave;
		if (!bond->current_arp_slave)
			return;
	}

	bond_set_slave_inactive_flags(bond->current_arp_slave);

	/* search for next candidate */
	bond_for_each_slave_from(bond, slave, i, bond->current_arp_slave->next) {
		if (IS_UP(slave->dev)) {
			slave->link = BOND_LINK_BACK;
			bond_set_slave_active_flags(slave);
			bond_arp_send_all(bond, slave);
			slave->jiffies = jiffies;
			bond->current_arp_slave = slave;
			break;
		}

		/* if the link state is up at this point, we
		 * mark it down - this can happen if we have
		 * simultaneous link failures and
		 * reselect_active_interface doesn't make this
		 * one the current slave so it is still marked
		 * up when it is actually down
		 */
		if (slave->link == BOND_LINK_UP) {
			slave->link = BOND_LINK_DOWN;
			if (slave->link_failure_count < UINT_MAX)
				slave->link_failure_count++;

			bond_set_slave_inactive_flags(slave);

			pr_info("%s: backup interface %s is now down.\n",
				bond->dev->name, slave->dev->name);
		}
	}
}

void bond_activebackup_arp_mon(struct work_struct *work)
{
	struct bonding *bond = container_of(work, struct bonding,
					    arp_work.work);
	bool should_notify_peers = false;
	int delta_in_ticks;

	read_lock(&bond->lock);

	delta_in_ticks = msecs_to_jiffies(bond->params.arp_interval);

	if (bond->slave_cnt == 0)
		goto re_arm;

	should_notify_peers = bond_should_notify_peers(bond);

	if (bond_ab_arp_inspect(bond, delta_in_ticks)) {
		read_unlock(&bond->lock);

		/* Race avoidance with bond_close flush of workqueue */
		if (!rtnl_trylock()) {
			read_lock(&bond->lock);
			delta_in_ticks = 1;
			should_notify_peers = false;
			goto re_arm;
		}

		read_lock(&bond->lock);

		bond_ab_arp_commit(bond, delta_in_ticks);

		read_unlock(&bond->lock);
		rtnl_unlock();
		read_lock(&bond->lock);
	}

	bond_ab_arp_probe(bond);

re_arm:
	if (bond->params.arp_interval)
		queue_delayed_work(bond->wq, &bond->arp_work, delta_in_ticks);

	read_unlock(&bond->lock);

	if (should_notify_peers) {
		if (!rtnl_trylock()) {
			read_lock(&bond->lock);
			bond->send_peer_notif++;
			read_unlock(&bond->lock);
			return;
		}
		call_netdevice_notifiers(NETDEV_NOTIFY_PEERS, bond->dev);
		rtnl_unlock();
	}
}

/*-------------------------- netdev event handling --------------------------*/

/*
 * Change device name
 */
static int bond_event_changename(struct bonding *bond)
{
	bond_remove_proc_entry(bond);
	bond_create_proc_entry(bond);

	bond_debug_reregister(bond);

	return NOTIFY_DONE;
}

static int bond_master_netdev_event(unsigned long event,
				    struct net_device *bond_dev)
{
	struct bonding *event_bond = netdev_priv(bond_dev);

	switch (event) {
	case NETDEV_CHANGENAME:
		return bond_event_changename(event_bond);
	case NETDEV_UNREGISTER:
		bond_remove_proc_entry(event_bond);
		break;
	case NETDEV_REGISTER:
		bond_create_proc_entry(event_bond);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static int bond_slave_netdev_event(unsigned long event,
				   struct net_device *slave_dev)
{
	struct net_device *bond_dev = slave_dev->master;
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave = NULL;

	switch (event) {
	case NETDEV_UNREGISTER:
		if (bond_dev) {
			if (bond->setup_by_slave)
				bond_release_and_destroy(bond_dev, slave_dev);
			else
				bond_release(bond_dev, slave_dev);
		}
		break;
	case NETDEV_UP:
	case NETDEV_CHANGE:
		slave = bond_get_slave_by_dev(bond, slave_dev);
		if (slave) {
			u32 old_speed = slave->speed;
			u8  old_duplex = slave->duplex;

			bond_update_speed_duplex(slave);

			if (bond->params.mode == BOND_MODE_8023AD) {
				if (old_speed != slave->speed)
					bond_3ad_adapter_speed_changed(slave);
				if (old_duplex != slave->duplex)
					bond_3ad_adapter_duplex_changed(slave);
			}
		}

		break;
	case NETDEV_DOWN:
		/*
		 * ... Or is it this?
		 */
		break;
	case NETDEV_CHANGEMTU:
		/*
		 * TODO: Should slaves be allowed to
		 * independently alter their MTU?  For
		 * an active-backup bond, slaves need
		 * not be the same type of device, so
		 * MTUs may vary.  For other modes,
		 * slaves arguably should have the
		 * same MTUs. To do this, we'd need to
		 * take over the slave's change_mtu
		 * function for the duration of their
		 * servitude.
		 */
		break;
	case NETDEV_CHANGENAME:
		/*
		 * TODO: handle changing the primary's name
		 */
		break;
	case NETDEV_FEAT_CHANGE:
		bond_compute_features(bond);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

/*
 * bond_netdev_event: handle netdev notifier chain events.
 *
 * This function receives events for the netdev chain.  The caller (an
 * ioctl handler calling blocking_notifier_call_chain) holds the necessary
 * locks for us to safely manipulate the slave devices (RTNL lock,
 * dev_probe_lock).
 */
static int bond_netdev_event(struct notifier_block *this,
			     unsigned long event, void *ptr)
{
	struct net_device *event_dev = (struct net_device *)ptr;

	pr_debug("event_dev: %s, event: %lx\n",
		 event_dev ? event_dev->name : "None",
		 event);

	if (!(event_dev->priv_flags & IFF_BONDING))
		return NOTIFY_DONE;

	if (event_dev->flags & IFF_MASTER) {
		pr_debug("IFF_MASTER\n");
		return bond_master_netdev_event(event, event_dev);
	}

	if (event_dev->flags & IFF_SLAVE) {
		pr_debug("IFF_SLAVE\n");
		return bond_slave_netdev_event(event, event_dev);
	}

	return NOTIFY_DONE;
}

static struct notifier_block bond_netdev_notifier = {
	.notifier_call = bond_netdev_event,
};

/*---------------------------- Hashing Policies -----------------------------*/

/*
 * Hash for the output device based upon layer 2 and layer 3 data. If
 * the packet is not IP mimic bond_xmit_hash_policy_l2()
 */
static int bond_xmit_hash_policy_l23(struct sk_buff *skb, int count)
{
	struct ethhdr *data = (struct ethhdr *)skb->data;
	struct iphdr *iph = ip_hdr(skb);

	if (skb->protocol == htons(ETH_P_IP)) {
		return ((ntohl(iph->saddr ^ iph->daddr) & 0xffff) ^
			(data->h_dest[5] ^ data->h_source[5])) % count;
	}

	return (data->h_dest[5] ^ data->h_source[5]) % count;
}

/*
 * Hash for the output device based upon layer 3 and layer 4 data. If
 * the packet is a frag or not TCP or UDP, just use layer 3 data.  If it is
 * altogether not IP, mimic bond_xmit_hash_policy_l2()
 */
static int bond_xmit_hash_policy_l34(struct sk_buff *skb, int count)
{
	struct ethhdr *data = (struct ethhdr *)skb->data;
	struct iphdr *iph = ip_hdr(skb);
	__be16 *layer4hdr = (__be16 *)((u32 *)iph + iph->ihl);
	int layer4_xor = 0;

	if (skb->protocol == htons(ETH_P_IP)) {
		if (!ip_is_fragment(iph) &&
		    (iph->protocol == IPPROTO_TCP ||
		     iph->protocol == IPPROTO_UDP)) {
			layer4_xor = ntohs((*layer4hdr ^ *(layer4hdr + 1)));
		}
		return (layer4_xor ^
			((ntohl(iph->saddr ^ iph->daddr)) & 0xffff)) % count;

	}

	return (data->h_dest[5] ^ data->h_source[5]) % count;
}

/*
 * Hash for the output device based upon layer 2 data
 */
static int bond_xmit_hash_policy_l2(struct sk_buff *skb, int count)
{
	struct ethhdr *data = (struct ethhdr *)skb->data;

	return (data->h_dest[5] ^ data->h_source[5]) % count;
}

/*-------------------------- Device entry points ----------------------------*/

static int bond_open(struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave;
	int i;

	/* reset slave->backup and slave->inactive */
	read_lock(&bond->lock);
	if (bond->slave_cnt > 0) {
		read_lock(&bond->curr_slave_lock);
		bond_for_each_slave(bond, slave, i) {
			if ((bond->params.mode == BOND_MODE_ACTIVEBACKUP)
				&& (slave != bond->curr_active_slave)) {
				bond_set_slave_inactive_flags(slave);
			} else {
				bond_set_slave_active_flags(slave);
			}
		}
		read_unlock(&bond->curr_slave_lock);
	}
	read_unlock(&bond->lock);

	INIT_DELAYED_WORK(&bond->mcast_work, bond_resend_igmp_join_requests_delayed);

	if (bond_is_lb(bond)) {
		/* bond_alb_initialize must be called before the timer
		 * is started.
		 */
		if (bond_alb_initialize(bond, (bond->params.mode == BOND_MODE_ALB))) {
			/* something went wrong - fail the open operation */
			return -ENOMEM;
		}

		INIT_DELAYED_WORK(&bond->alb_work, bond_alb_monitor);
		queue_delayed_work(bond->wq, &bond->alb_work, 0);
	}

	if (bond->params.miimon) {  /* link check interval, in milliseconds. */
		INIT_DELAYED_WORK(&bond->mii_work, bond_mii_monitor);
		queue_delayed_work(bond->wq, &bond->mii_work, 0);
	}

	if (bond->params.arp_interval) {  /* arp interval, in milliseconds. */
		if (bond->params.mode == BOND_MODE_ACTIVEBACKUP)
			INIT_DELAYED_WORK(&bond->arp_work,
					  bond_activebackup_arp_mon);
		else
			INIT_DELAYED_WORK(&bond->arp_work,
					  bond_loadbalance_arp_mon);

		queue_delayed_work(bond->wq, &bond->arp_work, 0);
		if (bond->params.arp_validate)
			bond->recv_probe = bond_arp_rcv;
	}

	if (bond->params.mode == BOND_MODE_8023AD) {
		INIT_DELAYED_WORK(&bond->ad_work, bond_3ad_state_machine_handler);
		queue_delayed_work(bond->wq, &bond->ad_work, 0);
		/* register to receive LACPDUs */
		bond->recv_probe = bond_3ad_lacpdu_recv;
		bond_3ad_initiate_agg_selection(bond, 1);
	}

	return 0;
}

static int bond_close(struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);

	write_lock_bh(&bond->lock);

	bond->send_peer_notif = 0;

	write_unlock_bh(&bond->lock);

	if (bond->params.miimon) {  /* link check interval, in milliseconds. */
		cancel_delayed_work_sync(&bond->mii_work);
	}

	if (bond->params.arp_interval) {  /* arp interval, in milliseconds. */
		cancel_delayed_work_sync(&bond->arp_work);
	}

	switch (bond->params.mode) {
	case BOND_MODE_8023AD:
		cancel_delayed_work_sync(&bond->ad_work);
		break;
	case BOND_MODE_TLB:
	case BOND_MODE_ALB:
		cancel_delayed_work_sync(&bond->alb_work);
		break;
	default:
		break;
	}

	if (delayed_work_pending(&bond->mcast_work))
		cancel_delayed_work_sync(&bond->mcast_work);

	if (bond_is_lb(bond)) {
		/* Must be called only after all
		 * slaves have been released
		 */
		bond_alb_deinitialize(bond);
	}
	bond->recv_probe = NULL;

	return 0;
}

static struct rtnl_link_stats64 *bond_get_stats(struct net_device *bond_dev,
						struct rtnl_link_stats64 *stats)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct rtnl_link_stats64 temp;
	struct slave *slave;
	int i;

	memset(stats, 0, sizeof(*stats));

	read_lock_bh(&bond->lock);

	bond_for_each_slave(bond, slave, i) {
		const struct rtnl_link_stats64 *sstats =
			dev_get_stats(slave->dev, &temp);

		stats->rx_packets += sstats->rx_packets;
		stats->rx_bytes += sstats->rx_bytes;
		stats->rx_errors += sstats->rx_errors;
		stats->rx_dropped += sstats->rx_dropped;

		stats->tx_packets += sstats->tx_packets;
		stats->tx_bytes += sstats->tx_bytes;
		stats->tx_errors += sstats->tx_errors;
		stats->tx_dropped += sstats->tx_dropped;

		stats->multicast += sstats->multicast;
		stats->collisions += sstats->collisions;

		stats->rx_length_errors += sstats->rx_length_errors;
		stats->rx_over_errors += sstats->rx_over_errors;
		stats->rx_crc_errors += sstats->rx_crc_errors;
		stats->rx_frame_errors += sstats->rx_frame_errors;
		stats->rx_fifo_errors += sstats->rx_fifo_errors;
		stats->rx_missed_errors += sstats->rx_missed_errors;

		stats->tx_aborted_errors += sstats->tx_aborted_errors;
		stats->tx_carrier_errors += sstats->tx_carrier_errors;
		stats->tx_fifo_errors += sstats->tx_fifo_errors;
		stats->tx_heartbeat_errors += sstats->tx_heartbeat_errors;
		stats->tx_window_errors += sstats->tx_window_errors;
	}

	read_unlock_bh(&bond->lock);

	return stats;
}

static int bond_do_ioctl(struct net_device *bond_dev, struct ifreq *ifr, int cmd)
{
	struct net_device *slave_dev = NULL;
	struct ifbond k_binfo;
	struct ifbond __user *u_binfo = NULL;
	struct ifslave k_sinfo;
	struct ifslave __user *u_sinfo = NULL;
	struct mii_ioctl_data *mii = NULL;
	int res = 0;

	pr_debug("bond_ioctl: master=%s, cmd=%d\n", bond_dev->name, cmd);

	switch (cmd) {
	case SIOCGMIIPHY:
		mii = if_mii(ifr);
		if (!mii)
			return -EINVAL;

		mii->phy_id = 0;
		/* Fall Through */
	case SIOCGMIIREG:
		/*
		 * We do this again just in case we were called by SIOCGMIIREG
		 * instead of SIOCGMIIPHY.
		 */
		mii = if_mii(ifr);
		if (!mii)
			return -EINVAL;


		if (mii->reg_num == 1) {
			struct bonding *bond = netdev_priv(bond_dev);
			mii->val_out = 0;
			read_lock(&bond->lock);
			read_lock(&bond->curr_slave_lock);
			if (netif_carrier_ok(bond->dev))
				mii->val_out = BMSR_LSTATUS;

			read_unlock(&bond->curr_slave_lock);
			read_unlock(&bond->lock);
		}

		return 0;
	case BOND_INFO_QUERY_OLD:
	case SIOCBONDINFOQUERY:
		u_binfo = (struct ifbond __user *)ifr->ifr_data;

		if (copy_from_user(&k_binfo, u_binfo, sizeof(ifbond)))
			return -EFAULT;

		res = bond_info_query(bond_dev, &k_binfo);
		if (res == 0 &&
		    copy_to_user(u_binfo, &k_binfo, sizeof(ifbond)))
			return -EFAULT;

		return res;
	case BOND_SLAVE_INFO_QUERY_OLD:
	case SIOCBONDSLAVEINFOQUERY:
		u_sinfo = (struct ifslave __user *)ifr->ifr_data;

		if (copy_from_user(&k_sinfo, u_sinfo, sizeof(ifslave)))
			return -EFAULT;

		res = bond_slave_info_query(bond_dev, &k_sinfo);
		if (res == 0 &&
		    copy_to_user(u_sinfo, &k_sinfo, sizeof(ifslave)))
			return -EFAULT;

		return res;
	default:
		/* Go on */
		break;
	}

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	slave_dev = dev_get_by_name(dev_net(bond_dev), ifr->ifr_slave);

	pr_debug("slave_dev=%p:\n", slave_dev);

	if (!slave_dev)
		res = -ENODEV;
	else {
		pr_debug("slave_dev->name=%s:\n", slave_dev->name);
		switch (cmd) {
		case BOND_ENSLAVE_OLD:
		case SIOCBONDENSLAVE:
			res = bond_enslave(bond_dev, slave_dev);
			break;
		case BOND_RELEASE_OLD:
		case SIOCBONDRELEASE:
			res = bond_release(bond_dev, slave_dev);
			break;
		case BOND_SETHWADDR_OLD:
		case SIOCBONDSETHWADDR:
			res = bond_sethwaddr(bond_dev, slave_dev);
			break;
		case BOND_CHANGE_ACTIVE_OLD:
		case SIOCBONDCHANGEACTIVE:
			res = bond_ioctl_change_active(bond_dev, slave_dev);
			break;
		default:
			res = -EOPNOTSUPP;
		}

		dev_put(slave_dev);
	}

	return res;
}

static bool bond_addr_in_mc_list(unsigned char *addr,
				 struct netdev_hw_addr_list *list,
				 int addrlen)
{
	struct netdev_hw_addr *ha;

	netdev_hw_addr_list_for_each(ha, list)
		if (!memcmp(ha->addr, addr, addrlen))
			return true;

	return false;
}

static void bond_change_rx_flags(struct net_device *bond_dev, int change)
{
	struct bonding *bond = netdev_priv(bond_dev);

	if (change & IFF_PROMISC)
		bond_set_promiscuity(bond,
				     bond_dev->flags & IFF_PROMISC ? 1 : -1);

	if (change & IFF_ALLMULTI)
		bond_set_allmulti(bond,
				  bond_dev->flags & IFF_ALLMULTI ? 1 : -1);
}

static void bond_set_multicast_list(struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct netdev_hw_addr *ha;
	bool found;

	read_lock(&bond->lock);

	/* looking for addresses to add to slaves' mc list */
	netdev_for_each_mc_addr(ha, bond_dev) {
		found = bond_addr_in_mc_list(ha->addr, &bond->mc_list,
					     bond_dev->addr_len);
		if (!found)
			bond_mc_add(bond, ha->addr);
	}

	/* looking for addresses to delete from slaves' list */
	netdev_hw_addr_list_for_each(ha, &bond->mc_list) {
		found = bond_addr_in_mc_list(ha->addr, &bond_dev->mc,
					     bond_dev->addr_len);
		if (!found)
			bond_mc_del(bond, ha->addr);
	}

	/* save master's multicast list */
	__hw_addr_flush(&bond->mc_list);
	__hw_addr_add_multiple(&bond->mc_list, &bond_dev->mc,
			       bond_dev->addr_len, NETDEV_HW_ADDR_T_MULTICAST);

	read_unlock(&bond->lock);
}

static int bond_neigh_init(struct neighbour *n)
{
	struct bonding *bond = netdev_priv(n->dev);
	struct slave *slave = bond->first_slave;
	const struct net_device_ops *slave_ops;
	struct neigh_parms parms;
	int ret;

	if (!slave)
		return 0;

	slave_ops = slave->dev->netdev_ops;

	if (!slave_ops->ndo_neigh_setup)
		return 0;

	parms.neigh_setup = NULL;
	parms.neigh_cleanup = NULL;
	ret = slave_ops->ndo_neigh_setup(slave->dev, &parms);
	if (ret)
		return ret;

	/*
	 * Assign slave's neigh_cleanup to neighbour in case cleanup is called
	 * after the last slave has been detached.  Assumes that all slaves
	 * utilize the same neigh_cleanup (true at this writing as only user
	 * is ipoib).
	 */
	n->parms->neigh_cleanup = parms.neigh_cleanup;

	if (!parms.neigh_setup)
		return 0;

	return parms.neigh_setup(n);
}

/*
 * The bonding ndo_neigh_setup is called at init time beofre any
 * slave exists. So we must declare proxy setup function which will
 * be used at run time to resolve the actual slave neigh param setup.
 */
static int bond_neigh_setup(struct net_device *dev,
			    struct neigh_parms *parms)
{
	parms->neigh_setup   = bond_neigh_init;

	return 0;
}

/*
 * Change the MTU of all of a master's slaves to match the master
 */
static int bond_change_mtu(struct net_device *bond_dev, int new_mtu)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave, *stop_at;
	int res = 0;
	int i;

	pr_debug("bond=%p, name=%s, new_mtu=%d\n", bond,
		 (bond_dev ? bond_dev->name : "None"), new_mtu);

	/* Can't hold bond->lock with bh disabled here since
	 * some base drivers panic. On the other hand we can't
	 * hold bond->lock without bh disabled because we'll
	 * deadlock. The only solution is to rely on the fact
	 * that we're under rtnl_lock here, and the slaves
	 * list won't change. This doesn't solve the problem
	 * of setting the slave's MTU while it is
	 * transmitting, but the assumption is that the base
	 * driver can handle that.
	 *
	 * TODO: figure out a way to safely iterate the slaves
	 * list, but without holding a lock around the actual
	 * call to the base driver.
	 */

	bond_for_each_slave(bond, slave, i) {
		pr_debug("s %p s->p %p c_m %p\n",
			 slave,
			 slave->prev,
			 slave->dev->netdev_ops->ndo_change_mtu);

		res = dev_set_mtu(slave->dev, new_mtu);

		if (res) {
			/* If we failed to set the slave's mtu to the new value
			 * we must abort the operation even in ACTIVE_BACKUP
			 * mode, because if we allow the backup slaves to have
			 * different mtu values than the active slave we'll
			 * need to change their mtu when doing a failover. That
			 * means changing their mtu from timer context, which
			 * is probably not a good idea.
			 */
			pr_debug("err %d %s\n", res, slave->dev->name);
			goto unwind;
		}
	}

	bond_dev->mtu = new_mtu;

	return 0;

unwind:
	/* unwind from head to the slave that failed */
	stop_at = slave;
	bond_for_each_slave_from_to(bond, slave, i, bond->first_slave, stop_at) {
		int tmp_res;

		tmp_res = dev_set_mtu(slave->dev, bond_dev->mtu);
		if (tmp_res) {
			pr_debug("unwind err %d dev %s\n",
				 tmp_res, slave->dev->name);
		}
	}

	return res;
}

/*
 * Change HW address
 *
 * Note that many devices must be down to change the HW address, and
 * downing the master releases all slaves.  We can make bonds full of
 * bonding devices to test this, however.
 */
static int bond_set_mac_address(struct net_device *bond_dev, void *addr)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct sockaddr *sa = addr, tmp_sa;
	struct slave *slave, *stop_at;
	int res = 0;
	int i;

	if (bond->params.mode == BOND_MODE_ALB)
		return bond_alb_set_mac_address(bond_dev, addr);


	pr_debug("bond=%p, name=%s\n",
		 bond, bond_dev ? bond_dev->name : "None");

	/*
	 * If fail_over_mac is set to active, do nothing and return
	 * success.  Returning an error causes ifenslave to fail.
	 */
	if (bond->params.fail_over_mac == BOND_FOM_ACTIVE)
		return 0;

	if (!is_valid_ether_addr(sa->sa_data))
		return -EADDRNOTAVAIL;

	/* Can't hold bond->lock with bh disabled here since
	 * some base drivers panic. On the other hand we can't
	 * hold bond->lock without bh disabled because we'll
	 * deadlock. The only solution is to rely on the fact
	 * that we're under rtnl_lock here, and the slaves
	 * list won't change. This doesn't solve the problem
	 * of setting the slave's hw address while it is
	 * transmitting, but the assumption is that the base
	 * driver can handle that.
	 *
	 * TODO: figure out a way to safely iterate the slaves
	 * list, but without holding a lock around the actual
	 * call to the base driver.
	 */

	bond_for_each_slave(bond, slave, i) {
		const struct net_device_ops *slave_ops = slave->dev->netdev_ops;
		pr_debug("slave %p %s\n", slave, slave->dev->name);

		if (slave_ops->ndo_set_mac_address == NULL) {
			res = -EOPNOTSUPP;
			pr_debug("EOPNOTSUPP %s\n", slave->dev->name);
			goto unwind;
		}

		res = dev_set_mac_address(slave->dev, addr);
		if (res) {
			/* TODO: consider downing the slave
			 * and retry ?
			 * User should expect communications
			 * breakage anyway until ARP finish
			 * updating, so...
			 */
			pr_debug("err %d %s\n", res, slave->dev->name);
			goto unwind;
		}
	}

	/* success */
	memcpy(bond_dev->dev_addr, sa->sa_data, bond_dev->addr_len);
	return 0;

unwind:
	memcpy(tmp_sa.sa_data, bond_dev->dev_addr, bond_dev->addr_len);
	tmp_sa.sa_family = bond_dev->type;

	/* unwind from head to the slave that failed */
	stop_at = slave;
	bond_for_each_slave_from_to(bond, slave, i, bond->first_slave, stop_at) {
		int tmp_res;

		tmp_res = dev_set_mac_address(slave->dev, &tmp_sa);
		if (tmp_res) {
			pr_debug("unwind err %d dev %s\n",
				 tmp_res, slave->dev->name);
		}
	}

	return res;
}

static int bond_xmit_roundrobin(struct sk_buff *skb, struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave, *start_at;
	int i, slave_no, res = 1;
	struct iphdr *iph = ip_hdr(skb);

	/*
	 * Start with the curr_active_slave that joined the bond as the
	 * default for sending IGMP traffic.  For failover purposes one
	 * needs to maintain some consistency for the interface that will
	 * send the join/membership reports.  The curr_active_slave found
	 * will send all of this type of traffic.
	 */
	if ((iph->protocol == IPPROTO_IGMP) &&
	    (skb->protocol == htons(ETH_P_IP))) {

		read_lock(&bond->curr_slave_lock);
		slave = bond->curr_active_slave;
		read_unlock(&bond->curr_slave_lock);

		if (!slave)
			goto out;
	} else {
		/*
		 * Concurrent TX may collide on rr_tx_counter; we accept
		 * that as being rare enough not to justify using an
		 * atomic op here.
		 */
		slave_no = bond->rr_tx_counter++ % bond->slave_cnt;

		bond_for_each_slave(bond, slave, i) {
			slave_no--;
			if (slave_no < 0)
				break;
		}
	}

	start_at = slave;
	bond_for_each_slave_from(bond, slave, i, start_at) {
		if (IS_UP(slave->dev) &&
		    (slave->link == BOND_LINK_UP) &&
		    bond_is_active_slave(slave)) {
			res = bond_dev_queue_xmit(bond, skb, slave->dev);
			break;
		}
	}

out:
	if (res) {
		/* no suitable interface, frame not sent */
		kfree_skb(skb);
	}

	return NETDEV_TX_OK;
}


/*
 * in active-backup mode, we know that bond->curr_active_slave is always valid if
 * the bond has a usable interface.
 */
static int bond_xmit_activebackup(struct sk_buff *skb, struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	int res = 1;

	read_lock(&bond->curr_slave_lock);

	if (bond->curr_active_slave)
		res = bond_dev_queue_xmit(bond, skb,
			bond->curr_active_slave->dev);

	read_unlock(&bond->curr_slave_lock);

	if (res)
		/* no suitable interface, frame not sent */
		kfree_skb(skb);

	return NETDEV_TX_OK;
}

/*
 * In bond_xmit_xor() , we determine the output device by using a pre-
 * determined xmit_hash_policy(), If the selected device is not enabled,
 * find the next active slave.
 */
static int bond_xmit_xor(struct sk_buff *skb, struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave, *start_at;
	int slave_no;
	int i;
	int res = 1;

	slave_no = bond->xmit_hash_policy(skb, bond->slave_cnt);

	bond_for_each_slave(bond, slave, i) {
		slave_no--;
		if (slave_no < 0)
			break;
	}

	start_at = slave;

	bond_for_each_slave_from(bond, slave, i, start_at) {
		if (IS_UP(slave->dev) &&
		    (slave->link == BOND_LINK_UP) &&
		    bond_is_active_slave(slave)) {
			res = bond_dev_queue_xmit(bond, skb, slave->dev);
			break;
		}
	}

	if (res) {
		/* no suitable interface, frame not sent */
		kfree_skb(skb);
	}

	return NETDEV_TX_OK;
}

/*
 * in broadcast mode, we send everything to all usable interfaces.
 */
static int bond_xmit_broadcast(struct sk_buff *skb, struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave, *start_at;
	struct net_device *tx_dev = NULL;
	int i;
	int res = 1;

	read_lock(&bond->curr_slave_lock);
	start_at = bond->curr_active_slave;
	read_unlock(&bond->curr_slave_lock);

	if (!start_at)
		goto out;

	bond_for_each_slave_from(bond, slave, i, start_at) {
		if (IS_UP(slave->dev) &&
		    (slave->link == BOND_LINK_UP) &&
		    bond_is_active_slave(slave)) {
			if (tx_dev) {
				struct sk_buff *skb2 = skb_clone(skb, GFP_ATOMIC);
				if (!skb2) {
					pr_err("%s: Error: bond_xmit_broadcast(): skb_clone() failed\n",
					       bond_dev->name);
					continue;
				}

				res = bond_dev_queue_xmit(bond, skb2, tx_dev);
				if (res) {
					kfree_skb(skb2);
					continue;
				}
			}
			tx_dev = slave->dev;
		}
	}

	if (tx_dev)
		res = bond_dev_queue_xmit(bond, skb, tx_dev);

out:
	if (res)
		/* no suitable interface, frame not sent */
		kfree_skb(skb);

	/* frame sent to all suitable interfaces */
	return NETDEV_TX_OK;
}

/*------------------------- Device initialization ---------------------------*/

static void bond_set_xmit_hash_policy(struct bonding *bond)
{
	switch (bond->params.xmit_policy) {
	case BOND_XMIT_POLICY_LAYER23:
		bond->xmit_hash_policy = bond_xmit_hash_policy_l23;
		break;
	case BOND_XMIT_POLICY_LAYER34:
		bond->xmit_hash_policy = bond_xmit_hash_policy_l34;
		break;
	case BOND_XMIT_POLICY_LAYER2:
	default:
		bond->xmit_hash_policy = bond_xmit_hash_policy_l2;
		break;
	}
}

/*
 * Lookup the slave that corresponds to a qid
 */
static inline int bond_slave_override(struct bonding *bond,
				      struct sk_buff *skb)
{
	int i, res = 1;
	struct slave *slave = NULL;
	struct slave *check_slave;

	if (!skb->queue_mapping)
		return 1;

	/* Find out if any slaves have the same mapping as this skb. */
	bond_for_each_slave(bond, check_slave, i) {
		if (check_slave->queue_id == skb->queue_mapping) {
			slave = check_slave;
			break;
		}
	}

	/* If the slave isn't UP, use default transmit policy. */
	if (slave && slave->queue_id && IS_UP(slave->dev) &&
	    (slave->link == BOND_LINK_UP)) {
		res = bond_dev_queue_xmit(bond, skb, slave->dev);
	}

	return res;
}


static u16 bond_select_queue(struct net_device *dev, struct sk_buff *skb)
{
	/*
	 * This helper function exists to help dev_pick_tx get the correct
	 * destination queue.  Using a helper function skips a call to
	 * skb_tx_hash and will put the skbs in the queue we expect on their
	 * way down to the bonding driver.
	 */
	u16 txq = skb_rx_queue_recorded(skb) ? skb_get_rx_queue(skb) : 0;

	/*
	 * Save the original txq to restore before passing to the driver
	 */
	qdisc_skb_cb(skb)->slave_dev_queue_mapping = skb->queue_mapping;

	if (unlikely(txq >= dev->real_num_tx_queues)) {
		do {
			txq -= dev->real_num_tx_queues;
		} while (txq >= dev->real_num_tx_queues);
	}
	return txq;
}

static netdev_tx_t __bond_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bonding *bond = netdev_priv(dev);

	if (TX_QUEUE_OVERRIDE(bond->params.mode)) {
		if (!bond_slave_override(bond, skb))
			return NETDEV_TX_OK;
	}

	switch (bond->params.mode) {
	case BOND_MODE_ROUNDROBIN:
		return bond_xmit_roundrobin(skb, dev);
	case BOND_MODE_ACTIVEBACKUP:
		return bond_xmit_activebackup(skb, dev);
	case BOND_MODE_XOR:
		return bond_xmit_xor(skb, dev);
	case BOND_MODE_BROADCAST:
		return bond_xmit_broadcast(skb, dev);
	case BOND_MODE_8023AD:
		return bond_3ad_xmit_xor(skb, dev);
	case BOND_MODE_ALB:
	case BOND_MODE_TLB:
		return bond_alb_xmit(skb, dev);
	default:
		/* Should never happen, mode already checked */
		pr_err("%s: Error: Unknown bonding mode %d\n",
		       dev->name, bond->params.mode);
		WARN_ON_ONCE(1);
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}
}

static netdev_tx_t bond_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bonding *bond = netdev_priv(dev);
	netdev_tx_t ret = NETDEV_TX_OK;

	/*
	 * If we risk deadlock from transmitting this in the
	 * netpoll path, tell netpoll to queue the frame for later tx
	 */
	if (is_netpoll_tx_blocked(dev))
		return NETDEV_TX_BUSY;

	read_lock(&bond->lock);

	if (bond->slave_cnt)
		ret = __bond_start_xmit(skb, dev);
	else
		kfree_skb(skb);

	read_unlock(&bond->lock);

	return ret;
}

/*
 * set bond mode specific net device operations
 */
void bond_set_mode_ops(struct bonding *bond, int mode)
{
	struct net_device *bond_dev = bond->dev;

	switch (mode) {
	case BOND_MODE_ROUNDROBIN:
		break;
	case BOND_MODE_ACTIVEBACKUP:
		break;
	case BOND_MODE_XOR:
		bond_set_xmit_hash_policy(bond);
		break;
	case BOND_MODE_BROADCAST:
		break;
	case BOND_MODE_8023AD:
		bond_set_xmit_hash_policy(bond);
		break;
	case BOND_MODE_ALB:
		/* FALLTHRU */
	case BOND_MODE_TLB:
		break;
	default:
		/* Should never happen, mode already checked */
		pr_err("%s: Error: Unknown bonding mode %d\n",
		       bond_dev->name, mode);
		break;
	}
}

static void bond_ethtool_get_drvinfo(struct net_device *bond_dev,
				    struct ethtool_drvinfo *drvinfo)
{
	strncpy(drvinfo->driver, DRV_NAME, 32);
	strncpy(drvinfo->version, DRV_VERSION, 32);
	snprintf(drvinfo->fw_version, 32, "%d", BOND_ABI_VERSION);
}

static const struct ethtool_ops bond_ethtool_ops = {
	.get_drvinfo		= bond_ethtool_get_drvinfo,
	.get_link		= ethtool_op_get_link,
};

static const struct net_device_ops bond_netdev_ops = {
	.ndo_init		= bond_init,
	.ndo_uninit		= bond_uninit,
	.ndo_open		= bond_open,
	.ndo_stop		= bond_close,
	.ndo_start_xmit		= bond_start_xmit,
	.ndo_select_queue	= bond_select_queue,
	.ndo_get_stats64	= bond_get_stats,
	.ndo_do_ioctl		= bond_do_ioctl,
	.ndo_change_rx_flags	= bond_change_rx_flags,
	.ndo_set_rx_mode	= bond_set_multicast_list,
	.ndo_change_mtu		= bond_change_mtu,
	.ndo_set_mac_address	= bond_set_mac_address,
	.ndo_neigh_setup	= bond_neigh_setup,
	.ndo_vlan_rx_add_vid	= bond_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= bond_vlan_rx_kill_vid,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_netpoll_setup	= bond_netpoll_setup,
	.ndo_netpoll_cleanup	= bond_netpoll_cleanup,
	.ndo_poll_controller	= bond_poll_controller,
#endif
	.ndo_add_slave		= bond_enslave,
	.ndo_del_slave		= bond_release,
	.ndo_fix_features	= bond_fix_features,
};

static void bond_destructor(struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	if (bond->wq)
		destroy_workqueue(bond->wq);
	free_netdev(bond_dev);
}

static void bond_setup(struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);

	/* initialize rwlocks */
	rwlock_init(&bond->lock);
	rwlock_init(&bond->curr_slave_lock);

	bond->params = bonding_defaults;

	/* Initialize pointers */
	bond->dev = bond_dev;
	INIT_LIST_HEAD(&bond->vlan_list);

	/* Initialize the device entry points */
	ether_setup(bond_dev);
	bond_dev->netdev_ops = &bond_netdev_ops;
	bond_dev->ethtool_ops = &bond_ethtool_ops;
	bond_set_mode_ops(bond, bond->params.mode);

	bond_dev->destructor = bond_destructor;

	/* Initialize the device options */
	bond_dev->tx_queue_len = 0;
	bond_dev->flags |= IFF_MASTER|IFF_MULTICAST;
	bond_dev->priv_flags |= IFF_BONDING;
	bond_dev->priv_flags &= ~(IFF_XMIT_DST_RELEASE | IFF_TX_SKB_SHARING);

	/* At first, we block adding VLANs. That's the only way to
	 * prevent problems that occur when adding VLANs over an
	 * empty bond. The block will be removed once non-challenged
	 * slaves are enslaved.
	 */
	bond_dev->features |= NETIF_F_VLAN_CHALLENGED;

	/* don't acquire bond device's netif_tx_lock when
	 * transmitting */
	bond_dev->features |= NETIF_F_LLTX;

	/* By default, we declare the bond to be fully
	 * VLAN hardware accelerated capable. Special
	 * care is taken in the various xmit functions
	 * when there are slaves that are not hw accel
	 * capable
	 */

	bond_dev->hw_features = BOND_VLAN_FEATURES |
				NETIF_F_HW_VLAN_TX |
				NETIF_F_HW_VLAN_RX |
				NETIF_F_HW_VLAN_FILTER;

	bond_dev->hw_features &= ~(NETIF_F_ALL_CSUM & ~NETIF_F_HW_CSUM);
	bond_dev->features |= bond_dev->hw_features;
}

static void bond_work_cancel_all(struct bonding *bond)
{
	if (bond->params.miimon && delayed_work_pending(&bond->mii_work))
		cancel_delayed_work_sync(&bond->mii_work);

	if (bond->params.arp_interval && delayed_work_pending(&bond->arp_work))
		cancel_delayed_work_sync(&bond->arp_work);

	if (bond->params.mode == BOND_MODE_ALB &&
	    delayed_work_pending(&bond->alb_work))
		cancel_delayed_work_sync(&bond->alb_work);

	if (bond->params.mode == BOND_MODE_8023AD &&
	    delayed_work_pending(&bond->ad_work))
		cancel_delayed_work_sync(&bond->ad_work);

	if (delayed_work_pending(&bond->mcast_work))
		cancel_delayed_work_sync(&bond->mcast_work);
}

/*
* Destroy a bonding device.
* Must be under rtnl_lock when this function is called.
*/
static void bond_uninit(struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct vlan_entry *vlan, *tmp;

	bond_netpoll_cleanup(bond_dev);

	/* Release the bonded slaves */
	bond_release_all(bond_dev);

	list_del(&bond->bond_list);

	bond_work_cancel_all(bond);

	bond_debug_unregister(bond);

	__hw_addr_flush(&bond->mc_list);

	list_for_each_entry_safe(vlan, tmp, &bond->vlan_list, vlan_list) {
		list_del(&vlan->vlan_list);
		kfree(vlan);
	}
}

/*------------------------- Module initialization ---------------------------*/

/*
 * Convert string input module parms.  Accept either the
 * number of the mode or its string name.  A bit complicated because
 * some mode names are substrings of other names, and calls from sysfs
 * may have whitespace in the name (trailing newlines, for example).
 */
int bond_parse_parm(const char *buf, const struct bond_parm_tbl *tbl)
{
	int modeint = -1, i, rv;
	char *p, modestr[BOND_MAX_MODENAME_LEN + 1] = { 0, };

	for (p = (char *)buf; *p; p++)
		if (!(isdigit(*p) || isspace(*p)))
			break;

	if (*p)
		rv = sscanf(buf, "%20s", modestr);
	else
		rv = sscanf(buf, "%d", &modeint);

	if (!rv)
		return -1;

	for (i = 0; tbl[i].modename; i++) {
		if (modeint == tbl[i].mode)
			return tbl[i].mode;
		if (strcmp(modestr, tbl[i].modename) == 0)
			return tbl[i].mode;
	}

	return -1;
}

static int bond_check_params(struct bond_params *params)
{
	int arp_validate_value, fail_over_mac_value, primary_reselect_value;

	/*
	 * Convert string parameters.
	 */
	if (mode) {
		bond_mode = bond_parse_parm(mode, bond_mode_tbl);
		if (bond_mode == -1) {
			pr_err("Error: Invalid bonding mode \"%s\"\n",
			       mode == NULL ? "NULL" : mode);
			return -EINVAL;
		}
	}

	if (xmit_hash_policy) {
		if ((bond_mode != BOND_MODE_XOR) &&
		    (bond_mode != BOND_MODE_8023AD)) {
			pr_info("xmit_hash_policy param is irrelevant in mode %s\n",
			       bond_mode_name(bond_mode));
		} else {
			xmit_hashtype = bond_parse_parm(xmit_hash_policy,
							xmit_hashtype_tbl);
			if (xmit_hashtype == -1) {
				pr_err("Error: Invalid xmit_hash_policy \"%s\"\n",
				       xmit_hash_policy == NULL ? "NULL" :
				       xmit_hash_policy);
				return -EINVAL;
			}
		}
	}

	if (lacp_rate) {
		if (bond_mode != BOND_MODE_8023AD) {
			pr_info("lacp_rate param is irrelevant in mode %s\n",
				bond_mode_name(bond_mode));
		} else {
			lacp_fast = bond_parse_parm(lacp_rate, bond_lacp_tbl);
			if (lacp_fast == -1) {
				pr_err("Error: Invalid lacp rate \"%s\"\n",
				       lacp_rate == NULL ? "NULL" : lacp_rate);
				return -EINVAL;
			}
		}
	}

	if (ad_select) {
		params->ad_select = bond_parse_parm(ad_select, ad_select_tbl);
		if (params->ad_select == -1) {
			pr_err("Error: Invalid ad_select \"%s\"\n",
			       ad_select == NULL ? "NULL" : ad_select);
			return -EINVAL;
		}

		if (bond_mode != BOND_MODE_8023AD) {
			pr_warning("ad_select param only affects 802.3ad mode\n");
		}
	} else {
		params->ad_select = BOND_AD_STABLE;
	}

	if (max_bonds < 0) {
		pr_warning("Warning: max_bonds (%d) not in range %d-%d, so it was reset to BOND_DEFAULT_MAX_BONDS (%d)\n",
			   max_bonds, 0, INT_MAX, BOND_DEFAULT_MAX_BONDS);
		max_bonds = BOND_DEFAULT_MAX_BONDS;
	}

	if (miimon < 0) {
		pr_warning("Warning: miimon module parameter (%d), not in range 0-%d, so it was reset to %d\n",
			   miimon, INT_MAX, BOND_LINK_MON_INTERV);
		miimon = BOND_LINK_MON_INTERV;
	}

	if (updelay < 0) {
		pr_warning("Warning: updelay module parameter (%d), not in range 0-%d, so it was reset to 0\n",
			   updelay, INT_MAX);
		updelay = 0;
	}

	if (downdelay < 0) {
		pr_warning("Warning: downdelay module parameter (%d), not in range 0-%d, so it was reset to 0\n",
			   downdelay, INT_MAX);
		downdelay = 0;
	}

	if ((use_carrier != 0) && (use_carrier != 1)) {
		pr_warning("Warning: use_carrier module parameter (%d), not of valid value (0/1), so it was set to 1\n",
			   use_carrier);
		use_carrier = 1;
	}

	if (num_peer_notif < 0 || num_peer_notif > 255) {
		pr_warning("Warning: num_grat_arp/num_unsol_na (%d) not in range 0-255 so it was reset to 1\n",
			   num_peer_notif);
		num_peer_notif = 1;
	}

	/* reset values for 802.3ad */
	if (bond_mode == BOND_MODE_8023AD) {
		if (!miimon) {
			pr_warning("Warning: miimon must be specified, otherwise bonding will not detect link failure, speed and duplex which are essential for 802.3ad operation\n");
			pr_warning("Forcing miimon to 100msec\n");
			miimon = 100;
		}
	}

	if (tx_queues < 1 || tx_queues > 255) {
		pr_warning("Warning: tx_queues (%d) should be between "
			   "1 and 255, resetting to %d\n",
			   tx_queues, BOND_DEFAULT_TX_QUEUES);
		tx_queues = BOND_DEFAULT_TX_QUEUES;
	}

	if ((all_slaves_active != 0) && (all_slaves_active != 1)) {
		pr_warning("Warning: all_slaves_active module parameter (%d), "
			   "not of valid value (0/1), so it was set to "
			   "0\n", all_slaves_active);
		all_slaves_active = 0;
	}

	if (resend_igmp < 0 || resend_igmp > 255) {
		pr_warning("Warning: resend_igmp (%d) should be between "
			   "0 and 255, resetting to %d\n",
			   resend_igmp, BOND_DEFAULT_RESEND_IGMP);
		resend_igmp = BOND_DEFAULT_RESEND_IGMP;
	}

	/* reset values for TLB/ALB */
	if ((bond_mode == BOND_MODE_TLB) ||
	    (bond_mode == BOND_MODE_ALB)) {
		if (!miimon) {
			pr_warning("Warning: miimon must be specified, otherwise bonding will not detect link failure and link speed which are essential for TLB/ALB load balancing\n");
			pr_warning("Forcing miimon to 100msec\n");
			miimon = 100;
		}
	}

	if (bond_mode == BOND_MODE_ALB) {
		pr_notice("In ALB mode you might experience client disconnections upon reconnection of a link if the bonding module updelay parameter (%d msec) is incompatible with the forwarding delay time of the switch\n",
			  updelay);
	}

	if (!miimon) {
		if (updelay || downdelay) {
			/* just warn the user the up/down delay will have
			 * no effect since miimon is zero...
			 */
			pr_warning("Warning: miimon module parameter not set and updelay (%d) or downdelay (%d) module parameter is set; updelay and downdelay have no effect unless miimon is set\n",
				   updelay, downdelay);
		}
	} else {
		/* don't allow arp monitoring */
		if (arp_interval) {
			pr_warning("Warning: miimon (%d) and arp_interval (%d) can't be used simultaneously, disabling ARP monitoring\n",
				   miimon, arp_interval);
			arp_interval = 0;
		}

		if ((updelay % miimon) != 0) {
			pr_warning("Warning: updelay (%d) is not a multiple of miimon (%d), updelay rounded to %d ms\n",
				   updelay, miimon,
				   (updelay / miimon) * miimon);
		}

		updelay /= miimon;

		if ((downdelay % miimon) != 0) {
			pr_warning("Warning: downdelay (%d) is not a multiple of miimon (%d), downdelay rounded to %d ms\n",
				   downdelay, miimon,
				   (downdelay / miimon) * miimon);
		}

		downdelay /= miimon;
	}

	if (arp_interval < 0) {
		pr_warning("Warning: arp_interval module parameter (%d) , not in range 0-%d, so it was reset to %d\n",
			   arp_interval, INT_MAX, BOND_LINK_ARP_INTERV);
		arp_interval = BOND_LINK_ARP_INTERV;
	}

	for (arp_ip_count = 0;
	     (arp_ip_count < BOND_MAX_ARP_TARGETS) && arp_ip_target[arp_ip_count];
	     arp_ip_count++) {
		/* not complete check, but should be good enough to
		   catch mistakes */
		if (!isdigit(arp_ip_target[arp_ip_count][0])) {
			pr_warning("Warning: bad arp_ip_target module parameter (%s), ARP monitoring will not be performed\n",
				   arp_ip_target[arp_ip_count]);
			arp_interval = 0;
		} else {
			__be32 ip = in_aton(arp_ip_target[arp_ip_count]);
			arp_target[arp_ip_count] = ip;
		}
	}

	if (arp_interval && !arp_ip_count) {
		/* don't allow arping if no arp_ip_target given... */
		pr_warning("Warning: arp_interval module parameter (%d) specified without providing an arp_ip_target parameter, arp_interval was reset to 0\n",
			   arp_interval);
		arp_interval = 0;
	}

	if (arp_validate) {
		if (bond_mode != BOND_MODE_ACTIVEBACKUP) {
			pr_err("arp_validate only supported in active-backup mode\n");
			return -EINVAL;
		}
		if (!arp_interval) {
			pr_err("arp_validate requires arp_interval\n");
			return -EINVAL;
		}

		arp_validate_value = bond_parse_parm(arp_validate,
						     arp_validate_tbl);
		if (arp_validate_value == -1) {
			pr_err("Error: invalid arp_validate \"%s\"\n",
			       arp_validate == NULL ? "NULL" : arp_validate);
			return -EINVAL;
		}
	} else
		arp_validate_value = 0;

	if (miimon) {
		pr_info("MII link monitoring set to %d ms\n", miimon);
	} else if (arp_interval) {
		int i;

		pr_info("ARP monitoring set to %d ms, validate %s, with %d target(s):",
			arp_interval,
			arp_validate_tbl[arp_validate_value].modename,
			arp_ip_count);

		for (i = 0; i < arp_ip_count; i++)
			pr_info(" %s", arp_ip_target[i]);

		pr_info("\n");

	} else if (max_bonds) {
		/* miimon and arp_interval not set, we need one so things
		 * work as expected, see bonding.txt for details
		 */
		pr_debug("Warning: either miimon or arp_interval and arp_ip_target module parameters must be specified, otherwise bonding will not detect link failures! see bonding.txt for details.\n");
	}

	if (primary && !USES_PRIMARY(bond_mode)) {
		/* currently, using a primary only makes sense
		 * in active backup, TLB or ALB modes
		 */
		pr_warning("Warning: %s primary device specified but has no effect in %s mode\n",
			   primary, bond_mode_name(bond_mode));
		primary = NULL;
	}

	if (primary && primary_reselect) {
		primary_reselect_value = bond_parse_parm(primary_reselect,
							 pri_reselect_tbl);
		if (primary_reselect_value == -1) {
			pr_err("Error: Invalid primary_reselect \"%s\"\n",
			       primary_reselect ==
					NULL ? "NULL" : primary_reselect);
			return -EINVAL;
		}
	} else {
		primary_reselect_value = BOND_PRI_RESELECT_ALWAYS;
	}

	if (fail_over_mac) {
		fail_over_mac_value = bond_parse_parm(fail_over_mac,
						      fail_over_mac_tbl);
		if (fail_over_mac_value == -1) {
			pr_err("Error: invalid fail_over_mac \"%s\"\n",
			       arp_validate == NULL ? "NULL" : arp_validate);
			return -EINVAL;
		}

		if (bond_mode != BOND_MODE_ACTIVEBACKUP)
			pr_warning("Warning: fail_over_mac only affects active-backup mode.\n");
	} else {
		fail_over_mac_value = BOND_FOM_NONE;
	}

	/* fill params struct with the proper values */
	params->mode = bond_mode;
	params->xmit_policy = xmit_hashtype;
	params->miimon = miimon;
	params->num_peer_notif = num_peer_notif;
	params->arp_interval = arp_interval;
	params->arp_validate = arp_validate_value;
	params->updelay = updelay;
	params->downdelay = downdelay;
	params->use_carrier = use_carrier;
	params->lacp_fast = lacp_fast;
	params->primary[0] = 0;
	params->primary_reselect = primary_reselect_value;
	params->fail_over_mac = fail_over_mac_value;
	params->tx_queues = tx_queues;
	params->all_slaves_active = all_slaves_active;
	params->resend_igmp = resend_igmp;
	params->min_links = min_links;

	if (primary) {
		strncpy(params->primary, primary, IFNAMSIZ);
		params->primary[IFNAMSIZ - 1] = 0;
	}

	memcpy(params->arp_targets, arp_target, sizeof(arp_target));

	return 0;
}

static struct lock_class_key bonding_netdev_xmit_lock_key;
static struct lock_class_key bonding_netdev_addr_lock_key;

static void bond_set_lockdep_class_one(struct net_device *dev,
				       struct netdev_queue *txq,
				       void *_unused)
{
	lockdep_set_class(&txq->_xmit_lock,
			  &bonding_netdev_xmit_lock_key);
}

static void bond_set_lockdep_class(struct net_device *dev)
{
	lockdep_set_class(&dev->addr_list_lock,
			  &bonding_netdev_addr_lock_key);
	netdev_for_each_tx_queue(dev, bond_set_lockdep_class_one, NULL);
}

/*
 * Called from registration process
 */
static int bond_init(struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct bond_net *bn = net_generic(dev_net(bond_dev), bond_net_id);
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));

	pr_debug("Begin bond_init for %s\n", bond_dev->name);

	/*
	 * Initialize locks that may be required during
	 * en/deslave operations.  All of the bond_open work
	 * (of which this is part) should really be moved to
	 * a phase prior to dev_open
	 */
	spin_lock_init(&(bond_info->tx_hashtbl_lock));
	spin_lock_init(&(bond_info->rx_hashtbl_lock));

	bond->wq = create_singlethread_workqueue(bond_dev->name);
	if (!bond->wq)
		return -ENOMEM;

	bond_set_lockdep_class(bond_dev);

	list_add_tail(&bond->bond_list, &bn->dev_list);

	bond_prepare_sysfs_group(bond);

	bond_debug_register(bond);

	__hw_addr_init(&bond->mc_list);
	return 0;
}

static int bond_validate(struct nlattr *tb[], struct nlattr *data[])
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;
		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}
	return 0;
}

static unsigned int bond_get_num_tx_queues(void)
{
	return tx_queues;
}

static struct rtnl_link_ops bond_link_ops __read_mostly = {
	.kind			= "bond",
	.priv_size		= sizeof(struct bonding),
	.setup			= bond_setup,
	.validate		= bond_validate,
	.get_num_tx_queues	= bond_get_num_tx_queues,
	.get_num_rx_queues	= bond_get_num_tx_queues, /* Use the same number
							     as for TX queues */
};

/* Create a new bond based on the specified name and bonding parameters.
 * If name is NULL, obtain a suitable "bond%d" name for us.
 * Caller must NOT hold rtnl_lock; we need to release it here before we
 * set up our sysfs entries.
 */
int bond_create(struct net *net, const char *name)
{
	struct net_device *bond_dev;
	int res;

	rtnl_lock();

	bond_dev = alloc_netdev_mq(sizeof(struct bonding),
				   name ? name : "bond%d",
				   bond_setup, tx_queues);
	if (!bond_dev) {
		pr_err("%s: eek! can't alloc netdev!\n", name);
		rtnl_unlock();
		return -ENOMEM;
	}

	dev_net_set(bond_dev, net);
	bond_dev->rtnl_link_ops = &bond_link_ops;

	res = register_netdevice(bond_dev);

	netif_carrier_off(bond_dev);

	rtnl_unlock();
	if (res < 0)
		bond_destructor(bond_dev);
	return res;
}

static int __net_init bond_net_init(struct net *net)
{
	struct bond_net *bn = net_generic(net, bond_net_id);

	bn->net = net;
	INIT_LIST_HEAD(&bn->dev_list);

	bond_create_proc_dir(bn);
	bond_create_sysfs(bn);
	
	return 0;
}

static void __net_exit bond_net_exit(struct net *net)
{
	struct bond_net *bn = net_generic(net, bond_net_id);

	bond_destroy_sysfs(bn);
	bond_destroy_proc_dir(bn);
}

static struct pernet_operations bond_net_ops = {
	.init = bond_net_init,
	.exit = bond_net_exit,
	.id   = &bond_net_id,
	.size = sizeof(struct bond_net),
};

static int __init bonding_init(void)
{
	int i;
	int res;

	pr_info("%s", bond_version);

	res = bond_check_params(&bonding_defaults);
	if (res)
		goto out;

	res = register_pernet_subsys(&bond_net_ops);
	if (res)
		goto out;

	res = rtnl_link_register(&bond_link_ops);
	if (res)
		goto err_link;

	bond_create_debugfs();

	for (i = 0; i < max_bonds; i++) {
		res = bond_create(&init_net, NULL);
		if (res)
			goto err;
	}

	register_netdevice_notifier(&bond_netdev_notifier);
out:
	return res;
err:
	rtnl_link_unregister(&bond_link_ops);
err_link:
	unregister_pernet_subsys(&bond_net_ops);
	goto out;

}

static void __exit bonding_exit(void)
{
	unregister_netdevice_notifier(&bond_netdev_notifier);

	bond_destroy_debugfs();

	rtnl_link_unregister(&bond_link_ops);
	unregister_pernet_subsys(&bond_net_ops);

#ifdef CONFIG_NET_POLL_CONTROLLER
	/*
	 * Make sure we don't have an imbalance on our netpoll blocking
	 */
	WARN_ON(atomic_read(&netpoll_block_tx));
#endif
}

module_init(bonding_init);
module_exit(bonding_exit);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION(DRV_DESCRIPTION ", v" DRV_VERSION);
MODULE_AUTHOR("Thomas Davis, tadavis@lbl.gov and many others");
MODULE_ALIAS_RTNL_LINK("bond");
