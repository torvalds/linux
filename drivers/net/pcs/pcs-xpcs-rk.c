// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip XPCS platform device driver
 *
 * Based on the Synopsys DesignWare XPCS platform driver.
 * Copyright (C) 2024 Serge Semin
 *
 * Adapted for Rockchip SoCs, with reference to the Rockchip OEM driver.
 * Copyright (C) 2026 Coia Prant
 */

#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/math.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pcs/pcs-xpcs-rk.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/sizes.h>
#include <linux/time.h>

#include "pcs-xpcs.h"

struct dw_xpcs_rk {
	struct platform_device *pdev;
	struct mii_bus *bus;
	void __iomem *reg_base;
	struct phy *serdes_phy;
	struct clk *csr_clk;
	struct clk *eee_clk;
	u8 eee_mult_fact;
};

static ptrdiff_t xpcs_rk_addr_format(int dev, int reg)
{
	return FIELD_PREP(0x70000, dev) | FIELD_PREP(0xffff, reg);
}

static int xpcs_rk_read_reg(struct dw_xpcs_rk *pxpcs, int dev, int reg)
{
	ptrdiff_t csr;
	int ret;

	csr = xpcs_rk_addr_format(dev, reg);

	ret = pm_runtime_resume_and_get(&pxpcs->pdev->dev);
	if (ret)
		return ret;

	ret = readl(pxpcs->reg_base + (csr << 2)) & 0xffff;

	pm_runtime_put(&pxpcs->pdev->dev);
	return ret;
}

static int xpcs_rk_write_reg(struct dw_xpcs_rk *pxpcs, int dev, int reg, u16 val)
{
	ptrdiff_t csr;
	int ret;

	csr = xpcs_rk_addr_format(dev, reg);

	ret = pm_runtime_resume_and_get(&pxpcs->pdev->dev);
	if (ret)
		return ret;

	writel(val, pxpcs->reg_base + (csr << 2));

	pm_runtime_put(&pxpcs->pdev->dev);
	return 0;
}

#define ROCKCHIP_MMD_MII1	2
#define ROCKCHIP_MMD_MII2	3
#define ROCKCHIP_MMD_MII3	4
#define ROCKCHIP_MMD_PMAPMD	6
#define ROCKCHIP_MMD_MII	7

static bool xpcs_rk_mdio_addr_validate(int addr)
{
	return !(addr < 0 || addr > 3);
}

static int xpcs_rk_mdio_read_remapping(int addr, int dev)
{
	switch (dev) {
	case MDIO_MMD_PMAPMD:
		return ROCKCHIP_MMD_PMAPMD;
	case MDIO_MMD_VEND2:
		break;
	default:
		return -ENXIO;
	}

	/*
	 * Reads are redirected by hardware to the port's read-only mirror;
	 * only writes have to be targeted at MII (see the write path).
	 */
	switch (addr) {
	case 0:
		return ROCKCHIP_MMD_MII;
	case 1:
		return ROCKCHIP_MMD_MII1;
	case 2:
		return ROCKCHIP_MMD_MII2;
	case 3:
		return ROCKCHIP_MMD_MII3;
	default:
		return -ENODEV;
	}
}

static int xpcs_rk_mdio_write_remapping(int addr, int dev, int reg)
{
	switch (dev) {
	case MDIO_MMD_PMAPMD:
		return ROCKCHIP_MMD_PMAPMD;
	case MDIO_MMD_VEND2:
		break;
	default:
		return -ENXIO;
	}

	/*
	 * These registers physically live only in MII (the management port).
	 * Ports 1-3 expose read-only mirrors of these bits, so writes must
	 * always target MII; the read path remaps per address and the
	 * hardware redirects to the port's mirror.
	 */
	switch (reg) {
	case DW_VR_MII_AN_CTRL:
	case DW_VR_MII_AN_INTR_STS:
	case DW_VR_MII_EEE_MCTRL0:
	case DW_VR_MII_EEE_MCTRL1:
	case DW_VR_MII_DIG_CTRL2:
		return ROCKCHIP_MMD_MII;
	default:
		break;
	}

	switch (addr) {
	case 0:
		return ROCKCHIP_MMD_MII;
	case 1:
		return ROCKCHIP_MMD_MII1;
	case 2:
		return ROCKCHIP_MMD_MII2;
	case 3:
		return ROCKCHIP_MMD_MII3;
	default:
		return -ENODEV;
	}
}

static int xpcs_rk_read_c22(struct mii_bus *bus, int addr, int reg)
{
	struct dw_xpcs_rk *pxpcs = bus->priv;
	int dev;

	if (!xpcs_rk_mdio_addr_validate(addr))
		return -ENODEV;

	dev = xpcs_rk_mdio_read_remapping(addr, MDIO_MMD_VEND2);
	if (dev < 0)
		return 0xffff;

	return xpcs_rk_read_reg(pxpcs, dev, reg);
}

static int xpcs_rk_write_c22(struct mii_bus *bus, int addr, int reg, u16 val)
{
	struct dw_xpcs_rk *pxpcs = bus->priv;
	int dev;

	if (!xpcs_rk_mdio_addr_validate(addr))
		return -ENODEV;

	dev = xpcs_rk_mdio_write_remapping(addr, MDIO_MMD_VEND2, reg);
	if (dev < 0)
		return 0;

	return xpcs_rk_write_reg(pxpcs, dev, reg, val);
}

static int xpcs_rk_read_c45(struct mii_bus *bus, int addr, int dev, int reg)
{
	struct dw_xpcs_rk *pxpcs = bus->priv;

	if (!xpcs_rk_mdio_addr_validate(addr))
		return -ENODEV;

	dev = xpcs_rk_mdio_read_remapping(addr, dev);
	if (dev < 0)
		return 0xffff;

	return xpcs_rk_read_reg(pxpcs, dev, reg);
}

static int xpcs_rk_write_c45(struct mii_bus *bus, int addr, int dev, int reg, u16 val)
{
	struct dw_xpcs_rk *pxpcs = bus->priv;

	if (!xpcs_rk_mdio_addr_validate(addr))
		return -ENODEV;

	dev = xpcs_rk_mdio_write_remapping(addr, dev, reg);
	if (dev < 0)
		return 0;

	return xpcs_rk_write_reg(pxpcs, dev, reg, val);
}

static struct dw_xpcs_rk *xpcs_rk_create_data(struct platform_device *pdev)
{
	struct dw_xpcs_rk *pxpcs;

	pxpcs = devm_kzalloc(&pdev->dev, sizeof(*pxpcs), GFP_KERNEL);
	if (!pxpcs)
		return ERR_PTR(-ENOMEM);

	pxpcs->pdev = pdev;

	dev_set_drvdata(&pdev->dev, pxpcs);

	return pxpcs;
}

static int xpcs_rk_serdes_phy_init(struct dw_xpcs_rk *pxpcs)
{
	struct device *dev = &pxpcs->pdev->dev;

	pxpcs->serdes_phy = devm_phy_get(dev, NULL);
	if (IS_ERR(pxpcs->serdes_phy))
		return dev_err_probe(dev, PTR_ERR(pxpcs->serdes_phy),
					"Failed to get SerDes PHY\n");

	return 0;
}

static void xpcs_rk_serdes_phy_poweroff(void *data)
{
	struct dw_xpcs_rk *pxpcs = data;
	struct device *dev = &pxpcs->pdev->dev;

	phy_power_off(pxpcs->serdes_phy);
	phy_exit(pxpcs->serdes_phy);

	dev_pm_genpd_rpm_always_on(dev, false);
}

static int xpcs_rk_serdes_phy_poweron(struct dw_xpcs_rk *pxpcs)
{
	struct device *dev = &pxpcs->pdev->dev;
	int ret;

	/*
	 * The power domain is required and must be enabled, which allows us to
	 * dynamically turn the CSR clock on/off using PM while keeping the PCS
	 * powered on.
	 */
	ret = dev_pm_genpd_rpm_always_on(dev, true);
	if (ret) {
		dev_err(dev, "Failed to power on power-domains\n");
		return ret;
	}

	ret = phy_init(pxpcs->serdes_phy);
	if (ret) {
		dev_err(dev, "Failed to init SerDes PHY\n");
		goto pm_domain;
	}

	ret = phy_power_on(pxpcs->serdes_phy);
	if (ret) {
		dev_err(dev, "Failed to power on SerDes PHY\n");
		goto serdes_phy;
	}

	ret = devm_add_action_or_reset(dev, xpcs_rk_serdes_phy_poweroff, pxpcs);
	if (ret) {
		dev_err(dev, "Failed to register devm for SerDes PHY: %d\n", ret);
		return ret;
	}

	return 0;

serdes_phy:
	phy_exit(pxpcs->serdes_phy);
pm_domain:
	dev_pm_genpd_rpm_always_on(dev, false);
	return ret;
}

static int xpcs_rk_init_res(struct dw_xpcs_rk *pxpcs)
{
	struct platform_device *pdev = pxpcs->pdev;
	struct device *dev = &pdev->dev;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "No reg-space found\n");
		return -EINVAL;
	}

	if (resource_size(res) < SZ_2M) {
		dev_err(dev, "Invalid reg-space size\n");
		return -EINVAL;
	}

	pxpcs->reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pxpcs->reg_base)) {
		dev_err(dev, "Failed to map reg-space\n");
		return PTR_ERR(pxpcs->reg_base);
	}

	return 0;
}

static void xpcs_rk_exit_clk(void *data)
{
	struct dw_xpcs_rk *pxpcs = data;
	struct device *dev = &pxpcs->pdev->dev;

	pm_runtime_force_suspend(dev);
	clk_disable_unprepare(pxpcs->eee_clk);
}

static int xpcs_rk_init_clk(struct dw_xpcs_rk *pxpcs)
{
	struct device *dev = &pxpcs->pdev->dev;
	unsigned long rate;
	u64 mult;
	int ret;

	pxpcs->csr_clk = devm_clk_get(dev, "csr");
	if (IS_ERR(pxpcs->csr_clk))
		return dev_err_probe(dev, PTR_ERR(pxpcs->csr_clk),
					 "Failed to get CSR clock\n");

	pxpcs->eee_clk = devm_clk_get(dev, "eee");
	if (IS_ERR(pxpcs->eee_clk))
		return dev_err_probe(dev, PTR_ERR(pxpcs->eee_clk),
					 "Failed to get EEE clock\n");

	ret = clk_prepare_enable(pxpcs->eee_clk);
	if (ret) {
		dev_err(dev, "Failed to enable EEE clock\n");
		return ret;
	}

	pm_runtime_set_suspended(dev);
	pm_runtime_enable(dev);

	ret = devm_add_action_or_reset(dev, xpcs_rk_exit_clk, pxpcs);
	if (ret) {
		dev_err(dev, "Failed to register devm for EEE clock: %d\n", ret);
		return ret;
	}

	/*
	 * Compute the multiplier for the EEE clock so that
	 * clk_eee_period * (mult_fact + 1) falls within 80..120 ns.
	 *
	 * On RK3568, clk_xpcs_eee is muxed between gpll200 (200 MHz, 5 ns)
	 * and cpll125 (125 MHz, 8 ns), selected by CRU_CLKSEL_CON29 bit 13.
	 * The reset value is 0 (200 MHz), but derive the value at runtime to
	 * stay correct if the mux is changed by a board.
	 *
	 * Use a 64-bit intermediate: on 32-bit builds, 100 * 200000000
	 * does not fit in unsigned long. Clamp to the 4-bit
	 * DW_VR_MII_EEE_MULT_FACT_100NS field. The mux only provides
	 * 125 MHz or 200 MHz, so the rate cannot drop below the 5 MHz
	 * threshold where DIV_ROUND_CLOSEST_ULL() would return 0 and the
	 * subtraction below would underflow.
	 */
	rate = clk_get_rate(pxpcs->eee_clk);
	if (!rate)
		return dev_err_probe(dev, -EINVAL, "Invalid EEE clock rate\n");

	mult = DIV_ROUND_CLOSEST_ULL(100ULL * rate, NSEC_PER_SEC) - 1;
	pxpcs->eee_mult_fact = min_t(u64, mult, 15);
	return 0;
}

static int xpcs_rk_init_bus(struct dw_xpcs_rk *pxpcs)
{
	struct device *dev = &pxpcs->pdev->dev;
	static atomic_t id = ATOMIC_INIT(-1);
	struct mii_bus *bus;
	int ret;

	bus = devm_mdiobus_alloc_size(dev, 0);
	if (!bus)
		return -ENOMEM;

	bus->name = "Rockchip DW XPCS MCI/APB3";
	bus->read = xpcs_rk_read_c22;
	bus->write = xpcs_rk_write_c22;
	bus->read_c45 = xpcs_rk_read_c45;
	bus->write_c45 = xpcs_rk_write_c45;
	bus->phy_mask = ~0;
	bus->parent = dev;
	bus->priv = pxpcs;

	snprintf(bus->id, MII_BUS_ID_SIZE,
		 "rockchip_dwxpcs-%x", atomic_inc_return(&id));

	/*
	 * MDIO-bus here serves as just a back-end engine abstracting out
	 * the MDIO and MCI/APB3 IO interfaces utilized for the Rockchip DWXPCS CSRs
	 * access.
	 */
	ret = devm_mdiobus_register(dev, bus);
	if (ret) {
		dev_err(dev, "Failed to create MDIO bus\n");
		return ret;
	}

	pxpcs->bus = bus;
	return 0;
}

static int xpcs_rk_probe(struct platform_device *pdev)
{
	struct dw_xpcs_rk *pxpcs;
	int ret;

	pxpcs = xpcs_rk_create_data(pdev);
	if (IS_ERR(pxpcs))
		return PTR_ERR(pxpcs);

	/*
	 * The XPCS lives in the PD_PIPE power domain. The domain must be
	 * powered on before any register access, otherwise the SoC will
	 * trigger a synchronous external abort (SError).
	 *
	 * Accessing the XPCS registers also requires a TX clock from the
	 * SerDes, which is needed for the soft reset.
	 */
	ret = xpcs_rk_serdes_phy_init(pxpcs);
	if (ret)
		return ret;

	ret = xpcs_rk_serdes_phy_poweron(pxpcs);
	if (ret)
		return ret;

	ret = xpcs_rk_init_res(pxpcs);
	if (ret)
		return ret;

	ret = xpcs_rk_init_clk(pxpcs);
	if (ret)
		return ret;

	ret = xpcs_rk_init_bus(pxpcs);
	if (ret)
		return ret;

	return 0;
}

static const struct of_device_id xpcs_rk_of_ids[] = {
	{ .compatible = "rockchip,rk3568-xpcs" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, xpcs_rk_of_ids);

struct dw_xpcs *xpcs_rk_create(struct device *dev, struct device_node *pcs, int port)
{
	struct platform_device *pdev;
	struct dw_xpcs_rk *pxpcs;
	struct dw_xpcs *xpcs;

	if (!of_device_is_available(pcs))
		return ERR_PTR(-ENODEV);

	if (!of_match_node(xpcs_rk_of_ids, pcs))
		return ERR_PTR(-EINVAL);

	if (!xpcs_rk_mdio_addr_validate(port))
		return ERR_PTR(-EINVAL);

	pdev = of_find_device_by_node(pcs);
	if (!pdev)
		return ERR_PTR(-EPROBE_DEFER);

	/*
	 * Establish the device link before reading the supplier's drvdata.
	 * device_link_add() does not fail on a supplier that is unbinding;
	 * it creates the link in DL_STATE_SUPPLIER_UNBIND.
	 *
	 * The check below only confirms that the supplier is bound at this
	 * point. It does not fully protect against a concurrent unbind
	 * between here and xpcs_create_mdiodev(); closing that window would
	 * require reading a managed link's status or device_lock(), neither
	 * of which is acceptable on this path. The window is small and
	 * requires an explicit unbind from userspace. Discussed on
	 * driver-core.
	 *
	 * The link is released automatically when the consumer device is
	 * destroyed (DL_FLAG_AUTOREMOVE_CONSUMER), so no explicit
	 * device_link_remove() is needed on the failure paths.
	 */
	if (!device_link_add(dev, &pdev->dev, DL_FLAG_AUTOREMOVE_CONSUMER)) {
		put_device(&pdev->dev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	if (READ_ONCE(pdev->dev.links.status) != DL_DEV_DRIVER_BOUND) {
		put_device(&pdev->dev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	pxpcs = platform_get_drvdata(pdev);
	if (!pxpcs || !pxpcs->bus) {
		put_device(&pdev->dev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	xpcs = xpcs_create_mdiodev(pxpcs->bus, port);
	if (IS_ERR(xpcs)) {
		put_device(&pdev->dev);
		return xpcs;
	}

	xpcs_config_eee_mult_fact(xpcs, pxpcs->eee_mult_fact);
	put_device(&pdev->dev);
	return xpcs;
}
EXPORT_SYMBOL_GPL(xpcs_rk_create);

static int xpcs_rk_pm_runtime_suspend(struct device *dev)
{
	struct dw_xpcs_rk *pxpcs = dev_get_drvdata(dev);

	clk_disable_unprepare(pxpcs->csr_clk);

	return 0;
}

static int xpcs_rk_pm_runtime_resume(struct device *dev)
{
	struct dw_xpcs_rk *pxpcs = dev_get_drvdata(dev);

	return clk_prepare_enable(pxpcs->csr_clk);
}

static int xpcs_rk_system_suspend(struct device *dev)
{
	/*
	 * Keep the PD_PIPE power domain on during system suspend.
	 *
	 * PD_PIPE is shared with SATA/PCIe and would be powered down by
	 * genpd once all its consumers are suspended, killing the SerDes
	 * and breaking MAC WoL.  Mark the XPCS as part of the wakeup path
	 * so genpd keeps the domain on.  Unconditional because the XPCS
	 * core has no callback to convey the MAC WoL state.
	 */
	device_set_wakeup_path(dev);
	return 0;
}

static int xpcs_rk_system_resume(struct device *dev)
{
	return 0;
}

static _DEFINE_DEV_PM_OPS(xpcs_rk_pm_ops,
			  xpcs_rk_system_suspend, xpcs_rk_system_resume,
			  xpcs_rk_pm_runtime_suspend, xpcs_rk_pm_runtime_resume,
			  NULL);

static struct platform_driver xpcs_rk_driver = {
	.probe = xpcs_rk_probe,
	.driver = {
		.name = "rk_xpcs-dwxpcs",
		.pm = pm_ptr(&xpcs_rk_pm_ops),
		.of_match_table = xpcs_rk_of_ids,
	},
};
module_platform_driver(xpcs_rk_driver);

MODULE_DESCRIPTION("Rockchip XPCS platform device driver");
MODULE_AUTHOR("Coia Prant <coiaprant@gmail.com>");
MODULE_LICENSE("GPL");
