// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2005 Marc Kleine-Budde, Pengutronix
 * Copyright (C) 2006 Andrey Volkov, Varma Electronics
 * Copyright (C) 2008-2009 Wolfgang Grandegger <wg@grandegger.com>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/workqueue.h>
#include <linux/can.h>
#include <linux/can/can-ml.h>
#include <linux/can/dev.h>
#include <linux/can/skb.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>

static void can_update_state_error_stats(struct net_device *dev,
					 enum can_state new_state)
{
	struct can_priv *priv = netdev_priv(dev);

	if (new_state <= priv->state)
		return;

	switch (new_state) {
	case CAN_STATE_ERROR_WARNING:
		priv->can_stats.error_warning++;
		break;
	case CAN_STATE_ERROR_PASSIVE:
		priv->can_stats.error_passive++;
		break;
	case CAN_STATE_BUS_OFF:
		priv->can_stats.bus_off++;
		break;
	default:
		break;
	}
}

static int can_tx_state_to_frame(struct net_device *dev, enum can_state state)
{
	switch (state) {
	case CAN_STATE_ERROR_ACTIVE:
		return CAN_ERR_CRTL_ACTIVE;
	case CAN_STATE_ERROR_WARNING:
		return CAN_ERR_CRTL_TX_WARNING;
	case CAN_STATE_ERROR_PASSIVE:
		return CAN_ERR_CRTL_TX_PASSIVE;
	default:
		return 0;
	}
}

static int can_rx_state_to_frame(struct net_device *dev, enum can_state state)
{
	switch (state) {
	case CAN_STATE_ERROR_ACTIVE:
		return CAN_ERR_CRTL_ACTIVE;
	case CAN_STATE_ERROR_WARNING:
		return CAN_ERR_CRTL_RX_WARNING;
	case CAN_STATE_ERROR_PASSIVE:
		return CAN_ERR_CRTL_RX_PASSIVE;
	default:
		return 0;
	}
}

const char *can_get_state_str(const enum can_state state)
{
	switch (state) {
	case CAN_STATE_ERROR_ACTIVE:
		return "Error Active";
	case CAN_STATE_ERROR_WARNING:
		return "Error Warning";
	case CAN_STATE_ERROR_PASSIVE:
		return "Error Passive";
	case CAN_STATE_BUS_OFF:
		return "Bus Off";
	case CAN_STATE_STOPPED:
		return "Stopped";
	case CAN_STATE_SLEEPING:
		return "Sleeping";
	default:
		return "<unknown>";
	}

	return "<unknown>";
}
EXPORT_SYMBOL_GPL(can_get_state_str);

static enum can_state can_state_err_to_state(u16 err)
{
	if (err < CAN_ERROR_WARNING_THRESHOLD)
		return CAN_STATE_ERROR_ACTIVE;
	if (err < CAN_ERROR_PASSIVE_THRESHOLD)
		return CAN_STATE_ERROR_WARNING;
	if (err < CAN_BUS_OFF_THRESHOLD)
		return CAN_STATE_ERROR_PASSIVE;

	return CAN_STATE_BUS_OFF;
}

void can_state_get_by_berr_counter(const struct net_device *dev,
				   const struct can_berr_counter *bec,
				   enum can_state *tx_state,
				   enum can_state *rx_state)
{
	*tx_state = can_state_err_to_state(bec->txerr);
	*rx_state = can_state_err_to_state(bec->rxerr);
}
EXPORT_SYMBOL_GPL(can_state_get_by_berr_counter);

void can_change_state(struct net_device *dev, struct can_frame *cf,
		      enum can_state tx_state, enum can_state rx_state)
{
	struct can_priv *priv = netdev_priv(dev);
	enum can_state new_state = max(tx_state, rx_state);

	if (unlikely(new_state == priv->state)) {
		netdev_warn(dev, "%s: oops, state did not change", __func__);
		return;
	}

	netdev_dbg(dev, "Controller changed from %s State (%d) into %s State (%d).\n",
		   can_get_state_str(priv->state), priv->state,
		   can_get_state_str(new_state), new_state);

	can_update_state_error_stats(dev, new_state);
	priv->state = new_state;

	if (!cf)
		return;

	if (unlikely(new_state == CAN_STATE_BUS_OFF)) {
		cf->can_id |= CAN_ERR_BUSOFF;
		return;
	}

	cf->can_id |= CAN_ERR_CRTL;
	cf->data[1] |= tx_state >= rx_state ?
		       can_tx_state_to_frame(dev, tx_state) : 0;
	cf->data[1] |= tx_state <= rx_state ?
		       can_rx_state_to_frame(dev, rx_state) : 0;
}
EXPORT_SYMBOL_GPL(can_change_state);

/* CAN device restart for bus-off recovery */
static void can_restart(struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);
	struct sk_buff *skb;
	struct can_frame *cf;
	int err;

	if (netif_carrier_ok(dev))
		netdev_err(dev, "Attempt to restart for bus-off recovery, but carrier is OK?\n");

	/* No synchronization needed because the device is bus-off and
	 * no messages can come in or go out.
	 */
	can_flush_echo_skb(dev);

	/* send restart message upstream */
	skb = alloc_can_err_skb(dev, &cf);
	if (skb) {
		cf->can_id |= CAN_ERR_RESTARTED;
		netif_rx(skb);
	}

	/* Now restart the device */
	netif_carrier_on(dev);
	err = priv->do_set_mode(dev, CAN_MODE_START);
	if (err) {
		netdev_err(dev, "Restart failed, error %pe\n", ERR_PTR(err));
		netif_carrier_off(dev);
	} else {
		netdev_dbg(dev, "Restarted\n");
		priv->can_stats.restarts++;
	}
}

static void can_restart_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct can_priv *priv = container_of(dwork, struct can_priv,
					     restart_work);

	can_restart(priv->dev);
}

int can_restart_now(struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);

	/* A manual restart is only permitted if automatic restart is
	 * disabled and the device is in the bus-off state
	 */
	if (priv->restart_ms)
		return -EINVAL;
	if (priv->state != CAN_STATE_BUS_OFF)
		return -EBUSY;

	cancel_delayed_work_sync(&priv->restart_work);
	can_restart(dev);

	return 0;
}

/* CAN bus-off
 *
 * This functions should be called when the device goes bus-off to
 * tell the netif layer that no more packets can be sent or received.
 * If enabled, a timer is started to trigger bus-off recovery.
 */
void can_bus_off(struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);

	if (priv->restart_ms)
		netdev_info(dev, "bus-off, scheduling restart in %d ms\n",
			    priv->restart_ms);
	else
		netdev_info(dev, "bus-off\n");

	netif_carrier_off(dev);

	if (priv->restart_ms)
		schedule_delayed_work(&priv->restart_work,
				      msecs_to_jiffies(priv->restart_ms));
}
EXPORT_SYMBOL_GPL(can_bus_off);

void can_setup(struct net_device *dev)
{
	dev->type = ARPHRD_CAN;
	dev->mtu = CAN_MTU;
	dev->hard_header_len = 0;
	dev->addr_len = 0;
	dev->tx_queue_len = 10;

	/* New-style flags. */
	dev->flags = IFF_NOARP;
	dev->features = NETIF_F_HW_CSUM;
}

/* Allocate and setup space for the CAN network device */
struct net_device *alloc_candev_mqs(int sizeof_priv, unsigned int echo_skb_max,
				    unsigned int txqs, unsigned int rxqs)
{
	struct can_ml_priv *can_ml;
	struct net_device *dev;
	struct can_priv *priv;
	int size;

	/* We put the driver's priv, the CAN mid layer priv and the
	 * echo skb into the netdevice's priv. The memory layout for
	 * the netdev_priv is like this:
	 *
	 * +-------------------------+
	 * | driver's priv           |
	 * +-------------------------+
	 * | struct can_ml_priv      |
	 * +-------------------------+
	 * | array of struct sk_buff |
	 * +-------------------------+
	 */

	size = ALIGN(sizeof_priv, NETDEV_ALIGN) + sizeof(struct can_ml_priv);

	if (echo_skb_max)
		size = ALIGN(size, sizeof(struct sk_buff *)) +
			echo_skb_max * sizeof(struct sk_buff *);

	dev = alloc_netdev_mqs(size, "can%d", NET_NAME_UNKNOWN, can_setup,
			       txqs, rxqs);
	if (!dev)
		return NULL;

	priv = netdev_priv(dev);
	priv->dev = dev;

	can_ml = (void *)priv + ALIGN(sizeof_priv, NETDEV_ALIGN);
	can_set_ml_priv(dev, can_ml);

	if (echo_skb_max) {
		priv->echo_skb_max = echo_skb_max;
		priv->echo_skb = (void *)priv +
			(size - echo_skb_max * sizeof(struct sk_buff *));
	}

	priv->state = CAN_STATE_STOPPED;

	INIT_DELAYED_WORK(&priv->restart_work, can_restart_work);

	return dev;
}
EXPORT_SYMBOL_GPL(alloc_candev_mqs);

/* Free space of the CAN network device */
void free_candev(struct net_device *dev)
{
	free_netdev(dev);
}
EXPORT_SYMBOL_GPL(free_candev);

/* changing MTU and control mode for CAN/CANFD devices */
int can_change_mtu(struct net_device *dev, int new_mtu)
{
	struct can_priv *priv = netdev_priv(dev);
	u32 ctrlmode_static = can_get_static_ctrlmode(priv);

	/* Do not allow changing the MTU while running */
	if (dev->flags & IFF_UP)
		return -EBUSY;

	/* allow change of MTU according to the CANFD ability of the device */
	switch (new_mtu) {
	case CAN_MTU:
		/* 'CANFD-only' controllers can not switch to CAN_MTU */
		if (ctrlmode_static & CAN_CTRLMODE_FD)
			return -EINVAL;

		priv->ctrlmode &= ~CAN_CTRLMODE_FD;
		break;

	case CANFD_MTU:
		/* check for potential CANFD ability */
		if (!(priv->ctrlmode_supported & CAN_CTRLMODE_FD) &&
		    !(ctrlmode_static & CAN_CTRLMODE_FD))
			return -EINVAL;

		priv->ctrlmode |= CAN_CTRLMODE_FD;
		break;

	default:
		return -EINVAL;
	}

	WRITE_ONCE(dev->mtu, new_mtu);
	return 0;
}
EXPORT_SYMBOL_GPL(can_change_mtu);

/* generic implementation of netdev_ops::ndo_eth_ioctl for CAN devices
 * supporting hardware timestamps
 */
int can_eth_ioctl_hwts(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct hwtstamp_config hwts_cfg = { 0 };

	switch (cmd) {
	case SIOCSHWTSTAMP: /* set */
		if (copy_from_user(&hwts_cfg, ifr->ifr_data, sizeof(hwts_cfg)))
			return -EFAULT;
		if (hwts_cfg.tx_type == HWTSTAMP_TX_ON &&
		    hwts_cfg.rx_filter == HWTSTAMP_FILTER_ALL)
			return 0;
		return -ERANGE;

	case SIOCGHWTSTAMP: /* get */
		hwts_cfg.tx_type = HWTSTAMP_TX_ON;
		hwts_cfg.rx_filter = HWTSTAMP_FILTER_ALL;
		if (copy_to_user(ifr->ifr_data, &hwts_cfg, sizeof(hwts_cfg)))
			return -EFAULT;
		return 0;

	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL(can_eth_ioctl_hwts);

/* generic implementation of ethtool_ops::get_ts_info for CAN devices
 * supporting hardware timestamps
 */
int can_ethtool_op_get_ts_info_hwts(struct net_device *dev,
				    struct kernel_ethtool_ts_info *info)
{
	info->so_timestamping =
		SOF_TIMESTAMPING_TX_SOFTWARE |
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types = BIT(HWTSTAMP_TX_ON);
	info->rx_filters = BIT(HWTSTAMP_FILTER_ALL);

	return 0;
}
EXPORT_SYMBOL(can_ethtool_op_get_ts_info_hwts);

/* Common open function when the device gets opened.
 *
 * This function should be called in the open function of the device
 * driver.
 */
int open_candev(struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);

	if (!priv->bittiming.bitrate) {
		netdev_err(dev, "bit-timing not yet defined\n");
		return -EINVAL;
	}

	/* For CAN FD the data bitrate has to be >= the arbitration bitrate */
	if ((priv->ctrlmode & CAN_CTRLMODE_FD) &&
	    (!priv->data_bittiming.bitrate ||
	     priv->data_bittiming.bitrate < priv->bittiming.bitrate)) {
		netdev_err(dev, "incorrect/missing data bit-timing\n");
		return -EINVAL;
	}

	/* Switch carrier on if device was stopped while in bus-off state */
	if (!netif_carrier_ok(dev))
		netif_carrier_on(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(open_candev);

#ifdef CONFIG_OF
/* Common function that can be used to understand the limitation of
 * a transceiver when it provides no means to determine these limitations
 * at runtime.
 */
void of_can_transceiver(struct net_device *dev)
{
	struct device_node *dn;
	struct can_priv *priv = netdev_priv(dev);
	struct device_node *np = dev->dev.parent->of_node;
	int ret;

	dn = of_get_child_by_name(np, "can-transceiver");
	if (!dn)
		return;

	ret = of_property_read_u32(dn, "max-bitrate", &priv->bitrate_max);
	of_node_put(dn);
	if ((ret && ret != -EINVAL) || (!ret && !priv->bitrate_max))
		netdev_warn(dev, "Invalid value for transceiver max bitrate. Ignoring bitrate limit.\n");
}
EXPORT_SYMBOL_GPL(of_can_transceiver);
#endif

/* Common close function for cleanup before the device gets closed.
 *
 * This function should be called in the close function of the device
 * driver.
 */
void close_candev(struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);

	cancel_delayed_work_sync(&priv->restart_work);
	can_flush_echo_skb(dev);
}
EXPORT_SYMBOL_GPL(close_candev);

static int can_set_termination(struct net_device *ndev, u16 term)
{
	struct can_priv *priv = netdev_priv(ndev);
	int set;

	if (term == priv->termination_gpio_ohms[CAN_TERMINATION_GPIO_ENABLED])
		set = 1;
	else
		set = 0;

	gpiod_set_value(priv->termination_gpio, set);

	return 0;
}

static int can_get_termination(struct net_device *ndev)
{
	struct can_priv *priv = netdev_priv(ndev);
	struct device *dev = ndev->dev.parent;
	struct gpio_desc *gpio;
	u32 term;
	int ret;

	/* Disabling termination by default is the safe choice: Else if many
	 * bus participants enable it, no communication is possible at all.
	 */
	gpio = devm_gpiod_get_optional(dev, "termination", GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return dev_err_probe(dev, PTR_ERR(gpio),
				     "Cannot get termination-gpios\n");

	if (!gpio)
		return 0;

	ret = device_property_read_u32(dev, "termination-ohms", &term);
	if (ret) {
		netdev_err(ndev, "Cannot get termination-ohms: %pe\n",
			   ERR_PTR(ret));
		return ret;
	}

	if (term > U16_MAX) {
		netdev_err(ndev, "Invalid termination-ohms value (%u > %u)\n",
			   term, U16_MAX);
		return -EINVAL;
	}

	priv->termination_const_cnt = ARRAY_SIZE(priv->termination_gpio_ohms);
	priv->termination_const = priv->termination_gpio_ohms;
	priv->termination_gpio = gpio;
	priv->termination_gpio_ohms[CAN_TERMINATION_GPIO_DISABLED] =
		CAN_TERMINATION_DISABLED;
	priv->termination_gpio_ohms[CAN_TERMINATION_GPIO_ENABLED] = term;
	priv->do_set_termination = can_set_termination;

	return 0;
}

static bool
can_bittiming_const_valid(const struct can_bittiming_const *btc)
{
	if (!btc)
		return true;

	if (!btc->sjw_max)
		return false;

	return true;
}

/* Register the CAN network device */
int register_candev(struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);
	int err;

	/* Ensure termination_const, termination_const_cnt and
	 * do_set_termination consistency. All must be either set or
	 * unset.
	 */
	if ((!priv->termination_const != !priv->termination_const_cnt) ||
	    (!priv->termination_const != !priv->do_set_termination))
		return -EINVAL;

	if (!priv->bitrate_const != !priv->bitrate_const_cnt)
		return -EINVAL;

	if (!priv->data_bitrate_const != !priv->data_bitrate_const_cnt)
		return -EINVAL;

	/* We only support either fixed bit rates or bit timing const. */
	if ((priv->bitrate_const || priv->data_bitrate_const) &&
	    (priv->bittiming_const || priv->data_bittiming_const))
		return -EINVAL;

	if (!can_bittiming_const_valid(priv->bittiming_const) ||
	    !can_bittiming_const_valid(priv->data_bittiming_const))
		return -EINVAL;

	if (!priv->termination_const) {
		err = can_get_termination(dev);
		if (err)
			return err;
	}

	dev->rtnl_link_ops = &can_link_ops;
	netif_carrier_off(dev);

	return register_netdev(dev);
}
EXPORT_SYMBOL_GPL(register_candev);

/* Unregister the CAN network device */
void unregister_candev(struct net_device *dev)
{
	unregister_netdev(dev);
}
EXPORT_SYMBOL_GPL(unregister_candev);

/* Test if a network device is a candev based device
 * and return the can_priv* if so.
 */
struct can_priv *safe_candev_priv(struct net_device *dev)
{
	if (dev->type != ARPHRD_CAN || dev->rtnl_link_ops != &can_link_ops)
		return NULL;

	return netdev_priv(dev);
}
EXPORT_SYMBOL_GPL(safe_candev_priv);

static __init int can_dev_init(void)
{
	int err;

	err = can_netlink_register();
	if (!err)
		pr_info("CAN device driver interface\n");

	return err;
}
module_init(can_dev_init);

static __exit void can_dev_exit(void)
{
	can_netlink_unregister();
}
module_exit(can_dev_exit);

MODULE_ALIAS_RTNL_LINK("can");
