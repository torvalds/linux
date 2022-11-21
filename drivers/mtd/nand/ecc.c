// SPDX-License-Identifier: GPL-2.0+
/*
 * Generic Error-Correcting Code (ECC) engine
 *
 * Copyright (C) 2019 Macronix
 * Author:
 *     Miquèl RAYNAL <miquel.raynal@bootlin.com>
 *
 *
 * This file describes the abstraction of any NAND ECC engine. It has been
 * designed to fit most cases, including parallel NANDs and SPI-NANDs.
 *
 * There are three main situations where instantiating this ECC engine makes
 * sense:
 *   - external: The ECC engine is outside the NAND pipeline, typically this
 *               is a software ECC engine, or an hardware engine that is
 *               outside the NAND controller pipeline.
 *   - pipelined: The ECC engine is inside the NAND pipeline, ie. on the
 *                controller's side. This is the case of most of the raw NAND
 *                controllers. In the pipeline case, the ECC bytes are
 *                generated/data corrected on the fly when a page is
 *                written/read.
 *   - ondie: The ECC engine is inside the NAND pipeline, on the chip's side.
 *            Some NAND chips can correct themselves the data.
 *
 * Besides the initial setup and final cleanups, the interfaces are rather
 * simple:
 *   - prepare: Prepare an I/O request. Enable/disable the ECC engine based on
 *              the I/O request type. In case of software correction or external
 *              engine, this step may involve to derive the ECC bytes and place
 *              them in the OOB area before a write.
 *   - finish: Finish an I/O request. Correct the data in case of a read
 *             request and report the number of corrected bits/uncorrectable
 *             errors. Most likely empty for write operations, unless you have
 *             hardware specific stuff to do, like shutting down the engine to
 *             save power.
 *
 * The I/O request should be enclosed in a prepare()/finish() pair of calls
 * and will behave differently depending on the requested I/O type:
 *   - raw: Correction disabled
 *   - ecc: Correction enabled
 *
 * The request direction is impacting the logic as well:
 *   - read: Load data from the NAND chip
 *   - write: Store data in the NAND chip
 *
 * Mixing all this combinations together gives the following behavior.
 * Those are just examples, drivers are free to add custom steps in their
 * prepare/finish hook.
 *
 * [external ECC engine]
 *   - external + prepare + raw + read: do nothing
 *   - external + finish  + raw + read: do nothing
 *   - external + prepare + raw + write: do nothing
 *   - external + finish  + raw + write: do nothing
 *   - external + prepare + ecc + read: do nothing
 *   - external + finish  + ecc + read: calculate expected ECC bytes, extract
 *                                      ECC bytes from OOB buffer, correct
 *                                      and report any bitflip/error
 *   - external + prepare + ecc + write: calculate ECC bytes and store them at
 *                                       the right place in the OOB buffer based
 *                                       on the OOB layout
 *   - external + finish  + ecc + write: do nothing
 *
 * [pipelined ECC engine]
 *   - pipelined + prepare + raw + read: disable the controller's ECC engine if
 *                                       activated
 *   - pipelined + finish  + raw + read: do nothing
 *   - pipelined + prepare + raw + write: disable the controller's ECC engine if
 *                                        activated
 *   - pipelined + finish  + raw + write: do nothing
 *   - pipelined + prepare + ecc + read: enable the controller's ECC engine if
 *                                       deactivated
 *   - pipelined + finish  + ecc + read: check the status, report any
 *                                       error/bitflip
 *   - pipelined + prepare + ecc + write: enable the controller's ECC engine if
 *                                        deactivated
 *   - pipelined + finish  + ecc + write: do nothing
 *
 * [ondie ECC engine]
 *   - ondie + prepare + raw + read: send commands to disable the on-chip ECC
 *                                   engine if activated
 *   - ondie + finish  + raw + read: do nothing
 *   - ondie + prepare + raw + write: send commands to disable the on-chip ECC
 *                                    engine if activated
 *   - ondie + finish  + raw + write: do nothing
 *   - ondie + prepare + ecc + read: send commands to enable the on-chip ECC
 *                                   engine if deactivated
 *   - ondie + finish  + ecc + read: send commands to check the status, report
 *                                   any error/bitflip
 *   - ondie + prepare + ecc + write: send commands to enable the on-chip ECC
 *                                    engine if deactivated
 *   - ondie + finish  + ecc + write: do nothing
 */

#include <linux/module.h>
#include <linux/mtd/nand.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

static LIST_HEAD(on_host_hw_engines);
static DEFINE_MUTEX(on_host_hw_engines_mutex);

/**
 * nand_ecc_init_ctx - Init the ECC engine context
 * @nand: the NAND device
 *
 * On success, the caller is responsible of calling @nand_ecc_cleanup_ctx().
 */
int nand_ecc_init_ctx(struct nand_device *nand)
{
	if (!nand->ecc.engine || !nand->ecc.engine->ops->init_ctx)
		return 0;

	return nand->ecc.engine->ops->init_ctx(nand);
}
EXPORT_SYMBOL(nand_ecc_init_ctx);

/**
 * nand_ecc_cleanup_ctx - Cleanup the ECC engine context
 * @nand: the NAND device
 */
void nand_ecc_cleanup_ctx(struct nand_device *nand)
{
	if (nand->ecc.engine && nand->ecc.engine->ops->cleanup_ctx)
		nand->ecc.engine->ops->cleanup_ctx(nand);
}
EXPORT_SYMBOL(nand_ecc_cleanup_ctx);

/**
 * nand_ecc_prepare_io_req - Prepare an I/O request
 * @nand: the NAND device
 * @req: the I/O request
 */
int nand_ecc_prepare_io_req(struct nand_device *nand,
			    struct nand_page_io_req *req)
{
	if (!nand->ecc.engine || !nand->ecc.engine->ops->prepare_io_req)
		return 0;

	return nand->ecc.engine->ops->prepare_io_req(nand, req);
}
EXPORT_SYMBOL(nand_ecc_prepare_io_req);

/**
 * nand_ecc_finish_io_req - Finish an I/O request
 * @nand: the NAND device
 * @req: the I/O request
 */
int nand_ecc_finish_io_req(struct nand_device *nand,
			   struct nand_page_io_req *req)
{
	if (!nand->ecc.engine || !nand->ecc.engine->ops->finish_io_req)
		return 0;

	return nand->ecc.engine->ops->finish_io_req(nand, req);
}
EXPORT_SYMBOL(nand_ecc_finish_io_req);

/* Define default OOB placement schemes for large and small page devices */
static int nand_ooblayout_ecc_sp(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	struct nand_device *nand = mtd_to_nanddev(mtd);
	unsigned int total_ecc_bytes = nand->ecc.ctx.total;

	if (section > 1)
		return -ERANGE;

	if (!section) {
		oobregion->offset = 0;
		if (mtd->oobsize == 16)
			oobregion->length = 4;
		else
			oobregion->length = 3;
	} else {
		if (mtd->oobsize == 8)
			return -ERANGE;

		oobregion->offset = 6;
		oobregion->length = total_ecc_bytes - 4;
	}

	return 0;
}

static int nand_ooblayout_free_sp(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *oobregion)
{
	if (section > 1)
		return -ERANGE;

	if (mtd->oobsize == 16) {
		if (section)
			return -ERANGE;

		oobregion->length = 8;
		oobregion->offset = 8;
	} else {
		oobregion->length = 2;
		if (!section)
			oobregion->offset = 3;
		else
			oobregion->offset = 6;
	}

	return 0;
}

static const struct mtd_ooblayout_ops nand_ooblayout_sp_ops = {
	.ecc = nand_ooblayout_ecc_sp,
	.free = nand_ooblayout_free_sp,
};

const struct mtd_ooblayout_ops *nand_get_small_page_ooblayout(void)
{
	return &nand_ooblayout_sp_ops;
}
EXPORT_SYMBOL_GPL(nand_get_small_page_ooblayout);

static int nand_ooblayout_ecc_lp(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	struct nand_device *nand = mtd_to_nanddev(mtd);
	unsigned int total_ecc_bytes = nand->ecc.ctx.total;

	if (section || !total_ecc_bytes)
		return -ERANGE;

	oobregion->length = total_ecc_bytes;
	oobregion->offset = mtd->oobsize - oobregion->length;

	return 0;
}

static int nand_ooblayout_free_lp(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *oobregion)
{
	struct nand_device *nand = mtd_to_nanddev(mtd);
	unsigned int total_ecc_bytes = nand->ecc.ctx.total;

	if (section)
		return -ERANGE;

	oobregion->length = mtd->oobsize - total_ecc_bytes - 2;
	oobregion->offset = 2;

	return 0;
}

static const struct mtd_ooblayout_ops nand_ooblayout_lp_ops = {
	.ecc = nand_ooblayout_ecc_lp,
	.free = nand_ooblayout_free_lp,
};

const struct mtd_ooblayout_ops *nand_get_large_page_ooblayout(void)
{
	return &nand_ooblayout_lp_ops;
}
EXPORT_SYMBOL_GPL(nand_get_large_page_ooblayout);

/*
 * Support the old "large page" layout used for 1-bit Hamming ECC where ECC
 * are placed at a fixed offset.
 */
static int nand_ooblayout_ecc_lp_hamming(struct mtd_info *mtd, int section,
					 struct mtd_oob_region *oobregion)
{
	struct nand_device *nand = mtd_to_nanddev(mtd);
	unsigned int total_ecc_bytes = nand->ecc.ctx.total;

	if (section)
		return -ERANGE;

	switch (mtd->oobsize) {
	case 64:
		oobregion->offset = 40;
		break;
	case 128:
		oobregion->offset = 80;
		break;
	default:
		return -EINVAL;
	}

	oobregion->length = total_ecc_bytes;
	if (oobregion->offset + oobregion->length > mtd->oobsize)
		return -ERANGE;

	return 0;
}

static int nand_ooblayout_free_lp_hamming(struct mtd_info *mtd, int section,
					  struct mtd_oob_region *oobregion)
{
	struct nand_device *nand = mtd_to_nanddev(mtd);
	unsigned int total_ecc_bytes = nand->ecc.ctx.total;
	int ecc_offset = 0;

	if (section < 0 || section > 1)
		return -ERANGE;

	switch (mtd->oobsize) {
	case 64:
		ecc_offset = 40;
		break;
	case 128:
		ecc_offset = 80;
		break;
	default:
		return -EINVAL;
	}

	if (section == 0) {
		oobregion->offset = 2;
		oobregion->length = ecc_offset - 2;
	} else {
		oobregion->offset = ecc_offset + total_ecc_bytes;
		oobregion->length = mtd->oobsize - oobregion->offset;
	}

	return 0;
}

static const struct mtd_ooblayout_ops nand_ooblayout_lp_hamming_ops = {
	.ecc = nand_ooblayout_ecc_lp_hamming,
	.free = nand_ooblayout_free_lp_hamming,
};

const struct mtd_ooblayout_ops *nand_get_large_page_hamming_ooblayout(void)
{
	return &nand_ooblayout_lp_hamming_ops;
}
EXPORT_SYMBOL_GPL(nand_get_large_page_hamming_ooblayout);

static enum nand_ecc_engine_type
of_get_nand_ecc_engine_type(struct device_node *np)
{
	struct device_node *eng_np;

	if (of_property_read_bool(np, "nand-no-ecc-engine"))
		return NAND_ECC_ENGINE_TYPE_NONE;

	if (of_property_read_bool(np, "nand-use-soft-ecc-engine"))
		return NAND_ECC_ENGINE_TYPE_SOFT;

	eng_np = of_parse_phandle(np, "nand-ecc-engine", 0);
	of_node_put(eng_np);

	if (eng_np) {
		if (eng_np == np)
			return NAND_ECC_ENGINE_TYPE_ON_DIE;
		else
			return NAND_ECC_ENGINE_TYPE_ON_HOST;
	}

	return NAND_ECC_ENGINE_TYPE_INVALID;
}

static const char * const nand_ecc_placement[] = {
	[NAND_ECC_PLACEMENT_OOB] = "oob",
	[NAND_ECC_PLACEMENT_INTERLEAVED] = "interleaved",
};

static enum nand_ecc_placement of_get_nand_ecc_placement(struct device_node *np)
{
	enum nand_ecc_placement placement;
	const char *pm;
	int err;

	err = of_property_read_string(np, "nand-ecc-placement", &pm);
	if (!err) {
		for (placement = NAND_ECC_PLACEMENT_OOB;
		     placement < ARRAY_SIZE(nand_ecc_placement); placement++) {
			if (!strcasecmp(pm, nand_ecc_placement[placement]))
				return placement;
		}
	}

	return NAND_ECC_PLACEMENT_UNKNOWN;
}

static const char * const nand_ecc_algos[] = {
	[NAND_ECC_ALGO_HAMMING] = "hamming",
	[NAND_ECC_ALGO_BCH] = "bch",
	[NAND_ECC_ALGO_RS] = "rs",
};

static enum nand_ecc_algo of_get_nand_ecc_algo(struct device_node *np)
{
	enum nand_ecc_algo ecc_algo;
	const char *pm;
	int err;

	err = of_property_read_string(np, "nand-ecc-algo", &pm);
	if (!err) {
		for (ecc_algo = NAND_ECC_ALGO_HAMMING;
		     ecc_algo < ARRAY_SIZE(nand_ecc_algos);
		     ecc_algo++) {
			if (!strcasecmp(pm, nand_ecc_algos[ecc_algo]))
				return ecc_algo;
		}
	}

	return NAND_ECC_ALGO_UNKNOWN;
}

static int of_get_nand_ecc_step_size(struct device_node *np)
{
	int ret;
	u32 val;

	ret = of_property_read_u32(np, "nand-ecc-step-size", &val);
	return ret ? ret : val;
}

static int of_get_nand_ecc_strength(struct device_node *np)
{
	int ret;
	u32 val;

	ret = of_property_read_u32(np, "nand-ecc-strength", &val);
	return ret ? ret : val;
}

void of_get_nand_ecc_user_config(struct nand_device *nand)
{
	struct device_node *dn = nanddev_get_of_node(nand);
	int strength, size;

	nand->ecc.user_conf.engine_type = of_get_nand_ecc_engine_type(dn);
	nand->ecc.user_conf.algo = of_get_nand_ecc_algo(dn);
	nand->ecc.user_conf.placement = of_get_nand_ecc_placement(dn);

	strength = of_get_nand_ecc_strength(dn);
	if (strength >= 0)
		nand->ecc.user_conf.strength = strength;

	size = of_get_nand_ecc_step_size(dn);
	if (size >= 0)
		nand->ecc.user_conf.step_size = size;

	if (of_property_read_bool(dn, "nand-ecc-maximize"))
		nand->ecc.user_conf.flags |= NAND_ECC_MAXIMIZE_STRENGTH;
}
EXPORT_SYMBOL(of_get_nand_ecc_user_config);

/**
 * nand_ecc_is_strong_enough - Check if the chip configuration meets the
 *                             datasheet requirements.
 *
 * @nand: Device to check
 *
 * If our configuration corrects A bits per B bytes and the minimum
 * required correction level is X bits per Y bytes, then we must ensure
 * both of the following are true:
 *
 * (1) A / B >= X / Y
 * (2) A >= X
 *
 * Requirement (1) ensures we can correct for the required bitflip density.
 * Requirement (2) ensures we can correct even when all bitflips are clumped
 * in the same sector.
 */
bool nand_ecc_is_strong_enough(struct nand_device *nand)
{
	const struct nand_ecc_props *reqs = nanddev_get_ecc_requirements(nand);
	const struct nand_ecc_props *conf = nanddev_get_ecc_conf(nand);
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	int corr, ds_corr;

	if (conf->step_size == 0 || reqs->step_size == 0)
		/* Not enough information */
		return true;

	/*
	 * We get the number of corrected bits per page to compare
	 * the correction density.
	 */
	corr = (mtd->writesize * conf->strength) / conf->step_size;
	ds_corr = (mtd->writesize * reqs->strength) / reqs->step_size;

	return corr >= ds_corr && conf->strength >= reqs->strength;
}
EXPORT_SYMBOL(nand_ecc_is_strong_enough);

/* ECC engine driver internal helpers */
int nand_ecc_init_req_tweaking(struct nand_ecc_req_tweak_ctx *ctx,
			       struct nand_device *nand)
{
	unsigned int total_buffer_size;

	ctx->nand = nand;

	/* Let the user decide the exact length of each buffer */
	if (!ctx->page_buffer_size)
		ctx->page_buffer_size = nanddev_page_size(nand);
	if (!ctx->oob_buffer_size)
		ctx->oob_buffer_size = nanddev_per_page_oobsize(nand);

	total_buffer_size = ctx->page_buffer_size + ctx->oob_buffer_size;

	ctx->spare_databuf = kzalloc(total_buffer_size, GFP_KERNEL);
	if (!ctx->spare_databuf)
		return -ENOMEM;

	ctx->spare_oobbuf = ctx->spare_databuf + ctx->page_buffer_size;

	return 0;
}
EXPORT_SYMBOL_GPL(nand_ecc_init_req_tweaking);

void nand_ecc_cleanup_req_tweaking(struct nand_ecc_req_tweak_ctx *ctx)
{
	kfree(ctx->spare_databuf);
}
EXPORT_SYMBOL_GPL(nand_ecc_cleanup_req_tweaking);

/*
 * Ensure data and OOB area is fully read/written otherwise the correction might
 * not work as expected.
 */
void nand_ecc_tweak_req(struct nand_ecc_req_tweak_ctx *ctx,
			struct nand_page_io_req *req)
{
	struct nand_device *nand = ctx->nand;
	struct nand_page_io_req *orig, *tweak;

	/* Save the original request */
	ctx->orig_req = *req;
	ctx->bounce_data = false;
	ctx->bounce_oob = false;
	orig = &ctx->orig_req;
	tweak = req;

	/* Ensure the request covers the entire page */
	if (orig->datalen < nanddev_page_size(nand)) {
		ctx->bounce_data = true;
		tweak->dataoffs = 0;
		tweak->datalen = nanddev_page_size(nand);
		tweak->databuf.in = ctx->spare_databuf;
		memset(tweak->databuf.in, 0xFF, ctx->page_buffer_size);
	}

	if (orig->ooblen < nanddev_per_page_oobsize(nand)) {
		ctx->bounce_oob = true;
		tweak->ooboffs = 0;
		tweak->ooblen = nanddev_per_page_oobsize(nand);
		tweak->oobbuf.in = ctx->spare_oobbuf;
		memset(tweak->oobbuf.in, 0xFF, ctx->oob_buffer_size);
	}

	/* Copy the data that must be writen in the bounce buffers, if needed */
	if (orig->type == NAND_PAGE_WRITE) {
		if (ctx->bounce_data)
			memcpy((void *)tweak->databuf.out + orig->dataoffs,
			       orig->databuf.out, orig->datalen);

		if (ctx->bounce_oob)
			memcpy((void *)tweak->oobbuf.out + orig->ooboffs,
			       orig->oobbuf.out, orig->ooblen);
	}
}
EXPORT_SYMBOL_GPL(nand_ecc_tweak_req);

void nand_ecc_restore_req(struct nand_ecc_req_tweak_ctx *ctx,
			  struct nand_page_io_req *req)
{
	struct nand_page_io_req *orig, *tweak;

	orig = &ctx->orig_req;
	tweak = req;

	/* Restore the data read from the bounce buffers, if needed */
	if (orig->type == NAND_PAGE_READ) {
		if (ctx->bounce_data)
			memcpy(orig->databuf.in,
			       tweak->databuf.in + orig->dataoffs,
			       orig->datalen);

		if (ctx->bounce_oob)
			memcpy(orig->oobbuf.in,
			       tweak->oobbuf.in + orig->ooboffs,
			       orig->ooblen);
	}

	/* Ensure the original request is restored */
	*req = *orig;
}
EXPORT_SYMBOL_GPL(nand_ecc_restore_req);

struct nand_ecc_engine *nand_ecc_get_sw_engine(struct nand_device *nand)
{
	unsigned int algo = nand->ecc.user_conf.algo;

	if (algo == NAND_ECC_ALGO_UNKNOWN)
		algo = nand->ecc.defaults.algo;

	switch (algo) {
	case NAND_ECC_ALGO_HAMMING:
		return nand_ecc_sw_hamming_get_engine();
	case NAND_ECC_ALGO_BCH:
		return nand_ecc_sw_bch_get_engine();
	default:
		break;
	}

	return NULL;
}
EXPORT_SYMBOL(nand_ecc_get_sw_engine);

struct nand_ecc_engine *nand_ecc_get_on_die_hw_engine(struct nand_device *nand)
{
	return nand->ecc.ondie_engine;
}
EXPORT_SYMBOL(nand_ecc_get_on_die_hw_engine);

int nand_ecc_register_on_host_hw_engine(struct nand_ecc_engine *engine)
{
	struct nand_ecc_engine *item;

	if (!engine)
		return -EINVAL;

	/* Prevent multiple registrations of one engine */
	list_for_each_entry(item, &on_host_hw_engines, node)
		if (item == engine)
			return 0;

	mutex_lock(&on_host_hw_engines_mutex);
	list_add_tail(&engine->node, &on_host_hw_engines);
	mutex_unlock(&on_host_hw_engines_mutex);

	return 0;
}
EXPORT_SYMBOL(nand_ecc_register_on_host_hw_engine);

int nand_ecc_unregister_on_host_hw_engine(struct nand_ecc_engine *engine)
{
	if (!engine)
		return -EINVAL;

	mutex_lock(&on_host_hw_engines_mutex);
	list_del(&engine->node);
	mutex_unlock(&on_host_hw_engines_mutex);

	return 0;
}
EXPORT_SYMBOL(nand_ecc_unregister_on_host_hw_engine);

static struct nand_ecc_engine *nand_ecc_match_on_host_hw_engine(struct device *dev)
{
	struct nand_ecc_engine *item;

	list_for_each_entry(item, &on_host_hw_engines, node)
		if (item->dev == dev)
			return item;

	return NULL;
}

struct nand_ecc_engine *nand_ecc_get_on_host_hw_engine(struct nand_device *nand)
{
	struct nand_ecc_engine *engine = NULL;
	struct device *dev = &nand->mtd.dev;
	struct platform_device *pdev;
	struct device_node *np;

	if (list_empty(&on_host_hw_engines))
		return NULL;

	/* Check for an explicit nand-ecc-engine property */
	np = of_parse_phandle(dev->of_node, "nand-ecc-engine", 0);
	if (np) {
		pdev = of_find_device_by_node(np);
		if (!pdev)
			return ERR_PTR(-EPROBE_DEFER);

		engine = nand_ecc_match_on_host_hw_engine(&pdev->dev);
		platform_device_put(pdev);
		of_node_put(np);

		if (!engine)
			return ERR_PTR(-EPROBE_DEFER);
	}

	if (engine)
		get_device(engine->dev);

	return engine;
}
EXPORT_SYMBOL(nand_ecc_get_on_host_hw_engine);

void nand_ecc_put_on_host_hw_engine(struct nand_device *nand)
{
	put_device(nand->ecc.engine->dev);
}
EXPORT_SYMBOL(nand_ecc_put_on_host_hw_engine);

/*
 * In the case of a pipelined engine, the device registering the ECC
 * engine is not necessarily the ECC engine itself but may be a host controller.
 * It is then useful to provide a helper to retrieve the right device object
 * which actually represents the ECC engine.
 */
struct device *nand_ecc_get_engine_dev(struct device *host)
{
	struct platform_device *ecc_pdev;
	struct device_node *np;

	/*
	 * If the device node contains this property, it means we need to follow
	 * it in order to get the right ECC engine device we are looking for.
	 */
	np = of_parse_phandle(host->of_node, "nand-ecc-engine", 0);
	if (!np)
		return host;

	ecc_pdev = of_find_device_by_node(np);
	if (!ecc_pdev) {
		of_node_put(np);
		return NULL;
	}

	platform_device_put(ecc_pdev);
	of_node_put(np);

	return &ecc_pdev->dev;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Miquel Raynal <miquel.raynal@bootlin.com>");
MODULE_DESCRIPTION("Generic ECC engine");
