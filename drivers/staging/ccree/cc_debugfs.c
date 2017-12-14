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

#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/stringify.h>
#include "ssi_config.h"
#include "ssi_driver.h"
#include "cc_crypto_ctx.h"

struct cc_debugfs_ctx {
	struct dentry *dir;
};

#define CC_DEBUG_REG(_X) {	\
	.name = __stringify(_X),\
	.offset = CC_REG(_X)	\
	}

/*
 * This is a global var for the dentry of the
 * debugfs ccree/ dir. It is not tied down to
 * a specific instance of ccree, hence it is
 * global.
 */
static struct dentry *cc_debugfs_dir;

struct debugfs_reg32 debug_regs[] = {
	CC_DEBUG_REG(HOST_SIGNATURE),
	CC_DEBUG_REG(HOST_IRR),
	CC_DEBUG_REG(HOST_POWER_DOWN_EN),
	CC_DEBUG_REG(AXIM_MON_ERR),
	CC_DEBUG_REG(DSCRPTR_QUEUE_CONTENT),
	CC_DEBUG_REG(HOST_IMR),
	CC_DEBUG_REG(AXIM_CFG),
	CC_DEBUG_REG(AXIM_CACHE_PARAMS),
	CC_DEBUG_REG(HOST_VERSION),
	CC_DEBUG_REG(GPR_HOST),
	CC_DEBUG_REG(AXIM_MON_COMP),
};

int cc_debugfs_global_init(void)
{
	cc_debugfs_dir = debugfs_create_dir("ccree", NULL);

	return !cc_debugfs_dir;
}

void cc_debugfs_global_fini(void)
{
	debugfs_remove(cc_debugfs_dir);
}

int cc_debugfs_init(struct cc_drvdata *drvdata)
{
	struct device *dev = drvdata_to_dev(drvdata);
	struct cc_debugfs_ctx *ctx;
	struct debugfs_regset32 *regset;
	struct dentry *file;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	regset = devm_kzalloc(dev, sizeof(*regset), GFP_KERNEL);
	if (!regset)
		return -ENOMEM;

	regset->regs = debug_regs;
	regset->nregs = ARRAY_SIZE(debug_regs);
	regset->base = drvdata->cc_base;

	ctx->dir = debugfs_create_dir(drvdata->plat_dev->name, cc_debugfs_dir);
	if (!ctx->dir)
		return -ENFILE;

	file = debugfs_create_regset32("regs", 0400, ctx->dir, regset);
	if (!file) {
		debugfs_remove(ctx->dir);
		return -ENFILE;
	}

	file = debugfs_create_bool("coherent", 0400, ctx->dir,
				   &drvdata->coherent);

	if (!file) {
		debugfs_remove_recursive(ctx->dir);
		return -ENFILE;
	}

	drvdata->debugfs = ctx;

	return 0;
}

void cc_debugfs_fini(struct cc_drvdata *drvdata)
{
	struct cc_debugfs_ctx *ctx = (struct cc_debugfs_ctx *)drvdata->debugfs;

	debugfs_remove_recursive(ctx->dir);
}
