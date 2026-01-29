// SPDX-License-Identifier: GPL-2.0
/*
 * Lantiq / Intel / MaxLinear GSWIP common function library
 *
 * Copyright (C) 2025 Daniel Golle <daniel@makrotopia.org>
 * Copyright (C) 2023 - 2024 MaxLinear Inc.
 * Copyright (C) 2022 Snap One, LLC.  All rights reserved.
 * Copyright (C) 2017 - 2019 Hauke Mehrtens <hauke@hauke-m.de>
 * Copyright (C) 2012 John Crispin <john@phrozen.org>
 * Copyright (C) 2010 Lantiq Deutschland
 *
 * The VLAN and bridge model the GSWIP hardware uses does not directly
 * matches the model DSA uses.
 *
 * The hardware has 64 possible table entries for bridges with one VLAN
 * ID, one flow id and a list of ports for each bridge. All entries which
 * match the same flow ID are combined in the mac learning table, they
 * act as one global bridge.
 * The hardware does not support VLAN filter on the port, but on the
 * bridge, this driver converts the DSA model to the hardware.
 *
 * The CPU gets all the exception frames which do not match any forwarding
 * rule and the CPU port is also added to all bridges. This makes it possible
 * to handle all the special cases easily in software.
 * At the initialization the driver allocates one bridge table entry for
 * each switch port which is used when the port is used without an
 * explicit bridge. This prevents the frames from being forwarded
 * between all LAN ports by default.
 */

#include "lantiq_gswip.h"

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/regmap.h>
#include <net/dsa.h>

struct gswip_pce_table_entry {
	u16 index;      // PCE_TBL_ADDR.ADDR = pData->table_index
	u16 table;      // PCE_TBL_CTRL.ADDR = pData->table
	u16 key[8];
	u16 val[5];
	u16 mask;
	u8 gmap;
	bool type;
	bool valid;
	bool key_mode;
};

struct gswip_rmon_cnt_desc {
	unsigned int size;
	unsigned int offset;
	const char *name;
};

#define MIB_DESC(_size, _offset, _name) {.size = _size, .offset = _offset, .name = _name}

static const struct gswip_rmon_cnt_desc gswip_rmon_cnt[] = {
	/** Receive Packet Count (only packets that are accepted and not discarded). */
	MIB_DESC(1, 0x1F, "RxGoodPkts"),
	MIB_DESC(1, 0x23, "RxUnicastPkts"),
	MIB_DESC(1, 0x22, "RxMulticastPkts"),
	MIB_DESC(1, 0x21, "RxFCSErrorPkts"),
	MIB_DESC(1, 0x1D, "RxUnderSizeGoodPkts"),
	MIB_DESC(1, 0x1E, "RxUnderSizeErrorPkts"),
	MIB_DESC(1, 0x1B, "RxOversizeGoodPkts"),
	MIB_DESC(1, 0x1C, "RxOversizeErrorPkts"),
	MIB_DESC(1, 0x20, "RxGoodPausePkts"),
	MIB_DESC(1, 0x1A, "RxAlignErrorPkts"),
	MIB_DESC(1, 0x12, "Rx64BytePkts"),
	MIB_DESC(1, 0x13, "Rx127BytePkts"),
	MIB_DESC(1, 0x14, "Rx255BytePkts"),
	MIB_DESC(1, 0x15, "Rx511BytePkts"),
	MIB_DESC(1, 0x16, "Rx1023BytePkts"),
	/** Receive Size 1024-1522 (or more, if configured) Packet Count. */
	MIB_DESC(1, 0x17, "RxMaxBytePkts"),
	MIB_DESC(1, 0x18, "RxDroppedPkts"),
	MIB_DESC(1, 0x19, "RxFilteredPkts"),
	MIB_DESC(2, 0x24, "RxGoodBytes"),
	MIB_DESC(2, 0x26, "RxBadBytes"),
	MIB_DESC(1, 0x11, "TxAcmDroppedPkts"),
	MIB_DESC(1, 0x0C, "TxGoodPkts"),
	MIB_DESC(1, 0x06, "TxUnicastPkts"),
	MIB_DESC(1, 0x07, "TxMulticastPkts"),
	MIB_DESC(1, 0x00, "Tx64BytePkts"),
	MIB_DESC(1, 0x01, "Tx127BytePkts"),
	MIB_DESC(1, 0x02, "Tx255BytePkts"),
	MIB_DESC(1, 0x03, "Tx511BytePkts"),
	MIB_DESC(1, 0x04, "Tx1023BytePkts"),
	/** Transmit Size 1024-1522 (or more, if configured) Packet Count. */
	MIB_DESC(1, 0x05, "TxMaxBytePkts"),
	MIB_DESC(1, 0x08, "TxSingleCollCount"),
	MIB_DESC(1, 0x09, "TxMultCollCount"),
	MIB_DESC(1, 0x0A, "TxLateCollCount"),
	MIB_DESC(1, 0x0B, "TxExcessCollCount"),
	MIB_DESC(1, 0x0D, "TxPauseCount"),
	MIB_DESC(1, 0x10, "TxDroppedPkts"),
	MIB_DESC(2, 0x0E, "TxGoodBytes"),
};

static u32 gswip_switch_r_timeout(struct gswip_priv *priv, u32 offset,
				  u32 cleared)
{
	u32 val;

	return regmap_read_poll_timeout(priv->gswip, offset, val,
					!(val & cleared), 20, 50000);
}

static void gswip_mii_mask_cfg(struct gswip_priv *priv, u32 mask, u32 set,
			       int port)
{
	int reg_port;

	/* MII_CFG register only exists for MII ports */
	if (!(priv->hw_info->mii_ports & BIT(port)))
		return;

	reg_port = port + priv->hw_info->mii_port_reg_offset;

	regmap_write_bits(priv->mii, GSWIP_MII_CFGp(reg_port), mask,
			  set);
}

static int gswip_mdio_poll(struct gswip_priv *priv)
{
	u32 ctrl;

	return regmap_read_poll_timeout(priv->mdio, GSWIP_MDIO_CTRL, ctrl,
					!(ctrl & GSWIP_MDIO_CTRL_BUSY), 40, 4000);
}

static int gswip_mdio_wr(struct mii_bus *bus, int addr, int reg, u16 val)
{
	struct gswip_priv *priv = bus->priv;
	int err;

	err = gswip_mdio_poll(priv);
	if (err) {
		dev_err(&bus->dev, "waiting for MDIO bus busy timed out\n");
		return err;
	}

	regmap_write(priv->mdio, GSWIP_MDIO_WRITE, val);
	regmap_write(priv->mdio, GSWIP_MDIO_CTRL,
		     GSWIP_MDIO_CTRL_BUSY | GSWIP_MDIO_CTRL_WR |
		     ((addr & GSWIP_MDIO_CTRL_PHYAD_MASK) << GSWIP_MDIO_CTRL_PHYAD_SHIFT) |
		     (reg & GSWIP_MDIO_CTRL_REGAD_MASK));

	return 0;
}

static int gswip_mdio_rd(struct mii_bus *bus, int addr, int reg)
{
	struct gswip_priv *priv = bus->priv;
	u32 val;
	int err;

	err = gswip_mdio_poll(priv);
	if (err) {
		dev_err(&bus->dev, "waiting for MDIO bus busy timed out\n");
		return err;
	}

	regmap_write(priv->mdio, GSWIP_MDIO_CTRL,
		     GSWIP_MDIO_CTRL_BUSY | GSWIP_MDIO_CTRL_RD |
		     ((addr & GSWIP_MDIO_CTRL_PHYAD_MASK) << GSWIP_MDIO_CTRL_PHYAD_SHIFT) |
		     (reg & GSWIP_MDIO_CTRL_REGAD_MASK));

	err = gswip_mdio_poll(priv);
	if (err) {
		dev_err(&bus->dev, "waiting for MDIO bus busy timed out\n");
		return err;
	}

	err = regmap_read(priv->mdio, GSWIP_MDIO_READ, &val);
	if (err)
		return err;

	return val;
}

static int gswip_mdio(struct gswip_priv *priv)
{
	struct device_node *mdio_np, *switch_np = priv->dev->of_node;
	struct device *dev = priv->dev;
	struct mii_bus *bus;
	int err = 0;

	mdio_np = of_get_compatible_child(switch_np, "lantiq,xrx200-mdio");
	if (!mdio_np)
		mdio_np = of_get_child_by_name(switch_np, "mdio");

	if (!of_device_is_available(mdio_np))
		goto out_put_node;

	bus = devm_mdiobus_alloc(dev);
	if (!bus) {
		err = -ENOMEM;
		goto out_put_node;
	}

	bus->priv = priv;
	bus->read = gswip_mdio_rd;
	bus->write = gswip_mdio_wr;
	bus->name = "lantiq,xrx200-mdio";
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-mii", dev_name(priv->dev));
	bus->parent = priv->dev;

	err = devm_of_mdiobus_register(dev, bus, mdio_np);

out_put_node:
	of_node_put(mdio_np);

	return err;
}

static int gswip_pce_table_entry_read(struct gswip_priv *priv,
				      struct gswip_pce_table_entry *tbl)
{
	int i;
	int err;
	u32 crtl;
	u32 tmp;
	u16 addr_mode = tbl->key_mode ? GSWIP_PCE_TBL_CTRL_OPMOD_KSRD :
					GSWIP_PCE_TBL_CTRL_OPMOD_ADRD;

	mutex_lock(&priv->pce_table_lock);

	err = gswip_switch_r_timeout(priv, GSWIP_PCE_TBL_CTRL,
				     GSWIP_PCE_TBL_CTRL_BAS);
	if (err)
		goto out_unlock;

	regmap_write(priv->gswip, GSWIP_PCE_TBL_ADDR, tbl->index);
	regmap_write_bits(priv->gswip, GSWIP_PCE_TBL_CTRL,
			  GSWIP_PCE_TBL_CTRL_ADDR_MASK |
			  GSWIP_PCE_TBL_CTRL_OPMOD_MASK |
			  GSWIP_PCE_TBL_CTRL_BAS,
			  tbl->table | addr_mode | GSWIP_PCE_TBL_CTRL_BAS);

	err = gswip_switch_r_timeout(priv, GSWIP_PCE_TBL_CTRL,
				     GSWIP_PCE_TBL_CTRL_BAS);
	if (err)
		goto out_unlock;

	for (i = 0; i < ARRAY_SIZE(tbl->key); i++) {
		err = regmap_read(priv->gswip, GSWIP_PCE_TBL_KEY(i), &tmp);
		if (err)
			goto out_unlock;
		tbl->key[i] = tmp;
	}
	for (i = 0; i < ARRAY_SIZE(tbl->val); i++) {
		err = regmap_read(priv->gswip, GSWIP_PCE_TBL_VAL(i), &tmp);
		if (err)
			goto out_unlock;
		tbl->val[i] = tmp;
	}

	err = regmap_read(priv->gswip, GSWIP_PCE_TBL_MASK, &tmp);
	if (err)
		goto out_unlock;

	tbl->mask = tmp;
	err = regmap_read(priv->gswip, GSWIP_PCE_TBL_CTRL, &crtl);
	if (err)
		goto out_unlock;

	tbl->type = !!(crtl & GSWIP_PCE_TBL_CTRL_TYPE);
	tbl->valid = !!(crtl & GSWIP_PCE_TBL_CTRL_VLD);
	tbl->gmap = (crtl & GSWIP_PCE_TBL_CTRL_GMAP_MASK) >> 7;

out_unlock:
	mutex_unlock(&priv->pce_table_lock);

	return err;
}

static int gswip_pce_table_entry_write(struct gswip_priv *priv,
				       struct gswip_pce_table_entry *tbl)
{
	int i;
	int err;
	u32 crtl;
	u16 addr_mode = tbl->key_mode ? GSWIP_PCE_TBL_CTRL_OPMOD_KSWR :
					GSWIP_PCE_TBL_CTRL_OPMOD_ADWR;

	mutex_lock(&priv->pce_table_lock);

	err = gswip_switch_r_timeout(priv, GSWIP_PCE_TBL_CTRL,
				     GSWIP_PCE_TBL_CTRL_BAS);
	if (err) {
		mutex_unlock(&priv->pce_table_lock);
		return err;
	}

	regmap_write(priv->gswip, GSWIP_PCE_TBL_ADDR, tbl->index);
	regmap_write_bits(priv->gswip, GSWIP_PCE_TBL_CTRL,
			  GSWIP_PCE_TBL_CTRL_ADDR_MASK |
			  GSWIP_PCE_TBL_CTRL_OPMOD_MASK,
			  tbl->table | addr_mode);

	for (i = 0; i < ARRAY_SIZE(tbl->key); i++)
		regmap_write(priv->gswip, GSWIP_PCE_TBL_KEY(i), tbl->key[i]);

	for (i = 0; i < ARRAY_SIZE(tbl->val); i++)
		regmap_write(priv->gswip, GSWIP_PCE_TBL_VAL(i), tbl->val[i]);

	regmap_write_bits(priv->gswip, GSWIP_PCE_TBL_CTRL,
			  GSWIP_PCE_TBL_CTRL_ADDR_MASK |
			  GSWIP_PCE_TBL_CTRL_OPMOD_MASK,
			  tbl->table | addr_mode);

	regmap_write(priv->gswip, GSWIP_PCE_TBL_MASK, tbl->mask);

	regmap_read(priv->gswip, GSWIP_PCE_TBL_CTRL, &crtl);
	crtl &= ~(GSWIP_PCE_TBL_CTRL_TYPE | GSWIP_PCE_TBL_CTRL_VLD |
		  GSWIP_PCE_TBL_CTRL_GMAP_MASK);
	if (tbl->type)
		crtl |= GSWIP_PCE_TBL_CTRL_TYPE;
	if (tbl->valid)
		crtl |= GSWIP_PCE_TBL_CTRL_VLD;
	crtl |= (tbl->gmap << 7) & GSWIP_PCE_TBL_CTRL_GMAP_MASK;
	crtl |= GSWIP_PCE_TBL_CTRL_BAS;
	regmap_write(priv->gswip, GSWIP_PCE_TBL_CTRL, crtl);

	err = gswip_switch_r_timeout(priv, GSWIP_PCE_TBL_CTRL,
				     GSWIP_PCE_TBL_CTRL_BAS);

	mutex_unlock(&priv->pce_table_lock);

	return err;
}

/* Add the LAN port into a bridge with the CPU port by
 * default. This prevents automatic forwarding of
 * packages between the LAN ports when no explicit
 * bridge is configured.
 */
static int gswip_add_single_port_br(struct gswip_priv *priv, int port, bool add)
{
	struct gswip_pce_table_entry vlan_active = {0,};
	struct gswip_pce_table_entry vlan_mapping = {0,};
	int err;

	vlan_active.index = port + 1;
	vlan_active.table = GSWIP_TABLE_ACTIVE_VLAN;
	vlan_active.key[0] = GSWIP_VLAN_UNAWARE_PVID;
	vlan_active.val[0] = port + 1 /* fid */;
	vlan_active.valid = add;
	err = gswip_pce_table_entry_write(priv, &vlan_active);
	if (err) {
		dev_err(priv->dev, "failed to write active VLAN: %d\n", err);
		return err;
	}

	if (!add)
		return 0;

	vlan_mapping.index = port + 1;
	vlan_mapping.table = GSWIP_TABLE_VLAN_MAPPING;
	vlan_mapping.val[0] = GSWIP_VLAN_UNAWARE_PVID;
	vlan_mapping.val[1] = BIT(port) | dsa_cpu_ports(priv->ds);
	vlan_mapping.val[2] = 0;
	err = gswip_pce_table_entry_write(priv, &vlan_mapping);
	if (err) {
		dev_err(priv->dev, "failed to write VLAN mapping: %d\n", err);
		return err;
	}

	return 0;
}

static int gswip_port_set_learning(struct gswip_priv *priv, int port,
				   bool enable)
{
	if (!GSWIP_VERSION_GE(priv, GSWIP_VERSION_2_2))
		return -EOPNOTSUPP;

	/* learning disable bit */
	return regmap_update_bits(priv->gswip, GSWIP_PCE_PCTRL_3p(port),
				  GSWIP_PCE_PCTRL_3_LNDIS,
				  enable ? 0 : GSWIP_PCE_PCTRL_3_LNDIS);
}

static int gswip_port_pre_bridge_flags(struct dsa_switch *ds, int port,
				       struct switchdev_brport_flags flags,
				       struct netlink_ext_ack *extack)
{
	struct gswip_priv *priv = ds->priv;
	unsigned long supported = 0;

	if (GSWIP_VERSION_GE(priv, GSWIP_VERSION_2_2))
		supported |= BR_LEARNING;

	if (flags.mask & ~supported)
		return -EINVAL;

	return 0;
}

static int gswip_port_bridge_flags(struct dsa_switch *ds, int port,
				   struct switchdev_brport_flags flags,
				   struct netlink_ext_ack *extack)
{
	struct gswip_priv *priv = ds->priv;

	if (flags.mask & BR_LEARNING)
		return gswip_port_set_learning(priv, port,
					       !!(flags.val & BR_LEARNING));

	return 0;
}

static int gswip_port_setup(struct dsa_switch *ds, int port)
{
	struct gswip_priv *priv = ds->priv;
	int err;

	if (!dsa_is_cpu_port(ds, port)) {
		err = gswip_add_single_port_br(priv, port, true);
		if (err)
			return err;
	}

	return 0;
}

static int gswip_port_enable(struct dsa_switch *ds, int port,
			     struct phy_device *phydev)
{
	struct gswip_priv *priv = ds->priv;

	if (!dsa_is_cpu_port(ds, port)) {
		u32 mdio_phy = 0;

		if (phydev)
			mdio_phy = phydev->mdio.addr & GSWIP_MDIO_PHY_ADDR_MASK;

		regmap_write_bits(priv->mdio, GSWIP_MDIO_PHYp(port),
				  GSWIP_MDIO_PHY_ADDR_MASK,
				  mdio_phy);
	}

	/* RMON Counter Enable for port */
	regmap_write(priv->gswip, GSWIP_BM_PCFGp(port), GSWIP_BM_PCFG_CNTEN);

	/* enable port fetch/store dma & VLAN Modification */
	regmap_set_bits(priv->gswip, GSWIP_FDMA_PCTRLp(port),
			GSWIP_FDMA_PCTRL_EN | GSWIP_FDMA_PCTRL_VLANMOD_BOTH);
	regmap_set_bits(priv->gswip, GSWIP_SDMA_PCTRLp(port),
			GSWIP_SDMA_PCTRL_EN);

	return 0;
}

static void gswip_port_disable(struct dsa_switch *ds, int port)
{
	struct gswip_priv *priv = ds->priv;

	regmap_clear_bits(priv->gswip, GSWIP_FDMA_PCTRLp(port),
			  GSWIP_FDMA_PCTRL_EN);
	regmap_clear_bits(priv->gswip, GSWIP_SDMA_PCTRLp(port),
			  GSWIP_SDMA_PCTRL_EN);
}

static int gswip_pce_load_microcode(struct gswip_priv *priv)
{
	int i;
	int err;

	regmap_write_bits(priv->gswip, GSWIP_PCE_TBL_CTRL,
			  GSWIP_PCE_TBL_CTRL_ADDR_MASK |
			  GSWIP_PCE_TBL_CTRL_OPMOD_MASK |
			  GSWIP_PCE_TBL_CTRL_OPMOD_ADWR,
			  GSWIP_PCE_TBL_CTRL_OPMOD_ADWR);
	regmap_write(priv->gswip, GSWIP_PCE_TBL_MASK, 0);

	for (i = 0; i < priv->hw_info->pce_microcode_size; i++) {
		regmap_write(priv->gswip, GSWIP_PCE_TBL_ADDR, i);
		regmap_write(priv->gswip, GSWIP_PCE_TBL_VAL(0),
			     (*priv->hw_info->pce_microcode)[i].val_0);
		regmap_write(priv->gswip, GSWIP_PCE_TBL_VAL(1),
			     (*priv->hw_info->pce_microcode)[i].val_1);
		regmap_write(priv->gswip, GSWIP_PCE_TBL_VAL(2),
			     (*priv->hw_info->pce_microcode)[i].val_2);
		regmap_write(priv->gswip, GSWIP_PCE_TBL_VAL(3),
			     (*priv->hw_info->pce_microcode)[i].val_3);

		/* start the table access: */
		regmap_set_bits(priv->gswip, GSWIP_PCE_TBL_CTRL,
				GSWIP_PCE_TBL_CTRL_BAS);
		err = gswip_switch_r_timeout(priv, GSWIP_PCE_TBL_CTRL,
					     GSWIP_PCE_TBL_CTRL_BAS);
		if (err)
			return err;
	}

	/* tell the switch that the microcode is loaded */
	regmap_set_bits(priv->gswip, GSWIP_PCE_GCTRL_0,
			GSWIP_PCE_GCTRL_0_MC_VALID);

	return 0;
}

static void gswip_port_commit_pvid(struct gswip_priv *priv, int port)
{
	struct dsa_port *dp = dsa_to_port(priv->ds, port);
	struct net_device *br = dsa_port_bridge_dev_get(dp);
	u32 vinr;
	int idx;

	if (!dsa_port_is_user(dp))
		return;

	if (br) {
		u16 pvid = GSWIP_VLAN_UNAWARE_PVID;

		if (br_vlan_enabled(br))
			br_vlan_get_pvid(br, &pvid);

		/* VLAN-aware bridge ports with no PVID will use Active VLAN
		 * index 0. The expectation is that this drops all untagged and
		 * VID-0 tagged ingress traffic.
		 */
		idx = 0;
		for (int i = priv->hw_info->max_ports;
		     i < ARRAY_SIZE(priv->vlans); i++) {
			if (priv->vlans[i].bridge == br &&
			    priv->vlans[i].vid == pvid) {
				idx = i;
				break;
			}
		}
	} else {
		/* The Active VLAN table index as configured by
		 * gswip_add_single_port_br()
		 */
		idx = port + 1;
	}

	vinr = idx ? GSWIP_PCE_VCTRL_VINR_ALL : GSWIP_PCE_VCTRL_VINR_TAGGED;
	regmap_write_bits(priv->gswip, GSWIP_PCE_VCTRL(port),
			  GSWIP_PCE_VCTRL_VINR,
			  FIELD_PREP(GSWIP_PCE_VCTRL_VINR, vinr));

	/* Note that in GSWIP 2.2 VLAN mode the VID needs to be programmed
	 * directly instead of referencing the index in the Active VLAN Tablet.
	 * However, without the VLANMD bit (9) in PCE_GCTRL_1 (0x457) even
	 * GSWIP 2.2 and newer hardware maintain the GSWIP 2.1 behavior.
	 */
	regmap_write(priv->gswip, GSWIP_PCE_DEFPVID(port), idx);
}

static int gswip_port_vlan_filtering(struct dsa_switch *ds, int port,
				     bool vlan_filtering,
				     struct netlink_ext_ack *extack)
{
	struct gswip_priv *priv = ds->priv;

	if (vlan_filtering) {
		/* Use tag based VLAN */
		regmap_write_bits(priv->gswip, GSWIP_PCE_VCTRL(port),
				  GSWIP_PCE_VCTRL_VSR |
				  GSWIP_PCE_VCTRL_UVR |
				  GSWIP_PCE_VCTRL_VIMR |
				  GSWIP_PCE_VCTRL_VEMR |
				  GSWIP_PCE_VCTRL_VID0,
				  GSWIP_PCE_VCTRL_UVR |
				  GSWIP_PCE_VCTRL_VIMR |
				  GSWIP_PCE_VCTRL_VEMR |
				  GSWIP_PCE_VCTRL_VID0);
		regmap_clear_bits(priv->gswip, GSWIP_PCE_PCTRL_0p(port),
				  GSWIP_PCE_PCTRL_0_TVM);
	} else {
		/* Use port based VLAN */
		regmap_write_bits(priv->gswip, GSWIP_PCE_VCTRL(port),
				  GSWIP_PCE_VCTRL_UVR |
				  GSWIP_PCE_VCTRL_VIMR |
				  GSWIP_PCE_VCTRL_VEMR |
				  GSWIP_PCE_VCTRL_VID0 |
				  GSWIP_PCE_VCTRL_VSR,
				  GSWIP_PCE_VCTRL_VSR);
		regmap_set_bits(priv->gswip, GSWIP_PCE_PCTRL_0p(port),
				GSWIP_PCE_PCTRL_0_TVM);
	}

	gswip_port_commit_pvid(priv, port);

	return 0;
}

static void gswip_mii_delay_setup(struct gswip_priv *priv, struct dsa_port *dp,
				  phy_interface_t interface)
{
	u32 tx_delay = GSWIP_MII_PCDU_TXDLY_DEFAULT;
	u32 rx_delay = GSWIP_MII_PCDU_RXDLY_DEFAULT;
	struct device_node *port_dn = dp->dn;
	u16 mii_pcdu_reg;

	/* As MII_PCDU registers only exist for MII ports, silently return
	 * unless the port is an MII port
	 */
	if (!(priv->hw_info->mii_ports & BIT(dp->index)))
		return;

	switch (dp->index + priv->hw_info->mii_port_reg_offset) {
	case 0:
		mii_pcdu_reg = GSWIP_MII_PCDU0;
		break;
	case 1:
		mii_pcdu_reg = GSWIP_MII_PCDU1;
		break;
	case 5:
		mii_pcdu_reg = GSWIP_MII_PCDU5;
		break;
	default:
		return;
	}

	/* legacy code to set default delays according to the interface mode */
	switch (interface) {
	case PHY_INTERFACE_MODE_RGMII_ID:
		tx_delay = 0;
		rx_delay = 0;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		rx_delay = 0;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		tx_delay = 0;
		break;
	default:
		break;
	}

	/* allow settings delays using device tree properties */
	of_property_read_u32(port_dn, "rx-internal-delay-ps", &rx_delay);
	of_property_read_u32(port_dn, "tx-internal-delay-ps", &tx_delay);

	regmap_write_bits(priv->mii, mii_pcdu_reg,
			  GSWIP_MII_PCDU_TXDLY_MASK |
			  GSWIP_MII_PCDU_RXDLY_MASK,
			  GSWIP_MII_PCDU_TXDLY(tx_delay) |
			  GSWIP_MII_PCDU_RXDLY(rx_delay));
}

static int gswip_setup(struct dsa_switch *ds)
{
	unsigned int cpu_ports = dsa_cpu_ports(ds);
	struct gswip_priv *priv = ds->priv;
	struct dsa_port *cpu_dp;
	int err, i;

	regmap_write(priv->gswip, GSWIP_SWRES, GSWIP_SWRES_R0);
	usleep_range(5000, 10000);
	regmap_write(priv->gswip, GSWIP_SWRES, 0);

	/* disable port fetch/store dma on all ports */
	for (i = 0; i < priv->hw_info->max_ports; i++) {
		gswip_port_disable(ds, i);
		gswip_port_vlan_filtering(ds, i, false, NULL);
	}

	/* enable Switch */
	regmap_set_bits(priv->mdio, GSWIP_MDIO_GLOB, GSWIP_MDIO_GLOB_ENABLE);

	err = gswip_pce_load_microcode(priv);
	if (err) {
		dev_err(priv->dev, "writing PCE microcode failed, %i\n", err);
		return err;
	}

	/* Default unknown Broadcast/Multicast/Unicast port maps */
	regmap_write(priv->gswip, GSWIP_PCE_PMAP1, cpu_ports);
	regmap_write(priv->gswip, GSWIP_PCE_PMAP2, cpu_ports);
	regmap_write(priv->gswip, GSWIP_PCE_PMAP3, cpu_ports);

	/* Deactivate MDIO PHY auto polling. Some PHYs as the AR8030 have an
	 * interoperability problem with this auto polling mechanism because
	 * their status registers think that the link is in a different state
	 * than it actually is. For the AR8030 it has the BMSR_ESTATEN bit set
	 * as well as ESTATUS_1000_TFULL and ESTATUS_1000_XFULL. This makes the
	 * auto polling state machine consider the link being negotiated with
	 * 1Gbit/s. Since the PHY itself is a Fast Ethernet RMII PHY this leads
	 * to the switch port being completely dead (RX and TX are both not
	 * working).
	 * Also with various other PHY / port combinations (PHY11G GPHY, PHY22F
	 * GPHY, external RGMII PEF7071/7072) any traffic would stop. Sometimes
	 * it would work fine for a few minutes to hours and then stop, on
	 * other device it would no traffic could be sent or received at all.
	 * Testing shows that when PHY auto polling is disabled these problems
	 * go away.
	 */
	regmap_write(priv->mdio, GSWIP_MDIO_MDC_CFG0, 0x0);

	/* Configure the MDIO Clock 2.5 MHz */
	regmap_write_bits(priv->mdio, GSWIP_MDIO_MDC_CFG1, 0xff, 0x09);

	/* bring up the mdio bus */
	err = gswip_mdio(priv);
	if (err) {
		dev_err(priv->dev, "mdio bus setup failed\n");
		return err;
	}

	/* Disable the xMII interface and clear it's isolation bit */
	for (i = 0; i < priv->hw_info->max_ports; i++)
		gswip_mii_mask_cfg(priv,
				   GSWIP_MII_CFG_EN | GSWIP_MII_CFG_ISOLATE,
				   0, i);

	dsa_switch_for_each_cpu_port(cpu_dp, ds) {
		/* enable special tag insertion on cpu port */
		regmap_set_bits(priv->gswip, GSWIP_FDMA_PCTRLp(cpu_dp->index),
				GSWIP_FDMA_PCTRL_STEN);

		/* accept special tag in ingress direction */
		regmap_set_bits(priv->gswip,
				GSWIP_PCE_PCTRL_0p(cpu_dp->index),
				GSWIP_PCE_PCTRL_0_INGRESS);
	}

	regmap_set_bits(priv->gswip, GSWIP_BM_QUEUE_GCTRL,
			GSWIP_BM_QUEUE_GCTRL_GL_MOD);

	/* VLAN aware Switching */
	regmap_set_bits(priv->gswip, GSWIP_PCE_GCTRL_0,
			GSWIP_PCE_GCTRL_0_VLAN);

	/* Flush MAC Table */
	regmap_set_bits(priv->gswip, GSWIP_PCE_GCTRL_0,
			GSWIP_PCE_GCTRL_0_MTFL);

	err = gswip_switch_r_timeout(priv, GSWIP_PCE_GCTRL_0,
				     GSWIP_PCE_GCTRL_0_MTFL);
	if (err) {
		dev_err(priv->dev, "MAC flushing didn't finish\n");
		return err;
	}

	ds->mtu_enforcement_ingress = true;

	return 0;
}

static void gswip_teardown(struct dsa_switch *ds)
{
	struct gswip_priv *priv = ds->priv;

	regmap_clear_bits(priv->mdio, GSWIP_MDIO_GLOB, GSWIP_MDIO_GLOB_ENABLE);
}

static enum dsa_tag_protocol gswip_get_tag_protocol(struct dsa_switch *ds,
						    int port,
						    enum dsa_tag_protocol mp)
{
	struct gswip_priv *priv = ds->priv;

	return priv->hw_info->tag_protocol;
}

static int gswip_vlan_active_create(struct gswip_priv *priv,
				    struct net_device *bridge,
				    int fid, u16 vid)
{
	struct gswip_pce_table_entry vlan_active = {0,};
	unsigned int max_ports = priv->hw_info->max_ports;
	int idx = -1;
	int err;
	int i;

	/* Look for a free slot */
	for (i = max_ports; i < ARRAY_SIZE(priv->vlans); i++) {
		if (!priv->vlans[i].bridge) {
			idx = i;
			break;
		}
	}

	if (idx == -1)
		return -ENOSPC;

	if (fid == -1)
		fid = idx;

	vlan_active.index = idx;
	vlan_active.table = GSWIP_TABLE_ACTIVE_VLAN;
	vlan_active.key[0] = vid;
	vlan_active.val[0] = fid;
	vlan_active.valid = true;

	err = gswip_pce_table_entry_write(priv, &vlan_active);
	if (err) {
		dev_err(priv->dev, "failed to write active VLAN: %d\n",	err);
		return err;
	}

	priv->vlans[idx].bridge = bridge;
	priv->vlans[idx].vid = vid;
	priv->vlans[idx].fid = fid;

	return idx;
}

static int gswip_vlan_active_remove(struct gswip_priv *priv, int idx)
{
	struct gswip_pce_table_entry vlan_active = {0,};
	int err;

	vlan_active.index = idx;
	vlan_active.table = GSWIP_TABLE_ACTIVE_VLAN;
	vlan_active.valid = false;
	err = gswip_pce_table_entry_write(priv, &vlan_active);
	if (err)
		dev_err(priv->dev, "failed to delete active VLAN: %d\n", err);
	priv->vlans[idx].bridge = NULL;

	return err;
}

static int gswip_vlan_add(struct gswip_priv *priv, struct net_device *bridge,
			  int port, u16 vid, bool untagged, bool pvid,
			  bool vlan_aware)
{
	struct gswip_pce_table_entry vlan_mapping = {0,};
	unsigned int max_ports = priv->hw_info->max_ports;
	unsigned int cpu_ports = dsa_cpu_ports(priv->ds);
	bool active_vlan_created = false;
	int fid = -1, idx = -1;
	int i, err;

	/* Check if there is already a page for this bridge */
	for (i = max_ports; i < ARRAY_SIZE(priv->vlans); i++) {
		if (priv->vlans[i].bridge == bridge) {
			if (vlan_aware) {
				if (fid != -1 && fid != priv->vlans[i].fid)
					dev_err(priv->dev, "one bridge with multiple flow ids\n");
				fid = priv->vlans[i].fid;
			}
			if (priv->vlans[i].vid == vid) {
				idx = i;
				break;
			}
		}
	}

	/* If this bridge is not programmed yet, add a Active VLAN table
	 * entry in a free slot and prepare the VLAN mapping table entry.
	 */
	if (idx == -1) {
		idx = gswip_vlan_active_create(priv, bridge, fid, vid);
		if (idx < 0)
			return idx;
		active_vlan_created = true;

		vlan_mapping.index = idx;
		vlan_mapping.table = GSWIP_TABLE_VLAN_MAPPING;
	} else {
		/* Read the existing VLAN mapping entry from the switch */
		vlan_mapping.index = idx;
		vlan_mapping.table = GSWIP_TABLE_VLAN_MAPPING;
		err = gswip_pce_table_entry_read(priv, &vlan_mapping);
		if (err) {
			dev_err(priv->dev, "failed to read VLAN mapping: %d\n",
				err);
			return err;
		}
	}

	/* VLAN ID byte, maps to the VLAN ID of vlan active table */
	vlan_mapping.val[0] = vid;
	/* Update the VLAN mapping entry and write it to the switch */
	vlan_mapping.val[1] |= cpu_ports;
	vlan_mapping.val[1] |= BIT(port);
	if (vlan_aware)
		vlan_mapping.val[2] |= cpu_ports;
	if (untagged)
		vlan_mapping.val[2] &= ~BIT(port);
	else
		vlan_mapping.val[2] |= BIT(port);
	err = gswip_pce_table_entry_write(priv, &vlan_mapping);
	if (err) {
		dev_err(priv->dev, "failed to write VLAN mapping: %d\n", err);
		/* In case an Active VLAN was creaetd delete it again */
		if (active_vlan_created)
			gswip_vlan_active_remove(priv, idx);
		return err;
	}

	gswip_port_commit_pvid(priv, port);

	return 0;
}

static int gswip_vlan_remove(struct gswip_priv *priv,
			     struct net_device *bridge, int port,
			     u16 vid)
{
	struct gswip_pce_table_entry vlan_mapping = {0,};
	unsigned int max_ports = priv->hw_info->max_ports;
	int idx = -1;
	int i;
	int err;

	/* Check if there is already a page for this bridge */
	for (i = max_ports; i < ARRAY_SIZE(priv->vlans); i++) {
		if (priv->vlans[i].bridge == bridge &&
		    priv->vlans[i].vid == vid) {
			idx = i;
			break;
		}
	}

	if (idx == -1) {
		dev_err(priv->dev, "Port %d cannot find VID %u of bridge %s\n",
			port, vid, bridge ? bridge->name : "(null)");
		return -ENOENT;
	}

	vlan_mapping.index = idx;
	vlan_mapping.table = GSWIP_TABLE_VLAN_MAPPING;
	err = gswip_pce_table_entry_read(priv, &vlan_mapping);
	if (err) {
		dev_err(priv->dev, "failed to read VLAN mapping: %d\n",	err);
		return err;
	}

	vlan_mapping.val[1] &= ~BIT(port);
	vlan_mapping.val[2] &= ~BIT(port);
	err = gswip_pce_table_entry_write(priv, &vlan_mapping);
	if (err) {
		dev_err(priv->dev, "failed to write VLAN mapping: %d\n", err);
		return err;
	}

	/* In case all ports are removed from the bridge, remove the VLAN */
	if (!(vlan_mapping.val[1] & ~dsa_cpu_ports(priv->ds))) {
		err = gswip_vlan_active_remove(priv, idx);
		if (err) {
			dev_err(priv->dev, "failed to write active VLAN: %d\n",
				err);
			return err;
		}
	}

	gswip_port_commit_pvid(priv, port);

	return 0;
}

static int gswip_port_bridge_join(struct dsa_switch *ds, int port,
				  struct dsa_bridge bridge,
				  bool *tx_fwd_offload,
				  struct netlink_ext_ack *extack)
{
	struct net_device *br = bridge.dev;
	struct gswip_priv *priv = ds->priv;
	int err;

	/* Set up the VLAN for VLAN-unaware bridging for this port, and remove
	 * it from the "single-port bridge" through which it was operating as
	 * standalone.
	 */
	err = gswip_vlan_add(priv, br, port, GSWIP_VLAN_UNAWARE_PVID,
			     true, true, false);
	if (err)
		return err;

	return gswip_add_single_port_br(priv, port, false);
}

static void gswip_port_bridge_leave(struct dsa_switch *ds, int port,
				    struct dsa_bridge bridge)
{
	struct net_device *br = bridge.dev;
	struct gswip_priv *priv = ds->priv;

	/* Add the port back to the "single-port bridge", and remove it from
	 * the VLAN-unaware PVID created for this bridge.
	 */
	gswip_add_single_port_br(priv, port, true);
	gswip_vlan_remove(priv, br, port, GSWIP_VLAN_UNAWARE_PVID);
}

static int gswip_port_vlan_prepare(struct dsa_switch *ds, int port,
				   const struct switchdev_obj_port_vlan *vlan,
				   struct netlink_ext_ack *extack)
{
	struct net_device *bridge = dsa_port_bridge_dev_get(dsa_to_port(ds, port));
	struct gswip_priv *priv = ds->priv;
	unsigned int max_ports = priv->hw_info->max_ports;
	int pos = max_ports;
	int i, idx = -1;

	/* We only support VLAN filtering on bridges */
	if (!dsa_is_cpu_port(ds, port) && !bridge)
		return -EOPNOTSUPP;

	/* Check if there is already a page for this VLAN */
	for (i = max_ports; i < ARRAY_SIZE(priv->vlans); i++) {
		if (priv->vlans[i].bridge == bridge &&
		    priv->vlans[i].vid == vlan->vid) {
			idx = i;
			break;
		}
	}

	/* If this VLAN is not programmed yet, we have to reserve
	 * one entry in the VLAN table. Make sure we start at the
	 * next position round.
	 */
	if (idx == -1) {
		/* Look for a free slot */
		for (; pos < ARRAY_SIZE(priv->vlans); pos++) {
			if (!priv->vlans[pos].bridge) {
				idx = pos;
				pos++;
				break;
			}
		}

		if (idx == -1) {
			NL_SET_ERR_MSG_MOD(extack, "No slot in VLAN table");
			return -ENOSPC;
		}
	}

	return 0;
}

static int gswip_port_vlan_add(struct dsa_switch *ds, int port,
			       const struct switchdev_obj_port_vlan *vlan,
			       struct netlink_ext_ack *extack)
{
	struct net_device *bridge = dsa_port_bridge_dev_get(dsa_to_port(ds, port));
	struct gswip_priv *priv = ds->priv;
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	int err;

	if (vlan->vid == GSWIP_VLAN_UNAWARE_PVID)
		return 0;

	err = gswip_port_vlan_prepare(ds, port, vlan, extack);
	if (err)
		return err;

	/* We have to receive all packets on the CPU port and should not
	 * do any VLAN filtering here. This is also called with bridge
	 * NULL and then we do not know for which bridge to configure
	 * this.
	 */
	if (dsa_is_cpu_port(ds, port))
		return 0;

	return gswip_vlan_add(priv, bridge, port, vlan->vid, untagged, pvid,
			      true);
}

static int gswip_port_vlan_del(struct dsa_switch *ds, int port,
			       const struct switchdev_obj_port_vlan *vlan)
{
	struct net_device *bridge = dsa_port_bridge_dev_get(dsa_to_port(ds, port));
	struct gswip_priv *priv = ds->priv;

	if (vlan->vid == GSWIP_VLAN_UNAWARE_PVID)
		return 0;

	/* We have to receive all packets on the CPU port and should not
	 * do any VLAN filtering here. This is also called with bridge
	 * NULL and then we do not know for which bridge to configure
	 * this.
	 */
	if (dsa_is_cpu_port(ds, port))
		return 0;

	return gswip_vlan_remove(priv, bridge, port, vlan->vid);
}

static void gswip_port_fast_age(struct dsa_switch *ds, int port)
{
	struct gswip_priv *priv = ds->priv;
	struct gswip_pce_table_entry mac_bridge = {0,};
	int i;
	int err;

	for (i = 0; i < 2048; i++) {
		mac_bridge.table = GSWIP_TABLE_MAC_BRIDGE;
		mac_bridge.index = i;

		err = gswip_pce_table_entry_read(priv, &mac_bridge);
		if (err) {
			dev_err(priv->dev, "failed to read mac bridge: %d\n",
				err);
			return;
		}

		if (!mac_bridge.valid)
			continue;

		if (mac_bridge.val[1] & GSWIP_TABLE_MAC_BRIDGE_VAL1_STATIC)
			continue;

		if (port != FIELD_GET(GSWIP_TABLE_MAC_BRIDGE_VAL0_PORT,
				      mac_bridge.val[0]))
			continue;

		mac_bridge.valid = false;
		err = gswip_pce_table_entry_write(priv, &mac_bridge);
		if (err) {
			dev_err(priv->dev, "failed to write mac bridge: %d\n",
				err);
			return;
		}
	}
}

static void gswip_port_stp_state_set(struct dsa_switch *ds, int port, u8 state)
{
	struct gswip_priv *priv = ds->priv;
	u32 stp_state;

	switch (state) {
	case BR_STATE_DISABLED:
		regmap_clear_bits(priv->gswip, GSWIP_SDMA_PCTRLp(port),
				  GSWIP_SDMA_PCTRL_EN);
		return;
	case BR_STATE_BLOCKING:
	case BR_STATE_LISTENING:
		stp_state = GSWIP_PCE_PCTRL_0_PSTATE_LISTEN;
		break;
	case BR_STATE_LEARNING:
		stp_state = GSWIP_PCE_PCTRL_0_PSTATE_LEARNING;
		break;
	case BR_STATE_FORWARDING:
		stp_state = GSWIP_PCE_PCTRL_0_PSTATE_FORWARDING;
		break;
	default:
		dev_err(priv->dev, "invalid STP state: %d\n", state);
		return;
	}

	regmap_set_bits(priv->gswip, GSWIP_SDMA_PCTRLp(port),
			GSWIP_SDMA_PCTRL_EN);
	regmap_write_bits(priv->gswip, GSWIP_PCE_PCTRL_0p(port),
			  GSWIP_PCE_PCTRL_0_PSTATE_MASK,
			  stp_state);
}

static int gswip_port_fdb(struct dsa_switch *ds, int port,
			  struct net_device *bridge, const unsigned char *addr,
			  u16 vid, bool add)
{
	struct gswip_priv *priv = ds->priv;
	struct gswip_pce_table_entry mac_bridge = {0,};
	unsigned int max_ports = priv->hw_info->max_ports;
	int fid = -1;
	int i;
	int err;

	for (i = max_ports; i < ARRAY_SIZE(priv->vlans); i++) {
		if (priv->vlans[i].bridge == bridge) {
			fid = priv->vlans[i].fid;
			break;
		}
	}

	if (fid == -1) {
		dev_err(priv->dev, "no FID found for bridge %s\n",
			bridge->name);
		return -EINVAL;
	}

	mac_bridge.table = GSWIP_TABLE_MAC_BRIDGE;
	mac_bridge.key_mode = true;
	mac_bridge.key[0] = addr[5] | (addr[4] << 8);
	mac_bridge.key[1] = addr[3] | (addr[2] << 8);
	mac_bridge.key[2] = addr[1] | (addr[0] << 8);
	mac_bridge.key[3] = FIELD_PREP(GSWIP_TABLE_MAC_BRIDGE_KEY3_FID, fid);
	mac_bridge.val[0] = add ? BIT(port) : 0; /* port map */
	if (GSWIP_VERSION_GE(priv, GSWIP_VERSION_2_2_ETC))
		mac_bridge.val[1] = add ? (GSWIP_TABLE_MAC_BRIDGE_VAL1_STATIC |
					   GSWIP_TABLE_MAC_BRIDGE_VAL1_VALID) : 0;
	else
		mac_bridge.val[1] = GSWIP_TABLE_MAC_BRIDGE_VAL1_STATIC;

	mac_bridge.valid = add;

	err = gswip_pce_table_entry_write(priv, &mac_bridge);
	if (err)
		dev_err(priv->dev, "failed to write mac bridge: %d\n", err);

	return err;
}

static int gswip_port_fdb_add(struct dsa_switch *ds, int port,
			      const unsigned char *addr, u16 vid,
			      struct dsa_db db)
{
	if (db.type != DSA_DB_BRIDGE)
		return -EOPNOTSUPP;

	return gswip_port_fdb(ds, port, db.bridge.dev, addr, vid, true);
}

static int gswip_port_fdb_del(struct dsa_switch *ds, int port,
			      const unsigned char *addr, u16 vid,
			      struct dsa_db db)
{
	if (db.type != DSA_DB_BRIDGE)
		return -EOPNOTSUPP;

	return gswip_port_fdb(ds, port, db.bridge.dev, addr, vid, false);
}

static int gswip_port_fdb_dump(struct dsa_switch *ds, int port,
			       dsa_fdb_dump_cb_t *cb, void *data)
{
	struct gswip_priv *priv = ds->priv;
	struct gswip_pce_table_entry mac_bridge = {0,};
	unsigned char addr[ETH_ALEN];
	int i;
	int err;

	for (i = 0; i < 2048; i++) {
		mac_bridge.table = GSWIP_TABLE_MAC_BRIDGE;
		mac_bridge.index = i;

		err = gswip_pce_table_entry_read(priv, &mac_bridge);
		if (err) {
			dev_err(priv->dev,
				"failed to read mac bridge entry %d: %d\n",
				i, err);
			return err;
		}

		if (!mac_bridge.valid)
			continue;

		addr[5] = mac_bridge.key[0] & 0xff;
		addr[4] = (mac_bridge.key[0] >> 8) & 0xff;
		addr[3] = mac_bridge.key[1] & 0xff;
		addr[2] = (mac_bridge.key[1] >> 8) & 0xff;
		addr[1] = mac_bridge.key[2] & 0xff;
		addr[0] = (mac_bridge.key[2] >> 8) & 0xff;
		if (mac_bridge.val[1] & GSWIP_TABLE_MAC_BRIDGE_VAL1_STATIC) {
			if (mac_bridge.val[0] & BIT(port)) {
				err = cb(addr, 0, true, data);
				if (err)
					return err;
			}
		} else {
			if (port == FIELD_GET(GSWIP_TABLE_MAC_BRIDGE_VAL0_PORT,
					      mac_bridge.val[0])) {
				err = cb(addr, 0, false, data);
				if (err)
					return err;
			}
		}
	}
	return 0;
}

static int gswip_port_max_mtu(struct dsa_switch *ds, int port)
{
	/* Includes 8 bytes for special header. */
	return GSWIP_MAX_PACKET_LENGTH - VLAN_ETH_HLEN - ETH_FCS_LEN;
}

static int gswip_port_change_mtu(struct dsa_switch *ds, int port, int new_mtu)
{
	struct gswip_priv *priv = ds->priv;

	/* CPU port always has maximum mtu of user ports, so use it to set
	 * switch frame size, including 8 byte special header.
	 */
	if (dsa_is_cpu_port(ds, port)) {
		new_mtu += 8;
		regmap_write(priv->gswip, GSWIP_MAC_FLEN,
			     VLAN_ETH_HLEN + new_mtu + ETH_FCS_LEN);
	}

	/* Enable MLEN for ports with non-standard MTUs, including the special
	 * header on the CPU port added above.
	 */
	if (new_mtu != ETH_DATA_LEN)
		regmap_set_bits(priv->gswip, GSWIP_MAC_CTRL_2p(port),
				GSWIP_MAC_CTRL_2_MLEN);
	else
		regmap_clear_bits(priv->gswip, GSWIP_MAC_CTRL_2p(port),
				  GSWIP_MAC_CTRL_2_MLEN);

	return 0;
}

static void gswip_phylink_get_caps(struct dsa_switch *ds, int port,
				   struct phylink_config *config)
{
	struct gswip_priv *priv = ds->priv;

	priv->hw_info->phylink_get_caps(ds, port, config);
}

static void gswip_port_set_link(struct gswip_priv *priv, int port, bool link)
{
	u32 mdio_phy;

	if (link)
		mdio_phy = GSWIP_MDIO_PHY_LINK_UP;
	else
		mdio_phy = GSWIP_MDIO_PHY_LINK_DOWN;

	regmap_write_bits(priv->mdio, GSWIP_MDIO_PHYp(port),
			  GSWIP_MDIO_PHY_LINK_MASK, mdio_phy);
}

static void gswip_port_set_speed(struct gswip_priv *priv, int port, int speed,
				 phy_interface_t interface)
{
	u32 mdio_phy = 0, mii_cfg = 0, mac_ctrl_0 = 0;

	switch (speed) {
	case SPEED_10:
		mdio_phy = GSWIP_MDIO_PHY_SPEED_M10;

		if (interface == PHY_INTERFACE_MODE_RMII)
			mii_cfg = GSWIP_MII_CFG_RATE_M50;
		else
			mii_cfg = GSWIP_MII_CFG_RATE_M2P5;

		mac_ctrl_0 = GSWIP_MAC_CTRL_0_GMII_MII;
		break;

	case SPEED_100:
		mdio_phy = GSWIP_MDIO_PHY_SPEED_M100;

		if (interface == PHY_INTERFACE_MODE_RMII)
			mii_cfg = GSWIP_MII_CFG_RATE_M50;
		else
			mii_cfg = GSWIP_MII_CFG_RATE_M25;

		mac_ctrl_0 = GSWIP_MAC_CTRL_0_GMII_MII;
		break;

	case SPEED_1000:
		mdio_phy = GSWIP_MDIO_PHY_SPEED_G1;

		mii_cfg = GSWIP_MII_CFG_RATE_M125;

		mac_ctrl_0 = GSWIP_MAC_CTRL_0_GMII_RGMII;
		break;
	}

	regmap_write_bits(priv->mdio, GSWIP_MDIO_PHYp(port),
			  GSWIP_MDIO_PHY_SPEED_MASK, mdio_phy);
	gswip_mii_mask_cfg(priv, GSWIP_MII_CFG_RATE_MASK, mii_cfg, port);
	regmap_write_bits(priv->gswip, GSWIP_MAC_CTRL_0p(port),
			  GSWIP_MAC_CTRL_0_GMII_MASK, mac_ctrl_0);
}

static void gswip_port_set_duplex(struct gswip_priv *priv, int port, int duplex)
{
	u32 mac_ctrl_0, mdio_phy;

	if (duplex == DUPLEX_FULL) {
		mac_ctrl_0 = GSWIP_MAC_CTRL_0_FDUP_EN;
		mdio_phy = GSWIP_MDIO_PHY_FDUP_EN;
	} else {
		mac_ctrl_0 = GSWIP_MAC_CTRL_0_FDUP_DIS;
		mdio_phy = GSWIP_MDIO_PHY_FDUP_DIS;
	}

	regmap_write_bits(priv->gswip, GSWIP_MAC_CTRL_0p(port),
			  GSWIP_MAC_CTRL_0_FDUP_MASK, mac_ctrl_0);
	regmap_write_bits(priv->mdio, GSWIP_MDIO_PHYp(port),
			  GSWIP_MDIO_PHY_FDUP_MASK, mdio_phy);
}

static void gswip_port_set_pause(struct gswip_priv *priv, int port,
				 bool tx_pause, bool rx_pause)
{
	u32 mac_ctrl_0, mdio_phy;

	if (tx_pause && rx_pause) {
		mac_ctrl_0 = GSWIP_MAC_CTRL_0_FCON_RXTX;
		mdio_phy = GSWIP_MDIO_PHY_FCONTX_EN |
			   GSWIP_MDIO_PHY_FCONRX_EN;
	} else if (tx_pause) {
		mac_ctrl_0 = GSWIP_MAC_CTRL_0_FCON_TX;
		mdio_phy = GSWIP_MDIO_PHY_FCONTX_EN |
			   GSWIP_MDIO_PHY_FCONRX_DIS;
	} else if (rx_pause) {
		mac_ctrl_0 = GSWIP_MAC_CTRL_0_FCON_RX;
		mdio_phy = GSWIP_MDIO_PHY_FCONTX_DIS |
			   GSWIP_MDIO_PHY_FCONRX_EN;
	} else {
		mac_ctrl_0 = GSWIP_MAC_CTRL_0_FCON_NONE;
		mdio_phy = GSWIP_MDIO_PHY_FCONTX_DIS |
			   GSWIP_MDIO_PHY_FCONRX_DIS;
	}

	regmap_write_bits(priv->gswip, GSWIP_MAC_CTRL_0p(port),
			  GSWIP_MAC_CTRL_0_FCON_MASK, mac_ctrl_0);
	regmap_write_bits(priv->mdio, GSWIP_MDIO_PHYp(port),
			  GSWIP_MDIO_PHY_FCONTX_MASK | GSWIP_MDIO_PHY_FCONRX_MASK,
			  mdio_phy);
}

static void gswip_phylink_mac_config(struct phylink_config *config,
				     unsigned int mode,
				     const struct phylink_link_state *state)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct gswip_priv *priv = dp->ds->priv;
	int port = dp->index;
	u32 miicfg = 0;

	miicfg |= GSWIP_MII_CFG_LDCLKDIS;

	switch (state->interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		return;
	case PHY_INTERFACE_MODE_MII:
	case PHY_INTERFACE_MODE_INTERNAL:
		miicfg |= GSWIP_MII_CFG_MODE_MIIM;
		break;
	case PHY_INTERFACE_MODE_REVMII:
		miicfg |= GSWIP_MII_CFG_MODE_MIIP;
		break;
	case PHY_INTERFACE_MODE_RMII:
		miicfg |= GSWIP_MII_CFG_MODE_RMIIM;
		if (of_property_read_bool(dp->dn, "maxlinear,rmii-refclk-out"))
			miicfg |= GSWIP_MII_CFG_RMII_CLK;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		miicfg |= GSWIP_MII_CFG_MODE_RGMII;
		break;
	case PHY_INTERFACE_MODE_GMII:
		miicfg |= GSWIP_MII_CFG_MODE_GMII;
		break;
	default:
		dev_err(dp->ds->dev,
			"Unsupported interface: %d\n", state->interface);
		return;
	}

	gswip_mii_mask_cfg(priv,
			   GSWIP_MII_CFG_MODE_MASK | GSWIP_MII_CFG_RMII_CLK |
			   GSWIP_MII_CFG_RGMII_IBS | GSWIP_MII_CFG_LDCLKDIS,
			   miicfg, port);

	gswip_mii_delay_setup(priv, dp, state->interface);
}

static void gswip_phylink_mac_link_down(struct phylink_config *config,
					unsigned int mode,
					phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct gswip_priv *priv = dp->ds->priv;
	int port = dp->index;

	gswip_mii_mask_cfg(priv, GSWIP_MII_CFG_EN, 0, port);

	if (!dsa_port_is_cpu(dp))
		gswip_port_set_link(priv, port, false);
}

static void gswip_phylink_mac_link_up(struct phylink_config *config,
				      struct phy_device *phydev,
				      unsigned int mode,
				      phy_interface_t interface,
				      int speed, int duplex,
				      bool tx_pause, bool rx_pause)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct gswip_priv *priv = dp->ds->priv;
	int port = dp->index;

	if (!dsa_port_is_cpu(dp) || interface != PHY_INTERFACE_MODE_INTERNAL) {
		gswip_port_set_link(priv, port, true);
		gswip_port_set_speed(priv, port, speed, interface);
		gswip_port_set_duplex(priv, port, duplex);
		gswip_port_set_pause(priv, port, tx_pause, rx_pause);
	}

	gswip_mii_mask_cfg(priv, GSWIP_MII_CFG_EN, GSWIP_MII_CFG_EN, port);
}

static void gswip_get_strings(struct dsa_switch *ds, int port, u32 stringset,
			      uint8_t *data)
{
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < ARRAY_SIZE(gswip_rmon_cnt); i++)
		ethtool_puts(&data, gswip_rmon_cnt[i].name);
}

static u32 gswip_bcm_ram_entry_read(struct gswip_priv *priv, u32 table,
				    u32 index)
{
	u32 result, val;
	int err;

	regmap_write(priv->gswip, GSWIP_BM_RAM_ADDR, index);
	regmap_write_bits(priv->gswip, GSWIP_BM_RAM_CTRL,
			  GSWIP_BM_RAM_CTRL_ADDR_MASK | GSWIP_BM_RAM_CTRL_OPMOD |
			  GSWIP_BM_RAM_CTRL_BAS,
			  table | GSWIP_BM_RAM_CTRL_BAS);

	err = gswip_switch_r_timeout(priv, GSWIP_BM_RAM_CTRL,
				     GSWIP_BM_RAM_CTRL_BAS);
	if (err) {
		dev_err(priv->dev, "timeout while reading table: %u, index: %u\n",
			table, index);
		return 0;
	}

	regmap_read(priv->gswip, GSWIP_BM_RAM_VAL(0), &result);
	regmap_read(priv->gswip, GSWIP_BM_RAM_VAL(1), &val);
	result |= val << 16;

	return result;
}

static void gswip_get_ethtool_stats(struct dsa_switch *ds, int port,
				    uint64_t *data)
{
	struct gswip_priv *priv = ds->priv;
	const struct gswip_rmon_cnt_desc *rmon_cnt;
	int i;
	u64 high;

	for (i = 0; i < ARRAY_SIZE(gswip_rmon_cnt); i++) {
		rmon_cnt = &gswip_rmon_cnt[i];

		data[i] = gswip_bcm_ram_entry_read(priv, port,
						   rmon_cnt->offset);
		if (rmon_cnt->size == 2) {
			high = gswip_bcm_ram_entry_read(priv, port,
							rmon_cnt->offset + 1);
			data[i] |= high << 32;
		}
	}
}

static int gswip_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	if (sset != ETH_SS_STATS)
		return 0;

	return ARRAY_SIZE(gswip_rmon_cnt);
}

static int gswip_set_mac_eee(struct dsa_switch *ds, int port,
			     struct ethtool_keee *e)
{
	if (e->tx_lpi_timer > 0x7f)
		return -EINVAL;

	return 0;
}

static void gswip_phylink_mac_disable_tx_lpi(struct phylink_config *config)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct gswip_priv *priv = dp->ds->priv;

	regmap_clear_bits(priv->gswip, GSWIP_MAC_CTRL_4p(dp->index),
			  GSWIP_MAC_CTRL_4_LPIEN);
}

static int gswip_phylink_mac_enable_tx_lpi(struct phylink_config *config,
					   u32 timer, bool tx_clock_stop)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct gswip_priv *priv = dp->ds->priv;

	return regmap_update_bits(priv->gswip, GSWIP_MAC_CTRL_4p(dp->index),
				  GSWIP_MAC_CTRL_4_LPIEN |
				  GSWIP_MAC_CTRL_4_GWAIT_MASK |
				  GSWIP_MAC_CTRL_4_WAIT_MASK,
				  GSWIP_MAC_CTRL_4_LPIEN |
				  GSWIP_MAC_CTRL_4_GWAIT(timer) |
				  GSWIP_MAC_CTRL_4_WAIT(timer));
}

static bool gswip_support_eee(struct dsa_switch *ds, int port)
{
	struct gswip_priv *priv = ds->priv;

	if (GSWIP_VERSION_GE(priv, GSWIP_VERSION_2_2))
		return true;

	return false;
}

static struct phylink_pcs *gswip_phylink_mac_select_pcs(struct phylink_config *config,
							phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct gswip_priv *priv = dp->ds->priv;

	if (priv->hw_info->mac_select_pcs)
		return priv->hw_info->mac_select_pcs(config, interface);

	return NULL;
}

static const struct phylink_mac_ops gswip_phylink_mac_ops = {
	.mac_config		= gswip_phylink_mac_config,
	.mac_link_down		= gswip_phylink_mac_link_down,
	.mac_link_up		= gswip_phylink_mac_link_up,
	.mac_disable_tx_lpi	= gswip_phylink_mac_disable_tx_lpi,
	.mac_enable_tx_lpi	= gswip_phylink_mac_enable_tx_lpi,
	.mac_select_pcs		= gswip_phylink_mac_select_pcs,
};

static const struct dsa_switch_ops gswip_switch_ops = {
	.get_tag_protocol	= gswip_get_tag_protocol,
	.setup			= gswip_setup,
	.teardown		= gswip_teardown,
	.port_setup		= gswip_port_setup,
	.port_enable		= gswip_port_enable,
	.port_disable		= gswip_port_disable,
	.port_pre_bridge_flags	= gswip_port_pre_bridge_flags,
	.port_bridge_flags	= gswip_port_bridge_flags,
	.port_bridge_join	= gswip_port_bridge_join,
	.port_bridge_leave	= gswip_port_bridge_leave,
	.port_fast_age		= gswip_port_fast_age,
	.port_vlan_filtering	= gswip_port_vlan_filtering,
	.port_vlan_add		= gswip_port_vlan_add,
	.port_vlan_del		= gswip_port_vlan_del,
	.port_stp_state_set	= gswip_port_stp_state_set,
	.port_fdb_add		= gswip_port_fdb_add,
	.port_fdb_del		= gswip_port_fdb_del,
	.port_fdb_dump		= gswip_port_fdb_dump,
	.port_change_mtu	= gswip_port_change_mtu,
	.port_max_mtu		= gswip_port_max_mtu,
	.phylink_get_caps	= gswip_phylink_get_caps,
	.get_strings		= gswip_get_strings,
	.get_ethtool_stats	= gswip_get_ethtool_stats,
	.get_sset_count		= gswip_get_sset_count,
	.set_mac_eee		= gswip_set_mac_eee,
	.support_eee		= gswip_support_eee,
	.port_hsr_join		= dsa_port_simple_hsr_join,
	.port_hsr_leave		= dsa_port_simple_hsr_leave,
};

static int gswip_validate_cpu_port(struct dsa_switch *ds)
{
	struct gswip_priv *priv = ds->priv;
	struct dsa_port *cpu_dp;
	int cpu_port = -1;

	dsa_switch_for_each_cpu_port(cpu_dp, ds) {
		if (cpu_port != -1)
			return dev_err_probe(ds->dev, -EINVAL,
					     "only a single CPU port is supported\n");

		cpu_port = cpu_dp->index;
	}

	if (cpu_port == -1)
		return dev_err_probe(ds->dev, -EINVAL, "no CPU port defined\n");

	if (BIT(cpu_port) & ~priv->hw_info->allowed_cpu_ports)
		return dev_err_probe(ds->dev, -EINVAL,
				     "unsupported CPU port defined\n");

	return 0;
}

int gswip_probe_common(struct gswip_priv *priv, u32 version)
{
	int err;

	mutex_init(&priv->pce_table_lock);

	priv->ds = devm_kzalloc(priv->dev, sizeof(*priv->ds), GFP_KERNEL);
	if (!priv->ds)
		return -ENOMEM;

	priv->ds->dev = priv->dev;
	priv->ds->num_ports = priv->hw_info->max_ports;
	priv->ds->ops = &gswip_switch_ops;
	priv->ds->phylink_mac_ops = &gswip_phylink_mac_ops;
	priv->ds->priv = priv;

	/* The hardware has the 'major/minor' version bytes in the wrong order
	 * preventing numerical comparisons. Construct a 16-bit unsigned integer
	 * having the REV field as most significant byte and the MOD field as
	 * least significant byte. This is effectively swapping the two bytes of
	 * the version variable, but other than using swab16 it doesn't affect
	 * the source variable.
	 */
	priv->version = GSWIP_VERSION_REV(version) << 8 |
			GSWIP_VERSION_MOD(version);

	err = dsa_register_switch(priv->ds);
	if (err)
		return dev_err_probe(priv->dev, err, "dsa switch registration failed\n");

	err = gswip_validate_cpu_port(priv->ds);
	if (err)
		goto unregister_switch;

	dev_info(priv->dev, "probed GSWIP version %lx mod %lx\n",
		 GSWIP_VERSION_REV(version), GSWIP_VERSION_MOD(version));

	return 0;

unregister_switch:
	dsa_unregister_switch(priv->ds);

	return err;
}
EXPORT_SYMBOL_GPL(gswip_probe_common);

MODULE_AUTHOR("Hauke Mehrtens <hauke@hauke-m.de>");
MODULE_AUTHOR("Daniel Golle <daniel@makrotopia.org>");
MODULE_DESCRIPTION("Lantiq / Intel / MaxLinear GSWIP common functions");
MODULE_LICENSE("GPL");
