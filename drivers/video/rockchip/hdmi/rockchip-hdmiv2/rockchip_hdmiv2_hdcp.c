#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/miscdevice.h>
#include <linux/workqueue.h>
#include <linux/firmware.h>
#include "rockchip_hdmiv2.h"
#include "rockchip_hdmiv2_hw.h"

#define	HDCP_KEY_SIZE		308
#define HDCP_PRIVATE_KEY_SIZE	280
#define HDCP_KEY_SHA_SIZE	20
#define HDCP_KEY_SEED_SIZE	2

struct hdcp_keys {
	u8 KSV[8];
	u8 devicekey[HDCP_PRIVATE_KEY_SIZE];
	u8 sha1[HDCP_KEY_SHA_SIZE];
};

struct hdcp {
	struct hdmi		*hdmi;
	int			enable;
	int			retry_times;
	struct hdcp_keys	*keys;
	char			*seeds;
	int			invalidkey;
	char			*invalidkeys;
};

static struct miscdevice mdev;
static struct hdcp *hdcp = NULL;

static void hdcp_load_key(struct hdmi *hdmi, struct hdcp_keys *key)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;
	int i, value;

	/* Disable decryption logic */
	hdmi_writel(hdmi_dev, HDCPREG_RMCTL, 0);
	/* Poll untile DPK write is allowed */
	do {
		value = hdmi_readl(hdmi_dev, HDCPREG_RMSTS);
	} while ((value & m_DPK_WR_OK_STS) == 0);

	/* write unencryped AKSV */
	hdmi_writel(hdmi_dev, HDCPREG_DPK6, 0);
	hdmi_writel(hdmi_dev, HDCPREG_DPK5, 0);
	hdmi_writel(hdmi_dev, HDCPREG_DPK4, key->KSV[4]);
	hdmi_writel(hdmi_dev, HDCPREG_DPK3, key->KSV[3]);
	hdmi_writel(hdmi_dev, HDCPREG_DPK2, key->KSV[2]);
	hdmi_writel(hdmi_dev, HDCPREG_DPK1, key->KSV[1]);
	hdmi_writel(hdmi_dev, HDCPREG_DPK0, key->KSV[0]);
	/* Poll untile DPK write is allowed */
	do {
		value = hdmi_readl(hdmi_dev, HDCPREG_RMSTS);
	} while ((value & m_DPK_WR_OK_STS) == 0);

	if (hdcp->seeds != NULL) {
		hdmi_writel(hdmi_dev, HDCPREG_RMCTL, 1);
		hdmi_writel(hdmi_dev, HDCPREG_SEED1, hdcp->seeds[0]);
		hdmi_writel(hdmi_dev, HDCPREG_SEED0, hdcp->seeds[1]);
	} else {
		hdmi_writel(hdmi_dev, HDCPREG_RMCTL, 0);
	}

	/* write private key */
	for (i = 0; i < HDCP_PRIVATE_KEY_SIZE; i += 7) {
		hdmi_writel(hdmi_dev, HDCPREG_DPK6, key->devicekey[i + 6]);
		hdmi_writel(hdmi_dev, HDCPREG_DPK5, key->devicekey[i + 5]);
		hdmi_writel(hdmi_dev, HDCPREG_DPK4, key->devicekey[i + 4]);
		hdmi_writel(hdmi_dev, HDCPREG_DPK3, key->devicekey[i + 3]);
		hdmi_writel(hdmi_dev, HDCPREG_DPK2, key->devicekey[i + 2]);
		hdmi_writel(hdmi_dev, HDCPREG_DPK1, key->devicekey[i + 1]);
		hdmi_writel(hdmi_dev, HDCPREG_DPK0, key->devicekey[i]);

		do {
			value = hdmi_readl(hdmi_dev, HDCPREG_RMSTS);
		} while ((value & m_DPK_WR_OK_STS) == 0);
	}

	pr_info("%s success\n", __func__);
}

static void hdcp_load_keys_cb(const struct firmware *fw,
			      void *context)
{
	struct hdmi *hdmi = (struct hdmi *)context;

	if (fw->size < HDCP_KEY_SIZE) {
		pr_err("HDCP: firmware wrong size %d\n", (int)fw->size);
		return;
	}
	hdcp->keys = kmalloc(HDCP_KEY_SIZE, GFP_KERNEL);
	memcpy(hdcp->keys, fw->data, HDCP_KEY_SIZE);

	if (fw->size > HDCP_KEY_SIZE) {
		if ((fw->size - HDCP_KEY_SIZE) < HDCP_KEY_SEED_SIZE) {
			pr_err("HDCP: invalid seed key size\n");
			return;
		}
		hdcp->seeds = kmalloc(HDCP_KEY_SEED_SIZE, GFP_KERNEL);
		if (hdcp->seeds == NULL) {
			pr_err("HDCP: can't allocated space for seed keys\n");
			return;
		}
		memcpy(hdcp->seeds, fw->data + HDCP_KEY_SIZE,
		       HDCP_KEY_SEED_SIZE);
	}
	hdcp_load_key(hdmi, hdcp->keys);
}

static void rockchip_hdmiv2_hdcp_start(struct hdmi *hdmi)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	if (!hdcp->enable)
		return;
	if (hdmi_dev->soctype == HDMI_SOC_RK3368) {
		hdmi_msk_reg(hdmi_dev, HDCP2REG_CTRL,
			     m_HDCP2_OVR_EN | m_HDCP2_FORCE,
			     v_HDCP2_OVR_EN(1) | v_HDCP2_FORCE(0));
		hdmi_writel(hdmi_dev, HDCP2REG_MASK, 0x00);
		hdmi_writel(hdmi_dev, HDCP2REG_MUTE, 0x00);
	}

	hdmi_msk_reg(hdmi_dev, FC_INVIDCONF,
		     m_FC_HDCP_KEEPOUT, v_FC_HDCP_KEEPOUT(1));
	hdmi_msk_reg(hdmi_dev, A_HDCPCFG0,
		     m_HDMI_DVI, v_HDMI_DVI(hdmi->edid.sink_hdmi));
	hdmi_writel(hdmi_dev, A_OESSWCFG, 0x40);
	hdmi_msk_reg(hdmi_dev, A_HDCPCFG0,
		     m_ENCRYPT_BYPASS | m_FEATURE11_EN | m_SYNC_RI_CHECK,
		     v_ENCRYPT_BYPASS(0) | v_FEATURE11_EN(0) |
		     v_SYNC_RI_CHECK(1));
	hdmi_msk_reg(hdmi_dev, A_HDCPCFG1,
		     m_ENCRYPT_DISBALE | m_PH2UPSHFTENC,
		     v_ENCRYPT_DISBALE(0) | v_PH2UPSHFTENC(1));
	/* Reset HDCP Engine */
	hdmi_msk_reg(hdmi_dev, A_HDCPCFG1,
		     m_HDCP_SW_RST, v_HDCP_SW_RST(0));

	hdmi_writel(hdmi_dev, A_APIINTMSK, 0x00);
	hdmi_msk_reg(hdmi_dev, A_HDCPCFG0, m_RX_DETECT, v_RX_DETECT(1));

	hdmi_msk_reg(hdmi_dev, MC_CLKDIS,
		     m_HDCPCLK_DISABLE, v_HDCPCLK_DISABLE(0));
	pr_info("%s success\n", __func__);
}

static void rockchip_hdmiv2_hdcp_stop(struct hdmi *hdmi)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	if (!hdcp->enable)
		return;

	hdmi_msk_reg(hdmi_dev, MC_CLKDIS,
		     m_HDCPCLK_DISABLE, v_HDCPCLK_DISABLE(1));
	hdmi_writel(hdmi_dev, A_APIINTMSK, 0xff);
	hdmi_msk_reg(hdmi_dev, A_HDCPCFG0, m_RX_DETECT, v_RX_DETECT(0));
}

static ssize_t hdcp_enable_read(struct device *device,
				struct device_attribute *attr, char *buf)
{
	int enable = 0;

	if (hdcp)
		enable = hdcp->enable;

	return snprintf(buf, PAGE_SIZE, "%d\n", enable);
}

static ssize_t hdcp_enable_write(struct device *device,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	int enable;

	if (hdcp == NULL)
		return -EINVAL;

	if (kstrtoint(buf, 0, &enable))
		return -EINVAL;

	if (hdcp->enable != enable) {
		if (!hdcp->enable)
			hdmi_submit_work(hdcp->hdmi, HDMI_ENABLE_HDCP, 0, NULL);
		else
			rockchip_hdmiv2_hdcp_stop(hdcp->hdmi);
		hdcp->enable =	enable;
	}

	return count;
}
static DEVICE_ATTR(enable, S_IRUGO|S_IWUSR,
		   hdcp_enable_read, hdcp_enable_write);

static ssize_t hdcp_trytimes_read(struct device *device,
				  struct device_attribute *attr, char *buf)
{
	int trytimes = 0;

	if (hdcp)
		trytimes = hdcp->retry_times;

	return snprintf(buf, PAGE_SIZE, "%d\n", trytimes);
}

static ssize_t hdcp_trytimes_wrtie(struct device *device,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int trytimes;

	if (hdcp == NULL)
		return -EINVAL;

	if (kstrtoint(buf, 0, &trytimes))
		return -EINVAL;

	if (hdcp->retry_times != trytimes)
		hdcp->retry_times = trytimes;

	return count;
}
static DEVICE_ATTR(trytimes, S_IRUGO|S_IWUSR,
		   hdcp_trytimes_read, hdcp_trytimes_wrtie);

static int hdcp_init(struct hdmi *hdmi)
{
	int ret;

	mdev.minor = MISC_DYNAMIC_MINOR;
	mdev.name = "hdcp";
	mdev.mode = 0666;
	hdcp = kmalloc(sizeof(*hdcp), GFP_KERNEL);
	if (!hdcp) {
		pr_err("HDCP: kmalloc fail!\n");
		ret = -ENOMEM;
		goto error0;
	}
	memset(hdcp, 0, sizeof(struct hdcp));
	hdcp->hdmi = hdmi;
	if (misc_register(&mdev)) {
		pr_err("HDCP: Could not add character driver\n");
		ret = HDMI_ERROR_FALSE;
		goto error1;
	}
	ret = device_create_file(mdev.this_device, &dev_attr_enable);
	if (ret) {
		pr_err("HDCP: Could not add sys file enable\n");
		ret = -EINVAL;
		goto error2;
	}
	ret = device_create_file(mdev.this_device, &dev_attr_trytimes);
	if (ret) {
		pr_err("HDCP: Could not add sys file enable\n");
		ret = -EINVAL;
		goto error3;
	}

	ret = request_firmware_nowait(THIS_MODULE, FW_ACTION_NOHOTPLUG,
				      "hdcp", mdev.this_device, GFP_KERNEL,
				      hdmi, hdcp_load_keys_cb);

	if (ret < 0) {
		pr_err("HDCP: request_firmware_nowait failed: %d\n", ret);
		goto error4;
	}

	hdmi->ops->hdcp_cb = rockchip_hdmiv2_hdcp_start;
	return 0;

error4:
	device_remove_file(mdev.this_device, &dev_attr_trytimes);
error3:
	device_remove_file(mdev.this_device, &dev_attr_enable);
error2:
	misc_deregister(&mdev);
error1:
	kfree(hdcp->keys);
	kfree(hdcp->invalidkeys);
	kfree(hdcp);
error0:
	return ret;
}

void rockchip_hdmiv2_hdcp_init(struct hdmi *hdmi)
{
	pr_info("%s", __func__);
	if (hdcp == NULL)
		hdcp_init(hdmi);
	else
		hdcp_load_key(hdmi, hdcp->keys);
}

