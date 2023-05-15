/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __STARFIVE_STR_H__
#define __STARFIVE_STR_H__

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>

#include <crypto/engine.h>

#define STARFIVE_ALG_CR_OFFSET			0x0
#define STARFIVE_ALG_FIFO_OFFSET		0x4
#define STARFIVE_IE_MASK_OFFSET			0x8
#define STARFIVE_IE_FLAG_OFFSET			0xc
#define STARFIVE_DMA_IN_LEN_OFFSET		0x10
#define STARFIVE_DMA_OUT_LEN_OFFSET		0x14

#define STARFIVE_MSG_BUFFER_SIZE		SZ_16K

union starfive_alg_cr {
	u32 v;
	struct {
		u32 start			:1;
		u32 aes_dma_en			:1;
		u32 rsvd_0			:1;
		u32 hash_dma_en			:1;
		u32 alg_done			:1;
		u32 rsvd_1			:3;
		u32 clear			:1;
		u32 rsvd_2			:23;
	};
};

struct starfive_cryp_ctx {
	struct crypto_engine_ctx		enginectx;
	struct starfive_cryp_dev		*cryp;
};

struct starfive_cryp_dev {
	struct list_head			list;
	struct device				*dev;

	struct clk				*hclk;
	struct clk				*ahb;
	struct reset_control			*rst;

	void __iomem				*base;
	phys_addr_t				phys_base;

	u32					dma_maxburst;
	struct dma_chan				*tx;
	struct dma_chan				*rx;
	struct dma_slave_config			cfg_in;
	struct dma_slave_config			cfg_out;

	struct crypto_engine			*engine;

	union starfive_alg_cr			alg_cr;
};

struct starfive_cryp_dev *starfive_cryp_find_dev(struct starfive_cryp_ctx *ctx);

#endif
