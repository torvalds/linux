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
#include "tpm.h"
#include "tpm_eventlog.h"

static DECLARE_BITMAP(dev_mask, TPM_NUM_DEVICES);
static LIST_HEAD(tpm_chip_list);
static DEFINE_SPINLOCK(driver_lock);

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

		if (try_module_get(pos->dev->driver->owner)) {
			chip = pos;
			break;
		}
	}
	rcu_read_unlock();
	return chip;
}

/**
 * tpmm_chip_remove() - free chip memory and device number
 * @data: points to struct tpm_chip instance
 *
 * This is used internally by tpmm_chip_alloc() and called by devres
 * when the device is released. This function does the opposite of
 * tpmm_chip_alloc() freeing memory and the device number.
 */
static void tpmm_chip_remove(void *data)
{
	struct tpm_chip *chip = (struct tpm_chip *) data;

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

	chip->dev = dev;
	devm_add_action(dev, tpmm_chip_remove, chip);
	dev_set_drvdata(dev, chip);

	return chip;
}
EXPORT_SYMBOL_GPL(tpmm_chip_alloc);

/*
 * tpm_chip_register() - create a misc driver for the TPM chip
 * @chip: TPM chip to use.
 *
 * Creates a misc driver for the TPM chip and adds sysfs interfaces for
 * the device, PPI and TCPA. As the last step this function adds the
 * chip to the list of TPM chips available for use.
 *
 * NOTE: This function should be only called after the chip initialization
 * is complete.
 *
 * Called from tpm_<specific>.c probe function only for devices
 * the driver has determined it should claim.  Prior to calling
 * this function the specific probe function has called pci_enable_device
 * upon errant exit from this function specific probe function should call
 * pci_disable_device
 */
int tpm_chip_register(struct tpm_chip *chip)
{
	int rc;

	rc = tpm_dev_add_device(chip);
	if (rc)
		return rc;

	rc = tpm_sysfs_add_device(chip);
	if (rc)
		goto del_misc;

	rc = tpm_add_ppi(&chip->dev->kobj);
	if (rc)
		goto del_sysfs;

	chip->bios_dir = tpm_bios_log_setup(chip->devname);

	/* Make the chip available. */
	spin_lock(&driver_lock);
	list_add_rcu(&chip->list, &tpm_chip_list);
	spin_unlock(&driver_lock);

	chip->flags |= TPM_CHIP_FLAG_REGISTERED;

	return 0;
del_sysfs:
	tpm_sysfs_del_device(chip);
del_misc:
	tpm_dev_del_device(chip);
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

	if (chip->bios_dir)
		tpm_bios_log_teardown(chip->bios_dir);
	tpm_remove_ppi(&chip->dev->kobj);
	tpm_sysfs_del_device(chip);

	tpm_dev_del_device(chip);
}
EXPORT_SYMBOL_GPL(tpm_chip_unregister);
