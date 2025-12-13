// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Macronix external hardware ECC engine for NAND devices, also
 * called DPE for Data Processing Engine.
 *
 * Copyright © 2019 Macronix
 * Author: Miquel Raynal <miquel.raynal@bootlin.com>
 */

#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand-ecc-mxic.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* DPE Configuration */
#define DP_CONFIG 0x00
#define   ECC_EN BIT(0)
#define   ECC_TYP(idx) (((idx) << 3) & GENMASK(6, 3))
/* DPE Interrupt Status */
#define INTRPT_STS 0x04
#define   TRANS_CMPLT BIT(0)
#define   SDMA_MAIN BIT(1)
#define   SDMA_SPARE BIT(2)
#define   ECC_ERR BIT(3)
#define   TO_SPARE BIT(4)
#define   TO_MAIN BIT(5)
/* DPE Interrupt Status Enable */
#define INTRPT_STS_EN 0x08
/* DPE Interrupt Signal Enable */
#define INTRPT_SIG_EN 0x0C
/* Host Controller Configuration */
#define HC_CONFIG 0x10
#define   DEV2MEM 0 /* TRANS_TYP_DMA in the spec */
#define   MEM2MEM BIT(4) /* TRANS_TYP_IO in the spec */
#define   MAPPING BIT(5) /* TRANS_TYP_MAPPING in the spec */
#define   ECC_PACKED 0 /* LAYOUT_TYP_INTEGRATED in the spec */
#define   ECC_INTERLEAVED BIT(2) /* LAYOUT_TYP_DISTRIBUTED in the spec */
#define   BURST_TYP_FIXED 0
#define   BURST_TYP_INCREASING BIT(0)
/* Host Controller Slave Address */
#define HC_SLV_ADDR 0x14
/* ECC Chunk Size */
#define CHUNK_SIZE 0x20
/* Main Data Size */
#define MAIN_SIZE 0x24
/* Spare Data Size */
#define SPARE_SIZE 0x28
#define   META_SZ(reg) ((reg) & GENMASK(7, 0))
#define   PARITY_SZ(reg) (((reg) & GENMASK(15, 8)) >> 8)
#define   RSV_SZ(reg) (((reg) & GENMASK(23, 16)) >> 16)
#define   SPARE_SZ(reg) ((reg) >> 24)
/* ECC Chunk Count */
#define CHUNK_CNT 0x30
/* SDMA Control */
#define SDMA_CTRL 0x40
#define   WRITE_NAND 0
#define   READ_NAND BIT(1)
#define   CONT_NAND BIT(29)
#define   CONT_SYSM BIT(30) /* Continue System Memory? */
#define   SDMA_STRT BIT(31)
/* SDMA Address of Main Data */
#define SDMA_MAIN_ADDR 0x44
/* SDMA Address of Spare Data */
#define SDMA_SPARE_ADDR 0x48
/* DPE Version Number */
#define DP_VER 0xD0
#define   DP_VER_OFFSET 16

/* Status bytes between each chunk of spare data */
#define STAT_BYTES 4
#define   NO_ERR 0x00
#define   MAX_CORR_ERR 0x28
#define   UNCORR_ERR 0xFE
#define   ERASED_CHUNK 0xFF

struct mxic_ecc_engine {
	struct device *dev;
	void __iomem *regs;
	int irq;
	struct completion complete;
	struct nand_ecc_engine external_engine;
	struct nand_ecc_engine pipelined_engine;
	struct mutex lock;
};

struct mxic_ecc_ctx {
	/* ECC machinery */
	unsigned int data_step_sz;
	unsigned int oob_step_sz;
	unsigned int parity_sz;
	unsigned int meta_sz;
	u8 *status;
	int steps;

	/* DMA boilerplate */
	struct nand_ecc_req_tweak_ctx req_ctx;
	u8 *oobwithstat;
	struct scatterlist sg[2];
	struct nand_page_io_req *req;
	unsigned int pageoffs;
};

static struct mxic_ecc_engine *ext_ecc_eng_to_mxic(struct nand_ecc_engine *eng)
{
	return container_of(eng, struct mxic_ecc_engine, external_engine);
}

static struct mxic_ecc_engine *pip_ecc_eng_to_mxic(struct nand_ecc_engine *eng)
{
	return container_of(eng, struct mxic_ecc_engine, pipelined_engine);
}

static struct mxic_ecc_engine *nand_to_mxic(struct nand_device *nand)
{
	struct nand_ecc_engine *eng = nand->ecc.engine;

	if (eng->integration == NAND_ECC_ENGINE_INTEGRATION_EXTERNAL)
		return ext_ecc_eng_to_mxic(eng);
	else
		return pip_ecc_eng_to_mxic(eng);
}

static int mxic_ecc_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *oobregion)
{
	struct nand_device *nand = mtd_to_nanddev(mtd);
	struct mxic_ecc_ctx *ctx = nand_to_ecc_ctx(nand);

	if (section < 0 || section >= ctx->steps)
		return -ERANGE;

	oobregion->offset = (section * ctx->oob_step_sz) + ctx->meta_sz;
	oobregion->length = ctx->parity_sz;

	return 0;
}

static int mxic_ecc_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *oobregion)
{
	struct nand_device *nand = mtd_to_nanddev(mtd);
	struct mxic_ecc_ctx *ctx = nand_to_ecc_ctx(nand);

	if (section < 0 || section >= ctx->steps)
		return -ERANGE;

	if (!section) {
		oobregion->offset = 2;
		oobregion->length = ctx->meta_sz - 2;
	} else {
		oobregion->offset = section * ctx->oob_step_sz;
		oobregion->length = ctx->meta_sz;
	}

	return 0;
}

static const struct mtd_ooblayout_ops mxic_ecc_ooblayout_ops = {
	.ecc = mxic_ecc_ooblayout_ecc,
	.free = mxic_ecc_ooblayout_free,
};

static void mxic_ecc_disable_engine(struct mxic_ecc_engine *mxic)
{
	u32 reg;

	reg = readl(mxic->regs + DP_CONFIG);
	reg &= ~ECC_EN;
	writel(reg, mxic->regs + DP_CONFIG);
}

static void mxic_ecc_enable_engine(struct mxic_ecc_engine *mxic)
{
	u32 reg;

	reg = readl(mxic->regs + DP_CONFIG);
	reg |= ECC_EN;
	writel(reg, mxic->regs + DP_CONFIG);
}

static void mxic_ecc_disable_int(struct mxic_ecc_engine *mxic)
{
	writel(0, mxic->regs + INTRPT_SIG_EN);
}

static void mxic_ecc_enable_int(struct mxic_ecc_engine *mxic)
{
	writel(TRANS_CMPLT, mxic->regs + INTRPT_SIG_EN);
}

static irqreturn_t mxic_ecc_isr(int irq, void *dev_id)
{
	struct mxic_ecc_engine *mxic = dev_id;
	u32 sts;

	sts = readl(mxic->regs + INTRPT_STS);
	if (!sts)
		return IRQ_NONE;

	if (sts & TRANS_CMPLT)
		complete(&mxic->complete);

	writel(sts, mxic->regs + INTRPT_STS);

	return IRQ_HANDLED;
}

static int mxic_ecc_init_ctx(struct nand_device *nand, struct device *dev)
{
	struct mxic_ecc_engine *mxic = nand_to_mxic(nand);
	struct nand_ecc_props *conf = &nand->ecc.ctx.conf;
	struct nand_ecc_props *reqs = &nand->ecc.requirements;
	struct nand_ecc_props *user = &nand->ecc.user_conf;
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	int step_size = 0, strength = 0, desired_correction = 0, steps, idx;
	static const int possible_strength[] = {4, 8, 40, 48};
	static const int spare_size[] = {32, 32, 96, 96};
	struct mxic_ecc_ctx *ctx;
	u32 spare_reg;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	nand->ecc.ctx.priv = ctx;

	/* Only large page NAND chips may use BCH */
	if (mtd->oobsize < 64) {
		pr_err("BCH cannot be used with small page NAND chips\n");
		return -EINVAL;
	}

	mtd_set_ooblayout(mtd, &mxic_ecc_ooblayout_ops);

	/* Enable all status bits */
	writel(TRANS_CMPLT | SDMA_MAIN | SDMA_SPARE | ECC_ERR |
	       TO_SPARE | TO_MAIN, mxic->regs + INTRPT_STS_EN);

	/* Configure the correction depending on the NAND device topology */
	if (user->step_size && user->strength) {
		step_size = user->step_size;
		strength = user->strength;
	} else if (reqs->step_size && reqs->strength) {
		step_size = reqs->step_size;
		strength = reqs->strength;
	}

	if (step_size && strength) {
		steps = mtd->writesize / step_size;
		desired_correction = steps * strength;
	}

	/* Step size is fixed to 1kiB, strength may vary (4 possible values) */
	conf->step_size = SZ_1K;
	steps = mtd->writesize / conf->step_size;

	ctx->status = devm_kzalloc(dev, steps * sizeof(u8), GFP_KERNEL);
	if (!ctx->status)
		return -ENOMEM;

	if (desired_correction) {
		strength = desired_correction / steps;

		for (idx = 0; idx < ARRAY_SIZE(possible_strength); idx++)
			if (possible_strength[idx] >= strength)
				break;

		idx = min_t(unsigned int, idx,
			    ARRAY_SIZE(possible_strength) - 1);
	} else {
		/* Missing data, maximize the correction */
		idx = ARRAY_SIZE(possible_strength) - 1;
	}

	/* Tune the selected strength until it fits in the OOB area */
	for (; idx >= 0; idx--) {
		if (spare_size[idx] * steps <= mtd->oobsize)
			break;
	}

	/* This engine cannot be used with this NAND device */
	if (idx < 0)
		return -EINVAL;

	/* Configure the engine for the desired strength */
	writel(ECC_TYP(idx), mxic->regs + DP_CONFIG);
	conf->strength = possible_strength[idx];
	spare_reg = readl(mxic->regs + SPARE_SIZE);

	ctx->steps = steps;
	ctx->data_step_sz = mtd->writesize / steps;
	ctx->oob_step_sz = mtd->oobsize / steps;
	ctx->parity_sz = PARITY_SZ(spare_reg);
	ctx->meta_sz = META_SZ(spare_reg);

	/* Ensure buffers will contain enough bytes to store the STAT_BYTES */
	ctx->req_ctx.oob_buffer_size = nanddev_per_page_oobsize(nand) +
					(ctx->steps * STAT_BYTES);
	ret = nand_ecc_init_req_tweaking(&ctx->req_ctx, nand);
	if (ret)
		return ret;

	ctx->oobwithstat = kmalloc(mtd->oobsize + (ctx->steps * STAT_BYTES),
				   GFP_KERNEL);
	if (!ctx->oobwithstat) {
		ret = -ENOMEM;
		goto cleanup_req_tweak;
	}

	sg_init_table(ctx->sg, 2);

	/* Configuration dump and sanity checks */
	dev_dbg(dev, "DPE version number: %d\n",
		readl(mxic->regs + DP_VER) >> DP_VER_OFFSET);
	dev_dbg(dev, "Chunk size: %d\n", readl(mxic->regs + CHUNK_SIZE));
	dev_dbg(dev, "Main size: %d\n", readl(mxic->regs + MAIN_SIZE));
	dev_dbg(dev, "Spare size: %d\n", SPARE_SZ(spare_reg));
	dev_dbg(dev, "Rsv size: %ld\n", RSV_SZ(spare_reg));
	dev_dbg(dev, "Parity size: %d\n", ctx->parity_sz);
	dev_dbg(dev, "Meta size: %d\n", ctx->meta_sz);

	if ((ctx->meta_sz + ctx->parity_sz + RSV_SZ(spare_reg)) !=
	    SPARE_SZ(spare_reg)) {
		dev_err(dev, "Wrong OOB configuration: %d + %d + %ld != %d\n",
			ctx->meta_sz, ctx->parity_sz, RSV_SZ(spare_reg),
			SPARE_SZ(spare_reg));
		ret = -EINVAL;
		goto free_oobwithstat;
	}

	if (ctx->oob_step_sz != SPARE_SZ(spare_reg)) {
		dev_err(dev, "Wrong OOB configuration: %d != %d\n",
			ctx->oob_step_sz, SPARE_SZ(spare_reg));
		ret = -EINVAL;
		goto free_oobwithstat;
	}

	return 0;

free_oobwithstat:
	kfree(ctx->oobwithstat);
cleanup_req_tweak:
	nand_ecc_cleanup_req_tweaking(&ctx->req_ctx);

	return ret;
}

static int mxic_ecc_init_ctx_external(struct nand_device *nand)
{
	struct mxic_ecc_engine *mxic = nand_to_mxic(nand);
	struct device *dev = nand->ecc.engine->dev;
	int ret;

	dev_info(dev, "Macronix ECC engine in external mode\n");

	ret = mxic_ecc_init_ctx(nand, dev);
	if (ret)
		return ret;

	/* Trigger each step manually */
	writel(1, mxic->regs + CHUNK_CNT);
	writel(BURST_TYP_INCREASING | ECC_PACKED | MEM2MEM,
	       mxic->regs + HC_CONFIG);

	return 0;
}

static int mxic_ecc_init_ctx_pipelined(struct nand_device *nand)
{
	struct mxic_ecc_engine *mxic = nand_to_mxic(nand);
	struct mxic_ecc_ctx *ctx;
	struct device *dev;
	int ret;

	dev = nand_ecc_get_engine_dev(nand->ecc.engine->dev);
	if (!dev)
		return -EINVAL;

	dev_info(dev, "Macronix ECC engine in pipelined/mapping mode\n");

	ret = mxic_ecc_init_ctx(nand, dev);
	if (ret)
		return ret;

	ctx = nand_to_ecc_ctx(nand);

	/* All steps should be handled in one go directly by the internal DMA */
	writel(ctx->steps, mxic->regs + CHUNK_CNT);

	/*
	 * Interleaved ECC scheme cannot be used otherwise factory bad block
	 * markers would be lost. A packed layout is mandatory.
	 */
	writel(BURST_TYP_INCREASING | ECC_PACKED | MAPPING,
	       mxic->regs + HC_CONFIG);

	return 0;
}

static void mxic_ecc_cleanup_ctx(struct nand_device *nand)
{
	struct mxic_ecc_ctx *ctx = nand_to_ecc_ctx(nand);

	if (ctx) {
		nand_ecc_cleanup_req_tweaking(&ctx->req_ctx);
		kfree(ctx->oobwithstat);
	}
}

static int mxic_ecc_data_xfer_wait_for_completion(struct mxic_ecc_engine *mxic)
{
	u32 val;
	int ret;

	if (mxic->irq) {
		reinit_completion(&mxic->complete);
		mxic_ecc_enable_int(mxic);
		ret = wait_for_completion_timeout(&mxic->complete,
						  msecs_to_jiffies(1000));
		ret = ret ? 0 : -ETIMEDOUT;
		mxic_ecc_disable_int(mxic);
	} else {
		ret = readl_poll_timeout(mxic->regs + INTRPT_STS, val,
					 val & TRANS_CMPLT, 10, USEC_PER_SEC);
		writel(val, mxic->regs + INTRPT_STS);
	}

	if (ret) {
		dev_err(mxic->dev, "Timeout on data xfer completion\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int mxic_ecc_process_data(struct mxic_ecc_engine *mxic,
				 unsigned int direction)
{
	unsigned int dir = (direction == NAND_PAGE_READ) ?
			   READ_NAND : WRITE_NAND;
	int ret;

	mxic_ecc_enable_engine(mxic);

	/* Trigger processing */
	writel(SDMA_STRT | dir, mxic->regs + SDMA_CTRL);

	/* Wait for completion */
	ret = mxic_ecc_data_xfer_wait_for_completion(mxic);

	mxic_ecc_disable_engine(mxic);

	return ret;
}

int mxic_ecc_process_data_pipelined(struct nand_ecc_engine *eng,
				    unsigned int direction, dma_addr_t dirmap)
{
	struct mxic_ecc_engine *mxic = pip_ecc_eng_to_mxic(eng);

	if (dirmap)
		writel(dirmap, mxic->regs + HC_SLV_ADDR);

	return mxic_ecc_process_data(mxic, direction);
}
EXPORT_SYMBOL_GPL(mxic_ecc_process_data_pipelined);

static void mxic_ecc_extract_status_bytes(struct mxic_ecc_ctx *ctx)
{
	u8 *buf = ctx->oobwithstat;
	int next_stat_pos;
	int step;

	/* Extract the ECC status */
	for (step = 0; step < ctx->steps; step++) {
		next_stat_pos = ctx->oob_step_sz +
				((STAT_BYTES + ctx->oob_step_sz) * step);

		ctx->status[step] = buf[next_stat_pos];
	}
}

static void mxic_ecc_reconstruct_oobbuf(struct mxic_ecc_ctx *ctx,
					u8 *dst, const u8 *src)
{
	int step;

	/* Reconstruct the OOB buffer linearly (without the ECC status bytes) */
	for (step = 0; step < ctx->steps; step++)
		memcpy(dst + (step * ctx->oob_step_sz),
		       src + (step * (ctx->oob_step_sz + STAT_BYTES)),
		       ctx->oob_step_sz);
}

static void mxic_ecc_add_room_in_oobbuf(struct mxic_ecc_ctx *ctx,
					u8 *dst, const u8 *src)
{
	int step;

	/* Add some space in the OOB buffer for the status bytes */
	for (step = 0; step < ctx->steps; step++)
		memcpy(dst + (step * (ctx->oob_step_sz + STAT_BYTES)),
		       src + (step * ctx->oob_step_sz),
		       ctx->oob_step_sz);
}

static int mxic_ecc_count_biterrs(struct mxic_ecc_engine *mxic,
				  struct nand_device *nand)
{
	struct mxic_ecc_ctx *ctx = nand_to_ecc_ctx(nand);
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	struct device *dev = mxic->dev;
	unsigned int max_bf = 0;
	bool failure = false;
	int step;

	for (step = 0; step < ctx->steps; step++) {
		u8 stat = ctx->status[step];

		if (stat == NO_ERR) {
			dev_dbg(dev, "ECC step %d: no error\n", step);
		} else if (stat == ERASED_CHUNK) {
			dev_dbg(dev, "ECC step %d: erased\n", step);
		} else if (stat == UNCORR_ERR || stat > MAX_CORR_ERR) {
			dev_dbg(dev, "ECC step %d: uncorrectable\n", step);
			mtd->ecc_stats.failed++;
			failure = true;
		} else {
			dev_dbg(dev, "ECC step %d: %d bits corrected\n",
				step, stat);
			max_bf = max_t(unsigned int, max_bf, stat);
			mtd->ecc_stats.corrected += stat;
		}
	}

	return failure ? -EBADMSG : max_bf;
}

/* External ECC engine helpers */
static int mxic_ecc_prepare_io_req_external(struct nand_device *nand,
					    struct nand_page_io_req *req)
{
	struct mxic_ecc_engine *mxic = nand_to_mxic(nand);
	struct mxic_ecc_ctx *ctx = nand_to_ecc_ctx(nand);
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	int offset, nents, step, ret;

	if (req->mode == MTD_OPS_RAW)
		return 0;

	nand_ecc_tweak_req(&ctx->req_ctx, req);
	ctx->req = req;

	if (req->type == NAND_PAGE_READ)
		return 0;

	mxic_ecc_add_room_in_oobbuf(ctx, ctx->oobwithstat,
				    ctx->req->oobbuf.out);

	sg_set_buf(&ctx->sg[0], req->databuf.out, req->datalen);
	sg_set_buf(&ctx->sg[1], ctx->oobwithstat,
		   req->ooblen + (ctx->steps * STAT_BYTES));

	nents = dma_map_sg(mxic->dev, ctx->sg, 2, DMA_BIDIRECTIONAL);
	if (!nents)
		return -EINVAL;

	mutex_lock(&mxic->lock);

	for (step = 0; step < ctx->steps; step++) {
		writel(sg_dma_address(&ctx->sg[0]) + (step * ctx->data_step_sz),
		       mxic->regs + SDMA_MAIN_ADDR);
		writel(sg_dma_address(&ctx->sg[1]) + (step * (ctx->oob_step_sz + STAT_BYTES)),
		       mxic->regs + SDMA_SPARE_ADDR);
		ret = mxic_ecc_process_data(mxic, ctx->req->type);
		if (ret)
			break;
	}

	mutex_unlock(&mxic->lock);

	dma_unmap_sg(mxic->dev, ctx->sg, 2, DMA_BIDIRECTIONAL);

	if (ret)
		return ret;

	/* Retrieve the calculated ECC bytes */
	for (step = 0; step < ctx->steps; step++) {
		offset = ctx->meta_sz + (step * ctx->oob_step_sz);
		mtd_ooblayout_get_eccbytes(mtd,
					   (u8 *)ctx->req->oobbuf.out + offset,
					   ctx->oobwithstat + (step * STAT_BYTES),
					   step * ctx->parity_sz,
					   ctx->parity_sz);
	}

	return 0;
}

static int mxic_ecc_finish_io_req_external(struct nand_device *nand,
					   struct nand_page_io_req *req)
{
	struct mxic_ecc_engine *mxic = nand_to_mxic(nand);
	struct mxic_ecc_ctx *ctx = nand_to_ecc_ctx(nand);
	int nents, step, ret = 0;

	if (req->mode == MTD_OPS_RAW)
		return 0;

	if (req->type == NAND_PAGE_WRITE) {
		nand_ecc_restore_req(&ctx->req_ctx, req);
		return 0;
	}

	/* Copy the OOB buffer and add room for the ECC engine status bytes */
	mxic_ecc_add_room_in_oobbuf(ctx, ctx->oobwithstat, ctx->req->oobbuf.in);

	sg_set_buf(&ctx->sg[0], req->databuf.in, req->datalen);
	sg_set_buf(&ctx->sg[1], ctx->oobwithstat,
		   req->ooblen + (ctx->steps * STAT_BYTES));
	nents = dma_map_sg(mxic->dev, ctx->sg, 2, DMA_BIDIRECTIONAL);
	if (!nents)
		return -EINVAL;

	mutex_lock(&mxic->lock);

	for (step = 0; step < ctx->steps; step++) {
		writel(sg_dma_address(&ctx->sg[0]) + (step * ctx->data_step_sz),
		       mxic->regs + SDMA_MAIN_ADDR);
		writel(sg_dma_address(&ctx->sg[1]) + (step * (ctx->oob_step_sz + STAT_BYTES)),
		       mxic->regs + SDMA_SPARE_ADDR);
		ret = mxic_ecc_process_data(mxic, ctx->req->type);
		if (ret)
			break;
	}

	mutex_unlock(&mxic->lock);

	dma_unmap_sg(mxic->dev, ctx->sg, 2, DMA_BIDIRECTIONAL);

	if (ret) {
		nand_ecc_restore_req(&ctx->req_ctx, req);
		return ret;
	}

	/* Extract the status bytes and reconstruct the buffer */
	mxic_ecc_extract_status_bytes(ctx);
	mxic_ecc_reconstruct_oobbuf(ctx, ctx->req->oobbuf.in, ctx->oobwithstat);

	nand_ecc_restore_req(&ctx->req_ctx, req);

	return mxic_ecc_count_biterrs(mxic, nand);
}

/* Pipelined ECC engine helpers */
static int mxic_ecc_prepare_io_req_pipelined(struct nand_device *nand,
					     struct nand_page_io_req *req)
{
	struct mxic_ecc_engine *mxic = nand_to_mxic(nand);
	struct mxic_ecc_ctx *ctx = nand_to_ecc_ctx(nand);
	int nents;

	if (req->mode == MTD_OPS_RAW)
		return 0;

	nand_ecc_tweak_req(&ctx->req_ctx, req);
	ctx->req = req;

	/* Copy the OOB buffer and add room for the ECC engine status bytes */
	mxic_ecc_add_room_in_oobbuf(ctx, ctx->oobwithstat, ctx->req->oobbuf.in);

	sg_set_buf(&ctx->sg[0], req->databuf.in, req->datalen);
	sg_set_buf(&ctx->sg[1], ctx->oobwithstat,
		   req->ooblen + (ctx->steps * STAT_BYTES));

	nents = dma_map_sg(mxic->dev, ctx->sg, 2, DMA_BIDIRECTIONAL);
	if (!nents)
		return -EINVAL;

	mutex_lock(&mxic->lock);

	writel(sg_dma_address(&ctx->sg[0]), mxic->regs + SDMA_MAIN_ADDR);
	writel(sg_dma_address(&ctx->sg[1]), mxic->regs + SDMA_SPARE_ADDR);

	return 0;
}

static int mxic_ecc_finish_io_req_pipelined(struct nand_device *nand,
					    struct nand_page_io_req *req)
{
	struct mxic_ecc_engine *mxic = nand_to_mxic(nand);
	struct mxic_ecc_ctx *ctx = nand_to_ecc_ctx(nand);
	int ret = 0;

	if (req->mode == MTD_OPS_RAW)
		return 0;

	mutex_unlock(&mxic->lock);

	dma_unmap_sg(mxic->dev, ctx->sg, 2, DMA_BIDIRECTIONAL);

	if (req->type == NAND_PAGE_READ) {
		mxic_ecc_extract_status_bytes(ctx);
		mxic_ecc_reconstruct_oobbuf(ctx, ctx->req->oobbuf.in,
					    ctx->oobwithstat);
		ret = mxic_ecc_count_biterrs(mxic, nand);
	}

	nand_ecc_restore_req(&ctx->req_ctx, req);

	return ret;
}

static const struct nand_ecc_engine_ops mxic_ecc_engine_external_ops = {
	.init_ctx = mxic_ecc_init_ctx_external,
	.cleanup_ctx = mxic_ecc_cleanup_ctx,
	.prepare_io_req = mxic_ecc_prepare_io_req_external,
	.finish_io_req = mxic_ecc_finish_io_req_external,
};

static const struct nand_ecc_engine_ops mxic_ecc_engine_pipelined_ops = {
	.init_ctx = mxic_ecc_init_ctx_pipelined,
	.cleanup_ctx = mxic_ecc_cleanup_ctx,
	.prepare_io_req = mxic_ecc_prepare_io_req_pipelined,
	.finish_io_req = mxic_ecc_finish_io_req_pipelined,
};

const struct nand_ecc_engine_ops *mxic_ecc_get_pipelined_ops(void)
{
	return &mxic_ecc_engine_pipelined_ops;
}
EXPORT_SYMBOL_GPL(mxic_ecc_get_pipelined_ops);

static struct platform_device *
mxic_ecc_get_pdev(struct platform_device *spi_pdev)
{
	struct platform_device *eng_pdev;
	struct device_node *np;

	/* Retrieve the nand-ecc-engine phandle */
	np = of_parse_phandle(spi_pdev->dev.of_node, "nand-ecc-engine", 0);
	if (!np)
		return NULL;

	/* Jump to the engine's device node */
	eng_pdev = of_find_device_by_node(np);
	of_node_put(np);

	return eng_pdev;
}

void mxic_ecc_put_pipelined_engine(struct nand_ecc_engine *eng)
{
	struct mxic_ecc_engine *mxic = pip_ecc_eng_to_mxic(eng);

	platform_device_put(to_platform_device(mxic->dev));
}
EXPORT_SYMBOL_GPL(mxic_ecc_put_pipelined_engine);

struct nand_ecc_engine *
mxic_ecc_get_pipelined_engine(struct platform_device *spi_pdev)
{
	struct platform_device *eng_pdev;
	struct mxic_ecc_engine *mxic;

	eng_pdev = mxic_ecc_get_pdev(spi_pdev);
	if (!eng_pdev)
		return ERR_PTR(-ENODEV);

	mxic = platform_get_drvdata(eng_pdev);
	if (!mxic) {
		platform_device_put(eng_pdev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	return &mxic->pipelined_engine;
}
EXPORT_SYMBOL_GPL(mxic_ecc_get_pipelined_engine);

/*
 * Only the external ECC engine is exported as the pipelined is SoC specific, so
 * it is registered directly by the drivers that wrap it.
 */
static int mxic_ecc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mxic_ecc_engine *mxic;
	int ret;

	mxic = devm_kzalloc(&pdev->dev, sizeof(*mxic), GFP_KERNEL);
	if (!mxic)
		return -ENOMEM;

	mxic->dev = &pdev->dev;

	/*
	 * Both memory regions for the ECC engine itself and the AXI slave
	 * address are mandatory.
	 */
	mxic->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mxic->regs)) {
		dev_err(&pdev->dev, "Missing memory region\n");
		return PTR_ERR(mxic->regs);
	}

	mxic_ecc_disable_engine(mxic);
	mxic_ecc_disable_int(mxic);

	/* IRQ is optional yet much more efficient */
	mxic->irq = platform_get_irq_byname_optional(pdev, "ecc-engine");
	if (mxic->irq > 0) {
		ret = devm_request_irq(&pdev->dev, mxic->irq, mxic_ecc_isr, 0,
				       "mxic-ecc", mxic);
		if (ret)
			return ret;
	} else {
		dev_info(dev, "Invalid or missing IRQ, fallback to polling\n");
		mxic->irq = 0;
	}

	mutex_init(&mxic->lock);

	/*
	 * In external mode, the device is the ECC engine. In pipelined mode,
	 * the device is the host controller. The device is used to match the
	 * right ECC engine based on the DT properties.
	 */
	mxic->external_engine.dev = &pdev->dev;
	mxic->external_engine.integration = NAND_ECC_ENGINE_INTEGRATION_EXTERNAL;
	mxic->external_engine.ops = &mxic_ecc_engine_external_ops;

	nand_ecc_register_on_host_hw_engine(&mxic->external_engine);

	platform_set_drvdata(pdev, mxic);

	return 0;
}

static void mxic_ecc_remove(struct platform_device *pdev)
{
	struct mxic_ecc_engine *mxic = platform_get_drvdata(pdev);

	nand_ecc_unregister_on_host_hw_engine(&mxic->external_engine);
}

static const struct of_device_id mxic_ecc_of_ids[] = {
	{
		.compatible = "mxicy,nand-ecc-engine-rev3",
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mxic_ecc_of_ids);

static struct platform_driver mxic_ecc_driver = {
	.driver	= {
		.name = "mxic-nand-ecc-engine",
		.of_match_table = mxic_ecc_of_ids,
	},
	.probe = mxic_ecc_probe,
	.remove = mxic_ecc_remove,
};
module_platform_driver(mxic_ecc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Miquel Raynal <miquel.raynal@bootlin.com>");
MODULE_DESCRIPTION("Macronix NAND hardware ECC controller");
