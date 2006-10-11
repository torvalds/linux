/*
 * Copyright (C) 2004 IBM Corporation
 *
 * Authors:
 * Leendert van Doorn <leendert@watson.ibm.com>
 * Dave Safford <safford@watson.ibm.com>
 * Reiner Sailer <sailer@watson.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * Maintained by: <tpmdd_devel@lists.sourceforge.net>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org	 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 * 
 * Note, the TPM chip is not interrupt driven (only polling)
 * and can have very long timeouts (minutes!). Hence the unusual
 * calls to msleep.
 *
 */

#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include "tpm.h"

enum tpm_const {
	TPM_MINOR = 224,	/* officially assigned */
	TPM_BUFSIZE = 2048,
	TPM_NUM_DEVICES = 256,
};

enum tpm_duration {
	TPM_SHORT = 0,
	TPM_MEDIUM = 1,
	TPM_LONG = 2,
	TPM_UNDEFINED,
};

#define TPM_MAX_ORDINAL 243
#define TPM_MAX_PROTECTED_ORDINAL 12
#define TPM_PROTECTED_ORDINAL_MASK 0xFF

static LIST_HEAD(tpm_chip_list);
static DEFINE_SPINLOCK(driver_lock);
static DECLARE_BITMAP(dev_mask, TPM_NUM_DEVICES);

/*
 * Array with one entry per ordinal defining the maximum amount
 * of time the chip could take to return the result.  The ordinal
 * designation of short, medium or long is defined in a table in
 * TCG Specification TPM Main Part 2 TPM Structures Section 17. The
 * values of the SHORT, MEDIUM, and LONG durations are retrieved
 * from the chip during initialization with a call to tpm_get_timeouts.
 */
static const u8 tpm_protected_ordinal_duration[TPM_MAX_PROTECTED_ORDINAL] = {
	TPM_UNDEFINED,		/* 0 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 5 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 10 */
	TPM_SHORT,
};

static const u8 tpm_ordinal_duration[TPM_MAX_ORDINAL] = {
	TPM_UNDEFINED,		/* 0 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 5 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 10 */
	TPM_SHORT,
	TPM_MEDIUM,
	TPM_LONG,
	TPM_LONG,
	TPM_MEDIUM,		/* 15 */
	TPM_SHORT,
	TPM_SHORT,
	TPM_MEDIUM,
	TPM_LONG,
	TPM_SHORT,		/* 20 */
	TPM_SHORT,
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_SHORT,		/* 25 */
	TPM_SHORT,
	TPM_MEDIUM,
	TPM_SHORT,
	TPM_SHORT,
	TPM_MEDIUM,		/* 30 */
	TPM_LONG,
	TPM_MEDIUM,
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,		/* 35 */
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_MEDIUM,		/* 40 */
	TPM_LONG,
	TPM_MEDIUM,
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,		/* 45 */
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_LONG,
	TPM_MEDIUM,		/* 50 */
	TPM_MEDIUM,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 55 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_MEDIUM,		/* 60 */
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_SHORT,
	TPM_SHORT,
	TPM_MEDIUM,		/* 65 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 70 */
	TPM_SHORT,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 75 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_LONG,		/* 80 */
	TPM_UNDEFINED,
	TPM_MEDIUM,
	TPM_LONG,
	TPM_SHORT,
	TPM_UNDEFINED,		/* 85 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 90 */
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_UNDEFINED,		/* 95 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_MEDIUM,		/* 100 */
	TPM_SHORT,
	TPM_SHORT,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 105 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 110 */
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,		/* 115 */
	TPM_SHORT,
	TPM_SHORT,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_LONG,		/* 120 */
	TPM_LONG,
	TPM_MEDIUM,
	TPM_UNDEFINED,
	TPM_SHORT,
	TPM_SHORT,		/* 125 */
	TPM_SHORT,
	TPM_LONG,
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,		/* 130 */
	TPM_MEDIUM,
	TPM_UNDEFINED,
	TPM_SHORT,
	TPM_MEDIUM,
	TPM_UNDEFINED,		/* 135 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 140 */
	TPM_SHORT,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 145 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 150 */
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_SHORT,
	TPM_SHORT,
	TPM_UNDEFINED,		/* 155 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 160 */
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 165 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_LONG,		/* 170 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 175 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_MEDIUM,		/* 180 */
	TPM_SHORT,
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_MEDIUM,		/* 185 */
	TPM_SHORT,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 190 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 195 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 200 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,
	TPM_SHORT,		/* 205 */
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_MEDIUM,		/* 210 */
	TPM_UNDEFINED,
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_UNDEFINED,		/* 215 */
	TPM_MEDIUM,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,
	TPM_SHORT,		/* 220 */
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_UNDEFINED,		/* 225 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 230 */
	TPM_LONG,
	TPM_MEDIUM,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 235 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 240 */
	TPM_UNDEFINED,
	TPM_MEDIUM,
};

static void user_reader_timeout(unsigned long ptr)
{
	struct tpm_chip *chip = (struct tpm_chip *) ptr;

	schedule_work(&chip->work);
}

static void timeout_work(void *ptr)
{
	struct tpm_chip *chip = ptr;

	down(&chip->buffer_mutex);
	atomic_set(&chip->data_pending, 0);
	memset(chip->data_buffer, 0, TPM_BUFSIZE);
	up(&chip->buffer_mutex);
}

/*
 * Returns max number of jiffies to wait
 */
unsigned long tpm_calc_ordinal_duration(struct tpm_chip *chip,
					   u32 ordinal)
{
	int duration_idx = TPM_UNDEFINED;
	int duration = 0;

	if (ordinal < TPM_MAX_ORDINAL)
		duration_idx = tpm_ordinal_duration[ordinal];
	else if ((ordinal & TPM_PROTECTED_ORDINAL_MASK) <
		 TPM_MAX_PROTECTED_ORDINAL)
		duration_idx =
		    tpm_protected_ordinal_duration[ordinal &
						   TPM_PROTECTED_ORDINAL_MASK];

	if (duration_idx != TPM_UNDEFINED)
		duration = chip->vendor.duration[duration_idx];
	if (duration <= 0)
		return 2 * 60 * HZ;
	else
		return duration;
}
EXPORT_SYMBOL_GPL(tpm_calc_ordinal_duration);

/*
 * Internal kernel interface to transmit TPM commands
 */
static ssize_t tpm_transmit(struct tpm_chip *chip, const char *buf,
			    size_t bufsiz)
{
	ssize_t rc;
	u32 count, ordinal;
	unsigned long stop;

	count = be32_to_cpu(*((__be32 *) (buf + 2)));
	ordinal = be32_to_cpu(*((__be32 *) (buf + 6)));
	if (count == 0)
		return -ENODATA;
	if (count > bufsiz) {
		dev_err(chip->dev,
			"invalid count value %x %zx \n", count, bufsiz);
		return -E2BIG;
	}

	down(&chip->tpm_mutex);

	if ((rc = chip->vendor.send(chip, (u8 *) buf, count)) < 0) {
		dev_err(chip->dev,
			"tpm_transmit: tpm_send: error %zd\n", rc);
		goto out;
	}

	if (chip->vendor.irq)
		goto out_recv;

	stop = jiffies + tpm_calc_ordinal_duration(chip, ordinal);
	do {
		u8 status = chip->vendor.status(chip);
		if ((status & chip->vendor.req_complete_mask) ==
		    chip->vendor.req_complete_val)
			goto out_recv;

		if ((status == chip->vendor.req_canceled)) {
			dev_err(chip->dev, "Operation Canceled\n");
			rc = -ECANCELED;
			goto out;
		}

		msleep(TPM_TIMEOUT);	/* CHECK */
		rmb();
	} while (time_before(jiffies, stop));

	chip->vendor.cancel(chip);
	dev_err(chip->dev, "Operation Timed out\n");
	rc = -ETIME;
	goto out;

out_recv:
	rc = chip->vendor.recv(chip, (u8 *) buf, bufsiz);
	if (rc < 0)
		dev_err(chip->dev,
			"tpm_transmit: tpm_recv: error %zd\n", rc);
out:
	up(&chip->tpm_mutex);
	return rc;
}

#define TPM_DIGEST_SIZE 20
#define TPM_ERROR_SIZE 10
#define TPM_RET_CODE_IDX 6
#define TPM_GET_CAP_RET_SIZE_IDX 10
#define TPM_GET_CAP_RET_UINT32_1_IDX 14
#define TPM_GET_CAP_RET_UINT32_2_IDX 18
#define TPM_GET_CAP_RET_UINT32_3_IDX 22
#define TPM_GET_CAP_RET_UINT32_4_IDX 26
#define TPM_GET_CAP_PERM_DISABLE_IDX 16
#define TPM_GET_CAP_PERM_INACTIVE_IDX 18
#define TPM_GET_CAP_RET_BOOL_1_IDX 14
#define TPM_GET_CAP_TEMP_INACTIVE_IDX 16

#define TPM_CAP_IDX 13
#define TPM_CAP_SUBCAP_IDX 21

enum tpm_capabilities {
	TPM_CAP_FLAG = 4,
	TPM_CAP_PROP = 5,
};

enum tpm_sub_capabilities {
	TPM_CAP_PROP_PCR = 0x1,
	TPM_CAP_PROP_MANUFACTURER = 0x3,
	TPM_CAP_FLAG_PERM = 0x8,
	TPM_CAP_FLAG_VOL = 0x9,
	TPM_CAP_PROP_OWNER = 0x11,
	TPM_CAP_PROP_TIS_TIMEOUT = 0x15,
	TPM_CAP_PROP_TIS_DURATION = 0x20,
};

/*
 * This is a semi generic GetCapability command for use
 * with the capability type TPM_CAP_PROP or TPM_CAP_FLAG
 * and their associated sub_capabilities.
 */

static const u8 tpm_cap[] = {
	0, 193,			/* TPM_TAG_RQU_COMMAND */
	0, 0, 0, 22,		/* length */
	0, 0, 0, 101,		/* TPM_ORD_GetCapability */
	0, 0, 0, 0,		/* TPM_CAP_<TYPE> */
	0, 0, 0, 4,		/* TPM_CAP_SUB_<TYPE> size */
	0, 0, 1, 0		/* TPM_CAP_SUB_<TYPE> */
};

static ssize_t transmit_cmd(struct tpm_chip *chip, u8 *data, int len,
			    char *desc)
{
	int err;

	len = tpm_transmit(chip, data, len);
	if (len <  0)
		return len;
	if (len == TPM_ERROR_SIZE) {
		err = be32_to_cpu(*((__be32 *) (data + TPM_RET_CODE_IDX)));
		dev_dbg(chip->dev, "A TPM error (%d) occurred %s\n", err, desc);
		return err;
	}
	return 0;
}

void tpm_gen_interrupt(struct tpm_chip *chip)
{
	u8 data[max_t(int, ARRAY_SIZE(tpm_cap), 30)];
	ssize_t rc;

	memcpy(data, tpm_cap, sizeof(tpm_cap));
	data[TPM_CAP_IDX] = TPM_CAP_PROP;
	data[TPM_CAP_SUBCAP_IDX] = TPM_CAP_PROP_TIS_TIMEOUT;

	rc = transmit_cmd(chip, data, sizeof(data),
			"attempting to determine the timeouts");
}
EXPORT_SYMBOL_GPL(tpm_gen_interrupt);

void tpm_get_timeouts(struct tpm_chip *chip)
{
	u8 data[max_t(int, ARRAY_SIZE(tpm_cap), 30)];
	ssize_t rc;
	u32 timeout;

	memcpy(data, tpm_cap, sizeof(tpm_cap));
	data[TPM_CAP_IDX] = TPM_CAP_PROP;
	data[TPM_CAP_SUBCAP_IDX] = TPM_CAP_PROP_TIS_TIMEOUT;

	rc = transmit_cmd(chip, data, sizeof(data),
			"attempting to determine the timeouts");
	if (rc)
		goto duration;

	if (be32_to_cpu(*((__be32 *) (data + TPM_GET_CAP_RET_SIZE_IDX)))
	    != 4 * sizeof(u32))
		goto duration;

	/* Don't overwrite default if value is 0 */
	timeout =
	    be32_to_cpu(*((__be32 *) (data + TPM_GET_CAP_RET_UINT32_1_IDX)));
	if (timeout)
		chip->vendor.timeout_a = msecs_to_jiffies(timeout);
	timeout =
	    be32_to_cpu(*((__be32 *) (data + TPM_GET_CAP_RET_UINT32_2_IDX)));
	if (timeout)
		chip->vendor.timeout_b = msecs_to_jiffies(timeout);
	timeout =
	    be32_to_cpu(*((__be32 *) (data + TPM_GET_CAP_RET_UINT32_3_IDX)));
	if (timeout)
		chip->vendor.timeout_c = msecs_to_jiffies(timeout);
	timeout =
	    be32_to_cpu(*((__be32 *) (data + TPM_GET_CAP_RET_UINT32_4_IDX)));
	if (timeout)
		chip->vendor.timeout_d = msecs_to_jiffies(timeout);

duration:
	memcpy(data, tpm_cap, sizeof(tpm_cap));
	data[TPM_CAP_IDX] = TPM_CAP_PROP;
	data[TPM_CAP_SUBCAP_IDX] = TPM_CAP_PROP_TIS_DURATION;

	rc = transmit_cmd(chip, data, sizeof(data),
			"attempting to determine the durations");
	if (rc)
		return;

	if (be32_to_cpu(*((__be32 *) (data + TPM_GET_CAP_RET_SIZE_IDX)))
	    != 3 * sizeof(u32))
		return;

	chip->vendor.duration[TPM_SHORT] =
	    msecs_to_jiffies(be32_to_cpu
			     (*((__be32 *) (data +
					    TPM_GET_CAP_RET_UINT32_1_IDX))));
	chip->vendor.duration[TPM_MEDIUM] =
	    msecs_to_jiffies(be32_to_cpu
			     (*((__be32 *) (data +
					    TPM_GET_CAP_RET_UINT32_2_IDX))));
	chip->vendor.duration[TPM_LONG] =
	    msecs_to_jiffies(be32_to_cpu
			     (*((__be32 *) (data +
					    TPM_GET_CAP_RET_UINT32_3_IDX))));
}
EXPORT_SYMBOL_GPL(tpm_get_timeouts);

void tpm_continue_selftest(struct tpm_chip *chip)
{
	u8 data[] = {
		0, 193,			/* TPM_TAG_RQU_COMMAND */
		0, 0, 0, 10,		/* length */
		0, 0, 0, 83,		/* TPM_ORD_GetCapability */
	};

	tpm_transmit(chip, data, sizeof(data));
}
EXPORT_SYMBOL_GPL(tpm_continue_selftest);

ssize_t tpm_show_enabled(struct device * dev, struct device_attribute * attr,
			char *buf)
{
	u8 data[max_t(int, ARRAY_SIZE(tpm_cap), 35)];
	ssize_t rc;

	struct tpm_chip *chip = dev_get_drvdata(dev);
	if (chip == NULL)
		return -ENODEV;

	memcpy(data, tpm_cap, sizeof(tpm_cap));
	data[TPM_CAP_IDX] = TPM_CAP_FLAG;
	data[TPM_CAP_SUBCAP_IDX] = TPM_CAP_FLAG_PERM;

	rc = transmit_cmd(chip, data, sizeof(data),
			"attemtping to determine the permanent state");
	if (rc)
		return 0;
	return sprintf(buf, "%d\n", !data[TPM_GET_CAP_PERM_DISABLE_IDX]);
}
EXPORT_SYMBOL_GPL(tpm_show_enabled);

ssize_t tpm_show_active(struct device * dev, struct device_attribute * attr,
			char *buf)
{
	u8 data[max_t(int, ARRAY_SIZE(tpm_cap), 35)];
	ssize_t rc;

	struct tpm_chip *chip = dev_get_drvdata(dev);
	if (chip == NULL)
		return -ENODEV;

	memcpy(data, tpm_cap, sizeof(tpm_cap));
	data[TPM_CAP_IDX] = TPM_CAP_FLAG;
	data[TPM_CAP_SUBCAP_IDX] = TPM_CAP_FLAG_PERM;

	rc = transmit_cmd(chip, data, sizeof(data),
			"attemtping to determine the permanent state");
	if (rc)
		return 0;
	return sprintf(buf, "%d\n", !data[TPM_GET_CAP_PERM_INACTIVE_IDX]);
}
EXPORT_SYMBOL_GPL(tpm_show_active);

ssize_t tpm_show_owned(struct device * dev, struct device_attribute * attr,
			char *buf)
{
	u8 data[sizeof(tpm_cap)];
	ssize_t rc;

	struct tpm_chip *chip = dev_get_drvdata(dev);
	if (chip == NULL)
		return -ENODEV;

	memcpy(data, tpm_cap, sizeof(tpm_cap));
	data[TPM_CAP_IDX] = TPM_CAP_PROP;
	data[TPM_CAP_SUBCAP_IDX] = TPM_CAP_PROP_OWNER;

	rc = transmit_cmd(chip, data, sizeof(data),
			"attempting to determine the owner state");
	if (rc)
		return 0;
	return sprintf(buf, "%d\n", data[TPM_GET_CAP_RET_BOOL_1_IDX]);
}
EXPORT_SYMBOL_GPL(tpm_show_owned);

ssize_t tpm_show_temp_deactivated(struct device * dev,
				struct device_attribute * attr, char *buf)
{
	u8 data[sizeof(tpm_cap)];
	ssize_t rc;

	struct tpm_chip *chip = dev_get_drvdata(dev);
	if (chip == NULL)
		return -ENODEV;

	memcpy(data, tpm_cap, sizeof(tpm_cap));
	data[TPM_CAP_IDX] = TPM_CAP_FLAG;
	data[TPM_CAP_SUBCAP_IDX] = TPM_CAP_FLAG_VOL;

	rc = transmit_cmd(chip, data, sizeof(data),
			"attempting to determine the temporary state");
	if (rc)
		return 0;
	return sprintf(buf, "%d\n", data[TPM_GET_CAP_TEMP_INACTIVE_IDX]);
}
EXPORT_SYMBOL_GPL(tpm_show_temp_deactivated);

static const u8 pcrread[] = {
	0, 193,			/* TPM_TAG_RQU_COMMAND */
	0, 0, 0, 14,		/* length */
	0, 0, 0, 21,		/* TPM_ORD_PcrRead */
	0, 0, 0, 0		/* PCR index */
};

ssize_t tpm_show_pcrs(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	u8 data[max_t(int, max(ARRAY_SIZE(tpm_cap), ARRAY_SIZE(pcrread)), 30)];
	ssize_t rc;
	int i, j, num_pcrs;
	__be32 index;
	char *str = buf;

	struct tpm_chip *chip = dev_get_drvdata(dev);
	if (chip == NULL)
		return -ENODEV;

	memcpy(data, tpm_cap, sizeof(tpm_cap));
	data[TPM_CAP_IDX] = TPM_CAP_PROP;
	data[TPM_CAP_SUBCAP_IDX] = TPM_CAP_PROP_PCR;

	rc = transmit_cmd(chip, data, sizeof(data),
			"attempting to determine the number of PCRS");
	if (rc)
		return 0;

	num_pcrs = be32_to_cpu(*((__be32 *) (data + 14)));
	for (i = 0; i < num_pcrs; i++) {
		memcpy(data, pcrread, sizeof(pcrread));
		index = cpu_to_be32(i);
		memcpy(data + 10, &index, 4);
		rc = transmit_cmd(chip, data, sizeof(data),
				"attempting to read a PCR");
		if (rc)
			goto out;
		str += sprintf(str, "PCR-%02d: ", i);
		for (j = 0; j < TPM_DIGEST_SIZE; j++)
			str += sprintf(str, "%02X ", *(data + 10 + j));
		str += sprintf(str, "\n");
	}
out:
	return str - buf;
}
EXPORT_SYMBOL_GPL(tpm_show_pcrs);

#define  READ_PUBEK_RESULT_SIZE 314
static const u8 readpubek[] = {
	0, 193,			/* TPM_TAG_RQU_COMMAND */
	0, 0, 0, 30,		/* length */
	0, 0, 0, 124,		/* TPM_ORD_ReadPubek */
};

ssize_t tpm_show_pubek(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	u8 *data;
	ssize_t err;
	int i, rc;
	char *str = buf;

	struct tpm_chip *chip = dev_get_drvdata(dev);
	if (chip == NULL)
		return -ENODEV;

	data = kzalloc(READ_PUBEK_RESULT_SIZE, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memcpy(data, readpubek, sizeof(readpubek));

	err = transmit_cmd(chip, data, READ_PUBEK_RESULT_SIZE,
			"attempting to read the PUBEK");
	if (err)
		goto out;

	/* 
	   ignore header 10 bytes
	   algorithm 32 bits (1 == RSA )
	   encscheme 16 bits
	   sigscheme 16 bits
	   parameters (RSA 12->bytes: keybit, #primes, expbit)  
	   keylenbytes 32 bits
	   256 byte modulus
	   ignore checksum 20 bytes
	 */

	str +=
	    sprintf(str,
		    "Algorithm: %02X %02X %02X %02X\nEncscheme: %02X %02X\n"
		    "Sigscheme: %02X %02X\nParameters: %02X %02X %02X %02X"
		    " %02X %02X %02X %02X %02X %02X %02X %02X\n"
		    "Modulus length: %d\nModulus: \n",
		    data[10], data[11], data[12], data[13], data[14],
		    data[15], data[16], data[17], data[22], data[23],
		    data[24], data[25], data[26], data[27], data[28],
		    data[29], data[30], data[31], data[32], data[33],
		    be32_to_cpu(*((__be32 *) (data + 34))));

	for (i = 0; i < 256; i++) {
		str += sprintf(str, "%02X ", data[i + 38]);
		if ((i + 1) % 16 == 0)
			str += sprintf(str, "\n");
	}
out:
	rc = str - buf;
	kfree(data);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_show_pubek);

#define CAP_VERSION_1_1 6
#define CAP_VERSION_1_2 0x1A
#define CAP_VERSION_IDX 13
static const u8 cap_version[] = {
	0, 193,			/* TPM_TAG_RQU_COMMAND */
	0, 0, 0, 18,		/* length */
	0, 0, 0, 101,		/* TPM_ORD_GetCapability */
	0, 0, 0, 0,
	0, 0, 0, 0
};

ssize_t tpm_show_caps(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	u8 data[max_t(int, max(ARRAY_SIZE(tpm_cap), ARRAY_SIZE(cap_version)), 30)];
	ssize_t rc;
	char *str = buf;

	struct tpm_chip *chip = dev_get_drvdata(dev);
	if (chip == NULL)
		return -ENODEV;

	memcpy(data, tpm_cap, sizeof(tpm_cap));
	data[TPM_CAP_IDX] = TPM_CAP_PROP;
	data[TPM_CAP_SUBCAP_IDX] = TPM_CAP_PROP_MANUFACTURER;

	rc = transmit_cmd(chip, data, sizeof(data),
			"attempting to determine the manufacturer");
	if (rc)
		return 0;

	str += sprintf(str, "Manufacturer: 0x%x\n",
		       be32_to_cpu(*((__be32 *) (data + TPM_GET_CAP_RET_UINT32_1_IDX))));

	memcpy(data, cap_version, sizeof(cap_version));
	data[CAP_VERSION_IDX] = CAP_VERSION_1_1;
	rc = transmit_cmd(chip, data, sizeof(data),
			"attempting to determine the 1.1 version");
	if (rc)
		goto out;

	str += sprintf(str,
		       "TCG version: %d.%d\nFirmware version: %d.%d\n",
		       (int) data[14], (int) data[15], (int) data[16],
		       (int) data[17]);

out:
	return str - buf;
}
EXPORT_SYMBOL_GPL(tpm_show_caps);

ssize_t tpm_show_caps_1_2(struct device * dev,
			  struct device_attribute * attr, char *buf)
{
	u8 data[max_t(int, max(ARRAY_SIZE(tpm_cap), ARRAY_SIZE(cap_version)), 30)];
	ssize_t len;
	char *str = buf;

	struct tpm_chip *chip = dev_get_drvdata(dev);
	if (chip == NULL)
		return -ENODEV;

	memcpy(data, tpm_cap, sizeof(tpm_cap));
	data[TPM_CAP_IDX] = TPM_CAP_PROP;
	data[TPM_CAP_SUBCAP_IDX] = TPM_CAP_PROP_MANUFACTURER;

	if ((len = tpm_transmit(chip, data, sizeof(data))) <=
	    TPM_ERROR_SIZE) {
		dev_dbg(chip->dev, "A TPM error (%d) occurred "
			"attempting to determine the manufacturer\n",
			be32_to_cpu(*((__be32 *) (data + TPM_RET_CODE_IDX))));
		return 0;
	}

	str += sprintf(str, "Manufacturer: 0x%x\n",
		       be32_to_cpu(*((__be32 *) (data + TPM_GET_CAP_RET_UINT32_1_IDX))));

	memcpy(data, cap_version, sizeof(cap_version));
	data[CAP_VERSION_IDX] = CAP_VERSION_1_2;

	if ((len = tpm_transmit(chip, data, sizeof(data))) <=
	    TPM_ERROR_SIZE) {
		dev_err(chip->dev, "A TPM error (%d) occurred "
			"attempting to determine the 1.2 version\n",
			be32_to_cpu(*((__be32 *) (data + TPM_RET_CODE_IDX))));
		goto out;
	}
	str += sprintf(str,
		       "TCG version: %d.%d\nFirmware version: %d.%d\n",
		       (int) data[16], (int) data[17], (int) data[18],
		       (int) data[19]);

out:
	return str - buf;
}
EXPORT_SYMBOL_GPL(tpm_show_caps_1_2);

ssize_t tpm_store_cancel(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	if (chip == NULL)
		return 0;

	chip->vendor.cancel(chip);
	return count;
}
EXPORT_SYMBOL_GPL(tpm_store_cancel);

/*
 * Device file system interface to the TPM
 */
int tpm_open(struct inode *inode, struct file *file)
{
	int rc = 0, minor = iminor(inode);
	struct tpm_chip *chip = NULL, *pos;

	spin_lock(&driver_lock);

	list_for_each_entry(pos, &tpm_chip_list, list) {
		if (pos->vendor.miscdev.minor == minor) {
			chip = pos;
			break;
		}
	}

	if (chip == NULL) {
		rc = -ENODEV;
		goto err_out;
	}

	if (chip->num_opens) {
		dev_dbg(chip->dev, "Another process owns this TPM\n");
		rc = -EBUSY;
		goto err_out;
	}

	chip->num_opens++;
	get_device(chip->dev);

	spin_unlock(&driver_lock);

	chip->data_buffer = kmalloc(TPM_BUFSIZE * sizeof(u8), GFP_KERNEL);
	if (chip->data_buffer == NULL) {
		chip->num_opens--;
		put_device(chip->dev);
		return -ENOMEM;
	}

	atomic_set(&chip->data_pending, 0);

	file->private_data = chip;
	return 0;

err_out:
	spin_unlock(&driver_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_open);

int tpm_release(struct inode *inode, struct file *file)
{
	struct tpm_chip *chip = file->private_data;

	spin_lock(&driver_lock);
	file->private_data = NULL;
	chip->num_opens--;
	del_singleshot_timer_sync(&chip->user_read_timer);
	flush_scheduled_work();
	atomic_set(&chip->data_pending, 0);
	put_device(chip->dev);
	kfree(chip->data_buffer);
	spin_unlock(&driver_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(tpm_release);

ssize_t tpm_write(struct file *file, const char __user *buf,
		  size_t size, loff_t *off)
{
	struct tpm_chip *chip = file->private_data;
	int in_size = size, out_size;

	/* cannot perform a write until the read has cleared
	   either via tpm_read or a user_read_timer timeout */
	while (atomic_read(&chip->data_pending) != 0)
		msleep(TPM_TIMEOUT);

	down(&chip->buffer_mutex);

	if (in_size > TPM_BUFSIZE)
		in_size = TPM_BUFSIZE;

	if (copy_from_user
	    (chip->data_buffer, (void __user *) buf, in_size)) {
		up(&chip->buffer_mutex);
		return -EFAULT;
	}

	/* atomic tpm command send and result receive */
	out_size = tpm_transmit(chip, chip->data_buffer, TPM_BUFSIZE);

	atomic_set(&chip->data_pending, out_size);
	up(&chip->buffer_mutex);

	/* Set a timeout by which the reader must come claim the result */
	mod_timer(&chip->user_read_timer, jiffies + (60 * HZ));

	return in_size;
}
EXPORT_SYMBOL_GPL(tpm_write);

ssize_t tpm_read(struct file *file, char __user *buf,
		 size_t size, loff_t *off)
{
	struct tpm_chip *chip = file->private_data;
	int ret_size;

	del_singleshot_timer_sync(&chip->user_read_timer);
	flush_scheduled_work();
	ret_size = atomic_read(&chip->data_pending);
	atomic_set(&chip->data_pending, 0);
	if (ret_size > 0) {	/* relay data */
		if (size < ret_size)
			ret_size = size;

		down(&chip->buffer_mutex);
		if (copy_to_user(buf, chip->data_buffer, ret_size))
			ret_size = -EFAULT;
		up(&chip->buffer_mutex);
	}

	return ret_size;
}
EXPORT_SYMBOL_GPL(tpm_read);

void tpm_remove_hardware(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);

	if (chip == NULL) {
		dev_err(dev, "No device data found\n");
		return;
	}

	spin_lock(&driver_lock);

	list_del(&chip->list);

	spin_unlock(&driver_lock);

	dev_set_drvdata(dev, NULL);
	misc_deregister(&chip->vendor.miscdev);
	kfree(chip->vendor.miscdev.name);

	sysfs_remove_group(&dev->kobj, chip->vendor.attr_group);
	tpm_bios_log_teardown(chip->bios_dir);

	clear_bit(chip->dev_num, dev_mask);

	kfree(chip);

	put_device(dev);
}
EXPORT_SYMBOL_GPL(tpm_remove_hardware);

static u8 savestate[] = {
	0, 193,			/* TPM_TAG_RQU_COMMAND */
	0, 0, 0, 10,		/* blob length (in bytes) */
	0, 0, 0, 152		/* TPM_ORD_SaveState */
};

/*
 * We are about to suspend. Save the TPM state
 * so that it can be restored.
 */
int tpm_pm_suspend(struct device *dev, pm_message_t pm_state)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	if (chip == NULL)
		return -ENODEV;

	tpm_transmit(chip, savestate, sizeof(savestate));
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

	return 0;
}
EXPORT_SYMBOL_GPL(tpm_pm_resume);

/*
 * Called from tpm_<specific>.c probe function only for devices 
 * the driver has determined it should claim.  Prior to calling
 * this function the specific probe function has called pci_enable_device
 * upon errant exit from this function specific probe function should call
 * pci_disable_device
 */
struct tpm_chip *tpm_register_hardware(struct device *dev, const struct tpm_vendor_specific
				       *entry)
{
#define DEVNAME_SIZE 7

	char *devname;
	struct tpm_chip *chip;

	/* Driver specific per-device data */
	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return NULL;

	init_MUTEX(&chip->buffer_mutex);
	init_MUTEX(&chip->tpm_mutex);
	INIT_LIST_HEAD(&chip->list);

	INIT_WORK(&chip->work, timeout_work, chip);

	init_timer(&chip->user_read_timer);
	chip->user_read_timer.function = user_reader_timeout;
	chip->user_read_timer.data = (unsigned long) chip;

	memcpy(&chip->vendor, entry, sizeof(struct tpm_vendor_specific));

	chip->dev_num = find_first_zero_bit(dev_mask, TPM_NUM_DEVICES);

	if (chip->dev_num >= TPM_NUM_DEVICES) {
		dev_err(dev, "No available tpm device numbers\n");
		kfree(chip);
		return NULL;
	} else if (chip->dev_num == 0)
		chip->vendor.miscdev.minor = TPM_MINOR;
	else
		chip->vendor.miscdev.minor = MISC_DYNAMIC_MINOR;

	set_bit(chip->dev_num, dev_mask);

	devname = kmalloc(DEVNAME_SIZE, GFP_KERNEL);
	scnprintf(devname, DEVNAME_SIZE, "%s%d", "tpm", chip->dev_num);
	chip->vendor.miscdev.name = devname;

	chip->vendor.miscdev.dev = dev;
	chip->dev = get_device(dev);

	if (misc_register(&chip->vendor.miscdev)) {
		dev_err(chip->dev,
			"unable to misc_register %s, minor %d\n",
			chip->vendor.miscdev.name,
			chip->vendor.miscdev.minor);
		put_device(dev);
		clear_bit(chip->dev_num, dev_mask);
		kfree(chip);
		kfree(devname);
		return NULL;
	}

	spin_lock(&driver_lock);

	dev_set_drvdata(dev, chip);

	list_add(&chip->list, &tpm_chip_list);

	spin_unlock(&driver_lock);

	if (sysfs_create_group(&dev->kobj, chip->vendor.attr_group)) {
		list_del(&chip->list);
		put_device(dev);
		clear_bit(chip->dev_num, dev_mask);
		kfree(chip);
		kfree(devname);
		return NULL;
	}

	chip->bios_dir = tpm_bios_log_setup(devname);

	return chip;
}
EXPORT_SYMBOL_GPL(tpm_register_hardware);

MODULE_AUTHOR("Leendert van Doorn (leendert@watson.ibm.com)");
MODULE_DESCRIPTION("TPM Driver");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
