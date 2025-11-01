/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef QCOMTEE_H
#define QCOMTEE_H

#include <linux/kobject.h>
#include <linux/tee_core.h>

#include "qcomtee_msg.h"
#include "qcomtee_object.h"

/* Flags relating to object reference. */
#define QCOMTEE_OBJREF_FLAG_TEE		BIT(0)
#define QCOMTEE_OBJREF_FLAG_USER	BIT(1)
#define QCOMTEE_OBJREF_FLAG_MEM		BIT(2)

/**
 * struct qcomtee - Main service struct.
 * @teedev: client device.
 * @pool: shared memory pool.
 * @ctx: driver private context.
 * @oic: context to use for the current driver invocation.
 * @wq: workqueue for QTEE async operations.
 * @xa_local_objects: array of objects exported to QTEE.
 * @xa_last_id: next ID to allocate.
 * @qtee_version: QTEE version.
 */
struct qcomtee {
	struct tee_device *teedev;
	struct tee_shm_pool *pool;
	struct tee_context *ctx;
	struct qcomtee_object_invoke_ctx oic;
	struct workqueue_struct *wq;
	struct xarray xa_local_objects;
	u32 xa_last_id;
	u32 qtee_version;
};

void qcomtee_fetch_async_reqs(struct qcomtee_object_invoke_ctx *oic);
struct qcomtee_object *qcomtee_idx_erase(struct qcomtee_object_invoke_ctx *oic,
					 u32 idx);

struct tee_shm_pool *qcomtee_shm_pool_alloc(void);
void qcomtee_msg_buffers_free(struct qcomtee_object_invoke_ctx *oic);
int qcomtee_msg_buffers_alloc(struct qcomtee_object_invoke_ctx *oic,
			      struct qcomtee_arg *u);

/**
 * qcomtee_object_do_invoke_internal() - Submit an invocation for an object.
 * @oic: context to use for the current invocation.
 * @object: object being invoked.
 * @op: requested operation on the object.
 * @u: array of arguments for the current invocation.
 * @result: result returned from QTEE.
 *
 * The caller is responsible for keeping track of the refcount for each
 * object, including @object. On return, the caller loses ownership of all
 * input objects of type %QCOMTEE_OBJECT_TYPE_CB.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
int qcomtee_object_do_invoke_internal(struct qcomtee_object_invoke_ctx *oic,
				      struct qcomtee_object *object, u32 op,
				      struct qcomtee_arg *u, int *result);

/**
 * struct qcomtee_context_data - Clients' or supplicants' context.
 * @qtee_objects_idr: QTEE objects in this context.
 * @qtee_lock: mutex for @qtee_objects_idr.
 * @reqs_idr: requests in this context that hold ID.
 * @reqs_list: FIFO for requests in PROCESSING or QUEUED state.
 * @reqs_lock: mutex for @reqs_idr, @reqs_list and request states.
 * @req_c: completion used when the supplicant is waiting for requests.
 * @released: state of this context.
 */
struct qcomtee_context_data {
	struct idr qtee_objects_idr;
	/* Synchronize access to @qtee_objects_idr. */
	struct mutex qtee_lock;

	struct idr reqs_idr;
	struct list_head reqs_list;
	/* Synchronize access to @reqs_idr, @reqs_list and updating requests states. */
	struct mutex reqs_lock;

	struct completion req_c;

	bool released;
};

int qcomtee_context_add_qtee_object(struct tee_param *param,
				    struct qcomtee_object *object,
				    struct tee_context *ctx);
int qcomtee_context_find_qtee_object(struct qcomtee_object **object,
				     struct tee_param *param,
				     struct tee_context *ctx);
void qcomtee_context_del_qtee_object(struct tee_param *param,
				     struct tee_context *ctx);

int qcomtee_objref_to_arg(struct qcomtee_arg *arg, struct tee_param *param,
			  struct tee_context *ctx);
int qcomtee_objref_from_arg(struct tee_param *param, struct qcomtee_arg *arg,
			    struct tee_context *ctx);

/* OBJECTS: */

/* (1) User Object API. */

int is_qcomtee_user_object(struct qcomtee_object *object);
void qcomtee_user_object_set_notify(struct qcomtee_object *object, bool notify);
void qcomtee_requests_destroy(struct qcomtee_context_data *ctxdata);
int qcomtee_user_param_to_object(struct qcomtee_object **object,
				 struct tee_param *param,
				 struct tee_context *ctx);
int qcomtee_user_param_from_object(struct tee_param *param,
				   struct qcomtee_object *object,
				   struct tee_context *ctx);

/**
 * struct qcomtee_user_object_request_data - Data for user object request.
 * @id: ID assigned to the request.
 * @object_id: Object ID being invoked by QTEE.
 * @op: Requested operation on object.
 * @np: Number of parameters in the request.
 */
struct qcomtee_user_object_request_data {
	int id;
	u64 object_id;
	u32 op;
	int np;
};

int qcomtee_user_object_select(struct tee_context *ctx,
			       struct tee_param *params, int num_params,
			       void __user *uaddr, size_t size,
			       struct qcomtee_user_object_request_data *data);
int qcomtee_user_object_submit(struct tee_context *ctx,
			       struct tee_param *params, int num_params,
			       int req_id, int errno);

/* (2) Primordial Object. */
extern struct qcomtee_object qcomtee_primordial_object;

/* (3) Memory Object API. */

/* Is it a memory object using tee_shm? */
int is_qcomtee_memobj_object(struct qcomtee_object *object);

/**
 * qcomtee_memobj_param_to_object() - OBJREF parameter to &struct qcomtee_object.
 * @object: object returned.
 * @param: TEE parameter.
 * @ctx: context in which the conversion should happen.
 *
 * @param is an OBJREF with %QCOMTEE_OBJREF_FLAG_MEM flags.
 *
 * Return: On success return 0 or <0 on failure.
 */
int qcomtee_memobj_param_to_object(struct qcomtee_object **object,
				   struct tee_param *param,
				   struct tee_context *ctx);

/* Reverse what qcomtee_memobj_param_to_object() does. */
int qcomtee_memobj_param_from_object(struct tee_param *param,
				     struct qcomtee_object *object,
				     struct tee_context *ctx);

/**
 * qcomtee_mem_object_map() - Map a memory object.
 * @object: memory object.
 * @map_object: created mapping object.
 * @mem_paddr: physical address of the memory.
 * @mem_size: size of the memory.
 * @perms: QTEE access permissions.
 *
 * Return: On success return 0 or <0 on failure.
 */
int qcomtee_mem_object_map(struct qcomtee_object *object,
			   struct qcomtee_object **map_object, u64 *mem_paddr,
			   u64 *mem_size, u32 *perms);

#endif /* QCOMTEE_H */
