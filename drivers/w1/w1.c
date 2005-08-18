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

#include <asm/atomic.h>

#include "w1.h"
#include "w1_io.h"
#include "w1_log.h"
#include "w1_int.h"
#include "w1_family.h"
#include "w1_netlink.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeniy Polyakov <johnpol@2ka.mipt.ru>");
MODULE_DESCRIPTION("Driver for 1-wire Dallas network protocol.");

static int w1_timeout = 10;
int w1_max_slave_count = 10;
int w1_max_slave_ttl = 10;

module_param_named(timeout, w1_timeout, int, 0);
module_param_named(max_slave_count, w1_max_slave_count, int, 0);
module_param_named(slave_ttl, w1_max_slave_ttl, int, 0);

DEFINE_SPINLOCK(w1_mlock);
LIST_HEAD(w1_masters);

static pid_t control_thread;
static int control_needs_exit;
static DECLARE_COMPLETION(w1_control_complete);

/* stuff for the default family */
static ssize_t w1_famdefault_read_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);
	return(sprintf(buf, "%s\n", sl->name));
}
static struct w1_family_ops w1_default_fops = {
	.rname = &w1_famdefault_read_name,
};
static struct w1_family w1_default_family = {
	.fops = &w1_default_fops,
};

static int w1_master_match(struct device *dev, struct device_driver *drv)
{
	return 1;
}

static int w1_master_probe(struct device *dev)
{
	return -ENODEV;
}

static int w1_master_remove(struct device *dev)
{
	return 0;
}

static void w1_master_release(struct device *dev)
{
	struct w1_master *md = container_of(dev, struct w1_master, dev);

	complete(&md->dev_released);
}

static void w1_slave_release(struct device *dev)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);

	complete(&sl->dev_released);
}

static ssize_t w1_default_read_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "No family registered.\n");
}

static ssize_t w1_default_read_bin(struct kobject *kobj, char *buf, loff_t off,
		     size_t count)
{
	return sprintf(buf, "No family registered.\n");
}

static struct device_attribute w1_slave_attribute =
	__ATTR(name, S_IRUGO, w1_default_read_name, NULL);

static struct bin_attribute w1_slave_bin_attribute = {
	.attr = {
		.name = "w1_slave",
		.mode = S_IRUGO,
		.owner = THIS_MODULE,
	},
	.size = W1_SLAVE_DATA_SIZE,
	.read = &w1_default_read_bin,
};


static struct bus_type w1_bus_type = {
	.name = "w1",
	.match = w1_master_match,
};

struct device_driver w1_driver = {
	.name = "w1_driver",
	.bus = &w1_bus_type,
	.probe = w1_master_probe,
	.remove = w1_master_remove,
};

struct device w1_device = {
	.parent = NULL,
	.bus = &w1_bus_type,
	.bus_id = "w1 bus master",
	.driver = &w1_driver,
	.release = &w1_master_release
};

static ssize_t w1_master_attribute_show_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w1_master *md = container_of(dev, struct w1_master, dev);
	ssize_t count;

	if (down_interruptible (&md->mutex))
		return -EBUSY;

	count = sprintf(buf, "%s\n", md->name);

	up(&md->mutex);

	return count;
}

static ssize_t w1_master_attribute_store_search(struct device * dev,
						struct device_attribute *attr,
						const char * buf, size_t count)
{
	struct w1_master *md = container_of(dev, struct w1_master, dev);

	if (down_interruptible (&md->mutex))
		return -EBUSY;

	md->search_count = simple_strtol(buf, NULL, 0);

	up(&md->mutex);

	return count;
}

static ssize_t w1_master_attribute_show_search(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct w1_master *md = container_of(dev, struct w1_master, dev);
	ssize_t count;

	if (down_interruptible (&md->mutex))
		return -EBUSY;

	count = sprintf(buf, "%d\n", md->search_count);

	up(&md->mutex);

	return count;
}

static ssize_t w1_master_attribute_show_pointer(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w1_master *md = container_of(dev, struct w1_master, dev);
	ssize_t count;

	if (down_interruptible(&md->mutex))
		return -EBUSY;

	count = sprintf(buf, "0x%p\n", md->bus_master);

	up(&md->mutex);
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
	struct w1_master *md = container_of(dev, struct w1_master, dev);
	ssize_t count;

	if (down_interruptible(&md->mutex))
		return -EBUSY;

	count = sprintf(buf, "%d\n", md->max_slave_count);

	up(&md->mutex);
	return count;
}

static ssize_t w1_master_attribute_show_attempts(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w1_master *md = container_of(dev, struct w1_master, dev);
	ssize_t count;

	if (down_interruptible(&md->mutex))
		return -EBUSY;

	count = sprintf(buf, "%lu\n", md->attempts);

	up(&md->mutex);
	return count;
}

static ssize_t w1_master_attribute_show_slave_count(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w1_master *md = container_of(dev, struct w1_master, dev);
	ssize_t count;

	if (down_interruptible(&md->mutex))
		return -EBUSY;

	count = sprintf(buf, "%d\n", md->slave_count);

	up(&md->mutex);
	return count;
}

static ssize_t w1_master_attribute_show_slaves(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w1_master *md = container_of(dev, struct w1_master, dev);
	int c = PAGE_SIZE;

	if (down_interruptible(&md->mutex))
		return -EBUSY;

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

	up(&md->mutex);

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

void w1_destroy_master_attributes(struct w1_master *master)
{
	sysfs_remove_group(&master->dev.kobj, &w1_master_defattr_group);
}

static int __w1_attach_slave_device(struct w1_slave *sl)
{
	int err;

	sl->dev.parent = &sl->master->dev;
	sl->dev.driver = sl->master->driver;
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

	dev_dbg(&sl->dev, "%s: registering %s.\n", __func__,
		&sl->dev.bus_id[0]);

	err = device_register(&sl->dev);
	if (err < 0) {
		dev_err(&sl->dev,
			"Device registration [%s] failed. err=%d\n",
			sl->dev.bus_id, err);
		return err;
	}

	memcpy(&sl->attr_bin, &w1_slave_bin_attribute, sizeof(sl->attr_bin));
	memcpy(&sl->attr_name, &w1_slave_attribute, sizeof(sl->attr_name));

	sl->attr_bin.read = sl->family->fops->rbin;
	sl->attr_name.show = sl->family->fops->rname;

	err = device_create_file(&sl->dev, &sl->attr_name);
	if (err < 0) {
		dev_err(&sl->dev,
			"sysfs file creation for [%s] failed. err=%d\n",
			sl->dev.bus_id, err);
		device_unregister(&sl->dev);
		return err;
	}

	if ( sl->attr_bin.read ) {
		err = sysfs_create_bin_file(&sl->dev.kobj, &sl->attr_bin);
		if (err < 0) {
			dev_err(&sl->dev,
				"sysfs file creation for [%s] failed. err=%d\n",
				sl->dev.bus_id, err);
			device_remove_file(&sl->dev, &sl->attr_name);
			device_unregister(&sl->dev);
			return err;
		}
	}

	list_add_tail(&sl->w1_slave_entry, &sl->master->slist);

	return 0;
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

	memcpy(&sl->reg_num, rn, sizeof(sl->reg_num));
	atomic_set(&sl->refcnt, 0);
	init_completion(&sl->dev_released);

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

	memcpy(&msg.id.id, rn, sizeof(msg.id.id));
	msg.type = W1_SLAVE_ADD;
	w1_netlink_send(dev, &msg);

	return 0;
}

static void w1_slave_detach(struct w1_slave *sl)
{
	struct w1_netlink_msg msg;

	dev_info(&sl->dev, "%s: detaching %s.\n", __func__, sl->name);

	while (atomic_read(&sl->refcnt)) {
		printk(KERN_INFO "Waiting for %s to become free: refcnt=%d.\n",
				sl->name, atomic_read(&sl->refcnt));

		if (msleep_interruptible(1000))
			flush_signals(current);
	}

	if ( sl->attr_bin.read ) {
		sysfs_remove_bin_file (&sl->dev.kobj, &sl->attr_bin);
	}
	device_remove_file(&sl->dev, &sl->attr_name);
	device_unregister(&sl->dev);
	w1_family_put(sl->family);

	sl->master->slave_count--;

	memcpy(&msg.id.id, &sl->reg_num, sizeof(msg.id.id));
	msg.type = W1_SLAVE_REMOVE;
	w1_netlink_send(sl->master, &msg);
}

static struct w1_master *w1_search_master(unsigned long data)
{
	struct w1_master *dev;
	int found = 0;

	spin_lock_bh(&w1_mlock);
	list_for_each_entry(dev, &w1_masters, w1_master_entry) {
		if (dev->bus_master->data == data) {
			found = 1;
			atomic_inc(&dev->refcnt);
			break;
		}
	}
	spin_unlock_bh(&w1_mlock);

	return (found)?dev:NULL;
}

void w1_reconnect_slaves(struct w1_family *f)
{
	struct w1_master *dev;

	spin_lock_bh(&w1_mlock);
	list_for_each_entry(dev, &w1_masters, w1_master_entry) {
		dev_info(&dev->dev, "Reconnecting slaves in %s into new family %02x.\n",
				dev->name, f->fid);
		set_bit(W1_MASTER_NEED_RECONNECT, &dev->flags);
	}
	spin_unlock_bh(&w1_mlock);
}


static void w1_slave_found(unsigned long data, u64 rn)
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
		printk(KERN_ERR "Failed to find w1 master device for data %08lx, it is impossible.\n",
				data);
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
void w1_search(struct w1_master *dev, w1_slave_found_callback cb)
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
		w1_write_8(dev, W1_SEARCH);
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
	int err, have_to_wait = 0;

	daemonize("w1_control");
	allow_signal(SIGTERM);

	while (!control_needs_exit || have_to_wait) {
		have_to_wait = 0;

		try_to_freeze();
		msleep_interruptible(w1_timeout * 1000);

		if (signal_pending(current))
			flush_signals(current);

		list_for_each_entry_safe(dev, n, &w1_masters, w1_master_entry) {
			if (!control_needs_exit && !dev->flags)
				continue;
			/*
			 * Little race: we can create thread but not set the flag.
			 * Get a chance for external process to set flag up.
			 */
			if (!dev->initialized) {
				have_to_wait = 1;
				continue;
			}

			if (control_needs_exit) {
				set_bit(W1_MASTER_NEED_EXIT, &dev->flags);

				err = kill_proc(dev->kpid, SIGTERM, 1);
				if (err)
					dev_err(&dev->dev,
						 "Failed to send signal to w1 kernel thread %d.\n",
						 dev->kpid);
			}

			if (test_bit(W1_MASTER_NEED_EXIT, &dev->flags)) {
				wait_for_completion(&dev->dev_exited);
				spin_lock_bh(&w1_mlock);
				list_del(&dev->w1_master_entry);
				spin_unlock_bh(&w1_mlock);

				list_for_each_entry_safe(sl, sln, &dev->slist, w1_slave_entry) {
					list_del(&sl->w1_slave_entry);

					w1_slave_detach(sl);
					kfree(sl);
				}
				w1_destroy_master_attributes(dev);
				atomic_dec(&dev->refcnt);
				continue;
			}

			if (test_bit(W1_MASTER_NEED_RECONNECT, &dev->flags)) {
				dev_info(&dev->dev, "Reconnecting slaves in device %s.\n", dev->name);
				down(&dev->mutex);
				list_for_each_entry(sl, &dev->slist, w1_slave_entry) {
					if (sl->family->fid == W1_FAMILY_DEFAULT) {
						struct w1_reg_num rn;
						list_del(&sl->w1_slave_entry);
						w1_slave_detach(sl);

						memcpy(&rn, &sl->reg_num, sizeof(rn));

						kfree(sl);

						w1_attach_slave_device(dev, &rn);
					}
				}
				clear_bit(W1_MASTER_NEED_RECONNECT, &dev->flags);
				up(&dev->mutex);
			}
		}
	}

	complete_and_exit(&w1_control_complete, 0);
}

int w1_process(void *data)
{
	struct w1_master *dev = (struct w1_master *) data;
	struct w1_slave *sl, *sln;

	daemonize("%s", dev->name);
	allow_signal(SIGTERM);

	while (!test_bit(W1_MASTER_NEED_EXIT, &dev->flags)) {
		try_to_freeze();
		msleep_interruptible(w1_timeout * 1000);

		if (signal_pending(current))
			flush_signals(current);

		if (test_bit(W1_MASTER_NEED_EXIT, &dev->flags))
			break;

		if (!dev->initialized)
			continue;

		if (dev->search_count == 0)
			continue;

		if (down_interruptible(&dev->mutex))
			continue;

		list_for_each_entry(sl, &dev->slist, w1_slave_entry)
			clear_bit(W1_SLAVE_ACTIVE, (long *)&sl->flags);

		w1_search_devices(dev, w1_slave_found);

		list_for_each_entry_safe(sl, sln, &dev->slist, w1_slave_entry) {
			if (!test_bit(W1_SLAVE_ACTIVE, (unsigned long *)&sl->flags) && !--sl->ttl) {
				list_del (&sl->w1_slave_entry);

				w1_slave_detach (sl);
				kfree (sl);

				dev->slave_count--;
			} else if (test_bit(W1_SLAVE_ACTIVE, (unsigned long *)&sl->flags))
				sl->ttl = dev->slave_ttl;
		}

		if (dev->search_count > 0)
			dev->search_count--;

		up(&dev->mutex);
	}

	atomic_dec(&dev->refcnt);
	complete_and_exit(&dev->dev_exited, 0);

	return 0;
}

static int w1_init(void)
{
	int retval;

	printk(KERN_INFO "Driver for 1-wire Dallas network protocol.\n");

	retval = bus_register(&w1_bus_type);
	if (retval) {
		printk(KERN_ERR "Failed to register bus. err=%d.\n", retval);
		goto err_out_exit_init;
	}

	retval = driver_register(&w1_driver);
	if (retval) {
		printk(KERN_ERR
			"Failed to register master driver. err=%d.\n",
			retval);
		goto err_out_bus_unregister;
	}

	control_thread = kernel_thread(&w1_control, NULL, 0);
	if (control_thread < 0) {
		printk(KERN_ERR "Failed to create control thread. err=%d\n",
			control_thread);
		retval = control_thread;
		goto err_out_driver_unregister;
	}

	return 0;

err_out_driver_unregister:
	driver_unregister(&w1_driver);

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

	control_needs_exit = 1;
	wait_for_completion(&w1_control_complete);

	driver_unregister(&w1_driver);
	bus_unregister(&w1_bus_type);
}

module_init(w1_init);
module_exit(w1_fini);
