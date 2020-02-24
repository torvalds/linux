/*
 * Key Wrapping: RFC3394 / NIST SP800-38F
 *
 * Copyright (C) 2015, Stephan Mueller <smueller@chronox.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU General Public License, in which case the provisions of the GPL2
 * are required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/*
 * Note for using key wrapping:
 *
 *	* The result of the encryption operation is the ciphertext starting
 *	  with the 2nd semiblock. The first semiblock is provided as the IV.
 *	  The IV used to start the encryption operation is the default IV.
 *
 *	* The input for the decryption is the first semiblock handed in as an
 *	  IV. The ciphertext is the data starting with the 2nd semiblock. The
 *	  return code of the decryption operation will be EBADMSG in case an
 *	  integrity error occurs.
 *
 * To obtain the full result of an encryption as expected by SP800-38F, the
 * caller must allocate a buffer of plaintext + 8 bytes:
 *
 *	unsigned int datalen = ptlen + crypto_skcipher_ivsize(tfm);
 *	u8 data[datalen];
 *	u8 *iv = data;
 *	u8 *pt = data + crypto_skcipher_ivsize(tfm);
 *		<ensure that pt contains the plaintext of size ptlen>
 *	sg_init_one(&sg, pt, ptlen);
 *	skcipher_request_set_crypt(req, &sg, &sg, ptlen, iv);
 *
 *	==> After encryption, data now contains full KW result as per SP800-38F.
 *
 * In case of decryption, ciphertext now already has the expected length
 * and must be segmented appropriately:
 *
 *	unsigned int datalen = CTLEN;
 *	u8 data[datalen];
 *		<ensure that data contains full ciphertext>
 *	u8 *iv = data;
 *	u8 *ct = data + crypto_skcipher_ivsize(tfm);
 *	unsigned int ctlen = datalen - crypto_skcipher_ivsize(tfm);
 *	sg_init_one(&sg, ct, ctlen);
 *	skcipher_request_set_crypt(req, &sg, &sg, ctlen, iv);
 *
 *	==> After decryption (which hopefully does not return EBADMSG), the ct
 *	pointer now points to the plaintext of size ctlen.
 *
 * Note 2: KWP is not implemented as this would defy in-place operation.
 *	   If somebody wants to wrap non-aligned data, he should simply pad
 *	   the input with zeros to fill it up to the 8 byte boundary.
 */

#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/skcipher.h>

struct crypto_kw_block {
#define SEMIBSIZE 8
	__be64 A;
	__be64 R;
};

/*
 * Fast forward the SGL to the "end" length minus SEMIBSIZE.
 * The start in the SGL defined by the fast-forward is returned with
 * the walk variable
 */
static void crypto_kw_scatterlist_ff(struct scatter_walk *walk,
				     struct scatterlist *sg,
				     unsigned int end)
{
	unsigned int skip = 0;

	/* The caller should only operate on full SEMIBLOCKs. */
	BUG_ON(end < SEMIBSIZE);

	skip = end - SEMIBSIZE;
	while (sg) {
		if (sg->length > skip) {
			scatterwalk_start(walk, sg);
			scatterwalk_advance(walk, skip);
			break;
		} else
			skip -= sg->length;

		sg = sg_next(sg);
	}
}

static int crypto_kw_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_cipher *cipher = skcipher_cipher_simple(tfm);
	struct crypto_kw_block block;
	struct scatterlist *src, *dst;
	u64 t = 6 * ((req->cryptlen) >> 3);
	unsigned int i;
	int ret = 0;

	/*
	 * Require at least 2 semiblocks (note, the 3rd semiblock that is
	 * required by SP800-38F is the IV.
	 */
	if (req->cryptlen < (2 * SEMIBSIZE) || req->cryptlen % SEMIBSIZE)
		return -EINVAL;

	/* Place the IV into block A */
	memcpy(&block.A, req->iv, SEMIBSIZE);

	/*
	 * src scatterlist is read-only. dst scatterlist is r/w. During the
	 * first loop, src points to req->src and dst to req->dst. For any
	 * subsequent round, the code operates on req->dst only.
	 */
	src = req->src;
	dst = req->dst;

	for (i = 0; i < 6; i++) {
		struct scatter_walk src_walk, dst_walk;
		unsigned int nbytes = req->cryptlen;

		while (nbytes) {
			/* move pointer by nbytes in the SGL */
			crypto_kw_scatterlist_ff(&src_walk, src, nbytes);
			/* get the source block */
			scatterwalk_copychunks(&block.R, &src_walk, SEMIBSIZE,
					       false);

			/* perform KW operation: modify IV with counter */
			block.A ^= cpu_to_be64(t);
			t--;
			/* perform KW operation: decrypt block */
			crypto_cipher_decrypt_one(cipher, (u8 *)&block,
						  (u8 *)&block);

			/* move pointer by nbytes in the SGL */
			crypto_kw_scatterlist_ff(&dst_walk, dst, nbytes);
			/* Copy block->R into place */
			scatterwalk_copychunks(&block.R, &dst_walk, SEMIBSIZE,
					       true);

			nbytes -= SEMIBSIZE;
		}

		/* we now start to operate on the dst SGL only */
		src = req->dst;
		dst = req->dst;
	}

	/* Perform authentication check */
	if (block.A != cpu_to_be64(0xa6a6a6a6a6a6a6a6ULL))
		ret = -EBADMSG;

	memzero_explicit(&block, sizeof(struct crypto_kw_block));

	return ret;
}

static int crypto_kw_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_cipher *cipher = skcipher_cipher_simple(tfm);
	struct crypto_kw_block block;
	struct scatterlist *src, *dst;
	u64 t = 1;
	unsigned int i;

	/*
	 * Require at least 2 semiblocks (note, the 3rd semiblock that is
	 * required by SP800-38F is the IV that occupies the first semiblock.
	 * This means that the dst memory must be one semiblock larger than src.
	 * Also ensure that the given data is aligned to semiblock.
	 */
	if (req->cryptlen < (2 * SEMIBSIZE) || req->cryptlen % SEMIBSIZE)
		return -EINVAL;

	/*
	 * Place the predefined IV into block A -- for encrypt, the caller
	 * does not need to provide an IV, but he needs to fetch the final IV.
	 */
	block.A = cpu_to_be64(0xa6a6a6a6a6a6a6a6ULL);

	/*
	 * src scatterlist is read-only. dst scatterlist is r/w. During the
	 * first loop, src points to req->src and dst to req->dst. For any
	 * subsequent round, the code operates on req->dst only.
	 */
	src = req->src;
	dst = req->dst;

	for (i = 0; i < 6; i++) {
		struct scatter_walk src_walk, dst_walk;
		unsigned int nbytes = req->cryptlen;

		scatterwalk_start(&src_walk, src);
		scatterwalk_start(&dst_walk, dst);

		while (nbytes) {
			/* get the source block */
			scatterwalk_copychunks(&block.R, &src_walk, SEMIBSIZE,
					       false);

			/* perform KW operation: encrypt block */
			crypto_cipher_encrypt_one(cipher, (u8 *)&block,
						  (u8 *)&block);
			/* perform KW operation: modify IV with counter */
			block.A ^= cpu_to_be64(t);
			t++;

			/* Copy block->R into place */
			scatterwalk_copychunks(&block.R, &dst_walk, SEMIBSIZE,
					       true);

			nbytes -= SEMIBSIZE;
		}

		/* we now start to operate on the dst SGL only */
		src = req->dst;
		dst = req->dst;
	}

	/* establish the IV for the caller to pick up */
	memcpy(req->iv, &block.A, SEMIBSIZE);

	memzero_explicit(&block, sizeof(struct crypto_kw_block));

	return 0;
}

static int crypto_kw_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct skcipher_instance *inst;
	struct crypto_alg *alg;
	int err;

	inst = skcipher_alloc_instance_simple(tmpl, tb);
	if (IS_ERR(inst))
		return PTR_ERR(inst);

	alg = skcipher_ialg_simple(inst);

	err = -EINVAL;
	/* Section 5.1 requirement for KW */
	if (alg->cra_blocksize != sizeof(struct crypto_kw_block))
		goto out_free_inst;

	inst->alg.base.cra_blocksize = SEMIBSIZE;
	inst->alg.base.cra_alignmask = 0;
	inst->alg.ivsize = SEMIBSIZE;

	inst->alg.encrypt = crypto_kw_encrypt;
	inst->alg.decrypt = crypto_kw_decrypt;

	err = skcipher_register_instance(tmpl, inst);
	if (err) {
out_free_inst:
		inst->free(inst);
	}

	return err;
}

static struct crypto_template crypto_kw_tmpl = {
	.name = "kw",
	.create = crypto_kw_create,
	.module = THIS_MODULE,
};

static int __init crypto_kw_init(void)
{
	return crypto_register_template(&crypto_kw_tmpl);
}

static void __exit crypto_kw_exit(void)
{
	crypto_unregister_template(&crypto_kw_tmpl);
}

subsys_initcall(crypto_kw_init);
module_exit(crypto_kw_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Stephan Mueller <smueller@chronox.de>");
MODULE_DESCRIPTION("Key Wrapping (RFC3394 / NIST SP800-38F)");
MODULE_ALIAS_CRYPTO("kw");
