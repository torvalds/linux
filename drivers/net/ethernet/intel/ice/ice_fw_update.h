/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2019, Intel Corporation. */

#ifndef _ICE_FW_UPDATE_H_
#define _ICE_FW_UPDATE_H_

int ice_devlink_flash_update(struct devlink *devlink,
			     struct devlink_flash_update_params *params,
			     struct netlink_ext_ack *extack);
int ice_get_pending_updates(struct ice_pf *pf, u8 *pending,
			    struct netlink_ext_ack *extack);
int ice_write_one_nvm_block(struct ice_pf *pf, u16 module, u32 offset,
			    u16 block_size, u8 *block, bool last_cmd,
			    u8 *reset_level, struct netlink_ext_ack *extack);

#endif
