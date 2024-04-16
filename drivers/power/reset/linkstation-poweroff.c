// SPDX-License-Identifier: GPL-2.0
/*
 * LinkStation power off restart driver
 * Copyright (C) 2020 Daniel González Cabanelas <dgcbueu@gmail.com>
 */

#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/reboot.h>
#include <linux/phy.h>

/* Defines from the eth phy Marvell driver */
#define MII_MARVELL_COPPER_PAGE		0
#define MII_MARVELL_LED_PAGE		3
#define MII_MARVELL_WOL_PAGE		17
#define MII_MARVELL_PHY_PAGE		22

#define MII_PHY_LED_CTRL		16
#define MII_PHY_LED_POL_CTRL		17
#define MII_88E1318S_PHY_LED_TCR	18
#define MII_88E1318S_PHY_WOL_CTRL	16
#define MII_M1011_IEVENT		19

#define MII_88E1318S_PHY_LED_TCR_INTn_ENABLE		BIT(7)
#define MII_88E1318S_PHY_LED_TCR_FORCE_INT		BIT(15)
#define MII_88E1318S_PHY_WOL_CTRL_CLEAR_WOL_STATUS	BIT(12)
#define LED2_FORCE_ON					(0x8 << 8)
#define LEDMASK						GENMASK(11,8)

#define MII_88E1318S_PHY_LED_POL_LED2		BIT(4)

struct power_off_cfg {
	char *mdio_node_name;
	void (*phy_set_reg)(bool restart);
};

static struct phy_device *phydev;
static const struct power_off_cfg *cfg;

static void linkstation_mvphy_reg_intn(bool restart)
{
	int rc = 0, saved_page;
	u16 data = 0;

	if (restart)
		data = MII_88E1318S_PHY_LED_TCR_FORCE_INT;

	saved_page = phy_select_page(phydev, MII_MARVELL_LED_PAGE);
	if (saved_page < 0)
		goto err;

	/* Force manual LED2 control to let INTn work */
	__phy_modify(phydev, MII_PHY_LED_CTRL, LEDMASK, LED2_FORCE_ON);

	/* Set the LED[2]/INTn pin to the required state */
	__phy_modify(phydev, MII_88E1318S_PHY_LED_TCR,
		     MII_88E1318S_PHY_LED_TCR_FORCE_INT,
		     MII_88E1318S_PHY_LED_TCR_INTn_ENABLE | data);

	if (!data) {
		/* Clear interrupts to ensure INTn won't be holded in high state */
		__phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_MARVELL_COPPER_PAGE);
		__phy_read(phydev, MII_M1011_IEVENT);

		/* If WOL was enabled and a magic packet was received before powering
		 * off, we won't be able to wake up by sending another magic packet.
		 * Clear WOL status.
		 */
		__phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_MARVELL_WOL_PAGE);
		__phy_set_bits(phydev, MII_88E1318S_PHY_WOL_CTRL,
			       MII_88E1318S_PHY_WOL_CTRL_CLEAR_WOL_STATUS);
	}
err:
	rc = phy_restore_page(phydev, saved_page, rc);
	if (rc < 0)
		dev_err(&phydev->mdio.dev, "Write register failed, %d\n", rc);
}

static void readynas_mvphy_set_reg(bool restart)
{
	int rc = 0, saved_page;
	u16 data = 0;

	if (restart)
		data = MII_88E1318S_PHY_LED_POL_LED2;

	saved_page = phy_select_page(phydev, MII_MARVELL_LED_PAGE);
	if (saved_page < 0)
		goto err;

	/* Set the LED[2].0 Polarity bit to the required state */
	__phy_modify(phydev, MII_PHY_LED_POL_CTRL,
		     MII_88E1318S_PHY_LED_POL_LED2, data);

	if (!data) {
		/* If WOL was enabled and a magic packet was received before powering
		 * off, we won't be able to wake up by sending another magic packet.
		 * Clear WOL status.
		 */
		__phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_MARVELL_WOL_PAGE);
		__phy_set_bits(phydev, MII_88E1318S_PHY_WOL_CTRL,
			       MII_88E1318S_PHY_WOL_CTRL_CLEAR_WOL_STATUS);
	}
err:
	rc = phy_restore_page(phydev, saved_page, rc);
	if (rc < 0)
		dev_err(&phydev->mdio.dev, "Write register failed, %d\n", rc);
}

static const struct power_off_cfg linkstation_power_off_cfg = {
	.mdio_node_name = "mdio",
	.phy_set_reg = linkstation_mvphy_reg_intn,
};

static const struct power_off_cfg readynas_power_off_cfg = {
	.mdio_node_name = "mdio-bus",
	.phy_set_reg = readynas_mvphy_set_reg,
};

static int linkstation_reboot_notifier(struct notifier_block *nb,
				       unsigned long action, void *unused)
{
	if (action == SYS_RESTART)
		cfg->phy_set_reg(true);

	return NOTIFY_DONE;
}

static struct notifier_block linkstation_reboot_nb = {
	.notifier_call = linkstation_reboot_notifier,
};

static void linkstation_poweroff(void)
{
	unregister_reboot_notifier(&linkstation_reboot_nb);
	cfg->phy_set_reg(false);

	kernel_restart("Power off");
}

static const struct of_device_id ls_poweroff_of_match[] = {
	{ .compatible = "buffalo,ls421d",
	  .data = &linkstation_power_off_cfg,
	},
	{ .compatible = "buffalo,ls421de",
	  .data = &linkstation_power_off_cfg,
	},
	{ .compatible = "netgear,readynas-duo-v2",
	  .data = &readynas_power_off_cfg,
	},
	{ },
};

static int __init linkstation_poweroff_init(void)
{
	struct mii_bus *bus;
	struct device_node *dn;
	const struct of_device_id *match;

	dn = of_find_matching_node(NULL, ls_poweroff_of_match);
	if (!dn)
		return -ENODEV;
	of_node_put(dn);

	match = of_match_node(ls_poweroff_of_match, dn);
	cfg = match->data;

	dn = of_find_node_by_name(NULL, cfg->mdio_node_name);
	if (!dn)
		return -ENODEV;

	bus = of_mdio_find_bus(dn);
	of_node_put(dn);
	if (!bus)
		return -EPROBE_DEFER;

	phydev = phy_find_first(bus);
	put_device(&bus->dev);
	if (!phydev)
		return -EPROBE_DEFER;

	register_reboot_notifier(&linkstation_reboot_nb);
	pm_power_off = linkstation_poweroff;

	return 0;
}

static void __exit linkstation_poweroff_exit(void)
{
	pm_power_off = NULL;
	unregister_reboot_notifier(&linkstation_reboot_nb);
}

module_init(linkstation_poweroff_init);
module_exit(linkstation_poweroff_exit);

MODULE_AUTHOR("Daniel González Cabanelas <dgcbueu@gmail.com>");
MODULE_DESCRIPTION("LinkStation power off driver");
MODULE_LICENSE("GPL v2");
