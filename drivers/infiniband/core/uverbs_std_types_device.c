// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2018, Mellanox Technologies inc.  All rights reserved.
 */

#include <rdma/uverbs_std_types.h>
#include "rdma_core.h"
#include "uverbs.h"
#include <rdma/uverbs_ioctl.h>
#include <rdma/opa_addr.h>

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

void copy_port_attr_to_resp(struct ib_port_attr *attr,
			    struct ib_uverbs_query_port_resp *resp,
			    struct ib_device *ib_dev, u8 port_num)
{
	resp->state = attr->state;
	resp->max_mtu = attr->max_mtu;
	resp->active_mtu = attr->active_mtu;
	resp->gid_tbl_len = attr->gid_tbl_len;
	resp->port_cap_flags = make_port_cap_flags(attr);
	resp->max_msg_sz = attr->max_msg_sz;
	resp->bad_pkey_cntr = attr->bad_pkey_cntr;
	resp->qkey_viol_cntr = attr->qkey_viol_cntr;
	resp->pkey_tbl_len = attr->pkey_tbl_len;

	if (rdma_is_grh_required(ib_dev, port_num))
		resp->flags |= IB_UVERBS_QPF_GRH_REQUIRED;

	if (rdma_cap_opa_ah(ib_dev, port_num)) {
		resp->lid = OPA_TO_IB_UCAST_LID(attr->lid);
		resp->sm_lid = OPA_TO_IB_UCAST_LID(attr->sm_lid);
	} else {
		resp->lid = ib_lid_cpu16(attr->lid);
		resp->sm_lid = ib_lid_cpu16(attr->sm_lid);
	}

	resp->lmc = attr->lmc;
	resp->max_vl_num = attr->max_vl_num;
	resp->sm_sl = attr->sm_sl;
	resp->subnet_timeout = attr->subnet_timeout;
	resp->init_type_reply = attr->init_type_reply;
	resp->active_width = attr->active_width;
	resp->active_speed = attr->active_speed;
	resp->phys_state = attr->phys_state;
	resp->link_layer = rdma_port_get_link_layer(ib_dev, port_num);
}

static int UVERBS_HANDLER(UVERBS_METHOD_QUERY_PORT)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_device *ib_dev;
	struct ib_port_attr attr = {};
	struct ib_uverbs_query_port_resp_ex resp = {};
	struct ib_ucontext *ucontext;
	int ret;
	u8 port_num;

	ucontext = ib_uverbs_get_ucontext(attrs);
	if (IS_ERR(ucontext))
		return PTR_ERR(ucontext);
	ib_dev = ucontext->device;

	/* FIXME: Extend the UAPI_DEF_OBJ_NEEDS_FN stuff.. */
	if (!ib_dev->ops.query_port)
		return -EOPNOTSUPP;

	ret = uverbs_get_const(&port_num, attrs,
			       UVERBS_ATTR_QUERY_PORT_PORT_NUM);
	if (ret)
		return ret;

	ret = ib_query_port(ib_dev, port_num, &attr);
	if (ret)
		return ret;

	copy_port_attr_to_resp(&attr, &resp.legacy_resp, ib_dev, port_num);
	resp.port_cap_flags2 = attr.port_cap_flags2;

	return uverbs_copy_to_struct_or_zero(attrs, UVERBS_ATTR_QUERY_PORT_RESP,
					     &resp, sizeof(resp));
}

static int UVERBS_HANDLER(UVERBS_METHOD_GET_CONTEXT)(
	struct uverbs_attr_bundle *attrs)
{
	u32 num_comp = attrs->ufile->device->num_comp_vectors;
	int ret;

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_GET_CONTEXT_NUM_COMP_VECTORS,
			     &num_comp, sizeof(num_comp));
	if (IS_UVERBS_COPY_ERR(ret))
		return ret;

	ret = ib_alloc_ucontext(attrs);
	if (ret)
		return ret;
	ret = ib_init_ucontext(attrs);
	if (ret) {
		kfree(attrs->context);
		attrs->context = NULL;
		return ret;
	}
	return 0;
}

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_GET_CONTEXT,
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_GET_CONTEXT_NUM_COMP_VECTORS,
			    UVERBS_ATTR_TYPE(u32), UA_OPTIONAL),
	UVERBS_ATTR_UHW());

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_INFO_HANDLES,
	/* Also includes any device specific object ids */
	UVERBS_ATTR_CONST_IN(UVERBS_ATTR_INFO_OBJECT_ID,
			     enum uverbs_default_objects, UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_INFO_TOTAL_HANDLES,
			    UVERBS_ATTR_TYPE(u32), UA_OPTIONAL),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_INFO_HANDLES_LIST,
			    UVERBS_ATTR_MIN_SIZE(sizeof(u32)), UA_OPTIONAL));

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_QUERY_PORT,
	UVERBS_ATTR_CONST_IN(UVERBS_ATTR_QUERY_PORT_PORT_NUM, u8, UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(
		UVERBS_ATTR_QUERY_PORT_RESP,
		UVERBS_ATTR_STRUCT(struct ib_uverbs_query_port_resp_ex,
				   reserved),
		UA_MANDATORY));

DECLARE_UVERBS_GLOBAL_METHODS(UVERBS_OBJECT_DEVICE,
			      &UVERBS_METHOD(UVERBS_METHOD_GET_CONTEXT),
			      &UVERBS_METHOD(UVERBS_METHOD_INVOKE_WRITE),
			      &UVERBS_METHOD(UVERBS_METHOD_INFO_HANDLES),
			      &UVERBS_METHOD(UVERBS_METHOD_QUERY_PORT));

const struct uapi_definition uverbs_def_obj_device[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(UVERBS_OBJECT_DEVICE),
	{},
};
