/*

  Broadcom B43 wireless driver

  SYSFS support routines

  Copyright (c) 2006 Michael Buesch <m@bues.ch>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

#include <linux/capability.h>
#include <linux/io.h>

#include "b43.h"
#include "sysfs.h"
#include "main.h"
#include "phy_common.h"

#define GENERIC_FILESIZE	64

static int get_integer(const char *buf, size_t count)
{
	char tmp[10 + 1] = { 0 };
	int ret = -EINVAL;

	if (count == 0)
		goto out;
	count = min_t(size_t, count, 10);
	memcpy(tmp, buf, count);
	ret = simple_strtol(tmp, NULL, 10);
      out:
	return ret;
}

static ssize_t b43_attr_interfmode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct b43_wldev *wldev = dev_to_b43_wldev(dev);
	ssize_t count = 0;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	mutex_lock(&wldev->wl->mutex);

	if (wldev->phy.type != B43_PHYTYPE_G) {
		mutex_unlock(&wldev->wl->mutex);
		return -ENOSYS;
	}

	switch (wldev->phy.g->interfmode) {
	case B43_INTERFMODE_NONE:
		count =
		    snprintf(buf, PAGE_SIZE,
			     "0 (No Interference Mitigation)\n");
		break;
	case B43_INTERFMODE_NONWLAN:
		count =
		    snprintf(buf, PAGE_SIZE,
			     "1 (Non-WLAN Interference Mitigation)\n");
		break;
	case B43_INTERFMODE_MANUALWLAN:
		count =
		    snprintf(buf, PAGE_SIZE,
			     "2 (WLAN Interference Mitigation)\n");
		break;
	default:
		B43_WARN_ON(1);
	}

	mutex_unlock(&wldev->wl->mutex);

	return count;
}

static ssize_t b43_attr_interfmode_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct b43_wldev *wldev = dev_to_b43_wldev(dev);
	int err;
	int mode;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	mode = get_integer(buf, count);
	switch (mode) {
	case 0:
		mode = B43_INTERFMODE_NONE;
		break;
	case 1:
		mode = B43_INTERFMODE_NONWLAN;
		break;
	case 2:
		mode = B43_INTERFMODE_MANUALWLAN;
		break;
	case 3:
		mode = B43_INTERFMODE_AUTOWLAN;
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&wldev->wl->mutex);

	if (wldev->phy.ops->interf_mitigation) {
		err = wldev->phy.ops->interf_mitigation(wldev, mode);
		if (err) {
			b43err(wldev->wl, "Interference Mitigation not "
			       "supported by device\n");
		}
	} else
		err = -ENOSYS;

	mutex_unlock(&wldev->wl->mutex);

	return err ? err : count;
}

static DEVICE_ATTR(interference, 0644,
		   b43_attr_interfmode_show, b43_attr_interfmode_store);

int b43_sysfs_register(struct b43_wldev *wldev)
{
	struct device *dev = wldev->dev->dev;

	B43_WARN_ON(b43_status(wldev) != B43_STAT_INITIALIZED);

	return device_create_file(dev, &dev_attr_interference);
}

void b43_sysfs_unregister(struct b43_wldev *wldev)
{
	struct device *dev = wldev->dev->dev;

	device_remove_file(dev, &dev_attr_interference);
}
