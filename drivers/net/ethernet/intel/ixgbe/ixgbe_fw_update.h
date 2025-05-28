/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2025 Intel Corporation. */

#ifndef _IXGBE_FW_UPDATE_H_
#define _IXGBE_FW_UPDATE_H_

int ixgbe_flash_pldm_image(struct devlink *devlink,
			   struct devlink_flash_update_params *params,
			   struct netlink_ext_ack *extack);
int ixgbe_get_pending_updates(struct ixgbe_adapter *adapter, u8 *pending,
			      struct netlink_ext_ack *extack);
#endif
