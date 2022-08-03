// SPDX-License-Identifier: GPL-2.0
/* NXP TJA1100 BroadRReach PHY driver
 *
 * Copyright (C) 2018 Marek Vasut <marex@denx.de>
 */
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/ethtool_netlink.h>
#include <linux/kernel.h>
#include <linux/mdio.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/hwmon.h>
#include <linux/bitfield.h>
#include <linux/of_mdio.h>
#include <linux/of_irq.h>

#define PHY_ID_MASK			0xfffffff0
#define PHY_ID_TJA1100			0x0180dc40
#define PHY_ID_TJA1101			0x0180dd00
#define PHY_ID_TJA1102			0x0180dc80

#define MII_ECTRL			17
#define MII_ECTRL_LINK_CONTROL		BIT(15)
#define MII_ECTRL_POWER_MODE_MASK	GENMASK(14, 11)
#define MII_ECTRL_POWER_MODE_NO_CHANGE	(0x0 << 11)
#define MII_ECTRL_POWER_MODE_NORMAL	(0x3 << 11)
#define MII_ECTRL_POWER_MODE_STANDBY	(0xc << 11)
#define MII_ECTRL_CABLE_TEST		BIT(5)
#define MII_ECTRL_CONFIG_EN		BIT(2)
#define MII_ECTRL_WAKE_REQUEST		BIT(0)

#define MII_CFG1			18
#define MII_CFG1_MASTER_SLAVE		BIT(15)
#define MII_CFG1_AUTO_OP		BIT(14)
#define MII_CFG1_SLEEP_CONFIRM		BIT(6)
#define MII_CFG1_LED_MODE_MASK		GENMASK(5, 4)
#define MII_CFG1_LED_MODE_LINKUP	0
#define MII_CFG1_LED_ENABLE		BIT(3)

#define MII_CFG2			19
#define MII_CFG2_SLEEP_REQUEST_TO	GENMASK(1, 0)
#define MII_CFG2_SLEEP_REQUEST_TO_16MS	0x3

#define MII_INTSRC			21
#define MII_INTSRC_LINK_FAIL		BIT(10)
#define MII_INTSRC_LINK_UP		BIT(9)
#define MII_INTSRC_MASK			(MII_INTSRC_LINK_FAIL | MII_INTSRC_LINK_UP)
#define MII_INTSRC_UV_ERR		BIT(3)
#define MII_INTSRC_TEMP_ERR		BIT(1)

#define MII_INTEN			22
#define MII_INTEN_LINK_FAIL		BIT(10)
#define MII_INTEN_LINK_UP		BIT(9)
#define MII_INTEN_UV_ERR		BIT(3)
#define MII_INTEN_TEMP_ERR		BIT(1)

#define MII_COMMSTAT			23
#define MII_COMMSTAT_LINK_UP		BIT(15)
#define MII_COMMSTAT_SQI_STATE		GENMASK(7, 5)
#define MII_COMMSTAT_SQI_MAX		7

#define MII_GENSTAT			24
#define MII_GENSTAT_PLL_LOCKED		BIT(14)

#define MII_EXTSTAT			25
#define MII_EXTSTAT_SHORT_DETECT	BIT(8)
#define MII_EXTSTAT_OPEN_DETECT		BIT(7)
#define MII_EXTSTAT_POLARITY_DETECT	BIT(6)

#define MII_COMMCFG			27
#define MII_COMMCFG_AUTO_OP		BIT(15)

struct tja11xx_priv {
	char		*hwmon_name;
	struct device	*hwmon_dev;
	struct phy_device *phydev;
	struct work_struct phy_register_work;
};

struct tja11xx_phy_stats {
	const char	*string;
	u8		reg;
	u8		off;
	u16		mask;
};

static struct tja11xx_phy_stats tja11xx_hw_stats[] = {
	{ "phy_symbol_error_count", 20, 0, GENMASK(15, 0) },
	{ "phy_polarity_detect", 25, 6, BIT(6) },
	{ "phy_open_detect", 25, 7, BIT(7) },
	{ "phy_short_detect", 25, 8, BIT(8) },
	{ "phy_rem_rcvr_count", 26, 0, GENMASK(7, 0) },
	{ "phy_loc_rcvr_count", 26, 8, GENMASK(15, 8) },
};

static int tja11xx_check(struct phy_device *phydev, u8 reg, u16 mask, u16 set)
{
	int val;

	return phy_read_poll_timeout(phydev, reg, val, (val & mask) == set,
				     150, 30000, false);
}

static int phy_modify_check(struct phy_device *phydev, u8 reg,
			    u16 mask, u16 set)
{
	int ret;

	ret = phy_modify(phydev, reg, mask, set);
	if (ret)
		return ret;

	return tja11xx_check(phydev, reg, mask, set);
}

static int tja11xx_enable_reg_write(struct phy_device *phydev)
{
	return phy_set_bits(phydev, MII_ECTRL, MII_ECTRL_CONFIG_EN);
}

static int tja11xx_enable_link_control(struct phy_device *phydev)
{
	return phy_set_bits(phydev, MII_ECTRL, MII_ECTRL_LINK_CONTROL);
}

static int tja11xx_disable_link_control(struct phy_device *phydev)
{
	return phy_clear_bits(phydev, MII_ECTRL, MII_ECTRL_LINK_CONTROL);
}

static int tja11xx_wakeup(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, MII_ECTRL);
	if (ret < 0)
		return ret;

	switch (ret & MII_ECTRL_POWER_MODE_MASK) {
	case MII_ECTRL_POWER_MODE_NO_CHANGE:
		break;
	case MII_ECTRL_POWER_MODE_NORMAL:
		ret = phy_set_bits(phydev, MII_ECTRL, MII_ECTRL_WAKE_REQUEST);
		if (ret)
			return ret;

		ret = phy_clear_bits(phydev, MII_ECTRL, MII_ECTRL_WAKE_REQUEST);
		if (ret)
			return ret;
		break;
	case MII_ECTRL_POWER_MODE_STANDBY:
		ret = phy_modify_check(phydev, MII_ECTRL,
				       MII_ECTRL_POWER_MODE_MASK,
				       MII_ECTRL_POWER_MODE_STANDBY);
		if (ret)
			return ret;

		ret = phy_modify(phydev, MII_ECTRL, MII_ECTRL_POWER_MODE_MASK,
				 MII_ECTRL_POWER_MODE_NORMAL);
		if (ret)
			return ret;

		ret = phy_modify_check(phydev, MII_GENSTAT,
				       MII_GENSTAT_PLL_LOCKED,
				       MII_GENSTAT_PLL_LOCKED);
		if (ret)
			return ret;

		return tja11xx_enable_link_control(phydev);
	default:
		break;
	}

	return 0;
}

static int tja11xx_soft_reset(struct phy_device *phydev)
{
	int ret;

	ret = tja11xx_enable_reg_write(phydev);
	if (ret)
		return ret;

	return genphy_soft_reset(phydev);
}

static int tja11xx_config_aneg_cable_test(struct phy_device *phydev)
{
	bool finished = false;
	int ret;

	if (phydev->link)
		return 0;

	if (!phydev->drv->cable_test_start ||
	    !phydev->drv->cable_test_get_status)
		return 0;

	ret = ethnl_cable_test_alloc(phydev, ETHTOOL_MSG_CABLE_TEST_NTF);
	if (ret)
		return ret;

	ret = phydev->drv->cable_test_start(phydev);
	if (ret)
		return ret;

	/* According to the documentation this test takes 100 usec */
	usleep_range(100, 200);

	ret = phydev->drv->cable_test_get_status(phydev, &finished);
	if (ret)
		return ret;

	if (finished)
		ethnl_cable_test_finished(phydev);

	return 0;
}

static int tja11xx_config_aneg(struct phy_device *phydev)
{
	int ret, changed = 0;
	u16 ctl = 0;

	switch (phydev->master_slave_set) {
	case MASTER_SLAVE_CFG_MASTER_FORCE:
		ctl |= MII_CFG1_MASTER_SLAVE;
		break;
	case MASTER_SLAVE_CFG_SLAVE_FORCE:
		break;
	case MASTER_SLAVE_CFG_UNKNOWN:
	case MASTER_SLAVE_CFG_UNSUPPORTED:
		goto do_test;
	default:
		phydev_warn(phydev, "Unsupported Master/Slave mode\n");
		return -ENOTSUPP;
	}

	changed = phy_modify_changed(phydev, MII_CFG1, MII_CFG1_MASTER_SLAVE, ctl);
	if (changed < 0)
		return changed;

do_test:
	ret = tja11xx_config_aneg_cable_test(phydev);
	if (ret)
		return ret;

	return __genphy_config_aneg(phydev, changed);
}

static int tja11xx_config_init(struct phy_device *phydev)
{
	int ret;

	ret = tja11xx_enable_reg_write(phydev);
	if (ret)
		return ret;

	phydev->autoneg = AUTONEG_DISABLE;
	phydev->speed = SPEED_100;
	phydev->duplex = DUPLEX_FULL;

	switch (phydev->phy_id & PHY_ID_MASK) {
	case PHY_ID_TJA1100:
		ret = phy_modify(phydev, MII_CFG1,
				 MII_CFG1_AUTO_OP | MII_CFG1_LED_MODE_MASK |
				 MII_CFG1_LED_ENABLE,
				 MII_CFG1_AUTO_OP | MII_CFG1_LED_MODE_LINKUP |
				 MII_CFG1_LED_ENABLE);
		if (ret)
			return ret;
		break;
	case PHY_ID_TJA1101:
	case PHY_ID_TJA1102:
		ret = phy_set_bits(phydev, MII_COMMCFG, MII_COMMCFG_AUTO_OP);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	ret = phy_clear_bits(phydev, MII_CFG1, MII_CFG1_SLEEP_CONFIRM);
	if (ret)
		return ret;

	ret = phy_modify(phydev, MII_CFG2, MII_CFG2_SLEEP_REQUEST_TO,
			 MII_CFG2_SLEEP_REQUEST_TO_16MS);
	if (ret)
		return ret;

	ret = tja11xx_wakeup(phydev);
	if (ret < 0)
		return ret;

	/* ACK interrupts by reading the status register */
	ret = phy_read(phydev, MII_INTSRC);
	if (ret < 0)
		return ret;

	return 0;
}

static int tja11xx_read_status(struct phy_device *phydev)
{
	int ret;

	phydev->master_slave_get = MASTER_SLAVE_CFG_UNKNOWN;
	phydev->master_slave_state = MASTER_SLAVE_STATE_UNSUPPORTED;

	ret = genphy_update_link(phydev);
	if (ret)
		return ret;

	ret = phy_read(phydev, MII_CFG1);
	if (ret < 0)
		return ret;

	if (ret & MII_CFG1_MASTER_SLAVE)
		phydev->master_slave_get = MASTER_SLAVE_CFG_MASTER_FORCE;
	else
		phydev->master_slave_get = MASTER_SLAVE_CFG_SLAVE_FORCE;

	if (phydev->link) {
		ret = phy_read(phydev, MII_COMMSTAT);
		if (ret < 0)
			return ret;

		if (!(ret & MII_COMMSTAT_LINK_UP))
			phydev->link = 0;
	}

	return 0;
}

static int tja11xx_get_sqi(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, MII_COMMSTAT);
	if (ret < 0)
		return ret;

	return FIELD_GET(MII_COMMSTAT_SQI_STATE, ret);
}

static int tja11xx_get_sqi_max(struct phy_device *phydev)
{
	return MII_COMMSTAT_SQI_MAX;
}

static int tja11xx_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(tja11xx_hw_stats);
}

static void tja11xx_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tja11xx_hw_stats); i++) {
		strncpy(data + i * ETH_GSTRING_LEN,
			tja11xx_hw_stats[i].string, ETH_GSTRING_LEN);
	}
}

static void tja11xx_get_stats(struct phy_device *phydev,
			      struct ethtool_stats *stats, u64 *data)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(tja11xx_hw_stats); i++) {
		ret = phy_read(phydev, tja11xx_hw_stats[i].reg);
		if (ret < 0)
			data[i] = U64_MAX;
		else {
			data[i] = ret & tja11xx_hw_stats[i].mask;
			data[i] >>= tja11xx_hw_stats[i].off;
		}
	}
}

static int tja11xx_hwmon_read(struct device *dev,
			      enum hwmon_sensor_types type,
			      u32 attr, int channel, long *value)
{
	struct phy_device *phydev = dev_get_drvdata(dev);
	int ret;

	if (type == hwmon_in && attr == hwmon_in_lcrit_alarm) {
		ret = phy_read(phydev, MII_INTSRC);
		if (ret < 0)
			return ret;

		*value = !!(ret & MII_INTSRC_TEMP_ERR);
		return 0;
	}

	if (type == hwmon_temp && attr == hwmon_temp_crit_alarm) {
		ret = phy_read(phydev, MII_INTSRC);
		if (ret < 0)
			return ret;

		*value = !!(ret & MII_INTSRC_UV_ERR);
		return 0;
	}

	return -EOPNOTSUPP;
}

static umode_t tja11xx_hwmon_is_visible(const void *data,
					enum hwmon_sensor_types type,
					u32 attr, int channel)
{
	if (type == hwmon_in && attr == hwmon_in_lcrit_alarm)
		return 0444;

	if (type == hwmon_temp && attr == hwmon_temp_crit_alarm)
		return 0444;

	return 0;
}

static const struct hwmon_channel_info *tja11xx_hwmon_info[] = {
	HWMON_CHANNEL_INFO(in, HWMON_I_LCRIT_ALARM),
	HWMON_CHANNEL_INFO(temp, HWMON_T_CRIT_ALARM),
	NULL
};

static const struct hwmon_ops tja11xx_hwmon_hwmon_ops = {
	.is_visible	= tja11xx_hwmon_is_visible,
	.read		= tja11xx_hwmon_read,
};

static const struct hwmon_chip_info tja11xx_hwmon_chip_info = {
	.ops		= &tja11xx_hwmon_hwmon_ops,
	.info		= tja11xx_hwmon_info,
};

static int tja11xx_hwmon_register(struct phy_device *phydev,
				  struct tja11xx_priv *priv)
{
	struct device *dev = &phydev->mdio.dev;

	priv->hwmon_name = devm_hwmon_sanitize_name(dev, dev_name(dev));
	if (IS_ERR(priv->hwmon_name))
		return PTR_ERR(priv->hwmon_name);

	priv->hwmon_dev =
		devm_hwmon_device_register_with_info(dev, priv->hwmon_name,
						     phydev,
						     &tja11xx_hwmon_chip_info,
						     NULL);

	return PTR_ERR_OR_ZERO(priv->hwmon_dev);
}

static int tja11xx_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct tja11xx_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->phydev = phydev;

	return tja11xx_hwmon_register(phydev, priv);
}

static void tja1102_p1_register(struct work_struct *work)
{
	struct tja11xx_priv *priv = container_of(work, struct tja11xx_priv,
						 phy_register_work);
	struct phy_device *phydev_phy0 = priv->phydev;
	struct mii_bus *bus = phydev_phy0->mdio.bus;
	struct device *dev = &phydev_phy0->mdio.dev;
	struct device_node *np = dev->of_node;
	struct device_node *child;
	int ret;

	for_each_available_child_of_node(np, child) {
		struct phy_device *phy;
		int addr;

		addr = of_mdio_parse_addr(dev, child);
		if (addr < 0) {
			dev_err(dev, "Can't parse addr\n");
			continue;
		} else if (addr != phydev_phy0->mdio.addr + 1) {
			/* Currently we care only about double PHY chip TJA1102.
			 * If some day NXP will decide to bring chips with more
			 * PHYs, this logic should be reworked.
			 */
			dev_err(dev, "Unexpected address. Should be: %i\n",
				phydev_phy0->mdio.addr + 1);
			continue;
		}

		if (mdiobus_is_registered_device(bus, addr)) {
			dev_err(dev, "device is already registered\n");
			continue;
		}

		/* Real PHY ID of Port 1 is 0 */
		phy = phy_device_create(bus, addr, PHY_ID_TJA1102, false, NULL);
		if (IS_ERR(phy)) {
			dev_err(dev, "Can't create PHY device for Port 1: %i\n",
				addr);
			continue;
		}

		/* Overwrite parent device. phy_device_create() set parent to
		 * the mii_bus->dev, which is not correct in case.
		 */
		phy->mdio.dev.parent = dev;

		ret = of_mdiobus_phy_device_register(bus, phy, child, addr);
		if (ret) {
			/* All resources needed for Port 1 should be already
			 * available for Port 0. Both ports use the same
			 * interrupt line, so -EPROBE_DEFER would make no sense
			 * here.
			 */
			dev_err(dev, "Can't register Port 1. Unexpected error: %i\n",
				ret);
			phy_device_free(phy);
		}
	}
}

static int tja1102_p0_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct tja11xx_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->phydev = phydev;
	INIT_WORK(&priv->phy_register_work, tja1102_p1_register);

	ret = tja11xx_hwmon_register(phydev, priv);
	if (ret)
		return ret;

	schedule_work(&priv->phy_register_work);

	return 0;
}

static int tja1102_match_phy_device(struct phy_device *phydev, bool port0)
{
	int ret;

	if ((phydev->phy_id & PHY_ID_MASK) != PHY_ID_TJA1102)
		return 0;

	ret = phy_read(phydev, MII_PHYSID2);
	if (ret < 0)
		return ret;

	/* TJA1102 Port 1 has phyid 0 and doesn't support temperature
	 * and undervoltage alarms.
	 */
	if (port0)
		return ret ? 1 : 0;

	return !ret;
}

static int tja1102_p0_match_phy_device(struct phy_device *phydev)
{
	return tja1102_match_phy_device(phydev, true);
}

static int tja1102_p1_match_phy_device(struct phy_device *phydev)
{
	return tja1102_match_phy_device(phydev, false);
}

static int tja11xx_ack_interrupt(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, MII_INTSRC);

	return (ret < 0) ? ret : 0;
}

static int tja11xx_config_intr(struct phy_device *phydev)
{
	int value = 0;
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = tja11xx_ack_interrupt(phydev);
		if (err)
			return err;

		value = MII_INTEN_LINK_FAIL | MII_INTEN_LINK_UP |
			MII_INTEN_UV_ERR | MII_INTEN_TEMP_ERR;
		err = phy_write(phydev, MII_INTEN, value);
	} else {
		err = phy_write(phydev, MII_INTEN, value);
		if (err)
			return err;

		err = tja11xx_ack_interrupt(phydev);
	}

	return err;
}

static irqreturn_t tja11xx_handle_interrupt(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	int irq_status;

	irq_status = phy_read(phydev, MII_INTSRC);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (irq_status & MII_INTSRC_TEMP_ERR)
		dev_warn(dev, "Overtemperature error detected (temp > 155C°).\n");
	if (irq_status & MII_INTSRC_UV_ERR)
		dev_warn(dev, "Undervoltage error detected.\n");

	if (!(irq_status & MII_INTSRC_MASK))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int tja11xx_cable_test_start(struct phy_device *phydev)
{
	int ret;

	ret = phy_clear_bits(phydev, MII_COMMCFG, MII_COMMCFG_AUTO_OP);
	if (ret)
		return ret;

	ret = tja11xx_wakeup(phydev);
	if (ret < 0)
		return ret;

	ret = tja11xx_disable_link_control(phydev);
	if (ret < 0)
		return ret;

	return phy_set_bits(phydev, MII_ECTRL, MII_ECTRL_CABLE_TEST);
}

/*
 * | BI_DA+           | BI_DA-                 | Result
 * | open             | open                   | open
 * | + short to -     | - short to +           | short
 * | short to Vdd     | open                   | open
 * | open             | shot to Vdd            | open
 * | short to Vdd     | short to Vdd           | short
 * | shot to GND      | open                   | open
 * | open             | shot to GND            | open
 * | short to GND     | shot to GND            | short
 * | connected to active link partner (master) | shot and open
 */
static int tja11xx_cable_test_report_trans(u32 result)
{
	u32 mask = MII_EXTSTAT_SHORT_DETECT | MII_EXTSTAT_OPEN_DETECT;

	if ((result & mask) == mask) {
		/* connected to active link partner (master) */
		return ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC;
	} else if ((result & mask) == 0) {
		return ETHTOOL_A_CABLE_RESULT_CODE_OK;
	} else if (result & MII_EXTSTAT_SHORT_DETECT) {
		return ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT;
	} else if (result & MII_EXTSTAT_OPEN_DETECT) {
		return ETHTOOL_A_CABLE_RESULT_CODE_OPEN;
	} else {
		return ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC;
	}
}

static int tja11xx_cable_test_report(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, MII_EXTSTAT);
	if (ret < 0)
		return ret;

	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
				tja11xx_cable_test_report_trans(ret));

	return 0;
}

static int tja11xx_cable_test_get_status(struct phy_device *phydev,
					 bool *finished)
{
	int ret;

	*finished = false;

	ret = phy_read(phydev, MII_ECTRL);
	if (ret < 0)
		return ret;

	if (!(ret & MII_ECTRL_CABLE_TEST)) {
		*finished = true;

		ret = phy_set_bits(phydev, MII_COMMCFG, MII_COMMCFG_AUTO_OP);
		if (ret)
			return ret;

		return tja11xx_cable_test_report(phydev);
	}

	return 0;
}

static struct phy_driver tja11xx_driver[] = {
	{
		PHY_ID_MATCH_MODEL(PHY_ID_TJA1100),
		.name		= "NXP TJA1100",
		.features       = PHY_BASIC_T1_FEATURES,
		.probe		= tja11xx_probe,
		.soft_reset	= tja11xx_soft_reset,
		.config_aneg	= tja11xx_config_aneg,
		.config_init	= tja11xx_config_init,
		.read_status	= tja11xx_read_status,
		.get_sqi	= tja11xx_get_sqi,
		.get_sqi_max	= tja11xx_get_sqi_max,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.set_loopback   = genphy_loopback,
		/* Statistics */
		.get_sset_count = tja11xx_get_sset_count,
		.get_strings	= tja11xx_get_strings,
		.get_stats	= tja11xx_get_stats,
	}, {
		PHY_ID_MATCH_MODEL(PHY_ID_TJA1101),
		.name		= "NXP TJA1101",
		.features       = PHY_BASIC_T1_FEATURES,
		.probe		= tja11xx_probe,
		.soft_reset	= tja11xx_soft_reset,
		.config_aneg	= tja11xx_config_aneg,
		.config_init	= tja11xx_config_init,
		.read_status	= tja11xx_read_status,
		.get_sqi	= tja11xx_get_sqi,
		.get_sqi_max	= tja11xx_get_sqi_max,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.set_loopback   = genphy_loopback,
		/* Statistics */
		.get_sset_count = tja11xx_get_sset_count,
		.get_strings	= tja11xx_get_strings,
		.get_stats	= tja11xx_get_stats,
	}, {
		.name		= "NXP TJA1102 Port 0",
		.features       = PHY_BASIC_T1_FEATURES,
		.flags          = PHY_POLL_CABLE_TEST,
		.probe		= tja1102_p0_probe,
		.soft_reset	= tja11xx_soft_reset,
		.config_aneg	= tja11xx_config_aneg,
		.config_init	= tja11xx_config_init,
		.read_status	= tja11xx_read_status,
		.get_sqi	= tja11xx_get_sqi,
		.get_sqi_max	= tja11xx_get_sqi_max,
		.match_phy_device = tja1102_p0_match_phy_device,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.set_loopback   = genphy_loopback,
		/* Statistics */
		.get_sset_count = tja11xx_get_sset_count,
		.get_strings	= tja11xx_get_strings,
		.get_stats	= tja11xx_get_stats,
		.config_intr	= tja11xx_config_intr,
		.handle_interrupt = tja11xx_handle_interrupt,
		.cable_test_start = tja11xx_cable_test_start,
		.cable_test_get_status = tja11xx_cable_test_get_status,
	}, {
		.name		= "NXP TJA1102 Port 1",
		.features       = PHY_BASIC_T1_FEATURES,
		.flags          = PHY_POLL_CABLE_TEST,
		/* currently no probe for Port 1 is need */
		.soft_reset	= tja11xx_soft_reset,
		.config_aneg	= tja11xx_config_aneg,
		.config_init	= tja11xx_config_init,
		.read_status	= tja11xx_read_status,
		.get_sqi	= tja11xx_get_sqi,
		.get_sqi_max	= tja11xx_get_sqi_max,
		.match_phy_device = tja1102_p1_match_phy_device,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.set_loopback   = genphy_loopback,
		/* Statistics */
		.get_sset_count = tja11xx_get_sset_count,
		.get_strings	= tja11xx_get_strings,
		.get_stats	= tja11xx_get_stats,
		.config_intr	= tja11xx_config_intr,
		.handle_interrupt = tja11xx_handle_interrupt,
		.cable_test_start = tja11xx_cable_test_start,
		.cable_test_get_status = tja11xx_cable_test_get_status,
	}
};

module_phy_driver(tja11xx_driver);

static struct mdio_device_id __maybe_unused tja11xx_tbl[] = {
	{ PHY_ID_MATCH_MODEL(PHY_ID_TJA1100) },
	{ PHY_ID_MATCH_MODEL(PHY_ID_TJA1101) },
	{ PHY_ID_MATCH_MODEL(PHY_ID_TJA1102) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, tja11xx_tbl);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("NXP TJA11xx BoardR-Reach PHY driver");
MODULE_LICENSE("GPL");
