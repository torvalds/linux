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

#include "internal.h"

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
#define IDX3		1
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

struct tcrypt_result {
	struct completion completion;
	int err;
};

struct aead_test_suite {
	struct {
		struct aead_testvec *vecs;
		unsigned int count;
	} enc, dec;
};

struct cipher_test_suite {
	struct {
		struct cipher_testvec *vecs;
		unsigned int count;
	} enc, dec;
};

struct comp_test_suite {
	struct {
		struct comp_testvec *vecs;
		unsigned int count;
	} comp, decomp;
};

struct hash_test_suite {
	struct hash_testvec *vecs;
	unsigned int count;
};

struct cprng_test_suite {
	struct cprng_testvec *vecs;
	unsigned int count;
};

struct drbg_test_suite {
	struct drbg_testvec *vecs;
	unsigned int count;
};

struct akcipher_test_suite {
	struct akcipher_testvec *vecs;
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
	} suite;
};

static unsigned int IDX[8] = { IDX1, IDX2, IDX3, IDX4, IDX5, IDX6, IDX7, IDX8 };

static void hexdump(unsigned char *buf, unsigned int len)
{
	print_hex_dump(KERN_CONT, "", DUMP_PREFIX_OFFSET,
			16, 1,
			buf, len, false);
}

static void tcrypt_complete(struct crypto_async_request *req, int err)
{
	struct tcrypt_result *res = req->data;

	if (err == -EINPROGRESS)
		return;

	res->err = err;
	complete(&res->completion);
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

static int wait_async_op(struct tcrypt_result *tr, int ret)
{
	if (ret == -EINPROGRESS || ret == -EBUSY) {
		wait_for_completion(&tr->completion);
		reinit_completion(&tr->completion);
		ret = tr->err;
	}
	return ret;
}

static int ahash_partial_update(struct ahash_request **preq,
	struct crypto_ahash *tfm, struct hash_testvec *template,
	void *hash_buff, int k, int temp, struct scatterlist *sg,
	const char *algo, char *result, struct tcrypt_result *tresult)
{
	char *state;
	struct ahash_request *req;
	int statesize, ret = -EINVAL;

	req = *preq;
	statesize = crypto_ahash_statesize(
			crypto_ahash_reqtfm(req));
	state = kmalloc(statesize, GFP_KERNEL);
	if (!state) {
		pr_err("alt: hash: Failed to alloc state for %s\n", algo);
		goto out_nostate;
	}
	ret = crypto_ahash_export(req, state);
	if (ret) {
		pr_err("alt: hash: Failed to export() for %s\n", algo);
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
		tcrypt_complete, tresult);

	memcpy(hash_buff, template->plaintext + temp,
		template->tap[k]);
	sg_init_one(&sg[0], hash_buff, template->tap[k]);
	ahash_request_set_crypt(req, sg, result, template->tap[k]);
	ret = crypto_ahash_import(req, state);
	if (ret) {
		pr_err("alg: hash: Failed to import() for %s\n", algo);
		goto out;
	}
	ret = wait_async_op(tresult, crypto_ahash_update(req));
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

static int __test_hash(struct crypto_ahash *tfm, struct hash_testvec *template,
		       unsigned int tcount, bool use_digest,
		       const int align_offset)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_ahash_tfm(tfm));
	unsigned int i, j, k, temp;
	struct scatterlist sg[8];
	char *result;
	char *key;
	struct ahash_request *req;
	struct tcrypt_result tresult;
	void *hash_buff;
	char *xbuf[XBUFSIZE];
	int ret = -ENOMEM;

	result = kmalloc(MAX_DIGEST_SIZE, GFP_KERNEL);
	if (!result)
		return ret;
	key = kmalloc(MAX_KEYLEN, GFP_KERNEL);
	if (!key)
		goto out_nobuf;
	if (testmgr_alloc_buf(xbuf))
		goto out_nobuf;

	init_completion(&tresult.completion);

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		printk(KERN_ERR "alg: hash: Failed to allocate request for "
		       "%s\n", algo);
		goto out_noreq;
	}
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   tcrypt_complete, &tresult);

	j = 0;
	for (i = 0; i < tcount; i++) {
		if (template[i].np)
			continue;

		ret = -EINVAL;
		if (WARN_ON(align_offset + template[i].psize > PAGE_SIZE))
			goto out;

		j++;
		memset(result, 0, MAX_DIGEST_SIZE);

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
			ret = wait_async_op(&tresult, crypto_ahash_digest(req));
			if (ret) {
				pr_err("alg: hash: digest failed on test %d "
				       "for %s: ret=%d\n", j, algo, -ret);
				goto out;
			}
		} else {
			ret = wait_async_op(&tresult, crypto_ahash_init(req));
			if (ret) {
				pr_err("alt: hash: init failed on test %d "
				       "for %s: ret=%d\n", j, algo, -ret);
				goto out;
			}
			ret = wait_async_op(&tresult, crypto_ahash_update(req));
			if (ret) {
				pr_err("alt: hash: update failed on test %d "
				       "for %s: ret=%d\n", j, algo, -ret);
				goto out;
			}
			ret = wait_async_op(&tresult, crypto_ahash_final(req));
			if (ret) {
				pr_err("alt: hash: final failed on test %d "
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
		memset(result, 0, MAX_DIGEST_SIZE);

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
		ret = crypto_ahash_digest(req);
		switch (ret) {
		case 0:
			break;
		case -EINPROGRESS:
		case -EBUSY:
			wait_for_completion(&tresult.completion);
			reinit_completion(&tresult.completion);
			ret = tresult.err;
			if (!ret)
				break;
			/* fall through */
		default:
			printk(KERN_ERR "alg: hash: digest failed "
			       "on chunking test %d for %s: "
			       "ret=%d\n", j, algo, -ret);
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
		memset(result, 0, MAX_DIGEST_SIZE);

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
		ret = wait_async_op(&tresult, crypto_ahash_init(req));
		if (ret) {
			pr_err("alt: hash: init failed on test %d for %s: ret=%d\n",
				j, algo, -ret);
			goto out;
		}
		ret = wait_async_op(&tresult, crypto_ahash_update(req));
		if (ret) {
			pr_err("alt: hash: update failed on test %d for %s: ret=%d\n",
				j, algo, -ret);
			goto out;
		}

		temp = template[i].tap[0];
		for (k = 1; k < template[i].np; k++) {
			ret = ahash_partial_update(&req, tfm, &template[i],
				hash_buff, k, temp, &sg[0], algo, result,
				&tresult);
			if (ret) {
				pr_err("hash: partial update failed on test %d for %s: ret=%d\n",
					j, algo, -ret);
				goto out_noreq;
			}
			temp += template[i].tap[k];
		}
		ret = wait_async_op(&tresult, crypto_ahash_final(req));
		if (ret) {
			pr_err("alt: hash: final failed on test %d for %s: ret=%d\n",
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

static int test_hash(struct crypto_ahash *tfm, struct hash_testvec *template,
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
		       struct aead_testvec *template, unsigned int tcount,
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
	struct tcrypt_result result;
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
	sg = kmalloc(sizeof(*sg) * 8 * (diff_dst ? 4 : 2), GFP_KERNEL);
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

	init_completion(&result.completion);

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("alg: aead%s: Failed to allocate request for %s\n",
		       d, algo);
		goto out;
	}

	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  tcrypt_complete, &result);

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
		if (!ret == template[i].fail) {
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

		ret = enc ? crypto_aead_encrypt(req) : crypto_aead_decrypt(req);

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
		case -EINPROGRESS:
		case -EBUSY:
			wait_for_completion(&result.completion);
			reinit_completion(&result.completion);
			ret = result.err;
			if (!ret)
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
		if (!ret == template[i].fail) {
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

		ret = enc ? crypto_aead_encrypt(req) : crypto_aead_decrypt(req);

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
		case -EINPROGRESS:
		case -EBUSY:
			wait_for_completion(&result.completion);
			reinit_completion(&result.completion);
			ret = result.err;
			if (!ret)
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
		     struct aead_testvec *template, unsigned int tcount)
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
		       struct cipher_testvec *template, unsigned int tcount)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_cipher_tfm(tfm));
	unsigned int i, j, k;
	char *q;
	const char *e;
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

		j++;

		ret = -EINVAL;
		if (WARN_ON(template[i].ilen > PAGE_SIZE))
			goto out;

		data = xbuf[0];
		memcpy(data, template[i].input, template[i].ilen);

		crypto_cipher_clear_flags(tfm, ~0);
		if (template[i].wk)
			crypto_cipher_set_flags(tfm, CRYPTO_TFM_REQ_WEAK_KEY);

		ret = crypto_cipher_setkey(tfm, template[i].key,
					   template[i].klen);
		if (!ret == template[i].fail) {
			printk(KERN_ERR "alg: cipher: setkey failed "
			       "on test %d for %s: flags=%x\n", j,
			       algo, crypto_cipher_get_flags(tfm));
			goto out;
		} else if (ret)
			continue;

		for (k = 0; k < template[i].ilen;
		     k += crypto_cipher_blocksize(tfm)) {
			if (enc)
				crypto_cipher_encrypt_one(tfm, data + k,
							  data + k);
			else
				crypto_cipher_decrypt_one(tfm, data + k,
							  data + k);
		}

		q = data;
		if (memcmp(q, template[i].result, template[i].rlen)) {
			printk(KERN_ERR "alg: cipher: Test %d failed "
			       "on %s for %s\n", j, e, algo);
			hexdump(q, template[i].rlen);
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
			   struct cipher_testvec *template, unsigned int tcount,
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
	struct tcrypt_result result;
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

	init_completion(&result.completion);

	req = skcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("alg: skcipher%s: Failed to allocate request for %s\n",
		       d, algo);
		goto out;
	}

	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				      tcrypt_complete, &result);

	j = 0;
	for (i = 0; i < tcount; i++) {
		if (template[i].np && !template[i].also_non_np)
			continue;

		if (template[i].iv)
			memcpy(iv, template[i].iv, ivsize);
		else
			memset(iv, 0, MAX_IVLEN);

		j++;
		ret = -EINVAL;
		if (WARN_ON(align_offset + template[i].ilen > PAGE_SIZE))
			goto out;

		data = xbuf[0];
		data += align_offset;
		memcpy(data, template[i].input, template[i].ilen);

		crypto_skcipher_clear_flags(tfm, ~0);
		if (template[i].wk)
			crypto_skcipher_set_flags(tfm,
						  CRYPTO_TFM_REQ_WEAK_KEY);

		ret = crypto_skcipher_setkey(tfm, template[i].key,
					     template[i].klen);
		if (!ret == template[i].fail) {
			pr_err("alg: skcipher%s: setkey failed on test %d for %s: flags=%x\n",
			       d, j, algo, crypto_skcipher_get_flags(tfm));
			goto out;
		} else if (ret)
			continue;

		sg_init_one(&sg[0], data, template[i].ilen);
		if (diff_dst) {
			data = xoutbuf[0];
			data += align_offset;
			sg_init_one(&sgout[0], data, template[i].ilen);
		}

		skcipher_request_set_crypt(req, sg, (diff_dst) ? sgout : sg,
					   template[i].ilen, iv);
		ret = enc ? crypto_skcipher_encrypt(req) :
			    crypto_skcipher_decrypt(req);

		switch (ret) {
		case 0:
			break;
		case -EINPROGRESS:
		case -EBUSY:
			wait_for_completion(&result.completion);
			reinit_completion(&result.completion);
			ret = result.err;
			if (!ret)
				break;
			/* fall through */
		default:
			pr_err("alg: skcipher%s: %s failed on test %d for %s: ret=%d\n",
			       d, e, j, algo, -ret);
			goto out;
		}

		q = data;
		if (memcmp(q, template[i].result, template[i].rlen)) {
			pr_err("alg: skcipher%s: Test %d failed (invalid result) on %s for %s\n",
			       d, j, e, algo);
			hexdump(q, template[i].rlen);
			ret = -EINVAL;
			goto out;
		}

		if (template[i].iv_out &&
		    memcmp(iv, template[i].iv_out,
			   crypto_skcipher_ivsize(tfm))) {
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

		if (template[i].iv)
			memcpy(iv, template[i].iv, ivsize);
		else
			memset(iv, 0, MAX_IVLEN);

		j++;
		crypto_skcipher_clear_flags(tfm, ~0);
		if (template[i].wk)
			crypto_skcipher_set_flags(tfm,
						  CRYPTO_TFM_REQ_WEAK_KEY);

		ret = crypto_skcipher_setkey(tfm, template[i].key,
					     template[i].klen);
		if (!ret == template[i].fail) {
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

			memcpy(q, template[i].input + temp, template[i].tap[k]);

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
					   template[i].ilen, iv);

		ret = enc ? crypto_skcipher_encrypt(req) :
			    crypto_skcipher_decrypt(req);

		switch (ret) {
		case 0:
			break;
		case -EINPROGRESS:
		case -EBUSY:
			wait_for_completion(&result.completion);
			reinit_completion(&result.completion);
			ret = result.err;
			if (!ret)
				break;
			/* fall through */
		default:
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

			if (memcmp(q, template[i].result + temp,
				   template[i].tap[k])) {
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
			 struct cipher_testvec *template, unsigned int tcount)
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

static int test_comp(struct crypto_comp *tfm, struct comp_testvec *ctemplate,
		     struct comp_testvec *dtemplate, int ctcount, int dtcount)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_comp_tfm(tfm));
	unsigned int i;
	char result[COMP_BUF_SIZE];
	int ret;

	for (i = 0; i < ctcount; i++) {
		int ilen;
		unsigned int dlen = COMP_BUF_SIZE;

		memset(result, 0, sizeof (result));

		ilen = ctemplate[i].inlen;
		ret = crypto_comp_compress(tfm, ctemplate[i].input,
		                           ilen, result, &dlen);
		if (ret) {
			printk(KERN_ERR "alg: comp: compression failed "
			       "on test %d for %s: ret=%d\n", i + 1, algo,
			       -ret);
			goto out;
		}

		if (dlen != ctemplate[i].outlen) {
			printk(KERN_ERR "alg: comp: Compression test %d "
			       "failed for %s: output len = %d\n", i + 1, algo,
			       dlen);
			ret = -EINVAL;
			goto out;
		}

		if (memcmp(result, ctemplate[i].output, dlen)) {
			printk(KERN_ERR "alg: comp: Compression test %d "
			       "failed for %s\n", i + 1, algo);
			hexdump(result, dlen);
			ret = -EINVAL;
			goto out;
		}
	}

	for (i = 0; i < dtcount; i++) {
		int ilen;
		unsigned int dlen = COMP_BUF_SIZE;

		memset(result, 0, sizeof (result));

		ilen = dtemplate[i].inlen;
		ret = crypto_comp_decompress(tfm, dtemplate[i].input,
		                             ilen, result, &dlen);
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

		if (memcmp(result, dtemplate[i].output, dlen)) {
			printk(KERN_ERR "alg: comp: Decompression test %d "
			       "failed for %s\n", i + 1, algo);
			hexdump(result, dlen);
			ret = -EINVAL;
			goto out;
		}
	}

	ret = 0;

out:
	return ret;
}

static int test_cprng(struct crypto_rng *tfm, struct cprng_testvec *template,
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

	tfm = crypto_alloc_aead(driver, type | CRYPTO_ALG_INTERNAL, mask);
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
	struct crypto_cipher *tfm;
	int err = 0;

	tfm = crypto_alloc_cipher(driver, type | CRYPTO_ALG_INTERNAL, mask);
	if (IS_ERR(tfm)) {
		printk(KERN_ERR "alg: cipher: Failed to load transform for "
		       "%s: %ld\n", driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	if (desc->suite.cipher.enc.vecs) {
		err = test_cipher(tfm, ENCRYPT, desc->suite.cipher.enc.vecs,
				  desc->suite.cipher.enc.count);
		if (err)
			goto out;
	}

	if (desc->suite.cipher.dec.vecs)
		err = test_cipher(tfm, DECRYPT, desc->suite.cipher.dec.vecs,
				  desc->suite.cipher.dec.count);

out:
	crypto_free_cipher(tfm);
	return err;
}

static int alg_test_skcipher(const struct alg_test_desc *desc,
			     const char *driver, u32 type, u32 mask)
{
	struct crypto_skcipher *tfm;
	int err = 0;

	tfm = crypto_alloc_skcipher(driver, type | CRYPTO_ALG_INTERNAL, mask);
	if (IS_ERR(tfm)) {
		printk(KERN_ERR "alg: skcipher: Failed to load transform for "
		       "%s: %ld\n", driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	if (desc->suite.cipher.enc.vecs) {
		err = test_skcipher(tfm, ENCRYPT, desc->suite.cipher.enc.vecs,
				    desc->suite.cipher.enc.count);
		if (err)
			goto out;
	}

	if (desc->suite.cipher.dec.vecs)
		err = test_skcipher(tfm, DECRYPT, desc->suite.cipher.dec.vecs,
				    desc->suite.cipher.dec.count);

out:
	crypto_free_skcipher(tfm);
	return err;
}

static int alg_test_comp(const struct alg_test_desc *desc, const char *driver,
			 u32 type, u32 mask)
{
	struct crypto_comp *tfm;
	int err;

	tfm = crypto_alloc_comp(driver, type, mask);
	if (IS_ERR(tfm)) {
		printk(KERN_ERR "alg: comp: Failed to load transform for %s: "
		       "%ld\n", driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	err = test_comp(tfm, desc->suite.comp.comp.vecs,
			desc->suite.comp.decomp.vecs,
			desc->suite.comp.comp.count,
			desc->suite.comp.decomp.count);

	crypto_free_comp(tfm);
	return err;
}

static int alg_test_hash(const struct alg_test_desc *desc, const char *driver,
			 u32 type, u32 mask)
{
	struct crypto_ahash *tfm;
	int err;

	tfm = crypto_alloc_ahash(driver, type | CRYPTO_ALG_INTERNAL, mask);
	if (IS_ERR(tfm)) {
		printk(KERN_ERR "alg: hash: Failed to load transform for %s: "
		       "%ld\n", driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	err = test_hash(tfm, desc->suite.hash.vecs,
			desc->suite.hash.count, true);
	if (!err)
		err = test_hash(tfm, desc->suite.hash.vecs,
				desc->suite.hash.count, false);

	crypto_free_ahash(tfm);
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

	tfm = crypto_alloc_shash(driver, type | CRYPTO_ALG_INTERNAL, mask);
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

	rng = crypto_alloc_rng(driver, type | CRYPTO_ALG_INTERNAL, mask);
	if (IS_ERR(rng)) {
		printk(KERN_ERR "alg: cprng: Failed to load transform for %s: "
		       "%ld\n", driver, PTR_ERR(rng));
		return PTR_ERR(rng);
	}

	err = test_cprng(rng, desc->suite.cprng.vecs, desc->suite.cprng.count);

	crypto_free_rng(rng);

	return err;
}


static int drbg_cavs_test(struct drbg_testvec *test, int pr,
			  const char *driver, u32 type, u32 mask)
{
	int ret = -EAGAIN;
	struct crypto_rng *drng;
	struct drbg_test_data test_data;
	struct drbg_string addtl, pers, testentropy;
	unsigned char *buf = kzalloc(test->expectedlen, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	drng = crypto_alloc_rng(driver, type | CRYPTO_ALG_INTERNAL, mask);
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
	struct drbg_testvec *template = desc->suite.drbg.vecs;
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

static int do_test_rsa(struct crypto_akcipher *tfm,
		       struct akcipher_testvec *vecs)
{
	struct akcipher_request *req;
	void *outbuf_enc = NULL;
	void *outbuf_dec = NULL;
	struct tcrypt_result result;
	unsigned int out_len_max, out_len = 0;
	int err = -ENOMEM;
	struct scatterlist src, dst, src_tab[2];

	req = akcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		return err;

	init_completion(&result.completion);

	if (vecs->public_key_vec)
		err = crypto_akcipher_set_pub_key(tfm, vecs->key,
						  vecs->key_len);
	else
		err = crypto_akcipher_set_priv_key(tfm, vecs->key,
						   vecs->key_len);
	if (err)
		goto free_req;

	out_len_max = crypto_akcipher_maxsize(tfm);
	outbuf_enc = kzalloc(out_len_max, GFP_KERNEL);
	if (!outbuf_enc)
		goto free_req;

	sg_init_table(src_tab, 2);
	sg_set_buf(&src_tab[0], vecs->m, 8);
	sg_set_buf(&src_tab[1], vecs->m + 8, vecs->m_size - 8);
	sg_init_one(&dst, outbuf_enc, out_len_max);
	akcipher_request_set_crypt(req, src_tab, &dst, vecs->m_size,
				   out_len_max);
	akcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				      tcrypt_complete, &result);

	/* Run RSA encrypt - c = m^e mod n;*/
	err = wait_async_op(&result, crypto_akcipher_encrypt(req));
	if (err) {
		pr_err("alg: rsa: encrypt test failed. err %d\n", err);
		goto free_all;
	}
	if (req->dst_len != vecs->c_size) {
		pr_err("alg: rsa: encrypt test failed. Invalid output len\n");
		err = -EINVAL;
		goto free_all;
	}
	/* verify that encrypted message is equal to expected */
	if (memcmp(vecs->c, sg_virt(req->dst), vecs->c_size)) {
		pr_err("alg: rsa: encrypt test failed. Invalid output\n");
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
	sg_init_one(&src, vecs->c, vecs->c_size);
	sg_init_one(&dst, outbuf_dec, out_len_max);
	init_completion(&result.completion);
	akcipher_request_set_crypt(req, &src, &dst, vecs->c_size, out_len_max);

	/* Run RSA decrypt - m = c^d mod n;*/
	err = wait_async_op(&result, crypto_akcipher_decrypt(req));
	if (err) {
		pr_err("alg: rsa: decrypt test failed. err %d\n", err);
		goto free_all;
	}
	out_len = req->dst_len;
	if (out_len != vecs->m_size) {
		pr_err("alg: rsa: decrypt test failed. Invalid output len\n");
		err = -EINVAL;
		goto free_all;
	}
	/* verify that decrypted message is equal to the original msg */
	if (memcmp(vecs->m, outbuf_dec, vecs->m_size)) {
		pr_err("alg: rsa: decrypt test failed. Invalid output\n");
		err = -EINVAL;
	}
free_all:
	kfree(outbuf_dec);
	kfree(outbuf_enc);
free_req:
	akcipher_request_free(req);
	return err;
}

static int test_rsa(struct crypto_akcipher *tfm, struct akcipher_testvec *vecs,
		    unsigned int tcount)
{
	int ret, i;

	for (i = 0; i < tcount; i++) {
		ret = do_test_rsa(tfm, vecs++);
		if (ret) {
			pr_err("alg: rsa: test failed on vector %d, err=%d\n",
			       i + 1, ret);
			return ret;
		}
	}
	return 0;
}

static int test_akcipher(struct crypto_akcipher *tfm, const char *alg,
			 struct akcipher_testvec *vecs, unsigned int tcount)
{
	if (strncmp(alg, "rsa", 3) == 0)
		return test_rsa(tfm, vecs, tcount);

	return 0;
}

static int alg_test_akcipher(const struct alg_test_desc *desc,
			     const char *driver, u32 type, u32 mask)
{
	struct crypto_akcipher *tfm;
	int err = 0;

	tfm = crypto_alloc_akcipher(driver, type | CRYPTO_ALG_INTERNAL, mask);
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

/* Please keep this list sorted by algorithm name. */
static const struct alg_test_desc alg_test_descs[] = {
	{
		.alg = "__cbc-cast5-avx",
		.test = alg_test_null,
	}, {
		.alg = "__cbc-cast6-avx",
		.test = alg_test_null,
	}, {
		.alg = "__cbc-serpent-avx",
		.test = alg_test_null,
	}, {
		.alg = "__cbc-serpent-avx2",
		.test = alg_test_null,
	}, {
		.alg = "__cbc-serpent-sse2",
		.test = alg_test_null,
	}, {
		.alg = "__cbc-twofish-avx",
		.test = alg_test_null,
	}, {
		.alg = "__driver-cbc-aes-aesni",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "__driver-cbc-camellia-aesni",
		.test = alg_test_null,
	}, {
		.alg = "__driver-cbc-camellia-aesni-avx2",
		.test = alg_test_null,
	}, {
		.alg = "__driver-cbc-cast5-avx",
		.test = alg_test_null,
	}, {
		.alg = "__driver-cbc-cast6-avx",
		.test = alg_test_null,
	}, {
		.alg = "__driver-cbc-serpent-avx",
		.test = alg_test_null,
	}, {
		.alg = "__driver-cbc-serpent-avx2",
		.test = alg_test_null,
	}, {
		.alg = "__driver-cbc-serpent-sse2",
		.test = alg_test_null,
	}, {
		.alg = "__driver-cbc-twofish-avx",
		.test = alg_test_null,
	}, {
		.alg = "__driver-ecb-aes-aesni",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "__driver-ecb-camellia-aesni",
		.test = alg_test_null,
	}, {
		.alg = "__driver-ecb-camellia-aesni-avx2",
		.test = alg_test_null,
	}, {
		.alg = "__driver-ecb-cast5-avx",
		.test = alg_test_null,
	}, {
		.alg = "__driver-ecb-cast6-avx",
		.test = alg_test_null,
	}, {
		.alg = "__driver-ecb-serpent-avx",
		.test = alg_test_null,
	}, {
		.alg = "__driver-ecb-serpent-avx2",
		.test = alg_test_null,
	}, {
		.alg = "__driver-ecb-serpent-sse2",
		.test = alg_test_null,
	}, {
		.alg = "__driver-ecb-twofish-avx",
		.test = alg_test_null,
	}, {
		.alg = "__driver-gcm-aes-aesni",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "__ghash-pclmulqdqni",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "ansi_cprng",
		.test = alg_test_cprng,
		.suite = {
			.cprng = {
				.vecs = ansi_cprng_aes_tv_template,
				.count = ANSI_CPRNG_AES_TEST_VECTORS
			}
		}
	}, {
		.alg = "authenc(hmac(md5),ecb(cipher_null))",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = {
					.vecs = hmac_md5_ecb_cipher_null_enc_tv_template,
					.count = HMAC_MD5_ECB_CIPHER_NULL_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = hmac_md5_ecb_cipher_null_dec_tv_template,
					.count = HMAC_MD5_ECB_CIPHER_NULL_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "authenc(hmac(sha1),cbc(aes))",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = {
					.vecs =
					hmac_sha1_aes_cbc_enc_tv_temp,
					.count =
					HMAC_SHA1_AES_CBC_ENC_TEST_VEC
				}
			}
		}
	}, {
		.alg = "authenc(hmac(sha1),cbc(des))",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = {
					.vecs =
					hmac_sha1_des_cbc_enc_tv_temp,
					.count =
					HMAC_SHA1_DES_CBC_ENC_TEST_VEC
				}
			}
		}
	}, {
		.alg = "authenc(hmac(sha1),cbc(des3_ede))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = {
					.vecs =
					hmac_sha1_des3_ede_cbc_enc_tv_temp,
					.count =
					HMAC_SHA1_DES3_EDE_CBC_ENC_TEST_VEC
				}
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
				.enc = {
					.vecs =
					hmac_sha1_ecb_cipher_null_enc_tv_temp,
					.count =
					HMAC_SHA1_ECB_CIPHER_NULL_ENC_TEST_VEC
				},
				.dec = {
					.vecs =
					hmac_sha1_ecb_cipher_null_dec_tv_temp,
					.count =
					HMAC_SHA1_ECB_CIPHER_NULL_DEC_TEST_VEC
				}
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
				.enc = {
					.vecs =
					hmac_sha224_des_cbc_enc_tv_temp,
					.count =
					HMAC_SHA224_DES_CBC_ENC_TEST_VEC
				}
			}
		}
	}, {
		.alg = "authenc(hmac(sha224),cbc(des3_ede))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = {
					.vecs =
					hmac_sha224_des3_ede_cbc_enc_tv_temp,
					.count =
					HMAC_SHA224_DES3_EDE_CBC_ENC_TEST_VEC
				}
			}
		}
	}, {
		.alg = "authenc(hmac(sha256),cbc(aes))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = {
					.vecs =
					hmac_sha256_aes_cbc_enc_tv_temp,
					.count =
					HMAC_SHA256_AES_CBC_ENC_TEST_VEC
				}
			}
		}
	}, {
		.alg = "authenc(hmac(sha256),cbc(des))",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = {
					.vecs =
					hmac_sha256_des_cbc_enc_tv_temp,
					.count =
					HMAC_SHA256_DES_CBC_ENC_TEST_VEC
				}
			}
		}
	}, {
		.alg = "authenc(hmac(sha256),cbc(des3_ede))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = {
					.vecs =
					hmac_sha256_des3_ede_cbc_enc_tv_temp,
					.count =
					HMAC_SHA256_DES3_EDE_CBC_ENC_TEST_VEC
				}
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
				.enc = {
					.vecs =
					hmac_sha384_des_cbc_enc_tv_temp,
					.count =
					HMAC_SHA384_DES_CBC_ENC_TEST_VEC
				}
			}
		}
	}, {
		.alg = "authenc(hmac(sha384),cbc(des3_ede))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = {
					.vecs =
					hmac_sha384_des3_ede_cbc_enc_tv_temp,
					.count =
					HMAC_SHA384_DES3_EDE_CBC_ENC_TEST_VEC
				}
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
				.enc = {
					.vecs =
					hmac_sha512_aes_cbc_enc_tv_temp,
					.count =
					HMAC_SHA512_AES_CBC_ENC_TEST_VEC
				}
			}
		}
	}, {
		.alg = "authenc(hmac(sha512),cbc(des))",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = {
					.vecs =
					hmac_sha512_des_cbc_enc_tv_temp,
					.count =
					HMAC_SHA512_DES_CBC_ENC_TEST_VEC
				}
			}
		}
	}, {
		.alg = "authenc(hmac(sha512),cbc(des3_ede))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = {
					.vecs =
					hmac_sha512_des3_ede_cbc_enc_tv_temp,
					.count =
					HMAC_SHA512_DES3_EDE_CBC_ENC_TEST_VEC
				}
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
			.cipher = {
				.enc = {
					.vecs = aes_cbc_enc_tv_template,
					.count = AES_CBC_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = aes_cbc_dec_tv_template,
					.count = AES_CBC_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "cbc(anubis)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = anubis_cbc_enc_tv_template,
					.count = ANUBIS_CBC_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = anubis_cbc_dec_tv_template,
					.count = ANUBIS_CBC_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "cbc(blowfish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = bf_cbc_enc_tv_template,
					.count = BF_CBC_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = bf_cbc_dec_tv_template,
					.count = BF_CBC_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "cbc(camellia)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = camellia_cbc_enc_tv_template,
					.count = CAMELLIA_CBC_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = camellia_cbc_dec_tv_template,
					.count = CAMELLIA_CBC_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "cbc(cast5)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = cast5_cbc_enc_tv_template,
					.count = CAST5_CBC_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = cast5_cbc_dec_tv_template,
					.count = CAST5_CBC_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "cbc(cast6)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = cast6_cbc_enc_tv_template,
					.count = CAST6_CBC_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = cast6_cbc_dec_tv_template,
					.count = CAST6_CBC_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "cbc(des)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = des_cbc_enc_tv_template,
					.count = DES_CBC_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = des_cbc_dec_tv_template,
					.count = DES_CBC_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "cbc(des3_ede)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = des3_ede_cbc_enc_tv_template,
					.count = DES3_EDE_CBC_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = des3_ede_cbc_dec_tv_template,
					.count = DES3_EDE_CBC_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "cbc(serpent)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = serpent_cbc_enc_tv_template,
					.count = SERPENT_CBC_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = serpent_cbc_dec_tv_template,
					.count = SERPENT_CBC_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "cbc(twofish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = tf_cbc_enc_tv_template,
					.count = TF_CBC_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = tf_cbc_dec_tv_template,
					.count = TF_CBC_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ccm(aes)",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = {
					.vecs = aes_ccm_enc_tv_template,
					.count = AES_CCM_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = aes_ccm_dec_tv_template,
					.count = AES_CCM_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "chacha20",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = chacha20_enc_tv_template,
					.count = CHACHA20_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = chacha20_enc_tv_template,
					.count = CHACHA20_ENC_TEST_VECTORS
				},
			}
		}
	}, {
		.alg = "cmac(aes)",
		.fips_allowed = 1,
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = aes_cmac128_tv_template,
				.count = CMAC_AES_TEST_VECTORS
			}
		}
	}, {
		.alg = "cmac(des3_ede)",
		.fips_allowed = 1,
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = des3_ede_cmac64_tv_template,
				.count = CMAC_DES3_EDE_TEST_VECTORS
			}
		}
	}, {
		.alg = "compress_null",
		.test = alg_test_null,
	}, {
		.alg = "crc32",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = crc32_tv_template,
				.count = CRC32_TEST_VECTORS
			}
		}
	}, {
		.alg = "crc32c",
		.test = alg_test_crc32c,
		.fips_allowed = 1,
		.suite = {
			.hash = {
				.vecs = crc32c_tv_template,
				.count = CRC32C_TEST_VECTORS
			}
		}
	}, {
		.alg = "crct10dif",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = {
				.vecs = crct10dif_tv_template,
				.count = CRCT10DIF_TEST_VECTORS
			}
		}
	}, {
		.alg = "cryptd(__driver-cbc-aes-aesni)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "cryptd(__driver-cbc-camellia-aesni)",
		.test = alg_test_null,
	}, {
		.alg = "cryptd(__driver-cbc-camellia-aesni-avx2)",
		.test = alg_test_null,
	}, {
		.alg = "cryptd(__driver-cbc-serpent-avx2)",
		.test = alg_test_null,
	}, {
		.alg = "cryptd(__driver-ecb-aes-aesni)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "cryptd(__driver-ecb-camellia-aesni)",
		.test = alg_test_null,
	}, {
		.alg = "cryptd(__driver-ecb-camellia-aesni-avx2)",
		.test = alg_test_null,
	}, {
		.alg = "cryptd(__driver-ecb-cast5-avx)",
		.test = alg_test_null,
	}, {
		.alg = "cryptd(__driver-ecb-cast6-avx)",
		.test = alg_test_null,
	}, {
		.alg = "cryptd(__driver-ecb-serpent-avx)",
		.test = alg_test_null,
	}, {
		.alg = "cryptd(__driver-ecb-serpent-avx2)",
		.test = alg_test_null,
	}, {
		.alg = "cryptd(__driver-ecb-serpent-sse2)",
		.test = alg_test_null,
	}, {
		.alg = "cryptd(__driver-ecb-twofish-avx)",
		.test = alg_test_null,
	}, {
		.alg = "cryptd(__driver-gcm-aes-aesni)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "cryptd(__ghash-pclmulqdqni)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "ctr(aes)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = aes_ctr_enc_tv_template,
					.count = AES_CTR_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = aes_ctr_dec_tv_template,
					.count = AES_CTR_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ctr(blowfish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = bf_ctr_enc_tv_template,
					.count = BF_CTR_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = bf_ctr_dec_tv_template,
					.count = BF_CTR_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ctr(camellia)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = camellia_ctr_enc_tv_template,
					.count = CAMELLIA_CTR_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = camellia_ctr_dec_tv_template,
					.count = CAMELLIA_CTR_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ctr(cast5)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = cast5_ctr_enc_tv_template,
					.count = CAST5_CTR_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = cast5_ctr_dec_tv_template,
					.count = CAST5_CTR_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ctr(cast6)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = cast6_ctr_enc_tv_template,
					.count = CAST6_CTR_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = cast6_ctr_dec_tv_template,
					.count = CAST6_CTR_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ctr(des)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = des_ctr_enc_tv_template,
					.count = DES_CTR_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = des_ctr_dec_tv_template,
					.count = DES_CTR_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ctr(des3_ede)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = des3_ede_ctr_enc_tv_template,
					.count = DES3_EDE_CTR_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = des3_ede_ctr_dec_tv_template,
					.count = DES3_EDE_CTR_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ctr(serpent)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = serpent_ctr_enc_tv_template,
					.count = SERPENT_CTR_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = serpent_ctr_dec_tv_template,
					.count = SERPENT_CTR_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ctr(twofish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = tf_ctr_enc_tv_template,
					.count = TF_CTR_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = tf_ctr_dec_tv_template,
					.count = TF_CTR_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "cts(cbc(aes))",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = cts_mode_enc_tv_template,
					.count = CTS_MODE_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = cts_mode_dec_tv_template,
					.count = CTS_MODE_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "deflate",
		.test = alg_test_comp,
		.fips_allowed = 1,
		.suite = {
			.comp = {
				.comp = {
					.vecs = deflate_comp_tv_template,
					.count = DEFLATE_COMP_TEST_VECTORS
				},
				.decomp = {
					.vecs = deflate_decomp_tv_template,
					.count = DEFLATE_DECOMP_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "digest_null",
		.test = alg_test_null,
	}, {
		.alg = "drbg_nopr_ctr_aes128",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = {
				.vecs = drbg_nopr_ctr_aes128_tv_template,
				.count = ARRAY_SIZE(drbg_nopr_ctr_aes128_tv_template)
			}
		}
	}, {
		.alg = "drbg_nopr_ctr_aes192",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = {
				.vecs = drbg_nopr_ctr_aes192_tv_template,
				.count = ARRAY_SIZE(drbg_nopr_ctr_aes192_tv_template)
			}
		}
	}, {
		.alg = "drbg_nopr_ctr_aes256",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = {
				.vecs = drbg_nopr_ctr_aes256_tv_template,
				.count = ARRAY_SIZE(drbg_nopr_ctr_aes256_tv_template)
			}
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
			.drbg = {
				.vecs = drbg_nopr_hmac_sha256_tv_template,
				.count =
				ARRAY_SIZE(drbg_nopr_hmac_sha256_tv_template)
			}
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
			.drbg = {
				.vecs = drbg_nopr_sha256_tv_template,
				.count = ARRAY_SIZE(drbg_nopr_sha256_tv_template)
			}
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
			.drbg = {
				.vecs = drbg_pr_ctr_aes128_tv_template,
				.count = ARRAY_SIZE(drbg_pr_ctr_aes128_tv_template)
			}
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
			.drbg = {
				.vecs = drbg_pr_hmac_sha256_tv_template,
				.count = ARRAY_SIZE(drbg_pr_hmac_sha256_tv_template)
			}
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
			.drbg = {
				.vecs = drbg_pr_sha256_tv_template,
				.count = ARRAY_SIZE(drbg_pr_sha256_tv_template)
			}
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
		.alg = "ecb(__aes-aesni)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "ecb(aes)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = aes_enc_tv_template,
					.count = AES_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = aes_dec_tv_template,
					.count = AES_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ecb(anubis)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = anubis_enc_tv_template,
					.count = ANUBIS_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = anubis_dec_tv_template,
					.count = ANUBIS_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ecb(arc4)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = arc4_enc_tv_template,
					.count = ARC4_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = arc4_dec_tv_template,
					.count = ARC4_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ecb(blowfish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = bf_enc_tv_template,
					.count = BF_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = bf_dec_tv_template,
					.count = BF_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ecb(camellia)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = camellia_enc_tv_template,
					.count = CAMELLIA_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = camellia_dec_tv_template,
					.count = CAMELLIA_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ecb(cast5)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = cast5_enc_tv_template,
					.count = CAST5_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = cast5_dec_tv_template,
					.count = CAST5_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ecb(cast6)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = cast6_enc_tv_template,
					.count = CAST6_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = cast6_dec_tv_template,
					.count = CAST6_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ecb(cipher_null)",
		.test = alg_test_null,
	}, {
		.alg = "ecb(des)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = des_enc_tv_template,
					.count = DES_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = des_dec_tv_template,
					.count = DES_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ecb(des3_ede)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = des3_ede_enc_tv_template,
					.count = DES3_EDE_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = des3_ede_dec_tv_template,
					.count = DES3_EDE_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ecb(fcrypt)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = fcrypt_pcbc_enc_tv_template,
					.count = 1
				},
				.dec = {
					.vecs = fcrypt_pcbc_dec_tv_template,
					.count = 1
				}
			}
		}
	}, {
		.alg = "ecb(khazad)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = khazad_enc_tv_template,
					.count = KHAZAD_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = khazad_dec_tv_template,
					.count = KHAZAD_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ecb(seed)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = seed_enc_tv_template,
					.count = SEED_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = seed_dec_tv_template,
					.count = SEED_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ecb(serpent)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = serpent_enc_tv_template,
					.count = SERPENT_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = serpent_dec_tv_template,
					.count = SERPENT_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ecb(tea)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = tea_enc_tv_template,
					.count = TEA_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = tea_dec_tv_template,
					.count = TEA_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ecb(tnepres)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = tnepres_enc_tv_template,
					.count = TNEPRES_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = tnepres_dec_tv_template,
					.count = TNEPRES_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ecb(twofish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = tf_enc_tv_template,
					.count = TF_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = tf_dec_tv_template,
					.count = TF_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ecb(xeta)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = xeta_enc_tv_template,
					.count = XETA_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = xeta_dec_tv_template,
					.count = XETA_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ecb(xtea)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = xtea_enc_tv_template,
					.count = XTEA_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = xtea_dec_tv_template,
					.count = XTEA_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "gcm(aes)",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = {
					.vecs = aes_gcm_enc_tv_template,
					.count = AES_GCM_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = aes_gcm_dec_tv_template,
					.count = AES_GCM_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "ghash",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = {
				.vecs = ghash_tv_template,
				.count = GHASH_TEST_VECTORS
			}
		}
	}, {
		.alg = "hmac(crc32)",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = bfin_crc_tv_template,
				.count = BFIN_CRC_TEST_VECTORS
			}
		}
	}, {
		.alg = "hmac(md5)",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = hmac_md5_tv_template,
				.count = HMAC_MD5_TEST_VECTORS
			}
		}
	}, {
		.alg = "hmac(rmd128)",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = hmac_rmd128_tv_template,
				.count = HMAC_RMD128_TEST_VECTORS
			}
		}
	}, {
		.alg = "hmac(rmd160)",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = hmac_rmd160_tv_template,
				.count = HMAC_RMD160_TEST_VECTORS
			}
		}
	}, {
		.alg = "hmac(sha1)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = {
				.vecs = hmac_sha1_tv_template,
				.count = HMAC_SHA1_TEST_VECTORS
			}
		}
	}, {
		.alg = "hmac(sha224)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = {
				.vecs = hmac_sha224_tv_template,
				.count = HMAC_SHA224_TEST_VECTORS
			}
		}
	}, {
		.alg = "hmac(sha256)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = {
				.vecs = hmac_sha256_tv_template,
				.count = HMAC_SHA256_TEST_VECTORS
			}
		}
	}, {
		.alg = "hmac(sha384)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = {
				.vecs = hmac_sha384_tv_template,
				.count = HMAC_SHA384_TEST_VECTORS
			}
		}
	}, {
		.alg = "hmac(sha512)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = {
				.vecs = hmac_sha512_tv_template,
				.count = HMAC_SHA512_TEST_VECTORS
			}
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
			.cipher = {
				.enc = {
					.vecs = aes_kw_enc_tv_template,
					.count = ARRAY_SIZE(aes_kw_enc_tv_template)
				},
				.dec = {
					.vecs = aes_kw_dec_tv_template,
					.count = ARRAY_SIZE(aes_kw_dec_tv_template)
				}
			}
		}
	}, {
		.alg = "lrw(aes)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = aes_lrw_enc_tv_template,
					.count = AES_LRW_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = aes_lrw_dec_tv_template,
					.count = AES_LRW_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "lrw(camellia)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = camellia_lrw_enc_tv_template,
					.count = CAMELLIA_LRW_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = camellia_lrw_dec_tv_template,
					.count = CAMELLIA_LRW_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "lrw(cast6)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = cast6_lrw_enc_tv_template,
					.count = CAST6_LRW_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = cast6_lrw_dec_tv_template,
					.count = CAST6_LRW_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "lrw(serpent)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = serpent_lrw_enc_tv_template,
					.count = SERPENT_LRW_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = serpent_lrw_dec_tv_template,
					.count = SERPENT_LRW_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "lrw(twofish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = tf_lrw_enc_tv_template,
					.count = TF_LRW_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = tf_lrw_dec_tv_template,
					.count = TF_LRW_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "lz4",
		.test = alg_test_comp,
		.fips_allowed = 1,
		.suite = {
			.comp = {
				.comp = {
					.vecs = lz4_comp_tv_template,
					.count = LZ4_COMP_TEST_VECTORS
				},
				.decomp = {
					.vecs = lz4_decomp_tv_template,
					.count = LZ4_DECOMP_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "lz4hc",
		.test = alg_test_comp,
		.fips_allowed = 1,
		.suite = {
			.comp = {
				.comp = {
					.vecs = lz4hc_comp_tv_template,
					.count = LZ4HC_COMP_TEST_VECTORS
				},
				.decomp = {
					.vecs = lz4hc_decomp_tv_template,
					.count = LZ4HC_DECOMP_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "lzo",
		.test = alg_test_comp,
		.fips_allowed = 1,
		.suite = {
			.comp = {
				.comp = {
					.vecs = lzo_comp_tv_template,
					.count = LZO_COMP_TEST_VECTORS
				},
				.decomp = {
					.vecs = lzo_decomp_tv_template,
					.count = LZO_DECOMP_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "md4",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = md4_tv_template,
				.count = MD4_TEST_VECTORS
			}
		}
	}, {
		.alg = "md5",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = md5_tv_template,
				.count = MD5_TEST_VECTORS
			}
		}
	}, {
		.alg = "michael_mic",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = michael_mic_tv_template,
				.count = MICHAEL_MIC_TEST_VECTORS
			}
		}
	}, {
		.alg = "ofb(aes)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = aes_ofb_enc_tv_template,
					.count = AES_OFB_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = aes_ofb_dec_tv_template,
					.count = AES_OFB_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "pcbc(fcrypt)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = fcrypt_pcbc_enc_tv_template,
					.count = FCRYPT_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = fcrypt_pcbc_dec_tv_template,
					.count = FCRYPT_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "poly1305",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = poly1305_tv_template,
				.count = POLY1305_TEST_VECTORS
			}
		}
	}, {
		.alg = "rfc3686(ctr(aes))",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = aes_ctr_rfc3686_enc_tv_template,
					.count = AES_CTR_3686_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = aes_ctr_rfc3686_dec_tv_template,
					.count = AES_CTR_3686_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "rfc4106(gcm(aes))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = {
					.vecs = aes_gcm_rfc4106_enc_tv_template,
					.count = AES_GCM_4106_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = aes_gcm_rfc4106_dec_tv_template,
					.count = AES_GCM_4106_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "rfc4309(ccm(aes))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = {
					.vecs = aes_ccm_rfc4309_enc_tv_template,
					.count = AES_CCM_4309_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = aes_ccm_rfc4309_dec_tv_template,
					.count = AES_CCM_4309_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "rfc4543(gcm(aes))",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = {
					.vecs = aes_gcm_rfc4543_enc_tv_template,
					.count = AES_GCM_4543_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = aes_gcm_rfc4543_dec_tv_template,
					.count = AES_GCM_4543_DEC_TEST_VECTORS
				},
			}
		}
	}, {
		.alg = "rfc7539(chacha20,poly1305)",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = {
					.vecs = rfc7539_enc_tv_template,
					.count = RFC7539_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = rfc7539_dec_tv_template,
					.count = RFC7539_DEC_TEST_VECTORS
				},
			}
		}
	}, {
		.alg = "rfc7539esp(chacha20,poly1305)",
		.test = alg_test_aead,
		.suite = {
			.aead = {
				.enc = {
					.vecs = rfc7539esp_enc_tv_template,
					.count = RFC7539ESP_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = rfc7539esp_dec_tv_template,
					.count = RFC7539ESP_DEC_TEST_VECTORS
				},
			}
		}
	}, {
		.alg = "rmd128",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = rmd128_tv_template,
				.count = RMD128_TEST_VECTORS
			}
		}
	}, {
		.alg = "rmd160",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = rmd160_tv_template,
				.count = RMD160_TEST_VECTORS
			}
		}
	}, {
		.alg = "rmd256",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = rmd256_tv_template,
				.count = RMD256_TEST_VECTORS
			}
		}
	}, {
		.alg = "rmd320",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = rmd320_tv_template,
				.count = RMD320_TEST_VECTORS
			}
		}
	}, {
		.alg = "rsa",
		.test = alg_test_akcipher,
		.fips_allowed = 1,
		.suite = {
			.akcipher = {
				.vecs = rsa_tv_template,
				.count = RSA_TEST_VECTORS
			}
		}
	}, {
		.alg = "salsa20",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = salsa20_stream_enc_tv_template,
					.count = SALSA20_STREAM_ENC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "sha1",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = {
				.vecs = sha1_tv_template,
				.count = SHA1_TEST_VECTORS
			}
		}
	}, {
		.alg = "sha224",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = {
				.vecs = sha224_tv_template,
				.count = SHA224_TEST_VECTORS
			}
		}
	}, {
		.alg = "sha256",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = {
				.vecs = sha256_tv_template,
				.count = SHA256_TEST_VECTORS
			}
		}
	}, {
		.alg = "sha384",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = {
				.vecs = sha384_tv_template,
				.count = SHA384_TEST_VECTORS
			}
		}
	}, {
		.alg = "sha512",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = {
				.vecs = sha512_tv_template,
				.count = SHA512_TEST_VECTORS
			}
		}
	}, {
		.alg = "tgr128",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = tgr128_tv_template,
				.count = TGR128_TEST_VECTORS
			}
		}
	}, {
		.alg = "tgr160",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = tgr160_tv_template,
				.count = TGR160_TEST_VECTORS
			}
		}
	}, {
		.alg = "tgr192",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = tgr192_tv_template,
				.count = TGR192_TEST_VECTORS
			}
		}
	}, {
		.alg = "vmac(aes)",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = aes_vmac128_tv_template,
				.count = VMAC_AES_TEST_VECTORS
			}
		}
	}, {
		.alg = "wp256",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = wp256_tv_template,
				.count = WP256_TEST_VECTORS
			}
		}
	}, {
		.alg = "wp384",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = wp384_tv_template,
				.count = WP384_TEST_VECTORS
			}
		}
	}, {
		.alg = "wp512",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = wp512_tv_template,
				.count = WP512_TEST_VECTORS
			}
		}
	}, {
		.alg = "xcbc(aes)",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = aes_xcbc128_tv_template,
				.count = XCBC_AES_TEST_VECTORS
			}
		}
	}, {
		.alg = "xts(aes)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = aes_xts_enc_tv_template,
					.count = AES_XTS_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = aes_xts_dec_tv_template,
					.count = AES_XTS_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "xts(camellia)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = camellia_xts_enc_tv_template,
					.count = CAMELLIA_XTS_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = camellia_xts_dec_tv_template,
					.count = CAMELLIA_XTS_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "xts(cast6)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = cast6_xts_enc_tv_template,
					.count = CAST6_XTS_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = cast6_xts_dec_tv_template,
					.count = CAST6_XTS_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "xts(serpent)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = serpent_xts_enc_tv_template,
					.count = SERPENT_XTS_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = serpent_xts_dec_tv_template,
					.count = SERPENT_XTS_DEC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "xts(twofish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = tf_xts_enc_tv_template,
					.count = TF_XTS_ENC_TEST_VECTORS
				},
				.dec = {
					.vecs = tf_xts_dec_tv_template,
					.count = TF_XTS_DEC_TEST_VECTORS
				}
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
