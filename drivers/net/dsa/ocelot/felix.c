// SPDX-License-Identifier: GPL-2.0
/* Copyright 2019 NXP Semiconductors
 */
#include <uapi/linux/if_bridge.h>
#include <soc/mscc/ocelot.h>
#include <linux/packing.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/of.h>
#include <net/dsa.h>
#include "felix.h"

static enum dsa_tag_protocol felix_get_tag_protocol(struct dsa_switch *ds,
						    int port)
{
	return DSA_TAG_PROTO_OCELOT;
}

static int felix_set_ageing_time(struct dsa_switch *ds,
				 unsigned int ageing_time)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_set_ageing_time(ocelot, ageing_time);

	return 0;
}

static void felix_adjust_link(struct dsa_switch *ds, int port,
			      struct phy_device *phydev)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_adjust_link(ocelot, port, phydev);
}

static int felix_fdb_dump(struct dsa_switch *ds, int port,
			  dsa_fdb_dump_cb_t *cb, void *data)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_fdb_dump(ocelot, port, cb, data);
}

static int felix_fdb_add(struct dsa_switch *ds, int port,
			 const unsigned char *addr, u16 vid)
{
	struct ocelot *ocelot = ds->priv;
	bool vlan_aware;

	vlan_aware = dsa_port_is_vlan_filtering(dsa_to_port(ds, port));

	return ocelot_fdb_add(ocelot, port, addr, vid, vlan_aware);
}

static int felix_fdb_del(struct dsa_switch *ds, int port,
			 const unsigned char *addr, u16 vid)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_fdb_del(ocelot, port, addr, vid);
}

static void felix_bridge_stp_state_set(struct dsa_switch *ds, int port,
				       u8 state)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_bridge_stp_state_set(ocelot, port, state);
}

static int felix_bridge_join(struct dsa_switch *ds, int port,
			     struct net_device *br)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_bridge_join(ocelot, port, br);
}

static void felix_bridge_leave(struct dsa_switch *ds, int port,
			       struct net_device *br)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_bridge_leave(ocelot, port, br);
}

/* This callback needs to be present */
static int felix_vlan_prepare(struct dsa_switch *ds, int port,
			      const struct switchdev_obj_port_vlan *vlan)
{
	return 0;
}

static int felix_vlan_filtering(struct dsa_switch *ds, int port, bool enabled)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_vlan_filtering(ocelot, port, enabled);

	return 0;
}

static void felix_vlan_add(struct dsa_switch *ds, int port,
			   const struct switchdev_obj_port_vlan *vlan)
{
	struct ocelot *ocelot = ds->priv;
	u16 vid;
	int err;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; vid++) {
		err = ocelot_vlan_add(ocelot, port, vid,
				      vlan->flags & BRIDGE_VLAN_INFO_PVID,
				      vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED);
		if (err) {
			dev_err(ds->dev, "Failed to add VLAN %d to port %d: %d\n",
				vid, port, err);
			return;
		}
	}
}

static int felix_vlan_del(struct dsa_switch *ds, int port,
			  const struct switchdev_obj_port_vlan *vlan)
{
	struct ocelot *ocelot = ds->priv;
	u16 vid;
	int err;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; vid++) {
		err = ocelot_vlan_del(ocelot, port, vid);
		if (err) {
			dev_err(ds->dev, "Failed to remove VLAN %d from port %d: %d\n",
				vid, port, err);
			return err;
		}
	}
	return 0;
}

static int felix_port_enable(struct dsa_switch *ds, int port,
			     struct phy_device *phy)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_enable(ocelot, port, phy);

	return 0;
}

static void felix_port_disable(struct dsa_switch *ds, int port)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_disable(ocelot, port);
}

static void felix_get_strings(struct dsa_switch *ds, int port,
			      u32 stringset, u8 *data)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_get_strings(ocelot, port, stringset, data);
}

static void felix_get_ethtool_stats(struct dsa_switch *ds, int port, u64 *data)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_get_ethtool_stats(ocelot, port, data);
}

static int felix_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_get_sset_count(ocelot, port, sset);
}

static int felix_get_ts_info(struct dsa_switch *ds, int port,
			     struct ethtool_ts_info *info)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_get_ts_info(ocelot, port, info);
}

static int felix_init_structs(struct felix *felix, int num_phys_ports)
{
	struct ocelot *ocelot = &felix->ocelot;
	resource_size_t base;
	int port, i, err;

	ocelot->num_phys_ports = num_phys_ports;
	ocelot->ports = devm_kcalloc(ocelot->dev, num_phys_ports,
				     sizeof(struct ocelot_port *), GFP_KERNEL);
	if (!ocelot->ports)
		return -ENOMEM;

	ocelot->map		= felix->info->map;
	ocelot->stats_layout	= felix->info->stats_layout;
	ocelot->num_stats	= felix->info->num_stats;
	ocelot->shared_queue_sz	= felix->info->shared_queue_sz;
	ocelot->ops		= felix->info->ops;

	base = pci_resource_start(felix->pdev, felix->info->pci_bar);

	for (i = 0; i < TARGET_MAX; i++) {
		struct regmap *target;
		struct resource *res;

		if (!felix->info->target_io_res[i].name)
			continue;

		res = &felix->info->target_io_res[i];
		res->flags = IORESOURCE_MEM;
		res->start += base;
		res->end += base;

		target = ocelot_regmap_init(ocelot, res);
		if (IS_ERR(target)) {
			dev_err(ocelot->dev,
				"Failed to map device memory space\n");
			return PTR_ERR(target);
		}

		ocelot->targets[i] = target;
	}

	err = ocelot_regfields_init(ocelot, felix->info->regfields);
	if (err) {
		dev_err(ocelot->dev, "failed to init reg fields map\n");
		return err;
	}

	for (port = 0; port < num_phys_ports; port++) {
		struct ocelot_port *ocelot_port;
		void __iomem *port_regs;
		struct resource *res;

		ocelot_port = devm_kzalloc(ocelot->dev,
					   sizeof(struct ocelot_port),
					   GFP_KERNEL);
		if (!ocelot_port) {
			dev_err(ocelot->dev,
				"failed to allocate port memory\n");
			return -ENOMEM;
		}

		res = &felix->info->port_io_res[port];
		res->flags = IORESOURCE_MEM;
		res->start += base;
		res->end += base;

		port_regs = devm_ioremap_resource(ocelot->dev, res);
		if (IS_ERR(port_regs)) {
			dev_err(ocelot->dev,
				"failed to map registers for port %d\n", port);
			return PTR_ERR(port_regs);
		}

		ocelot_port->ocelot = ocelot;
		ocelot_port->regs = port_regs;
		ocelot->ports[port] = ocelot_port;
	}

	return 0;
}

/* Hardware initialization done here so that we can allocate structures with
 * devm without fear of dsa_register_switch returning -EPROBE_DEFER and causing
 * us to allocate structures twice (leak memory) and map PCI memory twice
 * (which will not work).
 */
static int felix_setup(struct dsa_switch *ds)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	int port, err;

	err = felix_init_structs(felix, ds->num_ports);
	if (err)
		return err;

	ocelot_init(ocelot);

	for (port = 0; port < ds->num_ports; port++) {
		ocelot_init_port(ocelot, port);

		if (dsa_is_cpu_port(ds, port))
			ocelot_set_cpu_port(ocelot, port,
					    OCELOT_TAG_PREFIX_NONE,
					    OCELOT_TAG_PREFIX_LONG);
	}

	return 0;
}

static void felix_teardown(struct dsa_switch *ds)
{
	struct ocelot *ocelot = ds->priv;

	/* stop workqueue thread */
	ocelot_deinit(ocelot);
}

static int felix_hwtstamp_get(struct dsa_switch *ds, int port,
			      struct ifreq *ifr)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_hwstamp_get(ocelot, port, ifr);
}

static int felix_hwtstamp_set(struct dsa_switch *ds, int port,
			      struct ifreq *ifr)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_hwstamp_set(ocelot, port, ifr);
}

static bool felix_rxtstamp(struct dsa_switch *ds, int port,
			   struct sk_buff *skb, unsigned int type)
{
	struct skb_shared_hwtstamps *shhwtstamps;
	struct ocelot *ocelot = ds->priv;
	u8 *extraction = skb->data - ETH_HLEN - OCELOT_TAG_LEN;
	u32 tstamp_lo, tstamp_hi;
	struct timespec64 ts;
	u64 tstamp, val;

	ocelot_ptp_gettime64(&ocelot->ptp_info, &ts);
	tstamp = ktime_set(ts.tv_sec, ts.tv_nsec);

	packing(extraction, &val,  116, 85, OCELOT_TAG_LEN, UNPACK, 0);
	tstamp_lo = (u32)val;

	tstamp_hi = tstamp >> 32;
	if ((tstamp & 0xffffffff) < tstamp_lo)
		tstamp_hi--;

	tstamp = ((u64)tstamp_hi << 32) | tstamp_lo;

	shhwtstamps = skb_hwtstamps(skb);
	memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));
	shhwtstamps->hwtstamp = tstamp;
	return false;
}

static bool felix_txtstamp(struct dsa_switch *ds, int port,
			   struct sk_buff *clone, unsigned int type)
{
	struct ocelot *ocelot = ds->priv;
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	if (!ocelot_port_add_txtstamp_skb(ocelot_port, clone))
		return true;

	return false;
}

static const struct dsa_switch_ops felix_switch_ops = {
	.get_tag_protocol	= felix_get_tag_protocol,
	.setup			= felix_setup,
	.teardown		= felix_teardown,
	.set_ageing_time	= felix_set_ageing_time,
	.get_strings		= felix_get_strings,
	.get_ethtool_stats	= felix_get_ethtool_stats,
	.get_sset_count		= felix_get_sset_count,
	.get_ts_info		= felix_get_ts_info,
	.adjust_link		= felix_adjust_link,
	.port_enable		= felix_port_enable,
	.port_disable		= felix_port_disable,
	.port_fdb_dump		= felix_fdb_dump,
	.port_fdb_add		= felix_fdb_add,
	.port_fdb_del		= felix_fdb_del,
	.port_bridge_join	= felix_bridge_join,
	.port_bridge_leave	= felix_bridge_leave,
	.port_stp_state_set	= felix_bridge_stp_state_set,
	.port_vlan_prepare	= felix_vlan_prepare,
	.port_vlan_filtering	= felix_vlan_filtering,
	.port_vlan_add		= felix_vlan_add,
	.port_vlan_del		= felix_vlan_del,
	.port_hwtstamp_get	= felix_hwtstamp_get,
	.port_hwtstamp_set	= felix_hwtstamp_set,
	.port_rxtstamp		= felix_rxtstamp,
	.port_txtstamp		= felix_txtstamp,
};

static struct felix_info *felix_instance_tbl[] = {
	[FELIX_INSTANCE_VSC9959] = &felix_info_vsc9959,
};

static irqreturn_t felix_irq_handler(int irq, void *data)
{
	struct ocelot *ocelot = (struct ocelot *)data;

	/* The INTB interrupt is used for both PTP TX timestamp interrupt
	 * and preemption status change interrupt on each port.
	 *
	 * - Get txtstamp if have
	 * - TODO: handle preemption. Without handling it, driver may get
	 *   interrupt storm.
	 */

	ocelot_get_txtstamp(ocelot);

	return IRQ_HANDLED;
}

static int felix_pci_probe(struct pci_dev *pdev,
			   const struct pci_device_id *id)
{
	enum felix_instance instance = id->driver_data;
	struct dsa_switch *ds;
	struct ocelot *ocelot;
	struct felix *felix;
	int err;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "device enable failed\n");
		goto err_pci_enable;
	}

	/* set up for high or low dma */
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev,
				"DMA configuration failed: 0x%x\n", err);
			goto err_dma;
		}
	}

	felix = kzalloc(sizeof(struct felix), GFP_KERNEL);
	if (!felix) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "Failed to allocate driver memory\n");
		goto err_alloc_felix;
	}

	pci_set_drvdata(pdev, felix);
	ocelot = &felix->ocelot;
	ocelot->dev = &pdev->dev;
	felix->pdev = pdev;
	felix->info = felix_instance_tbl[instance];

	pci_set_master(pdev);

	err = devm_request_threaded_irq(&pdev->dev, pdev->irq, NULL,
					&felix_irq_handler, IRQF_ONESHOT,
					"felix-intb", ocelot);
	if (err) {
		dev_err(&pdev->dev, "Failed to request irq\n");
		goto err_alloc_irq;
	}

	ocelot->ptp = 1;

	ds = kzalloc(sizeof(struct dsa_switch), GFP_KERNEL);
	if (!ds) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "Failed to allocate DSA switch\n");
		goto err_alloc_ds;
	}

	ds->dev = &pdev->dev;
	ds->num_ports = felix->info->num_ports;
	ds->ops = &felix_switch_ops;
	ds->priv = ocelot;
	felix->ds = ds;

	err = dsa_register_switch(ds);
	if (err) {
		dev_err(&pdev->dev, "Failed to register DSA switch: %d\n", err);
		goto err_register_ds;
	}

	return 0;

err_register_ds:
	kfree(ds);
err_alloc_ds:
err_alloc_irq:
err_alloc_felix:
	kfree(felix);
err_dma:
	pci_disable_device(pdev);
err_pci_enable:
	return err;
}

static void felix_pci_remove(struct pci_dev *pdev)
{
	struct felix *felix;

	felix = pci_get_drvdata(pdev);

	dsa_unregister_switch(felix->ds);

	kfree(felix->ds);
	kfree(felix);

	pci_disable_device(pdev);
}

static struct pci_device_id felix_ids[] = {
	{
		/* NXP LS1028A */
		PCI_DEVICE(PCI_VENDOR_ID_FREESCALE, 0xEEF0),
		.driver_data = FELIX_INSTANCE_VSC9959,
	},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, felix_ids);

static struct pci_driver felix_pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= felix_ids,
	.probe		= felix_pci_probe,
	.remove		= felix_pci_remove,
};

module_pci_driver(felix_pci_driver);

MODULE_DESCRIPTION("Felix Switch driver");
MODULE_LICENSE("GPL v2");
