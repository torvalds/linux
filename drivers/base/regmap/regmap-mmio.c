// SPDX-License-Identifier: GPL-2.0
//
// Register map access API - MMIO support
//
// Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "internal.h"

struct regmap_mmio_context {
	void __iomem *regs;
	unsigned int val_bytes;

	bool attached_clk;
	struct clk *clk;

	void (*reg_write)(struct regmap_mmio_context *ctx,
			  unsigned int reg, unsigned int val);
	unsigned int (*reg_read)(struct regmap_mmio_context *ctx,
			         unsigned int reg);
};

static int regmap_mmio_regbits_check(size_t reg_bits)
{
	switch (reg_bits) {
	case 8:
	case 16:
	case 32:
		return 0;
	default:
		return -EINVAL;
	}
}

static int regmap_mmio_get_min_stride(size_t val_bits)
{
	int min_stride;

	switch (val_bits) {
	case 8:
		/* The core treats 0 as 1 */
		min_stride = 0;
		return 0;
	case 16:
		min_stride = 2;
		break;
	case 32:
		min_stride = 4;
		break;
	default:
		return -EINVAL;
	}

	return min_stride;
}

static void regmap_mmio_write8(struct regmap_mmio_context *ctx,
				unsigned int reg,
				unsigned int val)
{
	writeb(val, ctx->regs + reg);
}

static void regmap_mmio_write8_relaxed(struct regmap_mmio_context *ctx,
				unsigned int reg,
				unsigned int val)
{
	writeb_relaxed(val, ctx->regs + reg);
}

static void regmap_mmio_iowrite8(struct regmap_mmio_context *ctx,
				 unsigned int reg, unsigned int val)
{
	iowrite8(val, ctx->regs + reg);
}

static void regmap_mmio_write16le(struct regmap_mmio_context *ctx,
				  unsigned int reg,
				  unsigned int val)
{
	writew(val, ctx->regs + reg);
}

static void regmap_mmio_write16le_relaxed(struct regmap_mmio_context *ctx,
				  unsigned int reg,
				  unsigned int val)
{
	writew_relaxed(val, ctx->regs + reg);
}

static void regmap_mmio_iowrite16le(struct regmap_mmio_context *ctx,
				    unsigned int reg, unsigned int val)
{
	iowrite16(val, ctx->regs + reg);
}

static void regmap_mmio_write16be(struct regmap_mmio_context *ctx,
				  unsigned int reg,
				  unsigned int val)
{
	writew(swab16(val), ctx->regs + reg);
}

static void regmap_mmio_iowrite16be(struct regmap_mmio_context *ctx,
				    unsigned int reg, unsigned int val)
{
	iowrite16be(val, ctx->regs + reg);
}

static void regmap_mmio_write32le(struct regmap_mmio_context *ctx,
				  unsigned int reg,
				  unsigned int val)
{
	writel(val, ctx->regs + reg);
}

static void regmap_mmio_write32le_relaxed(struct regmap_mmio_context *ctx,
				  unsigned int reg,
				  unsigned int val)
{
	writel_relaxed(val, ctx->regs + reg);
}

static void regmap_mmio_iowrite32le(struct regmap_mmio_context *ctx,
				    unsigned int reg, unsigned int val)
{
	iowrite32(val, ctx->regs + reg);
}

static void regmap_mmio_write32be(struct regmap_mmio_context *ctx,
				  unsigned int reg,
				  unsigned int val)
{
	writel(swab32(val), ctx->regs + reg);
}

static void regmap_mmio_iowrite32be(struct regmap_mmio_context *ctx,
				    unsigned int reg, unsigned int val)
{
	iowrite32be(val, ctx->regs + reg);
}

static int regmap_mmio_write(void *context, unsigned int reg, unsigned int val)
{
	struct regmap_mmio_context *ctx = context;
	int ret;

	if (!IS_ERR(ctx->clk)) {
		ret = clk_enable(ctx->clk);
		if (ret < 0)
			return ret;
	}

	ctx->reg_write(ctx, reg, val);

	if (!IS_ERR(ctx->clk))
		clk_disable(ctx->clk);

	return 0;
}

static unsigned int regmap_mmio_read8(struct regmap_mmio_context *ctx,
				      unsigned int reg)
{
	return readb(ctx->regs + reg);
}

static unsigned int regmap_mmio_read8_relaxed(struct regmap_mmio_context *ctx,
				      unsigned int reg)
{
	return readb_relaxed(ctx->regs + reg);
}

static unsigned int regmap_mmio_ioread8(struct regmap_mmio_context *ctx,
					unsigned int reg)
{
	return ioread8(ctx->regs + reg);
}

static unsigned int regmap_mmio_read16le(struct regmap_mmio_context *ctx,
				         unsigned int reg)
{
	return readw(ctx->regs + reg);
}

static unsigned int regmap_mmio_read16le_relaxed(struct regmap_mmio_context *ctx,
						 unsigned int reg)
{
	return readw_relaxed(ctx->regs + reg);
}

static unsigned int regmap_mmio_ioread16le(struct regmap_mmio_context *ctx,
					   unsigned int reg)
{
	return ioread16(ctx->regs + reg);
}

static unsigned int regmap_mmio_read16be(struct regmap_mmio_context *ctx,
				         unsigned int reg)
{
	return swab16(readw(ctx->regs + reg));
}

static unsigned int regmap_mmio_ioread16be(struct regmap_mmio_context *ctx,
					   unsigned int reg)
{
	return ioread16be(ctx->regs + reg);
}

static unsigned int regmap_mmio_read32le(struct regmap_mmio_context *ctx,
				         unsigned int reg)
{
	return readl(ctx->regs + reg);
}

static unsigned int regmap_mmio_read32le_relaxed(struct regmap_mmio_context *ctx,
						 unsigned int reg)
{
	return readl_relaxed(ctx->regs + reg);
}

static unsigned int regmap_mmio_ioread32le(struct regmap_mmio_context *ctx,
					   unsigned int reg)
{
	return ioread32(ctx->regs + reg);
}

static unsigned int regmap_mmio_read32be(struct regmap_mmio_context *ctx,
				         unsigned int reg)
{
	return swab32(readl(ctx->regs + reg));
}

static unsigned int regmap_mmio_ioread32be(struct regmap_mmio_context *ctx,
					   unsigned int reg)
{
	return ioread32be(ctx->regs + reg);
}

static int regmap_mmio_read(void *context, unsigned int reg, unsigned int *val)
{
	struct regmap_mmio_context *ctx = context;
	int ret;

	if (!IS_ERR(ctx->clk)) {
		ret = clk_enable(ctx->clk);
		if (ret < 0)
			return ret;
	}

	*val = ctx->reg_read(ctx, reg);

	if (!IS_ERR(ctx->clk))
		clk_disable(ctx->clk);

	return 0;
}

static void regmap_mmio_free_context(void *context)
{
	struct regmap_mmio_context *ctx = context;

	if (!IS_ERR(ctx->clk)) {
		clk_unprepare(ctx->clk);
		if (!ctx->attached_clk)
			clk_put(ctx->clk);
	}
	kfree(context);
}

static const struct regmap_bus regmap_mmio = {
	.fast_io = true,
	.reg_write = regmap_mmio_write,
	.reg_read = regmap_mmio_read,
	.free_context = regmap_mmio_free_context,
	.val_format_endian_default = REGMAP_ENDIAN_LITTLE,
};

static struct regmap_mmio_context *regmap_mmio_gen_context(struct device *dev,
					const char *clk_id,
					void __iomem *regs,
					const struct regmap_config *config)
{
	struct regmap_mmio_context *ctx;
	int min_stride;
	int ret;

	ret = regmap_mmio_regbits_check(config->reg_bits);
	if (ret)
		return ERR_PTR(ret);

	if (config->pad_bits)
		return ERR_PTR(-EINVAL);

	min_stride = regmap_mmio_get_min_stride(config->val_bits);
	if (min_stride < 0)
		return ERR_PTR(min_stride);

	if (config->reg_stride < min_stride)
		return ERR_PTR(-EINVAL);

	if (config->use_relaxed_mmio && config->io_port)
		return ERR_PTR(-EINVAL);

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->regs = regs;
	ctx->val_bytes = config->val_bits / 8;
	ctx->clk = ERR_PTR(-ENODEV);

	switch (regmap_get_val_endian(dev, &regmap_mmio, config)) {
	case REGMAP_ENDIAN_DEFAULT:
	case REGMAP_ENDIAN_LITTLE:
#ifdef __LITTLE_ENDIAN
	case REGMAP_ENDIAN_NATIVE:
#endif
		switch (config->val_bits) {
		case 8:
			if (config->io_port) {
				ctx->reg_read = regmap_mmio_ioread8;
				ctx->reg_write = regmap_mmio_iowrite8;
			} else if (config->use_relaxed_mmio) {
				ctx->reg_read = regmap_mmio_read8_relaxed;
				ctx->reg_write = regmap_mmio_write8_relaxed;
			} else {
				ctx->reg_read = regmap_mmio_read8;
				ctx->reg_write = regmap_mmio_write8;
			}
			break;
		case 16:
			if (config->io_port) {
				ctx->reg_read = regmap_mmio_ioread16le;
				ctx->reg_write = regmap_mmio_iowrite16le;
			} else if (config->use_relaxed_mmio) {
				ctx->reg_read = regmap_mmio_read16le_relaxed;
				ctx->reg_write = regmap_mmio_write16le_relaxed;
			} else {
				ctx->reg_read = regmap_mmio_read16le;
				ctx->reg_write = regmap_mmio_write16le;
			}
			break;
		case 32:
			if (config->io_port) {
				ctx->reg_read = regmap_mmio_ioread32le;
				ctx->reg_write = regmap_mmio_iowrite32le;
			} else if (config->use_relaxed_mmio) {
				ctx->reg_read = regmap_mmio_read32le_relaxed;
				ctx->reg_write = regmap_mmio_write32le_relaxed;
			} else {
				ctx->reg_read = regmap_mmio_read32le;
				ctx->reg_write = regmap_mmio_write32le;
			}
			break;
		default:
			ret = -EINVAL;
			goto err_free;
		}
		break;
	case REGMAP_ENDIAN_BIG:
#ifdef __BIG_ENDIAN
	case REGMAP_ENDIAN_NATIVE:
#endif
		switch (config->val_bits) {
		case 8:
			if (config->io_port) {
				ctx->reg_read = regmap_mmio_ioread8;
				ctx->reg_write = regmap_mmio_iowrite8;
			} else {
				ctx->reg_read = regmap_mmio_read8;
				ctx->reg_write = regmap_mmio_write8;
			}
			break;
		case 16:
			if (config->io_port) {
				ctx->reg_read = regmap_mmio_ioread16be;
				ctx->reg_write = regmap_mmio_iowrite16be;
			} else {
				ctx->reg_read = regmap_mmio_read16be;
				ctx->reg_write = regmap_mmio_write16be;
			}
			break;
		case 32:
			if (config->io_port) {
				ctx->reg_read = regmap_mmio_ioread32be;
				ctx->reg_write = regmap_mmio_iowrite32be;
			} else {
				ctx->reg_read = regmap_mmio_read32be;
				ctx->reg_write = regmap_mmio_write32be;
			}
			break;
		default:
			ret = -EINVAL;
			goto err_free;
		}
		break;
	default:
		ret = -EINVAL;
		goto err_free;
	}

	if (clk_id == NULL)
		return ctx;

	ctx->clk = clk_get(dev, clk_id);
	if (IS_ERR(ctx->clk)) {
		ret = PTR_ERR(ctx->clk);
		goto err_free;
	}

	ret = clk_prepare(ctx->clk);
	if (ret < 0) {
		clk_put(ctx->clk);
		goto err_free;
	}

	return ctx;

err_free:
	kfree(ctx);

	return ERR_PTR(ret);
}

struct regmap *__regmap_init_mmio_clk(struct device *dev, const char *clk_id,
				      void __iomem *regs,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name)
{
	struct regmap_mmio_context *ctx;

	ctx = regmap_mmio_gen_context(dev, clk_id, regs, config);
	if (IS_ERR(ctx))
		return ERR_CAST(ctx);

	return __regmap_init(dev, &regmap_mmio, ctx, config,
			     lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__regmap_init_mmio_clk);

struct regmap *__devm_regmap_init_mmio_clk(struct device *dev,
					   const char *clk_id,
					   void __iomem *regs,
					   const struct regmap_config *config,
					   struct lock_class_key *lock_key,
					   const char *lock_name)
{
	struct regmap_mmio_context *ctx;

	ctx = regmap_mmio_gen_context(dev, clk_id, regs, config);
	if (IS_ERR(ctx))
		return ERR_CAST(ctx);

	return __devm_regmap_init(dev, &regmap_mmio, ctx, config,
				  lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_mmio_clk);

int regmap_mmio_attach_clk(struct regmap *map, struct clk *clk)
{
	struct regmap_mmio_context *ctx = map->bus_context;

	ctx->clk = clk;
	ctx->attached_clk = true;

	return clk_prepare(ctx->clk);
}
EXPORT_SYMBOL_GPL(regmap_mmio_attach_clk);

void regmap_mmio_detach_clk(struct regmap *map)
{
	struct regmap_mmio_context *ctx = map->bus_context;

	clk_unprepare(ctx->clk);

	ctx->attached_clk = false;
	ctx->clk = NULL;
}
EXPORT_SYMBOL_GPL(regmap_mmio_detach_clk);

MODULE_LICENSE("GPL v2");
