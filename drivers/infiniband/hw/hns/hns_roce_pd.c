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

#include <linux/platform_device.h>
#include <linux/pci.h>
#include "hns_roce_device.h"

static int hns_roce_pd_alloc(struct hns_roce_dev *hr_dev, unsigned long *pdn)
{
	return hns_roce_bitmap_alloc(&hr_dev->pd_bitmap, pdn) ? -ENOMEM : 0;
}

static void hns_roce_pd_free(struct hns_roce_dev *hr_dev, unsigned long pdn)
{
	hns_roce_bitmap_free(&hr_dev->pd_bitmap, pdn, BITMAP_NO_RR);
}

int hns_roce_init_pd_table(struct hns_roce_dev *hr_dev)
{
	return hns_roce_bitmap_init(&hr_dev->pd_bitmap, hr_dev->caps.num_pds,
				    hr_dev->caps.num_pds - 1,
				    hr_dev->caps.reserved_pds, 0);
}

void hns_roce_cleanup_pd_table(struct hns_roce_dev *hr_dev)
{
	hns_roce_bitmap_cleanup(&hr_dev->pd_bitmap);
}

int hns_roce_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct ib_device *ib_dev = ibpd->device;
	struct hns_roce_pd *pd = to_hr_pd(ibpd);
	int ret;

	ret = hns_roce_pd_alloc(to_hr_dev(ib_dev), &pd->pdn);
	if (ret) {
		ibdev_err(ib_dev, "failed to alloc pd, ret = %d.\n", ret);
		return ret;
	}

	if (udata) {
		struct hns_roce_ib_alloc_pd_resp resp = {.pdn = pd->pdn};

		ret = ib_copy_to_udata(udata, &resp,
				       min(udata->outlen, sizeof(resp)));
		if (ret) {
			hns_roce_pd_free(to_hr_dev(ib_dev), pd->pdn);
			ibdev_err(ib_dev, "failed to copy to udata, ret = %d\n", ret);
		}
	}

	return ret;
}

int hns_roce_dealloc_pd(struct ib_pd *pd, struct ib_udata *udata)
{
	hns_roce_pd_free(to_hr_dev(pd->device), to_hr_pd(pd)->pdn);
	return 0;
}

int hns_roce_uar_alloc(struct hns_roce_dev *hr_dev, struct hns_roce_uar *uar)
{
	struct resource *res;
	int ret;

	/* Using bitmap to manager UAR index */
	ret = hns_roce_bitmap_alloc(&hr_dev->uar_table.bitmap, &uar->logic_idx);
	if (ret)
		return -ENOMEM;

	if (uar->logic_idx > 0 && hr_dev->caps.phy_num_uars > 1)
		uar->index = (uar->logic_idx - 1) %
			     (hr_dev->caps.phy_num_uars - 1) + 1;
	else
		uar->index = 0;

	if (!dev_is_pci(hr_dev->dev)) {
		res = platform_get_resource(hr_dev->pdev, IORESOURCE_MEM, 0);
		if (!res) {
			dev_err(&hr_dev->pdev->dev, "memory resource not found!\n");
			return -EINVAL;
		}
		uar->pfn = ((res->start) >> PAGE_SHIFT) + uar->index;
	} else {
		uar->pfn = ((pci_resource_start(hr_dev->pci_dev, 2))
			   >> PAGE_SHIFT);
	}

	return 0;
}

void hns_roce_uar_free(struct hns_roce_dev *hr_dev, struct hns_roce_uar *uar)
{
	hns_roce_bitmap_free(&hr_dev->uar_table.bitmap, uar->logic_idx,
			     BITMAP_NO_RR);
}

int hns_roce_init_uar_table(struct hns_roce_dev *hr_dev)
{
	return hns_roce_bitmap_init(&hr_dev->uar_table.bitmap,
				    hr_dev->caps.num_uars,
				    hr_dev->caps.num_uars - 1,
				    hr_dev->caps.reserved_uars, 0);
}

void hns_roce_cleanup_uar_table(struct hns_roce_dev *hr_dev)
{
	hns_roce_bitmap_cleanup(&hr_dev->uar_table.bitmap);
}

static int hns_roce_xrcd_alloc(struct hns_roce_dev *hr_dev, u32 *xrcdn)
{
	return hns_roce_bitmap_alloc(&hr_dev->xrcd_bitmap,
				     (unsigned long *)xrcdn);
}

static void hns_roce_xrcd_free(struct hns_roce_dev *hr_dev,
			       u32 xrcdn)
{
	hns_roce_bitmap_free(&hr_dev->xrcd_bitmap, xrcdn, BITMAP_NO_RR);
}

int hns_roce_init_xrcd_table(struct hns_roce_dev *hr_dev)
{
	return hns_roce_bitmap_init(&hr_dev->xrcd_bitmap,
				    hr_dev->caps.num_xrcds,
				    hr_dev->caps.num_xrcds - 1,
				    hr_dev->caps.reserved_xrcds, 0);
}

void hns_roce_cleanup_xrcd_table(struct hns_roce_dev *hr_dev)
{
	hns_roce_bitmap_cleanup(&hr_dev->xrcd_bitmap);
}

int hns_roce_alloc_xrcd(struct ib_xrcd *ib_xrcd, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_xrcd->device);
	struct hns_roce_xrcd *xrcd = to_hr_xrcd(ib_xrcd);
	int ret;

	if (!(hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_XRC))
		return -EINVAL;

	ret = hns_roce_xrcd_alloc(hr_dev, &xrcd->xrcdn);
	if (ret) {
		dev_err(hr_dev->dev, "failed to alloc xrcdn, ret = %d.\n", ret);
		return ret;
	}

	return 0;
}

int hns_roce_dealloc_xrcd(struct ib_xrcd *ib_xrcd, struct ib_udata *udata)
{
	hns_roce_xrcd_free(to_hr_dev(ib_xrcd->device),
			   to_hr_xrcd(ib_xrcd)->xrcdn);

	return 0;
}
