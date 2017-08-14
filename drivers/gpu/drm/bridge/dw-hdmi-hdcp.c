/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author Huicong Xu <xhc@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/cryptohash.h>
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
#include <drm/bridge/dw_hdmi.h>

#include "dw-hdmi.h"
#include "dw-hdmi-hdcp.h"

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
	HDMI_MC_CLKDIS_HDCPCLK_MASK = 0x40,
	HDMI_MC_CLKDIS_HDCPCLK_ENABLE = 0x00,

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

static struct dw_hdcp *g_hdcp;

static inline unsigned int shacircularshift(unsigned int bits,
					    unsigned int word)
{
	return (((word << bits) & 0xFFFFFFFF) | (word >> (32 - bits)));
}

static void hdcp_modb(struct dw_hdcp *hdcp, u8 data, u8 mask, unsigned int reg)
{
	struct dw_hdmi *hdmi = hdcp->hdmi;
	u8 val = hdcp->read(hdmi, reg) & ~mask;

	val |= data & mask;
	hdcp->write(hdmi, val, reg);
}

static void sha_reset(struct sha_t *sha)
{
	u32 i = 0;

	sha->mindex = 0;
	sha->mcomputed = false;
	sha->mcorrupted = false;
	for (i = 0; i < sizeof(sha->mlength); i++)
		sha->mlength[i] = 0;

	sha_init(sha->mdigest);
}

static void sha_processblock(struct sha_t *sha)
{
	u32 array[SHA_WORKSPACE_WORDS];

	sha_transform(sha->mdigest, sha->mblock, array);
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
		if (data[size - SHAMAX + i] != (u8)(sha.mdigest[i / 4]
				>> ((i % 4) * 8)))
			return false;
	}
	return true;
}

static int hdcp_load_keys_cb(struct dw_hdcp *hdcp)
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
		dev_dbg(hdcp->dev, "HDCP: read size %d\n", size);
		memset(hdcp->keys, 0, HDCP_KEY_SIZE);
		memset(hdcp->seeds, 0, HDCP_KEY_SEED_SIZE);
	} else {
		memcpy(hdcp->keys, hdcp_vendor_data, HDCP_KEY_SIZE);
		memcpy(hdcp->seeds, hdcp_vendor_data + HDCP_KEY_SIZE,
		       HDCP_KEY_SEED_SIZE);
	}
	return 0;
}

static int dw_hdmi_hdcp_load_key(struct dw_hdcp *hdcp)
{
	int i, j;
	int ret, val;
	void __iomem *reg_rmsts_addr;
	struct hdcp_keys *hdcp_keys;
	struct dw_hdmi *hdmi = hdcp->hdmi;

	if (!hdcp->keys) {
		ret = hdcp_load_keys_cb(hdcp);
		if (ret)
			return ret;
	}
	hdcp_keys = hdcp->keys;

	if (hdcp->reg_io_width == 4)
		reg_rmsts_addr = hdcp->regs + (HDMI_HDCPREG_RMSTS << 2);
	else if (hdcp->reg_io_width == 1)
		reg_rmsts_addr = hdcp->regs + HDMI_HDCPREG_RMSTS;
	else
		return -EPERM;

	/* Disable decryption logic */
	hdcp->write(hdmi, 0, HDMI_HDCPREG_RMCTL);
	ret = readx_poll_timeout(readl, reg_rmsts_addr, val,
				 val & DPK_WR_OK_STS, 1000,
				 DPK_WR_OK_TIMEOUT_US);
	if (ret)
		return ret;
	hdcp->write(hdmi, 0, HDMI_HDCPREG_DPK6);
	hdcp->write(hdmi, 0, HDMI_HDCPREG_DPK5);

	/* The useful data in ksv should be 5 byte */
	for (i = 4; i >= 0; i--)
		hdcp->write(hdmi, hdcp_keys->KSV[i], HDMI_HDCPREG_DPK0 + i);
	ret = readx_poll_timeout(readl, reg_rmsts_addr, val,
				 val & DPK_WR_OK_STS, 1000,
				 DPK_WR_OK_TIMEOUT_US);

	if (ret)
		return ret;

	/* Enable decryption logic */
	if (hdcp->seeds) {
		hdcp->write(hdmi, 1, HDMI_HDCPREG_RMCTL);
		hdcp->write(hdmi, hdcp->seeds[0], HDMI_HDCPREG_SEED1);
		hdcp->write(hdmi, hdcp->seeds[1], HDMI_HDCPREG_SEED0);
	} else {
		hdcp->write(hdmi, 0, HDMI_HDCPREG_RMCTL);
	}

	/* Write encrypt device private key */
	for (i = 0; i < DW_HDMI_HDCP_DPK_LEN - 6; i += 7) {
		for (j = 6; j >= 0; j--)
			hdcp->write(hdmi, hdcp_keys->devicekey[i + j],
				    HDMI_HDCPREG_DPK0 + j);
		ret = readx_poll_timeout(readl, reg_rmsts_addr, val,
					 val & DPK_WR_OK_STS, 1000,
					 DPK_WR_OK_TIMEOUT_US);

		if (ret)
			return ret;
	}
	return 0;
}

static int dw_hdmi_hdcp_start(struct dw_hdcp *hdcp)
{
	struct dw_hdmi *hdmi = hdcp->hdmi;

	if (!hdcp->enable)
		return -EPERM;

	if (!(hdcp->read(hdmi, HDMI_HDCPREG_RMSTS) & 0x3f))
		dw_hdmi_hdcp_load_key(hdcp);

	hdcp_modb(hdcp, HDMI_FC_INVIDCONF_HDCP_KEEPOUT_ACTIVE,
		  HDMI_FC_INVIDCONF_HDCP_KEEPOUT_MASK,
		  HDMI_FC_INVIDCONF);

	hdcp->remaining_times = hdcp->retry_times;
	if (hdcp->read(hdmi, HDMI_CONFIG1_ID) & HDMI_A_HDCP22_MASK) {
		if (hdcp->hdcp2_enable == 0) {
			hdcp_modb(hdcp, HDMI_HDCP2_OVR_ENABLE |
				  HDMI_HDCP2_FORCE_DISABLE,
				  HDMI_HDCP2_OVR_EN_MASK |
				  HDMI_HDCP2_FORCE_MASK,
				  HDMI_HDCP2REG_CTRL);
			hdcp->write(hdmi, 0xff, HDMI_HDCP2REG_MASK);
			hdcp->write(hdmi, 0xff, HDMI_HDCP2REG_MUTE);
		} else {
			hdcp_modb(hdcp, HDMI_HDCP2_OVR_DISABLE |
				  HDMI_HDCP2_FORCE_DISABLE,
				  HDMI_HDCP2_OVR_EN_MASK |
				  HDMI_HDCP2_FORCE_MASK,
				  HDMI_HDCP2REG_CTRL);
			hdcp->write(hdmi, 0x00, HDMI_HDCP2REG_MASK);
			hdcp->write(hdmi, 0x00, HDMI_HDCP2REG_MUTE);
		}
	}

	hdcp->write(hdmi, 0x40, HDMI_A_OESSWCFG);
		    hdcp_modb(hdcp, HDMI_A_HDCPCFG0_BYPENCRYPTION_DISABLE |
		    HDMI_A_HDCPCFG0_EN11FEATURE_DISABLE |
		    HDMI_A_HDCPCFG0_SYNCRICHECK_ENABLE,
		    HDMI_A_HDCPCFG0_BYPENCRYPTION_MASK |
		    HDMI_A_HDCPCFG0_EN11FEATURE_MASK |
		    HDMI_A_HDCPCFG0_SYNCRICHECK_MASK, HDMI_A_HDCPCFG0);

	hdcp_modb(hdcp, HDMI_A_HDCPCFG1_ENCRYPTIONDISABLE_ENABLE |
		  HDMI_A_HDCPCFG1_PH2UPSHFTENC_ENABLE,
		  HDMI_A_HDCPCFG1_ENCRYPTIONDISABLE_MASK |
		  HDMI_A_HDCPCFG1_PH2UPSHFTENC_MASK, HDMI_A_HDCPCFG1);

	/* Reset HDCP Engine */
	if (hdcp->read(hdmi, HDMI_MC_CLKDIS) & HDMI_MC_CLKDIS_HDCPCLK_MASK) {
		hdcp_modb(hdcp, HDMI_A_HDCPCFG1_SWRESET_ASSERT,
			  HDMI_A_HDCPCFG1_SWRESET_MASK, HDMI_A_HDCPCFG1);
	}

	hdcp->write(hdmi, 0x00, HDMI_A_APIINTMSK);
	hdcp_modb(hdcp, HDMI_A_HDCPCFG0_RXDETECT_ENABLE,
		  HDMI_A_HDCPCFG0_RXDETECT_MASK, HDMI_A_HDCPCFG0);

	/*
	 * XXX: to sleep 100ms here between output hdmi and enable hdcpclk,
	 * otherwise hdcp auth fail when Connect to repeater
	 */
	msleep(100);
	hdcp_modb(hdcp, HDMI_MC_CLKDIS_HDCPCLK_ENABLE,
		  HDMI_MC_CLKDIS_HDCPCLK_MASK, HDMI_MC_CLKDIS);

	hdcp->status = DW_HDCP_AUTH_START;
	dev_dbg(hdcp->dev, "%s success\n", __func__);
	return 0;
}

static int dw_hdmi_hdcp_stop(struct dw_hdcp *hdcp)
{
	struct dw_hdmi *hdmi = hdcp->hdmi;

	if (!hdcp->enable)
		return -EPERM;

	hdcp_modb(hdcp, HDMI_MC_CLKDIS_HDCPCLK_DISABLE,
		  HDMI_MC_CLKDIS_HDCPCLK_MASK, HDMI_MC_CLKDIS);
	hdcp->write(hdmi, 0xff, HDMI_A_APIINTMSK);

	hdcp_modb(hdcp, HDMI_A_HDCPCFG0_RXDETECT_DISABLE,
		  HDMI_A_HDCPCFG0_RXDETECT_MASK, HDMI_A_HDCPCFG0);

	hdcp_modb(hdcp, HDMI_A_SRMCTRL_SHA1_FAIL_DISABLE |
		  HDMI_A_SRMCTRL_KSV_UPDATE_DISABLE,
		  HDMI_A_SRMCTRL_SHA1_FAIL_MASK |
		  HDMI_A_SRMCTRL_KSV_UPDATE_MASK, HDMI_A_SRMCTRL);

	hdcp->status = DW_HDCP_DISABLED;
	return 0;
}

static int dw_hdmi_hdcp_ksvsha1(struct dw_hdcp *hdcp)
{
	int rc = 0, value, list, i;
	char bstaus0, bstaus1;
	char *ksvlistbuf;
	struct dw_hdmi *hdmi = hdcp->hdmi;

	hdcp_modb(hdcp, HDMI_A_SRMCTRL_KSV_MEM_REQ_ENABLE,
		  HDMI_A_SRMCTRL_KSV_MEM_REQ_MASK, HDMI_A_SRMCTRL);

	list = 20;
	do {
		value = hdcp->read(hdmi, HDMI_A_SRMCTRL);
		usleep_range(500, 1000);
	} while ((value & HDMI_A_SRMCTRL_KSV_MEM_ACCESS_MASK) == 0 && --list);

	if ((value & HDMI_A_SRMCTRL_KSV_MEM_ACCESS_MASK) == 0) {
		dev_err(hdcp->dev, "KSV memory can not access\n");
		rc = -EPERM;
		goto out;
	}

	hdcp->read(hdmi, HDMI_A_SRM_BASE);
	bstaus0 = hdcp->read(hdmi, HDMI_A_SRM_BASE + 1);
	bstaus1 = hdcp->read(hdmi, HDMI_A_SRM_BASE + 2);

	if (bstaus0 & HDMI_A_SRM_BASE_MAX_DEVS_EXCEEDED) {
		dev_err(hdcp->dev, "MAX_DEVS_EXCEEDED\n");
		rc = -EPERM;
		goto out;
	}

	list = bstaus0 & HDMI_A_SRM_BASE_DEVICE_COUNT;
	if (list > MAX_DOWNSTREAM_DEVICE_NUM) {
		dev_err(hdcp->dev, "MAX_DOWNSTREAM_DEVICE_NUM\n");
		rc = -EPERM;
		goto out;
	}
	if (bstaus1 & HDMI_A_SRM_BASE_MAX_CASCADE_EXCEEDED) {
		dev_err(hdcp->dev, "MAX_CASCADE_EXCEEDED\n");
		rc = -EPERM;
		goto out;
	}

	value = (list * KSV_LEN) + HEADER + SHAMAX;
	ksvlistbuf = kmalloc(value, GFP_KERNEL);
	if (!ksvlistbuf) {
		rc = -ENOMEM;
		goto out;
	}

	ksvlistbuf[(list * KSV_LEN)] = bstaus0;
	ksvlistbuf[(list * KSV_LEN) + 1] = bstaus1;
	for (i = 2; i < value; i++) {
		if (i < HEADER)	/* BSTATUS & M0 */
			ksvlistbuf[(list * KSV_LEN) + i] =
				hdcp->read(hdmi, HDMI_A_SRM_BASE + i + 1);
		else if (i < (HEADER + (list * KSV_LEN))) /* KSV list */
			ksvlistbuf[i - HEADER] =
				hdcp->read(hdmi, HDMI_A_SRM_BASE + i + 1);
		else /* SHA */
			ksvlistbuf[i] =
				hdcp->read(hdmi, HDMI_A_SRM_BASE + i + 1);
	}
	if (hdcp_verify_ksv(ksvlistbuf, value) == true) {
		rc = 0;
		dev_dbg(hdcp->dev, "ksv check valid\n");
	} else {
		dev_err(hdcp->dev, "ksv check invalid\n");
		rc = -1;
	}
	kfree(ksvlistbuf);
out:
	hdcp_modb(hdcp, HDMI_A_SRMCTRL_KSV_MEM_REQ_DISABLE,
		  HDMI_A_SRMCTRL_KSV_MEM_REQ_MASK, HDMI_A_SRMCTRL);
	return rc;
}

static void dw_hdmi_hdcp_2nd_auth(struct dw_hdcp *hdcp)
{
	if (dw_hdmi_hdcp_ksvsha1(hdcp))
		hdcp_modb(hdcp, HDMI_A_SRMCTRL_SHA1_FAIL_ENABLE |
			  HDMI_A_SRMCTRL_KSV_UPDATE_ENABLE,
			  HDMI_A_SRMCTRL_SHA1_FAIL_MASK |
			  HDMI_A_SRMCTRL_KSV_UPDATE_MASK, HDMI_A_SRMCTRL);
	else
		hdcp_modb(hdcp, HDMI_A_SRMCTRL_SHA1_FAIL_DISABLE |
			  HDMI_A_SRMCTRL_KSV_UPDATE_ENABLE,
			  HDMI_A_SRMCTRL_SHA1_FAIL_MASK |
			  HDMI_A_SRMCTRL_KSV_UPDATE_MASK, HDMI_A_SRMCTRL);
}

static void dw_hdmi_hdcp_isr(struct dw_hdcp *hdcp, int hdcp_int)
{
	dev_dbg(hdcp->dev, "hdcp_int is 0x%02x\n", hdcp_int);
	if (hdcp_int & HDMI_A_APIINTSTAT_KSVSHA1_CALC_INT) {
		dev_dbg(hdcp->dev, "hdcp sink is a repeater\n");
		dw_hdmi_hdcp_2nd_auth(hdcp);
	}
	if (hdcp_int & 0x40) {
		hdcp->status = DW_HDCP_AUTH_FAIL;
		if (hdcp->remaining_times > 1)
			hdcp->remaining_times--;
		else if (hdcp->remaining_times == 1)
			hdcp_modb(hdcp,
				  HDMI_A_HDCPCFG1_ENCRYPTIONDISABLE_DISABLE,
				  HDMI_A_HDCPCFG1_ENCRYPTIONDISABLE_MASK,
				  HDMI_A_HDCPCFG1);
	}
	if (hdcp_int & 0x80) {
		dev_dbg(hdcp->dev, "hdcp auth success\n");
		hdcp->status = DW_HDCP_AUTH_SUCCESS;
	}
}

static ssize_t hdcp_enable_read(struct device *device,
				struct device_attribute *attr, char *buf)
{
	bool enable = 0;
	struct dw_hdcp *hdcp = g_hdcp;

	if (hdcp)
		enable = hdcp->enable;

	return snprintf(buf, PAGE_SIZE, "%d\n", enable);
}

static ssize_t hdcp_enable_write(struct device *device,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	bool enable;
	struct dw_hdcp *hdcp = g_hdcp;

	if (!hdcp)
		return -EINVAL;

	if (kstrtobool(buf, &enable))
		return -EINVAL;

	if (hdcp->enable != enable) {
		if (enable) {
			hdcp->enable = enable;
			if (hdcp->read(hdcp->hdmi, HDMI_PHY_STAT0) &
			    HDMI_PHY_HPD)
				dw_hdmi_hdcp_start(hdcp);
		} else {
			dw_hdmi_hdcp_stop(hdcp);
			hdcp->enable = enable;
		}
	}

	return count;
}

static DEVICE_ATTR(enable, 0644, hdcp_enable_read, hdcp_enable_write);

static ssize_t hdcp_trytimes_read(struct device *device,
				  struct device_attribute *attr, char *buf)
{
	int trytimes = 0;
	struct dw_hdcp *hdcp = g_hdcp;

	if (hdcp)
		trytimes = hdcp->retry_times;

	return snprintf(buf, PAGE_SIZE, "%d\n", trytimes);
}

static ssize_t hdcp_trytimes_write(struct device *device,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int trytimes;
	struct dw_hdcp *hdcp = g_hdcp;

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

static DEVICE_ATTR(trytimes, 0644, hdcp_trytimes_read, hdcp_trytimes_write);

static ssize_t hdcp_status_read(struct device *device,
				struct device_attribute *attr, char *buf)
{
	int status = DW_HDCP_DISABLED;
	struct dw_hdcp *hdcp = g_hdcp;

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

static DEVICE_ATTR(status, 0444, hdcp_status_read, NULL);

static int dw_hdmi_hdcp_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct dw_hdcp *hdcp = pdev->dev.platform_data;

	g_hdcp = hdcp;
	hdcp->mdev.minor = MISC_DYNAMIC_MINOR;
	hdcp->mdev.name = "hdmi_hdcp1x";
	hdcp->mdev.mode = 0666;

	if (misc_register(&hdcp->mdev)) {
		dev_err(&pdev->dev, "HDCP: Could not add character driver\n");
		return -EINVAL;
	}

	ret = device_create_file(hdcp->mdev.this_device, &dev_attr_enable);
	if (ret) {
		dev_err(&pdev->dev, "HDCP: Could not add sys file enable\n");
		ret = -EINVAL;
		goto error0;
	}

	ret = device_create_file(hdcp->mdev.this_device, &dev_attr_trytimes);
	if (ret) {
		dev_err(&pdev->dev, "HDCP: Could not add sys file trytimes\n");
		ret = -EINVAL;
		goto error1;
	}

	ret = device_create_file(hdcp->mdev.this_device, &dev_attr_status);
	if (ret) {
		dev_err(&pdev->dev, "HDCP: Could not add sys file status\n");
		ret = -EINVAL;
		goto error2;
	}

	/* retry time if hdcp auth fail. unlimited time if set 0 */
	hdcp->retry_times = 0;
	hdcp->dev = &pdev->dev;
	hdcp->hdcp_start = dw_hdmi_hdcp_start;
	hdcp->hdcp_stop = dw_hdmi_hdcp_stop;
	hdcp->hdcp_isr = dw_hdmi_hdcp_isr;
	dev_dbg(hdcp->dev, "%s success\n", __func__);
	return 0;

error2:
	device_remove_file(hdcp->mdev.this_device, &dev_attr_trytimes);
error1:
	device_remove_file(hdcp->mdev.this_device, &dev_attr_enable);
error0:
	misc_deregister(&hdcp->mdev);
	return ret;
}

static int dw_hdmi_hdcp_remove(struct platform_device *pdev)
{
	struct dw_hdcp *hdcp = pdev->dev.platform_data;

	device_remove_file(hdcp->mdev.this_device, &dev_attr_trytimes);
	device_remove_file(hdcp->mdev.this_device, &dev_attr_enable);
	device_remove_file(hdcp->mdev.this_device, &dev_attr_status);
	misc_deregister(&hdcp->mdev);

	kfree(hdcp->keys);
	kfree(hdcp->seeds);

	return 0;
}

static struct platform_driver dw_hdmi_hdcp_driver = {
	.probe  = dw_hdmi_hdcp_probe,
	.remove = dw_hdmi_hdcp_remove,
	.driver = {
		.name = DW_HDCP_DRIVER_NAME,
	},
};

module_platform_driver(dw_hdmi_hdcp_driver);
MODULE_DESCRIPTION("DW HDMI transmitter HDCP driver");
MODULE_LICENSE("GPL");
