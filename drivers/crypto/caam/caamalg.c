/*
 * caam - Freescale FSL CAAM support for crypto API
 *
 * Copyright 2008-2011 Freescale Semiconductor, Inc.
 * Copyright 2016 NXP
 *
 * Based on talitos crypto API driver.
 *
 * relationship of job descriptors to shared descriptors (SteveC Dec 10 2008):
 *
 * ---------------                     ---------------
 * | JobDesc #1  |-------------------->|  ShareDesc  |
 * | *(packet 1) |                     |   (PDB)     |
 * ---------------      |------------->|  (hashKey)  |
 *       .              |              | (cipherKey) |
 *       .              |    |-------->| (operation) |
 * ---------------      |    |         ---------------
 * | JobDesc #2  |------|    |
 * | *(packet 2) |           |
 * ---------------           |
 *       .                   |
 *       .                   |
 * ---------------           |
 * | JobDesc #3  |------------
 * | *(packet 3) |
 * ---------------
 *
 * The SharedDesc never changes for a connection unless rekeyed, but
 * each packet will likely be in a different place. So all we need
 * to know to process the packet is where the input is, where the
 * output goes, and what context we want to process with. Context is
 * in the SharedDesc, packet references in the JobDesc.
 *
 * So, a job desc looks like:
 *
 * ---------------------
 * | Header            |
 * | ShareDesc Pointer |
 * | SEQ_OUT_PTR       |
 * | (output buffer)   |
 * | (output length)   |
 * | SEQ_IN_PTR        |
 * | (input buffer)    |
 * | (input length)    |
 * ---------------------
 */

#include "compat.h"

#include "regs.h"
#include "intern.h"
#include "desc_constr.h"
#include "jr.h"
#include "error.h"
#include "sg_sw_sec4.h"
#include "key_gen.h"
#include "caamalg_desc.h"

/*
 * crypto alg
 */
#define CAAM_CRA_PRIORITY		3000
/* max key is sum of AES_MAX_KEY_SIZE, max split key size */
#define CAAM_MAX_KEY_SIZE		(AES_MAX_KEY_SIZE + \
					 CTR_RFC3686_NONCE_SIZE + \
					 SHA512_DIGEST_SIZE * 2)

#define AEAD_DESC_JOB_IO_LEN		(DESC_JOB_IO_LEN + CAAM_CMD_SZ * 2)
#define GCM_DESC_JOB_IO_LEN		(AEAD_DESC_JOB_IO_LEN + \
					 CAAM_CMD_SZ * 4)
#define AUTHENC_DESC_JOB_IO_LEN		(AEAD_DESC_JOB_IO_LEN + \
					 CAAM_CMD_SZ * 5)

#define DESC_MAX_USED_BYTES		(CAAM_DESC_BYTES_MAX - DESC_JOB_IO_LEN)
#define DESC_MAX_USED_LEN		(DESC_MAX_USED_BYTES / CAAM_CMD_SZ)

#ifdef DEBUG
/* for print_hex_dumps with line references */
#define debug(format, arg...) printk(format, arg)
#else
#define debug(format, arg...)
#endif

#ifdef DEBUG
#include <linux/highmem.h>

static void dbg_dump_sg(const char *level, const char *prefix_str,
			int prefix_type, int rowsize, int groupsize,
			struct scatterlist *sg, size_t tlen, bool ascii)
{
	struct scatterlist *it;
	void *it_page;
	size_t len;
	void *buf;

	for (it = sg; it != NULL && tlen > 0 ; it = sg_next(sg)) {
		/*
		 * make sure the scatterlist's page
		 * has a valid virtual memory mapping
		 */
		it_page = kmap_atomic(sg_page(it));
		if (unlikely(!it_page)) {
			printk(KERN_ERR "dbg_dump_sg: kmap failed\n");
			return;
		}

		buf = it_page + it->offset;
		len = min_t(size_t, tlen, it->length);
		print_hex_dump(level, prefix_str, prefix_type, rowsize,
			       groupsize, buf, len, ascii);
		tlen -= len;

		kunmap_atomic(it_page);
	}
}
#endif

static struct list_head alg_list;

struct caam_alg_entry {
	int class1_alg_type;
	int class2_alg_type;
	bool rfc3686;
	bool geniv;
};

struct caam_aead_alg {
	struct aead_alg aead;
	struct caam_alg_entry caam;
	bool registered;
};

/*
 * per-session context
 */
struct caam_ctx {
	struct device *jrdev;
	u32 sh_desc_enc[DESC_MAX_USED_LEN];
	u32 sh_desc_dec[DESC_MAX_USED_LEN];
	u32 sh_desc_givenc[DESC_MAX_USED_LEN];
	dma_addr_t sh_desc_enc_dma;
	dma_addr_t sh_desc_dec_dma;
	dma_addr_t sh_desc_givenc_dma;
	u8 key[CAAM_MAX_KEY_SIZE];
	dma_addr_t key_dma;
	struct alginfo adata;
	struct alginfo cdata;
	unsigned int authsize;
};

static int aead_null_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	u32 *desc;
	int rem_bytes = CAAM_DESC_BYTES_MAX - AEAD_DESC_JOB_IO_LEN -
			ctx->adata.keylen_pad;

	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_AEAD_NULL_ENC_LEN) {
		ctx->adata.key_inline = true;
		ctx->adata.key_virt = ctx->key;
	} else {
		ctx->adata.key_inline = false;
		ctx->adata.key_dma = ctx->key_dma;
	}

	/* aead_encrypt shared descriptor */
	desc = ctx->sh_desc_enc;
	cnstr_shdsc_aead_null_encap(desc, &ctx->adata, ctx->authsize);
	ctx->sh_desc_enc_dma = dma_map_single(jrdev, desc,
					      desc_bytes(desc),
					      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_enc_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}

	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_AEAD_NULL_DEC_LEN) {
		ctx->adata.key_inline = true;
		ctx->adata.key_virt = ctx->key;
	} else {
		ctx->adata.key_inline = false;
		ctx->adata.key_dma = ctx->key_dma;
	}

	/* aead_decrypt shared descriptor */
	desc = ctx->sh_desc_dec;
	cnstr_shdsc_aead_null_decap(desc, &ctx->adata, ctx->authsize);
	ctx->sh_desc_dec_dma = dma_map_single(jrdev, desc,
					      desc_bytes(desc),
					      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_dec_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}

	return 0;
}

static int aead_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_aead_alg *alg = container_of(crypto_aead_alg(aead),
						 struct caam_aead_alg, aead);
	unsigned int ivsize = crypto_aead_ivsize(aead);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	u32 ctx1_iv_off = 0;
	u32 *desc, *nonce = NULL;
	u32 inl_mask;
	unsigned int data_len[2];
	const bool ctr_mode = ((ctx->cdata.algtype & OP_ALG_AAI_MASK) ==
			       OP_ALG_AAI_CTR_MOD128);
	const bool is_rfc3686 = alg->caam.rfc3686;

	if (!ctx->authsize)
		return 0;

	/* NULL encryption / decryption */
	if (!ctx->cdata.keylen)
		return aead_null_set_sh_desc(aead);

	/*
	 * AES-CTR needs to load IV in CONTEXT1 reg
	 * at an offset of 128bits (16bytes)
	 * CONTEXT1[255:128] = IV
	 */
	if (ctr_mode)
		ctx1_iv_off = 16;

	/*
	 * RFC3686 specific:
	 *	CONTEXT1[255:128] = {NONCE, IV, COUNTER}
	 */
	if (is_rfc3686) {
		ctx1_iv_off = 16 + CTR_RFC3686_NONCE_SIZE;
		nonce = (u32 *)((void *)ctx->key + ctx->adata.keylen_pad +
				ctx->cdata.keylen - CTR_RFC3686_NONCE_SIZE);
	}

	data_len[0] = ctx->adata.keylen_pad;
	data_len[1] = ctx->cdata.keylen;

	if (alg->caam.geniv)
		goto skip_enc;

	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (desc_inline_query(DESC_AEAD_ENC_LEN +
			      (is_rfc3686 ? DESC_AEAD_CTR_RFC3686_LEN : 0),
			      AUTHENC_DESC_JOB_IO_LEN, data_len, &inl_mask,
			      ARRAY_SIZE(data_len)) < 0)
		return -EINVAL;

	if (inl_mask & 1)
		ctx->adata.key_virt = ctx->key;
	else
		ctx->adata.key_dma = ctx->key_dma;

	if (inl_mask & 2)
		ctx->cdata.key_virt = ctx->key + ctx->adata.keylen_pad;
	else
		ctx->cdata.key_dma = ctx->key_dma + ctx->adata.keylen_pad;

	ctx->adata.key_inline = !!(inl_mask & 1);
	ctx->cdata.key_inline = !!(inl_mask & 2);

	/* aead_encrypt shared descriptor */
	desc = ctx->sh_desc_enc;
	cnstr_shdsc_aead_encap(desc, &ctx->cdata, &ctx->adata, ctx->authsize,
			       is_rfc3686, nonce, ctx1_iv_off);
	ctx->sh_desc_enc_dma = dma_map_single(jrdev, desc,
					      desc_bytes(desc),
					      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_enc_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}

skip_enc:
	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (desc_inline_query(DESC_AEAD_DEC_LEN +
			      (is_rfc3686 ? DESC_AEAD_CTR_RFC3686_LEN : 0),
			      AUTHENC_DESC_JOB_IO_LEN, data_len, &inl_mask,
			      ARRAY_SIZE(data_len)) < 0)
		return -EINVAL;

	if (inl_mask & 1)
		ctx->adata.key_virt = ctx->key;
	else
		ctx->adata.key_dma = ctx->key_dma;

	if (inl_mask & 2)
		ctx->cdata.key_virt = ctx->key + ctx->adata.keylen_pad;
	else
		ctx->cdata.key_dma = ctx->key_dma + ctx->adata.keylen_pad;

	ctx->adata.key_inline = !!(inl_mask & 1);
	ctx->cdata.key_inline = !!(inl_mask & 2);

	/* aead_decrypt shared descriptor */
	desc = ctx->sh_desc_dec;
	cnstr_shdsc_aead_decap(desc, &ctx->cdata, &ctx->adata, ivsize,
			       ctx->authsize, alg->caam.geniv, is_rfc3686,
			       nonce, ctx1_iv_off);
	ctx->sh_desc_dec_dma = dma_map_single(jrdev, desc,
					      desc_bytes(desc),
					      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_dec_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}

	if (!alg->caam.geniv)
		goto skip_givenc;

	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (desc_inline_query(DESC_AEAD_GIVENC_LEN +
			      (is_rfc3686 ? DESC_AEAD_CTR_RFC3686_LEN : 0),
			      AUTHENC_DESC_JOB_IO_LEN, data_len, &inl_mask,
			      ARRAY_SIZE(data_len)) < 0)
		return -EINVAL;

	if (inl_mask & 1)
		ctx->adata.key_virt = ctx->key;
	else
		ctx->adata.key_dma = ctx->key_dma;

	if (inl_mask & 2)
		ctx->cdata.key_virt = ctx->key + ctx->adata.keylen_pad;
	else
		ctx->cdata.key_dma = ctx->key_dma + ctx->adata.keylen_pad;

	ctx->adata.key_inline = !!(inl_mask & 1);
	ctx->cdata.key_inline = !!(inl_mask & 2);

	/* aead_givencrypt shared descriptor */
	desc = ctx->sh_desc_enc;
	cnstr_shdsc_aead_givencap(desc, &ctx->cdata, &ctx->adata, ivsize,
				  ctx->authsize, is_rfc3686, nonce,
				  ctx1_iv_off);
	ctx->sh_desc_enc_dma = dma_map_single(jrdev, desc,
					      desc_bytes(desc),
					      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_enc_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}

skip_givenc:
	return 0;
}

static int aead_setauthsize(struct crypto_aead *authenc,
				    unsigned int authsize)
{
	struct caam_ctx *ctx = crypto_aead_ctx(authenc);

	ctx->authsize = authsize;
	aead_set_sh_desc(authenc);

	return 0;
}

static int gcm_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	u32 *desc;
	int rem_bytes = CAAM_DESC_BYTES_MAX - GCM_DESC_JOB_IO_LEN -
			ctx->cdata.keylen;

	if (!ctx->cdata.keylen || !ctx->authsize)
		return 0;

	/*
	 * AES GCM encrypt shared descriptor
	 * Job Descriptor and Shared Descriptor
	 * must fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_GCM_ENC_LEN) {
		ctx->cdata.key_inline = true;
		ctx->cdata.key_virt = ctx->key;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	desc = ctx->sh_desc_enc;
	cnstr_shdsc_gcm_encap(desc, &ctx->cdata, ctx->authsize);
	ctx->sh_desc_enc_dma = dma_map_single(jrdev, desc,
					      desc_bytes(desc),
					      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_enc_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}

	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_GCM_DEC_LEN) {
		ctx->cdata.key_inline = true;
		ctx->cdata.key_virt = ctx->key;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	desc = ctx->sh_desc_dec;
	cnstr_shdsc_gcm_decap(desc, &ctx->cdata, ctx->authsize);
	ctx->sh_desc_dec_dma = dma_map_single(jrdev, desc,
					      desc_bytes(desc),
					      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_dec_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}

	return 0;
}

static int gcm_setauthsize(struct crypto_aead *authenc, unsigned int authsize)
{
	struct caam_ctx *ctx = crypto_aead_ctx(authenc);

	ctx->authsize = authsize;
	gcm_set_sh_desc(authenc);

	return 0;
}

static int rfc4106_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	u32 *desc;
	int rem_bytes = CAAM_DESC_BYTES_MAX - GCM_DESC_JOB_IO_LEN -
			ctx->cdata.keylen;

	if (!ctx->cdata.keylen || !ctx->authsize)
		return 0;

	/*
	 * RFC4106 encrypt shared descriptor
	 * Job Descriptor and Shared Descriptor
	 * must fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_RFC4106_ENC_LEN) {
		ctx->cdata.key_inline = true;
		ctx->cdata.key_virt = ctx->key;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	desc = ctx->sh_desc_enc;
	cnstr_shdsc_rfc4106_encap(desc, &ctx->cdata, ctx->authsize);
	ctx->sh_desc_enc_dma = dma_map_single(jrdev, desc,
					      desc_bytes(desc),
					      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_enc_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}

	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_RFC4106_DEC_LEN) {
		ctx->cdata.key_inline = true;
		ctx->cdata.key_virt = ctx->key;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	desc = ctx->sh_desc_dec;
	cnstr_shdsc_rfc4106_decap(desc, &ctx->cdata, ctx->authsize);
	ctx->sh_desc_dec_dma = dma_map_single(jrdev, desc,
					      desc_bytes(desc),
					      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_dec_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}

	return 0;
}

static int rfc4106_setauthsize(struct crypto_aead *authenc,
			       unsigned int authsize)
{
	struct caam_ctx *ctx = crypto_aead_ctx(authenc);

	ctx->authsize = authsize;
	rfc4106_set_sh_desc(authenc);

	return 0;
}

static int rfc4543_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	u32 *desc;
	int rem_bytes = CAAM_DESC_BYTES_MAX - GCM_DESC_JOB_IO_LEN -
			ctx->cdata.keylen;

	if (!ctx->cdata.keylen || !ctx->authsize)
		return 0;

	/*
	 * RFC4543 encrypt shared descriptor
	 * Job Descriptor and Shared Descriptor
	 * must fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_RFC4543_ENC_LEN) {
		ctx->cdata.key_inline = true;
		ctx->cdata.key_virt = ctx->key;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	desc = ctx->sh_desc_enc;
	cnstr_shdsc_rfc4543_encap(desc, &ctx->cdata, ctx->authsize);
	ctx->sh_desc_enc_dma = dma_map_single(jrdev, desc,
					      desc_bytes(desc),
					      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_enc_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}

	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_RFC4543_DEC_LEN) {
		ctx->cdata.key_inline = true;
		ctx->cdata.key_virt = ctx->key;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	desc = ctx->sh_desc_dec;
	cnstr_shdsc_rfc4543_decap(desc, &ctx->cdata, ctx->authsize);
	ctx->sh_desc_dec_dma = dma_map_single(jrdev, desc,
					      desc_bytes(desc),
					      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_dec_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}

	return 0;
}

static int rfc4543_setauthsize(struct crypto_aead *authenc,
			       unsigned int authsize)
{
	struct caam_ctx *ctx = crypto_aead_ctx(authenc);

	ctx->authsize = authsize;
	rfc4543_set_sh_desc(authenc);

	return 0;
}

static int aead_setkey(struct crypto_aead *aead,
			       const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	struct crypto_authenc_keys keys;
	int ret = 0;

	if (crypto_authenc_extractkeys(&keys, key, keylen) != 0)
		goto badkey;

#ifdef DEBUG
	printk(KERN_ERR "keylen %d enckeylen %d authkeylen %d\n",
	       keys.authkeylen + keys.enckeylen, keys.enckeylen,
	       keys.authkeylen);
	print_hex_dump(KERN_ERR, "key in @"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);
#endif

	ret = gen_split_key(ctx->jrdev, ctx->key, &ctx->adata, keys.authkey,
			    keys.authkeylen, CAAM_MAX_KEY_SIZE -
			    keys.enckeylen);
	if (ret) {
		goto badkey;
	}

	/* postpend encryption key to auth split key */
	memcpy(ctx->key + ctx->adata.keylen_pad, keys.enckey, keys.enckeylen);

	ctx->key_dma = dma_map_single(jrdev, ctx->key, ctx->adata.keylen_pad +
				      keys.enckeylen, DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->key_dma)) {
		dev_err(jrdev, "unable to map key i/o memory\n");
		return -ENOMEM;
	}
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "ctx.key@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, ctx->key,
		       ctx->adata.keylen_pad + keys.enckeylen, 1);
#endif

	ctx->cdata.keylen = keys.enckeylen;

	ret = aead_set_sh_desc(aead);
	if (ret) {
		dma_unmap_single(jrdev, ctx->key_dma, ctx->adata.keylen_pad +
				 keys.enckeylen, DMA_TO_DEVICE);
	}

	return ret;
badkey:
	crypto_aead_set_flags(aead, CRYPTO_TFM_RES_BAD_KEY_LEN);
	return -EINVAL;
}

static int gcm_setkey(struct crypto_aead *aead,
		      const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	int ret = 0;

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "key in @"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);
#endif

	memcpy(ctx->key, key, keylen);
	ctx->key_dma = dma_map_single(jrdev, ctx->key, keylen,
				      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->key_dma)) {
		dev_err(jrdev, "unable to map key i/o memory\n");
		return -ENOMEM;
	}
	ctx->cdata.keylen = keylen;

	ret = gcm_set_sh_desc(aead);
	if (ret) {
		dma_unmap_single(jrdev, ctx->key_dma, ctx->cdata.keylen,
				 DMA_TO_DEVICE);
	}

	return ret;
}

static int rfc4106_setkey(struct crypto_aead *aead,
			  const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	int ret = 0;

	if (keylen < 4)
		return -EINVAL;

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "key in @"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);
#endif

	memcpy(ctx->key, key, keylen);

	/*
	 * The last four bytes of the key material are used as the salt value
	 * in the nonce. Update the AES key length.
	 */
	ctx->cdata.keylen = keylen - 4;

	ctx->key_dma = dma_map_single(jrdev, ctx->key, ctx->cdata.keylen,
				      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->key_dma)) {
		dev_err(jrdev, "unable to map key i/o memory\n");
		return -ENOMEM;
	}

	ret = rfc4106_set_sh_desc(aead);
	if (ret) {
		dma_unmap_single(jrdev, ctx->key_dma, ctx->cdata.keylen,
				 DMA_TO_DEVICE);
	}

	return ret;
}

static int rfc4543_setkey(struct crypto_aead *aead,
			  const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	int ret = 0;

	if (keylen < 4)
		return -EINVAL;

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "key in @"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);
#endif

	memcpy(ctx->key, key, keylen);

	/*
	 * The last four bytes of the key material are used as the salt value
	 * in the nonce. Update the AES key length.
	 */
	ctx->cdata.keylen = keylen - 4;

	ctx->key_dma = dma_map_single(jrdev, ctx->key, ctx->cdata.keylen,
				      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->key_dma)) {
		dev_err(jrdev, "unable to map key i/o memory\n");
		return -ENOMEM;
	}

	ret = rfc4543_set_sh_desc(aead);
	if (ret) {
		dma_unmap_single(jrdev, ctx->key_dma, ctx->cdata.keylen,
				 DMA_TO_DEVICE);
	}

	return ret;
}

static int ablkcipher_setkey(struct crypto_ablkcipher *ablkcipher,
			     const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_ablkcipher_ctx(ablkcipher);
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(ablkcipher);
	const char *alg_name = crypto_tfm_alg_name(tfm);
	struct device *jrdev = ctx->jrdev;
	unsigned int ivsize = crypto_ablkcipher_ivsize(ablkcipher);
	u32 *desc;
	u32 ctx1_iv_off = 0;
	const bool ctr_mode = ((ctx->cdata.algtype & OP_ALG_AAI_MASK) ==
			       OP_ALG_AAI_CTR_MOD128);
	const bool is_rfc3686 = (ctr_mode &&
				 (strstr(alg_name, "rfc3686") != NULL));

	memcpy(ctx->key, key, keylen);
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "key in @"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);
#endif
	/*
	 * AES-CTR needs to load IV in CONTEXT1 reg
	 * at an offset of 128bits (16bytes)
	 * CONTEXT1[255:128] = IV
	 */
	if (ctr_mode)
		ctx1_iv_off = 16;

	/*
	 * RFC3686 specific:
	 *	| CONTEXT1[255:128] = {NONCE, IV, COUNTER}
	 *	| *key = {KEY, NONCE}
	 */
	if (is_rfc3686) {
		ctx1_iv_off = 16 + CTR_RFC3686_NONCE_SIZE;
		keylen -= CTR_RFC3686_NONCE_SIZE;
	}

	ctx->key_dma = dma_map_single(jrdev, ctx->key, keylen,
				      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->key_dma)) {
		dev_err(jrdev, "unable to map key i/o memory\n");
		return -ENOMEM;
	}
	ctx->cdata.keylen = keylen;
	ctx->cdata.key_virt = ctx->key;
	ctx->cdata.key_inline = true;

	/* ablkcipher_encrypt shared descriptor */
	desc = ctx->sh_desc_enc;
	cnstr_shdsc_ablkcipher_encap(desc, &ctx->cdata, ivsize, is_rfc3686,
				     ctx1_iv_off);
	ctx->sh_desc_enc_dma = dma_map_single(jrdev, desc,
					      desc_bytes(desc),
					      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_enc_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}

	/* ablkcipher_decrypt shared descriptor */
	desc = ctx->sh_desc_dec;
	cnstr_shdsc_ablkcipher_decap(desc, &ctx->cdata, ivsize, is_rfc3686,
				     ctx1_iv_off);
	ctx->sh_desc_dec_dma = dma_map_single(jrdev, desc,
					      desc_bytes(desc),
					      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_dec_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}

	/* ablkcipher_givencrypt shared descriptor */
	desc = ctx->sh_desc_givenc;
	cnstr_shdsc_ablkcipher_givencap(desc, &ctx->cdata, ivsize, is_rfc3686,
					ctx1_iv_off);
	ctx->sh_desc_givenc_dma = dma_map_single(jrdev, desc,
						 desc_bytes(desc),
						 DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_givenc_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}

	return 0;
}

static int xts_ablkcipher_setkey(struct crypto_ablkcipher *ablkcipher,
				 const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_ablkcipher_ctx(ablkcipher);
	struct device *jrdev = ctx->jrdev;
	u32 *desc;

	if (keylen != 2 * AES_MIN_KEY_SIZE  && keylen != 2 * AES_MAX_KEY_SIZE) {
		crypto_ablkcipher_set_flags(ablkcipher,
					    CRYPTO_TFM_RES_BAD_KEY_LEN);
		dev_err(jrdev, "key size mismatch\n");
		return -EINVAL;
	}

	memcpy(ctx->key, key, keylen);
	ctx->key_dma = dma_map_single(jrdev, ctx->key, keylen, DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->key_dma)) {
		dev_err(jrdev, "unable to map key i/o memory\n");
		return -ENOMEM;
	}
	ctx->cdata.keylen = keylen;
	ctx->cdata.key_virt = ctx->key;
	ctx->cdata.key_inline = true;

	/* xts_ablkcipher_encrypt shared descriptor */
	desc = ctx->sh_desc_enc;
	cnstr_shdsc_xts_ablkcipher_encap(desc, &ctx->cdata);
	ctx->sh_desc_enc_dma = dma_map_single(jrdev, desc, desc_bytes(desc),
					      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_enc_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}

	/* xts_ablkcipher_decrypt shared descriptor */
	desc = ctx->sh_desc_dec;
	cnstr_shdsc_xts_ablkcipher_decap(desc, &ctx->cdata);
	ctx->sh_desc_dec_dma = dma_map_single(jrdev, desc, desc_bytes(desc),
					      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_dec_dma)) {
		dma_unmap_single(jrdev, ctx->sh_desc_enc_dma,
				 desc_bytes(ctx->sh_desc_enc), DMA_TO_DEVICE);
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}

	return 0;
}

/*
 * aead_edesc - s/w-extended aead descriptor
 * @src_nents: number of segments in input scatterlist
 * @dst_nents: number of segments in output scatterlist
 * @sec4_sg_bytes: length of dma mapped sec4_sg space
 * @sec4_sg_dma: bus physical mapped address of h/w link table
 * @sec4_sg: pointer to h/w link table
 * @hw_desc: the h/w job descriptor followed by any referenced link tables
 */
struct aead_edesc {
	int src_nents;
	int dst_nents;
	int sec4_sg_bytes;
	dma_addr_t sec4_sg_dma;
	struct sec4_sg_entry *sec4_sg;
	u32 hw_desc[];
};

/*
 * ablkcipher_edesc - s/w-extended ablkcipher descriptor
 * @src_nents: number of segments in input scatterlist
 * @dst_nents: number of segments in output scatterlist
 * @iv_dma: dma address of iv for checking continuity and link table
 * @sec4_sg_bytes: length of dma mapped sec4_sg space
 * @sec4_sg_dma: bus physical mapped address of h/w link table
 * @sec4_sg: pointer to h/w link table
 * @hw_desc: the h/w job descriptor followed by any referenced link tables
 */
struct ablkcipher_edesc {
	int src_nents;
	int dst_nents;
	dma_addr_t iv_dma;
	int sec4_sg_bytes;
	dma_addr_t sec4_sg_dma;
	struct sec4_sg_entry *sec4_sg;
	u32 hw_desc[0];
};

static void caam_unmap(struct device *dev, struct scatterlist *src,
		       struct scatterlist *dst, int src_nents,
		       int dst_nents,
		       dma_addr_t iv_dma, int ivsize, dma_addr_t sec4_sg_dma,
		       int sec4_sg_bytes)
{
	if (dst != src) {
		dma_unmap_sg(dev, src, src_nents ? : 1, DMA_TO_DEVICE);
		dma_unmap_sg(dev, dst, dst_nents ? : 1, DMA_FROM_DEVICE);
	} else {
		dma_unmap_sg(dev, src, src_nents ? : 1, DMA_BIDIRECTIONAL);
	}

	if (iv_dma)
		dma_unmap_single(dev, iv_dma, ivsize, DMA_TO_DEVICE);
	if (sec4_sg_bytes)
		dma_unmap_single(dev, sec4_sg_dma, sec4_sg_bytes,
				 DMA_TO_DEVICE);
}

static void aead_unmap(struct device *dev,
		       struct aead_edesc *edesc,
		       struct aead_request *req)
{
	caam_unmap(dev, req->src, req->dst,
		   edesc->src_nents, edesc->dst_nents, 0, 0,
		   edesc->sec4_sg_dma, edesc->sec4_sg_bytes);
}

static void ablkcipher_unmap(struct device *dev,
			     struct ablkcipher_edesc *edesc,
			     struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	int ivsize = crypto_ablkcipher_ivsize(ablkcipher);

	caam_unmap(dev, req->src, req->dst,
		   edesc->src_nents, edesc->dst_nents,
		   edesc->iv_dma, ivsize,
		   edesc->sec4_sg_dma, edesc->sec4_sg_bytes);
}

static void aead_encrypt_done(struct device *jrdev, u32 *desc, u32 err,
				   void *context)
{
	struct aead_request *req = context;
	struct aead_edesc *edesc;

#ifdef DEBUG
	dev_err(jrdev, "%s %d: err 0x%x\n", __func__, __LINE__, err);
#endif

	edesc = container_of(desc, struct aead_edesc, hw_desc[0]);

	if (err)
		caam_jr_strstatus(jrdev, err);

	aead_unmap(jrdev, edesc, req);

	kfree(edesc);

	aead_request_complete(req, err);
}

static void aead_decrypt_done(struct device *jrdev, u32 *desc, u32 err,
				   void *context)
{
	struct aead_request *req = context;
	struct aead_edesc *edesc;

#ifdef DEBUG
	dev_err(jrdev, "%s %d: err 0x%x\n", __func__, __LINE__, err);
#endif

	edesc = container_of(desc, struct aead_edesc, hw_desc[0]);

	if (err)
		caam_jr_strstatus(jrdev, err);

	aead_unmap(jrdev, edesc, req);

	/*
	 * verify hw auth check passed else return -EBADMSG
	 */
	if ((err & JRSTA_CCBERR_ERRID_MASK) == JRSTA_CCBERR_ERRID_ICVCHK)
		err = -EBADMSG;

	kfree(edesc);

	aead_request_complete(req, err);
}

static void ablkcipher_encrypt_done(struct device *jrdev, u32 *desc, u32 err,
				   void *context)
{
	struct ablkcipher_request *req = context;
	struct ablkcipher_edesc *edesc;
#ifdef DEBUG
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	int ivsize = crypto_ablkcipher_ivsize(ablkcipher);

	dev_err(jrdev, "%s %d: err 0x%x\n", __func__, __LINE__, err);
#endif

	edesc = container_of(desc, struct ablkcipher_edesc, hw_desc[0]);

	if (err)
		caam_jr_strstatus(jrdev, err);

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "dstiv  @"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, req->info,
		       edesc->src_nents > 1 ? 100 : ivsize, 1);
	dbg_dump_sg(KERN_ERR, "dst    @"__stringify(__LINE__)": ",
		    DUMP_PREFIX_ADDRESS, 16, 4, req->dst,
		    edesc->dst_nents > 1 ? 100 : req->nbytes, 1);
#endif

	ablkcipher_unmap(jrdev, edesc, req);
	kfree(edesc);

	ablkcipher_request_complete(req, err);
}

static void ablkcipher_decrypt_done(struct device *jrdev, u32 *desc, u32 err,
				    void *context)
{
	struct ablkcipher_request *req = context;
	struct ablkcipher_edesc *edesc;
#ifdef DEBUG
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	int ivsize = crypto_ablkcipher_ivsize(ablkcipher);

	dev_err(jrdev, "%s %d: err 0x%x\n", __func__, __LINE__, err);
#endif

	edesc = container_of(desc, struct ablkcipher_edesc, hw_desc[0]);
	if (err)
		caam_jr_strstatus(jrdev, err);

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "dstiv  @"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, req->info,
		       ivsize, 1);
	dbg_dump_sg(KERN_ERR, "dst    @"__stringify(__LINE__)": ",
		    DUMP_PREFIX_ADDRESS, 16, 4, req->dst,
		    edesc->dst_nents > 1 ? 100 : req->nbytes, 1);
#endif

	ablkcipher_unmap(jrdev, edesc, req);
	kfree(edesc);

	ablkcipher_request_complete(req, err);
}

/*
 * Fill in aead job descriptor
 */
static void init_aead_job(struct aead_request *req,
			  struct aead_edesc *edesc,
			  bool all_contig, bool encrypt)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	int authsize = ctx->authsize;
	u32 *desc = edesc->hw_desc;
	u32 out_options, in_options;
	dma_addr_t dst_dma, src_dma;
	int len, sec4_sg_index = 0;
	dma_addr_t ptr;
	u32 *sh_desc;

	sh_desc = encrypt ? ctx->sh_desc_enc : ctx->sh_desc_dec;
	ptr = encrypt ? ctx->sh_desc_enc_dma : ctx->sh_desc_dec_dma;

	len = desc_len(sh_desc);
	init_job_desc_shared(desc, ptr, len, HDR_SHARE_DEFER | HDR_REVERSE);

	if (all_contig) {
		src_dma = sg_dma_address(req->src);
		in_options = 0;
	} else {
		src_dma = edesc->sec4_sg_dma;
		sec4_sg_index += edesc->src_nents;
		in_options = LDST_SGF;
	}

	append_seq_in_ptr(desc, src_dma, req->assoclen + req->cryptlen,
			  in_options);

	dst_dma = src_dma;
	out_options = in_options;

	if (unlikely(req->src != req->dst)) {
		if (!edesc->dst_nents) {
			dst_dma = sg_dma_address(req->dst);
		} else {
			dst_dma = edesc->sec4_sg_dma +
				  sec4_sg_index *
				  sizeof(struct sec4_sg_entry);
			out_options = LDST_SGF;
		}
	}

	if (encrypt)
		append_seq_out_ptr(desc, dst_dma,
				   req->assoclen + req->cryptlen + authsize,
				   out_options);
	else
		append_seq_out_ptr(desc, dst_dma,
				   req->assoclen + req->cryptlen - authsize,
				   out_options);

	/* REG3 = assoclen */
	append_math_add_imm_u32(desc, REG3, ZERO, IMM, req->assoclen);
}

static void init_gcm_job(struct aead_request *req,
			 struct aead_edesc *edesc,
			 bool all_contig, bool encrypt)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	unsigned int ivsize = crypto_aead_ivsize(aead);
	u32 *desc = edesc->hw_desc;
	bool generic_gcm = (ivsize == 12);
	unsigned int last;

	init_aead_job(req, edesc, all_contig, encrypt);

	/* BUG This should not be specific to generic GCM. */
	last = 0;
	if (encrypt && generic_gcm && !(req->assoclen + req->cryptlen))
		last = FIFOLD_TYPE_LAST1;

	/* Read GCM IV */
	append_cmd(desc, CMD_FIFO_LOAD | FIFOLD_CLASS_CLASS1 | IMMEDIATE |
			 FIFOLD_TYPE_IV | FIFOLD_TYPE_FLUSH1 | 12 | last);
	/* Append Salt */
	if (!generic_gcm)
		append_data(desc, ctx->key + ctx->cdata.keylen, 4);
	/* Append IV */
	append_data(desc, req->iv, ivsize);
	/* End of blank commands */
}

static void init_authenc_job(struct aead_request *req,
			     struct aead_edesc *edesc,
			     bool all_contig, bool encrypt)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_aead_alg *alg = container_of(crypto_aead_alg(aead),
						 struct caam_aead_alg, aead);
	unsigned int ivsize = crypto_aead_ivsize(aead);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	const bool ctr_mode = ((ctx->cdata.algtype & OP_ALG_AAI_MASK) ==
			       OP_ALG_AAI_CTR_MOD128);
	const bool is_rfc3686 = alg->caam.rfc3686;
	u32 *desc = edesc->hw_desc;
	u32 ivoffset = 0;

	/*
	 * AES-CTR needs to load IV in CONTEXT1 reg
	 * at an offset of 128bits (16bytes)
	 * CONTEXT1[255:128] = IV
	 */
	if (ctr_mode)
		ivoffset = 16;

	/*
	 * RFC3686 specific:
	 *	CONTEXT1[255:128] = {NONCE, IV, COUNTER}
	 */
	if (is_rfc3686)
		ivoffset = 16 + CTR_RFC3686_NONCE_SIZE;

	init_aead_job(req, edesc, all_contig, encrypt);

	if (ivsize && ((is_rfc3686 && encrypt) || !alg->caam.geniv))
		append_load_as_imm(desc, req->iv, ivsize,
				   LDST_CLASS_1_CCB |
				   LDST_SRCDST_BYTE_CONTEXT |
				   (ivoffset << LDST_OFFSET_SHIFT));
}

/*
 * Fill in ablkcipher job descriptor
 */
static void init_ablkcipher_job(u32 *sh_desc, dma_addr_t ptr,
				struct ablkcipher_edesc *edesc,
				struct ablkcipher_request *req,
				bool iv_contig)
{
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	int ivsize = crypto_ablkcipher_ivsize(ablkcipher);
	u32 *desc = edesc->hw_desc;
	u32 out_options = 0, in_options;
	dma_addr_t dst_dma, src_dma;
	int len, sec4_sg_index = 0;

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "presciv@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, req->info,
		       ivsize, 1);
	printk(KERN_ERR "asked=%d, nbytes%d\n", (int)edesc->src_nents ? 100 : req->nbytes, req->nbytes);
	dbg_dump_sg(KERN_ERR, "src    @"__stringify(__LINE__)": ",
		    DUMP_PREFIX_ADDRESS, 16, 4, req->src,
		    edesc->src_nents ? 100 : req->nbytes, 1);
#endif

	len = desc_len(sh_desc);
	init_job_desc_shared(desc, ptr, len, HDR_SHARE_DEFER | HDR_REVERSE);

	if (iv_contig) {
		src_dma = edesc->iv_dma;
		in_options = 0;
	} else {
		src_dma = edesc->sec4_sg_dma;
		sec4_sg_index += edesc->src_nents + 1;
		in_options = LDST_SGF;
	}
	append_seq_in_ptr(desc, src_dma, req->nbytes + ivsize, in_options);

	if (likely(req->src == req->dst)) {
		if (!edesc->src_nents && iv_contig) {
			dst_dma = sg_dma_address(req->src);
		} else {
			dst_dma = edesc->sec4_sg_dma +
				sizeof(struct sec4_sg_entry);
			out_options = LDST_SGF;
		}
	} else {
		if (!edesc->dst_nents) {
			dst_dma = sg_dma_address(req->dst);
		} else {
			dst_dma = edesc->sec4_sg_dma +
				sec4_sg_index * sizeof(struct sec4_sg_entry);
			out_options = LDST_SGF;
		}
	}
	append_seq_out_ptr(desc, dst_dma, req->nbytes, out_options);
}

/*
 * Fill in ablkcipher givencrypt job descriptor
 */
static void init_ablkcipher_giv_job(u32 *sh_desc, dma_addr_t ptr,
				    struct ablkcipher_edesc *edesc,
				    struct ablkcipher_request *req,
				    bool iv_contig)
{
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	int ivsize = crypto_ablkcipher_ivsize(ablkcipher);
	u32 *desc = edesc->hw_desc;
	u32 out_options, in_options;
	dma_addr_t dst_dma, src_dma;
	int len, sec4_sg_index = 0;

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "presciv@" __stringify(__LINE__) ": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, req->info,
		       ivsize, 1);
	dbg_dump_sg(KERN_ERR, "src    @" __stringify(__LINE__) ": ",
		    DUMP_PREFIX_ADDRESS, 16, 4, req->src,
		    edesc->src_nents ? 100 : req->nbytes, 1);
#endif

	len = desc_len(sh_desc);
	init_job_desc_shared(desc, ptr, len, HDR_SHARE_DEFER | HDR_REVERSE);

	if (!edesc->src_nents) {
		src_dma = sg_dma_address(req->src);
		in_options = 0;
	} else {
		src_dma = edesc->sec4_sg_dma;
		sec4_sg_index += edesc->src_nents;
		in_options = LDST_SGF;
	}
	append_seq_in_ptr(desc, src_dma, req->nbytes, in_options);

	if (iv_contig) {
		dst_dma = edesc->iv_dma;
		out_options = 0;
	} else {
		dst_dma = edesc->sec4_sg_dma +
			  sec4_sg_index * sizeof(struct sec4_sg_entry);
		out_options = LDST_SGF;
	}
	append_seq_out_ptr(desc, dst_dma, req->nbytes + ivsize, out_options);
}

/*
 * allocate and map the aead extended descriptor
 */
static struct aead_edesc *aead_edesc_alloc(struct aead_request *req,
					   int desc_bytes, bool *all_contig_ptr,
					   bool encrypt)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	gfp_t flags = (req->base.flags & (CRYPTO_TFM_REQ_MAY_BACKLOG |
		       CRYPTO_TFM_REQ_MAY_SLEEP)) ? GFP_KERNEL : GFP_ATOMIC;
	int src_nents, dst_nents = 0;
	struct aead_edesc *edesc;
	int sgc;
	bool all_contig = true;
	int sec4_sg_index, sec4_sg_len = 0, sec4_sg_bytes;
	unsigned int authsize = ctx->authsize;

	if (unlikely(req->dst != req->src)) {
		src_nents = sg_count(req->src, req->assoclen + req->cryptlen);
		dst_nents = sg_count(req->dst,
				     req->assoclen + req->cryptlen +
					(encrypt ? authsize : (-authsize)));
	} else {
		src_nents = sg_count(req->src,
				     req->assoclen + req->cryptlen +
					(encrypt ? authsize : 0));
	}

	/* Check if data are contiguous. */
	all_contig = !src_nents;
	if (!all_contig)
		sec4_sg_len = src_nents;

	sec4_sg_len += dst_nents;

	sec4_sg_bytes = sec4_sg_len * sizeof(struct sec4_sg_entry);

	/* allocate space for base edesc and hw desc commands, link tables */
	edesc = kzalloc(sizeof(*edesc) + desc_bytes + sec4_sg_bytes,
			GFP_DMA | flags);
	if (!edesc) {
		dev_err(jrdev, "could not allocate extended descriptor\n");
		return ERR_PTR(-ENOMEM);
	}

	if (likely(req->src == req->dst)) {
		sgc = dma_map_sg(jrdev, req->src, src_nents ? : 1,
				 DMA_BIDIRECTIONAL);
		if (unlikely(!sgc)) {
			dev_err(jrdev, "unable to map source\n");
			kfree(edesc);
			return ERR_PTR(-ENOMEM);
		}
	} else {
		sgc = dma_map_sg(jrdev, req->src, src_nents ? : 1,
				 DMA_TO_DEVICE);
		if (unlikely(!sgc)) {
			dev_err(jrdev, "unable to map source\n");
			kfree(edesc);
			return ERR_PTR(-ENOMEM);
		}

		sgc = dma_map_sg(jrdev, req->dst, dst_nents ? : 1,
				 DMA_FROM_DEVICE);
		if (unlikely(!sgc)) {
			dev_err(jrdev, "unable to map destination\n");
			dma_unmap_sg(jrdev, req->src, src_nents ? : 1,
				     DMA_TO_DEVICE);
			kfree(edesc);
			return ERR_PTR(-ENOMEM);
		}
	}

	edesc->src_nents = src_nents;
	edesc->dst_nents = dst_nents;
	edesc->sec4_sg = (void *)edesc + sizeof(struct aead_edesc) +
			 desc_bytes;
	*all_contig_ptr = all_contig;

	sec4_sg_index = 0;
	if (!all_contig) {
		sg_to_sec4_sg_last(req->src, src_nents,
			      edesc->sec4_sg + sec4_sg_index, 0);
		sec4_sg_index += src_nents;
	}
	if (dst_nents) {
		sg_to_sec4_sg_last(req->dst, dst_nents,
				   edesc->sec4_sg + sec4_sg_index, 0);
	}

	if (!sec4_sg_bytes)
		return edesc;

	edesc->sec4_sg_dma = dma_map_single(jrdev, edesc->sec4_sg,
					    sec4_sg_bytes, DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, edesc->sec4_sg_dma)) {
		dev_err(jrdev, "unable to map S/G table\n");
		aead_unmap(jrdev, edesc, req);
		kfree(edesc);
		return ERR_PTR(-ENOMEM);
	}

	edesc->sec4_sg_bytes = sec4_sg_bytes;

	return edesc;
}

static int gcm_encrypt(struct aead_request *req)
{
	struct aead_edesc *edesc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	bool all_contig;
	u32 *desc;
	int ret = 0;

	/* allocate extended descriptor */
	edesc = aead_edesc_alloc(req, GCM_DESC_JOB_IO_LEN, &all_contig, true);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* Create and submit job descriptor */
	init_gcm_job(req, edesc, all_contig, true);
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "aead jobdesc@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, edesc->hw_desc,
		       desc_bytes(edesc->hw_desc), 1);
#endif

	desc = edesc->hw_desc;
	ret = caam_jr_enqueue(jrdev, desc, aead_encrypt_done, req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		aead_unmap(jrdev, edesc, req);
		kfree(edesc);
	}

	return ret;
}

static int ipsec_gcm_encrypt(struct aead_request *req)
{
	if (req->assoclen < 8)
		return -EINVAL;

	return gcm_encrypt(req);
}

static int aead_encrypt(struct aead_request *req)
{
	struct aead_edesc *edesc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	bool all_contig;
	u32 *desc;
	int ret = 0;

	/* allocate extended descriptor */
	edesc = aead_edesc_alloc(req, AUTHENC_DESC_JOB_IO_LEN,
				 &all_contig, true);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* Create and submit job descriptor */
	init_authenc_job(req, edesc, all_contig, true);
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "aead jobdesc@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, edesc->hw_desc,
		       desc_bytes(edesc->hw_desc), 1);
#endif

	desc = edesc->hw_desc;
	ret = caam_jr_enqueue(jrdev, desc, aead_encrypt_done, req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		aead_unmap(jrdev, edesc, req);
		kfree(edesc);
	}

	return ret;
}

static int gcm_decrypt(struct aead_request *req)
{
	struct aead_edesc *edesc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	bool all_contig;
	u32 *desc;
	int ret = 0;

	/* allocate extended descriptor */
	edesc = aead_edesc_alloc(req, GCM_DESC_JOB_IO_LEN, &all_contig, false);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* Create and submit job descriptor*/
	init_gcm_job(req, edesc, all_contig, false);
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "aead jobdesc@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, edesc->hw_desc,
		       desc_bytes(edesc->hw_desc), 1);
#endif

	desc = edesc->hw_desc;
	ret = caam_jr_enqueue(jrdev, desc, aead_decrypt_done, req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		aead_unmap(jrdev, edesc, req);
		kfree(edesc);
	}

	return ret;
}

static int ipsec_gcm_decrypt(struct aead_request *req)
{
	if (req->assoclen < 8)
		return -EINVAL;

	return gcm_decrypt(req);
}

static int aead_decrypt(struct aead_request *req)
{
	struct aead_edesc *edesc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	bool all_contig;
	u32 *desc;
	int ret = 0;

#ifdef DEBUG
	dbg_dump_sg(KERN_ERR, "dec src@"__stringify(__LINE__)": ",
		    DUMP_PREFIX_ADDRESS, 16, 4, req->src,
		    req->assoclen + req->cryptlen, 1);
#endif

	/* allocate extended descriptor */
	edesc = aead_edesc_alloc(req, AUTHENC_DESC_JOB_IO_LEN,
				 &all_contig, false);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* Create and submit job descriptor*/
	init_authenc_job(req, edesc, all_contig, false);
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "aead jobdesc@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, edesc->hw_desc,
		       desc_bytes(edesc->hw_desc), 1);
#endif

	desc = edesc->hw_desc;
	ret = caam_jr_enqueue(jrdev, desc, aead_decrypt_done, req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		aead_unmap(jrdev, edesc, req);
		kfree(edesc);
	}

	return ret;
}

/*
 * allocate and map the ablkcipher extended descriptor for ablkcipher
 */
static struct ablkcipher_edesc *ablkcipher_edesc_alloc(struct ablkcipher_request
						       *req, int desc_bytes,
						       bool *iv_contig_out)
{
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_ablkcipher_ctx(ablkcipher);
	struct device *jrdev = ctx->jrdev;
	gfp_t flags = (req->base.flags & (CRYPTO_TFM_REQ_MAY_BACKLOG |
					  CRYPTO_TFM_REQ_MAY_SLEEP)) ?
		       GFP_KERNEL : GFP_ATOMIC;
	int src_nents, dst_nents = 0, sec4_sg_bytes;
	struct ablkcipher_edesc *edesc;
	dma_addr_t iv_dma = 0;
	bool iv_contig = false;
	int sgc;
	int ivsize = crypto_ablkcipher_ivsize(ablkcipher);
	int sec4_sg_index;

	src_nents = sg_count(req->src, req->nbytes);

	if (req->dst != req->src)
		dst_nents = sg_count(req->dst, req->nbytes);

	if (likely(req->src == req->dst)) {
		sgc = dma_map_sg(jrdev, req->src, src_nents ? : 1,
				 DMA_BIDIRECTIONAL);
		if (unlikely(!sgc)) {
			dev_err(jrdev, "unable to map source\n");
			return ERR_PTR(-ENOMEM);
		}
	} else {
		sgc = dma_map_sg(jrdev, req->src, src_nents ? : 1,
				 DMA_TO_DEVICE);
		if (unlikely(!sgc)) {
			dev_err(jrdev, "unable to map source\n");
			return ERR_PTR(-ENOMEM);
		}

		sgc = dma_map_sg(jrdev, req->dst, dst_nents ? : 1,
				 DMA_FROM_DEVICE);
		if (unlikely(!sgc)) {
			dev_err(jrdev, "unable to map destination\n");
			dma_unmap_sg(jrdev, req->src, src_nents ? : 1,
				     DMA_TO_DEVICE);
			return ERR_PTR(-ENOMEM);
		}
	}

	iv_dma = dma_map_single(jrdev, req->info, ivsize, DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, iv_dma)) {
		dev_err(jrdev, "unable to map IV\n");
		caam_unmap(jrdev, req->src, req->dst, src_nents, dst_nents, 0,
			   0, 0, 0);
		return ERR_PTR(-ENOMEM);
	}

	/*
	 * Check if iv can be contiguous with source and destination.
	 * If so, include it. If not, create scatterlist.
	 */
	if (!src_nents && iv_dma + ivsize == sg_dma_address(req->src))
		iv_contig = true;
	else
		src_nents = src_nents ? : 1;
	sec4_sg_bytes = ((iv_contig ? 0 : 1) + src_nents + dst_nents) *
			sizeof(struct sec4_sg_entry);

	/* allocate space for base edesc and hw desc commands, link tables */
	edesc = kzalloc(sizeof(*edesc) + desc_bytes + sec4_sg_bytes,
			GFP_DMA | flags);
	if (!edesc) {
		dev_err(jrdev, "could not allocate extended descriptor\n");
		caam_unmap(jrdev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, 0, 0);
		return ERR_PTR(-ENOMEM);
	}

	edesc->src_nents = src_nents;
	edesc->dst_nents = dst_nents;
	edesc->sec4_sg_bytes = sec4_sg_bytes;
	edesc->sec4_sg = (void *)edesc + sizeof(struct ablkcipher_edesc) +
			 desc_bytes;

	sec4_sg_index = 0;
	if (!iv_contig) {
		dma_to_sec4_sg_one(edesc->sec4_sg, iv_dma, ivsize, 0);
		sg_to_sec4_sg_last(req->src, src_nents,
				   edesc->sec4_sg + 1, 0);
		sec4_sg_index += 1 + src_nents;
	}

	if (dst_nents) {
		sg_to_sec4_sg_last(req->dst, dst_nents,
			edesc->sec4_sg + sec4_sg_index, 0);
	}

	edesc->sec4_sg_dma = dma_map_single(jrdev, edesc->sec4_sg,
					    sec4_sg_bytes, DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, edesc->sec4_sg_dma)) {
		dev_err(jrdev, "unable to map S/G table\n");
		caam_unmap(jrdev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, 0, 0);
		kfree(edesc);
		return ERR_PTR(-ENOMEM);
	}

	edesc->iv_dma = iv_dma;

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "ablkcipher sec4_sg@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, edesc->sec4_sg,
		       sec4_sg_bytes, 1);
#endif

	*iv_contig_out = iv_contig;
	return edesc;
}

static int ablkcipher_encrypt(struct ablkcipher_request *req)
{
	struct ablkcipher_edesc *edesc;
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_ablkcipher_ctx(ablkcipher);
	struct device *jrdev = ctx->jrdev;
	bool iv_contig;
	u32 *desc;
	int ret = 0;

	/* allocate extended descriptor */
	edesc = ablkcipher_edesc_alloc(req, DESC_JOB_IO_LEN *
				       CAAM_CMD_SZ, &iv_contig);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* Create and submit job descriptor*/
	init_ablkcipher_job(ctx->sh_desc_enc,
		ctx->sh_desc_enc_dma, edesc, req, iv_contig);
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "ablkcipher jobdesc@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, edesc->hw_desc,
		       desc_bytes(edesc->hw_desc), 1);
#endif
	desc = edesc->hw_desc;
	ret = caam_jr_enqueue(jrdev, desc, ablkcipher_encrypt_done, req);

	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		ablkcipher_unmap(jrdev, edesc, req);
		kfree(edesc);
	}

	return ret;
}

static int ablkcipher_decrypt(struct ablkcipher_request *req)
{
	struct ablkcipher_edesc *edesc;
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_ablkcipher_ctx(ablkcipher);
	struct device *jrdev = ctx->jrdev;
	bool iv_contig;
	u32 *desc;
	int ret = 0;

	/* allocate extended descriptor */
	edesc = ablkcipher_edesc_alloc(req, DESC_JOB_IO_LEN *
				       CAAM_CMD_SZ, &iv_contig);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* Create and submit job descriptor*/
	init_ablkcipher_job(ctx->sh_desc_dec,
		ctx->sh_desc_dec_dma, edesc, req, iv_contig);
	desc = edesc->hw_desc;
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "ablkcipher jobdesc@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, edesc->hw_desc,
		       desc_bytes(edesc->hw_desc), 1);
#endif

	ret = caam_jr_enqueue(jrdev, desc, ablkcipher_decrypt_done, req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		ablkcipher_unmap(jrdev, edesc, req);
		kfree(edesc);
	}

	return ret;
}

/*
 * allocate and map the ablkcipher extended descriptor
 * for ablkcipher givencrypt
 */
static struct ablkcipher_edesc *ablkcipher_giv_edesc_alloc(
				struct skcipher_givcrypt_request *greq,
				int desc_bytes,
				bool *iv_contig_out)
{
	struct ablkcipher_request *req = &greq->creq;
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_ablkcipher_ctx(ablkcipher);
	struct device *jrdev = ctx->jrdev;
	gfp_t flags = (req->base.flags & (CRYPTO_TFM_REQ_MAY_BACKLOG |
					  CRYPTO_TFM_REQ_MAY_SLEEP)) ?
		       GFP_KERNEL : GFP_ATOMIC;
	int src_nents, dst_nents, sec4_sg_bytes;
	struct ablkcipher_edesc *edesc;
	dma_addr_t iv_dma = 0;
	bool iv_contig = false;
	int sgc;
	int ivsize = crypto_ablkcipher_ivsize(ablkcipher);
	int sec4_sg_index;

	src_nents = sg_count(req->src, req->nbytes);

	if (likely(req->src == req->dst)) {
		sgc = dma_map_sg(jrdev, req->src, src_nents ? : 1,
				 DMA_BIDIRECTIONAL);
		if (unlikely(!sgc)) {
			dev_err(jrdev, "unable to map source\n");
			return ERR_PTR(-ENOMEM);
		}

		dst_nents = src_nents;
	} else {
		sgc = dma_map_sg(jrdev, req->src, src_nents ? : 1,
				 DMA_TO_DEVICE);
		if (unlikely(!sgc)) {
			dev_err(jrdev, "unable to map source\n");
			return ERR_PTR(-ENOMEM);
		}

		dst_nents = sg_count(req->dst, req->nbytes);
		sgc = dma_map_sg(jrdev, req->dst, dst_nents ? : 1,
				 DMA_FROM_DEVICE);
		if (unlikely(!sgc)) {
			dev_err(jrdev, "unable to map destination\n");
			dma_unmap_sg(jrdev, req->src, src_nents ? : 1,
				     DMA_TO_DEVICE);
			return ERR_PTR(-ENOMEM);
		}
	}

	/*
	 * Check if iv can be contiguous with source and destination.
	 * If so, include it. If not, create scatterlist.
	 */
	iv_dma = dma_map_single(jrdev, greq->giv, ivsize, DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, iv_dma)) {
		dev_err(jrdev, "unable to map IV\n");
		caam_unmap(jrdev, req->src, req->dst, src_nents, dst_nents, 0,
			   0, 0, 0);
		return ERR_PTR(-ENOMEM);
	}

	if (!dst_nents && iv_dma + ivsize == sg_dma_address(req->dst))
		iv_contig = true;
	else
		dst_nents = dst_nents ? : 1;
	sec4_sg_bytes = ((iv_contig ? 0 : 1) + src_nents + dst_nents) *
			sizeof(struct sec4_sg_entry);

	/* allocate space for base edesc and hw desc commands, link tables */
	edesc = kzalloc(sizeof(*edesc) + desc_bytes + sec4_sg_bytes,
			GFP_DMA | flags);
	if (!edesc) {
		dev_err(jrdev, "could not allocate extended descriptor\n");
		caam_unmap(jrdev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, 0, 0);
		return ERR_PTR(-ENOMEM);
	}

	edesc->src_nents = src_nents;
	edesc->dst_nents = dst_nents;
	edesc->sec4_sg_bytes = sec4_sg_bytes;
	edesc->sec4_sg = (void *)edesc + sizeof(struct ablkcipher_edesc) +
			 desc_bytes;

	sec4_sg_index = 0;
	if (src_nents) {
		sg_to_sec4_sg_last(req->src, src_nents, edesc->sec4_sg, 0);
		sec4_sg_index += src_nents;
	}

	if (!iv_contig) {
		dma_to_sec4_sg_one(edesc->sec4_sg + sec4_sg_index,
				   iv_dma, ivsize, 0);
		sec4_sg_index += 1;
		sg_to_sec4_sg_last(req->dst, dst_nents,
				   edesc->sec4_sg + sec4_sg_index, 0);
	}

	edesc->sec4_sg_dma = dma_map_single(jrdev, edesc->sec4_sg,
					    sec4_sg_bytes, DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, edesc->sec4_sg_dma)) {
		dev_err(jrdev, "unable to map S/G table\n");
		caam_unmap(jrdev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, 0, 0);
		kfree(edesc);
		return ERR_PTR(-ENOMEM);
	}
	edesc->iv_dma = iv_dma;

#ifdef DEBUG
	print_hex_dump(KERN_ERR,
		       "ablkcipher sec4_sg@" __stringify(__LINE__) ": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, edesc->sec4_sg,
		       sec4_sg_bytes, 1);
#endif

	*iv_contig_out = iv_contig;
	return edesc;
}

static int ablkcipher_givencrypt(struct skcipher_givcrypt_request *creq)
{
	struct ablkcipher_request *req = &creq->creq;
	struct ablkcipher_edesc *edesc;
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_ablkcipher_ctx(ablkcipher);
	struct device *jrdev = ctx->jrdev;
	bool iv_contig;
	u32 *desc;
	int ret = 0;

	/* allocate extended descriptor */
	edesc = ablkcipher_giv_edesc_alloc(creq, DESC_JOB_IO_LEN *
				       CAAM_CMD_SZ, &iv_contig);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* Create and submit job descriptor*/
	init_ablkcipher_giv_job(ctx->sh_desc_givenc, ctx->sh_desc_givenc_dma,
				edesc, req, iv_contig);
#ifdef DEBUG
	print_hex_dump(KERN_ERR,
		       "ablkcipher jobdesc@" __stringify(__LINE__) ": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, edesc->hw_desc,
		       desc_bytes(edesc->hw_desc), 1);
#endif
	desc = edesc->hw_desc;
	ret = caam_jr_enqueue(jrdev, desc, ablkcipher_encrypt_done, req);

	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		ablkcipher_unmap(jrdev, edesc, req);
		kfree(edesc);
	}

	return ret;
}

#define template_aead		template_u.aead
#define template_ablkcipher	template_u.ablkcipher
struct caam_alg_template {
	char name[CRYPTO_MAX_ALG_NAME];
	char driver_name[CRYPTO_MAX_ALG_NAME];
	unsigned int blocksize;
	u32 type;
	union {
		struct ablkcipher_alg ablkcipher;
	} template_u;
	u32 class1_alg_type;
	u32 class2_alg_type;
};

static struct caam_alg_template driver_algs[] = {
	/* ablkcipher descriptor */
	{
		.name = "cbc(aes)",
		.driver_name = "cbc-aes-caam",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_GIVCIPHER,
		.template_ablkcipher = {
			.setkey = ablkcipher_setkey,
			.encrypt = ablkcipher_encrypt,
			.decrypt = ablkcipher_decrypt,
			.givencrypt = ablkcipher_givencrypt,
			.geniv = "<built-in>",
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
			},
		.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
	},
	{
		.name = "cbc(des3_ede)",
		.driver_name = "cbc-3des-caam",
		.blocksize = DES3_EDE_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_GIVCIPHER,
		.template_ablkcipher = {
			.setkey = ablkcipher_setkey,
			.encrypt = ablkcipher_encrypt,
			.decrypt = ablkcipher_decrypt,
			.givencrypt = ablkcipher_givencrypt,
			.geniv = "<built-in>",
			.min_keysize = DES3_EDE_KEY_SIZE,
			.max_keysize = DES3_EDE_KEY_SIZE,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			},
		.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
	},
	{
		.name = "cbc(des)",
		.driver_name = "cbc-des-caam",
		.blocksize = DES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_GIVCIPHER,
		.template_ablkcipher = {
			.setkey = ablkcipher_setkey,
			.encrypt = ablkcipher_encrypt,
			.decrypt = ablkcipher_decrypt,
			.givencrypt = ablkcipher_givencrypt,
			.geniv = "<built-in>",
			.min_keysize = DES_KEY_SIZE,
			.max_keysize = DES_KEY_SIZE,
			.ivsize = DES_BLOCK_SIZE,
			},
		.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
	},
	{
		.name = "ctr(aes)",
		.driver_name = "ctr-aes-caam",
		.blocksize = 1,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = ablkcipher_setkey,
			.encrypt = ablkcipher_encrypt,
			.decrypt = ablkcipher_decrypt,
			.geniv = "chainiv",
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
			},
		.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CTR_MOD128,
	},
	{
		.name = "rfc3686(ctr(aes))",
		.driver_name = "rfc3686-ctr-aes-caam",
		.blocksize = 1,
		.type = CRYPTO_ALG_TYPE_GIVCIPHER,
		.template_ablkcipher = {
			.setkey = ablkcipher_setkey,
			.encrypt = ablkcipher_encrypt,
			.decrypt = ablkcipher_decrypt,
			.givencrypt = ablkcipher_givencrypt,
			.geniv = "<built-in>",
			.min_keysize = AES_MIN_KEY_SIZE +
				       CTR_RFC3686_NONCE_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE +
				       CTR_RFC3686_NONCE_SIZE,
			.ivsize = CTR_RFC3686_IV_SIZE,
			},
		.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CTR_MOD128,
	},
	{
		.name = "xts(aes)",
		.driver_name = "xts-aes-caam",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = xts_ablkcipher_setkey,
			.encrypt = ablkcipher_encrypt,
			.decrypt = ablkcipher_decrypt,
			.geniv = "eseqiv",
			.min_keysize = 2 * AES_MIN_KEY_SIZE,
			.max_keysize = 2 * AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
			},
		.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_XTS,
	},
};

static struct caam_aead_alg driver_aeads[] = {
	{
		.aead = {
			.base = {
				.cra_name = "rfc4106(gcm(aes))",
				.cra_driver_name = "rfc4106-gcm-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = rfc4106_setkey,
			.setauthsize = rfc4106_setauthsize,
			.encrypt = ipsec_gcm_encrypt,
			.decrypt = ipsec_gcm_decrypt,
			.ivsize = 8,
			.maxauthsize = AES_BLOCK_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_GCM,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "rfc4543(gcm(aes))",
				.cra_driver_name = "rfc4543-gcm-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = rfc4543_setkey,
			.setauthsize = rfc4543_setauthsize,
			.encrypt = ipsec_gcm_encrypt,
			.decrypt = ipsec_gcm_decrypt,
			.ivsize = 8,
			.maxauthsize = AES_BLOCK_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_GCM,
		},
	},
	/* Galois Counter Mode */
	{
		.aead = {
			.base = {
				.cra_name = "gcm(aes)",
				.cra_driver_name = "gcm-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = gcm_setkey,
			.setauthsize = gcm_setauthsize,
			.encrypt = gcm_encrypt,
			.decrypt = gcm_decrypt,
			.ivsize = 12,
			.maxauthsize = AES_BLOCK_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_GCM,
		},
	},
	/* single-pass ipsec_esp descriptor */
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),"
					    "ecb(cipher_null))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "ecb-cipher_null-caam",
				.cra_blocksize = NULL_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = NULL_IV_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),"
					    "ecb(cipher_null))",
				.cra_driver_name = "authenc-hmac-sha1-"
						   "ecb-cipher_null-caam",
				.cra_blocksize = NULL_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = NULL_IV_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),"
					    "ecb(cipher_null))",
				.cra_driver_name = "authenc-hmac-sha224-"
						   "ecb-cipher_null-caam",
				.cra_blocksize = NULL_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = NULL_IV_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),"
					    "ecb(cipher_null))",
				.cra_driver_name = "authenc-hmac-sha256-"
						   "ecb-cipher_null-caam",
				.cra_blocksize = NULL_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = NULL_IV_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha384),"
					    "ecb(cipher_null))",
				.cra_driver_name = "authenc-hmac-sha384-"
						   "ecb-cipher_null-caam",
				.cra_blocksize = NULL_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = NULL_IV_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),"
					    "ecb(cipher_null))",
				.cra_driver_name = "authenc-hmac-sha512-"
						   "ecb-cipher_null-caam",
				.cra_blocksize = NULL_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = NULL_IV_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),cbc(aes))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "cbc-aes-caam",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(md5),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-hmac-md5-"
						   "cbc-aes-caam",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha1-"
						   "cbc-aes-caam",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha1),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha1-cbc-aes-caam",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha224-"
						   "cbc-aes-caam",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha224),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha224-cbc-aes-caam",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha256-"
						   "cbc-aes-caam",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha256),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha256-cbc-aes-caam",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha384),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha384-"
						   "cbc-aes-caam",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha384),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha384-cbc-aes-caam",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha512-"
						   "cbc-aes-caam",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha512),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha512-cbc-aes-caam",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "cbc-des3_ede-caam",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(md5),"
					    "cbc(des3_ede)))",
				.cra_driver_name = "echainiv-authenc-hmac-md5-"
						   "cbc-des3_ede-caam",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha1-"
						   "cbc-des3_ede-caam",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha1),"
					    "cbc(des3_ede)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha1-"
						   "cbc-des3_ede-caam",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha224-"
						   "cbc-des3_ede-caam",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha224),"
					    "cbc(des3_ede)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha224-"
						   "cbc-des3_ede-caam",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha256-"
						   "cbc-des3_ede-caam",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha256),"
					    "cbc(des3_ede)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha256-"
						   "cbc-des3_ede-caam",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha384),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha384-"
						   "cbc-des3_ede-caam",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha384),"
					    "cbc(des3_ede)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha384-"
						   "cbc-des3_ede-caam",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha512-"
						   "cbc-des3_ede-caam",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha512),"
					    "cbc(des3_ede)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha512-"
						   "cbc-des3_ede-caam",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),cbc(des))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "cbc-des-caam",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(md5),"
					    "cbc(des)))",
				.cra_driver_name = "echainiv-authenc-hmac-md5-"
						   "cbc-des-caam",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),cbc(des))",
				.cra_driver_name = "authenc-hmac-sha1-"
						   "cbc-des-caam",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha1),"
					    "cbc(des)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha1-cbc-des-caam",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),cbc(des))",
				.cra_driver_name = "authenc-hmac-sha224-"
						   "cbc-des-caam",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha224),"
					    "cbc(des)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha224-cbc-des-caam",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),cbc(des))",
				.cra_driver_name = "authenc-hmac-sha256-"
						   "cbc-des-caam",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha256),"
					    "cbc(des)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha256-cbc-des-caam",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha384),cbc(des))",
				.cra_driver_name = "authenc-hmac-sha384-"
						   "cbc-des-caam",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha384),"
					    "cbc(des)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha384-cbc-des-caam",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),cbc(des))",
				.cra_driver_name = "authenc-hmac-sha512-"
						   "cbc-des-caam",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha512),"
					    "cbc(des)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha512-cbc-des-caam",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),"
					    "rfc3686(ctr(aes)))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "rfc3686-ctr-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "seqiv(authenc("
					    "hmac(md5),rfc3686(ctr(aes))))",
				.cra_driver_name = "seqiv-authenc-hmac-md5-"
						   "rfc3686-ctr-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),"
					    "rfc3686(ctr(aes)))",
				.cra_driver_name = "authenc-hmac-sha1-"
						   "rfc3686-ctr-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "seqiv(authenc("
					    "hmac(sha1),rfc3686(ctr(aes))))",
				.cra_driver_name = "seqiv-authenc-hmac-sha1-"
						   "rfc3686-ctr-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),"
					    "rfc3686(ctr(aes)))",
				.cra_driver_name = "authenc-hmac-sha224-"
						   "rfc3686-ctr-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "seqiv(authenc("
					    "hmac(sha224),rfc3686(ctr(aes))))",
				.cra_driver_name = "seqiv-authenc-hmac-sha224-"
						   "rfc3686-ctr-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),"
					    "rfc3686(ctr(aes)))",
				.cra_driver_name = "authenc-hmac-sha256-"
						   "rfc3686-ctr-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "seqiv(authenc(hmac(sha256),"
					    "rfc3686(ctr(aes))))",
				.cra_driver_name = "seqiv-authenc-hmac-sha256-"
						   "rfc3686-ctr-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha384),"
					    "rfc3686(ctr(aes)))",
				.cra_driver_name = "authenc-hmac-sha384-"
						   "rfc3686-ctr-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "seqiv(authenc(hmac(sha384),"
					    "rfc3686(ctr(aes))))",
				.cra_driver_name = "seqiv-authenc-hmac-sha384-"
						   "rfc3686-ctr-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),"
					    "rfc3686(ctr(aes)))",
				.cra_driver_name = "authenc-hmac-sha512-"
						   "rfc3686-ctr-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "seqiv(authenc(hmac(sha512),"
					    "rfc3686(ctr(aes))))",
				.cra_driver_name = "seqiv-authenc-hmac-sha512-"
						   "rfc3686-ctr-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
			.geniv = true,
		},
	},
};

struct caam_crypto_alg {
	struct crypto_alg crypto_alg;
	struct list_head entry;
	struct caam_alg_entry caam;
};

static int caam_init_common(struct caam_ctx *ctx, struct caam_alg_entry *caam)
{
	ctx->jrdev = caam_jr_alloc();
	if (IS_ERR(ctx->jrdev)) {
		pr_err("Job Ring Device allocation for transform failed\n");
		return PTR_ERR(ctx->jrdev);
	}

	/* copy descriptor header template value */
	ctx->cdata.algtype = OP_TYPE_CLASS1_ALG | caam->class1_alg_type;
	ctx->adata.algtype = OP_TYPE_CLASS2_ALG | caam->class2_alg_type;

	return 0;
}

static int caam_cra_init(struct crypto_tfm *tfm)
{
	struct crypto_alg *alg = tfm->__crt_alg;
	struct caam_crypto_alg *caam_alg =
		 container_of(alg, struct caam_crypto_alg, crypto_alg);
	struct caam_ctx *ctx = crypto_tfm_ctx(tfm);

	return caam_init_common(ctx, &caam_alg->caam);
}

static int caam_aead_init(struct crypto_aead *tfm)
{
	struct aead_alg *alg = crypto_aead_alg(tfm);
	struct caam_aead_alg *caam_alg =
		 container_of(alg, struct caam_aead_alg, aead);
	struct caam_ctx *ctx = crypto_aead_ctx(tfm);

	return caam_init_common(ctx, &caam_alg->caam);
}

static void caam_exit_common(struct caam_ctx *ctx)
{
	if (ctx->sh_desc_enc_dma &&
	    !dma_mapping_error(ctx->jrdev, ctx->sh_desc_enc_dma))
		dma_unmap_single(ctx->jrdev, ctx->sh_desc_enc_dma,
				 desc_bytes(ctx->sh_desc_enc), DMA_TO_DEVICE);
	if (ctx->sh_desc_dec_dma &&
	    !dma_mapping_error(ctx->jrdev, ctx->sh_desc_dec_dma))
		dma_unmap_single(ctx->jrdev, ctx->sh_desc_dec_dma,
				 desc_bytes(ctx->sh_desc_dec), DMA_TO_DEVICE);
	if (ctx->sh_desc_givenc_dma &&
	    !dma_mapping_error(ctx->jrdev, ctx->sh_desc_givenc_dma))
		dma_unmap_single(ctx->jrdev, ctx->sh_desc_givenc_dma,
				 desc_bytes(ctx->sh_desc_givenc),
				 DMA_TO_DEVICE);
	if (ctx->key_dma &&
	    !dma_mapping_error(ctx->jrdev, ctx->key_dma))
		dma_unmap_single(ctx->jrdev, ctx->key_dma,
				 ctx->cdata.keylen + ctx->adata.keylen_pad,
				 DMA_TO_DEVICE);

	caam_jr_free(ctx->jrdev);
}

static void caam_cra_exit(struct crypto_tfm *tfm)
{
	caam_exit_common(crypto_tfm_ctx(tfm));
}

static void caam_aead_exit(struct crypto_aead *tfm)
{
	caam_exit_common(crypto_aead_ctx(tfm));
}

static void __exit caam_algapi_exit(void)
{

	struct caam_crypto_alg *t_alg, *n;
	int i;

	for (i = 0; i < ARRAY_SIZE(driver_aeads); i++) {
		struct caam_aead_alg *t_alg = driver_aeads + i;

		if (t_alg->registered)
			crypto_unregister_aead(&t_alg->aead);
	}

	if (!alg_list.next)
		return;

	list_for_each_entry_safe(t_alg, n, &alg_list, entry) {
		crypto_unregister_alg(&t_alg->crypto_alg);
		list_del(&t_alg->entry);
		kfree(t_alg);
	}
}

static struct caam_crypto_alg *caam_alg_alloc(struct caam_alg_template
					      *template)
{
	struct caam_crypto_alg *t_alg;
	struct crypto_alg *alg;

	t_alg = kzalloc(sizeof(*t_alg), GFP_KERNEL);
	if (!t_alg) {
		pr_err("failed to allocate t_alg\n");
		return ERR_PTR(-ENOMEM);
	}

	alg = &t_alg->crypto_alg;

	snprintf(alg->cra_name, CRYPTO_MAX_ALG_NAME, "%s", template->name);
	snprintf(alg->cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
		 template->driver_name);
	alg->cra_module = THIS_MODULE;
	alg->cra_init = caam_cra_init;
	alg->cra_exit = caam_cra_exit;
	alg->cra_priority = CAAM_CRA_PRIORITY;
	alg->cra_blocksize = template->blocksize;
	alg->cra_alignmask = 0;
	alg->cra_ctxsize = sizeof(struct caam_ctx);
	alg->cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_KERN_DRIVER_ONLY |
			 template->type;
	switch (template->type) {
	case CRYPTO_ALG_TYPE_GIVCIPHER:
		alg->cra_type = &crypto_givcipher_type;
		alg->cra_ablkcipher = template->template_ablkcipher;
		break;
	case CRYPTO_ALG_TYPE_ABLKCIPHER:
		alg->cra_type = &crypto_ablkcipher_type;
		alg->cra_ablkcipher = template->template_ablkcipher;
		break;
	}

	t_alg->caam.class1_alg_type = template->class1_alg_type;
	t_alg->caam.class2_alg_type = template->class2_alg_type;

	return t_alg;
}

static void caam_aead_alg_init(struct caam_aead_alg *t_alg)
{
	struct aead_alg *alg = &t_alg->aead;

	alg->base.cra_module = THIS_MODULE;
	alg->base.cra_priority = CAAM_CRA_PRIORITY;
	alg->base.cra_ctxsize = sizeof(struct caam_ctx);
	alg->base.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_KERN_DRIVER_ONLY;

	alg->init = caam_aead_init;
	alg->exit = caam_aead_exit;
}

static int __init caam_algapi_init(void)
{
	struct device_node *dev_node;
	struct platform_device *pdev;
	struct device *ctrldev;
	struct caam_drv_private *priv;
	int i = 0, err = 0;
	u32 cha_vid, cha_inst, des_inst, aes_inst, md_inst;
	unsigned int md_limit = SHA512_DIGEST_SIZE;
	bool registered = false;

	dev_node = of_find_compatible_node(NULL, NULL, "fsl,sec-v4.0");
	if (!dev_node) {
		dev_node = of_find_compatible_node(NULL, NULL, "fsl,sec4.0");
		if (!dev_node)
			return -ENODEV;
	}

	pdev = of_find_device_by_node(dev_node);
	if (!pdev) {
		of_node_put(dev_node);
		return -ENODEV;
	}

	ctrldev = &pdev->dev;
	priv = dev_get_drvdata(ctrldev);
	of_node_put(dev_node);

	/*
	 * If priv is NULL, it's probably because the caam driver wasn't
	 * properly initialized (e.g. RNG4 init failed). Thus, bail out here.
	 */
	if (!priv)
		return -ENODEV;


	INIT_LIST_HEAD(&alg_list);

	/*
	 * Register crypto algorithms the device supports.
	 * First, detect presence and attributes of DES, AES, and MD blocks.
	 */
	cha_vid = rd_reg32(&priv->ctrl->perfmon.cha_id_ls);
	cha_inst = rd_reg32(&priv->ctrl->perfmon.cha_num_ls);
	des_inst = (cha_inst & CHA_ID_LS_DES_MASK) >> CHA_ID_LS_DES_SHIFT;
	aes_inst = (cha_inst & CHA_ID_LS_AES_MASK) >> CHA_ID_LS_AES_SHIFT;
	md_inst = (cha_inst & CHA_ID_LS_MD_MASK) >> CHA_ID_LS_MD_SHIFT;

	/* If MD is present, limit digest size based on LP256 */
	if (md_inst && ((cha_vid & CHA_ID_LS_MD_MASK) == CHA_ID_LS_MD_LP256))
		md_limit = SHA256_DIGEST_SIZE;

	for (i = 0; i < ARRAY_SIZE(driver_algs); i++) {
		struct caam_crypto_alg *t_alg;
		struct caam_alg_template *alg = driver_algs + i;
		u32 alg_sel = alg->class1_alg_type & OP_ALG_ALGSEL_MASK;

		/* Skip DES algorithms if not supported by device */
		if (!des_inst &&
		    ((alg_sel == OP_ALG_ALGSEL_3DES) ||
		     (alg_sel == OP_ALG_ALGSEL_DES)))
				continue;

		/* Skip AES algorithms if not supported by device */
		if (!aes_inst && (alg_sel == OP_ALG_ALGSEL_AES))
				continue;

		/*
		 * Check support for AES modes not available
		 * on LP devices.
		 */
		if ((cha_vid & CHA_ID_LS_AES_MASK) == CHA_ID_LS_AES_LP)
			if ((alg->class1_alg_type & OP_ALG_AAI_MASK) ==
			     OP_ALG_AAI_XTS)
				continue;

		t_alg = caam_alg_alloc(alg);
		if (IS_ERR(t_alg)) {
			err = PTR_ERR(t_alg);
			pr_warn("%s alg allocation failed\n", alg->driver_name);
			continue;
		}

		err = crypto_register_alg(&t_alg->crypto_alg);
		if (err) {
			pr_warn("%s alg registration failed\n",
				t_alg->crypto_alg.cra_driver_name);
			kfree(t_alg);
			continue;
		}

		list_add_tail(&t_alg->entry, &alg_list);
		registered = true;
	}

	for (i = 0; i < ARRAY_SIZE(driver_aeads); i++) {
		struct caam_aead_alg *t_alg = driver_aeads + i;
		u32 c1_alg_sel = t_alg->caam.class1_alg_type &
				 OP_ALG_ALGSEL_MASK;
		u32 c2_alg_sel = t_alg->caam.class2_alg_type &
				 OP_ALG_ALGSEL_MASK;
		u32 alg_aai = t_alg->caam.class1_alg_type & OP_ALG_AAI_MASK;

		/* Skip DES algorithms if not supported by device */
		if (!des_inst &&
		    ((c1_alg_sel == OP_ALG_ALGSEL_3DES) ||
		     (c1_alg_sel == OP_ALG_ALGSEL_DES)))
				continue;

		/* Skip AES algorithms if not supported by device */
		if (!aes_inst && (c1_alg_sel == OP_ALG_ALGSEL_AES))
				continue;

		/*
		 * Check support for AES algorithms not available
		 * on LP devices.
		 */
		if ((cha_vid & CHA_ID_LS_AES_MASK) == CHA_ID_LS_AES_LP)
			if (alg_aai == OP_ALG_AAI_GCM)
				continue;

		/*
		 * Skip algorithms requiring message digests
		 * if MD or MD size is not supported by device.
		 */
		if (c2_alg_sel &&
		    (!md_inst || (t_alg->aead.maxauthsize > md_limit)))
				continue;

		caam_aead_alg_init(t_alg);

		err = crypto_register_aead(&t_alg->aead);
		if (err) {
			pr_warn("%s alg registration failed\n",
				t_alg->aead.base.cra_driver_name);
			continue;
		}

		t_alg->registered = true;
		registered = true;
	}

	if (registered)
		pr_info("caam algorithms registered in /proc/crypto\n");

	return err;
}

module_init(caam_algapi_init);
module_exit(caam_algapi_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FSL CAAM support for crypto API");
MODULE_AUTHOR("Freescale Semiconductor - NMG/STC");
