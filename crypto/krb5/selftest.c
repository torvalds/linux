// SPDX-License-Identifier: GPL-2.0-or-later
/* Kerberos library self-testing
 *
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <crypto/skcipher.h>
#include <crypto/hash.h>
#include "internal.h"

#define VALID(X) \
	({								\
		bool __x = (X);						\
		if (__x) {						\
			pr_warn("!!! TESTINVAL %s:%u\n", __FILE__, __LINE__); \
			ret = -EBADMSG;					\
		}							\
		__x;							\
	})

#define CHECK(X) \
	({								\
		bool __x = (X);						\
		if (__x) {						\
			pr_warn("!!! TESTFAIL %s:%u\n", __FILE__, __LINE__); \
			ret = -EBADMSG;					\
		}							\
		__x;							\
	})

enum which_key {
	TEST_KC, TEST_KE, TEST_KI,
};

#if 0
static void dump_sg(struct scatterlist *sg, unsigned int limit)
{
	unsigned int index = 0, n = 0;

	for (; sg && limit > 0; sg = sg_next(sg)) {
		unsigned int off = sg->offset, len = umin(sg->length, limit);
		const void *p = kmap_local_page(sg_page(sg));

		limit -= len;
		while (len > 0) {
			unsigned int part = umin(len, 32);

			pr_notice("[%x] %04x: %*phN\n", n, index, part, p + off);
			index += part;
			off += part;
			len -= part;
		}

		kunmap_local(p);
		n++;
	}
}
#endif

static int prep_buf(struct krb5_buffer *buf)
{
	buf->data = kmalloc(buf->len, GFP_KERNEL);
	if (!buf->data)
		return -ENOMEM;
	return 0;
}

#define PREP_BUF(BUF, LEN)					\
	do {							\
		(BUF)->len = (LEN);				\
		ret = prep_buf((BUF));				\
		if (ret < 0)					\
			goto out;				\
	} while (0)

static int load_buf(struct krb5_buffer *buf, const char *from)
{
	size_t len = strlen(from);
	int ret;

	if (len > 1 && from[0] == '\'') {
		PREP_BUF(buf, len - 1);
		memcpy(buf->data, from + 1, len - 1);
		ret = 0;
		goto out;
	}

	if (VALID(len & 1))
		return -EINVAL;

	PREP_BUF(buf, len / 2);
	ret = hex2bin(buf->data, from, buf->len);
	if (ret < 0) {
		VALID(1);
		goto out;
	}
out:
	return ret;
}

#define LOAD_BUF(BUF, FROM) do { ret = load_buf(BUF, FROM); if (ret < 0) goto out; } while (0)

static void clear_buf(struct krb5_buffer *buf)
{
	kfree(buf->data);
	buf->len = 0;
	buf->data = NULL;
}

/*
 * Perform a pseudo-random function check.
 */
static int krb5_test_one_prf(const struct krb5_prf_test *test)
{
	const struct krb5_enctype *krb5 = crypto_krb5_find_enctype(test->etype);
	struct krb5_buffer key = {}, octet = {}, result = {}, prf = {};
	int ret;

	if (!krb5)
		return -EOPNOTSUPP;

	pr_notice("Running %s %s\n", krb5->name, test->name);

	LOAD_BUF(&key,   test->key);
	LOAD_BUF(&octet, test->octet);
	LOAD_BUF(&prf,   test->prf);
	PREP_BUF(&result, krb5->prf_len);

	if (VALID(result.len != prf.len)) {
		ret = -EINVAL;
		goto out;
	}

	ret = krb5->profile->calc_PRF(krb5, &key, &octet, &result, GFP_KERNEL);
	if (ret < 0) {
		CHECK(1);
		pr_warn("PRF calculation failed %d\n", ret);
		goto out;
	}

	if (memcmp(result.data, prf.data, result.len) != 0) {
		CHECK(1);
		ret = -EKEYREJECTED;
		goto out;
	}

	ret = 0;

out:
	clear_buf(&result);
	clear_buf(&prf);
	clear_buf(&octet);
	clear_buf(&key);
	return ret;
}

/*
 * Perform a key derivation check.
 */
static int krb5_test_key(const struct krb5_enctype *krb5,
			 const struct krb5_buffer *base_key,
			 const struct krb5_key_test_one *test,
			 enum which_key which)
{
	struct krb5_buffer key = {}, result = {};
	int ret;

	LOAD_BUF(&key,   test->key);
	PREP_BUF(&result, key.len);

	switch (which) {
	case TEST_KC:
		ret = krb5_derive_Kc(krb5, base_key, test->use, &result, GFP_KERNEL);
		break;
	case TEST_KE:
		ret = krb5_derive_Ke(krb5, base_key, test->use, &result, GFP_KERNEL);
		break;
	case TEST_KI:
		ret = krb5_derive_Ki(krb5, base_key, test->use, &result, GFP_KERNEL);
		break;
	default:
		VALID(1);
		ret = -EINVAL;
		goto out;
	}

	if (ret < 0) {
		CHECK(1);
		pr_warn("Key derivation failed %d\n", ret);
		goto out;
	}

	if (memcmp(result.data, key.data, result.len) != 0) {
		CHECK(1);
		ret = -EKEYREJECTED;
		goto out;
	}

out:
	clear_buf(&key);
	clear_buf(&result);
	return ret;
}

static int krb5_test_one_key(const struct krb5_key_test *test)
{
	const struct krb5_enctype *krb5 = crypto_krb5_find_enctype(test->etype);
	struct krb5_buffer base_key = {};
	int ret;

	if (!krb5)
		return -EOPNOTSUPP;

	pr_notice("Running %s %s\n", krb5->name, test->name);

	LOAD_BUF(&base_key, test->key);

	ret = krb5_test_key(krb5, &base_key, &test->Kc, TEST_KC);
	if (ret < 0)
		goto out;
	ret = krb5_test_key(krb5, &base_key, &test->Ke, TEST_KE);
	if (ret < 0)
		goto out;
	ret = krb5_test_key(krb5, &base_key, &test->Ki, TEST_KI);
	if (ret < 0)
		goto out;

out:
	clear_buf(&base_key);
	return ret;
}

/*
 * Perform an encryption test.
 */
static int krb5_test_one_enc(const struct krb5_enc_test *test, void *buf)
{
	const struct krb5_enctype *krb5 = crypto_krb5_find_enctype(test->etype);
	struct crypto_aead *ci = NULL;
	struct krb5_buffer K0 = {}, Ke = {}, Ki = {}, keys = {};
	struct krb5_buffer conf = {}, plain = {}, ct = {};
	struct scatterlist sg[1];
	size_t data_len, data_offset, message_len;
	int ret;

	if (!krb5)
		return -EOPNOTSUPP;

	pr_notice("Running %s %s\n", krb5->name, test->name);

	/* Load the test data into binary buffers. */
	LOAD_BUF(&conf, test->conf);
	LOAD_BUF(&plain, test->plain);
	LOAD_BUF(&ct, test->ct);

	if (test->K0) {
		LOAD_BUF(&K0, test->K0);
	} else {
		LOAD_BUF(&Ke, test->Ke);
		LOAD_BUF(&Ki, test->Ki);

		ret = krb5->profile->load_encrypt_keys(krb5, &Ke, &Ki, &keys, GFP_KERNEL);
		if (ret < 0)
			goto out;
	}

	if (VALID(conf.len != krb5->conf_len) ||
	    VALID(ct.len != krb5->conf_len + plain.len + krb5->cksum_len))
		goto out;

	data_len = plain.len;
	message_len = crypto_krb5_how_much_buffer(krb5, KRB5_ENCRYPT_MODE,
						  data_len, &data_offset);

	if (CHECK(message_len != ct.len)) {
		pr_warn("Encrypted length mismatch %zu != %u\n", message_len, ct.len);
		goto out;
	}
	if (CHECK(data_offset != conf.len)) {
		pr_warn("Data offset mismatch %zu != %u\n", data_offset, conf.len);
		goto out;
	}

	memcpy(buf, conf.data, conf.len);
	memcpy(buf + data_offset, plain.data, plain.len);

	/* Allocate a crypto object and set its key. */
	if (test->K0)
		ci = crypto_krb5_prepare_encryption(krb5, &K0, test->usage, GFP_KERNEL);
	else
		ci = krb5_prepare_encryption(krb5, &keys, GFP_KERNEL);

	if (IS_ERR(ci)) {
		ret = PTR_ERR(ci);
		ci = NULL;
		pr_err("Couldn't alloc AEAD %s: %d\n", krb5->encrypt_name, ret);
		goto out;
	}

	/* Encrypt the message. */
	sg_init_one(sg, buf, message_len);
	ret = crypto_krb5_encrypt(krb5, ci, sg, 1, message_len,
				  data_offset, data_len, true);
	if (ret < 0) {
		CHECK(1);
		pr_warn("Encryption failed %d\n", ret);
		goto out;
	}
	if (ret != message_len) {
		CHECK(1);
		pr_warn("Encrypted message wrong size %x != %zx\n", ret, message_len);
		goto out;
	}

	if (memcmp(buf, ct.data, ct.len) != 0) {
		CHECK(1);
		pr_warn("Ciphertext mismatch\n");
		pr_warn("BUF %*phN\n", ct.len, buf);
		pr_warn("CT  %*phN\n", ct.len, ct.data);
		pr_warn("PT  %*phN%*phN\n", conf.len, conf.data, plain.len, plain.data);
		ret = -EKEYREJECTED;
		goto out;
	}

	/* Decrypt the encrypted message. */
	data_offset = 0;
	data_len = message_len;
	ret = crypto_krb5_decrypt(krb5, ci, sg, 1, &data_offset, &data_len);
	if (ret < 0) {
		CHECK(1);
		pr_warn("Decryption failed %d\n", ret);
		goto out;
	}

	if (CHECK(data_offset != conf.len) ||
	    CHECK(data_len != plain.len))
		goto out;

	if (memcmp(buf, conf.data, conf.len) != 0) {
		CHECK(1);
		pr_warn("Confounder mismatch\n");
		pr_warn("ENC %*phN\n", conf.len, buf);
		pr_warn("DEC %*phN\n", conf.len, conf.data);
		ret = -EKEYREJECTED;
		goto out;
	}

	if (memcmp(buf + conf.len, plain.data, plain.len) != 0) {
		CHECK(1);
		pr_warn("Plaintext mismatch\n");
		pr_warn("BUF %*phN\n", plain.len, buf + conf.len);
		pr_warn("PT  %*phN\n", plain.len, plain.data);
		ret = -EKEYREJECTED;
		goto out;
	}

	ret = 0;

out:
	clear_buf(&ct);
	clear_buf(&plain);
	clear_buf(&conf);
	clear_buf(&keys);
	clear_buf(&Ki);
	clear_buf(&Ke);
	clear_buf(&K0);
	if (ci)
		crypto_free_aead(ci);
	return ret;
}

/*
 * Perform a checksum test.
 */
static int krb5_test_one_mic(const struct krb5_mic_test *test, void *buf)
{
	const struct krb5_enctype *krb5 = crypto_krb5_find_enctype(test->etype);
	struct crypto_shash *ci = NULL;
	struct scatterlist sg[1];
	struct krb5_buffer K0 = {}, Kc = {}, keys = {}, plain = {}, mic = {};
	size_t offset, len, message_len;
	int ret;

	if (!krb5)
		return -EOPNOTSUPP;

	pr_notice("Running %s %s\n", krb5->name, test->name);

	/* Allocate a crypto object and set its key. */
	if (test->K0) {
		LOAD_BUF(&K0, test->K0);
		ci = crypto_krb5_prepare_checksum(krb5, &K0, test->usage, GFP_KERNEL);
	} else {
		LOAD_BUF(&Kc, test->Kc);

		ret = krb5->profile->load_checksum_key(krb5, &Kc, &keys, GFP_KERNEL);
		if (ret < 0)
			goto out;

		ci = krb5_prepare_checksum(krb5, &Kc, GFP_KERNEL);
	}
	if (IS_ERR(ci)) {
		ret = PTR_ERR(ci);
		ci = NULL;
		pr_err("Couldn't alloc shash %s: %d\n", krb5->cksum_name, ret);
		goto out;
	}

	/* Load the test data into binary buffers. */
	LOAD_BUF(&plain, test->plain);
	LOAD_BUF(&mic, test->mic);

	len = plain.len;
	message_len = crypto_krb5_how_much_buffer(krb5, KRB5_CHECKSUM_MODE,
						  len, &offset);

	if (CHECK(message_len != mic.len + plain.len)) {
		pr_warn("MIC length mismatch %zu != %u\n",
			message_len, mic.len + plain.len);
		goto out;
	}

	memcpy(buf + offset, plain.data, plain.len);

	/* Generate a MIC generation request. */
	sg_init_one(sg, buf, 1024);

	ret = crypto_krb5_get_mic(krb5, ci, NULL, sg, 1, 1024,
				  krb5->cksum_len, plain.len);
	if (ret < 0) {
		CHECK(1);
		pr_warn("Get MIC failed %d\n", ret);
		goto out;
	}
	len = ret;

	if (CHECK(len != plain.len + mic.len)) {
		pr_warn("MIC length mismatch %zu != %u\n", len, plain.len + mic.len);
		goto out;
	}

	if (memcmp(buf, mic.data, mic.len) != 0) {
		CHECK(1);
		pr_warn("MIC mismatch\n");
		pr_warn("BUF %*phN\n", mic.len, buf);
		pr_warn("MIC %*phN\n", mic.len, mic.data);
		ret = -EKEYREJECTED;
		goto out;
	}

	/* Generate a verification request. */
	offset = 0;
	ret = crypto_krb5_verify_mic(krb5, ci, NULL, sg, 1, &offset, &len);
	if (ret < 0) {
		CHECK(1);
		pr_warn("Verify MIC failed %d\n", ret);
		goto out;
	}

	if (CHECK(offset != mic.len) ||
	    CHECK(len != plain.len))
		goto out;

	if (memcmp(buf + offset, plain.data, plain.len) != 0) {
		CHECK(1);
		pr_warn("Plaintext mismatch\n");
		pr_warn("BUF %*phN\n", plain.len, buf + offset);
		pr_warn("PT  %*phN\n", plain.len, plain.data);
		ret = -EKEYREJECTED;
		goto out;
	}

	ret = 0;

out:
	clear_buf(&mic);
	clear_buf(&plain);
	clear_buf(&keys);
	clear_buf(&K0);
	clear_buf(&Kc);
	if (ci)
		crypto_free_shash(ci);
	return ret;
}

int krb5_selftest(void)
{
	void *buf;
	int ret = 0, i;

	buf = kmalloc(4096, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pr_notice("\n");
	pr_notice("Running selftests\n");

	for (i = 0; krb5_prf_tests[i].name; i++) {
		ret = krb5_test_one_prf(&krb5_prf_tests[i]);
		if (ret < 0) {
			if (ret != -EOPNOTSUPP)
				goto out;
			pr_notice("Skipping %s\n", krb5_prf_tests[i].name);
		}
	}

	for (i = 0; krb5_key_tests[i].name; i++) {
		ret = krb5_test_one_key(&krb5_key_tests[i]);
		if (ret < 0) {
			if (ret != -EOPNOTSUPP)
				goto out;
			pr_notice("Skipping %s\n", krb5_key_tests[i].name);
		}
	}

	for (i = 0; krb5_enc_tests[i].name; i++) {
		memset(buf, 0x5a, 4096);
		ret = krb5_test_one_enc(&krb5_enc_tests[i], buf);
		if (ret < 0) {
			if (ret != -EOPNOTSUPP)
				goto out;
			pr_notice("Skipping %s\n", krb5_enc_tests[i].name);
		}
	}

	for (i = 0; krb5_mic_tests[i].name; i++) {
		memset(buf, 0x5a, 4096);
		ret = krb5_test_one_mic(&krb5_mic_tests[i], buf);
		if (ret < 0) {
			if (ret != -EOPNOTSUPP)
				goto out;
			pr_notice("Skipping %s\n", krb5_mic_tests[i].name);
		}
	}

	ret = 0;
out:
	pr_notice("Selftests %s\n", ret == 0 ? "succeeded" : "failed");
	kfree(buf);
	return ret;
}
