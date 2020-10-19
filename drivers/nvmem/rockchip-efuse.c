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

#define RK3328_SECURE_SIZES	96
#define RK3328_INT_STATUS	0x0018
#define RK3328_DOUT		0x0020
#define RK3328_AUTO_CTRL	0x0024
#define RK3328_INT_FINISH	BIT(0)
#define RK3328_AUTO_ENB		BIT(0)
#define RK3328_AUTO_RD		BIT(1)

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
	struct clk_bulk_data *clks;
	int num_clks;
	phys_addr_t phys;
	struct mutex mutex;
};

static void rk1808_efuse_timing_init(void __iomem *base)
{
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
}

static void rk1808_efuse_timing_deinit(void __iomem *base)
{
	/* disable auto mode */
	writel(readl(base + RK1808_MOD) | RK1808_USER_MODE,
	       base + RK1808_MOD);

	/* clear efuse timing */
	writel(0, base + T_CSB_P);
	writel(0, base + T_PGENB_P);
	writel(0, base + T_LOAD_P);
	writel(0, base + T_ADDR_P);
	writel(0, base + T_STROBE_P);
	writel(0, base + T_CSB_R);
	writel(0, base + T_PGENB_R);
	writel(0, base + T_LOAD_R);
	writel(0, base + T_ADDR_R);
	writel(0, base + T_STROBE_R);
}

static int rockchip_rk1808_efuse_read(void *context, unsigned int offset,
				      void *val, size_t bytes)
{
	struct rockchip_efuse_chip *efuse = context;
	unsigned int addr_start, addr_end, addr_offset, addr_len;
	u32 out_value, status;
	u8 *buf;
	int ret, i = 0;

	mutex_lock(&efuse->mutex);

	ret = clk_bulk_prepare_enable(efuse->num_clks, efuse->clks);
	if (ret < 0) {
		dev_err(efuse->dev, "failed to prepare/enable efuse clk\n");
		goto out;
	}

	addr_start = rounddown(offset, RK1808_NBYTES) / RK1808_NBYTES;
	addr_end = roundup(offset + bytes, RK1808_NBYTES) / RK1808_NBYTES;
	addr_offset = offset % RK1808_NBYTES;
	addr_len = addr_end - addr_start;

	buf = kzalloc(sizeof(*buf) * addr_len * RK1808_NBYTES, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto nomem;
	}

	rk1808_efuse_timing_init(efuse->base);

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
	rk1808_efuse_timing_deinit(efuse->base);
	kfree(buf);
nomem:
	rk1808_efuse_timing_deinit(efuse->base);
	clk_bulk_disable_unprepare(efuse->num_clks, efuse->clks);
out:
	mutex_unlock(&efuse->mutex);

	return ret;
}

static int rockchip_rk3128_efuse_read(void *context, unsigned int offset,
				      void *val, size_t bytes)
{
	struct rockchip_efuse_chip *efuse = context;
	u8 *buf = val;
	int ret;

	ret = clk_bulk_prepare_enable(efuse->num_clks, efuse->clks);
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

	clk_bulk_disable_unprepare(efuse->num_clks, efuse->clks);

	return 0;
}

static int rockchip_rk3288_efuse_read(void *context, unsigned int offset,
				      void *val, size_t bytes)
{
	struct rockchip_efuse_chip *efuse = context;
	u8 *buf = val;
	int ret;

	ret = clk_bulk_prepare_enable(efuse->num_clks, efuse->clks);
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

	clk_bulk_disable_unprepare(efuse->num_clks, efuse->clks);

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

	ret = clk_bulk_prepare_enable(efuse->num_clks, efuse->clks);
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

	clk_bulk_disable_unprepare(efuse->num_clks, efuse->clks);

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

	ret = clk_bulk_prepare_enable(efuse->num_clks, efuse->clks);
	if (ret < 0) {
		dev_err(efuse->dev, "failed to prepare/enable efuse clk\n");
		return ret;
	}

	/* 128 Byte efuse, 96 Byte for secure, 32 Byte for non-secure */
	offset += RK3328_SECURE_SIZES;
	addr_start = rounddown(offset, RK3399_NBYTES) / RK3399_NBYTES;
	addr_end = roundup(offset + bytes, RK3399_NBYTES) / RK3399_NBYTES;
	addr_offset = offset % RK3399_NBYTES;
	addr_len = addr_end - addr_start;

	buf = kzalloc(array3_size(addr_len, RK3399_NBYTES, sizeof(*buf)),
		      GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto nomem;
	}

	while (addr_len--) {
		writel(RK3328_AUTO_RD | RK3328_AUTO_ENB |
		       ((addr_start++ & RK3399_A_MASK) << RK3399_A_SHIFT),
		       efuse->base + RK3328_AUTO_CTRL);
		udelay(4);
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
	clk_bulk_disable_unprepare(efuse->num_clks, efuse->clks);

	return ret;
}

static int rockchip_rk3368_efuse_read(void *context, unsigned int offset,
				      void *val, size_t bytes)
{
	struct rockchip_efuse_chip *efuse = context;
	u8 *buf = val;
	u32 wr_val;
	int ret;

	ret = clk_bulk_prepare_enable(efuse->num_clks, efuse->clks);
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

	clk_bulk_disable_unprepare(efuse->num_clks, efuse->clks);

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

	ret = clk_bulk_prepare_enable(efuse->num_clks, efuse->clks);
	if (ret < 0) {
		dev_err(efuse->dev, "failed to prepare/enable efuse clk\n");
		return ret;
	}

	addr_start = rounddown(offset, RK3399_NBYTES) / RK3399_NBYTES;
	addr_end = roundup(offset + bytes, RK3399_NBYTES) / RK3399_NBYTES;
	addr_offset = offset % RK3399_NBYTES;
	addr_len = addr_end - addr_start;

	buf = kzalloc(array3_size(addr_len, RK3399_NBYTES, sizeof(*buf)),
		      GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto disable_clks;
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

disable_clks:
	clk_bulk_disable_unprepare(efuse->num_clks, efuse->clks);

	return ret;
}

static struct nvmem_config econfig = {
	.name = "rockchip-efuse",
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
		.compatible = "rockchip,rk3228-efuse",
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
	const void *data;
	struct device *dev = &pdev->dev;

	data = of_device_get_match_data(dev);
	if (!data) {
		dev_err(dev, "failed to get match data\n");
		return -EINVAL;
	}

	efuse = devm_kzalloc(dev, sizeof(struct rockchip_efuse_chip),
			     GFP_KERNEL);
	if (!efuse)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	efuse->phys = res->start;
	efuse->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(efuse->base))
		return PTR_ERR(efuse->base);

	efuse->num_clks = devm_clk_bulk_get_all(dev, &efuse->clks);
	if (efuse->num_clks < 1)
		return -ENODEV;

	mutex_init(&efuse->mutex);

	efuse->dev = dev;
	if (of_property_read_u32(dev->of_node, "rockchip,efuse-size",
				 &econfig.size))
		econfig.size = resource_size(res);
	econfig.reg_read = data;
	econfig.priv = efuse;
	econfig.dev = efuse->dev;
	nvmem = devm_nvmem_register(dev, &econfig);

	return PTR_ERR_OR_ZERO(nvmem);
}

static struct platform_driver rockchip_efuse_driver = {
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
