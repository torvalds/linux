/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2022, Intel Corporation. */

#ifndef _ICE_VIRT_QUEUES_H_
#define _ICE_VIRT_QUEUES_H_

#include <linux/types.h>

struct ice_vf;

u16 ice_vc_get_max_frame_size(struct ice_vf *vf);
int ice_vc_ena_qs_msg(struct ice_vf *vf, u8 *msg);
int ice_vc_dis_qs_msg(struct ice_vf *vf, u8 *msg);
int ice_vc_cfg_irq_map_msg(struct ice_vf *vf, u8 *msg);
int ice_vc_cfg_q_bw(struct ice_vf *vf, u8 *msg);
int ice_vc_cfg_q_quanta(struct ice_vf *vf, u8 *msg);
int ice_vc_cfg_qs_msg(struct ice_vf *vf, u8 *msg);
int ice_vc_request_qs_msg(struct ice_vf *vf, u8 *msg);

#endif /* _ICE_VIRT_QUEUES_H_ */
