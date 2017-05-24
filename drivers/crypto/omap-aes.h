/*
 * Cryptographic API.
 *
 * Support for OMAP AES HW ACCELERATOR defines
 *
 * Copyright (c) 2015 Texas Instruments Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 */
#ifndef __OMAP_AES_H__
#define __OMAP_AES_H__

#define DST_MAXBURST			4
#define DMA_MIN				(DST_MAXBURST * sizeof(u32))

#define _calc_walked(inout) (dd->inout##_walk.offset - dd->inout##_sg->offset)

/*
 * OMAP TRM gives bitfields as start:end, where start is the higher bit
 * number. For example 7:0
 */
#define FLD_MASK(start, end)	(((1 << ((start) - (end) + 1)) - 1) << (end))
#define FLD_VAL(val, start, end) (((val) << (end)) & FLD_MASK(start, end))

#define AES_REG_KEY(dd, x)		((dd)->pdata->key_ofs - \
						(((x) ^ 0x01) * 0x04))
#define AES_REG_IV(dd, x)		((dd)->pdata->iv_ofs + ((x) * 0x04))

#define AES_REG_CTRL(dd)		((dd)->pdata->ctrl_ofs)
#define AES_REG_CTRL_CTR_WIDTH_MASK	GENMASK(8, 7)
#define AES_REG_CTRL_CTR_WIDTH_32	0
#define AES_REG_CTRL_CTR_WIDTH_64	BIT(7)
#define AES_REG_CTRL_CTR_WIDTH_96	BIT(8)
#define AES_REG_CTRL_CTR_WIDTH_128	GENMASK(8, 7)
#define AES_REG_CTRL_CTR		BIT(6)
#define AES_REG_CTRL_CBC		BIT(5)
#define AES_REG_CTRL_KEY_SIZE		GENMASK(4, 3)
#define AES_REG_CTRL_DIRECTION		BIT(2)
#define AES_REG_CTRL_INPUT_READY	BIT(1)
#define AES_REG_CTRL_OUTPUT_READY	BIT(0)
#define AES_REG_CTRL_MASK		GENMASK(24, 2)

#define AES_REG_DATA_N(dd, x)		((dd)->pdata->data_ofs + ((x) * 0x04))

#define AES_REG_REV(dd)			((dd)->pdata->rev_ofs)

#define AES_REG_MASK(dd)		((dd)->pdata->mask_ofs)
#define AES_REG_MASK_SIDLE		BIT(6)
#define AES_REG_MASK_START		BIT(5)
#define AES_REG_MASK_DMA_OUT_EN		BIT(3)
#define AES_REG_MASK_DMA_IN_EN		BIT(2)
#define AES_REG_MASK_SOFTRESET		BIT(1)
#define AES_REG_AUTOIDLE		BIT(0)

#define AES_REG_LENGTH_N(x)		(0x54 + ((x) * 0x04))

#define AES_REG_IRQ_STATUS(dd)         ((dd)->pdata->irq_status_ofs)
#define AES_REG_IRQ_ENABLE(dd)         ((dd)->pdata->irq_enable_ofs)
#define AES_REG_IRQ_DATA_IN            BIT(1)
#define AES_REG_IRQ_DATA_OUT           BIT(2)
#define DEFAULT_TIMEOUT		(5 * HZ)

#define DEFAULT_AUTOSUSPEND_DELAY	1000

#define FLAGS_MODE_MASK		0x000f
#define FLAGS_ENCRYPT		BIT(0)
#define FLAGS_CBC		BIT(1)
#define FLAGS_GIV		BIT(2)
#define FLAGS_CTR		BIT(3)

#define FLAGS_INIT		BIT(4)
#define FLAGS_FAST		BIT(5)
#define FLAGS_BUSY		BIT(6)

#define FLAGS_IN_DATA_ST_SHIFT	8
#define FLAGS_OUT_DATA_ST_SHIFT	10

#define AES_BLOCK_WORDS		(AES_BLOCK_SIZE >> 2)

struct omap_aes_ctx {
	int		keylen;
	u32		key[AES_KEYSIZE_256 / sizeof(u32)];
	struct crypto_skcipher	*fallback;
};

struct omap_aes_reqctx {
	struct omap_aes_dev *dd;
	unsigned long mode;
};

#define OMAP_AES_QUEUE_LENGTH	1
#define OMAP_AES_CACHE_SIZE	0

struct omap_aes_algs_info {
	struct crypto_alg	*algs_list;
	unsigned int		size;
	unsigned int		registered;
};

struct omap_aes_pdata {
	struct omap_aes_algs_info	*algs_info;
	unsigned int	algs_info_size;

	void		(*trigger)(struct omap_aes_dev *dd, int length);

	u32		key_ofs;
	u32		iv_ofs;
	u32		ctrl_ofs;
	u32		data_ofs;
	u32		rev_ofs;
	u32		mask_ofs;
	u32             irq_enable_ofs;
	u32             irq_status_ofs;

	u32		dma_enable_in;
	u32		dma_enable_out;
	u32		dma_start;

	u32		major_mask;
	u32		major_shift;
	u32		minor_mask;
	u32		minor_shift;
};

struct omap_aes_dev {
	struct list_head	list;
	unsigned long		phys_base;
	void __iomem		*io_base;
	struct omap_aes_ctx	*ctx;
	struct device		*dev;
	unsigned long		flags;
	int			err;

	struct tasklet_struct	done_task;

	struct ablkcipher_request	*req;
	struct crypto_engine		*engine;

	/*
	 * total is used by PIO mode for book keeping so introduce
	 * variable total_save as need it to calc page_order
	 */
	size_t				total;
	size_t				total_save;

	struct scatterlist		*in_sg;
	struct scatterlist		*out_sg;

	/* Buffers for copying for unaligned cases */
	struct scatterlist		in_sgl;
	struct scatterlist		out_sgl;
	struct scatterlist		*orig_out;

	struct scatter_walk		in_walk;
	struct scatter_walk		out_walk;
	struct dma_chan		*dma_lch_in;
	struct dma_chan		*dma_lch_out;
	int			in_sg_len;
	int			out_sg_len;
	int			pio_only;
	const struct omap_aes_pdata	*pdata;
};

#endif
