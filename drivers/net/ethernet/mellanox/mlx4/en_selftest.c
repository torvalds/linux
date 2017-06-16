/*
 * Copyright (c) 2007 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/kernel.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/mlx4/driver.h>

#include "mlx4_en.h"


static int mlx4_en_test_registers(struct mlx4_en_priv *priv)
{
	return mlx4_cmd(priv->mdev->dev, 0, 0, 0, MLX4_CMD_HW_HEALTH_CHECK,
			MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
}

static int mlx4_en_test_loopback_xmit(struct mlx4_en_priv *priv)
{
	struct sk_buff *skb;
	struct ethhdr *ethh;
	unsigned char *packet;
	unsigned int packet_size = MLX4_LOOPBACK_TEST_PAYLOAD;
	unsigned int i;
	int err;


	/* build the pkt before xmit */
	skb = netdev_alloc_skb(priv->dev, MLX4_LOOPBACK_TEST_PAYLOAD + ETH_HLEN + NET_IP_ALIGN);
	if (!skb)
		return -ENOMEM;

	skb_reserve(skb, NET_IP_ALIGN);

	ethh = skb_put(skb, sizeof(struct ethhdr));
	packet = skb_put(skb, packet_size);
	memcpy(ethh->h_dest, priv->dev->dev_addr, ETH_ALEN);
	eth_zero_addr(ethh->h_source);
	ethh->h_proto = htons(ETH_P_ARP);
	skb_reset_mac_header(skb);
	for (i = 0; i < packet_size; ++i)	/* fill our packet */
		packet[i] = (unsigned char)(i & 0xff);

	/* xmit the pkt */
	err = mlx4_en_xmit(skb, priv->dev);
	return err;
}

static int mlx4_en_test_loopback(struct mlx4_en_priv *priv)
{
	u32 loopback_ok = 0;
	int i;

        priv->loopback_ok = 0;
	priv->validate_loopback = 1;

	mlx4_en_update_loopback_state(priv->dev, priv->dev->features);

	/* xmit */
	if (mlx4_en_test_loopback_xmit(priv)) {
		en_err(priv, "Transmitting loopback packet failed\n");
		goto mlx4_en_test_loopback_exit;
	}

	/* polling for result */
	for (i = 0; i < MLX4_EN_LOOPBACK_RETRIES; ++i) {
		msleep(MLX4_EN_LOOPBACK_TIMEOUT);
		if (priv->loopback_ok) {
			loopback_ok = 1;
			break;
		}
	}
	if (!loopback_ok)
		en_err(priv, "Loopback packet didn't arrive\n");

mlx4_en_test_loopback_exit:

	priv->validate_loopback = 0;

	mlx4_en_update_loopback_state(priv->dev, priv->dev->features);
	return !loopback_ok;
}

static int mlx4_en_test_interrupts(struct mlx4_en_priv *priv)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	int err = 0;
	int i = 0;

	err = mlx4_test_async(mdev->dev);
	/* When not in MSI_X or slave, test only async */
	if (!(mdev->dev->flags & MLX4_FLAG_MSI_X) || mlx4_is_slave(mdev->dev))
		return err;

	/* A loop over all completion vectors of current port,
	 * for each vector check whether it works by mapping command
	 * completions to that vector and performing a NOP command
	 */
	for (i = 0; i < priv->rx_ring_num; i++) {
		err = mlx4_test_interrupt(mdev->dev, priv->rx_cq[i]->vector);
		if (err)
			break;
	}

	return err;
}

static int mlx4_en_test_link(struct mlx4_en_priv *priv)
{
	if (mlx4_en_QUERY_PORT(priv->mdev, priv->port))
		return -ENOMEM;
	if (priv->port_state.link_state == 1)
		return 0;
	else
		return 1;
}

static int mlx4_en_test_speed(struct mlx4_en_priv *priv)
{

	if (mlx4_en_QUERY_PORT(priv->mdev, priv->port))
		return -ENOMEM;

	/* The device supports 100M, 1G, 10G, 20G, 40G and 56G speed */
	if (priv->port_state.link_speed != SPEED_100 &&
	    priv->port_state.link_speed != SPEED_1000 &&
	    priv->port_state.link_speed != SPEED_10000 &&
	    priv->port_state.link_speed != SPEED_20000 &&
	    priv->port_state.link_speed != SPEED_40000 &&
	    priv->port_state.link_speed != SPEED_56000)
		return priv->port_state.link_speed;

	return 0;
}


void mlx4_en_ex_selftest(struct net_device *dev, u32 *flags, u64 *buf)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int i, carrier_ok;

	memset(buf, 0, sizeof(u64) * MLX4_EN_NUM_SELF_TEST);

	if (*flags & ETH_TEST_FL_OFFLINE) {
		/* disable the interface */
		carrier_ok = netif_carrier_ok(dev);

		netif_carrier_off(dev);
		/* Wait until all tx queues are empty.
		 * there should not be any additional incoming traffic
		 * since we turned the carrier off */
		msleep(200);

		if (priv->mdev->dev->caps.flags &
					MLX4_DEV_CAP_FLAG_UC_LOOPBACK) {
			buf[3] = mlx4_en_test_registers(priv);
			if (priv->port_up)
				buf[4] = mlx4_en_test_loopback(priv);
		}

		if (carrier_ok)
			netif_carrier_on(dev);

	}
	buf[0] = mlx4_en_test_interrupts(priv);
	buf[1] = mlx4_en_test_link(priv);
	buf[2] = mlx4_en_test_speed(priv);

	for (i = 0; i < MLX4_EN_NUM_SELF_TEST; i++) {
		if (buf[i])
			*flags |= ETH_TEST_FL_FAILED;
	}
}
