// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) Tehuti Networks Ltd. */

#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/phylink.h>

#include "tn40.h"

#define TN40_MDIO_DEVAD_MASK GENMASK(4, 0)
#define TN40_MDIO_PRTAD_MASK GENMASK(9, 5)
#define TN40_MDIO_CMD_VAL(device, port)			\
	(FIELD_PREP(TN40_MDIO_DEVAD_MASK, (device)) |	\
	 (FIELD_PREP(TN40_MDIO_PRTAD_MASK, (port))))
#define TN40_MDIO_CMD_READ BIT(15)

static void tn40_mdio_set_speed(struct tn40_priv *priv, u32 speed)
{
	void __iomem *regs = priv->regs;
	int mdio_cfg;

	if (speed == TN40_MDIO_SPEED_1MHZ)
		mdio_cfg = (0x7d << 7) | 0x08;	/* 1MHz */
	else
		mdio_cfg = 0xA08;	/* 6MHz */
	mdio_cfg |= (1 << 6);
	writel(mdio_cfg, regs + TN40_REG_MDIO_CMD_STAT);
	msleep(100);
}

static u32 tn40_mdio_stat(struct tn40_priv *priv)
{
	void __iomem *regs = priv->regs;

	return readl(regs + TN40_REG_MDIO_CMD_STAT);
}

static int tn40_mdio_wait_nobusy(struct tn40_priv *priv, u32 *val)
{
	u32 stat;
	int ret;

	ret = readx_poll_timeout_atomic(tn40_mdio_stat, priv, stat,
					TN40_GET_MDIO_BUSY(stat) == 0, 10,
					10000);
	if (val)
		*val = stat;
	return ret;
}

static int tn40_mdio_read(struct tn40_priv *priv, int port, int device,
			  u16 regnum)
{
	void __iomem *regs = priv->regs;
	u32 i;

	/* wait until MDIO is not busy */
	if (tn40_mdio_wait_nobusy(priv, NULL))
		return -EIO;

	i = TN40_MDIO_CMD_VAL(device, port);
	writel(i, regs + TN40_REG_MDIO_CMD);
	writel((u32)regnum, regs + TN40_REG_MDIO_ADDR);
	if (tn40_mdio_wait_nobusy(priv, NULL))
		return -EIO;

	writel(TN40_MDIO_CMD_READ | i, regs + TN40_REG_MDIO_CMD);
	/* read CMD_STAT until not busy */
	if (tn40_mdio_wait_nobusy(priv, NULL))
		return -EIO;

	return lower_16_bits(readl(regs + TN40_REG_MDIO_DATA));
}

static int tn40_mdio_write(struct tn40_priv *priv, int port, int device,
			   u16 regnum, u16 data)
{
	void __iomem *regs = priv->regs;
	u32 tmp_reg = 0;
	int ret;

	/* wait until MDIO is not busy */
	if (tn40_mdio_wait_nobusy(priv, NULL))
		return -EIO;
	writel(TN40_MDIO_CMD_VAL(device, port), regs + TN40_REG_MDIO_CMD);
	writel((u32)regnum, regs + TN40_REG_MDIO_ADDR);
	if (tn40_mdio_wait_nobusy(priv, NULL))
		return -EIO;
	writel((u32)data, regs + TN40_REG_MDIO_DATA);
	/* read CMD_STAT until not busy */
	ret = tn40_mdio_wait_nobusy(priv, &tmp_reg);
	if (ret)
		return -EIO;

	if (TN40_GET_MDIO_RD_ERR(tmp_reg)) {
		dev_err(&priv->pdev->dev, "MDIO error after write command\n");
		return -EIO;
	}
	return 0;
}

static int tn40_mdio_read_c45(struct mii_bus *mii_bus, int addr, int devnum,
			      int regnum)
{
	return tn40_mdio_read(mii_bus->priv, addr, devnum, regnum);
}

static int tn40_mdio_write_c45(struct mii_bus *mii_bus, int addr, int devnum,
			       int regnum, u16 val)
{
	return  tn40_mdio_write(mii_bus->priv, addr, devnum, regnum, val);
}

int tn40_mdiobus_init(struct tn40_priv *priv)
{
	struct pci_dev *pdev = priv->pdev;
	struct mii_bus *bus;
	int ret;

	bus = devm_mdiobus_alloc(&pdev->dev);
	if (!bus)
		return -ENOMEM;

	bus->name = TN40_DRV_NAME;
	bus->parent = &pdev->dev;
	snprintf(bus->id, MII_BUS_ID_SIZE, "tn40xx-%x-%x",
		 pci_domain_nr(pdev->bus), pci_dev_id(pdev));
	bus->priv = priv;

	bus->read_c45 = tn40_mdio_read_c45;
	bus->write_c45 = tn40_mdio_write_c45;

	ret = devm_mdiobus_register(&pdev->dev, bus);
	if (ret) {
		dev_err(&pdev->dev, "failed to register mdiobus %d %u %u\n",
			ret, bus->state, MDIOBUS_UNREGISTERED);
		return ret;
	}
	tn40_mdio_set_speed(priv, TN40_MDIO_SPEED_6MHZ);
	priv->mdio = bus;
	return 0;
}
