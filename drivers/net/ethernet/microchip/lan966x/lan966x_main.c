// SPDX-License-Identifier: GPL-2.0+

#include <linux/module.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/iopoll.h>
#include <linux/ip.h>
#include <linux/of_platform.h>
#include <linux/of_net.h>
#include <linux/packing.h>
#include <linux/phy/phy.h>
#include <linux/reset.h>
#include <net/addrconf.h>

#include "lan966x_main.h"

#define XTR_EOF_0			0x00000080U
#define XTR_EOF_1			0x01000080U
#define XTR_EOF_2			0x02000080U
#define XTR_EOF_3			0x03000080U
#define XTR_PRUNED			0x04000080U
#define XTR_ABORT			0x05000080U
#define XTR_ESCAPE			0x06000080U
#define XTR_NOT_READY			0x07000080U
#define XTR_VALID_BYTES(x)		(4 - (((x) >> 24) & 3))

#define READL_SLEEP_US			10
#define READL_TIMEOUT_US		100000000

#define IO_RANGES 2

static const struct of_device_id lan966x_match[] = {
	{ .compatible = "microchip,lan966x-switch" },
	{ }
};
MODULE_DEVICE_TABLE(of, lan966x_match);

struct lan966x_main_io_resource {
	enum lan966x_target id;
	phys_addr_t offset;
	int range;
};

static const struct lan966x_main_io_resource lan966x_main_iomap[] =  {
	{ TARGET_CPU,                   0xc0000, 0 }, /* 0xe00c0000 */
	{ TARGET_ORG,                         0, 1 }, /* 0xe2000000 */
	{ TARGET_GCB,                    0x4000, 1 }, /* 0xe2004000 */
	{ TARGET_QS,                     0x8000, 1 }, /* 0xe2008000 */
	{ TARGET_PTP,                    0xc000, 1 }, /* 0xe200c000 */
	{ TARGET_CHIP_TOP,              0x10000, 1 }, /* 0xe2010000 */
	{ TARGET_REW,                   0x14000, 1 }, /* 0xe2014000 */
	{ TARGET_SYS,                   0x28000, 1 }, /* 0xe2028000 */
	{ TARGET_DEV,                   0x34000, 1 }, /* 0xe2034000 */
	{ TARGET_DEV +  1,              0x38000, 1 }, /* 0xe2038000 */
	{ TARGET_DEV +  2,              0x3c000, 1 }, /* 0xe203c000 */
	{ TARGET_DEV +  3,              0x40000, 1 }, /* 0xe2040000 */
	{ TARGET_DEV +  4,              0x44000, 1 }, /* 0xe2044000 */
	{ TARGET_DEV +  5,              0x48000, 1 }, /* 0xe2048000 */
	{ TARGET_DEV +  6,              0x4c000, 1 }, /* 0xe204c000 */
	{ TARGET_DEV +  7,              0x50000, 1 }, /* 0xe2050000 */
	{ TARGET_QSYS,                 0x100000, 1 }, /* 0xe2100000 */
	{ TARGET_AFI,                  0x120000, 1 }, /* 0xe2120000 */
	{ TARGET_ANA,                  0x140000, 1 }, /* 0xe2140000 */
};

static int lan966x_create_targets(struct platform_device *pdev,
				  struct lan966x *lan966x)
{
	struct resource *iores[IO_RANGES];
	void __iomem *begin[IO_RANGES];
	int idx;

	/* Initially map the entire range and after that update each target to
	 * point inside the region at the correct offset. It is possible that
	 * other devices access the same region so don't add any checks about
	 * this.
	 */
	for (idx = 0; idx < IO_RANGES; idx++) {
		iores[idx] = platform_get_resource(pdev, IORESOURCE_MEM,
						   idx);
		if (!iores[idx]) {
			dev_err(&pdev->dev, "Invalid resource\n");
			return -EINVAL;
		}

		begin[idx] = devm_ioremap(&pdev->dev,
					  iores[idx]->start,
					  resource_size(iores[idx]));
		if (!begin[idx]) {
			dev_err(&pdev->dev, "Unable to get registers: %s\n",
				iores[idx]->name);
			return -ENOMEM;
		}
	}

	for (idx = 0; idx < ARRAY_SIZE(lan966x_main_iomap); idx++) {
		const struct lan966x_main_io_resource *iomap =
			&lan966x_main_iomap[idx];

		lan966x->regs[iomap->id] = begin[iomap->range] + iomap->offset;
	}

	return 0;
}

static bool lan966x_port_unique_address(struct net_device *dev)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	int p;

	for (p = 0; p < lan966x->num_phys_ports; ++p) {
		port = lan966x->ports[p];
		if (!port || port->dev == dev)
			continue;

		if (ether_addr_equal(dev->dev_addr, port->dev->dev_addr))
			return false;
	}

	return true;
}

static int lan966x_port_set_mac_address(struct net_device *dev, void *p)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	const struct sockaddr *addr = p;
	int ret;

	if (ether_addr_equal(addr->sa_data, dev->dev_addr))
		return 0;

	/* Learn the new net device MAC address in the mac table. */
	ret = lan966x_mac_cpu_learn(lan966x, addr->sa_data, HOST_PVID);
	if (ret)
		return ret;

	/* If there is another port with the same address as the dev, then don't
	 * delete it from the MAC table
	 */
	if (!lan966x_port_unique_address(dev))
		goto out;

	/* Then forget the previous one. */
	ret = lan966x_mac_cpu_forget(lan966x, dev->dev_addr, HOST_PVID);
	if (ret)
		return ret;

out:
	eth_hw_addr_set(dev, addr->sa_data);
	return ret;
}

static int lan966x_port_get_phys_port_name(struct net_device *dev,
					   char *buf, size_t len)
{
	struct lan966x_port *port = netdev_priv(dev);
	int ret;

	ret = snprintf(buf, len, "p%d", port->chip_port);
	if (ret >= len)
		return -EINVAL;

	return 0;
}

static int lan966x_port_open(struct net_device *dev)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	int err;

	/* Enable receiving frames on the port, and activate auto-learning of
	 * MAC addresses.
	 */
	lan_rmw(ANA_PORT_CFG_LEARNAUTO_SET(1) |
		ANA_PORT_CFG_RECV_ENA_SET(1) |
		ANA_PORT_CFG_PORTID_VAL_SET(port->chip_port),
		ANA_PORT_CFG_LEARNAUTO |
		ANA_PORT_CFG_RECV_ENA |
		ANA_PORT_CFG_PORTID_VAL,
		lan966x, ANA_PORT_CFG(port->chip_port));

	err = phylink_fwnode_phy_connect(port->phylink, port->fwnode, 0);
	if (err) {
		netdev_err(dev, "Could not attach to PHY\n");
		return err;
	}

	phylink_start(port->phylink);

	return 0;
}

static int lan966x_port_stop(struct net_device *dev)
{
	struct lan966x_port *port = netdev_priv(dev);

	lan966x_port_config_down(port);
	phylink_stop(port->phylink);
	phylink_disconnect_phy(port->phylink);

	return 0;
}

static int lan966x_port_inj_status(struct lan966x *lan966x)
{
	return lan_rd(lan966x, QS_INJ_STATUS);
}

static int lan966x_port_inj_ready(struct lan966x *lan966x, u8 grp)
{
	u32 val;

	if (lan_rd(lan966x, QS_INJ_STATUS) & QS_INJ_STATUS_FIFO_RDY_SET(BIT(grp)))
		return 0;

	return readx_poll_timeout_atomic(lan966x_port_inj_status, lan966x, val,
					 QS_INJ_STATUS_FIFO_RDY_GET(val) & BIT(grp),
					 READL_SLEEP_US, READL_TIMEOUT_US);
}

static int lan966x_port_ifh_xmit(struct sk_buff *skb,
				 __be32 *ifh,
				 struct net_device *dev)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	u32 i, count, last;
	u8 grp = 0;
	u32 val;
	int err;

	val = lan_rd(lan966x, QS_INJ_STATUS);
	if (!(QS_INJ_STATUS_FIFO_RDY_GET(val) & BIT(grp)) ||
	    (QS_INJ_STATUS_WMARK_REACHED_GET(val) & BIT(grp)))
		goto err;

	/* Write start of frame */
	lan_wr(QS_INJ_CTRL_GAP_SIZE_SET(1) |
	       QS_INJ_CTRL_SOF_SET(1),
	       lan966x, QS_INJ_CTRL(grp));

	/* Write IFH header */
	for (i = 0; i < IFH_LEN; ++i) {
		/* Wait until the fifo is ready */
		err = lan966x_port_inj_ready(lan966x, grp);
		if (err)
			goto err;

		lan_wr((__force u32)ifh[i], lan966x, QS_INJ_WR(grp));
	}

	/* Write frame */
	count = DIV_ROUND_UP(skb->len, 4);
	last = skb->len % 4;
	for (i = 0; i < count; ++i) {
		/* Wait until the fifo is ready */
		err = lan966x_port_inj_ready(lan966x, grp);
		if (err)
			goto err;

		lan_wr(((u32 *)skb->data)[i], lan966x, QS_INJ_WR(grp));
	}

	/* Add padding */
	while (i < (LAN966X_BUFFER_MIN_SZ / 4)) {
		/* Wait until the fifo is ready */
		err = lan966x_port_inj_ready(lan966x, grp);
		if (err)
			goto err;

		lan_wr(0, lan966x, QS_INJ_WR(grp));
		++i;
	}

	/* Inidcate EOF and valid bytes in the last word */
	lan_wr(QS_INJ_CTRL_GAP_SIZE_SET(1) |
	       QS_INJ_CTRL_VLD_BYTES_SET(skb->len < LAN966X_BUFFER_MIN_SZ ?
				     0 : last) |
	       QS_INJ_CTRL_EOF_SET(1),
	       lan966x, QS_INJ_CTRL(grp));

	/* Add dummy CRC */
	lan_wr(0, lan966x, QS_INJ_WR(grp));
	skb_tx_timestamp(skb);

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP &&
	    LAN966X_SKB_CB(skb)->rew_op == IFH_REW_OP_TWO_STEP_PTP)
		return NETDEV_TX_OK;

	dev_consume_skb_any(skb);
	return NETDEV_TX_OK;

err:
	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP &&
	    LAN966X_SKB_CB(skb)->rew_op == IFH_REW_OP_TWO_STEP_PTP)
		lan966x_ptp_txtstamp_release(port, skb);

	return NETDEV_TX_BUSY;
}

static void lan966x_ifh_set_bypass(void *ifh, u64 bypass)
{
	packing(ifh, &bypass, IFH_POS_BYPASS + IFH_WID_BYPASS - 1,
		IFH_POS_BYPASS, IFH_LEN * 4, PACK, 0);
}

static void lan966x_ifh_set_port(void *ifh, u64 bypass)
{
	packing(ifh, &bypass, IFH_POS_DSTS + IFH_WID_DSTS - 1,
		IFH_POS_DSTS, IFH_LEN * 4, PACK, 0);
}

static void lan966x_ifh_set_qos_class(void *ifh, u64 bypass)
{
	packing(ifh, &bypass, IFH_POS_QOS_CLASS + IFH_WID_QOS_CLASS - 1,
		IFH_POS_QOS_CLASS, IFH_LEN * 4, PACK, 0);
}

static void lan966x_ifh_set_ipv(void *ifh, u64 bypass)
{
	packing(ifh, &bypass, IFH_POS_IPV + IFH_WID_IPV - 1,
		IFH_POS_IPV, IFH_LEN * 4, PACK, 0);
}

static void lan966x_ifh_set_vid(void *ifh, u64 vid)
{
	packing(ifh, &vid, IFH_POS_TCI + IFH_WID_TCI - 1,
		IFH_POS_TCI, IFH_LEN * 4, PACK, 0);
}

static void lan966x_ifh_set_rew_op(void *ifh, u64 rew_op)
{
	packing(ifh, &rew_op, IFH_POS_REW_CMD + IFH_WID_REW_CMD - 1,
		IFH_POS_REW_CMD, IFH_LEN * 4, PACK, 0);
}

static void lan966x_ifh_set_timestamp(void *ifh, u64 timestamp)
{
	packing(ifh, &timestamp, IFH_POS_TIMESTAMP + IFH_WID_TIMESTAMP - 1,
		IFH_POS_TIMESTAMP, IFH_LEN * 4, PACK, 0);
}

static int lan966x_port_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	__be32 ifh[IFH_LEN];
	int err;

	memset(ifh, 0x0, sizeof(__be32) * IFH_LEN);

	lan966x_ifh_set_bypass(ifh, 1);
	lan966x_ifh_set_port(ifh, BIT_ULL(port->chip_port));
	lan966x_ifh_set_qos_class(ifh, skb->priority >= 7 ? 0x7 : skb->priority);
	lan966x_ifh_set_ipv(ifh, skb->priority >= 7 ? 0x7 : skb->priority);
	lan966x_ifh_set_vid(ifh, skb_vlan_tag_get(skb));

	if (port->lan966x->ptp && skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
		err = lan966x_ptp_txtstamp_request(port, skb);
		if (err)
			return err;

		lan966x_ifh_set_rew_op(ifh, LAN966X_SKB_CB(skb)->rew_op);
		lan966x_ifh_set_timestamp(ifh, LAN966X_SKB_CB(skb)->ts_id);
	}

	spin_lock(&lan966x->tx_lock);
	err = lan966x_port_ifh_xmit(skb, ifh, dev);
	spin_unlock(&lan966x->tx_lock);

	return err;
}

static int lan966x_port_change_mtu(struct net_device *dev, int new_mtu)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;

	lan_wr(DEV_MAC_MAXLEN_CFG_MAX_LEN_SET(new_mtu),
	       lan966x, DEV_MAC_MAXLEN_CFG(port->chip_port));
	dev->mtu = new_mtu;

	return 0;
}

static int lan966x_mc_unsync(struct net_device *dev, const unsigned char *addr)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;

	return lan966x_mac_forget(lan966x, addr, HOST_PVID, ENTRYTYPE_LOCKED);
}

static int lan966x_mc_sync(struct net_device *dev, const unsigned char *addr)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;

	return lan966x_mac_cpu_learn(lan966x, addr, HOST_PVID);
}

static void lan966x_port_set_rx_mode(struct net_device *dev)
{
	__dev_mc_sync(dev, lan966x_mc_sync, lan966x_mc_unsync);
}

static int lan966x_port_get_parent_id(struct net_device *dev,
				      struct netdev_phys_item_id *ppid)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;

	ppid->id_len = sizeof(lan966x->base_mac);
	memcpy(&ppid->id, &lan966x->base_mac, ppid->id_len);

	return 0;
}

static int lan966x_port_ioctl(struct net_device *dev, struct ifreq *ifr,
			      int cmd)
{
	struct lan966x_port *port = netdev_priv(dev);

	if (!phy_has_hwtstamp(dev->phydev) && port->lan966x->ptp) {
		switch (cmd) {
		case SIOCSHWTSTAMP:
			return lan966x_ptp_hwtstamp_set(port, ifr);
		case SIOCGHWTSTAMP:
			return lan966x_ptp_hwtstamp_get(port, ifr);
		}
	}

	if (!dev->phydev)
		return -ENODEV;

	return phy_mii_ioctl(dev->phydev, ifr, cmd);
}

static const struct net_device_ops lan966x_port_netdev_ops = {
	.ndo_open			= lan966x_port_open,
	.ndo_stop			= lan966x_port_stop,
	.ndo_start_xmit			= lan966x_port_xmit,
	.ndo_change_mtu			= lan966x_port_change_mtu,
	.ndo_set_rx_mode		= lan966x_port_set_rx_mode,
	.ndo_get_phys_port_name		= lan966x_port_get_phys_port_name,
	.ndo_get_stats64		= lan966x_stats_get,
	.ndo_set_mac_address		= lan966x_port_set_mac_address,
	.ndo_get_port_parent_id		= lan966x_port_get_parent_id,
	.ndo_eth_ioctl			= lan966x_port_ioctl,
};

bool lan966x_netdevice_check(const struct net_device *dev)
{
	return dev->netdev_ops == &lan966x_port_netdev_ops;
}

static bool lan966x_hw_offload(struct lan966x *lan966x, u32 port,
			       struct sk_buff *skb)
{
	u32 val;

	/* The IGMP and MLD frames are not forward by the HW if
	 * multicast snooping is enabled, therefor don't mark as
	 * offload to allow the SW to forward the frames accordingly.
	 */
	val = lan_rd(lan966x, ANA_CPU_FWD_CFG(port));
	if (!(val & (ANA_CPU_FWD_CFG_IGMP_REDIR_ENA |
		     ANA_CPU_FWD_CFG_MLD_REDIR_ENA)))
		return true;

	if (eth_type_vlan(skb->protocol)) {
		skb = skb_vlan_untag(skb);
		if (unlikely(!skb))
			return false;
	}

	if (skb->protocol == htons(ETH_P_IP) &&
	    ip_hdr(skb)->protocol == IPPROTO_IGMP)
		return false;

	if (IS_ENABLED(CONFIG_IPV6) &&
	    skb->protocol == htons(ETH_P_IPV6) &&
	    ipv6_addr_is_multicast(&ipv6_hdr(skb)->daddr) &&
	    !ipv6_mc_check_mld(skb))
		return false;

	return true;
}

static int lan966x_port_xtr_status(struct lan966x *lan966x, u8 grp)
{
	return lan_rd(lan966x, QS_XTR_RD(grp));
}

static int lan966x_port_xtr_ready(struct lan966x *lan966x, u8 grp)
{
	u32 val;

	return read_poll_timeout(lan966x_port_xtr_status, val,
				 val != XTR_NOT_READY,
				 READL_SLEEP_US, READL_TIMEOUT_US, false,
				 lan966x, grp);
}

static int lan966x_rx_frame_word(struct lan966x *lan966x, u8 grp, u32 *rval)
{
	u32 bytes_valid;
	u32 val;
	int err;

	val = lan_rd(lan966x, QS_XTR_RD(grp));
	if (val == XTR_NOT_READY) {
		err = lan966x_port_xtr_ready(lan966x, grp);
		if (err)
			return -EIO;
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
		val = lan_rd(lan966x, QS_XTR_RD(grp));
		if (val == XTR_ESCAPE)
			*rval = lan_rd(lan966x, QS_XTR_RD(grp));
		else
			*rval = val;

		return bytes_valid;
	case XTR_ESCAPE:
		*rval = lan_rd(lan966x, QS_XTR_RD(grp));

		return 4;
	default:
		*rval = val;

		return 4;
	}
}

static void lan966x_ifh_get_src_port(void *ifh, u64 *src_port)
{
	packing(ifh, src_port, IFH_POS_SRCPORT + IFH_WID_SRCPORT - 1,
		IFH_POS_SRCPORT, IFH_LEN * 4, UNPACK, 0);
}

static void lan966x_ifh_get_len(void *ifh, u64 *len)
{
	packing(ifh, len, IFH_POS_LEN + IFH_WID_LEN - 1,
		IFH_POS_LEN, IFH_LEN * 4, UNPACK, 0);
}

static void lan966x_ifh_get_timestamp(void *ifh, u64 *timestamp)
{
	packing(ifh, timestamp, IFH_POS_TIMESTAMP + IFH_WID_TIMESTAMP - 1,
		IFH_POS_TIMESTAMP, IFH_LEN * 4, UNPACK, 0);
}

static irqreturn_t lan966x_xtr_irq_handler(int irq, void *args)
{
	struct lan966x *lan966x = args;
	int i, grp = 0, err = 0;

	if (!(lan_rd(lan966x, QS_XTR_DATA_PRESENT) & BIT(grp)))
		return IRQ_NONE;

	do {
		u64 src_port, len, timestamp;
		struct net_device *dev;
		struct sk_buff *skb;
		int sz = 0, buf_len;
		u32 ifh[IFH_LEN];
		u32 *buf;
		u32 val;

		for (i = 0; i < IFH_LEN; i++) {
			err = lan966x_rx_frame_word(lan966x, grp, &ifh[i]);
			if (err != 4)
				goto recover;
		}

		err = 0;

		lan966x_ifh_get_src_port(ifh, &src_port);
		lan966x_ifh_get_len(ifh, &len);
		lan966x_ifh_get_timestamp(ifh, &timestamp);

		WARN_ON(src_port >= lan966x->num_phys_ports);

		dev = lan966x->ports[src_port]->dev;
		skb = netdev_alloc_skb(dev, len);
		if (unlikely(!skb)) {
			netdev_err(dev, "Unable to allocate sk_buff\n");
			err = -ENOMEM;
			break;
		}
		buf_len = len - ETH_FCS_LEN;
		buf = (u32 *)skb_put(skb, buf_len);

		len = 0;
		do {
			sz = lan966x_rx_frame_word(lan966x, grp, &val);
			if (sz < 0) {
				kfree_skb(skb);
				goto recover;
			}

			*buf++ = val;
			len += sz;
		} while (len < buf_len);

		/* Read the FCS */
		sz = lan966x_rx_frame_word(lan966x, grp, &val);
		if (sz < 0) {
			kfree_skb(skb);
			goto recover;
		}

		/* Update the statistics if part of the FCS was read before */
		len -= ETH_FCS_LEN - sz;

		if (unlikely(dev->features & NETIF_F_RXFCS)) {
			buf = (u32 *)skb_put(skb, ETH_FCS_LEN);
			*buf = val;
		}

		lan966x_ptp_rxtstamp(lan966x, skb, timestamp);
		skb->protocol = eth_type_trans(skb, dev);

		if (lan966x->bridge_mask & BIT(src_port)) {
			skb->offload_fwd_mark = 1;

			skb_reset_network_header(skb);
			if (!lan966x_hw_offload(lan966x, src_port, skb))
				skb->offload_fwd_mark = 0;
		}

		if (!skb_defer_rx_timestamp(skb))
			netif_rx(skb);

		dev->stats.rx_bytes += len;
		dev->stats.rx_packets++;

recover:
		if (sz < 0 || err)
			lan_rd(lan966x, QS_XTR_RD(grp));

	} while (lan_rd(lan966x, QS_XTR_DATA_PRESENT) & BIT(grp));

	return IRQ_HANDLED;
}

static irqreturn_t lan966x_ana_irq_handler(int irq, void *args)
{
	struct lan966x *lan966x = args;

	return lan966x_mac_irq_handler(lan966x);
}

static void lan966x_cleanup_ports(struct lan966x *lan966x)
{
	struct lan966x_port *port;
	int p;

	for (p = 0; p < lan966x->num_phys_ports; p++) {
		port = lan966x->ports[p];
		if (!port)
			continue;

		if (port->dev)
			unregister_netdev(port->dev);

		if (port->phylink) {
			rtnl_lock();
			lan966x_port_stop(port->dev);
			rtnl_unlock();
			phylink_destroy(port->phylink);
			port->phylink = NULL;
		}

		if (port->fwnode)
			fwnode_handle_put(port->fwnode);
	}

	disable_irq(lan966x->xtr_irq);
	lan966x->xtr_irq = -ENXIO;

	if (lan966x->ana_irq) {
		disable_irq(lan966x->ana_irq);
		lan966x->ana_irq = -ENXIO;
	}

	if (lan966x->ptp_irq)
		devm_free_irq(lan966x->dev, lan966x->ptp_irq, lan966x);
}

static int lan966x_probe_port(struct lan966x *lan966x, u32 p,
			      phy_interface_t phy_mode,
			      struct fwnode_handle *portnp)
{
	struct lan966x_port *port;
	struct phylink *phylink;
	struct net_device *dev;
	int err;

	if (p >= lan966x->num_phys_ports)
		return -EINVAL;

	dev = devm_alloc_etherdev_mqs(lan966x->dev,
				      sizeof(struct lan966x_port), 8, 1);
	if (!dev)
		return -ENOMEM;

	SET_NETDEV_DEV(dev, lan966x->dev);
	port = netdev_priv(dev);
	port->dev = dev;
	port->lan966x = lan966x;
	port->chip_port = p;
	lan966x->ports[p] = port;

	dev->max_mtu = ETH_MAX_MTU;

	dev->netdev_ops = &lan966x_port_netdev_ops;
	dev->ethtool_ops = &lan966x_ethtool_ops;
	dev->features |= NETIF_F_HW_VLAN_CTAG_TX |
			 NETIF_F_HW_VLAN_STAG_TX;
	dev->needed_headroom = IFH_LEN * sizeof(u32);

	eth_hw_addr_gen(dev, lan966x->base_mac, p + 1);

	lan966x_mac_learn(lan966x, PGID_CPU, dev->dev_addr, HOST_PVID,
			  ENTRYTYPE_LOCKED);

	port->phylink_config.dev = &port->dev->dev;
	port->phylink_config.type = PHYLINK_NETDEV;
	port->phylink_pcs.poll = true;
	port->phylink_pcs.ops = &lan966x_phylink_pcs_ops;

	port->phylink_config.mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
		MAC_10 | MAC_100 | MAC_1000FD | MAC_2500FD;

	__set_bit(PHY_INTERFACE_MODE_MII,
		  port->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_GMII,
		  port->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_SGMII,
		  port->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_QSGMII,
		  port->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_1000BASEX,
		  port->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_2500BASEX,
		  port->phylink_config.supported_interfaces);

	phylink = phylink_create(&port->phylink_config,
				 portnp,
				 phy_mode,
				 &lan966x_phylink_mac_ops);
	if (IS_ERR(phylink)) {
		port->dev = NULL;
		return PTR_ERR(phylink);
	}

	port->phylink = phylink;

	err = register_netdev(dev);
	if (err) {
		dev_err(lan966x->dev, "register_netdev failed\n");
		return err;
	}

	lan966x_vlan_port_set_vlan_aware(port, 0);
	lan966x_vlan_port_set_vid(port, HOST_PVID, false, false);
	lan966x_vlan_port_apply(port);

	return 0;
}

static void lan966x_init(struct lan966x *lan966x)
{
	u32 p, i;

	/* MAC table initialization */
	lan966x_mac_init(lan966x);

	lan966x_vlan_init(lan966x);

	/* Flush queues */
	lan_wr(lan_rd(lan966x, QS_XTR_FLUSH) |
	       GENMASK(1, 0),
	       lan966x, QS_XTR_FLUSH);

	/* Allow to drain */
	mdelay(1);

	/* All Queues normal */
	lan_wr(lan_rd(lan966x, QS_XTR_FLUSH) &
	       ~(GENMASK(1, 0)),
	       lan966x, QS_XTR_FLUSH);

	/* Set MAC age time to default value, the entry is aged after
	 * 2 * AGE_PERIOD
	 */
	lan_wr(ANA_AUTOAGE_AGE_PERIOD_SET(BR_DEFAULT_AGEING_TIME / 2 / HZ),
	       lan966x, ANA_AUTOAGE);

	/* Disable learning for frames discarded by VLAN ingress filtering */
	lan_rmw(ANA_ADVLEARN_VLAN_CHK_SET(1),
		ANA_ADVLEARN_VLAN_CHK,
		lan966x, ANA_ADVLEARN);

	/* Setup frame ageing - "2 sec" - The unit is 6.5 us on lan966x */
	lan_wr(SYS_FRM_AGING_AGE_TX_ENA_SET(1) |
	       (20000000 / 65),
	       lan966x,  SYS_FRM_AGING);

	/* Map the 8 CPU extraction queues to CPU port */
	lan_wr(0, lan966x, QSYS_CPU_GROUP_MAP);

	/* Do byte-swap and expect status after last data word
	 * Extraction: Mode: manual extraction) | Byte_swap
	 */
	lan_wr(QS_XTR_GRP_CFG_MODE_SET(1) |
	       QS_XTR_GRP_CFG_BYTE_SWAP_SET(1),
	       lan966x, QS_XTR_GRP_CFG(0));

	/* Injection: Mode: manual injection | Byte_swap */
	lan_wr(QS_INJ_GRP_CFG_MODE_SET(1) |
	       QS_INJ_GRP_CFG_BYTE_SWAP_SET(1),
	       lan966x, QS_INJ_GRP_CFG(0));

	lan_rmw(QS_INJ_CTRL_GAP_SIZE_SET(0),
		QS_INJ_CTRL_GAP_SIZE,
		lan966x, QS_INJ_CTRL(0));

	/* Enable IFH insertion/parsing on CPU ports */
	lan_wr(SYS_PORT_MODE_INCL_INJ_HDR_SET(1) |
	       SYS_PORT_MODE_INCL_XTR_HDR_SET(1),
	       lan966x, SYS_PORT_MODE(CPU_PORT));

	/* Setup flooding PGIDs */
	lan_wr(ANA_FLOODING_IPMC_FLD_MC4_DATA_SET(PGID_MCIPV4) |
	       ANA_FLOODING_IPMC_FLD_MC4_CTRL_SET(PGID_MC) |
	       ANA_FLOODING_IPMC_FLD_MC6_DATA_SET(PGID_MCIPV6) |
	       ANA_FLOODING_IPMC_FLD_MC6_CTRL_SET(PGID_MC),
	       lan966x, ANA_FLOODING_IPMC);

	/* There are 8 priorities */
	for (i = 0; i < 8; ++i)
		lan_rmw(ANA_FLOODING_FLD_MULTICAST_SET(PGID_MC) |
			ANA_FLOODING_FLD_UNICAST_SET(PGID_UC) |
			ANA_FLOODING_FLD_BROADCAST_SET(PGID_BC),
			ANA_FLOODING_FLD_MULTICAST |
			ANA_FLOODING_FLD_UNICAST |
			ANA_FLOODING_FLD_BROADCAST,
			lan966x, ANA_FLOODING(i));

	for (i = 0; i < PGID_ENTRIES; ++i)
		/* Set all the entries to obey VLAN_VLAN */
		lan_rmw(ANA_PGID_CFG_OBEY_VLAN_SET(1),
			ANA_PGID_CFG_OBEY_VLAN,
			lan966x, ANA_PGID_CFG(i));

	for (p = 0; p < lan966x->num_phys_ports; p++) {
		/* Disable bridging by default */
		lan_rmw(ANA_PGID_PGID_SET(0x0),
			ANA_PGID_PGID,
			lan966x, ANA_PGID(p + PGID_SRC));

		/* Do not forward BPDU frames to the front ports and copy them
		 * to CPU
		 */
		lan_wr(0xffff, lan966x, ANA_CPU_FWD_BPDU_CFG(p));
	}

	/* Set source buffer size for each priority and each port to 1500 bytes */
	for (i = 0; i <= QSYS_Q_RSRV; ++i) {
		lan_wr(1500 / 64, lan966x, QSYS_RES_CFG(i));
		lan_wr(1500 / 64, lan966x, QSYS_RES_CFG(512 + i));
	}

	/* Enable switching to/from cpu port */
	lan_wr(QSYS_SW_PORT_MODE_PORT_ENA_SET(1) |
	       QSYS_SW_PORT_MODE_SCH_NEXT_CFG_SET(1) |
	       QSYS_SW_PORT_MODE_INGRESS_DROP_MODE_SET(1),
	       lan966x,  QSYS_SW_PORT_MODE(CPU_PORT));

	/* Configure and enable the CPU port */
	lan_rmw(ANA_PGID_PGID_SET(0),
		ANA_PGID_PGID,
		lan966x, ANA_PGID(CPU_PORT));
	lan_rmw(ANA_PGID_PGID_SET(BIT(CPU_PORT)),
		ANA_PGID_PGID,
		lan966x, ANA_PGID(PGID_CPU));

	/* Multicast to all other ports */
	lan_rmw(GENMASK(lan966x->num_phys_ports - 1, 0),
		ANA_PGID_PGID,
		lan966x, ANA_PGID(PGID_MC));

	/* This will be controlled by mrouter ports */
	lan_rmw(GENMASK(lan966x->num_phys_ports - 1, 0),
		ANA_PGID_PGID,
		lan966x, ANA_PGID(PGID_MCIPV4));

	lan_rmw(GENMASK(lan966x->num_phys_ports - 1, 0),
		ANA_PGID_PGID,
		lan966x, ANA_PGID(PGID_MCIPV6));

	/* Unicast to all other ports */
	lan_rmw(GENMASK(lan966x->num_phys_ports - 1, 0),
		ANA_PGID_PGID,
		lan966x, ANA_PGID(PGID_UC));

	/* Broadcast to the CPU port and to other ports */
	lan_rmw(ANA_PGID_PGID_SET(BIT(CPU_PORT) | GENMASK(lan966x->num_phys_ports - 1, 0)),
		ANA_PGID_PGID,
		lan966x, ANA_PGID(PGID_BC));

	lan_wr(REW_PORT_CFG_NO_REWRITE_SET(1),
	       lan966x, REW_PORT_CFG(CPU_PORT));

	lan_rmw(ANA_ANAINTR_INTR_ENA_SET(1),
		ANA_ANAINTR_INTR_ENA,
		lan966x, ANA_ANAINTR);

	spin_lock_init(&lan966x->tx_lock);
}

static int lan966x_ram_init(struct lan966x *lan966x)
{
	return lan_rd(lan966x, SYS_RAM_INIT);
}

static int lan966x_reset_switch(struct lan966x *lan966x)
{
	struct reset_control *switch_reset, *phy_reset;
	int val = 0;
	int ret;

	switch_reset = devm_reset_control_get_shared(lan966x->dev, "switch");
	if (IS_ERR(switch_reset))
		return dev_err_probe(lan966x->dev, PTR_ERR(switch_reset),
				     "Could not obtain switch reset");

	phy_reset = devm_reset_control_get_shared(lan966x->dev, "phy");
	if (IS_ERR(phy_reset))
		return dev_err_probe(lan966x->dev, PTR_ERR(phy_reset),
				     "Could not obtain phy reset\n");

	reset_control_reset(switch_reset);
	reset_control_reset(phy_reset);

	lan_wr(SYS_RESET_CFG_CORE_ENA_SET(0), lan966x, SYS_RESET_CFG);
	lan_wr(SYS_RAM_INIT_RAM_INIT_SET(1), lan966x, SYS_RAM_INIT);
	ret = readx_poll_timeout(lan966x_ram_init, lan966x,
				 val, (val & BIT(1)) == 0, READL_SLEEP_US,
				 READL_TIMEOUT_US);
	if (ret)
		return ret;

	lan_wr(SYS_RESET_CFG_CORE_ENA_SET(1), lan966x, SYS_RESET_CFG);

	return 0;
}

static int lan966x_probe(struct platform_device *pdev)
{
	struct fwnode_handle *ports, *portnp;
	struct lan966x *lan966x;
	u8 mac_addr[ETH_ALEN];
	int err, i;

	lan966x = devm_kzalloc(&pdev->dev, sizeof(*lan966x), GFP_KERNEL);
	if (!lan966x)
		return -ENOMEM;

	platform_set_drvdata(pdev, lan966x);
	lan966x->dev = &pdev->dev;

	if (!device_get_mac_address(&pdev->dev, mac_addr)) {
		ether_addr_copy(lan966x->base_mac, mac_addr);
	} else {
		pr_info("MAC addr was not set, use random MAC\n");
		eth_random_addr(lan966x->base_mac);
		lan966x->base_mac[5] &= 0xf0;
	}

	ports = device_get_named_child_node(&pdev->dev, "ethernet-ports");
	if (!ports)
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "no ethernet-ports child found\n");

	err = lan966x_create_targets(pdev, lan966x);
	if (err)
		return dev_err_probe(&pdev->dev, err,
				     "Failed to create targets");

	err = lan966x_reset_switch(lan966x);
	if (err)
		return dev_err_probe(&pdev->dev, err, "Reset failed");

	i = 0;
	fwnode_for_each_available_child_node(ports, portnp)
		++i;

	lan966x->num_phys_ports = i;
	lan966x->ports = devm_kcalloc(&pdev->dev, lan966x->num_phys_ports,
				      sizeof(struct lan966x_port *),
				      GFP_KERNEL);
	if (!lan966x->ports)
		return -ENOMEM;

	/* There QS system has 32KB of memory */
	lan966x->shared_queue_sz = LAN966X_BUFFER_MEMORY;

	/* set irq */
	lan966x->xtr_irq = platform_get_irq_byname(pdev, "xtr");
	if (lan966x->xtr_irq <= 0)
		return -EINVAL;

	err = devm_request_threaded_irq(&pdev->dev, lan966x->xtr_irq, NULL,
					lan966x_xtr_irq_handler, IRQF_ONESHOT,
					"frame extraction", lan966x);
	if (err) {
		pr_err("Unable to use xtr irq");
		return -ENODEV;
	}

	lan966x->ana_irq = platform_get_irq_byname(pdev, "ana");
	if (lan966x->ana_irq) {
		err = devm_request_threaded_irq(&pdev->dev, lan966x->ana_irq, NULL,
						lan966x_ana_irq_handler, IRQF_ONESHOT,
						"ana irq", lan966x);
		if (err)
			return dev_err_probe(&pdev->dev, err, "Unable to use ana irq");
	}

	lan966x->ptp_irq = platform_get_irq_byname(pdev, "ptp");
	if (lan966x->ptp_irq > 0) {
		err = devm_request_threaded_irq(&pdev->dev, lan966x->ptp_irq, NULL,
						lan966x_ptp_irq_handler, IRQF_ONESHOT,
						"ptp irq", lan966x);
		if (err)
			return dev_err_probe(&pdev->dev, err, "Unable to use ptp irq");

		lan966x->ptp = 1;
	}

	/* init switch */
	lan966x_init(lan966x);
	lan966x_stats_init(lan966x);

	/* go over the child nodes */
	fwnode_for_each_available_child_node(ports, portnp) {
		phy_interface_t phy_mode;
		struct phy *serdes;
		u32 p;

		if (fwnode_property_read_u32(portnp, "reg", &p))
			continue;

		phy_mode = fwnode_get_phy_mode(portnp);
		err = lan966x_probe_port(lan966x, p, phy_mode, portnp);
		if (err)
			goto cleanup_ports;

		/* Read needed configuration */
		lan966x->ports[p]->config.portmode = phy_mode;
		lan966x->ports[p]->fwnode = fwnode_handle_get(portnp);

		serdes = devm_of_phy_get(lan966x->dev, to_of_node(portnp), NULL);
		if (!IS_ERR(serdes))
			lan966x->ports[p]->serdes = serdes;

		lan966x_port_init(lan966x->ports[p]);
	}

	lan966x_mdb_init(lan966x);
	err = lan966x_fdb_init(lan966x);
	if (err)
		goto cleanup_ports;

	err = lan966x_ptp_init(lan966x);
	if (err)
		goto cleanup_fdb;

	return 0;

cleanup_fdb:
	lan966x_fdb_deinit(lan966x);

cleanup_ports:
	fwnode_handle_put(portnp);

	lan966x_cleanup_ports(lan966x);

	cancel_delayed_work_sync(&lan966x->stats_work);
	destroy_workqueue(lan966x->stats_queue);
	mutex_destroy(&lan966x->stats_lock);

	return err;
}

static int lan966x_remove(struct platform_device *pdev)
{
	struct lan966x *lan966x = platform_get_drvdata(pdev);

	lan966x_cleanup_ports(lan966x);

	cancel_delayed_work_sync(&lan966x->stats_work);
	destroy_workqueue(lan966x->stats_queue);
	mutex_destroy(&lan966x->stats_lock);

	lan966x_mac_purge_entries(lan966x);
	lan966x_mdb_deinit(lan966x);
	lan966x_fdb_deinit(lan966x);
	lan966x_ptp_deinit(lan966x);

	return 0;
}

static struct platform_driver lan966x_driver = {
	.probe = lan966x_probe,
	.remove = lan966x_remove,
	.driver = {
		.name = "lan966x-switch",
		.of_match_table = lan966x_match,
	},
};

static int __init lan966x_switch_driver_init(void)
{
	int ret;

	lan966x_register_notifier_blocks();

	ret = platform_driver_register(&lan966x_driver);
	if (ret)
		goto err;

	return 0;

err:
	lan966x_unregister_notifier_blocks();
	return ret;
}

static void __exit lan966x_switch_driver_exit(void)
{
	platform_driver_unregister(&lan966x_driver);
	lan966x_unregister_notifier_blocks();
}

module_init(lan966x_switch_driver_init);
module_exit(lan966x_switch_driver_exit);

MODULE_DESCRIPTION("Microchip LAN966X switch driver");
MODULE_AUTHOR("Horatiu Vultur <horatiu.vultur@microchip.com>");
MODULE_LICENSE("Dual MIT/GPL");
