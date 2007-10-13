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

static int sprom2hex(const u16 *sprom, char *buf, size_t buf_len)
{
	int i, pos = 0;

	for (i = 0; i < BCM43xx_SPROM_SIZE; i++) {
		pos += snprintf(buf + pos, buf_len - pos - 1,
				"%04X", swab16(sprom[i]) & 0xFFFF);
	}
	pos += snprintf(buf + pos, buf_len - pos - 1, "\n");

	return pos + 1;
}

static int hex2sprom(u16 *sprom, const char *dump, size_t len)
{
	char tmp[5] = { 0 };
	int cnt = 0;
	unsigned long parsed;

	if (len < BCM43xx_SPROM_SIZE * sizeof(u16) * 2)
		return -EINVAL;

	while (cnt < BCM43xx_SPROM_SIZE) {
		memcpy(tmp, dump, 4);
		dump += 4;
		parsed = simple_strtoul(tmp, NULL, 16);
		sprom[cnt++] = swab16((u16)parsed);
	}

	return 0;
}

static ssize_t bcm43xx_attr_sprom_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct bcm43xx_private *bcm = dev_to_bcm(dev);
	u16 *sprom;
	unsigned long flags;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	assert(BCM43xx_SPROM_SIZE * sizeof(u16) <= PAGE_SIZE);
	sprom = kmalloc(BCM43xx_SPROM_SIZE * sizeof(*sprom),
			GFP_KERNEL);
	if (!sprom)
		return -ENOMEM;
	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);
	err = bcm43xx_sprom_read(bcm, sprom);
	if (!err)
		err = sprom2hex(sprom, buf, PAGE_SIZE);
	mmiowb();
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);
	kfree(sprom);

	return err;
}

static ssize_t bcm43xx_attr_sprom_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct bcm43xx_private *bcm = dev_to_bcm(dev);
	u16 *sprom;
	unsigned long flags;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	sprom = kmalloc(BCM43xx_SPROM_SIZE * sizeof(*sprom),
			GFP_KERNEL);
	if (!sprom)
		return -ENOMEM;
	err = hex2sprom(sprom, buf, count);
	if (err)
		goto out_kfree;
	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);
	spin_lock(&bcm->leds_lock);
	err = bcm43xx_sprom_write(bcm, sprom);
	mmiowb();
	spin_unlock(&bcm->leds_lock);
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);
out_kfree:
	kfree(sprom);

	return err ? err : count;

}

static DEVICE_ATTR(sprom, 0600,
		   bcm43xx_attr_sprom_show,
		   bcm43xx_attr_sprom_store);

static ssize_t bcm43xx_attr_interfmode_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct bcm43xx_private *bcm = dev_to_bcm(dev);
	ssize_t count = 0;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	mutex_lock(&bcm->mutex);

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

	mutex_unlock(&bcm->mutex);

	return count;

}

static ssize_t bcm43xx_attr_interfmode_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct bcm43xx_private *bcm = dev_to_bcm(dev);
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

	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);

	err = bcm43xx_radio_set_interference_mitigation(bcm, mode);
	if (err) {
		printk(KERN_ERR PFX "Interference Mitigation not "
				    "supported by device\n");
	}
	mmiowb();
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);

	return err ? err : count;
}

static DEVICE_ATTR(interference, 0644,
		   bcm43xx_attr_interfmode_show,
		   bcm43xx_attr_interfmode_store);

static ssize_t bcm43xx_attr_preamble_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct bcm43xx_private *bcm = dev_to_bcm(dev);
	ssize_t count;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	mutex_lock(&bcm->mutex);

	if (bcm->short_preamble)
		count = snprintf(buf, PAGE_SIZE, "1 (Short Preamble enabled)\n");
	else
		count = snprintf(buf, PAGE_SIZE, "0 (Short Preamble disabled)\n");

	mutex_unlock(&bcm->mutex);

	return count;
}

static ssize_t bcm43xx_attr_preamble_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct bcm43xx_private *bcm = dev_to_bcm(dev);
	unsigned long flags;
	int value;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	value = get_boolean(buf, count);
	if (value < 0)
		return value;
	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);

	bcm->short_preamble = !!value;

	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);

	return count;
}

static DEVICE_ATTR(shortpreamble, 0644,
		   bcm43xx_attr_preamble_show,
		   bcm43xx_attr_preamble_store);

static ssize_t bcm43xx_attr_phymode_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct bcm43xx_private *bcm = dev_to_bcm(dev);
	int phytype;
	int err = -EINVAL;

	if (count < 1)
		goto out;
	switch (buf[0]) {
	case 'a':  case 'A':
		phytype = BCM43xx_PHYTYPE_A;
		break;
	case 'b':  case 'B':
		phytype = BCM43xx_PHYTYPE_B;
		break;
	case 'g':  case 'G':
		phytype = BCM43xx_PHYTYPE_G;
		break;
	default:
		goto out;
	}

	bcm43xx_cancel_work(bcm);
	mutex_lock(&(bcm)->mutex);
	err = bcm43xx_select_wireless_core(bcm, phytype);
	if (!err)
		bcm43xx_periodic_tasks_setup(bcm);
	mutex_unlock(&(bcm)->mutex);
	if (err == -ESRCH)
		err = -ENODEV;

out:
	return err ? err : count;
}

static ssize_t bcm43xx_attr_phymode_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct bcm43xx_private *bcm = dev_to_bcm(dev);
	ssize_t count = 0;

	mutex_lock(&(bcm)->mutex);
	switch (bcm43xx_current_phy(bcm)->type) {
	case BCM43xx_PHYTYPE_A:
		snprintf(buf, PAGE_SIZE, "A");
		break;
	case BCM43xx_PHYTYPE_B:
		snprintf(buf, PAGE_SIZE, "B");
		break;
	case BCM43xx_PHYTYPE_G:
		snprintf(buf, PAGE_SIZE, "G");
		break;
	default:
		assert(0);
	}
	mutex_unlock(&(bcm)->mutex);

	return count;
}

static DEVICE_ATTR(phymode, 0644,
		   bcm43xx_attr_phymode_show,
		   bcm43xx_attr_phymode_store);

static ssize_t bcm43xx_attr_microcode_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	unsigned long flags;
	struct bcm43xx_private *bcm = dev_to_bcm(dev);
	ssize_t count = 0;
	u16 status;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	mutex_lock(&(bcm)->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);
	status = bcm43xx_shm_read16(bcm, BCM43xx_SHM_SHARED,
				    BCM43xx_UCODE_STATUS);

	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&(bcm)->mutex);
	switch (status) {
	case 0x0000:
		count = snprintf(buf, PAGE_SIZE, "0x%.4x (invalid)\n",
				 status);
		break;
	case 0x0001:
		count = snprintf(buf, PAGE_SIZE, "0x%.4x (init)\n",
				 status);
		break;
	case 0x0002:
		count = snprintf(buf, PAGE_SIZE, "0x%.4x (active)\n",
				 status);
		break;
	case 0x0003:
		count = snprintf(buf, PAGE_SIZE, "0x%.4x (suspended)\n",
				 status);
		break;
	case 0x0004:
		count = snprintf(buf, PAGE_SIZE, "0x%.4x (asleep)\n",
				 status);
		break;
	default:
		count = snprintf(buf, PAGE_SIZE, "0x%.4x (unknown)\n",
				 status);
		break;
	}

	return count;
}

static DEVICE_ATTR(microcodestatus, 0444,
		   bcm43xx_attr_microcode_show,
		   NULL);

int bcm43xx_sysfs_register(struct bcm43xx_private *bcm)
{
	struct device *dev = &bcm->pci_dev->dev;
	int err;

	assert(bcm43xx_status(bcm) == BCM43xx_STAT_INITIALIZED);

	err = device_create_file(dev, &dev_attr_sprom);
	if (err)
		goto out;
	err = device_create_file(dev, &dev_attr_interference);
	if (err)
		goto err_remove_sprom;
	err = device_create_file(dev, &dev_attr_shortpreamble);
	if (err)
		goto err_remove_interfmode;
	err = device_create_file(dev, &dev_attr_phymode);
	if (err)
		goto err_remove_shortpreamble;
	err = device_create_file(dev, &dev_attr_microcodestatus);
	if (err)
		goto err_remove_phymode;

out:
	return err;
err_remove_phymode:
	device_remove_file(dev, &dev_attr_phymode);
err_remove_shortpreamble:
	device_remove_file(dev, &dev_attr_shortpreamble);
err_remove_interfmode:
	device_remove_file(dev, &dev_attr_interference);
err_remove_sprom:
	device_remove_file(dev, &dev_attr_sprom);
	goto out;
}

void bcm43xx_sysfs_unregister(struct bcm43xx_private *bcm)
{
	struct device *dev = &bcm->pci_dev->dev;

	device_remove_file(dev, &dev_attr_microcodestatus);
	device_remove_file(dev, &dev_attr_phymode);
	device_remove_file(dev, &dev_attr_shortpreamble);
	device_remove_file(dev, &dev_attr_interference);
	device_remove_file(dev, &dev_attr_sprom);
}
