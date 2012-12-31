/* linux/drivers/media/video/exynos/tv/hdcp_drv.c
 *
 * Copyright (c) 2011 Samsung Electronics
 *		http://www.samsung.com/
 *
 * HDCP function for Samsung TV driver
 *
 * This program is free software. you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/workqueue.h>

#include "hdmi.h"
#include "regs-hdmi-5250.h"

#define AN_SIZE			8
#define AKSV_SIZE		5
#define BKSV_SIZE		5
#define MAX_KEY_SIZE		16

#define BKSV_RETRY_CNT		14
#define BKSV_DELAY		100

#define DDC_RETRY_CNT		400000
#define DDC_DELAY		25

#define KEY_LOAD_RETRY_CNT	1000
#define ENCRYPT_CHECK_CNT	10

#define KSV_FIFO_RETRY_CNT	50
#define KSV_FIFO_CHK_DELAY	100 /* ms */
#define KSV_LIST_RETRY_CNT	10000

#define BCAPS_SIZE		1
#define BSTATUS_SIZE		2
#define SHA_1_HASH_SIZE		20
#define HDCP_MAX_DEVS		128
#define HDCP_KSV_SIZE		5

/* offset of HDCP port */
#define HDCP_BKSV		0x00
#define HDCP_RI			0x08
#define HDCP_AKSV		0x10
#define HDCP_AN			0x18
#define HDCP_SHA1		0x20
#define HDCP_BCAPS		0x40
#define HDCP_BSTATUS		0x41
#define HDCP_KSVFIFO		0x43

#define KSV_FIFO_READY			(0x1 << 5)

#define MAX_CASCADE_EXCEEDED_ERROR	(-2)
#define MAX_DEVS_EXCEEDED_ERROR		(-3)
#define REPEATER_ILLEGAL_DEVICE_ERROR	(-4)
#define REPEATER_TIMEOUT_ERROR		(-5)

#define MAX_CASCADE_EXCEEDED		(0x1 << 3)
#define MAX_DEVS_EXCEEDED		(0x1 << 7)

struct i2c_client *hdcp_client;

int hdcp_i2c_read(struct hdmi_device *hdev, u8 offset, int bytes, u8 *buf)
{
	struct device *dev = hdev->dev;
	struct i2c_client *i2c = hdcp_client;
	int ret, cnt = 0;

	struct i2c_msg msg[] = {
		[0] = {
			.addr = i2c->addr,
			.flags = 0,
			.len = 1,
			.buf = &offset
		},
		[1] = {
			.addr = i2c->addr,
			.flags = I2C_M_RD,
			.len = bytes,
			.buf = buf
		}
	};

	do {
		if (!is_hdmi_streaming(hdev))
			goto ddc_read_err;

		ret = i2c_transfer(i2c->adapter, msg, 2);

		if (ret < 0 || ret != 2)
			dev_dbg(dev, "%s: can't read data, retry %d\n",
					__func__, cnt);
		else
			break;

		if (hdev->hdcp_info.auth_status == FIRST_AUTHENTICATION_DONE
			|| hdev->hdcp_info.auth_status == SECOND_AUTHENTICATION_DONE)
			goto ddc_read_err;

		msleep(DDC_DELAY);
		cnt++;
	} while (cnt < DDC_RETRY_CNT);

	if (cnt == DDC_RETRY_CNT)
		goto ddc_read_err;

	dev_dbg(dev, "%s: read data ok\n", __func__);

	return 0;

ddc_read_err:
	dev_err(dev, "%s: can't read data, timeout\n", __func__);
	return -ETIME;
}

int hdcp_i2c_write(struct hdmi_device *hdev, u8 offset, int bytes, u8 *buf)
{
	struct device *dev = hdev->dev;
	struct i2c_client *i2c = hdcp_client;
	u8 msg[bytes + 1];
	int ret, cnt = 0;

	msg[0] = offset;
	memcpy(&msg[1], buf, bytes);

	do {
		if (!is_hdmi_streaming(hdev))
			goto ddc_write_err;

		ret = i2c_master_send(i2c, msg, bytes + 1);

		if (ret < 0 || ret < bytes + 1)
			dev_dbg(dev, "%s: can't write data, retry %d\n",
					__func__, cnt);
		else
			break;

		msleep(DDC_DELAY);
		cnt++;
	} while (cnt < DDC_RETRY_CNT);

	if (cnt == DDC_RETRY_CNT)
		goto ddc_write_err;

	dev_dbg(dev, "%s: write data ok\n", __func__);
	return 0;

ddc_write_err:
	dev_err(dev, "%s: can't write data, timeout\n", __func__);
	return -ETIME;
}

static int __devinit hdcp_probe(struct i2c_client *client,
			const struct i2c_device_id *dev_id)
{
	int ret = 0;

	hdcp_client = client;

	dev_info(&client->adapter->dev, "attached exynos hdcp "
		"into i2c adapter successfully\n");

	return ret;
}

static int hdcp_remove(struct i2c_client *client)
{
	dev_info(&client->adapter->dev, "detached exynos hdcp "
		"from i2c adapter successfully\n");

	return 0;
}

static int hdcp_suspend(struct i2c_client *cl, pm_message_t mesg)
{
	return 0;
};

static int hdcp_resume(struct i2c_client *cl)
{
	return 0;
};

static struct i2c_device_id hdcp_idtable[] = {
	{"exynos_hdcp", 0},
};
MODULE_DEVICE_TABLE(i2c, hdcp_idtable);

static struct i2c_driver hdcp_driver = {
	.driver = {
		.name = "exynos_hdcp",
		.owner = THIS_MODULE,
	},
	.id_table	= hdcp_idtable,
	.probe		= hdcp_probe,
	.remove		= __devexit_p(hdcp_remove),
	.suspend	= hdcp_suspend,
	.resume		= hdcp_resume,
};

static int __init hdcp_init(void)
{
	return i2c_add_driver(&hdcp_driver);
}

static void __exit hdcp_exit(void)
{
	i2c_del_driver(&hdcp_driver);
}

module_init(hdcp_init);
module_exit(hdcp_exit);

/* internal functions of HDCP */
static void hdcp_encryption(struct hdmi_device *hdev, bool on)
{
	if (on)
		hdmi_write_mask(hdev, HDMI_ENC_EN, ~0, HDMI_HDCP_ENC_ENABLE);
	else
		hdmi_write_mask(hdev, HDMI_ENC_EN, 0, HDMI_HDCP_ENC_ENABLE);

	hdmi_reg_mute(hdev, !on);
}

static int hdcp_write_key(struct hdmi_device *hdev, int size, int reg, int offset)
{
	struct device *dev = hdev->dev;
	u8 buf[MAX_KEY_SIZE];
	int cnt, zero = 0;
	int i;

	memset(buf, 0, sizeof(buf));
	hdmi_read_bytes(hdev, reg, buf, size);

	for (cnt = 0; cnt < size; cnt++)
		if (buf[cnt] == 0)
			zero++;

	if (zero == size) {
		dev_dbg(dev, "%s: %s is null\n", __func__,
				offset == HDCP_AN ? "An" : "Aksv");
		goto write_key_err;
	}

	if (hdcp_i2c_write(hdev, offset, size, buf) < 0)
		goto write_key_err;

	for (i = 1; i < size + 1; i++)
		dev_dbg(dev, "%s: %s[%d] : 0x%02x\n", __func__,
				offset == HDCP_AN ? "An" : "Aksv", i, buf[i]);

	return 0;

write_key_err:
	dev_dbg(dev, "%s: write %s is failed\n", __func__,
			offset == HDCP_AN ? "An" : "Aksv");
	return -1;
}

static int hdcp_read_bcaps(struct hdmi_device *hdev)
{
	struct device *dev = hdev->dev;
	u8 bcaps = 0;

	if (hdcp_i2c_read(hdev, HDCP_BCAPS, BCAPS_SIZE, &bcaps) < 0)
		goto bcaps_read_err;

	if (!is_hdmi_streaming(hdev))
		goto bcaps_read_err;

	hdmi_writeb(hdev, HDMI_HDCP_BCAPS, bcaps);

	if (bcaps & HDMI_HDCP_BCAPS_REPEATER)
		hdev->hdcp_info.is_repeater = 1;
	else
		hdev->hdcp_info.is_repeater = 0;

	dev_dbg(dev, "%s: device is %s\n", __func__,
			hdev->hdcp_info.is_repeater ? "REPEAT" : "SINK");
	dev_dbg(dev, "%s: [i2c] bcaps : 0x%02x\n", __func__, bcaps);

	return 0;

bcaps_read_err:
	dev_err(dev, "can't read bcaps : timeout\n");
	return -ETIME;
}

static int hdcp_read_bksv(struct hdmi_device *hdev)
{
	struct device *dev = hdev->dev;
	u8 bksv[BKSV_SIZE];
	int i, j;
	u32 one = 0, zero = 0, result = 0;
	u32 cnt = 0;

	memset(bksv, 0, sizeof(bksv));

	do {
		if (hdcp_i2c_read(hdev, HDCP_BKSV, BKSV_SIZE, bksv) < 0)
			goto bksv_read_err;

		for (i = 0; i < BKSV_SIZE; i++)
			dev_dbg(dev, "%s: i2c read : bksv[%d]: 0x%x\n",
					__func__, i, bksv[i]);

		for (i = 0; i < BKSV_SIZE; i++) {

			for (j = 0; j < 8; j++) {
				result = bksv[i] & (0x1 << j);

				if (result == 0)
					zero++;
				else
					one++;
			}

		}

		if (!is_hdmi_streaming(hdev))
			goto bksv_read_err;

		if ((zero == 20) && (one == 20)) {
			hdmi_write_bytes(hdev, HDMI_HDCP_BKSV_(0), bksv, BKSV_SIZE);
			break;
		}
		dev_dbg(dev, "%s: invalid bksv, retry : %d\n", __func__, cnt);

		msleep(BKSV_DELAY);
		cnt++;
	} while (cnt < BKSV_RETRY_CNT);

	if (cnt == BKSV_RETRY_CNT)
		goto bksv_read_err;

	dev_dbg(dev, "%s: bksv read OK, retry : %d\n", __func__, cnt);
	return 0;

bksv_read_err:
	dev_err(dev, "%s: can't read bksv : timeout\n", __func__);
	return -ETIME;
}

static int hdcp_read_ri(struct hdmi_device *hdev)
{
	struct device *dev = hdev->dev;
	u8 ri[2] = {0, 0};
	u8 rj[2] = {0, 0};


	ri[0] = hdmi_readb(hdev, HDMI_HDCP_RI_0);
	ri[1] = hdmi_readb(hdev, HDMI_HDCP_RI_1);

	if (hdcp_i2c_read(hdev, HDCP_RI, 2, rj) < 0)
		goto compare_err;

	dev_dbg(dev, "%s: Rx -> rj[0]: 0x%02x, rj[1]: 0x%02x\n", __func__,
			rj[0], rj[1]);
	dev_dbg(dev, "%s: Tx -> ri[0]: 0x%02x, ri[1]: 0x%02x\n", __func__,
			ri[0], ri[1]);

	if ((ri[0] == rj[0]) && (ri[1] == rj[1]) && (ri[0] | ri[1]))
		hdmi_writeb(hdev, HDMI_HDCP_CHECK_RESULT,
				HDMI_HDCP_RI_MATCH_RESULT_Y);
	else {
		hdmi_writeb(hdev, HDMI_HDCP_CHECK_RESULT,
				HDMI_HDCP_RI_MATCH_RESULT_N);
		goto compare_err;
	}

	memset(ri, 0, sizeof(ri));
	memset(rj, 0, sizeof(rj));

	dev_dbg(dev, "%s: ri and ri' are matched\n", __func__);

	return 0;

compare_err:
	hdev->hdcp_info.event = HDCP_EVENT_STOP;
	hdev->hdcp_info.auth_status = NOT_AUTHENTICATED;
	dev_err(dev, "%s: ri and ri' are mismatched\n", __func__);
	msleep(10);
	return -1;
}

static void hdcp_sw_reset(struct hdmi_device *hdev)
{
	u8 val;

	val = hdmi_get_int_mask(hdev);

	hdmi_set_int_mask(hdev, HDMI_INTC_EN_HPD_PLUG, 0);
	hdmi_set_int_mask(hdev, HDMI_INTC_EN_HPD_UNPLUG, 0);

	hdmi_sw_hpd_enable(hdev, 1);
	hdmi_sw_hpd_plug(hdev, 0);
	hdmi_sw_hpd_plug(hdev, 1);
	hdmi_sw_hpd_enable(hdev, 0);

	if (val & HDMI_INTC_EN_HPD_PLUG)
		hdmi_set_int_mask(hdev, HDMI_INTC_EN_HPD_PLUG, 1);
	if (val & HDMI_INTC_EN_HPD_UNPLUG)
		hdmi_set_int_mask(hdev, HDMI_INTC_EN_HPD_UNPLUG, 1);
}

static int hdcp_reset_auth(struct hdmi_device *hdev)
{
	struct device *dev = hdev->dev;
	u8 val;
	unsigned long spin_flags;

	if (!is_hdmi_streaming(hdev))
		return -ENODEV;

	spin_lock_irqsave(&hdev->hdcp_info.reset_lock, spin_flags);

	hdev->hdcp_info.event		= HDCP_EVENT_STOP;
	hdev->hdcp_info.auth_status	= NOT_AUTHENTICATED;

	hdmi_write(hdev, HDMI_HDCP_CTRL1, 0x0);
	hdmi_write(hdev, HDMI_HDCP_CTRL2, 0x0);
	hdmi_reg_mute(hdev, 1);

	hdcp_encryption(hdev, 0);

	dev_dbg(dev, "%s: reset authentication\n", __func__);

	val = HDMI_UPDATE_RI_INT_EN | HDMI_WRITE_INT_EN |
		HDMI_WATCHDOG_INT_EN | HDMI_WTFORACTIVERX_INT_EN;
	hdmi_write_mask(hdev, HDMI_STATUS_EN, 0, val);

	hdmi_writeb(hdev, HDMI_HDCP_CHECK_RESULT, HDMI_HDCP_CLR_ALL_RESULTS);

	/* need some delay (at least 1 frame) */
	mdelay(16);

	hdcp_sw_reset(hdev);

	val = HDMI_UPDATE_RI_INT_EN | HDMI_WRITE_INT_EN |
		HDMI_WATCHDOG_INT_EN | HDMI_WTFORACTIVERX_INT_EN;
	hdmi_write_mask(hdev, HDMI_STATUS_EN, ~0, val);
	hdmi_write_mask(hdev, HDMI_HDCP_CTRL1, ~0, HDMI_HDCP_CP_DESIRED_EN);
	spin_unlock_irqrestore(&hdev->hdcp_info.reset_lock, spin_flags);

	return 0;
}

static int hdcp_loadkey(struct hdmi_device *hdev)
{
	struct device *dev = hdev->dev;
	u8 val;
	int cnt = 0;

	hdmi_write_mask(hdev, HDMI_EFUSE_CTRL, ~0, HDMI_EFUSE_CTRL_HDCP_KEY_READ);

	do {
		val = hdmi_readb(hdev, HDMI_EFUSE_STATUS);
		if (val & HDMI_EFUSE_ECC_DONE)
			break;
		cnt++;
		mdelay(1);
	} while (cnt < KEY_LOAD_RETRY_CNT);

	if (cnt == KEY_LOAD_RETRY_CNT)
		goto key_load_err;

	val = hdmi_readb(hdev, HDMI_EFUSE_STATUS);

	if (val & HDMI_EFUSE_ECC_FAIL)
		goto key_load_err;

	dev_dbg(dev, "%s: load key is ok\n", __func__);
	return 0;

key_load_err:
	dev_err(dev, "%s: can't load key\n", __func__);
	return -1;
}

static int hdmi_start_encryption(struct hdmi_device *hdev)
{
	struct device *dev = hdev->dev;
	u8 val;
	u32 cnt = 0;

	do {
		val = hdmi_readb(hdev, HDMI_STATUS);

		if (val & HDMI_AUTHEN_ACK_AUTH) {
			hdcp_encryption(hdev, 1);
			break;
		}

		mdelay(1);

		cnt++;
	} while (cnt < ENCRYPT_CHECK_CNT);

	if (cnt == ENCRYPT_CHECK_CNT)
		goto encrypt_err;


	dev_dbg(dev, "%s: encryption is start\n", __func__);
	return 0;

encrypt_err:
	hdcp_encryption(hdev, 0);
	dev_err(dev, "%s: encryption is failed\n", __func__);
	return -1;
}

static int hdmi_check_repeater(struct hdmi_device *hdev)
{
	struct device *dev = hdev->dev;
	int val, i;
	int cnt = 0, cnt2 = 0;

	u8 bcaps = 0;
	u8 status[BSTATUS_SIZE];
	u8 rx_v[SHA_1_HASH_SIZE];
	u8 ksv_list[HDCP_MAX_DEVS * HDCP_KSV_SIZE];

	u32 dev_cnt;

	memset(status, 0, sizeof(status));
	memset(rx_v, 0, sizeof(rx_v));
	memset(ksv_list, 0, sizeof(ksv_list));

	do {
		if (hdcp_read_bcaps(hdev) < 0)
			goto check_repeater_err;

		bcaps = hdmi_readb(hdev, HDMI_HDCP_BCAPS);

		if (bcaps & KSV_FIFO_READY) {
			dev_dbg(dev, "%s: repeater : ksv fifo not ready\n",
					__func__);
			dev_dbg(dev, "%s: retries = %d\n", __func__, cnt);
			break;
		}

		msleep(KSV_FIFO_CHK_DELAY);

		cnt++;
	} while (cnt < KSV_FIFO_RETRY_CNT);

	if (cnt == KSV_FIFO_RETRY_CNT)
		return REPEATER_TIMEOUT_ERROR;

	dev_dbg(dev, "%s: repeater : ksv fifo ready\n", __func__);

	if (hdcp_i2c_read(hdev, HDCP_BSTATUS, BSTATUS_SIZE, status) < 0)
		goto check_repeater_err;

	if (status[1] & MAX_CASCADE_EXCEEDED)
		return MAX_CASCADE_EXCEEDED_ERROR;
	else if (status[0] & MAX_DEVS_EXCEEDED)
		return MAX_DEVS_EXCEEDED_ERROR;

	hdmi_writeb(hdev, HDMI_HDCP_BSTATUS_0, status[0]);
	hdmi_writeb(hdev, HDMI_HDCP_BSTATUS_1, status[1]);

	dev_dbg(dev, "%s: status[0] :0x%02x\n", __func__, status[0]);
	dev_dbg(dev, "%s: status[1] :0x%02x\n", __func__, status[1]);

	dev_cnt = status[0] & 0x7f;

	dev_dbg(dev, "%s: repeater : dev cnt = %d\n", __func__, dev_cnt);

	if (dev_cnt) {

		if (hdcp_i2c_read(hdev, HDCP_KSVFIFO, dev_cnt * HDCP_KSV_SIZE,
				ksv_list) < 0)
			goto check_repeater_err;

		cnt = 0;

		do {
			hdmi_write_bytes(hdev, HDMI_HDCP_KSV_LIST_(0),
					&ksv_list[cnt * 5], HDCP_KSV_SIZE);

			val = HDMI_HDCP_KSV_WRITE_DONE;

			if (cnt == dev_cnt - 1)
				val |= HDMI_HDCP_KSV_END;

			hdmi_write(hdev, HDMI_HDCP_KSV_LIST_CON, val);

			if (cnt < dev_cnt - 1) {
				cnt2 = 0;
				do {
					val = hdmi_readb(hdev,
						HDMI_HDCP_KSV_LIST_CON);
					if (val & HDMI_HDCP_KSV_READ)
						break;
					cnt2++;
				} while (cnt2 < KSV_LIST_RETRY_CNT);

				if (cnt2 == KSV_LIST_RETRY_CNT)
					dev_dbg(dev, "%s: ksv list not readed\n",
							__func__);
			}
			cnt++;
		} while (cnt < dev_cnt);
	} else
		hdmi_writeb(hdev, HDMI_HDCP_KSV_LIST_CON, HDMI_HDCP_KSV_LIST_EMPTY);

	if (hdcp_i2c_read(hdev, HDCP_SHA1, SHA_1_HASH_SIZE, rx_v) < 0)
		goto check_repeater_err;

	for (i = 0; i < SHA_1_HASH_SIZE; i++)
		dev_dbg(dev, "%s: [i2c] SHA-1 rx :: %02x\n", __func__, rx_v[i]);

	hdmi_write_bytes(hdev, HDMI_HDCP_SHA1_(0), rx_v, SHA_1_HASH_SIZE);

	val = hdmi_readb(hdev, HDMI_HDCP_SHA_RESULT);
	if (val & HDMI_HDCP_SHA_VALID_RD) {
		if (val & HDMI_HDCP_SHA_VALID) {
			dev_dbg(dev, "%s: SHA-1 result is ok\n", __func__);
			hdmi_writeb(hdev, HDMI_HDCP_SHA_RESULT, 0x0);
		} else {
			dev_dbg(dev, "%s: SHA-1 result is not vaild\n", __func__);
			hdmi_writeb(hdev, HDMI_HDCP_SHA_RESULT, 0x0);
			goto check_repeater_err;
		}
	} else {
		dev_dbg(dev, "%s: SHA-1 result is not ready\n", __func__);
		hdmi_writeb(hdev, HDMI_HDCP_SHA_RESULT, 0x0);
		goto check_repeater_err;
	}

	dev_dbg(dev, "%s: check repeater is ok\n", __func__);
	return 0;

check_repeater_err:
	dev_err(dev, "%s: check repeater is failed\n", __func__);
	return -1;
}

static int hdcp_bksv(struct hdmi_device *hdev)
{
	struct device *dev = hdev->dev;

	dev_dbg(dev, "%s\n", __func__);

	hdev->hdcp_info.auth_status = RECEIVER_READ_READY;

	if (hdcp_read_bcaps(hdev) < 0)
		goto bksv_start_err;

	hdev->hdcp_info.auth_status = BCAPS_READ_DONE;

	if (hdcp_read_bksv(hdev) < 0)
		goto bksv_start_err;

	hdev->hdcp_info.auth_status = BKSV_READ_DONE;

	dev_dbg(dev, "%s: bksv start is ok\n", __func__);

	return 0;

bksv_start_err:
	dev_err(dev, "%s: failed to start bksv\n", __func__);
	msleep(100);
	return -1;
}

static int hdcp_second_auth(struct hdmi_device *hdev)
{
	struct device *dev = hdev->dev;
	int ret = 0;

	dev_dbg(dev, "%s\n", __func__);

	if (!hdev->hdcp_info.hdcp_start)
		goto second_auth_err;

	if (!is_hdmi_streaming(hdev))
		goto second_auth_err;

	ret = hdmi_check_repeater(hdev);

	if (!ret) {
		hdev->hdcp_info.auth_status = SECOND_AUTHENTICATION_DONE;
		hdmi_start_encryption(hdev);
	} else {
		switch (ret) {

		case REPEATER_ILLEGAL_DEVICE_ERROR:
			hdmi_writeb(hdev, HDMI_HDCP_CTRL2, 0x1);
			mdelay(1);
			hdmi_writeb(hdev, HDMI_HDCP_CTRL2, 0x0);

			dev_dbg(dev, "%s: repeater : illegal device\n",
					__func__);
			break;
		case REPEATER_TIMEOUT_ERROR:
			hdmi_write_mask(hdev, HDMI_HDCP_CTRL1, ~0,
					HDMI_HDCP_SET_REPEATER_TIMEOUT);
			hdmi_write_mask(hdev, HDMI_HDCP_CTRL1, 0,
					HDMI_HDCP_SET_REPEATER_TIMEOUT);

			dev_dbg(dev, "%s: repeater : timeout\n", __func__);
			break;
		case MAX_CASCADE_EXCEEDED_ERROR:

			dev_dbg(dev, "%s: repeater : exceeded MAX_CASCADE\n",
					__func__);
			break;
		case MAX_DEVS_EXCEEDED_ERROR:

			dev_dbg(dev, "%s: repeater : exceeded MAX_DEVS\n",
					__func__);
			break;
		default:
			break;
		}

		hdev->hdcp_info.auth_status = NOT_AUTHENTICATED;

		goto second_auth_err;
	}

	dev_dbg(dev, "%s: second authentication is OK\n", __func__);
	return 0;

second_auth_err:
	dev_dbg(dev, "%s: second authentication is failed\n", __func__);
	return -1;
}

static int hdcp_write_aksv(struct hdmi_device *hdev)
{
	struct device *dev = hdev->dev;
	dev_dbg(dev, "%s\n", __func__);

	if (hdev->hdcp_info.auth_status != BKSV_READ_DONE) {
		dev_err(dev, "%s: bksv is not ready\n", __func__);
		goto aksv_write_err;
	}
	if (!is_hdmi_streaming(hdev))
		goto aksv_write_err;

	if (hdcp_write_key(hdev, AN_SIZE, HDMI_HDCP_AN_(0), HDCP_AN) < 0)
		goto aksv_write_err;

	hdev->hdcp_info.auth_status = AN_WRITE_DONE;

	dev_dbg(dev, "%s: write An is done\n", __func__);

	if (hdcp_write_key(hdev, AKSV_SIZE, HDMI_HDCP_AKSV_(0), HDCP_AKSV) < 0)
		goto aksv_write_err;

	msleep(100);

	hdev->hdcp_info.auth_status = AKSV_WRITE_DONE;

	dev_dbg(dev, "%s: write aksv is done\n", __func__);
	dev_dbg(dev, "%s: aksv start is OK\n", __func__);
	return 0;

aksv_write_err:
	dev_err(dev, "%s: aksv start is failed\n", __func__);
	return -1;
}

static int hdcp_check_ri(struct hdmi_device *hdev)
{
	struct device *dev = hdev->dev;

	dev_dbg(dev, "%s\n", __func__);

	if (hdev->hdcp_info.auth_status < AKSV_WRITE_DONE) {
		dev_dbg(dev, "%s: ri check is not ready\n", __func__);
		goto check_ri_err;
	}

	if (!is_hdmi_streaming(hdev))
		goto check_ri_err;

	if (hdcp_read_ri(hdev) < 0)
		goto check_ri_err;

	if (hdev->hdcp_info.is_repeater)
		hdev->hdcp_info.auth_status
			= SECOND_AUTHENTICATION_RDY;
	else {
		hdev->hdcp_info.auth_status
			= FIRST_AUTHENTICATION_DONE;
		hdmi_start_encryption(hdev);
	}

	dev_dbg(dev, "%s: ri check is OK\n", __func__);
	return 0;

check_ri_err:
	dev_err(dev, "%s: ri check is failed\n", __func__);
	return -1;
}

static void hdcp_work(struct work_struct *work)
{
	struct hdmi_device *hdev = container_of(work, struct hdmi_device, work);

	if (!hdev->hdcp_info.hdcp_start)
		return;

	if (!is_hdmi_streaming(hdev))
		return;

	if (hdev->hdcp_info.event & HDCP_EVENT_READ_BKSV_START) {
		if (hdcp_bksv(hdev) < 0)
			goto work_err;
		else
			hdev->hdcp_info.event &= ~HDCP_EVENT_READ_BKSV_START;
	}

	if (hdev->hdcp_info.event & HDCP_EVENT_SECOND_AUTH_START) {
		if (hdcp_second_auth(hdev) < 0)
			goto work_err;
		else
			hdev->hdcp_info.event &= ~HDCP_EVENT_SECOND_AUTH_START;
	}

	if (hdev->hdcp_info.event & HDCP_EVENT_WRITE_AKSV_START) {
		if (hdcp_write_aksv(hdev) < 0)
			goto work_err;
		else
			hdev->hdcp_info.event  &= ~HDCP_EVENT_WRITE_AKSV_START;
	}

	if (hdev->hdcp_info.event & HDCP_EVENT_CHECK_RI_START) {
		if (hdcp_check_ri(hdev) < 0)
			goto work_err;
		else
			hdev->hdcp_info.event &= ~HDCP_EVENT_CHECK_RI_START;
	}
	return;
work_err:
	if (!hdev->hdcp_info.hdcp_start)
		return;
	if (!is_hdmi_streaming(hdev))
		return;

	hdcp_reset_auth(hdev);
}

/* HDCP APIs for hdmi driver */
irqreturn_t hdcp_irq_handler(struct hdmi_device *hdev)
{
	struct device *dev = hdev->dev;
	u32 event = 0;
	u8 flag;
	event = 0;

	if (!hdev->streaming) {
		hdev->hdcp_info.event		= HDCP_EVENT_STOP;
		hdev->hdcp_info.auth_status	= NOT_AUTHENTICATED;
		return IRQ_HANDLED;
	}

	flag = hdmi_readb(hdev, HDMI_STATUS);

	if (flag & HDMI_WTFORACTIVERX_INT_OCC) {
		event |= HDCP_EVENT_READ_BKSV_START;
		hdmi_write_mask(hdev, HDMI_STATUS, ~0, HDMI_WTFORACTIVERX_INT_OCC);
		hdmi_write(hdev, HDMI_HDCP_I2C_INT, 0x0);
	}

	if (flag & HDMI_WRITE_INT_OCC) {
		event |= HDCP_EVENT_WRITE_AKSV_START;
		hdmi_write_mask(hdev, HDMI_STATUS, ~0, HDMI_WRITE_INT_OCC);
		hdmi_write(hdev, HDMI_HDCP_AN_INT, 0x0);
	}

	if (flag & HDMI_UPDATE_RI_INT_OCC) {
		event |= HDCP_EVENT_CHECK_RI_START;
		hdmi_write_mask(hdev, HDMI_STATUS, ~0, HDMI_UPDATE_RI_INT_OCC);
		hdmi_write(hdev, HDMI_HDCP_RI_INT, 0x0);
	}

	if (flag & HDMI_WATCHDOG_INT_OCC) {
		event |= HDCP_EVENT_SECOND_AUTH_START;
		hdmi_write_mask(hdev, HDMI_STATUS, ~0, HDMI_WATCHDOG_INT_OCC);
		hdmi_write(hdev, HDMI_HDCP_WDT_INT, 0x0);
	}

	if (!event) {
		dev_dbg(dev, "%s: unknown irq\n", __func__);
		return IRQ_HANDLED;
	}

	if (is_hdmi_streaming(hdev)) {
		hdev->hdcp_info.event |= event;
		queue_work(hdev->hdcp_wq, &hdev->work);
	} else {
		hdev->hdcp_info.event		= HDCP_EVENT_STOP;
		hdev->hdcp_info.auth_status	= NOT_AUTHENTICATED;
	}

	return IRQ_HANDLED;
}

int hdcp_prepare(struct hdmi_device *hdev)
{
	hdev->hdcp_wq = create_workqueue("khdcpd");
	if (hdev->hdcp_wq == NULL)
		return -ENOMEM;

	INIT_WORK(&hdev->work, hdcp_work);

	spin_lock_init(&hdev->hdcp_info.reset_lock);

#if defined(CONFIG_VIDEO_EXYNOS_HDCP)
	hdev->hdcp_info.hdcp_enable = 1;
#else
	hdev->hdcp_info.hdcp_enable = 0;
#endif
	return 0;
}

int hdcp_start(struct hdmi_device *hdev)
{
	struct device *dev = hdev->dev;

	hdev->hdcp_info.event = HDCP_EVENT_STOP;
	hdev->hdcp_info.auth_status = NOT_AUTHENTICATED;

	dev_dbg(dev, "%s\n", __func__);

	hdcp_sw_reset(hdev);

	dev_dbg(dev, "%s: stop encryption\n", __func__);

	hdcp_encryption(hdev, 0);

	msleep(120);
	if (hdcp_loadkey(hdev) < 0)
		return -1;

	hdmi_write(hdev, HDMI_GCP_CON, HDMI_GCP_CON_NO_TRAN);
	hdmi_write(hdev, HDMI_STATUS_EN, HDMI_INT_EN_ALL);

	hdmi_write(hdev, HDMI_HDCP_CTRL1, HDMI_HDCP_CP_DESIRED_EN);

	hdmi_set_int_mask(hdev, HDMI_INTC_EN_HDCP, 1);

	hdev->hdcp_info.hdcp_start = 1;

	return 0;
}

int hdcp_stop(struct hdmi_device *hdev)
{
	struct device *dev = hdev->dev;
	u8 val;

	dev_dbg(dev, "%s\n", __func__);

	hdmi_set_int_mask(hdev, HDMI_INTC_EN_HDCP, 0);

	hdev->hdcp_info.event		= HDCP_EVENT_STOP;
	hdev->hdcp_info.auth_status	= NOT_AUTHENTICATED;
	hdev->hdcp_info.hdcp_start	= false;

	hdmi_writeb(hdev, HDMI_HDCP_CTRL1, 0x0);

	hdmi_sw_hpd_enable(hdev, 0);

	val = HDMI_UPDATE_RI_INT_EN | HDMI_WRITE_INT_EN |
		HDMI_WATCHDOG_INT_EN | HDMI_WTFORACTIVERX_INT_EN;
	hdmi_write_mask(hdev, HDMI_STATUS_EN, 0, val);
	hdmi_write_mask(hdev, HDMI_STATUS_EN, ~0, val);

	hdmi_write_mask(hdev, HDMI_STATUS, ~0, HDMI_INT_EN_ALL);

	dev_dbg(dev, "%s: stop encryption\n", __func__);
	hdcp_encryption(hdev, 0);

	hdmi_writeb(hdev, HDMI_HDCP_CHECK_RESULT, HDMI_HDCP_CLR_ALL_RESULTS);

	return 0;
}
