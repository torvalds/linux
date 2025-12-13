// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/xarray.h>

#include "qcomtee.h"

/* QTEE root object. */
struct qcomtee_object qcomtee_object_root = {
	.name = "root",
	.object_type = QCOMTEE_OBJECT_TYPE_ROOT,
	.info.qtee_id = QCOMTEE_MSG_OBJECT_ROOT,
};

/* Next argument of type @type after index @i. */
int qcomtee_next_arg_type(struct qcomtee_arg *u, int i,
			  enum qcomtee_arg_type type)
{
	while (u[i].type != QCOMTEE_ARG_TYPE_INV && u[i].type != type)
		i++;
	return i;
}

/*
 * QTEE expects IDs with QCOMTEE_MSG_OBJECT_NS_BIT set for objects of
 * QCOMTEE_OBJECT_TYPE_CB type. The first ID with QCOMTEE_MSG_OBJECT_NS_BIT
 * set is reserved for the primordial object.
 */
#define QCOMTEE_OBJECT_PRIMORDIAL (QCOMTEE_MSG_OBJECT_NS_BIT)
#define QCOMTEE_OBJECT_ID_START (QCOMTEE_OBJECT_PRIMORDIAL + 1)
#define QCOMTEE_OBJECT_ID_END (U32_MAX)

#define QCOMTEE_OBJECT_SET(p, type, ...) \
	__QCOMTEE_OBJECT_SET(p, type, ##__VA_ARGS__, 0UL)
#define __QCOMTEE_OBJECT_SET(p, type, optr, ...)           \
	do {                                               \
		(p)->object_type = (type);                 \
		(p)->info.qtee_id = (unsigned long)(optr); \
	} while (0)

static struct qcomtee_object *
qcomtee_qtee_object_alloc(struct qcomtee_object_invoke_ctx *oic,
			  unsigned int object_id)
{
	struct qcomtee *qcomtee = tee_get_drvdata(oic->ctx->teedev);
	struct qcomtee_object *object;

	object = kzalloc(sizeof(*object), GFP_KERNEL);
	if (!object)
		return NULL_QCOMTEE_OBJECT;

	/* If failed, "no-name". */
	object->name = kasprintf(GFP_KERNEL, "qcomtee-%u", object_id);
	QCOMTEE_OBJECT_SET(object, QCOMTEE_OBJECT_TYPE_TEE, object_id);
	kref_init(&object->refcount);
	/* A QTEE object requires a context for async operations. */
	object->info.qcomtee_async_ctx = qcomtee->ctx;
	teedev_ctx_get(object->info.qcomtee_async_ctx);

	return object;
}

static void qcomtee_qtee_object_free(struct qcomtee_object *object)
{
	/* See qcomtee_qtee_object_alloc(). */
	teedev_ctx_put(object->info.qcomtee_async_ctx);

	kfree(object->name);
	kfree(object);
}

static void qcomtee_do_release_qtee_object(struct work_struct *work)
{
	struct qcomtee_object *object;
	struct qcomtee *qcomtee;
	int ret, result = 0;

	/* RELEASE does not require any argument. */
	struct qcomtee_arg args[] = { { .type = QCOMTEE_ARG_TYPE_INV } };

	object = container_of(work, struct qcomtee_object, work);
	qcomtee = tee_get_drvdata(object->info.qcomtee_async_ctx->teedev);
	/* Get the TEE context used for asynchronous operations. */
	qcomtee->oic.ctx = object->info.qcomtee_async_ctx;

	ret = qcomtee_object_do_invoke_internal(&qcomtee->oic, object,
						QCOMTEE_MSG_OBJECT_OP_RELEASE,
						args, &result);

	/* Is it safe to retry the release? */
	if (ret && ret != -ENODEV) {
		queue_work(qcomtee->wq, &object->work);
	} else {
		if (ret || result)
			pr_err("%s release failed, ret = %d (%x)\n",
			       qcomtee_object_name(object), ret, result);
		qcomtee_qtee_object_free(object);
	}
}

static void qcomtee_release_qtee_object(struct qcomtee_object *object)
{
	struct qcomtee *qcomtee =
		tee_get_drvdata(object->info.qcomtee_async_ctx->teedev);

	INIT_WORK(&object->work, qcomtee_do_release_qtee_object);
	queue_work(qcomtee->wq, &object->work);
}

static void qcomtee_object_release(struct kref *refcount)
{
	struct qcomtee_object *object;
	const char *name;

	object = container_of(refcount, struct qcomtee_object, refcount);

	/*
	 * qcomtee_object_get() is called in a RCU read lock. synchronize_rcu()
	 * to avoid releasing the object while it is being accessed in
	 * qcomtee_object_get().
	 */
	synchronize_rcu();

	switch (typeof_qcomtee_object(object)) {
	case QCOMTEE_OBJECT_TYPE_TEE:
		qcomtee_release_qtee_object(object);

		break;
	case QCOMTEE_OBJECT_TYPE_CB:
		name = object->name;

		if (object->ops->release)
			object->ops->release(object);

		kfree_const(name);

		break;
	case QCOMTEE_OBJECT_TYPE_ROOT:
	case QCOMTEE_OBJECT_TYPE_NULL:
	default:
		break;
	}
}

/**
 * qcomtee_object_get() - Increase the object's reference count.
 * @object: object to increase the reference count.
 *
 * Context: The caller should hold RCU read lock.
 */
int qcomtee_object_get(struct qcomtee_object *object)
{
	if (object != &qcomtee_primordial_object &&
	    object != NULL_QCOMTEE_OBJECT &&
	    object != ROOT_QCOMTEE_OBJECT)
		return kref_get_unless_zero(&object->refcount);

	return 0;
}

/**
 * qcomtee_object_put() - Decrease the object's reference count.
 * @object: object to decrease the reference count.
 */
void qcomtee_object_put(struct qcomtee_object *object)
{
	if (object != &qcomtee_primordial_object &&
	    object != NULL_QCOMTEE_OBJECT &&
	    object != ROOT_QCOMTEE_OBJECT)
		kref_put(&object->refcount, qcomtee_object_release);
}

static int qcomtee_idx_alloc(struct qcomtee_object_invoke_ctx *oic, u32 *idx,
			     struct qcomtee_object *object)
{
	struct qcomtee *qcomtee = tee_get_drvdata(oic->ctx->teedev);

	/* Every ID allocated here has QCOMTEE_MSG_OBJECT_NS_BIT set. */
	return xa_alloc_cyclic(&qcomtee->xa_local_objects, idx, object,
			       XA_LIMIT(QCOMTEE_OBJECT_ID_START,
					QCOMTEE_OBJECT_ID_END),
			       &qcomtee->xa_last_id, GFP_KERNEL);
}

struct qcomtee_object *qcomtee_idx_erase(struct qcomtee_object_invoke_ctx *oic,
					 u32 idx)
{
	struct qcomtee *qcomtee = tee_get_drvdata(oic->ctx->teedev);

	if (idx < QCOMTEE_OBJECT_ID_START || idx > QCOMTEE_OBJECT_ID_END)
		return NULL_QCOMTEE_OBJECT;

	return xa_erase(&qcomtee->xa_local_objects, idx);
}

/**
 * qcomtee_object_id_get() - Get an ID for an object to send to QTEE.
 * @oic: context to use for the invocation.
 * @object: object to assign an ID.
 * @object_id: object ID.
 *
 * Called on the path to QTEE to construct the message; see
 * qcomtee_prepare_msg() and qcomtee_update_msg().
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
static int qcomtee_object_id_get(struct qcomtee_object_invoke_ctx *oic,
				 struct qcomtee_object *object,
				 unsigned int *object_id)
{
	u32 idx;

	switch (typeof_qcomtee_object(object)) {
	case QCOMTEE_OBJECT_TYPE_CB:
		if (qcomtee_idx_alloc(oic, &idx, object) < 0)
			return -ENOSPC;

		*object_id = idx;

		break;
	case QCOMTEE_OBJECT_TYPE_ROOT:
	case QCOMTEE_OBJECT_TYPE_TEE:
		*object_id = object->info.qtee_id;

		break;
	case QCOMTEE_OBJECT_TYPE_NULL:
		*object_id = QCOMTEE_MSG_OBJECT_NULL;

		break;
	}

	return 0;
}

/* Release object ID assigned in qcomtee_object_id_get. */
static void qcomtee_object_id_put(struct qcomtee_object_invoke_ctx *oic,
				  unsigned int object_id)
{
	qcomtee_idx_erase(oic, object_id);
}

/**
 * qcomtee_local_object_get() - Get the object referenced by the ID.
 * @oic: context to use for the invocation.
 * @object_id: object ID.
 *
 * It is called on the path from QTEE.
 * It is called on behalf of QTEE to obtain an instance of an object
 * for a given ID. It increases the object's reference count on success.
 *
 * Return: On error, returns %NULL_QCOMTEE_OBJECT.
 *         On success, returns the object.
 */
static struct qcomtee_object *
qcomtee_local_object_get(struct qcomtee_object_invoke_ctx *oic,
			 unsigned int object_id)
{
	struct qcomtee *qcomtee = tee_get_drvdata(oic->ctx->teedev);
	struct qcomtee_object *object;

	if (object_id == QCOMTEE_OBJECT_PRIMORDIAL)
		return &qcomtee_primordial_object;

	guard(rcu)();
	object = xa_load(&qcomtee->xa_local_objects, object_id);
	/* It already checks for %NULL_QCOMTEE_OBJECT. */
	qcomtee_object_get(object);

	return object;
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
			     const char *fmt, ...)
{
	va_list ap;
	int ret;

	kref_init(&object->refcount);
	QCOMTEE_OBJECT_SET(object, QCOMTEE_OBJECT_TYPE_NULL);

	va_start(ap, fmt);
	switch (ot) {
	case QCOMTEE_OBJECT_TYPE_NULL:
		ret = 0;

		break;
	case QCOMTEE_OBJECT_TYPE_CB:
		object->ops = ops;
		if (!object->ops->dispatch)
			return -EINVAL;

		/* If failed, "no-name". */
		object->name = kvasprintf_const(GFP_KERNEL, fmt, ap);
		QCOMTEE_OBJECT_SET(object, QCOMTEE_OBJECT_TYPE_CB);

		ret = 0;
		break;
	case QCOMTEE_OBJECT_TYPE_ROOT:
	case QCOMTEE_OBJECT_TYPE_TEE:
	default:
		ret = -EINVAL;
	}
	va_end(ap);

	return ret;
}

/**
 * qcomtee_object_type() - Returns the type of object represented by an ID.
 * @object_id: object ID for the object.
 *
 * Similar to typeof_qcomtee_object(), but instead of receiving an object as
 * an argument, it receives an object ID. It is used internally on the return
 * path from QTEE.
 *
 * Return: Returns the type of object referenced by @object_id.
 */
static enum qcomtee_object_type qcomtee_object_type(unsigned int object_id)
{
	if (object_id == QCOMTEE_MSG_OBJECT_NULL)
		return QCOMTEE_OBJECT_TYPE_NULL;

	if (object_id & QCOMTEE_MSG_OBJECT_NS_BIT)
		return QCOMTEE_OBJECT_TYPE_CB;

	return QCOMTEE_OBJECT_TYPE_TEE;
}

/**
 * qcomtee_object_qtee_init() - Initialize an object for QTEE.
 * @oic: context to use for the invocation.
 * @object: object returned.
 * @object_id: object ID received from QTEE.
 *
 * Return: On failure, returns < 0 and sets @object to %NULL_QCOMTEE_OBJECT.
 *         On success, returns 0
 */
static int qcomtee_object_qtee_init(struct qcomtee_object_invoke_ctx *oic,
				    struct qcomtee_object **object,
				    unsigned int object_id)
{
	int ret = 0;

	switch (qcomtee_object_type(object_id)) {
	case QCOMTEE_OBJECT_TYPE_NULL:
		*object = NULL_QCOMTEE_OBJECT;

		break;
	case QCOMTEE_OBJECT_TYPE_CB:
		*object = qcomtee_local_object_get(oic, object_id);
		if (*object == NULL_QCOMTEE_OBJECT)
			ret = -EINVAL;

		break;

	default: /* QCOMTEE_OBJECT_TYPE_TEE */
		*object = qcomtee_qtee_object_alloc(oic, object_id);
		if (*object == NULL_QCOMTEE_OBJECT)
			ret = -ENOMEM;

		break;
	}

	return ret;
}

/*
 * ''Marshaling API''
 * qcomtee_prepare_msg  - Prepare the inbound buffer for sending to QTEE
 * qcomtee_update_args  - Parse the QTEE response in the inbound buffer
 * qcomtee_prepare_args - Parse the QTEE request from the outbound buffer
 * qcomtee_update_msg   - Update the outbound buffer with the response for QTEE
 */

static int qcomtee_prepare_msg(struct qcomtee_object_invoke_ctx *oic,
			       struct qcomtee_object *object, u32 op,
			       struct qcomtee_arg *u)
{
	struct qcomtee_msg_object_invoke *msg;
	unsigned int object_id;
	int i, ib, ob, io, oo;
	size_t offset;

	/* Use the input message buffer in 'oic'. */
	msg = oic->in_msg.addr;

	/* Start offset in a message for buffer arguments. */
	offset = qcomtee_msg_buffer_args(struct qcomtee_msg_object_invoke,
					 qcomtee_args_len(u));

	/* Get the ID of the object being invoked. */
	if (qcomtee_object_id_get(oic, object, &object_id))
		return -ENOSPC;

	ib = 0;
	qcomtee_arg_for_each_input_buffer(i, u) {
		void *msgptr; /* Address of buffer payload: */
		/* Overflow already checked in qcomtee_msg_buffers_alloc(). */
		msg->args[ib].b.offset = offset;
		msg->args[ib].b.size = u[i].b.size;

		msgptr = qcomtee_msg_offset_to_ptr(msg, offset);
		/* Userspace client or kernel client!? */
		if (!(u[i].flags & QCOMTEE_ARG_FLAGS_UADDR))
			memcpy(msgptr, u[i].b.addr, u[i].b.size);
		else if (copy_from_user(msgptr, u[i].b.uaddr, u[i].b.size))
			return -EFAULT;

		offset += qcomtee_msg_offset_align(u[i].b.size);
		ib++;
	}

	ob = ib;
	qcomtee_arg_for_each_output_buffer(i, u) {
		/* Overflow already checked in qcomtee_msg_buffers_alloc(). */
		msg->args[ob].b.offset = offset;
		msg->args[ob].b.size = u[i].b.size;

		offset += qcomtee_msg_offset_align(u[i].b.size);
		ob++;
	}

	io = ob;
	qcomtee_arg_for_each_input_object(i, u) {
		if (qcomtee_object_id_get(oic, u[i].o, &msg->args[io].o)) {
			qcomtee_object_id_put(oic, object_id);
			for (io--; io >= ob; io--)
				qcomtee_object_id_put(oic, msg->args[io].o);

			return -ENOSPC;
		}

		io++;
	}

	oo = io;
	qcomtee_arg_for_each_output_object(i, u)
		oo++;

	/* Set object, operation, and argument counts. */
	qcomtee_msg_init(msg, object_id, op, ib, ob, io, oo);

	return 0;
}

/**
 * qcomtee_update_args() - Parse the QTEE response in the inbound buffer.
 * @u: array of arguments for the invocation.
 * @oic: context to use for the invocation.
 *
 * @u must be the same as the one used in qcomtee_prepare_msg() when
 * initializing the inbound buffer.
 *
 * On failure, it continues processing the QTEE message. The caller should
 * do the necessary cleanup, including calling qcomtee_object_put()
 * on the output objects.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
static int qcomtee_update_args(struct qcomtee_arg *u,
			       struct qcomtee_object_invoke_ctx *oic)
{
	struct qcomtee_msg_object_invoke *msg;
	int i, ib, ob, io, oo;
	int ret = 0;

	/* Use the input message buffer in 'oic'. */
	msg = oic->in_msg.addr;

	ib = 0;
	qcomtee_arg_for_each_input_buffer(i, u)
		ib++;

	ob = ib;
	qcomtee_arg_for_each_output_buffer(i, u) {
		void *msgptr; /* Address of buffer payload: */
		/* QTEE can override the size to a smaller value. */
		u[i].b.size = msg->args[ob].b.size;

		msgptr = qcomtee_msg_offset_to_ptr(msg, msg->args[ob].b.offset);
		/* Userspace client or kernel client!? */
		if (!(u[i].flags & QCOMTEE_ARG_FLAGS_UADDR))
			memcpy(u[i].b.addr, msgptr, u[i].b.size);
		else if (copy_to_user(u[i].b.uaddr, msgptr, u[i].b.size))
			ret = -EINVAL;

		ob++;
	}

	io = ob;
	qcomtee_arg_for_each_input_object(i, u)
		io++;

	oo = io;
	qcomtee_arg_for_each_output_object(i, u) {
		if (qcomtee_object_qtee_init(oic, &u[i].o, msg->args[oo].o))
			ret = -EINVAL;

		oo++;
	}

	return ret;
}

/**
 * qcomtee_prepare_args() - Parse the QTEE request from the outbound buffer.
 * @oic: context to use for the invocation.
 *
 * It initializes &qcomtee_object_invoke_ctx->u based on the QTEE request in
 * the outbound buffer. It sets %QCOMTEE_ARG_TYPE_INV at the end of the array.
 *
 * On failure, it continues processing the QTEE message. The caller should
 * do the necessary cleanup, including calling qcomtee_object_put()
 * on the input objects.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
static int qcomtee_prepare_args(struct qcomtee_object_invoke_ctx *oic)
{
	struct qcomtee_msg_callback *msg;
	int i, ret = 0;

	/* Use the output message buffer in 'oic'. */
	msg = oic->out_msg.addr;

	qcomtee_msg_for_each_input_buffer(i, msg) {
		oic->u[i].b.addr =
			qcomtee_msg_offset_to_ptr(msg, msg->args[i].b.offset);
		oic->u[i].b.size = msg->args[i].b.size;
		oic->u[i].type = QCOMTEE_ARG_TYPE_IB;
	}

	qcomtee_msg_for_each_output_buffer(i, msg) {
		oic->u[i].b.addr =
			qcomtee_msg_offset_to_ptr(msg, msg->args[i].b.offset);
		oic->u[i].b.size = msg->args[i].b.size;
		oic->u[i].type = QCOMTEE_ARG_TYPE_OB;
	}

	qcomtee_msg_for_each_input_object(i, msg) {
		if (qcomtee_object_qtee_init(oic, &oic->u[i].o, msg->args[i].o))
			ret = -EINVAL;

		oic->u[i].type = QCOMTEE_ARG_TYPE_IO;
	}

	qcomtee_msg_for_each_output_object(i, msg)
		oic->u[i].type = QCOMTEE_ARG_TYPE_OO;

	/* End of Arguments. */
	oic->u[i].type = QCOMTEE_ARG_TYPE_INV;

	return ret;
}

static int qcomtee_update_msg(struct qcomtee_object_invoke_ctx *oic)
{
	struct qcomtee_msg_callback *msg;
	int i, ib, ob, io, oo;

	/* Use the output message buffer in 'oic'. */
	msg = oic->out_msg.addr;

	ib = 0;
	qcomtee_arg_for_each_input_buffer(i, oic->u)
		ib++;

	ob = ib;
	qcomtee_arg_for_each_output_buffer(i, oic->u) {
		/* Only reduce size; never increase it. */
		if (msg->args[ob].b.size < oic->u[i].b.size)
			return -EINVAL;

		msg->args[ob].b.size = oic->u[i].b.size;
		ob++;
	}

	io = ob;
	qcomtee_arg_for_each_input_object(i, oic->u)
		io++;

	oo = io;
	qcomtee_arg_for_each_output_object(i, oic->u) {
		if (qcomtee_object_id_get(oic, oic->u[i].o, &msg->args[oo].o)) {
			for (oo--; oo >= io; oo--)
				qcomtee_object_id_put(oic, msg->args[oo].o);

			return -ENOSPC;
		}

		oo++;
	}

	return 0;
}

/* Invoke a callback object. */
static void qcomtee_cb_object_invoke(struct qcomtee_object_invoke_ctx *oic,
				     struct qcomtee_msg_callback *msg)
{
	int i, errno;
	u32 op;

	/* Get the object being invoked. */
	unsigned int object_id = msg->cxt;
	struct qcomtee_object *object;

	/* QTEE cannot invoke a NULL object or objects it hosts. */
	if (qcomtee_object_type(object_id) == QCOMTEE_OBJECT_TYPE_NULL ||
	    qcomtee_object_type(object_id) == QCOMTEE_OBJECT_TYPE_TEE) {
		errno = -EINVAL;
		goto out;
	}

	object = qcomtee_local_object_get(oic, object_id);
	if (object == NULL_QCOMTEE_OBJECT) {
		errno = -EINVAL;
		goto out;
	}

	oic->object = object;

	/* Filter bits used by transport. */
	op = msg->op & QCOMTEE_MSG_OBJECT_OP_MASK;

	switch (op) {
	case QCOMTEE_MSG_OBJECT_OP_RELEASE:
		qcomtee_object_id_put(oic, object_id);
		qcomtee_object_put(object);
		errno = 0;

		break;
	case QCOMTEE_MSG_OBJECT_OP_RETAIN:
		qcomtee_object_get(object);
		errno = 0;

		break;
	default:
		errno = qcomtee_prepare_args(oic);
		if (errno) {
			/* Release any object that arrived as input. */
			qcomtee_arg_for_each_input_buffer(i, oic->u)
				qcomtee_object_put(oic->u[i].o);

			break;
		}

		errno = object->ops->dispatch(oic, object, op, oic->u);
		if (!errno) {
			/* On success, notify at the appropriate time. */
			oic->flags |= QCOMTEE_OIC_FLAG_NOTIFY;
		}
	}

out:

	oic->errno = errno;
}

static int
qcomtee_object_invoke_ctx_invoke(struct qcomtee_object_invoke_ctx *oic,
				 int *result, u64 *res_type)
{
	phys_addr_t out_msg_paddr;
	phys_addr_t in_msg_paddr;
	int ret;
	u64 res;

	tee_shm_get_pa(oic->out_shm, 0, &out_msg_paddr);
	tee_shm_get_pa(oic->in_shm, 0, &in_msg_paddr);
	if (!(oic->flags & QCOMTEE_OIC_FLAG_BUSY))
		ret = qcom_scm_qtee_invoke_smc(in_msg_paddr, oic->in_msg.size,
					       out_msg_paddr, oic->out_msg.size,
					       &res, res_type);
	else
		ret = qcom_scm_qtee_callback_response(out_msg_paddr,
						      oic->out_msg.size,
						      &res, res_type);

	if (ret)
		pr_err("QTEE returned with %d.\n", ret);
	else
		*result = (int)res;

	return ret;
}

/**
 * qcomtee_qtee_objects_put() - Put the callback objects in the argument array.
 * @u: array of arguments.
 *
 * When qcomtee_object_do_invoke_internal() is successfully invoked,
 * QTEE takes ownership of the callback objects. If the invocation fails,
 * qcomtee_object_do_invoke_internal() calls qcomtee_qtee_objects_put()
 * to mimic the release of callback objects by QTEE.
 */
static void qcomtee_qtee_objects_put(struct qcomtee_arg *u)
{
	int i;

	qcomtee_arg_for_each_input_object(i, u) {
		if (typeof_qcomtee_object(u[i].o) == QCOMTEE_OBJECT_TYPE_CB)
			qcomtee_object_put(u[i].o);
	}
}

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
				      struct qcomtee_arg *u, int *result)
{
	struct qcomtee_msg_callback *cb_msg;
	struct qcomtee_object *qto;
	int i, ret, errno;
	u64 res_type;

	/* Allocate inbound and outbound buffers. */
	ret = qcomtee_msg_buffers_alloc(oic, u);
	if (ret) {
		qcomtee_qtee_objects_put(u);

		return ret;
	}

	ret = qcomtee_prepare_msg(oic, object, op, u);
	if (ret) {
		qcomtee_qtee_objects_put(u);

		goto out;
	}

	/* Use input message buffer in 'oic'. */
	cb_msg = oic->out_msg.addr;

	while (1) {
		if (oic->flags & QCOMTEE_OIC_FLAG_BUSY) {
			errno = oic->errno;
			if (!errno)
				errno = qcomtee_update_msg(oic);
			qcomtee_msg_set_result(cb_msg, errno);
		}

		/* Invoke the remote object. */
		ret = qcomtee_object_invoke_ctx_invoke(oic, result, &res_type);
		/* Return form callback objects result submission: */
		if (oic->flags & QCOMTEE_OIC_FLAG_BUSY) {
			qto = oic->object;
			if (qto) {
				if (oic->flags & QCOMTEE_OIC_FLAG_NOTIFY) {
					if (qto->ops->notify)
						qto->ops->notify(oic, qto,
								 errno || ret);
				}

				/* Get is in qcomtee_cb_object_invoke(). */
				qcomtee_object_put(qto);
			}

			oic->object = NULL_QCOMTEE_OBJECT;
			oic->flags &= ~(QCOMTEE_OIC_FLAG_BUSY |
					QCOMTEE_OIC_FLAG_NOTIFY);
		}

		if (ret) {
			/*
			 * Unable to finished the invocation.
			 * If QCOMTEE_OIC_FLAG_SHARED is not set, put
			 * QCOMTEE_OBJECT_TYPE_CB input objects.
			 */
			if (!(oic->flags & QCOMTEE_OIC_FLAG_SHARED))
				qcomtee_qtee_objects_put(u);
			else
				ret = -ENODEV;

			goto out;

		} else {
			/*
			 * QTEE obtained ownership of QCOMTEE_OBJECT_TYPE_CB
			 * input objects in 'u'. On further failure, QTEE is
			 * responsible for releasing them.
			 */
			oic->flags |= QCOMTEE_OIC_FLAG_SHARED;
		}

		/* Is it a callback request? */
		if (res_type != QCOMTEE_RESULT_INBOUND_REQ_NEEDED) {
			/*
			 * Parse results. If failed, assume the service
			 * was unavailable (i.e. QCOMTEE_MSG_ERROR_UNAVAIL)
			 * and put output objects to initiate cleanup.
			 */
			if (!*result && qcomtee_update_args(u, oic)) {
				*result = QCOMTEE_MSG_ERROR_UNAVAIL;
				qcomtee_arg_for_each_output_object(i, u)
					qcomtee_object_put(u[i].o);
			}

			break;

		} else {
			oic->flags |= QCOMTEE_OIC_FLAG_BUSY;
			qcomtee_fetch_async_reqs(oic);
			qcomtee_cb_object_invoke(oic, cb_msg);
		}
	}

	qcomtee_fetch_async_reqs(oic);
out:
	qcomtee_msg_buffers_free(oic);

	return ret;
}

int qcomtee_object_do_invoke(struct qcomtee_object_invoke_ctx *oic,
			     struct qcomtee_object *object, u32 op,
			     struct qcomtee_arg *u, int *result)
{
	/* User can not set bits used by transport. */
	if (op & ~QCOMTEE_MSG_OBJECT_OP_MASK)
		return -EINVAL;

	/* User can only invoke QTEE hosted objects. */
	if (typeof_qcomtee_object(object) != QCOMTEE_OBJECT_TYPE_TEE &&
	    typeof_qcomtee_object(object) != QCOMTEE_OBJECT_TYPE_ROOT)
		return -EINVAL;

	/* User cannot directly issue these operations to QTEE. */
	if (op == QCOMTEE_MSG_OBJECT_OP_RELEASE ||
	    op == QCOMTEE_MSG_OBJECT_OP_RETAIN)
		return -EINVAL;

	return qcomtee_object_do_invoke_internal(oic, object, op, u, result);
}

/**
 * qcomtee_object_get_client_env() - Get a privileged client env. object.
 * @oic: context to use for the current invocation.
 *
 * The caller should call qcomtee_object_put() on the returned object
 * to release it.
 *
 * Return: On error, returns %NULL_QCOMTEE_OBJECT.
 *         On success, returns the object.
 */
struct qcomtee_object *
qcomtee_object_get_client_env(struct qcomtee_object_invoke_ctx *oic)
{
	struct qcomtee_arg u[3] = { 0 };
	int ret, result;

	u[0].o = NULL_QCOMTEE_OBJECT;
	u[0].type = QCOMTEE_ARG_TYPE_IO;
	u[1].type = QCOMTEE_ARG_TYPE_OO;
	ret = qcomtee_object_do_invoke(oic, ROOT_QCOMTEE_OBJECT,
				       QCOMTEE_ROOT_OP_REG_WITH_CREDENTIALS, u,
				       &result);
	if (ret || result)
		return NULL_QCOMTEE_OBJECT;

	return u[1].o;
}

struct qcomtee_object *
qcomtee_object_get_service(struct qcomtee_object_invoke_ctx *oic,
			   struct qcomtee_object *client_env, u32 uid)
{
	struct qcomtee_arg u[3] = { 0 };
	int ret, result;

	u[0].b.addr = &uid;
	u[0].b.size = sizeof(uid);
	u[0].type = QCOMTEE_ARG_TYPE_IB;
	u[1].type = QCOMTEE_ARG_TYPE_OO;
	ret = qcomtee_object_do_invoke(oic, client_env, QCOMTEE_CLIENT_ENV_OPEN,
				       u, &result);

	if (ret || result)
		return NULL_QCOMTEE_OBJECT;

	return u[1].o;
}
