/*
 * Copyright 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __AMDGPU_VPE_H__
#define __AMDGPU_VPE_H__

#include "amdgpu_ring.h"
#include "amdgpu_irq.h"
#include "vpe_6_1_fw_if.h"

struct amdgpu_vpe;

struct vpe_funcs {
	uint32_t (*get_reg_offset)(struct amdgpu_vpe *vpe, uint32_t inst, uint32_t offset);
	int (*set_regs)(struct amdgpu_vpe *vpe);
	int (*irq_init)(struct amdgpu_vpe *vpe);
	int (*init_microcode)(struct amdgpu_vpe *vpe);
	int (*load_microcode)(struct amdgpu_vpe *vpe);
	int (*ring_init)(struct amdgpu_vpe *vpe);
	int (*ring_start)(struct amdgpu_vpe *vpe);
	int (*ring_stop)(struct amdgpu_vpe *vpe);
	int (*ring_fini)(struct amdgpu_vpe *vpe);
};

struct vpe_regs {
	uint32_t queue0_rb_rptr_lo;
	uint32_t queue0_rb_rptr_hi;
	uint32_t queue0_rb_wptr_lo;
	uint32_t queue0_rb_wptr_hi;
	uint32_t queue0_preempt;

	uint32_t dpm_enable;
	uint32_t dpm_pratio;
	uint32_t dpm_request_interval;
	uint32_t dpm_decision_threshold;
	uint32_t dpm_busy_clamp_threshold;
	uint32_t dpm_idle_clamp_threshold;
	uint32_t dpm_request_lv;
	uint32_t context_indicator;
};

struct amdgpu_vpe {
	struct amdgpu_ring		ring;
	struct amdgpu_irq_src		trap_irq;

	const struct vpe_funcs		*funcs;
	struct vpe_regs			regs;

	const struct firmware		*fw;
	uint32_t			fw_version;
	uint32_t			feature_version;

	struct amdgpu_bo		*cmdbuf_obj;
	uint64_t			cmdbuf_gpu_addr;
	uint32_t			*cmdbuf_cpu_addr;
	struct delayed_work		idle_work;
	bool				context_started;
};

int amdgpu_vpe_psp_update_sram(struct amdgpu_device *adev);
int amdgpu_vpe_init_microcode(struct amdgpu_vpe *vpe);
int amdgpu_vpe_ring_init(struct amdgpu_vpe *vpe);
int amdgpu_vpe_ring_fini(struct amdgpu_vpe *vpe);
int amdgpu_vpe_configure_dpm(struct amdgpu_vpe *vpe);

#define vpe_ring_init(vpe) ((vpe)->funcs->ring_init ? (vpe)->funcs->ring_init((vpe)) : 0)
#define vpe_ring_start(vpe) ((vpe)->funcs->ring_start ? (vpe)->funcs->ring_start((vpe)) : 0)
#define vpe_ring_stop(vpe) ((vpe)->funcs->ring_stop ? (vpe)->funcs->ring_stop((vpe)) : 0)
#define vpe_ring_fini(vpe) ((vpe)->funcs->ring_fini ? (vpe)->funcs->ring_fini((vpe)) : 0)

#define vpe_get_reg_offset(vpe, inst, offset) \
		((vpe)->funcs->get_reg_offset ? (vpe)->funcs->get_reg_offset((vpe), (inst), (offset)) : 0)
#define vpe_set_regs(vpe) \
		((vpe)->funcs->set_regs ? (vpe)->funcs->set_regs((vpe)) : 0)
#define vpe_irq_init(vpe) \
		((vpe)->funcs->irq_init ? (vpe)->funcs->irq_init((vpe)) : 0)
#define vpe_init_microcode(vpe) \
		((vpe)->funcs->init_microcode ? (vpe)->funcs->init_microcode((vpe)) : 0)
#define vpe_load_microcode(vpe) \
		((vpe)->funcs->load_microcode ? (vpe)->funcs->load_microcode((vpe)) : 0)

extern const struct amdgpu_ip_block_version vpe_v6_1_ip_block;

#endif
