/*
 * Broadcom Starfighter 2 DSA switch driver
 *
 * Copyright (C) 2014, Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/list.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/mii.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <net/dsa.h>
#include <linux/ethtool.h>

#include "bcm_sf2.h"
#include "bcm_sf2_regs.h"

/* String, offset, and register size in bytes if different from 4 bytes */
static const struct bcm_sf2_hw_stats bcm_sf2_mib[] = {
	{ "TxOctets",		0x000, 8	},
	{ "TxDropPkts",		0x020		},
	{ "TxQPKTQ0",		0x030		},
	{ "TxBroadcastPkts",	0x040		},
	{ "TxMulticastPkts",	0x050		},
	{ "TxUnicastPKts",	0x060		},
	{ "TxCollisions",	0x070		},
	{ "TxSingleCollision",	0x080		},
	{ "TxMultipleCollision", 0x090		},
	{ "TxDeferredCollision", 0x0a0		},
	{ "TxLateCollision",	0x0b0		},
	{ "TxExcessiveCollision", 0x0c0		},
	{ "TxFrameInDisc",	0x0d0		},
	{ "TxPausePkts",	0x0e0		},
	{ "TxQPKTQ1",		0x0f0		},
	{ "TxQPKTQ2",		0x100		},
	{ "TxQPKTQ3",		0x110		},
	{ "TxQPKTQ4",		0x120		},
	{ "TxQPKTQ5",		0x130		},
	{ "RxOctets",		0x140, 8	},
	{ "RxUndersizePkts",	0x160		},
	{ "RxPausePkts",	0x170		},
	{ "RxPkts64Octets",	0x180		},
	{ "RxPkts65to127Octets", 0x190		},
	{ "RxPkts128to255Octets", 0x1a0		},
	{ "RxPkts256to511Octets", 0x1b0		},
	{ "RxPkts512to1023Octets", 0x1c0	},
	{ "RxPkts1024toMaxPktsOctets", 0x1d0	},
	{ "RxOversizePkts",	0x1e0		},
	{ "RxJabbers",		0x1f0		},
	{ "RxAlignmentErrors",	0x200		},
	{ "RxFCSErrors",	0x210		},
	{ "RxGoodOctets",	0x220, 8	},
	{ "RxDropPkts",		0x240		},
	{ "RxUnicastPkts",	0x250		},
	{ "RxMulticastPkts",	0x260		},
	{ "RxBroadcastPkts",	0x270		},
	{ "RxSAChanges",	0x280		},
	{ "RxFragments",	0x290		},
	{ "RxJumboPkt",		0x2a0		},
	{ "RxSymblErr",		0x2b0		},
	{ "InRangeErrCount",	0x2c0		},
	{ "OutRangeErrCount",	0x2d0		},
	{ "EEELpiEvent",	0x2e0		},
	{ "EEELpiDuration",	0x2f0		},
	{ "RxDiscard",		0x300, 8	},
	{ "TxQPKTQ6",		0x320		},
	{ "TxQPKTQ7",		0x330		},
	{ "TxPkts64Octets",	0x340		},
	{ "TxPkts65to127Octets", 0x350		},
	{ "TxPkts128to255Octets", 0x360		},
	{ "TxPkts256to511Ocets", 0x370		},
	{ "TxPkts512to1023Ocets", 0x380		},
	{ "TxPkts1024toMaxPktOcets", 0x390	},
};

#define BCM_SF2_STATS_SIZE	ARRAY_SIZE(bcm_sf2_mib)

static void bcm_sf2_sw_get_strings(struct dsa_switch *ds,
				   int port, uint8_t *data)
{
	unsigned int i;

	for (i = 0; i < BCM_SF2_STATS_SIZE; i++)
		memcpy(data + i * ETH_GSTRING_LEN,
		       bcm_sf2_mib[i].string, ETH_GSTRING_LEN);
}

static void bcm_sf2_sw_get_ethtool_stats(struct dsa_switch *ds,
					 int port, uint64_t *data)
{
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
	const struct bcm_sf2_hw_stats *s;
	unsigned int i;
	u64 val = 0;
	u32 offset;

	mutex_lock(&priv->stats_mutex);

	/* Now fetch the per-port counters */
	for (i = 0; i < BCM_SF2_STATS_SIZE; i++) {
		s = &bcm_sf2_mib[i];

		/* Do a latched 64-bit read if needed */
		offset = s->reg + CORE_P_MIB_OFFSET(port);
		if (s->sizeof_stat == 8)
			val = core_readq(priv, offset);
		else
			val = core_readl(priv, offset);

		data[i] = (u64)val;
	}

	mutex_unlock(&priv->stats_mutex);
}

static int bcm_sf2_sw_get_sset_count(struct dsa_switch *ds)
{
	return BCM_SF2_STATS_SIZE;
}

static char *bcm_sf2_sw_probe(struct device *host_dev, int sw_addr)
{
	return "Broadcom Starfighter 2";
}

static void bcm_sf2_imp_vlan_setup(struct dsa_switch *ds, int cpu_port)
{
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
	unsigned int i;
	u32 reg;

	/* Enable the IMP Port to be in the same VLAN as the other ports
	 * on a per-port basis such that we only have Port i and IMP in
	 * the same VLAN.
	 */
	for (i = 0; i < priv->hw_params.num_ports; i++) {
		if (!((1 << i) & ds->phys_port_mask))
			continue;

		reg = core_readl(priv, CORE_PORT_VLAN_CTL_PORT(i));
		reg |= (1 << cpu_port);
		core_writel(priv, reg, CORE_PORT_VLAN_CTL_PORT(i));
	}
}

static void bcm_sf2_imp_setup(struct dsa_switch *ds, int port)
{
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
	u32 reg, val;

	/* Enable the port memories */
	reg = core_readl(priv, CORE_MEM_PSM_VDD_CTRL);
	reg &= ~P_TXQ_PSM_VDD(port);
	core_writel(priv, reg, CORE_MEM_PSM_VDD_CTRL);

	/* Enable Broadcast, Multicast, Unicast forwarding to IMP port */
	reg = core_readl(priv, CORE_IMP_CTL);
	reg |= (RX_BCST_EN | RX_MCST_EN | RX_UCST_EN);
	reg &= ~(RX_DIS | TX_DIS);
	core_writel(priv, reg, CORE_IMP_CTL);

	/* Enable forwarding */
	core_writel(priv, SW_FWDG_EN, CORE_SWMODE);

	/* Enable IMP port in dumb mode */
	reg = core_readl(priv, CORE_SWITCH_CTRL);
	reg |= MII_DUMB_FWDG_EN;
	core_writel(priv, reg, CORE_SWITCH_CTRL);

	/* Resolve which bit controls the Broadcom tag */
	switch (port) {
	case 8:
		val = BRCM_HDR_EN_P8;
		break;
	case 7:
		val = BRCM_HDR_EN_P7;
		break;
	case 5:
		val = BRCM_HDR_EN_P5;
		break;
	default:
		val = 0;
		break;
	}

	/* Enable Broadcom tags for IMP port */
	reg = core_readl(priv, CORE_BRCM_HDR_CTRL);
	reg |= val;
	core_writel(priv, reg, CORE_BRCM_HDR_CTRL);

	/* Enable reception Broadcom tag for CPU TX (switch RX) to
	 * allow us to tag outgoing frames
	 */
	reg = core_readl(priv, CORE_BRCM_HDR_RX_DIS);
	reg &= ~(1 << port);
	core_writel(priv, reg, CORE_BRCM_HDR_RX_DIS);

	/* Enable transmission of Broadcom tags from the switch (CPU RX) to
	 * allow delivering frames to the per-port net_devices
	 */
	reg = core_readl(priv, CORE_BRCM_HDR_TX_DIS);
	reg &= ~(1 << port);
	core_writel(priv, reg, CORE_BRCM_HDR_TX_DIS);

	/* Force link status for IMP port */
	reg = core_readl(priv, CORE_STS_OVERRIDE_IMP);
	reg |= (MII_SW_OR | LINK_STS);
	core_writel(priv, reg, CORE_STS_OVERRIDE_IMP);
}

static void bcm_sf2_eee_enable_set(struct dsa_switch *ds, int port, bool enable)
{
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
	u32 reg;

	reg = core_readl(priv, CORE_EEE_EN_CTRL);
	if (enable)
		reg |= 1 << port;
	else
		reg &= ~(1 << port);
	core_writel(priv, reg, CORE_EEE_EN_CTRL);
}

static void bcm_sf2_gphy_enable_set(struct dsa_switch *ds, bool enable)
{
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
	u32 reg;

	reg = reg_readl(priv, REG_SPHY_CNTRL);
	if (enable) {
		reg |= PHY_RESET;
		reg &= ~(EXT_PWR_DOWN | IDDQ_BIAS | CK25_DIS);
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
		reg = reg_readl(priv, REG_LED_CNTRL(0));
		reg |= SPDLNK_SRC_SEL;
		reg_writel(priv, reg, REG_LED_CNTRL(0));
	}
}

static int bcm_sf2_port_setup(struct dsa_switch *ds, int port,
			      struct phy_device *phy)
{
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
	s8 cpu_port = ds->dst[ds->index].cpu_port;
	u32 reg;

	/* Clear the memory power down */
	reg = core_readl(priv, CORE_MEM_PSM_VDD_CTRL);
	reg &= ~P_TXQ_PSM_VDD(port);
	core_writel(priv, reg, CORE_MEM_PSM_VDD_CTRL);

	/* Clear the Rx and Tx disable bits and set to no spanning tree */
	core_writel(priv, 0, CORE_G_PCTL_PORT(port));

	/* Re-enable the GPHY and re-apply workarounds */
	if (port == 0 && priv->hw_params.num_gphy == 1) {
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

	/* Enable port 7 interrupts to get notified */
	if (port == 7)
		intrl2_1_mask_clear(priv, P_IRQ_MASK(P7_IRQ_OFF));

	/* Set this port, and only this one to be in the default VLAN */
	reg = core_readl(priv, CORE_PORT_VLAN_CTL_PORT(port));
	reg &= ~PORT_VLAN_CTRL_MASK;
	reg |= (1 << port);
	core_writel(priv, reg, CORE_PORT_VLAN_CTL_PORT(port));

	bcm_sf2_imp_vlan_setup(ds, cpu_port);

	/* If EEE was enabled, restore it */
	if (priv->port_sts[port].eee.eee_enabled)
		bcm_sf2_eee_enable_set(ds, port, true);

	return 0;
}

static void bcm_sf2_port_disable(struct dsa_switch *ds, int port,
				 struct phy_device *phy)
{
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
	u32 off, reg;

	if (priv->wol_ports_mask & (1 << port))
		return;

	if (port == 7) {
		intrl2_1_mask_set(priv, P_IRQ_MASK(P7_IRQ_OFF));
		intrl2_1_writel(priv, P_IRQ_MASK(P7_IRQ_OFF), INTRL2_CPU_CLEAR);
	}

	if (port == 0 && priv->hw_params.num_gphy == 1)
		bcm_sf2_gphy_enable_set(ds, false);

	if (dsa_is_cpu_port(ds, port))
		off = CORE_IMP_CTL;
	else
		off = CORE_G_PCTL_PORT(port);

	reg = core_readl(priv, off);
	reg |= RX_DIS | TX_DIS;
	core_writel(priv, reg, off);

	/* Power down the port memory */
	reg = core_readl(priv, CORE_MEM_PSM_VDD_CTRL);
	reg |= P_TXQ_PSM_VDD(port);
	core_writel(priv, reg, CORE_MEM_PSM_VDD_CTRL);
}

/* Returns 0 if EEE was not enabled, or 1 otherwise
 */
static int bcm_sf2_eee_init(struct dsa_switch *ds, int port,
			    struct phy_device *phy)
{
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
	struct ethtool_eee *p = &priv->port_sts[port].eee;
	int ret;

	p->supported = (SUPPORTED_1000baseT_Full | SUPPORTED_100baseT_Full);

	ret = phy_init_eee(phy, 0);
	if (ret)
		return 0;

	bcm_sf2_eee_enable_set(ds, port, true);

	return 1;
}

static int bcm_sf2_sw_get_eee(struct dsa_switch *ds, int port,
			      struct ethtool_eee *e)
{
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
	struct ethtool_eee *p = &priv->port_sts[port].eee;
	u32 reg;

	reg = core_readl(priv, CORE_EEE_LPI_INDICATE);
	e->eee_enabled = p->eee_enabled;
	e->eee_active = !!(reg & (1 << port));

	return 0;
}

static int bcm_sf2_sw_set_eee(struct dsa_switch *ds, int port,
			      struct phy_device *phydev,
			      struct ethtool_eee *e)
{
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
	struct ethtool_eee *p = &priv->port_sts[port].eee;

	p->eee_enabled = e->eee_enabled;

	if (!p->eee_enabled) {
		bcm_sf2_eee_enable_set(ds, port, false);
	} else {
		p->eee_enabled = bcm_sf2_eee_init(ds, port, phydev);
		if (!p->eee_enabled)
			return -EOPNOTSUPP;
	}

	return 0;
}

static irqreturn_t bcm_sf2_switch_0_isr(int irq, void *dev_id)
{
	struct bcm_sf2_priv *priv = dev_id;

	priv->irq0_stat = intrl2_0_readl(priv, INTRL2_CPU_STATUS) &
				~priv->irq0_mask;
	intrl2_0_writel(priv, priv->irq0_stat, INTRL2_CPU_CLEAR);

	return IRQ_HANDLED;
}

static irqreturn_t bcm_sf2_switch_1_isr(int irq, void *dev_id)
{
	struct bcm_sf2_priv *priv = dev_id;

	priv->irq1_stat = intrl2_1_readl(priv, INTRL2_CPU_STATUS) &
				~priv->irq1_mask;
	intrl2_1_writel(priv, priv->irq1_stat, INTRL2_CPU_CLEAR);

	if (priv->irq1_stat & P_LINK_UP_IRQ(P7_IRQ_OFF))
		priv->port_sts[7].link = 1;
	if (priv->irq1_stat & P_LINK_DOWN_IRQ(P7_IRQ_OFF))
		priv->port_sts[7].link = 0;

	return IRQ_HANDLED;
}

static int bcm_sf2_sw_rst(struct bcm_sf2_priv *priv)
{
	unsigned int timeout = 1000;
	u32 reg;

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

static void bcm_sf2_intr_disable(struct bcm_sf2_priv *priv)
{
	intrl2_0_writel(priv, 0xffffffff, INTRL2_CPU_MASK_SET);
	intrl2_0_writel(priv, 0xffffffff, INTRL2_CPU_CLEAR);
	intrl2_0_writel(priv, 0, INTRL2_CPU_MASK_CLEAR);
	intrl2_1_writel(priv, 0xffffffff, INTRL2_CPU_MASK_SET);
	intrl2_1_writel(priv, 0xffffffff, INTRL2_CPU_CLEAR);
	intrl2_1_writel(priv, 0, INTRL2_CPU_MASK_CLEAR);
}

static int bcm_sf2_sw_setup(struct dsa_switch *ds)
{
	const char *reg_names[BCM_SF2_REGS_NUM] = BCM_SF2_REGS_NAME;
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
	struct device_node *dn;
	void __iomem **base;
	unsigned int port;
	unsigned int i;
	u32 reg, rev;
	int ret;

	spin_lock_init(&priv->indir_lock);
	mutex_init(&priv->stats_mutex);

	/* All the interesting properties are at the parent device_node
	 * level
	 */
	dn = ds->pd->of_node->parent;

	priv->irq0 = irq_of_parse_and_map(dn, 0);
	priv->irq1 = irq_of_parse_and_map(dn, 1);

	base = &priv->core;
	for (i = 0; i < BCM_SF2_REGS_NUM; i++) {
		*base = of_iomap(dn, i);
		if (*base == NULL) {
			pr_err("unable to find register: %s\n", reg_names[i]);
			ret = -ENOMEM;
			goto out_unmap;
		}
		base++;
	}

	ret = bcm_sf2_sw_rst(priv);
	if (ret) {
		pr_err("unable to software reset switch: %d\n", ret);
		goto out_unmap;
	}

	/* Disable all interrupts and request them */
	bcm_sf2_intr_disable(priv);

	ret = request_irq(priv->irq0, bcm_sf2_switch_0_isr, 0,
			  "switch_0", priv);
	if (ret < 0) {
		pr_err("failed to request switch_0 IRQ\n");
		goto out_unmap;
	}

	ret = request_irq(priv->irq1, bcm_sf2_switch_1_isr, 0,
			  "switch_1", priv);
	if (ret < 0) {
		pr_err("failed to request switch_1 IRQ\n");
		goto out_free_irq0;
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

	/* Enable all valid ports and disable those unused */
	for (port = 0; port < priv->hw_params.num_ports; port++) {
		/* IMP port receives special treatment */
		if ((1 << port) & ds->phys_port_mask)
			bcm_sf2_port_setup(ds, port, NULL);
		else if (dsa_is_cpu_port(ds, port))
			bcm_sf2_imp_setup(ds, port);
		else
			bcm_sf2_port_disable(ds, port, NULL);
	}

	/* Include the pseudo-PHY address and the broadcast PHY address to
	 * divert reads towards our workaround
	 */
	ds->phys_mii_mask |= ((1 << 30) | (1 << 0));

	rev = reg_readl(priv, REG_SWITCH_REVISION);
	priv->hw_params.top_rev = (rev >> SWITCH_TOP_REV_SHIFT) &
					SWITCH_TOP_REV_MASK;
	priv->hw_params.core_rev = (rev & SF2_REV_MASK);

	rev = reg_readl(priv, REG_PHY_REVISION);
	priv->hw_params.gphy_rev = rev & PHY_REVISION_MASK;

	pr_info("Starfighter 2 top: %x.%02x, core: %x.%02x base: 0x%p, IRQs: %d, %d\n",
		priv->hw_params.top_rev >> 8, priv->hw_params.top_rev & 0xff,
		priv->hw_params.core_rev >> 8, priv->hw_params.core_rev & 0xff,
		priv->core, priv->irq0, priv->irq1);

	return 0;

out_free_irq0:
	free_irq(priv->irq0, priv);
out_unmap:
	base = &priv->core;
	for (i = 0; i < BCM_SF2_REGS_NUM; i++) {
		if (*base)
			iounmap(*base);
		base++;
	}
	return ret;
}

static int bcm_sf2_sw_set_addr(struct dsa_switch *ds, u8 *addr)
{
	return 0;
}

static u32 bcm_sf2_sw_get_phy_flags(struct dsa_switch *ds, int port)
{
	struct bcm_sf2_priv *priv = ds_to_priv(ds);

	/* The BCM7xxx PHY driver expects to find the integrated PHY revision
	 * in bits 15:8 and the patch level in bits 7:0 which is exactly what
	 * the REG_PHY_REVISION register layout is.
	 */

	return priv->hw_params.gphy_rev;
}

static int bcm_sf2_sw_indir_rw(struct dsa_switch *ds, int op, int addr,
			       int regnum, u16 val)
{
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
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

static int bcm_sf2_sw_phy_read(struct dsa_switch *ds, int addr, int regnum)
{
	/* Intercept reads from the MDIO broadcast address or Broadcom
	 * pseudo-PHY address
	 */
	switch (addr) {
	case 0:
	case 30:
		return bcm_sf2_sw_indir_rw(ds, 1, addr, regnum, 0);
	default:
		return 0xffff;
	}
}

static int bcm_sf2_sw_phy_write(struct dsa_switch *ds, int addr, int regnum,
				u16 val)
{
	/* Intercept writes to the MDIO broadcast address or Broadcom
	 * pseudo-PHY address
	 */
	switch (addr) {
	case 0:
	case 30:
		bcm_sf2_sw_indir_rw(ds, 0, addr, regnum, val);
		break;
	}

	return 0;
}

static void bcm_sf2_sw_adjust_link(struct dsa_switch *ds, int port,
				   struct phy_device *phydev)
{
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
	u32 id_mode_dis = 0, port_mode;
	const char *str = NULL;
	u32 reg;

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		str = "RGMII (no delay)";
		id_mode_dis = 1;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		if (!str)
			str = "RGMII (TX delay)";
		port_mode = EXT_GPHY;
		break;
	case PHY_INTERFACE_MODE_MII:
		str = "MII";
		port_mode = EXT_EPHY;
		break;
	case PHY_INTERFACE_MODE_REVMII:
		str = "Reverse MII";
		port_mode = EXT_REVMII;
		break;
	default:
		/* All other PHYs: internal and MoCA */
		goto force_link;
	}

	/* If the link is down, just disable the interface to conserve power */
	if (!phydev->link) {
		reg = reg_readl(priv, REG_RGMII_CNTRL_P(port));
		reg &= ~RGMII_MODE_EN;
		reg_writel(priv, reg, REG_RGMII_CNTRL_P(port));
		goto force_link;
	}

	/* Clear id_mode_dis bit, and the existing port mode, but
	 * make sure we enable the RGMII block for data to pass
	 */
	reg = reg_readl(priv, REG_RGMII_CNTRL_P(port));
	reg &= ~ID_MODE_DIS;
	reg &= ~(PORT_MODE_MASK << PORT_MODE_SHIFT);
	reg &= ~(RX_PAUSE_EN | TX_PAUSE_EN);

	reg |= port_mode | RGMII_MODE_EN;
	if (id_mode_dis)
		reg |= ID_MODE_DIS;

	if (phydev->pause) {
		if (phydev->asym_pause)
			reg |= TX_PAUSE_EN;
		reg |= RX_PAUSE_EN;
	}

	reg_writel(priv, reg, REG_RGMII_CNTRL_P(port));

	pr_info("Port %d configured for %s\n", port, str);

force_link:
	/* Force link settings detected from the PHY */
	reg = SW_OVERRIDE;
	switch (phydev->speed) {
	case SPEED_1000:
		reg |= SPDSTS_1000 << SPEED_SHIFT;
		break;
	case SPEED_100:
		reg |= SPDSTS_100 << SPEED_SHIFT;
		break;
	}

	if (phydev->link)
		reg |= LINK_STS;
	if (phydev->duplex == DUPLEX_FULL)
		reg |= DUPLX_MODE;

	core_writel(priv, reg, CORE_STS_OVERRIDE_GMIIP_PORT(port));
}

static void bcm_sf2_sw_fixed_link_update(struct dsa_switch *ds, int port,
					 struct fixed_phy_status *status)
{
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
	u32 duplex, pause, speed;
	u32 reg;

	duplex = core_readl(priv, CORE_DUPSTS);
	pause = core_readl(priv, CORE_PAUSESTS);
	speed = core_readl(priv, CORE_SPDSTS);

	speed >>= (port * SPDSTS_SHIFT);
	speed &= SPDSTS_MASK;

	status->link = 0;

	/* Port 7 is special as we do not get link status from CORE_LNKSTS,
	 * which means that we need to force the link at the port override
	 * level to get the data to flow. We do use what the interrupt handler
	 * did determine before.
	 *
	 * For the other ports, we just force the link status, since this is
	 * a fixed PHY device.
	 */
	if (port == 7) {
		status->link = priv->port_sts[port].link;
		status->duplex = 1;
	} else {
		status->link = 1;
		status->duplex = !!(duplex & (1 << port));
	}

	reg = core_readl(priv, CORE_STS_OVERRIDE_GMIIP_PORT(port));
	reg |= SW_OVERRIDE;
	if (status->link)
		reg |= LINK_STS;
	else
		reg &= ~LINK_STS;
	core_writel(priv, reg, CORE_STS_OVERRIDE_GMIIP_PORT(port));

	switch (speed) {
	case SPDSTS_10:
		status->speed = SPEED_10;
		break;
	case SPDSTS_100:
		status->speed = SPEED_100;
		break;
	case SPDSTS_1000:
		status->speed = SPEED_1000;
		break;
	}

	if ((pause & (1 << port)) &&
	    (pause & (1 << (port + PAUSESTS_TX_PAUSE_SHIFT)))) {
		status->asym_pause = 1;
		status->pause = 1;
	}

	if (pause & (1 << port))
		status->pause = 1;
}

static int bcm_sf2_sw_suspend(struct dsa_switch *ds)
{
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
	unsigned int port;

	bcm_sf2_intr_disable(priv);

	/* Disable all ports physically present including the IMP
	 * port, the other ones have already been disabled during
	 * bcm_sf2_sw_setup
	 */
	for (port = 0; port < DSA_MAX_PORTS; port++) {
		if ((1 << port) & ds->phys_port_mask ||
		    dsa_is_cpu_port(ds, port))
			bcm_sf2_port_disable(ds, port, NULL);
	}

	return 0;
}

static int bcm_sf2_sw_resume(struct dsa_switch *ds)
{
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
	unsigned int port;
	int ret;

	ret = bcm_sf2_sw_rst(priv);
	if (ret) {
		pr_err("%s: failed to software reset switch\n", __func__);
		return ret;
	}

	if (priv->hw_params.num_gphy == 1)
		bcm_sf2_gphy_enable_set(ds, true);

	for (port = 0; port < DSA_MAX_PORTS; port++) {
		if ((1 << port) & ds->phys_port_mask)
			bcm_sf2_port_setup(ds, port, NULL);
		else if (dsa_is_cpu_port(ds, port))
			bcm_sf2_imp_setup(ds, port);
	}

	return 0;
}

static void bcm_sf2_sw_get_wol(struct dsa_switch *ds, int port,
			       struct ethtool_wolinfo *wol)
{
	struct net_device *p = ds->dst[ds->index].master_netdev;
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
	struct ethtool_wolinfo pwol;

	/* Get the parent device WoL settings */
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
	struct net_device *p = ds->dst[ds->index].master_netdev;
	struct bcm_sf2_priv *priv = ds_to_priv(ds);
	s8 cpu_port = ds->dst[ds->index].cpu_port;
	struct ethtool_wolinfo pwol;

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

static struct dsa_switch_driver bcm_sf2_switch_driver = {
	.tag_protocol		= DSA_TAG_PROTO_BRCM,
	.priv_size		= sizeof(struct bcm_sf2_priv),
	.probe			= bcm_sf2_sw_probe,
	.setup			= bcm_sf2_sw_setup,
	.set_addr		= bcm_sf2_sw_set_addr,
	.get_phy_flags		= bcm_sf2_sw_get_phy_flags,
	.phy_read		= bcm_sf2_sw_phy_read,
	.phy_write		= bcm_sf2_sw_phy_write,
	.get_strings		= bcm_sf2_sw_get_strings,
	.get_ethtool_stats	= bcm_sf2_sw_get_ethtool_stats,
	.get_sset_count		= bcm_sf2_sw_get_sset_count,
	.adjust_link		= bcm_sf2_sw_adjust_link,
	.fixed_link_update	= bcm_sf2_sw_fixed_link_update,
	.suspend		= bcm_sf2_sw_suspend,
	.resume			= bcm_sf2_sw_resume,
	.get_wol		= bcm_sf2_sw_get_wol,
	.set_wol		= bcm_sf2_sw_set_wol,
	.port_enable		= bcm_sf2_port_setup,
	.port_disable		= bcm_sf2_port_disable,
	.get_eee		= bcm_sf2_sw_get_eee,
	.set_eee		= bcm_sf2_sw_set_eee,
};

static int __init bcm_sf2_init(void)
{
	register_switch_driver(&bcm_sf2_switch_driver);

	return 0;
}
module_init(bcm_sf2_init);

static void __exit bcm_sf2_exit(void)
{
	unregister_switch_driver(&bcm_sf2_switch_driver);
}
module_exit(bcm_sf2_exit);

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Driver for Broadcom Starfighter 2 ethernet switch chip");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:brcm-sf2");
