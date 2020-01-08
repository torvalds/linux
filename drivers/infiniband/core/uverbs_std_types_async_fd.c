// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2019, Mellanox Technologies inc.  All rights reserved.
 */

#include <rdma/uverbs_std_types.h>
#include <rdma/uverbs_ioctl.h>
#include "rdma_core.h"
#include "uverbs.h"

static int uverbs_async_event_destroy_uobj(struct ib_uobject *uobj,
					   enum rdma_remove_reason why)
{
	struct ib_uverbs_async_event_file *event_file =
		container_of(uobj, struct ib_uverbs_async_event_file, uobj);

	ib_unregister_event_handler(&event_file->event_handler);
	ib_uverbs_free_event_queue(&event_file->ev_queue);
	return 0;
}

DECLARE_UVERBS_NAMED_OBJECT(
	UVERBS_OBJECT_ASYNC_EVENT,
	UVERBS_TYPE_ALLOC_FD(sizeof(struct ib_uverbs_async_event_file),
			     uverbs_async_event_destroy_uobj,
			     &uverbs_async_event_fops,
			     "[infinibandevent]",
			     O_RDONLY));

const struct uapi_definition uverbs_def_obj_async_fd[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(UVERBS_OBJECT_ASYNC_EVENT),
	{}
};
