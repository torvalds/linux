// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author:
 *      Algea Cao <algea.cao@rock-chips.com>
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hdmi.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/spinlock.h>
#include <linux/soc/rockchip/rk_vendor_storage.h>
#include <crypto/sha.h>
#include <drm/bridge/dw_hdmi.h>

#include "dw-hdmi-qp.h"
#include "dw-hdmi-qp-hdcp.h"

#define HDCP_KEY_SIZE		308
#define HDCP_KEY_SEED_SIZE	2

#define KSV_LEN			5
#define HEADER			10
#define SHAMAX			20

#define MAX_DOWNSTREAM_DEVICE_NUM	5
#define DPK_WR_OK_TIMEOUT_US		30000
#define HDMI_HDCP1X_ID			5

/* HDCP Registers */
#define HDMI_HDCPREG_RMCTL	0x780e
#define HDMI_HDCPREG_RMSTS	0x780f
#define HDMI_HDCPREG_SEED0	0x7810
#define HDMI_HDCPREG_SEED1	0x7811
#define HDMI_HDCPREG_DPK0	0x7812
#define HDMI_HDCPREG_DPK1	0x7813
#define HDMI_HDCPREG_DPK2	0x7814
#define HDMI_HDCPREG_DPK3	0x7815
#define HDMI_HDCPREG_DPK4	0x7816
#define HDMI_HDCPREG_DPK5	0x7817
#define HDMI_HDCPREG_DPK6	0x7818
#define HDMI_HDCP2REG_CTRL	0x7904
#define HDMI_HDCP2REG_MASK	0x790c
#define HDMI_HDCP2REG_MUTE	0x790e

enum dw_hdmi_hdcp_state {
	DW_HDCP_DISABLED,
	DW_HDCP_AUTH_START,
	DW_HDCP_AUTH_SUCCESS,
	DW_HDCP_AUTH_FAIL,
};

enum {
	DW_HDMI_HDCP_KSV_LEN = 8,
	DW_HDMI_HDCP_SHA_LEN = 20,
	DW_HDMI_HDCP_DPK_LEN = 280,
	DW_HDMI_HDCP_KEY_LEN = 308,
	DW_HDMI_HDCP_SEED_LEN = 2,
};

enum {
	HDCP14_R0_TIMER_OVR_EN_MASK = 0x01,
	HDCP14_R0_TIMER_OVR_EN = 0x01,
	HDCP14_R0_TIMER_OVR_DISABLE = 0x00,

	HDCP14_RI_TIMER_OVR_EN_MASK = 0x80,
	HDCP14_RI_TIMER_OVR_EN = 0x80,
	HDCP14_RI_TIMER_OVR_DISABLE = 0x00,

	HDCP14_R0_TIMER_OVR_VALUE_MASK = 0x1e,
	HDCP14_RI_TIMER_OVR_VALUE_MASK = 0xff00,

	HDCP14_KEY_WR_OK = 0x100,

	HDCP14_HPD_MASK = 0x01,
	HDCP14_HPD_EN = 0x01,
	HDCP14_HPD_DISABLE = 0x00,

	HDCP14_ENCRYPTION_ENABLE_MASK = 0x04,
	HDCP14_ENCRYPTION_ENABLE = 0x04,
	HDCP14_ENCRYPTION_DISABLE = 0x04,

	HDCP14_KEY_DECRYPT_EN_MASK = 0x400,
	HDCP14_KEY_DECRYPT_EN = 0x400,
	HDCP14_KEY_DECRYPT_DISABLE = 0x00,

	HDMI_A_SRMCTRL_SHA1_FAIL_MASK = 0X08,
	HDMI_A_SRMCTRL_SHA1_FAIL_DISABLE = 0X00,
	HDMI_A_SRMCTRL_SHA1_FAIL_ENABLE = 0X08,

	HDMI_A_SRMCTRL_KSV_UPDATE_MASK = 0X04,
	HDMI_A_SRMCTRL_KSV_UPDATE_DISABLE = 0X00,
	HDMI_A_SRMCTRL_KSV_UPDATE_ENABLE = 0X04,

	HDMI_A_SRMCTRL_KSV_MEM_REQ_MASK = 0X01,
	HDMI_A_SRMCTRL_KSV_MEM_REQ_DISABLE = 0X00,
	HDMI_A_SRMCTRL_KSV_MEM_REQ_ENABLE = 0X01,

	HDMI_A_SRMCTRL_KSV_MEM_ACCESS_MASK = 0X02,
	HDMI_A_SRMCTRL_KSV_MEM_ACCESS_DISABLE = 0X00,
	HDMI_A_SRMCTRL_KSV_MEM_ACCESS_ENABLE = 0X02,

	HDMI_A_SRM_BASE_MAX_DEVS_EXCEEDED = 0x80,
	HDMI_A_SRM_BASE_DEVICE_COUNT = 0x7f,

	HDMI_A_SRM_BASE_MAX_CASCADE_EXCEEDED = 0x08,

	HDMI_A_APIINTSTAT_KSVSHA1_CALC_INT = 0x02,

	/* HDCPREG_RMSTS field values */
	DPK_WR_OK_STS = 0x40,

	HDMI_A_HDCP22_MASK = 0x40,

	HDMI_HDCP2_OVR_EN_MASK = 0x02,
	HDMI_HDCP2_OVR_ENABLE = 0x02,
	HDMI_HDCP2_OVR_DISABLE = 0x00,

	HDMI_HDCP2_FORCE_MASK = 0x04,
	HDMI_HDCP2_FORCE_ENABLE = 0x04,
	HDMI_HDCP2_FORCE_DISABLE = 0x00,
};

struct sha_t {
	u8 mlength[8];
	u8 mblock[64];
	int mindex;
	int mcomputed;
	int mcorrupted;
	unsigned int mdigest[5];
};

static inline unsigned int shacircularshift(unsigned int bits,
					    unsigned int word)
{
	return (((word << bits) & 0xFFFFFFFF) | (word >> (32 - bits)));
}

static void hdcp_modb(struct dw_qp_hdcp *hdcp, u32 data, u32 mask, u32 reg)
{
	struct dw_hdmi_qp *hdmi = hdcp->hdmi;
	u32 val = hdcp->read(hdmi, reg) & ~mask;

	val |= data & mask;
	hdcp->write(hdmi, val, reg);
}

static int hdcp_load_keys_cb(struct dw_qp_hdcp *hdcp)
{
	u32 size;
	u8 hdcp_vendor_data[320];

	hdcp->keys = kmalloc(HDCP_KEY_SIZE, GFP_KERNEL);
	if (!hdcp->keys)
		return -ENOMEM;

	hdcp->seeds = kmalloc(HDCP_KEY_SEED_SIZE, GFP_KERNEL);
	if (!hdcp->seeds) {
		kfree(hdcp->keys);
		return -ENOMEM;
	}

	size = rk_vendor_read(HDMI_HDCP1X_ID, hdcp_vendor_data, 314);
	if (size < (HDCP_KEY_SIZE + HDCP_KEY_SEED_SIZE)) {
		dev_err(hdcp->dev, "HDCP: read size %d\n", size);
		memset(hdcp->keys, 0, HDCP_KEY_SIZE);
		memset(hdcp->seeds, 0, HDCP_KEY_SEED_SIZE);
	} else {
		memcpy(hdcp->keys, hdcp_vendor_data, HDCP_KEY_SIZE);
		memcpy(hdcp->seeds, hdcp_vendor_data + HDCP_KEY_SIZE,
		       HDCP_KEY_SEED_SIZE);
	}

	return 0;
}

static int dw_hdcp_qp_hdcp_load_key(struct dw_qp_hdcp *hdcp)
{
	int i, j;
	int ret, val;
	void __iomem *reg_rmsts_addr;
	struct dw_hdmi_qp_hdcp_keys *hdcp_keys;
	struct dw_hdmi_qp *hdmi = hdcp->hdmi;
	u32 ksv, dkl, dkh;

	if (!hdcp->keys) {
		ret = hdcp_load_keys_cb(hdcp);
		if (ret)
			return ret;
	}
	hdcp_keys = hdcp->keys;

	reg_rmsts_addr = hdcp->regs + HDCP14_KEY_STATUS;

	/* hdcp key has been written */
	if (hdcp->read(hdmi, HDCP14_KEY_STATUS) & 0x3f) {
		dev_info(hdcp->dev, "hdcp key has been written\n");
		return 0;
	}

	ksv = hdcp_keys->KSV[0] | hdcp_keys->KSV[1] << 8 |
		hdcp_keys->KSV[2] << 16 | hdcp_keys->KSV[3] << 24;
	hdcp->write(hdmi, ksv, HDCP14_AKSV_L);

	ksv = hdcp_keys->KSV[4];
	hdcp->write(hdmi, ksv, HDCP14_AKSV_H);

	if (hdcp->seeds) {
		hdcp_modb(hdcp, HDCP14_KEY_DECRYPT_EN,
			  HDCP14_KEY_DECRYPT_EN_MASK,
			  HDCP14_CONFIG0);
		hdcp->write(hdmi, (hdcp->seeds[0] << 8) | hdcp->seeds[1],
			    HDCP14_KEY_SEED);
	} else {
		hdcp_modb(hdcp, HDCP14_KEY_DECRYPT_DISABLE,
			  HDCP14_KEY_DECRYPT_EN_MASK,
			  HDCP14_CONFIG0);
	}

	for (i = 0; i < DW_HDMI_HDCP_DPK_LEN - 6; i += 7) {
		dkl = 0;
		dkh = 0;
		for (j = 0; j < 4; j++)
			dkl |= hdcp_keys->devicekey[i + j] << (j * 8);
		for (j = 4; j < 7; j++)
			dkh |= hdcp_keys->devicekey[i + j] << ((j - 4) * 8);

		hdcp->write(hdmi, dkh, HDCP14_KEY_H);
		hdcp->write(hdmi, dkl, HDCP14_KEY_L);

		ret = readx_poll_timeout(readl, reg_rmsts_addr, val,
					 val & HDCP14_KEY_WR_OK, 1000,
					 DPK_WR_OK_TIMEOUT_US);
		if (ret) {
			dev_err(hdcp->dev, "hdcp key write err\n");
			return ret;
		}
	}

	return 0;
}

static void dw_hdcp_qp_hdcp_restart(struct dw_qp_hdcp *hdcp)
{
	mutex_lock(&hdcp->mutex);

	if (!hdcp->remaining_times) {
		mutex_unlock(&hdcp->mutex);
		return;
	}

	hdcp_modb(hdcp, 0, HDCP14_ENCRYPTION_ENABLE_MASK | HDCP14_HPD_MASK,
		   HDCP14_CONFIG0);

	hdcp->write(hdcp->hdmi, 1, HDCP14_CONFIG1);
	mdelay(50);
	hdcp->write(hdcp->hdmi, HDCP14_AUTH_CHG_MASK_N | HDCP14_KSV_LIST_DONE_MASK_N,
		    AVP_1_INT_CLEAR);
	hdcp_modb(hdcp, HDCP14_AUTH_CHG_MASK_N | HDCP14_KSV_LIST_DONE_MASK_N,
		  HDCP14_AUTH_CHG_MASK_N | HDCP14_KSV_LIST_DONE_MASK_N, AVP_1_INT_MASK_N);

	hdcp_modb(hdcp, HDCP14_ENCRYPTION_ENABLE_MASK | HDCP14_HPD_MASK,
		  HDCP14_ENCRYPTION_ENABLE_MASK | HDCP14_HPD_MASK,
		   HDCP14_CONFIG0);

	hdcp->remaining_times--;
	mutex_unlock(&hdcp->mutex);
}

static int dw_hdcp_qp_hdcp_start(struct dw_qp_hdcp *hdcp)
{
	struct dw_hdmi_qp *hdmi = hdcp->hdmi;

	dw_hdcp_qp_hdcp_load_key(hdcp);

	mutex_lock(&hdcp->mutex);
	hdcp->remaining_times = hdcp->retry_times;

	hdcp->write(hdmi, HDCP14_AUTH_CHG_MASK_N | HDCP14_KSV_LIST_DONE_MASK_N, AVP_1_INT_CLEAR);
	hdcp_modb(hdcp, HDCP14_AUTH_CHG_MASK_N | HDCP14_KSV_LIST_DONE_MASK_N,
		  HDCP14_AUTH_CHG_MASK_N | HDCP14_KSV_LIST_DONE_MASK_N, AVP_1_INT_MASK_N);

	mdelay(50);

	hdcp_modb(hdcp, HDCP14_ENCRYPTION_ENABLE | HDCP14_HPD_EN,
		  HDCP14_ENCRYPTION_ENABLE_MASK | HDCP14_HPD_MASK,
		  HDCP14_CONFIG0);

	hdcp->status = DW_HDCP_AUTH_START;
	dev_info(hdcp->dev, "start hdcp\n");
	mutex_unlock(&hdcp->mutex);

	queue_work(hdcp->workqueue, &hdcp->work);
	return 0;
}

static int dw_hdcp_qp_hdcp_stop(struct dw_qp_hdcp *hdcp)
{
	mutex_lock(&hdcp->mutex);
	hdcp_modb(hdcp, 0, HDCP14_ENCRYPTION_ENABLE_MASK | HDCP14_HPD_MASK,
		  HDCP14_CONFIG0);

	hdcp_modb(hdcp, 0, HDCP14_AUTH_CHG_MASK_N | HDCP14_KSV_LIST_DONE_MASK_N, AVP_1_INT_MASK_N);
	hdcp->write(hdcp->hdmi, 0, HDCP14_CONFIG1);
	hdcp->status = DW_HDCP_DISABLED;
	mutex_unlock(&hdcp->mutex);
	return 0;
}

static void sha_reset(struct sha_t *sha)
{
	u32 i = 0;

	sha->mindex = 0;
	sha->mcomputed = false;
	sha->mcorrupted = false;
	for (i = 0; i < sizeof(sha->mlength); i++)
		sha->mlength[i] = 0;

	sha1_init(sha->mdigest);
}

static void sha_processblock(struct sha_t *sha)
{
	u32 array[SHA1_WORKSPACE_WORDS];

	sha1_transform(sha->mdigest, sha->mblock, array);
	sha->mindex = 0;
}

static void sha_padmessage(struct sha_t *sha)
{
	/*
	 *  Check to see if the current message block is too small to hold
	 *  the initial padding bits and length.  If so, we will pad the
	 *  block, process it, and then continue padding into a second
	 *  block.
	 */
	if (sha->mindex > 55) {
		sha->mblock[sha->mindex++] = 0x80;
		while (sha->mindex < 64)
			sha->mblock[sha->mindex++] = 0;

		sha_processblock(sha);
		while (sha->mindex < 56)
			sha->mblock[sha->mindex++] = 0;
	} else {
		sha->mblock[sha->mindex++] = 0x80;
		while (sha->mindex < 56)
			sha->mblock[sha->mindex++] = 0;
	}

	/* Store the message length as the last 8 octets */
	sha->mblock[56] = sha->mlength[7];
	sha->mblock[57] = sha->mlength[6];
	sha->mblock[58] = sha->mlength[5];
	sha->mblock[59] = sha->mlength[4];
	sha->mblock[60] = sha->mlength[3];
	sha->mblock[61] = sha->mlength[2];
	sha->mblock[62] = sha->mlength[1];
	sha->mblock[63] = sha->mlength[0];

	sha_processblock(sha);
}

static int sha_result(struct sha_t *sha)
{
	if (sha->mcorrupted)
		return false;

	if (sha->mcomputed == 0) {
		sha_padmessage(sha);
		sha->mcomputed = true;
	}
	return true;
}

static void sha_input(struct sha_t *sha, const u8 *data, u32 size)
{
	int i = 0;
	unsigned int j = 0;
	int rc = true;

	if (data == 0 || size == 0)
		return;

	if (sha->mcomputed || sha->mcorrupted) {
		sha->mcorrupted = true;
		return;
	}
	while (size-- && !sha->mcorrupted) {
		sha->mblock[sha->mindex++] = *data;

		for (i = 0; i < 8; i++) {
			rc = true;
			for (j = 0; j < sizeof(sha->mlength); j++) {
				sha->mlength[j]++;
				if (sha->mlength[j] != 0) {
					rc = false;
					break;
				}
			}
			sha->mcorrupted = (sha->mcorrupted  ||
					   rc) ? true : false;
		}
		/* if corrupted then message is too long */
		if (sha->mindex == 64)
			sha_processblock(sha);
		data++;
	}
}

static int hdcp_verify_ksv(const u8 *data, u32 size)
{
	u32 i = 0;
	struct sha_t sha;

	if ((!data) || (size < (HEADER + SHAMAX)))
		return false;

	sha_reset(&sha);
	sha_input(&sha, data, size - SHAMAX);
	if (sha_result(&sha) == false)
		return false;

	for (i = 0; i < SHAMAX; i++) {
		if (data[size - SHAMAX + i] != (u8)(sha.mdigest[i / 4] >> ((i % 4) * 8)))
			return false;
	}
	return true;
}

static void dw_hdcp_qp_hdcp_2nd_auth(struct dw_qp_hdcp *hdcp)
{
	u8 *data;
	u32 len;

	len = (hdcp->read(hdcp->hdmi, HDCP14_STATUS0) & HDCP14_RPT_DEVICE_COUNT) >> 9;
	len = len * KSV_LEN + BSTATUS_LEN + M0_LEN + SHAMAX;

	data = kmalloc(len, GFP_KERNEL);
	if (!data)
		return;

	hdcp->get_mem(hdcp->hdmi, data, len);

	if (hdcp_verify_ksv(data, len))
		hdcp->write(hdcp->hdmi, HDCP14_SHA1_MSG_CORRECT_P, HDCP14_CONFIG1);
	else
		dw_hdcp_qp_hdcp_restart(hdcp);
}

static void dw_hdcp_qp_hdcp_auth(struct dw_qp_hdcp *hdcp, u32 hdcp_status)
{
	if (!(hdcp_status & BIT(2))) {
		mutex_lock(&hdcp->mutex);
		if (hdcp->status == DW_HDCP_DISABLED) {
			mutex_unlock(&hdcp->mutex);
			return;
		}
		dev_err(hdcp->dev, "hdcp auth failed\n");
		hdcp_modb(hdcp, 0, HDCP14_ENCRYPTION_ENABLE_MASK | HDCP14_HPD_MASK,
			  HDCP14_CONFIG0);
		hdcp->status = DW_HDCP_AUTH_FAIL;
		mutex_unlock(&hdcp->mutex);

		dw_hdcp_qp_hdcp_restart(hdcp);
	} else {
		mutex_lock(&hdcp->mutex);
		dev_info(hdcp->dev, "hdcp auth success\n");
		hdcp->status = DW_HDCP_AUTH_SUCCESS;
		mutex_unlock(&hdcp->mutex);
	}
}

static void dw_hdcp_qp_hdcp_isr(struct dw_qp_hdcp *hdcp, u32 avp_int, u32 hdcp_status)
{
	if (hdcp->status == DW_HDCP_DISABLED)
		return;

	dev_info(hdcp->dev, "hdcp_int is 0x%02x\n", hdcp_status);

	if (avp_int & HDCP14_KSV_LIST_DONE_MASK_N)
		dw_hdcp_qp_hdcp_2nd_auth(hdcp);

	if (avp_int & HDCP14_AUTH_CHG_MASK_N)
		dw_hdcp_qp_hdcp_auth(hdcp, hdcp_status);
}

static ssize_t trytimes_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	int trytimes = 0;
	struct dw_qp_hdcp *hdcp = dev_get_drvdata(device);

	if (hdcp)
		trytimes = hdcp->retry_times;

	return snprintf(buf, PAGE_SIZE, "%d\n", trytimes);
}

static ssize_t trytimes_store(struct device *device,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	int trytimes;
	struct dw_qp_hdcp *hdcp = dev_get_drvdata(device);

	if (!hdcp)
		return -EINVAL;

	if (kstrtoint(buf, 0, &trytimes))
		return -EINVAL;

	if (hdcp->retry_times != trytimes) {
		hdcp->retry_times = trytimes;
		hdcp->remaining_times = hdcp->retry_times;
	}

	return count;
}

static DEVICE_ATTR_RW(trytimes);

static ssize_t status_show(struct device *device,
			   struct device_attribute *attr, char *buf)
{
	int status = DW_HDCP_DISABLED;
	struct dw_qp_hdcp *hdcp = dev_get_drvdata(device);

	if (hdcp)
		status = hdcp->status;

	if (status == DW_HDCP_DISABLED)
		return snprintf(buf, PAGE_SIZE, "hdcp disable\n");
	else if (status == DW_HDCP_AUTH_START)
		return snprintf(buf, PAGE_SIZE, "hdcp_auth_start\n");
	else if (status == DW_HDCP_AUTH_SUCCESS)
		return snprintf(buf, PAGE_SIZE, "hdcp_auth_success\n");
	else if (status == DW_HDCP_AUTH_FAIL)
		return snprintf(buf, PAGE_SIZE, "hdcp_auth_fail\n");
	else
		return snprintf(buf, PAGE_SIZE, "unknown status\n");
}

static DEVICE_ATTR_RO(status);

static struct attribute *dw_hdmi_qp_hdcp_attrs[] = {
	&dev_attr_trytimes.attr,
	&dev_attr_status.attr,
	NULL
};
ATTRIBUTE_GROUPS(dw_hdmi_qp_hdcp);

/* If sink is a repeater, we need to wait ksv list ready */
static void dw_hdmi_qp_hdcp(struct work_struct *p_work)
{
	struct dw_qp_hdcp *hdcp = container_of(p_work, struct dw_qp_hdcp, work);
	u32 val;
	int i = 500;

	while (i--) {
		usleep_range(7000, 8000);

		mutex_lock(&hdcp->mutex);
		if (hdcp->status == DW_HDCP_DISABLED) {
			dev_dbg(hdcp->dev, "hdcp is disabled, don't wait repeater ready\n");
			mutex_unlock(&hdcp->mutex);
			return;
		}

		val = hdcp->read(hdcp->hdmi, HDCP14_STATUS1);

		/* sink isn't repeater or ksv fifo ready, stop waiting */
		if (!(val & HDCP14_RCV_REPEATER) || (val & HDCP14_RCV_KSV_FIFO_READY)) {
			dev_dbg(hdcp->dev, "wait ksv fifo finished\n");
			mutex_unlock(&hdcp->mutex);
			return;
		}

		mutex_unlock(&hdcp->mutex);
	}

	if (i < 0) {
		dev_err(hdcp->dev, "wait repeater ready time out\n");
		dw_hdcp_qp_hdcp_restart(hdcp);
	}
}

static int dw_hdcp_qp_hdcp_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct dw_qp_hdcp *hdcp = pdev->dev.platform_data;

	/* retry time if hdcp auth fail. unlimited time if set 0 */
	hdcp->dev = &pdev->dev;
	hdcp->hdcp_start = dw_hdcp_qp_hdcp_start;
	hdcp->hdcp_stop = dw_hdcp_qp_hdcp_stop;
	hdcp->hdcp_isr = dw_hdcp_qp_hdcp_isr;

	ret = device_add_groups(hdcp->dev, dw_hdmi_qp_hdcp_groups);
	if (ret) {
		dev_err(hdcp->dev, "Failed to add sysfs files group\n");
		return ret;
	}

	platform_set_drvdata(pdev, hdcp);

	hdcp->workqueue = create_workqueue("hdcp_queue");
	INIT_WORK(&hdcp->work, dw_hdmi_qp_hdcp);

	hdcp->retry_times = 3;
	mutex_init(&hdcp->mutex);

	dev_info(hdcp->dev, "%s success\n", __func__);
	return 0;
}

static int dw_hdcp_qp_hdcp_remove(struct platform_device *pdev)
{
	struct dw_qp_hdcp *hdcp = pdev->dev.platform_data;

	cancel_work_sync(&hdcp->work);
	flush_workqueue(hdcp->workqueue);
	destroy_workqueue(hdcp->workqueue);

	device_remove_groups(hdcp->dev, dw_hdmi_qp_hdcp_groups);
	kfree(hdcp->keys);
	kfree(hdcp->seeds);

	return 0;
}

static struct platform_driver dw_hdcp_qp_hdcp_driver = {
	.probe  = dw_hdcp_qp_hdcp_probe,
	.remove = dw_hdcp_qp_hdcp_remove,
	.driver = {
		.name = DW_HDCP_QP_DRIVER_NAME,
	},
};

module_platform_driver(dw_hdcp_qp_hdcp_driver);
MODULE_DESCRIPTION("DW HDMI QP transmitter HDCP driver");
MODULE_LICENSE("GPL");
