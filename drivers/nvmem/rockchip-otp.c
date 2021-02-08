// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip OTP Driver
 *
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

/* OTP Register Offsets */
#define OTPC_SBPI_CTRL			0x0020
#define OTPC_SBPI_CMD_VALID_PRE		0x0024
#define OTPC_SBPI_CS_VALID_PRE		0x0028
#define OTPC_SBPI_STATUS		0x002C
#define OTPC_USER_CTRL			0x0100
#define OTPC_USER_ADDR			0x0104
#define OTPC_USER_ENABLE		0x0108
#define OTPC_USER_QP			0x0120
#define OTPC_USER_Q			0x0124
#define OTPC_INT_STATUS			0x0304
#define OTPC_SBPI_CMD0_OFFSET		0x1000
#define OTPC_SBPI_CMD1_OFFSET		0x1004

/* OTP Register bits and masks */
#define OTPC_USER_ADDR_MASK		GENMASK(31, 16)
#define OTPC_USE_USER			BIT(0)
#define OTPC_USE_USER_MASK		GENMASK(16, 16)
#define OTPC_USER_FSM_ENABLE		BIT(0)
#define OTPC_USER_FSM_ENABLE_MASK	GENMASK(16, 16)
#define OTPC_SBPI_DONE			BIT(1)
#define OTPC_USER_DONE			BIT(2)

#define SBPI_DAP_ADDR			0x02
#define SBPI_DAP_ADDR_SHIFT		8
#define SBPI_DAP_ADDR_MASK		GENMASK(31, 24)
#define SBPI_CMD_VALID_MASK		GENMASK(31, 16)
#define SBPI_DAP_CMD_WRF		0xC0
#define SBPI_DAP_REG_ECC		0x3A
#define SBPI_ECC_ENABLE			0x00
#define SBPI_ECC_DISABLE		0x09
#define SBPI_ENABLE			BIT(0)
#define SBPI_ENABLE_MASK		GENMASK(16, 16)

#define OTPC_TIMEOUT			10000
#define OTPC_TIMEOUT_PROG		100000
#define RK3568_NBYTES			2

#define RV1126_OTP_NVM_CEB		0x00
#define RV1126_OTP_NVM_RSTB		0x04
#define RV1126_OTP_NVM_ST		0x18
#define RV1126_OTP_NVM_RADDR		0x1C
#define RV1126_OTP_NVM_RSTART		0x20
#define RV1126_OTP_NVM_RDATA		0x24
#define RV1126_OTP_NVM_TRWH		0x28
#define RV1126_OTP_READ_ST		0x30
#define RV1126_OTP_NVM_PRADDR		0x34
#define RV1126_OTP_NVM_PRLEN		0x38
#define RV1126_OTP_NVM_PRDATA		0x3c
#define RV1126_OTP_NVM_FAILTIME		0x40
#define RV1126_OTP_NVM_PRSTART		0x44
#define RV1126_OTP_NVM_PRSTATE		0x48

/*
 * +----------+------------------+--------------------------+
 * | TYPE     | RANGE(byte)      | NOTE                     |
 * +----------+------------------+--------------------------+
 * | system   | 0x000 ~ 0x0ff    | system info, read only   |
 * +----------+------------------+--------------------------+
 * | oem      | 0x100 ~ 0x1ef    | for customized           |
 * +----------+------------------+--------------------------+
 * | reserved | 0x1f0 ~ 0x1f7    | future extension         |
 * +----------+------------------+--------------------------+
 * | wp       | 0x1f8 ~ 0x1ff    | write protection for oem |
 * +----------+------------------+--------------------------+
 *
 * +-----+    +------------------+
 * | wp  | -- | wp for oem range |
 * +-----+    +------------------+
 * | 1f8 |    | 0x100 ~ 0x11f    |
 * +-----+    +------------------+
 * | 1f9 |    | 0x120 ~ 0x13f    |
 * +-----+    +------------------+
 * | 1fa |    | 0x140 ~ 0x15f    |
 * +-----+    +------------------+
 * | 1fb |    | 0x160 ~ 0x17f    |
 * +-----+    +------------------+
 * | 1fc |    | 0x180 ~ 0x19f    |
 * +-----+    +------------------+
 * | 1fd |    | 0x1a0 ~ 0x1bf    |
 * +-----+    +------------------+
 * | 1fe |    | 0x1c0 ~ 0x1df    |
 * +-----+    +------------------+
 * | 1ff |    | 0x1e0 ~ 0x1ef    |
 * +-----+    +------------------+
 */
#define RV1126_OTP_OEM_OFFSET		0x100
#define RV1126_OTP_OEM_SIZE		0xf0
#define RV1126_OTP_WP_OFFSET		0x1f8
#define RV1126_OTP_WP_SIZE		0x8

/* magic for enable otp write func */
#define ROCKCHIP_OTP_WR_MAGIC		0x524F434B
/* each bit mask 32 bits in OTP NVM */
#define ROCKCHIP_OTP_WP_MASK_NBITS	64

static unsigned int rockchip_otp_wr_magic;
module_param(rockchip_otp_wr_magic, uint, 0644);
MODULE_PARM_DESC(rockchip_otp_wr_magic, "magic for enable otp write func.");

struct rockchip_data;

struct rockchip_otp {
	struct device *dev;
	void __iomem *base;
	struct clk_bulk_data *clks;
	int num_clks;
	struct reset_control *rst;
	struct nvmem_config *config;
	const struct rockchip_data *data;
	struct mutex mutex;
	DECLARE_BITMAP(wp_mask, ROCKCHIP_OTP_WP_MASK_NBITS);
};

struct rockchip_data {
	int size;
	const char * const *clocks;
	int num_clks;
	nvmem_reg_read_t reg_read;
	nvmem_reg_write_t reg_write;
	int (*init)(struct rockchip_otp *otp);
};

static int rockchip_otp_reset(struct rockchip_otp *otp)
{
	int ret;

	ret = reset_control_assert(otp->rst);
	if (ret) {
		dev_err(otp->dev, "failed to assert otp phy %d\n", ret);
		return ret;
	}

	udelay(2);

	ret = reset_control_deassert(otp->rst);
	if (ret) {
		dev_err(otp->dev, "failed to deassert otp phy %d\n", ret);
		return ret;
	}

	return 0;
}

static int px30_otp_wait_status(struct rockchip_otp *otp, u32 flag)
{
	u32 status = 0;
	int ret;

	ret = readl_poll_timeout_atomic(otp->base + OTPC_INT_STATUS, status,
					(status & flag), 1, OTPC_TIMEOUT);
	if (ret)
		return ret;

	/* clean int status */
	writel(flag, otp->base + OTPC_INT_STATUS);

	return 0;
}

static int px30_otp_ecc_enable(struct rockchip_otp *otp, bool enable)
{
	int ret = 0;

	writel(SBPI_DAP_ADDR_MASK | (SBPI_DAP_ADDR << SBPI_DAP_ADDR_SHIFT),
	       otp->base + OTPC_SBPI_CTRL);

	writel(SBPI_CMD_VALID_MASK | 0x1, otp->base + OTPC_SBPI_CMD_VALID_PRE);
	writel(SBPI_DAP_CMD_WRF | SBPI_DAP_REG_ECC,
	       otp->base + OTPC_SBPI_CMD0_OFFSET);
	if (enable)
		writel(SBPI_ECC_ENABLE, otp->base + OTPC_SBPI_CMD1_OFFSET);
	else
		writel(SBPI_ECC_DISABLE, otp->base + OTPC_SBPI_CMD1_OFFSET);

	writel(SBPI_ENABLE_MASK | SBPI_ENABLE, otp->base + OTPC_SBPI_CTRL);

	ret = px30_otp_wait_status(otp, OTPC_SBPI_DONE);
	if (ret < 0)
		dev_err(otp->dev, "timeout during ecc_enable\n");

	return ret;
}

static int px30_otp_read(void *context, unsigned int offset, void *val,
			 size_t bytes)
{
	struct rockchip_otp *otp = context;
	u8 *buf = val;
	int ret = 0;

	ret = clk_bulk_prepare_enable(otp->num_clks, otp->clks);
	if (ret < 0) {
		dev_err(otp->dev, "failed to prepare/enable clks\n");
		return ret;
	}

	ret = rockchip_otp_reset(otp);
	if (ret) {
		dev_err(otp->dev, "failed to reset otp phy\n");
		goto disable_clks;
	}

	ret = px30_otp_ecc_enable(otp, false);
	if (ret < 0) {
		dev_err(otp->dev, "rockchip_otp_ecc_enable err\n");
		goto disable_clks;
	}

	writel(OTPC_USE_USER | OTPC_USE_USER_MASK, otp->base + OTPC_USER_CTRL);
	udelay(5);
	while (bytes--) {
		writel(offset++ | OTPC_USER_ADDR_MASK,
		       otp->base + OTPC_USER_ADDR);
		writel(OTPC_USER_FSM_ENABLE | OTPC_USER_FSM_ENABLE_MASK,
		       otp->base + OTPC_USER_ENABLE);
		ret = px30_otp_wait_status(otp, OTPC_USER_DONE);
		if (ret < 0) {
			dev_err(otp->dev, "timeout during read setup\n");
			goto read_end;
		}
		*buf++ = readb(otp->base + OTPC_USER_Q);
	}

read_end:
	writel(0x0 | OTPC_USE_USER_MASK, otp->base + OTPC_USER_CTRL);
disable_clks:
	clk_bulk_disable_unprepare(otp->num_clks, otp->clks);

	return ret;
}

static int rk3568_otp_read(void *context, unsigned int offset, void *val,
			   size_t bytes)
{
	struct rockchip_otp *otp = context;
	unsigned int addr_start, addr_end, addr_offset, addr_len;
	unsigned int otp_qp;
	u32 out_value;
	u8 *buf;
	int ret = 0, i = 0;

	addr_start = rounddown(offset, RK3568_NBYTES) / RK3568_NBYTES;
	addr_end = roundup(offset + bytes, RK3568_NBYTES) / RK3568_NBYTES;
	addr_offset = offset % RK3568_NBYTES;
	addr_len = addr_end - addr_start;

	buf = kzalloc(array3_size(addr_len, RK3568_NBYTES, sizeof(*buf)),
		      GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = clk_bulk_prepare_enable(otp->num_clks, otp->clks);
	if (ret < 0) {
		dev_err(otp->dev, "failed to prepare/enable clks\n");
		goto out;
	}

	ret = rockchip_otp_reset(otp);
	if (ret) {
		dev_err(otp->dev, "failed to reset otp phy\n");
		goto disable_clks;
	}

	ret = px30_otp_ecc_enable(otp, true);
	if (ret < 0) {
		dev_err(otp->dev, "rockchip_otp_ecc_enable err\n");
		goto disable_clks;
	}

	writel(OTPC_USE_USER | OTPC_USE_USER_MASK, otp->base + OTPC_USER_CTRL);
	udelay(5);
	while (addr_len--) {
		writel(addr_start++ | OTPC_USER_ADDR_MASK,
		       otp->base + OTPC_USER_ADDR);
		writel(OTPC_USER_FSM_ENABLE | OTPC_USER_FSM_ENABLE_MASK,
		       otp->base + OTPC_USER_ENABLE);
		ret = px30_otp_wait_status(otp, OTPC_USER_DONE);
		if (ret < 0) {
			dev_err(otp->dev, "timeout during read setup\n");
			goto read_end;
		}
		otp_qp = readl(otp->base + OTPC_USER_QP);
		if (((otp_qp & 0xc0) == 0xc0) || (otp_qp & 0x20)) {
			ret = -EIO;
			dev_err(otp->dev, "ecc check error during read setup\n");
			goto read_end;
		}
		out_value = readl(otp->base + OTPC_USER_Q);
		memcpy(&buf[i], &out_value, RK3568_NBYTES);
		i += RK3568_NBYTES;
	}

	memcpy(val, buf + addr_offset, bytes);

read_end:
	writel(0x0 | OTPC_USE_USER_MASK, otp->base + OTPC_USER_CTRL);
disable_clks:
	clk_bulk_disable_unprepare(otp->num_clks, otp->clks);
out:
	kfree(buf);

	return ret;
}

static int rv1126_otp_init(struct rockchip_otp *otp)
{
	u32 status = 0;
	int ret;

	writel(0x0, otp->base + RV1126_OTP_NVM_CEB);
	ret = readl_poll_timeout_atomic(otp->base + RV1126_OTP_NVM_ST, status,
					status & 0x1, 1, OTPC_TIMEOUT);
	if (ret < 0) {
		dev_err(otp->dev, "timeout during set ceb\n");
		return ret;
	}

	writel(0x1, otp->base + RV1126_OTP_NVM_RSTB);
	ret = readl_poll_timeout_atomic(otp->base + RV1126_OTP_NVM_ST, status,
					status & 0x4, 1, OTPC_TIMEOUT);
	if (ret < 0) {
		dev_err(otp->dev, "timeout during set rstb\n");
		return ret;
	}

	otp->config->read_only = false;

	return 0;
}

static int rv1126_otp_read(void *context, unsigned int offset, void *val,
			   size_t bytes)
{
	struct rockchip_otp *otp = context;
	u32 status = 0;
	u8 *buf = val;
	int ret = 0;

	while (bytes--) {
		writel(offset++, otp->base + RV1126_OTP_NVM_RADDR);
		writel(0x1, otp->base + RV1126_OTP_NVM_RSTART);
		ret = readl_poll_timeout_atomic(otp->base + RV1126_OTP_READ_ST,
						status, status == 0, 1,
						OTPC_TIMEOUT);
		if (ret < 0) {
			dev_err(otp->dev, "timeout during read setup\n");
			return ret;
		}

		*buf++ = readb(otp->base + RV1126_OTP_NVM_RDATA);
	}

	return 0;
}

static int rv1126_otp_prog(struct rockchip_otp *otp, u32 bit_offset, u32 data,
			   u32 bit_len)
{
	u32 status = 0;
	int ret = 0;

	if (!data)
		return 0;

	writel(bit_offset, otp->base + RV1126_OTP_NVM_PRADDR);
	writel(bit_len - 1, otp->base + RV1126_OTP_NVM_PRLEN);
	writel(data, otp->base + RV1126_OTP_NVM_PRDATA);
	writel(1, otp->base + RV1126_OTP_NVM_PRSTART);
	/* Wait max 100 ms */
	ret = readl_poll_timeout_atomic(otp->base + RV1126_OTP_NVM_PRSTATE,
					status, status == 0, 1,
					OTPC_TIMEOUT_PROG);
	if (ret < 0)
		dev_err(otp->dev, "timeout during prog\n");

	return ret;
}

static int rv1126_otp_write(void *context, unsigned int offset, void *val,
			    size_t bytes)
{
	struct rockchip_otp *otp = context;
	u8 *buf = val;
	u8 val_r, val_w;
	int ret = 0;

	while (bytes--) {
		ret = rv1126_otp_read(context, offset, &val_r, 1);
		if (ret)
			return ret;
		val_w = *buf & (~val_r);
		ret = rv1126_otp_prog(otp, offset * 8, val_w, 8);
		if (ret)
			return ret;
		buf++;
		offset++;
	}

	return 0;
}

static int rv1126_otp_wp(void *context, unsigned int offset, size_t bytes)
{
	struct rockchip_otp *otp = context;

	bitmap_set(otp->wp_mask, (offset - RV1126_OTP_OEM_OFFSET) / 4, bytes / 4);

	return rv1126_otp_write(context, RV1126_OTP_WP_OFFSET, otp->wp_mask,
				RV1126_OTP_WP_SIZE);
}

static int rv1126_otp_oem_write(void *context, unsigned int offset, void *val,
				size_t bytes)
{
	int ret = 0;

	if (offset < RV1126_OTP_OEM_OFFSET ||
	    offset > (RV1126_OTP_OEM_OFFSET + RV1126_OTP_OEM_SIZE - 1) ||
	    bytes > RV1126_OTP_OEM_SIZE ||
	    (offset + bytes) > (RV1126_OTP_OEM_OFFSET + RV1126_OTP_OEM_SIZE))
		return -EINVAL;

	if (!IS_ALIGNED(offset, 4) || !IS_ALIGNED(bytes, 4))
		return -EINVAL;

	ret = rv1126_otp_write(context, offset, val, bytes);
	if (!ret)
		ret = rv1126_otp_wp(context, offset, bytes);

	return ret;
}

static int rockchip_otp_read(void *context, unsigned int offset, void *val,
			     size_t bytes)
{
	struct rockchip_otp *otp = context;
	int ret = -EINVAL;

	mutex_lock(&otp->mutex);
	if (otp->data && otp->data->reg_read)
		ret = otp->data->reg_read(context, offset, val, bytes);
	mutex_unlock(&otp->mutex);

	return ret;
}

static int rockchip_otp_write(void *context, unsigned int offset, void *val,
			      size_t bytes)
{
	struct rockchip_otp *otp = context;
	int ret = -EINVAL;

	mutex_lock(&otp->mutex);
	if (rockchip_otp_wr_magic == ROCKCHIP_OTP_WR_MAGIC &&
	    otp->data && otp->data->reg_write) {
		ret = otp->data->reg_write(context, offset, val, bytes);
		rockchip_otp_wr_magic = 0;
	}
	mutex_unlock(&otp->mutex);

	return ret;
}

static struct nvmem_config otp_config = {
	.name = "rockchip-otp",
	.owner = THIS_MODULE,
	.read_only = true,
	.reg_read = rockchip_otp_read,
	.reg_write = rockchip_otp_write,
	.stride = 1,
	.word_size = 1,
};

static const char * const px30_otp_clocks[] = {
	"otp", "apb_pclk", "phy",
};

static const struct rockchip_data px30_data = {
	.size = 0x40,
	.clocks = px30_otp_clocks,
	.num_clks = ARRAY_SIZE(px30_otp_clocks),
	.reg_read = px30_otp_read,
};

static const char * const rk3568_otp_clocks[] = {
	"usr", "sbpi", "apb", "phy",
};

static const struct rockchip_data rk3568_data = {
	.size = 0x80,
	.clocks = rk3568_otp_clocks,
	.num_clks = ARRAY_SIZE(rk3568_otp_clocks),
	.reg_read = rk3568_otp_read,
};

static const char * const rv1126_otp_clocks[] = {
	"otp", "apb_pclk",
};

static const struct rockchip_data rv1126_data = {
	.size = 0x200,
	.clocks = rv1126_otp_clocks,
	.num_clks = ARRAY_SIZE(rv1126_otp_clocks),
	.init = rv1126_otp_init,
	.reg_read = rv1126_otp_read,
	.reg_write = rv1126_otp_oem_write,
};

static const struct of_device_id rockchip_otp_match[] = {
	{
		.compatible = "rockchip,px30-otp",
		.data = (void *)&px30_data,
	},
	{
		.compatible = "rockchip,rk3308-otp",
		.data = (void *)&px30_data,
	},
	{
		.compatible = "rockchip,rk3568-otp",
		.data = (void *)&rk3568_data,
	},
	{
		.compatible = "rockchip,rv1126-otp",
		.data = (void *)&rv1126_data,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rockchip_otp_match);

static int __init rockchip_otp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_otp *otp;
	const struct rockchip_data *data;
	struct nvmem_device *nvmem;
	int ret, i;

	data = of_device_get_match_data(dev);
	if (!data) {
		dev_err(dev, "failed to get match data\n");
		return -EINVAL;
	}

	otp = devm_kzalloc(&pdev->dev, sizeof(struct rockchip_otp),
			   GFP_KERNEL);
	if (!otp)
		return -ENOMEM;

	mutex_init(&otp->mutex);
	otp->data = data;
	otp->dev = dev;
	otp->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(otp->base))
		return PTR_ERR(otp->base);

	otp->num_clks = data->num_clks;
	otp->clks = devm_kcalloc(dev, otp->num_clks,
				     sizeof(*otp->clks), GFP_KERNEL);
	if (!otp->clks)
		return -ENOMEM;

	for (i = 0; i < otp->num_clks; ++i)
		otp->clks[i].id = data->clocks[i];

	ret = devm_clk_bulk_get(dev, otp->num_clks, otp->clks);
	if (ret)
		return ret;

	otp->rst = devm_reset_control_array_get_optional_exclusive(dev);
	if (IS_ERR(otp->rst))
		return PTR_ERR(otp->rst);

	otp->config = &otp_config;
	otp->config->size = data->size;
	otp->config->priv = otp;
	otp->config->dev = dev;

	if (data->init) {
		ret = data->init(otp);
		if (ret)
			return ret;
	}

	nvmem = devm_nvmem_register(dev, otp->config);

	return PTR_ERR_OR_ZERO(nvmem);
}

static struct platform_driver rockchip_otp_driver = {
	.driver = {
		.name = "rockchip-otp",
		.of_match_table = rockchip_otp_match,
	},
};

static int __init rockchip_otp_module_init(void)
{
	return platform_driver_probe(&rockchip_otp_driver,
				     rockchip_otp_probe);
}

subsys_initcall(rockchip_otp_module_init);

MODULE_DESCRIPTION("Rockchip OTP driver");
MODULE_LICENSE("GPL v2");
