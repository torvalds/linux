// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCIe host controller driver for Tegra264 SoC
 *
 * Copyright (c) 2022-2026, NVIDIA CORPORATION. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci-ecam.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <soc/tegra/bpmp.h>
#include <soc/tegra/bpmp-abi.h>
#include <soc/tegra/fuse.h>

#include "../pci.h"

/* XAL registers */
#define XAL_RC_ECAM_BASE_HI			0x00
#define XAL_RC_ECAM_BASE_LO			0x04
#define XAL_RC_ECAM_BUSMASK			0x08
#define XAL_RC_IO_BASE_HI			0x0c
#define XAL_RC_IO_BASE_LO			0x10
#define XAL_RC_IO_LIMIT_HI			0x14
#define XAL_RC_IO_LIMIT_LO			0x18
#define XAL_RC_MEM_32BIT_BASE_HI		0x1c
#define XAL_RC_MEM_32BIT_BASE_LO		0x20
#define XAL_RC_MEM_32BIT_LIMIT_HI		0x24
#define XAL_RC_MEM_32BIT_LIMIT_LO		0x28
#define XAL_RC_MEM_64BIT_BASE_HI		0x2c
#define XAL_RC_MEM_64BIT_BASE_LO		0x30
#define XAL_RC_MEM_64BIT_LIMIT_HI		0x34
#define XAL_RC_MEM_64BIT_LIMIT_LO		0x38
#define XAL_RC_BAR_CNTL_STANDARD		0x40
#define XAL_RC_BAR_CNTL_STANDARD_IOBAR_EN	BIT(0)
#define XAL_RC_BAR_CNTL_STANDARD_32B_BAR_EN	BIT(1)
#define XAL_RC_BAR_CNTL_STANDARD_64B_BAR_EN	BIT(2)

/* XTL registers */
#define XTL_RC_PCIE_CFG_LINK_CAPS		0x56
#define XTL_RC_PCIE_CFG_LINK_STATUS		0x5a

#define XTL_RC_MGMT_PERST_CONTROL		0x218
#define XTL_RC_MGMT_PERST_CONTROL_PERST_O_N	BIT(0)

#define XTL_RC_MGMT_CLOCK_CONTROL		0x47c
#define XTL_RC_MGMT_CLOCK_CONTROL_PEX_CLKREQ_I_N_PIN_USE_CONV_TO_PRSNT	BIT(9)

struct tegra264_pcie {
	struct device *dev;

	/* I/O memory */
	void __iomem *xal;
	void __iomem *xtl;
	void __iomem *ecam;

	/* bridge configuration */
	struct pci_config_window *cfg;
	struct pci_host_bridge *bridge;

	/* BPMP and bandwidth management */
	struct icc_path *icc_path;
	struct tegra_bpmp *bpmp;
	u32 ctl_id;

	bool supports_hotplug;
	bool link_up;
};

static void tegra264_pcie_power_off(struct tegra264_pcie *pcie)
{
	struct tegra_bpmp_message msg = {};
	struct mrq_pcie_request req = {};
	int err;

	req.cmd = CMD_PCIE_RP_CONTROLLER_OFF;
	req.rp_ctrlr_off.rp_controller = pcie->ctl_id;

	msg.mrq = MRQ_PCIE;
	msg.tx.data = &req;
	msg.tx.size = sizeof(req);

	err = tegra_bpmp_transfer(pcie->bpmp, &msg);
	if (err)
		dev_err(pcie->dev, "failed to turn off PCIe #%u: %pe\n",
			pcie->ctl_id, ERR_PTR(err));

	if (msg.rx.ret)
		dev_err(pcie->dev, "failed to turn off PCIe #%u: %d\n",
			pcie->ctl_id, msg.rx.ret);
}

static void tegra264_pcie_icc_set(struct tegra264_pcie *pcie)
{
	u32 value, speed, width;
	int err;

	/*
	 * If the link is up, read the negotiated speed and width fields from
	 * the link status register. Otherwise, read the corresponding values
	 * from the link capabilities register to ensure the link works after
	 * hotplug.
	 *
	 * Ideally we'll want to update this dynamically, either on hotplug
	 * or bandwidth change notifications. Neither of those are currently
	 * possible, so this is as good as it gets for now.
	 */
	if (pcie->link_up) {
		value = readw(pcie->ecam + XTL_RC_PCIE_CFG_LINK_STATUS);
		speed = FIELD_GET(PCI_EXP_LNKSTA_CLS, value);
		width = FIELD_GET(PCI_EXP_LNKSTA_NLW, value);
	} else {
		value = readw(pcie->ecam + XTL_RC_PCIE_CFG_LINK_CAPS);
		speed = FIELD_GET(PCI_EXP_LNKCAP_SLS, value);
		width = FIELD_GET(PCI_EXP_LNKCAP_MLW, value);
	}

	value = Mbps_to_icc(width * PCIE_SPEED2MBS_ENC(pcie_link_speed[speed]));

	/*
	 * We don't want to error out here because a boot-critical device
	 * could be connected to this root port. Failure to set the bandwidth
	 * request may have an adverse impact on performance, but it is not
	 * generally fatal, so we opt to continue regardless so that users
	 * get a chance to fix things.
	 */
	err = icc_set_bw(pcie->icc_path, value, value);
	if (err < 0)
		dev_err(pcie->dev,
			"failed to request bandwidth (%u kBps): %pe\n",
			value, ERR_PTR(err));
}

/*
 * The various memory regions used by the controller (I/O, memory, ECAM) are
 * set up during early boot and have hardware-level protections in place. If
 * the DT ranges don't match what's been setup, the controller won't be able
 * to write the address endpoints properly, so make sure to validate that DT
 * and firmware programming agree on these ranges.
 */
static bool tegra264_pcie_valid_ranges(struct platform_device *pdev)
{
	struct tegra264_pcie *pcie = platform_get_drvdata(pdev);
	struct device_node *np = pcie->dev->of_node;
	struct of_pci_range_parser parser;
	phys_addr_t phys, limit, hi, lo;
	struct of_pci_range range;
	struct resource *res;
	bool status = true;
	u32 value;
	int err;

	err = of_pci_range_parser_init(&parser, np);
	if (err < 0)
		return false;

	for_each_of_pci_range(&parser, &range) {
		unsigned int addr_hi, addr_lo, limit_hi, limit_lo, enable;
		unsigned long type = range.flags & IORESOURCE_TYPE_BITS;
		phys_addr_t start, end, mask;
		const char *region = NULL;

		end = range.cpu_addr + range.size - 1;
		start = range.cpu_addr;

		switch (type) {
		case IORESOURCE_IO:
			addr_hi = XAL_RC_IO_BASE_HI;
			addr_lo = XAL_RC_IO_BASE_LO;
			limit_hi = XAL_RC_IO_LIMIT_HI;
			limit_lo = XAL_RC_IO_LIMIT_LO;
			enable = XAL_RC_BAR_CNTL_STANDARD_IOBAR_EN;
			mask = SZ_64K - 1;
			region = "I/O";
			break;

		case IORESOURCE_MEM:
			if (range.flags & IORESOURCE_PREFETCH) {
				addr_hi = XAL_RC_MEM_64BIT_BASE_HI;
				addr_lo = XAL_RC_MEM_64BIT_BASE_LO;
				limit_hi = XAL_RC_MEM_64BIT_LIMIT_HI;
				limit_lo = XAL_RC_MEM_64BIT_LIMIT_LO;
				enable = XAL_RC_BAR_CNTL_STANDARD_64B_BAR_EN;
				region = "prefetchable memory";
			} else {
				addr_hi = XAL_RC_MEM_32BIT_BASE_HI;
				addr_lo = XAL_RC_MEM_32BIT_BASE_LO;
				limit_hi = XAL_RC_MEM_32BIT_LIMIT_HI;
				limit_lo = XAL_RC_MEM_32BIT_LIMIT_LO;
				enable = XAL_RC_BAR_CNTL_STANDARD_32B_BAR_EN;
				region = "memory";
			}

			mask = SZ_1M - 1;
			break;
		}

		/* not interested in anything that's not I/O or memory */
		if (!region)
			continue;

		/* don't check regions that haven't been enabled */
		value = readl(pcie->xal + XAL_RC_BAR_CNTL_STANDARD);
		if ((value & enable) == 0)
			continue;

		hi = readl(pcie->xal + addr_hi);
		lo = readl(pcie->xal + addr_lo);
		phys = ((hi << 16) << 16) | lo;

		hi = readl(pcie->xal + limit_hi);
		lo = readl(pcie->xal + limit_lo);
		limit = ((hi << 16) << 16) | lo | mask;

		if (phys != start || limit != end) {
			dev_err(pcie->dev,
				"%s region mismatch: %pap-%pap -> %pap-%pap\n",
				region, &phys, &limit, &start, &end);
			status = false;
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ecam");
	if (!res)
		return false;

	hi = readl(pcie->xal + XAL_RC_ECAM_BASE_HI);
	lo = readl(pcie->xal + XAL_RC_ECAM_BASE_LO);
	phys = ((hi << 16) << 16) | lo;

	value = readl(pcie->xal + XAL_RC_ECAM_BUSMASK);
	limit = phys + ((value + 1) << 20) - 1;

	if (phys != res->start || limit != res->end) {
		dev_err(pcie->dev,
			"ECAM region mismatch: %pap-%pap -> %pap-%pap\n",
			&phys, &limit, &res->start, &res->end);
		status = false;
	}

	return status;
}

static bool tegra264_pcie_supports_hotplug(struct tegra264_pcie *pcie)
{
	u32 value = readl(pcie->xtl + XTL_RC_MGMT_CLOCK_CONTROL);

	return (value & XTL_RC_MGMT_CLOCK_CONTROL_PEX_CLKREQ_I_N_PIN_USE_CONV_TO_PRSNT) != 0;
}

static bool tegra264_pcie_link_up(struct tegra264_pcie *pcie,
				  enum pci_bus_speed *speed)
{
	u16 value = readw(pcie->ecam + XTL_RC_PCIE_CFG_LINK_STATUS);

	if (value & PCI_EXP_LNKSTA_DLLLA) {
		if (speed)
			*speed = pcie_link_speed[FIELD_GET(PCI_EXP_LNKSTA_CLS,
							   value)];

		return true;
	}

	return false;
}

static void tegra264_pcie_init(struct tegra264_pcie *pcie)
{
	enum pci_bus_speed speed;
	unsigned int i;
	u32 value;

	/* bring the endpoint out of reset */
	value = readl(pcie->xtl + XTL_RC_MGMT_PERST_CONTROL);
	value |= XTL_RC_MGMT_PERST_CONTROL_PERST_O_N;
	writel(value, pcie->xtl + XTL_RC_MGMT_PERST_CONTROL);

	for (i = 0; i < PCIE_LINK_WAIT_MAX_RETRIES; i++) {
		if (tegra264_pcie_link_up(pcie, NULL))
			break;

		msleep(PCIE_LINK_WAIT_SLEEP_MS);
	}

	pcie->supports_hotplug = tegra264_pcie_supports_hotplug(pcie);
	pcie->link_up = tegra264_pcie_link_up(pcie, &speed);

	if (pcie->link_up) {
		msleep(PCIE_RESET_CONFIG_WAIT_MS);
		dev_info(pcie->dev, "PCIe #%u link is up (speed: %s)\n",
			 pcie->ctl_id, pci_speed_string(speed));
		tegra264_pcie_icc_set(pcie);
	} else {
		dev_info(pcie->dev, "PCIe #%u link is down\n", pcie->ctl_id);

		/*
		 * Make sure to reset the bandwidth requirements if the link
		 * is down but hotplug-capable.
		 */
		if (pcie->supports_hotplug)
			tegra264_pcie_icc_set(pcie);
	}
}

static int tegra264_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pci_host_bridge *bridge;
	struct tegra264_pcie *pcie;
	struct resource_entry *bus;
	struct resource *res;
	int err;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(struct tegra264_pcie));
	if (!bridge)
		return dev_err_probe(dev, -ENOMEM,
				     "failed to allocate host bridge\n");

	pcie = pci_host_bridge_priv(bridge);
	platform_set_drvdata(pdev, pcie);
	pcie->bridge = bridge;
	pcie->dev = dev;

	pcie->xal = devm_platform_ioremap_resource_byname(pdev, "xal");
	if (IS_ERR(pcie->xal))
		return dev_err_probe(dev, PTR_ERR(pcie->xal),
				     "failed to map XAL memory\n");

	pcie->xtl = devm_platform_ioremap_resource_byname(pdev, "xtl-pri");
	if (IS_ERR(pcie->xtl))
		return dev_err_probe(dev, PTR_ERR(pcie->xtl),
				     "failed to map XTL-PRI memory\n");

	bus = resource_list_first_type(&bridge->windows, IORESOURCE_BUS);
	if (!bus)
		return dev_err_probe(dev, -ENODEV,
				     "failed to get bus resources\n");

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ecam");
	if (!res)
		return dev_err_probe(dev, -ENXIO,
				     "failed to get ECAM resource\n");

	pcie->icc_path = devm_of_icc_get(dev, "write");
	if (IS_ERR(pcie->icc_path))
		return dev_err_probe(dev, PTR_ERR(pcie->icc_path),
				     "failed to get ICC\n");

	pcie->bpmp = tegra_bpmp_get_with_id(dev, &pcie->ctl_id);
	if (IS_ERR(pcie->bpmp))
		return dev_err_probe(dev, PTR_ERR(pcie->bpmp),
				     "failed to get BPMP\n");

	err = devm_pm_runtime_set_active_enabled(dev);
	if (err < 0) {
		dev_err_probe(dev, err, "failed to enable runtime PM\n");
		goto err_put_bpmp;
	}

	err = pm_runtime_resume_and_get(dev);
	if (err < 0) {
		dev_err_probe(dev, err, "failed to power on device\n");
		goto err_put_bpmp;
	}

	/* sanity check that programmed ranges match what's in DT */
	if (!tegra264_pcie_valid_ranges(pdev)) {
		err = -EINVAL;
		goto err_put_pm;
	}

	pcie->cfg = pci_ecam_create(dev, res, bus->res, &pci_generic_ecam_ops);
	if (IS_ERR(pcie->cfg)) {
		err = dev_err_probe(dev, PTR_ERR(pcie->cfg),
				    "failed to create ECAM\n");
		goto err_put_pm;
	}

	bridge->ops = (struct pci_ops *)&pci_generic_ecam_ops.pci_ops;
	bridge->sysdata = pcie->cfg;
	pcie->ecam = pcie->cfg->win;

	tegra264_pcie_init(pcie);

	/*
	 * Fail if the link isn't up and doesn't support hotplug, no device
	 * will ever be able to be added on this bus.
	 */
	if (!pcie->link_up && !pcie->supports_hotplug) {
		err = dev_err_probe(pcie->dev, -ENODEV,
				    "PCIe #%u link is down and not hotplug-capable, turning off\n",
				    pcie->ctl_id);
		tegra264_pcie_power_off(pcie);
		goto err_free_ecam;
	}

	err = pci_host_probe(bridge);
	if (err < 0) {
		dev_err_probe(dev, err, "failed to register Host Bridge\n");
		goto err_free_ecam;
	}

	return 0;

err_free_ecam:
	pci_ecam_free(pcie->cfg);
err_put_pm:
	pm_runtime_put_sync(dev);
err_put_bpmp:
	tegra_bpmp_put(pcie->bpmp);

	return err;
}

static void tegra264_pcie_remove(struct platform_device *pdev)
{
	struct tegra264_pcie *pcie = platform_get_drvdata(pdev);

	/*
	 * If we undo tegra264_pcie_init() then link goes down and need
	 * controller reset to bring up the link again. Remove intention is
	 * to clean up the root bridge and re-enumerate during bind.
	 */
	pci_lock_rescan_remove();
	pci_stop_root_bus(pcie->bridge->bus);
	pci_remove_root_bus(pcie->bridge->bus);
	pci_unlock_rescan_remove();

	pm_runtime_put_sync(&pdev->dev);
	tegra_bpmp_put(pcie->bpmp);
	pci_ecam_free(pcie->cfg);
}

static const struct of_device_id tegra264_pcie_of_match[] = {
	{
		.compatible = "nvidia,tegra264-pcie",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tegra264_pcie_of_match);

static struct platform_driver tegra264_pcie_driver = {
	.probe = tegra264_pcie_probe,
	.remove = tegra264_pcie_remove,
	.driver = {
		.name = "tegra264-pcie",
		.of_match_table = tegra264_pcie_of_match,
	},
};
module_platform_driver(tegra264_pcie_driver);

MODULE_AUTHOR("Manikanta Maddireddy <mmaddireddy@nvidia.com>");
MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra264 PCIe host controller driver");
MODULE_LICENSE("GPL");
