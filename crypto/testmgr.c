/*
 * Algorithm testing framework and tests.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2002 Jean-Francois Dive <jef@linuxbe.org>
 * Copyright (c) 2007 Nokia Siemens Networks
 * Copyright (c) 2008 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * Updated RFC4106 AES-GCM testing.
 *    Authors: Aidan O'Mahony (aidan.o.mahony@intel.com)
 *             Adrian Hoban <adrian.hoban@intel.com>
 *             Gabriele Paoloni <gabriele.paoloni@intel.com>
 *             Tadeusz Struk (tadeusz.struk@intel.com)
 *    Copyright (c) 2010, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/aead.h>
#include <crypto/hash.h>
#include <crypto/skcipher.h>
#include <linux/err.h>
#include <linux/fips.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <crypto/rng.h>
#include <crypto/drbg.h>
#include <crypto/akcipher.h>
#include <crypto/kpp.h>
#include <crypto/acompress.h>

#include "internal.h"

static bool notests;
module_param(notests, bool, 0644);
MODULE_PARM_DESC(notests, "disable crypto self-tests");

#ifdef CONFIG_CRYPTO_MANAGER_DISABLE_TESTS

/* a perfect nop */
int alg_test(const char *driver, const char *alg, u32 type, u32 mask)
{
	return 0;
}

#else

#include "testmgr.h"

/*
 * Need slab memory for testing (size in number of pages).
 */
#define XBUFSIZE	8

/*
 * Indexes into the xbuf to simulate cross-page access.
 */
#define IDX1		32
#define IDX2		32400
#define IDX3		1511
#define IDX4		8193
#define IDX5		22222
#define IDX6		17101
#define IDX7		27333
#define IDX8		3000

/*
* Used by test_cipher()
*/
#define ENCRYPT 1
#define DECRYPT 0

struct aead_test_suite {
	struct {
		const struct aead_testvec *vecs;
		unsigned int count;
	} enc, dec;
};

struct cipher_test_suite {
	const struct cipher_testvec *vecs;
	unsigned int count;
};

struct comp_test_suite {
	struct {
		const struct comp_testvec *vecs;
		unsigned int count;
	} comp, decomp;
};

struct hash_test_suite {
	const struct hash_testvec *vecs;
	unsigned int count;
};

struct cprng_test_suite {
	const struct cprng_testvec *vecs;
	unsigned int count;
};

struct drbg_test_suite {
	const struct drbg_testvec *vecs;
	unsigned int count;
};

struct akcipher_test_suite {
	const struct akcipher_testvec *vecs;
	unsigned int count;
};

struct kpp_test_suite {
	const struct kpp_testvec *vecs;
	unsigned int count;
};

struct alg_test_desc {
	const char *alg;
	int (*test)(const struct alg_test_desc *desc, const char *driver,
		    u32 type, u32 mask);
	int fips_allowed;	/* set if alg is allowed in fips mode */

	union {
		struct aead_test_suite aead;
		struct cipher_test_suite cipher;
		struct comp_test_suite comp;
		struct hash_test_suite hash;
		struct cprng_test_suite cprng;
		struct drbg_test_suite drbg;
		struct akcipher_test_suite akcipher;
		struct kpp_test_suite kpp;
	} suite;
};

static const unsigned int IDX[8] = {
	IDX1, IDX2, IDX3, IDX4, IDX5, IDX6, IDX7, IDX8 };

static void hexdump(unsigned char *buf, unsigned int len)
{
	print_hex_dump(KERN_CONT, "", DUMP_PREFIX_OFFSET,
			16, 1,
			buf, len, false);
}

static int testmgr_alloc_buf(char *buf[XBUFSIZE])
{
	int i;

	for (i = 0; i < XBUFSIZE; i++) {
		buf[i] = (void *)__get_free_page(GFP_KERNEL);
		if (!buf[i])
			goto err_free_buf;
	}

	return 0;

err_free_buf:
	while (i-- > 0)
		free_page((unsigned long)buf[i]);

	return -ENOMEM;
}

static void testmgr_free_buf(char *buf[XBUFSIZE])
{
	int i;

	for (i = 0; i < XBUFSIZE; i++)
		free_page((unsigned long)buf[i]);
}

static int ahash_guard_result(char *result, char c, int size)
{
	int i;

	for (i = 0; i < size; i++) {
		if (result[i] != c)
			return -EINVAL;
	}

	return 0;
}

static int ahash_partial_update(struct ahash_request **preq,
	struct crypto_ahash *tfm, const struct hash_testvec *template,
	void *hash_buff, int k, int temp, struct scatterlist *sg,
	const char *algo, char *result, struct crypto_wait *wait)
{
	char *state;
	struct ahash_request *req;
	int statesize, ret = -EINVAL;
	static const unsigned char guard[] = { 0x00, 0xba, 0xad, 0x00 };
	int digestsize = crypto_ahash_digestsize(tfm);

	req = *preq;
	statesize = crypto_ahash_statesize(
			crypto_ahash_reqtfm(req));
	state = kmalloc(statesize + sizeof(guard), GFP_KERNEL);
	if (!state) {
		pr_err("alg: hash: Failed to alloc state for %s\n", algo);
		goto out_nostate;
	}
	memcpy(state + statesize, guard, sizeof(guard));
	memset(result, 1, digestsize);
	ret = crypto_ahash_export(req, state);
	WARN_ON(memcmp(state + statesize, guard, sizeof(guard)));
	if (ret) {
		pr_err("alg: hash: Failed to export() for %s\n", algo);
		goto out;
	}
	ret = ahash_guard_result(result, 1, digestsize);
	if (ret) {
		pr_err("alg: hash: Failed, export used req->result for %s\n",
		       algo);
		goto out;
	}
	ahash_request_free(req);
	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("alg: hash: Failed to alloc request for %s\n", algo);
		goto out_noreq;
	}
	ahash_request_set_callback(req,
		CRYPTO_TFM_REQ_MAY_BACKLOG,
		crypto_req_done, wait);

	memcpy(hash_buff, template->plaintext + temp,
		template->tap[k]);
	sg_init_one(&sg[0], hash_buff, template->tap[k]);
	ahash_request_set_crypt(req, sg, result, template->tap[k]);
	ret = crypto_ahash_import(req, state);
	if (ret) {
		pr_err("alg: hash: Failed to import() for %s\n", algo);
		goto out;
	}
	ret = ahash_guard_result(result, 1, digestsize);
	if (ret) {
		pr_err("alg: hash: Failed, import used req->result for %s\n",
		       algo);
		goto out;
	}
	ret = crypto_wait_req(crypto_ahash_update(req), wait);
	if (ret)
		goto out;
	*preq = req;
	ret = 0;
	goto out_noreq;
out:
	ahash_request_free(req);
out_noreq:
	kfree(state);
out_nostate:
	return ret;
}

static int __test_hash(struct crypto_ahash *tfm,
		       const struct hash_testvec *template, unsigned int tcount,
		       bool use_digest, const int align_offset)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_ahash_tfm(tfm));
	size_t digest_size = crypto_ahash_digestsize(tfm);
	unsigned int i, j, k, temp;
	struct scatterlist sg[8];
	char *result;
	char *key;
	struct ahash_request *req;
	struct crypto_wait wait;
	void *hash_buff;
	char *xbuf[XBUFSIZE];
	int ret = -ENOMEM;

	result = kmalloc(digest_size, GFP_KERNEL);
	if (!result)
		return ret;
	key = kmalloc(MAX_KEYLEN, GFP_KERNEL);
	if (!key)
		goto out_nobuf;
	if (testmgr_alloc_buf(xbuf))
		goto out_nobuf;

	crypto_init_wait(&wait);

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		printk(KERN_ERR "alg: hash: Failed to allocate request for "
		       "%s\n", algo);
		goto out_noreq;
	}
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   crypto_req_done, &wait);

	j = 0;
	for (i = 0; i < tcount; i++) {
		if (template[i].np)
			continue;

		ret = -EINVAL;
		if (WARN_ON(align_offset + template[i].psize > PAGE_SIZE))
			goto out;

		j++;
		memset(result, 0, digest_size);

		hash_buff = xbuf[0];
		hash_buff += align_offset;

		memcpy(hash_buff, template[i].plaintext, template[i].psize);
		sg_init_one(&sg[0], hash_buff, template[i].psize);

		if (template[i].ksize) {
			crypto_ahash_clear_flags(tfm, ~0);
			if (template[i].ksize > MAX_KEYLEN) {
				pr_err("alg: hash: setkey failed on test %d for %s: key size %d > %d\n",
				       j, algo, template[i].ksize, MAX_KEYLEN);
				ret = -EINVAL;
				goto out;
			}
			memcpy(key, template[i].key, template[i].ksize);
			ret = crypto_ahash_setkey(tfm, key, template[i].ksize);
			if (ret) {
				printk(KERN_ERR "alg: hash: setkey failed on "
				       "test %d for %s: ret=%d\n", j, algo,
				       -ret);
				goto out;
			}
		}

		ahash_request_set_crypt(req, sg, result, template[i].psize);
		if (use_digest) {
			ret = crypto_wait_req(crypto_ahash_digest(req), &wait);
			if (ret) {
				pr_err("alg: hash: digest failed on test %d "
				       "for %s: ret=%d\n", j, algo, -ret);
				goto out;
			}
		} else {
			memset(result, 1, digest_size);
			ret = crypto_wait_req(crypto_ahash_init(req), &wait);
			if (ret) {
				pr_err("alg: hash: init failed on test %d "
				       "for %s: ret=%d\n", j, algo, -ret);
				goto out;
			}
			ret = ahash_guard_result(result, 1, digest_size);
			if (ret) {
				pr_err("alg: hash: init failed on test %d "
				       "for %s: used req->result\n", j, algo);
				goto out;
			}
			ret = crypto_wait_req(crypto_ahash_update(req), &wait);
			if (ret) {
				pr_err("alg: hash: update failed on test %d "
				       "for %s: ret=%d\n", j, algo, -ret);
				goto out;
			}
			ret = ahash_guard_result(result, 1, digest_size);
			if (ret) {
				pr_err("alg: hash: update failed on test %d "
				       "for %s: used req->result\n", j, algo);
				goto out;
			}
			ret = crypto_wait_req(crypto_ahash_final(req), &wait);
			if (ret) {
				pr_err("alg: hash: final failed on test %d "
				       "for %s: ret=%d\n", j, algo, -ret);
				goto out;
			}
		}

		if (memcmp(result, template[i].digest,
			   crypto_ahash_digestsize(tfm))) {
			printk(KERN_ERR "alg: hash: Test %d failed for %s\n",
			       j, algo);
			hexdump(result, crypto_ahash_digestsize(tfm));
			ret = -EINVAL;
			goto out;
		}
	}

	j = 0;
	for (i = 0; i < tcount; i++) {
		/* alignment tests are only done with continuous buffers */
		if (align_offset != 0)
			break;

		if (!template[i].np)
			continue;

		j++;
		memset(result, 0, digest_size);

		temp = 0;
		sg_init_table(sg, template[i].np);
		ret = -EINVAL;
		for (k = 0; k < template[i].np; k++) {
			if (WARN_ON(offset_in_page(IDX[k]) +
				    template[i].tap[k] > PAGE_SIZE))
				goto out;
			sg_set_buf(&sg[k],
				   memcpy(xbuf[IDX[k] >> PAGE_SHIFT] +
					  offset_in_page(IDX[k]),
					  template[i].plaintext + temp,
					  template[i].tap[k]),
				   template[i].tap[k]);
			temp += template[i].tap[k];
		}

		if (template[i].ksize) {
			if (template[i].ksize > MAX_KEYLEN) {
				pr_err("alg: hash: setkey failed on test %d for %s: key size %d > %d\n",
				       j, algo, template[i].ksize, MAX_KEYLEN);
				ret = -EINVAL;
				goto out;
			}
			crypto_ahash_clear_flags(tfm, ~0);
			memcpy(key, template[i].key, template[i].ksize);
			ret = crypto_ahash_setkey(tfm, key, template[i].ksize);

			if (ret) {
				printk(KERN_ERR "alg: hash: setkey "
				       "failed on chunking test %d "
				       "for %s: ret=%d\n", j, algo, -ret);
				goto out;
			}
		}

		ahash_request_set_crypt(req, sg, result, template[i].psize);
		ret = crypto_wait_req(crypto_ahash_digest(req), &wait);
		if (ret) {
			pr_err("alg: hash: digest failed on chunking test %d for %s: ret=%d\n",
			       j, algo, -ret);
			goto out;
		}

		if (memcmp(result, template[i].digest,
			   crypto_ahash_digestsize(tfm))) {
			printk(KERN_ERR "alg: hash: Chunking test %d "
			       "failed for %s\n", j, algo);
			hexdump(result, crypto_ahash_digestsize(tfm));
			ret = -EINVAL;
			goto out;
		}
	}

	/* partial update exercise */
	j = 0;
	for (i = 0; i < tcount; i++) {
		/* alignment tests are only done with continuous buffers */
		if (align_offset != 0)
			break;

		if (template[i].np < 2)
			continue;

		j++;
		memset(result, 0, digest_size);

		ret = -EINVAL;
		hash_buff = xbuf[0];
		memcpy(hash_buff, template[i].plaintext,
			template[i].tap[0]);
		sg_init_one(&sg[0], hash_buff, template[i].tap[0]);

		if (template[i].ksize) {
			crypto_ahash_clear_flags(tfm, ~0);
			if (template[i].ksize > MAX_KEYLEN) {
				pr_err("alg: hash: setkey failed on test %d for %s: key size %d > %d\n",
					j, algo, template[i].ksize, MAX_KEYLEN);
				ret = -EINVAL;
				goto out;
			}
			memcpy(key, template[i].key, template[i].ksize);
			ret = crypto_ahash_setkey(tfm, key, template[i].ksize);
			if (ret) {
				pr_err("alg: hash: setkey failed on test %d for %s: ret=%d\n",
					j, algo, -ret);
				goto out;
			}
		}

		ahash_request_set_crypt(req, sg, result, template[i].tap[0]);
		ret = crypto_wait_req(crypto_ahash_init(req), &wait);
		if (ret) {
			pr_err("alg: hash: init failed on test %d for %s: ret=%d\n",
				j, algo, -ret);
			goto out;
		}
		ret = crypto_wait_req(crypto_ahash_update(req), &wait);
		if (ret) {
			pr_err("alg: hash: update failed on test %d for %s: ret=%d\n",
				j, algo, -ret);
			goto out;
		}

		temp = template[i].tap[0];
		for (k = 1; k < template[i].np; k++) {
			ret = ahash_partial_update(&req, tfm, &template[i],
				hash_buff, k, temp, &sg[0], algo, result,
				&wait);
			if (ret) {
				pr_err("alg: hash: partial update failed on test %d for %s: ret=%d\n",
					j, algo, -ret);
				goto out_noreq;
			}
			temp += template[i].tap[k];
		}
		ret = crypto_wait_req(crypto_ahash_final(req), &wait);
		if (ret) {
			pr_err("alg: hash: final failed on test %d for %s: ret=%d\n",
				j, algo, -ret);
			goto out;
		}
		if (memcmp(result, template[i].digest,
			   crypto_ahash_digestsize(tfm))) {
			pr_err("alg: hash: Partial Test %d failed for %s\n",
			       j, algo);
			hexdump(result, crypto_ahash_digestsize(tfm));
			ret = -EINVAL;
			goto out;
		}
	}

	ret = 0;

out:
	ahash_request_free(req);
out_noreq:
	testmgr_free_buf(xbuf);
out_nobuf:
	kfree(key);
	kfree(result);
	return ret;
}

static int test_hash(struct crypto_ahash *tfm,
		     const struct hash_testvec *template,
		     unsigned int tcount, bool use_digest)
{
	unsigned int alignmask;
	int ret;

	ret = __test_hash(tfm, template, tcount, use_digest, 0);
	if (ret)
		return ret;

	/* test unaligned buffers, check with one byte offset */
	ret = __test_hash(tfm, template, tcount, use_digest, 1);
	if (ret)
		return ret;

	alignmask = crypto_tfm_alg_alignmask(&tfm->base);
	if (alignmask) {
		/* Check if alignment mask for tfm is correctly set. */
		ret = __test_hash(tfm, template, tcount, use_digest,
				  alignmask + 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int __test_aead(struct crypto_aead *tfm, int enc,
		       const struct aead_testvec *template, unsigned int tcount,
		       const bool diff_dst, const int align_offset)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm));
	unsigned int i, j, k, n, temp;
	int ret = -ENOMEM;
	char *q;
	char *key;
	struct aead_request *req;
	struct scatterlist *sg;
	struct scatterlist *sgout;
	const char *e, *d;
	struct crypto_wait wait;
	unsigned int authsize, iv_len;
	void *input;
	void *output;
	void *assoc;
	char *iv;
	char *xbuf[XBUFSIZE];
	char *xoutbuf[XBUFSIZE];
	char *axbuf[XBUFSIZE];

	iv = kzalloc(MAX_IVLEN, GFP_KERNEL);
	if (!iv)
		return ret;
	key = kmalloc(MAX_KEYLEN, GFP_KERNEL);
	if (!key)
		goto out_noxbuf;
	if (testmgr_alloc_buf(xbuf))
		goto out_noxbuf;
	if (testmgr_alloc_buf(axbuf))
		goto out_noaxbuf;
	if (diff_dst && testmgr_alloc_buf(xoutbuf))
		goto out_nooutbuf;

	/* avoid "the frame size is larger than 1024 bytes" compiler warning */
	sg = kmalloc(array3_size(sizeof(*sg), 8, (diff_dst ? 4 : 2)),
		     GFP_KERNEL);
	if (!sg)
		goto out_nosg;
	sgout = &sg[16];

	if (diff_dst)
		d = "-ddst";
	else
		d = "";

	if (enc == ENCRYPT)
		e = "encryption";
	else
		e = "decryption";

	crypto_init_wait(&wait);

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("alg: aead%s: Failed to allocate request for %s\n",
		       d, algo);
		goto out;
	}

	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  crypto_req_done, &wait);

	iv_len = crypto_aead_ivsize(tfm);

	for (i = 0, j = 0; i < tcount; i++) {
		if (template[i].np)
			continue;

		j++;

		/* some templates have no input data but they will
		 * touch input
		 */
		input = xbuf[0];
		input += align_offset;
		assoc = axbuf[0];

		ret = -EINVAL;
		if (WARN_ON(align_offset + template[i].ilen >
			    PAGE_SIZE || template[i].alen > PAGE_SIZE))
			goto out;

		memcpy(input, template[i].input, template[i].ilen);
		memcpy(assoc, template[i].assoc, template[i].alen);
		if (template[i].iv)
			memcpy(iv, template[i].iv, iv_len);
		else
			memset(iv, 0, iv_len);

		crypto_aead_clear_flags(tfm, ~0);
		if (template[i].wk)
			crypto_aead_set_flags(tfm, CRYPTO_TFM_REQ_WEAK_KEY);

		if (template[i].klen > MAX_KEYLEN) {
			pr_err("alg: aead%s: setkey failed on test %d for %s: key size %d > %d\n",
			       d, j, algo, template[i].klen,
			       MAX_KEYLEN);
			ret = -EINVAL;
			goto out;
		}
		memcpy(key, template[i].key, template[i].klen);

		ret = crypto_aead_setkey(tfm, key, template[i].klen);
		if (template[i].fail == !ret) {
			pr_err("alg: aead%s: setkey failed on test %d for %s: flags=%x\n",
			       d, j, algo, crypto_aead_get_flags(tfm));
			goto out;
		} else if (ret)
			continue;

		authsize = abs(template[i].rlen - template[i].ilen);
		ret = crypto_aead_setauthsize(tfm, authsize);
		if (ret) {
			pr_err("alg: aead%s: Failed to set authsize to %u on test %d for %s\n",
			       d, authsize, j, algo);
			goto out;
		}

		k = !!template[i].alen;
		sg_init_table(sg, k + 1);
		sg_set_buf(&sg[0], assoc, template[i].alen);
		sg_set_buf(&sg[k], input,
			   template[i].ilen + (enc ? authsize : 0));
		output = input;

		if (diff_dst) {
			sg_init_table(sgout, k + 1);
			sg_set_buf(&sgout[0], assoc, template[i].alen);

			output = xoutbuf[0];
			output += align_offset;
			sg_set_buf(&sgout[k], output,
				   template[i].rlen + (enc ? 0 : authsize));
		}

		aead_request_set_crypt(req, sg, (diff_dst) ? sgout : sg,
				       template[i].ilen, iv);

		aead_request_set_ad(req, template[i].alen);

		ret = crypto_wait_req(enc ? crypto_aead_encrypt(req)
				      : crypto_aead_decrypt(req), &wait);

		switch (ret) {
		case 0:
			if (template[i].novrfy) {
				/* verification was supposed to fail */
				pr_err("alg: aead%s: %s failed on test %d for %s: ret was 0, expected -EBADMSG\n",
				       d, e, j, algo);
				/* so really, we got a bad message */
				ret = -EBADMSG;
				goto out;
			}
			break;
		case -EBADMSG:
			if (template[i].novrfy)
				/* verification failure was expected */
				continue;
			/* fall through */
		default:
			pr_err("alg: aead%s: %s failed on test %d for %s: ret=%d\n",
			       d, e, j, algo, -ret);
			goto out;
		}

		q = output;
		if (memcmp(q, template[i].result, template[i].rlen)) {
			pr_err("alg: aead%s: Test %d failed on %s for %s\n",
			       d, j, e, algo);
			hexdump(q, template[i].rlen);
			ret = -EINVAL;
			goto out;
		}
	}

	for (i = 0, j = 0; i < tcount; i++) {
		/* alignment tests are only done with continuous buffers */
		if (align_offset != 0)
			break;

		if (!template[i].np)
			continue;

		j++;

		if (template[i].iv)
			memcpy(iv, template[i].iv, iv_len);
		else
			memset(iv, 0, MAX_IVLEN);

		crypto_aead_clear_flags(tfm, ~0);
		if (template[i].wk)
			crypto_aead_set_flags(tfm, CRYPTO_TFM_REQ_WEAK_KEY);
		if (template[i].klen > MAX_KEYLEN) {
			pr_err("alg: aead%s: setkey failed on test %d for %s: key size %d > %d\n",
			       d, j, algo, template[i].klen, MAX_KEYLEN);
			ret = -EINVAL;
			goto out;
		}
		memcpy(key, template[i].key, template[i].klen);

		ret = crypto_aead_setkey(tfm, key, template[i].klen);
		if (template[i].fail == !ret) {
			pr_err("alg: aead%s: setkey failed on chunk test %d for %s: flags=%x\n",
			       d, j, algo, crypto_aead_get_flags(tfm));
			goto out;
		} else if (ret)
			continue;

		authsize = abs(template[i].rlen - template[i].ilen);

		ret = -EINVAL;
		sg_init_table(sg, template[i].anp + template[i].np);
		if (diff_dst)
			sg_init_table(sgout, template[i].anp + template[i].np);

		ret = -EINVAL;
		for (k = 0, temp = 0; k < template[i].anp; k++) {
			if (WARN_ON(offset_in_page(IDX[k]) +
				    template[i].atap[k] > PAGE_SIZE))
				goto out;
			sg_set_buf(&sg[k],
				   memcpy(axbuf[IDX[k] >> PAGE_SHIFT] +
					  offset_in_page(IDX[k]),
					  template[i].assoc + temp,
					  template[i].atap[k]),
				   template[i].atap[k]);
			if (diff_dst)
				sg_set_buf(&sgout[k],
					   axbuf[IDX[k] >> PAGE_SHIFT] +
					   offset_in_page(IDX[k]),
					   template[i].atap[k]);
			temp += template[i].atap[k];
		}

		for (k = 0, temp = 0; k < template[i].np; k++) {
			if (WARN_ON(offset_in_page(IDX[k]) +
				    template[i].tap[k] > PAGE_SIZE))
				goto out;

			q = xbuf[IDX[k] >> PAGE_SHIFT] + offset_in_page(IDX[k]);
			memcpy(q, template[i].input + temp, template[i].tap[k]);
			sg_set_buf(&sg[template[i].anp + k],
				   q, template[i].tap[k]);

			if (diff_dst) {
				q = xoutbuf[IDX[k] >> PAGE_SHIFT] +
				    offset_in_page(IDX[k]);

				memset(q, 0, template[i].tap[k]);

				sg_set_buf(&sgout[template[i].anp + k],
					   q, template[i].tap[k]);
			}

			n = template[i].tap[k];
			if (k == template[i].np - 1 && enc)
				n += authsize;
			if (offset_in_page(q) + n < PAGE_SIZE)
				q[n] = 0;

			temp += template[i].tap[k];
		}

		ret = crypto_aead_setauthsize(tfm, authsize);
		if (ret) {
			pr_err("alg: aead%s: Failed to set authsize to %u on chunk test %d for %s\n",
			       d, authsize, j, algo);
			goto out;
		}

		if (enc) {
			if (WARN_ON(sg[template[i].anp + k - 1].offset +
				    sg[template[i].anp + k - 1].length +
				    authsize > PAGE_SIZE)) {
				ret = -EINVAL;
				goto out;
			}

			if (diff_dst)
				sgout[template[i].anp + k - 1].length +=
					authsize;
			sg[template[i].anp + k - 1].length += authsize;
		}

		aead_request_set_crypt(req, sg, (diff_dst) ? sgout : sg,
				       template[i].ilen,
				       iv);

		aead_request_set_ad(req, template[i].alen);

		ret = crypto_wait_req(enc ? crypto_aead_encrypt(req)
				      : crypto_aead_decrypt(req), &wait);

		switch (ret) {
		case 0:
			if (template[i].novrfy) {
				/* verification was supposed to fail */
				pr_err("alg: aead%s: %s failed on chunk test %d for %s: ret was 0, expected -EBADMSG\n",
				       d, e, j, algo);
				/* so really, we got a bad message */
				ret = -EBADMSG;
				goto out;
			}
			break;
		case -EBADMSG:
			if (template[i].novrfy)
				/* verification failure was expected */
				continue;
			/* fall through */
		default:
			pr_err("alg: aead%s: %s failed on chunk test %d for %s: ret=%d\n",
			       d, e, j, algo, -ret);
			goto out;
		}

		ret = -EINVAL;
		for (k = 0, temp = 0; k < template[i].np; k++) {
			if (diff_dst)
				q = xoutbuf[IDX[k] >> PAGE_SHIFT] +
				    offset_in_page(IDX[k]);
			else
				q = xbuf[IDX[k] >> PAGE_SHIFT] +
				    offset_in_page(IDX[k]);

			n = template[i].tap[k];
			if (k == template[i].np - 1)
				n += enc ? authsize : -authsize;

			if (memcmp(q, template[i].result + temp, n)) {
				pr_err("alg: aead%s: Chunk test %d failed on %s at page %u for %s\n",
				       d, j, e, k, algo);
				hexdump(q, n);
				goto out;
			}

			q += n;
			if (k == template[i].np - 1 && !enc) {
				if (!diff_dst &&
					memcmp(q, template[i].input +
					      temp + n, authsize))
					n = authsize;
				else
					n = 0;
			} else {
				for (n = 0; offset_in_page(q + n) && q[n]; n++)
					;
			}
			if (n) {
				pr_err("alg: aead%s: Result buffer corruption in chunk test %d on %s at page %u for %s: %u bytes:\n",
				       d, j, e, k, algo, n);
				hexdump(q, n);
				goto out;
			}

			temp += template[i].tap[k];
		}
	}

	ret = 0;

out:
	aead_request_free(req);
	kfree(sg);
out_nosg:
	if (diff_dst)
		testmgr_free_buf(xoutbuf);
out_nooutbuf:
	testmgr_free_buf(axbuf);
out_noaxbuf:
	testmgr_free_buf(xbuf);
out_noxbuf:
	kfree(key);
	kfree(iv);
	return ret;
}

static int test_aead(struct crypto_aead *tfm, int enc,
		     const struct aead_testvec *template, unsigned int tcount)
{
	unsigned int alignmask;
	int ret;

	/* test 'dst == src' case */
	ret = __test_aead(tfm, enc, template, tcount, false, 0);
	if (ret)
		return ret;

	/* test 'dst != src' case */
	ret = __test_aead(tfm, enc, template, tcount, true, 0);
	if (ret)
		return ret;

	/* test unaligned buffers, check with one byte offset */
	ret = __test_aead(tfm, enc, template, tcount, true, 1);
	if (ret)
		return ret;

	alignmask = crypto_tfm_alg_alignmask(&tfm->base);
	if (alignmask) {
		/* Check if alignment mask for tfm is correctly set. */
		ret = __test_aead(tfm, enc, template, tcount, true,
				  alignmask + 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int test_cipher(struct crypto_cipher *tfm, int enc,
		       const struct cipher_testvec *template,
		       unsigned int tcount)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_cipher_tfm(tfm));
	unsigned int i, j, k;
	char *q;
	const char *e;
	const char *input, *result;
	void *data;
	char *xbuf[XBUFSIZE];
	int ret = -ENOMEM;

	if (testmgr_alloc_buf(xbuf))
		goto out_nobuf;

	if (enc == ENCRYPT)
	        e = "encryption";
	else
		e = "decryption";

	j = 0;
	for (i = 0; i < tcount; i++) {
		if (template[i].np)
			continue;

		if (fips_enabled && template[i].fips_skip)
			continue;

		input  = enc ? template[i].ptext : template[i].ctext;
		result = enc ? template[i].ctext : template[i].ptext;
		j++;

		ret = -EINVAL;
		if (WARN_ON(template[i].len > PAGE_SIZE))
			goto out;

		data = xbuf[0];
		memcpy(data, input, template[i].len);

		crypto_cipher_clear_flags(tfm, ~0);
		if (template[i].wk)
			crypto_cipher_set_flags(tfm, CRYPTO_TFM_REQ_WEAK_KEY);

		ret = crypto_cipher_setkey(tfm, template[i].key,
					   template[i].klen);
		if (template[i].fail == !ret) {
			printk(KERN_ERR "alg: cipher: setkey failed "
			       "on test %d for %s: flags=%x\n", j,
			       algo, crypto_cipher_get_flags(tfm));
			goto out;
		} else if (ret)
			continue;

		for (k = 0; k < template[i].len;
		     k += crypto_cipher_blocksize(tfm)) {
			if (enc)
				crypto_cipher_encrypt_one(tfm, data + k,
							  data + k);
			else
				crypto_cipher_decrypt_one(tfm, data + k,
							  data + k);
		}

		q = data;
		if (memcmp(q, result, template[i].len)) {
			printk(KERN_ERR "alg: cipher: Test %d failed "
			       "on %s for %s\n", j, e, algo);
			hexdump(q, template[i].len);
			ret = -EINVAL;
			goto out;
		}
	}

	ret = 0;

out:
	testmgr_free_buf(xbuf);
out_nobuf:
	return ret;
}

static int __test_skcipher(struct crypto_skcipher *tfm, int enc,
			   const struct cipher_testvec *template,
			   unsigned int tcount,
			   const bool diff_dst, const int align_offset)
{
	const char *algo =
		crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm));
	unsigned int i, j, k, n, temp;
	char *q;
	struct skcipher_request *req;
	struct scatterlist sg[8];
	struct scatterlist sgout[8];
	const char *e, *d;
	struct crypto_wait wait;
	const char *input, *result;
	void *data;
	char iv[MAX_IVLEN];
	char *xbuf[XBUFSIZE];
	char *xoutbuf[XBUFSIZE];
	int ret = -ENOMEM;
	unsigned int ivsize = crypto_skcipher_ivsize(tfm);

	if (testmgr_alloc_buf(xbuf))
		goto out_nobuf;

	if (diff_dst && testmgr_alloc_buf(xoutbuf))
		goto out_nooutbuf;

	if (diff_dst)
		d = "-ddst";
	else
		d = "";

	if (enc == ENCRYPT)
	        e = "encryption";
	else
		e = "decryption";

	crypto_init_wait(&wait);

	req = skcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("alg: skcipher%s: Failed to allocate request for %s\n",
		       d, algo);
		goto out;
	}

	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				      crypto_req_done, &wait);

	j = 0;
	for (i = 0; i < tcount; i++) {
		if (template[i].np && !template[i].also_non_np)
			continue;

		if (fips_enabled && template[i].fips_skip)
			continue;

		if (template[i].iv && !(template[i].generates_iv && enc))
			memcpy(iv, template[i].iv, ivsize);
		else
			memset(iv, 0, MAX_IVLEN);

		input  = enc ? template[i].ptext : template[i].ctext;
		result = enc ? template[i].ctext : template[i].ptext;
		j++;
		ret = -EINVAL;
		if (WARN_ON(align_offset + template[i].len > PAGE_SIZE))
			goto out;

		data = xbuf[0];
		data += align_offset;
		memcpy(data, input, template[i].len);

		crypto_skcipher_clear_flags(tfm, ~0);
		if (template[i].wk)
			crypto_skcipher_set_flags(tfm,
						  CRYPTO_TFM_REQ_WEAK_KEY);

		ret = crypto_skcipher_setkey(tfm, template[i].key,
					     template[i].klen);
		if (template[i].fail == !ret) {
			pr_err("alg: skcipher%s: setkey failed on test %d for %s: flags=%x\n",
			       d, j, algo, crypto_skcipher_get_flags(tfm));
			goto out;
		} else if (ret)
			continue;

		sg_init_one(&sg[0], data, template[i].len);
		if (diff_dst) {
			data = xoutbuf[0];
			data += align_offset;
			sg_init_one(&sgout[0], data, template[i].len);
		}

		skcipher_request_set_crypt(req, sg, (diff_dst) ? sgout : sg,
					   template[i].len, iv);
		ret = crypto_wait_req(enc ? crypto_skcipher_encrypt(req) :
				      crypto_skcipher_decrypt(req), &wait);

		if (ret) {
			pr_err("alg: skcipher%s: %s failed on test %d for %s: ret=%d\n",
			       d, e, j, algo, -ret);
			goto out;
		}

		q = data;
		if (memcmp(q, result, template[i].len)) {
			pr_err("alg: skcipher%s: Test %d failed (invalid result) on %s for %s\n",
			       d, j, e, algo);
			hexdump(q, template[i].len);
			ret = -EINVAL;
			goto out;
		}

		if (template[i].generates_iv && enc &&
		    memcmp(iv, template[i].iv, crypto_skcipher_ivsize(tfm))) {
			pr_err("alg: skcipher%s: Test %d failed (invalid output IV) on %s for %s\n",
			       d, j, e, algo);
			hexdump(iv, crypto_skcipher_ivsize(tfm));
			ret = -EINVAL;
			goto out;
		}
	}

	j = 0;
	for (i = 0; i < tcount; i++) {
		/* alignment tests are only done with continuous buffers */
		if (align_offset != 0)
			break;

		if (!template[i].np)
			continue;

		if (fips_enabled && template[i].fips_skip)
			continue;

		if (template[i].iv && !(template[i].generates_iv && enc))
			memcpy(iv, template[i].iv, ivsize);
		else
			memset(iv, 0, MAX_IVLEN);

		input  = enc ? template[i].ptext : template[i].ctext;
		result = enc ? template[i].ctext : template[i].ptext;
		j++;
		crypto_skcipher_clear_flags(tfm, ~0);
		if (template[i].wk)
			crypto_skcipher_set_flags(tfm,
						  CRYPTO_TFM_REQ_WEAK_KEY);

		ret = crypto_skcipher_setkey(tfm, template[i].key,
					     template[i].klen);
		if (template[i].fail == !ret) {
			pr_err("alg: skcipher%s: setkey failed on chunk test %d for %s: flags=%x\n",
			       d, j, algo, crypto_skcipher_get_flags(tfm));
			goto out;
		} else if (ret)
			continue;

		temp = 0;
		ret = -EINVAL;
		sg_init_table(sg, template[i].np);
		if (diff_dst)
			sg_init_table(sgout, template[i].np);
		for (k = 0; k < template[i].np; k++) {
			if (WARN_ON(offset_in_page(IDX[k]) +
				    template[i].tap[k] > PAGE_SIZE))
				goto out;

			q = xbuf[IDX[k] >> PAGE_SHIFT] + offset_in_page(IDX[k]);

			memcpy(q, input + temp, template[i].tap[k]);

			if (offset_in_page(q) + template[i].tap[k] < PAGE_SIZE)
				q[template[i].tap[k]] = 0;

			sg_set_buf(&sg[k], q, template[i].tap[k]);
			if (diff_dst) {
				q = xoutbuf[IDX[k] >> PAGE_SHIFT] +
				    offset_in_page(IDX[k]);

				sg_set_buf(&sgout[k], q, template[i].tap[k]);

				memset(q, 0, template[i].tap[k]);
				if (offset_in_page(q) +
				    template[i].tap[k] < PAGE_SIZE)
					q[template[i].tap[k]] = 0;
			}

			temp += template[i].tap[k];
		}

		skcipher_request_set_crypt(req, sg, (diff_dst) ? sgout : sg,
					   template[i].len, iv);

		ret = crypto_wait_req(enc ? crypto_skcipher_encrypt(req) :
				      crypto_skcipher_decrypt(req), &wait);

		if (ret) {
			pr_err("alg: skcipher%s: %s failed on chunk test %d for %s: ret=%d\n",
			       d, e, j, algo, -ret);
			goto out;
		}

		temp = 0;
		ret = -EINVAL;
		for (k = 0; k < template[i].np; k++) {
			if (diff_dst)
				q = xoutbuf[IDX[k] >> PAGE_SHIFT] +
				    offset_in_page(IDX[k]);
			else
				q = xbuf[IDX[k] >> PAGE_SHIFT] +
				    offset_in_page(IDX[k]);

			if (memcmp(q, result + temp, template[i].tap[k])) {
				pr_err("alg: skcipher%s: Chunk test %d failed on %s at page %u for %s\n",
				       d, j, e, k, algo);
				hexdump(q, template[i].tap[k]);
				goto out;
			}

			q += template[i].tap[k];
			for (n = 0; offset_in_page(q + n) && q[n]; n++)
				;
			if (n) {
				pr_err("alg: skcipher%s: Result buffer corruption in chunk test %d on %s at page %u for %s: %u bytes:\n",
				       d, j, e, k, algo, n);
				hexdump(q, n);
				goto out;
			}
			temp += template[i].tap[k];
		}
	}

	ret = 0;

out:
	skcipher_request_free(req);
	if (diff_dst)
		testmgr_free_buf(xoutbuf);
out_nooutbuf:
	testmgr_free_buf(xbuf);
out_nobuf:
	return ret;
}

static int test_skcipher(struct crypto_skcipher *tfm, int enc,
			 const struct cipher_testvec *template,
			 unsigned int tcount)
{
	unsigned int alignmask;
	int ret;

	/* test 'dst == src' case */
	ret = __test_skcipher(tfm, enc, template, tcount, false, 0);
	if (ret)
		return ret;

	/* test 'dst != src' case */
	ret = __test_skcipher(tfm, enc, template, tcount, true, 0);
	if (ret)
		return ret;

	/* test unaligned buffers, check with one byte offset */
	ret = __test_skcipher(tfm, enc, template, tcount, true, 1);
	if (ret)
		return ret;

	alignmask = crypto_tfm_alg_alignmask(&tfm->base);
	if (alignmask) {
		/* Check if alignment mask for tfm is correctly set. */
		ret = __test_skcipher(tfm, enc, template, tcount, true,
				      alignmask + 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int test_comp(struct crypto_comp *tfm,
		     const struct comp_testvec *ctemplate,
		     const struct comp_testvec *dtemplate,
		     int ctcount, int dtcount)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_comp_tfm(tfm));
	char *output, *decomp_output;
	unsigned int i;
	int ret;

	output = kmalloc(COMP_BUF_SIZE, GFP_KERNEL);
	if (!output)
		return -ENOMEM;

	decomp_output = kmalloc(COMP_BUF_SIZE, GFP_KERNEL);
	if (!decomp_output) {
		kfree(output);
		return -ENOMEM;
	}

	for (i = 0; i < ctcount; i++) {
		int ilen;
		unsigned int dlen = COMP_BUF_SIZE;

		memset(output, 0, sizeof(COMP_BUF_SIZE));
		memset(decomp_output, 0, sizeof(COMP_BUF_SIZE));

		ilen = ctemplate[i].inlen;
		ret = crypto_comp_compress(tfm, ctemplate[i].input,
					   ilen, output, &dlen);
		if (ret) {
			printk(KERN_ERR "alg: comp: compression failed "
			       "on test %d for %s: ret=%d\n", i + 1, algo,
			       -ret);
			goto out;
		}

		ilen = dlen;
		dlen = COMP_BUF_SIZE;
		ret = crypto_comp_decompress(tfm, output,
					     ilen, decomp_output, &dlen);
		if (ret) {
			pr_err("alg: comp: compression failed: decompress: on test %d for %s failed: ret=%d\n",
			       i + 1, algo, -ret);
			goto out;
		}

		if (dlen != ctemplate[i].inlen) {
			printk(KERN_ERR "alg: comp: Compression test %d "
			       "failed for %s: output len = %d\n", i + 1, algo,
			       dlen);
			ret = -EINVAL;
			goto out;
		}

		if (memcmp(decomp_output, ctemplate[i].input,
			   ctemplate[i].inlen)) {
			pr_err("alg: comp: compression failed: output differs: on test %d for %s\n",
			       i + 1, algo);
			hexdump(decomp_output, dlen);
			ret = -EINVAL;
			goto out;
		}
	}

	for (i = 0; i < dtcount; i++) {
		int ilen;
		unsigned int dlen = COMP_BUF_SIZE;

		memset(decomp_output, 0, sizeof(COMP_BUF_SIZE));

		ilen = dtemplate[i].inlen;
		ret = crypto_comp_decompress(tfm, dtemplate[i].input,
					     ilen, decomp_output, &dlen);
		if (ret) {
			printk(KERN_ERR "alg: comp: decompression failed "
			       "on test %d for %s: ret=%d\n", i + 1, algo,
			       -ret);
			goto out;
		}

		if (dlen != dtemplate[i].outlen) {
			printk(KERN_ERR "alg: comp: Decompression test %d "
			       "failed for %s: output len = %d\n", i + 1, algo,
			       dlen);
			ret = -EINVAL;
			goto out;
		}

		if (memcmp(decomp_output, dtemplate[i].output, dlen)) {
			printk(KERN_ERR "alg: comp: Decompression test %d "
			       "failed for %s\n", i + 1, algo);
			hexdump(decomp_output, dlen);
			ret = -EINVAL;
			goto out;
		}
	}

	ret = 0;

out:
	kfree(decomp_output);
	kfree(output);
	return ret;
}

static int test_acomp(struct crypto_acomp *tfm,
			      const struct comp_testvec *ctemplate,
		      const struct comp_testvec *dtemplate,
		      int ctcount, int dtcount)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_acomp_tfm(tfm));
	unsigned int i;
	char *output, *decomp_out;
	int ret;
	struct scatterlist src, dst;
	struct acomp_req *req;
	struct crypto_wait wait;

	output = kmalloc(COMP_BUF_SIZE, GFP_KERNEL);
	if (!output)
		return -ENOMEM;

	decomp_out = kmalloc(COMP_BUF_SIZE, GFP_KERNEL);
	if (!decomp_out) {
		kfree(output);
		return -ENOMEM;
	}

	for (i = 0; i < ctcount; i++) {
		unsigned int dlen = COMP_BUF_SIZE;
		int ilen = ctemplate[i].inlen;
		void *input_vec;

		input_vec = kmemdup(ctemplate[i].input, ilen, GFP_KERNEL);
		if (!input_vec) {
			ret = -ENOMEM;
			goto out;
		}

		memset(output, 0, dlen);
		crypto_init_wait(&wait);
		sg_init_one(&src, input_vec, ilen);
		sg_init_one(&dst, output, dlen);

		req = acomp_request_alloc(tfm);
		if (!req) {
			pr_err("alg: acomp: request alloc failed for %s\n",
			       algo);
			kfree(input_vec);
			ret = -ENOMEM;
			goto out;
		}

		acomp_request_set_params(req, &src, &dst, ilen, dlen);
		acomp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					   crypto_req_done, &wait);

		ret = crypto_wait_req(crypto_acomp_compress(req), &wait);
		if (ret) {
			pr_err("alg: acomp: compression failed on test %d for %s: ret=%d\n",
			       i + 1, algo, -ret);
			kfree(input_vec);
			acomp_request_free(req);
			goto out;
		}

		ilen = req->dlen;
		dlen = COMP_BUF_SIZE;
		sg_init_one(&src, output, ilen);
		sg_init_one(&dst, decomp_out, dlen);
		crypto_init_wait(&wait);
		acomp_request_set_params(req, &src, &dst, ilen, dlen);

		ret = crypto_wait_req(crypto_acomp_decompress(req), &wait);
		if (ret) {
			pr_err("alg: acomp: compression failed on test %d for %s: ret=%d\n",
			       i + 1, algo, -ret);
			kfree(input_vec);
			acomp_request_free(req);
			goto out;
		}

		if (req->dlen != ctemplate[i].inlen) {
			pr_err("alg: acomp: Compression test %d failed for %s: output len = %d\n",
			       i + 1, algo, req->dlen);
			ret = -EINVAL;
			kfree(input_vec);
			acomp_request_free(req);
			goto out;
		}

		if (memcmp(input_vec, decomp_out, req->dlen)) {
			pr_err("alg: acomp: Compression test %d failed for %s\n",
			       i + 1, algo);
			hexdump(output, req->dlen);
			ret = -EINVAL;
			kfree(input_vec);
			acomp_request_free(req);
			goto out;
		}

		kfree(input_vec);
		acomp_request_free(req);
	}

	for (i = 0; i < dtcount; i++) {
		unsigned int dlen = COMP_BUF_SIZE;
		int ilen = dtemplate[i].inlen;
		void *input_vec;

		input_vec = kmemdup(dtemplate[i].input, ilen, GFP_KERNEL);
		if (!input_vec) {
			ret = -ENOMEM;
			goto out;
		}

		memset(output, 0, dlen);
		crypto_init_wait(&wait);
		sg_init_one(&src, input_vec, ilen);
		sg_init_one(&dst, output, dlen);

		req = acomp_request_alloc(tfm);
		if (!req) {
			pr_err("alg: acomp: request alloc failed for %s\n",
			       algo);
			kfree(input_vec);
			ret = -ENOMEM;
			goto out;
		}

		acomp_request_set_params(req, &src, &dst, ilen, dlen);
		acomp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					   crypto_req_done, &wait);

		ret = crypto_wait_req(crypto_acomp_decompress(req), &wait);
		if (ret) {
			pr_err("alg: acomp: decompression failed on test %d for %s: ret=%d\n",
			       i + 1, algo, -ret);
			kfree(input_vec);
			acomp_request_free(req);
			goto out;
		}

		if (req->dlen != dtemplate[i].outlen) {
			pr_err("alg: acomp: Decompression test %d failed for %s: output len = %d\n",
			       i + 1, algo, req->dlen);
			ret = -EINVAL;
			kfree(input_vec);
			acomp_request_free(req);
			goto out;
		}

		if (memcmp(output, dtemplate[i].output, req->dlen)) {
			pr_err("alg: acomp: Decompression test %d failed for %s\n",
			       i + 1, algo);
			hexdump(output, req->dlen);
			ret = -EINVAL;
			kfree(input_vec);
			acomp_request_free(req);
			goto out;
		}

		kfree(input_vec);
		acomp_request_free(req);
	}

	ret = 0;

out:
	kfree(decomp_out);
	kfree(output);
	return ret;
}

static int test_cprng(struct crypto_rng *tfm,
		      const struct cprng_testvec *template,
		      unsigned int tcount)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_rng_tfm(tfm));
	int err = 0, i, j, seedsize;
	u8 *seed;
	char result[32];

	seedsize = crypto_rng_seedsize(tfm);

	seed = kmalloc(seedsize, GFP_KERNEL);
	if (!seed) {
		printk(KERN_ERR "alg: cprng: Failed to allocate seed space "
		       "for %s\n", algo);
		return -ENOMEM;
	}

	for (i = 0; i < tcount; i++) {
		memset(result, 0, 32);

		memcpy(seed, template[i].v, template[i].vlen);
		memcpy(seed + template[i].vlen, template[i].key,
		       template[i].klen);
		memcpy(seed + template[i].vlen + template[i].klen,
		       template[i].dt, template[i].dtlen);

		err = crypto_rng_reset(tfm, seed, seedsize);
		if (err) {
			printk(KERN_ERR "alg: cprng: Failed to reset rng "
			       "for %s\n", algo);
			goto out;
		}

		for (j = 0; j < template[i].loops; j++) {
			err = crypto_rng_get_bytes(tfm, result,
						   template[i].rlen);
			if (err < 0) {
				printk(KERN_ERR "alg: cprng: Failed to obtain "
				       "the correct amount of random data for "
				       "%s (requested %d)\n", algo,
				       template[i].rlen);
				goto out;
			}
		}

		err = memcmp(result, template[i].result,
			     template[i].rlen);
		if (err) {
			printk(KERN_ERR "alg: cprng: Test %d failed for %s\n",
			       i, algo);
			hexdump(result, template[i].rlen);
			err = -EINVAL;
			goto out;
		}
	}

out:
	kfree(seed);
	return err;
}

static int alg_test_aead(const struct alg_test_desc *desc, const char *driver,
			 u32 type, u32 mask)
{
	struct crypto_aead *tfm;
	int err = 0;

	tfm = crypto_alloc_aead(driver, type, mask);
	if (IS_ERR(tfm)) {
		printk(KERN_ERR "alg: aead: Failed to load transform for %s: "
		       "%ld\n", driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	if (desc->suite.aead.enc.vecs) {
		err = test_aead(tfm, ENCRYPT, desc->suite.aead.enc.vecs,
				desc->suite.aead.enc.count);
		if (err)
			goto out;
	}

	if (!err && desc->suite.aead.dec.vecs)
		err = test_aead(tfm, DECRYPT, desc->suite.aead.dec.vecs,
				desc->suite.aead.dec.count);

out:
	crypto_free_aead(tfm);
	return err;
}

static int alg_test_cipher(const struct alg_test_desc *desc,
			   const char *driver, u32 type, u32 mask)
{
	const struct cipher_test_suite *suite = &desc->suite.cipher;
	struct crypto_cipher *tfm;
	int err;

	tfm = crypto_alloc_cipher(driver, type, mask);
	if (IS_ERR(tfm)) {
		printk(KERN_ERR "alg: cipher: Failed to load transform for "
		       "%s: %ld\n", driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	err = test_cipher(tfm, ENCRYPT, suite->vecs, suite->count);
	if (!err)
		err = test_cipher(tfm, DECRYPT, suite->vecs, suite->count);

	crypto_free_cipher(tfm);
	return err;
}

static int alg_test_skcipher(const struct alg_test_desc *desc,
			     const char *driver, u32 type, u32 mask)
{
	const struct cipher_test_suite *suite = &desc->suite.cipher;
	struct crypto_skcipher *tfm;
	int err;

	tfm = crypto_alloc_skcipher(driver, type, mask);
	if (IS_ERR(tfm)) {
		printk(KERN_ERR "alg: skcipher: Failed to load transform for "
		       "%s: %ld\n", driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	err = test_skcipher(tfm, ENCRYPT, suite->vecs, suite->count);
	if (!err)
		err = test_skcipher(tfm, DECRYPT, suite->vecs, suite->count);

	crypto_free_skcipher(tfm);
	return err;
}

static int alg_test_comp(const struct alg_test_desc *desc, const char *driver,
			 u32 type, u32 mask)
{
	struct crypto_comp *comp;
	struct crypto_acomp *acomp;
	int err;
	u32 algo_type = type & CRYPTO_ALG_TYPE_ACOMPRESS_MASK;

	if (algo_type == CRYPTO_ALG_TYPE_ACOMPRESS) {
		acomp = crypto_alloc_acomp(driver, type, mask);
		if (IS_ERR(acomp)) {
			pr_err("alg: acomp: Failed to load transform for %s: %ld\n",
			       driver, PTR_ERR(acomp));
			return PTR_ERR(acomp);
		}
		err = test_acomp(acomp, desc->suite.comp.comp.vecs,
				 desc->suite.comp.decomp.vecs,
				 desc->suite.comp.comp.count,
				 desc->suite.comp.decomp.count);
		crypto_free_acomp(acomp);
	} else {
		comp = crypto_alloc_comp(driver, type, mask);
		if (IS_ERR(comp)) {
			pr_err("alg: comp: Failed to load transform for %s: %ld\n",
			       driver, PTR_ERR(comp));
			return PTR_ERR(comp);
		}

		err = test_comp(comp, desc->suite.comp.comp.vecs,
				desc->suite.comp.decomp.vecs,
				desc->suite.comp.comp.count,
				desc->suite.comp.decomp.count);

		crypto_free_comp(comp);
	}
	return err;
}

static int __alg_test_hash(const struct hash_testvec *template,
			   unsigned int tcount, const char *driver,
			   u32 type, u32 mask)
{
	struct crypto_ahash *tfm;
	int err;

	tfm = crypto_alloc_ahash(driver, type, mask);
	if (IS_ERR(tfm)) {
		printk(KERN_ERR "alg: hash: Failed to load transform for %s: "
		       "%ld\n", driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	err = test_hash(tfm, template, tcount, true);
	if (!err)
		err = test_hash(tfm, template, tcount, false);
	crypto_free_ahash(tfm);
	return err;
}

static int alg_test_hash(const struct alg_test_desc *desc, const char *driver,
			 u32 type, u32 mask)
{
	const struct hash_testvec *template = desc->suite.hash.vecs;
	unsigned int tcount = desc->suite.hash.count;
	unsigned int nr_unkeyed, nr_keyed;
	int err;

	/*
	 * For OPTIONAL_KEY algorithms, we have to do all the unkeyed tests
	 * first, before setting a key on the tfm.  To make this easier, we
	 * require that the unkeyed test vectors (if any) are listed first.
	 */

	for (nr_unkeyed = 0; nr_unkeyed < tcount; nr_unkeyed++) {
		if (template[nr_unkeyed].ksize)
			break;
	}
	for (nr_keyed = 0; nr_unkeyed + nr_keyed < tcount; nr_keyed++) {
		if (!template[nr_unkeyed + nr_keyed].ksize) {
			pr_err("alg: hash: test vectors for %s out of order, "
			       "unkeyed ones must come first\n", desc->alg);
			return -EINVAL;
		}
	}

	err = 0;
	if (nr_unkeyed) {
		err = __alg_test_hash(template, nr_unkeyed, driver, type, mask);
		template += nr_unkeyed;
	}

	if (!err && nr_keyed)
		err = __alg_test_hash(template, nr_keyed, driver, type, mask);

	return err;
}

static int alg_test_crc32c(const struct alg_test_desc *desc,
			   const char *driver, u32 type, u32 mask)
{
	struct crypto_shash *tfm;
	u32 val;
	int err;

	err = alg_test_hash(desc, driver, type, mask);
	if (err)
		goto out;

	tfm = crypto_alloc_shash(driver, type, mask);
	if (IS_ERR(tfm)) {
		printk(KERN_ERR "alg: crc32c: Failed to load transform for %s: "
		       "%ld\n", driver, PTR_ERR(tfm));
		err = PTR_ERR(tfm);
		goto out;
	}

	do {
		SHASH_DESC_ON_STACK(shash, tfm);
		u32 *ctx = (u32 *)shash_desc_ctx(shash);

		shash->tfm = tfm;
		shash->flags = 0;

		*ctx = le32_to_cpu(420553207);
		err = crypto_shash_final(shash, (u8 *)&val);
		if (err) {
			printk(KERN_ERR "alg: crc32c: Operation failed for "
			       "%s: %d\n", driver, err);
			break;
		}

		if (val != ~420553207) {
			printk(KERN_ERR "alg: crc32c: Test failed for %s: "
			       "%d\n", driver, val);
			err = -EINVAL;
		}
	} while (0);

	crypto_free_shash(tfm);

out:
	return err;
}

static int alg_test_cprng(const struct alg_test_desc *desc, const char *driver,
			  u32 type, u32 mask)
{
	struct crypto_rng *rng;
	int err;

	rng = crypto_alloc_rng(driver, type, mask);
	if (IS_ERR(rng)) {
		printk(KERN_ERR "alg: cprng: Failed to load transform for %s: "
		       "%ld\n", driver, PTR_ERR(rng));
		return PTR_ERR(rng);
	}

	err = test_cprng(rng, desc->suite.cprng.vecs, desc->suite.cprng.count);

	crypto_free_rng(rng);

	return err;
}


static int drbg_cavs_test(const struct drbg_testvec *test, int pr,
			  const char *driver, u32 type, u32 mask)
{
	int ret = -EAGAIN;
	struct crypto_rng *drng;
	struct drbg_test_data test_data;
	struct drbg_string addtl, pers, testentropy;
	unsigned char *buf = kzalloc(test->expectedlen, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	drng = crypto_alloc_rng(driver, type, mask);
	if (IS_ERR(drng)) {
		printk(KERN_ERR "alg: drbg: could not allocate DRNG handle for "
		       "%s\n", driver);
		kzfree(buf);
		return -ENOMEM;
	}

	test_data.testentropy = &testentropy;
	drbg_string_fill(&testentropy, test->entropy, test->entropylen);
	drbg_string_fill(&pers, test->pers, test->perslen);
	ret = crypto_drbg_reset_test(drng, &pers, &test_data);
	if (ret) {
		printk(KERN_ERR "alg: drbg: Failed to reset rng\n");
		goto outbuf;
	}

	drbg_string_fill(&addtl, test->addtla, test->addtllen);
	if (pr) {
		drbg_string_fill(&testentropy, test->entpra, test->entprlen);
		ret = crypto_drbg_get_bytes_addtl_test(drng,
			buf, test->expectedlen, &addtl,	&test_data);
	} else {
		ret = crypto_drbg_get_bytes_addtl(drng,
			buf, test->expectedlen, &addtl);
	}
	if (ret < 0) {
		printk(KERN_ERR "alg: drbg: could not obtain random data for "
		       "driver %s\n", driver);
		goto outbuf;
	}

	drbg_string_fill(&addtl, test->addtlb, test->addtllen);
	if (pr) {
		drbg_string_fill(&testentropy, test->entprb, test->entprlen);
		ret = crypto_drbg_get_bytes_addtl_test(drng,
			buf, test->expectedlen, &addtl, &test_data);
	} else {
		ret = crypto_drbg_get_bytes_addtl(drng,
			buf, test->expectedlen, &addtl);
	}
	if (ret < 0) {
		printk(KERN_ERR "alg: drbg: could not obtain random data for "
		       "driver %s\n", driver);
		goto outbuf;
	}

	ret = memcmp(test->expected, buf, test->expectedlen);

outbuf:
	crypto_free_rng(drng);
	kzfree(buf);
	return ret;
}


static int alg_test_drbg(const struct alg_test_desc *desc, const char *driver,
			 u32 type, u32 mask)
{
	int err = 0;
	int pr = 0;
	int i = 0;
	const struct drbg_testvec *template = desc->suite.drbg.vecs;
	unsigned int tcount = desc->suite.drbg.count;

	if (0 == memcmp(driver, "drbg_pr_", 8))
		pr = 1;

	for (i = 0; i < tcount; i++) {
		err = drbg_cavs_test(&template[i], pr, driver, type, mask);
		if (err) {
			printk(KERN_ERR "alg: drbg: Test %d failed for %s\n",
			       i, driver);
			err = -EINVAL;
			break;
		}
	}
	return err;

}

static int do_test_kpp(struct crypto_kpp *tfm, const struct kpp_testvec *vec,
		       const char *alg)
{
	struct kpp_request *req;
	void *input_buf = NULL;
	void *output_buf = NULL;
	void *a_public = NULL;
	void *a_ss = NULL;
	void *shared_secret = NULL;
	struct crypto_wait wait;
	unsigned int out_len_max;
	int err = -ENOMEM;
	struct scatterlist src, dst;

	req = kpp_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		return err;

	crypto_init_wait(&wait);

	err = crypto_kpp_set_secret(tfm, vec->secret, vec->secret_size);
	if (err < 0)
		goto free_req;

	out_len_max = crypto_kpp_maxsize(tfm);
	output_buf = kzalloc(out_len_max, GFP_KERNEL);
	if (!output_buf) {
		err = -ENOMEM;
		goto free_req;
	}

	/* Use appropriate parameter as base */
	kpp_request_set_input(req, NULL, 0);
	sg_init_one(&dst, output_buf, out_len_max);
	kpp_request_set_output(req, &dst, out_len_max);
	kpp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				 crypto_req_done, &wait);

	/* Compute party A's public key */
	err = crypto_wait_req(crypto_kpp_generate_public_key(req), &wait);
	if (err) {
		pr_err("alg: %s: Party A: generate public key test failed. err %d\n",
		       alg, err);
		goto free_output;
	}

	if (vec->genkey) {
		/* Save party A's public key */
		a_public = kzalloc(out_len_max, GFP_KERNEL);
		if (!a_public) {
			err = -ENOMEM;
			goto free_output;
		}
		memcpy(a_public, sg_virt(req->dst), out_len_max);
	} else {
		/* Verify calculated public key */
		if (memcmp(vec->expected_a_public, sg_virt(req->dst),
			   vec->expected_a_public_size)) {
			pr_err("alg: %s: Party A: generate public key test failed. Invalid output\n",
			       alg);
			err = -EINVAL;
			goto free_output;
		}
	}

	/* Calculate shared secret key by using counter part (b) public key. */
	input_buf = kzalloc(vec->b_public_size, GFP_KERNEL);
	if (!input_buf) {
		err = -ENOMEM;
		goto free_output;
	}

	memcpy(input_buf, vec->b_public, vec->b_public_size);
	sg_init_one(&src, input_buf, vec->b_public_size);
	sg_init_one(&dst, output_buf, out_len_max);
	kpp_request_set_input(req, &src, vec->b_public_size);
	kpp_request_set_output(req, &dst, out_len_max);
	kpp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				 crypto_req_done, &wait);
	err = crypto_wait_req(crypto_kpp_compute_shared_secret(req), &wait);
	if (err) {
		pr_err("alg: %s: Party A: compute shared secret test failed. err %d\n",
		       alg, err);
		goto free_all;
	}

	if (vec->genkey) {
		/* Save the shared secret obtained by party A */
		a_ss = kzalloc(vec->expected_ss_size, GFP_KERNEL);
		if (!a_ss) {
			err = -ENOMEM;
			goto free_all;
		}
		memcpy(a_ss, sg_virt(req->dst), vec->expected_ss_size);

		/*
		 * Calculate party B's shared secret by using party A's
		 * public key.
		 */
		err = crypto_kpp_set_secret(tfm, vec->b_secret,
					    vec->b_secret_size);
		if (err < 0)
			goto free_all;

		sg_init_one(&src, a_public, vec->expected_a_public_size);
		sg_init_one(&dst, output_buf, out_len_max);
		kpp_request_set_input(req, &src, vec->expected_a_public_size);
		kpp_request_set_output(req, &dst, out_len_max);
		kpp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					 crypto_req_done, &wait);
		err = crypto_wait_req(crypto_kpp_compute_shared_secret(req),
				      &wait);
		if (err) {
			pr_err("alg: %s: Party B: compute shared secret failed. err %d\n",
			       alg, err);
			goto free_all;
		}

		shared_secret = a_ss;
	} else {
		shared_secret = (void *)vec->expected_ss;
	}

	/*
	 * verify shared secret from which the user will derive
	 * secret key by executing whatever hash it has chosen
	 */
	if (memcmp(shared_secret, sg_virt(req->dst),
		   vec->expected_ss_size)) {
		pr_err("alg: %s: compute shared secret test failed. Invalid output\n",
		       alg);
		err = -EINVAL;
	}

free_all:
	kfree(a_ss);
	kfree(input_buf);
free_output:
	kfree(a_public);
	kfree(output_buf);
free_req:
	kpp_request_free(req);
	return err;
}

static int test_kpp(struct crypto_kpp *tfm, const char *alg,
		    const struct kpp_testvec *vecs, unsigned int tcount)
{
	int ret, i;

	for (i = 0; i < tcount; i++) {
		ret = do_test_kpp(tfm, vecs++, alg);
		if (ret) {
			pr_err("alg: %s: test failed on vector %d, err=%d\n",
			       alg, i + 1, ret);
			return ret;
		}
	}
	return 0;
}

static int alg_test_kpp(const struct alg_test_desc *desc, const char *driver,
			u32 type, u32 mask)
{
	struct crypto_kpp *tfm;
	int err = 0;

	tfm = crypto_alloc_kpp(driver, type, mask);
	if (IS_ERR(tfm)) {
		pr_err("alg: kpp: Failed to load tfm for %s: %ld\n",
		       driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}
	if (desc->suite.kpp.vecs)
		err = test_kpp(tfm, desc->alg, desc->suite.kpp.vecs,
			       desc->suite.kpp.count);

	crypto_free_kpp(tfm);
	return err;
}

static int test_akcipher_one(struct crypto_akcipher *tfm,
			     const struct akcipher_testvec *vecs)
{
	char *xbuf[XBUFSIZE];
	struct akcipher_request *req;
	void *outbuf_enc = NULL;
	void *outbuf_dec = NULL;
	struct crypto_wait wait;
	unsigned int out_len_max, out_len = 0;
	int err = -ENOMEM;
	struct scatterlist src, dst, src_tab[2];

	if (testmgr_alloc_buf(xbuf))
		return err;

	req = akcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		goto free_xbuf;

	crypto_init_wait(&wait);

	if (vecs->public_key_vec)
		err = crypto_akcipher_set_pub_key(tfm, vecs->key,
						  vecs->key_len);
	else
		err = crypto_akcipher_set_priv_key(tfm, vecs->key,
						   vecs->key_len);
	if (err)
		goto free_req;

	err = -ENOMEM;
	out_len_max = crypto_akcipher_maxsize(tfm);
	outbuf_enc = kzalloc(out_len_max, GFP_KERNEL);
	if (!outbuf_enc)
		goto free_req;

	if (WARN_ON(vecs->m_size > PAGE_SIZE))
		goto free_all;

	memcpy(xbuf[0], vecs->m, vecs->m_size);

	sg_init_table(src_tab, 2);
	sg_set_buf(&src_tab[0], xbuf[0], 8);
	sg_set_buf(&src_tab[1], xbuf[0] + 8, vecs->m_size - 8);
	sg_init_one(&dst, outbuf_enc, out_len_max);
	akcipher_request_set_crypt(req, src_tab, &dst, vecs->m_size,
				   out_len_max);
	akcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				      crypto_req_done, &wait);

	err = crypto_wait_req(vecs->siggen_sigver_test ?
			      /* Run asymmetric signature generation */
			      crypto_akcipher_sign(req) :
			      /* Run asymmetric encrypt */
			      crypto_akcipher_encrypt(req), &wait);
	if (err) {
		pr_err("alg: akcipher: encrypt test failed. err %d\n", err);
		goto free_all;
	}
	if (req->dst_len != vecs->c_size) {
		pr_err("alg: akcipher: encrypt test failed. Invalid output len\n");
		err = -EINVAL;
		goto free_all;
	}
	/* verify that encrypted message is equal to expected */
	if (memcmp(vecs->c, outbuf_enc, vecs->c_size)) {
		pr_err("alg: akcipher: encrypt test failed. Invalid output\n");
		hexdump(outbuf_enc, vecs->c_size);
		err = -EINVAL;
		goto free_all;
	}
	/* Don't invoke decrypt for vectors with public key */
	if (vecs->public_key_vec) {
		err = 0;
		goto free_all;
	}
	outbuf_dec = kzalloc(out_len_max, GFP_KERNEL);
	if (!outbuf_dec) {
		err = -ENOMEM;
		goto free_all;
	}

	if (WARN_ON(vecs->c_size > PAGE_SIZE))
		goto free_all;

	memcpy(xbuf[0], vecs->c, vecs->c_size);

	sg_init_one(&src, xbuf[0], vecs->c_size);
	sg_init_one(&dst, outbuf_dec, out_len_max);
	crypto_init_wait(&wait);
	akcipher_request_set_crypt(req, &src, &dst, vecs->c_size, out_len_max);

	err = crypto_wait_req(vecs->siggen_sigver_test ?
			      /* Run asymmetric signature verification */
			      crypto_akcipher_verify(req) :
			      /* Run asymmetric decrypt */
			      crypto_akcipher_decrypt(req), &wait);
	if (err) {
		pr_err("alg: akcipher: decrypt test failed. err %d\n", err);
		goto free_all;
	}
	out_len = req->dst_len;
	if (out_len < vecs->m_size) {
		pr_err("alg: akcipher: decrypt test failed. "
		       "Invalid output len %u\n", out_len);
		err = -EINVAL;
		goto free_all;
	}
	/* verify that decrypted message is equal to the original msg */
	if (memchr_inv(outbuf_dec, 0, out_len - vecs->m_size) ||
	    memcmp(vecs->m, outbuf_dec + out_len - vecs->m_size,
		   vecs->m_size)) {
		pr_err("alg: akcipher: decrypt test failed. Invalid output\n");
		hexdump(outbuf_dec, out_len);
		err = -EINVAL;
	}
free_all:
	kfree(outbuf_dec);
	kfree(outbuf_enc);
free_req:
	akcipher_request_free(req);
free_xbuf:
	testmgr_free_buf(xbuf);
	return err;
}

static int test_akcipher(struct crypto_akcipher *tfm, const char *alg,
			 const struct akcipher_testvec *vecs,
			 unsigned int tcount)
{
	const char *algo =
		crypto_tfm_alg_driver_name(crypto_akcipher_tfm(tfm));
	int ret, i;

	for (i = 0; i < tcount; i++) {
		ret = test_akcipher_one(tfm, vecs++);
		if (!ret)
			continue;

		pr_err("alg: akcipher: test %d failed for %s, err=%d\n",
		       i + 1, algo, ret);
		return ret;
	}
	return 0;
}

static int alg_test_akcipher(const struct alg_test_desc *desc,
			     const char *driver, u32 type, u32 mask)
{
	struct crypto_akcipher *tfm;
	int err = 0;

	tfm = crypto_alloc_akcipher(driver, type, mask);
	if (IS_ERR(tfm)) {
		pr_err("alg: akcipher: Failed to load tfm for %s: %ld\n",
		       driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}
	if (desc->suite.akcipher.vecs)
		err = test_akcipher(tfm, desc->alg, desc->suite.akcipher.vecs,
				    desc->suite.akcipher.count);

	crypto_free_akcipher(tfm);
	return err;
}

static int alg_test_null(const struct alg_test_desc *desc,
			     const char *driver, u32 type, u32 mask)
{
	return 0;
}

#define __VECS(tv)	{ .vecs = tv, .count = ARRAY_SIZE(tv) }

/* Please keep this list sorted by algorithm name. */
static const struct alg_test_desc alg_test_descs[] = {
	{
		.alg = "aegis128",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = __VECS(aegis128_enc_tv_template),
				.dec = __VECS(aegis128_dec_tv_template),
			}
		}
	}, {
		.alg = "aegis128l",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = __VECS(aegis128l_enc_tv_template),
				.dec = __VECS(aegis128l_dec_tv_template),
			}
		}
	}, {
		.alg = "aegis256",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = __VECS(aegis256_enc_tv_template),
				.dec = __VECS(aegis256_dec_tv_template),
			}
		}
	}, {
		.alg = "ansi_cprng",
		.test = alg_test_cprng,
		.suite = {
			.cprng = __VECS(ansi_cprng_aes_tv_template)
		}
	}, {
		.alg = "authenc(hmac(md5),ecb(cipher_null))",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = __VECS(hmac_md5_ecb_cipher_null_enc_tv_template),
				.dec = __VECS(hmac_md5_ecb_cipher_null_dec_tv_template)
			}
		}
	}, {
		.alg = "authenc(hmac(sha1),cbc(aes))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = __VECS(hmac_sha1_aes_cbc_enc_tv_temp)
			}
		}
	}, {
		.alg = "authenc(hmac(sha1),cbc(des))",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = __VECS(hmac_sha1_des_cbc_enc_tv_temp)
			}
		}
	}, {
		.alg = "authenc(hmac(sha1),cbc(des3_ede))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = __VECS(hmac_sha1_des3_ede_cbc_enc_tv_temp)
			}
		}
	}, {
		.alg = "authenc(hmac(sha1),ctr(aes))",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "authenc(hmac(sha1),ecb(cipher_null))",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = __VECS(hmac_sha1_ecb_cipher_null_enc_tv_temp),
				.dec = __VECS(hmac_sha1_ecb_cipher_null_dec_tv_temp)
			}
		}
	}, {
		.alg = "authenc(hmac(sha1),rfc3686(ctr(aes)))",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "authenc(hmac(sha224),cbc(des))",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = __VECS(hmac_sha224_des_cbc_enc_tv_temp)
			}
		}
	}, {
		.alg = "authenc(hmac(sha224),cbc(des3_ede))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = __VECS(hmac_sha224_des3_ede_cbc_enc_tv_temp)
			}
		}
	}, {
		.alg = "authenc(hmac(sha256),cbc(aes))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = __VECS(hmac_sha256_aes_cbc_enc_tv_temp)
			}
		}
	}, {
		.alg = "authenc(hmac(sha256),cbc(des))",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = __VECS(hmac_sha256_des_cbc_enc_tv_temp)
			}
		}
	}, {
		.alg = "authenc(hmac(sha256),cbc(des3_ede))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = __VECS(hmac_sha256_des3_ede_cbc_enc_tv_temp)
			}
		}
	}, {
		.alg = "authenc(hmac(sha256),ctr(aes))",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "authenc(hmac(sha256),rfc3686(ctr(aes)))",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "authenc(hmac(sha384),cbc(des))",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = __VECS(hmac_sha384_des_cbc_enc_tv_temp)
			}
		}
	}, {
		.alg = "authenc(hmac(sha384),cbc(des3_ede))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = __VECS(hmac_sha384_des3_ede_cbc_enc_tv_temp)
			}
		}
	}, {
		.alg = "authenc(hmac(sha384),ctr(aes))",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "authenc(hmac(sha384),rfc3686(ctr(aes)))",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "authenc(hmac(sha512),cbc(aes))",
		.fips_allowed = 1,
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = __VECS(hmac_sha512_aes_cbc_enc_tv_temp)
			}
		}
	}, {
		.alg = "authenc(hmac(sha512),cbc(des))",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = __VECS(hmac_sha512_des_cbc_enc_tv_temp)
			}
		}
	}, {
		.alg = "authenc(hmac(sha512),cbc(des3_ede))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = __VECS(hmac_sha512_des3_ede_cbc_enc_tv_temp)
			}
		}
	}, {
		.alg = "authenc(hmac(sha512),ctr(aes))",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "authenc(hmac(sha512),rfc3686(ctr(aes)))",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "cbc(aes)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(aes_cbc_tv_template)
		},
	}, {
		.alg = "cbc(anubis)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(anubis_cbc_tv_template)
		},
	}, {
		.alg = "cbc(blowfish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(bf_cbc_tv_template)
		},
	}, {
		.alg = "cbc(camellia)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(camellia_cbc_tv_template)
		},
	}, {
		.alg = "cbc(cast5)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(cast5_cbc_tv_template)
		},
	}, {
		.alg = "cbc(cast6)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(cast6_cbc_tv_template)
		},
	}, {
		.alg = "cbc(des)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(des_cbc_tv_template)
		},
	}, {
		.alg = "cbc(des3_ede)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(des3_ede_cbc_tv_template)
		},
	}, {
		/* Same as cbc(aes) except the key is stored in
		 * hardware secure memory which we reference by index
		 */
		.alg = "cbc(paes)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "cbc(serpent)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(serpent_cbc_tv_template)
		},
	}, {
		.alg = "cbc(twofish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(tf_cbc_tv_template)
		},
	}, {
		.alg = "cbcmac(aes)",
		.fips_allowed = 1,
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(aes_cbcmac_tv_template)
		}
	}, {
		.alg = "ccm(aes)",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = __VECS(aes_ccm_enc_tv_template),
				.dec = __VECS(aes_ccm_dec_tv_template)
			}
		}
	}, {
		.alg = "chacha20",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(chacha20_tv_template)
		},
	}, {
		.alg = "cmac(aes)",
		.fips_allowed = 1,
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(aes_cmac128_tv_template)
		}
	}, {
		.alg = "cmac(des3_ede)",
		.fips_allowed = 1,
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(des3_ede_cmac64_tv_template)
		}
	}, {
		.alg = "compress_null",
		.test = alg_test_null,
	}, {
		.alg = "crc32",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(crc32_tv_template)
		}
	}, {
		.alg = "crc32c",
		.test = alg_test_crc32c,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(crc32c_tv_template)
		}
	}, {
		.alg = "crct10dif",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(crct10dif_tv_template)
		}
	}, {
		.alg = "ctr(aes)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(aes_ctr_tv_template)
		}
	}, {
		.alg = "ctr(blowfish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(bf_ctr_tv_template)
		}
	}, {
		.alg = "ctr(camellia)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(camellia_ctr_tv_template)
		}
	}, {
		.alg = "ctr(cast5)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(cast5_ctr_tv_template)
		}
	}, {
		.alg = "ctr(cast6)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(cast6_ctr_tv_template)
		}
	}, {
		.alg = "ctr(des)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(des_ctr_tv_template)
		}
	}, {
		.alg = "ctr(des3_ede)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(des3_ede_ctr_tv_template)
		}
	}, {
		/* Same as ctr(aes) except the key is stored in
		 * hardware secure memory which we reference by index
		 */
		.alg = "ctr(paes)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "ctr(serpent)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(serpent_ctr_tv_template)
		}
	}, {
		.alg = "ctr(twofish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(tf_ctr_tv_template)
		}
	}, {
		.alg = "cts(cbc(aes))",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(cts_mode_tv_template)
		}
	}, {
		.alg = "deflate",
		.test = alg_test_comp,
		.fips_allowed = 1,
		.suite = {
			.comp = {
				.comp = __VECS(deflate_comp_tv_template),
				.decomp = __VECS(deflate_decomp_tv_template)
			}
		}
	}, {
		.alg = "dh",
		.test = alg_test_kpp,
		.fips_allowed = 1,
		.suite = {
			.kpp = __VECS(dh_tv_template)
		}
	}, {
		.alg = "digest_null",
		.test = alg_test_null,
	}, {
		.alg = "drbg_nopr_ctr_aes128",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = __VECS(drbg_nopr_ctr_aes128_tv_template)
		}
	}, {
		.alg = "drbg_nopr_ctr_aes192",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = __VECS(drbg_nopr_ctr_aes192_tv_template)
		}
	}, {
		.alg = "drbg_nopr_ctr_aes256",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = __VECS(drbg_nopr_ctr_aes256_tv_template)
		}
	}, {
		/*
		 * There is no need to specifically test the DRBG with every
		 * backend cipher -- covered by drbg_nopr_hmac_sha256 test
		 */
		.alg = "drbg_nopr_hmac_sha1",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_nopr_hmac_sha256",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = __VECS(drbg_nopr_hmac_sha256_tv_template)
		}
	}, {
		/* covered by drbg_nopr_hmac_sha256 test */
		.alg = "drbg_nopr_hmac_sha384",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_nopr_hmac_sha512",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "drbg_nopr_sha1",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_nopr_sha256",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = __VECS(drbg_nopr_sha256_tv_template)
		}
	}, {
		/* covered by drbg_nopr_sha256 test */
		.alg = "drbg_nopr_sha384",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_nopr_sha512",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_pr_ctr_aes128",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = __VECS(drbg_pr_ctr_aes128_tv_template)
		}
	}, {
		/* covered by drbg_pr_ctr_aes128 test */
		.alg = "drbg_pr_ctr_aes192",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_pr_ctr_aes256",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_pr_hmac_sha1",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_pr_hmac_sha256",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = __VECS(drbg_pr_hmac_sha256_tv_template)
		}
	}, {
		/* covered by drbg_pr_hmac_sha256 test */
		.alg = "drbg_pr_hmac_sha384",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_pr_hmac_sha512",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "drbg_pr_sha1",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_pr_sha256",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = __VECS(drbg_pr_sha256_tv_template)
		}
	}, {
		/* covered by drbg_pr_sha256 test */
		.alg = "drbg_pr_sha384",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_pr_sha512",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "ecb(aes)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(aes_tv_template)
		}
	}, {
		.alg = "ecb(anubis)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(anubis_tv_template)
		}
	}, {
		.alg = "ecb(arc4)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(arc4_tv_template)
		}
	}, {
		.alg = "ecb(blowfish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(bf_tv_template)
		}
	}, {
		.alg = "ecb(camellia)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(camellia_tv_template)
		}
	}, {
		.alg = "ecb(cast5)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(cast5_tv_template)
		}
	}, {
		.alg = "ecb(cast6)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(cast6_tv_template)
		}
	}, {
		.alg = "ecb(cipher_null)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "ecb(des)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(des_tv_template)
		}
	}, {
		.alg = "ecb(des3_ede)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(des3_ede_tv_template)
		}
	}, {
		.alg = "ecb(fcrypt)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.vecs = fcrypt_pcbc_tv_template,
				.count = 1
			}
		}
	}, {
		.alg = "ecb(khazad)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(khazad_tv_template)
		}
	}, {
		/* Same as ecb(aes) except the key is stored in
		 * hardware secure memory which we reference by index
		 */
		.alg = "ecb(paes)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "ecb(seed)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(seed_tv_template)
		}
	}, {
		.alg = "ecb(serpent)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(serpent_tv_template)
		}
	}, {
		.alg = "ecb(sm4)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(sm4_tv_template)
		}
	}, {
		.alg = "ecb(speck128)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(speck128_tv_template)
		}
	}, {
		.alg = "ecb(speck64)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(speck64_tv_template)
		}
	}, {
		.alg = "ecb(tea)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(tea_tv_template)
		}
	}, {
		.alg = "ecb(tnepres)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(tnepres_tv_template)
		}
	}, {
		.alg = "ecb(twofish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(tf_tv_template)
		}
	}, {
		.alg = "ecb(xeta)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(xeta_tv_template)
		}
	}, {
		.alg = "ecb(xtea)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(xtea_tv_template)
		}
	}, {
		.alg = "ecdh",
		.test = alg_test_kpp,
		.fips_allowed = 1,
		.suite = {
			.kpp = __VECS(ecdh_tv_template)
		}
	}, {
		.alg = "gcm(aes)",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = __VECS(aes_gcm_enc_tv_template),
				.dec = __VECS(aes_gcm_dec_tv_template)
			}
		}
	}, {
		.alg = "ghash",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(ghash_tv_template)
		}
	}, {
		.alg = "hmac(md5)",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(hmac_md5_tv_template)
		}
	}, {
		.alg = "hmac(rmd128)",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(hmac_rmd128_tv_template)
		}
	}, {
		.alg = "hmac(rmd160)",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(hmac_rmd160_tv_template)
		}
	}, {
		.alg = "hmac(sha1)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha1_tv_template)
		}
	}, {
		.alg = "hmac(sha224)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha224_tv_template)
		}
	}, {
		.alg = "hmac(sha256)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha256_tv_template)
		}
	}, {
		.alg = "hmac(sha3-224)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha3_224_tv_template)
		}
	}, {
		.alg = "hmac(sha3-256)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha3_256_tv_template)
		}
	}, {
		.alg = "hmac(sha3-384)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha3_384_tv_template)
		}
	}, {
		.alg = "hmac(sha3-512)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha3_512_tv_template)
		}
	}, {
		.alg = "hmac(sha384)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha384_tv_template)
		}
	}, {
		.alg = "hmac(sha512)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha512_tv_template)
		}
	}, {
		.alg = "jitterentropy_rng",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "kw(aes)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(aes_kw_tv_template)
		}
	}, {
		.alg = "lrw(aes)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(aes_lrw_tv_template)
		}
	}, {
		.alg = "lrw(camellia)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(camellia_lrw_tv_template)
		}
	}, {
		.alg = "lrw(cast6)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(cast6_lrw_tv_template)
		}
	}, {
		.alg = "lrw(serpent)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(serpent_lrw_tv_template)
		}
	}, {
		.alg = "lrw(twofish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(tf_lrw_tv_template)
		}
	}, {
		.alg = "lz4",
		.test = alg_test_comp,
		.fips_allowed = 1,
		.suite = {
			.comp = {
				.comp = __VECS(lz4_comp_tv_template),
				.decomp = __VECS(lz4_decomp_tv_template)
			}
		}
	}, {
		.alg = "lz4hc",
		.test = alg_test_comp,
		.fips_allowed = 1,
		.suite = {
			.comp = {
				.comp = __VECS(lz4hc_comp_tv_template),
				.decomp = __VECS(lz4hc_decomp_tv_template)
			}
		}
	}, {
		.alg = "lzo",
		.test = alg_test_comp,
		.fips_allowed = 1,
		.suite = {
			.comp = {
				.comp = __VECS(lzo_comp_tv_template),
				.decomp = __VECS(lzo_decomp_tv_template)
			}
		}
	}, {
		.alg = "md4",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(md4_tv_template)
		}
	}, {
		.alg = "md5",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(md5_tv_template)
		}
	}, {
		.alg = "michael_mic",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(michael_mic_tv_template)
		}
	}, {
		.alg = "morus1280",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = __VECS(morus1280_enc_tv_template),
				.dec = __VECS(morus1280_dec_tv_template),
			}
		}
	}, {
		.alg = "morus640",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = __VECS(morus640_enc_tv_template),
				.dec = __VECS(morus640_dec_tv_template),
			}
		}
	}, {
		.alg = "ofb(aes)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(aes_ofb_tv_template)
		}
	}, {
		/* Same as ofb(aes) except the key is stored in
		 * hardware secure memory which we reference by index
		 */
		.alg = "ofb(paes)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "pcbc(fcrypt)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(fcrypt_pcbc_tv_template)
		}
	}, {
		.alg = "pkcs1pad(rsa,sha224)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "pkcs1pad(rsa,sha256)",
		.test = alg_test_akcipher,
		.fips_allowed = 1,
		.suite = {
			.akcipher = __VECS(pkcs1pad_rsa_tv_template)
		}
	}, {
		.alg = "pkcs1pad(rsa,sha384)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "pkcs1pad(rsa,sha512)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "poly1305",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(poly1305_tv_template)
		}
	}, {
		.alg = "rfc3686(ctr(aes))",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(aes_ctr_rfc3686_tv_template)
		}
	}, {
		.alg = "rfc4106(gcm(aes))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = __VECS(aes_gcm_rfc4106_enc_tv_template),
				.dec = __VECS(aes_gcm_rfc4106_dec_tv_template)
			}
		}
	}, {
		.alg = "rfc4309(ccm(aes))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = __VECS(aes_ccm_rfc4309_enc_tv_template),
				.dec = __VECS(aes_ccm_rfc4309_dec_tv_template)
			}
		}
	}, {
		.alg = "rfc4543(gcm(aes))",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = __VECS(aes_gcm_rfc4543_enc_tv_template),
				.dec = __VECS(aes_gcm_rfc4543_dec_tv_template),
			}
		}
	}, {
		.alg = "rfc7539(chacha20,poly1305)",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = __VECS(rfc7539_enc_tv_template),
				.dec = __VECS(rfc7539_dec_tv_template),
			}
		}
	}, {
		.alg = "rfc7539esp(chacha20,poly1305)",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = __VECS(rfc7539esp_enc_tv_template),
				.dec = __VECS(rfc7539esp_dec_tv_template),
			}
		}
	}, {
		.alg = "rmd128",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(rmd128_tv_template)
		}
	}, {
		.alg = "rmd160",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(rmd160_tv_template)
		}
	}, {
		.alg = "rmd256",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(rmd256_tv_template)
		}
	}, {
		.alg = "rmd320",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(rmd320_tv_template)
		}
	}, {
		.alg = "rsa",
		.test = alg_test_akcipher,
		.fips_allowed = 1,
		.suite = {
			.akcipher = __VECS(rsa_tv_template)
		}
	}, {
		.alg = "salsa20",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(salsa20_stream_tv_template)
		}
	}, {
		.alg = "sha1",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha1_tv_template)
		}
	}, {
		.alg = "sha224",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha224_tv_template)
		}
	}, {
		.alg = "sha256",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha256_tv_template)
		}
	}, {
		.alg = "sha3-224",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha3_224_tv_template)
		}
	}, {
		.alg = "sha3-256",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha3_256_tv_template)
		}
	}, {
		.alg = "sha3-384",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha3_384_tv_template)
		}
	}, {
		.alg = "sha3-512",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha3_512_tv_template)
		}
	}, {
		.alg = "sha384",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha384_tv_template)
		}
	}, {
		.alg = "sha512",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha512_tv_template)
		}
	}, {
		.alg = "sm3",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(sm3_tv_template)
		}
	}, {
		.alg = "tgr128",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(tgr128_tv_template)
		}
	}, {
		.alg = "tgr160",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(tgr160_tv_template)
		}
	}, {
		.alg = "tgr192",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(tgr192_tv_template)
		}
	}, {
		.alg = "vmac(aes)",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(aes_vmac128_tv_template)
		}
	}, {
		.alg = "wp256",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(wp256_tv_template)
		}
	}, {
		.alg = "wp384",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(wp384_tv_template)
		}
	}, {
		.alg = "wp512",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(wp512_tv_template)
		}
	}, {
		.alg = "xcbc(aes)",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(aes_xcbc128_tv_template)
		}
	}, {
		.alg = "xts(aes)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(aes_xts_tv_template)
		}
	}, {
		.alg = "xts(camellia)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(camellia_xts_tv_template)
		}
	}, {
		.alg = "xts(cast6)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(cast6_xts_tv_template)
		}
	}, {
		/* Same as xts(aes) except the key is stored in
		 * hardware secure memory which we reference by index
		 */
		.alg = "xts(paes)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "xts(serpent)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(serpent_xts_tv_template)
		}
	}, {
		.alg = "xts(speck128)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(speck128_xts_tv_template)
		}
	}, {
		.alg = "xts(speck64)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(speck64_xts_tv_template)
		}
	}, {
		.alg = "xts(twofish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(tf_xts_tv_template)
		}
	}, {
		.alg = "xts4096(paes)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "xts512(paes)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "zlib-deflate",
		.test = alg_test_comp,
		.fips_allowed = 1,
		.suite = {
			.comp = {
				.comp = __VECS(zlib_deflate_comp_tv_template),
				.decomp = __VECS(zlib_deflate_decomp_tv_template)
			}
		}
	}, {
		.alg = "zstd",
		.test = alg_test_comp,
		.fips_allowed = 1,
		.suite = {
			.comp = {
				.comp = __VECS(zstd_comp_tv_template),
				.decomp = __VECS(zstd_decomp_tv_template)
			}
		}
	}
};

static bool alg_test_descs_checked;

static void alg_test_descs_check_order(void)
{
	int i;

	/* only check once */
	if (alg_test_descs_checked)
		return;

	alg_test_descs_checked = true;

	for (i = 1; i < ARRAY_SIZE(alg_test_descs); i++) {
		int diff = strcmp(alg_test_descs[i - 1].alg,
				  alg_test_descs[i].alg);

		if (WARN_ON(diff > 0)) {
			pr_warn("testmgr: alg_test_descs entries in wrong order: '%s' before '%s'\n",
				alg_test_descs[i - 1].alg,
				alg_test_descs[i].alg);
		}

		if (WARN_ON(diff == 0)) {
			pr_warn("testmgr: duplicate alg_test_descs entry: '%s'\n",
				alg_test_descs[i].alg);
		}
	}
}

static int alg_find_test(const char *alg)
{
	int start = 0;
	int end = ARRAY_SIZE(alg_test_descs);

	while (start < end) {
		int i = (start + end) / 2;
		int diff = strcmp(alg_test_descs[i].alg, alg);

		if (diff > 0) {
			end = i;
			continue;
		}

		if (diff < 0) {
			start = i + 1;
			continue;
		}

		return i;
	}

	return -1;
}

int alg_test(const char *driver, const char *alg, u32 type, u32 mask)
{
	int i;
	int j;
	int rc;

	if (!fips_enabled && notests) {
		printk_once(KERN_INFO "alg: self-tests disabled\n");
		return 0;
	}

	alg_test_descs_check_order();

	if ((type & CRYPTO_ALG_TYPE_MASK) == CRYPTO_ALG_TYPE_CIPHER) {
		char nalg[CRYPTO_MAX_ALG_NAME];

		if (snprintf(nalg, sizeof(nalg), "ecb(%s)", alg) >=
		    sizeof(nalg))
			return -ENAMETOOLONG;

		i = alg_find_test(nalg);
		if (i < 0)
			goto notest;

		if (fips_enabled && !alg_test_descs[i].fips_allowed)
			goto non_fips_alg;

		rc = alg_test_cipher(alg_test_descs + i, driver, type, mask);
		goto test_done;
	}

	i = alg_find_test(alg);
	j = alg_find_test(driver);
	if (i < 0 && j < 0)
		goto notest;

	if (fips_enabled && ((i >= 0 && !alg_test_descs[i].fips_allowed) ||
			     (j >= 0 && !alg_test_descs[j].fips_allowed)))
		goto non_fips_alg;

	rc = 0;
	if (i >= 0)
		rc |= alg_test_descs[i].test(alg_test_descs + i, driver,
					     type, mask);
	if (j >= 0 && j != i)
		rc |= alg_test_descs[j].test(alg_test_descs + j, driver,
					     type, mask);

test_done:
	if (fips_enabled && rc)
		panic("%s: %s alg self test failed in fips mode!\n", driver, alg);

	if (fips_enabled && !rc)
		pr_info("alg: self-tests for %s (%s) passed\n", driver, alg);

	return rc;

notest:
	printk(KERN_INFO "alg: No test for %s (%s)\n", alg, driver);
	return 0;
non_fips_alg:
	return -EINVAL;
}

#endif /* CONFIG_CRYPTO_MANAGER_DISABLE_TESTS */

EXPORT_SYMBOL_GPL(alg_test);
