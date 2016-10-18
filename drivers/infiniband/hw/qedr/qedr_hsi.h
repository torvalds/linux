/* QLogic qedr NIC Driver
 * Copyright (c) 2015-2016  QLogic Corporation
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
 *        disclaimer in the documentation and /or other materials
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
#ifndef __QED_HSI_ROCE__
#define __QED_HSI_ROCE__

#include <linux/qed/common_hsi.h>
#include <linux/qed/roce_common.h>
#include "qedr_hsi_rdma.h"

/* Affiliated asynchronous events / errors enumeration */
enum roce_async_events_type {
	ROCE_ASYNC_EVENT_NONE = 0,
	ROCE_ASYNC_EVENT_COMM_EST = 1,
	ROCE_ASYNC_EVENT_SQ_DRAINED,
	ROCE_ASYNC_EVENT_SRQ_LIMIT,
	ROCE_ASYNC_EVENT_LAST_WQE_REACHED,
	ROCE_ASYNC_EVENT_CQ_ERR,
	ROCE_ASYNC_EVENT_LOCAL_INVALID_REQUEST_ERR,
	ROCE_ASYNC_EVENT_LOCAL_CATASTROPHIC_ERR,
	ROCE_ASYNC_EVENT_LOCAL_ACCESS_ERR,
	ROCE_ASYNC_EVENT_QP_CATASTROPHIC_ERR,
	ROCE_ASYNC_EVENT_CQ_OVERFLOW_ERR,
	ROCE_ASYNC_EVENT_SRQ_EMPTY,
	MAX_ROCE_ASYNC_EVENTS_TYPE
};

#endif /* __QED_HSI_ROCE__ */
