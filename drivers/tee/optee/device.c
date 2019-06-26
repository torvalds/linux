// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Linaro Ltd.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/tee_drv.h>
#include <linux/uuid.h>
#include "optee_private.h"

/*
 * Get device UUIDs
 *
 * [out]     memref[0]        Array of device UUIDs
 *
 * Return codes:
 * TEE_SUCCESS - Invoke command success
 * TEE_ERROR_BAD_PARAMETERS - Incorrect input param
 * TEE_ERROR_SHORT_BUFFER - Output buffer size less than required
 */
#define PTA_CMD_GET_DEVICES		0x0

static int optee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	if (ver->impl_id == TEE_IMPL_ID_OPTEE)
		return 1;
	else
		return 0;
}

static int get_devices(struct tee_context *ctx, u32 session,
		       struct tee_shm *device_shm, u32 *shm_size)
{
	int ret = 0;
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	/* Invoke PTA_CMD_GET_DEVICES function */
	inv_arg.func = PTA_CMD_GET_DEVICES;
	inv_arg.session = session;
	inv_arg.num_params = 4;

	/* Fill invoke cmd params */
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT;
	param[0].u.memref.shm = device_shm;
	param[0].u.memref.size = *shm_size;
	param[0].u.memref.shm_offs = 0;

	ret = tee_client_invoke_func(ctx, &inv_arg, param);
	if ((ret < 0) || ((inv_arg.ret != TEEC_SUCCESS) &&
			  (inv_arg.ret != TEEC_ERROR_SHORT_BUFFER))) {
		pr_err("PTA_CMD_GET_DEVICES invoke function err: %x\n",
		       inv_arg.ret);
		return -EINVAL;
	}

	*shm_size = param[0].u.memref.size;

	return 0;
}

static int optee_register_device(const uuid_t *device_uuid, u32 device_id)
{
	struct tee_client_device *optee_device = NULL;
	int rc;

	optee_device = kzalloc(sizeof(*optee_device), GFP_KERNEL);
	if (!optee_device)
		return -ENOMEM;

	optee_device->dev.bus = &tee_bus_type;
	dev_set_name(&optee_device->dev, "optee-clnt%u", device_id);
	uuid_copy(&optee_device->id.uuid, device_uuid);

	rc = device_register(&optee_device->dev);
	if (rc) {
		pr_err("device registration failed, err: %d\n", rc);
		kfree(optee_device);
	}

	return rc;
}

int optee_enumerate_devices(void)
{
	const uuid_t pta_uuid =
		UUID_INIT(0x7011a688, 0xddde, 0x4053,
			  0xa5, 0xa9, 0x7b, 0x3c, 0x4d, 0xdf, 0x13, 0xb8);
	struct tee_ioctl_open_session_arg sess_arg;
	struct tee_shm *device_shm = NULL;
	const uuid_t *device_uuid = NULL;
	struct tee_context *ctx = NULL;
	u32 shm_size = 0, idx, num_devices = 0;
	int rc;

	memset(&sess_arg, 0, sizeof(sess_arg));

	/* Open context with OP-TEE driver */
	ctx = tee_client_open_context(NULL, optee_ctx_match, NULL, NULL);
	if (IS_ERR(ctx))
		return -ENODEV;

	/* Open session with device enumeration pseudo TA */
	memcpy(sess_arg.uuid, pta_uuid.b, TEE_IOCTL_UUID_LEN);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg.num_params = 0;

	rc = tee_client_open_session(ctx, &sess_arg, NULL);
	if ((rc < 0) || (sess_arg.ret != TEEC_SUCCESS)) {
		/* Device enumeration pseudo TA not found */
		rc = 0;
		goto out_ctx;
	}

	rc = get_devices(ctx, sess_arg.session, NULL, &shm_size);
	if (rc < 0 || !shm_size)
		goto out_sess;

	device_shm = tee_shm_alloc(ctx, shm_size,
				   TEE_SHM_MAPPED | TEE_SHM_DMA_BUF);
	if (IS_ERR(device_shm)) {
		pr_err("tee_shm_alloc failed\n");
		rc = PTR_ERR(device_shm);
		goto out_sess;
	}

	rc = get_devices(ctx, sess_arg.session, device_shm, &shm_size);
	if (rc < 0)
		goto out_shm;

	device_uuid = tee_shm_get_va(device_shm, 0);
	if (IS_ERR(device_uuid)) {
		pr_err("tee_shm_get_va failed\n");
		rc = PTR_ERR(device_uuid);
		goto out_shm;
	}

	num_devices = shm_size / sizeof(uuid_t);

	for (idx = 0; idx < num_devices; idx++) {
		rc = optee_register_device(&device_uuid[idx], idx);
		if (rc)
			goto out_shm;
	}

out_shm:
	tee_shm_free(device_shm);
out_sess:
	tee_client_close_session(ctx, sess_arg.session);
out_ctx:
	tee_client_close_context(ctx);

	return rc;
}
