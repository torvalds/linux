// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_net.h>
#include <linux/netdevice.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/mfd/syscon.h>
#include <linux/skbuff.h>
#include <net/switchdev.h>

#include "ocelot.h"

#define IFH_EXTRACT_BITFIELD64(x, o, w) (((x) >> (o)) & GENMASK_ULL((w) - 1, 0))

static int ocelot_parse_ifh(u32 *_ifh, struct frame_info *info)
{
	u8 llen, wlen;
	u64 ifh[2];

	ifh[0] = be64_to_cpu(((__force __be64 *)_ifh)[0]);
	ifh[1] = be64_to_cpu(((__force __be64 *)_ifh)[1]);

	wlen = IFH_EXTRACT_BITFIELD64(ifh[0], 7,  8);
	llen = IFH_EXTRACT_BITFIELD64(ifh[0], 15,  6);

	info->len = OCELOT_BUFFER_CELL_SZ * wlen + llen - 80;

	info->timestamp = IFH_EXTRACT_BITFIELD64(ifh[0], 21, 32);

	info->port = IFH_EXTRACT_BITFIELD64(ifh[1], 43, 4);

	info->tag_type = IFH_EXTRACT_BITFIELD64(ifh[1], 16,  1);
	info->vid = IFH_EXTRACT_BITFIELD64(ifh[1], 0,  12);

	return 0;
}

static int ocelot_rx_frame_word(struct ocelot *ocelot, u8 grp, bool ifh,
				u32 *rval)
{
	u32 val;
	u32 bytes_valid;

	val = ocelot_read_rix(ocelot, QS_XTR_RD, grp);
	if (val == XTR_NOT_READY) {
		if (ifh)
			return -EIO;

		do {
			val = ocelot_read_rix(ocelot, QS_XTR_RD, grp);
		} while (val == XTR_NOT_READY);
	}

	switch (val) {
	case XTR_ABORT:
		return -EIO;
	case XTR_EOF_0:
	case XTR_EOF_1:
	case XTR_EOF_2:
	case XTR_EOF_3:
	case XTR_PRUNED:
		bytes_valid = XTR_VALID_BYTES(val);
		val = ocelot_read_rix(ocelot, QS_XTR_RD, grp);
		if (val == XTR_ESCAPE)
			*rval = ocelot_read_rix(ocelot, QS_XTR_RD, grp);
		else
			*rval = val;

		return bytes_valid;
	case XTR_ESCAPE:
		*rval = ocelot_read_rix(ocelot, QS_XTR_RD, grp);

		return 4;
	default:
		*rval = val;

		return 4;
	}
}

static irqreturn_t ocelot_xtr_irq_handler(int irq, void *arg)
{
	struct ocelot *ocelot = arg;
	int i = 0, grp = 0;
	int err = 0;

	if (!(ocelot_read(ocelot, QS_XTR_DATA_PRESENT) & BIT(grp)))
		return IRQ_NONE;

	do {
		struct skb_shared_hwtstamps *shhwtstamps;
		u64 tod_in_ns, full_ts_in_ns;
		struct frame_info info = {};
		struct net_device *dev;
		u32 ifh[4], val, *buf;
		struct timespec64 ts;
		int sz, len, buf_len;
		struct sk_buff *skb;

		for (i = 0; i < IFH_LEN; i++) {
			err = ocelot_rx_frame_word(ocelot, grp, true, &ifh[i]);
			if (err != 4)
				break;
		}

		if (err != 4)
			break;

		ocelot_parse_ifh(ifh, &info);

		dev = ocelot->ports[info.port]->dev;

		skb = netdev_alloc_skb(dev, info.len);

		if (unlikely(!skb)) {
			netdev_err(dev, "Unable to allocate sk_buff\n");
			err = -ENOMEM;
			break;
		}
		buf_len = info.len - ETH_FCS_LEN;
		buf = (u32 *)skb_put(skb, buf_len);

		len = 0;
		do {
			sz = ocelot_rx_frame_word(ocelot, grp, false, &val);
			*buf++ = val;
			len += sz;
		} while (len < buf_len);

		/* Read the FCS */
		sz = ocelot_rx_frame_word(ocelot, grp, false, &val);
		/* Update the statistics if part of the FCS was read before */
		len -= ETH_FCS_LEN - sz;

		if (unlikely(dev->features & NETIF_F_RXFCS)) {
			buf = (u32 *)skb_put(skb, ETH_FCS_LEN);
			*buf = val;
		}

		if (sz < 0) {
			err = sz;
			break;
		}

		if (ocelot->ptp) {
			ocelot_ptp_gettime64(&ocelot->ptp_info, &ts);

			tod_in_ns = ktime_set(ts.tv_sec, ts.tv_nsec);
			if ((tod_in_ns & 0xffffffff) < info.timestamp)
				full_ts_in_ns = (((tod_in_ns >> 32) - 1) << 32) |
						info.timestamp;
			else
				full_ts_in_ns = (tod_in_ns & GENMASK_ULL(63, 32)) |
						info.timestamp;

			shhwtstamps = skb_hwtstamps(skb);
			memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));
			shhwtstamps->hwtstamp = full_ts_in_ns;
		}

		/* Everything we see on an interface that is in the HW bridge
		 * has already been forwarded.
		 */
		if (ocelot->bridge_mask & BIT(info.port))
			skb->offload_fwd_mark = 1;

		skb->protocol = eth_type_trans(skb, dev);
		netif_rx(skb);
		dev->stats.rx_bytes += len;
		dev->stats.rx_packets++;
	} while (ocelot_read(ocelot, QS_XTR_DATA_PRESENT) & BIT(grp));

	if (err)
		while (ocelot_read(ocelot, QS_XTR_DATA_PRESENT) & BIT(grp))
			ocelot_read_rix(ocelot, QS_XTR_RD, grp);

	return IRQ_HANDLED;
}

static irqreturn_t ocelot_ptp_rdy_irq_handler(int irq, void *arg)
{
	int budget = OCELOT_PTP_QUEUE_SZ;
	struct ocelot *ocelot = arg;

	while (budget--) {
		struct skb_shared_hwtstamps shhwtstamps;
		struct list_head *pos, *tmp;
		struct sk_buff *skb = NULL;
		struct ocelot_skb *entry;
		struct ocelot_port *port;
		struct timespec64 ts;
		u32 val, id, txport;

		val = ocelot_read(ocelot, SYS_PTP_STATUS);

		/* Check if a timestamp can be retrieved */
		if (!(val & SYS_PTP_STATUS_PTP_MESS_VLD))
			break;

		WARN_ON(val & SYS_PTP_STATUS_PTP_OVFL);

		/* Retrieve the ts ID and Tx port */
		id = SYS_PTP_STATUS_PTP_MESS_ID_X(val);
		txport = SYS_PTP_STATUS_PTP_MESS_TXPORT_X(val);

		/* Retrieve its associated skb */
		port = ocelot->ports[txport];

		list_for_each_safe(pos, tmp, &port->skbs) {
			entry = list_entry(pos, struct ocelot_skb, head);
			if (entry->id != id)
				continue;

			skb = entry->skb;

			list_del(pos);
			kfree(entry);
		}

		/* Next ts */
		ocelot_write(ocelot, SYS_PTP_NXT_PTP_NXT, SYS_PTP_NXT);

		if (unlikely(!skb))
			continue;

		/* Get the h/w timestamp */
		ocelot_get_hwtimestamp(ocelot, &ts);

		/* Set the timestamp into the skb */
		memset(&shhwtstamps, 0, sizeof(shhwtstamps));
		shhwtstamps.hwtstamp = ktime_set(ts.tv_sec, ts.tv_nsec);
		skb_tstamp_tx(skb, &shhwtstamps);

		dev_kfree_skb_any(skb);
	}

	return IRQ_HANDLED;
}

static const struct of_device_id mscc_ocelot_match[] = {
	{ .compatible = "mscc,vsc7514-switch" },
	{ }
};
MODULE_DEVICE_TABLE(of, mscc_ocelot_match);

static int mscc_ocelot_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *ports, *portnp;
	int err, irq_xtr, irq_ptp_rdy;
	struct ocelot *ocelot;
	struct regmap *hsio;
	unsigned int i;
	u32 val;

	struct {
		enum ocelot_target id;
		char *name;
		u8 optional:1;
	} res[] = {
		{ SYS, "sys" },
		{ REW, "rew" },
		{ QSYS, "qsys" },
		{ ANA, "ana" },
		{ QS, "qs" },
		{ S2, "s2" },
		{ PTP, "ptp", 1 },
	};

	if (!np && !pdev->dev.platform_data)
		return -ENODEV;

	ocelot = devm_kzalloc(&pdev->dev, sizeof(*ocelot), GFP_KERNEL);
	if (!ocelot)
		return -ENOMEM;

	platform_set_drvdata(pdev, ocelot);
	ocelot->dev = &pdev->dev;

	for (i = 0; i < ARRAY_SIZE(res); i++) {
		struct regmap *target;

		target = ocelot_io_platform_init(ocelot, pdev, res[i].name);
		if (IS_ERR(target)) {
			if (res[i].optional) {
				ocelot->targets[res[i].id] = NULL;
				continue;
			}

			return PTR_ERR(target);
		}

		ocelot->targets[res[i].id] = target;
	}

	hsio = syscon_regmap_lookup_by_compatible("mscc,ocelot-hsio");
	if (IS_ERR(hsio)) {
		dev_err(&pdev->dev, "missing hsio syscon\n");
		return PTR_ERR(hsio);
	}

	ocelot->targets[HSIO] = hsio;

	err = ocelot_chip_init(ocelot);
	if (err)
		return err;

	irq_xtr = platform_get_irq_byname(pdev, "xtr");
	if (irq_xtr < 0)
		return -ENODEV;

	err = devm_request_threaded_irq(&pdev->dev, irq_xtr, NULL,
					ocelot_xtr_irq_handler, IRQF_ONESHOT,
					"frame extraction", ocelot);
	if (err)
		return err;

	irq_ptp_rdy = platform_get_irq_byname(pdev, "ptp_rdy");
	if (irq_ptp_rdy > 0 && ocelot->targets[PTP]) {
		err = devm_request_threaded_irq(&pdev->dev, irq_ptp_rdy, NULL,
						ocelot_ptp_rdy_irq_handler,
						IRQF_ONESHOT, "ptp ready",
						ocelot);
		if (err)
			return err;

		/* Both the PTP interrupt and the PTP bank are available */
		ocelot->ptp = 1;
	}

	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_MEM_INIT], 1);
	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_MEM_ENA], 1);

	do {
		msleep(1);
		regmap_field_read(ocelot->regfields[SYS_RESET_CFG_MEM_INIT],
				  &val);
	} while (val);

	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_MEM_ENA], 1);
	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_CORE_ENA], 1);

	ocelot->num_cpu_ports = 1; /* 1 port on the switch, two groups */

	ports = of_get_child_by_name(np, "ethernet-ports");
	if (!ports) {
		dev_err(&pdev->dev, "no ethernet-ports child node found\n");
		return -ENODEV;
	}

	ocelot->num_phys_ports = of_get_child_count(ports);

	ocelot->ports = devm_kcalloc(&pdev->dev, ocelot->num_phys_ports,
				     sizeof(struct ocelot_port *), GFP_KERNEL);

	INIT_LIST_HEAD(&ocelot->multicast);
	ocelot_init(ocelot);

	for_each_available_child_of_node(ports, portnp) {
		struct device_node *phy_node;
		phy_interface_t phy_mode;
		struct phy_device *phy;
		struct resource *res;
		struct phy *serdes;
		void __iomem *regs;
		char res_name[8];
		u32 port;

		if (of_property_read_u32(portnp, "reg", &port))
			continue;

		snprintf(res_name, sizeof(res_name), "port%d", port);

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   res_name);
		regs = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(regs))
			continue;

		phy_node = of_parse_phandle(portnp, "phy-handle", 0);
		if (!phy_node)
			continue;

		phy = of_phy_find_device(phy_node);
		of_node_put(phy_node);
		if (!phy)
			continue;

		err = ocelot_probe_port(ocelot, port, regs, phy);
		if (err) {
			of_node_put(portnp);
			goto out_put_ports;
		}

		err = of_get_phy_mode(portnp, &phy_mode);
		if (err && err != -ENODEV)
			goto out_put_ports;

		ocelot->ports[port]->phy_mode = phy_mode;

		switch (ocelot->ports[port]->phy_mode) {
		case PHY_INTERFACE_MODE_NA:
			continue;
		case PHY_INTERFACE_MODE_SGMII:
			break;
		case PHY_INTERFACE_MODE_QSGMII:
			/* Ensure clock signals and speed is set on all
			 * QSGMII links
			 */
			ocelot_port_writel(ocelot->ports[port],
					   DEV_CLOCK_CFG_LINK_SPEED
					   (OCELOT_SPEED_1000),
					   DEV_CLOCK_CFG);
			break;
		default:
			dev_err(ocelot->dev,
				"invalid phy mode for port%d, (Q)SGMII only\n",
				port);
			of_node_put(portnp);
			err = -EINVAL;
			goto out_put_ports;
		}

		serdes = devm_of_phy_get(ocelot->dev, portnp, NULL);
		if (IS_ERR(serdes)) {
			err = PTR_ERR(serdes);
			if (err == -EPROBE_DEFER)
				dev_dbg(ocelot->dev, "deferring probe\n");
			else
				dev_err(ocelot->dev,
					"missing SerDes phys for port%d\n",
					port);

			of_node_put(portnp);
			goto out_put_ports;
		}

		ocelot->ports[port]->serdes = serdes;
	}

	register_netdevice_notifier(&ocelot_netdevice_nb);
	register_switchdev_notifier(&ocelot_switchdev_nb);
	register_switchdev_blocking_notifier(&ocelot_switchdev_blocking_nb);

	dev_info(&pdev->dev, "Ocelot switch probed\n");

out_put_ports:
	of_node_put(ports);
	return err;
}

static int mscc_ocelot_remove(struct platform_device *pdev)
{
	struct ocelot *ocelot = platform_get_drvdata(pdev);

	ocelot_deinit(ocelot);
	unregister_switchdev_blocking_notifier(&ocelot_switchdev_blocking_nb);
	unregister_switchdev_notifier(&ocelot_switchdev_nb);
	unregister_netdevice_notifier(&ocelot_netdevice_nb);

	return 0;
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
