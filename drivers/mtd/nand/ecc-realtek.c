// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Realtek hardware ECC engine in RTL93xx SoCs
 */

#include <linux/bitfield.h>
#include <linux/dma-mapping.h>
#include <linux/mtd/nand.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/*
 * The Realtek ECC engine has two operation modes.
 *
 * - BCH6  : Generate 10 ECC bytes from 512 data bytes plus 6 free bytes
 * - BCH12 : Generate 20 ECC bytes from 512 data bytes plus 6 free bytes
 *
 * It can run for arbitrary NAND flash chips with different block and OOB sizes. Currently there
 * are only two known devices in the wild that have NAND flash and make use of this ECC engine
 * (Linksys LGS328C & LGS352C). To keep compatibility with vendor firmware, new modes can only
 * be added when new data layouts have been analyzed. For now allow BCH6 on flash with 2048 byte
 * blocks and 64 bytes oob.
 *
 * This driver aligns with kernel ECC naming conventions. Neverthless a short notice on the
 * Realtek naming conventions for the different structures in the OOB area.
 *
 * - BBI      : Bad block indicator. The first two bytes of OOB. Protected by ECC!
 * - tag      : 6 User/free bytes. First tag "contains" 2 bytes BBI. Protected by ECC!
 * - syndrome : ECC/parity bytes
 *
 * Altogether this gives currently the following block layout.
 *
 * +------+------+------+------+-----+------+------+------+------+-----+-----+-----+-----+
 * |  512 |  512 |  512 |  512 |   2 |    4 |    6 |    6 |    6 |  10 |  10 |  10 |  10 |
 * +------+------+------+------+-----+------+------+------+------+-----+-----+-----+-----+
 * | data | data | data | data | BBI | free | free | free | free | ECC | ECC | ECC | ECC |
 * +------+------+------+------+-----+------+------+------+------+-----+-----+-----+-----+
 */

#define RTL_ECC_ALLOWED_PAGE_SIZE 	2048
#define RTL_ECC_ALLOWED_OOB_SIZE	64
#define RTL_ECC_ALLOWED_STRENGTH	6

#define RTL_ECC_BLOCK_SIZE 		512
#define RTL_ECC_FREE_SIZE 		6
#define RTL_ECC_PARITY_SIZE_BCH6	10
#define RTL_ECC_PARITY_SIZE_BCH12	20

/*
 * The engine is fed with two DMA regions. One for data (always 512 bytes) and one for free bytes
 * and parity (either 16 bytes for BCH6 or 26 bytes for BCH12). Start and length of each must be
 * aligned to a multiple of 4.
 */

#define RTL_ECC_DMA_FREE_PARITY_SIZE	ALIGN(RTL_ECC_FREE_SIZE + RTL_ECC_PARITY_SIZE_BCH12, 4)
#define RTL_ECC_DMA_SIZE		(RTL_ECC_BLOCK_SIZE + RTL_ECC_DMA_FREE_PARITY_SIZE)

#define RTL_ECC_CFG			0x00
#define   RTL_ECC_BCH6			0
#define   RTL_ECC_BCH12			BIT(28)
#define   RTL_ECC_DMA_PRECISE		BIT(12)
#define   RTL_ECC_BURST_128		GENMASK(1, 0)
#define RTL_ECC_DMA_TRIGGER 		0x08
#define   RTL_ECC_OP_DECODE		0
#define   RTL_ECC_OP_ENCODE		BIT(0)
#define RTL_ECC_DMA_START		0x0c
#define RTL_ECC_DMA_TAG			0x10
#define RTL_ECC_STATUS			0x14
#define   RTL_ECC_CORR_COUNT		GENMASK(19, 12)
#define   RTL_ECC_RESULT		BIT(8)
#define   RTL_ECC_ALL_ONE		BIT(4)
#define   RTL_ECC_OP_STATUS		BIT(0)

struct rtl_ecc_engine {
	struct device *dev;
	struct nand_ecc_engine engine;
	struct mutex lock;
	char *buf;
	dma_addr_t buf_dma;
	struct regmap *regmap;
};

struct rtl_ecc_ctx {
	struct rtl_ecc_engine * rtlc;
	struct nand_ecc_req_tweak_ctx req_ctx;
	int steps;
	int bch_mode;
	int strength;
	int parity_size;
};

static const struct regmap_config rtl_ecc_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
};

static inline void *nand_to_ctx(struct nand_device *nand)
{
	return nand->ecc.ctx.priv;
}

static inline struct rtl_ecc_engine *nand_to_rtlc(struct nand_device *nand)
{
	struct nand_ecc_engine *eng = nand->ecc.engine;

	return container_of(eng, struct rtl_ecc_engine, engine);
}

static int rtl_ecc_ooblayout_ecc(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	struct nand_device *nand = mtd_to_nanddev(mtd);
	struct rtl_ecc_ctx *ctx = nand_to_ctx(nand);

	if (section < 0 || section >= ctx->steps)
		return -ERANGE;

	oobregion->offset = ctx->steps * RTL_ECC_FREE_SIZE + section * ctx->parity_size;
	oobregion->length = ctx->parity_size;

	return 0;
}

static int rtl_ecc_ooblayout_free(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *oobregion)
{
	struct nand_device *nand = mtd_to_nanddev(mtd);
	struct rtl_ecc_ctx *ctx = nand_to_ctx(nand);
	int bbm;

	if (section < 0 || section >= ctx->steps)
		return -ERANGE;

	/* reserve 2 BBM bytes in first block */
	bbm = section ? 0 : 2;
	oobregion->offset = section * RTL_ECC_FREE_SIZE + bbm;
	oobregion->length = RTL_ECC_FREE_SIZE - bbm;

	return 0;
}

static const struct mtd_ooblayout_ops rtl_ecc_ooblayout_ops = {
	.ecc = rtl_ecc_ooblayout_ecc,
	.free = rtl_ecc_ooblayout_free,
};

static void rtl_ecc_kick_engine(struct rtl_ecc_ctx *ctx, int operation)
{
	struct rtl_ecc_engine *rtlc = ctx->rtlc;

	regmap_write(rtlc->regmap, RTL_ECC_CFG,
		     ctx->bch_mode | RTL_ECC_BURST_128 | RTL_ECC_DMA_PRECISE);

	regmap_write(rtlc->regmap, RTL_ECC_DMA_START, rtlc->buf_dma);
	regmap_write(rtlc->regmap, RTL_ECC_DMA_TAG, rtlc->buf_dma + RTL_ECC_BLOCK_SIZE);
	regmap_write(rtlc->regmap, RTL_ECC_DMA_TRIGGER, operation);
}

static int rtl_ecc_wait_for_engine(struct rtl_ecc_ctx *ctx)
{
	struct rtl_ecc_engine *rtlc = ctx->rtlc;
	int ret, status, bitflips;
	bool all_one;

	/*
	 * The ECC engine needs 6-8 us to encode/decode a BCH6 syndrome for 512 bytes of data
	 * and 6 free bytes. In case the NAND area has been erased and all data and oob is
	 * set to 0xff, decoding takes 30us (reason unknown). Although the engine can trigger
	 * interrupts when finished, use active polling for now. 12 us maximum wait time has
	 * proven to be a good tradeoff between performance and overhead.
	 */

	ret = regmap_read_poll_timeout(rtlc->regmap, RTL_ECC_STATUS, status,
				       !(status & RTL_ECC_OP_STATUS), 12, 1000000);
	if (ret)
		return ret;

	ret = FIELD_GET(RTL_ECC_RESULT, status);
	all_one = FIELD_GET(RTL_ECC_ALL_ONE, status);
	bitflips = FIELD_GET(RTL_ECC_CORR_COUNT, status);

	/* For erased blocks (all bits one) error status can be ignored */
	if (all_one)
		ret = 0;

	return ret ? -EBADMSG : bitflips;
}

static int rtl_ecc_run_engine(struct rtl_ecc_ctx *ctx, char *data, char *free,
			      char *parity, int operation)
{
	struct rtl_ecc_engine *rtlc = ctx->rtlc;
	char *buf_parity = rtlc->buf + RTL_ECC_BLOCK_SIZE + RTL_ECC_FREE_SIZE;
	char *buf_free = rtlc->buf + RTL_ECC_BLOCK_SIZE;
	char *buf_data = rtlc->buf;
	int ret;

	mutex_lock(&rtlc->lock);

	memcpy(buf_data, data, RTL_ECC_BLOCK_SIZE);
	memcpy(buf_free, free, RTL_ECC_FREE_SIZE);
	memcpy(buf_parity, parity, ctx->parity_size);

	dma_sync_single_for_device(rtlc->dev, rtlc->buf_dma, RTL_ECC_DMA_SIZE, DMA_TO_DEVICE);
	rtl_ecc_kick_engine(ctx, operation);
	ret = rtl_ecc_wait_for_engine(ctx);
	dma_sync_single_for_cpu(rtlc->dev, rtlc->buf_dma, RTL_ECC_DMA_SIZE, DMA_FROM_DEVICE);

	if (ret >= 0) {
		memcpy(data, buf_data, RTL_ECC_BLOCK_SIZE);
		memcpy(free, buf_free, RTL_ECC_FREE_SIZE);
		memcpy(parity, buf_parity, ctx->parity_size);
	}

	mutex_unlock(&rtlc->lock);

	return ret;
}

static int rtl_ecc_prepare_io_req(struct nand_device *nand, struct nand_page_io_req *req)
{
	struct rtl_ecc_engine *rtlc = nand_to_rtlc(nand);
	struct rtl_ecc_ctx *ctx = nand_to_ctx(nand);
	char *data, *free, *parity;
	int ret = 0;

	if (req->mode == MTD_OPS_RAW)
		return 0;

	nand_ecc_tweak_req(&ctx->req_ctx, req);

	if (req->type == NAND_PAGE_READ)
		return 0;

	free = req->oobbuf.in;
	data = req->databuf.in;
	parity = req->oobbuf.in + ctx->steps * RTL_ECC_FREE_SIZE;

	for (int i = 0; i < ctx->steps; i++) {
		ret |= rtl_ecc_run_engine(ctx, data, free, parity, RTL_ECC_OP_ENCODE);

		free += RTL_ECC_FREE_SIZE;
		data += RTL_ECC_BLOCK_SIZE;
		parity += ctx->parity_size;
	}

	if (unlikely(ret))
		dev_dbg(rtlc->dev, "ECC calculation failed\n");

	return ret ? -EBADMSG : 0;
}

static int rtl_ecc_finish_io_req(struct nand_device *nand, struct nand_page_io_req *req)
{
	struct rtl_ecc_engine *rtlc = nand_to_rtlc(nand);
	struct rtl_ecc_ctx *ctx = nand_to_ctx(nand);
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	char *data, *free, *parity;
	bool failure = false;
	int bitflips = 0;

	if (req->mode == MTD_OPS_RAW)
		return 0;

	if (req->type == NAND_PAGE_WRITE) {
		nand_ecc_restore_req(&ctx->req_ctx, req);
		return 0;
	}

	free = req->oobbuf.in;
	data = req->databuf.in;
	parity = req->oobbuf.in + ctx->steps * RTL_ECC_FREE_SIZE;

	for (int i = 0 ; i < ctx->steps; i++) {
		int ret = rtl_ecc_run_engine(ctx, data, free, parity, RTL_ECC_OP_DECODE);

		if (unlikely(ret < 0))
			/* ECC totally fails for bitflips in erased blocks */
			ret = nand_check_erased_ecc_chunk(data, RTL_ECC_BLOCK_SIZE,
							  parity, ctx->parity_size,
							  free, RTL_ECC_FREE_SIZE,
							  ctx->strength);
		if (unlikely(ret < 0)) {
			failure = true;
			mtd->ecc_stats.failed++;
		} else {
			mtd->ecc_stats.corrected += ret;
			bitflips = max_t(unsigned int, bitflips, ret);
		}

		free += RTL_ECC_FREE_SIZE;
		data += RTL_ECC_BLOCK_SIZE;
		parity += ctx->parity_size;
	}

	nand_ecc_restore_req(&ctx->req_ctx, req);

	if (unlikely(failure))
		dev_dbg(rtlc->dev, "ECC correction failed\n");
	else if (unlikely(bitflips > 2))
		dev_dbg(rtlc->dev, "%d bitflips detected\n", bitflips);

	return failure ? -EBADMSG : bitflips;
}

static int rtl_ecc_check_support(struct nand_device *nand)
{
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	struct device *dev = nand->ecc.engine->dev;

	if (mtd->oobsize != RTL_ECC_ALLOWED_OOB_SIZE ||
	    mtd->writesize != RTL_ECC_ALLOWED_PAGE_SIZE) {
		dev_err(dev, "only flash geometry data=%d, oob=%d supported\n",
			RTL_ECC_ALLOWED_PAGE_SIZE, RTL_ECC_ALLOWED_OOB_SIZE);
		return -EINVAL;
	}

	if (nand->ecc.user_conf.algo != NAND_ECC_ALGO_BCH ||
	    nand->ecc.user_conf.strength != RTL_ECC_ALLOWED_STRENGTH ||
	    nand->ecc.user_conf.placement != NAND_ECC_PLACEMENT_OOB ||
	    nand->ecc.user_conf.step_size != RTL_ECC_BLOCK_SIZE) {
		dev_err(dev, "only algo=bch, strength=%d, placement=oob, step=%d supported\n",
			RTL_ECC_ALLOWED_STRENGTH, RTL_ECC_BLOCK_SIZE);
		return -EINVAL;
	}

	return 0;
}

static int rtl_ecc_init_ctx(struct nand_device *nand)
{
	struct nand_ecc_props *conf = &nand->ecc.ctx.conf;
	struct rtl_ecc_engine *rtlc = nand_to_rtlc(nand);
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	int strength = nand->ecc.user_conf.strength;
	struct device *dev = nand->ecc.engine->dev;
	struct rtl_ecc_ctx *ctx;
	int ret;

	ret = rtl_ecc_check_support(nand);
	if (ret)
		return ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	nand->ecc.ctx.priv = ctx;
	mtd_set_ooblayout(mtd, &rtl_ecc_ooblayout_ops);

	conf->algo = NAND_ECC_ALGO_BCH;
	conf->strength = strength;
	conf->step_size = RTL_ECC_BLOCK_SIZE;
	conf->engine_type = NAND_ECC_ENGINE_TYPE_ON_HOST;

	ctx->rtlc = rtlc;
	ctx->steps = mtd->writesize / RTL_ECC_BLOCK_SIZE;
	ctx->strength = strength;
	ctx->bch_mode = strength == 6 ? RTL_ECC_BCH6 : RTL_ECC_BCH12;
	ctx->parity_size = strength == 6 ? RTL_ECC_PARITY_SIZE_BCH6 : RTL_ECC_PARITY_SIZE_BCH12;

	ret = nand_ecc_init_req_tweaking(&ctx->req_ctx, nand);
	if (ret)
		return ret;

	dev_dbg(dev, "using bch%d with geometry data=%dx%d, free=%dx6, parity=%dx%d",
		conf->strength, ctx->steps, conf->step_size,
		ctx->steps, ctx->steps, ctx->parity_size);

	return 0;
}

static void rtl_ecc_cleanup_ctx(struct nand_device *nand)
{
	struct rtl_ecc_ctx *ctx = nand_to_ctx(nand);

	if (ctx)
		nand_ecc_cleanup_req_tweaking(&ctx->req_ctx);
}

static const struct nand_ecc_engine_ops rtl_ecc_engine_ops = {
	.init_ctx = rtl_ecc_init_ctx,
	.cleanup_ctx = rtl_ecc_cleanup_ctx,
	.prepare_io_req = rtl_ecc_prepare_io_req,
	.finish_io_req = rtl_ecc_finish_io_req,
};

static int rtl_ecc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtl_ecc_engine *rtlc;
	void __iomem *base;
	int ret;

	rtlc = devm_kzalloc(dev, sizeof(*rtlc), GFP_KERNEL);
	if (!rtlc)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	ret = devm_mutex_init(dev, &rtlc->lock);
	if (ret)
		return ret;

	rtlc->regmap = devm_regmap_init_mmio(dev, base, &rtl_ecc_regmap_config);
	if (IS_ERR(rtlc->regmap))
		return PTR_ERR(rtlc->regmap);

	/*
	 * Focus on simplicity and use a preallocated DMA buffer for data exchange with the
	 * engine. For now make it a noncoherent memory model as invalidating/flushing caches
	 * is faster than reading/writing uncached memory on the known architectures.
	 */

	rtlc->buf = dma_alloc_noncoherent(dev, RTL_ECC_DMA_SIZE, &rtlc->buf_dma,
					  DMA_BIDIRECTIONAL, GFP_KERNEL);
	if (!rtlc->buf)
		return -ENOMEM;

	rtlc->dev = dev;
	rtlc->engine.dev = dev;
	rtlc->engine.ops = &rtl_ecc_engine_ops;
	rtlc->engine.integration = NAND_ECC_ENGINE_INTEGRATION_EXTERNAL;

	nand_ecc_register_on_host_hw_engine(&rtlc->engine);

	platform_set_drvdata(pdev, rtlc);

	return 0;
}

static void rtl_ecc_remove(struct platform_device *pdev)
{
	struct rtl_ecc_engine *rtlc = platform_get_drvdata(pdev);

	nand_ecc_unregister_on_host_hw_engine(&rtlc->engine);
	dma_free_noncoherent(rtlc->dev, RTL_ECC_DMA_SIZE, rtlc->buf, rtlc->buf_dma,
			     DMA_BIDIRECTIONAL);
}

static const struct of_device_id rtl_ecc_of_ids[] = {
	{
		.compatible = "realtek,rtl9301-ecc",
	},
	{ /* sentinel */ },
};

static struct platform_driver rtl_ecc_driver = {
	.driver	= {
		.name = "rtl-nand-ecc-engine",
		.of_match_table = rtl_ecc_of_ids,
	},
	.probe = rtl_ecc_probe,
	.remove = rtl_ecc_remove,
};
module_platform_driver(rtl_ecc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Markus Stockhausen <markus.stockhausen@gmx.de>");
MODULE_DESCRIPTION("Realtek NAND hardware ECC controller");
