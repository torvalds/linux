/*

  Broadcom BCM43xx wireless driver

  SYSFS support routines

  Copyright (c) 2006 Michael Buesch <mbuesch@freenet.de>

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

#include "bcm43xx_sysfs.h"
#include "bcm43xx.h"
#include "bcm43xx_main.h"
#include "bcm43xx_radio.h"

#include <linux/capability.h>


#define GENERIC_FILESIZE	64


static int get_integer(const char *buf, size_t count)
{
	char tmp[10 + 1] = { 0 };
	int ret = -EINVAL;

	if (count == 0)
		goto out;
	count = min(count, (size_t)10);
	memcpy(tmp, buf, count);
	ret = simple_strtol(tmp, NULL, 10);
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

static ssize_t bcm43xx_attr_sprom_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct bcm43xx_private *bcm = devattr_to_bcm(attr, attr_sprom);
	u16 *sprom;
	unsigned long flags;
	int i, err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	assert(BCM43xx_SPROM_SIZE * sizeof(u16) <= PAGE_SIZE);
	sprom = kmalloc(BCM43xx_SPROM_SIZE * sizeof(*sprom),
			GFP_KERNEL);
	if (!sprom)
		return -ENOMEM;
	bcm43xx_lock_mmio(bcm, flags);
	assert(bcm->initialized);
	err = bcm43xx_sprom_read(bcm, sprom);
	if (!err) {
		for (i = 0; i < BCM43xx_SPROM_SIZE; i++) {
			buf[i * 2] = sprom[i] & 0x00FF;
			buf[i * 2 + 1] = (sprom[i] & 0xFF00) >> 8;
		}
	}
	bcm43xx_unlock_mmio(bcm, flags);
	kfree(sprom);

	return err ? err : BCM43xx_SPROM_SIZE * sizeof(u16);
}

static ssize_t bcm43xx_attr_sprom_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct bcm43xx_private *bcm = devattr_to_bcm(attr, attr_sprom);
	u16 *sprom;
	unsigned long flags;
	int i, err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (count != BCM43xx_SPROM_SIZE * sizeof(u16))
		return -EINVAL;
	sprom = kmalloc(BCM43xx_SPROM_SIZE * sizeof(*sprom),
			GFP_KERNEL);
	if (!sprom)
		return -ENOMEM;
	for (i = 0; i < BCM43xx_SPROM_SIZE; i++) {
		sprom[i] = buf[i * 2] & 0xFF;
		sprom[i] |= ((u16)(buf[i * 2 + 1] & 0xFF)) << 8;
	}
	bcm43xx_lock_mmio(bcm, flags);
	assert(bcm->initialized);
	err = bcm43xx_sprom_write(bcm, sprom);
	bcm43xx_unlock_mmio(bcm, flags);
	kfree(sprom);

	return err ? err : count;

}

static ssize_t bcm43xx_attr_interfmode_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct bcm43xx_private *bcm = devattr_to_bcm(attr, attr_interfmode);
	unsigned long flags;
	int err;
	ssize_t count = 0;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	bcm43xx_lock(bcm, flags);
	assert(bcm->initialized);

	switch (bcm43xx_current_radio(bcm)->interfmode) {
	case BCM43xx_RADIO_INTERFMODE_NONE:
		count = snprintf(buf, PAGE_SIZE, "0 (No Interference Mitigation)\n");
		break;
	case BCM43xx_RADIO_INTERFMODE_NONWLAN:
		count = snprintf(buf, PAGE_SIZE, "1 (Non-WLAN Interference Mitigation)\n");
		break;
	case BCM43xx_RADIO_INTERFMODE_MANUALWLAN:
		count = snprintf(buf, PAGE_SIZE, "2 (WLAN Interference Mitigation)\n");
		break;
	default:
		assert(0);
	}
	err = 0;

	bcm43xx_unlock(bcm, flags);

	return err ? err : count;

}

static ssize_t bcm43xx_attr_interfmode_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct bcm43xx_private *bcm = devattr_to_bcm(attr, attr_interfmode);
	unsigned long flags;
	int err;
	int mode;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	mode = get_integer(buf, count);
	switch (mode) {
	case 0:
		mode = BCM43xx_RADIO_INTERFMODE_NONE;
		break;
	case 1:
		mode = BCM43xx_RADIO_INTERFMODE_NONWLAN;
		break;
	case 2:
		mode = BCM43xx_RADIO_INTERFMODE_MANUALWLAN;
		break;
	case 3:
		mode = BCM43xx_RADIO_INTERFMODE_AUTOWLAN;
		break;
	default:
		return -EINVAL;
	}

	bcm43xx_lock_mmio(bcm, flags);
	assert(bcm->initialized);

	err = bcm43xx_radio_set_interference_mitigation(bcm, mode);
	if (err) {
		printk(KERN_ERR PFX "Interference Mitigation not "
				    "supported by device\n");
	}

	bcm43xx_unlock_mmio(bcm, flags);

	return err ? err : count;
}

static ssize_t bcm43xx_attr_preamble_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct bcm43xx_private *bcm = devattr_to_bcm(attr, attr_preamble);
	unsigned long flags;
	int err;
	ssize_t count;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	bcm43xx_lock(bcm, flags);
	assert(bcm->initialized);

	if (bcm->short_preamble)
		count = snprintf(buf, PAGE_SIZE, "1 (Short Preamble enabled)\n");
	else
		count = snprintf(buf, PAGE_SIZE, "0 (Short Preamble disabled)\n");

	err = 0;
	bcm43xx_unlock(bcm, flags);

	return err ? err : count;
}

static ssize_t bcm43xx_attr_preamble_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct bcm43xx_private *bcm = devattr_to_bcm(attr, attr_preamble);
	unsigned long flags;
	int err;
	int value;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	value = get_boolean(buf, count);
	if (value < 0)
		return value;
	bcm43xx_lock(bcm, flags);
	assert(bcm->initialized);

	bcm->short_preamble = !!value;

	err = 0;
	bcm43xx_unlock(bcm, flags);

	return err ? err : count;
}

int bcm43xx_sysfs_register(struct bcm43xx_private *bcm)
{
	struct device *dev = &bcm->pci_dev->dev;
	struct bcm43xx_sysfs *sysfs = &bcm->sysfs;
	int err;

	assert(bcm->initialized);

	sysfs->attr_sprom.attr.name = "sprom";
	sysfs->attr_sprom.attr.owner = THIS_MODULE;
	sysfs->attr_sprom.attr.mode = 0600;
	sysfs->attr_sprom.show = bcm43xx_attr_sprom_show;
	sysfs->attr_sprom.store = bcm43xx_attr_sprom_store;
	err = device_create_file(dev, &sysfs->attr_sprom);
	if (err)
		goto out;

	sysfs->attr_interfmode.attr.name = "interference";
	sysfs->attr_interfmode.attr.owner = THIS_MODULE;
	sysfs->attr_interfmode.attr.mode = 0600;
	sysfs->attr_interfmode.show = bcm43xx_attr_interfmode_show;
	sysfs->attr_interfmode.store = bcm43xx_attr_interfmode_store;
	err = device_create_file(dev, &sysfs->attr_interfmode);
	if (err)
		goto err_remove_sprom;

	sysfs->attr_preamble.attr.name = "shortpreamble";
	sysfs->attr_preamble.attr.owner = THIS_MODULE;
	sysfs->attr_preamble.attr.mode = 0600;
	sysfs->attr_preamble.show = bcm43xx_attr_preamble_show;
	sysfs->attr_preamble.store = bcm43xx_attr_preamble_store;
	err = device_create_file(dev, &sysfs->attr_preamble);
	if (err)
		goto err_remove_interfmode;

out:
	return err;
err_remove_interfmode:
	device_remove_file(dev, &sysfs->attr_interfmode);
err_remove_sprom:
	device_remove_file(dev, &sysfs->attr_sprom);
	goto out;
}

void bcm43xx_sysfs_unregister(struct bcm43xx_private *bcm)
{
	struct device *dev = &bcm->pci_dev->dev;
	struct bcm43xx_sysfs *sysfs = &bcm->sysfs;

	device_remove_file(dev, &sysfs->attr_preamble);
	device_remove_file(dev, &sysfs->attr_interfmode);
	device_remove_file(dev, &sysfs->attr_sprom);
}
