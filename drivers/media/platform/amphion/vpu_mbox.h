/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020-2021 NXP
 */

#ifndef _AMPHION_VPU_MBOX_H
#define _AMPHION_VPU_MBOX_H

int vpu_mbox_init(struct vpu_core *core);
int vpu_mbox_request(struct vpu_core *core);
void vpu_mbox_free(struct vpu_core *core);
void vpu_mbox_send_msg(struct vpu_core *core, u32 type, u32 data);
void vpu_mbox_send_type(struct vpu_core *core, u32 type);
void vpu_mbox_enable_rx(struct vpu_dev *dev);

#endif
