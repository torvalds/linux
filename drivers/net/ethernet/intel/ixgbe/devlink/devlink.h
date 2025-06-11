/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025, Intel Corporation. */

#ifndef _IXGBE_DEVLINK_H_
#define _IXGBE_DEVLINK_H_

struct ixgbe_adapter *ixgbe_allocate_devlink(struct device *dev);
int ixgbe_devlink_register_port(struct ixgbe_adapter *adapter);
void ixgbe_devlink_init_regions(struct ixgbe_adapter *adapter);
void ixgbe_devlink_destroy_regions(struct ixgbe_adapter *adapter);

#endif /* _IXGBE_DEVLINK_H_ */
