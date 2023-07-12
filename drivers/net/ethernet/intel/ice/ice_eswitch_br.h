/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023, Intel Corporation. */

#ifndef _ICE_ESWITCH_BR_H_
#define _ICE_ESWITCH_BR_H_

enum ice_esw_br_port_type {
	ICE_ESWITCH_BR_UPLINK_PORT = 0,
	ICE_ESWITCH_BR_VF_REPR_PORT = 1,
};

struct ice_esw_br_port {
	struct ice_esw_br *bridge;
	struct ice_vsi *vsi;
	enum ice_esw_br_port_type type;
	u16 vsi_idx;
};

struct ice_esw_br {
	struct ice_esw_br_offloads *br_offloads;
	struct xarray ports;

	int ifindex;
};

struct ice_esw_br_offloads {
	struct ice_pf *pf;
	struct ice_esw_br *bridge;
	struct notifier_block netdev_nb;
};

#define ice_nb_to_br_offloads(nb, nb_name) \
	container_of(nb, \
		     struct ice_esw_br_offloads, \
		     nb_name)

void
ice_eswitch_br_offloads_deinit(struct ice_pf *pf);
int
ice_eswitch_br_offloads_init(struct ice_pf *pf);

#endif /* _ICE_ESWITCH_BR_H_ */
