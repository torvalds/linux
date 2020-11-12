/*
 *
 * (C) COPYRIGHT 2019-2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/of.h>

#include "mali_kbase.h"
#include "mali_kbase_config_defaults.h"
#include "mali_kbase_csf_firmware.h"
#include "mali_kbase_csf_timeout.h"
#include "backend/gpu/mali_kbase_pm_internal.h"

/**
 * set_timeout - set a new global progress timeout.
 *
 * @kbdev:   Instance of a GPU platform device that implements a command
 *           stream front-end interface.
 * @timeout: the maximum number of GPU cycles without forward progress to allow
 *           to elapse before terminating a GPU command queue group.
 *
 * Return:   0 on success, or negative on failure
 *           (e.g. -ERANGE if the requested timeout is too large).
 */
static int set_timeout(struct kbase_device *const kbdev, u64 const timeout)
{
	if (timeout > GLB_PROGRESS_TIMER_TIMEOUT_MAX) {
		dev_err(kbdev->dev, "Timeout %llu is too large.\n", timeout);
		return -ERANGE;
	}

	dev_dbg(kbdev->dev, "New progress timeout: %llu cycles\n", timeout);

	atomic64_set(&kbdev->csf.progress_timeout, timeout);

	return 0;
}

/**
 * progress_timeout_store - Store the progress_timeout device attribute.
 * @dev:   The device that has the attribute.
 * @attr:  The attributes of the sysfs file.
 * @buf:   The value written to the sysfs file.
 * @count: The number of bytes written to the sysfs file.
 *
 * This function is called when the progress_timeout sysfs file is written to.
 * It checks the data written, and if valid updates the progress timeout value.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t progress_timeout_store(struct device * const dev,
		struct device_attribute * const attr, const char * const buf,
		size_t const count)
{
	struct kbase_device *const kbdev = dev_get_drvdata(dev);
	int err;
	u64 timeout;

	if (!kbdev)
		return -ENODEV;

	err = kstrtou64(buf, 0, &timeout);
	if (err) {
		dev_err(kbdev->dev,
			"Couldn't process progress_timeout write operation.\n"
			"Use format <progress_timeout>\n");
		return err;
	}

	err = set_timeout(kbdev, timeout);
	if (!err) {
		kbase_csf_scheduler_pm_active(kbdev);

		err = kbase_pm_wait_for_desired_state(kbdev);
		if (!err)
			err = kbase_csf_firmware_set_timeout(kbdev, timeout);

		kbase_csf_scheduler_pm_idle(kbdev);
	}

	if (err)
		return err;

	return count;
}

/**
 * progress_timeout_show - Show the progress_timeout device attribute.
 * @dev: The device that has the attribute.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the global timeout.
 *
 * This function is called to get the progress timeout value.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t progress_timeout_show(struct device * const dev,
		struct device_attribute * const attr, char * const buf)
{
	struct kbase_device *const kbdev = dev_get_drvdata(dev);
	int err;

	if (!kbdev)
		return -ENODEV;

	err = scnprintf(buf, PAGE_SIZE, "%llu\n", kbase_csf_timeout_get(kbdev));

	return err;

}

static DEVICE_ATTR(progress_timeout, 0644, progress_timeout_show,
	progress_timeout_store);

int kbase_csf_timeout_init(struct kbase_device *const kbdev)
{
	u64 timeout = DEFAULT_PROGRESS_TIMEOUT;
	int err;

#ifdef CONFIG_OF
	err = of_property_read_u64(kbdev->dev->of_node,
		"progress_timeout", &timeout);
	if (!err)
		dev_info(kbdev->dev, "Found progress_timeout = %llu in Devicetree\n",
			timeout);
#endif

	err = set_timeout(kbdev, timeout);
	if (err)
		return err;

	err = sysfs_create_file(&kbdev->dev->kobj,
		&dev_attr_progress_timeout.attr);
	if (err)
		dev_err(kbdev->dev, "SysFS file creation failed\n");

	return err;
}

void kbase_csf_timeout_term(struct kbase_device * const kbdev)
{
	sysfs_remove_file(&kbdev->dev->kobj, &dev_attr_progress_timeout.attr);
}

u64 kbase_csf_timeout_get(struct kbase_device *const kbdev)
{
	return atomic64_read(&kbdev->csf.progress_timeout);
}
