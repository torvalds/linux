// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/usb/dwc3-msm.h>

#include "f_qdss.h"

#define NUM_EBC_IN_BUF	2

int alloc_hw_req(struct usb_ep *data_ep)
{
	struct usb_request *req = NULL;
	struct f_qdss *qdss = data_ep->driver_data;

	pr_debug("allocating EBC request\n");

	req = usb_ep_alloc_request(data_ep, GFP_ATOMIC);
	if (!req) {
		pr_err("usb_ep_alloc_request failed\n");
		return -ENOMEM;
	}

	req->length = NUM_EBC_IN_BUF * EBC_TRB_SIZE;
	qdss->endless_req = req;

	return 0;
}

static int enable_qdss_ebc_data_connection(struct f_qdss *qdss)
{
	int ret;

	ret = msm_ep_config(qdss->port.data, qdss->endless_req, 1);
	if (ret)
		pr_err("msm_ep_config failed\n");

	return ret;
}

int set_qdss_data_connection(struct f_qdss *qdss, int enable)
{
	struct usb_gadget *gadget;
	struct device *dev;
	int ret = 0;

	if (!qdss) {
		pr_err("%s: qdss ptr is NULL\n", __func__);
		return -EINVAL;
	}

	gadget = qdss->gadget;
	dev = gadget->dev.parent;

	pr_debug("%s ch_type:%d\n", __func__, qdss->ch.ch_type);
	if (enable) {
		ret = enable_qdss_ebc_data_connection(qdss);
	} else {
		ret = msm_ep_unconfig(qdss->port.data);
		if (ret)
			pr_err("msm_ep_unconfig failed\n");
	}

	return ret;
}
