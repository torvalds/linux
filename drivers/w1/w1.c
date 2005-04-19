/*
 * 	w1.c
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

static ssize_t w1_default_read_name(struct device *dev, char *buf)
{
	return sprintf(buf, "No family registered.\n");
}

static ssize_t w1_default_read_bin(struct kobject *kobj, char *buf, loff_t off,
		     size_t count)
{
	return sprintf(buf, "No family registered.\n");
}

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

static struct device_attribute w1_slave_attribute = {
	.attr = {
			.name = "name",
			.mode = S_IRUGO,
			.owner = THIS_MODULE
	},
	.show = &w1_default_read_name,
};

static struct device_attribute w1_slave_attribute_val = {
	.attr = {
			.name = "value",
			.mode = S_IRUGO,
			.owner = THIS_MODULE
	},
	.show = &w1_default_read_name,
};

static ssize_t w1_master_attribute_show_name(struct device *dev, char *buf)
{
	struct w1_master *md = container_of (dev, struct w1_master, dev);
	ssize_t count;
	
	if (down_interruptible (&md->mutex))
		return -EBUSY;

	count = sprintf(buf, "%s\n", md->name);
	
	up(&md->mutex);

	return count;
}

static ssize_t w1_master_attribute_show_pointer(struct device *dev, char *buf)
{
	struct w1_master *md = container_of(dev, struct w1_master, dev);
	ssize_t count;
	
	if (down_interruptible(&md->mutex))
		return -EBUSY;

	count = sprintf(buf, "0x%p\n", md->bus_master);
	
	up(&md->mutex);
	return count;
}

static ssize_t w1_master_attribute_show_timeout(struct device *dev, char *buf)
{
	ssize_t count;
	count = sprintf(buf, "%d\n", w1_timeout);
	return count;
}

static ssize_t w1_master_attribute_show_max_slave_count(struct device *dev, char *buf)
{
	struct w1_master *md = container_of(dev, struct w1_master, dev);
	ssize_t count;
	
	if (down_interruptible(&md->mutex))
		return -EBUSY;

	count = sprintf(buf, "%d\n", md->max_slave_count);
	
	up(&md->mutex);
	return count;
}

static ssize_t w1_master_attribute_show_attempts(struct device *dev, char *buf)
{
	struct w1_master *md = container_of(dev, struct w1_master, dev);
	ssize_t count;
	
	if (down_interruptible(&md->mutex))
		return -EBUSY;

	count = sprintf(buf, "%lu\n", md->attempts);
	
	up(&md->mutex);
	return count;
}

static ssize_t w1_master_attribute_show_slave_count(struct device *dev, char *buf)
{
	struct w1_master *md = container_of(dev, struct w1_master, dev);
	ssize_t count;
	
	if (down_interruptible(&md->mutex))
		return -EBUSY;

	count = sprintf(buf, "%d\n", md->slave_count);
	
	up(&md->mutex);
	return count;
}

static ssize_t w1_master_attribute_show_slaves(struct device *dev, char *buf)

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

static struct device_attribute w1_master_attribute_slaves = {
	.attr = {
 			.name = "w1_master_slaves",
			.mode = S_IRUGO,
			.owner = THIS_MODULE,
	},
 	.show = &w1_master_attribute_show_slaves,
};
static struct device_attribute w1_master_attribute_slave_count = {
	.attr = {
			.name = "w1_master_slave_count",
			.mode = S_IRUGO,
			.owner = THIS_MODULE
	},
	.show = &w1_master_attribute_show_slave_count,
};
static struct device_attribute w1_master_attribute_attempts = {
	.attr = {
			.name = "w1_master_attempts",
			.mode = S_IRUGO,
			.owner = THIS_MODULE
	},
	.show = &w1_master_attribute_show_attempts,
};
static struct device_attribute w1_master_attribute_max_slave_count = {
	.attr = {
			.name = "w1_master_max_slave_count",
			.mode = S_IRUGO,
			.owner = THIS_MODULE
	},
	.show = &w1_master_attribute_show_max_slave_count,
};
static struct device_attribute w1_master_attribute_timeout = {
	.attr = {
			.name = "w1_master_timeout",
			.mode = S_IRUGO,
			.owner = THIS_MODULE
	},
	.show = &w1_master_attribute_show_timeout,
};
static struct device_attribute w1_master_attribute_pointer = {
	.attr = {
			.name = "w1_master_pointer",
			.mode = S_IRUGO,
			.owner = THIS_MODULE
	},
	.show = &w1_master_attribute_show_pointer,
};
static struct device_attribute w1_master_attribute_name = {
	.attr = {
			.name = "w1_master_name",
			.mode = S_IRUGO,
			.owner = THIS_MODULE
	},
	.show = &w1_master_attribute_show_name,
};

static struct bin_attribute w1_slave_bin_attribute = {
	.attr = {
		 	.name = "w1_slave",
		 	.mode = S_IRUGO,
			.owner = THIS_MODULE,
	},
	.size = W1_SLAVE_DATA_SIZE,
	.read = &w1_default_read_bin,
};

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
	snprintf (&sl->name[0], sizeof(sl->name),
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
	memcpy(&sl->attr_val, &w1_slave_attribute_val, sizeof(sl->attr_val));
	
	sl->attr_bin.read = sl->family->fops->rbin;
	sl->attr_name.show = sl->family->fops->rname;
	sl->attr_val.show = sl->family->fops->rval;
	sl->attr_val.attr.name = sl->family->fops->rvalname;

	err = device_create_file(&sl->dev, &sl->attr_name);
	if (err < 0) {
		dev_err(&sl->dev,
			 "sysfs file creation for [%s] failed. err=%d\n",
			 sl->dev.bus_id, err);
		device_unregister(&sl->dev);
		return err;
	}

	err = device_create_file(&sl->dev, &sl->attr_val);
	if (err < 0) {
		dev_err(&sl->dev,
			 "sysfs file creation for [%s] failed. err=%d\n",
			 sl->dev.bus_id, err);
		device_remove_file(&sl->dev, &sl->attr_name);
		device_unregister(&sl->dev);
		return err;
	}

	err = sysfs_create_bin_file(&sl->dev.kobj, &sl->attr_bin);
	if (err < 0) {
		dev_err(&sl->dev,
			 "sysfs file creation for [%s] failed. err=%d\n",
			 sl->dev.bus_id, err);
		device_remove_file(&sl->dev, &sl->attr_name);
		device_remove_file(&sl->dev, &sl->attr_val);
		device_unregister(&sl->dev);
		return err;
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
		spin_unlock(&w1_flock);
		dev_info(&dev->dev, "Family %x for %02x.%012llx.%02x is not registered.\n",
			  rn->family, rn->family,
			  (unsigned long long)rn->id, rn->crc);
		kfree(sl);
		return -ENODEV;
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

	sysfs_remove_bin_file (&sl->dev.kobj, &sl->attr_bin);
	device_remove_file(&sl->dev, &sl->attr_name);
	device_remove_file(&sl->dev, &sl->attr_val);
	device_unregister(&sl->dev);
	w1_family_put(sl->family);

	memcpy(&msg.id.id, &sl->reg_num, sizeof(msg.id.id));
	msg.type = W1_SLAVE_REMOVE;
	w1_netlink_send(sl->master, &msg);
}

static struct w1_master *w1_search_master(unsigned long data)
{
	struct w1_master *dev;
	int found = 0;
	
	spin_lock_irq(&w1_mlock);
	list_for_each_entry(dev, &w1_masters, w1_master_entry) {
		if (dev->bus_master->data == data) {
			found = 1;
			atomic_inc(&dev->refcnt);
			break;
		}
	}
	spin_unlock_irq(&w1_mlock);

	return (found)?dev:NULL;
}

void w1_slave_found(unsigned long data, u64 rn)
{
	int slave_count;
	struct w1_slave *sl;
	struct list_head *ent;
	struct w1_reg_num *tmp;
	int family_found = 0;
	struct w1_master *dev;

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
		}
		else if (sl->reg_num.family == tmp->family) {
			family_found = 1;
			break;
		}

		slave_count++;
	}

	rn = cpu_to_le64(rn);

	if (slave_count == dev->slave_count &&
		rn && ((le64_to_cpu(rn) >> 56) & 0xff) == w1_calc_crc8((u8 *)&rn, 7)) {
		w1_attach_slave_device(dev, tmp);
	}
			
	atomic_dec(&dev->refcnt);
}

void w1_search(struct w1_master *dev)
{
	u64 last, rn, tmp;
	int i, count = 0;
	int last_family_desc, last_zero, last_device;
	int search_bit, id_bit, comp_bit, desc_bit;

	search_bit = id_bit = comp_bit = 0;
	rn = tmp = last = 0;
	last_device = last_zero = last_family_desc = 0;

	desc_bit = 64;

	while (!(id_bit && comp_bit) && !last_device
		&& count++ < dev->max_slave_count) {
		last = rn;
		rn = 0;

		last_family_desc = 0;

		/*
		 * Reset bus and all 1-wire device state machines
		 * so they can respond to our requests.
		 *
		 * Return 0 - device(s) present, 1 - no devices present.
		 */
		if (w1_reset_bus(dev)) {
			dev_info(&dev->dev, "No devices present on the wire.\n");
			break;
		}

#if 1
		w1_write_8(dev, W1_SEARCH);
		for (i = 0; i < 64; ++i) {
			/*
			 * Read 2 bits from bus.
			 * All who don't sleep must send ID bit and COMPLEMENT ID bit.
			 * They actually are ANDed between all senders.
			 */
			id_bit = w1_touch_bit(dev, 1);
			comp_bit = w1_touch_bit(dev, 1);

			if (id_bit && comp_bit)
				break;

			if (id_bit == 0 && comp_bit == 0) {
				if (i == desc_bit)
					search_bit = 1;
				else if (i > desc_bit)
					search_bit = 0;
				else
					search_bit = ((last >> i) & 0x1);

				if (search_bit == 0) {
					last_zero = i;
					if (last_zero < 9)
						last_family_desc = last_zero;
				}

			}
			else
				search_bit = id_bit;

			tmp = search_bit;
			rn |= (tmp << i);

			/*
			 * Write 1 bit to bus
			 * and make all who don't have "search_bit" in "i"'th position
			 * in it's registration number sleep.
			 */
			if (dev->bus_master->touch_bit)
				w1_touch_bit(dev, search_bit);
			else
				w1_write_bit(dev, search_bit);

		}
#endif

		if (desc_bit == last_zero)
			last_device = 1;

		desc_bit = last_zero;
	
		w1_slave_found(dev->bus_master->data, rn);
	}
}

int w1_create_master_attributes(struct w1_master *dev)
{
	if (	device_create_file(&dev->dev, &w1_master_attribute_slaves) < 0 ||
		device_create_file(&dev->dev, &w1_master_attribute_slave_count) < 0 ||
		device_create_file(&dev->dev, &w1_master_attribute_attempts) < 0 ||
		device_create_file(&dev->dev, &w1_master_attribute_max_slave_count) < 0 ||
		device_create_file(&dev->dev, &w1_master_attribute_timeout) < 0||
		device_create_file(&dev->dev, &w1_master_attribute_pointer) < 0||
		device_create_file(&dev->dev, &w1_master_attribute_name) < 0)
		return -EINVAL;

	return 0;
}

void w1_destroy_master_attributes(struct w1_master *dev)
{
	device_remove_file(&dev->dev, &w1_master_attribute_slaves);
	device_remove_file(&dev->dev, &w1_master_attribute_slave_count);
	device_remove_file(&dev->dev, &w1_master_attribute_attempts);
	device_remove_file(&dev->dev, &w1_master_attribute_max_slave_count);
	device_remove_file(&dev->dev, &w1_master_attribute_timeout);
	device_remove_file(&dev->dev, &w1_master_attribute_pointer);
	device_remove_file(&dev->dev, &w1_master_attribute_name);
}


int w1_control(void *data)
{
	struct w1_slave *sl;
	struct w1_master *dev;
	struct list_head *ent, *ment, *n, *mn;
	int err, have_to_wait = 0;

	daemonize("w1_control");
	allow_signal(SIGTERM);

	while (!control_needs_exit || have_to_wait) {
		have_to_wait = 0;

		try_to_freeze(PF_FREEZE);
		msleep_interruptible(w1_timeout * 1000);

		if (signal_pending(current))
			flush_signals(current);

		list_for_each_safe(ment, mn, &w1_masters) {
			dev = list_entry(ment, struct w1_master, w1_master_entry);

			if (!control_needs_exit && !dev->need_exit)
				continue;
			/*
			 * Little race: we can create thread but not set the flag.
			 * Get a chance for external process to set flag up.
			 */
			if (!dev->initialized) {
				have_to_wait = 1;
				continue;
			}

			spin_lock(&w1_mlock);
			list_del(&dev->w1_master_entry);
			spin_unlock(&w1_mlock);

			if (control_needs_exit) {
				dev->need_exit = 1;

				err = kill_proc(dev->kpid, SIGTERM, 1);
				if (err)
					dev_err(&dev->dev,
						 "Failed to send signal to w1 kernel thread %d.\n",
						 dev->kpid);
			}

			wait_for_completion(&dev->dev_exited);

			list_for_each_safe(ent, n, &dev->slist) {
				sl = list_entry(ent, struct w1_slave, w1_slave_entry);

				if (!sl)
					dev_warn(&dev->dev,
						  "%s: slave entry is NULL.\n",
						  __func__);
				else {
					list_del(&sl->w1_slave_entry);

					w1_slave_detach(sl);
					kfree(sl);
				}
			}
			w1_destroy_master_attributes(dev);
			atomic_dec(&dev->refcnt);
		}
	}

	complete_and_exit(&w1_control_complete, 0);
}

int w1_process(void *data)
{
	struct w1_master *dev = (struct w1_master *) data;
	struct list_head *ent, *n;
	struct w1_slave *sl;

	daemonize("%s", dev->name);
	allow_signal(SIGTERM);

	while (!dev->need_exit) {
		try_to_freeze(PF_FREEZE);
		msleep_interruptible(w1_timeout * 1000);

		if (signal_pending(current))
			flush_signals(current);

		if (dev->need_exit)
			break;

		if (!dev->initialized)
			continue;

		if (down_interruptible(&dev->mutex))
			continue;

		list_for_each_safe(ent, n, &dev->slist) {
			sl = list_entry(ent, struct w1_slave, w1_slave_entry);

			if (sl)
				clear_bit(W1_SLAVE_ACTIVE, (long *)&sl->flags);
		}
		
		w1_search_devices(dev, w1_slave_found);

		list_for_each_safe(ent, n, &dev->slist) {
			sl = list_entry(ent, struct w1_slave, w1_slave_entry);

			if (sl && !test_bit(W1_SLAVE_ACTIVE, (unsigned long *)&sl->flags) && !--sl->ttl) {
				list_del (&sl->w1_slave_entry);

				w1_slave_detach (sl);
				kfree (sl);

				dev->slave_count--;
			}
			else if (test_bit(W1_SLAVE_ACTIVE, (unsigned long *)&sl->flags))
				sl->ttl = dev->slave_ttl;
		}
		up(&dev->mutex);
	}

	atomic_dec(&dev->refcnt);
	complete_and_exit(&dev->dev_exited, 0);

	return 0;
}

int w1_init(void)
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

void w1_fini(void)
{
	struct w1_master *dev;
	struct list_head *ent, *n;

	list_for_each_safe(ent, n, &w1_masters) {
		dev = list_entry(ent, struct w1_master, w1_master_entry);
		__w1_remove_master_device(dev);
	}

	control_needs_exit = 1;

	wait_for_completion(&w1_control_complete);

	driver_unregister(&w1_driver);
	bus_unregister(&w1_bus_type);
}

module_init(w1_init);
module_exit(w1_fini);
