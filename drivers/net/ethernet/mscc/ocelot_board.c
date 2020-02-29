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

#include <soc/mscc/ocelot_vcap.h>
#include "ocelot.h"

#define IFH_EXTRACT_BITFIELD64(x, o, w) (((x) >> (o)) & GENMASK_ULL((w) - 1, 0))
#define VSC7514_VCAP_IS2_CNT 64
#define VSC7514_VCAP_IS2_ENTRY_WIDTH 376
#define VSC7514_VCAP_IS2_ACTION_WIDTH 99
#define VSC7514_VCAP_PORT_CNT 11

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
		struct ocelot_port_private *priv;
		struct ocelot_port *ocelot_port;
		u64 tod_in_ns, full_ts_in_ns;
		struct frame_info info = {};
		struct net_device *dev;
		u32 ifh[4], val, *buf;
		struct timespec64 ts;
		int sz, len, buf_len;
		struct sk_buff *skb;

		for (i = 0; i < OCELOT_TAG_LEN / 4; i++) {
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

		ocelot_port = ocelot->ports[info.port];
		priv = container_of(ocelot_port, struct ocelot_port_private,
				    port);
		dev = priv->dev;

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
	struct ocelot *ocelot = arg;

	ocelot_get_txtstamp(ocelot);

	return IRQ_HANDLED;
}

static const struct of_device_id mscc_ocelot_match[] = {
	{ .compatible = "mscc,vsc7514-switch" },
	{ }
};
MODULE_DEVICE_TABLE(of, mscc_ocelot_match);

static int ocelot_reset(struct ocelot *ocelot)
{
	int retries = 100;
	u32 val;

	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_MEM_INIT], 1);
	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_MEM_ENA], 1);

	do {
		msleep(1);
		regmap_field_read(ocelot->regfields[SYS_RESET_CFG_MEM_INIT],
				  &val);
	} while (val && --retries);

	if (!retries)
		return -ETIMEDOUT;

	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_MEM_ENA], 1);
	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_CORE_ENA], 1);

	return 0;
}

static const struct ocelot_ops ocelot_ops = {
	.reset			= ocelot_reset,
};

static const struct vcap_field vsc7514_vcap_is2_keys[] = {
	/* Common: 46 bits */
	[VCAP_IS2_TYPE]				= {  0,   4},
	[VCAP_IS2_HK_FIRST]			= {  4,   1},
	[VCAP_IS2_HK_PAG]			= {  5,   8},
	[VCAP_IS2_HK_IGR_PORT_MASK]		= { 13,  12},
	[VCAP_IS2_HK_RSV2]			= { 25,   1},
	[VCAP_IS2_HK_HOST_MATCH]		= { 26,   1},
	[VCAP_IS2_HK_L2_MC]			= { 27,   1},
	[VCAP_IS2_HK_L2_BC]			= { 28,   1},
	[VCAP_IS2_HK_VLAN_TAGGED]		= { 29,   1},
	[VCAP_IS2_HK_VID]			= { 30,  12},
	[VCAP_IS2_HK_DEI]			= { 42,   1},
	[VCAP_IS2_HK_PCP]			= { 43,   3},
	/* MAC_ETYPE / MAC_LLC / MAC_SNAP / OAM common */
	[VCAP_IS2_HK_L2_DMAC]			= { 46,  48},
	[VCAP_IS2_HK_L2_SMAC]			= { 94,  48},
	/* MAC_ETYPE (TYPE=000) */
	[VCAP_IS2_HK_MAC_ETYPE_ETYPE]		= {142,  16},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD0]	= {158,  16},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD1]	= {174,   8},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD2]	= {182,   3},
	/* MAC_LLC (TYPE=001) */
	[VCAP_IS2_HK_MAC_LLC_L2_LLC]		= {142,  40},
	/* MAC_SNAP (TYPE=010) */
	[VCAP_IS2_HK_MAC_SNAP_L2_SNAP]		= {142,  40},
	/* MAC_ARP (TYPE=011) */
	[VCAP_IS2_HK_MAC_ARP_SMAC]		= { 46,  48},
	[VCAP_IS2_HK_MAC_ARP_ADDR_SPACE_OK]	= { 94,   1},
	[VCAP_IS2_HK_MAC_ARP_PROTO_SPACE_OK]	= { 95,   1},
	[VCAP_IS2_HK_MAC_ARP_LEN_OK]		= { 96,   1},
	[VCAP_IS2_HK_MAC_ARP_TARGET_MATCH]	= { 97,   1},
	[VCAP_IS2_HK_MAC_ARP_SENDER_MATCH]	= { 98,   1},
	[VCAP_IS2_HK_MAC_ARP_OPCODE_UNKNOWN]	= { 99,   1},
	[VCAP_IS2_HK_MAC_ARP_OPCODE]		= {100,   2},
	[VCAP_IS2_HK_MAC_ARP_L3_IP4_DIP]	= {102,  32},
	[VCAP_IS2_HK_MAC_ARP_L3_IP4_SIP]	= {134,  32},
	[VCAP_IS2_HK_MAC_ARP_DIP_EQ_SIP]	= {166,   1},
	/* IP4_TCP_UDP / IP4_OTHER common */
	[VCAP_IS2_HK_IP4]			= { 46,   1},
	[VCAP_IS2_HK_L3_FRAGMENT]		= { 47,   1},
	[VCAP_IS2_HK_L3_FRAG_OFS_GT0]		= { 48,   1},
	[VCAP_IS2_HK_L3_OPTIONS]		= { 49,   1},
	[VCAP_IS2_HK_IP4_L3_TTL_GT0]		= { 50,   1},
	[VCAP_IS2_HK_L3_TOS]			= { 51,   8},
	[VCAP_IS2_HK_L3_IP4_DIP]		= { 59,  32},
	[VCAP_IS2_HK_L3_IP4_SIP]		= { 91,  32},
	[VCAP_IS2_HK_DIP_EQ_SIP]		= {123,   1},
	/* IP4_TCP_UDP (TYPE=100) */
	[VCAP_IS2_HK_TCP]			= {124,   1},
	[VCAP_IS2_HK_L4_SPORT]			= {125,  16},
	[VCAP_IS2_HK_L4_DPORT]			= {141,  16},
	[VCAP_IS2_HK_L4_RNG]			= {157,   8},
	[VCAP_IS2_HK_L4_SPORT_EQ_DPORT]		= {165,   1},
	[VCAP_IS2_HK_L4_SEQUENCE_EQ0]		= {166,   1},
	[VCAP_IS2_HK_L4_URG]			= {167,   1},
	[VCAP_IS2_HK_L4_ACK]			= {168,   1},
	[VCAP_IS2_HK_L4_PSH]			= {169,   1},
	[VCAP_IS2_HK_L4_RST]			= {170,   1},
	[VCAP_IS2_HK_L4_SYN]			= {171,   1},
	[VCAP_IS2_HK_L4_FIN]			= {172,   1},
	[VCAP_IS2_HK_L4_1588_DOM]		= {173,   8},
	[VCAP_IS2_HK_L4_1588_VER]		= {181,   4},
	/* IP4_OTHER (TYPE=101) */
	[VCAP_IS2_HK_IP4_L3_PROTO]		= {124,   8},
	[VCAP_IS2_HK_L3_PAYLOAD]		= {132,  56},
	/* IP6_STD (TYPE=110) */
	[VCAP_IS2_HK_IP6_L3_TTL_GT0]		= { 46,   1},
	[VCAP_IS2_HK_L3_IP6_SIP]		= { 47, 128},
	[VCAP_IS2_HK_IP6_L3_PROTO]		= {175,   8},
	/* OAM (TYPE=111) */
	[VCAP_IS2_HK_OAM_MEL_FLAGS]		= {142,   7},
	[VCAP_IS2_HK_OAM_VER]			= {149,   5},
	[VCAP_IS2_HK_OAM_OPCODE]		= {154,   8},
	[VCAP_IS2_HK_OAM_FLAGS]			= {162,   8},
	[VCAP_IS2_HK_OAM_MEPID]			= {170,  16},
	[VCAP_IS2_HK_OAM_CCM_CNTS_EQ0]		= {186,   1},
	[VCAP_IS2_HK_OAM_IS_Y1731]		= {187,   1},
};

static const struct vcap_field vsc7514_vcap_is2_actions[] = {
	[VCAP_IS2_ACT_HIT_ME_ONCE]		= {  0,  1},
	[VCAP_IS2_ACT_CPU_COPY_ENA]		= {  1,  1},
	[VCAP_IS2_ACT_CPU_QU_NUM]		= {  2,  3},
	[VCAP_IS2_ACT_MASK_MODE]		= {  5,  2},
	[VCAP_IS2_ACT_MIRROR_ENA]		= {  7,  1},
	[VCAP_IS2_ACT_LRN_DIS]			= {  8,  1},
	[VCAP_IS2_ACT_POLICE_ENA]		= {  9,  1},
	[VCAP_IS2_ACT_POLICE_IDX]		= { 10,  9},
	[VCAP_IS2_ACT_POLICE_VCAP_ONLY]		= { 19,  1},
	[VCAP_IS2_ACT_PORT_MASK]		= { 20, 11},
	[VCAP_IS2_ACT_REW_OP]			= { 31,  9},
	[VCAP_IS2_ACT_SMAC_REPLACE_ENA]		= { 40,  1},
	[VCAP_IS2_ACT_RSV]			= { 41,  2},
	[VCAP_IS2_ACT_ACL_ID]			= { 43,  6},
	[VCAP_IS2_ACT_HIT_CNT]			= { 49, 32},
};

static const struct vcap_props vsc7514_vcap_props[] = {
	[VCAP_IS2] = {
		.tg_width = 2,
		.sw_count = 4,
		.entry_count = VSC7514_VCAP_IS2_CNT,
		.entry_width = VSC7514_VCAP_IS2_ENTRY_WIDTH,
		.action_count = VSC7514_VCAP_IS2_CNT +
				VSC7514_VCAP_PORT_CNT + 2,
		.action_width = 99,
		.action_type_width = 1,
		.action_table = {
			[IS2_ACTION_TYPE_NORMAL] = {
				.width = 49,
				.count = 2
			},
			[IS2_ACTION_TYPE_SMAC_SIP] = {
				.width = 6,
				.count = 4
			},
		},
		.counter_words = 4,
		.counter_width = 32,
	},
};

static int mscc_ocelot_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *ports, *portnp;
	int err, irq_xtr, irq_ptp_rdy;
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
			return PTR_ERR(target);
		}

		ocelot->targets[io_target[i].id] = target;
	}

	hsio = syscon_regmap_lookup_by_compatible("mscc,ocelot-hsio");
	if (IS_ERR(hsio)) {
		dev_err(&pdev->dev, "missing hsio syscon\n");
		return PTR_ERR(hsio);
	}

	ocelot->targets[HSIO] = hsio;

	err = ocelot_chip_init(ocelot, &ocelot_ops);
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

	ocelot->num_cpu_ports = 1; /* 1 port on the switch, two groups */

	ports = of_get_child_by_name(np, "ethernet-ports");
	if (!ports) {
		dev_err(&pdev->dev, "no ethernet-ports child node found\n");
		return -ENODEV;
	}

	ocelot->num_phys_ports = of_get_child_count(ports);

	ocelot->ports = devm_kcalloc(&pdev->dev, ocelot->num_phys_ports,
				     sizeof(struct ocelot_port *), GFP_KERNEL);

	ocelot->vcap_is2_keys = vsc7514_vcap_is2_keys;
	ocelot->vcap_is2_actions = vsc7514_vcap_is2_actions;
	ocelot->vcap = vsc7514_vcap_props;

	ocelot_init(ocelot);
	ocelot_set_cpu_port(ocelot, ocelot->num_phys_ports,
			    OCELOT_TAG_PREFIX_NONE, OCELOT_TAG_PREFIX_NONE);

	for_each_available_child_of_node(ports, portnp) {
		struct ocelot_port_private *priv;
		struct ocelot_port *ocelot_port;
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

		ocelot_port = ocelot->ports[port];
		priv = container_of(ocelot_port, struct ocelot_port_private,
				    port);

		of_get_phy_mode(portnp, &phy_mode);

		ocelot_port->phy_mode = phy_mode;

		switch (ocelot_port->phy_mode) {
		case PHY_INTERFACE_MODE_NA:
			continue;
		case PHY_INTERFACE_MODE_SGMII:
			break;
		case PHY_INTERFACE_MODE_QSGMII:
			/* Ensure clock signals and speed is set on all
			 * QSGMII links
			 */
			ocelot_port_writel(ocelot_port,
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

		priv->serdes = serdes;
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
