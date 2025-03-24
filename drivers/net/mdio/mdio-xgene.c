// SPDX-License-Identifier: GPL-2.0+
/* Applied Micro X-Gene SoC MDIO Driver
 *
 * Copyright (c) 2016, Applied Micro Circuits Corporation
 * Author: Iyappan Subramanian <isubramanian@apm.com>
 */

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/efi.h>
#include <linux/if_vlan.h>
#include <linux/io.h>
#include <linux/mdio/mdio-xgene.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/prefetch.h>
#include <linux/property.h>
#include <net/ip.h>

u32 xgene_mdio_rd_mac(struct xgene_mdio_pdata *pdata, u32 rd_addr)
{
	void __iomem *addr, *rd, *cmd, *cmd_done;
	u32 done, rd_data = BUSY_MASK;
	u8 wait = 10;

	addr = pdata->mac_csr_addr + MAC_ADDR_REG_OFFSET;
	rd = pdata->mac_csr_addr + MAC_READ_REG_OFFSET;
	cmd = pdata->mac_csr_addr + MAC_COMMAND_REG_OFFSET;
	cmd_done = pdata->mac_csr_addr + MAC_COMMAND_DONE_REG_OFFSET;

	spin_lock(&pdata->mac_lock);
	iowrite32(rd_addr, addr);
	iowrite32(XGENE_ENET_RD_CMD, cmd);

	while (!(done = ioread32(cmd_done)) && wait--)
		udelay(1);

	if (done)
		rd_data = ioread32(rd);

	iowrite32(0, cmd);
	spin_unlock(&pdata->mac_lock);

	return rd_data;
}
EXPORT_SYMBOL(xgene_mdio_rd_mac);

void xgene_mdio_wr_mac(struct xgene_mdio_pdata *pdata, u32 wr_addr, u32 data)
{
	void __iomem *addr, *wr, *cmd, *cmd_done;
	u8 wait = 10;
	u32 done;

	addr = pdata->mac_csr_addr + MAC_ADDR_REG_OFFSET;
	wr = pdata->mac_csr_addr + MAC_WRITE_REG_OFFSET;
	cmd = pdata->mac_csr_addr + MAC_COMMAND_REG_OFFSET;
	cmd_done = pdata->mac_csr_addr + MAC_COMMAND_DONE_REG_OFFSET;

	spin_lock(&pdata->mac_lock);
	iowrite32(wr_addr, addr);
	iowrite32(data, wr);
	iowrite32(XGENE_ENET_WR_CMD, cmd);

	while (!(done = ioread32(cmd_done)) && wait--)
		udelay(1);

	if (!done)
		pr_err("MCX mac write failed, addr: 0x%04x\n", wr_addr);

	iowrite32(0, cmd);
	spin_unlock(&pdata->mac_lock);
}
EXPORT_SYMBOL(xgene_mdio_wr_mac);

int xgene_mdio_rgmii_read(struct mii_bus *bus, int phy_id, int reg)
{
	struct xgene_mdio_pdata *pdata = bus->priv;
	u32 data, done;
	u8 wait = 10;

	data = SET_VAL(PHY_ADDR, phy_id) | SET_VAL(REG_ADDR, reg);
	xgene_mdio_wr_mac(pdata, MII_MGMT_ADDRESS_ADDR, data);
	xgene_mdio_wr_mac(pdata, MII_MGMT_COMMAND_ADDR, READ_CYCLE_MASK);
	do {
		usleep_range(5, 10);
		done = xgene_mdio_rd_mac(pdata, MII_MGMT_INDICATORS_ADDR);
	} while ((done & BUSY_MASK) && wait--);

	if (done & BUSY_MASK) {
		dev_err(&bus->dev, "MII_MGMT read failed\n");
		return -EBUSY;
	}

	data = xgene_mdio_rd_mac(pdata, MII_MGMT_STATUS_ADDR);
	xgene_mdio_wr_mac(pdata, MII_MGMT_COMMAND_ADDR, 0);

	return data;
}
EXPORT_SYMBOL(xgene_mdio_rgmii_read);

int xgene_mdio_rgmii_write(struct mii_bus *bus, int phy_id, int reg, u16 data)
{
	struct xgene_mdio_pdata *pdata = bus->priv;
	u32 val, done;
	u8 wait = 10;

	val = SET_VAL(PHY_ADDR, phy_id) | SET_VAL(REG_ADDR, reg);
	xgene_mdio_wr_mac(pdata, MII_MGMT_ADDRESS_ADDR, val);

	xgene_mdio_wr_mac(pdata, MII_MGMT_CONTROL_ADDR, data);
	do {
		usleep_range(5, 10);
		done = xgene_mdio_rd_mac(pdata, MII_MGMT_INDICATORS_ADDR);
	} while ((done & BUSY_MASK) && wait--);

	if (done & BUSY_MASK) {
		dev_err(&bus->dev, "MII_MGMT write failed\n");
		return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL(xgene_mdio_rgmii_write);

static u32 xgene_menet_rd_diag_csr(struct xgene_mdio_pdata *pdata, u32 offset)
{
	return ioread32(pdata->diag_csr_addr + offset);
}

static void xgene_menet_wr_diag_csr(struct xgene_mdio_pdata *pdata,
				    u32 offset, u32 val)
{
	iowrite32(val, pdata->diag_csr_addr + offset);
}

static int xgene_enet_ecc_init(struct xgene_mdio_pdata *pdata)
{
	u32 data;
	u8 wait = 10;

	xgene_menet_wr_diag_csr(pdata, MENET_CFG_MEM_RAM_SHUTDOWN_ADDR, 0x0);
	do {
		usleep_range(100, 110);
		data = xgene_menet_rd_diag_csr(pdata, MENET_BLOCK_MEM_RDY_ADDR);
	} while ((data != 0xffffffff) && wait--);

	if (data != 0xffffffff) {
		dev_err(pdata->dev, "Failed to release memory from shutdown\n");
		return -ENODEV;
	}

	return 0;
}

static void xgene_gmac_reset(struct xgene_mdio_pdata *pdata)
{
	xgene_mdio_wr_mac(pdata, MAC_CONFIG_1_ADDR, SOFT_RESET);
	xgene_mdio_wr_mac(pdata, MAC_CONFIG_1_ADDR, 0);
}

static int xgene_mdio_reset(struct xgene_mdio_pdata *pdata)
{
	int ret;

	if (pdata->dev->of_node) {
		clk_prepare_enable(pdata->clk);
		udelay(5);
		clk_disable_unprepare(pdata->clk);
		udelay(5);
		clk_prepare_enable(pdata->clk);
		udelay(5);
	} else {
#ifdef CONFIG_ACPI
		acpi_evaluate_object(ACPI_HANDLE(pdata->dev),
				     "_RST", NULL, NULL);
#endif
	}

	ret = xgene_enet_ecc_init(pdata);
	if (ret) {
		if (pdata->dev->of_node)
			clk_disable_unprepare(pdata->clk);
		return ret;
	}
	xgene_gmac_reset(pdata);

	return 0;
}

static void xgene_enet_rd_mdio_csr(void __iomem *base_addr,
				   u32 offset, u32 *val)
{
	void __iomem *addr = base_addr  + offset;

	*val = ioread32(addr);
}

static void xgene_enet_wr_mdio_csr(void __iomem *base_addr,
				   u32 offset, u32 val)
{
	void __iomem *addr = base_addr  + offset;

	iowrite32(val, addr);
}

static int xgene_xfi_mdio_write(struct mii_bus *bus, int phy_id,
				int reg, u16 data)
{
	void __iomem *addr = (void __iomem *)bus->priv;
	int timeout = 100;
	u32 status, val;

	val = SET_VAL(HSTPHYADX, phy_id) | SET_VAL(HSTREGADX, reg) |
	      SET_VAL(HSTMIIMWRDAT, data);
	xgene_enet_wr_mdio_csr(addr, MIIM_FIELD_ADDR, val);

	val = HSTLDCMD | SET_VAL(HSTMIIMCMD, MIIM_CMD_LEGACY_WRITE);
	xgene_enet_wr_mdio_csr(addr, MIIM_COMMAND_ADDR, val);

	do {
		usleep_range(5, 10);
		xgene_enet_rd_mdio_csr(addr, MIIM_INDICATOR_ADDR, &status);
	} while ((status & BUSY_MASK) && timeout--);

	xgene_enet_wr_mdio_csr(addr, MIIM_COMMAND_ADDR, 0);

	return 0;
}

static int xgene_xfi_mdio_read(struct mii_bus *bus, int phy_id, int reg)
{
	void __iomem *addr = (void __iomem *)bus->priv;
	u32 data, status, val;
	int timeout = 100;

	val = SET_VAL(HSTPHYADX, phy_id) | SET_VAL(HSTREGADX, reg);
	xgene_enet_wr_mdio_csr(addr, MIIM_FIELD_ADDR, val);

	val = HSTLDCMD | SET_VAL(HSTMIIMCMD, MIIM_CMD_LEGACY_READ);
	xgene_enet_wr_mdio_csr(addr, MIIM_COMMAND_ADDR, val);

	do {
		usleep_range(5, 10);
		xgene_enet_rd_mdio_csr(addr, MIIM_INDICATOR_ADDR, &status);
	} while ((status & BUSY_MASK) && timeout--);

	if (status & BUSY_MASK) {
		pr_err("XGENET_MII_MGMT write failed\n");
		return -EBUSY;
	}

	xgene_enet_rd_mdio_csr(addr, MIIMRD_FIELD_ADDR, &data);
	xgene_enet_wr_mdio_csr(addr, MIIM_COMMAND_ADDR, 0);

	return data;
}

struct phy_device *xgene_enet_phy_register(struct mii_bus *bus, int phy_addr)
{
	struct phy_device *phy_dev;

	phy_dev = get_phy_device(bus, phy_addr, false);
	if (!phy_dev || IS_ERR(phy_dev))
		return NULL;

	if (phy_device_register(phy_dev))
		phy_device_free(phy_dev);

	return phy_dev;
}
EXPORT_SYMBOL(xgene_enet_phy_register);

#ifdef CONFIG_ACPI
static acpi_status acpi_register_phy(acpi_handle handle, u32 lvl,
				     void *context, void **ret)
{
	struct mii_bus *mdio = context;
	struct acpi_device *adev;
	struct phy_device *phy_dev;
	const union acpi_object *obj;
	u32 phy_addr;

	adev = acpi_fetch_acpi_dev(handle);
	if (!adev)
		return AE_OK;

	if (acpi_dev_get_property(adev, "phy-channel", ACPI_TYPE_INTEGER, &obj))
		return AE_OK;
	phy_addr = obj->integer.value;

	phy_dev = xgene_enet_phy_register(mdio, phy_addr);
	adev->driver_data = phy_dev;

	return AE_OK;
}
#endif

static const struct of_device_id xgene_mdio_of_match[] = {
	{
		.compatible = "apm,xgene-mdio-rgmii",
		.data = (void *)XGENE_MDIO_RGMII
	},
	{
		.compatible = "apm,xgene-mdio-xfi",
		.data = (void *)XGENE_MDIO_XFI
	},
	{},
};
MODULE_DEVICE_TABLE(of, xgene_mdio_of_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id xgene_mdio_acpi_match[] = {
	{ "APMC0D65", XGENE_MDIO_RGMII },
	{ "APMC0D66", XGENE_MDIO_XFI },
	{ }
};

MODULE_DEVICE_TABLE(acpi, xgene_mdio_acpi_match);
#endif


static int xgene_mdio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mii_bus *mdio_bus;
	struct xgene_mdio_pdata *pdata;
	void __iomem *csr_base;
	int mdio_id = 0, ret = 0;

	mdio_id = (uintptr_t)device_get_match_data(&pdev->dev);
	if (!mdio_id)
		return -ENODEV;

	pdata = devm_kzalloc(dev, sizeof(struct xgene_mdio_pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	pdata->mdio_id = mdio_id;
	pdata->dev = dev;

	csr_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(csr_base))
		return PTR_ERR(csr_base);
	pdata->mac_csr_addr = csr_base;
	pdata->mdio_csr_addr = csr_base + BLOCK_XG_MDIO_CSR_OFFSET;
	pdata->diag_csr_addr = csr_base + BLOCK_DIAG_CSR_OFFSET;

	if (mdio_id == XGENE_MDIO_RGMII)
		spin_lock_init(&pdata->mac_lock);

	if (dev->of_node) {
		pdata->clk = devm_clk_get(dev, NULL);
		if (IS_ERR(pdata->clk)) {
			dev_err(dev, "Unable to retrieve clk\n");
			return PTR_ERR(pdata->clk);
		}
	}

	ret = xgene_mdio_reset(pdata);
	if (ret)
		return ret;

	mdio_bus = mdiobus_alloc();
	if (!mdio_bus) {
		ret = -ENOMEM;
		goto out_clk;
	}

	mdio_bus->name = "APM X-Gene MDIO bus";

	if (mdio_id == XGENE_MDIO_RGMII) {
		mdio_bus->read = xgene_mdio_rgmii_read;
		mdio_bus->write = xgene_mdio_rgmii_write;
		mdio_bus->priv = (void __force *)pdata;
		snprintf(mdio_bus->id, MII_BUS_ID_SIZE, "%s",
			 "xgene-mii-rgmii");
	} else {
		mdio_bus->read = xgene_xfi_mdio_read;
		mdio_bus->write = xgene_xfi_mdio_write;
		mdio_bus->priv = (void __force *)pdata->mdio_csr_addr;
		snprintf(mdio_bus->id, MII_BUS_ID_SIZE, "%s",
			 "xgene-mii-xfi");
	}

	mdio_bus->parent = dev;
	platform_set_drvdata(pdev, pdata);

	if (dev->of_node) {
		ret = of_mdiobus_register(mdio_bus, dev->of_node);
	} else {
#ifdef CONFIG_ACPI
		/* Mask out all PHYs from auto probing. */
		mdio_bus->phy_mask = ~0;
		ret = mdiobus_register(mdio_bus);
		if (ret)
			goto out_mdiobus;

		acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_HANDLE(dev), 1,
				    acpi_register_phy, NULL, mdio_bus, NULL);
#endif
	}

	if (ret)
		goto out_mdiobus;

	pdata->mdio_bus = mdio_bus;

	return 0;

out_mdiobus:
	mdiobus_free(mdio_bus);

out_clk:
	if (dev->of_node)
		clk_disable_unprepare(pdata->clk);

	return ret;
}

static void xgene_mdio_remove(struct platform_device *pdev)
{
	struct xgene_mdio_pdata *pdata = platform_get_drvdata(pdev);
	struct mii_bus *mdio_bus = pdata->mdio_bus;
	struct device *dev = &pdev->dev;

	mdiobus_unregister(mdio_bus);
	mdiobus_free(mdio_bus);

	if (dev->of_node)
		clk_disable_unprepare(pdata->clk);
}

static struct platform_driver xgene_mdio_driver = {
	.driver = {
		.name = "xgene-mdio",
		.of_match_table = xgene_mdio_of_match,
		.acpi_match_table = ACPI_PTR(xgene_mdio_acpi_match),
	},
	.probe = xgene_mdio_probe,
	.remove = xgene_mdio_remove,
};

module_platform_driver(xgene_mdio_driver);

MODULE_DESCRIPTION("APM X-Gene SoC MDIO driver");
MODULE_AUTHOR("Iyappan Subramanian <isubramanian@apm.com>");
MODULE_LICENSE("GPL");
