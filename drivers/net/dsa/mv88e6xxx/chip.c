/*
 * Marvell 88e6xxx Ethernet switch single-chip support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2015 CMC Electronics, Inc.
 *	Added support for VLAN Table Unit operations
 *
 * Copyright (c) 2016 Andrew Lunn <andrew@lunn.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_bridge.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_mdio.h>
#include <linux/netdevice.h>
#include <linux/gpio/consumer.h>
#include <linux/phy.h>
#include <net/dsa.h>
#include <net/switchdev.h>

#include "mv88e6xxx.h"
#include "global1.h"
#include "global2.h"
#include "port.h"

static void assert_reg_lock(struct mv88e6xxx_chip *chip)
{
	if (unlikely(!mutex_is_locked(&chip->reg_lock))) {
		dev_err(chip->dev, "Switch registers lock not held!\n");
		dump_stack();
	}
}

/* The switch ADDR[4:1] configuration pins define the chip SMI device address
 * (ADDR[0] is always zero, thus only even SMI addresses can be strapped).
 *
 * When ADDR is all zero, the chip uses Single-chip Addressing Mode, assuming it
 * is the only device connected to the SMI master. In this mode it responds to
 * all 32 possible SMI addresses, and thus maps directly the internal devices.
 *
 * When ADDR is non-zero, the chip uses Multi-chip Addressing Mode, allowing
 * multiple devices to share the SMI interface. In this mode it responds to only
 * 2 registers, used to indirectly access the internal SMI devices.
 */

static int mv88e6xxx_smi_read(struct mv88e6xxx_chip *chip,
			      int addr, int reg, u16 *val)
{
	if (!chip->smi_ops)
		return -EOPNOTSUPP;

	return chip->smi_ops->read(chip, addr, reg, val);
}

static int mv88e6xxx_smi_write(struct mv88e6xxx_chip *chip,
			       int addr, int reg, u16 val)
{
	if (!chip->smi_ops)
		return -EOPNOTSUPP;

	return chip->smi_ops->write(chip, addr, reg, val);
}

static int mv88e6xxx_smi_single_chip_read(struct mv88e6xxx_chip *chip,
					  int addr, int reg, u16 *val)
{
	int ret;

	ret = mdiobus_read_nested(chip->bus, addr, reg);
	if (ret < 0)
		return ret;

	*val = ret & 0xffff;

	return 0;
}

static int mv88e6xxx_smi_single_chip_write(struct mv88e6xxx_chip *chip,
					   int addr, int reg, u16 val)
{
	int ret;

	ret = mdiobus_write_nested(chip->bus, addr, reg, val);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct mv88e6xxx_bus_ops mv88e6xxx_smi_single_chip_ops = {
	.read = mv88e6xxx_smi_single_chip_read,
	.write = mv88e6xxx_smi_single_chip_write,
};

static int mv88e6xxx_smi_multi_chip_wait(struct mv88e6xxx_chip *chip)
{
	int ret;
	int i;

	for (i = 0; i < 16; i++) {
		ret = mdiobus_read_nested(chip->bus, chip->sw_addr, SMI_CMD);
		if (ret < 0)
			return ret;

		if ((ret & SMI_CMD_BUSY) == 0)
			return 0;
	}

	return -ETIMEDOUT;
}

static int mv88e6xxx_smi_multi_chip_read(struct mv88e6xxx_chip *chip,
					 int addr, int reg, u16 *val)
{
	int ret;

	/* Wait for the bus to become free. */
	ret = mv88e6xxx_smi_multi_chip_wait(chip);
	if (ret < 0)
		return ret;

	/* Transmit the read command. */
	ret = mdiobus_write_nested(chip->bus, chip->sw_addr, SMI_CMD,
				   SMI_CMD_OP_22_READ | (addr << 5) | reg);
	if (ret < 0)
		return ret;

	/* Wait for the read command to complete. */
	ret = mv88e6xxx_smi_multi_chip_wait(chip);
	if (ret < 0)
		return ret;

	/* Read the data. */
	ret = mdiobus_read_nested(chip->bus, chip->sw_addr, SMI_DATA);
	if (ret < 0)
		return ret;

	*val = ret & 0xffff;

	return 0;
}

static int mv88e6xxx_smi_multi_chip_write(struct mv88e6xxx_chip *chip,
					  int addr, int reg, u16 val)
{
	int ret;

	/* Wait for the bus to become free. */
	ret = mv88e6xxx_smi_multi_chip_wait(chip);
	if (ret < 0)
		return ret;

	/* Transmit the data to write. */
	ret = mdiobus_write_nested(chip->bus, chip->sw_addr, SMI_DATA, val);
	if (ret < 0)
		return ret;

	/* Transmit the write command. */
	ret = mdiobus_write_nested(chip->bus, chip->sw_addr, SMI_CMD,
				   SMI_CMD_OP_22_WRITE | (addr << 5) | reg);
	if (ret < 0)
		return ret;

	/* Wait for the write command to complete. */
	ret = mv88e6xxx_smi_multi_chip_wait(chip);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct mv88e6xxx_bus_ops mv88e6xxx_smi_multi_chip_ops = {
	.read = mv88e6xxx_smi_multi_chip_read,
	.write = mv88e6xxx_smi_multi_chip_write,
};

int mv88e6xxx_read(struct mv88e6xxx_chip *chip, int addr, int reg, u16 *val)
{
	int err;

	assert_reg_lock(chip);

	err = mv88e6xxx_smi_read(chip, addr, reg, val);
	if (err)
		return err;

	dev_dbg(chip->dev, "<- addr: 0x%.2x reg: 0x%.2x val: 0x%.4x\n",
		addr, reg, *val);

	return 0;
}

int mv88e6xxx_write(struct mv88e6xxx_chip *chip, int addr, int reg, u16 val)
{
	int err;

	assert_reg_lock(chip);

	err = mv88e6xxx_smi_write(chip, addr, reg, val);
	if (err)
		return err;

	dev_dbg(chip->dev, "-> addr: 0x%.2x reg: 0x%.2x val: 0x%.4x\n",
		addr, reg, val);

	return 0;
}

static int mv88e6xxx_phy_read(struct mv88e6xxx_chip *chip, int phy,
			      int reg, u16 *val)
{
	int addr = phy; /* PHY devices addresses start at 0x0 */

	if (!chip->info->ops->phy_read)
		return -EOPNOTSUPP;

	return chip->info->ops->phy_read(chip, addr, reg, val);
}

static int mv88e6xxx_phy_write(struct mv88e6xxx_chip *chip, int phy,
			       int reg, u16 val)
{
	int addr = phy; /* PHY devices addresses start at 0x0 */

	if (!chip->info->ops->phy_write)
		return -EOPNOTSUPP;

	return chip->info->ops->phy_write(chip, addr, reg, val);
}

static int mv88e6xxx_phy_page_get(struct mv88e6xxx_chip *chip, int phy, u8 page)
{
	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_PHY_PAGE))
		return -EOPNOTSUPP;

	return mv88e6xxx_phy_write(chip, phy, PHY_PAGE, page);
}

static void mv88e6xxx_phy_page_put(struct mv88e6xxx_chip *chip, int phy)
{
	int err;

	/* Restore PHY page Copper 0x0 for access via the registered MDIO bus */
	err = mv88e6xxx_phy_write(chip, phy, PHY_PAGE, PHY_PAGE_COPPER);
	if (unlikely(err)) {
		dev_err(chip->dev, "failed to restore PHY %d page Copper (%d)\n",
			phy, err);
	}
}

static int mv88e6xxx_phy_page_read(struct mv88e6xxx_chip *chip, int phy,
				   u8 page, int reg, u16 *val)
{
	int err;

	/* There is no paging for registers 22 */
	if (reg == PHY_PAGE)
		return -EINVAL;

	err = mv88e6xxx_phy_page_get(chip, phy, page);
	if (!err) {
		err = mv88e6xxx_phy_read(chip, phy, reg, val);
		mv88e6xxx_phy_page_put(chip, phy);
	}

	return err;
}

static int mv88e6xxx_phy_page_write(struct mv88e6xxx_chip *chip, int phy,
				    u8 page, int reg, u16 val)
{
	int err;

	/* There is no paging for registers 22 */
	if (reg == PHY_PAGE)
		return -EINVAL;

	err = mv88e6xxx_phy_page_get(chip, phy, page);
	if (!err) {
		err = mv88e6xxx_phy_write(chip, phy, PHY_PAGE, page);
		mv88e6xxx_phy_page_put(chip, phy);
	}

	return err;
}

static int mv88e6xxx_serdes_read(struct mv88e6xxx_chip *chip, int reg, u16 *val)
{
	return mv88e6xxx_phy_page_read(chip, ADDR_SERDES, SERDES_PAGE_FIBER,
				       reg, val);
}

static int mv88e6xxx_serdes_write(struct mv88e6xxx_chip *chip, int reg, u16 val)
{
	return mv88e6xxx_phy_page_write(chip, ADDR_SERDES, SERDES_PAGE_FIBER,
					reg, val);
}

static void mv88e6xxx_g1_irq_mask(struct irq_data *d)
{
	struct mv88e6xxx_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned int n = d->hwirq;

	chip->g1_irq.masked |= (1 << n);
}

static void mv88e6xxx_g1_irq_unmask(struct irq_data *d)
{
	struct mv88e6xxx_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned int n = d->hwirq;

	chip->g1_irq.masked &= ~(1 << n);
}

static irqreturn_t mv88e6xxx_g1_irq_thread_fn(int irq, void *dev_id)
{
	struct mv88e6xxx_chip *chip = dev_id;
	unsigned int nhandled = 0;
	unsigned int sub_irq;
	unsigned int n;
	u16 reg;
	int err;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_g1_read(chip, GLOBAL_STATUS, &reg);
	mutex_unlock(&chip->reg_lock);

	if (err)
		goto out;

	for (n = 0; n < chip->g1_irq.nirqs; ++n) {
		if (reg & (1 << n)) {
			sub_irq = irq_find_mapping(chip->g1_irq.domain, n);
			handle_nested_irq(sub_irq);
			++nhandled;
		}
	}
out:
	return (nhandled > 0 ? IRQ_HANDLED : IRQ_NONE);
}

static void mv88e6xxx_g1_irq_bus_lock(struct irq_data *d)
{
	struct mv88e6xxx_chip *chip = irq_data_get_irq_chip_data(d);

	mutex_lock(&chip->reg_lock);
}

static void mv88e6xxx_g1_irq_bus_sync_unlock(struct irq_data *d)
{
	struct mv88e6xxx_chip *chip = irq_data_get_irq_chip_data(d);
	u16 mask = GENMASK(chip->g1_irq.nirqs, 0);
	u16 reg;
	int err;

	err = mv88e6xxx_g1_read(chip, GLOBAL_CONTROL, &reg);
	if (err)
		goto out;

	reg &= ~mask;
	reg |= (~chip->g1_irq.masked & mask);

	err = mv88e6xxx_g1_write(chip, GLOBAL_CONTROL, reg);
	if (err)
		goto out;

out:
	mutex_unlock(&chip->reg_lock);
}

static struct irq_chip mv88e6xxx_g1_irq_chip = {
	.name			= "mv88e6xxx-g1",
	.irq_mask		= mv88e6xxx_g1_irq_mask,
	.irq_unmask		= mv88e6xxx_g1_irq_unmask,
	.irq_bus_lock		= mv88e6xxx_g1_irq_bus_lock,
	.irq_bus_sync_unlock	= mv88e6xxx_g1_irq_bus_sync_unlock,
};

static int mv88e6xxx_g1_irq_domain_map(struct irq_domain *d,
				       unsigned int irq,
				       irq_hw_number_t hwirq)
{
	struct mv88e6xxx_chip *chip = d->host_data;

	irq_set_chip_data(irq, d->host_data);
	irq_set_chip_and_handler(irq, &chip->g1_irq.chip, handle_level_irq);
	irq_set_noprobe(irq);

	return 0;
}

static const struct irq_domain_ops mv88e6xxx_g1_irq_domain_ops = {
	.map	= mv88e6xxx_g1_irq_domain_map,
	.xlate	= irq_domain_xlate_twocell,
};

static void mv88e6xxx_g1_irq_free(struct mv88e6xxx_chip *chip)
{
	int irq, virq;

	for (irq = 0; irq < 16; irq++) {
		virq = irq_find_mapping(chip->g2_irq.domain, irq);
		irq_dispose_mapping(virq);
	}

	irq_domain_remove(chip->g2_irq.domain);
}

static int mv88e6xxx_g1_irq_setup(struct mv88e6xxx_chip *chip)
{
	int err, irq;
	u16 reg;

	chip->g1_irq.nirqs = chip->info->g1_irqs;
	chip->g1_irq.domain = irq_domain_add_simple(
		NULL, chip->g1_irq.nirqs, 0,
		&mv88e6xxx_g1_irq_domain_ops, chip);
	if (!chip->g1_irq.domain)
		return -ENOMEM;

	for (irq = 0; irq < chip->g1_irq.nirqs; irq++)
		irq_create_mapping(chip->g1_irq.domain, irq);

	chip->g1_irq.chip = mv88e6xxx_g1_irq_chip;
	chip->g1_irq.masked = ~0;

	err = mv88e6xxx_g1_read(chip, GLOBAL_CONTROL, &reg);
	if (err)
		goto out;

	reg &= ~GENMASK(chip->g1_irq.nirqs, 0);

	err = mv88e6xxx_g1_write(chip, GLOBAL_CONTROL, reg);
	if (err)
		goto out;

	/* Reading the interrupt status clears (most of) them */
	err = mv88e6xxx_g1_read(chip, GLOBAL_STATUS, &reg);
	if (err)
		goto out;

	err = request_threaded_irq(chip->irq, NULL,
				   mv88e6xxx_g1_irq_thread_fn,
				   IRQF_ONESHOT | IRQF_TRIGGER_FALLING,
				   dev_name(chip->dev), chip);
	if (err)
		goto out;

	return 0;

out:
	mv88e6xxx_g1_irq_free(chip);

	return err;
}

int mv88e6xxx_wait(struct mv88e6xxx_chip *chip, int addr, int reg, u16 mask)
{
	int i;

	for (i = 0; i < 16; i++) {
		u16 val;
		int err;

		err = mv88e6xxx_read(chip, addr, reg, &val);
		if (err)
			return err;

		if (!(val & mask))
			return 0;

		usleep_range(1000, 2000);
	}

	dev_err(chip->dev, "Timeout while waiting for switch\n");
	return -ETIMEDOUT;
}

/* Indirect write to single pointer-data register with an Update bit */
int mv88e6xxx_update(struct mv88e6xxx_chip *chip, int addr, int reg, u16 update)
{
	u16 val;
	int err;

	/* Wait until the previous operation is completed */
	err = mv88e6xxx_wait(chip, addr, reg, BIT(15));
	if (err)
		return err;

	/* Set the Update bit to trigger a write operation */
	val = BIT(15) | update;

	return mv88e6xxx_write(chip, addr, reg, val);
}

static int mv88e6xxx_ppu_disable(struct mv88e6xxx_chip *chip)
{
	u16 val;
	int i, err;

	err = mv88e6xxx_g1_read(chip, GLOBAL_CONTROL, &val);
	if (err)
		return err;

	err = mv88e6xxx_g1_write(chip, GLOBAL_CONTROL,
				 val & ~GLOBAL_CONTROL_PPU_ENABLE);
	if (err)
		return err;

	for (i = 0; i < 16; i++) {
		err = mv88e6xxx_g1_read(chip, GLOBAL_STATUS, &val);
		if (err)
			return err;

		usleep_range(1000, 2000);
		if ((val & GLOBAL_STATUS_PPU_MASK) != GLOBAL_STATUS_PPU_POLLING)
			return 0;
	}

	return -ETIMEDOUT;
}

static int mv88e6xxx_ppu_enable(struct mv88e6xxx_chip *chip)
{
	u16 val;
	int i, err;

	err = mv88e6xxx_g1_read(chip, GLOBAL_CONTROL, &val);
	if (err)
		return err;

	err = mv88e6xxx_g1_write(chip, GLOBAL_CONTROL,
				 val | GLOBAL_CONTROL_PPU_ENABLE);
	if (err)
		return err;

	for (i = 0; i < 16; i++) {
		err = mv88e6xxx_g1_read(chip, GLOBAL_STATUS, &val);
		if (err)
			return err;

		usleep_range(1000, 2000);
		if ((val & GLOBAL_STATUS_PPU_MASK) == GLOBAL_STATUS_PPU_POLLING)
			return 0;
	}

	return -ETIMEDOUT;
}

static void mv88e6xxx_ppu_reenable_work(struct work_struct *ugly)
{
	struct mv88e6xxx_chip *chip;

	chip = container_of(ugly, struct mv88e6xxx_chip, ppu_work);

	mutex_lock(&chip->reg_lock);

	if (mutex_trylock(&chip->ppu_mutex)) {
		if (mv88e6xxx_ppu_enable(chip) == 0)
			chip->ppu_disabled = 0;
		mutex_unlock(&chip->ppu_mutex);
	}

	mutex_unlock(&chip->reg_lock);
}

static void mv88e6xxx_ppu_reenable_timer(unsigned long _ps)
{
	struct mv88e6xxx_chip *chip = (void *)_ps;

	schedule_work(&chip->ppu_work);
}

static int mv88e6xxx_ppu_access_get(struct mv88e6xxx_chip *chip)
{
	int ret;

	mutex_lock(&chip->ppu_mutex);

	/* If the PHY polling unit is enabled, disable it so that
	 * we can access the PHY registers.  If it was already
	 * disabled, cancel the timer that is going to re-enable
	 * it.
	 */
	if (!chip->ppu_disabled) {
		ret = mv88e6xxx_ppu_disable(chip);
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

static void mv88e6xxx_ppu_access_put(struct mv88e6xxx_chip *chip)
{
	/* Schedule a timer to re-enable the PHY polling unit. */
	mod_timer(&chip->ppu_timer, jiffies + msecs_to_jiffies(10));
	mutex_unlock(&chip->ppu_mutex);
}

static void mv88e6xxx_ppu_state_init(struct mv88e6xxx_chip *chip)
{
	mutex_init(&chip->ppu_mutex);
	INIT_WORK(&chip->ppu_work, mv88e6xxx_ppu_reenable_work);
	setup_timer(&chip->ppu_timer, mv88e6xxx_ppu_reenable_timer,
		    (unsigned long)chip);
}

static void mv88e6xxx_ppu_state_destroy(struct mv88e6xxx_chip *chip)
{
	del_timer_sync(&chip->ppu_timer);
}

static int mv88e6xxx_phy_ppu_read(struct mv88e6xxx_chip *chip, int addr,
				  int reg, u16 *val)
{
	int err;

	err = mv88e6xxx_ppu_access_get(chip);
	if (!err) {
		err = mv88e6xxx_read(chip, addr, reg, val);
		mv88e6xxx_ppu_access_put(chip);
	}

	return err;
}

static int mv88e6xxx_phy_ppu_write(struct mv88e6xxx_chip *chip, int addr,
				   int reg, u16 val)
{
	int err;

	err = mv88e6xxx_ppu_access_get(chip);
	if (!err) {
		err = mv88e6xxx_write(chip, addr, reg, val);
		mv88e6xxx_ppu_access_put(chip);
	}

	return err;
}

static bool mv88e6xxx_6065_family(struct mv88e6xxx_chip *chip)
{
	return chip->info->family == MV88E6XXX_FAMILY_6065;
}

static bool mv88e6xxx_6095_family(struct mv88e6xxx_chip *chip)
{
	return chip->info->family == MV88E6XXX_FAMILY_6095;
}

static bool mv88e6xxx_6097_family(struct mv88e6xxx_chip *chip)
{
	return chip->info->family == MV88E6XXX_FAMILY_6097;
}

static bool mv88e6xxx_6165_family(struct mv88e6xxx_chip *chip)
{
	return chip->info->family == MV88E6XXX_FAMILY_6165;
}

static bool mv88e6xxx_6185_family(struct mv88e6xxx_chip *chip)
{
	return chip->info->family == MV88E6XXX_FAMILY_6185;
}

static bool mv88e6xxx_6320_family(struct mv88e6xxx_chip *chip)
{
	return chip->info->family == MV88E6XXX_FAMILY_6320;
}

static bool mv88e6xxx_6351_family(struct mv88e6xxx_chip *chip)
{
	return chip->info->family == MV88E6XXX_FAMILY_6351;
}

static bool mv88e6xxx_6352_family(struct mv88e6xxx_chip *chip)
{
	return chip->info->family == MV88E6XXX_FAMILY_6352;
}

static int mv88e6xxx_port_setup_mac(struct mv88e6xxx_chip *chip, int port,
				    int link, int speed, int duplex,
				    phy_interface_t mode)
{
	int err;

	if (!chip->info->ops->port_set_link)
		return 0;

	/* Port's MAC control must not be changed unless the link is down */
	err = chip->info->ops->port_set_link(chip, port, 0);
	if (err)
		return err;

	if (chip->info->ops->port_set_speed) {
		err = chip->info->ops->port_set_speed(chip, port, speed);
		if (err && err != -EOPNOTSUPP)
			goto restore_link;
	}

	if (chip->info->ops->port_set_duplex) {
		err = chip->info->ops->port_set_duplex(chip, port, duplex);
		if (err && err != -EOPNOTSUPP)
			goto restore_link;
	}

	if (chip->info->ops->port_set_rgmii_delay) {
		err = chip->info->ops->port_set_rgmii_delay(chip, port, mode);
		if (err && err != -EOPNOTSUPP)
			goto restore_link;
	}

	err = 0;
restore_link:
	if (chip->info->ops->port_set_link(chip, port, link))
		netdev_err(chip->ds->ports[port].netdev,
			   "failed to restore MAC's link\n");

	return err;
}

/* We expect the switch to perform auto negotiation if there is a real
 * phy. However, in the case of a fixed link phy, we force the port
 * settings from the fixed link settings.
 */
static void mv88e6xxx_adjust_link(struct dsa_switch *ds, int port,
				  struct phy_device *phydev)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	if (!phy_is_pseudo_fixed_link(phydev))
		return;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_port_setup_mac(chip, port, phydev->link, phydev->speed,
				       phydev->duplex, phydev->interface);
	mutex_unlock(&chip->reg_lock);

	if (err && err != -EOPNOTSUPP)
		netdev_err(ds->ports[port].netdev, "failed to configure MAC\n");
}

static int _mv88e6xxx_stats_wait(struct mv88e6xxx_chip *chip)
{
	u16 val;
	int i, err;

	for (i = 0; i < 10; i++) {
		err = mv88e6xxx_g1_read(chip, GLOBAL_STATS_OP, &val);
		if ((val & GLOBAL_STATS_OP_BUSY) == 0)
			return 0;
	}

	return -ETIMEDOUT;
}

static int _mv88e6xxx_stats_snapshot(struct mv88e6xxx_chip *chip, int port)
{
	int err;

	if (mv88e6xxx_6320_family(chip) || mv88e6xxx_6352_family(chip))
		port = (port + 1) << 5;

	/* Snapshot the hardware statistics counters for this port. */
	err = mv88e6xxx_g1_write(chip, GLOBAL_STATS_OP,
				 GLOBAL_STATS_OP_CAPTURE_PORT |
				 GLOBAL_STATS_OP_HIST_RX_TX | port);
	if (err)
		return err;

	/* Wait for the snapshotting to complete. */
	return _mv88e6xxx_stats_wait(chip);
}

static void _mv88e6xxx_stats_read(struct mv88e6xxx_chip *chip,
				  int stat, u32 *val)
{
	u32 value;
	u16 reg;
	int err;

	*val = 0;

	err = mv88e6xxx_g1_write(chip, GLOBAL_STATS_OP,
				 GLOBAL_STATS_OP_READ_CAPTURED |
				 GLOBAL_STATS_OP_HIST_RX_TX | stat);
	if (err)
		return;

	err = _mv88e6xxx_stats_wait(chip);
	if (err)
		return;

	err = mv88e6xxx_g1_read(chip, GLOBAL_STATS_COUNTER_32, &reg);
	if (err)
		return;

	value = reg << 16;

	err = mv88e6xxx_g1_read(chip, GLOBAL_STATS_COUNTER_01, &reg);
	if (err)
		return;

	*val = value | reg;
}

static struct mv88e6xxx_hw_stat mv88e6xxx_hw_stats[] = {
	{ "in_good_octets",	8, 0x00, BANK0, },
	{ "in_bad_octets",	4, 0x02, BANK0, },
	{ "in_unicast",		4, 0x04, BANK0, },
	{ "in_broadcasts",	4, 0x06, BANK0, },
	{ "in_multicasts",	4, 0x07, BANK0, },
	{ "in_pause",		4, 0x16, BANK0, },
	{ "in_undersize",	4, 0x18, BANK0, },
	{ "in_fragments",	4, 0x19, BANK0, },
	{ "in_oversize",	4, 0x1a, BANK0, },
	{ "in_jabber",		4, 0x1b, BANK0, },
	{ "in_rx_error",	4, 0x1c, BANK0, },
	{ "in_fcs_error",	4, 0x1d, BANK0, },
	{ "out_octets",		8, 0x0e, BANK0, },
	{ "out_unicast",	4, 0x10, BANK0, },
	{ "out_broadcasts",	4, 0x13, BANK0, },
	{ "out_multicasts",	4, 0x12, BANK0, },
	{ "out_pause",		4, 0x15, BANK0, },
	{ "excessive",		4, 0x11, BANK0, },
	{ "collisions",		4, 0x1e, BANK0, },
	{ "deferred",		4, 0x05, BANK0, },
	{ "single",		4, 0x14, BANK0, },
	{ "multiple",		4, 0x17, BANK0, },
	{ "out_fcs_error",	4, 0x03, BANK0, },
	{ "late",		4, 0x1f, BANK0, },
	{ "hist_64bytes",	4, 0x08, BANK0, },
	{ "hist_65_127bytes",	4, 0x09, BANK0, },
	{ "hist_128_255bytes",	4, 0x0a, BANK0, },
	{ "hist_256_511bytes",	4, 0x0b, BANK0, },
	{ "hist_512_1023bytes", 4, 0x0c, BANK0, },
	{ "hist_1024_max_bytes", 4, 0x0d, BANK0, },
	{ "sw_in_discards",	4, 0x10, PORT, },
	{ "sw_in_filtered",	2, 0x12, PORT, },
	{ "sw_out_filtered",	2, 0x13, PORT, },
	{ "in_discards",	4, 0x00 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_filtered",	4, 0x01 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_accepted",	4, 0x02 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_bad_accepted",	4, 0x03 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_good_avb_class_a", 4, 0x04 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_good_avb_class_b", 4, 0x05 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_bad_avb_class_a", 4, 0x06 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_bad_avb_class_b", 4, 0x07 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "tcam_counter_0",	4, 0x08 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "tcam_counter_1",	4, 0x09 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "tcam_counter_2",	4, 0x0a | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "tcam_counter_3",	4, 0x0b | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_da_unknown",	4, 0x0e | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_management",	4, 0x0f | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_queue_0",	4, 0x10 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_queue_1",	4, 0x11 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_queue_2",	4, 0x12 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_queue_3",	4, 0x13 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_queue_4",	4, 0x14 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_queue_5",	4, 0x15 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_queue_6",	4, 0x16 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_queue_7",	4, 0x17 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_cut_through",	4, 0x18 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_octets_a",	4, 0x1a | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_octets_b",	4, 0x1b | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_management",	4, 0x1f | GLOBAL_STATS_OP_BANK_1, BANK1, },
};

static bool mv88e6xxx_has_stat(struct mv88e6xxx_chip *chip,
			       struct mv88e6xxx_hw_stat *stat)
{
	switch (stat->type) {
	case BANK0:
		return true;
	case BANK1:
		return mv88e6xxx_6320_family(chip);
	case PORT:
		return mv88e6xxx_6095_family(chip) ||
			mv88e6xxx_6185_family(chip) ||
			mv88e6xxx_6097_family(chip) ||
			mv88e6xxx_6165_family(chip) ||
			mv88e6xxx_6351_family(chip) ||
			mv88e6xxx_6352_family(chip);
	}
	return false;
}

static uint64_t _mv88e6xxx_get_ethtool_stat(struct mv88e6xxx_chip *chip,
					    struct mv88e6xxx_hw_stat *s,
					    int port)
{
	u32 low;
	u32 high = 0;
	int err;
	u16 reg;
	u64 value;

	switch (s->type) {
	case PORT:
		err = mv88e6xxx_port_read(chip, port, s->reg, &reg);
		if (err)
			return UINT64_MAX;

		low = reg;
		if (s->sizeof_stat == 4) {
			err = mv88e6xxx_port_read(chip, port, s->reg + 1, &reg);
			if (err)
				return UINT64_MAX;
			high = reg;
		}
		break;
	case BANK0:
	case BANK1:
		_mv88e6xxx_stats_read(chip, s->reg, &low);
		if (s->sizeof_stat == 8)
			_mv88e6xxx_stats_read(chip, s->reg + 1, &high);
	}
	value = (((u64)high) << 16) | low;
	return value;
}

static void mv88e6xxx_get_strings(struct dsa_switch *ds, int port,
				  uint8_t *data)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_hw_stat *stat;
	int i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(mv88e6xxx_hw_stats); i++) {
		stat = &mv88e6xxx_hw_stats[i];
		if (mv88e6xxx_has_stat(chip, stat)) {
			memcpy(data + j * ETH_GSTRING_LEN, stat->string,
			       ETH_GSTRING_LEN);
			j++;
		}
	}
}

static int mv88e6xxx_get_sset_count(struct dsa_switch *ds)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_hw_stat *stat;
	int i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(mv88e6xxx_hw_stats); i++) {
		stat = &mv88e6xxx_hw_stats[i];
		if (mv88e6xxx_has_stat(chip, stat))
			j++;
	}
	return j;
}

static void mv88e6xxx_get_ethtool_stats(struct dsa_switch *ds, int port,
					uint64_t *data)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_hw_stat *stat;
	int ret;
	int i, j;

	mutex_lock(&chip->reg_lock);

	ret = _mv88e6xxx_stats_snapshot(chip, port);
	if (ret < 0) {
		mutex_unlock(&chip->reg_lock);
		return;
	}
	for (i = 0, j = 0; i < ARRAY_SIZE(mv88e6xxx_hw_stats); i++) {
		stat = &mv88e6xxx_hw_stats[i];
		if (mv88e6xxx_has_stat(chip, stat)) {
			data[j] = _mv88e6xxx_get_ethtool_stat(chip, stat, port);
			j++;
		}
	}

	mutex_unlock(&chip->reg_lock);
}

static int mv88e6xxx_get_regs_len(struct dsa_switch *ds, int port)
{
	return 32 * sizeof(u16);
}

static void mv88e6xxx_get_regs(struct dsa_switch *ds, int port,
			       struct ethtool_regs *regs, void *_p)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;
	u16 reg;
	u16 *p = _p;
	int i;

	regs->version = 0;

	memset(p, 0xff, 32 * sizeof(u16));

	mutex_lock(&chip->reg_lock);

	for (i = 0; i < 32; i++) {

		err = mv88e6xxx_port_read(chip, port, i, &reg);
		if (!err)
			p[i] = reg;
	}

	mutex_unlock(&chip->reg_lock);
}

static int _mv88e6xxx_atu_wait(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g1_wait(chip, GLOBAL_ATU_OP, GLOBAL_ATU_OP_BUSY);
}

static int mv88e6xxx_get_eee(struct dsa_switch *ds, int port,
			     struct ethtool_eee *e)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	u16 reg;
	int err;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_EEE))
		return -EOPNOTSUPP;

	mutex_lock(&chip->reg_lock);

	err = mv88e6xxx_phy_read(chip, port, 16, &reg);
	if (err)
		goto out;

	e->eee_enabled = !!(reg & 0x0200);
	e->tx_lpi_enabled = !!(reg & 0x0100);

	err = mv88e6xxx_port_read(chip, port, PORT_STATUS, &reg);
	if (err)
		goto out;

	e->eee_active = !!(reg & PORT_STATUS_EEE);
out:
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int mv88e6xxx_set_eee(struct dsa_switch *ds, int port,
			     struct phy_device *phydev, struct ethtool_eee *e)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	u16 reg;
	int err;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_EEE))
		return -EOPNOTSUPP;

	mutex_lock(&chip->reg_lock);

	err = mv88e6xxx_phy_read(chip, port, 16, &reg);
	if (err)
		goto out;

	reg &= ~0x0300;
	if (e->eee_enabled)
		reg |= 0x0200;
	if (e->tx_lpi_enabled)
		reg |= 0x0100;

	err = mv88e6xxx_phy_write(chip, port, 16, reg);
out:
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int _mv88e6xxx_atu_cmd(struct mv88e6xxx_chip *chip, u16 fid, u16 cmd)
{
	u16 val;
	int err;

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_G1_ATU_FID)) {
		err = mv88e6xxx_g1_write(chip, GLOBAL_ATU_FID, fid);
		if (err)
			return err;
	} else if (mv88e6xxx_num_databases(chip) == 256) {
		/* ATU DBNum[7:4] are located in ATU Control 15:12 */
		err = mv88e6xxx_g1_read(chip, GLOBAL_ATU_CONTROL, &val);
		if (err)
			return err;

		err = mv88e6xxx_g1_write(chip, GLOBAL_ATU_CONTROL,
					 (val & 0xfff) | ((fid << 8) & 0xf000));
		if (err)
			return err;

		/* ATU DBNum[3:0] are located in ATU Operation 3:0 */
		cmd |= fid & 0xf;
	}

	err = mv88e6xxx_g1_write(chip, GLOBAL_ATU_OP, cmd);
	if (err)
		return err;

	return _mv88e6xxx_atu_wait(chip);
}

static int _mv88e6xxx_atu_data_write(struct mv88e6xxx_chip *chip,
				     struct mv88e6xxx_atu_entry *entry)
{
	u16 data = entry->state & GLOBAL_ATU_DATA_STATE_MASK;

	if (entry->state != GLOBAL_ATU_DATA_STATE_UNUSED) {
		unsigned int mask, shift;

		if (entry->trunk) {
			data |= GLOBAL_ATU_DATA_TRUNK;
			mask = GLOBAL_ATU_DATA_TRUNK_ID_MASK;
			shift = GLOBAL_ATU_DATA_TRUNK_ID_SHIFT;
		} else {
			mask = GLOBAL_ATU_DATA_PORT_VECTOR_MASK;
			shift = GLOBAL_ATU_DATA_PORT_VECTOR_SHIFT;
		}

		data |= (entry->portv_trunkid << shift) & mask;
	}

	return mv88e6xxx_g1_write(chip, GLOBAL_ATU_DATA, data);
}

static int _mv88e6xxx_atu_flush_move(struct mv88e6xxx_chip *chip,
				     struct mv88e6xxx_atu_entry *entry,
				     bool static_too)
{
	int op;
	int err;

	err = _mv88e6xxx_atu_wait(chip);
	if (err)
		return err;

	err = _mv88e6xxx_atu_data_write(chip, entry);
	if (err)
		return err;

	if (entry->fid) {
		op = static_too ? GLOBAL_ATU_OP_FLUSH_MOVE_ALL_DB :
			GLOBAL_ATU_OP_FLUSH_MOVE_NON_STATIC_DB;
	} else {
		op = static_too ? GLOBAL_ATU_OP_FLUSH_MOVE_ALL :
			GLOBAL_ATU_OP_FLUSH_MOVE_NON_STATIC;
	}

	return _mv88e6xxx_atu_cmd(chip, entry->fid, op);
}

static int _mv88e6xxx_atu_flush(struct mv88e6xxx_chip *chip,
				u16 fid, bool static_too)
{
	struct mv88e6xxx_atu_entry entry = {
		.fid = fid,
		.state = 0, /* EntryState bits must be 0 */
	};

	return _mv88e6xxx_atu_flush_move(chip, &entry, static_too);
}

static int _mv88e6xxx_atu_move(struct mv88e6xxx_chip *chip, u16 fid,
			       int from_port, int to_port, bool static_too)
{
	struct mv88e6xxx_atu_entry entry = {
		.trunk = false,
		.fid = fid,
	};

	/* EntryState bits must be 0xF */
	entry.state = GLOBAL_ATU_DATA_STATE_MASK;

	/* ToPort and FromPort are respectively in PortVec bits 7:4 and 3:0 */
	entry.portv_trunkid = (to_port & 0x0f) << 4;
	entry.portv_trunkid |= from_port & 0x0f;

	return _mv88e6xxx_atu_flush_move(chip, &entry, static_too);
}

static int _mv88e6xxx_atu_remove(struct mv88e6xxx_chip *chip, u16 fid,
				 int port, bool static_too)
{
	/* Destination port 0xF means remove the entries */
	return _mv88e6xxx_atu_move(chip, fid, port, 0x0f, static_too);
}

static int _mv88e6xxx_port_based_vlan_map(struct mv88e6xxx_chip *chip, int port)
{
	struct net_device *bridge = chip->ports[port].bridge_dev;
	struct dsa_switch *ds = chip->ds;
	u16 output_ports = 0;
	int i;

	/* allow CPU port or DSA link(s) to send frames to every port */
	if (dsa_is_cpu_port(ds, port) || dsa_is_dsa_port(ds, port)) {
		output_ports = ~0;
	} else {
		for (i = 0; i < mv88e6xxx_num_ports(chip); ++i) {
			/* allow sending frames to every group member */
			if (bridge && chip->ports[i].bridge_dev == bridge)
				output_ports |= BIT(i);

			/* allow sending frames to CPU port and DSA link(s) */
			if (dsa_is_cpu_port(ds, i) || dsa_is_dsa_port(ds, i))
				output_ports |= BIT(i);
		}
	}

	/* prevent frames from going back out of the port they came in on */
	output_ports &= ~BIT(port);

	return mv88e6xxx_port_set_vlan_map(chip, port, output_ports);
}

static void mv88e6xxx_port_stp_state_set(struct dsa_switch *ds, int port,
					 u8 state)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int stp_state;
	int err;

	switch (state) {
	case BR_STATE_DISABLED:
		stp_state = PORT_CONTROL_STATE_DISABLED;
		break;
	case BR_STATE_BLOCKING:
	case BR_STATE_LISTENING:
		stp_state = PORT_CONTROL_STATE_BLOCKING;
		break;
	case BR_STATE_LEARNING:
		stp_state = PORT_CONTROL_STATE_LEARNING;
		break;
	case BR_STATE_FORWARDING:
	default:
		stp_state = PORT_CONTROL_STATE_FORWARDING;
		break;
	}

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_port_set_state(chip, port, stp_state);
	mutex_unlock(&chip->reg_lock);

	if (err)
		netdev_err(ds->ports[port].netdev, "failed to update state\n");
}

static void mv88e6xxx_port_fast_age(struct dsa_switch *ds, int port)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mutex_lock(&chip->reg_lock);
	err = _mv88e6xxx_atu_remove(chip, 0, port, false);
	mutex_unlock(&chip->reg_lock);

	if (err)
		netdev_err(ds->ports[port].netdev, "failed to flush ATU\n");
}

static int _mv88e6xxx_vtu_wait(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g1_wait(chip, GLOBAL_VTU_OP, GLOBAL_VTU_OP_BUSY);
}

static int _mv88e6xxx_vtu_cmd(struct mv88e6xxx_chip *chip, u16 op)
{
	int err;

	err = mv88e6xxx_g1_write(chip, GLOBAL_VTU_OP, op);
	if (err)
		return err;

	return _mv88e6xxx_vtu_wait(chip);
}

static int _mv88e6xxx_vtu_stu_flush(struct mv88e6xxx_chip *chip)
{
	int ret;

	ret = _mv88e6xxx_vtu_wait(chip);
	if (ret < 0)
		return ret;

	return _mv88e6xxx_vtu_cmd(chip, GLOBAL_VTU_OP_FLUSH_ALL);
}

static int _mv88e6xxx_vtu_stu_data_read(struct mv88e6xxx_chip *chip,
					struct mv88e6xxx_vtu_entry *entry,
					unsigned int nibble_offset)
{
	u16 regs[3];
	int i, err;

	for (i = 0; i < 3; ++i) {
		u16 *reg = &regs[i];

		err = mv88e6xxx_g1_read(chip, GLOBAL_VTU_DATA_0_3 + i, reg);
		if (err)
			return err;
	}

	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i) {
		unsigned int shift = (i % 4) * 4 + nibble_offset;
		u16 reg = regs[i / 4];

		entry->data[i] = (reg >> shift) & GLOBAL_VTU_STU_DATA_MASK;
	}

	return 0;
}

static int mv88e6xxx_vtu_data_read(struct mv88e6xxx_chip *chip,
				   struct mv88e6xxx_vtu_entry *entry)
{
	return _mv88e6xxx_vtu_stu_data_read(chip, entry, 0);
}

static int mv88e6xxx_stu_data_read(struct mv88e6xxx_chip *chip,
				   struct mv88e6xxx_vtu_entry *entry)
{
	return _mv88e6xxx_vtu_stu_data_read(chip, entry, 2);
}

static int _mv88e6xxx_vtu_stu_data_write(struct mv88e6xxx_chip *chip,
					 struct mv88e6xxx_vtu_entry *entry,
					 unsigned int nibble_offset)
{
	u16 regs[3] = { 0 };
	int i, err;

	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i) {
		unsigned int shift = (i % 4) * 4 + nibble_offset;
		u8 data = entry->data[i];

		regs[i / 4] |= (data & GLOBAL_VTU_STU_DATA_MASK) << shift;
	}

	for (i = 0; i < 3; ++i) {
		u16 reg = regs[i];

		err = mv88e6xxx_g1_write(chip, GLOBAL_VTU_DATA_0_3 + i, reg);
		if (err)
			return err;
	}

	return 0;
}

static int mv88e6xxx_vtu_data_write(struct mv88e6xxx_chip *chip,
				    struct mv88e6xxx_vtu_entry *entry)
{
	return _mv88e6xxx_vtu_stu_data_write(chip, entry, 0);
}

static int mv88e6xxx_stu_data_write(struct mv88e6xxx_chip *chip,
				    struct mv88e6xxx_vtu_entry *entry)
{
	return _mv88e6xxx_vtu_stu_data_write(chip, entry, 2);
}

static int _mv88e6xxx_vtu_vid_write(struct mv88e6xxx_chip *chip, u16 vid)
{
	return mv88e6xxx_g1_write(chip, GLOBAL_VTU_VID,
				  vid & GLOBAL_VTU_VID_MASK);
}

static int _mv88e6xxx_vtu_getnext(struct mv88e6xxx_chip *chip,
				  struct mv88e6xxx_vtu_entry *entry)
{
	struct mv88e6xxx_vtu_entry next = { 0 };
	u16 val;
	int err;

	err = _mv88e6xxx_vtu_wait(chip);
	if (err)
		return err;

	err = _mv88e6xxx_vtu_cmd(chip, GLOBAL_VTU_OP_VTU_GET_NEXT);
	if (err)
		return err;

	err = mv88e6xxx_g1_read(chip, GLOBAL_VTU_VID, &val);
	if (err)
		return err;

	next.vid = val & GLOBAL_VTU_VID_MASK;
	next.valid = !!(val & GLOBAL_VTU_VID_VALID);

	if (next.valid) {
		err = mv88e6xxx_vtu_data_read(chip, &next);
		if (err)
			return err;

		if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_G1_VTU_FID)) {
			err = mv88e6xxx_g1_read(chip, GLOBAL_VTU_FID, &val);
			if (err)
				return err;

			next.fid = val & GLOBAL_VTU_FID_MASK;
		} else if (mv88e6xxx_num_databases(chip) == 256) {
			/* VTU DBNum[7:4] are located in VTU Operation 11:8, and
			 * VTU DBNum[3:0] are located in VTU Operation 3:0
			 */
			err = mv88e6xxx_g1_read(chip, GLOBAL_VTU_OP, &val);
			if (err)
				return err;

			next.fid = (val & 0xf00) >> 4;
			next.fid |= val & 0xf;
		}

		if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_STU)) {
			err = mv88e6xxx_g1_read(chip, GLOBAL_VTU_SID, &val);
			if (err)
				return err;

			next.sid = val & GLOBAL_VTU_SID_MASK;
		}
	}

	*entry = next;
	return 0;
}

static int mv88e6xxx_port_vlan_dump(struct dsa_switch *ds, int port,
				    struct switchdev_obj_port_vlan *vlan,
				    int (*cb)(struct switchdev_obj *obj))
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_vtu_entry next;
	u16 pvid;
	int err;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_VTU))
		return -EOPNOTSUPP;

	mutex_lock(&chip->reg_lock);

	err = mv88e6xxx_port_get_pvid(chip, port, &pvid);
	if (err)
		goto unlock;

	err = _mv88e6xxx_vtu_vid_write(chip, GLOBAL_VTU_VID_MASK);
	if (err)
		goto unlock;

	do {
		err = _mv88e6xxx_vtu_getnext(chip, &next);
		if (err)
			break;

		if (!next.valid)
			break;

		if (next.data[port] == GLOBAL_VTU_DATA_MEMBER_TAG_NON_MEMBER)
			continue;

		/* reinit and dump this VLAN obj */
		vlan->vid_begin = next.vid;
		vlan->vid_end = next.vid;
		vlan->flags = 0;

		if (next.data[port] == GLOBAL_VTU_DATA_MEMBER_TAG_UNTAGGED)
			vlan->flags |= BRIDGE_VLAN_INFO_UNTAGGED;

		if (next.vid == pvid)
			vlan->flags |= BRIDGE_VLAN_INFO_PVID;

		err = cb(&vlan->obj);
		if (err)
			break;
	} while (next.vid < GLOBAL_VTU_VID_MASK);

unlock:
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int _mv88e6xxx_vtu_loadpurge(struct mv88e6xxx_chip *chip,
				    struct mv88e6xxx_vtu_entry *entry)
{
	u16 op = GLOBAL_VTU_OP_VTU_LOAD_PURGE;
	u16 reg = 0;
	int err;

	err = _mv88e6xxx_vtu_wait(chip);
	if (err)
		return err;

	if (!entry->valid)
		goto loadpurge;

	/* Write port member tags */
	err = mv88e6xxx_vtu_data_write(chip, entry);
	if (err)
		return err;

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_STU)) {
		reg = entry->sid & GLOBAL_VTU_SID_MASK;
		err = mv88e6xxx_g1_write(chip, GLOBAL_VTU_SID, reg);
		if (err)
			return err;
	}

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_G1_VTU_FID)) {
		reg = entry->fid & GLOBAL_VTU_FID_MASK;
		err = mv88e6xxx_g1_write(chip, GLOBAL_VTU_FID, reg);
		if (err)
			return err;
	} else if (mv88e6xxx_num_databases(chip) == 256) {
		/* VTU DBNum[7:4] are located in VTU Operation 11:8, and
		 * VTU DBNum[3:0] are located in VTU Operation 3:0
		 */
		op |= (entry->fid & 0xf0) << 8;
		op |= entry->fid & 0xf;
	}

	reg = GLOBAL_VTU_VID_VALID;
loadpurge:
	reg |= entry->vid & GLOBAL_VTU_VID_MASK;
	err = mv88e6xxx_g1_write(chip, GLOBAL_VTU_VID, reg);
	if (err)
		return err;

	return _mv88e6xxx_vtu_cmd(chip, op);
}

static int _mv88e6xxx_stu_getnext(struct mv88e6xxx_chip *chip, u8 sid,
				  struct mv88e6xxx_vtu_entry *entry)
{
	struct mv88e6xxx_vtu_entry next = { 0 };
	u16 val;
	int err;

	err = _mv88e6xxx_vtu_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g1_write(chip, GLOBAL_VTU_SID,
				 sid & GLOBAL_VTU_SID_MASK);
	if (err)
		return err;

	err = _mv88e6xxx_vtu_cmd(chip, GLOBAL_VTU_OP_STU_GET_NEXT);
	if (err)
		return err;

	err = mv88e6xxx_g1_read(chip, GLOBAL_VTU_SID, &val);
	if (err)
		return err;

	next.sid = val & GLOBAL_VTU_SID_MASK;

	err = mv88e6xxx_g1_read(chip, GLOBAL_VTU_VID, &val);
	if (err)
		return err;

	next.valid = !!(val & GLOBAL_VTU_VID_VALID);

	if (next.valid) {
		err = mv88e6xxx_stu_data_read(chip, &next);
		if (err)
			return err;
	}

	*entry = next;
	return 0;
}

static int _mv88e6xxx_stu_loadpurge(struct mv88e6xxx_chip *chip,
				    struct mv88e6xxx_vtu_entry *entry)
{
	u16 reg = 0;
	int err;

	err = _mv88e6xxx_vtu_wait(chip);
	if (err)
		return err;

	if (!entry->valid)
		goto loadpurge;

	/* Write port states */
	err = mv88e6xxx_stu_data_write(chip, entry);
	if (err)
		return err;

	reg = GLOBAL_VTU_VID_VALID;
loadpurge:
	err = mv88e6xxx_g1_write(chip, GLOBAL_VTU_VID, reg);
	if (err)
		return err;

	reg = entry->sid & GLOBAL_VTU_SID_MASK;
	err = mv88e6xxx_g1_write(chip, GLOBAL_VTU_SID, reg);
	if (err)
		return err;

	return _mv88e6xxx_vtu_cmd(chip, GLOBAL_VTU_OP_STU_LOAD_PURGE);
}

static int _mv88e6xxx_fid_new(struct mv88e6xxx_chip *chip, u16 *fid)
{
	DECLARE_BITMAP(fid_bitmap, MV88E6XXX_N_FID);
	struct mv88e6xxx_vtu_entry vlan;
	int i, err;

	bitmap_zero(fid_bitmap, MV88E6XXX_N_FID);

	/* Set every FID bit used by the (un)bridged ports */
	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i) {
		err = mv88e6xxx_port_get_fid(chip, i, fid);
		if (err)
			return err;

		set_bit(*fid, fid_bitmap);
	}

	/* Set every FID bit used by the VLAN entries */
	err = _mv88e6xxx_vtu_vid_write(chip, GLOBAL_VTU_VID_MASK);
	if (err)
		return err;

	do {
		err = _mv88e6xxx_vtu_getnext(chip, &vlan);
		if (err)
			return err;

		if (!vlan.valid)
			break;

		set_bit(vlan.fid, fid_bitmap);
	} while (vlan.vid < GLOBAL_VTU_VID_MASK);

	/* The reset value 0x000 is used to indicate that multiple address
	 * databases are not needed. Return the next positive available.
	 */
	*fid = find_next_zero_bit(fid_bitmap, MV88E6XXX_N_FID, 1);
	if (unlikely(*fid >= mv88e6xxx_num_databases(chip)))
		return -ENOSPC;

	/* Clear the database */
	return _mv88e6xxx_atu_flush(chip, *fid, true);
}

static int _mv88e6xxx_vtu_new(struct mv88e6xxx_chip *chip, u16 vid,
			      struct mv88e6xxx_vtu_entry *entry)
{
	struct dsa_switch *ds = chip->ds;
	struct mv88e6xxx_vtu_entry vlan = {
		.valid = true,
		.vid = vid,
	};
	int i, err;

	err = _mv88e6xxx_fid_new(chip, &vlan.fid);
	if (err)
		return err;

	/* exclude all ports except the CPU and DSA ports */
	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i)
		vlan.data[i] = dsa_is_cpu_port(ds, i) || dsa_is_dsa_port(ds, i)
			? GLOBAL_VTU_DATA_MEMBER_TAG_UNMODIFIED
			: GLOBAL_VTU_DATA_MEMBER_TAG_NON_MEMBER;

	if (mv88e6xxx_6097_family(chip) || mv88e6xxx_6165_family(chip) ||
	    mv88e6xxx_6351_family(chip) || mv88e6xxx_6352_family(chip)) {
		struct mv88e6xxx_vtu_entry vstp;

		/* Adding a VTU entry requires a valid STU entry. As VSTP is not
		 * implemented, only one STU entry is needed to cover all VTU
		 * entries. Thus, validate the SID 0.
		 */
		vlan.sid = 0;
		err = _mv88e6xxx_stu_getnext(chip, GLOBAL_VTU_SID_MASK, &vstp);
		if (err)
			return err;

		if (vstp.sid != vlan.sid || !vstp.valid) {
			memset(&vstp, 0, sizeof(vstp));
			vstp.valid = true;
			vstp.sid = vlan.sid;

			err = _mv88e6xxx_stu_loadpurge(chip, &vstp);
			if (err)
				return err;
		}
	}

	*entry = vlan;
	return 0;
}

static int _mv88e6xxx_vtu_get(struct mv88e6xxx_chip *chip, u16 vid,
			      struct mv88e6xxx_vtu_entry *entry, bool creat)
{
	int err;

	if (!vid)
		return -EINVAL;

	err = _mv88e6xxx_vtu_vid_write(chip, vid - 1);
	if (err)
		return err;

	err = _mv88e6xxx_vtu_getnext(chip, entry);
	if (err)
		return err;

	if (entry->vid != vid || !entry->valid) {
		if (!creat)
			return -EOPNOTSUPP;
		/* -ENOENT would've been more appropriate, but switchdev expects
		 * -EOPNOTSUPP to inform bridge about an eventual software VLAN.
		 */

		err = _mv88e6xxx_vtu_new(chip, vid, entry);
	}

	return err;
}

static int mv88e6xxx_port_check_hw_vlan(struct dsa_switch *ds, int port,
					u16 vid_begin, u16 vid_end)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_vtu_entry vlan;
	int i, err;

	if (!vid_begin)
		return -EOPNOTSUPP;

	mutex_lock(&chip->reg_lock);

	err = _mv88e6xxx_vtu_vid_write(chip, vid_begin - 1);
	if (err)
		goto unlock;

	do {
		err = _mv88e6xxx_vtu_getnext(chip, &vlan);
		if (err)
			goto unlock;

		if (!vlan.valid)
			break;

		if (vlan.vid > vid_end)
			break;

		for (i = 0; i < mv88e6xxx_num_ports(chip); ++i) {
			if (dsa_is_dsa_port(ds, i) || dsa_is_cpu_port(ds, i))
				continue;

			if (vlan.data[i] ==
			    GLOBAL_VTU_DATA_MEMBER_TAG_NON_MEMBER)
				continue;

			if (chip->ports[i].bridge_dev ==
			    chip->ports[port].bridge_dev)
				break; /* same bridge, check next VLAN */

			netdev_warn(ds->ports[port].netdev,
				    "hardware VLAN %d already used by %s\n",
				    vlan.vid,
				    netdev_name(chip->ports[i].bridge_dev));
			err = -EOPNOTSUPP;
			goto unlock;
		}
	} while (vlan.vid < vid_end);

unlock:
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int mv88e6xxx_port_vlan_filtering(struct dsa_switch *ds, int port,
					 bool vlan_filtering)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	u16 mode = vlan_filtering ? PORT_CONTROL_2_8021Q_SECURE :
		PORT_CONTROL_2_8021Q_DISABLED;
	int err;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_VTU))
		return -EOPNOTSUPP;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_port_set_8021q_mode(chip, port, mode);
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int
mv88e6xxx_port_vlan_prepare(struct dsa_switch *ds, int port,
			    const struct switchdev_obj_port_vlan *vlan,
			    struct switchdev_trans *trans)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_VTU))
		return -EOPNOTSUPP;

	/* If the requested port doesn't belong to the same bridge as the VLAN
	 * members, do not support it (yet) and fallback to software VLAN.
	 */
	err = mv88e6xxx_port_check_hw_vlan(ds, port, vlan->vid_begin,
					   vlan->vid_end);
	if (err)
		return err;

	/* We don't need any dynamic resource from the kernel (yet),
	 * so skip the prepare phase.
	 */
	return 0;
}

static int _mv88e6xxx_port_vlan_add(struct mv88e6xxx_chip *chip, int port,
				    u16 vid, bool untagged)
{
	struct mv88e6xxx_vtu_entry vlan;
	int err;

	err = _mv88e6xxx_vtu_get(chip, vid, &vlan, true);
	if (err)
		return err;

	vlan.data[port] = untagged ?
		GLOBAL_VTU_DATA_MEMBER_TAG_UNTAGGED :
		GLOBAL_VTU_DATA_MEMBER_TAG_TAGGED;

	return _mv88e6xxx_vtu_loadpurge(chip, &vlan);
}

static void mv88e6xxx_port_vlan_add(struct dsa_switch *ds, int port,
				    const struct switchdev_obj_port_vlan *vlan,
				    struct switchdev_trans *trans)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	u16 vid;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_VTU))
		return;

	mutex_lock(&chip->reg_lock);

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; ++vid)
		if (_mv88e6xxx_port_vlan_add(chip, port, vid, untagged))
			netdev_err(ds->ports[port].netdev,
				   "failed to add VLAN %d%c\n",
				   vid, untagged ? 'u' : 't');

	if (pvid && mv88e6xxx_port_set_pvid(chip, port, vlan->vid_end))
		netdev_err(ds->ports[port].netdev, "failed to set PVID %d\n",
			   vlan->vid_end);

	mutex_unlock(&chip->reg_lock);
}

static int _mv88e6xxx_port_vlan_del(struct mv88e6xxx_chip *chip,
				    int port, u16 vid)
{
	struct dsa_switch *ds = chip->ds;
	struct mv88e6xxx_vtu_entry vlan;
	int i, err;

	err = _mv88e6xxx_vtu_get(chip, vid, &vlan, false);
	if (err)
		return err;

	/* Tell switchdev if this VLAN is handled in software */
	if (vlan.data[port] == GLOBAL_VTU_DATA_MEMBER_TAG_NON_MEMBER)
		return -EOPNOTSUPP;

	vlan.data[port] = GLOBAL_VTU_DATA_MEMBER_TAG_NON_MEMBER;

	/* keep the VLAN unless all ports are excluded */
	vlan.valid = false;
	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i) {
		if (dsa_is_cpu_port(ds, i) || dsa_is_dsa_port(ds, i))
			continue;

		if (vlan.data[i] != GLOBAL_VTU_DATA_MEMBER_TAG_NON_MEMBER) {
			vlan.valid = true;
			break;
		}
	}

	err = _mv88e6xxx_vtu_loadpurge(chip, &vlan);
	if (err)
		return err;

	return _mv88e6xxx_atu_remove(chip, vlan.fid, port, false);
}

static int mv88e6xxx_port_vlan_del(struct dsa_switch *ds, int port,
				   const struct switchdev_obj_port_vlan *vlan)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	u16 pvid, vid;
	int err = 0;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_VTU))
		return -EOPNOTSUPP;

	mutex_lock(&chip->reg_lock);

	err = mv88e6xxx_port_get_pvid(chip, port, &pvid);
	if (err)
		goto unlock;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; ++vid) {
		err = _mv88e6xxx_port_vlan_del(chip, port, vid);
		if (err)
			goto unlock;

		if (vid == pvid) {
			err = mv88e6xxx_port_set_pvid(chip, port, 0);
			if (err)
				goto unlock;
		}
	}

unlock:
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int _mv88e6xxx_atu_mac_write(struct mv88e6xxx_chip *chip,
				    const unsigned char *addr)
{
	int i, err;

	for (i = 0; i < 3; i++) {
		err = mv88e6xxx_g1_write(chip, GLOBAL_ATU_MAC_01 + i,
					 (addr[i * 2] << 8) | addr[i * 2 + 1]);
		if (err)
			return err;
	}

	return 0;
}

static int _mv88e6xxx_atu_mac_read(struct mv88e6xxx_chip *chip,
				   unsigned char *addr)
{
	u16 val;
	int i, err;

	for (i = 0; i < 3; i++) {
		err = mv88e6xxx_g1_read(chip, GLOBAL_ATU_MAC_01 + i, &val);
		if (err)
			return err;

		addr[i * 2] = val >> 8;
		addr[i * 2 + 1] = val & 0xff;
	}

	return 0;
}

static int _mv88e6xxx_atu_load(struct mv88e6xxx_chip *chip,
			       struct mv88e6xxx_atu_entry *entry)
{
	int ret;

	ret = _mv88e6xxx_atu_wait(chip);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_atu_mac_write(chip, entry->mac);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_atu_data_write(chip, entry);
	if (ret < 0)
		return ret;

	return _mv88e6xxx_atu_cmd(chip, entry->fid, GLOBAL_ATU_OP_LOAD_DB);
}

static int _mv88e6xxx_atu_getnext(struct mv88e6xxx_chip *chip, u16 fid,
				  struct mv88e6xxx_atu_entry *entry);

static int mv88e6xxx_atu_get(struct mv88e6xxx_chip *chip, int fid,
			     const u8 *addr, struct mv88e6xxx_atu_entry *entry)
{
	struct mv88e6xxx_atu_entry next;
	int err;

	eth_broadcast_addr(next.mac);

	err = _mv88e6xxx_atu_mac_write(chip, next.mac);
	if (err)
		return err;

	do {
		err = _mv88e6xxx_atu_getnext(chip, fid, &next);
		if (err)
			return err;

		if (next.state == GLOBAL_ATU_DATA_STATE_UNUSED)
			break;

		if (ether_addr_equal(next.mac, addr)) {
			*entry = next;
			return 0;
		}
	} while (!is_broadcast_ether_addr(next.mac));

	memset(entry, 0, sizeof(*entry));
	entry->fid = fid;
	ether_addr_copy(entry->mac, addr);

	return 0;
}

static int mv88e6xxx_port_db_load_purge(struct mv88e6xxx_chip *chip, int port,
					const unsigned char *addr, u16 vid,
					u8 state)
{
	struct mv88e6xxx_vtu_entry vlan;
	struct mv88e6xxx_atu_entry entry;
	int err;

	/* Null VLAN ID corresponds to the port private database */
	if (vid == 0)
		err = mv88e6xxx_port_get_fid(chip, port, &vlan.fid);
	else
		err = _mv88e6xxx_vtu_get(chip, vid, &vlan, false);
	if (err)
		return err;

	err = mv88e6xxx_atu_get(chip, vlan.fid, addr, &entry);
	if (err)
		return err;

	/* Purge the ATU entry only if no port is using it anymore */
	if (state == GLOBAL_ATU_DATA_STATE_UNUSED) {
		entry.portv_trunkid &= ~BIT(port);
		if (!entry.portv_trunkid)
			entry.state = GLOBAL_ATU_DATA_STATE_UNUSED;
	} else {
		entry.portv_trunkid |= BIT(port);
		entry.state = state;
	}

	return _mv88e6xxx_atu_load(chip, &entry);
}

static int mv88e6xxx_port_fdb_prepare(struct dsa_switch *ds, int port,
				      const struct switchdev_obj_port_fdb *fdb,
				      struct switchdev_trans *trans)
{
	/* We don't need any dynamic resource from the kernel (yet),
	 * so skip the prepare phase.
	 */
	return 0;
}

static void mv88e6xxx_port_fdb_add(struct dsa_switch *ds, int port,
				   const struct switchdev_obj_port_fdb *fdb,
				   struct switchdev_trans *trans)
{
	struct mv88e6xxx_chip *chip = ds->priv;

	mutex_lock(&chip->reg_lock);
	if (mv88e6xxx_port_db_load_purge(chip, port, fdb->addr, fdb->vid,
					 GLOBAL_ATU_DATA_STATE_UC_STATIC))
		netdev_err(ds->ports[port].netdev, "failed to load unicast MAC address\n");
	mutex_unlock(&chip->reg_lock);
}

static int mv88e6xxx_port_fdb_del(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_fdb *fdb)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_port_db_load_purge(chip, port, fdb->addr, fdb->vid,
					   GLOBAL_ATU_DATA_STATE_UNUSED);
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int _mv88e6xxx_atu_getnext(struct mv88e6xxx_chip *chip, u16 fid,
				  struct mv88e6xxx_atu_entry *entry)
{
	struct mv88e6xxx_atu_entry next = { 0 };
	u16 val;
	int err;

	next.fid = fid;

	err = _mv88e6xxx_atu_wait(chip);
	if (err)
		return err;

	err = _mv88e6xxx_atu_cmd(chip, fid, GLOBAL_ATU_OP_GET_NEXT_DB);
	if (err)
		return err;

	err = _mv88e6xxx_atu_mac_read(chip, next.mac);
	if (err)
		return err;

	err = mv88e6xxx_g1_read(chip, GLOBAL_ATU_DATA, &val);
	if (err)
		return err;

	next.state = val & GLOBAL_ATU_DATA_STATE_MASK;
	if (next.state != GLOBAL_ATU_DATA_STATE_UNUSED) {
		unsigned int mask, shift;

		if (val & GLOBAL_ATU_DATA_TRUNK) {
			next.trunk = true;
			mask = GLOBAL_ATU_DATA_TRUNK_ID_MASK;
			shift = GLOBAL_ATU_DATA_TRUNK_ID_SHIFT;
		} else {
			next.trunk = false;
			mask = GLOBAL_ATU_DATA_PORT_VECTOR_MASK;
			shift = GLOBAL_ATU_DATA_PORT_VECTOR_SHIFT;
		}

		next.portv_trunkid = (val & mask) >> shift;
	}

	*entry = next;
	return 0;
}

static int mv88e6xxx_port_db_dump_fid(struct mv88e6xxx_chip *chip,
				      u16 fid, u16 vid, int port,
				      struct switchdev_obj *obj,
				      int (*cb)(struct switchdev_obj *obj))
{
	struct mv88e6xxx_atu_entry addr = {
		.mac = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
	};
	int err;

	err = _mv88e6xxx_atu_mac_write(chip, addr.mac);
	if (err)
		return err;

	do {
		err = _mv88e6xxx_atu_getnext(chip, fid, &addr);
		if (err)
			return err;

		if (addr.state == GLOBAL_ATU_DATA_STATE_UNUSED)
			break;

		if (addr.trunk || (addr.portv_trunkid & BIT(port)) == 0)
			continue;

		if (obj->id == SWITCHDEV_OBJ_ID_PORT_FDB) {
			struct switchdev_obj_port_fdb *fdb;

			if (!is_unicast_ether_addr(addr.mac))
				continue;

			fdb = SWITCHDEV_OBJ_PORT_FDB(obj);
			fdb->vid = vid;
			ether_addr_copy(fdb->addr, addr.mac);
			if (addr.state == GLOBAL_ATU_DATA_STATE_UC_STATIC)
				fdb->ndm_state = NUD_NOARP;
			else
				fdb->ndm_state = NUD_REACHABLE;
		} else if (obj->id == SWITCHDEV_OBJ_ID_PORT_MDB) {
			struct switchdev_obj_port_mdb *mdb;

			if (!is_multicast_ether_addr(addr.mac))
				continue;

			mdb = SWITCHDEV_OBJ_PORT_MDB(obj);
			mdb->vid = vid;
			ether_addr_copy(mdb->addr, addr.mac);
		} else {
			return -EOPNOTSUPP;
		}

		err = cb(obj);
		if (err)
			return err;
	} while (!is_broadcast_ether_addr(addr.mac));

	return err;
}

static int mv88e6xxx_port_db_dump(struct mv88e6xxx_chip *chip, int port,
				  struct switchdev_obj *obj,
				  int (*cb)(struct switchdev_obj *obj))
{
	struct mv88e6xxx_vtu_entry vlan = {
		.vid = GLOBAL_VTU_VID_MASK, /* all ones */
	};
	u16 fid;
	int err;

	/* Dump port's default Filtering Information Database (VLAN ID 0) */
	err = mv88e6xxx_port_get_fid(chip, port, &fid);
	if (err)
		return err;

	err = mv88e6xxx_port_db_dump_fid(chip, fid, 0, port, obj, cb);
	if (err)
		return err;

	/* Dump VLANs' Filtering Information Databases */
	err = _mv88e6xxx_vtu_vid_write(chip, vlan.vid);
	if (err)
		return err;

	do {
		err = _mv88e6xxx_vtu_getnext(chip, &vlan);
		if (err)
			return err;

		if (!vlan.valid)
			break;

		err = mv88e6xxx_port_db_dump_fid(chip, vlan.fid, vlan.vid, port,
						 obj, cb);
		if (err)
			return err;
	} while (vlan.vid < GLOBAL_VTU_VID_MASK);

	return err;
}

static int mv88e6xxx_port_fdb_dump(struct dsa_switch *ds, int port,
				   struct switchdev_obj_port_fdb *fdb,
				   int (*cb)(struct switchdev_obj *obj))
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_port_db_dump(chip, port, &fdb->obj, cb);
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int mv88e6xxx_port_bridge_join(struct dsa_switch *ds, int port,
				      struct net_device *bridge)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int i, err = 0;

	mutex_lock(&chip->reg_lock);

	/* Assign the bridge and remap each port's VLANTable */
	chip->ports[port].bridge_dev = bridge;

	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i) {
		if (chip->ports[i].bridge_dev == bridge) {
			err = _mv88e6xxx_port_based_vlan_map(chip, i);
			if (err)
				break;
		}
	}

	mutex_unlock(&chip->reg_lock);

	return err;
}

static void mv88e6xxx_port_bridge_leave(struct dsa_switch *ds, int port)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct net_device *bridge = chip->ports[port].bridge_dev;
	int i;

	mutex_lock(&chip->reg_lock);

	/* Unassign the bridge and remap each port's VLANTable */
	chip->ports[port].bridge_dev = NULL;

	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i)
		if (i == port || chip->ports[i].bridge_dev == bridge)
			if (_mv88e6xxx_port_based_vlan_map(chip, i))
				netdev_warn(ds->ports[i].netdev,
					    "failed to remap\n");

	mutex_unlock(&chip->reg_lock);
}

static int mv88e6xxx_switch_reset(struct mv88e6xxx_chip *chip)
{
	bool ppu_active = mv88e6xxx_has(chip, MV88E6XXX_FLAG_PPU_ACTIVE);
	u16 is_reset = (ppu_active ? 0x8800 : 0xc800);
	struct gpio_desc *gpiod = chip->reset;
	unsigned long timeout;
	u16 reg;
	int err;
	int i;

	/* Set all ports to the disabled state. */
	for (i = 0; i < mv88e6xxx_num_ports(chip); i++) {
		err = mv88e6xxx_port_set_state(chip, i,
					       PORT_CONTROL_STATE_DISABLED);
		if (err)
			return err;
	}

	/* Wait for transmit queues to drain. */
	usleep_range(2000, 4000);

	/* If there is a gpio connected to the reset pin, toggle it */
	if (gpiod) {
		gpiod_set_value_cansleep(gpiod, 1);
		usleep_range(10000, 20000);
		gpiod_set_value_cansleep(gpiod, 0);
		usleep_range(10000, 20000);
	}

	/* Reset the switch. Keep the PPU active if requested. The PPU
	 * needs to be active to support indirect phy register access
	 * through global registers 0x18 and 0x19.
	 */
	if (ppu_active)
		err = mv88e6xxx_g1_write(chip, 0x04, 0xc000);
	else
		err = mv88e6xxx_g1_write(chip, 0x04, 0xc400);
	if (err)
		return err;

	/* Wait up to one second for reset to complete. */
	timeout = jiffies + 1 * HZ;
	while (time_before(jiffies, timeout)) {
		err = mv88e6xxx_g1_read(chip, 0x00, &reg);
		if (err)
			return err;

		if ((reg & is_reset) == is_reset)
			break;
		usleep_range(1000, 2000);
	}
	if (time_after(jiffies, timeout))
		err = -ETIMEDOUT;
	else
		err = 0;

	return err;
}

static int mv88e6xxx_serdes_power_on(struct mv88e6xxx_chip *chip)
{
	u16 val;
	int err;

	/* Clear Power Down bit */
	err = mv88e6xxx_serdes_read(chip, MII_BMCR, &val);
	if (err)
		return err;

	if (val & BMCR_PDOWN) {
		val &= ~BMCR_PDOWN;
		err = mv88e6xxx_serdes_write(chip, MII_BMCR, val);
	}

	return err;
}

static int mv88e6xxx_setup_port(struct mv88e6xxx_chip *chip, int port)
{
	struct dsa_switch *ds = chip->ds;
	int err;
	u16 reg;

	/* MAC Forcing register: don't force link, speed, duplex or flow control
	 * state to any particular values on physical ports, but force the CPU
	 * port and all DSA ports to their maximum bandwidth and full duplex.
	 */
	if (dsa_is_cpu_port(ds, port) || dsa_is_dsa_port(ds, port))
		err = mv88e6xxx_port_setup_mac(chip, port, LINK_FORCED_UP,
					       SPEED_MAX, DUPLEX_FULL,
					       PHY_INTERFACE_MODE_NA);
	else
		err = mv88e6xxx_port_setup_mac(chip, port, LINK_UNFORCED,
					       SPEED_UNFORCED, DUPLEX_UNFORCED,
					       PHY_INTERFACE_MODE_NA);
	if (err)
		return err;

	/* Port Control: disable Drop-on-Unlock, disable Drop-on-Lock,
	 * disable Header mode, enable IGMP/MLD snooping, disable VLAN
	 * tunneling, determine priority by looking at 802.1p and IP
	 * priority fields (IP prio has precedence), and set STP state
	 * to Forwarding.
	 *
	 * If this is the CPU link, use DSA or EDSA tagging depending
	 * on which tagging mode was configured.
	 *
	 * If this is a link to another switch, use DSA tagging mode.
	 *
	 * If this is the upstream port for this switch, enable
	 * forwarding of unknown unicasts and multicasts.
	 */
	reg = 0;
	if (mv88e6xxx_6352_family(chip) || mv88e6xxx_6351_family(chip) ||
	    mv88e6xxx_6165_family(chip) || mv88e6xxx_6097_family(chip) ||
	    mv88e6xxx_6095_family(chip) || mv88e6xxx_6065_family(chip) ||
	    mv88e6xxx_6185_family(chip) || mv88e6xxx_6320_family(chip))
		reg = PORT_CONTROL_IGMP_MLD_SNOOP |
		PORT_CONTROL_USE_TAG | PORT_CONTROL_USE_IP |
		PORT_CONTROL_STATE_FORWARDING;
	if (dsa_is_cpu_port(ds, port)) {
		if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_EDSA))
			reg |= PORT_CONTROL_FRAME_ETHER_TYPE_DSA |
				PORT_CONTROL_FORWARD_UNKNOWN_MC;
		else
			reg |= PORT_CONTROL_DSA_TAG;
		reg |= PORT_CONTROL_EGRESS_ADD_TAG |
			PORT_CONTROL_FORWARD_UNKNOWN;
	}
	if (dsa_is_dsa_port(ds, port)) {
		if (mv88e6xxx_6095_family(chip) ||
		    mv88e6xxx_6185_family(chip))
			reg |= PORT_CONTROL_DSA_TAG;
		if (mv88e6xxx_6352_family(chip) ||
		    mv88e6xxx_6351_family(chip) ||
		    mv88e6xxx_6165_family(chip) ||
		    mv88e6xxx_6097_family(chip) ||
		    mv88e6xxx_6320_family(chip)) {
			reg |= PORT_CONTROL_FRAME_MODE_DSA;
		}

		if (port == dsa_upstream_port(ds))
			reg |= PORT_CONTROL_FORWARD_UNKNOWN |
				PORT_CONTROL_FORWARD_UNKNOWN_MC;
	}
	if (reg) {
		err = mv88e6xxx_port_write(chip, port, PORT_CONTROL, reg);
		if (err)
			return err;
	}

	/* If this port is connected to a SerDes, make sure the SerDes is not
	 * powered down.
	 */
	if (mv88e6xxx_has(chip, MV88E6XXX_FLAGS_SERDES)) {
		err = mv88e6xxx_port_read(chip, port, PORT_STATUS, &reg);
		if (err)
			return err;
		reg &= PORT_STATUS_CMODE_MASK;
		if ((reg == PORT_STATUS_CMODE_100BASE_X) ||
		    (reg == PORT_STATUS_CMODE_1000BASE_X) ||
		    (reg == PORT_STATUS_CMODE_SGMII)) {
			err = mv88e6xxx_serdes_power_on(chip);
			if (err < 0)
				return err;
		}
	}

	/* Port Control 2: don't force a good FCS, set the maximum frame size to
	 * 10240 bytes, disable 802.1q tags checking, don't discard tagged or
	 * untagged frames on this port, do a destination address lookup on all
	 * received packets as usual, disable ARP mirroring and don't send a
	 * copy of all transmitted/received frames on this port to the CPU.
	 */
	reg = 0;
	if (mv88e6xxx_6352_family(chip) || mv88e6xxx_6351_family(chip) ||
	    mv88e6xxx_6165_family(chip) || mv88e6xxx_6097_family(chip) ||
	    mv88e6xxx_6095_family(chip) || mv88e6xxx_6320_family(chip) ||
	    mv88e6xxx_6185_family(chip))
		reg = PORT_CONTROL_2_MAP_DA;

	if (mv88e6xxx_6352_family(chip) || mv88e6xxx_6351_family(chip) ||
	    mv88e6xxx_6165_family(chip) || mv88e6xxx_6320_family(chip))
		reg |= PORT_CONTROL_2_JUMBO_10240;

	if (mv88e6xxx_6095_family(chip) || mv88e6xxx_6185_family(chip)) {
		/* Set the upstream port this port should use */
		reg |= dsa_upstream_port(ds);
		/* enable forwarding of unknown multicast addresses to
		 * the upstream port
		 */
		if (port == dsa_upstream_port(ds))
			reg |= PORT_CONTROL_2_FORWARD_UNKNOWN;
	}

	reg |= PORT_CONTROL_2_8021Q_DISABLED;

	if (reg) {
		err = mv88e6xxx_port_write(chip, port, PORT_CONTROL_2, reg);
		if (err)
			return err;
	}

	/* Port Association Vector: when learning source addresses
	 * of packets, add the address to the address database using
	 * a port bitmap that has only the bit for this port set and
	 * the other bits clear.
	 */
	reg = 1 << port;
	/* Disable learning for CPU port */
	if (dsa_is_cpu_port(ds, port))
		reg = 0;

	err = mv88e6xxx_port_write(chip, port, PORT_ASSOC_VECTOR, reg);
	if (err)
		return err;

	/* Egress rate control 2: disable egress rate control. */
	err = mv88e6xxx_port_write(chip, port, PORT_RATE_CONTROL_2, 0x0000);
	if (err)
		return err;

	if (mv88e6xxx_6352_family(chip) || mv88e6xxx_6351_family(chip) ||
	    mv88e6xxx_6165_family(chip) || mv88e6xxx_6097_family(chip) ||
	    mv88e6xxx_6320_family(chip)) {
		/* Do not limit the period of time that this port can
		 * be paused for by the remote end or the period of
		 * time that this port can pause the remote end.
		 */
		err = mv88e6xxx_port_write(chip, port, PORT_PAUSE_CTRL, 0x0000);
		if (err)
			return err;

		/* Port ATU control: disable limiting the number of
		 * address database entries that this port is allowed
		 * to use.
		 */
		err = mv88e6xxx_port_write(chip, port, PORT_ATU_CONTROL,
					   0x0000);
		/* Priority Override: disable DA, SA and VTU priority
		 * override.
		 */
		err = mv88e6xxx_port_write(chip, port, PORT_PRI_OVERRIDE,
					   0x0000);
		if (err)
			return err;

		/* Port Ethertype: use the Ethertype DSA Ethertype
		 * value.
		 */
		if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_EDSA)) {
			err = mv88e6xxx_port_write(chip, port, PORT_ETH_TYPE,
						   ETH_P_EDSA);
			if (err)
				return err;
		}

		/* Tag Remap: use an identity 802.1p prio -> switch
		 * prio mapping.
		 */
		err = mv88e6xxx_port_write(chip, port, PORT_TAG_REGMAP_0123,
					   0x3210);
		if (err)
			return err;

		/* Tag Remap 2: use an identity 802.1p prio -> switch
		 * prio mapping.
		 */
		err = mv88e6xxx_port_write(chip, port, PORT_TAG_REGMAP_4567,
					   0x7654);
		if (err)
			return err;
	}

	/* Rate Control: disable ingress rate limiting. */
	if (mv88e6xxx_6352_family(chip) || mv88e6xxx_6351_family(chip) ||
	    mv88e6xxx_6165_family(chip) || mv88e6xxx_6097_family(chip) ||
	    mv88e6xxx_6320_family(chip)) {
		err = mv88e6xxx_port_write(chip, port, PORT_RATE_CONTROL,
					   0x0001);
		if (err)
			return err;
	} else if (mv88e6xxx_6185_family(chip) || mv88e6xxx_6095_family(chip)) {
		err = mv88e6xxx_port_write(chip, port, PORT_RATE_CONTROL,
					   0x0000);
		if (err)
			return err;
	}

	/* Port Control 1: disable trunking, disable sending
	 * learning messages to this port.
	 */
	err = mv88e6xxx_port_write(chip, port, PORT_CONTROL_1, 0x0000);
	if (err)
		return err;

	/* Port based VLAN map: give each port the same default address
	 * database, and allow bidirectional communication between the
	 * CPU and DSA port(s), and the other ports.
	 */
	err = mv88e6xxx_port_set_fid(chip, port, 0);
	if (err)
		return err;

	err = _mv88e6xxx_port_based_vlan_map(chip, port);
	if (err)
		return err;

	/* Default VLAN ID and priority: don't set a default VLAN
	 * ID, and set the default packet priority to zero.
	 */
	return mv88e6xxx_port_write(chip, port, PORT_DEFAULT_VLAN, 0x0000);
}

static int mv88e6xxx_g1_set_switch_mac(struct mv88e6xxx_chip *chip, u8 *addr)
{
	int err;

	err = mv88e6xxx_g1_write(chip, GLOBAL_MAC_01, (addr[0] << 8) | addr[1]);
	if (err)
		return err;

	err = mv88e6xxx_g1_write(chip, GLOBAL_MAC_23, (addr[2] << 8) | addr[3]);
	if (err)
		return err;

	err = mv88e6xxx_g1_write(chip, GLOBAL_MAC_45, (addr[4] << 8) | addr[5]);
	if (err)
		return err;

	return 0;
}

static int mv88e6xxx_g1_set_age_time(struct mv88e6xxx_chip *chip,
				     unsigned int msecs)
{
	const unsigned int coeff = chip->info->age_time_coeff;
	const unsigned int min = 0x01 * coeff;
	const unsigned int max = 0xff * coeff;
	u8 age_time;
	u16 val;
	int err;

	if (msecs < min || msecs > max)
		return -ERANGE;

	/* Round to nearest multiple of coeff */
	age_time = (msecs + coeff / 2) / coeff;

	err = mv88e6xxx_g1_read(chip, GLOBAL_ATU_CONTROL, &val);
	if (err)
		return err;

	/* AgeTime is 11:4 bits */
	val &= ~0xff0;
	val |= age_time << 4;

	return mv88e6xxx_g1_write(chip, GLOBAL_ATU_CONTROL, val);
}

static int mv88e6xxx_set_ageing_time(struct dsa_switch *ds,
				     unsigned int ageing_time)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_g1_set_age_time(chip, ageing_time);
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int mv88e6xxx_g1_setup(struct mv88e6xxx_chip *chip)
{
	struct dsa_switch *ds = chip->ds;
	u32 upstream_port = dsa_upstream_port(ds);
	u16 reg;
	int err;

	/* Enable the PHY Polling Unit if present, don't discard any packets,
	 * and mask all interrupt sources.
	 */
	err = mv88e6xxx_g1_read(chip, GLOBAL_CONTROL, &reg);
	if (err < 0)
		return err;

	reg &= ~GLOBAL_CONTROL_PPU_ENABLE;
	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_PPU) ||
	    mv88e6xxx_has(chip, MV88E6XXX_FLAG_PPU_ACTIVE))
		reg |= GLOBAL_CONTROL_PPU_ENABLE;

	err = mv88e6xxx_g1_write(chip, GLOBAL_CONTROL, reg);
	if (err)
		return err;

	/* Configure the upstream port, and configure it as the port to which
	 * ingress and egress and ARP monitor frames are to be sent.
	 */
	reg = upstream_port << GLOBAL_MONITOR_CONTROL_INGRESS_SHIFT |
		upstream_port << GLOBAL_MONITOR_CONTROL_EGRESS_SHIFT |
		upstream_port << GLOBAL_MONITOR_CONTROL_ARP_SHIFT;
	err = mv88e6xxx_g1_write(chip, GLOBAL_MONITOR_CONTROL, reg);
	if (err)
		return err;

	/* Disable remote management, and set the switch's DSA device number. */
	err = mv88e6xxx_g1_write(chip, GLOBAL_CONTROL_2,
				 GLOBAL_CONTROL_2_MULTIPLE_CASCADE |
				 (ds->index & 0x1f));
	if (err)
		return err;

	/* Clear all the VTU and STU entries */
	err = _mv88e6xxx_vtu_stu_flush(chip);
	if (err < 0)
		return err;

	/* Set the default address aging time to 5 minutes, and
	 * enable address learn messages to be sent to all message
	 * ports.
	 */
	err = mv88e6xxx_g1_write(chip, GLOBAL_ATU_CONTROL,
				 GLOBAL_ATU_CONTROL_LEARN2ALL);
	if (err)
		return err;

	err = mv88e6xxx_g1_set_age_time(chip, 300000);
	if (err)
		return err;

	/* Clear all ATU entries */
	err = _mv88e6xxx_atu_flush(chip, 0, true);
	if (err)
		return err;

	/* Configure the IP ToS mapping registers. */
	err = mv88e6xxx_g1_write(chip, GLOBAL_IP_PRI_0, 0x0000);
	if (err)
		return err;
	err = mv88e6xxx_g1_write(chip, GLOBAL_IP_PRI_1, 0x0000);
	if (err)
		return err;
	err = mv88e6xxx_g1_write(chip, GLOBAL_IP_PRI_2, 0x5555);
	if (err)
		return err;
	err = mv88e6xxx_g1_write(chip, GLOBAL_IP_PRI_3, 0x5555);
	if (err)
		return err;
	err = mv88e6xxx_g1_write(chip, GLOBAL_IP_PRI_4, 0xaaaa);
	if (err)
		return err;
	err = mv88e6xxx_g1_write(chip, GLOBAL_IP_PRI_5, 0xaaaa);
	if (err)
		return err;
	err = mv88e6xxx_g1_write(chip, GLOBAL_IP_PRI_6, 0xffff);
	if (err)
		return err;
	err = mv88e6xxx_g1_write(chip, GLOBAL_IP_PRI_7, 0xffff);
	if (err)
		return err;

	/* Configure the IEEE 802.1p priority mapping register. */
	err = mv88e6xxx_g1_write(chip, GLOBAL_IEEE_PRI, 0xfa41);
	if (err)
		return err;

	/* Clear the statistics counters for all ports */
	err = mv88e6xxx_g1_write(chip, GLOBAL_STATS_OP,
				 GLOBAL_STATS_OP_FLUSH_ALL);
	if (err)
		return err;

	/* Wait for the flush to complete. */
	err = _mv88e6xxx_stats_wait(chip);
	if (err)
		return err;

	return 0;
}

static int mv88e6xxx_setup(struct dsa_switch *ds)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;
	int i;

	chip->ds = ds;
	ds->slave_mii_bus = chip->mdio_bus;

	mutex_lock(&chip->reg_lock);

	/* Setup Switch Port Registers */
	for (i = 0; i < mv88e6xxx_num_ports(chip); i++) {
		err = mv88e6xxx_setup_port(chip, i);
		if (err)
			goto unlock;
	}

	/* Setup Switch Global 1 Registers */
	err = mv88e6xxx_g1_setup(chip);
	if (err)
		goto unlock;

	/* Setup Switch Global 2 Registers */
	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_GLOBAL2)) {
		err = mv88e6xxx_g2_setup(chip);
		if (err)
			goto unlock;
	}

unlock:
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int mv88e6xxx_set_addr(struct dsa_switch *ds, u8 *addr)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	if (!chip->info->ops->set_switch_mac)
		return -EOPNOTSUPP;

	mutex_lock(&chip->reg_lock);
	err = chip->info->ops->set_switch_mac(chip, addr);
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int mv88e6xxx_mdio_read(struct mii_bus *bus, int phy, int reg)
{
	struct mv88e6xxx_chip *chip = bus->priv;
	u16 val;
	int err;

	if (phy >= mv88e6xxx_num_ports(chip))
		return 0xffff;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_phy_read(chip, phy, reg, &val);
	mutex_unlock(&chip->reg_lock);

	return err ? err : val;
}

static int mv88e6xxx_mdio_write(struct mii_bus *bus, int phy, int reg, u16 val)
{
	struct mv88e6xxx_chip *chip = bus->priv;
	int err;

	if (phy >= mv88e6xxx_num_ports(chip))
		return 0xffff;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_phy_write(chip, phy, reg, val);
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int mv88e6xxx_mdio_register(struct mv88e6xxx_chip *chip,
				   struct device_node *np)
{
	static int index;
	struct mii_bus *bus;
	int err;

	if (np)
		chip->mdio_np = of_get_child_by_name(np, "mdio");

	bus = devm_mdiobus_alloc(chip->dev);
	if (!bus)
		return -ENOMEM;

	bus->priv = (void *)chip;
	if (np) {
		bus->name = np->full_name;
		snprintf(bus->id, MII_BUS_ID_SIZE, "%s", np->full_name);
	} else {
		bus->name = "mv88e6xxx SMI";
		snprintf(bus->id, MII_BUS_ID_SIZE, "mv88e6xxx-%d", index++);
	}

	bus->read = mv88e6xxx_mdio_read;
	bus->write = mv88e6xxx_mdio_write;
	bus->parent = chip->dev;

	if (chip->mdio_np)
		err = of_mdiobus_register(bus, chip->mdio_np);
	else
		err = mdiobus_register(bus);
	if (err) {
		dev_err(chip->dev, "Cannot register MDIO bus (%d)\n", err);
		goto out;
	}
	chip->mdio_bus = bus;

	return 0;

out:
	if (chip->mdio_np)
		of_node_put(chip->mdio_np);

	return err;
}

static void mv88e6xxx_mdio_unregister(struct mv88e6xxx_chip *chip)

{
	struct mii_bus *bus = chip->mdio_bus;

	mdiobus_unregister(bus);

	if (chip->mdio_np)
		of_node_put(chip->mdio_np);
}

#ifdef CONFIG_NET_DSA_HWMON

static int mv88e61xx_get_temp(struct dsa_switch *ds, int *temp)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	u16 val;
	int ret;

	*temp = 0;

	mutex_lock(&chip->reg_lock);

	ret = mv88e6xxx_phy_write(chip, 0x0, 0x16, 0x6);
	if (ret < 0)
		goto error;

	/* Enable temperature sensor */
	ret = mv88e6xxx_phy_read(chip, 0x0, 0x1a, &val);
	if (ret < 0)
		goto error;

	ret = mv88e6xxx_phy_write(chip, 0x0, 0x1a, val | (1 << 5));
	if (ret < 0)
		goto error;

	/* Wait for temperature to stabilize */
	usleep_range(10000, 12000);

	ret = mv88e6xxx_phy_read(chip, 0x0, 0x1a, &val);
	if (ret < 0)
		goto error;

	/* Disable temperature sensor */
	ret = mv88e6xxx_phy_write(chip, 0x0, 0x1a, val & ~(1 << 5));
	if (ret < 0)
		goto error;

	*temp = ((val & 0x1f) - 5) * 5;

error:
	mv88e6xxx_phy_write(chip, 0x0, 0x16, 0x0);
	mutex_unlock(&chip->reg_lock);
	return ret;
}

static int mv88e63xx_get_temp(struct dsa_switch *ds, int *temp)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int phy = mv88e6xxx_6320_family(chip) ? 3 : 0;
	u16 val;
	int ret;

	*temp = 0;

	mutex_lock(&chip->reg_lock);
	ret = mv88e6xxx_phy_page_read(chip, phy, 6, 27, &val);
	mutex_unlock(&chip->reg_lock);
	if (ret < 0)
		return ret;

	*temp = (val & 0xff) - 25;

	return 0;
}

static int mv88e6xxx_get_temp(struct dsa_switch *ds, int *temp)
{
	struct mv88e6xxx_chip *chip = ds->priv;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_TEMP))
		return -EOPNOTSUPP;

	if (mv88e6xxx_6320_family(chip) || mv88e6xxx_6352_family(chip))
		return mv88e63xx_get_temp(ds, temp);

	return mv88e61xx_get_temp(ds, temp);
}

static int mv88e6xxx_get_temp_limit(struct dsa_switch *ds, int *temp)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int phy = mv88e6xxx_6320_family(chip) ? 3 : 0;
	u16 val;
	int ret;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_TEMP_LIMIT))
		return -EOPNOTSUPP;

	*temp = 0;

	mutex_lock(&chip->reg_lock);
	ret = mv88e6xxx_phy_page_read(chip, phy, 6, 26, &val);
	mutex_unlock(&chip->reg_lock);
	if (ret < 0)
		return ret;

	*temp = (((val >> 8) & 0x1f) * 5) - 25;

	return 0;
}

static int mv88e6xxx_set_temp_limit(struct dsa_switch *ds, int temp)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int phy = mv88e6xxx_6320_family(chip) ? 3 : 0;
	u16 val;
	int err;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_TEMP_LIMIT))
		return -EOPNOTSUPP;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_phy_page_read(chip, phy, 6, 26, &val);
	if (err)
		goto unlock;
	temp = clamp_val(DIV_ROUND_CLOSEST(temp, 5) + 5, 0, 0x1f);
	err = mv88e6xxx_phy_page_write(chip, phy, 6, 26,
				       (val & 0xe0ff) | (temp << 8));
unlock:
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int mv88e6xxx_get_temp_alarm(struct dsa_switch *ds, bool *alarm)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int phy = mv88e6xxx_6320_family(chip) ? 3 : 0;
	u16 val;
	int ret;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_TEMP_LIMIT))
		return -EOPNOTSUPP;

	*alarm = false;

	mutex_lock(&chip->reg_lock);
	ret = mv88e6xxx_phy_page_read(chip, phy, 6, 26, &val);
	mutex_unlock(&chip->reg_lock);
	if (ret < 0)
		return ret;

	*alarm = !!(val & 0x40);

	return 0;
}
#endif /* CONFIG_NET_DSA_HWMON */

static int mv88e6xxx_get_eeprom_len(struct dsa_switch *ds)
{
	struct mv88e6xxx_chip *chip = ds->priv;

	return chip->eeprom_len;
}

static int mv88e6xxx_get_eeprom(struct dsa_switch *ds,
				struct ethtool_eeprom *eeprom, u8 *data)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	if (!chip->info->ops->get_eeprom)
		return -EOPNOTSUPP;

	mutex_lock(&chip->reg_lock);
	err = chip->info->ops->get_eeprom(chip, eeprom, data);
	mutex_unlock(&chip->reg_lock);

	if (err)
		return err;

	eeprom->magic = 0xc3ec4951;

	return 0;
}

static int mv88e6xxx_set_eeprom(struct dsa_switch *ds,
				struct ethtool_eeprom *eeprom, u8 *data)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	if (!chip->info->ops->set_eeprom)
		return -EOPNOTSUPP;

	if (eeprom->magic != 0xc3ec4951)
		return -EINVAL;

	mutex_lock(&chip->reg_lock);
	err = chip->info->ops->set_eeprom(chip, eeprom, data);
	mutex_unlock(&chip->reg_lock);

	return err;
}

static const struct mv88e6xxx_ops mv88e6085_ops = {
	.set_switch_mac = mv88e6xxx_g1_set_switch_mac,
	.phy_read = mv88e6xxx_phy_ppu_read,
	.phy_write = mv88e6xxx_phy_ppu_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_set_duplex = mv88e6xxx_port_set_duplex,
	.port_set_speed = mv88e6185_port_set_speed,
};

static const struct mv88e6xxx_ops mv88e6095_ops = {
	.set_switch_mac = mv88e6xxx_g1_set_switch_mac,
	.phy_read = mv88e6xxx_phy_ppu_read,
	.phy_write = mv88e6xxx_phy_ppu_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_set_duplex = mv88e6xxx_port_set_duplex,
	.port_set_speed = mv88e6185_port_set_speed,
};

static const struct mv88e6xxx_ops mv88e6123_ops = {
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_read,
	.phy_write = mv88e6xxx_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_set_duplex = mv88e6xxx_port_set_duplex,
	.port_set_speed = mv88e6185_port_set_speed,
};

static const struct mv88e6xxx_ops mv88e6131_ops = {
	.set_switch_mac = mv88e6xxx_g1_set_switch_mac,
	.phy_read = mv88e6xxx_phy_ppu_read,
	.phy_write = mv88e6xxx_phy_ppu_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_set_duplex = mv88e6xxx_port_set_duplex,
	.port_set_speed = mv88e6185_port_set_speed,
};

static const struct mv88e6xxx_ops mv88e6161_ops = {
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_read,
	.phy_write = mv88e6xxx_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_set_duplex = mv88e6xxx_port_set_duplex,
	.port_set_speed = mv88e6185_port_set_speed,
};

static const struct mv88e6xxx_ops mv88e6165_ops = {
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_read,
	.phy_write = mv88e6xxx_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_set_duplex = mv88e6xxx_port_set_duplex,
	.port_set_speed = mv88e6185_port_set_speed,
};

static const struct mv88e6xxx_ops mv88e6171_ops = {
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read,
	.phy_write = mv88e6xxx_g2_smi_phy_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_set_duplex = mv88e6xxx_port_set_duplex,
	.port_set_speed = mv88e6185_port_set_speed,
};

static const struct mv88e6xxx_ops mv88e6172_ops = {
	.get_eeprom = mv88e6xxx_g2_get_eeprom16,
	.set_eeprom = mv88e6xxx_g2_set_eeprom16,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read,
	.phy_write = mv88e6xxx_g2_smi_phy_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_set_duplex = mv88e6xxx_port_set_duplex,
	.port_set_rgmii_delay = mv88e6352_port_set_rgmii_delay,
	.port_set_speed = mv88e6352_port_set_speed,
};

static const struct mv88e6xxx_ops mv88e6175_ops = {
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read,
	.phy_write = mv88e6xxx_g2_smi_phy_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_set_duplex = mv88e6xxx_port_set_duplex,
	.port_set_speed = mv88e6185_port_set_speed,
};

static const struct mv88e6xxx_ops mv88e6176_ops = {
	.get_eeprom = mv88e6xxx_g2_get_eeprom16,
	.set_eeprom = mv88e6xxx_g2_set_eeprom16,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read,
	.phy_write = mv88e6xxx_g2_smi_phy_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_set_duplex = mv88e6xxx_port_set_duplex,
	.port_set_rgmii_delay = mv88e6352_port_set_rgmii_delay,
	.port_set_speed = mv88e6352_port_set_speed,
};

static const struct mv88e6xxx_ops mv88e6185_ops = {
	.set_switch_mac = mv88e6xxx_g1_set_switch_mac,
	.phy_read = mv88e6xxx_phy_ppu_read,
	.phy_write = mv88e6xxx_phy_ppu_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_set_duplex = mv88e6xxx_port_set_duplex,
	.port_set_speed = mv88e6185_port_set_speed,
};

static const struct mv88e6xxx_ops mv88e6240_ops = {
	.get_eeprom = mv88e6xxx_g2_get_eeprom16,
	.set_eeprom = mv88e6xxx_g2_set_eeprom16,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read,
	.phy_write = mv88e6xxx_g2_smi_phy_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_set_duplex = mv88e6xxx_port_set_duplex,
	.port_set_rgmii_delay = mv88e6352_port_set_rgmii_delay,
	.port_set_speed = mv88e6352_port_set_speed,
};

static const struct mv88e6xxx_ops mv88e6320_ops = {
	.get_eeprom = mv88e6xxx_g2_get_eeprom16,
	.set_eeprom = mv88e6xxx_g2_set_eeprom16,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read,
	.phy_write = mv88e6xxx_g2_smi_phy_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_set_duplex = mv88e6xxx_port_set_duplex,
	.port_set_speed = mv88e6185_port_set_speed,
};

static const struct mv88e6xxx_ops mv88e6321_ops = {
	.get_eeprom = mv88e6xxx_g2_get_eeprom16,
	.set_eeprom = mv88e6xxx_g2_set_eeprom16,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read,
	.phy_write = mv88e6xxx_g2_smi_phy_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_set_duplex = mv88e6xxx_port_set_duplex,
	.port_set_speed = mv88e6185_port_set_speed,
};

static const struct mv88e6xxx_ops mv88e6350_ops = {
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read,
	.phy_write = mv88e6xxx_g2_smi_phy_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_set_duplex = mv88e6xxx_port_set_duplex,
	.port_set_speed = mv88e6185_port_set_speed,
};

static const struct mv88e6xxx_ops mv88e6351_ops = {
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read,
	.phy_write = mv88e6xxx_g2_smi_phy_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_set_duplex = mv88e6xxx_port_set_duplex,
	.port_set_speed = mv88e6185_port_set_speed,
};

static const struct mv88e6xxx_ops mv88e6352_ops = {
	.get_eeprom = mv88e6xxx_g2_get_eeprom16,
	.set_eeprom = mv88e6xxx_g2_set_eeprom16,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read,
	.phy_write = mv88e6xxx_g2_smi_phy_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_set_duplex = mv88e6xxx_port_set_duplex,
	.port_set_rgmii_delay = mv88e6352_port_set_rgmii_delay,
	.port_set_speed = mv88e6352_port_set_speed,
};

static const struct mv88e6xxx_info mv88e6xxx_table[] = {
	[MV88E6085] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6085,
		.family = MV88E6XXX_FAMILY_6097,
		.name = "Marvell 88E6085",
		.num_databases = 4096,
		.num_ports = 10,
		.port_base_addr = 0x10,
		.global1_addr = 0x1b,
		.age_time_coeff = 15000,
		.g1_irqs = 8,
		.flags = MV88E6XXX_FLAGS_FAMILY_6097,
		.ops = &mv88e6085_ops,
	},

	[MV88E6095] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6095,
		.family = MV88E6XXX_FAMILY_6095,
		.name = "Marvell 88E6095/88E6095F",
		.num_databases = 256,
		.num_ports = 11,
		.port_base_addr = 0x10,
		.global1_addr = 0x1b,
		.age_time_coeff = 15000,
		.g1_irqs = 8,
		.flags = MV88E6XXX_FLAGS_FAMILY_6095,
		.ops = &mv88e6095_ops,
	},

	[MV88E6123] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6123,
		.family = MV88E6XXX_FAMILY_6165,
		.name = "Marvell 88E6123",
		.num_databases = 4096,
		.num_ports = 3,
		.port_base_addr = 0x10,
		.global1_addr = 0x1b,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.flags = MV88E6XXX_FLAGS_FAMILY_6165,
		.ops = &mv88e6123_ops,
	},

	[MV88E6131] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6131,
		.family = MV88E6XXX_FAMILY_6185,
		.name = "Marvell 88E6131",
		.num_databases = 256,
		.num_ports = 8,
		.port_base_addr = 0x10,
		.global1_addr = 0x1b,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.flags = MV88E6XXX_FLAGS_FAMILY_6185,
		.ops = &mv88e6131_ops,
	},

	[MV88E6161] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6161,
		.family = MV88E6XXX_FAMILY_6165,
		.name = "Marvell 88E6161",
		.num_databases = 4096,
		.num_ports = 6,
		.port_base_addr = 0x10,
		.global1_addr = 0x1b,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.flags = MV88E6XXX_FLAGS_FAMILY_6165,
		.ops = &mv88e6161_ops,
	},

	[MV88E6165] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6165,
		.family = MV88E6XXX_FAMILY_6165,
		.name = "Marvell 88E6165",
		.num_databases = 4096,
		.num_ports = 6,
		.port_base_addr = 0x10,
		.global1_addr = 0x1b,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.flags = MV88E6XXX_FLAGS_FAMILY_6165,
		.ops = &mv88e6165_ops,
	},

	[MV88E6171] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6171,
		.family = MV88E6XXX_FAMILY_6351,
		.name = "Marvell 88E6171",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.global1_addr = 0x1b,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.flags = MV88E6XXX_FLAGS_FAMILY_6351,
		.ops = &mv88e6171_ops,
	},

	[MV88E6172] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6172,
		.family = MV88E6XXX_FAMILY_6352,
		.name = "Marvell 88E6172",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.global1_addr = 0x1b,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.flags = MV88E6XXX_FLAGS_FAMILY_6352,
		.ops = &mv88e6172_ops,
	},

	[MV88E6175] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6175,
		.family = MV88E6XXX_FAMILY_6351,
		.name = "Marvell 88E6175",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.global1_addr = 0x1b,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.flags = MV88E6XXX_FLAGS_FAMILY_6351,
		.ops = &mv88e6175_ops,
	},

	[MV88E6176] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6176,
		.family = MV88E6XXX_FAMILY_6352,
		.name = "Marvell 88E6176",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.global1_addr = 0x1b,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.flags = MV88E6XXX_FLAGS_FAMILY_6352,
		.ops = &mv88e6176_ops,
	},

	[MV88E6185] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6185,
		.family = MV88E6XXX_FAMILY_6185,
		.name = "Marvell 88E6185",
		.num_databases = 256,
		.num_ports = 10,
		.port_base_addr = 0x10,
		.global1_addr = 0x1b,
		.age_time_coeff = 15000,
		.g1_irqs = 8,
		.flags = MV88E6XXX_FLAGS_FAMILY_6185,
		.ops = &mv88e6185_ops,
	},

	[MV88E6240] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6240,
		.family = MV88E6XXX_FAMILY_6352,
		.name = "Marvell 88E6240",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.global1_addr = 0x1b,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.flags = MV88E6XXX_FLAGS_FAMILY_6352,
		.ops = &mv88e6240_ops,
	},

	[MV88E6320] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6320,
		.family = MV88E6XXX_FAMILY_6320,
		.name = "Marvell 88E6320",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.global1_addr = 0x1b,
		.age_time_coeff = 15000,
		.g1_irqs = 8,
		.flags = MV88E6XXX_FLAGS_FAMILY_6320,
		.ops = &mv88e6320_ops,
	},

	[MV88E6321] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6321,
		.family = MV88E6XXX_FAMILY_6320,
		.name = "Marvell 88E6321",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.global1_addr = 0x1b,
		.age_time_coeff = 15000,
		.g1_irqs = 8,
		.flags = MV88E6XXX_FLAGS_FAMILY_6320,
		.ops = &mv88e6321_ops,
	},

	[MV88E6350] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6350,
		.family = MV88E6XXX_FAMILY_6351,
		.name = "Marvell 88E6350",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.global1_addr = 0x1b,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.flags = MV88E6XXX_FLAGS_FAMILY_6351,
		.ops = &mv88e6350_ops,
	},

	[MV88E6351] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6351,
		.family = MV88E6XXX_FAMILY_6351,
		.name = "Marvell 88E6351",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.global1_addr = 0x1b,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.flags = MV88E6XXX_FLAGS_FAMILY_6351,
		.ops = &mv88e6351_ops,
	},

	[MV88E6352] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6352,
		.family = MV88E6XXX_FAMILY_6352,
		.name = "Marvell 88E6352",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.global1_addr = 0x1b,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.flags = MV88E6XXX_FLAGS_FAMILY_6352,
		.ops = &mv88e6352_ops,
	},
};

static const struct mv88e6xxx_info *mv88e6xxx_lookup_info(unsigned int prod_num)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mv88e6xxx_table); ++i)
		if (mv88e6xxx_table[i].prod_num == prod_num)
			return &mv88e6xxx_table[i];

	return NULL;
}

static int mv88e6xxx_detect(struct mv88e6xxx_chip *chip)
{
	const struct mv88e6xxx_info *info;
	unsigned int prod_num, rev;
	u16 id;
	int err;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_port_read(chip, 0, PORT_SWITCH_ID, &id);
	mutex_unlock(&chip->reg_lock);
	if (err)
		return err;

	prod_num = (id & 0xfff0) >> 4;
	rev = id & 0x000f;

	info = mv88e6xxx_lookup_info(prod_num);
	if (!info)
		return -ENODEV;

	/* Update the compatible info with the probed one */
	chip->info = info;

	err = mv88e6xxx_g2_require(chip);
	if (err)
		return err;

	dev_info(chip->dev, "switch 0x%x detected: %s, revision %u\n",
		 chip->info->prod_num, chip->info->name, rev);

	return 0;
}

static struct mv88e6xxx_chip *mv88e6xxx_alloc_chip(struct device *dev)
{
	struct mv88e6xxx_chip *chip;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return NULL;

	chip->dev = dev;

	mutex_init(&chip->reg_lock);

	return chip;
}

static void mv88e6xxx_phy_init(struct mv88e6xxx_chip *chip)
{
	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_PPU))
		mv88e6xxx_ppu_state_init(chip);
}

static void mv88e6xxx_phy_destroy(struct mv88e6xxx_chip *chip)
{
	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_PPU))
		mv88e6xxx_ppu_state_destroy(chip);
}

static int mv88e6xxx_smi_init(struct mv88e6xxx_chip *chip,
			      struct mii_bus *bus, int sw_addr)
{
	/* ADDR[0] pin is unavailable externally and considered zero */
	if (sw_addr & 0x1)
		return -EINVAL;

	if (sw_addr == 0)
		chip->smi_ops = &mv88e6xxx_smi_single_chip_ops;
	else if (mv88e6xxx_has(chip, MV88E6XXX_FLAGS_MULTI_CHIP))
		chip->smi_ops = &mv88e6xxx_smi_multi_chip_ops;
	else
		return -EINVAL;

	chip->bus = bus;
	chip->sw_addr = sw_addr;

	return 0;
}

static enum dsa_tag_protocol mv88e6xxx_get_tag_protocol(struct dsa_switch *ds)
{
	struct mv88e6xxx_chip *chip = ds->priv;

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_EDSA))
		return DSA_TAG_PROTO_EDSA;

	return DSA_TAG_PROTO_DSA;
}

static const char *mv88e6xxx_drv_probe(struct device *dsa_dev,
				       struct device *host_dev, int sw_addr,
				       void **priv)
{
	struct mv88e6xxx_chip *chip;
	struct mii_bus *bus;
	int err;

	bus = dsa_host_dev_to_mii_bus(host_dev);
	if (!bus)
		return NULL;

	chip = mv88e6xxx_alloc_chip(dsa_dev);
	if (!chip)
		return NULL;

	/* Legacy SMI probing will only support chips similar to 88E6085 */
	chip->info = &mv88e6xxx_table[MV88E6085];

	err = mv88e6xxx_smi_init(chip, bus, sw_addr);
	if (err)
		goto free;

	err = mv88e6xxx_detect(chip);
	if (err)
		goto free;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_switch_reset(chip);
	mutex_unlock(&chip->reg_lock);
	if (err)
		goto free;

	mv88e6xxx_phy_init(chip);

	err = mv88e6xxx_mdio_register(chip, NULL);
	if (err)
		goto free;

	*priv = chip;

	return chip->info->name;
free:
	devm_kfree(dsa_dev, chip);

	return NULL;
}

static int mv88e6xxx_port_mdb_prepare(struct dsa_switch *ds, int port,
				      const struct switchdev_obj_port_mdb *mdb,
				      struct switchdev_trans *trans)
{
	/* We don't need any dynamic resource from the kernel (yet),
	 * so skip the prepare phase.
	 */

	return 0;
}

static void mv88e6xxx_port_mdb_add(struct dsa_switch *ds, int port,
				   const struct switchdev_obj_port_mdb *mdb,
				   struct switchdev_trans *trans)
{
	struct mv88e6xxx_chip *chip = ds->priv;

	mutex_lock(&chip->reg_lock);
	if (mv88e6xxx_port_db_load_purge(chip, port, mdb->addr, mdb->vid,
					 GLOBAL_ATU_DATA_STATE_MC_STATIC))
		netdev_err(ds->ports[port].netdev, "failed to load multicast MAC address\n");
	mutex_unlock(&chip->reg_lock);
}

static int mv88e6xxx_port_mdb_del(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_mdb *mdb)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_port_db_load_purge(chip, port, mdb->addr, mdb->vid,
					   GLOBAL_ATU_DATA_STATE_UNUSED);
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int mv88e6xxx_port_mdb_dump(struct dsa_switch *ds, int port,
				   struct switchdev_obj_port_mdb *mdb,
				   int (*cb)(struct switchdev_obj *obj))
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_port_db_dump(chip, port, &mdb->obj, cb);
	mutex_unlock(&chip->reg_lock);

	return err;
}

static struct dsa_switch_ops mv88e6xxx_switch_ops = {
	.probe			= mv88e6xxx_drv_probe,
	.get_tag_protocol	= mv88e6xxx_get_tag_protocol,
	.setup			= mv88e6xxx_setup,
	.set_addr		= mv88e6xxx_set_addr,
	.adjust_link		= mv88e6xxx_adjust_link,
	.get_strings		= mv88e6xxx_get_strings,
	.get_ethtool_stats	= mv88e6xxx_get_ethtool_stats,
	.get_sset_count		= mv88e6xxx_get_sset_count,
	.set_eee		= mv88e6xxx_set_eee,
	.get_eee		= mv88e6xxx_get_eee,
#ifdef CONFIG_NET_DSA_HWMON
	.get_temp		= mv88e6xxx_get_temp,
	.get_temp_limit		= mv88e6xxx_get_temp_limit,
	.set_temp_limit		= mv88e6xxx_set_temp_limit,
	.get_temp_alarm		= mv88e6xxx_get_temp_alarm,
#endif
	.get_eeprom_len		= mv88e6xxx_get_eeprom_len,
	.get_eeprom		= mv88e6xxx_get_eeprom,
	.set_eeprom		= mv88e6xxx_set_eeprom,
	.get_regs_len		= mv88e6xxx_get_regs_len,
	.get_regs		= mv88e6xxx_get_regs,
	.set_ageing_time	= mv88e6xxx_set_ageing_time,
	.port_bridge_join	= mv88e6xxx_port_bridge_join,
	.port_bridge_leave	= mv88e6xxx_port_bridge_leave,
	.port_stp_state_set	= mv88e6xxx_port_stp_state_set,
	.port_fast_age		= mv88e6xxx_port_fast_age,
	.port_vlan_filtering	= mv88e6xxx_port_vlan_filtering,
	.port_vlan_prepare	= mv88e6xxx_port_vlan_prepare,
	.port_vlan_add		= mv88e6xxx_port_vlan_add,
	.port_vlan_del		= mv88e6xxx_port_vlan_del,
	.port_vlan_dump		= mv88e6xxx_port_vlan_dump,
	.port_fdb_prepare       = mv88e6xxx_port_fdb_prepare,
	.port_fdb_add           = mv88e6xxx_port_fdb_add,
	.port_fdb_del           = mv88e6xxx_port_fdb_del,
	.port_fdb_dump          = mv88e6xxx_port_fdb_dump,
	.port_mdb_prepare       = mv88e6xxx_port_mdb_prepare,
	.port_mdb_add           = mv88e6xxx_port_mdb_add,
	.port_mdb_del           = mv88e6xxx_port_mdb_del,
	.port_mdb_dump          = mv88e6xxx_port_mdb_dump,
};

static int mv88e6xxx_register_switch(struct mv88e6xxx_chip *chip,
				     struct device_node *np)
{
	struct device *dev = chip->dev;
	struct dsa_switch *ds;

	ds = devm_kzalloc(dev, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return -ENOMEM;

	ds->dev = dev;
	ds->priv = chip;
	ds->ops = &mv88e6xxx_switch_ops;

	dev_set_drvdata(dev, ds);

	return dsa_register_switch(ds, np);
}

static void mv88e6xxx_unregister_switch(struct mv88e6xxx_chip *chip)
{
	dsa_unregister_switch(chip->ds);
}

static int mv88e6xxx_probe(struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct device_node *np = dev->of_node;
	const struct mv88e6xxx_info *compat_info;
	struct mv88e6xxx_chip *chip;
	u32 eeprom_len;
	int err;

	compat_info = of_device_get_match_data(dev);
	if (!compat_info)
		return -EINVAL;

	chip = mv88e6xxx_alloc_chip(dev);
	if (!chip)
		return -ENOMEM;

	chip->info = compat_info;

	err = mv88e6xxx_smi_init(chip, mdiodev->bus, mdiodev->addr);
	if (err)
		return err;

	err = mv88e6xxx_detect(chip);
	if (err)
		return err;

	mv88e6xxx_phy_init(chip);

	chip->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(chip->reset))
		return PTR_ERR(chip->reset);

	if (chip->info->ops->get_eeprom &&
	    !of_property_read_u32(np, "eeprom-length", &eeprom_len))
		chip->eeprom_len = eeprom_len;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_switch_reset(chip);
	mutex_unlock(&chip->reg_lock);
	if (err)
		goto out;

	chip->irq = of_irq_get(np, 0);
	if (chip->irq == -EPROBE_DEFER) {
		err = chip->irq;
		goto out;
	}

	if (chip->irq > 0) {
		/* Has to be performed before the MDIO bus is created,
		 * because the PHYs will link there interrupts to these
		 * interrupt controllers
		 */
		mutex_lock(&chip->reg_lock);
		err = mv88e6xxx_g1_irq_setup(chip);
		mutex_unlock(&chip->reg_lock);

		if (err)
			goto out;

		if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_G2_INT)) {
			err = mv88e6xxx_g2_irq_setup(chip);
			if (err)
				goto out_g1_irq;
		}
	}

	err = mv88e6xxx_mdio_register(chip, np);
	if (err)
		goto out_g2_irq;

	err = mv88e6xxx_register_switch(chip, np);
	if (err)
		goto out_mdio;

	return 0;

out_mdio:
	mv88e6xxx_mdio_unregister(chip);
out_g2_irq:
	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_G2_INT))
		mv88e6xxx_g2_irq_free(chip);
out_g1_irq:
	mv88e6xxx_g1_irq_free(chip);
out:
	return err;
}

static void mv88e6xxx_remove(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);
	struct mv88e6xxx_chip *chip = ds->priv;

	mv88e6xxx_phy_destroy(chip);
	mv88e6xxx_unregister_switch(chip);
	mv88e6xxx_mdio_unregister(chip);

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_G2_INT))
		mv88e6xxx_g2_irq_free(chip);
	mv88e6xxx_g1_irq_free(chip);
}

static const struct of_device_id mv88e6xxx_of_match[] = {
	{
		.compatible = "marvell,mv88e6085",
		.data = &mv88e6xxx_table[MV88E6085],
	},
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, mv88e6xxx_of_match);

static struct mdio_driver mv88e6xxx_driver = {
	.probe	= mv88e6xxx_probe,
	.remove = mv88e6xxx_remove,
	.mdiodrv.driver = {
		.name = "mv88e6085",
		.of_match_table = mv88e6xxx_of_match,
	},
};

static int __init mv88e6xxx_init(void)
{
	register_switch_driver(&mv88e6xxx_switch_ops);
	return mdio_driver_register(&mv88e6xxx_driver);
}
module_init(mv88e6xxx_init);

static void __exit mv88e6xxx_cleanup(void)
{
	mdio_driver_unregister(&mv88e6xxx_driver);
	unregister_switch_driver(&mv88e6xxx_switch_ops);
}
module_exit(mv88e6xxx_cleanup);

MODULE_AUTHOR("Lennert Buytenhek <buytenh@wantstofly.org>");
MODULE_DESCRIPTION("Driver for Marvell 88E6XXX ethernet switch chips");
MODULE_LICENSE("GPL");
