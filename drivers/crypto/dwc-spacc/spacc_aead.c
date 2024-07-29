// SPDX-License-Identifier: GPL-2.0

#include <crypto/aes.h>
#include <crypto/sm4.h>
#include <crypto/gcm.h>
#include <crypto/aead.h>
#include <crypto/authenc.h>
#include <linux/rtnetlink.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/aead.h>
#include <linux/platform_device.h>

#include "spacc_device.h"
#include "spacc_core.h"

static LIST_HEAD(spacc_aead_alg_list);
static DEFINE_MUTEX(spacc_aead_alg_mutex);

#define SPACC_B0_SIZE		16
#define SET_IV_IN_SRCBUF	0x80000000
#define SET_IV_IN_CONTEXT	0x0
#define AAD_BUF_SIZE		4096
#define ADATA_BUF_SIZE		(AAD_BUF_SIZE + SPACC_B0_SIZE +\
				 SPACC_MAX_IV_SIZE)

struct spacc_iv_buf {
	unsigned char iv[SPACC_MAX_IV_SIZE];
	unsigned char spacc_adata[ADATA_BUF_SIZE];
	struct scatterlist sg[2], spacc_adata_sg[2];
	struct scatterlist *spacc_ptextsg, temp_aad[2];
};

static struct kmem_cache *spacc_iv_pool;

static struct mode_tab possible_aeads[] = {
	{ MODE_TAB_AEAD("rfc7539(chacha20,poly1305)",
			CRYPTO_MODE_CHACHA20_POLY1305, CRYPTO_MODE_NULL,
			16, 12, 1), .keylen = { 16, 24, 32 }
	},
	{ MODE_TAB_AEAD("gcm(aes)",
			CRYPTO_MODE_AES_GCM, CRYPTO_MODE_NULL,
			16, 12, 1), .keylen = { 16, 24, 32 }
	},
	{ MODE_TAB_AEAD("gcm(sm4)",
			CRYPTO_MODE_SM4_GCM, CRYPTO_MODE_NULL,
			16, 12, 1), .keylen = { 16 }
	},
	{ MODE_TAB_AEAD("ccm(aes)",
			CRYPTO_MODE_AES_CCM, CRYPTO_MODE_NULL,
			16, 16, 1), .keylen = { 16, 24, 32 }
	},
	{ MODE_TAB_AEAD("ccm(sm4)",
			CRYPTO_MODE_SM4_CCM, CRYPTO_MODE_NULL,
			16, 16, 1), .keylen = { 16, 24, 32 }
	},
};

static void spacc_init_aead_alg(struct crypto_alg *calg,
				const struct mode_tab *mode)
{
	strscpy(calg->cra_name, mode->name, sizeof(mode->name) - 1);
	calg->cra_name[sizeof(mode->name) - 1] = '\0';

	strscpy(calg->cra_driver_name, "spacc-");
	strcat(calg->cra_driver_name, mode->name);
	calg->cra_driver_name[sizeof(calg->cra_driver_name) - 1] = '\0';

	calg->cra_blocksize = mode->blocklen;
}

static int ccm_16byte_aligned_len(int in_len)
{
	int len;
	int computed_mod;

	if (in_len > 0) {
		computed_mod = in_len % 16;
		if (computed_mod)
			len = in_len - computed_mod + 16;
		else
			len = in_len;
	} else {
		len = in_len;
	}

	return len;
}

/* taken from crypto/ccm.c */
static int spacc_aead_format_adata(u8 *adata, unsigned int a)
{
	int len = 0;

	/* add control info for associated data
	 * RFC 3610 and NIST Special Publication 800-38C
	 */
	if (a < 65280) {
		*(__be16 *)adata = cpu_to_be16(a);
		len = 2;
	} else  {
		*(__be16 *)adata = cpu_to_be16(0xfffe);
		*(__be32 *)&adata[2] = cpu_to_be32(a);
		len = 6;
	}

	return len;
}


/* taken from crypto/ccm.c */
static int spacc_aead_set_msg_len(u8 *block, unsigned int msglen, int csize)
{
	__be32 data;

	memset(block, 0, csize);
	block += csize;

	if (csize >= 4)
		csize = 4;
	else if (msglen > (unsigned int)(1 << (8 * csize)))
		return -EOVERFLOW;

	data = cpu_to_be32(msglen);
	memcpy(block - csize, (u8 *)&data + 4 - csize, csize);

	return 0;
}

static int spacc_aead_init_dma(struct device *dev, struct aead_request *req,
			       u64 seq, uint32_t icvlen, int encrypt, int *alen)
{
	struct crypto_aead *reqtfm      = crypto_aead_reqtfm(req);
	struct spacc_crypto_ctx *tctx   = crypto_aead_ctx(reqtfm);
	struct spacc_crypto_reqctx *ctx = aead_request_ctx(req);

	gfp_t mflags = GFP_ATOMIC;
	struct spacc_iv_buf *iv;
	int ccm_aad_16b_len = 0;
	int rc, B0len;
	int payload_len, spacc_adata_sg_buf_len;
	unsigned int ivsize = crypto_aead_ivsize(reqtfm);

	/* always have 1 byte of IV */
	if (!ivsize)
		ivsize = 1;

	if (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP)
		mflags = GFP_KERNEL;

	ctx->iv_buf = kmem_cache_alloc(spacc_iv_pool, mflags);
	if (!ctx->iv_buf)
		return -ENOMEM;
	iv = ctx->iv_buf;

	sg_init_table(iv->sg, ARRAY_SIZE(iv->sg));
	sg_init_table(iv->spacc_adata_sg, ARRAY_SIZE(iv->spacc_adata_sg));

	B0len = 0;
	ctx->aead_nents = 0;

	memset(iv->iv, 0, SPACC_MAX_IV_SIZE);
	memset(iv->spacc_adata, 0, ADATA_BUF_SIZE);

	/* copy the IV out for AAD */
	memcpy(iv->iv, req->iv, ivsize);
	memset(iv->spacc_adata, 0, 144);

	/* now we need to figure out the cipher IV which may or
	 * may not be "req->iv" depending on the mode we are in
	 */
	if (tctx->mode & SPACC_MANGLE_IV_FLAG) {
		switch (tctx->mode & 0x7F00) {
		case SPACC_MANGLE_IV_RFC3686:
		case SPACC_MANGLE_IV_RFC4106:
		case SPACC_MANGLE_IV_RFC4543:
			{
				unsigned char *p = iv->spacc_adata;
				/* we're in RFC3686 mode so the last
				 * 4 bytes of the key are the SALT
				 */
				memcpy(p, tctx->csalt, 4);
				memcpy(p + 4, req->iv, ivsize);

				p[12] = 0;
				p[13] = 0;
				p[14] = 0;
				p[15] = 1;
			}
			break;
		case SPACC_MANGLE_IV_RFC4309:
			{
				unsigned char *p = iv->spacc_adata;
				int L, M;
				u32 lm = req->cryptlen;

				/* CCM mode */
				/* p[0..15] is the CTR IV */
				/* p[16..31] is the CBC-MAC B0 block*/
				B0len = SPACC_B0_SIZE;
				/* IPsec requires L=4*/
				L = 4;
				M = tctx->auth_size;

				/* CTR block */
				p[0] = L - 1;
				memcpy(p + 1, tctx->csalt, 3);
				memcpy(p + 4, req->iv, ivsize);
				p[12] = 0;
				p[13] = 0;
				p[14] = 0;
				p[15] = 1;

				/* store B0 block at p[16..31] */
				p[16] = (1 << 6) | (((M - 2) >> 1) << 3)
					| (L - 1);
				memcpy(p + 1 + 16, tctx->csalt, 3);
				memcpy(p + 4 + 16, req->iv, ivsize);

				/* now store length */
				p[16 + 12 + 0] = (lm >> 24) & 0xFF;
				p[16 + 12 + 1] = (lm >> 16) & 0xFF;
				p[16 + 12 + 2] = (lm >> 8) & 0xFF;
				p[16 + 12 + 3] = (lm) & 0xFF;

				/*now store the pre-formatted AAD */
				p[32] = (req->assoclen >> 8) & 0xFF;
				p[33] = (req->assoclen) & 0xFF;
				/* we added 2 byte header to the AAD */
				B0len += 2;
			}
			break;
		}
	} else if (tctx->mode == CRYPTO_MODE_AES_CCM ||
		   tctx->mode == CRYPTO_MODE_SM4_CCM) {
		unsigned char *p = iv->spacc_adata;
		u8 *orig_iv = req->iv;
		int L, M;

		u32 lm = (encrypt) ?
			 req->cryptlen :
			 req->cryptlen - tctx->auth_size;

		memset(iv->spacc_adata, 0, 144);
		iv->spacc_ptextsg = req->src;
		/* CCM mode */
		/* p[0..15] is the CTR IV */
		/* p[16..31] is the CBC-MAC B0 block*/
		B0len = SPACC_B0_SIZE;

		/* IPsec requires L=4 */
		L = req->iv[0] + 1;
		M = tctx->auth_size;

		/* Note: rfc 3610 and NIST 800-38C require counter of
		 * zero to encrypt auth tag.
		 */
		memset(orig_iv + 15 - orig_iv[0], 0, orig_iv[0] + 1);

		/* CTR block */
		memcpy(p, req->iv, ivsize);
		memcpy(p + 16, req->iv, ivsize);

		/* Taken from ccm.c
		 * Note: rfc 3610 and NIST 800-38C require counter of
		 * zero to encrypt auth tag.
		 */

		/* Store B0 block at p[16..31] */
		p[16] |= (8 * ((M - 2) / 2));

		/* set adata if assoclen > 0 */
		if (req->assoclen)
			p[16] |= 64;

		/* now store length, this is L size starts from 16-L
		 * to 16 of B0
		 */
		spacc_aead_set_msg_len(p + 16 + 16 - L, lm, L);

		if (req->assoclen) {

			/* store pre-formatted AAD:
			 * AAD_LEN + AAD + PAD
			 */
			*alen = spacc_aead_format_adata(&p[32], req->assoclen);

			ccm_aad_16b_len =
				ccm_16byte_aligned_len(req->assoclen + *alen);

			/* Adding the rest of AAD from req->src */
			scatterwalk_map_and_copy(p + 32 + *alen,
						 req->src, 0,
						 req->assoclen, 0);

			/* Copy AAD to req->dst */
			scatterwalk_map_and_copy(p + 32 + *alen, req->dst,
						 0, req->assoclen, 1);

			iv->spacc_ptextsg = scatterwalk_ffwd(iv->temp_aad,
						       req->src, req->assoclen);
		}
		/* default is to copy the iv over since the
		 * cipher and protocol IV are the same
		 */
		memcpy(iv->spacc_adata, req->iv, ivsize);
	}

	/* this is part of the AAD */
	sg_set_buf(iv->sg, iv->iv, ivsize);

	/* GCM and CCM don't include the IV in the AAD */
	switch (tctx->mode) {
	case CRYPTO_MODE_AES_GCM_RFC4106:
	case CRYPTO_MODE_AES_GCM:
	case CRYPTO_MODE_SM4_GCM_RFC8998:
	case CRYPTO_MODE_CHACHA20_POLY1305:
	case CRYPTO_MODE_NULL:

		payload_len    = req->cryptlen + icvlen + req->assoclen;
		spacc_adata_sg_buf_len = SPACC_MAX_IV_SIZE + B0len;

		/* this is the actual IV getting fed to the core
		 * (via IV IMPORT)
		 */

		sg_set_buf(iv->spacc_adata_sg, iv->spacc_adata,
					spacc_adata_sg_buf_len);

		sg_chain(iv->spacc_adata_sg,
			 sg_nents_for_len(iv->spacc_adata_sg,
					spacc_adata_sg_buf_len) + 1, req->src);

		rc = spacc_sg_to_ddt(dev, iv->spacc_adata_sg,
				     spacc_adata_sg_buf_len + payload_len,
				     &ctx->src, DMA_TO_DEVICE);

		if (rc < 0)
			goto err_free_iv;
		ctx->aead_nents = rc;
		break;
	case CRYPTO_MODE_AES_CCM:
	case CRYPTO_MODE_AES_CCM_RFC4309:
	case CRYPTO_MODE_SM4_CCM:


		if (encrypt)
			payload_len =
				ccm_16byte_aligned_len(req->cryptlen + icvlen);
		else
			payload_len =
				ccm_16byte_aligned_len(req->cryptlen);

		spacc_adata_sg_buf_len = SPACC_MAX_IV_SIZE + B0len +
							ccm_aad_16b_len;

		/* this is the actual IV getting fed to the core (via IV IMPORT)
		 * This has CTR IV + B0 + AAD(B1, B2, ...)
		 */
		sg_set_buf(iv->spacc_adata_sg, iv->spacc_adata,
					spacc_adata_sg_buf_len);
		sg_chain(iv->spacc_adata_sg,
			 sg_nents_for_len(iv->spacc_adata_sg,
					  spacc_adata_sg_buf_len) + 1,
					  iv->spacc_ptextsg);

		rc = spacc_sg_to_ddt(dev, iv->spacc_adata_sg,
				     spacc_adata_sg_buf_len + payload_len,
				     &ctx->src, DMA_TO_DEVICE);
		if (rc < 0)
			goto err_free_iv;
		ctx->aead_nents = rc;
		break;
	default:

		/* this is the actual IV getting fed to the core (via IV IMPORT)
		 * This has CTR IV + B0 + AAD(B1, B2, ...)
		 */
		payload_len = req->cryptlen + icvlen + req->assoclen;
		spacc_adata_sg_buf_len = SPACC_MAX_IV_SIZE + B0len;
		sg_set_buf(iv->spacc_adata_sg, iv->spacc_adata,
					spacc_adata_sg_buf_len);

		sg_chain(iv->spacc_adata_sg,
			 sg_nents_for_len(iv->spacc_adata_sg,
					  spacc_adata_sg_buf_len) + 1,
					  req->src);

		rc = spacc_sg_to_ddt(dev, iv->spacc_adata_sg,
				     spacc_adata_sg_buf_len + payload_len,
				     &ctx->src, DMA_TO_DEVICE);

		if (rc < 0)
			goto err_free_iv;
		ctx->aead_nents = rc;
	}

	/* Putting in req->dst is good since it won't overwrite anything
	 * even in case of CCM this is fine condition
	 */
	if (req->dst != req->src) {
		switch (tctx->mode) {
		case CRYPTO_MODE_AES_CCM:
		case CRYPTO_MODE_AES_CCM_RFC4309:
		case CRYPTO_MODE_SM4_CCM:
			/* If req->dst buffer len is not-positive,
			 * then skip setting up of DMA
			 */
			if (req->dst->length <= 0) {
				ctx->dst_nents = 0;
				return 0;
			}

			if (encrypt)
				payload_len = req->cryptlen + icvlen +
						req->assoclen;
			else
				payload_len = req->cryptlen - tctx->auth_size +
						req->assoclen;

			/* For corner cases where PTlen=AADlen=0, we set default
			 * to 16
			 */
			rc = spacc_sg_to_ddt(dev, req->dst,
					     payload_len > 0 ? payload_len : 16,
					     &ctx->dst, DMA_FROM_DEVICE);
			if (rc < 0)
				goto err_free_src;
			ctx->dst_nents = rc;
			break;
		default:

			/* If req->dst buffer len is not-positive,
			 * then skip setting up of DMA
			 */
			if (req->dst->length <= 0) {
				ctx->dst_nents = 0;
				return 0;
			}

			if (encrypt)
				payload_len = SPACC_MAX_IV_SIZE + req->cryptlen
						+ icvlen + req->assoclen;
			else {
				payload_len = req->cryptlen - tctx->auth_size +
						req->assoclen;
				if (payload_len <= 0)
					return -EBADMSG;
			}


			rc = spacc_sg_to_ddt(dev, req->dst,
					     payload_len > 0 ? payload_len : 16,
					     &ctx->dst, DMA_FROM_DEVICE);
			if (rc < 0)
				goto err_free_src;
			ctx->dst_nents = rc;
		}
	}

	return 0;

err_free_src:
	if (ctx->aead_nents) {
		dma_unmap_sg(dev, iv->spacc_adata_sg, ctx->aead_nents,
			     DMA_TO_DEVICE);

		pdu_ddt_free(&ctx->src);
	}

err_free_iv:
	kmem_cache_free(spacc_iv_pool, ctx->iv_buf);

	return rc;
}

static void spacc_aead_cleanup_dma(struct device *dev, struct aead_request *req)
{
	struct spacc_crypto_reqctx *ctx = aead_request_ctx(req);
	struct spacc_iv_buf *iv = ctx->iv_buf;

	if (req->src != req->dst && ctx->dst_nents > 0) {
		dma_unmap_sg(dev, req->dst, ctx->dst_nents,
				DMA_FROM_DEVICE);
		pdu_ddt_free(&ctx->dst);
	}

	if (ctx->aead_nents) {
		dma_unmap_sg(dev, iv->spacc_adata_sg, ctx->aead_nents,
			     DMA_TO_DEVICE);

		pdu_ddt_free(&ctx->src);
	}

	kmem_cache_free(spacc_iv_pool, ctx->iv_buf);
}

static bool spacc_check_keylen(const struct spacc_alg *salg,
			       unsigned int keylen)
{
	unsigned int i, mask = salg->keylen_mask;

	if (mask > (1ul << ARRAY_SIZE(salg->mode->keylen)) - 1)
		return false;

	for (i = 0; mask; i++, mask >>= 1) {
		if (mask & 1 && salg->mode->keylen[i] == keylen)
			return true;
	}

	return false;
}

static void spacc_aead_cb(void *spacc, void *tfm)
{
	struct aead_cb_data *cb = tfm;
	int err = -1;
	u32 status_reg = readl(cb->spacc->regmap + SPACC_REG_STATUS);
	u32 status_ret = (status_reg >> 24) & 0x3;

	dma_sync_sg_for_cpu(cb->tctx->dev, cb->req->dst,
			    cb->ctx->dst_nents, DMA_FROM_DEVICE);

	/* ICV mismatch send bad msg */
	if (status_ret == 0x1) {
		err = -EBADMSG;
		goto REQ_DST_CP_SKIP;
	}
	err = cb->spacc->job[cb->new_handle].job_err;

REQ_DST_CP_SKIP:
	spacc_aead_cleanup_dma(cb->tctx->dev, cb->req);
	spacc_close(cb->spacc, cb->new_handle);

	/* call complete */
	aead_request_complete(cb->req, err);
}

static int spacc_aead_setkey(struct crypto_aead *tfm, const u8 *key,
			     unsigned int keylen)
{
	struct spacc_crypto_ctx *ctx  = crypto_aead_ctx(tfm);
	const struct spacc_alg  *salg = spacc_tfm_aead(&tfm->base);
	struct spacc_priv	*priv;
	struct rtattr *rta = (void *)key;
	struct crypto_authenc_key_param *param;
	unsigned int authkeylen, enckeylen;
	const unsigned char *authkey, *enckey;
	unsigned char xcbc[64];

	int err = -EINVAL;
	int singlekey = 0;

	/* are keylens valid? */
	ctx->ctx_valid = false;

	switch (ctx->mode & 0xFF) {
	case CRYPTO_MODE_SM4_GCM:
	case CRYPTO_MODE_SM4_CCM:
	case CRYPTO_MODE_NULL:
	case CRYPTO_MODE_AES_GCM:
	case CRYPTO_MODE_AES_CCM:
	case CRYPTO_MODE_CHACHA20_POLY1305:
		authkey      = key;
		authkeylen   = 0;
		enckey       = key;
		enckeylen    = keylen;
		ctx->keylen  = keylen;
		singlekey    = 1;
		goto skipover;
	}

	if (!RTA_OK(rta, keylen)		       ||
	    rta->rta_type != CRYPTO_AUTHENC_KEYA_PARAM ||
	    RTA_PAYLOAD(rta) < sizeof(*param))
		return -EINVAL;

	param	  = RTA_DATA(rta);
	enckeylen = be32_to_cpu(param->enckeylen);
	key	 += RTA_ALIGN(rta->rta_len);
	keylen	 -= RTA_ALIGN(rta->rta_len);

	if (keylen < enckeylen)
		return -EINVAL;

	authkeylen = keylen - enckeylen;

	/* enckey is at &key[authkeylen] and
	 * authkey is at &key[0]
	 */
	authkey = &key[0];
	enckey  = &key[authkeylen];

skipover:
	/* detect RFC3686/4106 and trim from enckeylen(and copy salt..) */
	if (ctx->mode & SPACC_MANGLE_IV_FLAG) {
		switch (ctx->mode & 0x7F00) {
		case SPACC_MANGLE_IV_RFC3686:
		case SPACC_MANGLE_IV_RFC4106:
		case SPACC_MANGLE_IV_RFC4543:
			memcpy(ctx->csalt, enckey + enckeylen - 4, 4);
			enckeylen -= 4;
			break;
		case SPACC_MANGLE_IV_RFC4309:
			memcpy(ctx->csalt, enckey + enckeylen - 3, 3);
			enckeylen -= 3;
			break;
		}
	}

	if (!singlekey) {
		if (authkeylen > salg->mode->hashlen) {
			dev_warn(ctx->dev, "Auth key size of %u is not valid\n",
				 authkeylen);
			return -EINVAL;
		}
	}

	if (!spacc_check_keylen(salg, enckeylen)) {
		dev_warn(ctx->dev, "Enc key size of %u is not valid\n",
			 enckeylen);
		return -EINVAL;
	}

	/* if we're already open close the handle since
	 * the size may have changed
	 */
	if (ctx->handle != -1) {
		priv = dev_get_drvdata(ctx->dev);
		spacc_close(&priv->spacc, ctx->handle);
		put_device(ctx->dev);
		ctx->handle = -1;
	}

	/* Open a handle and
	 * search all devices for an open handle
	 */
	priv = NULL;
	priv = dev_get_drvdata(salg->dev[0]);

	/* increase reference */
	ctx->dev = get_device(salg->dev[0]);

	/* check if its a valid mode ... */
	if (spacc_isenabled(&priv->spacc, salg->mode->aead.ciph & 0xFF,
				enckeylen) &&
			spacc_isenabled(&priv->spacc,
				salg->mode->aead.hash & 0xFF, authkeylen)) {
		/* try to open spacc handle */
		ctx->handle = spacc_open(&priv->spacc,
				salg->mode->aead.ciph & 0xFF,
				salg->mode->aead.hash & 0xFF,
				-1, 0, spacc_aead_cb, tfm);
	}

	if (ctx->handle < 0) {
		put_device(salg->dev[0]);
		pr_debug("Failed to open SPAcc context\n");
		return -EIO;
	}

	/* setup XCBC key */
	if (salg->mode->aead.hash == CRYPTO_MODE_MAC_XCBC) {
		err = spacc_compute_xcbc_key(&priv->spacc,
					     salg->mode->aead.hash,
					     ctx->handle, authkey,
					     authkeylen, xcbc);
		if (err < 0) {
			dev_warn(ctx->dev, "Failed to compute XCBC key: %d\n",
				 err);
			return -EIO;
		}
		authkey    = xcbc;
		authkeylen = 48;
	}

	/* handle zero key/zero len DEC condition for SM4/AES GCM mode */
	ctx->zero_key = 0;
	if (!key[0]) {
		int i, val = 0;

		for (i = 0; i < keylen ; i++)
			val += key[i];

		if (val == 0)
			ctx->zero_key = 1;
	}

	err = spacc_write_context(&priv->spacc, ctx->handle,
				  SPACC_CRYPTO_OPERATION, enckey,
				  enckeylen, NULL, 0);

	if (err) {
		dev_warn(ctx->dev,
			 "Could not write ciphering context: %d\n", err);
		return -EIO;
	}

	if (!singlekey) {
		err = spacc_write_context(&priv->spacc, ctx->handle,
					  SPACC_HASH_OPERATION, authkey,
					  authkeylen, NULL, 0);
		if (err) {
			dev_warn(ctx->dev,
				 "Could not write hashing context: %d\n", err);
			return -EIO;
		}
	}

	/* set expand key */
	spacc_set_key_exp(&priv->spacc, ctx->handle);
	ctx->ctx_valid = true;

	memset(xcbc, 0, sizeof(xcbc));

	/* copy key to ctx for fallback */
	memcpy(ctx->key, key, keylen);

	return 0;
}

static int spacc_aead_setauthsize(struct crypto_aead *tfm,
				  unsigned int authsize)
{
	struct spacc_crypto_ctx *ctx = crypto_aead_ctx(tfm);

	ctx->auth_size = authsize;

	/* taken from crypto/ccm.c */
	switch (ctx->mode) {
	case CRYPTO_MODE_SM4_GCM:
	case CRYPTO_MODE_AES_GCM:
		switch (authsize) {
		case 4:
		case 8:
		case 12:
		case 13:
		case 14:
		case 15:
		case 16:
			break;
		default:
			return -EINVAL;
		}
		break;

	case CRYPTO_MODE_AES_CCM:
	case CRYPTO_MODE_SM4_CCM:
		switch (authsize) {
		case 4:
		case 6:
		case 8:
		case 10:
		case 12:
		case 14:
		case 16:
			break;
		default:
			return -EINVAL;
		}
		break;

	case CRYPTO_MODE_CHACHA20_POLY1305:
		switch (authsize) {
		case 16:
			break;
		default:
			return -EINVAL;
		}
		break;
	}

	return 0;
}

static int spacc_aead_fallback(struct aead_request *req,
			       struct spacc_crypto_ctx *ctx, int encrypt)
{
	int ret;
	struct aead_request *subreq = aead_request_ctx(req);
	struct crypto_aead *reqtfm  = crypto_aead_reqtfm(req);
	struct aead_alg *alg	    = crypto_aead_alg(reqtfm);
	const char *aead_name	    = alg->base.cra_name;

	ctx->fb.aead = crypto_alloc_aead(aead_name, 0,
					 CRYPTO_ALG_NEED_FALLBACK |
					 CRYPTO_ALG_ASYNC);
	if (!ctx->fb.aead) {
		pr_err("Spacc aead fallback tfm is NULL!\n");
		return -EINVAL;
	}

	subreq = aead_request_alloc(ctx->fb.aead, GFP_KERNEL);
	if (!subreq)
		return -ENOMEM;

	crypto_aead_setkey(ctx->fb.aead, ctx->key, ctx->keylen);
	crypto_aead_setauthsize(ctx->fb.aead, ctx->auth_size);

	aead_request_set_tfm(subreq, ctx->fb.aead);
	aead_request_set_callback(subreq, req->base.flags,
			req->base.complete, req->base.data);
	aead_request_set_crypt(subreq, req->src, req->dst, req->cryptlen,
			req->iv);
	aead_request_set_ad(subreq, req->assoclen);

	if (encrypt)
		ret = crypto_aead_encrypt(subreq);
	else
		ret = crypto_aead_decrypt(subreq);

	aead_request_free(subreq);
	crypto_free_aead(ctx->fb.aead);
	ctx->fb.aead = NULL;

	return ret;
}

static int spacc_aead_process(struct aead_request *req, u64 seq, int encrypt)
{
	int rc;
	int B0len;
	int alen;
	u32 dstoff;
	int icvremove;
	int ivaadsize;
	int ptaadsize;
	int iv_to_context;
	int spacc_proc_len;
	u32 spacc_icv_offset = 0;
	int spacc_pre_aad_size;
	int ccm_aad_16b_len;
	struct crypto_aead *reqtfm	= crypto_aead_reqtfm(req);
	int ivsize			= crypto_aead_ivsize(reqtfm);
	struct spacc_crypto_ctx *tctx   = crypto_aead_ctx(reqtfm);
	struct spacc_crypto_reqctx *ctx = aead_request_ctx(req);
	struct spacc_priv *priv		= dev_get_drvdata(tctx->dev);

	ctx->encrypt_op = encrypt;
	alen = 0;
	ccm_aad_16b_len = 0;

	if (tctx->handle < 0 || !tctx->ctx_valid || (req->cryptlen +
				req->assoclen) > priv->max_msg_len)
		return -EINVAL;

	/* IV is programmed to context by default */
	iv_to_context = SET_IV_IN_CONTEXT;

	if (encrypt) {
		switch (tctx->mode & 0xFF) {
		case CRYPTO_MODE_AES_GCM:
		case CRYPTO_MODE_SM4_GCM:
		case CRYPTO_MODE_CHACHA20_POLY1305:
			/* For cryptlen = 0 */
			if (req->cryptlen + req->assoclen == 0)
				return spacc_aead_fallback(req, tctx, encrypt);
			break;
		case CRYPTO_MODE_AES_CCM:
		case CRYPTO_MODE_SM4_CCM:

			if (req->cryptlen + req->assoclen == 0)
				return spacc_aead_fallback(req, tctx, encrypt);

			/* verify that msglen can in fact be represented
			 * in L bytes
			 */
			/* 2 <= L <= 8, so 1 <= L' <= 7. */
			if (req->iv[0] < 1 || req->iv[0] > 7)
				return -EINVAL;

			break;
		default:
			pr_debug("Unsupported algo");
			return -EINVAL;
		}
	} else {
		/* Handle the decryption */
		switch (tctx->mode & 0xFF) {
		case CRYPTO_MODE_AES_GCM:
		case CRYPTO_MODE_SM4_GCM:
		case CRYPTO_MODE_CHACHA20_POLY1305:
			/* For assoclen = 0 */
			if (req->assoclen == 0 &&
					(req->cryptlen - tctx->auth_size == 0))
				return spacc_aead_fallback(req, tctx, encrypt);
			break;
		case CRYPTO_MODE_AES_CCM:
		case CRYPTO_MODE_SM4_CCM:

			if (req->assoclen == 0 &&
					(req->cryptlen - tctx->auth_size == 0))
				return spacc_aead_fallback(req, tctx, encrypt);
			/* 2 <= L <= 8, so 1 <= L' <= 7. */
			if (req->iv[0] < 1 || req->iv[0] > 7)
				return -EINVAL;
			break;
		default:
			pr_debug("Unsupported algo");
			return -EINVAL;
		}
	}

	icvremove = (encrypt) ? 0 : tctx->auth_size;

	rc = spacc_aead_init_dma(tctx->dev, req, seq, (encrypt) ?
			tctx->auth_size : 0, encrypt, &alen);
	if (rc < 0)
		return -EINVAL;

	if (req->assoclen)
		ccm_aad_16b_len = ccm_16byte_aligned_len(req->assoclen + alen);

	/* Note: This won't work if IV_IMPORT has been disabled */
	ctx->cb.new_handle = spacc_clone_handle(&priv->spacc, tctx->handle,
						&ctx->cb);
	if (ctx->cb.new_handle < 0) {
		spacc_aead_cleanup_dma(tctx->dev, req);
		return -EINVAL;
	}

	ctx->cb.tctx  = tctx;
	ctx->cb.ctx   = ctx;
	ctx->cb.req   = req;
	ctx->cb.spacc = &priv->spacc;

	/* Write IV to the spacc-context
	 * IV can be written to context or as part of the input src buffer
	 * IV in case of CCM is going in the input src buff.
	 * IV for GCM is written to the context.
	 */
	if (tctx->mode == CRYPTO_MODE_AES_GCM_RFC4106	||
	    tctx->mode == CRYPTO_MODE_AES_GCM		||
	    tctx->mode == CRYPTO_MODE_SM4_GCM_RFC8998	||
	    tctx->mode == CRYPTO_MODE_CHACHA20_POLY1305	||
	    tctx->mode == CRYPTO_MODE_NULL) {
		iv_to_context = SET_IV_IN_CONTEXT;
		rc = spacc_write_context(&priv->spacc, ctx->cb.new_handle,
					 SPACC_CRYPTO_OPERATION, NULL, 0,
					 req->iv, ivsize);

		if (rc < 0) {
			spacc_aead_cleanup_dma(tctx->dev, req);
			spacc_close(&priv->spacc, ctx->cb.new_handle);
			return -EINVAL;
		}
	}

	/* CCM and GCM don't include the IV in the AAD */
	if (tctx->mode == CRYPTO_MODE_AES_GCM_RFC4106	||
	    tctx->mode == CRYPTO_MODE_AES_CCM_RFC4309	||
	    tctx->mode == CRYPTO_MODE_AES_GCM		||
	    tctx->mode == CRYPTO_MODE_AES_CCM		||
	    tctx->mode == CRYPTO_MODE_SM4_CCM		||
	    tctx->mode == CRYPTO_MODE_SM4_GCM_RFC8998	||
	    tctx->mode == CRYPTO_MODE_CHACHA20_POLY1305	||
	    tctx->mode == CRYPTO_MODE_NULL) {
		ivaadsize = 0;
	} else {
		ivaadsize = ivsize;
	}

	/* CCM requires an extra block of AAD */
	if (tctx->mode == CRYPTO_MODE_AES_CCM_RFC4309 ||
	    tctx->mode == CRYPTO_MODE_AES_CCM	      ||
	    tctx->mode == CRYPTO_MODE_SM4_CCM)
		B0len = SPACC_B0_SIZE;
	else
		B0len = 0;

	/* GMAC mode uses AAD for the entire message.
	 * So does NULL cipher
	 */
	if (tctx->mode == CRYPTO_MODE_AES_GCM_RFC4543 ||
	    tctx->mode == CRYPTO_MODE_NULL) {
		if (req->cryptlen >= icvremove)
			ptaadsize = req->cryptlen - icvremove;
	} else {
		ptaadsize = 0;
	}

	/* Calculate and set the below, important parameters
	 * spacc icv offset	- spacc_icv_offset
	 * destination offset	- dstoff
	 * IV to context	- This is set for CCM, not set for GCM
	 */
	if (req->dst == req->src) {
		dstoff = ((uint32_t)(SPACC_MAX_IV_SIZE + B0len +
				     req->assoclen + ivaadsize));

		/* CCM case */
		if (tctx->mode == CRYPTO_MODE_AES_CCM_RFC4309	||
		    tctx->mode == CRYPTO_MODE_AES_CCM		||
		    tctx->mode == CRYPTO_MODE_SM4_CCM) {
			iv_to_context = SET_IV_IN_SRCBUF;
			dstoff = ((uint32_t)(SPACC_MAX_IV_SIZE + B0len +
				 ccm_aad_16b_len + ivaadsize));
		}

	} else {
		dstoff = ((uint32_t)(req->assoclen + ivaadsize));

		/* CCM case */
		if (tctx->mode == CRYPTO_MODE_AES_CCM_RFC4309	||
		    tctx->mode == CRYPTO_MODE_AES_CCM		||
		    tctx->mode == CRYPTO_MODE_SM4_CCM) {
			iv_to_context = SET_IV_IN_SRCBUF;
			dstoff = ((uint32_t)(req->assoclen + ivaadsize));

		}
	}

	/* Calculate and set the below, important parameters
	 * spacc proc_len - spacc_proc_len
	 * pre-AAD size   - spacc_pre_aad_size
	 */
	if (tctx->mode == CRYPTO_MODE_AES_CCM		||
	    tctx->mode == CRYPTO_MODE_SM4_CCM		||
	    tctx->mode == CRYPTO_MODE_AES_CCM_RFC4309	||
	    tctx->mode == CRYPTO_MODE_SM4_CCM_RFC8998) {
		spacc_proc_len = B0len + ccm_aad_16b_len
				+ req->cryptlen + ivaadsize
				- icvremove;
		spacc_pre_aad_size = B0len + ccm_aad_16b_len
				+ ivaadsize + ptaadsize;

	} else {
		spacc_proc_len = B0len + req->assoclen
				+ req->cryptlen - icvremove
				+ ivaadsize;
		spacc_pre_aad_size = B0len + req->assoclen
				+ ivaadsize + ptaadsize;
	}

	rc = spacc_set_operation(&priv->spacc,
			 ctx->cb.new_handle,
			 encrypt ? OP_ENCRYPT : OP_DECRYPT,
			 ICV_ENCRYPT_HASH, IP_ICV_APPEND,
			 spacc_icv_offset,
			 tctx->auth_size, 0);

	rc = spacc_packet_enqueue_ddt(&priv->spacc, ctx->cb.new_handle,
				      &ctx->src,
				      (req->dst == req->src) ? &ctx->src :
				      &ctx->dst, spacc_proc_len,
				      (dstoff << SPACC_OFFSET_DST_O) |
				      SPACC_MAX_IV_SIZE,
				      spacc_pre_aad_size,
				      0, iv_to_context, 0);

	if (rc < 0) {
		spacc_aead_cleanup_dma(tctx->dev, req);
		spacc_close(&priv->spacc, ctx->cb.new_handle);

		if (rc != -EBUSY) {
			dev_err(tctx->dev, "  failed to enqueue job, ERR: %d\n",
				rc);
		}

		if (!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
			return -EBUSY;

		return -EINVAL;
	}

	/* At this point the job is in flight to the engine ... remove first use
	 * so subsequent calls don't expand the key again... ideally we would
	 * pump a dummy job through the engine to pre-expand the key so that by
	 * the time setkey was done we wouldn't have to do this
	 */
	priv->spacc.job[tctx->handle].first_use  = 0;
	priv->spacc.job[tctx->handle].ctrl &= ~(1UL
			<< priv->spacc.config.ctrl_map[SPACC_CTRL_KEY_EXP]);

	return -EINPROGRESS;
}

static int spacc_aead_encrypt(struct aead_request *req)
{
	return spacc_aead_process(req, 0ULL, 1);
}

static int spacc_aead_decrypt(struct aead_request *req)
{
	return spacc_aead_process(req, 0ULL, 0);
}

static int spacc_aead_init(struct crypto_aead *tfm)
{
	struct spacc_crypto_ctx *ctx   = crypto_aead_ctx(tfm);
	const struct spacc_alg  *salg = spacc_tfm_aead(&tfm->base);

	crypto_aead_set_reqsize(tfm, sizeof(struct spacc_crypto_reqctx));

	ctx->zero_key = 0;
	ctx->fb.aead  = NULL;
	ctx->handle   = -1;
	ctx->mode     = salg->mode->aead.ciph;
	ctx->dev      = get_device(salg->dev[0]);

	return 0;
}

static void spacc_aead_exit(struct crypto_aead *tfm)
{
	struct spacc_crypto_ctx *ctx = crypto_aead_ctx(tfm);
	struct spacc_priv *priv = dev_get_drvdata(ctx->dev);

	ctx->fb.aead = NULL;
	/* close spacc handle */
	if (ctx->handle >= 0) {
		spacc_close(&priv->spacc, ctx->handle);
		ctx->handle = -1;
	}

	put_device(ctx->dev);
}

static struct aead_alg spacc_aead_algs = {
	.setkey      = spacc_aead_setkey,
	.setauthsize = spacc_aead_setauthsize,
	.encrypt     = spacc_aead_encrypt,
	.decrypt     = spacc_aead_decrypt,
	.init        = spacc_aead_init,
	.exit        = spacc_aead_exit,

	.base.cra_priority = 300,
	.base.cra_module   = THIS_MODULE,
	.base.cra_ctxsize  = sizeof(struct spacc_crypto_ctx),
	.base.cra_flags    = CRYPTO_ALG_TYPE_AEAD
			   | CRYPTO_ALG_ASYNC
			   | CRYPTO_ALG_NEED_FALLBACK
			   | CRYPTO_ALG_KERN_DRIVER_ONLY
			   | CRYPTO_ALG_OPTIONAL_KEY
};

static int spacc_register_aead(unsigned int aead_mode,
			       struct platform_device *spacc_pdev)
{
	int rc;
	struct spacc_alg *salg;

	salg = kmalloc(sizeof(*salg), GFP_KERNEL);
	if (!salg)
		return -ENOMEM;

	salg->mode	= &possible_aeads[aead_mode];
	salg->dev[0]	= &spacc_pdev->dev;
	salg->dev[1]	= NULL;
	salg->calg	= &salg->alg.aead.base;
	salg->alg.aead	= spacc_aead_algs;

	spacc_init_aead_alg(salg->calg, salg->mode);

	salg->alg.aead.ivsize		  = salg->mode->ivlen;
	salg->alg.aead.maxauthsize	  = salg->mode->hashlen;
	salg->alg.aead.base.cra_blocksize = salg->mode->blocklen;

	salg->keylen_mask = possible_aeads[aead_mode].keylen_mask;

	if (salg->mode->aead.ciph & SPACC_MANGLE_IV_FLAG) {
		switch (salg->mode->aead.ciph & 0x7F00) {
		case SPACC_MANGLE_IV_RFC3686: /*CTR*/
		case SPACC_MANGLE_IV_RFC4106: /*GCM*/
		case SPACC_MANGLE_IV_RFC4543: /*GMAC*/
		case SPACC_MANGLE_IV_RFC4309: /*CCM*/
		case SPACC_MANGLE_IV_RFC8998: /*GCM/CCM*/
			salg->alg.aead.ivsize  = 12;
			break;
		}
	}

	rc = crypto_register_aead(&salg->alg.aead);
	if (rc < 0) {
		kfree(salg);
		return rc;
	}

	dev_dbg(salg->dev[0], "Registered %s\n", salg->mode->name);

	mutex_lock(&spacc_aead_alg_mutex);
	list_add(&salg->list, &spacc_aead_alg_list);
	mutex_unlock(&spacc_aead_alg_mutex);

	return 0;
}

int probe_aeads(struct platform_device *spacc_pdev)
{
	int err;
	unsigned int x, y;
	struct spacc_priv *priv = NULL;

	size_t alloc_size = max_t(unsigned long,
			roundup_pow_of_two(sizeof(struct spacc_iv_buf)),
			dma_get_cache_alignment());

	spacc_iv_pool = kmem_cache_create("spacc-aead-iv", alloc_size,
					  alloc_size, 0, NULL);

	if (!spacc_iv_pool)
		return -ENOMEM;

	for (x = 0; x < ARRAY_SIZE(possible_aeads); x++) {
		possible_aeads[x].keylen_mask = 0;
		possible_aeads[x].valid       = 0;
	}

	/* compute cipher key masks (over all devices) */
	priv = dev_get_drvdata(&spacc_pdev->dev);

	for (x = 0; x < ARRAY_SIZE(possible_aeads); x++) {
		for (y = 0; y < ARRAY_SIZE(possible_aeads[x].keylen); y++) {
			if (spacc_isenabled(&priv->spacc,
					    possible_aeads[x].aead.ciph & 0xFF,
					possible_aeads[x].keylen[y]))
				possible_aeads[x].keylen_mask |= 1u << y;
		}
	}

	/* scan for combined modes */
	priv = dev_get_drvdata(&spacc_pdev->dev);

	for (x = 0; x < ARRAY_SIZE(possible_aeads); x++) {
		if (!possible_aeads[x].valid && possible_aeads[x].keylen_mask) {
			if (spacc_isenabled(&priv->spacc,
					    possible_aeads[x].aead.hash & 0xFF,
					possible_aeads[x].hashlen)) {

				possible_aeads[x].valid = 1;
				err = spacc_register_aead(x, spacc_pdev);
				if (err < 0)
					goto error;
			}
		}
	}

	return 0;

error:
	return err;
}

int spacc_unregister_aead_algs(void)
{
	struct spacc_alg *salg, *tmp;

	mutex_lock(&spacc_aead_alg_mutex);

	list_for_each_entry_safe(salg, tmp, &spacc_aead_alg_list, list) {
		crypto_unregister_alg(salg->calg);
		list_del(&salg->list);
		kfree(salg);
	}

	mutex_unlock(&spacc_aead_alg_mutex);

	kmem_cache_destroy(spacc_iv_pool);

	return 0;
}
