// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2017 Pengutronix, Uwe Kleine-König <kernel@pengutronix.de>
 */
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "siox.h"

/*
 * The lowest bit in the SIOX status word signals if the in-device watchdog is
 * ok. If the bit is set, the device is functional.
 *
 * On writing the watchdog timer is reset when this bit toggles.
 */
#define SIOX_STATUS_WDG			0x01

/*
 * Bits 1 to 3 of the status word read as the bitwise negation of what was
 * clocked in before. The value clocked in is changed in each cycle and so
 * allows to detect transmit/receive problems.
 */
#define SIOX_STATUS_COUNTER		0x0e

/*
 * Each Siox-Device has a 4 bit type number that is neither 0 nor 15. This is
 * available in the upper nibble of the read status.
 *
 * On write these bits are DC.
 */
#define SIOX_STATUS_TYPE		0xf0

#define CREATE_TRACE_POINTS
#include <trace/events/siox.h>

static bool siox_is_registered;

static void siox_master_lock(struct siox_master *smaster)
{
	mutex_lock(&smaster->lock);
}

static void siox_master_unlock(struct siox_master *smaster)
{
	mutex_unlock(&smaster->lock);
}

static inline u8 siox_status_clean(u8 status_read, u8 status_written)
{
	/*
	 * bits 3:1 of status sample the respective bit in the status
	 * byte written in the previous cycle but inverted. So if you wrote the
	 * status word as 0xa before (counter = 0b101), it is expected to get
	 * back the counter bits as 0b010.
	 *
	 * So given the last status written this function toggles the there
	 * unset counter bits in the read value such that the counter bits in
	 * the return value are all zero iff the bits were read as expected to
	 * simplify error detection.
	 */

	return status_read ^ (~status_written & 0xe);
}

static bool siox_device_counter_error(struct siox_device *sdevice,
				      u8 status_clean)
{
	return (status_clean & SIOX_STATUS_COUNTER) != 0;
}

static bool siox_device_type_error(struct siox_device *sdevice, u8 status_clean)
{
	u8 statustype = (status_clean & SIOX_STATUS_TYPE) >> 4;

	/*
	 * If the device knows which value the type bits should have, check
	 * against this value otherwise just rule out the invalid values 0b0000
	 * and 0b1111.
	 */
	if (sdevice->statustype) {
		if (statustype != sdevice->statustype)
			return true;
	} else {
		switch (statustype) {
		case 0:
		case 0xf:
			return true;
		}
	}

	return false;
}

static bool siox_device_wdg_error(struct siox_device *sdevice, u8 status_clean)
{
	return (status_clean & SIOX_STATUS_WDG) == 0;
}

/*
 * If there is a type or counter error the device is called "unsynced".
 */
bool siox_device_synced(struct siox_device *sdevice)
{
	if (siox_device_type_error(sdevice, sdevice->status_read_clean))
		return false;

	return !siox_device_counter_error(sdevice, sdevice->status_read_clean);

}
EXPORT_SYMBOL_GPL(siox_device_synced);

/*
 * A device is called "connected" if it is synced and the watchdog is not
 * asserted.
 */
bool siox_device_connected(struct siox_device *sdevice)
{
	if (!siox_device_synced(sdevice))
		return false;

	return !siox_device_wdg_error(sdevice, sdevice->status_read_clean);
}
EXPORT_SYMBOL_GPL(siox_device_connected);

static void siox_poll(struct siox_master *smaster)
{
	struct siox_device *sdevice;
	size_t i = smaster->setbuf_len;
	unsigned int devno = 0;
	int unsync_error = 0;

	smaster->last_poll = jiffies;

	/*
	 * The counter bits change in each second cycle, the watchdog bit
	 * toggles each time.
	 * The counter bits hold values from [0, 6]. 7 would be possible
	 * theoretically but the protocol designer considered that a bad idea
	 * for reasons unknown today. (Maybe that's because then the status read
	 * back has only zeros in the counter bits then which might be confused
	 * with a stuck-at-0 error. But for the same reason (with s/0/1/) 0
	 * could be skipped.)
	 */
	if (++smaster->status > 0x0d)
		smaster->status = 0;

	memset(smaster->buf, 0, smaster->setbuf_len);

	/* prepare data pushed out to devices in buf[0..setbuf_len) */
	list_for_each_entry(sdevice, &smaster->devices, node) {
		struct siox_driver *sdriver =
			to_siox_driver(sdevice->dev.driver);
		sdevice->status_written = smaster->status;

		i -= sdevice->inbytes;

		/*
		 * If the device or a previous one is unsynced, don't pet the
		 * watchdog. This is done to ensure that the device is kept in
		 * reset when something is wrong.
		 */
		if (!siox_device_synced(sdevice))
			unsync_error = 1;

		if (sdriver && !unsync_error)
			sdriver->set_data(sdevice, sdevice->status_written,
					  &smaster->buf[i + 1]);
		else
			/*
			 * Don't trigger watchdog if there is no driver or a
			 * sync problem
			 */
			sdevice->status_written &= ~SIOX_STATUS_WDG;

		smaster->buf[i] = sdevice->status_written;

		trace_siox_set_data(smaster, sdevice, devno, i);

		devno++;
	}

	smaster->pushpull(smaster, smaster->setbuf_len, smaster->buf,
			  smaster->getbuf_len,
			  smaster->buf + smaster->setbuf_len);

	unsync_error = 0;

	/* interpret data pulled in from devices in buf[setbuf_len..] */
	devno = 0;
	i = smaster->setbuf_len;
	list_for_each_entry(sdevice, &smaster->devices, node) {
		struct siox_driver *sdriver =
			to_siox_driver(sdevice->dev.driver);
		u8 status = smaster->buf[i + sdevice->outbytes - 1];
		u8 status_clean;
		u8 prev_status_clean = sdevice->status_read_clean;
		bool synced = true;
		bool connected = true;

		if (!siox_device_synced(sdevice))
			unsync_error = 1;

		/*
		 * If the watchdog bit wasn't toggled in this cycle, report the
		 * watchdog as active to give a consistent view for drivers and
		 * sysfs consumers.
		 */
		if (!sdriver || unsync_error)
			status &= ~SIOX_STATUS_WDG;

		status_clean =
			siox_status_clean(status,
					  sdevice->status_written_lastcycle);

		/* Check counter and type bits */
		if (siox_device_counter_error(sdevice, status_clean) ||
		    siox_device_type_error(sdevice, status_clean)) {
			bool prev_error;

			synced = false;

			/* only report a new error if the last cycle was ok */
			prev_error =
				siox_device_counter_error(sdevice,
							  prev_status_clean) ||
				siox_device_type_error(sdevice,
						       prev_status_clean);

			if (!prev_error) {
				sdevice->status_errors++;
				sysfs_notify_dirent(sdevice->status_errors_kn);
			}
		}

		/* If the device is unsynced report the watchdog as active */
		if (!synced) {
			status &= ~SIOX_STATUS_WDG;
			status_clean &= ~SIOX_STATUS_WDG;
		}

		if (siox_device_wdg_error(sdevice, status_clean))
			connected = false;

		/* The watchdog state changed just now */
		if ((status_clean ^ prev_status_clean) & SIOX_STATUS_WDG) {
			sysfs_notify_dirent(sdevice->watchdog_kn);

			if (siox_device_wdg_error(sdevice, status_clean)) {
				struct kernfs_node *wd_errs =
					sdevice->watchdog_errors_kn;

				sdevice->watchdog_errors++;
				sysfs_notify_dirent(wd_errs);
			}
		}

		if (connected != sdevice->connected)
			sysfs_notify_dirent(sdevice->connected_kn);

		sdevice->status_read_clean = status_clean;
		sdevice->status_written_lastcycle = sdevice->status_written;
		sdevice->connected = connected;

		trace_siox_get_data(smaster, sdevice, devno, status_clean, i);

		/* only give data read to driver if the device is connected */
		if (sdriver && connected)
			sdriver->get_data(sdevice, &smaster->buf[i]);

		devno++;
		i += sdevice->outbytes;
	}
}

static int siox_poll_thread(void *data)
{
	struct siox_master *smaster = data;
	signed long timeout = 0;

	get_device(&smaster->dev);

	for (;;) {
		if (kthread_should_stop()) {
			put_device(&smaster->dev);
			return 0;
		}

		siox_master_lock(smaster);

		if (smaster->active) {
			unsigned long next_poll =
				smaster->last_poll + smaster->poll_interval;
			if (time_is_before_eq_jiffies(next_poll))
				siox_poll(smaster);

			timeout = smaster->poll_interval -
				(jiffies - smaster->last_poll);
		} else {
			timeout = MAX_SCHEDULE_TIMEOUT;
		}

		/*
		 * Set the task to idle while holding the lock. This makes sure
		 * that we don't sleep too long when the bus is reenabled before
		 * schedule_timeout is reached.
		 */
		if (timeout > 0)
			set_current_state(TASK_IDLE);

		siox_master_unlock(smaster);

		if (timeout > 0)
			schedule_timeout(timeout);

		/*
		 * I'm not clear if/why it is important to set the state to
		 * RUNNING again, but it fixes a "do not call blocking ops when
		 * !TASK_RUNNING;"-warning.
		 */
		set_current_state(TASK_RUNNING);
	}
}

static int __siox_start(struct siox_master *smaster)
{
	if (!(smaster->setbuf_len + smaster->getbuf_len))
		return -ENODEV;

	if (!smaster->buf)
		return -ENOMEM;

	if (smaster->active)
		return 0;

	smaster->active = 1;
	wake_up_process(smaster->poll_thread);

	return 1;
}

static int siox_start(struct siox_master *smaster)
{
	int ret;

	siox_master_lock(smaster);
	ret = __siox_start(smaster);
	siox_master_unlock(smaster);

	return ret;
}

static int __siox_stop(struct siox_master *smaster)
{
	if (smaster->active) {
		struct siox_device *sdevice;

		smaster->active = 0;

		list_for_each_entry(sdevice, &smaster->devices, node) {
			if (sdevice->connected)
				sysfs_notify_dirent(sdevice->connected_kn);
			sdevice->connected = false;
		}

		return 1;
	}
	return 0;
}

static int siox_stop(struct siox_master *smaster)
{
	int ret;

	siox_master_lock(smaster);
	ret = __siox_stop(smaster);
	siox_master_unlock(smaster);

	return ret;
}

static ssize_t type_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct siox_device *sdev = to_siox_device(dev);

	return sprintf(buf, "%s\n", sdev->type);
}

static DEVICE_ATTR_RO(type);

static ssize_t inbytes_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct siox_device *sdev = to_siox_device(dev);

	return sprintf(buf, "%zu\n", sdev->inbytes);
}

static DEVICE_ATTR_RO(inbytes);

static ssize_t outbytes_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct siox_device *sdev = to_siox_device(dev);

	return sprintf(buf, "%zu\n", sdev->outbytes);
}

static DEVICE_ATTR_RO(outbytes);

static ssize_t status_errors_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct siox_device *sdev = to_siox_device(dev);
	unsigned int status_errors;

	siox_master_lock(sdev->smaster);

	status_errors = sdev->status_errors;

	siox_master_unlock(sdev->smaster);

	return sprintf(buf, "%u\n", status_errors);
}

static DEVICE_ATTR_RO(status_errors);

static ssize_t connected_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct siox_device *sdev = to_siox_device(dev);
	bool connected;

	siox_master_lock(sdev->smaster);

	connected = sdev->connected;

	siox_master_unlock(sdev->smaster);

	return sprintf(buf, "%u\n", connected);
}

static DEVICE_ATTR_RO(connected);

static ssize_t watchdog_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct siox_device *sdev = to_siox_device(dev);
	u8 status;

	siox_master_lock(sdev->smaster);

	status = sdev->status_read_clean;

	siox_master_unlock(sdev->smaster);

	return sprintf(buf, "%d\n", status & SIOX_STATUS_WDG);
}

static DEVICE_ATTR_RO(watchdog);

static ssize_t watchdog_errors_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct siox_device *sdev = to_siox_device(dev);
	unsigned int watchdog_errors;

	siox_master_lock(sdev->smaster);

	watchdog_errors = sdev->watchdog_errors;

	siox_master_unlock(sdev->smaster);

	return sprintf(buf, "%u\n", watchdog_errors);
}

static DEVICE_ATTR_RO(watchdog_errors);

static struct attribute *siox_device_attrs[] = {
	&dev_attr_type.attr,
	&dev_attr_inbytes.attr,
	&dev_attr_outbytes.attr,
	&dev_attr_status_errors.attr,
	&dev_attr_connected.attr,
	&dev_attr_watchdog.attr,
	&dev_attr_watchdog_errors.attr,
	NULL
};
ATTRIBUTE_GROUPS(siox_device);

static void siox_device_release(struct device *dev)
{
	struct siox_device *sdevice = to_siox_device(dev);

	kfree(sdevice);
}

static const struct device_type siox_device_type = {
	.groups = siox_device_groups,
	.release = siox_device_release,
};

static int siox_match(struct device *dev, const struct device_driver *drv)
{
	if (dev->type != &siox_device_type)
		return 0;

	/* up to now there is only a single driver so keeping this simple */
	return 1;
}

static int siox_probe(struct device *dev)
{
	struct siox_driver *sdriver = to_siox_driver(dev->driver);
	struct siox_device *sdevice = to_siox_device(dev);

	return sdriver->probe(sdevice);
}

static void siox_remove(struct device *dev)
{
	struct siox_driver *sdriver =
		container_of(dev->driver, struct siox_driver, driver);
	struct siox_device *sdevice = to_siox_device(dev);

	if (sdriver->remove)
		sdriver->remove(sdevice);
}

static void siox_shutdown(struct device *dev)
{
	struct siox_device *sdevice = to_siox_device(dev);
	struct siox_driver *sdriver;

	if (!dev->driver)
		return;

	sdriver = container_of(dev->driver, struct siox_driver, driver);
	if (sdriver->shutdown)
		sdriver->shutdown(sdevice);
}

static const struct bus_type siox_bus_type = {
	.name = "siox",
	.match = siox_match,
	.probe = siox_probe,
	.remove = siox_remove,
	.shutdown = siox_shutdown,
};

static ssize_t active_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct siox_master *smaster = to_siox_master(dev);

	return sprintf(buf, "%d\n", smaster->active);
}

static ssize_t active_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct siox_master *smaster = to_siox_master(dev);
	int ret;
	int active;

	ret = kstrtoint(buf, 0, &active);
	if (ret < 0)
		return ret;

	if (active)
		ret = siox_start(smaster);
	else
		ret = siox_stop(smaster);

	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR_RW(active);

static struct siox_device *siox_device_add(struct siox_master *smaster,
					   const char *type, size_t inbytes,
					   size_t outbytes, u8 statustype);

static ssize_t device_add_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct siox_master *smaster = to_siox_master(dev);
	int ret;
	char type[20] = "";
	size_t inbytes = 0, outbytes = 0;
	u8 statustype = 0;

	ret = sscanf(buf, "%19s %zu %zu %hhu", type, &inbytes,
		     &outbytes, &statustype);
	if (ret != 3 && ret != 4)
		return -EINVAL;

	if (strcmp(type, "siox-12x8") || inbytes != 2 || outbytes != 4)
		return -EINVAL;

	siox_device_add(smaster, "siox-12x8", inbytes, outbytes, statustype);

	return count;
}

static DEVICE_ATTR_WO(device_add);

static void siox_device_remove(struct siox_master *smaster);

static ssize_t device_remove_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct siox_master *smaster = to_siox_master(dev);

	/* XXX? require to write <type> <inbytes> <outbytes> */
	siox_device_remove(smaster);

	return count;
}

static DEVICE_ATTR_WO(device_remove);

static ssize_t poll_interval_ns_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct siox_master *smaster = to_siox_master(dev);

	return sprintf(buf, "%lld\n", jiffies_to_nsecs(smaster->poll_interval));
}

static ssize_t poll_interval_ns_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct siox_master *smaster = to_siox_master(dev);
	int ret;
	u64 val;

	ret = kstrtou64(buf, 0, &val);
	if (ret < 0)
		return ret;

	siox_master_lock(smaster);

	smaster->poll_interval = nsecs_to_jiffies(val);

	siox_master_unlock(smaster);

	return count;
}

static DEVICE_ATTR_RW(poll_interval_ns);

static struct attribute *siox_master_attrs[] = {
	&dev_attr_active.attr,
	&dev_attr_device_add.attr,
	&dev_attr_device_remove.attr,
	&dev_attr_poll_interval_ns.attr,
	NULL
};
ATTRIBUTE_GROUPS(siox_master);

static void siox_master_release(struct device *dev)
{
	struct siox_master *smaster = to_siox_master(dev);

	kfree(smaster);
}

static const struct device_type siox_master_type = {
	.groups = siox_master_groups,
	.release = siox_master_release,
};

struct siox_master *siox_master_alloc(struct device *dev,
				      size_t size)
{
	struct siox_master *smaster;

	if (!dev)
		return NULL;

	smaster = kzalloc(sizeof(*smaster) + size, GFP_KERNEL);
	if (!smaster)
		return NULL;

	device_initialize(&smaster->dev);

	smaster->busno = -1;
	smaster->dev.bus = &siox_bus_type;
	smaster->dev.type = &siox_master_type;
	smaster->dev.parent = dev;
	smaster->poll_interval = DIV_ROUND_UP(HZ, 40);

	dev_set_drvdata(&smaster->dev, &smaster[1]);

	return smaster;
}
EXPORT_SYMBOL_GPL(siox_master_alloc);

static void devm_siox_master_put(void *data)
{
	struct siox_master *smaster = data;

	siox_master_put(smaster);
}

struct siox_master *devm_siox_master_alloc(struct device *dev,
					   size_t size)
{
	struct siox_master *smaster;
	int ret;

	smaster = siox_master_alloc(dev, size);
	if (!smaster)
		return NULL;

	ret = devm_add_action_or_reset(dev, devm_siox_master_put, smaster);
	if (ret)
		return NULL;

	return smaster;
}
EXPORT_SYMBOL_GPL(devm_siox_master_alloc);

int siox_master_register(struct siox_master *smaster)
{
	int ret;

	if (!siox_is_registered)
		return -EPROBE_DEFER;

	if (!smaster->pushpull)
		return -EINVAL;

	get_device(&smaster->dev);

	dev_set_name(&smaster->dev, "siox-%d", smaster->busno);

	mutex_init(&smaster->lock);
	INIT_LIST_HEAD(&smaster->devices);

	smaster->last_poll = jiffies;
	smaster->poll_thread = kthread_run(siox_poll_thread, smaster,
					   "siox-%d", smaster->busno);
	if (IS_ERR(smaster->poll_thread)) {
		smaster->active = 0;
		return PTR_ERR(smaster->poll_thread);
	}

	ret = device_add(&smaster->dev);
	if (ret)
		kthread_stop(smaster->poll_thread);

	return ret;
}
EXPORT_SYMBOL_GPL(siox_master_register);

void siox_master_unregister(struct siox_master *smaster)
{
	/* remove device */
	device_del(&smaster->dev);

	siox_master_lock(smaster);

	__siox_stop(smaster);

	while (smaster->num_devices) {
		struct siox_device *sdevice;

		sdevice = container_of(smaster->devices.prev,
				       struct siox_device, node);
		list_del(&sdevice->node);
		smaster->num_devices--;

		siox_master_unlock(smaster);

		device_unregister(&sdevice->dev);

		siox_master_lock(smaster);
	}

	siox_master_unlock(smaster);

	put_device(&smaster->dev);
}
EXPORT_SYMBOL_GPL(siox_master_unregister);

static void devm_siox_master_unregister(void *data)
{
	struct siox_master *smaster = data;

	siox_master_unregister(smaster);
}

int devm_siox_master_register(struct device *dev, struct siox_master *smaster)
{
	int ret;

	ret = siox_master_register(smaster);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, devm_siox_master_unregister, smaster);
}
EXPORT_SYMBOL_GPL(devm_siox_master_register);

static struct siox_device *siox_device_add(struct siox_master *smaster,
					   const char *type, size_t inbytes,
					   size_t outbytes, u8 statustype)
{
	struct siox_device *sdevice;
	int ret;
	size_t buf_len;

	sdevice = kzalloc(sizeof(*sdevice), GFP_KERNEL);
	if (!sdevice)
		return ERR_PTR(-ENOMEM);

	sdevice->type = type;
	sdevice->inbytes = inbytes;
	sdevice->outbytes = outbytes;
	sdevice->statustype = statustype;

	sdevice->smaster = smaster;
	sdevice->dev.parent = &smaster->dev;
	sdevice->dev.bus = &siox_bus_type;
	sdevice->dev.type = &siox_device_type;

	siox_master_lock(smaster);

	dev_set_name(&sdevice->dev, "siox-%d-%d",
		     smaster->busno, smaster->num_devices);

	buf_len = smaster->setbuf_len + inbytes +
		smaster->getbuf_len + outbytes;
	if (smaster->buf_len < buf_len) {
		u8 *buf = krealloc(smaster->buf, buf_len, GFP_KERNEL);

		if (!buf) {
			dev_err(&smaster->dev,
				"failed to realloc buffer to %zu\n", buf_len);
			ret = -ENOMEM;
			goto err_buf_alloc;
		}

		smaster->buf_len = buf_len;
		smaster->buf = buf;
	}

	ret = device_register(&sdevice->dev);
	if (ret) {
		dev_err(&smaster->dev, "failed to register device: %d\n", ret);

		goto err_device_register;
	}

	smaster->num_devices++;
	list_add_tail(&sdevice->node, &smaster->devices);

	smaster->setbuf_len += sdevice->inbytes;
	smaster->getbuf_len += sdevice->outbytes;

	sdevice->status_errors_kn = sysfs_get_dirent(sdevice->dev.kobj.sd,
						     "status_errors");
	sdevice->watchdog_kn = sysfs_get_dirent(sdevice->dev.kobj.sd,
						"watchdog");
	sdevice->watchdog_errors_kn = sysfs_get_dirent(sdevice->dev.kobj.sd,
						       "watchdog_errors");
	sdevice->connected_kn = sysfs_get_dirent(sdevice->dev.kobj.sd,
						 "connected");

	siox_master_unlock(smaster);

	return sdevice;

err_device_register:
	/* don't care to make the buffer smaller again */
	put_device(&sdevice->dev);
	sdevice = NULL;

err_buf_alloc:
	siox_master_unlock(smaster);

	kfree(sdevice);

	return ERR_PTR(ret);
}

static void siox_device_remove(struct siox_master *smaster)
{
	struct siox_device *sdevice;

	siox_master_lock(smaster);

	if (!smaster->num_devices) {
		siox_master_unlock(smaster);
		return;
	}

	sdevice = container_of(smaster->devices.prev, struct siox_device, node);
	list_del(&sdevice->node);
	smaster->num_devices--;

	smaster->setbuf_len -= sdevice->inbytes;
	smaster->getbuf_len -= sdevice->outbytes;

	if (!smaster->num_devices)
		__siox_stop(smaster);

	siox_master_unlock(smaster);

	/*
	 * This must be done without holding the master lock because we're
	 * called from device_remove_store which also holds a sysfs mutex.
	 * device_unregister tries to aquire the same lock.
	 */
	device_unregister(&sdevice->dev);
}

int __siox_driver_register(struct siox_driver *sdriver, struct module *owner)
{
	int ret;

	if (unlikely(!siox_is_registered))
		return -EPROBE_DEFER;

	if (!sdriver->probe ||
	    (!sdriver->set_data && !sdriver->get_data)) {
		pr_err("Driver %s doesn't provide needed callbacks\n",
		       sdriver->driver.name);
		return -EINVAL;
	}

	sdriver->driver.owner = owner;
	sdriver->driver.bus = &siox_bus_type;

	ret = driver_register(&sdriver->driver);
	if (ret)
		pr_err("Failed to register siox driver %s (%d)\n",
		       sdriver->driver.name, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(__siox_driver_register);

static int __init siox_init(void)
{
	int ret;

	ret = bus_register(&siox_bus_type);
	if (ret) {
		pr_err("Registration of SIOX bus type failed: %d\n", ret);
		return ret;
	}

	siox_is_registered = true;

	return 0;
}
subsys_initcall(siox_init);

static void __exit siox_exit(void)
{
	bus_unregister(&siox_bus_type);
}
module_exit(siox_exit);

MODULE_AUTHOR("Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>");
MODULE_DESCRIPTION("Eckelmann SIOX driver core");
MODULE_LICENSE("GPL v2");
