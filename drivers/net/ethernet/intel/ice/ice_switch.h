/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_SWITCH_H_
#define _ICE_SWITCH_H_

#include "ice_common.h"

#define ICE_SW_CFG_MAX_BUF_LEN 2048
#define ICE_DFLT_VSI_INVAL 0xff

enum ice_status ice_get_initial_sw_cfg(struct ice_hw *hw);

#endif /* _ICE_SWITCH_H_ */
