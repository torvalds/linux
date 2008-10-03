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
 * Need to kmalloc() memory for testing.
 */
#define TVMEMSIZE	16384
#define XBUFSIZE	32768

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

static unsigned int IDX[8] = { IDX1, IDX2, IDX3, IDX4, IDX5, IDX6, IDX7, IDX8 };

/*
 * Used by test_cipher_speed()
 */
static unsigned int sec;

static int mode;
static char *xbuf;
static char *axbuf;
static char *tvmem;

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

static void test_hash(char *algo, struct hash_testvec *template,
		      unsigned int tcount)
{
	unsigned int i, j, k, temp;
	struct scatterlist sg[8];
	char result[64];
	struct crypto_ahash *tfm;
	struct ahash_request *req;
	struct tcrypt_result tresult;
	int ret;
	void *hash_buff;

	printk("\ntesting %s\n", algo);

	init_completion(&tresult.completion);

	tfm = crypto_alloc_ahash(algo, 0, 0);
	if (IS_ERR(tfm)) {
		printk("failed to load transform for %s: %ld\n", algo,
		       PTR_ERR(tfm));
		return;
	}

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		printk(KERN_ERR "failed to allocate request for %s\n", algo);
		goto out_noreq;
	}
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   tcrypt_complete, &tresult);

	for (i = 0; i < tcount; i++) {
		printk("test %u:\n", i + 1);
		memset(result, 0, 64);

		hash_buff = kzalloc(template[i].psize, GFP_KERNEL);
		if (!hash_buff)
			continue;

		memcpy(hash_buff, template[i].plaintext, template[i].psize);
		sg_init_one(&sg[0], hash_buff, template[i].psize);

		if (template[i].ksize) {
			crypto_ahash_clear_flags(tfm, ~0);
			ret = crypto_ahash_setkey(tfm, template[i].key,
						  template[i].ksize);
			if (ret) {
				printk("setkey() failed ret=%d\n", ret);
				kfree(hash_buff);
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
			printk("digest () failed ret=%d\n", ret);
			kfree(hash_buff);
			goto out;
		}

		hexdump(result, crypto_ahash_digestsize(tfm));
		printk("%s\n",
		       memcmp(result, template[i].digest,
			      crypto_ahash_digestsize(tfm)) ?
		       "fail" : "pass");
		kfree(hash_buff);
	}

	printk("testing %s across pages\n", algo);

	/* setup the dummy buffer first */
	memset(xbuf, 0, XBUFSIZE);

	j = 0;
	for (i = 0; i < tcount; i++) {
		if (template[i].np) {
			j++;
			printk("test %u:\n", j);
			memset(result, 0, 64);

			temp = 0;
			sg_init_table(sg, template[i].np);
			for (k = 0; k < template[i].np; k++) {
				memcpy(&xbuf[IDX[k]],
				       template[i].plaintext + temp,
				       template[i].tap[k]);
				temp += template[i].tap[k];
				sg_set_buf(&sg[k], &xbuf[IDX[k]],
					    template[i].tap[k]);
			}

			if (template[i].ksize) {
				crypto_ahash_clear_flags(tfm, ~0);
				ret = crypto_ahash_setkey(tfm, template[i].key,
							  template[i].ksize);

				if (ret) {
					printk("setkey() failed ret=%d\n", ret);
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
				printk("digest () failed ret=%d\n", ret);
				goto out;
			}

			hexdump(result, crypto_ahash_digestsize(tfm));
			printk("%s\n",
			       memcmp(result, template[i].digest,
				      crypto_ahash_digestsize(tfm)) ?
			       "fail" : "pass");
		}
	}

out:
	ahash_request_free(req);
out_noreq:
	crypto_free_ahash(tfm);
}

static void test_aead(char *algo, int enc, struct aead_testvec *template,
		      unsigned int tcount)
{
	unsigned int ret, i, j, k, n, temp;
	char *q;
	struct crypto_aead *tfm;
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

	printk(KERN_INFO "\ntesting %s %s\n", algo, e);

	init_completion(&result.completion);

	tfm = crypto_alloc_aead(algo, 0, 0);

	if (IS_ERR(tfm)) {
		printk(KERN_INFO "failed to load transform for %s: %ld\n",
		       algo, PTR_ERR(tfm));
		return;
	}

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		printk(KERN_INFO "failed to allocate request for %s\n", algo);
		goto out;
	}

	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  tcrypt_complete, &result);

	for (i = 0, j = 0; i < tcount; i++) {
		if (!template[i].np) {
			printk(KERN_INFO "test %u (%d bit key):\n",
			       ++j, template[i].klen * 8);

			/* some tepmplates have no input data but they will
			 * touch input
			 */
			input = kzalloc(template[i].ilen + template[i].rlen, GFP_KERNEL);
			if (!input)
				continue;

			assoc = kzalloc(template[i].alen, GFP_KERNEL);
			if (!assoc) {
				kfree(input);
				continue;
			}

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

			if (template[i].key)
				key = template[i].key;
			else
				key = kzalloc(template[i].klen, GFP_KERNEL);

			ret = crypto_aead_setkey(tfm, key,
						 template[i].klen);
			if (ret) {
				printk(KERN_INFO "setkey() failed flags=%x\n",
				       crypto_aead_get_flags(tfm));

				if (!template[i].fail)
					goto next_one;
			}

			authsize = abs(template[i].rlen - template[i].ilen);
			ret = crypto_aead_setauthsize(tfm, authsize);
			if (ret) {
				printk(KERN_INFO
				       "failed to set authsize = %u\n",
				       authsize);
				goto next_one;
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
				printk(KERN_INFO "%s () failed err=%d\n",
				       e, -ret);
				goto next_one;
			}

			q = input;
			hexdump(q, template[i].rlen);

			printk(KERN_INFO "enc/dec: %s\n",
			       memcmp(q, template[i].result,
				      template[i].rlen) ? "fail" : "pass");
next_one:
			if (!template[i].key)
				kfree(key);
			kfree(assoc);
			kfree(input);
		}
	}

	printk(KERN_INFO "\ntesting %s %s across pages (chunking)\n", algo, e);
	memset(axbuf, 0, XBUFSIZE);

	for (i = 0, j = 0; i < tcount; i++) {
		if (template[i].np) {
			printk(KERN_INFO "test %u (%d bit key):\n",
			       ++j, template[i].klen * 8);

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
			if (ret) {
				printk(KERN_INFO "setkey() failed flags=%x\n",
				       crypto_aead_get_flags(tfm));

				if (!template[i].fail)
					goto out;
			}

			memset(xbuf, 0, XBUFSIZE);
			sg_init_table(sg, template[i].np);
			for (k = 0, temp = 0; k < template[i].np; k++) {
				memcpy(&xbuf[IDX[k]],
				       template[i].input + temp,
				       template[i].tap[k]);
				temp += template[i].tap[k];
				sg_set_buf(&sg[k], &xbuf[IDX[k]],
					   template[i].tap[k]);
			}

			authsize = abs(template[i].rlen - template[i].ilen);
			ret = crypto_aead_setauthsize(tfm, authsize);
			if (ret) {
				printk(KERN_INFO
				       "failed to set authsize = %u\n",
				       authsize);
				goto out;
			}

			if (enc)
				sg[k - 1].length += authsize;

			sg_init_table(asg, template[i].anp);
			for (k = 0, temp = 0; k < template[i].anp; k++) {
				memcpy(&axbuf[IDX[k]],
				       template[i].assoc + temp,
				       template[i].atap[k]);
				temp += template[i].atap[k];
				sg_set_buf(&asg[k], &axbuf[IDX[k]],
					   template[i].atap[k]);
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
				printk(KERN_INFO "%s () failed err=%d\n",
				       e, -ret);
				goto out;
			}

			for (k = 0, temp = 0; k < template[i].np; k++) {
				printk(KERN_INFO "page %u\n", k);
				q = &xbuf[IDX[k]];

				n = template[i].tap[k];
				if (k == template[i].np - 1)
					n += enc ? authsize : -authsize;
				hexdump(q, n);
				printk(KERN_INFO "%s\n",
				       memcmp(q, template[i].result + temp, n) ?
				       "fail" : "pass");

				q += n;
				if (k == template[i].np - 1 && !enc) {
					if (memcmp(q, template[i].input +
						      temp + n, authsize))
						n = authsize;
					else
						n = 0;
				} else {
					for (n = 0; q[n]; n++)
						;
				}
				if (n) {
					printk("Result buffer corruption %u "
					       "bytes:\n", n);
					hexdump(q, n);
				}

				temp += template[i].tap[k];
			}
		}
	}

out:
	crypto_free_aead(tfm);
	aead_request_free(req);
}

static void test_cipher(char *algo, int enc,
			struct cipher_testvec *template, unsigned int tcount)
{
	unsigned int ret, i, j, k, n, temp;
	char *q;
	struct crypto_ablkcipher *tfm;
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

	printk("\ntesting %s %s\n", algo, e);

	init_completion(&result.completion);
	tfm = crypto_alloc_ablkcipher(algo, 0, 0);

	if (IS_ERR(tfm)) {
		printk("failed to load transform for %s: %ld\n", algo,
		       PTR_ERR(tfm));
		return;
	}

	req = ablkcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		printk("failed to allocate request for %s\n", algo);
		goto out;
	}

	ablkcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					tcrypt_complete, &result);

	j = 0;
	for (i = 0; i < tcount; i++) {

		data = kzalloc(template[i].ilen, GFP_KERNEL);
		if (!data)
			continue;

		memcpy(data, template[i].input, template[i].ilen);
		if (template[i].iv)
			memcpy(iv, template[i].iv, MAX_IVLEN);
		else
			memset(iv, 0, MAX_IVLEN);

		if (!(template[i].np)) {
			j++;
			printk("test %u (%d bit key):\n",
			j, template[i].klen * 8);

			crypto_ablkcipher_clear_flags(tfm, ~0);
			if (template[i].wk)
				crypto_ablkcipher_set_flags(
					tfm, CRYPTO_TFM_REQ_WEAK_KEY);

			ret = crypto_ablkcipher_setkey(tfm, template[i].key,
						       template[i].klen);
			if (ret) {
				printk("setkey() failed flags=%x\n",
				       crypto_ablkcipher_get_flags(tfm));

				if (!template[i].fail) {
					kfree(data);
					goto out;
				}
			}

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
				printk("%s () failed err=%d\n", e, -ret);
				kfree(data);
				goto out;
			}

			q = data;
			hexdump(q, template[i].rlen);

			printk("%s\n",
			       memcmp(q, template[i].result,
				      template[i].rlen) ? "fail" : "pass");
		}
		kfree(data);
	}

	printk("\ntesting %s %s across pages (chunking)\n", algo, e);

	j = 0;
	for (i = 0; i < tcount; i++) {

		if (template[i].iv)
			memcpy(iv, template[i].iv, MAX_IVLEN);
		else
			memset(iv, 0, MAX_IVLEN);

		if (template[i].np) {
			j++;
			printk("test %u (%d bit key):\n",
			j, template[i].klen * 8);

			memset(xbuf, 0, XBUFSIZE);
			crypto_ablkcipher_clear_flags(tfm, ~0);
			if (template[i].wk)
				crypto_ablkcipher_set_flags(
					tfm, CRYPTO_TFM_REQ_WEAK_KEY);

			ret = crypto_ablkcipher_setkey(tfm, template[i].key,
						       template[i].klen);
			if (ret) {
				printk("setkey() failed flags=%x\n",
						crypto_ablkcipher_get_flags(tfm));

				if (!template[i].fail)
					goto out;
			}

			temp = 0;
			sg_init_table(sg, template[i].np);
			for (k = 0; k < template[i].np; k++) {
				memcpy(&xbuf[IDX[k]],
						template[i].input + temp,
						template[i].tap[k]);
				temp += template[i].tap[k];
				sg_set_buf(&sg[k], &xbuf[IDX[k]],
						template[i].tap[k]);
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
				printk("%s () failed err=%d\n", e, -ret);
				goto out;
			}

			temp = 0;
			for (k = 0; k < template[i].np; k++) {
				printk("page %u\n", k);
				q = &xbuf[IDX[k]];
				hexdump(q, template[i].tap[k]);
				printk("%s\n",
					memcmp(q, template[i].result + temp,
						template[i].tap[k]) ? "fail" :
					"pass");

				for (n = 0; q[template[i].tap[k] + n]; n++)
					;
				if (n) {
					printk("Result buffer corruption %u "
					       "bytes:\n", n);
					hexdump(&q[template[i].tap[k]], n);
				}
				temp += template[i].tap[k];
			}
		}
	}
out:
	crypto_free_ablkcipher(tfm);
	ablkcipher_request_free(req);
}

static int test_cipher_jiffies(struct blkcipher_desc *desc, int enc, char *p,
			       int blen, int sec)
{
	struct scatterlist sg[1];
	unsigned long start, end;
	int bcount;
	int ret;

	sg_init_one(sg, p, blen);

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

static int test_cipher_cycles(struct blkcipher_desc *desc, int enc, char *p,
			      int blen)
{
	struct scatterlist sg[1];
	unsigned long cycles = 0;
	int ret = 0;
	int i;

	sg_init_one(sg, p, blen);

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

static void test_cipher_speed(char *algo, int enc, unsigned int sec,
			      struct cipher_testvec *template,
			      unsigned int tcount, u8 *keysize)
{
	unsigned int ret, i, j, iv_len;
	unsigned char *key, *p, iv[128];
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

			if ((*keysize + *b_size) > TVMEMSIZE) {
				printk("template (%u) too big for tvmem (%u)\n",
						*keysize + *b_size, TVMEMSIZE);
				goto out;
			}

			printk("test %u (%d bit key, %d byte blocks): ", i,
					*keysize * 8, *b_size);

			memset(tvmem, 0xff, *keysize + *b_size);

			/* set key, plain text and IV */
			key = (unsigned char *)tvmem;
			for (j = 0; j < tcount; j++) {
				if (template[j].klen == *keysize) {
					key = template[j].key;
					break;
				}
			}
			p = (unsigned char *)tvmem + *keysize;

			ret = crypto_blkcipher_setkey(tfm, key, *keysize);
			if (ret) {
				printk("setkey() failed flags=%x\n",
						crypto_blkcipher_get_flags(tfm));
				goto out;
			}

			iv_len = crypto_blkcipher_ivsize(tfm);
			if (iv_len) {
				memset(&iv, 0xff, iv_len);
				crypto_blkcipher_set_iv(tfm, iv, iv_len);
			}

			if (sec)
				ret = test_cipher_jiffies(&desc, enc, p, *b_size, sec);
			else
				ret = test_cipher_cycles(&desc, enc, p, *b_size);

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

static int test_hash_jiffies_digest(struct hash_desc *desc, char *p, int blen,
				    char *out, int sec)
{
	struct scatterlist sg[1];
	unsigned long start, end;
	int bcount;
	int ret;

	sg_init_table(sg, 1);

	for (start = jiffies, end = start + sec * HZ, bcount = 0;
	     time_before(jiffies, end); bcount++) {
		sg_set_buf(sg, p, blen);
		ret = crypto_hash_digest(desc, sg, blen, out);
		if (ret)
			return ret;
	}

	printk("%6u opers/sec, %9lu bytes/sec\n",
	       bcount / sec, ((long)bcount * blen) / sec);

	return 0;
}

static int test_hash_jiffies(struct hash_desc *desc, char *p, int blen,
			     int plen, char *out, int sec)
{
	struct scatterlist sg[1];
	unsigned long start, end;
	int bcount, pcount;
	int ret;

	if (plen == blen)
		return test_hash_jiffies_digest(desc, p, blen, out, sec);

	sg_init_table(sg, 1);

	for (start = jiffies, end = start + sec * HZ, bcount = 0;
	     time_before(jiffies, end); bcount++) {
		ret = crypto_hash_init(desc);
		if (ret)
			return ret;
		for (pcount = 0; pcount < blen; pcount += plen) {
			sg_set_buf(sg, p + pcount, plen);
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

static int test_hash_cycles_digest(struct hash_desc *desc, char *p, int blen,
				   char *out)
{
	struct scatterlist sg[1];
	unsigned long cycles = 0;
	int i;
	int ret;

	sg_init_table(sg, 1);

	local_bh_disable();
	local_irq_disable();

	/* Warm-up run. */
	for (i = 0; i < 4; i++) {
		sg_set_buf(sg, p, blen);
		ret = crypto_hash_digest(desc, sg, blen, out);
		if (ret)
			goto out;
	}

	/* The real thing. */
	for (i = 0; i < 8; i++) {
		cycles_t start, end;

		start = get_cycles();

		sg_set_buf(sg, p, blen);
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

static int test_hash_cycles(struct hash_desc *desc, char *p, int blen,
			    int plen, char *out)
{
	struct scatterlist sg[1];
	unsigned long cycles = 0;
	int i, pcount;
	int ret;

	if (plen == blen)
		return test_hash_cycles_digest(desc, p, blen, out);

	sg_init_table(sg, 1);

	local_bh_disable();
	local_irq_disable();

	/* Warm-up run. */
	for (i = 0; i < 4; i++) {
		ret = crypto_hash_init(desc);
		if (ret)
			goto out;
		for (pcount = 0; pcount < blen; pcount += plen) {
			sg_set_buf(sg, p + pcount, plen);
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
			sg_set_buf(sg, p + pcount, plen);
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

static void test_hash_speed(char *algo, unsigned int sec,
			      struct hash_speed *speed)
{
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

	for (i = 0; speed[i].blen != 0; i++) {
		if (speed[i].blen > TVMEMSIZE) {
			printk("template (%u) too big for tvmem (%u)\n",
			       speed[i].blen, TVMEMSIZE);
			goto out;
		}

		printk("test%3u (%5u byte blocks,%5u bytes per update,%4u updates): ",
		       i, speed[i].blen, speed[i].plen, speed[i].blen / speed[i].plen);

		memset(tvmem, 0xff, speed[i].blen);

		if (sec)
			ret = test_hash_jiffies(&desc, tvmem, speed[i].blen,
						speed[i].plen, output, sec);
		else
			ret = test_hash_cycles(&desc, tvmem, speed[i].blen,
					       speed[i].plen, output);

		if (ret) {
			printk("hashing failed ret=%d\n", ret);
			break;
		}
	}

out:
	crypto_free_hash(tfm);
}

static void test_comp(char *algo, struct comp_testvec *ctemplate,
		       struct comp_testvec *dtemplate, int ctcount, int dtcount)
{
	unsigned int i;
	char result[COMP_BUF_SIZE];
	struct crypto_comp *tfm;
	unsigned int tsize;

	printk("\ntesting %s compression\n", algo);

	tfm = crypto_alloc_comp(algo, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm)) {
		printk("failed to load transform for %s\n", algo);
		return;
	}

	for (i = 0; i < ctcount; i++) {
		int ilen, ret, dlen = COMP_BUF_SIZE;

		printk("test %u:\n", i + 1);
		memset(result, 0, sizeof (result));

		ilen = ctemplate[i].inlen;
		ret = crypto_comp_compress(tfm, ctemplate[i].input,
		                           ilen, result, &dlen);
		if (ret) {
			printk("fail: ret=%d\n", ret);
			continue;
		}
		hexdump(result, dlen);
		printk("%s (ratio %d:%d)\n",
		       memcmp(result, ctemplate[i].output, dlen) ? "fail" : "pass",
		       ilen, dlen);
	}

	printk("\ntesting %s decompression\n", algo);

	tsize = sizeof(struct comp_testvec);
	tsize *= dtcount;
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		goto out;
	}

	for (i = 0; i < dtcount; i++) {
		int ilen, ret, dlen = COMP_BUF_SIZE;

		printk("test %u:\n", i + 1);
		memset(result, 0, sizeof (result));

		ilen = dtemplate[i].inlen;
		ret = crypto_comp_decompress(tfm, dtemplate[i].input,
		                             ilen, result, &dlen);
		if (ret) {
			printk("fail: ret=%d\n", ret);
			continue;
		}
		hexdump(result, dlen);
		printk("%s (ratio %d:%d)\n",
		       memcmp(result, dtemplate[i].output, dlen) ? "fail" : "pass",
		       ilen, dlen);
	}
out:
	crypto_free_comp(tfm);
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

static void do_test(void)
{
	switch (mode) {

	case 0:
		test_hash("md5", md5_tv_template, MD5_TEST_VECTORS);

		test_hash("sha1", sha1_tv_template, SHA1_TEST_VECTORS);

		//DES
		test_cipher("ecb(des)", ENCRYPT, des_enc_tv_template,
			    DES_ENC_TEST_VECTORS);
		test_cipher("ecb(des)", DECRYPT, des_dec_tv_template,
			    DES_DEC_TEST_VECTORS);
		test_cipher("cbc(des)", ENCRYPT, des_cbc_enc_tv_template,
			    DES_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(des)", DECRYPT, des_cbc_dec_tv_template,
			    DES_CBC_DEC_TEST_VECTORS);

		//DES3_EDE
		test_cipher("ecb(des3_ede)", ENCRYPT, des3_ede_enc_tv_template,
			    DES3_EDE_ENC_TEST_VECTORS);
		test_cipher("ecb(des3_ede)", DECRYPT, des3_ede_dec_tv_template,
			    DES3_EDE_DEC_TEST_VECTORS);

		test_cipher("cbc(des3_ede)", ENCRYPT,
			    des3_ede_cbc_enc_tv_template,
			    DES3_EDE_CBC_ENC_TEST_VECTORS);

		test_cipher("cbc(des3_ede)", DECRYPT,
			    des3_ede_cbc_dec_tv_template,
			    DES3_EDE_CBC_DEC_TEST_VECTORS);

		test_hash("md4", md4_tv_template, MD4_TEST_VECTORS);

		test_hash("sha224", sha224_tv_template, SHA224_TEST_VECTORS);

		test_hash("sha256", sha256_tv_template, SHA256_TEST_VECTORS);

		//BLOWFISH
		test_cipher("ecb(blowfish)", ENCRYPT, bf_enc_tv_template,
			    BF_ENC_TEST_VECTORS);
		test_cipher("ecb(blowfish)", DECRYPT, bf_dec_tv_template,
			    BF_DEC_TEST_VECTORS);
		test_cipher("cbc(blowfish)", ENCRYPT, bf_cbc_enc_tv_template,
			    BF_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(blowfish)", DECRYPT, bf_cbc_dec_tv_template,
			    BF_CBC_DEC_TEST_VECTORS);

		//TWOFISH
		test_cipher("ecb(twofish)", ENCRYPT, tf_enc_tv_template,
			    TF_ENC_TEST_VECTORS);
		test_cipher("ecb(twofish)", DECRYPT, tf_dec_tv_template,
			    TF_DEC_TEST_VECTORS);
		test_cipher("cbc(twofish)", ENCRYPT, tf_cbc_enc_tv_template,
			    TF_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(twofish)", DECRYPT, tf_cbc_dec_tv_template,
			    TF_CBC_DEC_TEST_VECTORS);

		//SERPENT
		test_cipher("ecb(serpent)", ENCRYPT, serpent_enc_tv_template,
			    SERPENT_ENC_TEST_VECTORS);
		test_cipher("ecb(serpent)", DECRYPT, serpent_dec_tv_template,
			    SERPENT_DEC_TEST_VECTORS);

		//TNEPRES
		test_cipher("ecb(tnepres)", ENCRYPT, tnepres_enc_tv_template,
			    TNEPRES_ENC_TEST_VECTORS);
		test_cipher("ecb(tnepres)", DECRYPT, tnepres_dec_tv_template,
			    TNEPRES_DEC_TEST_VECTORS);

		//AES
		test_cipher("ecb(aes)", ENCRYPT, aes_enc_tv_template,
			    AES_ENC_TEST_VECTORS);
		test_cipher("ecb(aes)", DECRYPT, aes_dec_tv_template,
			    AES_DEC_TEST_VECTORS);
		test_cipher("cbc(aes)", ENCRYPT, aes_cbc_enc_tv_template,
			    AES_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(aes)", DECRYPT, aes_cbc_dec_tv_template,
			    AES_CBC_DEC_TEST_VECTORS);
		test_cipher("lrw(aes)", ENCRYPT, aes_lrw_enc_tv_template,
			    AES_LRW_ENC_TEST_VECTORS);
		test_cipher("lrw(aes)", DECRYPT, aes_lrw_dec_tv_template,
			    AES_LRW_DEC_TEST_VECTORS);
		test_cipher("xts(aes)", ENCRYPT, aes_xts_enc_tv_template,
			    AES_XTS_ENC_TEST_VECTORS);
		test_cipher("xts(aes)", DECRYPT, aes_xts_dec_tv_template,
			    AES_XTS_DEC_TEST_VECTORS);
		test_cipher("rfc3686(ctr(aes))", ENCRYPT, aes_ctr_enc_tv_template,
			    AES_CTR_ENC_TEST_VECTORS);
		test_cipher("rfc3686(ctr(aes))", DECRYPT, aes_ctr_dec_tv_template,
			    AES_CTR_DEC_TEST_VECTORS);
		test_aead("gcm(aes)", ENCRYPT, aes_gcm_enc_tv_template,
			  AES_GCM_ENC_TEST_VECTORS);
		test_aead("gcm(aes)", DECRYPT, aes_gcm_dec_tv_template,
			  AES_GCM_DEC_TEST_VECTORS);
		test_aead("ccm(aes)", ENCRYPT, aes_ccm_enc_tv_template,
			  AES_CCM_ENC_TEST_VECTORS);
		test_aead("ccm(aes)", DECRYPT, aes_ccm_dec_tv_template,
			  AES_CCM_DEC_TEST_VECTORS);

		//CAST5
		test_cipher("ecb(cast5)", ENCRYPT, cast5_enc_tv_template,
			    CAST5_ENC_TEST_VECTORS);
		test_cipher("ecb(cast5)", DECRYPT, cast5_dec_tv_template,
			    CAST5_DEC_TEST_VECTORS);

		//CAST6
		test_cipher("ecb(cast6)", ENCRYPT, cast6_enc_tv_template,
			    CAST6_ENC_TEST_VECTORS);
		test_cipher("ecb(cast6)", DECRYPT, cast6_dec_tv_template,
			    CAST6_DEC_TEST_VECTORS);

		//ARC4
		test_cipher("ecb(arc4)", ENCRYPT, arc4_enc_tv_template,
			    ARC4_ENC_TEST_VECTORS);
		test_cipher("ecb(arc4)", DECRYPT, arc4_dec_tv_template,
			    ARC4_DEC_TEST_VECTORS);

		//TEA
		test_cipher("ecb(tea)", ENCRYPT, tea_enc_tv_template,
			    TEA_ENC_TEST_VECTORS);
		test_cipher("ecb(tea)", DECRYPT, tea_dec_tv_template,
			    TEA_DEC_TEST_VECTORS);


		//XTEA
		test_cipher("ecb(xtea)", ENCRYPT, xtea_enc_tv_template,
			    XTEA_ENC_TEST_VECTORS);
		test_cipher("ecb(xtea)", DECRYPT, xtea_dec_tv_template,
			    XTEA_DEC_TEST_VECTORS);

		//KHAZAD
		test_cipher("ecb(khazad)", ENCRYPT, khazad_enc_tv_template,
			    KHAZAD_ENC_TEST_VECTORS);
		test_cipher("ecb(khazad)", DECRYPT, khazad_dec_tv_template,
			    KHAZAD_DEC_TEST_VECTORS);

		//ANUBIS
		test_cipher("ecb(anubis)", ENCRYPT, anubis_enc_tv_template,
			    ANUBIS_ENC_TEST_VECTORS);
		test_cipher("ecb(anubis)", DECRYPT, anubis_dec_tv_template,
			    ANUBIS_DEC_TEST_VECTORS);
		test_cipher("cbc(anubis)", ENCRYPT, anubis_cbc_enc_tv_template,
			    ANUBIS_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(anubis)", DECRYPT, anubis_cbc_dec_tv_template,
			    ANUBIS_CBC_ENC_TEST_VECTORS);

		//XETA
		test_cipher("ecb(xeta)", ENCRYPT, xeta_enc_tv_template,
			    XETA_ENC_TEST_VECTORS);
		test_cipher("ecb(xeta)", DECRYPT, xeta_dec_tv_template,
			    XETA_DEC_TEST_VECTORS);

		//FCrypt
		test_cipher("pcbc(fcrypt)", ENCRYPT, fcrypt_pcbc_enc_tv_template,
			    FCRYPT_ENC_TEST_VECTORS);
		test_cipher("pcbc(fcrypt)", DECRYPT, fcrypt_pcbc_dec_tv_template,
			    FCRYPT_DEC_TEST_VECTORS);

		//CAMELLIA
		test_cipher("ecb(camellia)", ENCRYPT,
			    camellia_enc_tv_template,
			    CAMELLIA_ENC_TEST_VECTORS);
		test_cipher("ecb(camellia)", DECRYPT,
			    camellia_dec_tv_template,
			    CAMELLIA_DEC_TEST_VECTORS);
		test_cipher("cbc(camellia)", ENCRYPT,
			    camellia_cbc_enc_tv_template,
			    CAMELLIA_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(camellia)", DECRYPT,
			    camellia_cbc_dec_tv_template,
			    CAMELLIA_CBC_DEC_TEST_VECTORS);

		//SEED
		test_cipher("ecb(seed)", ENCRYPT, seed_enc_tv_template,
			    SEED_ENC_TEST_VECTORS);
		test_cipher("ecb(seed)", DECRYPT, seed_dec_tv_template,
			    SEED_DEC_TEST_VECTORS);

		//CTS
		test_cipher("cts(cbc(aes))", ENCRYPT, cts_mode_enc_tv_template,
			    CTS_MODE_ENC_TEST_VECTORS);
		test_cipher("cts(cbc(aes))", DECRYPT, cts_mode_dec_tv_template,
			    CTS_MODE_DEC_TEST_VECTORS);

		test_hash("sha384", sha384_tv_template, SHA384_TEST_VECTORS);
		test_hash("sha512", sha512_tv_template, SHA512_TEST_VECTORS);
		test_hash("wp512", wp512_tv_template, WP512_TEST_VECTORS);
		test_hash("wp384", wp384_tv_template, WP384_TEST_VECTORS);
		test_hash("wp256", wp256_tv_template, WP256_TEST_VECTORS);
		test_hash("tgr192", tgr192_tv_template, TGR192_TEST_VECTORS);
		test_hash("tgr160", tgr160_tv_template, TGR160_TEST_VECTORS);
		test_hash("tgr128", tgr128_tv_template, TGR128_TEST_VECTORS);
		test_comp("deflate", deflate_comp_tv_template,
			  deflate_decomp_tv_template, DEFLATE_COMP_TEST_VECTORS,
			  DEFLATE_DECOMP_TEST_VECTORS);
		test_comp("lzo", lzo_comp_tv_template, lzo_decomp_tv_template,
			  LZO_COMP_TEST_VECTORS, LZO_DECOMP_TEST_VECTORS);
		test_hash("crc32c", crc32c_tv_template, CRC32C_TEST_VECTORS);
		test_hash("hmac(md5)", hmac_md5_tv_template,
			  HMAC_MD5_TEST_VECTORS);
		test_hash("hmac(sha1)", hmac_sha1_tv_template,
			  HMAC_SHA1_TEST_VECTORS);
		test_hash("hmac(sha224)", hmac_sha224_tv_template,
			  HMAC_SHA224_TEST_VECTORS);
		test_hash("hmac(sha256)", hmac_sha256_tv_template,
			  HMAC_SHA256_TEST_VECTORS);
		test_hash("hmac(sha384)", hmac_sha384_tv_template,
			  HMAC_SHA384_TEST_VECTORS);
		test_hash("hmac(sha512)", hmac_sha512_tv_template,
			  HMAC_SHA512_TEST_VECTORS);

		test_hash("xcbc(aes)", aes_xcbc128_tv_template,
			  XCBC_AES_TEST_VECTORS);

		test_hash("michael_mic", michael_mic_tv_template, MICHAEL_MIC_TEST_VECTORS);
		break;

	case 1:
		test_hash("md5", md5_tv_template, MD5_TEST_VECTORS);
		break;

	case 2:
		test_hash("sha1", sha1_tv_template, SHA1_TEST_VECTORS);
		break;

	case 3:
		test_cipher("ecb(des)", ENCRYPT, des_enc_tv_template,
			    DES_ENC_TEST_VECTORS);
		test_cipher("ecb(des)", DECRYPT, des_dec_tv_template,
			    DES_DEC_TEST_VECTORS);
		test_cipher("cbc(des)", ENCRYPT, des_cbc_enc_tv_template,
			    DES_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(des)", DECRYPT, des_cbc_dec_tv_template,
			    DES_CBC_DEC_TEST_VECTORS);
		break;

	case 4:
		test_cipher("ecb(des3_ede)", ENCRYPT, des3_ede_enc_tv_template,
			    DES3_EDE_ENC_TEST_VECTORS);
		test_cipher("ecb(des3_ede)", DECRYPT, des3_ede_dec_tv_template,
			    DES3_EDE_DEC_TEST_VECTORS);

		test_cipher("cbc(des3_ede)", ENCRYPT,
			    des3_ede_cbc_enc_tv_template,
			    DES3_EDE_CBC_ENC_TEST_VECTORS);

		test_cipher("cbc(des3_ede)", DECRYPT,
			    des3_ede_cbc_dec_tv_template,
			    DES3_EDE_CBC_DEC_TEST_VECTORS);
		break;

	case 5:
		test_hash("md4", md4_tv_template, MD4_TEST_VECTORS);
		break;

	case 6:
		test_hash("sha256", sha256_tv_template, SHA256_TEST_VECTORS);
		break;

	case 7:
		test_cipher("ecb(blowfish)", ENCRYPT, bf_enc_tv_template,
			    BF_ENC_TEST_VECTORS);
		test_cipher("ecb(blowfish)", DECRYPT, bf_dec_tv_template,
			    BF_DEC_TEST_VECTORS);
		test_cipher("cbc(blowfish)", ENCRYPT, bf_cbc_enc_tv_template,
			    BF_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(blowfish)", DECRYPT, bf_cbc_dec_tv_template,
			    BF_CBC_DEC_TEST_VECTORS);
		break;

	case 8:
		test_cipher("ecb(twofish)", ENCRYPT, tf_enc_tv_template,
			    TF_ENC_TEST_VECTORS);
		test_cipher("ecb(twofish)", DECRYPT, tf_dec_tv_template,
			    TF_DEC_TEST_VECTORS);
		test_cipher("cbc(twofish)", ENCRYPT, tf_cbc_enc_tv_template,
			    TF_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(twofish)", DECRYPT, tf_cbc_dec_tv_template,
			    TF_CBC_DEC_TEST_VECTORS);
		break;

	case 9:
		test_cipher("ecb(serpent)", ENCRYPT, serpent_enc_tv_template,
			    SERPENT_ENC_TEST_VECTORS);
		test_cipher("ecb(serpent)", DECRYPT, serpent_dec_tv_template,
			    SERPENT_DEC_TEST_VECTORS);
		break;

	case 10:
		test_cipher("ecb(aes)", ENCRYPT, aes_enc_tv_template,
			    AES_ENC_TEST_VECTORS);
		test_cipher("ecb(aes)", DECRYPT, aes_dec_tv_template,
			    AES_DEC_TEST_VECTORS);
		test_cipher("cbc(aes)", ENCRYPT, aes_cbc_enc_tv_template,
			    AES_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(aes)", DECRYPT, aes_cbc_dec_tv_template,
			    AES_CBC_DEC_TEST_VECTORS);
		test_cipher("lrw(aes)", ENCRYPT, aes_lrw_enc_tv_template,
			    AES_LRW_ENC_TEST_VECTORS);
		test_cipher("lrw(aes)", DECRYPT, aes_lrw_dec_tv_template,
			    AES_LRW_DEC_TEST_VECTORS);
		test_cipher("xts(aes)", ENCRYPT, aes_xts_enc_tv_template,
			    AES_XTS_ENC_TEST_VECTORS);
		test_cipher("xts(aes)", DECRYPT, aes_xts_dec_tv_template,
			    AES_XTS_DEC_TEST_VECTORS);
		test_cipher("rfc3686(ctr(aes))", ENCRYPT, aes_ctr_enc_tv_template,
			    AES_CTR_ENC_TEST_VECTORS);
		test_cipher("rfc3686(ctr(aes))", DECRYPT, aes_ctr_dec_tv_template,
			    AES_CTR_DEC_TEST_VECTORS);
		break;

	case 11:
		test_hash("sha384", sha384_tv_template, SHA384_TEST_VECTORS);
		break;

	case 12:
		test_hash("sha512", sha512_tv_template, SHA512_TEST_VECTORS);
		break;

	case 13:
		test_comp("deflate", deflate_comp_tv_template,
			  deflate_decomp_tv_template, DEFLATE_COMP_TEST_VECTORS,
			  DEFLATE_DECOMP_TEST_VECTORS);
		break;

	case 14:
		test_cipher("ecb(cast5)", ENCRYPT, cast5_enc_tv_template,
			    CAST5_ENC_TEST_VECTORS);
		test_cipher("ecb(cast5)", DECRYPT, cast5_dec_tv_template,
			    CAST5_DEC_TEST_VECTORS);
		break;

	case 15:
		test_cipher("ecb(cast6)", ENCRYPT, cast6_enc_tv_template,
			    CAST6_ENC_TEST_VECTORS);
		test_cipher("ecb(cast6)", DECRYPT, cast6_dec_tv_template,
			    CAST6_DEC_TEST_VECTORS);
		break;

	case 16:
		test_cipher("ecb(arc4)", ENCRYPT, arc4_enc_tv_template,
			    ARC4_ENC_TEST_VECTORS);
		test_cipher("ecb(arc4)", DECRYPT, arc4_dec_tv_template,
			    ARC4_DEC_TEST_VECTORS);
		break;

	case 17:
		test_hash("michael_mic", michael_mic_tv_template, MICHAEL_MIC_TEST_VECTORS);
		break;

	case 18:
		test_hash("crc32c", crc32c_tv_template, CRC32C_TEST_VECTORS);
		break;

	case 19:
		test_cipher("ecb(tea)", ENCRYPT, tea_enc_tv_template,
			    TEA_ENC_TEST_VECTORS);
		test_cipher("ecb(tea)", DECRYPT, tea_dec_tv_template,
			    TEA_DEC_TEST_VECTORS);
		break;

	case 20:
		test_cipher("ecb(xtea)", ENCRYPT, xtea_enc_tv_template,
			    XTEA_ENC_TEST_VECTORS);
		test_cipher("ecb(xtea)", DECRYPT, xtea_dec_tv_template,
			    XTEA_DEC_TEST_VECTORS);
		break;

	case 21:
		test_cipher("ecb(khazad)", ENCRYPT, khazad_enc_tv_template,
			    KHAZAD_ENC_TEST_VECTORS);
		test_cipher("ecb(khazad)", DECRYPT, khazad_dec_tv_template,
			    KHAZAD_DEC_TEST_VECTORS);
		break;

	case 22:
		test_hash("wp512", wp512_tv_template, WP512_TEST_VECTORS);
		break;

	case 23:
		test_hash("wp384", wp384_tv_template, WP384_TEST_VECTORS);
		break;

	case 24:
		test_hash("wp256", wp256_tv_template, WP256_TEST_VECTORS);
		break;

	case 25:
		test_cipher("ecb(tnepres)", ENCRYPT, tnepres_enc_tv_template,
			    TNEPRES_ENC_TEST_VECTORS);
		test_cipher("ecb(tnepres)", DECRYPT, tnepres_dec_tv_template,
			    TNEPRES_DEC_TEST_VECTORS);
		break;

	case 26:
		test_cipher("ecb(anubis)", ENCRYPT, anubis_enc_tv_template,
			    ANUBIS_ENC_TEST_VECTORS);
		test_cipher("ecb(anubis)", DECRYPT, anubis_dec_tv_template,
			    ANUBIS_DEC_TEST_VECTORS);
		test_cipher("cbc(anubis)", ENCRYPT, anubis_cbc_enc_tv_template,
			    ANUBIS_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(anubis)", DECRYPT, anubis_cbc_dec_tv_template,
			    ANUBIS_CBC_ENC_TEST_VECTORS);
		break;

	case 27:
		test_hash("tgr192", tgr192_tv_template, TGR192_TEST_VECTORS);
		break;

	case 28:

		test_hash("tgr160", tgr160_tv_template, TGR160_TEST_VECTORS);
		break;

	case 29:
		test_hash("tgr128", tgr128_tv_template, TGR128_TEST_VECTORS);
		break;

	case 30:
		test_cipher("ecb(xeta)", ENCRYPT, xeta_enc_tv_template,
			    XETA_ENC_TEST_VECTORS);
		test_cipher("ecb(xeta)", DECRYPT, xeta_dec_tv_template,
			    XETA_DEC_TEST_VECTORS);
		break;

	case 31:
		test_cipher("pcbc(fcrypt)", ENCRYPT, fcrypt_pcbc_enc_tv_template,
			    FCRYPT_ENC_TEST_VECTORS);
		test_cipher("pcbc(fcrypt)", DECRYPT, fcrypt_pcbc_dec_tv_template,
			    FCRYPT_DEC_TEST_VECTORS);
		break;

	case 32:
		test_cipher("ecb(camellia)", ENCRYPT,
			    camellia_enc_tv_template,
			    CAMELLIA_ENC_TEST_VECTORS);
		test_cipher("ecb(camellia)", DECRYPT,
			    camellia_dec_tv_template,
			    CAMELLIA_DEC_TEST_VECTORS);
		test_cipher("cbc(camellia)", ENCRYPT,
			    camellia_cbc_enc_tv_template,
			    CAMELLIA_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(camellia)", DECRYPT,
			    camellia_cbc_dec_tv_template,
			    CAMELLIA_CBC_DEC_TEST_VECTORS);
		break;
	case 33:
		test_hash("sha224", sha224_tv_template, SHA224_TEST_VECTORS);
		break;

	case 34:
		test_cipher("salsa20", ENCRYPT,
			    salsa20_stream_enc_tv_template,
			    SALSA20_STREAM_ENC_TEST_VECTORS);
		break;

	case 35:
		test_aead("gcm(aes)", ENCRYPT, aes_gcm_enc_tv_template,
			  AES_GCM_ENC_TEST_VECTORS);
		test_aead("gcm(aes)", DECRYPT, aes_gcm_dec_tv_template,
			  AES_GCM_DEC_TEST_VECTORS);
		break;

	case 36:
		test_comp("lzo", lzo_comp_tv_template, lzo_decomp_tv_template,
			  LZO_COMP_TEST_VECTORS, LZO_DECOMP_TEST_VECTORS);
		break;

	case 37:
		test_aead("ccm(aes)", ENCRYPT, aes_ccm_enc_tv_template,
			  AES_CCM_ENC_TEST_VECTORS);
		test_aead("ccm(aes)", DECRYPT, aes_ccm_dec_tv_template,
			  AES_CCM_DEC_TEST_VECTORS);
		break;

	case 38:
		test_cipher("cts(cbc(aes))", ENCRYPT, cts_mode_enc_tv_template,
			    CTS_MODE_ENC_TEST_VECTORS);
		test_cipher("cts(cbc(aes))", DECRYPT, cts_mode_dec_tv_template,
			    CTS_MODE_DEC_TEST_VECTORS);
		break;

        case 39:
		test_hash("rmd128", rmd128_tv_template, RMD128_TEST_VECTORS);
		break;

        case 40:
		test_hash("rmd160", rmd160_tv_template, RMD160_TEST_VECTORS);
		break;

	case 41:
		test_hash("rmd256", rmd256_tv_template, RMD256_TEST_VECTORS);
		break;

	case 42:
		test_hash("rmd320", rmd320_tv_template, RMD320_TEST_VECTORS);
		break;

	case 100:
		test_hash("hmac(md5)", hmac_md5_tv_template,
			  HMAC_MD5_TEST_VECTORS);
		break;

	case 101:
		test_hash("hmac(sha1)", hmac_sha1_tv_template,
			  HMAC_SHA1_TEST_VECTORS);
		break;

	case 102:
		test_hash("hmac(sha256)", hmac_sha256_tv_template,
			  HMAC_SHA256_TEST_VECTORS);
		break;

	case 103:
		test_hash("hmac(sha384)", hmac_sha384_tv_template,
			  HMAC_SHA384_TEST_VECTORS);
		break;

	case 104:
		test_hash("hmac(sha512)", hmac_sha512_tv_template,
			  HMAC_SHA512_TEST_VECTORS);
		break;

	case 105:
		test_hash("hmac(sha224)", hmac_sha224_tv_template,
			  HMAC_SHA224_TEST_VECTORS);
		break;

	case 106:
		test_hash("xcbc(aes)", aes_xcbc128_tv_template,
			  XCBC_AES_TEST_VECTORS);
		break;

	case 107:
		test_hash("hmac(rmd128)", hmac_rmd128_tv_template,
			  HMAC_RMD128_TEST_VECTORS);
		break;

	case 108:
		test_hash("hmac(rmd160)", hmac_rmd160_tv_template,
			  HMAC_RMD160_TEST_VECTORS);
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

	default:
		/* useful for debugging */
		printk("not testing anything\n");
		break;
	}
}

static int __init tcrypt_mod_init(void)
{
	int err = -ENOMEM;

	tvmem = kmalloc(TVMEMSIZE, GFP_KERNEL);
	if (tvmem == NULL)
		return err;

	xbuf = kmalloc(XBUFSIZE, GFP_KERNEL);
	if (xbuf == NULL)
		goto err_free_tv;

	axbuf = kmalloc(XBUFSIZE, GFP_KERNEL);
	if (axbuf == NULL)
		goto err_free_xbuf;

	do_test();

	/* We intentionaly return -EAGAIN to prevent keeping
	 * the module. It does all its work from init()
	 * and doesn't offer any runtime functionality 
	 * => we don't need it in the memory, do we?
	 *                                        -- mludvig
	 */
	err = -EAGAIN;

	kfree(axbuf);
 err_free_xbuf:
	kfree(xbuf);
 err_free_tv:
	kfree(tvmem);

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
