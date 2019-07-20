// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2012-2019 ARM Limited (or its affiliates). */

#include <crypto/ctr.h>
#include "cc_driver.h"
#include "cc_ivgen.h"
#include "cc_request_mgr.h"
#include "cc_sram_mgr.h"
#include "cc_buffer_mgr.h"

/* The max. size of pool *MUST* be <= SRAM total size */
#define CC_IVPOOL_SIZE 1024
/* The first 32B fraction of pool are dedicated to the
 * next encryption "key" & "IV" for pool regeneration
 */
#define CC_IVPOOL_META_SIZE (CC_AES_IV_SIZE + AES_KEYSIZE_128)
#define CC_IVPOOL_GEN_SEQ_LEN	4

/**
 * struct cc_ivgen_ctx -IV pool generation context
 * @pool:          the start address of the iv-pool resides in internal RAM
 * @ctr_key_dma:   address of pool's encryption key material in internal RAM
 * @ctr_iv_dma:    address of pool's counter iv in internal RAM
 * @next_iv_ofs:   the offset to the next available IV in pool
 * @pool_meta:     virt. address of the initial enc. key/IV
 * @pool_meta_dma: phys. address of the initial enc. key/IV
 */
struct cc_ivgen_ctx {
	cc_sram_addr_t pool;
	cc_sram_addr_t ctr_key;
	cc_sram_addr_t ctr_iv;
	u32 next_iv_ofs;
	u8 *pool_meta;
	dma_addr_t pool_meta_dma;
};

/*!
 * Generates CC_IVPOOL_SIZE of random bytes by
 * encrypting 0's using AES128-CTR.
 *
 * \param ivgen iv-pool context
 * \param iv_seq IN/OUT array to the descriptors sequence
 * \param iv_seq_len IN/OUT pointer to the sequence length
 */
static int cc_gen_iv_pool(struct cc_ivgen_ctx *ivgen_ctx,
			  struct cc_hw_desc iv_seq[], unsigned int *iv_seq_len)
{
	unsigned int idx = *iv_seq_len;

	if ((*iv_seq_len + CC_IVPOOL_GEN_SEQ_LEN) > CC_IVPOOL_SEQ_LEN) {
		/* The sequence will be longer than allowed */
		return -EINVAL;
	}
	/* Setup key */
	hw_desc_init(&iv_seq[idx]);
	set_din_sram(&iv_seq[idx], ivgen_ctx->ctr_key, AES_KEYSIZE_128);
	set_setup_mode(&iv_seq[idx], SETUP_LOAD_KEY0);
	set_cipher_config0(&iv_seq[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_flow_mode(&iv_seq[idx], S_DIN_to_AES);
	set_key_size_aes(&iv_seq[idx], CC_AES_128_BIT_KEY_SIZE);
	set_cipher_mode(&iv_seq[idx], DRV_CIPHER_CTR);
	idx++;

	/* Setup cipher state */
	hw_desc_init(&iv_seq[idx]);
	set_din_sram(&iv_seq[idx], ivgen_ctx->ctr_iv, CC_AES_IV_SIZE);
	set_cipher_config0(&iv_seq[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_flow_mode(&iv_seq[idx], S_DIN_to_AES);
	set_setup_mode(&iv_seq[idx], SETUP_LOAD_STATE1);
	set_key_size_aes(&iv_seq[idx], CC_AES_128_BIT_KEY_SIZE);
	set_cipher_mode(&iv_seq[idx], DRV_CIPHER_CTR);
	idx++;

	/* Perform dummy encrypt to skip first block */
	hw_desc_init(&iv_seq[idx]);
	set_din_const(&iv_seq[idx], 0, CC_AES_IV_SIZE);
	set_dout_sram(&iv_seq[idx], ivgen_ctx->pool, CC_AES_IV_SIZE);
	set_flow_mode(&iv_seq[idx], DIN_AES_DOUT);
	idx++;

	/* Generate IV pool */
	hw_desc_init(&iv_seq[idx]);
	set_din_const(&iv_seq[idx], 0, CC_IVPOOL_SIZE);
	set_dout_sram(&iv_seq[idx], ivgen_ctx->pool, CC_IVPOOL_SIZE);
	set_flow_mode(&iv_seq[idx], DIN_AES_DOUT);
	idx++;

	*iv_seq_len = idx; /* Update sequence length */

	/* queue ordering assures pool readiness */
	ivgen_ctx->next_iv_ofs = CC_IVPOOL_META_SIZE;

	return 0;
}

/*!
 * Generates the initial pool in SRAM.
 * This function should be invoked when resuming driver.
 *
 * \param drvdata
 *
 * \return int Zero for success, negative value otherwise.
 */
int cc_init_iv_sram(struct cc_drvdata *drvdata)
{
	struct cc_ivgen_ctx *ivgen_ctx = drvdata->ivgen_handle;
	struct cc_hw_desc iv_seq[CC_IVPOOL_SEQ_LEN];
	unsigned int iv_seq_len = 0;
	int rc;

	/* Generate initial enc. key/iv */
	get_random_bytes(ivgen_ctx->pool_meta, CC_IVPOOL_META_SIZE);

	/* The first 32B reserved for the enc. Key/IV */
	ivgen_ctx->ctr_key = ivgen_ctx->pool;
	ivgen_ctx->ctr_iv = ivgen_ctx->pool + AES_KEYSIZE_128;

	/* Copy initial enc. key and IV to SRAM at a single descriptor */
	hw_desc_init(&iv_seq[iv_seq_len]);
	set_din_type(&iv_seq[iv_seq_len], DMA_DLLI, ivgen_ctx->pool_meta_dma,
		     CC_IVPOOL_META_SIZE, NS_BIT);
	set_dout_sram(&iv_seq[iv_seq_len], ivgen_ctx->pool,
		      CC_IVPOOL_META_SIZE);
	set_flow_mode(&iv_seq[iv_seq_len], BYPASS);
	iv_seq_len++;

	/* Generate initial pool */
	rc = cc_gen_iv_pool(ivgen_ctx, iv_seq, &iv_seq_len);
	if (rc)
		return rc;

	/* Fire-and-forget */
	return send_request_init(drvdata, iv_seq, iv_seq_len);
}

/*!
 * Free iv-pool and ivgen context.
 *
 * \param drvdata
 */
void cc_ivgen_fini(struct cc_drvdata *drvdata)
{
	struct cc_ivgen_ctx *ivgen_ctx = drvdata->ivgen_handle;
	struct device *device = &drvdata->plat_dev->dev;

	if (!ivgen_ctx)
		return;

	if (ivgen_ctx->pool_meta) {
		memset(ivgen_ctx->pool_meta, 0, CC_IVPOOL_META_SIZE);
		dma_free_coherent(device, CC_IVPOOL_META_SIZE,
				  ivgen_ctx->pool_meta,
				  ivgen_ctx->pool_meta_dma);
	}

	ivgen_ctx->pool = NULL_SRAM_ADDR;
}

/*!
 * Allocates iv-pool and maps resources.
 * This function generates the first IV pool.
 *
 * \param drvdata Driver's private context
 *
 * \return int Zero for success, negative value otherwise.
 */
int cc_ivgen_init(struct cc_drvdata *drvdata)
{
	struct cc_ivgen_ctx *ivgen_ctx;
	struct device *device = &drvdata->plat_dev->dev;
	int rc;

	/* Allocate "this" context */
	ivgen_ctx = devm_kzalloc(device, sizeof(*ivgen_ctx), GFP_KERNEL);
	if (!ivgen_ctx)
		return -ENOMEM;

	drvdata->ivgen_handle = ivgen_ctx;

	/* Allocate pool's header for initial enc. key/IV */
	ivgen_ctx->pool_meta = dma_alloc_coherent(device, CC_IVPOOL_META_SIZE,
						  &ivgen_ctx->pool_meta_dma,
						  GFP_KERNEL);
	if (!ivgen_ctx->pool_meta) {
		dev_err(device, "Not enough memory to allocate DMA of pool_meta (%u B)\n",
			CC_IVPOOL_META_SIZE);
		rc = -ENOMEM;
		goto out;
	}
	/* Allocate IV pool in SRAM */
	ivgen_ctx->pool = cc_sram_alloc(drvdata, CC_IVPOOL_SIZE);
	if (ivgen_ctx->pool == NULL_SRAM_ADDR) {
		dev_err(device, "SRAM pool exhausted\n");
		rc = -ENOMEM;
		goto out;
	}

	return cc_init_iv_sram(drvdata);

out:
	cc_ivgen_fini(drvdata);
	return rc;
}

/*!
 * Acquires 16 Bytes IV from the iv-pool
 *
 * \param drvdata Driver private context
 * \param iv_out_dma Array of physical IV out addresses
 * \param iv_out_dma_len Length of iv_out_dma array (additional elements
 *                       of iv_out_dma array are ignore)
 * \param iv_out_size May be 8 or 16 bytes long
 * \param iv_seq IN/OUT array to the descriptors sequence
 * \param iv_seq_len IN/OUT pointer to the sequence length
 *
 * \return int Zero for success, negative value otherwise.
 */
int cc_get_iv(struct cc_drvdata *drvdata, dma_addr_t iv_out_dma[],
	      unsigned int iv_out_dma_len, unsigned int iv_out_size,
	      struct cc_hw_desc iv_seq[], unsigned int *iv_seq_len)
{
	struct cc_ivgen_ctx *ivgen_ctx = drvdata->ivgen_handle;
	unsigned int idx = *iv_seq_len;
	struct device *dev = drvdata_to_dev(drvdata);
	unsigned int t;

	if (iv_out_size != CC_AES_IV_SIZE &&
	    iv_out_size != CTR_RFC3686_IV_SIZE) {
		return -EINVAL;
	}
	if ((iv_out_dma_len + 1) > CC_IVPOOL_SEQ_LEN) {
		/* The sequence will be longer than allowed */
		return -EINVAL;
	}

	/* check that number of generated IV is limited to max dma address
	 * iv buffer size
	 */
	if (iv_out_dma_len > CC_MAX_IVGEN_DMA_ADDRESSES) {
		/* The sequence will be longer than allowed */
		return -EINVAL;
	}

	for (t = 0; t < iv_out_dma_len; t++) {
		/* Acquire IV from pool */
		hw_desc_init(&iv_seq[idx]);
		set_din_sram(&iv_seq[idx], (ivgen_ctx->pool +
					    ivgen_ctx->next_iv_ofs),
			     iv_out_size);
		set_dout_dlli(&iv_seq[idx], iv_out_dma[t], iv_out_size,
			      NS_BIT, 0);
		set_flow_mode(&iv_seq[idx], BYPASS);
		idx++;
	}

	/* Bypass operation is proceeded by crypto sequence, hence must
	 *  assure bypass-write-transaction by a memory barrier
	 */
	hw_desc_init(&iv_seq[idx]);
	set_din_no_dma(&iv_seq[idx], 0, 0xfffff0);
	set_dout_no_dma(&iv_seq[idx], 0, 0, 1);
	idx++;

	*iv_seq_len = idx; /* update seq length */

	/* Update iv index */
	ivgen_ctx->next_iv_ofs += iv_out_size;

	if ((CC_IVPOOL_SIZE - ivgen_ctx->next_iv_ofs) < CC_AES_IV_SIZE) {
		dev_dbg(dev, "Pool exhausted, regenerating iv-pool\n");
		/* pool is drained -regenerate it! */
		return cc_gen_iv_pool(ivgen_ctx, iv_seq, iv_seq_len);
	}

	return 0;
}
