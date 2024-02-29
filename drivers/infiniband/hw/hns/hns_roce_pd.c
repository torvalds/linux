/*
 * Copyright (c) 2016 Hisilicon Limited.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/pci.h>
#include "hns_roce_device.h"

void hns_roce_init_pd_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_ida *pd_ida = &hr_dev->pd_ida;

	ida_init(&pd_ida->ida);
	pd_ida->max = hr_dev->caps.num_pds - 1;
	pd_ida->min = hr_dev->caps.reserved_pds;
}

int hns_roce_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct ib_device *ib_dev = ibpd->device;
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_dev);
	struct hns_roce_ida *pd_ida = &hr_dev->pd_ida;
	struct hns_roce_pd *pd = to_hr_pd(ibpd);
	int ret = 0;
	int id;

	id = ida_alloc_range(&pd_ida->ida, pd_ida->min, pd_ida->max,
			     GFP_KERNEL);
	if (id < 0) {
		ibdev_err(ib_dev, "failed to alloc pd, id = %d.\n", id);
		return -ENOMEM;
	}
	pd->pdn = (unsigned long)id;

	if (udata) {
		struct hns_roce_ib_alloc_pd_resp resp = {.pdn = pd->pdn};

		ret = ib_copy_to_udata(udata, &resp,
				       min(udata->outlen, sizeof(resp)));
		if (ret) {
			ida_free(&pd_ida->ida, id);
			ibdev_err(ib_dev, "failed to copy to udata, ret = %d\n", ret);
		}
	}

	return ret;
}

int hns_roce_dealloc_pd(struct ib_pd *pd, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(pd->device);

	ida_free(&hr_dev->pd_ida.ida, (int)to_hr_pd(pd)->pdn);

	return 0;
}

int hns_roce_uar_alloc(struct hns_roce_dev *hr_dev, struct hns_roce_uar *uar)
{
	struct hns_roce_ida *uar_ida = &hr_dev->uar_ida;
	int id;

	/* Using bitmap to manager UAR index */
	id = ida_alloc_range(&uar_ida->ida, uar_ida->min, uar_ida->max,
			     GFP_KERNEL);
	if (id < 0) {
		ibdev_err(&hr_dev->ib_dev, "failed to alloc uar id(%d).\n", id);
		return -ENOMEM;
	}
	uar->logic_idx = (unsigned long)id;

	if (uar->logic_idx > 0 && hr_dev->caps.phy_num_uars > 1)
		uar->index = (uar->logic_idx - 1) %
			     (hr_dev->caps.phy_num_uars - 1) + 1;
	else
		uar->index = 0;

	uar->pfn = ((pci_resource_start(hr_dev->pci_dev, 2)) >> PAGE_SHIFT);
	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_DIRECT_WQE)
		hr_dev->dwqe_page = pci_resource_start(hr_dev->pci_dev, 4);

	return 0;
}

void hns_roce_init_uar_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_ida *uar_ida = &hr_dev->uar_ida;

	ida_init(&uar_ida->ida);
	uar_ida->max = hr_dev->caps.num_uars - 1;
	uar_ida->min = hr_dev->caps.reserved_uars;
}

static int hns_roce_xrcd_alloc(struct hns_roce_dev *hr_dev, u32 *xrcdn)
{
	struct hns_roce_ida *xrcd_ida = &hr_dev->xrcd_ida;
	int id;

	id = ida_alloc_range(&xrcd_ida->ida, xrcd_ida->min, xrcd_ida->max,
			     GFP_KERNEL);
	if (id < 0) {
		ibdev_err(&hr_dev->ib_dev, "failed to alloc xrcdn(%d).\n", id);
		return -ENOMEM;
	}
	*xrcdn = (u32)id;

	return 0;
}

void hns_roce_init_xrcd_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_ida *xrcd_ida = &hr_dev->xrcd_ida;

	ida_init(&xrcd_ida->ida);
	xrcd_ida->max = hr_dev->caps.num_xrcds - 1;
	xrcd_ida->min = hr_dev->caps.reserved_xrcds;
}

int hns_roce_alloc_xrcd(struct ib_xrcd *ib_xrcd, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_xrcd->device);
	struct hns_roce_xrcd *xrcd = to_hr_xrcd(ib_xrcd);
	int ret;

	if (!(hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_XRC)) {
		ret = -EOPNOTSUPP;
		goto err_out;
	}

	ret = hns_roce_xrcd_alloc(hr_dev, &xrcd->xrcdn);

err_out:
	if (ret)
		atomic64_inc(&hr_dev->dfx_cnt[HNS_ROCE_DFX_XRCD_ALLOC_ERR_CNT]);

	return ret;
}

int hns_roce_dealloc_xrcd(struct ib_xrcd *ib_xrcd, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_xrcd->device);
	u32 xrcdn = to_hr_xrcd(ib_xrcd)->xrcdn;

	ida_free(&hr_dev->xrcd_ida.ida, (int)xrcdn);

	return 0;
}
