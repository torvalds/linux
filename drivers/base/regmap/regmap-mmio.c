/*
 * Register map access API - MMIO support
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>

struct regmap_mmio_context {
	void __iomem *regs;
	unsigned reg_bytes;
	unsigned val_bytes;
	unsigned pad_bytes;
	struct clk *clk;
};

static inline void regmap_mmio_regsize_check(size_t reg_size)
{
	switch (reg_size) {
	case 1:
	case 2:
	case 4:
#ifdef CONFIG_64BIT
	case 8:
#endif
		break;
	default:
		BUG();
	}
}

static int regmap_mmio_regbits_check(size_t reg_bits)
{
	switch (reg_bits) {
	case 8:
	case 16:
	case 32:
#ifdef CONFIG_64BIT
	case 64:
#endif
		return 0;
	default:
		return -EINVAL;
	}
}

static inline void regmap_mmio_count_check(size_t count, u32 offset)
{
	BUG_ON(count <= offset);
}

static inline unsigned int
regmap_mmio_get_offset(const void *reg, size_t reg_size)
{
	switch (reg_size) {
	case 1:
		return *(u8 *)reg;
	case 2:
		return *(u16 *)reg;
	case 4:
		return *(u32 *)reg;
#ifdef CONFIG_64BIT
	case 8:
		return *(u64 *)reg;
#endif
	default:
		BUG();
	}
}

static int regmap_mmio_gather_write(void *context,
				    const void *reg, size_t reg_size,
				    const void *val, size_t val_size)
{
	struct regmap_mmio_context *ctx = context;
	unsigned int offset;
	int ret;

	regmap_mmio_regsize_check(reg_size);

	if (!IS_ERR(ctx->clk)) {
		ret = clk_enable(ctx->clk);
		if (ret < 0)
			return ret;
	}

	offset = regmap_mmio_get_offset(reg, reg_size);

	while (val_size) {
		switch (ctx->val_bytes) {
		case 1:
			writeb(*(u8 *)val, ctx->regs + offset);
			break;
		case 2:
			writew(*(u16 *)val, ctx->regs + offset);
			break;
		case 4:
			writel(*(u32 *)val, ctx->regs + offset);
			break;
#ifdef CONFIG_64BIT
		case 8:
			writeq(*(u64 *)val, ctx->regs + offset);
			break;
#endif
		default:
			/* Should be caught by regmap_mmio_check_config */
			BUG();
		}
		val_size -= ctx->val_bytes;
		val += ctx->val_bytes;
		offset += ctx->val_bytes;
	}

	if (!IS_ERR(ctx->clk))
		clk_disable(ctx->clk);

	return 0;
}

static int regmap_mmio_write(void *context, const void *data, size_t count)
{
	struct regmap_mmio_context *ctx = context;
	unsigned int offset = ctx->reg_bytes + ctx->pad_bytes;

	regmap_mmio_count_check(count, offset);

	return regmap_mmio_gather_write(context, data, ctx->reg_bytes,
					data + offset, count - offset);
}

static int regmap_mmio_read(void *context,
			    const void *reg, size_t reg_size,
			    void *val, size_t val_size)
{
	struct regmap_mmio_context *ctx = context;
	unsigned int offset;
	int ret;

	regmap_mmio_regsize_check(reg_size);

	if (!IS_ERR(ctx->clk)) {
		ret = clk_enable(ctx->clk);
		if (ret < 0)
			return ret;
	}

	offset = regmap_mmio_get_offset(reg, reg_size);

	while (val_size) {
		switch (ctx->val_bytes) {
		case 1:
			*(u8 *)val = readb(ctx->regs + offset);
			break;
		case 2:
			*(u16 *)val = readw(ctx->regs + offset);
			break;
		case 4:
			*(u32 *)val = readl(ctx->regs + offset);
			break;
#ifdef CONFIG_64BIT
		case 8:
			*(u64 *)val = readq(ctx->regs + offset);
			break;
#endif
		default:
			/* Should be caught by regmap_mmio_check_config */
			BUG();
		}
		val_size -= ctx->val_bytes;
		val += ctx->val_bytes;
		offset += ctx->val_bytes;
	}

	if (!IS_ERR(ctx->clk))
		clk_disable(ctx->clk);

	return 0;
}

static void regmap_mmio_free_context(void *context)
{
	struct regmap_mmio_context *ctx = context;

	if (!IS_ERR(ctx->clk)) {
		clk_unprepare(ctx->clk);
		clk_put(ctx->clk);
	}
	kfree(context);
}

static struct regmap_bus regmap_mmio = {
	.fast_io = true,
	.write = regmap_mmio_write,
	.gather_write = regmap_mmio_gather_write,
	.read = regmap_mmio_read,
	.free_context = regmap_mmio_free_context,
	.reg_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default = REGMAP_ENDIAN_NATIVE,
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

	switch (config->val_bits) {
	case 8:
		/* The core treats 0 as 1 */
		min_stride = 0;
		break;
	case 16:
		min_stride = 2;
		break;
	case 32:
		min_stride = 4;
		break;
#ifdef CONFIG_64BIT
	case 64:
		min_stride = 8;
		break;
#endif
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	if (config->reg_stride < min_stride)
		return ERR_PTR(-EINVAL);

	switch (config->reg_format_endian) {
	case REGMAP_ENDIAN_DEFAULT:
	case REGMAP_ENDIAN_NATIVE:
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->regs = regs;
	ctx->val_bytes = config->val_bits / 8;
	ctx->reg_bytes = config->reg_bits / 8;
	ctx->pad_bytes = config->pad_bits / 8;
	ctx->clk = ERR_PTR(-ENODEV);

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

MODULE_LICENSE("GPL v2");
