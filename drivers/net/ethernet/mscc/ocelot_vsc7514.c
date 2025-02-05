// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */
#include <linux/dsa/ocelot.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_net.h>
#include <linux/netdevice.h>
#include <linux/phylink.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/skbuff.h>
#include <net/switchdev.h>

#include <soc/mscc/ocelot.h>
#include <soc/mscc/ocelot_vcap.h>
#include <soc/mscc/vsc7514_regs.h>
#include "ocelot_fdma.h"
#include "ocelot.h"

#define VSC7514_VCAP_POLICER_BASE			128
#define VSC7514_VCAP_POLICER_MAX			191

static int ocelot_chip_init(struct ocelot *ocelot, const struct ocelot_ops *ops)
{
	int ret;

	ocelot->map = vsc7514_regmap;
	ocelot->num_mact_rows = 1024;
	ocelot->ops = ops;

	ret = ocelot_regfields_init(ocelot, vsc7514_regfields);
	if (ret)
		return ret;

	ocelot_pll5_init(ocelot);

	eth_random_addr(ocelot->base_mac);
	ocelot->base_mac[5] &= 0xf0;

	return 0;
}

static irqreturn_t ocelot_xtr_irq_handler(int irq, void *arg)
{
	struct ocelot *ocelot = arg;
	int grp = 0, err;

	ocelot_lock_xtr_grp(ocelot, grp);

	while (ocelot_read(ocelot, QS_XTR_DATA_PRESENT) & BIT(grp)) {
		struct sk_buff *skb;

		err = ocelot_xtr_poll_frame(ocelot, grp, &skb);
		if (err)
			goto out;

		skb->dev->stats.rx_bytes += skb->len;
		skb->dev->stats.rx_packets++;

		if (!skb_defer_rx_timestamp(skb))
			netif_rx(skb);
	}

out:
	if (err < 0)
		ocelot_drain_cpu_queue(ocelot, 0);

	ocelot_unlock_xtr_grp(ocelot, grp);

	return IRQ_HANDLED;
}

static irqreturn_t ocelot_ptp_rdy_irq_handler(int irq, void *arg)
{
	struct ocelot *ocelot = arg;

	ocelot_get_txtstamp(ocelot);

	return IRQ_HANDLED;
}

static const struct of_device_id mscc_ocelot_match[] = {
	{ .compatible = "mscc,vsc7514-switch" },
	{ }
};
MODULE_DEVICE_TABLE(of, mscc_ocelot_match);

static const struct ocelot_ops ocelot_ops = {
	.reset			= ocelot_reset,
	.wm_enc			= ocelot_wm_enc,
	.wm_dec			= ocelot_wm_dec,
	.wm_stat		= ocelot_wm_stat,
	.port_to_netdev		= ocelot_port_to_netdev,
	.netdev_to_port		= ocelot_netdev_to_port,
};

static struct ptp_clock_info ocelot_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.name		= "ocelot ptp",
	.max_adj	= 0x7fffffff,
	.n_alarm	= 0,
	.n_ext_ts	= 0,
	.n_per_out	= OCELOT_PTP_PINS_NUM,
	.n_pins		= OCELOT_PTP_PINS_NUM,
	.pps		= 0,
	.gettime64	= ocelot_ptp_gettime64,
	.settime64	= ocelot_ptp_settime64,
	.adjtime	= ocelot_ptp_adjtime,
	.adjfine	= ocelot_ptp_adjfine,
	.verify		= ocelot_ptp_verify,
	.enable		= ocelot_ptp_enable,
};

static void mscc_ocelot_teardown_devlink_ports(struct ocelot *ocelot)
{
	int port;

	for (port = 0; port < ocelot->num_phys_ports; port++)
		ocelot_port_devlink_teardown(ocelot, port);
}

static void mscc_ocelot_release_ports(struct ocelot *ocelot)
{
	int port;

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		struct ocelot_port *ocelot_port;

		ocelot_port = ocelot->ports[port];
		if (!ocelot_port)
			continue;

		ocelot_deinit_port(ocelot, port);
		ocelot_release_port(ocelot_port);
	}
}

static int mscc_ocelot_init_ports(struct platform_device *pdev,
				  struct device_node *ports)
{
	struct ocelot *ocelot = platform_get_drvdata(pdev);
	u32 devlink_ports_registered = 0;
	struct device_node *portnp;
	int port, err;
	u32 reg;

	ocelot->ports = devm_kcalloc(ocelot->dev, ocelot->num_phys_ports,
				     sizeof(struct ocelot_port *), GFP_KERNEL);
	if (!ocelot->ports)
		return -ENOMEM;

	ocelot->devlink_ports = devm_kcalloc(ocelot->dev,
					     ocelot->num_phys_ports,
					     sizeof(*ocelot->devlink_ports),
					     GFP_KERNEL);
	if (!ocelot->devlink_ports)
		return -ENOMEM;

	for_each_available_child_of_node(ports, portnp) {
		struct regmap *target;
		struct resource *res;
		char res_name[8];

		if (of_property_read_u32(portnp, "reg", &reg))
			continue;

		port = reg;
		if (port < 0 || port >= ocelot->num_phys_ports) {
			dev_err(ocelot->dev,
				"invalid port number: %d >= %d\n", port,
				ocelot->num_phys_ports);
			continue;
		}

		snprintf(res_name, sizeof(res_name), "port%d", port);

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   res_name);
		target = ocelot_regmap_init(ocelot, res);
		if (IS_ERR(target)) {
			err = PTR_ERR(target);
			of_node_put(portnp);
			goto out_teardown;
		}

		err = ocelot_port_devlink_init(ocelot, port,
					       DEVLINK_PORT_FLAVOUR_PHYSICAL);
		if (err) {
			of_node_put(portnp);
			goto out_teardown;
		}

		err = ocelot_probe_port(ocelot, port, target, portnp);
		if (err) {
			ocelot_port_devlink_teardown(ocelot, port);
			continue;
		}

		devlink_ports_registered |= BIT(port);
	}

	/* Initialize unused devlink ports at the end */
	for (port = 0; port < ocelot->num_phys_ports; port++) {
		if (devlink_ports_registered & BIT(port))
			continue;

		err = ocelot_port_devlink_init(ocelot, port,
					       DEVLINK_PORT_FLAVOUR_UNUSED);
		if (err)
			goto out_teardown;

		devlink_ports_registered |= BIT(port);
	}

	return 0;

out_teardown:
	/* Unregister the network interfaces */
	mscc_ocelot_release_ports(ocelot);
	/* Tear down devlink ports for the registered network interfaces */
	for (port = 0; port < ocelot->num_phys_ports; port++) {
		if (devlink_ports_registered & BIT(port))
			ocelot_port_devlink_teardown(ocelot, port);
	}
	return err;
}

static int mscc_ocelot_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int err, irq_xtr, irq_ptp_rdy;
	struct device_node *ports;
	struct devlink *devlink;
	struct ocelot *ocelot;
	struct regmap *hsio;
	unsigned int i;

	struct {
		enum ocelot_target id;
		char *name;
		u8 optional:1;
	} io_target[] = {
		{ SYS, "sys" },
		{ REW, "rew" },
		{ QSYS, "qsys" },
		{ ANA, "ana" },
		{ QS, "qs" },
		{ S0, "s0" },
		{ S1, "s1" },
		{ S2, "s2" },
		{ PTP, "ptp", 1 },
		{ FDMA, "fdma", 1 },
	};

	if (!np && !pdev->dev.platform_data)
		return -ENODEV;

	devlink =
		devlink_alloc(&ocelot_devlink_ops, sizeof(*ocelot), &pdev->dev);
	if (!devlink)
		return -ENOMEM;

	ocelot = devlink_priv(devlink);
	ocelot->devlink = priv_to_devlink(ocelot);
	platform_set_drvdata(pdev, ocelot);
	ocelot->dev = &pdev->dev;

	for (i = 0; i < ARRAY_SIZE(io_target); i++) {
		struct regmap *target;
		struct resource *res;

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   io_target[i].name);

		target = ocelot_regmap_init(ocelot, res);
		if (IS_ERR(target)) {
			if (io_target[i].optional) {
				ocelot->targets[io_target[i].id] = NULL;
				continue;
			}
			err = PTR_ERR(target);
			goto out_free_devlink;
		}

		ocelot->targets[io_target[i].id] = target;
	}

	if (ocelot->targets[FDMA])
		ocelot_fdma_init(pdev, ocelot);

	hsio = syscon_regmap_lookup_by_compatible("mscc,ocelot-hsio");
	if (IS_ERR(hsio)) {
		dev_err(&pdev->dev, "missing hsio syscon\n");
		err = PTR_ERR(hsio);
		goto out_free_devlink;
	}

	ocelot->targets[HSIO] = hsio;

	err = ocelot_chip_init(ocelot, &ocelot_ops);
	if (err)
		goto out_free_devlink;

	irq_xtr = platform_get_irq_byname(pdev, "xtr");
	if (irq_xtr < 0) {
		err = irq_xtr;
		goto out_free_devlink;
	}

	err = devm_request_threaded_irq(&pdev->dev, irq_xtr, NULL,
					ocelot_xtr_irq_handler, IRQF_ONESHOT,
					"frame extraction", ocelot);
	if (err)
		goto out_free_devlink;

	irq_ptp_rdy = platform_get_irq_byname(pdev, "ptp_rdy");
	if (irq_ptp_rdy > 0 && ocelot->targets[PTP]) {
		err = devm_request_threaded_irq(&pdev->dev, irq_ptp_rdy, NULL,
						ocelot_ptp_rdy_irq_handler,
						IRQF_ONESHOT, "ptp ready",
						ocelot);
		if (err)
			goto out_free_devlink;

		/* Both the PTP interrupt and the PTP bank are available */
		ocelot->ptp = 1;
	}

	ports = of_get_child_by_name(np, "ethernet-ports");
	if (!ports) {
		dev_err(ocelot->dev, "no ethernet-ports child node found\n");
		err = -ENODEV;
		goto out_free_devlink;
	}

	ocelot->num_phys_ports = of_get_child_count(ports);
	ocelot->num_flooding_pgids = 1;

	ocelot->vcap = vsc7514_vcap_props;

	ocelot->vcap_pol.base = VSC7514_VCAP_POLICER_BASE;
	ocelot->vcap_pol.max = VSC7514_VCAP_POLICER_MAX;

	ocelot->npi = -1;

	err = ocelot_init(ocelot);
	if (err)
		goto out_put_ports;

	err = mscc_ocelot_init_ports(pdev, ports);
	if (err)
		goto out_ocelot_devlink_unregister;

	if (ocelot->fdma)
		ocelot_fdma_start(ocelot);

	err = ocelot_devlink_sb_register(ocelot);
	if (err)
		goto out_ocelot_release_ports;

	if (ocelot->ptp) {
		err = ocelot_init_timestamp(ocelot, &ocelot_ptp_clock_info);
		if (err) {
			dev_err(ocelot->dev,
				"Timestamp initialization failed\n");
			ocelot->ptp = 0;
		}
	}

	register_netdevice_notifier(&ocelot_netdevice_nb);
	register_switchdev_notifier(&ocelot_switchdev_nb);
	register_switchdev_blocking_notifier(&ocelot_switchdev_blocking_nb);

	of_node_put(ports);
	devlink_register(devlink);

	dev_info(&pdev->dev, "Ocelot switch probed\n");

	return 0;

out_ocelot_release_ports:
	mscc_ocelot_release_ports(ocelot);
	mscc_ocelot_teardown_devlink_ports(ocelot);
out_ocelot_devlink_unregister:
	ocelot_deinit(ocelot);
out_put_ports:
	of_node_put(ports);
out_free_devlink:
	devlink_free(devlink);
	return err;
}

static void mscc_ocelot_remove(struct platform_device *pdev)
{
	struct ocelot *ocelot = platform_get_drvdata(pdev);

	if (ocelot->fdma)
		ocelot_fdma_deinit(ocelot);
	devlink_unregister(ocelot->devlink);
	ocelot_deinit_timestamp(ocelot);
	ocelot_devlink_sb_unregister(ocelot);
	mscc_ocelot_release_ports(ocelot);
	mscc_ocelot_teardown_devlink_ports(ocelot);
	ocelot_deinit(ocelot);
	unregister_switchdev_blocking_notifier(&ocelot_switchdev_blocking_nb);
	unregister_switchdev_notifier(&ocelot_switchdev_nb);
	unregister_netdevice_notifier(&ocelot_netdevice_nb);
	devlink_free(ocelot->devlink);
}

static struct platform_driver mscc_ocelot_driver = {
	.probe = mscc_ocelot_probe,
	.remove = mscc_ocelot_remove,
	.driver = {
		.name = "ocelot-switch",
		.of_match_table = mscc_ocelot_match,
	},
};

module_platform_driver(mscc_ocelot_driver);

MODULE_DESCRIPTION("Microsemi Ocelot switch driver");
MODULE_AUTHOR("Alexandre Belloni <alexandre.belloni@bootlin.com>");
MODULE_LICENSE("Dual MIT/GPL");
