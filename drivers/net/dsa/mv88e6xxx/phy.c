// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88e6xxx Ethernet switch PHY and PPU support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2017 Andrew Lunn <andrew@lunn.ch>
 */

#include <linux/mdio.h>
#include <linux/module.h>

#include "chip.h"
#include "phy.h"

int mv88e6165_phy_read(struct mv88e6xxx_chip *chip, struct mii_bus *bus,
		       int addr, int reg, u16 *val)
{
	return mv88e6xxx_read(chip, addr, reg, val);
}

int mv88e6165_phy_write(struct mv88e6xxx_chip *chip, struct mii_bus *bus,
			int addr, int reg, u16 val)
{
	return mv88e6xxx_write(chip, addr, reg, val);
}

int mv88e6xxx_phy_read(struct mv88e6xxx_chip *chip, int phy, int reg, u16 *val)
{
	int addr = phy; /* PHY devices addresses start at 0x0 */
	struct mii_bus *bus;

	bus = mv88e6xxx_default_mdio_bus(chip);
	if (!bus)
		return -EOPNOTSUPP;

	if (!chip->info->ops->phy_read)
		return -EOPNOTSUPP;

	return chip->info->ops->phy_read(chip, bus, addr, reg, val);
}

int mv88e6xxx_phy_write(struct mv88e6xxx_chip *chip, int phy, int reg, u16 val)
{
	int addr = phy; /* PHY devices addresses start at 0x0 */
	struct mii_bus *bus;

	bus = mv88e6xxx_default_mdio_bus(chip);
	if (!bus)
		return -EOPNOTSUPP;

	if (!chip->info->ops->phy_write)
		return -EOPNOTSUPP;

	return chip->info->ops->phy_write(chip, bus, addr, reg, val);
}

static int mv88e6xxx_phy_page_get(struct mv88e6xxx_chip *chip, int phy, u8 page)
{
	return mv88e6xxx_phy_write(chip, phy, MV88E6XXX_PHY_PAGE, page);
}

static void mv88e6xxx_phy_page_put(struct mv88e6xxx_chip *chip, int phy)
{
	int err;

	/* Restore PHY page Copper 0x0 for access via the registered
	 * MDIO bus
	 */
	err = mv88e6xxx_phy_write(chip, phy, MV88E6XXX_PHY_PAGE,
				  MV88E6XXX_PHY_PAGE_COPPER);
	if (unlikely(err)) {
		dev_err(chip->dev,
			"failed to restore PHY %d page Copper (%d)\n",
			phy, err);
	}
}

int mv88e6xxx_phy_page_read(struct mv88e6xxx_chip *chip, int phy,
			    u8 page, int reg, u16 *val)
{
	int err;

	/* There is no paging for registers 22 */
	if (reg == MV88E6XXX_PHY_PAGE)
		return -EINVAL;

	err = mv88e6xxx_phy_page_get(chip, phy, page);
	if (!err) {
		err = mv88e6xxx_phy_read(chip, phy, reg, val);
		mv88e6xxx_phy_page_put(chip, phy);
	}

	return err;
}

int mv88e6xxx_phy_page_write(struct mv88e6xxx_chip *chip, int phy,
			     u8 page, int reg, u16 val)
{
	int err;

	/* There is no paging for registers 22 */
	if (reg == MV88E6XXX_PHY_PAGE)
		return -EINVAL;

	err = mv88e6xxx_phy_page_get(chip, phy, page);
	if (!err) {
		err = mv88e6xxx_phy_write(chip, phy, MV88E6XXX_PHY_PAGE, page);
		if (!err)
			err = mv88e6xxx_phy_write(chip, phy, reg, val);

		mv88e6xxx_phy_page_put(chip, phy);
	}

	return err;
}

static int mv88e6xxx_phy_ppu_disable(struct mv88e6xxx_chip *chip)
{
	if (!chip->info->ops->ppu_disable)
		return 0;

	return chip->info->ops->ppu_disable(chip);
}

static int mv88e6xxx_phy_ppu_enable(struct mv88e6xxx_chip *chip)
{
	if (!chip->info->ops->ppu_enable)
		return 0;

	return chip->info->ops->ppu_enable(chip);
}

static void mv88e6xxx_phy_ppu_reenable_work(struct work_struct *ugly)
{
	struct mv88e6xxx_chip *chip;

	chip = container_of(ugly, struct mv88e6xxx_chip, ppu_work);

	mv88e6xxx_reg_lock(chip);

	if (mutex_trylock(&chip->ppu_mutex)) {
		if (mv88e6xxx_phy_ppu_enable(chip) == 0)
			chip->ppu_disabled = 0;
		mutex_unlock(&chip->ppu_mutex);
	}

	mv88e6xxx_reg_unlock(chip);
}

static void mv88e6xxx_phy_ppu_reenable_timer(struct timer_list *t)
{
	struct mv88e6xxx_chip *chip = from_timer(chip, t, ppu_timer);

	schedule_work(&chip->ppu_work);
}

static int mv88e6xxx_phy_ppu_access_get(struct mv88e6xxx_chip *chip)
{
	int ret;

	mutex_lock(&chip->ppu_mutex);

	/* If the PHY polling unit is enabled, disable it so that
	 * we can access the PHY registers.  If it was already
	 * disabled, cancel the timer that is going to re-enable
	 * it.
	 */
	if (!chip->ppu_disabled) {
		ret = mv88e6xxx_phy_ppu_disable(chip);
		if (ret < 0) {
			mutex_unlock(&chip->ppu_mutex);
			return ret;
		}
		chip->ppu_disabled = 1;
	} else {
		del_timer(&chip->ppu_timer);
		ret = 0;
	}

	return ret;
}

static void mv88e6xxx_phy_ppu_access_put(struct mv88e6xxx_chip *chip)
{
	/* Schedule a timer to re-enable the PHY polling unit. */
	mod_timer(&chip->ppu_timer, jiffies + msecs_to_jiffies(10));
	mutex_unlock(&chip->ppu_mutex);
}

static void mv88e6xxx_phy_ppu_state_init(struct mv88e6xxx_chip *chip)
{
	mutex_init(&chip->ppu_mutex);
	INIT_WORK(&chip->ppu_work, mv88e6xxx_phy_ppu_reenable_work);
	timer_setup(&chip->ppu_timer, mv88e6xxx_phy_ppu_reenable_timer, 0);
}

static void mv88e6xxx_phy_ppu_state_destroy(struct mv88e6xxx_chip *chip)
{
	del_timer_sync(&chip->ppu_timer);
}

int mv88e6185_phy_ppu_read(struct mv88e6xxx_chip *chip, struct mii_bus *bus,
			   int addr, int reg, u16 *val)
{
	int err;

	err = mv88e6xxx_phy_ppu_access_get(chip);
	if (!err) {
		err = mv88e6xxx_read(chip, addr, reg, val);
		mv88e6xxx_phy_ppu_access_put(chip);
	}

	return err;
}

int mv88e6185_phy_ppu_write(struct mv88e6xxx_chip *chip, struct mii_bus *bus,
			    int addr, int reg, u16 val)
{
	int err;

	err = mv88e6xxx_phy_ppu_access_get(chip);
	if (!err) {
		err = mv88e6xxx_write(chip, addr, reg, val);
		mv88e6xxx_phy_ppu_access_put(chip);
	}

	return err;
}

void mv88e6xxx_phy_init(struct mv88e6xxx_chip *chip)
{
	if (chip->info->ops->ppu_enable && chip->info->ops->ppu_disable)
		mv88e6xxx_phy_ppu_state_init(chip);
}

void mv88e6xxx_phy_destroy(struct mv88e6xxx_chip *chip)
{
	if (chip->info->ops->ppu_enable && chip->info->ops->ppu_disable)
		mv88e6xxx_phy_ppu_state_destroy(chip);
}

int mv88e6xxx_phy_setup(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_phy_ppu_enable(chip);
}
