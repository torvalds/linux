/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020, Intel Corporation. */

#ifndef _IGC_XDP_H_
#define _IGC_XDP_H_

int igc_xdp_set_prog(struct igc_adapter *adapter, struct bpf_prog *prog,
		     struct netlink_ext_ack *extack);

int igc_xdp_register_rxq_info(struct igc_ring *ring);
void igc_xdp_unregister_rxq_info(struct igc_ring *ring);

static inline bool igc_xdp_is_enabled(struct igc_adapter *adapter)
{
	return !!adapter->xdp_prog;
}

#endif /* _IGC_XDP_H_ */
