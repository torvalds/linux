// SPDX-License-Identifier: GPL-2.0-only
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

const struct class tpm_class = {
	.name = "tpm",
	.shutdown_pre = tpm_class_shutdown,
};
const struct class tpmrm_class = {
	.name = "tpmrm",
};
dev_t tpm_devt;

static int tpm_request_locality(struct tpm_chip *chip)
{
	int rc;

	if (!chip->ops->request_locality)
		return 0;

	rc = chip->ops->request_locality(chip, 0);
	if (rc < 0)
		return rc;

	chip->locality = rc;
	return 0;
}

static void tpm_relinquish_locality(struct tpm_chip *chip)
{
	int rc;

	if (!chip->ops->relinquish_locality)
		return;

	rc = chip->ops->relinquish_locality(chip, chip->locality);
	if (rc)
		dev_err(&chip->dev, "%s: : error %d\n", __func__, rc);

	chip->locality = -1;
}

static int tpm_cmd_ready(struct tpm_chip *chip)
{
	if (!chip->ops->cmd_ready)
		return 0;

	return chip->ops->cmd_ready(chip);
}

static int tpm_go_idle(struct tpm_chip *chip)
{
	if (!chip->ops->go_idle)
		return 0;

	return chip->ops->go_idle(chip);
}

static void tpm_clk_enable(struct tpm_chip *chip)
{
	if (chip->ops->clk_enable)
		chip->ops->clk_enable(chip, true);
}

static void tpm_clk_disable(struct tpm_chip *chip)
{
	if (chip->ops->clk_enable)
		chip->ops->clk_enable(chip, false);
}

/**
 * tpm_chip_start() - power on the TPM
 * @chip:	a TPM chip to use
 *
 * Return:
 * * The response length	- OK
 * * -errno			- A system error
 */
int tpm_chip_start(struct tpm_chip *chip)
{
	int ret;

	tpm_clk_enable(chip);

	if (chip->locality == -1) {
		ret = tpm_request_locality(chip);
		if (ret) {
			tpm_clk_disable(chip);
			return ret;
		}
	}

	ret = tpm_cmd_ready(chip);
	if (ret) {
		tpm_relinquish_locality(chip);
		tpm_clk_disable(chip);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(tpm_chip_start);

/**
 * tpm_chip_stop() - power off the TPM
 * @chip:	a TPM chip to use
 *
 * Return:
 * * The response length	- OK
 * * -errno			- A system error
 */
void tpm_chip_stop(struct tpm_chip *chip)
{
	tpm_go_idle(chip);
	tpm_relinquish_locality(chip);
	tpm_clk_disable(chip);
}
EXPORT_SYMBOL_GPL(tpm_chip_stop);

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

	if (chip->flags & TPM_CHIP_FLAG_DISABLE)
		return rc;

	get_device(&chip->dev);

	down_read(&chip->ops_sem);
	if (!chip->ops)
		goto out_ops;

	mutex_lock(&chip->tpm_mutex);

	/* tmp_chip_start may issue IO that is denied while suspended */
	if (chip->flags & TPM_CHIP_FLAG_SUSPENDED)
		goto out_lock;

	rc = tpm_chip_start(chip);
	if (rc)
		goto out_lock;

	return 0;
out_lock:
	mutex_unlock(&chip->tpm_mutex);
out_ops:
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
	tpm_chip_stop(chip);
	mutex_unlock(&chip->tpm_mutex);
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

	kfree(chip->work_space.context_buf);
	kfree(chip->work_space.session_buf);
	kfree(chip->allocated_banks);
#ifdef CONFIG_TCG_TPM2_HMAC
	kfree(chip->auth);
#endif
	kfree(chip);
}

/**
 * tpm_class_shutdown() - prepare the TPM device for loss of power.
 * @dev: device to which the chip is associated.
 *
 * Issues a TPM2_Shutdown command prior to loss of power, as required by the
 * TPM 2.0 spec. Then, calls bus- and device- specific shutdown code.
 *
 * Return: always 0 (i.e. success)
 */
int tpm_class_shutdown(struct device *dev)
{
	struct tpm_chip *chip = container_of(dev, struct tpm_chip, dev);

	down_write(&chip->ops_sem);
	if (chip->flags & TPM_CHIP_FLAG_TPM2) {
		if (!tpm_chip_start(chip)) {
			tpm2_end_auth_session(chip);
			tpm2_shutdown(chip, TPM2_SU_CLEAR);
			tpm_chip_stop(chip);
		}
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

	chip->dev.class = &tpm_class;
	chip->dev.release = tpm_dev_release;
	chip->dev.parent = pdev;
	chip->dev.groups = chip->groups;

	if (chip->dev_num == 0)
		chip->dev.devt = MKDEV(MISC_MAJOR, TPM_MINOR);
	else
		chip->dev.devt = MKDEV(MAJOR(tpm_devt), chip->dev_num);

	rc = dev_set_name(&chip->dev, "tpm%d", chip->dev_num);
	if (rc)
		goto out;

	if (!pdev)
		chip->flags |= TPM_CHIP_FLAG_VIRTUAL;

	cdev_init(&chip->cdev, &tpm_fops);
	chip->cdev.owner = THIS_MODULE;

	rc = tpm2_init_space(&chip->work_space, TPM2_SPACE_BUFFER_SIZE);
	if (rc) {
		rc = -ENOMEM;
		goto out;
	}

	chip->locality = -1;
	return chip;

out:
	put_device(&chip->dev);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(tpm_chip_alloc);

static void tpm_put_device(void *dev)
{
	put_device(dev);
}

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
				      tpm_put_device,
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

	if (chip->flags & TPM_CHIP_FLAG_TPM2 && !tpm_is_firmware_upgrade(chip)) {
		rc = tpm_devs_add(chip);
		if (rc)
			goto err_del_cdev;
	}

	/* Make the chip available. */
	mutex_lock(&idr_lock);
	idr_replace(&dev_nums_idr, chip, chip->dev_num);
	mutex_unlock(&idr_lock);

	return 0;

err_del_cdev:
	cdev_device_del(&chip->cdev, &chip->dev);
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

	/*
	 * Check if chip->ops is still valid: In case that the controller
	 * drivers shutdown handler unregisters the controller in its
	 * shutdown handler we are called twice and chip->ops to NULL.
	 */
	if (chip->ops) {
		if (chip->flags & TPM_CHIP_FLAG_TPM2) {
			if (!tpm_chip_start(chip)) {
				tpm2_shutdown(chip, TPM2_SU_CLEAR);
				tpm_chip_stop(chip);
			}
		}
		chip->ops = NULL;
	}
	up_write(&chip->ops_sem);
}

static void tpm_del_legacy_sysfs(struct tpm_chip *chip)
{
	struct attribute **i;

	if (chip->flags & (TPM_CHIP_FLAG_TPM2 | TPM_CHIP_FLAG_VIRTUAL) ||
	    tpm_is_firmware_upgrade(chip))
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

	if (chip->flags & (TPM_CHIP_FLAG_TPM2 | TPM_CHIP_FLAG_VIRTUAL) ||
		tpm_is_firmware_upgrade(chip))
		return 0;

	rc = compat_only_sysfs_link_entry_to_kobj(
		    &chip->dev.parent->kobj, &chip->dev.kobj, "ppi", NULL);
	if (rc && rc != -ENOENT)
		return rc;

	/* All the names from tpm-sysfs */
	for (i = chip->groups[0]->attrs; *i != NULL; ++i) {
		rc = compat_only_sysfs_link_entry_to_kobj(
		    &chip->dev.parent->kobj, &chip->dev.kobj, (*i)->name, NULL);
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

static bool tpm_is_hwrng_enabled(struct tpm_chip *chip)
{
	if (!IS_ENABLED(CONFIG_HW_RANDOM_TPM))
		return false;
	if (tpm_is_firmware_upgrade(chip))
		return false;
	if (chip->flags & TPM_CHIP_FLAG_HWRNG_DISABLED)
		return false;
	return true;
}

static int tpm_add_hwrng(struct tpm_chip *chip)
{
	if (!tpm_is_hwrng_enabled(chip))
		return 0;

	snprintf(chip->hwrng_name, sizeof(chip->hwrng_name),
		 "tpm-rng-%d", chip->dev_num);
	chip->hwrng.name = chip->hwrng_name;
	chip->hwrng.read = tpm_hwrng_read;
	return hwrng_register(&chip->hwrng);
}

static int tpm_get_pcr_allocation(struct tpm_chip *chip)
{
	int rc;

	if (tpm_is_firmware_upgrade(chip))
		return 0;

	rc = (chip->flags & TPM_CHIP_FLAG_TPM2) ?
	     tpm2_get_pcr_allocation(chip) :
	     tpm1_get_pcr_allocation(chip);

	if (rc > 0)
		return -ENODEV;

	return rc;
}

/*
 * tpm_chip_bootstrap() - Boostrap TPM chip after power on
 * @chip: TPM chip to use.
 *
 * Initialize TPM chip after power on. This a one-shot function: subsequent
 * calls will have no effect.
 */
int tpm_chip_bootstrap(struct tpm_chip *chip)
{
	int rc;

	if (chip->flags & TPM_CHIP_FLAG_BOOTSTRAPPED)
		return 0;

	rc = tpm_chip_start(chip);
	if (rc)
		return rc;

	rc = tpm_auto_startup(chip);
	if (rc)
		goto stop;

	rc = tpm_get_pcr_allocation(chip);
stop:
	tpm_chip_stop(chip);

	/*
	 * Unconditionally set, as driver initialization should cease, when the
	 * boostrapping process fails.
	 */
	chip->flags |= TPM_CHIP_FLAG_BOOTSTRAPPED;

	return rc;
}
EXPORT_SYMBOL_GPL(tpm_chip_bootstrap);

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

	rc = tpm_chip_bootstrap(chip);
	if (rc)
		return rc;

	tpm_sysfs_add_device(chip);

	tpm_bios_log_setup(chip);

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
	if (tpm_is_hwrng_enabled(chip))
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
#ifdef CONFIG_TCG_TPM2_HMAC
	int rc;

	rc = tpm_try_get_ops(chip);
	if (!rc) {
		tpm2_end_auth_session(chip);
		tpm_put_ops(chip);
	}
#endif

	tpm_del_legacy_sysfs(chip);
	if (tpm_is_hwrng_enabled(chip))
		hwrng_unregister(&chip->hwrng);
	tpm_bios_log_teardown(chip);
	if (chip->flags & TPM_CHIP_FLAG_TPM2 && !tpm_is_firmware_upgrade(chip))
		tpm_devs_remove(chip);
	tpm_del_char_device(chip);
}
EXPORT_SYMBOL_GPL(tpm_chip_unregister);
