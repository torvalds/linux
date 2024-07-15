// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2004 IBM Corporation
 * Copyright (C) 2014 Intel Corporation
 *
 * Authors:
 * Leendert van Doorn <leendert@watson.ibm.com>
 * Dave Safford <safford@watson.ibm.com>
 * Reiner Sailer <sailer@watson.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org
 *
 * Note, the TPM chip is not interrupt driven (only polling)
 * and can have very long timeouts (minutes!). Hence the unusual
 * calls to msleep.
 */

#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/suspend.h>
#include <linux/freezer.h>
#include <linux/tpm_eventlog.h>

#include "tpm.h"

/*
 * Bug workaround - some TPM's don't flush the most
 * recently changed pcr on suspend, so force the flush
 * with an extend to the selected _unused_ non-volatile pcr.
 */
static u32 tpm_suspend_pcr;
module_param_named(suspend_pcr, tpm_suspend_pcr, uint, 0644);
MODULE_PARM_DESC(suspend_pcr,
		 "PCR to use for dummy writes to facilitate flush on suspend.");

/**
 * tpm_calc_ordinal_duration() - calculate the maximum command duration
 * @chip:    TPM chip to use.
 * @ordinal: TPM command ordinal.
 *
 * The function returns the maximum amount of time the chip could take
 * to return the result for a particular ordinal in jiffies.
 *
 * Return: A maximal duration time for an ordinal in jiffies.
 */
unsigned long tpm_calc_ordinal_duration(struct tpm_chip *chip, u32 ordinal)
{
	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		return tpm2_calc_ordinal_duration(chip, ordinal);
	else
		return tpm1_calc_ordinal_duration(chip, ordinal);
}
EXPORT_SYMBOL_GPL(tpm_calc_ordinal_duration);

static ssize_t tpm_try_transmit(struct tpm_chip *chip, void *buf, size_t bufsiz)
{
	struct tpm_header *header = buf;
	int rc;
	ssize_t len = 0;
	u32 count, ordinal;
	unsigned long stop;

	if (bufsiz < TPM_HEADER_SIZE)
		return -EINVAL;

	if (bufsiz > TPM_BUFSIZE)
		bufsiz = TPM_BUFSIZE;

	count = be32_to_cpu(header->length);
	ordinal = be32_to_cpu(header->ordinal);
	if (count == 0)
		return -ENODATA;
	if (count > bufsiz) {
		dev_err(&chip->dev,
			"invalid count value %x %zx\n", count, bufsiz);
		return -E2BIG;
	}

	rc = chip->ops->send(chip, buf, count);
	if (rc < 0) {
		if (rc != -EPIPE)
			dev_err(&chip->dev,
				"%s: send(): error %d\n", __func__, rc);
		return rc;
	}

	/* A sanity check. send() should just return zero on success e.g.
	 * not the command length.
	 */
	if (rc > 0) {
		dev_warn(&chip->dev,
			 "%s: send(): invalid value %d\n", __func__, rc);
		rc = 0;
	}

	if (chip->flags & TPM_CHIP_FLAG_IRQ)
		goto out_recv;

	stop = jiffies + tpm_calc_ordinal_duration(chip, ordinal);
	do {
		u8 status = chip->ops->status(chip);
		if ((status & chip->ops->req_complete_mask) ==
		    chip->ops->req_complete_val)
			goto out_recv;

		if (chip->ops->req_canceled(chip, status)) {
			dev_err(&chip->dev, "Operation Canceled\n");
			return -ECANCELED;
		}

		tpm_msleep(TPM_TIMEOUT_POLL);
		rmb();
	} while (time_before(jiffies, stop));

	chip->ops->cancel(chip);
	dev_err(&chip->dev, "Operation Timed out\n");
	return -ETIME;

out_recv:
	len = chip->ops->recv(chip, buf, bufsiz);
	if (len < 0) {
		rc = len;
		dev_err(&chip->dev, "tpm_transmit: tpm_recv: error %d\n", rc);
	} else if (len < TPM_HEADER_SIZE || len != be32_to_cpu(header->length))
		rc = -EFAULT;

	return rc ? rc : len;
}

/**
 * tpm_transmit - Internal kernel interface to transmit TPM commands.
 * @chip:	a TPM chip to use
 * @buf:	a TPM command buffer
 * @bufsiz:	length of the TPM command buffer
 *
 * A wrapper around tpm_try_transmit() that handles TPM2_RC_RETRY returns from
 * the TPM and retransmits the command after a delay up to a maximum wait of
 * TPM2_DURATION_LONG.
 *
 * Note that TPM 1.x never returns TPM2_RC_RETRY so the retry logic is TPM 2.0
 * only.
 *
 * Return:
 * * The response length	- OK
 * * -errno			- A system error
 */
ssize_t tpm_transmit(struct tpm_chip *chip, u8 *buf, size_t bufsiz)
{
	struct tpm_header *header = (struct tpm_header *)buf;
	/* space for header and handles */
	u8 save[TPM_HEADER_SIZE + 3*sizeof(u32)];
	unsigned int delay_msec = TPM2_DURATION_SHORT;
	u32 rc = 0;
	ssize_t ret;
	const size_t save_size = min(sizeof(save), bufsiz);
	/* the command code is where the return code will be */
	u32 cc = be32_to_cpu(header->return_code);

	/*
	 * Subtlety here: if we have a space, the handles will be
	 * transformed, so when we restore the header we also have to
	 * restore the handles.
	 */
	memcpy(save, buf, save_size);

	for (;;) {
		ret = tpm_try_transmit(chip, buf, bufsiz);
		if (ret < 0)
			break;
		rc = be32_to_cpu(header->return_code);
		if (rc != TPM2_RC_RETRY && rc != TPM2_RC_TESTING)
			break;
		/*
		 * return immediately if self test returns test
		 * still running to shorten boot time.
		 */
		if (rc == TPM2_RC_TESTING && cc == TPM2_CC_SELF_TEST)
			break;

		if (delay_msec > TPM2_DURATION_LONG) {
			if (rc == TPM2_RC_RETRY)
				dev_err(&chip->dev, "in retry loop\n");
			else
				dev_err(&chip->dev,
					"self test is still running\n");
			break;
		}
		tpm_msleep(delay_msec);
		delay_msec *= 2;
		memcpy(buf, save, save_size);
	}
	return ret;
}

/**
 * tpm_transmit_cmd - send a tpm command to the device
 * @chip:			a TPM chip to use
 * @buf:			a TPM command buffer
 * @min_rsp_body_length:	minimum expected length of response body
 * @desc:			command description used in the error message
 *
 * Return:
 * * 0		- OK
 * * -errno	- A system error
 * * TPM_RC	- A TPM error
 */
ssize_t tpm_transmit_cmd(struct tpm_chip *chip, struct tpm_buf *buf,
			 size_t min_rsp_body_length, const char *desc)
{
	const struct tpm_header *header = (struct tpm_header *)buf->data;
	int err;
	ssize_t len;

	len = tpm_transmit(chip, buf->data, PAGE_SIZE);
	if (len <  0)
		return len;

	err = be32_to_cpu(header->return_code);
	if (err != 0 && err != TPM_ERR_DISABLED && err != TPM_ERR_DEACTIVATED
	    && err != TPM2_RC_TESTING && desc)
		dev_err(&chip->dev, "A TPM error (%d) occurred %s\n", err,
			desc);
	if (err)
		return err;

	if (len < min_rsp_body_length + TPM_HEADER_SIZE)
		return -EFAULT;

	return 0;
}
EXPORT_SYMBOL_GPL(tpm_transmit_cmd);

int tpm_get_timeouts(struct tpm_chip *chip)
{
	if (chip->flags & TPM_CHIP_FLAG_HAVE_TIMEOUTS)
		return 0;

	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		return tpm2_get_timeouts(chip);
	else
		return tpm1_get_timeouts(chip);
}
EXPORT_SYMBOL_GPL(tpm_get_timeouts);

/**
 * tpm_is_tpm2 - do we a have a TPM2 chip?
 * @chip:	a &struct tpm_chip instance, %NULL for the default chip
 *
 * Return:
 * 1 if we have a TPM2 chip.
 * 0 if we don't have a TPM2 chip.
 * A negative number for system errors (errno).
 */
int tpm_is_tpm2(struct tpm_chip *chip)
{
	int rc;

	chip = tpm_find_get_ops(chip);
	if (!chip)
		return -ENODEV;

	rc = (chip->flags & TPM_CHIP_FLAG_TPM2) != 0;

	tpm_put_ops(chip);

	return rc;
}
EXPORT_SYMBOL_GPL(tpm_is_tpm2);

/**
 * tpm_pcr_read - read a PCR value from SHA1 bank
 * @chip:	a &struct tpm_chip instance, %NULL for the default chip
 * @pcr_idx:	the PCR to be retrieved
 * @digest:	the PCR bank and buffer current PCR value is written to
 *
 * Return: same as with tpm_transmit_cmd()
 */
int tpm_pcr_read(struct tpm_chip *chip, u32 pcr_idx,
		 struct tpm_digest *digest)
{
	int rc;

	chip = tpm_find_get_ops(chip);
	if (!chip)
		return -ENODEV;

	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		rc = tpm2_pcr_read(chip, pcr_idx, digest, NULL);
	else
		rc = tpm1_pcr_read(chip, pcr_idx, digest->digest);

	tpm_put_ops(chip);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_pcr_read);

/**
 * tpm_pcr_extend - extend a PCR value in SHA1 bank.
 * @chip:	a &struct tpm_chip instance, %NULL for the default chip
 * @pcr_idx:	the PCR to be retrieved
 * @digests:	array of tpm_digest structures used to extend PCRs
 *
 * Note: callers must pass a digest for every allocated PCR bank, in the same
 * order of the banks in chip->allocated_banks.
 *
 * Return: same as with tpm_transmit_cmd()
 */
int tpm_pcr_extend(struct tpm_chip *chip, u32 pcr_idx,
		   struct tpm_digest *digests)
{
	int rc;
	int i;

	chip = tpm_find_get_ops(chip);
	if (!chip)
		return -ENODEV;

	for (i = 0; i < chip->nr_allocated_banks; i++) {
		if (digests[i].alg_id != chip->allocated_banks[i].alg_id) {
			rc = -EINVAL;
			goto out;
		}
	}

	if (chip->flags & TPM_CHIP_FLAG_TPM2) {
		rc = tpm2_pcr_extend(chip, pcr_idx, digests);
		goto out;
	}

	rc = tpm1_pcr_extend(chip, pcr_idx, digests[0].digest,
			     "attempting extend a PCR value");

out:
	tpm_put_ops(chip);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_pcr_extend);

/**
 * tpm_send - send a TPM command
 * @chip:	a &struct tpm_chip instance, %NULL for the default chip
 * @cmd:	a TPM command buffer
 * @buflen:	the length of the TPM command buffer
 *
 * Return: same as with tpm_transmit_cmd()
 */
int tpm_send(struct tpm_chip *chip, void *cmd, size_t buflen)
{
	struct tpm_buf buf;
	int rc;

	chip = tpm_find_get_ops(chip);
	if (!chip)
		return -ENODEV;

	buf.data = cmd;
	rc = tpm_transmit_cmd(chip, &buf, 0, "attempting to a send a command");

	tpm_put_ops(chip);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_send);

int tpm_auto_startup(struct tpm_chip *chip)
{
	int rc;

	if (!(chip->ops->flags & TPM_OPS_AUTO_STARTUP))
		return 0;

	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		rc = tpm2_auto_startup(chip);
	else
		rc = tpm1_auto_startup(chip);

	return rc;
}

/*
 * We are about to suspend. Save the TPM state
 * so that it can be restored.
 */
int tpm_pm_suspend(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	int rc = 0;

	if (!chip)
		return -ENODEV;

	if (chip->flags & TPM_CHIP_FLAG_ALWAYS_POWERED)
		goto suspended;

	if ((chip->flags & TPM_CHIP_FLAG_FIRMWARE_POWER_MANAGED) &&
	    !pm_suspend_via_firmware())
		goto suspended;

	rc = tpm_try_get_ops(chip);
	if (!rc) {
		if (chip->flags & TPM_CHIP_FLAG_TPM2)
			tpm2_shutdown(chip, TPM2_SU_STATE);
		else
			rc = tpm1_pm_suspend(chip, tpm_suspend_pcr);

		tpm_put_ops(chip);
	}

suspended:
	chip->flags |= TPM_CHIP_FLAG_SUSPENDED;

	if (rc)
		dev_err(dev, "Ignoring error %d while suspending\n", rc);
	return 0;
}
EXPORT_SYMBOL_GPL(tpm_pm_suspend);

/*
 * Resume from a power safe. The BIOS already restored
 * the TPM state.
 */
int tpm_pm_resume(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);

	if (chip == NULL)
		return -ENODEV;

	chip->flags &= ~TPM_CHIP_FLAG_SUSPENDED;

	/*
	 * Guarantee that SUSPENDED is written last, so that hwrng does not
	 * activate before the chip has been fully resumed.
	 */
	wmb();

	return 0;
}
EXPORT_SYMBOL_GPL(tpm_pm_resume);

/**
 * tpm_get_random() - get random bytes from the TPM's RNG
 * @chip:	a &struct tpm_chip instance, %NULL for the default chip
 * @out:	destination buffer for the random bytes
 * @max:	the max number of bytes to write to @out
 *
 * Return: number of random bytes read or a negative error value.
 */
int tpm_get_random(struct tpm_chip *chip, u8 *out, size_t max)
{
	int rc;

	if (!out || max > TPM_MAX_RNG_DATA)
		return -EINVAL;

	chip = tpm_find_get_ops(chip);
	if (!chip)
		return -ENODEV;

	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		rc = tpm2_get_random(chip, out, max);
	else
		rc = tpm1_get_random(chip, out, max);

	tpm_put_ops(chip);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_get_random);

static int __init tpm_init(void)
{
	int rc;

	rc = class_register(&tpm_class);
	if (rc) {
		pr_err("couldn't create tpm class\n");
		return rc;
	}

	rc = class_register(&tpmrm_class);
	if (rc) {
		pr_err("couldn't create tpmrm class\n");
		goto out_destroy_tpm_class;
	}

	rc = alloc_chrdev_region(&tpm_devt, 0, 2*TPM_NUM_DEVICES, "tpm");
	if (rc < 0) {
		pr_err("tpm: failed to allocate char dev region\n");
		goto out_destroy_tpmrm_class;
	}

	rc = tpm_dev_common_init();
	if (rc) {
		pr_err("tpm: failed to allocate char dev region\n");
		goto out_unreg_chrdev;
	}

	return 0;

out_unreg_chrdev:
	unregister_chrdev_region(tpm_devt, 2 * TPM_NUM_DEVICES);
out_destroy_tpmrm_class:
	class_unregister(&tpmrm_class);
out_destroy_tpm_class:
	class_unregister(&tpm_class);

	return rc;
}

static void __exit tpm_exit(void)
{
	idr_destroy(&dev_nums_idr);
	class_unregister(&tpm_class);
	class_unregister(&tpmrm_class);
	unregister_chrdev_region(tpm_devt, 2*TPM_NUM_DEVICES);
	tpm_dev_common_exit();
}

subsys_initcall(tpm_init);
module_exit(tpm_exit);

MODULE_AUTHOR("Leendert van Doorn <leendert@watson.ibm.com>");
MODULE_DESCRIPTION("TPM Driver");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
