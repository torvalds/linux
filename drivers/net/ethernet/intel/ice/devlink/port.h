/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024, Intel Corporation. */

#ifndef _DEVLINK_PORT_H_
#define _DEVLINK_PORT_H_

#include "../ice.h"
#include "../ice_sf_eth.h"

/**
 * struct ice_dynamic_port - Track dynamically added devlink port instance
 * @hw_addr: the HW address for this port
 * @active: true if the port has been activated
 * @attached: true it the prot is attached
 * @devlink_port: the associated devlink port structure
 * @pf: pointer to the PF private structure
 * @vsi: the VSI associated with this port
 * @repr_id: the representor ID
 * @sfnum: the subfunction ID
 * @sf_dev: pointer to the subfunction device
 *
 * An instance of a dynamically added devlink port. Each port flavour
 */
struct ice_dynamic_port {
	u8 hw_addr[ETH_ALEN];
	u8 active: 1;
	u8 attached: 1;
	struct devlink_port devlink_port;
	struct ice_pf *pf;
	struct ice_vsi *vsi;
	unsigned long repr_id;
	u32 sfnum;
	/* Flavour-specific implementation data */
	union {
		struct ice_sf_dev *sf_dev;
	};
};

void ice_dealloc_all_dynamic_ports(struct ice_pf *pf);

int ice_devlink_create_pf_port(struct ice_pf *pf);
void ice_devlink_destroy_pf_port(struct ice_pf *pf);
int ice_devlink_create_vf_port(struct ice_vf *vf);
void ice_devlink_destroy_vf_port(struct ice_vf *vf);
int ice_devlink_create_sf_port(struct ice_dynamic_port *dyn_port);
void ice_devlink_destroy_sf_port(struct ice_dynamic_port *dyn_port);
int ice_devlink_create_sf_dev_port(struct ice_sf_dev *sf_dev);
void ice_devlink_destroy_sf_dev_port(struct ice_sf_dev *sf_dev);

#define ice_devlink_port_to_dyn(port) \
	container_of(port, struct ice_dynamic_port, devlink_port)

int
ice_devlink_port_new(struct devlink *devlink,
		     const struct devlink_port_new_attrs *new_attr,
		     struct netlink_ext_ack *extack,
		     struct devlink_port **devlink_port);
#endif /* _DEVLINK_PORT_H_ */
