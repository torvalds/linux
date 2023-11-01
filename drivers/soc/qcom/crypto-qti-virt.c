// SPDX-License-Identifier: GPL-2.0-only
/*
 * Crypto virtual library for storage encryption.
 *
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/habmm.h>
#include <linux/crypto_qti_virt.h>
#include <linux/ktime.h>
#include <linux/kthread.h>
#include <linux/completion.h>

#define	RESERVE_SIZE                    (36*sizeof(uint16_t))
#define	SECRET_SIZE                     (32)

#define	HAB_TIMEOUT_MS                  (50000)

/* FBE request command ids */
#define	FBE_GET_MAX_SLOTS               (7)
#define	FBE_SET_KEY_V2                  (8)
#define	FBE_CLEAR_KEY_V2                (9)
#define	FBE_DERIVE_RAW_SECRET           (10)
#define	FBE_GET_CRYPTO_CAPABILITIES     (11)
#define	FBE_VERIFY_CRYPTO_CAPS          (12)

#define	MAX_CRYPTO_MODES_SUPPORTED (4)

struct fbe_derive_secret {
	uint8_t wrapped_key[BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE];
	uint32_t wrapped_key_size;
};

struct fbe_request_v2_t {
	uint8_t reserve[RESERVE_SIZE];	/*for compatibility*/
	uint32_t cmd;
	uint8_t  key[BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE];
	uint32_t key_size;
	uint32_t virt_slot;
	uint32_t data_unit_size;
	enum blk_crypto_mode_num crypto_mode;
	struct fbe_derive_secret derive_raw_secret;
};

struct fbe_v2_resp {
	int32_t status;
	uint8_t secret_key[SECRET_SIZE];
	uint32_t crypto_modes_supported[MAX_CRYPTO_MODES_SUPPORTED];
};

struct fbe_req_args {
	struct fbe_request_v2_t req;
	struct fbe_v2_resp response;
	int32_t ret;
};

static struct completion send_fbe_req_done;

static int32_t send_fbe_req_hab(void *arg)
{
	int ret = 0;
	uint32_t status_size;
	uint32_t handle;
	struct fbe_req_args *req_args = (struct fbe_req_args *)arg;

	do {
		if (!req_args) {
			pr_err("%s Null input\n", __func__);
			ret = -EINVAL;
			break;
		}

		ret = habmm_socket_open(&handle, MM_FDE_1, 0, 0);
		if (ret) {
			pr_err("habmm_socket_open failed with ret = %d\n", ret);
			break;
		}

		ret = habmm_socket_send(handle, &req_args->req, sizeof(struct fbe_request_v2_t), 0);
		if (ret) {
			pr_err("habmm_socket_send failed, ret= 0x%x\n", ret);
			break;
		}

		do {
			status_size = sizeof(struct fbe_v2_resp);
			ret = habmm_socket_recv(handle, &req_args->response, &status_size, 0,
						HABMM_SOCKET_RECV_FLAGS_UNINTERRUPTIBLE);
		} while (-EINTR == ret);

		if (ret) {
			pr_err("habmm_socket_recv failed, ret= 0x%x\n", ret);
			break;
		}
		if (status_size != sizeof(struct fbe_v2_resp)) {
			pr_err("habmm_socket_recv expected size: %lu, actual=%u\n",
			       sizeof(struct fbe_v2_resp),
			       status_size);
			ret = -E2BIG;
			break;
		}

		ret = habmm_socket_close(handle);
		if (ret) {
			pr_err("habmm_socket_close failed with ret = %d\n", ret);
			break;
		}
	} while (0);

	req_args->ret = ret;

	complete(&send_fbe_req_done);

	return 0;
}

static void send_fbe_req(struct fbe_req_args *arg)
{
	struct task_struct *thread;

	init_completion(&send_fbe_req_done);
	arg->response.status  = 0;

	thread = kthread_run(send_fbe_req_hab, arg, "send_fbe_req");
	if (IS_ERR(thread)) {
		arg->ret = -1;
		return;
	}

	if (wait_for_completion_interruptible_timeout(
		&send_fbe_req_done, msecs_to_jiffies(HAB_TIMEOUT_MS)) <= 0) {
		pr_err("%s: timeout hit\n", __func__);
		kthread_stop(thread);
		arg->ret = -ETIME;
		return;
	}
}

int crypto_qti_virt_ice_get_info(uint32_t *total_num_slots)
{
	struct fbe_req_args arg;

	if (!total_num_slots) {
		pr_err("%s Null input\n", __func__);
		return -EINVAL;
	}

	arg.req.cmd = FBE_GET_MAX_SLOTS;
	send_fbe_req(&arg);
	if (arg.ret || arg.response.status < 0) {
		pr_err("send_fbe_req_v2 failed with ret = %d, max_slots = %d\n",
		       arg.ret, arg.response.status);
		return -ECOMM;
	}

	*total_num_slots = (uint32_t) arg.response.status;

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_qti_virt_ice_get_info);

static int verify_crypto_capabilities(enum blk_crypto_mode_num crypto_mode,
				      unsigned int data_unit_size)
{
	struct fbe_req_args arg;

	arg.req.cmd = FBE_VERIFY_CRYPTO_CAPS;
	arg.req.crypto_mode = crypto_mode;
	arg.req.data_unit_size = data_unit_size;
	send_fbe_req(&arg);
	if (arg.ret || arg.response.status < 0) {
		pr_err("send_fbe_req_v2 failed with ret = %d, status = %d\n",
			arg.ret, arg.response.status);
		return -EINVAL;
	}

	return arg.response.status;
}

int crypto_qti_virt_program_key(const struct blk_crypto_key *key,
						unsigned int slot)
{
	struct fbe_req_args arg;
	int ret = 0;

	if (!key)
		return -EINVAL;

	/* Actual determination of capabilities for UFS/EMMC for different
	 * encryption modes are done in the back end (host operating system)
	 * in case of virtualization driver, so will send details to backend
	 * and BE will verify the given capabilities.
	 */
	ret = verify_crypto_capabilities(key->crypto_cfg.crypto_mode,
					 key->crypto_cfg.data_unit_size);
	if (ret)
		return -EINVAL;
	/* program key */
	arg.req.cmd = FBE_SET_KEY_V2;
	arg.req.virt_slot = slot;
	arg.req.key_size = key->size;
	memcpy(&(arg.req.key[0]), key->raw, key->size);
	send_fbe_req(&arg);

	if (arg.ret || arg.response.status) {
		pr_err("send_fbe_req_v2 failed with ret = %d, status = %d\n",
		       arg.ret, arg.response.status);
		return -ECOMM;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_qti_virt_program_key);

int crypto_qti_virt_invalidate_key(unsigned int slot)
{
	struct fbe_req_args arg;

	arg.req.cmd = FBE_CLEAR_KEY_V2;
	arg.req.virt_slot = slot;

	send_fbe_req(&arg);

	if (arg.ret || arg.response.status) {
		pr_err("send_fbe_req_v2 failed with ret = %d, status = %d\n",
		       arg.ret, arg.response.status);
		return -ECOMM;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_qti_virt_invalidate_key);

int crypto_qti_virt_get_crypto_capabilities(unsigned int *crypto_modes_supported,
					    uint32_t crypto_array_size)
{
	struct fbe_req_args arg;

	// To compatible with BE(Back End)
	crypto_array_size -= sizeof(uint32_t);

	arg.req.cmd = FBE_GET_CRYPTO_CAPABILITIES;

	send_fbe_req(&arg);

	if (arg.ret || arg.response.status) {
		pr_err("send_fbe_req_v2 failed with ret = %d, status = %d\n",
		       arg.ret, arg.response.status);
		return -ECOMM;
	}
	memcpy(crypto_modes_supported, &(arg.response.crypto_modes_supported[0]),
	       crypto_array_size);

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_qti_virt_get_crypto_capabilities);

int crypto_qti_virt_derive_raw_secret_platform(const u8 *wrapped_key,
					       unsigned int wrapped_key_size,
					       u8 *secret,
					       unsigned int secret_size)
{
	struct fbe_req_args arg;

	arg.req.cmd = FBE_DERIVE_RAW_SECRET;
	memcpy(&(arg.req.derive_raw_secret.wrapped_key[0]), wrapped_key,
	       wrapped_key_size);
	arg.req.derive_raw_secret.wrapped_key_size = wrapped_key_size;

	send_fbe_req(&arg);

	if (arg.ret || arg.response.status) {
		pr_err("send_fbe_req_v2 failed with ret = %d, status = %d\n",
		       arg.ret, arg.response.status);
		return -EINVAL;
	}
	memcpy(secret, &(arg.response.secret_key[0]), secret_size);

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_qti_virt_derive_raw_secret_platform);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Crypto Virtual library for storage encryption");

