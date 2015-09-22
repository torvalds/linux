/*
 * Copyright (C) 2004 IBM Corporation
 * Copyright (C) 2014 Intel Corporation
 *
 * Authors:
 * Jarkko Sakkinen <jarkko.sakkinen@linux.intel.com>
 * Leendert van Doorn <leendert@watson.ibm.com>
 * Dave Safford <safford@watson.ibm.com>
 * Reiner Sailer <sailer@watson.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * TPM chip management routines.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 */

#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/freezer.h>
#include <linux/major.h>
#include "tpm.h"
#include "tpm_eventlog.h"

static DECLARE_BITMAP(dev_mask, TPM_NUM_DEVICES);
static LIST_HEAD(tpm_chip_list);
static DEFINE_SPINLOCK(driver_lock);

struct class *tpm_class;
dev_t tpm_devt;

/*
 * tpm_chip_find_get - return tpm_chip for a given chip number
 * @chip_num the device number for the chip
 */
struct tpm_chip *tpm_chip_find_get(int chip_num)
{
	struct tpm_chip *pos, *chip = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(pos, &tpm_chip_list, list) {
		if (chip_num != TPM_ANY_NUM && chip_num != pos->dev_num)
			continue;

		if (try_module_get(pos->pdev->driver->owner)) {
			chip = pos;
			break;
		}
	}
	rcu_read_unlock();
	return chip;
}

/**
 * tpm_dev_release() - free chip memory and the device number
 * @dev: the character device for the TPM chip
 *
 * This is used as the release function for the character device.
 */
static void tpm_dev_release(struct device *dev)
{
	struct tpm_chip *chip = container_of(dev, struct tpm_chip, dev);

	spin_lock(&driver_lock);
	clear_bit(chip->dev_num, dev_mask);
	spin_unlock(&driver_lock);
	kfree(chip);
}

/**
 * tpmm_chip_alloc() - allocate a new struct tpm_chip instance
 * @dev: device to which the chip is associated
 * @ops: struct tpm_class_ops instance
 *
 * Allocates a new struct tpm_chip instance and assigns a free
 * device number for it. Caller does not have to worry about
 * freeing the allocated resources. When the devices is removed
 * devres calls tpmm_chip_remove() to do the job.
 */
struct tpm_chip *tpmm_chip_alloc(struct device *dev,
				 const struct tpm_class_ops *ops)
{
	struct tpm_chip *chip;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return ERR_PTR(-ENOMEM);

	mutex_init(&chip->tpm_mutex);
	INIT_LIST_HEAD(&chip->list);

	chip->ops = ops;

	spin_lock(&driver_lock);
	chip->dev_num = find_first_zero_bit(dev_mask, TPM_NUM_DEVICES);
	spin_unlock(&driver_lock);

	if (chip->dev_num >= TPM_NUM_DEVICES) {
		dev_err(dev, "No available tpm device numbers\n");
		kfree(chip);
		return ERR_PTR(-ENOMEM);
	}

	set_bit(chip->dev_num, dev_mask);

	scnprintf(chip->devname, sizeof(chip->devname), "tpm%d", chip->dev_num);

	chip->pdev = dev;

	dev_set_drvdata(dev, chip);

	chip->dev.class = tpm_class;
	chip->dev.release = tpm_dev_release;
	chip->dev.parent = chip->pdev;

	if (chip->dev_num == 0)
		chip->dev.devt = MKDEV(MISC_MAJOR, TPM_MINOR);
	else
		chip->dev.devt = MKDEV(MAJOR(tpm_devt), chip->dev_num);

	dev_set_name(&chip->dev, "%s", chip->devname);

	device_initialize(&chip->dev);

	cdev_init(&chip->cdev, &tpm_fops);
	chip->cdev.owner = chip->pdev->driver->owner;
	chip->cdev.kobj.parent = &chip->dev.kobj;

	return chip;
}
EXPORT_SYMBOL_GPL(tpmm_chip_alloc);

static int tpm_dev_add_device(struct tpm_chip *chip)
{
	int rc;

	rc = cdev_add(&chip->cdev, chip->dev.devt, 1);
	if (rc) {
		dev_err(&chip->dev,
			"unable to cdev_add() %s, major %d, minor %d, err=%d\n",
			chip->devname, MAJOR(chip->dev.devt),
			MINOR(chip->dev.devt), rc);

		device_unregister(&chip->dev);
		return rc;
	}

	rc = device_add(&chip->dev);
	if (rc) {
		dev_err(&chip->dev,
			"unable to device_register() %s, major %d, minor %d, err=%d\n",
			chip->devname, MAJOR(chip->dev.devt),
			MINOR(chip->dev.devt), rc);

		return rc;
	}

	return rc;
}

static void tpm_dev_del_device(struct tpm_chip *chip)
{
	cdev_del(&chip->cdev);
	device_unregister(&chip->dev);
}

static int tpm1_chip_register(struct tpm_chip *chip)
{
	int rc;

	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		return 0;

	rc = tpm_sysfs_add_device(chip);
	if (rc)
		return rc;

	rc = tpm_add_ppi(chip);
	if (rc) {
		tpm_sysfs_del_device(chip);
		return rc;
	}

	chip->bios_dir = tpm_bios_log_setup(chip->devname);

	return 0;
}

static void tpm1_chip_unregister(struct tpm_chip *chip)
{
	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		return;

	if (chip->bios_dir)
		tpm_bios_log_teardown(chip->bios_dir);

	tpm_remove_ppi(chip);

	tpm_sysfs_del_device(chip);
}

/*
 * tpm_chip_register() - create a character device for the TPM chip
 * @chip: TPM chip to use.
 *
 * Creates a character device for the TPM chip and adds sysfs attributes for
 * the device. As the last step this function adds the chip to the list of TPM
 * chips available for in-kernel use.
 *
 * This function should be only called after the chip initialization is
 * complete.
 */
int tpm_chip_register(struct tpm_chip *chip)
{
	int rc;

	rc = tpm1_chip_register(chip);
	if (rc)
		return rc;

	rc = tpm_dev_add_device(chip);
	if (rc)
		goto out_err;

	/* Make the chip available. */
	spin_lock(&driver_lock);
	list_add_rcu(&chip->list, &tpm_chip_list);
	spin_unlock(&driver_lock);

	chip->flags |= TPM_CHIP_FLAG_REGISTERED;

	return 0;
out_err:
	tpm1_chip_unregister(chip);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_chip_register);

/*
 * tpm_chip_unregister() - release the TPM driver
 * @chip: TPM chip to use.
 *
 * Takes the chip first away from the list of available TPM chips and then
 * cleans up all the resources reserved by tpm_chip_register().
 *
 * NOTE: This function should be only called before deinitializing chip
 * resources.
 */
void tpm_chip_unregister(struct tpm_chip *chip)
{
	if (!(chip->flags & TPM_CHIP_FLAG_REGISTERED))
		return;

	spin_lock(&driver_lock);
	list_del_rcu(&chip->list);
	spin_unlock(&driver_lock);
	synchronize_rcu();

	tpm1_chip_unregister(chip);
	tpm_dev_del_device(chip);
}
EXPORT_SYMBOL_GPL(tpm_chip_unregister);
