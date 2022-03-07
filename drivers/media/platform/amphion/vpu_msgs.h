/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020-2021 NXP
 */

#ifndef _AMPHION_VPU_MSGS_H
#define _AMPHION_VPU_MSGS_H

int vpu_isr(struct vpu_core *core, u32 irq);
void vpu_inst_run_work(struct work_struct *work);
void vpu_msg_run_work(struct work_struct *work);
void vpu_msg_delayed_work(struct work_struct *work);

#endif
