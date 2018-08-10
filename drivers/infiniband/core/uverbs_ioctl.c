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

struct bundle_alloc_head {
	struct bundle_alloc_head *next;
	u8 data[];
};

struct bundle_priv {
	/* Must be first */
	struct bundle_alloc_head alloc_head;
	struct bundle_alloc_head *allocated_mem;
	size_t internal_avail;
	size_t internal_used;

	struct ib_uverbs_attr __user *user_attrs;
	struct ib_uverbs_attr *uattrs;
	struct uverbs_obj_attr *destroy_attr;

	/*
	 * Must be last. bundle ends in a flex array which overlaps
	 * internal_buffer.
	 */
	struct uverbs_attr_bundle bundle;
	u64 internal_buffer[32];
};

/**
 * uverbs_alloc() - Quickly allocate memory for use with a bundle
 * @bundle: The bundle
 * @size: Number of bytes to allocate
 * @flags: Allocator flags
 *
 * The bundle allocator is intended for allocations that are connected with
 * processing the system call related to the bundle. The allocated memory is
 * always freed once the system call completes, and cannot be freed any other
 * way.
 *
 * This tries to use a small pool of pre-allocated memory for performance.
 */
__malloc void *_uverbs_alloc(struct uverbs_attr_bundle *bundle, size_t size,
			     gfp_t flags)
{
	struct bundle_priv *pbundle =
		container_of(bundle, struct bundle_priv, bundle);
	size_t new_used;
	void *res;

	if (check_add_overflow(size, pbundle->internal_used, &new_used))
		return ERR_PTR(-EINVAL);

	if (new_used > pbundle->internal_avail) {
		struct bundle_alloc_head *buf;

		buf = kvmalloc(struct_size(buf, data, size), flags);
		if (!buf)
			return ERR_PTR(-ENOMEM);
		buf->next = pbundle->allocated_mem;
		pbundle->allocated_mem = buf;
		return buf->data;
	}

	res = (void *)pbundle->internal_buffer + pbundle->internal_used;
	pbundle->internal_used =
		ALIGN(new_used, sizeof(*pbundle->internal_buffer));
	if (flags & __GFP_ZERO)
		memset(res, 0, size);
	return res;
}
EXPORT_SYMBOL(_uverbs_alloc);

static bool uverbs_is_attr_cleared(const struct ib_uverbs_attr *uattr,
				   u16 len)
{
	if (uattr->len > sizeof(((struct ib_uverbs_attr *)0)->data))
		return ib_is_buffer_cleared(u64_to_user_ptr(uattr->data) + len,
					    uattr->len - len);

	return !memchr_inv((const void *)&uattr->data + len,
			   0, uattr->len - len);
}

static int uverbs_process_attr(struct bundle_priv *pbundle,
			       const struct ib_uverbs_attr *uattr,
			       u16 attr_id,
			       const struct uverbs_attr_spec_hash *attr_spec_bucket,
			       struct uverbs_attr_bundle_hash *attr_bundle_h,
			       struct ib_uverbs_attr __user *uattr_ptr)
{
	const struct uverbs_attr_spec *spec;
	const struct uverbs_attr_spec *val_spec;
	struct uverbs_attr *e;
	struct uverbs_obj_attr *o_attr;
	struct uverbs_attr *elements = attr_bundle_h->attrs;

	if (attr_id >= attr_spec_bucket->num_attrs) {
		if (uattr->flags & UVERBS_ATTR_F_MANDATORY)
			return -EINVAL;
		else
			return 0;
	}

	if (test_bit(attr_id, attr_bundle_h->valid_bitmap))
		return -EINVAL;

	spec = &attr_spec_bucket->attrs[attr_id];
	val_spec = spec;
	e = &elements[attr_id];

	switch (spec->type) {
	case UVERBS_ATTR_TYPE_ENUM_IN:
		if (uattr->attr_data.enum_data.elem_id >= spec->u.enum_def.num_elems)
			return -EOPNOTSUPP;

		if (uattr->attr_data.enum_data.reserved)
			return -EINVAL;

		val_spec = &spec->u2.enum_def.ids[uattr->attr_data.enum_data.elem_id];

		/* Currently we only support PTR_IN based enums */
		if (val_spec->type != UVERBS_ATTR_TYPE_PTR_IN)
			return -EOPNOTSUPP;

		e->ptr_attr.enum_id = uattr->attr_data.enum_data.elem_id;
	/* fall through */
	case UVERBS_ATTR_TYPE_PTR_IN:
		/* Ensure that any data provided by userspace beyond the known
		 * struct is zero. Userspace that knows how to use some future
		 * longer struct will fail here if used with an old kernel and
		 * non-zero content, making ABI compat/discovery simpler.
		 */
		if (uattr->len > val_spec->u.ptr.len &&
		    val_spec->zero_trailing &&
		    !uverbs_is_attr_cleared(uattr, val_spec->u.ptr.len))
			return -EOPNOTSUPP;

	/* fall through */
	case UVERBS_ATTR_TYPE_PTR_OUT:
		if (uattr->len < val_spec->u.ptr.min_len ||
		    (!val_spec->zero_trailing &&
		     uattr->len > val_spec->u.ptr.len))
			return -EINVAL;

		if (spec->type != UVERBS_ATTR_TYPE_ENUM_IN &&
		    uattr->attr_data.reserved)
			return -EINVAL;

		e->ptr_attr.uattr_idx = uattr - pbundle->uattrs;
		e->ptr_attr.len = uattr->len;

		if (val_spec->alloc_and_copy && !uverbs_attr_ptr_is_inline(e)) {
			void *p;

			p = uverbs_alloc(&pbundle->bundle, uattr->len);
			if (IS_ERR(p))
				return PTR_ERR(p);

			e->ptr_attr.ptr = p;

			if (copy_from_user(p, u64_to_user_ptr(uattr->data),
					   uattr->len))
				return -EFAULT;
		} else {
			e->ptr_attr.data = uattr->data;
		}
		break;

	case UVERBS_ATTR_TYPE_IDR:
	case UVERBS_ATTR_TYPE_FD:
		if (uattr->attr_data.reserved)
			return -EINVAL;

		if (uattr->len != 0)
			return -EINVAL;

		o_attr = &e->obj_attr;

		/* specs are allowed to have only one destroy attribute */
		WARN_ON(spec->u.obj.access == UVERBS_ACCESS_DESTROY &&
			pbundle->destroy_attr);
		if (spec->u.obj.access == UVERBS_ACCESS_DESTROY)
			pbundle->destroy_attr = o_attr;

		/*
		 * The type of uattr->data is u64 for UVERBS_ATTR_TYPE_IDR and
		 * s64 for UVERBS_ATTR_TYPE_FD. We can cast the u64 to s64
		 * here without caring about truncation as we know that the
		 * IDR implementation today rejects negative IDs
		 */
		o_attr->uobject = uverbs_get_uobject_from_file(
					spec->u.obj.obj_type,
					pbundle->bundle.ufile,
					spec->u.obj.access,
					uattr->data_s64);

		if (IS_ERR(o_attr->uobject))
			return PTR_ERR(o_attr->uobject);

		if (spec->u.obj.access == UVERBS_ACCESS_NEW) {
			s64 id = o_attr->uobject->id;

			/* Copy the allocated id to the user-space */
			if (put_user(id, &uattr_ptr->data)) {
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

static int uverbs_finalize_attrs(struct bundle_priv *pbundle,
				 struct uverbs_attr_spec_hash *const *spec_hash,
				 size_t num, bool commit)
{
	struct uverbs_attr_bundle *attrs_bundle = &pbundle->bundle;
	unsigned int i;
	int ret = 0;

	for (i = 0; i < num; i++) {
		struct uverbs_attr_bundle_hash *curr_bundle =
			&attrs_bundle->hash[i];
		const struct uverbs_attr_spec_hash *curr_spec_bucket =
			spec_hash[i];
		unsigned int j;

		if (!curr_spec_bucket)
			continue;

		for (j = 0; j < curr_bundle->num_attrs; j++) {
			struct uverbs_attr *attr;
			const struct uverbs_attr_spec *spec;

			if (!uverbs_attr_is_valid_in_hash(curr_bundle, j))
				continue;

			attr = &curr_bundle->attrs[j];
			spec = &curr_spec_bucket->attrs[j];

			if (spec->type == UVERBS_ATTR_TYPE_IDR ||
			    spec->type == UVERBS_ATTR_TYPE_FD) {
				int current_ret;

				current_ret = uverbs_finalize_object(
					attr->obj_attr.uobject,
					spec->u.obj.access, commit);
				if (!ret)
					ret = current_ret;
			}
		}
	}
	return ret;
}

static int uverbs_uattrs_process(size_t num_uattrs,
				 const struct uverbs_method_spec *method,
				 struct bundle_priv *pbundle)
{
	struct uverbs_attr_bundle *attr_bundle = &pbundle->bundle;
	struct ib_uverbs_attr __user *uattr_ptr = pbundle->user_attrs;
	size_t i;
	int ret = 0;
	int num_given_buckets = 0;

	for (i = 0; i < num_uattrs; i++) {
		const struct ib_uverbs_attr *uattr = &pbundle->uattrs[i];
		u16 attr_id = uattr->attr_id;
		struct uverbs_attr_spec_hash *attr_spec_bucket;

		ret = uverbs_ns_idx(&attr_id, method->num_buckets);
		if (ret < 0 || !method->attr_buckets[ret]) {
			if (uattr->flags & UVERBS_ATTR_F_MANDATORY) {
				uverbs_finalize_attrs(pbundle,
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
		ret = uverbs_process_attr(pbundle,
					  uattr, attr_id,
					  attr_spec_bucket,
					  &attr_bundle->hash[ret],
					  uattr_ptr++);
		if (ret) {
			uverbs_finalize_attrs(pbundle,
					      method->attr_buckets,
					      num_given_buckets,
					      false);
			return ret;
		}
	}

	return num_given_buckets;
}

static int uverbs_validate_kernel_mandatory(const struct uverbs_method_spec *method_spec,
					    struct bundle_priv *pbundle)
{
	struct uverbs_attr_bundle *attr_bundle = &pbundle->bundle;
	unsigned int i;

	for (i = 0; i < attr_bundle->num_buckets; i++) {
		struct uverbs_attr_spec_hash *attr_spec_bucket =
			method_spec->attr_buckets[i];

		if (!attr_spec_bucket)
			continue;

		if (!bitmap_subset(attr_spec_bucket->mandatory_attrs_bitmask,
				   attr_bundle->hash[i].valid_bitmap,
				   attr_spec_bucket->num_attrs))
			return -EINVAL;
	}

	for (; i < method_spec->num_buckets; i++) {
		struct uverbs_attr_spec_hash *attr_spec_bucket =
			method_spec->attr_buckets[i];

		if (!bitmap_empty(attr_spec_bucket->mandatory_attrs_bitmask,
				  attr_spec_bucket->num_attrs))
			return -EINVAL;
	}

	return 0;
}

static int uverbs_handle_method(size_t num_uattrs,
				const struct uverbs_method_spec *method_spec,
				struct bundle_priv *pbundle)
{
	struct uverbs_attr_bundle *attr_bundle = &pbundle->bundle;
	int ret;
	int finalize_ret;
	int num_given_buckets;

	num_given_buckets =
		uverbs_uattrs_process(num_uattrs, method_spec, pbundle);
	if (num_given_buckets <= 0)
		return -EINVAL;

	attr_bundle->num_buckets = num_given_buckets;
	ret = uverbs_validate_kernel_mandatory(method_spec, pbundle);
	if (ret)
		goto cleanup;

	/*
	 * We destroy the HW object before invoking the handler, handlers do
	 * not get to manipulate the HW objects.
	 */
	if (pbundle->destroy_attr) {
		ret = uobj_destroy(pbundle->destroy_attr->uobject);
		if (ret)
			goto cleanup;
	}

	ret = method_spec->handler(pbundle->bundle.ufile, attr_bundle);

	if (pbundle->destroy_attr) {
		uobj_put_destroy(pbundle->destroy_attr->uobject);
		pbundle->destroy_attr->uobject = NULL;
	}

cleanup:
	finalize_ret = uverbs_finalize_attrs(pbundle,
					     method_spec->attr_buckets,
					     attr_bundle->num_buckets,
					     !ret);

	return ret ? ret : finalize_ret;
}

static void bundle_destroy(struct bundle_priv *pbundle)
{
	struct bundle_alloc_head *memblock;

	for (memblock = pbundle->allocated_mem; memblock;) {
		struct bundle_alloc_head *tmp = memblock;

		memblock = memblock->next;
		kvfree(tmp);
	}
}

static long ib_uverbs_cmd_verbs(struct ib_device *ib_dev,
				struct ib_uverbs_file *file,
				struct ib_uverbs_ioctl_hdr *hdr,
				struct ib_uverbs_attr __user *user_attrs)
{
	const struct uverbs_object_spec *object_spec;
	const struct uverbs_method_spec *method_spec;
	long err = 0;
	unsigned int i;
	struct bundle_priv onstack_pbundle;
	struct bundle_priv *ctx;
	struct uverbs_attr *curr_attr;
	unsigned long *curr_bitmap;
	size_t ctx_size;

	if (hdr->driver_id != ib_dev->driver_id)
		return -EINVAL;

	object_spec = uverbs_get_object(file, hdr->object_id);
	if (!object_spec)
		return -EPROTONOSUPPORT;

	method_spec = uverbs_get_method(object_spec, hdr->method_id);
	if (!method_spec)
		return -EPROTONOSUPPORT;

	ctx_size = sizeof(*ctx) - sizeof(ctx->internal_buffer) +
		   sizeof(struct uverbs_attr_bundle_hash) * method_spec->num_buckets +
		   sizeof(*ctx->uattrs) * hdr->num_attrs +
		   sizeof(*ctx->bundle.hash[0].attrs) *
		   method_spec->num_child_attrs +
		   sizeof(*ctx->bundle.hash[0].valid_bitmap) *
			(method_spec->num_child_attrs / BITS_PER_LONG +
			 method_spec->num_buckets);

	if (ctx_size <= sizeof(onstack_pbundle)) {
		ctx = &onstack_pbundle;
		ctx->internal_avail =
			sizeof(onstack_pbundle) -
			offsetof(struct bundle_priv, internal_buffer);
		ctx->allocated_mem = NULL;
	} else {
		ctx = kmalloc(ctx_size, GFP_KERNEL);
		if (!ctx)
			return -ENOMEM;
		ctx->internal_avail = 0;
		ctx->alloc_head.next = NULL;
		ctx->allocated_mem = &ctx->alloc_head;
	}

	ctx->uattrs = (void *)(ctx + 1) +
		      (sizeof(ctx->bundle.hash[0]) * method_spec->num_buckets);
	curr_attr = (void *)(ctx->uattrs + hdr->num_attrs);
	curr_bitmap = (void *)(curr_attr + method_spec->num_child_attrs);
	ctx->internal_used = ALIGN(ctx_size, sizeof(*ctx->internal_buffer));

	/*
	 * We just fill the pointers and num_attrs here. The data itself will be
	 * filled at a later stage (uverbs_process_attr)
	 */
	for (i = 0; i < method_spec->num_buckets; i++) {
		unsigned int curr_num_attrs;

		if (!method_spec->attr_buckets[i])
			continue;

		curr_num_attrs = method_spec->attr_buckets[i]->num_attrs;

		ctx->bundle.hash[i].attrs = curr_attr;
		curr_attr += curr_num_attrs;
		ctx->bundle.hash[i].num_attrs = curr_num_attrs;
		ctx->bundle.hash[i].valid_bitmap = curr_bitmap;
		bitmap_zero(curr_bitmap, curr_num_attrs);
		curr_bitmap += BITS_TO_LONGS(curr_num_attrs);
	}

	err = copy_from_user(ctx->uattrs, user_attrs,
			     sizeof(*ctx->uattrs) * hdr->num_attrs);
	if (err) {
		err = -EFAULT;
		goto out;
	}

	ctx->destroy_attr = NULL;
	ctx->bundle.ufile = file;
	ctx->user_attrs = user_attrs;
	err = uverbs_handle_method(hdr->num_attrs, method_spec, ctx);

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
	bundle_destroy(ctx);
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

		if (hdr.reserved1 || hdr.reserved2) {
			err = -EPROTONOSUPPORT;
			goto out;
		}

		err = ib_uverbs_cmd_verbs(ib_dev, file, &hdr, user_hdr->attrs);
	} else {
		err = -ENOIOCTLCMD;
	}
out:
	srcu_read_unlock(&file->device->disassociate_srcu, srcu_key);

	return err;
}

int uverbs_get_flags64(u64 *to, const struct uverbs_attr_bundle *attrs_bundle,
		       size_t idx, u64 allowed_bits)
{
	const struct uverbs_attr *attr;
	u64 flags;

	attr = uverbs_attr_get(attrs_bundle, idx);
	/* Missing attribute means 0 flags */
	if (IS_ERR(attr)) {
		*to = 0;
		return 0;
	}

	/*
	 * New userspace code should use 8 bytes to pass flags, but we
	 * transparently support old userspaces that were using 4 bytes as
	 * well.
	 */
	if (attr->ptr_attr.len == 8)
		flags = attr->ptr_attr.data;
	else if (attr->ptr_attr.len == 4)
		flags = *(u32 *)&attr->ptr_attr.data;
	else
		return -EINVAL;

	if (flags & ~allowed_bits)
		return -EINVAL;

	*to = flags;
	return 0;
}
EXPORT_SYMBOL(uverbs_get_flags64);

int uverbs_get_flags32(u32 *to, const struct uverbs_attr_bundle *attrs_bundle,
		       size_t idx, u64 allowed_bits)
{
	u64 flags;
	int ret;

	ret = uverbs_get_flags64(&flags, attrs_bundle, idx, allowed_bits);
	if (ret)
		return ret;

	if (flags > U32_MAX)
		return -EINVAL;
	*to = flags;

	return 0;
}
EXPORT_SYMBOL(uverbs_get_flags32);

/*
 * This is for ease of conversion. The purpose is to convert all drivers to
 * use uverbs_attr_bundle instead of ib_udata.  Assume attr == 0 is input and
 * attr == 1 is output.
 */
void create_udata(struct uverbs_attr_bundle *bundle, struct ib_udata *udata)
{
	struct bundle_priv *pbundle =
		container_of(bundle, struct bundle_priv, bundle);
	const struct uverbs_attr *uhw_in =
		uverbs_attr_get(bundle, UVERBS_ATTR_UHW_IN);
	const struct uverbs_attr *uhw_out =
		uverbs_attr_get(bundle, UVERBS_ATTR_UHW_OUT);

	if (!IS_ERR(uhw_in)) {
		udata->inlen = uhw_in->ptr_attr.len;
		if (uverbs_attr_ptr_is_inline(uhw_in))
			udata->inbuf =
				&pbundle->user_attrs[uhw_in->ptr_attr.uattr_idx]
					 .data;
		else
			udata->inbuf = u64_to_user_ptr(uhw_in->ptr_attr.data);
	} else {
		udata->inbuf = NULL;
		udata->inlen = 0;
	}

	if (!IS_ERR(uhw_out)) {
		udata->outbuf = u64_to_user_ptr(uhw_out->ptr_attr.data);
		udata->outlen = uhw_out->ptr_attr.len;
	} else {
		udata->outbuf = NULL;
		udata->outlen = 0;
	}
}

int uverbs_copy_to(const struct uverbs_attr_bundle *bundle, size_t idx,
		   const void *from, size_t size)
{
	struct bundle_priv *pbundle =
		container_of(bundle, struct bundle_priv, bundle);
	const struct uverbs_attr *attr = uverbs_attr_get(bundle, idx);
	u16 flags;
	size_t min_size;

	if (IS_ERR(attr))
		return PTR_ERR(attr);

	min_size = min_t(size_t, attr->ptr_attr.len, size);
	if (copy_to_user(u64_to_user_ptr(attr->ptr_attr.data), from, min_size))
		return -EFAULT;

	flags = pbundle->uattrs[attr->ptr_attr.uattr_idx].flags |
		UVERBS_ATTR_F_VALID_OUTPUT;
	if (put_user(flags,
		     &pbundle->user_attrs[attr->ptr_attr.uattr_idx].flags))
		return -EFAULT;

	return 0;
}
EXPORT_SYMBOL(uverbs_copy_to);
