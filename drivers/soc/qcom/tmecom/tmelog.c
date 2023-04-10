// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"tmelog: [%s][%d]:" fmt, __func__, __LINE__

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/tmelog.h>

#include "tmecom.h"

#define TME_MSG_CBOR_TAG_LOG_GET		(280)
#define TME_LOG_CBOR_TAG				0x481801D9 /* _be32 0xD9011848 */

struct tmelog_get_req_t {
	uint32_t cbor_header;
	uint32_t log_len;
	uint32_t log_ptr;
} __packed;

struct tmelog_get_rsp_t {
	uint32_t status;
	uint32_t log_len;
	uint32_t log_ptr;
} __packed;

int tmelog_process_request(uint32_t buf, uint32_t buf_capacity, uint32_t *buf_size)
{
	struct tmelog_get_req_t *request;
	struct tmelog_get_rsp_t *response;
	size_t response_len = 0;
	int ret = 0;

	if (!buf || !buf_size) {
		pr_err("Invalid input parameters\n");
		return -EINVAL;
	}

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	response = kzalloc(sizeof(*response), GFP_KERNEL);
	if (!response || !request) {
		pr_err("Memory allocation failed!\n");
		ret = -ENOMEM;
		goto err_exit;
	}

	request->cbor_header = TME_LOG_CBOR_TAG;
	request->log_len = buf_capacity;
	request->log_ptr = buf;
	response->status = -1;

	pr_debug("request var: cbor_header: %#x, log_len: %#x, log_ptr: %#x\n",
			request->cbor_header, request->log_len, request->log_ptr);
	pr_debug("request_size: %#x, response_size: %#x\n", sizeof(*request), sizeof(*response));
	ret = tmecom_process_request(request, sizeof(*request), response, &response_len);

	if (ret != 0) {
		pr_err("Tme log request failed, ret: %d\n", ret);
		goto err_exit;
	}

	if (response_len != sizeof(*response)) {
		pr_err("Tme Log failed with invalid length: %u, %u\n",
				response_len, sizeof(response));
		ret = -EBADMSG;
		goto err_exit;
	}
	*buf_size = response->log_len;
	ret = response->status;
	pr_debug("response var: status: %d, log_len: %#x, log_ptr: %#x\n",
			 response->status, response->log_len, response->log_ptr);

err_exit:
	kfree(request);
	kfree(response);
	return ret;
}
EXPORT_SYMBOL(tmelog_process_request);
