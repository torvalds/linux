// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Broadcom Starfighter 2 DSA switch driver
 *
 * Copyright (C) 2014, Broadcom Corporation
 */

#include <linux/list.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/phylink.h>
#include <linux/mii.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <net/dsa.h>
#include <linux/ethtool.h>
#include <linux/if_bridge.h>
#include <linux/brcmphy.h>
#include <linux/etherdevice.h>
#include <linux/platform_data/b53.h>

#include "bcm_sf2.h"
#include "bcm_sf2_regs.h"
#include "b53/b53_priv.h"
#include "b53/b53_regs.h"

static u16 bcm_sf2_reg_rgmii_cntrl(struct bcm_sf2_priv *priv, int port)
{
	switch (priv->type) {
	case BCM4908_DEVICE_ID:
		switch (port) {
		case 7:
			return REG_RGMII_11_CNTRL;
		default:
			break;
		}
		break;
	default:
		switch (port) {
		case 0:
			return REG_RGMII_0_CNTRL;
		case 1:
			return REG_RGMII_1_CNTRL;
		case 2:
			return REG_RGMII_2_CNTRL;
		default:
			break;
		}
	}

	WARN_ONCE(1, "Unsupported port %d\n", port);

	/* RO fallback reg */
	return REG_SWITCH_STATUS;
}

static u16 bcm_sf2_reg_led_base(struct bcm_sf2_priv *priv, int port)
{
	switch (port) {
	case 0:
		return REG_LED_0_CNTRL;
	case 1:
		return REG_LED_1_CNTRL;
	case 2:
		return REG_LED_2_CNTRL;
	}

	switch (priv->type) {
	case BCM4908_DEVICE_ID:
		switch (port) {
		case 3:
			return REG_LED_3_CNTRL;
		case 7:
			return REG_LED_4_CNTRL;
		default:
			break;
		}
		break;
	default:
		break;
	}

	WARN_ONCE(1, "Unsupported port %d\n", port);

	/* RO fallback reg */
	return REG_SWITCH_STATUS;
}

/* Return the number of active ports, not counting the IMP (CPU) port */
static unsigned int bcm_sf2_num_active_ports(struct dsa_switch *ds)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	unsigned int port, count = 0;

	for (port = 0; port < ds->num_ports; port++) {
		if (dsa_is_cpu_port(ds, port))
			continue;
		if (priv->port_sts[port].enabled)
			count++;
	}

	return count;
}

static void bcm_sf2_recalc_clock(struct dsa_switch *ds)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	unsigned long new_rate;
	unsigned int ports_active;
	/* Frequenty in Mhz */
	static const unsigned long rate_table[] = {
		59220000,
		60820000,
		62500000,
		62500000,
	};

	ports_active = bcm_sf2_num_active_ports(ds);
	if (ports_active == 0 || !priv->clk_mdiv)
		return;

	/* If we overflow our table, just use the recommended operational
	 * frequency
	 */
	if (ports_active > ARRAY_SIZE(rate_table))
		new_rate = 90000000;
	else
		new_rate = rate_table[ports_active - 1];
	clk_set_rate(priv->clk_mdiv, new_rate);
}

static void bcm_sf2_imp_setup(struct dsa_switch *ds, int port)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	unsigned int i;
	u32 reg, offset;

	/* Enable the port memories */
	reg = core_readl(priv, CORE_MEM_PSM_VDD_CTRL);
	reg &= ~P_TXQ_PSM_VDD(port);
	core_writel(priv, reg, CORE_MEM_PSM_VDD_CTRL);

	/* Enable forwarding */
	core_writel(priv, SW_FWDG_EN, CORE_SWMODE);

	/* Enable IMP port in dumb mode */
	reg = core_readl(priv, CORE_SWITCH_CTRL);
	reg |= MII_DUMB_FWDG_EN;
	core_writel(priv, reg, CORE_SWITCH_CTRL);

	/* Configure Traffic Class to QoS mapping, allow each priority to map
	 * to a different queue number
	 */
	reg = core_readl(priv, CORE_PORT_TC2_QOS_MAP_PORT(port));
	for (i = 0; i < SF2_NUM_EGRESS_QUEUES; i++)
		reg |= i << (PRT_TO_QID_SHIFT * i);
	core_writel(priv, reg, CORE_PORT_TC2_QOS_MAP_PORT(port));

	b53_brcm_hdr_setup(ds, port);

	if (port == 8) {
		if (priv->type == BCM4908_DEVICE_ID ||
		    priv->type == BCM7445_DEVICE_ID)
			offset = CORE_STS_OVERRIDE_IMP;
		else
			offset = CORE_STS_OVERRIDE_IMP2;

		/* Force link status for IMP port */
		reg = core_readl(priv, offset);
		reg |= (MII_SW_OR | LINK_STS);
		if (priv->type == BCM4908_DEVICE_ID)
			reg |= GMII_SPEED_UP_2G;
		else
			reg &= ~GMII_SPEED_UP_2G;
		core_writel(priv, reg, offset);

		/* Enable Broadcast, Multicast, Unicast forwarding to IMP port */
		reg = core_readl(priv, CORE_IMP_CTL);
		reg |= (RX_BCST_EN | RX_MCST_EN | RX_UCST_EN);
		reg &= ~(RX_DIS | TX_DIS);
		core_writel(priv, reg, CORE_IMP_CTL);
	} else {
		reg = core_readl(priv, CORE_G_PCTL_PORT(port));
		reg &= ~(RX_DIS | TX_DIS);
		core_writel(priv, reg, CORE_G_PCTL_PORT(port));
	}

	priv->port_sts[port].enabled = true;
}

static void bcm_sf2_gphy_enable_set(struct dsa_switch *ds, bool enable)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	u32 reg;

	reg = reg_readl(priv, REG_SPHY_CNTRL);
	if (enable) {
		reg |= PHY_RESET;
		reg &= ~(EXT_PWR_DOWN | IDDQ_BIAS | IDDQ_GLOBAL_PWR | CK25_DIS);
		reg_writel(priv, reg, REG_SPHY_CNTRL);
		udelay(21);
		reg = reg_readl(priv, REG_SPHY_CNTRL);
		reg &= ~PHY_RESET;
	} else {
		reg |= EXT_PWR_DOWN | IDDQ_BIAS | PHY_RESET;
		reg_writel(priv, reg, REG_SPHY_CNTRL);
		mdelay(1);
		reg |= CK25_DIS;
	}
	reg_writel(priv, reg, REG_SPHY_CNTRL);

	/* Use PHY-driven LED signaling */
	if (!enable) {
		u16 led_ctrl = bcm_sf2_reg_led_base(priv, 0);

		if (priv->type == BCM7278_DEVICE_ID ||
		    priv->type == BCM7445_DEVICE_ID) {
			reg = reg_led_readl(priv, led_ctrl, 0);
			reg |= LED_CNTRL_SPDLNK_SRC_SEL;
			reg_led_writel(priv, reg, led_ctrl, 0);
		}
	}
}

static inline void bcm_sf2_port_intr_enable(struct bcm_sf2_priv *priv,
					    int port)
{
	unsigned int off;

	switch (port) {
	case 7:
		off = P7_IRQ_OFF;
		break;
	case 0:
		/* Port 0 interrupts are located on the first bank */
		intrl2_0_mask_clear(priv, P_IRQ_MASK(P0_IRQ_OFF));
		return;
	default:
		off = P_IRQ_OFF(port);
		break;
	}

	intrl2_1_mask_clear(priv, P_IRQ_MASK(off));
}

static inline void bcm_sf2_port_intr_disable(struct bcm_sf2_priv *priv,
					     int port)
{
	unsigned int off;

	switch (port) {
	case 7:
		off = P7_IRQ_OFF;
		break;
	case 0:
		/* Port 0 interrupts are located on the first bank */
		intrl2_0_mask_set(priv, P_IRQ_MASK(P0_IRQ_OFF));
		intrl2_0_writel(priv, P_IRQ_MASK(P0_IRQ_OFF), INTRL2_CPU_CLEAR);
		return;
	default:
		off = P_IRQ_OFF(port);
		break;
	}

	intrl2_1_mask_set(priv, P_IRQ_MASK(off));
	intrl2_1_writel(priv, P_IRQ_MASK(off), INTRL2_CPU_CLEAR);
}

static int bcm_sf2_port_setup(struct dsa_switch *ds, int port,
			      struct phy_device *phy)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	unsigned int i;
	u32 reg;

	if (!dsa_is_user_port(ds, port))
		return 0;

	priv->port_sts[port].enabled = true;

	bcm_sf2_recalc_clock(ds);

	/* Clear the memory power down */
	reg = core_readl(priv, CORE_MEM_PSM_VDD_CTRL);
	reg &= ~P_TXQ_PSM_VDD(port);
	core_writel(priv, reg, CORE_MEM_PSM_VDD_CTRL);

	/* Enable Broadcom tags for that port if requested */
	if (priv->brcm_tag_mask & BIT(port))
		b53_brcm_hdr_setup(ds, port);

	/* Configure Traffic Class to QoS mapping, allow each priority to map
	 * to a different queue number
	 */
	reg = core_readl(priv, CORE_PORT_TC2_QOS_MAP_PORT(port));
	for (i = 0; i < SF2_NUM_EGRESS_QUEUES; i++)
		reg |= i << (PRT_TO_QID_SHIFT * i);
	core_writel(priv, reg, CORE_PORT_TC2_QOS_MAP_PORT(port));

	/* Re-enable the GPHY and re-apply workarounds */
	if (priv->int_phy_mask & 1 << port && priv->hw_params.num_gphy == 1) {
		bcm_sf2_gphy_enable_set(ds, true);
		if (phy) {
			/* if phy_stop() has been called before, phy
			 * will be in halted state, and phy_start()
			 * will call resume.
			 *
			 * the resume path does not configure back
			 * autoneg settings, and since we hard reset
			 * the phy manually here, we need to reset the
			 * state machine also.
			 */
			phy->state = PHY_READY;
			phy_init_hw(phy);
		}
	}

	/* Enable MoCA port interrupts to get notified */
	if (port == priv->moca_port)
		bcm_sf2_port_intr_enable(priv, port);

	/* Set per-queue pause threshold to 32 */
	core_writel(priv, 32, CORE_TXQ_THD_PAUSE_QN_PORT(port));

	/* Set ACB threshold to 24 */
	for (i = 0; i < SF2_NUM_EGRESS_QUEUES; i++) {
		reg = acb_readl(priv, ACB_QUEUE_CFG(port *
						    SF2_NUM_EGRESS_QUEUES + i));
		reg &= ~XOFF_THRESHOLD_MASK;
		reg |= 24;
		acb_writel(priv, reg, ACB_QUEUE_CFG(port *
						    SF2_NUM_EGRESS_QUEUES + i));
	}

	return b53_enable_port(ds, port, phy);
}

static void bcm_sf2_port_disable(struct dsa_switch *ds, int port)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	u32 reg;

	/* Disable learning while in WoL mode */
	if (priv->wol_ports_mask & (1 << port)) {
		reg = core_readl(priv, CORE_DIS_LEARN);
		reg |= BIT(port);
		core_writel(priv, reg, CORE_DIS_LEARN);
		return;
	}

	if (port == priv->moca_port)
		bcm_sf2_port_intr_disable(priv, port);

	if (priv->int_phy_mask & 1 << port && priv->hw_params.num_gphy == 1)
		bcm_sf2_gphy_enable_set(ds, false);

	b53_disable_port(ds, port);

	/* Power down the port memory */
	reg = core_readl(priv, CORE_MEM_PSM_VDD_CTRL);
	reg |= P_TXQ_PSM_VDD(port);
	core_writel(priv, reg, CORE_MEM_PSM_VDD_CTRL);

	priv->port_sts[port].enabled = false;

	bcm_sf2_recalc_clock(ds);
}


static int bcm_sf2_sw_indir_rw(struct bcm_sf2_priv *priv, int op, int addr,
			       int regnum, u16 val)
{
	int ret = 0;
	u32 reg;

	reg = reg_readl(priv, REG_SWITCH_CNTRL);
	reg |= MDIO_MASTER_SEL;
	reg_writel(priv, reg, REG_SWITCH_CNTRL);

	/* Page << 8 | offset */
	reg = 0x70;
	reg <<= 2;
	core_writel(priv, addr, reg);

	/* Page << 8 | offset */
	reg = 0x80 << 8 | regnum << 1;
	reg <<= 2;

	if (op)
		ret = core_readl(priv, reg);
	else
		core_writel(priv, val, reg);

	reg = reg_readl(priv, REG_SWITCH_CNTRL);
	reg &= ~MDIO_MASTER_SEL;
	reg_writel(priv, reg, REG_SWITCH_CNTRL);

	return ret & 0xffff;
}

static int bcm_sf2_sw_mdio_read(struct mii_bus *bus, int addr, int regnum)
{
	struct bcm_sf2_priv *priv = bus->priv;

	/* Intercept reads from Broadcom pseudo-PHY address, else, send
	 * them to our master MDIO bus controller
	 */
	if (addr == BRCM_PSEUDO_PHY_ADDR && priv->indir_phy_mask & BIT(addr))
		return bcm_sf2_sw_indir_rw(priv, 1, addr, regnum, 0);
	else
		return mdiobus_read_nested(priv->master_mii_bus, addr, regnum);
}

static int bcm_sf2_sw_mdio_write(struct mii_bus *bus, int addr, int regnum,
				 u16 val)
{
	struct bcm_sf2_priv *priv = bus->priv;

	/* Intercept writes to the Broadcom pseudo-PHY address, else,
	 * send them to our master MDIO bus controller
	 */
	if (addr == BRCM_PSEUDO_PHY_ADDR && priv->indir_phy_mask & BIT(addr))
		return bcm_sf2_sw_indir_rw(priv, 0, addr, regnum, val);
	else
		return mdiobus_write_nested(priv->master_mii_bus, addr,
				regnum, val);
}

static irqreturn_t bcm_sf2_switch_0_isr(int irq, void *dev_id)
{
	struct dsa_switch *ds = dev_id;
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);

	priv->irq0_stat = intrl2_0_readl(priv, INTRL2_CPU_STATUS) &
				~priv->irq0_mask;
	intrl2_0_writel(priv, priv->irq0_stat, INTRL2_CPU_CLEAR);

	return IRQ_HANDLED;
}

static irqreturn_t bcm_sf2_switch_1_isr(int irq, void *dev_id)
{
	struct dsa_switch *ds = dev_id;
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);

	priv->irq1_stat = intrl2_1_readl(priv, INTRL2_CPU_STATUS) &
				~priv->irq1_mask;
	intrl2_1_writel(priv, priv->irq1_stat, INTRL2_CPU_CLEAR);

	if (priv->irq1_stat & P_LINK_UP_IRQ(P7_IRQ_OFF)) {
		priv->port_sts[7].link = true;
		dsa_port_phylink_mac_change(ds, 7, true);
	}
	if (priv->irq1_stat & P_LINK_DOWN_IRQ(P7_IRQ_OFF)) {
		priv->port_sts[7].link = false;
		dsa_port_phylink_mac_change(ds, 7, false);
	}

	return IRQ_HANDLED;
}

static int bcm_sf2_sw_rst(struct bcm_sf2_priv *priv)
{
	unsigned int timeout = 1000;
	u32 reg;
	int ret;

	/* The watchdog reset does not work on 7278, we need to hit the
	 * "external" reset line through the reset controller.
	 */
	if (priv->type == BCM7278_DEVICE_ID) {
		ret = reset_control_assert(priv->rcdev);
		if (ret)
			return ret;

		return reset_control_deassert(priv->rcdev);
	}

	reg = core_readl(priv, CORE_WATCHDOG_CTRL);
	reg |= SOFTWARE_RESET | EN_CHIP_RST | EN_SW_RESET;
	core_writel(priv, reg, CORE_WATCHDOG_CTRL);

	do {
		reg = core_readl(priv, CORE_WATCHDOG_CTRL);
		if (!(reg & SOFTWARE_RESET))
			break;

		usleep_range(1000, 2000);
	} while (timeout-- > 0);

	if (timeout == 0)
		return -ETIMEDOUT;

	return 0;
}

static void bcm_sf2_crossbar_setup(struct bcm_sf2_priv *priv)
{
	struct device *dev = priv->dev->ds->dev;
	int shift;
	u32 mask;
	u32 reg;
	int i;

	mask = BIT(priv->num_crossbar_int_ports) - 1;

	reg = reg_readl(priv, REG_CROSSBAR);
	switch (priv->type) {
	case BCM4908_DEVICE_ID:
		shift = CROSSBAR_BCM4908_INT_P7 * priv->num_crossbar_int_ports;
		reg &= ~(mask << shift);
		if (0) /* FIXME */
			reg |= CROSSBAR_BCM4908_EXT_SERDES << shift;
		else if (priv->int_phy_mask & BIT(7))
			reg |= CROSSBAR_BCM4908_EXT_GPHY4 << shift;
		else if (phy_interface_mode_is_rgmii(priv->port_sts[7].mode))
			reg |= CROSSBAR_BCM4908_EXT_RGMII << shift;
		else if (WARN(1, "Invalid port mode\n"))
			return;
		break;
	default:
		return;
	}
	reg_writel(priv, reg, REG_CROSSBAR);

	reg = reg_readl(priv, REG_CROSSBAR);
	for (i = 0; i < priv->num_crossbar_int_ports; i++) {
		shift = i * priv->num_crossbar_int_ports;

		dev_dbg(dev, "crossbar int port #%d - ext port #%d\n", i,
			(reg >> shift) & mask);
	}
}

static void bcm_sf2_intr_disable(struct bcm_sf2_priv *priv)
{
	intrl2_0_mask_set(priv, 0xffffffff);
	intrl2_0_writel(priv, 0xffffffff, INTRL2_CPU_CLEAR);
	intrl2_1_mask_set(priv, 0xffffffff);
	intrl2_1_writel(priv, 0xffffffff, INTRL2_CPU_CLEAR);
}

static void bcm_sf2_identify_ports(struct bcm_sf2_priv *priv,
				   struct device_node *dn)
{
	struct device *dev = priv->dev->ds->dev;
	struct bcm_sf2_port_status *port_st;
	struct device_node *port;
	unsigned int port_num;
	struct property *prop;
	int err;

	priv->moca_port = -1;

	for_each_available_child_of_node(dn, port) {
		if (of_property_read_u32(port, "reg", &port_num))
			continue;

		if (port_num >= DSA_MAX_PORTS) {
			dev_err(dev, "Invalid port number %d\n", port_num);
			continue;
		}

		port_st = &priv->port_sts[port_num];

		/* Internal PHYs get assigned a specific 'phy-mode' property
		 * value: "internal" to help flag them before MDIO probing
		 * has completed, since they might be turned off at that
		 * time
		 */
		err = of_get_phy_mode(port, &port_st->mode);
		if (err)
			continue;

		if (port_st->mode == PHY_INTERFACE_MODE_INTERNAL)
			priv->int_phy_mask |= 1 << port_num;

		if (port_st->mode == PHY_INTERFACE_MODE_MOCA)
			priv->moca_port = port_num;

		if (of_property_read_bool(port, "brcm,use-bcm-hdr"))
			priv->brcm_tag_mask |= 1 << port_num;

		/* Ensure that port 5 is not picked up as a DSA CPU port
		 * flavour but a regular port instead. We should be using
		 * devlink to be able to set the port flavour.
		 */
		if (port_num == 5 && priv->type == BCM7278_DEVICE_ID) {
			prop = of_find_property(port, "ethernet", NULL);
			if (prop)
				of_remove_property(port, prop);
		}
	}
}

static int bcm_sf2_mdio_register(struct dsa_switch *ds)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	struct device_node *dn, *child;
	struct phy_device *phydev;
	struct property *prop;
	static int index;
	int err, reg;

	/* Find our integrated MDIO bus node */
	dn = of_find_compatible_node(NULL, NULL, "brcm,unimac-mdio");
	priv->master_mii_bus = of_mdio_find_bus(dn);
	if (!priv->master_mii_bus) {
		of_node_put(dn);
		return -EPROBE_DEFER;
	}

	get_device(&priv->master_mii_bus->dev);
	priv->master_mii_dn = dn;

	priv->slave_mii_bus = mdiobus_alloc();
	if (!priv->slave_mii_bus) {
		of_node_put(dn);
		return -ENOMEM;
	}

	priv->slave_mii_bus->priv = priv;
	priv->slave_mii_bus->name = "sf2 slave mii";
	priv->slave_mii_bus->read = bcm_sf2_sw_mdio_read;
	priv->slave_mii_bus->write = bcm_sf2_sw_mdio_write;
	snprintf(priv->slave_mii_bus->id, MII_BUS_ID_SIZE, "sf2-%d",
		 index++);
	priv->slave_mii_bus->dev.of_node = dn;

	/* Include the pseudo-PHY address to divert reads towards our
	 * workaround. This is only required for 7445D0, since 7445E0
	 * disconnects the internal switch pseudo-PHY such that we can use the
	 * regular SWITCH_MDIO master controller instead.
	 *
	 * Here we flag the pseudo PHY as needing special treatment and would
	 * otherwise make all other PHY read/writes go to the master MDIO bus
	 * controller that comes with this switch backed by the "mdio-unimac"
	 * driver.
	 */
	if (of_machine_is_compatible("brcm,bcm7445d0"))
		priv->indir_phy_mask |= (1 << BRCM_PSEUDO_PHY_ADDR) | (1 << 0);
	else
		priv->indir_phy_mask = 0;

	ds->phys_mii_mask = priv->indir_phy_mask;
	ds->slave_mii_bus = priv->slave_mii_bus;
	priv->slave_mii_bus->parent = ds->dev->parent;
	priv->slave_mii_bus->phy_mask = ~priv->indir_phy_mask;

	/* We need to make sure that of_phy_connect() will not work by
	 * removing the 'phandle' and 'linux,phandle' properties and
	 * unregister the existing PHY device that was already registered.
	 */
	for_each_available_child_of_node(dn, child) {
		if (of_property_read_u32(child, "reg", &reg) ||
		    reg >= PHY_MAX_ADDR)
			continue;

		if (!(priv->indir_phy_mask & BIT(reg)))
			continue;

		prop = of_find_property(child, "phandle", NULL);
		if (prop)
			of_remove_property(child, prop);

		prop = of_find_property(child, "linux,phandle", NULL);
		if (prop)
			of_remove_property(child, prop);

		phydev = of_phy_find_device(child);
		if (phydev)
			phy_device_remove(phydev);
	}

	err = mdiobus_register(priv->slave_mii_bus);
	if (err && dn) {
		mdiobus_free(priv->slave_mii_bus);
		of_node_put(dn);
	}

	return err;
}

static void bcm_sf2_mdio_unregister(struct bcm_sf2_priv *priv)
{
	mdiobus_unregister(priv->slave_mii_bus);
	mdiobus_free(priv->slave_mii_bus);
	of_node_put(priv->master_mii_dn);
}

static u32 bcm_sf2_sw_get_phy_flags(struct dsa_switch *ds, int port)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);

	/* The BCM7xxx PHY driver expects to find the integrated PHY revision
	 * in bits 15:8 and the patch level in bits 7:0 which is exactly what
	 * the REG_PHY_REVISION register layout is.
	 */
	if (priv->int_phy_mask & BIT(port))
		return priv->hw_params.gphy_rev;
	else
		return PHY_BRCM_AUTO_PWRDWN_ENABLE |
		       PHY_BRCM_DIS_TXCRXC_NOENRGY |
		       PHY_BRCM_IDDQ_SUSPEND;
}

static void bcm_sf2_sw_get_caps(struct dsa_switch *ds, int port,
				struct phylink_config *config)
{
	unsigned long *interfaces = config->supported_interfaces;
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);

	if (priv->int_phy_mask & BIT(port)) {
		__set_bit(PHY_INTERFACE_MODE_INTERNAL, interfaces);
	} else if (priv->moca_port == port) {
		__set_bit(PHY_INTERFACE_MODE_MOCA, interfaces);
	} else {
		__set_bit(PHY_INTERFACE_MODE_MII, interfaces);
		__set_bit(PHY_INTERFACE_MODE_REVMII, interfaces);
		__set_bit(PHY_INTERFACE_MODE_GMII, interfaces);
		phy_interface_set_rgmii(interfaces);
	}

	config->mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
		MAC_10 | MAC_100 | MAC_1000;
}

static void bcm_sf2_sw_mac_config(struct dsa_switch *ds, int port,
				  unsigned int mode,
				  const struct phylink_link_state *state)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	u32 id_mode_dis = 0, port_mode;
	u32 reg_rgmii_ctrl;
	u32 reg;

	if (port == core_readl(priv, CORE_IMP0_PRT_ID))
		return;

	switch (state->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		id_mode_dis = 1;
		fallthrough;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		port_mode = EXT_GPHY;
		break;
	case PHY_INTERFACE_MODE_MII:
		port_mode = EXT_EPHY;
		break;
	case PHY_INTERFACE_MODE_REVMII:
		port_mode = EXT_REVMII;
		break;
	default:
		/* Nothing required for all other PHYs: internal and MoCA */
		return;
	}

	reg_rgmii_ctrl = bcm_sf2_reg_rgmii_cntrl(priv, port);

	/* Clear id_mode_dis bit, and the existing port mode, let
	 * RGMII_MODE_EN bet set by mac_link_{up,down}
	 */
	reg = reg_readl(priv, reg_rgmii_ctrl);
	reg &= ~ID_MODE_DIS;
	reg &= ~(PORT_MODE_MASK << PORT_MODE_SHIFT);

	reg |= port_mode;
	if (id_mode_dis)
		reg |= ID_MODE_DIS;

	reg_writel(priv, reg, reg_rgmii_ctrl);
}

static void bcm_sf2_sw_mac_link_set(struct dsa_switch *ds, int port,
				    phy_interface_t interface, bool link)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	u32 reg_rgmii_ctrl;
	u32 reg;

	if (!phy_interface_mode_is_rgmii(interface) &&
	    interface != PHY_INTERFACE_MODE_MII &&
	    interface != PHY_INTERFACE_MODE_REVMII)
		return;

	reg_rgmii_ctrl = bcm_sf2_reg_rgmii_cntrl(priv, port);

	/* If the link is down, just disable the interface to conserve power */
	reg = reg_readl(priv, reg_rgmii_ctrl);
	if (link)
		reg |= RGMII_MODE_EN;
	else
		reg &= ~RGMII_MODE_EN;
	reg_writel(priv, reg, reg_rgmii_ctrl);
}

static void bcm_sf2_sw_mac_link_down(struct dsa_switch *ds, int port,
				     unsigned int mode,
				     phy_interface_t interface)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	u32 reg, offset;

	if (priv->wol_ports_mask & BIT(port))
		return;

	if (port != core_readl(priv, CORE_IMP0_PRT_ID)) {
		if (priv->type == BCM4908_DEVICE_ID ||
		    priv->type == BCM7445_DEVICE_ID)
			offset = CORE_STS_OVERRIDE_GMIIP_PORT(port);
		else
			offset = CORE_STS_OVERRIDE_GMIIP2_PORT(port);

		reg = core_readl(priv, offset);
		reg &= ~LINK_STS;
		core_writel(priv, reg, offset);
	}

	bcm_sf2_sw_mac_link_set(ds, port, interface, false);
}

static void bcm_sf2_sw_mac_link_up(struct dsa_switch *ds, int port,
				   unsigned int mode,
				   phy_interface_t interface,
				   struct phy_device *phydev,
				   int speed, int duplex,
				   bool tx_pause, bool rx_pause)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	struct ethtool_eee *p = &priv->dev->ports[port].eee;

	bcm_sf2_sw_mac_link_set(ds, port, interface, true);

	if (port != core_readl(priv, CORE_IMP0_PRT_ID)) {
		u32 reg_rgmii_ctrl = 0;
		u32 reg, offset;

		if (priv->type == BCM4908_DEVICE_ID ||
		    priv->type == BCM7445_DEVICE_ID)
			offset = CORE_STS_OVERRIDE_GMIIP_PORT(port);
		else
			offset = CORE_STS_OVERRIDE_GMIIP2_PORT(port);

		if (interface == PHY_INTERFACE_MODE_RGMII ||
		    interface == PHY_INTERFACE_MODE_RGMII_TXID ||
		    interface == PHY_INTERFACE_MODE_MII ||
		    interface == PHY_INTERFACE_MODE_REVMII) {
			reg_rgmii_ctrl = bcm_sf2_reg_rgmii_cntrl(priv, port);
			reg = reg_readl(priv, reg_rgmii_ctrl);
			reg &= ~(RX_PAUSE_EN | TX_PAUSE_EN);

			if (tx_pause)
				reg |= TX_PAUSE_EN;
			if (rx_pause)
				reg |= RX_PAUSE_EN;

			reg_writel(priv, reg, reg_rgmii_ctrl);
		}

		reg = SW_OVERRIDE | LINK_STS;
		switch (speed) {
		case SPEED_1000:
			reg |= SPDSTS_1000 << SPEED_SHIFT;
			break;
		case SPEED_100:
			reg |= SPDSTS_100 << SPEED_SHIFT;
			break;
		}

		if (duplex == DUPLEX_FULL)
			reg |= DUPLX_MODE;

		core_writel(priv, reg, offset);
	}

	if (mode == MLO_AN_PHY && phydev)
		p->eee_enabled = b53_eee_init(ds, port, phydev);
}

static void bcm_sf2_sw_fixed_state(struct dsa_switch *ds, int port,
				   struct phylink_link_state *status)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);

	status->link = false;

	/* MoCA port is special as we do not get link status from CORE_LNKSTS,
	 * which means that we need to force the link at the port override
	 * level to get the data to flow. We do use what the interrupt handler
	 * did determine before.
	 *
	 * For the other ports, we just force the link status, since this is
	 * a fixed PHY device.
	 */
	if (port == priv->moca_port) {
		status->link = priv->port_sts[port].link;
		/* For MoCA interfaces, also force a link down notification
		 * since some version of the user-space daemon (mocad) use
		 * cmd->autoneg to force the link, which messes up the PHY
		 * state machine and make it go in PHY_FORCING state instead.
		 */
		if (!status->link)
			netif_carrier_off(dsa_to_port(ds, port)->slave);
		status->duplex = DUPLEX_FULL;
	} else {
		status->link = true;
	}
}

static void bcm_sf2_enable_acb(struct dsa_switch *ds)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	u32 reg;

	/* Enable ACB globally */
	reg = acb_readl(priv, ACB_CONTROL);
	reg |= (ACB_FLUSH_MASK << ACB_FLUSH_SHIFT);
	acb_writel(priv, reg, ACB_CONTROL);
	reg &= ~(ACB_FLUSH_MASK << ACB_FLUSH_SHIFT);
	reg |= ACB_EN | ACB_ALGORITHM;
	acb_writel(priv, reg, ACB_CONTROL);
}

static int bcm_sf2_sw_suspend(struct dsa_switch *ds)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	unsigned int port;

	bcm_sf2_intr_disable(priv);

	/* Disable all ports physically present including the IMP
	 * port, the other ones have already been disabled during
	 * bcm_sf2_sw_setup
	 */
	for (port = 0; port < ds->num_ports; port++) {
		if (dsa_is_user_port(ds, port) || dsa_is_cpu_port(ds, port))
			bcm_sf2_port_disable(ds, port);
	}

	if (!priv->wol_ports_mask)
		clk_disable_unprepare(priv->clk);

	return 0;
}

static int bcm_sf2_sw_resume(struct dsa_switch *ds)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	int ret;

	if (!priv->wol_ports_mask)
		clk_prepare_enable(priv->clk);

	ret = bcm_sf2_sw_rst(priv);
	if (ret) {
		pr_err("%s: failed to software reset switch\n", __func__);
		return ret;
	}

	bcm_sf2_crossbar_setup(priv);

	ret = bcm_sf2_cfp_resume(ds);
	if (ret)
		return ret;

	if (priv->hw_params.num_gphy == 1)
		bcm_sf2_gphy_enable_set(ds, true);

	ds->ops->setup(ds);

	return 0;
}

static void bcm_sf2_sw_get_wol(struct dsa_switch *ds, int port,
			       struct ethtool_wolinfo *wol)
{
	struct net_device *p = dsa_to_port(ds, port)->cpu_dp->master;
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	struct ethtool_wolinfo pwol = { };

	/* Get the parent device WoL settings */
	if (p->ethtool_ops->get_wol)
		p->ethtool_ops->get_wol(p, &pwol);

	/* Advertise the parent device supported settings */
	wol->supported = pwol.supported;
	memset(&wol->sopass, 0, sizeof(wol->sopass));

	if (pwol.wolopts & WAKE_MAGICSECURE)
		memcpy(&wol->sopass, pwol.sopass, sizeof(wol->sopass));

	if (priv->wol_ports_mask & (1 << port))
		wol->wolopts = pwol.wolopts;
	else
		wol->wolopts = 0;
}

static int bcm_sf2_sw_set_wol(struct dsa_switch *ds, int port,
			      struct ethtool_wolinfo *wol)
{
	struct net_device *p = dsa_to_port(ds, port)->cpu_dp->master;
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	s8 cpu_port = dsa_to_port(ds, port)->cpu_dp->index;
	struct ethtool_wolinfo pwol =  { };

	if (p->ethtool_ops->get_wol)
		p->ethtool_ops->get_wol(p, &pwol);
	if (wol->wolopts & ~pwol.supported)
		return -EINVAL;

	if (wol->wolopts)
		priv->wol_ports_mask |= (1 << port);
	else
		priv->wol_ports_mask &= ~(1 << port);

	/* If we have at least one port enabled, make sure the CPU port
	 * is also enabled. If the CPU port is the last one enabled, we disable
	 * it since this configuration does not make sense.
	 */
	if (priv->wol_ports_mask && priv->wol_ports_mask != (1 << cpu_port))
		priv->wol_ports_mask |= (1 << cpu_port);
	else
		priv->wol_ports_mask &= ~(1 << cpu_port);

	return p->ethtool_ops->set_wol(p, wol);
}

static int bcm_sf2_sw_setup(struct dsa_switch *ds)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	unsigned int port;

	/* Enable all valid ports and disable those unused */
	for (port = 0; port < priv->hw_params.num_ports; port++) {
		/* IMP port receives special treatment */
		if (dsa_is_user_port(ds, port))
			bcm_sf2_port_setup(ds, port, NULL);
		else if (dsa_is_cpu_port(ds, port))
			bcm_sf2_imp_setup(ds, port);
		else
			bcm_sf2_port_disable(ds, port);
	}

	b53_configure_vlan(ds);
	bcm_sf2_enable_acb(ds);

	return b53_setup_devlink_resources(ds);
}

static void bcm_sf2_sw_teardown(struct dsa_switch *ds)
{
	dsa_devlink_resources_unregister(ds);
}

/* The SWITCH_CORE register space is managed by b53 but operates on a page +
 * register basis so we need to translate that into an address that the
 * bus-glue understands.
 */
#define SF2_PAGE_REG_MKADDR(page, reg)	((page) << 10 | (reg) << 2)

static int bcm_sf2_core_read8(struct b53_device *dev, u8 page, u8 reg,
			      u8 *val)
{
	struct bcm_sf2_priv *priv = dev->priv;

	*val = core_readl(priv, SF2_PAGE_REG_MKADDR(page, reg));

	return 0;
}

static int bcm_sf2_core_read16(struct b53_device *dev, u8 page, u8 reg,
			       u16 *val)
{
	struct bcm_sf2_priv *priv = dev->priv;

	*val = core_readl(priv, SF2_PAGE_REG_MKADDR(page, reg));

	return 0;
}

static int bcm_sf2_core_read32(struct b53_device *dev, u8 page, u8 reg,
			       u32 *val)
{
	struct bcm_sf2_priv *priv = dev->priv;

	*val = core_readl(priv, SF2_PAGE_REG_MKADDR(page, reg));

	return 0;
}

static int bcm_sf2_core_read64(struct b53_device *dev, u8 page, u8 reg,
			       u64 *val)
{
	struct bcm_sf2_priv *priv = dev->priv;

	*val = core_readq(priv, SF2_PAGE_REG_MKADDR(page, reg));

	return 0;
}

static int bcm_sf2_core_write8(struct b53_device *dev, u8 page, u8 reg,
			       u8 value)
{
	struct bcm_sf2_priv *priv = dev->priv;

	core_writel(priv, value, SF2_PAGE_REG_MKADDR(page, reg));

	return 0;
}

static int bcm_sf2_core_write16(struct b53_device *dev, u8 page, u8 reg,
				u16 value)
{
	struct bcm_sf2_priv *priv = dev->priv;

	core_writel(priv, value, SF2_PAGE_REG_MKADDR(page, reg));

	return 0;
}

static int bcm_sf2_core_write32(struct b53_device *dev, u8 page, u8 reg,
				u32 value)
{
	struct bcm_sf2_priv *priv = dev->priv;

	core_writel(priv, value, SF2_PAGE_REG_MKADDR(page, reg));

	return 0;
}

static int bcm_sf2_core_write64(struct b53_device *dev, u8 page, u8 reg,
				u64 value)
{
	struct bcm_sf2_priv *priv = dev->priv;

	core_writeq(priv, value, SF2_PAGE_REG_MKADDR(page, reg));

	return 0;
}

static const struct b53_io_ops bcm_sf2_io_ops = {
	.read8	= bcm_sf2_core_read8,
	.read16	= bcm_sf2_core_read16,
	.read32	= bcm_sf2_core_read32,
	.read48	= bcm_sf2_core_read64,
	.read64	= bcm_sf2_core_read64,
	.write8	= bcm_sf2_core_write8,
	.write16 = bcm_sf2_core_write16,
	.write32 = bcm_sf2_core_write32,
	.write48 = bcm_sf2_core_write64,
	.write64 = bcm_sf2_core_write64,
};

static void bcm_sf2_sw_get_strings(struct dsa_switch *ds, int port,
				   u32 stringset, uint8_t *data)
{
	int cnt = b53_get_sset_count(ds, port, stringset);

	b53_get_strings(ds, port, stringset, data);
	bcm_sf2_cfp_get_strings(ds, port, stringset,
				data + cnt * ETH_GSTRING_LEN);
}

static void bcm_sf2_sw_get_ethtool_stats(struct dsa_switch *ds, int port,
					 uint64_t *data)
{
	int cnt = b53_get_sset_count(ds, port, ETH_SS_STATS);

	b53_get_ethtool_stats(ds, port, data);
	bcm_sf2_cfp_get_ethtool_stats(ds, port, data + cnt);
}

static int bcm_sf2_sw_get_sset_count(struct dsa_switch *ds, int port,
				     int sset)
{
	int cnt = b53_get_sset_count(ds, port, sset);

	if (cnt < 0)
		return cnt;

	cnt += bcm_sf2_cfp_get_sset_count(ds, port, sset);

	return cnt;
}

static const struct dsa_switch_ops bcm_sf2_ops = {
	.get_tag_protocol	= b53_get_tag_protocol,
	.setup			= bcm_sf2_sw_setup,
	.teardown		= bcm_sf2_sw_teardown,
	.get_strings		= bcm_sf2_sw_get_strings,
	.get_ethtool_stats	= bcm_sf2_sw_get_ethtool_stats,
	.get_sset_count		= bcm_sf2_sw_get_sset_count,
	.get_ethtool_phy_stats	= b53_get_ethtool_phy_stats,
	.get_phy_flags		= bcm_sf2_sw_get_phy_flags,
	.phylink_get_caps	= bcm_sf2_sw_get_caps,
	.phylink_mac_config	= bcm_sf2_sw_mac_config,
	.phylink_mac_link_down	= bcm_sf2_sw_mac_link_down,
	.phylink_mac_link_up	= bcm_sf2_sw_mac_link_up,
	.phylink_fixed_state	= bcm_sf2_sw_fixed_state,
	.suspend		= bcm_sf2_sw_suspend,
	.resume			= bcm_sf2_sw_resume,
	.get_wol		= bcm_sf2_sw_get_wol,
	.set_wol		= bcm_sf2_sw_set_wol,
	.port_enable		= bcm_sf2_port_setup,
	.port_disable		= bcm_sf2_port_disable,
	.get_mac_eee		= b53_get_mac_eee,
	.set_mac_eee		= b53_set_mac_eee,
	.port_bridge_join	= b53_br_join,
	.port_bridge_leave	= b53_br_leave,
	.port_pre_bridge_flags	= b53_br_flags_pre,
	.port_bridge_flags	= b53_br_flags,
	.port_stp_state_set	= b53_br_set_stp_state,
	.port_fast_age		= b53_br_fast_age,
	.port_vlan_filtering	= b53_vlan_filtering,
	.port_vlan_add		= b53_vlan_add,
	.port_vlan_del		= b53_vlan_del,
	.port_fdb_dump		= b53_fdb_dump,
	.port_fdb_add		= b53_fdb_add,
	.port_fdb_del		= b53_fdb_del,
	.get_rxnfc		= bcm_sf2_get_rxnfc,
	.set_rxnfc		= bcm_sf2_set_rxnfc,
	.port_mirror_add	= b53_mirror_add,
	.port_mirror_del	= b53_mirror_del,
	.port_mdb_add		= b53_mdb_add,
	.port_mdb_del		= b53_mdb_del,
};

struct bcm_sf2_of_data {
	u32 type;
	const u16 *reg_offsets;
	unsigned int core_reg_align;
	unsigned int num_cfp_rules;
	unsigned int num_crossbar_int_ports;
};

static const u16 bcm_sf2_4908_reg_offsets[] = {
	[REG_SWITCH_CNTRL]	= 0x00,
	[REG_SWITCH_STATUS]	= 0x04,
	[REG_DIR_DATA_WRITE]	= 0x08,
	[REG_DIR_DATA_READ]	= 0x0c,
	[REG_SWITCH_REVISION]	= 0x10,
	[REG_PHY_REVISION]	= 0x14,
	[REG_SPHY_CNTRL]	= 0x24,
	[REG_CROSSBAR]		= 0xc8,
	[REG_RGMII_11_CNTRL]	= 0x014c,
	[REG_LED_0_CNTRL]		= 0x40,
	[REG_LED_1_CNTRL]		= 0x4c,
	[REG_LED_2_CNTRL]		= 0x58,
	[REG_LED_3_CNTRL]		= 0x64,
	[REG_LED_4_CNTRL]		= 0x88,
	[REG_LED_5_CNTRL]		= 0xa0,
	[REG_LED_AGGREGATE_CTRL]	= 0xb8,

};

static const struct bcm_sf2_of_data bcm_sf2_4908_data = {
	.type		= BCM4908_DEVICE_ID,
	.core_reg_align	= 0,
	.reg_offsets	= bcm_sf2_4908_reg_offsets,
	.num_cfp_rules	= 256,
	.num_crossbar_int_ports = 2,
};

/* Register offsets for the SWITCH_REG_* block */
static const u16 bcm_sf2_7445_reg_offsets[] = {
	[REG_SWITCH_CNTRL]	= 0x00,
	[REG_SWITCH_STATUS]	= 0x04,
	[REG_DIR_DATA_WRITE]	= 0x08,
	[REG_DIR_DATA_READ]	= 0x0C,
	[REG_SWITCH_REVISION]	= 0x18,
	[REG_PHY_REVISION]	= 0x1C,
	[REG_SPHY_CNTRL]	= 0x2C,
	[REG_RGMII_0_CNTRL]	= 0x34,
	[REG_RGMII_1_CNTRL]	= 0x40,
	[REG_RGMII_2_CNTRL]	= 0x4c,
	[REG_LED_0_CNTRL]	= 0x90,
	[REG_LED_1_CNTRL]	= 0x94,
	[REG_LED_2_CNTRL]	= 0x98,
};

static const struct bcm_sf2_of_data bcm_sf2_7445_data = {
	.type		= BCM7445_DEVICE_ID,
	.core_reg_align	= 0,
	.reg_offsets	= bcm_sf2_7445_reg_offsets,
	.num_cfp_rules	= 256,
};

static const u16 bcm_sf2_7278_reg_offsets[] = {
	[REG_SWITCH_CNTRL]	= 0x00,
	[REG_SWITCH_STATUS]	= 0x04,
	[REG_DIR_DATA_WRITE]	= 0x08,
	[REG_DIR_DATA_READ]	= 0x0c,
	[REG_SWITCH_REVISION]	= 0x10,
	[REG_PHY_REVISION]	= 0x14,
	[REG_SPHY_CNTRL]	= 0x24,
	[REG_RGMII_0_CNTRL]	= 0xe0,
	[REG_RGMII_1_CNTRL]	= 0xec,
	[REG_RGMII_2_CNTRL]	= 0xf8,
	[REG_LED_0_CNTRL]	= 0x40,
	[REG_LED_1_CNTRL]	= 0x4c,
	[REG_LED_2_CNTRL]	= 0x58,
};

static const struct bcm_sf2_of_data bcm_sf2_7278_data = {
	.type		= BCM7278_DEVICE_ID,
	.core_reg_align	= 1,
	.reg_offsets	= bcm_sf2_7278_reg_offsets,
	.num_cfp_rules	= 128,
};

static const struct of_device_id bcm_sf2_of_match[] = {
	{ .compatible = "brcm,bcm4908-switch",
	  .data = &bcm_sf2_4908_data
	},
	{ .compatible = "brcm,bcm7445-switch-v4.0",
	  .data = &bcm_sf2_7445_data
	},
	{ .compatible = "brcm,bcm7278-switch-v4.0",
	  .data = &bcm_sf2_7278_data
	},
	{ .compatible = "brcm,bcm7278-switch-v4.8",
	  .data = &bcm_sf2_7278_data
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, bcm_sf2_of_match);

static int bcm_sf2_sw_probe(struct platform_device *pdev)
{
	const char *reg_names[BCM_SF2_REGS_NUM] = BCM_SF2_REGS_NAME;
	struct device_node *dn = pdev->dev.of_node;
	const struct of_device_id *of_id = NULL;
	const struct bcm_sf2_of_data *data;
	struct b53_platform_data *pdata;
	struct dsa_switch_ops *ops;
	struct device_node *ports;
	struct bcm_sf2_priv *priv;
	struct b53_device *dev;
	struct dsa_switch *ds;
	void __iomem **base;
	unsigned int i;
	u32 reg, rev;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ops = devm_kzalloc(&pdev->dev, sizeof(*ops), GFP_KERNEL);
	if (!ops)
		return -ENOMEM;

	dev = b53_switch_alloc(&pdev->dev, &bcm_sf2_io_ops, priv);
	if (!dev)
		return -ENOMEM;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	of_id = of_match_node(bcm_sf2_of_match, dn);
	if (!of_id || !of_id->data)
		return -EINVAL;

	data = of_id->data;

	/* Set SWITCH_REG register offsets and SWITCH_CORE align factor */
	priv->type = data->type;
	priv->reg_offsets = data->reg_offsets;
	priv->core_reg_align = data->core_reg_align;
	priv->num_cfp_rules = data->num_cfp_rules;
	priv->num_crossbar_int_ports = data->num_crossbar_int_ports;

	priv->rcdev = devm_reset_control_get_optional_exclusive(&pdev->dev,
								"switch");
	if (IS_ERR(priv->rcdev))
		return PTR_ERR(priv->rcdev);

	/* Auto-detection using standard registers will not work, so
	 * provide an indication of what kind of device we are for
	 * b53_common to work with
	 */
	pdata->chip_id = priv->type;
	dev->pdata = pdata;

	priv->dev = dev;
	ds = dev->ds;
	ds->ops = &bcm_sf2_ops;

	/* Advertise the 8 egress queues */
	ds->num_tx_queues = SF2_NUM_EGRESS_QUEUES;

	dev_set_drvdata(&pdev->dev, priv);

	spin_lock_init(&priv->indir_lock);
	mutex_init(&priv->cfp.lock);
	INIT_LIST_HEAD(&priv->cfp.rules_list);

	/* CFP rule #0 cannot be used for specific classifications, flag it as
	 * permanently used
	 */
	set_bit(0, priv->cfp.used);
	set_bit(0, priv->cfp.unique);

	/* Balance of_node_put() done by of_find_node_by_name() */
	of_node_get(dn);
	ports = of_find_node_by_name(dn, "ports");
	if (ports) {
		bcm_sf2_identify_ports(priv, ports);
		of_node_put(ports);
	}

	priv->irq0 = irq_of_parse_and_map(dn, 0);
	priv->irq1 = irq_of_parse_and_map(dn, 1);

	base = &priv->core;
	for (i = 0; i < BCM_SF2_REGS_NUM; i++) {
		*base = devm_platform_ioremap_resource(pdev, i);
		if (IS_ERR(*base)) {
			pr_err("unable to find register: %s\n", reg_names[i]);
			return PTR_ERR(*base);
		}
		base++;
	}

	priv->clk = devm_clk_get_optional(&pdev->dev, "sw_switch");
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	clk_prepare_enable(priv->clk);

	priv->clk_mdiv = devm_clk_get_optional(&pdev->dev, "sw_switch_mdiv");
	if (IS_ERR(priv->clk_mdiv)) {
		ret = PTR_ERR(priv->clk_mdiv);
		goto out_clk;
	}

	clk_prepare_enable(priv->clk_mdiv);

	ret = bcm_sf2_sw_rst(priv);
	if (ret) {
		pr_err("unable to software reset switch: %d\n", ret);
		goto out_clk_mdiv;
	}

	bcm_sf2_crossbar_setup(priv);

	bcm_sf2_gphy_enable_set(priv->dev->ds, true);

	ret = bcm_sf2_mdio_register(ds);
	if (ret) {
		pr_err("failed to register MDIO bus\n");
		goto out_clk_mdiv;
	}

	bcm_sf2_gphy_enable_set(priv->dev->ds, false);

	ret = bcm_sf2_cfp_rst(priv);
	if (ret) {
		pr_err("failed to reset CFP\n");
		goto out_mdio;
	}

	/* Disable all interrupts and request them */
	bcm_sf2_intr_disable(priv);

	ret = devm_request_irq(&pdev->dev, priv->irq0, bcm_sf2_switch_0_isr, 0,
			       "switch_0", ds);
	if (ret < 0) {
		pr_err("failed to request switch_0 IRQ\n");
		goto out_mdio;
	}

	ret = devm_request_irq(&pdev->dev, priv->irq1, bcm_sf2_switch_1_isr, 0,
			       "switch_1", ds);
	if (ret < 0) {
		pr_err("failed to request switch_1 IRQ\n");
		goto out_mdio;
	}

	/* Reset the MIB counters */
	reg = core_readl(priv, CORE_GMNCFGCFG);
	reg |= RST_MIB_CNT;
	core_writel(priv, reg, CORE_GMNCFGCFG);
	reg &= ~RST_MIB_CNT;
	core_writel(priv, reg, CORE_GMNCFGCFG);

	/* Get the maximum number of ports for this switch */
	priv->hw_params.num_ports = core_readl(priv, CORE_IMP0_PRT_ID) + 1;
	if (priv->hw_params.num_ports > DSA_MAX_PORTS)
		priv->hw_params.num_ports = DSA_MAX_PORTS;

	/* Assume a single GPHY setup if we can't read that property */
	if (of_property_read_u32(dn, "brcm,num-gphy",
				 &priv->hw_params.num_gphy))
		priv->hw_params.num_gphy = 1;

	rev = reg_readl(priv, REG_SWITCH_REVISION);
	priv->hw_params.top_rev = (rev >> SWITCH_TOP_REV_SHIFT) &
					SWITCH_TOP_REV_MASK;
	priv->hw_params.core_rev = (rev & SF2_REV_MASK);

	rev = reg_readl(priv, REG_PHY_REVISION);
	priv->hw_params.gphy_rev = rev & PHY_REVISION_MASK;

	ret = b53_switch_register(dev);
	if (ret)
		goto out_mdio;

	dev_info(&pdev->dev,
		 "Starfighter 2 top: %x.%02x, core: %x.%02x, IRQs: %d, %d\n",
		 priv->hw_params.top_rev >> 8, priv->hw_params.top_rev & 0xff,
		 priv->hw_params.core_rev >> 8, priv->hw_params.core_rev & 0xff,
		 priv->irq0, priv->irq1);

	return 0;

out_mdio:
	bcm_sf2_mdio_unregister(priv);
out_clk_mdiv:
	clk_disable_unprepare(priv->clk_mdiv);
out_clk:
	clk_disable_unprepare(priv->clk);
	return ret;
}

static int bcm_sf2_sw_remove(struct platform_device *pdev)
{
	struct bcm_sf2_priv *priv = platform_get_drvdata(pdev);

	if (!priv)
		return 0;

	priv->wol_ports_mask = 0;
	/* Disable interrupts */
	bcm_sf2_intr_disable(priv);
	dsa_unregister_switch(priv->dev->ds);
	bcm_sf2_cfp_exit(priv->dev->ds);
	bcm_sf2_mdio_unregister(priv);
	clk_disable_unprepare(priv->clk_mdiv);
	clk_disable_unprepare(priv->clk);
	if (priv->type == BCM7278_DEVICE_ID)
		reset_control_assert(priv->rcdev);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static void bcm_sf2_sw_shutdown(struct platform_device *pdev)
{
	struct bcm_sf2_priv *priv = platform_get_drvdata(pdev);

	if (!priv)
		return;

	/* For a kernel about to be kexec'd we want to keep the GPHY on for a
	 * successful MDIO bus scan to occur. If we did turn off the GPHY
	 * before (e.g: port_disable), this will also power it back on.
	 *
	 * Do not rely on kexec_in_progress, just power the PHY on.
	 */
	if (priv->hw_params.num_gphy == 1)
		bcm_sf2_gphy_enable_set(priv->dev->ds, true);

	dsa_switch_shutdown(priv->dev->ds);

	platform_set_drvdata(pdev, NULL);
}

#ifdef CONFIG_PM_SLEEP
static int bcm_sf2_suspend(struct device *dev)
{
	struct bcm_sf2_priv *priv = dev_get_drvdata(dev);

	return dsa_switch_suspend(priv->dev->ds);
}

static int bcm_sf2_resume(struct device *dev)
{
	struct bcm_sf2_priv *priv = dev_get_drvdata(dev);

	return dsa_switch_resume(priv->dev->ds);
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(bcm_sf2_pm_ops,
			 bcm_sf2_suspend, bcm_sf2_resume);


static struct platform_driver bcm_sf2_driver = {
	.probe	= bcm_sf2_sw_probe,
	.remove	= bcm_sf2_sw_remove,
	.shutdown = bcm_sf2_sw_shutdown,
	.driver = {
		.name = "brcm-sf2",
		.of_match_table = bcm_sf2_of_match,
		.pm = &bcm_sf2_pm_ops,
	},
};
module_platform_driver(bcm_sf2_driver);

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Driver for Broadcom Starfighter 2 ethernet switch chip");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:brcm-sf2");
