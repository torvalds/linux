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
#include <linux/tpm_eventlog.h>
#include <linux/hw_random.h>
#include "tpm.h"

DEFINE_IDR(dev_nums_idr);
static DEFINE_MUTEX(idr_lock);

struct class *tpm_class;
struct class *tpmrm_class;
dev_t tpm_devt;

/**
 * tpm_try_get_ops() - Get a ref to the tpm_chip
 * @chip: Chip to ref
 *
 * The caller must already have some kind of locking to ensure that chip is
 * valid. This function will lock the chip so that the ops member can be
 * accessed safely. The locking prevents tpm_chip_unregister from
 * completing, so it should not be held for long periods.
 *
 * Returns -ERRNO if the chip could not be got.
 */
int tpm_try_get_ops(struct tpm_chip *chip)
{
	int rc = -EIO;

	get_device(&chip->dev);

	down_read(&chip->ops_sem);
	if (!chip->ops)
		goto out_lock;

	return 0;
out_lock:
	up_read(&chip->ops_sem);
	put_device(&chip->dev);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_try_get_ops);

/**
 * tpm_put_ops() - Release a ref to the tpm_chip
 * @chip: Chip to put
 *
 * This is the opposite pair to tpm_try_get_ops(). After this returns chip may
 * be kfree'd.
 */
void tpm_put_ops(struct tpm_chip *chip)
{
	up_read(&chip->ops_sem);
	put_device(&chip->dev);
}
EXPORT_SYMBOL_GPL(tpm_put_ops);

/**
 * tpm_default_chip() - find a TPM chip and get a reference to it
 */
struct tpm_chip *tpm_default_chip(void)
{
	struct tpm_chip *chip, *res = NULL;
	int chip_num = 0;
	int chip_prev;

	mutex_lock(&idr_lock);

	do {
		chip_prev = chip_num;
		chip = idr_get_next(&dev_nums_idr, &chip_num);
		if (chip) {
			get_device(&chip->dev);
			res = chip;
			break;
		}
	} while (chip_prev != chip_num);

	mutex_unlock(&idr_lock);

	return res;
}
EXPORT_SYMBOL_GPL(tpm_default_chip);

/**
 * tpm_find_get_ops() - find and reserve a TPM chip
 * @chip:	a &struct tpm_chip instance, %NULL for the default chip
 *
 * Finds a TPM chip and reserves its class device and operations. The chip must
 * be released with tpm_put_ops() after use.
 * This function is for internal use only. It supports existing TPM callers
 * by accepting NULL, but those callers should be converted to pass in a chip
 * directly.
 *
 * Return:
 * A reserved &struct tpm_chip instance.
 * %NULL if a chip is not found.
 * %NULL if the chip is not available.
 */
struct tpm_chip *tpm_find_get_ops(struct tpm_chip *chip)
{
	int rc;

	if (chip) {
		if (!tpm_try_get_ops(chip))
			return chip;
		return NULL;
	}

	chip = tpm_default_chip();
	if (!chip)
		return NULL;
	rc = tpm_try_get_ops(chip);
	/* release additional reference we got from tpm_default_chip() */
	put_device(&chip->dev);
	if (rc)
		return NULL;
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

	mutex_lock(&idr_lock);
	idr_remove(&dev_nums_idr, chip->dev_num);
	mutex_unlock(&idr_lock);

	kfree(chip->log.bios_event_log);
	kfree(chip->work_space.context_buf);
	kfree(chip->work_space.session_buf);
	kfree(chip);
}

static void tpm_devs_release(struct device *dev)
{
	struct tpm_chip *chip = container_of(dev, struct tpm_chip, devs);

	/* release the master device reference */
	put_device(&chip->dev);
}

/**
 * tpm_class_shutdown() - prepare the TPM device for loss of power.
 * @dev: device to which the chip is associated.
 *
 * Issues a TPM2_Shutdown command prior to loss of power, as required by the
 * TPM 2.0 spec.
 * Then, calls bus- and device- specific shutdown code.
 *
 * XXX: This codepath relies on the fact that sysfs is not enabled for
 * TPM2: sysfs uses an implicit lock on chip->ops, so this could race if TPM2
 * has sysfs support enabled before TPM sysfs's implicit locking is fixed.
 */
static int tpm_class_shutdown(struct device *dev)
{
	struct tpm_chip *chip = container_of(dev, struct tpm_chip, dev);

	down_write(&chip->ops_sem);
	if (chip->flags & TPM_CHIP_FLAG_TPM2) {
		tpm2_shutdown(chip, TPM2_SU_CLEAR);
		chip->ops = NULL;
	}
	chip->ops = NULL;
	up_write(&chip->ops_sem);

	return 0;
}

/**
 * tpm_chip_alloc() - allocate a new struct tpm_chip instance
 * @pdev: device to which the chip is associated
 *        At this point pdev mst be initialized, but does not have to
 *        be registered
 * @ops: struct tpm_class_ops instance
 *
 * Allocates a new struct tpm_chip instance and assigns a free
 * device number for it. Must be paired with put_device(&chip->dev).
 */
struct tpm_chip *tpm_chip_alloc(struct device *pdev,
				const struct tpm_class_ops *ops)
{
	struct tpm_chip *chip;
	int rc;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return ERR_PTR(-ENOMEM);

	mutex_init(&chip->tpm_mutex);
	init_rwsem(&chip->ops_sem);

	chip->ops = ops;

	mutex_lock(&idr_lock);
	rc = idr_alloc(&dev_nums_idr, NULL, 0, TPM_NUM_DEVICES, GFP_KERNEL);
	mutex_unlock(&idr_lock);
	if (rc < 0) {
		dev_err(pdev, "No available tpm device numbers\n");
		kfree(chip);
		return ERR_PTR(rc);
	}
	chip->dev_num = rc;

	device_initialize(&chip->dev);
	device_initialize(&chip->devs);

	chip->dev.class = tpm_class;
	chip->dev.class->shutdown_pre = tpm_class_shutdown;
	chip->dev.release = tpm_dev_release;
	chip->dev.parent = pdev;
	chip->dev.groups = chip->groups;

	chip->devs.parent = pdev;
	chip->devs.class = tpmrm_class;
	chip->devs.release = tpm_devs_release;
	/* get extra reference on main device to hold on
	 * behalf of devs.  This holds the chip structure
	 * while cdevs is in use.  The corresponding put
	 * is in the tpm_devs_release (TPM2 only)
	 */
	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		get_device(&chip->dev);

	if (chip->dev_num == 0)
		chip->dev.devt = MKDEV(MISC_MAJOR, TPM_MINOR);
	else
		chip->dev.devt = MKDEV(MAJOR(tpm_devt), chip->dev_num);

	chip->devs.devt =
		MKDEV(MAJOR(tpm_devt), chip->dev_num + TPM_NUM_DEVICES);

	rc = dev_set_name(&chip->dev, "tpm%d", chip->dev_num);
	if (rc)
		goto out;
	rc = dev_set_name(&chip->devs, "tpmrm%d", chip->dev_num);
	if (rc)
		goto out;

	if (!pdev)
		chip->flags |= TPM_CHIP_FLAG_VIRTUAL;

	cdev_init(&chip->cdev, &tpm_fops);
	cdev_init(&chip->cdevs, &tpmrm_fops);
	chip->cdev.owner = THIS_MODULE;
	chip->cdevs.owner = THIS_MODULE;

	chip->work_space.context_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!chip->work_space.context_buf) {
		rc = -ENOMEM;
		goto out;
	}
	chip->work_space.session_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!chip->work_space.session_buf) {
		rc = -ENOMEM;
		goto out;
	}

	chip->locality = -1;
	return chip;

out:
	put_device(&chip->devs);
	put_device(&chip->dev);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(tpm_chip_alloc);

/**
 * tpmm_chip_alloc() - allocate a new struct tpm_chip instance
 * @pdev: parent device to which the chip is associated
 * @ops: struct tpm_class_ops instance
 *
 * Same as tpm_chip_alloc except devm is used to do the put_device
 */
struct tpm_chip *tpmm_chip_alloc(struct device *pdev,
				 const struct tpm_class_ops *ops)
{
	struct tpm_chip *chip;
	int rc;

	chip = tpm_chip_alloc(pdev, ops);
	if (IS_ERR(chip))
		return chip;

	rc = devm_add_action_or_reset(pdev,
				      (void (*)(void *)) put_device,
				      &chip->dev);
	if (rc)
		return ERR_PTR(rc);

	dev_set_drvdata(pdev, chip);

	return chip;
}
EXPORT_SYMBOL_GPL(tpmm_chip_alloc);

static int tpm_add_char_device(struct tpm_chip *chip)
{
	int rc;

	rc = cdev_device_add(&chip->cdev, &chip->dev);
	if (rc) {
		dev_err(&chip->dev,
			"unable to cdev_device_add() %s, major %d, minor %d, err=%d\n",
			dev_name(&chip->dev), MAJOR(chip->dev.devt),
			MINOR(chip->dev.devt), rc);
		return rc;
	}

	if (chip->flags & TPM_CHIP_FLAG_TPM2) {
		rc = cdev_device_add(&chip->cdevs, &chip->devs);
		if (rc) {
			dev_err(&chip->devs,
				"unable to cdev_device_add() %s, major %d, minor %d, err=%d\n",
				dev_name(&chip->devs), MAJOR(chip->devs.devt),
				MINOR(chip->devs.devt), rc);
			return rc;
		}
	}

	/* Make the chip available. */
	mutex_lock(&idr_lock);
	idr_replace(&dev_nums_idr, chip, chip->dev_num);
	mutex_unlock(&idr_lock);

	return rc;
}

static void tpm_del_char_device(struct tpm_chip *chip)
{
	cdev_device_del(&chip->cdev, &chip->dev);

	/* Make the chip unavailable. */
	mutex_lock(&idr_lock);
	idr_replace(&dev_nums_idr, NULL, chip->dev_num);
	mutex_unlock(&idr_lock);

	/* Make the driver uncallable. */
	down_write(&chip->ops_sem);
	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		tpm2_shutdown(chip, TPM2_SU_CLEAR);
	chip->ops = NULL;
	up_write(&chip->ops_sem);
}

static void tpm_del_legacy_sysfs(struct tpm_chip *chip)
{
	struct attribute **i;

	if (chip->flags & (TPM_CHIP_FLAG_TPM2 | TPM_CHIP_FLAG_VIRTUAL))
		return;

	sysfs_remove_link(&chip->dev.parent->kobj, "ppi");

	for (i = chip->groups[0]->attrs; *i != NULL; ++i)
		sysfs_remove_link(&chip->dev.parent->kobj, (*i)->name);
}

/* For compatibility with legacy sysfs paths we provide symlinks from the
 * parent dev directory to selected names within the tpm chip directory. Old
 * kernel versions created these files directly under the parent.
 */
static int tpm_add_legacy_sysfs(struct tpm_chip *chip)
{
	struct attribute **i;
	int rc;

	if (chip->flags & (TPM_CHIP_FLAG_TPM2 | TPM_CHIP_FLAG_VIRTUAL))
		return 0;

	rc = __compat_only_sysfs_link_entry_to_kobj(
		    &chip->dev.parent->kobj, &chip->dev.kobj, "ppi");
	if (rc && rc != -ENOENT)
		return rc;

	/* All the names from tpm-sysfs */
	for (i = chip->groups[0]->attrs; *i != NULL; ++i) {
		rc = __compat_only_sysfs_link_entry_to_kobj(
		    &chip->dev.parent->kobj, &chip->dev.kobj, (*i)->name);
		if (rc) {
			tpm_del_legacy_sysfs(chip);
			return rc;
		}
	}

	return 0;
}

static int tpm_hwrng_read(struct hwrng *rng, void *data, size_t max, bool wait)
{
	struct tpm_chip *chip = container_of(rng, struct tpm_chip, hwrng);

	return tpm_get_random(chip, data, max);
}

static int tpm_add_hwrng(struct tpm_chip *chip)
{
	if (!IS_ENABLED(CONFIG_HW_RANDOM_TPM))
		return 0;

	snprintf(chip->hwrng_name, sizeof(chip->hwrng_name),
		 "tpm-rng-%d", chip->dev_num);
	chip->hwrng.name = chip->hwrng_name;
	chip->hwrng.read = tpm_hwrng_read;
	return hwrng_register(&chip->hwrng);
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

	if (chip->ops->flags & TPM_OPS_AUTO_STARTUP) {
		if (chip->flags & TPM_CHIP_FLAG_TPM2)
			rc = tpm2_auto_startup(chip);
		else
			rc = tpm1_auto_startup(chip);
		if (rc)
			return rc;
	}

	tpm_sysfs_add_device(chip);

	rc = tpm_bios_log_setup(chip);
	if (rc != 0 && rc != -ENODEV)
		return rc;

	tpm_add_ppi(chip);

	rc = tpm_add_hwrng(chip);
	if (rc)
		goto out_ppi;

	rc = tpm_add_char_device(chip);
	if (rc)
		goto out_hwrng;

	rc = tpm_add_legacy_sysfs(chip);
	if (rc) {
		tpm_chip_unregister(chip);
		return rc;
	}

	return 0;

out_hwrng:
	if (IS_ENABLED(CONFIG_HW_RANDOM_TPM))
		hwrng_unregister(&chip->hwrng);
out_ppi:
	tpm_bios_log_teardown(chip);

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
 * Once this function returns the driver call backs in 'op's will not be
 * running and will no longer start.
 *
 * NOTE: This function should be only called before deinitializing chip
 * resources.
 */
void tpm_chip_unregister(struct tpm_chip *chip)
{
	tpm_del_legacy_sysfs(chip);
	if (IS_ENABLED(CONFIG_HW_RANDOM_TPM))
		hwrng_unregister(&chip->hwrng);
	tpm_bios_log_teardown(chip);
	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		cdev_device_del(&chip->cdevs, &chip->devs);
	tpm_del_char_device(chip);
}
EXPORT_SYMBOL_GPL(tpm_chip_unregister);
