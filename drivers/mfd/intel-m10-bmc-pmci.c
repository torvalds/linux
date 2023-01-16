// SPDX-License-Identifier: GPL-2.0
/*
 * MAX10 BMC Platform Management Component Interface (PMCI) based
 * interface.
 *
 * Copyright (C) 2020-2023 Intel Corporation.
 */

#include <linux/device.h>
#include <linux/dfl.h>
#include <linux/mfd/core.h>
#include <linux/mfd/intel-m10-bmc.h>
#include <linux/module.h>
#include <linux/regmap.h>

struct m10bmc_pmci_device {
	void __iomem *base;
	struct intel_m10bmc m10bmc;
};

/*
 * Intel FGPA indirect register access via hardware controller/bridge.
 */
#define INDIRECT_CMD_OFF	0
#define INDIRECT_CMD_CLR	0
#define INDIRECT_CMD_RD		BIT(0)
#define INDIRECT_CMD_WR		BIT(1)
#define INDIRECT_CMD_ACK	BIT(2)

#define INDIRECT_ADDR_OFF	0x4
#define INDIRECT_RD_OFF		0x8
#define INDIRECT_WR_OFF		0xc

#define INDIRECT_INT_US		1
#define INDIRECT_TIMEOUT_US	10000

struct indirect_ctx {
	void __iomem *base;
	struct device *dev;
};

static int indirect_clear_cmd(struct indirect_ctx *ctx)
{
	unsigned int cmd;
	int ret;

	writel(INDIRECT_CMD_CLR, ctx->base + INDIRECT_CMD_OFF);

	ret = readl_poll_timeout(ctx->base + INDIRECT_CMD_OFF, cmd,
				 cmd == INDIRECT_CMD_CLR,
				 INDIRECT_INT_US, INDIRECT_TIMEOUT_US);
	if (ret)
		dev_err(ctx->dev, "timed out waiting clear cmd (residual cmd=0x%x)\n", cmd);

	return ret;
}

static int indirect_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct indirect_ctx *ctx = context;
	unsigned int cmd, ack, tmpval;
	int ret, ret2;

	cmd = readl(ctx->base + INDIRECT_CMD_OFF);
	if (cmd != INDIRECT_CMD_CLR)
		dev_warn(ctx->dev, "residual cmd 0x%x on read entry\n", cmd);

	writel(reg, ctx->base + INDIRECT_ADDR_OFF);
	writel(INDIRECT_CMD_RD, ctx->base + INDIRECT_CMD_OFF);

	ret = readl_poll_timeout(ctx->base + INDIRECT_CMD_OFF, ack,
				 (ack & INDIRECT_CMD_ACK) == INDIRECT_CMD_ACK,
				 INDIRECT_INT_US, INDIRECT_TIMEOUT_US);
	if (ret)
		dev_err(ctx->dev, "read timed out on reg 0x%x ack 0x%x\n", reg, ack);
	else
		tmpval = readl(ctx->base + INDIRECT_RD_OFF);

	ret2 = indirect_clear_cmd(ctx);

	if (ret)
		return ret;
	if (ret2)
		return ret2;

	*val = tmpval;
	return 0;
}

static int indirect_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct indirect_ctx *ctx = context;
	unsigned int cmd, ack;
	int ret, ret2;

	cmd = readl(ctx->base + INDIRECT_CMD_OFF);
	if (cmd != INDIRECT_CMD_CLR)
		dev_warn(ctx->dev, "residual cmd 0x%x on write entry\n", cmd);

	writel(val, ctx->base + INDIRECT_WR_OFF);
	writel(reg, ctx->base + INDIRECT_ADDR_OFF);
	writel(INDIRECT_CMD_WR, ctx->base + INDIRECT_CMD_OFF);

	ret = readl_poll_timeout(ctx->base + INDIRECT_CMD_OFF, ack,
				 (ack & INDIRECT_CMD_ACK) == INDIRECT_CMD_ACK,
				 INDIRECT_INT_US, INDIRECT_TIMEOUT_US);
	if (ret)
		dev_err(ctx->dev, "write timed out on reg 0x%x ack 0x%x\n", reg, ack);

	ret2 = indirect_clear_cmd(ctx);

	if (ret)
		return ret;
	return ret2;
}

static const struct regmap_range m10bmc_pmci_regmap_range[] = {
	regmap_reg_range(M10BMC_N6000_SYS_BASE, M10BMC_N6000_SYS_END),
};

static const struct regmap_access_table m10bmc_pmci_access_table = {
	.yes_ranges	= m10bmc_pmci_regmap_range,
	.n_yes_ranges	= ARRAY_SIZE(m10bmc_pmci_regmap_range),
};

static struct regmap_config m10bmc_pmci_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.wr_table = &m10bmc_pmci_access_table,
	.rd_table = &m10bmc_pmci_access_table,
	.reg_read = &indirect_reg_read,
	.reg_write = &indirect_reg_write,
	.max_register = M10BMC_N6000_SYS_END,
};

static struct mfd_cell m10bmc_pmci_n6000_bmc_subdevs[] = {
	{ .name = "n6000bmc-hwmon" },
};

static const struct m10bmc_csr_map m10bmc_n6000_csr_map = {
	.base = M10BMC_N6000_SYS_BASE,
	.build_version = M10BMC_N6000_BUILD_VER,
	.fw_version = NIOS2_N6000_FW_VERSION,
	.mac_low = M10BMC_N6000_MAC_LOW,
	.mac_high = M10BMC_N6000_MAC_HIGH,
	.doorbell = M10BMC_N6000_DOORBELL,
	.auth_result = M10BMC_N6000_AUTH_RESULT,
	.bmc_prog_addr = M10BMC_N6000_BMC_PROG_ADDR,
	.bmc_reh_addr = M10BMC_N6000_BMC_REH_ADDR,
	.bmc_magic = M10BMC_N6000_BMC_PROG_MAGIC,
	.sr_prog_addr = M10BMC_N6000_SR_PROG_ADDR,
	.sr_reh_addr = M10BMC_N6000_SR_REH_ADDR,
	.sr_magic = M10BMC_N6000_SR_PROG_MAGIC,
	.pr_prog_addr = M10BMC_N6000_PR_PROG_ADDR,
	.pr_reh_addr = M10BMC_N6000_PR_REH_ADDR,
	.pr_magic = M10BMC_N6000_PR_PROG_MAGIC,
	.rsu_update_counter = M10BMC_N6000_STAGING_FLASH_COUNT,
};

static const struct intel_m10bmc_platform_info m10bmc_pmci_n6000 = {
	.cells = m10bmc_pmci_n6000_bmc_subdevs,
	.n_cells = ARRAY_SIZE(m10bmc_pmci_n6000_bmc_subdevs),
	.csr_map = &m10bmc_n6000_csr_map,
};

static int m10bmc_pmci_probe(struct dfl_device *ddev)
{
	struct device *dev = &ddev->dev;
	struct m10bmc_pmci_device *pmci;
	struct indirect_ctx *ctx;

	pmci = devm_kzalloc(dev, sizeof(*pmci), GFP_KERNEL);
	if (!pmci)
		return -ENOMEM;

	pmci->m10bmc.dev = dev;

	pmci->base = devm_ioremap_resource(dev, &ddev->mmio_res);
	if (IS_ERR(pmci->base))
		return PTR_ERR(pmci->base);

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->base = pmci->base + M10BMC_N6000_INDIRECT_BASE;
	ctx->dev = dev;
	indirect_clear_cmd(ctx);
	pmci->m10bmc.regmap = devm_regmap_init(dev, NULL, ctx, &m10bmc_pmci_regmap_config);

	if (IS_ERR(pmci->m10bmc.regmap))
		return PTR_ERR(pmci->m10bmc.regmap);

	return m10bmc_dev_init(&pmci->m10bmc, &m10bmc_pmci_n6000);
}

#define FME_FEATURE_ID_M10BMC_PMCI	0x12

static const struct dfl_device_id m10bmc_pmci_ids[] = {
	{ FME_ID, FME_FEATURE_ID_M10BMC_PMCI },
	{ }
};
MODULE_DEVICE_TABLE(dfl, m10bmc_pmci_ids);

static struct dfl_driver m10bmc_pmci_driver = {
	.drv	= {
		.name       = "intel-m10-bmc",
		.dev_groups = m10bmc_dev_groups,
	},
	.id_table = m10bmc_pmci_ids,
	.probe    = m10bmc_pmci_probe,
};

module_dfl_driver(m10bmc_pmci_driver);

MODULE_DESCRIPTION("MAX10 BMC PMCI-based interface");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
