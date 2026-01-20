// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2025 Vincent Mailhol <mailhol@kernel.org> */

#include <linux/array_size.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/units.h>
#include <linux/string_choices.h>

#include <linux/can.h>
#include <linux/can/bittiming.h>
#include <linux/can/dev.h>
#include <linux/can/skb.h>

struct dummy_can {
	struct can_priv can;
	struct net_device *dev;
};

static struct dummy_can *dummy_can;

static const struct can_bittiming_const dummy_can_bittiming_const = {
	.name = "dummy_can CC",
	.tseg1_min = 2,
	.tseg1_max = 256,
	.tseg2_min = 2,
	.tseg2_max = 128,
	.sjw_max = 128,
	.brp_min = 1,
	.brp_max = 512,
	.brp_inc = 1
};

static const struct can_bittiming_const dummy_can_fd_databittiming_const = {
	.name = "dummy_can FD",
	.tseg1_min = 2,
	.tseg1_max = 256,
	.tseg2_min = 2,
	.tseg2_max = 128,
	.sjw_max = 128,
	.brp_min = 1,
	.brp_max = 512,
	.brp_inc = 1
};

static const struct can_tdc_const dummy_can_fd_tdc_const = {
	.tdcv_min = 0,
	.tdcv_max = 0, /* Manual mode not supported. */
	.tdco_min = 0,
	.tdco_max = 127,
	.tdcf_min = 0,
	.tdcf_max = 127
};

static const struct can_bittiming_const dummy_can_xl_databittiming_const = {
	.name = "dummy_can XL",
	.tseg1_min = 2,
	.tseg1_max = 256,
	.tseg2_min = 2,
	.tseg2_max = 128,
	.sjw_max = 128,
	.brp_min = 1,
	.brp_max = 512,
	.brp_inc = 1
};

static const struct can_tdc_const dummy_can_xl_tdc_const = {
	.tdcv_min = 0,
	.tdcv_max = 0, /* Manual mode not supported. */
	.tdco_min = 0,
	.tdco_max = 127,
	.tdcf_min = 0,
	.tdcf_max = 127
};

static const struct can_pwm_const dummy_can_pwm_const = {
	.pwms_min = 1,
	.pwms_max = 8,
	.pwml_min = 2,
	.pwml_max = 24,
	.pwmo_min = 0,
	.pwmo_max = 16,
};

static void dummy_can_print_bittiming(struct net_device *dev,
				      struct can_bittiming *bt)
{
	netdev_dbg(dev, "\tbitrate: %u\n", bt->bitrate);
	netdev_dbg(dev, "\tsample_point: %u\n", bt->sample_point);
	netdev_dbg(dev, "\ttq: %u\n", bt->tq);
	netdev_dbg(dev, "\tprop_seg: %u\n", bt->prop_seg);
	netdev_dbg(dev, "\tphase_seg1: %u\n", bt->phase_seg1);
	netdev_dbg(dev, "\tphase_seg2: %u\n", bt->phase_seg2);
	netdev_dbg(dev, "\tsjw: %u\n", bt->sjw);
	netdev_dbg(dev, "\tbrp: %u\n", bt->brp);
}

static void dummy_can_print_tdc(struct net_device *dev, struct can_tdc *tdc)
{
	netdev_dbg(dev, "\t\ttdcv: %u\n", tdc->tdcv);
	netdev_dbg(dev, "\t\ttdco: %u\n", tdc->tdco);
	netdev_dbg(dev, "\t\ttdcf: %u\n", tdc->tdcf);
}

static void dummy_can_print_pwm(struct net_device *dev, struct can_pwm *pwm,
				struct can_bittiming *dbt)
{
	netdev_dbg(dev, "\t\tpwms: %u\n", pwm->pwms);
	netdev_dbg(dev, "\t\tpwml: %u\n", pwm->pwml);
	netdev_dbg(dev, "\t\tpwmo: %u\n", pwm->pwmo);
}

static void dummy_can_print_ctrlmode(struct net_device *dev)
{
	struct dummy_can *priv = netdev_priv(dev);
	struct can_priv *can_priv = &priv->can;
	unsigned long supported = can_priv->ctrlmode_supported;
	u32 enabled = can_priv->ctrlmode;

	netdev_dbg(dev, "Control modes:\n");
	netdev_dbg(dev, "\tsupported: 0x%08x\n", (u32)supported);
	netdev_dbg(dev, "\tenabled: 0x%08x\n", enabled);

	if (supported) {
		int idx;

		netdev_dbg(dev, "\tlist:");
		for_each_set_bit(idx, &supported, BITS_PER_TYPE(u32))
			netdev_dbg(dev, "\t\t%s: %s\n",
				   can_get_ctrlmode_str(BIT(idx)),
				   enabled & BIT(idx) ? "on" : "off");
	}
}

static void dummy_can_print_bittiming_info(struct net_device *dev)
{
	struct dummy_can *priv = netdev_priv(dev);
	struct can_priv *can_priv = &priv->can;

	netdev_dbg(dev, "Clock frequency: %u\n", can_priv->clock.freq);
	netdev_dbg(dev, "Maximum bitrate: %u\n", can_priv->bitrate_max);
	netdev_dbg(dev, "MTU: %u\n", dev->mtu);
	netdev_dbg(dev, "\n");

	dummy_can_print_ctrlmode(dev);
	netdev_dbg(dev, "\n");

	netdev_dbg(dev, "Classical CAN nominal bittiming:\n");
	dummy_can_print_bittiming(dev, &can_priv->bittiming);
	netdev_dbg(dev, "\n");

	if (can_priv->ctrlmode & CAN_CTRLMODE_FD) {
		netdev_dbg(dev, "CAN FD databittiming:\n");
		dummy_can_print_bittiming(dev, &can_priv->fd.data_bittiming);
		if (can_fd_tdc_is_enabled(can_priv)) {
			netdev_dbg(dev, "\tCAN FD TDC:\n");
			dummy_can_print_tdc(dev, &can_priv->fd.tdc);
		}
	}
	netdev_dbg(dev, "\n");

	if (can_priv->ctrlmode & CAN_CTRLMODE_XL) {
		netdev_dbg(dev, "CAN XL databittiming:\n");
		dummy_can_print_bittiming(dev, &can_priv->xl.data_bittiming);
		if (can_xl_tdc_is_enabled(can_priv)) {
			netdev_dbg(dev, "\tCAN XL TDC:\n");
			dummy_can_print_tdc(dev, &can_priv->xl.tdc);
		}
		if (can_priv->ctrlmode & CAN_CTRLMODE_XL_TMS) {
			netdev_dbg(dev, "\tCAN XL PWM:\n");
			dummy_can_print_pwm(dev, &can_priv->xl.pwm,
					    &can_priv->xl.data_bittiming);
		}
	}
	netdev_dbg(dev, "\n");
}

static int dummy_can_netdev_open(struct net_device *dev)
{
	int ret;
	struct can_priv *priv = netdev_priv(dev);

	dummy_can_print_bittiming_info(dev);
	netdev_dbg(dev, "error-signalling is %s\n",
		   str_enabled_disabled(!can_dev_in_xl_only_mode(priv)));

	ret = open_candev(dev);
	if (ret)
		return ret;
	netif_start_queue(dev);
	netdev_dbg(dev, "dummy-can is up\n");

	return 0;
}

static int dummy_can_netdev_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	close_candev(dev);
	netdev_dbg(dev, "dummy-can is down\n");

	return 0;
}

static netdev_tx_t dummy_can_start_xmit(struct sk_buff *skb,
					struct net_device *dev)
{
	if (can_dev_dropped_skb(dev, skb))
		return NETDEV_TX_OK;

	can_put_echo_skb(skb, dev, 0, 0);
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += can_get_echo_skb(dev, 0, NULL);

	return NETDEV_TX_OK;
}

static const struct net_device_ops dummy_can_netdev_ops = {
	.ndo_open = dummy_can_netdev_open,
	.ndo_stop = dummy_can_netdev_close,
	.ndo_start_xmit = dummy_can_start_xmit,
};

static const struct ethtool_ops dummy_can_ethtool_ops = {
	.get_ts_info = ethtool_op_get_ts_info,
};

static int __init dummy_can_init(void)
{
	struct net_device *dev;
	struct dummy_can *priv;
	int ret;

	dev = alloc_candev(sizeof(*priv), 1);
	if (!dev)
		return -ENOMEM;

	dev->netdev_ops = &dummy_can_netdev_ops;
	dev->ethtool_ops = &dummy_can_ethtool_ops;
	priv = netdev_priv(dev);
	priv->can.bittiming_const = &dummy_can_bittiming_const;
	priv->can.bitrate_max = 20 * MEGA /* BPS */;
	priv->can.clock.freq = 160 * MEGA /* Hz */;
	priv->can.fd.data_bittiming_const = &dummy_can_fd_databittiming_const;
	priv->can.fd.tdc_const = &dummy_can_fd_tdc_const;
	priv->can.xl.data_bittiming_const = &dummy_can_xl_databittiming_const;
	priv->can.xl.tdc_const = &dummy_can_xl_tdc_const;
	priv->can.xl.pwm_const = &dummy_can_pwm_const;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LISTENONLY |
		CAN_CTRLMODE_FD | CAN_CTRLMODE_TDC_AUTO |
		CAN_CTRLMODE_RESTRICTED | CAN_CTRLMODE_XL |
		CAN_CTRLMODE_XL_TDC_AUTO | CAN_CTRLMODE_XL_TMS;
	priv->dev = dev;

	ret = register_candev(priv->dev);
	if (ret) {
		free_candev(priv->dev);
		return ret;
	}

	dummy_can = priv;
	netdev_dbg(dev, "dummy-can ready\n");

	return 0;
}

static void __exit dummy_can_exit(void)
{
	struct net_device *dev = dummy_can->dev;

	netdev_dbg(dev, "dummy-can bye bye\n");
	unregister_candev(dev);
	free_candev(dev);
}

module_init(dummy_can_init);
module_exit(dummy_can_exit);

MODULE_DESCRIPTION("A dummy CAN driver, mainly to test the netlink interface");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vincent Mailhol <mailhol@kernel.org>");
