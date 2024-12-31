// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2020 Intel Corporation */

#include <crypto/algapi.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/bitops.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_cfg.h"
#include "adf_cfg_common.h"
#include "adf_cfg_user.h"

#define ADF_CFG_MAX_SECTION 512
#define ADF_CFG_MAX_KEY_VAL 256

#define DEVICE_NAME "qat_adf_ctl"

static DEFINE_MUTEX(adf_ctl_lock);
static long adf_ctl_ioctl(struct file *fp, unsigned int cmd, unsigned long arg);

static const struct file_operations adf_ctl_ops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = adf_ctl_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static const struct class adf_ctl_class = {
	.name = DEVICE_NAME,
};

struct adf_ctl_drv_info {
	unsigned int major;
	struct cdev drv_cdev;
};

static struct adf_ctl_drv_info adf_ctl_drv;

static void adf_chr_drv_destroy(void)
{
	device_destroy(&adf_ctl_class, MKDEV(adf_ctl_drv.major, 0));
	cdev_del(&adf_ctl_drv.drv_cdev);
	class_unregister(&adf_ctl_class);
	unregister_chrdev_region(MKDEV(adf_ctl_drv.major, 0), 1);
}

static int adf_chr_drv_create(void)
{
	dev_t dev_id;
	struct device *drv_device;
	int ret;

	if (alloc_chrdev_region(&dev_id, 0, 1, DEVICE_NAME)) {
		pr_err("QAT: unable to allocate chrdev region\n");
		return -EFAULT;
	}

	ret = class_register(&adf_ctl_class);
	if (ret)
		goto err_chrdev_unreg;

	adf_ctl_drv.major = MAJOR(dev_id);
	cdev_init(&adf_ctl_drv.drv_cdev, &adf_ctl_ops);
	if (cdev_add(&adf_ctl_drv.drv_cdev, dev_id, 1)) {
		pr_err("QAT: cdev add failed\n");
		goto err_class_destr;
	}

	drv_device = device_create(&adf_ctl_class, NULL,
				   MKDEV(adf_ctl_drv.major, 0),
				   NULL, DEVICE_NAME);
	if (IS_ERR(drv_device)) {
		pr_err("QAT: failed to create device\n");
		goto err_cdev_del;
	}
	return 0;
err_cdev_del:
	cdev_del(&adf_ctl_drv.drv_cdev);
err_class_destr:
	class_unregister(&adf_ctl_class);
err_chrdev_unreg:
	unregister_chrdev_region(dev_id, 1);
	return -EFAULT;
}

static int adf_ctl_alloc_resources(struct adf_user_cfg_ctl_data **ctl_data,
				   unsigned long arg)
{
	struct adf_user_cfg_ctl_data *cfg_data;

	cfg_data = kzalloc(sizeof(*cfg_data), GFP_KERNEL);
	if (!cfg_data)
		return -ENOMEM;

	/* Initialize device id to NO DEVICE as 0 is a valid device id */
	cfg_data->device_id = ADF_CFG_NO_DEVICE;

	if (copy_from_user(cfg_data, (void __user *)arg, sizeof(*cfg_data))) {
		pr_err("QAT: failed to copy from user cfg_data.\n");
		kfree(cfg_data);
		return -EIO;
	}

	*ctl_data = cfg_data;
	return 0;
}

static int adf_add_key_value_data(struct adf_accel_dev *accel_dev,
				  const char *section,
				  const struct adf_user_cfg_key_val *key_val)
{
	if (key_val->type == ADF_HEX) {
		long *ptr = (long *)key_val->val;
		long val = *ptr;

		if (adf_cfg_add_key_value_param(accel_dev, section,
						key_val->key, (void *)val,
						key_val->type)) {
			dev_err(&GET_DEV(accel_dev),
				"failed to add hex keyvalue.\n");
			return -EFAULT;
		}
	} else {
		if (adf_cfg_add_key_value_param(accel_dev, section,
						key_val->key, key_val->val,
						key_val->type)) {
			dev_err(&GET_DEV(accel_dev),
				"failed to add keyvalue.\n");
			return -EFAULT;
		}
	}
	return 0;
}

static int adf_copy_key_value_data(struct adf_accel_dev *accel_dev,
				   struct adf_user_cfg_ctl_data *ctl_data)
{
	struct adf_user_cfg_key_val key_val;
	struct adf_user_cfg_key_val *params_head;
	struct adf_user_cfg_section section, *section_head;
	int i, j;

	section_head = ctl_data->config_section;

	for (i = 0; section_head && i < ADF_CFG_MAX_SECTION; i++) {
		if (copy_from_user(&section, (void __user *)section_head,
				   sizeof(*section_head))) {
			dev_err(&GET_DEV(accel_dev),
				"failed to copy section info\n");
			goto out_err;
		}

		if (adf_cfg_section_add(accel_dev, section.name)) {
			dev_err(&GET_DEV(accel_dev),
				"failed to add section.\n");
			goto out_err;
		}

		params_head = section.params;

		for (j = 0; params_head && j < ADF_CFG_MAX_KEY_VAL; j++) {
			if (copy_from_user(&key_val, (void __user *)params_head,
					   sizeof(key_val))) {
				dev_err(&GET_DEV(accel_dev),
					"Failed to copy keyvalue.\n");
				goto out_err;
			}
			if (adf_add_key_value_data(accel_dev, section.name,
						   &key_val)) {
				goto out_err;
			}
			params_head = key_val.next;
		}
		section_head = section.next;
	}
	return 0;
out_err:
	adf_cfg_del_all(accel_dev);
	return -EFAULT;
}

static int adf_ctl_ioctl_dev_config(struct file *fp, unsigned int cmd,
				    unsigned long arg)
{
	int ret;
	struct adf_user_cfg_ctl_data *ctl_data;
	struct adf_accel_dev *accel_dev;

	ret = adf_ctl_alloc_resources(&ctl_data, arg);
	if (ret)
		return ret;

	accel_dev = adf_devmgr_get_dev_by_id(ctl_data->device_id);
	if (!accel_dev) {
		ret = -EFAULT;
		goto out;
	}

	if (adf_dev_started(accel_dev)) {
		ret = -EFAULT;
		goto out;
	}

	if (adf_copy_key_value_data(accel_dev, ctl_data)) {
		ret = -EFAULT;
		goto out;
	}
	set_bit(ADF_STATUS_CONFIGURED, &accel_dev->status);
out:
	kfree(ctl_data);
	return ret;
}

static int adf_ctl_is_device_in_use(int id)
{
	struct adf_accel_dev *dev;

	list_for_each_entry(dev, adf_devmgr_get_head(), list) {
		if (id == dev->accel_id || id == ADF_CFG_ALL_DEVICES) {
			if (adf_devmgr_in_reset(dev) || adf_dev_in_use(dev)) {
				dev_info(&GET_DEV(dev),
					 "device qat_dev%d is busy\n",
					 dev->accel_id);
				return -EBUSY;
			}
		}
	}
	return 0;
}

static void adf_ctl_stop_devices(u32 id)
{
	struct adf_accel_dev *accel_dev;

	list_for_each_entry(accel_dev, adf_devmgr_get_head(), list) {
		if (id == accel_dev->accel_id || id == ADF_CFG_ALL_DEVICES) {
			if (!adf_dev_started(accel_dev))
				continue;

			/* First stop all VFs */
			if (!accel_dev->is_vf)
				continue;

			adf_dev_down(accel_dev);
		}
	}

	list_for_each_entry(accel_dev, adf_devmgr_get_head(), list) {
		if (id == accel_dev->accel_id || id == ADF_CFG_ALL_DEVICES) {
			if (!adf_dev_started(accel_dev))
				continue;

			adf_dev_down(accel_dev);
		}
	}
}

static int adf_ctl_ioctl_dev_stop(struct file *fp, unsigned int cmd,
				  unsigned long arg)
{
	int ret;
	struct adf_user_cfg_ctl_data *ctl_data;

	ret = adf_ctl_alloc_resources(&ctl_data, arg);
	if (ret)
		return ret;

	if (adf_devmgr_verify_id(ctl_data->device_id)) {
		pr_err("QAT: Device %d not found\n", ctl_data->device_id);
		ret = -ENODEV;
		goto out;
	}

	ret = adf_ctl_is_device_in_use(ctl_data->device_id);
	if (ret)
		goto out;

	if (ctl_data->device_id == ADF_CFG_ALL_DEVICES)
		pr_info("QAT: Stopping all acceleration devices.\n");
	else
		pr_info("QAT: Stopping acceleration device qat_dev%d.\n",
			ctl_data->device_id);

	adf_ctl_stop_devices(ctl_data->device_id);

out:
	kfree(ctl_data);
	return ret;
}

static int adf_ctl_ioctl_dev_start(struct file *fp, unsigned int cmd,
				   unsigned long arg)
{
	int ret;
	struct adf_user_cfg_ctl_data *ctl_data;
	struct adf_accel_dev *accel_dev;

	ret = adf_ctl_alloc_resources(&ctl_data, arg);
	if (ret)
		return ret;

	ret = -ENODEV;
	accel_dev = adf_devmgr_get_dev_by_id(ctl_data->device_id);
	if (!accel_dev)
		goto out;

	dev_info(&GET_DEV(accel_dev),
		 "Starting acceleration device qat_dev%d.\n",
		 ctl_data->device_id);

	ret = adf_dev_up(accel_dev, false);

	if (ret) {
		dev_err(&GET_DEV(accel_dev), "Failed to start qat_dev%d\n",
			ctl_data->device_id);
		adf_dev_down(accel_dev);
	}
out:
	kfree(ctl_data);
	return ret;
}

static int adf_ctl_ioctl_get_num_devices(struct file *fp, unsigned int cmd,
					 unsigned long arg)
{
	u32 num_devices = 0;

	adf_devmgr_get_num_dev(&num_devices);
	if (copy_to_user((void __user *)arg, &num_devices, sizeof(num_devices)))
		return -EFAULT;

	return 0;
}

static int adf_ctl_ioctl_get_status(struct file *fp, unsigned int cmd,
				    unsigned long arg)
{
	struct adf_hw_device_data *hw_data;
	struct adf_dev_status_info dev_info;
	struct adf_accel_dev *accel_dev;

	if (copy_from_user(&dev_info, (void __user *)arg,
			   sizeof(struct adf_dev_status_info))) {
		pr_err("QAT: failed to copy from user.\n");
		return -EFAULT;
	}

	accel_dev = adf_devmgr_get_dev_by_id(dev_info.accel_id);
	if (!accel_dev)
		return -ENODEV;

	hw_data = accel_dev->hw_device;
	dev_info.state = adf_dev_started(accel_dev) ? DEV_UP : DEV_DOWN;
	dev_info.num_ae = hw_data->get_num_aes(hw_data);
	dev_info.num_accel = hw_data->get_num_accels(hw_data);
	dev_info.num_logical_accel = hw_data->num_logical_accel;
	dev_info.banks_per_accel = hw_data->num_banks
					/ hw_data->num_logical_accel;
	strscpy(dev_info.name, hw_data->dev_class->name, sizeof(dev_info.name));
	dev_info.instance_id = hw_data->instance_id;
	dev_info.type = hw_data->dev_class->type;
	dev_info.bus = accel_to_pci_dev(accel_dev)->bus->number;
	dev_info.dev = PCI_SLOT(accel_to_pci_dev(accel_dev)->devfn);
	dev_info.fun = PCI_FUNC(accel_to_pci_dev(accel_dev)->devfn);

	if (copy_to_user((void __user *)arg, &dev_info,
			 sizeof(struct adf_dev_status_info))) {
		dev_err(&GET_DEV(accel_dev), "failed to copy status.\n");
		return -EFAULT;
	}
	return 0;
}

static long adf_ctl_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	int ret;

	if (mutex_lock_interruptible(&adf_ctl_lock))
		return -EFAULT;

	switch (cmd) {
	case IOCTL_CONFIG_SYS_RESOURCE_PARAMETERS:
		ret = adf_ctl_ioctl_dev_config(fp, cmd, arg);
		break;

	case IOCTL_STOP_ACCEL_DEV:
		ret = adf_ctl_ioctl_dev_stop(fp, cmd, arg);
		break;

	case IOCTL_START_ACCEL_DEV:
		ret = adf_ctl_ioctl_dev_start(fp, cmd, arg);
		break;

	case IOCTL_GET_NUM_DEVICES:
		ret = adf_ctl_ioctl_get_num_devices(fp, cmd, arg);
		break;

	case IOCTL_STATUS_ACCEL_DEV:
		ret = adf_ctl_ioctl_get_status(fp, cmd, arg);
		break;
	default:
		pr_err_ratelimited("QAT: Invalid ioctl %d\n", cmd);
		ret = -EFAULT;
		break;
	}
	mutex_unlock(&adf_ctl_lock);
	return ret;
}

static int __init adf_register_ctl_device_driver(void)
{
	if (adf_chr_drv_create())
		goto err_chr_dev;

	if (adf_init_misc_wq())
		goto err_misc_wq;

	if (adf_init_aer())
		goto err_aer;

	if (adf_init_pf_wq())
		goto err_pf_wq;

	if (adf_init_vf_wq())
		goto err_vf_wq;

	if (qat_crypto_register())
		goto err_crypto_register;

	if (qat_compression_register())
		goto err_compression_register;

	return 0;

err_compression_register:
	qat_crypto_unregister();
err_crypto_register:
	adf_exit_vf_wq();
err_vf_wq:
	adf_exit_pf_wq();
err_pf_wq:
	adf_exit_aer();
err_aer:
	adf_exit_misc_wq();
err_misc_wq:
	adf_chr_drv_destroy();
err_chr_dev:
	mutex_destroy(&adf_ctl_lock);
	return -EFAULT;
}

static void __exit adf_unregister_ctl_device_driver(void)
{
	adf_chr_drv_destroy();
	adf_exit_misc_wq();
	adf_exit_aer();
	adf_exit_vf_wq();
	adf_exit_pf_wq();
	qat_crypto_unregister();
	qat_compression_unregister();
	adf_clean_vf_map(false);
	mutex_destroy(&adf_ctl_lock);
}

module_init(adf_register_ctl_device_driver);
module_exit(adf_unregister_ctl_device_driver);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel");
MODULE_DESCRIPTION("Intel(R) QuickAssist Technology");
MODULE_ALIAS_CRYPTO("intel_qat");
MODULE_VERSION(ADF_DRV_VERSION);
MODULE_IMPORT_NS("CRYPTO_INTERNAL");
