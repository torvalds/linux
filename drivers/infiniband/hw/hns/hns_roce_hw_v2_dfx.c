// SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause)
// Copyright (c) 2019 Hisilicon Limited.

#include "hnae3.h"
#include "hns_roce_device.h"
#include "hns_roce_cmd.h"
#include "hns_roce_hw_v2.h"

int hns_roce_v2_query_cqc_info(struct hns_roce_dev *hr_dev, u32 cqn,
			       int *buffer)
{
	struct hns_roce_v2_cq_context *cq_context;
	struct hns_roce_cmd_mailbox *mailbox;
	int ret;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	cq_context = mailbox->buf;
	ret = hns_roce_cmd_mbox(hr_dev, 0, mailbox->dma, cqn, 0,
				HNS_ROCE_CMD_QUERY_CQC,
				HNS_ROCE_CMD_TIMEOUT_MSECS);
	if (ret) {
		dev_err(hr_dev->dev, "QUERY cqc cmd process error\n");
		goto err_mailbox;
	}

	memcpy(buffer, cq_context, sizeof(*cq_context));

err_mailbox:
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);

	return ret;
}
