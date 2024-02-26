/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025, Intel Corporation. */

#ifndef _IXGBE_DEVLINK_H_
#define _IXGBE_DEVLINK_H_

int ixgbe_allocate_devlink(struct ixgbe_adapter *adapter);
int ixgbe_devlink_register_port(struct ixgbe_adapter *adapter);

#endif /* _IXGBE_DEVLINK_H_ */
