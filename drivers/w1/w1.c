/*
 *	w1.c
 *
 * Copyright (c) 2004 Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kthread.h>

#include <asm/atomic.h>

#include "w1.h"
#include "w1_log.h"
#include "w1_int.h"
#include "w1_family.h"
#include "w1_netlink.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeniy Polyakov <johnpol@2ka.mipt.ru>");
MODULE_DESCRIPTION("Driver for 1-wire Dallas network protocol.");

static int w1_timeout = 10;
static int w1_control_timeout = 1;
int w1_max_slave_count = 10;
int w1_max_slave_ttl = 10;

module_param_named(timeout, w1_timeout, int, 0);
module_param_named(control_timeout, w1_control_timeout, int, 0);
module_param_named(max_slave_count, w1_max_slave_count, int, 0);
module_param_named(slave_ttl, w1_max_slave_ttl, int, 0);

DEFINE_MUTEX(w1_mlock);
LIST_HEAD(w1_masters);

static struct task_struct *w1_control_thread;

static int w1_master_match(struct device *dev, struct device_driver *drv)
{
	return 1;
}

static int w1_master_probe(struct device *dev)
{
	return -ENODEV;
}

static void w1_master_release(struct device *dev)
{
	struct w1_master *md = dev_to_w1_master(dev);

	dev_dbg(dev, "%s: Releasing %s.\n", __func__, md->name);
	memset(md, 0, sizeof(struct w1_master) + sizeof(struct w1_bus_master));
	kfree(md);
}

static void w1_slave_release(struct device *dev)
{
	struct w1_slave *sl = dev_to_w1_slave(dev);

	printk("%s: Releasing %s.\n", __func__, sl->name);

	while (atomic_read(&sl->refcnt)) {
		printk("Waiting for %s to become free: refcnt=%d.\n",
				sl->name, atomic_read(&sl->refcnt));
		if (msleep_interruptible(1000))
			flush_signals(current);
	}

	w1_family_put(sl->family);
	sl->master->slave_count--;

	complete(&sl->released);
}

static ssize_t w1_slave_read_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w1_slave *sl = dev_to_w1_slave(dev);

	return sprintf(buf, "%s\n", sl->name);
}

static ssize_t w1_slave_read_id(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);

	if (off > 8) {
		count = 0;
	} else {
		if (off + count > 8)
			count = 8 - off;

		memcpy(buf, (u8 *)&sl->reg_num, count);
	}

	return count;
}

static struct device_attribute w1_slave_attr_name =
	__ATTR(name, S_IRUGO, w1_slave_read_name, NULL);

static struct bin_attribute w1_slave_attr_bin_id = {
      .attr = {
              .name = "id",
              .mode = S_IRUGO,
              .owner = THIS_MODULE,
      },
      .size = 8,
      .read = w1_slave_read_id,
};

/* Default family */

static ssize_t w1_default_write(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);

	mutex_lock(&sl->master->mutex);
	if (w1_reset_select_slave(sl)) {
		count = 0;
		goto out_up;
	}

	w1_write_block(sl->master, buf, count);

out_up:
	mutex_unlock(&sl->master->mutex);
	return count;
}

static ssize_t w1_default_read(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);

	mutex_lock(&sl->master->mutex);
	w1_read_block(sl->master, buf, count);
	mutex_unlock(&sl->master->mutex);
	return count;
}

static struct bin_attribute w1_default_attr = {
      .attr = {
              .name = "rw",
              .mode = S_IRUGO | S_IWUSR,
              .owner = THIS_MODULE,
      },
      .size = PAGE_SIZE,
      .read = w1_default_read,
      .write = w1_default_write,
};

static int w1_default_add_slave(struct w1_slave *sl)
{
	return sysfs_create_bin_file(&sl->dev.kobj, &w1_default_attr);
}

static void w1_default_remove_slave(struct w1_slave *sl)
{
	sysfs_remove_bin_file(&sl->dev.kobj, &w1_default_attr);
}

static struct w1_family_ops w1_default_fops = {
	.add_slave	= w1_default_add_slave,
	.remove_slave	= w1_default_remove_slave,
};

static struct w1_family w1_default_family = {
	.fops = &w1_default_fops,
};

static int w1_uevent(struct device *dev, char **envp, int num_envp, char *buffer, int buffer_size);

static struct bus_type w1_bus_type = {
	.name = "w1",
	.match = w1_master_match,
	.uevent = w1_uevent,
};

struct device_driver w1_master_driver = {
	.name = "w1_master_driver",
	.bus = &w1_bus_type,
	.probe = w1_master_probe,
};

struct device w1_master_device = {
	.parent = NULL,
	.bus = &w1_bus_type,
	.bus_id = "w1 bus master",
	.driver = &w1_master_driver,
	.release = &w1_master_release
};

static struct device_driver w1_slave_driver = {
	.name = "w1_slave_driver",
	.bus = &w1_bus_type,
};

#if 0
struct device w1_slave_device = {
	.parent = NULL,
	.bus = &w1_bus_type,
	.bus_id = "w1 bus slave",
	.driver = &w1_slave_driver,
	.release = &w1_slave_release
};
#endif  /*  0  */

static ssize_t w1_master_attribute_show_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w1_master *md = dev_to_w1_master(dev);
	ssize_t count;

	mutex_lock(&md->mutex);
	count = sprintf(buf, "%s\n", md->name);
	mutex_unlock(&md->mutex);

	return count;
}

static ssize_t w1_master_attribute_store_search(struct device * dev,
						struct device_attribute *attr,
						const char * buf, size_t count)
{
	struct w1_master *md = dev_to_w1_master(dev);

	mutex_lock(&md->mutex);
	md->search_count = simple_strtol(buf, NULL, 0);
	mutex_unlock(&md->mutex);

	return count;
}

static ssize_t w1_master_attribute_show_search(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct w1_master *md = dev_to_w1_master(dev);
	ssize_t count;

	mutex_lock(&md->mutex);
	count = sprintf(buf, "%d\n", md->search_count);
	mutex_unlock(&md->mutex);

	return count;
}

static ssize_t w1_master_attribute_show_pointer(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w1_master *md = dev_to_w1_master(dev);
	ssize_t count;

	mutex_lock(&md->mutex);
	count = sprintf(buf, "0x%p\n", md->bus_master);
	mutex_unlock(&md->mutex);
	return count;
}

static ssize_t w1_master_attribute_show_timeout(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t count;
	count = sprintf(buf, "%d\n", w1_timeout);
	return count;
}

static ssize_t w1_master_attribute_show_max_slave_count(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w1_master *md = dev_to_w1_master(dev);
	ssize_t count;

	mutex_lock(&md->mutex);
	count = sprintf(buf, "%d\n", md->max_slave_count);
	mutex_unlock(&md->mutex);
	return count;
}

static ssize_t w1_master_attribute_show_attempts(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w1_master *md = dev_to_w1_master(dev);
	ssize_t count;

	mutex_lock(&md->mutex);
	count = sprintf(buf, "%lu\n", md->attempts);
	mutex_unlock(&md->mutex);
	return count;
}

static ssize_t w1_master_attribute_show_slave_count(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w1_master *md = dev_to_w1_master(dev);
	ssize_t count;

	mutex_lock(&md->mutex);
	count = sprintf(buf, "%d\n", md->slave_count);
	mutex_unlock(&md->mutex);
	return count;
}

static ssize_t w1_master_attribute_show_slaves(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w1_master *md = dev_to_w1_master(dev);
	int c = PAGE_SIZE;

	mutex_lock(&md->mutex);

	if (md->slave_count == 0)
		c -= snprintf(buf + PAGE_SIZE - c, c, "not found.\n");
	else {
		struct list_head *ent, *n;
		struct w1_slave *sl;

		list_for_each_safe(ent, n, &md->slist) {
			sl = list_entry(ent, struct w1_slave, w1_slave_entry);

			c -= snprintf(buf + PAGE_SIZE - c, c, "%s\n", sl->name);
		}
	}

	mutex_unlock(&md->mutex);

	return PAGE_SIZE - c;
}

#define W1_MASTER_ATTR_RO(_name, _mode)				\
	struct device_attribute w1_master_attribute_##_name =	\
		__ATTR(w1_master_##_name, _mode,		\
		       w1_master_attribute_show_##_name, NULL)

#define W1_MASTER_ATTR_RW(_name, _mode)				\
	struct device_attribute w1_master_attribute_##_name =	\
		__ATTR(w1_master_##_name, _mode,		\
		       w1_master_attribute_show_##_name,	\
		       w1_master_attribute_store_##_name)

static W1_MASTER_ATTR_RO(name, S_IRUGO);
static W1_MASTER_ATTR_RO(slaves, S_IRUGO);
static W1_MASTER_ATTR_RO(slave_count, S_IRUGO);
static W1_MASTER_ATTR_RO(max_slave_count, S_IRUGO);
static W1_MASTER_ATTR_RO(attempts, S_IRUGO);
static W1_MASTER_ATTR_RO(timeout, S_IRUGO);
static W1_MASTER_ATTR_RO(pointer, S_IRUGO);
static W1_MASTER_ATTR_RW(search, S_IRUGO | S_IWUGO);

static struct attribute *w1_master_default_attrs[] = {
	&w1_master_attribute_name.attr,
	&w1_master_attribute_slaves.attr,
	&w1_master_attribute_slave_count.attr,
	&w1_master_attribute_max_slave_count.attr,
	&w1_master_attribute_attempts.attr,
	&w1_master_attribute_timeout.attr,
	&w1_master_attribute_pointer.attr,
	&w1_master_attribute_search.attr,
	NULL
};

static struct attribute_group w1_master_defattr_group = {
	.attrs = w1_master_default_attrs,
};

int w1_create_master_attributes(struct w1_master *master)
{
	return sysfs_create_group(&master->dev.kobj, &w1_master_defattr_group);
}

static void w1_destroy_master_attributes(struct w1_master *master)
{
	sysfs_remove_group(&master->dev.kobj, &w1_master_defattr_group);
}

#ifdef CONFIG_HOTPLUG
static int w1_uevent(struct device *dev, char **envp, int num_envp,
			char *buffer, int buffer_size)
{
	struct w1_master *md = NULL;
	struct w1_slave *sl = NULL;
	char *event_owner, *name;
	int err, cur_index=0, cur_len=0;

	if (dev->driver == &w1_master_driver) {
		md = container_of(dev, struct w1_master, dev);
		event_owner = "master";
		name = md->name;
	} else if (dev->driver == &w1_slave_driver) {
		sl = container_of(dev, struct w1_slave, dev);
		event_owner = "slave";
		name = sl->name;
	} else {
		dev_dbg(dev, "Unknown event.\n");
		return -EINVAL;
	}

	dev_dbg(dev, "Hotplug event for %s %s, bus_id=%s.\n",
			event_owner, name, dev->bus_id);

	if (dev->driver != &w1_slave_driver || !sl)
		return 0;

	err = add_uevent_var(envp, num_envp, &cur_index, buffer, buffer_size,
			&cur_len, "W1_FID=%02X", sl->reg_num.family);
	if (err)
		return err;

	err = add_uevent_var(envp, num_envp, &cur_index, buffer, buffer_size,
			&cur_len, "W1_SLAVE_ID=%024LX",
			(unsigned long long)sl->reg_num.id);
	if (err)
		return err;

	return 0;
};
#else
static int w1_uevent(struct device *dev, char **envp, int num_envp,
			char *buffer, int buffer_size)
{
	return 0;
}
#endif

static int __w1_attach_slave_device(struct w1_slave *sl)
{
	int err;

	sl->dev.parent = &sl->master->dev;
	sl->dev.driver = &w1_slave_driver;
	sl->dev.bus = &w1_bus_type;
	sl->dev.release = &w1_slave_release;

	snprintf(&sl->dev.bus_id[0], sizeof(sl->dev.bus_id),
		 "%02x-%012llx",
		 (unsigned int) sl->reg_num.family,
		 (unsigned long long) sl->reg_num.id);
	snprintf(&sl->name[0], sizeof(sl->name),
		 "%02x-%012llx",
		 (unsigned int) sl->reg_num.family,
		 (unsigned long long) sl->reg_num.id);

	dev_dbg(&sl->dev, "%s: registering %s as %p.\n", __func__,
		&sl->dev.bus_id[0]);

	err = device_register(&sl->dev);
	if (err < 0) {
		dev_err(&sl->dev,
			"Device registration [%s] failed. err=%d\n",
			sl->dev.bus_id, err);
		return err;
	}

	/* Create "name" entry */
	err = device_create_file(&sl->dev, &w1_slave_attr_name);
	if (err < 0) {
		dev_err(&sl->dev,
			"sysfs file creation for [%s] failed. err=%d\n",
			sl->dev.bus_id, err);
		goto out_unreg;
	}

	/* Create "id" entry */
	err = sysfs_create_bin_file(&sl->dev.kobj, &w1_slave_attr_bin_id);
	if (err < 0) {
		dev_err(&sl->dev,
			"sysfs file creation for [%s] failed. err=%d\n",
			sl->dev.bus_id, err);
		goto out_rem1;
	}

	/* if the family driver needs to initialize something... */
	if (sl->family->fops && sl->family->fops->add_slave &&
	    ((err = sl->family->fops->add_slave(sl)) < 0)) {
		dev_err(&sl->dev,
			"sysfs file creation for [%s] failed. err=%d\n",
			sl->dev.bus_id, err);
		goto out_rem2;
	}

	list_add_tail(&sl->w1_slave_entry, &sl->master->slist);

	return 0;

out_rem2:
	sysfs_remove_bin_file(&sl->dev.kobj, &w1_slave_attr_bin_id);
out_rem1:
	device_remove_file(&sl->dev, &w1_slave_attr_name);
out_unreg:
	device_unregister(&sl->dev);
	return err;
}

static int w1_attach_slave_device(struct w1_master *dev, struct w1_reg_num *rn)
{
	struct w1_slave *sl;
	struct w1_family *f;
	int err;
	struct w1_netlink_msg msg;

	sl = kmalloc(sizeof(struct w1_slave), GFP_KERNEL);
	if (!sl) {
		dev_err(&dev->dev,
			 "%s: failed to allocate new slave device.\n",
			 __func__);
		return -ENOMEM;
	}

	memset(sl, 0, sizeof(*sl));

	sl->owner = THIS_MODULE;
	sl->master = dev;
	set_bit(W1_SLAVE_ACTIVE, (long *)&sl->flags);

	memset(&msg, 0, sizeof(msg));
	memcpy(&sl->reg_num, rn, sizeof(sl->reg_num));
	atomic_set(&sl->refcnt, 0);
	init_completion(&sl->released);

	spin_lock(&w1_flock);
	f = w1_family_registered(rn->family);
	if (!f) {
		f= &w1_default_family;
		dev_info(&dev->dev, "Family %x for %02x.%012llx.%02x is not registered.\n",
			  rn->family, rn->family,
			  (unsigned long long)rn->id, rn->crc);
	}
	__w1_family_get(f);
	spin_unlock(&w1_flock);

	sl->family = f;


	err = __w1_attach_slave_device(sl);
	if (err < 0) {
		dev_err(&dev->dev, "%s: Attaching %s failed.\n", __func__,
			 sl->name);
		w1_family_put(sl->family);
		kfree(sl);
		return err;
	}

	sl->ttl = dev->slave_ttl;
	dev->slave_count++;

	memcpy(msg.id.id, rn, sizeof(msg.id));
	msg.type = W1_SLAVE_ADD;
	w1_netlink_send(dev, &msg);

	return 0;
}

static void w1_slave_detach(struct w1_slave *sl)
{
	struct w1_netlink_msg msg;

	dev_dbg(&sl->dev, "%s: detaching %s [%p].\n", __func__, sl->name, sl);

	list_del(&sl->w1_slave_entry);

	if (sl->family->fops && sl->family->fops->remove_slave)
		sl->family->fops->remove_slave(sl);

	memset(&msg, 0, sizeof(msg));
	memcpy(msg.id.id, &sl->reg_num, sizeof(msg.id));
	msg.type = W1_SLAVE_REMOVE;
	w1_netlink_send(sl->master, &msg);

	sysfs_remove_bin_file(&sl->dev.kobj, &w1_slave_attr_bin_id);
	device_remove_file(&sl->dev, &w1_slave_attr_name);
	device_unregister(&sl->dev);

	wait_for_completion(&sl->released);
	kfree(sl);
}

static struct w1_master *w1_search_master(void *data)
{
	struct w1_master *dev;
	int found = 0;

	mutex_lock(&w1_mlock);
	list_for_each_entry(dev, &w1_masters, w1_master_entry) {
		if (dev->bus_master->data == data) {
			found = 1;
			atomic_inc(&dev->refcnt);
			break;
		}
	}
	mutex_unlock(&w1_mlock);

	return (found)?dev:NULL;
}

struct w1_master *w1_search_master_id(u32 id)
{
	struct w1_master *dev;
	int found = 0;

	mutex_lock(&w1_mlock);
	list_for_each_entry(dev, &w1_masters, w1_master_entry) {
		if (dev->id == id) {
			found = 1;
			atomic_inc(&dev->refcnt);
			break;
		}
	}
	mutex_unlock(&w1_mlock);

	return (found)?dev:NULL;
}

struct w1_slave *w1_search_slave(struct w1_reg_num *id)
{
	struct w1_master *dev;
	struct w1_slave *sl = NULL;
	int found = 0;

	mutex_lock(&w1_mlock);
	list_for_each_entry(dev, &w1_masters, w1_master_entry) {
		mutex_lock(&dev->mutex);
		list_for_each_entry(sl, &dev->slist, w1_slave_entry) {
			if (sl->reg_num.family == id->family &&
					sl->reg_num.id == id->id &&
					sl->reg_num.crc == id->crc) {
				found = 1;
				atomic_inc(&dev->refcnt);
				atomic_inc(&sl->refcnt);
				break;
			}
		}
		mutex_unlock(&dev->mutex);

		if (found)
			break;
	}
	mutex_unlock(&w1_mlock);

	return (found)?sl:NULL;
}

void w1_reconnect_slaves(struct w1_family *f)
{
	struct w1_master *dev;

	mutex_lock(&w1_mlock);
	list_for_each_entry(dev, &w1_masters, w1_master_entry) {
		dev_dbg(&dev->dev, "Reconnecting slaves in %s into new family %02x.\n",
				dev->name, f->fid);
		set_bit(W1_MASTER_NEED_RECONNECT, &dev->flags);
	}
	mutex_unlock(&w1_mlock);
}

static void w1_slave_found(void *data, u64 rn)
{
	int slave_count;
	struct w1_slave *sl;
	struct list_head *ent;
	struct w1_reg_num *tmp;
	int family_found = 0;
	struct w1_master *dev;
	u64 rn_le = cpu_to_le64(rn);

	dev = w1_search_master(data);
	if (!dev) {
		printk(KERN_ERR "Failed to find w1 master device for data %p, "
		       "it is impossible.\n", data);
		return;
	}

	tmp = (struct w1_reg_num *) &rn;

	slave_count = 0;
	list_for_each(ent, &dev->slist) {

		sl = list_entry(ent, struct w1_slave, w1_slave_entry);

		if (sl->reg_num.family == tmp->family &&
		    sl->reg_num.id == tmp->id &&
		    sl->reg_num.crc == tmp->crc) {
			set_bit(W1_SLAVE_ACTIVE, (long *)&sl->flags);
			break;
		} else if (sl->reg_num.family == tmp->family) {
			family_found = 1;
			break;
		}

		slave_count++;
	}

	if (slave_count == dev->slave_count &&
		rn && ((rn >> 56) & 0xff) == w1_calc_crc8((u8 *)&rn_le, 7)) {
		w1_attach_slave_device(dev, tmp);
	}

	atomic_dec(&dev->refcnt);
}

/**
 * Performs a ROM Search & registers any devices found.
 * The 1-wire search is a simple binary tree search.
 * For each bit of the address, we read two bits and write one bit.
 * The bit written will put to sleep all devies that don't match that bit.
 * When the two reads differ, the direction choice is obvious.
 * When both bits are 0, we must choose a path to take.
 * When we can scan all 64 bits without having to choose a path, we are done.
 *
 * See "Application note 187 1-wire search algorithm" at www.maxim-ic.com
 *
 * @dev        The master device to search
 * @cb         Function to call when a device is found
 */
void w1_search(struct w1_master *dev, u8 search_type, w1_slave_found_callback cb)
{
	u64 last_rn, rn, tmp64;
	int i, slave_count = 0;
	int last_zero, last_device;
	int search_bit, desc_bit;
	u8  triplet_ret = 0;

	search_bit = 0;
	rn = last_rn = 0;
	last_device = 0;
	last_zero = -1;

	desc_bit = 64;

	while ( !last_device && (slave_count++ < dev->max_slave_count) ) {
		last_rn = rn;
		rn = 0;

		/*
		 * Reset bus and all 1-wire device state machines
		 * so they can respond to our requests.
		 *
		 * Return 0 - device(s) present, 1 - no devices present.
		 */
		if (w1_reset_bus(dev)) {
			dev_dbg(&dev->dev, "No devices present on the wire.\n");
			break;
		}

		/* Start the search */
		w1_write_8(dev, search_type);
		for (i = 0; i < 64; ++i) {
			/* Determine the direction/search bit */
			if (i == desc_bit)
				search_bit = 1;	  /* took the 0 path last time, so take the 1 path */
			else if (i > desc_bit)
				search_bit = 0;	  /* take the 0 path on the next branch */
			else
				search_bit = ((last_rn >> i) & 0x1);

			/** Read two bits and write one bit */
			triplet_ret = w1_triplet(dev, search_bit);

			/* quit if no device responded */
			if ( (triplet_ret & 0x03) == 0x03 )
				break;

			/* If both directions were valid, and we took the 0 path... */
			if (triplet_ret == 0)
				last_zero = i;

			/* extract the direction taken & update the device number */
			tmp64 = (triplet_ret >> 2);
			rn |= (tmp64 << i);
		}

		if ( (triplet_ret & 0x03) != 0x03 ) {
			if ( (desc_bit == last_zero) || (last_zero < 0))
				last_device = 1;
			desc_bit = last_zero;
			cb(dev->bus_master->data, rn);
		}
	}
}

static int w1_control(void *data)
{
	struct w1_slave *sl, *sln;
	struct w1_master *dev, *n;
	int have_to_wait = 0;

	while (!kthread_should_stop() || have_to_wait) {
		have_to_wait = 0;

		try_to_freeze();
		msleep_interruptible(w1_control_timeout * 1000);

		list_for_each_entry_safe(dev, n, &w1_masters, w1_master_entry) {
			if (!kthread_should_stop() && !dev->flags)
				continue;
			/*
			 * Little race: we can create thread but not set the flag.
			 * Get a chance for external process to set flag up.
			 */
			if (!dev->initialized) {
				have_to_wait = 1;
				continue;
			}

			if (kthread_should_stop() || test_bit(W1_MASTER_NEED_EXIT, &dev->flags)) {
				set_bit(W1_MASTER_NEED_EXIT, &dev->flags);

				mutex_lock(&w1_mlock);
				list_del(&dev->w1_master_entry);
				mutex_unlock(&w1_mlock);

				mutex_lock(&dev->mutex);
				list_for_each_entry_safe(sl, sln, &dev->slist, w1_slave_entry) {
					w1_slave_detach(sl);
				}
				w1_destroy_master_attributes(dev);
				mutex_unlock(&dev->mutex);
				atomic_dec(&dev->refcnt);
				continue;
			}

			if (test_bit(W1_MASTER_NEED_RECONNECT, &dev->flags)) {
				dev_dbg(&dev->dev, "Reconnecting slaves in device %s.\n", dev->name);
				mutex_lock(&dev->mutex);
				list_for_each_entry_safe(sl, sln, &dev->slist, w1_slave_entry) {
					if (sl->family->fid == W1_FAMILY_DEFAULT) {
						struct w1_reg_num rn;

						memcpy(&rn, &sl->reg_num, sizeof(rn));
						w1_slave_detach(sl);

						w1_attach_slave_device(dev, &rn);
					}
				}
				dev_dbg(&dev->dev, "Reconnecting slaves in device %s has been finished.\n", dev->name);
				clear_bit(W1_MASTER_NEED_RECONNECT, &dev->flags);
				mutex_unlock(&dev->mutex);
			}
		}
	}

	return 0;
}

void w1_search_process(struct w1_master *dev, u8 search_type)
{
	struct w1_slave *sl, *sln;

	list_for_each_entry(sl, &dev->slist, w1_slave_entry)
		clear_bit(W1_SLAVE_ACTIVE, (long *)&sl->flags);

	w1_search_devices(dev, search_type, w1_slave_found);

	list_for_each_entry_safe(sl, sln, &dev->slist, w1_slave_entry) {
		if (!test_bit(W1_SLAVE_ACTIVE, (unsigned long *)&sl->flags) && !--sl->ttl) {
			w1_slave_detach(sl);

			dev->slave_count--;
		} else if (test_bit(W1_SLAVE_ACTIVE, (unsigned long *)&sl->flags))
			sl->ttl = dev->slave_ttl;
	}

	if (dev->search_count > 0)
		dev->search_count--;
}

int w1_process(void *data)
{
	struct w1_master *dev = (struct w1_master *) data;

	while (!kthread_should_stop() && !test_bit(W1_MASTER_NEED_EXIT, &dev->flags)) {
		try_to_freeze();
		msleep_interruptible(w1_timeout * 1000);

		if (kthread_should_stop() || test_bit(W1_MASTER_NEED_EXIT, &dev->flags))
			break;

		if (!dev->initialized)
			continue;

		if (dev->search_count == 0)
			continue;

		mutex_lock(&dev->mutex);
		w1_search_process(dev, W1_SEARCH);
		mutex_unlock(&dev->mutex);
	}

	atomic_dec(&dev->refcnt);

	return 0;
}

static int w1_init(void)
{
	int retval;

	printk(KERN_INFO "Driver for 1-wire Dallas network protocol.\n");

	w1_init_netlink();

	retval = bus_register(&w1_bus_type);
	if (retval) {
		printk(KERN_ERR "Failed to register bus. err=%d.\n", retval);
		goto err_out_exit_init;
	}

	retval = driver_register(&w1_master_driver);
	if (retval) {
		printk(KERN_ERR
			"Failed to register master driver. err=%d.\n",
			retval);
		goto err_out_bus_unregister;
	}

	retval = driver_register(&w1_slave_driver);
	if (retval) {
		printk(KERN_ERR
			"Failed to register master driver. err=%d.\n",
			retval);
		goto err_out_master_unregister;
	}

	w1_control_thread = kthread_run(w1_control, NULL, "w1_control");
	if (IS_ERR(w1_control_thread)) {
		retval = PTR_ERR(w1_control_thread);
		printk(KERN_ERR "Failed to create control thread. err=%d\n",
			retval);
		goto err_out_slave_unregister;
	}

	return 0;

err_out_slave_unregister:
	driver_unregister(&w1_slave_driver);

err_out_master_unregister:
	driver_unregister(&w1_master_driver);

err_out_bus_unregister:
	bus_unregister(&w1_bus_type);

err_out_exit_init:
	return retval;
}

static void w1_fini(void)
{
	struct w1_master *dev;

	list_for_each_entry(dev, &w1_masters, w1_master_entry)
		__w1_remove_master_device(dev);

	w1_fini_netlink();

	kthread_stop(w1_control_thread);

	driver_unregister(&w1_slave_driver);
	driver_unregister(&w1_master_driver);
	bus_unregister(&w1_bus_type);
}

module_init(w1_init);
module_exit(w1_fini);
