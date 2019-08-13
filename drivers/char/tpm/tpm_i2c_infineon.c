// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012,2013 Infineon Technologies
 *
 * Authors:
 * Peter Huewe <peter.huewe@infineon.com>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org
 *
 * This device driver implements the TPM interface as defined in
 * the TCG TPM Interface Spec version 1.2, revision 1.0 and the
 * Infineon I2C Protocol Stack Specification v0.20.
 *
 * It is based on the original tpm_tis device driver from Leendert van
 * Dorn and Kyleen Hall.
 */
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/wait.h>
#include "tpm.h"

#define TPM_I2C_INFINEON_BUFSIZE 1260

/* max. number of iterations after I2C NAK */
#define MAX_COUNT 3

#define SLEEP_DURATION_LOW 55
#define SLEEP_DURATION_HI 65

/* max. number of iterations after I2C NAK for 'long' commands
 * we need this especially for sending TPM_READY, since the cleanup after the
 * transtion to the ready state may take some time, but it is unpredictable
 * how long it will take.
 */
#define MAX_COUNT_LONG 50

#define SLEEP_DURATION_LONG_LOW 200
#define SLEEP_DURATION_LONG_HI 220

/* After sending TPM_READY to 'reset' the TPM we have to sleep even longer */
#define SLEEP_DURATION_RESET_LOW 2400
#define SLEEP_DURATION_RESET_HI 2600

/* we want to use usleep_range instead of msleep for the 5ms TPM_TIMEOUT */
#define TPM_TIMEOUT_US_LOW (TPM_TIMEOUT * 1000)
#define TPM_TIMEOUT_US_HI  (TPM_TIMEOUT_US_LOW + 2000)

/* expected value for DIDVID register */
#define TPM_TIS_I2C_DID_VID_9635 0xd1150b00L
#define TPM_TIS_I2C_DID_VID_9645 0x001a15d1L

enum i2c_chip_type {
	SLB9635,
	SLB9645,
	UNKNOWN,
};

struct tpm_inf_dev {
	struct i2c_client *client;
	int locality;
	/* In addition to the data itself, the buffer must fit the 7-bit I2C
	 * address and the direction bit.
	 */
	u8 buf[TPM_I2C_INFINEON_BUFSIZE + 1];
	struct tpm_chip *chip;
	enum i2c_chip_type chip_type;
	unsigned int adapterlimit;
};

static struct tpm_inf_dev tpm_dev;

/*
 * iic_tpm_read() - read from TPM register
 * @addr: register address to read from
 * @buffer: provided by caller
 * @len: number of bytes to read
 *
 * Read len bytes from TPM register and put them into
 * buffer (little-endian format, i.e. first byte is put into buffer[0]).
 *
 * NOTE: TPM is big-endian for multi-byte values. Multi-byte
 * values have to be swapped.
 *
 * NOTE: We can't unfortunately use the combined read/write functions
 * provided by the i2c core as the TPM currently does not support the
 * repeated start condition and due to it's special requirements.
 * The i2c_smbus* functions do not work for this chip.
 *
 * Return -EIO on error, 0 on success.
 */
static int iic_tpm_read(u8 addr, u8 *buffer, size_t len)
{

	struct i2c_msg msg1 = {
		.addr = tpm_dev.client->addr,
		.len = 1,
		.buf = &addr
	};
	struct i2c_msg msg2 = {
		.addr = tpm_dev.client->addr,
		.flags = I2C_M_RD,
		.len = len,
		.buf = buffer
	};
	struct i2c_msg msgs[] = {msg1, msg2};

	int rc = 0;
	int count;
	unsigned int msglen = len;

	/* Lock the adapter for the duration of the whole sequence. */
	if (!tpm_dev.client->adapter->algo->master_xfer)
		return -EOPNOTSUPP;
	i2c_lock_bus(tpm_dev.client->adapter, I2C_LOCK_SEGMENT);

	if (tpm_dev.chip_type == SLB9645) {
		/* use a combined read for newer chips
		 * unfortunately the smbus functions are not suitable due to
		 * the 32 byte limit of the smbus.
		 * retries should usually not be needed, but are kept just to
		 * be on the safe side.
		 */
		for (count = 0; count < MAX_COUNT; count++) {
			rc = __i2c_transfer(tpm_dev.client->adapter, msgs, 2);
			if (rc > 0)
				break;	/* break here to skip sleep */
			usleep_range(SLEEP_DURATION_LOW, SLEEP_DURATION_HI);
		}
	} else {
		/* Expect to send one command message and one data message, but
		 * support looping over each or both if necessary.
		 */
		while (len > 0) {
			/* slb9635 protocol should work in all cases */
			for (count = 0; count < MAX_COUNT; count++) {
				rc = __i2c_transfer(tpm_dev.client->adapter,
						    &msg1, 1);
				if (rc > 0)
					break;	/* break here to skip sleep */

				usleep_range(SLEEP_DURATION_LOW,
					     SLEEP_DURATION_HI);
			}

			if (rc <= 0)
				goto out;

			/* After the TPM has successfully received the register
			 * address it needs some time, thus we're sleeping here
			 * again, before retrieving the data
			 */
			for (count = 0; count < MAX_COUNT; count++) {
				if (tpm_dev.adapterlimit) {
					msglen = min_t(unsigned int,
						       tpm_dev.adapterlimit,
						       len);
					msg2.len = msglen;
				}
				usleep_range(SLEEP_DURATION_LOW,
					     SLEEP_DURATION_HI);
				rc = __i2c_transfer(tpm_dev.client->adapter,
						    &msg2, 1);
				if (rc > 0) {
					/* Since len is unsigned, make doubly
					 * sure we do not underflow it.
					 */
					if (msglen > len)
						len = 0;
					else
						len -= msglen;
					msg2.buf += msglen;
					break;
				}
				/* If the I2C adapter rejected the request (e.g
				 * when the quirk read_max_len < len) fall back
				 * to a sane minimum value and try again.
				 */
				if (rc == -EOPNOTSUPP)
					tpm_dev.adapterlimit =
							I2C_SMBUS_BLOCK_MAX;
			}

			if (rc <= 0)
				goto out;
		}
	}

out:
	i2c_unlock_bus(tpm_dev.client->adapter, I2C_LOCK_SEGMENT);
	/* take care of 'guard time' */
	usleep_range(SLEEP_DURATION_LOW, SLEEP_DURATION_HI);

	/* __i2c_transfer returns the number of successfully transferred
	 * messages.
	 * So rc should be greater than 0 here otherwise we have an error.
	 */
	if (rc <= 0)
		return -EIO;

	return 0;
}

static int iic_tpm_write_generic(u8 addr, u8 *buffer, size_t len,
				 unsigned int sleep_low,
				 unsigned int sleep_hi, u8 max_count)
{
	int rc = -EIO;
	int count;

	struct i2c_msg msg1 = {
		.addr = tpm_dev.client->addr,
		.len = len + 1,
		.buf = tpm_dev.buf
	};

	if (len > TPM_I2C_INFINEON_BUFSIZE)
		return -EINVAL;

	if (!tpm_dev.client->adapter->algo->master_xfer)
		return -EOPNOTSUPP;
	i2c_lock_bus(tpm_dev.client->adapter, I2C_LOCK_SEGMENT);

	/* prepend the 'register address' to the buffer */
	tpm_dev.buf[0] = addr;
	memcpy(&(tpm_dev.buf[1]), buffer, len);

	/*
	 * NOTE: We have to use these special mechanisms here and unfortunately
	 * cannot rely on the standard behavior of i2c_transfer.
	 * Even for newer chips the smbus functions are not
	 * suitable due to the 32 byte limit of the smbus.
	 */
	for (count = 0; count < max_count; count++) {
		rc = __i2c_transfer(tpm_dev.client->adapter, &msg1, 1);
		if (rc > 0)
			break;
		usleep_range(sleep_low, sleep_hi);
	}

	i2c_unlock_bus(tpm_dev.client->adapter, I2C_LOCK_SEGMENT);
	/* take care of 'guard time' */
	usleep_range(SLEEP_DURATION_LOW, SLEEP_DURATION_HI);

	/* __i2c_transfer returns the number of successfully transferred
	 * messages.
	 * So rc should be greater than 0 here otherwise we have an error.
	 */
	if (rc <= 0)
		return -EIO;

	return 0;
}

/*
 * iic_tpm_write() - write to TPM register
 * @addr: register address to write to
 * @buffer: containing data to be written
 * @len: number of bytes to write
 *
 * Write len bytes from provided buffer to TPM register (little
 * endian format, i.e. buffer[0] is written as first byte).
 *
 * NOTE: TPM is big-endian for multi-byte values. Multi-byte
 * values have to be swapped.
 *
 * NOTE: use this function instead of the iic_tpm_write_generic function.
 *
 * Return -EIO on error, 0 on success
 */
static int iic_tpm_write(u8 addr, u8 *buffer, size_t len)
{
	return iic_tpm_write_generic(addr, buffer, len, SLEEP_DURATION_LOW,
				     SLEEP_DURATION_HI, MAX_COUNT);
}

/*
 * This function is needed especially for the cleanup situation after
 * sending TPM_READY
 * */
static int iic_tpm_write_long(u8 addr, u8 *buffer, size_t len)
{
	return iic_tpm_write_generic(addr, buffer, len, SLEEP_DURATION_LONG_LOW,
				     SLEEP_DURATION_LONG_HI, MAX_COUNT_LONG);
}

enum tis_access {
	TPM_ACCESS_VALID = 0x80,
	TPM_ACCESS_ACTIVE_LOCALITY = 0x20,
	TPM_ACCESS_REQUEST_PENDING = 0x04,
	TPM_ACCESS_REQUEST_USE = 0x02,
};

enum tis_status {
	TPM_STS_VALID = 0x80,
	TPM_STS_COMMAND_READY = 0x40,
	TPM_STS_GO = 0x20,
	TPM_STS_DATA_AVAIL = 0x10,
	TPM_STS_DATA_EXPECT = 0x08,
};

enum tis_defaults {
	TIS_SHORT_TIMEOUT = 750,	/* ms */
	TIS_LONG_TIMEOUT = 2000,	/* 2 sec */
};

#define	TPM_ACCESS(l)			(0x0000 | ((l) << 4))
#define	TPM_STS(l)			(0x0001 | ((l) << 4))
#define	TPM_DATA_FIFO(l)		(0x0005 | ((l) << 4))
#define	TPM_DID_VID(l)			(0x0006 | ((l) << 4))

static bool check_locality(struct tpm_chip *chip, int loc)
{
	u8 buf;
	int rc;

	rc = iic_tpm_read(TPM_ACCESS(loc), &buf, 1);
	if (rc < 0)
		return false;

	if ((buf & (TPM_ACCESS_ACTIVE_LOCALITY | TPM_ACCESS_VALID)) ==
	    (TPM_ACCESS_ACTIVE_LOCALITY | TPM_ACCESS_VALID)) {
		tpm_dev.locality = loc;
		return true;
	}

	return false;
}

/* implementation similar to tpm_tis */
static void release_locality(struct tpm_chip *chip, int loc, int force)
{
	u8 buf;
	if (iic_tpm_read(TPM_ACCESS(loc), &buf, 1) < 0)
		return;

	if (force || (buf & (TPM_ACCESS_REQUEST_PENDING | TPM_ACCESS_VALID)) ==
	    (TPM_ACCESS_REQUEST_PENDING | TPM_ACCESS_VALID)) {
		buf = TPM_ACCESS_ACTIVE_LOCALITY;
		iic_tpm_write(TPM_ACCESS(loc), &buf, 1);
	}
}

static int request_locality(struct tpm_chip *chip, int loc)
{
	unsigned long stop;
	u8 buf = TPM_ACCESS_REQUEST_USE;

	if (check_locality(chip, loc))
		return loc;

	iic_tpm_write(TPM_ACCESS(loc), &buf, 1);

	/* wait for burstcount */
	stop = jiffies + chip->timeout_a;
	do {
		if (check_locality(chip, loc))
			return loc;
		usleep_range(TPM_TIMEOUT_US_LOW, TPM_TIMEOUT_US_HI);
	} while (time_before(jiffies, stop));

	return -ETIME;
}

static u8 tpm_tis_i2c_status(struct tpm_chip *chip)
{
	/* NOTE: since I2C read may fail, return 0 in this case --> time-out */
	u8 buf = 0xFF;
	u8 i = 0;

	do {
		if (iic_tpm_read(TPM_STS(tpm_dev.locality), &buf, 1) < 0)
			return 0;

		i++;
	/* if locallity is set STS should not be 0xFF */
	} while ((buf == 0xFF) && i < 10);

	return buf;
}

static void tpm_tis_i2c_ready(struct tpm_chip *chip)
{
	/* this causes the current command to be aborted */
	u8 buf = TPM_STS_COMMAND_READY;
	iic_tpm_write_long(TPM_STS(tpm_dev.locality), &buf, 1);
}

static ssize_t get_burstcount(struct tpm_chip *chip)
{
	unsigned long stop;
	ssize_t burstcnt;
	u8 buf[3];

	/* wait for burstcount */
	/* which timeout value, spec has 2 answers (c & d) */
	stop = jiffies + chip->timeout_d;
	do {
		/* Note: STS is little endian */
		if (iic_tpm_read(TPM_STS(tpm_dev.locality)+1, buf, 3) < 0)
			burstcnt = 0;
		else
			burstcnt = (buf[2] << 16) + (buf[1] << 8) + buf[0];

		if (burstcnt)
			return burstcnt;

		usleep_range(TPM_TIMEOUT_US_LOW, TPM_TIMEOUT_US_HI);
	} while (time_before(jiffies, stop));
	return -EBUSY;
}

static int wait_for_stat(struct tpm_chip *chip, u8 mask, unsigned long timeout,
			 int *status)
{
	unsigned long stop;

	/* check current status */
	*status = tpm_tis_i2c_status(chip);
	if ((*status != 0xFF) && (*status & mask) == mask)
		return 0;

	stop = jiffies + timeout;
	do {
		/* since we just checked the status, give the TPM some time */
		usleep_range(TPM_TIMEOUT_US_LOW, TPM_TIMEOUT_US_HI);
		*status = tpm_tis_i2c_status(chip);
		if ((*status & mask) == mask)
			return 0;

	} while (time_before(jiffies, stop));

	return -ETIME;
}

static int recv_data(struct tpm_chip *chip, u8 *buf, size_t count)
{
	size_t size = 0;
	ssize_t burstcnt;
	u8 retries = 0;
	int rc;

	while (size < count) {
		burstcnt = get_burstcount(chip);

		/* burstcnt < 0 = TPM is busy */
		if (burstcnt < 0)
			return burstcnt;

		/* limit received data to max. left */
		if (burstcnt > (count - size))
			burstcnt = count - size;

		rc = iic_tpm_read(TPM_DATA_FIFO(tpm_dev.locality),
				  &(buf[size]), burstcnt);
		if (rc == 0)
			size += burstcnt;
		else if (rc < 0)
			retries++;

		/* avoid endless loop in case of broken HW */
		if (retries > MAX_COUNT_LONG)
			return -EIO;
	}
	return size;
}

static int tpm_tis_i2c_recv(struct tpm_chip *chip, u8 *buf, size_t count)
{
	int size = 0;
	int status;
	u32 expected;

	if (count < TPM_HEADER_SIZE) {
		size = -EIO;
		goto out;
	}

	/* read first 10 bytes, including tag, paramsize, and result */
	size = recv_data(chip, buf, TPM_HEADER_SIZE);
	if (size < TPM_HEADER_SIZE) {
		dev_err(&chip->dev, "Unable to read header\n");
		goto out;
	}

	expected = be32_to_cpu(*(__be32 *)(buf + 2));
	if (((size_t) expected > count) || (expected < TPM_HEADER_SIZE)) {
		size = -EIO;
		goto out;
	}

	size += recv_data(chip, &buf[TPM_HEADER_SIZE],
			  expected - TPM_HEADER_SIZE);
	if (size < expected) {
		dev_err(&chip->dev, "Unable to read remainder of result\n");
		size = -ETIME;
		goto out;
	}

	wait_for_stat(chip, TPM_STS_VALID, chip->timeout_c, &status);
	if (status & TPM_STS_DATA_AVAIL) {	/* retry? */
		dev_err(&chip->dev, "Error left over data\n");
		size = -EIO;
		goto out;
	}

out:
	tpm_tis_i2c_ready(chip);
	/* The TPM needs some time to clean up here,
	 * so we sleep rather than keeping the bus busy
	 */
	usleep_range(SLEEP_DURATION_RESET_LOW, SLEEP_DURATION_RESET_HI);
	release_locality(chip, tpm_dev.locality, 0);
	return size;
}

static int tpm_tis_i2c_send(struct tpm_chip *chip, u8 *buf, size_t len)
{
	int rc, status;
	ssize_t burstcnt;
	size_t count = 0;
	u8 retries = 0;
	u8 sts = TPM_STS_GO;

	if (len > TPM_I2C_INFINEON_BUFSIZE)
		return -E2BIG;

	if (request_locality(chip, 0) < 0)
		return -EBUSY;

	status = tpm_tis_i2c_status(chip);
	if ((status & TPM_STS_COMMAND_READY) == 0) {
		tpm_tis_i2c_ready(chip);
		if (wait_for_stat
		    (chip, TPM_STS_COMMAND_READY,
		     chip->timeout_b, &status) < 0) {
			rc = -ETIME;
			goto out_err;
		}
	}

	while (count < len - 1) {
		burstcnt = get_burstcount(chip);

		/* burstcnt < 0 = TPM is busy */
		if (burstcnt < 0)
			return burstcnt;

		if (burstcnt > (len - 1 - count))
			burstcnt = len - 1 - count;

		rc = iic_tpm_write(TPM_DATA_FIFO(tpm_dev.locality),
				   &(buf[count]), burstcnt);
		if (rc == 0)
			count += burstcnt;
		else if (rc < 0)
			retries++;

		/* avoid endless loop in case of broken HW */
		if (retries > MAX_COUNT_LONG) {
			rc = -EIO;
			goto out_err;
		}

		wait_for_stat(chip, TPM_STS_VALID,
			      chip->timeout_c, &status);

		if ((status & TPM_STS_DATA_EXPECT) == 0) {
			rc = -EIO;
			goto out_err;
		}
	}

	/* write last byte */
	iic_tpm_write(TPM_DATA_FIFO(tpm_dev.locality), &(buf[count]), 1);
	wait_for_stat(chip, TPM_STS_VALID, chip->timeout_c, &status);
	if ((status & TPM_STS_DATA_EXPECT) != 0) {
		rc = -EIO;
		goto out_err;
	}

	/* go and do it */
	iic_tpm_write(TPM_STS(tpm_dev.locality), &sts, 1);

	return 0;
out_err:
	tpm_tis_i2c_ready(chip);
	/* The TPM needs some time to clean up here,
	 * so we sleep rather than keeping the bus busy
	 */
	usleep_range(SLEEP_DURATION_RESET_LOW, SLEEP_DURATION_RESET_HI);
	release_locality(chip, tpm_dev.locality, 0);
	return rc;
}

static bool tpm_tis_i2c_req_canceled(struct tpm_chip *chip, u8 status)
{
	return (status == TPM_STS_COMMAND_READY);
}

static const struct tpm_class_ops tpm_tis_i2c = {
	.flags = TPM_OPS_AUTO_STARTUP,
	.status = tpm_tis_i2c_status,
	.recv = tpm_tis_i2c_recv,
	.send = tpm_tis_i2c_send,
	.cancel = tpm_tis_i2c_ready,
	.req_complete_mask = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	.req_complete_val = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	.req_canceled = tpm_tis_i2c_req_canceled,
};

static int tpm_tis_i2c_init(struct device *dev)
{
	u32 vendor;
	int rc = 0;
	struct tpm_chip *chip;

	chip = tpmm_chip_alloc(dev, &tpm_tis_i2c);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	/* Default timeouts */
	chip->timeout_a = msecs_to_jiffies(TIS_SHORT_TIMEOUT);
	chip->timeout_b = msecs_to_jiffies(TIS_LONG_TIMEOUT);
	chip->timeout_c = msecs_to_jiffies(TIS_SHORT_TIMEOUT);
	chip->timeout_d = msecs_to_jiffies(TIS_SHORT_TIMEOUT);

	if (request_locality(chip, 0) != 0) {
		dev_err(dev, "could not request locality\n");
		rc = -ENODEV;
		goto out_err;
	}

	/* read four bytes from DID_VID register */
	if (iic_tpm_read(TPM_DID_VID(0), (u8 *)&vendor, 4) < 0) {
		dev_err(dev, "could not read vendor id\n");
		rc = -EIO;
		goto out_release;
	}

	if (vendor == TPM_TIS_I2C_DID_VID_9645) {
		tpm_dev.chip_type = SLB9645;
	} else if (vendor == TPM_TIS_I2C_DID_VID_9635) {
		tpm_dev.chip_type = SLB9635;
	} else {
		dev_err(dev, "vendor id did not match! ID was %08x\n", vendor);
		rc = -ENODEV;
		goto out_release;
	}

	dev_info(dev, "1.2 TPM (device-id 0x%X)\n", vendor >> 16);

	tpm_dev.chip = chip;

	return tpm_chip_register(chip);
out_release:
	release_locality(chip, tpm_dev.locality, 1);
	tpm_dev.client = NULL;
out_err:
	return rc;
}

static const struct i2c_device_id tpm_tis_i2c_table[] = {
	{"tpm_i2c_infineon"},
	{"slb9635tt"},
	{"slb9645tt"},
	{},
};

MODULE_DEVICE_TABLE(i2c, tpm_tis_i2c_table);

#ifdef CONFIG_OF
static const struct of_device_id tpm_tis_i2c_of_match[] = {
	{.compatible = "infineon,tpm_i2c_infineon"},
	{.compatible = "infineon,slb9635tt"},
	{.compatible = "infineon,slb9645tt"},
	{},
};
MODULE_DEVICE_TABLE(of, tpm_tis_i2c_of_match);
#endif

static SIMPLE_DEV_PM_OPS(tpm_tis_i2c_ops, tpm_pm_suspend, tpm_pm_resume);

static int tpm_tis_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	int rc;
	struct device *dev = &(client->dev);

	if (tpm_dev.client != NULL) {
		dev_err(dev, "This driver only supports one client at a time\n");
		return -EBUSY;	/* We only support one client */
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "no algorithms associated to the i2c bus\n");
		return -ENODEV;
	}

	tpm_dev.client = client;
	rc = tpm_tis_i2c_init(&client->dev);
	if (rc != 0) {
		tpm_dev.client = NULL;
		rc = -ENODEV;
	}
	return rc;
}

static int tpm_tis_i2c_remove(struct i2c_client *client)
{
	struct tpm_chip *chip = tpm_dev.chip;

	tpm_chip_unregister(chip);
	release_locality(chip, tpm_dev.locality, 1);
	tpm_dev.client = NULL;

	return 0;
}

static struct i2c_driver tpm_tis_i2c_driver = {
	.id_table = tpm_tis_i2c_table,
	.probe = tpm_tis_i2c_probe,
	.remove = tpm_tis_i2c_remove,
	.driver = {
		   .name = "tpm_i2c_infineon",
		   .pm = &tpm_tis_i2c_ops,
		   .of_match_table = of_match_ptr(tpm_tis_i2c_of_match),
		   },
};

module_i2c_driver(tpm_tis_i2c_driver);
MODULE_AUTHOR("Peter Huewe <peter.huewe@infineon.com>");
MODULE_DESCRIPTION("TPM TIS I2C Infineon Driver");
MODULE_VERSION("2.2.0");
MODULE_LICENSE("GPL");
