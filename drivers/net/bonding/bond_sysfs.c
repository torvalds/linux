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
#include <linux/etherdevice.h>
#include <net/net_namespace.h>

#include "bonding.h"

#define to_dev(obj)	container_of(obj, struct device, kobj)
#define to_bond(cd)	((struct bonding *)(netdev_priv(to_net_dev(cd))))

/*
 * "show" function for the bond_masters attribute.
 * The class parameter is ignored.
 */
static ssize_t bonding_show_bonds(struct class *cls, char *buf)
{
	int res = 0;
	struct bonding *bond;

	rtnl_lock();

	list_for_each_entry(bond, &bond_dev_list, bond_list) {
		if (res > (PAGE_SIZE - IFNAMSIZ)) {
			/* not enough space for another interface name */
			if ((PAGE_SIZE - res) > 10)
				res = PAGE_SIZE - 10;
			res += sprintf(buf + res, "++more++ ");
			break;
		}
		res += sprintf(buf + res, "%s ", bond->dev->name);
	}
	if (res)
		buf[res-1] = '\n'; /* eat the leftover space */

	rtnl_unlock();
	return res;
}

static struct net_device *bond_get_by_name(const char *ifname)
{
	struct bonding *bond;

	list_for_each_entry(bond, &bond_dev_list, bond_list) {
		if (strncmp(bond->dev->name, ifname, IFNAMSIZ) == 0)
			return bond->dev;
	}
	return NULL;
}

/*
 * "store" function for the bond_masters attribute.  This is what
 * creates and deletes entire bonds.
 *
 * The class parameter is ignored.
 *
 */

static ssize_t bonding_store_bonds(struct class *cls,
				   const char *buffer, size_t count)
{
	char command[IFNAMSIZ + 1] = {0, };
	char *ifname;
	int rv, res = count;

	sscanf(buffer, "%16s", command); /* IFNAMSIZ*/
	ifname = command + 1;
	if ((strlen(command) <= 1) ||
	    !dev_valid_name(ifname))
		goto err_no_cmd;

	if (command[0] == '+') {
		pr_info(DRV_NAME
			": %s is being created...\n", ifname);
		rv = bond_create(ifname);
		if (rv) {
			pr_info(DRV_NAME ": Bond creation failed.\n");
			res = rv;
		}
	} else if (command[0] == '-') {
		struct net_device *bond_dev;

		rtnl_lock();
		bond_dev = bond_get_by_name(ifname);
		if (bond_dev) {
			pr_info(DRV_NAME ": %s is being deleted...\n",
				ifname);
			unregister_netdevice(bond_dev);
		} else {
			pr_err(DRV_NAME ": unable to delete non-existent %s\n",
			       ifname);
			res = -ENODEV;
		}
		rtnl_unlock();
	} else
		goto err_no_cmd;

	/* Always return either count or an error.  If you return 0, you'll
	 * get called forever, which is bad.
	 */
	return res;

err_no_cmd:
	pr_err(DRV_NAME ": no command found in bonding_masters."
	       " Use +ifname or -ifname.\n");
	return -EPERM;
}

/* class attribute for bond_masters file.  This ends up in /sys/class/net */
static CLASS_ATTR(bonding_masters,  S_IWUSR | S_IRUGO,
		  bonding_show_bonds, bonding_store_bonds);

int bond_create_slave_symlinks(struct net_device *master,
			       struct net_device *slave)
{
	char linkname[IFNAMSIZ+7];
	int ret = 0;

	/* first, create a link from the slave back to the master */
	ret = sysfs_create_link(&(slave->dev.kobj), &(master->dev.kobj),
				"master");
	if (ret)
		return ret;
	/* next, create a link from the master to the slave */
	sprintf(linkname, "slave_%s", slave->name);
	ret = sysfs_create_link(&(master->dev.kobj), &(slave->dev.kobj),
				linkname);
	return ret;

}

void bond_destroy_slave_symlinks(struct net_device *master,
				 struct net_device *slave)
{
	char linkname[IFNAMSIZ+7];

	sysfs_remove_link(&(slave->dev.kobj), "master");
	sprintf(linkname, "slave_%s", slave->name);
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
			res += sprintf(buf + res, "++more++ ");
			break;
		}
		res += sprintf(buf + res, "%s ", slave->dev->name);
	}
	read_unlock(&bond->lock);
	if (res)
		buf[res-1] = '\n'; /* eat the leftover space */
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

	if (!rtnl_trylock())
		return restart_syscall();

	sscanf(buffer, "%16s", command); /* IFNAMSIZ*/
	ifname = command + 1;
	if ((strlen(command) <= 1) ||
	    !dev_valid_name(ifname))
		goto err_no_cmd;

	if (command[0] == '+') {

		/* Got a slave name in ifname.  Is it already in the list? */
		found = 0;

		/* FIXME: get netns from sysfs object */
		dev = __dev_get_by_name(&init_net, ifname);
		if (!dev) {
			pr_info(DRV_NAME
			       ": %s: Interface %s does not exist!\n",
			       bond->dev->name, ifname);
			ret = -ENODEV;
			goto out;
		}

		if (dev->flags & IFF_UP) {
			pr_err(DRV_NAME
			       ": %s: Error: Unable to enslave %s "
			       "because it is already up.\n",
			       bond->dev->name, dev->name);
			ret = -EPERM;
			goto out;
		}

		read_lock(&bond->lock);
		bond_for_each_slave(bond, slave, i)
			if (slave->dev == dev) {
				pr_err(DRV_NAME
				       ": %s: Interface %s is already enslaved!\n",
				       bond->dev->name, ifname);
				ret = -EPERM;
				read_unlock(&bond->lock);
				goto out;
			}
		read_unlock(&bond->lock);

		pr_info(DRV_NAME ": %s: Adding slave %s.\n",
			bond->dev->name, ifname);

		/* If this is the first slave, then we need to set
		   the master's hardware address to be the same as the
		   slave's. */
		if (is_zero_ether_addr(bond->dev->dev_addr))
			memcpy(bond->dev->dev_addr, dev->dev_addr,
			       dev->addr_len);

		/* Set the slave's MTU to match the bond */
		original_mtu = dev->mtu;
		res = dev_set_mtu(dev, bond->dev->mtu);
		if (res) {
			ret = res;
			goto out;
		}

		res = bond_enslave(bond->dev, dev);
		bond_for_each_slave(bond, slave, i)
			if (strnicmp(slave->dev->name, ifname, IFNAMSIZ) == 0)
				slave->original_mtu = original_mtu;
		if (res)
			ret = res;

		goto out;
	}

	if (command[0] == '-') {
		dev = NULL;
		original_mtu = 0;
		bond_for_each_slave(bond, slave, i)
			if (strnicmp(slave->dev->name, ifname, IFNAMSIZ) == 0) {
				dev = slave->dev;
				original_mtu = slave->original_mtu;
				break;
			}
		if (dev) {
			pr_info(DRV_NAME ": %s: Removing slave %s\n",
				bond->dev->name, dev->name);
				res = bond_release(bond->dev, dev);
			if (res) {
				ret = res;
				goto out;
			}
			/* set the slave MTU to the default */
			dev_set_mtu(dev, original_mtu);
		} else {
			pr_err(DRV_NAME ": unable to remove non-existent"
			       " slave %s for bond %s.\n",
				ifname, bond->dev->name);
			ret = -ENODEV;
		}
		goto out;
	}

err_no_cmd:
	pr_err(DRV_NAME ": no command found in slaves file for bond %s. Use +ifname or -ifname.\n", bond->dev->name);
	ret = -EPERM;

out:
	rtnl_unlock();
	return ret;
}

static DEVICE_ATTR(slaves, S_IRUGO | S_IWUSR, bonding_show_slaves,
		   bonding_store_slaves);

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
			bond->params.mode);
}

static ssize_t bonding_store_mode(struct device *d,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);

	if (bond->dev->flags & IFF_UP) {
		pr_err(DRV_NAME ": unable to update mode of %s"
		       " because interface is up.\n", bond->dev->name);
		ret = -EPERM;
		goto out;
	}

	new_value = bond_parse_parm(buf, bond_mode_tbl);
	if (new_value < 0)  {
		pr_err(DRV_NAME
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
		pr_info(DRV_NAME ": %s: setting mode to %s (%d).\n",
		       bond->dev->name, bond_mode_tbl[new_value].modename,
		       new_value);
	}
out:
	return ret;
}
static DEVICE_ATTR(mode, S_IRUGO | S_IWUSR,
		   bonding_show_mode, bonding_store_mode);

/*
 * Show and set the bonding transmit hash method.
 * The bond interface must be down to change the xmit hash policy.
 */
static ssize_t bonding_show_xmit_hash(struct device *d,
				      struct device_attribute *attr,
				      char *buf)
{
	struct bonding *bond = to_bond(d);

	return sprintf(buf, "%s %d\n",
		       xmit_hashtype_tbl[bond->params.xmit_policy].modename,
		       bond->params.xmit_policy);
}

static ssize_t bonding_store_xmit_hash(struct device *d,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);

	if (bond->dev->flags & IFF_UP) {
		pr_err(DRV_NAME
		       "%s: Interface is up. Unable to update xmit policy.\n",
		       bond->dev->name);
		ret = -EPERM;
		goto out;
	}

	new_value = bond_parse_parm(buf, xmit_hashtype_tbl);
	if (new_value < 0)  {
		pr_err(DRV_NAME
		       ": %s: Ignoring invalid xmit hash policy value %.*s.\n",
		       bond->dev->name,
		       (int)strlen(buf) - 1, buf);
		ret = -EINVAL;
		goto out;
	} else {
		bond->params.xmit_policy = new_value;
		bond_set_mode_ops(bond, bond->params.mode);
		pr_info(DRV_NAME ": %s: setting xmit hash policy to %s (%d).\n",
			bond->dev->name,
			xmit_hashtype_tbl[new_value].modename, new_value);
	}
out:
	return ret;
}
static DEVICE_ATTR(xmit_hash_policy, S_IRUGO | S_IWUSR,
		   bonding_show_xmit_hash, bonding_store_xmit_hash);

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
		       bond->params.arp_validate);
}

static ssize_t bonding_store_arp_validate(struct device *d,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	int new_value;
	struct bonding *bond = to_bond(d);

	new_value = bond_parse_parm(buf, arp_validate_tbl);
	if (new_value < 0) {
		pr_err(DRV_NAME
		       ": %s: Ignoring invalid arp_validate value %s\n",
		       bond->dev->name, buf);
		return -EINVAL;
	}
	if (new_value && (bond->params.mode != BOND_MODE_ACTIVEBACKUP)) {
		pr_err(DRV_NAME
		       ": %s: arp_validate only supported in active-backup mode.\n",
		       bond->dev->name);
		return -EINVAL;
	}
	pr_info(DRV_NAME ": %s: setting arp_validate to %s (%d).\n",
	       bond->dev->name, arp_validate_tbl[new_value].modename,
	       new_value);

	if (!bond->params.arp_validate && new_value)
		bond_register_arp(bond);
	else if (bond->params.arp_validate && !new_value)
		bond_unregister_arp(bond);

	bond->params.arp_validate = new_value;

	return count;
}

static DEVICE_ATTR(arp_validate, S_IRUGO | S_IWUSR, bonding_show_arp_validate,
		   bonding_store_arp_validate);

/*
 * Show and store fail_over_mac.  User only allowed to change the
 * value when there are no slaves.
 */
static ssize_t bonding_show_fail_over_mac(struct device *d,
					  struct device_attribute *attr,
					  char *buf)
{
	struct bonding *bond = to_bond(d);

	return sprintf(buf, "%s %d\n",
		       fail_over_mac_tbl[bond->params.fail_over_mac].modename,
		       bond->params.fail_over_mac);
}

static ssize_t bonding_store_fail_over_mac(struct device *d,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	int new_value;
	struct bonding *bond = to_bond(d);

	if (bond->slave_cnt != 0) {
		pr_err(DRV_NAME
		       ": %s: Can't alter fail_over_mac with slaves in bond.\n",
		       bond->dev->name);
		return -EPERM;
	}

	new_value = bond_parse_parm(buf, fail_over_mac_tbl);
	if (new_value < 0) {
		pr_err(DRV_NAME
		       ": %s: Ignoring invalid fail_over_mac value %s.\n",
		       bond->dev->name, buf);
		return -EINVAL;
	}

	bond->params.fail_over_mac = new_value;
	pr_info(DRV_NAME ": %s: Setting fail_over_mac to %s (%d).\n",
	       bond->dev->name, fail_over_mac_tbl[new_value].modename,
	       new_value);

	return count;
}

static DEVICE_ATTR(fail_over_mac, S_IRUGO | S_IWUSR,
		   bonding_show_fail_over_mac, bonding_store_fail_over_mac);

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

	return sprintf(buf, "%d\n", bond->params.arp_interval);
}

static ssize_t bonding_store_arp_interval(struct device *d,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);

	if (sscanf(buf, "%d", &new_value) != 1) {
		pr_err(DRV_NAME
		       ": %s: no arp_interval value specified.\n",
		       bond->dev->name);
		ret = -EINVAL;
		goto out;
	}
	if (new_value < 0) {
		pr_err(DRV_NAME
		       ": %s: Invalid arp_interval value %d not in range 1-%d; rejected.\n",
		       bond->dev->name, new_value, INT_MAX);
		ret = -EINVAL;
		goto out;
	}

	pr_info(DRV_NAME
	       ": %s: Setting ARP monitoring interval to %d.\n",
	       bond->dev->name, new_value);
	bond->params.arp_interval = new_value;
	if (bond->params.arp_interval)
		bond->dev->priv_flags |= IFF_MASTER_ARPMON;
	if (bond->params.miimon) {
		pr_info(DRV_NAME
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
		pr_info(DRV_NAME
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
static DEVICE_ATTR(arp_interval, S_IRUGO | S_IWUSR,
		   bonding_show_arp_interval, bonding_store_arp_interval);

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
			res += sprintf(buf + res, "%pI4 ",
				       &bond->params.arp_targets[i]);
	}
	if (res)
		buf[res-1] = '\n'; /* eat the leftover space */
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
			pr_err(DRV_NAME
			       ": %s: invalid ARP target %pI4 specified for addition\n",
			       bond->dev->name, &newtarget);
			ret = -EINVAL;
			goto out;
		}
		/* look for an empty slot to put the target in, and check for dupes */
		for (i = 0; (i < BOND_MAX_ARP_TARGETS) && !done; i++) {
			if (targets[i] == newtarget) { /* duplicate */
				pr_err(DRV_NAME
				       ": %s: ARP target %pI4 is already present\n",
				       bond->dev->name, &newtarget);
				ret = -EINVAL;
				goto out;
			}
			if (targets[i] == 0) {
				pr_info(DRV_NAME
				       ": %s: adding ARP target %pI4.\n",
				       bond->dev->name, &newtarget);
				done = 1;
				targets[i] = newtarget;
			}
		}
		if (!done) {
			pr_err(DRV_NAME
			       ": %s: ARP target table is full!\n",
			       bond->dev->name);
			ret = -EINVAL;
			goto out;
		}

	} else if (buf[0] == '-')	{
		if ((newtarget == 0) || (newtarget == htonl(INADDR_BROADCAST))) {
			pr_err(DRV_NAME
			       ": %s: invalid ARP target %pI4 specified for removal\n",
			       bond->dev->name, &newtarget);
			ret = -EINVAL;
			goto out;
		}

		for (i = 0; (i < BOND_MAX_ARP_TARGETS) && !done; i++) {
			if (targets[i] == newtarget) {
				int j;
				pr_info(DRV_NAME
				       ": %s: removing ARP target %pI4.\n",
				       bond->dev->name, &newtarget);
				for (j = i; (j < (BOND_MAX_ARP_TARGETS-1)) && targets[j+1]; j++)
					targets[j] = targets[j+1];

				targets[j] = 0;
				done = 1;
			}
		}
		if (!done) {
			pr_info(DRV_NAME
			       ": %s: unable to remove nonexistent ARP target %pI4.\n",
			       bond->dev->name, &newtarget);
			ret = -EINVAL;
			goto out;
		}
	} else {
		pr_err(DRV_NAME ": no command found in arp_ip_targets file"
		       " for bond %s. Use +<addr> or -<addr>.\n",
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

	return sprintf(buf, "%d\n", bond->params.downdelay * bond->params.miimon);
}

static ssize_t bonding_store_downdelay(struct device *d,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);

	if (!(bond->params.miimon)) {
		pr_err(DRV_NAME
		       ": %s: Unable to set down delay as MII monitoring is disabled\n",
		       bond->dev->name);
		ret = -EPERM;
		goto out;
	}

	if (sscanf(buf, "%d", &new_value) != 1) {
		pr_err(DRV_NAME
		       ": %s: no down delay value specified.\n",
		       bond->dev->name);
		ret = -EINVAL;
		goto out;
	}
	if (new_value < 0) {
		pr_err(DRV_NAME
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
		pr_info(DRV_NAME ": %s: Setting down delay to %d.\n",
		       bond->dev->name,
		       bond->params.downdelay * bond->params.miimon);

	}

out:
	return ret;
}
static DEVICE_ATTR(downdelay, S_IRUGO | S_IWUSR,
		   bonding_show_downdelay, bonding_store_downdelay);

static ssize_t bonding_show_updelay(struct device *d,
				    struct device_attribute *attr,
				    char *buf)
{
	struct bonding *bond = to_bond(d);

	return sprintf(buf, "%d\n", bond->params.updelay * bond->params.miimon);

}

static ssize_t bonding_store_updelay(struct device *d,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);

	if (!(bond->params.miimon)) {
		pr_err(DRV_NAME
		       ": %s: Unable to set up delay as MII monitoring is disabled\n",
		       bond->dev->name);
		ret = -EPERM;
		goto out;
	}

	if (sscanf(buf, "%d", &new_value) != 1) {
		pr_err(DRV_NAME
		       ": %s: no up delay value specified.\n",
		       bond->dev->name);
		ret = -EINVAL;
		goto out;
	}
	if (new_value < 0) {
		pr_err(DRV_NAME
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
		pr_info(DRV_NAME ": %s: Setting up delay to %d.\n",
		       bond->dev->name, bond->params.updelay * bond->params.miimon);

	}

out:
	return ret;
}
static DEVICE_ATTR(updelay, S_IRUGO | S_IWUSR,
		   bonding_show_updelay, bonding_store_updelay);

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
		bond->params.lacp_fast);
}

static ssize_t bonding_store_lacp(struct device *d,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);

	if (bond->dev->flags & IFF_UP) {
		pr_err(DRV_NAME
		       ": %s: Unable to update LACP rate because interface is up.\n",
		       bond->dev->name);
		ret = -EPERM;
		goto out;
	}

	if (bond->params.mode != BOND_MODE_8023AD) {
		pr_err(DRV_NAME
		       ": %s: Unable to update LACP rate because bond is not in 802.3ad mode.\n",
		       bond->dev->name);
		ret = -EPERM;
		goto out;
	}

	new_value = bond_parse_parm(buf, bond_lacp_tbl);

	if ((new_value == 1) || (new_value == 0)) {
		bond->params.lacp_fast = new_value;
		pr_info(DRV_NAME ": %s: Setting LACP rate to %s (%d).\n",
			bond->dev->name, bond_lacp_tbl[new_value].modename,
			new_value);
	} else {
		pr_err(DRV_NAME
		       ": %s: Ignoring invalid LACP rate value %.*s.\n",
		       bond->dev->name, (int)strlen(buf) - 1, buf);
		ret = -EINVAL;
	}
out:
	return ret;
}
static DEVICE_ATTR(lacp_rate, S_IRUGO | S_IWUSR,
		   bonding_show_lacp, bonding_store_lacp);

static ssize_t bonding_show_ad_select(struct device *d,
				      struct device_attribute *attr,
				      char *buf)
{
	struct bonding *bond = to_bond(d);

	return sprintf(buf, "%s %d\n",
		ad_select_tbl[bond->params.ad_select].modename,
		bond->params.ad_select);
}


static ssize_t bonding_store_ad_select(struct device *d,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);

	if (bond->dev->flags & IFF_UP) {
		pr_err(DRV_NAME
		       ": %s: Unable to update ad_select because interface "
		       "is up.\n", bond->dev->name);
		ret = -EPERM;
		goto out;
	}

	new_value = bond_parse_parm(buf, ad_select_tbl);

	if (new_value != -1) {
		bond->params.ad_select = new_value;
		pr_info(DRV_NAME
		       ": %s: Setting ad_select to %s (%d).\n",
		       bond->dev->name, ad_select_tbl[new_value].modename,
		       new_value);
	} else {
		pr_err(DRV_NAME
		       ": %s: Ignoring invalid ad_select value %.*s.\n",
		       bond->dev->name, (int)strlen(buf) - 1, buf);
		ret = -EINVAL;
	}
out:
	return ret;
}
static DEVICE_ATTR(ad_select, S_IRUGO | S_IWUSR,
		   bonding_show_ad_select, bonding_store_ad_select);

/*
 * Show and set the number of grat ARP to send after a failover event.
 */
static ssize_t bonding_show_n_grat_arp(struct device *d,
				   struct device_attribute *attr,
				   char *buf)
{
	struct bonding *bond = to_bond(d);

	return sprintf(buf, "%d\n", bond->params.num_grat_arp);
}

static ssize_t bonding_store_n_grat_arp(struct device *d,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);

	if (sscanf(buf, "%d", &new_value) != 1) {
		pr_err(DRV_NAME
		       ": %s: no num_grat_arp value specified.\n",
		       bond->dev->name);
		ret = -EINVAL;
		goto out;
	}
	if (new_value < 0 || new_value > 255) {
		pr_err(DRV_NAME
		       ": %s: Invalid num_grat_arp value %d not in range 0-255; rejected.\n",
		       bond->dev->name, new_value);
		ret = -EINVAL;
		goto out;
	} else {
		bond->params.num_grat_arp = new_value;
	}
out:
	return ret;
}
static DEVICE_ATTR(num_grat_arp, S_IRUGO | S_IWUSR,
		   bonding_show_n_grat_arp, bonding_store_n_grat_arp);

/*
 * Show and set the number of unsolicited NA's to send after a failover event.
 */
static ssize_t bonding_show_n_unsol_na(struct device *d,
				       struct device_attribute *attr,
				       char *buf)
{
	struct bonding *bond = to_bond(d);

	return sprintf(buf, "%d\n", bond->params.num_unsol_na);
}

static ssize_t bonding_store_n_unsol_na(struct device *d,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);

	if (sscanf(buf, "%d", &new_value) != 1) {
		pr_err(DRV_NAME
		       ": %s: no num_unsol_na value specified.\n",
		       bond->dev->name);
		ret = -EINVAL;
		goto out;
	}

	if (new_value < 0 || new_value > 255) {
		pr_err(DRV_NAME
		       ": %s: Invalid num_unsol_na value %d not in range 0-255; rejected.\n",
		       bond->dev->name, new_value);
		ret = -EINVAL;
		goto out;
	} else
		bond->params.num_unsol_na = new_value;
out:
	return ret;
}
static DEVICE_ATTR(num_unsol_na, S_IRUGO | S_IWUSR,
		   bonding_show_n_unsol_na, bonding_store_n_unsol_na);

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

	return sprintf(buf, "%d\n", bond->params.miimon);
}

static ssize_t bonding_store_miimon(struct device *d,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);

	if (sscanf(buf, "%d", &new_value) != 1) {
		pr_err(DRV_NAME
		       ": %s: no miimon value specified.\n",
		       bond->dev->name);
		ret = -EINVAL;
		goto out;
	}
	if (new_value < 0) {
		pr_err(DRV_NAME
		       ": %s: Invalid miimon value %d not in range %d-%d; rejected.\n",
		       bond->dev->name, new_value, 1, INT_MAX);
		ret = -EINVAL;
		goto out;
	} else {
		pr_info(DRV_NAME
		       ": %s: Setting MII monitoring interval to %d.\n",
		       bond->dev->name, new_value);
		bond->params.miimon = new_value;
		if (bond->params.updelay)
			pr_info(DRV_NAME
			      ": %s: Note: Updating updelay (to %d) "
			      "since it is a multiple of the miimon value.\n",
			      bond->dev->name,
			      bond->params.updelay * bond->params.miimon);
		if (bond->params.downdelay)
			pr_info(DRV_NAME
			      ": %s: Note: Updating downdelay (to %d) "
			      "since it is a multiple of the miimon value.\n",
			      bond->dev->name,
			      bond->params.downdelay * bond->params.miimon);
		if (bond->params.arp_interval) {
			pr_info(DRV_NAME
			       ": %s: MII monitoring cannot be used with "
			       "ARP monitoring. Disabling ARP monitoring...\n",
			       bond->dev->name);
			bond->params.arp_interval = 0;
			bond->dev->priv_flags &= ~IFF_MASTER_ARPMON;
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
static DEVICE_ATTR(miimon, S_IRUGO | S_IWUSR,
		   bonding_show_miimon, bonding_store_miimon);

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
		count = sprintf(buf, "%s\n", bond->primary_slave->dev->name);

	return count;
}

static ssize_t bonding_store_primary(struct device *d,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int i;
	struct slave *slave;
	struct bonding *bond = to_bond(d);

	if (!rtnl_trylock())
		return restart_syscall();
	read_lock(&bond->lock);
	write_lock_bh(&bond->curr_slave_lock);

	if (!USES_PRIMARY(bond->params.mode)) {
		pr_info(DRV_NAME
		       ": %s: Unable to set primary slave; %s is in mode %d\n",
		       bond->dev->name, bond->dev->name, bond->params.mode);
	} else {
		bond_for_each_slave(bond, slave, i) {
			if (strnicmp
			    (slave->dev->name, buf,
			     strlen(slave->dev->name)) == 0) {
				pr_info(DRV_NAME
				       ": %s: Setting %s as primary slave.\n",
				       bond->dev->name, slave->dev->name);
				bond->primary_slave = slave;
				bond_select_active_slave(bond);
				goto out;
			}
		}

		/* if we got here, then we didn't match the name of any slave */

		if (strlen(buf) == 0 || buf[0] == '\n') {
			pr_info(DRV_NAME
			       ": %s: Setting primary slave to None.\n",
			       bond->dev->name);
			bond->primary_slave = NULL;
				bond_select_active_slave(bond);
		} else {
			pr_info(DRV_NAME
			       ": %s: Unable to set %.*s as primary slave as it is not a slave.\n",
			       bond->dev->name, (int)strlen(buf) - 1, buf);
		}
	}
out:
	write_unlock_bh(&bond->curr_slave_lock);
	read_unlock(&bond->lock);
	rtnl_unlock();

	return count;
}
static DEVICE_ATTR(primary, S_IRUGO | S_IWUSR,
		   bonding_show_primary, bonding_store_primary);

/*
 * Show and set the use_carrier flag.
 */
static ssize_t bonding_show_carrier(struct device *d,
				    struct device_attribute *attr,
				    char *buf)
{
	struct bonding *bond = to_bond(d);

	return sprintf(buf, "%d\n", bond->params.use_carrier);
}

static ssize_t bonding_store_carrier(struct device *d,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int new_value, ret = count;
	struct bonding *bond = to_bond(d);


	if (sscanf(buf, "%d", &new_value) != 1) {
		pr_err(DRV_NAME
		       ": %s: no use_carrier value specified.\n",
		       bond->dev->name);
		ret = -EINVAL;
		goto out;
	}
	if ((new_value == 0) || (new_value == 1)) {
		bond->params.use_carrier = new_value;
		pr_info(DRV_NAME ": %s: Setting use_carrier to %d.\n",
		       bond->dev->name, new_value);
	} else {
		pr_info(DRV_NAME
		       ": %s: Ignoring invalid use_carrier value %d.\n",
		       bond->dev->name, new_value);
	}
out:
	return count;
}
static DEVICE_ATTR(use_carrier, S_IRUGO | S_IWUSR,
		   bonding_show_carrier, bonding_store_carrier);


/*
 * Show and set currently active_slave.
 */
static ssize_t bonding_show_active_slave(struct device *d,
					 struct device_attribute *attr,
					 char *buf)
{
	struct slave *curr;
	struct bonding *bond = to_bond(d);
	int count = 0;

	read_lock(&bond->curr_slave_lock);
	curr = bond->curr_active_slave;
	read_unlock(&bond->curr_slave_lock);

	if (USES_PRIMARY(bond->params.mode) && curr)
		count = sprintf(buf, "%s\n", curr->dev->name);
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

	if (!rtnl_trylock())
		return restart_syscall();
	read_lock(&bond->lock);
	write_lock_bh(&bond->curr_slave_lock);

	if (!USES_PRIMARY(bond->params.mode))
		pr_info(DRV_NAME ": %s: Unable to change active slave;"
			" %s is in mode %d\n",
			bond->dev->name, bond->dev->name, bond->params.mode);
	else {
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
			pr_info(DRV_NAME
				": %s: Setting active slave to None.\n",
				bond->dev->name);
			bond->primary_slave = NULL;
			bond_select_active_slave(bond);
		} else {
			pr_info(DRV_NAME ": %s: Unable to set %.*s"
				" as active slave as it is not a slave.\n",
				bond->dev->name, (int)strlen(buf) - 1, buf);
		}
	}
 out:
	write_unlock_bh(&bond->curr_slave_lock);
	read_unlock(&bond->lock);
	rtnl_unlock();

	return count;

}
static DEVICE_ATTR(active_slave, S_IRUGO | S_IWUSR,
		   bonding_show_active_slave, bonding_store_active_slave);


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

	return sprintf(buf, "%s\n", curr ? "up" : "down");
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
		count = sprintf(buf, "%d\n",
				(bond_3ad_get_active_agg_info(bond, &ad_info))
				?  0 : ad_info.aggregator_id);
	}

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
		count = sprintf(buf, "%d\n",
				(bond_3ad_get_active_agg_info(bond, &ad_info))
				?  0 : ad_info.ports);
	}

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
		count = sprintf(buf, "%d\n",
				(bond_3ad_get_active_agg_info(bond, &ad_info))
				?  0 : ad_info.actor_key);
	}

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
		count = sprintf(buf, "%d\n",
				(bond_3ad_get_active_agg_info(bond, &ad_info))
				?  0 : ad_info.partner_key);
	}

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

	if (bond->params.mode == BOND_MODE_8023AD) {
		struct ad_info ad_info;
		if (!bond_3ad_get_active_agg_info(bond, &ad_info))
			count = sprintf(buf, "%pM\n", ad_info.partner_system);
	}

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
	&dev_attr_ad_select.attr,
	&dev_attr_xmit_hash_policy.attr,
	&dev_attr_num_grat_arp.attr,
	&dev_attr_num_unsol_na.attr,
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
	int ret;

	ret = netdev_class_create_file(&class_attr_bonding_masters);
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
		/* Is someone being kinky and naming a device bonding_master? */
		if (__dev_get_by_name(&init_net,
				      class_attr_bonding_masters.attr.name))
			printk(KERN_ERR
			       "network device named %s already exists in sysfs",
			       class_attr_bonding_masters.attr.name);
		ret = 0;
	}

	return ret;

}

/*
 * Remove /sys/class/net/bonding_masters.
 */
void bond_destroy_sysfs(void)
{
	netdev_class_remove_file(&class_attr_bonding_masters);
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
	if (err)
		printk(KERN_EMERG "eek! didn't create group!\n");

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

