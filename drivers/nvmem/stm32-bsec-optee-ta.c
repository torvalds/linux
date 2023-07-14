// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OP-TEE STM32MP BSEC PTA interface, used by STM32 ROMEM driver
 *
 * Copyright (C) 2022, STMicroelectronics - All Rights Reserved
 */

#include <linux/tee_drv.h>

#include "stm32-bsec-optee-ta.h"

/*
 * Read OTP memory
 *
 * [in]		value[0].a		OTP start offset in byte
 * [in]		value[0].b		Access type (0:shadow, 1:fuse, 2:lock)
 * [out]	memref[1].buffer	Output buffer to store read values
 * [out]	memref[1].size		Size of OTP to be read
 *
 * Return codes:
 * TEE_SUCCESS - Invoke command success
 * TEE_ERROR_BAD_PARAMETERS - Incorrect input param
 * TEE_ERROR_ACCESS_DENIED - OTP not accessible by caller
 */
#define PTA_BSEC_READ_MEM		0x0

/*
 * Write OTP memory
 *
 * [in]		value[0].a		OTP start offset in byte
 * [in]		value[0].b		Access type (0:shadow, 1:fuse, 2:lock)
 * [in]		memref[1].buffer	Input buffer to read values
 * [in]		memref[1].size		Size of OTP to be written
 *
 * Return codes:
 * TEE_SUCCESS - Invoke command success
 * TEE_ERROR_BAD_PARAMETERS - Incorrect input param
 * TEE_ERROR_ACCESS_DENIED - OTP not accessible by caller
 */
#define PTA_BSEC_WRITE_MEM		0x1

/* value of PTA_BSEC access type = value[in] b */
#define SHADOW_ACCESS	0
#define FUSE_ACCESS	1
#define LOCK_ACCESS	2

/* Bitfield definition for LOCK status */
#define LOCK_PERM			BIT(30)

/* OP-TEE STM32MP BSEC TA UUID */
static const uuid_t stm32mp_bsec_ta_uuid =
	UUID_INIT(0x94cf71ad, 0x80e6, 0x40b5,
		  0xa7, 0xc6, 0x3d, 0xc5, 0x01, 0xeb, 0x28, 0x03);

/*
 * Check whether this driver supports the BSEC TA in the TEE instance
 * represented by the params (ver/data) to this function.
 */
static int stm32_bsec_optee_ta_match(struct tee_ioctl_version_data *ver,
				     const void *data)
{
	/* Currently this driver only supports GP compliant, OP-TEE based TA */
	if ((ver->impl_id == TEE_IMPL_ID_OPTEE) &&
		(ver->gen_caps & TEE_GEN_CAP_GP))
		return 1;
	else
		return 0;
}

/* Open a session to OP-TEE for STM32MP BSEC TA */
static int stm32_bsec_ta_open_session(struct tee_context *ctx, u32 *id)
{
	struct tee_ioctl_open_session_arg sess_arg;
	int rc;

	memset(&sess_arg, 0, sizeof(sess_arg));
	export_uuid(sess_arg.uuid, &stm32mp_bsec_ta_uuid);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_REE_KERNEL;
	sess_arg.num_params = 0;

	rc = tee_client_open_session(ctx, &sess_arg, NULL);
	if ((rc < 0) || (sess_arg.ret != 0)) {
		pr_err("%s: tee_client_open_session failed err:%#x, ret:%#x\n",
		       __func__, sess_arg.ret, rc);
		if (!rc)
			rc = -EINVAL;
	} else {
		*id = sess_arg.session;
	}

	return rc;
}

/* close a session to OP-TEE for STM32MP BSEC TA */
static void stm32_bsec_ta_close_session(void *ctx, u32 id)
{
	tee_client_close_session(ctx, id);
}

/* stm32_bsec_optee_ta_open() - initialize the STM32MP BSEC TA */
int stm32_bsec_optee_ta_open(struct tee_context **ctx)
{
	struct tee_context *tee_ctx;
	u32 session_id;
	int rc;

	/* Open context with TEE driver */
	tee_ctx = tee_client_open_context(NULL, stm32_bsec_optee_ta_match, NULL, NULL);
	if (IS_ERR(tee_ctx)) {
		rc = PTR_ERR(tee_ctx);
		if (rc == -ENOENT)
			return -EPROBE_DEFER;
		pr_err("%s: tee_client_open_context failed (%d)\n", __func__, rc);

		return rc;
	}

	/* Check STM32MP BSEC TA presence */
	rc = stm32_bsec_ta_open_session(tee_ctx, &session_id);
	if (rc) {
		tee_client_close_context(tee_ctx);
		return rc;
	}

	stm32_bsec_ta_close_session(tee_ctx, session_id);

	*ctx = tee_ctx;

	return 0;
}

/* stm32_bsec_optee_ta_open() - release the PTA STM32MP BSEC TA */
void stm32_bsec_optee_ta_close(void *ctx)
{
	tee_client_close_context(ctx);
}

/* stm32_bsec_optee_ta_read() - nvmem read access using PTA client driver */
int stm32_bsec_optee_ta_read(struct tee_context *ctx, unsigned int offset,
			     void *buf, size_t bytes)
{
	struct tee_shm *shm;
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[2];
	u8 *shm_buf;
	u32 start, num_bytes;
	int ret;
	u32 session_id;

	ret = stm32_bsec_ta_open_session(ctx, &session_id);
	if (ret)
		return ret;

	memset(&arg, 0, sizeof(arg));
	memset(&param, 0, sizeof(param));

	arg.func = PTA_BSEC_READ_MEM;
	arg.session = session_id;
	arg.num_params = 2;

	/* align access on 32bits */
	start = ALIGN_DOWN(offset, 4);
	num_bytes = round_up(offset + bytes - start, 4);
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = start;
	param[0].u.value.b = SHADOW_ACCESS;

	shm = tee_shm_alloc_kernel_buf(ctx, num_bytes);
	if (IS_ERR(shm)) {
		ret = PTR_ERR(shm);
		goto out_tee_session;
	}

	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT;
	param[1].u.memref.shm = shm;
	param[1].u.memref.size = num_bytes;

	ret = tee_client_invoke_func(ctx, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		pr_err("TA_BSEC invoke failed TEE err:%#x, ret:%#x\n",
			arg.ret, ret);
		if (!ret)
			ret = -EIO;
	}
	if (!ret) {
		shm_buf = tee_shm_get_va(shm, 0);
		if (IS_ERR(shm_buf)) {
			ret = PTR_ERR(shm_buf);
			pr_err("tee_shm_get_va failed for transmit (%d)\n", ret);
		} else {
			/* read data from 32 bits aligned buffer */
			memcpy(buf, &shm_buf[offset % 4], bytes);
		}
	}

	tee_shm_free(shm);

out_tee_session:
	stm32_bsec_ta_close_session(ctx, session_id);

	return ret;
}

/* stm32_bsec_optee_ta_write() - nvmem write access using PTA client driver */
int stm32_bsec_optee_ta_write(struct tee_context *ctx, unsigned int lower,
			      unsigned int offset, void *buf, size_t bytes)
{	struct tee_shm *shm;
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[2];
	u8 *shm_buf;
	int ret;
	u32 session_id;

	ret = stm32_bsec_ta_open_session(ctx, &session_id);
	if (ret)
		return ret;

	/* Allow only writing complete 32-bits aligned words */
	if ((bytes % 4) || (offset % 4))
		return -EINVAL;

	memset(&arg, 0, sizeof(arg));
	memset(&param, 0, sizeof(param));

	arg.func = PTA_BSEC_WRITE_MEM;
	arg.session = session_id;
	arg.num_params = 2;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = offset;
	param[0].u.value.b = FUSE_ACCESS;

	shm = tee_shm_alloc_kernel_buf(ctx, bytes);
	if (IS_ERR(shm)) {
		ret = PTR_ERR(shm);
		goto out_tee_session;
	}

	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	param[1].u.memref.shm = shm;
	param[1].u.memref.size = bytes;

	shm_buf = tee_shm_get_va(shm, 0);
	if (IS_ERR(shm_buf)) {
		ret = PTR_ERR(shm_buf);
		pr_err("tee_shm_get_va failed for transmit (%d)\n", ret);
		tee_shm_free(shm);

		goto out_tee_session;
	}

	memcpy(shm_buf, buf, bytes);

	ret = tee_client_invoke_func(ctx, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		pr_err("TA_BSEC invoke failed TEE err:%#x, ret:%#x\n", arg.ret, ret);
		if (!ret)
			ret = -EIO;
	}
	pr_debug("Write OTPs %d to %zu, ret=%d\n", offset / 4, (offset + bytes) / 4, ret);

	/* Lock the upper OTPs with ECC protection, word programming only */
	if (!ret && ((offset + bytes) >= (lower * 4))) {
		u32 start, nb_lock;
		u32 *lock = (u32 *)shm_buf;
		int i;

		/*
		 * don't lock the lower OTPs, no ECC protection and incremental
		 * bit programming, a second write is allowed
		 */
		start = max_t(u32, offset, lower * 4);
		nb_lock = (offset + bytes - start) / 4;

		param[0].u.value.a = start;
		param[0].u.value.b = LOCK_ACCESS;
		param[1].u.memref.size = nb_lock * 4;

		for (i = 0; i < nb_lock; i++)
			lock[i] = LOCK_PERM;

		ret = tee_client_invoke_func(ctx, &arg, param);
		if (ret < 0 || arg.ret != 0) {
			pr_err("TA_BSEC invoke failed TEE err:%#x, ret:%#x\n", arg.ret, ret);
			if (!ret)
				ret = -EIO;
		}
		pr_debug("Lock upper OTPs %d to %d, ret=%d\n",
			 start / 4, start / 4 + nb_lock, ret);
	}

	tee_shm_free(shm);

out_tee_session:
	stm32_bsec_ta_close_session(ctx, session_id);

	return ret;
}
