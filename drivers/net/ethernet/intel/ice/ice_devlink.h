/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_DEVLINK_H_
#define _ICE_DEVLINK_H_

struct ice_pf *ice_allocate_pf(struct device *dev);

int ice_devlink_register(struct ice_pf *pf);
void ice_devlink_unregister(struct ice_pf *pf);
int ice_devlink_create_port(struct ice_pf *pf);
void ice_devlink_destroy_port(struct ice_pf *pf);

void ice_devlink_init_regions(struct ice_pf *pf);
void ice_devlink_destroy_regions(struct ice_pf *pf);

#endif /* _ICE_DEVLINK_H_ */
