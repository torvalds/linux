// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Broadcom.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/tee_drv.h>
#include <linux/uuid.h>

#include <linux/firmware/broadcom/tee_bnxt_fw.h>

#define MAX_SHM_MEM_SZ	SZ_4M

#define MAX_TEE_PARAM_ARRY_MEMB		4

enum ta_cmd {
	/*
	 * TA_CMD_BNXT_FASTBOOT - boot bnxt device by copying f/w into sram
	 *
	 *	param[0] unused
	 *	param[1] unused
	 *	param[2] unused
	 *	param[3] unused
	 *
	 * Result:
	 *	TEE_SUCCESS - Invoke command success
	 *	TEE_ERROR_ITEM_NOT_FOUND - Corrupt f/w image found on memory
	 */
	TA_CMD_BNXT_FASTBOOT = 0,

	/*
	 * TA_CMD_BNXT_COPY_COREDUMP - copy the core dump into shm
	 *
	 *	param[0] (inout memref) - Coredump buffer memory reference
	 *	param[1] (in value) - value.a: offset, data to be copied from
	 *			      value.b: size of data to be copied
	 *	param[2] unused
	 *	param[3] unused
	 *
	 * Result:
	 *	TEE_SUCCESS - Invoke command success
	 *	TEE_ERROR_BAD_PARAMETERS - Incorrect input param
	 *	TEE_ERROR_ITEM_NOT_FOUND - Corrupt core dump
	 */
	TA_CMD_BNXT_COPY_COREDUMP = 3,
};

/**
 * struct tee_bnxt_fw_private - OP-TEE bnxt private data
 * @dev:		OP-TEE based bnxt device.
 * @ctx:		OP-TEE context handler.
 * @session_id:		TA session identifier.
 */
struct tee_bnxt_fw_private {
	struct device *dev;
	struct tee_context *ctx;
	u32 session_id;
	struct tee_shm *fw_shm_pool;
};

static struct tee_bnxt_fw_private pvt_data;

static void prepare_args(int cmd,
			 struct tee_ioctl_invoke_arg *arg,
			 struct tee_param *param)
{
	memset(arg, 0, sizeof(*arg));
	memset(param, 0, MAX_TEE_PARAM_ARRY_MEMB * sizeof(*param));

	arg->func = cmd;
	arg->session = pvt_data.session_id;
	arg->num_params = MAX_TEE_PARAM_ARRY_MEMB;

	/* Fill invoke cmd params */
	switch (cmd) {
	case TA_CMD_BNXT_COPY_COREDUMP:
		param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
		param[0].u.memref.shm = pvt_data.fw_shm_pool;
		param[0].u.memref.size = MAX_SHM_MEM_SZ;
		param[0].u.memref.shm_offs = 0;
		param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
		break;
	case TA_CMD_BNXT_FASTBOOT:
	default:
		/* Nothing to do */
		break;
	}
}

/**
 * tee_bnxt_fw_load() - Load the bnxt firmware
 *		    Uses an OP-TEE call to start a secure
 *		    boot process.
 * Returns 0 on success, negative errno otherwise.
 */
int tee_bnxt_fw_load(void)
{
	int ret = 0;
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[MAX_TEE_PARAM_ARRY_MEMB];

	if (!pvt_data.ctx)
		return -ENODEV;

	prepare_args(TA_CMD_BNXT_FASTBOOT, &arg, param);

	ret = tee_client_invoke_func(pvt_data.ctx, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		dev_err(pvt_data.dev,
			"TA_CMD_BNXT_FASTBOOT invoke failed TEE err: %x, ret:%x\n",
			arg.ret, ret);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(tee_bnxt_fw_load);

/**
 * tee_bnxt_copy_coredump() - Copy coredump from the allocated memory
 *			    Uses an OP-TEE call to copy coredump
 * @buf:	destination buffer where core dump is copied into
 * @offset:	offset from the base address of core dump area
 * @size:	size of the dump
 *
 * Returns 0 on success, negative errno otherwise.
 */
int tee_bnxt_copy_coredump(void *buf, u32 offset, u32 size)
{
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[MAX_TEE_PARAM_ARRY_MEMB];
	void *core_data;
	u32 rbytes = size;
	u32 nbytes = 0;
	int ret = 0;

	if (!pvt_data.ctx)
		return -ENODEV;

	prepare_args(TA_CMD_BNXT_COPY_COREDUMP, &arg, param);

	while (rbytes)  {
		nbytes = rbytes;

		nbytes = min_t(u32, rbytes, param[0].u.memref.size);

		/* Fill additional invoke cmd params */
		param[1].u.value.a = offset;
		param[1].u.value.b = nbytes;

		ret = tee_client_invoke_func(pvt_data.ctx, &arg, param);
		if (ret < 0 || arg.ret != 0) {
			dev_err(pvt_data.dev,
				"TA_CMD_BNXT_COPY_COREDUMP invoke failed TEE err: %x, ret:%x\n",
				arg.ret, ret);
			return -EINVAL;
		}

		core_data = tee_shm_get_va(pvt_data.fw_shm_pool, 0);
		if (IS_ERR(core_data)) {
			dev_err(pvt_data.dev, "tee_shm_get_va failed\n");
			return PTR_ERR(core_data);
		}

		memcpy(buf, core_data, nbytes);

		rbytes -= nbytes;
		buf += nbytes;
		offset += nbytes;
	}

	return 0;
}
EXPORT_SYMBOL(tee_bnxt_copy_coredump);

static int optee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	return (ver->impl_id == TEE_IMPL_ID_OPTEE);
}

static int tee_bnxt_fw_probe(struct device *dev)
{
	struct tee_client_device *bnxt_device = to_tee_client_device(dev);
	int ret, err = -ENODEV;
	struct tee_ioctl_open_session_arg sess_arg;
	struct tee_shm *fw_shm_pool;

	memset(&sess_arg, 0, sizeof(sess_arg));

	/* Open context with TEE driver */
	pvt_data.ctx = tee_client_open_context(NULL, optee_ctx_match, NULL,
					       NULL);
	if (IS_ERR(pvt_data.ctx))
		return -ENODEV;

	/* Open session with Bnxt load Trusted App */
	export_uuid(sess_arg.uuid, &bnxt_device->id.uuid);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg.num_params = 0;

	ret = tee_client_open_session(pvt_data.ctx, &sess_arg, NULL);
	if (ret < 0 || sess_arg.ret != 0) {
		dev_err(dev, "tee_client_open_session failed, err: %x\n",
			sess_arg.ret);
		err = -EINVAL;
		goto out_ctx;
	}
	pvt_data.session_id = sess_arg.session;

	pvt_data.dev = dev;

	fw_shm_pool = tee_shm_alloc_kernel_buf(pvt_data.ctx, MAX_SHM_MEM_SZ);
	if (IS_ERR(fw_shm_pool)) {
		dev_err(pvt_data.dev, "tee_shm_alloc_kernel_buf failed\n");
		err = PTR_ERR(fw_shm_pool);
		goto out_sess;
	}

	pvt_data.fw_shm_pool = fw_shm_pool;

	return 0;

out_sess:
	tee_client_close_session(pvt_data.ctx, pvt_data.session_id);
out_ctx:
	tee_client_close_context(pvt_data.ctx);

	return err;
}

static int tee_bnxt_fw_remove(struct device *dev)
{
	tee_shm_free(pvt_data.fw_shm_pool);
	tee_client_close_session(pvt_data.ctx, pvt_data.session_id);
	tee_client_close_context(pvt_data.ctx);
	pvt_data.ctx = NULL;

	return 0;
}

static void tee_bnxt_fw_shutdown(struct device *dev)
{
	tee_shm_free(pvt_data.fw_shm_pool);
	tee_client_close_session(pvt_data.ctx, pvt_data.session_id);
	tee_client_close_context(pvt_data.ctx);
	pvt_data.ctx = NULL;
}

static const struct tee_client_device_id tee_bnxt_fw_id_table[] = {
	{UUID_INIT(0x6272636D, 0x2019, 0x0716,
		    0x42, 0x43, 0x4D, 0x5F, 0x53, 0x43, 0x48, 0x49)},
	{}
};

MODULE_DEVICE_TABLE(tee, tee_bnxt_fw_id_table);

static struct tee_client_driver tee_bnxt_fw_driver = {
	.id_table	= tee_bnxt_fw_id_table,
	.driver		= {
		.name		= KBUILD_MODNAME,
		.bus		= &tee_bus_type,
		.probe		= tee_bnxt_fw_probe,
		.remove		= tee_bnxt_fw_remove,
		.shutdown	= tee_bnxt_fw_shutdown,
	},
};

static int __init tee_bnxt_fw_mod_init(void)
{
	return driver_register(&tee_bnxt_fw_driver.driver);
}

static void __exit tee_bnxt_fw_mod_exit(void)
{
	driver_unregister(&tee_bnxt_fw_driver.driver);
}

module_init(tee_bnxt_fw_mod_init);
module_exit(tee_bnxt_fw_mod_exit);

MODULE_AUTHOR("Vikas Gupta <vikas.gupta@broadcom.com>");
MODULE_DESCRIPTION("Broadcom bnxt firmware manager");
MODULE_LICENSE("GPL v2");
