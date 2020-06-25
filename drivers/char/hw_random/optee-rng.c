// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018-2019 Linaro Ltd.
 */

#include <linux/delay.h>
#include <linux/of.h>
#include <linux/hw_random.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/tee_drv.h>
#include <linux/uuid.h>

#define DRIVER_NAME "optee-rng"

#define TEE_ERROR_HEALTH_TEST_FAIL	0x00000001

/*
 * TA_CMD_GET_ENTROPY - Get Entropy from RNG
 *
 * param[0] (inout memref) - Entropy buffer memory reference
 * param[1] unused
 * param[2] unused
 * param[3] unused
 *
 * Result:
 * TEE_SUCCESS - Invoke command success
 * TEE_ERROR_BAD_PARAMETERS - Incorrect input param
 * TEE_ERROR_NOT_SUPPORTED - Requested entropy size greater than size of pool
 * TEE_ERROR_HEALTH_TEST_FAIL - Continuous health testing failed
 */
#define TA_CMD_GET_ENTROPY		0x0

/*
 * TA_CMD_GET_RNG_INFO - Get RNG information
 *
 * param[0] (out value) - value.a: RNG data-rate in bytes per second
 *                        value.b: Quality/Entropy per 1024 bit of data
 * param[1] unused
 * param[2] unused
 * param[3] unused
 *
 * Result:
 * TEE_SUCCESS - Invoke command success
 * TEE_ERROR_BAD_PARAMETERS - Incorrect input param
 */
#define TA_CMD_GET_RNG_INFO		0x1

#define MAX_ENTROPY_REQ_SZ		(4 * 1024)

/**
 * struct optee_rng_private - OP-TEE Random Number Generator private data
 * @dev:		OP-TEE based RNG device.
 * @ctx:		OP-TEE context handler.
 * @session_id:		RNG TA session identifier.
 * @data_rate:		RNG data rate.
 * @entropy_shm_pool:	Memory pool shared with RNG device.
 * @optee_rng:		OP-TEE RNG driver structure.
 */
struct optee_rng_private {
	struct device *dev;
	struct tee_context *ctx;
	u32 session_id;
	u32 data_rate;
	struct tee_shm *entropy_shm_pool;
	struct hwrng optee_rng;
};

#define to_optee_rng_private(r) \
		container_of(r, struct optee_rng_private, optee_rng)

static size_t get_optee_rng_data(struct optee_rng_private *pvt_data,
				 void *buf, size_t req_size)
{
	int ret = 0;
	u8 *rng_data = NULL;
	size_t rng_size = 0;
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	/* Invoke TA_CMD_GET_ENTROPY function of Trusted App */
	inv_arg.func = TA_CMD_GET_ENTROPY;
	inv_arg.session = pvt_data->session_id;
	inv_arg.num_params = 4;

	/* Fill invoke cmd params */
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	param[0].u.memref.shm = pvt_data->entropy_shm_pool;
	param[0].u.memref.size = req_size;
	param[0].u.memref.shm_offs = 0;

	ret = tee_client_invoke_func(pvt_data->ctx, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		dev_err(pvt_data->dev, "TA_CMD_GET_ENTROPY invoke err: %x\n",
			inv_arg.ret);
		return 0;
	}

	rng_data = tee_shm_get_va(pvt_data->entropy_shm_pool, 0);
	if (IS_ERR(rng_data)) {
		dev_err(pvt_data->dev, "tee_shm_get_va failed\n");
		return 0;
	}

	rng_size = param[0].u.memref.size;
	memcpy(buf, rng_data, rng_size);

	return rng_size;
}

static int optee_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct optee_rng_private *pvt_data = to_optee_rng_private(rng);
	size_t read = 0, rng_size = 0;
	int timeout = 1;
	u8 *data = buf;

	if (max > MAX_ENTROPY_REQ_SZ)
		max = MAX_ENTROPY_REQ_SZ;

	while (read == 0) {
		rng_size = get_optee_rng_data(pvt_data, data, (max - read));

		data += rng_size;
		read += rng_size;

		if (wait) {
			if (timeout-- == 0)
				return read;
			msleep((1000 * (max - read)) / pvt_data->data_rate);
		} else {
			return read;
		}
	}

	return read;
}

static int optee_rng_init(struct hwrng *rng)
{
	struct optee_rng_private *pvt_data = to_optee_rng_private(rng);
	struct tee_shm *entropy_shm_pool = NULL;

	entropy_shm_pool = tee_shm_alloc(pvt_data->ctx, MAX_ENTROPY_REQ_SZ,
					 TEE_SHM_MAPPED | TEE_SHM_DMA_BUF);
	if (IS_ERR(entropy_shm_pool)) {
		dev_err(pvt_data->dev, "tee_shm_alloc failed\n");
		return PTR_ERR(entropy_shm_pool);
	}

	pvt_data->entropy_shm_pool = entropy_shm_pool;

	return 0;
}

static void optee_rng_cleanup(struct hwrng *rng)
{
	struct optee_rng_private *pvt_data = to_optee_rng_private(rng);

	tee_shm_free(pvt_data->entropy_shm_pool);
}

static struct optee_rng_private pvt_data = {
	.optee_rng = {
		.name		= DRIVER_NAME,
		.init		= optee_rng_init,
		.cleanup	= optee_rng_cleanup,
		.read		= optee_rng_read,
	}
};

static int get_optee_rng_info(struct device *dev)
{
	int ret = 0;
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	/* Invoke TA_CMD_GET_RNG_INFO function of Trusted App */
	inv_arg.func = TA_CMD_GET_RNG_INFO;
	inv_arg.session = pvt_data.session_id;
	inv_arg.num_params = 4;

	/* Fill invoke cmd params */
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT;

	ret = tee_client_invoke_func(pvt_data.ctx, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		dev_err(dev, "TA_CMD_GET_RNG_INFO invoke err: %x\n",
			inv_arg.ret);
		return -EINVAL;
	}

	pvt_data.data_rate = param[0].u.value.a;
	pvt_data.optee_rng.quality = param[0].u.value.b;

	return 0;
}

static int optee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	if (ver->impl_id == TEE_IMPL_ID_OPTEE)
		return 1;
	else
		return 0;
}

static int optee_rng_probe(struct device *dev)
{
	struct tee_client_device *rng_device = to_tee_client_device(dev);
	int ret = 0, err = -ENODEV;
	struct tee_ioctl_open_session_arg sess_arg;

	memset(&sess_arg, 0, sizeof(sess_arg));

	/* Open context with TEE driver */
	pvt_data.ctx = tee_client_open_context(NULL, optee_ctx_match, NULL,
					       NULL);
	if (IS_ERR(pvt_data.ctx))
		return -ENODEV;

	/* Open session with hwrng Trusted App */
	export_uuid(sess_arg.uuid, &rng_device->id.uuid);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg.num_params = 0;

	ret = tee_client_open_session(pvt_data.ctx, &sess_arg, NULL);
	if ((ret < 0) || (sess_arg.ret != 0)) {
		dev_err(dev, "tee_client_open_session failed, err: %x\n",
			sess_arg.ret);
		err = -EINVAL;
		goto out_ctx;
	}
	pvt_data.session_id = sess_arg.session;

	err = get_optee_rng_info(dev);
	if (err)
		goto out_sess;

	err = hwrng_register(&pvt_data.optee_rng);
	if (err) {
		dev_err(dev, "hwrng registration failed (%d)\n", err);
		goto out_sess;
	}

	pvt_data.dev = dev;

	return 0;

out_sess:
	tee_client_close_session(pvt_data.ctx, pvt_data.session_id);
out_ctx:
	tee_client_close_context(pvt_data.ctx);

	return err;
}

static int optee_rng_remove(struct device *dev)
{
	hwrng_unregister(&pvt_data.optee_rng);
	tee_client_close_session(pvt_data.ctx, pvt_data.session_id);
	tee_client_close_context(pvt_data.ctx);

	return 0;
}

static const struct tee_client_device_id optee_rng_id_table[] = {
	{UUID_INIT(0xab7a617c, 0xb8e7, 0x4d8f,
		   0x83, 0x01, 0xd0, 0x9b, 0x61, 0x03, 0x6b, 0x64)},
	{}
};

MODULE_DEVICE_TABLE(tee, optee_rng_id_table);

static struct tee_client_driver optee_rng_driver = {
	.id_table	= optee_rng_id_table,
	.driver		= {
		.name		= DRIVER_NAME,
		.bus		= &tee_bus_type,
		.probe		= optee_rng_probe,
		.remove		= optee_rng_remove,
	},
};

static int __init optee_rng_mod_init(void)
{
	return driver_register(&optee_rng_driver.driver);
}

static void __exit optee_rng_mod_exit(void)
{
	driver_unregister(&optee_rng_driver.driver);
}

module_init(optee_rng_mod_init);
module_exit(optee_rng_mod_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sumit Garg <sumit.garg@linaro.org>");
MODULE_DESCRIPTION("OP-TEE based random number generator driver");
