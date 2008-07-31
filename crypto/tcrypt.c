/*
 * Quick & dirty crypto testing module.
 *
 * This will only exist until we have a better testing mechanism
 * (e.g. a char device).
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2002 Jean-Francois Dive <jef@linuxbe.org>
 * Copyright (c) 2007 Nokia Siemens Networks
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/hash.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <linux/crypto.h>
#include <linux/moduleparam.h>
#include <linux/jiffies.h>
#include <linux/timex.h>
#include <linux/interrupt.h>
#include "tcrypt.h"

/*
 * Need slab memory for testing (size in number of pages).
 */
#define TVMEMSIZE	4
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

struct alg_test_desc {
	const char *alg;
	int (*test)(const struct alg_test_desc *desc, const char *driver,
		    u32 type, u32 mask);

	union {
		struct aead_test_suite aead;
		struct cipher_test_suite cipher;
		struct comp_test_suite comp;
		struct hash_test_suite hash;
	} suite;
};

static unsigned int IDX[8] = { IDX1, IDX2, IDX3, IDX4, IDX5, IDX6, IDX7, IDX8 };

/*
 * Used by test_cipher_speed()
 */
static unsigned int sec;

static int mode;
static char *xbuf[XBUFSIZE];
static char *axbuf[XBUFSIZE];
static char *tvmem[TVMEMSIZE];

static char *check[] = {
	"des", "md5", "des3_ede", "rot13", "sha1", "sha224", "sha256",
	"blowfish", "twofish", "serpent", "sha384", "sha512", "md4", "aes",
	"cast6", "arc4", "michael_mic", "deflate", "crc32c", "tea", "xtea",
	"khazad", "wp512", "wp384", "wp256", "tnepres", "xeta",  "fcrypt",
	"camellia", "seed", "salsa20", "rmd128", "rmd160", "rmd256", "rmd320",
	"lzo", "cts", NULL
};

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

static int test_hash(struct crypto_ahash *tfm, struct hash_testvec *template,
		     unsigned int tcount)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_ahash_tfm(tfm));
	unsigned int i, j, k, temp;
	struct scatterlist sg[8];
	char result[64];
	struct ahash_request *req;
	struct tcrypt_result tresult;
	int ret;
	void *hash_buff;

	init_completion(&tresult.completion);

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		printk(KERN_ERR "alg: hash: Failed to allocate request for "
		       "%s\n", algo);
		ret = -ENOMEM;
		goto out_noreq;
	}
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   tcrypt_complete, &tresult);

	for (i = 0; i < tcount; i++) {
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
				       "test %d for %s: ret=%d\n", i + 1, algo,
				       -ret);
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
			ret = wait_for_completion_interruptible(
				&tresult.completion);
			if (!ret && !(ret = tresult.err)) {
				INIT_COMPLETION(tresult.completion);
				break;
			}
			/* fall through */
		default:
			printk(KERN_ERR "alg: hash: digest failed on test %d "
			       "for %s: ret=%d\n", i + 1, algo, -ret);
			goto out;
		}

		if (memcmp(result, template[i].digest,
			   crypto_ahash_digestsize(tfm))) {
			printk(KERN_ERR "alg: hash: Test %d failed for %s\n",
			       i + 1, algo);
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
			for (k = 0; k < template[i].np; k++) {
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
	return ret;
}

static int test_aead(struct crypto_aead *tfm, int enc,
		     struct aead_testvec *template, unsigned int tcount)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm));
	unsigned int i, j, k, n, temp;
	int ret = 0;
	char *q;
	char *key;
	struct aead_request *req;
	struct scatterlist sg[8];
	struct scatterlist asg[8];
	const char *e;
	struct tcrypt_result result;
	unsigned int authsize;
	void *input;
	void *assoc;
	char iv[MAX_IVLEN];

	if (enc == ENCRYPT)
		e = "encryption";
	else
		e = "decryption";

	init_completion(&result.completion);

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		printk(KERN_ERR "alg: aead: Failed to allocate request for "
		       "%s\n", algo);
		ret = -ENOMEM;
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
				printk(KERN_ERR "alg: aead: setkey failed on "
				       "test %d for %s: flags=%x\n", j, algo,
				       crypto_aead_get_flags(tfm));
				goto out;
			} else if (ret)
				continue;

			authsize = abs(template[i].rlen - template[i].ilen);
			ret = crypto_aead_setauthsize(tfm, authsize);
			if (ret) {
				printk(KERN_ERR "alg: aead: Failed to set "
				       "authsize to %u on test %d for %s\n",
				       authsize, j, algo);
				goto out;
			}

			sg_init_one(&sg[0], input,
				    template[i].ilen + (enc ? authsize : 0));

			sg_init_one(&asg[0], assoc, template[i].alen);

			aead_request_set_crypt(req, sg, sg,
					       template[i].ilen, iv);

			aead_request_set_assoc(req, asg, template[i].alen);

			ret = enc ?
				crypto_aead_encrypt(req) :
				crypto_aead_decrypt(req);

			switch (ret) {
			case 0:
				break;
			case -EINPROGRESS:
			case -EBUSY:
				ret = wait_for_completion_interruptible(
					&result.completion);
				if (!ret && !(ret = result.err)) {
					INIT_COMPLETION(result.completion);
					break;
				}
				/* fall through */
			default:
				printk(KERN_ERR "alg: aead: %s failed on test "
				       "%d for %s: ret=%d\n", e, j, algo, -ret);
				goto out;
			}

			q = input;
			if (memcmp(q, template[i].result, template[i].rlen)) {
				printk(KERN_ERR "alg: aead: Test %d failed on "
				       "%s for %s\n", j, e, algo);
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
				printk(KERN_ERR "alg: aead: setkey failed on "
				       "chunk test %d for %s: flags=%x\n", j,
				       algo, crypto_aead_get_flags(tfm));
				goto out;
			} else if (ret)
				continue;

			authsize = abs(template[i].rlen - template[i].ilen);

			ret = -EINVAL;
			sg_init_table(sg, template[i].np);
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
				temp += template[i].tap[k];
			}

			ret = crypto_aead_setauthsize(tfm, authsize);
			if (ret) {
				printk(KERN_ERR "alg: aead: Failed to set "
				       "authsize to %u on chunk test %d for "
				       "%s\n", authsize, j, algo);
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
			}

			sg_init_table(asg, template[i].anp);
			for (k = 0, temp = 0; k < template[i].anp; k++) {
				sg_set_buf(&asg[k],
					   memcpy(axbuf[IDX[k] >> PAGE_SHIFT] +
						  offset_in_page(IDX[k]),
						  template[i].assoc + temp,
						  template[i].atap[k]),
					   template[i].atap[k]);
				temp += template[i].atap[k];
			}

			aead_request_set_crypt(req, sg, sg,
					       template[i].ilen,
					       iv);

			aead_request_set_assoc(req, asg, template[i].alen);

			ret = enc ?
				crypto_aead_encrypt(req) :
				crypto_aead_decrypt(req);

			switch (ret) {
			case 0:
				break;
			case -EINPROGRESS:
			case -EBUSY:
				ret = wait_for_completion_interruptible(
					&result.completion);
				if (!ret && !(ret = result.err)) {
					INIT_COMPLETION(result.completion);
					break;
				}
				/* fall through */
			default:
				printk(KERN_ERR "alg: aead: %s failed on "
				       "chunk test %d for %s: ret=%d\n", e, j,
				       algo, -ret);
				goto out;
			}

			ret = -EINVAL;
			for (k = 0, temp = 0; k < template[i].np; k++) {
				q = xbuf[IDX[k] >> PAGE_SHIFT] +
				    offset_in_page(IDX[k]);

				n = template[i].tap[k];
				if (k == template[i].np - 1)
					n += enc ? authsize : -authsize;

				if (memcmp(q, template[i].result + temp, n)) {
					printk(KERN_ERR "alg: aead: Chunk "
					       "test %d failed on %s at page "
					       "%u for %s\n", j, e, k, algo);
					hexdump(q, n);
					goto out;
				}

				q += n;
				if (k == template[i].np - 1 && !enc) {
					if (memcmp(q, template[i].input +
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
					printk(KERN_ERR "alg: aead: Result "
					       "buffer corruption in chunk "
					       "test %d on %s at page %u for "
					       "%s: %u bytes:\n", j, e, k,
					       algo, n);
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
	return ret;
}

static int test_cipher(struct crypto_ablkcipher *tfm, int enc,
		       struct cipher_testvec *template, unsigned int tcount)
{
	const char *algo =
		crypto_tfm_alg_driver_name(crypto_ablkcipher_tfm(tfm));
	unsigned int i, j, k, n, temp;
	int ret;
	char *q;
	struct ablkcipher_request *req;
	struct scatterlist sg[8];
	const char *e;
	struct tcrypt_result result;
	void *data;
	char iv[MAX_IVLEN];

	if (enc == ENCRYPT)
	        e = "encryption";
	else
		e = "decryption";

	init_completion(&result.completion);

	req = ablkcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		printk(KERN_ERR "alg: cipher: Failed to allocate request for "
		       "%s\n", algo);
		ret = -ENOMEM;
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

		if (!(template[i].np)) {
			j++;

			data = xbuf[0];
			memcpy(data, template[i].input, template[i].ilen);

			crypto_ablkcipher_clear_flags(tfm, ~0);
			if (template[i].wk)
				crypto_ablkcipher_set_flags(
					tfm, CRYPTO_TFM_REQ_WEAK_KEY);

			ret = crypto_ablkcipher_setkey(tfm, template[i].key,
						       template[i].klen);
			if (!ret == template[i].fail) {
				printk(KERN_ERR "alg: cipher: setkey failed "
				       "on test %d for %s: flags=%x\n", j,
				       algo, crypto_ablkcipher_get_flags(tfm));
				goto out;
			} else if (ret)
				continue;

			sg_init_one(&sg[0], data, template[i].ilen);

			ablkcipher_request_set_crypt(req, sg, sg,
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
				printk(KERN_ERR "alg: cipher: %s failed on "
				       "test %d for %s: ret=%d\n", e, j, algo,
				       -ret);
				goto out;
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
				printk(KERN_ERR "alg: cipher: setkey failed "
				       "on chunk test %d for %s: flags=%x\n",
				       j, algo,
				       crypto_ablkcipher_get_flags(tfm));
				goto out;
			} else if (ret)
				continue;

			temp = 0;
			ret = -EINVAL;
			sg_init_table(sg, template[i].np);
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

				temp += template[i].tap[k];
			}

			ablkcipher_request_set_crypt(req, sg, sg,
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
				printk(KERN_ERR "alg: cipher: %s failed on "
				       "chunk test %d for %s: ret=%d\n", e, j,
				       algo, -ret);
				goto out;
			}

			temp = 0;
			ret = -EINVAL;
			for (k = 0; k < template[i].np; k++) {
				q = xbuf[IDX[k] >> PAGE_SHIFT] +
				    offset_in_page(IDX[k]);

				if (memcmp(q, template[i].result + temp,
					   template[i].tap[k])) {
					printk(KERN_ERR "alg: cipher: Chunk "
					       "test %d failed on %s at page "
					       "%u for %s\n", j, e, k, algo);
					hexdump(q, template[i].tap[k]);
					goto out;
				}

				q += template[i].tap[k];
				for (n = 0; offset_in_page(q + n) && q[n]; n++)
					;
				if (n) {
					printk(KERN_ERR "alg: cipher: "
					       "Result buffer corruption in "
					       "chunk test %d on %s at page "
					       "%u for %s: %u bytes:\n", j, e,
					       k, algo, n);
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
	return ret;
}

static int test_cipher_jiffies(struct blkcipher_desc *desc, int enc,
			       struct scatterlist *sg, int blen, int sec)
{
	unsigned long start, end;
	int bcount;
	int ret;

	for (start = jiffies, end = start + sec * HZ, bcount = 0;
	     time_before(jiffies, end); bcount++) {
		if (enc)
			ret = crypto_blkcipher_encrypt(desc, sg, sg, blen);
		else
			ret = crypto_blkcipher_decrypt(desc, sg, sg, blen);

		if (ret)
			return ret;
	}

	printk("%d operations in %d seconds (%ld bytes)\n",
	       bcount, sec, (long)bcount * blen);
	return 0;
}

static int test_cipher_cycles(struct blkcipher_desc *desc, int enc,
			      struct scatterlist *sg, int blen)
{
	unsigned long cycles = 0;
	int ret = 0;
	int i;

	local_bh_disable();
	local_irq_disable();

	/* Warm-up run. */
	for (i = 0; i < 4; i++) {
		if (enc)
			ret = crypto_blkcipher_encrypt(desc, sg, sg, blen);
		else
			ret = crypto_blkcipher_decrypt(desc, sg, sg, blen);

		if (ret)
			goto out;
	}

	/* The real thing. */
	for (i = 0; i < 8; i++) {
		cycles_t start, end;

		start = get_cycles();
		if (enc)
			ret = crypto_blkcipher_encrypt(desc, sg, sg, blen);
		else
			ret = crypto_blkcipher_decrypt(desc, sg, sg, blen);
		end = get_cycles();

		if (ret)
			goto out;

		cycles += end - start;
	}

out:
	local_irq_enable();
	local_bh_enable();

	if (ret == 0)
		printk("1 operation in %lu cycles (%d bytes)\n",
		       (cycles + 4) / 8, blen);

	return ret;
}

static u32 block_sizes[] = { 16, 64, 256, 1024, 8192, 0 };

static void test_cipher_speed(const char *algo, int enc, unsigned int sec,
			      struct cipher_testvec *template,
			      unsigned int tcount, u8 *keysize)
{
	unsigned int ret, i, j, iv_len;
	unsigned char *key, iv[128];
	struct crypto_blkcipher *tfm;
	struct blkcipher_desc desc;
	const char *e;
	u32 *b_size;

	if (enc == ENCRYPT)
	        e = "encryption";
	else
		e = "decryption";

	printk("\ntesting speed of %s %s\n", algo, e);

	tfm = crypto_alloc_blkcipher(algo, 0, CRYPTO_ALG_ASYNC);

	if (IS_ERR(tfm)) {
		printk("failed to load transform for %s: %ld\n", algo,
		       PTR_ERR(tfm));
		return;
	}
	desc.tfm = tfm;
	desc.flags = 0;

	i = 0;
	do {

		b_size = block_sizes;
		do {
			struct scatterlist sg[TVMEMSIZE];

			if ((*keysize + *b_size) > TVMEMSIZE * PAGE_SIZE) {
				printk("template (%u) too big for "
				       "tvmem (%lu)\n", *keysize + *b_size,
				       TVMEMSIZE * PAGE_SIZE);
				goto out;
			}

			printk("test %u (%d bit key, %d byte blocks): ", i,
					*keysize * 8, *b_size);

			memset(tvmem[0], 0xff, PAGE_SIZE);

			/* set key, plain text and IV */
			key = (unsigned char *)tvmem[0];
			for (j = 0; j < tcount; j++) {
				if (template[j].klen == *keysize) {
					key = template[j].key;
					break;
				}
			}

			ret = crypto_blkcipher_setkey(tfm, key, *keysize);
			if (ret) {
				printk("setkey() failed flags=%x\n",
						crypto_blkcipher_get_flags(tfm));
				goto out;
			}

			sg_init_table(sg, TVMEMSIZE);
			sg_set_buf(sg, tvmem[0] + *keysize,
				   PAGE_SIZE - *keysize);
			for (j = 1; j < TVMEMSIZE; j++) {
				sg_set_buf(sg + j, tvmem[j], PAGE_SIZE);
				memset (tvmem[j], 0xff, PAGE_SIZE);
			}

			iv_len = crypto_blkcipher_ivsize(tfm);
			if (iv_len) {
				memset(&iv, 0xff, iv_len);
				crypto_blkcipher_set_iv(tfm, iv, iv_len);
			}

			if (sec)
				ret = test_cipher_jiffies(&desc, enc, sg,
							  *b_size, sec);
			else
				ret = test_cipher_cycles(&desc, enc, sg,
							 *b_size);

			if (ret) {
				printk("%s() failed flags=%x\n", e, desc.flags);
				break;
			}
			b_size++;
			i++;
		} while (*b_size);
		keysize++;
	} while (*keysize);

out:
	crypto_free_blkcipher(tfm);
}

static int test_hash_jiffies_digest(struct hash_desc *desc,
				    struct scatterlist *sg, int blen,
				    char *out, int sec)
{
	unsigned long start, end;
	int bcount;
	int ret;

	for (start = jiffies, end = start + sec * HZ, bcount = 0;
	     time_before(jiffies, end); bcount++) {
		ret = crypto_hash_digest(desc, sg, blen, out);
		if (ret)
			return ret;
	}

	printk("%6u opers/sec, %9lu bytes/sec\n",
	       bcount / sec, ((long)bcount * blen) / sec);

	return 0;
}

static int test_hash_jiffies(struct hash_desc *desc, struct scatterlist *sg,
			     int blen, int plen, char *out, int sec)
{
	unsigned long start, end;
	int bcount, pcount;
	int ret;

	if (plen == blen)
		return test_hash_jiffies_digest(desc, sg, blen, out, sec);

	for (start = jiffies, end = start + sec * HZ, bcount = 0;
	     time_before(jiffies, end); bcount++) {
		ret = crypto_hash_init(desc);
		if (ret)
			return ret;
		for (pcount = 0; pcount < blen; pcount += plen) {
			ret = crypto_hash_update(desc, sg, plen);
			if (ret)
				return ret;
		}
		/* we assume there is enough space in 'out' for the result */
		ret = crypto_hash_final(desc, out);
		if (ret)
			return ret;
	}

	printk("%6u opers/sec, %9lu bytes/sec\n",
	       bcount / sec, ((long)bcount * blen) / sec);

	return 0;
}

static int test_hash_cycles_digest(struct hash_desc *desc,
				   struct scatterlist *sg, int blen, char *out)
{
	unsigned long cycles = 0;
	int i;
	int ret;

	local_bh_disable();
	local_irq_disable();

	/* Warm-up run. */
	for (i = 0; i < 4; i++) {
		ret = crypto_hash_digest(desc, sg, blen, out);
		if (ret)
			goto out;
	}

	/* The real thing. */
	for (i = 0; i < 8; i++) {
		cycles_t start, end;

		start = get_cycles();

		ret = crypto_hash_digest(desc, sg, blen, out);
		if (ret)
			goto out;

		end = get_cycles();

		cycles += end - start;
	}

out:
	local_irq_enable();
	local_bh_enable();

	if (ret)
		return ret;

	printk("%6lu cycles/operation, %4lu cycles/byte\n",
	       cycles / 8, cycles / (8 * blen));

	return 0;
}

static int test_hash_cycles(struct hash_desc *desc, struct scatterlist *sg,
			    int blen, int plen, char *out)
{
	unsigned long cycles = 0;
	int i, pcount;
	int ret;

	if (plen == blen)
		return test_hash_cycles_digest(desc, sg, blen, out);

	local_bh_disable();
	local_irq_disable();

	/* Warm-up run. */
	for (i = 0; i < 4; i++) {
		ret = crypto_hash_init(desc);
		if (ret)
			goto out;
		for (pcount = 0; pcount < blen; pcount += plen) {
			ret = crypto_hash_update(desc, sg, plen);
			if (ret)
				goto out;
		}
		ret = crypto_hash_final(desc, out);
		if (ret)
			goto out;
	}

	/* The real thing. */
	for (i = 0; i < 8; i++) {
		cycles_t start, end;

		start = get_cycles();

		ret = crypto_hash_init(desc);
		if (ret)
			goto out;
		for (pcount = 0; pcount < blen; pcount += plen) {
			ret = crypto_hash_update(desc, sg, plen);
			if (ret)
				goto out;
		}
		ret = crypto_hash_final(desc, out);
		if (ret)
			goto out;

		end = get_cycles();

		cycles += end - start;
	}

out:
	local_irq_enable();
	local_bh_enable();

	if (ret)
		return ret;

	printk("%6lu cycles/operation, %4lu cycles/byte\n",
	       cycles / 8, cycles / (8 * blen));

	return 0;
}

static void test_hash_speed(const char *algo, unsigned int sec,
			    struct hash_speed *speed)
{
	struct scatterlist sg[TVMEMSIZE];
	struct crypto_hash *tfm;
	struct hash_desc desc;
	char output[1024];
	int i;
	int ret;

	printk("\ntesting speed of %s\n", algo);

	tfm = crypto_alloc_hash(algo, 0, CRYPTO_ALG_ASYNC);

	if (IS_ERR(tfm)) {
		printk("failed to load transform for %s: %ld\n", algo,
		       PTR_ERR(tfm));
		return;
	}

	desc.tfm = tfm;
	desc.flags = 0;

	if (crypto_hash_digestsize(tfm) > sizeof(output)) {
		printk("digestsize(%u) > outputbuffer(%zu)\n",
		       crypto_hash_digestsize(tfm), sizeof(output));
		goto out;
	}

	sg_init_table(sg, TVMEMSIZE);
	for (i = 0; i < TVMEMSIZE; i++) {
		sg_set_buf(sg + i, tvmem[i], PAGE_SIZE);
		memset(tvmem[i], 0xff, PAGE_SIZE);
	}

	for (i = 0; speed[i].blen != 0; i++) {
		if (speed[i].blen > TVMEMSIZE * PAGE_SIZE) {
			printk("template (%u) too big for tvmem (%lu)\n",
			       speed[i].blen, TVMEMSIZE * PAGE_SIZE);
			goto out;
		}

		printk("test%3u (%5u byte blocks,%5u bytes per update,%4u updates): ",
		       i, speed[i].blen, speed[i].plen, speed[i].blen / speed[i].plen);

		if (sec)
			ret = test_hash_jiffies(&desc, sg, speed[i].blen,
						speed[i].plen, output, sec);
		else
			ret = test_hash_cycles(&desc, sg, speed[i].blen,
					       speed[i].plen, output);

		if (ret) {
			printk("hashing failed ret=%d\n", ret);
			break;
		}
	}

out:
	crypto_free_hash(tfm);
}

static int test_comp(struct crypto_comp *tfm, struct comp_testvec *ctemplate,
		     struct comp_testvec *dtemplate, int ctcount, int dtcount)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_comp_tfm(tfm));
	unsigned int i;
	char result[COMP_BUF_SIZE];
	int ret;

	for (i = 0; i < ctcount; i++) {
		int ilen, dlen = COMP_BUF_SIZE;

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

		if (memcmp(result, ctemplate[i].output, dlen)) {
			printk(KERN_ERR "alg: comp: Compression test %d "
			       "failed for %s\n", i + 1, algo);
			hexdump(result, dlen);
			ret = -EINVAL;
			goto out;
		}
	}

	for (i = 0; i < dtcount; i++) {
		int ilen, ret, dlen = COMP_BUF_SIZE;

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

static void test_available(void)
{
	char **name = check;

	while (*name) {
		printk("alg %s ", *name);
		printk(crypto_has_alg(*name, 0, 0) ?
		       "found\n" : "not found\n");
		name++;
	}
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
	struct crypto_ablkcipher *tfm;
	int err = 0;

	tfm = crypto_alloc_ablkcipher(driver, type, mask);
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

	err = test_hash(tfm, desc->suite.hash.vecs, desc->suite.hash.count);

	crypto_free_ahash(tfm);
	return err;
}

/* Please keep this list sorted by algorithm name. */
static const struct alg_test_desc alg_test_descs[] = {
	{
		.alg = "cbc(aes)",
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.alg = "cbc(des)",
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.alg = "cbc(twofish)",
		.test = alg_test_cipher,
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
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = crc32c_tv_template,
				.count = CRC32C_TEST_VECTORS
			}
		}
	}, {
		.alg = "cts(cbc(aes))",
		.test = alg_test_cipher,
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
		.alg = "ecb(aes)",
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.suite = {
			.hash = {
				.vecs = hmac_sha1_tv_template,
				.count = HMAC_SHA1_TEST_VECTORS
			}
		}
	}, {
		.alg = "hmac(sha224)",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = hmac_sha224_tv_template,
				.count = HMAC_SHA224_TEST_VECTORS
			}
		}
	}, {
		.alg = "hmac(sha256)",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = hmac_sha256_tv_template,
				.count = HMAC_SHA256_TEST_VECTORS
			}
		}
	}, {
		.alg = "hmac(sha384)",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = hmac_sha384_tv_template,
				.count = HMAC_SHA384_TEST_VECTORS
			}
		}
	}, {
		.alg = "hmac(sha512)",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = hmac_sha512_tv_template,
				.count = HMAC_SHA512_TEST_VECTORS
			}
		}
	}, {
		.alg = "lrw(aes)",
		.test = alg_test_cipher,
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
		.alg = "pcbc(fcrypt)",
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.test = alg_test_cipher,
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
		.suite = {
			.hash = {
				.vecs = sha1_tv_template,
				.count = SHA1_TEST_VECTORS
			}
		}
	}, {
		.alg = "sha224",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = sha224_tv_template,
				.count = SHA224_TEST_VECTORS
			}
		}
	}, {
		.alg = "sha256",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = sha256_tv_template,
				.count = SHA256_TEST_VECTORS
			}
		}
	}, {
		.alg = "sha384",
		.test = alg_test_hash,
		.suite = {
			.hash = {
				.vecs = sha384_tv_template,
				.count = SHA384_TEST_VECTORS
			}
		}
	}, {
		.alg = "sha512",
		.test = alg_test_hash,
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
		.test = alg_test_cipher,
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
	}
};

static int alg_test(const char *driver, const char *alg, u32 type, u32 mask)
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

		return alg_test_descs[i].test(alg_test_descs + i, driver,
					      type, mask);
	}

	printk(KERN_INFO "alg: No test for %s (%s)\n", alg, driver);
	return 0;
}

static inline int tcrypt_test(const char *alg)
{
	return alg_test(alg, alg, 0, 0);
}

static void do_test(int m)
{
	int i;

	switch (m) {
	case 0:
		for (i = 1; i < 200; i++)
			do_test(i);
		break;

	case 1:
		tcrypt_test("md5");
		break;

	case 2:
		tcrypt_test("sha1");
		break;

	case 3:
		tcrypt_test("ecb(des)");
		tcrypt_test("cbc(des)");
		break;

	case 4:
		tcrypt_test("ecb(des3_ede)");
		tcrypt_test("cbc(des3_ede)");
		break;

	case 5:
		tcrypt_test("md4");
		break;

	case 6:
		tcrypt_test("sha256");
		break;

	case 7:
		tcrypt_test("ecb(blowfish)");
		tcrypt_test("cbc(blowfish)");
		break;

	case 8:
		tcrypt_test("ecb(twofish)");
		tcrypt_test("cbc(twofish)");
		break;

	case 9:
		tcrypt_test("ecb(serpent)");
		break;

	case 10:
		tcrypt_test("ecb(aes)");
		tcrypt_test("cbc(aes)");
		tcrypt_test("lrw(aes)");
		tcrypt_test("xts(aes)");
		tcrypt_test("rfc3686(ctr(aes))");
		break;

	case 11:
		tcrypt_test("sha384");
		break;

	case 12:
		tcrypt_test("sha512");
		break;

	case 13:
		tcrypt_test("deflate");
		break;

	case 14:
		tcrypt_test("ecb(cast5)");
		break;

	case 15:
		tcrypt_test("ecb(cast6)");
		break;

	case 16:
		tcrypt_test("ecb(arc4)");
		break;

	case 17:
		tcrypt_test("michael_mic");
		break;

	case 18:
		tcrypt_test("crc32c");
		break;

	case 19:
		tcrypt_test("ecb(tea)");
		break;

	case 20:
		tcrypt_test("ecb(xtea)");
		break;

	case 21:
		tcrypt_test("ecb(khazad)");
		break;

	case 22:
		tcrypt_test("wp512");
		break;

	case 23:
		tcrypt_test("wp384");
		break;

	case 24:
		tcrypt_test("wp256");
		break;

	case 25:
		tcrypt_test("ecb(tnepres)");
		break;

	case 26:
		tcrypt_test("ecb(anubis)");
		tcrypt_test("cbc(anubis)");
		break;

	case 27:
		tcrypt_test("tgr192");
		break;

	case 28:

		tcrypt_test("tgr160");
		break;

	case 29:
		tcrypt_test("tgr128");
		break;

	case 30:
		tcrypt_test("ecb(xeta)");
		break;

	case 31:
		tcrypt_test("pcbc(fcrypt)");
		break;

	case 32:
		tcrypt_test("ecb(camellia)");
		tcrypt_test("cbc(camellia)");
		break;
	case 33:
		tcrypt_test("sha224");
		break;

	case 34:
		tcrypt_test("salsa20");
		break;

	case 35:
		tcrypt_test("gcm(aes)");
		break;

	case 36:
		tcrypt_test("lzo");
		break;

	case 37:
		tcrypt_test("ccm(aes)");
		break;

	case 38:
		tcrypt_test("cts(cbc(aes))");
		break;

        case 39:
		tcrypt_test("rmd128");
		break;

        case 40:
		tcrypt_test("rmd160");
		break;

	case 41:
		tcrypt_test("rmd256");
		break;

	case 42:
		tcrypt_test("rmd320");
		break;

	case 43:
		tcrypt_test("ecb(seed)");
		break;

	case 100:
		tcrypt_test("hmac(md5)");
		break;

	case 101:
		tcrypt_test("hmac(sha1)");
		break;

	case 102:
		tcrypt_test("hmac(sha256)");
		break;

	case 103:
		tcrypt_test("hmac(sha384)");
		break;

	case 104:
		tcrypt_test("hmac(sha512)");
		break;

	case 105:
		tcrypt_test("hmac(sha224)");
		break;

	case 106:
		tcrypt_test("xcbc(aes)");
		break;

	case 107:
		tcrypt_test("hmac(rmd128)");
		break;

	case 108:
		tcrypt_test("hmac(rmd160)");
		break;

	case 200:
		test_cipher_speed("ecb(aes)", ENCRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("ecb(aes)", DECRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("cbc(aes)", ENCRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("cbc(aes)", DECRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("lrw(aes)", ENCRYPT, sec, NULL, 0,
				speed_template_32_40_48);
		test_cipher_speed("lrw(aes)", DECRYPT, sec, NULL, 0,
				speed_template_32_40_48);
		test_cipher_speed("xts(aes)", ENCRYPT, sec, NULL, 0,
				speed_template_32_48_64);
		test_cipher_speed("xts(aes)", DECRYPT, sec, NULL, 0,
				speed_template_32_48_64);
		break;

	case 201:
		test_cipher_speed("ecb(des3_ede)", ENCRYPT, sec,
				des3_ede_enc_tv_template, DES3_EDE_ENC_TEST_VECTORS,
				speed_template_24);
		test_cipher_speed("ecb(des3_ede)", DECRYPT, sec,
				des3_ede_enc_tv_template, DES3_EDE_ENC_TEST_VECTORS,
				speed_template_24);
		test_cipher_speed("cbc(des3_ede)", ENCRYPT, sec,
				des3_ede_enc_tv_template, DES3_EDE_ENC_TEST_VECTORS,
				speed_template_24);
		test_cipher_speed("cbc(des3_ede)", DECRYPT, sec,
				des3_ede_enc_tv_template, DES3_EDE_ENC_TEST_VECTORS,
				speed_template_24);
		break;

	case 202:
		test_cipher_speed("ecb(twofish)", ENCRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("ecb(twofish)", DECRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("cbc(twofish)", ENCRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("cbc(twofish)", DECRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		break;

	case 203:
		test_cipher_speed("ecb(blowfish)", ENCRYPT, sec, NULL, 0,
				  speed_template_8_32);
		test_cipher_speed("ecb(blowfish)", DECRYPT, sec, NULL, 0,
				  speed_template_8_32);
		test_cipher_speed("cbc(blowfish)", ENCRYPT, sec, NULL, 0,
				  speed_template_8_32);
		test_cipher_speed("cbc(blowfish)", DECRYPT, sec, NULL, 0,
				  speed_template_8_32);
		break;

	case 204:
		test_cipher_speed("ecb(des)", ENCRYPT, sec, NULL, 0,
				  speed_template_8);
		test_cipher_speed("ecb(des)", DECRYPT, sec, NULL, 0,
				  speed_template_8);
		test_cipher_speed("cbc(des)", ENCRYPT, sec, NULL, 0,
				  speed_template_8);
		test_cipher_speed("cbc(des)", DECRYPT, sec, NULL, 0,
				  speed_template_8);
		break;

	case 205:
		test_cipher_speed("ecb(camellia)", ENCRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("ecb(camellia)", DECRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("cbc(camellia)", ENCRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("cbc(camellia)", DECRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		break;

	case 206:
		test_cipher_speed("salsa20", ENCRYPT, sec, NULL, 0,
				  speed_template_16_32);
		break;

	case 300:
		/* fall through */

	case 301:
		test_hash_speed("md4", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 302:
		test_hash_speed("md5", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 303:
		test_hash_speed("sha1", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 304:
		test_hash_speed("sha256", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 305:
		test_hash_speed("sha384", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 306:
		test_hash_speed("sha512", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 307:
		test_hash_speed("wp256", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 308:
		test_hash_speed("wp384", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 309:
		test_hash_speed("wp512", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 310:
		test_hash_speed("tgr128", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 311:
		test_hash_speed("tgr160", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 312:
		test_hash_speed("tgr192", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 313:
		test_hash_speed("sha224", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 314:
		test_hash_speed("rmd128", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 315:
		test_hash_speed("rmd160", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 316:
		test_hash_speed("rmd256", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 317:
		test_hash_speed("rmd320", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 399:
		break;

	case 1000:
		test_available();
		break;
	}
}

static int __init tcrypt_mod_init(void)
{
	int err = -ENOMEM;
	int i;

	for (i = 0; i < TVMEMSIZE; i++) {
		tvmem[i] = (void *)__get_free_page(GFP_KERNEL);
		if (!tvmem[i])
			goto err_free_tv;
	}

	for (i = 0; i < XBUFSIZE; i++) {
		xbuf[i] = (void *)__get_free_page(GFP_KERNEL);
		if (!xbuf[i])
			goto err_free_xbuf;
	}

	for (i = 0; i < XBUFSIZE; i++) {
		axbuf[i] = (void *)__get_free_page(GFP_KERNEL);
		if (!axbuf[i])
			goto err_free_axbuf;
	}

	do_test(mode);

	/* We intentionaly return -EAGAIN to prevent keeping
	 * the module. It does all its work from init()
	 * and doesn't offer any runtime functionality 
	 * => we don't need it in the memory, do we?
	 *                                        -- mludvig
	 */
	err = -EAGAIN;

err_free_axbuf:
	for (i = 0; i < XBUFSIZE && axbuf[i]; i++)
		free_page((unsigned long)axbuf[i]);
err_free_xbuf:
	for (i = 0; i < XBUFSIZE && xbuf[i]; i++)
		free_page((unsigned long)xbuf[i]);
err_free_tv:
	for (i = 0; i < TVMEMSIZE && tvmem[i]; i++)
		free_page((unsigned long)tvmem[i]);

	return err;
}

/*
 * If an init function is provided, an exit function must also be provided
 * to allow module unload.
 */
static void __exit tcrypt_mod_fini(void) { }

module_init(tcrypt_mod_init);
module_exit(tcrypt_mod_fini);

module_param(mode, int, 0);
module_param(sec, uint, 0);
MODULE_PARM_DESC(sec, "Length in seconds of speed tests "
		      "(defaults to zero which uses CPU cycles instead)");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Quick & dirty crypto testing module");
MODULE_AUTHOR("James Morris <jmorris@intercode.com.au>");
