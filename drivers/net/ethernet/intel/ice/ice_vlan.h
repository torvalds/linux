/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019-2021, Intel Corporation. */

#ifndef _ICE_VLAN_H_
#define _ICE_VLAN_H_

#include <linux/types.h>
#include "ice_type.h"

struct ice_vlan {
	u16 tpid;
	u16 vid;
	u8 prio;
};

#define ICE_VLAN(tpid, vid, prio) ((struct ice_vlan){ tpid, vid, prio })

#endif /* _ICE_VLAN_H_ */
