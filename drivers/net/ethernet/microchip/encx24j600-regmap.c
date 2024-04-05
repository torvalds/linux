// SPDX-License-Identifier: GPL-2.0-only
/*
 * Register map access API - ENCX24J600 support
 *
 * Copyright 2015 Gridpoint
 *
 * Author: Jon Ringle <jringle@gridpoint.com>
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "encx24j600_hw.h"

static int encx24j600_switch_bank(struct encx24j600_context *ctx,
				  int bank)
{
	int ret = 0;
	int bank_opcode = BANK_SELECT(bank);

	ret = spi_write(ctx->spi, &bank_opcode, 1);
	if (ret == 0)
		ctx->bank = bank;

	return ret;
}

static int encx24j600_cmdn(struct encx24j600_context *ctx, u8 opcode,
			   const void *buf, size_t len)
{
	struct spi_message m;
	struct spi_transfer t[2] = { { .tx_buf = &opcode, .len = 1, },
				     { .tx_buf = buf, .len = len }, };
	spi_message_init(&m);
	spi_message_add_tail(&t[0], &m);
	spi_message_add_tail(&t[1], &m);

	return spi_sync(ctx->spi, &m);
}

static void regmap_lock_mutex(void *context)
{
	struct encx24j600_context *ctx = context;

	mutex_lock(&ctx->mutex);
}

static void regmap_unlock_mutex(void *context)
{
	struct encx24j600_context *ctx = context;

	mutex_unlock(&ctx->mutex);
}

static int regmap_encx24j600_sfr_read(void *context, u8 reg, u8 *val,
				      size_t len)
{
	struct encx24j600_context *ctx = context;
	u8 banked_reg = reg & ADDR_MASK;
	u8 bank = ((reg & BANK_MASK) >> BANK_SHIFT);
	u8 cmd = RCRU;
	int ret = 0;
	int i = 0;
	u8 tx_buf[2];

	if (reg < 0x80) {
		cmd = RCRCODE | banked_reg;
		if ((banked_reg < 0x16) && (ctx->bank != bank))
			ret = encx24j600_switch_bank(ctx, bank);
		if (unlikely(ret))
			return ret;
	} else {
		/* Translate registers that are more effecient using
		 * 3-byte SPI commands
		 */
		switch (reg) {
		case EGPRDPT:
			cmd = RGPRDPT; break;
		case EGPWRPT:
			cmd = RGPWRPT; break;
		case ERXRDPT:
			cmd = RRXRDPT; break;
		case ERXWRPT:
			cmd = RRXWRPT; break;
		case EUDARDPT:
			cmd = RUDARDPT; break;
		case EUDAWRPT:
			cmd = RUDAWRPT; break;
		case EGPDATA:
		case ERXDATA:
		case EUDADATA:
		default:
			return -EINVAL;
		}
	}

	tx_buf[i++] = cmd;
	if (cmd == RCRU)
		tx_buf[i++] = reg;

	ret = spi_write_then_read(ctx->spi, tx_buf, i, val, len);

	return ret;
}

static int regmap_encx24j600_sfr_update(struct encx24j600_context *ctx,
					u8 reg, u8 *val, size_t len,
					u8 unbanked_cmd, u8 banked_code)
{
	u8 banked_reg = reg & ADDR_MASK;
	u8 bank = ((reg & BANK_MASK) >> BANK_SHIFT);
	u8 cmd = unbanked_cmd;
	struct spi_message m;
	struct spi_transfer t[3] = { { .tx_buf = &cmd, .len = sizeof(cmd), },
				     { .tx_buf = &reg, .len = sizeof(reg), },
				     { .tx_buf = val, .len = len }, };

	if (reg < 0x80) {
		int ret = 0;

		cmd = banked_code | banked_reg;
		if ((banked_reg < 0x16) && (ctx->bank != bank))
			ret = encx24j600_switch_bank(ctx, bank);
		if (unlikely(ret))
			return ret;
	} else {
		/* Translate registers that are more effecient using
		 * 3-byte SPI commands
		 */
		switch (reg) {
		case EGPRDPT:
			cmd = WGPRDPT; break;
		case EGPWRPT:
			cmd = WGPWRPT; break;
		case ERXRDPT:
			cmd = WRXRDPT; break;
		case ERXWRPT:
			cmd = WRXWRPT; break;
		case EUDARDPT:
			cmd = WUDARDPT; break;
		case EUDAWRPT:
			cmd = WUDAWRPT; break;
		case EGPDATA:
		case ERXDATA:
		case EUDADATA:
		default:
			return -EINVAL;
		}
	}

	spi_message_init(&m);
	spi_message_add_tail(&t[0], &m);

	if (cmd == unbanked_cmd) {
		t[1].tx_buf = &reg;
		spi_message_add_tail(&t[1], &m);
	}

	spi_message_add_tail(&t[2], &m);
	return spi_sync(ctx->spi, &m);
}

static int regmap_encx24j600_sfr_write(void *context, u8 reg, u8 *val,
				       size_t len)
{
	struct encx24j600_context *ctx = context;

	return regmap_encx24j600_sfr_update(ctx, reg, val, len, WCRU, WCRCODE);
}

static int regmap_encx24j600_sfr_set_bits(struct encx24j600_context *ctx,
					  u8 reg, u8 val)
{
	return regmap_encx24j600_sfr_update(ctx, reg, &val, 1, BFSU, BFSCODE);
}

static int regmap_encx24j600_sfr_clr_bits(struct encx24j600_context *ctx,
					  u8 reg, u8 val)
{
	return regmap_encx24j600_sfr_update(ctx, reg, &val, 1, BFCU, BFCCODE);
}

static int regmap_encx24j600_reg_update_bits(void *context, unsigned int reg,
					     unsigned int mask,
					     unsigned int val)
{
	struct encx24j600_context *ctx = context;

	int ret = 0;
	unsigned int set_mask = mask & val;
	unsigned int clr_mask = mask & ~val;

	if ((reg >= 0x40 && reg < 0x6c) || reg >= 0x80)
		return -EINVAL;

	if (set_mask & 0xff)
		ret = regmap_encx24j600_sfr_set_bits(ctx, reg, set_mask);

	set_mask = (set_mask & 0xff00) >> 8;

	if ((set_mask & 0xff) && (ret == 0))
		ret = regmap_encx24j600_sfr_set_bits(ctx, reg + 1, set_mask);

	if ((clr_mask & 0xff) && (ret == 0))
		ret = regmap_encx24j600_sfr_clr_bits(ctx, reg, clr_mask);

	clr_mask = (clr_mask & 0xff00) >> 8;

	if ((clr_mask & 0xff) && (ret == 0))
		ret = regmap_encx24j600_sfr_clr_bits(ctx, reg + 1, clr_mask);

	return ret;
}

int regmap_encx24j600_spi_write(void *context, u8 reg, const u8 *data,
				size_t count)
{
	struct encx24j600_context *ctx = context;

	if (reg < 0xc0)
		return encx24j600_cmdn(ctx, reg, data, count);

	/* SPI 1-byte command. Ignore data */
	return spi_write(ctx->spi, &reg, 1);
}
EXPORT_SYMBOL_GPL(regmap_encx24j600_spi_write);

int regmap_encx24j600_spi_read(void *context, u8 reg, u8 *data, size_t count)
{
	struct encx24j600_context *ctx = context;

	if (reg == RBSEL && count > 1)
		count = 1;

	return spi_write_then_read(ctx->spi, &reg, sizeof(reg), data, count);
}
EXPORT_SYMBOL_GPL(regmap_encx24j600_spi_read);

static int regmap_encx24j600_write(void *context, const void *data,
				   size_t len)
{
	u8 *dout = (u8 *)data;
	u8 reg = dout[0];
	++dout;
	--len;

	if (reg > 0xa0)
		return regmap_encx24j600_spi_write(context, reg, dout, len);

	if (len > 2)
		return -EINVAL;

	return regmap_encx24j600_sfr_write(context, reg, dout, len);
}

static int regmap_encx24j600_read(void *context,
				  const void *reg_buf, size_t reg_size,
				  void *val, size_t val_size)
{
	u8 reg = *(const u8 *)reg_buf;

	if (reg_size != 1) {
		pr_err("%s: reg=%02x reg_size=%zu\n", __func__, reg, reg_size);
		return -EINVAL;
	}

	if (reg > 0xa0)
		return regmap_encx24j600_spi_read(context, reg, val, val_size);

	if (val_size > 2) {
		pr_err("%s: reg=%02x val_size=%zu\n", __func__, reg, val_size);
		return -EINVAL;
	}

	return regmap_encx24j600_sfr_read(context, reg, val, val_size);
}

static bool encx24j600_regmap_readable(struct device *dev, unsigned int reg)
{
	if ((reg < 0x36) ||
	    ((reg >= 0x40) && (reg < 0x4c)) ||
	    ((reg >= 0x52) && (reg < 0x56)) ||
	    ((reg >= 0x60) && (reg < 0x66)) ||
	    ((reg >= 0x68) && (reg < 0x80)) ||
	    ((reg >= 0x86) && (reg < 0x92)) ||
	    (reg == 0xc8))
		return true;
	else
		return false;
}

static bool encx24j600_regmap_writeable(struct device *dev, unsigned int reg)
{
	if ((reg < 0x12) ||
	    ((reg >= 0x14) && (reg < 0x1a)) ||
	    ((reg >= 0x1c) && (reg < 0x36)) ||
	    ((reg >= 0x40) && (reg < 0x4c)) ||
	    ((reg >= 0x52) && (reg < 0x56)) ||
	    ((reg >= 0x60) && (reg < 0x68)) ||
	    ((reg >= 0x6c) && (reg < 0x80)) ||
	    ((reg >= 0x86) && (reg < 0x92)) ||
	    ((reg >= 0xc0) && (reg < 0xc8)) ||
	    ((reg >= 0xca) && (reg < 0xf0)))
		return true;
	else
		return false;
}

static bool encx24j600_regmap_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ERXHEAD:
	case EDMACS:
	case ETXSTAT:
	case ETXWIRE:
	case ECON1:	/* Can be modified via single byte cmds */
	case ECON2:	/* Can be modified via single byte cmds */
	case ESTAT:
	case EIR:	/* Can be modified via single byte cmds */
	case MIRD:
	case MISTAT:
		return true;
	default:
		break;
	}

	return false;
}

static bool encx24j600_regmap_precious(struct device *dev, unsigned int reg)
{
	/* single byte cmds are precious */
	if (((reg >= 0xc0) && (reg < 0xc8)) ||
	    ((reg >= 0xca) && (reg < 0xf0)))
		return true;
	else
		return false;
}

static int regmap_encx24j600_phy_reg_read(void *context, unsigned int reg,
					  unsigned int *val)
{
	struct encx24j600_context *ctx = context;
	int ret;
	unsigned int mistat;

	reg = MIREGADR_VAL | (reg & PHREG_MASK);
	ret = regmap_write(ctx->regmap, MIREGADR, reg);
	if (unlikely(ret))
		goto err_out;

	ret = regmap_write(ctx->regmap, MICMD, MIIRD);
	if (unlikely(ret))
		goto err_out;

	usleep_range(26, 100);
	while (((ret = regmap_read(ctx->regmap, MISTAT, &mistat)) == 0) &&
	       (mistat & BUSY))
		cpu_relax();

	if (unlikely(ret))
		goto err_out;

	ret = regmap_write(ctx->regmap, MICMD, 0);
	if (unlikely(ret))
		goto err_out;

	ret = regmap_read(ctx->regmap, MIRD, val);

err_out:
	if (ret)
		pr_err("%s: error %d reading reg %02x\n", __func__, ret,
		       reg & PHREG_MASK);

	return ret;
}

static int regmap_encx24j600_phy_reg_write(void *context, unsigned int reg,
					   unsigned int val)
{
	struct encx24j600_context *ctx = context;
	int ret;
	unsigned int mistat;

	reg = MIREGADR_VAL | (reg & PHREG_MASK);
	ret = regmap_write(ctx->regmap, MIREGADR, reg);
	if (unlikely(ret))
		goto err_out;

	ret = regmap_write(ctx->regmap, MIWR, val);
	if (unlikely(ret))
		goto err_out;

	usleep_range(26, 100);
	while (((ret = regmap_read(ctx->regmap, MISTAT, &mistat)) == 0) &&
	       (mistat & BUSY))
		cpu_relax();

err_out:
	if (ret)
		pr_err("%s: error %d writing reg %02x=%04x\n", __func__, ret,
		       reg & PHREG_MASK, val);

	return ret;
}

static bool encx24j600_phymap_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PHCON1:
	case PHSTAT1:
	case PHANA:
	case PHANLPA:
	case PHANE:
	case PHCON2:
	case PHSTAT2:
	case PHSTAT3:
		return true;
	default:
		return false;
	}
}

static bool encx24j600_phymap_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PHCON1:
	case PHCON2:
	case PHANA:
		return true;
	case PHSTAT1:
	case PHSTAT2:
	case PHSTAT3:
	case PHANLPA:
	case PHANE:
	default:
		return false;
	}
}

static bool encx24j600_phymap_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PHSTAT1:
	case PHSTAT2:
	case PHSTAT3:
	case PHANLPA:
	case PHANE:
	case PHCON2:
		return true;
	default:
		return false;
	}
}

static struct regmap_config regcfg = {
	.name = "reg",
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = 0xee,
	.reg_stride = 2,
	.cache_type = REGCACHE_MAPLE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.readable_reg = encx24j600_regmap_readable,
	.writeable_reg = encx24j600_regmap_writeable,
	.volatile_reg = encx24j600_regmap_volatile,
	.precious_reg = encx24j600_regmap_precious,
	.lock = regmap_lock_mutex,
	.unlock = regmap_unlock_mutex,
};

static struct regmap_bus regmap_encx24j600 = {
	.write = regmap_encx24j600_write,
	.read = regmap_encx24j600_read,
	.reg_update_bits = regmap_encx24j600_reg_update_bits,
};

static struct regmap_config phycfg = {
	.name = "phy",
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = 0x1f,
	.cache_type = REGCACHE_MAPLE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.readable_reg = encx24j600_phymap_readable,
	.writeable_reg = encx24j600_phymap_writeable,
	.volatile_reg = encx24j600_phymap_volatile,
};

static struct regmap_bus phymap_encx24j600 = {
	.reg_write = regmap_encx24j600_phy_reg_write,
	.reg_read = regmap_encx24j600_phy_reg_read,
};

int devm_regmap_init_encx24j600(struct device *dev,
				struct encx24j600_context *ctx)
{
	mutex_init(&ctx->mutex);
	regcfg.lock_arg = ctx;
	ctx->regmap = devm_regmap_init(dev, &regmap_encx24j600, ctx, &regcfg);
	if (IS_ERR(ctx->regmap))
		return PTR_ERR(ctx->regmap);
	ctx->phymap = devm_regmap_init(dev, &phymap_encx24j600, ctx, &phycfg);
	if (IS_ERR(ctx->phymap))
		return PTR_ERR(ctx->phymap);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_regmap_init_encx24j600);

MODULE_DESCRIPTION("Microchip ENCX24J600 helpers");
MODULE_LICENSE("GPL");
