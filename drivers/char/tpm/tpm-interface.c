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
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/freezer.h>

#include "tpm.h"
#include "tpm_eventlog.h"

#define TPM_MAX_ORDINAL 243
#define TSC_MAX_ORDINAL 12
#define TPM_PROTECTED_COMMAND 0x00
#define TPM_CONNECTION_COMMAND 0x40

/*
 * Bug workaround - some TPM's don't flush the most
 * recently changed pcr on suspend, so force the flush
 * with an extend to the selected _unused_ non-volatile pcr.
 */
static int tpm_suspend_pcr;
module_param_named(suspend_pcr, tpm_suspend_pcr, uint, 0644);
MODULE_PARM_DESC(suspend_pcr,
		 "PCR to use for dummy writes to faciltate flush on suspend.");

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

/*
 * Returns max number of jiffies to wait
 */
unsigned long tpm_calc_ordinal_duration(struct tpm_chip *chip,
					   u32 ordinal)
{
	int duration_idx = TPM_UNDEFINED;
	int duration = 0;
	u8 category = (ordinal >> 24) & 0xFF;

	if ((category == TPM_PROTECTED_COMMAND && ordinal < TPM_MAX_ORDINAL) ||
	    (category == TPM_CONNECTION_COMMAND && ordinal < TSC_MAX_ORDINAL))
		duration_idx = tpm_ordinal_duration[ordinal];

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
ssize_t tpm_transmit(struct tpm_chip *chip, const char *buf,
		     size_t bufsiz)
{
	ssize_t rc;
	u32 count, ordinal;
	unsigned long stop;

	if (bufsiz > TPM_BUFSIZE)
		bufsiz = TPM_BUFSIZE;

	count = be32_to_cpu(*((__be32 *) (buf + 2)));
	ordinal = be32_to_cpu(*((__be32 *) (buf + 6)));
	if (count == 0)
		return -ENODATA;
	if (count > bufsiz) {
		dev_err(chip->dev,
			"invalid count value %x %zx\n", count, bufsiz);
		return -E2BIG;
	}

	mutex_lock(&chip->tpm_mutex);

	rc = chip->ops->send(chip, (u8 *) buf, count);
	if (rc < 0) {
		dev_err(chip->dev,
			"tpm_transmit: tpm_send: error %zd\n", rc);
		goto out;
	}

	if (chip->vendor.irq)
		goto out_recv;

	stop = jiffies + tpm_calc_ordinal_duration(chip, ordinal);
	do {
		u8 status = chip->ops->status(chip);
		if ((status & chip->ops->req_complete_mask) ==
		    chip->ops->req_complete_val)
			goto out_recv;

		if (chip->ops->req_canceled(chip, status)) {
			dev_err(chip->dev, "Operation Canceled\n");
			rc = -ECANCELED;
			goto out;
		}

		msleep(TPM_TIMEOUT);	/* CHECK */
		rmb();
	} while (time_before(jiffies, stop));

	chip->ops->cancel(chip);
	dev_err(chip->dev, "Operation Timed out\n");
	rc = -ETIME;
	goto out;

out_recv:
	rc = chip->ops->recv(chip, (u8 *) buf, bufsiz);
	if (rc < 0)
		dev_err(chip->dev,
			"tpm_transmit: tpm_recv: error %zd\n", rc);
out:
	mutex_unlock(&chip->tpm_mutex);
	return rc;
}

#define TPM_DIGEST_SIZE 20
#define TPM_RET_CODE_IDX 6

static ssize_t transmit_cmd(struct tpm_chip *chip, struct tpm_cmd_t *cmd,
			    int len, const char *desc)
{
	int err;

	len = tpm_transmit(chip, (u8 *) cmd, len);
	if (len <  0)
		return len;
	else if (len < TPM_HEADER_SIZE)
		return -EFAULT;

	err = be32_to_cpu(cmd->header.out.return_code);
	if (err != 0 && desc)
		dev_err(chip->dev, "A TPM error (%d) occurred %s\n", err, desc);

	return err;
}

#define TPM_INTERNAL_RESULT_SIZE 200
#define TPM_ORD_GET_CAP cpu_to_be32(101)
#define TPM_ORD_GET_RANDOM cpu_to_be32(70)

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

#define TPM_ORD_STARTUP cpu_to_be32(153)
#define TPM_ST_CLEAR cpu_to_be16(1)
#define TPM_ST_STATE cpu_to_be16(2)
#define TPM_ST_DEACTIVATED cpu_to_be16(3)
static const struct tpm_input_header tpm_startup_header = {
	.tag = TPM_TAG_RQU_COMMAND,
	.length = cpu_to_be32(12),
	.ordinal = TPM_ORD_STARTUP
};

static int tpm_startup(struct tpm_chip *chip, __be16 startup_type)
{
	struct tpm_cmd_t start_cmd;
	start_cmd.header.in = tpm_startup_header;
	start_cmd.params.startup_in.startup_type = startup_type;
	return transmit_cmd(chip, &start_cmd, TPM_INTERNAL_RESULT_SIZE,
			    "attempting to start the TPM");
}

int tpm_get_timeouts(struct tpm_chip *chip)
{
	struct tpm_cmd_t tpm_cmd;
	struct timeout_t *timeout_cap;
	struct duration_t *duration_cap;
	ssize_t rc;
	u32 timeout;
	unsigned int scale = 1;

	tpm_cmd.header.in = tpm_getcap_header;
	tpm_cmd.params.getcap_in.cap = TPM_CAP_PROP;
	tpm_cmd.params.getcap_in.subcap_size = cpu_to_be32(4);
	tpm_cmd.params.getcap_in.subcap = TPM_CAP_PROP_TIS_TIMEOUT;
	rc = transmit_cmd(chip, &tpm_cmd, TPM_INTERNAL_RESULT_SIZE, NULL);

	if (rc == TPM_ERR_INVALID_POSTINIT) {
		/* The TPM is not started, we are the first to talk to it.
		   Execute a startup command. */
		dev_info(chip->dev, "Issuing TPM_STARTUP");
		if (tpm_startup(chip, TPM_ST_CLEAR))
			return rc;

		tpm_cmd.header.in = tpm_getcap_header;
		tpm_cmd.params.getcap_in.cap = TPM_CAP_PROP;
		tpm_cmd.params.getcap_in.subcap_size = cpu_to_be32(4);
		tpm_cmd.params.getcap_in.subcap = TPM_CAP_PROP_TIS_TIMEOUT;
		rc = transmit_cmd(chip, &tpm_cmd, TPM_INTERNAL_RESULT_SIZE,
				  NULL);
	}
	if (rc) {
		dev_err(chip->dev,
			"A TPM error (%zd) occurred attempting to determine the timeouts\n",
			rc);
		goto duration;
	}

	if (be32_to_cpu(tpm_cmd.header.out.return_code) != 0 ||
	    be32_to_cpu(tpm_cmd.header.out.length)
	    != sizeof(tpm_cmd.header.out) + sizeof(u32) + 4 * sizeof(u32))
		return -EINVAL;

	timeout_cap = &tpm_cmd.params.getcap_out.cap.timeout;
	/* Don't overwrite default if value is 0 */
	timeout = be32_to_cpu(timeout_cap->a);
	if (timeout && timeout < 1000) {
		/* timeouts in msec rather usec */
		scale = 1000;
		chip->vendor.timeout_adjusted = true;
	}
	if (timeout)
		chip->vendor.timeout_a = usecs_to_jiffies(timeout * scale);
	timeout = be32_to_cpu(timeout_cap->b);
	if (timeout)
		chip->vendor.timeout_b = usecs_to_jiffies(timeout * scale);
	timeout = be32_to_cpu(timeout_cap->c);
	if (timeout)
		chip->vendor.timeout_c = usecs_to_jiffies(timeout * scale);
	timeout = be32_to_cpu(timeout_cap->d);
	if (timeout)
		chip->vendor.timeout_d = usecs_to_jiffies(timeout * scale);

duration:
	tpm_cmd.header.in = tpm_getcap_header;
	tpm_cmd.params.getcap_in.cap = TPM_CAP_PROP;
	tpm_cmd.params.getcap_in.subcap_size = cpu_to_be32(4);
	tpm_cmd.params.getcap_in.subcap = TPM_CAP_PROP_TIS_DURATION;

	rc = transmit_cmd(chip, &tpm_cmd, TPM_INTERNAL_RESULT_SIZE,
			"attempting to determine the durations");
	if (rc)
		return rc;

	if (be32_to_cpu(tpm_cmd.header.out.return_code) != 0 ||
	    be32_to_cpu(tpm_cmd.header.out.length)
	    != sizeof(tpm_cmd.header.out) + sizeof(u32) + 3 * sizeof(u32))
		return -EINVAL;

	duration_cap = &tpm_cmd.params.getcap_out.cap.duration;
	chip->vendor.duration[TPM_SHORT] =
	    usecs_to_jiffies(be32_to_cpu(duration_cap->tpm_short));
	chip->vendor.duration[TPM_MEDIUM] =
	    usecs_to_jiffies(be32_to_cpu(duration_cap->tpm_medium));
	chip->vendor.duration[TPM_LONG] =
	    usecs_to_jiffies(be32_to_cpu(duration_cap->tpm_long));

	/* The Broadcom BCM0102 chipset in a Dell Latitude D820 gets the above
	 * value wrong and apparently reports msecs rather than usecs. So we
	 * fix up the resulting too-small TPM_SHORT value to make things work.
	 * We also scale the TPM_MEDIUM and -_LONG values by 1000.
	 */
	if (chip->vendor.duration[TPM_SHORT] < (HZ / 100)) {
		chip->vendor.duration[TPM_SHORT] = HZ;
		chip->vendor.duration[TPM_MEDIUM] *= 1000;
		chip->vendor.duration[TPM_LONG] *= 1000;
		chip->vendor.duration_adjusted = true;
		dev_info(chip->dev, "Adjusting TPM timeout parameters.");
	}
	return 0;
}
EXPORT_SYMBOL_GPL(tpm_get_timeouts);

#define TPM_ORD_CONTINUE_SELFTEST 83
#define CONTINUE_SELFTEST_RESULT_SIZE 10

static struct tpm_input_header continue_selftest_header = {
	.tag = TPM_TAG_RQU_COMMAND,
	.length = cpu_to_be32(10),
	.ordinal = cpu_to_be32(TPM_ORD_CONTINUE_SELFTEST),
};

/**
 * tpm_continue_selftest -- run TPM's selftest
 * @chip: TPM chip to use
 *
 * Returns 0 on success, < 0 in case of fatal error or a value > 0 representing
 * a TPM error code.
 */
static int tpm_continue_selftest(struct tpm_chip *chip)
{
	int rc;
	struct tpm_cmd_t cmd;

	cmd.header.in = continue_selftest_header;
	rc = transmit_cmd(chip, &cmd, CONTINUE_SELFTEST_RESULT_SIZE,
			  "continue selftest");
	return rc;
}

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

int tpm_pcr_read_dev(struct tpm_chip *chip, int pcr_idx, u8 *res_buf)
{
	int rc;
	struct tpm_cmd_t cmd;

	cmd.header.in = pcrread_header;
	cmd.params.pcrread_in.pcr_idx = cpu_to_be32(pcr_idx);
	rc = transmit_cmd(chip, &cmd, READ_PCR_RESULT_SIZE,
			  "attempting to read a pcr value");

	if (rc == 0)
		memcpy(res_buf, cmd.params.pcrread_out.pcr_result,
		       TPM_DIGEST_SIZE);
	return rc;
}

/**
 * tpm_pcr_read - read a pcr value
 * @chip_num:	tpm idx # or ANY
 * @pcr_idx:	pcr idx to retrieve
 * @res_buf:	TPM_PCR value
 *		size of res_buf is 20 bytes (or NULL if you don't care)
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
	rc = tpm_pcr_read_dev(chip, pcr_idx, res_buf);
	tpm_chip_put(chip);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_pcr_read);

/**
 * tpm_pcr_extend - extend pcr value with hash
 * @chip_num:	tpm idx # or AN&
 * @pcr_idx:	pcr idx to extend
 * @hash:	hash value used to extend pcr value
 *
 * The TPM driver should be built-in, but for whatever reason it
 * isn't, protect against the chip disappearing, by incrementing
 * the module usage count.
 */
#define TPM_ORD_PCR_EXTEND cpu_to_be32(20)
#define EXTEND_PCR_RESULT_SIZE 34
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
	cmd.params.pcrextend_in.pcr_idx = cpu_to_be32(pcr_idx);
	memcpy(cmd.params.pcrextend_in.hash, hash, TPM_DIGEST_SIZE);
	rc = transmit_cmd(chip, &cmd, EXTEND_PCR_RESULT_SIZE,
			  "attempting extend a PCR value");

	tpm_chip_put(chip);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_pcr_extend);

/**
 * tpm_do_selftest - have the TPM continue its selftest and wait until it
 *                   can receive further commands
 * @chip: TPM chip to use
 *
 * Returns 0 on success, < 0 in case of fatal error or a value > 0 representing
 * a TPM error code.
 */
int tpm_do_selftest(struct tpm_chip *chip)
{
	int rc;
	unsigned int loops;
	unsigned int delay_msec = 100;
	unsigned long duration;
	struct tpm_cmd_t cmd;

	duration = tpm_calc_ordinal_duration(chip, TPM_ORD_CONTINUE_SELFTEST);

	loops = jiffies_to_msecs(duration) / delay_msec;

	rc = tpm_continue_selftest(chip);
	/* This may fail if there was no TPM driver during a suspend/resume
	 * cycle; some may return 10 (BAD_ORDINAL), others 28 (FAILEDSELFTEST)
	 */
	if (rc)
		return rc;

	do {
		/* Attempt to read a PCR value */
		cmd.header.in = pcrread_header;
		cmd.params.pcrread_in.pcr_idx = cpu_to_be32(0);
		rc = tpm_transmit(chip, (u8 *) &cmd, READ_PCR_RESULT_SIZE);
		/* Some buggy TPMs will not respond to tpm_tis_ready() for
		 * around 300ms while the self test is ongoing, keep trying
		 * until the self test duration expires. */
		if (rc == -ETIME) {
			dev_info(chip->dev, HW_ERR "TPM command timed out during continue self test");
			msleep(delay_msec);
			continue;
		}

		if (rc < TPM_HEADER_SIZE)
			return -EFAULT;

		rc = be32_to_cpu(cmd.header.out.return_code);
		if (rc == TPM_ERR_DISABLED || rc == TPM_ERR_DEACTIVATED) {
			dev_info(chip->dev,
				 "TPM is disabled/deactivated (0x%X)\n", rc);
			/* TPM is disabled and/or deactivated; driver can
			 * proceed and TPM does handle commands for
			 * suspend/resume correctly
			 */
			return 0;
		}
		if (rc != TPM_WARN_DOING_SELFTEST)
			return rc;
		msleep(delay_msec);
	} while (--loops > 0);

	return rc;
}
EXPORT_SYMBOL_GPL(tpm_do_selftest);

int tpm_send(u32 chip_num, void *cmd, size_t buflen)
{
	struct tpm_chip *chip;
	int rc;

	chip = tpm_chip_find_get(chip_num);
	if (chip == NULL)
		return -ENODEV;

	rc = transmit_cmd(chip, cmd, buflen, "attempting tpm_cmd");

	tpm_chip_put(chip);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_send);

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

int wait_for_tpm_stat(struct tpm_chip *chip, u8 mask, unsigned long timeout,
		      wait_queue_head_t *queue, bool check_cancel)
{
	unsigned long stop;
	long rc;
	u8 status;
	bool canceled = false;

	/* check current status */
	status = chip->ops->status(chip);
	if ((status & mask) == mask)
		return 0;

	stop = jiffies + timeout;

	if (chip->vendor.irq) {
again:
		timeout = stop - jiffies;
		if ((long)timeout <= 0)
			return -ETIME;
		rc = wait_event_interruptible_timeout(*queue,
			wait_for_tpm_stat_cond(chip, mask, check_cancel,
					       &canceled),
			timeout);
		if (rc > 0) {
			if (canceled)
				return -ECANCELED;
			return 0;
		}
		if (rc == -ERESTARTSYS && freezing(current)) {
			clear_thread_flag(TIF_SIGPENDING);
			goto again;
		}
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
EXPORT_SYMBOL_GPL(wait_for_tpm_stat);

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

	tpm_dev_del_device(chip);
	tpm_sysfs_del_device(chip);
	tpm_remove_ppi(&dev->kobj);
	tpm_bios_log_teardown(chip->bios_dir);

	/* write it this way to be explicit (chip->dev == dev) */
	put_device(chip->dev);
}
EXPORT_SYMBOL_GPL(tpm_remove_hardware);

#define TPM_ORD_SAVESTATE cpu_to_be32(152)
#define SAVESTATE_RESULT_SIZE 10

static struct tpm_input_header savestate_header = {
	.tag = TPM_TAG_RQU_COMMAND,
	.length = cpu_to_be32(10),
	.ordinal = TPM_ORD_SAVESTATE
};

/*
 * We are about to suspend. Save the TPM state
 * so that it can be restored.
 */
int tpm_pm_suspend(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	struct tpm_cmd_t cmd;
	int rc, try;

	u8 dummy_hash[TPM_DIGEST_SIZE] = { 0 };

	if (chip == NULL)
		return -ENODEV;

	/* for buggy tpm, flush pcrs with extend to selected dummy */
	if (tpm_suspend_pcr) {
		cmd.header.in = pcrextend_header;
		cmd.params.pcrextend_in.pcr_idx = cpu_to_be32(tpm_suspend_pcr);
		memcpy(cmd.params.pcrextend_in.hash, dummy_hash,
		       TPM_DIGEST_SIZE);
		rc = transmit_cmd(chip, &cmd, EXTEND_PCR_RESULT_SIZE,
				  "extending dummy pcr before suspend");
	}

	/* now do the actual savestate */
	for (try = 0; try < TPM_RETRY; try++) {
		cmd.header.in = savestate_header;
		rc = transmit_cmd(chip, &cmd, SAVESTATE_RESULT_SIZE, NULL);

		/*
		 * If the TPM indicates that it is too busy to respond to
		 * this command then retry before giving up.  It can take
		 * several seconds for this TPM to be ready.
		 *
		 * This can happen if the TPM has already been sent the
		 * SaveState command before the driver has loaded.  TCG 1.2
		 * specification states that any communication after SaveState
		 * may cause the TPM to invalidate previously saved state.
		 */
		if (rc != TPM_WARN_RETRY)
			break;
		msleep(TPM_TIMEOUT_RETRY);
	}

	if (rc)
		dev_err(chip->dev,
			"Error (%d) sending savestate before suspend\n", rc);
	else if (try > 0)
		dev_warn(chip->dev, "TPM savestate took %dms\n",
			 try * TPM_TIMEOUT_RETRY);

	return rc;
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

#define TPM_GETRANDOM_RESULT_SIZE	18
static struct tpm_input_header tpm_getrandom_header = {
	.tag = TPM_TAG_RQU_COMMAND,
	.length = cpu_to_be32(14),
	.ordinal = TPM_ORD_GET_RANDOM
};

/**
 * tpm_get_random() - Get random bytes from the tpm's RNG
 * @chip_num: A specific chip number for the request or TPM_ANY_NUM
 * @out: destination buffer for the random bytes
 * @max: the max number of bytes to write to @out
 *
 * Returns < 0 on error and the number of bytes read on success
 */
int tpm_get_random(u32 chip_num, u8 *out, size_t max)
{
	struct tpm_chip *chip;
	struct tpm_cmd_t tpm_cmd;
	u32 recd, num_bytes = min_t(u32, max, TPM_MAX_RNG_DATA);
	int err, total = 0, retries = 5;
	u8 *dest = out;

	chip = tpm_chip_find_get(chip_num);
	if (chip == NULL)
		return -ENODEV;

	if (!out || !num_bytes || max > TPM_MAX_RNG_DATA)
		return -EINVAL;

	do {
		tpm_cmd.header.in = tpm_getrandom_header;
		tpm_cmd.params.getrandom_in.num_bytes = cpu_to_be32(num_bytes);

		err = transmit_cmd(chip, &tpm_cmd,
				   TPM_GETRANDOM_RESULT_SIZE + num_bytes,
				   "attempting get random");
		if (err)
			break;

		recd = be32_to_cpu(tpm_cmd.params.getrandom_out.rng_data_len);
		memcpy(dest, tpm_cmd.params.getrandom_out.rng_data, recd);

		dest += recd;
		total += recd;
		num_bytes -= recd;
	} while (retries-- && total < max);

	return total ? total : -EIO;
}
EXPORT_SYMBOL_GPL(tpm_get_random);

/* In case vendor provided release function, call it too.*/

void tpm_dev_vendor_release(struct tpm_chip *chip)
{
	if (!chip)
		return;

	clear_bit(chip->dev_num, dev_mask);
}
EXPORT_SYMBOL_GPL(tpm_dev_vendor_release);


/*
 * Once all references to platform device are down to 0,
 * release all allocated structures.
 */
static void tpm_dev_release(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);

	if (!chip)
		return;

	tpm_dev_vendor_release(chip);

	chip->release(dev);
	kfree(chip);
}

/*
 * Called from tpm_<specific>.c probe function only for devices
 * the driver has determined it should claim.  Prior to calling
 * this function the specific probe function has called pci_enable_device
 * upon errant exit from this function specific probe function should call
 * pci_disable_device
 */
struct tpm_chip *tpm_register_hardware(struct device *dev,
				       const struct tpm_class_ops *ops)
{
	struct tpm_chip *chip;

	/* Driver specific per-device data */
	chip = kzalloc(sizeof(*chip), GFP_KERNEL);

	if (chip == NULL)
		return NULL;

	mutex_init(&chip->tpm_mutex);
	INIT_LIST_HEAD(&chip->list);

	chip->ops = ops;
	chip->dev_num = find_first_zero_bit(dev_mask, TPM_NUM_DEVICES);

	if (chip->dev_num >= TPM_NUM_DEVICES) {
		dev_err(dev, "No available tpm device numbers\n");
		goto out_free;
	}

	set_bit(chip->dev_num, dev_mask);

	scnprintf(chip->devname, sizeof(chip->devname), "%s%d", "tpm",
		  chip->dev_num);

	chip->dev = get_device(dev);
	chip->release = dev->release;
	dev->release = tpm_dev_release;
	dev_set_drvdata(dev, chip);

	if (tpm_dev_add_device(chip))
		goto put_device;

	if (tpm_sysfs_add_device(chip))
		goto del_misc;

	if (tpm_add_ppi(&dev->kobj))
		goto del_sysfs;

	chip->bios_dir = tpm_bios_log_setup(chip->devname);

	/* Make chip available */
	spin_lock(&driver_lock);
	list_add_rcu(&chip->list, &tpm_chip_list);
	spin_unlock(&driver_lock);

	return chip;

del_sysfs:
	tpm_sysfs_del_device(chip);
del_misc:
	tpm_dev_del_device(chip);
put_device:
	put_device(chip->dev);
out_free:
	kfree(chip);
	return NULL;
}
EXPORT_SYMBOL_GPL(tpm_register_hardware);

MODULE_AUTHOR("Leendert van Doorn (leendert@watson.ibm.com)");
MODULE_DESCRIPTION("TPM Driver");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
