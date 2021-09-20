// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021, Intel Corporation. */

#include <net/xdp_sock_drv.h>

#include "stmmac.h"
#include "stmmac_xdp.h"

static int stmmac_xdp_enable_pool(struct stmmac_priv *priv,
				  struct xsk_buff_pool *pool, u16 queue)
{
	struct stmmac_channel *ch = &priv->channel[queue];
	bool need_update;
	u32 frame_size;
	int err;

	if (queue >= priv->plat->rx_queues_to_use ||
	    queue >= priv->plat->tx_queues_to_use)
		return -EINVAL;

	frame_size = xsk_pool_get_rx_frame_size(pool);
	/* XDP ZC does not span multiple frame, make sure XSK pool buffer
	 * size can at least store Q-in-Q frame.
	 */
	if (frame_size < ETH_FRAME_LEN + VLAN_HLEN * 2)
		return -EOPNOTSUPP;

	err = xsk_pool_dma_map(pool, priv->device, STMMAC_RX_DMA_ATTR);
	if (err) {
		netdev_err(priv->dev, "Failed to map xsk pool\n");
		return err;
	}

	need_update = netif_running(priv->dev) && stmmac_xdp_is_enabled(priv);

	if (need_update) {
		napi_disable(&ch->rx_napi);
		napi_disable(&ch->tx_napi);
		stmmac_disable_rx_queue(priv, queue);
		stmmac_disable_tx_queue(priv, queue);
	}

	set_bit(queue, priv->af_xdp_zc_qps);

	if (need_update) {
		stmmac_enable_rx_queue(priv, queue);
		stmmac_enable_tx_queue(priv, queue);
		napi_enable(&ch->rxtx_napi);

		err = stmmac_xsk_wakeup(priv->dev, queue, XDP_WAKEUP_RX);
		if (err)
			return err;
	}

	return 0;
}

static int stmmac_xdp_disable_pool(struct stmmac_priv *priv, u16 queue)
{
	struct stmmac_channel *ch = &priv->channel[queue];
	struct xsk_buff_pool *pool;
	bool need_update;

	if (queue >= priv->plat->rx_queues_to_use ||
	    queue >= priv->plat->tx_queues_to_use)
		return -EINVAL;

	pool = xsk_get_pool_from_qid(priv->dev, queue);
	if (!pool)
		return -EINVAL;

	need_update = netif_running(priv->dev) && stmmac_xdp_is_enabled(priv);

	if (need_update) {
		napi_disable(&ch->rxtx_napi);
		stmmac_disable_rx_queue(priv, queue);
		stmmac_disable_tx_queue(priv, queue);
		synchronize_rcu();
	}

	xsk_pool_dma_unmap(pool, STMMAC_RX_DMA_ATTR);

	clear_bit(queue, priv->af_xdp_zc_qps);

	if (need_update) {
		stmmac_enable_rx_queue(priv, queue);
		stmmac_enable_tx_queue(priv, queue);
		napi_enable(&ch->rx_napi);
		napi_enable(&ch->tx_napi);
	}

	return 0;
}

int stmmac_xdp_setup_pool(struct stmmac_priv *priv, struct xsk_buff_pool *pool,
			  u16 queue)
{
	return pool ? stmmac_xdp_enable_pool(priv, pool, queue) :
		      stmmac_xdp_disable_pool(priv, queue);
}

int stmmac_xdp_set_prog(struct stmmac_priv *priv, struct bpf_prog *prog,
			struct netlink_ext_ack *extack)
{
	struct net_device *dev = priv->dev;
	struct bpf_prog *old_prog;
	bool need_update;
	bool if_running;

	if_running = netif_running(dev);

	if (prog && dev->mtu > ETH_DATA_LEN) {
		/* For now, the driver doesn't support XDP functionality with
		 * jumbo frames so we return error.
		 */
		NL_SET_ERR_MSG_MOD(extack, "Jumbo frames not supported");
		return -EOPNOTSUPP;
	}

	need_update = !!priv->xdp_prog != !!prog;
	if (if_running && need_update)
		stmmac_release(dev);

	old_prog = xchg(&priv->xdp_prog, prog);
	if (old_prog)
		bpf_prog_put(old_prog);

	/* Disable RX SPH for XDP operation */
	priv->sph = priv->sph_cap && !stmmac_xdp_is_enabled(priv);

	if (if_running && need_update)
		stmmac_open(dev);

	return 0;
}
