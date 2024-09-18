// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022,2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/qseecom_kernel.h>
#include <linux/of_platform.h>
#include <linux/mod_devicetable.h>

static struct qseecom_drv_ops qseecom_fun_ops = {0};

int provide_qseecom_kernel_fun_ops(const struct qseecom_drv_ops *ops)
{
	if (!ops) {
		pr_err("ops is NULL\n");
		return -1;
	}
	qseecom_fun_ops = *ops;
	pr_debug("QSEECOM proxy Ready to be served\n");
	return 0;
}
EXPORT_SYMBOL(provide_qseecom_kernel_fun_ops);

int qseecom_start_app(struct qseecom_handle **handle,
					char *app_name, uint32_t size)
{
	int32_t ret = -1;

	/* start the application */
	if (qseecom_fun_ops.qseecom_start_app) {
		ret = qseecom_fun_ops.qseecom_start_app(handle, app_name, size);
		if (ret != 0)
			pr_err("%s: Start app -%s failed\n", __func__, app_name);
	} else {
		pr_err_ratelimited("Qseecom driver is not up yet\n");
		ret = -EAGAIN;
	}
	return ret;
}
EXPORT_SYMBOL(qseecom_start_app);


int qseecom_shutdown_app(struct qseecom_handle **handle)
{
	int32_t ret = -1;

	/* shutdown the application */
	if (qseecom_fun_ops.qseecom_shutdown_app) {
		ret = qseecom_fun_ops.qseecom_shutdown_app(handle);
		if (ret != 0)
			pr_err("%s: qseecom shutdown app failed with ret = %d\n", __func__, ret);
	} else {
		pr_err_ratelimited("Qseecom driver is not up yet\n");
		ret = -EAGAIN;
	}
	return ret;
}
EXPORT_SYMBOL(qseecom_shutdown_app);


int qseecom_send_command(struct qseecom_handle *handle, void *send_buf,
	uint32_t sbuf_len, void *resp_buf, uint32_t rbuf_len)
{
	int32_t ret = -1;

	/* send command to application*/
	if (qseecom_fun_ops.qseecom_send_command) {
		ret = qseecom_fun_ops.qseecom_send_command(handle, send_buf, sbuf_len,
					resp_buf, rbuf_len);
		if (ret != 0)
			pr_err("%s: qseecom send command failed with ret = %d\n", __func__, ret);
	} else {
		pr_err_ratelimited("Qseecom driver is not up yet\n");
		ret = -EAGAIN;
	}
	return ret;
}
EXPORT_SYMBOL(qseecom_send_command);

int qseecom_process_listener_from_smcinvoke(uint32_t *result,
	u64 *response_type, unsigned int *data)
{
	int32_t ret = -1;

	/* process listener from smcinvoke*/
	if (qseecom_fun_ops.qseecom_process_listener_from_smcinvoke) {
		ret = qseecom_fun_ops.qseecom_process_listener_from_smcinvoke(result,
				response_type, data);
		if (ret != 0)
			pr_err("%s: failed with =%d\n", __func__, ret);
	} else {
		pr_err_ratelimited("Qseecom driver is not yet up or qseecom not enabled.\n");
		ret = -EAGAIN;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(qseecom_process_listener_from_smcinvoke);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qseecom proxy driver");
