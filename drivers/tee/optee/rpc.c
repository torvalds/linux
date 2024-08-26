// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2021, Linaro Limited
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/tee_core.h>
#include "optee_private.h"
#include "optee_rpc_cmd.h"

static void handle_rpc_func_cmd_get_time(struct optee_msg_arg *arg)
{
	struct timespec64 ts;

	if (arg->num_params != 1)
		goto bad;
	if ((arg->params[0].attr & OPTEE_MSG_ATTR_TYPE_MASK) !=
			OPTEE_MSG_ATTR_TYPE_VALUE_OUTPUT)
		goto bad;

	ktime_get_real_ts64(&ts);
	arg->params[0].u.value.a = ts.tv_sec;
	arg->params[0].u.value.b = ts.tv_nsec;

	arg->ret = TEEC_SUCCESS;
	return;
bad:
	arg->ret = TEEC_ERROR_BAD_PARAMETERS;
}

#if IS_REACHABLE(CONFIG_I2C)
static void handle_rpc_func_cmd_i2c_transfer(struct tee_context *ctx,
					     struct optee_msg_arg *arg)
{
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct tee_param *params;
	struct i2c_adapter *adapter;
	struct i2c_msg msg = { };
	size_t i;
	int ret = -EOPNOTSUPP;
	u8 attr[] = {
		TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT,
		TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT,
		TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT,
		TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT,
	};

	if (arg->num_params != ARRAY_SIZE(attr)) {
		arg->ret = TEEC_ERROR_BAD_PARAMETERS;
		return;
	}

	params = kmalloc_array(arg->num_params, sizeof(struct tee_param),
			       GFP_KERNEL);
	if (!params) {
		arg->ret = TEEC_ERROR_OUT_OF_MEMORY;
		return;
	}

	if (optee->ops->from_msg_param(optee, params, arg->num_params,
				       arg->params))
		goto bad;

	for (i = 0; i < arg->num_params; i++) {
		if (params[i].attr != attr[i])
			goto bad;
	}

	adapter = i2c_get_adapter(params[0].u.value.b);
	if (!adapter)
		goto bad;

	if (params[1].u.value.a & OPTEE_RPC_I2C_FLAGS_TEN_BIT) {
		if (!i2c_check_functionality(adapter,
					     I2C_FUNC_10BIT_ADDR)) {
			i2c_put_adapter(adapter);
			goto bad;
		}

		msg.flags = I2C_M_TEN;
	}

	msg.addr = params[0].u.value.c;
	msg.buf  = params[2].u.memref.shm->kaddr;
	msg.len  = params[2].u.memref.size;

	switch (params[0].u.value.a) {
	case OPTEE_RPC_I2C_TRANSFER_RD:
		msg.flags |= I2C_M_RD;
		break;
	case OPTEE_RPC_I2C_TRANSFER_WR:
		break;
	default:
		i2c_put_adapter(adapter);
		goto bad;
	}

	ret = i2c_transfer(adapter, &msg, 1);

	if (ret < 0) {
		arg->ret = TEEC_ERROR_COMMUNICATION;
	} else {
		params[3].u.value.a = msg.len;
		if (optee->ops->to_msg_param(optee, arg->params,
					     arg->num_params, params))
			arg->ret = TEEC_ERROR_BAD_PARAMETERS;
		else
			arg->ret = TEEC_SUCCESS;
	}

	i2c_put_adapter(adapter);
	kfree(params);
	return;
bad:
	kfree(params);
	arg->ret = TEEC_ERROR_BAD_PARAMETERS;
}
#else
static void handle_rpc_func_cmd_i2c_transfer(struct tee_context *ctx,
					     struct optee_msg_arg *arg)
{
	arg->ret = TEEC_ERROR_NOT_SUPPORTED;
}
#endif

static void handle_rpc_func_cmd_wq(struct optee *optee,
				   struct optee_msg_arg *arg)
{
	int rc = 0;

	if (arg->num_params != 1)
		goto bad;

	if ((arg->params[0].attr & OPTEE_MSG_ATTR_TYPE_MASK) !=
			OPTEE_MSG_ATTR_TYPE_VALUE_INPUT)
		goto bad;

	switch (arg->params[0].u.value.a) {
	case OPTEE_RPC_NOTIFICATION_WAIT:
		rc = optee_notif_wait(optee, arg->params[0].u.value.b, arg->params[0].u.value.c);
		if (rc)
			goto bad;
		break;
	case OPTEE_RPC_NOTIFICATION_SEND:
		if (optee_notif_send(optee, arg->params[0].u.value.b))
			goto bad;
		break;
	default:
		goto bad;
	}

	arg->ret = TEEC_SUCCESS;
	return;
bad:
	if (rc == -ETIMEDOUT)
		arg->ret = TEE_ERROR_TIMEOUT;
	else
		arg->ret = TEEC_ERROR_BAD_PARAMETERS;
}

static void handle_rpc_func_cmd_wait(struct optee_msg_arg *arg)
{
	u32 msec_to_wait;

	if (arg->num_params != 1)
		goto bad;

	if ((arg->params[0].attr & OPTEE_MSG_ATTR_TYPE_MASK) !=
			OPTEE_MSG_ATTR_TYPE_VALUE_INPUT)
		goto bad;

	msec_to_wait = arg->params[0].u.value.a;

	/* Go to interruptible sleep */
	msleep_interruptible(msec_to_wait);

	arg->ret = TEEC_SUCCESS;
	return;
bad:
	arg->ret = TEEC_ERROR_BAD_PARAMETERS;
}

static void handle_rpc_supp_cmd(struct tee_context *ctx, struct optee *optee,
				struct optee_msg_arg *arg)
{
	struct tee_param *params;

	arg->ret_origin = TEEC_ORIGIN_COMMS;

	params = kmalloc_array(arg->num_params, sizeof(struct tee_param),
			       GFP_KERNEL);
	if (!params) {
		arg->ret = TEEC_ERROR_OUT_OF_MEMORY;
		return;
	}

	if (optee->ops->from_msg_param(optee, params, arg->num_params,
				       arg->params)) {
		arg->ret = TEEC_ERROR_BAD_PARAMETERS;
		goto out;
	}

	arg->ret = optee_supp_thrd_req(ctx, arg->cmd, arg->num_params, params);

	if (optee->ops->to_msg_param(optee, arg->params, arg->num_params,
				     params))
		arg->ret = TEEC_ERROR_BAD_PARAMETERS;
out:
	kfree(params);
}

struct tee_shm *optee_rpc_cmd_alloc_suppl(struct tee_context *ctx, size_t sz)
{
	u32 ret;
	struct tee_param param;
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct tee_shm *shm;

	param.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
	param.u.value.a = OPTEE_RPC_SHM_TYPE_APPL;
	param.u.value.b = sz;
	param.u.value.c = 0;

	ret = optee_supp_thrd_req(ctx, OPTEE_RPC_CMD_SHM_ALLOC, 1, &param);
	if (ret)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&optee->supp.mutex);
	/* Increases count as secure world doesn't have a reference */
	shm = tee_shm_get_from_id(optee->supp.ctx, param.u.value.c);
	mutex_unlock(&optee->supp.mutex);
	return shm;
}

void optee_rpc_cmd_free_suppl(struct tee_context *ctx, struct tee_shm *shm)
{
	struct tee_param param;

	param.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
	param.u.value.a = OPTEE_RPC_SHM_TYPE_APPL;
	param.u.value.b = tee_shm_get_id(shm);
	param.u.value.c = 0;

	/*
	 * Match the tee_shm_get_from_id() in cmd_alloc_suppl() as secure
	 * world has released its reference.
	 *
	 * It's better to do this before sending the request to supplicant
	 * as we'd like to let the process doing the initial allocation to
	 * do release the last reference too in order to avoid stacking
	 * many pending fput() on the client process. This could otherwise
	 * happen if secure world does many allocate and free in a single
	 * invoke.
	 */
	tee_shm_put(shm);

	optee_supp_thrd_req(ctx, OPTEE_RPC_CMD_SHM_FREE, 1, &param);
}

void optee_rpc_cmd(struct tee_context *ctx, struct optee *optee,
		   struct optee_msg_arg *arg)
{
	switch (arg->cmd) {
	case OPTEE_RPC_CMD_GET_TIME:
		handle_rpc_func_cmd_get_time(arg);
		break;
	case OPTEE_RPC_CMD_NOTIFICATION:
		handle_rpc_func_cmd_wq(optee, arg);
		break;
	case OPTEE_RPC_CMD_SUSPEND:
		handle_rpc_func_cmd_wait(arg);
		break;
	case OPTEE_RPC_CMD_I2C_TRANSFER:
		handle_rpc_func_cmd_i2c_transfer(ctx, arg);
		break;
	default:
		handle_rpc_supp_cmd(ctx, optee, arg);
	}
}


