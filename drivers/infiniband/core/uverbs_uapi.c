// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2017, Mellanox Technologies inc.  All rights reserved.
 */
#include <rdma/uverbs_ioctl.h>
#include <rdma/rdma_user_ioctl.h>
#include <linux/bitops.h>
#include "rdma_core.h"
#include "uverbs.h"

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

static int uapi_merge_method(struct uverbs_api *uapi,
			     struct uverbs_api_object *obj_elm, u32 obj_key,
			     const struct uverbs_method_def *method,
			     bool is_driver)
{
	u32 method_key = obj_key | uapi_key_ioctl_method(method->id);
	struct uverbs_api_ioctl_method *method_elm;
	unsigned int i;

	if (!method->attrs)
		return 0;

	method_elm = uapi_add_elm(uapi, method_key, sizeof(*method_elm));
	if (IS_ERR(method_elm)) {
		if (method_elm != ERR_PTR(-EEXIST))
			return PTR_ERR(method_elm);

		/*
		 * This occurs when a driver uses ADD_UVERBS_ATTRIBUTES_SIMPLE
		 */
		if (WARN_ON(method->handler))
			return -EINVAL;
		method_elm = radix_tree_lookup(&uapi->radix, method_key);
		if (WARN_ON(!method_elm))
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

static int uapi_merge_tree(struct uverbs_api *uapi,
			   const struct uverbs_object_tree_def *tree,
			   bool is_driver)
{
	unsigned int i, j;
	int rc;

	if (!tree->objects)
		return 0;

	for (i = 0; i != tree->num_objects; i++) {
		const struct uverbs_object_def *obj = (*tree->objects)[i];
		struct uverbs_api_object *obj_elm;
		u32 obj_key;

		if (!obj)
			continue;

		obj_key = uapi_key_obj(obj->id);
		obj_elm = uapi_add_elm(uapi, obj_key, sizeof(*obj_elm));
		if (IS_ERR(obj_elm)) {
			if (obj_elm != ERR_PTR(-EEXIST))
				return PTR_ERR(obj_elm);

			/* This occurs when a driver uses ADD_UVERBS_METHODS */
			if (WARN_ON(obj->type_attrs))
				return -EINVAL;
			obj_elm = radix_tree_lookup(&uapi->radix, obj_key);
			if (WARN_ON(!obj_elm))
				return -EINVAL;
		} else {
			obj_elm->type_attrs = obj->type_attrs;
			if (obj->type_attrs) {
				obj_elm->type_class =
					obj->type_attrs->type_class;
				/*
				 * Today drivers are only permitted to use
				 * idr_class types. They cannot use FD types
				 * because we currently have no way to revoke
				 * the fops pointer after device
				 * disassociation.
				 */
				if (WARN_ON(is_driver &&
					    obj->type_attrs->type_class !=
						    &uverbs_idr_class))
					return -EINVAL;
			}
		}

		if (!obj->methods)
			continue;

		for (j = 0; j != obj->num_methods; j++) {
			const struct uverbs_method_def *method =
				(*obj->methods)[j];
			if (!method)
				continue;

			rc = uapi_merge_method(uapi, obj_elm, obj_key, method,
					       is_driver);
			if (rc)
				return rc;
		}
	}

	return 0;
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

		if (uapi_key_attr_to_method(iter.index) !=
		    uapi_key_attr_to_method(method_key))
			break;

		if (elm->spec.mandatory)
			__set_bit(attr_bkey, method_elm->attr_mandatory);

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
	struct radix_tree_iter iter;
	void __rcu **slot;
	int rc;

	radix_tree_for_each_slot (slot, &uapi->radix, &iter, 0) {
		struct uverbs_api_ioctl_method *method_elm =
			rcu_dereference_protected(*slot, true);

		if (uapi_key_is_ioctl_method(iter.index)) {
			rc = uapi_finalize_ioctl_method(uapi, method_elm,
							iter.index);
			if (rc)
				return rc;
		}
	}

	return 0;
}

void uverbs_destroy_api(struct uverbs_api *uapi)
{
	struct radix_tree_iter iter;
	void __rcu **slot;

	if (!uapi)
		return;

	radix_tree_for_each_slot (slot, &uapi->radix, &iter, 0) {
		kfree(rcu_dereference_protected(*slot, true));
		radix_tree_iter_delete(&uapi->radix, &iter, slot);
	}
	kfree(uapi);
}

struct uverbs_api *uverbs_alloc_api(
	const struct uverbs_object_tree_def *const *driver_specs,
	enum rdma_driver_id driver_id)
{
	struct uverbs_api *uapi;
	int rc;

	uapi = kzalloc(sizeof(*uapi), GFP_KERNEL);
	if (!uapi)
		return ERR_PTR(-ENOMEM);

	INIT_RADIX_TREE(&uapi->radix, GFP_KERNEL);
	uapi->driver_id = driver_id;

	rc = uapi_merge_tree(uapi, uverbs_default_get_objects(), false);
	if (rc)
		goto err;

	for (; driver_specs && *driver_specs; driver_specs++) {
		rc = uapi_merge_tree(uapi, *driver_specs, true);
		if (rc)
			goto err;
	}

	rc = uapi_finalize(uapi);
	if (rc)
		goto err;

	return uapi;
err:
	if (rc != -ENOMEM)
		pr_err("Setup of uverbs_api failed, kernel parsing tree description is not valid (%d)??\n",
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
