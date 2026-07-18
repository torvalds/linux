// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * NXP NETC switch driver
 * Copyright 2025-2026 NXP
 */

#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/fsl/enetc_mdio.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/of_mdio.h>

#include "netc_switch.h"

static struct netc_fdb_entry *
netc_lookup_fdb_entry(struct netc_switch *priv,
		      const unsigned char *addr,
		      u16 vid)
{
	struct netc_fdb_entry *entry;

	hlist_for_each_entry(entry, &priv->fdb_list, node)
		if (ether_addr_equal(entry->keye.mac_addr, addr) &&
		    le16_to_cpu(entry->keye.fid) == vid)
			return entry;

	return NULL;
}

static void netc_destroy_fdb_list(struct netc_switch *priv)
{
	struct netc_fdb_entry *entry;
	struct hlist_node *tmp;

	hlist_for_each_entry_safe(entry, tmp, &priv->fdb_list, node)
		netc_del_fdb_entry(entry);
}

static struct netc_vlan_entry *
netc_lookup_vlan_entry(struct netc_switch *priv, u16 vid)
{
	struct netc_vlan_entry *entry;

	hlist_for_each_entry(entry, &priv->vlan_list, node)
		if (entry->vid == vid)
			return entry;

	return NULL;
}

static void netc_destroy_vlan_list(struct netc_switch *priv)
{
	struct netc_vlan_entry *entry;
	struct hlist_node *tmp;

	hlist_for_each_entry_safe(entry, tmp, &priv->vlan_list, node)
		netc_del_vlan_entry(entry);
}

static enum dsa_tag_protocol
netc_get_tag_protocol(struct dsa_switch *ds, int port,
		      enum dsa_tag_protocol mprot)
{
	return DSA_TAG_PROTO_NETC;
}

static void netc_port_rmw(struct netc_port *np, u32 reg,
			  u32 mask, u32 val)
{
	u32 old, new;

	WARN_ON((mask | val) != mask);

	old = netc_port_rd(np, reg);
	new = (old & ~mask) | val;
	if (new == old)
		return;

	netc_port_wr(np, reg, new);
}

static void netc_mac_port_wr(struct netc_port *np, u32 reg, u32 val)
{
	if (is_netc_pseudo_port(np))
		return;

	netc_port_wr(np, reg, val);
	if (np->caps.pmac)
		netc_port_wr(np, reg + NETC_PMAC_OFFSET, val);
}

/* netc_mac_port_rmw() is used to synchronize the configurations of eMAC
 * and pMAC to maintain consistency. This function should not be used if
 * differentiated settings are required.
 */
static void netc_mac_port_rmw(struct netc_port *np, u32 reg,
			      u32 mask, u32 val)
{
	u32 old, new;

	if (is_netc_pseudo_port(np))
		return;

	WARN_ON((mask | val) != mask);

	old = netc_port_rd(np, reg);
	new = (old & ~mask) | val;
	if (new == old)
		return;

	netc_port_wr(np, reg, new);
	if (np->caps.pmac)
		netc_port_wr(np, reg + NETC_PMAC_OFFSET, new);
}

static void netc_port_get_capability(struct netc_port *np)
{
	u32 val;

	val = netc_port_rd(np, NETC_PMCAPR);
	if (val & PMCAPR_HD)
		np->caps.half_duplex = true;

	if (FIELD_GET(PMCAPR_FP, val) == FP_SUPPORT)
		np->caps.pmac = true;

	val = netc_port_rd(np, NETC_PCAPR);
	if (val & PCAPR_LINK_TYPE)
		np->caps.pseudo_link = true;
}

static int netc_port_get_info_from_dt(struct netc_port *np,
				      struct device_node *node,
				      struct device *dev)
{
	if (of_find_property(node, "clock-names", NULL)) {
		np->ref_clk = devm_get_clk_from_child(dev, node, "ref");
		if (IS_ERR(np->ref_clk)) {
			dev_err(dev, "Port %d cannot get reference clock\n",
				np->dp->index);
			return PTR_ERR(np->ref_clk);
		}
	}

	return 0;
}

static int netc_port_create_emdio_bus(struct netc_port *np,
				      struct device_node *node)
{
	struct netc_switch *priv = np->switch_priv;
	struct enetc_mdio_priv *mdio_priv;
	struct device *dev = priv->dev;
	struct enetc_hw *hw;
	struct mii_bus *bus;
	int err;

	hw = enetc_hw_alloc(dev, np->iobase);
	if (IS_ERR(hw))
		return dev_err_probe(dev, PTR_ERR(hw),
				     "Failed to allocate enetc_hw\n");

	bus = devm_mdiobus_alloc_size(dev, sizeof(*mdio_priv));
	if (!bus)
		return -ENOMEM;

	bus->name = "NXP NETC switch external MDIO Bus";
	bus->read = enetc_mdio_read_c22;
	bus->write = enetc_mdio_write_c22;
	bus->read_c45 = enetc_mdio_read_c45;
	bus->write_c45 = enetc_mdio_write_c45;
	bus->parent = dev;
	mdio_priv = bus->priv;
	mdio_priv->hw = hw;
	mdio_priv->mdio_base = NETC_EMDIO_BASE;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-p%d-emdio",
		 dev_name(dev), np->dp->index);

	err = devm_of_mdiobus_register(dev, bus, node);
	if (err)
		return dev_err_probe(dev, err,
				     "Cannot register EMDIO bus\n");

	np->emdio = bus;

	return 0;
}

static int netc_port_create_mdio_bus(struct netc_port *np,
				     struct device_node *node)
{
	struct device_node *mdio_node;
	int err;

	mdio_node = of_get_child_by_name(node, "mdio");
	if (mdio_node) {
		err = netc_port_create_emdio_bus(np, mdio_node);
		of_node_put(mdio_node);
		if (err)
			return err;
	}

	return 0;
}

static int netc_init_switch_id(struct netc_switch *priv)
{
	struct netc_switch_regs *regs = &priv->regs;
	struct dsa_switch *ds = priv->ds;

	/* The value of 0 is reserved for the VEPA switch and cannot
	 * be used. So 'dsa,member' is a required property for NETC
	 * switch, the member is used to specify the switch ID, which
	 * cannot be zero. This way, the hardware switch ID and the
	 * software switch ID are consistent.
	 */
	if (ds->index > FIELD_MAX(SWCR_SWID) || !ds->index) {
		dev_err(priv->dev, "Switch index %d out of range\n",
			ds->index);
		return -ERANGE;
	}

	netc_base_wr(regs, NETC_SWCR, ds->index);

	return 0;
}

static void netc_get_switch_capabilities(struct netc_switch *priv)
{
	struct netc_switch_regs *regs = &priv->regs;
	u32 val;

	val = netc_base_rd(regs, NETC_HTMCAPR);
	priv->htmcapr_num_words = FIELD_GET(HTMCAPR_NUM_WORDS, val);

	val = netc_base_rd(regs, NETC_BPCAPR);
	priv->num_bp = FIELD_GET(BPCAPR_NUM_BP, val);
}

static int netc_init_all_ports(struct netc_switch *priv)
{
	struct device *dev = priv->dev;
	struct netc_port *np;
	struct dsa_port *dp;
	int ett_offset = 0;
	int err;

	priv->ports = devm_kcalloc(dev, priv->info->num_ports,
				   sizeof(struct netc_port *),
				   GFP_KERNEL);
	if (!priv->ports)
		return -ENOMEM;

	/* Some DSA interfaces may set the port even it is disabled, such
	 * as .port_disable(), .port_stp_state_set() and so on. To avoid
	 * crash caused by accessing NULL port pointer, each port is
	 * allocated its own memory. Otherwise, we need to check whether
	 * the port pointer is NULL in these interfaces. The latter is
	 * difficult for us to cover.
	 */
	for (int i = 0; i < priv->info->num_ports; i++) {
		np = devm_kzalloc(dev, sizeof(*np), GFP_KERNEL);
		if (!np)
			return -ENOMEM;

		np->switch_priv = priv;
		np->iobase = priv->regs.port + PORT_IOBASE(i);
		netc_port_get_capability(np);
		priv->ports[i] = np;
	}

	dsa_switch_for_each_available_port(dp, priv->ds) {
		np = priv->ports[dp->index];
		np->dp = dp;
		np->ett_offset = ett_offset++;
		priv->port_bitmap |= BIT(dp->index);

		err = netc_port_get_info_from_dt(np, dp->dn, dev);
		if (err)
			return err;

		if (dsa_port_is_user(dp)) {
			err = netc_port_create_mdio_bus(np, dp->dn);
			if (err) {
				dev_err(dev, "Failed to create MDIO bus\n");
				return err;
			}
		}
	}

	return 0;
}

static void netc_init_ntmp_tbl_versions(struct netc_switch *priv)
{
	struct ntmp_user *ntmp = &priv->ntmp;

	/* All tables default to version 0 */
	memset(&ntmp->tbl, 0, sizeof(ntmp->tbl));
}

static int netc_init_all_cbdrs(struct netc_switch *priv)
{
	struct netc_switch_regs *regs = &priv->regs;
	struct ntmp_user *ntmp = &priv->ntmp;
	int i, err;

	ntmp->cbdr_num = NETC_CBDR_NUM;
	ntmp->dev = priv->dev;
	ntmp->ring = devm_kcalloc(ntmp->dev, ntmp->cbdr_num,
				  sizeof(struct netc_cbdr),
				  GFP_KERNEL);
	if (!ntmp->ring)
		return -ENOMEM;

	for (i = 0; i < ntmp->cbdr_num; i++) {
		struct netc_cbdr *cbdr = &ntmp->ring[i];
		struct netc_cbdr_regs cbdr_regs;

		cbdr_regs.pir = regs->base + NETC_CBDRPIR(i);
		cbdr_regs.cir = regs->base + NETC_CBDRCIR(i);
		cbdr_regs.mr = regs->base + NETC_CBDRMR(i);
		cbdr_regs.bar0 = regs->base + NETC_CBDRBAR0(i);
		cbdr_regs.bar1 = regs->base + NETC_CBDRBAR1(i);
		cbdr_regs.lenr = regs->base + NETC_CBDRLENR(i);

		err = ntmp_init_cbdr(cbdr, ntmp->dev, &cbdr_regs);
		if (err)
			goto free_cbdrs;
	}

	return 0;

free_cbdrs:
	for (i--; i >= 0; i--)
		ntmp_free_cbdr(&ntmp->ring[i]);

	return err;
}

static void netc_remove_all_cbdrs(struct netc_switch *priv)
{
	struct ntmp_user *ntmp = &priv->ntmp;

	for (int i = 0; i < NETC_CBDR_NUM; i++)
		ntmp_free_cbdr(&ntmp->ring[i]);
}

static u32 netc_num_available_ports(struct netc_switch *priv)
{
	struct dsa_port *dp;
	u32 num_ports = 0;

	dsa_switch_for_each_available_port(dp, priv->ds)
		num_ports++;

	return num_ports;
}

static int netc_init_ntmp_bitmap_sizes(struct netc_switch *priv)
{
	u32 num_ports = netc_num_available_ports(priv);
	struct netc_switch_regs *regs = &priv->regs;
	struct ntmp_user *ntmp = &priv->ntmp;
	u32 val;

	if (!num_ports)
		return -EINVAL;

	val = netc_base_rd(regs, NETC_ETTCAPR);
	ntmp->ett_bitmap_size = NETC_GET_NUM_ENTRIES(val) / num_ports;
	if (!ntmp->ett_bitmap_size)
		return -EINVAL;

	val = netc_base_rd(regs, NETC_ECTCAPR);
	ntmp->ect_bitmap_size = NETC_GET_NUM_ENTRIES(val) / num_ports;
	if (!ntmp->ect_bitmap_size)
		return -EINVAL;

	return 0;
}

static int netc_init_ntmp_bitmaps(struct netc_switch *priv)
{
	struct ntmp_user *ntmp = &priv->ntmp;

	ntmp->ett_gid_bitmap = bitmap_zalloc(ntmp->ett_bitmap_size,
					     GFP_KERNEL);
	if (!ntmp->ett_gid_bitmap)
		return -ENOMEM;

	ntmp->ect_gid_bitmap = bitmap_zalloc(ntmp->ect_bitmap_size,
					     GFP_KERNEL);
	if (!ntmp->ect_gid_bitmap)
		goto free_ett_gid_bitmap;

	return 0;

free_ett_gid_bitmap:
	bitmap_free(ntmp->ett_gid_bitmap);
	ntmp->ett_gid_bitmap = NULL;

	return -ENOMEM;
}

static void netc_free_ntmp_bitmaps(struct netc_switch *priv)
{
	struct ntmp_user *ntmp = &priv->ntmp;

	bitmap_free(ntmp->ect_gid_bitmap);
	ntmp->ect_gid_bitmap = NULL;

	bitmap_free(ntmp->ett_gid_bitmap);
	ntmp->ett_gid_bitmap = NULL;
}

static int netc_init_ntmp_user(struct netc_switch *priv)
{
	int err;

	netc_init_ntmp_tbl_versions(priv);

	err = netc_init_ntmp_bitmap_sizes(priv);
	if (err)
		return err;

	err = netc_init_ntmp_bitmaps(priv);
	if (err)
		return err;

	err = netc_init_all_cbdrs(priv);
	if (err)
		goto free_ntmp_bitmaps;

	return 0;

free_ntmp_bitmaps:
	netc_free_ntmp_bitmaps(priv);

	return err;
}

static void netc_free_ntmp_user(struct netc_switch *priv)
{
	netc_remove_all_cbdrs(priv);
	netc_free_ntmp_bitmaps(priv);
}

static void netc_clean_fdbt_ageing_entries(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct netc_switch *priv;

	priv = container_of(dwork, struct netc_switch, fdbt_ageing_work);

	/* Update the activity element in FDB table */
	mutex_lock(&priv->fdbt_lock);
	ntmp_fdbt_update_activity_element(&priv->ntmp);
	/* Delete the ageing entries after the activity element is updated */
	ntmp_fdbt_delete_ageing_entries(&priv->ntmp, NETC_FDBT_AGEING_THRESH);
	mutex_unlock(&priv->fdbt_lock);

	if (atomic_read(&priv->br_cnt))
		schedule_delayed_work(&priv->fdbt_ageing_work,
				      READ_ONCE(priv->fdbt_ageing_delay));
}

static void netc_switch_dos_default_config(struct netc_switch *priv)
{
	struct netc_switch_regs *regs = &priv->regs;
	u32 val;

	val = DOSL2CR_SAMEADDR | DOSL2CR_MSAMCC;
	netc_base_wr(regs, NETC_DOSL2CR, val);

	val = DOSL3CR_SAMEADDR | DOSL3CR_IPSAMCC;
	netc_base_wr(regs, NETC_DOSL3CR, val);
}

static void netc_switch_vfht_default_config(struct netc_switch *priv)
{
	struct netc_switch_regs *regs = &priv->regs;
	u32 val;

	val = netc_base_rd(regs, NETC_VFHTDECR2);

	/* If no match is found in the VLAN Filter table, then VFHTDECR2[MLO]
	 * will take effect. VFHTDECR2[MLO] is set to "Software MAC learning
	 * secure" by default. Notice BPCR[MLO] will override VFHTDECR2[MLO]
	 * if its value is not zero.
	 */
	val = u32_replace_bits(val, MLO_SW_SEC, VFHTDECR2_MLO);
	val = u32_replace_bits(val, MFO_NO_MATCH_DISCARD, VFHTDECR2_MFO);
	netc_base_wr(regs, NETC_VFHTDECR2, val);
}

static void netc_port_set_max_frame_size(struct netc_port *np,
					 u32 max_frame_size)
{
	netc_mac_port_wr(np, NETC_PM_MAXFRM(0),
			 max_frame_size & PM_MAXFRAM);
}

static void netc_switch_fixed_config(struct netc_switch *priv)
{
	netc_switch_dos_default_config(priv);
	netc_switch_vfht_default_config(priv);
}

static void netc_port_set_tc_max_sdu(struct netc_port *np,
				     int tc, u32 max_sdu)
{
	u32 val = FIELD_PREP(PTCTMSDUR_MAXSDU, max_sdu) |
		  FIELD_PREP(PTCTMSDUR_SDU_TYPE, SDU_TYPE_MPDU);

	netc_port_wr(np, NETC_PTCTMSDUR(tc), val);
}

static void netc_port_set_all_tc_msdu(struct netc_port *np)
{
	for (int tc = 0; tc < NETC_TC_NUM; tc++)
		netc_port_set_tc_max_sdu(np, tc, NETC_MAX_FRAME_LEN);
}

static void netc_port_set_mlo(struct netc_port *np, enum netc_mlo mlo)
{
	netc_port_rmw(np, NETC_BPCR, BPCR_MLO, FIELD_PREP(BPCR_MLO, mlo));
}

static void netc_port_set_pvid(struct netc_port *np, u16 pvid)
{
	netc_port_rmw(np, NETC_BPDVR, BPDVR_VID, pvid);
}

static void netc_port_set_vlan_aware(struct netc_port *np, bool aware)
{
	netc_port_rmw(np, NETC_BPDVR, BPDVR_RXVAM,
		      aware ? 0 : BPDVR_RXVAM);
}

static void netc_port_fixed_config(struct netc_port *np)
{
	/* Default IPV and DR setting */
	netc_port_rmw(np, NETC_PQOSMR, PQOSMR_VS | PQOSMR_VE,
		      PQOSMR_VS | PQOSMR_VE);

	/* Enable L2 and L3 DOS */
	netc_port_rmw(np, NETC_PCR, PCR_L2DOSE | PCR_L3DOSE,
		      PCR_L2DOSE | PCR_L3DOSE);

	/* Set the quanta value of TX PAUSE frame */
	netc_mac_port_wr(np, NETC_PM_PAUSE_QUANTA(0), NETC_PAUSE_QUANTA);

	/* When a quanta timer counts down and reaches this value,
	 * the MAC sends a refresh PAUSE frame with the programmed
	 * full quanta value if a pause condition still exists.
	 */
	netc_mac_port_wr(np, NETC_PM_PAUSE_THRESH(0), NETC_PAUSE_THRESH);
}

static void netc_port_default_config(struct netc_port *np)
{
	netc_port_fixed_config(np);

	/* Default VLAN unaware */
	netc_port_set_vlan_aware(np, false);

	if (dsa_port_is_cpu(np->dp))
		/* For CPU port, source port pruning is disabled */
		netc_port_rmw(np, NETC_BPCR, BPCR_SRCPRND, BPCR_SRCPRND);
	else
		netc_port_set_mlo(np, MLO_DISABLE);

	netc_port_set_max_frame_size(np, NETC_MAX_FRAME_LEN);
	netc_port_set_all_tc_msdu(np);
}

static u32 netc_available_port_bitmap(struct netc_switch *priv)
{
	struct dsa_port *dp;
	u32 bitmap = 0;

	dsa_switch_for_each_available_port(dp, priv->ds)
		bitmap |= BIT(dp->index);

	return bitmap;
}

static int netc_add_standalone_vlan_entry(struct netc_switch *priv)
{
	u32 bitmap_stg = VFT_STG_ID(0) | netc_available_port_bitmap(priv);
	struct vft_cfge_data *cfge;
	u16 cfg;
	int err;

	cfge = kzalloc_obj(*cfge);
	if (!cfge)
		return -ENOMEM;

	cfge->bitmap_stg = cpu_to_le32(bitmap_stg);
	cfge->et_eid = cpu_to_le32(NTMP_NULL_ENTRY_ID);
	cfge->fid = cpu_to_le16(NETC_STANDALONE_PVID);

	/* For standalone ports, MAC learning needs to be disabled, so frames
	 * from other user ports will not be forwarded to the standalone ports,
	 * because there are no FDB entries on the standalone ports. Also, the
	 * frames received by the standalone ports cannot be flooded to other
	 * ports, so MAC forwarding option needs to be set to
	 * MFO_NO_MATCH_DISCARD, so the frames will be discarded rather than
	 * flooding to other ports.
	 */
	cfg = FIELD_PREP(VFT_MLO, MLO_DISABLE) |
	      FIELD_PREP(VFT_MFO, MFO_NO_MATCH_DISCARD);
	cfge->cfg = cpu_to_le16(cfg);

	err = ntmp_vft_add_entry(&priv->ntmp, NETC_STANDALONE_PVID, cfge);
	if (err)
		dev_err(priv->dev,
			"Failed to add standalone VLAN entry\n");

	kfree(cfge);

	return err;
}

static int netc_port_add_fdb_entry(struct netc_port *np,
				   const unsigned char *addr, u16 vid)
{
	struct netc_switch *priv = np->switch_priv;
	struct netc_fdb_entry *entry;
	struct fdbt_keye_data *keye;
	struct fdbt_cfge_data *cfge;
	int port = np->dp->index;
	u32 cfg = 0;
	int err;

	entry = kzalloc_obj(*entry);
	if (!entry)
		return -ENOMEM;

	keye = &entry->keye;
	cfge = &entry->cfge;
	ether_addr_copy(keye->mac_addr, addr);
	keye->fid = cpu_to_le16(vid);

	cfge->port_bitmap = cpu_to_le32(BIT(port));
	cfge->cfg = cpu_to_le32(cfg);
	cfge->et_eid = cpu_to_le32(NTMP_NULL_ENTRY_ID);

	err = ntmp_fdbt_add_entry(&priv->ntmp, &entry->entry_id, keye, cfge);
	if (err) {
		kfree(entry);

		return err;
	}

	netc_add_fdb_entry(priv, entry);

	return 0;
}

static int netc_port_set_fdb_entry(struct netc_port *np,
				   const unsigned char *addr, u16 vid)
{
	struct netc_switch *priv = np->switch_priv;
	struct netc_fdb_entry *entry;
	struct fdbt_cfge_data *cfge;
	int port = np->dp->index;
	__le32 old_port_bitmap;
	int err = 0;

	mutex_lock(&priv->fdbt_lock);

	entry = netc_lookup_fdb_entry(priv, addr, vid);
	if (!entry) {
		err = netc_port_add_fdb_entry(np, addr, vid);
		if (err)
			dev_err(priv->dev,
				"Failed to add FDB entry on port %d\n",
				port);

		goto unlock_fdbt;
	}

	cfge = &entry->cfge;
	/* If the entry already exists on the port, return 0 directly */
	if (unlikely(cfge->port_bitmap & cpu_to_le32(BIT(port))))
		goto unlock_fdbt;

	/* If the entry already exists, but not on this port, we need to
	 * update the port bitmap. In general, it should only be valid
	 * for multicast or broadcast address.
	 */
	old_port_bitmap = cfge->port_bitmap;
	if (is_multicast_ether_addr(addr))
		cfge->port_bitmap |= cpu_to_le32(BIT(port));
	else
		cfge->port_bitmap = cpu_to_le32(BIT(port));

	err = ntmp_fdbt_update_entry(&priv->ntmp, entry->entry_id, cfge);
	if (err) {
		cfge->port_bitmap = old_port_bitmap;
		dev_err(priv->dev, "Failed to set FDB entry on port %d\n",
			port);
	}

unlock_fdbt:
	mutex_unlock(&priv->fdbt_lock);

	return err;
}

static int netc_port_del_fdb_entry(struct netc_port *np,
				   const unsigned char *addr, u16 vid)
{
	struct netc_switch *priv = np->switch_priv;
	struct ntmp_user *ntmp = &priv->ntmp;
	struct netc_fdb_entry *entry;
	struct fdbt_cfge_data *cfge;
	int port = np->dp->index;
	int err = 0;

	mutex_lock(&priv->fdbt_lock);

	entry = netc_lookup_fdb_entry(priv, addr, vid);
	if (unlikely(!entry))
		/* The hardware-learned dynamic FDB entries cannot be deleted
		 * through .port_fdb_del() interface.
		 * For NTF_MASTER path: Since hardware-learned dynamic FDB
		 * entries are never synchronized back to the bridge software
		 * database. br_fdb_delete() -> br_fdb_find() cannot find the
		 * FDB entry, so .port_fdb_del() will not be called.
		 * For NTF_SELF path: dsa_user_netdev_ops does not implement
		 * ndo_fdb_del(), so rtnl_fdb_del() falls back to
		 * ndo_dflt_fdb_del(), which only supports NUD_PERMANENT static
		 * entries and rejects all others with -EINVAL.
		 */
		goto unlock_fdbt;

	cfge = &entry->cfge;
	if (unlikely(!(cfge->port_bitmap & cpu_to_le32(BIT(port)))))
		goto unlock_fdbt;

	if (cfge->port_bitmap != cpu_to_le32(BIT(port))) {
		/* If the entry also exists on other ports, we need to
		 * update the entry in the FDB table.
		 */
		cfge->port_bitmap &= cpu_to_le32(~BIT(port));
		err = ntmp_fdbt_update_entry(ntmp, entry->entry_id, cfge);
		if (err) {
			cfge->port_bitmap |= cpu_to_le32(BIT(port));
			goto unlock_fdbt;
		}
	} else {
		/* If the entry only exists on this port, just delete
		 * it from the FDB table.
		 */
		err = ntmp_fdbt_delete_entry(ntmp, entry->entry_id);
		if (err)
			goto unlock_fdbt;

		netc_del_fdb_entry(entry);
	}

unlock_fdbt:
	mutex_unlock(&priv->fdbt_lock);

	return err;
}

static int netc_add_standalone_fdb_bcast_entry(struct netc_switch *priv)
{
	const u8 bcast[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct dsa_port *dp, *cpu_dp = NULL;

	dsa_switch_for_each_cpu_port(dp, priv->ds) {
		/* The switch has only one CPU port, so only need to find
		 * the first CPU port to break out of the loop.
		 */
		cpu_dp = dp;
		break;
	}

	if (!cpu_dp)
		return -ENODEV;

	/* If the user port acts as a standalone port, then its PVID is 0,
	 * MLO is set to "disable MAC learning" and MFO is set to "discard
	 * frames if no matching entry found in FDB table". Therefore, we
	 * need to add a broadcast FDB entry on the CPU port so that the
	 * broadcast frames received on the user port can be forwarded to
	 * the CPU port.
	 */
	return netc_port_set_fdb_entry(NETC_PORT(priv->ds, cpu_dp->index),
				       bcast, NETC_STANDALONE_PVID);
}

static void netc_port_set_pbpmcr(struct netc_port *np, u64 mapping)
{
	u32 pbpmcr0 = lower_32_bits(mapping);
	u32 pbpmcr1 = upper_32_bits(mapping);

	netc_port_wr(np, NETC_PBPMCR0, pbpmcr0);
	netc_port_wr(np, NETC_PBPMCR1, pbpmcr1);
}

static void netc_ipv_to_buffer_pool_mapping(struct netc_switch *priv)
{
	int bp_per_port = priv->num_bp / priv->info->num_ports;
	int q = NETC_IPV_NUM / bp_per_port;
	int r = NETC_IPV_NUM % bp_per_port;
	int num = q + r;

	/* IPV-to-buffer-pool mapping per port:
	 * Each port is allocated 'bp_per_port' buffer pools and supports 8
	 * IPVs, where a higher IPV indicates a higher frame priority. Each
	 * IPV can be mapped to only one buffer pool, from hardware design
	 * perspective, bp_per_port will not be greater than 8. So 'q' will
	 * not be 0.
	 *
	 * The mapping rule is as follows:
	 * - The first 'num' IPVs share the port's first buffer pool (index
	 * 'base_id').
	 * - After that, every 'q' IPVs share one buffer pool, with pool
	 * indices increasing sequentially.
	 */
	for (int i = 0; i < priv->info->num_ports; i++) {
		u32 base_id = i * bp_per_port;
		u32 bp_id = base_id;
		u64 mapping = 0;

		for (int ipv = 0; ipv < NETC_IPV_NUM; ipv++) {
			/* Update the buffer pool index */
			if (ipv >= num)
				bp_id = base_id + ((ipv - num) / q) + 1;

			mapping |= (u64)bp_id << (ipv * 8);
		}

		netc_port_set_pbpmcr(priv->ports[i], mapping);
	}
}

static int netc_switch_bpt_default_config(struct netc_switch *priv)
{
	if (priv->num_bp < priv->info->num_ports)
		return -EINVAL;

	priv->bpt_list = devm_kcalloc(priv->dev, priv->num_bp,
				      sizeof(struct bpt_cfge_data),
				      GFP_KERNEL);
	if (!priv->bpt_list)
		return -ENOMEM;

	/* Initialize the maximum threshold of each buffer pool entry */
	for (int i = 0; i < priv->num_bp; i++) {
		struct bpt_cfge_data *cfge = &priv->bpt_list[i];
		int err;

		cfge->max_thresh = cpu_to_le16(NETC_BP_THRESH);
		err = ntmp_bpt_update_entry(&priv->ntmp, i, cfge);
		if (err)
			return err;
	}

	netc_ipv_to_buffer_pool_mapping(priv);

	return 0;
}

static int netc_setup(struct dsa_switch *ds)
{
	struct netc_switch *priv = ds->priv;
	struct dsa_port *dp;
	int err;

	err = netc_init_switch_id(priv);
	if (err)
		return err;

	netc_get_switch_capabilities(priv);

	err = netc_init_all_ports(priv);
	if (err)
		return err;

	err = netc_init_ntmp_user(priv);
	if (err)
		return err;

	INIT_HLIST_HEAD(&priv->fdb_list);
	mutex_init(&priv->fdbt_lock);
	priv->fdbt_ageing_delay = NETC_FDBT_AGEING_DELAY;
	atomic_set(&priv->br_cnt, 0);
	INIT_DELAYED_WORK(&priv->fdbt_ageing_work,
			  netc_clean_fdbt_ageing_entries);
	INIT_HLIST_HEAD(&priv->vlan_list);
	mutex_init(&priv->vft_lock);

	netc_switch_fixed_config(priv);

	/* default setting for ports */
	dsa_switch_for_each_available_port(dp, ds)
		netc_port_default_config(priv->ports[dp->index]);

	err = netc_switch_bpt_default_config(priv);
	if (err)
		goto free_lock_and_ntmp_user;

	err = netc_add_standalone_vlan_entry(priv);
	if (err)
		goto free_lock_and_ntmp_user;

	err = netc_add_standalone_fdb_bcast_entry(priv);
	if (err)
		goto free_lock_and_ntmp_user;

	return 0;

free_lock_and_ntmp_user:
	/* No need to clear the hardware state, netc_setup() is only called
	 * when the driver is bound, and FLR will be performed to reset the
	 * hardware state.
	 */
	mutex_destroy(&priv->fdbt_lock);
	mutex_destroy(&priv->vft_lock);
	netc_free_ntmp_user(priv);

	return err;
}

static void netc_destroy_all_lists(struct netc_switch *priv)
{
	netc_destroy_fdb_list(priv);
	mutex_destroy(&priv->fdbt_lock);
	netc_destroy_vlan_list(priv);
	mutex_destroy(&priv->vft_lock);
}

static void netc_free_host_flood_rules(struct netc_switch *priv)
{
	struct dsa_port *dp;

	dsa_switch_for_each_user_port(dp, priv->ds) {
		struct netc_port *np = priv->ports[dp->index];

		/* No need to clear the hardware IPFT entry. Because PCIe
		 * FLR will be performed when the switch is re-registered,
		 * it will reset hardware state. So only need to free the
		 * memory to avoid memory leak.
		 */
		kfree(np->host_flood);
		np->host_flood = NULL;
	}
}

static void netc_teardown(struct dsa_switch *ds)
{
	struct netc_switch *priv = ds->priv;

	disable_delayed_work_sync(&priv->fdbt_ageing_work);
	netc_destroy_all_lists(priv);
	netc_free_host_flood_rules(priv);
	netc_free_ntmp_user(priv);
}

static bool netc_port_is_emdio_consumer(struct device_node *node)
{
	struct device_node *mdio_node;

	/* If the port node has phy-handle property and it does
	 * not contain a mdio child node, then the port is the
	 * EMDIO consumer.
	 */
	mdio_node = of_get_child_by_name(node, "mdio");
	if (!mdio_node)
		return true;

	of_node_put(mdio_node);

	return false;
}

/* Currently, phylink_of_phy_connect() is called by dsa_user_create(),
 * so if the switch uses the external MDIO controller (like the EMDIO
 * function) to manage the external PHYs. The MDIO bus may not be
 * created when phylink_of_phy_connect() is called, so it will return
 * an error and cause the switch driver to fail to probe.
 * This workaround can be removed when DSA phylink_of_phy_connect()
 * calls are moved from probe() to ndo_open().
 */
static int netc_switch_check_emdio_is_ready(struct device *dev)
{
	struct device_node *ports, *phy_node;
	struct phy_device *phydev;
	int err = 0;

	ports = of_get_child_by_name(dev->of_node, "ethernet-ports");
	if (!ports) {
		dev_err(dev, "Cannot find the ethernet-ports node\n");
		return -EINVAL;
	}

	for_each_available_child_of_node_scoped(ports, child) {
		/* If the node does not have phy-handle property, then the
		 * port does not connect to a PHY, so the port is not the
		 * EMDIO consumer.
		 */
		phy_node = of_parse_phandle(child, "phy-handle", 0);
		if (!phy_node)
			continue;

		/* Note that from the hardware perspective, the switch ports
		 * do not support sharing the MDIO bus defined under one port.
		 * Each port can only access its own external PHY through its
		 * port MDIO bus.
		 */
		if (!netc_port_is_emdio_consumer(child)) {
			of_node_put(phy_node);
			continue;
		}

		phydev = of_phy_find_device(phy_node);
		of_node_put(phy_node);
		if (!phydev) {
			err = -EPROBE_DEFER;
			goto out;
		}

		put_device(&phydev->mdio.dev);
	}

out:
	of_node_put(ports);

	return err;
}

static int netc_switch_pci_init(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct netc_switch_regs *regs;
	struct netc_switch *priv;
	void __iomem *base;
	int err;

	pcie_flr(pdev);
	err = pcim_enable_device(pdev);
	if (err)
		return dev_err_probe(dev, err, "Failed to enable device\n");

	err = pcim_request_all_regions(pdev, KBUILD_MODNAME);
	if (err)
		return dev_err_probe(dev, err, "Failed to request regions\n");

	/* The command BD rings and NTMP tables need DMA. No need to check
	 * the return value, because it never returns fail when the mask is
	 * DMA_BIT_MASK(64), see dma-api-howto.rst.
	 */
	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));

	if (pci_resource_len(pdev, NETC_REGS_BAR) < NETC_REGS_SIZE) {
		return dev_err_probe(dev, -EINVAL,
				     "Invalid register space size\n");
	}

	base = pcim_iomap(pdev, NETC_REGS_BAR, 0);
	if (!base)
		return dev_err_probe(dev, -ENXIO, "pcim_iomap() failed\n");

	pci_set_master(pdev);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pdev = pdev;
	priv->dev = dev;

	regs = &priv->regs;
	regs->base = base;
	regs->port = regs->base + NETC_REGS_PORT_BASE;
	regs->global = regs->base + NETC_REGS_GLOBAL_BASE;
	pci_set_drvdata(pdev, priv);

	return 0;
}

static void netc_switch_get_ip_revision(struct netc_switch *priv)
{
	struct netc_switch_regs *regs = &priv->regs;
	u32 val = netc_glb_rd(regs, NETC_IPBRR0);

	priv->revision = FIELD_GET(IPBRR0_IP_REV, val);
}

static void netc_init_ett_cfge(struct ett_cfge_data *cfge,
			       bool untagged, u32 ect_eid)
{
	u32 vuda_sqta = FMTEID_VUDA_SQTA;
	u16 efm_cfg = 0;

	if (ect_eid != NTMP_NULL_ENTRY_ID) {
		/* Increase egress frame counter */
		efm_cfg |= FIELD_PREP(ETT_ECA, ETT_ECA_INC);
		cfge->ec_eid = cpu_to_le32(ect_eid);
	}

	/* If egress rule is VLAN untagged */
	if (untagged) {
		/* delete outer VLAN tag */
		vuda_sqta |= FIELD_PREP(FMTEID_VUDA, FMTEID_VUDA_DEL_OTAG);
		/* length change: twos-complement notation */
		efm_cfg |= FIELD_PREP(ETT_EFM_LEN_CHANGE,
				      ETT_FRM_LEN_DEL_VLAN);
	}

	cfge->efm_eid = cpu_to_le32(vuda_sqta);
	cfge->efm_cfg = cpu_to_le16(efm_cfg);
}

static int netc_add_ett_entry(struct netc_switch *priv, bool untagged,
			      u32 ett_eid, u32 ect_eid)
{
	struct ntmp_user *ntmp = &priv->ntmp;
	struct ett_cfge_data cfge = {};

	netc_init_ett_cfge(&cfge, untagged, ect_eid);

	return ntmp_ett_add_entry(ntmp, ett_eid, &cfge);
}

static int netc_update_ett_entry(struct netc_switch *priv, bool untagged,
				 u32 ett_eid, u32 ect_eid)
{
	struct ntmp_user *ntmp = &priv->ntmp;
	struct ett_cfge_data cfge = {};

	netc_init_ett_cfge(&cfge, untagged, ect_eid);

	return ntmp_ett_update_entry(ntmp, ett_eid, &cfge);
}

static int netc_add_ett_group_entries(struct netc_switch *priv,
				      u32 untagged_port_bitmap,
				      u32 ett_base_eid,
				      u32 ect_base_eid)
{
	struct netc_port **ports = priv->ports;
	u32 ett_eid, ect_eid;
	bool untagged;
	int i, err;

	for (i = 0; i < priv->info->num_ports; i++) {
		if (!ports[i]->dp)
			continue;

		untagged = !!(untagged_port_bitmap & BIT(i));
		ett_eid = ett_base_eid + ports[i]->ett_offset;
		ect_eid = NTMP_NULL_ENTRY_ID;
		if (ect_base_eid != NTMP_NULL_ENTRY_ID)
			ect_eid = ect_base_eid + ports[i]->ett_offset;

		err = netc_add_ett_entry(priv, untagged, ett_eid, ect_eid);
		if (err)
			goto clear_ett_entries;
	}

	return 0;

clear_ett_entries:
	while (--i >= 0) {
		if (!ports[i]->dp)
			continue;

		ett_eid = ett_base_eid + ports[i]->ett_offset;
		ntmp_ett_delete_entry(&priv->ntmp, ett_eid);
	}

	return err;
}

static int netc_add_vlan_egress_rule(struct netc_switch *priv,
				     struct netc_vlan_entry *entry)
{
	u32 num_ports = netc_num_available_ports(priv);
	struct ntmp_user *ntmp = &priv->ntmp;
	u32 ect_eid = NTMP_NULL_ENTRY_ID;
	u32 ett_eid, ett_gid, ect_gid;
	int err;

	/* Step 1: Find available egress counter table entries and update
	 * these entries.
	 */
	ect_gid = ntmp_lookup_free_eid(ntmp->ect_gid_bitmap,
				       ntmp->ect_bitmap_size);
	if (ect_gid == NTMP_NULL_ENTRY_ID) {
		dev_info(priv->dev,
			 "No egress counter table entries available\n");
	} else {
		ect_eid = ect_gid * num_ports;
		for (int i = 0; i < num_ports; i++)
			/* There is no need to check the return value, the only
			 * issue is that the entry's counter might be inaccurate,
			 * but it will not affect the functionality, it is only
			 * for future debugging.
			 */
			ntmp_ect_update_entry(ntmp, ect_eid + i);
	}

	/* Step 2: Find available egress treatment table entries and add
	 * these entries.
	 */
	ett_gid = ntmp_lookup_free_eid(ntmp->ett_gid_bitmap,
				       ntmp->ett_bitmap_size);
	if (ett_gid == NTMP_NULL_ENTRY_ID) {
		dev_err(priv->dev,
			"No egress treatment table entries available\n");
		err = -ENOSPC;
		goto clear_ect_gid;
	}

	ett_eid = ett_gid * num_ports;
	err = netc_add_ett_group_entries(priv, entry->untagged_port_bitmap,
					 ett_eid, ect_eid);
	if (err)
		goto clear_ett_gid;

	entry->cfge.et_eid = cpu_to_le32(ett_eid);
	entry->ect_gid = ect_gid;

	return 0;

clear_ett_gid:
	ntmp_clear_eid_bitmap(ntmp->ett_gid_bitmap, ett_gid);

clear_ect_gid:
	if (ect_gid != NTMP_NULL_ENTRY_ID)
		ntmp_clear_eid_bitmap(ntmp->ect_gid_bitmap, ect_gid);

	return err;
}

static void netc_delete_vlan_egress_rule(struct netc_switch *priv,
					 struct netc_vlan_entry *entry)
{
	u32 num_ports = netc_num_available_ports(priv);
	struct ntmp_user *ntmp = &priv->ntmp;
	u32 ett_eid, ett_gid;

	ett_eid = le32_to_cpu(entry->cfge.et_eid);
	if (ett_eid == NTMP_NULL_ENTRY_ID)
		return;

	ett_gid = ett_eid / num_ports;
	ntmp_clear_eid_bitmap(ntmp->ett_gid_bitmap, ett_gid);
	for (int i = 0; i < num_ports; i++)
		ntmp_ett_delete_entry(ntmp, ett_eid + i);

	if (entry->ect_gid == NTMP_NULL_ENTRY_ID)
		return;

	ntmp_clear_eid_bitmap(ntmp->ect_gid_bitmap, entry->ect_gid);
}

static int netc_port_update_vlan_egress_rule(struct netc_port *np,
					     struct netc_vlan_entry *entry)
{
	bool untagged = !!(entry->untagged_port_bitmap & BIT(np->dp->index));
	u32 num_ports = netc_num_available_ports(np->switch_priv);
	u32 ett_eid = le32_to_cpu(entry->cfge.et_eid);
	struct netc_switch *priv = np->switch_priv;
	u32 ect_eid = NTMP_NULL_ENTRY_ID;
	int err;

	if (ett_eid == NTMP_NULL_ENTRY_ID)
		return 0;

	if (entry->ect_gid != NTMP_NULL_ENTRY_ID)
		/* Each ETT entry maps to an ECT entry if ect_gid is not NULL
		 * entry ID. The offset of the ECT entry corresponding to the
		 * port in the group is equal to ett_offset.
		 */
		ect_eid = entry->ect_gid * num_ports + np->ett_offset;

	ett_eid += np->ett_offset;
	err = netc_update_ett_entry(priv, untagged, ett_eid, ect_eid);
	if (err) {
		dev_err(priv->dev,
			"Failed to update VLAN %u egress rule on port %d\n",
			entry->vid, np->dp->index);
		return err;
	}

	if (ect_eid != NTMP_NULL_ENTRY_ID)
		ntmp_ect_update_entry(&priv->ntmp, ect_eid);

	return 0;
}

static int netc_port_add_vlan_entry(struct netc_port *np, u16 vid,
				    bool untagged)
{
	struct netc_switch *priv = np->switch_priv;
	struct netc_vlan_entry *entry;
	struct vft_cfge_data *cfge;
	u32 index = np->dp->index;
	u32 bitmap_stg;
	int err;
	u16 cfg;

	entry = kzalloc_obj(*entry);
	if (!entry)
		return -ENOMEM;

	entry->vid = vid;
	entry->ect_gid = NTMP_NULL_ENTRY_ID;

	bitmap_stg = BIT(index) | VFT_STG_ID(0);
	/* If the VID is a VLAN-unaware PVID, the CPU port needs to be
	 * a member of this VLAN.
	 */
	if (dsa_port_is_user(np->dp) &&
	    vid >= NETC_VLAN_UNAWARE_PVID(priv->ds->max_num_bridges)) {
		struct dsa_port *cpu_dp = np->dp->cpu_dp;

		bitmap_stg |= BIT(cpu_dp->index);
	}

	cfg = FIELD_PREP(VFT_MLO, MLO_HW) |
	      FIELD_PREP(VFT_MFO, MFO_NO_MATCH_FLOOD);

	cfge = &entry->cfge;
	cfge->et_eid = cpu_to_le32(NTMP_NULL_ENTRY_ID);
	cfge->bitmap_stg = cpu_to_le32(bitmap_stg);
	cfge->fid = cpu_to_le16(vid);
	cfge->cfg = cpu_to_le16(cfg);
	cfge->eta_port_bitmap = cpu_to_le32(priv->port_bitmap);

	if (untagged)
		entry->untagged_port_bitmap = BIT(index);

	err = netc_add_vlan_egress_rule(priv, entry);
	if (err)
		goto free_vlan_entry;

	err = ntmp_vft_add_entry(&priv->ntmp, vid, cfge);
	if (err) {
		dev_err(priv->dev,
			"Failed to add VLAN %u entry on port %d\n",
			vid, index);
		goto delete_vlan_egress_rule;
	}

	netc_add_vlan_entry(priv, entry);

	return 0;

delete_vlan_egress_rule:
	netc_delete_vlan_egress_rule(priv, entry);
free_vlan_entry:
	kfree(entry);

	return err;
}

static bool netc_port_vlan_egress_rule_changed(struct netc_switch *priv,
					       struct netc_vlan_entry *entry,
					       int port, bool untagged)
{
	bool old_untagged = !!(entry->untagged_port_bitmap & BIT(port));

	/* VLAN-unaware VIDs have no egress rules, so return 'false' */
	if (entry->vid >= NETC_VLAN_UNAWARE_PVID(priv->ds->max_num_bridges))
		return false;

	return old_untagged != untagged;
}

static int netc_port_set_vlan_entry(struct netc_port *np, u16 vid,
				    bool untagged)
{
	struct netc_switch *priv = np->switch_priv;
	struct netc_vlan_entry *entry;
	struct vft_cfge_data *cfge;
	int port = np->dp->index;
	bool changed;
	int err = 0;

	mutex_lock(&priv->vft_lock);

	entry = netc_lookup_vlan_entry(priv, vid);
	if (!entry) {
		err = netc_port_add_vlan_entry(np, vid, untagged);
		goto unlock_vft;
	}

	/* Check whether the egress VLAN rule is changed */
	changed = netc_port_vlan_egress_rule_changed(priv, entry, port,
						     untagged);
	if (changed) {
		entry->untagged_port_bitmap ^= BIT(port);
		err = netc_port_update_vlan_egress_rule(np, entry);
		if (err) {
			entry->untagged_port_bitmap ^= BIT(port);
			goto unlock_vft;
		}
	}

	cfge = &entry->cfge;
	if (cfge->bitmap_stg & cpu_to_le32(BIT(port)))
		goto unlock_vft;

	cfge->bitmap_stg |= cpu_to_le32(BIT(port));
	err = ntmp_vft_update_entry(&priv->ntmp, vid, cfge);
	if (err) {
		dev_err(priv->dev,
			"Failed to update VLAN %u entry on port %d\n",
			vid, port);

		goto restore_bitmap_stg;
	}

	mutex_unlock(&priv->vft_lock);

	return 0;

restore_bitmap_stg:
	cfge->bitmap_stg &= cpu_to_le32(~BIT(port));
	if (changed) {
		entry->untagged_port_bitmap ^= BIT(port);
		/* Recover the corresponding ETT entry. It doesn't matter
		 * if it fails because the bit corresponding to the port
		 * in the port bitmap of the VFT entry is not set. so the
		 * frame will not match that ETT entry.
		 */
		if (netc_port_update_vlan_egress_rule(np, entry))
			entry->untagged_port_bitmap ^= BIT(port);
	}
unlock_vft:
	mutex_unlock(&priv->vft_lock);

	return err;
}

static int netc_port_del_vlan_entry(struct netc_port *np, u16 vid)
{
	struct netc_switch *priv = np->switch_priv;
	struct netc_vlan_entry *entry;
	struct vft_cfge_data *cfge;
	int port = np->dp->index;
	u32 vlan_port_bitmap;
	int err = 0;

	mutex_lock(&priv->vft_lock);

	entry = netc_lookup_vlan_entry(priv, vid);
	if (!entry)
		goto unlock_vft;

	cfge = &entry->cfge;
	vlan_port_bitmap = FIELD_GET(VFT_PORT_MEMBERSHIP,
				     le32_to_cpu(cfge->bitmap_stg));
	/* If the VID is a VLAN-unaware PVID, we need to clear the CPU
	 * port bit of vlan_port_bitmap, so that the VLAN entry can be
	 * deleted if no user ports use this VLAN.
	 */
	if (dsa_port_is_user(np->dp) &&
	    vid >= NETC_VLAN_UNAWARE_PVID(priv->ds->max_num_bridges)) {
		struct dsa_port *cpu_dp = np->dp->cpu_dp;

		vlan_port_bitmap &= ~BIT(cpu_dp->index);
	}

	/* If the VLAN only belongs to the current port */
	if (vlan_port_bitmap == BIT(port)) {
		err = ntmp_vft_delete_entry(&priv->ntmp, vid);
		if (err)
			goto unlock_vft;

		netc_delete_vlan_egress_rule(priv, entry);
		netc_del_vlan_entry(entry);

		goto unlock_vft;
	}

	if (!(vlan_port_bitmap & BIT(port)))
		goto unlock_vft;

	cfge->bitmap_stg &= cpu_to_le32(~BIT(port));
	err = ntmp_vft_update_entry(&priv->ntmp, vid, cfge);
	if (err) {
		cfge->bitmap_stg |= cpu_to_le32(BIT(port));
		goto unlock_vft;
	}

unlock_vft:
	mutex_unlock(&priv->vft_lock);

	return err;
}

static int netc_port_enable(struct dsa_switch *ds, int port,
			    struct phy_device *phy)
{
	struct netc_port *np = NETC_PORT(ds, port);
	int err;

	if (np->enable)
		return 0;

	err = clk_prepare_enable(np->ref_clk);
	if (err) {
		dev_err(ds->dev,
			"Failed to enable enet_ref_clk of port %d\n", port);
		return err;
	}

	np->enable = true;

	return 0;
}

static void netc_port_disable(struct dsa_switch *ds, int port)
{
	struct netc_port *np = NETC_PORT(ds, port);

	/* When .port_disable() is called, .port_enable() may not have been
	 * called. In this case, both the prepare_count and enable_count of
	 * clock are 0. Calling clk_disable_unprepare() at this time will
	 * cause warnings.
	 */
	if (!np->enable)
		return;

	clk_disable_unprepare(np->ref_clk);
	np->enable = false;
}

static void netc_port_stp_state_set(struct dsa_switch *ds,
				    int port, u8 state)
{
	struct netc_port *np = NETC_PORT(ds, port);
	u32 val;

	switch (state) {
	case BR_STATE_DISABLED:
	case BR_STATE_LISTENING:
	case BR_STATE_BLOCKING:
		val = NETC_STG_STATE_DISABLED;
		break;
	case BR_STATE_LEARNING:
		val = NETC_STG_STATE_LEARNING;
		break;
	case BR_STATE_FORWARDING:
		val = NETC_STG_STATE_FORWARDING;
		break;
	default:
		return;
	}

	netc_port_wr(np, NETC_BPSTGSR, val);
}

static int netc_port_change_mtu(struct dsa_switch *ds,
				int port, int mtu)
{
	u32 max_frame_size = mtu + VLAN_ETH_HLEN + ETH_FCS_LEN;

	netc_port_set_max_frame_size(NETC_PORT(ds, port), max_frame_size);

	return 0;
}

static int netc_port_max_mtu(struct dsa_switch *ds, int port)
{
	return NETC_MAX_FRAME_LEN - VLAN_ETH_HLEN - ETH_FCS_LEN;
}

static struct net_device *netc_classify_db(struct dsa_db db)
{
	switch (db.type) {
	case DSA_DB_PORT:
		return NULL;
	case DSA_DB_BRIDGE:
		return db.bridge.dev;
	default:
		return ERR_PTR(-EOPNOTSUPP);
	}
}

static u16 netc_vlan_unaware_pvid(struct dsa_bridge *bridge)
{
	u32 br_num;

	if (!bridge)
		return NETC_STANDALONE_PVID;

	br_num = bridge->num;

	/* The br_num is supposed to be 1 ~ ds->max_num_bridges, see
	 * dsa_bridge_num_get(). Since max_num_bridges is non-zero,
	 * so dsa_port_bridge_create() will return an error if
	 * dsa_bridge_num_get() returns 0.
	 */
	if (WARN_ON(!br_num))
		return NETC_STANDALONE_PVID;

	return NETC_VLAN_UNAWARE_PVID(br_num);
}

static int netc_port_fdb_add(struct dsa_switch *ds, int port,
			     const unsigned char *addr, u16 vid,
			     struct dsa_db db)
{
	struct net_device *br_ndev = netc_classify_db(db);
	struct netc_port *np = NETC_PORT(ds, port);

	if (IS_ERR(br_ndev))
		return PTR_ERR(br_ndev);

	if (!vid)
		vid = netc_vlan_unaware_pvid(br_ndev ? &db.bridge : NULL);

	return netc_port_set_fdb_entry(np, addr, vid);
}

static int netc_port_fdb_del(struct dsa_switch *ds, int port,
			     const unsigned char *addr, u16 vid,
			     struct dsa_db db)
{
	struct net_device *br_ndev = netc_classify_db(db);
	struct netc_port *np = NETC_PORT(ds, port);

	if (IS_ERR(br_ndev))
		return PTR_ERR(br_ndev);

	if (!vid)
		vid = netc_vlan_unaware_pvid(br_ndev ? &db.bridge : NULL);

	return netc_port_del_fdb_entry(np, addr, vid);
}

static int netc_port_fdb_dump(struct dsa_switch *ds, int port,
			      dsa_fdb_dump_cb_t *cb, void *data)
{
	struct netc_switch *priv = ds->priv;
	u32 resume_eid = NTMP_NULL_ENTRY_ID;
	struct fdbt_entry_data *entry;
	struct fdbt_keye_data *keye;
	struct fdbt_cfge_data *cfge;
	u32 cfg, cnt = 0;
	bool is_static;
	int err;
	u16 vid;

	entry = kmalloc_obj(*entry);
	if (!entry)
		return -ENOMEM;

	keye = &entry->keye;
	cfge = &entry->cfge;
	mutex_lock(&priv->fdbt_lock);

	do {
		memset(entry, 0, sizeof(*entry));
		err = ntmp_fdbt_search_port_entry(&priv->ntmp, port,
						  &resume_eid, entry);
		if (err || entry->entry_id == NTMP_NULL_ENTRY_ID)
			break;

		cfg = le32_to_cpu(cfge->cfg);
		is_static = (cfg & FDBT_DYNAMIC) ? false : true;
		vid = le16_to_cpu(keye->fid);
		if (vid >= NETC_VLAN_UNAWARE_PVID(ds->max_num_bridges))
			vid = 0;

		err = cb(keye->mac_addr, vid, is_static, data);
		if (err)
			break;

		/* To prevent hardware malfunctions from causing an
		 * infinite loop.
		 */
		if (++cnt >= priv->htmcapr_num_words)
			break;
	} while (resume_eid != NTMP_NULL_ENTRY_ID);

	mutex_unlock(&priv->fdbt_lock);
	kfree(entry);

	return err;
}

static int netc_port_mdb_add(struct dsa_switch *ds, int port,
			     const struct switchdev_obj_port_mdb *mdb,
			     struct dsa_db db)
{
	return netc_port_fdb_add(ds, port, mdb->addr, mdb->vid, db);
}

static int netc_port_mdb_del(struct dsa_switch *ds, int port,
			     const struct switchdev_obj_port_mdb *mdb,
			     struct dsa_db db)
{
	return netc_port_fdb_del(ds, port, mdb->addr, mdb->vid, db);
}

static int netc_port_add_host_flood_rule(struct netc_port *np,
					 bool uc, bool mc)
{
	const u8 dmac_mask[ETH_ALEN] = {0x1, 0, 0, 0, 0, 0};
	struct netc_switch *priv = np->switch_priv;
	struct ipft_entry_data *host_flood;
	struct ipft_keye_data *keye;
	struct ipft_cfge_data *cfge;
	u16 src_port;
	u32 cfg;
	int err;

	if (!uc && !mc) {
		/* Disable ingress port filter table lookup */
		netc_port_wr(np, NETC_PIPFCR, 0);
		np->uc = false;
		np->mc = false;

		return 0;
	}

	host_flood = kzalloc_obj(*host_flood);
	if (!host_flood)
		return -ENOMEM;

	keye = &host_flood->keye;
	cfge = &host_flood->cfge;

	src_port = FIELD_PREP(IPFT_SRC_PORT, np->dp->index);
	src_port |= IPFT_SRC_PORT_MASK;
	keye->src_port = cpu_to_le16(src_port);

	/* If either only unicast or only multicast need to be flooded
	 * to the host, we always set the mask that tests the first MAC
	 * DA octet. The value should be 0 for the first bit (if unicast
	 * has to be flooded) or 1 (if multicast). If both unicast and
	 * multicast have to be flooded, we leave the key mask empty, so
	 * it matches everything.
	 */
	if (uc && !mc)
		ether_addr_copy(keye->dmac_mask, dmac_mask);

	if (!uc && mc) {
		ether_addr_copy(keye->dmac, dmac_mask);
		ether_addr_copy(keye->dmac_mask, dmac_mask);
	}

	cfg = FIELD_PREP(IPFT_FLTFA, IPFT_FLTFA_REDIRECT);
	cfg |= FIELD_PREP(IPFT_HR, NETC_HR_HOST_FLOOD);
	cfge->cfg = cpu_to_le32(cfg);

	err = ntmp_ipft_add_entry(&priv->ntmp, host_flood);
	if (err) {
		kfree(host_flood);
		return err;
	}

	np->uc = uc;
	np->mc = mc;
	np->host_flood = host_flood;
	/* Enable ingress port filter table lookup */
	netc_port_wr(np, NETC_PIPFCR, PIPFCR_EN);

	return 0;
}

static void netc_port_remove_host_flood(struct netc_port *np,
					struct ipft_entry_data *host_flood)
{
	struct netc_switch *priv = np->switch_priv;
	bool disable_host_flood = false;

	if (!host_flood)
		return;

	if (np->host_flood == host_flood)
		disable_host_flood = true;

	ntmp_ipft_delete_entry(&priv->ntmp, host_flood->entry_id);
	kfree(host_flood);

	if (disable_host_flood) {
		np->host_flood = NULL;
		np->uc = false;
		np->mc = false;
		netc_port_wr(np, NETC_PIPFCR, 0);
	}
}

static void netc_port_set_host_flood(struct dsa_switch *ds, int port,
				     bool uc, bool mc)
{
	struct netc_port *np = NETC_PORT(ds, port);
	struct ipft_entry_data *old_host_flood;

	/* Do not add host flood rule to ingress port filter table when
	 * the port has joined a bridge. Otherwise, the ingress frames
	 * will bypass FDB table lookup and MAC learning, so the frames
	 * will be redirected directly to the CPU port.
	 */
	if (dsa_port_bridge_dev_get(np->dp)) {
		netc_port_remove_host_flood(np, np->host_flood);

		return;
	}

	if (np->uc == uc && np->mc == mc)
		return;

	/* IPFT does not support in-place updates to the KEYE element,
	 * we need to add a new entry and then delete the old one. So
	 * save the old entry first.
	 */
	old_host_flood = np->host_flood;
	np->host_flood = NULL;

	if (netc_port_add_host_flood_rule(np, uc, mc)) {
		np->host_flood = old_host_flood;
		dev_err(ds->dev, "Failed to add host flood rule on port %d\n",
			port);
		return;
	}

	/* Remove the old host flood entry */
	netc_port_remove_host_flood(np, old_host_flood);
}

static int netc_single_vlan_aware_bridge(struct dsa_switch *ds,
					 struct netlink_ext_ack *extack)
{
	struct net_device *br_ndev = NULL;
	struct dsa_port *dp;

	dsa_switch_for_each_available_port(dp, ds) {
		struct net_device *port_br = dsa_port_bridge_dev_get(dp);

		if (!port_br || !br_vlan_enabled(port_br))
			continue;

		if (!br_ndev) {
			br_ndev = port_br;
			continue;
		}

		if (br_ndev == port_br)
			continue;

		NL_SET_ERR_MSG_MOD(extack,
				   "Only one VLAN-aware bridge is supported");

		return -EBUSY;
	}

	return 0;
}

static int netc_port_vlan_filtering(struct dsa_switch *ds,
				    int port, bool vlan_aware,
				    struct netlink_ext_ack *extack)
{
	struct netc_port *np = NETC_PORT(ds, port);
	u16 pvid;
	int err;

	/* Before calling port_vlan_filtering(), br_vlan_filter_toggle() has
	 * already updated the BROPT_VLAN_ENABLED bit of br->options. So the
	 * VLAN filtering status of the switch ports can be checked by the
	 * br_vlan_enabled() function.
	 */
	err = netc_single_vlan_aware_bridge(ds, extack);
	if (err)
		return err;

	pvid = netc_vlan_unaware_pvid(np->dp->bridge);
	if (pvid == NETC_STANDALONE_PVID) {
		vlan_aware = false;
		goto bpdvr_config;
	}

	if (vlan_aware) {
		/* The FDB entries associated with unaware_pvid do not need
		 * to be deleted, so that when switching from VLAN-aware to
		 * VLAN-unaware mode, these FDB entries do not need to be
		 * re-added.
		 */
		err = netc_port_del_vlan_entry(np, pvid);
		if (err)
			return err;

		pvid = np->pvid;
	} else {
		err = netc_port_set_vlan_entry(np, pvid, false);
		if (err)
			return err;
	}

bpdvr_config:
	netc_port_set_vlan_aware(np, vlan_aware);
	netc_port_set_pvid(np, pvid);

	return 0;
}

static int netc_port_vlan_add(struct dsa_switch *ds, int port,
			      const struct switchdev_obj_port_vlan *vlan,
			      struct netlink_ext_ack *extack)
{
	struct netc_port *np = NETC_PORT(ds, port);
	struct dsa_port *dp = np->dp;
	bool untagged;
	int err;

	/* The 8021q layer may attempt to change NETC_STANDALONE_PVID
	 * (VID 0), so we need to ignore it.
	 */
	if (vlan->vid == NETC_STANDALONE_PVID)
		return 0;

	if (vlan->vid >= NETC_VLAN_UNAWARE_PVID(ds->max_num_bridges)) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "VID %d~4095 reserved for VLAN-unaware bridge",
				       NETC_VLAN_UNAWARE_PVID(ds->max_num_bridges));
		return -EINVAL;
	}

	untagged = !!(vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED);
	err = netc_port_set_vlan_entry(np, vlan->vid, untagged);
	if (err)
		return err;

	if (vlan->flags & BRIDGE_VLAN_INFO_PVID) {
		np->pvid = vlan->vid;
		if (dsa_port_is_vlan_filtering(dp))
			netc_port_set_pvid(np, vlan->vid);

		return 0;
	}

	if (np->pvid != vlan->vid)
		return 0;

	/* Delete PVID */
	np->pvid = NETC_STANDALONE_PVID;
	if (dsa_port_is_vlan_filtering(dp))
		netc_port_set_pvid(np, NETC_STANDALONE_PVID);

	return 0;
}

static int netc_port_vlan_del(struct dsa_switch *ds, int port,
			      const struct switchdev_obj_port_vlan *vlan)
{
	struct netc_port *np = NETC_PORT(ds, port);
	int err;

	if (vlan->vid == NETC_STANDALONE_PVID)
		return 0;

	if (vlan->vid >= NETC_VLAN_UNAWARE_PVID(ds->max_num_bridges))
		return -EINVAL;

	err = netc_port_del_vlan_entry(np, vlan->vid);
	if (err)
		return err;

	if (np->pvid == vlan->vid) {
		np->pvid = NETC_STANDALONE_PVID;

		/* Set the port PVID to NETC_STANDALONE_PVID if the VLAN-aware
		 * bridge port has no PVID. The untagged frames will not be
		 * forwarded to other user ports, as NETC_STANDALONE_PVID VLAN
		 * entry has disabled MAC learning and flooding, and other user
		 * ports do not have FDB entries with NETC_STANDALONE_PVID.
		 */
		if (dsa_port_is_vlan_filtering(np->dp))
			netc_port_set_pvid(np, NETC_STANDALONE_PVID);
	}

	return 0;
}

static int netc_port_bridge_join(struct dsa_switch *ds, int port,
				 struct dsa_bridge bridge,
				 bool *tx_fwd_offload,
				 struct netlink_ext_ack *extack)
{
	struct netc_port *np = NETC_PORT(ds, port);
	struct netc_switch *priv = ds->priv;
	u16 vlan_unaware_pvid;
	int err;

	if (!bridge.num) {
		NL_SET_ERR_MSG_MOD(extack, "Bridge number 0 is unsupported");
		return -EINVAL;
	}

	err = netc_single_vlan_aware_bridge(ds, extack);
	if (err)
		return err;

	netc_port_set_mlo(np, MLO_NOT_OVERRIDE);

	if (br_vlan_enabled(bridge.dev))
		goto out;

	vlan_unaware_pvid = NETC_VLAN_UNAWARE_PVID(bridge.num);
	err = netc_port_set_vlan_entry(np, vlan_unaware_pvid, false);
	if (err)
		goto disable_mlo;

	netc_port_set_pvid(np, vlan_unaware_pvid);

out:
	netc_port_remove_host_flood(np, np->host_flood);

	if (atomic_inc_return(&priv->br_cnt) == 1)
		schedule_delayed_work(&priv->fdbt_ageing_work,
				      READ_ONCE(priv->fdbt_ageing_delay));

	return 0;

disable_mlo:
	netc_port_set_mlo(np, MLO_DISABLE);

	return err;
}

static void netc_port_remove_dynamic_entries(struct netc_port *np)
{
	struct netc_switch *priv = np->switch_priv;

	/* Return if the port is not available */
	if (!np->dp)
		return;

	mutex_lock(&priv->fdbt_lock);
	ntmp_fdbt_delete_port_dynamic_entries(&priv->ntmp, np->dp->index);
	mutex_unlock(&priv->fdbt_lock);
}

static void netc_port_bridge_leave(struct dsa_switch *ds, int port,
				   struct dsa_bridge bridge)
{
	struct netc_port *np = NETC_PORT(ds, port);
	struct net_device *ndev = np->dp->user;
	struct netc_switch *priv = ds->priv;
	u16 vlan_unaware_pvid;
	bool mc, uc;

	netc_port_set_mlo(np, MLO_DISABLE);
	netc_port_set_pvid(np, NETC_STANDALONE_PVID);
	np->pvid = NETC_STANDALONE_PVID;

	if (atomic_dec_and_test(&priv->br_cnt))
		cancel_delayed_work_sync(&priv->fdbt_ageing_work);

	netc_port_remove_dynamic_entries(np);
	uc = ndev->flags & IFF_PROMISC;
	mc = ndev->flags & (IFF_PROMISC | IFF_ALLMULTI);

	if (netc_port_add_host_flood_rule(np, uc, mc))
		dev_warn(ds->dev,
			 "Failed to restore host flood rule on port %d\n",
			 port);

	/* When a port leaves a VLAN-aware bridge, dsa_port_bridge_leave()
	 * follows the sequence below:
	 *
	 * 1. dsa_port_bridge_destroy() is called to set dp->bridge to NULL.
	 * 2. dsa_broadcast() is called, which eventually invokes
	 *    ds->ops->port_bridge_leave()
	 * 3. dsa_port_switchdev_unsync_attrs() is called, which triggers
	 *    dsa_port_reset_vlan_filtering() and ultimately calls
	 *    ds->ops->port_vlan_filtering() to transition the port from
	 *    VLAN-aware mode to VLAN-unaware mode.
	 *
	 * At step 3, since dp->bridge has already been set to NULL in step 1,
	 * netc_port_vlan_filtering() will detect this and skip the creation
	 * of an unaware PVID entry in the VLAN filter table. Therefore, it is
	 * safe to return directly here.
	 */
	if (br_vlan_enabled(bridge.dev))
		return;

	vlan_unaware_pvid = NETC_VLAN_UNAWARE_PVID(bridge.num);
	/* There is no need to check the return value even if it fails.
	 * Because the PVID has been set to NETC_STANDALONE_PVID, the
	 * frames will not match this VLAN entry.
	 */
	netc_port_del_vlan_entry(np, vlan_unaware_pvid);
}

static int netc_set_ageing_time(struct dsa_switch *ds, unsigned int msecs)
{
	struct netc_switch *priv = ds->priv;
	unsigned long delay_jiffies;

	/* The dynamic FDB entry is deleted when its activity counter reaches
	 * NETC_FDBT_AGEING_THRESH (100). Each delayed_work tick increments
	 * the counter by 1 if the entry is inactive.
	 *
	 * Therefore:
	 *   msecs (ms)    = NETC_FDBT_AGEING_THRESH * delay_ms (ms)
	 *   delay_ms      = msecs / NETC_FDBT_AGEING_THRESH
	 *   delay_jiffies = (delay_ms / 1000) * HZ
	 *                 = (msecs * HZ) / (1000 * NETC_FDBT_AGEING_THRESH)
	 *
	 * Use DIV_ROUND_CLOSEST_ULL to perform a single nearest-jiffy
	 * rounding, avoiding the two-step rounding error of the intermediate
	 * delay_ms approach.
	 *   Maximum error = +/-0.5 jiffy * 100 = +/-50000/HZ ms.
	 */
	delay_jiffies = DIV_ROUND_CLOSEST_ULL((u64)msecs * HZ,
					      1000 * NETC_FDBT_AGEING_THRESH);
	WRITE_ONCE(priv->fdbt_ageing_delay, delay_jiffies);

	if (atomic_read(&priv->br_cnt))
		mod_delayed_work(system_percpu_wq, &priv->fdbt_ageing_work,
				 READ_ONCE(priv->fdbt_ageing_delay));

	return 0;
}

static void netc_port_fast_age(struct dsa_switch *ds, int port)
{
	struct netc_port *np = NETC_PORT(ds, port);

	netc_port_remove_dynamic_entries(np);
}

static void netc_phylink_get_caps(struct dsa_switch *ds, int port,
				  struct phylink_config *config)
{
	struct netc_switch *priv = ds->priv;

	priv->info->phylink_get_caps(port, config);
}

static void netc_port_set_mac_mode(struct netc_port *np,
				   unsigned int mode,
				   phy_interface_t phy_mode)
{
	u32 mask = PM_IF_MODE_IFMODE | PM_IF_MODE_REVMII;
	u32 val = 0;

	switch (phy_mode) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val |= IFMODE_RGMII;
		break;
	case PHY_INTERFACE_MODE_RMII:
		val |= IFMODE_RMII;
		break;
	case PHY_INTERFACE_MODE_REVMII:
		val |= PM_IF_MODE_REVMII;
		fallthrough;
	case PHY_INTERFACE_MODE_MII:
		val |= IFMODE_MII;
		break;
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_2500BASEX:
		val |= IFMODE_SGMII;
		break;
	default:
		break;
	}

	netc_mac_port_rmw(np, NETC_PM_IF_MODE(0), mask, val);
}

static void netc_mac_config(struct phylink_config *config, unsigned int mode,
			    const struct phylink_link_state *state)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);

	netc_port_set_mac_mode(NETC_PORT(dp->ds, dp->index), mode,
			       state->interface);
}

static void netc_port_set_speed(struct netc_port *np, int speed)
{
	netc_port_rmw(np, NETC_PCR, PCR_PSPEED, PSPEED_SET_VAL(speed));
}

static void netc_port_set_rgmii_mac(struct netc_port *np,
				    int speed, int duplex)
{
	u32 mask, val;

	mask = PM_IF_MODE_SSP | PM_IF_MODE_HD | PM_IF_MODE_M10;

	switch (speed) {
	default:
	case SPEED_1000:
		val = FIELD_PREP(PM_IF_MODE_SSP, SSP_1G);
		break;
	case SPEED_100:
		val = FIELD_PREP(PM_IF_MODE_SSP, SSP_100M);
		break;
	case SPEED_10:
		val = FIELD_PREP(PM_IF_MODE_SSP, SSP_10M);
		break;
	}

	if (duplex != DUPLEX_FULL)
		val |= PM_IF_MODE_HD;

	netc_mac_port_rmw(np, NETC_PM_IF_MODE(0), mask, val);
}

static void netc_port_set_rmii_mii_mac(struct netc_port *np,
				       int speed, int duplex)
{
	u32 mask, val = 0;

	mask = PM_IF_MODE_SSP | PM_IF_MODE_HD | PM_IF_MODE_M10;

	if (speed == SPEED_10)
		val |= PM_IF_MODE_M10;

	if (duplex != DUPLEX_FULL)
		val |= PM_IF_MODE_HD;

	netc_mac_port_rmw(np, NETC_PM_IF_MODE(0), mask, val);
}

static void netc_port_set_tx_pause(struct netc_port *np, bool tx_pause)
{
	struct netc_switch *priv = np->switch_priv;
	int port = np->dp->index;
	int i, j, num_bp;

	num_bp = priv->num_bp / priv->info->num_ports;
	for (i = 0, j = port * num_bp; i < num_bp; i++, j++) {
		struct bpt_cfge_data *cfge = &priv->bpt_list[j];
		struct bpt_cfge_data old_cfge = *cfge;

		if (tx_pause) {
			cfge->fc_on_thresh = cpu_to_le16(NETC_FC_THRESH_ON);
			cfge->fc_off_thresh = cpu_to_le16(NETC_FC_THRESH_OFF);
			cfge->fccfg_sbpen = FIELD_PREP(BPT_FC_CFG,
						       BPT_FC_CFG_EN_BPFC);
			cfge->fc_ports = cpu_to_le32(BIT(port));
		} else {
			cfge->fc_on_thresh = cpu_to_le16(0);
			cfge->fc_off_thresh = cpu_to_le16(0);
			cfge->fccfg_sbpen = 0;
			cfge->fc_ports = cpu_to_le32(0);
		}

		if (ntmp_bpt_update_entry(&priv->ntmp, j, cfge)) {
			*cfge = old_cfge;
			dev_warn(priv->dev,
				 "Failed to %s TX pause of buffer pool %d (swp%d)\n",
				 tx_pause ? "enable" : "disable", j, port);
		}
	}
}

static void netc_port_set_rx_pause(struct netc_port *np, bool rx_pause)
{
	netc_mac_port_rmw(np, NETC_PM_CMD_CFG(0), PM_CMD_CFG_PAUSE_IGN,
			  rx_pause ? 0 : PM_CMD_CFG_PAUSE_IGN);
}

static void netc_port_mac_rx_enable(struct netc_port *np)
{
	netc_port_rmw(np, NETC_POR, POR_RXDIS, 0);
	netc_mac_port_rmw(np, NETC_PM_CMD_CFG(0), PM_CMD_CFG_RX_EN,
			  PM_CMD_CFG_RX_EN);
}

static void netc_port_wait_rx_empty(struct netc_port *np, int mac)
{
	u32 val;

	/* PM_IEVENT_RX_EMPTY is a read-only bit, it is automatically set by
	 * hardware if RX FIFO is empty and no RX packet receive in process.
	 * And it is automatically cleared if RX FIFO is not empty or RX
	 * packet receive in process.
	 */
	if (read_poll_timeout(netc_port_rd, val, val & PM_IEVENT_RX_EMPTY,
			      100, 10000, false, np, NETC_PM_IEVENT(mac)))
		dev_warn(np->switch_priv->dev,
			 "swp%d MAC%d: RX is not idle\n", np->dp->index, mac);
}

static void netc_port_mac_rx_graceful_stop(struct netc_port *np)
{
	u32 val;

	if (is_netc_pseudo_port(np))
		goto rx_disable;

	if (np->caps.pmac) {
		netc_port_rmw(np, NETC_PM_CMD_CFG(1), PM_CMD_CFG_RX_EN, 0);
		netc_port_wait_rx_empty(np, 1);
	}

	netc_port_rmw(np, NETC_PM_CMD_CFG(0), PM_CMD_CFG_RX_EN, 0);
	netc_port_wait_rx_empty(np, 0);

	if (read_poll_timeout(netc_port_rd, val, !(val & PSR_RX_BUSY),
			      100, 10000, false, np, NETC_PSR))
		dev_warn(np->switch_priv->dev, "swp%d RX is busy\n",
			 np->dp->index);

rx_disable:
	netc_port_rmw(np, NETC_POR, POR_RXDIS, POR_RXDIS);
}

static void netc_port_mac_tx_enable(struct netc_port *np)
{
	netc_mac_port_rmw(np, NETC_PM_CMD_CFG(0), PM_CMD_CFG_TX_EN,
			  PM_CMD_CFG_TX_EN);
	netc_port_rmw(np, NETC_POR, POR_TXDIS, 0);
}

static void netc_port_wait_tx_empty(struct netc_port *np, int mac)
{
	u32 val;

	/* PM_IEVENT_TX_EMPTY is a read-only bit, it is automatically set by
	 * hardware if TX FIFO is empty. And it is automatically cleared if
	 * TX FIFO is not empty.
	 */
	if (read_poll_timeout(netc_port_rd, val, val & PM_IEVENT_TX_EMPTY,
			      100, 10000, false, np, NETC_PM_IEVENT(mac)))
		dev_warn(np->switch_priv->dev,
			 "swp%d MAC%d: TX FIFO is not empty\n",
			 np->dp->index, mac);
}

static void netc_port_mac_tx_graceful_stop(struct netc_port *np)
{
	netc_port_rmw(np, NETC_POR, POR_TXDIS, POR_TXDIS);

	if (is_netc_pseudo_port(np))
		return;

	netc_port_wait_tx_empty(np, 0);
	if (np->caps.pmac)
		netc_port_wait_tx_empty(np, 1);

	netc_mac_port_rmw(np, NETC_PM_CMD_CFG(0), PM_CMD_CFG_TX_EN, 0);
}

static void netc_mac_link_up(struct phylink_config *config,
			     struct phy_device *phy, unsigned int mode,
			     phy_interface_t interface, int speed,
			     int duplex, bool tx_pause, bool rx_pause)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct netc_port *np;

	np = NETC_PORT(dp->ds, dp->index);
	netc_port_set_speed(np, speed);

	if (phy_interface_mode_is_rgmii(interface))
		netc_port_set_rgmii_mac(np, speed, duplex);

	if (interface == PHY_INTERFACE_MODE_RMII ||
	    interface == PHY_INTERFACE_MODE_REVMII ||
	    interface == PHY_INTERFACE_MODE_MII)
		netc_port_set_rmii_mii_mac(np, speed, duplex);

	netc_port_set_tx_pause(np, tx_pause);
	netc_port_set_rx_pause(np, rx_pause);
	netc_port_mac_tx_enable(np);
	netc_port_mac_rx_enable(np);
}

static void netc_mac_link_down(struct phylink_config *config,
			       unsigned int mode,
			       phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct netc_port *np;

	np = NETC_PORT(dp->ds, dp->index);
	netc_port_mac_rx_graceful_stop(np);
	netc_port_mac_tx_graceful_stop(np);
	netc_port_remove_dynamic_entries(np);
}

static const struct phylink_mac_ops netc_phylink_mac_ops = {
	.mac_config		= netc_mac_config,
	.mac_link_up		= netc_mac_link_up,
	.mac_link_down		= netc_mac_link_down,
};

static const struct dsa_switch_ops netc_switch_ops = {
	.get_tag_protocol		= netc_get_tag_protocol,
	.setup				= netc_setup,
	.teardown			= netc_teardown,
	.phylink_get_caps		= netc_phylink_get_caps,
	.port_enable			= netc_port_enable,
	.port_disable			= netc_port_disable,
	.port_stp_state_set		= netc_port_stp_state_set,
	.port_change_mtu		= netc_port_change_mtu,
	.port_max_mtu			= netc_port_max_mtu,
	.port_fdb_add			= netc_port_fdb_add,
	.port_fdb_del			= netc_port_fdb_del,
	.port_fdb_dump			= netc_port_fdb_dump,
	.port_mdb_add			= netc_port_mdb_add,
	.port_mdb_del			= netc_port_mdb_del,
	.port_set_host_flood		= netc_port_set_host_flood,
	.port_vlan_filtering		= netc_port_vlan_filtering,
	.port_vlan_add			= netc_port_vlan_add,
	.port_vlan_del			= netc_port_vlan_del,
	.port_bridge_join		= netc_port_bridge_join,
	.port_bridge_leave		= netc_port_bridge_leave,
	.set_ageing_time		= netc_set_ageing_time,
	.port_fast_age			= netc_port_fast_age,
	.get_pause_stats		= netc_port_get_pause_stats,
	.get_rmon_stats			= netc_port_get_rmon_stats,
	.get_eth_ctrl_stats		= netc_port_get_eth_ctrl_stats,
	.get_eth_mac_stats		= netc_port_get_eth_mac_stats,
	.get_sset_count			= netc_port_get_sset_count,
	.get_strings			= netc_port_get_strings,
	.get_ethtool_stats		= netc_port_get_ethtool_stats,
};

static int netc_switch_probe(struct pci_dev *pdev,
			     const struct pci_device_id *id)
{
	struct device_node *node = dev_of_node(&pdev->dev);
	struct device *dev = &pdev->dev;
	struct netc_switch *priv;
	struct dsa_switch *ds;
	int err;

	if (!node)
		return dev_err_probe(dev, -ENODEV,
				     "No DT bindings, skipping\n");

	err = netc_switch_check_emdio_is_ready(dev);
	if (err)
		return err;

	err = netc_switch_pci_init(pdev);
	if (err)
		return err;

	priv = pci_get_drvdata(pdev);
	netc_switch_get_ip_revision(priv);

	err = netc_switch_platform_probe(priv);
	if (err)
		return err;

	ds = devm_kzalloc(dev, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return -ENOMEM;

	ds->dev = dev;
	ds->num_ports = priv->info->num_ports;
	ds->num_tx_queues = NETC_TC_NUM;
	ds->ops = &netc_switch_ops;
	ds->phylink_mac_ops = &netc_phylink_mac_ops;
	ds->fdb_isolation = true;
	ds->max_num_bridges = priv->info->num_ports - 1;
	ds->ageing_time_min = 1000;
	ds->ageing_time_max = U32_MAX;
	ds->priv = priv;
	priv->ds = ds;

	err = dsa_register_switch(ds);
	if (err)
		return dev_err_probe(dev, err,
				     "Failed to register DSA switch\n");

	return 0;
}

static void netc_switch_remove(struct pci_dev *pdev)
{
	struct netc_switch *priv = pci_get_drvdata(pdev);

	if (!priv)
		return;

	dsa_unregister_switch(priv->ds);
}

static void netc_switch_shutdown(struct pci_dev *pdev)
{
	struct netc_switch *priv = pci_get_drvdata(pdev);

	if (!priv)
		return;

	dsa_switch_shutdown(priv->ds);
	pci_set_drvdata(pdev, NULL);
}

static const struct pci_device_id netc_switch_ids[] = {
	{ PCI_DEVICE(NETC_SWITCH_VENDOR_ID, NETC_SWITCH_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, netc_switch_ids);

static struct pci_driver netc_switch_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= netc_switch_ids,
	.probe		= netc_switch_probe,
	.remove		= netc_switch_remove,
	.shutdown	= netc_switch_shutdown,
};
module_pci_driver(netc_switch_driver);

MODULE_DESCRIPTION("NXP NETC Switch driver");
MODULE_LICENSE("Dual BSD/GPL");
