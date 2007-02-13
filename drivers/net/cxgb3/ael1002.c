/*
 * Copyright (c) 2005-2007 Chelsio, Inc. All rights reserved.
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
	AEL100X_TX_DISABLE = 9,
	AEL100X_TX_CONFIG1 = 0xc002,
	AEL1002_PWR_DOWN_HI = 0xc011,
	AEL1002_PWR_DOWN_LO = 0xc012,
	AEL1002_XFI_EQL = 0xc015,
	AEL1002_LB_EN = 0xc017,

	LASI_CTRL = 0x9002,
	LASI_STAT = 0x9005
};

static void ael100x_txon(struct cphy *phy)
{
	int tx_on_gpio = phy->addr == 0 ? F_GPIO7_OUT_VAL : F_GPIO2_OUT_VAL;

	msleep(100);
	t3_set_reg_field(phy->adapter, A_T3DBG_GPIO_EN, 0, tx_on_gpio);
	msleep(30);
}

static int ael1002_power_down(struct cphy *phy, int enable)
{
	int err;

	err = mdio_write(phy, MDIO_DEV_PMA_PMD, AEL100X_TX_DISABLE, !!enable);
	if (!err)
		err = t3_mdio_change_bits(phy, MDIO_DEV_PMA_PMD, MII_BMCR,
					  BMCR_PDOWN, enable ? BMCR_PDOWN : 0);
	return err;
}

static int ael1002_reset(struct cphy *phy, int wait)
{
	int err;

	if ((err = ael1002_power_down(phy, 0)) ||
	    (err = mdio_write(phy, MDIO_DEV_PMA_PMD, AEL100X_TX_CONFIG1, 1)) ||
	    (err = mdio_write(phy, MDIO_DEV_PMA_PMD, AEL1002_PWR_DOWN_HI, 0)) ||
	    (err = mdio_write(phy, MDIO_DEV_PMA_PMD, AEL1002_PWR_DOWN_LO, 0)) ||
	    (err = mdio_write(phy, MDIO_DEV_PMA_PMD, AEL1002_XFI_EQL, 0x18)) ||
	    (err = t3_mdio_change_bits(phy, MDIO_DEV_PMA_PMD, AEL1002_LB_EN,
				       0, 1 << 5)))
		return err;
	return 0;
}

static int ael1002_intr_noop(struct cphy *phy)
{
	return 0;
}

static int ael100x_get_link_status(struct cphy *phy, int *link_ok,
				   int *speed, int *duplex, int *fc)
{
	if (link_ok) {
		unsigned int status;
		int err = mdio_read(phy, MDIO_DEV_PMA_PMD, MII_BMSR, &status);

		/*
		 * BMSR_LSTATUS is latch-low, so if it is 0 we need to read it
		 * once more to get the current link state.
		 */
		if (!err && !(status & BMSR_LSTATUS))
			err = mdio_read(phy, MDIO_DEV_PMA_PMD, MII_BMSR,
					&status);
		if (err)
			return err;
		*link_ok = !!(status & BMSR_LSTATUS);
	}
	if (speed)
		*speed = SPEED_10000;
	if (duplex)
		*duplex = DUPLEX_FULL;
	return 0;
}

static struct cphy_ops ael1002_ops = {
	.reset = ael1002_reset,
	.intr_enable = ael1002_intr_noop,
	.intr_disable = ael1002_intr_noop,
	.intr_clear = ael1002_intr_noop,
	.intr_handler = ael1002_intr_noop,
	.get_link_status = ael100x_get_link_status,
	.power_down = ael1002_power_down,
};

void t3_ael1002_phy_prep(struct cphy *phy, struct adapter *adapter,
			 int phy_addr, const struct mdio_ops *mdio_ops)
{
	cphy_init(phy, adapter, phy_addr, &ael1002_ops, mdio_ops);
	ael100x_txon(phy);
}

static int ael1006_reset(struct cphy *phy, int wait)
{
	return t3_phy_reset(phy, MDIO_DEV_PMA_PMD, wait);
}

static int ael1006_intr_enable(struct cphy *phy)
{
	return mdio_write(phy, MDIO_DEV_PMA_PMD, LASI_CTRL, 1);
}

static int ael1006_intr_disable(struct cphy *phy)
{
	return mdio_write(phy, MDIO_DEV_PMA_PMD, LASI_CTRL, 0);
}

static int ael1006_intr_clear(struct cphy *phy)
{
	u32 val;

	return mdio_read(phy, MDIO_DEV_PMA_PMD, LASI_STAT, &val);
}

static int ael1006_intr_handler(struct cphy *phy)
{
	unsigned int status;
	int err = mdio_read(phy, MDIO_DEV_PMA_PMD, LASI_STAT, &status);

	if (err)
		return err;
	return (status & 1) ? cphy_cause_link_change : 0;
}

static int ael1006_power_down(struct cphy *phy, int enable)
{
	return t3_mdio_change_bits(phy, MDIO_DEV_PMA_PMD, MII_BMCR,
				   BMCR_PDOWN, enable ? BMCR_PDOWN : 0);
}

static struct cphy_ops ael1006_ops = {
	.reset = ael1006_reset,
	.intr_enable = ael1006_intr_enable,
	.intr_disable = ael1006_intr_disable,
	.intr_clear = ael1006_intr_clear,
	.intr_handler = ael1006_intr_handler,
	.get_link_status = ael100x_get_link_status,
	.power_down = ael1006_power_down,
};

void t3_ael1006_phy_prep(struct cphy *phy, struct adapter *adapter,
			 int phy_addr, const struct mdio_ops *mdio_ops)
{
	cphy_init(phy, adapter, phy_addr, &ael1006_ops, mdio_ops);
	ael100x_txon(phy);
}

static struct cphy_ops qt2045_ops = {
	.reset = ael1006_reset,
	.intr_enable = ael1006_intr_enable,
	.intr_disable = ael1006_intr_disable,
	.intr_clear = ael1006_intr_clear,
	.intr_handler = ael1006_intr_handler,
	.get_link_status = ael100x_get_link_status,
	.power_down = ael1006_power_down,
};

void t3_qt2045_phy_prep(struct cphy *phy, struct adapter *adapter,
			int phy_addr, const struct mdio_ops *mdio_ops)
{
	unsigned int stat;

	cphy_init(phy, adapter, phy_addr, &qt2045_ops, mdio_ops);

	/*
	 * Some cards where the PHY is supposed to be at address 0 actually
	 * have it at 1.
	 */
	if (!phy_addr && !mdio_read(phy, MDIO_DEV_PMA_PMD, MII_BMSR, &stat) &&
	    stat == 0xffff)
		phy->addr = 1;
}

static int xaui_direct_reset(struct cphy *phy, int wait)
{
	return 0;
}

static int xaui_direct_get_link_status(struct cphy *phy, int *link_ok,
				       int *speed, int *duplex, int *fc)
{
	if (link_ok) {
		unsigned int status;

		status = t3_read_reg(phy->adapter,
				     XGM_REG(A_XGM_SERDES_STAT0, phy->addr));
		*link_ok = !(status & F_LOWSIG0);
	}
	if (speed)
		*speed = SPEED_10000;
	if (duplex)
		*duplex = DUPLEX_FULL;
	return 0;
}

static int xaui_direct_power_down(struct cphy *phy, int enable)
{
	return 0;
}

static struct cphy_ops xaui_direct_ops = {
	.reset = xaui_direct_reset,
	.intr_enable = ael1002_intr_noop,
	.intr_disable = ael1002_intr_noop,
	.intr_clear = ael1002_intr_noop,
	.intr_handler = ael1002_intr_noop,
	.get_link_status = xaui_direct_get_link_status,
	.power_down = xaui_direct_power_down,
};

void t3_xaui_direct_phy_prep(struct cphy *phy, struct adapter *adapter,
			     int phy_addr, const struct mdio_ops *mdio_ops)
{
	cphy_init(phy, adapter, 1, &xaui_direct_ops, mdio_ops);
}
