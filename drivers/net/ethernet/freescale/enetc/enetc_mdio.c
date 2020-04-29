// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2019 NXP */

#include <linux/fsl/enetc_mdio.h>
#include <linux/mdio.h>
#include <linux/of_mdio.h>
#include <linux/iopoll.h>
#include <linux/of.h>

#include "enetc_pf.h"

#define	ENETC_MDIO_CFG	0x0	/* MDIO configuration and status */
#define	ENETC_MDIO_CTL	0x4	/* MDIO control */
#define	ENETC_MDIO_DATA	0x8	/* MDIO data */
#define	ENETC_MDIO_ADDR	0xc	/* MDIO address */

static inline u32 _enetc_mdio_rd(struct enetc_mdio_priv *mdio_priv, int off)
{
	return enetc_port_rd(mdio_priv->hw, mdio_priv->mdio_base + off);
}

static inline void _enetc_mdio_wr(struct enetc_mdio_priv *mdio_priv, int off,
				  u32 val)
{
	enetc_port_wr(mdio_priv->hw, mdio_priv->mdio_base + off, val);
}

#define enetc_mdio_rd(mdio_priv, off) \
	_enetc_mdio_rd(mdio_priv, ENETC_##off)
#define enetc_mdio_wr(mdio_priv, off, val) \
	_enetc_mdio_wr(mdio_priv, ENETC_##off, val)
#define enetc_mdio_rd_reg(off)	enetc_mdio_rd(mdio_priv, off)

#define MDIO_CFG_CLKDIV(x)	((((x) >> 1) & 0xff) << 8)
#define MDIO_CFG_BSY		BIT(0)
#define MDIO_CFG_RD_ER		BIT(1)
#define MDIO_CFG_HOLD(x)	(((x) << 2) & GENMASK(4, 2))
#define MDIO_CFG_ENC45		BIT(6)
 /* external MDIO only - driven on neg MDC edge */
#define MDIO_CFG_NEG		BIT(23)

#define ENETC_EMDIO_CFG \
	(MDIO_CFG_HOLD(2) | \
	 MDIO_CFG_CLKDIV(258) | \
	 MDIO_CFG_NEG)

#define MDIO_CTL_DEV_ADDR(x)	((x) & 0x1f)
#define MDIO_CTL_PORT_ADDR(x)	(((x) & 0x1f) << 5)
#define MDIO_CTL_READ		BIT(15)
#define MDIO_DATA(x)		((x) & 0xffff)

#define TIMEOUT	1000
static int enetc_mdio_wait_complete(struct enetc_mdio_priv *mdio_priv)
{
	u32 val;

	return readx_poll_timeout(enetc_mdio_rd_reg, MDIO_CFG, val,
				  !(val & MDIO_CFG_BSY), 10, 10 * TIMEOUT);
}

int enetc_mdio_write(struct mii_bus *bus, int phy_id, int regnum, u16 value)
{
	struct enetc_mdio_priv *mdio_priv = bus->priv;
	u32 mdio_ctl, mdio_cfg;
	u16 dev_addr;
	int ret;

	mdio_cfg = ENETC_EMDIO_CFG;
	if (regnum & MII_ADDR_C45) {
		dev_addr = (regnum >> 16) & 0x1f;
		mdio_cfg |= MDIO_CFG_ENC45;
	} else {
		/* clause 22 (ie 1G) */
		dev_addr = regnum & 0x1f;
		mdio_cfg &= ~MDIO_CFG_ENC45;
	}

	enetc_mdio_wr(mdio_priv, MDIO_CFG, mdio_cfg);

	ret = enetc_mdio_wait_complete(mdio_priv);
	if (ret)
		return ret;

	/* set port and dev addr */
	mdio_ctl = MDIO_CTL_PORT_ADDR(phy_id) | MDIO_CTL_DEV_ADDR(dev_addr);
	enetc_mdio_wr(mdio_priv, MDIO_CTL, mdio_ctl);

	/* set the register address */
	if (regnum & MII_ADDR_C45) {
		enetc_mdio_wr(mdio_priv, MDIO_ADDR, regnum & 0xffff);

		ret = enetc_mdio_wait_complete(mdio_priv);
		if (ret)
			return ret;
	}

	/* write the value */
	enetc_mdio_wr(mdio_priv, MDIO_DATA, MDIO_DATA(value));

	ret = enetc_mdio_wait_complete(mdio_priv);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(enetc_mdio_write);

int enetc_mdio_read(struct mii_bus *bus, int phy_id, int regnum)
{
	struct enetc_mdio_priv *mdio_priv = bus->priv;
	u32 mdio_ctl, mdio_cfg;
	u16 dev_addr, value;
	int ret;

	mdio_cfg = ENETC_EMDIO_CFG;
	if (regnum & MII_ADDR_C45) {
		dev_addr = (regnum >> 16) & 0x1f;
		mdio_cfg |= MDIO_CFG_ENC45;
	} else {
		dev_addr = regnum & 0x1f;
		mdio_cfg &= ~MDIO_CFG_ENC45;
	}

	enetc_mdio_wr(mdio_priv, MDIO_CFG, mdio_cfg);

	ret = enetc_mdio_wait_complete(mdio_priv);
	if (ret)
		return ret;

	/* set port and device addr */
	mdio_ctl = MDIO_CTL_PORT_ADDR(phy_id) | MDIO_CTL_DEV_ADDR(dev_addr);
	enetc_mdio_wr(mdio_priv, MDIO_CTL, mdio_ctl);

	/* set the register address */
	if (regnum & MII_ADDR_C45) {
		enetc_mdio_wr(mdio_priv, MDIO_ADDR, regnum & 0xffff);

		ret = enetc_mdio_wait_complete(mdio_priv);
		if (ret)
			return ret;
	}

	/* initiate the read */
	enetc_mdio_wr(mdio_priv, MDIO_CTL, mdio_ctl | MDIO_CTL_READ);

	ret = enetc_mdio_wait_complete(mdio_priv);
	if (ret)
		return ret;

	/* return all Fs if nothing was there */
	if (enetc_mdio_rd(mdio_priv, MDIO_CFG) & MDIO_CFG_RD_ER) {
		dev_dbg(&bus->dev,
			"Error while reading PHY%d reg at %d.%hhu\n",
			phy_id, dev_addr, regnum);
		return 0xffff;
	}

	value = enetc_mdio_rd(mdio_priv, MDIO_DATA) & 0xffff;

	return value;
}
EXPORT_SYMBOL_GPL(enetc_mdio_read);

struct enetc_hw *enetc_hw_alloc(struct device *dev, void __iomem *port_regs)
{
	struct enetc_hw *hw;

	hw = devm_kzalloc(dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return ERR_PTR(-ENOMEM);

	hw->port = port_regs;

	return hw;
}
EXPORT_SYMBOL_GPL(enetc_hw_alloc);
