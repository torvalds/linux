// SPDX-License-Identifier: GPL-2.0-or-later
/*

  Broadcom B43legacy wireless driver

  SYSFS support routines

  Copyright (c) 2006 Michael Buesch <m@bues.ch>


*/

#include "sysfs.h"
#include "b43legacy.h"
#include "main.h"
#include "phy.h"
#include "radio.h"

#include <linux/capability.h>


#define GENERIC_FILESIZE	64


static int get_integer(const char *buf, size_t count)
{
	char tmp[10 + 1] = { 0 };
	int ret = -EINVAL, res;

	if (count == 0)
		goto out;
	count = min_t(size_t, count, 10);
	memcpy(tmp, buf, count);
	ret = kstrtoint(tmp, 10, &res);
	if (!ret)
		return res;
out:
	return ret;
}

static int get_boolean(const char *buf, size_t count)
{
	if (count != 0) {
		if (buf[0] == '1')
			return 1;
		if (buf[0] == '0')
			return 0;
		if (count >= 4 && memcmp(buf, "true", 4) == 0)
			return 1;
		if (count >= 5 && memcmp(buf, "false", 5) == 0)
			return 0;
		if (count >= 3 && memcmp(buf, "yes", 3) == 0)
			return 1;
		if (count >= 2 && memcmp(buf, "no", 2) == 0)
			return 0;
		if (count >= 2 && memcmp(buf, "on", 2) == 0)
			return 1;
		if (count >= 3 && memcmp(buf, "off", 3) == 0)
			return 0;
	}
	return -EINVAL;
}

static ssize_t b43legacy_attr_interfmode_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct b43legacy_wldev *wldev = dev_to_b43legacy_wldev(dev);
	ssize_t count = 0;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	mutex_lock(&wldev->wl->mutex);

	switch (wldev->phy.interfmode) {
	case B43legacy_INTERFMODE_NONE:
		count = sysfs_emit(buf, "0 (No Interference Mitigation)\n");
		break;
	case B43legacy_INTERFMODE_NONWLAN:
		count = sysfs_emit(buf,
				   "1 (Non-WLAN Interference Mitigation)\n");
		break;
	case B43legacy_INTERFMODE_MANUALWLAN:
		count = sysfs_emit(buf, "2 (WLAN Interference Mitigation)\n");
		break;
	default:
		B43legacy_WARN_ON(1);
	}

	mutex_unlock(&wldev->wl->mutex);

	return count;
}

static ssize_t b43legacy_attr_interfmode_store(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t count)
{
	struct b43legacy_wldev *wldev = dev_to_b43legacy_wldev(dev);
	unsigned long flags;
	int err;
	int mode;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	mode = get_integer(buf, count);
	switch (mode) {
	case 0:
		mode = B43legacy_INTERFMODE_NONE;
		break;
	case 1:
		mode = B43legacy_INTERFMODE_NONWLAN;
		break;
	case 2:
		mode = B43legacy_INTERFMODE_MANUALWLAN;
		break;
	case 3:
		mode = B43legacy_INTERFMODE_AUTOWLAN;
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&wldev->wl->mutex);
	spin_lock_irqsave(&wldev->wl->irq_lock, flags);

	err = b43legacy_radio_set_interference_mitigation(wldev, mode);
	if (err)
		b43legacyerr(wldev->wl, "Interference Mitigation not "
		       "supported by device\n");
	spin_unlock_irqrestore(&wldev->wl->irq_lock, flags);
	mutex_unlock(&wldev->wl->mutex);

	return err ? err : count;
}

static DEVICE_ATTR(interference, 0644,
		   b43legacy_attr_interfmode_show,
		   b43legacy_attr_interfmode_store);

static ssize_t b43legacy_attr_preamble_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct b43legacy_wldev *wldev = dev_to_b43legacy_wldev(dev);
	ssize_t count;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	mutex_lock(&wldev->wl->mutex);

	if (wldev->short_preamble)
		count = sysfs_emit(buf, "1 (Short Preamble enabled)\n");
	else
		count = sysfs_emit(buf, "0 (Short Preamble disabled)\n");

	mutex_unlock(&wldev->wl->mutex);

	return count;
}

static ssize_t b43legacy_attr_preamble_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct b43legacy_wldev *wldev = dev_to_b43legacy_wldev(dev);
	unsigned long flags;
	int value;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	value = get_boolean(buf, count);
	if (value < 0)
		return value;
	mutex_lock(&wldev->wl->mutex);
	spin_lock_irqsave(&wldev->wl->irq_lock, flags);

	wldev->short_preamble = !!value;

	spin_unlock_irqrestore(&wldev->wl->irq_lock, flags);
	mutex_unlock(&wldev->wl->mutex);

	return count;
}

static DEVICE_ATTR(shortpreamble, 0644,
		   b43legacy_attr_preamble_show,
		   b43legacy_attr_preamble_store);

int b43legacy_sysfs_register(struct b43legacy_wldev *wldev)
{
	struct device *dev = wldev->dev->dev;
	int err;

	B43legacy_WARN_ON(b43legacy_status(wldev) !=
			  B43legacy_STAT_INITIALIZED);

	err = device_create_file(dev, &dev_attr_interference);
	if (err)
		goto out;
	err = device_create_file(dev, &dev_attr_shortpreamble);
	if (err)
		goto err_remove_interfmode;

out:
	return err;
err_remove_interfmode:
	device_remove_file(dev, &dev_attr_interference);
	goto out;
}

void b43legacy_sysfs_unregister(struct b43legacy_wldev *wldev)
{
	struct device *dev = wldev->dev->dev;

	device_remove_file(dev, &dev_attr_shortpreamble);
	device_remove_file(dev, &dev_attr_interference);
}
