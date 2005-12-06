/*
 * Bond several ethernet interfaces into a Cisco, running 'Etherchannel'.
 *
 * Portions are (c) Copyright 1995 Simon "Guru Aleph-Null" Janes
 * NCM: Network and Communications Management, Inc.
 *
 * BUT, I'm the one who modified it for ethernet, so:
 * (c) Copyright 1999, Thomas Davis, tadavis@lbl.gov
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU Public License, incorporated herein by reference.
 *
 *
 * 2003/03/18 - Amir Noam <amir.noam at intel dot com>,
 *		Tsippy Mendelson <tsippy.mendelson at intel dot com> and
 *		Shmulik Hen <shmulik.hen at intel dot com>
 *	- Added support for IEEE 802.3ad Dynamic link aggregation mode.
 *
 * 2003/05/01 - Tsippy Mendelson <tsippy.mendelson at intel dot com> and
 *		Amir Noam <amir.noam at intel dot com>
 *	- Code beautification and style changes (mainly in comments).
 *
 * 2003/05/01 - Shmulik Hen <shmulik.hen at intel dot com>
 *	- Added support for Transmit load balancing mode.
 *
 * 2003/12/01 - Shmulik Hen <shmulik.hen at intel dot com>
 *	- Code cleanup and style changes
 *
 * 2005/05/05 - Jason Gabler <jygabler at lbl dot gov>
 *      - added "xmit_policy" kernel parameter for alternate hashing policy
 *	  support for mode 2
 */

#ifndef _LINUX_BONDING_H
#define _LINUX_BONDING_H

#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <linux/if_bonding.h>
#include "bond_3ad.h"
#include "bond_alb.h"

#define DRV_VERSION	"2.6.5"
#define DRV_RELDATE	"November 4, 2005"
#define DRV_NAME	"bonding"
#define DRV_DESCRIPTION	"Ethernet Channel Bonding Driver"

#define BOND_MAX_ARP_TARGETS	16

#ifdef BONDING_DEBUG
#define dprintk(fmt, args...) \
	printk(KERN_DEBUG     \
	       DRV_NAME ": %s() %d: " fmt, __FUNCTION__, __LINE__ , ## args )
#else
#define dprintk(fmt, args...)
#endif /* BONDING_DEBUG */

#define IS_UP(dev)					   \
	      ((((dev)->flags & IFF_UP) == IFF_UP)	&& \
	       netif_running(dev)			&& \
	       netif_carrier_ok(dev))

/*
 * Checks whether bond is ready for transmit.
 *
 * Caller must hold bond->lock
 */
#define BOND_IS_OK(bond)			     \
		   (((bond)->dev->flags & IFF_UP) && \
		    netif_running((bond)->dev)	  && \
		    ((bond)->slave_cnt > 0))

/*
 * Checks whether slave is ready for transmit.
 */
#define SLAVE_IS_OK(slave)			        \
		    (((slave)->dev->flags & IFF_UP)  && \
		     netif_running((slave)->dev)     && \
		     ((slave)->link == BOND_LINK_UP) && \
		     ((slave)->state == BOND_STATE_ACTIVE))


#define USES_PRIMARY(mode)				\
		(((mode) == BOND_MODE_ACTIVEBACKUP) ||	\
		 ((mode) == BOND_MODE_TLB)          ||	\
		 ((mode) == BOND_MODE_ALB))

/*
 * Less bad way to call ioctl from within the kernel; this needs to be
 * done some other way to get the call out of interrupt context.
 * Needs "ioctl" variable to be supplied by calling context.
 */
#define IOCTL(dev, arg, cmd) ({		\
	int res = 0;			\
	mm_segment_t fs = get_fs();	\
	set_fs(get_ds());		\
	res = ioctl(dev, arg, cmd);	\
	set_fs(fs);			\
	res; })

/**
 * bond_for_each_slave_from - iterate the slaves list from a starting point
 * @bond:	the bond holding this list.
 * @pos:	current slave.
 * @cnt:	counter for max number of moves
 * @start:	starting point.
 *
 * Caller must hold bond->lock
 */
#define bond_for_each_slave_from(bond, pos, cnt, start)	\
	for (cnt = 0, pos = start;				\
	     cnt < (bond)->slave_cnt;				\
             cnt++, pos = (pos)->next)

/**
 * bond_for_each_slave_from_to - iterate the slaves list from start point to stop point
 * @bond:	the bond holding this list.
 * @pos:	current slave.
 * @cnt:	counter for number max of moves
 * @start:	start point.
 * @stop:	stop point.
 *
 * Caller must hold bond->lock
 */
#define bond_for_each_slave_from_to(bond, pos, cnt, start, stop)	\
	for (cnt = 0, pos = start;					\
	     ((cnt < (bond)->slave_cnt) && (pos != (stop)->next));	\
             cnt++, pos = (pos)->next)

/**
 * bond_for_each_slave - iterate the slaves list from head
 * @bond:	the bond holding this list.
 * @pos:	current slave.
 * @cnt:	counter for max number of moves
 *
 * Caller must hold bond->lock
 */
#define bond_for_each_slave(bond, pos, cnt)	\
		bond_for_each_slave_from(bond, pos, cnt, (bond)->first_slave)


struct bond_params {
	int mode;
	int xmit_policy;
	int miimon;
	int arp_interval;
	int use_carrier;
	int updelay;
	int downdelay;
	int lacp_fast;
	char primary[IFNAMSIZ];
	u32 arp_targets[BOND_MAX_ARP_TARGETS];
};

struct vlan_entry {
	struct list_head vlan_list;
	u32 vlan_ip;
	unsigned short vlan_id;
};

struct slave {
	struct net_device *dev; /* first - usefull for panic debug */
	struct slave *next;
	struct slave *prev;
	s16    delay;
	u32    jiffies;
	s8     link;    /* one of BOND_LINK_XXXX */
	s8     state;   /* one of BOND_STATE_XXXX */
	u32    original_flags;
	u32    link_failure_count;
	u16    speed;
	u8     duplex;
	u8     perm_hwaddr[ETH_ALEN];
	struct ad_slave_info ad_info; /* HUGE - better to dynamically alloc */
	struct tlb_slave_info tlb_info;
};

/*
 * Here are the locking policies for the two bonding locks:
 *
 * 1) Get bond->lock when reading/writing slave list.
 * 2) Get bond->curr_slave_lock when reading/writing bond->curr_active_slave.
 *    (It is unnecessary when the write-lock is put with bond->lock.)
 * 3) When we lock with bond->curr_slave_lock, we must lock with bond->lock
 *    beforehand.
 */
struct bonding {
	struct   net_device *dev; /* first - usefull for panic debug */
	struct   slave *first_slave;
	struct   slave *curr_active_slave;
	struct   slave *current_arp_slave;
	struct   slave *primary_slave;
	s32      slave_cnt; /* never change this value outside the attach/detach wrappers */
	rwlock_t lock;
	rwlock_t curr_slave_lock;
	struct   timer_list mii_timer;
	struct   timer_list arp_timer;
	s8       kill_timers;
	struct   net_device_stats stats;
#ifdef CONFIG_PROC_FS
	struct   proc_dir_entry *proc_entry;
	char     proc_file_name[IFNAMSIZ];
#endif /* CONFIG_PROC_FS */
	struct   list_head bond_list;
	struct   dev_mc_list *mc_list;
	int      (*xmit_hash_policy)(struct sk_buff *, struct net_device *, int);
	u32      master_ip;
	u16      flags;
	struct   ad_bond_info ad_info;
	struct   alb_bond_info alb_info;
	struct   bond_params params;
	struct   list_head vlan_list;
	struct   vlan_group *vlgrp;
};

/**
 * Returns NULL if the net_device does not belong to any of the bond's slaves
 *
 * Caller must hold bond lock for read
 */
extern inline struct slave *bond_get_slave_by_dev(struct bonding *bond, struct net_device *slave_dev)
{
	struct slave *slave = NULL;
	int i;

	bond_for_each_slave(bond, slave, i) {
		if (slave->dev == slave_dev) {
			break;
		}
	}

	return slave;
}

extern inline struct bonding *bond_get_bond_by_slave(struct slave *slave)
{
	if (!slave || !slave->dev->master) {
		return NULL;
	}

	return (struct bonding *)slave->dev->master->priv;
}

extern inline void bond_set_slave_inactive_flags(struct slave *slave)
{
	slave->state = BOND_STATE_BACKUP;
	slave->dev->flags |= IFF_NOARP;
}

extern inline void bond_set_slave_active_flags(struct slave *slave)
{
	slave->state = BOND_STATE_ACTIVE;
	slave->dev->flags &= ~IFF_NOARP;
}

struct vlan_entry *bond_next_vlan(struct bonding *bond, struct vlan_entry *curr);
int bond_dev_queue_xmit(struct bonding *bond, struct sk_buff *skb, struct net_device *slave_dev);

#endif /* _LINUX_BONDING_H */

