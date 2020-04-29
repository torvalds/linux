/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005-2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2005 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2005 PathScale, Inc. All rights reserved.
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

#ifndef RDMA_CORE_H
#define RDMA_CORE_H

#include <linux/idr.h>
#include <rdma/uverbs_types.h>
#include <rdma/uverbs_ioctl.h>
#include <rdma/ib_verbs.h>
#include <linux/mutex.h>

struct ib_uverbs_device;

void uverbs_destroy_ufile_hw(struct ib_uverbs_file *ufile,
			     enum rdma_remove_reason reason);

int uobj_destroy(struct ib_uobject *uobj, struct uverbs_attr_bundle *attrs);

/*
 * Get an ib_uobject that corresponds to the given id from ufile, assuming
 * the object is from the given type. Lock it to the required access when
 * applicable.
 * This function could create (access == NEW), destroy (access == DESTROY)
 * or unlock (access == READ || access == WRITE) objects if required.
 * The action will be finalized only when uverbs_finalize_object or
 * uverbs_finalize_objects are called.
 */
struct ib_uobject *
uverbs_get_uobject_from_file(u16 object_id, enum uverbs_obj_access access,
			     s64 id, struct uverbs_attr_bundle *attrs);

void uverbs_finalize_object(struct ib_uobject *uobj,
			    enum uverbs_obj_access access, bool commit,
			    struct uverbs_attr_bundle *attrs);

int uverbs_output_written(const struct uverbs_attr_bundle *bundle, size_t idx);

void setup_ufile_idr_uobject(struct ib_uverbs_file *ufile);
void release_ufile_idr_uobject(struct ib_uverbs_file *ufile);

struct ib_udata *uverbs_get_cleared_udata(struct uverbs_attr_bundle *attrs);

/*
 * This is the runtime description of the uverbs API, used by the syscall
 * machinery to validate and dispatch calls.
 */

/*
 * Depending on ID the slot pointer in the radix tree points at one of these
 * structs.
 */

struct uverbs_api_ioctl_method {
	int(__rcu *handler)(struct uverbs_attr_bundle *attrs);
	DECLARE_BITMAP(attr_mandatory, UVERBS_API_ATTR_BKEY_LEN);
	u16 bundle_size;
	u8 use_stack:1;
	u8 driver_method:1;
	u8 disabled:1;
	u8 has_udata:1;
	u8 key_bitmap_len;
	u8 destroy_bkey;
};

struct uverbs_api_write_method {
	int (*handler)(struct uverbs_attr_bundle *attrs);
	u8 disabled:1;
	u8 is_ex:1;
	u8 has_udata:1;
	u8 has_resp:1;
	u8 req_size;
	u8 resp_size;
};

struct uverbs_api_attr {
	struct uverbs_attr_spec spec;
};

struct uverbs_api {
	/* radix tree contains struct uverbs_api_* pointers */
	struct radix_tree_root radix;
	enum rdma_driver_id driver_id;

	unsigned int num_write;
	unsigned int num_write_ex;
	struct uverbs_api_write_method notsupp_method;
	const struct uverbs_api_write_method **write_methods;
	const struct uverbs_api_write_method **write_ex_methods;
};

/*
 * Get an uverbs_api_object that corresponds to the given object_id.
 * Note:
 * -ENOMSG means that any object is allowed to match during lookup.
 */
static inline const struct uverbs_api_object *
uapi_get_object(struct uverbs_api *uapi, u16 object_id)
{
	const struct uverbs_api_object *res;

	if (object_id == UVERBS_IDR_ANY_OBJECT)
		return ERR_PTR(-ENOMSG);

	res = radix_tree_lookup(&uapi->radix, uapi_key_obj(object_id));
	if (!res)
		return ERR_PTR(-ENOENT);

	return res;
}

char *uapi_key_format(char *S, unsigned int key);
struct uverbs_api *uverbs_alloc_api(struct ib_device *ibdev);
void uverbs_disassociate_api_pre(struct ib_uverbs_device *uverbs_dev);
void uverbs_disassociate_api(struct uverbs_api *uapi);
void uverbs_destroy_api(struct uverbs_api *uapi);
void uapi_compute_bundle_size(struct uverbs_api_ioctl_method *method_elm,
			      unsigned int num_attrs);
void uverbs_user_mmap_disassociate(struct ib_uverbs_file *ufile);

extern const struct uapi_definition uverbs_def_obj_async_fd[];
extern const struct uapi_definition uverbs_def_obj_counters[];
extern const struct uapi_definition uverbs_def_obj_cq[];
extern const struct uapi_definition uverbs_def_obj_device[];
extern const struct uapi_definition uverbs_def_obj_dm[];
extern const struct uapi_definition uverbs_def_obj_flow_action[];
extern const struct uapi_definition uverbs_def_obj_intf[];
extern const struct uapi_definition uverbs_def_obj_mr[];
extern const struct uapi_definition uverbs_def_write_intf[];

static inline const struct uverbs_api_write_method *
uapi_get_method(const struct uverbs_api *uapi, u32 command)
{
	u32 cmd_idx = command & IB_USER_VERBS_CMD_COMMAND_MASK;

	if (command & ~(u32)(IB_USER_VERBS_CMD_FLAG_EXTENDED |
			     IB_USER_VERBS_CMD_COMMAND_MASK))
		return ERR_PTR(-EINVAL);

	if (command & IB_USER_VERBS_CMD_FLAG_EXTENDED) {
		if (cmd_idx >= uapi->num_write_ex)
			return ERR_PTR(-EOPNOTSUPP);
		return uapi->write_ex_methods[cmd_idx];
	}

	if (cmd_idx >= uapi->num_write)
		return ERR_PTR(-EOPNOTSUPP);
	return uapi->write_methods[cmd_idx];
}

void uverbs_fill_udata(struct uverbs_attr_bundle *bundle,
		       struct ib_udata *udata, unsigned int attr_in,
		       unsigned int attr_out);

#endif /* RDMA_CORE_H */
