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

#include <rdma/uverbs_ioctl.h>
#include <rdma/rdma_user_ioctl.h>
#include <linux/bitops.h>
#include "uverbs.h"

#define UVERBS_NUM_NS (UVERBS_ID_NS_MASK >> UVERBS_ID_NS_SHIFT)
#define GET_NS_ID(idx) (((idx) & UVERBS_ID_NS_MASK) >> UVERBS_ID_NS_SHIFT)
#define GET_ID(idx) ((idx) & ~UVERBS_ID_NS_MASK)

#define _for_each_element(elem, tmpi, tmpj, hashes, num_buckets_offset,	       \
			  buckets_offset)				       \
	for (tmpj = 0,							       \
	     elem = (*(const void ***)((hashes)[tmpi] +			       \
				       (buckets_offset)))[0];	               \
	     tmpj < *(size_t *)((hashes)[tmpi] + (num_buckets_offset));        \
	     tmpj++)						               \
		if ((elem = ((*(const void ***)(hashes[tmpi] +		       \
						(buckets_offset)))[tmpj])))

/*
 * Iterate all elements of a few @hashes. The number of given hashes is
 * indicated by @num_hashes. The offset of the number of buckets in the hash is
 * represented by @num_buckets_offset, while the offset of the buckets array in
 * the hash structure is represented by @buckets_offset. tmpi and tmpj are two
 * short (or int) based indices that are given by the user. tmpi iterates over
 * the different hashes. @elem points the current element in the hashes[tmpi]
 * bucket we are looping on. To be honest, @hashes representation isn't exactly
 * a hash, but more a collection of elements. These elements' ids are treated
 * in a hash like manner, where the first upper bits are the bucket number.
 * These elements are later mapped into a perfect-hash.
 */
#define for_each_element(elem, tmpi, tmpj, hashes, num_hashes,                 \
			 num_buckets_offset, buckets_offset)		       \
	for (tmpi = 0; tmpi < (num_hashes); tmpi++)		               \
		_for_each_element(elem, tmpi, tmpj, hashes, num_buckets_offset,\
				  buckets_offset)

#define get_elements_iterators_entry_above(iters, num_elements, elements,     \
					  num_objects_fld, objects_fld, bucket,\
					  min_id)			       \
	get_elements_above_id((const void **)iters, num_elements,       \
				     (const void **)(elements),		       \
				     offsetof(typeof(**elements),	       \
					      num_objects_fld),		       \
				     offsetof(typeof(**elements), objects_fld),\
				     offsetof(typeof(***(*elements)->objects_fld), id),\
				     bucket, min_id)

#define get_objects_above_id(iters, num_trees, trees, bucket, min_id)	       \
	get_elements_iterators_entry_above(iters, num_trees, trees,	       \
					   num_objects, objects, bucket, min_id)

#define get_methods_above_id(method_iters, num_iters, iters, bucket, min_id)\
	get_elements_iterators_entry_above(method_iters, num_iters, iters,     \
					   num_methods, methods, bucket, min_id)

#define get_attrs_above_id(attrs_iters, num_iters, iters, bucket, min_id)\
	get_elements_iterators_entry_above(attrs_iters, num_iters, iters,      \
					   num_attrs, attrs, bucket, min_id)

/*
 * get_elements_above_id get a few hashes represented by @elements and
 * @num_elements. The hashes fields are described by @num_offset, @data_offset
 * and @id_offset in the same way as required by for_each_element. The function
 * returns an array of @iters, represents an array of elements in the hashes
 * buckets, which their ids are the smallest ids in all hashes but are all
 * larger than the id given by min_id. Elements are only added to the iters
 * array if their id belongs to the bucket @bucket. The number of elements in
 * the returned array is returned by the function. @min_id is also updated to
 * reflect the new min_id of all elements in iters.
 */
static size_t get_elements_above_id(const void **iters,
				    unsigned int num_elements,
				    const void **elements,
				    size_t num_offset,
				    size_t data_offset,
				    size_t id_offset,
				    u16 bucket,
				    short *min_id)
{
	size_t num_iters = 0;
	short min = SHRT_MAX;
	const void *elem;
	int i, j, last_stored = -1;
	unsigned int equal_min = 0;

	for_each_element(elem, i, j, elements, num_elements, num_offset,
			 data_offset) {
		u16 id = *(u16 *)(elem + id_offset);

		if (GET_NS_ID(id) != bucket)
			continue;

		if (GET_ID(id) < *min_id ||
		    (min != SHRT_MAX && GET_ID(id) > min))
			continue;

		/*
		 * We first iterate all hashes represented by @elements. When
		 * we do, we try to find an element @elem in the bucket @bucket
		 * which its id is min. Since we can't ensure the user sorted
		 * the elements in increasing order, we override this hash's
		 * minimal id element we found, if a new element with a smaller
		 * id was just found.
		 */
		iters[last_stored == i ? num_iters - 1 : num_iters++] = elem;
		last_stored = i;
		if (min == GET_ID(id))
			equal_min++;
		else
			equal_min = 1;
		min = GET_ID(id);
	}

	/*
	 * We only insert to our iters array an element, if its id is smaller
	 * than all previous ids. Therefore, the final iters array is sorted so
	 * that smaller ids are in the end of the array.
	 * Therefore, we need to clean the beginning of the array to make sure
	 * all ids of final elements are equal to min.
	 */
	memmove(iters, iters + num_iters - equal_min, sizeof(*iters) * equal_min);

	*min_id = min;
	return equal_min;
}

#define find_max_element_entry_id(num_elements, elements, num_objects_fld, \
				  objects_fld, bucket)			   \
	find_max_element_id(num_elements, (const void **)(elements),	   \
			    offsetof(typeof(**elements), num_objects_fld),    \
			    offsetof(typeof(**elements), objects_fld),	      \
			    offsetof(typeof(***(*elements)->objects_fld), id),\
			    bucket)

static short find_max_element_ns_id(unsigned int num_elements,
				    const void **elements,
				    size_t num_offset,
				    size_t data_offset,
				    size_t id_offset)
{
	short max_ns = SHRT_MIN;
	const void *elem;
	int i, j;

	for_each_element(elem, i, j, elements, num_elements, num_offset,
			 data_offset) {
		u16 id = *(u16 *)(elem + id_offset);

		if (GET_NS_ID(id) > max_ns)
			max_ns = GET_NS_ID(id);
	}

	return max_ns;
}

static short find_max_element_id(unsigned int num_elements,
				 const void **elements,
				 size_t num_offset,
				 size_t data_offset,
				 size_t id_offset,
				 u16 bucket)
{
	short max_id = SHRT_MIN;
	const void *elem;
	int i, j;

	for_each_element(elem, i, j, elements, num_elements, num_offset,
			 data_offset) {
		u16 id = *(u16 *)(elem + id_offset);

		if (GET_NS_ID(id) == bucket &&
		    GET_ID(id) > max_id)
			max_id = GET_ID(id);
	}
	return max_id;
}

#define find_max_element_entry_id(num_elements, elements, num_objects_fld,   \
				  objects_fld, bucket)			      \
	find_max_element_id(num_elements, (const void **)(elements),	      \
			    offsetof(typeof(**elements), num_objects_fld),    \
			    offsetof(typeof(**elements), objects_fld),	      \
			    offsetof(typeof(***(*elements)->objects_fld), id),\
			    bucket)

#define find_max_element_ns_entry_id(num_elements, elements,		    \
				     num_objects_fld, objects_fld)	    \
	find_max_element_ns_id(num_elements, (const void **)(elements),	    \
			      offsetof(typeof(**elements), num_objects_fld),\
			      offsetof(typeof(**elements), objects_fld),    \
			      offsetof(typeof(***(*elements)->objects_fld), id))

/*
 * find_max_xxxx_ns_id gets a few elements. Each element is described by an id
 * which its upper bits represents a namespace. It finds the max namespace. This
 * could be used in order to know how many buckets do we need to allocate. If no
 * elements exist, SHRT_MIN is returned. Namespace represents here different
 * buckets. The common example is "common bucket" and "driver bucket".
 *
 * find_max_xxxx_id gets a few elements and a bucket. Each element is described
 * by an id which its upper bits represent a namespace. It returns the max id
 * which is contained in the same namespace defined in @bucket. This could be
 * used in order to know how many elements do we need to allocate in the bucket.
 * If no elements exist, SHRT_MIN is returned.
 */

#define find_max_object_id(num_trees, trees, bucket)			\
		find_max_element_entry_id(num_trees, trees, num_objects,\
					  objects, bucket)
#define find_max_object_ns_id(num_trees, trees)			\
		find_max_element_ns_entry_id(num_trees, trees,		\
					     num_objects, objects)

#define find_max_method_id(num_iters, iters, bucket)			\
		find_max_element_entry_id(num_iters, iters, num_methods,\
					  methods, bucket)
#define find_max_method_ns_id(num_iters, iters)			\
		find_max_element_ns_entry_id(num_iters, iters,		\
					     num_methods, methods)

#define find_max_attr_id(num_iters, iters, bucket)			\
		find_max_element_entry_id(num_iters, iters, num_attrs,  \
					  attrs, bucket)
#define find_max_attr_ns_id(num_iters, iters)				\
		find_max_element_ns_entry_id(num_iters, iters,		\
					     num_attrs, attrs)

static void free_method(struct uverbs_method_spec *method)
{
	unsigned int i;

	if (!method)
		return;

	for (i = 0; i < method->num_buckets; i++)
		kfree(method->attr_buckets[i]);

	kfree(method);
}

#define IS_ATTR_OBJECT(attr) ((attr)->type == UVERBS_ATTR_TYPE_IDR || \
			      (attr)->type == UVERBS_ATTR_TYPE_FD)

/*
 * This function gets array of size @num_method_defs which contains pointers to
 * method definitions @method_defs. The function allocates an
 * uverbs_method_spec structure and initializes its number of buckets and the
 * elements in buckets to the correct attributes. While doing that, it
 * validates that there aren't conflicts between attributes of different
 * method_defs.
 */
static struct uverbs_method_spec *build_method_with_attrs(const struct uverbs_method_def **method_defs,
							  size_t num_method_defs)
{
	int bucket_idx;
	int max_attr_buckets = 0;
	size_t num_attr_buckets = 0;
	int res = 0;
	struct uverbs_method_spec *method = NULL;
	const struct uverbs_attr_def **attr_defs;
	unsigned int num_of_singularities = 0;

	max_attr_buckets = find_max_attr_ns_id(num_method_defs, method_defs);
	if (max_attr_buckets >= 0)
		num_attr_buckets = max_attr_buckets + 1;

	method = kzalloc(struct_size(method, attr_buckets, num_attr_buckets),
			 GFP_KERNEL);
	if (!method)
		return ERR_PTR(-ENOMEM);

	method->num_buckets = num_attr_buckets;
	attr_defs = kcalloc(num_method_defs, sizeof(*attr_defs), GFP_KERNEL);
	if (!attr_defs) {
		res = -ENOMEM;
		goto free_method;
	}
	for (bucket_idx = 0; bucket_idx < method->num_buckets; bucket_idx++) {
		short min_id = SHRT_MIN;
		int attr_max_bucket = 0;
		struct uverbs_attr_spec_hash *hash = NULL;

		attr_max_bucket = find_max_attr_id(num_method_defs, method_defs,
						   bucket_idx);
		if (attr_max_bucket < 0)
			continue;

		hash = kzalloc(sizeof(*hash) +
			       ALIGN(sizeof(*hash->attrs) * (attr_max_bucket + 1),
				     sizeof(long)) +
			       BITS_TO_LONGS(attr_max_bucket + 1) * sizeof(long),
			       GFP_KERNEL);
		if (!hash) {
			res = -ENOMEM;
			goto free;
		}
		hash->num_attrs = attr_max_bucket + 1;
		method->num_child_attrs += hash->num_attrs;
		hash->mandatory_attrs_bitmask = (void *)(hash + 1) +
						 ALIGN(sizeof(*hash->attrs) *
						       (attr_max_bucket + 1),
						       sizeof(long));

		method->attr_buckets[bucket_idx] = hash;

		do {
			size_t			 num_attr_defs;
			struct uverbs_attr_spec	*attr;
			bool attr_obj_with_special_access;

			num_attr_defs =
				get_attrs_above_id(attr_defs,
						   num_method_defs,
						   method_defs,
						   bucket_idx,
						   &min_id);
			/* Last attr in bucket */
			if (!num_attr_defs)
				break;

			if (num_attr_defs > 1) {
				/*
				 * We don't allow two attribute definitions for
				 * the same attribute. This is usually a
				 * programmer error. If required, it's better to
				 * just add a new attribute to capture the new
				 * semantics.
				 */
				res = -EEXIST;
				goto free;
			}

			attr = &hash->attrs[min_id];
			memcpy(attr, &attr_defs[0]->attr, sizeof(*attr));

			attr_obj_with_special_access = IS_ATTR_OBJECT(attr) &&
				   (attr->obj.access == UVERBS_ACCESS_NEW ||
				    attr->obj.access == UVERBS_ACCESS_DESTROY);
			num_of_singularities +=  !!attr_obj_with_special_access;
			if (WARN(num_of_singularities > 1,
				 "ib_uverbs: Method contains more than one object attr (%d) with new/destroy access\n",
				 min_id) ||
			    WARN(attr_obj_with_special_access &&
				 !(attr->flags & UVERBS_ATTR_SPEC_F_MANDATORY),
				 "ib_uverbs: Tried to merge attr (%d) but it's an object with new/destroy access but isn't mandatory\n",
				 min_id) ||
			    WARN(IS_ATTR_OBJECT(attr) &&
				 attr->flags & UVERBS_ATTR_SPEC_F_MIN_SZ_OR_ZERO,
				 "ib_uverbs: Tried to merge attr (%d) but it's an object with min_sz flag\n",
				 min_id)) {
				res = -EINVAL;
				goto free;
			}

			if (attr->flags & UVERBS_ATTR_SPEC_F_MANDATORY)
				set_bit(min_id, hash->mandatory_attrs_bitmask);
			min_id++;

		} while (1);
	}
	kfree(attr_defs);
	return method;

free:
	kfree(attr_defs);
free_method:
	free_method(method);
	return ERR_PTR(res);
}

static void free_object(struct uverbs_object_spec *object)
{
	unsigned int i, j;

	if (!object)
		return;

	for (i = 0; i < object->num_buckets; i++) {
		struct uverbs_method_spec_hash	*method_buckets =
			object->method_buckets[i];

		if (!method_buckets)
			continue;

		for (j = 0; j < method_buckets->num_methods; j++)
			free_method(method_buckets->methods[j]);

		kfree(method_buckets);
	}

	kfree(object);
}

/*
 * This function gets array of size @num_object_defs which contains pointers to
 * object definitions @object_defs. The function allocated an
 * uverbs_object_spec structure and initialize its number of buckets and the
 * elements in buckets to the correct methods. While doing that, it
 * sorts out the correct relationship between conflicts in the same method.
 */
static struct uverbs_object_spec *build_object_with_methods(const struct uverbs_object_def **object_defs,
							    size_t num_object_defs)
{
	u16 bucket_idx;
	int max_method_buckets = 0;
	u16 num_method_buckets = 0;
	int res = 0;
	struct uverbs_object_spec *object = NULL;
	const struct uverbs_method_def **method_defs;

	max_method_buckets = find_max_method_ns_id(num_object_defs, object_defs);
	if (max_method_buckets >= 0)
		num_method_buckets = max_method_buckets + 1;

	object = kzalloc(struct_size(object, method_buckets,
				     num_method_buckets),
			 GFP_KERNEL);
	if (!object)
		return ERR_PTR(-ENOMEM);

	object->num_buckets = num_method_buckets;
	method_defs = kcalloc(num_object_defs, sizeof(*method_defs), GFP_KERNEL);
	if (!method_defs) {
		res = -ENOMEM;
		goto free_object;
	}

	for (bucket_idx = 0; bucket_idx < object->num_buckets; bucket_idx++) {
		short min_id = SHRT_MIN;
		int methods_max_bucket = 0;
		struct uverbs_method_spec_hash *hash = NULL;

		methods_max_bucket = find_max_method_id(num_object_defs, object_defs,
							bucket_idx);
		if (methods_max_bucket < 0)
			continue;

		hash = kzalloc(struct_size(hash, methods,
					   methods_max_bucket + 1),
			       GFP_KERNEL);
		if (!hash) {
			res = -ENOMEM;
			goto free;
		}

		hash->num_methods = methods_max_bucket + 1;
		object->method_buckets[bucket_idx] = hash;

		do {
			size_t				num_method_defs;
			struct uverbs_method_spec	*method;
			int i;

			num_method_defs =
				get_methods_above_id(method_defs,
						     num_object_defs,
						     object_defs,
						     bucket_idx,
						     &min_id);
			/* Last method in bucket */
			if (!num_method_defs)
				break;

			method = build_method_with_attrs(method_defs,
							 num_method_defs);
			if (IS_ERR(method)) {
				res = PTR_ERR(method);
				goto free;
			}

			/*
			 * The last tree which is given as an argument to the
			 * merge overrides previous method handler.
			 * Therefore, we iterate backwards and search for the
			 * first handler which != NULL. This also defines the
			 * set of flags used for this handler.
			 */
			for (i = num_method_defs - 1;
			     i >= 0 && !method_defs[i]->handler; i--)
				;
			hash->methods[min_id++] = method;
			/* NULL handler isn't allowed */
			if (WARN(i < 0,
				 "ib_uverbs: tried to merge function id %d, but all handlers are NULL\n",
				 min_id)) {
				res = -EINVAL;
				goto free;
			}
			method->handler = method_defs[i]->handler;
			method->flags = method_defs[i]->flags;

		} while (1);
	}
	kfree(method_defs);
	return object;

free:
	kfree(method_defs);
free_object:
	free_object(object);
	return ERR_PTR(res);
}

void uverbs_free_spec_tree(struct uverbs_root_spec *root)
{
	unsigned int i, j;

	if (!root)
		return;

	for (i = 0; i < root->num_buckets; i++) {
		struct uverbs_object_spec_hash *object_hash =
			root->object_buckets[i];

		if (!object_hash)
			continue;

		for (j = 0; j < object_hash->num_objects; j++)
			free_object(object_hash->objects[j]);

		kfree(object_hash);
	}

	kfree(root);
}
EXPORT_SYMBOL(uverbs_free_spec_tree);

struct uverbs_root_spec *uverbs_alloc_spec_tree(unsigned int num_trees,
						const struct uverbs_object_tree_def **trees)
{
	u16 bucket_idx;
	short max_object_buckets = 0;
	size_t num_objects_buckets = 0;
	struct uverbs_root_spec *root_spec = NULL;
	const struct uverbs_object_def **object_defs;
	int i;
	int res = 0;

	max_object_buckets = find_max_object_ns_id(num_trees, trees);
	/*
	 * Devices which don't want to support ib_uverbs, should just allocate
	 * an empty parsing tree. Every user-space command won't hit any valid
	 * entry in the parsing tree and thus will fail.
	 */
	if (max_object_buckets >= 0)
		num_objects_buckets = max_object_buckets + 1;

	root_spec = kzalloc(struct_size(root_spec, object_buckets,
					num_objects_buckets),
			    GFP_KERNEL);
	if (!root_spec)
		return ERR_PTR(-ENOMEM);
	root_spec->num_buckets = num_objects_buckets;

	object_defs = kcalloc(num_trees, sizeof(*object_defs),
			      GFP_KERNEL);
	if (!object_defs) {
		res = -ENOMEM;
		goto free_root;
	}

	for (bucket_idx = 0; bucket_idx < root_spec->num_buckets; bucket_idx++) {
		short min_id = SHRT_MIN;
		short objects_max_bucket;
		struct uverbs_object_spec_hash *hash = NULL;

		objects_max_bucket = find_max_object_id(num_trees, trees,
							bucket_idx);
		if (objects_max_bucket < 0)
			continue;

		hash = kzalloc(struct_size(hash, objects,
					   objects_max_bucket + 1),
			       GFP_KERNEL);
		if (!hash) {
			res = -ENOMEM;
			goto free;
		}
		hash->num_objects = objects_max_bucket + 1;
		root_spec->object_buckets[bucket_idx] = hash;

		do {
			size_t				num_object_defs;
			struct uverbs_object_spec	*object;

			num_object_defs = get_objects_above_id(object_defs,
							       num_trees,
							       trees,
							       bucket_idx,
							       &min_id);
			/* Last object in bucket */
			if (!num_object_defs)
				break;

			object = build_object_with_methods(object_defs,
							   num_object_defs);
			if (IS_ERR(object)) {
				res = PTR_ERR(object);
				goto free;
			}

			/*
			 * The last tree which is given as an argument to the
			 * merge overrides previous object's type_attrs.
			 * Therefore, we iterate backwards and search for the
			 * first type_attrs which != NULL.
			 */
			for (i = num_object_defs - 1;
			     i >= 0 && !object_defs[i]->type_attrs; i--)
				;
			/*
			 * NULL is a valid type_attrs. It means an object we
			 * can't instantiate (like DEVICE).
			 */
			object->type_attrs = i < 0 ? NULL :
				object_defs[i]->type_attrs;

			hash->objects[min_id++] = object;
		} while (1);
	}

	kfree(object_defs);
	return root_spec;

free:
	kfree(object_defs);
free_root:
	uverbs_free_spec_tree(root_spec);
	return ERR_PTR(res);
}
EXPORT_SYMBOL(uverbs_alloc_spec_tree);
