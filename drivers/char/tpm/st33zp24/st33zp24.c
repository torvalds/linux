// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * STMicroelectronics TPM Linux driver for TPM ST33ZP24
 * Copyright (C) 2009 - 2016 STMicroelectronics
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/freezer.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/slab.h>

#include "../tpm.h"
#include "st33zp24.h"

#define TPM_ACCESS			0x0
#define TPM_STS				0x18
#define TPM_DATA_FIFO			0x24
#define TPM_INTF_CAPABILITY		0x14
#define TPM_INT_STATUS			0x10
#define TPM_INT_ENABLE			0x08

#define LOCALITY0			0

enum st33zp24_access {
	TPM_ACCESS_VALID = 0x80,
	TPM_ACCESS_ACTIVE_LOCALITY = 0x20,
	TPM_ACCESS_REQUEST_PENDING = 0x04,
	TPM_ACCESS_REQUEST_USE = 0x02,
};

enum st33zp24_status {
	TPM_STS_VALID = 0x80,
	TPM_STS_COMMAND_READY = 0x40,
	TPM_STS_GO = 0x20,
	TPM_STS_DATA_AVAIL = 0x10,
	TPM_STS_DATA_EXPECT = 0x08,
};

enum st33zp24_int_flags {
	TPM_GLOBAL_INT_ENABLE = 0x80,
	TPM_INTF_CMD_READY_INT = 0x080,
	TPM_INTF_FIFO_AVALAIBLE_INT = 0x040,
	TPM_INTF_WAKE_UP_READY_INT = 0x020,
	TPM_INTF_LOCALITY_CHANGE_INT = 0x004,
	TPM_INTF_STS_VALID_INT = 0x002,
	TPM_INTF_DATA_AVAIL_INT = 0x001,
};

enum tis_defaults {
	TIS_SHORT_TIMEOUT = 750,
	TIS_LONG_TIMEOUT = 2000,
};

/*
 * clear the pending interrupt.
 */
static u8 clear_interruption(struct st33zp24_dev *tpm_dev)
{
	u8 interrupt;

	tpm_dev->ops->recv(tpm_dev->phy_id, TPM_INT_STATUS, &interrupt, 1);
	tpm_dev->ops->send(tpm_dev->phy_id, TPM_INT_STATUS, &interrupt, 1);
	return interrupt;
}

/*
 * cancel the current command execution or set STS to COMMAND READY.
 */
static void st33zp24_cancel(struct tpm_chip *chip)
{
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	u8 data;

	data = TPM_STS_COMMAND_READY;
	tpm_dev->ops->send(tpm_dev->phy_id, TPM_STS, &data, 1);
}

/*
 * return the TPM_STS register
 */
static u8 st33zp24_status(struct tpm_chip *chip)
{
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	u8 data;

	tpm_dev->ops->recv(tpm_dev->phy_id, TPM_STS, &data, 1);
	return data;
}

/*
 * if the locality is active
 */
static bool check_locality(struct tpm_chip *chip)
{
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	u8 data;
	u8 status;

	status = tpm_dev->ops->recv(tpm_dev->phy_id, TPM_ACCESS, &data, 1);
	if (status && (data &
		(TPM_ACCESS_ACTIVE_LOCALITY | TPM_ACCESS_VALID)) ==
		(TPM_ACCESS_ACTIVE_LOCALITY | TPM_ACCESS_VALID))
		return true;

	return false;
}

static int request_locality(struct tpm_chip *chip)
{
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	unsigned long stop;
	long ret;
	u8 data;

	if (check_locality(chip))
		return tpm_dev->locality;

	data = TPM_ACCESS_REQUEST_USE;
	ret = tpm_dev->ops->send(tpm_dev->phy_id, TPM_ACCESS, &data, 1);
	if (ret < 0)
		return ret;

	stop = jiffies + chip->timeout_a;

	/* Request locality is usually effective after the request */
	do {
		if (check_locality(chip))
			return tpm_dev->locality;
		msleep(TPM_TIMEOUT);
	} while (time_before(jiffies, stop));

	/* could not get locality */
	return -EACCES;
}

static void release_locality(struct tpm_chip *chip)
{
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	u8 data;

	data = TPM_ACCESS_ACTIVE_LOCALITY;

	tpm_dev->ops->send(tpm_dev->phy_id, TPM_ACCESS, &data, 1);
}

/*
 * get_burstcount return the burstcount value
 */
static int get_burstcount(struct tpm_chip *chip)
{
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	unsigned long stop;
	int burstcnt, status;
	u8 temp;

	stop = jiffies + chip->timeout_d;
	do {
		status = tpm_dev->ops->recv(tpm_dev->phy_id, TPM_STS + 1,
					    &temp, 1);
		if (status < 0)
			return -EBUSY;

		burstcnt = temp;
		status = tpm_dev->ops->recv(tpm_dev->phy_id, TPM_STS + 2,
					    &temp, 1);
		if (status < 0)
			return -EBUSY;

		burstcnt |= temp << 8;
		if (burstcnt)
			return burstcnt;
		msleep(TPM_TIMEOUT);
	} while (time_before(jiffies, stop));
	return -EBUSY;
}

static bool wait_for_tpm_stat_cond(struct tpm_chip *chip, u8 mask,
				bool check_cancel, bool *canceled)
{
	u8 status = chip->ops->status(chip);

	*canceled = false;
	if ((status & mask) == mask)
		return true;
	if (check_cancel && chip->ops->req_canceled(chip, status)) {
		*canceled = true;
		return true;
	}
	return false;
}

/*
 * wait for a TPM_STS value
 */
static int wait_for_stat(struct tpm_chip *chip, u8 mask, unsigned long timeout,
			wait_queue_head_t *queue, bool check_cancel)
{
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	unsigned long stop;
	int ret = 0;
	bool canceled = false;
	bool condition;
	u32 cur_intrs;
	u8 status;

	/* check current status */
	status = st33zp24_status(chip);
	if ((status & mask) == mask)
		return 0;

	stop = jiffies + timeout;

	if (chip->flags & TPM_CHIP_FLAG_IRQ) {
		cur_intrs = tpm_dev->intrs;
		clear_interruption(tpm_dev);
		enable_irq(tpm_dev->irq);

		do {
			if (ret == -ERESTARTSYS && freezing(current))
				clear_thread_flag(TIF_SIGPENDING);

			timeout = stop - jiffies;
			if ((long) timeout <= 0)
				return -1;

			ret = wait_event_interruptible_timeout(*queue,
						cur_intrs != tpm_dev->intrs,
						timeout);
			clear_interruption(tpm_dev);
			condition = wait_for_tpm_stat_cond(chip, mask,
						check_cancel, &canceled);
			if (ret >= 0 && condition) {
				if (canceled)
					return -ECANCELED;
				return 0;
			}
		} while (ret == -ERESTARTSYS && freezing(current));

		disable_irq_nosync(tpm_dev->irq);

	} else {
		do {
			msleep(TPM_TIMEOUT);
			status = chip->ops->status(chip);
			if ((status & mask) == mask)
				return 0;
		} while (time_before(jiffies, stop));
	}

	return -ETIME;
}

static int recv_data(struct tpm_chip *chip, u8 *buf, size_t count)
{
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	int size = 0, burstcnt, len, ret;

	while (size < count &&
	       wait_for_stat(chip,
			     TPM_STS_DATA_AVAIL | TPM_STS_VALID,
			     chip->timeout_c,
			     &tpm_dev->read_queue, true) == 0) {
		burstcnt = get_burstcount(chip);
		if (burstcnt < 0)
			return burstcnt;
		len = min_t(int, burstcnt, count - size);
		ret = tpm_dev->ops->recv(tpm_dev->phy_id, TPM_DATA_FIFO,
					 buf + size, len);
		if (ret < 0)
			return ret;

		size += len;
	}
	return size;
}

static irqreturn_t tpm_ioserirq_handler(int irq, void *dev_id)
{
	struct tpm_chip *chip = dev_id;
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);

	tpm_dev->intrs++;
	wake_up_interruptible(&tpm_dev->read_queue);
	disable_irq_nosync(tpm_dev->irq);

	return IRQ_HANDLED;
}

/*
 * send TPM commands through the I2C bus.
 */
static int st33zp24_send(struct tpm_chip *chip, unsigned char *buf,
			 size_t len)
{
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	u32 status, i, size, ordinal;
	int burstcnt = 0;
	int ret;
	u8 data;

	if (len < TPM_HEADER_SIZE)
		return -EBUSY;

	ret = request_locality(chip);
	if (ret < 0)
		return ret;

	status = st33zp24_status(chip);
	if ((status & TPM_STS_COMMAND_READY) == 0) {
		st33zp24_cancel(chip);
		if (wait_for_stat
		    (chip, TPM_STS_COMMAND_READY, chip->timeout_b,
		     &tpm_dev->read_queue, false) < 0) {
			ret = -ETIME;
			goto out_err;
		}
	}

	for (i = 0; i < len - 1;) {
		burstcnt = get_burstcount(chip);
		if (burstcnt < 0)
			return burstcnt;
		size = min_t(int, len - i - 1, burstcnt);
		ret = tpm_dev->ops->send(tpm_dev->phy_id, TPM_DATA_FIFO,
					 buf + i, size);
		if (ret < 0)
			goto out_err;

		i += size;
	}

	status = st33zp24_status(chip);
	if ((status & TPM_STS_DATA_EXPECT) == 0) {
		ret = -EIO;
		goto out_err;
	}

	ret = tpm_dev->ops->send(tpm_dev->phy_id, TPM_DATA_FIFO,
				 buf + len - 1, 1);
	if (ret < 0)
		goto out_err;

	status = st33zp24_status(chip);
	if ((status & TPM_STS_DATA_EXPECT) != 0) {
		ret = -EIO;
		goto out_err;
	}

	data = TPM_STS_GO;
	ret = tpm_dev->ops->send(tpm_dev->phy_id, TPM_STS, &data, 1);
	if (ret < 0)
		goto out_err;

	if (chip->flags & TPM_CHIP_FLAG_IRQ) {
		ordinal = be32_to_cpu(*((__be32 *) (buf + 6)));

		ret = wait_for_stat(chip, TPM_STS_DATA_AVAIL | TPM_STS_VALID,
				tpm_calc_ordinal_duration(chip, ordinal),
				&tpm_dev->read_queue, false);
		if (ret < 0)
			goto out_err;
	}

	return 0;
out_err:
	st33zp24_cancel(chip);
	release_locality(chip);
	return ret;
}

static int st33zp24_recv(struct tpm_chip *chip, unsigned char *buf,
			    size_t count)
{
	int size = 0;
	u32 expected;

	if (!chip)
		return -EBUSY;

	if (count < TPM_HEADER_SIZE) {
		size = -EIO;
		goto out;
	}

	size = recv_data(chip, buf, TPM_HEADER_SIZE);
	if (size < TPM_HEADER_SIZE) {
		dev_err(&chip->dev, "Unable to read header\n");
		goto out;
	}

	expected = be32_to_cpu(*(__be32 *)(buf + 2));
	if (expected > count || expected < TPM_HEADER_SIZE) {
		size = -EIO;
		goto out;
	}

	size += recv_data(chip, &buf[TPM_HEADER_SIZE],
			expected - TPM_HEADER_SIZE);
	if (size < expected) {
		dev_err(&chip->dev, "Unable to read remainder of result\n");
		size = -ETIME;
	}

out:
	st33zp24_cancel(chip);
	release_locality(chip);
	return size;
}

static bool st33zp24_req_canceled(struct tpm_chip *chip, u8 status)
{
	return (status == TPM_STS_COMMAND_READY);
}

static const struct tpm_class_ops st33zp24_tpm = {
	.flags = TPM_OPS_AUTO_STARTUP,
	.send = st33zp24_send,
	.recv = st33zp24_recv,
	.cancel = st33zp24_cancel,
	.status = st33zp24_status,
	.req_complete_mask = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	.req_complete_val = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	.req_canceled = st33zp24_req_canceled,
};

static const struct acpi_gpio_params lpcpd_gpios = { 1, 0, false };

static const struct acpi_gpio_mapping acpi_st33zp24_gpios[] = {
	{ "lpcpd-gpios", &lpcpd_gpios, 1 },
	{ },
};

/*
 * initialize the TPM device
 */
int st33zp24_probe(void *phy_id, const struct st33zp24_phy_ops *ops,
		   struct device *dev, int irq)
{
	int ret;
	u8 intmask = 0;
	struct tpm_chip *chip;
	struct st33zp24_dev *tpm_dev;

	chip = tpmm_chip_alloc(dev, &st33zp24_tpm);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	tpm_dev = devm_kzalloc(dev, sizeof(struct st33zp24_dev),
			       GFP_KERNEL);
	if (!tpm_dev)
		return -ENOMEM;

	tpm_dev->phy_id = phy_id;
	tpm_dev->ops = ops;
	dev_set_drvdata(&chip->dev, tpm_dev);

	chip->timeout_a = msecs_to_jiffies(TIS_SHORT_TIMEOUT);
	chip->timeout_b = msecs_to_jiffies(TIS_LONG_TIMEOUT);
	chip->timeout_c = msecs_to_jiffies(TIS_SHORT_TIMEOUT);
	chip->timeout_d = msecs_to_jiffies(TIS_SHORT_TIMEOUT);

	tpm_dev->locality = LOCALITY0;

	if (ACPI_COMPANION(dev)) {
		ret = devm_acpi_dev_add_driver_gpios(dev, acpi_st33zp24_gpios);
		if (ret)
			return ret;
	}

	/*
	 * Get LPCPD GPIO. If lpcpd pin is not specified. This is not an
	 * issue as power management can be also managed by TPM specific
	 * commands.
	 */
	tpm_dev->io_lpcpd = devm_gpiod_get_optional(dev, "lpcpd",
						    GPIOD_OUT_HIGH);
	ret = PTR_ERR_OR_ZERO(tpm_dev->io_lpcpd);
	if (ret) {
		dev_err(dev, "failed to request lpcpd gpio: %d\n", ret);
		return ret;
	}

	if (irq) {
		/* INTERRUPT Setup */
		init_waitqueue_head(&tpm_dev->read_queue);
		tpm_dev->intrs = 0;

		if (request_locality(chip) != LOCALITY0) {
			ret = -ENODEV;
			goto _tpm_clean_answer;
		}

		clear_interruption(tpm_dev);
		ret = devm_request_irq(dev, irq, tpm_ioserirq_handler,
				IRQF_TRIGGER_HIGH, "TPM SERIRQ management",
				chip);
		if (ret < 0) {
			dev_err(&chip->dev, "TPM SERIRQ signals %d not available\n",
				irq);
			goto _tpm_clean_answer;
		}

		intmask |= TPM_INTF_CMD_READY_INT
			|  TPM_INTF_STS_VALID_INT
			|  TPM_INTF_DATA_AVAIL_INT;

		ret = tpm_dev->ops->send(tpm_dev->phy_id, TPM_INT_ENABLE,
					 &intmask, 1);
		if (ret < 0)
			goto _tpm_clean_answer;

		intmask = TPM_GLOBAL_INT_ENABLE;
		ret = tpm_dev->ops->send(tpm_dev->phy_id, (TPM_INT_ENABLE + 3),
					 &intmask, 1);
		if (ret < 0)
			goto _tpm_clean_answer;

		tpm_dev->irq = irq;
		chip->flags |= TPM_CHIP_FLAG_IRQ;

		disable_irq_nosync(tpm_dev->irq);
	}

	return tpm_chip_register(chip);
_tpm_clean_answer:
	dev_info(&chip->dev, "TPM initialization fail\n");
	return ret;
}
EXPORT_SYMBOL(st33zp24_probe);

void st33zp24_remove(struct tpm_chip *chip)
{
	tpm_chip_unregister(chip);
}
EXPORT_SYMBOL(st33zp24_remove);

#ifdef CONFIG_PM_SLEEP
int st33zp24_pm_suspend(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);

	int ret = 0;

	if (tpm_dev->io_lpcpd)
		gpiod_set_value_cansleep(tpm_dev->io_lpcpd, 0);
	else
		ret = tpm_pm_suspend(dev);

	return ret;
}
EXPORT_SYMBOL(st33zp24_pm_suspend);

int st33zp24_pm_resume(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	int ret = 0;

	if (tpm_dev->io_lpcpd) {
		gpiod_set_value_cansleep(tpm_dev->io_lpcpd, 1);
		ret = wait_for_stat(chip,
				TPM_STS_VALID, chip->timeout_b,
				&tpm_dev->read_queue, false);
	} else {
		ret = tpm_pm_resume(dev);
		if (!ret)
			tpm1_do_selftest(chip);
	}
	return ret;
}
EXPORT_SYMBOL(st33zp24_pm_resume);
#endif

MODULE_AUTHOR("TPM support <TPMsupport@list.st.com>");
MODULE_DESCRIPTION("ST33ZP24 TPM 1.2 driver");
MODULE_VERSION("1.3.0");
MODULE_LICENSE("GPL");
