// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2019, Mellanox Technologies inc.  All rights reserved.
 */

#include <rdma/uverbs_std_types.h>
#include <rdma/uverbs_ioctl.h>
#include "rdma_core.h"
#include "uverbs.h"

static int UVERBS_HANDLER(UVERBS_METHOD_ASYNC_EVENT_ALLOC)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj =
		uverbs_attr_get_uobject(attrs, UVERBS_METHOD_ASYNC_EVENT_ALLOC);

	ib_uverbs_init_async_event_file(
		container_of(uobj, struct ib_uverbs_async_event_file, uobj));
	return 0;
}

static int uverbs_async_event_destroy_uobj(struct ib_uobject *uobj,
					   enum rdma_remove_reason why)
{
	struct ib_uverbs_async_event_file *event_file =
		container_of(uobj, struct ib_uverbs_async_event_file, uobj);

	ib_unregister_event_handler(&event_file->event_handler);

	if (why == RDMA_REMOVE_DRIVER_REMOVE)
		ib_uverbs_async_handler(event_file, 0, IB_EVENT_DEVICE_FATAL,
					NULL, NULL);
	return 0;
}

int uverbs_async_event_release(struct inode *inode, struct file *filp)
{
	struct ib_uverbs_async_event_file *event_file;
	struct ib_uobject *uobj = filp->private_data;
	int ret;

	if (!uobj)
		return uverbs_uobject_fd_release(inode, filp);

	event_file =
		container_of(uobj, struct ib_uverbs_async_event_file, uobj);

	/*
	 * The async event FD has to deliver IB_EVENT_DEVICE_FATAL even after
	 * disassociation, so cleaning the event list must only happen after
	 * release. The user knows it has reached the end of the event stream
	 * when it sees IB_EVENT_DEVICE_FATAL.
	 */
	uverbs_uobject_get(uobj);
	ret = uverbs_uobject_fd_release(inode, filp);
	ib_uverbs_free_event_queue(&event_file->ev_queue);
	uverbs_uobject_put(uobj);
	return ret;
}

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_ASYNC_EVENT_ALLOC,
	UVERBS_ATTR_FD(UVERBS_ATTR_ASYNC_EVENT_ALLOC_FD_HANDLE,
		       UVERBS_OBJECT_ASYNC_EVENT,
		       UVERBS_ACCESS_NEW,
		       UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(
	UVERBS_OBJECT_ASYNC_EVENT,
	UVERBS_TYPE_ALLOC_FD(sizeof(struct ib_uverbs_async_event_file),
			     uverbs_async_event_destroy_uobj,
			     &uverbs_async_event_fops,
			     "[infinibandevent]",
			     O_RDONLY),
	&UVERBS_METHOD(UVERBS_METHOD_ASYNC_EVENT_ALLOC));

const struct uapi_definition uverbs_def_obj_async_fd[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(UVERBS_OBJECT_ASYNC_EVENT),
	{}
};
