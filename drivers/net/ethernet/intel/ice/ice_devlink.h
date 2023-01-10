/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_DEVLINK_H_
#define _ICE_DEVLINK_H_

struct ice_pf *ice_allocate_pf(struct device *dev);

void ice_devlink_register(struct ice_pf *pf);
void ice_devlink_unregister(struct ice_pf *pf);
int ice_devlink_register_params(struct ice_pf *pf);
void ice_devlink_unregister_params(struct ice_pf *pf);
int ice_devlink_create_pf_port(struct ice_pf *pf);
void ice_devlink_destroy_pf_port(struct ice_pf *pf);
int ice_devlink_create_vf_port(struct ice_vf *vf);
void ice_devlink_destroy_vf_port(struct ice_vf *vf);

void ice_devlink_init_regions(struct ice_pf *pf);
void ice_devlink_destroy_regions(struct ice_pf *pf);

int ice_devlink_rate_init_tx_topology(struct devlink *devlink, struct ice_vsi *vsi);
void ice_tear_down_devlink_rate_tree(struct ice_pf *pf);

#endif /* _ICE_DEVLINK_H_ */
