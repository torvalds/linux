// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "abi/guc_actions_abi.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"
#include "xe_guc_pagefault.h"
#include "xe_pagefault.h"

static void guc_ack_fault(struct xe_pagefault *pf, int err)
{
	u32 vfid = FIELD_GET(PFD_VFID, pf->producer.msg[2]);
	u32 engine_instance = FIELD_GET(PFD_ENG_INSTANCE, pf->producer.msg[0]);
	u32 engine_class = FIELD_GET(PFD_ENG_CLASS, pf->producer.msg[0]);
	u32 pdata = FIELD_GET(PFD_PDATA_LO, pf->producer.msg[0]) |
		(FIELD_GET(PFD_PDATA_HI, pf->producer.msg[1]) <<
		 PFD_PDATA_HI_SHIFT);
	u32 action[] = {
		XE_GUC_ACTION_PAGE_FAULT_RES_DESC,

		FIELD_PREP(PFR_VALID, 1) |
		FIELD_PREP(PFR_SUCCESS, !!err) |
		FIELD_PREP(PFR_REPLY, PFR_ACCESS) |
		FIELD_PREP(PFR_DESC_TYPE, FAULT_RESPONSE_DESC) |
		FIELD_PREP(PFR_ASID, pf->consumer.asid),

		FIELD_PREP(PFR_VFID, vfid) |
		FIELD_PREP(PFR_ENG_INSTANCE, engine_instance) |
		FIELD_PREP(PFR_ENG_CLASS, engine_class) |
		FIELD_PREP(PFR_PDATA, pdata),
	};
	struct xe_guc *guc = pf->producer.private;

	xe_guc_ct_send(&guc->ct, action, ARRAY_SIZE(action), 0, 0);
}

static const struct xe_pagefault_ops guc_pagefault_ops = {
	.ack_fault = guc_ack_fault,
};

/**
 * xe_guc_pagefault_handler() - G2H page fault handler
 * @guc: GuC object
 * @msg: G2H message
 * @len: Length of G2H message
 *
 * Parse GuC to host (G2H) message into a struct xe_pagefault and forward onto
 * the Xe page fault layer.
 *
 * Return: 0 on success, errno on failure
 */
int xe_guc_pagefault_handler(struct xe_guc *guc, u32 *msg, u32 len)
{
	struct xe_pagefault pf;
	int i;

#define GUC_PF_MSG_LEN_DW	\
	(sizeof(struct xe_guc_pagefault_desc) / sizeof(u32))

	BUILD_BUG_ON(GUC_PF_MSG_LEN_DW > XE_PAGEFAULT_PRODUCER_MSG_LEN_DW);

	if (len != GUC_PF_MSG_LEN_DW)
		return -EPROTO;

	pf.gt = guc_to_gt(guc);

	/*
	 * XXX: These values happen to match the enum in xe_pagefault_types.h.
	 * If that changes, we’ll need to remap them here.
	 */
	pf.consumer.page_addr = ((u64)FIELD_GET(PFD_VIRTUAL_ADDR_HI, msg[3])
				      << PFD_VIRTUAL_ADDR_HI_SHIFT) |
		(FIELD_GET(PFD_VIRTUAL_ADDR_LO, msg[2]) <<
		 PFD_VIRTUAL_ADDR_LO_SHIFT);
	pf.consumer.asid = FIELD_GET(PFD_ASID, msg[1]);
	pf.consumer.access_type = FIELD_GET(PFD_ACCESS_TYPE, msg[2]);
	pf.consumer.fault_type = FIELD_GET(PFD_FAULT_TYPE, msg[2]);
	if (FIELD_GET(XE2_PFD_TRVA_FAULT, msg[0]))
		pf.consumer.fault_level = XE_PAGEFAULT_LEVEL_NACK;
	else
		pf.consumer.fault_level = FIELD_GET(PFD_FAULT_LEVEL, msg[0]);
	pf.consumer.engine_class = FIELD_GET(PFD_ENG_CLASS, msg[0]);
	pf.consumer.engine_instance = FIELD_GET(PFD_ENG_INSTANCE, msg[0]);

	pf.producer.private = guc;
	pf.producer.ops = &guc_pagefault_ops;
	for (i = 0; i < GUC_PF_MSG_LEN_DW; ++i)
		pf.producer.msg[i] = msg[i];

#undef GUC_PF_MSG_LEN_DW

	return xe_pagefault_handler(guc_to_xe(guc), &pf);
}
