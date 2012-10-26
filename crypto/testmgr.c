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

#include <crypto/hash.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <crypto/rng.h>

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

struct pcomp_test_suite {
	struct {
		struct pcomp_testvec *vecs;
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

struct alg_test_desc {
	const char *alg;
	int (*test)(const struct alg_test_desc *desc, const char *driver,
		    u32 type, u32 mask);
	int fips_allowed;	/* set if alg is allowed in fips mode */

	union {
		struct aead_test_suite aead;
		struct cipher_test_suite cipher;
		struct comp_test_suite comp;
		struct pcomp_test_suite pcomp;
		struct hash_test_suite hash;
		struct cprng_test_suite cprng;
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

static int do_one_async_hash_op(struct ahash_request *req,
				struct tcrypt_result *tr,
				int ret)
{
	if (ret == -EINPROGRESS || ret == -EBUSY) {
		ret = wait_for_completion_interruptible(&tr->completion);
		if (!ret)
			ret = tr->err;
		INIT_COMPLETION(tr->completion);
	}
	return ret;
}

static int test_hash(struct crypto_ahash *tfm, struct hash_testvec *template,
		     unsigned int tcount, bool use_digest)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_ahash_tfm(tfm));
	unsigned int i, j, k, temp;
	struct scatterlist sg[8];
	char result[64];
	struct ahash_request *req;
	struct tcrypt_result tresult;
	void *hash_buff;
	char *xbuf[XBUFSIZE];
	int ret = -ENOMEM;

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

		j++;
		memset(result, 0, 64);

		hash_buff = xbuf[0];

		memcpy(hash_buff, template[i].plaintext, template[i].psize);
		sg_init_one(&sg[0], hash_buff, template[i].psize);

		if (template[i].ksize) {
			crypto_ahash_clear_flags(tfm, ~0);
			ret = crypto_ahash_setkey(tfm, template[i].key,
						  template[i].ksize);
			if (ret) {
				printk(KERN_ERR "alg: hash: setkey failed on "
				       "test %d for %s: ret=%d\n", j, algo,
				       -ret);
				goto out;
			}
		}

		ahash_request_set_crypt(req, sg, result, template[i].psize);
		if (use_digest) {
			ret = do_one_async_hash_op(req, &tresult,
						   crypto_ahash_digest(req));
			if (ret) {
				pr_err("alg: hash: digest failed on test %d "
				       "for %s: ret=%d\n", j, algo, -ret);
				goto out;
			}
		} else {
			ret = do_one_async_hash_op(req, &tresult,
						   crypto_ahash_init(req));
			if (ret) {
				pr_err("alt: hash: init failed on test %d "
				       "for %s: ret=%d\n", j, algo, -ret);
				goto out;
			}
			ret = do_one_async_hash_op(req, &tresult,
						   crypto_ahash_update(req));
			if (ret) {
				pr_err("alt: hash: update failed on test %d "
				       "for %s: ret=%d\n", j, algo, -ret);
				goto out;
			}
			ret = do_one_async_hash_op(req, &tresult,
						   crypto_ahash_final(req));
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
		if (template[i].np) {
			j++;
			memset(result, 0, 64);

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
				crypto_ahash_clear_flags(tfm, ~0);
				ret = crypto_ahash_setkey(tfm, template[i].key,
							  template[i].ksize);

				if (ret) {
					printk(KERN_ERR "alg: hash: setkey "
					       "failed on chunking test %d "
					       "for %s: ret=%d\n", j, algo,
					       -ret);
					goto out;
				}
			}

			ahash_request_set_crypt(req, sg, result,
						template[i].psize);
			ret = crypto_ahash_digest(req);
			switch (ret) {
			case 0:
				break;
			case -EINPROGRESS:
			case -EBUSY:
				ret = wait_for_completion_interruptible(
					&tresult.completion);
				if (!ret && !(ret = tresult.err)) {
					INIT_COMPLETION(tresult.completion);
					break;
				}
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
	}

	ret = 0;

out:
	ahash_request_free(req);
out_noreq:
	testmgr_free_buf(xbuf);
out_nobuf:
	return ret;
}

static int __test_aead(struct crypto_aead *tfm, int enc,
		       struct aead_testvec *template, unsigned int tcount,
		       const bool diff_dst)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm));
	unsigned int i, j, k, n, temp;
	int ret = -ENOMEM;
	char *q;
	char *key;
	struct aead_request *req;
	struct scatterlist *sg;
	struct scatterlist *asg;
	struct scatterlist *sgout;
	const char *e, *d;
	struct tcrypt_result result;
	unsigned int authsize;
	void *input;
	void *output;
	void *assoc;
	char iv[MAX_IVLEN];
	char *xbuf[XBUFSIZE];
	char *xoutbuf[XBUFSIZE];
	char *axbuf[XBUFSIZE];

	if (testmgr_alloc_buf(xbuf))
		goto out_noxbuf;
	if (testmgr_alloc_buf(axbuf))
		goto out_noaxbuf;

	if (diff_dst && testmgr_alloc_buf(xoutbuf))
		goto out_nooutbuf;

	/* avoid "the frame size is larger than 1024 bytes" compiler warning */
	sg = kmalloc(sizeof(*sg) * 8 * (diff_dst ? 3 : 2), GFP_KERNEL);
	if (!sg)
		goto out_nosg;
	asg = &sg[8];
	sgout = &asg[8];

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

	for (i = 0, j = 0; i < tcount; i++) {
		if (!template[i].np) {
			j++;

			/* some tepmplates have no input data but they will
			 * touch input
			 */
			input = xbuf[0];
			assoc = axbuf[0];

			ret = -EINVAL;
			if (WARN_ON(template[i].ilen > PAGE_SIZE ||
				    template[i].alen > PAGE_SIZE))
				goto out;

			memcpy(input, template[i].input, template[i].ilen);
			memcpy(assoc, template[i].assoc, template[i].alen);
			if (template[i].iv)
				memcpy(iv, template[i].iv, MAX_IVLEN);
			else
				memset(iv, 0, MAX_IVLEN);

			crypto_aead_clear_flags(tfm, ~0);
			if (template[i].wk)
				crypto_aead_set_flags(
					tfm, CRYPTO_TFM_REQ_WEAK_KEY);

			key = template[i].key;

			ret = crypto_aead_setkey(tfm, key,
						 template[i].klen);
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

			sg_init_one(&sg[0], input,
				    template[i].ilen + (enc ? authsize : 0));

			if (diff_dst) {
				output = xoutbuf[0];
				sg_init_one(&sgout[0], output,
					    template[i].ilen +
						(enc ? authsize : 0));
			} else {
				output = input;
			}

			sg_init_one(&asg[0], assoc, template[i].alen);

			aead_request_set_crypt(req, sg, (diff_dst) ? sgout : sg,
					       template[i].ilen, iv);

			aead_request_set_assoc(req, asg, template[i].alen);

			ret = enc ?
				crypto_aead_encrypt(req) :
				crypto_aead_decrypt(req);

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
				ret = wait_for_completion_interruptible(
					&result.completion);
				if (!ret && !(ret = result.err)) {
					INIT_COMPLETION(result.completion);
					break;
				}
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
	}

	for (i = 0, j = 0; i < tcount; i++) {
		if (template[i].np) {
			j++;

			if (template[i].iv)
				memcpy(iv, template[i].iv, MAX_IVLEN);
			else
				memset(iv, 0, MAX_IVLEN);

			crypto_aead_clear_flags(tfm, ~0);
			if (template[i].wk)
				crypto_aead_set_flags(
					tfm, CRYPTO_TFM_REQ_WEAK_KEY);
			key = template[i].key;

			ret = crypto_aead_setkey(tfm, key, template[i].klen);
			if (!ret == template[i].fail) {
				pr_err("alg: aead%s: setkey failed on chunk test %d for %s: flags=%x\n",
				       d, j, algo, crypto_aead_get_flags(tfm));
				goto out;
			} else if (ret)
				continue;

			authsize = abs(template[i].rlen - template[i].ilen);

			ret = -EINVAL;
			sg_init_table(sg, template[i].np);
			if (diff_dst)
				sg_init_table(sgout, template[i].np);
			for (k = 0, temp = 0; k < template[i].np; k++) {
				if (WARN_ON(offset_in_page(IDX[k]) +
					    template[i].tap[k] > PAGE_SIZE))
					goto out;

				q = xbuf[IDX[k] >> PAGE_SHIFT] +
				    offset_in_page(IDX[k]);

				memcpy(q, template[i].input + temp,
				       template[i].tap[k]);

				n = template[i].tap[k];
				if (k == template[i].np - 1 && enc)
					n += authsize;
				if (offset_in_page(q) + n < PAGE_SIZE)
					q[n] = 0;

				sg_set_buf(&sg[k], q, template[i].tap[k]);

				if (diff_dst) {
					q = xoutbuf[IDX[k] >> PAGE_SHIFT] +
					    offset_in_page(IDX[k]);

					memset(q, 0, template[i].tap[k]);
					if (offset_in_page(q) + n < PAGE_SIZE)
						q[n] = 0;

					sg_set_buf(&sgout[k], q,
						   template[i].tap[k]);
				}

				temp += template[i].tap[k];
			}

			ret = crypto_aead_setauthsize(tfm, authsize);
			if (ret) {
				pr_err("alg: aead%s: Failed to set authsize to %u on chunk test %d for %s\n",
				       d, authsize, j, algo);
				goto out;
			}

			if (enc) {
				if (WARN_ON(sg[k - 1].offset +
					    sg[k - 1].length + authsize >
					    PAGE_SIZE)) {
					ret = -EINVAL;
					goto out;
				}

				sg[k - 1].length += authsize;

				if (diff_dst)
					sgout[k - 1].length += authsize;
			}

			sg_init_table(asg, template[i].anp);
			ret = -EINVAL;
			for (k = 0, temp = 0; k < template[i].anp; k++) {
				if (WARN_ON(offset_in_page(IDX[k]) +
					    template[i].atap[k] > PAGE_SIZE))
					goto out;
				sg_set_buf(&asg[k],
					   memcpy(axbuf[IDX[k] >> PAGE_SHIFT] +
						  offset_in_page(IDX[k]),
						  template[i].assoc + temp,
						  template[i].atap[k]),
					   template[i].atap[k]);
				temp += template[i].atap[k];
			}

			aead_request_set_crypt(req, sg, (diff_dst) ? sgout : sg,
					       template[i].ilen,
					       iv);

			aead_request_set_assoc(req, asg, template[i].alen);

			ret = enc ?
				crypto_aead_encrypt(req) :
				crypto_aead_decrypt(req);

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
				ret = wait_for_completion_interruptible(
					&result.completion);
				if (!ret && !(ret = result.err)) {
					INIT_COMPLETION(result.completion);
					break;
				}
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
					for (n = 0; offset_in_page(q + n) &&
						    q[n]; n++)
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
	return ret;
}

static int test_aead(struct crypto_aead *tfm, int enc,
		     struct aead_testvec *template, unsigned int tcount)
{
	int ret;

	/* test 'dst == src' case */
	ret = __test_aead(tfm, enc, template, tcount, false);
	if (ret)
		return ret;

	/* test 'dst != src' case */
	return __test_aead(tfm, enc, template, tcount, true);
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

static int __test_skcipher(struct crypto_ablkcipher *tfm, int enc,
			   struct cipher_testvec *template, unsigned int tcount,
			   const bool diff_dst)
{
	const char *algo =
		crypto_tfm_alg_driver_name(crypto_ablkcipher_tfm(tfm));
	unsigned int i, j, k, n, temp;
	char *q;
	struct ablkcipher_request *req;
	struct scatterlist sg[8];
	struct scatterlist sgout[8];
	const char *e, *d;
	struct tcrypt_result result;
	void *data;
	char iv[MAX_IVLEN];
	char *xbuf[XBUFSIZE];
	char *xoutbuf[XBUFSIZE];
	int ret = -ENOMEM;

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

	req = ablkcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("alg: skcipher%s: Failed to allocate request for %s\n",
		       d, algo);
		goto out;
	}

	ablkcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					tcrypt_complete, &result);

	j = 0;
	for (i = 0; i < tcount; i++) {
		if (template[i].iv)
			memcpy(iv, template[i].iv, MAX_IVLEN);
		else
			memset(iv, 0, MAX_IVLEN);

		if (!(template[i].np) || (template[i].also_non_np)) {
			j++;

			ret = -EINVAL;
			if (WARN_ON(template[i].ilen > PAGE_SIZE))
				goto out;

			data = xbuf[0];
			memcpy(data, template[i].input, template[i].ilen);

			crypto_ablkcipher_clear_flags(tfm, ~0);
			if (template[i].wk)
				crypto_ablkcipher_set_flags(
					tfm, CRYPTO_TFM_REQ_WEAK_KEY);

			ret = crypto_ablkcipher_setkey(tfm, template[i].key,
						       template[i].klen);
			if (!ret == template[i].fail) {
				pr_err("alg: skcipher%s: setkey failed on test %d for %s: flags=%x\n",
				       d, j, algo,
				       crypto_ablkcipher_get_flags(tfm));
				goto out;
			} else if (ret)
				continue;

			sg_init_one(&sg[0], data, template[i].ilen);
			if (diff_dst) {
				data = xoutbuf[0];
				sg_init_one(&sgout[0], data, template[i].ilen);
			}

			ablkcipher_request_set_crypt(req, sg,
						     (diff_dst) ? sgout : sg,
						     template[i].ilen, iv);
			ret = enc ?
				crypto_ablkcipher_encrypt(req) :
				crypto_ablkcipher_decrypt(req);

			switch (ret) {
			case 0:
				break;
			case -EINPROGRESS:
			case -EBUSY:
				ret = wait_for_completion_interruptible(
					&result.completion);
				if (!ret && !((ret = result.err))) {
					INIT_COMPLETION(result.completion);
					break;
				}
				/* fall through */
			default:
				pr_err("alg: skcipher%s: %s failed on test %d for %s: ret=%d\n",
				       d, e, j, algo, -ret);
				goto out;
			}

			q = data;
			if (memcmp(q, template[i].result, template[i].rlen)) {
				pr_err("alg: skcipher%s: Test %d failed on %s for %s\n",
				       d, j, e, algo);
				hexdump(q, template[i].rlen);
				ret = -EINVAL;
				goto out;
			}
		}
	}

	j = 0;
	for (i = 0; i < tcount; i++) {

		if (template[i].iv)
			memcpy(iv, template[i].iv, MAX_IVLEN);
		else
			memset(iv, 0, MAX_IVLEN);

		if (template[i].np) {
			j++;

			crypto_ablkcipher_clear_flags(tfm, ~0);
			if (template[i].wk)
				crypto_ablkcipher_set_flags(
					tfm, CRYPTO_TFM_REQ_WEAK_KEY);

			ret = crypto_ablkcipher_setkey(tfm, template[i].key,
						       template[i].klen);
			if (!ret == template[i].fail) {
				pr_err("alg: skcipher%s: setkey failed on chunk test %d for %s: flags=%x\n",
				       d, j, algo,
				       crypto_ablkcipher_get_flags(tfm));
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

				q = xbuf[IDX[k] >> PAGE_SHIFT] +
				    offset_in_page(IDX[k]);

				memcpy(q, template[i].input + temp,
				       template[i].tap[k]);

				if (offset_in_page(q) + template[i].tap[k] <
				    PAGE_SIZE)
					q[template[i].tap[k]] = 0;

				sg_set_buf(&sg[k], q, template[i].tap[k]);
				if (diff_dst) {
					q = xoutbuf[IDX[k] >> PAGE_SHIFT] +
					    offset_in_page(IDX[k]);

					sg_set_buf(&sgout[k], q,
						   template[i].tap[k]);

					memset(q, 0, template[i].tap[k]);
					if (offset_in_page(q) +
					    template[i].tap[k] < PAGE_SIZE)
						q[template[i].tap[k]] = 0;
				}

				temp += template[i].tap[k];
			}

			ablkcipher_request_set_crypt(req, sg,
					(diff_dst) ? sgout : sg,
					template[i].ilen, iv);

			ret = enc ?
				crypto_ablkcipher_encrypt(req) :
				crypto_ablkcipher_decrypt(req);

			switch (ret) {
			case 0:
				break;
			case -EINPROGRESS:
			case -EBUSY:
				ret = wait_for_completion_interruptible(
					&result.completion);
				if (!ret && !((ret = result.err))) {
					INIT_COMPLETION(result.completion);
					break;
				}
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
	}

	ret = 0;

out:
	ablkcipher_request_free(req);
	if (diff_dst)
		testmgr_free_buf(xoutbuf);
out_nooutbuf:
	testmgr_free_buf(xbuf);
out_nobuf:
	return ret;
}

static int test_skcipher(struct crypto_ablkcipher *tfm, int enc,
			 struct cipher_testvec *template, unsigned int tcount)
{
	int ret;

	/* test 'dst == src' case */
	ret = __test_skcipher(tfm, enc, template, tcount, false);
	if (ret)
		return ret;

	/* test 'dst != src' case */
	return __test_skcipher(tfm, enc, template, tcount, true);
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

static int test_pcomp(struct crypto_pcomp *tfm,
		      struct pcomp_testvec *ctemplate,
		      struct pcomp_testvec *dtemplate, int ctcount,
		      int dtcount)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_pcomp_tfm(tfm));
	unsigned int i;
	char result[COMP_BUF_SIZE];
	int res;

	for (i = 0; i < ctcount; i++) {
		struct comp_request req;
		unsigned int produced = 0;

		res = crypto_compress_setup(tfm, ctemplate[i].params,
					    ctemplate[i].paramsize);
		if (res) {
			pr_err("alg: pcomp: compression setup failed on test "
			       "%d for %s: error=%d\n", i + 1, algo, res);
			return res;
		}

		res = crypto_compress_init(tfm);
		if (res) {
			pr_err("alg: pcomp: compression init failed on test "
			       "%d for %s: error=%d\n", i + 1, algo, res);
			return res;
		}

		memset(result, 0, sizeof(result));

		req.next_in = ctemplate[i].input;
		req.avail_in = ctemplate[i].inlen / 2;
		req.next_out = result;
		req.avail_out = ctemplate[i].outlen / 2;

		res = crypto_compress_update(tfm, &req);
		if (res < 0 && (res != -EAGAIN || req.avail_in)) {
			pr_err("alg: pcomp: compression update failed on test "
			       "%d for %s: error=%d\n", i + 1, algo, res);
			return res;
		}
		if (res > 0)
			produced += res;

		/* Add remaining input data */
		req.avail_in += (ctemplate[i].inlen + 1) / 2;

		res = crypto_compress_update(tfm, &req);
		if (res < 0 && (res != -EAGAIN || req.avail_in)) {
			pr_err("alg: pcomp: compression update failed on test "
			       "%d for %s: error=%d\n", i + 1, algo, res);
			return res;
		}
		if (res > 0)
			produced += res;

		/* Provide remaining output space */
		req.avail_out += COMP_BUF_SIZE - ctemplate[i].outlen / 2;

		res = crypto_compress_final(tfm, &req);
		if (res < 0) {
			pr_err("alg: pcomp: compression final failed on test "
			       "%d for %s: error=%d\n", i + 1, algo, res);
			return res;
		}
		produced += res;

		if (COMP_BUF_SIZE - req.avail_out != ctemplate[i].outlen) {
			pr_err("alg: comp: Compression test %d failed for %s: "
			       "output len = %d (expected %d)\n", i + 1, algo,
			       COMP_BUF_SIZE - req.avail_out,
			       ctemplate[i].outlen);
			return -EINVAL;
		}

		if (produced != ctemplate[i].outlen) {
			pr_err("alg: comp: Compression test %d failed for %s: "
			       "returned len = %u (expected %d)\n", i + 1,
			       algo, produced, ctemplate[i].outlen);
			return -EINVAL;
		}

		if (memcmp(result, ctemplate[i].output, ctemplate[i].outlen)) {
			pr_err("alg: pcomp: Compression test %d failed for "
			       "%s\n", i + 1, algo);
			hexdump(result, ctemplate[i].outlen);
			return -EINVAL;
		}
	}

	for (i = 0; i < dtcount; i++) {
		struct comp_request req;
		unsigned int produced = 0;

		res = crypto_decompress_setup(tfm, dtemplate[i].params,
					      dtemplate[i].paramsize);
		if (res) {
			pr_err("alg: pcomp: decompression setup failed on "
			       "test %d for %s: error=%d\n", i + 1, algo, res);
			return res;
		}

		res = crypto_decompress_init(tfm);
		if (res) {
			pr_err("alg: pcomp: decompression init failed on test "
			       "%d for %s: error=%d\n", i + 1, algo, res);
			return res;
		}

		memset(result, 0, sizeof(result));

		req.next_in = dtemplate[i].input;
		req.avail_in = dtemplate[i].inlen / 2;
		req.next_out = result;
		req.avail_out = dtemplate[i].outlen / 2;

		res = crypto_decompress_update(tfm, &req);
		if (res < 0 && (res != -EAGAIN || req.avail_in)) {
			pr_err("alg: pcomp: decompression update failed on "
			       "test %d for %s: error=%d\n", i + 1, algo, res);
			return res;
		}
		if (res > 0)
			produced += res;

		/* Add remaining input data */
		req.avail_in += (dtemplate[i].inlen + 1) / 2;

		res = crypto_decompress_update(tfm, &req);
		if (res < 0 && (res != -EAGAIN || req.avail_in)) {
			pr_err("alg: pcomp: decompression update failed on "
			       "test %d for %s: error=%d\n", i + 1, algo, res);
			return res;
		}
		if (res > 0)
			produced += res;

		/* Provide remaining output space */
		req.avail_out += COMP_BUF_SIZE - dtemplate[i].outlen / 2;

		res = crypto_decompress_final(tfm, &req);
		if (res < 0 && (res != -EAGAIN || req.avail_in)) {
			pr_err("alg: pcomp: decompression final failed on "
			       "test %d for %s: error=%d\n", i + 1, algo, res);
			return res;
		}
		if (res > 0)
			produced += res;

		if (COMP_BUF_SIZE - req.avail_out != dtemplate[i].outlen) {
			pr_err("alg: comp: Decompression test %d failed for "
			       "%s: output len = %d (expected %d)\n", i + 1,
			       algo, COMP_BUF_SIZE - req.avail_out,
			       dtemplate[i].outlen);
			return -EINVAL;
		}

		if (produced != dtemplate[i].outlen) {
			pr_err("alg: comp: Decompression test %d failed for "
			       "%s: returned len = %u (expected %d)\n", i + 1,
			       algo, produced, dtemplate[i].outlen);
			return -EINVAL;
		}

		if (memcmp(result, dtemplate[i].output, dtemplate[i].outlen)) {
			pr_err("alg: pcomp: Decompression test %d failed for "
			       "%s\n", i + 1, algo);
			hexdump(result, dtemplate[i].outlen);
			return -EINVAL;
		}
	}

	return 0;
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
			if (err != template[i].rlen) {
				printk(KERN_ERR "alg: cprng: Failed to obtain "
				       "the correct amount of random data for "
				       "%s (requested %d, got %d)\n", algo,
				       template[i].rlen, err);
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
	struct crypto_cipher *tfm;
	int err = 0;

	tfm = crypto_alloc_cipher(driver, type, mask);
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
	struct crypto_ablkcipher *tfm;
	int err = 0;

	tfm = crypto_alloc_ablkcipher(driver, type, mask);
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
	crypto_free_ablkcipher(tfm);
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

static int alg_test_pcomp(const struct alg_test_desc *desc, const char *driver,
			  u32 type, u32 mask)
{
	struct crypto_pcomp *tfm;
	int err;

	tfm = crypto_alloc_pcomp(driver, type, mask);
	if (IS_ERR(tfm)) {
		pr_err("alg: pcomp: Failed to load transform for %s: %ld\n",
		       driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	err = test_pcomp(tfm, desc->suite.pcomp.comp.vecs,
			 desc->suite.pcomp.decomp.vecs,
			 desc->suite.pcomp.comp.count,
			 desc->suite.pcomp.decomp.count);

	crypto_free_pcomp(tfm);
	return err;
}

static int alg_test_hash(const struct alg_test_desc *desc, const char *driver,
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

	tfm = crypto_alloc_shash(driver, type, mask);
	if (IS_ERR(tfm)) {
		printk(KERN_ERR "alg: crc32c: Failed to load transform for %s: "
		       "%ld\n", driver, PTR_ERR(tfm));
		err = PTR_ERR(tfm);
		goto out;
	}

	do {
		struct {
			struct shash_desc shash;
			char ctx[crypto_shash_descsize(tfm)];
		} sdesc;

		sdesc.shash.tfm = tfm;
		sdesc.shash.flags = 0;

		*(u32 *)sdesc.ctx = le32_to_cpu(420553207);
		err = crypto_shash_final(&sdesc.shash, (u8 *)&val);
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
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__cbc-cast6-avx",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__cbc-serpent-avx",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__cbc-serpent-sse2",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__cbc-twofish-avx",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__driver-cbc-aes-aesni",
		.test = alg_test_null,
		.fips_allowed = 1,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__driver-cbc-camellia-aesni",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__driver-cbc-cast5-avx",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__driver-cbc-cast6-avx",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__driver-cbc-serpent-avx",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__driver-cbc-serpent-sse2",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__driver-cbc-twofish-avx",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__driver-ecb-aes-aesni",
		.test = alg_test_null,
		.fips_allowed = 1,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__driver-ecb-camellia-aesni",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__driver-ecb-cast5-avx",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__driver-ecb-cast6-avx",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__driver-ecb-serpent-avx",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__driver-ecb-serpent-sse2",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__driver-ecb-twofish-avx",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "__ghash-pclmulqdqni",
		.test = alg_test_null,
		.fips_allowed = 1,
		.suite = {
			.hash = {
				.vecs = NULL,
				.count = 0
			}
		}
	}, {
		.alg = "ansi_cprng",
		.test = alg_test_cprng,
		.fips_allowed = 1,
		.suite = {
			.cprng = {
				.vecs = ansi_cprng_aes_tv_template,
				.count = ANSI_CPRNG_AES_TEST_VECTORS
			}
		}
	}, {
		.alg = "authenc(hmac(sha1),cbc(aes))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = {
					.vecs = hmac_sha1_aes_cbc_enc_tv_template,
					.count = HMAC_SHA1_AES_CBC_ENC_TEST_VECTORS
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
					.vecs = hmac_sha256_aes_cbc_enc_tv_template,
					.count = HMAC_SHA256_AES_CBC_ENC_TEST_VECTORS
				}
			}
		}
	}, {
		.alg = "authenc(hmac(sha512),cbc(aes))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = {
				.enc = {
					.vecs = hmac_sha512_aes_cbc_enc_tv_template,
					.count = HMAC_SHA512_AES_CBC_ENC_TEST_VECTORS
				}
			}
		}
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
		.alg = "cryptd(__driver-cbc-aes-aesni)",
		.test = alg_test_null,
		.fips_allowed = 1,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "cryptd(__driver-cbc-camellia-aesni)",
		.test = alg_test_null,
		.fips_allowed = 1,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "cryptd(__driver-ecb-aes-aesni)",
		.test = alg_test_null,
		.fips_allowed = 1,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "cryptd(__driver-ecb-camellia-aesni)",
		.test = alg_test_null,
		.fips_allowed = 1,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "cryptd(__driver-ecb-cast5-avx)",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "cryptd(__driver-ecb-cast6-avx)",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "cryptd(__driver-ecb-serpent-avx)",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "cryptd(__driver-ecb-serpent-sse2)",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "cryptd(__driver-ecb-twofish-avx)",
		.test = alg_test_null,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "cryptd(__driver-gcm-aes-aesni)",
		.test = alg_test_null,
		.fips_allowed = 1,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
	}, {
		.alg = "cryptd(__ghash-pclmulqdqni)",
		.test = alg_test_null,
		.fips_allowed = 1,
		.suite = {
			.hash = {
				.vecs = NULL,
				.count = 0
			}
		}
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
		.alg = "ecb(__aes-aesni)",
		.test = alg_test_null,
		.fips_allowed = 1,
		.suite = {
			.cipher = {
				.enc = {
					.vecs = NULL,
					.count = 0
				},
				.dec = {
					.vecs = NULL,
					.count = 0
				}
			}
		}
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
		.alg = "ecb(des)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
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
		.alg = "lzo",
		.test = alg_test_comp,
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
	}, {
		.alg = "zlib",
		.test = alg_test_pcomp,
		.suite = {
			.pcomp = {
				.comp = {
					.vecs = zlib_comp_tv_template,
					.count = ZLIB_COMP_TEST_VECTORS
				},
				.decomp = {
					.vecs = zlib_decomp_tv_template,
					.count = ZLIB_DECOMP_TEST_VECTORS
				}
			}
		}
	}
};

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
	if (j >= 0)
		rc |= alg_test_descs[j].test(alg_test_descs + j, driver,
					     type, mask);

test_done:
	if (fips_enabled && rc)
		panic("%s: %s alg self test failed in fips mode!\n", driver, alg);

	if (fips_enabled && !rc)
		printk(KERN_INFO "alg: self-tests for %s (%s) passed\n",
		       driver, alg);

	return rc;

notest:
	printk(KERN_INFO "alg: No test for %s (%s)\n", alg, driver);
	return 0;
non_fips_alg:
	return -EINVAL;
}

#endif /* CONFIG_CRYPTO_MANAGER_DISABLE_TESTS */

EXPORT_SYMBOL_GPL(alg_test);
