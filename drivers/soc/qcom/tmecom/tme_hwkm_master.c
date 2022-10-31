// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "tme_hwkm_master_intf.h"
#include "tmecom.h"

#define TME_MSG_CBOR_TAG_HWKM   (303)

#define TME_CLEAR_KEY_CBOR_TAG     0x482F01D9 /* _be32 0xD9012F48 */
#define TME_DERIVE_KEY_CBOR_TAG    0x582F01D9 /* _be32 0xD9012F58 */
#define TME_GENERATE_KEY_CBOR_TAG  0x542F01D9 /* _be32 0xD9012F54 */
#define TME_IMPORT_KEY_CBOR_TAG    0x582F01D9 /* _be32 0xD9012F58 */
#define TME_WRAP_KEY_CBOR_TAG      0x502F01D9 /* _be32 0xD9012F50 */
#define TME_UNWRAP_KEY_CBOR_TAG    0x582F01D9 /* _be32 0xD9012F58 */
#define TME_BORADCAST_KEY_CBOR_TAG 0x442F01D9 /* _be32 0xD9012F44 */

/*
 * Static alloc for wrapped key
 * Protected by tmecom dev mutex
 */
static struct wrap_key_resp gwrpk_response = {0};

static inline uint32_t update_ext_err(
		struct tme_ext_err_info *err_info,
		struct tme_response_sts *result)
{
	bool is_failure = false;

	err_info->tme_err_status     = result->tme_err_status;
	err_info->seq_err_status     = result->seq_err_status;
	err_info->seq_kp_err_status0 = result->seq_kp_err_status0;
	err_info->seq_kp_err_status1 = result->seq_kp_err_status1;
	err_info->seq_rsp_status     = result->seq_rsp_status;

	is_failure = err_info->tme_err_status ||
		err_info->seq_err_status ||
		err_info->seq_kp_err_status0 ||
		err_info->seq_kp_err_status1;

	print_hex_dump_bytes("err_info decoded bytes : ",
			DUMP_PREFIX_ADDRESS, (void *)err_info,
			sizeof(*err_info));

	return  is_failure ? 1 : 0;
}

uint32_t tme_hwkm_master_clearkey(uint32_t key_id,
		struct tme_ext_err_info *err_info)
{
	struct clear_key_req *request = NULL;
	struct tme_response_sts *response = NULL;
	uint32_t ret = 0;
	size_t response_len = sizeof(*response);

	if (!err_info)
		return -EINVAL;

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	response = kzalloc(response_len, GFP_KERNEL);

	if (!request || !response) {
		ret = -ENOMEM;
		goto err_exit;
	}

	request->cmd.code    = TME_HWKM_CMD_CLEAR_KEY;
	request->key_id      = key_id;
	request->cbor_header = TME_CLEAR_KEY_CBOR_TAG;

	ret = tmecom_process_request(request, sizeof(*request), response,
			&response_len);

	if (ret != 0) {
		pr_err("HWKM clear key request failed for %d\n", key_id);
		goto err_exit;
	}

	if (response_len != sizeof(*response)) {
		pr_err("HWKM response failed with invalid length: %u, %u\n",
				response_len, sizeof(response));
		ret = -EBADMSG;
		goto err_exit;
	}

	ret = update_ext_err(err_info, response);

err_exit:
	kfree(request);
	kfree(response);
	return ret;
}
EXPORT_SYMBOL(tme_hwkm_master_clearkey);

uint32_t tme_hwkm_master_generatekey(uint32_t key_id,
		struct tme_key_policy *policy,
		uint32_t cred_slot,
		struct tme_ext_err_info *err_info)
{
	struct gen_key_req *request = NULL;
	struct tme_response_sts *response = NULL;
	uint32_t ret = 0;
	size_t response_len = sizeof(*response);

	if (!err_info || !policy)
		return -EINVAL;

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	response = kzalloc(response_len, GFP_KERNEL);

	if (!request || !response) {
		ret = -ENOMEM;
		goto err_exit;
	}

	request->cmd.code    = TME_HWKM_CMD_GENERATE_KEY;
	request->key_id      = key_id;
	request->cred_slot   = cred_slot;
	request->cbor_header = TME_GENERATE_KEY_CBOR_TAG;
	memcpy(&request->key_policy, policy, sizeof(*policy));

	ret = tmecom_process_request(request, sizeof(*request), response,
			&response_len);

	if (ret != 0) {
		pr_err("HWKM generate key request failed for %d\n", key_id);
		goto err_exit;
	}

	if (response_len != sizeof(*response)) {
		pr_err("HWKM response failed with invalid length: %u, %u\n",
				response_len, sizeof(response));
		ret = -EBADMSG;
		goto err_exit;
	}

	ret = update_ext_err(err_info, response);

err_exit:
	kfree(request);
	kfree(response);
	return ret;
}
EXPORT_SYMBOL(tme_hwkm_master_generatekey);

uint32_t tme_hwkm_master_derivekey(uint32_t key_id,
		struct tme_kdf_spec *kdf_info,
		uint32_t cred_slot,
		struct tme_ext_err_info *err_info)
{
	struct derive_key_req *request = NULL;
	struct tme_response_sts *response = NULL;
	uint32_t ret = 0;
	size_t response_len = sizeof(*response);

	if (!kdf_info || !err_info)
		return -EINVAL;

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	response = kzalloc(response_len, GFP_KERNEL);

	if (!request || !response) {
		ret = -ENOMEM;
		goto err_exit;
	}

	request->cmd.code    = TME_HWKM_CMD_DERIVE_KEY;
	request->key_id      = key_id;
	request->cred_slot   = cred_slot;
	request->cbor_header = TME_DERIVE_KEY_CBOR_TAG;
	memcpy(&request->kdf_info, kdf_info, sizeof(*kdf_info));

	ret = tmecom_process_request(request, sizeof(*request), response,
			&response_len);

	if (ret != 0) {
		pr_err("HWKM derive key request failed for %d\n", key_id);
		goto err_exit;
	}

	if (response_len != sizeof(*response)) {
		pr_err("HWKM response failed with invalid length: %u, %u\n",
				response_len, sizeof(response));
		ret = -EBADMSG;
		goto err_exit;
	}

	ret = update_ext_err(err_info, response);

err_exit:
	kfree(request);
	kfree(response);
	return ret;
}
EXPORT_SYMBOL(tme_hwkm_master_derivekey);

uint32_t tme_hwkm_master_wrapkey(uint32_t key_id,
		uint32_t targetkey_id,
		uint32_t cred_slot,
		struct tme_wrapped_key *wrapped,
		struct tme_ext_err_info *err_info)
{
	struct wrap_key_req *request = NULL;
	struct wrap_key_resp *wrpk_response = NULL;
	uint32_t ret = 0;
	size_t response_len = sizeof(*wrpk_response);

	if (!wrapped || !err_info)
		return -EINVAL;

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	wrpk_response = &gwrpk_response;

	if (!request)
		return -ENOMEM;

	request->cmd.code       = TME_HWKM_CMD_WRAP_KEY;
	request->key_id         = key_id;
	request->target_key_id  = targetkey_id;
	request->cbor_header    = TME_WRAP_KEY_CBOR_TAG;

	ret = tmecom_process_request(request, sizeof(*request), wrpk_response,
			&response_len);

	if (ret != 0) {
		pr_err("HWKM wrap key request failed for %d\n", key_id);
		goto err_exit;
	}

	if (response_len != sizeof(*wrpk_response)) {
		pr_err("HWKM response failed with invalid length: %u, %u\n",
				response_len, sizeof(wrpk_response));
		ret = -EBADMSG;
		goto err_exit;
	}

	ret = update_ext_err(err_info, &wrpk_response->status);

	if (!ret)
		memcpy(wrapped, &wrpk_response->wrapped_key, sizeof(*wrapped));

err_exit:
	kfree(request);
	return ret;
}
EXPORT_SYMBOL(tme_hwkm_master_wrapkey);

uint32_t tme_hwkm_master_unwrapkey(uint32_t key_id,
		uint32_t kwkey_id,
		uint32_t cred_slot,
		struct tme_wrapped_key *wrapped,
		struct tme_ext_err_info *err_info)
{
	struct unwrap_key_req *request = NULL;
	struct tme_response_sts *response = NULL;
	uint32_t ret = 0;
	size_t response_len = sizeof(*response);

	if (!wrapped || !err_info)
		return -EINVAL;

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	response = kzalloc(response_len, GFP_KERNEL);

	if (!request || !response) {
		ret = -ENOMEM;
		goto err_exit;
	}

	request->cmd.code    = TME_HWKM_CMD_UNWRAP_KEY;
	request->key_id      = key_id;
	request->kw_key_id   = kwkey_id;
	request->cbor_header = TME_UNWRAP_KEY_CBOR_TAG;
	memcpy(&request->wrapped, wrapped, sizeof(*wrapped));

	ret = tmecom_process_request(request, sizeof(*request), response,
			&response_len);

	if (ret != 0) {
		pr_err("HWKM unwrap key request failed for %d\n", key_id);
		goto err_exit;
	}

	if (response_len != sizeof(*response)) {
		pr_err("HWKM response failed with invalid length: %u, %u\n",
				response_len, sizeof(response));
		ret = -EBADMSG;
		goto err_exit;
	}

	ret = update_ext_err(err_info, response);

err_exit:
	kfree(request);
	kfree(response);
	return ret;
}
EXPORT_SYMBOL(tme_hwkm_master_unwrapkey);

uint32_t tme_hwkm_master_importkey(uint32_t key_id,
		struct tme_key_policy *policy,
		struct tme_plaintext_key *key_material,
		uint32_t cred_slot,
		struct tme_ext_err_info *err_info)
{
	struct import_key_req *request = NULL;
	struct tme_response_sts *response = NULL;
	uint32_t ret = 0;
	size_t response_len = sizeof(*response);

	if (!key_material || !err_info || !policy)
		return -EINVAL;

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	response = kzalloc(response_len, GFP_KERNEL);

	if (!request || !response) {
		ret = -ENOMEM;
		goto err_exit;
	}

	request->cmd.code     = TME_HWKM_CMD_IMPORT_KEY;
	request->key_id       = key_id;
	request->cred_slot    = cred_slot;
	request->cbor_header  = TME_IMPORT_KEY_CBOR_TAG;
	memcpy(&request->key_policy, policy, sizeof(*policy));
	memcpy(&request->key_material, key_material, sizeof(*key_material));

	ret = tmecom_process_request(request, sizeof(*request), response,
			&response_len);

	if (ret != 0) {
		pr_err("HWKM import key request failed for %d\n", key_id);
		goto err_exit;
	}

	if (response_len != sizeof(*response)) {
		pr_err("HWKM response failed with invalid length: %u, %u\n",
				response_len, sizeof(response));
		ret = -EBADMSG;
		goto err_exit;
	}

	ret = update_ext_err(err_info, response);

err_exit:
	kfree(request);
	kfree(response);
	return ret;
}
EXPORT_SYMBOL(tme_hwkm_master_importkey);

uint32_t tme_hwkm_master_broadcast_transportkey(
		struct tme_ext_err_info *err_info)
{
	struct broadcast_tpkey_req *request = NULL;
	struct tme_response_sts *response = NULL;
	uint32_t ret = 0;
	size_t response_len = sizeof(*response);

	if (!err_info)
		return -EINVAL;

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	response = kzalloc(response_len, GFP_KERNEL);

	if (!request || !response) {
		ret = -ENOMEM;
		goto err_exit;
	}

	request->cbor_header = TME_BORADCAST_KEY_CBOR_TAG;
	request->cmd.code    = TME_HWKM_CMD_BROADCAST_TP_KEY;

	ret = tmecom_process_request(request, sizeof(*request), response,
			&response_len);

	if (ret != 0) {
		pr_err("HWKM broadcast TP key request failed\n");
		goto err_exit;
	}

	if (response_len != sizeof(*response)) {
		pr_err("HWKM response failed with invalid length: %u, %u\n",
				response_len, sizeof(response));
		ret = -EBADMSG;
		goto err_exit;
	}

	ret = update_ext_err(err_info, response);

err_exit:
	kfree(request);
	kfree(response);
	return ret;
}
EXPORT_SYMBOL(tme_hwkm_master_broadcast_transportkey);

