// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/habmm.h>
#include <linux/qcom_scm.h>
#include <soc/qcom/qseecom_scm.h>

#include "qcom_scm.h"

/**
 * struct smc_params_s
 * @fn_id: Function id used for hab channel communication
 * @arginfo: Argument information used for hab channel communication
 * @args: The array of values used for hab cannel communication
 */
struct smc_params_s {
	uint64_t fn_id;
	uint64_t arginfo;
	uint64_t args[MAX_SCM_ARGS];
} __packed;

static u32 handle;
static bool opened;

int scm_qcpe_hab_open(void)
{
	int ret;

	if (!opened) {
		ret = habmm_socket_open(&handle, MM_QCPE_VM1, 0, 0);
		if (ret) {
			pr_err("habmm_socket_open failed with ret = %d\n", ret);
			return ret;
		}
		opened = true;
	}

	return 0;
}
EXPORT_SYMBOL(scm_qcpe_hab_open);

void scm_qcpe_hab_close(void)
{
	if (opened) {
		habmm_socket_close(handle);
		opened = false;
		handle = 0;
	}
}
EXPORT_SYMBOL(scm_qcpe_hab_close);

/*
 * Send SMC over HAB, receive the response. Both operations are blocking.
 * This is meant to be called from non-atomic context.
 */
static int scm_qcpe_hab_send_receive(struct smc_params_s *smc_params,
		u32 *size_bytes)
{
	int ret;

	ret = habmm_socket_send(handle, smc_params, sizeof(*smc_params), 0);
	if (ret) {
		pr_err("habmm_socket_send failed, ret= 0x%x\n", ret);
		return ret;
	}

	memset(smc_params, 0x0, sizeof(*smc_params));

	do {
		*size_bytes = sizeof(*smc_params);
		ret = habmm_socket_recv(handle, smc_params, size_bytes, 0,
					HABMM_SOCKET_RECV_FLAGS_UNINTERRUPTIBLE);
	} while (-EINTR == ret);

	if (ret) {
		pr_err("habmm_socket_recv failed, ret= 0x%x\n", ret);
		return ret;
	}

	return 0;
}

/*
 * Send SMC over HAB, receive the response, in non-blocking mode.
 * This is meant to be called from atomic context.
 */
static int scm_qcpe_hab_send_receive_atomic(struct smc_params_s *smc_params,
		u32 *size_bytes)
{
	int ret;
	unsigned long delay;

	delay = jiffies + msecs_to_jiffies(1000); /* 1 second delay for send */

	do {
		ret = habmm_socket_send(handle, smc_params, sizeof(*smc_params),
					HABMM_SOCKET_SEND_FLAGS_NON_BLOCKING);
	} while ((-EAGAIN == ret) && time_before(jiffies, delay));

	if (ret) {
		pr_err("HAB send failed, non-blocking, ret= 0x%x\n", ret);
		return ret;
	}

	memset(smc_params, 0x0, sizeof(*smc_params));

	delay = jiffies + msecs_to_jiffies(1000); /* 1 second delay for receive */

	do {
		*size_bytes = sizeof(*smc_params);
		ret = habmm_socket_recv(handle, smc_params, size_bytes, 0,
					HABMM_SOCKET_RECV_FLAGS_NON_BLOCKING);
	} while ((-EAGAIN == ret) && time_before(jiffies, delay) &&
			(*size_bytes == 0));

	if (ret) {
		pr_err("HAB recv failed, non-blocking, ret= 0x%x\n", ret);
		return ret;
	}

	return 0;
}

int scm_call_qcpe(const struct arm_smccc_args *smc,
		struct arm_smccc_res *res, const bool atomic)
{
	u32 size_bytes;
	struct smc_params_s smc_params = {0,};
	int ret;

	if (!opened) {
		if (!atomic) {
			if (scm_qcpe_hab_open()) {
				pr_err("HAB channel re-open failed\n");
				return -ENODEV;
			}
		} else {
			pr_err("HAB channel is not opened\n");
			return -ENODEV;
		}
	}

	smc_params.fn_id   = smc->args[0];
	smc_params.arginfo = smc->args[1];
	smc_params.args[0] = smc->args[2];
	smc_params.args[1] = smc->args[3];
	smc_params.args[2] = smc->args[4];

	smc_params.args[3] = smc->args[5];
	smc_params.args[4] = 0;

	if (!atomic) {
		ret = scm_qcpe_hab_send_receive(&smc_params, &size_bytes);
		if (ret) {
			pr_err("send/receive failed, non-atomic, ret= 0x%x\n",
				ret);
			goto err_ret;
		}
	} else {
		ret = scm_qcpe_hab_send_receive_atomic(&smc_params,
							&size_bytes);
		if (ret) {
			pr_err("send/receive failed, ret= 0x%x\n", ret);
			goto err_ret;
		}
	}

	if (size_bytes != sizeof(smc_params)) {
		pr_err("habmm_socket_recv expected size: %lu, actual=%u\n",
			sizeof(smc_params), size_bytes);
		ret = QCOM_SCM_ERROR;
		goto err_ret;
	}

	res->a1 = smc_params.args[1];
	res->a2 = smc_params.args[2];
	res->a3 = smc_params.args[3];
	res->a0 = smc_params.args[0];

	goto no_err;

err_ret:
	if (!atomic) {
		/* In case of an error, try to recover the hab connection
		 * for next time. This can only be done if called in
		 * non-atomic context.
		 */
		scm_qcpe_hab_close();
		if (scm_qcpe_hab_open())
			pr_err("scm_qcpe_hab_open failed\n");
	}

no_err:
	return res->a0;
}
EXPORT_SYMBOL(scm_call_qcpe);

MODULE_DESCRIPTION("SCM HAB driver");
MODULE_LICENSE("GPL");
