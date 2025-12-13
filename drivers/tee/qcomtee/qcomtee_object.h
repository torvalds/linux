/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef QCOMTEE_OBJECT_H
#define QCOMTEE_OBJECT_H

#include <linux/completion.h>
#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

struct qcomtee_object;

/**
 * DOC: Overview
 *
 * qcomtee_object provides object refcounting, ID allocation for objects hosted
 * in the kernel, and necessary message marshaling for Qualcomm TEE (QTEE).
 *
 * To invoke an object in QTEE, the user calls qcomtee_object_do_invoke()
 * while passing an instance of &struct qcomtee_object and the requested
 * operation + arguments.
 *
 * After boot, QTEE provides a static object %ROOT_QCOMTEE_OBJECT (type of
 * %QCOMTEE_OBJECT_TYPE_ROOT). The root object is invoked to pass the user's
 * credentials and obtain other instances of &struct qcomtee_object (type of
 * %QCOMTEE_OBJECT_TYPE_TEE) that represent services and TAs in QTEE;
 * see &enum qcomtee_object_type.
 *
 * The objects received from QTEE are refcounted. So the owner of these objects
 * can issue qcomtee_object_get() to increase the refcount and pass objects
 * to other clients, or issue qcomtee_object_put() to decrease the refcount
 * and release the resources in QTEE.
 *
 * The kernel can host services accessible to QTEE. A driver should embed
 * an instance of &struct qcomtee_object in the struct it wants to export to
 * QTEE (this is called a callback object). It issues qcomtee_object_user_init()
 * to set the dispatch() operation for the callback object and set its type
 * to %QCOMTEE_OBJECT_TYPE_CB.
 *
 * core.c holds an object table for callback objects. An object ID is assigned
 * to each callback object, which is an index to the object table. QTEE uses
 * these IDs to reference or invoke callback objects.
 *
 * If QTEE invokes a callback object in the kernel, the dispatch() operation is
 * called in the context of the thread that originally called
 * qcomtee_object_do_invoke().
 */

/**
 * enum qcomtee_object_type - Object types.
 * @QCOMTEE_OBJECT_TYPE_TEE: object hosted on QTEE.
 * @QCOMTEE_OBJECT_TYPE_CB: object hosted on kernel.
 * @QCOMTEE_OBJECT_TYPE_ROOT: 'primordial' object.
 * @QCOMTEE_OBJECT_TYPE_NULL: NULL object.
 *
 * The primordial object is used for bootstrapping the IPC connection between
 * the kernel and QTEE. It is invoked by the kernel when it wants to get a
 * 'client env'.
 */
enum qcomtee_object_type {
	QCOMTEE_OBJECT_TYPE_TEE,
	QCOMTEE_OBJECT_TYPE_CB,
	QCOMTEE_OBJECT_TYPE_ROOT,
	QCOMTEE_OBJECT_TYPE_NULL,
};

/**
 * enum qcomtee_arg_type - Type of QTEE argument.
 * @QCOMTEE_ARG_TYPE_INV: invalid type.
 * @QCOMTEE_ARG_TYPE_OB: output buffer (OB).
 * @QCOMTEE_ARG_TYPE_OO: output object (OO).
 * @QCOMTEE_ARG_TYPE_IB: input buffer (IB).
 * @QCOMTEE_ARG_TYPE_IO: input object (IO).
 *
 * Use the invalid type to specify the end of the argument array.
 */
enum qcomtee_arg_type {
	QCOMTEE_ARG_TYPE_INV = 0,
	QCOMTEE_ARG_TYPE_OB,
	QCOMTEE_ARG_TYPE_OO,
	QCOMTEE_ARG_TYPE_IB,
	QCOMTEE_ARG_TYPE_IO,
	QCOMTEE_ARG_TYPE_NR,
};

/**
 * define QCOMTEE_ARGS_PER_TYPE - Maximum arguments of a specific type.
 *
 * The QTEE transport protocol limits the maximum number of arguments of
 * a specific type (i.e., IB, OB, IO, and OO).
 */
#define QCOMTEE_ARGS_PER_TYPE 16

/* Maximum arguments that can fit in a QTEE message, ignoring the type. */
#define QCOMTEE_ARGS_MAX (QCOMTEE_ARGS_PER_TYPE * (QCOMTEE_ARG_TYPE_NR - 1))

struct qcomtee_buffer {
	union {
		void *addr;
		void __user *uaddr;
	};
	size_t size;
};

/**
 * struct qcomtee_arg - Argument for QTEE object invocation.
 * @type: type of argument as &enum qcomtee_arg_type.
 * @flags: extra flags.
 * @b: address and size if the type of argument is a buffer.
 * @o: object instance if the type of argument is an object.
 *
 * &qcomtee_arg.flags only accepts %QCOMTEE_ARG_FLAGS_UADDR for now, which
 * states that &qcomtee_arg.b contains a userspace address in uaddr.
 */
struct qcomtee_arg {
	enum qcomtee_arg_type type;
/* 'b.uaddr' holds a __user address. */
#define QCOMTEE_ARG_FLAGS_UADDR BIT(0)
	unsigned int flags;
	union {
		struct qcomtee_buffer b;
		struct qcomtee_object *o;
	};
};

static inline int qcomtee_args_len(struct qcomtee_arg *args)
{
	int i = 0;

	while (args[i].type != QCOMTEE_ARG_TYPE_INV)
		i++;
	return i;
}

/* Context is busy (callback is in progress). */
#define QCOMTEE_OIC_FLAG_BUSY BIT(1)
/* Context needs to notify the current object. */
#define QCOMTEE_OIC_FLAG_NOTIFY BIT(2)
/* Context has shared state with QTEE. */
#define QCOMTEE_OIC_FLAG_SHARED BIT(3)

/**
 * struct qcomtee_object_invoke_ctx - QTEE context for object invocation.
 * @ctx: TEE context for this invocation.
 * @flags: flags for the invocation context.
 * @errno: error code for the invocation.
 * @object: current object invoked in this callback context.
 * @u: array of arguments for the current invocation (+1 for ending arg).
 * @in_msg: inbound buffer shared with QTEE.
 * @out_msg: outbound buffer shared with QTEE.
 * @in_shm: TEE shm allocated for inbound buffer.
 * @out_shm: TEE shm allocated for outbound buffer.
 * @data: extra data attached to this context.
 */
struct qcomtee_object_invoke_ctx {
	struct tee_context *ctx;
	unsigned long flags;
	int errno;

	struct qcomtee_object *object;
	struct qcomtee_arg u[QCOMTEE_ARGS_MAX + 1];

	struct qcomtee_buffer in_msg;
	struct qcomtee_buffer out_msg;
	struct tee_shm *in_shm;
	struct tee_shm *out_shm;

	void *data;
};

static inline struct qcomtee_object_invoke_ctx *
qcomtee_object_invoke_ctx_alloc(struct tee_context *ctx)
{
	struct qcomtee_object_invoke_ctx *oic;

	oic = kzalloc(sizeof(*oic), GFP_KERNEL);
	if (oic)
		oic->ctx = ctx;
	return oic;
}

/**
 * qcomtee_object_do_invoke() - Submit an invocation for an object.
 * @oic: context to use for the current invocation.
 * @object: object being invoked.
 * @op: requested operation on the object.
 * @u: array of arguments for the current invocation.
 * @result: result returned from QTEE.
 *
 * The caller is responsible for keeping track of the refcount for each object,
 * including @object. On return, the caller loses ownership of all input
 * objects of type %QCOMTEE_OBJECT_TYPE_CB.
 *
 * @object can be of %QCOMTEE_OBJECT_TYPE_ROOT or %QCOMTEE_OBJECT_TYPE_TEE.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
int qcomtee_object_do_invoke(struct qcomtee_object_invoke_ctx *oic,
			     struct qcomtee_object *object, u32 op,
			     struct qcomtee_arg *u, int *result);

/**
 * struct qcomtee_object_operations - Callback object operations.
 * @release: release the object if QTEE is not using it.
 * @dispatch: dispatch the operation requested by QTEE.
 * @notify: report the status of any pending response submitted by @dispatch.
 */
struct qcomtee_object_operations {
	void (*release)(struct qcomtee_object *object);
	int (*dispatch)(struct qcomtee_object_invoke_ctx *oic,
			struct qcomtee_object *object, u32 op,
			struct qcomtee_arg *args);
	void (*notify)(struct qcomtee_object_invoke_ctx *oic,
		       struct qcomtee_object *object, int err);
};

/**
 * struct qcomtee_object - QTEE or kernel object.
 * @name: object name.
 * @refcount: reference counter.
 * @object_type: object type as &enum qcomtee_object_type.
 * @info: extra information for the object.
 * @ops: callback operations for objects of type %QCOMTEE_OBJECT_TYPE_CB.
 * @work: work for async operations on the object.
 *
 * @work is used for releasing objects of %QCOMTEE_OBJECT_TYPE_TEE type.
 */
struct qcomtee_object {
	const char *name;
	struct kref refcount;

	enum qcomtee_object_type object_type;
	struct object_info {
		unsigned long qtee_id;
		/* TEE context for QTEE object async requests. */
		struct tee_context *qcomtee_async_ctx;
	} info;

	struct qcomtee_object_operations *ops;
	struct work_struct work;
};

/* Static instances of qcomtee_object objects. */
#define NULL_QCOMTEE_OBJECT ((struct qcomtee_object *)(0))
extern struct qcomtee_object qcomtee_object_root;
#define ROOT_QCOMTEE_OBJECT (&qcomtee_object_root)

static inline enum qcomtee_object_type
typeof_qcomtee_object(struct qcomtee_object *object)
{
	if (object == NULL_QCOMTEE_OBJECT)
		return QCOMTEE_OBJECT_TYPE_NULL;
	return object->object_type;
}

static inline const char *qcomtee_object_name(struct qcomtee_object *object)
{
	if (object == NULL_QCOMTEE_OBJECT)
		return "null";

	if (!object->name)
		return "no-name";
	return object->name;
}

/**
 * qcomtee_object_user_init() - Initialize an object for the user.
 * @object: object to initialize.
 * @ot: type of object as &enum qcomtee_object_type.
 * @ops: instance of callbacks.
 * @fmt: name assigned to the object.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
int qcomtee_object_user_init(struct qcomtee_object *object,
			     enum qcomtee_object_type ot,
			     struct qcomtee_object_operations *ops,
			     const char *fmt, ...) __printf(4, 5);

/* Object release is RCU protected. */
int qcomtee_object_get(struct qcomtee_object *object);
void qcomtee_object_put(struct qcomtee_object *object);

#define qcomtee_arg_for_each(i, args) \
	for (i = 0; args[i].type != QCOMTEE_ARG_TYPE_INV; i++)

/* Next argument of type @type after index @i. */
int qcomtee_next_arg_type(struct qcomtee_arg *u, int i,
			  enum qcomtee_arg_type type);

/* Iterate over argument of given type. */
#define qcomtee_arg_for_each_type(i, args, at)       \
	for (i = qcomtee_next_arg_type(args, 0, at); \
	     args[i].type != QCOMTEE_ARG_TYPE_INV;   \
	     i = qcomtee_next_arg_type(args, i + 1, at))

#define qcomtee_arg_for_each_input_buffer(i, args) \
	qcomtee_arg_for_each_type(i, args, QCOMTEE_ARG_TYPE_IB)
#define qcomtee_arg_for_each_output_buffer(i, args) \
	qcomtee_arg_for_each_type(i, args, QCOMTEE_ARG_TYPE_OB)
#define qcomtee_arg_for_each_input_object(i, args) \
	qcomtee_arg_for_each_type(i, args, QCOMTEE_ARG_TYPE_IO)
#define qcomtee_arg_for_each_output_object(i, args) \
	qcomtee_arg_for_each_type(i, args, QCOMTEE_ARG_TYPE_OO)

struct qcomtee_object *
qcomtee_object_get_client_env(struct qcomtee_object_invoke_ctx *oic);

struct qcomtee_object *
qcomtee_object_get_service(struct qcomtee_object_invoke_ctx *oic,
			   struct qcomtee_object *client_env, u32 uid);

#endif /* QCOMTEE_OBJECT_H */
