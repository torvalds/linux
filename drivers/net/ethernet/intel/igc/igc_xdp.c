// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Intel Corporation. */

#include "igc.h"
#include "igc_xdp.h"

int igc_xdp_set_prog(struct igc_adapter *adapter, struct bpf_prog *prog,
		     struct netlink_ext_ack *extack)
{
	struct net_device *dev = adapter->netdev;
	bool if_running = netif_running(dev);
	struct bpf_prog *old_prog;

	if (dev->mtu > ETH_DATA_LEN) {
		/* For now, the driver doesn't support XDP functionality with
		 * jumbo frames so we return error.
		 */
		NL_SET_ERR_MSG_MOD(extack, "Jumbo frames not supported");
		return -EOPNOTSUPP;
	}

	if (if_running)
		igc_close(dev);

	old_prog = xchg(&adapter->xdp_prog, prog);
	if (old_prog)
		bpf_prog_put(old_prog);

	if (if_running)
		igc_open(dev);

	return 0;
}
