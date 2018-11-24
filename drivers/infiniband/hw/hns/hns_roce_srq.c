// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2018 Hisilicon Limited.
 */

#include <rdma/ib_umem.h>
#include <rdma/hns-abi.h>
#include "hns_roce_device.h"
#include "hns_roce_cmd.h"
#include "hns_roce_hem.h"

int hns_roce_init_srq_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_srq_table *srq_table = &hr_dev->srq_table;

	xa_init(&srq_table->xa);

	return hns_roce_bitmap_init(&srq_table->bitmap, hr_dev->caps.num_srqs,
				    hr_dev->caps.num_srqs - 1,
				    hr_dev->caps.reserved_srqs, 0);
}

void hns_roce_cleanup_srq_table(struct hns_roce_dev *hr_dev)
{
	hns_roce_bitmap_cleanup(&hr_dev->srq_table.bitmap);
}
