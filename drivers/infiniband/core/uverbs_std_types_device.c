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

DECLARE_UVERBS_GLOBAL_METHODS(UVERBS_OBJECT_DEVICE,
			      &UVERBS_METHOD(UVERBS_METHOD_INVOKE_WRITE));

const struct uapi_definition uverbs_def_obj_device[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(UVERBS_OBJECT_DEVICE),
	{},
};
