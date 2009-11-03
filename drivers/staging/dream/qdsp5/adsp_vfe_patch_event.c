/* arch/arm/mach-msm/qdsp5/adsp_vfe_patch_event.c
 *
 * Verification code for aDSP VFE packets from userspace.
 *
 * Copyright (c) 2008 QUALCOMM Incorporated
 * Copyright (C) 2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <mach/qdsp5/qdsp5vfemsg.h>
#include "adsp.h"

static int patch_op_event(struct msm_adsp_module *module,
				struct adsp_event *event)
{
	vfe_msg_op1 *op = (vfe_msg_op1 *)event->data.msg16;
	if (adsp_pmem_paddr_fixup(module, (void **)&op->op1_buf_y_addr) ||
	    adsp_pmem_paddr_fixup(module, (void **)&op->op1_buf_cbcr_addr))
		return -1;
	return 0;
}

static int patch_af_wb_event(struct msm_adsp_module *module,
				struct adsp_event *event)
{
	vfe_msg_stats_wb_exp *af = (vfe_msg_stats_wb_exp *)event->data.msg16;
	return adsp_pmem_paddr_fixup(module, (void **)&af->wb_exp_stats_op_buf);
}

int adsp_vfe_patch_event(struct msm_adsp_module *module,
			struct adsp_event *event)
{
	switch(event->msg_id) {
	case VFE_MSG_OP1:
	case VFE_MSG_OP2:
		return patch_op_event(module, event);
	case VFE_MSG_STATS_AF:
	case VFE_MSG_STATS_WB_EXP:
		return patch_af_wb_event(module, event);
	default:
		break;
	}

	return 0;
}
