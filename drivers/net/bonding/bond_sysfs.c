
/*
 * Copyright(c) 2004-2005 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/sysdev.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/in.h>
#include <linux/sysfs.h>
#include <linux/ctype.h>
#include <linux/inet.h>
#include <linux/rtnetlink.h>
#include <net/net_namespace.h>

/* #define BONDING_DEBUG 1 */
#include "bonding.h"
#define to_dev(obj)	container_of(obj,struct device,kobj)
#define to_bond(cd)	((struct bonding *)(to_net_dev(cd)->priv))

/*---------------------------- Declarations -------------------------------*/


extern struct list_head bond_dev_list;
extern struct bond_params bonding_defaults;
extern struct bond_parm_tbl bond_mode_tbl[];
extern struct bond_parm_tbl bond_lacp_tbl[];
extern struct bond_parm_tbl xmit_hashtype_tbl[];
extern struct bond_parm_tbl arp_validate_tbl[];

static int expected_refcount = -1;
static struct class *netdev_class;
/*--------------------------- Data Structures -----------------------------*/

/* Bonding sysfs lock.  Why can't we just use the subsystem lock?
 * Because kobject_register tries to acquire the subsystem lock.  If
 * we already hold the lock (which we would if the user was creating
 * a new bond through the sysfs interface), we deadlock.
 * This lock is only needed when deleting a bond - we need to make sure
 * that we don't collide with an ongoing ioctl.
 */

struct rw_semaphore bonding_rwsem;




/*------------------------------ Functions --------------------------------*/

/*
 * "show" function for the bond_masters attribute.
 * The class parameter is ignored.
 */
static ssize_t bonding_show_bonds(struct class *cls, char *buffer)
{
	int res = 0;
	struct bonding *bond;

	down_read(&(bonding_rwsem));

	list_for_each_entry(bond, &bond_dev_list, bond_list) {
		if (res > (PAGE_SIZE - IFNAMSIZ)) {
			/* not enough space for another interface name */
			if ((PAGE_SIZE - res) > 10)
				res = PAGE_SIZE - 10;
			res += sprintf(buffer + res, "++more++");
			break;
		}
		res += sprintf(buffer + res, "%s ",
			       bond->dev->name);
	}
	res += sprintf(buffer + res, "\n");
	res++;
	up_read(&(bonding_rwsem));
	return res;
}

/*
 * "store" function for the bond_masters attribute.  This is what
 * creates and deletes entire bonds.
 *
 * The class parameter is ignored.
 *
 */

static ssize_t bonding_store_bonds(struct class *cls, const char *buffer, size_t count)
{
	char command[IFNAMSIZ + 1] = {0, };
	char *ifname;
	int res = count;
	struct bonding *bond;
	struct bonding *nxt;

	down_write(&(bonding_rwsem));
	sscanf(buffer, "%16s", command); /* IFNAMSIZ*/
	ifname = command + 1;
	if ((strlen(command) <= 1) ||
	    !dev_valid_name(ifname))
		goto err_no_cmd;

	if (command[0] == '+') {

		/* Check to see if the bond already exists. */
		list_for_each_entry_safe(bond, nxt, &bond_dev_list, bond_list)
			if (strnicmp(bond->dev->name, ifname, IFNAMSIZ) == 0) {
				printk(KERN_ERR DRV_NAME
					": cannot add bond %s; it already exists\n",
					ifname);
				res = -EPERM;
				goto out;
			}

		printk(KERN_INFO DRV_NAME
			": %s is being created...\n", ifname);
		if (bond_create(ifname, &bonding_defaults, &bond)) {
			printk(KERN_INFO DRV_NAME
			": %s interface already exists. Bond creation failed.\n",
			ifname);
			res = -EPERM;
		}
		goto out;
	}

	if (command[0] == '-') {
		list_for_each_entry_safe(bond, nxt, &bond_dev_list, bond_list)
			if (strnicmp(bond->dev->name, ifname, IFNAMSIZ) == 0) {
				rtnl_lock();
				/* check the ref count on the bond's kobject.
				 * If it's > expected, then there's a file open,
				 * and we have to fail.
				 */
				if (atomic_read(&bond->dev->dev.kobj.kref.refcount)
							> expected_refcount){
					rtnl_unlock();
					printk(KERN_INFO DRV_NAME
						": Unable remove bond %s due to open references.\n",
						ifname);
					res = -EPERM;
					goto out;
				}
				printk(KERN_INFO DRV_NAME
					": %s is being deleted...\n",
					bond->dev->name);
				bond_destroy(bond);
				rtnl_unlock();
				goto out;
			}

		printk(KERN_ERR DRV_NAME
			": unable to delete non-existent bond %s\n", ifname);
		res = -ENODEV;
		goto out;
	}

err_no_cmd:
	printk(KERN_ERR DRV_NAME
		": no command found in bonding_masters. Use +ifname or -ifname.\n");
	res = -EPERM;

	/* Always return either count or an error.  If you return 0, you'll
	 * get called forever, which is bad.
	 */
out:
	up_write(&(bonding_rwsem));
	return res;
}
/* class attribute for bond_masters file.  This ends up in /sys/class/net */
static CLASS_ATTR(bonding_masters,  S_IWUSR | S_IRUGO,
		  bonding_show_bonds, bonding_store_bonds);

int bond_create_slave_symlinks(struct net_device *master, struct net_device *slave)
{
	char linkname[IFNAMSIZ+7];
	int ret = 0;

	/* first, create a link from the slave back to the master */
	ret = sysfs_create_link(&(slave->dev.kobj), &(master->dev.kobj),
				"master");
	if (ret)
		return ret;
	/* next, create a link from the master to the slave */
	sprintf(linkname,"slave_%s",slave->name);
	ret = sysfs_create_link(&(master->dev.kobj), &(slave->dev.kobj),
				linkname);
	return ret;

}

void bond_destroy_slave_symlinks(struct net_device *master, struct net_device *slave)
{
	char linkname[IFNAMSIZ+7];

	sysfs_remove_link(&(slave->dev.kobj), "master");
	sprintf(linkname,"slave_%s",slave->name);
	sysfs_remove_link(&(master->dev.kobj), linkname);
}


/*
 * Show the slaves in the current bond.
 */
static ssize_t bonding_show_slaves(struct device *d,
				   struct device_attribute *attr, char *buf)
{
	struct slave *slave;
	int i, res = 0;
	struct bonding *bond = to_bond(d);

	read_lock(&bond->lock);
	bond_for_each_slave(bond, slave, i) {
		if (res > (PAGE_SIZE - IFNAMSIZ)) {
			/* not enough space for another interface name */
			if ((PAGE_SIZE - res) > 10)
				res = PAGE_SIZE - 10;
			res += sprintf(buf + res, "++more++");
			break;
		}
		res += sprintf(buf + res, "%s ", slave->dev->name);
	}
	read_unlock(&bond->lock);
	res += sprintf(buf + res, "\n");
	res++;
	return res;
}

/*
 * Set the slaves in the current bond.  The bond interface must be
 * up for this to succeed.
 * This function is largely the same flow as bonding_update_bonds().
 */
static ssize_t bonding_store_slaves(struct device *d,
				    struct device_attribute *attr,
				    const char *buffer, size_t count)
{
	char command[IFNAMSIZ + 1] = { 0, };
	char *ifname;
	int i, res, found, ret = count;
	u32 original_mtu;
	struct slave *slave;
	struct net_device *dev = NULL;
	struct bonding *bond = to_bond(d);

	/* Quick sanity check -- is the bond interface up? */
	if (!(bond->dev->flags & IFF_UP)) {
		printk(KERN_WARNING DRV_NAME
		       ": %s: doing slave updates when interface is down.\n",
		       bond->dev->name);
	}

	/* Note:  We can't hold bond->lock here, as bond_create grabs it. */

	sscanf(buffer, "%16s", command); /* IFNAMSIZ*/
	ifname = command + 1;
	if ((strlen(command) <= 1) ||
	    !dev_valid_name(ifname))
		goto err_no_cmd;

	if (command[0] == '+') {

		/* Got a slave name in ifname.  Is it already in the list? */
		found = 0;
		read_lock(&bond->lock);
		bond_for_each_slave(bond, slave, i)
			if (strnicmp(slave->dev->name, ifname, IFNAMSIZ) == 0) {
				printk(KERN_ERR DRV_NAME
				       ": %s: Interface %s is already enslaved!\n",
				       bond->dev->name, ifname);
				ret = -EPERM;
				read_unlock(&bond->lock);
				goto out;
			}

		read_unlock(&bond->lock);
		printk(KERN_INFO DRV_NAME ": %s: Adding slave %s.\n",
		       bond->dev->name, ifname);
		dev = dev_get_by_name(&init_net, ifname);
		if (!dev) {
			printk(KERN_INFO DRV_NAME
			       ": %s: Interface %s does not exist!\n",
			       bond->dev->name, ifname);
			ret = -EPERM;
			goto out;
		}
		else
			dev_put(dev);

		if (dev->flags & IFF_UP) {
			printk(KERN_ERR DRV_NAME
			       ": %s: Error: Unable to enslave %s "
			       "because it is already up.\n",
			       bond->dev->name, dev->name);
			ret = -EPERM;
			goto out;
		}
		/* If this is the first slave, then we need to set
		   the master's hardware address to be the same as the
		   slave's. */
		if (!(*((u32 *) & (bond->dev->dev_addr[0])))) {
			memcpy(bond->dev->dev_addr, dev->dev_addr,
			       dev->addr_len);
		}

		/* Set the slave's MTU to match the bond */
		original_mtu = dev->mtu;
		if (dev->mtu != bond->dev->mtu) {
			if (dev->change_mtu) {
				res = dev->change_mtu(dev,
						      bond->dev->mtu);
				if (res) {
					ret = res;
					goto out;
				}
			} else {
				dev->mtu = bond->dev->mtu;
			}
		}
		rtnl_lock();
		res = bond_enslave(bond->dev, dev);
		bond_for_each_slave(bond, slave, i)
			if (strnicmp(slave->dev->name, ifname, IFNAMSIZ) == 0)
				slave->original_mtu = original_mtu;
		rtnl_unlock();
		if (res) {
			ret = res;
		}
		goto out;
	}

	if (command[0] == '-') {
		dev = NULL;
		bond_for_each_slave(bond, slave, i)
			if (strnicmp(slave->dev->name, ifname, IFNAMSIZ) == 0) {
				dev = slave->dev;
				original_mtu = slave->original_mtu;
				break;
			}
		if (dev) {
			printk(KERN_INFO DRV_NAME ": %s: Removing slave %s\n",
				bond->dev->name, dev->name);
			rtnl_lock();
			if (bond->setup_by_slave)
				res = bond_release_and_destroy(bond->dev, dev);
			else
				res = bond_release(bond->dev, dev);
			rtnl_unlock();
			if (res) {
				ret = res;
				goto out;
			}
			/* set the slave MTU to the default */
			if (dev->change_mtu) {
				dev->change_mtu(dev, original_mtu);
			} else {
				dev->mtu = original_mtu;
			}
		}
		else {
			printk(KERN_ERR DRV_NAME ": unable to remove non-existent slave %s for bond %s.\n",
				ifname, bond->dev->name);
			ret = -ENODEV;
		}
		goto out;
	}

err_no_cmd:
	printk(KERN_ERR DRV_NAME ": no command found in slaves file for bond %s. Use +ifname or -ifname.\n", bond->dev->name);
	ret = -EPERM;

out:
	return ret;
}

static DEVICE_ATTR(slaves, S_IRUGO | S_IWUSR, bonding_show_slaves, bonding_store_slaves);

/*
 * Show and set the bonding mode.  The bond interface must be down to
 * change the mode.
 */
static ssize_t bonding_show_mode(struct device *d,
				 struct device_attribute *attr, char *buf)
{
	struct bonding *bond = to_bond(d);

	return sprintf(buf, "%s %d\n",
			bond_mode_tbl[bond->params.mode].modename,
			bond->params.mode) + 1;
}

static ssize_t bonding_store_mode(struct device *d,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);

	if (bond->dev->flags & IFF_UP) {
		printk(KERN_ERR DRV_NAME
		       ": unable to update mode of %s because interface is up.\n",
		       bond->dev->name);
		ret = -EPERM;
		goto out;
	}

	new_value = bond_parse_parm((char *)buf, bond_mode_tbl);
	if (new_value < 0)  {
		printk(KERN_ERR DRV_NAME
		       ": %s: Ignoring invalid mode value %.*s.\n",
		       bond->dev->name,
		       (int)strlen(buf) - 1, buf);
		ret = -EINVAL;
		goto out;
	} else {
		if (bond->params.mode == BOND_MODE_8023AD)
			bond_unset_master_3ad_flags(bond);

		if (bond->params.mode == BOND_MODE_ALB)
			bond_unset_master_alb_flags(bond);

		bond->params.mode = new_value;
		bond_set_mode_ops(bond, bond->params.mode);
		printk(KERN_INFO DRV_NAME ": %s: setting mode to %s (%d).\n",
			bond->dev->name, bond_mode_tbl[new_value].modename, new_value);
	}
out:
	return ret;
}
static DEVICE_ATTR(mode, S_IRUGO | S_IWUSR, bonding_show_mode, bonding_store_mode);

/*
 * Show and set the bonding transmit hash method.  The bond interface must be down to
 * change the xmit hash policy.
 */
static ssize_t bonding_show_xmit_hash(struct device *d,
				      struct device_attribute *attr,
				      char *buf)
{
	int count;
	struct bonding *bond = to_bond(d);

	if ((bond->params.mode != BOND_MODE_XOR) &&
	    (bond->params.mode != BOND_MODE_8023AD)) {
		// Not Applicable
		count = sprintf(buf, "NA\n") + 1;
	} else {
		count = sprintf(buf, "%s %d\n",
			xmit_hashtype_tbl[bond->params.xmit_policy].modename,
			bond->params.xmit_policy) + 1;
	}

	return count;
}

static ssize_t bonding_store_xmit_hash(struct device *d,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);

	if (bond->dev->flags & IFF_UP) {
		printk(KERN_ERR DRV_NAME
		       "%s: Interface is up. Unable to update xmit policy.\n",
		       bond->dev->name);
		ret = -EPERM;
		goto out;
	}

	if ((bond->params.mode != BOND_MODE_XOR) &&
	    (bond->params.mode != BOND_MODE_8023AD)) {
		printk(KERN_ERR DRV_NAME
		       "%s: Transmit hash policy is irrelevant in this mode.\n",
		       bond->dev->name);
		ret = -EPERM;
		goto out;
	}

	new_value = bond_parse_parm((char *)buf, xmit_hashtype_tbl);
	if (new_value < 0)  {
		printk(KERN_ERR DRV_NAME
		       ": %s: Ignoring invalid xmit hash policy value %.*s.\n",
		       bond->dev->name,
		       (int)strlen(buf) - 1, buf);
		ret = -EINVAL;
		goto out;
	} else {
		bond->params.xmit_policy = new_value;
		bond_set_mode_ops(bond, bond->params.mode);
		printk(KERN_INFO DRV_NAME ": %s: setting xmit hash policy to %s (%d).\n",
			bond->dev->name, xmit_hashtype_tbl[new_value].modename, new_value);
	}
out:
	return ret;
}
static DEVICE_ATTR(xmit_hash_policy, S_IRUGO | S_IWUSR, bonding_show_xmit_hash, bonding_store_xmit_hash);

/*
 * Show and set arp_validate.
 */
static ssize_t bonding_show_arp_validate(struct device *d,
					 struct device_attribute *attr,
					 char *buf)
{
	struct bonding *bond = to_bond(d);

	return sprintf(buf, "%s %d\n",
		       arp_validate_tbl[bond->params.arp_validate].modename,
		       bond->params.arp_validate) + 1;
}

static ssize_t bonding_store_arp_validate(struct device *d,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	int new_value;
	struct bonding *bond = to_bond(d);

	new_value = bond_parse_parm((char *)buf, arp_validate_tbl);
	if (new_value < 0) {
		printk(KERN_ERR DRV_NAME
		       ": %s: Ignoring invalid arp_validate value %s\n",
		       bond->dev->name, buf);
		return -EINVAL;
	}
	if (new_value && (bond->params.mode != BOND_MODE_ACTIVEBACKUP)) {
		printk(KERN_ERR DRV_NAME
		       ": %s: arp_validate only supported in active-backup mode.\n",
		       bond->dev->name);
		return -EINVAL;
	}
	printk(KERN_INFO DRV_NAME ": %s: setting arp_validate to %s (%d).\n",
	       bond->dev->name, arp_validate_tbl[new_value].modename,
	       new_value);

	if (!bond->params.arp_validate && new_value) {
		bond_register_arp(bond);
	} else if (bond->params.arp_validate && !new_value) {
		bond_unregister_arp(bond);
	}

	bond->params.arp_validate = new_value;

	return count;
}

static DEVICE_ATTR(arp_validate, S_IRUGO | S_IWUSR, bonding_show_arp_validate, bonding_store_arp_validate);

/*
 * Show and store fail_over_mac.  User only allowed to change the
 * value when there are no slaves.
 */
static ssize_t bonding_show_fail_over_mac(struct device *d, struct device_attribute *attr, char *buf)
{
	struct bonding *bond = to_bond(d);

	return sprintf(buf, "%d\n", bond->params.fail_over_mac) + 1;
}

static ssize_t bonding_store_fail_over_mac(struct device *d, struct device_attribute *attr, const char *buf, size_t count)
{
	int new_value;
	int ret = count;
	struct bonding *bond = to_bond(d);

	if (bond->slave_cnt != 0) {
		printk(KERN_ERR DRV_NAME
		       ": %s: Can't alter fail_over_mac with slaves in bond.\n",
		       bond->dev->name);
		ret = -EPERM;
		goto out;
	}

	if (sscanf(buf, "%d", &new_value) != 1) {
		printk(KERN_ERR DRV_NAME
		       ": %s: no fail_over_mac value specified.\n",
		       bond->dev->name);
		ret = -EINVAL;
		goto out;
	}

	if ((new_value == 0) || (new_value == 1)) {
		bond->params.fail_over_mac = new_value;
		printk(KERN_INFO DRV_NAME ": %s: Setting fail_over_mac to %d.\n",
		       bond->dev->name, new_value);
	} else {
		printk(KERN_INFO DRV_NAME
		       ": %s: Ignoring invalid fail_over_mac value %d.\n",
		       bond->dev->name, new_value);
	}
out:
	return ret;
}

static DEVICE_ATTR(fail_over_mac, S_IRUGO | S_IWUSR, bonding_show_fail_over_mac, bonding_store_fail_over_mac);

/*
 * Show and set the arp timer interval.  There are two tricky bits
 * here.  First, if ARP monitoring is activated, then we must disable
 * MII monitoring.  Second, if the ARP timer isn't running, we must
 * start it.
 */
static ssize_t bonding_show_arp_interval(struct device *d,
					 struct device_attribute *attr,
					 char *buf)
{
	struct bonding *bond = to_bond(d);

	return sprintf(buf, "%d\n", bond->params.arp_interval) + 1;
}

static ssize_t bonding_store_arp_interval(struct device *d,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);

	if (sscanf(buf, "%d", &new_value) != 1) {
		printk(KERN_ERR DRV_NAME
		       ": %s: no arp_interval value specified.\n",
		       bond->dev->name);
		ret = -EINVAL;
		goto out;
	}
	if (new_value < 0) {
		printk(KERN_ERR DRV_NAME
		       ": %s: Invalid arp_interval value %d not in range 1-%d; rejected.\n",
		       bond->dev->name, new_value, INT_MAX);
		ret = -EINVAL;
		goto out;
	}

	printk(KERN_INFO DRV_NAME
	       ": %s: Setting ARP monitoring interval to %d.\n",
	       bond->dev->name, new_value);
	bond->params.arp_interval = new_value;
	if (bond->params.miimon) {
		printk(KERN_INFO DRV_NAME
		       ": %s: ARP monitoring cannot be used with MII monitoring. "
		       "%s Disabling MII monitoring.\n",
		       bond->dev->name, bond->dev->name);
		bond->params.miimon = 0;
		if (delayed_work_pending(&bond->mii_work)) {
			cancel_delayed_work(&bond->mii_work);
			flush_workqueue(bond->wq);
		}
	}
	if (!bond->params.arp_targets[0]) {
		printk(KERN_INFO DRV_NAME
		       ": %s: ARP monitoring has been set up, "
		       "but no ARP targets have been specified.\n",
		       bond->dev->name);
	}
	if (bond->dev->flags & IFF_UP) {
		/* If the interface is up, we may need to fire off
		 * the ARP timer.  If the interface is down, the
		 * timer will get fired off when the open function
		 * is called.
		 */
		if (!delayed_work_pending(&bond->arp_work)) {
			if (bond->params.mode == BOND_MODE_ACTIVEBACKUP)
				INIT_DELAYED_WORK(&bond->arp_work,
						  bond_activebackup_arp_mon);
			else
				INIT_DELAYED_WORK(&bond->arp_work,
						  bond_loadbalance_arp_mon);

			queue_delayed_work(bond->wq, &bond->arp_work, 0);
		}
	}

out:
	return ret;
}
static DEVICE_ATTR(arp_interval, S_IRUGO | S_IWUSR , bonding_show_arp_interval, bonding_store_arp_interval);

/*
 * Show and set the arp targets.
 */
static ssize_t bonding_show_arp_targets(struct device *d,
					struct device_attribute *attr,
					char *buf)
{
	int i, res = 0;
	struct bonding *bond = to_bond(d);

	for (i = 0; i < BOND_MAX_ARP_TARGETS; i++) {
		if (bond->params.arp_targets[i])
			res += sprintf(buf + res, "%u.%u.%u.%u ",
			       NIPQUAD(bond->params.arp_targets[i]));
	}
	if (res)
		res--;  /* eat the leftover space */
	res += sprintf(buf + res, "\n");
	res++;
	return res;
}

static ssize_t bonding_store_arp_targets(struct device *d,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	__be32 newtarget;
	int i = 0, done = 0, ret = count;
	struct bonding *bond = to_bond(d);
	__be32 *targets;

	targets = bond->params.arp_targets;
	newtarget = in_aton(buf + 1);
	/* look for adds */
	if (buf[0] == '+') {
		if ((newtarget == 0) || (newtarget == htonl(INADDR_BROADCAST))) {
			printk(KERN_ERR DRV_NAME
			       ": %s: invalid ARP target %u.%u.%u.%u specified for addition\n",
 			       bond->dev->name, NIPQUAD(newtarget));
			ret = -EINVAL;
			goto out;
		}
		/* look for an empty slot to put the target in, and check for dupes */
		for (i = 0; (i < BOND_MAX_ARP_TARGETS); i++) {
			if (targets[i] == newtarget) { /* duplicate */
				printk(KERN_ERR DRV_NAME
				       ": %s: ARP target %u.%u.%u.%u is already present\n",
				       bond->dev->name, NIPQUAD(newtarget));
				if (done)
					targets[i] = 0;
				ret = -EINVAL;
				goto out;
			}
			if (targets[i] == 0 && !done) {
				printk(KERN_INFO DRV_NAME
				       ": %s: adding ARP target %d.%d.%d.%d.\n",
				       bond->dev->name, NIPQUAD(newtarget));
				done = 1;
				targets[i] = newtarget;
			}
		}
		if (!done) {
			printk(KERN_ERR DRV_NAME
			       ": %s: ARP target table is full!\n",
			       bond->dev->name);
			ret = -EINVAL;
			goto out;
		}

	}
	else if (buf[0] == '-')	{
		if ((newtarget == 0) || (newtarget == htonl(INADDR_BROADCAST))) {
			printk(KERN_ERR DRV_NAME
			       ": %s: invalid ARP target %d.%d.%d.%d specified for removal\n",
			       bond->dev->name, NIPQUAD(newtarget));
			ret = -EINVAL;
			goto out;
		}

		for (i = 0; (i < BOND_MAX_ARP_TARGETS); i++) {
			if (targets[i] == newtarget) {
				printk(KERN_INFO DRV_NAME
				       ": %s: removing ARP target %d.%d.%d.%d.\n",
				       bond->dev->name, NIPQUAD(newtarget));
				targets[i] = 0;
				done = 1;
			}
		}
		if (!done) {
			printk(KERN_INFO DRV_NAME
			       ": %s: unable to remove nonexistent ARP target %d.%d.%d.%d.\n",
			       bond->dev->name, NIPQUAD(newtarget));
			ret = -EINVAL;
			goto out;
		}
	}
	else {
		printk(KERN_ERR DRV_NAME ": no command found in arp_ip_targets file for bond %s. Use +<addr> or -<addr>.\n",
			bond->dev->name);
		ret = -EPERM;
		goto out;
	}

out:
	return ret;
}
static DEVICE_ATTR(arp_ip_target, S_IRUGO | S_IWUSR , bonding_show_arp_targets, bonding_store_arp_targets);

/*
 * Show and set the up and down delays.  These must be multiples of the
 * MII monitoring value, and are stored internally as the multiplier.
 * Thus, we must translate to MS for the real world.
 */
static ssize_t bonding_show_downdelay(struct device *d,
				      struct device_attribute *attr,
				      char *buf)
{
	struct bonding *bond = to_bond(d);

	return sprintf(buf, "%d\n", bond->params.downdelay * bond->params.miimon) + 1;
}

static ssize_t bonding_store_downdelay(struct device *d,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);

	if (!(bond->params.miimon)) {
		printk(KERN_ERR DRV_NAME
		       ": %s: Unable to set down delay as MII monitoring is disabled\n",
		       bond->dev->name);
		ret = -EPERM;
		goto out;
	}

	if (sscanf(buf, "%d", &new_value) != 1) {
		printk(KERN_ERR DRV_NAME
		       ": %s: no down delay value specified.\n",
		       bond->dev->name);
		ret = -EINVAL;
		goto out;
	}
	if (new_value < 0) {
		printk(KERN_ERR DRV_NAME
		       ": %s: Invalid down delay value %d not in range %d-%d; rejected.\n",
		       bond->dev->name, new_value, 1, INT_MAX);
		ret = -EINVAL;
		goto out;
	} else {
		if ((new_value % bond->params.miimon) != 0) {
			printk(KERN_WARNING DRV_NAME
			       ": %s: Warning: down delay (%d) is not a multiple "
			       "of miimon (%d), delay rounded to %d ms\n",
			       bond->dev->name, new_value, bond->params.miimon,
			       (new_value / bond->params.miimon) *
			       bond->params.miimon);
		}
		bond->params.downdelay = new_value / bond->params.miimon;
		printk(KERN_INFO DRV_NAME ": %s: Setting down delay to %d.\n",
		       bond->dev->name, bond->params.downdelay * bond->params.miimon);

	}

out:
	return ret;
}
static DEVICE_ATTR(downdelay, S_IRUGO | S_IWUSR , bonding_show_downdelay, bonding_store_downdelay);

static ssize_t bonding_show_updelay(struct device *d,
				    struct device_attribute *attr,
				    char *buf)
{
	struct bonding *bond = to_bond(d);

	return sprintf(buf, "%d\n", bond->params.updelay * bond->params.miimon) + 1;

}

static ssize_t bonding_store_updelay(struct device *d,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);

	if (!(bond->params.miimon)) {
		printk(KERN_ERR DRV_NAME
		       ": %s: Unable to set up delay as MII monitoring is disabled\n",
		       bond->dev->name);
		ret = -EPERM;
		goto out;
	}

	if (sscanf(buf, "%d", &new_value) != 1) {
		printk(KERN_ERR DRV_NAME
		       ": %s: no up delay value specified.\n",
		       bond->dev->name);
		ret = -EINVAL;
		goto out;
	}
	if (new_value < 0) {
		printk(KERN_ERR DRV_NAME
		       ": %s: Invalid down delay value %d not in range %d-%d; rejected.\n",
		       bond->dev->name, new_value, 1, INT_MAX);
		ret = -EINVAL;
		goto out;
	} else {
		if ((new_value % bond->params.miimon) != 0) {
			printk(KERN_WARNING DRV_NAME
			       ": %s: Warning: up delay (%d) is not a multiple "
			       "of miimon (%d), updelay rounded to %d ms\n",
			       bond->dev->name, new_value, bond->params.miimon,
			       (new_value / bond->params.miimon) *
			       bond->params.miimon);
		}
		bond->params.updelay = new_value / bond->params.miimon;
		printk(KERN_INFO DRV_NAME ": %s: Setting up delay to %d.\n",
		       bond->dev->name, bond->params.updelay * bond->params.miimon);

	}

out:
	return ret;
}
static DEVICE_ATTR(updelay, S_IRUGO | S_IWUSR , bonding_show_updelay, bonding_store_updelay);

/*
 * Show and set the LACP interval.  Interface must be down, and the mode
 * must be set to 802.3ad mode.
 */
static ssize_t bonding_show_lacp(struct device *d,
				 struct device_attribute *attr,
				 char *buf)
{
	struct bonding *bond = to_bond(d);

	return sprintf(buf, "%s %d\n",
		bond_lacp_tbl[bond->params.lacp_fast].modename,
		bond->params.lacp_fast) + 1;
}

static ssize_t bonding_store_lacp(struct device *d,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);

	if (bond->dev->flags & IFF_UP) {
		printk(KERN_ERR DRV_NAME
		       ": %s: Unable to update LACP rate because interface is up.\n",
		       bond->dev->name);
		ret = -EPERM;
		goto out;
	}

	if (bond->params.mode != BOND_MODE_8023AD) {
		printk(KERN_ERR DRV_NAME
		       ": %s: Unable to update LACP rate because bond is not in 802.3ad mode.\n",
		       bond->dev->name);
		ret = -EPERM;
		goto out;
	}

	new_value = bond_parse_parm((char *)buf, bond_lacp_tbl);

	if ((new_value == 1) || (new_value == 0)) {
		bond->params.lacp_fast = new_value;
		printk(KERN_INFO DRV_NAME
		       ": %s: Setting LACP rate to %s (%d).\n",
		       bond->dev->name, bond_lacp_tbl[new_value].modename, new_value);
	} else {
		printk(KERN_ERR DRV_NAME
		       ": %s: Ignoring invalid LACP rate value %.*s.\n",
		     	bond->dev->name, (int)strlen(buf) - 1, buf);
		ret = -EINVAL;
	}
out:
	return ret;
}
static DEVICE_ATTR(lacp_rate, S_IRUGO | S_IWUSR, bonding_show_lacp, bonding_store_lacp);

/*
 * Show and set the MII monitor interval.  There are two tricky bits
 * here.  First, if MII monitoring is activated, then we must disable
 * ARP monitoring.  Second, if the timer isn't running, we must
 * start it.
 */
static ssize_t bonding_show_miimon(struct device *d,
				   struct device_attribute *attr,
				   char *buf)
{
	struct bonding *bond = to_bond(d);

	return sprintf(buf, "%d\n", bond->params.miimon) + 1;
}

static ssize_t bonding_store_miimon(struct device *d,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);

	if (sscanf(buf, "%d", &new_value) != 1) {
		printk(KERN_ERR DRV_NAME
		       ": %s: no miimon value specified.\n",
		       bond->dev->name);
		ret = -EINVAL;
		goto out;
	}
	if (new_value < 0) {
		printk(KERN_ERR DRV_NAME
		       ": %s: Invalid miimon value %d not in range %d-%d; rejected.\n",
		       bond->dev->name, new_value, 1, INT_MAX);
		ret = -EINVAL;
		goto out;
	} else {
		printk(KERN_INFO DRV_NAME
		       ": %s: Setting MII monitoring interval to %d.\n",
		       bond->dev->name, new_value);
		bond->params.miimon = new_value;
		if(bond->params.updelay)
			printk(KERN_INFO DRV_NAME
			      ": %s: Note: Updating updelay (to %d) "
			      "since it is a multiple of the miimon value.\n",
			      bond->dev->name,
			      bond->params.updelay * bond->params.miimon);
		if(bond->params.downdelay)
			printk(KERN_INFO DRV_NAME
			      ": %s: Note: Updating downdelay (to %d) "
			      "since it is a multiple of the miimon value.\n",
			      bond->dev->name,
			      bond->params.downdelay * bond->params.miimon);
		if (bond->params.arp_interval) {
			printk(KERN_INFO DRV_NAME
			       ": %s: MII monitoring cannot be used with "
			       "ARP monitoring. Disabling ARP monitoring...\n",
			       bond->dev->name);
			bond->params.arp_interval = 0;
			if (bond->params.arp_validate) {
				bond_unregister_arp(bond);
				bond->params.arp_validate =
					BOND_ARP_VALIDATE_NONE;
			}
			if (delayed_work_pending(&bond->arp_work)) {
				cancel_delayed_work(&bond->arp_work);
				flush_workqueue(bond->wq);
			}
		}

		if (bond->dev->flags & IFF_UP) {
			/* If the interface is up, we may need to fire off
			 * the MII timer. If the interface is down, the
			 * timer will get fired off when the open function
			 * is called.
			 */
			if (!delayed_work_pending(&bond->mii_work)) {
				INIT_DELAYED_WORK(&bond->mii_work,
						  bond_mii_monitor);
				queue_delayed_work(bond->wq,
						   &bond->mii_work, 0);
			}
		}
	}
out:
	return ret;
}
static DEVICE_ATTR(miimon, S_IRUGO | S_IWUSR, bonding_show_miimon, bonding_store_miimon);

/*
 * Show and set the primary slave.  The store function is much
 * simpler than bonding_store_slaves function because it only needs to
 * handle one interface name.
 * The bond must be a mode that supports a primary for this be
 * set.
 */
static ssize_t bonding_show_primary(struct device *d,
				    struct device_attribute *attr,
				    char *buf)
{
	int count = 0;
	struct bonding *bond = to_bond(d);

	if (bond->primary_slave)
		count = sprintf(buf, "%s\n", bond->primary_slave->dev->name) + 1;
	else
		count = sprintf(buf, "\n") + 1;

	return count;
}

static ssize_t bonding_store_primary(struct device *d,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int i;
	struct slave *slave;
	struct bonding *bond = to_bond(d);

	write_lock_bh(&bond->lock);
	if (!USES_PRIMARY(bond->params.mode)) {
		printk(KERN_INFO DRV_NAME
		       ": %s: Unable to set primary slave; %s is in mode %d\n",
		       bond->dev->name, bond->dev->name, bond->params.mode);
	} else {
		bond_for_each_slave(bond, slave, i) {
			if (strnicmp
			    (slave->dev->name, buf,
			     strlen(slave->dev->name)) == 0) {
				printk(KERN_INFO DRV_NAME
				       ": %s: Setting %s as primary slave.\n",
				       bond->dev->name, slave->dev->name);
				bond->primary_slave = slave;
				bond_select_active_slave(bond);
				goto out;
			}
		}

		/* if we got here, then we didn't match the name of any slave */

		if (strlen(buf) == 0 || buf[0] == '\n') {
			printk(KERN_INFO DRV_NAME
			       ": %s: Setting primary slave to None.\n",
			       bond->dev->name);
			bond->primary_slave = NULL;
				bond_select_active_slave(bond);
		} else {
			printk(KERN_INFO DRV_NAME
			       ": %s: Unable to set %.*s as primary slave as it is not a slave.\n",
			       bond->dev->name, (int)strlen(buf) - 1, buf);
		}
	}
out:
	write_unlock_bh(&bond->lock);

	rtnl_unlock();

	return count;
}
static DEVICE_ATTR(primary, S_IRUGO | S_IWUSR, bonding_show_primary, bonding_store_primary);

/*
 * Show and set the use_carrier flag.
 */
static ssize_t bonding_show_carrier(struct device *d,
				    struct device_attribute *attr,
				    char *buf)
{
	struct bonding *bond = to_bond(d);

	return sprintf(buf, "%d\n", bond->params.use_carrier) + 1;
}

static ssize_t bonding_store_carrier(struct device *d,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);


	if (sscanf(buf, "%d", &new_value) != 1) {
		printk(KERN_ERR DRV_NAME
		       ": %s: no use_carrier value specified.\n",
		       bond->dev->name);
		ret = -EINVAL;
		goto out;
	}
	if ((new_value == 0) || (new_value == 1)) {
		bond->params.use_carrier = new_value;
		printk(KERN_INFO DRV_NAME ": %s: Setting use_carrier to %d.\n",
		       bond->dev->name, new_value);
	} else {
		printk(KERN_INFO DRV_NAME
		       ": %s: Ignoring invalid use_carrier value %d.\n",
		       bond->dev->name, new_value);
	}
out:
	return count;
}
static DEVICE_ATTR(use_carrier, S_IRUGO | S_IWUSR, bonding_show_carrier, bonding_store_carrier);


/*
 * Show and set currently active_slave.
 */
static ssize_t bonding_show_active_slave(struct device *d,
					 struct device_attribute *attr,
					 char *buf)
{
	struct slave *curr;
	struct bonding *bond = to_bond(d);
	int count;

	read_lock(&bond->curr_slave_lock);
	curr = bond->curr_active_slave;
	read_unlock(&bond->curr_slave_lock);

	if (USES_PRIMARY(bond->params.mode) && curr)
		count = sprintf(buf, "%s\n", curr->dev->name) + 1;
	else
		count = sprintf(buf, "\n") + 1;
	return count;
}

static ssize_t bonding_store_active_slave(struct device *d,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	int i;
	struct slave *slave;
        struct slave *old_active = NULL;
        struct slave *new_active = NULL;
	struct bonding *bond = to_bond(d);

	rtnl_lock();
	write_lock_bh(&bond->lock);

	if (!USES_PRIMARY(bond->params.mode)) {
		printk(KERN_INFO DRV_NAME
		       ": %s: Unable to change active slave; %s is in mode %d\n",
		       bond->dev->name, bond->dev->name, bond->params.mode);
	} else {
		bond_for_each_slave(bond, slave, i) {
			if (strnicmp
			    (slave->dev->name, buf,
			     strlen(slave->dev->name)) == 0) {
        			old_active = bond->curr_active_slave;
        			new_active = slave;
        			if (new_active == old_active) {
					/* do nothing */
					printk(KERN_INFO DRV_NAME
				       	       ": %s: %s is already the current active slave.\n",
				               bond->dev->name, slave->dev->name);
					goto out;
				}
				else {
        				if ((new_active) &&
            				    (old_active) &&
				            (new_active->link == BOND_LINK_UP) &&
				            IS_UP(new_active->dev)) {
						printk(KERN_INFO DRV_NAME
				       	              ": %s: Setting %s as active slave.\n",
				                      bond->dev->name, slave->dev->name);
                				bond_change_active_slave(bond, new_active);
        				}
					else {
						printk(KERN_INFO DRV_NAME
				       	              ": %s: Could not set %s as active slave; "
						      "either %s is down or the link is down.\n",
				                      bond->dev->name, slave->dev->name,
						      slave->dev->name);
					}
					goto out;
				}
			}
		}

		/* if we got here, then we didn't match the name of any slave */

		if (strlen(buf) == 0 || buf[0] == '\n') {
			printk(KERN_INFO DRV_NAME
			       ": %s: Setting active slave to None.\n",
			       bond->dev->name);
			bond->primary_slave = NULL;
				bond_select_active_slave(bond);
		} else {
			printk(KERN_INFO DRV_NAME
			       ": %s: Unable to set %.*s as active slave as it is not a slave.\n",
			       bond->dev->name, (int)strlen(buf) - 1, buf);
		}
	}
out:
	write_unlock_bh(&bond->lock);
	rtnl_unlock();

	return count;

}
static DEVICE_ATTR(active_slave, S_IRUGO | S_IWUSR, bonding_show_active_slave, bonding_store_active_slave);


/*
 * Show link status of the bond interface.
 */
static ssize_t bonding_show_mii_status(struct device *d,
				       struct device_attribute *attr,
				       char *buf)
{
	struct slave *curr;
	struct bonding *bond = to_bond(d);

	read_lock(&bond->curr_slave_lock);
	curr = bond->curr_active_slave;
	read_unlock(&bond->curr_slave_lock);

	return sprintf(buf, "%s\n", (curr) ? "up" : "down") + 1;
}
static DEVICE_ATTR(mii_status, S_IRUGO, bonding_show_mii_status, NULL);


/*
 * Show current 802.3ad aggregator ID.
 */
static ssize_t bonding_show_ad_aggregator(struct device *d,
					  struct device_attribute *attr,
					  char *buf)
{
	int count = 0;
	struct bonding *bond = to_bond(d);

	if (bond->params.mode == BOND_MODE_8023AD) {
		struct ad_info ad_info;
		count = sprintf(buf, "%d\n", (bond_3ad_get_active_agg_info(bond, &ad_info)) ?  0 : ad_info.aggregator_id) + 1;
	}
	else
		count = sprintf(buf, "\n") + 1;

	return count;
}
static DEVICE_ATTR(ad_aggregator, S_IRUGO, bonding_show_ad_aggregator, NULL);


/*
 * Show number of active 802.3ad ports.
 */
static ssize_t bonding_show_ad_num_ports(struct device *d,
					 struct device_attribute *attr,
					 char *buf)
{
	int count = 0;
	struct bonding *bond = to_bond(d);

	if (bond->params.mode == BOND_MODE_8023AD) {
		struct ad_info ad_info;
		count = sprintf(buf, "%d\n", (bond_3ad_get_active_agg_info(bond, &ad_info)) ?  0: ad_info.ports) + 1;
	}
	else
		count = sprintf(buf, "\n") + 1;

	return count;
}
static DEVICE_ATTR(ad_num_ports, S_IRUGO, bonding_show_ad_num_ports, NULL);


/*
 * Show current 802.3ad actor key.
 */
static ssize_t bonding_show_ad_actor_key(struct device *d,
					 struct device_attribute *attr,
					 char *buf)
{
	int count = 0;
	struct bonding *bond = to_bond(d);

	if (bond->params.mode == BOND_MODE_8023AD) {
		struct ad_info ad_info;
		count = sprintf(buf, "%d\n", (bond_3ad_get_active_agg_info(bond, &ad_info)) ?  0 : ad_info.actor_key) + 1;
	}
	else
		count = sprintf(buf, "\n") + 1;

	return count;
}
static DEVICE_ATTR(ad_actor_key, S_IRUGO, bonding_show_ad_actor_key, NULL);


/*
 * Show current 802.3ad partner key.
 */
static ssize_t bonding_show_ad_partner_key(struct device *d,
					   struct device_attribute *attr,
					   char *buf)
{
	int count = 0;
	struct bonding *bond = to_bond(d);

	if (bond->params.mode == BOND_MODE_8023AD) {
		struct ad_info ad_info;
		count = sprintf(buf, "%d\n", (bond_3ad_get_active_agg_info(bond, &ad_info)) ?  0 : ad_info.partner_key) + 1;
	}
	else
		count = sprintf(buf, "\n") + 1;

	return count;
}
static DEVICE_ATTR(ad_partner_key, S_IRUGO, bonding_show_ad_partner_key, NULL);


/*
 * Show current 802.3ad partner mac.
 */
static ssize_t bonding_show_ad_partner_mac(struct device *d,
					   struct device_attribute *attr,
					   char *buf)
{
	int count = 0;
	struct bonding *bond = to_bond(d);
	DECLARE_MAC_BUF(mac);

	if (bond->params.mode == BOND_MODE_8023AD) {
		struct ad_info ad_info;
		if (!bond_3ad_get_active_agg_info(bond, &ad_info)) {
			count = sprintf(buf,"%s\n",
					print_mac(mac, ad_info.partner_system))
				+ 1;
		}
	}
	else
		count = sprintf(buf, "\n") + 1;

	return count;
}
static DEVICE_ATTR(ad_partner_mac, S_IRUGO, bonding_show_ad_partner_mac, NULL);



static struct attribute *per_bond_attrs[] = {
	&dev_attr_slaves.attr,
	&dev_attr_mode.attr,
	&dev_attr_fail_over_mac.attr,
	&dev_attr_arp_validate.attr,
	&dev_attr_arp_interval.attr,
	&dev_attr_arp_ip_target.attr,
	&dev_attr_downdelay.attr,
	&dev_attr_updelay.attr,
	&dev_attr_lacp_rate.attr,
	&dev_attr_xmit_hash_policy.attr,
	&dev_attr_miimon.attr,
	&dev_attr_primary.attr,
	&dev_attr_use_carrier.attr,
	&dev_attr_active_slave.attr,
	&dev_attr_mii_status.attr,
	&dev_attr_ad_aggregator.attr,
	&dev_attr_ad_num_ports.attr,
	&dev_attr_ad_actor_key.attr,
	&dev_attr_ad_partner_key.attr,
	&dev_attr_ad_partner_mac.attr,
	NULL,
};

static struct attribute_group bonding_group = {
	.name = "bonding",
	.attrs = per_bond_attrs,
};

/*
 * Initialize sysfs.  This sets up the bonding_masters file in
 * /sys/class/net.
 */
int bond_create_sysfs(void)
{
	int ret = 0;
	struct bonding *firstbond;

	init_rwsem(&bonding_rwsem);

	/* get the netdev class pointer */
	firstbond = container_of(bond_dev_list.next, struct bonding, bond_list);
	if (!firstbond)
		return -ENODEV;

	netdev_class = firstbond->dev->dev.class;
	if (!netdev_class)
		return -ENODEV;

	ret = class_create_file(netdev_class, &class_attr_bonding_masters);
	/*
	 * Permit multiple loads of the module by ignoring failures to
	 * create the bonding_masters sysfs file.  Bonding devices
	 * created by second or subsequent loads of the module will
	 * not be listed in, or controllable by, bonding_masters, but
	 * will have the usual "bonding" sysfs directory.
	 *
	 * This is done to preserve backwards compatibility for
	 * initscripts/sysconfig, which load bonding multiple times to
	 * configure multiple bonding devices.
	 */
	if (ret == -EEXIST) {
		netdev_class = NULL;
		return 0;
	}

	return ret;

}

/*
 * Remove /sys/class/net/bonding_masters.
 */
void bond_destroy_sysfs(void)
{
	if (netdev_class)
		class_remove_file(netdev_class, &class_attr_bonding_masters);
}

/*
 * Initialize sysfs for each bond.  This sets up and registers
 * the 'bondctl' directory for each individual bond under /sys/class/net.
 */
int bond_create_sysfs_entry(struct bonding *bond)
{
	struct net_device *dev = bond->dev;
	int err;

	err = sysfs_create_group(&(dev->dev.kobj), &bonding_group);
	if (err) {
		printk(KERN_EMERG "eek! didn't create group!\n");
	}

	if (expected_refcount < 1)
		expected_refcount = atomic_read(&bond->dev->dev.kobj.kref.refcount);

	return err;
}
/*
 * Remove sysfs entries for each bond.
 */
void bond_destroy_sysfs_entry(struct bonding *bond)
{
	struct net_device *dev = bond->dev;

	sysfs_remove_group(&(dev->dev.kobj), &bonding_group);
}

