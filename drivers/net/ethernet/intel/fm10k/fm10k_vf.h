/* Intel Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#ifndef _FM10K_VF_H_
#define _FM10K_VF_H_

#include "fm10k_type.h"
#include "fm10k_common.h"

enum fm10k_vf_tlv_msg_id {
	FM10K_VF_MSG_ID_TEST = 0,	/* msg ID reserved for testing */
	FM10K_VF_MSG_ID_MSIX,
	FM10K_VF_MSG_ID_MAC_VLAN,
	FM10K_VF_MSG_ID_LPORT_STATE,
	FM10K_VF_MSG_ID_MAX,
};

enum fm10k_tlv_mac_vlan_attr_id {
	FM10K_MAC_VLAN_MSG_VLAN,
	FM10K_MAC_VLAN_MSG_SET,
	FM10K_MAC_VLAN_MSG_MAC,
	FM10K_MAC_VLAN_MSG_DEFAULT_MAC,
	FM10K_MAC_VLAN_MSG_MULTICAST,
	FM10K_MAC_VLAN_MSG_ID_MAX
};

enum fm10k_tlv_lport_state_attr_id {
	FM10K_LPORT_STATE_MSG_DISABLE,
	FM10K_LPORT_STATE_MSG_XCAST_MODE,
	FM10K_LPORT_STATE_MSG_READY,
	FM10K_LPORT_STATE_MSG_MAX
};

#define FM10K_VF_MSG_MSIX_HANDLER(func) \
	 FM10K_MSG_HANDLER(FM10K_VF_MSG_ID_MSIX, NULL, func)

s32 fm10k_msg_mac_vlan_vf(struct fm10k_hw *, u32 **, struct fm10k_mbx_info *);
extern const struct fm10k_tlv_attr fm10k_mac_vlan_msg_attr[];
#define FM10K_VF_MSG_MAC_VLAN_HANDLER(func) \
	FM10K_MSG_HANDLER(FM10K_VF_MSG_ID_MAC_VLAN, \
			  fm10k_mac_vlan_msg_attr, func)

s32 fm10k_msg_lport_state_vf(struct fm10k_hw *, u32 **,
			     struct fm10k_mbx_info *);
extern const struct fm10k_tlv_attr fm10k_lport_state_msg_attr[];
#define FM10K_VF_MSG_LPORT_STATE_HANDLER(func) \
	FM10K_MSG_HANDLER(FM10K_VF_MSG_ID_LPORT_STATE, \
			  fm10k_lport_state_msg_attr, func)

extern struct fm10k_info fm10k_vf_info;
#endif /* _FM10K_VF_H */
