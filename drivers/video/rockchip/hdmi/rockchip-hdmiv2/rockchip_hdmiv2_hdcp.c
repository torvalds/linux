/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/miscdevice.h>
#include <linux/workqueue.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include "rockchip_hdmiv2.h"
#include "rockchip_hdmiv2_hw.h"

#define	HDCP_KEY_SIZE		308
#define HDCP_PRIVATE_KEY_SIZE	280
#define HDCP_KEY_SHA_SIZE	20
#define HDCP_KEY_SEED_SIZE	2

#define KSV_LEN			5
#define HEADER			10
#define SHAMAX			20

#define MAX_DOWNSTREAM_DEVICE_NUM	5

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

struct sha_t {
	u8 mlength[8];
	u8 mblock[64];
	int mindex;
	int mcomputed;
	int mcorrupted;
	unsigned int mdigest[5];
};

static struct miscdevice mdev;
static struct hdcp *hdcp;

static void sha_reset(struct sha_t *sha)
{
	u32 i = 0;

	sha->mindex = 0;
	sha->mcomputed = false;
	sha->mcorrupted = false;
	for (i = 0; i < sizeof(sha->mlength); i++)
		sha->mlength[i] = 0;

	sha->mdigest[0] = 0x67452301;
	sha->mdigest[1] = 0xEFCDAB89;
	sha->mdigest[2] = 0x98BADCFE;
	sha->mdigest[3] = 0x10325476;
	sha->mdigest[4] = 0xC3D2E1F0;
}

#define shacircularshift(bits, word) ((((word) << (bits)) & 0xFFFFFFFF) | \
				     ((word) >> (32 - (bits))))
void sha_processblock(struct sha_t *sha)
{
	const unsigned int K[] = {
	/* constants defined in SHA-1 */
	0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6 };
	unsigned int W[80]; /* word sequence */
	unsigned int A, B, C, D, E; /* word buffers */
	unsigned int temp = 0;
	int t = 0;

	/* Initialize the first 16 words in the array W */
	for (t = 0; t < 80; t++) {
		if (t < 16) {
			W[t] = ((unsigned int)sha->mblock[t * 4 + 0]) << 24;
			W[t] |= ((unsigned int)sha->mblock[t * 4 + 1]) << 16;
			W[t] |= ((unsigned int)sha->mblock[t * 4 + 2]) << 8;
			W[t] |= ((unsigned int)sha->mblock[t * 4 + 3]) << 0;
		} else {
			A = W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16];
			W[t] = shacircularshift(1, A);
		}
	}

	A = sha->mdigest[0];
	B = sha->mdigest[1];
	C = sha->mdigest[2];
	D = sha->mdigest[3];
	E = sha->mdigest[4];

	for (t = 0; t < 80; t++) {
		temp = shacircularshift(5, A);
		if (t < 20)
			temp += ((B & C) | ((~B) & D)) + E + W[t] + K[0];
		else if (t < 40)
			temp += (B ^ C ^ D) + E + W[t] + K[1];
		else if (t < 60)
			temp += ((B & C) | (B & D) | (C & D)) + E + W[t] + K[2];
		else
			temp += (B ^ C ^ D) + E + W[t] + K[3];

		E = D;
		D = C;
		C = shacircularshift(30, B);
		B = A;
		A = (temp & 0xFFFFFFFF);
	}

	sha->mdigest[0] = (sha->mdigest[0] + A) & 0xFFFFFFFF;
	sha->mdigest[1] = (sha->mdigest[1] + B) & 0xFFFFFFFF;
	sha->mdigest[2] = (sha->mdigest[2] + C) & 0xFFFFFFFF;
	sha->mdigest[3] = (sha->mdigest[3] + D) & 0xFFFFFFFF;
	sha->mdigest[4] = (sha->mdigest[4] + E) & 0xFFFFFFFF;

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
	unsigned j = 0;
	int rc = true;

	if (data == 0 || size == 0) {
		pr_err("invalid input data");
		return;
	}
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

static int hdcpverify_ksv(const u8 *data, u32 size)
{
	u32 i = 0;
	struct sha_t sha;

	if ((!data) || (size < (HEADER + SHAMAX))) {
		pr_err("invalid input data");
		return false;
	}

	sha_reset(&sha);
	sha_input(&sha, data, size - SHAMAX);
	if (sha_result(&sha) == false) {
		pr_err("cannot process SHA digest");
		return false;
	}

	for (i = 0; i < SHAMAX; i++) {
		if (data[size - SHAMAX + i] != (u8)(sha.mdigest[i / 4]
				>> ((i % 4) * 8))) {
			pr_err("SHA digest does not match");
			return false;
		}
	}
	return true;
}

static int rockchip_hdmiv2_hdcp_ksvsha1(struct hdmi_dev *hdmi_dev)
{
	int rc = 0, value, list, i;
	char bstaus0, bstaus1;
	char *ksvlistbuf;

	hdmi_msk_reg(hdmi_dev, A_KSVMEMCTRL, m_KSV_MEM_REQ, v_KSV_MEM_REQ(1));
	list = 20;
	do {
		value = hdmi_readl(hdmi_dev, A_KSVMEMCTRL);
		usleep_range(500, 1000);
	} while ((value & m_KSV_MEM_ACCESS) == 0 && --list);

	if ((value & m_KSV_MEM_ACCESS) == 0) {
		pr_err("KSV memory can not access\n");
		rc = -1;
		goto out;
	}

	hdmi_readl(hdmi_dev, HDCP_BSTATUS_0);
	bstaus0 = hdmi_readl(hdmi_dev, HDCP_BSTATUS_0 + 1);
	bstaus1 = hdmi_readl(hdmi_dev, HDCP_BSTATUS_1 + 1);

	if (bstaus0 & m_MAX_DEVS_EXCEEDED) {
		pr_err("m_MAX_DEVS_EXCEEDED\n");
		rc = -1;
		goto out;
	}
	list = bstaus0 & m_DEVICE_COUNT;
	if (list > MAX_DOWNSTREAM_DEVICE_NUM) {
		pr_err("MAX_DOWNSTREAM_DEVICE_NUM\n");
		rc = -1;
		goto out;
	}
	if (bstaus1 & (1 << 3)) {
		pr_err("MAX_CASCADE_EXCEEDED\n");
		rc = -1;
		goto out;
	}
	value = (list * KSV_LEN) + HEADER + SHAMAX;
	ksvlistbuf = kmalloc(value, GFP_KERNEL);
	if (!ksvlistbuf) {
		pr_err("HDCP: kmalloc ksvlistbuf fail!\n");
		rc = -ENOMEM;
		goto out;
	}
	ksvlistbuf[(list * KSV_LEN)] = bstaus0;
	ksvlistbuf[(list * KSV_LEN) + 1] = bstaus1;
	for (i = 2; i < value; i++) {
		if (i < HEADER)	/* BSTATUS & M0 */
			ksvlistbuf[(list * KSV_LEN) + i] =
				hdmi_readl(hdmi_dev, HDCP_BSTATUS_0 + i + 1);
		else if (i < (HEADER + (list * KSV_LEN))) /* KSV list */
			ksvlistbuf[i - HEADER] =
				hdmi_readl(hdmi_dev, HDCP_BSTATUS_0 + i + 1);
		else /* SHA */
			ksvlistbuf[i] =
				hdmi_readl(hdmi_dev, HDCP_BSTATUS_0 + i + 1);
	}
	if (hdcpverify_ksv(ksvlistbuf, value) == true) {
		rc = 0;
		pr_info("ksv check valid\n");
	} else {
		pr_info("ksv check invalid\n");
		rc = -1;
	}
	kfree(ksvlistbuf);
out:
	hdmi_msk_reg(hdmi_dev, A_KSVMEMCTRL, m_KSV_MEM_REQ, v_KSV_MEM_REQ(0));
	return rc;
}

static void rockchip_hdmiv2_hdcp_2nd_auth(struct hdmi *hdmi)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	if (rockchip_hdmiv2_hdcp_ksvsha1(hdmi_dev))
		hdmi_msk_reg(hdmi_dev, A_KSVMEMCTRL,
			     m_SHA1_FAIL | m_KSV_UPDATE,
			     v_SHA1_FAIL(1) | v_KSV_UPDATE(1));
	else
		hdmi_msk_reg(hdmi_dev, A_KSVMEMCTRL,
			     m_SHA1_FAIL | m_KSV_UPDATE,
			     v_SHA1_FAIL(0) | v_KSV_UPDATE(1));
}

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

	if (hdcp->seeds) {
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

	if (!fw) {
		pr_info("HDCP: firmware is not loaded\n");
		return;
	}
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
		if (!hdcp->seeds)
			return;

		memcpy(hdcp->seeds, fw->data + HDCP_KEY_SIZE,
		       HDCP_KEY_SEED_SIZE);
	}
	hdcp_load_key(hdmi, hdcp->keys);
}

void rockchip_hdmiv2_hdcp2_enable(int enable)
{
	struct hdmi_dev *hdmi_dev;

	if (!hdcp) {
		pr_err("rockchip hdmiv2 hdcp is not exist\n");
		return;
	}
	hdmi_dev = hdcp->hdmi->property->priv;
	if ((hdmi_readl(hdmi_dev, CONFIG1_ID) & m_HDCP22) == 0) {
		pr_err("Don't support hdcp22\n");
		return;
	}
	if (hdmi_dev->hdcp2_enable != enable) {
		hdmi_dev->hdcp2_enable = enable;
		if (hdmi_dev->hdcp2_enable == 0) {
			hdmi_msk_reg(hdmi_dev, HDCP2REG_CTRL,
				     m_HDCP2_OVR_EN | m_HDCP2_FORCE,
				     v_HDCP2_OVR_EN(1) | v_HDCP2_FORCE(0));
			hdmi_writel(hdmi_dev, HDCP2REG_MASK, 0xff);
			hdmi_writel(hdmi_dev, HDCP2REG_MUTE, 0xff);
		} else {
			hdmi_msk_reg(hdmi_dev, HDCP2REG_CTRL,
				     m_HDCP2_OVR_EN | m_HDCP2_FORCE,
				     v_HDCP2_OVR_EN(0) | v_HDCP2_FORCE(0));
			hdmi_writel(hdmi_dev, HDCP2REG_MASK, 0x00);
			hdmi_writel(hdmi_dev, HDCP2REG_MUTE, 0x00);
		}
	}
}
EXPORT_SYMBOL(rockchip_hdmiv2_hdcp2_enable);

void rockchip_hdmiv2_hdcp2_init(void (*hdcp2_enble)(int),
				void (*hdcp2_reset)(void),
				void (*hdcp2_start)(void))
{
	struct hdmi_dev *hdmi_dev;

	if (!hdcp) {
		pr_err("rockchip hdmiv2 hdcp is not exist\n");
		return;
	}
	hdmi_dev = hdcp->hdmi->property->priv;
	hdmi_dev->hdcp2_en = hdcp2_enble;
	hdmi_dev->hdcp2_reset = hdcp2_reset;
	hdmi_dev->hdcp2_start = hdcp2_start;
}
EXPORT_SYMBOL(rockchip_hdmiv2_hdcp2_init);

static void rockchip_hdmiv2_hdcp_start(struct hdmi *hdmi)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	if (!hdcp->enable)
		return;
	if (hdmi_readl(hdmi_dev, CONFIG1_ID) & m_HDCP22) {
		if (hdmi_dev->hdcp2_enable == 0) {
			hdmi_msk_reg(hdmi_dev, HDCP2REG_CTRL,
				     m_HDCP2_OVR_EN | m_HDCP2_FORCE,
				     v_HDCP2_OVR_EN(1) | v_HDCP2_FORCE(0));
			hdmi_writel(hdmi_dev, HDCP2REG_MASK, 0xff);
			hdmi_writel(hdmi_dev, HDCP2REG_MUTE, 0xff);
		} else {
			hdmi_msk_reg(hdmi_dev, HDCP2REG_CTRL,
				     m_HDCP2_OVR_EN | m_HDCP2_FORCE,
				     v_HDCP2_OVR_EN(0) | v_HDCP2_FORCE(0));
			hdmi_writel(hdmi_dev, HDCP2REG_MASK, 0x00);
			hdmi_writel(hdmi_dev, HDCP2REG_MUTE, 0x00);
		}
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
	if (hdmi_readl(hdmi_dev, MC_CLKDIS) & m_HDCPCLK_DISABLE)
		hdmi_msk_reg(hdmi_dev, A_HDCPCFG1,
			     m_HDCP_SW_RST, v_HDCP_SW_RST(0));

	hdmi_writel(hdmi_dev, A_APIINTMSK, 0x00);
	hdmi_msk_reg(hdmi_dev, A_HDCPCFG0, m_RX_DETECT, v_RX_DETECT(1));

	hdmi_msk_reg(hdmi_dev, MC_CLKDIS,
		     m_HDCPCLK_DISABLE, v_HDCPCLK_DISABLE(0));
	if (hdmi_dev->hdcp2_start)
		hdmi_dev->hdcp2_start();
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
	hdmi_msk_reg(hdmi_dev, A_KSVMEMCTRL,
		     m_SHA1_FAIL | m_KSV_UPDATE,
		     v_SHA1_FAIL(0) | v_KSV_UPDATE(0));
	rockchip_hdmiv2_hdcp2_enable(0);
}

void rockchip_hdmiv2_hdcp_isr(struct hdmi_dev *hdmi_dev, int hdcp_int)
{
	pr_info("hdcp_int is 0x%02x\n", hdcp_int);

	if (hdcp_int & m_KSVSHA1_CALC_INT) {
		pr_info("hdcp sink is a repeater\n");
		hdmi_submit_work(hdcp->hdmi, HDMI_HDCP_AUTH_2ND, 0, 0);
	}
	if (hdcp_int & 0x40) {
		pr_info("hdcp check failed\n");
		rockchip_hdmiv2_hdcp_stop(hdmi_dev->hdmi);
		hdmi_submit_work(hdcp->hdmi, HDMI_ENABLE_HDCP, 0, 0);
	}
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

	if (!hdcp)
		return -EINVAL;
	if (!hdcp->keys) {
		pr_err("HDCP: key is not loaded\n");
		return -EINVAL;
	}
	if (kstrtoint(buf, 0, &enable))
		return -EINVAL;

	if (hdcp->enable != enable) {
		if (!hdcp->enable)
			hdmi_submit_work(hdcp->hdmi, HDMI_ENABLE_HDCP, 0, 0);
		else
			rockchip_hdmiv2_hdcp_stop(hdcp->hdmi);
		hdcp->enable =	enable;
	}

	return count;
}

static DEVICE_ATTR(enable, 0644, hdcp_enable_read, hdcp_enable_write);

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

	if (!hdcp)
		return -EINVAL;

	if (kstrtoint(buf, 0, &trytimes))
		return -EINVAL;

	if (hdcp->retry_times != trytimes)
		hdcp->retry_times = trytimes;

	return count;
}

static DEVICE_ATTR(trytimes, 0644, hdcp_trytimes_read, hdcp_trytimes_wrtie);

static int hdcp_init(struct hdmi *hdmi)
{
	int ret;
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

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
	if ((hdmi_readl(hdmi_dev, MC_CLKDIS) & m_HDCPCLK_DISABLE) == 0)
		hdcp->enable = 1;
	hdmi->ops->hdcp_cb = rockchip_hdmiv2_hdcp_start;
	hdmi->ops->hdcp_auth2nd = rockchip_hdmiv2_hdcp_2nd_auth;
	hdmi->ops->hdcp_power_off_cb = rockchip_hdmiv2_hdcp_stop;
	return 0;

error4:
	device_remove_file(mdev.this_device, &dev_attr_trytimes);
error3:
	device_remove_file(mdev.this_device, &dev_attr_enable);
error2:
	misc_deregister(&mdev);
error1:
	kfree(hdcp);
error0:
	return ret;
}

void rockchip_hdmiv2_hdcp_init(struct hdmi *hdmi)
{
	pr_info("%s", __func__);

	if (!hdcp) {
		hdcp_init(hdmi);
	} else {
		if (hdcp->keys)
			hdcp_load_key(hdmi, hdcp->keys);
		else
			pr_info("hdcpkeys is no load\n");
	}
}
