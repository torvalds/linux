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

#include "rdma_core.h"
#include "uverbs.h"
#include <rdma/uverbs_std_types.h>

static int uverbs_free_flow_action(struct ib_uobject *uobject,
				   enum rdma_remove_reason why,
				   struct uverbs_attr_bundle *attrs)
{
	struct ib_flow_action *action = uobject->object;

	if (atomic_read(&action->usecnt))
		return -EBUSY;

	return action->device->ops.destroy_flow_action(action);
}

DECLARE_UVERBS_NAMED_METHOD_DESTROY(
	UVERBS_METHOD_FLOW_ACTION_DESTROY,
	UVERBS_ATTR_IDR(UVERBS_ATTR_DESTROY_FLOW_ACTION_HANDLE,
			UVERBS_OBJECT_FLOW_ACTION,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(
	UVERBS_OBJECT_FLOW_ACTION,
	UVERBS_TYPE_ALLOC_IDR(uverbs_free_flow_action),
	&UVERBS_METHOD(UVERBS_METHOD_FLOW_ACTION_DESTROY));

const struct uapi_definition uverbs_def_obj_flow_action[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(
		UVERBS_OBJECT_FLOW_ACTION,
		UAPI_DEF_OBJ_NEEDS_FN(destroy_flow_action)),
	{}
};
