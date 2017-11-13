/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/platform_device.h>
#include <crypto/ctr.h>
#include "ssi_config.h"
#include "ssi_driver.h"
#include "ssi_ivgen.h"
#include "ssi_request_mgr.h"
#include "ssi_sram_mgr.h"
#include "ssi_buffer_mgr.h"

/* The max. size of pool *MUST* be <= SRAM total size */
#define SSI_IVPOOL_SIZE 1024
/* The first 32B fraction of pool are dedicated to the
 * next encryption "key" & "IV" for pool regeneration
 */
#define SSI_IVPOOL_META_SIZE (CC_AES_IV_SIZE + AES_KEYSIZE_128)
#define SSI_IVPOOL_GEN_SEQ_LEN	4

/**
 * struct ssi_ivgen_ctx -IV pool generation context
 * @pool:          the start address of the iv-pool resides in internal RAM
 * @ctr_key_dma:   address of pool's encryption key material in internal RAM
 * @ctr_iv_dma:    address of pool's counter iv in internal RAM
 * @next_iv_ofs:   the offset to the next available IV in pool
 * @pool_meta:     virt. address of the initial enc. key/IV
 * @pool_meta_dma: phys. address of the initial enc. key/IV
 */
struct ssi_ivgen_ctx {
	ssi_sram_addr_t pool;
	ssi_sram_addr_t ctr_key;
	ssi_sram_addr_t ctr_iv;
	u32 next_iv_ofs;
	u8 *pool_meta;
	dma_addr_t pool_meta_dma;
};

/*!
 * Generates SSI_IVPOOL_SIZE of random bytes by
 * encrypting 0's using AES128-CTR.
 *
 * \param ivgen iv-pool context
 * \param iv_seq IN/OUT array to the descriptors sequence
 * \param iv_seq_len IN/OUT pointer to the sequence length
 */
static int ssi_ivgen_generate_pool(
	struct ssi_ivgen_ctx *ivgen_ctx,
	struct cc_hw_desc iv_seq[],
	unsigned int *iv_seq_len)
{
	unsigned int idx = *iv_seq_len;

	if ((*iv_seq_len + SSI_IVPOOL_GEN_SEQ_LEN) > SSI_IVPOOL_SEQ_LEN) {
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
	set_din_const(&iv_seq[idx], 0, SSI_IVPOOL_SIZE);
	set_dout_sram(&iv_seq[idx], ivgen_ctx->pool, SSI_IVPOOL_SIZE);
	set_flow_mode(&iv_seq[idx], DIN_AES_DOUT);
	idx++;

	*iv_seq_len = idx; /* Update sequence length */

	/* queue ordering assures pool readiness */
	ivgen_ctx->next_iv_ofs = SSI_IVPOOL_META_SIZE;

	return 0;
}

/*!
 * Generates the initial pool in SRAM.
 * This function should be invoked when resuming DX driver.
 *
 * \param drvdata
 *
 * \return int Zero for success, negative value otherwise.
 */
int ssi_ivgen_init_sram_pool(struct ssi_drvdata *drvdata)
{
	struct ssi_ivgen_ctx *ivgen_ctx = drvdata->ivgen_handle;
	struct cc_hw_desc iv_seq[SSI_IVPOOL_SEQ_LEN];
	unsigned int iv_seq_len = 0;
	int rc;

	/* Generate initial enc. key/iv */
	get_random_bytes(ivgen_ctx->pool_meta, SSI_IVPOOL_META_SIZE);

	/* The first 32B reserved for the enc. Key/IV */
	ivgen_ctx->ctr_key = ivgen_ctx->pool;
	ivgen_ctx->ctr_iv = ivgen_ctx->pool + AES_KEYSIZE_128;

	/* Copy initial enc. key and IV to SRAM at a single descriptor */
	hw_desc_init(&iv_seq[iv_seq_len]);
	set_din_type(&iv_seq[iv_seq_len], DMA_DLLI, ivgen_ctx->pool_meta_dma,
		     SSI_IVPOOL_META_SIZE, NS_BIT);
	set_dout_sram(&iv_seq[iv_seq_len], ivgen_ctx->pool,
		      SSI_IVPOOL_META_SIZE);
	set_flow_mode(&iv_seq[iv_seq_len], BYPASS);
	iv_seq_len++;

	/* Generate initial pool */
	rc = ssi_ivgen_generate_pool(ivgen_ctx, iv_seq, &iv_seq_len);
	if (unlikely(rc))
		return rc;

	/* Fire-and-forget */
	return send_request_init(drvdata, iv_seq, iv_seq_len);
}

/*!
 * Free iv-pool and ivgen context.
 *
 * \param drvdata
 */
void ssi_ivgen_fini(struct ssi_drvdata *drvdata)
{
	struct ssi_ivgen_ctx *ivgen_ctx = drvdata->ivgen_handle;
	struct device *device = &drvdata->plat_dev->dev;

	if (!ivgen_ctx)
		return;

	if (ivgen_ctx->pool_meta) {
		memset(ivgen_ctx->pool_meta, 0, SSI_IVPOOL_META_SIZE);
		dma_free_coherent(device, SSI_IVPOOL_META_SIZE,
				  ivgen_ctx->pool_meta,
				  ivgen_ctx->pool_meta_dma);
	}

	ivgen_ctx->pool = NULL_SRAM_ADDR;

	/* release "this" context */
	kfree(ivgen_ctx);
}

/*!
 * Allocates iv-pool and maps resources.
 * This function generates the first IV pool.
 *
 * \param drvdata Driver's private context
 *
 * \return int Zero for success, negative value otherwise.
 */
int ssi_ivgen_init(struct ssi_drvdata *drvdata)
{
	struct ssi_ivgen_ctx *ivgen_ctx;
	struct device *device = &drvdata->plat_dev->dev;
	int rc;

	/* Allocate "this" context */
	drvdata->ivgen_handle = kzalloc(sizeof(*drvdata->ivgen_handle),
					GFP_KERNEL);
	if (!drvdata->ivgen_handle)
		return -ENOMEM;

	ivgen_ctx = drvdata->ivgen_handle;

	/* Allocate pool's header for initial enc. key/IV */
	ivgen_ctx->pool_meta = dma_alloc_coherent(device, SSI_IVPOOL_META_SIZE,
						  &ivgen_ctx->pool_meta_dma,
						  GFP_KERNEL);
	if (!ivgen_ctx->pool_meta) {
		dev_err(device, "Not enough memory to allocate DMA of pool_meta (%u B)\n",
			SSI_IVPOOL_META_SIZE);
		rc = -ENOMEM;
		goto out;
	}
	/* Allocate IV pool in SRAM */
	ivgen_ctx->pool = cc_sram_alloc(drvdata, SSI_IVPOOL_SIZE);
	if (ivgen_ctx->pool == NULL_SRAM_ADDR) {
		dev_err(device, "SRAM pool exhausted\n");
		rc = -ENOMEM;
		goto out;
	}

	return ssi_ivgen_init_sram_pool(drvdata);

out:
	ssi_ivgen_fini(drvdata);
	return rc;
}

/*!
 * Acquires 16 Bytes IV from the iv-pool
 *
 * \param drvdata Driver private context
 * \param iv_out_dma Array of physical IV out addresses
 * \param iv_out_dma_len Length of iv_out_dma array (additional elements of iv_out_dma array are ignore)
 * \param iv_out_size May be 8 or 16 bytes long
 * \param iv_seq IN/OUT array to the descriptors sequence
 * \param iv_seq_len IN/OUT pointer to the sequence length
 *
 * \return int Zero for success, negative value otherwise.
 */
int ssi_ivgen_getiv(
	struct ssi_drvdata *drvdata,
	dma_addr_t iv_out_dma[],
	unsigned int iv_out_dma_len,
	unsigned int iv_out_size,
	struct cc_hw_desc iv_seq[],
	unsigned int *iv_seq_len)
{
	struct ssi_ivgen_ctx *ivgen_ctx = drvdata->ivgen_handle;
	unsigned int idx = *iv_seq_len;
	struct device *dev = drvdata_to_dev(drvdata);
	unsigned int t;

	if (iv_out_size != CC_AES_IV_SIZE &&
	    iv_out_size != CTR_RFC3686_IV_SIZE) {
		return -EINVAL;
	}
	if ((iv_out_dma_len + 1) > SSI_IVPOOL_SEQ_LEN) {
		/* The sequence will be longer than allowed */
		return -EINVAL;
	}

	//check that number of generated IV is limited to max dma address iv buffer size
	if (iv_out_dma_len > SSI_MAX_IVGEN_DMA_ADDRESSES) {
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

	if ((SSI_IVPOOL_SIZE - ivgen_ctx->next_iv_ofs) < CC_AES_IV_SIZE) {
		dev_dbg(dev, "Pool exhausted, regenerating iv-pool\n");
		/* pool is drained -regenerate it! */
		return ssi_ivgen_generate_pool(ivgen_ctx, iv_seq, iv_seq_len);
	}

	return 0;
}

