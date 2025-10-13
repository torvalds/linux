/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2022, Intel Corporation. */

#ifndef _ICE_VIRT_RSS_H_
#define _ICE_VIRT_RSS_H_

#include <linux/types.h>

struct ice_vf;

int ice_vc_handle_rss_cfg(struct ice_vf *vf, u8 *msg, bool add);
int ice_vc_config_rss_key(struct ice_vf *vf, u8 *msg);
int ice_vc_config_rss_lut(struct ice_vf *vf, u8 *msg);
int ice_vc_config_rss_hfunc(struct ice_vf *vf, u8 *msg);
int ice_vc_get_rss_hashcfg(struct ice_vf *vf);
int ice_vc_set_rss_hashcfg(struct ice_vf *vf, u8 *msg);

#endif /* _ICE_VIRT_RSS_H_ */
