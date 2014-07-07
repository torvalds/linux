/*
 * drivers/net/phy/at803x.c
 *
 * Driver for Atheros 803x PHY
 *
 * Author: Matus Ujhelyi <ujhelyi.m@gmail.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/phy.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>

#define AT803X_INTR_ENABLE			0x12
#define AT803X_INTR_STATUS			0x13
#define AT803X_SMART_SPEED			0x14
#define AT803X_LED_CONTROL			0x18
#define AT803X_WOL_ENABLE			0x01
#define AT803X_DEVICE_ADDR			0x03
#define AT803X_LOC_MAC_ADDR_0_15_OFFSET		0x804C
#define AT803X_LOC_MAC_ADDR_16_31_OFFSET	0x804B
#define AT803X_LOC_MAC_ADDR_32_47_OFFSET	0x804A
#define AT803X_MMD_ACCESS_CONTROL		0x0D
#define AT803X_MMD_ACCESS_CONTROL_DATA		0x0E
#define AT803X_FUNC_DATA			0x4003
#define AT803X_INER				0x0012
#define AT803X_INER_INIT			0xec00
#define AT803X_INSR				0x0013
#define AT803X_DEBUG_ADDR			0x1D
#define AT803X_DEBUG_DATA			0x1E
#define AT803X_DEBUG_SYSTEM_MODE_CTRL		0x05
#define AT803X_DEBUG_RGMII_TX_CLK_DLY		BIT(8)

#define ATH8030_PHY_ID 0x004dd076
#define ATH8031_PHY_ID 0x004dd074
#define ATH8035_PHY_ID 0x004dd072

MODULE_DESCRIPTION("Atheros 803x PHY driver");
MODULE_AUTHOR("Matus Ujhelyi");
MODULE_LICENSE("GPL");

struct at803x_priv {
	bool phy_reset:1;
	struct gpio_desc *gpiod_reset;
};

struct at803x_context {
	u16 bmcr;
	u16 advertise;
	u16 control1000;
	u16 int_enable;
	u16 smart_speed;
	u16 led_control;
};

/* save relevant PHY registers to private copy */
static void at803x_context_save(struct phy_device *phydev,
				struct at803x_context *context)
{
	context->bmcr = phy_read(phydev, MII_BMCR);
	context->advertise = phy_read(phydev, MII_ADVERTISE);
	context->control1000 = phy_read(phydev, MII_CTRL1000);
	context->int_enable = phy_read(phydev, AT803X_INTR_ENABLE);
	context->smart_speed = phy_read(phydev, AT803X_SMART_SPEED);
	context->led_control = phy_read(phydev, AT803X_LED_CONTROL);
}

/* restore relevant PHY registers from private copy */
static void at803x_context_restore(struct phy_device *phydev,
				   const struct at803x_context *context)
{
	phy_write(phydev, MII_BMCR, context->bmcr);
	phy_write(phydev, MII_ADVERTISE, context->advertise);
	phy_write(phydev, MII_CTRL1000, context->control1000);
	phy_write(phydev, AT803X_INTR_ENABLE, context->int_enable);
	phy_write(phydev, AT803X_SMART_SPEED, context->smart_speed);
	phy_write(phydev, AT803X_LED_CONTROL, context->led_control);
}

static int at803x_set_wol(struct phy_device *phydev,
			  struct ethtool_wolinfo *wol)
{
	struct net_device *ndev = phydev->attached_dev;
	const u8 *mac;
	int ret;
	u32 value;
	unsigned int i, offsets[] = {
		AT803X_LOC_MAC_ADDR_32_47_OFFSET,
		AT803X_LOC_MAC_ADDR_16_31_OFFSET,
		AT803X_LOC_MAC_ADDR_0_15_OFFSET,
	};

	if (!ndev)
		return -ENODEV;

	if (wol->wolopts & WAKE_MAGIC) {
		mac = (const u8 *) ndev->dev_addr;

		if (!is_valid_ether_addr(mac))
			return -EFAULT;

		for (i = 0; i < 3; i++) {
			phy_write(phydev, AT803X_MMD_ACCESS_CONTROL,
				  AT803X_DEVICE_ADDR);
			phy_write(phydev, AT803X_MMD_ACCESS_CONTROL_DATA,
				  offsets[i]);
			phy_write(phydev, AT803X_MMD_ACCESS_CONTROL,
				  AT803X_FUNC_DATA);
			phy_write(phydev, AT803X_MMD_ACCESS_CONTROL_DATA,
				  mac[(i * 2) + 1] | (mac[(i * 2)] << 8));
		}

		value = phy_read(phydev, AT803X_INTR_ENABLE);
		value |= AT803X_WOL_ENABLE;
		ret = phy_write(phydev, AT803X_INTR_ENABLE, value);
		if (ret)
			return ret;
		value = phy_read(phydev, AT803X_INTR_STATUS);
	} else {
		value = phy_read(phydev, AT803X_INTR_ENABLE);
		value &= (~AT803X_WOL_ENABLE);
		ret = phy_write(phydev, AT803X_INTR_ENABLE, value);
		if (ret)
			return ret;
		value = phy_read(phydev, AT803X_INTR_STATUS);
	}

	return ret;
}

static void at803x_get_wol(struct phy_device *phydev,
			   struct ethtool_wolinfo *wol)
{
	u32 value;

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	value = phy_read(phydev, AT803X_INTR_ENABLE);
	if (value & AT803X_WOL_ENABLE)
		wol->wolopts |= WAKE_MAGIC;
}

static int at803x_suspend(struct phy_device *phydev)
{
	int value;
	int wol_enabled;

	mutex_lock(&phydev->lock);

	value = phy_read(phydev, AT803X_INTR_ENABLE);
	wol_enabled = value & AT803X_WOL_ENABLE;

	value = phy_read(phydev, MII_BMCR);

	if (wol_enabled)
		value |= BMCR_ISOLATE;
	else
		value |= BMCR_PDOWN;

	phy_write(phydev, MII_BMCR, value);

	mutex_unlock(&phydev->lock);

	return 0;
}

static int at803x_resume(struct phy_device *phydev)
{
	int value;

	mutex_lock(&phydev->lock);

	value = phy_read(phydev, MII_BMCR);
	value &= ~(BMCR_PDOWN | BMCR_ISOLATE);
	phy_write(phydev, MII_BMCR, value);

	mutex_unlock(&phydev->lock);

	return 0;
}

static int at803x_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->dev;
	struct at803x_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->gpiod_reset = devm_gpiod_get(dev, "reset");
	if (IS_ERR(priv->gpiod_reset))
		priv->gpiod_reset = NULL;
	else
		gpiod_direction_output(priv->gpiod_reset, 1);

	phydev->priv = priv;

	return 0;
}

static int at803x_config_init(struct phy_device *phydev)
{
	int ret;

	ret = genphy_config_init(phydev);
	if (ret < 0)
		return ret;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID) {
		ret = phy_write(phydev, AT803X_DEBUG_ADDR,
				AT803X_DEBUG_SYSTEM_MODE_CTRL);
		if (ret)
			return ret;
		ret = phy_write(phydev, AT803X_DEBUG_DATA,
				AT803X_DEBUG_RGMII_TX_CLK_DLY);
		if (ret)
			return ret;
	}

	return 0;
}

static int at803x_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, AT803X_INSR);

	return (err < 0) ? err : 0;
}

static int at803x_config_intr(struct phy_device *phydev)
{
	int err;
	int value;

	value = phy_read(phydev, AT803X_INER);

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, AT803X_INER,
				value | AT803X_INER_INIT);
	else
		err = phy_write(phydev, AT803X_INER, 0);

	return err;
}

static void at803x_link_change_notify(struct phy_device *phydev)
{
	struct at803x_priv *priv = phydev->priv;

	/*
	 * Conduct a hardware reset for AT8030 every time a link loss is
	 * signalled. This is necessary to circumvent a hardware bug that
	 * occurs when the cable is unplugged while TX packets are pending
	 * in the FIFO. In such cases, the FIFO enters an error mode it
	 * cannot recover from by software.
	 */
	if (phydev->drv->phy_id == ATH8030_PHY_ID) {
		if (phydev->state == PHY_NOLINK) {
			if (priv->gpiod_reset && !priv->phy_reset) {
				struct at803x_context context;

				at803x_context_save(phydev, &context);

				gpiod_set_value(priv->gpiod_reset, 0);
				msleep(1);
				gpiod_set_value(priv->gpiod_reset, 1);
				msleep(1);

				at803x_context_restore(phydev, &context);

				dev_dbg(&phydev->dev, "%s(): phy was reset\n",
					__func__);
				priv->phy_reset = true;
			}
		} else {
			priv->phy_reset = false;
		}
	}
}

static struct phy_driver at803x_driver[] = {
{
	/* ATHEROS 8035 */
	.phy_id			= ATH8035_PHY_ID,
	.name			= "Atheros 8035 ethernet",
	.phy_id_mask		= 0xffffffef,
	.probe			= at803x_probe,
	.config_init		= at803x_config_init,
	.link_change_notify	= at803x_link_change_notify,
	.set_wol		= at803x_set_wol,
	.get_wol		= at803x_get_wol,
	.suspend		= at803x_suspend,
	.resume			= at803x_resume,
	.features		= PHY_GBIT_FEATURES,
	.flags			= PHY_HAS_INTERRUPT,
	.config_aneg		= genphy_config_aneg,
	.read_status		= genphy_read_status,
	.driver			= {
		.owner = THIS_MODULE,
	},
}, {
	/* ATHEROS 8030 */
	.phy_id			= ATH8030_PHY_ID,
	.name			= "Atheros 8030 ethernet",
	.phy_id_mask		= 0xffffffef,
	.probe			= at803x_probe,
	.config_init		= at803x_config_init,
	.link_change_notify	= at803x_link_change_notify,
	.set_wol		= at803x_set_wol,
	.get_wol		= at803x_get_wol,
	.suspend		= at803x_suspend,
	.resume			= at803x_resume,
	.features		= PHY_GBIT_FEATURES,
	.flags			= PHY_HAS_INTERRUPT,
	.config_aneg		= genphy_config_aneg,
	.read_status		= genphy_read_status,
	.driver			= {
		.owner = THIS_MODULE,
	},
}, {
	/* ATHEROS 8031 */
	.phy_id			= ATH8031_PHY_ID,
	.name			= "Atheros 8031 ethernet",
	.phy_id_mask		= 0xffffffef,
	.probe			= at803x_probe,
	.config_init		= at803x_config_init,
	.link_change_notify	= at803x_link_change_notify,
	.set_wol		= at803x_set_wol,
	.get_wol		= at803x_get_wol,
	.suspend		= at803x_suspend,
	.resume			= at803x_resume,
	.features		= PHY_GBIT_FEATURES,
	.flags			= PHY_HAS_INTERRUPT,
	.config_aneg		= genphy_config_aneg,
	.read_status		= genphy_read_status,
	.ack_interrupt		= &at803x_ack_interrupt,
	.config_intr		= &at803x_config_intr,
	.driver			= {
		.owner = THIS_MODULE,
	},
} };

static int __init atheros_init(void)
{
	return phy_drivers_register(at803x_driver,
				    ARRAY_SIZE(at803x_driver));
}

static void __exit atheros_exit(void)
{
	phy_drivers_unregister(at803x_driver, ARRAY_SIZE(at803x_driver));
}

module_init(atheros_init);
module_exit(atheros_exit);

static struct mdio_device_id __maybe_unused atheros_tbl[] = {
	{ ATH8030_PHY_ID, 0xffffffef },
	{ ATH8031_PHY_ID, 0xffffffef },
	{ ATH8035_PHY_ID, 0xffffffef },
	{ }
};

MODULE_DEVICE_TABLE(mdio, atheros_tbl);
