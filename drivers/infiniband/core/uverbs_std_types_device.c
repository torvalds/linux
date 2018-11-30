// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2018, Mellanox Technologies inc.  All rights reserved.
 */

#include <rdma/uverbs_std_types.h>
#include "rdma_core.h"
#include "uverbs.h"

/*
 * This ioctl method allows calling any defined write or write_ex
 * handler. This essentially replaces the hdr/ex_hdr system with the ioctl
 * marshalling, and brings the non-ex path into the same marshalling as the ex
 * path.
 */
static int UVERBS_HANDLER(UVERBS_METHOD_INVOKE_WRITE)(
	struct uverbs_attr_bundle *attrs)
{
	struct uverbs_api *uapi = attrs->ufile->device->uapi;
	const struct uverbs_api_write_method *method_elm;
	u32 cmd;
	int rc;

	rc = uverbs_get_const(&cmd, attrs, UVERBS_ATTR_WRITE_CMD);
	if (rc)
		return rc;

	method_elm = uapi_get_method(uapi, cmd);
	if (IS_ERR(method_elm))
		return PTR_ERR(method_elm);

	uverbs_fill_udata(attrs, &attrs->ucore, UVERBS_ATTR_CORE_IN,
			  UVERBS_ATTR_CORE_OUT);

	if (attrs->ucore.inlen < method_elm->req_size ||
	    attrs->ucore.outlen < method_elm->resp_size)
		return -ENOSPC;

	return method_elm->handler(attrs);
}

DECLARE_UVERBS_NAMED_METHOD(UVERBS_METHOD_INVOKE_WRITE,
			    UVERBS_ATTR_CONST_IN(UVERBS_ATTR_WRITE_CMD,
						 enum ib_uverbs_write_cmds,
						 UA_MANDATORY),
			    UVERBS_ATTR_PTR_IN(UVERBS_ATTR_CORE_IN,
					       UVERBS_ATTR_MIN_SIZE(sizeof(u32)),
					       UA_OPTIONAL),
			    UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_CORE_OUT,
						UVERBS_ATTR_MIN_SIZE(0),
						UA_OPTIONAL),
			    UVERBS_ATTR_UHW());

static uint32_t *
gather_objects_handle(struct ib_uverbs_file *ufile,
		      const struct uverbs_api_object *uapi_object,
		      struct uverbs_attr_bundle *attrs,
		      ssize_t out_len,
		      u64 *total)
{
	u64 max_count = out_len / sizeof(u32);
	struct ib_uobject *obj;
	u64 count = 0;
	u32 *handles;

	/* Allocated memory that cannot page out where we gather
	 * all object ids under a spin_lock.
	 */
	handles = uverbs_zalloc(attrs, out_len);
	if (IS_ERR(handles))
		return handles;

	spin_lock_irq(&ufile->uobjects_lock);
	list_for_each_entry(obj, &ufile->uobjects, list) {
		u32 obj_id = obj->id;

		if (obj->uapi_object != uapi_object)
			continue;

		if (count >= max_count)
			break;

		handles[count] = obj_id;
		count++;
	}
	spin_unlock_irq(&ufile->uobjects_lock);

	*total = count;
	return handles;
}

static int UVERBS_HANDLER(UVERBS_METHOD_INFO_HANDLES)(
	struct uverbs_attr_bundle *attrs)
{
	const struct uverbs_api_object *uapi_object;
	ssize_t out_len;
	u64 total = 0;
	u16 object_id;
	u32 *handles;
	int ret;

	out_len = uverbs_attr_get_len(attrs, UVERBS_ATTR_INFO_HANDLES_LIST);
	if (out_len <= 0 || (out_len % sizeof(u32) != 0))
		return -EINVAL;

	ret = uverbs_get_const(&object_id, attrs, UVERBS_ATTR_INFO_OBJECT_ID);
	if (ret)
		return ret;

	uapi_object = uapi_get_object(attrs->ufile->device->uapi, object_id);
	if (!uapi_object)
		return -EINVAL;

	handles = gather_objects_handle(attrs->ufile, uapi_object, attrs,
					out_len, &total);
	if (IS_ERR(handles))
		return PTR_ERR(handles);

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_INFO_HANDLES_LIST, handles,
			     sizeof(u32) * total);
	if (ret)
		goto err;

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_INFO_TOTAL_HANDLES, &total,
			     sizeof(total));
err:
	return ret;
}

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_INFO_HANDLES,
	/* Also includes any device specific object ids */
	UVERBS_ATTR_CONST_IN(UVERBS_ATTR_INFO_OBJECT_ID,
			     enum uverbs_default_objects, UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_INFO_TOTAL_HANDLES,
			    UVERBS_ATTR_TYPE(u32), UA_OPTIONAL),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_INFO_HANDLES_LIST,
			    UVERBS_ATTR_MIN_SIZE(sizeof(u32)), UA_OPTIONAL));

DECLARE_UVERBS_GLOBAL_METHODS(UVERBS_OBJECT_DEVICE,
			      &UVERBS_METHOD(UVERBS_METHOD_INVOKE_WRITE),
			      &UVERBS_METHOD(UVERBS_METHOD_INFO_HANDLES));

const struct uapi_definition uverbs_def_obj_device[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(UVERBS_OBJECT_DEVICE),
	{},
};
