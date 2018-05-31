// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2018, Mellanox Technologies inc.  All rights reserved.
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

#include "uverbs.h"
#include <rdma/uverbs_std_types.h>

static int uverbs_free_counters(struct ib_uobject *uobject,
				enum rdma_remove_reason why)
{
	struct ib_counters *counters = uobject->object;

	if (why == RDMA_REMOVE_DESTROY &&
	    atomic_read(&counters->usecnt))
		return -EBUSY;

	return counters->device->destroy_counters(counters);
}

static int UVERBS_HANDLER(UVERBS_METHOD_COUNTERS_CREATE)(struct ib_device *ib_dev,
							 struct ib_uverbs_file *file,
							 struct uverbs_attr_bundle *attrs)
{
	struct ib_counters *counters;
	struct ib_uobject *uobj;
	int ret;

	/*
	 * This check should be removed once the infrastructure
	 * have the ability to remove methods from parse tree once
	 * such condition is met.
	 */
	if (!ib_dev->create_counters)
		return -EOPNOTSUPP;

	uobj = uverbs_attr_get_uobject(attrs, UVERBS_ATTR_CREATE_COUNTERS_HANDLE);
	counters = ib_dev->create_counters(ib_dev, attrs);
	if (IS_ERR(counters)) {
		ret = PTR_ERR(counters);
		goto err_create_counters;
	}

	counters->device = ib_dev;
	counters->uobject = uobj;
	uobj->object = counters;
	atomic_set(&counters->usecnt, 0);

	return 0;

err_create_counters:
	return ret;
}

static DECLARE_UVERBS_NAMED_METHOD(UVERBS_METHOD_COUNTERS_CREATE,
	&UVERBS_ATTR_IDR(UVERBS_ATTR_CREATE_COUNTERS_HANDLE,
			 UVERBS_OBJECT_COUNTERS,
			 UVERBS_ACCESS_NEW,
			 UA_FLAGS(UVERBS_ATTR_SPEC_F_MANDATORY)));

static DECLARE_UVERBS_NAMED_METHOD_WITH_HANDLER(UVERBS_METHOD_COUNTERS_DESTROY,
	uverbs_destroy_def_handler,
	&UVERBS_ATTR_IDR(UVERBS_ATTR_DESTROY_COUNTERS_HANDLE,
			 UVERBS_OBJECT_COUNTERS,
			 UVERBS_ACCESS_DESTROY,
			 UA_FLAGS(UVERBS_ATTR_SPEC_F_MANDATORY)));

DECLARE_UVERBS_NAMED_OBJECT(UVERBS_OBJECT_COUNTERS,
			    &UVERBS_TYPE_ALLOC_IDR(0, uverbs_free_counters),
			    &UVERBS_METHOD(UVERBS_METHOD_COUNTERS_CREATE),
			    &UVERBS_METHOD(UVERBS_METHOD_COUNTERS_DESTROY));

