/*
 * ELAN HID-I2C TouchScreen driver.
 *
 * Copyright (C) 2014 Elan Microelectronics Corporation.
 * Chuming Zhang <chuming.zhang@elanic.com.cn>
 *
 *
 * This software is licensed under the terms of the GNU General Public                                                                
 * License version 2, as published by the Free Software Foundation, and                                                               
 * may be copied, distributed, and modified under those terms.                                                                        
 *                                                                                                                                    
 * This program is distributed in the hope that it will be useful,                                                                    
 * but WITHOUT ANY WARRANTY; without even the implied warranty of                                                                     
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                                                                      
 * GNU General Public License for more details.                                                                                       
 *                                                                                                                                    
*/

#include <linux/module.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
//#include <linux/wakelock.h>
#include <linux/pm_wakeup.h>
#include <linux/string.h>
#include <linux/delay.h>
#include "elan_ts.h"




#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,unsigned long event, void *data);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
static void elan_ts_early_suspend(struct early_suspend *h);
static void elan_ts_late_resume(struct early_suspend *h);
#endif

//define private data
struct elan_ts_data *private_ts;
unsigned long delay = HZ;

/************************key event define**************************/
static const int key_value[] = {KEY_MENU, KEY_HOMEPAGE, KEY_BACK};

void elan_ts_hw_reset(struct ts_chip_hw_info *hw_info)
{
	gpio_set_value(hw_info->rst_gpio, 1);
	msleep(10);
	gpio_set_value(hw_info->rst_gpio, 0);
	msleep(100);
	gpio_set_value(hw_info->rst_gpio, 1);
        printk("[elan] elan_ts_hw_reset()\n");
}

void elan_switch_irq(struct elan_ts_data *ts, int on)
{
//	struct elan_ts_data *ts = private_ts;

	dev_err(&ts->client->dev,
		"[elan] %s enter, irq = %d, on = %d, irq_lock_flag=%d\n",
		__func__, ts->hw_info.irq_num, on, ts->irq_lock_flag);
	mutex_lock(&ts->irq_mutex);
	if (on) { 
		if(ts->irq_lock_flag == 1) {
			enable_irq(ts->hw_info.irq_num);
			ts->irq_lock_flag = 0;
		}
	} else {
		if(ts->irq_lock_flag == 0) {
			disable_irq(ts->hw_info.irq_num);
			ts->irq_lock_flag = 1;
		}
	}
	mutex_unlock(&ts->irq_mutex);
}

static int elan_poll_int(void)
{
	int status = 0, retry = 50;//20;

	do {
		status = gpio_get_value(private_ts->hw_info.intr_gpio);
		if (status == 0)
			break;
		retry--;
		msleep(10);//msleep(20);
	} while (status == 1 && retry > 0);

	if (status > 0)
		dev_info(&private_ts->client->dev,
				"%s: poll interrupt status %s\n",\
				__func__, status == 1 ? "high" : "low");

	return status == 0 ? 0 : -ETIMEDOUT;
}

static int elan_i2c_send(const uint8_t *buf, int count)
{
	int ret = -1;
	int retries = 0;
	struct i2c_client *client = private_ts->client;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;
	
	msg.addr = client->addr;
	msg.flags = client->flags & I2C_M_TEN;
	msg.len = count;
	msg.buf = (char *)buf;

	while(retries < 5)
	{
		ret = i2c_transfer(adap, &msg, 1);
		if (ret == 1)break;
			retries++;
	}

	/*
	 * If everything went ok (i.e. 1 msg transmitted), return #bytes
	 * transmitted, else error code.
	 */
	return (ret == 1) ? count : ret;
}

static int elan_i2c_recv(uint8_t *buf, int count)
{
	int ret = -1;
	int retries = 0;
	struct i2c_client *client = private_ts->client;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;
	
	msg.addr = client->addr;
	msg.flags = client->flags & I2C_M_TEN;
	msg.flags |= I2C_M_RD;
	msg.len = count;
	msg.buf = buf;

	while(retries < 5)
	{
		ret = i2c_transfer(adap, &msg, 1);
		if(ret == 1)break;
			retries++;
	}
	/*
	 * If everything went ok (i.e. 1 msg received), return #bytes received,
	 * else error code.
	 */

	return (ret == 1) ? count : ret;
}

struct elan_i2c_operation elan_ops = {
	.send	=	elan_i2c_send,
	.recv	=	elan_i2c_recv,
	.poll	=	elan_poll_int,
};

int elan_ic_status(struct i2c_client *client)
{
	uint8_t checkstatus[HID_CMD_LEN] = {0x04, 0x00, 0x23, 0x00, 0x03, 0x18};
	uint8_t buf[HID_RECV_LEN] = {0x00};
	int err = 0;
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	int retry = 3;

	/*HID protocol after reset need delay 300ms*/
	msleep(100);

RETRY:
	err = ts->ops->send(checkstatus,sizeof(checkstatus));
	if (err != sizeof(checkstatus)) {
		dev_err(&client->dev,
				"[elan] ERROR: Send get hid hello cmd fail!len=%d\n", err);
		if((retry--)> 0)
			goto RETRY;
		return -1;
	}
	
	err = ts->ops->poll();//elan_poll_int();
	if (err) {
		dev_err(&client->dev,
				"[elan] ERROR: %s INT status high",__func__);

		if((retry--) > 0)
			goto RETRY;
		return err;
	}

	err = ts->ops->recv(buf, sizeof(buf));
	if (err != sizeof(buf)) {
		dev_err(&client->dev,
				"[elan] ERROR:%s Read Hello Data error\n", __func__);
		if((retry--) > 0)
			goto RETRY;
		return -1;
	}

	dev_err(&client->dev, "[elan] FW Mode = 0x%2x\n",buf[4]);
	if ( HID_FW_NORMAL_MODE == buf[4]) {
		return COMPARE_UPGRADE;
	} else if (HID_FW_RECOVERY_MODE == buf[4]) {
		if (buf[6] != buf[7])
			ts->fw_info.fw_bcl  = buf[7];
		else 
			ts->fw_info.fw_bcl  = buf[4];
		return FORCED_UPGRADE;
	}else
		return UNKNOW_TYPE;
}

static int get_normal_hello(struct i2c_client *client)
{
	int err = 0;
	uint8_t buf[8] = { 0 };
	uint8_t normal_hello[4] = {NORMAL_FW_NORMAL_MODE,NORMAL_FW_NORMAL_MODE,NORMAL_FW_NORMAL_MODE,NORMAL_FW_NORMAL_MODE};
	uint8_t recovery_hello[4] = {NORMAL_FW_NORMAL_MODE,NORMAL_FW_NORMAL_MODE,NORMAL_FW_RECOVERY_MODE,NORMAL_FW_RECOVERY_MODE};
	struct elan_ts_data *ts = i2c_get_clientdata(client);

	err = ts->ops->poll();//elan_poll_int();
	if (err) {
		dev_err(&client->dev,
				"[elan] ERROR: %s INT status high",__func__);
		return -1;
	}

	err = ts->ops->recv(buf, sizeof(buf));
	if (err != sizeof(buf)) {
		dev_err(&client->dev,
				"[elan] ERROR:%s Read Hello Data error\n", __func__);
		return -1;
	}

	if( memcmp(buf,normal_hello,sizeof(normal_hello)) == 0) {
		dev_info(&client->dev, "[elan] hello packet check success!!\n");
		return COMPARE_UPGRADE;
	}
	else if( memcmp(buf,recovery_hello,sizeof(recovery_hello)) == 0) {
		dev_info(&client->dev, "[elan] hello packet check faile!!\n");
		return FORCED_UPGRADE;
	} else {
		dev_info(&client->dev, "[elan] recive hello packet error!!\n");
		return UNKNOW_TYPE;
	}
}

int elan__hello_packet_handler(struct i2c_client *client, int chip_type)
{
	int ret = 0;
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	
	dev_err(&client->dev, "[elan] chip_type =%d\n", chip_type);
	if (HID_TYPE_PROTOCOL == chip_type) {
		ret = elan_ic_status(client);
	} else if (chip_type == NORMAL_TYPE_PROTOCOL) {
		ret = get_normal_hello(client);
	}

	ts->recover = ret;
	dev_err(&client->dev, "[elan] ts->recover =%d\n", ts->recover);
	return ret;
}

static int elan_ts_get_data(struct i2c_client *client, const uint8_t *wbuf,
		size_t wsize, uint8_t *rbuf, size_t rsize)
{
	int err = 0;
	struct elan_ts_data *ts = i2c_get_clientdata(client);

	if (!wbuf || !rbuf)
		return -EINVAL;

	err = ts->ops->send(wbuf, wsize);
	if(err != wsize) {
		dev_err(&client->dev, "[elan] %s send cmd faile\n",__func__);
		err = -1;
		return err;
	}

	err = ts->ops->poll();//elan_poll_int();
	if (err !=  0) {
		dev_err(&client->dev, "[elan] %s Int status hight\n",__func__);
		return err;
	}

	err = ts->ops->recv(rbuf,rsize);
	if (err != rsize) {
		dev_err(&client->dev, "[elan] %s cmd respone error\n",__func__);
		err = -1;
		return err;
	} else {
		if (ts->chip_type == HID_TYPE_PROTOCOL) {
			if ((CMD_S_PKT == rbuf[4])|| (REG_S_PKT == rbuf[4]))
				return 0;
			else
				return -EINVAL;
		} else { 
			if ((CMD_S_PKT == rbuf[0]) || (REG_S_PKT == rbuf[0]))
				return 0;
			else
				return -EINVAL;
		}
	}
}

static int hid_fw_packet_handler(struct i2c_client *client)
{
	const int pen_osr = 260; /*Ntring=256, Warcon: 260*/
	const uint8_t cmd_ver[HID_CMD_LEN]		=   {0x04,0x00,0x23,0x00,0x03,0x00,0x04,0x53,0x00,0x00,0x01};
	const uint8_t cmd_id[HID_CMD_LEN]		=   {0x04,0x00,0x23,0x00,0x03,0x00,0x04,0x53,0xf0,0x00,0x01};
	const uint8_t cmd_bc[HID_CMD_LEN]		=   {0x04,0x00,0x23,0x00,0x03,0x00,0x04,0x53,0x10,0x00,0x01};
	const uint8_t cmd_osr[HID_CMD_LEN]		=	{0x04,0x00,0x23,0x00,0x03,0x00,0x04,0x53,0xD6,0x00,0x01};
	const uint8_t cmd_test_ver[HID_CMD_LEN] =	{0x04,0x00,0x23,0x00,0x03,0x00,0x04,0x53,0xe0,0x00,0x01};
	const uint8_t cmd_whck_ver[HID_CMD_LEN] =	{0x04,0x00,0x23,0x00,0x03,0x00,0x04,0x53,0xd2,0x00,0x01};
	const uint8_t cmd_res[HID_CMD_LEN]      =	{0x04,0x00,0x23,0x00,0x03,0x00,0x04,0x5B,0x00,0x00,0x00,0x00,0x00};
	uint8_t rbuf[HID_RECV_LEN] = {0};
	int err = 0;
	int major, minor;
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	struct elan_fw_info *fw_info = &ts->fw_info;

	/*fw version*/
	err = elan_ts_get_data(client, cmd_ver, sizeof(cmd_ver), rbuf, sizeof(rbuf));
	if ( err ) {
		dev_err(&client->dev, "[elan] %s get fw version failed\n",__func__);
		return err;
	}
	major = ((rbuf[5] & 0x0f) << 4) | ((rbuf[6] & 0xf0) >> 4);
	minor = ((rbuf[6] & 0x0f) << 4) | ((rbuf[7] & 0xf0) >> 4);
	fw_info->fw_ver = major << 8 | minor;


	/*fw id*/
	err = elan_ts_get_data(client, cmd_id, sizeof(cmd_id),rbuf,sizeof(rbuf));
	if ( err ) {
		dev_err(&client->dev, "[elan] %s get fw id failed\n",__func__);
		return err;
	}
	major = ((rbuf[5] & 0x0f) << 4) | ((rbuf[6] & 0xf0) >> 4);
	minor = ((rbuf[6] & 0x0f) << 4) | ((rbuf[7] & 0xf0) >> 4);
	fw_info->fw_id = major << 8 | minor;	

	/*get bootcode version*/
	err = elan_ts_get_data(client, cmd_bc, sizeof(cmd_bc),rbuf,sizeof(rbuf));
	if ( err ) {
		dev_err(&client->dev, "[elan] %s get bootcode version failed\n",__func__);
		return err;
	}
	major = ((rbuf[5] & 0x0f) << 4) | ((rbuf[6] & 0xf0) >> 4);
	minor = ((rbuf[6] & 0x0f) << 4) | ((rbuf[7] & 0xf0) >> 4);
	fw_info->fw_bcv = major << 8 | minor;	
	fw_info->fw_bcl = minor;

	/*get finger osr*/
	err = elan_ts_get_data(client, cmd_osr, sizeof(cmd_osr),rbuf,sizeof(rbuf));
	if ( err ) {
		dev_err(&client->dev, "[elan] %s get finger osr failed\n",__func__);
		return err;
	}
	fw_info->finger_osr = rbuf[7];

	/*get trace num*/
	err = elan_ts_get_data(client, cmd_res, sizeof(cmd_res),rbuf,sizeof(rbuf));
	if ( err ) {
		dev_err(&client->dev, "[elan] %s get finger osr failed\n",__func__);
		return err;
	}
	
	// for ic rx = buf[6], tx = buf[7], rx > tx
	fw_info->rx = rbuf[6];
	fw_info->tx = rbuf[7];
	/*finger resolution*/
	fw_info->finger_xres = (rbuf[6] * 2 - 1) * fw_info->finger_osr;
	fw_info->finger_yres = (rbuf[7] - 1) * fw_info->finger_osr;

	/*pen resolution*/
	fw_info->pen_xres = (rbuf[6] * 2 - 1) * pen_osr;
	fw_info->pen_yres = (rbuf[7] - 1) * pen_osr;

	/*get test ver*/
	err = elan_ts_get_data(client, cmd_test_ver, sizeof(cmd_test_ver),rbuf,sizeof(rbuf));
	if ( err ) {
		dev_err(&client->dev, "[elan] %s get test ver failed\n",__func__);
		return err;
	}
	major = ((rbuf[5] & 0x0f) << 4) | ((rbuf[6] & 0xf0) >> 4);
	minor = ((rbuf[6] & 0x0f) << 4) | ((rbuf[7] & 0xf0) >> 4);
	fw_info->testsolversion  = major << 8 | minor;
	fw_info->testversion = major;
	fw_info->solutionversion = minor;

	/*get whck ver*/
	err = elan_ts_get_data(client, cmd_whck_ver, sizeof(cmd_whck_ver),rbuf,sizeof(rbuf));
	if ( err ) {
		dev_err(&client->dev, "[elan] %s get test ver failed\n",__func__);
		return err;
	}
	major = ((rbuf[5] & 0x0f) << 4) | ((rbuf[6] & 0xf0) >> 4);
	minor = ((rbuf[6] & 0x0f) << 4) | ((rbuf[7] & 0xf0) >> 4);
	fw_info->whck_ver = major << 8 | minor;

	dev_info(&client->dev,
			"[elan] %s fw version:0x%4.4x\n",
			__func__,fw_info->fw_ver);
	dev_info(&client->dev,
			"[elan] %s fw id:0x%4.4x\n",
			__func__,fw_info->fw_id);
	dev_info(&client->dev,
			"[elan] %s bootcode version:0x%4.4x: low byte 0x%2.2x\n",
			__func__,fw_info->fw_bcv,fw_info->fw_bcl);
	dev_info(&client->dev,
			"[elan] %s fw_info->rx, fw_info->tx: %d:%d\n",
			__func__,fw_info->rx, fw_info->tx);
	dev_info(&client->dev,
			"[elan] %s finger x/y resolution:0x%4.4x/0x%4.4x\n",
			__func__,fw_info->finger_xres,fw_info->finger_yres);
	dev_info(&client->dev,
			"[elan] %s pen x/y resolution:0x%4.4x/0x%4.4x\n",
			__func__,fw_info->pen_xres,fw_info->pen_yres);
	dev_info(&client->dev,
			"[elan] %s  testsolversion:testversion :0x%4.4x/0x%4.4x\n",
			__func__,fw_info->testsolversion,fw_info->testversion);
	dev_info(&client->dev,
			"[elan] %s solutionversion:whck_ver : 0x%4.4x/0x%4.4x\n",
			__func__,fw_info->solutionversion,fw_info->whck_ver);

	return err;
}

static int normal_fw_packet_handler(struct i2c_client *client)
{
	int err = 0;
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	int major, minor;
	struct elan_fw_info *fw_info = &ts->fw_info;

	const uint8_t cmd_ver[]	=	{0x53, 0x00, 0x00, 0x01};
	const uint8_t cmd_id[]	=	{0x53, 0xf0, 0x00, 0x01};
	const uint8_t cmd_bc[]	=	{0x53, 0x10, 0x00, 0x01};

#ifndef TWO_LAYER
	const uint8_t cmd_x[]   =	{0x53, 0x60, 0x00, 0x00};
	const uint8_t cmd_y[]   =	{0x53, 0x63, 0x00, 0x00};
	uint8_t rbuf[4] = {0x00};
#else
	const uint8_t cmd_info[] = {0x5B, 0x00, 0x00, 0x00, 0x00, 0x00};
	uint8_t rbuf[17] = {0x00};
#endif

	/*fw version*/
	err = elan_ts_get_data(client, cmd_ver, sizeof(cmd_ver),rbuf,sizeof(rbuf));
	if ( err ) {
		dev_err(&client->dev, "[elan] %s get fw version failed\n",__func__);
		return err;
	}
	major = ((rbuf[1] & 0x0f) << 4) | ((rbuf[2] & 0xf0) >> 4);
	minor = ((rbuf[2] & 0x0f) << 4) | ((rbuf[3] & 0xf0) >> 4);
	fw_info->fw_ver = major << 8 | minor;

	/*fw id*/
	err = elan_ts_get_data(client, cmd_id, sizeof(cmd_id),rbuf,sizeof(rbuf));
	if ( err ) {
		dev_err(&client->dev, "[elan] %s get fw id failed\n",__func__);
		return err;
	}
	major = ((rbuf[1] & 0x0f) << 4) | ((rbuf[2] & 0xf0) >> 4);
	minor = ((rbuf[2] & 0x0f) << 4) | ((rbuf[3] & 0xf0) >> 4);
	fw_info->fw_id = major << 8 | minor;

	/*get boocode version*/
	err = elan_ts_get_data(client, cmd_bc, sizeof(cmd_bc),rbuf,sizeof(rbuf));
	if ( err ) {
		dev_err(&client->dev, "[elan] %s get bootcode version failed\n",__func__);
		return err;
	}
	major = ((rbuf[1] & 0x0f) << 4) | ((rbuf[2] & 0xf0) >> 4);
	minor = ((rbuf[2] & 0x0f) << 4) | ((rbuf[3] & 0xf0) >> 4);
	fw_info->fw_bcv = major << 8 | minor;

#ifndef TWO_LAYER
	err = elan_ts_get_data(client, cmd_x, sizeof(cmd_x),rbuf,sizeof(rbuf));
	if ( err ) {
		dev_err(&client->dev, "[elan] %s get finger xresolution failed\n",__func__);
		return err;
	}
	minor = ((rbuf[2])) | ((rbuf[3] & 0xf0) << 4);
	fw_info->finger_xres = minor;

	err = elan_ts_get_data(client, cmd_y, sizeof(cmd_y),rbuf,sizeof(rbuf));
	if ( err ) {
		dev_err(&client->dev, "[elan] %s get finger yresolution failed\n",__func__);
		return err;
	}
	minor = ((rbuf[2])) | ((rbuf[3] & 0xf0) << 4);
	fw_info->finger_yres = minor;
#else
	err = elan_ts_get_data(client, cmd_info, sizeof(cmd_info),rbuf,sizeof(rbuf));
	if ( err ) {
		dev_err(&client->dev, "[elan] %s get two layer x/y resolution failed\n",__func__);
		return err;
	}
	fw_info->finger_xres = (rbuf[2]+rbuf[6] - 1) * 64;
	fw_info->finger_yres = (rbuf[3]+rbuf[7] - 1) * 64;
#endif

	dev_info(&client->dev,
			"[elan] %s fw version:0x%4.4x\n",
			__func__,fw_info->fw_ver);
	dev_info(&client->dev,
			"[elan] %s fw id:0x%4.4x\n",
			__func__,fw_info->fw_id);
	dev_info(&client->dev,
			"[elan] %s bootcode version:0x%4.4x\n",
			__func__,fw_info->fw_bcv);
	dev_info(&client->dev,
			"[elan] %s finger x/y resolution:0x%4.4x/0x%4.4x\n",
			__func__,fw_info->finger_xres,fw_info->finger_yres);

	return err;
}

int elan__fw_packet_handler(struct i2c_client *client)
{
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	int ret = 0;

	dev_err(&client->dev, "[elan] fw packet handler chip_type %d\n",ts->chip_type);
	if (ts->chip_type == HID_TYPE_PROTOCOL) {
		ret = hid_fw_packet_handler(client);
		if ( ret ) {
			dev_err(&client->dev,
					"[elan] %s HID get fw msg failed\n",__func__);
		}
	} else if (ts->chip_type == NORMAL_TYPE_PROTOCOL) {
		ret = normal_fw_packet_handler(client);
		if ( ret ) {
			dev_err(&client->dev,
					"[elan] %s Normal get fw msg failed\n",__func__);
		}
	}
	return ret;
}

static int elan_ts_recv_data(struct elan_ts_data *ts, uint8_t *buf)
{
	int rc = 0;
	uint8_t rbuf[HID_RECV_LEN]={0};
	struct elan_report_struct *report = &ts->report;
	int i = 0;

	rc = ts->ops->recv(rbuf, sizeof(rbuf));
	if ( rc < 0) {
		dev_err(&ts->client->dev, "[elan] recv report data error [%d] !!\n",rc);
		return -1;
	}

	if (ts->chip_type == HID_TYPE_PROTOCOL) {
		if (rbuf[2] == HID_FID) {
			report->finger.fvalid_num		= rbuf[62];
			report->finger.fbutton_value	= rbuf[63];
			memcpy(buf,rbuf,67);
			if (rbuf[62] > 5) {
				rc = ts->ops->recv(rbuf, sizeof(rbuf));
				if (rc != sizeof(rbuf)) {
					dev_err(&ts->client->dev, "[elan] recv second report data error!!\n");
					return -1;
				}
				report->finger.fbuf_valid_size = 67*2;
				memcpy(buf+58,rbuf+3,67-3);

			} else {
				report->finger.fbuf_valid_size	= 67;
			}
			report->finger.fid				=  HID_FID;
		    report->finger.fsupport_num		= 10;
			report->finger.freport_idx		= 3;
			report->finger.fshift_byte		= 11;
			report->tool_type				= ELAN_FINGER;
		} else if (rbuf[2] == HID_PID) {
			report->stylus.pid					= HID_PID;
			report->stylus.pbuf_valid_size		= rbuf[0];
			report->stylus.pbutton_value		= 0;//rbuf[13];
			report->stylus.preport_idx			= 3;
			report->stylus.tip_status			= (rbuf[3] & 0x33) >> 1;
			report->stylus.inrange_status		= rbuf[3] & 0x02;
			report->stylus.key					= rbuf[3] >> 1;
//			report->stylus.eraser				= rbuf[3] >> 1;
//			report->stylus.inver				= rbuf[3] >> 1;
//			report->stylus.barrel				= rbuf[3] >> 1;
			report->stylus.barrel_tip			= rbuf[3];
			report->tool_type					= ELAN_PEN;
			memcpy(buf,rbuf,report->stylus.pbuf_valid_size);
		}

	} else if (ts->chip_type == NORMAL_TYPE_PROTOCOL) {
		if (rbuf[0] == NOR2_FID) {
			report->finger.fid				= NOR2_FID;
			report->finger.fbuf_valid_size	= NOR2_SIZE;
			report->finger.fsupport_num		= 2;
			report->finger.fvalid_num		= rbuf[7] & 0x03;
			report->finger.freport_idx		= 1;
			report->finger.fbits			= rbuf[7] & 0x03;
		} else if(rbuf[0] == NOR5_FID) {
			report->finger.fid				= NOR5_FID;
			report->finger.fbuf_valid_size	= NOR5_SIZE;
			report->finger.fsupport_num		= 5;
			report->finger.fvalid_num		= rbuf[1] & 0x07;
			report->finger.freport_idx		= 2;
			report->finger.fbits			= rbuf[1] >> 3;
		} else if (rbuf[0] == NOR10_FID) {
			report->finger.fid				= NOR10_FID;
			report->finger.fbuf_valid_size	= NOR10_SIZE;
			report->finger.fsupport_num		= 10;
			report->finger.fvalid_num		= rbuf[2] & 0x0f;
			report->finger.freport_idx		= 3;
			report->finger.fbits			= ((rbuf[2] & 0x30)<<4) | (rbuf[1]);	
		}
		report->finger.fbutton_value		= rbuf[report->finger.fbuf_valid_size - 1];
		report->finger.fshift_byte			= 3;
		report->tool_type					= ELAN_FINGER;
		memcpy(buf,rbuf, report->finger.fbuf_valid_size);
	}

	if ( report->tool_type == ELAN_PEN) {
		for(i = 0; i < report->stylus.pbuf_valid_size/8 + 1; i++) {
			print_log(ts->level,"%02x %02x %02x %02x %02x %02x %02x %02x\n",\
					buf[i*8+0],buf[i*8+1],buf[i*8+2],buf[i*8+3],\
					buf[i*8+4],buf[i*8+5],buf[i*8+6],buf[i*8+7]);
		}
	} else {
		for(i = 0; i < report->finger.fbuf_valid_size/8 + 1; i++) {
			print_log(ts->level,"%02x %02x %02x %02x %02x %02x %02x %02x\n",\
					buf[i*8+0],buf[i*8+1],buf[i*8+2],buf[i*8+3],\
					buf[i*8+4],buf[i*8+5],buf[i*8+6],buf[i*8+7]);
		}
	}

	return rc;	
}

static inline int elan_ts_fparse_xy(uint8_t *data, uint16_t *x, uint16_t *y, const int type)
{
	*x = *y  = 0;

	if (type == HID_FID) {
		*x = (data[6]);
		*x <<= 8;
		*x |= data[5];

		*y = (data[10]);
		*y <<= 8;
		*y |= data[9];
	} else {
		*x = (data[0] & 0xf0);
		*x <<= 4;
		*x |= data[1];
		*y = (data[0] & 0x0f);
		*y <<= 8;
		*y |= data[2];
	}
	
	return 0;

}

static inline int elan_ts_pparse_xy(uint8_t *data, uint16_t *x, uint16_t *y, uint16_t *p)
{
	*x = *y = *p = 0;
	
	*x = data[5];
	*x <<= 8;
	*x |= data[4];

	*y = data[7];
	*y <<= 8;
	*y |= data[6];

	*p = data[9];
	*p <<= 8;
	*p |= data[8];
	
	

	return 0;
}

static void  elants_a_report(struct elan_ts_data *ts, uint8_t *buf)
{
	struct elan_report_struct *report = &ts->report;
	struct elan_finger_struct finger = report->finger;
	struct elan_stylus_struct stylus = report->stylus;
	int fbits = finger.fbits;
	int reportid = 0;
	int valid_num =  finger.fvalid_num;
	uint16_t x = 0, y = 0, p = 0;
	int fbit = 0;
	static int pkey = 0;

	if (report->tool_type == ELAN_FINGER) {
		for (reportid = 0; reportid < finger.fvalid_num; reportid++ ) {
			if (finger.fid ==  HID_FID) {  /*hid over i2c protocol*/
				
				fbits = (buf[finger.freport_idx] & 0x03);
				if (fbits) {
					fbit  = (((buf[finger.freport_idx] & 0xfc) >> 2) - 1);
					elan_ts_fparse_xy(&buf[finger.freport_idx], &y, &x, finger.fid);
					
					x = 800 * x / 2112;
					y = 1200 * y / 3392;

					input_report_key(ts->finger_idev, BTN_TOUCH, 1);
					input_report_key(ts->finger_idev, BTN_TOOL_FINGER, true);
					input_report_abs(ts->finger_idev, ABS_MT_POSITION_X, x);
					input_report_abs(ts->finger_idev, ABS_MT_POSITION_Y, y);
					input_report_abs(ts->finger_idev, ABS_MT_TRACKING_ID, fbit);
					input_mt_sync(ts->finger_idev);
				} else
					valid_num --;
				finger.freport_idx  += 11;

			} else { /*normal i2c protocol*/
				
				if (fbits & 0x01) {

					elan_ts_fparse_xy(&buf[finger.freport_idx], &y, &x, finger.fid);
					input_report_key(ts->finger_idev, BTN_TOUCH, 1);
					input_report_key(ts->finger_idev, BTN_TOOL_FINGER, true);
					input_report_abs(ts->finger_idev, ABS_MT_POSITION_X, x);
					input_report_abs(ts->finger_idev, ABS_MT_POSITION_Y, y);
					input_report_abs(ts->finger_idev, ABS_MT_TRACKING_ID, reportid);
					input_mt_sync(ts->finger_idev);
					
				}else
					valid_num--;
				fbits = fbits >> 1;
				finger.freport_idx  += 3;
			}

		}

		if (!valid_num) {
			input_report_key(ts->finger_idev, BTN_TOUCH, 0);
			input_report_key(ts->finger_idev, BTN_TOOL_FINGER, false);
			input_mt_sync(ts->finger_idev);
		}

		input_sync(ts->finger_idev);
	} else if (report->tool_type == ELAN_PEN) {
		print_log(ts->level,"[elan] stylus.key %d, pkey %d\n",stylus.key,pkey);
		if (stylus.key > 0 || !pkey) {
			switch(stylus.key) {
				case 2:
					pkey =  BTN_STYLUS;
					input_report_key(ts->pen_idev, pkey , 1);
					input_sync(ts->pen_idev);
					break;
				case 3:
					pkey =  BTN_STYLUS2;
					input_report_key(ts->pen_idev, pkey , 1);
					input_sync(ts->pen_idev);
					break;
				case 4:
					pkey = BTN_STYLUS;
					input_report_key(ts->pen_idev, pkey , 1);
					input_sync(ts->pen_idev);
					break;
				case 8:
					pkey = BTN_STYLUS2;
					input_report_key(ts->pen_idev, pkey , 1);
					input_sync(ts->pen_idev);
					break;
				default:
					input_report_key(ts->pen_idev, pkey , 0);
					pkey = 0;
					input_sync(ts->pen_idev);
					break;
			}
		}

		print_log(ts->level, "[elan] stylus.tip_status  %d, stylus.inrange_status %d\n",\
				stylus.tip_status,stylus.inrange_status);
		if (stylus.inrange_status) {
			elan_ts_pparse_xy(&buf[0],&y,&x,&p);

			x = 800  * x / 8580;
			y = 1200 * y / 13780;
			
			input_report_abs(ts->pen_idev, ABS_PRESSURE, p);
			input_report_abs(ts->pen_idev, ABS_X, x);
			input_report_abs(ts->pen_idev, ABS_Y, y);
			dev_info(&ts->client->dev, "[elan] X:Y:P ====%d:%d:%d\n",x,y,p);
		}

		input_report_key(ts->pen_idev, BTN_TOUCH, stylus.tip_status);
		input_report_key(ts->pen_idev, BTN_TOOL_PEN, stylus.tip_status);
		input_sync(ts->pen_idev);
	}
}

static void elants_slot_report(struct elan_ts_data *ts, uint8_t *buf)
{
	
	struct elan_report_struct *report = &ts->report;
	struct elan_finger_struct finger = report->finger;
	struct elan_stylus_struct stylus = report->stylus;
	uint16_t x = 0, y = 0, p = 0;
	int16_t x_tilt_raw = 0, y_tilt_raw = 0;
	int8_t x_tilt = 0, y_tilt = 0;
	int fbits = finger.fbits;
	int fprebits = 0;
	int fbits_tmp = 0;
	int active = 0; 
	int id = 0, reportid = 0;
	int num = finger.fvalid_num;
	static int pkey = 0;

	if (finger.fid > HID_PID)
		fbits_tmp	= fbits;

	if (report->tool_type == ELAN_FINGER) {
		

		if (finger.fid ==  HID_FID) {  /*hid over i2c protocol*/
			for (reportid = 0; reportid < finger.fvalid_num; reportid++ ) {
				active = (buf[finger.freport_idx] & 0x03);	
				/*id = (((buf[finger.freport_idx] & 0xfc) >> 2) -1);*/
				id = (((buf[finger.freport_idx] & 0xfc) >> 2));
				elan_ts_fparse_xy(&buf[finger.freport_idx], &x, &y, finger.fid); //lcm x :y = 720 : 1280 tp x: y = 1296:720
				x = ts->hw_info.screen_x * x / ts->fw_info.finger_xres;
				y = ts->hw_info.screen_y * y / ts->fw_info.finger_yres;
				//x = ts->hw_info.screen_x - x;
				//y = ts->hw_info.screen_y - y;
	
				if (active) { /*finger contact*/
					input_mt_slot(ts->finger_idev, id);
					//input_report_abs(ts->finger_idev, ABS_MT_PRESSURE, 100);
					input_report_abs(ts->finger_idev, ABS_MT_TOUCH_MAJOR, 100);
					input_report_abs(ts->finger_idev, ABS_MT_POSITION_X, x);
					input_report_abs(ts->finger_idev, ABS_MT_POSITION_Y, y);
					input_mt_report_slot_state(ts->finger_idev, MT_TOOL_FINGER, true);
					input_report_key(ts->finger_idev, BTN_TOUCH, 1);
					//dev_info(&ts->client->dev, "[elan] finger X:Y ====%d:%d\n",x,y);
				} else {     /*finger leave*/
					input_mt_slot(ts->finger_idev, id);
					input_mt_report_slot_state(ts->finger_idev, MT_TOOL_FINGER, false);
					//input_report_key(ts->finger_idev, BTN_TOUCH, 0);
					num--;
				}

				finger.freport_idx  += 11;
			}
			//input_mt_sync(ts->finger_idev);
			if(num == 0)
				input_report_key(ts->finger_idev, BTN_TOUCH, 0);
			
			 input_sync(ts->finger_idev);

		} else { /*notmal i2c protocol*/
		
	
			if (fbits || fprebits) { 
				for (reportid = 0; reportid < finger.fvalid_num; reportid++ ) {
					if(fbits&0x01){
						elan_ts_fparse_xy(&buf[finger.freport_idx], &y, &x, finger.fid);
						input_mt_slot(ts->finger_idev, reportid);
						input_mt_report_slot_state(ts->finger_idev, MT_TOOL_FINGER, true);
						input_report_abs(ts->finger_idev, ABS_MT_POSITION_X, x);
						input_report_abs(ts->finger_idev, ABS_MT_POSITION_Y, y);
					}  else if(fprebits&0x01){
						input_mt_slot(ts->finger_idev, id);
						input_mt_report_slot_state(ts->finger_idev, MT_TOOL_FINGER, false);
					}
					finger.freport_idx  += 3;
				}
			}
			fprebits = fbits_tmp;
			input_sync(ts->finger_idev);
		}
	} else if (report->tool_type == ELAN_PEN) {
		if (stylus.key > 0 || !pkey) {
			switch(stylus.key) {
				case 2:
					pkey = BTN_TOOL_RUBBER;
					input_report_key(ts->pen_idev, pkey , 1);
					input_sync(ts->pen_idev);
					break;
				case 3:
					pkey = BTN_TOOL_RUBBER;
					input_report_key(ts->pen_idev, pkey , 1);
					input_sync(ts->pen_idev);
					break;
				case 4:
					pkey = BTN_STYLUS;
					input_report_key(ts->pen_idev, pkey , 1);
					input_sync(ts->pen_idev);
					break;
				case 8:
					pkey = BTN_STYLUS2;
					input_report_key(ts->pen_idev, pkey , 1);
					input_sync(ts->pen_idev);
					break;
				default:
					input_report_key(ts->pen_idev, pkey , 0);
					pkey = 0;
					input_sync(ts->pen_idev);
					break;
			}
		}
		
		print_log(ts->level, "[elan] stylus.inrange_status = %d, stylus.barrel_tip = %d", \
				stylus.inrange_status,stylus.barrel_tip);
		if(stylus.inrange_status) {
			 elan_ts_pparse_xy(&buf[0],&x,&y,&p);
			 x = ts->hw_info.screen_x * x / ts->fw_info.pen_xres;
			 y = ts->hw_info.screen_y * y / ts->fw_info.pen_yres;
			 
			 //x = ts->hw_info.screen_x - x;
			 //y = ts->hw_info.screen_y - y;
			 
			x_tilt_raw = (int16_t)((buf[12] << 8) | buf[11]);
			y_tilt_raw = (int16_t)((buf[14] << 8) | buf[13]);
             		x_tilt = (int8_t)(x_tilt_raw / 100);
             		y_tilt = (int8_t)(y_tilt_raw / 100);
			  

			 input_mt_slot(ts->pen_idev, 0);
			 input_mt_report_slot_state(ts->pen_idev, MT_TOOL_PEN, true);
			 input_report_key(ts->pen_idev, BTN_TOUCH, 1);
			 input_report_abs(ts->pen_idev, ABS_MT_TOOL_TYPE, MT_TOOL_PEN);
			 input_report_abs(ts->pen_idev, ABS_MT_PRESSURE, p);
			 input_report_abs(ts->pen_idev, ABS_MT_POSITION_X, x);
			 input_report_abs(ts->pen_idev, ABS_MT_POSITION_Y, y);
			 input_report_abs(ts->pen_idev, ABS_TILT_X, x_tilt);
			 input_report_abs(ts->pen_idev, ABS_TILT_Y, y_tilt);
			 print_log(ts->level, "[elan] pen X:Y:P:TX:TY ====%d:%d:%d:%d:%d\n",
				   x, y, p, x_tilt, y_tilt);
		} else {
			input_mt_slot(ts->pen_idev, 0);
			input_mt_report_slot_state(ts->pen_idev, MT_TOOL_PEN, false);
			input_report_key(ts->pen_idev, BTN_TOUCH, 0);
			dev_info(&ts->client->dev, "[elan] pen relese!!!!");
		}
	
		input_sync(ts->pen_idev);
	}

}

static int report_mbutton(struct input_dev *idev, int button_value)
{
	static int key;
	
	switch(button_value) {
		case 0x01:
			key = KEY_BACK;
			input_report_key(idev, key, 1);
			break;

		case 0x02:
			key = KEY_HOMEPAGE;
			input_report_key(idev, key, 1);

			break;
		case 0x03:
		case 0x04:
			key = KEY_BACK;
			input_report_key(idev, key, 1);
			break;
		default:
			if (key != 0) {
				input_report_key(idev, key, 0);
				key = 0;
			}
			break;
	}
	input_sync(idev);
	return key;
}



static void elan_ts_hid_report(struct elan_ts_data *ts, uint8_t *buf)
{
	struct elan_report_struct *report = &ts->report;
	int button = report->finger.fbutton_value;
	int pbutton = report->stylus.pbutton_value;
	static int prekey = 0;

	/*for hid protocol finger contat mutual button or pen contact mutual button*/
	/*button priority is higher than other*/
	if ((button != 0 && button != 0xFF) || (pbutton != 0 && pbutton != 0xFF)|| (prekey != 0)) {
		if (ts->report.tool_type == ELAN_FINGER) {
			prekey = report_mbutton(ts->finger_idev,button);
			return;
		} else {
			prekey = report_mbutton(ts->pen_idev,pbutton);
			return;
		}
	}

	if (ts->report_type == PROTOCOL_TYPE_B) {
		elants_slot_report(ts,buf);
	} else {
		elants_a_report(ts,buf);
	}
}

static void elan_ts_normal_report(struct elan_ts_data *ts, uint8_t *buf)
{
	struct elan_report_struct *report = &ts->report;
	int button = report->finger.fbutton_value;
	static int prekey = 0;

	if ((button != 0 && button != 0xFF) || (prekey != 0)) {
		prekey = report_mbutton(ts->finger_idev,button);
		return;
	}
	
	if (ts->report_type == PROTOCOL_TYPE_B) {
		elants_slot_report(ts,buf);
		return;
	} else {
		elants_a_report(ts,buf);
		return;
	}

}

static void elan_ts_report_data(struct elan_ts_data *ts, uint8_t *buf)
{
	switch (ts->chip_type) {
		case HID_TYPE_PROTOCOL:

			elan_ts_hid_report(ts, buf);
			break;
		case NORMAL_TYPE_PROTOCOL:

			elan_ts_normal_report(ts,buf);
			break;
		default:
			dev_err(&ts->client->dev,
					"[elan] unknow type 0x%2x:0x%2x:0x%2x:0x%2x",\
					buf[0],buf[1],buf[2],buf[3]);
			break;
	}
}

static void  elan_ts_work_func(struct work_struct *work)
{
	struct elan_ts_data *ts = 
		container_of(work, struct elan_ts_data, ts_work);
	uint8_t buf[HID_REPORT_MAX_LEN] = {0x00};	
	int rc = 0;

	if(gpio_get_value(ts->hw_info.intr_gpio)) {
		dev_err(&ts->client->dev,"[elan]interrupt jitter\n.");
		return;
	}

	memset(&ts->report, 0, sizeof(ts->report));
	
	rc = elan_ts_recv_data(ts,buf);
	if (rc < 0) {
		dev_err(&ts->client->dev,"[elan]recv data error\n.");
		return;
	}
	
	elan_ts_report_data(ts,buf);
	return;
}


static irqreturn_t elan_ts_irq_handler(int irq, void *dev_id)
{	
	struct elan_ts_data *ts = (struct elan_ts_data*)dev_id;
	
	if (ts->user_handle_irq) { 
		wake_up_interruptible(&ts->elan_userqueue);
		ts->int_val = 0;
		return IRQ_HANDLED;
	} else {
		queue_work(ts->elan_wq,&ts->ts_work);
		return IRQ_HANDLED;
	}
}

static int elan_request_pen_input_dev(struct elan_ts_data *ts)
{
	int err = 0;

	ts->pen_idev = input_allocate_device();
	if (ts->pen_idev == NULL) {
		err = -ENOMEM;
		dev_err(&ts->client->dev,
				"[elan error] Failed to allocate pen device\n");
		return err;
	}
	
	if (ts->report_type == PROTOCOL_TYPE_B) {
		//input_mt_init_slots(ts->pen_idev, 10);
		input_mt_init_slots(ts->pen_idev, FINGERS_NUM,INPUT_MT_DIRECT);
		input_set_abs_params(ts->pen_idev, ABS_MT_TOOL_TYPE, 0, MT_TOOL_MAX, 0, 0);
		input_set_abs_params(ts->pen_idev, ABS_MT_POSITION_X, 0,  ts->hw_info.screen_x, 0, 0);
		input_set_abs_params(ts->pen_idev, ABS_MT_POSITION_Y, 0,  ts->hw_info.screen_y, 0, 0);
		input_set_abs_params(ts->pen_idev, ABS_MT_PRESSURE, 0, 4096, 0, 0);
		input_set_abs_params(ts->pen_idev, ABS_TILT_X, -90, 90, 0, 0);
		input_set_abs_params(ts->pen_idev, ABS_TILT_Y, -90, 90, 0, 0);
	} else {
		__set_bit(BTN_TOOL_PEN, ts->pen_idev->keybit);
		__set_bit(BTN_TOUCH, ts->pen_idev->keybit);
		ts->pen_idev->absbit[0] = BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE);
		input_set_abs_params(ts->pen_idev, ABS_X, 0,  ts->hw_info.screen_x, 0, 0);
		input_set_abs_params(ts->pen_idev, ABS_Y, 0,  ts->hw_info.screen_y, 0, 0);
		input_set_abs_params(ts->pen_idev, ABS_PRESSURE, 0, 4096, 0, 0);
		
		input_set_abs_params(ts->pen_idev, ABS_TILT_X, -90, 90, 0, 0);
		input_set_abs_params(ts->pen_idev, ABS_TILT_Y, -90, 90, 0, 0);
	}	

	ts->pen_idev->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) | BIT_MASK(EV_SYN);

	__set_bit(KEY_BACK, ts->pen_idev->keybit);
	__set_bit(BTN_TOOL_RUBBER, ts->pen_idev->keybit);
	__set_bit(BTN_STYLUS, ts->pen_idev->keybit);
	__set_bit(BTN_STYLUS2, ts->pen_idev->keybit);
	__set_bit(INPUT_PROP_DIRECT, ts->pen_idev->propbit);
	__set_bit(BTN_TOUCH, ts->pen_idev->keybit);
	input_set_abs_params(ts->pen_idev, ABS_MT_TRACKING_ID, 0, 10, 0, 0);
	
	ts->pen_idev->name = "elan_pen";
	ts->pen_idev->phys = "input/ts";
	ts->pen_idev->id.bustype = BUS_I2C;

	err = input_register_device(ts->pen_idev);
	if (err) {
		input_free_device(ts->pen_idev);
		dev_err(&ts->client->dev,
				"unable to register pen input device: %d\n", err);
		return err;
	}
	
	return err;
}

static int elan_request_finger_input_dev(struct elan_ts_data *ts)
{
	int err = 0;
	int i = 0;

	ts->finger_idev = input_allocate_device();
	if (ts->finger_idev == NULL) {
		err = -ENOMEM;
		dev_err(&ts->client->dev,
				"[elan] Failed to allocate input device\n");
		return err;
	}

	ts->finger_idev->evbit[0] = BIT(EV_KEY)|BIT_MASK(EV_REP);

	/*key setting*/
	for (i = 0; i < ARRAY_SIZE(key_value); i++)
		__set_bit(key_value[i], ts->finger_idev->keybit);


	ts->finger_idev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	
	__set_bit(INPUT_PROP_DIRECT, ts->finger_idev->propbit);

	if (ts->report_type == PROTOCOL_TYPE_B) {
		//input_mt_init_slots(ts->finger_idev, 10);
		input_mt_init_slots(ts->finger_idev, FINGERS_NUM,INPUT_MT_DIRECT);
		input_set_abs_params(ts->finger_idev, ABS_MT_TOOL_TYPE, 0, MT_TOOL_MAX, 0, 0);
	} else {
		__set_bit(BTN_TOOL_FINGER, ts->finger_idev->keybit);
	}
	
	dev_info(&ts->client->dev,
			"[elan] %s: x resolution: %d, y resolution: %d\n",
			__func__, ts->fw_info.finger_xres, ts->fw_info.finger_yres);
			
	ts->finger_idev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);													
	input_set_abs_params(ts->finger_idev, ABS_MT_POSITION_X, 0, ts->hw_info.screen_x, 0, 0);
	input_set_abs_params(ts->finger_idev, ABS_MT_POSITION_Y, 0, ts->hw_info.screen_y, 0, 0);
//	input_set_abs_params(ts->finger_idev, ABS_MT_PRESSURE, 0, 255, 0, 0);
//	input_set_abs_params(ts->finger_idev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->finger_idev, ABS_MT_TRACKING_ID, 0, 255, 0, 0);

	ts->finger_idev->name = ELAN_TS_NAME;
	ts->finger_idev->phys = "input/ts";
	ts->finger_idev->id.bustype = BUS_I2C;
	ts->finger_idev->id.vendor = 0x0001;
	ts->finger_idev->id.product = 0x0002;
	ts->finger_idev->id.version = 0x0003;

	err = input_register_device(ts->finger_idev);
	if (err) {
		input_free_device(ts->finger_idev);
		dev_err(&ts->client->dev,
				"[elan]%s: unable to register %s input device\n",
				__func__, ts->finger_idev->name);
		return err;
	}

	return 0;
}

static int elan_ts_register_interrupt(struct elan_ts_data *ts)
{
	int err = 0;

	err = request_threaded_irq(ts->hw_info.irq_num,
			NULL, elan_ts_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			ELAN_TS_NAME, ts);

	if (err < 0)
		dev_err(&ts->client->dev,
				"[elan] %s: request_irq %d failed,err = %d\n",
				__func__, ts->client->irq, err);
	
	return err;
}


static void elan_ic_init_work(struct work_struct *work)
{
	int rc = 0;
	int retry_cnt = 0;
	struct elan_ts_data *ts = private_ts;
	struct i2c_client *client = ts->client;
	
	/*Get FW MSG: ID,VERSION,X_RES,Y_RES,etc*/
	if (ts->recover == COMPARE_UPGRADE) {
		for (retry_cnt = 0; retry_cnt < 3; retry_cnt++) {
			rc = elan__fw_packet_handler(client);
			if (rc < 0)
				dev_err(&client->dev,
						"[elan]%s, fw_packet_handler fail, rc = %d\n",
						__func__, rc);
			else
				break;
		}
		if (retry_cnt >= 3) {
			dev_err(&client->dev,
					"[elan]%s, fw_packet_handler failed,retry out, rc = %d\n",
					__func__,rc);
			return;
		}
	} else {
			dev_err(&client->dev,
					"[elan]%s, fw into recovery mode force update, rc = %d\n", __func__,rc);
	}

#ifdef IAP_PORTION

	dev_err(&ts->client->dev, "[elan]Start IAP Flow!!!\n");
	ts->power_lock = 1; //skip resume / suspend flow
	check_update_flage(ts);
	
#endif

	/*finget and pen input event register*/
	rc = elan_request_pen_input_dev(ts);
	if ( rc ) {
		dev_err(&ts->client->dev,
				"[elan]: %s pen input event request failed.\n",
				__func__);
		goto exit_pen_input_dev_failed;
		
	}

	rc = elan_request_finger_input_dev(ts);
	if ( rc ) {
		dev_err(&ts->client->dev,
				"[elan]: %s finger input event request failed %d.\n",
				__func__, rc);
		goto exit_finger_input_dev_failed;
	}

	mutex_lock(&ts->irq_mutex);
	ts->irq_lock_flag = 0;
	mutex_unlock(&ts->irq_mutex);

	/*elan irq resgister*/
	rc = elan_ts_register_interrupt(private_ts);
	if ( rc ) {
		dev_err(&private_ts->client->dev,
				"[elan]: %s elan_ts_register_interrupt failed %d\n",
				__func__, rc);
		goto exit_register_interrupt_failed;
	}

#ifdef IAP_PORTION
	ts->power_lock = 0;
#endif

	return;

exit_register_interrupt_failed:
	input_unregister_device(ts->finger_idev);
exit_finger_input_dev_failed:
	input_unregister_device(ts->pen_idev);
exit_pen_input_dev_failed:
	return;
}


static int elan_ts_setup(struct elan_ts_data *ts)
{
	int err = 0;

	dev_err(&ts->client->dev, "[elan] setup hw reset\n");
	/*HW RESET TP and delay 200ms*/
	elan_ts_hw_reset(&ts->hw_info);
	msleep(500);

	err = elan__hello_packet_handler(ts->client, ts->chip_type);
	if ( err < 0 ) {
		dev_err(&ts->client->dev,
				"[elan error] %s, hello_packet_handler fail,err= %d\n",
				__func__,err);
		return err;
	} else {
		dev_err(&ts->client->dev,
				"[elan] %s,ic status = %s",
				__func__, err == FORCED_UPGRADE ? "recovery":"normal");
	}

	return err;
}

static int elan_iap_open(struct inode *inode, struct file *filp)
{
	struct elan_ts_data *ts = container_of(((struct miscdevice*)filp->private_data), struct elan_ts_data, firmware);	

	dev_dbg(&ts->client->dev,"%s enter\n", __func__);
	
	filp->private_data = ts;	
//	ts->int_val = 1;
//	ts->user_handle_irq = 1;
	return 0;
}

static int elan_iap_release(struct inode *inode, struct file *filp)
{
	dev_info(&private_ts->client->dev,"%s enter", __func__);

	filp->private_data = NULL;
	//private_ts->user_handle_irq = 0;
	//private_ts->int_val = 0;
	return 0;
}

static ssize_t elan_iap_write(struct file *filp, const char *buff, size_t count, loff_t *offp)
{
	int ret;
	char *tmp;
	struct elan_ts_data *ts = (struct elan_ts_data *)filp->private_data;
	struct i2c_client *client= ts->client;

	dev_info(&client->dev,"%s enter", __func__);
	if (count > 8192){
		count = 8192;
	}

	tmp = kmalloc(count, GFP_KERNEL);
	if (tmp == NULL){
		return -ENOMEM;
	}

	if (copy_from_user(tmp, buff, count)) {
		return -EFAULT;
	}

	ret = elan_i2c_send(tmp, count);
	if (ret != count){
		dev_err(&client->dev, "[elan]elan elan_i2c_send fail, ret=%d \n", ret);
	}

	kfree(tmp);

	return ret;
}

static ssize_t elan_iap_read(struct file *filp, char *buff, size_t count, loff_t *offp)
{
	char *tmp;
	int ret;
	long rc;
	struct elan_ts_data *ts = (struct elan_ts_data *)filp->private_data;
	struct i2c_client *client = ts->client;

	dev_info(&client->dev, "%s enter", __func__);

	if (count > 8192){
		count = 8192;
	}

	tmp = kmalloc(count, GFP_KERNEL);
	if (tmp == NULL){
		return -ENOMEM;
	}

	if (ts->user_handle_irq == 1) {
		wait_event_interruptible(ts->elan_userqueue, ts->int_val == 0);
	}

	ret = elan_i2c_recv(tmp, count);
	if (ret != count){
		dev_err(&client->dev, "[elan error]elan elan_i2c_recv fail, ret=%d \n", ret);
	}

	if (ret == count){
		rc = copy_to_user(buff, tmp, count);
	}
   
	if (ts->user_handle_irq == 1) {
		ts->int_val = 1;
	}

	kfree(tmp);
	return ret;
}

static long elan_iap_ioctl( struct file *filp, unsigned int cmd, unsigned long arg)
{
	int __user *ip = (int __user *)arg;
	struct elan_ts_data *ts = (struct elan_ts_data *)filp->private_data;
	struct i2c_client *client =  ts->client;

	dev_info(&client->dev, "%s enter cmd value %x\n", __func__,cmd);

	switch (cmd) {
	case IOCTL_I2C_SLAVE:
		dev_info(&client->dev, "pre addr is %X\n",  client->addr);
		client->addr = (int __user)arg;
		dev_info(&client->dev, "new addr is %X\n",  client->addr);
		break;
	case IOCTL_RESET:
		elan_ts_hw_reset(&ts->hw_info);
		break;
	case IOCTL_IAP_MODE_LOCK:
		if(private_ts->power_lock == 0){
			private_ts->power_lock = 1;
			elan_switch_irq(ts,0);
		}
		break;
        case IOCTL_IAP_MODE_UNLOCK:
		if(private_ts->power_lock == 1){
			private_ts->power_lock = 0;
			elan_switch_irq(ts,1);
		}
		break;
	case IOCTL_CHECK_RECOVERY_MODE:
		return private_ts->recover;
		break;
	case IOCTL_ROUGH_CALIBRATE:
		return elan_ts_calibrate(ts->client);
	case IOCTL_I2C_INT:
		put_user(gpio_get_value(ts->hw_info.intr_gpio), ip);
		break;
		case IOCTL_USER_HANDLE_IRQ:
			ts->user_handle_irq = 1;
			break;
		case IOCTL_KERN_HANDLE_IRQ:
			ts->user_handle_irq = 0;
	default:
		break;
	}
	return 0;
}

static unsigned int elan_iap_poll(struct file *filp, struct poll_table_struct *wait)
{
	int mask = 0;
	struct elan_ts_data *ts = (struct elan_ts_data *)filp->private_data;
	dev_info(&ts->client->dev, "[elan] polling int_val = %d\n", ts->int_val);

	poll_wait(filp,&ts->elan_userqueue, wait);
	if (ts->int_val == 0)
		mask |= POLLIN|POLLRDNORM;
	else if(ts->int_val == 1)
		mask |= POLLOUT|POLLWRNORM;

	return mask;	
}

struct file_operations elan_touch_fops = {
	.open			= elan_iap_open,
	.write			= elan_iap_write,
	.read			= elan_iap_read,
	.release		= elan_iap_release,
	.unlocked_ioctl		= elan_iap_ioctl,
	.compat_ioctl		= elan_iap_ioctl,
	.poll			= elan_iap_poll,
};


static void elan_touch_node_init(struct elan_ts_data *ts)
{

	elan_sysfs_attri_file(ts);

	/*creat dev/elan-iap node for fw operation*/
	ts->firmware.minor = MISC_DYNAMIC_MINOR;
	ts->firmware.name = "elan-iap";
	ts->firmware.fops = &elan_touch_fops;
	ts->firmware.mode = S_IFREG|S_IRWXUGO;

	if (misc_register(&ts->firmware) < 0)
		dev_err(&ts->client->dev, "misc_register failed!!\n");

	ts->p = proc_create("elan-iap", 0664, NULL, (const struct proc_ops *)&elan_touch_fops);
	if (ts->p == NULL)
		dev_err(&ts->client->dev, "[elan error] proc_create failed!!\n");
	else
		dev_info(&ts->client->dev, "proc_create ok!!\n");

	return;
}

static void elan_touch_node_deinit(struct elan_ts_data *ts)
{
	elan_sysfs_attri_file_remove(ts);
	misc_deregister(&ts->firmware);
	remove_proc_entry("elan-iap", NULL);
}
/*******************************************************
Function:
   	Power on  Funtion.
Input:
    ts: elan_ts_data struct.
    on: bool, true:on, flase:off
Output:
    Executive outcomes.
    0: succeed. otherwise: failed 
*******************************************************/
#if 1
static int elan_ts_power_on(struct elan_ts_data *ts, bool on)
{
	int ret = 0;
	
	if (!on)
		goto power_off;

	ret = regulator_enable(ts->vdd);
	if (ret) {
		dev_err(&ts->client->dev,
				"Regulator vdd enable failed ret = %d\n",ret);
		return ret;
	}
#if 0
	ret = regulator_enable(ts->vcc_i2c);
	if (ret) {
		dev_err(&ts->client->dev,
				"Regulator vcc_i2c enable failed ret = %d\n",ret);
		regulator_disable(ts->vdd);
	}
#endif
	return ret;

power_off:
	ret = regulator_disable(ts->vdd);
	if (ret) {
		dev_err(&ts->client->dev,
				"Regulator vdd disable failed ret = %d\n",ret);
		return ret;
	}
#if 0
	ret = regulator_disable(ts->vcc_i2c);
	if (ret) {
		dev_err(&ts->client->dev,
				"Regulator vcc_i2c disable failed ret = %d\n", ret);
		ret = regulator_enable(ts->vdd);
		if (ret)
			dev_err(&ts->client->dev,
					"Regulator vdd enable failed ret = %d\n", ret);
	}
#endif	
	return ret;
}

static int elan_power_initial(struct elan_ts_data *ts)
{
	int ret = 0;
	
	ts->vdd = regulator_get(&ts->client->dev, "vdd");
	if (IS_ERR(ts->vdd)) {
		ret = PTR_ERR(ts->vdd);
		dev_err(&ts->client->dev,
			"Regulator get failed vdd rc=%d\n", ret);
		return ret;
	}

#if 0
	if (regulator_count_voltages(ts->vdd) > 0) {
		ret = regulator_set_voltage(ts->vdd,ELAN_VTG_MIN_UV,
				ELAN_VTG_MAX_UV);
		if (ret) {
			dev_err(&ts->client->dev,
				"Regulator set_vtg failed vdd rc=%d\n", ret);
			goto reg_vdd_put;
		}
	}

	ts->vcc_i2c = regulator_get(&ts->client->dev, "vcc_i2c");
	if (IS_ERR(ts->vcc_i2c)) {
		ret = PTR_ERR(ts->vcc_i2c);
		dev_err(&ts->client->dev,
			"Regulator get failed vcc_i2c rc=%d\n", ret);
		goto reg_vdd_set_vtg;
	}

	if (regulator_count_voltages(ts->vcc_i2c) > 0) {
		ret = regulator_set_voltage(ts->vcc_i2c, ELAN_I2C_VTG_MIN_UV,
				ELAN_I2C_VTG_MAX_UV);
		if (ret) {
			dev_err(&ts->client->dev,
				"Regulator set_vtg failed vcc_i2c rc=%d\n", ret);
			goto reg_vcc_i2c_put;
		}
	}
#endif
	return ret;

#if 0		
reg_vcc_i2c_put:
	regulator_put(ts->vcc_i2c);
reg_vdd_set_vtg:
	if (regulator_count_voltages(ts->vdd) > 0)
		regulator_set_voltage(ts->vdd, 0, ELAN_VTG_MAX_UV);


reg_vdd_put:
	regulator_put(ts->vdd);
#endif		
	return ret;
}

static int elan_ts_set_power(struct elan_ts_data *ts, bool on)
{
	int ret = 0;
		
	if(!on) {
		ret = on;
		goto pwr_deinit;
	}
		
	/*initial power*/
	ret = elan_power_initial(ts);
	if(ret)
		goto elan_power_init_failed;
		
	/*power on*/
	ret = elan_ts_power_on(ts,on);
	if(ret)
		goto elan_power_on_failed;
		
	return ret;

elan_power_on_failed:
	regulator_put(ts->vdd);
	regulator_put(ts->vcc_i2c);
elan_power_init_failed:
pwr_deinit:
	return ret;
}
#endif
/*******************************************************
Function:
   	Initial gpio Funtion
Input:
    hw_info: ts_chip_hw_info struct
Output:
    Executive outcomes.
    0: succeed. otherwise: failed 
*******************************************************/

static int elan_ts_gpio_initial(struct ts_chip_hw_info *hw_info)
{
	int ret = 0;
	
	printk("[elan] request reset gpio\n");
	ret = gpio_request(hw_info->rst_gpio, "tp_reset");
	if (ret < 0) {
		pr_err("%s: request rst_gpio pin failed\n", __func__);
		goto free_rst_gpio;
	}
	
	gpio_direction_output(hw_info->rst_gpio, 1);

	printk("[elan] request interrupt gpio\n");
	/*set int pin input*/
	ret = gpio_request(hw_info->intr_gpio, "tp_irq");
	if (ret < 0) {
		pr_err("%s: request intr_gpio pin failed\n", __func__);
		goto free_irq_gpio;
	}
	gpio_direction_input(hw_info->intr_gpio);
	
	hw_info->irq_num = gpio_to_irq(hw_info->intr_gpio);
	
	

	return ret;
		
free_irq_gpio:
	if (gpio_is_valid(hw_info->intr_gpio)) 
		gpio_free(hw_info->intr_gpio);
free_rst_gpio:
	if (gpio_is_valid(hw_info->rst_gpio))
		gpio_free(hw_info->rst_gpio);
	return ret;
}

/*******************************************************
Function:
   	Get dts gpio num
Input:
    dev: device struct.
    hw_info: ts_chip_hw_info struct
Output:
    Executive outcomes.
    0: succeed. otherwise: failed 
*******************************************************/
#ifdef CONFIG_OF
static int elan_parse_dt(struct device *dev, 
			struct ts_chip_hw_info *chip_hw_info)
{
	int ret = 0;
	u32 data = 0;
	//struct device_node *node = NULL;
	struct elan_ts_data *ts =
		container_of(chip_hw_info, struct elan_ts_data, hw_info);
	//u32 lcm_coordinate[2] = {0};
	struct device_node *np = dev->of_node;
/*	
	node = of_find_compatible_node(NULL, NULL, "elan,ektf");
	if(node){
		dev_err(&ts->client->dev,"[elan]of_find_compatible_node of : %s\n", "elan,ektf");
		return -ENODEV;
	}
*/
	/*get irq gpio from dts*/
	chip_hw_info->intr_gpio = of_get_named_gpio_flags(np,
		"elan,irq-gpio", 0, NULL);
	if (!gpio_is_valid(chip_hw_info->intr_gpio)) {
		dev_err(&ts->client->dev, "[elan] hw_info->intr_gpio invalid\n");
		ret =  -EINVAL;
		goto request_intr_gpio_failed;
	}
		
	/*get reset gpio from dts*/
	chip_hw_info->rst_gpio = of_get_named_gpio_flags(np,
		"elan,rst-gpio", 0, NULL);
	if (!gpio_is_valid(chip_hw_info->rst_gpio)) {
		dev_err(&ts->client->dev, "[elan] hw_info->rst_gpio invalid\n");
		ret = -EINVAL;
		goto request_rst_gpio_failed;
	}
	
	/*get ic communicate protocol*/
	ret = of_property_read_u32(np, "chip_type", &data);
	if (ret == 0) {
		ts->chip_type = data;
		dev_info(&ts->client->dev,"[elan]:chip protocol_type=%s", ts->chip_type == 1 ? "HID IIC":"NORMAL IIC" );
	} else {
		ret = -EINVAL;
		goto read_chip_type_failed;	
	}

	/*get report protocol */
	ret = of_property_read_u32(np, "report_type", &data);
	if (ret == 0) {
		ts->report_type = data;
		dev_info(&ts->client->dev,"[elan]:report protocol_type=%s", ts->report_type == 1?"B protocol":"A protocol");
	} else {
		ret = -EINVAL;//hw_info->rst_gpio;
		goto read_report_type_failed;	
	}
	ts->hw_info.screen_x = 2160; //1728; //2160;
	ts->hw_info.screen_y = 1440; //2368; //1440
#if 0
	/*get lcm coordinate*/
	ret = of_property_read_u32_array(np, "lcm_resolution", lcm_coordinate,sizeof(lcm_coordinate));
	if (ret == 0) {
		ts->hw_info.screen_x = lcm_coordinate[0];
		ts->hw_info.screen_y = lcm_coordinate[1];
		dev_info(&ts->client->dev,"[elan]:LCM RESOLUTION X:Y=%d:%d,", ts->hw_info.screen_x, ts->hw_info.screen_y);
	} else {
		ret = -EINVAL;//hw_info->rst_gpio;
		goto read_lcm_res_failed;	
	}
#endif
	return ret;

//read_lcm_res_failed:
read_report_type_failed:
read_chip_type_failed:
	if (gpio_is_valid(chip_hw_info->rst_gpio)) 
		gpio_free(chip_hw_info->rst_gpio);
request_rst_gpio_failed:
	if (gpio_is_valid(chip_hw_info->intr_gpio)) 
		gpio_free(chip_hw_info->intr_gpio);
request_intr_gpio_failed:	
	return ret;
}

#endif

/*******************************************************
Function:
   	Get platform data Funtion
Input:
    ts: elan_ts_data struct.
Output:
    Executive outcomes.
    0: succeed. otherwise: failed 
*******************************************************/

static int elan_ts_hw_initial(struct elan_ts_data *ts)
{
	int ret = 0;
	struct i2c_client *client = ts->client;
	struct ts_chip_hw_info *hw_info;

	hw_info = &(ts->hw_info);
#if 0
	hw_info = devm_kzalloc(&client->dev,sizeof(struct ts_chip_hw_info), GFP_KERNEL);
	if (!hw_info) {
		dev_err(&client->dev,
				"ETP Failed to allocate memory for hw_info\n");
		return -ENOMEM;
	}
	else {
        hw_info = client->dev.platform_data;
		ts->chip_type = 1; /*1:HID IIC, 0: NORMAL IIC*/
		ts->report_type = 1; /*1:B protocol, 0:A protocol*/
  	}
#endif


#ifdef CONFIG_OF	
	if (client->dev.of_node) {
        ret = elan_parse_dt(&client->dev, hw_info);
        if (ret)
            return ret;		
  	}
#endif		
	

	ts->fw_store_type = FROM_DRIVER_FIRMWARE; //define get fw solution
	ts->user_handle_irq = 0;
	//ts->irq_lock_flag = 0;

	//ts->hw_info = *hw_info;
	ret = elan_ts_gpio_initial(&ts->hw_info);
	if (ret) 
	dev_err(&client->dev, "gpio initial failed ret = %d\n",ret);

	dev_err(&client->dev, "[elan] rst = %d, int = %d, irq=%d\n",hw_info->rst_gpio, hw_info->intr_gpio,hw_info->irq_num);
	dev_err(&client->dev, "[elan] lcm_x = %d, lcm_y = %d\n",hw_info->screen_x, hw_info->screen_y);
    
	return ret;
}

static void elan_ts_hw_deinit(struct elan_ts_data *ts)
{

	regulator_put(ts->vdd);
	regulator_put(ts->vcc_i2c);	
	if (gpio_is_valid(ts->hw_info.intr_gpio)) 
		gpio_free(ts->hw_info.intr_gpio);
	
	if (gpio_is_valid(ts->hw_info.rst_gpio))
		gpio_free(ts->hw_info.rst_gpio);
}

/*******************************************************
Function:
    I2c probe.
Input:
    client: i2c device struct.
    id: device id.
Output:
    Executive outcomes.
    0: succeed.
*******************************************************/
static int elan_ts_probe(struct i2c_client *client,
               const struct i2c_device_id *id) 
{
//#define SM_BUS
	int err;
#ifdef SM_BUS
	union i2c_smbus_data dummy;
#endif
	struct elan_ts_data *ts;
	int retry = 0;

       	printk("elan %s() %d\n", __func__, __LINE__);

	/*check i2c bus support fuction*/
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev,
			"i2c check functionality error\n");
			return -ENXIO;
	}
	
	/*kzalloc struct elan_ts_data memory */
	ts = devm_kzalloc(&client->dev, sizeof(struct elan_ts_data), GFP_KERNEL);
	if (!ts) {
		dev_err(&client->dev,
			"%s: allocate elan_ts_data failed", __func__);
		return -ENOMEM;		
	}

	printk("elan %s() %d\n", __func__, __LINE__);

	ts->client = client;
	i2c_set_clientdata(client, ts);
	private_ts = ts;	
	
	
	/*get hw info and initial*/
	err = elan_ts_hw_initial(ts);
	if(err) {
		dev_err(&client->dev, "%s hw initial failed\n",__func__);
		goto free_client_data;
	}

	printk("elan %s() %d\n", __func__, __LINE__);
	/*set power & power on*/
#if 1
	err = elan_ts_set_power(ts,1);
	if (err) {
		dev_err(&client->dev, "%s power seting  failed\n",__func__);
		goto free_io_port;
	}
	msleep(100);
#endif		
	
	printk("elan %s() %d\n", __func__, __LINE__);
	/*check elan ic in bus or not*/
#ifdef SM_BUS
	if (i2c_smbus_xfer(client->adapter, client->addr, 0,
		I2C_SMBUS_READ, 0, I2C_SMBUS_BYTE, &dummy) < 0) {
		dev_err(&client->dev, "nothing at this address 0x%x\n", client->addr);
		goto free_power_set;
	}		
#endif
	
	/*elan ic transfer initial*/
	ts->ops = &elan_ops;		


	printk("elan %s() %d\n", __func__, __LINE__);
	/*check elan ic status*/
	err = elan_ts_setup(ts);
	if (err < 0) {
		dev_err(&client->dev, "%s ic initial failed\n",__func__);
		goto err_no_elan_chip;
	}
	
	/*check rek */
	if(COMPARE_UPGRADE == ts->recover) {
		for (retry = 0; retry < 3; retry++) {
			err = elan_ts_check_calibrate(ts->client); /*ic reponse rek count,count != 0xff? "ok":"failed" */
			if (err) { 
				dev_err(&ts->client->dev, "[elan] check rek failed, retry=%d\n",retry);
				err = elan_ts_calibrate(ts->client);
				if (err) {
					dev_err(&ts->client->dev, "[elan]calibrate failed, retry=%d\n",retry);
				} else 
					break;
			} else
				break;
		}		
	}
	
	/*creat dev node & sysfs node for fw operatrion*/
	elan_touch_node_init(ts);
			

	printk("elan %s() %d\n", __func__, __LINE__);
	/*get fw infomation, register input dev, register interrupt*/
	INIT_DELAYED_WORK(&ts->init_work, elan_ic_init_work);
	ts->init_elan_ic_wq = create_singlethread_workqueue("init_elan_ic_wq");
	if (IS_ERR(ts->init_elan_ic_wq)) {
		err = PTR_ERR(ts->init_elan_ic_wq);
		goto err_ic_init_failed;
	}
	queue_delayed_work(ts->init_elan_ic_wq, &ts->init_work, delay);


	printk("elan %s() %d\n", __func__, __LINE__);
	/*report work thread*/
	ts->elan_wq = create_singlethread_workqueue("elan_wq");
	if (IS_ERR(ts->elan_wq)) {
		err = PTR_ERR(ts->elan_wq);
		dev_err(&client->dev,
				"[elan error] failed to create kernel thread: %d\n",
				err);
		goto err_create_workqueue_failed;
	}
	INIT_WORK(&ts->ts_work, elan_ts_work_func);

	/*set print log level*/
	ts->level = TP_DEBUG;

	/*initial wait queue for userspace*/
	init_waitqueue_head(&ts->elan_userqueue);
	/*lcm callback resume and suspend*/
#if defined(CONFIG_FB)
	ts->fb_notif.notifier_call = fb_notifier_callback;
	err = fb_register_client(&ts->fb_notif);
	if (err)
		dev_err(&client->dev,"[FB]Unable to register fb_notifier: %d", err);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = elan_ts_early_suspend;
	ts->early_suspend.resume = elan_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

	printk("elan %s() %d probe success!\n", __func__, __LINE__);
	return err;

err_create_workqueue_failed:
	destroy_workqueue(ts->elan_wq);
err_ic_init_failed:
	destroy_workqueue(ts->init_elan_ic_wq);
err_no_elan_chip:
#ifdef SM_BUS
free_power_set:
#endif
	regulator_put(ts->vdd);
	regulator_put(ts->vcc_i2c);
#if 1	
free_io_port:
	if (gpio_is_valid(ts->hw_info.intr_gpio)) 
		gpio_free(ts->hw_info.intr_gpio);
	
	if (gpio_is_valid(ts->hw_info.rst_gpio))
		gpio_free(ts->hw_info.rst_gpio);
#endif
free_client_data:
	i2c_set_clientdata(client,NULL);

	return err;
}


/*******************************************************
Function:
    Elan touchscreen driver release function.
Input:
    client: i2c device struct.
Output:
    Executive outcomes. 0---succeed.
*******************************************************/

static int elan_ts_remove(struct i2c_client *client)
{
	struct elan_ts_data *ts = i2c_get_clientdata(client);

	elan_ts_hw_deinit(ts);
	elan_touch_node_deinit(ts);

	input_unregister_device(ts->finger_idev);
	input_unregister_device(ts->pen_idev);
	free_irq(ts->hw_info.irq_num,(void *)elan_ts_irq_handler);

	if (!IS_ERR(ts->init_elan_ic_wq)) {
		destroy_workqueue(ts->init_elan_ic_wq);
	}
	
	if (!IS_ERR(ts->elan_wq)) {
		destroy_workqueue(ts->elan_wq);
	}
#if defined(CONFIG_FB)
	fb_unregister_client(&ts->fb_notif);
#endif 

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif
	i2c_set_clientdata(client,NULL);
	return 0;
}


static void elan_release_point(void)
{
	struct input_dev *fidev;
	struct input_dev *pidev;
	int i = 0;

	if (private_ts->finger_idev && private_ts->pen_idev) {
		fidev = private_ts->finger_idev;
		pidev = private_ts->pen_idev;

	if (private_ts->report_type == PROTOCOL_TYPE_B) {
		for (i = 0; i < 10; i++) {
			input_mt_slot(fidev, i);
			input_mt_report_slot_state(fidev, MT_TOOL_FINGER, 0);
		}

		if (private_ts->report.tool_type == ELAN_PEN) {
			 input_mt_slot(pidev, 0);
			 input_mt_report_slot_state(pidev, MT_TOOL_PEN, false);
		}

	} else {
		input_mt_sync(fidev);
		input_report_key(fidev, BTN_TOUCH, 0);

		if (private_ts->report.tool_type == ELAN_PEN) {
			input_mt_sync(pidev);
			input_report_key(pidev, BTN_TOUCH, 0);
		}
	}
	input_sync(fidev);

	if (private_ts->report.tool_type == ELAN_PEN) {
		input_sync(pidev);
		}
	} else {
		dev_err(&private_ts->client->dev, "Noting done\n");
	}
	return;
}

static int elan_ts_set_power_state(struct i2c_client *client, int state)
{
	int err = 0;
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	/*send ic sleep/wake up  command*/
	uint8_t hid_cmd[HID_CMD_LEN] = {0x04, 0x00, 0x23, 0x00, 0x03, 0x00, 0x04, CMD_W_PKT, 0x50, 0x00, 0x01};
	uint8_t cmd[4] = {CMD_W_PKT, 0x50, 0x00, 0x01};

	if (ts->chip_type == HID_TYPE_PROTOCOL) {
		hid_cmd[8] |= (state << 3);
		err = ts->ops->send(hid_cmd, sizeof(hid_cmd));
		if (err != sizeof(hid_cmd)) {
			err = -EINVAL;
			goto err_set_power_state;
		}
	} else {
		cmd[1] |= (state << 3);
		err = ts->ops->send(cmd,sizeof(cmd));
		if (err != sizeof(cmd)) {
			err = -EINVAL;
			goto err_set_power_state;
		}
	}

	print_log(ts->level, "[elan] set power stats success\n");
	return 0;

err_set_power_state:
	return err;
}


static int elan_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	int err = 0;
	int retry = RETRY_TIMES;

	//if do fw upgrade, don't sleep
	if (ts->power_lock == 0) {
		dev_err(&client->dev, "[elan] %s suspend flow \n", __func__);
		elan_switch_irq(ts, 0);
try_set_power:  // if system would not power off, must do this and check
		err = elan_ts_set_power_state(ts->client, PWR_STATE_DEEP_SLEEP);
		if (err) {
			dev_err(&client->dev, "[elan] set power stats failed err = %d\n", err);
			if ( (retry --) > 0)
				goto try_set_power;
		}
	
		/*release finger*/
		elan_release_point();

		/*power off*/
		elan_ts_power_on(ts,false);
	} else {
		dev_err(&client->dev, "[elsn] %s Nothing Done!!\n",__func__);
	}

	return 0;
}


static int elan_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	int err = 0;
	int retry = RETRY_TIMES;


	

	/*
	** enable irq, set ic status, reset ic
	**/
	if (ts->power_lock == 0) {
		dev_err(&client->dev, "[elan] reset gpio to resum tp\n");
		/*device power on*/
		elan_ts_power_on(ts,true);
	
		/*delay for ic initial*/
		msleep(100);
		
reset_power_state:
		err = elan_ts_set_power_state(ts->client, PWR_STATE_NORMAL);
		if (err) {
			dev_err(&client->dev, "[elan]%s set power stata failed!!\n",__func__);
			if ((retry--) > 0)
				goto reset_power_state;
			else
				elan_ts_hw_reset(&ts->hw_info);
		} 
		/*release point*/
		elan_release_point();
		elan_switch_irq(ts, 1);
	} else {
		dev_err(&client->dev, "[elsn] %s Nothing Done!!\n",__func__);
	}
	
	

	return 0;
}

/*******************************************************
Function:
   fb_notifier_callback function.
Input:
    self: 	notifier_block struct.
    event: 	unsigned long.
    data: 	void
Output:
    0.
*******************************************************/
#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
                 unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct elan_ts_data *ts =
	container_of(self, struct elan_ts_data, fb_notif);

	if (evdata && evdata->data && event == FB_EVENT_BLANK &&
	    ts && ts->client) {
		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK)
            		elan_ts_resume(&ts->client->dev);
        	else if (*blank == FB_BLANK_POWERDOWN)
			elan_ts_suspend(&ts->client->dev);
	}

	return 0;
}

#elif defined(CONFIG_HAS_EARLYSUSPEND)
/*******************************************************
Function:
    Early suspend function.
Input:
    h: early_suspend struct.
Output:
    None.
*******************************************************/
static void elan_ts_early_suspend(struct early_suspend *h)
{
	struct elan_ts_data *ts;
	ts = container_of(h, struct elan_ts_data, early_suspend);
	elan_ts_suspend(&ts->client->dev);
}

/*******************************************************
Function:
    Late resume function.
Input:
    h: early_suspend struct.
Output:
    None.
*******************************************************/

static void elan_ts_late_resume(struct early_suspend *h)
{
	struct elan_ts_data *ts;
	ts = container_of(h, struct elan_ts_data, early_suspend);
	elan_ts_resume(&ts->client->dev);
}
#endif/* !CONFIG_HAS_EARLYSUSPEND && !CONFIG_FB*/

#ifdef CONFIG_PM
static const struct dev_pm_ops elan_ts_dev_pm_ops = {
#if (!defined(CONFIG_FB) && !defined(CONFIG_HAS_EARLYSUSPEND))                                                                        
	.suspend = elan_ts_suspend,
	.resume = elan_ts_resume,                                                                                                     
#endif
};                                                                                                                                    
#endif

static const struct i2c_device_id elan_ts_id[] = {
	{ ELAN_TS_NAME, 0 },
	{ }
};

#ifdef CONFIG_OF
static const struct of_device_id elan_of_match[] = {
	{.compatible = "elan,ektf"},
	{},
};
MODULE_DEVICE_TABLE(of, elan_of_match);
#endif

static struct i2c_driver elan_ts_driver = {
	.probe		= elan_ts_probe,
	.remove		= elan_ts_remove,
	.id_table	= elan_ts_id,
	.driver		= {
		.name	= ELAN_TS_NAME,
#ifdef CONFIG_OF
		.of_match_table = elan_of_match,
#endif
#ifdef CONFIG_PM
		.pm = &elan_ts_dev_pm_ops,
#endif
	},	
};

static int __init elan_ts_init(void)
{
	int ret = 0;
	 
	ret = i2c_add_driver(&elan_ts_driver);
	return ret;
}

static void __exit elan_ts_exit(void)
{
	i2c_del_driver(&elan_ts_driver);
	return;
}

module_init(elan_ts_init);
module_exit(elan_ts_exit);
MODULE_DESCRIPTION("ELAN HID-I2C and I2C Touchscreen Driver");
MODULE_AUTHOR("Minger Zhang <chuming.zhang@elanic.com.cn>");
MODULE_LICENSE("GPL v2");
