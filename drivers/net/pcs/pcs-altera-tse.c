// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Bootlin
 *
 * Maxime Chevallier <maxime.chevallier@bootlin.com>
 */

#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/pcs-altera-tse.h>

/* SGMII PCS register addresses
 */
#define SGMII_PCS_SCRATCH	0x10
#define SGMII_PCS_REV		0x11
#define SGMII_PCS_LINK_TIMER_0	0x12
#define   SGMII_PCS_LINK_TIMER_REG(x)		(0x12 + (x))
#define SGMII_PCS_LINK_TIMER_1	0x13
#define SGMII_PCS_IF_MODE	0x14
#define   PCS_IF_MODE_SGMII_ENA		BIT(0)
#define   PCS_IF_MODE_USE_SGMII_AN	BIT(1)
#define   PCS_IF_MODE_SGMI_SPEED_MASK	GENMASK(3, 2)
#define   PCS_IF_MODE_SGMI_SPEED_10	(0 << 2)
#define   PCS_IF_MODE_SGMI_SPEED_100	(1 << 2)
#define   PCS_IF_MODE_SGMI_SPEED_1000	(2 << 2)
#define   PCS_IF_MODE_SGMI_HALF_DUPLEX	BIT(4)
#define   PCS_IF_MODE_SGMI_PHY_AN	BIT(5)
#define SGMII_PCS_DIS_READ_TO	0x15
#define SGMII_PCS_READ_TO	0x16
#define SGMII_PCS_SW_RESET_TIMEOUT 100 /* usecs */

struct altera_tse_pcs {
	struct phylink_pcs pcs;
	void __iomem *base;
	int reg_width;
};

static struct altera_tse_pcs *phylink_pcs_to_tse_pcs(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct altera_tse_pcs, pcs);
}

static u16 tse_pcs_read(struct altera_tse_pcs *tse_pcs, int regnum)
{
	if (tse_pcs->reg_width == 4)
		return readl(tse_pcs->base + regnum * 4);
	else
		return readw(tse_pcs->base + regnum * 2);
}

static void tse_pcs_write(struct altera_tse_pcs *tse_pcs, int regnum,
			  u16 value)
{
	if (tse_pcs->reg_width == 4)
		writel(value, tse_pcs->base + regnum * 4);
	else
		writew(value, tse_pcs->base + regnum * 2);
}

static int tse_pcs_reset(struct altera_tse_pcs *tse_pcs)
{
	int i = 0;
	u16 bmcr;

	/* Reset PCS block */
	bmcr = tse_pcs_read(tse_pcs, MII_BMCR);
	bmcr |= BMCR_RESET;
	tse_pcs_write(tse_pcs, MII_BMCR, bmcr);

	for (i = 0; i < SGMII_PCS_SW_RESET_TIMEOUT; i++) {
		if (!(tse_pcs_read(tse_pcs, MII_BMCR) & BMCR_RESET))
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

static int alt_tse_pcs_validate(struct phylink_pcs *pcs,
				unsigned long *supported,
				const struct phylink_link_state *state)
{
	if (state->interface == PHY_INTERFACE_MODE_SGMII ||
	    state->interface == PHY_INTERFACE_MODE_1000BASEX)
		return 1;

	return -EINVAL;
}

static int alt_tse_pcs_config(struct phylink_pcs *pcs, unsigned int mode,
			      phy_interface_t interface,
			      const unsigned long *advertising,
			      bool permit_pause_to_mac)
{
	struct altera_tse_pcs *tse_pcs = phylink_pcs_to_tse_pcs(pcs);
	u32 ctrl, if_mode;

	ctrl = tse_pcs_read(tse_pcs, MII_BMCR);
	if_mode = tse_pcs_read(tse_pcs, SGMII_PCS_IF_MODE);

	/* Set link timer to 1.6ms, as per the MegaCore Function User Guide */
	tse_pcs_write(tse_pcs, SGMII_PCS_LINK_TIMER_0, 0x0D40);
	tse_pcs_write(tse_pcs, SGMII_PCS_LINK_TIMER_1, 0x03);

	if (interface == PHY_INTERFACE_MODE_SGMII) {
		if_mode |= PCS_IF_MODE_USE_SGMII_AN | PCS_IF_MODE_SGMII_ENA;
	} else if (interface == PHY_INTERFACE_MODE_1000BASEX) {
		if_mode &= ~(PCS_IF_MODE_USE_SGMII_AN | PCS_IF_MODE_SGMII_ENA);
		if_mode |= PCS_IF_MODE_SGMI_SPEED_1000;
	}

	ctrl |= (BMCR_SPEED1000 | BMCR_FULLDPLX | BMCR_ANENABLE);

	tse_pcs_write(tse_pcs, MII_BMCR, ctrl);
	tse_pcs_write(tse_pcs, SGMII_PCS_IF_MODE, if_mode);

	return tse_pcs_reset(tse_pcs);
}

static void alt_tse_pcs_get_state(struct phylink_pcs *pcs,
				  struct phylink_link_state *state)
{
	struct altera_tse_pcs *tse_pcs = phylink_pcs_to_tse_pcs(pcs);
	u16 bmsr, lpa;

	bmsr = tse_pcs_read(tse_pcs, MII_BMSR);
	lpa = tse_pcs_read(tse_pcs, MII_LPA);

	phylink_mii_c22_pcs_decode_state(state, bmsr, lpa);
}

static void alt_tse_pcs_an_restart(struct phylink_pcs *pcs)
{
	struct altera_tse_pcs *tse_pcs = phylink_pcs_to_tse_pcs(pcs);
	u16 bmcr;

	bmcr = tse_pcs_read(tse_pcs, MII_BMCR);
	bmcr |= BMCR_ANRESTART;
	tse_pcs_write(tse_pcs, MII_BMCR, bmcr);

	/* This PCS seems to require a soft reset to re-sync the AN logic */
	tse_pcs_reset(tse_pcs);
}

static const struct phylink_pcs_ops alt_tse_pcs_ops = {
	.pcs_validate = alt_tse_pcs_validate,
	.pcs_get_state = alt_tse_pcs_get_state,
	.pcs_config = alt_tse_pcs_config,
	.pcs_an_restart = alt_tse_pcs_an_restart,
};

struct phylink_pcs *alt_tse_pcs_create(struct net_device *ndev,
				       void __iomem *pcs_base, int reg_width)
{
	struct altera_tse_pcs *tse_pcs;

	if (reg_width != 4 && reg_width != 2)
		return ERR_PTR(-EINVAL);

	tse_pcs = devm_kzalloc(&ndev->dev, sizeof(*tse_pcs), GFP_KERNEL);
	if (!tse_pcs)
		return ERR_PTR(-ENOMEM);

	tse_pcs->pcs.ops = &alt_tse_pcs_ops;
	tse_pcs->base = pcs_base;
	tse_pcs->reg_width = reg_width;

	return &tse_pcs->pcs;
}
EXPORT_SYMBOL_GPL(alt_tse_pcs_create);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Altera TSE PCS driver");
MODULE_AUTHOR("Maxime Chevallier <maxime.chevallier@bootlin.com>");
