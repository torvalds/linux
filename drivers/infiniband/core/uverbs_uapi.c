// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2017, Mellanox Technologies inc.  All rights reserved.
 */
#include <rdma/uverbs_ioctl.h>
#include <rdma/rdma_user_ioctl.h>
#include <linux/bitops.h>
#include "rdma_core.h"
#include "uverbs.h"

static int ib_uverbs_notsupp(struct uverbs_attr_bundle *attrs)
{
	return -EOPNOTSUPP;
}

static void *uapi_add_elm(struct uverbs_api *uapi, u32 key, size_t alloc_size)
{
	void *elm;
	int rc;

	if (key == UVERBS_API_KEY_ERR)
		return ERR_PTR(-EOVERFLOW);

	elm = kzalloc(alloc_size, GFP_KERNEL);
	if (!elm)
		return ERR_PTR(-ENOMEM);
	rc = radix_tree_insert(&uapi->radix, key, elm);
	if (rc) {
		kfree(elm);
		return ERR_PTR(rc);
	}

	return elm;
}

static void *uapi_add_get_elm(struct uverbs_api *uapi, u32 key,
			      size_t alloc_size, bool *exists)
{
	void *elm;

	elm = uapi_add_elm(uapi, key, alloc_size);
	if (!IS_ERR(elm)) {
		*exists = false;
		return elm;
	}

	if (elm != ERR_PTR(-EEXIST))
		return elm;

	elm = radix_tree_lookup(&uapi->radix, key);
	if (WARN_ON(!elm))
		return ERR_PTR(-EINVAL);
	*exists = true;
	return elm;
}

static int uapi_create_write(struct uverbs_api *uapi,
			     struct ib_device *ibdev,
			     const struct uapi_definition *def,
			     u32 obj_key,
			     u32 *cur_method_key)
{
	struct uverbs_api_write_method *method_elm;
	u32 method_key = obj_key;
	bool exists;

	if (def->write.is_ex)
		method_key |= uapi_key_write_ex_method(def->write.command_num);
	else
		method_key |= uapi_key_write_method(def->write.command_num);

	method_elm = uapi_add_get_elm(uapi, method_key, sizeof(*method_elm),
				      &exists);
	if (IS_ERR(method_elm))
		return PTR_ERR(method_elm);

	if (WARN_ON(exists && (def->write.is_ex != method_elm->is_ex)))
		return -EINVAL;

	method_elm->is_ex = def->write.is_ex;
	method_elm->handler = def->func_write;
	if (!def->write.is_ex)
		method_elm->disabled = !(ibdev->uverbs_cmd_mask &
					 BIT_ULL(def->write.command_num));

	if (!def->write.is_ex && def->func_write) {
		method_elm->has_udata = def->write.has_udata;
		method_elm->has_resp = def->write.has_resp;
		method_elm->req_size = def->write.req_size;
		method_elm->resp_size = def->write.resp_size;
	}

	*cur_method_key = method_key;
	return 0;
}

static int uapi_merge_method(struct uverbs_api *uapi,
			     struct uverbs_api_object *obj_elm, u32 obj_key,
			     const struct uverbs_method_def *method,
			     bool is_driver)
{
	u32 method_key = obj_key | uapi_key_ioctl_method(method->id);
	struct uverbs_api_ioctl_method *method_elm;
	unsigned int i;
	bool exists;

	if (!method->attrs)
		return 0;

	method_elm = uapi_add_get_elm(uapi, method_key, sizeof(*method_elm),
				      &exists);
	if (IS_ERR(method_elm))
		return PTR_ERR(method_elm);
	if (exists) {
		/*
		 * This occurs when a driver uses ADD_UVERBS_ATTRIBUTES_SIMPLE
		 */
		if (WARN_ON(method->handler))
			return -EINVAL;
	} else {
		WARN_ON(!method->handler);
		rcu_assign_pointer(method_elm->handler, method->handler);
		if (method->handler != uverbs_destroy_def_handler)
			method_elm->driver_method = is_driver;
	}

	for (i = 0; i != method->num_attrs; i++) {
		const struct uverbs_attr_def *attr = (*method->attrs)[i];
		struct uverbs_api_attr *attr_slot;

		if (!attr)
			continue;

		/*
		 * ENUM_IN contains the 'ids' pointer to the driver's .rodata,
		 * so if it is specified by a driver then it always makes this
		 * into a driver method.
		 */
		if (attr->attr.type == UVERBS_ATTR_TYPE_ENUM_IN)
			method_elm->driver_method |= is_driver;

		/*
		 * Like other uobject based things we only support a single
		 * uobject being NEW'd or DESTROY'd
		 */
		if (attr->attr.type == UVERBS_ATTR_TYPE_IDRS_ARRAY) {
			u8 access = attr->attr.u2.objs_arr.access;

			if (WARN_ON(access == UVERBS_ACCESS_NEW ||
				    access == UVERBS_ACCESS_DESTROY))
				return -EINVAL;
		}

		attr_slot =
			uapi_add_elm(uapi, method_key | uapi_key_attr(attr->id),
				     sizeof(*attr_slot));
		/* Attributes are not allowed to be modified by drivers */
		if (IS_ERR(attr_slot))
			return PTR_ERR(attr_slot);

		attr_slot->spec = attr->attr;
	}

	return 0;
}

static int uapi_merge_obj_tree(struct uverbs_api *uapi,
			       const struct uverbs_object_def *obj,
			       bool is_driver)
{
	struct uverbs_api_object *obj_elm;
	unsigned int i;
	u32 obj_key;
	bool exists;
	int rc;

	obj_key = uapi_key_obj(obj->id);
	obj_elm = uapi_add_get_elm(uapi, obj_key, sizeof(*obj_elm), &exists);
	if (IS_ERR(obj_elm))
		return PTR_ERR(obj_elm);

	if (obj->type_attrs) {
		if (WARN_ON(obj_elm->type_attrs))
			return -EINVAL;

		obj_elm->id = obj->id;
		obj_elm->type_attrs = obj->type_attrs;
		obj_elm->type_class = obj->type_attrs->type_class;
		/*
		 * Today drivers are only permitted to use idr_class and
		 * fd_class types. We can revoke the IDR types during
		 * disassociation, and the FD types require the driver to use
		 * struct file_operations.owner to prevent the driver module
		 * code from unloading while the file is open. This provides
		 * enough safety that uverbs_uobject_fd_release() will
		 * continue to work.  Drivers using FD are responsible to
		 * handle disassociation of the device on their own.
		 */
		if (WARN_ON(is_driver &&
			    obj->type_attrs->type_class != &uverbs_idr_class &&
			    obj->type_attrs->type_class != &uverbs_fd_class))
			return -EINVAL;
	}

	if (!obj->methods)
		return 0;

	for (i = 0; i != obj->num_methods; i++) {
		const struct uverbs_method_def *method = (*obj->methods)[i];

		if (!method)
			continue;

		rc = uapi_merge_method(uapi, obj_elm, obj_key, method,
				       is_driver);
		if (rc)
			return rc;
	}

	return 0;
}

static int uapi_disable_elm(struct uverbs_api *uapi,
			    const struct uapi_definition *def,
			    u32 obj_key,
			    u32 method_key)
{
	bool exists;

	if (def->scope == UAPI_SCOPE_OBJECT) {
		struct uverbs_api_object *obj_elm;

		obj_elm = uapi_add_get_elm(
			uapi, obj_key, sizeof(*obj_elm), &exists);
		if (IS_ERR(obj_elm))
			return PTR_ERR(obj_elm);
		obj_elm->disabled = 1;
		return 0;
	}

	if (def->scope == UAPI_SCOPE_METHOD &&
	    uapi_key_is_ioctl_method(method_key)) {
		struct uverbs_api_ioctl_method *method_elm;

		method_elm = uapi_add_get_elm(uapi, method_key,
					      sizeof(*method_elm), &exists);
		if (IS_ERR(method_elm))
			return PTR_ERR(method_elm);
		method_elm->disabled = 1;
		return 0;
	}

	if (def->scope == UAPI_SCOPE_METHOD &&
	    (uapi_key_is_write_method(method_key) ||
	     uapi_key_is_write_ex_method(method_key))) {
		struct uverbs_api_write_method *write_elm;

		write_elm = uapi_add_get_elm(uapi, method_key,
					     sizeof(*write_elm), &exists);
		if (IS_ERR(write_elm))
			return PTR_ERR(write_elm);
		write_elm->disabled = 1;
		return 0;
	}

	WARN_ON(true);
	return -EINVAL;
}

static int uapi_merge_def(struct uverbs_api *uapi, struct ib_device *ibdev,
			  const struct uapi_definition *def_list,
			  bool is_driver)
{
	const struct uapi_definition *def = def_list;
	u32 cur_obj_key = UVERBS_API_KEY_ERR;
	u32 cur_method_key = UVERBS_API_KEY_ERR;
	bool exists;
	int rc;

	if (!def_list)
		return 0;

	for (;; def++) {
		switch ((enum uapi_definition_kind)def->kind) {
		case UAPI_DEF_CHAIN:
			rc = uapi_merge_def(uapi, ibdev, def->chain, is_driver);
			if (rc)
				return rc;
			continue;

		case UAPI_DEF_CHAIN_OBJ_TREE:
			if (WARN_ON(def->object_start.object_id !=
				    def->chain_obj_tree->id))
				return -EINVAL;

			cur_obj_key = uapi_key_obj(def->object_start.object_id);
			rc = uapi_merge_obj_tree(uapi, def->chain_obj_tree,
						 is_driver);
			if (rc)
				return rc;
			continue;

		case UAPI_DEF_END:
			return 0;

		case UAPI_DEF_IS_SUPPORTED_DEV_FN: {
			void **ibdev_fn =
				(void *)(&ibdev->ops) + def->needs_fn_offset;

			if (*ibdev_fn)
				continue;
			rc = uapi_disable_elm(
				uapi, def, cur_obj_key, cur_method_key);
			if (rc)
				return rc;
			continue;
		}

		case UAPI_DEF_IS_SUPPORTED_FUNC:
			if (def->func_is_supported(ibdev))
				continue;
			rc = uapi_disable_elm(
				uapi, def, cur_obj_key, cur_method_key);
			if (rc)
				return rc;
			continue;

		case UAPI_DEF_OBJECT_START: {
			struct uverbs_api_object *obj_elm;

			cur_obj_key = uapi_key_obj(def->object_start.object_id);
			obj_elm = uapi_add_get_elm(uapi, cur_obj_key,
						   sizeof(*obj_elm), &exists);
			if (IS_ERR(obj_elm))
				return PTR_ERR(obj_elm);
			continue;
		}

		case UAPI_DEF_WRITE:
			rc = uapi_create_write(
				uapi, ibdev, def, cur_obj_key, &cur_method_key);
			if (rc)
				return rc;
			continue;
		}
		WARN_ON(true);
		return -EINVAL;
	}
}

static int
uapi_finalize_ioctl_method(struct uverbs_api *uapi,
			   struct uverbs_api_ioctl_method *method_elm,
			   u32 method_key)
{
	struct radix_tree_iter iter;
	unsigned int num_attrs = 0;
	unsigned int max_bkey = 0;
	bool single_uobj = false;
	void __rcu **slot;

	method_elm->destroy_bkey = UVERBS_API_ATTR_BKEY_LEN;
	radix_tree_for_each_slot (slot, &uapi->radix, &iter,
				  uapi_key_attrs_start(method_key)) {
		struct uverbs_api_attr *elm =
			rcu_dereference_protected(*slot, true);
		u32 attr_key = iter.index & UVERBS_API_ATTR_KEY_MASK;
		u32 attr_bkey = uapi_bkey_attr(attr_key);
		u8 type = elm->spec.type;

		if (uapi_key_attr_to_ioctl_method(iter.index) !=
		    uapi_key_attr_to_ioctl_method(method_key))
			break;

		if (elm->spec.mandatory)
			__set_bit(attr_bkey, method_elm->attr_mandatory);

		if (elm->spec.is_udata)
			method_elm->has_udata = true;

		if (type == UVERBS_ATTR_TYPE_IDR ||
		    type == UVERBS_ATTR_TYPE_FD) {
			u8 access = elm->spec.u.obj.access;

			/*
			 * Verbs specs may only have one NEW/DESTROY, we don't
			 * have the infrastructure to abort multiple NEW's or
			 * cope with multiple DESTROY failure.
			 */
			if (access == UVERBS_ACCESS_NEW ||
			    access == UVERBS_ACCESS_DESTROY) {
				if (WARN_ON(single_uobj))
					return -EINVAL;

				single_uobj = true;
				if (WARN_ON(!elm->spec.mandatory))
					return -EINVAL;
			}

			if (access == UVERBS_ACCESS_DESTROY)
				method_elm->destroy_bkey = attr_bkey;
		}

		max_bkey = max(max_bkey, attr_bkey);
		num_attrs++;
	}

	method_elm->key_bitmap_len = max_bkey + 1;
	WARN_ON(method_elm->key_bitmap_len > UVERBS_API_ATTR_BKEY_LEN);

	uapi_compute_bundle_size(method_elm, num_attrs);
	return 0;
}

static int uapi_finalize(struct uverbs_api *uapi)
{
	const struct uverbs_api_write_method **data;
	unsigned long max_write_ex = 0;
	unsigned long max_write = 0;
	struct radix_tree_iter iter;
	void __rcu **slot;
	int rc;
	int i;

	radix_tree_for_each_slot (slot, &uapi->radix, &iter, 0) {
		struct uverbs_api_ioctl_method *method_elm =
			rcu_dereference_protected(*slot, true);

		if (uapi_key_is_ioctl_method(iter.index)) {
			rc = uapi_finalize_ioctl_method(uapi, method_elm,
							iter.index);
			if (rc)
				return rc;
		}

		if (uapi_key_is_write_method(iter.index))
			max_write = max(max_write,
					iter.index & UVERBS_API_ATTR_KEY_MASK);
		if (uapi_key_is_write_ex_method(iter.index))
			max_write_ex =
				max(max_write_ex,
				    iter.index & UVERBS_API_ATTR_KEY_MASK);
	}

	uapi->notsupp_method.handler = ib_uverbs_notsupp;
	uapi->num_write = max_write + 1;
	uapi->num_write_ex = max_write_ex + 1;
	data = kmalloc_array(uapi->num_write + uapi->num_write_ex,
			     sizeof(*uapi->write_methods), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	for (i = 0; i != uapi->num_write + uapi->num_write_ex; i++)
		data[i] = &uapi->notsupp_method;
	uapi->write_methods = data;
	uapi->write_ex_methods = data + uapi->num_write;

	radix_tree_for_each_slot (slot, &uapi->radix, &iter, 0) {
		if (uapi_key_is_write_method(iter.index))
			uapi->write_methods[iter.index &
					    UVERBS_API_ATTR_KEY_MASK] =
				rcu_dereference_protected(*slot, true);
		if (uapi_key_is_write_ex_method(iter.index))
			uapi->write_ex_methods[iter.index &
					       UVERBS_API_ATTR_KEY_MASK] =
				rcu_dereference_protected(*slot, true);
	}

	return 0;
}

static void uapi_remove_range(struct uverbs_api *uapi, u32 start, u32 last)
{
	struct radix_tree_iter iter;
	void __rcu **slot;

	radix_tree_for_each_slot (slot, &uapi->radix, &iter, start) {
		if (iter.index > last)
			return;
		kfree(rcu_dereference_protected(*slot, true));
		radix_tree_iter_delete(&uapi->radix, &iter, slot);
	}
}

static void uapi_remove_object(struct uverbs_api *uapi, u32 obj_key)
{
	uapi_remove_range(uapi, obj_key,
			  obj_key | UVERBS_API_METHOD_KEY_MASK |
				  UVERBS_API_ATTR_KEY_MASK);
}

static void uapi_remove_method(struct uverbs_api *uapi, u32 method_key)
{
	uapi_remove_range(uapi, method_key,
			  method_key | UVERBS_API_ATTR_KEY_MASK);
}


static u32 uapi_get_obj_id(struct uverbs_attr_spec *spec)
{
	if (spec->type == UVERBS_ATTR_TYPE_IDR ||
	    spec->type == UVERBS_ATTR_TYPE_FD)
		return spec->u.obj.obj_type;
	if (spec->type == UVERBS_ATTR_TYPE_IDRS_ARRAY)
		return spec->u2.objs_arr.obj_type;
	return UVERBS_API_KEY_ERR;
}

static void uapi_key_okay(u32 key)
{
	unsigned int count = 0;

	if (uapi_key_is_object(key))
		count++;
	if (uapi_key_is_ioctl_method(key))
		count++;
	if (uapi_key_is_write_method(key))
		count++;
	if (uapi_key_is_write_ex_method(key))
		count++;
	if (uapi_key_is_attr(key))
		count++;
	WARN(count != 1, "Bad count %u key=%x", count, key);
}

static void uapi_finalize_disable(struct uverbs_api *uapi)
{
	struct radix_tree_iter iter;
	u32 starting_key = 0;
	bool scan_again = false;
	void __rcu **slot;

again:
	radix_tree_for_each_slot (slot, &uapi->radix, &iter, starting_key) {
		uapi_key_okay(iter.index);

		if (uapi_key_is_object(iter.index)) {
			struct uverbs_api_object *obj_elm =
				rcu_dereference_protected(*slot, true);

			if (obj_elm->disabled) {
				/* Have to check all the attrs again */
				scan_again = true;
				starting_key = iter.index;
				uapi_remove_object(uapi, iter.index);
				goto again;
			}
			continue;
		}

		if (uapi_key_is_ioctl_method(iter.index)) {
			struct uverbs_api_ioctl_method *method_elm =
				rcu_dereference_protected(*slot, true);

			if (method_elm->disabled) {
				starting_key = iter.index;
				uapi_remove_method(uapi, iter.index);
				goto again;
			}
			continue;
		}

		if (uapi_key_is_write_method(iter.index) ||
		    uapi_key_is_write_ex_method(iter.index)) {
			struct uverbs_api_write_method *method_elm =
				rcu_dereference_protected(*slot, true);

			if (method_elm->disabled) {
				kfree(method_elm);
				radix_tree_iter_delete(&uapi->radix, &iter, slot);
			}
			continue;
		}

		if (uapi_key_is_attr(iter.index)) {
			struct uverbs_api_attr *attr_elm =
				rcu_dereference_protected(*slot, true);
			const struct uverbs_api_object *tmp_obj;
			u32 obj_key;

			/*
			 * If the method has a mandatory object handle
			 * attribute which relies on an object which is not
			 * present then the entire method is uncallable.
			 */
			if (!attr_elm->spec.mandatory)
				continue;
			obj_key = uapi_get_obj_id(&attr_elm->spec);
			if (obj_key == UVERBS_API_KEY_ERR)
				continue;
			tmp_obj = uapi_get_object(uapi, obj_key);
			if (IS_ERR(tmp_obj)) {
				if (PTR_ERR(tmp_obj) == -ENOMSG)
					continue;
			} else {
				if (!tmp_obj->disabled)
					continue;
			}

			starting_key = iter.index;
			uapi_remove_method(
				uapi,
				iter.index & (UVERBS_API_OBJ_KEY_MASK |
					      UVERBS_API_METHOD_KEY_MASK));
			goto again;
		}

		WARN_ON(false);
	}

	if (!scan_again)
		return;
	scan_again = false;
	starting_key = 0;
	goto again;
}

void uverbs_destroy_api(struct uverbs_api *uapi)
{
	if (!uapi)
		return;

	uapi_remove_range(uapi, 0, U32_MAX);
	kfree(uapi->write_methods);
	kfree(uapi);
}

static const struct uapi_definition uverbs_core_api[] = {
	UAPI_DEF_CHAIN(uverbs_def_obj_async_fd),
	UAPI_DEF_CHAIN(uverbs_def_obj_counters),
	UAPI_DEF_CHAIN(uverbs_def_obj_cq),
	UAPI_DEF_CHAIN(uverbs_def_obj_device),
	UAPI_DEF_CHAIN(uverbs_def_obj_dm),
	UAPI_DEF_CHAIN(uverbs_def_obj_flow_action),
	UAPI_DEF_CHAIN(uverbs_def_obj_intf),
	UAPI_DEF_CHAIN(uverbs_def_obj_mr),
	UAPI_DEF_CHAIN(uverbs_def_obj_qp),
	UAPI_DEF_CHAIN(uverbs_def_obj_srq),
	UAPI_DEF_CHAIN(uverbs_def_obj_wq),
	UAPI_DEF_CHAIN(uverbs_def_write_intf),
	{},
};

struct uverbs_api *uverbs_alloc_api(struct ib_device *ibdev)
{
	struct uverbs_api *uapi;
	int rc;

	uapi = kzalloc(sizeof(*uapi), GFP_KERNEL);
	if (!uapi)
		return ERR_PTR(-ENOMEM);

	INIT_RADIX_TREE(&uapi->radix, GFP_KERNEL);
	uapi->driver_id = ibdev->ops.driver_id;

	rc = uapi_merge_def(uapi, ibdev, uverbs_core_api, false);
	if (rc)
		goto err;
	rc = uapi_merge_def(uapi, ibdev, ibdev->driver_def, true);
	if (rc)
		goto err;

	uapi_finalize_disable(uapi);
	rc = uapi_finalize(uapi);
	if (rc)
		goto err;

	return uapi;
err:
	if (rc != -ENOMEM)
		dev_err(&ibdev->dev,
			"Setup of uverbs_api failed, kernel parsing tree description is not valid (%d)??\n",
			rc);

	uverbs_destroy_api(uapi);
	return ERR_PTR(rc);
}

/*
 * The pre version is done before destroying the HW objects, it only blocks
 * off method access. All methods that require the ib_dev or the module data
 * must test one of these assignments prior to continuing.
 */
void uverbs_disassociate_api_pre(struct ib_uverbs_device *uverbs_dev)
{
	struct uverbs_api *uapi = uverbs_dev->uapi;
	struct radix_tree_iter iter;
	void __rcu **slot;

	rcu_assign_pointer(uverbs_dev->ib_dev, NULL);

	radix_tree_for_each_slot (slot, &uapi->radix, &iter, 0) {
		if (uapi_key_is_ioctl_method(iter.index)) {
			struct uverbs_api_ioctl_method *method_elm =
				rcu_dereference_protected(*slot, true);

			if (method_elm->driver_method)
				rcu_assign_pointer(method_elm->handler, NULL);
		}
	}

	synchronize_srcu(&uverbs_dev->disassociate_srcu);
}

/*
 * Called when a driver disassociates from the ib_uverbs_device. The
 * assumption is that the driver module will unload after. Replace everything
 * related to the driver with NULL as a safety measure.
 */
void uverbs_disassociate_api(struct uverbs_api *uapi)
{
	struct radix_tree_iter iter;
	void __rcu **slot;

	radix_tree_for_each_slot (slot, &uapi->radix, &iter, 0) {
		if (uapi_key_is_object(iter.index)) {
			struct uverbs_api_object *object_elm =
				rcu_dereference_protected(*slot, true);

			/*
			 * Some type_attrs are in the driver module. We don't
			 * bother to keep track of which since there should be
			 * no use of this after disassociate.
			 */
			object_elm->type_attrs = NULL;
		} else if (uapi_key_is_attr(iter.index)) {
			struct uverbs_api_attr *elm =
				rcu_dereference_protected(*slot, true);

			if (elm->spec.type == UVERBS_ATTR_TYPE_ENUM_IN)
				elm->spec.u2.enum_def.ids = NULL;
		}
	}
}
