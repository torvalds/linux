// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.
#include <linux/pci.h>

#include "bng_res.h"
#include "bng_fw.h"

void bng_re_free_rcfw_channel(struct bng_re_rcfw *rcfw)
{
	kfree(rcfw->crsqe_tbl);
	bng_re_free_hwq(rcfw->res, &rcfw->cmdq.hwq);
	bng_re_free_hwq(rcfw->res, &rcfw->creq.hwq);
	rcfw->pdev = NULL;
}

int bng_re_alloc_fw_channel(struct bng_re_res *res,
			    struct bng_re_rcfw *rcfw)
{
	struct bng_re_hwq_attr hwq_attr = {};
	struct bng_re_sg_info sginfo = {};
	struct bng_re_cmdq_ctx *cmdq;
	struct bng_re_creq_ctx *creq;

	rcfw->pdev = res->pdev;
	cmdq = &rcfw->cmdq;
	creq = &rcfw->creq;
	rcfw->res = res;

	sginfo.pgsize = PAGE_SIZE;
	sginfo.pgshft = PAGE_SHIFT;

	hwq_attr.sginfo = &sginfo;
	hwq_attr.res = rcfw->res;
	hwq_attr.depth = BNG_FW_CREQE_MAX_CNT;
	hwq_attr.stride = BNG_FW_CREQE_UNITS;
	hwq_attr.type = BNG_HWQ_TYPE_QUEUE;

	if (bng_re_alloc_init_hwq(&creq->hwq, &hwq_attr)) {
		dev_err(&rcfw->pdev->dev,
			"HW channel CREQ allocation failed\n");
		goto fail;
	}

	rcfw->cmdq_depth = BNG_FW_CMDQE_MAX_CNT;

	sginfo.pgsize = bng_fw_cmdqe_page_size(rcfw->cmdq_depth);
	hwq_attr.depth = rcfw->cmdq_depth & 0x7FFFFFFF;
	hwq_attr.stride = BNG_FW_CMDQE_UNITS;
	hwq_attr.type = BNG_HWQ_TYPE_CTX;
	if (bng_re_alloc_init_hwq(&cmdq->hwq, &hwq_attr)) {
		dev_err(&rcfw->pdev->dev,
			"HW channel CMDQ allocation failed\n");
		goto fail;
	}

	rcfw->crsqe_tbl = kcalloc(cmdq->hwq.max_elements,
				  sizeof(*rcfw->crsqe_tbl), GFP_KERNEL);
	if (!rcfw->crsqe_tbl)
		goto fail;

	spin_lock_init(&rcfw->tbl_lock);

	rcfw->max_timeout = res->cctx->hwrm_cmd_max_timeout;

	return 0;

fail:
	bng_re_free_rcfw_channel(rcfw);
	return -ENOMEM;
}
