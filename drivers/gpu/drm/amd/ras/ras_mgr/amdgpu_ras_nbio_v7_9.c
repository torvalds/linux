// SPDX-License-Identifier: MIT
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
 *
 */

#include "amdgpu_ras_mgr.h"
#include "amdgpu_ras_nbio_v7_9.h"
#include "nbio/nbio_7_9_0_offset.h"
#include "nbio/nbio_7_9_0_sh_mask.h"
#include "ivsrcid/nbio/irqsrcs_nbif_7_4.h"

static int nbio_v7_9_set_ras_controller_irq_state(struct amdgpu_device *adev,
						  struct amdgpu_irq_src *src,
						  unsigned int type,
						  enum amdgpu_interrupt_state state)
{
	/* Dummy function, there is no initialization operation in driver */

	return 0;
}

static int nbio_v7_9_process_ras_controller_irq(struct amdgpu_device *adev,
						struct amdgpu_irq_src *source,
						struct amdgpu_iv_entry *entry)
{
	/* By design, the ih cookie for ras_controller_irq should be written
	 * to BIFring instead of general iv ring. However, due to known bif ring
	 * hw bug, it has to be disabled. There is no chance the process function
	 * will be involked. Just left it as a dummy one.
	 */
	return 0;
}

static int nbio_v7_9_set_ras_err_event_athub_irq_state(struct amdgpu_device *adev,
						       struct amdgpu_irq_src *src,
						       unsigned int type,
						       enum amdgpu_interrupt_state state)
{
	/* Dummy function, there is no initialization operation in driver */

	return 0;
}

static int nbio_v7_9_process_err_event_athub_irq(struct amdgpu_device *adev,
						 struct amdgpu_irq_src *source,
						 struct amdgpu_iv_entry *entry)
{
	/* By design, the ih cookie for err_event_athub_irq should be written
	 * to BIFring instead of general iv ring. However, due to known bif ring
	 * hw bug, it has to be disabled. There is no chance the process function
	 * will be involked. Just left it as a dummy one.
	 */
	return 0;
}

static const struct amdgpu_irq_src_funcs nbio_v7_9_ras_controller_irq_funcs = {
	.set = nbio_v7_9_set_ras_controller_irq_state,
	.process = nbio_v7_9_process_ras_controller_irq,
};

static const struct amdgpu_irq_src_funcs nbio_v7_9_ras_err_event_athub_irq_funcs = {
	.set = nbio_v7_9_set_ras_err_event_athub_irq_state,
	.process = nbio_v7_9_process_err_event_athub_irq,
};

static int nbio_v7_9_init_ras_controller_interrupt(struct ras_core_context *ras_core, bool state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	int r;

	/* init the irq funcs */
	adev->nbio.ras_controller_irq.funcs =
		&nbio_v7_9_ras_controller_irq_funcs;
	adev->nbio.ras_controller_irq.num_types = 1;

	/* register ras controller interrupt */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_BIF,
			      NBIF_7_4__SRCID__RAS_CONTROLLER_INTERRUPT,
			      &adev->nbio.ras_controller_irq);

	return r;
}

static int nbio_v7_9_init_ras_err_event_athub_interrupt(struct ras_core_context *ras_core,
			bool state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	int r;

	/* init the irq funcs */
	adev->nbio.ras_err_event_athub_irq.funcs =
		&nbio_v7_9_ras_err_event_athub_irq_funcs;
	adev->nbio.ras_err_event_athub_irq.num_types = 1;

	/* register ras err event athub interrupt */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_BIF,
			      NBIF_7_4__SRCID__ERREVENT_ATHUB_INTERRUPT,
			      &adev->nbio.ras_err_event_athub_irq);

	return r;
}

const struct ras_nbio_sys_func amdgpu_ras_nbio_sys_func_v7_9 = {
	.set_ras_controller_irq_state = nbio_v7_9_init_ras_controller_interrupt,
	.set_ras_err_event_athub_irq_state = nbio_v7_9_init_ras_err_event_athub_interrupt,
};
