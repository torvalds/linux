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

	struct radix_tree_root *radix;
	const struct uverbs_api_ioctl_method *method_elm;
	void __rcu **radix_slots;
	unsigned long radix_slots_len;
	u32 method_key;

	struct ib_uverbs_attr __user *user_attrs;
	struct ib_uverbs_attr *uattrs;

	DECLARE_BITMAP(uobj_finalize, UVERBS_API_ATTR_BKEY_LEN);
	DECLARE_BITMAP(spec_finalize, UVERBS_API_ATTR_BKEY_LEN);

	/*
	 * Must be last. bundle ends in a flex array which overlaps
	 * internal_buffer.
	 */
	struct uverbs_attr_bundle bundle;
	u64 internal_buffer[32];
};

/*
 * Each method has an absolute minimum amount of memory it needs to allocate,
 * precompute that amount and determine if the onstack memory can be used or
 * if allocation is need.
 */
void uapi_compute_bundle_size(struct uverbs_api_ioctl_method *method_elm,
			      unsigned int num_attrs)
{
	struct bundle_priv *pbundle;
	size_t bundle_size =
		offsetof(struct bundle_priv, internal_buffer) +
		sizeof(*pbundle->bundle.attrs) * method_elm->key_bitmap_len +
		sizeof(*pbundle->uattrs) * num_attrs;

	method_elm->use_stack = bundle_size <= sizeof(*pbundle);
	method_elm->bundle_size =
		ALIGN(bundle_size + 256, sizeof(*pbundle->internal_buffer));

	/* Do not want order-2 allocations for this. */
	WARN_ON_ONCE(method_elm->bundle_size > PAGE_SIZE);
}

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
		return ERR_PTR(-EOVERFLOW);

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

static int uverbs_set_output(const struct uverbs_attr_bundle *bundle,
			     const struct uverbs_attr *attr)
{
	struct bundle_priv *pbundle =
		container_of(bundle, struct bundle_priv, bundle);
	u16 flags;

	flags = pbundle->uattrs[attr->ptr_attr.uattr_idx].flags |
		UVERBS_ATTR_F_VALID_OUTPUT;
	if (put_user(flags,
		     &pbundle->user_attrs[attr->ptr_attr.uattr_idx].flags))
		return -EFAULT;
	return 0;
}

static int uverbs_process_idrs_array(struct bundle_priv *pbundle,
				     const struct uverbs_api_attr *attr_uapi,
				     struct uverbs_objs_arr_attr *attr,
				     struct ib_uverbs_attr *uattr,
				     u32 attr_bkey)
{
	const struct uverbs_attr_spec *spec = &attr_uapi->spec;
	size_t array_len;
	u32 *idr_vals;
	int ret = 0;
	size_t i;

	if (uattr->attr_data.reserved)
		return -EINVAL;

	if (uattr->len % sizeof(u32))
		return -EINVAL;

	array_len = uattr->len / sizeof(u32);
	if (array_len < spec->u2.objs_arr.min_len ||
	    array_len > spec->u2.objs_arr.max_len)
		return -EINVAL;

	attr->uobjects =
		uverbs_alloc(&pbundle->bundle,
			     array_size(array_len, sizeof(*attr->uobjects)));
	if (IS_ERR(attr->uobjects))
		return PTR_ERR(attr->uobjects);

	/*
	 * Since idr is 4B and *uobjects is >= 4B, we can use attr->uobjects
	 * to store idrs array and avoid additional memory allocation. The
	 * idrs array is offset to the end of the uobjects array so we will be
	 * able to read idr and replace with a pointer.
	 */
	idr_vals = (u32 *)(attr->uobjects + array_len) - array_len;

	if (uattr->len > sizeof(uattr->data)) {
		ret = copy_from_user(idr_vals, u64_to_user_ptr(uattr->data),
				     uattr->len);
		if (ret)
			return -EFAULT;
	} else {
		memcpy(idr_vals, &uattr->data, uattr->len);
	}

	for (i = 0; i != array_len; i++) {
		attr->uobjects[i] = uverbs_get_uobject_from_file(
			spec->u2.objs_arr.obj_type, spec->u2.objs_arr.access,
			idr_vals[i], &pbundle->bundle);
		if (IS_ERR(attr->uobjects[i])) {
			ret = PTR_ERR(attr->uobjects[i]);
			break;
		}
	}

	attr->len = i;
	__set_bit(attr_bkey, pbundle->spec_finalize);
	return ret;
}

static int uverbs_free_idrs_array(const struct uverbs_api_attr *attr_uapi,
				  struct uverbs_objs_arr_attr *attr,
				  bool commit, struct uverbs_attr_bundle *attrs)
{
	const struct uverbs_attr_spec *spec = &attr_uapi->spec;
	int current_ret;
	int ret = 0;
	size_t i;

	for (i = 0; i != attr->len; i++) {
		current_ret = uverbs_finalize_object(attr->uobjects[i],
						     spec->u2.objs_arr.access,
						     commit, attrs);
		if (!ret)
			ret = current_ret;
	}

	return ret;
}

static int uverbs_process_attr(struct bundle_priv *pbundle,
			       const struct uverbs_api_attr *attr_uapi,
			       struct ib_uverbs_attr *uattr, u32 attr_bkey)
{
	const struct uverbs_attr_spec *spec = &attr_uapi->spec;
	struct uverbs_attr *e = &pbundle->bundle.attrs[attr_bkey];
	const struct uverbs_attr_spec *val_spec = spec;
	struct uverbs_obj_attr *o_attr;

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
		o_attr->attr_elm = attr_uapi;

		/*
		 * The type of uattr->data is u64 for UVERBS_ATTR_TYPE_IDR and
		 * s64 for UVERBS_ATTR_TYPE_FD. We can cast the u64 to s64
		 * here without caring about truncation as we know that the
		 * IDR implementation today rejects negative IDs
		 */
		o_attr->uobject = uverbs_get_uobject_from_file(
			spec->u.obj.obj_type, spec->u.obj.access,
			uattr->data_s64, &pbundle->bundle);
		if (IS_ERR(o_attr->uobject))
			return PTR_ERR(o_attr->uobject);
		__set_bit(attr_bkey, pbundle->uobj_finalize);

		if (spec->u.obj.access == UVERBS_ACCESS_NEW) {
			unsigned int uattr_idx = uattr - pbundle->uattrs;
			s64 id = o_attr->uobject->id;

			/* Copy the allocated id to the user-space */
			if (put_user(id, &pbundle->user_attrs[uattr_idx].data))
				return -EFAULT;
		}

		break;

	case UVERBS_ATTR_TYPE_IDRS_ARRAY:
		return uverbs_process_idrs_array(pbundle, attr_uapi,
						 &e->objs_arr_attr, uattr,
						 attr_bkey);
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

/*
 * We search the radix tree with the method prefix and now we want to fast
 * search the suffix bits to get a particular attribute pointer. It is not
 * totally clear to me if this breaks the radix tree encasulation or not, but
 * it uses the iter data to determine if the method iter points at the same
 * chunk that will store the attribute, if so it just derefs it directly. By
 * construction in most kernel configs the method and attrs will all fit in a
 * single radix chunk, so in most cases this will have no search. Other cases
 * this falls back to a full search.
 */
static void __rcu **uapi_get_attr_for_method(struct bundle_priv *pbundle,
					     u32 attr_key)
{
	void __rcu **slot;

	if (likely(attr_key < pbundle->radix_slots_len)) {
		void *entry;

		slot = pbundle->radix_slots + attr_key;
		entry = rcu_dereference_raw(*slot);
		if (likely(!radix_tree_is_internal_node(entry) && entry))
			return slot;
	}

	return radix_tree_lookup_slot(pbundle->radix,
				      pbundle->method_key | attr_key);
}

static int uverbs_set_attr(struct bundle_priv *pbundle,
			   struct ib_uverbs_attr *uattr)
{
	u32 attr_key = uapi_key_attr(uattr->attr_id);
	u32 attr_bkey = uapi_bkey_attr(attr_key);
	const struct uverbs_api_attr *attr;
	void __rcu **slot;
	int ret;

	slot = uapi_get_attr_for_method(pbundle, attr_key);
	if (!slot) {
		/*
		 * Kernel does not support the attribute but user-space says it
		 * is mandatory
		 */
		if (uattr->flags & UVERBS_ATTR_F_MANDATORY)
			return -EPROTONOSUPPORT;
		return 0;
	}
	attr = rcu_dereference_protected(*slot, true);

	/* Reject duplicate attributes from user-space */
	if (test_bit(attr_bkey, pbundle->bundle.attr_present))
		return -EINVAL;

	ret = uverbs_process_attr(pbundle, attr, uattr, attr_bkey);
	if (ret)
		return ret;

	__set_bit(attr_bkey, pbundle->bundle.attr_present);

	return 0;
}

static int ib_uverbs_run_method(struct bundle_priv *pbundle,
				unsigned int num_attrs)
{
	int (*handler)(struct uverbs_attr_bundle *attrs);
	size_t uattrs_size = array_size(sizeof(*pbundle->uattrs), num_attrs);
	unsigned int destroy_bkey = pbundle->method_elm->destroy_bkey;
	unsigned int i;
	int ret;

	/* See uverbs_disassociate_api() */
	handler = srcu_dereference(
		pbundle->method_elm->handler,
		&pbundle->bundle.ufile->device->disassociate_srcu);
	if (!handler)
		return -EIO;

	pbundle->uattrs = uverbs_alloc(&pbundle->bundle, uattrs_size);
	if (IS_ERR(pbundle->uattrs))
		return PTR_ERR(pbundle->uattrs);
	if (copy_from_user(pbundle->uattrs, pbundle->user_attrs, uattrs_size))
		return -EFAULT;

	for (i = 0; i != num_attrs; i++) {
		ret = uverbs_set_attr(pbundle, &pbundle->uattrs[i]);
		if (unlikely(ret))
			return ret;
	}

	/* User space did not provide all the mandatory attributes */
	if (unlikely(!bitmap_subset(pbundle->method_elm->attr_mandatory,
				    pbundle->bundle.attr_present,
				    pbundle->method_elm->key_bitmap_len)))
		return -EINVAL;

	if (pbundle->method_elm->has_udata)
		uverbs_fill_udata(&pbundle->bundle,
				  &pbundle->bundle.driver_udata,
				  UVERBS_ATTR_UHW_IN, UVERBS_ATTR_UHW_OUT);

	if (destroy_bkey != UVERBS_API_ATTR_BKEY_LEN) {
		struct uverbs_obj_attr *destroy_attr =
			&pbundle->bundle.attrs[destroy_bkey].obj_attr;

		ret = uobj_destroy(destroy_attr->uobject, &pbundle->bundle);
		if (ret)
			return ret;
		__clear_bit(destroy_bkey, pbundle->uobj_finalize);

		ret = handler(&pbundle->bundle);
		uobj_put_destroy(destroy_attr->uobject);
	} else {
		ret = handler(&pbundle->bundle);
	}

	/*
	 * Until the drivers are revised to use the bundle directly we have to
	 * assume that the driver wrote to its UHW_OUT and flag userspace
	 * appropriately.
	 */
	if (!ret && pbundle->method_elm->has_udata) {
		const struct uverbs_attr *attr =
			uverbs_attr_get(&pbundle->bundle, UVERBS_ATTR_UHW_OUT);

		if (!IS_ERR(attr))
			ret = uverbs_set_output(&pbundle->bundle, attr);
	}

	/*
	 * EPROTONOSUPPORT is ONLY to be returned if the ioctl framework can
	 * not invoke the method because the request is not supported.  No
	 * other cases should return this code.
	 */
	if (WARN_ON_ONCE(ret == -EPROTONOSUPPORT))
		return -EINVAL;

	return ret;
}

static int bundle_destroy(struct bundle_priv *pbundle, bool commit)
{
	unsigned int key_bitmap_len = pbundle->method_elm->key_bitmap_len;
	struct bundle_alloc_head *memblock;
	unsigned int i;
	int ret = 0;

	/* fast path for simple uobjects */
	i = -1;
	while ((i = find_next_bit(pbundle->uobj_finalize, key_bitmap_len,
				  i + 1)) < key_bitmap_len) {
		struct uverbs_attr *attr = &pbundle->bundle.attrs[i];
		int current_ret;

		current_ret = uverbs_finalize_object(
			attr->obj_attr.uobject,
			attr->obj_attr.attr_elm->spec.u.obj.access, commit,
			&pbundle->bundle);
		if (!ret)
			ret = current_ret;
	}

	i = -1;
	while ((i = find_next_bit(pbundle->spec_finalize, key_bitmap_len,
				  i + 1)) < key_bitmap_len) {
		struct uverbs_attr *attr = &pbundle->bundle.attrs[i];
		const struct uverbs_api_attr *attr_uapi;
		void __rcu **slot;
		int current_ret;

		slot = uapi_get_attr_for_method(
			pbundle,
			pbundle->method_key | uapi_bkey_to_key_attr(i));
		if (WARN_ON(!slot))
			continue;

		attr_uapi = rcu_dereference_protected(*slot, true);

		if (attr_uapi->spec.type == UVERBS_ATTR_TYPE_IDRS_ARRAY) {
			current_ret = uverbs_free_idrs_array(
				attr_uapi, &attr->objs_arr_attr, commit,
				&pbundle->bundle);
			if (!ret)
				ret = current_ret;
		}
	}

	for (memblock = pbundle->allocated_mem; memblock;) {
		struct bundle_alloc_head *tmp = memblock;

		memblock = memblock->next;
		kvfree(tmp);
	}

	return ret;
}

static int ib_uverbs_cmd_verbs(struct ib_uverbs_file *ufile,
			       struct ib_uverbs_ioctl_hdr *hdr,
			       struct ib_uverbs_attr __user *user_attrs)
{
	const struct uverbs_api_ioctl_method *method_elm;
	struct uverbs_api *uapi = ufile->device->uapi;
	struct radix_tree_iter attrs_iter;
	struct bundle_priv *pbundle;
	struct bundle_priv onstack;
	void __rcu **slot;
	int destroy_ret;
	int ret;

	if (unlikely(hdr->driver_id != uapi->driver_id))
		return -EINVAL;

	slot = radix_tree_iter_lookup(
		&uapi->radix, &attrs_iter,
		uapi_key_obj(hdr->object_id) |
			uapi_key_ioctl_method(hdr->method_id));
	if (unlikely(!slot))
		return -EPROTONOSUPPORT;
	method_elm = rcu_dereference_protected(*slot, true);

	if (!method_elm->use_stack) {
		pbundle = kmalloc(method_elm->bundle_size, GFP_KERNEL);
		if (!pbundle)
			return -ENOMEM;
		pbundle->internal_avail =
			method_elm->bundle_size -
			offsetof(struct bundle_priv, internal_buffer);
		pbundle->alloc_head.next = NULL;
		pbundle->allocated_mem = &pbundle->alloc_head;
	} else {
		pbundle = &onstack;
		pbundle->internal_avail = sizeof(pbundle->internal_buffer);
		pbundle->allocated_mem = NULL;
	}

	/* Space for the pbundle->bundle.attrs flex array */
	pbundle->method_elm = method_elm;
	pbundle->method_key = attrs_iter.index;
	pbundle->bundle.ufile = ufile;
	pbundle->bundle.context = NULL; /* only valid if bundle has uobject */
	pbundle->radix = &uapi->radix;
	pbundle->radix_slots = slot;
	pbundle->radix_slots_len = radix_tree_chunk_size(&attrs_iter);
	pbundle->user_attrs = user_attrs;

	pbundle->internal_used = ALIGN(pbundle->method_elm->key_bitmap_len *
					       sizeof(*pbundle->bundle.attrs),
				       sizeof(*pbundle->internal_buffer));
	memset(pbundle->bundle.attr_present, 0,
	       sizeof(pbundle->bundle.attr_present));
	memset(pbundle->uobj_finalize, 0, sizeof(pbundle->uobj_finalize));
	memset(pbundle->spec_finalize, 0, sizeof(pbundle->spec_finalize));

	ret = ib_uverbs_run_method(pbundle, hdr->num_attrs);
	destroy_ret = bundle_destroy(pbundle, ret == 0);
	if (unlikely(destroy_ret && !ret))
		return destroy_ret;

	return ret;
}

long ib_uverbs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ib_uverbs_file *file = filp->private_data;
	struct ib_uverbs_ioctl_hdr __user *user_hdr =
		(struct ib_uverbs_ioctl_hdr __user *)arg;
	struct ib_uverbs_ioctl_hdr hdr;
	int srcu_key;
	int err;

	if (unlikely(cmd != RDMA_VERBS_IOCTL))
		return -ENOIOCTLCMD;

	err = copy_from_user(&hdr, user_hdr, sizeof(hdr));
	if (err)
		return -EFAULT;

	if (hdr.length > PAGE_SIZE ||
	    hdr.length != struct_size(&hdr, attrs, hdr.num_attrs))
		return -EINVAL;

	if (hdr.reserved1 || hdr.reserved2)
		return -EPROTONOSUPPORT;

	srcu_key = srcu_read_lock(&file->device->disassociate_srcu);
	err = ib_uverbs_cmd_verbs(file, &hdr, user_hdr->attrs);
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
 * Fill a ib_udata struct (core or uhw) using the given attribute IDs.
 * This is primarily used to convert the UVERBS_ATTR_UHW() into the
 * ib_udata format used by the drivers.
 */
void uverbs_fill_udata(struct uverbs_attr_bundle *bundle,
		       struct ib_udata *udata, unsigned int attr_in,
		       unsigned int attr_out)
{
	struct bundle_priv *pbundle =
		container_of(bundle, struct bundle_priv, bundle);
	const struct uverbs_attr *in =
		uverbs_attr_get(&pbundle->bundle, attr_in);
	const struct uverbs_attr *out =
		uverbs_attr_get(&pbundle->bundle, attr_out);

	if (!IS_ERR(in)) {
		udata->inlen = in->ptr_attr.len;
		if (uverbs_attr_ptr_is_inline(in))
			udata->inbuf =
				&pbundle->user_attrs[in->ptr_attr.uattr_idx]
					 .data;
		else
			udata->inbuf = u64_to_user_ptr(in->ptr_attr.data);
	} else {
		udata->inbuf = NULL;
		udata->inlen = 0;
	}

	if (!IS_ERR(out)) {
		udata->outbuf = u64_to_user_ptr(out->ptr_attr.data);
		udata->outlen = out->ptr_attr.len;
	} else {
		udata->outbuf = NULL;
		udata->outlen = 0;
	}
}

int uverbs_copy_to(const struct uverbs_attr_bundle *bundle, size_t idx,
		   const void *from, size_t size)
{
	const struct uverbs_attr *attr = uverbs_attr_get(bundle, idx);
	size_t min_size;

	if (IS_ERR(attr))
		return PTR_ERR(attr);

	min_size = min_t(size_t, attr->ptr_attr.len, size);
	if (copy_to_user(u64_to_user_ptr(attr->ptr_attr.data), from, min_size))
		return -EFAULT;

	return uverbs_set_output(bundle, attr);
}
EXPORT_SYMBOL(uverbs_copy_to);


/*
 * This is only used if the caller has directly used copy_to_use to write the
 * data.  It signals to user space that the buffer is filled in.
 */
int uverbs_output_written(const struct uverbs_attr_bundle *bundle, size_t idx)
{
	const struct uverbs_attr *attr = uverbs_attr_get(bundle, idx);

	if (IS_ERR(attr))
		return PTR_ERR(attr);

	return uverbs_set_output(bundle, attr);
}

int _uverbs_get_const(s64 *to, const struct uverbs_attr_bundle *attrs_bundle,
		      size_t idx, s64 lower_bound, u64 upper_bound,
		      s64  *def_val)
{
	const struct uverbs_attr *attr;

	attr = uverbs_attr_get(attrs_bundle, idx);
	if (IS_ERR(attr)) {
		if ((PTR_ERR(attr) != -ENOENT) || !def_val)
			return PTR_ERR(attr);

		*to = *def_val;
	} else {
		*to = attr->ptr_attr.data;
	}

	if (*to < lower_bound || (*to > 0 && (u64)*to > upper_bound))
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(_uverbs_get_const);

int uverbs_copy_to_struct_or_zero(const struct uverbs_attr_bundle *bundle,
				  size_t idx, const void *from, size_t size)
{
	const struct uverbs_attr *attr = uverbs_attr_get(bundle, idx);

	if (size < attr->ptr_attr.len) {
		if (clear_user(u64_to_user_ptr(attr->ptr_attr.data) + size,
			       attr->ptr_attr.len - size))
			return -EFAULT;
	}
	return uverbs_copy_to(bundle, idx, from, size);
}
