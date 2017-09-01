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
ssize_t tpm_transmit(struct tpm_chip *chip, const u8 *buf, size_t bufsiz,
		     unsigned int flags)
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
		dev_err(&chip->dev,
			"invalid count value %x %zx\n", count, bufsiz);
		return -E2BIG;
	}

	if (!(flags & TPM_TRANSMIT_UNLOCKED))
		mutex_lock(&chip->tpm_mutex);

	rc = chip->ops->send(chip, (u8 *) buf, count);
	if (rc < 0) {
		dev_err(&chip->dev,
			"tpm_transmit: tpm_send: error %zd\n", rc);
		goto out;
	}

	if (chip->vendor.irq)
		goto out_recv;

	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		stop = jiffies + tpm2_calc_ordinal_duration(chip, ordinal);
	else
		stop = jiffies + tpm_calc_ordinal_duration(chip, ordinal);
	do {
		u8 status = chip->ops->status(chip);
		if ((status & chip->ops->req_complete_mask) ==
		    chip->ops->req_complete_val)
			goto out_recv;

		if (chip->ops->req_canceled(chip, status)) {
			dev_err(&chip->dev, "Operation Canceled\n");
			rc = -ECANCELED;
			goto out;
		}

		msleep(TPM_TIMEOUT);	/* CHECK */
		rmb();
	} while (time_before(jiffies, stop));

	chip->ops->cancel(chip);
	dev_err(&chip->dev, "Operation Timed out\n");
	rc = -ETIME;
	goto out;

out_recv:
	rc = chip->ops->recv(chip, (u8 *) buf, bufsiz);
	if (rc < 0)
		dev_err(&chip->dev,
			"tpm_transmit: tpm_recv: error %zd\n", rc);
out:
	if (!(flags & TPM_TRANSMIT_UNLOCKED))
		mutex_unlock(&chip->tpm_mutex);
	return rc;
}

#define TPM_DIGEST_SIZE 20
#define TPM_RET_CODE_IDX 6

ssize_t tpm_transmit_cmd(struct tpm_chip *chip, const void *cmd,
			 int len, unsigned int flags, const char *desc)
{
	const struct tpm_output_header *header;
	int err;

	len = tpm_transmit(chip, (const u8 *)cmd, len, flags);
	if (len <  0)
		return len;
	else if (len < TPM_HEADER_SIZE)
		return -EFAULT;

	header = cmd;

	err = be32_to_cpu(header->return_code);
	if (err != 0 && desc)
		dev_err(&chip->dev, "A TPM error (%d) occurred %s\n", err,
			desc);

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
	rc = tpm_transmit_cmd(chip, &tpm_cmd, TPM_INTERNAL_RESULT_SIZE, 0,
			      desc);
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

	rc = tpm_transmit_cmd(chip, &tpm_cmd, TPM_INTERNAL_RESULT_SIZE, 0,
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
	return tpm_transmit_cmd(chip, &start_cmd, TPM_INTERNAL_RESULT_SIZE, 0,
				"attempting to start the TPM");
}

int tpm_get_timeouts(struct tpm_chip *chip)
{
	struct tpm_cmd_t tpm_cmd;
	unsigned long new_timeout[4];
	unsigned long old_timeout[4];
	struct duration_t *duration_cap;
	ssize_t rc;

	tpm_cmd.header.in = tpm_getcap_header;
	tpm_cmd.params.getcap_in.cap = TPM_CAP_PROP;
	tpm_cmd.params.getcap_in.subcap_size = cpu_to_be32(4);
	tpm_cmd.params.getcap_in.subcap = TPM_CAP_PROP_TIS_TIMEOUT;
	rc = tpm_transmit_cmd(chip, &tpm_cmd, TPM_INTERNAL_RESULT_SIZE, 0,
			      NULL);

	if (rc == TPM_ERR_INVALID_POSTINIT) {
		/* The TPM is not started, we are the first to talk to it.
		   Execute a startup command. */
		dev_info(&chip->dev, "Issuing TPM_STARTUP");
		if (tpm_startup(chip, TPM_ST_CLEAR))
			return rc;

		tpm_cmd.header.in = tpm_getcap_header;
		tpm_cmd.params.getcap_in.cap = TPM_CAP_PROP;
		tpm_cmd.params.getcap_in.subcap_size = cpu_to_be32(4);
		tpm_cmd.params.getcap_in.subcap = TPM_CAP_PROP_TIS_TIMEOUT;
		rc = tpm_transmit_cmd(chip, &tpm_cmd, TPM_INTERNAL_RESULT_SIZE,
				      0, NULL);
	}
	if (rc) {
		dev_err(&chip->dev,
			"A TPM error (%zd) occurred attempting to determine the timeouts\n",
			rc);
		goto duration;
	}

	if (be32_to_cpu(tpm_cmd.header.out.return_code) != 0 ||
	    be32_to_cpu(tpm_cmd.header.out.length)
	    != sizeof(tpm_cmd.header.out) + sizeof(u32) + 4 * sizeof(u32))
		return -EINVAL;

	old_timeout[0] = be32_to_cpu(tpm_cmd.params.getcap_out.cap.timeout.a);
	old_timeout[1] = be32_to_cpu(tpm_cmd.params.getcap_out.cap.timeout.b);
	old_timeout[2] = be32_to_cpu(tpm_cmd.params.getcap_out.cap.timeout.c);
	old_timeout[3] = be32_to_cpu(tpm_cmd.params.getcap_out.cap.timeout.d);
	memcpy(new_timeout, old_timeout, sizeof(new_timeout));

	/*
	 * Provide ability for vendor overrides of timeout values in case
	 * of misreporting.
	 */
	if (chip->ops->update_timeouts != NULL)
		chip->vendor.timeout_adjusted =
			chip->ops->update_timeouts(chip, new_timeout);

	if (!chip->vendor.timeout_adjusted) {
		/* Don't overwrite default if value is 0 */
		if (new_timeout[0] != 0 && new_timeout[0] < 1000) {
			int i;

			/* timeouts in msec rather usec */
			for (i = 0; i != ARRAY_SIZE(new_timeout); i++)
				new_timeout[i] *= 1000;
			chip->vendor.timeout_adjusted = true;
		}
	}

	/* Report adjusted timeouts */
	if (chip->vendor.timeout_adjusted) {
		dev_info(&chip->dev,
			 HW_ERR "Adjusting reported timeouts: A %lu->%luus B %lu->%luus C %lu->%luus D %lu->%luus\n",
			 old_timeout[0], new_timeout[0],
			 old_timeout[1], new_timeout[1],
			 old_timeout[2], new_timeout[2],
			 old_timeout[3], new_timeout[3]);
	}

	chip->vendor.timeout_a = usecs_to_jiffies(new_timeout[0]);
	chip->vendor.timeout_b = usecs_to_jiffies(new_timeout[1]);
	chip->vendor.timeout_c = usecs_to_jiffies(new_timeout[2]);
	chip->vendor.timeout_d = usecs_to_jiffies(new_timeout[3]);

duration:
	tpm_cmd.header.in = tpm_getcap_header;
	tpm_cmd.params.getcap_in.cap = TPM_CAP_PROP;
	tpm_cmd.params.getcap_in.subcap_size = cpu_to_be32(4);
	tpm_cmd.params.getcap_in.subcap = TPM_CAP_PROP_TIS_DURATION;

	rc = tpm_transmit_cmd(chip, &tpm_cmd, TPM_INTERNAL_RESULT_SIZE, 0,
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
		dev_info(&chip->dev, "Adjusting TPM timeout parameters.");
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
	rc = tpm_transmit_cmd(chip, &cmd, CONTINUE_SELFTEST_RESULT_SIZE, 0,
			      "continue selftest");
	return rc;
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
	rc = tpm_transmit_cmd(chip, &cmd, READ_PCR_RESULT_SIZE, 0,
			      "attempting to read a pcr value");

	if (rc == 0)
		memcpy(res_buf, cmd.params.pcrread_out.pcr_result,
		       TPM_DIGEST_SIZE);
	return rc;
}

/**
 * tpm_is_tpm2 - is the chip a TPM2 chip?
 * @chip_num:	tpm idx # or ANY
 *
 * Returns < 0 on error, and 1 or 0 on success depending whether the chip
 * is a TPM2 chip.
 */
int tpm_is_tpm2(u32 chip_num)
{
	struct tpm_chip *chip;
	int rc;

	chip = tpm_chip_find_get(chip_num);
	if (chip == NULL)
		return -ENODEV;

	rc = (chip->flags & TPM_CHIP_FLAG_TPM2) != 0;

	tpm_put_ops(chip);

	return rc;
}
EXPORT_SYMBOL_GPL(tpm_is_tpm2);

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
	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		rc = tpm2_pcr_read(chip, pcr_idx, res_buf);
	else
		rc = tpm_pcr_read_dev(chip, pcr_idx, res_buf);
	tpm_put_ops(chip);
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

	if (chip->flags & TPM_CHIP_FLAG_TPM2) {
		rc = tpm2_pcr_extend(chip, pcr_idx, hash);
		tpm_put_ops(chip);
		return rc;
	}

	cmd.header.in = pcrextend_header;
	cmd.params.pcrextend_in.pcr_idx = cpu_to_be32(pcr_idx);
	memcpy(cmd.params.pcrextend_in.hash, hash, TPM_DIGEST_SIZE);
	rc = tpm_transmit_cmd(chip, &cmd, EXTEND_PCR_RESULT_SIZE, 0,
			      "attempting extend a PCR value");

	tpm_put_ops(chip);
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
		rc = tpm_transmit(chip, (u8 *) &cmd, READ_PCR_RESULT_SIZE, 0);
		/* Some buggy TPMs will not respond to tpm_tis_ready() for
		 * around 300ms while the self test is ongoing, keep trying
		 * until the self test duration expires. */
		if (rc == -ETIME) {
			dev_info(
			    &chip->dev, HW_ERR
			    "TPM command timed out during continue self test");
			msleep(delay_msec);
			continue;
		}

		if (rc < TPM_HEADER_SIZE)
			return -EFAULT;

		rc = be32_to_cpu(cmd.header.out.return_code);
		if (rc == TPM_ERR_DISABLED || rc == TPM_ERR_DEACTIVATED) {
			dev_info(&chip->dev,
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

	rc = tpm_transmit_cmd(chip, cmd, buflen, 0, "attempting tpm_cmd");

	tpm_put_ops(chip);
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

	if (chip->flags & TPM_CHIP_FLAG_TPM2) {
		tpm2_shutdown(chip, TPM2_SU_STATE);
		return 0;
	}

	/* for buggy tpm, flush pcrs with extend to selected dummy */
	if (tpm_suspend_pcr) {
		cmd.header.in = pcrextend_header;
		cmd.params.pcrextend_in.pcr_idx = cpu_to_be32(tpm_suspend_pcr);
		memcpy(cmd.params.pcrextend_in.hash, dummy_hash,
		       TPM_DIGEST_SIZE);
		rc = tpm_transmit_cmd(chip, &cmd, EXTEND_PCR_RESULT_SIZE, 0,
				      "extending dummy pcr before suspend");
	}

	/* now do the actual savestate */
	for (try = 0; try < TPM_RETRY; try++) {
		cmd.header.in = savestate_header;
		rc = tpm_transmit_cmd(chip, &cmd, SAVESTATE_RESULT_SIZE, 0,
				      NULL);

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
		dev_err(&chip->dev,
			"Error (%d) sending savestate before suspend\n", rc);
	else if (try > 0)
		dev_warn(&chip->dev, "TPM savestate took %dms\n",
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

	if (!out || !num_bytes || max > TPM_MAX_RNG_DATA)
		return -EINVAL;

	chip = tpm_chip_find_get(chip_num);
	if (chip == NULL)
		return -ENODEV;

	if (chip->flags & TPM_CHIP_FLAG_TPM2) {
		err = tpm2_get_random(chip, out, max);
		tpm_put_ops(chip);
		return err;
	}

	do {
		tpm_cmd.header.in = tpm_getrandom_header;
		tpm_cmd.params.getrandom_in.num_bytes = cpu_to_be32(num_bytes);

		err = tpm_transmit_cmd(chip, &tpm_cmd,
				       TPM_GETRANDOM_RESULT_SIZE + num_bytes,
				       0, "attempting get random");
		if (err)
			break;

		recd = be32_to_cpu(tpm_cmd.params.getrandom_out.rng_data_len);
		memcpy(dest, tpm_cmd.params.getrandom_out.rng_data, recd);

		dest += recd;
		total += recd;
		num_bytes -= recd;
	} while (retries-- && total < max);

	tpm_put_ops(chip);
	return total ? total : -EIO;
}
EXPORT_SYMBOL_GPL(tpm_get_random);

/**
 * tpm_seal_trusted() - seal a trusted key
 * @chip_num: A specific chip number for the request or TPM_ANY_NUM
 * @options: authentication values and other options
 * @payload: the key data in clear and encrypted form
 *
 * Returns < 0 on error and 0 on success. At the moment, only TPM 2.0 chips
 * are supported.
 */
int tpm_seal_trusted(u32 chip_num, struct trusted_key_payload *payload,
		     struct trusted_key_options *options)
{
	struct tpm_chip *chip;
	int rc;

	chip = tpm_chip_find_get(chip_num);
	if (chip == NULL || !(chip->flags & TPM_CHIP_FLAG_TPM2))
		return -ENODEV;

	rc = tpm2_seal_trusted(chip, payload, options);

	tpm_put_ops(chip);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_seal_trusted);

/**
 * tpm_unseal_trusted() - unseal a trusted key
 * @chip_num: A specific chip number for the request or TPM_ANY_NUM
 * @options: authentication values and other options
 * @payload: the key data in clear and encrypted form
 *
 * Returns < 0 on error and 0 on success. At the moment, only TPM 2.0 chips
 * are supported.
 */
int tpm_unseal_trusted(u32 chip_num, struct trusted_key_payload *payload,
		       struct trusted_key_options *options)
{
	struct tpm_chip *chip;
	int rc;

	chip = tpm_chip_find_get(chip_num);
	if (chip == NULL || !(chip->flags & TPM_CHIP_FLAG_TPM2))
		return -ENODEV;

	rc = tpm2_unseal_trusted(chip, payload, options);

	tpm_put_ops(chip);

	return rc;
}
EXPORT_SYMBOL_GPL(tpm_unseal_trusted);

static int __init tpm_init(void)
{
	int rc;

	tpm_class = class_create(THIS_MODULE, "tpm");
	if (IS_ERR(tpm_class)) {
		pr_err("couldn't create tpm class\n");
		return PTR_ERR(tpm_class);
	}

	rc = alloc_chrdev_region(&tpm_devt, 0, TPM_NUM_DEVICES, "tpm");
	if (rc < 0) {
		pr_err("tpm: failed to allocate char dev region\n");
		class_destroy(tpm_class);
		return rc;
	}

	return 0;
}

static void __exit tpm_exit(void)
{
	idr_destroy(&dev_nums_idr);
	class_destroy(tpm_class);
	unregister_chrdev_region(tpm_devt, TPM_NUM_DEVICES);
}

subsys_initcall(tpm_init);
module_exit(tpm_exit);

MODULE_AUTHOR("Leendert van Doorn (leendert@watson.ibm.com)");
MODULE_DESCRIPTION("TPM Driver");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
