// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Shunqing Chen <csq@rock-chips.com>
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rockchip/rockchip_sip.h>
#include <linux/sched.h>
#include <linux/soc/rockchip/rk_vendor_storage.h>
#include <linux/slab.h>

#include "rk_hdmirx.h"
#include "rk_hdmirx_hdcp.h"

enum rk_hdmirx_hdcp_state {
	HDMIRX_HDCP_DISABLED,
	HDMIRX_HDCP_AUTH_START,
	HDMIRX_HDCP_AUTH_SUCCESS,
	HDMIRX_HDCP_AUTH_FAIL,
};

static struct rk_hdmirx_hdcp *g_hdmirx_hdcp;

static void hdmirx_hdcp_write(struct rk_hdmirx_hdcp *hdcp, int reg, u32 val)
{
	hdcp->write(hdcp->hdmirx, reg, val);
}

static u32 hdmirx_hdcp_read(struct rk_hdmirx_hdcp *hdcp, int reg)
{
	return hdcp->read(hdcp->hdmirx, reg);
}

static void hdmirx_hdcp_update_bits(struct rk_hdmirx_hdcp *hdcp, int reg,
				    u32 mask, u32 data)
{
	u32 val = hdmirx_hdcp_read(hdcp, reg) & ~mask;

	val |= (data & mask);
	hdmirx_hdcp_write(hdcp, reg, val);
}

static int hdcp_load_keys_cb(struct rk_hdmirx_hdcp *hdcp)
{
	int size;
	u8 hdcp_vendor_data[VENDOR_DATA_SIZE + 1];
	struct hdcp_key_data_t *key_data;
	void __iomem *base;

	size = rk_vendor_read(HDMIRX_HDCP1X_ID, hdcp_vendor_data,
			      VENDOR_DATA_SIZE);
	if (size < (HDCP_KEY_SIZE + HDCP_KEY_SEED_SIZE)) {
		dev_dbg(hdcp->dev, "HDCP: read size %d\n", size);
		return -EINVAL;
	}

	key_data = (struct hdcp_key_data_t *)hdcp_vendor_data;
	if ((key_data->signature != HDCP_SIG_MAGIC)
	    || !(key_data->flags & HDCP_FLG_AES))
		hdcp->aes_encrypt = false;
	else
		hdcp->aes_encrypt = true;

	base = sip_hdcp_request_share_memory(HDMI_RX);
	if (!base)
		return -ENOMEM;

	memcpy(base, hdcp_vendor_data, size);
	hdcp->keys_is_load = true;

	return 0;
}

static int rk_hdmirx_hdcp_load_key(struct rk_hdmirx_hdcp *hdcp)
{
	int ret;

	if (!hdcp->keys_is_load) {
		ret = hdcp_load_keys_cb(hdcp);
		if (ret)
			return ret;
	}

	hdcp->status = HDMIRX_HDCP_AUTH_START;
	if (hdcp->aes_encrypt)
		sip_hdcp_config(HDCP_FUNC_KEY_LOAD, HDMI_RX, 0);
	else
		sip_hdcp_config(HDCP_FUNC_KEY_LOAD, HDMI_RX, 1);

	hdmirx_hdcp_update_bits(hdcp, HDCP2_CONFIG,
				HDCP2_CONNECTED |
				HDCP2_SWITCH_OVR_VALUE |
				HDCP2_SWITCH_OVR_EN,
				HDCP2_CONNECTED |
				HDCP2_SWITCH_OVR_EN);

	return 0;
}

static int rk_hdmirx_hdcp1x_start(struct rk_hdmirx_hdcp *hdcp)
{
	if (!hdcp->enable)
		return -EPERM;

	rk_hdmirx_hdcp_load_key(hdcp);

	dev_dbg(hdcp->dev, "%s success\n", __func__);
	return 0;
}

static int rk_hdmirx_hdcp1x_stop(struct rk_hdmirx_hdcp *hdcp)
{
	if (!hdcp->enable)
		return -EPERM;

	hdmirx_hdcp_update_bits(hdcp, GLOBAL_SWENABLE, HDCP_ENABLE, 0);
	hdcp->status = HDMIRX_HDCP_DISABLED;
	return 0;
}

static int rk_hdmirx_hdcp_start(struct rk_hdmirx_hdcp *hdcp)
{
	if (hdcp->hdcp2 && hdcp->hdcp2->enable) {
		hdcp->hdcp2->start();
		return 0;
	}
	return rk_hdmirx_hdcp1x_start(hdcp);
}

static int rk_hdmirx_hdcp_stop(struct rk_hdmirx_hdcp *hdcp)
{
	if (hdcp->hdcp2 && hdcp->hdcp2->stop) {
		hdcp->hdcp2->stop();
		dev_dbg(hdcp->dev, "hdcp2 stop\n");
	}

	return rk_hdmirx_hdcp1x_stop(hdcp);
}

static ssize_t enable_show(struct device *device,
				struct device_attribute *attr, char *buf)
{
	bool enable = 0;
	struct rk_hdmirx_hdcp *hdcp = g_hdmirx_hdcp;

	if (hdcp)
		enable = hdcp->enable;

	return snprintf(buf, PAGE_SIZE, "%d\n", enable);
}

static ssize_t enable_store(struct device *device,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	bool enable;
	struct rk_hdmirx_hdcp *hdcp = g_hdmirx_hdcp;

	if (!hdcp)
		return -EINVAL;

	if (kstrtobool(buf, &enable))
		return -EINVAL;

	if (hdcp->enable != enable) {
		if (enable) {
			hdcp->enable = enable;
			rk_hdmirx_hdcp1x_start(hdcp);
		} else {
			rk_hdmirx_hdcp1x_stop(hdcp);
			hdcp->enable = enable;
		}
	}

	return count;
}

static DEVICE_ATTR_RW(enable);

static ssize_t status_show(struct device *device,
			   struct device_attribute *attr, char *buf)
{
	int status = HDMIRX_HDCP_DISABLED;
	struct rk_hdmirx_hdcp *hdcp = g_hdmirx_hdcp;
	u32 val;

	if (hdcp)
		status = hdcp->status;

	if (status == HDMIRX_HDCP_AUTH_START) {
		val = hdmirx_hdcp_read(hdcp, HDCP14_STATUS);
		if ((val & 0x3) == 0)
			status = HDMIRX_HDCP_DISABLED;
		else if ((val & 0x3) == 0x1)
			status = HDMIRX_HDCP_AUTH_START;
		else if (val & BIT(8))
			status = HDMIRX_HDCP_AUTH_SUCCESS;
		else
			status = HDMIRX_HDCP_AUTH_FAIL;
	}
	if (status == HDMIRX_HDCP_DISABLED)
		return snprintf(buf, PAGE_SIZE, "hdcp disable\n");
	else if (status == HDMIRX_HDCP_AUTH_START)
		return snprintf(buf, PAGE_SIZE, "hdcp_auth_start\n");
	else if (status == HDMIRX_HDCP_AUTH_SUCCESS)
		return snprintf(buf, PAGE_SIZE, "hdcp_auth_success\n");
	else if (status == HDMIRX_HDCP_AUTH_FAIL)
		return snprintf(buf, PAGE_SIZE, "hdcp_auth_fail\n");
	else
		return snprintf(buf, PAGE_SIZE, "unknown status\n");
}

static DEVICE_ATTR_RO(status);

struct rk_hdmirx_hdcp *rk_hdmirx_hdcp_register(struct rk_hdmirx_hdcp *hdcp_data)
{
	int ret = 0;
	struct rk_hdmirx_hdcp *hdcp;

	if (!hdcp_data)
		return NULL;


	hdcp = devm_kzalloc(hdcp_data->dev, sizeof(*hdcp), GFP_KERNEL);
	if (!hdcp)
		return NULL;

	hdcp->hdmirx = hdcp_data->hdmirx;
	hdcp->write = hdcp_data->write;
	hdcp->read = hdcp_data->read;
	hdcp->enable = hdcp_data->enable;
	hdcp->dev = hdcp_data->dev;
	g_hdmirx_hdcp = hdcp;
	hdcp->mdev.minor = MISC_DYNAMIC_MINOR;
	hdcp->mdev.name = "hdmirx_hdcp1x";
	hdcp->mdev.mode = 0666;

	if (misc_register(&hdcp->mdev)) {
		dev_err(hdcp->dev, "HDCP: Could not add character driver\n");
		return NULL;
	}

	ret = device_create_file(hdcp->mdev.this_device, &dev_attr_enable);
	if (ret) {
		dev_err(hdcp->dev, "HDCP: Could not add sys file enable\n");
		goto error0;
	}

	ret = device_create_file(hdcp->mdev.this_device, &dev_attr_status);
	if (ret) {
		dev_err(hdcp->dev, "HDCP: Could not add sys file status\n");
		goto error1;
	}

	hdcp->hdcp_start = rk_hdmirx_hdcp_start;
	hdcp->hdcp_stop = rk_hdmirx_hdcp_stop;
	dev_info(hdcp->dev, "%s success\n", __func__);
	return hdcp;

error1:
	device_remove_file(hdcp->mdev.this_device, &dev_attr_enable);
error0:
	misc_deregister(&hdcp->mdev);
	return NULL;
}

void rk_hdmirx_hdcp_unregister(struct rk_hdmirx_hdcp *hdcp)
{
	device_remove_file(hdcp->mdev.this_device, &dev_attr_enable);
	device_remove_file(hdcp->mdev.this_device, &dev_attr_status);
	misc_deregister(&hdcp->mdev);
}
