// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCIe controller driver for Renesas R-Car Gen4 Series SoCs
 * Copyright (C) 2022-2023 Renesas Electronics Corporation
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include "../../pci.h"
#include "pcie-designware.h"

/* Renesas-specific */
/* PCIe Mode Setting Register 0 */
#define PCIEMSR0		0x0000
#define BIFUR_MOD_SET_ON	BIT(0)
#define DEVICE_TYPE_RC		BIT(4)

/* PCIe Interrupt Status 0 */
#define PCIEINTSTS0		0x0084

/* PCIe Interrupt Status 0 Enable */
#define PCIEINTSTS0EN		0x0310
#define MSI_CTRL_INT		BIT(26)
#define SMLH_LINK_UP		BIT(7)
#define RDLH_LINK_UP		BIT(6)

/* PCIe DMA Interrupt Status Enable */
#define PCIEDMAINTSTSEN		0x0314
#define PCIEDMAINTSTSEN_INIT	GENMASK(15, 0)

/* PCIe Reset Control Register 1 */
#define PCIERSTCTRL1		0x0014
#define APP_HOLD_PHY_RST	BIT(16)
#define APP_LTSSM_ENABLE	BIT(0)

#define RCAR_NUM_SPEED_CHANGE_RETRIES	10
#define RCAR_MAX_LINK_SPEED		4

struct rcar_gen4_pcie {
	struct dw_pcie dw;
	void __iomem *base;
	struct platform_device *pdev;
	enum dw_pcie_device_mode mode;
};
#define to_rcar_gen4_pcie(_dw)	container_of(_dw, struct rcar_gen4_pcie, dw)

static void rcar_gen4_pcie_ltssm_enable(struct rcar_gen4_pcie *rcar,
					bool enable)
{
	u32 val;

	val = readl(rcar->base + PCIERSTCTRL1);
	if (enable) {
		val |= APP_LTSSM_ENABLE;
		val &= ~APP_HOLD_PHY_RST;
	} else {
		/*
		 * Since the datasheet of R-Car doesn't mention how to assert
		 * the APP_HOLD_PHY_RST, don't assert it again. Otherwise,
		 * hang-up issue happened in the dw_edma_core_off() when
		 * the controller didn't detect a PCI device.
		 */
		val &= ~APP_LTSSM_ENABLE;
	}
	writel(val, rcar->base + PCIERSTCTRL1);
}

static int rcar_gen4_pcie_link_up(struct dw_pcie *dw)
{
	struct rcar_gen4_pcie *rcar = to_rcar_gen4_pcie(dw);
	u32 val, mask;

	val = readl(rcar->base + PCIEINTSTS0);
	mask = RDLH_LINK_UP | SMLH_LINK_UP;

	return (val & mask) == mask;
}

/*
 * Manually initiate the speed change. Return 0 if change succeeded; otherwise
 * -ETIMEDOUT.
 */
static int rcar_gen4_pcie_speed_change(struct dw_pcie *dw)
{
	u32 val;
	int i;

	val = dw_pcie_readl_dbi(dw, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val &= ~PORT_LOGIC_SPEED_CHANGE;
	dw_pcie_writel_dbi(dw, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	val = dw_pcie_readl_dbi(dw, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val |= PORT_LOGIC_SPEED_CHANGE;
	dw_pcie_writel_dbi(dw, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	for (i = 0; i < RCAR_NUM_SPEED_CHANGE_RETRIES; i++) {
		val = dw_pcie_readl_dbi(dw, PCIE_LINK_WIDTH_SPEED_CONTROL);
		if (!(val & PORT_LOGIC_SPEED_CHANGE))
			return 0;
		usleep_range(10000, 11000);
	}

	return -ETIMEDOUT;
}

/*
 * Enable LTSSM of this controller and manually initiate the speed change.
 * Always return 0.
 */
static int rcar_gen4_pcie_start_link(struct dw_pcie *dw)
{
	struct rcar_gen4_pcie *rcar = to_rcar_gen4_pcie(dw);
	int i, changes;

	rcar_gen4_pcie_ltssm_enable(rcar, true);

	/*
	 * Require direct speed change with retrying here if the link_gen is
	 * PCIe Gen2 or higher.
	 */
	changes = min_not_zero(dw->link_gen, RCAR_MAX_LINK_SPEED) - 1;

	/*
	 * Since dw_pcie_setup_rc() sets it once, PCIe Gen2 will be trained.
	 * So, this needs remaining times for up to PCIe Gen4 if RC mode.
	 */
	if (changes && rcar->mode == DW_PCIE_RC_TYPE)
		changes--;

	for (i = 0; i < changes; i++) {
		/* It may not be connected in EP mode yet. So, break the loop */
		if (rcar_gen4_pcie_speed_change(dw))
			break;
	}

	return 0;
}

static void rcar_gen4_pcie_stop_link(struct dw_pcie *dw)
{
	struct rcar_gen4_pcie *rcar = to_rcar_gen4_pcie(dw);

	rcar_gen4_pcie_ltssm_enable(rcar, false);
}

static int rcar_gen4_pcie_common_init(struct rcar_gen4_pcie *rcar)
{
	struct dw_pcie *dw = &rcar->dw;
	u32 val;
	int ret;

	ret = clk_bulk_prepare_enable(DW_PCIE_NUM_CORE_CLKS, dw->core_clks);
	if (ret) {
		dev_err(dw->dev, "Enabling core clocks failed\n");
		return ret;
	}

	if (!reset_control_status(dw->core_rsts[DW_PCIE_PWR_RST].rstc))
		reset_control_assert(dw->core_rsts[DW_PCIE_PWR_RST].rstc);

	val = readl(rcar->base + PCIEMSR0);
	if (rcar->mode == DW_PCIE_RC_TYPE) {
		val |= DEVICE_TYPE_RC;
	} else {
		ret = -EINVAL;
		goto err_unprepare;
	}

	if (dw->num_lanes < 4)
		val |= BIFUR_MOD_SET_ON;

	writel(val, rcar->base + PCIEMSR0);

	ret = reset_control_deassert(dw->core_rsts[DW_PCIE_PWR_RST].rstc);
	if (ret)
		goto err_unprepare;

	return 0;

err_unprepare:
	clk_bulk_disable_unprepare(DW_PCIE_NUM_CORE_CLKS, dw->core_clks);

	return ret;
}

static void rcar_gen4_pcie_common_deinit(struct rcar_gen4_pcie *rcar)
{
	struct dw_pcie *dw = &rcar->dw;

	reset_control_assert(dw->core_rsts[DW_PCIE_PWR_RST].rstc);
	clk_bulk_disable_unprepare(DW_PCIE_NUM_CORE_CLKS, dw->core_clks);
}

static int rcar_gen4_pcie_prepare(struct rcar_gen4_pcie *rcar)
{
	struct device *dev = rcar->dw.dev;
	int err;

	pm_runtime_enable(dev);
	err = pm_runtime_resume_and_get(dev);
	if (err < 0) {
		dev_err(dev, "Runtime resume failed\n");
		pm_runtime_disable(dev);
	}

	return err;
}

static void rcar_gen4_pcie_unprepare(struct rcar_gen4_pcie *rcar)
{
	struct device *dev = rcar->dw.dev;

	pm_runtime_put(dev);
	pm_runtime_disable(dev);
}

static int rcar_gen4_pcie_get_resources(struct rcar_gen4_pcie *rcar)
{
	/* Renesas-specific registers */
	rcar->base = devm_platform_ioremap_resource_byname(rcar->pdev, "app");

	return PTR_ERR_OR_ZERO(rcar->base);
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.start_link = rcar_gen4_pcie_start_link,
	.stop_link = rcar_gen4_pcie_stop_link,
	.link_up = rcar_gen4_pcie_link_up,
};

static struct rcar_gen4_pcie *rcar_gen4_pcie_alloc(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rcar_gen4_pcie *rcar;

	rcar = devm_kzalloc(dev, sizeof(*rcar), GFP_KERNEL);
	if (!rcar)
		return ERR_PTR(-ENOMEM);

	rcar->dw.ops = &dw_pcie_ops;
	rcar->dw.dev = dev;
	rcar->pdev = pdev;
	dw_pcie_cap_set(&rcar->dw, EDMA_UNROLL);
	dw_pcie_cap_set(&rcar->dw, REQ_RES);
	platform_set_drvdata(pdev, rcar);

	return rcar;
}

/* Host mode */
static int rcar_gen4_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *dw = to_dw_pcie_from_pp(pp);
	struct rcar_gen4_pcie *rcar = to_rcar_gen4_pcie(dw);
	int ret;
	u32 val;

	gpiod_set_value_cansleep(dw->pe_rst, 1);

	ret = rcar_gen4_pcie_common_init(rcar);
	if (ret)
		return ret;

	/*
	 * According to the section 3.5.7.2 "RC Mode" in DWC PCIe Dual Mode
	 * Rev.5.20a and 3.5.6.1 "RC mode" in DWC PCIe RC databook v5.20a, we
	 * should disable two BARs to avoid unnecessary memory assignment
	 * during device enumeration.
	 */
	dw_pcie_writel_dbi2(dw, PCI_BASE_ADDRESS_0, 0x0);
	dw_pcie_writel_dbi2(dw, PCI_BASE_ADDRESS_1, 0x0);

	/* Enable MSI interrupt signal */
	val = readl(rcar->base + PCIEINTSTS0EN);
	val |= MSI_CTRL_INT;
	writel(val, rcar->base + PCIEINTSTS0EN);

	msleep(PCIE_T_PVPERL_MS);	/* pe_rst requires 100msec delay */

	gpiod_set_value_cansleep(dw->pe_rst, 0);

	return 0;
}

static void rcar_gen4_pcie_host_deinit(struct dw_pcie_rp *pp)
{
	struct dw_pcie *dw = to_dw_pcie_from_pp(pp);
	struct rcar_gen4_pcie *rcar = to_rcar_gen4_pcie(dw);

	gpiod_set_value_cansleep(dw->pe_rst, 1);
	rcar_gen4_pcie_common_deinit(rcar);
}

static const struct dw_pcie_host_ops rcar_gen4_pcie_host_ops = {
	.host_init = rcar_gen4_pcie_host_init,
	.host_deinit = rcar_gen4_pcie_host_deinit,
};

static int rcar_gen4_add_dw_pcie_rp(struct rcar_gen4_pcie *rcar)
{
	struct dw_pcie_rp *pp = &rcar->dw.pp;

	pp->num_vectors = MAX_MSI_IRQS;
	pp->ops = &rcar_gen4_pcie_host_ops;
	rcar->mode = DW_PCIE_RC_TYPE;

	return dw_pcie_host_init(pp);
}

static void rcar_gen4_remove_dw_pcie_rp(struct rcar_gen4_pcie *rcar)
{
	dw_pcie_host_deinit(&rcar->dw.pp);
}

static int rcar_gen4_pcie_probe(struct platform_device *pdev)
{
	struct rcar_gen4_pcie *rcar;
	int err;

	rcar = rcar_gen4_pcie_alloc(pdev);
	if (IS_ERR(rcar))
		return PTR_ERR(rcar);

	err = rcar_gen4_pcie_get_resources(rcar);
	if (err)
		return err;

	err = rcar_gen4_pcie_prepare(rcar);
	if (err)
		return err;

	err = rcar_gen4_add_dw_pcie_rp(rcar);
	if (err)
		goto err_unprepare;

	return 0;

err_unprepare:
	rcar_gen4_pcie_unprepare(rcar);

	return err;
}

static void rcar_gen4_pcie_remove(struct platform_device *pdev)
{
	struct rcar_gen4_pcie *rcar = platform_get_drvdata(pdev);

	rcar_gen4_remove_dw_pcie_rp(rcar);
	rcar_gen4_pcie_unprepare(rcar);
}

static const struct of_device_id rcar_gen4_pcie_of_match[] = {
	{ .compatible = "renesas,rcar-gen4-pcie", },
	{},
};
MODULE_DEVICE_TABLE(of, rcar_gen4_pcie_of_match);

static struct platform_driver rcar_gen4_pcie_driver = {
	.driver = {
		.name = "pcie-rcar-gen4",
		.of_match_table = rcar_gen4_pcie_of_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = rcar_gen4_pcie_probe,
	.remove_new = rcar_gen4_pcie_remove,
};
module_platform_driver(rcar_gen4_pcie_driver);

MODULE_DESCRIPTION("Renesas R-Car Gen4 PCIe controller driver");
MODULE_LICENSE("GPL");
