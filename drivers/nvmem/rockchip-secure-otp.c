// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip Secure OTP Driver
 *
 * Copyright (c) 2023 Rockchip Electronics Co. Ltd.
 * Author: Hisping <hisping.lin@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/reset.h>
#include <linux/rockchip/cpu.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/tee_drv.h>
#include <linux/uuid.h>

static DEFINE_MUTEX(nvmem_mutex);

struct rockchip_data;

struct rockchip_otp {
	struct device *dev;
	struct nvmem_config *config;
	const struct rockchip_data *data;
};

struct rockchip_data {
	int size;
	int (*reg_read)(unsigned int offset, void *val, size_t bytes);
	int (*reg_write)(unsigned int offset, void *val, size_t bytes);
	int (*init)(struct rockchip_otp *otp);
};

static int optee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	if (ver->impl_id == TEE_IMPL_ID_OPTEE)
		return 1;
	else
		return 0;
}

/*
 * func: read data from non-protected oem zone in secure otp
 */
#define STORAGE_CMD_READ_OEM_NS_OTP		13
int rockchip_read_oem_non_protected_otp(unsigned int byte_off,
				void *byte_buf, size_t byte_len)
{
	const uuid_t pta_uuid =
		UUID_INIT(0x2d26d8a8, 0x5134, 0x4dd8,
			  0xb3, 0x2f, 0xb3, 0x4b, 0xce, 0xeb, 0xc4, 0x71);
	struct tee_ioctl_open_session_arg sess_arg;
	struct tee_shm *device_shm = NULL;
	struct tee_context *ctx = NULL;
	u32 shm_size = 0;
	int rc;
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];
	u8 *read_data = NULL;

	if (!byte_buf) {
		pr_err("buf is null\n");
		return -EINVAL;
	}

	memset(&sess_arg, 0, sizeof(sess_arg));

	mutex_lock(&nvmem_mutex);

	/* Open context with OP-TEE driver */
	ctx = tee_client_open_context(NULL, optee_ctx_match, NULL, NULL);
	if (IS_ERR(ctx)) {
		pr_err("tee_client_open_context failed\n");
		rc = -ENODEV;
		goto out_exit;
	}

	/* Open session */
	memcpy(sess_arg.uuid, pta_uuid.b, TEE_IOCTL_UUID_LEN);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg.num_params = 0;

	rc = tee_client_open_session(ctx, &sess_arg, NULL);
	if ((rc < 0) || (sess_arg.ret != 0)) {
		pr_err("tee_client_open_session failed, err: %x\n",
			sess_arg.ret);
		rc = -EINVAL;
		goto out_ctx;
	}

	/* Alloc share memory */
	shm_size = byte_len;
	device_shm = tee_shm_alloc(ctx, shm_size,
				   TEE_SHM_MAPPED | TEE_SHM_DMA_BUF);
	if (IS_ERR(device_shm)) {
		pr_err("tee_shm_alloc failed\n");
		rc = PTR_ERR(device_shm);
		goto out_sess;
	}

	/* Invoke func */
	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	inv_arg.func = STORAGE_CMD_READ_OEM_NS_OTP;
	inv_arg.session =  sess_arg.session;
	inv_arg.num_params = 4;

	/* Fill invoke cmd params */
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = byte_off;

	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT;
	param[1].u.memref.shm = device_shm;
	param[1].u.memref.size = shm_size;
	param[1].u.memref.shm_offs = 0;

	rc = tee_client_invoke_func(ctx, &inv_arg, param);
	if ((rc < 0) || (inv_arg.ret != 0)) {
		pr_err("invoke function err: %x\n", inv_arg.ret);
		rc = -EINVAL;
		goto out_shm;
	}

	read_data = tee_shm_get_va(device_shm, 0);
	if (IS_ERR(read_data)) {
		pr_err("tee_shm_get_va failed\n");
		rc = -EINVAL;
		goto out_shm;
	}
	memcpy(byte_buf, read_data, byte_len);

out_shm:
	tee_shm_free(device_shm);
out_sess:
	tee_client_close_session(ctx, sess_arg.session);
out_ctx:
	tee_client_close_context(ctx);
out_exit:
	mutex_unlock(&nvmem_mutex);
	return rc;
}
EXPORT_SYMBOL_GPL(rockchip_read_oem_non_protected_otp);

/*
 * func: write data to non-protected oem zone in secure otp
 */
#define STORAGE_CMD_WRITE_OEM_NS_OTP		12
int rockchip_write_oem_non_protected_otp(unsigned int byte_off,
				void *byte_buf, size_t byte_len)
{
	const uuid_t pta_uuid =
		UUID_INIT(0x2d26d8a8, 0x5134, 0x4dd8,
			  0xb3, 0x2f, 0xb3, 0x4b, 0xce, 0xeb, 0xc4, 0x71);
	struct tee_ioctl_open_session_arg sess_arg;
	struct tee_shm *device_shm = NULL;
	struct tee_context *ctx = NULL;
	u32 shm_size = 0;
	int rc;
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];
	u8 *write_data = NULL;

	if (!byte_buf) {
		pr_err("buf is null\n");
		return -EINVAL;
	}

	memset(&sess_arg, 0, sizeof(sess_arg));

	mutex_lock(&nvmem_mutex);

	/* Open context with OP-TEE driver */
	ctx = tee_client_open_context(NULL, optee_ctx_match, NULL, NULL);
	if (IS_ERR(ctx)) {
		pr_err("tee_client_open_context failed\n");
		rc = -ENODEV;
		goto out_exit;
	}

	/* Open session */
	memcpy(sess_arg.uuid, pta_uuid.b, TEE_IOCTL_UUID_LEN);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg.num_params = 0;

	rc = tee_client_open_session(ctx, &sess_arg, NULL);
	if ((rc < 0) || (sess_arg.ret != 0)) {
		pr_err("tee_client_open_session failed, err: %x\n",
			sess_arg.ret);
		rc = -EINVAL;
		goto out_ctx;
	}

	/* Alloc share memory */
	shm_size = byte_len;
	device_shm = tee_shm_alloc(ctx, shm_size,
				   TEE_SHM_MAPPED | TEE_SHM_DMA_BUF);
	if (IS_ERR(device_shm)) {
		pr_err("tee_shm_alloc failed\n");
		rc = PTR_ERR(device_shm);
		goto out_sess;
	}

	write_data = tee_shm_get_va(device_shm, 0);
	if (IS_ERR(write_data)) {
		pr_err("tee_shm_get_va failed\n");
		rc = -EINVAL;
		goto out_shm;
	}
	memcpy(write_data, byte_buf, byte_len);

	/* Invoke func */
	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	inv_arg.func = STORAGE_CMD_WRITE_OEM_NS_OTP;
	inv_arg.session =  sess_arg.session;
	inv_arg.num_params = 4;

	/* Fill invoke cmd params */
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = byte_off;

	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	param[1].u.memref.shm = device_shm;
	param[1].u.memref.size = shm_size;
	param[1].u.memref.shm_offs = 0;

	rc = tee_client_invoke_func(ctx, &inv_arg, param);
	if ((rc < 0) || (inv_arg.ret != 0)) {
		pr_err("invoke function err: %x\n", inv_arg.ret);
		rc = -EINVAL;
		goto out_shm;
	}

out_shm:
	tee_shm_free(device_shm);
out_sess:
	tee_client_close_session(ctx, sess_arg.session);
out_ctx:
	tee_client_close_context(ctx);
out_exit:
	mutex_unlock(&nvmem_mutex);
	return rc;
}
EXPORT_SYMBOL_GPL(rockchip_write_oem_non_protected_otp);

static int rockchip_secure_otp_read(void *context, unsigned int offset,
				void *val, size_t bytes)
{
	struct rockchip_otp *otp = context;
	int ret = -EINVAL;

	if (otp->data && otp->data->reg_read)
		ret = otp->data->reg_read(offset, val, bytes);

	return ret;
}

static int rockchip_secure_otp_write(void *context, unsigned int offset,
				void *val, size_t bytes)
{
	struct rockchip_otp *otp = context;
	int ret = -EINVAL;

	if (otp->data && otp->data->reg_write)
		ret = otp->data->reg_write(offset, val, bytes);

	return ret;
}

static struct nvmem_config otp_config = {
	.name = "rockchip-secure-otp",
	.owner = THIS_MODULE,
	.read_only = false,
	.reg_read = rockchip_secure_otp_read,
	.reg_write = rockchip_secure_otp_write,
	.stride = 4,
	.word_size = 4,
};

static const struct rockchip_data secure_otp_data = {
	.reg_read = rockchip_read_oem_non_protected_otp,
	.reg_write = rockchip_write_oem_non_protected_otp,
};

static const struct of_device_id rockchip_secure_otp_match[] = {
	{
		.compatible = "rockchip,secure-otp",
		.data = (void *)&secure_otp_data,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rockchip_secure_otp_match);

static int rockchip_secure_otp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_otp *otp;
	const struct rockchip_data *data;
	struct nvmem_device *nvmem;
	u32 otp_size;
	int ret;

	data = device_get_match_data(dev);
	if (!data) {
		dev_err(dev, "failed to get match data\n");
		return -EINVAL;
	}

	ret = device_property_read_u32(dev, "rockchip,otp-size", &otp_size);
	if (ret) {
		dev_err(dev, "otp size parameter not specified\n");
		return -EINVAL;
	} else if (otp_size == 0) {
		dev_err(dev, "otp size must be > 0\n");
		return -EINVAL;
	}

	otp = devm_kzalloc(&pdev->dev, sizeof(struct rockchip_otp),
			   GFP_KERNEL);
	if (!otp)
		return -ENOMEM;

	otp->data = data;
	otp->dev = dev;

	otp->config = &otp_config;
	otp->config->size = otp_size;
	otp->config->priv = otp;
	otp->config->dev = dev;

	if (data->init) {
		ret = data->init(otp);
		if (ret)
			return ret;
	}

	nvmem = devm_nvmem_register(dev, otp->config);

	return PTR_ERR_OR_ZERO(nvmem);
}

static struct platform_driver rockchip_secure_otp_driver = {
	.probe = rockchip_secure_otp_probe,
	.driver = {
		.name = "rockchip-secure-otp",
		.of_match_table = rockchip_secure_otp_match,
	},
};

static int __init rockchip_secure_otp_init(void)
{
	int ret;

	ret = platform_driver_register(&rockchip_secure_otp_driver);
	if (ret) {
		pr_err("failed to register secure otp driver\n");
		return ret;
	}

	return 0;
}

static void __exit rockchip_secure_otp_exit(void)
{
	return platform_driver_unregister(&rockchip_secure_otp_driver);
}

subsys_initcall(rockchip_secure_otp_init);
module_exit(rockchip_secure_otp_exit);

MODULE_DESCRIPTION("Rockchip Secure OTP Driver");
MODULE_LICENSE("GPL");
