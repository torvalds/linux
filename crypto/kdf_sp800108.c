// SPDX-License-Identifier: GPL-2.0

/*
 * SP800-108 Key-derivation function
 *
 * Copyright (C) 2021, Stephan Mueller <smueller@chronox.de>
 */

#include <linux/fips.h>
#include <linux/module.h>
#include <crypto/kdf_sp800108.h>
#include <crypto/internal/kdf_selftest.h>

/*
 * SP800-108 CTR KDF implementation
 */
int crypto_kdf108_ctr_generate(struct crypto_shash *kmd,
			       const struct kvec *info, unsigned int info_nvec,
			       u8 *dst, unsigned int dlen)
{
	SHASH_DESC_ON_STACK(desc, kmd);
	__be32 counter = cpu_to_be32(1);
	const unsigned int h = crypto_shash_digestsize(kmd), dlen_orig = dlen;
	unsigned int i;
	int err = 0;
	u8 *dst_orig = dst;

	desc->tfm = kmd;

	while (dlen) {
		err = crypto_shash_init(desc);
		if (err)
			goto out;

		err = crypto_shash_update(desc, (u8 *)&counter, sizeof(__be32));
		if (err)
			goto out;

		for (i = 0; i < info_nvec; i++) {
			err = crypto_shash_update(desc, info[i].iov_base,
						  info[i].iov_len);
			if (err)
				goto out;
		}

		if (dlen < h) {
			u8 tmpbuffer[HASH_MAX_DIGESTSIZE];

			err = crypto_shash_final(desc, tmpbuffer);
			if (err)
				goto out;
			memcpy(dst, tmpbuffer, dlen);
			memzero_explicit(tmpbuffer, h);
			goto out;
		}

		err = crypto_shash_final(desc, dst);
		if (err)
			goto out;

		dlen -= h;
		dst += h;
		counter = cpu_to_be32(be32_to_cpu(counter) + 1);
	}

out:
	if (err)
		memzero_explicit(dst_orig, dlen_orig);
	shash_desc_zero(desc);
	return err;
}
EXPORT_SYMBOL(crypto_kdf108_ctr_generate);

/*
 * The seeding of the KDF
 */
int crypto_kdf108_setkey(struct crypto_shash *kmd,
			 const u8 *key, size_t keylen,
			 const u8 *ikm, size_t ikmlen)
{
	unsigned int ds = crypto_shash_digestsize(kmd);

	/* SP800-108 does not support IKM */
	if (ikm || ikmlen)
		return -EINVAL;

	/* Check according to SP800-108 section 7.2 */
	if (ds > keylen)
		return -EINVAL;

	/* Set the key for the MAC used for the KDF. */
	return crypto_shash_setkey(kmd, key, keylen);
}
EXPORT_SYMBOL(crypto_kdf108_setkey);

/*
 * Test vector obtained from
 * http://csrc.nist.gov/groups/STM/cavp/documents/KBKDF800-108/CounterMode.zip
 */
static const struct kdf_testvec kdf_ctr_hmac_sha256_tv_template[] = {
	{
		.key = "\xdd\x1d\x91\xb7\xd9\x0b\x2b\xd3"
		       "\x13\x85\x33\xce\x92\xb2\x72\xfb"
		       "\xf8\xa3\x69\x31\x6a\xef\xe2\x42"
		       "\xe6\x59\xcc\x0a\xe2\x38\xaf\xe0",
		.keylen = 32,
		.ikm = NULL,
		.ikmlen = 0,
		.info = {
			.iov_base = "\x01\x32\x2b\x96\xb3\x0a\xcd\x19"
				    "\x79\x79\x44\x4e\x46\x8e\x1c\x5c"
				    "\x68\x59\xbf\x1b\x1c\xf9\x51\xb7"
				    "\xe7\x25\x30\x3e\x23\x7e\x46\xb8"
				    "\x64\xa1\x45\xfa\xb2\x5e\x51\x7b"
				    "\x08\xf8\x68\x3d\x03\x15\xbb\x29"
				    "\x11\xd8\x0a\x0e\x8a\xba\x17\xf3"
				    "\xb4\x13\xfa\xac",
			.iov_len  = 60
		},
		.expected	  = "\x10\x62\x13\x42\xbf\xb0\xfd\x40"
				    "\x04\x6c\x0e\x29\xf2\xcf\xdb\xf0",
		.expectedlen	  = 16
	}
};

static int __init crypto_kdf108_init(void)
{
	int ret = kdf_test(&kdf_ctr_hmac_sha256_tv_template[0], "hmac(sha256)",
			   crypto_kdf108_setkey, crypto_kdf108_ctr_generate);

	if (ret) {
		if (fips_enabled)
			panic("alg: self-tests for CTR-KDF (hmac(sha256)) failed (rc=%d)\n",
			      ret);

		WARN(1,
		     "alg: self-tests for CTR-KDF (hmac(sha256)) failed (rc=%d)\n",
		     ret);
	} else {
		pr_info("alg: self-tests for CTR-KDF (hmac(sha256)) passed\n");
	}

	return ret;
}

static void __exit crypto_kdf108_exit(void) { }

module_init(crypto_kdf108_init);
module_exit(crypto_kdf108_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Stephan Mueller <smueller@chronox.de>");
MODULE_DESCRIPTION("Key Derivation Function conformant to SP800-108");
