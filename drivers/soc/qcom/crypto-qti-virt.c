// SPDX-License-Identifier: GPL-2.0-only
/*
 * Crypto virtual library for storage encryption.
 *
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

static int32_t send_fbe_req(struct fbe_request_v2_t *req, struct fbe_v2_resp *response)
{
	int32_t ret = 0;
	uint32_t status_size;
	uint32_t handle = 0;

	do {
		if (!req || !response) {
			pr_err("%s Null input\n", __func__);
			ret = -EINVAL;
			break;
		}

		ret = habmm_socket_open(&handle, MM_FDE_1, 0,
					HABMM_SOCKET_OPEN_FLAGS_UNINTERRUPTIBLE);
		if (ret) {
			pr_err("habmm_socket_open failed with ret = %d\n", ret);
			break;
		}

		ret = habmm_socket_send(handle, req, sizeof(struct fbe_request_v2_t), 0);
		if (ret) {
			pr_err("habmm_socket_send failed, ret= 0x%x\n", ret);
			break;
		}

		do {
			status_size = sizeof(struct fbe_v2_resp);
			ret = habmm_socket_recv(handle, response, &status_size, 0,
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

	} while (0);

	if (handle) {
		ret = habmm_socket_close(handle);
		if (ret)
			pr_err("habmm_socket_close failed with err = %d\n", ret);
	}

	return ret;
}

int crypto_qti_virt_ice_get_info(uint32_t *total_num_slots)
{
	struct fbe_request_v2_t req;
	struct fbe_v2_resp response;
	int32_t ret = 0;

	if (!total_num_slots) {
		pr_err("%s: Null input\n", __func__);
		return -EINVAL;
	}

	req.cmd = FBE_GET_MAX_SLOTS;
	ret = send_fbe_req(&req, &response);
	if (ret || response.status < 0) {
		pr_err("%s: send_fbe_req_v2 failed with ret = %d, max_slots = %d\n",
		       __func__, ret, response.status);
		return -ECOMM;
	}

	*total_num_slots = (uint32_t) response.status;

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_qti_virt_ice_get_info);

static int verify_crypto_capabilities(enum blk_crypto_mode_num crypto_mode,
				      unsigned int data_unit_size)
{
	struct fbe_request_v2_t req;
	struct fbe_v2_resp response;
	int32_t ret = 0;

	req.cmd = FBE_VERIFY_CRYPTO_CAPS;
	req.crypto_mode = crypto_mode;
	req.data_unit_size = data_unit_size;
	ret = send_fbe_req(&req, &response);
	if (ret || response.status < 0) {
		pr_err("%s: send_fbe_req_v2 failed with ret = %d, status = %d\n",
			__func__, ret, response.status);
		return -EINVAL;
	}

	return response.status;
}

int crypto_qti_virt_program_key(const struct blk_crypto_key *key,
						unsigned int slot)
{
	struct fbe_request_v2_t req;
	struct fbe_v2_resp response;
	int32_t ret = 0;

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
	req.cmd = FBE_SET_KEY_V2;
	req.virt_slot = slot;
	req.key_size = key->size;
	memcpy(&(req.key[0]),  key->raw, key->size);
	ret = send_fbe_req(&req, &response);

	if (ret || response.status) {
		pr_err("%s: send_fbe_req_v2 failed with ret = %d, status = %d\n",
		       __func__, ret, response.status);
		return -ECOMM;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_qti_virt_program_key);

int crypto_qti_virt_invalidate_key(unsigned int slot)
{
	struct fbe_request_v2_t req;
	struct fbe_v2_resp response;
	int32_t ret = 0;

	req.cmd = FBE_CLEAR_KEY_V2;
	req.virt_slot = slot;

	ret = send_fbe_req(&req, &response);

	if (ret || response.status) {
		pr_err("%s: send_fbe_req_v2 failed with ret = %d, status = %d\n",
		       __func__, ret, response.status);
		return -ECOMM;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_qti_virt_invalidate_key);

int crypto_qti_virt_get_crypto_capabilities(unsigned int *crypto_modes_supported,
					    uint32_t crypto_array_size)
{
	struct fbe_request_v2_t req;
	struct fbe_v2_resp response;
	int32_t ret = 0;

	// To compatible with BE(Back End)
	crypto_array_size -= sizeof(uint32_t);

	req.cmd = FBE_GET_CRYPTO_CAPABILITIES;

	ret = send_fbe_req(&req, &response);

	if (ret || response.status) {
		pr_err("%s: send_fbe_req_v2 failed with ret = %d, status = %d\n",
		       __func__, ret, response.status);
		return -ECOMM;
	}
	memcpy(crypto_modes_supported, &(response.crypto_modes_supported[0]),
	       crypto_array_size);

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_qti_virt_get_crypto_capabilities);

int crypto_qti_virt_derive_raw_secret_platform(const u8 *wrapped_key,
					       unsigned int wrapped_key_size,
					       u8 *secret,
					       unsigned int secret_size)
{
	struct fbe_request_v2_t req;
	struct fbe_v2_resp response;

	int32_t ret = 0;

	req.cmd = FBE_DERIVE_RAW_SECRET;
	memcpy(&(req.derive_raw_secret.wrapped_key[0]), wrapped_key,
	       wrapped_key_size);
	req.derive_raw_secret.wrapped_key_size = wrapped_key_size;

	ret = send_fbe_req(&req, &response);

	if (ret || response.status) {
		pr_err("%s: send_fbe_req_v2 failed with ret = %d, status = %d\n",
		       __func__, ret, response.status);
		return -EINVAL;
	}
	memcpy(secret, &(response.secret_key[0]), secret_size);

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_qti_virt_derive_raw_secret_platform);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Crypto Virtual library for storage encryption");

