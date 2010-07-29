/* drivers/mfd/cpcap-audio.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *      Iliyan Malchev <malchev@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spi/cpcap-regbits.h>
#include <linux/spi/cpcap.h>
#include <linux/regulator/consumer.h>
#include <linux/cpcap_audio.h>
#include <linux/spi/cpcap.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/cpcap_audio.h>
#include <linux/uaccess.h>

#include <mach/cpcap_audio.h>

static struct cpcap_device *cpcap;
static struct cpcap_audio_platform_data *pdata;
static unsigned current_output = CPCAP_AUDIO_OUT_SPEAKER;
static unsigned current_volume = 10;

static int cpcap_audio_set(const struct cpcap_audio_path *path, bool on)
{
	int len, rc;
	const struct cpcap_audio_config_table *entry;

	pr_info("%s: %s\n", __func__, path->name);

	if (!path) {
		pr_info("%s: no path\n", __func__);
		return -ENOSYS;
	}

	if (path->gpio >= 0) {
		pr_info("%s: %s: enable gpio %d\n", __func__,
			path->name, path->gpio);
		rc = gpio_direction_output(path->gpio, on);
		if (rc)
			pr_err("%s: could not set gpio %d to %d\n", __func__,
				path->gpio, on);
	}

	if (!on)
		return 0;
	if (!path->table) {
		pr_info("%s: no config table for path %s\n", __func__,
				path->name);
		return -ENOSYS;
	}

	entry = path->table;
	len = path->table_len;
	while (len--) {
		u16 val = entry->val | (pdata->master ? 0 : entry->slave_or);
		int rc = cpcap_regacc_write(cpcap,
				entry->reg,
				val,
				entry->mask);
		if (rc) {
			pr_err("%s: cpcap_regacc_write %d %x/%x %x failed: %d\n",
				__func__,
				entry->reg,
				entry->val,
				entry->slave_or,
				entry->mask, rc);
			rc = -EIO;
		}
		entry++;
	}

	return 0;
}

static int cpcap_set_volume(struct cpcap_device *cpcap, unsigned volume)
{
	pr_info("%s\n", __func__);
	volume &= 0xF;
	volume = volume << 12 | volume << 8;
	return cpcap_regacc_write(cpcap, CPCAP_REG_RXVC, volume, 0xFF00);
}

static int cpcap_audio_ctl_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int cpcap_audio_ctl_release(struct inode *inode, struct file *file)
{
	return 0;
}

static DEFINE_MUTEX(cpcap_lock);

static long cpcap_audio_ctl_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	int rc = 0;

	mutex_lock(&cpcap_lock);

	switch (cmd) {
	case CPCAP_AUDIO_OUT_SET_OUTPUT:
		if (arg > CPCAP_AUDIO_OUT_MAX) {
			pr_err("%s: invalid audio path selector %ld\n",
				__func__, arg);
			goto done;
		}
		if (current_output == arg) {
			pr_info("%s: no path change\n", __func__);
			goto done;
		}
		if (current_output == CPCAP_AUDIO_OUT_SPEAKER) {
			pr_info("%s: setting path to %s\n", __func__,
					pdata->headset->name);
			cpcap_audio_set(pdata->speaker, 0);
			cpcap_audio_set(pdata->headset, 1);
		} else {
			pr_info("%s: setting path to %s\n", __func__,
					pdata->speaker->name);
			cpcap_audio_set(pdata->headset, 0);
			cpcap_audio_set(pdata->speaker, 1);
		}
		current_output = arg;
		break;
	case CPCAP_AUDIO_OUT_GET_OUTPUT:
		if (copy_to_user((void *)arg, &current_output,
					sizeof(unsigned int))) {
			rc = -EFAULT;
			goto done;
		}
		break;
	case CPCAP_AUDIO_OUT_SET_VOLUME:
		if (arg > CPCAP_AUDIO_OUT_VOL_MAX) {
			pr_err("%s: invalid audio volume selector %ld\n",
				__func__, arg);
			goto done;
		}
		if (current_volume == arg) {
			pr_info("%s: no volume change\n", __func__);
			goto done;
		}
		rc = cpcap_set_volume(cpcap, (unsigned)arg);
		if (rc < 0) {
			pr_err("%s: could not set audio volume to %ld: %d\n",
				__func__, arg, rc);
			goto done;
		}
		current_volume = arg;
		break;
	case CPCAP_AUDIO_OUT_GET_VOLUME:
		if (copy_to_user((void *)arg, &current_volume,
					sizeof(unsigned int))) {
			rc = -EFAULT;
			goto done;
		}
		break;
	}

done:
	mutex_unlock(&cpcap_lock);
	return rc;
}

static const struct file_operations cpcap_audio_ctl_fops = {
	.open = cpcap_audio_ctl_open,
	.release = cpcap_audio_ctl_release,
	.unlocked_ioctl = cpcap_audio_ctl_ioctl,
};

static struct miscdevice cpcap_audio_ctl = {
	.name = "audio_ctl",
	.minor = MISC_DYNAMIC_MINOR,
	.fops = &cpcap_audio_ctl_fops,
};

static int cpcap_audio_probe(struct platform_device *pdev)
{
	int rc;
	struct regulator *audio_reg;

	pr_info("%s\n", __func__);

	cpcap = platform_get_drvdata(pdev);
	BUG_ON(!cpcap);

	pdata = pdev->dev.platform_data;
	BUG_ON(!pdata);

	audio_reg = regulator_get(NULL, "vaudio");
	if (IS_ERR(audio_reg)) {
		rc = PTR_ERR(audio_reg);
		pr_err("%s: could not get vaudio regulator: %d\n", __func__,
			rc);
		return rc;
	}

	rc = regulator_enable(audio_reg);
	if (rc) {
		pr_err("%s: failed to enable vaudio regulator: %d\n", __func__,
			rc);
		regulator_put(audio_reg);
		return rc;
	}

	if (pdata->speaker->gpio >= 0) {
		tegra_gpio_enable(pdata->speaker->gpio);
		rc = gpio_request(pdata->speaker->gpio, pdata->speaker->name);
		if (rc) {
			pr_err("%s: could not get speaker GPIO %d: %d\n",
				__func__, pdata->speaker->gpio, rc);
			goto fail;
		}
	}

	if (pdata->headset->gpio >= 0) {
		tegra_gpio_enable(pdata->headset->gpio);
		rc = gpio_request(pdata->headset->gpio, pdata->headset->name);
		if (rc) {
			pr_err("%s: could not get headset GPIO %d: %d\n",
				__func__, pdata->headset->gpio, rc);
			goto fail2;
		}
	}

	cpcap_audio_set(pdata->speaker, 1);
	cpcap_set_volume(cpcap, current_volume);

	rc = misc_register(&cpcap_audio_ctl);
	if (rc < 0) {
		pr_err("%s: failed to register misc device: %d\n", __func__,
				rc);
		goto fail3;
	}

	return rc;

fail3:
	if (pdata->headset->gpio >= 0)
		gpio_free(pdata->headset->gpio);
fail2:
	if (pdata->speaker->gpio >= 0)
		gpio_free(pdata->speaker->gpio);
fail:
	regulator_put(audio_reg);
	return rc;
}

static struct platform_driver cpcap_audio_driver = {
	.probe = cpcap_audio_probe,
	.driver = {
		.name = "cpcap_audio",
		.owner = THIS_MODULE,
	},
};

static int __init cpcap_audio_init(void)
{
	return cpcap_driver_register(&cpcap_audio_driver);
}

module_init(cpcap_audio_init);
MODULE_LICENSE("GPL");
