// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6xxx Switch Port Registers support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2016-2017 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 */

#include <linux/bitfield.h>
#include <linux/if_bridge.h>
#include <linux/phy.h>
#include <linux/phylink.h>

#include "chip.h"
#include "global2.h"
#include "port.h"
#include "serdes.h"

int mv88e6xxx_port_read(struct mv88e6xxx_chip *chip, int port, int reg,
			u16 *val)
{
	int addr = chip->info->port_base_addr + port;

	return mv88e6xxx_read(chip, addr, reg, val);
}

int mv88e6xxx_port_wait_bit(struct mv88e6xxx_chip *chip, int port, int reg,
			    int bit, int val)
{
	int addr = chip->info->port_base_addr + port;

	return mv88e6xxx_wait_bit(chip, addr, reg, bit, val);
}

int mv88e6xxx_port_write(struct mv88e6xxx_chip *chip, int port, int reg,
			 u16 val)
{
	int addr = chip->info->port_base_addr + port;

	return mv88e6xxx_write(chip, addr, reg, val);
}

/* Offset 0x00: MAC (or PCS or Physical) Status Register
 *
 * For most devices, this is read only. However the 6185 has the MyPause
 * bit read/write.
 */
int mv88e6185_port_set_pause(struct mv88e6xxx_chip *chip, int port,
			     int pause)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_STS, &reg);
	if (err)
		return err;

	if (pause)
		reg |= MV88E6XXX_PORT_STS_MY_PAUSE;
	else
		reg &= ~MV88E6XXX_PORT_STS_MY_PAUSE;

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_STS, reg);
}

/* Offset 0x01: MAC (or PCS or Physical) Control Register
 *
 * Link, Duplex and Flow Control have one force bit, one value bit.
 *
 * For port's MAC speed, ForceSpd (or SpdValue) bits 1:0 program the value.
 * Alternative values require the 200BASE (or AltSpeed) bit 12 set.
 * Newer chips need a ForcedSpd bit 13 set to consider the value.
 */

static int mv88e6xxx_port_set_rgmii_delay(struct mv88e6xxx_chip *chip, int port,
					  phy_interface_t mode)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_MAC_CTL, &reg);
	if (err)
		return err;

	reg &= ~(MV88E6XXX_PORT_MAC_CTL_RGMII_DELAY_RXCLK |
		 MV88E6XXX_PORT_MAC_CTL_RGMII_DELAY_TXCLK);

	switch (mode) {
	case PHY_INTERFACE_MODE_RGMII_RXID:
		reg |= MV88E6XXX_PORT_MAC_CTL_RGMII_DELAY_RXCLK;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		reg |= MV88E6XXX_PORT_MAC_CTL_RGMII_DELAY_TXCLK;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		reg |= MV88E6XXX_PORT_MAC_CTL_RGMII_DELAY_RXCLK |
			MV88E6XXX_PORT_MAC_CTL_RGMII_DELAY_TXCLK;
		break;
	case PHY_INTERFACE_MODE_RGMII:
		break;
	default:
		return 0;
	}

	err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_MAC_CTL, reg);
	if (err)
		return err;

	dev_dbg(chip->dev, "p%d: delay RXCLK %s, TXCLK %s\n", port,
		reg & MV88E6XXX_PORT_MAC_CTL_RGMII_DELAY_RXCLK ? "yes" : "no",
		reg & MV88E6XXX_PORT_MAC_CTL_RGMII_DELAY_TXCLK ? "yes" : "no");

	return 0;
}

int mv88e6352_port_set_rgmii_delay(struct mv88e6xxx_chip *chip, int port,
				   phy_interface_t mode)
{
	if (port < 5)
		return -EOPNOTSUPP;

	return mv88e6xxx_port_set_rgmii_delay(chip, port, mode);
}

int mv88e6390_port_set_rgmii_delay(struct mv88e6xxx_chip *chip, int port,
				   phy_interface_t mode)
{
	if (port != 0)
		return -EOPNOTSUPP;

	return mv88e6xxx_port_set_rgmii_delay(chip, port, mode);
}

int mv88e6320_port_set_rgmii_delay(struct mv88e6xxx_chip *chip, int port,
				   phy_interface_t mode)
{
	if (port != 2 && port != 5 && port != 6)
		return -EOPNOTSUPP;

	return mv88e6xxx_port_set_rgmii_delay(chip, port, mode);
}

int mv88e6xxx_port_set_link(struct mv88e6xxx_chip *chip, int port, int link)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_MAC_CTL, &reg);
	if (err)
		return err;

	reg &= ~(MV88E6XXX_PORT_MAC_CTL_FORCE_LINK |
		 MV88E6XXX_PORT_MAC_CTL_LINK_UP);

	switch (link) {
	case LINK_FORCED_DOWN:
		reg |= MV88E6XXX_PORT_MAC_CTL_FORCE_LINK;
		break;
	case LINK_FORCED_UP:
		reg |= MV88E6XXX_PORT_MAC_CTL_FORCE_LINK |
			MV88E6XXX_PORT_MAC_CTL_LINK_UP;
		break;
	case LINK_UNFORCED:
		/* normal link detection */
		break;
	default:
		return -EINVAL;
	}

	err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_MAC_CTL, reg);
	if (err)
		return err;

	dev_dbg(chip->dev, "p%d: %s link %s\n", port,
		reg & MV88E6XXX_PORT_MAC_CTL_FORCE_LINK ? "Force" : "Unforce",
		reg & MV88E6XXX_PORT_MAC_CTL_LINK_UP ? "up" : "down");

	return 0;
}

int mv88e6xxx_port_sync_link(struct mv88e6xxx_chip *chip, int port, unsigned int mode, bool isup)
{
	const struct mv88e6xxx_ops *ops = chip->info->ops;
	int err = 0;
	int link;

	if (isup)
		link = LINK_FORCED_UP;
	else
		link = LINK_FORCED_DOWN;

	if (ops->port_set_link)
		err = ops->port_set_link(chip, port, link);

	return err;
}

int mv88e6185_port_sync_link(struct mv88e6xxx_chip *chip, int port, unsigned int mode, bool isup)
{
	const struct mv88e6xxx_ops *ops = chip->info->ops;
	int err = 0;
	int link;

	if (mode == MLO_AN_INBAND)
		link = LINK_UNFORCED;
	else if (isup)
		link = LINK_FORCED_UP;
	else
		link = LINK_FORCED_DOWN;

	if (ops->port_set_link)
		err = ops->port_set_link(chip, port, link);

	return err;
}

static int mv88e6xxx_port_set_speed_duplex(struct mv88e6xxx_chip *chip,
					   int port, int speed, bool alt_bit,
					   bool force_bit, int duplex)
{
	u16 reg, ctrl;
	int err;

	switch (speed) {
	case 10:
		ctrl = MV88E6XXX_PORT_MAC_CTL_SPEED_10;
		break;
	case 100:
		ctrl = MV88E6XXX_PORT_MAC_CTL_SPEED_100;
		break;
	case 200:
		if (alt_bit)
			ctrl = MV88E6XXX_PORT_MAC_CTL_SPEED_100 |
				MV88E6390_PORT_MAC_CTL_ALTSPEED;
		else
			ctrl = MV88E6065_PORT_MAC_CTL_SPEED_200;
		break;
	case 1000:
		ctrl = MV88E6XXX_PORT_MAC_CTL_SPEED_1000;
		break;
	case 2500:
		if (alt_bit)
			ctrl = MV88E6390_PORT_MAC_CTL_SPEED_10000 |
				MV88E6390_PORT_MAC_CTL_ALTSPEED;
		else
			ctrl = MV88E6390_PORT_MAC_CTL_SPEED_10000;
		break;
	case 10000:
		/* all bits set, fall through... */
	case SPEED_UNFORCED:
		ctrl = MV88E6XXX_PORT_MAC_CTL_SPEED_UNFORCED;
		break;
	default:
		return -EOPNOTSUPP;
	}

	switch (duplex) {
	case DUPLEX_HALF:
		ctrl |= MV88E6XXX_PORT_MAC_CTL_FORCE_DUPLEX;
		break;
	case DUPLEX_FULL:
		ctrl |= MV88E6XXX_PORT_MAC_CTL_FORCE_DUPLEX |
			MV88E6XXX_PORT_MAC_CTL_DUPLEX_FULL;
		break;
	case DUPLEX_UNFORCED:
		/* normal duplex detection */
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_MAC_CTL, &reg);
	if (err)
		return err;

	reg &= ~(MV88E6XXX_PORT_MAC_CTL_SPEED_MASK |
		 MV88E6XXX_PORT_MAC_CTL_FORCE_DUPLEX |
		 MV88E6XXX_PORT_MAC_CTL_DUPLEX_FULL);

	if (alt_bit)
		reg &= ~MV88E6390_PORT_MAC_CTL_ALTSPEED;
	if (force_bit) {
		reg &= ~MV88E6390_PORT_MAC_CTL_FORCE_SPEED;
		if (speed != SPEED_UNFORCED)
			ctrl |= MV88E6390_PORT_MAC_CTL_FORCE_SPEED;
	}
	reg |= ctrl;

	err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_MAC_CTL, reg);
	if (err)
		return err;

	if (speed != SPEED_UNFORCED)
		dev_dbg(chip->dev, "p%d: Speed set to %d Mbps\n", port, speed);
	else
		dev_dbg(chip->dev, "p%d: Speed unforced\n", port);
	dev_dbg(chip->dev, "p%d: %s %s duplex\n", port,
		reg & MV88E6XXX_PORT_MAC_CTL_FORCE_DUPLEX ? "Force" : "Unforce",
		reg & MV88E6XXX_PORT_MAC_CTL_DUPLEX_FULL ? "full" : "half");

	return 0;
}

/* Support 10, 100, 1000 Mbps (e.g. 88E6185 family) */
int mv88e6185_port_set_speed_duplex(struct mv88e6xxx_chip *chip, int port,
				    int speed, int duplex)
{
	if (speed == 200 || speed > 1000)
		return -EOPNOTSUPP;

	return mv88e6xxx_port_set_speed_duplex(chip, port, speed, false, false,
					       duplex);
}

/* Support 10, 100 Mbps (e.g. 88E6250 family) */
int mv88e6250_port_set_speed_duplex(struct mv88e6xxx_chip *chip, int port,
				    int speed, int duplex)
{
	if (speed > 100)
		return -EOPNOTSUPP;

	return mv88e6xxx_port_set_speed_duplex(chip, port, speed, false, false,
					       duplex);
}

/* Support 10, 100, 200, 1000, 2500 Mbps (e.g. 88E6341) */
int mv88e6341_port_set_speed_duplex(struct mv88e6xxx_chip *chip, int port,
				    int speed, int duplex)
{
	if (speed > 2500)
		return -EOPNOTSUPP;

	if (speed == 200 && port != 0)
		return -EOPNOTSUPP;

	if (speed == 2500 && port < 5)
		return -EOPNOTSUPP;

	return mv88e6xxx_port_set_speed_duplex(chip, port, speed, !port, true,
					       duplex);
}

phy_interface_t mv88e6341_port_max_speed_mode(struct mv88e6xxx_chip *chip,
					      int port)
{
	if (port == 5)
		return PHY_INTERFACE_MODE_2500BASEX;

	return PHY_INTERFACE_MODE_NA;
}

/* Support 10, 100, 200, 1000 Mbps (e.g. 88E6352 family) */
int mv88e6352_port_set_speed_duplex(struct mv88e6xxx_chip *chip, int port,
				    int speed, int duplex)
{
	if (speed > 1000)
		return -EOPNOTSUPP;

	if (speed == 200 && port < 5)
		return -EOPNOTSUPP;

	return mv88e6xxx_port_set_speed_duplex(chip, port, speed, true, false,
					       duplex);
}

/* Support 10, 100, 200, 1000, 2500 Mbps (e.g. 88E6390) */
int mv88e6390_port_set_speed_duplex(struct mv88e6xxx_chip *chip, int port,
				    int speed, int duplex)
{
	if (speed > 2500)
		return -EOPNOTSUPP;

	if (speed == 200 && port != 0)
		return -EOPNOTSUPP;

	if (speed == 2500 && port < 9)
		return -EOPNOTSUPP;

	return mv88e6xxx_port_set_speed_duplex(chip, port, speed, true, true,
					       duplex);
}

phy_interface_t mv88e6390_port_max_speed_mode(struct mv88e6xxx_chip *chip,
					      int port)
{
	if (port == 9 || port == 10)
		return PHY_INTERFACE_MODE_2500BASEX;

	return PHY_INTERFACE_MODE_NA;
}

/* Support 10, 100, 200, 1000, 2500, 10000 Mbps (e.g. 88E6190X) */
int mv88e6390x_port_set_speed_duplex(struct mv88e6xxx_chip *chip, int port,
				     int speed, int duplex)
{
	if (speed == 200 && port != 0)
		return -EOPNOTSUPP;

	if (speed >= 2500 && port < 9)
		return -EOPNOTSUPP;

	return mv88e6xxx_port_set_speed_duplex(chip, port, speed, true, true,
					       duplex);
}

phy_interface_t mv88e6390x_port_max_speed_mode(struct mv88e6xxx_chip *chip,
					       int port)
{
	if (port == 9 || port == 10)
		return PHY_INTERFACE_MODE_XAUI;

	return PHY_INTERFACE_MODE_NA;
}

/* Support 10, 100, 200, 1000, 2500, 5000, 10000 Mbps (e.g. 88E6393X)
 * Function mv88e6xxx_port_set_speed_duplex() can't be used as the register
 * values for speeds 2500 & 5000 conflict.
 */
int mv88e6393x_port_set_speed_duplex(struct mv88e6xxx_chip *chip, int port,
				     int speed, int duplex)
{
	u16 reg, ctrl;
	int err;

	if (chip->info->prod_num == MV88E6XXX_PORT_SWITCH_ID_PROD_6361 &&
	    speed > 2500)
		return -EOPNOTSUPP;

	if (speed == 200 && port != 0)
		return -EOPNOTSUPP;

	if (speed >= 2500 && port > 0 && port < 9)
		return -EOPNOTSUPP;

	switch (speed) {
	case 10:
		ctrl = MV88E6XXX_PORT_MAC_CTL_SPEED_10;
		break;
	case 100:
		ctrl = MV88E6XXX_PORT_MAC_CTL_SPEED_100;
		break;
	case 200:
		ctrl = MV88E6XXX_PORT_MAC_CTL_SPEED_100 |
			MV88E6390_PORT_MAC_CTL_ALTSPEED;
		break;
	case 1000:
		ctrl = MV88E6XXX_PORT_MAC_CTL_SPEED_1000;
		break;
	case 2500:
		ctrl = MV88E6XXX_PORT_MAC_CTL_SPEED_1000 |
			MV88E6390_PORT_MAC_CTL_ALTSPEED;
		break;
	case 5000:
		ctrl = MV88E6390_PORT_MAC_CTL_SPEED_10000 |
			MV88E6390_PORT_MAC_CTL_ALTSPEED;
		break;
	case 10000:
	case SPEED_UNFORCED:
		ctrl = MV88E6XXX_PORT_MAC_CTL_SPEED_UNFORCED;
		break;
	default:
		return -EOPNOTSUPP;
	}

	switch (duplex) {
	case DUPLEX_HALF:
		ctrl |= MV88E6XXX_PORT_MAC_CTL_FORCE_DUPLEX;
		break;
	case DUPLEX_FULL:
		ctrl |= MV88E6XXX_PORT_MAC_CTL_FORCE_DUPLEX |
			MV88E6XXX_PORT_MAC_CTL_DUPLEX_FULL;
		break;
	case DUPLEX_UNFORCED:
		/* normal duplex detection */
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_MAC_CTL, &reg);
	if (err)
		return err;

	reg &= ~(MV88E6XXX_PORT_MAC_CTL_SPEED_MASK |
		 MV88E6390_PORT_MAC_CTL_ALTSPEED |
		 MV88E6390_PORT_MAC_CTL_FORCE_SPEED);

	if (speed != SPEED_UNFORCED)
		reg |= MV88E6390_PORT_MAC_CTL_FORCE_SPEED;

	reg |= ctrl;

	err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_MAC_CTL, reg);
	if (err)
		return err;

	if (speed != SPEED_UNFORCED)
		dev_dbg(chip->dev, "p%d: Speed set to %d Mbps\n", port, speed);
	else
		dev_dbg(chip->dev, "p%d: Speed unforced\n", port);
	dev_dbg(chip->dev, "p%d: %s %s duplex\n", port,
		reg & MV88E6XXX_PORT_MAC_CTL_FORCE_DUPLEX ? "Force" : "Unforce",
		reg & MV88E6XXX_PORT_MAC_CTL_DUPLEX_FULL ? "full" : "half");

	return 0;
}

phy_interface_t mv88e6393x_port_max_speed_mode(struct mv88e6xxx_chip *chip,
					       int port)
{

	if (port != 0 && port != 9 && port != 10)
		return PHY_INTERFACE_MODE_NA;

	if (chip->info->prod_num == MV88E6XXX_PORT_SWITCH_ID_PROD_6361)
		return PHY_INTERFACE_MODE_2500BASEX;

	return PHY_INTERFACE_MODE_10GBASER;
}

static int mv88e6xxx_port_set_cmode(struct mv88e6xxx_chip *chip, int port,
				    phy_interface_t mode, bool force)
{
	u16 cmode;
	u16 reg;
	int err;

	/* Default to a slow mode, so freeing up SERDES interfaces for
	 * other ports which might use them for SFPs.
	 */
	if (mode == PHY_INTERFACE_MODE_NA)
		mode = PHY_INTERFACE_MODE_1000BASEX;

	switch (mode) {
	case PHY_INTERFACE_MODE_RMII:
		cmode = MV88E6XXX_PORT_STS_CMODE_RMII;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		cmode = MV88E6XXX_PORT_STS_CMODE_RGMII;
		break;
	case PHY_INTERFACE_MODE_1000BASEX:
		cmode = MV88E6XXX_PORT_STS_CMODE_1000BASEX;
		break;
	case PHY_INTERFACE_MODE_SGMII:
		cmode = MV88E6XXX_PORT_STS_CMODE_SGMII;
		break;
	case PHY_INTERFACE_MODE_2500BASEX:
		cmode = MV88E6XXX_PORT_STS_CMODE_2500BASEX;
		break;
	case PHY_INTERFACE_MODE_5GBASER:
		cmode = MV88E6393X_PORT_STS_CMODE_5GBASER;
		break;
	case PHY_INTERFACE_MODE_XGMII:
	case PHY_INTERFACE_MODE_XAUI:
		cmode = MV88E6XXX_PORT_STS_CMODE_XAUI;
		break;
	case PHY_INTERFACE_MODE_RXAUI:
		cmode = MV88E6XXX_PORT_STS_CMODE_RXAUI;
		break;
	case PHY_INTERFACE_MODE_10GBASER:
		cmode = MV88E6393X_PORT_STS_CMODE_10GBASER;
		break;
	case PHY_INTERFACE_MODE_USXGMII:
		cmode = MV88E6393X_PORT_STS_CMODE_USXGMII;
		break;
	default:
		cmode = 0;
	}

	/* cmode doesn't change, nothing to do for us unless forced */
	if (cmode == chip->ports[port].cmode && !force)
		return 0;

	chip->ports[port].cmode = 0;

	if (cmode) {
		err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_STS, &reg);
		if (err)
			return err;

		reg &= ~MV88E6XXX_PORT_STS_CMODE_MASK;
		reg |= cmode;

		err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_STS, reg);
		if (err)
			return err;

		chip->ports[port].cmode = cmode;
	}

	return 0;
}

int mv88e6390x_port_set_cmode(struct mv88e6xxx_chip *chip, int port,
			      phy_interface_t mode)
{
	if (port != 9 && port != 10)
		return -EOPNOTSUPP;

	return mv88e6xxx_port_set_cmode(chip, port, mode, false);
}

int mv88e6390_port_set_cmode(struct mv88e6xxx_chip *chip, int port,
			     phy_interface_t mode)
{
	if (port != 9 && port != 10)
		return -EOPNOTSUPP;

	switch (mode) {
	case PHY_INTERFACE_MODE_NA:
		return 0;
	case PHY_INTERFACE_MODE_XGMII:
	case PHY_INTERFACE_MODE_XAUI:
	case PHY_INTERFACE_MODE_RXAUI:
		return -EINVAL;
	default:
		break;
	}

	return mv88e6xxx_port_set_cmode(chip, port, mode, false);
}

int mv88e6393x_port_set_cmode(struct mv88e6xxx_chip *chip, int port,
			      phy_interface_t mode)
{
	int err;
	u16 reg;

	if (port != 0 && port != 9 && port != 10)
		return -EOPNOTSUPP;

	if (port == 9 || port == 10) {
		switch (mode) {
		case PHY_INTERFACE_MODE_RMII:
		case PHY_INTERFACE_MODE_RGMII:
		case PHY_INTERFACE_MODE_RGMII_ID:
		case PHY_INTERFACE_MODE_RGMII_RXID:
		case PHY_INTERFACE_MODE_RGMII_TXID:
			return -EINVAL;
		default:
			break;
		}
	}

	/* mv88e6393x errata 4.5: EEE should be disabled on SERDES ports */
	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_MAC_CTL, &reg);
	if (err)
		return err;

	reg &= ~MV88E6XXX_PORT_MAC_CTL_EEE;
	reg |= MV88E6XXX_PORT_MAC_CTL_FORCE_EEE;
	err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_MAC_CTL, reg);
	if (err)
		return err;

	return mv88e6xxx_port_set_cmode(chip, port, mode, false);
}

static int mv88e6341_port_set_cmode_writable(struct mv88e6xxx_chip *chip,
					     int port)
{
	int err, addr;
	u16 reg, bits;

	if (port != 5)
		return -EOPNOTSUPP;

	addr = chip->info->port_base_addr + port;

	err = mv88e6xxx_port_hidden_read(chip, 0x7, addr, 0, &reg);
	if (err)
		return err;

	bits = MV88E6341_PORT_RESERVED_1A_FORCE_CMODE |
	       MV88E6341_PORT_RESERVED_1A_SGMII_AN;

	if ((reg & bits) == bits)
		return 0;

	reg |= bits;
	return mv88e6xxx_port_hidden_write(chip, 0x7, addr, 0, reg);
}

int mv88e6341_port_set_cmode(struct mv88e6xxx_chip *chip, int port,
			     phy_interface_t mode)
{
	int err;

	if (port != 5)
		return -EOPNOTSUPP;

	switch (mode) {
	case PHY_INTERFACE_MODE_NA:
		return 0;
	case PHY_INTERFACE_MODE_XGMII:
	case PHY_INTERFACE_MODE_XAUI:
	case PHY_INTERFACE_MODE_RXAUI:
		return -EINVAL;
	default:
		break;
	}

	err = mv88e6341_port_set_cmode_writable(chip, port);
	if (err)
		return err;

	return mv88e6xxx_port_set_cmode(chip, port, mode, true);
}

int mv88e6185_port_get_cmode(struct mv88e6xxx_chip *chip, int port, u8 *cmode)
{
	int err;
	u16 reg;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_STS, &reg);
	if (err)
		return err;

	*cmode = reg & MV88E6185_PORT_STS_CMODE_MASK;

	return 0;
}

int mv88e6352_port_get_cmode(struct mv88e6xxx_chip *chip, int port, u8 *cmode)
{
	int err;
	u16 reg;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_STS, &reg);
	if (err)
		return err;

	*cmode = reg & MV88E6XXX_PORT_STS_CMODE_MASK;

	return 0;
}

/* Offset 0x02: Jamming Control
 *
 * Do not limit the period of time that this port can be paused for by
 * the remote end or the period of time that this port can pause the
 * remote end.
 */
int mv88e6097_port_pause_limit(struct mv88e6xxx_chip *chip, int port, u8 in,
			       u8 out)
{
	return mv88e6xxx_port_write(chip, port, MV88E6097_PORT_JAM_CTL,
				    out << 8 | in);
}

int mv88e6390_port_pause_limit(struct mv88e6xxx_chip *chip, int port, u8 in,
			       u8 out)
{
	int err;

	err = mv88e6xxx_port_write(chip, port, MV88E6390_PORT_FLOW_CTL,
				   MV88E6390_PORT_FLOW_CTL_UPDATE |
				   MV88E6390_PORT_FLOW_CTL_LIMIT_IN | in);
	if (err)
		return err;

	return mv88e6xxx_port_write(chip, port, MV88E6390_PORT_FLOW_CTL,
				    MV88E6390_PORT_FLOW_CTL_UPDATE |
				    MV88E6390_PORT_FLOW_CTL_LIMIT_OUT | out);
}

/* Offset 0x04: Port Control Register */

static const char * const mv88e6xxx_port_state_names[] = {
	[MV88E6XXX_PORT_CTL0_STATE_DISABLED] = "Disabled",
	[MV88E6XXX_PORT_CTL0_STATE_BLOCKING] = "Blocking/Listening",
	[MV88E6XXX_PORT_CTL0_STATE_LEARNING] = "Learning",
	[MV88E6XXX_PORT_CTL0_STATE_FORWARDING] = "Forwarding",
};

int mv88e6xxx_port_set_state(struct mv88e6xxx_chip *chip, int port, u8 state)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL0, &reg);
	if (err)
		return err;

	reg &= ~MV88E6XXX_PORT_CTL0_STATE_MASK;

	switch (state) {
	case BR_STATE_DISABLED:
		state = MV88E6XXX_PORT_CTL0_STATE_DISABLED;
		break;
	case BR_STATE_BLOCKING:
	case BR_STATE_LISTENING:
		state = MV88E6XXX_PORT_CTL0_STATE_BLOCKING;
		break;
	case BR_STATE_LEARNING:
		state = MV88E6XXX_PORT_CTL0_STATE_LEARNING;
		break;
	case BR_STATE_FORWARDING:
		state = MV88E6XXX_PORT_CTL0_STATE_FORWARDING;
		break;
	default:
		return -EINVAL;
	}

	reg |= state;

	err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL0, reg);
	if (err)
		return err;

	dev_dbg(chip->dev, "p%d: PortState set to %s\n", port,
		mv88e6xxx_port_state_names[state]);

	return 0;
}

int mv88e6xxx_port_set_egress_mode(struct mv88e6xxx_chip *chip, int port,
				   enum mv88e6xxx_egress_mode mode)
{
	int err;
	u16 reg;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL0, &reg);
	if (err)
		return err;

	reg &= ~MV88E6XXX_PORT_CTL0_EGRESS_MODE_MASK;

	switch (mode) {
	case MV88E6XXX_EGRESS_MODE_UNMODIFIED:
		reg |= MV88E6XXX_PORT_CTL0_EGRESS_MODE_UNMODIFIED;
		break;
	case MV88E6XXX_EGRESS_MODE_UNTAGGED:
		reg |= MV88E6XXX_PORT_CTL0_EGRESS_MODE_UNTAGGED;
		break;
	case MV88E6XXX_EGRESS_MODE_TAGGED:
		reg |= MV88E6XXX_PORT_CTL0_EGRESS_MODE_TAGGED;
		break;
	case MV88E6XXX_EGRESS_MODE_ETHERTYPE:
		reg |= MV88E6XXX_PORT_CTL0_EGRESS_MODE_ETHER_TYPE_DSA;
		break;
	default:
		return -EINVAL;
	}

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL0, reg);
}

int mv88e6085_port_set_frame_mode(struct mv88e6xxx_chip *chip, int port,
				  enum mv88e6xxx_frame_mode mode)
{
	int err;
	u16 reg;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL0, &reg);
	if (err)
		return err;

	reg &= ~MV88E6XXX_PORT_CTL0_FRAME_MODE_MASK;

	switch (mode) {
	case MV88E6XXX_FRAME_MODE_NORMAL:
		reg |= MV88E6XXX_PORT_CTL0_FRAME_MODE_NORMAL;
		break;
	case MV88E6XXX_FRAME_MODE_DSA:
		reg |= MV88E6XXX_PORT_CTL0_FRAME_MODE_DSA;
		break;
	default:
		return -EINVAL;
	}

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL0, reg);
}

int mv88e6351_port_set_frame_mode(struct mv88e6xxx_chip *chip, int port,
				  enum mv88e6xxx_frame_mode mode)
{
	int err;
	u16 reg;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL0, &reg);
	if (err)
		return err;

	reg &= ~MV88E6XXX_PORT_CTL0_FRAME_MODE_MASK;

	switch (mode) {
	case MV88E6XXX_FRAME_MODE_NORMAL:
		reg |= MV88E6XXX_PORT_CTL0_FRAME_MODE_NORMAL;
		break;
	case MV88E6XXX_FRAME_MODE_DSA:
		reg |= MV88E6XXX_PORT_CTL0_FRAME_MODE_DSA;
		break;
	case MV88E6XXX_FRAME_MODE_PROVIDER:
		reg |= MV88E6XXX_PORT_CTL0_FRAME_MODE_PROVIDER;
		break;
	case MV88E6XXX_FRAME_MODE_ETHERTYPE:
		reg |= MV88E6XXX_PORT_CTL0_FRAME_MODE_ETHER_TYPE_DSA;
		break;
	default:
		return -EINVAL;
	}

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL0, reg);
}

int mv88e6185_port_set_forward_unknown(struct mv88e6xxx_chip *chip,
				       int port, bool unicast)
{
	int err;
	u16 reg;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL0, &reg);
	if (err)
		return err;

	if (unicast)
		reg |= MV88E6185_PORT_CTL0_FORWARD_UNKNOWN;
	else
		reg &= ~MV88E6185_PORT_CTL0_FORWARD_UNKNOWN;

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL0, reg);
}

int mv88e6352_port_set_ucast_flood(struct mv88e6xxx_chip *chip, int port,
				   bool unicast)
{
	int err;
	u16 reg;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL0, &reg);
	if (err)
		return err;

	if (unicast)
		reg |= MV88E6352_PORT_CTL0_EGRESS_FLOODS_UC;
	else
		reg &= ~MV88E6352_PORT_CTL0_EGRESS_FLOODS_UC;

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL0, reg);
}

int mv88e6352_port_set_mcast_flood(struct mv88e6xxx_chip *chip, int port,
				   bool multicast)
{
	int err;
	u16 reg;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL0, &reg);
	if (err)
		return err;

	if (multicast)
		reg |= MV88E6352_PORT_CTL0_EGRESS_FLOODS_MC;
	else
		reg &= ~MV88E6352_PORT_CTL0_EGRESS_FLOODS_MC;

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL0, reg);
}

/* Offset 0x05: Port Control 1 */

int mv88e6xxx_port_set_message_port(struct mv88e6xxx_chip *chip, int port,
				    bool message_port)
{
	u16 val;
	int err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL1, &val);
	if (err)
		return err;

	if (message_port)
		val |= MV88E6XXX_PORT_CTL1_MESSAGE_PORT;
	else
		val &= ~MV88E6XXX_PORT_CTL1_MESSAGE_PORT;

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL1, val);
}

int mv88e6xxx_port_set_trunk(struct mv88e6xxx_chip *chip, int port,
			     bool trunk, u8 id)
{
	u16 val;
	int err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL1, &val);
	if (err)
		return err;

	val &= ~MV88E6XXX_PORT_CTL1_TRUNK_ID_MASK;

	if (trunk)
		val |= MV88E6XXX_PORT_CTL1_TRUNK_PORT |
			(id << MV88E6XXX_PORT_CTL1_TRUNK_ID_SHIFT);
	else
		val &= ~MV88E6XXX_PORT_CTL1_TRUNK_PORT;

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL1, val);
}

/* Offset 0x06: Port Based VLAN Map */

int mv88e6xxx_port_set_vlan_map(struct mv88e6xxx_chip *chip, int port, u16 map)
{
	const u16 mask = mv88e6xxx_port_mask(chip);
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_BASE_VLAN, &reg);
	if (err)
		return err;

	reg &= ~mask;
	reg |= map & mask;

	err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_BASE_VLAN, reg);
	if (err)
		return err;

	dev_dbg(chip->dev, "p%d: VLANTable set to %.3x\n", port, map);

	return 0;
}

int mv88e6xxx_port_get_fid(struct mv88e6xxx_chip *chip, int port, u16 *fid)
{
	const u16 upper_mask = (mv88e6xxx_num_databases(chip) - 1) >> 4;
	u16 reg;
	int err;

	/* Port's default FID lower 4 bits are located in reg 0x06, offset 12 */
	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_BASE_VLAN, &reg);
	if (err)
		return err;

	*fid = (reg & 0xf000) >> 12;

	/* Port's default FID upper bits are located in reg 0x05, offset 0 */
	if (upper_mask) {
		err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL1,
					  &reg);
		if (err)
			return err;

		*fid |= (reg & upper_mask) << 4;
	}

	return 0;
}

int mv88e6xxx_port_set_fid(struct mv88e6xxx_chip *chip, int port, u16 fid)
{
	const u16 upper_mask = (mv88e6xxx_num_databases(chip) - 1) >> 4;
	u16 reg;
	int err;

	if (fid >= mv88e6xxx_num_databases(chip))
		return -EINVAL;

	/* Port's default FID lower 4 bits are located in reg 0x06, offset 12 */
	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_BASE_VLAN, &reg);
	if (err)
		return err;

	reg &= 0x0fff;
	reg |= (fid & 0x000f) << 12;

	err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_BASE_VLAN, reg);
	if (err)
		return err;

	/* Port's default FID upper bits are located in reg 0x05, offset 0 */
	if (upper_mask) {
		err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL1,
					  &reg);
		if (err)
			return err;

		reg &= ~upper_mask;
		reg |= (fid >> 4) & upper_mask;

		err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL1,
					   reg);
		if (err)
			return err;
	}

	dev_dbg(chip->dev, "p%d: FID set to %u\n", port, fid);

	return 0;
}

/* Offset 0x07: Default Port VLAN ID & Priority */

int mv88e6xxx_port_get_pvid(struct mv88e6xxx_chip *chip, int port, u16 *pvid)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_DEFAULT_VLAN,
				  &reg);
	if (err)
		return err;

	*pvid = reg & MV88E6XXX_PORT_DEFAULT_VLAN_MASK;

	return 0;
}

int mv88e6xxx_port_set_pvid(struct mv88e6xxx_chip *chip, int port, u16 pvid)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_DEFAULT_VLAN,
				  &reg);
	if (err)
		return err;

	reg &= ~MV88E6XXX_PORT_DEFAULT_VLAN_MASK;
	reg |= pvid & MV88E6XXX_PORT_DEFAULT_VLAN_MASK;

	err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_DEFAULT_VLAN,
				   reg);
	if (err)
		return err;

	dev_dbg(chip->dev, "p%d: DefaultVID set to %u\n", port, pvid);

	return 0;
}

/* Offset 0x08: Port Control 2 Register */

static const char * const mv88e6xxx_port_8021q_mode_names[] = {
	[MV88E6XXX_PORT_CTL2_8021Q_MODE_DISABLED] = "Disabled",
	[MV88E6XXX_PORT_CTL2_8021Q_MODE_FALLBACK] = "Fallback",
	[MV88E6XXX_PORT_CTL2_8021Q_MODE_CHECK] = "Check",
	[MV88E6XXX_PORT_CTL2_8021Q_MODE_SECURE] = "Secure",
};

int mv88e6185_port_set_default_forward(struct mv88e6xxx_chip *chip,
				       int port, bool multicast)
{
	int err;
	u16 reg;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL2, &reg);
	if (err)
		return err;

	if (multicast)
		reg |= MV88E6XXX_PORT_CTL2_DEFAULT_FORWARD;
	else
		reg &= ~MV88E6XXX_PORT_CTL2_DEFAULT_FORWARD;

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL2, reg);
}

int mv88e6095_port_set_upstream_port(struct mv88e6xxx_chip *chip, int port,
				     int upstream_port)
{
	int err;
	u16 reg;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL2, &reg);
	if (err)
		return err;

	reg &= ~MV88E6095_PORT_CTL2_CPU_PORT_MASK;
	reg |= upstream_port;

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL2, reg);
}

int mv88e6xxx_port_set_mirror(struct mv88e6xxx_chip *chip, int port,
			      enum mv88e6xxx_egress_direction direction,
			      bool mirror)
{
	bool *mirror_port;
	u16 reg;
	u16 bit;
	int err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL2, &reg);
	if (err)
		return err;

	switch (direction) {
	case MV88E6XXX_EGRESS_DIR_INGRESS:
		bit = MV88E6XXX_PORT_CTL2_INGRESS_MONITOR;
		mirror_port = &chip->ports[port].mirror_ingress;
		break;
	case MV88E6XXX_EGRESS_DIR_EGRESS:
		bit = MV88E6XXX_PORT_CTL2_EGRESS_MONITOR;
		mirror_port = &chip->ports[port].mirror_egress;
		break;
	default:
		return -EINVAL;
	}

	reg &= ~bit;
	if (mirror)
		reg |= bit;

	err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL2, reg);
	if (!err)
		*mirror_port = mirror;

	return err;
}

int mv88e6xxx_port_set_lock(struct mv88e6xxx_chip *chip, int port,
			    bool locked)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL0, &reg);
	if (err)
		return err;

	reg &= ~MV88E6XXX_PORT_CTL0_SA_FILT_MASK;
	if (locked)
		reg |= MV88E6XXX_PORT_CTL0_SA_FILT_DROP_ON_LOCK;

	err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL0, reg);
	if (err)
		return err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_ASSOC_VECTOR, &reg);
	if (err)
		return err;

	reg &= ~MV88E6XXX_PORT_ASSOC_VECTOR_LOCKED_PORT;
	if (locked)
		reg |= MV88E6XXX_PORT_ASSOC_VECTOR_LOCKED_PORT;

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_ASSOC_VECTOR, reg);
}

int mv88e6xxx_port_set_8021q_mode(struct mv88e6xxx_chip *chip, int port,
				  u16 mode)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL2, &reg);
	if (err)
		return err;

	reg &= ~MV88E6XXX_PORT_CTL2_8021Q_MODE_MASK;
	reg |= mode & MV88E6XXX_PORT_CTL2_8021Q_MODE_MASK;

	err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL2, reg);
	if (err)
		return err;

	dev_dbg(chip->dev, "p%d: 802.1QMode set to %s\n", port,
		mv88e6xxx_port_8021q_mode_names[mode]);

	return 0;
}

int mv88e6xxx_port_drop_untagged(struct mv88e6xxx_chip *chip, int port,
				 bool drop_untagged)
{
	u16 old, new;
	int err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL2, &old);
	if (err)
		return err;

	if (drop_untagged)
		new = old | MV88E6XXX_PORT_CTL2_DISCARD_UNTAGGED;
	else
		new = old & ~MV88E6XXX_PORT_CTL2_DISCARD_UNTAGGED;

	if (new == old)
		return 0;

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL2, new);
}

int mv88e6xxx_port_set_map_da(struct mv88e6xxx_chip *chip, int port, bool map)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL2, &reg);
	if (err)
		return err;

	if (map)
		reg |= MV88E6XXX_PORT_CTL2_MAP_DA;
	else
		reg &= ~MV88E6XXX_PORT_CTL2_MAP_DA;

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL2, reg);
}

int mv88e6165_port_set_jumbo_size(struct mv88e6xxx_chip *chip, int port,
				  size_t size)
{
	u16 reg;
	int err;

	size += VLAN_ETH_HLEN + ETH_FCS_LEN;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_CTL2, &reg);
	if (err)
		return err;

	reg &= ~MV88E6XXX_PORT_CTL2_JUMBO_MODE_MASK;

	if (size <= 1522)
		reg |= MV88E6XXX_PORT_CTL2_JUMBO_MODE_1522;
	else if (size <= 2048)
		reg |= MV88E6XXX_PORT_CTL2_JUMBO_MODE_2048;
	else if (size <= 10240)
		reg |= MV88E6XXX_PORT_CTL2_JUMBO_MODE_10240;
	else
		return -ERANGE;

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL2, reg);
}

/* Offset 0x09: Port Rate Control */

int mv88e6095_port_egress_rate_limiting(struct mv88e6xxx_chip *chip, int port)
{
	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_EGRESS_RATE_CTL1,
				    0x0000);
}

int mv88e6097_port_egress_rate_limiting(struct mv88e6xxx_chip *chip, int port)
{
	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_EGRESS_RATE_CTL1,
				    0x0001);
}

/* Offset 0x0B: Port Association Vector */

int mv88e6xxx_port_set_assoc_vector(struct mv88e6xxx_chip *chip, int port,
				    u16 pav)
{
	u16 reg, mask;
	int err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_ASSOC_VECTOR,
				  &reg);
	if (err)
		return err;

	mask = mv88e6xxx_port_mask(chip);
	reg &= ~mask;
	reg |= pav & mask;

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_ASSOC_VECTOR,
				    reg);
}

/* Offset 0x0C: Port ATU Control */

int mv88e6xxx_port_disable_learn_limit(struct mv88e6xxx_chip *chip, int port)
{
	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_ATU_CTL, 0);
}

/* Offset 0x0D: (Priority) Override Register */

int mv88e6xxx_port_disable_pri_override(struct mv88e6xxx_chip *chip, int port)
{
	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_PRI_OVERRIDE, 0);
}

/* Offset 0x0E: Policy & MGMT Control Register for FAMILY 6191X 6193X 6393X */

static int mv88e6393x_port_policy_read(struct mv88e6xxx_chip *chip, int port,
				       u16 pointer, u8 *data)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_write(chip, port, MV88E6393X_PORT_POLICY_MGMT_CTL,
				   pointer);
	if (err)
		return err;

	err = mv88e6xxx_port_read(chip, port, MV88E6393X_PORT_POLICY_MGMT_CTL,
				  &reg);
	if (err)
		return err;

	*data = reg;

	return 0;
}

static int mv88e6393x_port_policy_write(struct mv88e6xxx_chip *chip, int port,
					u16 pointer, u8 data)
{
	u16 reg;

	reg = MV88E6393X_PORT_POLICY_MGMT_CTL_UPDATE | pointer | data;

	return mv88e6xxx_port_write(chip, port, MV88E6393X_PORT_POLICY_MGMT_CTL,
				    reg);
}

static int mv88e6393x_port_policy_write_all(struct mv88e6xxx_chip *chip,
					    u16 pointer, u8 data)
{
	int err, port;

	for (port = 0; port < mv88e6xxx_num_ports(chip); port++) {
		if (dsa_is_unused_port(chip->ds, port))
			continue;

		err = mv88e6393x_port_policy_write(chip, port, pointer, data);
		if (err)
			return err;
	}

	return 0;
}

int mv88e6393x_set_egress_port(struct mv88e6xxx_chip *chip,
			       enum mv88e6xxx_egress_direction direction,
			       int port)
{
	u16 ptr;
	int err;

	switch (direction) {
	case MV88E6XXX_EGRESS_DIR_INGRESS:
		ptr = MV88E6393X_PORT_POLICY_MGMT_CTL_PTR_INGRESS_DEST;
		err = mv88e6393x_port_policy_write_all(chip, ptr, port);
		if (err)
			return err;
		break;
	case MV88E6XXX_EGRESS_DIR_EGRESS:
		ptr = MV88E6393X_G2_EGRESS_MONITOR_DEST;
		err = mv88e6xxx_g2_write(chip, ptr, port);
		if (err)
			return err;
		break;
	}

	return 0;
}

int mv88e6393x_port_set_upstream_port(struct mv88e6xxx_chip *chip, int port,
				      int upstream_port)
{
	u16 ptr = MV88E6393X_PORT_POLICY_MGMT_CTL_PTR_CPU_DEST;
	u8 data = MV88E6393X_PORT_POLICY_MGMT_CTL_CPU_DEST_MGMTPRI |
		  upstream_port;

	return mv88e6393x_port_policy_write(chip, port, ptr, data);
}

int mv88e6393x_port_mgmt_rsvd2cpu(struct mv88e6xxx_chip *chip)
{
	u16 ptr;
	int err;

	/* Consider the frames with reserved multicast destination
	 * addresses matching 01:80:c2:00:00:00 and
	 * 01:80:c2:00:00:02 as MGMT.
	 */
	ptr = MV88E6393X_PORT_POLICY_MGMT_CTL_PTR_01C280000000XLO;
	err = mv88e6393x_port_policy_write_all(chip, ptr, 0xff);
	if (err)
		return err;

	ptr = MV88E6393X_PORT_POLICY_MGMT_CTL_PTR_01C280000000XHI;
	err = mv88e6393x_port_policy_write_all(chip, ptr, 0xff);
	if (err)
		return err;

	ptr = MV88E6393X_PORT_POLICY_MGMT_CTL_PTR_01C280000002XLO;
	err = mv88e6393x_port_policy_write_all(chip, ptr, 0xff);
	if (err)
		return err;

	ptr = MV88E6393X_PORT_POLICY_MGMT_CTL_PTR_01C280000002XHI;
	err = mv88e6393x_port_policy_write_all(chip, ptr, 0xff);
	if (err)
		return err;

	return 0;
}

/* Offset 0x10 & 0x11: EPC */

static int mv88e6393x_port_epc_wait_ready(struct mv88e6xxx_chip *chip, int port)
{
	int bit = __bf_shf(MV88E6393X_PORT_EPC_CMD_BUSY);

	return mv88e6xxx_port_wait_bit(chip, port, MV88E6393X_PORT_EPC_CMD, bit, 0);
}

/* Port Ether type for 6393X family */

int mv88e6393x_port_set_ether_type(struct mv88e6xxx_chip *chip, int port,
				   u16 etype)
{
	u16 val;
	int err;

	err = mv88e6393x_port_epc_wait_ready(chip, port);
	if (err)
		return err;

	err = mv88e6xxx_port_write(chip, port, MV88E6393X_PORT_EPC_DATA, etype);
	if (err)
		return err;

	val = MV88E6393X_PORT_EPC_CMD_BUSY |
	      MV88E6393X_PORT_EPC_CMD_WRITE |
	      MV88E6393X_PORT_EPC_INDEX_PORT_ETYPE;

	return mv88e6xxx_port_write(chip, port, MV88E6393X_PORT_EPC_CMD, val);
}

/* Offset 0x0f: Port Ether type */

int mv88e6351_port_set_ether_type(struct mv88e6xxx_chip *chip, int port,
				  u16 etype)
{
	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_ETH_TYPE, etype);
}

/* Offset 0x18: Port IEEE Priority Remapping Registers [0-3]
 * Offset 0x19: Port IEEE Priority Remapping Registers [4-7]
 */

int mv88e6095_port_tag_remap(struct mv88e6xxx_chip *chip, int port)
{
	int err;

	/* Use a direct priority mapping for all IEEE tagged frames */
	err = mv88e6xxx_port_write(chip, port,
				   MV88E6095_PORT_IEEE_PRIO_REMAP_0123,
				   0x3210);
	if (err)
		return err;

	return mv88e6xxx_port_write(chip, port,
				    MV88E6095_PORT_IEEE_PRIO_REMAP_4567,
				    0x7654);
}

static int mv88e6xxx_port_ieeepmt_write(struct mv88e6xxx_chip *chip,
					int port, u16 table, u8 ptr, u16 data)
{
	u16 reg;

	reg = MV88E6390_PORT_IEEE_PRIO_MAP_TABLE_UPDATE | table |
		(ptr << __bf_shf(MV88E6390_PORT_IEEE_PRIO_MAP_TABLE_PTR_MASK)) |
		(data & MV88E6390_PORT_IEEE_PRIO_MAP_TABLE_DATA_MASK);

	return mv88e6xxx_port_write(chip, port,
				    MV88E6390_PORT_IEEE_PRIO_MAP_TABLE, reg);
}

int mv88e6390_port_tag_remap(struct mv88e6xxx_chip *chip, int port)
{
	int err, i;
	u16 table;

	for (i = 0; i <= 7; i++) {
		table = MV88E6390_PORT_IEEE_PRIO_MAP_TABLE_INGRESS_PCP;
		err = mv88e6xxx_port_ieeepmt_write(chip, port, table, i,
						   (i | i << 4));
		if (err)
			return err;

		table = MV88E6390_PORT_IEEE_PRIO_MAP_TABLE_EGRESS_GREEN_PCP;
		err = mv88e6xxx_port_ieeepmt_write(chip, port, table, i, i);
		if (err)
			return err;

		table = MV88E6390_PORT_IEEE_PRIO_MAP_TABLE_EGRESS_YELLOW_PCP;
		err = mv88e6xxx_port_ieeepmt_write(chip, port, table, i, i);
		if (err)
			return err;

		table = MV88E6390_PORT_IEEE_PRIO_MAP_TABLE_EGRESS_AVB_PCP;
		err = mv88e6xxx_port_ieeepmt_write(chip, port, table, i, i);
		if (err)
			return err;
	}

	return 0;
}

/* Offset 0x0E: Policy Control Register */

static int
mv88e6xxx_port_policy_mapping_get_pos(enum mv88e6xxx_policy_mapping mapping,
				      enum mv88e6xxx_policy_action action,
				      u16 *mask, u16 *val, int *shift)
{
	switch (mapping) {
	case MV88E6XXX_POLICY_MAPPING_DA:
		*shift = __bf_shf(MV88E6XXX_PORT_POLICY_CTL_DA_MASK);
		*mask = MV88E6XXX_PORT_POLICY_CTL_DA_MASK;
		break;
	case MV88E6XXX_POLICY_MAPPING_SA:
		*shift = __bf_shf(MV88E6XXX_PORT_POLICY_CTL_SA_MASK);
		*mask = MV88E6XXX_PORT_POLICY_CTL_SA_MASK;
		break;
	case MV88E6XXX_POLICY_MAPPING_VTU:
		*shift = __bf_shf(MV88E6XXX_PORT_POLICY_CTL_VTU_MASK);
		*mask = MV88E6XXX_PORT_POLICY_CTL_VTU_MASK;
		break;
	case MV88E6XXX_POLICY_MAPPING_ETYPE:
		*shift = __bf_shf(MV88E6XXX_PORT_POLICY_CTL_ETYPE_MASK);
		*mask = MV88E6XXX_PORT_POLICY_CTL_ETYPE_MASK;
		break;
	case MV88E6XXX_POLICY_MAPPING_PPPOE:
		*shift = __bf_shf(MV88E6XXX_PORT_POLICY_CTL_PPPOE_MASK);
		*mask = MV88E6XXX_PORT_POLICY_CTL_PPPOE_MASK;
		break;
	case MV88E6XXX_POLICY_MAPPING_VBAS:
		*shift = __bf_shf(MV88E6XXX_PORT_POLICY_CTL_VBAS_MASK);
		*mask = MV88E6XXX_PORT_POLICY_CTL_VBAS_MASK;
		break;
	case MV88E6XXX_POLICY_MAPPING_OPT82:
		*shift = __bf_shf(MV88E6XXX_PORT_POLICY_CTL_OPT82_MASK);
		*mask = MV88E6XXX_PORT_POLICY_CTL_OPT82_MASK;
		break;
	case MV88E6XXX_POLICY_MAPPING_UDP:
		*shift = __bf_shf(MV88E6XXX_PORT_POLICY_CTL_UDP_MASK);
		*mask = MV88E6XXX_PORT_POLICY_CTL_UDP_MASK;
		break;
	default:
		return -EOPNOTSUPP;
	}

	switch (action) {
	case MV88E6XXX_POLICY_ACTION_NORMAL:
		*val = MV88E6XXX_PORT_POLICY_CTL_NORMAL;
		break;
	case MV88E6XXX_POLICY_ACTION_MIRROR:
		*val = MV88E6XXX_PORT_POLICY_CTL_MIRROR;
		break;
	case MV88E6XXX_POLICY_ACTION_TRAP:
		*val = MV88E6XXX_PORT_POLICY_CTL_TRAP;
		break;
	case MV88E6XXX_POLICY_ACTION_DISCARD:
		*val = MV88E6XXX_PORT_POLICY_CTL_DISCARD;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

int mv88e6352_port_set_policy(struct mv88e6xxx_chip *chip, int port,
			      enum mv88e6xxx_policy_mapping mapping,
			      enum mv88e6xxx_policy_action action)
{
	u16 reg, mask, val;
	int shift;
	int err;

	err = mv88e6xxx_port_policy_mapping_get_pos(mapping, action, &mask,
						    &val, &shift);
	if (err)
		return err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_POLICY_CTL, &reg);
	if (err)
		return err;

	reg &= ~mask;
	reg |= (val << shift) & mask;

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_POLICY_CTL, reg);
}

int mv88e6393x_port_set_policy(struct mv88e6xxx_chip *chip, int port,
			       enum mv88e6xxx_policy_mapping mapping,
			       enum mv88e6xxx_policy_action action)
{
	u16 mask, val;
	int shift;
	int err;
	u16 ptr;
	u8 reg;

	err = mv88e6xxx_port_policy_mapping_get_pos(mapping, action, &mask,
						    &val, &shift);
	if (err)
		return err;

	/* The 16-bit Port Policy CTL register from older chips is on 6393x
	 * changed to Port Policy MGMT CTL, which can access more data, but
	 * indirectly. The original 16-bit value is divided into two 8-bit
	 * registers.
	 */
	ptr = shift / 8;
	shift %= 8;
	mask >>= ptr * 8;
	ptr <<= 8;

	err = mv88e6393x_port_policy_read(chip, port, ptr, &reg);
	if (err)
		return err;

	reg &= ~mask;
	reg |= (val << shift) & mask;

	return mv88e6393x_port_policy_write(chip, port, ptr, reg);
}
