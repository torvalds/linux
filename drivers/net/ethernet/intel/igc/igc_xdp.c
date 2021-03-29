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

int igc_xdp_register_rxq_info(struct igc_ring *ring)
{
	struct net_device *dev = ring->netdev;
	int err;

	err = xdp_rxq_info_reg(&ring->xdp_rxq, dev, ring->queue_index, 0);
	if (err) {
		netdev_err(dev, "Failed to register xdp rxq info\n");
		return err;
	}

	err = xdp_rxq_info_reg_mem_model(&ring->xdp_rxq, MEM_TYPE_PAGE_SHARED,
					 NULL);
	if (err) {
		netdev_err(dev, "Failed to register xdp rxq mem model\n");
		xdp_rxq_info_unreg(&ring->xdp_rxq);
		return err;
	}

	return 0;
}

void igc_xdp_unregister_rxq_info(struct igc_ring *ring)
{
	xdp_rxq_info_unreg(&ring->xdp_rxq);
}
