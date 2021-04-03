// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021, Intel Corporation. */

#include "stmmac.h"
#include "stmmac_xdp.h"

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
