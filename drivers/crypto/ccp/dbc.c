// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Secure Processor Dynamic Boost Control interface
 *
 * Copyright (C) 2023 Advanced Micro Devices, Inc.
 *
 * Author: Mario Limonciello <mario.limonciello@amd.com>
 */

#include "dbc.h"

#define DBC_DEFAULT_TIMEOUT		(10 * MSEC_PER_SEC)
struct error_map {
	u32 psp;
	int ret;
};

#define DBC_ERROR_ACCESS_DENIED		0x0001
#define DBC_ERROR_EXCESS_DATA		0x0004
#define DBC_ERROR_BAD_PARAMETERS	0x0006
#define DBC_ERROR_BAD_STATE		0x0007
#define DBC_ERROR_NOT_IMPLEMENTED	0x0009
#define DBC_ERROR_BUSY			0x000D
#define DBC_ERROR_MESSAGE_FAILURE	0x0307
#define DBC_ERROR_OVERFLOW		0x300F
#define DBC_ERROR_SIGNATURE_INVALID	0x3072

static struct error_map error_codes[] = {
	{DBC_ERROR_ACCESS_DENIED,	-EACCES},
	{DBC_ERROR_EXCESS_DATA,		-E2BIG},
	{DBC_ERROR_BAD_PARAMETERS,	-EINVAL},
	{DBC_ERROR_BAD_STATE,		-EAGAIN},
	{DBC_ERROR_MESSAGE_FAILURE,	-ENOENT},
	{DBC_ERROR_NOT_IMPLEMENTED,	-ENOENT},
	{DBC_ERROR_BUSY,		-EBUSY},
	{DBC_ERROR_OVERFLOW,		-ENFILE},
	{DBC_ERROR_SIGNATURE_INVALID,	-EPERM},
	{0x0,	0x0},
};

static inline int send_dbc_cmd_thru_ext(struct psp_dbc_device *dbc_dev, int msg)
{
	dbc_dev->mbox->ext_req.header.sub_cmd_id = msg;

	return psp_extended_mailbox_cmd(dbc_dev->psp,
					DBC_DEFAULT_TIMEOUT,
					(struct psp_ext_request *)dbc_dev->mbox);
}

static inline int send_dbc_cmd_thru_pa(struct psp_dbc_device *dbc_dev, int msg)
{
	return psp_send_platform_access_msg(msg,
					    (struct psp_request *)dbc_dev->mbox);
}

static int send_dbc_cmd(struct psp_dbc_device *dbc_dev, int msg)
{
	int ret;

	*dbc_dev->result = 0;
	ret = dbc_dev->use_ext ? send_dbc_cmd_thru_ext(dbc_dev, msg) :
				 send_dbc_cmd_thru_pa(dbc_dev, msg);
	if (ret == -EIO) {
		int i;

		dev_dbg(dbc_dev->dev,
			 "msg 0x%x failed with PSP error: 0x%x\n",
			 msg, *dbc_dev->result);

		for (i = 0; error_codes[i].psp; i++) {
			if (*dbc_dev->result == error_codes[i].psp)
				return error_codes[i].ret;
		}
	}

	return ret;
}

static int send_dbc_nonce(struct psp_dbc_device *dbc_dev)
{
	int ret;

	*dbc_dev->payload_size = dbc_dev->header_size + sizeof(struct dbc_user_nonce);
	ret = send_dbc_cmd(dbc_dev, PSP_DYNAMIC_BOOST_GET_NONCE);
	if (ret == -EAGAIN) {
		dev_dbg(dbc_dev->dev, "retrying get nonce\n");
		ret = send_dbc_cmd(dbc_dev, PSP_DYNAMIC_BOOST_GET_NONCE);
	}

	return ret;
}

static int send_dbc_parameter(struct psp_dbc_device *dbc_dev)
{
	struct dbc_user_param *user_param = (struct dbc_user_param *)dbc_dev->payload;

	switch (user_param->msg_index) {
	case PARAM_SET_FMAX_CAP:
	case PARAM_SET_PWR_CAP:
	case PARAM_SET_GFX_MODE:
		return send_dbc_cmd(dbc_dev, PSP_DYNAMIC_BOOST_SET_PARAMETER);
	case PARAM_GET_FMAX_CAP:
	case PARAM_GET_PWR_CAP:
	case PARAM_GET_CURR_TEMP:
	case PARAM_GET_FMAX_MAX:
	case PARAM_GET_FMAX_MIN:
	case PARAM_GET_SOC_PWR_MAX:
	case PARAM_GET_SOC_PWR_MIN:
	case PARAM_GET_SOC_PWR_CUR:
	case PARAM_GET_GFX_MODE:
		return send_dbc_cmd(dbc_dev, PSP_DYNAMIC_BOOST_GET_PARAMETER);
	}

	return -EINVAL;
}

void dbc_dev_destroy(struct psp_device *psp)
{
	struct psp_dbc_device *dbc_dev = psp->dbc_data;

	if (!dbc_dev)
		return;

	misc_deregister(&dbc_dev->char_dev);
	mutex_destroy(&dbc_dev->ioctl_mutex);
	psp->dbc_data = NULL;
}

static long dbc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct psp_device *psp_master = psp_get_master_device();
	void __user *argp = (void __user *)arg;
	struct psp_dbc_device *dbc_dev;
	int ret;

	if (!psp_master || !psp_master->dbc_data)
		return -ENODEV;
	dbc_dev = psp_master->dbc_data;

	mutex_lock(&dbc_dev->ioctl_mutex);

	switch (cmd) {
	case DBCIOCNONCE:
		if (copy_from_user(dbc_dev->payload, argp, sizeof(struct dbc_user_nonce))) {
			ret = -EFAULT;
			goto unlock;
		}

		ret = send_dbc_nonce(dbc_dev);
		if (ret)
			goto unlock;

		if (copy_to_user(argp, dbc_dev->payload, sizeof(struct dbc_user_nonce))) {
			ret = -EFAULT;
			goto unlock;
		}
		break;
	case DBCIOCUID:
		if (copy_from_user(dbc_dev->payload, argp, sizeof(struct dbc_user_setuid))) {
			ret = -EFAULT;
			goto unlock;
		}

		*dbc_dev->payload_size = dbc_dev->header_size + sizeof(struct dbc_user_setuid);
		ret = send_dbc_cmd(dbc_dev, PSP_DYNAMIC_BOOST_SET_UID);
		if (ret)
			goto unlock;

		if (copy_to_user(argp, dbc_dev->payload, sizeof(struct dbc_user_setuid))) {
			ret = -EFAULT;
			goto unlock;
		}
		break;
	case DBCIOCPARAM:
		if (copy_from_user(dbc_dev->payload, argp, sizeof(struct dbc_user_param))) {
			ret = -EFAULT;
			goto unlock;
		}

		*dbc_dev->payload_size = dbc_dev->header_size + sizeof(struct dbc_user_param);
		ret = send_dbc_parameter(dbc_dev);
		if (ret)
			goto unlock;

		if (copy_to_user(argp, dbc_dev->payload, sizeof(struct dbc_user_param)))  {
			ret = -EFAULT;
			goto unlock;
		}
		break;
	default:
		ret = -EINVAL;

	}
unlock:
	mutex_unlock(&dbc_dev->ioctl_mutex);

	return ret;
}

static const struct file_operations dbc_fops = {
	.owner	= THIS_MODULE,
	.unlocked_ioctl = dbc_ioctl,
};

int dbc_dev_init(struct psp_device *psp)
{
	struct device *dev = psp->dev;
	struct psp_dbc_device *dbc_dev;
	int ret;

	dbc_dev = devm_kzalloc(dev, sizeof(*dbc_dev), GFP_KERNEL);
	if (!dbc_dev)
		return -ENOMEM;

	BUILD_BUG_ON(sizeof(union dbc_buffer) > PAGE_SIZE);
	dbc_dev->mbox = (void *)devm_get_free_pages(dev, GFP_KERNEL | __GFP_ZERO, 0);
	if (!dbc_dev->mbox) {
		ret = -ENOMEM;
		goto cleanup_dev;
	}

	psp->dbc_data = dbc_dev;
	dbc_dev->dev = dev;
	dbc_dev->psp = psp;

	if (psp->capability.dbc_thru_ext) {
		dbc_dev->use_ext = true;
		dbc_dev->payload_size = &dbc_dev->mbox->ext_req.header.payload_size;
		dbc_dev->result = &dbc_dev->mbox->ext_req.header.status;
		dbc_dev->payload = &dbc_dev->mbox->ext_req.buf;
		dbc_dev->header_size = sizeof(struct psp_ext_req_buffer_hdr);
	} else {
		dbc_dev->payload_size = &dbc_dev->mbox->pa_req.header.payload_size;
		dbc_dev->result = &dbc_dev->mbox->pa_req.header.status;
		dbc_dev->payload = &dbc_dev->mbox->pa_req.buf;
		dbc_dev->header_size = sizeof(struct psp_req_buffer_hdr);
	}

	ret = send_dbc_nonce(dbc_dev);
	if (ret == -EACCES) {
		dev_dbg(dbc_dev->dev,
			"dynamic boost control was previously authenticated\n");
		ret = 0;
	}
	dev_dbg(dbc_dev->dev, "dynamic boost control is %savailable\n",
		ret ? "un" : "");
	if (ret) {
		ret = 0;
		goto cleanup_mbox;
	}

	dbc_dev->char_dev.minor = MISC_DYNAMIC_MINOR;
	dbc_dev->char_dev.name = "dbc";
	dbc_dev->char_dev.fops = &dbc_fops;
	dbc_dev->char_dev.mode = 0600;
	ret = misc_register(&dbc_dev->char_dev);
	if (ret)
		goto cleanup_mbox;

	mutex_init(&dbc_dev->ioctl_mutex);

	return 0;

cleanup_mbox:
	devm_free_pages(dev, (unsigned long)dbc_dev->mbox);

cleanup_dev:
	psp->dbc_data = NULL;
	devm_kfree(dev, dbc_dev);

	return ret;
}
