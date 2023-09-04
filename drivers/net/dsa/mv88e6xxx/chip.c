// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88e6xxx Ethernet switch single-chip support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2016 Andrew Lunn <andrew@lunn.ch>
 *
 * Copyright (c) 2016-2017 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/dsa/mv88e6xxx.h>
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
#include <linux/platform_data/mv88e6xxx.h>
#include <linux/netdevice.h>
#include <linux/gpio/consumer.h>
#include <linux/phylink.h>
#include <net/dsa.h>

#include "chip.h"
#include "devlink.h"
#include "global1.h"
#include "global2.h"
#include "hwtstamp.h"
#include "phy.h"
#include "port.h"
#include "ptp.h"
#include "serdes.h"
#include "smi.h"

static void assert_reg_lock(struct mv88e6xxx_chip *chip)
{
	if (unlikely(!mutex_is_locked(&chip->reg_lock))) {
		dev_err(chip->dev, "Switch registers lock not held!\n");
		dump_stack();
	}
}

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

int mv88e6xxx_wait_mask(struct mv88e6xxx_chip *chip, int addr, int reg,
			u16 mask, u16 val)
{
	const unsigned long timeout = jiffies + msecs_to_jiffies(50);
	u16 data;
	int err;
	int i;

	/* There's no bus specific operation to wait for a mask. Even
	 * if the initial poll takes longer than 50ms, always do at
	 * least one more attempt.
	 */
	for (i = 0; time_before(jiffies, timeout) || (i < 2); i++) {
		err = mv88e6xxx_read(chip, addr, reg, &data);
		if (err)
			return err;

		if ((data & mask) == val)
			return 0;

		if (i < 2)
			cpu_relax();
		else
			usleep_range(1000, 2000);
	}

	err = mv88e6xxx_read(chip, addr, reg, &data);
	if (err)
		return err;

	if ((data & mask) == val)
		return 0;

	dev_err(chip->dev, "Timeout while waiting for switch\n");
	return -ETIMEDOUT;
}

int mv88e6xxx_wait_bit(struct mv88e6xxx_chip *chip, int addr, int reg,
		       int bit, int val)
{
	return mv88e6xxx_wait_mask(chip, addr, reg, BIT(bit),
				   val ? BIT(bit) : 0x0000);
}

struct mii_bus *mv88e6xxx_default_mdio_bus(struct mv88e6xxx_chip *chip)
{
	struct mv88e6xxx_mdio_bus *mdio_bus;

	mdio_bus = list_first_entry(&chip->mdios, struct mv88e6xxx_mdio_bus,
				    list);
	if (!mdio_bus)
		return NULL;

	return mdio_bus->bus;
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

static irqreturn_t mv88e6xxx_g1_irq_thread_work(struct mv88e6xxx_chip *chip)
{
	unsigned int nhandled = 0;
	unsigned int sub_irq;
	unsigned int n;
	u16 reg;
	u16 ctl1;
	int err;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_STS, &reg);
	mv88e6xxx_reg_unlock(chip);

	if (err)
		goto out;

	do {
		for (n = 0; n < chip->g1_irq.nirqs; ++n) {
			if (reg & (1 << n)) {
				sub_irq = irq_find_mapping(chip->g1_irq.domain,
							   n);
				handle_nested_irq(sub_irq);
				++nhandled;
			}
		}

		mv88e6xxx_reg_lock(chip);
		err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_CTL1, &ctl1);
		if (err)
			goto unlock;
		err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_STS, &reg);
unlock:
		mv88e6xxx_reg_unlock(chip);
		if (err)
			goto out;
		ctl1 &= GENMASK(chip->g1_irq.nirqs, 0);
	} while (reg & ctl1);

out:
	return (nhandled > 0 ? IRQ_HANDLED : IRQ_NONE);
}

static irqreturn_t mv88e6xxx_g1_irq_thread_fn(int irq, void *dev_id)
{
	struct mv88e6xxx_chip *chip = dev_id;

	return mv88e6xxx_g1_irq_thread_work(chip);
}

static void mv88e6xxx_g1_irq_bus_lock(struct irq_data *d)
{
	struct mv88e6xxx_chip *chip = irq_data_get_irq_chip_data(d);

	mv88e6xxx_reg_lock(chip);
}

static void mv88e6xxx_g1_irq_bus_sync_unlock(struct irq_data *d)
{
	struct mv88e6xxx_chip *chip = irq_data_get_irq_chip_data(d);
	u16 mask = GENMASK(chip->g1_irq.nirqs, 0);
	u16 reg;
	int err;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_CTL1, &reg);
	if (err)
		goto out;

	reg &= ~mask;
	reg |= (~chip->g1_irq.masked & mask);

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_CTL1, reg);
	if (err)
		goto out;

out:
	mv88e6xxx_reg_unlock(chip);
}

static const struct irq_chip mv88e6xxx_g1_irq_chip = {
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

/* To be called with reg_lock held */
static void mv88e6xxx_g1_irq_free_common(struct mv88e6xxx_chip *chip)
{
	int irq, virq;
	u16 mask;

	mv88e6xxx_g1_read(chip, MV88E6XXX_G1_CTL1, &mask);
	mask &= ~GENMASK(chip->g1_irq.nirqs, 0);
	mv88e6xxx_g1_write(chip, MV88E6XXX_G1_CTL1, mask);

	for (irq = 0; irq < chip->g1_irq.nirqs; irq++) {
		virq = irq_find_mapping(chip->g1_irq.domain, irq);
		irq_dispose_mapping(virq);
	}

	irq_domain_remove(chip->g1_irq.domain);
}

static void mv88e6xxx_g1_irq_free(struct mv88e6xxx_chip *chip)
{
	/*
	 * free_irq must be called without reg_lock taken because the irq
	 * handler takes this lock, too.
	 */
	free_irq(chip->irq, chip);

	mv88e6xxx_reg_lock(chip);
	mv88e6xxx_g1_irq_free_common(chip);
	mv88e6xxx_reg_unlock(chip);
}

static int mv88e6xxx_g1_irq_setup_common(struct mv88e6xxx_chip *chip)
{
	int err, irq, virq;
	u16 reg, mask;

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

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_CTL1, &mask);
	if (err)
		goto out_mapping;

	mask &= ~GENMASK(chip->g1_irq.nirqs, 0);

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_CTL1, mask);
	if (err)
		goto out_disable;

	/* Reading the interrupt status clears (most of) them */
	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_STS, &reg);
	if (err)
		goto out_disable;

	return 0;

out_disable:
	mask &= ~GENMASK(chip->g1_irq.nirqs, 0);
	mv88e6xxx_g1_write(chip, MV88E6XXX_G1_CTL1, mask);

out_mapping:
	for (irq = 0; irq < 16; irq++) {
		virq = irq_find_mapping(chip->g1_irq.domain, irq);
		irq_dispose_mapping(virq);
	}

	irq_domain_remove(chip->g1_irq.domain);

	return err;
}

static int mv88e6xxx_g1_irq_setup(struct mv88e6xxx_chip *chip)
{
	static struct lock_class_key lock_key;
	static struct lock_class_key request_key;
	int err;

	err = mv88e6xxx_g1_irq_setup_common(chip);
	if (err)
		return err;

	/* These lock classes tells lockdep that global 1 irqs are in
	 * a different category than their parent GPIO, so it won't
	 * report false recursion.
	 */
	irq_set_lockdep_class(chip->irq, &lock_key, &request_key);

	snprintf(chip->irq_name, sizeof(chip->irq_name),
		 "mv88e6xxx-%s", dev_name(chip->dev));

	mv88e6xxx_reg_unlock(chip);
	err = request_threaded_irq(chip->irq, NULL,
				   mv88e6xxx_g1_irq_thread_fn,
				   IRQF_ONESHOT | IRQF_SHARED,
				   chip->irq_name, chip);
	mv88e6xxx_reg_lock(chip);
	if (err)
		mv88e6xxx_g1_irq_free_common(chip);

	return err;
}

static void mv88e6xxx_irq_poll(struct kthread_work *work)
{
	struct mv88e6xxx_chip *chip = container_of(work,
						   struct mv88e6xxx_chip,
						   irq_poll_work.work);
	mv88e6xxx_g1_irq_thread_work(chip);

	kthread_queue_delayed_work(chip->kworker, &chip->irq_poll_work,
				   msecs_to_jiffies(100));
}

static int mv88e6xxx_irq_poll_setup(struct mv88e6xxx_chip *chip)
{
	int err;

	err = mv88e6xxx_g1_irq_setup_common(chip);
	if (err)
		return err;

	kthread_init_delayed_work(&chip->irq_poll_work,
				  mv88e6xxx_irq_poll);

	chip->kworker = kthread_create_worker(0, "%s", dev_name(chip->dev));
	if (IS_ERR(chip->kworker))
		return PTR_ERR(chip->kworker);

	kthread_queue_delayed_work(chip->kworker, &chip->irq_poll_work,
				   msecs_to_jiffies(100));

	return 0;
}

static void mv88e6xxx_irq_poll_free(struct mv88e6xxx_chip *chip)
{
	kthread_cancel_delayed_work_sync(&chip->irq_poll_work);
	kthread_destroy_worker(chip->kworker);

	mv88e6xxx_reg_lock(chip);
	mv88e6xxx_g1_irq_free_common(chip);
	mv88e6xxx_reg_unlock(chip);
}

static int mv88e6xxx_port_config_interface(struct mv88e6xxx_chip *chip,
					   int port, phy_interface_t interface)
{
	int err;

	if (chip->info->ops->port_set_rgmii_delay) {
		err = chip->info->ops->port_set_rgmii_delay(chip, port,
							    interface);
		if (err && err != -EOPNOTSUPP)
			return err;
	}

	if (chip->info->ops->port_set_cmode) {
		err = chip->info->ops->port_set_cmode(chip, port,
						      interface);
		if (err && err != -EOPNOTSUPP)
			return err;
	}

	return 0;
}

static int mv88e6xxx_port_setup_mac(struct mv88e6xxx_chip *chip, int port,
				    int link, int speed, int duplex, int pause,
				    phy_interface_t mode)
{
	int err;

	if (!chip->info->ops->port_set_link)
		return 0;

	/* Port's MAC control must not be changed unless the link is down */
	err = chip->info->ops->port_set_link(chip, port, LINK_FORCED_DOWN);
	if (err)
		return err;

	if (chip->info->ops->port_set_speed_duplex) {
		err = chip->info->ops->port_set_speed_duplex(chip, port,
							     speed, duplex);
		if (err && err != -EOPNOTSUPP)
			goto restore_link;
	}

	if (chip->info->ops->port_set_pause) {
		err = chip->info->ops->port_set_pause(chip, port, pause);
		if (err)
			goto restore_link;
	}

	err = mv88e6xxx_port_config_interface(chip, port, mode);
restore_link:
	if (chip->info->ops->port_set_link(chip, port, link))
		dev_err(chip->dev, "p%d: failed to restore MAC's link\n", port);

	return err;
}

static int mv88e6xxx_phy_is_internal(struct mv88e6xxx_chip *chip, int port)
{
	return port >= chip->info->internal_phys_offset &&
		port < chip->info->num_internal_phys +
			chip->info->internal_phys_offset;
}

static int mv88e6xxx_port_ppu_updates(struct mv88e6xxx_chip *chip, int port)
{
	u16 reg;
	int err;

	/* The 88e6250 family does not have the PHY detect bit. Instead,
	 * report whether the port is internal.
	 */
	if (chip->info->family == MV88E6XXX_FAMILY_6250)
		return mv88e6xxx_phy_is_internal(chip, port);

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_STS, &reg);
	if (err) {
		dev_err(chip->dev,
			"p%d: %s: failed to read port status\n",
			port, __func__);
		return err;
	}

	return !!(reg & MV88E6XXX_PORT_STS_PHY_DETECT);
}

static int mv88e6xxx_serdes_pcs_get_state(struct dsa_switch *ds, int port,
					  struct phylink_link_state *state)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int lane;
	int err;

	mv88e6xxx_reg_lock(chip);
	lane = mv88e6xxx_serdes_get_lane(chip, port);
	if (lane >= 0 && chip->info->ops->serdes_pcs_get_state)
		err = chip->info->ops->serdes_pcs_get_state(chip, port, lane,
							    state);
	else
		err = -EOPNOTSUPP;
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static int mv88e6xxx_serdes_pcs_config(struct mv88e6xxx_chip *chip, int port,
				       unsigned int mode,
				       phy_interface_t interface,
				       const unsigned long *advertise)
{
	const struct mv88e6xxx_ops *ops = chip->info->ops;
	int lane;

	if (ops->serdes_pcs_config) {
		lane = mv88e6xxx_serdes_get_lane(chip, port);
		if (lane >= 0)
			return ops->serdes_pcs_config(chip, port, lane, mode,
						      interface, advertise);
	}

	return 0;
}

static void mv88e6xxx_serdes_pcs_an_restart(struct dsa_switch *ds, int port)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	const struct mv88e6xxx_ops *ops;
	int err = 0;
	int lane;

	ops = chip->info->ops;

	if (ops->serdes_pcs_an_restart) {
		mv88e6xxx_reg_lock(chip);
		lane = mv88e6xxx_serdes_get_lane(chip, port);
		if (lane >= 0)
			err = ops->serdes_pcs_an_restart(chip, port, lane);
		mv88e6xxx_reg_unlock(chip);

		if (err)
			dev_err(ds->dev, "p%d: failed to restart AN\n", port);
	}
}

static int mv88e6xxx_serdes_pcs_link_up(struct mv88e6xxx_chip *chip, int port,
					unsigned int mode,
					int speed, int duplex)
{
	const struct mv88e6xxx_ops *ops = chip->info->ops;
	int lane;

	if (!phylink_autoneg_inband(mode) && ops->serdes_pcs_link_up) {
		lane = mv88e6xxx_serdes_get_lane(chip, port);
		if (lane >= 0)
			return ops->serdes_pcs_link_up(chip, port, lane,
						       speed, duplex);
	}

	return 0;
}

static const u8 mv88e6185_phy_interface_modes[] = {
	[MV88E6185_PORT_STS_CMODE_GMII_FD]	 = PHY_INTERFACE_MODE_GMII,
	[MV88E6185_PORT_STS_CMODE_MII_100_FD_PS] = PHY_INTERFACE_MODE_MII,
	[MV88E6185_PORT_STS_CMODE_MII_100]	 = PHY_INTERFACE_MODE_MII,
	[MV88E6185_PORT_STS_CMODE_MII_10]	 = PHY_INTERFACE_MODE_MII,
	[MV88E6185_PORT_STS_CMODE_SERDES]	 = PHY_INTERFACE_MODE_1000BASEX,
	[MV88E6185_PORT_STS_CMODE_1000BASE_X]	 = PHY_INTERFACE_MODE_1000BASEX,
	[MV88E6185_PORT_STS_CMODE_PHY]		 = PHY_INTERFACE_MODE_SGMII,
};

static void mv88e6095_phylink_get_caps(struct mv88e6xxx_chip *chip, int port,
				       struct phylink_config *config)
{
	u8 cmode = chip->ports[port].cmode;

	config->mac_capabilities = MAC_SYM_PAUSE | MAC_10 | MAC_100;

	if (mv88e6xxx_phy_is_internal(chip, port)) {
		__set_bit(PHY_INTERFACE_MODE_MII, config->supported_interfaces);
	} else {
		if (cmode < ARRAY_SIZE(mv88e6185_phy_interface_modes) &&
		    mv88e6185_phy_interface_modes[cmode])
			__set_bit(mv88e6185_phy_interface_modes[cmode],
				  config->supported_interfaces);

		config->mac_capabilities |= MAC_1000FD;
	}
}

static void mv88e6185_phylink_get_caps(struct mv88e6xxx_chip *chip, int port,
				       struct phylink_config *config)
{
	u8 cmode = chip->ports[port].cmode;

	if (cmode < ARRAY_SIZE(mv88e6185_phy_interface_modes) &&
	    mv88e6185_phy_interface_modes[cmode])
		__set_bit(mv88e6185_phy_interface_modes[cmode],
			  config->supported_interfaces);

	config->mac_capabilities = MAC_SYM_PAUSE | MAC_10 | MAC_100 |
				   MAC_1000FD;
}

static const u8 mv88e6xxx_phy_interface_modes[] = {
	[MV88E6XXX_PORT_STS_CMODE_MII_PHY]	= PHY_INTERFACE_MODE_REVMII,
	[MV88E6XXX_PORT_STS_CMODE_MII]		= PHY_INTERFACE_MODE_MII,
	[MV88E6XXX_PORT_STS_CMODE_GMII]		= PHY_INTERFACE_MODE_GMII,
	[MV88E6XXX_PORT_STS_CMODE_RMII_PHY]	= PHY_INTERFACE_MODE_REVRMII,
	[MV88E6XXX_PORT_STS_CMODE_RMII]		= PHY_INTERFACE_MODE_RMII,
	[MV88E6XXX_PORT_STS_CMODE_100BASEX]	= PHY_INTERFACE_MODE_100BASEX,
	[MV88E6XXX_PORT_STS_CMODE_1000BASEX]	= PHY_INTERFACE_MODE_1000BASEX,
	[MV88E6XXX_PORT_STS_CMODE_SGMII]	= PHY_INTERFACE_MODE_SGMII,
	/* higher interface modes are not needed here, since ports supporting
	 * them are writable, and so the supported interfaces are filled in the
	 * corresponding .phylink_set_interfaces() implementation below
	 */
};

static void mv88e6xxx_translate_cmode(u8 cmode, unsigned long *supported)
{
	if (cmode < ARRAY_SIZE(mv88e6xxx_phy_interface_modes) &&
	    mv88e6xxx_phy_interface_modes[cmode])
		__set_bit(mv88e6xxx_phy_interface_modes[cmode], supported);
	else if (cmode == MV88E6XXX_PORT_STS_CMODE_RGMII)
		phy_interface_set_rgmii(supported);
}

static void mv88e6250_phylink_get_caps(struct mv88e6xxx_chip *chip, int port,
				       struct phylink_config *config)
{
	unsigned long *supported = config->supported_interfaces;

	/* Translate the default cmode */
	mv88e6xxx_translate_cmode(chip->ports[port].cmode, supported);

	config->mac_capabilities = MAC_SYM_PAUSE | MAC_10 | MAC_100;
}

static int mv88e6352_get_port4_serdes_cmode(struct mv88e6xxx_chip *chip)
{
	u16 reg, val;
	int err;

	err = mv88e6xxx_port_read(chip, 4, MV88E6XXX_PORT_STS, &reg);
	if (err)
		return err;

	/* If PHY_DETECT is zero, then we are not in auto-media mode */
	if (!(reg & MV88E6XXX_PORT_STS_PHY_DETECT))
		return 0xf;

	val = reg & ~MV88E6XXX_PORT_STS_PHY_DETECT;
	err = mv88e6xxx_port_write(chip, 4, MV88E6XXX_PORT_STS, val);
	if (err)
		return err;

	err = mv88e6xxx_port_read(chip, 4, MV88E6XXX_PORT_STS, &val);
	if (err)
		return err;

	/* Restore PHY_DETECT value */
	err = mv88e6xxx_port_write(chip, 4, MV88E6XXX_PORT_STS, reg);
	if (err)
		return err;

	return val & MV88E6XXX_PORT_STS_CMODE_MASK;
}

static void mv88e6352_phylink_get_caps(struct mv88e6xxx_chip *chip, int port,
				       struct phylink_config *config)
{
	unsigned long *supported = config->supported_interfaces;
	int err, cmode;

	/* Translate the default cmode */
	mv88e6xxx_translate_cmode(chip->ports[port].cmode, supported);

	config->mac_capabilities = MAC_SYM_PAUSE | MAC_10 | MAC_100 |
				   MAC_1000FD;

	/* Port 4 supports automedia if the serdes is associated with it. */
	if (port == 4) {
		err = mv88e6352_g2_scratch_port_has_serdes(chip, port);
		if (err < 0)
			dev_err(chip->dev, "p%d: failed to read scratch\n",
				port);
		if (err <= 0)
			return;

		cmode = mv88e6352_get_port4_serdes_cmode(chip);
		if (cmode < 0)
			dev_err(chip->dev, "p%d: failed to read serdes cmode\n",
				port);
		else
			mv88e6xxx_translate_cmode(cmode, supported);
	}
}

static void mv88e6341_phylink_get_caps(struct mv88e6xxx_chip *chip, int port,
				       struct phylink_config *config)
{
	unsigned long *supported = config->supported_interfaces;

	/* Translate the default cmode */
	mv88e6xxx_translate_cmode(chip->ports[port].cmode, supported);

	/* No ethtool bits for 200Mbps */
	config->mac_capabilities = MAC_SYM_PAUSE | MAC_10 | MAC_100 |
				   MAC_1000FD;

	/* The C_Mode field is programmable on port 5 */
	if (port == 5) {
		__set_bit(PHY_INTERFACE_MODE_SGMII, supported);
		__set_bit(PHY_INTERFACE_MODE_1000BASEX, supported);
		__set_bit(PHY_INTERFACE_MODE_2500BASEX, supported);

		config->mac_capabilities |= MAC_2500FD;
	}
}

static void mv88e6390_phylink_get_caps(struct mv88e6xxx_chip *chip, int port,
				       struct phylink_config *config)
{
	unsigned long *supported = config->supported_interfaces;

	/* Translate the default cmode */
	mv88e6xxx_translate_cmode(chip->ports[port].cmode, supported);

	/* No ethtool bits for 200Mbps */
	config->mac_capabilities = MAC_SYM_PAUSE | MAC_10 | MAC_100 |
				   MAC_1000FD;

	/* The C_Mode field is programmable on ports 9 and 10 */
	if (port == 9 || port == 10) {
		__set_bit(PHY_INTERFACE_MODE_SGMII, supported);
		__set_bit(PHY_INTERFACE_MODE_1000BASEX, supported);
		__set_bit(PHY_INTERFACE_MODE_2500BASEX, supported);

		config->mac_capabilities |= MAC_2500FD;
	}
}

static void mv88e6390x_phylink_get_caps(struct mv88e6xxx_chip *chip, int port,
					struct phylink_config *config)
{
	unsigned long *supported = config->supported_interfaces;

	mv88e6390_phylink_get_caps(chip, port, config);

	/* For the 6x90X, ports 2-7 can be in automedia mode.
	 * (Note that 6x90 doesn't support RXAUI nor XAUI).
	 *
	 * Port 2 can also support 1000BASE-X in automedia mode if port 9 is
	 * configured for 1000BASE-X, SGMII or 2500BASE-X.
	 * Port 3-4 can also support 1000BASE-X in automedia mode if port 9 is
	 * configured for RXAUI, 1000BASE-X, SGMII or 2500BASE-X.
	 *
	 * Port 5 can also support 1000BASE-X in automedia mode if port 10 is
	 * configured for 1000BASE-X, SGMII or 2500BASE-X.
	 * Port 6-7 can also support 1000BASE-X in automedia mode if port 10 is
	 * configured for RXAUI, 1000BASE-X, SGMII or 2500BASE-X.
	 *
	 * For now, be permissive (as the old code was) and allow 1000BASE-X
	 * on ports 2..7.
	 */
	if (port >= 2 && port <= 7)
		__set_bit(PHY_INTERFACE_MODE_1000BASEX, supported);

	/* The C_Mode field can also be programmed for 10G speeds */
	if (port == 9 || port == 10) {
		__set_bit(PHY_INTERFACE_MODE_XAUI, supported);
		__set_bit(PHY_INTERFACE_MODE_RXAUI, supported);

		config->mac_capabilities |= MAC_10000FD;
	}
}

static void mv88e6393x_phylink_get_caps(struct mv88e6xxx_chip *chip, int port,
					struct phylink_config *config)
{
	unsigned long *supported = config->supported_interfaces;
	bool is_6191x =
		chip->info->prod_num == MV88E6XXX_PORT_SWITCH_ID_PROD_6191X;
	bool is_6361 =
		chip->info->prod_num == MV88E6XXX_PORT_SWITCH_ID_PROD_6361;

	mv88e6xxx_translate_cmode(chip->ports[port].cmode, supported);

	config->mac_capabilities = MAC_SYM_PAUSE | MAC_10 | MAC_100 |
				   MAC_1000FD;

	/* The C_Mode field can be programmed for ports 0, 9 and 10 */
	if (port == 0 || port == 9 || port == 10) {
		__set_bit(PHY_INTERFACE_MODE_SGMII, supported);
		__set_bit(PHY_INTERFACE_MODE_1000BASEX, supported);

		/* 6191X supports >1G modes only on port 10 */
		if (!is_6191x || port == 10) {
			__set_bit(PHY_INTERFACE_MODE_2500BASEX, supported);
			config->mac_capabilities |= MAC_2500FD;

			/* 6361 only supports up to 2500BaseX */
			if (!is_6361) {
				__set_bit(PHY_INTERFACE_MODE_5GBASER, supported);
				__set_bit(PHY_INTERFACE_MODE_10GBASER, supported);
				__set_bit(PHY_INTERFACE_MODE_USXGMII, supported);
				config->mac_capabilities |= MAC_5000FD |
					MAC_10000FD;
			}
		}
	}

	if (port == 0) {
		__set_bit(PHY_INTERFACE_MODE_RMII, supported);
		__set_bit(PHY_INTERFACE_MODE_RGMII, supported);
		__set_bit(PHY_INTERFACE_MODE_RGMII_ID, supported);
		__set_bit(PHY_INTERFACE_MODE_RGMII_RXID, supported);
		__set_bit(PHY_INTERFACE_MODE_RGMII_TXID, supported);
	}
}

static void mv88e6xxx_get_caps(struct dsa_switch *ds, int port,
			       struct phylink_config *config)
{
	struct mv88e6xxx_chip *chip = ds->priv;

	mv88e6xxx_reg_lock(chip);
	chip->info->ops->phylink_get_caps(chip, port, config);
	mv88e6xxx_reg_unlock(chip);

	if (mv88e6xxx_phy_is_internal(chip, port)) {
		__set_bit(PHY_INTERFACE_MODE_INTERNAL,
			  config->supported_interfaces);
		/* Internal ports with no phy-mode need GMII for PHYLIB */
		__set_bit(PHY_INTERFACE_MODE_GMII,
			  config->supported_interfaces);
	}
}

static int mv88e6xxx_mac_prepare(struct dsa_switch *ds, int port,
				 unsigned int mode, phy_interface_t interface)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err = 0;

	/* In inband mode, the link may come up at any time while the link
	 * is not forced down. Force the link down while we reconfigure the
	 * interface mode.
	 */
	if (mode == MLO_AN_INBAND &&
	    chip->ports[port].interface != interface &&
	    chip->info->ops->port_set_link) {
		mv88e6xxx_reg_lock(chip);
		err = chip->info->ops->port_set_link(chip, port,
						     LINK_FORCED_DOWN);
		mv88e6xxx_reg_unlock(chip);
	}

	return err;
}

static void mv88e6xxx_mac_config(struct dsa_switch *ds, int port,
				 unsigned int mode,
				 const struct phylink_link_state *state)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err = 0;

	mv88e6xxx_reg_lock(chip);

	if (mode != MLO_AN_PHY || !mv88e6xxx_phy_is_internal(chip, port)) {
		err = mv88e6xxx_port_config_interface(chip, port,
						      state->interface);
		if (err && err != -EOPNOTSUPP)
			goto err_unlock;

		err = mv88e6xxx_serdes_pcs_config(chip, port, mode,
						  state->interface,
						  state->advertising);
		/* FIXME: we should restart negotiation if something changed -
		 * which is something we get if we convert to using phylinks
		 * PCS operations.
		 */
		if (err > 0)
			err = 0;
	}

err_unlock:
	mv88e6xxx_reg_unlock(chip);

	if (err && err != -EOPNOTSUPP)
		dev_err(ds->dev, "p%d: failed to configure MAC/PCS\n", port);
}

static int mv88e6xxx_mac_finish(struct dsa_switch *ds, int port,
				unsigned int mode, phy_interface_t interface)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err = 0;

	/* Undo the forced down state above after completing configuration
	 * irrespective of its state on entry, which allows the link to come
	 * up in the in-band case where there is no separate SERDES. Also
	 * ensure that the link can come up if the PPU is in use and we are
	 * in PHY mode (we treat the PPU as an effective in-band mechanism.)
	 */
	mv88e6xxx_reg_lock(chip);

	if (chip->info->ops->port_set_link &&
	    ((mode == MLO_AN_INBAND &&
	      chip->ports[port].interface != interface) ||
	     (mode == MLO_AN_PHY && mv88e6xxx_port_ppu_updates(chip, port))))
		err = chip->info->ops->port_set_link(chip, port, LINK_UNFORCED);

	mv88e6xxx_reg_unlock(chip);

	chip->ports[port].interface = interface;

	return err;
}

static void mv88e6xxx_mac_link_down(struct dsa_switch *ds, int port,
				    unsigned int mode,
				    phy_interface_t interface)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	const struct mv88e6xxx_ops *ops;
	int err = 0;

	ops = chip->info->ops;

	mv88e6xxx_reg_lock(chip);
	/* Force the link down if we know the port may not be automatically
	 * updated by the switch or if we are using fixed-link mode.
	 */
	if ((!mv88e6xxx_port_ppu_updates(chip, port) ||
	     mode == MLO_AN_FIXED) && ops->port_sync_link)
		err = ops->port_sync_link(chip, port, mode, false);

	if (!err && ops->port_set_speed_duplex)
		err = ops->port_set_speed_duplex(chip, port, SPEED_UNFORCED,
						 DUPLEX_UNFORCED);
	mv88e6xxx_reg_unlock(chip);

	if (err)
		dev_err(chip->dev,
			"p%d: failed to force MAC link down\n", port);
}

static void mv88e6xxx_mac_link_up(struct dsa_switch *ds, int port,
				  unsigned int mode, phy_interface_t interface,
				  struct phy_device *phydev,
				  int speed, int duplex,
				  bool tx_pause, bool rx_pause)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	const struct mv88e6xxx_ops *ops;
	int err = 0;

	ops = chip->info->ops;

	mv88e6xxx_reg_lock(chip);
	/* Configure and force the link up if we know that the port may not
	 * automatically updated by the switch or if we are using fixed-link
	 * mode.
	 */
	if (!mv88e6xxx_port_ppu_updates(chip, port) ||
	    mode == MLO_AN_FIXED) {
		/* FIXME: for an automedia port, should we force the link
		 * down here - what if the link comes up due to "other" media
		 * while we're bringing the port up, how is the exclusivity
		 * handled in the Marvell hardware? E.g. port 2 on 88E6390
		 * shared between internal PHY and Serdes.
		 */
		err = mv88e6xxx_serdes_pcs_link_up(chip, port, mode, speed,
						   duplex);
		if (err)
			goto error;

		if (ops->port_set_speed_duplex) {
			err = ops->port_set_speed_duplex(chip, port,
							 speed, duplex);
			if (err && err != -EOPNOTSUPP)
				goto error;
		}

		if (ops->port_sync_link)
			err = ops->port_sync_link(chip, port, mode, true);
	}
error:
	mv88e6xxx_reg_unlock(chip);

	if (err && err != -EOPNOTSUPP)
		dev_err(ds->dev,
			"p%d: failed to configure MAC link up\n", port);
}

static int mv88e6xxx_stats_snapshot(struct mv88e6xxx_chip *chip, int port)
{
	if (!chip->info->ops->stats_snapshot)
		return -EOPNOTSUPP;

	return chip->info->ops->stats_snapshot(chip, port);
}

static struct mv88e6xxx_hw_stat mv88e6xxx_hw_stats[] = {
	{ "in_good_octets",		8, 0x00, STATS_TYPE_BANK0, },
	{ "in_bad_octets",		4, 0x02, STATS_TYPE_BANK0, },
	{ "in_unicast",			4, 0x04, STATS_TYPE_BANK0, },
	{ "in_broadcasts",		4, 0x06, STATS_TYPE_BANK0, },
	{ "in_multicasts",		4, 0x07, STATS_TYPE_BANK0, },
	{ "in_pause",			4, 0x16, STATS_TYPE_BANK0, },
	{ "in_undersize",		4, 0x18, STATS_TYPE_BANK0, },
	{ "in_fragments",		4, 0x19, STATS_TYPE_BANK0, },
	{ "in_oversize",		4, 0x1a, STATS_TYPE_BANK0, },
	{ "in_jabber",			4, 0x1b, STATS_TYPE_BANK0, },
	{ "in_rx_error",		4, 0x1c, STATS_TYPE_BANK0, },
	{ "in_fcs_error",		4, 0x1d, STATS_TYPE_BANK0, },
	{ "out_octets",			8, 0x0e, STATS_TYPE_BANK0, },
	{ "out_unicast",		4, 0x10, STATS_TYPE_BANK0, },
	{ "out_broadcasts",		4, 0x13, STATS_TYPE_BANK0, },
	{ "out_multicasts",		4, 0x12, STATS_TYPE_BANK0, },
	{ "out_pause",			4, 0x15, STATS_TYPE_BANK0, },
	{ "excessive",			4, 0x11, STATS_TYPE_BANK0, },
	{ "collisions",			4, 0x1e, STATS_TYPE_BANK0, },
	{ "deferred",			4, 0x05, STATS_TYPE_BANK0, },
	{ "single",			4, 0x14, STATS_TYPE_BANK0, },
	{ "multiple",			4, 0x17, STATS_TYPE_BANK0, },
	{ "out_fcs_error",		4, 0x03, STATS_TYPE_BANK0, },
	{ "late",			4, 0x1f, STATS_TYPE_BANK0, },
	{ "hist_64bytes",		4, 0x08, STATS_TYPE_BANK0, },
	{ "hist_65_127bytes",		4, 0x09, STATS_TYPE_BANK0, },
	{ "hist_128_255bytes",		4, 0x0a, STATS_TYPE_BANK0, },
	{ "hist_256_511bytes",		4, 0x0b, STATS_TYPE_BANK0, },
	{ "hist_512_1023bytes",		4, 0x0c, STATS_TYPE_BANK0, },
	{ "hist_1024_max_bytes",	4, 0x0d, STATS_TYPE_BANK0, },
	{ "sw_in_discards",		4, 0x10, STATS_TYPE_PORT, },
	{ "sw_in_filtered",		2, 0x12, STATS_TYPE_PORT, },
	{ "sw_out_filtered",		2, 0x13, STATS_TYPE_PORT, },
	{ "in_discards",		4, 0x00, STATS_TYPE_BANK1, },
	{ "in_filtered",		4, 0x01, STATS_TYPE_BANK1, },
	{ "in_accepted",		4, 0x02, STATS_TYPE_BANK1, },
	{ "in_bad_accepted",		4, 0x03, STATS_TYPE_BANK1, },
	{ "in_good_avb_class_a",	4, 0x04, STATS_TYPE_BANK1, },
	{ "in_good_avb_class_b",	4, 0x05, STATS_TYPE_BANK1, },
	{ "in_bad_avb_class_a",		4, 0x06, STATS_TYPE_BANK1, },
	{ "in_bad_avb_class_b",		4, 0x07, STATS_TYPE_BANK1, },
	{ "tcam_counter_0",		4, 0x08, STATS_TYPE_BANK1, },
	{ "tcam_counter_1",		4, 0x09, STATS_TYPE_BANK1, },
	{ "tcam_counter_2",		4, 0x0a, STATS_TYPE_BANK1, },
	{ "tcam_counter_3",		4, 0x0b, STATS_TYPE_BANK1, },
	{ "in_da_unknown",		4, 0x0e, STATS_TYPE_BANK1, },
	{ "in_management",		4, 0x0f, STATS_TYPE_BANK1, },
	{ "out_queue_0",		4, 0x10, STATS_TYPE_BANK1, },
	{ "out_queue_1",		4, 0x11, STATS_TYPE_BANK1, },
	{ "out_queue_2",		4, 0x12, STATS_TYPE_BANK1, },
	{ "out_queue_3",		4, 0x13, STATS_TYPE_BANK1, },
	{ "out_queue_4",		4, 0x14, STATS_TYPE_BANK1, },
	{ "out_queue_5",		4, 0x15, STATS_TYPE_BANK1, },
	{ "out_queue_6",		4, 0x16, STATS_TYPE_BANK1, },
	{ "out_queue_7",		4, 0x17, STATS_TYPE_BANK1, },
	{ "out_cut_through",		4, 0x18, STATS_TYPE_BANK1, },
	{ "out_octets_a",		4, 0x1a, STATS_TYPE_BANK1, },
	{ "out_octets_b",		4, 0x1b, STATS_TYPE_BANK1, },
	{ "out_management",		4, 0x1f, STATS_TYPE_BANK1, },
};

static uint64_t _mv88e6xxx_get_ethtool_stat(struct mv88e6xxx_chip *chip,
					    struct mv88e6xxx_hw_stat *s,
					    int port, u16 bank1_select,
					    u16 histogram)
{
	u32 low;
	u32 high = 0;
	u16 reg = 0;
	int err;
	u64 value;

	switch (s->type) {
	case STATS_TYPE_PORT:
		err = mv88e6xxx_port_read(chip, port, s->reg, &reg);
		if (err)
			return U64_MAX;

		low = reg;
		if (s->size == 4) {
			err = mv88e6xxx_port_read(chip, port, s->reg + 1, &reg);
			if (err)
				return U64_MAX;
			low |= ((u32)reg) << 16;
		}
		break;
	case STATS_TYPE_BANK1:
		reg = bank1_select;
		fallthrough;
	case STATS_TYPE_BANK0:
		reg |= s->reg | histogram;
		mv88e6xxx_g1_stats_read(chip, reg, &low);
		if (s->size == 8)
			mv88e6xxx_g1_stats_read(chip, reg + 1, &high);
		break;
	default:
		return U64_MAX;
	}
	value = (((u64)high) << 32) | low;
	return value;
}

static int mv88e6xxx_stats_get_strings(struct mv88e6xxx_chip *chip,
				       uint8_t *data, int types)
{
	struct mv88e6xxx_hw_stat *stat;
	int i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(mv88e6xxx_hw_stats); i++) {
		stat = &mv88e6xxx_hw_stats[i];
		if (stat->type & types) {
			memcpy(data + j * ETH_GSTRING_LEN, stat->string,
			       ETH_GSTRING_LEN);
			j++;
		}
	}

	return j;
}

static int mv88e6095_stats_get_strings(struct mv88e6xxx_chip *chip,
				       uint8_t *data)
{
	return mv88e6xxx_stats_get_strings(chip, data,
					   STATS_TYPE_BANK0 | STATS_TYPE_PORT);
}

static int mv88e6250_stats_get_strings(struct mv88e6xxx_chip *chip,
				       uint8_t *data)
{
	return mv88e6xxx_stats_get_strings(chip, data, STATS_TYPE_BANK0);
}

static int mv88e6320_stats_get_strings(struct mv88e6xxx_chip *chip,
				       uint8_t *data)
{
	return mv88e6xxx_stats_get_strings(chip, data,
					   STATS_TYPE_BANK0 | STATS_TYPE_BANK1);
}

static const uint8_t *mv88e6xxx_atu_vtu_stats_strings[] = {
	"atu_member_violation",
	"atu_miss_violation",
	"atu_full_violation",
	"vtu_member_violation",
	"vtu_miss_violation",
};

static void mv88e6xxx_atu_vtu_get_strings(uint8_t *data)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mv88e6xxx_atu_vtu_stats_strings); i++)
		strscpy(data + i * ETH_GSTRING_LEN,
			mv88e6xxx_atu_vtu_stats_strings[i],
			ETH_GSTRING_LEN);
}

static void mv88e6xxx_get_strings(struct dsa_switch *ds, int port,
				  u32 stringset, uint8_t *data)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int count = 0;

	if (stringset != ETH_SS_STATS)
		return;

	mv88e6xxx_reg_lock(chip);

	if (chip->info->ops->stats_get_strings)
		count = chip->info->ops->stats_get_strings(chip, data);

	if (chip->info->ops->serdes_get_strings) {
		data += count * ETH_GSTRING_LEN;
		count = chip->info->ops->serdes_get_strings(chip, port, data);
	}

	data += count * ETH_GSTRING_LEN;
	mv88e6xxx_atu_vtu_get_strings(data);

	mv88e6xxx_reg_unlock(chip);
}

static int mv88e6xxx_stats_get_sset_count(struct mv88e6xxx_chip *chip,
					  int types)
{
	struct mv88e6xxx_hw_stat *stat;
	int i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(mv88e6xxx_hw_stats); i++) {
		stat = &mv88e6xxx_hw_stats[i];
		if (stat->type & types)
			j++;
	}
	return j;
}

static int mv88e6095_stats_get_sset_count(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_stats_get_sset_count(chip, STATS_TYPE_BANK0 |
					      STATS_TYPE_PORT);
}

static int mv88e6250_stats_get_sset_count(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_stats_get_sset_count(chip, STATS_TYPE_BANK0);
}

static int mv88e6320_stats_get_sset_count(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_stats_get_sset_count(chip, STATS_TYPE_BANK0 |
					      STATS_TYPE_BANK1);
}

static int mv88e6xxx_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int serdes_count = 0;
	int count = 0;

	if (sset != ETH_SS_STATS)
		return 0;

	mv88e6xxx_reg_lock(chip);
	if (chip->info->ops->stats_get_sset_count)
		count = chip->info->ops->stats_get_sset_count(chip);
	if (count < 0)
		goto out;

	if (chip->info->ops->serdes_get_sset_count)
		serdes_count = chip->info->ops->serdes_get_sset_count(chip,
								      port);
	if (serdes_count < 0) {
		count = serdes_count;
		goto out;
	}
	count += serdes_count;
	count += ARRAY_SIZE(mv88e6xxx_atu_vtu_stats_strings);

out:
	mv88e6xxx_reg_unlock(chip);

	return count;
}

static int mv88e6xxx_stats_get_stats(struct mv88e6xxx_chip *chip, int port,
				     uint64_t *data, int types,
				     u16 bank1_select, u16 histogram)
{
	struct mv88e6xxx_hw_stat *stat;
	int i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(mv88e6xxx_hw_stats); i++) {
		stat = &mv88e6xxx_hw_stats[i];
		if (stat->type & types) {
			mv88e6xxx_reg_lock(chip);
			data[j] = _mv88e6xxx_get_ethtool_stat(chip, stat, port,
							      bank1_select,
							      histogram);
			mv88e6xxx_reg_unlock(chip);

			j++;
		}
	}
	return j;
}

static int mv88e6095_stats_get_stats(struct mv88e6xxx_chip *chip, int port,
				     uint64_t *data)
{
	return mv88e6xxx_stats_get_stats(chip, port, data,
					 STATS_TYPE_BANK0 | STATS_TYPE_PORT,
					 0, MV88E6XXX_G1_STATS_OP_HIST_RX_TX);
}

static int mv88e6250_stats_get_stats(struct mv88e6xxx_chip *chip, int port,
				     uint64_t *data)
{
	return mv88e6xxx_stats_get_stats(chip, port, data, STATS_TYPE_BANK0,
					 0, MV88E6XXX_G1_STATS_OP_HIST_RX_TX);
}

static int mv88e6320_stats_get_stats(struct mv88e6xxx_chip *chip, int port,
				     uint64_t *data)
{
	return mv88e6xxx_stats_get_stats(chip, port, data,
					 STATS_TYPE_BANK0 | STATS_TYPE_BANK1,
					 MV88E6XXX_G1_STATS_OP_BANK_1_BIT_9,
					 MV88E6XXX_G1_STATS_OP_HIST_RX_TX);
}

static int mv88e6390_stats_get_stats(struct mv88e6xxx_chip *chip, int port,
				     uint64_t *data)
{
	return mv88e6xxx_stats_get_stats(chip, port, data,
					 STATS_TYPE_BANK0 | STATS_TYPE_BANK1,
					 MV88E6XXX_G1_STATS_OP_BANK_1_BIT_10,
					 0);
}

static void mv88e6xxx_atu_vtu_get_stats(struct mv88e6xxx_chip *chip, int port,
					uint64_t *data)
{
	*data++ = chip->ports[port].atu_member_violation;
	*data++ = chip->ports[port].atu_miss_violation;
	*data++ = chip->ports[port].atu_full_violation;
	*data++ = chip->ports[port].vtu_member_violation;
	*data++ = chip->ports[port].vtu_miss_violation;
}

static void mv88e6xxx_get_stats(struct mv88e6xxx_chip *chip, int port,
				uint64_t *data)
{
	int count = 0;

	if (chip->info->ops->stats_get_stats)
		count = chip->info->ops->stats_get_stats(chip, port, data);

	mv88e6xxx_reg_lock(chip);
	if (chip->info->ops->serdes_get_stats) {
		data += count;
		count = chip->info->ops->serdes_get_stats(chip, port, data);
	}
	data += count;
	mv88e6xxx_atu_vtu_get_stats(chip, port, data);
	mv88e6xxx_reg_unlock(chip);
}

static void mv88e6xxx_get_ethtool_stats(struct dsa_switch *ds, int port,
					uint64_t *data)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int ret;

	mv88e6xxx_reg_lock(chip);

	ret = mv88e6xxx_stats_snapshot(chip, port);
	mv88e6xxx_reg_unlock(chip);

	if (ret < 0)
		return;

	mv88e6xxx_get_stats(chip, port, data);

}

static int mv88e6xxx_get_regs_len(struct dsa_switch *ds, int port)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int len;

	len = 32 * sizeof(u16);
	if (chip->info->ops->serdes_get_regs_len)
		len += chip->info->ops->serdes_get_regs_len(chip, port);

	return len;
}

static void mv88e6xxx_get_regs(struct dsa_switch *ds, int port,
			       struct ethtool_regs *regs, void *_p)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;
	u16 reg;
	u16 *p = _p;
	int i;

	regs->version = chip->info->prod_num;

	memset(p, 0xff, 32 * sizeof(u16));

	mv88e6xxx_reg_lock(chip);

	for (i = 0; i < 32; i++) {

		err = mv88e6xxx_port_read(chip, port, i, &reg);
		if (!err)
			p[i] = reg;
	}

	if (chip->info->ops->serdes_get_regs)
		chip->info->ops->serdes_get_regs(chip, port, &p[i]);

	mv88e6xxx_reg_unlock(chip);
}

static int mv88e6xxx_get_mac_eee(struct dsa_switch *ds, int port,
				 struct ethtool_eee *e)
{
	/* Nothing to do on the port's MAC */
	return 0;
}

static int mv88e6xxx_set_mac_eee(struct dsa_switch *ds, int port,
				 struct ethtool_eee *e)
{
	/* Nothing to do on the port's MAC */
	return 0;
}

/* Mask of the local ports allowed to receive frames from a given fabric port */
static u16 mv88e6xxx_port_vlan(struct mv88e6xxx_chip *chip, int dev, int port)
{
	struct dsa_switch *ds = chip->ds;
	struct dsa_switch_tree *dst = ds->dst;
	struct dsa_port *dp, *other_dp;
	bool found = false;
	u16 pvlan;

	/* dev is a physical switch */
	if (dev <= dst->last_switch) {
		list_for_each_entry(dp, &dst->ports, list) {
			if (dp->ds->index == dev && dp->index == port) {
				/* dp might be a DSA link or a user port, so it
				 * might or might not have a bridge.
				 * Use the "found" variable for both cases.
				 */
				found = true;
				break;
			}
		}
	/* dev is a virtual bridge */
	} else {
		list_for_each_entry(dp, &dst->ports, list) {
			unsigned int bridge_num = dsa_port_bridge_num_get(dp);

			if (!bridge_num)
				continue;

			if (bridge_num + dst->last_switch != dev)
				continue;

			found = true;
			break;
		}
	}

	/* Prevent frames from unknown switch or virtual bridge */
	if (!found)
		return 0;

	/* Frames from DSA links and CPU ports can egress any local port */
	if (dp->type == DSA_PORT_TYPE_CPU || dp->type == DSA_PORT_TYPE_DSA)
		return mv88e6xxx_port_mask(chip);

	pvlan = 0;

	/* Frames from standalone user ports can only egress on the
	 * upstream port.
	 */
	if (!dsa_port_bridge_dev_get(dp))
		return BIT(dsa_switch_upstream_port(ds));

	/* Frames from bridged user ports can egress any local DSA
	 * links and CPU ports, as well as any local member of their
	 * bridge group.
	 */
	dsa_switch_for_each_port(other_dp, ds)
		if (other_dp->type == DSA_PORT_TYPE_CPU ||
		    other_dp->type == DSA_PORT_TYPE_DSA ||
		    dsa_port_bridge_same(dp, other_dp))
			pvlan |= BIT(other_dp->index);

	return pvlan;
}

static int mv88e6xxx_port_vlan_map(struct mv88e6xxx_chip *chip, int port)
{
	u16 output_ports = mv88e6xxx_port_vlan(chip, chip->ds->index, port);

	/* prevent frames from going back out of the port they came in on */
	output_ports &= ~BIT(port);

	return mv88e6xxx_port_set_vlan_map(chip, port, output_ports);
}

static void mv88e6xxx_port_stp_state_set(struct dsa_switch *ds, int port,
					 u8 state)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_port_set_state(chip, port, state);
	mv88e6xxx_reg_unlock(chip);

	if (err)
		dev_err(ds->dev, "p%d: failed to update state\n", port);
}

static int mv88e6xxx_pri_setup(struct mv88e6xxx_chip *chip)
{
	int err;

	if (chip->info->ops->ieee_pri_map) {
		err = chip->info->ops->ieee_pri_map(chip);
		if (err)
			return err;
	}

	if (chip->info->ops->ip_pri_map) {
		err = chip->info->ops->ip_pri_map(chip);
		if (err)
			return err;
	}

	return 0;
}

static int mv88e6xxx_devmap_setup(struct mv88e6xxx_chip *chip)
{
	struct dsa_switch *ds = chip->ds;
	int target, port;
	int err;

	if (!chip->info->global2_addr)
		return 0;

	/* Initialize the routing port to the 32 possible target devices */
	for (target = 0; target < 32; target++) {
		port = dsa_routing_port(ds, target);
		if (port == ds->num_ports)
			port = 0x1f;

		err = mv88e6xxx_g2_device_mapping_write(chip, target, port);
		if (err)
			return err;
	}

	if (chip->info->ops->set_cascade_port) {
		port = MV88E6XXX_CASCADE_PORT_MULTIPLE;
		err = chip->info->ops->set_cascade_port(chip, port);
		if (err)
			return err;
	}

	err = mv88e6xxx_g1_set_device_number(chip, chip->ds->index);
	if (err)
		return err;

	return 0;
}

static int mv88e6xxx_trunk_setup(struct mv88e6xxx_chip *chip)
{
	/* Clear all trunk masks and mapping */
	if (chip->info->global2_addr)
		return mv88e6xxx_g2_trunk_clear(chip);

	return 0;
}

static int mv88e6xxx_rmu_setup(struct mv88e6xxx_chip *chip)
{
	if (chip->info->ops->rmu_disable)
		return chip->info->ops->rmu_disable(chip);

	return 0;
}

static int mv88e6xxx_pot_setup(struct mv88e6xxx_chip *chip)
{
	if (chip->info->ops->pot_clear)
		return chip->info->ops->pot_clear(chip);

	return 0;
}

static int mv88e6xxx_rsvd2cpu_setup(struct mv88e6xxx_chip *chip)
{
	if (chip->info->ops->mgmt_rsvd2cpu)
		return chip->info->ops->mgmt_rsvd2cpu(chip);

	return 0;
}

static int mv88e6xxx_atu_setup(struct mv88e6xxx_chip *chip)
{
	int err;

	err = mv88e6xxx_g1_atu_flush(chip, 0, true);
	if (err)
		return err;

	/* The chips that have a "learn2all" bit in Global1, ATU
	 * Control are precisely those whose port registers have a
	 * Message Port bit in Port Control 1 and hence implement
	 * ->port_setup_message_port.
	 */
	if (chip->info->ops->port_setup_message_port) {
		err = mv88e6xxx_g1_atu_set_learn2all(chip, true);
		if (err)
			return err;
	}

	return mv88e6xxx_g1_atu_set_age_time(chip, 300000);
}

static int mv88e6xxx_irl_setup(struct mv88e6xxx_chip *chip)
{
	int port;
	int err;

	if (!chip->info->ops->irl_init_all)
		return 0;

	for (port = 0; port < mv88e6xxx_num_ports(chip); port++) {
		/* Disable ingress rate limiting by resetting all per port
		 * ingress rate limit resources to their initial state.
		 */
		err = chip->info->ops->irl_init_all(chip, port);
		if (err)
			return err;
	}

	return 0;
}

static int mv88e6xxx_mac_setup(struct mv88e6xxx_chip *chip)
{
	if (chip->info->ops->set_switch_mac) {
		u8 addr[ETH_ALEN];

		eth_random_addr(addr);

		return chip->info->ops->set_switch_mac(chip, addr);
	}

	return 0;
}

static int mv88e6xxx_pvt_map(struct mv88e6xxx_chip *chip, int dev, int port)
{
	struct dsa_switch_tree *dst = chip->ds->dst;
	struct dsa_switch *ds;
	struct dsa_port *dp;
	u16 pvlan = 0;

	if (!mv88e6xxx_has_pvt(chip))
		return 0;

	/* Skip the local source device, which uses in-chip port VLAN */
	if (dev != chip->ds->index) {
		pvlan = mv88e6xxx_port_vlan(chip, dev, port);

		ds = dsa_switch_find(dst->index, dev);
		dp = ds ? dsa_to_port(ds, port) : NULL;
		if (dp && dp->lag) {
			/* As the PVT is used to limit flooding of
			 * FORWARD frames, which use the LAG ID as the
			 * source port, we must translate dev/port to
			 * the special "LAG device" in the PVT, using
			 * the LAG ID (one-based) as the port number
			 * (zero-based).
			 */
			dev = MV88E6XXX_G2_PVT_ADDR_DEV_TRUNK;
			port = dsa_port_lag_id_get(dp) - 1;
		}
	}

	return mv88e6xxx_g2_pvt_write(chip, dev, port, pvlan);
}

static int mv88e6xxx_pvt_setup(struct mv88e6xxx_chip *chip)
{
	int dev, port;
	int err;

	if (!mv88e6xxx_has_pvt(chip))
		return 0;

	/* Clear 5 Bit Port for usage with Marvell Link Street devices:
	 * use 4 bits for the Src_Port/Src_Trunk and 5 bits for the Src_Dev.
	 */
	err = mv88e6xxx_g2_misc_4_bit_port(chip);
	if (err)
		return err;

	for (dev = 0; dev < MV88E6XXX_MAX_PVT_SWITCHES; ++dev) {
		for (port = 0; port < MV88E6XXX_MAX_PVT_PORTS; ++port) {
			err = mv88e6xxx_pvt_map(chip, dev, port);
			if (err)
				return err;
		}
	}

	return 0;
}

static int mv88e6xxx_port_fast_age_fid(struct mv88e6xxx_chip *chip, int port,
				       u16 fid)
{
	if (dsa_to_port(chip->ds, port)->lag)
		/* Hardware is incapable of fast-aging a LAG through a
		 * regular ATU move operation. Until we have something
		 * more fancy in place this is a no-op.
		 */
		return -EOPNOTSUPP;

	return mv88e6xxx_g1_atu_remove(chip, fid, port, false);
}

static void mv88e6xxx_port_fast_age(struct dsa_switch *ds, int port)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_port_fast_age_fid(chip, port, 0);
	mv88e6xxx_reg_unlock(chip);

	if (err)
		dev_err(chip->ds->dev, "p%d: failed to flush ATU: %d\n",
			port, err);
}

static int mv88e6xxx_vtu_setup(struct mv88e6xxx_chip *chip)
{
	if (!mv88e6xxx_max_vid(chip))
		return 0;

	return mv88e6xxx_g1_vtu_flush(chip);
}

static int mv88e6xxx_vtu_get(struct mv88e6xxx_chip *chip, u16 vid,
			     struct mv88e6xxx_vtu_entry *entry)
{
	int err;

	if (!chip->info->ops->vtu_getnext)
		return -EOPNOTSUPP;

	entry->vid = vid ? vid - 1 : mv88e6xxx_max_vid(chip);
	entry->valid = false;

	err = chip->info->ops->vtu_getnext(chip, entry);

	if (entry->vid != vid)
		entry->valid = false;

	return err;
}

int mv88e6xxx_vtu_walk(struct mv88e6xxx_chip *chip,
		       int (*cb)(struct mv88e6xxx_chip *chip,
				 const struct mv88e6xxx_vtu_entry *entry,
				 void *priv),
		       void *priv)
{
	struct mv88e6xxx_vtu_entry entry = {
		.vid = mv88e6xxx_max_vid(chip),
		.valid = false,
	};
	int err;

	if (!chip->info->ops->vtu_getnext)
		return -EOPNOTSUPP;

	do {
		err = chip->info->ops->vtu_getnext(chip, &entry);
		if (err)
			return err;

		if (!entry.valid)
			break;

		err = cb(chip, &entry, priv);
		if (err)
			return err;
	} while (entry.vid < mv88e6xxx_max_vid(chip));

	return 0;
}

static int mv88e6xxx_vtu_loadpurge(struct mv88e6xxx_chip *chip,
				   struct mv88e6xxx_vtu_entry *entry)
{
	if (!chip->info->ops->vtu_loadpurge)
		return -EOPNOTSUPP;

	return chip->info->ops->vtu_loadpurge(chip, entry);
}

static int mv88e6xxx_fid_map_vlan(struct mv88e6xxx_chip *chip,
				  const struct mv88e6xxx_vtu_entry *entry,
				  void *_fid_bitmap)
{
	unsigned long *fid_bitmap = _fid_bitmap;

	set_bit(entry->fid, fid_bitmap);
	return 0;
}

int mv88e6xxx_fid_map(struct mv88e6xxx_chip *chip, unsigned long *fid_bitmap)
{
	bitmap_zero(fid_bitmap, MV88E6XXX_N_FID);

	/* Every FID has an associated VID, so walking the VTU
	 * will discover the full set of FIDs in use.
	 */
	return mv88e6xxx_vtu_walk(chip, mv88e6xxx_fid_map_vlan, fid_bitmap);
}

static int mv88e6xxx_atu_new(struct mv88e6xxx_chip *chip, u16 *fid)
{
	DECLARE_BITMAP(fid_bitmap, MV88E6XXX_N_FID);
	int err;

	err = mv88e6xxx_fid_map(chip, fid_bitmap);
	if (err)
		return err;

	*fid = find_first_zero_bit(fid_bitmap, MV88E6XXX_N_FID);
	if (unlikely(*fid >= mv88e6xxx_num_databases(chip)))
		return -ENOSPC;

	/* Clear the database */
	return mv88e6xxx_g1_atu_flush(chip, *fid, true);
}

static int mv88e6xxx_stu_loadpurge(struct mv88e6xxx_chip *chip,
				   struct mv88e6xxx_stu_entry *entry)
{
	if (!chip->info->ops->stu_loadpurge)
		return -EOPNOTSUPP;

	return chip->info->ops->stu_loadpurge(chip, entry);
}

static int mv88e6xxx_stu_setup(struct mv88e6xxx_chip *chip)
{
	struct mv88e6xxx_stu_entry stu = {
		.valid = true,
		.sid = 0
	};

	if (!mv88e6xxx_has_stu(chip))
		return 0;

	/* Make sure that SID 0 is always valid. This is used by VTU
	 * entries that do not make use of the STU, e.g. when creating
	 * a VLAN upper on a port that is also part of a VLAN
	 * filtering bridge.
	 */
	return mv88e6xxx_stu_loadpurge(chip, &stu);
}

static int mv88e6xxx_sid_get(struct mv88e6xxx_chip *chip, u8 *sid)
{
	DECLARE_BITMAP(busy, MV88E6XXX_N_SID) = { 0 };
	struct mv88e6xxx_mst *mst;

	__set_bit(0, busy);

	list_for_each_entry(mst, &chip->msts, node)
		__set_bit(mst->stu.sid, busy);

	*sid = find_first_zero_bit(busy, MV88E6XXX_N_SID);

	return (*sid >= mv88e6xxx_max_sid(chip)) ? -ENOSPC : 0;
}

static int mv88e6xxx_mst_put(struct mv88e6xxx_chip *chip, u8 sid)
{
	struct mv88e6xxx_mst *mst, *tmp;
	int err;

	if (!sid)
		return 0;

	list_for_each_entry_safe(mst, tmp, &chip->msts, node) {
		if (mst->stu.sid != sid)
			continue;

		if (!refcount_dec_and_test(&mst->refcnt))
			return 0;

		mst->stu.valid = false;
		err = mv88e6xxx_stu_loadpurge(chip, &mst->stu);
		if (err) {
			refcount_set(&mst->refcnt, 1);
			return err;
		}

		list_del(&mst->node);
		kfree(mst);
		return 0;
	}

	return -ENOENT;
}

static int mv88e6xxx_mst_get(struct mv88e6xxx_chip *chip, struct net_device *br,
			     u16 msti, u8 *sid)
{
	struct mv88e6xxx_mst *mst;
	int err, i;

	if (!mv88e6xxx_has_stu(chip)) {
		err = -EOPNOTSUPP;
		goto err;
	}

	if (!msti) {
		*sid = 0;
		return 0;
	}

	list_for_each_entry(mst, &chip->msts, node) {
		if (mst->br == br && mst->msti == msti) {
			refcount_inc(&mst->refcnt);
			*sid = mst->stu.sid;
			return 0;
		}
	}

	err = mv88e6xxx_sid_get(chip, sid);
	if (err)
		goto err;

	mst = kzalloc(sizeof(*mst), GFP_KERNEL);
	if (!mst) {
		err = -ENOMEM;
		goto err;
	}

	INIT_LIST_HEAD(&mst->node);
	refcount_set(&mst->refcnt, 1);
	mst->br = br;
	mst->msti = msti;
	mst->stu.valid = true;
	mst->stu.sid = *sid;

	/* The bridge starts out all ports in the disabled state. But
	 * a STU state of disabled means to go by the port-global
	 * state. So we set all user port's initial state to blocking,
	 * to match the bridge's behavior.
	 */
	for (i = 0; i < mv88e6xxx_num_ports(chip); i++)
		mst->stu.state[i] = dsa_is_user_port(chip->ds, i) ?
			MV88E6XXX_PORT_CTL0_STATE_BLOCKING :
			MV88E6XXX_PORT_CTL0_STATE_DISABLED;

	err = mv88e6xxx_stu_loadpurge(chip, &mst->stu);
	if (err)
		goto err_free;

	list_add_tail(&mst->node, &chip->msts);
	return 0;

err_free:
	kfree(mst);
err:
	return err;
}

static int mv88e6xxx_port_mst_state_set(struct dsa_switch *ds, int port,
					const struct switchdev_mst_state *st)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_mst *mst;
	u8 state;
	int err;

	if (!mv88e6xxx_has_stu(chip))
		return -EOPNOTSUPP;

	switch (st->state) {
	case BR_STATE_DISABLED:
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

	list_for_each_entry(mst, &chip->msts, node) {
		if (mst->br == dsa_port_bridge_dev_get(dp) &&
		    mst->msti == st->msti) {
			if (mst->stu.state[port] == state)
				return 0;

			mst->stu.state[port] = state;
			mv88e6xxx_reg_lock(chip);
			err = mv88e6xxx_stu_loadpurge(chip, &mst->stu);
			mv88e6xxx_reg_unlock(chip);
			return err;
		}
	}

	return -ENOENT;
}

static int mv88e6xxx_port_check_hw_vlan(struct dsa_switch *ds, int port,
					u16 vid)
{
	struct dsa_port *dp = dsa_to_port(ds, port), *other_dp;
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_vtu_entry vlan;
	int err;

	/* DSA and CPU ports have to be members of multiple vlans */
	if (dsa_port_is_dsa(dp) || dsa_port_is_cpu(dp))
		return 0;

	err = mv88e6xxx_vtu_get(chip, vid, &vlan);
	if (err)
		return err;

	if (!vlan.valid)
		return 0;

	dsa_switch_for_each_user_port(other_dp, ds) {
		struct net_device *other_br;

		if (vlan.member[other_dp->index] ==
		    MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_NON_MEMBER)
			continue;

		if (dsa_port_bridge_same(dp, other_dp))
			break; /* same bridge, check next VLAN */

		other_br = dsa_port_bridge_dev_get(other_dp);
		if (!other_br)
			continue;

		dev_err(ds->dev, "p%d: hw VLAN %d already used by port %d in %s\n",
			port, vlan.vid, other_dp->index, netdev_name(other_br));
		return -EOPNOTSUPP;
	}

	return 0;
}

static int mv88e6xxx_port_commit_pvid(struct mv88e6xxx_chip *chip, int port)
{
	struct dsa_port *dp = dsa_to_port(chip->ds, port);
	struct net_device *br = dsa_port_bridge_dev_get(dp);
	struct mv88e6xxx_port *p = &chip->ports[port];
	u16 pvid = MV88E6XXX_VID_STANDALONE;
	bool drop_untagged = false;
	int err;

	if (br) {
		if (br_vlan_enabled(br)) {
			pvid = p->bridge_pvid.vid;
			drop_untagged = !p->bridge_pvid.valid;
		} else {
			pvid = MV88E6XXX_VID_BRIDGED;
		}
	}

	err = mv88e6xxx_port_set_pvid(chip, port, pvid);
	if (err)
		return err;

	return mv88e6xxx_port_drop_untagged(chip, port, drop_untagged);
}

static int mv88e6xxx_port_vlan_filtering(struct dsa_switch *ds, int port,
					 bool vlan_filtering,
					 struct netlink_ext_ack *extack)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	u16 mode = vlan_filtering ? MV88E6XXX_PORT_CTL2_8021Q_MODE_SECURE :
		MV88E6XXX_PORT_CTL2_8021Q_MODE_DISABLED;
	int err;

	if (!mv88e6xxx_max_vid(chip))
		return -EOPNOTSUPP;

	mv88e6xxx_reg_lock(chip);

	err = mv88e6xxx_port_set_8021q_mode(chip, port, mode);
	if (err)
		goto unlock;

	err = mv88e6xxx_port_commit_pvid(chip, port);
	if (err)
		goto unlock;

unlock:
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static int
mv88e6xxx_port_vlan_prepare(struct dsa_switch *ds, int port,
			    const struct switchdev_obj_port_vlan *vlan)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	if (!mv88e6xxx_max_vid(chip))
		return -EOPNOTSUPP;

	/* If the requested port doesn't belong to the same bridge as the VLAN
	 * members, do not support it (yet) and fallback to software VLAN.
	 */
	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_port_check_hw_vlan(ds, port, vlan->vid);
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static int mv88e6xxx_port_db_load_purge(struct mv88e6xxx_chip *chip, int port,
					const unsigned char *addr, u16 vid,
					u8 state)
{
	struct mv88e6xxx_atu_entry entry;
	struct mv88e6xxx_vtu_entry vlan;
	u16 fid;
	int err;

	/* Ports have two private address databases: one for when the port is
	 * standalone and one for when the port is under a bridge and the
	 * 802.1Q mode is disabled. When the port is standalone, DSA wants its
	 * address database to remain 100% empty, so we never load an ATU entry
	 * into a standalone port's database. Therefore, translate the null
	 * VLAN ID into the port's database used for VLAN-unaware bridging.
	 */
	if (vid == 0) {
		fid = MV88E6XXX_FID_BRIDGED;
	} else {
		err = mv88e6xxx_vtu_get(chip, vid, &vlan);
		if (err)
			return err;

		/* switchdev expects -EOPNOTSUPP to honor software VLANs */
		if (!vlan.valid)
			return -EOPNOTSUPP;

		fid = vlan.fid;
	}

	entry.state = 0;
	ether_addr_copy(entry.mac, addr);
	eth_addr_dec(entry.mac);

	err = mv88e6xxx_g1_atu_getnext(chip, fid, &entry);
	if (err)
		return err;

	/* Initialize a fresh ATU entry if it isn't found */
	if (!entry.state || !ether_addr_equal(entry.mac, addr)) {
		memset(&entry, 0, sizeof(entry));
		ether_addr_copy(entry.mac, addr);
	}

	/* Purge the ATU entry only if no port is using it anymore */
	if (!state) {
		entry.portvec &= ~BIT(port);
		if (!entry.portvec)
			entry.state = 0;
	} else {
		if (state == MV88E6XXX_G1_ATU_DATA_STATE_UC_STATIC)
			entry.portvec = BIT(port);
		else
			entry.portvec |= BIT(port);

		entry.state = state;
	}

	return mv88e6xxx_g1_atu_loadpurge(chip, fid, &entry);
}

static int mv88e6xxx_policy_apply(struct mv88e6xxx_chip *chip, int port,
				  const struct mv88e6xxx_policy *policy)
{
	enum mv88e6xxx_policy_mapping mapping = policy->mapping;
	enum mv88e6xxx_policy_action action = policy->action;
	const u8 *addr = policy->addr;
	u16 vid = policy->vid;
	u8 state;
	int err;
	int id;

	if (!chip->info->ops->port_set_policy)
		return -EOPNOTSUPP;

	switch (mapping) {
	case MV88E6XXX_POLICY_MAPPING_DA:
	case MV88E6XXX_POLICY_MAPPING_SA:
		if (action == MV88E6XXX_POLICY_ACTION_NORMAL)
			state = 0; /* Dissociate the port and address */
		else if (action == MV88E6XXX_POLICY_ACTION_DISCARD &&
			 is_multicast_ether_addr(addr))
			state = MV88E6XXX_G1_ATU_DATA_STATE_MC_STATIC_POLICY;
		else if (action == MV88E6XXX_POLICY_ACTION_DISCARD &&
			 is_unicast_ether_addr(addr))
			state = MV88E6XXX_G1_ATU_DATA_STATE_UC_STATIC_POLICY;
		else
			return -EOPNOTSUPP;

		err = mv88e6xxx_port_db_load_purge(chip, port, addr, vid,
						   state);
		if (err)
			return err;
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* Skip the port's policy clearing if the mapping is still in use */
	if (action == MV88E6XXX_POLICY_ACTION_NORMAL)
		idr_for_each_entry(&chip->policies, policy, id)
			if (policy->port == port &&
			    policy->mapping == mapping &&
			    policy->action != action)
				return 0;

	return chip->info->ops->port_set_policy(chip, port, mapping, action);
}

static int mv88e6xxx_policy_insert(struct mv88e6xxx_chip *chip, int port,
				   struct ethtool_rx_flow_spec *fs)
{
	struct ethhdr *mac_entry = &fs->h_u.ether_spec;
	struct ethhdr *mac_mask = &fs->m_u.ether_spec;
	enum mv88e6xxx_policy_mapping mapping;
	enum mv88e6xxx_policy_action action;
	struct mv88e6xxx_policy *policy;
	u16 vid = 0;
	u8 *addr;
	int err;
	int id;

	if (fs->location != RX_CLS_LOC_ANY)
		return -EINVAL;

	if (fs->ring_cookie == RX_CLS_FLOW_DISC)
		action = MV88E6XXX_POLICY_ACTION_DISCARD;
	else
		return -EOPNOTSUPP;

	switch (fs->flow_type & ~FLOW_EXT) {
	case ETHER_FLOW:
		if (!is_zero_ether_addr(mac_mask->h_dest) &&
		    is_zero_ether_addr(mac_mask->h_source)) {
			mapping = MV88E6XXX_POLICY_MAPPING_DA;
			addr = mac_entry->h_dest;
		} else if (is_zero_ether_addr(mac_mask->h_dest) &&
		    !is_zero_ether_addr(mac_mask->h_source)) {
			mapping = MV88E6XXX_POLICY_MAPPING_SA;
			addr = mac_entry->h_source;
		} else {
			/* Cannot support DA and SA mapping in the same rule */
			return -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	if ((fs->flow_type & FLOW_EXT) && fs->m_ext.vlan_tci) {
		if (fs->m_ext.vlan_tci != htons(0xffff))
			return -EOPNOTSUPP;
		vid = be16_to_cpu(fs->h_ext.vlan_tci) & VLAN_VID_MASK;
	}

	idr_for_each_entry(&chip->policies, policy, id) {
		if (policy->port == port && policy->mapping == mapping &&
		    policy->action == action && policy->vid == vid &&
		    ether_addr_equal(policy->addr, addr))
			return -EEXIST;
	}

	policy = devm_kzalloc(chip->dev, sizeof(*policy), GFP_KERNEL);
	if (!policy)
		return -ENOMEM;

	fs->location = 0;
	err = idr_alloc_u32(&chip->policies, policy, &fs->location, 0xffffffff,
			    GFP_KERNEL);
	if (err) {
		devm_kfree(chip->dev, policy);
		return err;
	}

	memcpy(&policy->fs, fs, sizeof(*fs));
	ether_addr_copy(policy->addr, addr);
	policy->mapping = mapping;
	policy->action = action;
	policy->port = port;
	policy->vid = vid;

	err = mv88e6xxx_policy_apply(chip, port, policy);
	if (err) {
		idr_remove(&chip->policies, fs->location);
		devm_kfree(chip->dev, policy);
		return err;
	}

	return 0;
}

static int mv88e6xxx_get_rxnfc(struct dsa_switch *ds, int port,
			       struct ethtool_rxnfc *rxnfc, u32 *rule_locs)
{
	struct ethtool_rx_flow_spec *fs = &rxnfc->fs;
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_policy *policy;
	int err;
	int id;

	mv88e6xxx_reg_lock(chip);

	switch (rxnfc->cmd) {
	case ETHTOOL_GRXCLSRLCNT:
		rxnfc->data = 0;
		rxnfc->data |= RX_CLS_LOC_SPECIAL;
		rxnfc->rule_cnt = 0;
		idr_for_each_entry(&chip->policies, policy, id)
			if (policy->port == port)
				rxnfc->rule_cnt++;
		err = 0;
		break;
	case ETHTOOL_GRXCLSRULE:
		err = -ENOENT;
		policy = idr_find(&chip->policies, fs->location);
		if (policy) {
			memcpy(fs, &policy->fs, sizeof(*fs));
			err = 0;
		}
		break;
	case ETHTOOL_GRXCLSRLALL:
		rxnfc->data = 0;
		rxnfc->rule_cnt = 0;
		idr_for_each_entry(&chip->policies, policy, id)
			if (policy->port == port)
				rule_locs[rxnfc->rule_cnt++] = id;
		err = 0;
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	mv88e6xxx_reg_unlock(chip);

	return err;
}

static int mv88e6xxx_set_rxnfc(struct dsa_switch *ds, int port,
			       struct ethtool_rxnfc *rxnfc)
{
	struct ethtool_rx_flow_spec *fs = &rxnfc->fs;
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_policy *policy;
	int err;

	mv88e6xxx_reg_lock(chip);

	switch (rxnfc->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		err = mv88e6xxx_policy_insert(chip, port, fs);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		err = -ENOENT;
		policy = idr_remove(&chip->policies, fs->location);
		if (policy) {
			policy->action = MV88E6XXX_POLICY_ACTION_NORMAL;
			err = mv88e6xxx_policy_apply(chip, port, policy);
			devm_kfree(chip->dev, policy);
		}
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	mv88e6xxx_reg_unlock(chip);

	return err;
}

static int mv88e6xxx_port_add_broadcast(struct mv88e6xxx_chip *chip, int port,
					u16 vid)
{
	u8 state = MV88E6XXX_G1_ATU_DATA_STATE_MC_STATIC;
	u8 broadcast[ETH_ALEN];

	eth_broadcast_addr(broadcast);

	return mv88e6xxx_port_db_load_purge(chip, port, broadcast, vid, state);
}

static int mv88e6xxx_broadcast_setup(struct mv88e6xxx_chip *chip, u16 vid)
{
	int port;
	int err;

	for (port = 0; port < mv88e6xxx_num_ports(chip); port++) {
		struct dsa_port *dp = dsa_to_port(chip->ds, port);
		struct net_device *brport;

		if (dsa_is_unused_port(chip->ds, port))
			continue;

		brport = dsa_port_to_bridge_port(dp);
		if (brport && !br_port_flag_is_set(brport, BR_BCAST_FLOOD))
			/* Skip bridged user ports where broadcast
			 * flooding is disabled.
			 */
			continue;

		err = mv88e6xxx_port_add_broadcast(chip, port, vid);
		if (err)
			return err;
	}

	return 0;
}

struct mv88e6xxx_port_broadcast_sync_ctx {
	int port;
	bool flood;
};

static int
mv88e6xxx_port_broadcast_sync_vlan(struct mv88e6xxx_chip *chip,
				   const struct mv88e6xxx_vtu_entry *vlan,
				   void *_ctx)
{
	struct mv88e6xxx_port_broadcast_sync_ctx *ctx = _ctx;
	u8 broadcast[ETH_ALEN];
	u8 state;

	if (ctx->flood)
		state = MV88E6XXX_G1_ATU_DATA_STATE_MC_STATIC;
	else
		state = MV88E6XXX_G1_ATU_DATA_STATE_MC_UNUSED;

	eth_broadcast_addr(broadcast);

	return mv88e6xxx_port_db_load_purge(chip, ctx->port, broadcast,
					    vlan->vid, state);
}

static int mv88e6xxx_port_broadcast_sync(struct mv88e6xxx_chip *chip, int port,
					 bool flood)
{
	struct mv88e6xxx_port_broadcast_sync_ctx ctx = {
		.port = port,
		.flood = flood,
	};
	struct mv88e6xxx_vtu_entry vid0 = {
		.vid = 0,
	};
	int err;

	/* Update the port's private database... */
	err = mv88e6xxx_port_broadcast_sync_vlan(chip, &vid0, &ctx);
	if (err)
		return err;

	/* ...and the database for all VLANs. */
	return mv88e6xxx_vtu_walk(chip, mv88e6xxx_port_broadcast_sync_vlan,
				  &ctx);
}

static int mv88e6xxx_port_vlan_join(struct mv88e6xxx_chip *chip, int port,
				    u16 vid, u8 member, bool warn)
{
	const u8 non_member = MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_NON_MEMBER;
	struct mv88e6xxx_vtu_entry vlan;
	int i, err;

	err = mv88e6xxx_vtu_get(chip, vid, &vlan);
	if (err)
		return err;

	if (!vlan.valid) {
		memset(&vlan, 0, sizeof(vlan));

		if (vid == MV88E6XXX_VID_STANDALONE)
			vlan.policy = true;

		err = mv88e6xxx_atu_new(chip, &vlan.fid);
		if (err)
			return err;

		for (i = 0; i < mv88e6xxx_num_ports(chip); ++i)
			if (i == port)
				vlan.member[i] = member;
			else
				vlan.member[i] = non_member;

		vlan.vid = vid;
		vlan.valid = true;

		err = mv88e6xxx_vtu_loadpurge(chip, &vlan);
		if (err)
			return err;

		err = mv88e6xxx_broadcast_setup(chip, vlan.vid);
		if (err)
			return err;
	} else if (vlan.member[port] != member) {
		vlan.member[port] = member;

		err = mv88e6xxx_vtu_loadpurge(chip, &vlan);
		if (err)
			return err;
	} else if (warn) {
		dev_info(chip->dev, "p%d: already a member of VLAN %d\n",
			 port, vid);
	}

	return 0;
}

static int mv88e6xxx_port_vlan_add(struct dsa_switch *ds, int port,
				   const struct switchdev_obj_port_vlan *vlan,
				   struct netlink_ext_ack *extack)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	struct mv88e6xxx_port *p = &chip->ports[port];
	bool warn;
	u8 member;
	int err;

	if (!vlan->vid)
		return 0;

	err = mv88e6xxx_port_vlan_prepare(ds, port, vlan);
	if (err)
		return err;

	if (dsa_is_dsa_port(ds, port) || dsa_is_cpu_port(ds, port))
		member = MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_UNMODIFIED;
	else if (untagged)
		member = MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_UNTAGGED;
	else
		member = MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_TAGGED;

	/* net/dsa/slave.c will call dsa_port_vlan_add() for the affected port
	 * and then the CPU port. Do not warn for duplicates for the CPU port.
	 */
	warn = !dsa_is_cpu_port(ds, port) && !dsa_is_dsa_port(ds, port);

	mv88e6xxx_reg_lock(chip);

	err = mv88e6xxx_port_vlan_join(chip, port, vlan->vid, member, warn);
	if (err) {
		dev_err(ds->dev, "p%d: failed to add VLAN %d%c\n", port,
			vlan->vid, untagged ? 'u' : 't');
		goto out;
	}

	if (pvid) {
		p->bridge_pvid.vid = vlan->vid;
		p->bridge_pvid.valid = true;

		err = mv88e6xxx_port_commit_pvid(chip, port);
		if (err)
			goto out;
	} else if (vlan->vid && p->bridge_pvid.vid == vlan->vid) {
		/* The old pvid was reinstalled as a non-pvid VLAN */
		p->bridge_pvid.valid = false;

		err = mv88e6xxx_port_commit_pvid(chip, port);
		if (err)
			goto out;
	}

out:
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static int mv88e6xxx_port_vlan_leave(struct mv88e6xxx_chip *chip,
				     int port, u16 vid)
{
	struct mv88e6xxx_vtu_entry vlan;
	int i, err;

	if (!vid)
		return 0;

	err = mv88e6xxx_vtu_get(chip, vid, &vlan);
	if (err)
		return err;

	/* If the VLAN doesn't exist in hardware or the port isn't a member,
	 * tell switchdev that this VLAN is likely handled in software.
	 */
	if (!vlan.valid ||
	    vlan.member[port] == MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_NON_MEMBER)
		return -EOPNOTSUPP;

	vlan.member[port] = MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_NON_MEMBER;

	/* keep the VLAN unless all ports are excluded */
	vlan.valid = false;
	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i) {
		if (vlan.member[i] !=
		    MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_NON_MEMBER) {
			vlan.valid = true;
			break;
		}
	}

	err = mv88e6xxx_vtu_loadpurge(chip, &vlan);
	if (err)
		return err;

	if (!vlan.valid) {
		err = mv88e6xxx_mst_put(chip, vlan.sid);
		if (err)
			return err;
	}

	return mv88e6xxx_g1_atu_remove(chip, vlan.fid, port, false);
}

static int mv88e6xxx_port_vlan_del(struct dsa_switch *ds, int port,
				   const struct switchdev_obj_port_vlan *vlan)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_port *p = &chip->ports[port];
	int err = 0;
	u16 pvid;

	if (!mv88e6xxx_max_vid(chip))
		return -EOPNOTSUPP;

	/* The ATU removal procedure needs the FID to be mapped in the VTU,
	 * but FDB deletion runs concurrently with VLAN deletion. Flush the DSA
	 * switchdev workqueue to ensure that all FDB entries are deleted
	 * before we remove the VLAN.
	 */
	dsa_flush_workqueue();

	mv88e6xxx_reg_lock(chip);

	err = mv88e6xxx_port_get_pvid(chip, port, &pvid);
	if (err)
		goto unlock;

	err = mv88e6xxx_port_vlan_leave(chip, port, vlan->vid);
	if (err)
		goto unlock;

	if (vlan->vid == pvid) {
		p->bridge_pvid.valid = false;

		err = mv88e6xxx_port_commit_pvid(chip, port);
		if (err)
			goto unlock;
	}

unlock:
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static int mv88e6xxx_port_vlan_fast_age(struct dsa_switch *ds, int port, u16 vid)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_vtu_entry vlan;
	int err;

	mv88e6xxx_reg_lock(chip);

	err = mv88e6xxx_vtu_get(chip, vid, &vlan);
	if (err)
		goto unlock;

	err = mv88e6xxx_port_fast_age_fid(chip, port, vlan.fid);

unlock:
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static int mv88e6xxx_vlan_msti_set(struct dsa_switch *ds,
				   struct dsa_bridge bridge,
				   const struct switchdev_vlan_msti *msti)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_vtu_entry vlan;
	u8 old_sid, new_sid;
	int err;

	if (!mv88e6xxx_has_stu(chip))
		return -EOPNOTSUPP;

	mv88e6xxx_reg_lock(chip);

	err = mv88e6xxx_vtu_get(chip, msti->vid, &vlan);
	if (err)
		goto unlock;

	if (!vlan.valid) {
		err = -EINVAL;
		goto unlock;
	}

	old_sid = vlan.sid;

	err = mv88e6xxx_mst_get(chip, bridge.dev, msti->msti, &new_sid);
	if (err)
		goto unlock;

	if (new_sid != old_sid) {
		vlan.sid = new_sid;

		err = mv88e6xxx_vtu_loadpurge(chip, &vlan);
		if (err) {
			mv88e6xxx_mst_put(chip, new_sid);
			goto unlock;
		}
	}

	err = mv88e6xxx_mst_put(chip, old_sid);

unlock:
	mv88e6xxx_reg_unlock(chip);
	return err;
}

static int mv88e6xxx_port_fdb_add(struct dsa_switch *ds, int port,
				  const unsigned char *addr, u16 vid,
				  struct dsa_db db)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_port_db_load_purge(chip, port, addr, vid,
					   MV88E6XXX_G1_ATU_DATA_STATE_UC_STATIC);
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static int mv88e6xxx_port_fdb_del(struct dsa_switch *ds, int port,
				  const unsigned char *addr, u16 vid,
				  struct dsa_db db)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_port_db_load_purge(chip, port, addr, vid, 0);
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static int mv88e6xxx_port_db_dump_fid(struct mv88e6xxx_chip *chip,
				      u16 fid, u16 vid, int port,
				      dsa_fdb_dump_cb_t *cb, void *data)
{
	struct mv88e6xxx_atu_entry addr;
	bool is_static;
	int err;

	addr.state = 0;
	eth_broadcast_addr(addr.mac);

	do {
		err = mv88e6xxx_g1_atu_getnext(chip, fid, &addr);
		if (err)
			return err;

		if (!addr.state)
			break;

		if (addr.trunk || (addr.portvec & BIT(port)) == 0)
			continue;

		if (!is_unicast_ether_addr(addr.mac))
			continue;

		is_static = (addr.state ==
			     MV88E6XXX_G1_ATU_DATA_STATE_UC_STATIC);
		err = cb(addr.mac, vid, is_static, data);
		if (err)
			return err;
	} while (!is_broadcast_ether_addr(addr.mac));

	return err;
}

struct mv88e6xxx_port_db_dump_vlan_ctx {
	int port;
	dsa_fdb_dump_cb_t *cb;
	void *data;
};

static int mv88e6xxx_port_db_dump_vlan(struct mv88e6xxx_chip *chip,
				       const struct mv88e6xxx_vtu_entry *entry,
				       void *_data)
{
	struct mv88e6xxx_port_db_dump_vlan_ctx *ctx = _data;

	return mv88e6xxx_port_db_dump_fid(chip, entry->fid, entry->vid,
					  ctx->port, ctx->cb, ctx->data);
}

static int mv88e6xxx_port_db_dump(struct mv88e6xxx_chip *chip, int port,
				  dsa_fdb_dump_cb_t *cb, void *data)
{
	struct mv88e6xxx_port_db_dump_vlan_ctx ctx = {
		.port = port,
		.cb = cb,
		.data = data,
	};
	u16 fid;
	int err;

	/* Dump port's default Filtering Information Database (VLAN ID 0) */
	err = mv88e6xxx_port_get_fid(chip, port, &fid);
	if (err)
		return err;

	err = mv88e6xxx_port_db_dump_fid(chip, fid, 0, port, cb, data);
	if (err)
		return err;

	return mv88e6xxx_vtu_walk(chip, mv88e6xxx_port_db_dump_vlan, &ctx);
}

static int mv88e6xxx_port_fdb_dump(struct dsa_switch *ds, int port,
				   dsa_fdb_dump_cb_t *cb, void *data)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_port_db_dump(chip, port, cb, data);
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static int mv88e6xxx_bridge_map(struct mv88e6xxx_chip *chip,
				struct dsa_bridge bridge)
{
	struct dsa_switch *ds = chip->ds;
	struct dsa_switch_tree *dst = ds->dst;
	struct dsa_port *dp;
	int err;

	list_for_each_entry(dp, &dst->ports, list) {
		if (dsa_port_offloads_bridge(dp, &bridge)) {
			if (dp->ds == ds) {
				/* This is a local bridge group member,
				 * remap its Port VLAN Map.
				 */
				err = mv88e6xxx_port_vlan_map(chip, dp->index);
				if (err)
					return err;
			} else {
				/* This is an external bridge group member,
				 * remap its cross-chip Port VLAN Table entry.
				 */
				err = mv88e6xxx_pvt_map(chip, dp->ds->index,
							dp->index);
				if (err)
					return err;
			}
		}
	}

	return 0;
}

/* Treat the software bridge as a virtual single-port switch behind the
 * CPU and map in the PVT. First dst->last_switch elements are taken by
 * physical switches, so start from beyond that range.
 */
static int mv88e6xxx_map_virtual_bridge_to_pvt(struct dsa_switch *ds,
					       unsigned int bridge_num)
{
	u8 dev = bridge_num + ds->dst->last_switch;
	struct mv88e6xxx_chip *chip = ds->priv;

	return mv88e6xxx_pvt_map(chip, dev, 0);
}

static int mv88e6xxx_port_bridge_join(struct dsa_switch *ds, int port,
				      struct dsa_bridge bridge,
				      bool *tx_fwd_offload,
				      struct netlink_ext_ack *extack)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mv88e6xxx_reg_lock(chip);

	err = mv88e6xxx_bridge_map(chip, bridge);
	if (err)
		goto unlock;

	err = mv88e6xxx_port_set_map_da(chip, port, true);
	if (err)
		goto unlock;

	err = mv88e6xxx_port_commit_pvid(chip, port);
	if (err)
		goto unlock;

	if (mv88e6xxx_has_pvt(chip)) {
		err = mv88e6xxx_map_virtual_bridge_to_pvt(ds, bridge.num);
		if (err)
			goto unlock;

		*tx_fwd_offload = true;
	}

unlock:
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static void mv88e6xxx_port_bridge_leave(struct dsa_switch *ds, int port,
					struct dsa_bridge bridge)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mv88e6xxx_reg_lock(chip);

	if (bridge.tx_fwd_offload &&
	    mv88e6xxx_map_virtual_bridge_to_pvt(ds, bridge.num))
		dev_err(ds->dev, "failed to remap cross-chip Port VLAN\n");

	if (mv88e6xxx_bridge_map(chip, bridge) ||
	    mv88e6xxx_port_vlan_map(chip, port))
		dev_err(ds->dev, "failed to remap in-chip Port VLAN\n");

	err = mv88e6xxx_port_set_map_da(chip, port, false);
	if (err)
		dev_err(ds->dev,
			"port %d failed to restore map-DA: %pe\n",
			port, ERR_PTR(err));

	err = mv88e6xxx_port_commit_pvid(chip, port);
	if (err)
		dev_err(ds->dev,
			"port %d failed to restore standalone pvid: %pe\n",
			port, ERR_PTR(err));

	mv88e6xxx_reg_unlock(chip);
}

static int mv88e6xxx_crosschip_bridge_join(struct dsa_switch *ds,
					   int tree_index, int sw_index,
					   int port, struct dsa_bridge bridge,
					   struct netlink_ext_ack *extack)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	if (tree_index != ds->dst->index)
		return 0;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_pvt_map(chip, sw_index, port);
	err = err ? : mv88e6xxx_map_virtual_bridge_to_pvt(ds, bridge.num);
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static void mv88e6xxx_crosschip_bridge_leave(struct dsa_switch *ds,
					     int tree_index, int sw_index,
					     int port, struct dsa_bridge bridge)
{
	struct mv88e6xxx_chip *chip = ds->priv;

	if (tree_index != ds->dst->index)
		return;

	mv88e6xxx_reg_lock(chip);
	if (mv88e6xxx_pvt_map(chip, sw_index, port) ||
	    mv88e6xxx_map_virtual_bridge_to_pvt(ds, bridge.num))
		dev_err(ds->dev, "failed to remap cross-chip Port VLAN\n");
	mv88e6xxx_reg_unlock(chip);
}

static int mv88e6xxx_software_reset(struct mv88e6xxx_chip *chip)
{
	if (chip->info->ops->reset)
		return chip->info->ops->reset(chip);

	return 0;
}

static void mv88e6xxx_hardware_reset(struct mv88e6xxx_chip *chip)
{
	struct gpio_desc *gpiod = chip->reset;

	/* If there is a GPIO connected to the reset pin, toggle it */
	if (gpiod) {
		/* If the switch has just been reset and not yet completed
		 * loading EEPROM, the reset may interrupt the I2C transaction
		 * mid-byte, causing the first EEPROM read after the reset
		 * from the wrong location resulting in the switch booting
		 * to wrong mode and inoperable.
		 */
		mv88e6xxx_g1_wait_eeprom_done(chip);

		gpiod_set_value_cansleep(gpiod, 1);
		usleep_range(10000, 20000);
		gpiod_set_value_cansleep(gpiod, 0);
		usleep_range(10000, 20000);

		mv88e6xxx_g1_wait_eeprom_done(chip);
	}
}

static int mv88e6xxx_disable_ports(struct mv88e6xxx_chip *chip)
{
	int i, err;

	/* Set all ports to the Disabled state */
	for (i = 0; i < mv88e6xxx_num_ports(chip); i++) {
		err = mv88e6xxx_port_set_state(chip, i, BR_STATE_DISABLED);
		if (err)
			return err;
	}

	/* Wait for transmit queues to drain,
	 * i.e. 2ms for a maximum frame to be transmitted at 10 Mbps.
	 */
	usleep_range(2000, 4000);

	return 0;
}

static int mv88e6xxx_switch_reset(struct mv88e6xxx_chip *chip)
{
	int err;

	err = mv88e6xxx_disable_ports(chip);
	if (err)
		return err;

	mv88e6xxx_hardware_reset(chip);

	return mv88e6xxx_software_reset(chip);
}

static int mv88e6xxx_set_port_mode(struct mv88e6xxx_chip *chip, int port,
				   enum mv88e6xxx_frame_mode frame,
				   enum mv88e6xxx_egress_mode egress, u16 etype)
{
	int err;

	if (!chip->info->ops->port_set_frame_mode)
		return -EOPNOTSUPP;

	err = mv88e6xxx_port_set_egress_mode(chip, port, egress);
	if (err)
		return err;

	err = chip->info->ops->port_set_frame_mode(chip, port, frame);
	if (err)
		return err;

	if (chip->info->ops->port_set_ether_type)
		return chip->info->ops->port_set_ether_type(chip, port, etype);

	return 0;
}

static int mv88e6xxx_set_port_mode_normal(struct mv88e6xxx_chip *chip, int port)
{
	return mv88e6xxx_set_port_mode(chip, port, MV88E6XXX_FRAME_MODE_NORMAL,
				       MV88E6XXX_EGRESS_MODE_UNMODIFIED,
				       MV88E6XXX_PORT_ETH_TYPE_DEFAULT);
}

static int mv88e6xxx_set_port_mode_dsa(struct mv88e6xxx_chip *chip, int port)
{
	return mv88e6xxx_set_port_mode(chip, port, MV88E6XXX_FRAME_MODE_DSA,
				       MV88E6XXX_EGRESS_MODE_UNMODIFIED,
				       MV88E6XXX_PORT_ETH_TYPE_DEFAULT);
}

static int mv88e6xxx_set_port_mode_edsa(struct mv88e6xxx_chip *chip, int port)
{
	return mv88e6xxx_set_port_mode(chip, port,
				       MV88E6XXX_FRAME_MODE_ETHERTYPE,
				       MV88E6XXX_EGRESS_MODE_ETHERTYPE,
				       ETH_P_EDSA);
}

static int mv88e6xxx_setup_port_mode(struct mv88e6xxx_chip *chip, int port)
{
	if (dsa_is_dsa_port(chip->ds, port))
		return mv88e6xxx_set_port_mode_dsa(chip, port);

	if (dsa_is_user_port(chip->ds, port))
		return mv88e6xxx_set_port_mode_normal(chip, port);

	/* Setup CPU port mode depending on its supported tag format */
	if (chip->tag_protocol == DSA_TAG_PROTO_DSA)
		return mv88e6xxx_set_port_mode_dsa(chip, port);

	if (chip->tag_protocol == DSA_TAG_PROTO_EDSA)
		return mv88e6xxx_set_port_mode_edsa(chip, port);

	return -EINVAL;
}

static int mv88e6xxx_setup_message_port(struct mv88e6xxx_chip *chip, int port)
{
	bool message = dsa_is_dsa_port(chip->ds, port);

	return mv88e6xxx_port_set_message_port(chip, port, message);
}

static int mv88e6xxx_setup_egress_floods(struct mv88e6xxx_chip *chip, int port)
{
	int err;

	if (chip->info->ops->port_set_ucast_flood) {
		err = chip->info->ops->port_set_ucast_flood(chip, port, true);
		if (err)
			return err;
	}
	if (chip->info->ops->port_set_mcast_flood) {
		err = chip->info->ops->port_set_mcast_flood(chip, port, true);
		if (err)
			return err;
	}

	return 0;
}

static irqreturn_t mv88e6xxx_serdes_irq_thread_fn(int irq, void *dev_id)
{
	struct mv88e6xxx_port *mvp = dev_id;
	struct mv88e6xxx_chip *chip = mvp->chip;
	irqreturn_t ret = IRQ_NONE;
	int port = mvp->port;
	int lane;

	mv88e6xxx_reg_lock(chip);
	lane = mv88e6xxx_serdes_get_lane(chip, port);
	if (lane >= 0)
		ret = mv88e6xxx_serdes_irq_status(chip, port, lane);
	mv88e6xxx_reg_unlock(chip);

	return ret;
}

static int mv88e6xxx_serdes_irq_request(struct mv88e6xxx_chip *chip, int port,
					int lane)
{
	struct mv88e6xxx_port *dev_id = &chip->ports[port];
	unsigned int irq;
	int err;

	/* Nothing to request if this SERDES port has no IRQ */
	irq = mv88e6xxx_serdes_irq_mapping(chip, port);
	if (!irq)
		return 0;

	snprintf(dev_id->serdes_irq_name, sizeof(dev_id->serdes_irq_name),
		 "mv88e6xxx-%s-serdes-%d", dev_name(chip->dev), port);

	/* Requesting the IRQ will trigger IRQ callbacks, so release the lock */
	mv88e6xxx_reg_unlock(chip);
	err = request_threaded_irq(irq, NULL, mv88e6xxx_serdes_irq_thread_fn,
				   IRQF_ONESHOT, dev_id->serdes_irq_name,
				   dev_id);
	mv88e6xxx_reg_lock(chip);
	if (err)
		return err;

	dev_id->serdes_irq = irq;

	return mv88e6xxx_serdes_irq_enable(chip, port, lane);
}

static int mv88e6xxx_serdes_irq_free(struct mv88e6xxx_chip *chip, int port,
				     int lane)
{
	struct mv88e6xxx_port *dev_id = &chip->ports[port];
	unsigned int irq = dev_id->serdes_irq;
	int err;

	/* Nothing to free if no IRQ has been requested */
	if (!irq)
		return 0;

	err = mv88e6xxx_serdes_irq_disable(chip, port, lane);

	/* Freeing the IRQ will trigger IRQ callbacks, so release the lock */
	mv88e6xxx_reg_unlock(chip);
	free_irq(irq, dev_id);
	mv88e6xxx_reg_lock(chip);

	dev_id->serdes_irq = 0;

	return err;
}

static int mv88e6xxx_serdes_power(struct mv88e6xxx_chip *chip, int port,
				  bool on)
{
	int lane;
	int err;

	lane = mv88e6xxx_serdes_get_lane(chip, port);
	if (lane < 0)
		return 0;

	if (on) {
		err = mv88e6xxx_serdes_power_up(chip, port, lane);
		if (err)
			return err;

		err = mv88e6xxx_serdes_irq_request(chip, port, lane);
	} else {
		err = mv88e6xxx_serdes_irq_free(chip, port, lane);
		if (err)
			return err;

		err = mv88e6xxx_serdes_power_down(chip, port, lane);
	}

	return err;
}

static int mv88e6xxx_set_egress_port(struct mv88e6xxx_chip *chip,
				     enum mv88e6xxx_egress_direction direction,
				     int port)
{
	int err;

	if (!chip->info->ops->set_egress_port)
		return -EOPNOTSUPP;

	err = chip->info->ops->set_egress_port(chip, direction, port);
	if (err)
		return err;

	if (direction == MV88E6XXX_EGRESS_DIR_INGRESS)
		chip->ingress_dest_port = port;
	else
		chip->egress_dest_port = port;

	return 0;
}

static int mv88e6xxx_setup_upstream_port(struct mv88e6xxx_chip *chip, int port)
{
	struct dsa_switch *ds = chip->ds;
	int upstream_port;
	int err;

	upstream_port = dsa_upstream_port(ds, port);
	if (chip->info->ops->port_set_upstream_port) {
		err = chip->info->ops->port_set_upstream_port(chip, port,
							      upstream_port);
		if (err)
			return err;
	}

	if (port == upstream_port) {
		if (chip->info->ops->set_cpu_port) {
			err = chip->info->ops->set_cpu_port(chip,
							    upstream_port);
			if (err)
				return err;
		}

		err = mv88e6xxx_set_egress_port(chip,
						MV88E6XXX_EGRESS_DIR_INGRESS,
						upstream_port);
		if (err && err != -EOPNOTSUPP)
			return err;

		err = mv88e6xxx_set_egress_port(chip,
						MV88E6XXX_EGRESS_DIR_EGRESS,
						upstream_port);
		if (err && err != -EOPNOTSUPP)
			return err;
	}

	return 0;
}

static int mv88e6xxx_setup_port(struct mv88e6xxx_chip *chip, int port)
{
	struct device_node *phy_handle = NULL;
	struct dsa_switch *ds = chip->ds;
	phy_interface_t mode;
	struct dsa_port *dp;
	int tx_amp, speed;
	int err;
	u16 reg;

	chip->ports[port].chip = chip;
	chip->ports[port].port = port;

	dp = dsa_to_port(ds, port);

	/* MAC Forcing register: don't force link, speed, duplex or flow control
	 * state to any particular values on physical ports, but force the CPU
	 * port and all DSA ports to their maximum bandwidth and full duplex.
	 */
	if (dsa_is_cpu_port(ds, port) || dsa_is_dsa_port(ds, port)) {
		struct phylink_config pl_config = {};
		unsigned long caps;

		chip->info->ops->phylink_get_caps(chip, port, &pl_config);

		caps = pl_config.mac_capabilities;

		if (chip->info->ops->port_max_speed_mode)
			mode = chip->info->ops->port_max_speed_mode(chip, port);
		else
			mode = PHY_INTERFACE_MODE_NA;

		if (caps & MAC_10000FD)
			speed = SPEED_10000;
		else if (caps & MAC_5000FD)
			speed = SPEED_5000;
		else if (caps & MAC_2500FD)
			speed = SPEED_2500;
		else if (caps & MAC_1000)
			speed = SPEED_1000;
		else if (caps & MAC_100)
			speed = SPEED_100;
		else
			speed = SPEED_10;

		err = mv88e6xxx_port_setup_mac(chip, port, LINK_FORCED_UP,
					       speed, DUPLEX_FULL,
					       PAUSE_OFF, mode);
	} else {
		err = mv88e6xxx_port_setup_mac(chip, port, LINK_UNFORCED,
					       SPEED_UNFORCED, DUPLEX_UNFORCED,
					       PAUSE_ON,
					       PHY_INTERFACE_MODE_NA);
	}
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
	reg = MV88E6185_PORT_CTL0_USE_TAG | MV88E6185_PORT_CTL0_USE_IP |
		MV88E6XXX_PORT_CTL0_STATE_FORWARDING;
	/* Forward any IPv4 IGMP or IPv6 MLD frames received
	 * by a USER port to the CPU port to allow snooping.
	 */
	if (dsa_is_user_port(ds, port))
		reg |= MV88E6XXX_PORT_CTL0_IGMP_MLD_SNOOP;

	err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_CTL0, reg);
	if (err)
		return err;

	err = mv88e6xxx_setup_port_mode(chip, port);
	if (err)
		return err;

	err = mv88e6xxx_setup_egress_floods(chip, port);
	if (err)
		return err;

	/* Port Control 2: don't force a good FCS, set the MTU size to
	 * 10222 bytes, disable 802.1q tags checking, don't discard
	 * tagged or untagged frames on this port, skip destination
	 * address lookup on user ports, disable ARP mirroring and don't
	 * send a copy of all transmitted/received frames on this port
	 * to the CPU.
	 */
	err = mv88e6xxx_port_set_map_da(chip, port, !dsa_is_user_port(ds, port));
	if (err)
		return err;

	err = mv88e6xxx_setup_upstream_port(chip, port);
	if (err)
		return err;

	/* On chips that support it, set all downstream DSA ports'
	 * VLAN policy to TRAP. In combination with loading
	 * MV88E6XXX_VID_STANDALONE as a policy entry in the VTU, this
	 * provides a better isolation barrier between standalone
	 * ports, as the ATU is bypassed on any intermediate switches
	 * between the incoming port and the CPU.
	 */
	if (dsa_is_downstream_port(ds, port) &&
	    chip->info->ops->port_set_policy) {
		err = chip->info->ops->port_set_policy(chip, port,
						MV88E6XXX_POLICY_MAPPING_VTU,
						MV88E6XXX_POLICY_ACTION_TRAP);
		if (err)
			return err;
	}

	/* User ports start out in standalone mode and 802.1Q is
	 * therefore disabled. On DSA ports, all valid VIDs are always
	 * loaded in the VTU - therefore, enable 802.1Q in order to take
	 * advantage of VLAN policy on chips that supports it.
	 */
	err = mv88e6xxx_port_set_8021q_mode(chip, port,
				dsa_is_user_port(ds, port) ?
				MV88E6XXX_PORT_CTL2_8021Q_MODE_DISABLED :
				MV88E6XXX_PORT_CTL2_8021Q_MODE_SECURE);
	if (err)
		return err;

	/* Bind MV88E6XXX_VID_STANDALONE to MV88E6XXX_FID_STANDALONE by
	 * virtue of the fact that mv88e6xxx_atu_new() will pick it as
	 * the first free FID. This will be used as the private PVID for
	 * unbridged ports. Shared (DSA and CPU) ports must also be
	 * members of this VID, in order to trap all frames assigned to
	 * it to the CPU.
	 */
	err = mv88e6xxx_port_vlan_join(chip, port, MV88E6XXX_VID_STANDALONE,
				       MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_UNMODIFIED,
				       false);
	if (err)
		return err;

	/* Associate MV88E6XXX_VID_BRIDGED with MV88E6XXX_FID_BRIDGED in the
	 * ATU by virtue of the fact that mv88e6xxx_atu_new() will pick it as
	 * the first free FID after MV88E6XXX_FID_STANDALONE. This will be used
	 * as the private PVID on ports under a VLAN-unaware bridge.
	 * Shared (DSA and CPU) ports must also be members of it, to translate
	 * the VID from the DSA tag into MV88E6XXX_FID_BRIDGED, instead of
	 * relying on their port default FID.
	 */
	err = mv88e6xxx_port_vlan_join(chip, port, MV88E6XXX_VID_BRIDGED,
				       MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_UNMODIFIED,
				       false);
	if (err)
		return err;

	if (chip->info->ops->port_set_jumbo_size) {
		err = chip->info->ops->port_set_jumbo_size(chip, port, 10218);
		if (err)
			return err;
	}

	/* Port Association Vector: disable automatic address learning
	 * on all user ports since they start out in standalone
	 * mode. When joining a bridge, learning will be configured to
	 * match the bridge port settings. Enable learning on all
	 * DSA/CPU ports. NOTE: FROM_CPU frames always bypass the
	 * learning process.
	 *
	 * Disable HoldAt1, IntOnAgeOut, LockedPort, IgnoreWrongData,
	 * and RefreshLocked. I.e. setup standard automatic learning.
	 */
	if (dsa_is_user_port(ds, port))
		reg = 0;
	else
		reg = 1 << port;

	err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_ASSOC_VECTOR,
				   reg);
	if (err)
		return err;

	/* Egress rate control 2: disable egress rate control. */
	err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_EGRESS_RATE_CTL2,
				   0x0000);
	if (err)
		return err;

	if (chip->info->ops->port_pause_limit) {
		err = chip->info->ops->port_pause_limit(chip, port, 0, 0);
		if (err)
			return err;
	}

	if (chip->info->ops->port_disable_learn_limit) {
		err = chip->info->ops->port_disable_learn_limit(chip, port);
		if (err)
			return err;
	}

	if (chip->info->ops->port_disable_pri_override) {
		err = chip->info->ops->port_disable_pri_override(chip, port);
		if (err)
			return err;
	}

	if (chip->info->ops->port_tag_remap) {
		err = chip->info->ops->port_tag_remap(chip, port);
		if (err)
			return err;
	}

	if (chip->info->ops->port_egress_rate_limiting) {
		err = chip->info->ops->port_egress_rate_limiting(chip, port);
		if (err)
			return err;
	}

	if (chip->info->ops->port_setup_message_port) {
		err = chip->info->ops->port_setup_message_port(chip, port);
		if (err)
			return err;
	}

	if (chip->info->ops->serdes_set_tx_amplitude) {
		if (dp)
			phy_handle = of_parse_phandle(dp->dn, "phy-handle", 0);

		if (phy_handle && !of_property_read_u32(phy_handle,
							"tx-p2p-microvolt",
							&tx_amp))
			err = chip->info->ops->serdes_set_tx_amplitude(chip,
								port, tx_amp);
		if (phy_handle) {
			of_node_put(phy_handle);
			if (err)
				return err;
		}
	}

	/* Port based VLAN map: give each port the same default address
	 * database, and allow bidirectional communication between the
	 * CPU and DSA port(s), and the other ports.
	 */
	err = mv88e6xxx_port_set_fid(chip, port, MV88E6XXX_FID_STANDALONE);
	if (err)
		return err;

	err = mv88e6xxx_port_vlan_map(chip, port);
	if (err)
		return err;

	/* Default VLAN ID and priority: don't set a default VLAN
	 * ID, and set the default packet priority to zero.
	 */
	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_DEFAULT_VLAN, 0);
}

static int mv88e6xxx_get_max_mtu(struct dsa_switch *ds, int port)
{
	struct mv88e6xxx_chip *chip = ds->priv;

	if (chip->info->ops->port_set_jumbo_size)
		return 10240 - VLAN_ETH_HLEN - EDSA_HLEN - ETH_FCS_LEN;
	else if (chip->info->ops->set_max_frame_size)
		return 1632 - VLAN_ETH_HLEN - EDSA_HLEN - ETH_FCS_LEN;
	return ETH_DATA_LEN;
}

static int mv88e6xxx_change_mtu(struct dsa_switch *ds, int port, int new_mtu)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int ret = 0;

	/* For families where we don't know how to alter the MTU,
	 * just accept any value up to ETH_DATA_LEN
	 */
	if (!chip->info->ops->port_set_jumbo_size &&
	    !chip->info->ops->set_max_frame_size) {
		if (new_mtu > ETH_DATA_LEN)
			return -EINVAL;

		return 0;
	}

	if (dsa_is_dsa_port(ds, port) || dsa_is_cpu_port(ds, port))
		new_mtu += EDSA_HLEN;

	mv88e6xxx_reg_lock(chip);
	if (chip->info->ops->port_set_jumbo_size)
		ret = chip->info->ops->port_set_jumbo_size(chip, port, new_mtu);
	else if (chip->info->ops->set_max_frame_size)
		ret = chip->info->ops->set_max_frame_size(chip, new_mtu);
	mv88e6xxx_reg_unlock(chip);

	return ret;
}

static int mv88e6xxx_port_enable(struct dsa_switch *ds, int port,
				 struct phy_device *phydev)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_serdes_power(chip, port, true);
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static void mv88e6xxx_port_disable(struct dsa_switch *ds, int port)
{
	struct mv88e6xxx_chip *chip = ds->priv;

	mv88e6xxx_reg_lock(chip);
	if (mv88e6xxx_serdes_power(chip, port, false))
		dev_err(chip->dev, "failed to power off SERDES\n");
	mv88e6xxx_reg_unlock(chip);
}

static int mv88e6xxx_set_ageing_time(struct dsa_switch *ds,
				     unsigned int ageing_time)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_g1_atu_set_age_time(chip, ageing_time);
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static int mv88e6xxx_stats_setup(struct mv88e6xxx_chip *chip)
{
	int err;

	/* Initialize the statistics unit */
	if (chip->info->ops->stats_set_histogram) {
		err = chip->info->ops->stats_set_histogram(chip);
		if (err)
			return err;
	}

	return mv88e6xxx_g1_stats_clear(chip);
}

/* Check if the errata has already been applied. */
static bool mv88e6390_setup_errata_applied(struct mv88e6xxx_chip *chip)
{
	int port;
	int err;
	u16 val;

	for (port = 0; port < mv88e6xxx_num_ports(chip); port++) {
		err = mv88e6xxx_port_hidden_read(chip, 0xf, port, 0, &val);
		if (err) {
			dev_err(chip->dev,
				"Error reading hidden register: %d\n", err);
			return false;
		}
		if (val != 0x01c0)
			return false;
	}

	return true;
}

/* The 6390 copper ports have an errata which require poking magic
 * values into undocumented hidden registers and then performing a
 * software reset.
 */
static int mv88e6390_setup_errata(struct mv88e6xxx_chip *chip)
{
	int port;
	int err;

	if (mv88e6390_setup_errata_applied(chip))
		return 0;

	/* Set the ports into blocking mode */
	for (port = 0; port < mv88e6xxx_num_ports(chip); port++) {
		err = mv88e6xxx_port_set_state(chip, port, BR_STATE_DISABLED);
		if (err)
			return err;
	}

	for (port = 0; port < mv88e6xxx_num_ports(chip); port++) {
		err = mv88e6xxx_port_hidden_write(chip, 0xf, port, 0, 0x01c0);
		if (err)
			return err;
	}

	return mv88e6xxx_software_reset(chip);
}

/* prod_id for switch families which do not have a PHY model number */
static const u16 family_prod_id_table[] = {
	[MV88E6XXX_FAMILY_6341] = MV88E6XXX_PORT_SWITCH_ID_PROD_6341,
	[MV88E6XXX_FAMILY_6390] = MV88E6XXX_PORT_SWITCH_ID_PROD_6390,
	[MV88E6XXX_FAMILY_6393] = MV88E6XXX_PORT_SWITCH_ID_PROD_6393X,
};

static int mv88e6xxx_mdio_read(struct mii_bus *bus, int phy, int reg)
{
	struct mv88e6xxx_mdio_bus *mdio_bus = bus->priv;
	struct mv88e6xxx_chip *chip = mdio_bus->chip;
	u16 prod_id;
	u16 val;
	int err;

	if (!chip->info->ops->phy_read)
		return -EOPNOTSUPP;

	mv88e6xxx_reg_lock(chip);
	err = chip->info->ops->phy_read(chip, bus, phy, reg, &val);
	mv88e6xxx_reg_unlock(chip);

	/* Some internal PHYs don't have a model number. */
	if (reg == MII_PHYSID2 && !(val & 0x3f0) &&
	    chip->info->family < ARRAY_SIZE(family_prod_id_table)) {
		prod_id = family_prod_id_table[chip->info->family];
		if (prod_id)
			val |= prod_id >> 4;
	}

	return err ? err : val;
}

static int mv88e6xxx_mdio_read_c45(struct mii_bus *bus, int phy, int devad,
				   int reg)
{
	struct mv88e6xxx_mdio_bus *mdio_bus = bus->priv;
	struct mv88e6xxx_chip *chip = mdio_bus->chip;
	u16 val;
	int err;

	if (!chip->info->ops->phy_read_c45)
		return -EOPNOTSUPP;

	mv88e6xxx_reg_lock(chip);
	err = chip->info->ops->phy_read_c45(chip, bus, phy, devad, reg, &val);
	mv88e6xxx_reg_unlock(chip);

	return err ? err : val;
}

static int mv88e6xxx_mdio_write(struct mii_bus *bus, int phy, int reg, u16 val)
{
	struct mv88e6xxx_mdio_bus *mdio_bus = bus->priv;
	struct mv88e6xxx_chip *chip = mdio_bus->chip;
	int err;

	if (!chip->info->ops->phy_write)
		return -EOPNOTSUPP;

	mv88e6xxx_reg_lock(chip);
	err = chip->info->ops->phy_write(chip, bus, phy, reg, val);
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static int mv88e6xxx_mdio_write_c45(struct mii_bus *bus, int phy, int devad,
				    int reg, u16 val)
{
	struct mv88e6xxx_mdio_bus *mdio_bus = bus->priv;
	struct mv88e6xxx_chip *chip = mdio_bus->chip;
	int err;

	if (!chip->info->ops->phy_write_c45)
		return -EOPNOTSUPP;

	mv88e6xxx_reg_lock(chip);
	err = chip->info->ops->phy_write_c45(chip, bus, phy, devad, reg, val);
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static int mv88e6xxx_mdio_register(struct mv88e6xxx_chip *chip,
				   struct device_node *np,
				   bool external)
{
	static int index;
	struct mv88e6xxx_mdio_bus *mdio_bus;
	struct mii_bus *bus;
	int err;

	if (external) {
		mv88e6xxx_reg_lock(chip);
		err = mv88e6xxx_g2_scratch_gpio_set_smi(chip, true);
		mv88e6xxx_reg_unlock(chip);

		if (err)
			return err;
	}

	bus = mdiobus_alloc_size(sizeof(*mdio_bus));
	if (!bus)
		return -ENOMEM;

	mdio_bus = bus->priv;
	mdio_bus->bus = bus;
	mdio_bus->chip = chip;
	INIT_LIST_HEAD(&mdio_bus->list);
	mdio_bus->external = external;

	if (np) {
		bus->name = np->full_name;
		snprintf(bus->id, MII_BUS_ID_SIZE, "%pOF", np);
	} else {
		bus->name = "mv88e6xxx SMI";
		snprintf(bus->id, MII_BUS_ID_SIZE, "mv88e6xxx-%d", index++);
	}

	bus->read = mv88e6xxx_mdio_read;
	bus->write = mv88e6xxx_mdio_write;
	bus->read_c45 = mv88e6xxx_mdio_read_c45;
	bus->write_c45 = mv88e6xxx_mdio_write_c45;
	bus->parent = chip->dev;
	bus->phy_mask = ~GENMASK(chip->info->phy_base_addr +
				 mv88e6xxx_num_ports(chip) - 1,
				 chip->info->phy_base_addr);

	if (!external) {
		err = mv88e6xxx_g2_irq_mdio_setup(chip, bus);
		if (err)
			goto out;
	}

	err = of_mdiobus_register(bus, np);
	if (err) {
		dev_err(chip->dev, "Cannot register MDIO bus (%d)\n", err);
		mv88e6xxx_g2_irq_mdio_free(chip, bus);
		goto out;
	}

	if (external)
		list_add_tail(&mdio_bus->list, &chip->mdios);
	else
		list_add(&mdio_bus->list, &chip->mdios);

	return 0;

out:
	mdiobus_free(bus);
	return err;
}

static void mv88e6xxx_mdios_unregister(struct mv88e6xxx_chip *chip)

{
	struct mv88e6xxx_mdio_bus *mdio_bus, *p;
	struct mii_bus *bus;

	list_for_each_entry_safe(mdio_bus, p, &chip->mdios, list) {
		bus = mdio_bus->bus;

		if (!mdio_bus->external)
			mv88e6xxx_g2_irq_mdio_free(chip, bus);

		mdiobus_unregister(bus);
		mdiobus_free(bus);
	}
}

static int mv88e6xxx_mdios_register(struct mv88e6xxx_chip *chip)
{
	struct device_node *np = chip->dev->of_node;
	struct device_node *child;
	int err;

	/* Always register one mdio bus for the internal/default mdio
	 * bus. This maybe represented in the device tree, but is
	 * optional.
	 */
	child = of_get_child_by_name(np, "mdio");
	err = mv88e6xxx_mdio_register(chip, child, false);
	of_node_put(child);
	if (err)
		return err;

	/* Walk the device tree, and see if there are any other nodes
	 * which say they are compatible with the external mdio
	 * bus.
	 */
	for_each_available_child_of_node(np, child) {
		if (of_device_is_compatible(
			    child, "marvell,mv88e6xxx-mdio-external")) {
			err = mv88e6xxx_mdio_register(chip, child, true);
			if (err) {
				mv88e6xxx_mdios_unregister(chip);
				of_node_put(child);
				return err;
			}
		}
	}

	return 0;
}

static void mv88e6xxx_teardown(struct dsa_switch *ds)
{
	struct mv88e6xxx_chip *chip = ds->priv;

	mv88e6xxx_teardown_devlink_params(ds);
	dsa_devlink_resources_unregister(ds);
	mv88e6xxx_teardown_devlink_regions_global(ds);
	mv88e6xxx_mdios_unregister(chip);
}

static int mv88e6xxx_setup(struct dsa_switch *ds)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	u8 cmode;
	int err;
	int i;

	err = mv88e6xxx_mdios_register(chip);
	if (err)
		return err;

	chip->ds = ds;
	ds->slave_mii_bus = mv88e6xxx_default_mdio_bus(chip);

	/* Since virtual bridges are mapped in the PVT, the number we support
	 * depends on the physical switch topology. We need to let DSA figure
	 * that out and therefore we cannot set this at dsa_register_switch()
	 * time.
	 */
	if (mv88e6xxx_has_pvt(chip))
		ds->max_num_bridges = MV88E6XXX_MAX_PVT_SWITCHES -
				      ds->dst->last_switch - 1;

	mv88e6xxx_reg_lock(chip);

	if (chip->info->ops->setup_errata) {
		err = chip->info->ops->setup_errata(chip);
		if (err)
			goto unlock;
	}

	/* Cache the cmode of each port. */
	for (i = 0; i < mv88e6xxx_num_ports(chip); i++) {
		if (chip->info->ops->port_get_cmode) {
			err = chip->info->ops->port_get_cmode(chip, i, &cmode);
			if (err)
				goto unlock;

			chip->ports[i].cmode = cmode;
		}
	}

	err = mv88e6xxx_vtu_setup(chip);
	if (err)
		goto unlock;

	/* Must be called after mv88e6xxx_vtu_setup (which flushes the
	 * VTU, thereby also flushing the STU).
	 */
	err = mv88e6xxx_stu_setup(chip);
	if (err)
		goto unlock;

	/* Setup Switch Port Registers */
	for (i = 0; i < mv88e6xxx_num_ports(chip); i++) {
		if (dsa_is_unused_port(ds, i))
			continue;

		/* Prevent the use of an invalid port. */
		if (mv88e6xxx_is_invalid_port(chip, i)) {
			dev_err(chip->dev, "port %d is invalid\n", i);
			err = -EINVAL;
			goto unlock;
		}

		err = mv88e6xxx_setup_port(chip, i);
		if (err)
			goto unlock;
	}

	err = mv88e6xxx_irl_setup(chip);
	if (err)
		goto unlock;

	err = mv88e6xxx_mac_setup(chip);
	if (err)
		goto unlock;

	err = mv88e6xxx_phy_setup(chip);
	if (err)
		goto unlock;

	err = mv88e6xxx_pvt_setup(chip);
	if (err)
		goto unlock;

	err = mv88e6xxx_atu_setup(chip);
	if (err)
		goto unlock;

	err = mv88e6xxx_broadcast_setup(chip, 0);
	if (err)
		goto unlock;

	err = mv88e6xxx_pot_setup(chip);
	if (err)
		goto unlock;

	err = mv88e6xxx_rmu_setup(chip);
	if (err)
		goto unlock;

	err = mv88e6xxx_rsvd2cpu_setup(chip);
	if (err)
		goto unlock;

	err = mv88e6xxx_trunk_setup(chip);
	if (err)
		goto unlock;

	err = mv88e6xxx_devmap_setup(chip);
	if (err)
		goto unlock;

	err = mv88e6xxx_pri_setup(chip);
	if (err)
		goto unlock;

	/* Setup PTP Hardware Clock and timestamping */
	if (chip->info->ptp_support) {
		err = mv88e6xxx_ptp_setup(chip);
		if (err)
			goto unlock;

		err = mv88e6xxx_hwtstamp_setup(chip);
		if (err)
			goto unlock;
	}

	err = mv88e6xxx_stats_setup(chip);
	if (err)
		goto unlock;

unlock:
	mv88e6xxx_reg_unlock(chip);

	if (err)
		goto out_mdios;

	/* Have to be called without holding the register lock, since
	 * they take the devlink lock, and we later take the locks in
	 * the reverse order when getting/setting parameters or
	 * resource occupancy.
	 */
	err = mv88e6xxx_setup_devlink_resources(ds);
	if (err)
		goto out_mdios;

	err = mv88e6xxx_setup_devlink_params(ds);
	if (err)
		goto out_resources;

	err = mv88e6xxx_setup_devlink_regions_global(ds);
	if (err)
		goto out_params;

	return 0;

out_params:
	mv88e6xxx_teardown_devlink_params(ds);
out_resources:
	dsa_devlink_resources_unregister(ds);
out_mdios:
	mv88e6xxx_mdios_unregister(chip);

	return err;
}

static int mv88e6xxx_port_setup(struct dsa_switch *ds, int port)
{
	return mv88e6xxx_setup_devlink_regions_port(ds, port);
}

static void mv88e6xxx_port_teardown(struct dsa_switch *ds, int port)
{
	mv88e6xxx_teardown_devlink_regions_port(ds, port);
}

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

	mv88e6xxx_reg_lock(chip);
	err = chip->info->ops->get_eeprom(chip, eeprom, data);
	mv88e6xxx_reg_unlock(chip);

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

	mv88e6xxx_reg_lock(chip);
	err = chip->info->ops->set_eeprom(chip, eeprom, data);
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static const struct mv88e6xxx_ops mv88e6085_ops = {
	/* MV88E6XXX_FAMILY_6097 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.set_switch_mac = mv88e6xxx_g1_set_switch_mac,
	.phy_read = mv88e6185_phy_ppu_read,
	.phy_write = mv88e6185_phy_ppu_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_speed_duplex = mv88e6185_port_set_speed_duplex,
	.port_tag_remap = mv88e6095_port_tag_remap,
	.port_set_policy = mv88e6352_port_set_policy,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6097_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6185_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6xxx_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6095_stats_get_sset_count,
	.stats_get_strings = mv88e6095_stats_get_strings,
	.stats_get_stats = mv88e6095_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6097_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6352_g2_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.ppu_enable = mv88e6185_g1_ppu_enable,
	.ppu_disable = mv88e6185_g1_ppu_disable,
	.reset = mv88e6185_g1_reset,
	.rmu_disable = mv88e6085_g1_rmu_disable,
	.vtu_getnext = mv88e6352_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6352_g1_vtu_loadpurge,
	.stu_getnext = mv88e6352_g1_stu_getnext,
	.stu_loadpurge = mv88e6352_g1_stu_loadpurge,
	.phylink_get_caps = mv88e6185_phylink_get_caps,
	.set_max_frame_size = mv88e6185_g1_set_max_frame_size,
};

static const struct mv88e6xxx_ops mv88e6095_ops = {
	/* MV88E6XXX_FAMILY_6095 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.set_switch_mac = mv88e6xxx_g1_set_switch_mac,
	.phy_read = mv88e6185_phy_ppu_read,
	.phy_write = mv88e6185_phy_ppu_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6185_port_sync_link,
	.port_set_speed_duplex = mv88e6185_port_set_speed_duplex,
	.port_set_frame_mode = mv88e6085_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6185_port_set_forward_unknown,
	.port_set_mcast_flood = mv88e6185_port_set_default_forward,
	.port_set_upstream_port = mv88e6095_port_set_upstream_port,
	.port_get_cmode = mv88e6185_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6xxx_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6095_stats_get_sset_count,
	.stats_get_strings = mv88e6095_stats_get_strings,
	.stats_get_stats = mv88e6095_stats_get_stats,
	.mgmt_rsvd2cpu = mv88e6185_g2_mgmt_rsvd2cpu,
	.serdes_power = mv88e6185_serdes_power,
	.serdes_get_lane = mv88e6185_serdes_get_lane,
	.serdes_pcs_get_state = mv88e6185_serdes_pcs_get_state,
	.ppu_enable = mv88e6185_g1_ppu_enable,
	.ppu_disable = mv88e6185_g1_ppu_disable,
	.reset = mv88e6185_g1_reset,
	.vtu_getnext = mv88e6185_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6185_g1_vtu_loadpurge,
	.phylink_get_caps = mv88e6095_phylink_get_caps,
	.set_max_frame_size = mv88e6185_g1_set_max_frame_size,
};

static const struct mv88e6xxx_ops mv88e6097_ops = {
	/* MV88E6XXX_FAMILY_6097 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6185_port_sync_link,
	.port_set_speed_duplex = mv88e6185_port_set_speed_duplex,
	.port_tag_remap = mv88e6095_port_tag_remap,
	.port_set_policy = mv88e6352_port_set_policy,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_egress_rate_limiting = mv88e6095_port_egress_rate_limiting,
	.port_pause_limit = mv88e6097_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6185_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6xxx_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6095_stats_get_sset_count,
	.stats_get_strings = mv88e6095_stats_get_strings,
	.stats_get_stats = mv88e6095_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6097_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6352_g2_mgmt_rsvd2cpu,
	.serdes_power = mv88e6185_serdes_power,
	.serdes_get_lane = mv88e6185_serdes_get_lane,
	.serdes_pcs_get_state = mv88e6185_serdes_pcs_get_state,
	.serdes_irq_mapping = mv88e6390_serdes_irq_mapping,
	.serdes_irq_enable = mv88e6097_serdes_irq_enable,
	.serdes_irq_status = mv88e6097_serdes_irq_status,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.rmu_disable = mv88e6085_g1_rmu_disable,
	.vtu_getnext = mv88e6352_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6352_g1_vtu_loadpurge,
	.phylink_get_caps = mv88e6095_phylink_get_caps,
	.stu_getnext = mv88e6352_g1_stu_getnext,
	.stu_loadpurge = mv88e6352_g1_stu_loadpurge,
	.set_max_frame_size = mv88e6185_g1_set_max_frame_size,
};

static const struct mv88e6xxx_ops mv88e6123_ops = {
	/* MV88E6XXX_FAMILY_6165 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_speed_duplex = mv88e6185_port_set_speed_duplex,
	.port_set_frame_mode = mv88e6085_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6185_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6320_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6095_stats_get_sset_count,
	.stats_get_strings = mv88e6095_stats_get_strings,
	.stats_get_stats = mv88e6095_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6097_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6352_g2_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6352_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6352_g1_vtu_loadpurge,
	.stu_getnext = mv88e6352_g1_stu_getnext,
	.stu_loadpurge = mv88e6352_g1_stu_loadpurge,
	.phylink_get_caps = mv88e6185_phylink_get_caps,
	.set_max_frame_size = mv88e6185_g1_set_max_frame_size,
};

static const struct mv88e6xxx_ops mv88e6131_ops = {
	/* MV88E6XXX_FAMILY_6185 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.set_switch_mac = mv88e6xxx_g1_set_switch_mac,
	.phy_read = mv88e6185_phy_ppu_read,
	.phy_write = mv88e6185_phy_ppu_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_speed_duplex = mv88e6185_port_set_speed_duplex,
	.port_tag_remap = mv88e6095_port_tag_remap,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6185_port_set_forward_unknown,
	.port_set_mcast_flood = mv88e6185_port_set_default_forward,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_set_upstream_port = mv88e6095_port_set_upstream_port,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6097_port_pause_limit,
	.port_set_pause = mv88e6185_port_set_pause,
	.port_get_cmode = mv88e6185_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6xxx_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6095_stats_get_sset_count,
	.stats_get_strings = mv88e6095_stats_get_strings,
	.stats_get_stats = mv88e6095_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6097_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6185_g2_mgmt_rsvd2cpu,
	.ppu_enable = mv88e6185_g1_ppu_enable,
	.set_cascade_port = mv88e6185_g1_set_cascade_port,
	.ppu_disable = mv88e6185_g1_ppu_disable,
	.reset = mv88e6185_g1_reset,
	.vtu_getnext = mv88e6185_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6185_g1_vtu_loadpurge,
	.phylink_get_caps = mv88e6185_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6141_ops = {
	/* MV88E6XXX_FAMILY_6341 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.get_eeprom = mv88e6xxx_g2_get_eeprom8,
	.set_eeprom = mv88e6xxx_g2_set_eeprom8,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6390_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6341_port_set_speed_duplex,
	.port_max_speed_mode = mv88e6341_port_max_speed_mode,
	.port_tag_remap = mv88e6095_port_tag_remap,
	.port_set_policy = mv88e6352_port_set_policy,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6097_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_set_cmode = mv88e6341_port_set_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6390_g1_stats_snapshot,
	.stats_set_histogram = mv88e6390_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6320_stats_get_sset_count,
	.stats_get_strings = mv88e6320_stats_get_strings,
	.stats_get_stats = mv88e6390_stats_get_stats,
	.set_cpu_port = mv88e6390_g1_set_cpu_port,
	.set_egress_port = mv88e6390_g1_set_egress_port,
	.watchdog_ops = &mv88e6390_watchdog_ops,
	.mgmt_rsvd2cpu =  mv88e6390_g1_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.rmu_disable = mv88e6390_g1_rmu_disable,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6352_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6352_g1_vtu_loadpurge,
	.stu_getnext = mv88e6352_g1_stu_getnext,
	.stu_loadpurge = mv88e6352_g1_stu_loadpurge,
	.serdes_power = mv88e6390_serdes_power,
	.serdes_get_lane = mv88e6341_serdes_get_lane,
	/* Check status register pause & lpa register */
	.serdes_pcs_get_state = mv88e6390_serdes_pcs_get_state,
	.serdes_pcs_config = mv88e6390_serdes_pcs_config,
	.serdes_pcs_an_restart = mv88e6390_serdes_pcs_an_restart,
	.serdes_pcs_link_up = mv88e6390_serdes_pcs_link_up,
	.serdes_irq_mapping = mv88e6390_serdes_irq_mapping,
	.serdes_irq_enable = mv88e6390_serdes_irq_enable,
	.serdes_irq_status = mv88e6390_serdes_irq_status,
	.gpio_ops = &mv88e6352_gpio_ops,
	.serdes_get_sset_count = mv88e6390_serdes_get_sset_count,
	.serdes_get_strings = mv88e6390_serdes_get_strings,
	.serdes_get_stats = mv88e6390_serdes_get_stats,
	.serdes_get_regs_len = mv88e6390_serdes_get_regs_len,
	.serdes_get_regs = mv88e6390_serdes_get_regs,
	.phylink_get_caps = mv88e6341_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6161_ops = {
	/* MV88E6XXX_FAMILY_6165 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_speed_duplex = mv88e6185_port_set_speed_duplex,
	.port_tag_remap = mv88e6095_port_tag_remap,
	.port_set_policy = mv88e6352_port_set_policy,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6097_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6185_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6xxx_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6095_stats_get_sset_count,
	.stats_get_strings = mv88e6095_stats_get_strings,
	.stats_get_stats = mv88e6095_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6097_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6352_g2_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6352_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6352_g1_vtu_loadpurge,
	.stu_getnext = mv88e6352_g1_stu_getnext,
	.stu_loadpurge = mv88e6352_g1_stu_loadpurge,
	.avb_ops = &mv88e6165_avb_ops,
	.ptp_ops = &mv88e6165_ptp_ops,
	.phylink_get_caps = mv88e6185_phylink_get_caps,
	.set_max_frame_size = mv88e6185_g1_set_max_frame_size,
};

static const struct mv88e6xxx_ops mv88e6165_ops = {
	/* MV88E6XXX_FAMILY_6165 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6165_phy_read,
	.phy_write = mv88e6165_phy_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_speed_duplex = mv88e6185_port_set_speed_duplex,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6185_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6xxx_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6095_stats_get_sset_count,
	.stats_get_strings = mv88e6095_stats_get_strings,
	.stats_get_stats = mv88e6095_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6097_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6352_g2_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6352_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6352_g1_vtu_loadpurge,
	.stu_getnext = mv88e6352_g1_stu_getnext,
	.stu_loadpurge = mv88e6352_g1_stu_loadpurge,
	.avb_ops = &mv88e6165_avb_ops,
	.ptp_ops = &mv88e6165_ptp_ops,
	.phylink_get_caps = mv88e6185_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6171_ops = {
	/* MV88E6XXX_FAMILY_6351 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6352_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6185_port_set_speed_duplex,
	.port_tag_remap = mv88e6095_port_tag_remap,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6097_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6320_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6095_stats_get_sset_count,
	.stats_get_strings = mv88e6095_stats_get_strings,
	.stats_get_stats = mv88e6095_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6097_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6352_g2_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6352_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6352_g1_vtu_loadpurge,
	.stu_getnext = mv88e6352_g1_stu_getnext,
	.stu_loadpurge = mv88e6352_g1_stu_loadpurge,
	.phylink_get_caps = mv88e6185_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6172_ops = {
	/* MV88E6XXX_FAMILY_6352 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.get_eeprom = mv88e6xxx_g2_get_eeprom16,
	.set_eeprom = mv88e6xxx_g2_set_eeprom16,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6352_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6352_port_set_speed_duplex,
	.port_tag_remap = mv88e6095_port_tag_remap,
	.port_set_policy = mv88e6352_port_set_policy,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6097_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6320_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6095_stats_get_sset_count,
	.stats_get_strings = mv88e6095_stats_get_strings,
	.stats_get_stats = mv88e6095_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6097_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6352_g2_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.rmu_disable = mv88e6352_g1_rmu_disable,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6352_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6352_g1_vtu_loadpurge,
	.stu_getnext = mv88e6352_g1_stu_getnext,
	.stu_loadpurge = mv88e6352_g1_stu_loadpurge,
	.serdes_get_lane = mv88e6352_serdes_get_lane,
	.serdes_pcs_get_state = mv88e6352_serdes_pcs_get_state,
	.serdes_pcs_config = mv88e6352_serdes_pcs_config,
	.serdes_pcs_an_restart = mv88e6352_serdes_pcs_an_restart,
	.serdes_pcs_link_up = mv88e6352_serdes_pcs_link_up,
	.serdes_power = mv88e6352_serdes_power,
	.serdes_get_regs_len = mv88e6352_serdes_get_regs_len,
	.serdes_get_regs = mv88e6352_serdes_get_regs,
	.gpio_ops = &mv88e6352_gpio_ops,
	.phylink_get_caps = mv88e6352_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6175_ops = {
	/* MV88E6XXX_FAMILY_6351 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6352_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6185_port_set_speed_duplex,
	.port_tag_remap = mv88e6095_port_tag_remap,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6097_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6320_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6095_stats_get_sset_count,
	.stats_get_strings = mv88e6095_stats_get_strings,
	.stats_get_stats = mv88e6095_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6097_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6352_g2_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6352_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6352_g1_vtu_loadpurge,
	.stu_getnext = mv88e6352_g1_stu_getnext,
	.stu_loadpurge = mv88e6352_g1_stu_loadpurge,
	.phylink_get_caps = mv88e6185_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6176_ops = {
	/* MV88E6XXX_FAMILY_6352 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.get_eeprom = mv88e6xxx_g2_get_eeprom16,
	.set_eeprom = mv88e6xxx_g2_set_eeprom16,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6352_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6352_port_set_speed_duplex,
	.port_tag_remap = mv88e6095_port_tag_remap,
	.port_set_policy = mv88e6352_port_set_policy,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6097_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6320_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6095_stats_get_sset_count,
	.stats_get_strings = mv88e6095_stats_get_strings,
	.stats_get_stats = mv88e6095_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6097_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6352_g2_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.rmu_disable = mv88e6352_g1_rmu_disable,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6352_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6352_g1_vtu_loadpurge,
	.stu_getnext = mv88e6352_g1_stu_getnext,
	.stu_loadpurge = mv88e6352_g1_stu_loadpurge,
	.serdes_get_lane = mv88e6352_serdes_get_lane,
	.serdes_pcs_get_state = mv88e6352_serdes_pcs_get_state,
	.serdes_pcs_config = mv88e6352_serdes_pcs_config,
	.serdes_pcs_an_restart = mv88e6352_serdes_pcs_an_restart,
	.serdes_pcs_link_up = mv88e6352_serdes_pcs_link_up,
	.serdes_power = mv88e6352_serdes_power,
	.serdes_irq_mapping = mv88e6352_serdes_irq_mapping,
	.serdes_irq_enable = mv88e6352_serdes_irq_enable,
	.serdes_irq_status = mv88e6352_serdes_irq_status,
	.serdes_get_regs_len = mv88e6352_serdes_get_regs_len,
	.serdes_get_regs = mv88e6352_serdes_get_regs,
	.serdes_set_tx_amplitude = mv88e6352_serdes_set_tx_amplitude,
	.gpio_ops = &mv88e6352_gpio_ops,
	.phylink_get_caps = mv88e6352_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6185_ops = {
	/* MV88E6XXX_FAMILY_6185 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.set_switch_mac = mv88e6xxx_g1_set_switch_mac,
	.phy_read = mv88e6185_phy_ppu_read,
	.phy_write = mv88e6185_phy_ppu_write,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6185_port_sync_link,
	.port_set_speed_duplex = mv88e6185_port_set_speed_duplex,
	.port_set_frame_mode = mv88e6085_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6185_port_set_forward_unknown,
	.port_set_mcast_flood = mv88e6185_port_set_default_forward,
	.port_egress_rate_limiting = mv88e6095_port_egress_rate_limiting,
	.port_set_upstream_port = mv88e6095_port_set_upstream_port,
	.port_set_pause = mv88e6185_port_set_pause,
	.port_get_cmode = mv88e6185_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6xxx_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6095_stats_get_sset_count,
	.stats_get_strings = mv88e6095_stats_get_strings,
	.stats_get_stats = mv88e6095_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6097_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6185_g2_mgmt_rsvd2cpu,
	.serdes_power = mv88e6185_serdes_power,
	.serdes_get_lane = mv88e6185_serdes_get_lane,
	.serdes_pcs_get_state = mv88e6185_serdes_pcs_get_state,
	.set_cascade_port = mv88e6185_g1_set_cascade_port,
	.ppu_enable = mv88e6185_g1_ppu_enable,
	.ppu_disable = mv88e6185_g1_ppu_disable,
	.reset = mv88e6185_g1_reset,
	.vtu_getnext = mv88e6185_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6185_g1_vtu_loadpurge,
	.phylink_get_caps = mv88e6185_phylink_get_caps,
	.set_max_frame_size = mv88e6185_g1_set_max_frame_size,
};

static const struct mv88e6xxx_ops mv88e6190_ops = {
	/* MV88E6XXX_FAMILY_6390 */
	.setup_errata = mv88e6390_setup_errata,
	.irl_init_all = mv88e6390_g2_irl_init_all,
	.get_eeprom = mv88e6xxx_g2_get_eeprom8,
	.set_eeprom = mv88e6xxx_g2_set_eeprom8,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6390_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6390_port_set_speed_duplex,
	.port_max_speed_mode = mv88e6390_port_max_speed_mode,
	.port_tag_remap = mv88e6390_port_tag_remap,
	.port_set_policy = mv88e6352_port_set_policy,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_pause_limit = mv88e6390_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_set_cmode = mv88e6390_port_set_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6390_g1_stats_snapshot,
	.stats_set_histogram = mv88e6390_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6320_stats_get_sset_count,
	.stats_get_strings = mv88e6320_stats_get_strings,
	.stats_get_stats = mv88e6390_stats_get_stats,
	.set_cpu_port = mv88e6390_g1_set_cpu_port,
	.set_egress_port = mv88e6390_g1_set_egress_port,
	.watchdog_ops = &mv88e6390_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6390_g1_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.rmu_disable = mv88e6390_g1_rmu_disable,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6390_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6390_g1_vtu_loadpurge,
	.stu_getnext = mv88e6390_g1_stu_getnext,
	.stu_loadpurge = mv88e6390_g1_stu_loadpurge,
	.serdes_power = mv88e6390_serdes_power,
	.serdes_get_lane = mv88e6390_serdes_get_lane,
	/* Check status register pause & lpa register */
	.serdes_pcs_get_state = mv88e6390_serdes_pcs_get_state,
	.serdes_pcs_config = mv88e6390_serdes_pcs_config,
	.serdes_pcs_an_restart = mv88e6390_serdes_pcs_an_restart,
	.serdes_pcs_link_up = mv88e6390_serdes_pcs_link_up,
	.serdes_irq_mapping = mv88e6390_serdes_irq_mapping,
	.serdes_irq_enable = mv88e6390_serdes_irq_enable,
	.serdes_irq_status = mv88e6390_serdes_irq_status,
	.serdes_get_strings = mv88e6390_serdes_get_strings,
	.serdes_get_stats = mv88e6390_serdes_get_stats,
	.serdes_get_regs_len = mv88e6390_serdes_get_regs_len,
	.serdes_get_regs = mv88e6390_serdes_get_regs,
	.gpio_ops = &mv88e6352_gpio_ops,
	.phylink_get_caps = mv88e6390_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6190x_ops = {
	/* MV88E6XXX_FAMILY_6390 */
	.setup_errata = mv88e6390_setup_errata,
	.irl_init_all = mv88e6390_g2_irl_init_all,
	.get_eeprom = mv88e6xxx_g2_get_eeprom8,
	.set_eeprom = mv88e6xxx_g2_set_eeprom8,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6390_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6390x_port_set_speed_duplex,
	.port_max_speed_mode = mv88e6390x_port_max_speed_mode,
	.port_tag_remap = mv88e6390_port_tag_remap,
	.port_set_policy = mv88e6352_port_set_policy,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_pause_limit = mv88e6390_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_set_cmode = mv88e6390x_port_set_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6390_g1_stats_snapshot,
	.stats_set_histogram = mv88e6390_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6320_stats_get_sset_count,
	.stats_get_strings = mv88e6320_stats_get_strings,
	.stats_get_stats = mv88e6390_stats_get_stats,
	.set_cpu_port = mv88e6390_g1_set_cpu_port,
	.set_egress_port = mv88e6390_g1_set_egress_port,
	.watchdog_ops = &mv88e6390_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6390_g1_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.rmu_disable = mv88e6390_g1_rmu_disable,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6390_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6390_g1_vtu_loadpurge,
	.stu_getnext = mv88e6390_g1_stu_getnext,
	.stu_loadpurge = mv88e6390_g1_stu_loadpurge,
	.serdes_power = mv88e6390_serdes_power,
	.serdes_get_lane = mv88e6390x_serdes_get_lane,
	/* Check status register pause & lpa register */
	.serdes_pcs_get_state = mv88e6390_serdes_pcs_get_state,
	.serdes_pcs_config = mv88e6390_serdes_pcs_config,
	.serdes_pcs_an_restart = mv88e6390_serdes_pcs_an_restart,
	.serdes_pcs_link_up = mv88e6390_serdes_pcs_link_up,
	.serdes_irq_mapping = mv88e6390_serdes_irq_mapping,
	.serdes_irq_enable = mv88e6390_serdes_irq_enable,
	.serdes_irq_status = mv88e6390_serdes_irq_status,
	.serdes_get_strings = mv88e6390_serdes_get_strings,
	.serdes_get_stats = mv88e6390_serdes_get_stats,
	.serdes_get_regs_len = mv88e6390_serdes_get_regs_len,
	.serdes_get_regs = mv88e6390_serdes_get_regs,
	.gpio_ops = &mv88e6352_gpio_ops,
	.phylink_get_caps = mv88e6390x_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6191_ops = {
	/* MV88E6XXX_FAMILY_6390 */
	.setup_errata = mv88e6390_setup_errata,
	.irl_init_all = mv88e6390_g2_irl_init_all,
	.get_eeprom = mv88e6xxx_g2_get_eeprom8,
	.set_eeprom = mv88e6xxx_g2_set_eeprom8,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6390_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6390_port_set_speed_duplex,
	.port_max_speed_mode = mv88e6390_port_max_speed_mode,
	.port_tag_remap = mv88e6390_port_tag_remap,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_pause_limit = mv88e6390_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_set_cmode = mv88e6390_port_set_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6390_g1_stats_snapshot,
	.stats_set_histogram = mv88e6390_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6320_stats_get_sset_count,
	.stats_get_strings = mv88e6320_stats_get_strings,
	.stats_get_stats = mv88e6390_stats_get_stats,
	.set_cpu_port = mv88e6390_g1_set_cpu_port,
	.set_egress_port = mv88e6390_g1_set_egress_port,
	.watchdog_ops = &mv88e6390_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6390_g1_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.rmu_disable = mv88e6390_g1_rmu_disable,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6390_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6390_g1_vtu_loadpurge,
	.stu_getnext = mv88e6390_g1_stu_getnext,
	.stu_loadpurge = mv88e6390_g1_stu_loadpurge,
	.serdes_power = mv88e6390_serdes_power,
	.serdes_get_lane = mv88e6390_serdes_get_lane,
	/* Check status register pause & lpa register */
	.serdes_pcs_get_state = mv88e6390_serdes_pcs_get_state,
	.serdes_pcs_config = mv88e6390_serdes_pcs_config,
	.serdes_pcs_an_restart = mv88e6390_serdes_pcs_an_restart,
	.serdes_pcs_link_up = mv88e6390_serdes_pcs_link_up,
	.serdes_irq_mapping = mv88e6390_serdes_irq_mapping,
	.serdes_irq_enable = mv88e6390_serdes_irq_enable,
	.serdes_irq_status = mv88e6390_serdes_irq_status,
	.serdes_get_strings = mv88e6390_serdes_get_strings,
	.serdes_get_stats = mv88e6390_serdes_get_stats,
	.serdes_get_regs_len = mv88e6390_serdes_get_regs_len,
	.serdes_get_regs = mv88e6390_serdes_get_regs,
	.avb_ops = &mv88e6390_avb_ops,
	.ptp_ops = &mv88e6352_ptp_ops,
	.phylink_get_caps = mv88e6390_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6240_ops = {
	/* MV88E6XXX_FAMILY_6352 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.get_eeprom = mv88e6xxx_g2_get_eeprom16,
	.set_eeprom = mv88e6xxx_g2_set_eeprom16,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6352_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6352_port_set_speed_duplex,
	.port_tag_remap = mv88e6095_port_tag_remap,
	.port_set_policy = mv88e6352_port_set_policy,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6097_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6320_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6095_stats_get_sset_count,
	.stats_get_strings = mv88e6095_stats_get_strings,
	.stats_get_stats = mv88e6095_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6097_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6352_g2_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.rmu_disable = mv88e6352_g1_rmu_disable,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6352_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6352_g1_vtu_loadpurge,
	.stu_getnext = mv88e6352_g1_stu_getnext,
	.stu_loadpurge = mv88e6352_g1_stu_loadpurge,
	.serdes_get_lane = mv88e6352_serdes_get_lane,
	.serdes_pcs_get_state = mv88e6352_serdes_pcs_get_state,
	.serdes_pcs_config = mv88e6352_serdes_pcs_config,
	.serdes_pcs_an_restart = mv88e6352_serdes_pcs_an_restart,
	.serdes_pcs_link_up = mv88e6352_serdes_pcs_link_up,
	.serdes_power = mv88e6352_serdes_power,
	.serdes_irq_mapping = mv88e6352_serdes_irq_mapping,
	.serdes_irq_enable = mv88e6352_serdes_irq_enable,
	.serdes_irq_status = mv88e6352_serdes_irq_status,
	.serdes_get_regs_len = mv88e6352_serdes_get_regs_len,
	.serdes_get_regs = mv88e6352_serdes_get_regs,
	.serdes_set_tx_amplitude = mv88e6352_serdes_set_tx_amplitude,
	.gpio_ops = &mv88e6352_gpio_ops,
	.avb_ops = &mv88e6352_avb_ops,
	.ptp_ops = &mv88e6352_ptp_ops,
	.phylink_get_caps = mv88e6352_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6250_ops = {
	/* MV88E6XXX_FAMILY_6250 */
	.ieee_pri_map = mv88e6250_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.get_eeprom = mv88e6xxx_g2_get_eeprom16,
	.set_eeprom = mv88e6xxx_g2_set_eeprom16,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6352_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6250_port_set_speed_duplex,
	.port_tag_remap = mv88e6095_port_tag_remap,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6097_port_pause_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.stats_snapshot = mv88e6320_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6250_stats_get_sset_count,
	.stats_get_strings = mv88e6250_stats_get_strings,
	.stats_get_stats = mv88e6250_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6250_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6352_g2_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6250_g1_reset,
	.vtu_getnext = mv88e6185_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6185_g1_vtu_loadpurge,
	.avb_ops = &mv88e6352_avb_ops,
	.ptp_ops = &mv88e6250_ptp_ops,
	.phylink_get_caps = mv88e6250_phylink_get_caps,
	.set_max_frame_size = mv88e6185_g1_set_max_frame_size,
};

static const struct mv88e6xxx_ops mv88e6290_ops = {
	/* MV88E6XXX_FAMILY_6390 */
	.setup_errata = mv88e6390_setup_errata,
	.irl_init_all = mv88e6390_g2_irl_init_all,
	.get_eeprom = mv88e6xxx_g2_get_eeprom8,
	.set_eeprom = mv88e6xxx_g2_set_eeprom8,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6390_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6390_port_set_speed_duplex,
	.port_max_speed_mode = mv88e6390_port_max_speed_mode,
	.port_tag_remap = mv88e6390_port_tag_remap,
	.port_set_policy = mv88e6352_port_set_policy,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_pause_limit = mv88e6390_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_set_cmode = mv88e6390_port_set_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6390_g1_stats_snapshot,
	.stats_set_histogram = mv88e6390_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6320_stats_get_sset_count,
	.stats_get_strings = mv88e6320_stats_get_strings,
	.stats_get_stats = mv88e6390_stats_get_stats,
	.set_cpu_port = mv88e6390_g1_set_cpu_port,
	.set_egress_port = mv88e6390_g1_set_egress_port,
	.watchdog_ops = &mv88e6390_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6390_g1_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.rmu_disable = mv88e6390_g1_rmu_disable,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6390_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6390_g1_vtu_loadpurge,
	.stu_getnext = mv88e6390_g1_stu_getnext,
	.stu_loadpurge = mv88e6390_g1_stu_loadpurge,
	.serdes_power = mv88e6390_serdes_power,
	.serdes_get_lane = mv88e6390_serdes_get_lane,
	/* Check status register pause & lpa register */
	.serdes_pcs_get_state = mv88e6390_serdes_pcs_get_state,
	.serdes_pcs_config = mv88e6390_serdes_pcs_config,
	.serdes_pcs_an_restart = mv88e6390_serdes_pcs_an_restart,
	.serdes_pcs_link_up = mv88e6390_serdes_pcs_link_up,
	.serdes_irq_mapping = mv88e6390_serdes_irq_mapping,
	.serdes_irq_enable = mv88e6390_serdes_irq_enable,
	.serdes_irq_status = mv88e6390_serdes_irq_status,
	.serdes_get_strings = mv88e6390_serdes_get_strings,
	.serdes_get_stats = mv88e6390_serdes_get_stats,
	.serdes_get_regs_len = mv88e6390_serdes_get_regs_len,
	.serdes_get_regs = mv88e6390_serdes_get_regs,
	.gpio_ops = &mv88e6352_gpio_ops,
	.avb_ops = &mv88e6390_avb_ops,
	.ptp_ops = &mv88e6390_ptp_ops,
	.phylink_get_caps = mv88e6390_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6320_ops = {
	/* MV88E6XXX_FAMILY_6320 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.get_eeprom = mv88e6xxx_g2_get_eeprom16,
	.set_eeprom = mv88e6xxx_g2_set_eeprom16,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6320_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6185_port_set_speed_duplex,
	.port_tag_remap = mv88e6095_port_tag_remap,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6097_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6320_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6320_stats_get_sset_count,
	.stats_get_strings = mv88e6320_stats_get_strings,
	.stats_get_stats = mv88e6320_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6390_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6352_g2_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.vtu_getnext = mv88e6185_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6185_g1_vtu_loadpurge,
	.gpio_ops = &mv88e6352_gpio_ops,
	.avb_ops = &mv88e6352_avb_ops,
	.ptp_ops = &mv88e6352_ptp_ops,
	.phylink_get_caps = mv88e6185_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6321_ops = {
	/* MV88E6XXX_FAMILY_6320 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.get_eeprom = mv88e6xxx_g2_get_eeprom16,
	.set_eeprom = mv88e6xxx_g2_set_eeprom16,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6320_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6185_port_set_speed_duplex,
	.port_tag_remap = mv88e6095_port_tag_remap,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6097_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6320_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6320_stats_get_sset_count,
	.stats_get_strings = mv88e6320_stats_get_strings,
	.stats_get_stats = mv88e6320_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6390_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6352_g2_mgmt_rsvd2cpu,
	.reset = mv88e6352_g1_reset,
	.vtu_getnext = mv88e6185_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6185_g1_vtu_loadpurge,
	.gpio_ops = &mv88e6352_gpio_ops,
	.avb_ops = &mv88e6352_avb_ops,
	.ptp_ops = &mv88e6352_ptp_ops,
	.phylink_get_caps = mv88e6185_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6341_ops = {
	/* MV88E6XXX_FAMILY_6341 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.get_eeprom = mv88e6xxx_g2_get_eeprom8,
	.set_eeprom = mv88e6xxx_g2_set_eeprom8,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6390_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6341_port_set_speed_duplex,
	.port_max_speed_mode = mv88e6341_port_max_speed_mode,
	.port_tag_remap = mv88e6095_port_tag_remap,
	.port_set_policy = mv88e6352_port_set_policy,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6097_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_set_cmode = mv88e6341_port_set_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6390_g1_stats_snapshot,
	.stats_set_histogram = mv88e6390_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6320_stats_get_sset_count,
	.stats_get_strings = mv88e6320_stats_get_strings,
	.stats_get_stats = mv88e6390_stats_get_stats,
	.set_cpu_port = mv88e6390_g1_set_cpu_port,
	.set_egress_port = mv88e6390_g1_set_egress_port,
	.watchdog_ops = &mv88e6390_watchdog_ops,
	.mgmt_rsvd2cpu =  mv88e6390_g1_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.rmu_disable = mv88e6390_g1_rmu_disable,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6352_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6352_g1_vtu_loadpurge,
	.stu_getnext = mv88e6352_g1_stu_getnext,
	.stu_loadpurge = mv88e6352_g1_stu_loadpurge,
	.serdes_power = mv88e6390_serdes_power,
	.serdes_get_lane = mv88e6341_serdes_get_lane,
	/* Check status register pause & lpa register */
	.serdes_pcs_get_state = mv88e6390_serdes_pcs_get_state,
	.serdes_pcs_config = mv88e6390_serdes_pcs_config,
	.serdes_pcs_an_restart = mv88e6390_serdes_pcs_an_restart,
	.serdes_pcs_link_up = mv88e6390_serdes_pcs_link_up,
	.serdes_irq_mapping = mv88e6390_serdes_irq_mapping,
	.serdes_irq_enable = mv88e6390_serdes_irq_enable,
	.serdes_irq_status = mv88e6390_serdes_irq_status,
	.gpio_ops = &mv88e6352_gpio_ops,
	.avb_ops = &mv88e6390_avb_ops,
	.ptp_ops = &mv88e6352_ptp_ops,
	.serdes_get_sset_count = mv88e6390_serdes_get_sset_count,
	.serdes_get_strings = mv88e6390_serdes_get_strings,
	.serdes_get_stats = mv88e6390_serdes_get_stats,
	.serdes_get_regs_len = mv88e6390_serdes_get_regs_len,
	.serdes_get_regs = mv88e6390_serdes_get_regs,
	.phylink_get_caps = mv88e6341_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6350_ops = {
	/* MV88E6XXX_FAMILY_6351 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6352_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6185_port_set_speed_duplex,
	.port_tag_remap = mv88e6095_port_tag_remap,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6097_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6320_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6095_stats_get_sset_count,
	.stats_get_strings = mv88e6095_stats_get_strings,
	.stats_get_stats = mv88e6095_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6097_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6352_g2_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6352_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6352_g1_vtu_loadpurge,
	.stu_getnext = mv88e6352_g1_stu_getnext,
	.stu_loadpurge = mv88e6352_g1_stu_loadpurge,
	.phylink_get_caps = mv88e6185_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6351_ops = {
	/* MV88E6XXX_FAMILY_6351 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6352_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6185_port_set_speed_duplex,
	.port_tag_remap = mv88e6095_port_tag_remap,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6097_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6320_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6095_stats_get_sset_count,
	.stats_get_strings = mv88e6095_stats_get_strings,
	.stats_get_stats = mv88e6095_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6097_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6352_g2_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6352_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6352_g1_vtu_loadpurge,
	.stu_getnext = mv88e6352_g1_stu_getnext,
	.stu_loadpurge = mv88e6352_g1_stu_loadpurge,
	.avb_ops = &mv88e6352_avb_ops,
	.ptp_ops = &mv88e6352_ptp_ops,
	.phylink_get_caps = mv88e6185_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6352_ops = {
	/* MV88E6XXX_FAMILY_6352 */
	.ieee_pri_map = mv88e6085_g1_ieee_pri_map,
	.ip_pri_map = mv88e6085_g1_ip_pri_map,
	.irl_init_all = mv88e6352_g2_irl_init_all,
	.get_eeprom = mv88e6xxx_g2_get_eeprom16,
	.set_eeprom = mv88e6xxx_g2_set_eeprom16,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6352_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6352_port_set_speed_duplex,
	.port_tag_remap = mv88e6095_port_tag_remap,
	.port_set_policy = mv88e6352_port_set_policy,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6097_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6320_g1_stats_snapshot,
	.stats_set_histogram = mv88e6095_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6095_stats_get_sset_count,
	.stats_get_strings = mv88e6095_stats_get_strings,
	.stats_get_stats = mv88e6095_stats_get_stats,
	.set_cpu_port = mv88e6095_g1_set_cpu_port,
	.set_egress_port = mv88e6095_g1_set_egress_port,
	.watchdog_ops = &mv88e6097_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6352_g2_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.rmu_disable = mv88e6352_g1_rmu_disable,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6352_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6352_g1_vtu_loadpurge,
	.stu_getnext = mv88e6352_g1_stu_getnext,
	.stu_loadpurge = mv88e6352_g1_stu_loadpurge,
	.serdes_get_lane = mv88e6352_serdes_get_lane,
	.serdes_pcs_get_state = mv88e6352_serdes_pcs_get_state,
	.serdes_pcs_config = mv88e6352_serdes_pcs_config,
	.serdes_pcs_an_restart = mv88e6352_serdes_pcs_an_restart,
	.serdes_pcs_link_up = mv88e6352_serdes_pcs_link_up,
	.serdes_power = mv88e6352_serdes_power,
	.serdes_irq_mapping = mv88e6352_serdes_irq_mapping,
	.serdes_irq_enable = mv88e6352_serdes_irq_enable,
	.serdes_irq_status = mv88e6352_serdes_irq_status,
	.gpio_ops = &mv88e6352_gpio_ops,
	.avb_ops = &mv88e6352_avb_ops,
	.ptp_ops = &mv88e6352_ptp_ops,
	.serdes_get_sset_count = mv88e6352_serdes_get_sset_count,
	.serdes_get_strings = mv88e6352_serdes_get_strings,
	.serdes_get_stats = mv88e6352_serdes_get_stats,
	.serdes_get_regs_len = mv88e6352_serdes_get_regs_len,
	.serdes_get_regs = mv88e6352_serdes_get_regs,
	.serdes_set_tx_amplitude = mv88e6352_serdes_set_tx_amplitude,
	.phylink_get_caps = mv88e6352_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6390_ops = {
	/* MV88E6XXX_FAMILY_6390 */
	.setup_errata = mv88e6390_setup_errata,
	.irl_init_all = mv88e6390_g2_irl_init_all,
	.get_eeprom = mv88e6xxx_g2_get_eeprom8,
	.set_eeprom = mv88e6xxx_g2_set_eeprom8,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6390_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6390_port_set_speed_duplex,
	.port_max_speed_mode = mv88e6390_port_max_speed_mode,
	.port_tag_remap = mv88e6390_port_tag_remap,
	.port_set_policy = mv88e6352_port_set_policy,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6390_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_set_cmode = mv88e6390_port_set_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6390_g1_stats_snapshot,
	.stats_set_histogram = mv88e6390_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6320_stats_get_sset_count,
	.stats_get_strings = mv88e6320_stats_get_strings,
	.stats_get_stats = mv88e6390_stats_get_stats,
	.set_cpu_port = mv88e6390_g1_set_cpu_port,
	.set_egress_port = mv88e6390_g1_set_egress_port,
	.watchdog_ops = &mv88e6390_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6390_g1_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.rmu_disable = mv88e6390_g1_rmu_disable,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6390_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6390_g1_vtu_loadpurge,
	.stu_getnext = mv88e6390_g1_stu_getnext,
	.stu_loadpurge = mv88e6390_g1_stu_loadpurge,
	.serdes_power = mv88e6390_serdes_power,
	.serdes_get_lane = mv88e6390_serdes_get_lane,
	/* Check status register pause & lpa register */
	.serdes_pcs_get_state = mv88e6390_serdes_pcs_get_state,
	.serdes_pcs_config = mv88e6390_serdes_pcs_config,
	.serdes_pcs_an_restart = mv88e6390_serdes_pcs_an_restart,
	.serdes_pcs_link_up = mv88e6390_serdes_pcs_link_up,
	.serdes_irq_mapping = mv88e6390_serdes_irq_mapping,
	.serdes_irq_enable = mv88e6390_serdes_irq_enable,
	.serdes_irq_status = mv88e6390_serdes_irq_status,
	.gpio_ops = &mv88e6352_gpio_ops,
	.avb_ops = &mv88e6390_avb_ops,
	.ptp_ops = &mv88e6390_ptp_ops,
	.serdes_get_sset_count = mv88e6390_serdes_get_sset_count,
	.serdes_get_strings = mv88e6390_serdes_get_strings,
	.serdes_get_stats = mv88e6390_serdes_get_stats,
	.serdes_get_regs_len = mv88e6390_serdes_get_regs_len,
	.serdes_get_regs = mv88e6390_serdes_get_regs,
	.phylink_get_caps = mv88e6390_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6390x_ops = {
	/* MV88E6XXX_FAMILY_6390 */
	.setup_errata = mv88e6390_setup_errata,
	.irl_init_all = mv88e6390_g2_irl_init_all,
	.get_eeprom = mv88e6xxx_g2_get_eeprom8,
	.set_eeprom = mv88e6xxx_g2_set_eeprom8,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6390_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6390x_port_set_speed_duplex,
	.port_max_speed_mode = mv88e6390x_port_max_speed_mode,
	.port_tag_remap = mv88e6390_port_tag_remap,
	.port_set_policy = mv88e6352_port_set_policy,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6351_port_set_ether_type,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6390_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_set_cmode = mv88e6390x_port_set_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.stats_snapshot = mv88e6390_g1_stats_snapshot,
	.stats_set_histogram = mv88e6390_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6320_stats_get_sset_count,
	.stats_get_strings = mv88e6320_stats_get_strings,
	.stats_get_stats = mv88e6390_stats_get_stats,
	.set_cpu_port = mv88e6390_g1_set_cpu_port,
	.set_egress_port = mv88e6390_g1_set_egress_port,
	.watchdog_ops = &mv88e6390_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6390_g1_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.rmu_disable = mv88e6390_g1_rmu_disable,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6390_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6390_g1_vtu_loadpurge,
	.stu_getnext = mv88e6390_g1_stu_getnext,
	.stu_loadpurge = mv88e6390_g1_stu_loadpurge,
	.serdes_power = mv88e6390_serdes_power,
	.serdes_get_lane = mv88e6390x_serdes_get_lane,
	.serdes_pcs_get_state = mv88e6390_serdes_pcs_get_state,
	.serdes_pcs_config = mv88e6390_serdes_pcs_config,
	.serdes_pcs_an_restart = mv88e6390_serdes_pcs_an_restart,
	.serdes_pcs_link_up = mv88e6390_serdes_pcs_link_up,
	.serdes_irq_mapping = mv88e6390_serdes_irq_mapping,
	.serdes_irq_enable = mv88e6390_serdes_irq_enable,
	.serdes_irq_status = mv88e6390_serdes_irq_status,
	.serdes_get_sset_count = mv88e6390_serdes_get_sset_count,
	.serdes_get_strings = mv88e6390_serdes_get_strings,
	.serdes_get_stats = mv88e6390_serdes_get_stats,
	.serdes_get_regs_len = mv88e6390_serdes_get_regs_len,
	.serdes_get_regs = mv88e6390_serdes_get_regs,
	.gpio_ops = &mv88e6352_gpio_ops,
	.avb_ops = &mv88e6390_avb_ops,
	.ptp_ops = &mv88e6390_ptp_ops,
	.phylink_get_caps = mv88e6390x_phylink_get_caps,
};

static const struct mv88e6xxx_ops mv88e6393x_ops = {
	/* MV88E6XXX_FAMILY_6393 */
	.setup_errata = mv88e6393x_serdes_setup_errata,
	.irl_init_all = mv88e6390_g2_irl_init_all,
	.get_eeprom = mv88e6xxx_g2_get_eeprom8,
	.set_eeprom = mv88e6xxx_g2_set_eeprom8,
	.set_switch_mac = mv88e6xxx_g2_set_switch_mac,
	.phy_read = mv88e6xxx_g2_smi_phy_read_c22,
	.phy_write = mv88e6xxx_g2_smi_phy_write_c22,
	.phy_read_c45 = mv88e6xxx_g2_smi_phy_read_c45,
	.phy_write_c45 = mv88e6xxx_g2_smi_phy_write_c45,
	.port_set_link = mv88e6xxx_port_set_link,
	.port_sync_link = mv88e6xxx_port_sync_link,
	.port_set_rgmii_delay = mv88e6390_port_set_rgmii_delay,
	.port_set_speed_duplex = mv88e6393x_port_set_speed_duplex,
	.port_max_speed_mode = mv88e6393x_port_max_speed_mode,
	.port_tag_remap = mv88e6390_port_tag_remap,
	.port_set_policy = mv88e6393x_port_set_policy,
	.port_set_frame_mode = mv88e6351_port_set_frame_mode,
	.port_set_ucast_flood = mv88e6352_port_set_ucast_flood,
	.port_set_mcast_flood = mv88e6352_port_set_mcast_flood,
	.port_set_ether_type = mv88e6393x_port_set_ether_type,
	.port_set_jumbo_size = mv88e6165_port_set_jumbo_size,
	.port_egress_rate_limiting = mv88e6097_port_egress_rate_limiting,
	.port_pause_limit = mv88e6390_port_pause_limit,
	.port_disable_learn_limit = mv88e6xxx_port_disable_learn_limit,
	.port_disable_pri_override = mv88e6xxx_port_disable_pri_override,
	.port_get_cmode = mv88e6352_port_get_cmode,
	.port_set_cmode = mv88e6393x_port_set_cmode,
	.port_setup_message_port = mv88e6xxx_setup_message_port,
	.port_set_upstream_port = mv88e6393x_port_set_upstream_port,
	.stats_snapshot = mv88e6390_g1_stats_snapshot,
	.stats_set_histogram = mv88e6390_g1_stats_set_histogram,
	.stats_get_sset_count = mv88e6320_stats_get_sset_count,
	.stats_get_strings = mv88e6320_stats_get_strings,
	.stats_get_stats = mv88e6390_stats_get_stats,
	/* .set_cpu_port is missing because this family does not support a global
	 * CPU port, only per port CPU port which is set via
	 * .port_set_upstream_port method.
	 */
	.set_egress_port = mv88e6393x_set_egress_port,
	.watchdog_ops = &mv88e6393x_watchdog_ops,
	.mgmt_rsvd2cpu = mv88e6393x_port_mgmt_rsvd2cpu,
	.pot_clear = mv88e6xxx_g2_pot_clear,
	.reset = mv88e6352_g1_reset,
	.rmu_disable = mv88e6390_g1_rmu_disable,
	.atu_get_hash = mv88e6165_g1_atu_get_hash,
	.atu_set_hash = mv88e6165_g1_atu_set_hash,
	.vtu_getnext = mv88e6390_g1_vtu_getnext,
	.vtu_loadpurge = mv88e6390_g1_vtu_loadpurge,
	.stu_getnext = mv88e6390_g1_stu_getnext,
	.stu_loadpurge = mv88e6390_g1_stu_loadpurge,
	.serdes_power = mv88e6393x_serdes_power,
	.serdes_get_lane = mv88e6393x_serdes_get_lane,
	.serdes_pcs_get_state = mv88e6393x_serdes_pcs_get_state,
	.serdes_pcs_config = mv88e6390_serdes_pcs_config,
	.serdes_pcs_an_restart = mv88e6390_serdes_pcs_an_restart,
	.serdes_pcs_link_up = mv88e6390_serdes_pcs_link_up,
	.serdes_irq_mapping = mv88e6390_serdes_irq_mapping,
	.serdes_irq_enable = mv88e6393x_serdes_irq_enable,
	.serdes_irq_status = mv88e6393x_serdes_irq_status,
	/* TODO: serdes stats */
	.gpio_ops = &mv88e6352_gpio_ops,
	.avb_ops = &mv88e6390_avb_ops,
	.ptp_ops = &mv88e6352_ptp_ops,
	.phylink_get_caps = mv88e6393x_phylink_get_caps,
};

static const struct mv88e6xxx_info mv88e6xxx_table[] = {
	[MV88E6020] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6020,
		.family = MV88E6XXX_FAMILY_6250,
		.name = "Marvell 88E6020",
		.num_databases = 64,
		.num_ports = 4,
		.num_internal_phys = 2,
		.max_vid = 4095,
		.port_base_addr = 0x8,
		.phy_base_addr = 0x0,
		.global1_addr = 0xf,
		.global2_addr = 0x7,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.g2_irqs = 5,
		.atu_move_port_mask = 0xf,
		.dual_chip = true,
		.ops = &mv88e6250_ops,
	},

	[MV88E6071] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6071,
		.family = MV88E6XXX_FAMILY_6250,
		.name = "Marvell 88E6071",
		.num_databases = 64,
		.num_ports = 7,
		.num_internal_phys = 5,
		.max_vid = 4095,
		.port_base_addr = 0x08,
		.phy_base_addr = 0x00,
		.global1_addr = 0x0f,
		.global2_addr = 0x07,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.g2_irqs = 5,
		.atu_move_port_mask = 0xf,
		.dual_chip = true,
		.ops = &mv88e6250_ops,
	},

	[MV88E6085] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6085,
		.family = MV88E6XXX_FAMILY_6097,
		.name = "Marvell 88E6085",
		.num_databases = 4096,
		.num_macs = 8192,
		.num_ports = 10,
		.num_internal_phys = 5,
		.max_vid = 4095,
		.max_sid = 63,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 8,
		.g2_irqs = 10,
		.atu_move_port_mask = 0xf,
		.pvt = true,
		.multi_chip = true,
		.ops = &mv88e6085_ops,
	},

	[MV88E6095] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6095,
		.family = MV88E6XXX_FAMILY_6095,
		.name = "Marvell 88E6095/88E6095F",
		.num_databases = 256,
		.num_macs = 8192,
		.num_ports = 11,
		.num_internal_phys = 0,
		.max_vid = 4095,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 8,
		.atu_move_port_mask = 0xf,
		.multi_chip = true,
		.ops = &mv88e6095_ops,
	},

	[MV88E6097] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6097,
		.family = MV88E6XXX_FAMILY_6097,
		.name = "Marvell 88E6097/88E6097F",
		.num_databases = 4096,
		.num_macs = 8192,
		.num_ports = 11,
		.num_internal_phys = 8,
		.max_vid = 4095,
		.max_sid = 63,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 8,
		.g2_irqs = 10,
		.atu_move_port_mask = 0xf,
		.pvt = true,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_SUPPORTED,
		.ops = &mv88e6097_ops,
	},

	[MV88E6123] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6123,
		.family = MV88E6XXX_FAMILY_6165,
		.name = "Marvell 88E6123",
		.num_databases = 4096,
		.num_macs = 1024,
		.num_ports = 3,
		.num_internal_phys = 5,
		.max_vid = 4095,
		.max_sid = 63,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.g2_irqs = 10,
		.atu_move_port_mask = 0xf,
		.pvt = true,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_SUPPORTED,
		.ops = &mv88e6123_ops,
	},

	[MV88E6131] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6131,
		.family = MV88E6XXX_FAMILY_6185,
		.name = "Marvell 88E6131",
		.num_databases = 256,
		.num_macs = 8192,
		.num_ports = 8,
		.num_internal_phys = 0,
		.max_vid = 4095,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.atu_move_port_mask = 0xf,
		.multi_chip = true,
		.ops = &mv88e6131_ops,
	},

	[MV88E6141] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6141,
		.family = MV88E6XXX_FAMILY_6341,
		.name = "Marvell 88E6141",
		.num_databases = 4096,
		.num_macs = 2048,
		.num_ports = 6,
		.num_internal_phys = 5,
		.num_gpio = 11,
		.max_vid = 4095,
		.max_sid = 63,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x10,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 3750,
		.atu_move_port_mask = 0x1f,
		.g1_irqs = 9,
		.g2_irqs = 10,
		.pvt = true,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_SUPPORTED,
		.ops = &mv88e6141_ops,
	},

	[MV88E6161] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6161,
		.family = MV88E6XXX_FAMILY_6165,
		.name = "Marvell 88E6161",
		.num_databases = 4096,
		.num_macs = 1024,
		.num_ports = 6,
		.num_internal_phys = 5,
		.max_vid = 4095,
		.max_sid = 63,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.g2_irqs = 10,
		.atu_move_port_mask = 0xf,
		.pvt = true,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_SUPPORTED,
		.ptp_support = true,
		.ops = &mv88e6161_ops,
	},

	[MV88E6165] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6165,
		.family = MV88E6XXX_FAMILY_6165,
		.name = "Marvell 88E6165",
		.num_databases = 4096,
		.num_macs = 8192,
		.num_ports = 6,
		.num_internal_phys = 0,
		.max_vid = 4095,
		.max_sid = 63,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.g2_irqs = 10,
		.atu_move_port_mask = 0xf,
		.pvt = true,
		.multi_chip = true,
		.ptp_support = true,
		.ops = &mv88e6165_ops,
	},

	[MV88E6171] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6171,
		.family = MV88E6XXX_FAMILY_6351,
		.name = "Marvell 88E6171",
		.num_databases = 4096,
		.num_macs = 8192,
		.num_ports = 7,
		.num_internal_phys = 5,
		.max_vid = 4095,
		.max_sid = 63,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.g2_irqs = 10,
		.atu_move_port_mask = 0xf,
		.pvt = true,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_SUPPORTED,
		.ops = &mv88e6171_ops,
	},

	[MV88E6172] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6172,
		.family = MV88E6XXX_FAMILY_6352,
		.name = "Marvell 88E6172",
		.num_databases = 4096,
		.num_macs = 8192,
		.num_ports = 7,
		.num_internal_phys = 5,
		.num_gpio = 15,
		.max_vid = 4095,
		.max_sid = 63,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.g2_irqs = 10,
		.atu_move_port_mask = 0xf,
		.pvt = true,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_SUPPORTED,
		.ops = &mv88e6172_ops,
	},

	[MV88E6175] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6175,
		.family = MV88E6XXX_FAMILY_6351,
		.name = "Marvell 88E6175",
		.num_databases = 4096,
		.num_macs = 8192,
		.num_ports = 7,
		.num_internal_phys = 5,
		.max_vid = 4095,
		.max_sid = 63,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.g2_irqs = 10,
		.atu_move_port_mask = 0xf,
		.pvt = true,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_SUPPORTED,
		.ops = &mv88e6175_ops,
	},

	[MV88E6176] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6176,
		.family = MV88E6XXX_FAMILY_6352,
		.name = "Marvell 88E6176",
		.num_databases = 4096,
		.num_macs = 8192,
		.num_ports = 7,
		.num_internal_phys = 5,
		.num_gpio = 15,
		.max_vid = 4095,
		.max_sid = 63,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.g2_irqs = 10,
		.atu_move_port_mask = 0xf,
		.pvt = true,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_SUPPORTED,
		.ops = &mv88e6176_ops,
	},

	[MV88E6185] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6185,
		.family = MV88E6XXX_FAMILY_6185,
		.name = "Marvell 88E6185",
		.num_databases = 256,
		.num_macs = 8192,
		.num_ports = 10,
		.num_internal_phys = 0,
		.max_vid = 4095,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 8,
		.atu_move_port_mask = 0xf,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_SUPPORTED,
		.ops = &mv88e6185_ops,
	},

	[MV88E6190] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6190,
		.family = MV88E6XXX_FAMILY_6390,
		.name = "Marvell 88E6190",
		.num_databases = 4096,
		.num_macs = 16384,
		.num_ports = 11,	/* 10 + Z80 */
		.num_internal_phys = 9,
		.num_gpio = 16,
		.max_vid = 8191,
		.max_sid = 63,
		.port_base_addr = 0x0,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 3750,
		.g1_irqs = 9,
		.g2_irqs = 14,
		.pvt = true,
		.multi_chip = true,
		.atu_move_port_mask = 0x1f,
		.ops = &mv88e6190_ops,
	},

	[MV88E6190X] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6190X,
		.family = MV88E6XXX_FAMILY_6390,
		.name = "Marvell 88E6190X",
		.num_databases = 4096,
		.num_macs = 16384,
		.num_ports = 11,	/* 10 + Z80 */
		.num_internal_phys = 9,
		.num_gpio = 16,
		.max_vid = 8191,
		.max_sid = 63,
		.port_base_addr = 0x0,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 3750,
		.g1_irqs = 9,
		.g2_irqs = 14,
		.atu_move_port_mask = 0x1f,
		.pvt = true,
		.multi_chip = true,
		.ops = &mv88e6190x_ops,
	},

	[MV88E6191] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6191,
		.family = MV88E6XXX_FAMILY_6390,
		.name = "Marvell 88E6191",
		.num_databases = 4096,
		.num_macs = 16384,
		.num_ports = 11,	/* 10 + Z80 */
		.num_internal_phys = 9,
		.max_vid = 8191,
		.max_sid = 63,
		.port_base_addr = 0x0,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 3750,
		.g1_irqs = 9,
		.g2_irqs = 14,
		.atu_move_port_mask = 0x1f,
		.pvt = true,
		.multi_chip = true,
		.ptp_support = true,
		.ops = &mv88e6191_ops,
	},

	[MV88E6191X] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6191X,
		.family = MV88E6XXX_FAMILY_6393,
		.name = "Marvell 88E6191X",
		.num_databases = 4096,
		.num_ports = 11,	/* 10 + Z80 */
		.num_internal_phys = 8,
		.internal_phys_offset = 1,
		.max_vid = 8191,
		.max_sid = 63,
		.port_base_addr = 0x0,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 3750,
		.g1_irqs = 10,
		.g2_irqs = 14,
		.atu_move_port_mask = 0x1f,
		.pvt = true,
		.multi_chip = true,
		.ptp_support = true,
		.ops = &mv88e6393x_ops,
	},

	[MV88E6193X] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6193X,
		.family = MV88E6XXX_FAMILY_6393,
		.name = "Marvell 88E6193X",
		.num_databases = 4096,
		.num_ports = 11,	/* 10 + Z80 */
		.num_internal_phys = 8,
		.internal_phys_offset = 1,
		.max_vid = 8191,
		.max_sid = 63,
		.port_base_addr = 0x0,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 3750,
		.g1_irqs = 10,
		.g2_irqs = 14,
		.atu_move_port_mask = 0x1f,
		.pvt = true,
		.multi_chip = true,
		.ptp_support = true,
		.ops = &mv88e6393x_ops,
	},

	[MV88E6220] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6220,
		.family = MV88E6XXX_FAMILY_6250,
		.name = "Marvell 88E6220",
		.num_databases = 64,

		/* Ports 2-4 are not routed to pins
		 * => usable ports 0, 1, 5, 6
		 */
		.num_ports = 7,
		.num_internal_phys = 2,
		.invalid_port_mask = BIT(2) | BIT(3) | BIT(4),
		.max_vid = 4095,
		.port_base_addr = 0x08,
		.phy_base_addr = 0x00,
		.global1_addr = 0x0f,
		.global2_addr = 0x07,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.g2_irqs = 10,
		.atu_move_port_mask = 0xf,
		.dual_chip = true,
		.ptp_support = true,
		.ops = &mv88e6250_ops,
	},

	[MV88E6240] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6240,
		.family = MV88E6XXX_FAMILY_6352,
		.name = "Marvell 88E6240",
		.num_databases = 4096,
		.num_macs = 8192,
		.num_ports = 7,
		.num_internal_phys = 5,
		.num_gpio = 15,
		.max_vid = 4095,
		.max_sid = 63,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.g2_irqs = 10,
		.atu_move_port_mask = 0xf,
		.pvt = true,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_SUPPORTED,
		.ptp_support = true,
		.ops = &mv88e6240_ops,
	},

	[MV88E6250] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6250,
		.family = MV88E6XXX_FAMILY_6250,
		.name = "Marvell 88E6250",
		.num_databases = 64,
		.num_ports = 7,
		.num_internal_phys = 5,
		.max_vid = 4095,
		.port_base_addr = 0x08,
		.phy_base_addr = 0x00,
		.global1_addr = 0x0f,
		.global2_addr = 0x07,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.g2_irqs = 10,
		.atu_move_port_mask = 0xf,
		.dual_chip = true,
		.ptp_support = true,
		.ops = &mv88e6250_ops,
	},

	[MV88E6290] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6290,
		.family = MV88E6XXX_FAMILY_6390,
		.name = "Marvell 88E6290",
		.num_databases = 4096,
		.num_ports = 11,	/* 10 + Z80 */
		.num_internal_phys = 9,
		.num_gpio = 16,
		.max_vid = 8191,
		.max_sid = 63,
		.port_base_addr = 0x0,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 3750,
		.g1_irqs = 9,
		.g2_irqs = 14,
		.atu_move_port_mask = 0x1f,
		.pvt = true,
		.multi_chip = true,
		.ptp_support = true,
		.ops = &mv88e6290_ops,
	},

	[MV88E6320] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6320,
		.family = MV88E6XXX_FAMILY_6320,
		.name = "Marvell 88E6320",
		.num_databases = 4096,
		.num_macs = 8192,
		.num_ports = 7,
		.num_internal_phys = 5,
		.num_gpio = 15,
		.max_vid = 4095,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 8,
		.g2_irqs = 10,
		.atu_move_port_mask = 0xf,
		.pvt = true,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_SUPPORTED,
		.ptp_support = true,
		.ops = &mv88e6320_ops,
	},

	[MV88E6321] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6321,
		.family = MV88E6XXX_FAMILY_6320,
		.name = "Marvell 88E6321",
		.num_databases = 4096,
		.num_macs = 8192,
		.num_ports = 7,
		.num_internal_phys = 5,
		.num_gpio = 15,
		.max_vid = 4095,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 8,
		.g2_irqs = 10,
		.atu_move_port_mask = 0xf,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_SUPPORTED,
		.ptp_support = true,
		.ops = &mv88e6321_ops,
	},

	[MV88E6341] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6341,
		.family = MV88E6XXX_FAMILY_6341,
		.name = "Marvell 88E6341",
		.num_databases = 4096,
		.num_macs = 2048,
		.num_internal_phys = 5,
		.num_ports = 6,
		.num_gpio = 11,
		.max_vid = 4095,
		.max_sid = 63,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x10,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 3750,
		.atu_move_port_mask = 0x1f,
		.g1_irqs = 9,
		.g2_irqs = 10,
		.pvt = true,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_SUPPORTED,
		.ptp_support = true,
		.ops = &mv88e6341_ops,
	},

	[MV88E6350] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6350,
		.family = MV88E6XXX_FAMILY_6351,
		.name = "Marvell 88E6350",
		.num_databases = 4096,
		.num_macs = 8192,
		.num_ports = 7,
		.num_internal_phys = 5,
		.max_vid = 4095,
		.max_sid = 63,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.g2_irqs = 10,
		.atu_move_port_mask = 0xf,
		.pvt = true,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_SUPPORTED,
		.ops = &mv88e6350_ops,
	},

	[MV88E6351] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6351,
		.family = MV88E6XXX_FAMILY_6351,
		.name = "Marvell 88E6351",
		.num_databases = 4096,
		.num_macs = 8192,
		.num_ports = 7,
		.num_internal_phys = 5,
		.max_vid = 4095,
		.max_sid = 63,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.g2_irqs = 10,
		.atu_move_port_mask = 0xf,
		.pvt = true,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_SUPPORTED,
		.ops = &mv88e6351_ops,
	},

	[MV88E6352] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6352,
		.family = MV88E6XXX_FAMILY_6352,
		.name = "Marvell 88E6352",
		.num_databases = 4096,
		.num_macs = 8192,
		.num_ports = 7,
		.num_internal_phys = 5,
		.num_gpio = 15,
		.max_vid = 4095,
		.max_sid = 63,
		.port_base_addr = 0x10,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 15000,
		.g1_irqs = 9,
		.g2_irqs = 10,
		.atu_move_port_mask = 0xf,
		.pvt = true,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_SUPPORTED,
		.ptp_support = true,
		.ops = &mv88e6352_ops,
	},
	[MV88E6361] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6361,
		.family = MV88E6XXX_FAMILY_6393,
		.name = "Marvell 88E6361",
		.num_databases = 4096,
		.num_macs = 16384,
		.num_ports = 11,
		/* Ports 1, 2 and 8 are not routed */
		.invalid_port_mask = BIT(1) | BIT(2) | BIT(8),
		.num_internal_phys = 5,
		.internal_phys_offset = 3,
		.max_vid = 4095,
		.max_sid = 63,
		.port_base_addr = 0x0,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 3750,
		.g1_irqs = 10,
		.g2_irqs = 14,
		.atu_move_port_mask = 0x1f,
		.pvt = true,
		.multi_chip = true,
		.ptp_support = true,
		.ops = &mv88e6393x_ops,
	},
	[MV88E6390] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6390,
		.family = MV88E6XXX_FAMILY_6390,
		.name = "Marvell 88E6390",
		.num_databases = 4096,
		.num_macs = 16384,
		.num_ports = 11,	/* 10 + Z80 */
		.num_internal_phys = 9,
		.num_gpio = 16,
		.max_vid = 8191,
		.max_sid = 63,
		.port_base_addr = 0x0,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 3750,
		.g1_irqs = 9,
		.g2_irqs = 14,
		.atu_move_port_mask = 0x1f,
		.pvt = true,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_UNDOCUMENTED,
		.ptp_support = true,
		.ops = &mv88e6390_ops,
	},
	[MV88E6390X] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6390X,
		.family = MV88E6XXX_FAMILY_6390,
		.name = "Marvell 88E6390X",
		.num_databases = 4096,
		.num_macs = 16384,
		.num_ports = 11,	/* 10 + Z80 */
		.num_internal_phys = 9,
		.num_gpio = 16,
		.max_vid = 8191,
		.max_sid = 63,
		.port_base_addr = 0x0,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 3750,
		.g1_irqs = 9,
		.g2_irqs = 14,
		.atu_move_port_mask = 0x1f,
		.pvt = true,
		.multi_chip = true,
		.edsa_support = MV88E6XXX_EDSA_UNDOCUMENTED,
		.ptp_support = true,
		.ops = &mv88e6390x_ops,
	},

	[MV88E6393X] = {
		.prod_num = MV88E6XXX_PORT_SWITCH_ID_PROD_6393X,
		.family = MV88E6XXX_FAMILY_6393,
		.name = "Marvell 88E6393X",
		.num_databases = 4096,
		.num_ports = 11,	/* 10 + Z80 */
		.num_internal_phys = 8,
		.internal_phys_offset = 1,
		.max_vid = 8191,
		.max_sid = 63,
		.port_base_addr = 0x0,
		.phy_base_addr = 0x0,
		.global1_addr = 0x1b,
		.global2_addr = 0x1c,
		.age_time_coeff = 3750,
		.g1_irqs = 10,
		.g2_irqs = 14,
		.atu_move_port_mask = 0x1f,
		.pvt = true,
		.multi_chip = true,
		.ptp_support = true,
		.ops = &mv88e6393x_ops,
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

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_port_read(chip, 0, MV88E6XXX_PORT_SWITCH_ID, &id);
	mv88e6xxx_reg_unlock(chip);
	if (err)
		return err;

	prod_num = id & MV88E6XXX_PORT_SWITCH_ID_PROD_MASK;
	rev = id & MV88E6XXX_PORT_SWITCH_ID_REV_MASK;

	info = mv88e6xxx_lookup_info(prod_num);
	if (!info)
		return -ENODEV;

	/* Update the compatible info with the probed one */
	chip->info = info;

	dev_info(chip->dev, "switch 0x%x detected: %s, revision %u\n",
		 chip->info->prod_num, chip->info->name, rev);

	return 0;
}

static int mv88e6xxx_single_chip_detect(struct mv88e6xxx_chip *chip,
					struct mdio_device *mdiodev)
{
	int err;

	/* dual_chip takes precedence over single/multi-chip modes */
	if (chip->info->dual_chip)
		return -EINVAL;

	/* If the mdio addr is 16 indicating the first port address of a switch
	 * (e.g. mv88e6*41) in single chip addressing mode the device may be
	 * configured in single chip addressing mode. Setup the smi access as
	 * single chip addressing mode and attempt to detect the model of the
	 * switch, if this fails the device is not configured in single chip
	 * addressing mode.
	 */
	if (mdiodev->addr != 16)
		return -EINVAL;

	err = mv88e6xxx_smi_init(chip, mdiodev->bus, 0);
	if (err)
		return err;

	return mv88e6xxx_detect(chip);
}

static struct mv88e6xxx_chip *mv88e6xxx_alloc_chip(struct device *dev)
{
	struct mv88e6xxx_chip *chip;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return NULL;

	chip->dev = dev;

	mutex_init(&chip->reg_lock);
	INIT_LIST_HEAD(&chip->mdios);
	idr_init(&chip->policies);
	INIT_LIST_HEAD(&chip->msts);

	return chip;
}

static enum dsa_tag_protocol mv88e6xxx_get_tag_protocol(struct dsa_switch *ds,
							int port,
							enum dsa_tag_protocol m)
{
	struct mv88e6xxx_chip *chip = ds->priv;

	return chip->tag_protocol;
}

static int mv88e6xxx_change_tag_protocol(struct dsa_switch *ds,
					 enum dsa_tag_protocol proto)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	enum dsa_tag_protocol old_protocol;
	struct dsa_port *cpu_dp;
	int err;

	switch (proto) {
	case DSA_TAG_PROTO_EDSA:
		switch (chip->info->edsa_support) {
		case MV88E6XXX_EDSA_UNSUPPORTED:
			return -EPROTONOSUPPORT;
		case MV88E6XXX_EDSA_UNDOCUMENTED:
			dev_warn(chip->dev, "Relying on undocumented EDSA tagging behavior\n");
			fallthrough;
		case MV88E6XXX_EDSA_SUPPORTED:
			break;
		}
		break;
	case DSA_TAG_PROTO_DSA:
		break;
	default:
		return -EPROTONOSUPPORT;
	}

	old_protocol = chip->tag_protocol;
	chip->tag_protocol = proto;

	mv88e6xxx_reg_lock(chip);
	dsa_switch_for_each_cpu_port(cpu_dp, ds) {
		err = mv88e6xxx_setup_port_mode(chip, cpu_dp->index);
		if (err) {
			mv88e6xxx_reg_unlock(chip);
			goto unwind;
		}
	}
	mv88e6xxx_reg_unlock(chip);

	return 0;

unwind:
	chip->tag_protocol = old_protocol;

	mv88e6xxx_reg_lock(chip);
	dsa_switch_for_each_cpu_port_continue_reverse(cpu_dp, ds)
		mv88e6xxx_setup_port_mode(chip, cpu_dp->index);
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static int mv88e6xxx_port_mdb_add(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_mdb *mdb,
				  struct dsa_db db)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_port_db_load_purge(chip, port, mdb->addr, mdb->vid,
					   MV88E6XXX_G1_ATU_DATA_STATE_MC_STATIC);
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static int mv88e6xxx_port_mdb_del(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_mdb *mdb,
				  struct dsa_db db)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_port_db_load_purge(chip, port, mdb->addr, mdb->vid, 0);
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static int mv88e6xxx_port_mirror_add(struct dsa_switch *ds, int port,
				     struct dsa_mall_mirror_tc_entry *mirror,
				     bool ingress,
				     struct netlink_ext_ack *extack)
{
	enum mv88e6xxx_egress_direction direction = ingress ?
						MV88E6XXX_EGRESS_DIR_INGRESS :
						MV88E6XXX_EGRESS_DIR_EGRESS;
	struct mv88e6xxx_chip *chip = ds->priv;
	bool other_mirrors = false;
	int i;
	int err;

	mutex_lock(&chip->reg_lock);
	if ((ingress ? chip->ingress_dest_port : chip->egress_dest_port) !=
	    mirror->to_local_port) {
		for (i = 0; i < mv88e6xxx_num_ports(chip); i++)
			other_mirrors |= ingress ?
					 chip->ports[i].mirror_ingress :
					 chip->ports[i].mirror_egress;

		/* Can't change egress port when other mirror is active */
		if (other_mirrors) {
			err = -EBUSY;
			goto out;
		}

		err = mv88e6xxx_set_egress_port(chip, direction,
						mirror->to_local_port);
		if (err)
			goto out;
	}

	err = mv88e6xxx_port_set_mirror(chip, port, direction, true);
out:
	mutex_unlock(&chip->reg_lock);

	return err;
}

static void mv88e6xxx_port_mirror_del(struct dsa_switch *ds, int port,
				      struct dsa_mall_mirror_tc_entry *mirror)
{
	enum mv88e6xxx_egress_direction direction = mirror->ingress ?
						MV88E6XXX_EGRESS_DIR_INGRESS :
						MV88E6XXX_EGRESS_DIR_EGRESS;
	struct mv88e6xxx_chip *chip = ds->priv;
	bool other_mirrors = false;
	int i;

	mutex_lock(&chip->reg_lock);
	if (mv88e6xxx_port_set_mirror(chip, port, direction, false))
		dev_err(ds->dev, "p%d: failed to disable mirroring\n", port);

	for (i = 0; i < mv88e6xxx_num_ports(chip); i++)
		other_mirrors |= mirror->ingress ?
				 chip->ports[i].mirror_ingress :
				 chip->ports[i].mirror_egress;

	/* Reset egress port when no other mirror is active */
	if (!other_mirrors) {
		if (mv88e6xxx_set_egress_port(chip, direction,
					      dsa_upstream_port(ds, port)))
			dev_err(ds->dev, "failed to set egress port\n");
	}

	mutex_unlock(&chip->reg_lock);
}

static int mv88e6xxx_port_pre_bridge_flags(struct dsa_switch *ds, int port,
					   struct switchdev_brport_flags flags,
					   struct netlink_ext_ack *extack)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	const struct mv88e6xxx_ops *ops;

	if (flags.mask & ~(BR_LEARNING | BR_FLOOD | BR_MCAST_FLOOD |
			   BR_BCAST_FLOOD | BR_PORT_LOCKED | BR_PORT_MAB))
		return -EINVAL;

	ops = chip->info->ops;

	if ((flags.mask & BR_FLOOD) && !ops->port_set_ucast_flood)
		return -EINVAL;

	if ((flags.mask & BR_MCAST_FLOOD) && !ops->port_set_mcast_flood)
		return -EINVAL;

	return 0;
}

static int mv88e6xxx_port_bridge_flags(struct dsa_switch *ds, int port,
				       struct switchdev_brport_flags flags,
				       struct netlink_ext_ack *extack)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err = 0;

	mv88e6xxx_reg_lock(chip);

	if (flags.mask & BR_LEARNING) {
		bool learning = !!(flags.val & BR_LEARNING);
		u16 pav = learning ? (1 << port) : 0;

		err = mv88e6xxx_port_set_assoc_vector(chip, port, pav);
		if (err)
			goto out;
	}

	if (flags.mask & BR_FLOOD) {
		bool unicast = !!(flags.val & BR_FLOOD);

		err = chip->info->ops->port_set_ucast_flood(chip, port,
							    unicast);
		if (err)
			goto out;
	}

	if (flags.mask & BR_MCAST_FLOOD) {
		bool multicast = !!(flags.val & BR_MCAST_FLOOD);

		err = chip->info->ops->port_set_mcast_flood(chip, port,
							    multicast);
		if (err)
			goto out;
	}

	if (flags.mask & BR_BCAST_FLOOD) {
		bool broadcast = !!(flags.val & BR_BCAST_FLOOD);

		err = mv88e6xxx_port_broadcast_sync(chip, port, broadcast);
		if (err)
			goto out;
	}

	if (flags.mask & BR_PORT_MAB) {
		bool mab = !!(flags.val & BR_PORT_MAB);

		mv88e6xxx_port_set_mab(chip, port, mab);
	}

	if (flags.mask & BR_PORT_LOCKED) {
		bool locked = !!(flags.val & BR_PORT_LOCKED);

		err = mv88e6xxx_port_set_lock(chip, port, locked);
		if (err)
			goto out;
	}
out:
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static bool mv88e6xxx_lag_can_offload(struct dsa_switch *ds,
				      struct dsa_lag lag,
				      struct netdev_lag_upper_info *info,
				      struct netlink_ext_ack *extack)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct dsa_port *dp;
	int members = 0;

	if (!mv88e6xxx_has_lag(chip)) {
		NL_SET_ERR_MSG_MOD(extack, "Chip does not support LAG offload");
		return false;
	}

	if (!lag.id)
		return false;

	dsa_lag_foreach_port(dp, ds->dst, &lag)
		/* Includes the port joining the LAG */
		members++;

	if (members > 8) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot offload more than 8 LAG ports");
		return false;
	}

	/* We could potentially relax this to include active
	 * backup in the future.
	 */
	if (info->tx_type != NETDEV_LAG_TX_TYPE_HASH) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Can only offload LAG using hash TX type");
		return false;
	}

	/* Ideally we would also validate that the hash type matches
	 * the hardware. Alas, this is always set to unknown on team
	 * interfaces.
	 */
	return true;
}

static int mv88e6xxx_lag_sync_map(struct dsa_switch *ds, struct dsa_lag lag)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct dsa_port *dp;
	u16 map = 0;
	int id;

	/* DSA LAG IDs are one-based, hardware is zero-based */
	id = lag.id - 1;

	/* Build the map of all ports to distribute flows destined for
	 * this LAG. This can be either a local user port, or a DSA
	 * port if the LAG port is on a remote chip.
	 */
	dsa_lag_foreach_port(dp, ds->dst, &lag)
		map |= BIT(dsa_towards_port(ds, dp->ds->index, dp->index));

	return mv88e6xxx_g2_trunk_mapping_write(chip, id, map);
}

static const u8 mv88e6xxx_lag_mask_table[8][8] = {
	/* Row number corresponds to the number of active members in a
	 * LAG. Each column states which of the eight hash buckets are
	 * mapped to the column:th port in the LAG.
	 *
	 * Example: In a LAG with three active ports, the second port
	 * ([2][1]) would be selected for traffic mapped to buckets
	 * 3,4,5 (0x38).
	 */
	{ 0xff,    0,    0,    0,    0,    0,    0,    0 },
	{ 0x0f, 0xf0,    0,    0,    0,    0,    0,    0 },
	{ 0x07, 0x38, 0xc0,    0,    0,    0,    0,    0 },
	{ 0x03, 0x0c, 0x30, 0xc0,    0,    0,    0,    0 },
	{ 0x03, 0x0c, 0x30, 0x40, 0x80,    0,    0,    0 },
	{ 0x03, 0x0c, 0x10, 0x20, 0x40, 0x80,    0,    0 },
	{ 0x03, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,    0 },
	{ 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 },
};

static void mv88e6xxx_lag_set_port_mask(u16 *mask, int port,
					int num_tx, int nth)
{
	u8 active = 0;
	int i;

	num_tx = num_tx <= 8 ? num_tx : 8;
	if (nth < num_tx)
		active = mv88e6xxx_lag_mask_table[num_tx - 1][nth];

	for (i = 0; i < 8; i++) {
		if (BIT(i) & active)
			mask[i] |= BIT(port);
	}
}

static int mv88e6xxx_lag_sync_masks(struct dsa_switch *ds)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	unsigned int id, num_tx;
	struct dsa_port *dp;
	struct dsa_lag *lag;
	int i, err, nth;
	u16 mask[8];
	u16 ivec;

	/* Assume no port is a member of any LAG. */
	ivec = BIT(mv88e6xxx_num_ports(chip)) - 1;

	/* Disable all masks for ports that _are_ members of a LAG. */
	dsa_switch_for_each_port(dp, ds) {
		if (!dp->lag)
			continue;

		ivec &= ~BIT(dp->index);
	}

	for (i = 0; i < 8; i++)
		mask[i] = ivec;

	/* Enable the correct subset of masks for all LAG ports that
	 * are in the Tx set.
	 */
	dsa_lags_foreach_id(id, ds->dst) {
		lag = dsa_lag_by_id(ds->dst, id);
		if (!lag)
			continue;

		num_tx = 0;
		dsa_lag_foreach_port(dp, ds->dst, lag) {
			if (dp->lag_tx_enabled)
				num_tx++;
		}

		if (!num_tx)
			continue;

		nth = 0;
		dsa_lag_foreach_port(dp, ds->dst, lag) {
			if (!dp->lag_tx_enabled)
				continue;

			if (dp->ds == ds)
				mv88e6xxx_lag_set_port_mask(mask, dp->index,
							    num_tx, nth);

			nth++;
		}
	}

	for (i = 0; i < 8; i++) {
		err = mv88e6xxx_g2_trunk_mask_write(chip, i, true, mask[i]);
		if (err)
			return err;
	}

	return 0;
}

static int mv88e6xxx_lag_sync_masks_map(struct dsa_switch *ds,
					struct dsa_lag lag)
{
	int err;

	err = mv88e6xxx_lag_sync_masks(ds);

	if (!err)
		err = mv88e6xxx_lag_sync_map(ds, lag);

	return err;
}

static int mv88e6xxx_port_lag_change(struct dsa_switch *ds, int port)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_lag_sync_masks(ds);
	mv88e6xxx_reg_unlock(chip);
	return err;
}

static int mv88e6xxx_port_lag_join(struct dsa_switch *ds, int port,
				   struct dsa_lag lag,
				   struct netdev_lag_upper_info *info,
				   struct netlink_ext_ack *extack)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err, id;

	if (!mv88e6xxx_lag_can_offload(ds, lag, info, extack))
		return -EOPNOTSUPP;

	/* DSA LAG IDs are one-based */
	id = lag.id - 1;

	mv88e6xxx_reg_lock(chip);

	err = mv88e6xxx_port_set_trunk(chip, port, true, id);
	if (err)
		goto err_unlock;

	err = mv88e6xxx_lag_sync_masks_map(ds, lag);
	if (err)
		goto err_clear_trunk;

	mv88e6xxx_reg_unlock(chip);
	return 0;

err_clear_trunk:
	mv88e6xxx_port_set_trunk(chip, port, false, 0);
err_unlock:
	mv88e6xxx_reg_unlock(chip);
	return err;
}

static int mv88e6xxx_port_lag_leave(struct dsa_switch *ds, int port,
				    struct dsa_lag lag)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err_sync, err_trunk;

	mv88e6xxx_reg_lock(chip);
	err_sync = mv88e6xxx_lag_sync_masks_map(ds, lag);
	err_trunk = mv88e6xxx_port_set_trunk(chip, port, false, 0);
	mv88e6xxx_reg_unlock(chip);
	return err_sync ? : err_trunk;
}

static int mv88e6xxx_crosschip_lag_change(struct dsa_switch *ds, int sw_index,
					  int port)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_lag_sync_masks(ds);
	mv88e6xxx_reg_unlock(chip);
	return err;
}

static int mv88e6xxx_crosschip_lag_join(struct dsa_switch *ds, int sw_index,
					int port, struct dsa_lag lag,
					struct netdev_lag_upper_info *info,
					struct netlink_ext_ack *extack)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	if (!mv88e6xxx_lag_can_offload(ds, lag, info, extack))
		return -EOPNOTSUPP;

	mv88e6xxx_reg_lock(chip);

	err = mv88e6xxx_lag_sync_masks_map(ds, lag);
	if (err)
		goto unlock;

	err = mv88e6xxx_pvt_map(chip, sw_index, port);

unlock:
	mv88e6xxx_reg_unlock(chip);
	return err;
}

static int mv88e6xxx_crosschip_lag_leave(struct dsa_switch *ds, int sw_index,
					 int port, struct dsa_lag lag)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err_sync, err_pvt;

	mv88e6xxx_reg_lock(chip);
	err_sync = mv88e6xxx_lag_sync_masks_map(ds, lag);
	err_pvt = mv88e6xxx_pvt_map(chip, sw_index, port);
	mv88e6xxx_reg_unlock(chip);
	return err_sync ? : err_pvt;
}

static const struct dsa_switch_ops mv88e6xxx_switch_ops = {
	.get_tag_protocol	= mv88e6xxx_get_tag_protocol,
	.change_tag_protocol	= mv88e6xxx_change_tag_protocol,
	.setup			= mv88e6xxx_setup,
	.teardown		= mv88e6xxx_teardown,
	.port_setup		= mv88e6xxx_port_setup,
	.port_teardown		= mv88e6xxx_port_teardown,
	.phylink_get_caps	= mv88e6xxx_get_caps,
	.phylink_mac_link_state	= mv88e6xxx_serdes_pcs_get_state,
	.phylink_mac_prepare	= mv88e6xxx_mac_prepare,
	.phylink_mac_config	= mv88e6xxx_mac_config,
	.phylink_mac_finish	= mv88e6xxx_mac_finish,
	.phylink_mac_an_restart	= mv88e6xxx_serdes_pcs_an_restart,
	.phylink_mac_link_down	= mv88e6xxx_mac_link_down,
	.phylink_mac_link_up	= mv88e6xxx_mac_link_up,
	.get_strings		= mv88e6xxx_get_strings,
	.get_ethtool_stats	= mv88e6xxx_get_ethtool_stats,
	.get_sset_count		= mv88e6xxx_get_sset_count,
	.port_enable		= mv88e6xxx_port_enable,
	.port_disable		= mv88e6xxx_port_disable,
	.port_max_mtu		= mv88e6xxx_get_max_mtu,
	.port_change_mtu	= mv88e6xxx_change_mtu,
	.get_mac_eee		= mv88e6xxx_get_mac_eee,
	.set_mac_eee		= mv88e6xxx_set_mac_eee,
	.get_eeprom_len		= mv88e6xxx_get_eeprom_len,
	.get_eeprom		= mv88e6xxx_get_eeprom,
	.set_eeprom		= mv88e6xxx_set_eeprom,
	.get_regs_len		= mv88e6xxx_get_regs_len,
	.get_regs		= mv88e6xxx_get_regs,
	.get_rxnfc		= mv88e6xxx_get_rxnfc,
	.set_rxnfc		= mv88e6xxx_set_rxnfc,
	.set_ageing_time	= mv88e6xxx_set_ageing_time,
	.port_bridge_join	= mv88e6xxx_port_bridge_join,
	.port_bridge_leave	= mv88e6xxx_port_bridge_leave,
	.port_pre_bridge_flags	= mv88e6xxx_port_pre_bridge_flags,
	.port_bridge_flags	= mv88e6xxx_port_bridge_flags,
	.port_stp_state_set	= mv88e6xxx_port_stp_state_set,
	.port_mst_state_set	= mv88e6xxx_port_mst_state_set,
	.port_fast_age		= mv88e6xxx_port_fast_age,
	.port_vlan_fast_age	= mv88e6xxx_port_vlan_fast_age,
	.port_vlan_filtering	= mv88e6xxx_port_vlan_filtering,
	.port_vlan_add		= mv88e6xxx_port_vlan_add,
	.port_vlan_del		= mv88e6xxx_port_vlan_del,
	.vlan_msti_set		= mv88e6xxx_vlan_msti_set,
	.port_fdb_add		= mv88e6xxx_port_fdb_add,
	.port_fdb_del		= mv88e6xxx_port_fdb_del,
	.port_fdb_dump		= mv88e6xxx_port_fdb_dump,
	.port_mdb_add		= mv88e6xxx_port_mdb_add,
	.port_mdb_del		= mv88e6xxx_port_mdb_del,
	.port_mirror_add	= mv88e6xxx_port_mirror_add,
	.port_mirror_del	= mv88e6xxx_port_mirror_del,
	.crosschip_bridge_join	= mv88e6xxx_crosschip_bridge_join,
	.crosschip_bridge_leave	= mv88e6xxx_crosschip_bridge_leave,
	.port_hwtstamp_set	= mv88e6xxx_port_hwtstamp_set,
	.port_hwtstamp_get	= mv88e6xxx_port_hwtstamp_get,
	.port_txtstamp		= mv88e6xxx_port_txtstamp,
	.port_rxtstamp		= mv88e6xxx_port_rxtstamp,
	.get_ts_info		= mv88e6xxx_get_ts_info,
	.devlink_param_get	= mv88e6xxx_devlink_param_get,
	.devlink_param_set	= mv88e6xxx_devlink_param_set,
	.devlink_info_get	= mv88e6xxx_devlink_info_get,
	.port_lag_change	= mv88e6xxx_port_lag_change,
	.port_lag_join		= mv88e6xxx_port_lag_join,
	.port_lag_leave		= mv88e6xxx_port_lag_leave,
	.crosschip_lag_change	= mv88e6xxx_crosschip_lag_change,
	.crosschip_lag_join	= mv88e6xxx_crosschip_lag_join,
	.crosschip_lag_leave	= mv88e6xxx_crosschip_lag_leave,
};

static int mv88e6xxx_register_switch(struct mv88e6xxx_chip *chip)
{
	struct device *dev = chip->dev;
	struct dsa_switch *ds;

	ds = devm_kzalloc(dev, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return -ENOMEM;

	ds->dev = dev;
	ds->num_ports = mv88e6xxx_num_ports(chip);
	ds->priv = chip;
	ds->dev = dev;
	ds->ops = &mv88e6xxx_switch_ops;
	ds->ageing_time_min = chip->info->age_time_coeff;
	ds->ageing_time_max = chip->info->age_time_coeff * U8_MAX;

	/* Some chips support up to 32, but that requires enabling the
	 * 5-bit port mode, which we do not support. 640k^W16 ought to
	 * be enough for anyone.
	 */
	ds->num_lag_ids = mv88e6xxx_has_lag(chip) ? 16 : 0;

	dev_set_drvdata(dev, ds);

	return dsa_register_switch(ds);
}

static void mv88e6xxx_unregister_switch(struct mv88e6xxx_chip *chip)
{
	dsa_unregister_switch(chip->ds);
}

static const void *pdata_device_get_match_data(struct device *dev)
{
	const struct of_device_id *matches = dev->driver->of_match_table;
	const struct dsa_mv88e6xxx_pdata *pdata = dev->platform_data;

	for (; matches->name[0] || matches->type[0] || matches->compatible[0];
	     matches++) {
		if (!strcmp(pdata->compatible, matches->compatible))
			return matches->data;
	}
	return NULL;
}

/* There is no suspend to RAM support at DSA level yet, the switch configuration
 * would be lost after a power cycle so prevent it to be suspended.
 */
static int __maybe_unused mv88e6xxx_suspend(struct device *dev)
{
	return -EOPNOTSUPP;
}

static int __maybe_unused mv88e6xxx_resume(struct device *dev)
{
	return 0;
}

static SIMPLE_DEV_PM_OPS(mv88e6xxx_pm_ops, mv88e6xxx_suspend, mv88e6xxx_resume);

static int mv88e6xxx_probe(struct mdio_device *mdiodev)
{
	struct dsa_mv88e6xxx_pdata *pdata = mdiodev->dev.platform_data;
	const struct mv88e6xxx_info *compat_info = NULL;
	struct device *dev = &mdiodev->dev;
	struct device_node *np = dev->of_node;
	struct mv88e6xxx_chip *chip;
	int port;
	int err;

	if (!np && !pdata)
		return -EINVAL;

	if (np)
		compat_info = of_device_get_match_data(dev);

	if (pdata) {
		compat_info = pdata_device_get_match_data(dev);

		if (!pdata->netdev)
			return -EINVAL;

		for (port = 0; port < DSA_MAX_PORTS; port++) {
			if (!(pdata->enabled_ports & (1 << port)))
				continue;
			if (strcmp(pdata->cd.port_names[port], "cpu"))
				continue;
			pdata->cd.netdev[port] = &pdata->netdev->dev;
			break;
		}
	}

	if (!compat_info)
		return -EINVAL;

	chip = mv88e6xxx_alloc_chip(dev);
	if (!chip) {
		err = -ENOMEM;
		goto out;
	}

	chip->info = compat_info;

	chip->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(chip->reset)) {
		err = PTR_ERR(chip->reset);
		goto out;
	}
	if (chip->reset)
		usleep_range(10000, 20000);

	/* Detect if the device is configured in single chip addressing mode,
	 * otherwise continue with address specific smi init/detection.
	 */
	err = mv88e6xxx_single_chip_detect(chip, mdiodev);
	if (err) {
		err = mv88e6xxx_smi_init(chip, mdiodev->bus, mdiodev->addr);
		if (err)
			goto out;

		err = mv88e6xxx_detect(chip);
		if (err)
			goto out;
	}

	if (chip->info->edsa_support == MV88E6XXX_EDSA_SUPPORTED)
		chip->tag_protocol = DSA_TAG_PROTO_EDSA;
	else
		chip->tag_protocol = DSA_TAG_PROTO_DSA;

	mv88e6xxx_phy_init(chip);

	if (chip->info->ops->get_eeprom) {
		if (np)
			of_property_read_u32(np, "eeprom-length",
					     &chip->eeprom_len);
		else
			chip->eeprom_len = pdata->eeprom_len;
	}

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_switch_reset(chip);
	mv88e6xxx_reg_unlock(chip);
	if (err)
		goto out;

	if (np) {
		chip->irq = of_irq_get(np, 0);
		if (chip->irq == -EPROBE_DEFER) {
			err = chip->irq;
			goto out;
		}
	}

	if (pdata)
		chip->irq = pdata->irq;

	/* Has to be performed before the MDIO bus is created, because
	 * the PHYs will link their interrupts to these interrupt
	 * controllers
	 */
	mv88e6xxx_reg_lock(chip);
	if (chip->irq > 0)
		err = mv88e6xxx_g1_irq_setup(chip);
	else
		err = mv88e6xxx_irq_poll_setup(chip);
	mv88e6xxx_reg_unlock(chip);

	if (err)
		goto out;

	if (chip->info->g2_irqs > 0) {
		err = mv88e6xxx_g2_irq_setup(chip);
		if (err)
			goto out_g1_irq;
	}

	err = mv88e6xxx_g1_atu_prob_irq_setup(chip);
	if (err)
		goto out_g2_irq;

	err = mv88e6xxx_g1_vtu_prob_irq_setup(chip);
	if (err)
		goto out_g1_atu_prob_irq;

	err = mv88e6xxx_register_switch(chip);
	if (err)
		goto out_g1_vtu_prob_irq;

	return 0;

out_g1_vtu_prob_irq:
	mv88e6xxx_g1_vtu_prob_irq_free(chip);
out_g1_atu_prob_irq:
	mv88e6xxx_g1_atu_prob_irq_free(chip);
out_g2_irq:
	if (chip->info->g2_irqs > 0)
		mv88e6xxx_g2_irq_free(chip);
out_g1_irq:
	if (chip->irq > 0)
		mv88e6xxx_g1_irq_free(chip);
	else
		mv88e6xxx_irq_poll_free(chip);
out:
	if (pdata)
		dev_put(pdata->netdev);

	return err;
}

static void mv88e6xxx_remove(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);
	struct mv88e6xxx_chip *chip;

	if (!ds)
		return;

	chip = ds->priv;

	if (chip->info->ptp_support) {
		mv88e6xxx_hwtstamp_free(chip);
		mv88e6xxx_ptp_free(chip);
	}

	mv88e6xxx_phy_destroy(chip);
	mv88e6xxx_unregister_switch(chip);

	mv88e6xxx_g1_vtu_prob_irq_free(chip);
	mv88e6xxx_g1_atu_prob_irq_free(chip);

	if (chip->info->g2_irqs > 0)
		mv88e6xxx_g2_irq_free(chip);

	if (chip->irq > 0)
		mv88e6xxx_g1_irq_free(chip);
	else
		mv88e6xxx_irq_poll_free(chip);
}

static void mv88e6xxx_shutdown(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);

	if (!ds)
		return;

	dsa_switch_shutdown(ds);

	dev_set_drvdata(&mdiodev->dev, NULL);
}

static const struct of_device_id mv88e6xxx_of_match[] = {
	{
		.compatible = "marvell,mv88e6085",
		.data = &mv88e6xxx_table[MV88E6085],
	},
	{
		.compatible = "marvell,mv88e6190",
		.data = &mv88e6xxx_table[MV88E6190],
	},
	{
		.compatible = "marvell,mv88e6250",
		.data = &mv88e6xxx_table[MV88E6250],
	},
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, mv88e6xxx_of_match);

static struct mdio_driver mv88e6xxx_driver = {
	.probe	= mv88e6xxx_probe,
	.remove = mv88e6xxx_remove,
	.shutdown = mv88e6xxx_shutdown,
	.mdiodrv.driver = {
		.name = "mv88e6085",
		.of_match_table = mv88e6xxx_of_match,
		.pm = &mv88e6xxx_pm_ops,
	},
};

mdio_module_driver(mv88e6xxx_driver);

MODULE_AUTHOR("Lennert Buytenhek <buytenh@wantstofly.org>");
MODULE_DESCRIPTION("Driver for Marvell 88E6XXX ethernet switch chips");
MODULE_LICENSE("GPL");
