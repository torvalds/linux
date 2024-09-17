/*
 * Copyright (c) 2005-2008 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "common.h"
#include "regs.h"

enum {
	/* MDIO_DEV_PMA_PMD registers */
	AQ_LINK_STAT	= 0xe800,
	AQ_IMASK_PMA	= 0xf000,

	/* MDIO_DEV_XGXS registers */
	AQ_XAUI_RX_CFG	= 0xc400,
	AQ_XAUI_TX_CFG	= 0xe400,

	/* MDIO_DEV_ANEG registers */
	AQ_1G_CTRL	= 0xc400,
	AQ_ANEG_STAT	= 0xc800,

	/* MDIO_DEV_VEND1 registers */
	AQ_FW_VERSION	= 0x0020,
	AQ_IFLAG_GLOBAL	= 0xfc00,
	AQ_IMASK_GLOBAL	= 0xff00,
};

enum {
	IMASK_PMA	= 1 << 2,
	IMASK_GLOBAL	= 1 << 15,
	ADV_1G_FULL	= 1 << 15,
	ADV_1G_HALF	= 1 << 14,
	ADV_10G_FULL	= 1 << 12,
	AQ_RESET	= (1 << 14) | (1 << 15),
	AQ_LOWPOWER	= 1 << 12,
};

static int aq100x_reset(struct cphy *phy, int wait)
{
	/*
	 * Ignore the caller specified wait time; always wait for the reset to
	 * complete. Can take up to 3s.
	 */
	int err = t3_phy_reset(phy, MDIO_MMD_VEND1, 3000);

	if (err)
		CH_WARN(phy->adapter, "PHY%d: reset failed (0x%x).\n",
			phy->mdio.prtad, err);

	return err;
}

static int aq100x_intr_enable(struct cphy *phy)
{
	int err = t3_mdio_write(phy, MDIO_MMD_PMAPMD, AQ_IMASK_PMA, IMASK_PMA);
	if (err)
		return err;

	err = t3_mdio_write(phy, MDIO_MMD_VEND1, AQ_IMASK_GLOBAL, IMASK_GLOBAL);
	return err;
}

static int aq100x_intr_disable(struct cphy *phy)
{
	return t3_mdio_write(phy, MDIO_MMD_VEND1, AQ_IMASK_GLOBAL, 0);
}

static int aq100x_intr_clear(struct cphy *phy)
{
	unsigned int v;

	t3_mdio_read(phy, MDIO_MMD_VEND1, AQ_IFLAG_GLOBAL, &v);
	t3_mdio_read(phy, MDIO_MMD_PMAPMD, MDIO_STAT1, &v);

	return 0;
}

static int aq100x_intr_handler(struct cphy *phy)
{
	int err;
	unsigned int cause, v;

	err = t3_mdio_read(phy, MDIO_MMD_VEND1, AQ_IFLAG_GLOBAL, &cause);
	if (err)
		return err;

	/* Read (and reset) the latching version of the status */
	t3_mdio_read(phy, MDIO_MMD_PMAPMD, MDIO_STAT1, &v);

	return cphy_cause_link_change;
}

static int aq100x_power_down(struct cphy *phy, int off)
{
	return mdio_set_flag(&phy->mdio, phy->mdio.prtad,
			     MDIO_MMD_PMAPMD, MDIO_CTRL1,
			     MDIO_CTRL1_LPOWER, off);
}

static int aq100x_autoneg_enable(struct cphy *phy)
{
	int err;

	err = aq100x_power_down(phy, 0);
	if (!err)
		err = mdio_set_flag(&phy->mdio, phy->mdio.prtad,
				    MDIO_MMD_AN, MDIO_CTRL1,
				    BMCR_ANENABLE | BMCR_ANRESTART, 1);

	return err;
}

static int aq100x_autoneg_restart(struct cphy *phy)
{
	int err;

	err = aq100x_power_down(phy, 0);
	if (!err)
		err = mdio_set_flag(&phy->mdio, phy->mdio.prtad,
				    MDIO_MMD_AN, MDIO_CTRL1,
				    BMCR_ANENABLE | BMCR_ANRESTART, 1);

	return err;
}

static int aq100x_advertise(struct cphy *phy, unsigned int advertise_map)
{
	unsigned int adv;
	int err;

	/* 10G advertisement */
	adv = 0;
	if (advertise_map & ADVERTISED_10000baseT_Full)
		adv |= ADV_10G_FULL;
	err = t3_mdio_change_bits(phy, MDIO_MMD_AN, MDIO_AN_10GBT_CTRL,
				  ADV_10G_FULL, adv);
	if (err)
		return err;

	/* 1G advertisement */
	adv = 0;
	if (advertise_map & ADVERTISED_1000baseT_Full)
		adv |= ADV_1G_FULL;
	if (advertise_map & ADVERTISED_1000baseT_Half)
		adv |= ADV_1G_HALF;
	err = t3_mdio_change_bits(phy, MDIO_MMD_AN, AQ_1G_CTRL,
				  ADV_1G_FULL | ADV_1G_HALF, adv);
	if (err)
		return err;

	/* 100M, pause advertisement */
	adv = 0;
	if (advertise_map & ADVERTISED_100baseT_Half)
		adv |= ADVERTISE_100HALF;
	if (advertise_map & ADVERTISED_100baseT_Full)
		adv |= ADVERTISE_100FULL;
	if (advertise_map & ADVERTISED_Pause)
		adv |= ADVERTISE_PAUSE_CAP;
	if (advertise_map & ADVERTISED_Asym_Pause)
		adv |= ADVERTISE_PAUSE_ASYM;
	err = t3_mdio_change_bits(phy, MDIO_MMD_AN, MDIO_AN_ADVERTISE,
				  0xfe0, adv);

	return err;
}

static int aq100x_set_loopback(struct cphy *phy, int mmd, int dir, int enable)
{
	return mdio_set_flag(&phy->mdio, phy->mdio.prtad,
			     MDIO_MMD_PMAPMD, MDIO_CTRL1,
			     BMCR_LOOPBACK, enable);
}

static int aq100x_set_speed_duplex(struct cphy *phy, int speed, int duplex)
{
	/* no can do */
	return -1;
}

static int aq100x_get_link_status(struct cphy *phy, int *link_ok,
				  int *speed, int *duplex, int *fc)
{
	int err;
	unsigned int v;

	if (link_ok) {
		err = t3_mdio_read(phy, MDIO_MMD_PMAPMD, AQ_LINK_STAT, &v);
		if (err)
			return err;

		*link_ok = v & 1;
		if (!*link_ok)
			return 0;
	}

	err = t3_mdio_read(phy, MDIO_MMD_AN, AQ_ANEG_STAT, &v);
	if (err)
		return err;

	if (speed) {
		switch (v & 0x6) {
		case 0x6:
			*speed = SPEED_10000;
			break;
		case 0x4:
			*speed = SPEED_1000;
			break;
		case 0x2:
			*speed = SPEED_100;
			break;
		case 0x0:
			*speed = SPEED_10;
			break;
		}
	}

	if (duplex)
		*duplex = v & 1 ? DUPLEX_FULL : DUPLEX_HALF;

	return 0;
}

static const struct cphy_ops aq100x_ops = {
	.reset             = aq100x_reset,
	.intr_enable       = aq100x_intr_enable,
	.intr_disable      = aq100x_intr_disable,
	.intr_clear        = aq100x_intr_clear,
	.intr_handler      = aq100x_intr_handler,
	.autoneg_enable    = aq100x_autoneg_enable,
	.autoneg_restart   = aq100x_autoneg_restart,
	.advertise         = aq100x_advertise,
	.set_loopback      = aq100x_set_loopback,
	.set_speed_duplex  = aq100x_set_speed_duplex,
	.get_link_status   = aq100x_get_link_status,
	.power_down        = aq100x_power_down,
	.mmds 		   = MDIO_DEVS_PMAPMD | MDIO_DEVS_PCS | MDIO_DEVS_PHYXS,
};

int t3_aq100x_phy_prep(struct cphy *phy, struct adapter *adapter, int phy_addr,
		       const struct mdio_ops *mdio_ops)
{
	unsigned int v, v2, gpio, wait;
	int err;

	cphy_init(phy, adapter, phy_addr, &aq100x_ops, mdio_ops,
		  SUPPORTED_1000baseT_Full | SUPPORTED_10000baseT_Full |
		  SUPPORTED_TP | SUPPORTED_Autoneg | SUPPORTED_AUI,
		  "1000/10GBASE-T");

	/*
	 * The PHY has been out of reset ever since the system powered up.  So
	 * we do a hard reset over here.
	 */
	gpio = phy_addr ? F_GPIO10_OUT_VAL : F_GPIO6_OUT_VAL;
	t3_set_reg_field(adapter, A_T3DBG_GPIO_EN, gpio, 0);
	msleep(1);
	t3_set_reg_field(adapter, A_T3DBG_GPIO_EN, gpio, gpio);

	/*
	 * Give it enough time to load the firmware and get ready for mdio.
	 */
	msleep(1000);
	wait = 500; /* in 10ms increments */
	do {
		err = t3_mdio_read(phy, MDIO_MMD_VEND1, MDIO_CTRL1, &v);
		if (err || v == 0xffff) {

			/* Allow prep_adapter to succeed when ffff is read */

			CH_WARN(adapter, "PHY%d: reset failed (0x%x, 0x%x).\n",
				phy_addr, err, v);
			goto done;
		}

		v &= AQ_RESET;
		if (v)
			msleep(10);
	} while (v && --wait);
	if (v) {
		CH_WARN(adapter, "PHY%d: reset timed out (0x%x).\n",
			phy_addr, v);

		goto done; /* let prep_adapter succeed */
	}

	/* Datasheet says 3s max but this has been observed */
	wait = (500 - wait) * 10 + 1000;
	if (wait > 3000)
		CH_WARN(adapter, "PHY%d: reset took %ums\n", phy_addr, wait);

	/* Firmware version check. */
	t3_mdio_read(phy, MDIO_MMD_VEND1, AQ_FW_VERSION, &v);
	if (v != 101)
		CH_WARN(adapter, "PHY%d: unsupported firmware %d\n",
			phy_addr, v);

	/*
	 * The PHY should start in really-low-power mode.  Prepare it for normal
	 * operations.
	 */
	err = t3_mdio_read(phy, MDIO_MMD_VEND1, MDIO_CTRL1, &v);
	if (err)
		return err;
	if (v & AQ_LOWPOWER) {
		err = t3_mdio_change_bits(phy, MDIO_MMD_VEND1, MDIO_CTRL1,
					  AQ_LOWPOWER, 0);
		if (err)
			return err;
		msleep(10);
	} else
		CH_WARN(adapter, "PHY%d does not start in low power mode.\n",
			phy_addr);

	/*
	 * Verify XAUI settings, but let prep succeed no matter what.
	 */
	v = v2 = 0;
	t3_mdio_read(phy, MDIO_MMD_PHYXS, AQ_XAUI_RX_CFG, &v);
	t3_mdio_read(phy, MDIO_MMD_PHYXS, AQ_XAUI_TX_CFG, &v2);
	if (v != 0x1b || v2 != 0x1b)
		CH_WARN(adapter,
			"PHY%d: incorrect XAUI settings (0x%x, 0x%x).\n",
			phy_addr, v, v2);

done:
	return err;
}
