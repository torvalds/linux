// SPDX-License-Identifier: MIT
/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 */

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/tee_drv.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>
#include "amdtee_private.h"
#include "../tee_private.h"
#include <linux/psp-tee.h>

static struct amdtee_driver_data *drv_data;
static DEFINE_MUTEX(session_list_mutex);

static void amdtee_get_version(struct tee_device *teedev,
			       struct tee_ioctl_version_data *vers)
{
	struct tee_ioctl_version_data v = {
		.impl_id = TEE_IMPL_ID_AMDTEE,
		.impl_caps = 0,
		.gen_caps = TEE_GEN_CAP_GP,
	};
	*vers = v;
}

static int amdtee_open(struct tee_context *ctx)
{
	struct amdtee_context_data *ctxdata;

	ctxdata = kzalloc(sizeof(*ctxdata), GFP_KERNEL);
	if (!ctxdata)
		return -ENOMEM;

	INIT_LIST_HEAD(&ctxdata->sess_list);
	INIT_LIST_HEAD(&ctxdata->shm_list);
	mutex_init(&ctxdata->shm_mutex);

	ctx->data = ctxdata;
	return 0;
}

static void release_session(struct amdtee_session *sess)
{
	int i;

	/* Close any open session */
	for (i = 0; i < TEE_NUM_SESSIONS; ++i) {
		/* Check if session entry 'i' is valid */
		if (!test_bit(i, sess->sess_mask))
			continue;

		handle_close_session(sess->ta_handle, sess->session_info[i]);
		handle_unload_ta(sess->ta_handle);
	}

	kfree(sess);
}

static void amdtee_release(struct tee_context *ctx)
{
	struct amdtee_context_data *ctxdata = ctx->data;

	if (!ctxdata)
		return;

	while (true) {
		struct amdtee_session *sess;

		sess = list_first_entry_or_null(&ctxdata->sess_list,
						struct amdtee_session,
						list_node);

		if (!sess)
			break;

		list_del(&sess->list_node);
		release_session(sess);
	}
	mutex_destroy(&ctxdata->shm_mutex);
	kfree(ctxdata);

	ctx->data = NULL;
}

/**
 * alloc_session() - Allocate a session structure
 * @ctxdata:    TEE Context data structure
 * @session:    Session ID for which 'struct amdtee_session' structure is to be
 *              allocated.
 *
 * Scans the TEE context's session list to check if TA is already loaded in to
 * TEE. If yes, returns the 'session' structure for that TA. Else allocates,
 * initializes a new 'session' structure and adds it to context's session list.
 *
 * The caller must hold a mutex.
 *
 * Returns:
 * 'struct amdtee_session *' on success and NULL on failure.
 */
static struct amdtee_session *alloc_session(struct amdtee_context_data *ctxdata,
					    u32 session)
{
	struct amdtee_session *sess;
	u32 ta_handle = get_ta_handle(session);

	/* Scan session list to check if TA is already loaded in to TEE */
	list_for_each_entry(sess, &ctxdata->sess_list, list_node)
		if (sess->ta_handle == ta_handle) {
			kref_get(&sess->refcount);
			return sess;
		}

	/* Allocate a new session and add to list */
	sess = kzalloc(sizeof(*sess), GFP_KERNEL);
	if (sess) {
		sess->ta_handle = ta_handle;
		kref_init(&sess->refcount);
		spin_lock_init(&sess->lock);
		list_add(&sess->list_node, &ctxdata->sess_list);
	}

	return sess;
}

/* Requires mutex to be held */
static struct amdtee_session *find_session(struct amdtee_context_data *ctxdata,
					   u32 session)
{
	u32 ta_handle = get_ta_handle(session);
	u32 index = get_session_index(session);
	struct amdtee_session *sess;

	if (index >= TEE_NUM_SESSIONS)
		return NULL;

	list_for_each_entry(sess, &ctxdata->sess_list, list_node)
		if (ta_handle == sess->ta_handle &&
		    test_bit(index, sess->sess_mask))
			return sess;

	return NULL;
}

u32 get_buffer_id(struct tee_shm *shm)
{
	struct amdtee_context_data *ctxdata = shm->ctx->data;
	struct amdtee_shm_data *shmdata;
	u32 buf_id = 0;

	mutex_lock(&ctxdata->shm_mutex);
	list_for_each_entry(shmdata, &ctxdata->shm_list, shm_node)
		if (shmdata->kaddr == shm->kaddr) {
			buf_id = shmdata->buf_id;
			break;
		}
	mutex_unlock(&ctxdata->shm_mutex);

	return buf_id;
}

static DEFINE_MUTEX(drv_mutex);
static int copy_ta_binary(struct tee_context *ctx, void *ptr, void **ta,
			  size_t *ta_size)
{
	const struct firmware *fw;
	char fw_name[TA_PATH_MAX];
	struct {
		u32 lo;
		u16 mid;
		u16 hi_ver;
		u8 seq_n[8];
	} *uuid = ptr;
	int n, rc = 0;

	n = snprintf(fw_name, TA_PATH_MAX,
		     "%s/%08x-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x.bin",
		     TA_LOAD_PATH, uuid->lo, uuid->mid, uuid->hi_ver,
		     uuid->seq_n[0], uuid->seq_n[1],
		     uuid->seq_n[2], uuid->seq_n[3],
		     uuid->seq_n[4], uuid->seq_n[5],
		     uuid->seq_n[6], uuid->seq_n[7]);
	if (n < 0 || n >= TA_PATH_MAX) {
		pr_err("failed to get firmware name\n");
		return -EINVAL;
	}

	mutex_lock(&drv_mutex);
	n = request_firmware(&fw, fw_name, &ctx->teedev->dev);
	if (n) {
		pr_err("failed to load firmware %s\n", fw_name);
		rc = -ENOMEM;
		goto unlock;
	}

	*ta_size = roundup(fw->size, PAGE_SIZE);
	*ta = (void *)__get_free_pages(GFP_KERNEL, get_order(*ta_size));
	if (IS_ERR(*ta)) {
		pr_err("%s: get_free_pages failed 0x%llx\n", __func__,
		       (u64)*ta);
		rc = -ENOMEM;
		goto rel_fw;
	}

	memcpy(*ta, fw->data, fw->size);
rel_fw:
	release_firmware(fw);
unlock:
	mutex_unlock(&drv_mutex);
	return rc;
}

static void destroy_session(struct kref *ref)
{
	struct amdtee_session *sess = container_of(ref, struct amdtee_session,
						   refcount);

	mutex_lock(&session_list_mutex);
	list_del(&sess->list_node);
	mutex_unlock(&session_list_mutex);
	kfree(sess);
}

int amdtee_open_session(struct tee_context *ctx,
			struct tee_ioctl_open_session_arg *arg,
			struct tee_param *param)
{
	struct amdtee_context_data *ctxdata = ctx->data;
	struct amdtee_session *sess = NULL;
	u32 session_info, ta_handle;
	size_t ta_size;
	int rc, i;
	void *ta;

	if (arg->clnt_login != TEE_IOCTL_LOGIN_PUBLIC) {
		pr_err("unsupported client login method\n");
		return -EINVAL;
	}

	rc = copy_ta_binary(ctx, &arg->uuid[0], &ta, &ta_size);
	if (rc) {
		pr_err("failed to copy TA binary\n");
		return rc;
	}

	/* Load the TA binary into TEE environment */
	handle_load_ta(ta, ta_size, arg);
	if (arg->ret != TEEC_SUCCESS)
		goto out;

	ta_handle = get_ta_handle(arg->session);

	mutex_lock(&session_list_mutex);
	sess = alloc_session(ctxdata, arg->session);
	mutex_unlock(&session_list_mutex);

	if (!sess) {
		handle_unload_ta(ta_handle);
		rc = -ENOMEM;
		goto out;
	}

	/* Find an empty session index for the given TA */
	spin_lock(&sess->lock);
	i = find_first_zero_bit(sess->sess_mask, TEE_NUM_SESSIONS);
	if (i < TEE_NUM_SESSIONS)
		set_bit(i, sess->sess_mask);
	spin_unlock(&sess->lock);

	if (i >= TEE_NUM_SESSIONS) {
		pr_err("reached maximum session count %d\n", TEE_NUM_SESSIONS);
		handle_unload_ta(ta_handle);
		kref_put(&sess->refcount, destroy_session);
		rc = -ENOMEM;
		goto out;
	}

	/* Open session with loaded TA */
	handle_open_session(arg, &session_info, param);
	if (arg->ret != TEEC_SUCCESS) {
		pr_err("open_session failed %d\n", arg->ret);
		spin_lock(&sess->lock);
		clear_bit(i, sess->sess_mask);
		spin_unlock(&sess->lock);
		handle_unload_ta(ta_handle);
		kref_put(&sess->refcount, destroy_session);
		goto out;
	}

	sess->session_info[i] = session_info;
	set_session_id(ta_handle, i, &arg->session);
out:
	free_pages((u64)ta, get_order(ta_size));
	return rc;
}

int amdtee_close_session(struct tee_context *ctx, u32 session)
{
	struct amdtee_context_data *ctxdata = ctx->data;
	u32 i, ta_handle, session_info;
	struct amdtee_session *sess;

	pr_debug("%s: sid = 0x%x\n", __func__, session);

	/*
	 * Check that the session is valid and clear the session
	 * usage bit
	 */
	mutex_lock(&session_list_mutex);
	sess = find_session(ctxdata, session);
	if (sess) {
		ta_handle = get_ta_handle(session);
		i = get_session_index(session);
		session_info = sess->session_info[i];
		spin_lock(&sess->lock);
		clear_bit(i, sess->sess_mask);
		spin_unlock(&sess->lock);
	}
	mutex_unlock(&session_list_mutex);

	if (!sess)
		return -EINVAL;

	/* Close the session */
	handle_close_session(ta_handle, session_info);
	handle_unload_ta(ta_handle);

	kref_put(&sess->refcount, destroy_session);

	return 0;
}

int amdtee_map_shmem(struct tee_shm *shm)
{
	struct amdtee_context_data *ctxdata;
	struct amdtee_shm_data *shmnode;
	struct shmem_desc shmem;
	int rc, count;
	u32 buf_id;

	if (!shm)
		return -EINVAL;

	shmnode = kmalloc(sizeof(*shmnode), GFP_KERNEL);
	if (!shmnode)
		return -ENOMEM;

	count = 1;
	shmem.kaddr = shm->kaddr;
	shmem.size = shm->size;

	/*
	 * Send a MAP command to TEE and get the corresponding
	 * buffer Id
	 */
	rc = handle_map_shmem(count, &shmem, &buf_id);
	if (rc) {
		pr_err("map_shmem failed: ret = %d\n", rc);
		kfree(shmnode);
		return rc;
	}

	shmnode->kaddr = shm->kaddr;
	shmnode->buf_id = buf_id;
	ctxdata = shm->ctx->data;
	mutex_lock(&ctxdata->shm_mutex);
	list_add(&shmnode->shm_node, &ctxdata->shm_list);
	mutex_unlock(&ctxdata->shm_mutex);

	pr_debug("buf_id :[%x] kaddr[%p]\n", shmnode->buf_id, shmnode->kaddr);

	return 0;
}

void amdtee_unmap_shmem(struct tee_shm *shm)
{
	struct amdtee_context_data *ctxdata;
	struct amdtee_shm_data *shmnode;
	u32 buf_id;

	if (!shm)
		return;

	buf_id = get_buffer_id(shm);
	/* Unmap the shared memory from TEE */
	handle_unmap_shmem(buf_id);

	ctxdata = shm->ctx->data;
	mutex_lock(&ctxdata->shm_mutex);
	list_for_each_entry(shmnode, &ctxdata->shm_list, shm_node)
		if (buf_id == shmnode->buf_id) {
			list_del(&shmnode->shm_node);
			kfree(shmnode);
			break;
		}
	mutex_unlock(&ctxdata->shm_mutex);
}

int amdtee_invoke_func(struct tee_context *ctx,
		       struct tee_ioctl_invoke_arg *arg,
		       struct tee_param *param)
{
	struct amdtee_context_data *ctxdata = ctx->data;
	struct amdtee_session *sess;
	u32 i, session_info;

	/* Check that the session is valid */
	mutex_lock(&session_list_mutex);
	sess = find_session(ctxdata, arg->session);
	if (sess) {
		i = get_session_index(arg->session);
		session_info = sess->session_info[i];
	}
	mutex_unlock(&session_list_mutex);

	if (!sess)
		return -EINVAL;

	handle_invoke_cmd(arg, session_info, param);

	return 0;
}

int amdtee_cancel_req(struct tee_context *ctx, u32 cancel_id, u32 session)
{
	return -EINVAL;
}

static const struct tee_driver_ops amdtee_ops = {
	.get_version = amdtee_get_version,
	.open = amdtee_open,
	.release = amdtee_release,
	.open_session = amdtee_open_session,
	.close_session = amdtee_close_session,
	.invoke_func = amdtee_invoke_func,
	.cancel_req = amdtee_cancel_req,
};

static const struct tee_desc amdtee_desc = {
	.name = DRIVER_NAME "-clnt",
	.ops = &amdtee_ops,
	.owner = THIS_MODULE,
};

static int __init amdtee_driver_init(void)
{
	struct tee_device *teedev;
	struct tee_shm_pool *pool;
	struct amdtee *amdtee;
	int rc;

	rc = psp_check_tee_status();
	if (rc) {
		pr_err("amd-tee driver: tee not present\n");
		return rc;
	}

	drv_data = kzalloc(sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	amdtee = kzalloc(sizeof(*amdtee), GFP_KERNEL);
	if (!amdtee) {
		rc = -ENOMEM;
		goto err_kfree_drv_data;
	}

	pool = amdtee_config_shm();
	if (IS_ERR(pool)) {
		pr_err("shared pool configuration error\n");
		rc = PTR_ERR(pool);
		goto err_kfree_amdtee;
	}

	teedev = tee_device_alloc(&amdtee_desc, NULL, pool, amdtee);
	if (IS_ERR(teedev)) {
		rc = PTR_ERR(teedev);
		goto err_free_pool;
	}
	amdtee->teedev = teedev;

	rc = tee_device_register(amdtee->teedev);
	if (rc)
		goto err_device_unregister;

	amdtee->pool = pool;

	drv_data->amdtee = amdtee;

	pr_info("amd-tee driver initialization successful\n");
	return 0;

err_device_unregister:
	tee_device_unregister(amdtee->teedev);

err_free_pool:
	tee_shm_pool_free(pool);

err_kfree_amdtee:
	kfree(amdtee);

err_kfree_drv_data:
	kfree(drv_data);
	drv_data = NULL;

	pr_err("amd-tee driver initialization failed\n");
	return rc;
}
module_init(amdtee_driver_init);

static void __exit amdtee_driver_exit(void)
{
	struct amdtee *amdtee;

	if (!drv_data || !drv_data->amdtee)
		return;

	amdtee = drv_data->amdtee;

	tee_device_unregister(amdtee->teedev);
	tee_shm_pool_free(amdtee->pool);
}
module_exit(amdtee_driver_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION("AMD-TEE driver");
MODULE_VERSION("1.0");
MODULE_LICENSE("Dual MIT/GPL");
