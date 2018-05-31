/*
 * Copyright (c) 2017, Mellanox Technologies inc.  All rights reserved.
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

#include <rdma/rdma_user_ioctl.h>
#include <rdma/uverbs_ioctl.h>
#include "rdma_core.h"
#include "uverbs.h"

static int uverbs_process_attr(struct ib_device *ibdev,
			       struct ib_ucontext *ucontext,
			       const struct ib_uverbs_attr *uattr,
			       u16 attr_id,
			       const struct uverbs_attr_spec_hash *attr_spec_bucket,
			       struct uverbs_attr_bundle_hash *attr_bundle_h,
			       struct ib_uverbs_attr __user *uattr_ptr)
{
	const struct uverbs_attr_spec *spec;
	struct uverbs_attr *e;
	const struct uverbs_object_spec *object;
	struct uverbs_obj_attr *o_attr;
	struct uverbs_attr *elements = attr_bundle_h->attrs;

	if (uattr->reserved)
		return -EINVAL;

	if (attr_id >= attr_spec_bucket->num_attrs) {
		if (uattr->flags & UVERBS_ATTR_F_MANDATORY)
			return -EINVAL;
		else
			return 0;
	}

	if (test_bit(attr_id, attr_bundle_h->valid_bitmap))
		return -EINVAL;

	spec = &attr_spec_bucket->attrs[attr_id];
	e = &elements[attr_id];
	e->uattr = uattr_ptr;

	switch (spec->type) {
	case UVERBS_ATTR_TYPE_PTR_IN:
	case UVERBS_ATTR_TYPE_PTR_OUT:
		if (uattr->len < spec->len ||
		    (!(spec->flags & UVERBS_ATTR_SPEC_F_MIN_SZ) &&
		     uattr->len > spec->len))
			return -EINVAL;

		e->ptr_attr.data = uattr->data;
		e->ptr_attr.len = uattr->len;
		e->ptr_attr.flags = uattr->flags;
		break;

	case UVERBS_ATTR_TYPE_IDR:
		if (uattr->data >> 32)
			return -EINVAL;
	/* fall through */
	case UVERBS_ATTR_TYPE_FD:
		if (uattr->len != 0 || !ucontext || uattr->data > INT_MAX)
			return -EINVAL;

		o_attr = &e->obj_attr;
		object = uverbs_get_object(ibdev, spec->obj.obj_type);
		if (!object)
			return -EINVAL;
		o_attr->type = object->type_attrs;

		o_attr->id = (int)uattr->data;
		o_attr->uobject = uverbs_get_uobject_from_context(
					o_attr->type,
					ucontext,
					spec->obj.access,
					o_attr->id);

		if (IS_ERR(o_attr->uobject))
			return PTR_ERR(o_attr->uobject);

		if (spec->obj.access == UVERBS_ACCESS_NEW) {
			u64 id = o_attr->uobject->id;

			/* Copy the allocated id to the user-space */
			if (put_user(id, &e->uattr->data)) {
				uverbs_finalize_object(o_attr->uobject,
						       UVERBS_ACCESS_NEW,
						       false);
				return -EFAULT;
			}
		}

		break;
	default:
		return -EOPNOTSUPP;
	}

	set_bit(attr_id, attr_bundle_h->valid_bitmap);
	return 0;
}

static int uverbs_uattrs_process(struct ib_device *ibdev,
				 struct ib_ucontext *ucontext,
				 const struct ib_uverbs_attr *uattrs,
				 size_t num_uattrs,
				 const struct uverbs_method_spec *method,
				 struct uverbs_attr_bundle *attr_bundle,
				 struct ib_uverbs_attr __user *uattr_ptr)
{
	size_t i;
	int ret = 0;
	int num_given_buckets = 0;

	for (i = 0; i < num_uattrs; i++) {
		const struct ib_uverbs_attr *uattr = &uattrs[i];
		u16 attr_id = uattr->attr_id;
		struct uverbs_attr_spec_hash *attr_spec_bucket;

		ret = uverbs_ns_idx(&attr_id, method->num_buckets);
		if (ret < 0) {
			if (uattr->flags & UVERBS_ATTR_F_MANDATORY) {
				uverbs_finalize_objects(attr_bundle,
							method->attr_buckets,
							num_given_buckets,
							false);
				return ret;
			}
			continue;
		}

		/*
		 * ret is the found ns, so increase num_given_buckets if
		 * necessary.
		 */
		if (ret >= num_given_buckets)
			num_given_buckets = ret + 1;

		attr_spec_bucket = method->attr_buckets[ret];
		ret = uverbs_process_attr(ibdev, ucontext, uattr, attr_id,
					  attr_spec_bucket, &attr_bundle->hash[ret],
					  uattr_ptr++);
		if (ret) {
			uverbs_finalize_objects(attr_bundle,
						method->attr_buckets,
						num_given_buckets,
						false);
			return ret;
		}
	}

	return num_given_buckets;
}

static int uverbs_validate_kernel_mandatory(const struct uverbs_method_spec *method_spec,
					    struct uverbs_attr_bundle *attr_bundle)
{
	unsigned int i;

	for (i = 0; i < attr_bundle->num_buckets; i++) {
		struct uverbs_attr_spec_hash *attr_spec_bucket =
			method_spec->attr_buckets[i];

		if (!bitmap_subset(attr_spec_bucket->mandatory_attrs_bitmask,
				   attr_bundle->hash[i].valid_bitmap,
				   attr_spec_bucket->num_attrs))
			return -EINVAL;
	}

	return 0;
}

static int uverbs_handle_method(struct ib_uverbs_attr __user *uattr_ptr,
				const struct ib_uverbs_attr *uattrs,
				size_t num_uattrs,
				struct ib_device *ibdev,
				struct ib_uverbs_file *ufile,
				const struct uverbs_method_spec *method_spec,
				struct uverbs_attr_bundle *attr_bundle)
{
	int ret;
	int finalize_ret;
	int num_given_buckets;

	num_given_buckets = uverbs_uattrs_process(ibdev, ufile->ucontext, uattrs,
						  num_uattrs, method_spec,
						  attr_bundle, uattr_ptr);
	if (num_given_buckets <= 0)
		return -EINVAL;

	attr_bundle->num_buckets = num_given_buckets;
	ret = uverbs_validate_kernel_mandatory(method_spec, attr_bundle);
	if (ret)
		goto cleanup;

	ret = method_spec->handler(ibdev, ufile, attr_bundle);
cleanup:
	finalize_ret = uverbs_finalize_objects(attr_bundle,
					       method_spec->attr_buckets,
					       attr_bundle->num_buckets,
					       !ret);

	return ret ? ret : finalize_ret;
}

#define UVERBS_OPTIMIZE_USING_STACK_SZ  256
static long ib_uverbs_cmd_verbs(struct ib_device *ib_dev,
				struct ib_uverbs_file *file,
				struct ib_uverbs_ioctl_hdr *hdr,
				void __user *buf)
{
	const struct uverbs_object_spec *object_spec;
	const struct uverbs_method_spec *method_spec;
	long err = 0;
	unsigned int i;
	struct {
		struct ib_uverbs_attr		*uattrs;
		struct uverbs_attr_bundle	*uverbs_attr_bundle;
	} *ctx = NULL;
	struct uverbs_attr *curr_attr;
	unsigned long *curr_bitmap;
	size_t ctx_size;
#ifdef UVERBS_OPTIMIZE_USING_STACK_SZ
	uintptr_t data[UVERBS_OPTIMIZE_USING_STACK_SZ / sizeof(uintptr_t)];
#endif

	object_spec = uverbs_get_object(ib_dev, hdr->object_id);
	if (!object_spec)
		return -EPROTONOSUPPORT;

	method_spec = uverbs_get_method(object_spec, hdr->method_id);
	if (!method_spec)
		return -EPROTONOSUPPORT;

	if ((method_spec->flags & UVERBS_ACTION_FLAG_CREATE_ROOT) ^ !file->ucontext)
		return -EINVAL;

	ctx_size = sizeof(*ctx) +
		   sizeof(struct uverbs_attr_bundle) +
		   sizeof(struct uverbs_attr_bundle_hash) * method_spec->num_buckets +
		   sizeof(*ctx->uattrs) * hdr->num_attrs +
		   sizeof(*ctx->uverbs_attr_bundle->hash[0].attrs) *
		   method_spec->num_child_attrs +
		   sizeof(*ctx->uverbs_attr_bundle->hash[0].valid_bitmap) *
			(method_spec->num_child_attrs / BITS_PER_LONG +
			 method_spec->num_buckets);

#ifdef UVERBS_OPTIMIZE_USING_STACK_SZ
	if (ctx_size <= UVERBS_OPTIMIZE_USING_STACK_SZ)
		ctx = (void *)data;

	if (!ctx)
#endif
	ctx = kmalloc(ctx_size, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->uverbs_attr_bundle = (void *)ctx + sizeof(*ctx);
	ctx->uattrs = (void *)(ctx->uverbs_attr_bundle + 1) +
			      (sizeof(ctx->uverbs_attr_bundle->hash[0]) *
			       method_spec->num_buckets);
	curr_attr = (void *)(ctx->uattrs + hdr->num_attrs);
	curr_bitmap = (void *)(curr_attr + method_spec->num_child_attrs);

	/*
	 * We just fill the pointers and num_attrs here. The data itself will be
	 * filled at a later stage (uverbs_process_attr)
	 */
	for (i = 0; i < method_spec->num_buckets; i++) {
		unsigned int curr_num_attrs = method_spec->attr_buckets[i]->num_attrs;

		ctx->uverbs_attr_bundle->hash[i].attrs = curr_attr;
		curr_attr += curr_num_attrs;
		ctx->uverbs_attr_bundle->hash[i].num_attrs = curr_num_attrs;
		ctx->uverbs_attr_bundle->hash[i].valid_bitmap = curr_bitmap;
		bitmap_zero(curr_bitmap, curr_num_attrs);
		curr_bitmap += BITS_TO_LONGS(curr_num_attrs);
	}

	err = copy_from_user(ctx->uattrs, buf,
			     sizeof(*ctx->uattrs) * hdr->num_attrs);
	if (err) {
		err = -EFAULT;
		goto out;
	}

	err = uverbs_handle_method(buf, ctx->uattrs, hdr->num_attrs, ib_dev,
				   file, method_spec, ctx->uverbs_attr_bundle);

	/*
	 * EPROTONOSUPPORT is ONLY to be returned if the ioctl framework can
	 * not invoke the method because the request is not supported.  No
	 * other cases should return this code.
	*/
	if (unlikely(err == -EPROTONOSUPPORT)) {
		WARN_ON_ONCE(err == -EPROTONOSUPPORT);
		err = -EINVAL;
	}
out:
#ifdef UVERBS_OPTIMIZE_USING_STACK_SZ
	if (ctx_size > UVERBS_OPTIMIZE_USING_STACK_SZ)
#endif
	kfree(ctx);
	return err;
}

#define IB_UVERBS_MAX_CMD_SZ 4096

long ib_uverbs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ib_uverbs_file *file = filp->private_data;
	struct ib_uverbs_ioctl_hdr __user *user_hdr =
		(struct ib_uverbs_ioctl_hdr __user *)arg;
	struct ib_uverbs_ioctl_hdr hdr;
	struct ib_device *ib_dev;
	int srcu_key;
	long err;

	srcu_key = srcu_read_lock(&file->device->disassociate_srcu);
	ib_dev = srcu_dereference(file->device->ib_dev,
				  &file->device->disassociate_srcu);
	if (!ib_dev) {
		err = -EIO;
		goto out;
	}

	if (cmd == RDMA_VERBS_IOCTL) {
		err = copy_from_user(&hdr, user_hdr, sizeof(hdr));

		if (err || hdr.length > IB_UVERBS_MAX_CMD_SZ ||
		    hdr.length != sizeof(hdr) + hdr.num_attrs * sizeof(struct ib_uverbs_attr)) {
			err = -EINVAL;
			goto out;
		}

		if (hdr.reserved) {
			err = -EPROTONOSUPPORT;
			goto out;
		}

		err = ib_uverbs_cmd_verbs(ib_dev, file, &hdr,
					  (__user void *)arg + sizeof(hdr));
	} else {
		err = -ENOIOCTLCMD;
	}
out:
	srcu_read_unlock(&file->device->disassociate_srcu, srcu_key);

	return err;
}
