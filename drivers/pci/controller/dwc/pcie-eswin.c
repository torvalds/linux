// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN PCIe Root Complex driver
 *
 * Copyright 2026, Beijing ESWIN Computing Technology Co., Ltd.
 *
 * Authors: Yu Ning <ningyu@eswincomputing.com>
 *          Senchuan Zhang <zhangsenchuan@eswincomputing.com>
 *          Yanghui Ou <ouyanghui@eswincomputing.com>
 */

#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/resource.h>
#include <linux/reset.h>
#include <linux/types.h>

#include "pcie-designware.h"

/* ELBI registers */
#define PCIEELBI_CTRL0_OFFSET		0x0
#define PCIEELBI_STATUS0_OFFSET		0x100

/* LTSSM register fields */
#define PCIEELBI_APP_LTSSM_ENABLE	BIT(5)

/* APP_HOLD_PHY_RST register fields */
#define PCIEELBI_APP_HOLD_PHY_RST	BIT(6)

/* PM_SEL_AUX_CLK register fields */
#define PCIEELBI_PM_SEL_AUX_CLK		BIT(16)

/* DEV_TYPE register fields */
#define PCIEELBI_CTRL0_DEV_TYPE		GENMASK(3, 0)

/* Vendor and device ID value */
#define PCI_VENDOR_ID_ESWIN		0x1fe1
#define PCI_DEVICE_ID_ESWIN_EIC7700	0x2030

#define ESWIN_NUM_RSTS			ARRAY_SIZE(eswin_pcie_rsts)

static const char * const eswin_pcie_rsts[] = {
	"pwr",
	"dbi",
};

struct eswin_pcie_data {
	bool skip_l23;
};

struct eswin_pcie_port {
	struct list_head list;
	struct reset_control *perst;
	int num_lanes;
};

struct eswin_pcie {
	struct dw_pcie pci;
	struct clk_bulk_data *clks;
	struct reset_control_bulk_data resets[ESWIN_NUM_RSTS];
	struct list_head ports;
	const struct eswin_pcie_data *data;
	int num_clks;
};

#define to_eswin_pcie(x) dev_get_drvdata((x)->dev)

static int eswin_pcie_start_link(struct dw_pcie *pci)
{
	u32 val;

	/* Enable LTSSM */
	val = readl_relaxed(pci->elbi_base + PCIEELBI_CTRL0_OFFSET);
	val |= PCIEELBI_APP_LTSSM_ENABLE;
	writel_relaxed(val, pci->elbi_base + PCIEELBI_CTRL0_OFFSET);

	return 0;
}

static bool eswin_pcie_link_up(struct dw_pcie *pci)
{
	u16 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	u16 val = dw_pcie_readw_dbi(pci, offset + PCI_EXP_LNKSTA);

	return val & PCI_EXP_LNKSTA_DLLLA;
}

static int eswin_pcie_perst_reset(struct eswin_pcie_port *port,
				  struct eswin_pcie *pcie)
{
	int ret;

	ret = reset_control_assert(port->perst);
	if (ret) {
		dev_err(pcie->pci.dev, "Failed to assert PERST#\n");
		return ret;
	}

	/* Ensure that PERST# has been asserted for at least 100 ms */
	msleep(PCIE_T_PVPERL_MS);

	ret = reset_control_deassert(port->perst);
	if (ret) {
		dev_err(pcie->pci.dev, "Failed to deassert PERST#\n");
		return ret;
	}

	return 0;
}

static void eswin_pcie_assert(struct eswin_pcie *pcie)
{
	struct eswin_pcie_port *port;

	list_for_each_entry(port, &pcie->ports, list)
		reset_control_assert(port->perst);
	reset_control_bulk_assert(ESWIN_NUM_RSTS, pcie->resets);
}

static int eswin_pcie_parse_port(struct eswin_pcie *pcie,
				 struct device_node *node)
{
	struct device *dev = pcie->pci.dev;
	struct eswin_pcie_port *port;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->perst = of_reset_control_get_exclusive(node, "perst");
	if (IS_ERR(port->perst)) {
		dev_err(dev, "Failed to get PERST# reset\n");
		return PTR_ERR(port->perst);
	}

	/*
	 * TODO: Since the Root Port node is separated out by pcie devicetree,
	 * the DWC core initialization code can't parse the num-lanes attribute
	 * in the Root Port. Before entering the DWC core initialization code,
	 * the platform driver code parses the Root Port node. The ESWIN only
	 * supports one Root Port node, and the num-lanes attribute is suitable
	 * for the case of one Root Port.
	 */
	if (!of_property_read_u32(node, "num-lanes", &port->num_lanes))
		pcie->pci.num_lanes = port->num_lanes;

	INIT_LIST_HEAD(&port->list);
	list_add_tail(&port->list, &pcie->ports);

	return 0;
}

static int eswin_pcie_parse_ports(struct eswin_pcie *pcie)
{
	struct eswin_pcie_port *port, *tmp;
	struct device *dev = pcie->pci.dev;
	int ret;

	for_each_available_child_of_node_scoped(dev->of_node, of_port) {
		ret = eswin_pcie_parse_port(pcie, of_port);
		if (ret)
			goto err_port;
	}

	return 0;

err_port:
	list_for_each_entry_safe(port, tmp, &pcie->ports, list) {
		reset_control_put(port->perst);
		list_del(&port->list);
	}

	return ret;
}

static int eswin_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct eswin_pcie *pcie = to_eswin_pcie(pci);
	struct eswin_pcie_port *port, *tmp;
	u32 val;
	int ret;

	ret = clk_bulk_prepare_enable(pcie->num_clks, pcie->clks);
	if (ret)
		return ret;

	/*
	 * The PWR and DBI reset signals are respectively used to reset the
	 * PCIe controller and the DBI register.
	 *
	 * The PERST# signal is a reset signal that simultaneously controls the
	 * PCIe controller, PHY, and Endpoint. Before configuring the PHY, the
	 * PERST# signal must first be deasserted.
	 *
	 * The external reference clock is supplied simultaneously to the PHY
	 * and EP. When the PHY is configurable, the entire chip already has
	 * stable power and reference clock. The PHY will be ready within 20ms
	 * after writing app_hold_phy_rst register bit of ELBI register space.
	 */
	ret = reset_control_bulk_deassert(ESWIN_NUM_RSTS, pcie->resets);
	if (ret) {
		dev_err(pcie->pci.dev, "Failed to deassert resets\n");
		goto err_deassert;
	}

	/* Configure Root Port type */
	val = readl_relaxed(pci->elbi_base + PCIEELBI_CTRL0_OFFSET);
	val &= ~PCIEELBI_CTRL0_DEV_TYPE;
	val |= FIELD_PREP(PCIEELBI_CTRL0_DEV_TYPE, PCI_EXP_TYPE_ROOT_PORT);
	writel_relaxed(val, pci->elbi_base + PCIEELBI_CTRL0_OFFSET);

	list_for_each_entry(port, &pcie->ports, list) {
		ret = eswin_pcie_perst_reset(port, pcie);
		if (ret)
			goto err_perst;
	}

	/* Configure app_hold_phy_rst */
	val = readl_relaxed(pci->elbi_base + PCIEELBI_CTRL0_OFFSET);
	val &= ~PCIEELBI_APP_HOLD_PHY_RST;
	writel_relaxed(val, pci->elbi_base + PCIEELBI_CTRL0_OFFSET);

	/* The maximum waiting time for the clock switch lock is 20ms */
	ret = readl_poll_timeout(pci->elbi_base + PCIEELBI_STATUS0_OFFSET, val,
				 !(val & PCIEELBI_PM_SEL_AUX_CLK), 1000,
				 20000);
	if (ret) {
		dev_err(pci->dev, "Timeout waiting for PM_SEL_AUX_CLK ready\n");
		goto err_phy_init;
	}

	/*
	 * Configure ESWIN VID:DID for Root Port as the default values are
	 * invalid.
	 */
	dw_pcie_dbi_ro_wr_en(pci);
	dw_pcie_writew_dbi(pci, PCI_VENDOR_ID, PCI_VENDOR_ID_ESWIN);
	dw_pcie_writew_dbi(pci, PCI_DEVICE_ID, PCI_DEVICE_ID_ESWIN_EIC7700);
	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;

err_phy_init:
	list_for_each_entry(port, &pcie->ports, list)
		reset_control_assert(port->perst);
err_perst:
	reset_control_bulk_assert(ESWIN_NUM_RSTS, pcie->resets);
err_deassert:
	clk_bulk_disable_unprepare(pcie->num_clks, pcie->clks);
	list_for_each_entry_safe(port, tmp, &pcie->ports, list) {
		reset_control_put(port->perst);
		list_del(&port->list);
	}

	return ret;
}

static void eswin_pcie_host_deinit(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct eswin_pcie *pcie = to_eswin_pcie(pci);

	eswin_pcie_assert(pcie);
	clk_bulk_disable_unprepare(pcie->num_clks, pcie->clks);
}

static void eswin_pcie_pme_turn_off(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct eswin_pcie *pcie = to_eswin_pcie(pci);

	/*
	 * The ESWIN EIC7700 SoC lacks hardware support for the L2/L3 low-power
	 * link states. It cannot enter the L2/L3 Ready state through the
	 * PME_Turn_Off/PME_To_Ack handshake protocol. To avoid this problem,
	 * the skip_l23_ready has been set.
	 */
	pp->skip_l23_ready = pcie->data->skip_l23;
}

static const struct dw_pcie_host_ops eswin_pcie_host_ops = {
	.init = eswin_pcie_host_init,
	.deinit = eswin_pcie_host_deinit,
	.pme_turn_off = eswin_pcie_pme_turn_off,
};

static const struct dw_pcie_ops dw_pcie_ops = {
	.start_link = eswin_pcie_start_link,
	.link_up = eswin_pcie_link_up,
};

static int eswin_pcie_probe(struct platform_device *pdev)
{
	const struct eswin_pcie_data *data;
	struct eswin_pcie_port *port, *tmp;
	struct device *dev = &pdev->dev;
	struct eswin_pcie *pcie;
	struct dw_pcie *pci;
	int ret, i;

	data = of_device_get_match_data(dev);
	if (!data)
		return dev_err_probe(dev, -ENODATA, "No platform data\n");

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	INIT_LIST_HEAD(&pcie->ports);

	pci = &pcie->pci;
	pci->dev = dev;
	pci->ops = &dw_pcie_ops;
	pci->pp.ops = &eswin_pcie_host_ops;
	pcie->data = data;

	pcie->num_clks = devm_clk_bulk_get_all(dev, &pcie->clks);
	if (pcie->num_clks < 0)
		return dev_err_probe(dev, pcie->num_clks,
				     "Failed to get pcie clocks\n");

	for (i = 0; i < ESWIN_NUM_RSTS; i++)
		pcie->resets[i].id = eswin_pcie_rsts[i];

	ret = devm_reset_control_bulk_get_exclusive(dev, ESWIN_NUM_RSTS,
						    pcie->resets);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get resets\n");

	ret = eswin_pcie_parse_ports(pcie);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to parse Root Port\n");

	platform_set_drvdata(pdev, pcie);

	pm_runtime_no_callbacks(dev);
	devm_pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto err_pm_runtime_put;

	ret = dw_pcie_host_init(&pci->pp);
	if (ret) {
		dev_err(dev, "Failed to init host\n");
		goto err_init;
	}

	return 0;

err_pm_runtime_put:
	list_for_each_entry_safe(port, tmp, &pcie->ports, list) {
		reset_control_put(port->perst);
		list_del(&port->list);
	}
err_init:
	pm_runtime_put(dev);

	return ret;
}

static int eswin_pcie_suspend_noirq(struct device *dev)
{
	struct eswin_pcie *pcie = dev_get_drvdata(dev);

	return dw_pcie_suspend_noirq(&pcie->pci);
}

static int eswin_pcie_resume_noirq(struct device *dev)
{
	struct eswin_pcie *pcie = dev_get_drvdata(dev);

	return dw_pcie_resume_noirq(&pcie->pci);
}

static DEFINE_NOIRQ_DEV_PM_OPS(eswin_pcie_pm, eswin_pcie_suspend_noirq,
				eswin_pcie_resume_noirq);

static const struct eswin_pcie_data eswin_eic7700_data = {
	.skip_l23 = true,
};

static const struct of_device_id eswin_pcie_of_match[] = {
	{ .compatible = "eswin,eic7700-pcie", .data = &eswin_eic7700_data },
	{}
};

static struct platform_driver eswin_pcie_driver = {
	.probe = eswin_pcie_probe,
	.driver = {
		.name = "eswin-pcie",
		.of_match_table = eswin_pcie_of_match,
		.suppress_bind_attrs = true,
		.pm = &eswin_pcie_pm,
	},
};
builtin_platform_driver(eswin_pcie_driver);

MODULE_DESCRIPTION("ESWIN PCIe Root Complex driver");
MODULE_AUTHOR("Yu Ning <ningyu@eswincomputing.com>");
MODULE_AUTHOR("Senchuan Zhang <zhangsenchuan@eswincomputing.com>");
MODULE_AUTHOR("Yanghui Ou <ouyanghui@eswincomputing.com>");
MODULE_LICENSE("GPL");
