// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Arm Limited
 */

#include <linux/arm_ffa.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/tee_core.h>
#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/xarray.h>
#include "tstee_private.h"

#define FFA_DIRECT_REQ_ARG_NUM 5
#define FFA_INVALID_MEM_HANDLE U64_MAX

static void arg_list_to_ffa_data(const u32 *args,
				 struct ffa_send_direct_data *data)
{
	data->data0 = args[0];
	data->data1 = args[1];
	data->data2 = args[2];
	data->data3 = args[3];
	data->data4 = args[4];
}

static void arg_list_from_ffa_data(const struct ffa_send_direct_data *data,
				   u32 *args)
{
	args[0] = lower_32_bits(data->data0);
	args[1] = lower_32_bits(data->data1);
	args[2] = lower_32_bits(data->data2);
	args[3] = lower_32_bits(data->data3);
	args[4] = lower_32_bits(data->data4);
}

static void tstee_get_version(struct tee_device *teedev,
			      struct tee_ioctl_version_data *vers)
{
	struct tstee *tstee = tee_get_drvdata(teedev);
	struct tee_ioctl_version_data v = {
		.impl_id = TEE_IMPL_ID_TSTEE,
		/* FF-A endpoint ID only uses the lower 16 bits */
		.impl_caps = lower_16_bits(tstee->ffa_dev->vm_id),
		.gen_caps = 0,
	};

	*vers = v;
}

static int tstee_open(struct tee_context *ctx)
{
	struct ts_context_data *ctxdata;

	ctxdata = kzalloc(sizeof(*ctxdata), GFP_KERNEL);
	if (!ctxdata)
		return -ENOMEM;

	xa_init_flags(&ctxdata->sess_list, XA_FLAGS_ALLOC);

	ctx->data = ctxdata;

	return 0;
}

static void tstee_release(struct tee_context *ctx)
{
	struct ts_context_data *ctxdata = ctx->data;
	struct ts_session *sess;
	unsigned long idx;

	if (!ctxdata)
		return;

	xa_for_each(&ctxdata->sess_list, idx, sess) {
		xa_erase(&ctxdata->sess_list, idx);
		kfree(sess);
	}

	xa_destroy(&ctxdata->sess_list);

	kfree(ctxdata);
	ctx->data = NULL;
}

static int tstee_open_session(struct tee_context *ctx,
			      struct tee_ioctl_open_session_arg *arg,
			      struct tee_param *param __always_unused)
{
	struct tstee *tstee = tee_get_drvdata(ctx->teedev);
	struct ffa_device *ffa_dev = tstee->ffa_dev;
	struct ts_context_data *ctxdata = ctx->data;
	struct ffa_send_direct_data ffa_data;
	struct ts_session *sess = NULL;
	u32 ffa_args[FFA_DIRECT_REQ_ARG_NUM] = {};
	u32 sess_id;
	int rc;

	ffa_args[TS_RPC_CTRL_REG] =
		TS_RPC_CTRL_PACK_IFACE_OPCODE(TS_RPC_MGMT_IFACE_ID,
					      TS_RPC_OP_SERVICE_INFO);

	memcpy(ffa_args + TS_RPC_SERVICE_INFO_UUID0, arg->uuid, UUID_SIZE);

	arg_list_to_ffa_data(ffa_args, &ffa_data);
	rc = ffa_dev->ops->msg_ops->sync_send_receive(ffa_dev, &ffa_data);
	if (rc)
		return rc;

	arg_list_from_ffa_data(&ffa_data, ffa_args);

	if (ffa_args[TS_RPC_SERVICE_INFO_RPC_STATUS] != TS_RPC_OK)
		return -ENODEV;

	if (ffa_args[TS_RPC_SERVICE_INFO_IFACE] > U8_MAX)
		return -EINVAL;

	sess = kzalloc(sizeof(*sess), GFP_KERNEL);
	if (!sess)
		return -ENOMEM;

	sess->iface_id = ffa_args[TS_RPC_SERVICE_INFO_IFACE];

	rc = xa_alloc(&ctxdata->sess_list, &sess_id, sess, xa_limit_32b,
		      GFP_KERNEL);
	if (rc) {
		kfree(sess);
		return rc;
	}

	arg->session = sess_id;
	arg->ret = 0;

	return 0;
}

static int tstee_close_session(struct tee_context *ctx, u32 session)
{
	struct ts_context_data *ctxdata = ctx->data;
	struct ts_session *sess;

	/* Calls xa_lock() internally */
	sess = xa_erase(&ctxdata->sess_list, session);
	if (!sess)
		return -EINVAL;

	kfree(sess);

	return 0;
}

static int tstee_invoke_func(struct tee_context *ctx,
			     struct tee_ioctl_invoke_arg *arg,
			     struct tee_param *param)
{
	struct tstee *tstee = tee_get_drvdata(ctx->teedev);
	struct ffa_device *ffa_dev = tstee->ffa_dev;
	struct ts_context_data *ctxdata = ctx->data;
	struct ffa_send_direct_data ffa_data;
	struct tee_shm *shm = NULL;
	struct ts_session *sess;
	u32 req_len, ffa_args[FFA_DIRECT_REQ_ARG_NUM] = {};
	int shm_id, rc;
	u8 iface_id;
	u64 handle;
	u16 opcode;

	xa_lock(&ctxdata->sess_list);
	sess = xa_load(&ctxdata->sess_list, arg->session);

	/*
	 * Do this while holding the lock to make sure that the session wasn't
	 * closed meanwhile
	 */
	if (sess)
		iface_id = sess->iface_id;

	xa_unlock(&ctxdata->sess_list);
	if (!sess)
		return -EINVAL;

	opcode = lower_16_bits(arg->func);
	shm_id = lower_32_bits(param[0].u.value.a);
	req_len = lower_32_bits(param[0].u.value.b);

	if (shm_id != 0) {
		shm = tee_shm_get_from_id(ctx, shm_id);
		if (IS_ERR(shm))
			return PTR_ERR(shm);

		if (shm->size < req_len) {
			dev_err(&ffa_dev->dev,
				"request doesn't fit into shared memory buffer\n");
			rc = -EINVAL;
			goto out;
		}

		handle = shm->sec_world_id;
	} else {
		handle = FFA_INVALID_MEM_HANDLE;
	}

	ffa_args[TS_RPC_CTRL_REG] = TS_RPC_CTRL_PACK_IFACE_OPCODE(iface_id,
								  opcode);
	ffa_args[TS_RPC_SERVICE_MEM_HANDLE_LSW] = lower_32_bits(handle);
	ffa_args[TS_RPC_SERVICE_MEM_HANDLE_MSW] = upper_32_bits(handle);
	ffa_args[TS_RPC_SERVICE_REQ_LEN] = req_len;
	ffa_args[TS_RPC_SERVICE_CLIENT_ID] = 0;

	arg_list_to_ffa_data(ffa_args, &ffa_data);
	rc = ffa_dev->ops->msg_ops->sync_send_receive(ffa_dev, &ffa_data);
	if (rc)
		goto out;

	arg_list_from_ffa_data(&ffa_data, ffa_args);

	if (ffa_args[TS_RPC_SERVICE_RPC_STATUS] != TS_RPC_OK) {
		dev_err(&ffa_dev->dev, "invoke_func rpc status: %d\n",
			ffa_args[TS_RPC_SERVICE_RPC_STATUS]);
		rc = -EINVAL;
		goto out;
	}

	arg->ret = ffa_args[TS_RPC_SERVICE_STATUS];
	if (shm && shm->size >= ffa_args[TS_RPC_SERVICE_RESP_LEN])
		param[0].u.value.a = ffa_args[TS_RPC_SERVICE_RESP_LEN];

out:
	if (shm)
		tee_shm_put(shm);

	return rc;
}

static int tstee_shm_register(struct tee_context *ctx, struct tee_shm *shm,
			      struct page **pages, size_t num_pages,
			      unsigned long start __always_unused)
{
	struct tstee *tstee = tee_get_drvdata(ctx->teedev);
	struct ffa_device *ffa_dev = tstee->ffa_dev;
	struct ffa_mem_region_attributes mem_attr = {
		.receiver = tstee->ffa_dev->vm_id,
		.attrs = FFA_MEM_RW,
		.flag = 0,
	};
	struct ffa_mem_ops_args mem_args = {
		.attrs = &mem_attr,
		.use_txbuf = true,
		.nattrs = 1,
		.flags = 0,
	};
	struct ffa_send_direct_data ffa_data;
	struct sg_table sgt;
	u32 ffa_args[FFA_DIRECT_REQ_ARG_NUM] = {};
	int rc;

	rc = sg_alloc_table_from_pages(&sgt, pages, num_pages, 0,
				       num_pages * PAGE_SIZE, GFP_KERNEL);
	if (rc)
		return rc;

	mem_args.sg = sgt.sgl;
	rc = ffa_dev->ops->mem_ops->memory_share(&mem_args);
	sg_free_table(&sgt);
	if (rc)
		return rc;

	shm->sec_world_id = mem_args.g_handle;

	ffa_args[TS_RPC_CTRL_REG] =
			TS_RPC_CTRL_PACK_IFACE_OPCODE(TS_RPC_MGMT_IFACE_ID,
						      TS_RPC_OP_RETRIEVE_MEM);
	ffa_args[TS_RPC_RETRIEVE_MEM_HANDLE_LSW] =
			lower_32_bits(shm->sec_world_id);
	ffa_args[TS_RPC_RETRIEVE_MEM_HANDLE_MSW] =
			upper_32_bits(shm->sec_world_id);
	ffa_args[TS_RPC_RETRIEVE_MEM_TAG_LSW] = 0;
	ffa_args[TS_RPC_RETRIEVE_MEM_TAG_MSW] = 0;

	arg_list_to_ffa_data(ffa_args, &ffa_data);
	rc = ffa_dev->ops->msg_ops->sync_send_receive(ffa_dev, &ffa_data);
	if (rc) {
		(void)ffa_dev->ops->mem_ops->memory_reclaim(shm->sec_world_id,
							    0);
		return rc;
	}

	arg_list_from_ffa_data(&ffa_data, ffa_args);

	if (ffa_args[TS_RPC_RETRIEVE_MEM_RPC_STATUS] != TS_RPC_OK) {
		dev_err(&ffa_dev->dev, "shm_register rpc status: %d\n",
			ffa_args[TS_RPC_RETRIEVE_MEM_RPC_STATUS]);
		ffa_dev->ops->mem_ops->memory_reclaim(shm->sec_world_id, 0);
		return -EINVAL;
	}

	return 0;
}

static int tstee_shm_unregister(struct tee_context *ctx, struct tee_shm *shm)
{
	struct tstee *tstee = tee_get_drvdata(ctx->teedev);
	struct ffa_device *ffa_dev = tstee->ffa_dev;
	struct ffa_send_direct_data ffa_data;
	u32 ffa_args[FFA_DIRECT_REQ_ARG_NUM] = {};
	int rc;

	ffa_args[TS_RPC_CTRL_REG] =
			TS_RPC_CTRL_PACK_IFACE_OPCODE(TS_RPC_MGMT_IFACE_ID,
						      TS_RPC_OP_RELINQ_MEM);
	ffa_args[TS_RPC_RELINQ_MEM_HANDLE_LSW] =
			lower_32_bits(shm->sec_world_id);
	ffa_args[TS_RPC_RELINQ_MEM_HANDLE_MSW] =
			upper_32_bits(shm->sec_world_id);

	arg_list_to_ffa_data(ffa_args, &ffa_data);
	rc = ffa_dev->ops->msg_ops->sync_send_receive(ffa_dev, &ffa_data);
	if (rc)
		return rc;
	arg_list_from_ffa_data(&ffa_data, ffa_args);

	if (ffa_args[TS_RPC_RELINQ_MEM_RPC_STATUS] != TS_RPC_OK) {
		dev_err(&ffa_dev->dev, "shm_unregister rpc status: %d\n",
			ffa_args[TS_RPC_RELINQ_MEM_RPC_STATUS]);
		return -EINVAL;
	}

	rc = ffa_dev->ops->mem_ops->memory_reclaim(shm->sec_world_id, 0);

	return rc;
}

static const struct tee_driver_ops tstee_ops = {
	.get_version = tstee_get_version,
	.open = tstee_open,
	.release = tstee_release,
	.open_session = tstee_open_session,
	.close_session = tstee_close_session,
	.invoke_func = tstee_invoke_func,
};

static const struct tee_desc tstee_desc = {
	.name = "tstee-clnt",
	.ops = &tstee_ops,
	.owner = THIS_MODULE,
};

static int pool_op_alloc(struct tee_shm_pool *pool, struct tee_shm *shm,
			 size_t size, size_t align)
{
	return tee_dyn_shm_alloc_helper(shm, size, align, tstee_shm_register);
}

static void pool_op_free(struct tee_shm_pool *pool, struct tee_shm *shm)
{
	tee_dyn_shm_free_helper(shm, tstee_shm_unregister);
}

static void pool_op_destroy_pool(struct tee_shm_pool *pool)
{
	kfree(pool);
}

static const struct tee_shm_pool_ops pool_ops = {
	.alloc = pool_op_alloc,
	.free = pool_op_free,
	.destroy_pool = pool_op_destroy_pool,
};

static struct tee_shm_pool *tstee_create_shm_pool(void)
{
	struct tee_shm_pool *pool = kzalloc(sizeof(*pool), GFP_KERNEL);

	if (!pool)
		return ERR_PTR(-ENOMEM);

	pool->ops = &pool_ops;

	return pool;
}

static bool tstee_check_rpc_compatible(struct ffa_device *ffa_dev)
{
	struct ffa_send_direct_data ffa_data;
	u32 ffa_args[FFA_DIRECT_REQ_ARG_NUM] = {};

	ffa_args[TS_RPC_CTRL_REG] =
			TS_RPC_CTRL_PACK_IFACE_OPCODE(TS_RPC_MGMT_IFACE_ID,
						      TS_RPC_OP_GET_VERSION);

	arg_list_to_ffa_data(ffa_args, &ffa_data);
	if (ffa_dev->ops->msg_ops->sync_send_receive(ffa_dev, &ffa_data))
		return false;

	arg_list_from_ffa_data(&ffa_data, ffa_args);

	return ffa_args[TS_RPC_GET_VERSION_RESP] == TS_RPC_PROTOCOL_VERSION;
}

static int tstee_probe(struct ffa_device *ffa_dev)
{
	struct tstee *tstee;
	int rc;

	ffa_dev->ops->msg_ops->mode_32bit_set(ffa_dev);

	if (!tstee_check_rpc_compatible(ffa_dev))
		return -EINVAL;

	tstee = kzalloc(sizeof(*tstee), GFP_KERNEL);
	if (!tstee)
		return -ENOMEM;

	tstee->ffa_dev = ffa_dev;

	tstee->pool = tstee_create_shm_pool();
	if (IS_ERR(tstee->pool)) {
		rc = PTR_ERR(tstee->pool);
		tstee->pool = NULL;
		goto err_free_tstee;
	}

	tstee->teedev = tee_device_alloc(&tstee_desc, NULL, tstee->pool, tstee);
	if (IS_ERR(tstee->teedev)) {
		rc = PTR_ERR(tstee->teedev);
		tstee->teedev = NULL;
		goto err_free_pool;
	}

	rc = tee_device_register(tstee->teedev);
	if (rc)
		goto err_unreg_teedev;

	ffa_dev_set_drvdata(ffa_dev, tstee);

	return 0;

err_unreg_teedev:
	tee_device_unregister(tstee->teedev);
err_free_pool:
	tee_shm_pool_free(tstee->pool);
err_free_tstee:
	kfree(tstee);
	return rc;
}

static void tstee_remove(struct ffa_device *ffa_dev)
{
	struct tstee *tstee = ffa_dev->dev.driver_data;

	tee_device_unregister(tstee->teedev);
	tee_shm_pool_free(tstee->pool);
	kfree(tstee);
}

static const struct ffa_device_id tstee_device_ids[] = {
	/* TS RPC protocol UUID: bdcd76d7-825e-4751-963b-86d4f84943ac */
	{ TS_RPC_UUID },
	{}
};

static struct ffa_driver tstee_driver = {
	.name = "arm_tstee",
	.probe = tstee_probe,
	.remove = tstee_remove,
	.id_table = tstee_device_ids,
};

module_ffa_driver(tstee_driver);

MODULE_AUTHOR("Balint Dobszay <balint.dobszay@arm.com>");
MODULE_DESCRIPTION("Arm Trusted Services TEE driver");
MODULE_LICENSE("GPL");
