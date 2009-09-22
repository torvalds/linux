/*
 * Copyright (C) 2004 IBM Corporation
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

#include <linux/poll.h>
#include <linux/mutex.h>
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

static void timeout_work(struct work_struct *work)
{
	struct tpm_chip *chip = container_of(work, struct tpm_chip, work);

	mutex_lock(&chip->buffer_mutex);
	atomic_set(&chip->data_pending, 0);
	memset(chip->data_buffer, 0, TPM_BUFSIZE);
	mutex_unlock(&chip->buffer_mutex);
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

	mutex_lock(&chip->tpm_mutex);

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
	mutex_unlock(&chip->tpm_mutex);
	return rc;
}

#define TPM_DIGEST_SIZE 20
#define TPM_ERROR_SIZE 10
#define TPM_RET_CODE_IDX 6

enum tpm_capabilities {
	TPM_CAP_FLAG = cpu_to_be32(4),
	TPM_CAP_PROP = cpu_to_be32(5),
	CAP_VERSION_1_1 = cpu_to_be32(0x06),
	CAP_VERSION_1_2 = cpu_to_be32(0x1A)
};

enum tpm_sub_capabilities {
	TPM_CAP_PROP_PCR = cpu_to_be32(0x101),
	TPM_CAP_PROP_MANUFACTURER = cpu_to_be32(0x103),
	TPM_CAP_FLAG_PERM = cpu_to_be32(0x108),
	TPM_CAP_FLAG_VOL = cpu_to_be32(0x109),
	TPM_CAP_PROP_OWNER = cpu_to_be32(0x111),
	TPM_CAP_PROP_TIS_TIMEOUT = cpu_to_be32(0x115),
	TPM_CAP_PROP_TIS_DURATION = cpu_to_be32(0x120),

};

static ssize_t transmit_cmd(struct tpm_chip *chip, struct tpm_cmd_t *cmd,
			    int len, const char *desc)
{
	int err;

	len = tpm_transmit(chip,(u8 *) cmd, len);
	if (len <  0)
		return len;
	if (len == TPM_ERROR_SIZE) {
		err = be32_to_cpu(cmd->header.out.return_code);
		dev_dbg(chip->dev, "A TPM error (%d) occurred %s\n", err, desc);
		return err;
	}
	return 0;
}

#define TPM_INTERNAL_RESULT_SIZE 200
#define TPM_TAG_RQU_COMMAND cpu_to_be16(193)
#define TPM_ORD_GET_CAP cpu_to_be32(101)

static const struct tpm_input_header tpm_getcap_header = {
	.tag = TPM_TAG_RQU_COMMAND,
	.length = cpu_to_be32(22),
	.ordinal = TPM_ORD_GET_CAP
};

ssize_t tpm_getcap(struct device *dev, __be32 subcap_id, cap_t *cap,
		   const char *desc)
{
	struct tpm_cmd_t tpm_cmd;
	int rc;
	struct tpm_chip *chip = dev_get_drvdata(dev);

	tpm_cmd.header.in = tpm_getcap_header;
	if (subcap_id == CAP_VERSION_1_1 || subcap_id == CAP_VERSION_1_2) {
		tpm_cmd.params.getcap_in.cap = subcap_id;
		/*subcap field not necessary */
		tpm_cmd.params.getcap_in.subcap_size = cpu_to_be32(0);
		tpm_cmd.header.in.length -= cpu_to_be32(sizeof(__be32));
	} else {
		if (subcap_id == TPM_CAP_FLAG_PERM ||
		    subcap_id == TPM_CAP_FLAG_VOL)
			tpm_cmd.params.getcap_in.cap = TPM_CAP_FLAG;
		else
			tpm_cmd.params.getcap_in.cap = TPM_CAP_PROP;
		tpm_cmd.params.getcap_in.subcap_size = cpu_to_be32(4);
		tpm_cmd.params.getcap_in.subcap = subcap_id;
	}
	rc = transmit_cmd(chip, &tpm_cmd, TPM_INTERNAL_RESULT_SIZE, desc);
	if (!rc)
		*cap = tpm_cmd.params.getcap_out.cap;
	return rc;
}

void tpm_gen_interrupt(struct tpm_chip *chip)
{
	struct	tpm_cmd_t tpm_cmd;
	ssize_t rc;

	tpm_cmd.header.in = tpm_getcap_header;
	tpm_cmd.params.getcap_in.cap = TPM_CAP_PROP;
	tpm_cmd.params.getcap_in.subcap_size = cpu_to_be32(4);
	tpm_cmd.params.getcap_in.subcap = TPM_CAP_PROP_TIS_TIMEOUT;

	rc = transmit_cmd(chip, &tpm_cmd, TPM_INTERNAL_RESULT_SIZE,
			"attempting to determine the timeouts");
}
EXPORT_SYMBOL_GPL(tpm_gen_interrupt);

void tpm_get_timeouts(struct tpm_chip *chip)
{
	struct tpm_cmd_t tpm_cmd;
	struct timeout_t *timeout_cap;
	struct duration_t *duration_cap;
	ssize_t rc;
	u32 timeout;

	tpm_cmd.header.in = tpm_getcap_header;
	tpm_cmd.params.getcap_in.cap = TPM_CAP_PROP;
	tpm_cmd.params.getcap_in.subcap_size = cpu_to_be32(4);
	tpm_cmd.params.getcap_in.subcap = TPM_CAP_PROP_TIS_TIMEOUT;

	rc = transmit_cmd(chip, &tpm_cmd, TPM_INTERNAL_RESULT_SIZE,
			"attempting to determine the timeouts");
	if (rc)
		goto duration;

	if (be32_to_cpu(tpm_cmd.header.out.length)
	    != 4 * sizeof(u32))
		goto duration;

	timeout_cap = &tpm_cmd.params.getcap_out.cap.timeout;
	/* Don't overwrite default if value is 0 */
	timeout = be32_to_cpu(timeout_cap->a);
	if (timeout)
		chip->vendor.timeout_a = usecs_to_jiffies(timeout);
	timeout = be32_to_cpu(timeout_cap->b);
	if (timeout)
		chip->vendor.timeout_b = usecs_to_jiffies(timeout);
	timeout = be32_to_cpu(timeout_cap->c);
	if (timeout)
		chip->vendor.timeout_c = usecs_to_jiffies(timeout);
	timeout = be32_to_cpu(timeout_cap->d);
	if (timeout)
		chip->vendor.timeout_d = usecs_to_jiffies(timeout);

duration:
	tpm_cmd.header.in = tpm_getcap_header;
	tpm_cmd.params.getcap_in.cap = TPM_CAP_PROP;
	tpm_cmd.params.getcap_in.subcap_size = cpu_to_be32(4);
	tpm_cmd.params.getcap_in.subcap = TPM_CAP_PROP_TIS_DURATION;

	rc = transmit_cmd(chip, &tpm_cmd, TPM_INTERNAL_RESULT_SIZE,
			"attempting to determine the durations");
	if (rc)
		return;

	if (be32_to_cpu(tpm_cmd.header.out.return_code)
	    != 3 * sizeof(u32))
		return;
	duration_cap = &tpm_cmd.params.getcap_out.cap.duration;
	chip->vendor.duration[TPM_SHORT] =
	    usecs_to_jiffies(be32_to_cpu(duration_cap->tpm_short));
	/* The Broadcom BCM0102 chipset in a Dell Latitude D820 gets the above
	 * value wrong and apparently reports msecs rather than usecs. So we
	 * fix up the resulting too-small TPM_SHORT value to make things work.
	 */
	if (chip->vendor.duration[TPM_SHORT] < (HZ/100))
		chip->vendor.duration[TPM_SHORT] = HZ;

	chip->vendor.duration[TPM_MEDIUM] =
	    usecs_to_jiffies(be32_to_cpu(duration_cap->tpm_medium));
	chip->vendor.duration[TPM_LONG] =
	    usecs_to_jiffies(be32_to_cpu(duration_cap->tpm_long));
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
	cap_t cap;
	ssize_t rc;

	rc = tpm_getcap(dev, TPM_CAP_FLAG_PERM, &cap,
			 "attempting to determine the permanent enabled state");
	if (rc)
		return 0;

	rc = sprintf(buf, "%d\n", !cap.perm_flags.disable);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_show_enabled);

ssize_t tpm_show_active(struct device * dev, struct device_attribute * attr,
			char *buf)
{
	cap_t cap;
	ssize_t rc;

	rc = tpm_getcap(dev, TPM_CAP_FLAG_PERM, &cap,
			 "attempting to determine the permanent active state");
	if (rc)
		return 0;

	rc = sprintf(buf, "%d\n", !cap.perm_flags.deactivated);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_show_active);

ssize_t tpm_show_owned(struct device * dev, struct device_attribute * attr,
			char *buf)
{
	cap_t cap;
	ssize_t rc;

	rc = tpm_getcap(dev, TPM_CAP_PROP_OWNER, &cap,
			 "attempting to determine the owner state");
	if (rc)
		return 0;

	rc = sprintf(buf, "%d\n", cap.owned);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_show_owned);

ssize_t tpm_show_temp_deactivated(struct device * dev,
				struct device_attribute * attr, char *buf)
{
	cap_t cap;
	ssize_t rc;

	rc = tpm_getcap(dev, TPM_CAP_FLAG_VOL, &cap,
			 "attempting to determine the temporary state");
	if (rc)
		return 0;

	rc = sprintf(buf, "%d\n", cap.stclear_flags.deactivated);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_show_temp_deactivated);

/*
 * tpm_chip_find_get - return tpm_chip for given chip number
 */
static struct tpm_chip *tpm_chip_find_get(int chip_num)
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

#define TPM_ORDINAL_PCRREAD cpu_to_be32(21)
#define READ_PCR_RESULT_SIZE 30
static struct tpm_input_header pcrread_header = {
	.tag = TPM_TAG_RQU_COMMAND,
	.length = cpu_to_be32(14),
	.ordinal = TPM_ORDINAL_PCRREAD
};

int __tpm_pcr_read(struct tpm_chip *chip, int pcr_idx, u8 *res_buf)
{
	int rc;
	struct tpm_cmd_t cmd;

	cmd.header.in = pcrread_header;
	cmd.params.pcrread_in.pcr_idx = cpu_to_be32(pcr_idx);
	BUILD_BUG_ON(cmd.header.in.length > READ_PCR_RESULT_SIZE);
	rc = transmit_cmd(chip, &cmd, cmd.header.in.length,
			  "attempting to read a pcr value");

	if (rc == 0)
		memcpy(res_buf, cmd.params.pcrread_out.pcr_result,
		       TPM_DIGEST_SIZE);
	return rc;
}

/**
 * tpm_pcr_read - read a pcr value
 * @chip_num: 	tpm idx # or ANY
 * @pcr_idx:	pcr idx to retrieve
 * @res_buf: 	TPM_PCR value
 * 		size of res_buf is 20 bytes (or NULL if you don't care)
 *
 * The TPM driver should be built-in, but for whatever reason it
 * isn't, protect against the chip disappearing, by incrementing
 * the module usage count.
 */
int tpm_pcr_read(u32 chip_num, int pcr_idx, u8 *res_buf)
{
	struct tpm_chip *chip;
	int rc;

	chip = tpm_chip_find_get(chip_num);
	if (chip == NULL)
		return -ENODEV;
	rc = __tpm_pcr_read(chip, pcr_idx, res_buf);
	module_put(chip->dev->driver->owner);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_pcr_read);

/**
 * tpm_pcr_extend - extend pcr value with hash
 * @chip_num: 	tpm idx # or AN&
 * @pcr_idx:	pcr idx to extend
 * @hash: 	hash value used to extend pcr value
 *
 * The TPM driver should be built-in, but for whatever reason it
 * isn't, protect against the chip disappearing, by incrementing
 * the module usage count.
 */
#define TPM_ORD_PCR_EXTEND cpu_to_be32(20)
#define EXTEND_PCR_SIZE 34
static struct tpm_input_header pcrextend_header = {
	.tag = TPM_TAG_RQU_COMMAND,
	.length = cpu_to_be32(34),
	.ordinal = TPM_ORD_PCR_EXTEND
};

int tpm_pcr_extend(u32 chip_num, int pcr_idx, const u8 *hash)
{
	struct tpm_cmd_t cmd;
	int rc;
	struct tpm_chip *chip;

	chip = tpm_chip_find_get(chip_num);
	if (chip == NULL)
		return -ENODEV;

	cmd.header.in = pcrextend_header;
	BUILD_BUG_ON(be32_to_cpu(cmd.header.in.length) > EXTEND_PCR_SIZE);
	cmd.params.pcrextend_in.pcr_idx = cpu_to_be32(pcr_idx);
	memcpy(cmd.params.pcrextend_in.hash, hash, TPM_DIGEST_SIZE);
	rc = transmit_cmd(chip, &cmd, cmd.header.in.length,
			  "attempting extend a PCR value");

	module_put(chip->dev->driver->owner);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_pcr_extend);

ssize_t tpm_show_pcrs(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	cap_t cap;
	u8 digest[TPM_DIGEST_SIZE];
	ssize_t rc;
	int i, j, num_pcrs;
	char *str = buf;
	struct tpm_chip *chip = dev_get_drvdata(dev);

	rc = tpm_getcap(dev, TPM_CAP_PROP_PCR, &cap,
			"attempting to determine the number of PCRS");
	if (rc)
		return 0;

	num_pcrs = be32_to_cpu(cap.num_pcrs);
	for (i = 0; i < num_pcrs; i++) {
		rc = __tpm_pcr_read(chip, i, digest);
		if (rc)
			break;
		str += sprintf(str, "PCR-%02d: ", i);
		for (j = 0; j < TPM_DIGEST_SIZE; j++)
			str += sprintf(str, "%02X ", digest[j]);
		str += sprintf(str, "\n");
	}
	return str - buf;
}
EXPORT_SYMBOL_GPL(tpm_show_pcrs);

#define  READ_PUBEK_RESULT_SIZE 314
#define TPM_ORD_READPUBEK cpu_to_be32(124)
struct tpm_input_header tpm_readpubek_header = {
	.tag = TPM_TAG_RQU_COMMAND,
	.length = cpu_to_be32(30),
	.ordinal = TPM_ORD_READPUBEK
};

ssize_t tpm_show_pubek(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	u8 *data;
	struct tpm_cmd_t tpm_cmd;
	ssize_t err;
	int i, rc;
	char *str = buf;

	struct tpm_chip *chip = dev_get_drvdata(dev);

	tpm_cmd.header.in = tpm_readpubek_header;
	err = transmit_cmd(chip, &tpm_cmd, READ_PUBEK_RESULT_SIZE,
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
	data = tpm_cmd.params.readpubek_out_buffer;
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
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_show_pubek);


ssize_t tpm_show_caps(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	cap_t cap;
	ssize_t rc;
	char *str = buf;

	rc = tpm_getcap(dev, TPM_CAP_PROP_MANUFACTURER, &cap,
			"attempting to determine the manufacturer");
	if (rc)
		return 0;
	str += sprintf(str, "Manufacturer: 0x%x\n",
		       be32_to_cpu(cap.manufacturer_id));

	rc = tpm_getcap(dev, CAP_VERSION_1_1, &cap,
		        "attempting to determine the 1.1 version");
	if (rc)
		return 0;
	str += sprintf(str,
		       "TCG version: %d.%d\nFirmware version: %d.%d\n",
		       cap.tpm_version.Major, cap.tpm_version.Minor,
		       cap.tpm_version.revMajor, cap.tpm_version.revMinor);
	return str - buf;
}
EXPORT_SYMBOL_GPL(tpm_show_caps);

ssize_t tpm_show_caps_1_2(struct device * dev,
			  struct device_attribute * attr, char *buf)
{
	cap_t cap;
	ssize_t rc;
	char *str = buf;

	rc = tpm_getcap(dev, TPM_CAP_PROP_MANUFACTURER, &cap,
			"attempting to determine the manufacturer");
	if (rc)
		return 0;
	str += sprintf(str, "Manufacturer: 0x%x\n",
		       be32_to_cpu(cap.manufacturer_id));
	rc = tpm_getcap(dev, CAP_VERSION_1_2, &cap,
			 "attempting to determine the 1.2 version");
	if (rc)
		return 0;
	str += sprintf(str,
		       "TCG version: %d.%d\nFirmware version: %d.%d\n",
		       cap.tpm_version_1_2.Major, cap.tpm_version_1_2.Minor,
		       cap.tpm_version_1_2.revMajor,
		       cap.tpm_version_1_2.revMinor);
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
 *
 * It's assured that the chip will be opened just once,
 * by the check of is_open variable, which is protected
 * by driver_lock.
 */
int tpm_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct tpm_chip *chip = NULL, *pos;

	rcu_read_lock();
	list_for_each_entry_rcu(pos, &tpm_chip_list, list) {
		if (pos->vendor.miscdev.minor == minor) {
			chip = pos;
			get_device(chip->dev);
			break;
		}
	}
	rcu_read_unlock();

	if (!chip)
		return -ENODEV;

	if (test_and_set_bit(0, &chip->is_open)) {
		dev_dbg(chip->dev, "Another process owns this TPM\n");
		put_device(chip->dev);
		return -EBUSY;
	}

	chip->data_buffer = kmalloc(TPM_BUFSIZE * sizeof(u8), GFP_KERNEL);
	if (chip->data_buffer == NULL) {
		clear_bit(0, &chip->is_open);
		put_device(chip->dev);
		return -ENOMEM;
	}

	atomic_set(&chip->data_pending, 0);

	file->private_data = chip;
	return 0;
}
EXPORT_SYMBOL_GPL(tpm_open);

/*
 * Called on file close
 */
int tpm_release(struct inode *inode, struct file *file)
{
	struct tpm_chip *chip = file->private_data;

	del_singleshot_timer_sync(&chip->user_read_timer);
	flush_scheduled_work();
	file->private_data = NULL;
	atomic_set(&chip->data_pending, 0);
	kfree(chip->data_buffer);
	clear_bit(0, &chip->is_open);
	put_device(chip->dev);
	return 0;
}
EXPORT_SYMBOL_GPL(tpm_release);

ssize_t tpm_write(struct file *file, const char __user *buf,
		  size_t size, loff_t *off)
{
	struct tpm_chip *chip = file->private_data;
	size_t in_size = size, out_size;

	/* cannot perform a write until the read has cleared
	   either via tpm_read or a user_read_timer timeout */
	while (atomic_read(&chip->data_pending) != 0)
		msleep(TPM_TIMEOUT);

	mutex_lock(&chip->buffer_mutex);

	if (in_size > TPM_BUFSIZE)
		in_size = TPM_BUFSIZE;

	if (copy_from_user
	    (chip->data_buffer, (void __user *) buf, in_size)) {
		mutex_unlock(&chip->buffer_mutex);
		return -EFAULT;
	}

	/* atomic tpm command send and result receive */
	out_size = tpm_transmit(chip, chip->data_buffer, TPM_BUFSIZE);

	atomic_set(&chip->data_pending, out_size);
	mutex_unlock(&chip->buffer_mutex);

	/* Set a timeout by which the reader must come claim the result */
	mod_timer(&chip->user_read_timer, jiffies + (60 * HZ));

	return in_size;
}
EXPORT_SYMBOL_GPL(tpm_write);

ssize_t tpm_read(struct file *file, char __user *buf,
		 size_t size, loff_t *off)
{
	struct tpm_chip *chip = file->private_data;
	ssize_t ret_size;

	del_singleshot_timer_sync(&chip->user_read_timer);
	flush_scheduled_work();
	ret_size = atomic_read(&chip->data_pending);
	atomic_set(&chip->data_pending, 0);
	if (ret_size > 0) {	/* relay data */
		if (size < ret_size)
			ret_size = size;

		mutex_lock(&chip->buffer_mutex);
		if (copy_to_user(buf, chip->data_buffer, ret_size))
			ret_size = -EFAULT;
		mutex_unlock(&chip->buffer_mutex);
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
	list_del_rcu(&chip->list);
	spin_unlock(&driver_lock);
	synchronize_rcu();

	misc_deregister(&chip->vendor.miscdev);
	sysfs_remove_group(&dev->kobj, chip->vendor.attr_group);
	tpm_bios_log_teardown(chip->bios_dir);

	/* write it this way to be explicit (chip->dev == dev) */
	put_device(chip->dev);
}
EXPORT_SYMBOL_GPL(tpm_remove_hardware);

/*
 * We are about to suspend. Save the TPM state
 * so that it can be restored.
 */
int tpm_pm_suspend(struct device *dev, pm_message_t pm_state)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	u8 savestate[] = {
		0, 193,		/* TPM_TAG_RQU_COMMAND */
		0, 0, 0, 10,	/* blob length (in bytes) */
		0, 0, 0, 152	/* TPM_ORD_SaveState */
	};

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

/* In case vendor provided release function, call it too.*/

void tpm_dev_vendor_release(struct tpm_chip *chip)
{
	if (chip->vendor.release)
		chip->vendor.release(chip->dev);

	clear_bit(chip->dev_num, dev_mask);
	kfree(chip->vendor.miscdev.name);
}
EXPORT_SYMBOL_GPL(tpm_dev_vendor_release);


/*
 * Once all references to platform device are down to 0,
 * release all allocated structures.
 */
void tpm_dev_release(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);

	tpm_dev_vendor_release(chip);

	chip->release(dev);
	kfree(chip);
}
EXPORT_SYMBOL_GPL(tpm_dev_release);

/*
 * Called from tpm_<specific>.c probe function only for devices 
 * the driver has determined it should claim.  Prior to calling
 * this function the specific probe function has called pci_enable_device
 * upon errant exit from this function specific probe function should call
 * pci_disable_device
 */
struct tpm_chip *tpm_register_hardware(struct device *dev,
					const struct tpm_vendor_specific *entry)
{
#define DEVNAME_SIZE 7

	char *devname;
	struct tpm_chip *chip;

	/* Driver specific per-device data */
	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	devname = kmalloc(DEVNAME_SIZE, GFP_KERNEL);

	if (chip == NULL || devname == NULL)
		goto out_free;

	mutex_init(&chip->buffer_mutex);
	mutex_init(&chip->tpm_mutex);
	INIT_LIST_HEAD(&chip->list);

	INIT_WORK(&chip->work, timeout_work);

	setup_timer(&chip->user_read_timer, user_reader_timeout,
			(unsigned long)chip);

	memcpy(&chip->vendor, entry, sizeof(struct tpm_vendor_specific));

	chip->dev_num = find_first_zero_bit(dev_mask, TPM_NUM_DEVICES);

	if (chip->dev_num >= TPM_NUM_DEVICES) {
		dev_err(dev, "No available tpm device numbers\n");
		goto out_free;
	} else if (chip->dev_num == 0)
		chip->vendor.miscdev.minor = TPM_MINOR;
	else
		chip->vendor.miscdev.minor = MISC_DYNAMIC_MINOR;

	set_bit(chip->dev_num, dev_mask);

	scnprintf(devname, DEVNAME_SIZE, "%s%d", "tpm", chip->dev_num);
	chip->vendor.miscdev.name = devname;

	chip->vendor.miscdev.parent = dev;
	chip->dev = get_device(dev);
	chip->release = dev->release;
	dev->release = tpm_dev_release;
	dev_set_drvdata(dev, chip);

	if (misc_register(&chip->vendor.miscdev)) {
		dev_err(chip->dev,
			"unable to misc_register %s, minor %d\n",
			chip->vendor.miscdev.name,
			chip->vendor.miscdev.minor);
		put_device(chip->dev);
		return NULL;
	}

	if (sysfs_create_group(&dev->kobj, chip->vendor.attr_group)) {
		misc_deregister(&chip->vendor.miscdev);
		put_device(chip->dev);

		return NULL;
	}

	chip->bios_dir = tpm_bios_log_setup(devname);

	/* Make chip available */
	spin_lock(&driver_lock);
	list_add_rcu(&chip->list, &tpm_chip_list);
	spin_unlock(&driver_lock);

	return chip;

out_free:
	kfree(chip);
	kfree(devname);
	return NULL;
}
EXPORT_SYMBOL_GPL(tpm_register_hardware);

MODULE_AUTHOR("Leendert van Doorn (leendert@watson.ibm.com)");
MODULE_DESCRIPTION("TPM Driver");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
