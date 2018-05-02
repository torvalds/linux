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
 *	sg_init_one(&sg, ptdata, ptlen);
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
 *	sg_init_one(&sg, ctdata, ctlen);
 *	skcipher_request_set_crypt(req, &sg, &sg, ptlen, iv);
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

struct crypto_kw_ctx {
	struct crypto_cipher *child;
};

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

static int crypto_kw_decrypt(struct blkcipher_desc *desc,
			     struct scatterlist *dst, struct scatterlist *src,
			     unsigned int nbytes)
{
	struct crypto_blkcipher *tfm = desc->tfm;
	struct crypto_kw_ctx *ctx = crypto_blkcipher_ctx(tfm);
	struct crypto_cipher *child = ctx->child;
	struct crypto_kw_block block;
	struct scatterlist *lsrc, *ldst;
	u64 t = 6 * ((nbytes) >> 3);
	unsigned int i;
	int ret = 0;

	/*
	 * Require at least 2 semiblocks (note, the 3rd semiblock that is
	 * required by SP800-38F is the IV.
	 */
	if (nbytes < (2 * SEMIBSIZE) || nbytes % SEMIBSIZE)
		return -EINVAL;

	/* Place the IV into block A */
	memcpy(&block.A, desc->info, SEMIBSIZE);

	/*
	 * src scatterlist is read-only. dst scatterlist is r/w. During the
	 * first loop, lsrc points to src and ldst to dst. For any
	 * subsequent round, the code operates on dst only.
	 */
	lsrc = src;
	ldst = dst;

	for (i = 0; i < 6; i++) {
		struct scatter_walk src_walk, dst_walk;
		unsigned int tmp_nbytes = nbytes;

		while (tmp_nbytes) {
			/* move pointer by tmp_nbytes in the SGL */
			crypto_kw_scatterlist_ff(&src_walk, lsrc, tmp_nbytes);
			/* get the source block */
			scatterwalk_copychunks(&block.R, &src_walk, SEMIBSIZE,
					       false);

			/* perform KW operation: modify IV with counter */
			block.A ^= cpu_to_be64(t);
			t--;
			/* perform KW operation: decrypt block */
			crypto_cipher_decrypt_one(child, (u8*)&block,
						  (u8*)&block);

			/* move pointer by tmp_nbytes in the SGL */
			crypto_kw_scatterlist_ff(&dst_walk, ldst, tmp_nbytes);
			/* Copy block->R into place */
			scatterwalk_copychunks(&block.R, &dst_walk, SEMIBSIZE,
					       true);

			tmp_nbytes -= SEMIBSIZE;
		}

		/* we now start to operate on the dst SGL only */
		lsrc = dst;
		ldst = dst;
	}

	/* Perform authentication check */
	if (block.A != cpu_to_be64(0xa6a6a6a6a6a6a6a6ULL))
		ret = -EBADMSG;

	memzero_explicit(&block, sizeof(struct crypto_kw_block));

	return ret;
}

static int crypto_kw_encrypt(struct blkcipher_desc *desc,
			     struct scatterlist *dst, struct scatterlist *src,
			     unsigned int nbytes)
{
	struct crypto_blkcipher *tfm = desc->tfm;
	struct crypto_kw_ctx *ctx = crypto_blkcipher_ctx(tfm);
	struct crypto_cipher *child = ctx->child;
	struct crypto_kw_block block;
	struct scatterlist *lsrc, *ldst;
	u64 t = 1;
	unsigned int i;

	/*
	 * Require at least 2 semiblocks (note, the 3rd semiblock that is
	 * required by SP800-38F is the IV that occupies the first semiblock.
	 * This means that the dst memory must be one semiblock larger than src.
	 * Also ensure that the given data is aligned to semiblock.
	 */
	if (nbytes < (2 * SEMIBSIZE) || nbytes % SEMIBSIZE)
		return -EINVAL;

	/*
	 * Place the predefined IV into block A -- for encrypt, the caller
	 * does not need to provide an IV, but he needs to fetch the final IV.
	 */
	block.A = cpu_to_be64(0xa6a6a6a6a6a6a6a6ULL);

	/*
	 * src scatterlist is read-only. dst scatterlist is r/w. During the
	 * first loop, lsrc points to src and ldst to dst. For any
	 * subsequent round, the code operates on dst only.
	 */
	lsrc = src;
	ldst = dst;

	for (i = 0; i < 6; i++) {
		struct scatter_walk src_walk, dst_walk;
		unsigned int tmp_nbytes = nbytes;

		scatterwalk_start(&src_walk, lsrc);
		scatterwalk_start(&dst_walk, ldst);

		while (tmp_nbytes) {
			/* get the source block */
			scatterwalk_copychunks(&block.R, &src_walk, SEMIBSIZE,
					       false);

			/* perform KW operation: encrypt block */
			crypto_cipher_encrypt_one(child, (u8 *)&block,
						  (u8 *)&block);
			/* perform KW operation: modify IV with counter */
			block.A ^= cpu_to_be64(t);
			t++;

			/* Copy block->R into place */
			scatterwalk_copychunks(&block.R, &dst_walk, SEMIBSIZE,
					       true);

			tmp_nbytes -= SEMIBSIZE;
		}

		/* we now start to operate on the dst SGL only */
		lsrc = dst;
		ldst = dst;
	}

	/* establish the IV for the caller to pick up */
	memcpy(desc->info, &block.A, SEMIBSIZE);

	memzero_explicit(&block, sizeof(struct crypto_kw_block));

	return 0;
}

static int crypto_kw_setkey(struct crypto_tfm *parent, const u8 *key,
			    unsigned int keylen)
{
	struct crypto_kw_ctx *ctx = crypto_tfm_ctx(parent);
	struct crypto_cipher *child = ctx->child;
	int err;

	crypto_cipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_cipher_set_flags(child, crypto_tfm_get_flags(parent) &
				       CRYPTO_TFM_REQ_MASK);
	err = crypto_cipher_setkey(child, key, keylen);
	crypto_tfm_set_flags(parent, crypto_cipher_get_flags(child) &
				     CRYPTO_TFM_RES_MASK);
	return err;
}

static int crypto_kw_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = crypto_tfm_alg_instance(tfm);
	struct crypto_spawn *spawn = crypto_instance_ctx(inst);
	struct crypto_kw_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_cipher *cipher;

	cipher = crypto_spawn_cipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;
	return 0;
}

static void crypto_kw_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_kw_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_cipher(ctx->child);
}

static struct crypto_instance *crypto_kw_alloc(struct rtattr **tb)
{
	struct crypto_instance *inst = NULL;
	struct crypto_alg *alg = NULL;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_BLKCIPHER);
	if (err)
		return ERR_PTR(err);

	alg = crypto_get_attr_alg(tb, CRYPTO_ALG_TYPE_CIPHER,
				  CRYPTO_ALG_TYPE_MASK);
	if (IS_ERR(alg))
		return ERR_CAST(alg);

	inst = ERR_PTR(-EINVAL);
	/* Section 5.1 requirement for KW */
	if (alg->cra_blocksize != sizeof(struct crypto_kw_block))
		goto err;

	inst = crypto_alloc_instance("kw", alg);
	if (IS_ERR(inst))
		goto err;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_BLKCIPHER;
	inst->alg.cra_priority = alg->cra_priority;
	inst->alg.cra_blocksize = SEMIBSIZE;
	inst->alg.cra_alignmask = 0;
	inst->alg.cra_type = &crypto_blkcipher_type;
	inst->alg.cra_blkcipher.ivsize = SEMIBSIZE;
	inst->alg.cra_blkcipher.min_keysize = alg->cra_cipher.cia_min_keysize;
	inst->alg.cra_blkcipher.max_keysize = alg->cra_cipher.cia_max_keysize;

	inst->alg.cra_ctxsize = sizeof(struct crypto_kw_ctx);

	inst->alg.cra_init = crypto_kw_init_tfm;
	inst->alg.cra_exit = crypto_kw_exit_tfm;

	inst->alg.cra_blkcipher.setkey = crypto_kw_setkey;
	inst->alg.cra_blkcipher.encrypt = crypto_kw_encrypt;
	inst->alg.cra_blkcipher.decrypt = crypto_kw_decrypt;

err:
	crypto_mod_put(alg);
	return inst;
}

static void crypto_kw_free(struct crypto_instance *inst)
{
	crypto_drop_spawn(crypto_instance_ctx(inst));
	kfree(inst);
}

static struct crypto_template crypto_kw_tmpl = {
	.name = "kw",
	.alloc = crypto_kw_alloc,
	.free = crypto_kw_free,
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

module_init(crypto_kw_init);
module_exit(crypto_kw_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Stephan Mueller <smueller@chronox.de>");
MODULE_DESCRIPTION("Key Wrapping (RFC3394 / NIST SP800-38F)");
MODULE_ALIAS_CRYPTO("kw");
