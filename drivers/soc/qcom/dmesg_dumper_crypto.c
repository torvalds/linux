// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "dmesg_dumper_crypto: " fmt

#include <crypto/aead.h>
#include <crypto/akcipher.h>
#include <linux/kmsg_dump.h>
#include <linux/of.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/sizes.h>

#include "dmesg_dumper_private.h"

struct pubkey_data {
	u8 *pubkey;
	u32 pubkeysize;
};

static struct pubkey_data *input_data;
static bool init_done;

static int qcom_ddump_encrypt_key_with_rsa(u8 *nonce, u64 nonce_size,
			struct encrypt_data *output_data)
{
	struct scatterlist sg_cipher;
	struct akcipher_request *req;
	struct scatterlist sg_plain;
	struct crypto_akcipher *tfm;
	DECLARE_CRYPTO_WAIT(wait);
	u64 ciphersize;
	u64 plainsize;
	u8 *cipher;
	u8 *plain;
	u8 *key;
	int err;

	plainsize = nonce_size;
	ciphersize = AES_256_ENCRYPTED_KEY_SIZE;

	key = kmemdup(input_data->pubkey, input_data->pubkeysize, GFP_KERNEL);
	if (!key) {
		err = -ENOMEM;
		return err;
	}

	plain = kmemdup(nonce, nonce_size, GFP_KERNEL);
	if (!plain) {
		err = -ENOMEM;
		goto free_key;
	}

	sg_init_one(&sg_plain, plain, plainsize);
	cipher = kzalloc(ciphersize, GFP_KERNEL);
	if (!cipher) {
		err = -ENOMEM;
		goto free_plain;
	}

	sg_init_one(&sg_cipher, cipher, ciphersize);
	tfm = crypto_alloc_akcipher("rsa", 0, 0);
	if (IS_ERR(tfm)) {
		err = PTR_ERR(tfm);
		pr_err("Error allocating rsa handle: %ld\n", PTR_ERR(tfm));
		goto free_cipher;
	}

	req = akcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		err = -ENOMEM;
		goto free_akcipher;
	}

	/* use public key to encrypt */
	err = crypto_akcipher_set_pub_key(tfm, key, input_data->pubkeysize);
	if (err) {
		pr_err("Error setting public key: %d\n", err);
		goto free_akcipher_request;
	}
	akcipher_request_set_crypt(req, &sg_plain, &sg_cipher, plainsize, ciphersize);
	akcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
							  crypto_req_done, &wait);
	/* Wait for encryption result */
	err = crypto_wait_req(crypto_akcipher_encrypt(req), &wait);
	if (err) {
		pr_err("Error rsa encrypting: %d\n", err);
		goto free_akcipher_request;
	}

	memcpy(output_data->key, cipher, AES_256_ENCRYPTED_KEY_SIZE);

free_akcipher_request:
	akcipher_request_free(req);
	req = NULL;
free_akcipher:
	crypto_free_akcipher(tfm);
	tfm = NULL;
free_cipher:
	kfree(cipher);
	cipher = NULL;
free_plain:
	memset(plain, 0, plainsize);
	kfree(plain);
	plain = NULL;
free_key:
	kfree(key);
	key = NULL;
	return err;
}

static int qcom_ddump_encrypt_log_with_gcm(u8 *log, u64 log_size,
			u8 *key, u32 key_size, struct encrypt_data *output_data)
{
	struct scatterlist sg_cipher;
	struct scatterlist sg_plain;
	DECLARE_CRYPTO_WAIT(wait);
	struct aead_request *req;
	struct crypto_aead *tfm;
	u64 ciphersize;
	u64 plainsize;
	u8 iv[IV_LEN];
	u8 *cipher;
	u8 *plain;
	int err;

	plain = log;
	plainsize = log_size;
	ciphersize = plainsize + TAG_LEN;

	/* Get random iv */
	get_random_bytes(iv, IV_LEN);
	/* crypto api needs scatterlist as input */
	sg_init_one(&sg_plain, plain, plainsize);
	cipher = kzalloc(ciphersize, GFP_KERNEL);
	if (!cipher) {
		err = -ENOMEM;
		return err;
	}

	sg_init_one(&sg_cipher, cipher, ciphersize);
	/* Set aead crypto environment */
	tfm = crypto_alloc_aead("gcm(aes)", 0, 0);
	if (IS_ERR(tfm)) {
		pr_err("Error allocating gcm(aes) handle: %ld\n", PTR_ERR(tfm));
		err = PTR_ERR(tfm);
		goto free_cipher;
	}

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		err = -ENOMEM;
		goto free_akcipher;
	}

	err = crypto_aead_setkey(tfm, key, key_size);
	if (err) {
		pr_err("Error setting key: %d\n", err);
		goto free_akcipher_request;
	}

	err = crypto_aead_setauthsize(tfm, TAG_LEN);
	if (err) {
		pr_err("Error setting authsize: %d\n", err);
		goto free_akcipher_request;
	}

	/* Encrypt plain and get cipher */
	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
							  crypto_req_done, &wait);
	aead_request_set_crypt(req, &sg_plain, &sg_cipher, plainsize, iv);
	/* Wait for encryption result */
	err = crypto_wait_req(crypto_aead_encrypt(req), &wait);
	if (err) {
		pr_err("Error encrypting: %d\n", err);
		goto free_akcipher_request;
	}

	memcpy(output_data->iv, iv, IV_LEN);
	memcpy(output_data->tag, cipher + plainsize, TAG_LEN);
	memcpy(output_data->cipher_log, cipher, ciphersize);

free_akcipher_request:
	aead_request_free(req);
	req = NULL;
free_akcipher:
	crypto_free_aead(tfm);
	tfm = NULL;
free_cipher:
	kfree(cipher);
	cipher = NULL;
	return err;
}

static int qcom_ddump_do_alive_log_encrypt(u8 *log, u64 log_size,
		struct encrypt_data *output_data)
{
	u8 key[KEY_LEN];
	int ret;

	/* Get random key */
	get_random_bytes(key, KEY_LEN);

	ret = qcom_ddump_encrypt_key_with_rsa(key, KEY_LEN, output_data);
	if (ret) {
		pr_err("Error encrypt key with rsa: %d\n", ret);
		return ret;
	}

	ret = qcom_ddump_encrypt_log_with_gcm(log, log_size, key, KEY_LEN, output_data);
	if (ret) {
		pr_err("Error encrypt log with gcm: %d\n", ret);
		return ret;
	}

	output_data->frame_size = sizeof(*output_data) + log_size + TAG_LEN;
	return ret;
}

int qcom_ddump_alive_log_to_shm(struct qcom_dmesg_dumper *qdd,
			     u64 user_size)
{
	size_t total_len, line_len;
	struct ddump_shm_hdr *hdr;
	u64 valid_size;
	void *buf;
	void *base;
	int ret;

	if (!init_done) {
		pr_err("%s: Driver probe failed\n", __func__);
		return -ENXIO;
	}

	if (!qdd)
		return -EINVAL;

	total_len = 0;
	hdr = qdd->base;
	hdr->svm_dump_len = 0;

	valid_size = qcom_ddump_get_valid_size(qdd, user_size);
	if (valid_size < LOG_LINE_MAX)
		return -EINVAL;

	buf = vzalloc(valid_size + ALIGN_LEN);
	if (!buf)
		return -ENOMEM;

	base = buf;
	while ((total_len < valid_size - LOG_LINE_MAX) &&
		   kmsg_dump_get_line(&qdd->iter, false, base, valid_size - total_len, &line_len) &&
		   (line_len > 0)) {
		base = base + line_len;
		total_len = total_len + line_len;
	}

	if (total_len == 0) {
		vfree(buf);
		return 0;
	}

	/* log len need 16 bytes algin to do encrypt */
	total_len = ALIGN(total_len, ALIGN_LEN);
	ret = qcom_ddump_do_alive_log_encrypt(buf, total_len, &hdr->data);
	vfree(buf);
	if (ret)
		return ret;

	hdr->svm_dump_len = hdr->data.frame_size;
	return ret;
}

void qcom_ddump_encrypt_exit(void)
{
	if (!init_done) {
		pr_err("%s: Driver probe failed\n", __func__);
		return;
	}

	vfree(input_data->pubkey);
	input_data->pubkey = NULL;
	kfree(input_data);
	input_data = NULL;
	init_done = false;
}

int qcom_ddump_encrypt_init(struct device_node *node)
{
	int ret;

	if (!node)
		return -EINVAL;

	input_data = kzalloc(sizeof(struct pubkey_data), GFP_KERNEL);
	if (!input_data)
		return -ENOMEM;

	ret = of_property_read_u32(node, "ddump-pubkey-size", &input_data->pubkeysize);
	if (ret) {
		pr_err("Failed to read ddump-pubkey-size %d\n", ret);
		goto free_input_data;
	}

	input_data->pubkey = vzalloc(input_data->pubkeysize);
	if (!input_data->pubkey) {
		ret = -ENOMEM;
		goto free_input_data;
	}

	ret = of_property_read_u8_array(node, "ddump-pubkey",
				input_data->pubkey, input_data->pubkeysize);
	if (ret) {
		pr_err("Failed to read ddump-pubkey %d\n", ret);
		goto free_pubkey;
	}

	init_done = true;
	return ret;

free_pubkey:
	vfree(input_data->pubkey);
	input_data->pubkey = NULL;
free_input_data:
	kfree(input_data);
	input_data = NULL;
	return ret;
}
