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
#include <linux/pm_runtime.h>

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
		 "PCR to use for dummy writes to facilitate flush on suspend.");

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

	/*
	 * We only have a duration table for protected commands, where the upper
	 * 16 bits are 0. For the few other ordinals the fallback will be used.
	 */
	if (ordinal < TPM_MAX_ORDINAL)
		duration_idx = tpm_ordinal_duration[ordinal];

	if (duration_idx != TPM_UNDEFINED)
		duration = chip->duration[duration_idx];
	if (duration <= 0)
		return 2 * 60 * HZ;
	else
		return duration;
}
EXPORT_SYMBOL_GPL(tpm_calc_ordinal_duration);

static int tpm_validate_command(struct tpm_chip *chip,
				 struct tpm_space *space,
				 const u8 *cmd,
				 size_t len)
{
	const struct tpm_input_header *header = (const void *)cmd;
	int i;
	u32 cc;
	u32 attrs;
	unsigned int nr_handles;

	if (len < TPM_HEADER_SIZE)
		return -EINVAL;

	if (!space)
		return 0;

	if (chip->flags & TPM_CHIP_FLAG_TPM2 && chip->nr_commands) {
		cc = be32_to_cpu(header->ordinal);

		i = tpm2_find_cc(chip, cc);
		if (i < 0) {
			dev_dbg(&chip->dev, "0x%04X is an invalid command\n",
				cc);
			return -EOPNOTSUPP;
		}

		attrs = chip->cc_attrs_tbl[i];
		nr_handles =
			4 * ((attrs >> TPM2_CC_ATTR_CHANDLES) & GENMASK(2, 0));
		if (len < TPM_HEADER_SIZE + 4 * nr_handles)
			goto err_len;
	}

	return 0;
err_len:
	dev_dbg(&chip->dev,
		"%s: insufficient command length %zu", __func__, len);
	return -EINVAL;
}

static int tpm_request_locality(struct tpm_chip *chip, unsigned int flags)
{
	int rc;

	if (flags & TPM_TRANSMIT_RAW)
		return 0;

	if (!chip->ops->request_locality)
		return 0;

	rc = chip->ops->request_locality(chip, 0);
	if (rc < 0)
		return rc;

	chip->locality = rc;

	return 0;
}

static void tpm_relinquish_locality(struct tpm_chip *chip, unsigned int flags)
{
	int rc;

	if (flags & TPM_TRANSMIT_RAW)
		return;

	if (!chip->ops->relinquish_locality)
		return;

	rc = chip->ops->relinquish_locality(chip, chip->locality);
	if (rc)
		dev_err(&chip->dev, "%s: : error %d\n", __func__, rc);

	chip->locality = -1;
}

static int tpm_cmd_ready(struct tpm_chip *chip, unsigned int flags)
{
	if (flags & TPM_TRANSMIT_RAW)
		return 0;

	if (!chip->ops->cmd_ready)
		return 0;

	return chip->ops->cmd_ready(chip);
}

static int tpm_go_idle(struct tpm_chip *chip, unsigned int flags)
{
	if (flags & TPM_TRANSMIT_RAW)
		return 0;

	if (!chip->ops->go_idle)
		return 0;

	return chip->ops->go_idle(chip);
}

static ssize_t tpm_try_transmit(struct tpm_chip *chip,
				struct tpm_space *space,
				u8 *buf, size_t bufsiz,
				unsigned int flags)
{
	struct tpm_output_header *header = (void *)buf;
	int rc;
	ssize_t len = 0;
	u32 count, ordinal;
	unsigned long stop;
	bool need_locality;

	rc = tpm_validate_command(chip, space, buf, bufsiz);
	if (rc == -EINVAL)
		return rc;
	/*
	 * If the command is not implemented by the TPM, synthesize a
	 * response with a TPM2_RC_COMMAND_CODE return for user-space.
	 */
	if (rc == -EOPNOTSUPP) {
		header->length = cpu_to_be32(sizeof(*header));
		header->tag = cpu_to_be16(TPM2_ST_NO_SESSIONS);
		header->return_code = cpu_to_be32(TPM2_RC_COMMAND_CODE |
						  TSS2_RESMGR_TPM_RC_LAYER);
		return sizeof(*header);
	}

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


	if (chip->ops->clk_enable != NULL)
		chip->ops->clk_enable(chip, true);

	/* Store the decision as chip->locality will be changed. */
	need_locality = chip->locality == -1;

	if (need_locality) {
		rc = tpm_request_locality(chip, flags);
		if (rc < 0)
			goto out_no_locality;
	}

	rc = tpm_cmd_ready(chip, flags);
	if (rc)
		goto out;

	rc = tpm2_prepare_space(chip, space, ordinal, buf);
	if (rc)
		goto out;

	rc = chip->ops->send(chip, (u8 *) buf, count);
	if (rc < 0) {
		if (rc != -EPIPE)
			dev_err(&chip->dev,
				"%s: tpm_send: error %d\n", __func__, rc);
		goto out;
	}

	if (chip->flags & TPM_CHIP_FLAG_IRQ)
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

		tpm_msleep(TPM_TIMEOUT);
		rmb();
	} while (time_before(jiffies, stop));

	chip->ops->cancel(chip);
	dev_err(&chip->dev, "Operation Timed out\n");
	rc = -ETIME;
	goto out;

out_recv:
	len = chip->ops->recv(chip, (u8 *) buf, bufsiz);
	if (len < 0) {
		rc = len;
		dev_err(&chip->dev,
			"tpm_transmit: tpm_recv: error %d\n", rc);
		goto out;
	} else if (len < TPM_HEADER_SIZE) {
		rc = -EFAULT;
		goto out;
	}

	if (len != be32_to_cpu(header->length)) {
		rc = -EFAULT;
		goto out;
	}

	rc = tpm2_commit_space(chip, space, ordinal, buf, &len);
	if (rc)
		dev_err(&chip->dev, "tpm2_commit_space: error %d\n", rc);

out:
	rc = tpm_go_idle(chip, flags);
	if (rc)
		goto out;

	if (need_locality)
		tpm_relinquish_locality(chip, flags);

out_no_locality:
	if (chip->ops->clk_enable != NULL)
		chip->ops->clk_enable(chip, false);

	if (!(flags & TPM_TRANSMIT_UNLOCKED))
		mutex_unlock(&chip->tpm_mutex);
	return rc ? rc : len;
}

/**
 * tpm_transmit - Internal kernel interface to transmit TPM commands.
 *
 * @chip: TPM chip to use
 * @space: tpm space
 * @buf: TPM command buffer
 * @bufsiz: length of the TPM command buffer
 * @flags: tpm transmit flags - bitmap
 *
 * A wrapper around tpm_try_transmit that handles TPM2_RC_RETRY
 * returns from the TPM and retransmits the command after a delay up
 * to a maximum wait of TPM2_DURATION_LONG.
 *
 * Note: TPM1 never returns TPM2_RC_RETRY so the retry logic is TPM2
 * only
 *
 * Return:
 *     the length of the return when the operation is successful.
 *     A negative number for system errors (errno).
 */
ssize_t tpm_transmit(struct tpm_chip *chip, struct tpm_space *space,
		     u8 *buf, size_t bufsiz, unsigned int flags)
{
	struct tpm_output_header *header = (struct tpm_output_header *)buf;
	/* space for header and handles */
	u8 save[TPM_HEADER_SIZE + 3*sizeof(u32)];
	unsigned int delay_msec = TPM2_DURATION_SHORT;
	u32 rc = 0;
	ssize_t ret;
	const size_t save_size = min(space ? sizeof(save) : TPM_HEADER_SIZE,
				     bufsiz);

	/*
	 * Subtlety here: if we have a space, the handles will be
	 * transformed, so when we restore the header we also have to
	 * restore the handles.
	 */
	memcpy(save, buf, save_size);

	for (;;) {
		ret = tpm_try_transmit(chip, space, buf, bufsiz, flags);
		if (ret < 0)
			break;
		rc = be32_to_cpu(header->return_code);
		if (rc != TPM2_RC_RETRY)
			break;

		if (delay_msec > TPM2_DURATION_LONG) {
			dev_err(&chip->dev, "TPM is in retry loop\n");
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
 *    The function extracts tpm out header return code
 *
 * @chip: TPM chip to use
 * @space: tpm space
 * @buf: TPM command buffer
 * @bufsiz: length of the buffer
 * @min_rsp_body_length: minimum expected length of response body
 * @flags: tpm transmit flags - bitmap
 * @desc: command description used in the error message
 *
 * Return:
 *     0 when the operation is successful.
 *     A negative number for system errors (errno).
 *     A positive number for a TPM error.
 */
ssize_t tpm_transmit_cmd(struct tpm_chip *chip, struct tpm_space *space,
			 const void *buf, size_t bufsiz,
			 size_t min_rsp_body_length, unsigned int flags,
			 const char *desc)
{
	const struct tpm_output_header *header = buf;
	int err;
	ssize_t len;

	len = tpm_transmit(chip, space, (u8 *)buf, bufsiz, flags);
	if (len <  0)
		return len;

	err = be32_to_cpu(header->return_code);
	if (err != 0 && err != TPM_ERR_DISABLED && err != TPM_ERR_DEACTIVATED
	    && desc)
		dev_err(&chip->dev, "A TPM error (%d) occurred %s\n", err,
			desc);
	if (err)
		return err;

	if (len < min_rsp_body_length + TPM_HEADER_SIZE)
		return -EFAULT;

	return 0;
}
EXPORT_SYMBOL_GPL(tpm_transmit_cmd);

#define TPM_ORD_STARTUP 153
#define TPM_ST_CLEAR 1

/**
 * tpm_startup - turn on the TPM
 * @chip: TPM chip to use
 *
 * Normally the firmware should start the TPM. This function is provided as a
 * workaround if this does not happen. A legal case for this could be for
 * example when a TPM emulator is used.
 *
 * Return: same as tpm_transmit_cmd()
 */
int tpm_startup(struct tpm_chip *chip)
{
	struct tpm_buf buf;
	int rc;

	dev_info(&chip->dev, "starting up the TPM manually\n");

	if (chip->flags & TPM_CHIP_FLAG_TPM2) {
		rc = tpm_buf_init(&buf, TPM2_ST_NO_SESSIONS, TPM2_CC_STARTUP);
		if (rc < 0)
			return rc;

		tpm_buf_append_u16(&buf, TPM2_SU_CLEAR);
	} else {
		rc = tpm_buf_init(&buf, TPM_TAG_RQU_COMMAND, TPM_ORD_STARTUP);
		if (rc < 0)
			return rc;

		tpm_buf_append_u16(&buf, TPM_ST_CLEAR);
	}

	rc = tpm_transmit_cmd(chip, NULL, buf.data, PAGE_SIZE, 0, 0,
			      "attempting to start the TPM");

	tpm_buf_destroy(&buf);
	return rc;
}

#define TPM_DIGEST_SIZE 20
#define TPM_RET_CODE_IDX 6
#define TPM_INTERNAL_RESULT_SIZE 200
#define TPM_ORD_GET_CAP 101
#define TPM_ORD_GET_RANDOM 70

static const struct tpm_input_header tpm_getcap_header = {
	.tag = cpu_to_be16(TPM_TAG_RQU_COMMAND),
	.length = cpu_to_be32(22),
	.ordinal = cpu_to_be32(TPM_ORD_GET_CAP)
};

ssize_t tpm_getcap(struct tpm_chip *chip, u32 subcap_id, cap_t *cap,
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
	rc = tpm_transmit_cmd(chip, NULL, buf.data, PAGE_SIZE,
			      min_cap_length, 0, desc);
	if (!rc)
		*cap = *(cap_t *)&buf.data[TPM_HEADER_SIZE + 4];

	tpm_buf_destroy(&buf);
	return rc;
}
EXPORT_SYMBOL_GPL(tpm_getcap);

int tpm_get_timeouts(struct tpm_chip *chip)
{
	cap_t cap;
	unsigned long timeout_old[4], timeout_chip[4], timeout_eff[4];
	ssize_t rc;

	if (chip->flags & TPM_CHIP_FLAG_HAVE_TIMEOUTS)
		return 0;

	if (chip->flags & TPM_CHIP_FLAG_TPM2) {
		/* Fixed timeouts for TPM2 */
		chip->timeout_a = msecs_to_jiffies(TPM2_TIMEOUT_A);
		chip->timeout_b = msecs_to_jiffies(TPM2_TIMEOUT_B);
		chip->timeout_c = msecs_to_jiffies(TPM2_TIMEOUT_C);
		chip->timeout_d = msecs_to_jiffies(TPM2_TIMEOUT_D);
		chip->duration[TPM_SHORT] =
		    msecs_to_jiffies(TPM2_DURATION_SHORT);
		chip->duration[TPM_MEDIUM] =
		    msecs_to_jiffies(TPM2_DURATION_MEDIUM);
		chip->duration[TPM_LONG] =
		    msecs_to_jiffies(TPM2_DURATION_LONG);

		chip->flags |= TPM_CHIP_FLAG_HAVE_TIMEOUTS;
		return 0;
	}

	rc = tpm_getcap(chip, TPM_CAP_PROP_TIS_TIMEOUT, &cap, NULL,
			sizeof(cap.timeout));
	if (rc == TPM_ERR_INVALID_POSTINIT) {
		if (tpm_startup(chip))
			return rc;

		rc = tpm_getcap(chip, TPM_CAP_PROP_TIS_TIMEOUT, &cap,
				"attempting to determine the timeouts",
				sizeof(cap.timeout));
	}

	if (rc) {
		dev_err(&chip->dev,
			"A TPM error (%zd) occurred attempting to determine the timeouts\n",
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
	if (chip->ops->update_timeouts != NULL)
		chip->timeout_adjusted =
			chip->ops->update_timeouts(chip, timeout_eff);

	if (!chip->timeout_adjusted) {
		/* Restore default if chip reported 0 */
		int i;

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
		dev_info(&chip->dev,
			 HW_ERR "Adjusting reported timeouts: A %lu->%luus B %lu->%luus C %lu->%luus D %lu->%luus\n",
			 timeout_chip[0], timeout_eff[0],
			 timeout_chip[1], timeout_eff[1],
			 timeout_chip[2], timeout_eff[2],
			 timeout_chip[3], timeout_eff[3]);
	}

	chip->timeout_a = usecs_to_jiffies(timeout_eff[0]);
	chip->timeout_b = usecs_to_jiffies(timeout_eff[1]);
	chip->timeout_c = usecs_to_jiffies(timeout_eff[2]);
	chip->timeout_d = usecs_to_jiffies(timeout_eff[3]);

	rc = tpm_getcap(chip, TPM_CAP_PROP_TIS_DURATION, &cap,
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
EXPORT_SYMBOL_GPL(tpm_get_timeouts);

#define TPM_ORD_CONTINUE_SELFTEST 83
#define CONTINUE_SELFTEST_RESULT_SIZE 10

static const struct tpm_input_header continue_selftest_header = {
	.tag = cpu_to_be16(TPM_TAG_RQU_COMMAND),
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
	rc = tpm_transmit_cmd(chip, NULL, &cmd, CONTINUE_SELFTEST_RESULT_SIZE,
			      0, 0, "continue selftest");
	return rc;
}

#define TPM_ORDINAL_PCRREAD 21
#define READ_PCR_RESULT_SIZE 30
#define READ_PCR_RESULT_BODY_SIZE 20
static const struct tpm_input_header pcrread_header = {
	.tag = cpu_to_be16(TPM_TAG_RQU_COMMAND),
	.length = cpu_to_be32(14),
	.ordinal = cpu_to_be32(TPM_ORDINAL_PCRREAD)
};

int tpm_pcr_read_dev(struct tpm_chip *chip, int pcr_idx, u8 *res_buf)
{
	int rc;
	struct tpm_cmd_t cmd;

	cmd.header.in = pcrread_header;
	cmd.params.pcrread_in.pcr_idx = cpu_to_be32(pcr_idx);
	rc = tpm_transmit_cmd(chip, NULL, &cmd, READ_PCR_RESULT_SIZE,
			      READ_PCR_RESULT_BODY_SIZE, 0,
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

#define TPM_ORD_PCR_EXTEND 20
#define EXTEND_PCR_RESULT_SIZE 34
#define EXTEND_PCR_RESULT_BODY_SIZE 20
static const struct tpm_input_header pcrextend_header = {
	.tag = cpu_to_be16(TPM_TAG_RQU_COMMAND),
	.length = cpu_to_be32(34),
	.ordinal = cpu_to_be32(TPM_ORD_PCR_EXTEND)
};

static int tpm1_pcr_extend(struct tpm_chip *chip, int pcr_idx, const u8 *hash,
			   char *log_msg)
{
	struct tpm_buf buf;
	int rc;

	rc = tpm_buf_init(&buf, TPM_TAG_RQU_COMMAND, TPM_ORD_PCR_EXTEND);
	if (rc)
		return rc;

	tpm_buf_append_u32(&buf, pcr_idx);
	tpm_buf_append(&buf, hash, TPM_DIGEST_SIZE);

	rc = tpm_transmit_cmd(chip, NULL, buf.data, EXTEND_PCR_RESULT_SIZE,
			      EXTEND_PCR_RESULT_BODY_SIZE, 0, log_msg);
	tpm_buf_destroy(&buf);
	return rc;
}

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
int tpm_pcr_extend(u32 chip_num, int pcr_idx, const u8 *hash)
{
	int rc;
	struct tpm_chip *chip;
	struct tpm2_digest digest_list[ARRAY_SIZE(chip->active_banks)];
	u32 count = 0;
	int i;

	chip = tpm_chip_find_get(chip_num);
	if (chip == NULL)
		return -ENODEV;

	if (chip->flags & TPM_CHIP_FLAG_TPM2) {
		memset(digest_list, 0, sizeof(digest_list));

		for (i = 0; i < ARRAY_SIZE(chip->active_banks) &&
			    chip->active_banks[i] != TPM2_ALG_ERROR; i++) {
			digest_list[i].alg_id = chip->active_banks[i];
			memcpy(digest_list[i].digest, hash, TPM_DIGEST_SIZE);
			count++;
		}

		rc = tpm2_pcr_extend(chip, pcr_idx, count, digest_list);
		tpm_put_ops(chip);
		return rc;
	}

	rc = tpm1_pcr_extend(chip, pcr_idx, hash,
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
	u8 dummy[TPM_DIGEST_SIZE];

	duration = tpm_calc_ordinal_duration(chip, TPM_ORD_CONTINUE_SELFTEST);

	loops = jiffies_to_msecs(duration) / delay_msec;

	rc = tpm_continue_selftest(chip);
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
		rc = tpm_pcr_read_dev(chip, 0, dummy);

		/* Some buggy TPMs will not respond to tpm_tis_ready() for
		 * around 300ms while the self test is ongoing, keep trying
		 * until the self test duration expires. */
		if (rc == -ETIME) {
			dev_info(
			    &chip->dev, HW_ERR
			    "TPM command timed out during continue self test");
			tpm_msleep(delay_msec);
			continue;
		}

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
		tpm_msleep(delay_msec);
	} while (--loops > 0);

	return rc;
}
EXPORT_SYMBOL_GPL(tpm_do_selftest);

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

	rc = tpm_get_timeouts(chip);
	if (rc)
		goto out;
	rc = tpm_do_selftest(chip);
	if (rc) {
		dev_err(&chip->dev, "TPM self test failed\n");
		goto out;
	}

	return rc;
out:
	if (rc > 0)
		rc = -ENODEV;
	return rc;
}

int tpm_send(u32 chip_num, void *cmd, size_t buflen)
{
	struct tpm_chip *chip;
	int rc;

	chip = tpm_chip_find_get(chip_num);
	if (chip == NULL)
		return -ENODEV;

	rc = tpm_transmit_cmd(chip, NULL, cmd, buflen, 0, 0,
			      "attempting tpm_cmd");
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

	if (chip->flags & TPM_CHIP_FLAG_IRQ) {
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
			tpm_msleep(TPM_TIMEOUT);
			status = chip->ops->status(chip);
			if ((status & mask) == mask)
				return 0;
		} while (time_before(jiffies, stop));
	}
	return -ETIME;
}
EXPORT_SYMBOL_GPL(wait_for_tpm_stat);

#define TPM_ORD_SAVESTATE 152
#define SAVESTATE_RESULT_SIZE 10

static const struct tpm_input_header savestate_header = {
	.tag = cpu_to_be16(TPM_TAG_RQU_COMMAND),
	.length = cpu_to_be32(10),
	.ordinal = cpu_to_be32(TPM_ORD_SAVESTATE)
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

	if (chip->flags & TPM_CHIP_FLAG_ALWAYS_POWERED)
		return 0;

	if (chip->flags & TPM_CHIP_FLAG_TPM2) {
		tpm2_shutdown(chip, TPM2_SU_STATE);
		return 0;
	}

	/* for buggy tpm, flush pcrs with extend to selected dummy */
	if (tpm_suspend_pcr)
		rc = tpm1_pcr_extend(chip, tpm_suspend_pcr, dummy_hash,
				     "extending dummy pcr before suspend");

	/* now do the actual savestate */
	for (try = 0; try < TPM_RETRY; try++) {
		cmd.header.in = savestate_header;
		rc = tpm_transmit_cmd(chip, NULL, &cmd, SAVESTATE_RESULT_SIZE,
				      0, 0, NULL);

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
static const struct tpm_input_header tpm_getrandom_header = {
	.tag = cpu_to_be16(TPM_TAG_RQU_COMMAND),
	.length = cpu_to_be32(14),
	.ordinal = cpu_to_be32(TPM_ORD_GET_RANDOM)
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
	u32 recd, num_bytes = min_t(u32, max, TPM_MAX_RNG_DATA), rlength;
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

		err = tpm_transmit_cmd(chip, NULL, &tpm_cmd,
				       TPM_GETRANDOM_RESULT_SIZE + num_bytes,
				       offsetof(struct tpm_getrandom_out,
						rng_data),
				       0, "attempting get random");
		if (err)
			break;

		recd = be32_to_cpu(tpm_cmd.params.getrandom_out.rng_data_len);
		if (recd > num_bytes) {
			total = -EFAULT;
			break;
		}

		rlength = be32_to_cpu(tpm_cmd.header.out.length);
		if (rlength < offsetof(struct tpm_getrandom_out, rng_data) +
			      recd) {
			total = -EFAULT;
			break;
		}
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

	tpmrm_class = class_create(THIS_MODULE, "tpmrm");
	if (IS_ERR(tpmrm_class)) {
		pr_err("couldn't create tpmrm class\n");
		class_destroy(tpm_class);
		return PTR_ERR(tpmrm_class);
	}

	rc = alloc_chrdev_region(&tpm_devt, 0, 2*TPM_NUM_DEVICES, "tpm");
	if (rc < 0) {
		pr_err("tpm: failed to allocate char dev region\n");
		class_destroy(tpmrm_class);
		class_destroy(tpm_class);
		return rc;
	}

	return 0;
}

static void __exit tpm_exit(void)
{
	idr_destroy(&dev_nums_idr);
	class_destroy(tpm_class);
	class_destroy(tpmrm_class);
	unregister_chrdev_region(tpm_devt, 2*TPM_NUM_DEVICES);
}

subsys_initcall(tpm_init);
module_exit(tpm_exit);

MODULE_AUTHOR("Leendert van Doorn (leendert@watson.ibm.com)");
MODULE_DESCRIPTION("TPM Driver");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
