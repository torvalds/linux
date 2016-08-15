/*
 * CTS: Cipher Text Stealing mode
 *
 * COPYRIGHT (c) 2008
 * The Regents of the University of Michigan
 * ALL RIGHTS RESERVED
 *
 * Permission is granted to use, copy, create derivative works
 * and redistribute this software and such derivative works
 * for any purpose, so long as the name of The University of
 * Michigan is not used in any advertising or publicity
 * pertaining to the use of distribution of this software
 * without specific, written prior authorization.  If the
 * above copyright notice or any other identification of the
 * University of Michigan is included in any copy of any
 * portion of this software, then the disclaimer below must
 * also be included.
 *
 * THIS SOFTWARE IS PROVIDED AS IS, WITHOUT REPRESENTATION
 * FROM THE UNIVERSITY OF MICHIGAN AS TO ITS FITNESS FOR ANY
 * PURPOSE, AND WITHOUT WARRANTY BY THE UNIVERSITY OF
 * MICHIGAN OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 * WITHOUT LIMITATION THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE
 * REGENTS OF THE UNIVERSITY OF MICHIGAN SHALL NOT BE LIABLE
 * FOR ANY DAMAGES, INCLUDING SPECIAL, INDIRECT, INCIDENTAL, OR
 * CONSEQUENTIAL DAMAGES, WITH RESPECT TO ANY CLAIM ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OF THE SOFTWARE, EVEN
 * IF IT HAS BEEN OR IS HEREAFTER ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGES.
 */

/* Derived from various:
 *	Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 */

/*
 * This is the Cipher Text Stealing mode as described by
 * Section 8 of rfc2040 and referenced by rfc3962.
 * rfc3962 includes errata information in its Appendix A.
 */

#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <crypto/scatterwalk.h>
#include <linux/slab.h>

struct crypto_cts_ctx {
	struct crypto_skcipher *child;
};

struct crypto_cts_reqctx {
	struct scatterlist sg[2];
	unsigned offset;
	struct skcipher_request subreq;
};

static inline u8 *crypto_cts_reqctx_space(struct skcipher_request *req)
{
	struct crypto_cts_reqctx *rctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_cts_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher *child = ctx->child;

	return PTR_ALIGN((u8 *)(rctx + 1) + crypto_skcipher_reqsize(child),
			 crypto_skcipher_alignmask(tfm) + 1);
}

static int crypto_cts_setkey(struct crypto_skcipher *parent, const u8 *key,
			     unsigned int keylen)
{
	struct crypto_cts_ctx *ctx = crypto_skcipher_ctx(parent);
	struct crypto_skcipher *child = ctx->child;
	int err;

	crypto_skcipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(child, crypto_skcipher_get_flags(parent) &
					 CRYPTO_TFM_REQ_MASK);
	err = crypto_skcipher_setkey(child, key, keylen);
	crypto_skcipher_set_flags(parent, crypto_skcipher_get_flags(child) &
					  CRYPTO_TFM_RES_MASK);
	return err;
}

static void cts_cbc_crypt_done(struct crypto_async_request *areq, int err)
{
	struct skcipher_request *req = areq->data;

	if (err == -EINPROGRESS)
		return;

	skcipher_request_complete(req, err);
}

static int cts_cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_cts_reqctx *rctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct skcipher_request *subreq = &rctx->subreq;
	int bsize = crypto_skcipher_blocksize(tfm);
	u8 d[bsize * 2] __attribute__ ((aligned(__alignof__(u32))));
	struct scatterlist *sg;
	unsigned int offset;
	int lastn;

	offset = rctx->offset;
	lastn = req->cryptlen - offset;

	sg = scatterwalk_ffwd(rctx->sg, req->dst, offset - bsize);
	scatterwalk_map_and_copy(d + bsize, sg, 0, bsize, 0);

	memset(d, 0, bsize);
	scatterwalk_map_and_copy(d, req->src, offset, lastn, 0);

	scatterwalk_map_and_copy(d, sg, 0, bsize + lastn, 1);
	memzero_explicit(d, sizeof(d));

	skcipher_request_set_callback(subreq, req->base.flags &
					      CRYPTO_TFM_REQ_MAY_BACKLOG,
				      cts_cbc_crypt_done, req);
	skcipher_request_set_crypt(subreq, sg, sg, bsize, req->iv);
	return crypto_skcipher_encrypt(subreq);
}

static void crypto_cts_encrypt_done(struct crypto_async_request *areq, int err)
{
	struct skcipher_request *req = areq->data;

	if (err)
		goto out;

	err = cts_cbc_encrypt(req);
	if (err == -EINPROGRESS ||
	    (err == -EBUSY && req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
		return;

out:
	skcipher_request_complete(req, err);
}

static int crypto_cts_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_cts_reqctx *rctx = skcipher_request_ctx(req);
	struct crypto_cts_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_request *subreq = &rctx->subreq;
	int bsize = crypto_skcipher_blocksize(tfm);
	unsigned int nbytes = req->cryptlen;
	int cbc_blocks = (nbytes + bsize - 1) / bsize - 1;
	unsigned int offset;

	skcipher_request_set_tfm(subreq, ctx->child);

	if (cbc_blocks <= 0) {
		skcipher_request_set_callback(subreq, req->base.flags,
					      req->base.complete,
					      req->base.data);
		skcipher_request_set_crypt(subreq, req->src, req->dst, nbytes,
					   req->iv);
		return crypto_skcipher_encrypt(subreq);
	}

	offset = cbc_blocks * bsize;
	rctx->offset = offset;

	skcipher_request_set_callback(subreq, req->base.flags,
				      crypto_cts_encrypt_done, req);
	skcipher_request_set_crypt(subreq, req->src, req->dst,
				   offset, req->iv);

	return crypto_skcipher_encrypt(subreq) ?:
	       cts_cbc_encrypt(req);
}

static int cts_cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_cts_reqctx *rctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct skcipher_request *subreq = &rctx->subreq;
	int bsize = crypto_skcipher_blocksize(tfm);
	u8 d[bsize * 2] __attribute__ ((aligned(__alignof__(u32))));
	struct scatterlist *sg;
	unsigned int offset;
	u8 *space;
	int lastn;

	offset = rctx->offset;
	lastn = req->cryptlen - offset;

	sg = scatterwalk_ffwd(rctx->sg, req->dst, offset - bsize);

	/* 1. Decrypt Cn-1 (s) to create Dn */
	scatterwalk_map_and_copy(d + bsize, sg, 0, bsize, 0);
	space = crypto_cts_reqctx_space(req);
	crypto_xor(d + bsize, space, bsize);
	/* 2. Pad Cn with zeros at the end to create C of length BB */
	memset(d, 0, bsize);
	scatterwalk_map_and_copy(d, req->src, offset, lastn, 0);
	/* 3. Exclusive-or Dn with C to create Xn */
	/* 4. Select the first Ln bytes of Xn to create Pn */
	crypto_xor(d + bsize, d, lastn);

	/* 5. Append the tail (BB - Ln) bytes of Xn to Cn to create En */
	memcpy(d + lastn, d + bsize + lastn, bsize - lastn);
	/* 6. Decrypt En to create Pn-1 */

	scatterwalk_map_and_copy(d, sg, 0, bsize + lastn, 1);
	memzero_explicit(d, sizeof(d));

	skcipher_request_set_callback(subreq, req->base.flags &
					      CRYPTO_TFM_REQ_MAY_BACKLOG,
				      cts_cbc_crypt_done, req);

	skcipher_request_set_crypt(subreq, sg, sg, bsize, space);
	return crypto_skcipher_decrypt(subreq);
}

static void crypto_cts_decrypt_done(struct crypto_async_request *areq, int err)
{
	struct skcipher_request *req = areq->data;

	if (err)
		goto out;

	err = cts_cbc_decrypt(req);
	if (err == -EINPROGRESS ||
	    (err == -EBUSY && req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
		return;

out:
	skcipher_request_complete(req, err);
}

static int crypto_cts_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_cts_reqctx *rctx = skcipher_request_ctx(req);
	struct crypto_cts_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_request *subreq = &rctx->subreq;
	int bsize = crypto_skcipher_blocksize(tfm);
	unsigned int nbytes = req->cryptlen;
	int cbc_blocks = (nbytes + bsize - 1) / bsize - 1;
	unsigned int offset;
	u8 *space;

	skcipher_request_set_tfm(subreq, ctx->child);

	if (cbc_blocks <= 0) {
		skcipher_request_set_callback(subreq, req->base.flags,
					      req->base.complete,
					      req->base.data);
		skcipher_request_set_crypt(subreq, req->src, req->dst, nbytes,
					   req->iv);
		return crypto_skcipher_decrypt(subreq);
	}

	skcipher_request_set_callback(subreq, req->base.flags,
				      crypto_cts_decrypt_done, req);

	space = crypto_cts_reqctx_space(req);

	offset = cbc_blocks * bsize;
	rctx->offset = offset;

	if (cbc_blocks <= 1)
		memcpy(space, req->iv, bsize);
	else
		scatterwalk_map_and_copy(space, req->src, offset - 2 * bsize,
					 bsize, 0);

	skcipher_request_set_crypt(subreq, req->src, req->dst,
				   offset, req->iv);

	return crypto_skcipher_decrypt(subreq) ?:
	       cts_cbc_decrypt(req);
}

static int crypto_cts_init_tfm(struct crypto_skcipher *tfm)
{
	struct skcipher_instance *inst = skcipher_alg_instance(tfm);
	struct crypto_skcipher_spawn *spawn = skcipher_instance_ctx(inst);
	struct crypto_cts_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher *cipher;
	unsigned reqsize;
	unsigned bsize;
	unsigned align;

	cipher = crypto_spawn_skcipher2(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;

	align = crypto_skcipher_alignmask(tfm);
	bsize = crypto_skcipher_blocksize(cipher);
	reqsize = ALIGN(sizeof(struct crypto_cts_reqctx) +
			crypto_skcipher_reqsize(cipher),
			crypto_tfm_ctx_alignment()) +
		  (align & ~(crypto_tfm_ctx_alignment() - 1)) + bsize;

	crypto_skcipher_set_reqsize(tfm, reqsize);

	return 0;
}

static void crypto_cts_exit_tfm(struct crypto_skcipher *tfm)
{
	struct crypto_cts_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_free_skcipher(ctx->child);
}

static void crypto_cts_free(struct skcipher_instance *inst)
{
	crypto_drop_skcipher(skcipher_instance_ctx(inst));
	kfree(inst);
}

static int crypto_cts_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct crypto_skcipher_spawn *spawn;
	struct skcipher_instance *inst;
	struct crypto_attr_type *algt;
	struct skcipher_alg *alg;
	const char *cipher_name;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	if ((algt->type ^ CRYPTO_ALG_TYPE_SKCIPHER) & algt->mask)
		return -EINVAL;

	cipher_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(cipher_name))
		return PTR_ERR(cipher_name);

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	spawn = skcipher_instance_ctx(inst);

	crypto_set_skcipher_spawn(spawn, skcipher_crypto_instance(inst));
	err = crypto_grab_skcipher2(spawn, cipher_name, 0,
				    crypto_requires_sync(algt->type,
							 algt->mask));
	if (err)
		goto err_free_inst;

	alg = crypto_spawn_skcipher_alg(spawn);

	err = -EINVAL;
	if (crypto_skcipher_alg_ivsize(alg) != alg->base.cra_blocksize)
		goto err_drop_spawn;

	if (strncmp(alg->base.cra_name, "cbc(", 4))
		goto err_drop_spawn;

	err = crypto_inst_setname(skcipher_crypto_instance(inst), "cts",
				  &alg->base);
	if (err)
		goto err_drop_spawn;

	inst->alg.base.cra_flags = alg->base.cra_flags & CRYPTO_ALG_ASYNC;
	inst->alg.base.cra_priority = alg->base.cra_priority;
	inst->alg.base.cra_blocksize = alg->base.cra_blocksize;
	inst->alg.base.cra_alignmask = alg->base.cra_alignmask;

	/* We access the data as u32s when xoring. */
	inst->alg.base.cra_alignmask |= __alignof__(u32) - 1;

	inst->alg.ivsize = alg->base.cra_blocksize;
	inst->alg.chunksize = crypto_skcipher_alg_chunksize(alg);
	inst->alg.min_keysize = crypto_skcipher_alg_min_keysize(alg);
	inst->alg.max_keysize = crypto_skcipher_alg_max_keysize(alg);

	inst->alg.base.cra_ctxsize = sizeof(struct crypto_cts_ctx);

	inst->alg.init = crypto_cts_init_tfm;
	inst->alg.exit = crypto_cts_exit_tfm;

	inst->alg.setkey = crypto_cts_setkey;
	inst->alg.encrypt = crypto_cts_encrypt;
	inst->alg.decrypt = crypto_cts_decrypt;

	inst->free = crypto_cts_free;

	err = skcipher_register_instance(tmpl, inst);
	if (err)
		goto err_drop_spawn;

out:
	return err;

err_drop_spawn:
	crypto_drop_skcipher(spawn);
err_free_inst:
	kfree(inst);
	goto out;
}

static struct crypto_template crypto_cts_tmpl = {
	.name = "cts",
	.create = crypto_cts_create,
	.module = THIS_MODULE,
};

static int __init crypto_cts_module_init(void)
{
	return crypto_register_template(&crypto_cts_tmpl);
}

static void __exit crypto_cts_module_exit(void)
{
	crypto_unregister_template(&crypto_cts_tmpl);
}

module_init(crypto_cts_module_init);
module_exit(crypto_cts_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("CTS-CBC CipherText Stealing for CBC");
MODULE_ALIAS_CRYPTO("cts");
