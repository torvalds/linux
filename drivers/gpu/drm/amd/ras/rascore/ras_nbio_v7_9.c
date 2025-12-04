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

#include "ras.h"
#include "ras_nbio_v7_9.h"

#define BIF_BX0_BIF_DOORBELL_INT_CNTL__RAS_ATHUB_ERR_EVENT_INTERRUPT_CLEAR__SHIFT      0x12
#define BIF_BX0_BIF_DOORBELL_INT_CNTL__RAS_ATHUB_ERR_EVENT_INTERRUPT_CLEAR_MASK        0x00040000L
#define BIF_BX0_BIF_DOORBELL_INT_CNTL__RAS_ATHUB_ERR_EVENT_INTERRUPT_STATUS__SHIFT     0x2
#define BIF_BX0_BIF_DOORBELL_INT_CNTL__RAS_ATHUB_ERR_EVENT_INTERRUPT_STATUS_MASK       0x00000004L
#define BIF_BX0_BIF_DOORBELL_INT_CNTL__RAS_CNTLR_INTERRUPT_CLEAR__SHIFT                0x11
#define BIF_BX0_BIF_DOORBELL_INT_CNTL__RAS_CNTLR_INTERRUPT_CLEAR_MASK                  0x00020000L
#define BIF_BX0_BIF_DOORBELL_INT_CNTL__RAS_CNTLR_INTERRUPT_STATUS__SHIFT               0x1
#define BIF_BX0_BIF_DOORBELL_INT_CNTL__RAS_CNTLR_INTERRUPT_STATUS_MASK                 0x00000002L

#define regBIF_BX0_BIF_DOORBELL_INT_CNTL_BASE_IDX      2
#define regBIF_BX0_BIF_DOORBELL_INT_CNTL               0x00fe

#define regBIF_BX0_BIF_INTR_CNTL                                                       0x0101
#define regBIF_BX0_BIF_INTR_CNTL_BASE_IDX                                              2

/* BIF_BX0_BIF_INTR_CNTL */
#define BIF_BX0_BIF_INTR_CNTL__RAS_INTR_VEC_SEL__SHIFT                                 0x0
#define BIF_BX0_BIF_INTR_CNTL__RAS_INTR_VEC_SEL_MASK                                   0x00000001L

#define regBIF_BX_PF0_PARTITION_MEM_STATUS                                             0x0164
#define regBIF_BX_PF0_PARTITION_MEM_STATUS_BASE_IDX                                    2
/* BIF_BX_PF0_PARTITION_MEM_STATUS */
#define BIF_BX_PF0_PARTITION_MEM_STATUS__CHANGE_STATUE__SHIFT                          0x0
#define BIF_BX_PF0_PARTITION_MEM_STATUS__NPS_MODE__SHIFT                               0x4
#define BIF_BX_PF0_PARTITION_MEM_STATUS__CHANGE_STATUE_MASK                            0x0000000FL
#define BIF_BX_PF0_PARTITION_MEM_STATUS__NPS_MODE_MASK                                 0x00000FF0L


static int nbio_v7_9_handle_ras_controller_intr_no_bifring(struct ras_core_context *ras_core)
{
	uint32_t bif_doorbell_intr_cntl = 0;

	bif_doorbell_intr_cntl =
		RAS_DEV_RREG32_SOC15(ras_core->dev, NBIO, 0, regBIF_BX0_BIF_DOORBELL_INT_CNTL);

	if (REG_GET_FIELD(bif_doorbell_intr_cntl,
		BIF_BX0_BIF_DOORBELL_INT_CNTL, RAS_CNTLR_INTERRUPT_STATUS)) {
		/* driver has to clear the interrupt status when bif ring is disabled */
		bif_doorbell_intr_cntl = REG_SET_FIELD(bif_doorbell_intr_cntl,
						BIF_BX0_BIF_DOORBELL_INT_CNTL,
						RAS_CNTLR_INTERRUPT_CLEAR, 1);

		RAS_DEV_WREG32_SOC15(ras_core->dev,
			NBIO, 0, regBIF_BX0_BIF_DOORBELL_INT_CNTL, bif_doorbell_intr_cntl);

		/* TODO: handle ras controller interrupt */
	}

	return 0;
}

static int nbio_v7_9_handle_ras_err_event_athub_intr_no_bifring(struct ras_core_context *ras_core)
{
	uint32_t bif_doorbell_intr_cntl = 0;
	int ret = 0;

	bif_doorbell_intr_cntl =
		RAS_DEV_RREG32_SOC15(ras_core->dev, NBIO, 0, regBIF_BX0_BIF_DOORBELL_INT_CNTL);

	if (REG_GET_FIELD(bif_doorbell_intr_cntl,
		BIF_BX0_BIF_DOORBELL_INT_CNTL, RAS_ATHUB_ERR_EVENT_INTERRUPT_STATUS)) {
		/* driver has to clear the interrupt status when bif ring is disabled */
		bif_doorbell_intr_cntl = REG_SET_FIELD(bif_doorbell_intr_cntl,
						BIF_BX0_BIF_DOORBELL_INT_CNTL,
						RAS_ATHUB_ERR_EVENT_INTERRUPT_CLEAR, 1);

		RAS_DEV_WREG32_SOC15(ras_core->dev,
			NBIO, 0, regBIF_BX0_BIF_DOORBELL_INT_CNTL, bif_doorbell_intr_cntl);

		ret = ras_core_handle_fatal_error(ras_core);
	}

	return ret;
}

static uint32_t nbio_v7_9_get_memory_partition_mode(struct ras_core_context *ras_core)
{
	uint32_t mem_status;
	uint32_t mem_mode;

	mem_status =
		RAS_DEV_RREG32_SOC15(ras_core->dev, NBIO, 0, regBIF_BX_PF0_PARTITION_MEM_STATUS);

	/* Each bit represents a mode 1-8*/
	mem_mode = REG_GET_FIELD(mem_status, BIF_BX_PF0_PARTITION_MEM_STATUS, NPS_MODE);

	return ffs(mem_mode);
}

const struct ras_nbio_ip_func ras_nbio_v7_9 = {
	.handle_ras_controller_intr_no_bifring =
		nbio_v7_9_handle_ras_controller_intr_no_bifring,
	.handle_ras_err_event_athub_intr_no_bifring =
		nbio_v7_9_handle_ras_err_event_athub_intr_no_bifring,
	.get_memory_partition_mode = nbio_v7_9_get_memory_partition_mode,
};
