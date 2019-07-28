// SPDX-License-Identifier: GPL-2.0
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
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org
 */

#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/freezer.h>
#include <linux/tpm_eventlog.h>

#include "tpm.h"

#define TPM_MAX_ORDINAL 243

/*
 * Array with one entry per ordinal defining the maximum amount
 * of time the chip could take to return the result.  The ordinal
 * designation of short, medium or long is defined in a table in
 * TCG Specification TPM Main Part 2 TPM Structures Section 17. The
 * values of the SHORT, MEDIUM, and LONG durations are retrieved
 * from the chip during initialization with a call to tpm_get_timeouts.
 */
static const u8 tpm1_ordinal_duration[TPM_MAX_ORDINAL] = {
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

/**
 * tpm1_calc_ordinal_duration() - calculate the maximum command duration
 * @chip:    TPM chip to use.
 * @ordinal: TPM command ordinal.
 *
 * The function returns the maximum amount of time the chip could take
 * to return the result for a particular ordinal in jiffies.
 *
 * Return: A maximal duration time for an ordinal in jiffies.
 */
unsigned long tpm1_calc_ordinal_duration(struct tpm_chip *chip, u32 ordinal)
{
	int duration_idx = TPM_UNDEFINED;
	int duration = 0;

	/*
	 * We only have a duration table for protected commands, where the upper
	 * 16 bits are 0. For the few other ordinals the fallback will be used.
	 */
	if (ordinal < TPM_MAX_ORDINAL)
		duration_idx = tpm1_ordinal_duration[ordinal];

	if (duration_idx != TPM_UNDEFINED)
		duration = chip->duration[duration_idx];
	if (duration <= 0)
		return 2 * 60 * HZ;
	else
		return duration;
}

#define TPM_ORD_STARTUP 153
#define TPM_ST_CLEAR 1

/**
 * tpm_startup() - turn on the TPM
 * @chip: TPM chip to use
 *
 * Normally the firmware should start the TPM. This function is provided as a
 * workaround if this does not happen. A legal case for this could be for
 * example when a TPM emulator is used.
 *
 * Return: same as tpm_transmit_cmd()
 */
static int tpm1_startup(struct tpm_chip *chip)
{
	struct tpm_buf buf;
	int rc;

	dev_info(&chip->dev, "starting up the TPM manually\n");

	rc = tpm_buf_init(&buf, TPM_TAG_RQU_COMMAND, TPM_ORD_STARTUP);
	if (rc < 0)
		return rc;

	tpm_buf_append_u16(&buf, TPM_ST_CLEAR);

	rc = tpm_transmit_cmd(chip, &buf, 0, "attempting to start the TPM");
	tpm_buf_destroy(&buf);
	return rc;
}

int tpm1_get_timeouts(struct tpm_chip *chip)
{
	cap_t cap;
	unsigned long timeout_old[4], timeout_chip[4], timeout_eff[4];
	ssize_t rc;

	rc = tpm1_getcap(chip, TPM_CAP_PROP_TIS_TIMEOUT, &cap, NULL,
			 sizeof(cap.timeout));
	if (rc == TPM_ERR_INVALID_POSTINIT) {
		if (tpm1_startup(chip))
			return rc;

		rc = tpm1_getcap(chip, TPM_CAP_PROP_TIS_TIMEOUT, &cap,
				 "attempting to determine the timeouts",
				 sizeof(cap.timeout));
	}

	if (rc) {
		dev_err(&chip->dev, "A TPM error (%zd) occurred attempting to determine the timeouts\n",
			rc);
		return rc;
	}

	timeout_old[0] = jiffies_to_usecs(chip->timeout_a);
	timeout_old[1] = jiffies_to_usecs(chip->timeout_b);
	timeout_old[2] = jiffies_to_usecs(chip->timeout_c);
	timeout_old[3] = jiffies_to_usecs(chip->timeout_d);
	timeout_chip[0] = be32_to_cpu(cap.timeout.a);
	timeout_chip[1] = be32_to_cpu(cap.timeout.b);
	timeout_chip[2] = be32_to_cpu(cap.timeout.c);
	timeout_chip[3] = be32_to_cpu(cap.timeout.d);
	memcpy(timeout_eff, timeout_chip, sizeof(timeout_eff));

	/*
	 * Provide ability for vendor overrides of timeout values in case
	 * of misreporting.
	 */
	if (chip->ops->update_timeouts)
		chip->ops->update_timeouts(chip, timeout_eff);

	if (!chip->timeout_adjusted) {
		/* Restore default if chip reported 0 */
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(timeout_eff); i++) {
			if (timeout_eff[i])
				continue;

			timeout_eff[i] = timeout_old[i];
			chip->timeout_adjusted = true;
		}

		if (timeout_eff[0] != 0 && timeout_eff[0] < 1000) {
			/* timeouts in msec rather usec */
			for (i = 0; i != ARRAY_SIZE(timeout_eff); i++)
				timeout_eff[i] *= 1000;
			chip->timeout_adjusted = true;
		}
	}

	/* Report adjusted timeouts */
	if (chip->timeout_adjusted) {
		dev_info(&chip->dev, HW_ERR "Adjusting reported timeouts: A %lu->%luus B %lu->%luus C %lu->%luus D %lu->%luus\n",
			 timeout_chip[0], timeout_eff[0],
			 timeout_chip[1], timeout_eff[1],
			 timeout_chip[2], timeout_eff[2],
			 timeout_chip[3], timeout_eff[3]);
	}

	chip->timeout_a = usecs_to_jiffies(timeout_eff[0]);
	chip->timeout_b = usecs_to_jiffies(timeout_eff[1]);
	chip->timeout_c = usecs_to_jiffies(timeout_eff[2]);
	chip->timeout_d = usecs_to_jiffies(timeout_eff[3]);

	rc = tpm1_getcap(chip, TPM_CAP_PROP_TIS_DURATION, &cap,
			 "attempting to determine the durations",
			  sizeof(cap.duration));
	if (rc)
		return rc;

	chip->duration[TPM_SHORT] =
		usecs_to_jiffies(be32_to_cpu(cap.duration.tpm_short));
	chip->duration[TPM_MEDIUM] =
		usecs_to_jiffies(be32_to_cpu(cap.duration.tpm_medium));
	chip->duration[TPM_LONG] =
		usecs_to_jiffies(be32_to_cpu(cap.duration.tpm_long));
	chip->duration[TPM_LONG_LONG] = 0; /* not used under 1.2 */

	/* The Broadcom BCM0102 chipset in a Dell Latitude D820 gets the above
	 * value wrong and apparently reports msecs rather than usecs. So we
	 * fix up the resulting too-small TPM_SHORT value to make things work.
	 * We also scale the TPM_MEDIUM and -_LONG values by 1000.
	 */
	if (chip->duration[TPM_SHORT] < (HZ / 100)) {
		chip->duration[TPM_SHORT] = HZ;
		chip->duration[TPM_MEDIUM] *= 1000;
		chip->duration[TPM_LONG] *= 1000;
		chip->duration_adjusted = true;
		dev_info(&chip->dev, "Adjusting TPM timeout parameters.");
	}

	chip->flags |= TPM_CHIP_FLAG_HAVE_TIMEOUTS;
	return 0;
}

#define TPM_ORD_PCR_EXTEND 20
int tpm1_pcr_extend(struct tpm_chip *chip, u32 pcr_idx, const u8 *hash,
		    const char *log_msg)
{
	struct tpm_buf buf;
	int rc;

	rc = tpm_buf_init(&buf, TPM_TAG_RQU_COMMAND, TPM_ORD_PCR_EXTEND);
	if (rc)
		return rc;

	tpm_buf_append_u32(&buf, pcr_idx);
	tpm_buf_append(&buf, hash, TPM_DIGEST_SIZE);

	rc = tpm_transmit_cmd(chip, &buf, TPM_DIGEST_SIZE, log_msg);
	tpm_buf_destroy(&buf);
	return rc;
}

#define TPM_ORD_GET_CAP 101
ssize_t tpm1_getcap(struct tpm_chip *chip, u32 subcap_id, cap_t *cap,
		    const char *desc, size_t min_cap_length)
{
	struct tpm_buf buf;
	int rc;

	rc = tpm_buf_init(&buf, TPM_TAG_RQU_COMMAND, TPM_ORD_GET_CAP);
	if (rc)
		return rc;

	if (subcap_id == TPM_CAP_VERSION_1_1 ||
	    subcap_id == TPM_CAP_VERSION_1_2) {
		tpm_buf_append_u32(&buf, subcap_id);
		tpm_buf_append_u32(&buf, 0);
	} else {
		if (subcap_id == TPM_CAP_FLAG_PERM ||
		    subcap_id == TPM_CAP_FLAG_VOL)
			tpm_buf_append_u32(&buf, TPM_CAP_FLAG);
		else
			tpm_buf_append_u32(&buf, TPM_CAP_PROP);

		tpm_buf_append_u32(&buf, 4);
		tpm_buf_append_u32(&buf, subcap_id);
	}
	rc = tpm_transmit_cmd(chip, &buf, min_cap_length, desc);
	if (!rc)
		*cap = *(cap_t *)&buf.data[TPM_HEADER_SIZE + 4];
	tpm_buf_destroy(&buf);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm1_getcap);

#define TPM_ORD_GET_RANDOM 70
struct tpm1_get_random_out {
	__be32 rng_data_len;
	u8 rng_data[TPM_MAX_RNG_DATA];
} __packed;

/**
 * tpm1_get_random() - get random bytes from the TPM's RNG
 * @chip:	a &struct tpm_chip instance
 * @dest:	destination buffer for the random bytes
 * @max:	the maximum number of bytes to write to @dest
 *
 * Return:
 * *  number of bytes read
 * * -errno (positive TPM return codes are masked to -EIO)
 */
int tpm1_get_random(struct tpm_chip *chip, u8 *dest, size_t max)
{
	struct tpm1_get_random_out *out;
	u32 num_bytes =  min_t(u32, max, TPM_MAX_RNG_DATA);
	struct tpm_buf buf;
	u32 total = 0;
	int retries = 5;
	u32 recd;
	int rc;

	rc = tpm_buf_init(&buf, TPM_TAG_RQU_COMMAND, TPM_ORD_GET_RANDOM);
	if (rc)
		return rc;

	do {
		tpm_buf_append_u32(&buf, num_bytes);

		rc = tpm_transmit_cmd(chip, &buf, sizeof(out->rng_data_len),
				      "attempting get random");
		if (rc) {
			if (rc > 0)
				rc = -EIO;
			goto out;
		}

		out = (struct tpm1_get_random_out *)&buf.data[TPM_HEADER_SIZE];

		recd = be32_to_cpu(out->rng_data_len);
		if (recd > num_bytes) {
			rc = -EFAULT;
			goto out;
		}

		if (tpm_buf_length(&buf) < TPM_HEADER_SIZE +
					   sizeof(out->rng_data_len) + recd) {
			rc = -EFAULT;
			goto out;
		}
		memcpy(dest, out->rng_data, recd);

		dest += recd;
		total += recd;
		num_bytes -= recd;

		tpm_buf_reset(&buf, TPM_TAG_RQU_COMMAND, TPM_ORD_GET_RANDOM);
	} while (retries-- && total < max);

	rc = total ? (int)total : -EIO;
out:
	tpm_buf_destroy(&buf);
	return rc;
}

#define TPM_ORD_PCRREAD 21
int tpm1_pcr_read(struct tpm_chip *chip, u32 pcr_idx, u8 *res_buf)
{
	struct tpm_buf buf;
	int rc;

	rc = tpm_buf_init(&buf, TPM_TAG_RQU_COMMAND, TPM_ORD_PCRREAD);
	if (rc)
		return rc;

	tpm_buf_append_u32(&buf, pcr_idx);

	rc = tpm_transmit_cmd(chip, &buf, TPM_DIGEST_SIZE,
			      "attempting to read a pcr value");
	if (rc)
		goto out;

	if (tpm_buf_length(&buf) < TPM_DIGEST_SIZE) {
		rc = -EFAULT;
		goto out;
	}

	memcpy(res_buf, &buf.data[TPM_HEADER_SIZE], TPM_DIGEST_SIZE);

out:
	tpm_buf_destroy(&buf);
	return rc;
}

#define TPM_ORD_CONTINUE_SELFTEST 83
/**
 * tpm_continue_selftest() - run TPM's selftest
 * @chip: TPM chip to use
 *
 * Returns 0 on success, < 0 in case of fatal error or a value > 0 representing
 * a TPM error code.
 */
static int tpm1_continue_selftest(struct tpm_chip *chip)
{
	struct tpm_buf buf;
	int rc;

	rc = tpm_buf_init(&buf, TPM_TAG_RQU_COMMAND, TPM_ORD_CONTINUE_SELFTEST);
	if (rc)
		return rc;

	rc = tpm_transmit_cmd(chip, &buf, 0, "continue selftest");
	tpm_buf_destroy(&buf);
	return rc;
}

/**
 * tpm1_do_selftest - have the TPM continue its selftest and wait until it
 *                   can receive further commands
 * @chip: TPM chip to use
 *
 * Returns 0 on success, < 0 in case of fatal error or a value > 0 representing
 * a TPM error code.
 */
int tpm1_do_selftest(struct tpm_chip *chip)
{
	int rc;
	unsigned int loops;
	unsigned int delay_msec = 100;
	unsigned long duration;
	u8 dummy[TPM_DIGEST_SIZE];

	duration = tpm1_calc_ordinal_duration(chip, TPM_ORD_CONTINUE_SELFTEST);

	loops = jiffies_to_msecs(duration) / delay_msec;

	rc = tpm1_continue_selftest(chip);
	if (rc == TPM_ERR_INVALID_POSTINIT) {
		chip->flags |= TPM_CHIP_FLAG_ALWAYS_POWERED;
		dev_info(&chip->dev, "TPM not ready (%d)\n", rc);
	}
	/* This may fail if there was no TPM driver during a suspend/resume
	 * cycle; some may return 10 (BAD_ORDINAL), others 28 (FAILEDSELFTEST)
	 */
	if (rc)
		return rc;

	do {
		/* Attempt to read a PCR value */
		rc = tpm1_pcr_read(chip, 0, dummy);

		/* Some buggy TPMs will not respond to tpm_tis_ready() for
		 * around 300ms while the self test is ongoing, keep trying
		 * until the self test duration expires.
		 */
		if (rc == -ETIME) {
			dev_info(&chip->dev, HW_ERR "TPM command timed out during continue self test");
			tpm_msleep(delay_msec);
			continue;
		}

		if (rc == TPM_ERR_DISABLED || rc == TPM_ERR_DEACTIVATED) {
			dev_info(&chip->dev, "TPM is disabled/deactivated (0x%X)\n",
				 rc);
			/* TPM is disabled and/or deactivated; driver can
			 * proceed and TPM does handle commands for
			 * suspend/resume correctly
			 */
			return 0;
		}
		if (rc != TPM_WARN_DOING_SELFTEST)
			return rc;
		tpm_msleep(delay_msec);
	} while (--loops > 0);

	return rc;
}
EXPORT_SYMBOL_GPL(tpm1_do_selftest);

/**
 * tpm1_auto_startup - Perform the standard automatic TPM initialization
 *                     sequence
 * @chip: TPM chip to use
 *
 * Returns 0 on success, < 0 in case of fatal error.
 */
int tpm1_auto_startup(struct tpm_chip *chip)
{
	int rc;

	rc = tpm1_get_timeouts(chip);
	if (rc)
		goto out;
	rc = tpm1_do_selftest(chip);
	if (rc) {
		dev_err(&chip->dev, "TPM self test failed\n");
		goto out;
	}

	chip->allocated_banks = kcalloc(1, sizeof(*chip->allocated_banks),
					GFP_KERNEL);
	if (!chip->allocated_banks) {
		rc = -ENOMEM;
		goto out;
	}

	chip->allocated_banks[0].alg_id = TPM_ALG_SHA1;
	chip->allocated_banks[0].digest_size = hash_digest_size[HASH_ALGO_SHA1];
	chip->allocated_banks[0].crypto_id = HASH_ALGO_SHA1;
	chip->nr_allocated_banks = 1;

	return rc;
out:
	if (rc > 0)
		rc = -ENODEV;
	return rc;
}

#define TPM_ORD_SAVESTATE 152

/**
 * tpm1_pm_suspend() - pm suspend handler
 * @chip: TPM chip to use.
 * @tpm_suspend_pcr: flush pcr for buggy TPM chips.
 *
 * The functions saves the TPM state to be restored on resume.
 *
 * Return:
 * * 0 on success,
 * * < 0 on error.
 */
int tpm1_pm_suspend(struct tpm_chip *chip, u32 tpm_suspend_pcr)
{
	u8 dummy_hash[TPM_DIGEST_SIZE] = { 0 };
	struct tpm_buf buf;
	unsigned int try;
	int rc;


	/* for buggy tpm, flush pcrs with extend to selected dummy */
	if (tpm_suspend_pcr)
		rc = tpm1_pcr_extend(chip, tpm_suspend_pcr, dummy_hash,
				     "extending dummy pcr before suspend");

	rc = tpm_buf_init(&buf, TPM_TAG_RQU_COMMAND, TPM_ORD_SAVESTATE);
	if (rc)
		return rc;
	/* now do the actual savestate */
	for (try = 0; try < TPM_RETRY; try++) {
		rc = tpm_transmit_cmd(chip, &buf, 0, NULL);
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
		tpm_msleep(TPM_TIMEOUT_RETRY);

		tpm_buf_reset(&buf, TPM_TAG_RQU_COMMAND, TPM_ORD_SAVESTATE);
	}

	if (rc)
		dev_err(&chip->dev, "Error (%d) sending savestate before suspend\n",
			rc);
	else if (try > 0)
		dev_warn(&chip->dev, "TPM savestate took %dms\n",
			 try * TPM_TIMEOUT_RETRY);

	tpm_buf_destroy(&buf);

	return rc;
}

