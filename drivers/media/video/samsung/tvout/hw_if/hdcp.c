/* linux/drivers/media/video/samsung/tvout/hw_if/hdcp.c
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * HDCP function for Samsung TVOUT driver
 *
 * This program is free software. you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/sched.h>

#include <mach/regs-hdmi.h>

#include "hw_if.h"
#include "../s5p_tvout_common_lib.h"

#undef tvout_dbg

#ifdef CONFIG_TVOUT_DEBUG
#define tvout_dbg(fmt, ...)						\
do {									\
	if (unlikely(tvout_dbg_flag & (1 << DBG_FLAG_HDCP))) {		\
		printk(KERN_INFO "\t\t[HDCP] %s(): " fmt,		\
			__func__, ##__VA_ARGS__);			\
	}								\
} while (0)
#else
#define tvout_dbg(fmt, ...)
#endif

#define AN_SZ			8
#define AKSV_SZ			5
#define BKSV_SZ			5
#define MAX_KEY_SZ		16

#define BKSV_RETRY_CNT		14
#define BKSV_DELAY		100

#define DDC_RETRY_CNT		400000
#define DDC_DELAY		25

#define KEY_LOAD_RETRY_CNT	1000
#define ENCRYPT_CHECK_CNT	10

#define KSV_FIFO_RETRY_CNT	50
#define KSV_FIFO_CHK_DELAY	100 /* ms */
#define KSV_LIST_RETRY_CNT	10000
#define SHA_1_RETRY_CNT		4

#define BCAPS_SIZE		1
#define BSTATUS_SIZE		2
#define SHA_1_HASH_SIZE		20
#define HDCP_MAX_DEVS		128
#define HDCP_KSV_SIZE		5

#define HDCP_Bksv		0x00
#define HDCP_Ri			0x08
#define HDCP_Aksv		0x10
#define HDCP_Ainfo		0x15
#define HDCP_An			0x18
#define HDCP_SHA1		0x20
#define HDCP_Bcaps		0x40
#define HDCP_BStatus		0x41
#define HDCP_KSVFIFO		0x43

#define KSV_FIFO_READY			(0x1 << 5)

#define MAX_CASCADE_EXCEEDED_ERROR	(-2)
#define MAX_DEVS_EXCEEDED_ERROR		(-3)
#define REPEATER_ILLEGAL_DEVICE_ERROR	(-4)
#define REPEATER_TIMEOUT_ERROR		(-5)

#define MAX_CASCADE_EXCEEDED		(0x1 << 3)
#define MAX_DEVS_EXCEEDED		(0x1 << 7)


#define DDC_BUF_SIZE		32

enum hdcp_event {
	HDCP_EVENT_STOP			= 1 << 0,
	HDCP_EVENT_START		= 1 << 1,
	HDCP_EVENT_READ_BKSV_START	= 1 << 2,
	HDCP_EVENT_WRITE_AKSV_START	= 1 << 4,
	HDCP_EVENT_CHECK_RI_START	= 1 << 8,
	HDCP_EVENT_SECOND_AUTH_START	= 1 << 16
};

enum hdcp_state {
	NOT_AUTHENTICATED,
	RECEIVER_READ_READY,
	BCAPS_READ_DONE,
	BKSV_READ_DONE,
	AN_WRITE_DONE,
	AKSV_WRITE_DONE,
	FIRST_AUTHENTICATION_DONE,
	SECOND_AUTHENTICATION_RDY,
	SECOND_AUTHENTICATION_DONE,
};

struct s5p_hdcp_info {
	u8			is_repeater;
	u32			hdcp_enable;

	spinlock_t		reset_lock;

	enum hdcp_event		event;
	enum hdcp_state		auth_status;

	struct work_struct	work;
};

struct i2c_client *ddc_port;

static bool sw_reset;
extern bool s5p_hdmi_ctrl_status(void);

static struct s5p_hdcp_info hdcp_info = {
	.is_repeater	= false,
	.hdcp_enable	= false,
	.event		= HDCP_EVENT_STOP,
	.auth_status	= NOT_AUTHENTICATED,
};

static struct workqueue_struct *hdcp_wq;

/* start: external functions for HDMI */
extern void __iomem *hdmi_base;


/* end: external functions for HDMI */

/* ddc i2c */
static int s5p_ddc_read(u8 reg, int bytes, u8 *dest)
{
	struct i2c_client *i2c = ddc_port;
	u8 addr = reg;
	int ret, cnt = 0;

	struct i2c_msg msg[] = {
		[0] = {
			.addr = i2c->addr,
			.flags = 0,
			.len = 1,
			.buf = &addr
		},
		[1] = {
			.addr = i2c->addr,
			.flags = I2C_M_RD,
			.len = bytes,
			.buf = dest
		}
	};

	do {
		if (s5p_hdmi_ctrl_status() == false ||
		    !s5p_hdmi_reg_get_hpd_status() ||
		    on_stop_process)
			goto ddc_read_err;

		ret = i2c_transfer(i2c->adapter, msg, 2);

		if (ret < 0 || ret != 2)
			tvout_dbg("ddc : can't read data, retry %d\n", cnt);
		else
			break;

		if (hdcp_info.auth_status == FIRST_AUTHENTICATION_DONE
			|| hdcp_info.auth_status == SECOND_AUTHENTICATION_DONE)
			goto ddc_read_err;

		msleep(DDC_DELAY);
		cnt++;
	} while (cnt < DDC_RETRY_CNT);

	if (cnt == DDC_RETRY_CNT)
		goto ddc_read_err;

	tvout_dbg("ddc : read data ok\n");

	return 0;
ddc_read_err:
	tvout_err("ddc : can't read data, timeout\n");
	return -1;
}

static int s5p_ddc_write(u8 reg, int bytes, u8 *src)
{
	struct i2c_client *i2c = ddc_port;
	u8 msg[bytes + 1];
	int ret, cnt = 0;

	msg[0] = reg;
	memcpy(&msg[1], src, bytes);

	do {
		if (s5p_hdmi_ctrl_status() == false ||
		    !s5p_hdmi_reg_get_hpd_status() ||
		    on_stop_process)
			goto ddc_write_err;

		ret = i2c_master_send(i2c, msg, bytes + 1);

		if (ret < 0 || ret < bytes + 1)
			tvout_dbg("ddc : can't write data, retry %d\n", cnt);
		else
			break;

		msleep(DDC_DELAY);
		cnt++;
	} while (cnt < DDC_RETRY_CNT);

	if (cnt == DDC_RETRY_CNT)
		goto ddc_write_err;

	tvout_dbg("ddc : write data ok\n");
	return 0;
ddc_write_err:
	tvout_err("ddc : can't write data, timeout\n");
	return -1;
}

static ssize_t sysfs_hdcp_ddc_i2c_num_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	int size;
	int ddc_i2c_num = ddc_port->adapter->nr;

	pr_info("%s() ddc_i2c_num : %d\n",
			__func__, ddc_i2c_num);
	size = sprintf(buf, "DDC %d\n", ddc_i2c_num);

	return size;
}

static CLASS_ATTR(ddc_i2c_num, 0664 , sysfs_hdcp_ddc_i2c_num_show, NULL);

static int __devinit s5p_ddc_probe(struct i2c_client *client,
			const struct i2c_device_id *dev_id)
{
	int ret = 0;
	struct class *sec_hdcp;

	ddc_port = client;

	sec_hdcp = class_create(THIS_MODULE, "hdcp");
	if (IS_ERR(sec_hdcp)) {
		pr_err("Failed to create class(sec_hdcp)!\n");
		ret = -ENOMEM;
		goto err_exit1;
	}

	ret = class_create_file(sec_hdcp, &class_attr_ddc_i2c_num);
	if (ret) {
		pr_err("Failed to create device file in sysfs entries!\n");
		ret = -ENOMEM;
		goto err_exit2;
	}

	dev_info(&client->adapter->dev, "attached s5p_ddc "
		"into i2c adapter successfully\n");
	return ret;

err_exit2:
	class_destroy(sec_hdcp);

err_exit1:
	return ret;
}

static int s5p_ddc_remove(struct i2c_client *client)
{
	dev_info(&client->adapter->dev, "detached s5p_ddc "
		"from i2c adapter successfully\n");

	return 0;
}

static int s5p_ddc_suspend(struct i2c_client *cl, pm_message_t mesg)
{
	return 0;
};

static int s5p_ddc_resume(struct i2c_client *cl)
{
	return 0;
};

static struct i2c_device_id ddc_idtable[] = {
	{"s5p_ddc", 0},
};
MODULE_DEVICE_TABLE(i2c, ddc_idtable);

static struct i2c_driver ddc_driver = {
	.driver = {
		.name = "s5p_ddc",
		.owner = THIS_MODULE,
	},
	.id_table	= ddc_idtable,
	.probe		= s5p_ddc_probe,
	.remove		= __devexit_p(s5p_ddc_remove),
	.suspend	= s5p_ddc_suspend,
	.resume		= s5p_ddc_resume,
};

static int __init s5p_ddc_init(void)
{
	return i2c_add_driver(&ddc_driver);
}

static void __exit s5p_ddc_exit(void)
{
	i2c_del_driver(&ddc_driver);
}


module_init(s5p_ddc_init);
module_exit(s5p_ddc_exit);

/* hdcp */
static int s5p_hdcp_encryption(bool on)
{
	u8 reg;
	if (on)
		reg = S5P_HDMI_HDCP_ENC_ENABLE;
	else
		reg = S5P_HDMI_HDCP_ENC_DISABLE;

	writeb(reg, hdmi_base + S5P_HDMI_ENC_EN);
	s5p_hdmi_reg_mute(!on);

	return 0;
}

static int s5p_hdcp_write_key(int sz, int reg, int type)
{
	u8 buff[MAX_KEY_SZ] = {0,};
	int cnt = 0, zero = 0;

	hdmi_read_l(buff, hdmi_base, reg, sz);

	for (cnt = 0; cnt < sz; cnt++)
		if (buff[cnt] == 0)
			zero++;

	if (zero == sz) {
		tvout_dbg("%s : null\n", type == HDCP_An ? "an" : "aksv");
		goto write_key_err;
	}

	if (s5p_ddc_write(type, sz, buff) < 0)
		goto write_key_err;

#ifdef CONFIG_HDCP_DEBUG
	{
		u16 i = 0;

		for (i = 1; i < sz + 1; i++)
			tvout_dbg("%s[%d] : 0x%02x\n",
				type == HDCP_An ? "an" : "aksv", i, buff[i]);
	}
#endif

	return 0;
write_key_err:
	tvout_err("write %s : failed\n", type == HDCP_An ? "an" : "aksv");
	return -1;
}

static int s5p_hdcp_read_bcaps(void)
{
	u8 bcaps = 0;

	if (s5p_ddc_read(HDCP_Bcaps, BCAPS_SIZE, &bcaps) < 0)
		goto bcaps_read_err;

	if (s5p_hdmi_ctrl_status() == false ||
		!s5p_hdmi_reg_get_hpd_status() ||
		on_stop_process)
		goto bcaps_read_err;

	writeb(bcaps, hdmi_base + S5P_HDMI_HDCP_BCAPS);

	if (bcaps & S5P_HDMI_HDCP_BCAPS_REPEATER)
		hdcp_info.is_repeater = 1;
	else
		hdcp_info.is_repeater = 0;

	tvout_dbg("device : %s\n", hdcp_info.is_repeater ? "REPEAT" : "SINK");
	tvout_dbg("[i2c] bcaps : 0x%02x\n", bcaps);
	tvout_dbg("[sfr] bcaps : 0x%02x\n",
		readb(hdmi_base + S5P_HDMI_HDCP_BCAPS));

	return 0;

bcaps_read_err:
	tvout_err("can't read bcaps : timeout\n");
	return -1;
}

static int s5p_hdcp_read_bksv(void)
{
	u8 bksv[BKSV_SZ] = {0, };
	int i = 0, j = 0;
	u32 one = 0, zero = 0, res = 0;
	u32 cnt = 0;

	do {
		if (s5p_ddc_read(HDCP_Bksv, BKSV_SZ, bksv) < 0)
			goto bksv_read_err;

#ifdef CONFIG_HDCP_DEBUG
		for (i = 0; i < BKSV_SZ; i++)
			tvout_dbg("i2c read : bksv[%d]: 0x%x\n", i, bksv[i]);
#endif

		for (i = 0; i < BKSV_SZ; i++) {

			for (j = 0; j < 8; j++) {
				res = bksv[i] & (0x1 << j);

				if (res == 0)
					zero++;
				else
					one++;
			}

		}

		if (s5p_hdmi_ctrl_status() == false ||
		    !s5p_hdmi_reg_get_hpd_status() || on_stop_process)
			goto bksv_read_err;

		if ((zero == 20) && (one == 20)) {
			hdmi_write_l(bksv, hdmi_base,
				S5P_HDMI_HDCP_BKSV_0_0, BKSV_SZ);
			break;
		}
		tvout_dbg("invalid bksv, retry : %d\n", cnt);

		msleep(BKSV_DELAY);
		cnt++;
	} while (cnt < BKSV_RETRY_CNT);

	if (cnt == BKSV_RETRY_CNT)
		goto bksv_read_err;

	tvout_dbg("bksv read OK, retry : %d\n", cnt);
	return 0;

bksv_read_err:
	tvout_err("can't read bksv : timeout\n");
	return -1;
}

static int s5p_hdcp_read_ri(void)
{
	static unsigned long int cnt;
	u8 ri[2] = {0, 0};
	u8 rj[2] = {0, 0};

	cnt++;
	ri[0] = readb(hdmi_base + S5P_HDMI_HDCP_Ri_0);
	ri[1] = readb(hdmi_base + S5P_HDMI_HDCP_Ri_1);

	if (s5p_ddc_read(HDCP_Ri, 2, rj) < 0)
		goto compare_err;

	tvout_dbg("Rx(ddc) -> rj[0]: 0x%02x, rj[1]: 0x%02x\n",
		rj[0], rj[1]);
	tvout_dbg("Tx(register) -> ri[0]: 0x%02x, ri[1]: 0x%02x\n",
		ri[0], ri[1]);

	if ((ri[0] == rj[0]) && (ri[1] == rj[1]) && (ri[0] | ri[1]))
		writeb(S5P_HDMI_HDCP_Ri_MATCH_RESULT_Y,
			hdmi_base + S5P_HDMI_HDCP_CHECK_RESULT);
	else {
		writeb(S5P_HDMI_HDCP_Ri_MATCH_RESULT_N,
			hdmi_base + S5P_HDMI_HDCP_CHECK_RESULT);
		goto compare_err;
	}

	ri[0] = 0;
	ri[1] = 0;
	rj[0] = 0;
	rj[1] = 0;

	tvout_dbg("ri, ri' : matched\n");

	return 0;
compare_err:
	hdcp_info.event		= HDCP_EVENT_STOP;
	hdcp_info.auth_status	= NOT_AUTHENTICATED;
	tvout_err("read ri : failed - missmatch "
			"Rx(ddc) rj[0]:0x%02x, rj[1]:0x%02x "
			"Tx(register) ri[0]:0x%02x, ri[1]:0x%02x "
			"cnt = %lu\n",
			rj[0], rj[1], ri[0], ri[1], cnt);
	msleep(10);
	return -1;
}

static void s5p_hdcp_reset_sw(void)
{
	u8 reg;

	sw_reset = true;
	reg = s5p_hdmi_reg_intc_get_enabled();

	s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_PLUG, 0);
	s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_UNPLUG, 0);

	s5p_hdmi_reg_sw_hpd_enable(true);
	s5p_hdmi_reg_set_hpd_onoff(false);
	s5p_hdmi_reg_set_hpd_onoff(true);
	s5p_hdmi_reg_sw_hpd_enable(false);

	if (reg & 1<<HDMI_IRQ_HPD_PLUG)
		s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_PLUG, 1);
	if (reg & 1<<HDMI_IRQ_HPD_UNPLUG)
		s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_UNPLUG, 1);

	sw_reset = false;
}

static void s5p_hdcp_reset_auth(void)
{
	u8 reg;
	unsigned long spin_flags;

	if (s5p_hdmi_ctrl_status() == false ||
		!s5p_hdmi_reg_get_hpd_status() ||
		on_stop_process)
		return;
	spin_lock_irqsave(&hdcp_info.reset_lock, spin_flags);

	hdcp_info.event		= HDCP_EVENT_STOP;
	hdcp_info.auth_status	= NOT_AUTHENTICATED;

	writeb(0x0, hdmi_base + S5P_HDMI_HDCP_CTRL1);
	writeb(0x0, hdmi_base + S5P_HDMI_HDCP_CTRL2);
	s5p_hdmi_reg_mute(true);

	s5p_hdcp_encryption(false);

	tvout_err("reset authentication\n");

	reg = readb(hdmi_base + S5P_HDMI_STATUS_EN);
	reg &= S5P_HDMI_INT_DIS_ALL;
	writeb(reg, hdmi_base + S5P_HDMI_STATUS_EN);

	writeb(S5P_HDMI_HDCP_CLR_ALL_RESULTS,
				hdmi_base + S5P_HDMI_HDCP_CHECK_RESULT);

	/* need some delay (at least 1 frame) */
	mdelay(16);

	s5p_hdcp_reset_sw();

	reg = readb(hdmi_base + S5P_HDMI_STATUS_EN);
	reg |= S5P_HDMI_WTFORACTIVERX_INT_OCC |
		S5P_HDMI_WATCHDOG_INT_OCC |
		S5P_HDMI_WRITE_INT_OCC |
		S5P_HDMI_UPDATE_RI_INT_OCC;
	writeb(reg, hdmi_base + S5P_HDMI_STATUS_EN);
	writeb(S5P_HDMI_HDCP_CP_DESIRED_EN, hdmi_base + S5P_HDMI_HDCP_CTRL1);
	spin_unlock_irqrestore(&hdcp_info.reset_lock, spin_flags);
}

static int s5p_hdcp_loadkey(void)
{
	u8 reg;
	int cnt = 0;

	writeb(S5P_HDMI_EFUSE_CTRL_HDCP_KEY_READ,
				hdmi_base + S5P_HDMI_EFUSE_CTRL);

	do {
		reg = readb(hdmi_base + S5P_HDMI_EFUSE_STATUS);
		if (reg & S5P_HDMI_EFUSE_ECC_DONE)
			break;
		cnt++;
		mdelay(1);
	} while (cnt < KEY_LOAD_RETRY_CNT);

	if (cnt == KEY_LOAD_RETRY_CNT)
		goto key_load_err;

	reg = readb(hdmi_base + S5P_HDMI_EFUSE_STATUS);

	if (reg & S5P_HDMI_EFUSE_ECC_FAIL)
		goto key_load_err;

	tvout_dbg("load key : OK\n");
	return 0;
key_load_err:
	tvout_err("can't load key\n");
	return -1;
}

static int s5p_hdmi_start_encryption(void)
{
	u8 reg;
	u32 cnt = 0;

	do {
		reg = readb(hdmi_base + S5P_HDMI_SYS_STATUS);

		if (reg & S5P_HDMI_AUTHEN_ACK_AUTH) {
			s5p_hdcp_encryption(true);
			break;
		}

		mdelay(1);

		cnt++;
	} while (cnt < ENCRYPT_CHECK_CNT);

	if (cnt == ENCRYPT_CHECK_CNT)
		goto encrypt_err;


	tvout_dbg("encrypt : start\n");
	return 0;

encrypt_err:
	s5p_hdcp_encryption(false);
	tvout_err("encrypt : failed\n");
	return -1;
}

static int s5p_hdmi_check_repeater(void)
{
	int reg = 0;
	int cnt = 0, cnt2 = 0;

	u8 bcaps = 0;
	u8 status[BSTATUS_SIZE] = {0, 0};
	u8 rx_v[SHA_1_HASH_SIZE] = {0};
	u8 ksv_list[HDCP_MAX_DEVS * HDCP_KSV_SIZE] = {0};

	u32 dev_cnt;

	memset(rx_v, 0x0, SHA_1_HASH_SIZE);
	memset(ksv_list, 0x0, HDCP_MAX_DEVS * HDCP_KSV_SIZE);

	do {
		if (s5p_hdcp_read_bcaps() < 0)
			goto check_repeater_err;

		bcaps = readb(hdmi_base + S5P_HDMI_HDCP_BCAPS);

		if (bcaps & KSV_FIFO_READY)
			break;

		msleep(KSV_FIFO_CHK_DELAY);

		cnt++;
	} while (cnt < KSV_FIFO_RETRY_CNT);

	if (cnt == KSV_FIFO_RETRY_CNT) {
		tvout_dbg("repeater : ksv fifo not ready, timeout error");
		tvout_dbg(", retries : %d\n", cnt);
		return REPEATER_TIMEOUT_ERROR;
	}

	tvout_dbg("repeater : ksv fifo ready\n");
	tvout_dbg(", retries : %d\n", cnt);


	if (s5p_ddc_read(HDCP_BStatus, BSTATUS_SIZE, status) < 0)
		goto check_repeater_err;

	if (status[1] & MAX_CASCADE_EXCEEDED)
		return MAX_CASCADE_EXCEEDED_ERROR;
	else if (status[0] & MAX_DEVS_EXCEEDED)
		return MAX_DEVS_EXCEEDED_ERROR;

	writeb(status[0], hdmi_base + S5P_HDMI_HDCP_BSTATUS_0);
	writeb(status[1], hdmi_base + S5P_HDMI_HDCP_BSTATUS_1);

	tvout_dbg("status[0] :0x%02x\n", status[0]);
	tvout_dbg("status[1] :0x%02x\n", status[1]);

	dev_cnt = status[0] & 0x7f;

	tvout_dbg("repeater : dev cnt = %d\n", dev_cnt);

	if (dev_cnt) {

		if (s5p_ddc_read(HDCP_KSVFIFO, dev_cnt * HDCP_KSV_SIZE,
				ksv_list) < 0)
			goto check_repeater_err;

		cnt = 0;

		do {
			hdmi_write_l(&ksv_list[cnt*5], hdmi_base,
				S5P_HDMI_HDCP_RX_KSV_0_0, HDCP_KSV_SIZE);

			reg = S5P_HDMI_HDCP_KSV_WRITE_DONE;

			if (cnt == dev_cnt - 1)
				reg |= S5P_HDMI_HDCP_KSV_END;

			writeb(reg, hdmi_base + S5P_HDMI_HDCP_KSV_LIST_CON);

			if (cnt < dev_cnt - 1) {
				cnt2 = 0;
				do {
					reg = readb(hdmi_base
						+ S5P_HDMI_HDCP_KSV_LIST_CON);

					if (reg & S5P_HDMI_HDCP_KSV_READ)
						break;
					cnt2++;
				} while (cnt2 < KSV_LIST_RETRY_CNT);

				if (cnt2 == KSV_LIST_RETRY_CNT)
					tvout_dbg("ksv list not readed\n");
			}
			cnt++;
		} while (cnt < dev_cnt);
	} else {
		writeb(S5P_HDMI_HDCP_KSV_LIST_EMPTY,
			hdmi_base + S5P_HDMI_HDCP_KSV_LIST_CON);
	}

	if (s5p_ddc_read(HDCP_SHA1, SHA_1_HASH_SIZE, rx_v) < 0)
		goto check_repeater_err;

#ifdef S5P_HDCP_DEBUG
	for (i = 0; i < SHA_1_HASH_SIZE; i++)
		tvout_dbg("[i2c] SHA-1 rx :: %02x\n", rx_v[i]);
#endif

	hdmi_write_l(rx_v, hdmi_base, S5P_HDMI_HDCP_RX_SHA1_0_0,
		SHA_1_HASH_SIZE);

	reg = readb(hdmi_base + S5P_HDMI_HDCP_SHA_RESULT);
	if (reg & S5P_HDMI_HDCP_SHA_VALID_RD) {
		if (reg & S5P_HDMI_HDCP_SHA_VALID) {
			tvout_dbg("SHA-1 result : OK\n");
			writeb(0x0, hdmi_base + S5P_HDMI_HDCP_SHA_RESULT);
		} else {
			tvout_dbg("SHA-1 result : not vaild\n");
			writeb(0x0, hdmi_base + S5P_HDMI_HDCP_SHA_RESULT);
			goto check_repeater_err;
		}
	} else {
		tvout_dbg("SHA-1 result : not ready\n");
		writeb(0x0, hdmi_base + S5P_HDMI_HDCP_SHA_RESULT);
		goto check_repeater_err;
	}

	tvout_dbg("check repeater : OK\n");
	return 0;
check_repeater_err:
	tvout_err("check repeater : failed\n");
	return -1;
}

int s5p_hdcp_stop(void)
{
	u32  sfr_val = 0;

	tvout_dbg("HDCP ftn. Stop!!\n");

	s5p_hdmi_reg_intc_enable(HDMI_IRQ_HDCP, 0);

	hdcp_info.event		= HDCP_EVENT_STOP;
	hdcp_info.auth_status	= NOT_AUTHENTICATED;
	hdcp_info.hdcp_enable	= false;

	writeb(0x0, hdmi_base + S5P_HDMI_HDCP_CTRL1);

	s5p_hdmi_reg_sw_hpd_enable(false);

	sfr_val = readb(hdmi_base + S5P_HDMI_STATUS_EN);
	sfr_val &= S5P_HDMI_INT_DIS_ALL;
	writeb(sfr_val, hdmi_base + S5P_HDMI_STATUS_EN);

	sfr_val = readb(hdmi_base + S5P_HDMI_SYS_STATUS);
	sfr_val |= S5P_HDMI_INT_EN_ALL;
	writeb(sfr_val, hdmi_base + S5P_HDMI_SYS_STATUS);

	tvout_dbg("Stop Encryption by Stop!!\n");
	s5p_hdcp_encryption(false);

	writeb(S5P_HDMI_HDCP_Ri_MATCH_RESULT_N,
			hdmi_base + S5P_HDMI_HDCP_CHECK_RESULT);
	writeb(S5P_HDMI_HDCP_CLR_ALL_RESULTS,
			hdmi_base + S5P_HDMI_HDCP_CHECK_RESULT);

	return 0;
}
#ifdef CONFIG_SAMSUNG_MHL_9290
extern void sii9234_tmds_reset(void);
#endif
int s5p_hdcp_start(void)
{
	u32  sfr_val;

	hdcp_info.event		= HDCP_EVENT_STOP;
	hdcp_info.auth_status	= NOT_AUTHENTICATED;

	tvout_dbg("HDCP ftn. Start\n");

	s5p_hdcp_reset_sw();

	tvout_dbg("Stop Encryption by Start\n");

	s5p_hdcp_encryption(false);

	msleep(120);
	if (s5p_hdcp_loadkey() < 0)
		return -1;

	writeb(S5P_HDMI_GCP_CON_NO_TRAN, hdmi_base + S5P_HDMI_GCP_CON);
	writeb(S5P_HDMI_INT_EN_ALL, hdmi_base + S5P_HDMI_STATUS_EN);

	sfr_val = S5P_HDMI_HDCP_CP_DESIRED_EN;
	writeb(sfr_val, hdmi_base + S5P_HDMI_HDCP_CTRL1);

	s5p_hdmi_reg_intc_enable(HDMI_IRQ_HDCP, 1);

	hdcp_info.hdcp_enable = 1;
#ifdef CONFIG_SAMSUNG_MHL_9290
	sii9234_tmds_reset();
#endif
	return 0;
}

static int s5p_hdcp_bksv(void)
{
	tvout_dbg("bksv start : start\n");

	hdcp_info.auth_status = RECEIVER_READ_READY;

	msleep(100);

	if (s5p_hdcp_read_bcaps() < 0)
		goto bksv_start_err;

	hdcp_info.auth_status = BCAPS_READ_DONE;

	if (s5p_hdcp_read_bksv() < 0)
		goto bksv_start_err;

	hdcp_info.auth_status = BKSV_READ_DONE;

	tvout_dbg("bksv start : OK\n");

	return 0;

bksv_start_err:
	tvout_err("bksv start : failed\n");
	msleep(100);
	return -1;
}

static int s5p_hdcp_second_auth(void)
{
	int ret = 0;

	tvout_dbg("second auth : start\n");

	if (!hdcp_info.hdcp_enable)
		goto second_auth_err;

	if (s5p_hdmi_ctrl_status() == false ||
	    !s5p_hdmi_reg_get_hpd_status() || on_stop_process)
		goto second_auth_err;

	ret = s5p_hdmi_check_repeater();
	if (ret)
		goto second_auth_err;

	hdcp_info.auth_status = SECOND_AUTHENTICATION_DONE;
	s5p_hdmi_start_encryption();

	tvout_dbg("second auth : OK\n");
	return 0;

second_auth_err:
	hdcp_info.auth_status = NOT_AUTHENTICATED;
	tvout_err("second auth : failed\n");
	return -1;
}

static int s5p_hdcp_write_aksv(void)
{
	tvout_dbg("aksv start : start\n");

	if (hdcp_info.auth_status != BKSV_READ_DONE) {
		tvout_dbg("aksv start : bksv is not ready\n");
		goto aksv_write_err;
	}
	if (s5p_hdmi_ctrl_status() == false ||
	    !s5p_hdmi_reg_get_hpd_status() || on_stop_process)
		goto aksv_write_err;

	if (s5p_hdcp_write_key(AN_SZ, S5P_HDMI_HDCP_An_0_0, HDCP_An) < 0)
		goto aksv_write_err;

	hdcp_info.auth_status = AN_WRITE_DONE;

	tvout_dbg("write an : done\n");

	if (s5p_hdcp_write_key(AKSV_SZ, S5P_HDMI_HDCP_AKSV_0_0, HDCP_Aksv) < 0)
		goto aksv_write_err;

	msleep(100);

	hdcp_info.auth_status = AKSV_WRITE_DONE;

	tvout_dbg("write aksv : done\n");
	tvout_dbg("aksv start : OK\n");
	return 0;

aksv_write_err:
	tvout_err("aksv start : failed\n");
	return -1;
}

static int s5p_hdcp_check_ri(void)
{
	tvout_dbg("ri check : start\n");

	if (hdcp_info.auth_status < AKSV_WRITE_DONE) {
		tvout_dbg("ri check : not ready\n");
		goto check_ri_err;
	}

	if (s5p_hdmi_ctrl_status() == false ||
	    !s5p_hdmi_reg_get_hpd_status() || on_stop_process)
		goto check_ri_err;

	if (s5p_hdcp_read_ri() < 0)
		goto check_ri_err;

	if (hdcp_info.is_repeater)
		hdcp_info.auth_status
			= SECOND_AUTHENTICATION_RDY;
	else {
		hdcp_info.auth_status
			= FIRST_AUTHENTICATION_DONE;
		s5p_hdmi_start_encryption();
	}

	tvout_dbg("ri check : OK\n");
	return 0;

check_ri_err:
	tvout_err("ri check : failed\n");
	return -1;
}

static void s5p_hdcp_work(void *arg)
{
	s5p_tvout_mutex_lock();
	if (!hdcp_info.hdcp_enable || s5p_hdmi_ctrl_status() == false ||
	    !s5p_hdmi_reg_get_hpd_status() || on_stop_process) {
		s5p_tvout_mutex_unlock();
		return;
	}

	if (hdcp_info.event & HDCP_EVENT_READ_BKSV_START) {
		if (s5p_hdcp_bksv() < 0)
			goto work_err;
		else
			hdcp_info.event &= ~HDCP_EVENT_READ_BKSV_START;
	}

	if (hdcp_info.event & HDCP_EVENT_SECOND_AUTH_START) {
		if (s5p_hdcp_second_auth() < 0)
			goto work_err;
		else
			hdcp_info.event &= ~HDCP_EVENT_SECOND_AUTH_START;
	}

	if (hdcp_info.event & HDCP_EVENT_WRITE_AKSV_START) {
		if (s5p_hdcp_write_aksv() < 0)
			goto work_err;
		else
			hdcp_info.event  &= ~HDCP_EVENT_WRITE_AKSV_START;
	}

	if (hdcp_info.event & HDCP_EVENT_CHECK_RI_START) {
		if (s5p_hdcp_check_ri() < 0)
			goto work_err;
		else
			hdcp_info.event &= ~HDCP_EVENT_CHECK_RI_START;
	}
	s5p_tvout_mutex_unlock();
	return;
work_err:
	if (!hdcp_info.hdcp_enable || s5p_hdmi_ctrl_status() == false ||
	    !s5p_hdmi_reg_get_hpd_status() || on_stop_process)	{
		s5p_tvout_mutex_unlock();
		return;
	}
	s5p_hdcp_reset_auth();
	s5p_tvout_mutex_unlock();
}

irqreturn_t s5p_hdcp_irq_handler(int irq, void *dev_id)
{
	u32 event = 0;
	u8 flag;

	event = 0;

	if (s5p_hdmi_ctrl_status() == false) {
		hdcp_info.event		= HDCP_EVENT_STOP;
		hdcp_info.auth_status	= NOT_AUTHENTICATED;
		tvout_dbg("[WARNING] s5p_hdmi_ctrl_status fail\n");
		return IRQ_HANDLED;
	}

	flag = readb(hdmi_base + S5P_HDMI_SYS_STATUS);
	tvout_dbg("flag = 0x%x\n", flag);

	if (flag & S5P_HDMI_WTFORACTIVERX_INT_OCC) {
		event |= HDCP_EVENT_READ_BKSV_START;
		writeb(flag | S5P_HDMI_WTFORACTIVERX_INT_OCC,
			 hdmi_base + S5P_HDMI_SYS_STATUS);
		writeb(0x0, hdmi_base + S5P_HDMI_HDCP_I2C_INT);
	}

	if (flag & S5P_HDMI_WRITE_INT_OCC) {
		event |= HDCP_EVENT_WRITE_AKSV_START;
		writeb(flag | S5P_HDMI_WRITE_INT_OCC,
			hdmi_base + S5P_HDMI_SYS_STATUS);
		writeb(0x0, hdmi_base + S5P_HDMI_HDCP_AN_INT);
	}

	if (flag & S5P_HDMI_UPDATE_RI_INT_OCC) {
		event |= HDCP_EVENT_CHECK_RI_START;
		writeb(flag | S5P_HDMI_UPDATE_RI_INT_OCC,
			hdmi_base + S5P_HDMI_SYS_STATUS);
		writeb(0x0, hdmi_base + S5P_HDMI_HDCP_RI_INT);
	}

	if (flag & S5P_HDMI_WATCHDOG_INT_OCC) {
		event |= HDCP_EVENT_SECOND_AUTH_START;
		writeb(flag | S5P_HDMI_WATCHDOG_INT_OCC,
			hdmi_base + S5P_HDMI_SYS_STATUS);
		writeb(0x0, hdmi_base + S5P_HDMI_HDCP_WDT_INT);
	}

	if (!event) {
		tvout_dbg("unknown irq\n");
		return IRQ_HANDLED;
	}

	if (hdcp_info.hdcp_enable && s5p_hdmi_ctrl_status() == true &&
	    s5p_hdmi_reg_get_hpd_status() && !on_stop_process) {
		hdcp_info.event |= event;
		queue_work_on(0, hdcp_wq, &hdcp_info.work);
	} else {
		hdcp_info.event		= HDCP_EVENT_STOP;
		hdcp_info.auth_status	= NOT_AUTHENTICATED;
	}

	return IRQ_HANDLED;
}

int s5p_hdcp_init(void)
{
	hdcp_wq = create_freezable_workqueue("hdcp work");
	if (!hdcp_wq)
		return -1;
	INIT_WORK(&hdcp_info.work, (work_func_t) s5p_hdcp_work);

	spin_lock_init(&hdcp_info.reset_lock);

	s5p_hdmi_reg_intc_set_isr(s5p_hdcp_irq_handler,
					(u8) HDMI_IRQ_HDCP);

	return 0;
}

int s5p_hdcp_encrypt_stop(bool on)
{
	u32 reg;
	unsigned long spin_flags;

	tvout_dbg("\n");
	spin_lock_irqsave(&hdcp_info.reset_lock, spin_flags);


	if (s5p_hdmi_ctrl_status() == false) {
		hdcp_info.event	= HDCP_EVENT_STOP;
		hdcp_info.auth_status = NOT_AUTHENTICATED;
		spin_unlock_irqrestore(&hdcp_info.reset_lock, spin_flags);
		return -1;
	}

	if (hdcp_info.hdcp_enable) {
		writeb(0x0, hdmi_base + S5P_HDMI_HDCP_I2C_INT);
		writeb(0x0, hdmi_base + S5P_HDMI_HDCP_AN_INT);
		writeb(0x0, hdmi_base + S5P_HDMI_HDCP_RI_INT);
		writeb(0x0, hdmi_base + S5P_HDMI_HDCP_WDT_INT);

		s5p_hdcp_encryption(false);

		if (!sw_reset) {
			reg = readb(hdmi_base + S5P_HDMI_HDCP_CTRL1);

			if (on) {
				writeb(reg | S5P_HDMI_HDCP_CP_DESIRED_EN,
					hdmi_base + S5P_HDMI_HDCP_CTRL1);
				s5p_hdmi_reg_intc_enable(HDMI_IRQ_HDCP, 1);
			} else {
				hdcp_info.event	= HDCP_EVENT_STOP;
				hdcp_info.auth_status = NOT_AUTHENTICATED;

				writeb(reg & ~S5P_HDMI_HDCP_CP_DESIRED_EN,
					hdmi_base + S5P_HDMI_HDCP_CTRL1);
				s5p_hdmi_reg_intc_enable(HDMI_IRQ_HDCP, 0);
			}
		}

		tvout_dbg("stop encryption by HPD\n");
	}

	spin_unlock_irqrestore(&hdcp_info.reset_lock, spin_flags);

	return 0;
}

void s5p_hdcp_flush_work(void)
{
	flush_workqueue(hdcp_wq);
}
