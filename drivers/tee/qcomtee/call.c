// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <linux/tee.h>
#include <linux/platform_device.h>
#include <linux/xarray.h>

#include "qcomtee.h"

static int find_qtee_object(struct qcomtee_object **object, unsigned long id,
			    struct qcomtee_context_data *ctxdata)
{
	int err = 0;

	guard(rcu)();
	/* Object release is RCU protected. */
	*object = idr_find(&ctxdata->qtee_objects_idr, id);
	if (!qcomtee_object_get(*object))
		err = -EINVAL;

	return err;
}

static void del_qtee_object(unsigned long id,
			    struct qcomtee_context_data *ctxdata)
{
	struct qcomtee_object *object;

	scoped_guard(mutex, &ctxdata->qtee_lock)
		object = idr_remove(&ctxdata->qtee_objects_idr, id);

	qcomtee_object_put(object);
}

/**
 * qcomtee_context_add_qtee_object() - Add a QTEE object to the context.
 * @param: TEE parameter representing @object.
 * @object: QTEE object.
 * @ctx: context to add the object.
 *
 * It assumes @object is %QCOMTEE_OBJECT_TYPE_TEE and the caller has already
 * issued qcomtee_object_get() for @object.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
int qcomtee_context_add_qtee_object(struct tee_param *param,
				    struct qcomtee_object *object,
				    struct tee_context *ctx)
{
	int ret;
	struct qcomtee_context_data *ctxdata = ctx->data;

	scoped_guard(mutex, &ctxdata->qtee_lock)
		ret = idr_alloc(&ctxdata->qtee_objects_idr, object, 0, 0,
				GFP_KERNEL);
	if (ret < 0)
		return ret;

	param->u.objref.id = ret;
	/* QTEE Object: QCOMTEE_OBJREF_FLAG_TEE set. */
	param->u.objref.flags = QCOMTEE_OBJREF_FLAG_TEE;

	return 0;
}

/* Retrieve the QTEE object added with qcomtee_context_add_qtee_object(). */
int qcomtee_context_find_qtee_object(struct qcomtee_object **object,
				     struct tee_param *param,
				     struct tee_context *ctx)
{
	struct qcomtee_context_data *ctxdata = ctx->data;

	return find_qtee_object(object, param->u.objref.id, ctxdata);
}

/**
 * qcomtee_context_del_qtee_object() - Delete a QTEE object from the context.
 * @param: TEE parameter representing @object.
 * @ctx: context for deleting the object.
 *
 * The @param has been initialized by qcomtee_context_add_qtee_object().
 */
void qcomtee_context_del_qtee_object(struct tee_param *param,
				     struct tee_context *ctx)
{
	struct qcomtee_context_data *ctxdata = ctx->data;
	/* 'qtee_objects_idr' stores QTEE objects only. */
	if (param->u.objref.flags & QCOMTEE_OBJREF_FLAG_TEE)
		del_qtee_object(param->u.objref.id, ctxdata);
}

/**
 * qcomtee_objref_to_arg() - Convert OBJREF parameter to QTEE argument.
 * @arg: QTEE argument.
 * @param: TEE parameter.
 * @ctx: context in which the conversion should happen.
 *
 * It assumes @param is an OBJREF.
 * It does not set @arg.type; the caller should initialize it to a correct
 * &enum qcomtee_arg_type value. It gets the object's refcount in @arg;
 * the caller should manage to put it afterward.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
int qcomtee_objref_to_arg(struct qcomtee_arg *arg, struct tee_param *param,
			  struct tee_context *ctx)
{
	int err = -EINVAL;

	arg->o = NULL_QCOMTEE_OBJECT;
	/* param is a NULL object: */
	if (param->u.objref.id == TEE_OBJREF_NULL)
		return 0;

	/* param is a callback object: */
	if (param->u.objref.flags & QCOMTEE_OBJREF_FLAG_USER)
		err =  qcomtee_user_param_to_object(&arg->o, param, ctx);
	/* param is a QTEE object: */
	else if (param->u.objref.flags & QCOMTEE_OBJREF_FLAG_TEE)
		err = qcomtee_context_find_qtee_object(&arg->o, param, ctx);
	/* param is a memory object: */
	else if (param->u.objref.flags & QCOMTEE_OBJREF_FLAG_MEM)
		err = qcomtee_memobj_param_to_object(&arg->o, param, ctx);

	/*
	 * For callback objects, call qcomtee_object_get() to keep a temporary
	 * copy for the driver, as these objects are released asynchronously
	 * and may disappear even before returning from QTEE.
	 *
	 *  - For direct object invocations, the matching put is called in
	 *    qcomtee_object_invoke() when parsing the QTEE response.
	 *  - For callback responses, put is called in qcomtee_user_object_notify()
	 *    after QTEE has received its copies.
	 */

	if (!err && (typeof_qcomtee_object(arg->o) == QCOMTEE_OBJECT_TYPE_CB))
		qcomtee_object_get(arg->o);

	return err;
}

/**
 * qcomtee_objref_from_arg() - Convert QTEE argument to OBJREF param.
 * @param: TEE parameter.
 * @arg: QTEE argument.
 * @ctx: context in which the conversion should happen.
 *
 * It assumes @arg is of %QCOMTEE_ARG_TYPE_IO or %QCOMTEE_ARG_TYPE_OO.
 * It does not set @param.attr; the caller should initialize it to a
 * correct type.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
int qcomtee_objref_from_arg(struct tee_param *param, struct qcomtee_arg *arg,
			    struct tee_context *ctx)
{
	struct qcomtee_object *object = arg->o;

	switch (typeof_qcomtee_object(object)) {
	case QCOMTEE_OBJECT_TYPE_NULL:
		param->u.objref.id = TEE_OBJREF_NULL;

		return 0;
	case QCOMTEE_OBJECT_TYPE_CB:
		/* object is a callback object: */
		if (is_qcomtee_user_object(object))
			return qcomtee_user_param_from_object(param, object,
							      ctx);
		/* object is a memory object: */
		else if (is_qcomtee_memobj_object(object))
			return qcomtee_memobj_param_from_object(param, object,
							       ctx);

		break;
	case QCOMTEE_OBJECT_TYPE_TEE:
		return qcomtee_context_add_qtee_object(param, object, ctx);

	case QCOMTEE_OBJECT_TYPE_ROOT:
	default:
		break;
	}

	return -EINVAL;
}

/**
 * qcomtee_params_to_args() - Convert TEE parameters to QTEE arguments.
 * @u: QTEE arguments.
 * @params: TEE parameters.
 * @num_params: number of elements in the parameter array.
 * @ctx: context in which the conversion should happen.
 *
 * It assumes @u has at least @num_params + 1 entries and has been initialized
 * with %QCOMTEE_ARG_TYPE_INV as &struct qcomtee_arg.type.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
static int qcomtee_params_to_args(struct qcomtee_arg *u,
				  struct tee_param *params, int num_params,
				  struct tee_context *ctx)
{
	int i;

	for (i = 0; i < num_params; i++) {
		switch (params[i].attr) {
		case TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_INPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_OUTPUT:
			u[i].flags = QCOMTEE_ARG_FLAGS_UADDR;
			u[i].b.uaddr = params[i].u.ubuf.uaddr;
			u[i].b.size = params[i].u.ubuf.size;

			if (params[i].attr ==
			    TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_INPUT)
				u[i].type = QCOMTEE_ARG_TYPE_IB;
			else /* TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_OUTPUT */
				u[i].type = QCOMTEE_ARG_TYPE_OB;

			break;
		case TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_INPUT:
			u[i].type = QCOMTEE_ARG_TYPE_IO;
			if (qcomtee_objref_to_arg(&u[i], &params[i], ctx))
				goto out_failed;

			break;
		case TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_OUTPUT:
			u[i].type = QCOMTEE_ARG_TYPE_OO;
			u[i].o = NULL_QCOMTEE_OBJECT;
			break;
		default:
			goto out_failed;
		}
	}

	return 0;

out_failed:
	/* Undo qcomtee_objref_to_arg(). */
	for (i--; i >= 0; i--) {
		if (u[i].type != QCOMTEE_ARG_TYPE_IO)
			continue;

		qcomtee_user_object_set_notify(u[i].o, false);
		/* See docs for qcomtee_objref_to_arg() for double put. */
		if (typeof_qcomtee_object(u[i].o) == QCOMTEE_OBJECT_TYPE_CB)
			qcomtee_object_put(u[i].o);

		qcomtee_object_put(u[i].o);
	}

	return -EINVAL;
}

/**
 * qcomtee_params_from_args() - Convert QTEE arguments to TEE parameters.
 * @params: TEE parameters.
 * @u: QTEE arguments.
 * @num_params: number of elements in the parameter array.
 * @ctx: context in which the conversion should happen.
 *
 * @u should have already been initialized by qcomtee_params_to_args().
 * This also represents the end of a QTEE invocation that started with
 * qcomtee_params_to_args() by releasing %QCOMTEE_ARG_TYPE_IO objects.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
static int qcomtee_params_from_args(struct tee_param *params,
				    struct qcomtee_arg *u, int num_params,
				    struct tee_context *ctx)
{
	int i, np;

	qcomtee_arg_for_each(np, u) {
		switch (u[np].type) {
		case QCOMTEE_ARG_TYPE_OB:
			/* TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_OUTPUT */
			params[np].u.ubuf.size = u[np].b.size;

			break;
		case QCOMTEE_ARG_TYPE_IO:
			/* IEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_INPUT */
			qcomtee_object_put(u[np].o);

			break;
		case QCOMTEE_ARG_TYPE_OO:
			/* TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_OUTPUT */
			if (qcomtee_objref_from_arg(&params[np], &u[np], ctx))
				goto out_failed;

			break;
		case QCOMTEE_ARG_TYPE_IB:
		default:
			break;
		}
	}

	return 0;

out_failed:
	/* Undo qcomtee_objref_from_arg(). */
	for (i = 0; i < np; i++) {
		if (params[i].attr == TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_OUTPUT)
			qcomtee_context_del_qtee_object(&params[i], ctx);
	}

	/* Release any IO and OO objects not processed. */
	for (; u[i].type && i < num_params; i++) {
		if (u[i].type == QCOMTEE_ARG_TYPE_OO ||
		    u[i].type == QCOMTEE_ARG_TYPE_IO)
			qcomtee_object_put(u[i].o);
	}

	return -EINVAL;
}

/* TEE Device Ops. */

static int qcomtee_params_check(struct tee_param *params, int num_params)
{
	int io = 0, oo = 0, ib = 0, ob = 0;
	int i;

	/* QTEE can accept 64 arguments. */
	if (num_params > QCOMTEE_ARGS_MAX)
		return -EINVAL;

	/* Supported parameter types. */
	for (i = 0; i < num_params; i++) {
		switch (params[i].attr) {
		case TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_INPUT:
			ib++;
			break;
		case TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_OUTPUT:
			ob++;
			break;
		case TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_INPUT:
			io++;
			break;
		case TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_OUTPUT:
			oo++;
			break;
		default:
			return -EINVAL;
		}
	}

	/*  QTEE can accept 16 arguments of each supported types. */
	if (io > QCOMTEE_ARGS_PER_TYPE || oo > QCOMTEE_ARGS_PER_TYPE ||
	    ib > QCOMTEE_ARGS_PER_TYPE || ob > QCOMTEE_ARGS_PER_TYPE)
		return -EINVAL;

	return 0;
}

/* Check if an operation on ROOT_QCOMTEE_OBJECT from userspace is permitted. */
static int qcomtee_root_object_check(u32 op, struct tee_param *params,
				     int num_params)
{
	/* Some privileged operations recognized by QTEE. */
	if (op == QCOMTEE_ROOT_OP_NOTIFY_DOMAIN_CHANGE ||
	    op == QCOMTEE_ROOT_OP_ADCI_ACCEPT ||
	    op == QCOMTEE_ROOT_OP_ADCI_SHUTDOWN)
		return -EINVAL;

	/*
	 * QCOMTEE_ROOT_OP_REG_WITH_CREDENTIALS is to register with QTEE
	 * by passing a credential object as input OBJREF. TEE_OBJREF_NULL as a
	 * credential object represents a privileged client for QTEE and
	 * is used by the kernel only.
	 */
	if (op == QCOMTEE_ROOT_OP_REG_WITH_CREDENTIALS && num_params == 2) {
		if (params[0].attr == TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_INPUT &&
		    params[1].attr == TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_OUTPUT) {
			if (params[0].u.objref.id == TEE_OBJREF_NULL)
				return -EINVAL;
		}
	}

	return 0;
}

/**
 * qcomtee_object_invoke() - Invoke a QTEE object.
 * @ctx: TEE context.
 * @arg: ioctl arguments.
 * @params: parameters for the object.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
static int qcomtee_object_invoke(struct tee_context *ctx,
				 struct tee_ioctl_object_invoke_arg *arg,
				 struct tee_param *params)
{
	struct qcomtee_object_invoke_ctx *oic __free(kfree) = NULL;
	struct qcomtee_context_data *ctxdata = ctx->data;
	struct qcomtee_arg *u __free(kfree) = NULL;
	struct qcomtee_object *object;
	int i, ret, result;

	if (qcomtee_params_check(params, arg->num_params))
		return -EINVAL;

	/* First, handle reserved operations: */
	if (arg->op == QCOMTEE_MSG_OBJECT_OP_RELEASE) {
		del_qtee_object(arg->id, ctxdata);

		return 0;
	}

	/* Otherwise, invoke a QTEE object: */
	oic = qcomtee_object_invoke_ctx_alloc(ctx);
	if (!oic)
		return -ENOMEM;

	/* +1 for ending QCOMTEE_ARG_TYPE_INV. */
	u = kcalloc(arg->num_params + 1, sizeof(*u), GFP_KERNEL);
	if (!u)
		return -ENOMEM;

	/* Get an object to invoke. */
	if (arg->id == TEE_OBJREF_NULL) {
		/* Use ROOT if TEE_OBJREF_NULL is invoked. */
		if (qcomtee_root_object_check(arg->op, params, arg->num_params))
			return -EINVAL;

		object = ROOT_QCOMTEE_OBJECT;
	} else if (find_qtee_object(&object, arg->id, ctxdata)) {
		return -EINVAL;
	}

	ret = qcomtee_params_to_args(u, params, arg->num_params, ctx);
	if (ret)
		goto out;

	ret = qcomtee_object_do_invoke(oic, object, arg->op, u, &result);
	if (ret) {
		qcomtee_arg_for_each_input_object(i, u) {
			qcomtee_user_object_set_notify(u[i].o, false);
			qcomtee_object_put(u[i].o);
		}

		goto out;
	}

	/* Prase QTEE response and put driver's object copies: */

	if (!result) {
		/* Assume service is UNAVAIL if unable to process the result. */
		if (qcomtee_params_from_args(params, u, arg->num_params, ctx))
			result = QCOMTEE_MSG_ERROR_UNAVAIL;
	} else {
		/*
		 * qcomtee_params_to_args() gets a copy of IO for the driver to
		 * make sure they do not get released while in the middle of
		 * invocation. On success (!result), qcomtee_params_from_args()
		 * puts them; Otherwise, put them here.
		 */
		qcomtee_arg_for_each_input_object(i, u)
			qcomtee_object_put(u[i].o);
	}

	arg->ret = result;
out:
	qcomtee_object_put(object);

	return ret;
}

/**
 * qcomtee_supp_recv() - Wait for a request for the supplicant.
 * @ctx: TEE context.
 * @op: requested operation on the object.
 * @num_params: number of elements in the parameter array.
 * @params: parameters for @op.
 *
 * The first parameter is a meta %TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT.
 * On input, it provides a user buffer. This buffer is used for parameters of
 * type %TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_INPUT in qcomtee_cb_params_from_args().
 * On output, the object ID and request ID are stored in the meta parameter.
 *
 * @num_params is updated to the number of parameters that actually exist
 * in @params on return.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
static int qcomtee_supp_recv(struct tee_context *ctx, u32 *op, u32 *num_params,
			     struct tee_param *params)
{
	struct qcomtee_user_object_request_data data;
	void __user *uaddr;
	size_t ubuf_size;
	int i, ret;

	if (!*num_params)
		return -EINVAL;

	/* First parameter should be an INOUT + meta parameter. */
	if (params->attr !=
	    (TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT | TEE_IOCTL_PARAM_ATTR_META))
		return -EINVAL;

	/* Other parameters are none. */
	for (i = 1; i < *num_params; i++)
		if (params[i].attr)
			return -EINVAL;

	if (!IS_ALIGNED(params->u.value.a, 8))
		return -EINVAL;

	/* User buffer and size from meta parameter. */
	uaddr = u64_to_user_ptr(params->u.value.a);
	ubuf_size = params->u.value.b;
	/* Process TEE parameters. +/-1 to ignore the meta parameter. */
	ret = qcomtee_user_object_select(ctx, params + 1, *num_params - 1,
					 uaddr, ubuf_size, &data);
	if (ret)
		return ret;

	params->u.value.a = data.object_id;
	params->u.value.b = data.id;
	params->u.value.c = 0;
	*op = data.op;
	*num_params = data.np + 1;

	return 0;
}

/**
 * qcomtee_supp_send() - Submit a response for a request.
 * @ctx: TEE context.
 * @errno: return value for the request.
 * @num_params: number of elements in the parameter array.
 * @params: returned parameters.
 *
 * The first parameter is a meta %TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT.
 * It specifies the request ID this response belongs to.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
static int qcomtee_supp_send(struct tee_context *ctx, u32 errno, u32 num_params,
			     struct tee_param *params)
{
	int req_id;

	if (!num_params)
		return -EINVAL;

	/* First parameter should be an OUTPUT + meta parameter. */
	if (params->attr != (TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT |
			     TEE_IOCTL_PARAM_ATTR_META))
		return -EINVAL;

	req_id = params->u.value.a;
	/* Process TEE parameters. +/-1 to ignore the meta parameter. */
	return qcomtee_user_object_submit(ctx, params + 1, num_params - 1,
					  req_id, errno);
}

static int qcomtee_open(struct tee_context *ctx)
{
	struct qcomtee_context_data *ctxdata __free(kfree) = NULL;

	ctxdata = kzalloc(sizeof(*ctxdata), GFP_KERNEL);
	if (!ctxdata)
		return -ENOMEM;

	/*
	 * In the QTEE driver, the same context is used to refcount resources
	 * shared by QTEE. For example, teedev_ctx_get() is called for any
	 * instance of callback objects (see qcomtee_user_param_to_object()).
	 *
	 * Maintain a copy of teedev for QTEE as it serves as a direct user of
	 * this context. The teedev will be released in the context's release().
	 *
	 * tee_device_unregister() will remain blocked until all contexts
	 * are released. This includes contexts owned by the user, which are
	 * closed by teedev_close_context(), as well as those owned by QTEE
	 * closed by teedev_ctx_put() in object's release().
	 */
	if (!tee_device_get(ctx->teedev))
		return -EINVAL;

	idr_init(&ctxdata->qtee_objects_idr);
	mutex_init(&ctxdata->qtee_lock);
	idr_init(&ctxdata->reqs_idr);
	INIT_LIST_HEAD(&ctxdata->reqs_list);
	mutex_init(&ctxdata->reqs_lock);
	init_completion(&ctxdata->req_c);

	ctx->data = no_free_ptr(ctxdata);

	return 0;
}

/* Gets called when the user closes the device */
static void qcomtee_close_context(struct tee_context *ctx)
{
	struct qcomtee_context_data *ctxdata = ctx->data;
	struct qcomtee_object *object;
	int id;

	/* Process QUEUED or PROCESSING requests. */
	qcomtee_requests_destroy(ctxdata);
	/* Release QTEE objects. */
	idr_for_each_entry(&ctxdata->qtee_objects_idr, object, id)
		qcomtee_object_put(object);
}

/* Gets called when the final reference to the context goes away. */
static void qcomtee_release(struct tee_context *ctx)
{
	struct qcomtee_context_data *ctxdata = ctx->data;

	idr_destroy(&ctxdata->qtee_objects_idr);
	idr_destroy(&ctxdata->reqs_idr);
	kfree(ctxdata);

	/* There is nothing shared in this context with QTEE. */
	tee_device_put(ctx->teedev);
}

static void qcomtee_get_version(struct tee_device *teedev,
				struct tee_ioctl_version_data *vers)
{
	struct tee_ioctl_version_data v = {
		.impl_id = TEE_IMPL_ID_QTEE,
		.gen_caps = TEE_GEN_CAP_OBJREF,
	};

	*vers = v;
}

/**
 * qcomtee_get_qtee_feature_list() - Query QTEE features versions.
 * @ctx: TEE context.
 * @id: ID of the feature to query.
 * @version: version of the feature.
 *
 * Used to query the verion of features supported by QTEE.
 */
static void qcomtee_get_qtee_feature_list(struct tee_context *ctx, u32 id,
					  u32 *version)
{
	struct qcomtee_object_invoke_ctx *oic __free(kfree);
	struct qcomtee_object *client_env, *service;
	struct qcomtee_arg u[3] = { 0 };
	int result;

	oic = qcomtee_object_invoke_ctx_alloc(ctx);
	if (!oic)
		return;

	client_env = qcomtee_object_get_client_env(oic);
	if (client_env == NULL_QCOMTEE_OBJECT)
		return;

	/* Get ''FeatureVersions Service'' object. */
	service = qcomtee_object_get_service(oic, client_env,
					     QCOMTEE_FEATURE_VER_UID);
	if (service == NULL_QCOMTEE_OBJECT)
		goto out_failed;

	/* IB: Feature to query. */
	u[0].b.addr = &id;
	u[0].b.size = sizeof(id);
	u[0].type = QCOMTEE_ARG_TYPE_IB;

	/* OB: Version returned. */
	u[1].b.addr = version;
	u[1].b.size = sizeof(*version);
	u[1].type = QCOMTEE_ARG_TYPE_OB;

	qcomtee_object_do_invoke(oic, service, QCOMTEE_FEATURE_VER_OP_GET, u,
				 &result);

out_failed:
	qcomtee_object_put(service);
	qcomtee_object_put(client_env);
}

static const struct tee_driver_ops qcomtee_ops = {
	.get_version = qcomtee_get_version,
	.open = qcomtee_open,
	.close_context = qcomtee_close_context,
	.release = qcomtee_release,
	.object_invoke_func = qcomtee_object_invoke,
	.supp_recv = qcomtee_supp_recv,
	.supp_send = qcomtee_supp_send,
};

static const struct tee_desc qcomtee_desc = {
	.name = "qcomtee",
	.ops = &qcomtee_ops,
	.owner = THIS_MODULE,
};

static int qcomtee_probe(struct platform_device *pdev)
{
	struct workqueue_struct *async_wq;
	struct tee_device *teedev;
	struct tee_shm_pool *pool;
	struct tee_context *ctx;
	struct qcomtee *qcomtee;
	int err;

	qcomtee = kzalloc(sizeof(*qcomtee), GFP_KERNEL);
	if (!qcomtee)
		return -ENOMEM;

	pool = qcomtee_shm_pool_alloc();
	if (IS_ERR(pool)) {
		err = PTR_ERR(pool);

		goto err_free_qcomtee;
	}

	teedev = tee_device_alloc(&qcomtee_desc, NULL, pool, qcomtee);
	if (IS_ERR(teedev)) {
		err = PTR_ERR(teedev);

		goto err_pool_destroy;
	}

	qcomtee->teedev = teedev;
	qcomtee->pool = pool;
	err = tee_device_register(qcomtee->teedev);
	if (err)
		goto err_unreg_teedev;

	platform_set_drvdata(pdev, qcomtee);
	/* Start async wq. */
	async_wq = alloc_ordered_workqueue("qcomtee_wq", 0);
	if (!async_wq) {
		err = -ENOMEM;

		goto err_unreg_teedev;
	}

	qcomtee->wq = async_wq;
	/* Driver context used for async operations of teedev. */
	ctx = teedev_open(qcomtee->teedev);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);

		goto err_dest_wq;
	}

	qcomtee->ctx = ctx;
	/* Init Object table. */
	qcomtee->xa_last_id = 0;
	xa_init_flags(&qcomtee->xa_local_objects, XA_FLAGS_ALLOC);
	/* Get QTEE verion. */
	qcomtee_get_qtee_feature_list(qcomtee->ctx,
				      QCOMTEE_FEATURE_VER_OP_GET_QTEE_ID,
				      &qcomtee->qtee_version);

	pr_info("QTEE version %u.%u.%u\n",
		QTEE_VERSION_GET_MAJOR(qcomtee->qtee_version),
		QTEE_VERSION_GET_MINOR(qcomtee->qtee_version),
		QTEE_VERSION_GET_PATCH(qcomtee->qtee_version));

	return 0;

err_dest_wq:
	destroy_workqueue(qcomtee->wq);
err_unreg_teedev:
	tee_device_unregister(qcomtee->teedev);
err_pool_destroy:
	tee_shm_pool_free(pool);
err_free_qcomtee:
	kfree(qcomtee);

	return err;
}

/**
 * qcomtee_remove() - Device Removal Routine.
 * @pdev: platform device information struct.
 *
 * It is called by the platform subsystem to alert the driver that it should
 * release the device.
 *
 * QTEE does not provide an API to inform it about a callback object going away.
 * However, when releasing QTEE objects, any callback object sent to QTEE
 * previously would be released by QTEE as part of the object release.
 */
static void qcomtee_remove(struct platform_device *pdev)
{
	struct qcomtee *qcomtee = platform_get_drvdata(pdev);

	teedev_close_context(qcomtee->ctx);
	/* Wait for RELEASE operations to be processed for QTEE objects. */
	tee_device_unregister(qcomtee->teedev);
	destroy_workqueue(qcomtee->wq);
	tee_shm_pool_free(qcomtee->pool);
	kfree(qcomtee);
}

static const struct platform_device_id qcomtee_ids[] = { { "qcomtee", 0 }, {} };
MODULE_DEVICE_TABLE(platform, qcomtee_ids);

static struct platform_driver qcomtee_platform_driver = {
	.probe = qcomtee_probe,
	.remove = qcomtee_remove,
	.driver = {
		.name = "qcomtee",
	},
	.id_table = qcomtee_ids,
};

module_platform_driver(qcomtee_platform_driver);

MODULE_AUTHOR("Qualcomm");
MODULE_DESCRIPTION("QTEE driver");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
