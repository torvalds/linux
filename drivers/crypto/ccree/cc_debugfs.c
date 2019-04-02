// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2012-2018 ARM Limited or its affiliates. */

#include <linux/kernel.h>
#include <linux/defs.h>
#include <linux/stringify.h>
#include "cc_driver.h"
#include "cc_crypto_ctx.h"
#include "cc_defs.h"

struct cc_defs_ctx {
	struct dentry *dir;
};

#define CC_DE_REG(_X) {	\
	.name = __stringify(_X),\
	.offset = CC_REG(_X)	\
	}

/*
 * This is a global var for the dentry of the
 * defs ccree/ dir. It is not tied down to
 * a specific instance of ccree, hence it is
 * global.
 */
static struct dentry *cc_defs_dir;

static struct defs_reg32 de_regs[] = {
	{ .name = "SIGNATURE" }, /* Must be 0th */
	{ .name = "VERSION" }, /* Must be 1st */
	CC_DE_REG(HOST_IRR),
	CC_DE_REG(HOST_POWER_DOWN_EN),
	CC_DE_REG(AXIM_MON_ERR),
	CC_DE_REG(DSCRPTR_QUEUE_CONTENT),
	CC_DE_REG(HOST_IMR),
	CC_DE_REG(AXIM_CFG),
	CC_DE_REG(AXIM_CACHE_PARAMS),
	CC_DE_REG(GPR_HOST),
	CC_DE_REG(AXIM_MON_COMP),
};

void __init cc_defs_global_init(void)
{
	cc_defs_dir = defs_create_dir("ccree", NULL);
}

void __exit cc_defs_global_fini(void)
{
	defs_remove(cc_defs_dir);
}

int cc_defs_init(struct cc_drvdata *drvdata)
{
	struct device *dev = drvdata_to_dev(drvdata);
	struct cc_defs_ctx *ctx;
	struct defs_regset32 *regset;

	de_regs[0].offset = drvdata->sig_offset;
	de_regs[1].offset = drvdata->ver_offset;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	regset = devm_kzalloc(dev, sizeof(*regset), GFP_KERNEL);
	if (!regset)
		return -ENOMEM;

	regset->regs = de_regs;
	regset->nregs = ARRAY_SIZE(de_regs);
	regset->base = drvdata->cc_base;

	ctx->dir = defs_create_dir(drvdata->plat_dev->name, cc_defs_dir);

	defs_create_regset32("regs", 0400, ctx->dir, regset);
	defs_create_bool("coherent", 0400, ctx->dir, &drvdata->coherent);

	drvdata->defs = ctx;

	return 0;
}

void cc_defs_fini(struct cc_drvdata *drvdata)
{
	struct cc_defs_ctx *ctx = (struct cc_defs_ctx *)drvdata->defs;

	defs_remove_recursive(ctx->dir);
}
