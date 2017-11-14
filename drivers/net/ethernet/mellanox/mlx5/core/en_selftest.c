/*
 * Copyright (c) 2016, Mellanox Technologies, Ltd.  All rights reserved.
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
 */

#include <linux/prefetch.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <net/udp.h>
#include "en.h"

enum {
	MLX5E_ST_LINK_STATE,
	MLX5E_ST_LINK_SPEED,
	MLX5E_ST_HEALTH_INFO,
#ifdef CONFIG_INET
	MLX5E_ST_LOOPBACK,
#endif
	MLX5E_ST_NUM,
};

const char mlx5e_self_tests[MLX5E_ST_NUM][ETH_GSTRING_LEN] = {
	"Link Test",
	"Speed Test",
	"Health Test",
#ifdef CONFIG_INET
	"Loopback Test",
#endif
};

int mlx5e_self_test_num(struct mlx5e_priv *priv)
{
	return ARRAY_SIZE(mlx5e_self_tests);
}

static int mlx5e_test_health_info(struct mlx5e_priv *priv)
{
	struct mlx5_core_health *health = &priv->mdev->priv.health;

	return health->sick ? 1 : 0;
}

static int mlx5e_test_link_state(struct mlx5e_priv *priv)
{
	u8 port_state;

	if (!netif_carrier_ok(priv->netdev))
		return 1;

	port_state = mlx5_query_vport_state(priv->mdev, MLX5_QUERY_VPORT_STATE_IN_OP_MOD_VNIC_VPORT, 0);
	return port_state == VPORT_STATE_UP ? 0 : 1;
}

static int mlx5e_test_link_speed(struct mlx5e_priv *priv)
{
	u32 out[MLX5_ST_SZ_DW(ptys_reg)];
	u32 eth_proto_oper;
	int i;

	if (!netif_carrier_ok(priv->netdev))
		return 1;

	if (mlx5_query_port_ptys(priv->mdev, out, sizeof(out), MLX5_PTYS_EN, 1))
		return 1;

	eth_proto_oper = MLX5_GET(ptys_reg, out, eth_proto_oper);
	for (i = 0; i < MLX5E_LINK_MODES_NUMBER; i++) {
		if (eth_proto_oper & MLX5E_PROT_MASK(i))
			return 0;
	}
	return 1;
}

#ifdef CONFIG_INET
/* loopback test */
#define MLX5E_TEST_PKT_SIZE (MLX5_MPWRQ_SMALL_PACKET_THRESHOLD - NET_IP_ALIGN)
static const char mlx5e_test_text[ETH_GSTRING_LEN] = "MLX5E SELF TEST";
#define MLX5E_TEST_MAGIC 0x5AEED15C001ULL

struct mlx5ehdr {
	__be32 version;
	__be64 magic;
	char   text[ETH_GSTRING_LEN];
};

static struct sk_buff *mlx5e_test_get_udp_skb(struct mlx5e_priv *priv)
{
	struct sk_buff *skb = NULL;
	struct mlx5ehdr *mlxh;
	struct ethhdr *ethh;
	struct udphdr *udph;
	struct iphdr *iph;
	int datalen, iplen;

	datalen = MLX5E_TEST_PKT_SIZE -
		  (sizeof(*ethh) + sizeof(*iph) + sizeof(*udph));

	skb = netdev_alloc_skb(priv->netdev, MLX5E_TEST_PKT_SIZE);
	if (!skb) {
		netdev_err(priv->netdev, "\tFailed to alloc loopback skb\n");
		return NULL;
	}

	prefetchw(skb->data);
	skb_reserve(skb, NET_IP_ALIGN);

	/*  Reserve for ethernet and IP header  */
	ethh = skb_push(skb, ETH_HLEN);
	skb_reset_mac_header(skb);

	skb_set_network_header(skb, skb->len);
	iph = skb_put(skb, sizeof(struct iphdr));

	skb_set_transport_header(skb, skb->len);
	udph = skb_put(skb, sizeof(struct udphdr));

	/* Fill ETH header */
	ether_addr_copy(ethh->h_dest, priv->netdev->dev_addr);
	eth_zero_addr(ethh->h_source);
	ethh->h_proto = htons(ETH_P_IP);

	/* Fill UDP header */
	udph->source = htons(9);
	udph->dest = htons(9); /* Discard Protocol */
	udph->len = htons(datalen + sizeof(struct udphdr));
	udph->check = 0;

	/* Fill IP header */
	iph->ihl = 5;
	iph->ttl = 32;
	iph->version = 4;
	iph->protocol = IPPROTO_UDP;
	iplen = sizeof(struct iphdr) + sizeof(struct udphdr) + datalen;
	iph->tot_len = htons(iplen);
	iph->frag_off = 0;
	iph->saddr = 0;
	iph->daddr = 0;
	iph->tos = 0;
	iph->id = 0;
	ip_send_check(iph);

	/* Fill test header and data */
	mlxh = skb_put(skb, sizeof(*mlxh));
	mlxh->version = 0;
	mlxh->magic = cpu_to_be64(MLX5E_TEST_MAGIC);
	strlcpy(mlxh->text, mlx5e_test_text, sizeof(mlxh->text));
	datalen -= sizeof(*mlxh);
	skb_put_zero(skb, datalen);

	skb->csum = 0;
	skb->ip_summed = CHECKSUM_PARTIAL;
	udp4_hwcsum(skb, iph->saddr, iph->daddr);

	skb->protocol = htons(ETH_P_IP);
	skb->pkt_type = PACKET_HOST;
	skb->dev = priv->netdev;

	return skb;
}

struct mlx5e_lbt_priv {
	struct packet_type pt;
	struct completion comp;
	bool loopback_ok;
	bool local_lb;
};

static int
mlx5e_test_loopback_validate(struct sk_buff *skb,
			     struct net_device *ndev,
			     struct packet_type *pt,
			     struct net_device *orig_ndev)
{
	struct mlx5e_lbt_priv *lbtp = pt->af_packet_priv;
	struct mlx5ehdr *mlxh;
	struct ethhdr *ethh;
	struct udphdr *udph;
	struct iphdr *iph;

	/* We are only going to peek, no need to clone the SKB */
	if (MLX5E_TEST_PKT_SIZE - ETH_HLEN > skb_headlen(skb))
		goto out;

	ethh = (struct ethhdr *)skb_mac_header(skb);
	if (!ether_addr_equal(ethh->h_dest, orig_ndev->dev_addr))
		goto out;

	iph = ip_hdr(skb);
	if (iph->protocol != IPPROTO_UDP)
		goto out;

	udph = udp_hdr(skb);
	if (udph->dest != htons(9))
		goto out;

	mlxh = (struct mlx5ehdr *)((char *)udph + sizeof(*udph));
	if (mlxh->magic != cpu_to_be64(MLX5E_TEST_MAGIC))
		goto out; /* so close ! */

	/* bingo */
	lbtp->loopback_ok = true;
	complete(&lbtp->comp);
out:
	kfree_skb(skb);
	return 0;
}

static int mlx5e_test_loopback_setup(struct mlx5e_priv *priv,
				     struct mlx5e_lbt_priv *lbtp)
{
	int err = 0;

	/* Temporarily enable local_lb */
	if (MLX5_CAP_GEN(priv->mdev, disable_local_lb)) {
		mlx5_nic_vport_query_local_lb(priv->mdev, &lbtp->local_lb);
		if (!lbtp->local_lb)
			mlx5_nic_vport_update_local_lb(priv->mdev, true);
	}

	err = mlx5e_refresh_tirs(priv, true);
	if (err)
		return err;

	lbtp->loopback_ok = false;
	init_completion(&lbtp->comp);

	lbtp->pt.type = htons(ETH_P_IP);
	lbtp->pt.func = mlx5e_test_loopback_validate;
	lbtp->pt.dev = priv->netdev;
	lbtp->pt.af_packet_priv = lbtp;
	dev_add_pack(&lbtp->pt);
	return err;
}

static void mlx5e_test_loopback_cleanup(struct mlx5e_priv *priv,
					struct mlx5e_lbt_priv *lbtp)
{
	if (MLX5_CAP_GEN(priv->mdev, disable_local_lb)) {
		if (!lbtp->local_lb)
			mlx5_nic_vport_update_local_lb(priv->mdev, false);
	}

	dev_remove_pack(&lbtp->pt);
	mlx5e_refresh_tirs(priv, false);
}

#define MLX5E_LB_VERIFY_TIMEOUT (msecs_to_jiffies(200))
static int mlx5e_test_loopback(struct mlx5e_priv *priv)
{
	struct mlx5e_lbt_priv *lbtp;
	struct sk_buff *skb = NULL;
	int err;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		netdev_err(priv->netdev,
			   "\tCan't perform loobpack test while device is down\n");
		return -ENODEV;
	}

	lbtp = kzalloc(sizeof(*lbtp), GFP_KERNEL);
	if (!lbtp)
		return -ENOMEM;
	lbtp->loopback_ok = false;

	err = mlx5e_test_loopback_setup(priv, lbtp);
	if (err)
		goto out;

	skb = mlx5e_test_get_udp_skb(priv);
	if (!skb) {
		err = -ENOMEM;
		goto cleanup;
	}

	skb_set_queue_mapping(skb, 0);
	err = dev_queue_xmit(skb);
	if (err) {
		netdev_err(priv->netdev,
			   "\tFailed to xmit loopback packet err(%d)\n",
			   err);
		goto cleanup;
	}

	wait_for_completion_timeout(&lbtp->comp, MLX5E_LB_VERIFY_TIMEOUT);
	err = !lbtp->loopback_ok;

cleanup:
	mlx5e_test_loopback_cleanup(priv, lbtp);
out:
	kfree(lbtp);
	return err;
}
#endif

static int (*mlx5e_st_func[MLX5E_ST_NUM])(struct mlx5e_priv *) = {
	mlx5e_test_link_state,
	mlx5e_test_link_speed,
	mlx5e_test_health_info,
#ifdef CONFIG_INET
	mlx5e_test_loopback,
#endif
};

void mlx5e_self_test(struct net_device *ndev, struct ethtool_test *etest,
		     u64 *buf)
{
	struct mlx5e_priv *priv = netdev_priv(ndev);
	int i;

	memset(buf, 0, sizeof(u64) * MLX5E_ST_NUM);

	mutex_lock(&priv->state_lock);
	netdev_info(ndev, "Self test begin..\n");

	for (i = 0; i < MLX5E_ST_NUM; i++) {
		netdev_info(ndev, "\t[%d] %s start..\n",
			    i, mlx5e_self_tests[i]);
		buf[i] = mlx5e_st_func[i](priv);
		netdev_info(ndev, "\t[%d] %s end: result(%lld)\n",
			    i, mlx5e_self_tests[i], buf[i]);
	}

	mutex_unlock(&priv->state_lock);

	for (i = 0; i < MLX5E_ST_NUM; i++) {
		if (buf[i]) {
			etest->flags |= ETH_TEST_FL_FAILED;
			break;
		}
	}
	netdev_info(ndev, "Self test out: status flags(0x%x)\n",
		    etest->flags);
}
