// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/skbuff.h>

#include "ocelot.h"

static int ocelot_parse_ifh(u32 *ifh, struct frame_info *info)
{
	int i;
	u8 llen, wlen;

	/* The IFH is in network order, switch to CPU order */
	for (i = 0; i < IFH_LEN; i++)
		ifh[i] = ntohl((__force __be32)ifh[i]);

	wlen = (ifh[1] >> 7) & 0xff;
	llen = (ifh[1] >> 15) & 0x3f;
	info->len = OCELOT_BUFFER_CELL_SZ * wlen + llen - 80;

	info->port = (ifh[2] & GENMASK(14, 11)) >> 11;

	info->cpuq = (ifh[3] & GENMASK(27, 20)) >> 20;
	info->tag_type = (ifh[3] & BIT(16)) >> 16;
	info->vid = ifh[3] & GENMASK(11, 0);

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
		struct sk_buff *skb;
		struct net_device *dev;
		u32 *buf;
		int sz, len, buf_len;
		u32 ifh[4];
		u32 val;
		struct frame_info info;

		for (i = 0; i < IFH_LEN; i++) {
			err = ocelot_rx_frame_word(ocelot, grp, true, &ifh[i]);
			if (err != 4)
				break;
		}

		if (err != 4)
			break;

		/* At this point the IFH was read correctly, so it is safe to
		 * presume that there is no error. The err needs to be reset
		 * otherwise a frame could come in CPU queue between the while
		 * condition and the check for error later on. And in that case
		 * the new frame is just removed and not processed.
		 */
		err = 0;

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

		/* Read the FCS and discard it */
		sz = ocelot_rx_frame_word(ocelot, grp, false, &val);
		/* Update the statistics if part of the FCS was read before */
		len -= ETH_FCS_LEN - sz;

		if (sz < 0) {
			err = sz;
			break;
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

static const struct of_device_id mscc_ocelot_match[] = {
	{ .compatible = "mscc,vsc7514-switch" },
	{ }
};
MODULE_DEVICE_TABLE(of, mscc_ocelot_match);

static int mscc_ocelot_probe(struct platform_device *pdev)
{
	int err, irq;
	unsigned int i;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *ports, *portnp;
	struct ocelot *ocelot;
	u32 val;

	struct {
		enum ocelot_target id;
		char *name;
	} res[] = {
		{ SYS, "sys" },
		{ REW, "rew" },
		{ QSYS, "qsys" },
		{ ANA, "ana" },
		{ QS, "qs" },
		{ HSIO, "hsio" },
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
		if (IS_ERR(target))
			return PTR_ERR(target);

		ocelot->targets[res[i].id] = target;
	}

	err = ocelot_chip_init(ocelot);
	if (err)
		return err;

	irq = platform_get_irq_byname(pdev, "xtr");
	if (irq < 0)
		return -ENODEV;

	err = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					ocelot_xtr_irq_handler, IRQF_ONESHOT,
					"frame extraction", ocelot);
	if (err)
		return err;

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

	ocelot_rmw(ocelot, HSIO_HW_CFG_DEV1G_4_MODE |
		     HSIO_HW_CFG_DEV1G_6_MODE |
		     HSIO_HW_CFG_DEV1G_9_MODE,
		     HSIO_HW_CFG_DEV1G_4_MODE |
		     HSIO_HW_CFG_DEV1G_6_MODE |
		     HSIO_HW_CFG_DEV1G_9_MODE,
		     HSIO_HW_CFG);

	for_each_available_child_of_node(ports, portnp) {
		struct device_node *phy_node;
		struct phy_device *phy;
		struct resource *res;
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
		if (!phy)
			continue;

		err = ocelot_probe_port(ocelot, port, regs, phy);
		if (err) {
			dev_err(&pdev->dev, "failed to probe ports\n");
			goto err_probe_ports;
		}
	}

	register_netdevice_notifier(&ocelot_netdevice_nb);

	dev_info(&pdev->dev, "Ocelot switch probed\n");

	return 0;

err_probe_ports:
	return err;
}

static int mscc_ocelot_remove(struct platform_device *pdev)
{
	struct ocelot *ocelot = platform_get_drvdata(pdev);

	ocelot_deinit(ocelot);
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
