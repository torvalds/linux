/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020-2021 NXP
 */

#ifndef _AMPHION_VPU_CORE_H
#define _AMPHION_VPU_CORE_H

void csr_writel(struct vpu_core *core, u32 reg, u32 val);
u32 csr_readl(struct vpu_core *core, u32 reg);
int vpu_alloc_dma(struct vpu_core *core, struct vpu_buffer *buf);
void vpu_free_dma(struct vpu_buffer *buf);
struct vpu_inst *vpu_core_find_instance(struct vpu_core *core, u32 index);
void vpu_core_set_state(struct vpu_core *core, enum vpu_core_state state);

#endif
