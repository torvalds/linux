// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * AMCC SoC PPC4xx Crypto Driver
 *
 * Copyright (c) 2008 Applied Micro Circuits Corporation.
 * All rights reserved. James Hsiao <jhsiao@amcc.com>
 *
 * This file implements AMCC crypto offload Linux device driver for use with
 * Linux CryptoAPI.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/spinlock_types.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <asm/dcr.h>
#include <asm/dcr-regs.h>
#include <asm/cacheflush.h>
#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/ctr.h>
#include <crypto/gcm.h>
#include <crypto/sha.h>
#include <crypto/rng.h>
#include <crypto/scatterwalk.h>
#include <crypto/skcipher.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/rng.h>
#include <crypto/internal/skcipher.h>
#include "crypto4xx_reg_def.h"
#include "crypto4xx_core.h"
#include "crypto4xx_sa.h"
#include "crypto4xx_trng.h"

#define PPC4XX_SEC_VERSION_STR			"0.5"

/**
 * PPC4xx Crypto Engine Initialization Routine
 */
static void crypto4xx_hw_init(struct crypto4xx_device *dev)
{
	union ce_ring_size ring_size;
	union ce_ring_control ring_ctrl;
	union ce_part_ring_size part_ring_size;
	union ce_io_threshold io_threshold;
	u32 rand_num;
	union ce_pe_dma_cfg pe_dma_cfg;
	u32 device_ctrl;

	writel(PPC4XX_BYTE_ORDER, dev->ce_base + CRYPTO4XX_BYTE_ORDER_CFG);
	/* setup pe dma, include reset sg, pdr and pe, then release reset */
	pe_dma_cfg.w = 0;
	pe_dma_cfg.bf.bo_sgpd_en = 1;
	pe_dma_cfg.bf.bo_data_en = 0;
	pe_dma_cfg.bf.bo_sa_en = 1;
	pe_dma_cfg.bf.bo_pd_en = 1;
	pe_dma_cfg.bf.dynamic_sa_en = 1;
	pe_dma_cfg.bf.reset_sg = 1;
	pe_dma_cfg.bf.reset_pdr = 1;
	pe_dma_cfg.bf.reset_pe = 1;
	writel(pe_dma_cfg.w, dev->ce_base + CRYPTO4XX_PE_DMA_CFG);
	/* un reset pe,sg and pdr */
	pe_dma_cfg.bf.pe_mode = 0;
	pe_dma_cfg.bf.reset_sg = 0;
	pe_dma_cfg.bf.reset_pdr = 0;
	pe_dma_cfg.bf.reset_pe = 0;
	pe_dma_cfg.bf.bo_td_en = 0;
	writel(pe_dma_cfg.w, dev->ce_base + CRYPTO4XX_PE_DMA_CFG);
	writel(dev->pdr_pa, dev->ce_base + CRYPTO4XX_PDR_BASE);
	writel(dev->pdr_pa, dev->ce_base + CRYPTO4XX_RDR_BASE);
	writel(PPC4XX_PRNG_CTRL_AUTO_EN, dev->ce_base + CRYPTO4XX_PRNG_CTRL);
	get_random_bytes(&rand_num, sizeof(rand_num));
	writel(rand_num, dev->ce_base + CRYPTO4XX_PRNG_SEED_L);
	get_random_bytes(&rand_num, sizeof(rand_num));
	writel(rand_num, dev->ce_base + CRYPTO4XX_PRNG_SEED_H);
	ring_size.w = 0;
	ring_size.bf.ring_offset = PPC4XX_PD_SIZE;
	ring_size.bf.ring_size   = PPC4XX_NUM_PD;
	writel(ring_size.w, dev->ce_base + CRYPTO4XX_RING_SIZE);
	ring_ctrl.w = 0;
	writel(ring_ctrl.w, dev->ce_base + CRYPTO4XX_RING_CTRL);
	device_ctrl = readl(dev->ce_base + CRYPTO4XX_DEVICE_CTRL);
	device_ctrl |= PPC4XX_DC_3DES_EN;
	writel(device_ctrl, dev->ce_base + CRYPTO4XX_DEVICE_CTRL);
	writel(dev->gdr_pa, dev->ce_base + CRYPTO4XX_GATH_RING_BASE);
	writel(dev->sdr_pa, dev->ce_base + CRYPTO4XX_SCAT_RING_BASE);
	part_ring_size.w = 0;
	part_ring_size.bf.sdr_size = PPC4XX_SDR_SIZE;
	part_ring_size.bf.gdr_size = PPC4XX_GDR_SIZE;
	writel(part_ring_size.w, dev->ce_base + CRYPTO4XX_PART_RING_SIZE);
	writel(PPC4XX_SD_BUFFER_SIZE, dev->ce_base + CRYPTO4XX_PART_RING_CFG);
	io_threshold.w = 0;
	io_threshold.bf.output_threshold = PPC4XX_OUTPUT_THRESHOLD;
	io_threshold.bf.input_threshold  = PPC4XX_INPUT_THRESHOLD;
	writel(io_threshold.w, dev->ce_base + CRYPTO4XX_IO_THRESHOLD);
	writel(0, dev->ce_base + CRYPTO4XX_PDR_BASE_UADDR);
	writel(0, dev->ce_base + CRYPTO4XX_RDR_BASE_UADDR);
	writel(0, dev->ce_base + CRYPTO4XX_PKT_SRC_UADDR);
	writel(0, dev->ce_base + CRYPTO4XX_PKT_DEST_UADDR);
	writel(0, dev->ce_base + CRYPTO4XX_SA_UADDR);
	writel(0, dev->ce_base + CRYPTO4XX_GATH_RING_BASE_UADDR);
	writel(0, dev->ce_base + CRYPTO4XX_SCAT_RING_BASE_UADDR);
	/* un reset pe,sg and pdr */
	pe_dma_cfg.bf.pe_mode = 1;
	pe_dma_cfg.bf.reset_sg = 0;
	pe_dma_cfg.bf.reset_pdr = 0;
	pe_dma_cfg.bf.reset_pe = 0;
	pe_dma_cfg.bf.bo_td_en = 0;
	writel(pe_dma_cfg.w, dev->ce_base + CRYPTO4XX_PE_DMA_CFG);
	/*clear all pending interrupt*/
	writel(PPC4XX_INTERRUPT_CLR, dev->ce_base + CRYPTO4XX_INT_CLR);
	writel(PPC4XX_INT_DESCR_CNT, dev->ce_base + CRYPTO4XX_INT_DESCR_CNT);
	writel(PPC4XX_INT_DESCR_CNT, dev->ce_base + CRYPTO4XX_INT_DESCR_CNT);
	writel(PPC4XX_INT_CFG, dev->ce_base + CRYPTO4XX_INT_CFG);
	if (dev->is_revb) {
		writel(PPC4XX_INT_TIMEOUT_CNT_REVB << 10,
		       dev->ce_base + CRYPTO4XX_INT_TIMEOUT_CNT);
		writel(PPC4XX_PD_DONE_INT | PPC4XX_TMO_ERR_INT,
		       dev->ce_base + CRYPTO4XX_INT_EN);
	} else {
		writel(PPC4XX_PD_DONE_INT, dev->ce_base + CRYPTO4XX_INT_EN);
	}
}

int crypto4xx_alloc_sa(struct crypto4xx_ctx *ctx, u32 size)
{
	ctx->sa_in = kcalloc(size, 4, GFP_ATOMIC);
	if (ctx->sa_in == NULL)
		return -ENOMEM;

	ctx->sa_out = kcalloc(size, 4, GFP_ATOMIC);
	if (ctx->sa_out == NULL) {
		kfree(ctx->sa_in);
		ctx->sa_in = NULL;
		return -ENOMEM;
	}

	ctx->sa_len = size;

	return 0;
}

void crypto4xx_free_sa(struct crypto4xx_ctx *ctx)
{
	kfree(ctx->sa_in);
	ctx->sa_in = NULL;
	kfree(ctx->sa_out);
	ctx->sa_out = NULL;
	ctx->sa_len = 0;
}

/**
 * alloc memory for the gather ring
 * no need to alloc buf for the ring
 * gdr_tail, gdr_head and gdr_count are initialized by this function
 */
static u32 crypto4xx_build_pdr(struct crypto4xx_device *dev)
{
	int i;
	dev->pdr = dma_alloc_coherent(dev->core_dev->device,
				      sizeof(struct ce_pd) * PPC4XX_NUM_PD,
				      &dev->pdr_pa, GFP_KERNEL);
	if (!dev->pdr)
		return -ENOMEM;

	dev->pdr_uinfo = kcalloc(PPC4XX_NUM_PD, sizeof(struct pd_uinfo),
				 GFP_KERNEL);
	if (!dev->pdr_uinfo) {
		dma_free_coherent(dev->core_dev->device,
				  sizeof(struct ce_pd) * PPC4XX_NUM_PD,
				  dev->pdr,
				  dev->pdr_pa);
		return -ENOMEM;
	}
	dev->shadow_sa_pool = dma_alloc_coherent(dev->core_dev->device,
				   sizeof(union shadow_sa_buf) * PPC4XX_NUM_PD,
				   &dev->shadow_sa_pool_pa,
				   GFP_KERNEL);
	if (!dev->shadow_sa_pool)
		return -ENOMEM;

	dev->shadow_sr_pool = dma_alloc_coherent(dev->core_dev->device,
			 sizeof(struct sa_state_record) * PPC4XX_NUM_PD,
			 &dev->shadow_sr_pool_pa, GFP_KERNEL);
	if (!dev->shadow_sr_pool)
		return -ENOMEM;
	for (i = 0; i < PPC4XX_NUM_PD; i++) {
		struct ce_pd *pd = &dev->pdr[i];
		struct pd_uinfo *pd_uinfo = &dev->pdr_uinfo[i];

		pd->sa = dev->shadow_sa_pool_pa +
			sizeof(union shadow_sa_buf) * i;

		/* alloc 256 bytes which is enough for any kind of dynamic sa */
		pd_uinfo->sa_va = &dev->shadow_sa_pool[i].sa;

		/* alloc state record */
		pd_uinfo->sr_va = &dev->shadow_sr_pool[i];
		pd_uinfo->sr_pa = dev->shadow_sr_pool_pa +
		    sizeof(struct sa_state_record) * i;
	}

	return 0;
}

static void crypto4xx_destroy_pdr(struct crypto4xx_device *dev)
{
	if (dev->pdr)
		dma_free_coherent(dev->core_dev->device,
				  sizeof(struct ce_pd) * PPC4XX_NUM_PD,
				  dev->pdr, dev->pdr_pa);

	if (dev->shadow_sa_pool)
		dma_free_coherent(dev->core_dev->device,
			sizeof(union shadow_sa_buf) * PPC4XX_NUM_PD,
			dev->shadow_sa_pool, dev->shadow_sa_pool_pa);

	if (dev->shadow_sr_pool)
		dma_free_coherent(dev->core_dev->device,
			sizeof(struct sa_state_record) * PPC4XX_NUM_PD,
			dev->shadow_sr_pool, dev->shadow_sr_pool_pa);

	kfree(dev->pdr_uinfo);
}

static u32 crypto4xx_get_pd_from_pdr_nolock(struct crypto4xx_device *dev)
{
	u32 retval;
	u32 tmp;

	retval = dev->pdr_head;
	tmp = (dev->pdr_head + 1) % PPC4XX_NUM_PD;

	if (tmp == dev->pdr_tail)
		return ERING_WAS_FULL;

	dev->pdr_head = tmp;

	return retval;
}

static u32 crypto4xx_put_pd_to_pdr(struct crypto4xx_device *dev, u32 idx)
{
	struct pd_uinfo *pd_uinfo = &dev->pdr_uinfo[idx];
	u32 tail;
	unsigned long flags;

	spin_lock_irqsave(&dev->core_dev->lock, flags);
	pd_uinfo->state = PD_ENTRY_FREE;

	if (dev->pdr_tail != PPC4XX_LAST_PD)
		dev->pdr_tail++;
	else
		dev->pdr_tail = 0;
	tail = dev->pdr_tail;
	spin_unlock_irqrestore(&dev->core_dev->lock, flags);

	return tail;
}

/**
 * alloc memory for the gather ring
 * no need to alloc buf for the ring
 * gdr_tail, gdr_head and gdr_count are initialized by this function
 */
static u32 crypto4xx_build_gdr(struct crypto4xx_device *dev)
{
	dev->gdr = dma_alloc_coherent(dev->core_dev->device,
				      sizeof(struct ce_gd) * PPC4XX_NUM_GD,
				      &dev->gdr_pa, GFP_KERNEL);
	if (!dev->gdr)
		return -ENOMEM;

	return 0;
}

static inline void crypto4xx_destroy_gdr(struct crypto4xx_device *dev)
{
	if (dev->gdr)
		dma_free_coherent(dev->core_dev->device,
			  sizeof(struct ce_gd) * PPC4XX_NUM_GD,
			  dev->gdr, dev->gdr_pa);
}

/*
 * when this function is called.
 * preemption or interrupt must be disabled
 */
static u32 crypto4xx_get_n_gd(struct crypto4xx_device *dev, int n)
{
	u32 retval;
	u32 tmp;

	if (n >= PPC4XX_NUM_GD)
		return ERING_WAS_FULL;

	retval = dev->gdr_head;
	tmp = (dev->gdr_head + n) % PPC4XX_NUM_GD;
	if (dev->gdr_head > dev->gdr_tail) {
		if (tmp < dev->gdr_head && tmp >= dev->gdr_tail)
			return ERING_WAS_FULL;
	} else if (dev->gdr_head < dev->gdr_tail) {
		if (tmp < dev->gdr_head || tmp >= dev->gdr_tail)
			return ERING_WAS_FULL;
	}
	dev->gdr_head = tmp;

	return retval;
}

static u32 crypto4xx_put_gd_to_gdr(struct crypto4xx_device *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->core_dev->lock, flags);
	if (dev->gdr_tail == dev->gdr_head) {
		spin_unlock_irqrestore(&dev->core_dev->lock, flags);
		return 0;
	}

	if (dev->gdr_tail != PPC4XX_LAST_GD)
		dev->gdr_tail++;
	else
		dev->gdr_tail = 0;

	spin_unlock_irqrestore(&dev->core_dev->lock, flags);

	return 0;
}

static inline struct ce_gd *crypto4xx_get_gdp(struct crypto4xx_device *dev,
					      dma_addr_t *gd_dma, u32 idx)
{
	*gd_dma = dev->gdr_pa + sizeof(struct ce_gd) * idx;

	return &dev->gdr[idx];
}

/**
 * alloc memory for the scatter ring
 * need to alloc buf for the ring
 * sdr_tail, sdr_head and sdr_count are initialized by this function
 */
static u32 crypto4xx_build_sdr(struct crypto4xx_device *dev)
{
	int i;

	dev->scatter_buffer_va =
		dma_alloc_coherent(dev->core_dev->device,
			PPC4XX_SD_BUFFER_SIZE * PPC4XX_NUM_SD,
			&dev->scatter_buffer_pa, GFP_KERNEL);
	if (!dev->scatter_buffer_va)
		return -ENOMEM;

	/* alloc memory for scatter descriptor ring */
	dev->sdr = dma_alloc_coherent(dev->core_dev->device,
				      sizeof(struct ce_sd) * PPC4XX_NUM_SD,
				      &dev->sdr_pa, GFP_KERNEL);
	if (!dev->sdr)
		return -ENOMEM;

	for (i = 0; i < PPC4XX_NUM_SD; i++) {
		dev->sdr[i].ptr = dev->scatter_buffer_pa +
				  PPC4XX_SD_BUFFER_SIZE * i;
	}

	return 0;
}

static void crypto4xx_destroy_sdr(struct crypto4xx_device *dev)
{
	if (dev->sdr)
		dma_free_coherent(dev->core_dev->device,
				  sizeof(struct ce_sd) * PPC4XX_NUM_SD,
				  dev->sdr, dev->sdr_pa);

	if (dev->scatter_buffer_va)
		dma_free_coherent(dev->core_dev->device,
				  PPC4XX_SD_BUFFER_SIZE * PPC4XX_NUM_SD,
				  dev->scatter_buffer_va,
				  dev->scatter_buffer_pa);
}

/*
 * when this function is called.
 * preemption or interrupt must be disabled
 */
static u32 crypto4xx_get_n_sd(struct crypto4xx_device *dev, int n)
{
	u32 retval;
	u32 tmp;

	if (n >= PPC4XX_NUM_SD)
		return ERING_WAS_FULL;

	retval = dev->sdr_head;
	tmp = (dev->sdr_head + n) % PPC4XX_NUM_SD;
	if (dev->sdr_head > dev->gdr_tail) {
		if (tmp < dev->sdr_head && tmp >= dev->sdr_tail)
			return ERING_WAS_FULL;
	} else if (dev->sdr_head < dev->sdr_tail) {
		if (tmp < dev->sdr_head || tmp >= dev->sdr_tail)
			return ERING_WAS_FULL;
	} /* the head = tail, or empty case is already take cared */
	dev->sdr_head = tmp;

	return retval;
}

static u32 crypto4xx_put_sd_to_sdr(struct crypto4xx_device *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->core_dev->lock, flags);
	if (dev->sdr_tail == dev->sdr_head) {
		spin_unlock_irqrestore(&dev->core_dev->lock, flags);
		return 0;
	}
	if (dev->sdr_tail != PPC4XX_LAST_SD)
		dev->sdr_tail++;
	else
		dev->sdr_tail = 0;
	spin_unlock_irqrestore(&dev->core_dev->lock, flags);

	return 0;
}

static inline struct ce_sd *crypto4xx_get_sdp(struct crypto4xx_device *dev,
					      dma_addr_t *sd_dma, u32 idx)
{
	*sd_dma = dev->sdr_pa + sizeof(struct ce_sd) * idx;

	return &dev->sdr[idx];
}

static void crypto4xx_copy_pkt_to_dst(struct crypto4xx_device *dev,
				      struct ce_pd *pd,
				      struct pd_uinfo *pd_uinfo,
				      u32 nbytes,
				      struct scatterlist *dst)
{
	unsigned int first_sd = pd_uinfo->first_sd;
	unsigned int last_sd;
	unsigned int overflow = 0;
	unsigned int to_copy;
	unsigned int dst_start = 0;

	/*
	 * Because the scatter buffers are all neatly organized in one
	 * big continuous ringbuffer; scatterwalk_map_and_copy() can
	 * be instructed to copy a range of buffers in one go.
	 */

	last_sd = (first_sd + pd_uinfo->num_sd);
	if (last_sd > PPC4XX_LAST_SD) {
		last_sd = PPC4XX_LAST_SD;
		overflow = last_sd % PPC4XX_NUM_SD;
	}

	while (nbytes) {
		void *buf = dev->scatter_buffer_va +
			first_sd * PPC4XX_SD_BUFFER_SIZE;

		to_copy = min(nbytes, PPC4XX_SD_BUFFER_SIZE *
				      (1 + last_sd - first_sd));
		scatterwalk_map_and_copy(buf, dst, dst_start, to_copy, 1);
		nbytes -= to_copy;

		if (overflow) {
			first_sd = 0;
			last_sd = overflow;
			dst_start += to_copy;
			overflow = 0;
		}
	}
}

static void crypto4xx_copy_digest_to_dst(void *dst,
					struct pd_uinfo *pd_uinfo,
					struct crypto4xx_ctx *ctx)
{
	struct dynamic_sa_ctl *sa = (struct dynamic_sa_ctl *) ctx->sa_in;

	if (sa->sa_command_0.bf.hash_alg == SA_HASH_ALG_SHA1) {
		memcpy(dst, pd_uinfo->sr_va->save_digest,
		       SA_HASH_ALG_SHA1_DIGEST_SIZE);
	}
}

static void crypto4xx_ret_sg_desc(struct crypto4xx_device *dev,
				  struct pd_uinfo *pd_uinfo)
{
	int i;
	if (pd_uinfo->num_gd) {
		for (i = 0; i < pd_uinfo->num_gd; i++)
			crypto4xx_put_gd_to_gdr(dev);
		pd_uinfo->first_gd = 0xffffffff;
		pd_uinfo->num_gd = 0;
	}
	if (pd_uinfo->num_sd) {
		for (i = 0; i < pd_uinfo->num_sd; i++)
			crypto4xx_put_sd_to_sdr(dev);

		pd_uinfo->first_sd = 0xffffffff;
		pd_uinfo->num_sd = 0;
	}
}

static void crypto4xx_cipher_done(struct crypto4xx_device *dev,
				     struct pd_uinfo *pd_uinfo,
				     struct ce_pd *pd)
{
	struct skcipher_request *req;
	struct scatterlist *dst;
	dma_addr_t addr;

	req = skcipher_request_cast(pd_uinfo->async_req);

	if (pd_uinfo->sa_va->sa_command_0.bf.scatter) {
		crypto4xx_copy_pkt_to_dst(dev, pd, pd_uinfo,
					  req->cryptlen, req->dst);
	} else {
		dst = pd_uinfo->dest_va;
		addr = dma_map_page(dev->core_dev->device, sg_page(dst),
				    dst->offset, dst->length, DMA_FROM_DEVICE);
	}

	if (pd_uinfo->sa_va->sa_command_0.bf.save_iv == SA_SAVE_IV) {
		struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);

		crypto4xx_memcpy_from_le32((u32 *)req->iv,
			pd_uinfo->sr_va->save_iv,
			crypto_skcipher_ivsize(skcipher));
	}

	crypto4xx_ret_sg_desc(dev, pd_uinfo);

	if (pd_uinfo->state & PD_ENTRY_BUSY)
		skcipher_request_complete(req, -EINPROGRESS);
	skcipher_request_complete(req, 0);
}

static void crypto4xx_ahash_done(struct crypto4xx_device *dev,
				struct pd_uinfo *pd_uinfo)
{
	struct crypto4xx_ctx *ctx;
	struct ahash_request *ahash_req;

	ahash_req = ahash_request_cast(pd_uinfo->async_req);
	ctx  = crypto_tfm_ctx(ahash_req->base.tfm);

	crypto4xx_copy_digest_to_dst(ahash_req->result, pd_uinfo,
				     crypto_tfm_ctx(ahash_req->base.tfm));
	crypto4xx_ret_sg_desc(dev, pd_uinfo);

	if (pd_uinfo->state & PD_ENTRY_BUSY)
		ahash_request_complete(ahash_req, -EINPROGRESS);
	ahash_request_complete(ahash_req, 0);
}

static void crypto4xx_aead_done(struct crypto4xx_device *dev,
				struct pd_uinfo *pd_uinfo,
				struct ce_pd *pd)
{
	struct aead_request *aead_req = container_of(pd_uinfo->async_req,
		struct aead_request, base);
	struct scatterlist *dst = pd_uinfo->dest_va;
	size_t cp_len = crypto_aead_authsize(
		crypto_aead_reqtfm(aead_req));
	u32 icv[AES_BLOCK_SIZE];
	int err = 0;

	if (pd_uinfo->sa_va->sa_command_0.bf.scatter) {
		crypto4xx_copy_pkt_to_dst(dev, pd, pd_uinfo,
					  pd->pd_ctl_len.bf.pkt_len,
					  dst);
	} else {
		dma_unmap_page(dev->core_dev->device, pd->dest, dst->length,
				DMA_FROM_DEVICE);
	}

	if (pd_uinfo->sa_va->sa_command_0.bf.dir == DIR_OUTBOUND) {
		/* append icv at the end */
		crypto4xx_memcpy_from_le32(icv, pd_uinfo->sr_va->save_digest,
					   sizeof(icv));

		scatterwalk_map_and_copy(icv, dst, aead_req->cryptlen,
					 cp_len, 1);
	} else {
		/* check icv at the end */
		scatterwalk_map_and_copy(icv, aead_req->src,
			aead_req->assoclen + aead_req->cryptlen -
			cp_len, cp_len, 0);

		crypto4xx_memcpy_from_le32(icv, icv, sizeof(icv));

		if (crypto_memneq(icv, pd_uinfo->sr_va->save_digest, cp_len))
			err = -EBADMSG;
	}

	crypto4xx_ret_sg_desc(dev, pd_uinfo);

	if (pd->pd_ctl.bf.status & 0xff) {
		if (!__ratelimit(&dev->aead_ratelimit)) {
			if (pd->pd_ctl.bf.status & 2)
				pr_err("pad fail error\n");
			if (pd->pd_ctl.bf.status & 4)
				pr_err("seqnum fail\n");
			if (pd->pd_ctl.bf.status & 8)
				pr_err("error _notify\n");
			pr_err("aead return err status = 0x%02x\n",
				pd->pd_ctl.bf.status & 0xff);
			pr_err("pd pad_ctl = 0x%08x\n",
				pd->pd_ctl.bf.pd_pad_ctl);
		}
		err = -EINVAL;
	}

	if (pd_uinfo->state & PD_ENTRY_BUSY)
		aead_request_complete(aead_req, -EINPROGRESS);

	aead_request_complete(aead_req, err);
}

static void crypto4xx_pd_done(struct crypto4xx_device *dev, u32 idx)
{
	struct ce_pd *pd = &dev->pdr[idx];
	struct pd_uinfo *pd_uinfo = &dev->pdr_uinfo[idx];

	switch (crypto_tfm_alg_type(pd_uinfo->async_req->tfm)) {
	case CRYPTO_ALG_TYPE_SKCIPHER:
		crypto4xx_cipher_done(dev, pd_uinfo, pd);
		break;
	case CRYPTO_ALG_TYPE_AEAD:
		crypto4xx_aead_done(dev, pd_uinfo, pd);
		break;
	case CRYPTO_ALG_TYPE_AHASH:
		crypto4xx_ahash_done(dev, pd_uinfo);
		break;
	}
}

static void crypto4xx_stop_all(struct crypto4xx_core_device *core_dev)
{
	crypto4xx_destroy_pdr(core_dev->dev);
	crypto4xx_destroy_gdr(core_dev->dev);
	crypto4xx_destroy_sdr(core_dev->dev);
	iounmap(core_dev->dev->ce_base);
	kfree(core_dev->dev);
	kfree(core_dev);
}

static u32 get_next_gd(u32 current)
{
	if (current != PPC4XX_LAST_GD)
		return current + 1;
	else
		return 0;
}

static u32 get_next_sd(u32 current)
{
	if (current != PPC4XX_LAST_SD)
		return current + 1;
	else
		return 0;
}

int crypto4xx_build_pd(struct crypto_async_request *req,
		       struct crypto4xx_ctx *ctx,
		       struct scatterlist *src,
		       struct scatterlist *dst,
		       const unsigned int datalen,
		       const __le32 *iv, const u32 iv_len,
		       const struct dynamic_sa_ctl *req_sa,
		       const unsigned int sa_len,
		       const unsigned int assoclen,
		       struct scatterlist *_dst)
{
	struct crypto4xx_device *dev = ctx->dev;
	struct dynamic_sa_ctl *sa;
	struct ce_gd *gd;
	struct ce_pd *pd;
	u32 num_gd, num_sd;
	u32 fst_gd = 0xffffffff;
	u32 fst_sd = 0xffffffff;
	u32 pd_entry;
	unsigned long flags;
	struct pd_uinfo *pd_uinfo;
	unsigned int nbytes = datalen;
	size_t offset_to_sr_ptr;
	u32 gd_idx = 0;
	int tmp;
	bool is_busy, force_sd;

	/*
	 * There's a very subtile/disguised "bug" in the hardware that
	 * gets indirectly mentioned in 18.1.3.5 Encryption/Decryption
	 * of the hardware spec:
	 * *drum roll* the AES/(T)DES OFB and CFB modes are listed as
	 * operation modes for >>> "Block ciphers" <<<.
	 *
	 * To workaround this issue and stop the hardware from causing
	 * "overran dst buffer" on crypttexts that are not a multiple
	 * of 16 (AES_BLOCK_SIZE), we force the driver to use the
	 * scatter buffers.
	 */
	force_sd = (req_sa->sa_command_1.bf.crypto_mode9_8 == CRYPTO_MODE_CFB
		|| req_sa->sa_command_1.bf.crypto_mode9_8 == CRYPTO_MODE_OFB)
		&& (datalen % AES_BLOCK_SIZE);

	/* figure how many gd are needed */
	tmp = sg_nents_for_len(src, assoclen + datalen);
	if (tmp < 0) {
		dev_err(dev->core_dev->device, "Invalid number of src SG.\n");
		return tmp;
	}
	if (tmp == 1)
		tmp = 0;
	num_gd = tmp;

	if (assoclen) {
		nbytes += assoclen;
		dst = scatterwalk_ffwd(_dst, dst, assoclen);
	}

	/* figure how many sd are needed */
	if (sg_is_last(dst) && force_sd == false) {
		num_sd = 0;
	} else {
		if (datalen > PPC4XX_SD_BUFFER_SIZE) {
			num_sd = datalen / PPC4XX_SD_BUFFER_SIZE;
			if (datalen % PPC4XX_SD_BUFFER_SIZE)
				num_sd++;
		} else {
			num_sd = 1;
		}
	}

	/*
	 * The follow section of code needs to be protected
	 * The gather ring and scatter ring needs to be consecutive
	 * In case of run out of any kind of descriptor, the descriptor
	 * already got must be return the original place.
	 */
	spin_lock_irqsave(&dev->core_dev->lock, flags);
	/*
	 * Let the caller know to slow down, once more than 13/16ths = 81%
	 * of the available data contexts are being used simultaneously.
	 *
	 * With PPC4XX_NUM_PD = 256, this will leave a "backlog queue" for
	 * 31 more contexts. Before new requests have to be rejected.
	 */
	if (req->flags & CRYPTO_TFM_REQ_MAY_BACKLOG) {
		is_busy = ((dev->pdr_head - dev->pdr_tail) % PPC4XX_NUM_PD) >=
			((PPC4XX_NUM_PD * 13) / 16);
	} else {
		/*
		 * To fix contention issues between ipsec (no blacklog) and
		 * dm-crypto (backlog) reserve 32 entries for "no backlog"
		 * data contexts.
		 */
		is_busy = ((dev->pdr_head - dev->pdr_tail) % PPC4XX_NUM_PD) >=
			((PPC4XX_NUM_PD * 15) / 16);

		if (is_busy) {
			spin_unlock_irqrestore(&dev->core_dev->lock, flags);
			return -EBUSY;
		}
	}

	if (num_gd) {
		fst_gd = crypto4xx_get_n_gd(dev, num_gd);
		if (fst_gd == ERING_WAS_FULL) {
			spin_unlock_irqrestore(&dev->core_dev->lock, flags);
			return -EAGAIN;
		}
	}
	if (num_sd) {
		fst_sd = crypto4xx_get_n_sd(dev, num_sd);
		if (fst_sd == ERING_WAS_FULL) {
			if (num_gd)
				dev->gdr_head = fst_gd;
			spin_unlock_irqrestore(&dev->core_dev->lock, flags);
			return -EAGAIN;
		}
	}
	pd_entry = crypto4xx_get_pd_from_pdr_nolock(dev);
	if (pd_entry == ERING_WAS_FULL) {
		if (num_gd)
			dev->gdr_head = fst_gd;
		if (num_sd)
			dev->sdr_head = fst_sd;
		spin_unlock_irqrestore(&dev->core_dev->lock, flags);
		return -EAGAIN;
	}
	spin_unlock_irqrestore(&dev->core_dev->lock, flags);

	pd = &dev->pdr[pd_entry];
	pd->sa_len = sa_len;

	pd_uinfo = &dev->pdr_uinfo[pd_entry];
	pd_uinfo->num_gd = num_gd;
	pd_uinfo->num_sd = num_sd;
	pd_uinfo->dest_va = dst;
	pd_uinfo->async_req = req;

	if (iv_len)
		memcpy(pd_uinfo->sr_va->save_iv, iv, iv_len);

	sa = pd_uinfo->sa_va;
	memcpy(sa, req_sa, sa_len * 4);

	sa->sa_command_1.bf.hash_crypto_offset = (assoclen >> 2);
	offset_to_sr_ptr = get_dynamic_sa_offset_state_ptr_field(sa);
	*(u32 *)((unsigned long)sa + offset_to_sr_ptr) = pd_uinfo->sr_pa;

	if (num_gd) {
		dma_addr_t gd_dma;
		struct scatterlist *sg;

		/* get first gd we are going to use */
		gd_idx = fst_gd;
		pd_uinfo->first_gd = fst_gd;
		gd = crypto4xx_get_gdp(dev, &gd_dma, gd_idx);
		pd->src = gd_dma;
		/* enable gather */
		sa->sa_command_0.bf.gather = 1;
		/* walk the sg, and setup gather array */

		sg = src;
		while (nbytes) {
			size_t len;

			len = min(sg->length, nbytes);
			gd->ptr = dma_map_page(dev->core_dev->device,
				sg_page(sg), sg->offset, len, DMA_TO_DEVICE);
			gd->ctl_len.len = len;
			gd->ctl_len.done = 0;
			gd->ctl_len.ready = 1;
			if (len >= nbytes)
				break;

			nbytes -= sg->length;
			gd_idx = get_next_gd(gd_idx);
			gd = crypto4xx_get_gdp(dev, &gd_dma, gd_idx);
			sg = sg_next(sg);
		}
	} else {
		pd->src = (u32)dma_map_page(dev->core_dev->device, sg_page(src),
				src->offset, min(nbytes, src->length),
				DMA_TO_DEVICE);
		/*
		 * Disable gather in sa command
		 */
		sa->sa_command_0.bf.gather = 0;
		/*
		 * Indicate gather array is not used
		 */
		pd_uinfo->first_gd = 0xffffffff;
	}
	if (!num_sd) {
		/*
		 * we know application give us dst a whole piece of memory
		 * no need to use scatter ring.
		 */
		pd_uinfo->first_sd = 0xffffffff;
		sa->sa_command_0.bf.scatter = 0;
		pd->dest = (u32)dma_map_page(dev->core_dev->device,
					     sg_page(dst), dst->offset,
					     min(datalen, dst->length),
					     DMA_TO_DEVICE);
	} else {
		dma_addr_t sd_dma;
		struct ce_sd *sd = NULL;

		u32 sd_idx = fst_sd;
		nbytes = datalen;
		sa->sa_command_0.bf.scatter = 1;
		pd_uinfo->first_sd = fst_sd;
		sd = crypto4xx_get_sdp(dev, &sd_dma, sd_idx);
		pd->dest = sd_dma;
		/* setup scatter descriptor */
		sd->ctl.done = 0;
		sd->ctl.rdy = 1;
		/* sd->ptr should be setup by sd_init routine*/
		if (nbytes >= PPC4XX_SD_BUFFER_SIZE)
			nbytes -= PPC4XX_SD_BUFFER_SIZE;
		else
			nbytes = 0;
		while (nbytes) {
			sd_idx = get_next_sd(sd_idx);
			sd = crypto4xx_get_sdp(dev, &sd_dma, sd_idx);
			/* setup scatter descriptor */
			sd->ctl.done = 0;
			sd->ctl.rdy = 1;
			if (nbytes >= PPC4XX_SD_BUFFER_SIZE) {
				nbytes -= PPC4XX_SD_BUFFER_SIZE;
			} else {
				/*
				 * SD entry can hold PPC4XX_SD_BUFFER_SIZE,
				 * which is more than nbytes, so done.
				 */
				nbytes = 0;
			}
		}
	}

	pd->pd_ctl.w = PD_CTL_HOST_READY |
		((crypto_tfm_alg_type(req->tfm) == CRYPTO_ALG_TYPE_AHASH) |
		 (crypto_tfm_alg_type(req->tfm) == CRYPTO_ALG_TYPE_AEAD) ?
			PD_CTL_HASH_FINAL : 0);
	pd->pd_ctl_len.w = 0x00400000 | (assoclen + datalen);
	pd_uinfo->state = PD_ENTRY_INUSE | (is_busy ? PD_ENTRY_BUSY : 0);

	wmb();
	/* write any value to push engine to read a pd */
	writel(0, dev->ce_base + CRYPTO4XX_INT_DESCR_RD);
	writel(1, dev->ce_base + CRYPTO4XX_INT_DESCR_RD);
	return is_busy ? -EBUSY : -EINPROGRESS;
}

/**
 * Algorithm Registration Functions
 */
static void crypto4xx_ctx_init(struct crypto4xx_alg *amcc_alg,
			       struct crypto4xx_ctx *ctx)
{
	ctx->dev = amcc_alg->dev;
	ctx->sa_in = NULL;
	ctx->sa_out = NULL;
	ctx->sa_len = 0;
}

static int crypto4xx_sk_init(struct crypto_skcipher *sk)
{
	struct skcipher_alg *alg = crypto_skcipher_alg(sk);
	struct crypto4xx_alg *amcc_alg;
	struct crypto4xx_ctx *ctx =  crypto_skcipher_ctx(sk);

	if (alg->base.cra_flags & CRYPTO_ALG_NEED_FALLBACK) {
		ctx->sw_cipher.cipher =
			crypto_alloc_sync_skcipher(alg->base.cra_name, 0,
					      CRYPTO_ALG_NEED_FALLBACK);
		if (IS_ERR(ctx->sw_cipher.cipher))
			return PTR_ERR(ctx->sw_cipher.cipher);
	}

	amcc_alg = container_of(alg, struct crypto4xx_alg, alg.u.cipher);
	crypto4xx_ctx_init(amcc_alg, ctx);
	return 0;
}

static void crypto4xx_common_exit(struct crypto4xx_ctx *ctx)
{
	crypto4xx_free_sa(ctx);
}

static void crypto4xx_sk_exit(struct crypto_skcipher *sk)
{
	struct crypto4xx_ctx *ctx =  crypto_skcipher_ctx(sk);

	crypto4xx_common_exit(ctx);
	if (ctx->sw_cipher.cipher)
		crypto_free_sync_skcipher(ctx->sw_cipher.cipher);
}

static int crypto4xx_aead_init(struct crypto_aead *tfm)
{
	struct aead_alg *alg = crypto_aead_alg(tfm);
	struct crypto4xx_ctx *ctx = crypto_aead_ctx(tfm);
	struct crypto4xx_alg *amcc_alg;

	ctx->sw_cipher.aead = crypto_alloc_aead(alg->base.cra_name, 0,
						CRYPTO_ALG_NEED_FALLBACK |
						CRYPTO_ALG_ASYNC);
	if (IS_ERR(ctx->sw_cipher.aead))
		return PTR_ERR(ctx->sw_cipher.aead);

	amcc_alg = container_of(alg, struct crypto4xx_alg, alg.u.aead);
	crypto4xx_ctx_init(amcc_alg, ctx);
	crypto_aead_set_reqsize(tfm, max(sizeof(struct aead_request) + 32 +
				crypto_aead_reqsize(ctx->sw_cipher.aead),
				sizeof(struct crypto4xx_aead_reqctx)));
	return 0;
}

static void crypto4xx_aead_exit(struct crypto_aead *tfm)
{
	struct crypto4xx_ctx *ctx = crypto_aead_ctx(tfm);

	crypto4xx_common_exit(ctx);
	crypto_free_aead(ctx->sw_cipher.aead);
}

static int crypto4xx_register_alg(struct crypto4xx_device *sec_dev,
				  struct crypto4xx_alg_common *crypto_alg,
				  int array_size)
{
	struct crypto4xx_alg *alg;
	int i;
	int rc = 0;

	for (i = 0; i < array_size; i++) {
		alg = kzalloc(sizeof(struct crypto4xx_alg), GFP_KERNEL);
		if (!alg)
			return -ENOMEM;

		alg->alg = crypto_alg[i];
		alg->dev = sec_dev;

		switch (alg->alg.type) {
		case CRYPTO_ALG_TYPE_AEAD:
			rc = crypto_register_aead(&alg->alg.u.aead);
			break;

		case CRYPTO_ALG_TYPE_AHASH:
			rc = crypto_register_ahash(&alg->alg.u.hash);
			break;

		case CRYPTO_ALG_TYPE_RNG:
			rc = crypto_register_rng(&alg->alg.u.rng);
			break;

		default:
			rc = crypto_register_skcipher(&alg->alg.u.cipher);
			break;
		}

		if (rc)
			kfree(alg);
		else
			list_add_tail(&alg->entry, &sec_dev->alg_list);
	}

	return 0;
}

static void crypto4xx_unregister_alg(struct crypto4xx_device *sec_dev)
{
	struct crypto4xx_alg *alg, *tmp;

	list_for_each_entry_safe(alg, tmp, &sec_dev->alg_list, entry) {
		list_del(&alg->entry);
		switch (alg->alg.type) {
		case CRYPTO_ALG_TYPE_AHASH:
			crypto_unregister_ahash(&alg->alg.u.hash);
			break;

		case CRYPTO_ALG_TYPE_AEAD:
			crypto_unregister_aead(&alg->alg.u.aead);
			break;

		case CRYPTO_ALG_TYPE_RNG:
			crypto_unregister_rng(&alg->alg.u.rng);
			break;

		default:
			crypto_unregister_skcipher(&alg->alg.u.cipher);
		}
		kfree(alg);
	}
}

static void crypto4xx_bh_tasklet_cb(unsigned long data)
{
	struct device *dev = (struct device *)data;
	struct crypto4xx_core_device *core_dev = dev_get_drvdata(dev);
	struct pd_uinfo *pd_uinfo;
	struct ce_pd *pd;
	u32 tail = core_dev->dev->pdr_tail;
	u32 head = core_dev->dev->pdr_head;

	do {
		pd_uinfo = &core_dev->dev->pdr_uinfo[tail];
		pd = &core_dev->dev->pdr[tail];
		if ((pd_uinfo->state & PD_ENTRY_INUSE) &&
		     ((READ_ONCE(pd->pd_ctl.w) &
		       (PD_CTL_PE_DONE | PD_CTL_HOST_READY)) ==
		       PD_CTL_PE_DONE)) {
			crypto4xx_pd_done(core_dev->dev, tail);
			tail = crypto4xx_put_pd_to_pdr(core_dev->dev, tail);
		} else {
			/* if tail not done, break */
			break;
		}
	} while (head != tail);
}

/**
 * Top Half of isr.
 */
static inline irqreturn_t crypto4xx_interrupt_handler(int irq, void *data,
						      u32 clr_val)
{
	struct device *dev = (struct device *)data;
	struct crypto4xx_core_device *core_dev = dev_get_drvdata(dev);

	writel(clr_val, core_dev->dev->ce_base + CRYPTO4XX_INT_CLR);
	tasklet_schedule(&core_dev->tasklet);

	return IRQ_HANDLED;
}

static irqreturn_t crypto4xx_ce_interrupt_handler(int irq, void *data)
{
	return crypto4xx_interrupt_handler(irq, data, PPC4XX_INTERRUPT_CLR);
}

static irqreturn_t crypto4xx_ce_interrupt_handler_revb(int irq, void *data)
{
	return crypto4xx_interrupt_handler(irq, data, PPC4XX_INTERRUPT_CLR |
		PPC4XX_TMO_ERR_INT);
}

static int ppc4xx_prng_data_read(struct crypto4xx_device *dev,
				 u8 *data, unsigned int max)
{
	unsigned int i, curr = 0;
	u32 val[2];

	do {
		/* trigger PRN generation */
		writel(PPC4XX_PRNG_CTRL_AUTO_EN,
		       dev->ce_base + CRYPTO4XX_PRNG_CTRL);

		for (i = 0; i < 1024; i++) {
			/* usually 19 iterations are enough */
			if ((readl(dev->ce_base + CRYPTO4XX_PRNG_STAT) &
			     CRYPTO4XX_PRNG_STAT_BUSY))
				continue;

			val[0] = readl_be(dev->ce_base + CRYPTO4XX_PRNG_RES_0);
			val[1] = readl_be(dev->ce_base + CRYPTO4XX_PRNG_RES_1);
			break;
		}
		if (i == 1024)
			return -ETIMEDOUT;

		if ((max - curr) >= 8) {
			memcpy(data, &val, 8);
			data += 8;
			curr += 8;
		} else {
			/* copy only remaining bytes */
			memcpy(data, &val, max - curr);
			break;
		}
	} while (curr < max);

	return curr;
}

static int crypto4xx_prng_generate(struct crypto_rng *tfm,
				   const u8 *src, unsigned int slen,
				   u8 *dstn, unsigned int dlen)
{
	struct rng_alg *alg = crypto_rng_alg(tfm);
	struct crypto4xx_alg *amcc_alg;
	struct crypto4xx_device *dev;
	int ret;

	amcc_alg = container_of(alg, struct crypto4xx_alg, alg.u.rng);
	dev = amcc_alg->dev;

	mutex_lock(&dev->core_dev->rng_lock);
	ret = ppc4xx_prng_data_read(dev, dstn, dlen);
	mutex_unlock(&dev->core_dev->rng_lock);
	return ret;
}


static int crypto4xx_prng_seed(struct crypto_rng *tfm, const u8 *seed,
			unsigned int slen)
{
	return 0;
}

/**
 * Supported Crypto Algorithms
 */
static struct crypto4xx_alg_common crypto4xx_alg[] = {
	/* Crypto AES modes */
	{ .type = CRYPTO_ALG_TYPE_SKCIPHER, .u.cipher = {
		.base = {
			.cra_name = "cbc(aes)",
			.cra_driver_name = "cbc-aes-ppc4xx",
			.cra_priority = CRYPTO4XX_CRYPTO_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct crypto4xx_ctx),
			.cra_module = THIS_MODULE,
		},
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize	= AES_IV_SIZE,
		.setkey = crypto4xx_setkey_aes_cbc,
		.encrypt = crypto4xx_encrypt_iv_block,
		.decrypt = crypto4xx_decrypt_iv_block,
		.init = crypto4xx_sk_init,
		.exit = crypto4xx_sk_exit,
	} },
	{ .type = CRYPTO_ALG_TYPE_SKCIPHER, .u.cipher = {
		.base = {
			.cra_name = "cfb(aes)",
			.cra_driver_name = "cfb-aes-ppc4xx",
			.cra_priority = CRYPTO4XX_CRYPTO_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct crypto4xx_ctx),
			.cra_module = THIS_MODULE,
		},
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize	= AES_IV_SIZE,
		.setkey	= crypto4xx_setkey_aes_cfb,
		.encrypt = crypto4xx_encrypt_iv_stream,
		.decrypt = crypto4xx_decrypt_iv_stream,
		.init = crypto4xx_sk_init,
		.exit = crypto4xx_sk_exit,
	} },
	{ .type = CRYPTO_ALG_TYPE_SKCIPHER, .u.cipher = {
		.base = {
			.cra_name = "ctr(aes)",
			.cra_driver_name = "ctr-aes-ppc4xx",
			.cra_priority = CRYPTO4XX_CRYPTO_PRIORITY,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK |
				CRYPTO_ALG_ASYNC |
				CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct crypto4xx_ctx),
			.cra_module = THIS_MODULE,
		},
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize	= AES_IV_SIZE,
		.setkey	= crypto4xx_setkey_aes_ctr,
		.encrypt = crypto4xx_encrypt_ctr,
		.decrypt = crypto4xx_decrypt_ctr,
		.init = crypto4xx_sk_init,
		.exit = crypto4xx_sk_exit,
	} },
	{ .type = CRYPTO_ALG_TYPE_SKCIPHER, .u.cipher = {
		.base = {
			.cra_name = "rfc3686(ctr(aes))",
			.cra_driver_name = "rfc3686-ctr-aes-ppc4xx",
			.cra_priority = CRYPTO4XX_CRYPTO_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct crypto4xx_ctx),
			.cra_module = THIS_MODULE,
		},
		.min_keysize = AES_MIN_KEY_SIZE + CTR_RFC3686_NONCE_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE + CTR_RFC3686_NONCE_SIZE,
		.ivsize	= CTR_RFC3686_IV_SIZE,
		.setkey = crypto4xx_setkey_rfc3686,
		.encrypt = crypto4xx_rfc3686_encrypt,
		.decrypt = crypto4xx_rfc3686_decrypt,
		.init = crypto4xx_sk_init,
		.exit = crypto4xx_sk_exit,
	} },
	{ .type = CRYPTO_ALG_TYPE_SKCIPHER, .u.cipher = {
		.base = {
			.cra_name = "ecb(aes)",
			.cra_driver_name = "ecb-aes-ppc4xx",
			.cra_priority = CRYPTO4XX_CRYPTO_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct crypto4xx_ctx),
			.cra_module = THIS_MODULE,
		},
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.setkey	= crypto4xx_setkey_aes_ecb,
		.encrypt = crypto4xx_encrypt_noiv_block,
		.decrypt = crypto4xx_decrypt_noiv_block,
		.init = crypto4xx_sk_init,
		.exit = crypto4xx_sk_exit,
	} },
	{ .type = CRYPTO_ALG_TYPE_SKCIPHER, .u.cipher = {
		.base = {
			.cra_name = "ofb(aes)",
			.cra_driver_name = "ofb-aes-ppc4xx",
			.cra_priority = CRYPTO4XX_CRYPTO_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct crypto4xx_ctx),
			.cra_module = THIS_MODULE,
		},
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize	= AES_IV_SIZE,
		.setkey	= crypto4xx_setkey_aes_ofb,
		.encrypt = crypto4xx_encrypt_iv_stream,
		.decrypt = crypto4xx_decrypt_iv_stream,
		.init = crypto4xx_sk_init,
		.exit = crypto4xx_sk_exit,
	} },

	/* AEAD */
	{ .type = CRYPTO_ALG_TYPE_AEAD, .u.aead = {
		.setkey		= crypto4xx_setkey_aes_ccm,
		.setauthsize	= crypto4xx_setauthsize_aead,
		.encrypt	= crypto4xx_encrypt_aes_ccm,
		.decrypt	= crypto4xx_decrypt_aes_ccm,
		.init		= crypto4xx_aead_init,
		.exit		= crypto4xx_aead_exit,
		.ivsize		= AES_BLOCK_SIZE,
		.maxauthsize    = 16,
		.base = {
			.cra_name	= "ccm(aes)",
			.cra_driver_name = "ccm-aes-ppc4xx",
			.cra_priority	= CRYPTO4XX_CRYPTO_PRIORITY,
			.cra_flags	= CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_NEED_FALLBACK |
					  CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize	= 1,
			.cra_ctxsize	= sizeof(struct crypto4xx_ctx),
			.cra_module	= THIS_MODULE,
		},
	} },
	{ .type = CRYPTO_ALG_TYPE_AEAD, .u.aead = {
		.setkey		= crypto4xx_setkey_aes_gcm,
		.setauthsize	= crypto4xx_setauthsize_aead,
		.encrypt	= crypto4xx_encrypt_aes_gcm,
		.decrypt	= crypto4xx_decrypt_aes_gcm,
		.init		= crypto4xx_aead_init,
		.exit		= crypto4xx_aead_exit,
		.ivsize		= GCM_AES_IV_SIZE,
		.maxauthsize	= 16,
		.base = {
			.cra_name	= "gcm(aes)",
			.cra_driver_name = "gcm-aes-ppc4xx",
			.cra_priority	= CRYPTO4XX_CRYPTO_PRIORITY,
			.cra_flags	= CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_NEED_FALLBACK |
					  CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize	= 1,
			.cra_ctxsize	= sizeof(struct crypto4xx_ctx),
			.cra_module	= THIS_MODULE,
		},
	} },
	{ .type = CRYPTO_ALG_TYPE_RNG, .u.rng = {
		.base = {
			.cra_name		= "stdrng",
			.cra_driver_name        = "crypto4xx_rng",
			.cra_priority		= 300,
			.cra_ctxsize		= 0,
			.cra_module		= THIS_MODULE,
		},
		.generate               = crypto4xx_prng_generate,
		.seed                   = crypto4xx_prng_seed,
		.seedsize               = 0,
	} },
};

/**
 * Module Initialization Routine
 */
static int crypto4xx_probe(struct platform_device *ofdev)
{
	int rc;
	struct resource res;
	struct device *dev = &ofdev->dev;
	struct crypto4xx_core_device *core_dev;
	u32 pvr;
	bool is_revb = true;

	rc = of_address_to_resource(ofdev->dev.of_node, 0, &res);
	if (rc)
		return -ENODEV;

	if (of_find_compatible_node(NULL, NULL, "amcc,ppc460ex-crypto")) {
		mtdcri(SDR0, PPC460EX_SDR0_SRST,
		       mfdcri(SDR0, PPC460EX_SDR0_SRST) | PPC460EX_CE_RESET);
		mtdcri(SDR0, PPC460EX_SDR0_SRST,
		       mfdcri(SDR0, PPC460EX_SDR0_SRST) & ~PPC460EX_CE_RESET);
	} else if (of_find_compatible_node(NULL, NULL,
			"amcc,ppc405ex-crypto")) {
		mtdcri(SDR0, PPC405EX_SDR0_SRST,
		       mfdcri(SDR0, PPC405EX_SDR0_SRST) | PPC405EX_CE_RESET);
		mtdcri(SDR0, PPC405EX_SDR0_SRST,
		       mfdcri(SDR0, PPC405EX_SDR0_SRST) & ~PPC405EX_CE_RESET);
		is_revb = false;
	} else if (of_find_compatible_node(NULL, NULL,
			"amcc,ppc460sx-crypto")) {
		mtdcri(SDR0, PPC460SX_SDR0_SRST,
		       mfdcri(SDR0, PPC460SX_SDR0_SRST) | PPC460SX_CE_RESET);
		mtdcri(SDR0, PPC460SX_SDR0_SRST,
		       mfdcri(SDR0, PPC460SX_SDR0_SRST) & ~PPC460SX_CE_RESET);
	} else {
		printk(KERN_ERR "Crypto Function Not supported!\n");
		return -EINVAL;
	}

	core_dev = kzalloc(sizeof(struct crypto4xx_core_device), GFP_KERNEL);
	if (!core_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, core_dev);
	core_dev->ofdev = ofdev;
	core_dev->dev = kzalloc(sizeof(struct crypto4xx_device), GFP_KERNEL);
	rc = -ENOMEM;
	if (!core_dev->dev)
		goto err_alloc_dev;

	/*
	 * Older version of 460EX/GT have a hardware bug.
	 * Hence they do not support H/W based security intr coalescing
	 */
	pvr = mfspr(SPRN_PVR);
	if (is_revb && ((pvr >> 4) == 0x130218A)) {
		u32 min = PVR_MIN(pvr);

		if (min < 4) {
			dev_info(dev, "RevA detected - disable interrupt coalescing\n");
			is_revb = false;
		}
	}

	core_dev->dev->core_dev = core_dev;
	core_dev->dev->is_revb = is_revb;
	core_dev->device = dev;
	mutex_init(&core_dev->rng_lock);
	spin_lock_init(&core_dev->lock);
	INIT_LIST_HEAD(&core_dev->dev->alg_list);
	ratelimit_default_init(&core_dev->dev->aead_ratelimit);
	rc = crypto4xx_build_sdr(core_dev->dev);
	if (rc)
		goto err_build_sdr;
	rc = crypto4xx_build_pdr(core_dev->dev);
	if (rc)
		goto err_build_sdr;

	rc = crypto4xx_build_gdr(core_dev->dev);
	if (rc)
		goto err_build_sdr;

	/* Init tasklet for bottom half processing */
	tasklet_init(&core_dev->tasklet, crypto4xx_bh_tasklet_cb,
		     (unsigned long) dev);

	core_dev->dev->ce_base = of_iomap(ofdev->dev.of_node, 0);
	if (!core_dev->dev->ce_base) {
		dev_err(dev, "failed to of_iomap\n");
		rc = -ENOMEM;
		goto err_iomap;
	}

	/* Register for Crypto isr, Crypto Engine IRQ */
	core_dev->irq = irq_of_parse_and_map(ofdev->dev.of_node, 0);
	rc = request_irq(core_dev->irq, is_revb ?
			 crypto4xx_ce_interrupt_handler_revb :
			 crypto4xx_ce_interrupt_handler, 0,
			 KBUILD_MODNAME, dev);
	if (rc)
		goto err_request_irq;

	/* need to setup pdr, rdr, gdr and sdr before this */
	crypto4xx_hw_init(core_dev->dev);

	/* Register security algorithms with Linux CryptoAPI */
	rc = crypto4xx_register_alg(core_dev->dev, crypto4xx_alg,
			       ARRAY_SIZE(crypto4xx_alg));
	if (rc)
		goto err_start_dev;

	ppc4xx_trng_probe(core_dev);
	return 0;

err_start_dev:
	free_irq(core_dev->irq, dev);
err_request_irq:
	irq_dispose_mapping(core_dev->irq);
	iounmap(core_dev->dev->ce_base);
err_iomap:
	tasklet_kill(&core_dev->tasklet);
err_build_sdr:
	crypto4xx_destroy_sdr(core_dev->dev);
	crypto4xx_destroy_gdr(core_dev->dev);
	crypto4xx_destroy_pdr(core_dev->dev);
	kfree(core_dev->dev);
err_alloc_dev:
	kfree(core_dev);

	return rc;
}

static int crypto4xx_remove(struct platform_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	struct crypto4xx_core_device *core_dev = dev_get_drvdata(dev);

	ppc4xx_trng_remove(core_dev);

	free_irq(core_dev->irq, dev);
	irq_dispose_mapping(core_dev->irq);

	tasklet_kill(&core_dev->tasklet);
	/* Un-register with Linux CryptoAPI */
	crypto4xx_unregister_alg(core_dev->dev);
	mutex_destroy(&core_dev->rng_lock);
	/* Free all allocated memory */
	crypto4xx_stop_all(core_dev);

	return 0;
}

static const struct of_device_id crypto4xx_match[] = {
	{ .compatible      = "amcc,ppc4xx-crypto",},
	{ },
};
MODULE_DEVICE_TABLE(of, crypto4xx_match);

static struct platform_driver crypto4xx_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = crypto4xx_match,
	},
	.probe		= crypto4xx_probe,
	.remove		= crypto4xx_remove,
};

module_platform_driver(crypto4xx_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Hsiao <jhsiao@amcc.com>");
MODULE_DESCRIPTION("Driver for AMCC PPC4xx crypto accelerator");
