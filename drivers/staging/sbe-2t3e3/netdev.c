/*
 * SBE 2T3E3 synchronous serial card driver for Linux
 *
 * Copyright (C) 2009-2010 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This code is based on a driver written by SBE Inc.
 */

#include <linux/capability.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/hdlc.h>
#include <linux/if_arp.h>
#include <linux/interrupt.h>
#include "2t3e3.h"

int t3e3_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct channel *sc = dev_to_priv(dev);
	int cmd_2t3e3, len, rlen;
	t3e3_param_t param;
	t3e3_resp_t  resp;
	void __user *data = ifr->ifr_data + sizeof(cmd_2t3e3) + sizeof(len);

	if (cmd == SIOCWANDEV)
		return hdlc_ioctl(dev, ifr, cmd);
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (cmd != SIOCDEVPRIVATE + 15)
		return -EINVAL;

	if (copy_from_user(&cmd_2t3e3, ifr->ifr_data, sizeof(cmd_2t3e3)))
		return -EFAULT;
	if (copy_from_user(&len, ifr->ifr_data + sizeof(cmd_2t3e3), sizeof(len)))
		return -EFAULT;

	if (len > sizeof(param))
		return -EFAULT;

	if (len)
		if (copy_from_user(&param, data, len))
			return -EFAULT;

	t3e3_if_config(sc, cmd_2t3e3, (char *)&param, &resp, &rlen);

	if (rlen)
		if (copy_to_user(data, &resp, rlen))
			return -EFAULT;

	return 0;
}

static struct net_device_stats* t3e3_get_stats(struct net_device *dev)
{
	struct net_device_stats *nstats = &dev->stats;
	struct channel *sc = dev_to_priv(dev);
	t3e3_stats_t *stats = &sc->s;

	memset(nstats, 0, sizeof(struct net_device_stats));
	nstats->rx_packets = stats->in_packets;
	nstats->tx_packets = stats->out_packets;
	nstats->rx_bytes = stats->in_bytes;
	nstats->tx_bytes = stats->out_bytes;

	nstats->rx_errors = stats->in_errors;
	nstats->tx_errors = stats->out_errors;
	nstats->rx_crc_errors = stats->in_error_crc;


	nstats->rx_dropped = stats->in_dropped;
	nstats->tx_dropped = stats->out_dropped;
	nstats->tx_carrier_errors = stats->out_error_lost_carr +
		stats->out_error_no_carr;

	return nstats;
}

int t3e3_open(struct net_device *dev)
{
	struct channel *sc = dev_to_priv(dev);
	int ret = hdlc_open(dev);

	if (ret)
		return ret;

	sc->r.flags |= SBE_2T3E3_FLAG_NETWORK_UP;
	dc_start(dev_to_priv(dev));
	netif_start_queue(dev);
	try_module_get(THIS_MODULE);
	return 0;
}

int t3e3_close(struct net_device *dev)
{
	struct channel *sc = dev_to_priv(dev);
	hdlc_close(dev);
	netif_stop_queue(dev);
	dc_stop(sc);
	sc->r.flags &= ~SBE_2T3E3_FLAG_NETWORK_UP;
	module_put(THIS_MODULE);
	return 0;
}

static int t3e3_attach(struct net_device *dev, unsigned short foo1,
		       unsigned short foo2)
{
	return 0;
}

static const struct net_device_ops t3e3_ops = {
	.ndo_open       = t3e3_open,
	.ndo_stop       = t3e3_close,
	.ndo_change_mtu = hdlc_change_mtu,
	.ndo_start_xmit = hdlc_start_xmit,
	.ndo_do_ioctl   = t3e3_ioctl,
	.ndo_get_stats  = t3e3_get_stats,
};

int setup_device(struct net_device *dev, struct channel *sc)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	int retval;

	dev->base_addr = pci_resource_start(sc->pdev, 0);
	dev->irq = sc->pdev->irq;
	dev->netdev_ops = &t3e3_ops;
	dev->tx_queue_len = 100;
	hdlc->xmit = t3e3_if_start_xmit;
	hdlc->attach = t3e3_attach;
	if ((retval = register_hdlc_device(dev))) {
		dev_err(&sc->pdev->dev, "error registering HDLC device\n");
		return retval;
	}
	return 0;
}
