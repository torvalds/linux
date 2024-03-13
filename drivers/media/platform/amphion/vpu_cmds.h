/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020-2021 NXP
 */

#ifndef _AMPHION_VPU_CMDS_H
#define _AMPHION_VPU_CMDS_H

int vpu_session_configure_codec(struct vpu_inst *inst);
int vpu_session_start(struct vpu_inst *inst);
int vpu_session_stop(struct vpu_inst *inst);
int vpu_session_abort(struct vpu_inst *inst);
int vpu_session_rst_buf(struct vpu_inst *inst);
int vpu_session_encode_frame(struct vpu_inst *inst, s64 timestamp);
int vpu_session_alloc_fs(struct vpu_inst *inst, struct vpu_fs_info *fs);
int vpu_session_release_fs(struct vpu_inst *inst, struct vpu_fs_info *fs);
int vpu_session_fill_timestamp(struct vpu_inst *inst, struct vpu_ts_info *info);
int vpu_session_update_parameters(struct vpu_inst *inst, void *arg);
int vpu_core_snapshot(struct vpu_core *core);
int vpu_core_sw_reset(struct vpu_core *core);
int vpu_response_cmd(struct vpu_inst *inst, u32 response, u32 handled);
void vpu_clear_request(struct vpu_inst *inst);
int vpu_session_debug(struct vpu_inst *inst);

#endif
