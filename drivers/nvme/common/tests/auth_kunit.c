// SPDX-License-Identifier: GPL-2.0-only
/*
 * Unit tests for NVMe authentication functions
 *
 * Copyright 2026 Google LLC
 */

#include <crypto/sha2.h>
#include <kunit/test.h>
#include <linux/nvme.h>
#include <linux/nvme-auth.h>
#include <linux/slab.h>

struct nvme_auth_test_values {
	u8 hmac_id;
	size_t hash_len;
	u8 expected_psk[NVME_AUTH_MAX_DIGEST_SIZE];
	char *expected_psk_digest;
	u8 expected_tls_psk[NVME_AUTH_MAX_DIGEST_SIZE];
};

static void kfree_action(void *ptr)
{
	kfree(ptr);
}

static void kunit_add_kfree_action(struct kunit *test, void *ptr)
{
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, kfree_action, ptr));
}

/*
 * Test the derivation of a TLS PSK from the initial skey.  The vals parameter
 * gives the expected value of tls_psk as well as the intermediate values psk
 * and psk_digest.  The inputs are implicitly the fixed values set below.
 */
static void
test_nvme_auth_derive_tls_psk(struct kunit *test,
			      const struct nvme_auth_test_values *vals)
{
	const u8 hmac_id = vals->hmac_id;
	const size_t hash_len = vals->hash_len;
	const size_t skey_len = hash_len;
	u8 skey[NVME_AUTH_MAX_DIGEST_SIZE];
	u8 c1[NVME_AUTH_MAX_DIGEST_SIZE];
	u8 c2[NVME_AUTH_MAX_DIGEST_SIZE];
	const char *subsysnqn = "subsysnqn";
	const char *hostnqn = "hostnqn";
	u8 *psk = NULL, *tls_psk = NULL;
	char *psk_digest = NULL;
	size_t psk_len;
	int ret;

	for (int i = 0; i < NVME_AUTH_MAX_DIGEST_SIZE; i++) {
		skey[i] = 'A' + i;
		c1[i] = i;
		c2[i] = 0xff - i;
	}

	ret = nvme_auth_generate_psk(hmac_id, skey, skey_len, c1, c2, hash_len,
				     &psk, &psk_len);
	kunit_add_kfree_action(test, psk);
	KUNIT_ASSERT_EQ(test, 0, ret);
	KUNIT_ASSERT_EQ(test, hash_len, psk_len);
	KUNIT_ASSERT_MEMEQ(test, vals->expected_psk, psk, psk_len);

	ret = nvme_auth_generate_digest(hmac_id, psk, psk_len, subsysnqn,
					hostnqn, &psk_digest);
	kunit_add_kfree_action(test, psk_digest);
	if (vals->expected_psk_digest == NULL) {
		/*
		 * Algorithm has an ID assigned but is not supported by
		 * nvme_auth_generate_digest().
		 */
		KUNIT_ASSERT_EQ(test, -EINVAL, ret);
		return;
	}
	KUNIT_ASSERT_EQ(test, 0, ret);
	KUNIT_ASSERT_STREQ(test, vals->expected_psk_digest, psk_digest);

	ret = nvme_auth_derive_tls_psk(hmac_id, psk, psk_len, psk_digest,
				       &tls_psk);
	kunit_add_kfree_action(test, tls_psk);
	KUNIT_ASSERT_EQ(test, 0, ret);
	KUNIT_ASSERT_MEMEQ(test, vals->expected_tls_psk, tls_psk, psk_len);
}

static void test_nvme_auth_derive_tls_psk_hmac_sha256(struct kunit *test)
{
	static const struct nvme_auth_test_values vals = {
		.hmac_id = NVME_AUTH_HASH_SHA256,
		.hash_len = SHA256_DIGEST_SIZE,
		.expected_psk = {
			0x17, 0x33, 0xc5, 0x9f, 0xa7, 0xf4, 0x8f, 0xcf,
			0x37, 0xf5, 0xf2, 0x6f, 0xc4, 0xff, 0x02, 0x68,
			0xad, 0x4f, 0x78, 0xe0, 0x30, 0xf4, 0xf3, 0xb0,
			0xbf, 0xd1, 0xd4, 0x7e, 0x7b, 0xb1, 0x44, 0x7a,
		},
		.expected_psk_digest = "OldoKuTfKddMuyCznAZojkWD7P4D9/AtzDzLimtOxqI=",
		.expected_tls_psk = {
			0x3c, 0x17, 0xda, 0x62, 0x84, 0x74, 0xa0, 0x4d,
			0x22, 0x47, 0xc4, 0xca, 0xb4, 0x79, 0x68, 0xc9,
			0x15, 0x38, 0x81, 0x93, 0xf7, 0xc0, 0x71, 0xbd,
			0x94, 0x89, 0xcc, 0x36, 0x66, 0xcd, 0x7c, 0xc8,
		},
	};

	test_nvme_auth_derive_tls_psk(test, &vals);
}

static void test_nvme_auth_derive_tls_psk_hmac_sha384(struct kunit *test)
{
	static const struct nvme_auth_test_values vals = {
		.hmac_id = NVME_AUTH_HASH_SHA384,
		.hash_len = SHA384_DIGEST_SIZE,
		.expected_psk = {
			0xf1, 0x4b, 0x2d, 0xd3, 0x23, 0x4c, 0x45, 0x96,
			0x94, 0xd3, 0xbc, 0x63, 0xf8, 0x96, 0x8b, 0xd6,
			0xb3, 0x7c, 0x2c, 0x6d, 0xe8, 0x49, 0xe2, 0x2e,
			0x11, 0x87, 0x49, 0x00, 0x1c, 0xe4, 0xbb, 0xe8,
			0x64, 0x0b, 0x9e, 0x3a, 0x74, 0x8c, 0xb1, 0x1c,
			0xe4, 0xb1, 0xd7, 0x1d, 0x35, 0x9c, 0xce, 0x39,
		},
		.expected_psk_digest = "cffMWk8TSS7HOQebjgYEIkrPrjWPV4JE5cdPB8WhEvY4JBW5YynKyv66XscN4A9n",
		.expected_tls_psk = {
			0x27, 0x74, 0x75, 0x32, 0x33, 0x53, 0x7b, 0x3f,
			0xa5, 0x0e, 0xb7, 0xd1, 0x6a, 0x8e, 0x43, 0x45,
			0x7d, 0x85, 0xf4, 0x90, 0x6c, 0x00, 0x5b, 0x22,
			0x36, 0x61, 0x6c, 0x5d, 0x80, 0x93, 0x9d, 0x08,
			0x98, 0xff, 0xf1, 0x5b, 0xb8, 0xb7, 0x71, 0x19,
			0xd2, 0xbe, 0x0a, 0xac, 0x42, 0x3e, 0x75, 0x90,
		},
	};

	test_nvme_auth_derive_tls_psk(test, &vals);
}

static void test_nvme_auth_derive_tls_psk_hmac_sha512(struct kunit *test)
{
	static const struct nvme_auth_test_values vals = {
		.hmac_id = NVME_AUTH_HASH_SHA512,
		.hash_len = SHA512_DIGEST_SIZE,
		.expected_psk = {
			0x9c, 0x9f, 0x08, 0x9a, 0x61, 0x8b, 0x47, 0xd2,
			0xd7, 0x5f, 0x4b, 0x6c, 0x28, 0x07, 0x04, 0x24,
			0x48, 0x7b, 0x44, 0x5d, 0xd9, 0x6e, 0x70, 0xc4,
			0xc0, 0x9b, 0x55, 0xe8, 0xb6, 0x00, 0x01, 0x52,
			0xa3, 0x36, 0x3c, 0x34, 0x54, 0x04, 0x3f, 0x38,
			0xf0, 0xb8, 0x50, 0x36, 0xde, 0xd4, 0x06, 0x55,
			0x35, 0x0a, 0xa8, 0x7b, 0x8b, 0x6a, 0x28, 0x2b,
			0x5c, 0x1a, 0xca, 0xe1, 0x62, 0x33, 0xdd, 0x5b,
		},
		/* nvme_auth_generate_digest() doesn't support SHA-512 yet. */
		.expected_psk_digest = NULL,
	};

	test_nvme_auth_derive_tls_psk(test, &vals);
}

static struct kunit_case nvme_auth_test_cases[] = {
	KUNIT_CASE(test_nvme_auth_derive_tls_psk_hmac_sha256),
	KUNIT_CASE(test_nvme_auth_derive_tls_psk_hmac_sha384),
	KUNIT_CASE(test_nvme_auth_derive_tls_psk_hmac_sha512),
	{},
};

static struct kunit_suite nvme_auth_test_suite = {
	.name = "nvme-auth",
	.test_cases = nvme_auth_test_cases,
};
kunit_test_suite(nvme_auth_test_suite);

MODULE_DESCRIPTION("Unit tests for NVMe authentication functions");
MODULE_LICENSE("GPL");
