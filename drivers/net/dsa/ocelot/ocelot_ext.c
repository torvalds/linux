// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright 2021-2022 Innovative Advantage Inc.
 */

#include <linux/mfd/ocelot.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <soc/mscc/ocelot.h>
#include <soc/mscc/vsc7514_regs.h>
#include "felix.h"

#define VSC7514_NUM_PORTS		11

#define OCELOT_PORT_MODE_SERDES		(OCELOT_PORT_MODE_SGMII | \
					 OCELOT_PORT_MODE_QSGMII)

static const u32 vsc7512_port_modes[VSC7514_NUM_PORTS] = {
	OCELOT_PORT_MODE_INTERNAL,
	OCELOT_PORT_MODE_INTERNAL,
	OCELOT_PORT_MODE_INTERNAL,
	OCELOT_PORT_MODE_INTERNAL,
	OCELOT_PORT_MODE_SERDES,
	OCELOT_PORT_MODE_SERDES,
	OCELOT_PORT_MODE_SERDES,
	OCELOT_PORT_MODE_SERDES,
	OCELOT_PORT_MODE_SERDES,
	OCELOT_PORT_MODE_SGMII,
	OCELOT_PORT_MODE_SERDES,
};

static const struct ocelot_ops ocelot_ext_ops = {
	.reset		= ocelot_reset,
	.wm_enc		= ocelot_wm_enc,
	.wm_dec		= ocelot_wm_dec,
	.wm_stat	= ocelot_wm_stat,
	.port_to_netdev	= felix_port_to_netdev,
	.netdev_to_port	= felix_netdev_to_port,
};

static const char * const vsc7512_resource_names[TARGET_MAX] = {
	[SYS] = "sys",
	[REW] = "rew",
	[S0] = "s0",
	[S1] = "s1",
	[S2] = "s2",
	[QS] = "qs",
	[QSYS] = "qsys",
	[ANA] = "ana",
};

static const struct felix_info vsc7512_info = {
	.resource_names			= vsc7512_resource_names,
	.regfields			= vsc7514_regfields,
	.map				= vsc7514_regmap,
	.ops				= &ocelot_ext_ops,
	.vcap				= vsc7514_vcap_props,
	.num_mact_rows			= 1024,
	.num_ports			= VSC7514_NUM_PORTS,
	.num_tx_queues			= OCELOT_NUM_TC,
	.port_modes			= vsc7512_port_modes,
	.phylink_mac_config		= ocelot_phylink_mac_config,
	.configure_serdes		= ocelot_port_configure_serdes,
};

static int ocelot_ext_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dsa_switch *ds;
	struct ocelot *ocelot;
	struct felix *felix;
	int err;

	felix = kzalloc(sizeof(*felix), GFP_KERNEL);
	if (!felix)
		return -ENOMEM;

	dev_set_drvdata(dev, felix);

	ocelot = &felix->ocelot;
	ocelot->dev = dev;

	ocelot->num_flooding_pgids = 1;

	felix->info = &vsc7512_info;

	ds = kzalloc(sizeof(*ds), GFP_KERNEL);
	if (!ds) {
		err = -ENOMEM;
		dev_err_probe(dev, err, "Failed to allocate DSA switch\n");
		goto err_free_felix;
	}

	ds->dev = dev;
	ds->num_ports = felix->info->num_ports;
	ds->num_tx_queues = felix->info->num_tx_queues;

	ds->ops = &felix_switch_ops;
	ds->priv = ocelot;
	felix->ds = ds;
	felix->tag_proto = DSA_TAG_PROTO_OCELOT;

	err = dsa_register_switch(ds);
	if (err) {
		dev_err_probe(dev, err, "Failed to register DSA switch\n");
		goto err_free_ds;
	}

	return 0;

err_free_ds:
	kfree(ds);
err_free_felix:
	kfree(felix);
	return err;
}

static void ocelot_ext_remove(struct platform_device *pdev)
{
	struct felix *felix = dev_get_drvdata(&pdev->dev);

	if (!felix)
		return;

	dsa_unregister_switch(felix->ds);

	kfree(felix->ds);
	kfree(felix);
}

static void ocelot_ext_shutdown(struct platform_device *pdev)
{
	struct felix *felix = dev_get_drvdata(&pdev->dev);

	if (!felix)
		return;

	dsa_switch_shutdown(felix->ds);

	dev_set_drvdata(&pdev->dev, NULL);
}

static const struct of_device_id ocelot_ext_switch_of_match[] = {
	{ .compatible = "mscc,vsc7512-switch" },
	{ },
};
MODULE_DEVICE_TABLE(of, ocelot_ext_switch_of_match);

static struct platform_driver ocelot_ext_switch_driver = {
	.driver = {
		.name = "ocelot-ext-switch",
		.of_match_table = ocelot_ext_switch_of_match,
	},
	.probe = ocelot_ext_probe,
	.remove_new = ocelot_ext_remove,
	.shutdown = ocelot_ext_shutdown,
};
module_platform_driver(ocelot_ext_switch_driver);

MODULE_DESCRIPTION("External Ocelot Switch driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(MFD_OCELOT);
