/*
 * Copyright (c) 2018, Mellanox Technologies inc.  All rights reserved.
 * Copyright (c) 2020, Intel Corporation.  All rights reserved.
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
#include "restrack.h"

static int uverbs_free_mr(struct ib_uobject *uobject,
			  enum rdma_remove_reason why,
			  struct uverbs_attr_bundle *attrs)
{
	return ib_dereg_mr_user((struct ib_mr *)uobject->object,
				&attrs->driver_udata);
}

static int UVERBS_HANDLER(UVERBS_METHOD_ADVISE_MR)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_pd *pd =
		uverbs_attr_get_obj(attrs, UVERBS_ATTR_ADVISE_MR_PD_HANDLE);
	enum ib_uverbs_advise_mr_advice advice;
	struct ib_device *ib_dev = pd->device;
	struct ib_sge *sg_list;
	int num_sge;
	u32 flags;
	int ret;

	/* FIXME: Extend the UAPI_DEF_OBJ_NEEDS_FN stuff.. */
	if (!ib_dev->ops.advise_mr)
		return -EOPNOTSUPP;

	ret = uverbs_get_const(&advice, attrs, UVERBS_ATTR_ADVISE_MR_ADVICE);
	if (ret)
		return ret;

	ret = uverbs_get_flags32(&flags, attrs, UVERBS_ATTR_ADVISE_MR_FLAGS,
				 IB_UVERBS_ADVISE_MR_FLAG_FLUSH);
	if (ret)
		return ret;

	num_sge = uverbs_attr_ptr_get_array_size(
		attrs, UVERBS_ATTR_ADVISE_MR_SGE_LIST, sizeof(struct ib_sge));
	if (num_sge <= 0)
		return num_sge;

	sg_list = uverbs_attr_get_alloced_ptr(attrs,
					      UVERBS_ATTR_ADVISE_MR_SGE_LIST);
	return ib_dev->ops.advise_mr(pd, advice, flags, sg_list, num_sge,
				     attrs);
}

static int UVERBS_HANDLER(UVERBS_METHOD_DM_MR_REG)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_dm_mr_attr attr = {};
	struct ib_uobject *uobj =
		uverbs_attr_get_uobject(attrs, UVERBS_ATTR_REG_DM_MR_HANDLE);
	struct ib_dm *dm =
		uverbs_attr_get_obj(attrs, UVERBS_ATTR_REG_DM_MR_DM_HANDLE);
	struct ib_pd *pd =
		uverbs_attr_get_obj(attrs, UVERBS_ATTR_REG_DM_MR_PD_HANDLE);
	struct ib_device *ib_dev = pd->device;

	struct ib_mr *mr;
	int ret;

	if (!ib_dev->ops.reg_dm_mr)
		return -EOPNOTSUPP;

	ret = uverbs_copy_from(&attr.offset, attrs, UVERBS_ATTR_REG_DM_MR_OFFSET);
	if (ret)
		return ret;

	ret = uverbs_copy_from(&attr.length, attrs,
			       UVERBS_ATTR_REG_DM_MR_LENGTH);
	if (ret)
		return ret;

	ret = uverbs_get_flags32(&attr.access_flags, attrs,
				 UVERBS_ATTR_REG_DM_MR_ACCESS_FLAGS,
				 IB_ACCESS_SUPPORTED);
	if (ret)
		return ret;

	if (!(attr.access_flags & IB_ZERO_BASED))
		return -EINVAL;

	ret = ib_check_mr_access(ib_dev, attr.access_flags);
	if (ret)
		return ret;

	if (attr.offset > dm->length || attr.length > dm->length ||
	    attr.length > dm->length - attr.offset)
		return -EINVAL;

	mr = pd->device->ops.reg_dm_mr(pd, dm, &attr, attrs);
	if (IS_ERR(mr))
		return PTR_ERR(mr);

	mr->device  = pd->device;
	mr->pd      = pd;
	mr->type    = IB_MR_TYPE_DM;
	mr->dm      = dm;
	mr->uobject = uobj;
	atomic_inc(&pd->usecnt);
	atomic_inc(&dm->usecnt);

	rdma_restrack_new(&mr->res, RDMA_RESTRACK_MR);
	rdma_restrack_set_name(&mr->res, NULL);
	rdma_restrack_add(&mr->res);
	uobj->object = mr;

	uverbs_finalize_uobj_create(attrs, UVERBS_ATTR_REG_DM_MR_HANDLE);

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_REG_DM_MR_RESP_LKEY, &mr->lkey,
			     sizeof(mr->lkey));
	if (ret)
		return ret;

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_REG_DM_MR_RESP_RKEY,
			     &mr->rkey, sizeof(mr->rkey));
	return ret;
}

static int UVERBS_HANDLER(UVERBS_METHOD_QUERY_MR)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_mr *mr =
		uverbs_attr_get_obj(attrs, UVERBS_ATTR_QUERY_MR_HANDLE);
	int ret;

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_QUERY_MR_RESP_LKEY, &mr->lkey,
			     sizeof(mr->lkey));
	if (ret)
		return ret;

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_QUERY_MR_RESP_RKEY,
			     &mr->rkey, sizeof(mr->rkey));

	if (ret)
		return ret;

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_QUERY_MR_RESP_LENGTH,
			     &mr->length, sizeof(mr->length));

	if (ret)
		return ret;

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_QUERY_MR_RESP_IOVA,
			     &mr->iova, sizeof(mr->iova));

	return IS_UVERBS_COPY_ERR(ret) ? ret : 0;
}

static int UVERBS_HANDLER(UVERBS_METHOD_REG_DMABUF_MR)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj =
		uverbs_attr_get_uobject(attrs, UVERBS_ATTR_REG_DMABUF_MR_HANDLE);
	struct ib_pd *pd =
		uverbs_attr_get_obj(attrs, UVERBS_ATTR_REG_DMABUF_MR_PD_HANDLE);
	struct ib_device *ib_dev = pd->device;

	u64 offset, length, iova;
	u32 fd, access_flags;
	struct ib_mr *mr;
	int ret;

	if (!ib_dev->ops.reg_user_mr_dmabuf)
		return -EOPNOTSUPP;

	ret = uverbs_copy_from(&offset, attrs,
			       UVERBS_ATTR_REG_DMABUF_MR_OFFSET);
	if (ret)
		return ret;

	ret = uverbs_copy_from(&length, attrs,
			       UVERBS_ATTR_REG_DMABUF_MR_LENGTH);
	if (ret)
		return ret;

	ret = uverbs_copy_from(&iova, attrs,
			       UVERBS_ATTR_REG_DMABUF_MR_IOVA);
	if (ret)
		return ret;

	if ((offset & ~PAGE_MASK) != (iova & ~PAGE_MASK))
		return -EINVAL;

	ret = uverbs_copy_from(&fd, attrs,
			       UVERBS_ATTR_REG_DMABUF_MR_FD);
	if (ret)
		return ret;

	ret = uverbs_get_flags32(&access_flags, attrs,
				 UVERBS_ATTR_REG_DMABUF_MR_ACCESS_FLAGS,
				 IB_ACCESS_LOCAL_WRITE |
				 IB_ACCESS_REMOTE_READ |
				 IB_ACCESS_REMOTE_WRITE |
				 IB_ACCESS_REMOTE_ATOMIC |
				 IB_ACCESS_RELAXED_ORDERING);
	if (ret)
		return ret;

	ret = ib_check_mr_access(ib_dev, access_flags);
	if (ret)
		return ret;

	mr = pd->device->ops.reg_user_mr_dmabuf(pd, offset, length, iova, fd,
						access_flags,
						&attrs->driver_udata);
	if (IS_ERR(mr))
		return PTR_ERR(mr);

	mr->device = pd->device;
	mr->pd = pd;
	mr->type = IB_MR_TYPE_USER;
	mr->uobject = uobj;
	atomic_inc(&pd->usecnt);

	uobj->object = mr;

	uverbs_finalize_uobj_create(attrs, UVERBS_ATTR_REG_DMABUF_MR_HANDLE);

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_REG_DMABUF_MR_RESP_LKEY,
			     &mr->lkey, sizeof(mr->lkey));
	if (ret)
		return ret;

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_REG_DMABUF_MR_RESP_RKEY,
			     &mr->rkey, sizeof(mr->rkey));
	return ret;
}

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_ADVISE_MR,
	UVERBS_ATTR_IDR(UVERBS_ATTR_ADVISE_MR_PD_HANDLE,
			UVERBS_OBJECT_PD,
			UVERBS_ACCESS_READ,
			UA_MANDATORY),
	UVERBS_ATTR_CONST_IN(UVERBS_ATTR_ADVISE_MR_ADVICE,
			     enum ib_uverbs_advise_mr_advice,
			     UA_MANDATORY),
	UVERBS_ATTR_FLAGS_IN(UVERBS_ATTR_ADVISE_MR_FLAGS,
			     enum ib_uverbs_advise_mr_flag,
			     UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_ADVISE_MR_SGE_LIST,
			   UVERBS_ATTR_MIN_SIZE(sizeof(struct ib_uverbs_sge)),
			   UA_MANDATORY,
			   UA_ALLOC_AND_COPY));

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_QUERY_MR,
	UVERBS_ATTR_IDR(UVERBS_ATTR_QUERY_MR_HANDLE,
			UVERBS_OBJECT_MR,
			UVERBS_ACCESS_READ,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_QUERY_MR_RESP_RKEY,
			    UVERBS_ATTR_TYPE(u32),
			    UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_QUERY_MR_RESP_LKEY,
			    UVERBS_ATTR_TYPE(u32),
			    UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_QUERY_MR_RESP_LENGTH,
			    UVERBS_ATTR_TYPE(u64),
			    UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_QUERY_MR_RESP_IOVA,
			    UVERBS_ATTR_TYPE(u64),
			    UA_OPTIONAL));

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_DM_MR_REG,
	UVERBS_ATTR_IDR(UVERBS_ATTR_REG_DM_MR_HANDLE,
			UVERBS_OBJECT_MR,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_REG_DM_MR_OFFSET,
			   UVERBS_ATTR_TYPE(u64),
			   UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_REG_DM_MR_LENGTH,
			   UVERBS_ATTR_TYPE(u64),
			   UA_MANDATORY),
	UVERBS_ATTR_IDR(UVERBS_ATTR_REG_DM_MR_PD_HANDLE,
			UVERBS_OBJECT_PD,
			UVERBS_ACCESS_READ,
			UA_MANDATORY),
	UVERBS_ATTR_FLAGS_IN(UVERBS_ATTR_REG_DM_MR_ACCESS_FLAGS,
			     enum ib_access_flags),
	UVERBS_ATTR_IDR(UVERBS_ATTR_REG_DM_MR_DM_HANDLE,
			UVERBS_OBJECT_DM,
			UVERBS_ACCESS_READ,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_REG_DM_MR_RESP_LKEY,
			    UVERBS_ATTR_TYPE(u32),
			    UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_REG_DM_MR_RESP_RKEY,
			    UVERBS_ATTR_TYPE(u32),
			    UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_REG_DMABUF_MR,
	UVERBS_ATTR_IDR(UVERBS_ATTR_REG_DMABUF_MR_HANDLE,
			UVERBS_OBJECT_MR,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_IDR(UVERBS_ATTR_REG_DMABUF_MR_PD_HANDLE,
			UVERBS_OBJECT_PD,
			UVERBS_ACCESS_READ,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_REG_DMABUF_MR_OFFSET,
			   UVERBS_ATTR_TYPE(u64),
			   UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_REG_DMABUF_MR_LENGTH,
			   UVERBS_ATTR_TYPE(u64),
			   UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_REG_DMABUF_MR_IOVA,
			   UVERBS_ATTR_TYPE(u64),
			   UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_REG_DMABUF_MR_FD,
			   UVERBS_ATTR_TYPE(u32),
			   UA_MANDATORY),
	UVERBS_ATTR_FLAGS_IN(UVERBS_ATTR_REG_DMABUF_MR_ACCESS_FLAGS,
			     enum ib_access_flags),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_REG_DMABUF_MR_RESP_LKEY,
			    UVERBS_ATTR_TYPE(u32),
			    UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_REG_DMABUF_MR_RESP_RKEY,
			    UVERBS_ATTR_TYPE(u32),
			    UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD_DESTROY(
	UVERBS_METHOD_MR_DESTROY,
	UVERBS_ATTR_IDR(UVERBS_ATTR_DESTROY_MR_HANDLE,
			UVERBS_OBJECT_MR,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(
	UVERBS_OBJECT_MR,
	UVERBS_TYPE_ALLOC_IDR(uverbs_free_mr),
	&UVERBS_METHOD(UVERBS_METHOD_ADVISE_MR),
	&UVERBS_METHOD(UVERBS_METHOD_DM_MR_REG),
	&UVERBS_METHOD(UVERBS_METHOD_MR_DESTROY),
	&UVERBS_METHOD(UVERBS_METHOD_QUERY_MR),
	&UVERBS_METHOD(UVERBS_METHOD_REG_DMABUF_MR));

const struct uapi_definition uverbs_def_obj_mr[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(UVERBS_OBJECT_MR,
				      UAPI_DEF_OBJ_NEEDS_FN(dereg_mr)),
	{}
};
