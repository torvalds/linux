/*
 * Rockchip eFuse Driver
 *
 * Copyright (c) 2015 Rockchip Electronics Co. Ltd.
 * Author: Caesar Wang <wxt@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/rockchip/rockchip_sip.h>

#define T_CSB_P_S		0
#define T_PGENB_P_S		0
#define T_LOAD_P_S		0
#define T_ADDR_P_S		0
#define T_STROBE_P_S		(0 + 110) /* 1.1us */
#define T_CSB_P_L		(0 + 110 + 1000 + 20) /* 200ns */
#define T_PGENB_P_L		(0 + 110 + 1000 + 20)
#define T_LOAD_P_L		(0 + 110 + 1000 + 20)
#define T_ADDR_P_L		(0 + 110 + 1000 + 20)
#define T_STROBE_P_L		(0 + 110 + 1000) /* 10us */
#define T_CSB_R_S		0
#define T_PGENB_R_S		0
#define T_LOAD_R_S		0
#define T_ADDR_R_S		2
#define T_STROBE_R_S		(2 + 3)
#define T_CSB_R_L		(2 + 3 + 3 + 3)
#define T_PGENB_R_L		(2 + 3 + 3 + 3)
#define T_LOAD_R_L		(2 + 3 + 3 + 3)
#define T_ADDR_R_L		(2 + 3 + 3 + 2)
#define T_STROBE_R_L		(2 + 3 + 3)

#define T_CSB_P			0x28
#define T_PGENB_P		0x2c
#define T_LOAD_P		0x30
#define T_ADDR_P		0x34
#define T_STROBE_P		0x38
#define T_CSB_R			0x3c
#define T_PGENB_R		0x40
#define T_LOAD_R		0x44
#define T_ADDR_R		0x48
#define T_STROBE_R		0x4c

#define RK1808_MOD		0x00
#define RK1808_INT_STATUS	RK3328_INT_STATUS
#define RK1808_DOUT		RK3328_DOUT
#define RK1808_AUTO_CTRL	RK3328_AUTO_CTRL
#define RK1808_USER_MODE	BIT(0)
#define RK1808_INT_FINISH	RK3328_INT_FINISH
#define RK1808_AUTO_ENB		RK3328_AUTO_ENB
#define RK1808_AUTO_RD		RK3328_AUTO_RD
#define RK1808_A_SHIFT		RK3399_A_SHIFT
#define RK1808_A_MASK		RK3399_A_MASK
#define RK1808_NBYTES		RK3399_NBYTES

#define RK3128_A_SHIFT		7
#define RK3288_A_SHIFT		6
#define RK3288_A_MASK		0x3ff
#define RK3288_PGENB		BIT(3)
#define RK3288_LOAD		BIT(2)
#define RK3288_STROBE		BIT(1)
#define RK3288_CSB		BIT(0)

#define RK3328_INT_STATUS	0x0018
#define RK3328_DOUT		0x0020
#define RK3328_AUTO_CTRL	0x0024
#define RK3328_INT_FINISH	BIT(0)
#define RK3328_AUTO_ENB		BIT(0)
#define RK3328_AUTO_RD		BIT(1)

#define RK3366_A_SHIFT		6
#define RK3366_A_MASK		0x3ff
#define RK3366_RDEN		BIT(2)
#define RK3366_AEN		BIT(1)

#define RK3399_A_SHIFT		16
#define RK3399_A_MASK		0x3ff
#define RK3399_NBYTES		4
#define RK3399_STROBSFTSEL	BIT(9)
#define RK3399_RSB		BIT(7)
#define RK3399_PD		BIT(5)
#define RK3399_PGENB		BIT(3)
#define RK3399_LOAD		BIT(2)
#define RK3399_STROBE		BIT(1)
#define RK3399_CSB		BIT(0)

#define REG_EFUSE_CTRL		0x0000
#define REG_EFUSE_DOUT		0x0004

struct rockchip_efuse_chip {
	struct device *dev;
	void __iomem *base;
	struct clk *clk;
	struct clk *sclk;
	phys_addr_t phys;
};

static void rk1808_efuse_timing_init(void __iomem *base)
{
	static bool init;

	if (init)
		return;

	/* enable auto mode */
	writel(readl(base + RK1808_MOD) & (~RK1808_USER_MODE),
	       base + RK1808_MOD);

	/* setup efuse timing */
	writel((T_CSB_P_S << 16) | T_CSB_P_L, base + T_CSB_P);
	writel((T_PGENB_P_S << 16) | T_PGENB_P_L, base + T_PGENB_P);
	writel((T_LOAD_P_S << 16) | T_LOAD_P_L, base + T_LOAD_P);
	writel((T_ADDR_P_S << 16) | T_ADDR_P_L, base + T_ADDR_P);
	writel((T_STROBE_P_S << 16) | T_STROBE_P_L, base + T_STROBE_P);
	writel((T_CSB_R_S << 16) | T_CSB_R_L, base + T_CSB_R);
	writel((T_PGENB_R_S << 16) | T_PGENB_R_L, base + T_PGENB_R);
	writel((T_LOAD_R_S << 16) | T_LOAD_R_L, base + T_LOAD_R);
	writel((T_ADDR_R_S << 16) | T_ADDR_R_L, base + T_ADDR_R);
	writel((T_STROBE_R_S << 16) | T_STROBE_R_L, base + T_STROBE_R);

	init = true;
}

static int rockchip_rk1808_efuse_read(void *context, unsigned int offset,
				      void *val, size_t bytes)
{
	struct rockchip_efuse_chip *efuse = context;
	unsigned int addr_start, addr_end, addr_offset, addr_len;
	u32 out_value, status;
	u8 *buf;
	int ret, i = 0;

	ret = clk_prepare_enable(efuse->clk);
	if (ret < 0) {
		dev_err(efuse->dev, "failed to prepare/enable efuse pclk\n");
		return ret;
	}

	ret = clk_prepare_enable(efuse->sclk);
	if (ret < 0) {
		clk_disable_unprepare(efuse->clk);
		dev_err(efuse->dev, "failed to prepare/enable efuse sclk\n");
		return ret;
	}

	rk1808_efuse_timing_init(efuse->base);

	addr_start = rounddown(offset, RK1808_NBYTES) / RK1808_NBYTES;
	addr_end = roundup(offset + bytes, RK1808_NBYTES) / RK1808_NBYTES;
	addr_offset = offset % RK1808_NBYTES;
	addr_len = addr_end - addr_start;

	buf = kzalloc(sizeof(*buf) * addr_len * RK1808_NBYTES, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto nomem;
	}

	while (addr_len--) {
		writel(RK1808_AUTO_RD | RK1808_AUTO_ENB |
		       ((addr_start++ & RK1808_A_MASK) << RK1808_A_SHIFT),
		       efuse->base + RK1808_AUTO_CTRL);
		udelay(2);
		status = readl(efuse->base + RK1808_INT_STATUS);
		if (!(status & RK1808_INT_FINISH)) {
			ret = -EIO;
			goto err;
		}
		out_value = readl(efuse->base + RK1808_DOUT);
		writel(RK1808_INT_FINISH, efuse->base + RK1808_INT_STATUS);

		memcpy(&buf[i], &out_value, RK1808_NBYTES);
		i += RK1808_NBYTES;
	}
	memcpy(val, buf + addr_offset, bytes);
err:
	kfree(buf);
nomem:
	clk_disable_unprepare(efuse->clk);
	clk_disable_unprepare(efuse->sclk);

	return ret;
}

static int rockchip_rk3128_efuse_read(void *context, unsigned int offset,
				      void *val, size_t bytes)
{
	struct rockchip_efuse_chip *efuse = context;
	u8 *buf = val;
	int ret;

	ret = clk_prepare_enable(efuse->clk);
	if (ret < 0) {
		dev_err(efuse->dev, "failed to prepare/enable efuse clk\n");
		return ret;
	}

	writel(RK3288_LOAD | RK3288_PGENB, efuse->base + REG_EFUSE_CTRL);
	udelay(1);
	while (bytes--) {
		writel(readl(efuse->base + REG_EFUSE_CTRL) &
			     (~(RK3288_A_MASK << RK3128_A_SHIFT)),
			     efuse->base + REG_EFUSE_CTRL);
		writel(readl(efuse->base + REG_EFUSE_CTRL) |
			     ((offset++ & RK3288_A_MASK) << RK3128_A_SHIFT),
			     efuse->base + REG_EFUSE_CTRL);
		udelay(1);
		writel(readl(efuse->base + REG_EFUSE_CTRL) |
			     RK3288_STROBE, efuse->base + REG_EFUSE_CTRL);
		udelay(1);
		*buf++ = readb(efuse->base + REG_EFUSE_DOUT);
		writel(readl(efuse->base + REG_EFUSE_CTRL) &
		       (~RK3288_STROBE), efuse->base + REG_EFUSE_CTRL);
		udelay(1);
	}

	/* Switch to standby mode */
	writel(RK3288_PGENB | RK3288_CSB, efuse->base + REG_EFUSE_CTRL);

	clk_disable_unprepare(efuse->clk);

	return 0;
}

static int rockchip_rk3288_efuse_read(void *context, unsigned int offset,
				      void *val, size_t bytes)
{
	struct rockchip_efuse_chip *efuse = context;
	u8 *buf = val;
	int ret;

	ret = clk_prepare_enable(efuse->clk);
	if (ret < 0) {
		dev_err(efuse->dev, "failed to prepare/enable efuse clk\n");
		return ret;
	}

	writel(RK3288_LOAD | RK3288_PGENB, efuse->base + REG_EFUSE_CTRL);
	udelay(1);
	while (bytes--) {
		writel(readl(efuse->base + REG_EFUSE_CTRL) &
			     (~(RK3288_A_MASK << RK3288_A_SHIFT)),
			     efuse->base + REG_EFUSE_CTRL);
		writel(readl(efuse->base + REG_EFUSE_CTRL) |
			     ((offset++ & RK3288_A_MASK) << RK3288_A_SHIFT),
			     efuse->base + REG_EFUSE_CTRL);
		udelay(1);
		writel(readl(efuse->base + REG_EFUSE_CTRL) |
			     RK3288_STROBE, efuse->base + REG_EFUSE_CTRL);
		udelay(1);
		*buf++ = readb(efuse->base + REG_EFUSE_DOUT);
		writel(readl(efuse->base + REG_EFUSE_CTRL) &
		       (~RK3288_STROBE), efuse->base + REG_EFUSE_CTRL);
		udelay(1);
	}

	/* Switch to standby mode */
	writel(RK3288_PGENB | RK3288_CSB, efuse->base + REG_EFUSE_CTRL);

	clk_disable_unprepare(efuse->clk);

	return 0;
}

static int rockchip_rk3288_efuse_secure_read(void *context,
					     unsigned int offset,
					     void *val, size_t bytes)
{
	struct rockchip_efuse_chip *efuse = context;
	u8 *buf = val;
	u32 wr_val;
	int ret;

	ret = clk_prepare_enable(efuse->clk);
	if (ret < 0) {
		dev_err(efuse->dev, "failed to prepare/enable efuse clk\n");
		return ret;
	}

	sip_smc_secure_reg_write(efuse->phys + REG_EFUSE_CTRL,
				 RK3288_LOAD | RK3288_PGENB);
	udelay(1);
	while (bytes--) {
		wr_val = sip_smc_secure_reg_read(efuse->phys + REG_EFUSE_CTRL) &
			 (~(RK3288_A_MASK << RK3288_A_SHIFT));
		sip_smc_secure_reg_write(efuse->phys + REG_EFUSE_CTRL, wr_val);
		wr_val = sip_smc_secure_reg_read(efuse->phys + REG_EFUSE_CTRL) |
			 ((offset++ & RK3288_A_MASK) << RK3288_A_SHIFT);
		sip_smc_secure_reg_write(efuse->phys + REG_EFUSE_CTRL, wr_val);
		udelay(1);
		wr_val = sip_smc_secure_reg_read(efuse->phys + REG_EFUSE_CTRL) |
			 RK3288_STROBE;
		sip_smc_secure_reg_write(efuse->phys + REG_EFUSE_CTRL, wr_val);
		udelay(1);
		*buf++ = sip_smc_secure_reg_read(efuse->phys + REG_EFUSE_DOUT);
		wr_val = sip_smc_secure_reg_read(efuse->phys + REG_EFUSE_CTRL) &
			 (~RK3288_STROBE);
		sip_smc_secure_reg_write(efuse->phys + REG_EFUSE_CTRL, wr_val);
		udelay(1);
	}

	/* Switch to standby mode */
	sip_smc_secure_reg_write(efuse->phys + REG_EFUSE_CTRL,
				 RK3288_PGENB | RK3288_CSB);

	clk_disable_unprepare(efuse->clk);

	return 0;
}

static int rockchip_rk3328_efuse_read(void *context, unsigned int offset,
				      void *val, size_t bytes)
{
	struct rockchip_efuse_chip *efuse = context;
	unsigned int addr_start, addr_end, addr_offset, addr_len;
	u32 out_value, status;
	u8 *buf;
	int ret, i = 0;

	/* 128 Byte efuse, 96 Byte for secure, 32 Byte for non-secure */
	offset += 96;
	ret = clk_prepare_enable(efuse->clk);
	if (ret < 0) {
		dev_err(efuse->dev, "failed to prepare/enable efuse clk\n");
		return ret;
	}

	addr_start = rounddown(offset, RK3399_NBYTES) / RK3399_NBYTES;
	addr_end = roundup(offset + bytes, RK3399_NBYTES) / RK3399_NBYTES;
	addr_offset = offset % RK3399_NBYTES;
	addr_len = addr_end - addr_start;

	buf = kzalloc(sizeof(*buf) * addr_len * RK3399_NBYTES, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto nomem;
	}

	while (addr_len--) {
		writel(RK3328_AUTO_RD | RK3328_AUTO_ENB |
		       ((addr_start++ & RK3399_A_MASK) << RK3399_A_SHIFT),
		       efuse->base + RK3328_AUTO_CTRL);
		udelay(2);
		status = readl(efuse->base + RK3328_INT_STATUS);
		if (!(status & RK3328_INT_FINISH)) {
			ret = -EIO;
			goto err;
		}
		out_value = readl(efuse->base + RK3328_DOUT);
		writel(RK3328_INT_FINISH, efuse->base + RK3328_INT_STATUS);

		memcpy(&buf[i], &out_value, RK3399_NBYTES);
		i += RK3399_NBYTES;
	}
	memcpy(val, buf + addr_offset, bytes);
err:
	kfree(buf);
nomem:
	clk_disable_unprepare(efuse->clk);

	return ret;
}

static int rockchip_rk3366_efuse_read(void *context, unsigned int offset,
				      void *val, size_t bytes)
{
	struct rockchip_efuse_chip *efuse = context;
	u8 *buf = val;
	int ret;

	ret = clk_prepare_enable(efuse->clk);
	if (ret < 0) {
		dev_err(efuse->dev, "failed to prepare/enable efuse clk\n");
		return ret;
	}

	writel(RK3366_RDEN, efuse->base + REG_EFUSE_CTRL);
	udelay(1);
	while (bytes--) {
		writel(readl(efuse->base + REG_EFUSE_CTRL) &
		       (~(RK3366_A_MASK << RK3366_A_SHIFT)),
		       efuse->base + REG_EFUSE_CTRL);
		writel(readl(efuse->base + REG_EFUSE_CTRL) |
		       ((offset++ & RK3366_A_MASK) << RK3366_A_SHIFT),
		       efuse->base + REG_EFUSE_CTRL);
		udelay(1);
		writel(readl(efuse->base + REG_EFUSE_CTRL) |
		       RK3366_AEN, efuse->base + REG_EFUSE_CTRL);
		udelay(1);
		*buf++ = readb(efuse->base + REG_EFUSE_DOUT);
		writel(readl(efuse->base + REG_EFUSE_CTRL) &
		       (~RK3366_AEN), efuse->base + REG_EFUSE_CTRL);
		udelay(1);
	}

	writel(readl(efuse->base + REG_EFUSE_CTRL) &
	       (~RK3366_RDEN), efuse->base + REG_EFUSE_CTRL);

	clk_disable_unprepare(efuse->clk);

	return 0;
}

static int rockchip_rk3368_efuse_read(void *context, unsigned int offset,
				      void *val, size_t bytes)
{
	struct rockchip_efuse_chip *efuse = context;
	u8 *buf = val;
	u32 wr_val;
	int ret;

	ret = clk_prepare_enable(efuse->clk);
	if (ret < 0) {
		dev_err(efuse->dev, "failed to prepare/enable efuse clk\n");
		return ret;
	}

	sip_smc_secure_reg_write(efuse->phys + REG_EFUSE_CTRL,
				 RK3288_LOAD | RK3288_PGENB);
	udelay(1);
	while (bytes--) {
		wr_val = sip_smc_secure_reg_read(efuse->phys + REG_EFUSE_CTRL) &
			 (~(RK3288_A_MASK << RK3288_A_SHIFT));
		sip_smc_secure_reg_write(efuse->phys + REG_EFUSE_CTRL, wr_val);
		wr_val = sip_smc_secure_reg_read(efuse->phys + REG_EFUSE_CTRL) |
			 ((offset++ & RK3288_A_MASK) << RK3288_A_SHIFT);
		sip_smc_secure_reg_write(efuse->phys + REG_EFUSE_CTRL, wr_val);
		udelay(1);
		wr_val = sip_smc_secure_reg_read(efuse->phys + REG_EFUSE_CTRL) |
			 RK3288_STROBE;
		sip_smc_secure_reg_write(efuse->phys + REG_EFUSE_CTRL, wr_val);
		udelay(1);
		*buf++ = sip_smc_secure_reg_read(efuse->phys + REG_EFUSE_DOUT);
		wr_val = sip_smc_secure_reg_read(efuse->phys + REG_EFUSE_CTRL) &
			 (~RK3288_STROBE);
		sip_smc_secure_reg_write(efuse->phys + REG_EFUSE_CTRL, wr_val);
		udelay(1);
	}

	/* Switch to standby mode */
	sip_smc_secure_reg_write(efuse->phys + REG_EFUSE_CTRL,
				 RK3288_PGENB | RK3288_CSB);

	clk_disable_unprepare(efuse->clk);

	return 0;
}

static int rockchip_rk3399_efuse_read(void *context, unsigned int offset,
				      void *val, size_t bytes)
{
	struct rockchip_efuse_chip *efuse = context;
	unsigned int addr_start, addr_end, addr_offset, addr_len;
	u32 out_value;
	u8 *buf;
	int ret, i = 0;

	ret = clk_prepare_enable(efuse->clk);
	if (ret < 0) {
		dev_err(efuse->dev, "failed to prepare/enable efuse clk\n");
		return ret;
	}

	addr_start = rounddown(offset, RK3399_NBYTES) / RK3399_NBYTES;
	addr_end = roundup(offset + bytes, RK3399_NBYTES) / RK3399_NBYTES;
	addr_offset = offset % RK3399_NBYTES;
	addr_len = addr_end - addr_start;

	buf = kzalloc(sizeof(*buf) * addr_len * RK3399_NBYTES, GFP_KERNEL);
	if (!buf) {
		clk_disable_unprepare(efuse->clk);
		return -ENOMEM;
	}

	writel(RK3399_LOAD | RK3399_PGENB | RK3399_STROBSFTSEL | RK3399_RSB,
	       efuse->base + REG_EFUSE_CTRL);
	udelay(1);
	while (addr_len--) {
		writel(readl(efuse->base + REG_EFUSE_CTRL) | RK3399_STROBE |
		       ((addr_start++ & RK3399_A_MASK) << RK3399_A_SHIFT),
		       efuse->base + REG_EFUSE_CTRL);
		udelay(1);
		out_value = readl(efuse->base + REG_EFUSE_DOUT);
		writel(readl(efuse->base + REG_EFUSE_CTRL) & (~RK3399_STROBE),
		       efuse->base + REG_EFUSE_CTRL);
		udelay(1);

		memcpy(&buf[i], &out_value, RK3399_NBYTES);
		i += RK3399_NBYTES;
	}

	/* Switch to standby mode */
	writel(RK3399_PD | RK3399_CSB, efuse->base + REG_EFUSE_CTRL);

	memcpy(val, buf + addr_offset, bytes);

	kfree(buf);

	clk_disable_unprepare(efuse->clk);

	return 0;
}

static struct nvmem_config econfig = {
	.name = "rockchip-efuse",
	.owner = THIS_MODULE,
	.stride = 1,
	.word_size = 1,
	.read_only = true,
};

static const struct of_device_id rockchip_efuse_match[] = {
	/* deprecated but kept around for dts binding compatibility */
	{
		.compatible = "rockchip,rk1808-efuse",
		.data = (void *)&rockchip_rk1808_efuse_read,
	},
	{
		.compatible = "rockchip,rockchip-efuse",
		.data = (void *)&rockchip_rk3288_efuse_read,
	},
	{
		.compatible = "rockchip,rk3066a-efuse",
		.data = (void *)&rockchip_rk3288_efuse_read,
	},
	{
		.compatible = "rockchip,rk3128-efuse",
		.data = (void *)&rockchip_rk3128_efuse_read,
	},
	{
		.compatible = "rockchip,rk3188-efuse",
		.data = (void *)&rockchip_rk3288_efuse_read,
	},
	{
		.compatible = "rockchip,rk322x-efuse",
		.data = (void *)&rockchip_rk3288_efuse_read,
	},
	{
		.compatible = "rockchip,rk3288-efuse",
		.data = (void *)&rockchip_rk3288_efuse_read,
	},
	{
		.compatible = "rockchip,rk3288-secure-efuse",
		.data = (void *)&rockchip_rk3288_efuse_secure_read,
	},
	{
		.compatible = "rockchip,rk3328-efuse",
		.data = (void *)&rockchip_rk3328_efuse_read,
	},
	{
		.compatible = "rockchip,rk3366-efuse",
		.data = (void *)&rockchip_rk3366_efuse_read,
	},
	{
		.compatible = "rockchip,rk3368-efuse",
		.data = (void *)&rockchip_rk3368_efuse_read,
	},
	{
		.compatible = "rockchip,rk3399-efuse",
		.data = (void *)&rockchip_rk3399_efuse_read,
	},
	{ /* sentinel */},
};
MODULE_DEVICE_TABLE(of, rockchip_efuse_match);

static int __init rockchip_efuse_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct nvmem_device *nvmem;
	struct rockchip_efuse_chip *efuse;
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	int count;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data) {
		dev_err(dev, "failed to get match data\n");
		return -EINVAL;
	}

	efuse = devm_kzalloc(&pdev->dev, sizeof(struct rockchip_efuse_chip),
			     GFP_KERNEL);
	if (!efuse)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	efuse->phys = res->start;
	efuse->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(efuse->base))
		return PTR_ERR(efuse->base);

	efuse->clk = devm_clk_get(&pdev->dev, "pclk_efuse");
	if (IS_ERR(efuse->clk))
		return PTR_ERR(efuse->clk);

	count = of_clk_get_parent_count(pdev->dev.of_node);
	if (count == 2)
		efuse->sclk = devm_clk_get(&pdev->dev, "sclk_efuse");
	else
		efuse->sclk = efuse->clk;
	if (IS_ERR(efuse->sclk))
		return PTR_ERR(efuse->sclk);

	efuse->dev = &pdev->dev;
	if (of_property_read_u32_index(dev->of_node,
				       "rockchip,efuse-size",
				       0,
				       &econfig.size))
		econfig.size = resource_size(res);

	econfig.reg_read = match->data;
	econfig.priv = efuse;
	econfig.dev = efuse->dev;
	nvmem = nvmem_register(&econfig);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	platform_set_drvdata(pdev, nvmem);

	return 0;
}

static int rockchip_efuse_remove(struct platform_device *pdev)
{
	struct nvmem_device *nvmem = platform_get_drvdata(pdev);

	return nvmem_unregister(nvmem);
}

static struct platform_driver rockchip_efuse_driver = {
	.remove = rockchip_efuse_remove,
	.driver = {
		.name = "rockchip-efuse",
		.of_match_table = rockchip_efuse_match,
	},
};

static int __init rockchip_efuse_module_init(void)
{
	return platform_driver_probe(&rockchip_efuse_driver,
				     rockchip_efuse_probe);
}

subsys_initcall(rockchip_efuse_module_init);

MODULE_DESCRIPTION("rockchip_efuse driver");
MODULE_LICENSE("GPL v2");
