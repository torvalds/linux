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

int uverbs_ns_idx(u16 *id, unsigned int ns_count);
const struct uverbs_object_spec *uverbs_get_object(const struct ib_device *ibdev,
						   uint16_t object);
const struct uverbs_method_spec *uverbs_get_method(const struct uverbs_object_spec *object,
						   uint16_t method);
/*
 * These functions initialize the context and cleanups its uobjects.
 * The context has a list of objects which is protected by a mutex
 * on the context. initialize_ucontext should be called when we create
 * a context.
 * cleanup_ucontext removes all uobjects from the context and puts them.
 */
void uverbs_cleanup_ucontext(struct ib_ucontext *ucontext, bool device_removed);
void uverbs_initialize_ucontext(struct ib_ucontext *ucontext);

/*
 * uverbs_uobject_get is called in order to increase the reference count on
 * an uobject. This is useful when a handler wants to keep the uobject's memory
 * alive, regardless if this uobject is still alive in the context's objects
 * repository. Objects are put via uverbs_uobject_put.
 */
void uverbs_uobject_get(struct ib_uobject *uobject);

/*
 * In order to indicate we no longer needs this uobject, uverbs_uobject_put
 * is called. When the reference count is decreased, the uobject is freed.
 * For example, this is used when attaching a completion channel to a CQ.
 */
void uverbs_uobject_put(struct ib_uobject *uobject);

/* Indicate this fd is no longer used by this consumer, but its memory isn't
 * necessarily released yet. When the last reference is put, we release the
 * memory. After this call is executed, calling uverbs_uobject_get isn't
 * allowed.
 * This must be called from the release file_operations of the file!
 */
void uverbs_close_fd(struct file *f);

/*
 * Get an ib_uobject that corresponds to the given id from ucontext, assuming
 * the object is from the given type. Lock it to the required access when
 * applicable.
 * This function could create (access == NEW), destroy (access == DESTROY)
 * or unlock (access == READ || access == WRITE) objects if required.
 * The action will be finalized only when uverbs_finalize_object or
 * uverbs_finalize_objects are called.
 */
struct ib_uobject *uverbs_get_uobject_from_context(const struct uverbs_obj_type *type_attrs,
						   struct ib_ucontext *ucontext,
						   enum uverbs_obj_access access,
						   int id);
/*
 * Note that certain finalize stages could return a status:
 *   (a) alloc_commit could return a failure if the object is committed at the
 *       same time when the context is destroyed.
 *   (b) remove_commit could fail if the object wasn't destroyed successfully.
 * Since multiple objects could be finalized in one transaction, it is very NOT
 * recommended to have several finalize actions which have side effects.
 * For example, it's NOT recommended to have a certain action which has both
 * a commit action and a destroy action or two destroy objects in the same
 * action. The rule of thumb is to have one destroy or commit action with
 * multiple lookups.
 * The first non zero return value of finalize_object is returned from this
 * function. For example, this could happen when we couldn't destroy an
 * object.
 */
int uverbs_finalize_object(struct ib_uobject *uobj,
			   enum uverbs_obj_access access,
			   bool commit);

#endif /* RDMA_CORE_H */
