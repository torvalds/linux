// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2020-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

#include <mali_kbase.h>
#include "mali_kbase_csf_firmware_cfg.h"
#include <mali_kbase_reset_gpu.h>
#include <linux/version.h>

#if CONFIG_SYSFS
#define CSF_FIRMWARE_CFG_SYSFS_DIR_NAME "firmware_config"

/**
 * struct firmware_config - Configuration item within the MCU firmware
 *
 * @node:        List head linking all options to
 *               kbase_device:csf.firmware_config
 * @kbdev:       Pointer to the Kbase device
 * @kobj:        Kobject corresponding to the sysfs sub-directory,
 *               inside CSF_FIRMWARE_CFG_SYSFS_DIR_NAME directory,
 *               representing the configuration option @name.
 * @kobj_inited: kobject initialization state
 * @updatable:   Indicates whether config items can be updated with
 *               FIRMWARE_CONFIG_UPDATE
 * @name:        NUL-terminated string naming the option
 * @address:     The address in the firmware image of the configuration option
 * @min:         The lowest legal value of the configuration option
 * @max:         The maximum legal value of the configuration option
 * @cur_val:     The current value of the configuration option
 *
 * The firmware may expose configuration options. Each option has a name, the
 * address where the option is controlled and the minimum and maximum values
 * that the option can take.
 */
struct firmware_config {
	struct list_head node;
	struct kbase_device *kbdev;
	struct kobject kobj;
	bool kobj_inited;
	bool updatable;
	char *name;
	u32 address;
	u32 min;
	u32 max;
	u32 cur_val;
};

#define FW_CFG_ATTR(_name, _mode)					\
	struct attribute fw_cfg_attr_##_name = {			\
			.name = __stringify(_name),			\
			.mode = VERIFY_OCTAL_PERMISSIONS(_mode),	\
	}

static FW_CFG_ATTR(min, 0444);
static FW_CFG_ATTR(max, 0444);
static FW_CFG_ATTR(cur, 0644);

static void fw_cfg_kobj_release(struct kobject *kobj)
{
	struct firmware_config *config =
		container_of(kobj, struct firmware_config, kobj);

	kfree(config);
}

static ssize_t show_fw_cfg(struct kobject *kobj,
	struct attribute *attr, char *buf)
{
	struct firmware_config *config =
		container_of(kobj, struct firmware_config, kobj);
	struct kbase_device *kbdev = config->kbdev;
	u32 val = 0;

	if (!kbdev)
		return -ENODEV;

	if (attr == &fw_cfg_attr_max)
		val = config->max;
	else if (attr == &fw_cfg_attr_min)
		val = config->min;
	else if (attr == &fw_cfg_attr_cur) {
		unsigned long flags;

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		val = config->cur_val;
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	} else {
		dev_warn(kbdev->dev,
			"Unexpected read from entry %s/%s",
			config->name, attr->name);
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE, "%u\n", val);
}

static ssize_t store_fw_cfg(struct kobject *kobj,
	struct attribute *attr,
	const char *buf,
	size_t count)
{
	struct firmware_config *config =
		container_of(kobj, struct firmware_config, kobj);
	struct kbase_device *kbdev = config->kbdev;

	if (!kbdev)
		return -ENODEV;

	if (attr == &fw_cfg_attr_cur) {
		unsigned long flags;
		u32 val;
		int ret = kstrtouint(buf, 0, &val);

		if (ret) {
			dev_err(kbdev->dev,
				"Couldn't process %s/%s write operation.\n"
				"Use format <value>\n",
				config->name, attr->name);
			return -EINVAL;
		}

		if ((val < config->min) || (val > config->max))
			return -EINVAL;

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		if (config->cur_val == val) {
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			return count;
		}

		/* If configuration update cannot be performed with
		 * FIRMWARE_CONFIG_UPDATE then we need to do a
		 * silent reset before we update the memory.
		 */
		if (!config->updatable) {
			/*
			 * If there is already a GPU reset pending then inform
			 * the User to retry the write.
			 */
			if (kbase_reset_gpu_silent(kbdev)) {
				spin_unlock_irqrestore(&kbdev->hwaccess_lock,
						       flags);
				return -EAGAIN;
			}
		}

		/*
		 * GPU reset request has been placed, now update the
		 * firmware image. GPU reset will take place only after
		 * hwaccess_lock is released.
		 * Update made to firmware image in memory would not
		 * be lost on GPU reset as configuration entries reside
		 * in the RONLY section of firmware image, which is not
		 * reloaded on firmware reboot due to GPU reset.
		 */
		kbase_csf_update_firmware_memory(
			kbdev, config->address, val);

		config->cur_val = val;

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

		/* If we can update the config without firmware reset then
		 * we need to just trigger FIRMWARE_CONFIG_UPDATE.
		 */
		if (config->updatable) {
			ret = kbase_csf_trigger_firmware_config_update(kbdev);
			if (ret)
				return ret;
		}

		/* Wait for the config update to take effect */
		if (!config->updatable)
			kbase_reset_gpu_wait(kbdev);
	} else {
		dev_warn(kbdev->dev,
			"Unexpected write to entry %s/%s",
			config->name, attr->name);
		return -EINVAL;
	}

	return count;
}

static const struct sysfs_ops fw_cfg_ops = {
	.show = &show_fw_cfg,
	.store = &store_fw_cfg,
};

static struct attribute *fw_cfg_attrs[] = {
	&fw_cfg_attr_min,
	&fw_cfg_attr_max,
	&fw_cfg_attr_cur,
	NULL,
};
#if (KERNEL_VERSION(5, 2, 0) <= LINUX_VERSION_CODE)
ATTRIBUTE_GROUPS(fw_cfg);
#endif

static struct kobj_type fw_cfg_kobj_type = {
	.release = &fw_cfg_kobj_release,
	.sysfs_ops = &fw_cfg_ops,
#if (KERNEL_VERSION(5, 2, 0) <= LINUX_VERSION_CODE)
	.default_groups = fw_cfg_groups,
#else
	.default_attrs = fw_cfg_attrs,
#endif
};

int kbase_csf_firmware_cfg_init(struct kbase_device *kbdev)
{
	struct firmware_config *config;

	kbdev->csf.fw_cfg_kobj = kobject_create_and_add(
		CSF_FIRMWARE_CFG_SYSFS_DIR_NAME, &kbdev->dev->kobj);
	if (!kbdev->csf.fw_cfg_kobj) {
		kobject_put(kbdev->csf.fw_cfg_kobj);
		dev_err(kbdev->dev,
			"Creation of %s sysfs sub-directory failed\n",
			CSF_FIRMWARE_CFG_SYSFS_DIR_NAME);
		return -ENOMEM;
	}

	list_for_each_entry(config, &kbdev->csf.firmware_config, node) {
		int err;

		kbase_csf_read_firmware_memory(kbdev, config->address,
			&config->cur_val);

		err = kobject_init_and_add(&config->kobj, &fw_cfg_kobj_type,
				kbdev->csf.fw_cfg_kobj, "%s", config->name);
		if (err) {
			kobject_put(&config->kobj);
			dev_err(kbdev->dev,
				"Creation of %s sysfs sub-directory failed\n",
				config->name);
			return err;
		}

		config->kobj_inited = true;
	}

	return 0;
}

void kbase_csf_firmware_cfg_term(struct kbase_device *kbdev)
{
	while (!list_empty(&kbdev->csf.firmware_config)) {
		struct firmware_config *config;

		config = list_first_entry(&kbdev->csf.firmware_config,
				struct firmware_config, node);
		list_del(&config->node);

		if (config->kobj_inited) {
			kobject_del(&config->kobj);
			kobject_put(&config->kobj);
		} else
			kfree(config);
	}

	kobject_del(kbdev->csf.fw_cfg_kobj);
	kobject_put(kbdev->csf.fw_cfg_kobj);
}

int kbase_csf_firmware_cfg_option_entry_parse(struct kbase_device *kbdev,
					      const struct kbase_csf_mcu_fw *const fw,
					      const u32 *entry, unsigned int size, bool updatable)
{
	const char *name = (char *)&entry[3];
	struct firmware_config *config;
	const unsigned int name_len = size - CONFIGURATION_ENTRY_NAME_OFFSET;

	/* Allocate enough space for struct firmware_config and the
	 * configuration option name (with NULL termination)
	 */
	config = kzalloc(sizeof(*config) + name_len + 1, GFP_KERNEL);

	if (!config)
		return -ENOMEM;

	config->kbdev = kbdev;
	config->updatable = updatable;
	config->name = (char *)(config+1);
	config->address = entry[0];
	config->min = entry[1];
	config->max = entry[2];

	memcpy(config->name, name, name_len);
	config->name[name_len] = 0;

	list_add(&config->node, &kbdev->csf.firmware_config);

	dev_dbg(kbdev->dev, "Configuration option '%s' at 0x%x range %u-%u",
			config->name, config->address,
			config->min, config->max);

	return 0;
}
#else
int kbase_csf_firmware_cfg_init(struct kbase_device *kbdev)
{
	return 0;
}

void kbase_csf_firmware_cfg_term(struct kbase_device *kbdev)
{
	/* !CONFIG_SYSFS: Nothing to do here */
}

int kbase_csf_firmware_cfg_option_entry_parse(struct kbase_device *kbdev,
					      const struct kbase_csf_mcu_fw *const fw,
					      const u32 *entry, unsigned int size)
{
	return 0;
}
#endif /* CONFIG_SYSFS */
