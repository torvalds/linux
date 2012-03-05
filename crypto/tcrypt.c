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
 * Updated RFC4106 AES-GCM testing.
 *    Authors: Aidan O'Mahony (aidan.o.mahony@intel.com)
 *             Adrian Hoban <adrian.hoban@intel.com>
 *             Gabriele Paoloni <gabriele.paoloni@intel.com>
 *             Tadeusz Struk (tadeusz.struk@intel.com)
 *             Copyright (c) 2010, Intel Corporation.
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
#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <linux/moduleparam.h>
#include <linux/jiffies.h>
#include <linux/timex.h>
#include <linux/interrupt.h>
#include "tcrypt.h"
#include "internal.h"

/*
 * Need slab memory for testing (size in number of pages).
 */
#define TVMEMSIZE	4

/*
* Used by test_cipher_speed()
*/
#define ENCRYPT 1
#define DECRYPT 0

/*
 * Used by test_cipher_speed()
 */
static unsigned int sec;

static char *alg = NULL;
static u32 type;
static u32 mask;
static int mode;
static char *tvmem[TVMEMSIZE];

static char *check[] = {
	"des", "md5", "des3_ede", "rot13", "sha1", "sha224", "sha256",
	"blowfish", "twofish", "serpent", "sha384", "sha512", "md4", "aes",
	"cast6", "arc4", "michael_mic", "deflate", "crc32c", "tea", "xtea",
	"khazad", "wp512", "wp384", "wp256", "tnepres", "xeta",  "fcrypt",
	"camellia", "seed", "salsa20", "rmd128", "rmd160", "rmd256", "rmd320",
	"lzo", "cts", "zlib", NULL
};

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
			      struct cipher_speed_template *template,
			      unsigned int tcount, u8 *keysize)
{
	unsigned int ret, i, j, iv_len;
	const char *key;
	char iv[128];
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
			key = tvmem[0];
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

static void test_hash_sg_init(struct scatterlist *sg)
{
	int i;

	sg_init_table(sg, TVMEMSIZE);
	for (i = 0; i < TVMEMSIZE; i++) {
		sg_set_buf(sg + i, tvmem[i], PAGE_SIZE);
		memset(tvmem[i], 0xff, PAGE_SIZE);
	}
}

static void test_hash_speed(const char *algo, unsigned int sec,
			    struct hash_speed *speed)
{
	struct scatterlist sg[TVMEMSIZE];
	struct crypto_hash *tfm;
	struct hash_desc desc;
	static char output[1024];
	int i;
	int ret;

	printk(KERN_INFO "\ntesting speed of %s\n", algo);

	tfm = crypto_alloc_hash(algo, 0, CRYPTO_ALG_ASYNC);

	if (IS_ERR(tfm)) {
		printk(KERN_ERR "failed to load transform for %s: %ld\n", algo,
		       PTR_ERR(tfm));
		return;
	}

	desc.tfm = tfm;
	desc.flags = 0;

	if (crypto_hash_digestsize(tfm) > sizeof(output)) {
		printk(KERN_ERR "digestsize(%u) > outputbuffer(%zu)\n",
		       crypto_hash_digestsize(tfm), sizeof(output));
		goto out;
	}

	test_hash_sg_init(sg);
	for (i = 0; speed[i].blen != 0; i++) {
		if (speed[i].blen > TVMEMSIZE * PAGE_SIZE) {
			printk(KERN_ERR
			       "template (%u) too big for tvmem (%lu)\n",
			       speed[i].blen, TVMEMSIZE * PAGE_SIZE);
			goto out;
		}

		if (speed[i].klen)
			crypto_hash_setkey(tfm, tvmem[0], speed[i].klen);

		printk(KERN_INFO "test%3u "
		       "(%5u byte blocks,%5u bytes per update,%4u updates): ",
		       i, speed[i].blen, speed[i].plen, speed[i].blen / speed[i].plen);

		if (sec)
			ret = test_hash_jiffies(&desc, sg, speed[i].blen,
						speed[i].plen, output, sec);
		else
			ret = test_hash_cycles(&desc, sg, speed[i].blen,
					       speed[i].plen, output);

		if (ret) {
			printk(KERN_ERR "hashing failed ret=%d\n", ret);
			break;
		}
	}

out:
	crypto_free_hash(tfm);
}

struct tcrypt_result {
	struct completion completion;
	int err;
};

static void tcrypt_complete(struct crypto_async_request *req, int err)
{
	struct tcrypt_result *res = req->data;

	if (err == -EINPROGRESS)
		return;

	res->err = err;
	complete(&res->completion);
}

static inline int do_one_ahash_op(struct ahash_request *req, int ret)
{
	if (ret == -EINPROGRESS || ret == -EBUSY) {
		struct tcrypt_result *tr = req->base.data;

		ret = wait_for_completion_interruptible(&tr->completion);
		if (!ret)
			ret = tr->err;
		INIT_COMPLETION(tr->completion);
	}
	return ret;
}

static int test_ahash_jiffies_digest(struct ahash_request *req, int blen,
				     char *out, int sec)
{
	unsigned long start, end;
	int bcount;
	int ret;

	for (start = jiffies, end = start + sec * HZ, bcount = 0;
	     time_before(jiffies, end); bcount++) {
		ret = do_one_ahash_op(req, crypto_ahash_digest(req));
		if (ret)
			return ret;
	}

	printk("%6u opers/sec, %9lu bytes/sec\n",
	       bcount / sec, ((long)bcount * blen) / sec);

	return 0;
}

static int test_ahash_jiffies(struct ahash_request *req, int blen,
			      int plen, char *out, int sec)
{
	unsigned long start, end;
	int bcount, pcount;
	int ret;

	if (plen == blen)
		return test_ahash_jiffies_digest(req, blen, out, sec);

	for (start = jiffies, end = start + sec * HZ, bcount = 0;
	     time_before(jiffies, end); bcount++) {
		ret = crypto_ahash_init(req);
		if (ret)
			return ret;
		for (pcount = 0; pcount < blen; pcount += plen) {
			ret = do_one_ahash_op(req, crypto_ahash_update(req));
			if (ret)
				return ret;
		}
		/* we assume there is enough space in 'out' for the result */
		ret = do_one_ahash_op(req, crypto_ahash_final(req));
		if (ret)
			return ret;
	}

	pr_cont("%6u opers/sec, %9lu bytes/sec\n",
		bcount / sec, ((long)bcount * blen) / sec);

	return 0;
}

static int test_ahash_cycles_digest(struct ahash_request *req, int blen,
				    char *out)
{
	unsigned long cycles = 0;
	int ret, i;

	/* Warm-up run. */
	for (i = 0; i < 4; i++) {
		ret = do_one_ahash_op(req, crypto_ahash_digest(req));
		if (ret)
			goto out;
	}

	/* The real thing. */
	for (i = 0; i < 8; i++) {
		cycles_t start, end;

		start = get_cycles();

		ret = do_one_ahash_op(req, crypto_ahash_digest(req));
		if (ret)
			goto out;

		end = get_cycles();

		cycles += end - start;
	}

out:
	if (ret)
		return ret;

	pr_cont("%6lu cycles/operation, %4lu cycles/byte\n",
		cycles / 8, cycles / (8 * blen));

	return 0;
}

static int test_ahash_cycles(struct ahash_request *req, int blen,
			     int plen, char *out)
{
	unsigned long cycles = 0;
	int i, pcount, ret;

	if (plen == blen)
		return test_ahash_cycles_digest(req, blen, out);

	/* Warm-up run. */
	for (i = 0; i < 4; i++) {
		ret = crypto_ahash_init(req);
		if (ret)
			goto out;
		for (pcount = 0; pcount < blen; pcount += plen) {
			ret = do_one_ahash_op(req, crypto_ahash_update(req));
			if (ret)
				goto out;
		}
		ret = do_one_ahash_op(req, crypto_ahash_final(req));
		if (ret)
			goto out;
	}

	/* The real thing. */
	for (i = 0; i < 8; i++) {
		cycles_t start, end;

		start = get_cycles();

		ret = crypto_ahash_init(req);
		if (ret)
			goto out;
		for (pcount = 0; pcount < blen; pcount += plen) {
			ret = do_one_ahash_op(req, crypto_ahash_update(req));
			if (ret)
				goto out;
		}
		ret = do_one_ahash_op(req, crypto_ahash_final(req));
		if (ret)
			goto out;

		end = get_cycles();

		cycles += end - start;
	}

out:
	if (ret)
		return ret;

	pr_cont("%6lu cycles/operation, %4lu cycles/byte\n",
		cycles / 8, cycles / (8 * blen));

	return 0;
}

static void test_ahash_speed(const char *algo, unsigned int sec,
			     struct hash_speed *speed)
{
	struct scatterlist sg[TVMEMSIZE];
	struct tcrypt_result tresult;
	struct ahash_request *req;
	struct crypto_ahash *tfm;
	static char output[1024];
	int i, ret;

	printk(KERN_INFO "\ntesting speed of async %s\n", algo);

	tfm = crypto_alloc_ahash(algo, 0, 0);
	if (IS_ERR(tfm)) {
		pr_err("failed to load transform for %s: %ld\n",
		       algo, PTR_ERR(tfm));
		return;
	}

	if (crypto_ahash_digestsize(tfm) > sizeof(output)) {
		pr_err("digestsize(%u) > outputbuffer(%zu)\n",
		       crypto_ahash_digestsize(tfm), sizeof(output));
		goto out;
	}

	test_hash_sg_init(sg);
	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("ahash request allocation failure\n");
		goto out;
	}

	init_completion(&tresult.completion);
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   tcrypt_complete, &tresult);

	for (i = 0; speed[i].blen != 0; i++) {
		if (speed[i].blen > TVMEMSIZE * PAGE_SIZE) {
			pr_err("template (%u) too big for tvmem (%lu)\n",
			       speed[i].blen, TVMEMSIZE * PAGE_SIZE);
			break;
		}

		pr_info("test%3u "
			"(%5u byte blocks,%5u bytes per update,%4u updates): ",
			i, speed[i].blen, speed[i].plen, speed[i].blen / speed[i].plen);

		ahash_request_set_crypt(req, sg, output, speed[i].plen);

		if (sec)
			ret = test_ahash_jiffies(req, speed[i].blen,
						 speed[i].plen, output, sec);
		else
			ret = test_ahash_cycles(req, speed[i].blen,
						speed[i].plen, output);

		if (ret) {
			pr_err("hashing failed ret=%d\n", ret);
			break;
		}
	}

	ahash_request_free(req);

out:
	crypto_free_ahash(tfm);
}

static inline int do_one_acipher_op(struct ablkcipher_request *req, int ret)
{
	if (ret == -EINPROGRESS || ret == -EBUSY) {
		struct tcrypt_result *tr = req->base.data;

		ret = wait_for_completion_interruptible(&tr->completion);
		if (!ret)
			ret = tr->err;
		INIT_COMPLETION(tr->completion);
	}

	return ret;
}

static int test_acipher_jiffies(struct ablkcipher_request *req, int enc,
				int blen, int sec)
{
	unsigned long start, end;
	int bcount;
	int ret;

	for (start = jiffies, end = start + sec * HZ, bcount = 0;
	     time_before(jiffies, end); bcount++) {
		if (enc)
			ret = do_one_acipher_op(req,
						crypto_ablkcipher_encrypt(req));
		else
			ret = do_one_acipher_op(req,
						crypto_ablkcipher_decrypt(req));

		if (ret)
			return ret;
	}

	pr_cont("%d operations in %d seconds (%ld bytes)\n",
		bcount, sec, (long)bcount * blen);
	return 0;
}

static int test_acipher_cycles(struct ablkcipher_request *req, int enc,
			       int blen)
{
	unsigned long cycles = 0;
	int ret = 0;
	int i;

	/* Warm-up run. */
	for (i = 0; i < 4; i++) {
		if (enc)
			ret = do_one_acipher_op(req,
						crypto_ablkcipher_encrypt(req));
		else
			ret = do_one_acipher_op(req,
						crypto_ablkcipher_decrypt(req));

		if (ret)
			goto out;
	}

	/* The real thing. */
	for (i = 0; i < 8; i++) {
		cycles_t start, end;

		start = get_cycles();
		if (enc)
			ret = do_one_acipher_op(req,
						crypto_ablkcipher_encrypt(req));
		else
			ret = do_one_acipher_op(req,
						crypto_ablkcipher_decrypt(req));
		end = get_cycles();

		if (ret)
			goto out;

		cycles += end - start;
	}

out:
	if (ret == 0)
		pr_cont("1 operation in %lu cycles (%d bytes)\n",
			(cycles + 4) / 8, blen);

	return ret;
}

static void test_acipher_speed(const char *algo, int enc, unsigned int sec,
			       struct cipher_speed_template *template,
			       unsigned int tcount, u8 *keysize)
{
	unsigned int ret, i, j, iv_len;
	struct tcrypt_result tresult;
	const char *key;
	char iv[128];
	struct ablkcipher_request *req;
	struct crypto_ablkcipher *tfm;
	const char *e;
	u32 *b_size;

	if (enc == ENCRYPT)
		e = "encryption";
	else
		e = "decryption";

	pr_info("\ntesting speed of async %s %s\n", algo, e);

	init_completion(&tresult.completion);

	tfm = crypto_alloc_ablkcipher(algo, 0, 0);

	if (IS_ERR(tfm)) {
		pr_err("failed to load transform for %s: %ld\n", algo,
		       PTR_ERR(tfm));
		return;
	}

	req = ablkcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("tcrypt: skcipher: Failed to allocate request for %s\n",
		       algo);
		goto out;
	}

	ablkcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					tcrypt_complete, &tresult);

	i = 0;
	do {
		b_size = block_sizes;

		do {
			struct scatterlist sg[TVMEMSIZE];

			if ((*keysize + *b_size) > TVMEMSIZE * PAGE_SIZE) {
				pr_err("template (%u) too big for "
				       "tvmem (%lu)\n", *keysize + *b_size,
				       TVMEMSIZE * PAGE_SIZE);
				goto out_free_req;
			}

			pr_info("test %u (%d bit key, %d byte blocks): ", i,
				*keysize * 8, *b_size);

			memset(tvmem[0], 0xff, PAGE_SIZE);

			/* set key, plain text and IV */
			key = tvmem[0];
			for (j = 0; j < tcount; j++) {
				if (template[j].klen == *keysize) {
					key = template[j].key;
					break;
				}
			}

			crypto_ablkcipher_clear_flags(tfm, ~0);

			ret = crypto_ablkcipher_setkey(tfm, key, *keysize);
			if (ret) {
				pr_err("setkey() failed flags=%x\n",
					crypto_ablkcipher_get_flags(tfm));
				goto out_free_req;
			}

			sg_init_table(sg, TVMEMSIZE);
			sg_set_buf(sg, tvmem[0] + *keysize,
				   PAGE_SIZE - *keysize);
			for (j = 1; j < TVMEMSIZE; j++) {
				sg_set_buf(sg + j, tvmem[j], PAGE_SIZE);
				memset(tvmem[j], 0xff, PAGE_SIZE);
			}

			iv_len = crypto_ablkcipher_ivsize(tfm);
			if (iv_len)
				memset(&iv, 0xff, iv_len);

			ablkcipher_request_set_crypt(req, sg, sg, *b_size, iv);

			if (sec)
				ret = test_acipher_jiffies(req, enc,
							   *b_size, sec);
			else
				ret = test_acipher_cycles(req, enc,
							  *b_size);

			if (ret) {
				pr_err("%s() failed flags=%x\n", e,
					crypto_ablkcipher_get_flags(tfm));
				break;
			}
			b_size++;
			i++;
		} while (*b_size);
		keysize++;
	} while (*keysize);

out_free_req:
	ablkcipher_request_free(req);
out:
	crypto_free_ablkcipher(tfm);
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

static inline int tcrypt_test(const char *alg)
{
	int ret;

	ret = alg_test(alg, alg, 0, 0);
	/* non-fips algs return -EINVAL in fips mode */
	if (fips_enabled && ret == -EINVAL)
		ret = 0;
	return ret;
}

static int do_test(int m)
{
	int i;
	int ret = 0;

	switch (m) {
	case 0:
		for (i = 1; i < 200; i++)
			ret += do_test(i);
		break;

	case 1:
		ret += tcrypt_test("md5");
		break;

	case 2:
		ret += tcrypt_test("sha1");
		break;

	case 3:
		ret += tcrypt_test("ecb(des)");
		ret += tcrypt_test("cbc(des)");
		break;

	case 4:
		ret += tcrypt_test("ecb(des3_ede)");
		ret += tcrypt_test("cbc(des3_ede)");
		break;

	case 5:
		ret += tcrypt_test("md4");
		break;

	case 6:
		ret += tcrypt_test("sha256");
		break;

	case 7:
		ret += tcrypt_test("ecb(blowfish)");
		ret += tcrypt_test("cbc(blowfish)");
		ret += tcrypt_test("ctr(blowfish)");
		break;

	case 8:
		ret += tcrypt_test("ecb(twofish)");
		ret += tcrypt_test("cbc(twofish)");
		ret += tcrypt_test("ctr(twofish)");
		ret += tcrypt_test("lrw(twofish)");
		ret += tcrypt_test("xts(twofish)");
		break;

	case 9:
		ret += tcrypt_test("ecb(serpent)");
		ret += tcrypt_test("cbc(serpent)");
		ret += tcrypt_test("ctr(serpent)");
		ret += tcrypt_test("lrw(serpent)");
		ret += tcrypt_test("xts(serpent)");
		break;

	case 10:
		ret += tcrypt_test("ecb(aes)");
		ret += tcrypt_test("cbc(aes)");
		ret += tcrypt_test("lrw(aes)");
		ret += tcrypt_test("xts(aes)");
		ret += tcrypt_test("ctr(aes)");
		ret += tcrypt_test("rfc3686(ctr(aes))");
		break;

	case 11:
		ret += tcrypt_test("sha384");
		break;

	case 12:
		ret += tcrypt_test("sha512");
		break;

	case 13:
		ret += tcrypt_test("deflate");
		break;

	case 14:
		ret += tcrypt_test("ecb(cast5)");
		break;

	case 15:
		ret += tcrypt_test("ecb(cast6)");
		break;

	case 16:
		ret += tcrypt_test("ecb(arc4)");
		break;

	case 17:
		ret += tcrypt_test("michael_mic");
		break;

	case 18:
		ret += tcrypt_test("crc32c");
		break;

	case 19:
		ret += tcrypt_test("ecb(tea)");
		break;

	case 20:
		ret += tcrypt_test("ecb(xtea)");
		break;

	case 21:
		ret += tcrypt_test("ecb(khazad)");
		break;

	case 22:
		ret += tcrypt_test("wp512");
		break;

	case 23:
		ret += tcrypt_test("wp384");
		break;

	case 24:
		ret += tcrypt_test("wp256");
		break;

	case 25:
		ret += tcrypt_test("ecb(tnepres)");
		break;

	case 26:
		ret += tcrypt_test("ecb(anubis)");
		ret += tcrypt_test("cbc(anubis)");
		break;

	case 27:
		ret += tcrypt_test("tgr192");
		break;

	case 28:

		ret += tcrypt_test("tgr160");
		break;

	case 29:
		ret += tcrypt_test("tgr128");
		break;

	case 30:
		ret += tcrypt_test("ecb(xeta)");
		break;

	case 31:
		ret += tcrypt_test("pcbc(fcrypt)");
		break;

	case 32:
		ret += tcrypt_test("ecb(camellia)");
		ret += tcrypt_test("cbc(camellia)");
		break;
	case 33:
		ret += tcrypt_test("sha224");
		break;

	case 34:
		ret += tcrypt_test("salsa20");
		break;

	case 35:
		ret += tcrypt_test("gcm(aes)");
		break;

	case 36:
		ret += tcrypt_test("lzo");
		break;

	case 37:
		ret += tcrypt_test("ccm(aes)");
		break;

	case 38:
		ret += tcrypt_test("cts(cbc(aes))");
		break;

        case 39:
		ret += tcrypt_test("rmd128");
		break;

        case 40:
		ret += tcrypt_test("rmd160");
		break;

	case 41:
		ret += tcrypt_test("rmd256");
		break;

	case 42:
		ret += tcrypt_test("rmd320");
		break;

	case 43:
		ret += tcrypt_test("ecb(seed)");
		break;

	case 44:
		ret += tcrypt_test("zlib");
		break;

	case 45:
		ret += tcrypt_test("rfc4309(ccm(aes))");
		break;

	case 100:
		ret += tcrypt_test("hmac(md5)");
		break;

	case 101:
		ret += tcrypt_test("hmac(sha1)");
		break;

	case 102:
		ret += tcrypt_test("hmac(sha256)");
		break;

	case 103:
		ret += tcrypt_test("hmac(sha384)");
		break;

	case 104:
		ret += tcrypt_test("hmac(sha512)");
		break;

	case 105:
		ret += tcrypt_test("hmac(sha224)");
		break;

	case 106:
		ret += tcrypt_test("xcbc(aes)");
		break;

	case 107:
		ret += tcrypt_test("hmac(rmd128)");
		break;

	case 108:
		ret += tcrypt_test("hmac(rmd160)");
		break;

	case 109:
		ret += tcrypt_test("vmac(aes)");
		break;

	case 150:
		ret += tcrypt_test("ansi_cprng");
		break;

	case 151:
		ret += tcrypt_test("rfc4106(gcm(aes))");
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
		test_cipher_speed("ctr(aes)", ENCRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("ctr(aes)", DECRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		break;

	case 201:
		test_cipher_speed("ecb(des3_ede)", ENCRYPT, sec,
				des3_speed_template, DES3_SPEED_VECTORS,
				speed_template_24);
		test_cipher_speed("ecb(des3_ede)", DECRYPT, sec,
				des3_speed_template, DES3_SPEED_VECTORS,
				speed_template_24);
		test_cipher_speed("cbc(des3_ede)", ENCRYPT, sec,
				des3_speed_template, DES3_SPEED_VECTORS,
				speed_template_24);
		test_cipher_speed("cbc(des3_ede)", DECRYPT, sec,
				des3_speed_template, DES3_SPEED_VECTORS,
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
		test_cipher_speed("ctr(twofish)", ENCRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("ctr(twofish)", DECRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("lrw(twofish)", ENCRYPT, sec, NULL, 0,
				speed_template_32_40_48);
		test_cipher_speed("lrw(twofish)", DECRYPT, sec, NULL, 0,
				speed_template_32_40_48);
		test_cipher_speed("xts(twofish)", ENCRYPT, sec, NULL, 0,
				speed_template_32_48_64);
		test_cipher_speed("xts(twofish)", DECRYPT, sec, NULL, 0,
				speed_template_32_48_64);
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
		test_cipher_speed("ctr(blowfish)", ENCRYPT, sec, NULL, 0,
				  speed_template_8_32);
		test_cipher_speed("ctr(blowfish)", DECRYPT, sec, NULL, 0,
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

	case 207:
		test_cipher_speed("ecb(serpent)", ENCRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("ecb(serpent)", DECRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("cbc(serpent)", ENCRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("cbc(serpent)", DECRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("ctr(serpent)", ENCRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("ctr(serpent)", DECRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("lrw(serpent)", ENCRYPT, sec, NULL, 0,
				  speed_template_32_48);
		test_cipher_speed("lrw(serpent)", DECRYPT, sec, NULL, 0,
				  speed_template_32_48);
		test_cipher_speed("xts(serpent)", ENCRYPT, sec, NULL, 0,
				  speed_template_32_64);
		test_cipher_speed("xts(serpent)", DECRYPT, sec, NULL, 0,
				  speed_template_32_64);
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

	case 318:
		test_hash_speed("ghash-generic", sec, hash_speed_template_16);
		if (mode > 300 && mode < 400) break;

	case 399:
		break;

	case 400:
		/* fall through */

	case 401:
		test_ahash_speed("md4", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 402:
		test_ahash_speed("md5", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 403:
		test_ahash_speed("sha1", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 404:
		test_ahash_speed("sha256", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 405:
		test_ahash_speed("sha384", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 406:
		test_ahash_speed("sha512", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 407:
		test_ahash_speed("wp256", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 408:
		test_ahash_speed("wp384", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 409:
		test_ahash_speed("wp512", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 410:
		test_ahash_speed("tgr128", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 411:
		test_ahash_speed("tgr160", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 412:
		test_ahash_speed("tgr192", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 413:
		test_ahash_speed("sha224", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 414:
		test_ahash_speed("rmd128", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 415:
		test_ahash_speed("rmd160", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 416:
		test_ahash_speed("rmd256", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 417:
		test_ahash_speed("rmd320", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 499:
		break;

	case 500:
		test_acipher_speed("ecb(aes)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("ecb(aes)", DECRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("cbc(aes)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("cbc(aes)", DECRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("lrw(aes)", ENCRYPT, sec, NULL, 0,
				   speed_template_32_40_48);
		test_acipher_speed("lrw(aes)", DECRYPT, sec, NULL, 0,
				   speed_template_32_40_48);
		test_acipher_speed("xts(aes)", ENCRYPT, sec, NULL, 0,
				   speed_template_32_48_64);
		test_acipher_speed("xts(aes)", DECRYPT, sec, NULL, 0,
				   speed_template_32_48_64);
		test_acipher_speed("ctr(aes)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("ctr(aes)", DECRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		break;

	case 501:
		test_acipher_speed("ecb(des3_ede)", ENCRYPT, sec,
				   des3_speed_template, DES3_SPEED_VECTORS,
				   speed_template_24);
		test_acipher_speed("ecb(des3_ede)", DECRYPT, sec,
				   des3_speed_template, DES3_SPEED_VECTORS,
				   speed_template_24);
		test_acipher_speed("cbc(des3_ede)", ENCRYPT, sec,
				   des3_speed_template, DES3_SPEED_VECTORS,
				   speed_template_24);
		test_acipher_speed("cbc(des3_ede)", DECRYPT, sec,
				   des3_speed_template, DES3_SPEED_VECTORS,
				   speed_template_24);
		break;

	case 502:
		test_acipher_speed("ecb(des)", ENCRYPT, sec, NULL, 0,
				   speed_template_8);
		test_acipher_speed("ecb(des)", DECRYPT, sec, NULL, 0,
				   speed_template_8);
		test_acipher_speed("cbc(des)", ENCRYPT, sec, NULL, 0,
				   speed_template_8);
		test_acipher_speed("cbc(des)", DECRYPT, sec, NULL, 0,
				   speed_template_8);
		break;

	case 503:
		test_acipher_speed("ecb(serpent)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("ecb(serpent)", DECRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("cbc(serpent)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("cbc(serpent)", DECRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("ctr(serpent)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("ctr(serpent)", DECRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("lrw(serpent)", ENCRYPT, sec, NULL, 0,
				   speed_template_32_48);
		test_acipher_speed("lrw(serpent)", DECRYPT, sec, NULL, 0,
				   speed_template_32_48);
		test_acipher_speed("xts(serpent)", ENCRYPT, sec, NULL, 0,
				   speed_template_32_64);
		test_acipher_speed("xts(serpent)", DECRYPT, sec, NULL, 0,
				   speed_template_32_64);
		break;

	case 1000:
		test_available();
		break;
	}

	return ret;
}

static int do_alg_test(const char *alg, u32 type, u32 mask)
{
	return crypto_has_alg(alg, type, mask ?: CRYPTO_ALG_TYPE_MASK) ?
	       0 : -ENOENT;
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

	if (alg)
		err = do_alg_test(alg, type, mask);
	else
		err = do_test(mode);

	if (err) {
		printk(KERN_ERR "tcrypt: one or more tests failed!\n");
		goto err_free_tv;
	}

	/* We intentionaly return -EAGAIN to prevent keeping the module,
	 * unless we're running in fips mode. It does all its work from
	 * init() and doesn't offer any runtime functionality, but in
	 * the fips case, checking for a successful load is helpful.
	 * => we don't need it in the memory, do we?
	 *                                        -- mludvig
	 */
	if (!fips_enabled)
		err = -EAGAIN;

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

module_param(alg, charp, 0);
module_param(type, uint, 0);
module_param(mask, uint, 0);
module_param(mode, int, 0);
module_param(sec, uint, 0);
MODULE_PARM_DESC(sec, "Length in seconds of speed tests "
		      "(defaults to zero which uses CPU cycles instead)");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Quick & dirty crypto testing module");
MODULE_AUTHOR("James Morris <jmorris@intercode.com.au>");
