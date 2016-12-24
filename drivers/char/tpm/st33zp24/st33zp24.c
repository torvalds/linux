/*
 * STMicroelectronics TPM Linux driver for TPM ST33ZP24
 * Copyright (C) 2009 - 2016 STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/freezer.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
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
 * clear_interruption clear the pending interrupt.
 * @param: tpm_dev, the tpm device device.
 * @return: the interrupt status value.
 */
static u8 clear_interruption(struct st33zp24_dev *tpm_dev)
{
	u8 interrupt;

	tpm_dev->ops->recv(tpm_dev->phy_id, TPM_INT_STATUS, &interrupt, 1);
	tpm_dev->ops->send(tpm_dev->phy_id, TPM_INT_STATUS, &interrupt, 1);
	return interrupt;
} /* clear_interruption() */

/*
 * st33zp24_cancel, cancel the current command execution or
 * set STS to COMMAND READY.
 * @param: chip, the tpm_chip description as specified in driver/char/tpm/tpm.h
 */
static void st33zp24_cancel(struct tpm_chip *chip)
{
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	u8 data;

	data = TPM_STS_COMMAND_READY;
	tpm_dev->ops->send(tpm_dev->phy_id, TPM_STS, &data, 1);
} /* st33zp24_cancel() */

/*
 * st33zp24_status return the TPM_STS register
 * @param: chip, the tpm chip description
 * @return: the TPM_STS register value.
 */
static u8 st33zp24_status(struct tpm_chip *chip)
{
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	u8 data;

	tpm_dev->ops->recv(tpm_dev->phy_id, TPM_STS, &data, 1);
	return data;
} /* st33zp24_status() */

/*
 * check_locality if the locality is active
 * @param: chip, the tpm chip description
 * @return: the active locality or -EACCESS.
 */
static int check_locality(struct tpm_chip *chip)
{
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	u8 data;
	u8 status;

	status = tpm_dev->ops->recv(tpm_dev->phy_id, TPM_ACCESS, &data, 1);
	if (status && (data &
		(TPM_ACCESS_ACTIVE_LOCALITY | TPM_ACCESS_VALID)) ==
		(TPM_ACCESS_ACTIVE_LOCALITY | TPM_ACCESS_VALID))
		return tpm_dev->locality;

	return -EACCES;
} /* check_locality() */

/*
 * request_locality request the TPM locality
 * @param: chip, the chip description
 * @return: the active locality or negative value.
 */
static int request_locality(struct tpm_chip *chip)
{
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	unsigned long stop;
	long ret;
	u8 data;

	if (check_locality(chip) == tpm_dev->locality)
		return tpm_dev->locality;

	data = TPM_ACCESS_REQUEST_USE;
	ret = tpm_dev->ops->send(tpm_dev->phy_id, TPM_ACCESS, &data, 1);
	if (ret < 0)
		return ret;

	stop = jiffies + chip->timeout_a;

	/* Request locality is usually effective after the request */
	do {
		if (check_locality(chip) >= 0)
			return tpm_dev->locality;
		msleep(TPM_TIMEOUT);
	} while (time_before(jiffies, stop));

	/* could not get locality */
	return -EACCES;
} /* request_locality() */

/*
 * release_locality release the active locality
 * @param: chip, the tpm chip description.
 */
static void release_locality(struct tpm_chip *chip)
{
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	u8 data;

	data = TPM_ACCESS_ACTIVE_LOCALITY;

	tpm_dev->ops->send(tpm_dev->phy_id, TPM_ACCESS, &data, 1);
}

/*
 * get_burstcount return the burstcount value
 * @param: chip, the chip description
 * return: the burstcount or negative value.
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
} /* get_burstcount() */


/*
 * wait_for_tpm_stat_cond
 * @param: chip, chip description
 * @param: mask, expected mask value
 * @param: check_cancel, does the command expected to be canceled ?
 * @param: canceled, did we received a cancel request ?
 * @return: true if status == mask or if the command is canceled.
 * false in other cases.
 */
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
 * wait_for_stat wait for a TPM_STS value
 * @param: chip, the tpm chip description
 * @param: mask, the value mask to wait
 * @param: timeout, the timeout
 * @param: queue, the wait queue.
 * @param: check_cancel, does the command can be cancelled ?
 * @return: the tpm status, 0 if success, -ETIME if timeout is reached.
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
} /* wait_for_stat() */

/*
 * recv_data receive data
 * @param: chip, the tpm chip description
 * @param: buf, the buffer where the data are received
 * @param: count, the number of data to receive
 * @return: the number of bytes read from TPM FIFO.
 */
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

/*
 * tpm_ioserirq_handler the serirq irq handler
 * @param: irq, the tpm chip description
 * @param: dev_id, the description of the chip
 * @return: the status of the handler.
 */
static irqreturn_t tpm_ioserirq_handler(int irq, void *dev_id)
{
	struct tpm_chip *chip = dev_id;
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);

	tpm_dev->intrs++;
	wake_up_interruptible(&tpm_dev->read_queue);
	disable_irq_nosync(tpm_dev->irq);

	return IRQ_HANDLED;
} /* tpm_ioserirq_handler() */

/*
 * st33zp24_send send TPM commands through the I2C bus.
 *
 * @param: chip, the tpm_chip description as specified in driver/char/tpm/tpm.h
 * @param: buf,	the buffer to send.
 * @param: count, the number of bytes to send.
 * @return: In case of success the number of bytes sent.
 *			In other case, a < 0 value describing the issue.
 */
static int st33zp24_send(struct tpm_chip *chip, unsigned char *buf,
			 size_t len)
{
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	u32 status, i, size, ordinal;
	int burstcnt = 0;
	int ret;
	u8 data;

	if (!chip)
		return -EBUSY;
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

	return len;
out_err:
	st33zp24_cancel(chip);
	release_locality(chip);
	return ret;
}

/*
 * st33zp24_recv received TPM response through TPM phy.
 * @param: chip, the tpm_chip description as specified in driver/char/tpm/tpm.h.
 * @param: buf,	the buffer to store datas.
 * @param: count, the number of bytes to send.
 * @return: In case of success the number of bytes received.
 *	    In other case, a < 0 value describing the issue.
 */
static int st33zp24_recv(struct tpm_chip *chip, unsigned char *buf,
			    size_t count)
{
	int size = 0;
	int expected;

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
	if (expected > count) {
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

/*
 * st33zp24_req_canceled
 * @param: chip, the tpm_chip description as specified in driver/char/tpm/tpm.h.
 * @param: status, the TPM status.
 * @return: Does TPM ready to compute a new command ? true.
 */
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

/*
 * st33zp24_probe initialize the TPM device
 * @param: client, the i2c_client drescription (TPM I2C description).
 * @param: id, the i2c_device_id struct.
 * @return: 0 in case of success.
 *	 -1 in other case.
 */
int st33zp24_probe(void *phy_id, const struct st33zp24_phy_ops *ops,
		   struct device *dev, int irq, int io_lpcpd)
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

/*
 * st33zp24_remove remove the TPM device
 * @param: tpm_data, the tpm phy.
 * @return: 0 in case of success.
 */
int st33zp24_remove(struct tpm_chip *chip)
{
	tpm_chip_unregister(chip);
	return 0;
}
EXPORT_SYMBOL(st33zp24_remove);

#ifdef CONFIG_PM_SLEEP
/*
 * st33zp24_pm_suspend suspend the TPM device
 * @param: tpm_data, the tpm phy.
 * @param: mesg, the power management message.
 * @return: 0 in case of success.
 */
int st33zp24_pm_suspend(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);

	int ret = 0;

	if (gpio_is_valid(tpm_dev->io_lpcpd))
		gpio_set_value(tpm_dev->io_lpcpd, 0);
	else
		ret = tpm_pm_suspend(dev);

	return ret;
} /* st33zp24_pm_suspend() */
EXPORT_SYMBOL(st33zp24_pm_suspend);

/*
 * st33zp24_pm_resume resume the TPM device
 * @param: tpm_data, the tpm phy.
 * @return: 0 in case of success.
 */
int st33zp24_pm_resume(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	struct st33zp24_dev *tpm_dev = dev_get_drvdata(&chip->dev);
	int ret = 0;

	if (gpio_is_valid(tpm_dev->io_lpcpd)) {
		gpio_set_value(tpm_dev->io_lpcpd, 1);
		ret = wait_for_stat(chip,
				TPM_STS_VALID, chip->timeout_b,
				&tpm_dev->read_queue, false);
	} else {
		ret = tpm_pm_resume(dev);
		if (!ret)
			tpm_do_selftest(chip);
	}
	return ret;
} /* st33zp24_pm_resume() */
EXPORT_SYMBOL(st33zp24_pm_resume);
#endif

MODULE_AUTHOR("TPM support (TPMsupport@list.st.com)");
MODULE_DESCRIPTION("ST33ZP24 TPM 1.2 driver");
MODULE_VERSION("1.3.0");
MODULE_LICENSE("GPL");
