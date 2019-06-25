// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Intel Corporation
 *
 * Authors:
 * Jarkko Sakkinen <jarkko.sakkinen@linux.intel.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * This file contains TPM2 protocol implementations of the commands
 * used by the kernel internally.
 */

#include <linux/gfp.h>
#include <asm/unaligned.h>
#include "tpm.h"

enum tpm2_handle_types {
	TPM2_HT_HMAC_SESSION	= 0x02000000,
	TPM2_HT_POLICY_SESSION	= 0x03000000,
	TPM2_HT_TRANSIENT	= 0x80000000,
};

struct tpm2_context {
	__be64 sequence;
	__be32 saved_handle;
	__be32 hierarchy;
	__be16 blob_size;
} __packed;

static void tpm2_flush_sessions(struct tpm_chip *chip, struct tpm_space *space)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(space->session_tbl); i++) {
		if (space->session_tbl[i])
			tpm2_flush_context(chip, space->session_tbl[i]);
	}
}

int tpm2_init_space(struct tpm_space *space)
{
	space->context_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!space->context_buf)
		return -ENOMEM;

	space->session_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (space->session_buf == NULL) {
		kfree(space->context_buf);
		return -ENOMEM;
	}

	return 0;
}

void tpm2_del_space(struct tpm_chip *chip, struct tpm_space *space)
{
	mutex_lock(&chip->tpm_mutex);
	if (!tpm_chip_start(chip)) {
		tpm2_flush_sessions(chip, space);
		tpm_chip_stop(chip);
	}
	mutex_unlock(&chip->tpm_mutex);
	kfree(space->context_buf);
	kfree(space->session_buf);
}

static int tpm2_load_context(struct tpm_chip *chip, u8 *buf,
			     unsigned int *offset, u32 *handle)
{
	struct tpm_buf tbuf;
	struct tpm2_context *ctx;
	unsigned int body_size;
	int rc;

	rc = tpm_buf_init(&tbuf, TPM2_ST_NO_SESSIONS, TPM2_CC_CONTEXT_LOAD);
	if (rc)
		return rc;

	ctx = (struct tpm2_context *)&buf[*offset];
	body_size = sizeof(*ctx) + be16_to_cpu(ctx->blob_size);
	tpm_buf_append(&tbuf, &buf[*offset], body_size);

	rc = tpm_transmit_cmd(chip, &tbuf, 4, NULL);
	if (rc < 0) {
		dev_warn(&chip->dev, "%s: failed with a system error %d\n",
			 __func__, rc);
		tpm_buf_destroy(&tbuf);
		return -EFAULT;
	} else if (tpm2_rc_value(rc) == TPM2_RC_HANDLE ||
		   rc == TPM2_RC_REFERENCE_H0) {
		/*
		 * TPM_RC_HANDLE means that the session context can't
		 * be loaded because of an internal counter mismatch
		 * that makes the TPM think there might have been a
		 * replay.  This might happen if the context was saved
		 * and loaded outside the space.
		 *
		 * TPM_RC_REFERENCE_H0 means the session has been
		 * flushed outside the space
		 */
		*handle = 0;
		tpm_buf_destroy(&tbuf);
		return -ENOENT;
	} else if (rc > 0) {
		dev_warn(&chip->dev, "%s: failed with a TPM error 0x%04X\n",
			 __func__, rc);
		tpm_buf_destroy(&tbuf);
		return -EFAULT;
	}

	*handle = be32_to_cpup((__be32 *)&tbuf.data[TPM_HEADER_SIZE]);
	*offset += body_size;

	tpm_buf_destroy(&tbuf);
	return 0;
}

static int tpm2_save_context(struct tpm_chip *chip, u32 handle, u8 *buf,
			     unsigned int buf_size, unsigned int *offset)
{
	struct tpm_buf tbuf;
	unsigned int body_size;
	int rc;

	rc = tpm_buf_init(&tbuf, TPM2_ST_NO_SESSIONS, TPM2_CC_CONTEXT_SAVE);
	if (rc)
		return rc;

	tpm_buf_append_u32(&tbuf, handle);

	rc = tpm_transmit_cmd(chip, &tbuf, 0, NULL);
	if (rc < 0) {
		dev_warn(&chip->dev, "%s: failed with a system error %d\n",
			 __func__, rc);
		tpm_buf_destroy(&tbuf);
		return -EFAULT;
	} else if (tpm2_rc_value(rc) == TPM2_RC_REFERENCE_H0) {
		tpm_buf_destroy(&tbuf);
		return -ENOENT;
	} else if (rc) {
		dev_warn(&chip->dev, "%s: failed with a TPM error 0x%04X\n",
			 __func__, rc);
		tpm_buf_destroy(&tbuf);
		return -EFAULT;
	}

	body_size = tpm_buf_length(&tbuf) - TPM_HEADER_SIZE;
	if ((*offset + body_size) > buf_size) {
		dev_warn(&chip->dev, "%s: out of backing storage\n", __func__);
		tpm_buf_destroy(&tbuf);
		return -ENOMEM;
	}

	memcpy(&buf[*offset], &tbuf.data[TPM_HEADER_SIZE], body_size);
	*offset += body_size;
	tpm_buf_destroy(&tbuf);
	return 0;
}

void tpm2_flush_space(struct tpm_chip *chip)
{
	struct tpm_space *space = &chip->work_space;
	int i;

	for (i = 0; i < ARRAY_SIZE(space->context_tbl); i++)
		if (space->context_tbl[i] && ~space->context_tbl[i])
			tpm2_flush_context(chip, space->context_tbl[i]);

	tpm2_flush_sessions(chip, space);
}

static int tpm2_load_space(struct tpm_chip *chip)
{
	struct tpm_space *space = &chip->work_space;
	unsigned int offset;
	int i;
	int rc;

	for (i = 0, offset = 0; i < ARRAY_SIZE(space->context_tbl); i++) {
		if (!space->context_tbl[i])
			continue;

		/* sanity check, should never happen */
		if (~space->context_tbl[i]) {
			dev_err(&chip->dev, "context table is inconsistent");
			return -EFAULT;
		}

		rc = tpm2_load_context(chip, space->context_buf, &offset,
				       &space->context_tbl[i]);
		if (rc)
			return rc;
	}

	for (i = 0, offset = 0; i < ARRAY_SIZE(space->session_tbl); i++) {
		u32 handle;

		if (!space->session_tbl[i])
			continue;

		rc = tpm2_load_context(chip, space->session_buf,
				       &offset, &handle);
		if (rc == -ENOENT) {
			/* load failed, just forget session */
			space->session_tbl[i] = 0;
		} else if (rc) {
			tpm2_flush_space(chip);
			return rc;
		}
		if (handle != space->session_tbl[i]) {
			dev_warn(&chip->dev, "session restored to wrong handle\n");
			tpm2_flush_space(chip);
			return -EFAULT;
		}
	}

	return 0;
}

static bool tpm2_map_to_phandle(struct tpm_space *space, void *handle)
{
	u32 vhandle = be32_to_cpup((__be32 *)handle);
	u32 phandle;
	int i;

	i = 0xFFFFFF - (vhandle & 0xFFFFFF);
	if (i >= ARRAY_SIZE(space->context_tbl) || !space->context_tbl[i])
		return false;

	phandle = space->context_tbl[i];
	*((__be32 *)handle) = cpu_to_be32(phandle);
	return true;
}

static int tpm2_map_command(struct tpm_chip *chip, u32 cc, u8 *cmd)
{
	struct tpm_space *space = &chip->work_space;
	unsigned int nr_handles;
	u32 attrs;
	__be32 *handle;
	int i;

	i = tpm2_find_cc(chip, cc);
	if (i < 0)
		return -EINVAL;

	attrs = chip->cc_attrs_tbl[i];
	nr_handles = (attrs >> TPM2_CC_ATTR_CHANDLES) & GENMASK(2, 0);

	handle = (__be32 *)&cmd[TPM_HEADER_SIZE];
	for (i = 0; i < nr_handles; i++, handle++) {
		if ((be32_to_cpu(*handle) & 0xFF000000) == TPM2_HT_TRANSIENT) {
			if (!tpm2_map_to_phandle(space, handle))
				return -EINVAL;
		}
	}

	return 0;
}

static int tpm_find_and_validate_cc(struct tpm_chip *chip,
				    struct tpm_space *space,
				    const void *cmd, size_t len)
{
	const struct tpm_header *header = (const void *)cmd;
	int i;
	u32 cc;
	u32 attrs;
	unsigned int nr_handles;

	if (len < TPM_HEADER_SIZE || !chip->nr_commands)
		return -EINVAL;

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

	return cc;
err_len:
	dev_dbg(&chip->dev, "%s: insufficient command length %zu", __func__,
		len);
	return -EINVAL;
}

int tpm2_prepare_space(struct tpm_chip *chip, struct tpm_space *space, u8 *cmd,
		       size_t cmdsiz)
{
	int rc;
	int cc;

	if (!space)
		return 0;

	cc = tpm_find_and_validate_cc(chip, space, cmd, cmdsiz);
	if (cc < 0)
		return cc;

	memcpy(&chip->work_space.context_tbl, &space->context_tbl,
	       sizeof(space->context_tbl));
	memcpy(&chip->work_space.session_tbl, &space->session_tbl,
	       sizeof(space->session_tbl));
	memcpy(chip->work_space.context_buf, space->context_buf, PAGE_SIZE);
	memcpy(chip->work_space.session_buf, space->session_buf, PAGE_SIZE);

	rc = tpm2_load_space(chip);
	if (rc) {
		tpm2_flush_space(chip);
		return rc;
	}

	rc = tpm2_map_command(chip, cc, cmd);
	if (rc) {
		tpm2_flush_space(chip);
		return rc;
	}

	chip->last_cc = cc;
	return 0;
}

static bool tpm2_add_session(struct tpm_chip *chip, u32 handle)
{
	struct tpm_space *space = &chip->work_space;
	int i;

	for (i = 0; i < ARRAY_SIZE(space->session_tbl); i++)
		if (space->session_tbl[i] == 0)
			break;

	if (i == ARRAY_SIZE(space->session_tbl))
		return false;

	space->session_tbl[i] = handle;
	return true;
}

static u32 tpm2_map_to_vhandle(struct tpm_space *space, u32 phandle, bool alloc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(space->context_tbl); i++) {
		if (alloc) {
			if (!space->context_tbl[i]) {
				space->context_tbl[i] = phandle;
				break;
			}
		} else if (space->context_tbl[i] == phandle)
			break;
	}

	if (i == ARRAY_SIZE(space->context_tbl))
		return 0;

	return TPM2_HT_TRANSIENT | (0xFFFFFF - i);
}

static int tpm2_map_response_header(struct tpm_chip *chip, u32 cc, u8 *rsp,
				    size_t len)
{
	struct tpm_space *space = &chip->work_space;
	struct tpm_header *header = (struct tpm_header *)rsp;
	u32 phandle;
	u32 phandle_type;
	u32 vhandle;
	u32 attrs;
	int i;

	if (be32_to_cpu(header->return_code) != TPM2_RC_SUCCESS)
		return 0;

	i = tpm2_find_cc(chip, cc);
	/* sanity check, should never happen */
	if (i < 0)
		return -EFAULT;

	attrs = chip->cc_attrs_tbl[i];
	if (!((attrs >> TPM2_CC_ATTR_RHANDLE) & 1))
		return 0;

	phandle = be32_to_cpup((__be32 *)&rsp[TPM_HEADER_SIZE]);
	phandle_type = phandle & 0xFF000000;

	switch (phandle_type) {
	case TPM2_HT_TRANSIENT:
		vhandle = tpm2_map_to_vhandle(space, phandle, true);
		if (!vhandle)
			goto out_no_slots;

		*(__be32 *)&rsp[TPM_HEADER_SIZE] = cpu_to_be32(vhandle);
		break;
	case TPM2_HT_HMAC_SESSION:
	case TPM2_HT_POLICY_SESSION:
		if (!tpm2_add_session(chip, phandle))
			goto out_no_slots;
		break;
	default:
		dev_err(&chip->dev, "%s: unknown handle 0x%08X\n",
			__func__, phandle);
		break;
	}

	return 0;
out_no_slots:
	tpm2_flush_context(chip, phandle);
	dev_warn(&chip->dev, "%s: out of slots for 0x%08X\n", __func__,
		 phandle);
	return -ENOMEM;
}

struct tpm2_cap_handles {
	u8 more_data;
	__be32 capability;
	__be32 count;
	__be32 handles[];
} __packed;

static int tpm2_map_response_body(struct tpm_chip *chip, u32 cc, u8 *rsp,
				  size_t len)
{
	struct tpm_space *space = &chip->work_space;
	struct tpm_header *header = (struct tpm_header *)rsp;
	struct tpm2_cap_handles *data;
	u32 phandle;
	u32 phandle_type;
	u32 vhandle;
	int i;
	int j;

	if (cc != TPM2_CC_GET_CAPABILITY ||
	    be32_to_cpu(header->return_code) != TPM2_RC_SUCCESS) {
		return 0;
	}

	if (len < TPM_HEADER_SIZE + 9)
		return -EFAULT;

	data = (void *)&rsp[TPM_HEADER_SIZE];
	if (be32_to_cpu(data->capability) != TPM2_CAP_HANDLES)
		return 0;

	if (len != TPM_HEADER_SIZE + 9 + 4 * be32_to_cpu(data->count))
		return -EFAULT;

	for (i = 0, j = 0; i < be32_to_cpu(data->count); i++) {
		phandle = be32_to_cpup((__be32 *)&data->handles[i]);
		phandle_type = phandle & 0xFF000000;

		switch (phandle_type) {
		case TPM2_HT_TRANSIENT:
			vhandle = tpm2_map_to_vhandle(space, phandle, false);
			if (!vhandle)
				break;

			data->handles[j] = cpu_to_be32(vhandle);
			j++;
			break;

		default:
			data->handles[j] = cpu_to_be32(phandle);
			j++;
			break;
		}

	}

	header->length = cpu_to_be32(TPM_HEADER_SIZE + 9 + 4 * j);
	data->count = cpu_to_be32(j);
	return 0;
}

static int tpm2_save_space(struct tpm_chip *chip)
{
	struct tpm_space *space = &chip->work_space;
	unsigned int offset;
	int i;
	int rc;

	for (i = 0, offset = 0; i < ARRAY_SIZE(space->context_tbl); i++) {
		if (!(space->context_tbl[i] && ~space->context_tbl[i]))
			continue;

		rc = tpm2_save_context(chip, space->context_tbl[i],
				       space->context_buf, PAGE_SIZE,
				       &offset);
		if (rc == -ENOENT) {
			space->context_tbl[i] = 0;
			continue;
		} else if (rc)
			return rc;

		tpm2_flush_context(chip, space->context_tbl[i]);
		space->context_tbl[i] = ~0;
	}

	for (i = 0, offset = 0; i < ARRAY_SIZE(space->session_tbl); i++) {
		if (!space->session_tbl[i])
			continue;

		rc = tpm2_save_context(chip, space->session_tbl[i],
				       space->session_buf, PAGE_SIZE,
				       &offset);

		if (rc == -ENOENT) {
			/* handle error saving session, just forget it */
			space->session_tbl[i] = 0;
		} else if (rc < 0) {
			tpm2_flush_space(chip);
			return rc;
		}
	}

	return 0;
}

int tpm2_commit_space(struct tpm_chip *chip, struct tpm_space *space,
		      void *buf, size_t *bufsiz)
{
	struct tpm_header *header = buf;
	int rc;

	if (!space)
		return 0;

	rc = tpm2_map_response_header(chip, chip->last_cc, buf, *bufsiz);
	if (rc) {
		tpm2_flush_space(chip);
		goto out;
	}

	rc = tpm2_map_response_body(chip, chip->last_cc, buf, *bufsiz);
	if (rc) {
		tpm2_flush_space(chip);
		goto out;
	}

	rc = tpm2_save_space(chip);
	if (rc) {
		tpm2_flush_space(chip);
		goto out;
	}

	*bufsiz = be32_to_cpu(header->length);

	memcpy(&space->context_tbl, &chip->work_space.context_tbl,
	       sizeof(space->context_tbl));
	memcpy(&space->session_tbl, &chip->work_space.session_tbl,
	       sizeof(space->session_tbl));
	memcpy(space->context_buf, chip->work_space.context_buf, PAGE_SIZE);
	memcpy(space->session_buf, chip->work_space.session_buf, PAGE_SIZE);

	return 0;
out:
	dev_err(&chip->dev, "%s: error %d\n", __func__, rc);
	return rc;
}
