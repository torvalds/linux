// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2020 Intel Corporation.

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <sound/sdca_function.h>
#include "internal.h"

struct regmap_mbq_context {
	struct device *dev;

	struct regmap_sdw_mbq_cfg cfg;

	int val_size;
	bool (*readable_reg)(struct device *dev, unsigned int reg);
};

static int regmap_sdw_mbq_size(struct regmap_mbq_context *ctx, unsigned int reg)
{
	int size = ctx->val_size;

	if (ctx->cfg.mbq_size) {
		size = ctx->cfg.mbq_size(ctx->dev, reg);
		if (!size || size > ctx->val_size)
			return -EINVAL;
	}

	return size;
}

static bool regmap_sdw_mbq_deferrable(struct regmap_mbq_context *ctx, unsigned int reg)
{
	if (ctx->cfg.deferrable)
		return ctx->cfg.deferrable(ctx->dev, reg);

	return false;
}

static int regmap_sdw_mbq_poll_busy(struct sdw_slave *slave, unsigned int reg,
				    struct regmap_mbq_context *ctx)
{
	struct device *dev = &slave->dev;
	int val, ret = 0;

	dev_dbg(dev, "Deferring transaction for 0x%x\n", reg);

	reg = SDW_SDCA_CTL(SDW_SDCA_CTL_FUNC(reg), 0,
			   SDCA_CTL_ENTITY_0_FUNCTION_STATUS, 0);

	if (ctx->readable_reg(dev, reg)) {
		ret = read_poll_timeout(sdw_read_no_pm, val,
					val < 0 || !(val & SDCA_CTL_ENTITY_0_FUNCTION_BUSY),
					ctx->cfg.timeout_us, ctx->cfg.retry_us,
					false, slave, reg);
		if (val < 0)
			return val;
		if (ret)
			dev_err(dev, "Function busy timed out 0x%x: %d\n", reg, val);
	} else {
		fsleep(ctx->cfg.timeout_us);
	}

	return ret;
}

static int regmap_sdw_mbq_write_impl(struct sdw_slave *slave,
				     unsigned int reg, unsigned int val,
				     int mbq_size, bool deferrable)
{
	int shift = mbq_size * BITS_PER_BYTE;
	int ret;

	while (--mbq_size > 0) {
		shift -= BITS_PER_BYTE;

		ret = sdw_write_no_pm(slave, SDW_SDCA_MBQ_CTL(reg),
				      (val >> shift) & 0xff);
		if (ret < 0)
			return ret;
	}

	ret = sdw_write_no_pm(slave, reg, val & 0xff);
	if (deferrable && ret == -ENODATA)
		return -EAGAIN;

	return ret;
}

static int regmap_sdw_mbq_write(void *context, unsigned int reg, unsigned int val)
{
	struct regmap_mbq_context *ctx = context;
	struct device *dev = ctx->dev;
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	bool deferrable = regmap_sdw_mbq_deferrable(ctx, reg);
	int mbq_size = regmap_sdw_mbq_size(ctx, reg);
	int ret;

	if (mbq_size < 0)
		return mbq_size;

	/*
	 * Technically the spec does allow a device to set itself to busy for
	 * internal reasons, but since it doesn't provide any information on
	 * how to handle timeouts in that case, for now the code will only
	 * process a single wait/timeout on function busy and a single retry
	 * of the transaction.
	 */
	ret = regmap_sdw_mbq_write_impl(slave, reg, val, mbq_size, deferrable);
	if (ret == -EAGAIN) {
		ret = regmap_sdw_mbq_poll_busy(slave, reg, ctx);
		if (ret)
			return ret;

		ret = regmap_sdw_mbq_write_impl(slave, reg, val, mbq_size, false);
	}

	return ret;
}

static int regmap_sdw_mbq_read_impl(struct sdw_slave *slave,
				    unsigned int reg, unsigned int *val,
				    int mbq_size, bool deferrable)
{
	int shift = BITS_PER_BYTE;
	int read;

	read = sdw_read_no_pm(slave, reg);
	if (read < 0) {
		if (deferrable && read == -ENODATA)
			return -EAGAIN;

		return read;
	}

	*val = read;

	while (--mbq_size > 0) {
		read = sdw_read_no_pm(slave, SDW_SDCA_MBQ_CTL(reg));
		if (read < 0)
			return read;

		*val |= read << shift;
		shift += BITS_PER_BYTE;
	}

	return 0;
}

static int regmap_sdw_mbq_read(void *context, unsigned int reg, unsigned int *val)
{
	struct regmap_mbq_context *ctx = context;
	struct device *dev = ctx->dev;
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	bool deferrable = regmap_sdw_mbq_deferrable(ctx, reg);
	int mbq_size = regmap_sdw_mbq_size(ctx, reg);
	int ret;

	if (mbq_size < 0)
		return mbq_size;

	/*
	 * Technically the spec does allow a device to set itself to busy for
	 * internal reasons, but since it doesn't provide any information on
	 * how to handle timeouts in that case, for now the code will only
	 * process a single wait/timeout on function busy and a single retry
	 * of the transaction.
	 */
	ret = regmap_sdw_mbq_read_impl(slave, reg, val, mbq_size, deferrable);
	if (ret == -EAGAIN) {
		ret = regmap_sdw_mbq_poll_busy(slave, reg, ctx);
		if (ret)
			return ret;

		ret = regmap_sdw_mbq_read_impl(slave, reg, val, mbq_size, false);
	}

	return ret;
}

static const struct regmap_bus regmap_sdw_mbq = {
	.reg_read = regmap_sdw_mbq_read,
	.reg_write = regmap_sdw_mbq_write,
	.reg_format_endian_default = REGMAP_ENDIAN_LITTLE,
	.val_format_endian_default = REGMAP_ENDIAN_LITTLE,
};

static int regmap_sdw_mbq_config_check(const struct regmap_config *config)
{
	if (config->val_bits > (sizeof(unsigned int) * BITS_PER_BYTE))
		return -ENOTSUPP;

	/* Registers are 32 bits wide */
	if (config->reg_bits != 32)
		return -ENOTSUPP;

	if (config->pad_bits != 0)
		return -ENOTSUPP;

	return 0;
}

static struct regmap_mbq_context *
regmap_sdw_mbq_gen_context(struct device *dev,
			   const struct regmap_config *config,
			   const struct regmap_sdw_mbq_cfg *mbq_config)
{
	struct regmap_mbq_context *ctx;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->dev = dev;

	if (mbq_config)
		ctx->cfg = *mbq_config;

	ctx->val_size = config->val_bits / BITS_PER_BYTE;
	ctx->readable_reg = config->readable_reg;

	return ctx;
}

struct regmap *__regmap_init_sdw_mbq(struct sdw_slave *sdw,
				     const struct regmap_config *config,
				     const struct regmap_sdw_mbq_cfg *mbq_config,
				     struct lock_class_key *lock_key,
				     const char *lock_name)
{
	struct regmap_mbq_context *ctx;
	int ret;

	ret = regmap_sdw_mbq_config_check(config);
	if (ret)
		return ERR_PTR(ret);

	ctx = regmap_sdw_mbq_gen_context(&sdw->dev, config, mbq_config);
	if (IS_ERR(ctx))
		return ERR_CAST(ctx);

	return __regmap_init(&sdw->dev, &regmap_sdw_mbq, ctx,
			     config, lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__regmap_init_sdw_mbq);

struct regmap *__devm_regmap_init_sdw_mbq(struct sdw_slave *sdw,
					  const struct regmap_config *config,
					  const struct regmap_sdw_mbq_cfg *mbq_config,
					  struct lock_class_key *lock_key,
					  const char *lock_name)
{
	struct regmap_mbq_context *ctx;
	int ret;

	ret = regmap_sdw_mbq_config_check(config);
	if (ret)
		return ERR_PTR(ret);

	ctx = regmap_sdw_mbq_gen_context(&sdw->dev, config, mbq_config);
	if (IS_ERR(ctx))
		return ERR_CAST(ctx);

	return __devm_regmap_init(&sdw->dev, &regmap_sdw_mbq, ctx,
				  config, lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_sdw_mbq);

MODULE_DESCRIPTION("regmap SoundWire MBQ Module");
MODULE_LICENSE("GPL");
