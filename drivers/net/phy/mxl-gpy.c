// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2021 Maxlinear Corporation
 * Copyright (C) 2020 Intel Corporation
 *
 * Drivers for Maxlinear Ethernet GPY
 *
 */

#include <linux/module.h>
#include <linux/bitfield.h>
#include <linux/hwmon.h>
#include <linux/mutex.h>
#include <linux/phy.h>
#include <linux/polynomial.h>
#include <linux/property.h>
#include <linux/netdevice.h>

/* PHY ID */
#define PHY_ID_GPYx15B_MASK	0xFFFFFFFC
#define PHY_ID_GPY21xB_MASK	0xFFFFFFF9
#define PHY_ID_GPY2xx		0x67C9DC00
#define PHY_ID_GPY115B		0x67C9DF00
#define PHY_ID_GPY115C		0x67C9DF10
#define PHY_ID_GPY211B		0x67C9DE08
#define PHY_ID_GPY211C		0x67C9DE10
#define PHY_ID_GPY212B		0x67C9DE09
#define PHY_ID_GPY212C		0x67C9DE20
#define PHY_ID_GPY215B		0x67C9DF04
#define PHY_ID_GPY215C		0x67C9DF20
#define PHY_ID_GPY241B		0x67C9DE40
#define PHY_ID_GPY241BM		0x67C9DE80
#define PHY_ID_GPY245B		0x67C9DEC0

#define PHY_CTL1		0x13
#define PHY_CTL1_MDICD		BIT(3)
#define PHY_CTL1_MDIAB		BIT(2)
#define PHY_CTL1_AMDIX		BIT(0)
#define PHY_MIISTAT		0x18	/* MII state */
#define PHY_IMASK		0x19	/* interrupt mask */
#define PHY_ISTAT		0x1A	/* interrupt status */
#define PHY_LED			0x1B	/* LEDs */
#define PHY_FWV			0x1E	/* firmware version */

#define PHY_MIISTAT_SPD_MASK	GENMASK(2, 0)
#define PHY_MIISTAT_DPX		BIT(3)
#define PHY_MIISTAT_LS		BIT(10)

#define PHY_MIISTAT_SPD_10	0
#define PHY_MIISTAT_SPD_100	1
#define PHY_MIISTAT_SPD_1000	2
#define PHY_MIISTAT_SPD_2500	4

#define PHY_IMASK_WOL		BIT(15)	/* Wake-on-LAN */
#define PHY_IMASK_ANC		BIT(10)	/* Auto-Neg complete */
#define PHY_IMASK_ADSC		BIT(5)	/* Link auto-downspeed detect */
#define PHY_IMASK_DXMC		BIT(2)	/* Duplex mode change */
#define PHY_IMASK_LSPC		BIT(1)	/* Link speed change */
#define PHY_IMASK_LSTC		BIT(0)	/* Link state change */
#define PHY_IMASK_MASK		(PHY_IMASK_LSTC | \
				 PHY_IMASK_LSPC | \
				 PHY_IMASK_DXMC | \
				 PHY_IMASK_ADSC | \
				 PHY_IMASK_ANC)

#define GPY_MAX_LEDS		4
#define PHY_LED_POLARITY(idx)	BIT(12 + (idx))
#define PHY_LED_HWCONTROL(idx)	BIT(8 + (idx))
#define PHY_LED_ON(idx)		BIT(idx)

#define PHY_FWV_REL_MASK	BIT(15)
#define PHY_FWV_MAJOR_MASK	GENMASK(11, 8)
#define PHY_FWV_MINOR_MASK	GENMASK(7, 0)

#define PHY_PMA_MGBT_POLARITY	0x82
#define PHY_MDI_MDI_X_MASK	GENMASK(1, 0)
#define PHY_MDI_MDI_X_NORMAL	0x3
#define PHY_MDI_MDI_X_AB	0x2
#define PHY_MDI_MDI_X_CD	0x1
#define PHY_MDI_MDI_X_CROSS	0x0

/* LED */
#define VSPEC1_LED(idx)		(1 + (idx))
#define VSPEC1_LED_BLINKS	GENMASK(15, 12)
#define VSPEC1_LED_PULSE	GENMASK(11, 8)
#define VSPEC1_LED_CON		GENMASK(7, 4)
#define VSPEC1_LED_BLINKF	GENMASK(3, 0)

#define VSPEC1_LED_LINK10	BIT(0)
#define VSPEC1_LED_LINK100	BIT(1)
#define VSPEC1_LED_LINK1000	BIT(2)
#define VSPEC1_LED_LINK2500	BIT(3)

#define VSPEC1_LED_TXACT	BIT(0)
#define VSPEC1_LED_RXACT	BIT(1)
#define VSPEC1_LED_COL		BIT(2)
#define VSPEC1_LED_NO_CON	BIT(3)

/* SGMII */
#define VSPEC1_SGMII_CTRL	0x08
#define VSPEC1_SGMII_CTRL_ANEN	BIT(12)		/* Aneg enable */
#define VSPEC1_SGMII_CTRL_ANRS	BIT(9)		/* Restart Aneg */
#define VSPEC1_SGMII_ANEN_ANRS	(VSPEC1_SGMII_CTRL_ANEN | \
				 VSPEC1_SGMII_CTRL_ANRS)

/* Temperature sensor */
#define VSPEC1_TEMP_STA	0x0E
#define VSPEC1_TEMP_STA_DATA	GENMASK(9, 0)

/* Mailbox */
#define VSPEC1_MBOX_DATA	0x5
#define VSPEC1_MBOX_ADDRLO	0x6
#define VSPEC1_MBOX_CMD		0x7
#define VSPEC1_MBOX_CMD_ADDRHI	GENMASK(7, 0)
#define VSPEC1_MBOX_CMD_RD	(0 << 8)
#define VSPEC1_MBOX_CMD_READY	BIT(15)

/* WoL */
#define VPSPEC2_WOL_CTL		0x0E06
#define VPSPEC2_WOL_AD01	0x0E08
#define VPSPEC2_WOL_AD23	0x0E09
#define VPSPEC2_WOL_AD45	0x0E0A
#define WOL_EN			BIT(0)

/* Internal registers, access via mbox */
#define REG_GPIO0_OUT		0xd3ce00

struct gpy_priv {
	/* serialize mailbox acesses */
	struct mutex mbox_lock;

	u8 fw_major;
	u8 fw_minor;
	u32 wolopts;

	/* It takes 3 seconds to fully switch out of loopback mode before
	 * it can safely re-enter loopback mode. Record the time when
	 * loopback is disabled. Check and wait if necessary before loopback
	 * is enabled.
	 */
	u64 lb_dis_to;
};

static const struct {
	int major;
	int minor;
} ver_need_sgmii_reaneg[] = {
	{7, 0x6D},
	{8, 0x6D},
	{9, 0x73},
};

#if IS_ENABLED(CONFIG_HWMON)
/* The original translation formulae of the temperature (in degrees of Celsius)
 * are as follows:
 *
 *   T = -2.5761e-11*(N^4) + 9.7332e-8*(N^3) + -1.9165e-4*(N^2) +
 *       3.0762e-1*(N^1) + -5.2156e1
 *
 * where [-52.156, 137.961]C and N = [0, 1023].
 *
 * They must be accordingly altered to be suitable for the integer arithmetics.
 * The technique is called 'factor redistribution', which just makes sure the
 * multiplications and divisions are made so to have a result of the operations
 * within the integer numbers limit. In addition we need to translate the
 * formulae to accept millidegrees of Celsius. Here what it looks like after
 * the alterations:
 *
 *   T = -25761e-12*(N^4) + 97332e-9*(N^3) + -191650e-6*(N^2) +
 *       307620e-3*(N^1) + -52156
 *
 * where T = [-52156, 137961]mC and N = [0, 1023].
 */
static const struct polynomial poly_N_to_temp = {
	.terms = {
		{4,  -25761, 1000, 1},
		{3,   97332, 1000, 1},
		{2, -191650, 1000, 1},
		{1,  307620, 1000, 1},
		{0,  -52156,    1, 1}
	}
};

static int gpy_hwmon_read(struct device *dev,
			  enum hwmon_sensor_types type,
			  u32 attr, int channel, long *value)
{
	struct phy_device *phydev = dev_get_drvdata(dev);
	int ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, VSPEC1_TEMP_STA);
	if (ret < 0)
		return ret;
	if (!ret)
		return -ENODATA;

	*value = polynomial_calc(&poly_N_to_temp,
				 FIELD_GET(VSPEC1_TEMP_STA_DATA, ret));

	return 0;
}

static umode_t gpy_hwmon_is_visible(const void *data,
				    enum hwmon_sensor_types type,
				    u32 attr, int channel)
{
	return 0444;
}

static const struct hwmon_channel_info * const gpy_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	NULL
};

static const struct hwmon_ops gpy_hwmon_hwmon_ops = {
	.is_visible	= gpy_hwmon_is_visible,
	.read		= gpy_hwmon_read,
};

static const struct hwmon_chip_info gpy_hwmon_chip_info = {
	.ops		= &gpy_hwmon_hwmon_ops,
	.info		= gpy_hwmon_info,
};

static int gpy_hwmon_register(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct device *hwmon_dev;
	char *hwmon_name;

	hwmon_name = devm_hwmon_sanitize_name(dev, dev_name(dev));
	if (IS_ERR(hwmon_name))
		return PTR_ERR(hwmon_name);

	hwmon_dev = devm_hwmon_device_register_with_info(dev, hwmon_name,
							 phydev,
							 &gpy_hwmon_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}
#else
static int gpy_hwmon_register(struct phy_device *phydev)
{
	return 0;
}
#endif

static int gpy_ack_interrupt(struct phy_device *phydev)
{
	int ret;

	/* Clear all pending interrupts */
	ret = phy_read(phydev, PHY_ISTAT);
	return ret < 0 ? ret : 0;
}

static int gpy_mbox_read(struct phy_device *phydev, u32 addr)
{
	struct gpy_priv *priv = phydev->priv;
	int val, ret;
	u16 cmd;

	mutex_lock(&priv->mbox_lock);

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VSPEC1_MBOX_ADDRLO,
			    addr);
	if (ret)
		goto out;

	cmd = VSPEC1_MBOX_CMD_RD;
	cmd |= FIELD_PREP(VSPEC1_MBOX_CMD_ADDRHI, addr >> 16);

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VSPEC1_MBOX_CMD, cmd);
	if (ret)
		goto out;

	/* The mbox read is used in the interrupt workaround. It was observed
	 * that a read might take up to 2.5ms. This is also the time for which
	 * the interrupt line is stuck low. To be on the safe side, poll the
	 * ready bit for 10ms.
	 */
	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_VEND1,
					VSPEC1_MBOX_CMD, val,
					(val & VSPEC1_MBOX_CMD_READY),
					500, 10000, false);
	if (ret)
		goto out;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, VSPEC1_MBOX_DATA);

out:
	mutex_unlock(&priv->mbox_lock);
	return ret;
}

static int gpy_config_init(struct phy_device *phydev)
{
	/* Nothing to configure. Configuration Requirement Placeholder */
	return 0;
}

static int gpy21x_config_init(struct phy_device *phydev)
{
	__set_bit(PHY_INTERFACE_MODE_2500BASEX, phydev->possible_interfaces);
	__set_bit(PHY_INTERFACE_MODE_SGMII, phydev->possible_interfaces);

	return gpy_config_init(phydev);
}

static int gpy_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct gpy_priv *priv;
	int fw_version;
	int ret;

	if (!phydev->is_c45) {
		ret = phy_get_c45_ids(phydev);
		if (ret < 0)
			return ret;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	phydev->priv = priv;
	mutex_init(&priv->mbox_lock);

	if (!device_property_present(dev, "maxlinear,use-broken-interrupts"))
		phydev->dev_flags |= PHY_F_NO_IRQ;

	fw_version = phy_read(phydev, PHY_FWV);
	if (fw_version < 0)
		return fw_version;
	priv->fw_major = FIELD_GET(PHY_FWV_MAJOR_MASK, fw_version);
	priv->fw_minor = FIELD_GET(PHY_FWV_MINOR_MASK, fw_version);

	ret = gpy_hwmon_register(phydev);
	if (ret)
		return ret;

	/* Show GPY PHY FW version in dmesg */
	phydev_info(phydev, "Firmware Version: %d.%d (0x%04X%s)\n",
		    priv->fw_major, priv->fw_minor, fw_version,
		    fw_version & PHY_FWV_REL_MASK ? "" : " test version");

	return 0;
}

static bool gpy_sgmii_need_reaneg(struct phy_device *phydev)
{
	struct gpy_priv *priv = phydev->priv;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(ver_need_sgmii_reaneg); i++) {
		if (priv->fw_major != ver_need_sgmii_reaneg[i].major)
			continue;
		if (priv->fw_minor < ver_need_sgmii_reaneg[i].minor)
			return true;
		break;
	}

	return false;
}

static bool gpy_2500basex_chk(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, PHY_MIISTAT);
	if (ret < 0) {
		phydev_err(phydev, "Error: MDIO register access failed: %d\n",
			   ret);
		return false;
	}

	if (!(ret & PHY_MIISTAT_LS) ||
	    FIELD_GET(PHY_MIISTAT_SPD_MASK, ret) != PHY_MIISTAT_SPD_2500)
		return false;

	phydev->speed = SPEED_2500;
	phydev->interface = PHY_INTERFACE_MODE_2500BASEX;
	phy_modify_mmd(phydev, MDIO_MMD_VEND1, VSPEC1_SGMII_CTRL,
		       VSPEC1_SGMII_CTRL_ANEN, 0);
	return true;
}

static bool gpy_sgmii_aneg_en(struct phy_device *phydev)
{
	int ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, VSPEC1_SGMII_CTRL);
	if (ret < 0) {
		phydev_err(phydev, "Error: MMD register access failed: %d\n",
			   ret);
		return true;
	}

	return (ret & VSPEC1_SGMII_CTRL_ANEN) ? true : false;
}

static int gpy_config_mdix(struct phy_device *phydev, u8 ctrl)
{
	int ret;
	u16 val;

	switch (ctrl) {
	case ETH_TP_MDI_AUTO:
		val = PHY_CTL1_AMDIX;
		break;
	case ETH_TP_MDI_X:
		val = (PHY_CTL1_MDIAB | PHY_CTL1_MDICD);
		break;
	case ETH_TP_MDI:
		val = 0;
		break;
	default:
		return 0;
	}

	ret =  phy_modify(phydev, PHY_CTL1, PHY_CTL1_AMDIX | PHY_CTL1_MDIAB |
			  PHY_CTL1_MDICD, val);
	if (ret < 0)
		return ret;

	return genphy_c45_restart_aneg(phydev);
}

static int gpy_config_aneg(struct phy_device *phydev)
{
	bool changed = false;
	u32 adv;
	int ret;

	if (phydev->autoneg == AUTONEG_DISABLE) {
		/* Configure half duplex with genphy_setup_forced,
		 * because genphy_c45_pma_setup_forced does not support.
		 */
		return phydev->duplex != DUPLEX_FULL
			? genphy_setup_forced(phydev)
			: genphy_c45_pma_setup_forced(phydev);
	}

	ret = gpy_config_mdix(phydev,  phydev->mdix_ctrl);
	if (ret < 0)
		return ret;

	ret = genphy_c45_an_config_aneg(phydev);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	adv = linkmode_adv_to_mii_ctrl1000_t(phydev->advertising);
	ret = phy_modify_changed(phydev, MII_CTRL1000,
				 ADVERTISE_1000FULL | ADVERTISE_1000HALF,
				 adv);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	ret = genphy_c45_check_and_restart_aneg(phydev, changed);
	if (ret < 0)
		return ret;

	if (phydev->interface == PHY_INTERFACE_MODE_USXGMII ||
	    phydev->interface == PHY_INTERFACE_MODE_INTERNAL)
		return 0;

	/* No need to trigger re-ANEG if link speed is 2.5G or SGMII ANEG is
	 * disabled.
	 */
	if (!gpy_sgmii_need_reaneg(phydev) || gpy_2500basex_chk(phydev) ||
	    !gpy_sgmii_aneg_en(phydev))
		return 0;

	/* There is a design constraint in GPY2xx device where SGMII AN is
	 * only triggered when there is change of speed. If, PHY link
	 * partner`s speed is still same even after PHY TPI is down and up
	 * again, SGMII AN is not triggered and hence no new in-band message
	 * from GPY to MAC side SGMII.
	 * This could cause an issue during power up, when PHY is up prior to
	 * MAC. At this condition, once MAC side SGMII is up, MAC side SGMII
	 * wouldn`t receive new in-band message from GPY with correct link
	 * status, speed and duplex info.
	 *
	 * 1) If PHY is already up and TPI link status is still down (such as
	 *    hard reboot), TPI link status is polled for 4 seconds before
	 *    retriggerring SGMII AN.
	 * 2) If PHY is already up and TPI link status is also up (such as soft
	 *    reboot), polling of TPI link status is not needed and SGMII AN is
	 *    immediately retriggered.
	 * 3) Other conditions such as PHY is down, speed change etc, skip
	 *    retriggering SGMII AN. Note: in case of speed change, GPY FW will
	 *    initiate SGMII AN.
	 */

	if (phydev->state != PHY_UP)
		return 0;

	ret = phy_read_poll_timeout(phydev, MII_BMSR, ret, ret & BMSR_LSTATUS,
				    20000, 4000000, false);
	if (ret == -ETIMEDOUT)
		return 0;
	else if (ret < 0)
		return ret;

	/* Trigger SGMII AN. */
	return phy_modify_mmd(phydev, MDIO_MMD_VEND1, VSPEC1_SGMII_CTRL,
			      VSPEC1_SGMII_CTRL_ANRS, VSPEC1_SGMII_CTRL_ANRS);
}

static int gpy_update_mdix(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, PHY_CTL1);
	if (ret < 0)
		return ret;

	if (ret & PHY_CTL1_AMDIX)
		phydev->mdix_ctrl = ETH_TP_MDI_AUTO;
	else
		if (ret & PHY_CTL1_MDICD || ret & PHY_CTL1_MDIAB)
			phydev->mdix_ctrl = ETH_TP_MDI_X;
		else
			phydev->mdix_ctrl = ETH_TP_MDI;

	ret = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, PHY_PMA_MGBT_POLARITY);
	if (ret < 0)
		return ret;

	if ((ret & PHY_MDI_MDI_X_MASK) < PHY_MDI_MDI_X_NORMAL)
		phydev->mdix = ETH_TP_MDI_X;
	else
		phydev->mdix = ETH_TP_MDI;

	return 0;
}

static int gpy_update_interface(struct phy_device *phydev)
{
	int ret;

	/* Interface mode is fixed for USXGMII and integrated PHY */
	if (phydev->interface == PHY_INTERFACE_MODE_USXGMII ||
	    phydev->interface == PHY_INTERFACE_MODE_INTERNAL)
		return -EINVAL;

	/* Automatically switch SERDES interface between SGMII and 2500-BaseX
	 * according to speed. Disable ANEG in 2500-BaseX mode.
	 */
	switch (phydev->speed) {
	case SPEED_2500:
		phydev->interface = PHY_INTERFACE_MODE_2500BASEX;
		ret = phy_modify_mmd(phydev, MDIO_MMD_VEND1, VSPEC1_SGMII_CTRL,
				     VSPEC1_SGMII_CTRL_ANEN, 0);
		if (ret < 0) {
			phydev_err(phydev,
				   "Error: Disable of SGMII ANEG failed: %d\n",
				   ret);
			return ret;
		}
		break;
	case SPEED_1000:
	case SPEED_100:
	case SPEED_10:
		phydev->interface = PHY_INTERFACE_MODE_SGMII;
		if (gpy_sgmii_aneg_en(phydev))
			break;
		/* Enable and restart SGMII ANEG for 10/100/1000Mbps link speed
		 * if ANEG is disabled (in 2500-BaseX mode).
		 */
		ret = phy_modify_mmd(phydev, MDIO_MMD_VEND1, VSPEC1_SGMII_CTRL,
				     VSPEC1_SGMII_ANEN_ANRS,
				     VSPEC1_SGMII_ANEN_ANRS);
		if (ret < 0) {
			phydev_err(phydev,
				   "Error: Enable of SGMII ANEG failed: %d\n",
				   ret);
			return ret;
		}
		break;
	}

	if (phydev->speed == SPEED_2500 || phydev->speed == SPEED_1000) {
		ret = genphy_read_master_slave(phydev);
		if (ret < 0)
			return ret;
	}

	return gpy_update_mdix(phydev);
}

static int gpy_read_status(struct phy_device *phydev)
{
	int ret;

	ret = genphy_update_link(phydev);
	if (ret)
		return ret;

	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->pause = 0;
	phydev->asym_pause = 0;

	if (phydev->autoneg == AUTONEG_ENABLE && phydev->autoneg_complete) {
		ret = genphy_c45_read_lpa(phydev);
		if (ret < 0)
			return ret;

		/* Read the link partner's 1G advertisement */
		ret = phy_read(phydev, MII_STAT1000);
		if (ret < 0)
			return ret;
		mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising, ret);
	} else if (phydev->autoneg == AUTONEG_DISABLE) {
		linkmode_zero(phydev->lp_advertising);
	}

	ret = phy_read(phydev, PHY_MIISTAT);
	if (ret < 0)
		return ret;

	phydev->link = (ret & PHY_MIISTAT_LS) ? 1 : 0;
	phydev->duplex = (ret & PHY_MIISTAT_DPX) ? DUPLEX_FULL : DUPLEX_HALF;
	switch (FIELD_GET(PHY_MIISTAT_SPD_MASK, ret)) {
	case PHY_MIISTAT_SPD_10:
		phydev->speed = SPEED_10;
		break;
	case PHY_MIISTAT_SPD_100:
		phydev->speed = SPEED_100;
		break;
	case PHY_MIISTAT_SPD_1000:
		phydev->speed = SPEED_1000;
		break;
	case PHY_MIISTAT_SPD_2500:
		phydev->speed = SPEED_2500;
		break;
	}

	if (phydev->link) {
		ret = gpy_update_interface(phydev);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int gpy_config_intr(struct phy_device *phydev)
{
	struct gpy_priv *priv = phydev->priv;
	u16 mask = 0;
	int ret;

	ret = gpy_ack_interrupt(phydev);
	if (ret)
		return ret;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		mask = PHY_IMASK_MASK;

	if (priv->wolopts & WAKE_MAGIC)
		mask |= PHY_IMASK_WOL;

	if (priv->wolopts & WAKE_PHY)
		mask |= PHY_IMASK_LSTC;

	return phy_write(phydev, PHY_IMASK, mask);
}

static irqreturn_t gpy_handle_interrupt(struct phy_device *phydev)
{
	int reg;

	reg = phy_read(phydev, PHY_ISTAT);
	if (reg < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (!(reg & PHY_IMASK_MASK))
		return IRQ_NONE;

	/* The PHY might leave the interrupt line asserted even after PHY_ISTAT
	 * is read. To avoid interrupt storms, delay the interrupt handling as
	 * long as the PHY drives the interrupt line. An internal bus read will
	 * stall as long as the interrupt line is asserted, thus just read a
	 * random register here.
	 * Because we cannot access the internal bus at all while the interrupt
	 * is driven by the PHY, there is no way to make the interrupt line
	 * unstuck (e.g. by changing the pinmux to GPIO input) during that time
	 * frame. Therefore, polling is the best we can do and won't do any more
	 * harm.
	 * It was observed that this bug happens on link state and link speed
	 * changes independent of the firmware version.
	 */
	if (reg & (PHY_IMASK_LSTC | PHY_IMASK_LSPC)) {
		reg = gpy_mbox_read(phydev, REG_GPIO0_OUT);
		if (reg < 0) {
			phy_error(phydev);
			return IRQ_NONE;
		}
	}

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int gpy_set_wol(struct phy_device *phydev,
		       struct ethtool_wolinfo *wol)
{
	struct net_device *attach_dev = phydev->attached_dev;
	struct gpy_priv *priv = phydev->priv;
	int ret;

	if (wol->wolopts & WAKE_MAGIC) {
		/* MAC address - Byte0:Byte1:Byte2:Byte3:Byte4:Byte5
		 * VPSPEC2_WOL_AD45 = Byte0:Byte1
		 * VPSPEC2_WOL_AD23 = Byte2:Byte3
		 * VPSPEC2_WOL_AD01 = Byte4:Byte5
		 */
		ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND2,
				       VPSPEC2_WOL_AD45,
				       ((attach_dev->dev_addr[0] << 8) |
				       attach_dev->dev_addr[1]));
		if (ret < 0)
			return ret;

		ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND2,
				       VPSPEC2_WOL_AD23,
				       ((attach_dev->dev_addr[2] << 8) |
				       attach_dev->dev_addr[3]));
		if (ret < 0)
			return ret;

		ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND2,
				       VPSPEC2_WOL_AD01,
				       ((attach_dev->dev_addr[4] << 8) |
				       attach_dev->dev_addr[5]));
		if (ret < 0)
			return ret;

		/* Enable the WOL interrupt */
		ret = phy_write(phydev, PHY_IMASK, PHY_IMASK_WOL);
		if (ret < 0)
			return ret;

		/* Enable magic packet matching */
		ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND2,
				       VPSPEC2_WOL_CTL,
				       WOL_EN);
		if (ret < 0)
			return ret;

		/* Clear the interrupt status register.
		 * Only WoL is enabled so clear all.
		 */
		ret = phy_read(phydev, PHY_ISTAT);
		if (ret < 0)
			return ret;

		priv->wolopts |= WAKE_MAGIC;
	} else {
		/* Disable magic packet matching */
		ret = phy_clear_bits_mmd(phydev, MDIO_MMD_VEND2,
					 VPSPEC2_WOL_CTL,
					 WOL_EN);
		if (ret < 0)
			return ret;

		/* Disable the WOL interrupt */
		ret = phy_clear_bits(phydev, PHY_IMASK, PHY_IMASK_WOL);
		if (ret < 0)
			return ret;

		priv->wolopts &= ~WAKE_MAGIC;
	}

	if (wol->wolopts & WAKE_PHY) {
		/* Enable the link state change interrupt */
		ret = phy_set_bits(phydev, PHY_IMASK, PHY_IMASK_LSTC);
		if (ret < 0)
			return ret;

		/* Clear the interrupt status register */
		ret = phy_read(phydev, PHY_ISTAT);
		if (ret < 0)
			return ret;

		if (ret & (PHY_IMASK_MASK & ~PHY_IMASK_LSTC))
			phy_trigger_machine(phydev);

		priv->wolopts |= WAKE_PHY;
		return 0;
	}

	priv->wolopts &= ~WAKE_PHY;
	/* Disable the link state change interrupt */
	return phy_clear_bits(phydev, PHY_IMASK, PHY_IMASK_LSTC);
}

static void gpy_get_wol(struct phy_device *phydev,
			struct ethtool_wolinfo *wol)
{
	struct gpy_priv *priv = phydev->priv;

	wol->supported = WAKE_MAGIC | WAKE_PHY;
	wol->wolopts = priv->wolopts;
}

static int gpy_loopback(struct phy_device *phydev, bool enable)
{
	struct gpy_priv *priv = phydev->priv;
	u16 set = 0;
	int ret;

	if (enable) {
		u64 now = get_jiffies_64();

		/* wait until 3 seconds from last disable */
		if (time_before64(now, priv->lb_dis_to))
			msleep(jiffies64_to_msecs(priv->lb_dis_to - now));

		set = BMCR_LOOPBACK;
	}

	ret = phy_modify(phydev, MII_BMCR, BMCR_LOOPBACK, set);
	if (ret <= 0)
		return ret;

	if (enable) {
		/* It takes some time for PHY device to switch into
		 * loopback mode.
		 */
		msleep(100);
	} else {
		priv->lb_dis_to = get_jiffies_64() + HZ * 3;
	}

	return 0;
}

static int gpy115_loopback(struct phy_device *phydev, bool enable)
{
	struct gpy_priv *priv = phydev->priv;

	if (enable)
		return gpy_loopback(phydev, enable);

	if (priv->fw_minor > 0x76)
		return gpy_loopback(phydev, 0);

	return genphy_soft_reset(phydev);
}

static int gpy_led_brightness_set(struct phy_device *phydev,
				  u8 index, enum led_brightness value)
{
	int ret;

	if (index >= GPY_MAX_LEDS)
		return -EINVAL;

	/* clear HWCONTROL and set manual LED state */
	ret = phy_modify(phydev, PHY_LED,
			 ((value == LED_OFF) ? PHY_LED_HWCONTROL(index) : 0) |
			 PHY_LED_ON(index),
			 (value == LED_OFF) ? 0 : PHY_LED_ON(index));
	if (ret)
		return ret;

	/* ToDo: set PWM brightness */

	/* clear HW LED setup */
	if (value == LED_OFF)
		return phy_write_mmd(phydev, MDIO_MMD_VEND1, VSPEC1_LED(index), 0);
	else
		return 0;
}

static const unsigned long supported_triggers = (BIT(TRIGGER_NETDEV_LINK) |
						 BIT(TRIGGER_NETDEV_LINK_10) |
						 BIT(TRIGGER_NETDEV_LINK_100) |
						 BIT(TRIGGER_NETDEV_LINK_1000) |
						 BIT(TRIGGER_NETDEV_LINK_2500) |
						 BIT(TRIGGER_NETDEV_RX) |
						 BIT(TRIGGER_NETDEV_TX));

static int gpy_led_hw_is_supported(struct phy_device *phydev, u8 index,
				   unsigned long rules)
{
	if (index >= GPY_MAX_LEDS)
		return -EINVAL;

	/* All combinations of the supported triggers are allowed */
	if (rules & ~supported_triggers)
		return -EOPNOTSUPP;

	return 0;
}

static int gpy_led_hw_control_get(struct phy_device *phydev, u8 index,
				  unsigned long *rules)
{
	int val;

	if (index >= GPY_MAX_LEDS)
		return -EINVAL;

	val = phy_read_mmd(phydev, MDIO_MMD_VEND1, VSPEC1_LED(index));
	if (val < 0)
		return val;

	if (FIELD_GET(VSPEC1_LED_CON, val) & VSPEC1_LED_LINK10)
		*rules |= BIT(TRIGGER_NETDEV_LINK_10);

	if (FIELD_GET(VSPEC1_LED_CON, val) & VSPEC1_LED_LINK100)
		*rules |= BIT(TRIGGER_NETDEV_LINK_100);

	if (FIELD_GET(VSPEC1_LED_CON, val) & VSPEC1_LED_LINK1000)
		*rules |= BIT(TRIGGER_NETDEV_LINK_1000);

	if (FIELD_GET(VSPEC1_LED_CON, val) & VSPEC1_LED_LINK2500)
		*rules |= BIT(TRIGGER_NETDEV_LINK_2500);

	if (FIELD_GET(VSPEC1_LED_CON, val) == (VSPEC1_LED_LINK10 |
					       VSPEC1_LED_LINK100 |
					       VSPEC1_LED_LINK1000 |
					       VSPEC1_LED_LINK2500))
		*rules |= BIT(TRIGGER_NETDEV_LINK);

	if (FIELD_GET(VSPEC1_LED_PULSE, val) & VSPEC1_LED_TXACT)
		*rules |= BIT(TRIGGER_NETDEV_TX);

	if (FIELD_GET(VSPEC1_LED_PULSE, val) & VSPEC1_LED_RXACT)
		*rules |= BIT(TRIGGER_NETDEV_RX);

	return 0;
}

static int gpy_led_hw_control_set(struct phy_device *phydev, u8 index,
				  unsigned long rules)
{
	u16 val = 0;
	int ret;

	if (index >= GPY_MAX_LEDS)
		return -EINVAL;

	if (rules & BIT(TRIGGER_NETDEV_LINK) ||
	    rules & BIT(TRIGGER_NETDEV_LINK_10))
		val |= FIELD_PREP(VSPEC1_LED_CON, VSPEC1_LED_LINK10);

	if (rules & BIT(TRIGGER_NETDEV_LINK) ||
	    rules & BIT(TRIGGER_NETDEV_LINK_100))
		val |= FIELD_PREP(VSPEC1_LED_CON, VSPEC1_LED_LINK100);

	if (rules & BIT(TRIGGER_NETDEV_LINK) ||
	    rules & BIT(TRIGGER_NETDEV_LINK_1000))
		val |= FIELD_PREP(VSPEC1_LED_CON, VSPEC1_LED_LINK1000);

	if (rules & BIT(TRIGGER_NETDEV_LINK) ||
	    rules & BIT(TRIGGER_NETDEV_LINK_2500))
		val |= FIELD_PREP(VSPEC1_LED_CON, VSPEC1_LED_LINK2500);

	if (rules & BIT(TRIGGER_NETDEV_TX))
		val |= FIELD_PREP(VSPEC1_LED_PULSE, VSPEC1_LED_TXACT);

	if (rules & BIT(TRIGGER_NETDEV_RX))
		val |= FIELD_PREP(VSPEC1_LED_PULSE, VSPEC1_LED_RXACT);

	/* allow RX/TX pulse without link indication */
	if ((rules & BIT(TRIGGER_NETDEV_TX) || rules & BIT(TRIGGER_NETDEV_RX)) &&
	    !(val & VSPEC1_LED_CON))
		val |= FIELD_PREP(VSPEC1_LED_PULSE, VSPEC1_LED_NO_CON) | VSPEC1_LED_CON;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VSPEC1_LED(index), val);
	if (ret)
		return ret;

	return phy_set_bits(phydev, PHY_LED, PHY_LED_HWCONTROL(index));
}

static int gpy_led_polarity_set(struct phy_device *phydev, int index,
				unsigned long modes)
{
	bool force_active_low = false, force_active_high = false;
	u32 mode;

	if (index >= GPY_MAX_LEDS)
		return -EINVAL;

	for_each_set_bit(mode, &modes, __PHY_LED_MODES_NUM) {
		switch (mode) {
		case PHY_LED_ACTIVE_LOW:
			force_active_low = true;
			break;
		case PHY_LED_ACTIVE_HIGH:
			force_active_high = true;
			break;
		default:
			return -EINVAL;
		}
	}

	if (force_active_low)
		return phy_set_bits(phydev, PHY_LED, PHY_LED_POLARITY(index));

	if (force_active_high)
		return phy_clear_bits(phydev, PHY_LED, PHY_LED_POLARITY(index));

	return -EINVAL;
}

static struct phy_driver gpy_drivers[] = {
	{
		PHY_ID_MATCH_MODEL(PHY_ID_GPY2xx),
		.name		= "Maxlinear Ethernet GPY2xx",
		.get_features	= genphy_c45_pma_read_abilities,
		.config_init	= gpy_config_init,
		.probe		= gpy_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.config_aneg	= gpy_config_aneg,
		.aneg_done	= genphy_c45_aneg_done,
		.read_status	= gpy_read_status,
		.config_intr	= gpy_config_intr,
		.handle_interrupt = gpy_handle_interrupt,
		.set_wol	= gpy_set_wol,
		.get_wol	= gpy_get_wol,
		.set_loopback	= gpy_loopback,
		.led_brightness_set = gpy_led_brightness_set,
		.led_hw_is_supported = gpy_led_hw_is_supported,
		.led_hw_control_get = gpy_led_hw_control_get,
		.led_hw_control_set = gpy_led_hw_control_set,
		.led_polarity_set = gpy_led_polarity_set,
	},
	{
		.phy_id		= PHY_ID_GPY115B,
		.phy_id_mask	= PHY_ID_GPYx15B_MASK,
		.name		= "Maxlinear Ethernet GPY115B",
		.get_features	= genphy_c45_pma_read_abilities,
		.config_init	= gpy_config_init,
		.probe		= gpy_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.config_aneg	= gpy_config_aneg,
		.aneg_done	= genphy_c45_aneg_done,
		.read_status	= gpy_read_status,
		.config_intr	= gpy_config_intr,
		.handle_interrupt = gpy_handle_interrupt,
		.set_wol	= gpy_set_wol,
		.get_wol	= gpy_get_wol,
		.set_loopback	= gpy115_loopback,
		.led_brightness_set = gpy_led_brightness_set,
		.led_hw_is_supported = gpy_led_hw_is_supported,
		.led_hw_control_get = gpy_led_hw_control_get,
		.led_hw_control_set = gpy_led_hw_control_set,
		.led_polarity_set = gpy_led_polarity_set,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_GPY115C),
		.name		= "Maxlinear Ethernet GPY115C",
		.get_features	= genphy_c45_pma_read_abilities,
		.config_init	= gpy_config_init,
		.probe		= gpy_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.config_aneg	= gpy_config_aneg,
		.aneg_done	= genphy_c45_aneg_done,
		.read_status	= gpy_read_status,
		.config_intr	= gpy_config_intr,
		.handle_interrupt = gpy_handle_interrupt,
		.set_wol	= gpy_set_wol,
		.get_wol	= gpy_get_wol,
		.set_loopback	= gpy115_loopback,
		.led_brightness_set = gpy_led_brightness_set,
		.led_hw_is_supported = gpy_led_hw_is_supported,
		.led_hw_control_get = gpy_led_hw_control_get,
		.led_hw_control_set = gpy_led_hw_control_set,
		.led_polarity_set = gpy_led_polarity_set,
	},
	{
		.phy_id		= PHY_ID_GPY211B,
		.phy_id_mask	= PHY_ID_GPY21xB_MASK,
		.name		= "Maxlinear Ethernet GPY211B",
		.get_features	= genphy_c45_pma_read_abilities,
		.config_init	= gpy21x_config_init,
		.probe		= gpy_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.config_aneg	= gpy_config_aneg,
		.aneg_done	= genphy_c45_aneg_done,
		.read_status	= gpy_read_status,
		.config_intr	= gpy_config_intr,
		.handle_interrupt = gpy_handle_interrupt,
		.set_wol	= gpy_set_wol,
		.get_wol	= gpy_get_wol,
		.set_loopback	= gpy_loopback,
		.led_brightness_set = gpy_led_brightness_set,
		.led_hw_is_supported = gpy_led_hw_is_supported,
		.led_hw_control_get = gpy_led_hw_control_get,
		.led_hw_control_set = gpy_led_hw_control_set,
		.led_polarity_set = gpy_led_polarity_set,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_GPY211C),
		.name		= "Maxlinear Ethernet GPY211C",
		.get_features	= genphy_c45_pma_read_abilities,
		.config_init	= gpy21x_config_init,
		.probe		= gpy_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.config_aneg	= gpy_config_aneg,
		.aneg_done	= genphy_c45_aneg_done,
		.read_status	= gpy_read_status,
		.config_intr	= gpy_config_intr,
		.handle_interrupt = gpy_handle_interrupt,
		.set_wol	= gpy_set_wol,
		.get_wol	= gpy_get_wol,
		.set_loopback	= gpy_loopback,
		.led_brightness_set = gpy_led_brightness_set,
		.led_hw_is_supported = gpy_led_hw_is_supported,
		.led_hw_control_get = gpy_led_hw_control_get,
		.led_hw_control_set = gpy_led_hw_control_set,
		.led_polarity_set = gpy_led_polarity_set,
	},
	{
		.phy_id		= PHY_ID_GPY212B,
		.phy_id_mask	= PHY_ID_GPY21xB_MASK,
		.name		= "Maxlinear Ethernet GPY212B",
		.get_features	= genphy_c45_pma_read_abilities,
		.config_init	= gpy21x_config_init,
		.probe		= gpy_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.config_aneg	= gpy_config_aneg,
		.aneg_done	= genphy_c45_aneg_done,
		.read_status	= gpy_read_status,
		.config_intr	= gpy_config_intr,
		.handle_interrupt = gpy_handle_interrupt,
		.set_wol	= gpy_set_wol,
		.get_wol	= gpy_get_wol,
		.set_loopback	= gpy_loopback,
		.led_brightness_set = gpy_led_brightness_set,
		.led_hw_is_supported = gpy_led_hw_is_supported,
		.led_hw_control_get = gpy_led_hw_control_get,
		.led_hw_control_set = gpy_led_hw_control_set,
		.led_polarity_set = gpy_led_polarity_set,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_GPY212C),
		.name		= "Maxlinear Ethernet GPY212C",
		.get_features	= genphy_c45_pma_read_abilities,
		.config_init	= gpy21x_config_init,
		.probe		= gpy_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.config_aneg	= gpy_config_aneg,
		.aneg_done	= genphy_c45_aneg_done,
		.read_status	= gpy_read_status,
		.config_intr	= gpy_config_intr,
		.handle_interrupt = gpy_handle_interrupt,
		.set_wol	= gpy_set_wol,
		.get_wol	= gpy_get_wol,
		.set_loopback	= gpy_loopback,
		.led_brightness_set = gpy_led_brightness_set,
		.led_hw_is_supported = gpy_led_hw_is_supported,
		.led_hw_control_get = gpy_led_hw_control_get,
		.led_hw_control_set = gpy_led_hw_control_set,
		.led_polarity_set = gpy_led_polarity_set,
	},
	{
		.phy_id		= PHY_ID_GPY215B,
		.phy_id_mask	= PHY_ID_GPYx15B_MASK,
		.name		= "Maxlinear Ethernet GPY215B",
		.get_features	= genphy_c45_pma_read_abilities,
		.config_init	= gpy21x_config_init,
		.probe		= gpy_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.config_aneg	= gpy_config_aneg,
		.aneg_done	= genphy_c45_aneg_done,
		.read_status	= gpy_read_status,
		.config_intr	= gpy_config_intr,
		.handle_interrupt = gpy_handle_interrupt,
		.set_wol	= gpy_set_wol,
		.get_wol	= gpy_get_wol,
		.set_loopback	= gpy_loopback,
		.led_brightness_set = gpy_led_brightness_set,
		.led_hw_is_supported = gpy_led_hw_is_supported,
		.led_hw_control_get = gpy_led_hw_control_get,
		.led_hw_control_set = gpy_led_hw_control_set,
		.led_polarity_set = gpy_led_polarity_set,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_GPY215C),
		.name		= "Maxlinear Ethernet GPY215C",
		.get_features	= genphy_c45_pma_read_abilities,
		.config_init	= gpy21x_config_init,
		.probe		= gpy_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.config_aneg	= gpy_config_aneg,
		.aneg_done	= genphy_c45_aneg_done,
		.read_status	= gpy_read_status,
		.config_intr	= gpy_config_intr,
		.handle_interrupt = gpy_handle_interrupt,
		.set_wol	= gpy_set_wol,
		.get_wol	= gpy_get_wol,
		.set_loopback	= gpy_loopback,
		.led_brightness_set = gpy_led_brightness_set,
		.led_hw_is_supported = gpy_led_hw_is_supported,
		.led_hw_control_get = gpy_led_hw_control_get,
		.led_hw_control_set = gpy_led_hw_control_set,
		.led_polarity_set = gpy_led_polarity_set,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_GPY241B),
		.name		= "Maxlinear Ethernet GPY241B",
		.get_features	= genphy_c45_pma_read_abilities,
		.config_init	= gpy_config_init,
		.probe		= gpy_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.config_aneg	= gpy_config_aneg,
		.aneg_done	= genphy_c45_aneg_done,
		.read_status	= gpy_read_status,
		.config_intr	= gpy_config_intr,
		.handle_interrupt = gpy_handle_interrupt,
		.set_wol	= gpy_set_wol,
		.get_wol	= gpy_get_wol,
		.set_loopback	= gpy_loopback,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_GPY241BM),
		.name		= "Maxlinear Ethernet GPY241BM",
		.get_features	= genphy_c45_pma_read_abilities,
		.config_init	= gpy_config_init,
		.probe		= gpy_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.config_aneg	= gpy_config_aneg,
		.aneg_done	= genphy_c45_aneg_done,
		.read_status	= gpy_read_status,
		.config_intr	= gpy_config_intr,
		.handle_interrupt = gpy_handle_interrupt,
		.set_wol	= gpy_set_wol,
		.get_wol	= gpy_get_wol,
		.set_loopback	= gpy_loopback,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_GPY245B),
		.name		= "Maxlinear Ethernet GPY245B",
		.get_features	= genphy_c45_pma_read_abilities,
		.config_init	= gpy_config_init,
		.probe		= gpy_probe,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.config_aneg	= gpy_config_aneg,
		.aneg_done	= genphy_c45_aneg_done,
		.read_status	= gpy_read_status,
		.config_intr	= gpy_config_intr,
		.handle_interrupt = gpy_handle_interrupt,
		.set_wol	= gpy_set_wol,
		.get_wol	= gpy_get_wol,
		.set_loopback	= gpy_loopback,
	},
};
module_phy_driver(gpy_drivers);

static struct mdio_device_id __maybe_unused gpy_tbl[] = {
	{PHY_ID_MATCH_MODEL(PHY_ID_GPY2xx)},
	{PHY_ID_GPY115B, PHY_ID_GPYx15B_MASK},
	{PHY_ID_MATCH_MODEL(PHY_ID_GPY115C)},
	{PHY_ID_GPY211B, PHY_ID_GPY21xB_MASK},
	{PHY_ID_MATCH_MODEL(PHY_ID_GPY211C)},
	{PHY_ID_GPY212B, PHY_ID_GPY21xB_MASK},
	{PHY_ID_MATCH_MODEL(PHY_ID_GPY212C)},
	{PHY_ID_GPY215B, PHY_ID_GPYx15B_MASK},
	{PHY_ID_MATCH_MODEL(PHY_ID_GPY215C)},
	{PHY_ID_MATCH_MODEL(PHY_ID_GPY241B)},
	{PHY_ID_MATCH_MODEL(PHY_ID_GPY241BM)},
	{PHY_ID_MATCH_MODEL(PHY_ID_GPY245B)},
	{ }
};
MODULE_DEVICE_TABLE(mdio, gpy_tbl);

MODULE_DESCRIPTION("Maxlinear Ethernet GPY Driver");
MODULE_AUTHOR("Xu Liang");
MODULE_LICENSE("GPL");
