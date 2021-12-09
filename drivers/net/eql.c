/*
 * Equalizer Load-balancer for serial network interfaces.
 *
 * (c) Copyright 1995 Simon "Guru Aleph-Null" Janes
 * NCM: Network and Communications Management, Inc.
 *
 * (c) Copyright 2002 David S. Miller (davem@redhat.com)
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU General Public License, incorporated herein by reference.
 *
 * The author may be reached as simon@ncm.com, or C/O
 *    NCM
 *    Attn: Simon Janes
 *    6803 Whittier Ave
 *    McLean VA 22101
 *    Phone: 1-703-847-0040 ext 103
 */

/*
 * Sources:
 *   skeleton.c by Donald Becker.
 * Inspirations:
 *   The Harried and Overworked Alan Cox
 * Conspiracies:
 *   The Alan Cox and Mike McLagan plot to get someone else to do the code,
 *   which turned out to be me.
 */

/*
 * $Log: eql.c,v $
 * Revision 1.2  1996/04/11 17:51:52  guru
 * Added one-line eql_remove_slave patch.
 *
 * Revision 1.1  1996/04/11 17:44:17  guru
 * Initial revision
 *
 * Revision 3.13  1996/01/21  15:17:18  alan
 * tx_queue_len changes.
 * reformatted.
 *
 * Revision 3.12  1995/03/22  21:07:51  anarchy
 * Added capable() checks on configuration.
 * Moved header file.
 *
 * Revision 3.11  1995/01/19  23:14:31  guru
 * 		      slave_load = (ULONG_MAX - (ULONG_MAX / 2)) -
 * 			(priority_Bps) + bytes_queued * 8;
 *
 * Revision 3.10  1995/01/19  23:07:53  guru
 * back to
 * 		      slave_load = (ULONG_MAX - (ULONG_MAX / 2)) -
 * 			(priority_Bps) + bytes_queued;
 *
 * Revision 3.9  1995/01/19  22:38:20  guru
 * 		      slave_load = (ULONG_MAX - (ULONG_MAX / 2)) -
 * 			(priority_Bps) + bytes_queued * 4;
 *
 * Revision 3.8  1995/01/19  22:30:55  guru
 *       slave_load = (ULONG_MAX - (ULONG_MAX / 2)) -
 * 			(priority_Bps) + bytes_queued * 2;
 *
 * Revision 3.7  1995/01/19  21:52:35  guru
 * printk's trimmed out.
 *
 * Revision 3.6  1995/01/19  21:49:56  guru
 * This is working pretty well. I gained 1 K/s in speed.. now it's just
 * robustness and printk's to be diked out.
 *
 * Revision 3.5  1995/01/18  22:29:59  guru
 * still crashes the kernel when the lock_wait thing is woken up.
 *
 * Revision 3.4  1995/01/18  21:59:47  guru
 * Broken set-bit locking snapshot
 *
 * Revision 3.3  1995/01/17  22:09:18  guru
 * infinite sleep in a lock somewhere..
 *
 * Revision 3.2  1995/01/15  16:46:06  guru
 * Log trimmed of non-pertinent 1.x branch messages
 *
 * Revision 3.1  1995/01/15  14:41:45  guru
 * New Scheduler and timer stuff...
 *
 * Revision 1.15  1995/01/15  14:29:02  guru
 * Will make 1.14 (now 1.15) the 3.0 branch, and the 1.12 the 2.0 branch, the one
 * with the dumber scheduler
 *
 * Revision 1.14  1995/01/15  02:37:08  guru
 * shock.. the kept-new-versions could have zonked working
 * stuff.. shudder
 *
 * Revision 1.13  1995/01/15  02:36:31  guru
 * big changes
 *
 * 	scheduler was torn out and replaced with something smarter
 *
 * 	global names not prefixed with eql_ were renamed to protect
 * 	against namespace collisions
 *
 * 	a few more abstract interfaces were added to facilitate any
 * 	potential change of datastructure.  the driver is still using
 * 	a linked list of slaves.  going to a heap would be a bit of
 * 	an overkill.
 *
 * 	this compiles fine with no warnings.
 *
 * 	the locking mechanism and timer stuff must be written however,
 * 	this version will not work otherwise
 *
 * Sorry, I had to rewrite most of this for 2.5.x -DaveM
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/compat.h>
#include <linux/capability.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/netdevice.h>
#include <net/net_namespace.h>

#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/if_eql.h>
#include <linux/pkt_sched.h>

#include <linux/uaccess.h>

static int eql_open(struct net_device *dev);
static int eql_close(struct net_device *dev);
static int eql_siocdevprivate(struct net_device *dev, struct ifreq *ifr,
			      void __user *data, int cmd);
static netdev_tx_t eql_slave_xmit(struct sk_buff *skb, struct net_device *dev);

#define eql_is_slave(dev)	((dev->flags & IFF_SLAVE) == IFF_SLAVE)
#define eql_is_master(dev)	((dev->flags & IFF_MASTER) == IFF_MASTER)

static void eql_kill_one_slave(slave_queue_t *queue, slave_t *slave);

static void eql_timer(struct timer_list *t)
{
	equalizer_t *eql = from_timer(eql, t, timer);
	struct list_head *this, *tmp, *head;

	spin_lock(&eql->queue.lock);
	head = &eql->queue.all_slaves;
	list_for_each_safe(this, tmp, head) {
		slave_t *slave = list_entry(this, slave_t, list);

		if ((slave->dev->flags & IFF_UP) == IFF_UP) {
			slave->bytes_queued -= slave->priority_Bps;
			if (slave->bytes_queued < 0)
				slave->bytes_queued = 0;
		} else {
			eql_kill_one_slave(&eql->queue, slave);
		}

	}
	spin_unlock(&eql->queue.lock);

	eql->timer.expires = jiffies + EQL_DEFAULT_RESCHED_IVAL;
	add_timer(&eql->timer);
}

static const char version[] __initconst =
	"Equalizer2002: Simon Janes (simon@ncm.com) and David S. Miller (davem@redhat.com)";

static const struct net_device_ops eql_netdev_ops = {
	.ndo_open	= eql_open,
	.ndo_stop	= eql_close,
	.ndo_siocdevprivate = eql_siocdevprivate,
	.ndo_start_xmit	= eql_slave_xmit,
};

static void __init eql_setup(struct net_device *dev)
{
	equalizer_t *eql = netdev_priv(dev);

	timer_setup(&eql->timer, eql_timer, 0);
	eql->timer.expires  	= jiffies + EQL_DEFAULT_RESCHED_IVAL;

	spin_lock_init(&eql->queue.lock);
	INIT_LIST_HEAD(&eql->queue.all_slaves);
	eql->queue.master_dev	= dev;

	dev->netdev_ops		= &eql_netdev_ops;

	/*
	 *	Now we undo some of the things that eth_setup does
	 * 	that we don't like
	 */

	dev->mtu        	= EQL_DEFAULT_MTU;	/* set to 576 in if_eql.h */
	dev->flags      	= IFF_MASTER;

	dev->type       	= ARPHRD_SLIP;
	dev->tx_queue_len 	= 5;		/* Hands them off fast */
	netif_keep_dst(dev);
}

static int eql_open(struct net_device *dev)
{
	equalizer_t *eql = netdev_priv(dev);

	/* XXX We should force this off automatically for the user. */
	netdev_info(dev,
		    "remember to turn off Van-Jacobson compression on your slave devices\n");

	BUG_ON(!list_empty(&eql->queue.all_slaves));

	eql->min_slaves = 1;
	eql->max_slaves = EQL_DEFAULT_MAX_SLAVES; /* 4 usually... */

	add_timer(&eql->timer);

	return 0;
}

static void eql_kill_one_slave(slave_queue_t *queue, slave_t *slave)
{
	list_del(&slave->list);
	queue->num_slaves--;
	slave->dev->flags &= ~IFF_SLAVE;
	dev_put(slave->dev);
	kfree(slave);
}

static void eql_kill_slave_queue(slave_queue_t *queue)
{
	struct list_head *head, *tmp, *this;

	spin_lock_bh(&queue->lock);

	head = &queue->all_slaves;
	list_for_each_safe(this, tmp, head) {
		slave_t *s = list_entry(this, slave_t, list);

		eql_kill_one_slave(queue, s);
	}

	spin_unlock_bh(&queue->lock);
}

static int eql_close(struct net_device *dev)
{
	equalizer_t *eql = netdev_priv(dev);

	/*
	 *	The timer has to be stopped first before we start hacking away
	 *	at the data structure it scans every so often...
	 */

	del_timer_sync(&eql->timer);

	eql_kill_slave_queue(&eql->queue);

	return 0;
}

static int eql_enslave(struct net_device *dev,  slaving_request_t __user *srq);
static int eql_emancipate(struct net_device *dev, slaving_request_t __user *srq);

static int eql_g_slave_cfg(struct net_device *dev, slave_config_t __user *sc);
static int eql_s_slave_cfg(struct net_device *dev, slave_config_t __user *sc);

static int eql_g_master_cfg(struct net_device *dev, master_config_t __user *mc);
static int eql_s_master_cfg(struct net_device *dev, master_config_t __user *mc);

static int eql_siocdevprivate(struct net_device *dev, struct ifreq *ifr,
			      void __user *data, int cmd)
{
	if (cmd != EQL_GETMASTRCFG && cmd != EQL_GETSLAVECFG &&
	    !capable(CAP_NET_ADMIN))
	  	return -EPERM;

	if (in_compat_syscall()) /* to be implemented */
		return -EOPNOTSUPP;

	switch (cmd) {
		case EQL_ENSLAVE:
			return eql_enslave(dev, data);
		case EQL_EMANCIPATE:
			return eql_emancipate(dev, data);
		case EQL_GETSLAVECFG:
			return eql_g_slave_cfg(dev, data);
		case EQL_SETSLAVECFG:
			return eql_s_slave_cfg(dev, data);
		case EQL_GETMASTRCFG:
			return eql_g_master_cfg(dev, data);
		case EQL_SETMASTRCFG:
			return eql_s_master_cfg(dev, data);
		default:
			return -EOPNOTSUPP;
	}
}

/* queue->lock must be held */
static slave_t *__eql_schedule_slaves(slave_queue_t *queue)
{
	unsigned long best_load = ~0UL;
	struct list_head *this, *tmp, *head;
	slave_t *best_slave;

	best_slave = NULL;

	/* Make a pass to set the best slave. */
	head = &queue->all_slaves;
	list_for_each_safe(this, tmp, head) {
		slave_t *slave = list_entry(this, slave_t, list);
		unsigned long slave_load, bytes_queued, priority_Bps;

		/* Go through the slave list once, updating best_slave
		 * whenever a new best_load is found.
		 */
		bytes_queued = slave->bytes_queued;
		priority_Bps = slave->priority_Bps;
		if ((slave->dev->flags & IFF_UP) == IFF_UP) {
			slave_load = (~0UL - (~0UL / 2)) -
				(priority_Bps) + bytes_queued * 8;

			if (slave_load < best_load) {
				best_load = slave_load;
				best_slave = slave;
			}
		} else {
			/* We found a dead slave, kill it. */
			eql_kill_one_slave(queue, slave);
		}
	}
	return best_slave;
}

static netdev_tx_t eql_slave_xmit(struct sk_buff *skb, struct net_device *dev)
{
	equalizer_t *eql = netdev_priv(dev);
	slave_t *slave;

	spin_lock(&eql->queue.lock);

	slave = __eql_schedule_slaves(&eql->queue);
	if (slave) {
		struct net_device *slave_dev = slave->dev;

		skb->dev = slave_dev;
		skb->priority = TC_PRIO_FILLER;
		slave->bytes_queued += skb->len;
		dev_queue_xmit(skb);
		dev->stats.tx_packets++;
	} else {
		dev->stats.tx_dropped++;
		dev_kfree_skb(skb);
	}

	spin_unlock(&eql->queue.lock);

	return NETDEV_TX_OK;
}

/*
 *	Private ioctl functions
 */

/* queue->lock must be held */
static slave_t *__eql_find_slave_dev(slave_queue_t *queue, struct net_device *dev)
{
	struct list_head *this, *head;

	head = &queue->all_slaves;
	list_for_each(this, head) {
		slave_t *slave = list_entry(this, slave_t, list);

		if (slave->dev == dev)
			return slave;
	}

	return NULL;
}

static inline int eql_is_full(slave_queue_t *queue)
{
	equalizer_t *eql = netdev_priv(queue->master_dev);

	if (queue->num_slaves >= eql->max_slaves)
		return 1;
	return 0;
}

/* queue->lock must be held */
static int __eql_insert_slave(slave_queue_t *queue, slave_t *slave)
{
	if (!eql_is_full(queue)) {
		slave_t *duplicate_slave = NULL;

		duplicate_slave = __eql_find_slave_dev(queue, slave->dev);
		if (duplicate_slave)
			eql_kill_one_slave(queue, duplicate_slave);

		dev_hold(slave->dev);
		list_add(&slave->list, &queue->all_slaves);
		queue->num_slaves++;
		slave->dev->flags |= IFF_SLAVE;

		return 0;
	}

	return -ENOSPC;
}

static int eql_enslave(struct net_device *master_dev, slaving_request_t __user *srqp)
{
	struct net_device *slave_dev;
	slaving_request_t srq;

	if (copy_from_user(&srq, srqp, sizeof (slaving_request_t)))
		return -EFAULT;

	slave_dev = __dev_get_by_name(&init_net, srq.slave_name);
	if (!slave_dev)
		return -ENODEV;

	if ((master_dev->flags & IFF_UP) == IFF_UP) {
		/* slave is not a master & not already a slave: */
		if (!eql_is_master(slave_dev) && !eql_is_slave(slave_dev)) {
			slave_t *s = kmalloc(sizeof(*s), GFP_KERNEL);
			equalizer_t *eql = netdev_priv(master_dev);
			int ret;

			if (!s)
				return -ENOMEM;

			memset(s, 0, sizeof(*s));
			s->dev = slave_dev;
			s->priority = srq.priority;
			s->priority_bps = srq.priority;
			s->priority_Bps = srq.priority / 8;

			spin_lock_bh(&eql->queue.lock);
			ret = __eql_insert_slave(&eql->queue, s);
			if (ret)
				kfree(s);

			spin_unlock_bh(&eql->queue.lock);

			return ret;
		}
	}

	return -EINVAL;
}

static int eql_emancipate(struct net_device *master_dev, slaving_request_t __user *srqp)
{
	equalizer_t *eql = netdev_priv(master_dev);
	struct net_device *slave_dev;
	slaving_request_t srq;
	int ret;

	if (copy_from_user(&srq, srqp, sizeof (slaving_request_t)))
		return -EFAULT;

	slave_dev = __dev_get_by_name(&init_net, srq.slave_name);
	if (!slave_dev)
		return -ENODEV;

	ret = -EINVAL;
	spin_lock_bh(&eql->queue.lock);
	if (eql_is_slave(slave_dev)) {
		slave_t *slave = __eql_find_slave_dev(&eql->queue, slave_dev);
		if (slave) {
			eql_kill_one_slave(&eql->queue, slave);
			ret = 0;
		}
	}
	spin_unlock_bh(&eql->queue.lock);

	return ret;
}

static int eql_g_slave_cfg(struct net_device *dev, slave_config_t __user *scp)
{
	equalizer_t *eql = netdev_priv(dev);
	slave_t *slave;
	struct net_device *slave_dev;
	slave_config_t sc;
	int ret;

	if (copy_from_user(&sc, scp, sizeof (slave_config_t)))
		return -EFAULT;

	slave_dev = __dev_get_by_name(&init_net, sc.slave_name);
	if (!slave_dev)
		return -ENODEV;

	ret = -EINVAL;

	spin_lock_bh(&eql->queue.lock);
	if (eql_is_slave(slave_dev)) {
		slave = __eql_find_slave_dev(&eql->queue, slave_dev);
		if (slave) {
			sc.priority = slave->priority;
			ret = 0;
		}
	}
	spin_unlock_bh(&eql->queue.lock);

	if (!ret && copy_to_user(scp, &sc, sizeof (slave_config_t)))
		ret = -EFAULT;

	return ret;
}

static int eql_s_slave_cfg(struct net_device *dev, slave_config_t __user *scp)
{
	slave_t *slave;
	equalizer_t *eql;
	struct net_device *slave_dev;
	slave_config_t sc;
	int ret;

	if (copy_from_user(&sc, scp, sizeof (slave_config_t)))
		return -EFAULT;

	slave_dev = __dev_get_by_name(&init_net, sc.slave_name);
	if (!slave_dev)
		return -ENODEV;

	ret = -EINVAL;

	eql = netdev_priv(dev);
	spin_lock_bh(&eql->queue.lock);
	if (eql_is_slave(slave_dev)) {
		slave = __eql_find_slave_dev(&eql->queue, slave_dev);
		if (slave) {
			slave->priority = sc.priority;
			slave->priority_bps = sc.priority;
			slave->priority_Bps = sc.priority / 8;
			ret = 0;
		}
	}
	spin_unlock_bh(&eql->queue.lock);

	return ret;
}

static int eql_g_master_cfg(struct net_device *dev, master_config_t __user *mcp)
{
	equalizer_t *eql;
	master_config_t mc;

	memset(&mc, 0, sizeof(master_config_t));

	if (eql_is_master(dev)) {
		eql = netdev_priv(dev);
		mc.max_slaves = eql->max_slaves;
		mc.min_slaves = eql->min_slaves;
		if (copy_to_user(mcp, &mc, sizeof (master_config_t)))
			return -EFAULT;
		return 0;
	}
	return -EINVAL;
}

static int eql_s_master_cfg(struct net_device *dev, master_config_t __user *mcp)
{
	equalizer_t *eql;
	master_config_t mc;

	if (copy_from_user(&mc, mcp, sizeof (master_config_t)))
		return -EFAULT;

	if (eql_is_master(dev)) {
		eql = netdev_priv(dev);
		eql->max_slaves = mc.max_slaves;
		eql->min_slaves = mc.min_slaves;
		return 0;
	}
	return -EINVAL;
}

static struct net_device *dev_eql;

static int __init eql_init_module(void)
{
	int err;

	pr_info("%s\n", version);

	dev_eql = alloc_netdev(sizeof(equalizer_t), "eql", NET_NAME_UNKNOWN,
			       eql_setup);
	if (!dev_eql)
		return -ENOMEM;

	err = register_netdev(dev_eql);
	if (err)
		free_netdev(dev_eql);
	return err;
}

static void __exit eql_cleanup_module(void)
{
	unregister_netdev(dev_eql);
	free_netdev(dev_eql);
}

module_init(eql_init_module);
module_exit(eql_cleanup_module);
MODULE_LICENSE("GPL");
