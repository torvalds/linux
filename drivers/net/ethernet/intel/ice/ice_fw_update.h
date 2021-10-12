/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2019, Intel Corporation. */

#ifndef _ICE_FW_UPDATE_H_
#define _ICE_FW_UPDATE_H_

int ice_flash_pldm_image(struct ice_pf *pf, const struct firmware *fw,
			 u8 preservation, struct netlink_ext_ack *extack);
int ice_cancel_pending_update(struct ice_pf *pf, const char *component,
			      struct netlink_ext_ack *extack);

#endif
