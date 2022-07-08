// SPDX-License-Identifier: GPL-2.0
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/bitfield.h>
#include <linux/of_mdio.h>

#include "spl2sw_register.h"
#include "spl2sw_define.h"
#include "spl2sw_mdio.h"

#define SPL2SW_MDIO_READ_CMD           0x02
#define SPL2SW_MDIO_WRITE_CMD          0x01

static int spl2sw_mdio_access(struct spl2sw_common *comm, u8 cmd, u8 addr, u8 regnum, u16 wdata)
{
	u32 reg, reg2;
	u32 val;
	int ret;

	/* Note that addr (of phy) should match either ext_phy0_addr
	 * or ext_phy1_addr, or mdio commands won't be sent out.
	 */
	reg = readl(comm->l2sw_reg_base + L2SW_MAC_FORCE_MODE);
	reg &= ~MAC_EXT_PHY0_ADDR;
	reg |= FIELD_PREP(MAC_EXT_PHY0_ADDR, addr);

	reg2 = FIELD_PREP(MAC_CPU_PHY_WT_DATA, wdata) | FIELD_PREP(MAC_CPU_PHY_CMD, cmd) |
	       FIELD_PREP(MAC_CPU_PHY_REG_ADDR, regnum) | FIELD_PREP(MAC_CPU_PHY_ADDR, addr);

	/* Set ext_phy0_addr and then issue mdio command.
	 * No interrupt is allowed in between.
	 */
	spin_lock_irq(&comm->mdio_lock);
	writel(reg, comm->l2sw_reg_base + L2SW_MAC_FORCE_MODE);
	writel(reg2, comm->l2sw_reg_base + L2SW_PHY_CNTL_REG0);
	spin_unlock_irq(&comm->mdio_lock);

	ret = read_poll_timeout(readl, val, val & cmd, 1, 1000, true,
				comm->l2sw_reg_base + L2SW_PHY_CNTL_REG1);

	/* Set ext_phy0_addr back to 31 to prevent
	 * from sending mdio command to phy by
	 * hardware auto-mdio function.
	 */
	reg = readl(comm->l2sw_reg_base + L2SW_MAC_FORCE_MODE);
	reg &= ~MAC_EXT_PHY0_ADDR;
	reg |= FIELD_PREP(MAC_EXT_PHY0_ADDR, 31);
	writel(reg, comm->l2sw_reg_base + L2SW_MAC_FORCE_MODE);

	if (ret == 0)
		return val >> 16;
	else
		return ret;
}

static int spl2sw_mii_read(struct mii_bus *bus, int addr, int regnum)
{
	struct spl2sw_common *comm = bus->priv;

	if (regnum & MII_ADDR_C45)
		return -EOPNOTSUPP;

	return spl2sw_mdio_access(comm, SPL2SW_MDIO_READ_CMD, addr, regnum, 0);
}

static int spl2sw_mii_write(struct mii_bus *bus, int addr, int regnum, u16 val)
{
	struct spl2sw_common *comm = bus->priv;
	int ret;

	if (regnum & MII_ADDR_C45)
		return -EOPNOTSUPP;

	ret = spl2sw_mdio_access(comm, SPL2SW_MDIO_WRITE_CMD, addr, regnum, val);
	if (ret < 0)
		return ret;

	return 0;
}

u32 spl2sw_mdio_init(struct spl2sw_common *comm)
{
	struct device_node *mdio_np;
	struct mii_bus *mii_bus;
	int ret;

	/* Get mdio child node. */
	mdio_np = of_get_child_by_name(comm->pdev->dev.of_node, "mdio");
	if (!mdio_np) {
		dev_err(&comm->pdev->dev, "No mdio child node found!\n");
		return -ENODEV;
	}

	/* Allocate and register mdio bus. */
	mii_bus = devm_mdiobus_alloc(&comm->pdev->dev);
	if (!mii_bus) {
		ret = -ENOMEM;
		goto out;
	}

	mii_bus->name = "sunplus_mii_bus";
	mii_bus->parent = &comm->pdev->dev;
	mii_bus->priv = comm;
	mii_bus->read = spl2sw_mii_read;
	mii_bus->write = spl2sw_mii_write;
	snprintf(mii_bus->id, MII_BUS_ID_SIZE, "%s-mii", dev_name(&comm->pdev->dev));

	ret = of_mdiobus_register(mii_bus, mdio_np);
	if (ret) {
		dev_err(&comm->pdev->dev, "Failed to register mdiobus!\n");
		goto out;
	}

	comm->mii_bus = mii_bus;

out:
	of_node_put(mdio_np);
	return ret;
}

void spl2sw_mdio_remove(struct spl2sw_common *comm)
{
	if (comm->mii_bus) {
		mdiobus_unregister(comm->mii_bus);
		comm->mii_bus = NULL;
	}
}
